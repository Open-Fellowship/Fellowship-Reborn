/* patch.c: the verify-before-write core, tested two ways.
 *
 * The first half checks the guards on their own: does validate see a match, does write_expect
 * refuse a site that is not what it was told, does repoint_operand decline the wrong old value,
 * is a second run of the same patch idempotent. Those are byte comparisons against a buffer, so
 * they run anywhere.
 *
 * The second half is the one worth having. It builds three real functions in executable memory,
 * wires a `call` to one of them, RUNS it, then uses patch_redirect_call to move the call to a
 * different function and runs it again to prove the redirect took. It then feeds the wrong
 * expected-target and proves the redirect is declined and the call is left alone. That last check
 * is the guard the first audit asked for and the code now has: a call redirect that does not
 * confirm where the call currently goes will silently steal a site from whatever owned it, and
 * the only way to know the guard actually fires is to execute the thing.
 *
 * Everything here is 32-bit x86 machine code by hand, which is what the process is (configure
 * with -A Win32), so the emitted `call rel32` and the functions it reaches are all in range.
 */
#include "unittest.h"

#include "common/patch.h"
#include "common/trampoline.h"

#include <stdint.h>
#include <string.h>

typedef int (__cdecl *returns_int_fn)(void);

/* mov eax, <value> ; ret. Five bytes plus one. Writes into `at` and returns its length. */
static size_t write_return_constant(uint8_t *at, uint32_t value)
{
    at[0] = 0xB8;                        /* mov eax, imm32 */
    memcpy(at + 1, &value, sizeof(value));
    at[5] = 0xC3;                        /* ret */
    return 6;
}

int main(void)
{
    ut_section("validate sees a match and only a match");
    {
        uint8_t       cell[4]     = { 0x8B, 0x0D, 0x00, 0x10 };
        const uint8_t expected[4] = { 0x8B, 0x0D, 0x00, 0x10 };
        const uint8_t wrong[4]    = { 0x8B, 0x0D, 0x00, 0x20 };

        ut_check(patch_validate_bytes((uintptr_t)cell, expected, sizeof(expected)),
                 "identical bytes validate");
        ut_check(!patch_validate_bytes((uintptr_t)cell, wrong, sizeof(wrong)),
                 "a single differing byte does not validate");
        ut_check(!patch_validate_bytes((uintptr_t)cell, expected, 0),
                 "a zero-length validate is refused, not trivially true");
    }

    ut_section("write_expect writes only what it was promised, and is idempotent");
    {
        uint8_t       site[2]        = { 0x74, 0x06 };   /* je +6 */
        const uint8_t expected[2]    = { 0x74, 0x06 };
        const uint8_t replacement[2] = { 0xEB, 0x06 };   /* jmp +6 */
        patch_result_t r;

        r = patch_write_expect((uintptr_t)site, expected, replacement, sizeof(site));
        ut_check(r == PATCH_RESULT_OK, "a matching site is patched");
        ut_check(site[0] == 0xEB && site[1] == 0x06, "the replacement bytes are in place");

        /* Run it again. The site now holds the replacement, not the expected, and the code should
         * recognise its own finished work as done, not as a mismatch. */
        r = patch_write_expect((uintptr_t)site, expected, replacement, sizeof(site));
        ut_check(r == PATCH_RESULT_OK, "a second run finds the patch already applied and is ok");

        /* A site that is neither the expected nor the replacement is a different build, and must
         * be refused without a write. A fresh site is used, because the one above now holds the
         * replacement and would correctly be recognised as already patched. */
        {
            uint8_t other[2] = { 0x90, 0x90 };   /* neither `expected` nor `replacement` */

            r = patch_write_expect((uintptr_t)other, expected, replacement, sizeof(other));
            ut_check(r == PATCH_RESULT_UNEXPECTED_BYTES,
                     "a site that matches neither expected nor replacement is declined");
            ut_check(other[0] == 0x90 && other[1] == 0x90,
                     "and the declined site is left exactly as it was");
        }
    }

    ut_section("repoint_operand checks the old value before believing it");
    {
        uint32_t       operand = 0x00860000u;
        patch_result_t r;

        r = patch_repoint_operand((uintptr_t)&operand, 0x00860000u, 0x00870000u);
        ut_check(r == PATCH_RESULT_OK && operand == 0x00870000u,
                 "the operand is moved when the old value matches");

        r = patch_repoint_operand((uintptr_t)&operand, 0x00870000u, 0x00870000u);
        ut_check(r == PATCH_RESULT_OK && operand == 0x00870000u,
                 "repointing to where it already points is a no-op ok, not a mismatch");

        r = patch_repoint_operand((uintptr_t)&operand, 0x00860000u, 0x00880000u);
        ut_check(r == PATCH_RESULT_UNEXPECTED_BYTES && operand == 0x00870000u,
                 "a wrong old value is declined and the operand is left untouched");
    }

    ut_section("a call redirect, built and executed");
    {
        uint8_t *code = (uint8_t *)trampoline_alloc(64);

        ut_check(code != NULL, "executable memory was allocated for the test");
        if (code != NULL) {
            /* Layout inside the block:
             *   +0x00  callee A: mov eax, 0xAA ; ret
             *   +0x10  callee B: mov eax, 0xBB ; ret
             *   +0x20  call site: call A ; ret          (the thing we redirect) */
            uint8_t       *callee_a   = code + 0x00;
            uint8_t       *callee_b   = code + 0x10;
            uint8_t       *call_site  = code + 0x20;
            returns_int_fn run        = (returns_int_fn)(void *)call_site;
            int32_t        to_a;
            patch_result_t r;

            write_return_constant(callee_a, 0xAA);
            write_return_constant(callee_b, 0xBB);

            /* call rel32 to A, then ret. rel32 measures from the byte after the call. */
            to_a = (int32_t)((uintptr_t)callee_a - ((uintptr_t)call_site + 5));
            call_site[0] = 0xE8;
            memcpy(call_site + 1, &to_a, sizeof(to_a));
            call_site[5] = 0xC3;

            ut_check(run() == 0xAA, "before any patch, the call site runs callee A");

            /* Redirect A -> B, telling the redirect the truth about where it currently points. */
            r = patch_redirect_call((uintptr_t)call_site, (uintptr_t)callee_a, callee_b);
            ut_check(r == PATCH_RESULT_OK, "a redirect with the correct expected target succeeds");
            ut_check(run() == 0xBB, "and the call site now runs callee B, proven by running it");

            /* Now try to redirect again claiming it still points at A. It points at B, so the
             * guard must decline and leave the site running B. This is the audit-1 fix under
             * live fire: without the expected-target check this would silently succeed. */
            r = patch_redirect_call((uintptr_t)call_site, (uintptr_t)callee_a, callee_a);
            ut_check(r == PATCH_RESULT_UNEXPECTED_BYTES,
                     "a redirect whose expected target is wrong is declined");
            ut_check(run() == 0xBB, "and the call site is left untouched, still running B");

            /* Passing 0 means 'target already established elsewhere' and skips the check; it must
             * still verify the opcode is a call. Point it at the middle of callee A, which is not
             * an E8, and it must refuse. */
            r = patch_redirect_call((uintptr_t)(callee_a + 1), 0, callee_b);
            ut_check(r == PATCH_RESULT_UNEXPECTED_BYTES,
                     "a site that does not hold an E8 call is refused even with the check skipped");
        }
    }

    return ut_summary("patch");
}

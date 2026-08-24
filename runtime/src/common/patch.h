/* patch.h: every write into engine code or engine data goes through here.
 *
 * 1. VALIDATE BEFORE WRITING. That check is also what makes a patch idempotent.
 * 2. WRITE THE WHOLE WORD, not a byte of it.
 * 3. REPOINT, do not assume: refuse when the operand does not hold what the caller expected.
 *
 * README.md says what each of those cost to learn.
 */
#ifndef COMMON_PATCH_H
#define COMMON_PATCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum patch_result {
    PATCH_RESULT_OK,
    PATCH_RESULT_INVALID_ARGUMENT,
    PATCH_RESULT_UNEXPECTED_BYTES,
    PATCH_RESULT_PROTECTION_FAILED
} patch_result_t;

const char *patch_result_text(patch_result_t result);

/* True when the bytes at `address` equal `expected`. Refuses (false) when the range is not
 * readable, rather than faulting. */
bool patch_validate_bytes(uintptr_t address, const uint8_t *expected, size_t size);

/* Unconditional write; restores the original page protection afterwards. */
patch_result_t patch_write_bytes(uintptr_t address, const void *data, size_t size);

/* The form almost every call site should use: write `replacement` only if the target currently
 * holds `expected`. Returns PATCH_RESULT_UNEXPECTED_BYTES and logs what was found otherwise. */
patch_result_t patch_write_expect(uintptr_t address, const uint8_t *expected,
                                  const uint8_t *replacement, size_t size);

/* Rewrites a 32-bit absolute memory operand: the address field of an instruction, not the
 * instruction. `operand_address` points at the field itself. */
patch_result_t patch_repoint_operand(uintptr_t operand_address, uint32_t expected_old,
                                     uint32_t new_value);

/* Redirects an existing `call rel32` to `new_target`, keeping the E8. The opcode is verified
 * first. This is how a single call site is diverted without touching the callee, which matters
 * whenever the callee has other callers that must stay untouched. */
/* `expected_target` is the callee the call is believed to hold. It is decoded from the
 * displacement that is already there and compared before anything is written, so a build
 * whose call goes somewhere else is declined instead of quietly losing its own callee.
 * Pass 0 only where the target has already been established some other way. */
patch_result_t patch_redirect_call(uintptr_t call_address, uintptr_t expected_target,
                                   const void *new_target);

/* Writes `E9 rel32` at `address` and pads to `size` with NOPs. `size` must be >= 5, and the
 * caller is responsible for it being an instruction boundary. */
patch_result_t patch_write_jump(uintptr_t address, const void *target, size_t size);

#endif /* COMMON_PATCH_H */

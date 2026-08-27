/* emit.c: the little assembler that every hand-built stub in the tree runs through.
 *
 * Nothing here needs the game or even Windows: emit writes bytes into a caller's buffer and does
 * displacement arithmetic, and both have a known right answer. The reason to test it anyway is
 * that a wrong displacement is the single most common way a runtime patch crashes, and it crashes
 * far from the mistake, so the arithmetic is exactly the thing worth pinning down in a place that
 * fails loudly at build time instead of quietly in the game.
 */
#include "unittest.h"

#include "common/emit.h"

#include <stdint.h>
#include <string.h>

int main(void)
{
    ut_section("bytes go in, in order, little-endian");
    {
        uint8_t buffer[16];
        emit_t  emit;

        emit_init(&emit, buffer, sizeof(buffer));
        emit_u8(&emit, 0xE9);
        emit_u32(&emit, 0x11223344u);

        ut_check(!emit_overflowed(&emit), "a write that fits does not set the overflow flag");
        ut_check(emit_size(&emit) == 5, "one byte plus one dword is five bytes");
        ut_check(buffer[0] == 0xE9, "the opcode byte is first");
        ut_check(buffer[1] == 0x44 && buffer[2] == 0x33 &&
                 buffer[3] == 0x22 && buffer[4] == 0x11,
                 "the dword is stored least-significant byte first");
    }

    ut_section("the buffer is a hard wall, not a suggestion");
    {
        uint8_t buffer[4];
        emit_t  emit;

        emit_init(&emit, buffer, sizeof(buffer));
        emit_u32(&emit, 0xDEADBEEFu);   /* exactly fills it */
        ut_check(!emit_overflowed(&emit), "a write that exactly fills the buffer is fine");

        emit_u8(&emit, 0x90);           /* one byte too many */
        ut_check(emit_overflowed(&emit), "the byte past the end sets overflow");
        ut_check(emit_size(&emit) == 4, "and is not counted: the size stays at capacity");
        ut_check(buffer[0] == 0xEF, "the bytes that did fit are untouched by the refused one");
    }

    ut_section("a rel32 jump measures from the end of its own instruction");
    {
        /* A stub sitting at 0x00400000 whose first act is to jump to 0x00401000. The jump is five
         * bytes (E9 + dword) and x86 measures the displacement from the byte AFTER it, so the
         * answer is 0x1000 - 5 = 0x0FFB. Getting the "+5" wrong is the classic off-by-an-
         * instruction that lands the branch inside the next opcode. */
        uint8_t   buffer[8];
        emit_t    emit;
        int32_t   displacement;
        uintptr_t stub   = 0x00400000u;
        uintptr_t target = 0x00401000u;

        emit_init(&emit, buffer, sizeof(buffer));
        emit_jump_rel32(&emit, stub, target);

        ut_check(emit_size(&emit) == 5, "E9 plus a dword is a five-byte jump");
        ut_check(buffer[0] == 0xE9, "the jump opcode is E9");
        memcpy(&displacement, buffer + 1, sizeof(displacement));
        ut_check(displacement == 0x0FFB, "displacement is target - (stub + 5)");
    }

    ut_section("a backward rel32 jump is a negative displacement");
    {
        uint8_t   buffer[8];
        emit_t    emit;
        int32_t   displacement;
        uintptr_t stub   = 0x00401000u;
        uintptr_t target = 0x00400000u;   /* earlier than the stub */

        emit_init(&emit, buffer, sizeof(buffer));
        emit_jump_rel32(&emit, stub, target);
        memcpy(&displacement, buffer + 1, sizeof(displacement));
        ut_check(displacement == -0x1005, "target - (stub + 5) is negative and exact");
    }

    ut_section("a short conditional branch patches its own reach");
    {
        /* emit a `jne .` and then two filler bytes, then resolve the branch to land just past
         * them. The distance is measured from the byte after the rel8, so two filler bytes means
         * a displacement of 2. */
        uint8_t buffer[8];
        emit_t  emit;
        size_t  slot;

        emit_init(&emit, buffer, sizeof(buffer));
        slot = emit_jcc_rel8(&emit, 0x75);   /* jne rel8 */
        emit_u8(&emit, 0x90);
        emit_u8(&emit, 0x90);
        emit_patch_rel8(&emit, slot);

        ut_check(buffer[0] == 0x75, "the conditional opcode is written through");
        ut_check(buffer[slot] == 2, "the rel8 counts the bytes between it and the target");
        ut_check(!emit_overflowed(&emit), "a reachable short branch does not overflow");
    }

    ut_section("a short branch that cannot reach is refused, not truncated");
    {
        /* A rel8 reaches at most 127 bytes forward. Fill past that and the resolve has to fail
         * loudly, and never write a wrapped byte that jumps somewhere random. */
        uint8_t buffer[200];
        emit_t  emit;
        size_t  slot;
        int     i;

        emit_init(&emit, buffer, sizeof(buffer));
        slot = emit_jcc_rel8(&emit, 0x74);          /* je rel8 */
        for (i = 0; i < 130; ++i) {
            emit_u8(&emit, 0x90);
        }
        emit_patch_rel8(&emit, slot);

        ut_check(emit_overflowed(&emit),
                 "a target more than 127 bytes away marks the buffer overflowed");
    }

    return ut_summary("emit");
}

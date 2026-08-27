/* memory.c: the readable-range and range-arithmetic guards.
 *
 * These decide whether the code is allowed to touch an address before it does, and the two things
 * that can go wrong are a range that wraps past the top of the address space and a read that
 * strays off a committed page. Both have deterministic answers against memory this test controls,
 * so they can be checked without the game: a real stack buffer is readable, address zero is not,
 * and a length that overflows the pointer is refused, not "checked" one page at a time
 * forever.
 */
#include "unittest.h"

#include "common/memory.h"

#include <stdint.h>
#include <string.h>

int main(void)
{
    ut_section("a real buffer is readable, in whole and in part");
    {
        uint8_t buffer[64];
        memset(buffer, 0, sizeof(buffer));

        ut_check(memory_is_readable_range((uintptr_t)buffer, sizeof(buffer)),
                 "a live stack buffer reads as readable");
        ut_check(memory_is_readable_range((uintptr_t)buffer + 8, 16),
                 "a sub-range inside it is readable too");
        ut_check(!memory_is_readable_range((uintptr_t)buffer, 0),
                 "a zero-length range is refused, not trivially true");
    }

    ut_section("obviously bad addresses are refused");
    {
        ut_check(!memory_is_readable_range(0, 4),
                 "address zero is not readable");
        /* A range whose start plus size wraps past the end of the address space is a caller
         * arithmetic error, and must be caught, never walked page by page forever. */
        ut_check(!memory_is_readable_range((uintptr_t)-16, 64),
                 "a range that wraps the address space is refused");
    }

    ut_section("the typed readers copy the bytes they claim to");
    {
        uint8_t  bytes[4] = { 0x78, 0x56, 0x34, 0x12 };
        uint8_t  got8     = 0;
        uint32_t got32    = 0;

        ut_check(memory_read_u8((uintptr_t)bytes, &got8) && got8 == 0x78,
                 "read_u8 returns the first byte");
        ut_check(memory_read_u32((uintptr_t)bytes, &got32) && got32 == 0x12345678u,
                 "read_u32 assembles the little-endian dword");
        ut_check(!memory_read((uintptr_t)bytes, NULL, 4),
                 "a NULL destination is refused");
        ut_check(!memory_read_u32(0, &got32),
                 "a read from address zero fails instead of faulting");
    }

    ut_section("inside-image needs a resolved image, and refuses without one");
    {
        /* In a test process host_image has not been pointed at a game, so base/end are zero and
         * every inside-image question must answer no, never guess. This is the guard that
         * keeps an unresolved site from being treated as valid. */
        ut_check(!memory_is_inside_image((uintptr_t)&main, 4),
                 "with no image resolved, nothing is inside it");
        ut_check(!memory_is_inside_image(0, 0),
                 "a zero-length inside-image query is refused");
    }

    return ut_summary("memory");
}

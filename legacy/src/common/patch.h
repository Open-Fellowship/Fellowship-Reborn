/* patch.h: every write into engine code or engine data goes through here.
 *
 * The three habits this module makes cheap, because they were expensive to learn:
 *
 *   1. VALIDATE BEFORE WRITING. patch_write_expect() reads the current bytes and refuses when
 *      they are not what the caller expected. That single check is also what makes a patch
 *      idempotent: a second run finds the new bytes, not the expected old ones, and declines.
 *
 *   2. WRITE THE WHOLE WORD, not a byte of it. Poking one byte of a little-endian immediate is
 *      how a limit you meant to lower becomes one you raised.
 *
 *   3. REPOINT, do not assume. patch_repoint_operand() rewrites the 32-bit absolute address
 *      field of an instruction and refuses when the field does not currently hold the address
 *      the caller expected, which is what stops a patch landing on a different build's operand.
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
patch_result_t patch_redirect_call(uintptr_t call_address, const void *new_target);

/* Writes `E9 rel32` at `address` and pads to `size` with NOPs. `size` must be >= 5, and the
 * caller is responsible for it being an instruction boundary. */
patch_result_t patch_write_jump(uintptr_t address, const void *target, size_t size);

#endif /* COMMON_PATCH_H */

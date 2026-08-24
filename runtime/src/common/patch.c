#include "common/patch.h"

#include "common/logging.h"
#include "common/memory.h"

#include <windows.h>

#include <stdio.h>
#include <string.h>

#define JMP_REL32_OPCODE 0xE9u
#define NOP_OPCODE       0x90u

const char *patch_result_text(patch_result_t result)
{
    switch (result) {
    case PATCH_RESULT_OK:                return "ok";
    case PATCH_RESULT_INVALID_ARGUMENT:  return "invalid argument";
    case PATCH_RESULT_UNEXPECTED_BYTES:  return "unexpected bytes";
    case PATCH_RESULT_PROTECTION_FAILED: return "VirtualProtect failed";
    default:                             return "?";
    }
}

/* Formats up to 16 bytes for a log line. Static buffer: log lines are built and consumed one at
 * a time on one thread, and two of these in a single printf would be a bug in the caller. */
static const char *hex_of(const uint8_t *bytes, size_t size)
{
    static char text[16 * 3 + 4];
    size_t      index;
    size_t      used = 0;

    if (size > 16) {
        size = 16;
    }
    for (index = 0; index < size; ++index) {
        used += (size_t)snprintf(text + used, sizeof(text) - used,
                                 (index == 0) ? "%02X" : " %02X", bytes[index]);
    }
    text[sizeof(text) - 1] = '\0';
    return text;
}

bool patch_validate_bytes(uintptr_t address, const uint8_t *expected, size_t size)
{
    uint8_t found[64];

    if (expected == NULL || size == 0 || size > sizeof(found)) {
        return false;
    }
    if (!memory_read(address, found, size)) {
        return false;
    }
    return memcmp(found, expected, size) == 0;
}

patch_result_t patch_write_bytes(uintptr_t address, const void *data, size_t size)
{
    DWORD previous;
    DWORD unused;

    if (data == NULL || size == 0) {
        return PATCH_RESULT_INVALID_ARGUMENT;
    }
    if (!VirtualProtect((LPVOID)address, size, PAGE_EXECUTE_READWRITE, &previous)) {
        return PATCH_RESULT_PROTECTION_FAILED;
    }
    memcpy((void *)address, data, size);
    VirtualProtect((LPVOID)address, size, previous, &unused);
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)address, size);
    return PATCH_RESULT_OK;
}

patch_result_t patch_write_expect(uintptr_t address, const uint8_t *expected,
                                  const uint8_t *replacement, size_t size)
{
    uint8_t found[64];

    if (expected == NULL || replacement == NULL || size == 0 || size > sizeof(found)) {
        return PATCH_RESULT_INVALID_ARGUMENT;
    }
    if (!memory_read(address, found, size)) {
        log_error("%08X is not readable, nothing written", (unsigned)address);
        return PATCH_RESULT_UNEXPECTED_BYTES;
    }
    if (memcmp(found, expected, size) != 0) {
        /* Two logs, because the two cases mean different things: already patched is fine and
         * idempotent, anything else means this is not the build we were told it was. */
        if (memcmp(found, replacement, size) == 0) {
            log_info("%08X already holds the patched bytes, left alone", (unsigned)address);
            return PATCH_RESULT_OK;
        }
        log_error("%08X holds %s", (unsigned)address, hex_of(found, size));
        log_error("   expected %s, refusing to write", hex_of(expected, size));
        return PATCH_RESULT_UNEXPECTED_BYTES;
    }
    return patch_write_bytes(address, replacement, size);
}

patch_result_t patch_repoint_operand(uintptr_t operand_address, uint32_t expected_old,
                                     uint32_t new_value)
{
    uint32_t current;

    if (!memory_read_u32(operand_address, &current)) {
        log_error("operand at %08X is not readable", (unsigned)operand_address);
        return PATCH_RESULT_UNEXPECTED_BYTES;
    }
    if (current == new_value) {
        log_info("operand at %08X already points at %08X",
                 (unsigned)operand_address, (unsigned)new_value);
        return PATCH_RESULT_OK;
    }
    if (current != expected_old) {
        log_error("operand at %08X points at %08X, expected %08X, refusing",
                  (unsigned)operand_address, (unsigned)current, (unsigned)expected_old);
        return PATCH_RESULT_UNEXPECTED_BYTES;
    }
    return patch_write_bytes(operand_address, &new_value, sizeof(new_value));
}

patch_result_t patch_redirect_call(uintptr_t call_address, uintptr_t expected_target,
                                   const void *new_target)
{
    uint8_t opcode;
    int32_t displacement;

    if (new_target == NULL) {
        return PATCH_RESULT_INVALID_ARGUMENT;
    }
    if (!memory_read_u8(call_address, &opcode)) {
        log_error("%08X is not readable", (unsigned)call_address);
        return PATCH_RESULT_UNEXPECTED_BYTES;
    }
    if (opcode != 0xE8u) {
        /* Not a call. Believing the displacement of something that is not a call is how a patch
         * lands in the middle of an unrelated instruction. */
        log_error("%08X holds %02X, not E8, refusing to redirect a call that is not there",
                  (unsigned)call_address, opcode);
        return PATCH_RESULT_UNEXPECTED_BYTES;
    }

    if (expected_target != 0) {
        uint32_t existing;

        if (!memory_read_u32(call_address + 1u, &existing)) {
            log_error("%08X has an unreadable displacement", (unsigned)call_address);
            return PATCH_RESULT_UNEXPECTED_BYTES;
        }
        /* Where the call goes today, worked out the same way the processor does it. */
        if ((uintptr_t)(call_address + 5u + (int32_t)existing) != expected_target) {
            log_error("%08X calls %08X, not the %08X this patch was measured against; "
                      "refusing to redirect a call to something else",
                      (unsigned)call_address,
                      (unsigned)(call_address + 5u + (int32_t)existing),
                      (unsigned)expected_target);
            return PATCH_RESULT_UNEXPECTED_BYTES;
        }
    }

    displacement = (int32_t)((uintptr_t)new_target - (call_address + 5u));
    return patch_write_bytes(call_address + 1u, &displacement, sizeof(displacement));
}

patch_result_t patch_write_jump(uintptr_t address, const void *target, size_t size)
{
    uint8_t branch[32];
    int32_t displacement;

    if (target == NULL || size < 5 || size > sizeof(branch)) {
        return PATCH_RESULT_INVALID_ARGUMENT;
    }

    displacement = (int32_t)((uintptr_t)target - (address + 5));

    branch[0] = JMP_REL32_OPCODE;
    memcpy(branch + 1, &displacement, sizeof(displacement));
    memset(branch + 5, NOP_OPCODE, size - 5);

    return patch_write_bytes(address, branch, size);
}

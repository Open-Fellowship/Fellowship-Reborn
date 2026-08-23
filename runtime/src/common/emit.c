#include "common/emit.h"

#include <string.h>

void emit_init(emit_t *emit, uint8_t *buffer, size_t capacity)
{
    emit->bytes      = buffer;
    emit->capacity   = capacity;
    emit->count      = 0;
    emit->overflowed = false;
}

static bool room_for(emit_t *emit, size_t size)
{
    if (emit->count + size > emit->capacity) {
        emit->overflowed = true;
        return false;
    }
    return true;
}

void emit_u8(emit_t *emit, uint8_t value)
{
    if (!room_for(emit, 1)) {
        return;
    }
    emit->bytes[emit->count++] = value;
}

void emit_u32(emit_t *emit, uint32_t value)
{
    if (!room_for(emit, sizeof(value))) {
        return;
    }
    memcpy(emit->bytes + emit->count, &value, sizeof(value));
    emit->count += sizeof(value);
}

void emit_bytes(emit_t *emit, const void *data, size_t size)
{
    if (!room_for(emit, size)) {
        return;
    }
    memcpy(emit->bytes + emit->count, data, size);
    emit->count += size;
}

size_t emit_jcc_rel8(emit_t *emit, uint8_t opcode)
{
    size_t placeholder;

    emit_u8(emit, opcode);
    placeholder = emit->count;
    emit_u8(emit, 0);
    return placeholder;
}

void emit_patch_rel8(emit_t *emit, size_t placeholder)
{
    ptrdiff_t distance;

    if (emit->overflowed || placeholder >= emit->count) {
        return;
    }
    distance = (ptrdiff_t)emit->count - (ptrdiff_t)(placeholder + 1);
    if (distance < 0 || distance > 127) {
        emit->overflowed = true;   /* a short branch that cannot reach is not a branch */
        return;
    }
    emit->bytes[placeholder] = (uint8_t)distance;
}

void emit_jump_rel32(emit_t *emit, uintptr_t stub_address, uintptr_t target)
{
    int32_t displacement;

    /* The displacement is measured from the END of this instruction, which is five bytes on from
     * where it starts, hence count + 5 rather than count. */
    displacement = (int32_t)(target - (stub_address + emit->count + 5u));
    emit_u8(emit, 0xE9);
    emit_u32(emit, (uint32_t)displacement);
}

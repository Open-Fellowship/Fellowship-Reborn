/* emit.h: assemble a short stub, byte by byte, without counting offsets by hand.
 *
 * For fixes that splice into the MIDDLE of an engine function, where the relocated instructions,
 * the new arithmetic and the jump back have to be laid out together. Getting it wrong by one
 * byte is a jump into the middle of an instruction. See README.md.
 */
#ifndef COMMON_EMIT_H
#define COMMON_EMIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct emit {
    uint8_t *bytes;
    size_t   capacity;
    size_t   count;
    bool     overflowed;
} emit_t;

void emit_init(emit_t *emit, uint8_t *buffer, size_t capacity);

void emit_u8   (emit_t *emit, uint8_t value);
void emit_u32  (emit_t *emit, uint32_t value);
void emit_bytes(emit_t *emit, const void *data, size_t size);

/* Emits `opcode` plus a placeholder rel8 and returns the index of that placeholder. */
size_t emit_jcc_rel8(emit_t *emit, uint8_t opcode);
/* Points a placeholder emitted earlier at the current position. */
void   emit_patch_rel8(emit_t *emit, size_t placeholder);

/* `E9 rel32` from the stub's eventual home to `target`. Call last: it needs the final length. */
void emit_jump_rel32(emit_t *emit, uintptr_t stub_address, uintptr_t target);

static inline bool   emit_overflowed(const emit_t *emit) { return emit->overflowed; }
static inline size_t emit_size      (const emit_t *emit) { return emit->count; }

#endif /* COMMON_EMIT_H */

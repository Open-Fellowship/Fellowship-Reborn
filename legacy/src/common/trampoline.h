/* trampoline.h: executable memory for stubs assembled at run time.
 *
 * The byte-patch generation of this project wrote its stubs into unused space inside the game's
 * own .text - the zero region at 0x51B302 in the executable, the slack past VirtualSize in the
 * rfl. That worked, and it is exactly what a loader removes the need for: caves are finite, two
 * fixes can collide in one, and a fix that owns a cave cannot be uninstalled.
 *
 * A plugin allocates its own page instead. 32-bit user address space is 2 GB, so an `E9 rel32`
 * always reaches from anywhere to anywhere and no proximity trick is needed.
 */
#ifndef COMMON_TRAMPOLINE_H
#define COMMON_TRAMPOLINE_H

#include <stddef.h>
#include <stdint.h>

/* Returns executable, writable memory of at least `size` bytes, or NULL. Never freed: a stub is
 * live for as long as the branch that points at it, and nothing removes those. */
void *trampoline_alloc(size_t size);

#endif /* COMMON_TRAMPOLINE_H */

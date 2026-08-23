/* early_trigger.h: load the plugins BEFORE the game has initialised anything.
 *
 * Waiting for DirectInput8Create is too late for a whole class of patch: graphics startup runs
 * first, so by then the display mode is chosen and the camera's viewport has already been built.
 *
 * A ONE-SHOT hook with no trampoline. DllMain saves the entry point's first five bytes and
 * writes a jump over them; the stub restores them, loads the plugins and jumps back. RESTORING
 * BEFORE JUMPING BACK is what makes this safe without decoding an instruction: the entry point
 * is re-executed from its first byte, so it does not matter if those five bytes ended mid
 * instruction. Only one thread exists at that point.
 *
 * The address comes from the PE header, not a byte pattern. See README.md.
 */
#ifndef LOADER_EARLY_TRIGGER_H
#define LOADER_EARLY_TRIGGER_H

#include <stdbool.h>

/* Safe to call from DllMain: it only reads PE headers and rewrites five bytes. It loads nothing.
 * Returns false when the entry point could not be hooked, in which case the DirectInput8Create
 * fallback is the only trigger and the graphics-related plugins will install too late. */
bool early_trigger_arm(void);

#endif /* LOADER_EARLY_TRIGGER_H */

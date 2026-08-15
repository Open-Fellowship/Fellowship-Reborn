/* early_trigger.h: load the plugins BEFORE the game has initialised anything.
 *
 * ==============================================================================================
 * WHY THE TRIGGER IS THE ENTRY POINT AND NOT THE PROXY CALL
 *
 * dinput8.dll is a static import of Fellowship.exe, so its DllMain runs during process
 * initialisation, before a single instruction of the game. Loading the plugins THERE is what
 * must not happen: LoadLibrary under the loader lock is how deadlocks are made. But we do not
 * have to load anything there. We only have to arrange to be called back at the earliest moment
 * the lock is gone, and that moment is the host's entry point.
 *
 * Waiting for DirectInput8Create instead would be simpler and is too late for a whole class of
 * patch. Fellowship.exe imports both d3d8.dll (Direct3DCreate8) and DINPUT8.dll
 * (DirectInput8Create), and graphics startup runs first: by the time input is created, the
 * display mode is chosen, the device exists and the camera's viewport has already been built
 * once. Anything that has to be in place before the first SetViewport therefore has to be
 * installed before the entry point runs the CRT, not at input time.
 *
 * ==============================================================================================
 * THE MECHANISM: a ONE-SHOT hook with no trampoline
 *
 *   in DllMain   save the first 5 bytes of the entry point, write `jmp our_stub` over them
 *   in the stub  restore those 5 bytes, load the plugins, jump back to the entry point
 *
 * Restoring before jumping back is what makes this safe without decoding a single instruction:
 * the entry point is re-executed from its first byte, so it does not matter whether those five
 * bytes happened to end mid-instruction. Only one thread exists at that point, so nothing can be
 * executing them while they are swapped.
 *
 * The address comes from the PE header (AddressOfEntryPoint), not from a byte pattern, so it is
 * build-independent and cannot be wrong.
 *
 * DirectInput8Create remains a fallback trigger. plugin_loader_run_once() is idempotent, so
 * whichever fires first wins and the other becomes a no-op.
 */
#ifndef LOADER_EARLY_TRIGGER_H
#define LOADER_EARLY_TRIGGER_H

#include <stdbool.h>

/* Safe to call from DllMain: it only reads PE headers and rewrites five bytes. It loads nothing.
 * Returns false when the entry point could not be hooked, in which case the DirectInput8Create
 * fallback is the only trigger and the graphics-related plugins will install too late. */
bool early_trigger_arm(void);

#endif /* LOADER_EARLY_TRIGGER_H */

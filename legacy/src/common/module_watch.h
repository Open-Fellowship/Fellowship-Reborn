/* module_watch.h: run something when Fellowship.rfl finally exists.
 *
 * The loader calls a plugin at the host's ENTRY POINT, which is before the CRT has run and long
 * before the game has loaded its game-code DLL. A plugin that patches Fellowship.exe can work
 * immediately; a plugin that patches Fellowship.rfl cannot, because at install time
 * GetModuleHandleA("Fellowship.rfl") returns NULL.
 *
 * This is the wait. It starts one thread, polls for the module, and calls `on_loaded` once with
 * the base address the moment it appears. The callback therefore runs on that thread, not on a
 * game thread, which is acceptable here for a specific reason: the rfl is loaded during
 * start-up, before the first frame is drawn, so the code being patched is not yet executing.
 * A plugin that wants to patch something already running every frame needs a different tool.
 *
 * Polling rather than hooking LoadLibrary, because the honest comparison is not "poll versus
 * elegant", it is "poll versus one more inline hook installed before the CRT has initialised".
 */
#ifndef COMMON_MODULE_WATCH_H
#define COMMON_MODULE_WATCH_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*module_watch_callback_t)(uintptr_t module_base);

/* Returns false if the thread could not be started, in which case nothing will ever be called.
 * `module_name` and the callback must outlive the process. */
bool module_watch_when_loaded(const char *module_name, module_watch_callback_t on_loaded,
                              unsigned timeout_ms);

#endif /* COMMON_MODULE_WATCH_H */

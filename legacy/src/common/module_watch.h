/* module_watch.h: run something when Fellowship.rfl finally exists.
 *
 * A plugin patching the rfl cannot work at install time, because GetModuleHandleA returns NULL
 * for it then. This polls, and calls back once on its own thread, which is acceptable only
 * because the rfl loads before the first frame is drawn. A plugin patching something already
 * running every frame needs a different tool. See README.md.
 */
#ifndef COMMON_MODULE_WATCH_H
#define COMMON_MODULE_WATCH_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*module_watch_callback_t)(uintptr_t module_base);

/* The module that actually holds the engine's code, by name.
 *
 * Normally that is Fellowship.rfl. It is not when engine/'s proxy is installed: the proxy takes
 * the name Fellowship.rfl and the retail engine is renamed Fellowship.orig.rfl beside it, so a
 * plugin that patches the engine by base+offset and looks up "Fellowship.rfl" finds a 123 KB
 * forwarding stub instead of 1.3 MB of engine and refuses to patch - which is what it should do,
 * but for the wrong reason.
 *
 * Resolved once, from whether Fellowship.orig.rfl exists beside the executable, because that file
 * is exactly what the proxy needs to work: if it is there the proxy is in use, and if it is not
 * then nothing has moved. Safe to call before the engine module is loaded, since it looks at the
 * disk rather than the module list, and safe to pass straight to module_watch_when_loaded.
 *
 * Requires host_image_resolve() to have run; returns "Fellowship.rfl" if it has not, which is the
 * pre-proxy behaviour and therefore the safe answer. */
const char *fellowship_rfl_module_name(void);

/* Returns false if the thread could not be started, in which case nothing will ever be called.
 * `module_name` and the callback must outlive the process. */
bool module_watch_when_loaded(const char *module_name, module_watch_callback_t on_loaded,
                              unsigned timeout_ms);

#endif /* COMMON_MODULE_WATCH_H */

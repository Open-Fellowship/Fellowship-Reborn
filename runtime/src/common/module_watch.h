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

/* Returns false if the thread could not be started, in which case nothing will ever be called.
 * `module_name` and the callback must outlive the process. */
bool module_watch_when_loaded(const char *module_name, module_watch_callback_t on_loaded,
                              unsigned timeout_ms);

#endif /* COMMON_MODULE_WATCH_H */

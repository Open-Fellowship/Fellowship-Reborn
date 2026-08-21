/* logging.h: one shared log file, one prefix per DLL.
 *
 * Every module in the process appends to <game>\fix_enhancers.log. The prefix is set once by
 * log_init() and written in front of every line automatically, so a call site cannot forget it
 * and two plugins cannot drift apart in how they spell their own name:
 *
 *     [loader]      plugin hud_scaling.dll         loaded at 10000000, calling open_fellowship_install
 *     [hud_scaling] WARNING: control_apply_scale did not match, HUD left alone
 *
 * The loader calls log_init("loader", true) first and truncates; every plugin calls
 * log_init("<plugin>", false) and appends. Each line is flushed, because the interesting case is
 * the one where the process dies immediately afterwards.
 */
#ifndef COMMON_LOGGING_H
#define COMMON_LOGGING_H

#include <stdbool.h>

/* `feature_name` must be a string literal or otherwise outlive the process. */
void log_init(const char *feature_name, bool truncate);
void log_shutdown(void);

void log_info(const char *format, ...);
void log_warning(const char *format, ...);
void log_error(const char *format, ...);

/* Full path of the log file, for messages that want to name it. Empty before log_init(). */
const char *log_path(void);

#endif /* COMMON_LOGGING_H */

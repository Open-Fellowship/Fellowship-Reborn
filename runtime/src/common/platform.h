/* platform.h: is this Windows, or is it Wine?
 *
 * Two fixes here are wrong on Windows and necessary on Wine. Wine exports wine_get_version from
 * ntdll and Windows does not, so this is a fact and not a heuristic. Answered once and
 * remembered. See README.md.
 */
#ifndef COMMON_PLATFORM_H
#define COMMON_PLATFORM_H

#include <stdbool.h>

bool platform_is_wine(void);

/* The version string Wine reports, or NULL on Windows. */
const char *platform_wine_version(void);

#endif /* COMMON_PLATFORM_H */

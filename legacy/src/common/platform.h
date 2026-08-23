/* platform.h: is this Windows, or is it Wine?
 *
 * Not a curiosity. Two fixes in this project are wrong on Windows and necessary on Wine, the
 * opening movies go through a runtime Wine only stubs, and exclusive full screen behaves
 * differently there, so "off by default unless this is Wine" is the honest default for both, and
 * a default that has to be typed into an ini is a default nobody gets.
 *
 * Wine exports `wine_get_version` from ntdll and Windows does not. That is the documented way to
 * ask, and it is a fact rather than a heuristic. Answered once and remembered.
 */
#ifndef COMMON_PLATFORM_H
#define COMMON_PLATFORM_H

#include <stdbool.h>

bool platform_is_wine(void);

/* The version string Wine reports, or NULL on Windows. */
const char *platform_wine_version(void);

#endif /* COMMON_PLATFORM_H */

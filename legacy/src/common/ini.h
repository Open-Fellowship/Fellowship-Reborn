/* ini.h: generic access to the shared configuration file.
 *
 * One file, <game>\fellowship_reborn.ini, one section per DLL, so a plugin cannot touch another
 * plugin's key by accident. This module knows nothing about what any key means; range checks and
 * validation belong to the plugin that owns the value. See README.md.
 */
#ifndef COMMON_INI_H
#define COMMON_INI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Full path of the configuration file, next to the host executable. Never NULL. The old name
 * fellowship_reborn.ini is still accepted WHEN THE NEW ONE IS ABSENT, so a rename does not revert
 * an existing install to the built-in defaults. See README.md. */
const char *ini_path(void);

/* True when the path above resolved to the old name. The loader says so in the log, because
 * "which of the two files am I actually editing" is not a question anyone should have to answer
 * by comparing timestamps. */
bool ini_using_legacy_name(void);

bool    ini_read_bool (const char *section, const char *key, bool default_value);
int32_t ini_read_int  (const char *section, const char *key, int32_t default_value);
float   ini_read_float(const char *section, const char *key, float default_value);

/* Always null-terminates `buffer`. Returns false when the key was absent, in which case
 * `default_value` has been copied instead. */
bool ini_read_string(const char *section, const char *key, const char *default_value,
                     char *buffer, size_t buffer_size);

/* `decimal_places` is clamped to 0..6. Returns false and leaves the file alone on failure: a
 * caller that logs "saved" without checking this is lying to the user. */
bool ini_write_float(const char *section, const char *key, float value, int decimal_places);

#endif /* COMMON_INI_H */

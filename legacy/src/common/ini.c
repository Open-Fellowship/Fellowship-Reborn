#include "common/ini.h"

#include "common/host_image.h"

#include <windows.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INI_FILE_NAME "open_fellowship.ini"

static char ini_file_path[MAX_PATH];

const char *ini_path(void)
{
    if (ini_file_path[0] == '\0') {
        host_image_resolve();
        snprintf(ini_file_path, sizeof(ini_file_path), "%s%s", host_directory(), INI_FILE_NAME);
        ini_file_path[sizeof(ini_file_path) - 1] = '\0';
    }
    return ini_file_path;
}

/* A sentinel nobody would type. GetPrivateProfileString cannot otherwise distinguish "the key
 * says nothing" from "there is no key", and those are different: the first is a deliberate empty
 * value and the second means fall back to the built-in default.
 *
 * Split across two string literals on purpose: "\x02absent" would be read as the single hex
 * escape \x02a, which is a different character and an error at -Werror. */
#define INI_ABSENT "\x01\x02" "absent"

bool ini_read_string(const char *section, const char *key, const char *default_value,
                     char *buffer, size_t buffer_size)
{
    char raw[1024];
    DWORD length;

    if (buffer == NULL || buffer_size == 0) {
        return false;
    }

    length = GetPrivateProfileStringA(section, key, INI_ABSENT, raw, (DWORD)sizeof(raw),
                                      ini_path());
    if (length == 0 || strcmp(raw, INI_ABSENT) == 0) {
        snprintf(buffer, buffer_size, "%s", (default_value != NULL) ? default_value : "");
        return false;
    }

    snprintf(buffer, buffer_size, "%s", raw);
    return true;
}

bool ini_read_bool(const char *section, const char *key, bool default_value)
{
    char value[64];

    if (!ini_read_string(section, key, "", value, sizeof(value))) {
        return default_value;
    }
    if (_stricmp(value, "1") == 0 || _stricmp(value, "true") == 0 ||
        _stricmp(value, "yes") == 0 || _stricmp(value, "on") == 0) {
        return true;
    }
    if (_stricmp(value, "0") == 0 || _stricmp(value, "false") == 0 ||
        _stricmp(value, "no") == 0 || _stricmp(value, "off") == 0) {
        return false;
    }
    return default_value;
}

int32_t ini_read_int(const char *section, const char *key, int32_t default_value)
{
    char  value[64];
    char *stop;
    long  parsed;

    if (!ini_read_string(section, key, "", value, sizeof(value))) {
        return default_value;
    }

    stop = NULL;
    parsed = strtol(value, &stop, 0);
    if (stop == value) {
        return default_value;
    }
    return (int32_t)parsed;
}

float ini_read_float(const char *section, const char *key, float default_value)
{
    char   value[64];
    char  *stop;
    double parsed;

    if (!ini_read_string(section, key, "", value, sizeof(value))) {
        return default_value;
    }

    stop = NULL;
    parsed = strtod(value, &stop);
    if (stop == value) {
        return default_value;
    }
    return (float)parsed;
}

bool ini_write_int(const char *section, const char *key, int32_t value)
{
    char text[64];
    snprintf(text, sizeof(text), "%ld", (long)value);
    return WritePrivateProfileStringA(section, key, text, ini_path()) != 0;
}

bool ini_write_float(const char *section, const char *key, float value, int decimal_places)
{
    char text[64];

    if (decimal_places < 0) {
        decimal_places = 0;
    }
    if (decimal_places > 6) {
        decimal_places = 6;
    }

    snprintf(text, sizeof(text), "%.*f", decimal_places, (double)value);
    return WritePrivateProfileStringA(section, key, text, ini_path()) != 0;
}

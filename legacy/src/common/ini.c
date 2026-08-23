#include "common/ini.h"

#include "common/host_image.h"

#include <windows.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INI_FILE_NAME   "fix_enhancers.ini"
#define INI_LEGACY_NAME "open_fellowship.ini"

static char ini_file_path[MAX_PATH];
static bool ini_is_legacy;

static bool file_exists(const char *path)
{
    DWORD attributes = GetFileAttributesA(path);

    return attributes != INVALID_FILE_ATTRIBUTES
           && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

/* Resolved once. A plugin that reads twenty keys must not stat the directory twenty times, and
 * more importantly must not change its mind halfway through because a file appeared. */
const char *ini_path(void)
{
    if (ini_file_path[0] == '\0') {
        char legacy[MAX_PATH];

        host_image_resolve();
        snprintf(ini_file_path, sizeof(ini_file_path), "%s%s", host_directory(), INI_FILE_NAME);
        ini_file_path[sizeof(ini_file_path) - 1] = '\0';

        if (!file_exists(ini_file_path)) {
            snprintf(legacy, sizeof(legacy), "%s%s", host_directory(), INI_LEGACY_NAME);
            legacy[sizeof(legacy) - 1] = '\0';
            if (file_exists(legacy)) {
                memcpy(ini_file_path, legacy, sizeof(ini_file_path));
                ini_is_legacy = true;
            }
        }
    }
    return ini_file_path;
}

bool ini_using_legacy_name(void)
{
    (void)ini_path();      /* so the answer is never "not yet decided" */
    return ini_is_legacy;
}

/* A sentinel nobody would type: GetPrivateProfileString cannot otherwise tell "the key says
 * nothing" from "there is no key".
 *
 * Split across two string literals on purpose: "\x02absent" reads as the single escape \x02a,
 * which is a different character and an error under /WX. */
#define INI_ABSENT "\x01\x02" "absent"

/* The profile API returns everything after the '=' verbatim, INLINE COMMENT AND ALL. The
 * numeric readers survive it because strtol stops at the space; the boolean reader did not, and
 * every documented boolean in the shipped ini was ignored until this was added. A comment is a
 * ';' or '#' at the start of the value or following whitespace. See README.md. */
static void strip_inline_comment(char *value)
{
    size_t end = strlen(value);
    size_t i;

    for (i = 0; i < end; ++i) {
        if ((value[i] == ';' || value[i] == '#') &&
            (i == 0 || value[i - 1] == ' ' || value[i - 1] == '\t')) {
            end = i;
            break;
        }
    }

    while (end > 0 && (value[end - 1] == ' '  || value[end - 1] == '\t' ||
                       value[end - 1] == '\r' || value[end - 1] == '\n')) {
        --end;
    }
    value[end] = '\0';
}

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

    strip_inline_comment(raw);

    /* A key whose value is nothing but a comment is a key that says nothing, which is the same as
     * not being there. */
    if (raw[0] == '\0') {
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

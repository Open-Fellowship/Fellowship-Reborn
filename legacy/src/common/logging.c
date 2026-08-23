#include "common/logging.h"

#include "common/host_image.h"

#include <windows.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Renamed alongside the ini. The log needs no fallback the way the ini does: it is written from
 * scratch every run, so an old one lying beside it is a stale file rather than lost settings. */
#define LOG_FILE_NAME "fix_enhancers.log"

static char        log_file_path[MAX_PATH];
static const char *log_prefix = "?";
static bool        log_ready;

static void write_line(const char *severity, const char *format, va_list arguments)
{
    FILE *file;

    if (!log_ready) {
        return;
    }

    file = fopen(log_file_path, "a");
    if (file == NULL) {
        return;
    }

    fprintf(file, "[%s] ", log_prefix);
    if (severity != NULL) {
        fprintf(file, "%s: ", severity);
    }
    vfprintf(file, format, arguments);
    fputc('\n', file);

    /* Flushed and closed per line on purpose. The log that matters is the one written by the
     * run that crashed a millisecond later, and a buffered line is not in that file. */
    fclose(file);
}

void log_init(const char *feature_name, bool truncate)
{
    host_image_resolve();

    if (feature_name != NULL && feature_name[0] != '\0') {
        log_prefix = feature_name;
    }

    snprintf(log_file_path, sizeof(log_file_path), "%s%s", host_directory(), LOG_FILE_NAME);
    log_file_path[sizeof(log_file_path) - 1] = '\0';

    if (truncate) {
        FILE *file = fopen(log_file_path, "w");
        if (file != NULL) {
            fclose(file);
        }
    }

    log_ready = true;
}

void log_info(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    write_line(NULL, format, arguments);
    va_end(arguments);
}

void log_warning(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    write_line("WARNING", format, arguments);
    va_end(arguments);
}

void log_error(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    write_line("ERROR", format, arguments);
    va_end(arguments);
}

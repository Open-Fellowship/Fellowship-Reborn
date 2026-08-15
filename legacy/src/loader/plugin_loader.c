#include "plugin_loader.h"

#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/plugin_entry.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LOADER_SECTION           "loader"
#define DEFAULT_PLUGIN_DIRECTORY "plugins"
#define MAX_PLUGINS              64

typedef void (__cdecl *open_fellowship_install_fn_t)(void);

static bool loader_has_run;

typedef struct plugin_list {
    char   names[MAX_PLUGINS][MAX_PATH];
    size_t count;
    size_t skipped;
} plugin_list_t;

/* Case-insensitive, so the order does not depend on how the files happen to be capitalised. */
static void insert_sorted(plugin_list_t *list, const char *name)
{
    size_t position;
    size_t index;

    if (list->count >= MAX_PLUGINS) {
        ++list->skipped;
        return;
    }

    for (position = 0; position < list->count; ++position) {
        if (_stricmp(name, list->names[position]) < 0) {
            break;
        }
    }
    for (index = list->count; index > position; --index) {
        memcpy(list->names[index], list->names[index - 1], MAX_PATH);
    }

    /* snprintf rather than strncpy: it always terminates, and it does not make the compiler
     * guess whether a truncated copy was intended. */
    snprintf(list->names[position], MAX_PATH, "%s", name);
    ++list->count;
}

static bool collect_plugins(const char *directory, plugin_list_t *list)
{
    WIN32_FIND_DATAA entry;
    HANDLE           search;
    char             pattern[MAX_PATH];

    snprintf(pattern, sizeof(pattern), "%s\\*.dll", directory);
    pattern[sizeof(pattern) - 1] = '\0';

    search = FindFirstFileA(pattern, &entry);
    if (search == INVALID_HANDLE_VALUE) {
        return false;
    }

    do {
        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        insert_sorted(list, entry.cFileName);
    } while (FindNextFileA(search, &entry));

    FindClose(search);
    return true;
}

static void load_one(const char *directory, const char *name)
{
    char                         path[MAX_PATH];
    HMODULE                      module;
    open_fellowship_install_fn_t install;

    snprintf(path, sizeof(path), "%s\\%s", directory, name);
    path[sizeof(path) - 1] = '\0';

    module = LoadLibraryA(path);
    if (module == NULL) {
        log_error("plugin %-28s LoadLibrary failed (%lu)", name, (unsigned long)GetLastError());
        return;
    }

    install = (open_fellowship_install_fn_t)(void *)
              GetProcAddress(module, OPEN_FELLOWSHIP_ENTRY_NAME);
    if (install == NULL) {
        log_info("plugin %-28s loaded at %08X, no %s export, left to its own DllMain",
                 name, (unsigned)(uintptr_t)module, OPEN_FELLOWSHIP_ENTRY_NAME);
        return;
    }

    log_info("plugin %-28s loaded at %08X, calling %s",
             name, (unsigned)(uintptr_t)module, OPEN_FELLOWSHIP_ENTRY_NAME);
    install();
}

void plugin_loader_run_once(void)
{
    char          configured[MAX_PATH];
    char          directory[MAX_PATH];
    plugin_list_t list;
    size_t        index;

    if (loader_has_run) {
        return;
    }
    loader_has_run = true;

    host_image_resolve();
    log_init("loader", true);

    log_info("OpenFellowship loader");
    log_info("host %s", host_path());
    log_info("ini  %s", ini_path());

    /* Every plugin falls back to its built-in defaults when the file is absent. That is a
     * legitimate way to run, but it must not look like the settings were read. */
    if (GetFileAttributesA(ini_path()) == INVALID_FILE_ATTRIBUTES) {
        log_warning("there is no configuration file at that path. Every plugin is running on its "
                    "built-in defaults. Copy dist/open_fellowship.ini next to Fellowship.exe to "
                    "change anything.");
    }

    if (!ini_read_bool(LOADER_SECTION, "Enabled", true)) {
        log_warning("Enabled=0 in [%s] - no plugin is loaded, the game runs exactly as before",
                    LOADER_SECTION);
        return;
    }

    ini_read_string(LOADER_SECTION, "PluginDirectory", DEFAULT_PLUGIN_DIRECTORY,
                    configured, sizeof(configured));
    snprintf(directory, sizeof(directory), "%s%s", host_directory(), configured);
    directory[sizeof(directory) - 1] = '\0';

    memset(&list, 0, sizeof(list));
    if (!collect_plugins(directory, &list)) {
        log_warning("no plugin directory at %s, nothing to load. Create it and put the plugin "
                    "DLLs in it.", directory);
        return;
    }
    if (list.skipped != 0) {
        log_warning("%s holds more than %u DLLs; %u were not loaded",
                    directory, (unsigned)MAX_PLUGINS, (unsigned)list.skipped);
    }

    log_info("loading %u plugin(s) from %s", (unsigned)list.count, directory);
    for (index = 0; index < list.count; ++index) {
        load_one(directory, list.names[index]);
    }
    log_info("--- all plugins loaded ---");
}

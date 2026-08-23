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

/* ------------------------------------------------------------------------------- which build
 *
 * Two file sizes, logged before anything else happens, because they decide whether any of the
 * rest of this log means what it says. Every site in this project was measured against one pair
 * of files, and a plugin that declines on a different pair is behaving correctly, but a reader
 * cannot tell that apart from a plugin that is broken unless the log says which files these are.
 *
 * The sizes are read off DISK rather than from the loaded image, because Fellowship.rfl is not
 * loaded yet at this point and will not be for several seconds.
 *
 * The retail values are recorded here so that the two builds anyone actually has are both named
 * rather than one of them being "unexpected":
 *
 *     Fellowship.exe   2,133,459   the No-CD executable, what this project targets
 *                      2,137,555   retail, SafeDisc. Its code is encrypted on disk, so every
 *                                  byte check made at the entry point fails.
 *     Fellowship.rfl   1,372,160   the v1.1 game, what this project targets
 *                      1,306,624   pre-1.1. Different addresses; eight of the nine rfl sites
 *                                  used here are not in that build at all.
 */
#define EXE_SIZE_SUPPORTED   2133459u
#define EXE_SIZE_RETAIL_CD   2137555u
#define RFL_SIZE_SUPPORTED   1372160u
#define RFL_SIZE_PRE_11      1306624u

static bool file_size(const char *path, uint32_t *out)
{
    WIN32_FILE_ATTRIBUTE_DATA attributes;

    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &attributes)) {
        return false;
    }
    if (attributes.nFileSizeHigh != 0) {
        return false;
    }

    *out = attributes.nFileSizeLow;
    return true;
}

static void log_which_build(void)
{
    char     rfl_path[MAX_PATH];
    uint32_t exe = 0;
    uint32_t rfl = 0;
    bool     exe_known;
    bool     rfl_known;

    snprintf(rfl_path, sizeof(rfl_path), "%sFellowship.rfl", host_directory());
    rfl_path[sizeof(rfl_path) - 1] = '\0';

    exe_known = file_size(host_path(), &exe);
    rfl_known = file_size(rfl_path, &rfl);

    if (exe_known) {
        log_info("Fellowship.exe %lu bytes%s", (unsigned long)exe,
                 exe == EXE_SIZE_SUPPORTED ? "  (the build this project targets)" :
                 exe == EXE_SIZE_RETAIL_CD ? "  (retail, SafeDisc: its code is encrypted on disk, "
                                             "so the exe plugins below will all decline)" :
                                             "  (not a build this project has been measured "
                                             "against)");
    } else {
        log_warning("could not read the size of %s", host_path());
    }

    if (rfl_known) {
        log_info("Fellowship.rfl %lu bytes%s", (unsigned long)rfl,
                 rfl == RFL_SIZE_SUPPORTED ? "  (the build this project targets)" :
                 rfl == RFL_SIZE_PRE_11    ? "  (pre-1.1: different addresses, most rfl plugins "
                                             "will decline. Apply the official v1.1 patch)" :
                                             "  (not a build this project has been measured "
                                             "against)");
    } else {
        log_warning("there is no Fellowship.rfl next to the executable");
    }

    if (exe_known && rfl_known && (exe != EXE_SIZE_SUPPORTED || rfl != RFL_SIZE_SUPPORTED)) {
        log_warning("this is not the pair everything here was measured against, which is "
                    "Fellowship.exe %u and Fellowship.rfl %u. Plugins that decline below are "
                    "declining correctly. The order that gets you there: install, apply the "
                    "official v1.1 patch, then put the 1.1 No-CD executable in, the patch "
                    "replaces the executable, so the No-CD goes in last.",
                    (unsigned)EXE_SIZE_SUPPORTED, (unsigned)RFL_SIZE_SUPPORTED);
    }
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
    if (ini_using_legacy_name()) {
        log_info("     that is the OLD name. fix_enhancers.ini is what this now looks for, and "
                 "the old one is read only because the new one is not there. Renaming it is "
                 "optional and loses nothing.");
    }

    /* Before the ini, before the plugin list, before anything can fail: which two files is this?
     * Every bug report that starts with a log now answers that question in its first three
     * lines. */
    log_which_build();

    /* Every plugin falls back to its built-in defaults when the file is absent. That is a
     * legitimate way to run, but it must not look like the settings were read. */
    if (GetFileAttributesA(ini_path()) == INVALID_FILE_ATTRIBUTES) {
        log_warning("there is no configuration file at that path. Every plugin is running on its "
                    "built-in defaults. Copy dist/fix_enhancers.ini next to Fellowship.exe to "
                    "change anything.");
    }

    if (!ini_read_bool(LOADER_SECTION, "Enabled", true)) {
        log_warning("Enabled=0 in [%s]; no plugin is loaded, the game runs exactly as before",
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

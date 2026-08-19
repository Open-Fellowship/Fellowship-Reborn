/* proxy.c - a stand-in for Fellowship.rfl that forwards to the retail engine.
 *
 * This is phase one of replacing the engine module. The DLL exports the same
 * eleven names the retail Fellowship.rfl exports, loads the original alongside
 * itself, and forwards every call to it. The game behaves exactly as before.
 *
 * That sounds like it achieves nothing, and the point is precisely that it
 * achieves nothing *observable*: it establishes the seam. Once the host is
 * talking to us instead of to the retail module, a function moves from
 * forwarded to reimplemented by changing one line in the dispatch table below,
 * and the game keeps running the whole way. There is never a moment where the
 * project has half an engine and no way to test it.
 *
 * ## How the host finds us
 *
 * `Fellowship.exe` has no static import of the engine and contains no ".rfl"
 * string, so the module is loaded dynamically and its name is built rather than
 * spelled out. The retail pair is `Fellowship.exe` beside `Fellowship.rfl`, and
 * the exe does hold `RiotDllType` and `RiotDllGetID` as GetProcAddress strings,
 * so the sequence is: derive the module path from the executable's own, load
 * it, ask it what it is, then ask it for its interfaces.
 *
 * We therefore take the name `Fellowship.rfl`, and the retail module is renamed
 * so that we can load it ourselves.
 *
 * ## Installation
 *
 *     Fellowship.rfl        <- this DLL
 *     Fellowship.orig.rfl   <- the retail engine, renamed
 *
 * Nothing else moves. If the renamed original is missing this DLL fails loudly
 * at first call rather than returning null interfaces, because a null interface
 * surfaces later as an unexplained crash inside the host.
 *
 * ## What this file may not do
 *
 * Nothing here may run during DllMain. Loading a library from inside DllMain
 * deadlocks against the loader lock, and the retail module has its own DllMain
 * to run. Resolution is therefore deferred to the first exported call, which is
 * safe because every one of them is called from ordinary host code.
 */

#include <windows.h>
#include <stdio.h>

#include "proxy.h"
#include "objectdef.h"
#include "objtype.h"
#include "registry.h"

/* Every export of the retail module, as a typed pointer. Typed rather than
 * FARPROC because a forwarder that gets an argument count wrong corrupts the
 * stack at the call site and the damage surfaces somewhere else entirely. */
typedef void *(__cdecl *pfn_get_interface)(void);
typedef int(__cdecl *pfn_is_light)(int class_id, void *out_desc);
typedef int(__cdecl *pfn_is_class)(int class_id);
typedef int(__cdecl *pfn_dll_id)(void);
typedef unsigned int(__cdecl *pfn_dll_type)(void);

static struct {
    HMODULE module;
    pfn_get_interface GetBaseRFLInterface;
    pfn_get_interface GetLandTypeInterface;
    pfn_get_interface GetMessageInterface;
    pfn_get_interface GetObjTypeInterface;
    pfn_get_interface GetObjectDefInterface;
    pfn_is_light IsObjectLight;
    pfn_is_class IsObjectPortal;
    pfn_is_class IsObjectMoveNode;
    pfn_dll_id RiotDllGetID;
    pfn_dll_type RiotDllType;
} g_retail;

static HMODULE g_self;

void of_engine_log(const char *format, ...)
{
    char path[MAX_PATH];
    va_list args;
    FILE *fh;
    DWORD n;

    n = GetModuleFileNameA(g_self, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return;
    }
    /* Beside the DLL, not in the working directory: the host may chdir, and a
     * log that moves is a log nobody finds. */
    while (n > 0 && path[n - 1] != '\\' && path[n - 1] != '/') {
        n--;
    }
    path[n] = '\0';
    if (strlen(path) + strlen(OF_ENGINE_LOG_NAME) >= MAX_PATH) {
        return;
    }
    strcat(path, OF_ENGINE_LOG_NAME);

    fh = fopen(path, "a");
    if (fh == NULL) {
        return;
    }
    va_start(args, format);
    vfprintf(fh, format, args);
    va_end(args);
    fputc('\n', fh);
    fclose(fh);
}

/* Whether one of our systems is enabled. Absent ini, absent key or unreadable
 * file all mean "on", because the switch exists to turn things OFF while
 * bisecting and a missing file should not silently disable the layer. */
int of_use_own(const char *key)
{
    char path[MAX_PATH];
    char line[256];
    FILE *fh;
    DWORD n;
    size_t klen;
    int result = 1;

    n = GetModuleFileNameA(g_self, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return 1;
    }
    while (n > 0 && path[n - 1] != '\\' && path[n - 1] != '/') {
        n--;
    }
    path[n] = '\0';
    if (strlen(path) + strlen(OF_ENGINE_INI_NAME) >= MAX_PATH) {
        return 1;
    }
    strcat(path, OF_ENGINE_INI_NAME);

    fh = fopen(path, "r");
    if (fh == NULL) {
        return 1;
    }
    klen = strlen(key);
    while (fgets(line, sizeof line, fh) != NULL) {
        if (_strnicmp(line, key, klen) == 0) {
            const char *p = line + klen;
            while (*p == ' ' || *p == '	') {
                p++;
            }
            if (*p == '=') {
                p++;
                while (*p == ' ' || *p == '	') {
                    p++;
                }
                result = (*p != '0');
            }
        }
    }
    fclose(fh);
    return result;
}

/* Path of the renamed retail module, derived from our own so the pair travels
 * together regardless of where the game is installed. */
static int retail_path(char *out, size_t size)
{
    char path[MAX_PATH];
    DWORD n;
    size_t len;

    n = GetModuleFileNameA(g_self, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return 0;
    }
    len = strlen(path);
    if (len < 4 || _stricmp(path + len - 4, ".rfl") != 0) {
        return 0;
    }
    path[len - 4] = '\0';                       /* drop the extension */
    if (strlen(path) + strlen(OF_RETAIL_SUFFIX) >= size) {
        return 0;
    }
    strcpy(out, path);
    strcat(out, OF_RETAIL_SUFFIX);
    return 1;
}

/* Assigning through a FARPROC lvalue rather than casting the result to the
 * member's type. GetProcAddress returns a function pointer and C has no
 * portable cast from one to a data pointer, so the obvious `(void *)` spelling
 * is a constraint violation that MSVC reports as C4152 - and this project
 * builds with /WX. Writing through `*(FARPROC *)&member` keeps the conversion
 * function-pointer-to-function-pointer, which is the one C does allow. */
#define BIND(name)                                                            \
    do {                                                                      \
        FARPROC proc = GetProcAddress(g_retail.module, #name);                \
        if (proc == NULL) {                                                   \
            of_engine_log("  MISSING export: %s", #name);                     \
            missing++;                                                        \
        }                                                                     \
        *(FARPROC *)&g_retail.name = proc;                                    \
    } while (0)

/* Returns non-zero once the retail module is loaded and every export bound.
 *
 * Deliberately all-or-nothing. A partial bind would let the game start and then
 * fail at whichever interface happened to be missing, which is a far worse
 * thing to debug than a refusal at startup. */
int of_retail_ready(void)
{
    char path[MAX_PATH];
    int missing = 0;

    if (g_retail.module != NULL) {
        return 1;
    }
    if (!retail_path(path, sizeof path)) {
        of_engine_log("cannot derive the retail module path from our own");
        return 0;
    }
    g_retail.module = LoadLibraryA(path);
    if (g_retail.module == NULL) {
        of_engine_log("LoadLibrary failed (%lu) for %s", GetLastError(), path);
        of_engine_log("  rename the retail Fellowship.rfl to %s beside this DLL",
                      OF_RETAIL_NAME);
        return 0;
    }
    of_engine_log("loaded retail engine: %s", path);

    BIND(GetBaseRFLInterface);
    BIND(GetLandTypeInterface);
    BIND(GetMessageInterface);
    BIND(GetObjTypeInterface);
    BIND(GetObjectDefInterface);
    BIND(IsObjectLight);
    BIND(IsObjectPortal);
    BIND(IsObjectMoveNode);
    BIND(RiotDllGetID);
    BIND(RiotDllType);

    if (missing != 0) {
        of_engine_log("%d export(s) missing - refusing to proxy a partial module",
                      missing);
        FreeLibrary(g_retail.module);
        g_retail.module = NULL;
        return 0;
    }
    return 1;
}

/* The dispatch table.
 *
 * Each export is one of two things: forwarded to the retail module, or
 * implemented here. Moving a function across is a one-line change, and the
 * comment beside each says where its implementation came from - which matters,
 * because a reimplementation that was verified byte-for-byte against the retail
 * code is a very different claim from one that was merely written to look
 * right. */

void *__cdecl GetBaseRFLInterface(void)
{
    return of_retail_ready() ? g_retail.GetBaseRFLInterface() : NULL;
}

/* OURS. 192 land types, {name, flags}; see registry.h. */
void *__cdecl GetLandTypeInterface(void)
{
    static int decided, use_ours;

    if (!decided) {
        decided = 1;
        use_ours = of_use_own("LandTypes");
        of_engine_log("LandType registry: %s",
                      use_ours ? "ours (generated, verified against the retail image)"
                               : "forwarded to the retail module (LandTypes=0)");
    }
    if (use_ours) {
        return (void *)&g_landTypeInterface;
    }
    return of_retail_ready() ? g_retail.GetLandTypeInterface() : NULL;
}

/* OURS. 54 messages, {name, id} - the names authored levels use to say what a
 * trigger sends. The ids are sparse above 0x100, which is why they are carried
 * rather than counted. */
void *__cdecl GetMessageInterface(void)
{
    static int decided, use_ours;

    if (!decided) {
        decided = 1;
        use_ours = of_use_own("Messages");
        of_engine_log("Message registry: %s",
                      use_ours ? "ours (generated, verified against the retail image)"
                               : "forwarded to the retail module (Messages=0)");
    }
    if (use_ours) {
        return (void *)&g_messageInterface;
    }
    return of_retail_ready() ? g_retail.GetMessageInterface() : NULL;
}

/* OURS. Nineteen records of {id, name}, published by an initialiser that is two
 * stores and a return - the same shape as the ObjectDef registry and the same
 * argument for taking it over: the table IS the implementation.
 *
 * Worth having for its own sake rather than for its size. An ObjectDef's +0x04
 * field holds one of these ids, so until this table is ours that field is a
 * number we can reproduce but not resolve. */
void *__cdecl GetObjTypeInterface(void)
{
    static int decided, use_ours;

    if (!decided) {
        decided = 1;
        use_ours = of_use_own("ObjTypes");
        of_engine_log("ObjType registry: %s",
                      use_ours ? "ours (generated, verified against the retail image)"
                               : "forwarded to the retail module (ObjTypes=0)");
    }
    if (use_ours) {
        return (void *)&g_objTypeInterface;
    }
    return of_retail_ready() ? g_retail.GetObjTypeInterface() : NULL;
}

/* OURS. The registry is static data behind a getter - the retail module's
 * initialiser is two stores and its getter is six bytes - so serving our own
 * generated table IS the implementation, with no behaviour to reproduce.
 *
 * objectdef/objectdef_table.c is generated from the retail image and every one of
 * its 9,069 non-pointer fields is compared back against that image by
 * `objdefgen.py --verify`. The two fields whose meaning is unestablished,
 * ObjectDef+0x08 and the flags, are carried across verbatim and the comparison
 * proves it: a field does not have to be understood to be reproduced. */
void *__cdecl GetObjectDefInterface(void)
{
    static int decided, use_ours;

    if (!decided) {
        decided = 1;
        use_ours = of_use_own("ObjectDefs");
        of_engine_log("ObjectDef registry: %s",
                      use_ours ? "ours (generated, verified against the retail image)"
                               : "forwarded to the retail module (ObjectDefs=0)");
    }
    if (use_ours) {
        return (void *)&g_objectDefInterface;
    }
    return of_retail_ready() ? g_retail.GetObjectDefInterface() : NULL;
}

int __cdecl IsObjectLight(int class_id, void *out_desc)
{
    /* Forwarded. We have a byte-matched reading of this one, but it calls two
     * further functions inside the retail image that are not reachable by name,
     * so reimplementing it means resolving them by address first. Not yet. */
    return of_retail_ready() ? g_retail.IsObjectLight(class_id, out_desc) : 0;
}

int __cdecl RiotDllGetID(void)
{
    return of_retail_ready() ? g_retail.RiotDllGetID() : 0;
}

unsigned int __cdecl RiotDllType(void)
{
    return of_retail_ready() ? g_retail.RiotDllType() : 0;
}

/* IsObjectPortal and IsObjectMoveNode are implemented in predicates.c - the
 * first two functions this project runs instead of the retail engine's. */

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = instance;
        DisableThreadLibraryCalls(instance);
        /* No LoadLibrary here - see the note at the top of this file. */
    }
    return TRUE;
}

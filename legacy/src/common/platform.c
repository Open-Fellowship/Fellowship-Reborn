#include "common/platform.h"

#include <windows.h>

static const char *g_version;
static bool        g_asked;

const char *platform_wine_version(void)
{
    HMODULE ntdll;
    const char *(CDECL *get_version)(void);

    if (g_asked) {
        return g_version;
    }
    g_asked = true;

    ntdll = GetModuleHandleA("ntdll.dll");
    if (ntdll == NULL) {
        return NULL;
    }

    *(FARPROC *)&get_version = GetProcAddress(ntdll, "wine_get_version");
    if (get_version != NULL) {
        g_version = get_version();
    }
    return g_version;
}

bool platform_is_wine(void)
{
    return platform_wine_version() != NULL;
}

/* dll_main.c: the entry point, and nothing else. The contract is in common/plugin_entry.h. */
#include "cd_check.h"

#include "common/plugin_entry.h"

#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

FELLOWSHIP_REBORN_ENTRY
{
    cd_check_install();
}

/* dll_main.c: the entry point, and nothing else.
 *
 * DllMain does no work. The loader calls open_fellowship_install AFTER LoadLibrary has returned,
 * which is outside the loader lock and with the host image fully mapped, and that is the only
 * place a plugin may scan, read files or load anything.
 */
#include "frame_state.h"

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

OPEN_FELLOWSHIP_ENTRY
{
    frame_state_install();
}

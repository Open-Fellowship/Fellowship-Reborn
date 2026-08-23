/* dll_main.c: the loader's entry point.
 *
 * dinput8.dll is a static import of Fellowship.exe, so this runs during process initialisation.
 * NOTHING IS LOADED, SCANNED OR READ HERE: LoadLibrary under the loader lock is how deadlocks
 * are made. All that happens is five bytes, arming the entry-point trigger. See early_trigger.h.
 */
#include "early_trigger.h"

#include "common/host_image.h"

#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        /* Cheap and loader-lock-safe: this only reads the already mapped PE headers of the
         * process that is starting, so the game folder is known before anything asks for it. */
        host_image_resolve();
        early_trigger_arm();
    }

    return TRUE;
}

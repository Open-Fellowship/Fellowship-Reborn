/* dll_main.c: the loader's entry point.
 *
 * dinput8.dll is a static import of Fellowship.exe, so this runs during process initialisation,
 * before a single instruction of the game. Nothing is loaded, scanned or read here: LoadLibrary
 * under the loader lock is how deadlocks are made.
 *
 * What DOES happen here is five bytes. early_trigger_arm() redirects the host's entry point at
 * itself, so the plugins can be loaded at the earliest moment the loader lock is gone. That
 * timing is not cosmetic: Fellowship.exe imports Direct3DCreate8 and DirectInput8Create, and
 * graphics comes up first, so a loader that waits for the input call installs its patches after
 * the display mode has been chosen and the camera's viewport has already been built.
 * See early_trigger.h.
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

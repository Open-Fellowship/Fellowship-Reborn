#include "common/module_watch.h"

#include "common/logging.h"

#include <windows.h>

#define POLL_INTERVAL_MS 25u
#define SETTLE_MS        200u

typedef struct watch_request {
    const char             *module_name;
    module_watch_callback_t on_loaded;
    unsigned                timeout_ms;
} watch_request_t;

static DWORD WINAPI watch_thread(LPVOID parameter)
{
    watch_request_t *request = (watch_request_t *)parameter;
    unsigned         waited  = 0;
    HMODULE          module;

    for (;;) {
        module = GetModuleHandleA(request->module_name);
        if (module != NULL) {
            /* SEEN ONCE IS NOT READY.
             *
             * GetModuleHandleA answers as soon as the module is in the loader's list, and a
             * plugin that patches on that first sighting is racing whatever the loader has left
             * to do. It bit us: with a seventeenth plugin in the folder the timing shifted by one
             * poll, text_scaling won the race by 25 ms, and one of its seven sites came back
             * "unexpected bytes" - on the same rfl, at the same base, that had installed cleanly
             * the run before.
             *
             * So the module has to be seen, and then still be there and unchanged a full settle
             * later. Two hundred milliseconds during a five-second load costs nothing and closes
             * the window that produced a PARTIAL install. */
            Sleep(SETTLE_MS);
            if (GetModuleHandleA(request->module_name) != module) {
                continue;
            }
            log_info("%s appeared at %08X after %u ms",
                     request->module_name, (unsigned)(uintptr_t)module, waited);
            request->on_loaded((uintptr_t)module);
            break;
        }
        if (request->timeout_ms != 0 && waited >= request->timeout_ms) {
            log_error("%s did not load within %u ms; this plugin is doing nothing",
                      request->module_name, request->timeout_ms);
            break;
        }
        Sleep(POLL_INTERVAL_MS);
        waited += POLL_INTERVAL_MS;
    }

    HeapFree(GetProcessHeap(), 0, request);
    return 0;
}

bool module_watch_when_loaded(const char *module_name, module_watch_callback_t on_loaded,
                              unsigned timeout_ms)
{
    watch_request_t *request;
    HANDLE           thread;

    if (module_name == NULL || on_loaded == NULL) {
        return false;
    }

    /* Already there? Then do it here and now, on the caller's thread, and start nothing. */
    {
        HMODULE existing = GetModuleHandleA(module_name);
        if (existing != NULL) {
            log_info("%s is already loaded at %08X",
                     module_name, (unsigned)(uintptr_t)existing);
            on_loaded((uintptr_t)existing);
            return true;
        }
    }

    request = (watch_request_t *)HeapAlloc(GetProcessHeap(), 0, sizeof(*request));
    if (request == NULL) {
        return false;
    }
    request->module_name = module_name;
    request->on_loaded   = on_loaded;
    request->timeout_ms  = timeout_ms;

    thread = CreateThread(NULL, 0, watch_thread, request, 0, NULL);
    if (thread == NULL) {
        HeapFree(GetProcessHeap(), 0, request);
        return false;
    }
    CloseHandle(thread);
    return true;
}

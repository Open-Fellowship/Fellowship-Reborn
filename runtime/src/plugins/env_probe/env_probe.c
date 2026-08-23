#include "env_probe.h"

#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/module_watch.h"

#include <windows.h>
#include <tlhelp32.h>         /* the thread list, only ever walked once, on a stall */

#include <stdint.h>
#include <stdio.h>            /* _snprintf, for naming an address module+offset */
#include <string.h>

#define PLUGIN_SECTION "env_probe"

#define D3D8_GETADAPTERIDENTIFIER 5
#define D3D8_CREATEDEVICE         15

/* IDirect3DDevice8, same COM ordering. Reset is where a windowed device becomes a fullscreen one,
 * and Present is the proof that frames are still being produced at all, which is the difference
 * between a game that has hung and a game that is drawing into something nobody can see. */
#define D3D8_DEVICE_RESET         14
#define D3D8_DEVICE_PRESENT       15

typedef struct present_parameters {
    UINT  back_buffer_width;
    UINT  back_buffer_height;
    DWORD back_buffer_format;
    UINT  back_buffer_count;
    DWORD multi_sample_type;
    DWORD swap_effect;
    HWND  device_window;
    BOOL  windowed;
    BOOL  enable_auto_depth_stencil;
    DWORD auto_depth_stencil_format;
    DWORD flags;
    UINT  full_screen_refresh_rate;
    UINT  full_screen_presentation_interval;
} present_parameters_t;

typedef struct adapter_identifier {
    char  driver[512];
    char  description[512];
    LARGE_INTEGER driver_version;
    DWORD vendor_id;
    DWORD device_id;
    DWORD sub_sys_id;
    DWORD revision;
    GUID  device_identifier;
    DWORD whql_level;
} adapter_identifier_t;

typedef void *(WINAPI *direct3d_create8_t)(UINT sdk_version);
typedef HRESULT (STDMETHODCALLTYPE *create_device_t)(void *self, UINT adapter, DWORD device_type,
                                                     HWND focus_window, DWORD behaviour_flags,
                                                     present_parameters_t *parameters,
                                                     void **returned_device);
typedef HRESULT (STDMETHODCALLTYPE *get_adapter_identifier_t)(void *self, UINT adapter,
                                                              DWORD flags,
                                                              adapter_identifier_t *identifier);

typedef HRESULT (STDMETHODCALLTYPE *device_reset_t)(void *self, present_parameters_t *parameters);
typedef HRESULT (STDMETHODCALLTYPE *device_present_t)(void *self, const RECT *source,
                                                      const RECT *destination, HWND override,
                                                      const void *dirty_region);

static direct3d_create8_t g_original_create;
static create_device_t    g_original_create_device;
static device_reset_t     g_original_reset;
static device_present_t   g_original_present;
static bool               g_device_hooked;
static bool               g_present_hooked;
static unsigned           g_frames;
static DWORD              g_last_report;

static HWND               g_watch_window;

/* The thread that presented the last frame, which is the thread worth asking about when the
 * frames stop. */
static DWORD              g_present_thread;

/* ------------------------------------------------------------------------------- the platform */

static const char *wine_version(void)
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    const char *(CDECL *get_version)(void);

    if (ntdll == NULL) {
        return NULL;
    }

    /* Wine exports this and Windows does not. It is the documented way to ask. */
    *(FARPROC *)&get_version = GetProcAddress(ntdll, "wine_get_version");
    return (get_version != NULL) ? get_version() : NULL;
}

static void log_environment_variable(const char *name)
{
    char value[512];
    DWORD length = GetEnvironmentVariableA(name, value, sizeof(value));

    if (length > 0 && length < sizeof(value)) {
        log_info("  %-24s %s", name, value);
    }
}

static void log_platform(void)
{
    const char *wine = wine_version();
    SYSTEM_INFO info;
    MEMORYSTATUSEX memory;

    if (wine != NULL) {
        log_info("running under WINE %s, Proton, a Steam Deck, or a Linux desktop", wine);
        log_environment_variable("SteamGameId");
        log_environment_variable("SteamAppId");
        log_environment_variable("SteamDeck");
        log_environment_variable("WINEPREFIX");
        log_environment_variable("WINEDLLOVERRIDES");
        log_environment_variable("PROTON_USE_WINED3D");
        log_environment_variable("PROTON_NO_D3D8");
        log_environment_variable("DXVK_HUD");
        log_environment_variable("DXVK_FILTER_DEVICE_NAME");
        log_environment_variable("VKD3D_CONFIG");
    } else {
        log_info("running on Windows (ntdll has no wine_get_version)");
    }

    memset(&info, 0, sizeof(info));
    GetSystemInfo(&info);

    memset(&memory, 0, sizeof(memory));
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) {
        log_info("  %lu processor(s), %lu MB of memory",
                 (unsigned long)info.dwNumberOfProcessors,
                 (unsigned long)(memory.ullTotalPhys / (1024u * 1024u)));
    }
}

/* --------------------------------------------------------------------------- which d3d8 is it */

static void report_module(const char *name)
{
    HMODULE module = GetModuleHandleA(name);
    char    path[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA attributes;

    if (module == NULL) {
        log_info("  %-14s not loaded", name);
        return;
    }
    if (GetModuleFileNameA(module, path, sizeof(path)) == 0) {
        log_info("  %-14s loaded at %08X, path unavailable", name, (unsigned)(uintptr_t)module);
        return;
    }

    if (GetFileAttributesExA(path, GetFileExInfoStandard, &attributes)) {
        log_info("  %-14s %s (%lu bytes)", name, path, (unsigned long)attributes.nFileSizeLow);
    } else {
        log_info("  %-14s %s", name, path);
    }
}

static void on_d3d8_loaded(uintptr_t module_base)
{
    (void)module_base;

    log_info("Direct3D 8 is in the process:");
    report_module("d3d8.dll");
    report_module("wined3d.dll");
    report_module("dxvk_d3d8.dll");
    report_module("d3d9.dll");

    /* A d3d8.dll sitting in the game folder is a wrapper, and under Proton that is a translation
     * layer stacked on a translation layer. Worth saying out loud, because it is a common cause
     * of a black screen and nothing in the game will mention it. */
    {
        HMODULE module = GetModuleHandleA("d3d8.dll");
        char    path[MAX_PATH];

        if (module != NULL && GetModuleFileNameA(module, path, sizeof(path)) != 0) {
            const char *directory = host_directory();

            if (directory != NULL && _strnicmp(path, directory, strlen(directory)) == 0) {
                log_warning("that d3d8.dll is in the GAME FOLDER, so it is a wrapper rather than "
                            "the system one. Under Wine or Proton this stacks a translation layer "
                            "on top of another; if the screen is black, move it aside and try "
                            "again.");
            }
        }
    }
}

/* ------------------------------------------------------------- what the game asked for, and got */

static const char *result_text(HRESULT result)
{
    switch ((unsigned long)result) {
    case 0x00000000ul: return "D3D_OK";
    case 0x8876086Cul: return "D3DERR_INVALIDCALL";
    case 0x8876086Aul: return "D3DERR_NOTAVAILABLE";
    case 0x88760868ul: return "D3DERR_DEVICELOST";
    case 0x88760869ul: return "D3DERR_DEVICENOTRESET";
    case 0x8876017Cul: return "D3DERR_OUTOFVIDEOMEMORY";
    case 0x8007000Eul: return "E_OUTOFMEMORY";
    case 0x80004001ul: return "E_NOTIMPL";
    default:           return "an unlisted result";
    }
}

static const char *format_text(DWORD format)
{
    switch (format) {
    case 0:  return "UNKNOWN";
    case 20: return "R8G8B8";
    case 21: return "A8R8G8B8";
    case 22: return "X8R8G8B8";
    case 23: return "R5G6B5";
    case 24: return "X1R5G5B5";
    case 25: return "A1R5G5B5";
    case 41: return "P8";
    case 50: return "L8";
    case 75: return "D16";
    case 77: return "D24S8";
    case 80: return "D24X8";
    default: return "a format not named here";
    }
}

/* A window with no client area is a black screen with a device attached to it, and the parameters
 * alone will not say so: 0x0 in windowed mode means "whatever size the window is", so the window
 * is the number that matters. */
static void report_window(const char *label, HWND window)
{
    RECT client;
    RECT frame;

    if (window == NULL) {
        log_info("  %-14s (none)", label);
        return;
    }
    if (!GetClientRect(window, &client)) {
        log_info("  %-14s %08X, client rect unavailable", label, (unsigned)(uintptr_t)window);
        return;
    }
    memset(&frame, 0, sizeof(frame));
    GetWindowRect(window, &frame);

    log_info("  %-14s %08X, client %ldx%ld, window %ldx%ld at %ld,%ld, %s%s",
             label, (unsigned)(uintptr_t)window,
             (long)(client.right - client.left), (long)(client.bottom - client.top),
             (long)(frame.right - frame.left), (long)(frame.bottom - frame.top),
             (long)frame.left, (long)frame.top,
             IsWindowVisible(window) ? "visible" : "HIDDEN",
             IsIconic(window) ? ", minimised" : "");

    if ((client.right - client.left) <= 0 || (client.bottom - client.top) <= 0) {
        log_error("  that client area is EMPTY. A windowed device sized from a zero-sized window "
                  "presents nothing, which looks exactly like a hang.");
    }
}

static void log_parameters(const char *what, const present_parameters_t *parameters,
                           DWORD behaviour_flags)
{
    if (parameters == NULL) {
        log_info("%s: no parameters", what);
        return;
    }
    log_info("%s: %ux%u %s, %s, refresh %u, flags %08lX", what,
             parameters->back_buffer_width, parameters->back_buffer_height,
             format_text(parameters->back_buffer_format),
             parameters->windowed ? "windowed" : "FULLSCREEN",
             parameters->full_screen_refresh_rate, (unsigned long)behaviour_flags);
    log_info("  depth %s, %u back buffer(s), swap effect %lu, multisample %lu",
             parameters->enable_auto_depth_stencil
                 ? format_text(parameters->auto_depth_stencil_format) : "none",
             parameters->back_buffer_count, (unsigned long)parameters->swap_effect,
             (unsigned long)parameters->multi_sample_type);
    report_window("device window", parameters->device_window);
}

/* Presenting is the heartbeat. Reported once at the start and then sparingly, because the only
 * questions it answers are "is it still going" and "did one of them fail". */
static HRESULT STDMETHODCALLTYPE hooked_present(void *self, const RECT *source,
                                                const RECT *destination, HWND override,
                                                const void *dirty_region)
{
    HRESULT result = g_original_present(self, source, destination, override, dirty_region);
    DWORD   now    = GetTickCount();

    ++g_frames;

    if (g_frames == 1) {
        g_present_thread = GetCurrentThreadId();
        log_info("first Present -> %s. The game is drawing.", result_text(result));
        report_window("drawing into", g_watch_window);
        g_last_report = now;
    } else if (FAILED(result)) {
        log_error("Present %u -> %s (%08lX)", g_frames, result_text(result),
                  (unsigned long)result);
        g_last_report = now;
    } else if ((now - g_last_report) >= 10000u) {
        log_info("%u frames presented, still %s", g_frames, result_text(result));
        report_window("drawing into", g_watch_window);
        g_last_report = now;
    }

    return result;
}

static HRESULT STDMETHODCALLTYPE hooked_reset(void *self, present_parameters_t *parameters)
{
    HRESULT result;

    log_parameters("Reset", parameters, 0);
    result = g_original_reset(self, parameters);

    if (SUCCEEDED(result)) {
        log_info("  -> %s", result_text(result));
        /* A fullscreen Reset with no device window of its own is the runtime's cue to resize the
         * focus window itself, so what that window became is the point of asking. */
        report_window("focus window", g_watch_window);
    } else {
        log_error("  -> %s (%08lX). The device is lost and the game keeps running with nothing "
                  "to draw into. This is the black screen.", result_text(result),
                  (unsigned long)result);
        if (parameters != NULL && !parameters->windowed) {
            log_error("  it was switching to FULLSCREEN. Try [windowed_res] Enabled=1 at your "
                      "screen's size, or [resolution_unlock] Enabled=0.");
        }
    }
    return result;
}

static void hook_device(void *device)
{
    void  **vtable;
    DWORD   protection = 0;

    if (g_present_hooked || device == NULL) {
        return;
    }
    vtable = *(void ***)device;
    if (!memory_is_readable_range((uintptr_t)vtable,
                                  (D3D8_DEVICE_PRESENT + 1) * sizeof(void *))) {
        return;
    }
    if (!VirtualProtect(&vtable[D3D8_DEVICE_RESET], 2 * sizeof(void *), PAGE_READWRITE,
                        &protection)) {
        log_warning("the device vtable could not be made writable, not watching Reset/Present");
        return;
    }

    g_original_reset   = (device_reset_t)vtable[D3D8_DEVICE_RESET];
    g_original_present = (device_present_t)vtable[D3D8_DEVICE_PRESENT];
    vtable[D3D8_DEVICE_RESET]   = (void *)hooked_reset;
    vtable[D3D8_DEVICE_PRESENT] = (void *)hooked_present;
    VirtualProtect(&vtable[D3D8_DEVICE_RESET], 2 * sizeof(void *), protection, &protection);

    g_present_hooked = true;
    log_info("watching Reset (%08X) and Present (%08X)", (unsigned)(uintptr_t)g_original_reset,
             (unsigned)(uintptr_t)g_original_present);
}

static bool g_watching = true;

/* The presenting thread is suspended for exactly as long as it takes to copy its registers.
 * NOTHING is read or logged while it is stopped: a thread frozen inside the runtime's own lock,
 * by a diagnostic that then wants that lock, is how the diagnostic becomes the bug. The stack is
 * walked afterwards. See README.md. */
static void describe_address(uintptr_t address, char *buffer, size_t size)
{
    MEMORY_BASIC_INFORMATION region;
    char        path[MAX_PATH];
    const char *name;
    const char *cursor;

    memset(&region, 0, sizeof(region));

    if (VirtualQuery((LPCVOID)address, &region, sizeof(region)) == 0 ||
        region.Type != MEM_IMAGE || region.AllocationBase == NULL ||
        GetModuleFileNameA((HMODULE)region.AllocationBase, path, sizeof(path)) == 0) {
        _snprintf(buffer, size, "%08X", (unsigned)address);
        buffer[size - 1] = '\0';
        return;
    }

    name = path;
    for (cursor = path; *cursor != '\0'; ++cursor) {
        if (*cursor == '\\' || *cursor == '/') {
            name = cursor + 1;
        }
    }

    /* module plus offset, so an address can be looked up in the binary without knowing where it
     * happened to load. */
    _snprintf(buffer, size, "%s+%06X", name,
              (unsigned)(address - (uintptr_t)region.AllocationBase));
    buffer[size - 1] = '\0';
}

static bool address_is_code(uintptr_t address)
{
    MEMORY_BASIC_INFORMATION region;
    DWORD                    protect;

    memset(&region, 0, sizeof(region));
    if (VirtualQuery((LPCVOID)address, &region, sizeof(region)) == 0) {
        return false;
    }
    if (region.Type != MEM_IMAGE || region.State != MEM_COMMIT) {
        return false;
    }

    protect = region.Protect & 0xFFu;
    return protect == PAGE_EXECUTE || protect == PAGE_EXECUTE_READ ||
           protect == PAGE_EXECUTE_READWRITE || protect == PAGE_EXECUTE_WRITECOPY;
}

static void report_thread(const char *label, DWORD thread_id)
{
    HANDLE    thread;
    CONTEXT   context;
    char      where[160];
    uintptr_t eip;
    uintptr_t esp;
    uintptr_t frame;
    unsigned  printed = 0;
    unsigned  i;

    if (thread_id == 0) {
        return;
    }

    thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, thread_id);
    if (thread == NULL) {
        log_info("  %s: thread %lu is gone or cannot be opened", label,
                 (unsigned long)thread_id);
        return;
    }

    memset(&context, 0, sizeof(context));
    context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;

    if (SuspendThread(thread) == (DWORD)-1) {
        CloseHandle(thread);
        log_info("  %s: thread %lu could not be suspended", label, (unsigned long)thread_id);
        return;
    }
    if (!GetThreadContext(thread, &context)) {
        ResumeThread(thread);
        CloseHandle(thread);
        log_info("  %s: thread %lu would not give up its registers", label,
                 (unsigned long)thread_id);
        return;
    }

    ResumeThread(thread);
    CloseHandle(thread);

    eip   = (uintptr_t)context.Eip;
    esp   = (uintptr_t)context.Esp;
    frame = (uintptr_t)context.Ebp;

    describe_address(eip, where, sizeof(where));
    log_info("  %s is at %s", label, where);

    /* The frame pointer chain, while it holds. */
    for (i = 0; i < 16u && printed < 8u; ++i) {
        uint32_t caller = 0;
        uint32_t next   = 0;

        if (!memory_is_readable_range(frame, 8) ||
            !memory_read_u32(frame + 4u, &caller) ||
            !memory_read_u32(frame, &next) ||
            !address_is_code((uintptr_t)caller)) {
            break;
        }

        describe_address((uintptr_t)caller, where, sizeof(where));
        log_info("    called from %s", where);
        ++printed;

        if ((uintptr_t)next <= frame) {
            break;
        }
        frame = (uintptr_t)next;
    }

    /* A release build usually has no frame pointer to follow, so the fallback is to read the
     * stack for anything that looks like a return address. Noisier, and still the answer. */
    if (printed == 0) {
        for (i = 0; i < 256u && printed < 8u; ++i) {
            uintptr_t slot  = esp + (uintptr_t)i * 4u;
            uint32_t  value = 0;

            if (!memory_is_readable_range(slot, 4) || !memory_read_u32(slot, &value)) {
                break;
            }
            if (!address_is_code((uintptr_t)value)) {
                continue;
            }

            describe_address((uintptr_t)value, where, sizeof(where));
            log_info("    on the stack: %s", where);
            ++printed;
        }
    }
}

/* Whoever holds the foreground is the whole question once the game's own window turns out to be
 * minimised, so the window gets named rather than numbered: class, title, and whether it belongs
 * to this process at all. */
static void identify_window(const char *label, HWND window)
{
    char  class_name[96];
    char  title[128];
    DWORD process = 0;
    DWORD thread;

    if (window == NULL) {
        log_info("  %s: nothing has the foreground", label);
        return;
    }

    class_name[0] = '\0';
    title[0]      = '\0';
    GetClassNameA(window, class_name, sizeof(class_name));
    GetWindowTextA(window, title, sizeof(title));
    class_name[sizeof(class_name) - 1] = '\0';
    title[sizeof(title) - 1]           = '\0';

    thread = GetWindowThreadProcessId(window, &process);

    log_info("  %s %08X, class \"%s\", title \"%s\", process %lu%s (thread %lu)",
             label, (unsigned)(uintptr_t)window, class_name, title, (unsigned long)process,
             (process == GetCurrentProcessId()) ? ", THIS GAME" : ", another process",
             (unsigned long)thread);
}

/* Every thread in the process, not just the one that was drawing. A main thread waiting politely
 * on a worker that is itself stuck is a common shape, and it is invisible if you only ask the one
 * you already suspect. */
static void report_all_threads(void)
{
    HANDLE        snapshot;
    THREADENTRY32 entry;
    DWORD         self    = GetCurrentThreadId();
    DWORD         process = GetCurrentProcessId();
    unsigned      reported = 0;
    BOOL          more;

    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        log_info("  the thread list is unavailable on this system");
        return;
    }

    memset(&entry, 0, sizeof(entry));
    entry.dwSize = sizeof(entry);

    for (more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        char label[64];

        entry.dwSize = sizeof(entry);

        if (entry.th32OwnerProcessID != process || entry.th32ThreadID == self) {
            continue;
        }
        if (++reported > 16u) {
            log_info("  (more threads than are worth printing; stopping at 16)");
            break;
        }

        _snprintf(label, sizeof(label), "thread %lu%s", (unsigned long)entry.th32ThreadID,
                  (entry.th32ThreadID == g_present_thread) ? ", which was drawing," : "");
        label[sizeof(label) - 1] = '\0';
        report_thread(label, entry.th32ThreadID);
    }

    CloseHandle(snapshot);
}

static bool window_is_pumping(HWND window)
{
    DWORD_PTR answer = 0;

    if (window == NULL) {
        return false;
    }
    /* WM_NULL costs the game nothing and only completes if the main thread is running a message
     * loop. A timeout here means the thread is busy or blocked somewhere else entirely. */
    return SendMessageTimeoutA(window, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 1000, &answer) != 0;
}

static DWORD WINAPI watchdog_thread(void *unused)
{
    unsigned last_seen   = 0;
    unsigned quiet_ticks = 0;

    (void)unused;

    while (g_watching) {
        Sleep(5000);

        if (g_frames != last_seen) {
            if (quiet_ticks >= 1) {
                log_info("frames are moving again (%u presented)", g_frames);
            }
            last_seen   = g_frames;
            quiet_ticks = 0;
            continue;
        }

        ++quiet_ticks;
        if (quiet_ticks != 1 && quiet_ticks != 6) {
            continue;
        }

        if (g_frames == 0) {
            log_error("%u seconds after the device was created and NOT ONE frame has been "
                      "presented. The game is not drawing at all; it is stuck before its render "
                      "loop rather than drawing into something invisible.", quiet_ticks * 5u);
        } else {
            log_error("%u frames presented and then nothing for %u seconds. The game STOPPED "
                      "drawing rather than never starting, so whatever went wrong happened after "
                      "the device was working.", g_frames, quiet_ticks * 5u);
        }

        report_window("window now", g_watch_window);

        if (g_watch_window != NULL && IsIconic(g_watch_window)) {
            log_error("  the game's own window is MINIMISED. A fullscreen Direct3D device takes "
                      "its window down with it when it loses the foreground, and this engine "
                      "stops drawing while it is minimised. That is the black screen, and it is "
                      "a focus problem rather than a graphics one.");
        }

        identify_window("foreground window", GetForegroundWindow());
        log_info("  main thread %s", window_is_pumping(g_watch_window)
                     ? "is still pumping messages, so the process is alive and waiting on "
                       "something else"
                     : "did NOT answer a message in a second; it is blocked or gone");

        /* Every thread once, then the drawing thread again a second later. One sample says where
         * it is; two say whether it is sitting still or going round a loop, and those are
         * different bugs. */
        report_all_threads();
        Sleep(1000);
        report_thread("a second later the drawing thread", g_present_thread);
    }
    return 0;
}

static void start_watchdog(HWND window)
{
    HANDLE thread;

    g_watch_window = window;

    thread = CreateThread(NULL, 0, watchdog_thread, NULL, 0, NULL);
    if (thread != NULL) {
        CloseHandle(thread);
    }
}

static HRESULT STDMETHODCALLTYPE hooked_create_device(void *self, UINT adapter, DWORD device_type,
                                                      HWND focus_window, DWORD behaviour_flags,
                                                      present_parameters_t *parameters,
                                                      void **returned_device)
{
    HRESULT result;

    log_info("CreateDevice: adapter %u, type %lu", adapter, (unsigned long)device_type);
    log_parameters("  requested", parameters, behaviour_flags);
    report_window("focus window", focus_window);

    result = g_original_create_device(self, adapter, device_type, focus_window, behaviour_flags,
                                      parameters, returned_device);

    if (SUCCEEDED(result)) {
        void *device = (returned_device != NULL) ? *returned_device : NULL;

        log_info("  -> %s, device %08X", result_text(result), (unsigned)(uintptr_t)device);
        hook_device(device);
        start_watchdog((parameters != NULL && parameters->device_window != NULL)
                           ? parameters->device_window
                           : focus_window);
    } else {
        /* The whole reason this plugin exists. A refused device leaves the game running with
         * nothing to draw into, which from the outside is a black screen and nothing else. */
        log_error("  -> %s (%08lX). The game has no device to draw into; this is what a black "
                  "screen looks like from in here.", result_text(result), (unsigned long)result);
        if (parameters != NULL && !parameters->windowed) {
            log_error("  it was asking for a FULLSCREEN mode. Try [windowed_res] Enabled=1 with "
                      "your screen's size, or switch [resolution_unlock] off so the game stops "
                      "offering modes the driver will not give it.");
        }
    }

    return result;
}

static void hook_device_creation(void *d3d8)
{
    void   **vtable;
    DWORD    protection = 0;
    adapter_identifier_t identifier;

    if (g_device_hooked || d3d8 == NULL) {
        return;
    }

    vtable = *(void ***)d3d8;
    if (!memory_is_readable_range((uintptr_t)vtable, (D3D8_CREATEDEVICE + 1) * sizeof(void *))) {
        log_warning("the IDirect3D8 vtable is not readable, not watching device creation");
        return;
    }

    /* Ask who we are talking to before hooking anything. wined3d and DXVK both answer this and
     * name themselves, which is the quickest way to know which stack is under the game. */
    memset(&identifier, 0, sizeof(identifier));
    if (SUCCEEDED(((get_adapter_identifier_t)vtable[D3D8_GETADAPTERIDENTIFIER])(
            d3d8, 0, 0, &identifier))) {
        identifier.driver[sizeof(identifier.driver) - 1]           = '\0';
        identifier.description[sizeof(identifier.description) - 1] = '\0';
        log_info("adapter 0: %s", identifier.description);
        log_info("  driver %s, vendor %04lX device %04lX", identifier.driver,
                 (unsigned long)identifier.vendor_id, (unsigned long)identifier.device_id);
    }

    if (!VirtualProtect(&vtable[D3D8_CREATEDEVICE], sizeof(void *), PAGE_READWRITE, &protection)) {
        log_warning("the IDirect3D8 vtable could not be made writable, not watching device "
                    "creation");
        return;
    }

    g_original_create_device = (create_device_t)vtable[D3D8_CREATEDEVICE];
    vtable[D3D8_CREATEDEVICE] = (void *)hooked_create_device;
    VirtualProtect(&vtable[D3D8_CREATEDEVICE], sizeof(void *), protection, &protection);

    g_device_hooked = true;
    log_info("watching CreateDevice (%08X)", (unsigned)(uintptr_t)g_original_create_device);
}

static void *WINAPI hooked_direct3d_create8(UINT sdk_version)
{
    void *d3d8 = g_original_create(sdk_version);

    log_info("Direct3DCreate8(%u) -> %08X", sdk_version, (unsigned)(uintptr_t)d3d8);
    hook_device_creation(d3d8);
    return d3d8;
}

/* ------------------------------------------------------------------------------ the import slot */

static void **find_import_slot(const char *dll_name, const char *symbol)
{
    uintptr_t                base = host_image_base();
    IMAGE_DOS_HEADER        *dos  = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS        *nt;
    IMAGE_IMPORT_DESCRIPTOR *import;
    DWORD                    rva;

    if (base == 0 || dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return NULL;
    }
    nt = (IMAGE_NT_HEADERS *)(base + (uintptr_t)dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return NULL;
    }
    rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (rva == 0) {
        return NULL;
    }

    for (import = (IMAGE_IMPORT_DESCRIPTOR *)(base + rva); import->Name != 0; ++import) {
        const char       *name = (const char *)(base + import->Name);
        IMAGE_THUNK_DATA *names;
        IMAGE_THUNK_DATA *addresses;

        if (_stricmp(name, dll_name) != 0) {
            continue;
        }
        names     = (IMAGE_THUNK_DATA *)(base + (import->OriginalFirstThunk != 0
                                                 ? import->OriginalFirstThunk
                                                 : import->FirstThunk));
        addresses = (IMAGE_THUNK_DATA *)(base + import->FirstThunk);

        for (; names->u1.AddressOfData != 0; ++names, ++addresses) {
            IMAGE_IMPORT_BY_NAME *entry;

            if ((names->u1.Ordinal & IMAGE_ORDINAL_FLAG32) != 0) {
                continue;
            }
            entry = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
            if (strcmp((const char *)entry->Name, symbol) == 0) {
                return (void **)&addresses->u1.Function;
            }
        }
    }
    return NULL;
}

static bool write_pointer(void **slot, void *value)
{
    DWORD protection = 0;

    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &protection)) {
        return false;
    }
    *slot = value;
    VirtualProtect(slot, sizeof(void *), protection, &protection);
    return true;
}

/* ------------------------------------------------------------------------------------ install */

void env_probe_install(void)
{
    void **slot;

    log_init(PLUGIN_SECTION, false);

    /* Off unless asked for. It reports rather than fixes, and it is the tool you turn on when
     * somebody else's machine shows a black screen, not something to leave running. */
    if (!ini_read_bool(PLUGIN_SECTION, "Enabled", false)) {
        return;
    }
    if (!host_image_resolve()) {
        log_error("the host image could not be resolved");
        return;
    }

    log_platform();

    /* Hooked at the import rather than in d3d8 itself, because the game calls this exactly once
     * and the slot is the only place that is true of. Everything here forwards; nothing is
     * changed. */
    slot = find_import_slot("D3D8.dll", "Direct3DCreate8");
    if (slot == NULL) {
        slot = find_import_slot("d3d8.dll", "Direct3DCreate8");
    }

    if (slot == NULL) {
        log_warning("Direct3DCreate8 is not imported by name, device creation will not be "
                    "reported. The rest of this plugin still works.");
    } else {
        g_original_create = (direct3d_create8_t)*slot;
        if (write_pointer(slot, (void *)hooked_direct3d_create8)) {
            log_info("watching Direct3DCreate8 (%08X)", (unsigned)(uintptr_t)g_original_create);
        } else {
            log_warning("the Direct3DCreate8 import slot could not be made writable");
        }
    }

    /* d3d8.dll is not loaded at the entry point; it turns up when the game first asks for it. */
    if (!module_watch_when_loaded("d3d8.dll", on_d3d8_loaded, 60000)) {
        log_warning("could not watch for d3d8.dll");
    }
}

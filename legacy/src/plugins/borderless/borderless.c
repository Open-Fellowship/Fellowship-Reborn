#include "borderless.h"

#include "common/compiler.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/platform.h"

#include <windows.h>

#include <stdint.h>
#include <string.h>

#define PLUGIN_SECTION "borderless"

/* COM vtable positions, not addresses in this game: IDirect3D8::CreateDevice is slot 15,
 * IDirect3DDevice8::Reset is slot 14. True of every implementation. */
#define D3D8_CREATEDEVICE   15
#define D3D8_DEVICE_RESET   14

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

typedef void *(WINAPI *direct3d_create8_t)(UINT sdk_version);
typedef HRESULT (STDMETHODCALLTYPE *create_device_t)(void *self, UINT adapter, DWORD device_type,
                                                     HWND focus_window, DWORD behaviour_flags,
                                                     present_parameters_t *parameters,
                                                     void **returned_device);
typedef HRESULT (STDMETHODCALLTYPE *device_reset_t)(void *self, present_parameters_t *parameters);

static direct3d_create8_t g_original_create;
static create_device_t    g_original_create_device;
static device_reset_t     g_original_reset;
static bool               g_device_hooked;
static bool               g_reset_hooked;
static HWND               g_window;

/* Configuration, read once. Zero means "the size of the desktop", which is what anybody who wants
 * borderless full screen actually means. */
static int g_width;
static int g_height;

/* The size the GAME asked for, which is the size its viewport will be and therefore the size the
 * window has to be. Forcing a bigger back buffer than the game's own mode leaves it drawing into
 * one corner of a surface it never clears; the rest stays black, which is exactly what it looked
 * like from the outside. */
static int g_back_width;
static int g_back_height;

static int screen_width(void)
{
    return GetSystemMetrics(SM_CXSCREEN);
}

static int screen_height(void)
{
    return GetSystemMetrics(SM_CYSCREEN);
}

/* What the window must be: the game's own back buffer, or an override, or the desktop when the
 * game asked for nothing in particular. */
static int wanted_width(void)
{
    if (g_width > 0)       { return g_width; }
    if (g_back_width > 0)  { return g_back_width; }
    return screen_width();
}

static int wanted_height(void)
{
    if (g_height > 0)       { return g_height; }
    if (g_back_height > 0)  { return g_back_height; }
    return screen_height();
}

/* A popup with no frame, at the origin, the size of the screen. The same shape a full-screen
 * window has, without the exclusive mode that goes with it. */
static void apply_window(HWND window)
{
    LONG style;
    RECT client;

    if (window == NULL) {
        return;
    }

    /* Nothing to do if it is already the shape we want. Checked because this runs again after
     * every Reset and on a timer, and a SetWindowPos the game did not ask for is not free. */
    if (IsWindow(window) && !IsIconic(window) && GetClientRect(window, &client) &&
        (client.right - client.left) == wanted_width() &&
        (client.bottom - client.top) == wanted_height() &&
        (GetWindowLongA(window, GWL_STYLE) & (LONG)WS_CAPTION) == 0) {
        g_window = window;
        return;
    }

    if (IsIconic(window)) {
        /* The failure this plugin exists for: something took the foreground, the window went
         * down, and the engine draws nothing while it is down. */
        ShowWindow(window, SW_RESTORE);
    }

    style = GetWindowLongA(window, GWL_STYLE);
    style &= ~(LONG)(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU |
                     WS_BORDER | WS_DLGFRAME);
    style |= (LONG)(WS_POPUP | WS_VISIBLE);
    SetWindowLongA(window, GWL_STYLE, style);

    SetWindowLongA(window, GWL_EXSTYLE,
                   GetWindowLongA(window, GWL_EXSTYLE) & ~(LONG)(WS_EX_DLGMODALFRAME |
                                                                 WS_EX_WINDOWEDGE |
                                                                 WS_EX_CLIENTEDGE |
                                                                 WS_EX_STATICEDGE));

    /* The size asked for is the CLIENT area, which is what the back buffer has to match. */
    {
        RECT wanted;
        int  left;
        int  top;

        wanted.left   = 0;
        wanted.top    = 0;
        wanted.right  = wanted_width();
        wanted.bottom = wanted_height();
        AdjustWindowRectEx(&wanted, (DWORD)GetWindowLongA(window, GWL_STYLE), FALSE,
                           (DWORD)GetWindowLongA(window, GWL_EXSTYLE));

        /* Centred, because a back buffer smaller than the screen is presented one pixel to one
         * pixel and there is nowhere else sensible to put it. */
        left = (screen_width()  - (wanted.right - wanted.left)) / 2;
        top  = (screen_height() - (wanted.bottom - wanted.top)) / 2;
        if (left < 0) { left = 0; }
        if (top  < 0) { top  = 0; }

        SetWindowPos(window, HWND_TOP, left, top, wanted.right - wanted.left,
                     wanted.bottom - wanted.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE);
    }
    ShowWindow(window, SW_SHOW);

    if (window != g_window) {
        log_info("window %08X restyled: borderless, %dx%d, centred on a %dx%d screen",
                 (unsigned)(uintptr_t)window, wanted_width(), wanted_height(),
                 screen_width(), screen_height());
    }
    g_window = window;
}

/* The one edit. Everything else in this plugin exists to reach this function at the right two
 * moments. */
static void make_windowed(const char *what, present_parameters_t *parameters, HWND focus_window)
{
    HWND window;

    if (parameters == NULL) {
        return;
    }

    window = (parameters->device_window != NULL) ? parameters->device_window : focus_window;

    /* THE SIZE IS THE GAME'S BUSINESS. Only the exclusive mode is ours.
     *
     * The first version of this plugin overrode the back buffer to the size of the screen, and
     * that was the bug: the engine's viewport stays the size of the mode it chose, everything it
     * draws lands in one corner of the larger surface, and the rest of the surface is never
     * written to. From outside, a mostly black screen with the game hiding in the top left. */
    if (parameters->back_buffer_width > 0 && parameters->back_buffer_height > 0) {
        g_back_width  = (int)parameters->back_buffer_width;
        g_back_height = (int)parameters->back_buffer_height;
    }

    if (!parameters->windowed) {
        log_info("%s asked for FULLSCREEN %ux%u, same size, as a window", what,
                 parameters->back_buffer_width, parameters->back_buffer_height);
        parameters->windowed                          = TRUE;
        parameters->full_screen_refresh_rate           = 0;  /* refused on a windowed device */
        parameters->full_screen_presentation_interval  = 0;
    }

    (void)window;
}

/* AFTER the call, always. Leaving exclusive fullscreen makes the runtime restore the window's
 * saved style and size, so anything done before Reset is undone by Reset. That restore is why the
 * first version of this plugin left a small window with a title bar and a black inside. */
/* A window this plugin has shaped can still be reshaped by the runtime, the window manager or the
 * game itself, and on Wine it is. So the shape is re-asserted on a slow timer rather than trusted
 * to hold, four times a second, doing nothing at all while it already looks right. */
OF_NORETURN_THREAD_BEGIN
static DWORD WINAPI keeper_thread(void *unused)
{
    (void)unused;

    for (;;) {
        Sleep(250);
        if (g_window != NULL && IsWindow(g_window)) {
            apply_window(g_window);
        }
    }

    /* Not reached: the thread lives as long as the process. The return is here because the
     * signature demands one, not because control can arrive at it. */
    return 0;
}
OF_NORETURN_THREAD_END

static void start_keeper(void)
{
    static bool started;
    HANDLE      thread;

    if (started) {
        return;
    }
    started = true;

    thread = CreateThread(NULL, 0, keeper_thread, NULL, 0, NULL);
    if (thread != NULL) {
        CloseHandle(thread);
    }
}

static HRESULT STDMETHODCALLTYPE hooked_reset(void *self, present_parameters_t *parameters)
{
    HRESULT result;
    HWND    window = (parameters != NULL && parameters->device_window != NULL)
                         ? parameters->device_window : g_window;

    make_windowed("Reset", parameters, window);
    result = g_original_reset(self, parameters);
    apply_window(window);
    return result;
}

static void hook_device(void *device)
{
    void  **vtable;
    DWORD   protection = 0;

    if (g_reset_hooked || device == NULL) {
        return;
    }
    vtable = *(void ***)device;
    if (!memory_is_readable_range((uintptr_t)vtable, (D3D8_DEVICE_RESET + 1) * sizeof(void *))) {
        return;
    }
    if (!VirtualProtect(&vtable[D3D8_DEVICE_RESET], sizeof(void *), PAGE_READWRITE, &protection)) {
        log_warning("the device vtable could not be made writable; a later Reset could still go "
                    "full screen");
        return;
    }

    g_original_reset = (device_reset_t)vtable[D3D8_DEVICE_RESET];
    vtable[D3D8_DEVICE_RESET] = (void *)hooked_reset;
    VirtualProtect(&vtable[D3D8_DEVICE_RESET], sizeof(void *), protection, &protection);

    g_reset_hooked = true;
}

static HRESULT STDMETHODCALLTYPE hooked_create_device(void *self, UINT adapter, DWORD device_type,
                                                      HWND focus_window, DWORD behaviour_flags,
                                                      present_parameters_t *parameters,
                                                      void **returned_device)
{
    HRESULT result;

    make_windowed("CreateDevice", parameters, focus_window);

    result = g_original_create_device(self, adapter, device_type, focus_window, behaviour_flags,
                                      parameters, returned_device);

    if (SUCCEEDED(result)) {
        if (returned_device != NULL) {
            hook_device(*returned_device);
        }
        apply_window((parameters != NULL && parameters->device_window != NULL)
                         ? parameters->device_window : focus_window);
        start_keeper();
    }
    return result;
}

static void hook_device_creation(void *d3d8)
{
    void  **vtable;
    DWORD   protection = 0;

    if (g_device_hooked || d3d8 == NULL) {
        return;
    }
    vtable = *(void ***)d3d8;
    if (!memory_is_readable_range((uintptr_t)vtable, (D3D8_CREATEDEVICE + 1) * sizeof(void *))) {
        log_warning("the IDirect3D8 vtable is not readable, not installing");
        return;
    }
    if (!VirtualProtect(&vtable[D3D8_CREATEDEVICE], sizeof(void *), PAGE_READWRITE, &protection)) {
        log_warning("the IDirect3D8 vtable could not be made writable, not installing");
        return;
    }

    g_original_create_device = (create_device_t)vtable[D3D8_CREATEDEVICE];
    vtable[D3D8_CREATEDEVICE] = (void *)hooked_create_device;
    VirtualProtect(&vtable[D3D8_CREATEDEVICE], sizeof(void *), protection, &protection);

    g_device_hooked = true;
}

static void *WINAPI hooked_direct3d_create8(UINT sdk_version)
{
    void *d3d8 = g_original_create(sdk_version);

    hook_device_creation(d3d8);
    return d3d8;
}

/* ------------------------------------------------------------------------------ the import slot
 *
 * The game calls Direct3DCreate8 exactly once, so the import slot is the only place that is
 * reliably true of. Several plugins may hook the same slot; each keeps whatever it found and
 * forwards to it, so they chain in load order rather than fight. */
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

void borderless_install(void)
{
    void **slot;

    log_init(PLUGIN_SECTION, false);

    /* On by default under Wine and off on Windows, because that is where the difference between
     * "alt-tab is slow" and "the window is minimised and the game draws nothing" lives. The ini
     * overrides either way. */
    if (!ini_read_bool(PLUGIN_SECTION, "Enabled", platform_is_wine())) {
        log_info("Enabled=0, the game takes the screen exclusively as it always did");
        return;
    }
    if (platform_is_wine()) {
        log_info("this is WINE %s, where exclusive full screen loses its window to the focus, "
                 "so this is on unless the ini says otherwise", platform_wine_version());
    }
    if (!host_image_resolve()) {
        log_error("the host image could not be resolved");
        return;
    }

    g_width  = ini_read_int(PLUGIN_SECTION, "Width", 0);
    g_height = ini_read_int(PLUGIN_SECTION, "Height", 0);

    slot = find_import_slot("D3D8.dll", "Direct3DCreate8");
    if (slot == NULL) {
        slot = find_import_slot("d3d8.dll", "Direct3DCreate8");
    }
    if (slot == NULL) {
        log_error("Direct3DCreate8 is not imported by name, cannot install");
        return;
    }

    g_original_create = (direct3d_create8_t)*slot;
    if (!write_pointer(slot, (void *)hooked_direct3d_create8)) {
        log_error("the Direct3DCreate8 import slot could not be made writable");
        return;
    }

    log_info("installed: every device will be windowed at whatever size the game asks for, in a "
             "borderless window centred on the %dx%d screen", screen_width(), screen_height());
}

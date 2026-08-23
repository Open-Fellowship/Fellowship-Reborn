#include "dev_menu.h"
#include "common/compiler.h"
#include "cheats.h"
#include "flags.h"
#include "messages.h"
#include "player.h"
#include "timing.h"
#include "d3d8_min.h"
#include "dinput8_min.h"
#include "overlay.h"

#include "common/camera.h"
#include "common/channel.h"
#include "common/engine_sites.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"

#include <windows.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PLUGIN_SECTION "dev_menu"

/* The key immediately below Escape. VK_OEM_3 is that key on both US and UK layouts, backquote
 * there, and whatever sits in that position elsewhere. The game's own cheats are F5 to F12 and
 * fog_toggle took F1, so this position is free. */
#define DEFAULT_TOGGLE_KEY VK_OEM_3

#define PANEL_X       24
#define PANEL_Y       24
#define PANEL_W       860
/* The panel's height is computed rather than fixed, because FontHeight is configurable and a
 * constant here would cut the cheat buttons off at anything above the default. */
#define PADDING       16

#define COLOUR_PANEL   0xC8101014u
#define COLOUR_EDGE    0xFFC8A43Cu
#define COLOUR_TITLE   0xFFF0D890u
#define COLOUR_LABEL   0xFFD8D8D0u
#define COLOUR_VALUE   0xFFFFFFFFu
#define COLOUR_DIM     0xFF908C84u
#define COLOUR_TRACK   0xFF303038u
#define COLOUR_FILL    0xFF7A6A34u
#define COLOUR_KNOB    0xFFF0D890u
#define COLOUR_BUTTON  0xFF3A3A44u
#define COLOUR_ON      0xFF6AA84Fu

/* The band the slider covers, in degrees of VERTICAL field of view. 55.4 is what 4:3 gives at
 * the game's authored horizontal, so the useful range sits comfortably inside this. */
#define FOV_MIN 40.0f
#define FOV_MAX 130.0f

static int              g_toggle_key = DEFAULT_TOGGLE_KEY;
static int              g_font_height = 16;
static bool             g_visible;
static bool             g_hook_installed;
static bool             g_hook_failed;
static void           **g_vtable;
static d3d8_end_scene_t g_original_end_scene;
static channel_block_t *g_channel;

static bool  g_auto_fov = true;      /* true: field_of_view uses its own value, we ask nothing */
static float g_fov_degrees;          /* the slider's position, once the user has taken over    */

/* Two pages. The camera page is what this menu has always been; the flags page is the engine's
 * own 124-entry developer menu, which does not fit anywhere near the same panel. */
#define TAB_CAMERA   0
#define TAB_FLAGS    1
#define TAB_MESSAGES 2
/* This was TAB_PLAYER while the page held only the size sliders. It now holds anything of ours
 * that the engine has no notion of, which is what "Fellowship Reborn" names. */
#define TAB_FIXES    3
#define TAB_COUNT    4

static int  g_tab;
static int  g_flag_page;
static bool g_show_messages;   /* the BOX. Capturing runs from start-up, see messages.c */
static bool g_show_stats = true;   /* include the per-frame statistics rows in the box */

/* The typed field. -1 when nothing is being edited; while editing, the flag's value is not
 * touched until Enter, so a half-typed number never reaches the engine. */
static int  g_edit_flag = -1;
static char g_edit_text[12];
static int  g_edit_length;   /* include the per-frame statistics rows in the box */

static bool  g_mouse_down;
static bool  g_mouse_was_down;
static int   g_mouse_x;
static int   g_mouse_y;
static bool  g_dragging;

/* NOT GetCursorPos: the game holds the mouse through DirectInput in EXCLUSIVE mode, which
 * freezes the system cursor and stops mouse window messages, so the menu drew and could not be
 * clicked.
 *
 * The shipping path is our own DirectInput device. Raw input is kept as a fallback, through a
 * message-only window of our own, because it costs nothing and a different setup may not have
 * the registration conflict that made it useless here. See README.md. */
static void         *g_mouse_device;          /* our own DirectInput mouse, not the game's */
static void         *g_game_mouse;            /* the game's, so its reads can be silenced   */
static bool          g_take_mouse = true;
static bool          g_intercepting;
static bool          g_mouse_exclusive;       /* did we manage to take it outright?         */
static HWND          g_game_window;
static const char   *g_source = "none";

static volatile LONG g_raw_dx;
static volatile LONG g_raw_dy;
static volatile LONG g_raw_button;
static volatile LONG g_raw_ready;
static float         g_sensitivity = 1.0f;
static int           g_view_w = 1920;
static int           g_view_h = 1080;

/* --------------------------------------------------------------------------- finding things */

/* DirectInput wants a window to hang a cooperative level on. Not for coordinates; there are no
 * coordinates any more, only movement, just for the association. */
static BOOL CALLBACK pick_window(HWND window, LPARAM parameter)
{
    DWORD process = 0;

    (void)parameter;
    GetWindowThreadProcessId(window, &process);
    if (process == GetCurrentProcessId() && IsWindowVisible(window)
        && GetParent(window) == NULL) {
        g_game_window = window;
        return FALSE;
    }
    return TRUE;
}

/* Silencing the game's own mouse reads at the source, because asking DirectInput for it
 * exclusively is refused: the game got there first.
 *
 * EVERY HOOK FORWARDS, and the check is on the DEVICE, not the vtable: DirectInput gives every
 * device of a class the same vtable, so silencing the vtable would take the keyboard with it,
 * and our own mouse. See README.md. */

static direct_input8_create_t g_original_create;
static di8_create_device_t    g_original_create_device;
static di8_get_state_t        g_original_get_state;
static di8_get_data_t         g_original_get_data;

static bool write_pointer(void *slot, void *value)
{
    DWORD protection = 0;

    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &protection)) {
        return false;
    }
    *(void **)slot = value;
    VirtualProtect(slot, sizeof(void *), protection, &protection);
    return true;
}

static HRESULT STDMETHODCALLTYPE hooked_get_state(void *self, DWORD size, void *data)
{
    HRESULT result = g_original_get_state(self, size, data);

    if (self == g_game_mouse && g_visible && data != NULL && SUCCEEDED(result)) {
        memset(data, 0, size);
    }
    return result;
}

static HRESULT STDMETHODCALLTYPE hooked_get_data(void *self, DWORD object_size, void *data,
                                                 DWORD *count, DWORD flags)
{
    HRESULT result = g_original_get_data(self, object_size, data, count, flags);

    /* Forwarded first and only then emptied: the buffered events still have to be drained, or
     * DirectInput's queue overflows and the game gets a fault the moment the menu closes. */
    if (self == g_game_mouse && g_visible && count != NULL && SUCCEEDED(result)) {
        *count = 0;
    }
    return result;
}

static void capture_game_mouse(void *device)
{
    void **vtable;

    if (g_game_mouse != NULL || device == NULL) {
        return;
    }
    vtable = *(void ***)device;

    g_original_get_state = (di8_get_state_t)vtable[DI8_DEV_GETDEVICESTATE];
    g_original_get_data  = (di8_get_data_t)vtable[DI8_DEV_GETDEVICEDATA];

    if (write_pointer(&vtable[DI8_DEV_GETDEVICESTATE], (void *)hooked_get_state)
        && write_pointer(&vtable[DI8_DEV_GETDEVICEDATA], (void *)hooked_get_data)) {
        g_game_mouse   = device;
        g_intercepting = true;
    }
}

static HRESULT STDMETHODCALLTYPE hooked_create_device(void *self, const GUID *device,
                                                      void **out, void *outer)
{
    HRESULT result = g_original_create_device(self, device, out, outer);

    if (SUCCEEDED(result) && out != NULL && *out != NULL && device != NULL
        && memcmp(device, &DEV_GUID_SysMouse, sizeof(GUID)) == 0) {
        capture_game_mouse(*out);
    }
    return result;
}

static HRESULT WINAPI hooked_direct_input8_create(HINSTANCE instance, DWORD version,
                                                  const GUID *riid, void **out, void *outer)
{
    HRESULT result = g_original_create(instance, version, riid, out, outer);

    if (SUCCEEDED(result) && out != NULL && *out != NULL && g_original_create_device == NULL) {
        void **vtable = *(void ***)(*out);
        g_original_create_device = (di8_create_device_t)vtable[DI8_CREATEDEVICE];
        write_pointer(&vtable[DI8_CREATEDEVICE], (void *)hooked_create_device);
    }
    return result;
}

/* The import slot for DINPUT8.dll!DirectInput8Create, found by walking the host's import
 * directory rather than by an address, because an import address table is the one part of a PE
 * whose layout is described by the file itself. */
static void **find_import_slot(const char *dll_name, const char *symbol)
{
    uintptr_t                 base = host_image_base();
    IMAGE_DOS_HEADER         *dos  = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS         *nt;
    IMAGE_IMPORT_DESCRIPTOR  *import;
    DWORD                     rva;

    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
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
                continue;   /* imported by ordinal: no name to compare */
            }
            entry = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
            if (strcmp((const char *)entry->Name, symbol) == 0) {
                return (void **)&addresses->u1.Function;
            }
        }
    }
    return NULL;
}

static void install_input_intercept(void)
{
    void **slot = find_import_slot("DINPUT8.dll", "DirectInput8Create");

    if (slot == NULL) {
        log_warning("DirectInput8Create is not imported where expected; the game will keep "
                    "seeing the mouse while the menu is open");
        return;
    }
    g_original_create = (direct_input8_create_t)*slot;
    if (!write_pointer(slot, (void *)hooked_direct_input8_create)) {
        log_warning("the import slot could not be made writable");
        return;
    }
    log_info("DirectInput8Create %08X -> %08X, so the game's mouse can be muted while the menu "
             "is open", (unsigned)(uintptr_t)g_original_create,
             (unsigned)(uintptr_t)hooked_direct_input8_create);
}

/* ------------------------------------------------------------------------- our own mouse */

static dev_object_format_t mouse_objects[] = {
    { &DEV_GUID_XAxis,  0,  DIDFT_AXIS   | DIDFT_ANYINSTANCE, DIDOI_ASPECTPOSITION },
    { &DEV_GUID_YAxis,  4,  DIDFT_AXIS   | DIDFT_ANYINSTANCE, DIDOI_ASPECTPOSITION },
    { &DEV_GUID_ZAxis,  8,  DIDFT_AXIS   | DIDFT_ANYINSTANCE, DIDOI_ASPECTPOSITION },
    { &DEV_GUID_Button, 12, DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0 },
    { &DEV_GUID_Button, 13, DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0 },
    { &DEV_GUID_Button, 14, DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0 },
    { &DEV_GUID_Button, 15, DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0 }
};

static dev_data_format_t mouse_format = {
    sizeof(dev_data_format_t),
    sizeof(dev_object_format_t),
    DIDF_RELAXIS,
    sizeof(dev_mouse_state_t),
    sizeof(mouse_objects) / sizeof(mouse_objects[0]),
    mouse_objects
};

static bool open_mouse(void)
{
    char                   path[MAX_PATH];
    HMODULE                library;
    direct_input8_create_t create;
    void                  *input = NULL;
    void                  *device = NULL;
    UINT                   length;

    if (g_mouse_device != NULL) {
        return true;
    }
    if (g_game_window == NULL) {
        EnumWindows(pick_window, 0);
    }
    if (g_game_window == NULL) {
        return false;
    }

    /* The SYSTEM dinput8, by full path, on purpose. Plain LoadLibrary("dinput8.dll") would find
     * OUR loader, which is the proxy sitting in the game folder under that name; going straight
     * to the real one keeps this plugin working whatever the loader is doing. */
    length = GetSystemDirectoryA(path, MAX_PATH);
    if (length == 0 || length > MAX_PATH - 16) {
        return false;
    }
    strcat(path, "\\dinput8.dll");

    library = LoadLibraryA(path);
    if (library == NULL) {
        return false;
    }
    create = (direct_input8_create_t)(void *)GetProcAddress(library, "DirectInput8Create");
    if (create == NULL) {
        return false;
    }
    if (FAILED(create(GetModuleHandleA(NULL), DIRECTINPUT_VERSION_8, &DEV_IID_IDirectInput8A,
                      &input, NULL)) || input == NULL) {
        return false;
    }
    if (FAILED(((di8_create_device_t)(*(void ***)input)[DI8_CREATEDEVICE])(
            input, &DEV_GUID_SysMouse, &device, NULL)) || device == NULL) {
        return false;
    }
    if (FAILED(((di8_set_format_t)(*(void ***)device)[DI8_DEV_SETDATAFORMAT])(
            device, &mouse_format))) {
        return false;
    }

    /* Exclusive first. If the game is not holding the mouse exclusively we take it, and it stops
     * seeing the movement while the menu is open, which is the point of a menu. If it is, we
     * share, and the game keeps reacting to the same movement we do. Either way the pointer
     * works; only whether the world moves underneath it changes. */
    if (SUCCEEDED(((di8_set_coop_t)(*(void ***)device)[DI8_DEV_SETCOOPLEVEL])(
            device, g_game_window, DISCL_EXCLUSIVE | DISCL_FOREGROUND))
        && SUCCEEDED(((di8_acquire_t)(*(void ***)device)[DI8_DEV_ACQUIRE])(device))) {
        g_mouse_exclusive = true;
    } else if (SUCCEEDED(((di8_set_coop_t)(*(void ***)device)[DI8_DEV_SETCOOPLEVEL])(
                   device, g_game_window, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND))
               && SUCCEEDED(((di8_acquire_t)(*(void ***)device)[DI8_DEV_ACQUIRE])(device))) {
        g_mouse_exclusive = false;
    } else {
        return false;
    }

    g_mouse_device = device;
    log_info("mouse opened through DirectInput, %s",
             g_mouse_exclusive ? "exclusively; the game will not see it while the menu is open"
                               : "shared, the game still sees the same movement");
    return true;
}

/* Movement since the last call, or false when there is none to be had. */
static bool poll_mouse(int *dx, int *dy, bool *button)
{
    dev_mouse_state_t state;
    HRESULT           result;

    if (g_mouse_device == NULL) {
        return false;
    }
    memset(&state, 0, sizeof(state));
    result = ((di8_get_state_t)(*(void ***)g_mouse_device)[DI8_DEV_GETDEVICESTATE])(
        g_mouse_device, sizeof(state), &state);

    if (result == DIERR_INPUTLOST || result == DIERR_NOTACQUIRED) {
        /* Alt-tab, or the game taking it back. Re-acquiring every tick until it works is what
         * every DirectInput loop does, and it is why this returns false rather than giving up. */
        ((di8_acquire_t)(*(void ***)g_mouse_device)[DI8_DEV_ACQUIRE])(g_mouse_device);
        return false;
    }
    if (FAILED(result)) {
        return false;
    }

    *dx     = (int)state.x;
    *dy     = (int)state.y;
    *button = (state.buttons[0] & 0x80) != 0;
    return true;
}

/* The device, reached the way the engine reaches it, with every step checked. A wrong guess here
 * is a call through a pointer that is not a vtable, so nothing is believed until all of it is. */
static void *find_device(void)
{
    uint32_t renderer = 0;
    uint32_t device   = 0;
    void   **vtable;
    int      index;

    if (!memory_read_u32(exe_site(EXE_RENDERER_PTR), &renderer) || renderer == 0) {
        return NULL;
    }
    if (!memory_is_readable_range((uintptr_t)renderer, RENDERER_D3D_DEVICE + 4u)) {
        return NULL;
    }
    if (!memory_read_u32((uintptr_t)renderer + RENDERER_D3D_DEVICE, &device) || device == 0) {
        return NULL;
    }
    if (!memory_is_readable_range((uintptr_t)device, sizeof(void *))) {
        return NULL;
    }

    vtable = *(void ***)(uintptr_t)device;
    if (!memory_is_readable_range((uintptr_t)vtable,
                                  sizeof(void *) * D3D8_VTABLE_ENTRIES_USED)) {
        return NULL;
    }

    /* Every entry has to be a readable address, and the module it lives in has to be the same
     * one for all of them. A real vtable satisfies both; a structure that merely happens to
     * start with a plausible pointer does not. */
    {
        HMODULE owner = NULL;
        for (index = 0; index < D3D8_VTABLE_ENTRIES_USED; ++index) {
            HMODULE module = NULL;
            if (vtable[index] == NULL
                || !memory_is_readable_range((uintptr_t)vtable[index], 1u)) {
                return NULL;
            }
            if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                    | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                    (LPCSTR)vtable[index], &module) || module == NULL) {
                return NULL;
            }
            if (owner == NULL) {
                owner = module;
            } else if (owner != module) {
                return NULL;
            }
        }
    }

    g_vtable = vtable;
    return (void *)(uintptr_t)device;
}

/* --------------------------------------------------------------------------------- raw input */

#define RAW_WINDOW_CLASS "FellowshipRebornDevMenuInput"

static LRESULT CALLBACK raw_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_INPUT) {
        RAWINPUT input;
        UINT     size = sizeof(input);

        if (GetRawInputData((HRAWINPUT)lparam, RID_INPUT, &input, &size,
                            sizeof(RAWINPUTHEADER)) != (UINT)-1
            && input.header.dwType == RIM_TYPEMOUSE) {
            const RAWMOUSE *mouse = &input.data.mouse;

            /* Almost every mouse reports relative movement. A tablet, a remote desktop session
             * or a virtual machine reports absolute, in a 0..65535 box, and saying so here is
             * cheaper than someone later discovering the menu only works on real hardware. */
            if ((mouse->usFlags & MOUSE_MOVE_ABSOLUTE) != 0) {
                int screen_w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
                int screen_h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
                if (screen_w > 0 && screen_h > 0) {
                    g_mouse_x = (int)((long long)mouse->lLastX * screen_w / 65535);
                    g_mouse_y = (int)((long long)mouse->lLastY * screen_h / 65535);
                    InterlockedExchange(&g_raw_ready, 2);
                }
            } else if (mouse->lLastX != 0 || mouse->lLastY != 0) {
                InterlockedExchangeAdd(&g_raw_dx, mouse->lLastX);
                InterlockedExchangeAdd(&g_raw_dy, mouse->lLastY);
                InterlockedExchange(&g_raw_ready, 1);
            }

            if ((mouse->usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN) != 0) {
                InterlockedExchange(&g_raw_button, 1);
            }
            if ((mouse->usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP) != 0) {
                InterlockedExchange(&g_raw_button, 0);
            }
        }
        return 0;
    }
    return DefWindowProcA(window, message, wparam, lparam);
}

static HWND create_raw_window(void)
{
    WNDCLASSA class_info;
    HINSTANCE instance = GetModuleHandleA(NULL);
    HWND      window;

    memset(&class_info, 0, sizeof(class_info));
    class_info.lpfnWndProc   = raw_window_proc;
    class_info.hInstance     = instance;
    class_info.lpszClassName = RAW_WINDOW_CLASS;

    /* A class that is already registered is not an error: it means this ran twice. */
    RegisterClassA(&class_info);

    window = CreateWindowExA(0, RAW_WINDOW_CLASS, "", 0, 0, 0, 0, 0,
                             HWND_MESSAGE, NULL, instance, NULL);
    if (window == NULL) {
        return NULL;
    }

    {
        RAWINPUTDEVICE device;
        device.usUsagePage = 0x01;    /* generic desktop */
        device.usUsage     = 0x02;    /* mouse */
        /* INPUTSINK: deliver even when our window is not the foreground one, which it never is. */
        device.dwFlags     = RIDEV_INPUTSINK;
        device.hwndTarget  = window;

        if (!RegisterRawInputDevices(&device, 1, sizeof(device))) {
            DestroyWindow(window);
            return NULL;
        }
    }
    return window;
}

/* ------------------------------------------------------------------------------- the drawing */

static float clampf(float value, float low, float high)
{
    if (value < low)  { return low; }
    if (value > high) { return high; }
    return value;
}

/* Only degrees are needed here. This plugin never turns an angle into a focal length; that is
 * field_of_view's job, and keeping the conversion in one place is the point of the channel. */
static double to_degrees(double radians) { return radians * 180.0 / 3.14159265358979323846; }

static bool inside(int x, int y, int w, int h)
{
    return g_mouse_x >= x && g_mouse_x < x + w && g_mouse_y >= y && g_mouse_y < y + h;
}

static void read_mouse(void)
{
    int  dx = 0;
    int  dy = 0;
    bool button = false;

    g_mouse_was_down = g_mouse_down;

    if (poll_mouse(&dx, &dy, &button)) {
        g_mouse_down = button;
        g_source     = g_mouse_exclusive ? "DirectInput (exclusive)" : "DirectInput (shared)";
    } else {
        /* Raw input is kept as the second string only because it costs nothing to keep. It is
         * beaten by DirectInput8's own registration inside this process on the machine tested,
         * but a different setup may not have that problem, and the fallback is free. */
        LONG raw_dx = InterlockedExchange(&g_raw_dx, 0);
        LONG raw_dy = InterlockedExchange(&g_raw_dy, 0);

        dx = (int)raw_dx;
        dy = (int)raw_dy;
        g_mouse_down = InterlockedCompareExchange(&g_raw_button, 0, 0) != 0;
        if (dx != 0 || dy != 0) {
            g_source = "raw input";
        }
    }

    g_mouse_x += (int)((float)dx * g_sensitivity);
    g_mouse_y += (int)((float)dy * g_sensitivity);

    if (g_mouse_x < 0)            { g_mouse_x = 0; }
    if (g_mouse_y < 0)            { g_mouse_y = 0; }
    if (g_mouse_x > g_view_w - 1) { g_mouse_x = g_view_w - 1; }
    if (g_mouse_y > g_view_h - 1) { g_mouse_y = g_view_h - 1; }
}

/* Our own pointer, because the system one is hidden and frozen while the game holds the mouse.
 * A filled wedge of shrinking rows: recognisably a cursor, and eight rectangles. */
static void draw_pointer(void)
{
    int i;

    overlay_rect(g_mouse_x, g_mouse_y, 13, 1, 0xFF000000u);
    for (i = 0; i < 12; ++i) {
        overlay_rect(g_mouse_x + 1, g_mouse_y + i + 1, 12 - i, 1, 0xFFFFFFFFu);
        overlay_rect(g_mouse_x + 13 - i, g_mouse_y + i + 1, 1, 1, 0xFF000000u);
    }
    overlay_rect(g_mouse_x, g_mouse_y, 1, 13, 0xFF000000u);
}

/* ------------------------------------------------------------------------------ the layout
 *
 * ONE definition of where everything is, used by the drawing and by the hit testing. The field
 * of view row predates this and computed its geometry twice, once in each place; that has been
 * pulled into content_top() below rather than repeated a third time.
 */
#define CHEAT_COLUMNS 2
#define CHEAT_GAP     10
#define FLAG_COLUMNS  2
#define FLAG_GAP      12
#define FLAG_PANEL_W  900

static int row_step(void)
{
    return overlay_line_height() + 6;
}

static int panel_width(void)
{
    int width = (g_tab == TAB_FLAGS || g_tab == TAB_MESSAGES) ? FLAG_PANEL_W : PANEL_W;
    int room  = g_view_w - PANEL_X * 2;

    if (room > 0 && width > room) { width = room; }
    if (width < 320)              { width = 320; }
    return width;
}

/* The first row below the title and the tab strip. Everything on the camera page hangs off this,
 * so adding the strip moved the slider and its hit box together rather than one of them. */
static int content_top(void)
{
    int step = row_step();
    return PANEL_Y + PADDING + (step + 6) + (step + 6);
}

static void messages_button_rect(int *x, int *y, int *w, int *h)
{
    *w = overlay_text_width(" engine messages: off ", 1) + 10;
    *h = overlay_line_height() + 4;
    *x = PANEL_X + panel_width() - PADDING - *w;
    *y = PANEL_Y + PADDING + row_step() + 4;
}

static const char *const g_tab_labels[TAB_COUNT] = {
    " camera ", " engine flags ", " messages ", " Fellowship Reborn "
};

/* Each tab is as wide as ITS OWN label, and sits after the ones before it.
 *
 * Every tab used to be given the width of " engine flags ", which was fine while that was the
 * longest. The fourth tab is longer, so its text ran out of its box and over the button beside
 * it. Measuring per tab means adding another one, or renaming one, can never do that again. */
static void tab_rect(int index, int *x, int *y, int *w, int *h)
{
    int i;

    *x = PANEL_X + PADDING;
    for (i = 0; i < index && i < TAB_COUNT; ++i) {
        *x += overlay_text_width(g_tab_labels[i], 1) + 10 + 8;
    }
    *w = overlay_text_width(g_tab_labels[index < TAB_COUNT ? index : 0], 1) + 10;
    *h = overlay_line_height() + 4;
    *y = PANEL_Y + PADDING + row_step() + 4;
}

/* -------------------------------------------------------------------------- the camera page */

static int cheats_top(void)
{
    int step = row_step();
    return content_top() + step + (step + 8) + step * 3 + 8;
}

static int cheat_rows(void)
{
    return (CHEAT_COUNT + CHEAT_COLUMNS - 1) / CHEAT_COLUMNS;
}

static void cheat_button_rect(int index, int *bx, int *by, int *bw, int *bh)
{
    int step  = row_step();
    int width = (panel_width() - PADDING * 2 - CHEAT_GAP * (CHEAT_COLUMNS - 1)) / CHEAT_COLUMNS;

    *bx = PANEL_X + PADDING + (index % CHEAT_COLUMNS) * (width + CHEAT_GAP);
    *by = cheats_top() + step + (index / CHEAT_COLUMNS) * (step + 4);
    *bw = width;
    *bh = overlay_line_height() + 6;
}

/* Defined further down with the rest of the input helpers, and declared here because this page is
 * laid out above them. Released inside the box, which is what a button is, as opposed to
 * inside(), which is a hover and fires every frame. */
static bool clicked(int x, int y, int w, int h);

#define SIZE_OPTION_COUNT 3

/* Which size the user last chose. Re-applied every frame from the EndScene hook, because the
 * engine rewrites the orientation matrix from animation and would otherwise undo it within a
 * frame. -1 means "not chosen", which is different from Normal: Normal actively holds the matrix
 * at unit scale, and not-chosen leaves the engine entirely alone. */
/* Size is height, build is width on top of it. Both are held as FLOATS rather than as a chosen
 * button, because the sliders below can land anywhere between the presets and the buttons are
 * just quick ways to reach a value. A button lights up when the value happens to be its own. */
static float g_size_value  = 1.0f;
static float g_build_value = 1.0f;

/* Whether something non-neutral is currently written. Restoring has to happen ONCE when the
 * values come back to 1, not every frame afterwards; the scale vector is not something the
 * engine rewrites, so there is nothing to keep correcting once it is back. */
static bool  g_player_active;

/* 0 none, 1 size, 2 build. Grab anywhere on a track and keep the grab until the button comes up,
 * so a fast drag that wanders off the track vertically does not drop the knob, the same rule the
 * field of view slider follows. */
static int   g_player_drag;

/* Named for the sliders rather than for the rows, because SIZE_MAX is a standard
 * library macro from <stdint.h> and redefining it is a warning this tree treats as an
 * error. */
#define HEIGHT_LOW   0.10f
#define HEIGHT_HIGH   4.00f
#define WIDTH_LOW  0.25f
#define WIDTH_HIGH  10.00f

static float chosen_height(void)
{
    return g_size_value;
}

static float chosen_girth(void)
{
    return g_size_value * g_build_value;
}

/* One place decides where every row of this page sits, and the drawing and the hit testing both
 * read it. A rectangle computed twice is a button that looks right and cannot be clicked. */
static int player_row(int n)
{
    return content_top() + n * (row_step() + 6);
}

/* Laid out like the field of view slider so the two feel the same: a label, a track, and the
 * number on the right. `row` is which row of the page the track sits on. */
/* Close enough to have come from a reset rather than from a drag that happened to land near 1.0.
 * Half a percent is under one pixel of travel on either track. */
static bool is_preset(float value, float preset)
{
    return (float)fabs((double)(value - preset)) < 0.005f;
}

static void player_reset_rect(int row, int *bx, int *by, int *bw, int *bh)
{
    *bw = overlay_text_width(" reset ", 1) + 10;
    *bh = overlay_line_height() + 6;
    *bx = PANEL_X + panel_width() - PADDING - *bw;
    *by = player_row(row) - 3;
}

static void player_slider_rect(int row, int *tx, int *ty, int *tw)
{
    int bx;
    int by;
    int bw;
    int bh;

    player_reset_rect(row, &bx, &by, &bw, &bh);
    *tx = PANEL_X + PADDING + 110;
    *ty = player_row(row) + overlay_line_height() / 2 - 3;
    /* Up to the number, which sits just left of the reset button. Derived from the button's own
     * rectangle rather than from a second guess at the same arithmetic. */
    *tw = (bx - 10 - overlay_text_width("00.00", 1) - 10) - *tx;
}

static void draw_player_slider(int row, const char *label, float value, float low, float high,
                               bool usable)
{
    char  line[64];
    int   tx;
    int   ty;
    int   tw;
    int   knob;
    float fraction;

    player_slider_rect(row, &tx, &ty, &tw);
    if (tw < 20) {
        return;
    }
    fraction = (clampf(value, low, high) - low) / (high - low);
    knob     = tx + (int)(fraction * (float)(tw - 10));

    overlay_text(PANEL_X + PADDING, player_row(row), 1, COLOUR_LABEL, label);
    overlay_rect(tx, ty, tw, 6, COLOUR_TRACK);
    overlay_rect(tx, ty, knob - tx + 10, 6, usable ? COLOUR_FILL : COLOUR_TRACK);
    overlay_rect(knob, ty - 7, 10, 20, usable ? COLOUR_KNOB : COLOUR_DIM);

    sprintf(line, "%.2f", (double)value);
    {
        int bx;
        int by;
        int bw;
        int bh;

        player_reset_rect(row, &bx, &by, &bw, &bh);
        overlay_text(bx - 10 - overlay_text_width("00.00", 1), player_row(row), 1,
                     usable ? COLOUR_VALUE : COLOUR_DIM, line);

        /* Dim unless there is something to undo, so the button says whether it would do anything
         * before it is pressed. */
        overlay_rect(bx, by, bw, bh,
                     (usable && !is_preset(value, 1.0f)) ? COLOUR_BUTTON : COLOUR_TRACK);
        overlay_text(bx + 6, by + 3, 1,
                     (usable && !is_preset(value, 1.0f)) ? COLOUR_VALUE : COLOUR_DIM, "reset");
    }
}

static void draw_player(void)
{
    int         x      = PANEL_X + PADDING;
    const char *why    = "";
    uintptr_t   object = player_object(&why);
    bool        usable = (object != 0);

    overlay_text(x, player_row(0), 1, COLOUR_TITLE, "Player size");
    if (!usable) {
        overlay_text(x + overlay_text_width("Player size    ", 1), player_row(0), 1, COLOUR_DIM,
                     why[0] != 0 ? why : "no player");
    }

    draw_player_slider(1, "Height", g_size_value,  HEIGHT_LOW, HEIGHT_HIGH, usable);
    draw_player_slider(2, "Width",  g_build_value, WIDTH_LOW,  WIDTH_HIGH,  usable);

    overlay_text(x, player_row(3), 1, COLOUR_DIM,
                 "width is on top of height; the camera holds its distance either way");
}

#define FPS_ROW_TITLE   4
#define FPS_ROW_SLIDER  5
#define FPS_ROW_BUTTONS 6
#define FPS_ROW_STATS   7
#define FPS_ROW_HINT    8
#define FPS_ROW_COUNT   9

#define FPS_PRESET_COUNT 4
static const int g_fps_presets[FPS_PRESET_COUNT] = { 30, 60, 120, 144 };

static int g_fps_drag;   /* 1 while the frame rate track is grabbed, 0 otherwise */

static void fps_uncapped_rect(int *bx, int *by, int *bw, int *bh)
{
    *bw = overlay_text_width(" uncapped ", 1) + 10;
    *bh = overlay_line_height() + 6;
    *bx = PANEL_X + panel_width() - PADDING - *bw;
    *by = player_row(FPS_ROW_SLIDER) - 3;
}

static void fps_slider_rect(int *tx, int *ty, int *tw)
{
    int bx;
    int by;
    int bw;
    int bh;

    fps_uncapped_rect(&bx, &by, &bw, &bh);
    *tx = PANEL_X + PADDING + 110;
    *ty = player_row(FPS_ROW_SLIDER) + overlay_line_height() / 2 - 3;
    *tw = (bx - 10 - overlay_text_width("0000", 1) - 10) - *tx;
}

static void fps_preset_rect(int index, int *bx, int *by, int *bw, int *bh)
{
    int width = overlay_text_width(" 000 ", 1) + 10;

    *bw = width;
    *bh = overlay_line_height() + 6;
    *bx = PANEL_X + PADDING + index * (width + 8);
    *by = player_row(FPS_ROW_BUTTONS) - 3;
}

static void fps_save_rect(int *bx, int *by, int *bw, int *bh)
{
    *bw = overlay_text_width(" save as default ", 1) + 10;
    *bh = overlay_line_height() + 6;
    *bx = PANEL_X + panel_width() - PADDING - *bw;
    *by = player_row(FPS_ROW_BUTTONS) - 3;
}

static void draw_frame_rate(void)
{
    char     line[160];
    int      x    = PANEL_X + PADDING;
    float    target = timing_target();
    unsigned rate = timing_tick_rate();
    int      tx;
    int      ty;
    int      tw;
    int      bx;
    int      by;
    int      bw;
    int      bh;
    int      index;

    /* ---- the title, and which clock the engine is on. That second part is the honest answer to
     * "is the fix even installed", read out of the engine rather than out of our own state. */
    overlay_rect(x, player_row(FPS_ROW_TITLE) - 8, panel_width() - PADDING * 2, 1, COLOUR_TRACK);
    overlay_text(x, player_row(FPS_ROW_TITLE), 1, COLOUR_TITLE, "Frame rate");

    if (rate == 0u) {
        sprintf(line, "the engine timer has not been built yet");
    } else if (rate <= 1000u) {
        sprintf(line, "clock: GetTickCount at %u Hz, frame_timing is not installed, so the "
                      "delta below is quantised to 15.6 ms", rate);
    } else {
        sprintf(line, "clock: QueryPerformanceCounter at %u Hz", rate);
    }
    overlay_text(x + overlay_text_width("Frame rate    ", 1), player_row(FPS_ROW_TITLE), 1,
                 (rate > 1000u) ? COLOUR_DIM : COLOUR_EDGE, line);

    /* ---- the slider */
    fps_slider_rect(&tx, &ty, &tw);
    if (tw >= 20) {
        float fraction = (clampf(target <= 0.0f ? TIMING_FPS_HIGH : target,
                                 TIMING_FPS_LOW, TIMING_FPS_HIGH) - TIMING_FPS_LOW)
                         / (TIMING_FPS_HIGH - TIMING_FPS_LOW);
        int   knob     = tx + (int)(fraction * (float)(tw - 10));
        bool  capped   = (target > 0.0f);

        overlay_text(x, player_row(FPS_ROW_SLIDER), 1, COLOUR_LABEL, "Target");
        overlay_rect(tx, ty, tw, 6, COLOUR_TRACK);
        overlay_rect(tx, ty, knob - tx + 10, 6, capped ? COLOUR_FILL : COLOUR_TRACK);
        overlay_rect(knob, ty - 7, 10, 20, capped ? COLOUR_KNOB : COLOUR_DIM);

        if (capped) {
            sprintf(line, "%d", (int)(target + 0.5f));
        } else {
            sprintf(line, "off");
        }
        fps_uncapped_rect(&bx, &by, &bw, &bh);
        overlay_text(bx - 10 - overlay_text_width("0000", 1), player_row(FPS_ROW_SLIDER), 1,
                     capped ? COLOUR_VALUE : COLOUR_DIM, line);

        overlay_rect(bx, by, bw, bh, capped ? COLOUR_BUTTON : COLOUR_ON);
        overlay_text(bx + 6, by + 3, 1, COLOUR_VALUE, "uncapped");
    }

    /* ---- the presets, and the save button */
    for (index = 0; index < FPS_PRESET_COUNT; ++index) {
        bool on = (target > 0.0f)
                  && (float)fabs((double)(target - (float)g_fps_presets[index])) < 0.5f;

        fps_preset_rect(index, &bx, &by, &bw, &bh);
        sprintf(line, "%d", g_fps_presets[index]);
        overlay_rect(bx, by, bw, bh, on ? COLOUR_ON : COLOUR_BUTTON);
        overlay_text(bx + (bw - overlay_text_width(line, 1)) / 2, by + 3, 1, COLOUR_VALUE, line);
    }

    fps_save_rect(&bx, &by, &bw, &bh);
    overlay_rect(bx, by, bw, bh, timing_saved() ? COLOUR_TRACK : COLOUR_BUTTON);
    overlay_text(bx + 6, by + 3, 1, timing_saved() ? COLOUR_DIM : COLOUR_VALUE,
                 "save as default");

    /* ---- what the engine's own counter says */
    {
        float engine = timing_engine_fps();

        if (engine > 0.0f) {
            sprintf(line, "engine fps %.1f", (double)engine);
        } else {
            sprintf(line, "engine fps not yet sampled");
        }
        overlay_text(x, player_row(FPS_ROW_STATS), 1, COLOUR_DIM, line);
    }

    overlay_text(x, player_row(FPS_ROW_HINT), 1, COLOUR_DIM,
                 "the slider drives fps_limit; save writes MaxFPS into fellowship_reborn.ini");
}

static void handle_frame_rate_input(void)
{
    int   bx;
    int   by;
    int   bw;
    int   bh;
    int   tx;
    int   ty;
    int   tw;
    int   index;
    float target = timing_target();

    fps_uncapped_rect(&bx, &by, &bw, &bh);
    if (clicked(bx, by, bw, bh)) {
        /* Off turns on at whatever the slider last showed, and 60 when it has never shown one,
         * so the button is a toggle rather than a one-way door. */
        timing_set_target(target > 0.0f ? 0.0f : 60.0f);
        return;
    }

    for (index = 0; index < FPS_PRESET_COUNT; ++index) {
        fps_preset_rect(index, &bx, &by, &bw, &bh);
        if (clicked(bx, by, bw, bh)) {
            timing_set_target((float)g_fps_presets[index]);
            return;
        }
    }

    fps_save_rect(&bx, &by, &bw, &bh);
    if (clicked(bx, by, bw, bh)) {
        timing_save();
        return;
    }

    /* Grab on the way down and hold it until the button comes up, exactly as the two sliders
     * above do. Dragging the track also takes it off uncapped, because moving a slider is an
     * unambiguous request for the value under the knob. */
    fps_slider_rect(&tx, &ty, &tw);
    if (g_mouse_down && !g_mouse_was_down && inside(tx - 6, ty - 10, tw + 12, 26)) {
        g_fps_drag = 1;
    }
    if (!g_mouse_down) {
        g_fps_drag = 0;
    }
    if (g_fps_drag != 0 && tw > 12) {
        float fraction = (float)(g_mouse_x - tx) / (float)(tw - 10);
        float value    = clampf(TIMING_FPS_LOW + fraction * (TIMING_FPS_HIGH - TIMING_FPS_LOW),
                                TIMING_FPS_LOW, TIMING_FPS_HIGH);

        /* Whole frames per second. A fractional target is meaningless to a person and makes the
         * preset buttons impossible to light up. */
        value = (float)(int)(value + 0.5f);
        if (value != target) {
            timing_set_target(value);
        }
    }
}

/* Both sliders, and the buttons, all end here. Writing on every frame rather than only on change
 * is what makes a drag smooth: the value moves with the mouse and the next frame shows it. */
static void apply_player(void)
{
    const char *why = "";

    if (!player_apply_size(chosen_girth(), chosen_height(), &why)) {
        log_info("player size: %s", why);
    }
}

static void handle_player_input(void)
{
    int row;
    int bx;
    int by;
    int bw;
    int bh;
    int tx;
    int ty;
    int tw;

    for (row = 1; row <= 2; ++row) {
        player_reset_rect(row, &bx, &by, &bw, &bh);
        if (clicked(bx, by, bw, bh)) {
            if (row == 1) {
                g_size_value = 1.0f;
            } else {
                g_build_value = 1.0f;
            }
            apply_player();
            return;
        }
    }

    /* Grab on the way down, anywhere on the track, and hold it until the button comes up. */
    if (g_mouse_down && !g_mouse_was_down) {
        for (row = 1; row <= 2; ++row) {
            player_slider_rect(row, &tx, &ty, &tw);
            if (inside(tx - 6, ty - 10, tw + 12, 26)) {
                g_player_drag = row;
                break;
            }
        }
    }
    if (!g_mouse_down) {
        g_player_drag = 0;
    }

    if (g_player_drag != 0) {
        float low  = (g_player_drag == 1) ? HEIGHT_LOW  : WIDTH_LOW;
        float high = (g_player_drag == 1) ? HEIGHT_HIGH : WIDTH_HIGH;

        player_slider_rect(g_player_drag, &tx, &ty, &tw);
        if (tw > 12) {
            float fraction = (float)(g_mouse_x - tx) / (float)(tw - 10);
            float value    = clampf(low + fraction * (high - low), low, high);

            if (g_player_drag == 1) {
                g_size_value = value;
            } else {
                g_build_value = value;
            }
            apply_player();
        }
    }
}

/* Called every frame from the EndScene hook, whether or not the menu is open; a size has to
 * survive the menu closing, and the engine rewrites the transform from animation regardless of
 * what is on screen. */
static void player_hold_size(void)
{
    const char *why = "";

    if (is_preset(g_size_value, 1.0f) && is_preset(g_build_value, 1.0f)) {
        /* Back to normal. Restore ONCE and then leave the engine alone: the scale vector is not
         * something it rewrites, so there is nothing to keep correcting, and writing neutral
         * values sixty times a second would be sixty pointless writes into a live object. */
        if (g_player_active) {
            player_apply_size(1.0f, 1.0f, &why);
            g_player_active = false;
        }
        return;
    }
    if (player_apply_size(chosen_girth(), chosen_height(), &why)) {
        g_player_active = true;
    }
}

/* Rows are not all the same height, so the page is laid out once per frame into a table that
 * the drawing and the hit testing both read. Nothing computes a rectangle twice. */
#define FLAG_MAX_ROWS   64
#define FLAG_MAX_PAGES  16
#define FLAG_SWITCH_W   52

typedef struct flag_row {
    int  index;
    int  x;
    int  y;
    int  w;
    int  line;          /* height of one line: the switch sits on this */
    bool has_picker;    /* a second line underneath with value,, and + */
} flag_row_t;

static flag_row_t g_rows[FLAG_MAX_ROWS];
static int        g_row_count;
static int        g_page_starts[FLAG_MAX_PAGES];
static int        g_page_count;

static int flag_rows(void)
{
    int step = row_step();
    int room = g_view_h - PANEL_Y * 2 - (content_top() - PANEL_Y) - step * 2 - PADDING;
    int rows = (step > 0) ? room / step : 0;

    if (rows < 4)  { rows = 4; }
    if (rows > 22) { rows = 22; }   /* two columns of these stay inside the vertex batch */
    return rows;
}

static int flag_row_height(int index)
{
    int step = row_step();
    return (flags_kind(index) == FLAG_CYCLE) ? step * 2 : step;
}

static bool flag_is_number(int index)
{
    return flags_kind(index) == FLAG_NUMBER;
}

/* Fills one page starting at `first` and returns the index the next page would start at. Rows are
 * only recorded when `collect` is set, so the same walk both measures the pages and builds the
 * one being shown. */
static int layout_page(int first, bool collect)
{
    int step      = row_step();
    int column_h  = flag_rows() * step;
    int width     = (panel_width() - PADDING * 2 - FLAG_GAP * (FLAG_COLUMNS - 1)) / FLAG_COLUMNS;
    int index     = first;
    int column;

    if (collect) { g_row_count = 0; }

    for (column = 0; column < FLAG_COLUMNS; ++column) {
        int x = PANEL_X + PADDING + column * (width + FLAG_GAP);
        int y = content_top();

        while (index < FLAG_COUNT) {
            int height = flag_row_height(index);

            if (y + height > content_top() + column_h) {
                break;
            }
            if (collect && g_row_count < FLAG_MAX_ROWS) {
                flag_row_t *row = &g_rows[g_row_count++];
                row->index      = index;
                row->x          = x;
                row->y          = y;
                row->w          = width;
                row->line       = overlay_line_height() + 2;
                row->has_picker = (flags_kind(index) == FLAG_CYCLE);
            }
            y += height;
            ++index;
        }
    }

    return index;
}

static void layout_flags(void)
{
    int index = 0;

    g_page_count = 0;
    while (index < FLAG_COUNT && g_page_count < FLAG_MAX_PAGES) {
        int next;

        g_page_starts[g_page_count++] = index;
        next = layout_page(index, false);
        if (next <= index) {
            break;                            /* no forward progress: stop rather than spin */
        }
        index = next;
    }
    if (g_page_count == 0) { g_page_count = 1; g_page_starts[0] = 0; }
    if (g_flag_page >= g_page_count) { g_flag_page = g_page_count - 1; }

    layout_page(g_page_starts[g_flag_page], true);
}

static void flag_switch_rect(const flag_row_t *row, int *bx, int *by, int *bw, int *bh)
{
    *bw = FLAG_SWITCH_W;
    *bh = row->line;
    *bx = row->x + row->w - FLAG_SWITCH_W;
    *by = row->y;
}

static void flag_step_rect(const flag_row_t *row, bool plus, int *bx, int *by, int *bw, int *bh)
{
    *bw = 18;
    *bh = row->line;
    *bx = row->x + row->w - (plus ? 20 : 42);
    *by = row->y + (row->has_picker ? row_step() : 0);   /* second line for a cycle, same line
                                                          * for a typed number */
}

/* The box a number is typed into. Wide enough for a five figure coordinate and a minus sign. */
static void flag_field_rect(const flag_row_t *row, int *bx, int *by, int *bw, int *bh)
{
    *bw = 76;
    *bh = row->line;
    *bx = row->x + row->w - 44 - *bw;
    *by = row->y;
}

static void page_button_rect(bool next, int *bx, int *by, int *bw, int *bh)
{
    *bw = overlay_text_width(" next ", 1) + 8;
    *bh = overlay_line_height() + 6;
    *bx = PANEL_X + PADDING + (next ? (*bw + 8) : 0);
    *by = content_top() + flag_rows() * row_step() + 6;
}

static int panel_height(void)
{
    int step = row_step();

    if (g_tab == TAB_FLAGS || g_tab == TAB_MESSAGES) {
        return content_top() - PANEL_Y + flag_rows() * step + step + 6 + PADDING;
    }
    /* This page is a fixed number of rows, so it says so rather than inheriting the camera
     * page's height and hoping the frame rate readout lands inside it. */
    if (g_tab == TAB_FIXES) {
        return player_row(FPS_ROW_COUNT) - PANEL_Y + PADDING;
    }
    return cheats_top() - PANEL_Y + step + cheat_rows() * (step + 4) + PADDING;
}

static void draw_cheats(int x, bool have_camera)
{
    bool available = have_camera && cheats_available();
    int  index;
    int  y = cheats_top();

    overlay_rect(PANEL_X + PADDING, y - 6, panel_width() - PADDING * 2, 1, COLOUR_TRACK);

    overlay_text(x, y, 1, COLOUR_TITLE, "Cheats");
    overlay_text(x + overlay_text_width("Cheats    ", 1), y, 1, COLOUR_DIM,
                 available ? "the game's own commands"
                           : "load a level; the game has nothing to send them to");

    for (index = 0; index < CHEAT_COUNT; ++index) {
        int bx;
        int by;
        int bw;
        int bh;
        unsigned face;
        unsigned ink;

        cheat_button_rect(index, &bx, &by, &bw, &bh);

        if (!available) {
            face = COLOUR_TRACK;
            ink  = COLOUR_DIM;
        } else if (cheat_is_toggle((cheat_id_t)index) && cheat_believed_state((cheat_id_t)index)) {
            face = COLOUR_ON;
            ink  = COLOUR_VALUE;
        } else {
            face = COLOUR_BUTTON;
            ink  = COLOUR_VALUE;
        }

        overlay_rect(bx, by, bw, bh, face);
        overlay_text(bx + 6, by + 3, 1, ink, cheat_label((cheat_id_t)index));

        /* The two the engine tracks a state for say which state we believe they are in. The word
         * is "believed" on purpose: the command is fire-and-forget and the game offers no way to
         * ask, so this is what was last sent, not what is true. */
        if (cheat_is_toggle((cheat_id_t)index)) {
            const char *state = cheat_believed_state((cheat_id_t)index) ? "on" : "off";
            overlay_text(bx + bw - 8 - overlay_text_width(state, 1), by + 3, 1,
                         available ? ink : COLOUR_DIM, state);
        }
    }
}

static void draw_tabs(void)
{
    int index;

    for (index = 0; index < TAB_COUNT; ++index) {
        int bx;
        int by;
        int bw;
        int bh;

        tab_rect(index, &bx, &by, &bw, &bh);
        overlay_rect(bx, by, bw, bh, (g_tab == index) ? COLOUR_FILL : COLOUR_BUTTON);
        overlay_text(bx + 5, by + 2, 1, (g_tab == index) ? COLOUR_VALUE : COLOUR_DIM,
                     g_tab_labels[index]);
    }

    {
        int bx;
        int by;
        int bw;
        int bh;

        messages_button_rect(&bx, &by, &bw, &bh);
        overlay_rect(bx, by, bw, bh, g_show_messages ? COLOUR_ON : COLOUR_BUTTON);
        overlay_text(bx + 5, by + 2, 1, COLOUR_VALUE,
                     g_show_messages ? " engine messages: on " : " engine messages: off ");
    }
}

/* The engine's own developer menu. Names and values are read from the engine every frame rather
 * than cached here, so a flag the game changes by itself is shown changing. */
static void draw_flags(void)
{
    char line[64];
    int  i;
    int  bx;
    int  by;
    int  bw;
    int  bh;

    if (!flags_available()) {
        overlay_text(PANEL_X + PADDING, content_top(), 1, COLOUR_DIM,
                     "the engine has not registered its debug flags yet");
        return;
    }

    layout_flags();

    for (i = 0; i < g_row_count; ++i) {
        const flag_row_t *row   = &g_rows[i];
        const char       *name  = flags_name(row->index);
        int32_t           value = 0;
        flag_kind_t       kind  = flags_kind(row->index);
        const char       *state;
        unsigned          face;

        if (!flags_value(row->index, &value)) {
            continue;
        }

        /* A typed number: the value in a box, with steppers, and no switch at all. Clicking it
         * starts typing rather than setting it to 1. */
        if (kind == FLAG_NUMBER) {
            bool editing = (g_edit_flag == row->index);

            if (inside(row->x, row->y, row->w, row->line)) {
                overlay_rect(row->x, row->y, row->w, row->line, COLOUR_TRACK);
            }

            sprintf(line, "%3d", row->index);
            overlay_text(row->x + 2, row->y + 1, 1, COLOUR_DIM, line);
            overlay_text(row->x + 2 + overlay_text_width("000 ", 1), row->y + 1, 1, COLOUR_LABEL,
                         name ? name : "(unnamed in this build)");

            flag_field_rect(row, &bx, &by, &bw, &bh);
            overlay_rect(bx, by, bw, bh, editing ? COLOUR_FILL : COLOUR_BUTTON);
            if (editing) {
                sprintf(line, "%s_", g_edit_text);
            } else {
                sprintf(line, "%ld", (long)value);
            }
            overlay_text(bx + bw - 6 - overlay_text_width(line, 1), by + 1, 1, COLOUR_VALUE, line);

            flag_step_rect(row, false, &bx, &by, &bw, &bh);
            overlay_rect(bx, by, bw, bh, COLOUR_BUTTON);
            overlay_text(bx + 6, by + 1, 1, COLOUR_VALUE, "-");

            flag_step_rect(row, true, &bx, &by, &bw, &bh);
            overlay_rect(bx, by, bw, bh, COLOUR_BUTTON);
            overlay_text(bx + 5, by + 1, 1, COLOUR_VALUE, "+");
            continue;
        }

        flag_switch_rect(row, &bx, &by, &bw, &bh);

        /* Green when it is doing something, exactly like the cheat buttons, so the page can be
         * read down the right hand edge without comparing numbers. */
        if (kind == FLAG_ACTION) {
            state = " run";
            face  = COLOUR_BUTTON;
        } else if (value != 0) {
            state = "  on";
            face  = COLOUR_ON;
        } else {
            state = " off";
            face  = COLOUR_BUTTON;
        }

        if (inside(row->x, row->y, row->w, row->line)) {
            overlay_rect(row->x, row->y, row->w, row->line, COLOUR_TRACK);
        }

        sprintf(line, "%3d", row->index);
        overlay_text(row->x + 2, row->y + 1, 1, COLOUR_DIM, line);
        overlay_text(row->x + 2 + overlay_text_width("000 ", 1), row->y + 1, 1,
                     (value != 0 && kind != FLAG_ACTION) ? COLOUR_VALUE : COLOUR_LABEL,
                     name ? name : "(unnamed in this build)");

        overlay_rect(bx, by, bw, bh, face);
        overlay_text(bx + 6, by + 1, 1, COLOUR_VALUE, state);

        /* The second line, only for the entries that hold a range. */
        if (row->has_picker) {
            sprintf(line, "%ld of 0-%d", (long)value, flags_cycle_range(row->index) - 1);
            overlay_text(row->x + 2 + overlay_text_width("000 ", 1), row->y + row_step() + 1, 1,
                         COLOUR_DIM, line);

            flag_step_rect(row, false, &bx, &by, &bw, &bh);
            overlay_rect(bx, by, bw, bh, COLOUR_BUTTON);
            overlay_text(bx + 6, by + 1, 1, COLOUR_VALUE, "-");

            flag_step_rect(row, true, &bx, &by, &bw, &bh);
            overlay_rect(bx, by, bw, bh, COLOUR_BUTTON);
            overlay_text(bx + 5, by + 1, 1, COLOUR_VALUE, "+");
        }
    }

    page_button_rect(false, &bx, &by, &bw, &bh);
    overlay_rect(bx, by, bw, bh, COLOUR_BUTTON);
    overlay_text(bx + 4, by + 3, 1, COLOUR_VALUE, " prev ");

    page_button_rect(true, &bx, &by, &bw, &bh);
    overlay_rect(bx, by, bw, bh, COLOUR_BUTTON);
    overlay_text(bx + 4, by + 3, 1, COLOUR_VALUE, " next ");

    sprintf(line, "page %d of %d", g_flag_page + 1, g_page_count);
    overlay_text(bx + bw + 16, by + 3, 1, COLOUR_DIM, line);

    overlay_text(bx + bw + 16 + overlay_text_width("page 0 of 0        ", 1), by + 3, 1,
                 COLOUR_DIM, overlay_overflowed()
                             ? "overlay batch full, some rows are missing"
                             : (g_edit_flag >= 0
                                ? "type a number, Enter to set it, Escape also opens the game's pause menu"
                                : "click a row to press it, as the game's own menu would"));
}

/* ------------------------------------------------------------------------- engine messages
 *
 * Its own box, at the bottom of the screen, drawn whenever "Engine Debug Messages" is not zero,
* with the menu open or closed, because a log you can only see while a menu covers the game is
 * not much of a log.
 */
#define MESSAGE_PANEL_W   900
#define MESSAGE_LINES_MAX 14
#define MESSAGE_SCALE     1

static int message_step(void)
{
    return overlay_line_height() * MESSAGE_SCALE + 6;
}

static int message_lines_shown(void)
{
    int step = message_step();
    int room = (g_view_h / 3) / (step > 0 ? step : 1);

    if (room < 4)                 { room = 4; }
    if (room > MESSAGE_LINES_MAX) { room = MESSAGE_LINES_MAX; }
    return room;
}

static void message_panel_rect(int *x, int *y, int *w, int *h)
{
    int step = message_step();

    *w = MESSAGE_PANEL_W;
    if (*w > g_view_w - PANEL_X * 2) { *w = g_view_w - PANEL_X * 2; }
    if (*w < 320)                    { *w = 320; }

    *h = PADDING + step + message_lines_shown() * step + PADDING / 2;
    *x = PANEL_X;
    *y = g_view_h - *h - PANEL_Y;
    if (*y < PANEL_Y) { *y = PANEL_Y; }
}

static void message_stats_rect(int *bx, int *by, int *bw, int *bh)
{
    int x;
    int y;
    int w;
    int h;

    message_panel_rect(&x, &y, &w, &h);
    *bw = overlay_text_width(" stats: off ", 1) + 8;
    *bh = overlay_line_height() + 4;
    *bx = x + w - PADDING - (overlay_text_width(" clear ", 1) + 8) - 8 - *bw;
    *by = y + PADDING - 2;
}

static void message_clear_rect(int *bx, int *by, int *bw, int *bh)
{
    int x;
    int y;
    int w;
    int h;

    message_panel_rect(&x, &y, &w, &h);
    *bw = overlay_text_width(" clear ", 1) + 8;
    *bh = overlay_line_height() + 4;
    *bx = x + w - PADDING - *bw;
    *by = y + PADDING - 2;
}

static void draw_messages(void)
{
    char     line[96];
    int      x;
    int      y;
    int      w;
    int      h;
    int      shown = message_lines_shown();
    int      step  = message_step();
    int      row   = 0;
    unsigned live  = messages_live_count();
    unsigned i;

    message_panel_rect(&x, &y, &w, &h);

    overlay_rect(x, y, w, h, COLOUR_PANEL);
    overlay_frame(x, y, w, h, 1, COLOUR_EDGE);

    sprintf(line, "engine messages   %u seen, %u kept", messages_total(), messages_kept());
    overlay_text(x + PADDING, y + PADDING - 2, 1, COLOUR_TITLE, line);

    if (!messages_installed()) {
        overlay_text(x + PADDING, y + PADDING + step, MESSAGE_SCALE, COLOUR_DIM,
                     "not hooked yet");
        return;
    }

    /* The live rows first: the statistics, each replaced in place as it arrives, and gone within
     * a second and a half of the flag that produces it being switched off. */
    if (g_show_stats) {
        for (i = 0; i < live && row < shown; ++i) {
            const char *text = messages_live(i);

            if (text == NULL) {
                break;
            }
            if (!messages_text_enabled(text)) {
                continue;
            }
            overlay_text(x + PADDING, y + PADDING + step + row * step, MESSAGE_SCALE,
                         COLOUR_VALUE, text);
            ++row;
        }

        if (row > 0 && row < shown) {
            overlay_rect(x + PADDING, y + PADDING + step + row * step + step / 2 - 1,
                         w - PADDING * 2, 1, COLOUR_TRACK);
            ++row;
        }
    }

    /* Then the events, newest at the bottom of what is left. */
    {
        const char *picked[MESSAGE_LINES_MAX];
        int         count = 0;
        int         space = shown - row;
        unsigned    age;

        for (age = 0; count < space && age < MESSAGE_LINES; ++age) {
            const char *text = messages_line_ex(age, NULL);

            if (text == NULL) {
                break;
            }
            if (text[0] == '\0' || !messages_text_enabled(text)) {
                continue;
            }
            picked[count++] = text;
        }

        for (i = 0; (int)i < count; ++i) {
            overlay_text(x + PADDING, y + PADDING + step + (row + count - 1 - (int)i) * step,
                         MESSAGE_SCALE, ((int)i == 0) ? COLOUR_VALUE : COLOUR_LABEL, picked[i]);
        }
    }

    /* Only when the menu is open, because that is the only time there is a pointer to click
     * them with. */
    if (g_visible) {
        int bx;
        int by;
        int bw;
        int bh;

        message_stats_rect(&bx, &by, &bw, &bh);
        overlay_rect(bx, by, bw, bh, g_show_stats ? COLOUR_ON : COLOUR_BUTTON);
        overlay_text(bx + 4, by + 1, 1, COLOUR_VALUE, g_show_stats ? " stats: on " : " stats: off ");

        message_clear_rect(&bx, &by, &bw, &bh);
        overlay_rect(bx, by, bw, bh, COLOUR_BUTTON);
        overlay_text(bx + 4, by + 1, 1, COLOUR_VALUE, " clear ");
    }
}

/* OUR switch, NOT the engine's flag 0. Flag 0 turns on the engine's own message display, which
 * on this build corrupts the lighting and then takes the game down. Capturing does not need it.
 * Switching the box on also puts flag 0 back to 0, so a stray click cannot bring it back. */
static bool messages_wanted(void)
{
    return g_show_messages && messages_enabled();
}

static void set_messages(bool on)
{
    int32_t engine_display = 0;

    g_show_messages = on;

    if (on && flags_value(0, &engine_display) && engine_display != 0) {
        flags_set(0, 0);
        log_warning("the engine's own message display (flag 0) was on and has been switched off. "
                    "It is what makes the lighting go wrong and then crashes; this box does not "
                    "need it.");
    }
}

/* --------------------------------------------------------------------------- the messages page
 *
 * One row per channel, and the channels are whatever the engine has actually printed since the
 * game started. Nothing here is a fixed list: play for five minutes in a different level and the
 * page grows.
 */
static void channel_row_rect(int slot, int *bx, int *by, int *bw, int *bh)
{
    int step  = row_step();
    int width = (panel_width() - PADDING * 2 - FLAG_GAP) / 2;

    *bx = PANEL_X + PADDING + (slot / flag_rows()) * (width + FLAG_GAP);
    *by = content_top() + (slot % flag_rows()) * step;
    *bw = width;
    *bh = overlay_line_height() + 2;
}

static void channel_all_rect(bool on, int *bx, int *by, int *bw, int *bh)
{
    *bw = overlay_text_width(" none ", 1) + 8;
    *bh = overlay_line_height() + 6;
    *bx = PANEL_X + PADDING + (on ? 0 : (*bw + 8));
    *by = content_top() + flag_rows() * row_step() + 6;
}

static void draw_channels(void)
{
    char     line[64];
    unsigned count = messages_channel_count();
    unsigned i;
    int      bx;
    int      by;
    int      bw;
    int      bh;

    if (count == 0) {
        overlay_text(PANEL_X + PADDING, content_top(), 1, COLOUR_DIM,
                     "nothing has been printed yet");
        return;
    }

    for (i = 0; i < count && (int)i < flag_rows() * 2; ++i) {
        bool        on   = messages_channel_enabled(i);
        const char *name = messages_channel_name(i);

        channel_row_rect((int)i, &bx, &by, &bw, &bh);

        if (inside(bx, by, bw, bh)) {
            overlay_rect(bx, by, bw, bh, COLOUR_TRACK);
        }

        overlay_text(bx + 2, by + 1, 1, on ? COLOUR_VALUE : COLOUR_DIM,
                     (name != NULL) ? name : "?");

        sprintf(line, "%u", messages_channel_hits(i));
        overlay_text(bx + bw - 56 - overlay_text_width(line, 1), by + 1, 1, COLOUR_DIM, line);

        overlay_rect(bx + bw - 52, by, 50, bh, on ? COLOUR_ON : COLOUR_BUTTON);
        overlay_text(bx + bw - 46, by + 1, 1, COLOUR_VALUE, on ? " on" : "off");
    }

    channel_all_rect(true, &bx, &by, &bw, &bh);
    overlay_rect(bx, by, bw, bh, COLOUR_BUTTON);
    overlay_text(bx + 4, by + 3, 1, COLOUR_VALUE, " all  ");

    channel_all_rect(false, &bx, &by, &bw, &bh);
    overlay_rect(bx, by, bw, bh, COLOUR_BUTTON);
    overlay_text(bx + 4, by + 3, 1, COLOUR_VALUE, " none ");

    sprintf(line, "%u channels, switched off means not recorded at all, not merely hidden", count);
    overlay_text(bx + bw + 16, by + 3, 1, COLOUR_DIM, line);
}

static void draw_menu(const camera_view_t *view, bool have_camera)
{

    char  line[128];
    int   x     = PANEL_X + PADDING;
    int   y     = PANEL_Y + PADDING;
    int   step  = overlay_line_height() + 6;
    int   track_x;
    int   track_y;
    int   track_w;
    int   knob;
    float fraction;

    overlay_rect(PANEL_X, PANEL_Y, panel_width(), panel_height(), COLOUR_PANEL);
    overlay_frame(PANEL_X, PANEL_Y, panel_width(), panel_height(), 1, COLOUR_EDGE);

    overlay_text(x, y, 1, COLOUR_TITLE, "Fellowship Reborn  -  dev menu");
    overlay_text(PANEL_X + panel_width() - PADDING - overlay_text_width("` to close", 1), y, 1,
                 COLOUR_DIM, "` to close");

    draw_tabs();

    if (g_tab == TAB_FLAGS) {
        draw_flags();
        return;
    }

    if (g_tab == TAB_MESSAGES) {
        draw_channels();
        return;
    }

    if (g_tab == TAB_FIXES) {
        draw_player();
        draw_frame_rate();
        return;
    }

    y = content_top();

    /* ---- the field of view row */
    overlay_text(x, y, 1, COLOUR_LABEL, "Vertical FOV");

    track_x = x + 150;
    track_y = y + overlay_line_height() / 2 - 3;
    track_w = panel_width() - PADDING * 2 - 150 - 90;

    fraction = (clampf(g_fov_degrees, FOV_MIN, FOV_MAX) - FOV_MIN) / (FOV_MAX - FOV_MIN);
    knob     = track_x + (int)(fraction * (float)(track_w - 10));

    overlay_rect(track_x, track_y, track_w, 6, COLOUR_TRACK);
    overlay_rect(track_x, track_y, knob - track_x + 10, 6, g_auto_fov ? COLOUR_TRACK : COLOUR_FILL);
    overlay_rect(knob, track_y - 7, 10, 20, g_auto_fov ? COLOUR_DIM : COLOUR_KNOB);

    sprintf(line, "%.1f", (double)g_fov_degrees);
    overlay_text(PANEL_X + panel_width() - PADDING - overlay_text_width("000.0", 1), y, 1,
                 g_auto_fov ? COLOUR_DIM : COLOUR_VALUE, line);
    y += step;

    /* ---- the auto button */
    {
        int button_w = overlay_text_width(" automatic ", 1) + 8;
        overlay_rect(x, y, button_w, overlay_line_height() + 6,
                     g_auto_fov ? COLOUR_ON : COLOUR_BUTTON);
        overlay_text(x + 4, y + 3, 1, COLOUR_VALUE, " automatic ");
        overlay_text(x + button_w + 12, y + 3, 1, COLOUR_DIM,
                     g_auto_fov ? "field_of_view is choosing" : "the slider is choosing");
    }
    y += step + 8;

    /* ---- what the camera actually reads back, so the slider can be believed */
    if (have_camera) {
        double horizontal = 2.0 * to_degrees(atan((double)view->half_w / (double)view->focal));
        double vertical   = 2.0 * to_degrees(atan((double)view->half_h / (double)view->focal));
        sprintf(line, "camera  %dx%d   focal %.2f", (int)view->viewport_width,
                (int)view->viewport_height, (double)view->focal);
        overlay_text(x, y, 1, COLOUR_DIM, line);
        y += step;
        sprintf(line, "live    %.1f vertical    %.1f horizontal", vertical, horizontal);
        overlay_text(x, y, 1, COLOUR_DIM, line);
        y += step;
        sprintf(line, "mouse   %s%s", g_source,
                g_intercepting ? "   game muted" : "   game still reading");
        overlay_text(x, y, 1, COLOUR_DIM, line);
    } else {
        overlay_text(x, y, 1, COLOUR_DIM, "no camera yet, load a save");
        y += step;
        overlay_text(x, y, 1, COLOUR_DIM, "the menus have none");
    }
    y += step;

    draw_cheats(x, have_camera);

    y = cheats_top() + row_step() + cheat_rows() * (row_step() + 4);

    if (overlay_overflowed()) {
        overlay_text(x, y, 1, COLOUR_EDGE, "overlay batch full, some of this is missing");
    }

}

/* Released inside the box, which is what a button is. Kept in one place so every clickable thing
 * on both pages agrees about what a click is. */
static bool clicked(int x, int y, int w, int h)
{
    return g_mouse_was_down && !g_mouse_down && inside(x, y, w, h);
}

/* Edge-detected, because this runs once a frame and a held key would otherwise repeat sixty
 * times a second. One entry per key we care about; nothing else on the keyboard is looked at. */
static bool key_pressed(int vk)
{
    static bool previous[256];
    bool        down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool        went_down;

    if (vk < 0 || vk > 255) {
        return false;
    }
    went_down     = down && !previous[vk];
    previous[vk]  = down;
    return went_down;
}

static void edit_begin(int index)
{
    int32_t value = 0;

    flags_value(index, &value);
    _snprintf(g_edit_text, sizeof(g_edit_text), "%ld", (long)value);
    g_edit_text[sizeof(g_edit_text) - 1] = '\0';
    g_edit_length = (int)strlen(g_edit_text);
    g_edit_flag   = index;
}

static void edit_commit(bool keep)
{
    if (g_edit_flag < 0) {
        return;
    }
    if (keep && g_edit_length > 0) {
        flags_set(g_edit_flag, (int32_t)atoi(g_edit_text));
    }
    g_edit_flag   = -1;
    g_edit_length = 0;
    g_edit_text[0] = '\0';
}

static void edit_keys(void)
{
    int digit;

    if (g_edit_flag < 0) {
        return;
    }

    for (digit = 0; digit <= 9; ++digit) {
        if (key_pressed('0' + digit) || key_pressed(VK_NUMPAD0 + digit)) {
            if (g_edit_length < (int)sizeof(g_edit_text) - 1) {
                g_edit_text[g_edit_length++] = (char)('0' + digit);
                g_edit_text[g_edit_length]   = '\0';
            }
        }
    }

    /* A minus only means anything as the first character, which is also the only place the
     * engine would accept one. */
    if ((key_pressed(VK_OEM_MINUS) || key_pressed(VK_SUBTRACT)) && g_edit_length == 0) {
        g_edit_text[g_edit_length++] = '-';
        g_edit_text[g_edit_length]   = '\0';
    }

    if (key_pressed(VK_BACK) && g_edit_length > 0) {
        g_edit_text[--g_edit_length] = '\0';
    }
    if (key_pressed(VK_RETURN)) {
        edit_commit(true);
    }
    if (key_pressed(VK_ESCAPE)) {
        edit_commit(false);
    }
}

static void handle_flags_input(void)
{
    int bx;
    int by;
    int bw;
    int bh;
    int i;

    /* Laid out here as well as in the drawing, rather than reusing last frame's table: input runs
     * before the draw, and a page that has just changed would otherwise be clicked at the old
     * page's rectangles for one frame. */
    layout_flags();

    edit_keys();

    page_button_rect(false, &bx, &by, &bw, &bh);
    if (clicked(bx, by, bw, bh)) {
        if (g_flag_page > 0) { --g_flag_page; }
        return;
    }
    page_button_rect(true, &bx, &by, &bw, &bh);
    if (clicked(bx, by, bw, bh)) {
        if (g_flag_page + 1 < g_page_count) { ++g_flag_page; }
        return;
    }

    for (i = 0; i < g_row_count; ++i) {
        const flag_row_t *row   = &g_rows[i];
        int32_t           value = 0;

        if (!flags_value(row->index, &value)) {
            continue;
        }

        /* A typed number: the field takes the click, the steppers nudge, and the rest of the row
         * does nothing, pressing it would run the dispatcher's default case and flatten the
         * coordinate to 0 or 1. */
        if (flag_is_number(row->index)) {
            flag_field_rect(row, &bx, &by, &bw, &bh);
            if (clicked(bx, by, bw, bh)) {
                if (g_edit_flag == row->index) {
                    edit_commit(true);
                } else {
                    edit_commit(true);
                    edit_begin(row->index);
                }
                return;
            }

            flag_step_rect(row, false, &bx, &by, &bw, &bh);
            if (clicked(bx, by, bw, bh)) { flags_set(row->index, value - 1); return; }

            flag_step_rect(row, true, &bx, &by, &bw, &bh);
            if (clicked(bx, by, bw, bh)) { flags_set(row->index, value + 1); return; }

            continue;
        }

        /* The steppers first: they sit inside the row's own area and mean the raw number, which
         * is a different thing from pressing the entry. */
        if (row->has_picker) {
            int range = flags_cycle_range(row->index);

            flag_step_rect(row, false, &bx, &by, &bw, &bh);
            if (clicked(bx, by, bw, bh)) {
                flags_set(row->index, (value > 0) ? value - 1 : range - 1);
                return;
            }
            flag_step_rect(row, true, &bx, &by, &bw, &bh);
            if (clicked(bx, by, bw, bh)) {
                flags_set(row->index, (value + 1 < range) ? value + 1 : 0);
                return;
            }
        }

        /* Anywhere else on the row's first line presses the entry, and the engine's dispatcher
         * decides what that means: a switch flips, a range steps, an action fires, and the
         * side effects that make twenty-four of these mean anything are run. */
        if (clicked(row->x, row->y, row->w, row->line)) {
            flags_activate(row->index);
            return;
        }
    }
}

static void handle_input(bool have_camera)
{
    int track_x = PANEL_X + PADDING + 150;
    int track_y = content_top() + overlay_line_height() / 2 - 3;
    int track_w = panel_width() - PADDING * 2 - 150 - 90;
    int button_y = content_top() + row_step();
    int button_w = overlay_text_width(" automatic ", 1) + 8;
    int index;

    {
        int bx;
        int by;
        int bw;
        int bh;

        messages_button_rect(&bx, &by, &bw, &bh);
        if (clicked(bx, by, bw, bh)) {
            set_messages(!g_show_messages);
            return;
        }
    }

    /* the tab strip */
    for (index = 0; index < TAB_COUNT; ++index) {
        int bx;
        int by;
        int bw;
        int bh;

        tab_rect(index, &bx, &by, &bw, &bh);
        if (clicked(bx, by, bw, bh)) {
            edit_commit(false);
            g_tab = index;
            return;
        }
    }

    /* The message box's clear button. It is not on either page; the box is its own panel, so
     * it is checked before the page split. */
    if (messages_wanted()) {
        int bx;
        int by;
        int bw;
        int bh;

        message_stats_rect(&bx, &by, &bw, &bh);
        if (clicked(bx, by, bw, bh)) {
            g_show_stats = !g_show_stats;
            return;
        }

        message_clear_rect(&bx, &by, &bw, &bh);
        if (clicked(bx, by, bw, bh)) {
            messages_clear();
            return;
        }
    }

    if (g_tab == TAB_FLAGS) {
        handle_flags_input();
        return;
    }

    if (g_tab == TAB_FIXES) {
        /* Frame rate first. It owns the right hand end of two rows the player half does not
         * reach, and checking it first means a click there can never be swallowed by a slider
         * grab belonging to the rows above. */
        handle_frame_rate_input();
        handle_player_input();
        return;
    }

    if (g_tab == TAB_MESSAGES) {
        unsigned count = messages_channel_count();
        unsigned i;
        int      bx;
        int      by;
        int      bw;
        int      bh;

        channel_all_rect(true, &bx, &by, &bw, &bh);
        if (clicked(bx, by, bw, bh)) { messages_channels_all(true);  return; }
        channel_all_rect(false, &bx, &by, &bw, &bh);
        if (clicked(bx, by, bw, bh)) { messages_channels_all(false); return; }

        for (i = 0; i < count && (int)i < flag_rows() * 2; ++i) {
            channel_row_rect((int)i, &bx, &by, &bw, &bh);
            if (clicked(bx, by, bw, bh)) {
                messages_channel_set(i, !messages_channel_enabled(i));
                return;
            }
        }
        return;
    }

    /* the automatic button, on release inside it */
    if (g_mouse_was_down && !g_mouse_down
        && inside(PANEL_X + PADDING, button_y, button_w, overlay_line_height() + 6)) {
        g_auto_fov = !g_auto_fov;
        channel_publish_field_of_view(g_channel, g_auto_fov ? 0.0f : g_fov_degrees);
        log_info(g_auto_fov ? "field of view released back to field_of_view"
                            : "field of view taken over by the slider");
    }

    /* the slider: grab anywhere on the track, then keep the grab until the button comes up,
     * so a fast drag that leaves the track vertically does not drop the knob */
    if (g_mouse_down && !g_mouse_was_down
        && inside(track_x - 6, track_y - 10, track_w + 12, 26)) {
        g_dragging = true;
    }
    if (!g_mouse_down) {
        g_dragging = false;
    }

    if (g_dragging && track_w > 12) {
        float fraction = (float)(g_mouse_x - track_x) / (float)(track_w - 10);
        g_fov_degrees  = clampf(FOV_MIN + fraction * (FOV_MAX - FOV_MIN), FOV_MIN, FOV_MAX);
        g_auto_fov     = false;
        channel_publish_field_of_view(g_channel, g_fov_degrees);
    }

    /* ---- the cheat buttons, on release inside one, exactly like the automatic button above.
     *
     * This runs inside the EndScene hook, which is the game's own thread inside its own frame,
     * and that is the only reason calling into the engine from here is reasonable at all. A
     * button pressed on a key thread would be calling engine code from underneath the engine. */
    if (g_mouse_was_down && !g_mouse_down && have_camera) {
        int cheat;   /* not `index`: the tab loop above already has one, and shadowing it is C4456 */

        for (cheat = 0; cheat < CHEAT_COUNT; ++cheat) {
            int bx;
            int by;
            int bw;
            int bh;

            cheat_button_rect(cheat, &bx, &by, &bw, &bh);
            if (inside(bx, by, bw, bh)) {
                cheat_send((cheat_id_t)cheat);
                break;
            }
        }
    }

    /* Arrow keys do the same job. Not a fallback in spirit; a slider is for finding the value
     * and a key is for landing on it, but it is also what still works if the cursor turns out
     * to be somewhere this plugin cannot see it. */
    if (GetAsyncKeyState(VK_LEFT) & 0x8000) {
        g_fov_degrees = clampf(g_fov_degrees - 0.25f, FOV_MIN, FOV_MAX);
        g_auto_fov    = false;
        channel_publish_field_of_view(g_channel, g_fov_degrees);
    }
    if (GetAsyncKeyState(VK_RIGHT) & 0x8000) {
        g_fov_degrees = clampf(g_fov_degrees + 0.25f, FOV_MIN, FOV_MAX);
        g_auto_fov    = false;
        channel_publish_field_of_view(g_channel, g_fov_degrees);
    }
}

/* ------------------------------------------------------------------------------- the hook */

static HRESULT STDMETHODCALLTYPE hooked_end_scene(void *device)
{
    if (device != NULL) {
        camera_view_t   view;
        bool            have_camera = camera_read(&view);
        bool            show_messages;
        d3d8_viewport_t viewport;

        show_messages = messages_wanted();

        /* Before the early return, not after: a chosen size has to hold whether or not the menu
         * is open, and the engine rewrites the matrix from animation every frame regardless of
         * what is on screen. This is also the game's own thread, which is the only place writing
         * into a live game object is reasonable at all. */
        player_hold_size();

        if (!g_visible && !show_messages) {
            return g_original_end_scene(device);
        }

        /* The device's own answer, so the pointer is clamped to the surface actually being drawn
         * into rather than to whatever the camera last reported. */
        memset(&viewport, 0, sizeof(viewport));
        if (SUCCEEDED(((d3d8_get_viewport_t)g_vtable[D3D8_GETVIEWPORT])(device, &viewport))
            && viewport.width > 0 && viewport.height > 0) {
            g_view_w = (int)viewport.width;
            g_view_h = (int)viewport.height;
        }

        overlay_begin();

        if (show_messages) {
            draw_messages();
        }

        if (g_visible) {
            /* The slider starts wherever the game currently is, so opening the menu never moves
             * the picture. Only a drag does. */
            if (g_fov_degrees <= 0.0f && have_camera) {
                g_fov_degrees = (float)(2.0 * to_degrees(atan((double)view.half_h
                                                              / (double)view.focal)));
                g_fov_degrees = clampf(g_fov_degrees, FOV_MIN, FOV_MAX);
            }

            if (g_mouse_x == 0 && g_mouse_y == 0) {
                g_mouse_x = PANEL_X + PANEL_W / 2;
                g_mouse_y = PANEL_Y + panel_height() / 2;
            }
            read_mouse();
            handle_input(have_camera);
            draw_menu(&view, have_camera);
            draw_pointer();
        }

        overlay_flush(device);
    }

    return g_original_end_scene(device);
}

static bool install_hook(void)
{
    void  *device;
    DWORD  protection = 0;

    if (g_hook_installed) {
        return true;
    }
    if (g_hook_failed) {
        return false;
    }

    device = find_device();
    if (device == NULL) {
        log_error("the Direct3D device could not be verified; the menu will not open. "
                  "Nothing has been changed.");
        g_hook_failed = true;
        return false;
    }
    if (!overlay_prepare(g_font_height)) {
        g_hook_failed = true;
        return false;
    }

    g_original_end_scene = (d3d8_end_scene_t)g_vtable[D3D8_ENDSCENE];

    if (!VirtualProtect(&g_vtable[D3D8_ENDSCENE], sizeof(void *), PAGE_READWRITE, &protection)) {
        log_error("the device vtable could not be made writable");
        g_hook_failed = true;
        return false;
    }
    /* One aligned pointer-sized store, which is atomic on x86, so a render thread calling
     * EndScene at this instant gets either the old function or ours, never half of each. */
    g_vtable[D3D8_ENDSCENE] = (void *)hooked_end_scene;
    VirtualProtect(&g_vtable[D3D8_ENDSCENE], sizeof(void *), protection, &protection);

    g_hook_installed = true;
    log_info("device %08X, EndScene %08X -> %08X", (unsigned)(uintptr_t)device,
             (unsigned)(uintptr_t)g_original_end_scene, (unsigned)(uintptr_t)hooked_end_scene);
    return true;
}

/* ------------------------------------------------------------------------------- the thread */

OF_NORETURN_THREAD_BEGIN
static DWORD WINAPI poll_thread(LPVOID parameter)
{
    bool previous = false;
    HWND raw_window;
    MSG  message;

    (void)parameter;

    /* Created on THIS thread, because a window's messages are delivered to the thread that made
     * it. That is also why the loop below pumps: with nobody calling PeekMessage, WM_INPUT would
     * queue up and the pointer would never move. */
    raw_window = create_raw_window();
    if (raw_window == NULL) {
        log_warning("raw mouse input is unavailable, the arrow keys still work, the pointer "
                    "will not");
    }

    for (;;) {
        bool pressed = (GetAsyncKeyState(g_toggle_key) & 0x8000) != 0;

        /* The message capture goes in as soon as the engine has an object to hook, which is
         * during start-up and long before anybody presses anything. Nearly everything this engine
         * prints, it prints while loading: hooking at the moment the box is opened meant the
         * interesting lines had already gone by, and the only way to see any was to quit to the
         * menu and load again. Now the ring is filling from the first frame and opening the box
         * shows what has already happened. */
        if (messages_enabled() && !messages_installed()) {
            messages_install();
        }

        while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }

        if (pressed && !previous) {
            if (!g_visible) {
                /* Installed on the first press, never before it: an install that never opens
                 * the menu is one where this plugin has touched nothing at all. */
                if (install_hook()) {
                    /* Opened here rather than at startup so that, when we do get the mouse
                     * exclusively, the game only loses it for as long as the menu is up. */
                    if (!open_mouse()) {
                        log_warning("no mouse could be opened, the arrow keys still work");
                    }
                    g_visible = true;
                }
            } else {
                g_visible = false;
                if (g_mouse_device != NULL) {
                    /* Handed straight back. Holding an exclusive mouse after the menu has closed
                     * would be indistinguishable, from the player's side, from a broken game. */
                    ((di8_release_t)(*(void ***)g_mouse_device)[DI8_DEV_RELEASE])(g_mouse_device);
                    g_mouse_device = NULL;
                }
            }
        }
        previous = pressed;
        Sleep(8);
    }

    /* Not reached; the thread lives as long as the process. */
    return 0;
}
OF_NORETURN_THREAD_END

void dev_menu_install(void)
{
    HANDLE  thread;
    int32_t configured;

    log_init(PLUGIN_SECTION, false);

    if (!ini_read_bool(PLUGIN_SECTION, "Enabled", true)) {
        log_info("Enabled=0, doing nothing");
        return;
    }
    if (!host_image_resolve()) {
        log_error("the host image could not be resolved; refusing to touch anything");
        return;
    }

    configured = ini_read_int(PLUGIN_SECTION, "KeyCode", DEFAULT_TOGGLE_KEY);
    if (configured > 0 && configured < 256) {
        g_toggle_key = configured;
    }
    configured = ini_read_int(PLUGIN_SECTION, "FontHeight", 16);
    if (configured >= 10 && configured <= 32) {
        g_font_height = configured;
    }

    {
        float sensitivity = ini_read_float(PLUGIN_SECTION, "PointerSpeed", 1.0f);
        if (sensitivity >= 0.1f && sensitivity <= 10.0f) {
            g_sensitivity = sensitivity;
        }
    }

    g_take_mouse = ini_read_bool(PLUGIN_SECTION, "TakeMouse", true);
    if (g_take_mouse) {
        /* This one IS installed at startup, and has to be: the game calls DirectInput8Create
         * once, early, and an import slot rewritten afterwards is a slot nobody will read again.
         * It forwards until the menu opens, so the cost of it existing is a jump. */
        install_input_intercept();
    }

    /* Capture is separate from the box: the box is a view of a ring that has been filling since
     * start-up. Off only if somebody asks, because two vtable entries that record a string are
     * not something anyone needs to opt out of by default. */
    messages_set_enabled(ini_read_bool(PLUGIN_SECTION, "CaptureMessages", true));

    /* Off by default: it is a great deal of text. On when the screen is the thing that is broken,
     * because then the log is the only place the engine's own account of itself can go. */
    if (ini_read_bool(PLUGIN_SECTION, "LogMessages", false)) {
        messages_set_logging(true);
        log_info("LogMessages=1: everything the engine prints is going into this file as well");
    }

    g_channel = channel_open();
    if (g_channel == NULL) {
        log_warning("the shared channel could not be opened; the sliders will have nothing to "
                    "drive, though the menu will still open and report");
    }

    /* Starts the frame rate slider where the ini already has it, so opening the menu shows what
     * the game is doing rather than a default that disagrees with it. Nothing is published until
     * the slider is actually moved. */
    timing_init(g_channel);

    thread = CreateThread(NULL, 0, poll_thread, NULL, 0, NULL);
    if (thread == NULL) {
        log_error("could not start the hotkey thread");
        return;
    }
    CloseHandle(thread);

    log_info("installed, key code %d (the key under Escape). Nothing is hooked until you press "
             "it.", g_toggle_key);
}

#include "dev_menu.h"
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
#include <string.h>

#define PLUGIN_SECTION "dev_menu"

/* The key immediately below Escape. VK_OEM_3 is that key on both US and UK layouts - backquote
 * there, and whatever sits in that position elsewhere. The game's own cheats are F5 to F12 and
 * fog_toggle took F1, so this position is free. */
#define DEFAULT_TOGGLE_KEY VK_OEM_3

#define PANEL_X       24
#define PANEL_Y       24
#define PANEL_W       520
#define PANEL_H       224
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

static bool  g_mouse_down;
static bool  g_mouse_was_down;
static int   g_mouse_x;
static int   g_mouse_y;
static bool  g_dragging;

/* RAW INPUT, NOT THE SYSTEM CURSOR
 *
 * The first version read GetCursorPos and the menu drew perfectly and could not be clicked. That
 * is the documented behaviour of a DirectInput device acquired in EXCLUSIVE mode, which is how
 * this game takes the mouse: the system cursor stops moving and mouse movement stops generating
 * window messages. GetCursorPos then returns the same frozen point forever, which is exactly
 * what a menu that draws but cannot be interacted with looks like.
 *
 * Raw input sits underneath all of that. WM_INPUT delivers the device's own relative movement
 * whatever DirectInput has done with the cursor, so this plugin keeps its OWN pointer position,
 * accumulated from those deltas and drawn by the overlay - the system cursor is invisible and
 * frozen, and is no longer anything to do with us.
 *
 * The window that receives it is a message-only window of our own on our own thread. Subclassing
 * the game's window would also have worked and would have put our code in its message loop; this
 * way nothing of the game's is touched. */
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

/* DirectInput wants a window to hang a cooperative level on. Not for coordinates - there are no
 * coordinates any more, only movement - just for the association. */
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

/* ------------------------------------------------- silencing the game's own mouse reads
 *
 * Asking DirectInput for the mouse EXCLUSIVELY is the polite way to stop the game seeing it, and
 * on this game it does not work: the game got there first and holds it exclusively itself, so our
 * request is refused and we fall back to sharing. Sharing means the menu works and the world
 * swings around underneath it, which is worse than either extreme.
 *
 * So the game's own reads are silenced at the source. The executable imports exactly ONE symbol
 * from DINPUT8.dll - DirectInput8Create - and a plugin installed at the entry point runs long
 * before the game calls it. Rewriting that import slot gives us the interface it is handed, the
 * interface gives us the CreateDevice call, and CreateDevice gives us the mouse device itself.
 * From there, one vtable entry decides whether the game hears anything.
 *
 * Every hook forwards. While the menu is closed the game reads its mouse exactly as it always
 * did; while it is open, the two functions that return mouse data return nothing. And the check
 * is on the DEVICE, not the vtable, because DirectInput gives every device of a class the same
 * vtable - silencing the vtable outright would take the keyboard with it, and our own mouse.
 */

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
        log_warning("DirectInput8Create is not imported where expected - the game will keep "
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
     * seeing the movement while the menu is open - which is the point of a menu. If it is, we
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
             g_mouse_exclusive ? "exclusively - the game will not see it while the menu is open"
                               : "shared - the game still sees the same movement");
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

#define RAW_WINDOW_CLASS "OpenFellowshipDevMenuInput"

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

/* Only degrees are needed here. This plugin never turns an angle into a focal length - that is
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

static void draw_menu(void *device, const camera_view_t *view, bool have_camera)
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

    overlay_begin();
    overlay_rect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, COLOUR_PANEL);
    overlay_frame(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 1, COLOUR_EDGE);

    overlay_text(x, y, 1, COLOUR_TITLE, "OpenFellowship  -  dev menu");
    overlay_text(PANEL_X + PANEL_W - PADDING - overlay_text_width("` to close", 1), y, 1,
                 COLOUR_DIM, "` to close");
    y += step + 6;

    /* ---- the field of view row */
    overlay_text(x, y, 1, COLOUR_LABEL, "Vertical FOV");

    track_x = x + 150;
    track_y = y + overlay_line_height() / 2 - 3;
    track_w = PANEL_W - PADDING * 2 - 150 - 70;

    fraction = (clampf(g_fov_degrees, FOV_MIN, FOV_MAX) - FOV_MIN) / (FOV_MAX - FOV_MIN);
    knob     = track_x + (int)(fraction * (float)(track_w - 10));

    overlay_rect(track_x, track_y, track_w, 6, COLOUR_TRACK);
    overlay_rect(track_x, track_y, knob - track_x + 10, 6, g_auto_fov ? COLOUR_TRACK : COLOUR_FILL);
    overlay_rect(knob, track_y - 7, 10, 20, g_auto_fov ? COLOUR_DIM : COLOUR_KNOB);

    sprintf(line, "%.1f", (double)g_fov_degrees);
    overlay_text(PANEL_X + PANEL_W - PADDING - overlay_text_width("000.0", 1), y, 1,
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
        overlay_text(x, y, 1, COLOUR_DIM, "no camera yet - load a save");
        y += step;
        overlay_text(x, y, 1, COLOUR_DIM, "the menus have none");
    }
    y += step;

    if (overlay_overflowed()) {
        overlay_text(x, y, 1, COLOUR_EDGE, "overlay batch full - some of this is missing");
    }

    draw_pointer();
    overlay_flush(device);
}

static void handle_input(void)
{
    int track_x = PANEL_X + PADDING + 150;
    int track_y = PANEL_Y + PADDING + overlay_line_height() + 6
                  + overlay_line_height() / 2 - 3;
    int track_w = PANEL_W - PADDING * 2 - 150 - 70;
    int button_y = PANEL_Y + PADDING + (overlay_line_height() + 6) * 2 + 6;
    int button_w = overlay_text_width(" automatic ", 1) + 8;

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

    /* Arrow keys do the same job. Not a fallback in spirit - a slider is for finding the value
     * and a key is for landing on it - but it is also what still works if the cursor turns out
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
    if (g_visible && device != NULL) {
        camera_view_t   view;
        bool            have_camera = camera_read(&view);
        d3d8_viewport_t viewport;

        /* The device's own answer, so the pointer is clamped to the surface actually being drawn
         * into rather than to whatever the camera last reported. */
        memset(&viewport, 0, sizeof(viewport));
        if (SUCCEEDED(((d3d8_get_viewport_t)g_vtable[D3D8_GETVIEWPORT])(device, &viewport))
            && viewport.width > 0 && viewport.height > 0) {
            g_view_w = (int)viewport.width;
            g_view_h = (int)viewport.height;
        }

        /* The slider starts wherever the game currently is, so opening the menu never moves the
         * picture. Only a drag does. */
        if (g_fov_degrees <= 0.0f && have_camera) {
            g_fov_degrees = (float)(2.0 * to_degrees(atan((double)view.half_h
                                                          / (double)view.focal)));
            g_fov_degrees = clampf(g_fov_degrees, FOV_MIN, FOV_MAX);
        }

        if (g_mouse_x == 0 && g_mouse_y == 0) {
            g_mouse_x = PANEL_X + PANEL_W / 2;
            g_mouse_y = PANEL_Y + PANEL_H / 2;
        }
        read_mouse();
        handle_input();
        draw_menu(device, &view, have_camera);
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
        log_error("the Direct3D device could not be verified - the menu will not open. "
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
    /* One aligned pointer-sized store, which is atomic on x86 - so a render thread calling
     * EndScene at this instant gets either the old function or ours, never half of each. */
    g_vtable[D3D8_ENDSCENE] = (void *)hooked_end_scene;
    VirtualProtect(&g_vtable[D3D8_ENDSCENE], sizeof(void *), protection, &protection);

    g_hook_installed = true;
    log_info("device %08X, EndScene %08X -> %08X", (unsigned)(uintptr_t)device,
             (unsigned)(uintptr_t)g_original_end_scene, (unsigned)(uintptr_t)hooked_end_scene);
    return true;
}

/* ------------------------------------------------------------------------------- the thread */

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
        log_warning("raw mouse input is unavailable - the arrow keys still work, the pointer "
                    "will not");
    }

    for (;;) {
        bool pressed = (GetAsyncKeyState(g_toggle_key) & 0x8000) != 0;

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
                        log_warning("no mouse could be opened - the arrow keys still work");
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

    g_channel = channel_open();
    if (g_channel == NULL) {
        log_warning("the shared channel could not be opened - the slider will have nothing to "
                    "drive, though the menu will still open and report");
    }

    thread = CreateThread(NULL, 0, poll_thread, NULL, 0, NULL);
    if (thread == NULL) {
        log_error("could not start the hotkey thread");
        return;
    }
    CloseHandle(thread);

    log_info("installed, key code %d (the key under Escape). Nothing is hooked until you press "
             "it.", g_toggle_key);
}

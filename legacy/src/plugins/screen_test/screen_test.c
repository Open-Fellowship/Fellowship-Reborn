#include "screen_test.h"

#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"

#include <windows.h>

#include <stdint.h>
#include <string.h>

#define PLUGIN_SECTION "screen_test"

/* COM vtable positions on IDirect3DDevice8. Present is 15 and Clear is 36, counted from
 * QueryInterface at 0 - the same on every implementation, which is the whole reason this works
 * against wined3d and DXVK alike. */
#define D3D8_CREATEDEVICE     15
#define D3D8_DEVICE_PRESENT   15
#define D3D8_DEVICE_CLEAR     36

#define D3DCLEAR_TARGET       0x00000001u

typedef void *(WINAPI *direct3d_create8_t)(UINT sdk_version);
typedef HRESULT (STDMETHODCALLTYPE *create_device_t)(void *self, UINT adapter, DWORD device_type,
                                                     HWND focus_window, DWORD behaviour_flags,
                                                     void *parameters, void **returned_device);
typedef HRESULT (STDMETHODCALLTYPE *device_present_t)(void *self, const RECT *source,
                                                      const RECT *destination, HWND override,
                                                      const void *dirty_region);
typedef HRESULT (STDMETHODCALLTYPE *device_clear_t)(void *self, DWORD count, const void *rects,
                                                    DWORD flags, DWORD colour, float z,
                                                    DWORD stencil);

static direct3d_create8_t g_original_create;
static create_device_t    g_original_create_device;
static device_present_t   g_original_present;
static bool               g_device_hooked;
static bool               g_present_hooked;
static unsigned           g_frames;
static unsigned           g_interval = 60;   /* frames per colour */

/* Bright, unambiguous, and nothing in this game looks like any of them. */
static const DWORD g_colours[3] = { 0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu };
static const char *g_names[3]   = { "RED", "GREEN", "BLUE" };

static HRESULT STDMETHODCALLTYPE hooked_present(void *self, const RECT *source,
                                                const RECT *destination, HWND override,
                                                const void *dirty_region)
{
    void          **vtable = *(void ***)self;
    unsigned        step   = (g_frames / g_interval) % 3u;
    device_clear_t  clear;

    /* Painted BEFORE the frame goes out, and over the top of whatever the game drew, because the
     * question is not what the game drew - it is whether anything at all arrives on the panel. */
    clear = (device_clear_t)vtable[D3D8_DEVICE_CLEAR];
    clear(self, 0, NULL, D3DCLEAR_TARGET, g_colours[step], 1.0f, 0);

    if (g_frames == 0 || (g_frames % (g_interval * 3u)) == 0) {
        log_info("frame %u: the back buffer has been painted %s. If the screen is still black, "
                 "nothing the game presents is reaching the display.", g_frames, g_names[step]);
    }
    ++g_frames;

    return g_original_present(self, source, destination, override, dirty_region);
}

static void hook_device(void *device)
{
    void  **vtable;
    DWORD   protection = 0;

    if (g_present_hooked || device == NULL) {
        return;
    }
    vtable = *(void ***)device;
    if (!memory_is_readable_range((uintptr_t)vtable, (D3D8_DEVICE_CLEAR + 1) * sizeof(void *))) {
        log_warning("the device vtable is not readable - not painting anything");
        return;
    }
    if (!VirtualProtect(&vtable[D3D8_DEVICE_PRESENT], sizeof(void *), PAGE_READWRITE,
                        &protection)) {
        log_warning("the device vtable could not be made writable - not painting anything");
        return;
    }

    g_original_present = (device_present_t)vtable[D3D8_DEVICE_PRESENT];
    vtable[D3D8_DEVICE_PRESENT] = (void *)hooked_present;
    VirtualProtect(&vtable[D3D8_DEVICE_PRESENT], sizeof(void *), protection, &protection);

    g_present_hooked = true;
    log_info("painting every frame red, green, blue, %u frames each. Watch the screen, not this "
             "file.", g_interval);
}

static HRESULT STDMETHODCALLTYPE hooked_create_device(void *self, UINT adapter, DWORD device_type,
                                                      HWND focus_window, DWORD behaviour_flags,
                                                      void *parameters, void **returned_device)
{
    HRESULT result = g_original_create_device(self, adapter, device_type, focus_window,
                                              behaviour_flags, parameters, returned_device);

    if (SUCCEEDED(result) && returned_device != NULL) {
        hook_device(*returned_device);
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
        return;
    }
    if (!VirtualProtect(&vtable[D3D8_CREATEDEVICE], sizeof(void *), PAGE_READWRITE, &protection)) {
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

void screen_test_install(void)
{
    void **slot;
    int    configured;

    log_init(PLUGIN_SECTION, false);

    if (!ini_read_bool(PLUGIN_SECTION, "Enabled", false)) {
        log_info("Enabled=0. A diagnostic that PAINTS OVER THE GAME - only ever turn it on to "
                 "find out whether the screen works at all.");
        return;
    }
    if (!host_image_resolve()) {
        log_error("the host image could not be resolved");
        return;
    }

    configured = ini_read_int(PLUGIN_SECTION, "FramesPerColour", 60);
    if (configured >= 5 && configured <= 600) {
        g_interval = (unsigned)configured;
    }

    slot = find_import_slot("D3D8.dll", "Direct3DCreate8");
    if (slot == NULL) {
        slot = find_import_slot("d3d8.dll", "Direct3DCreate8");
    }
    if (slot == NULL) {
        log_error("Direct3DCreate8 is not imported by name - cannot install");
        return;
    }

    g_original_create = (direct3d_create8_t)*slot;
    if (!write_pointer(slot, (void *)hooked_direct3d_create8)) {
        log_error("the Direct3DCreate8 import slot could not be made writable");
        return;
    }

    log_info("installed. THE GAME WILL BE UNPLAYABLE while this is on - that is the point.");
}

#include "texture_scaling.h"

#include "common/camera.h"
#include "common/emit.h"
#include "common/engine_sites.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/module_watch.h"
#include "common/patch.h"
#include "common/trampoline.h"

#include <windows.h>

#include <stdint.h>
#include <string.h>

#define PLUGIN_SECTION "texture_scaling"

/* The mouse pointer is a GUIControl_Texture, and every one of them draws its art at the size of
 * its source rectangle in texels. At 3840x2160 the pointer is 32x32 device pixels while its own
 * data asks for 20% of the screen.
 *
 * The engine already has the mechanism to fix this and simply never uses it. Texture::Render, in
 * Fellowship.exe at 0x0043F1E0, takes a destination scale pair as its last two arguments and
 * computes the drawn extent as source * scale:
 *
 *     0043F391  fmul [esp+0xf8]     destination width  = source width  * scaleX
 *     0043F3AA  fmul [esp+0xfc]     destination height = source height * scaleY
 *
 * The pair comes from the control at +0x78 and +0x7C. The constructor writes 1.0 to both and
 * only the save slot thumbnails ever call SetScale, so every other control is 1:1 for ever.
 *
 * So there is nothing to patch in the draw. Find the pointer control, write the scale into the
 * two floats the engine already reads, and the art scales with the filtering switched on for
 * free, because Texture::Render selects that on whether the pair is exactly 1.0. */

/* The GUI manager storing its pointer control. ebp holds the object, or zero when the
 * construction above it failed, and six bytes is the whole instruction. */
#define CURSOR_RVA        0x67083u
#define CURSOR_RETURN_RVA 0x67089u
#define CURSOR_SIZE       6u

static const uint8_t cursor_expected[CURSOR_SIZE] = {
    0x89, 0xAE, 0x90, 0x00, 0x00, 0x00   /* mov [esi+0x90], ebp */
};

#define CONTROL_SCALE_X 0x78u
#define CONTROL_SCALE_Y 0x7Cu

/* SECOND SITE, a different class and a different mechanism with the same disease.
 *
 * FUN_1007B1A0 is slot 21 of the HUD Texture family, which draws the small circle under the
 * health bar. Its draw computes the scale as destination over source and takes the destination
 * from +0x40 and +0x44, so correcting those corrects the quad. That was never true on the
 * pointer path, where the source and the destination were the same number.
 *
 * The fallback below runs when the authored size is 1.0 or less, which for these objects it
 * always is, and copies the texture texel dimensions into the on-screen size:
 *
 *     1007B2A1  fxch st(1)             st0 = width, st1 = height
 *     1007B2A3  fstp [edi+0x40]        on-screen WIDTH  := texel width
 *     1007B2A6  fstp [edi+0x44]        on-screen HEIGHT := texel height
 *
 * MULTIPLY, never delete. Removing the copy leaves the authored size, which is zero for these,
 * and the element disappears instead of scaling. */
#define HUD_RVA        0x7B2A3u
#define HUD_RETURN_RVA 0x7B2A9u
#define HUD_SIZE       6u

static const uint8_t hud_expected[HUD_SIZE] = {
    0xD9, 0x5F, 0x40,        /* fstp [edi+0x40] */
    0xD9, 0x5F, 0x44         /* fstp [edi+0x44] */
};

/* The unscaled texel dimensions, captured by the stub before it multiplies, so the poll can
 * re-derive, never compound. */
static float g_hud_base_w = 1.0f;
static float g_hud_base_h = 1.0f;

/* Live scale, 1.0 until a camera validates. The stub multiplies by these, so a HUD built before
 * the camera is ready is left alone and the poll corrects it afterwards. */
static float g_scale_x = 1.0f;
static float g_scale_y = 1.0f;

#define HUD_ROWS 8

typedef struct hud_entry {
    uintptr_t control;
    float     base_w;
    float     base_h;
} hud_entry_t;

static hud_entry_t   g_hud[HUD_ROWS];
static volatile LONG g_hud_count;

/* THIRD SITE, the One Ring icon under the purple bar. Its own class, its own setup, and the same
 * disease a third time.
 *
 * FUN_1007ABB0 reads RingXSize and RingYSize, properties 26 and 27, both authored in texels with
 * a default of 64, and stores them straight into the on-screen size. Its draw re-reads the source
 * from the property table on every frame and takes only the destination from +0x40 and +0x44, so
 * correcting the setup is sufficient, exactly as it is for the HUD Texture family.
 *
 *     1007ACA1  mov [edi+0x44], ecx    on-screen HEIGHT := texel height
 *     1007ACA4  mov ecx, edi           the this for the call at 1007ACA9
 *     1007ACA6  mov [edi+0x40], eax    on-screen WIDTH  := texel width
 *
 * Eight bytes, and the middle instruction is not decoration: leaving it out sends the call below
 * a stale this. These are integer moves of float bit patterns, so the stub routes them through
 * the FPU, and does not multiply integers. */
#define RING_RVA        0x7ACA1u
#define RING_RETURN_RVA 0x7ACA9u
#define RING_SIZE       8u

static const uint8_t ring_expected[RING_SIZE] = {
    0x89, 0x4F, 0x44,        /* mov [edi+0x44], ecx */
    0x8B, 0xCF,              /* mov ecx, edi        */
    0x89, 0x47, 0x40         /* mov [edi+0x40], eax */
};

/* Written by the stub, read by the poll. Whatever the GUI manager last built. */
static volatile uintptr_t g_cursor;
static volatile uintptr_t g_camera;

static int32_t g_reference_width  = 640;
static int32_t g_reference_height = 480;

/* __cdecl, from the stub, after it has already stored the scaled values. Records the control and
 * the dimensions it was built from, so the poll can put the right numbers back when the camera
 * validates later than the HUD is built. */
static void __cdecl remember_hud(uintptr_t control)
{
    LONG count = g_hud_count;
    LONG i;

    if (control == 0) {
        return;
    }
    for (i = 0; i < count && i < HUD_ROWS; ++i) {
        if (g_hud[i].control == control) {
            g_hud[i].base_w = g_hud_base_w;
            g_hud[i].base_h = g_hud_base_h;
            return;
        }
    }
    if (count >= HUD_ROWS) {
        return;
    }
    g_hud[count].control = control;
    g_hud[count].base_w  = g_hud_base_w;
    g_hud[count].base_h  = g_hud_base_h;
    InterlockedExchange(&g_hud_count, count + 1);
    log_info("control %08X built from %.0f x %.0f texels",
             (unsigned)control, (double)g_hud_base_w, (double)g_hud_base_h);
}

static void *build_hud_stub(uintptr_t stub_address, uintptr_t return_address)
{
    uint8_t buffer[64];
    emit_t  emit;

    emit_init(&emit, buffer, sizeof(buffer));

    /* st0 is the width and st1 the height, both texel counts, after the fxch above. */
    emit_u8(&emit, 0xD9); emit_u8(&emit, 0x15);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_hud_base_w);
    emit_u8(&emit, 0xD8); emit_u8(&emit, 0x0D);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_scale_x);
    emit_u8(&emit, 0xD9); emit_u8(&emit, 0x5F); emit_u8(&emit, 0x40);

    emit_u8(&emit, 0xD9); emit_u8(&emit, 0x15);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_hud_base_h);
    emit_u8(&emit, 0xD8); emit_u8(&emit, 0x0D);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_scale_y);
    emit_u8(&emit, 0xD9); emit_u8(&emit, 0x5F); emit_u8(&emit, 0x44);

    emit_u8(&emit, 0x60);                                    /* pushad                       */
    emit_u8(&emit, 0x9C);                                    /* pushfd                       */
    emit_u8(&emit, 0x57);                                    /* push edi, the control        */
    emit_u8(&emit, 0xE8);
    emit_u32(&emit, (uint32_t)((uintptr_t)&remember_hud -
                               (stub_address + (uintptr_t)emit_size(&emit) + 4u)));
    emit_u8(&emit, 0x83); emit_u8(&emit, 0xC4); emit_u8(&emit, 0x04);
    emit_u8(&emit, 0x9D);                                    /* popfd                        */
    emit_u8(&emit, 0x61);                                    /* popad                        */
    emit_jump_rel32(&emit, stub_address, return_address);

    if (emit_overflowed(&emit)) {
        return NULL;
    }
    memcpy((void *)stub_address, buffer, emit_size(&emit));
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)stub_address, emit_size(&emit));
    return (void *)stub_address;
}

/* The ring stores through the same two fields as the HUD textures, so it joins the same table
 * and the same poll corrects it. */
static void *build_ring_stub(uintptr_t stub_address, uintptr_t return_address)
{
    uint8_t buffer[96];
    emit_t  emit;

    emit_init(&emit, buffer, sizeof(buffer));

    /* The two texel counts arrive as float bit patterns in ecx and eax. */
    emit_u8(&emit, 0x89); emit_u8(&emit, 0x0D);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_hud_base_h);     /* mov [base_h], ecx            */
    emit_u8(&emit, 0xA3);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_hud_base_w);     /* mov [base_w], eax            */

    emit_u8(&emit, 0xD9); emit_u8(&emit, 0x05);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_hud_base_h);     /* fld  [base_h]                */
    emit_u8(&emit, 0xD8); emit_u8(&emit, 0x0D);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_scale_y);        /* fmul [scale_y]               */
    emit_u8(&emit, 0xD9); emit_u8(&emit, 0x5F); emit_u8(&emit, 0x44);

    emit_u8(&emit, 0xD9); emit_u8(&emit, 0x05);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_hud_base_w);     /* fld  [base_w]                */
    emit_u8(&emit, 0xD8); emit_u8(&emit, 0x0D);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_scale_x);        /* fmul [scale_x]               */
    emit_u8(&emit, 0xD9); emit_u8(&emit, 0x5F); emit_u8(&emit, 0x40);

    emit_u8(&emit, 0x60);                                    /* pushad                       */
    emit_u8(&emit, 0x9C);                                    /* pushfd                       */
    emit_u8(&emit, 0x57);                                    /* push edi                     */
    emit_u8(&emit, 0xE8);
    emit_u32(&emit, (uint32_t)((uintptr_t)&remember_hud -
                               (stub_address + (uintptr_t)emit_size(&emit) + 4u)));
    emit_u8(&emit, 0x83); emit_u8(&emit, 0xC4); emit_u8(&emit, 0x04);
    emit_u8(&emit, 0x9D);                                    /* popfd                        */
    emit_u8(&emit, 0x61);                                    /* popad                        */

    /* AFTER popad, never before: popad would put the old ecx back and the call below the hook
     * would receive a stale this. */
    emit_u8(&emit, 0x8B); emit_u8(&emit, 0xCF);              /* mov ecx, edi                 */
    emit_jump_rel32(&emit, stub_address, return_address);

    if (emit_overflowed(&emit)) {
        return NULL;
    }
    memcpy((void *)stub_address, buffer, emit_size(&emit));
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)stub_address, emit_size(&emit));
    return (void *)stub_address;
}

static void *build_stub(uintptr_t stub_address, uintptr_t return_address)
{
    uint8_t buffer[32];
    emit_t  emit;

    emit_init(&emit, buffer, sizeof(buffer));

    emit_bytes(&emit, cursor_expected, CURSOR_SIZE);         /* the relocated store          */
    emit_u8(&emit, 0x89); emit_u8(&emit, 0x2D);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_cursor);         /* mov [g_cursor], ebp          */
    emit_jump_rel32(&emit, stub_address, return_address);

    if (emit_overflowed(&emit)) {
        return NULL;
    }
    memcpy((void *)stub_address, buffer, emit_size(&emit));
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)stub_address, emit_size(&emit));
    return (void *)stub_address;
}

/* Polled, not written once, because the manager rebuilds its pointer control and the
 * constructor puts 1.0 back every time it does. An aligned four byte float store is atomic on
 * x86, so the render thread reading these mid-write is not a hazard. */
static DWORD WINAPI hold_scale(LPVOID parameter)
{
    float announced_x = 0.0f;

    (void)parameter;
    for (;;) {
        uintptr_t control = g_cursor;
        uintptr_t camera  = g_camera;

        if (control != 0 && camera != 0 &&
            memory_is_readable_range(control, CONTROL_SCALE_Y + 4u)) {
            int32_t viewport_w = 0;
            int32_t viewport_h = 0;

            if (memory_read(camera + CAMERA_VIEWPORT_W, &viewport_w, sizeof(viewport_w)) &&
                memory_read(camera + CAMERA_VIEWPORT_H, &viewport_h, sizeof(viewport_h)) &&
                viewport_w > 0 && viewport_h > 0) {
                float x = (float)viewport_w / (float)g_reference_width;
                float y = (float)viewport_h / (float)g_reference_height;

                if (memory_make_writable(control + CONTROL_SCALE_X, 8u)) {
                    memcpy((void *)(control + CONTROL_SCALE_X), &x, sizeof(x));
                    memcpy((void *)(control + CONTROL_SCALE_Y), &y, sizeof(y));

                    if (x != announced_x) {
                        announced_x = x;
                        log_info("pointer control %08X scaled %.4f x %.4f",
                                 (unsigned)control, (double)x, (double)y);
                    }
                }

                /* The HUD textures, re-derived from the dimensions they were built with, so a
                 * repeated pass cannot compound. */
                g_scale_x = x;
                g_scale_y = y;
                {
                    LONG n = g_hud_count;
                    LONG i;

                    for (i = 0; i < n && i < HUD_ROWS; ++i) {
                        uintptr_t c = g_hud[i].control;
                        float     w = g_hud[i].base_w * x;
                        float     h = g_hud[i].base_h * y;

                        if (c != 0 && memory_is_readable_range(c, 0x48u) &&
                            memory_make_writable(c + 0x40u, 8u)) {
                            memcpy((void *)(c + 0x40u), &w, sizeof(w));
                            memcpy((void *)(c + 0x44u), &h, sizeof(h));
                        }
                    }
                }
            }
        }
        Sleep(250);
    }
}

static void on_rfl_loaded(uintptr_t rfl_base)
{
    uintptr_t site = rfl_site(rfl_base, CURSOR_RVA);
    uintptr_t stub_address;
    HANDLE    thread;

    if (!patch_validate_bytes(site, cursor_expected, CURSOR_SIZE)) {
        log_error("rfl+%X is not the store this was measured against, not installing",
                  CURSOR_RVA);
        return;
    }
    stub_address = (uintptr_t)trampoline_alloc(32);
    if (stub_address == 0) {
        log_error("could not allocate the stub");
        return;
    }
    if (build_stub(stub_address, rfl_site(rfl_base, CURSOR_RETURN_RVA)) == NULL) {
        log_error("the stub did not fit its buffer, not installing");
        return;
    }
    if (patch_write_jump(site, (const void *)stub_address, CURSOR_SIZE) != PATCH_RESULT_OK) {
        log_error("could not branch to the stub");
        return;
    }

    /* Independent of the pointer hook: if this site does not match, that fix still works. */
    {
        uintptr_t hud = rfl_site(rfl_base, HUD_RVA);

        if (patch_validate_bytes(hud, hud_expected, HUD_SIZE)) {
            uintptr_t hud_stub = (uintptr_t)trampoline_alloc(64);

            if (hud_stub != 0 &&
                build_hud_stub(hud_stub, rfl_site(rfl_base, HUD_RETURN_RVA)) != NULL &&
                patch_write_jump(hud, (const void *)hud_stub, HUD_SIZE) == PATCH_RESULT_OK) {
                log_info("  and rfl+%X -> stub at %08X, the HUD texture size",
                         HUD_RVA, (unsigned)hud_stub);
            } else {
                log_warning("the HUD texture hook could not be installed");
            }
        } else {
            log_warning("rfl+%X is not the HUD texture store this expects", HUD_RVA);
        }
    }

    /* Independent again: any of the three can fail without taking the others with it. */
    {
        uintptr_t ring = rfl_site(rfl_base, RING_RVA);

        if (patch_validate_bytes(ring, ring_expected, RING_SIZE)) {
            uintptr_t ring_stub = (uintptr_t)trampoline_alloc(96);

            if (ring_stub != 0 &&
                build_ring_stub(ring_stub, rfl_site(rfl_base, RING_RETURN_RVA)) != NULL &&
                patch_write_jump(ring, (const void *)ring_stub, RING_SIZE) == PATCH_RESULT_OK) {
                log_info("  and rfl+%X -> stub at %08X, the ring icon size",
                         RING_RVA, (unsigned)ring_stub);
            } else {
                log_warning("the ring icon hook could not be installed");
            }
        } else {
            log_warning("rfl+%X is not the ring icon store this expects", RING_RVA);
        }
    }

    thread = CreateThread(NULL, 0, hold_scale, NULL, 0, NULL);
    if (thread != NULL) {
        CloseHandle(thread);
    } else {
        log_error("could not start the scale thread; nothing would ever be written");
        return;
    }

    log_info("installed: rfl+%X -> stub at %08X, waiting for the pointer control",
             CURSOR_RVA, (unsigned)stub_address);
}

void texture_scaling_install(void)
{
    log_init(PLUGIN_SECTION, false);

    if (!ini_read_bool(PLUGIN_SECTION, "Enabled", false)) {
        log_info("Enabled=0, the mouse pointer stays the size of its own texture");
        return;
    }
    if (!host_image_resolve()) {
        log_error("the host image could not be resolved; refusing to touch anything");
        return;
    }

    g_reference_width  = (int32_t)ini_read_int(PLUGIN_SECTION, "ReferenceWidth", 640);
    g_reference_height = (int32_t)ini_read_int(PLUGIN_SECTION, "ReferenceHeight", 480);
    if (g_reference_width < 64 || g_reference_height < 64) {
        log_error("ReferenceWidth=%ld ReferenceHeight=%ld is not a resolution anything was "
                  "authored against, not installing",
                  (long)g_reference_width, (long)g_reference_height);
        return;
    }

    if (!camera_track(250, &g_camera, NULL)) {
        log_error("could not start the camera watch; nothing would ever be scaled");
        return;
    }

    if (!module_watch_when_loaded(FELLOWSHIP_RFL_MODULE, on_rfl_loaded, 60000)) {
        log_error("could not start the module watch");
    }
}

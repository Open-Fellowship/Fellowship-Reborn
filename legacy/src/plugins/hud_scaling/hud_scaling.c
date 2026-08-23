#include "hud_scaling.h"

#include "common/camera.h"
#include "common/emit.h"
#include "common/engine_sites.h"
#include "common/engine_types.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/module_watch.h"
#include "common/patch.h"
#include "common/trampoline.h"

#include <windows.h>

#include <stdint.h>
#include <string.h>

#define PLUGIN_SECTION "hud_scaling"

/* The control's pixels-per-unit, authored property 0x1C, stored raw at +0x9C with no
 * resolution term. Eight bytes relocated whole, three more than a branch needs. See README.md. */
#define HUD_HOOK_RVA   0x789A7u
#define HUD_RETURN_RVA 0x789AFu
#define HUD_HOOK_SIZE  8u

/* TWO THINGS MEASURED AND DELIBERATELY NOT DONE. Both look like the obvious next fix.
 *
 * 1. rfl+789BB, the untemplated branch that sets pixels-per-unit to exactly 1. Hooking it was
 *    written, shipped and measured, and IT CHANGED NOTHING: the in-game HUD is sized in fixed
 *    texels on a different draw path this plugin cannot reach.
 * 2. +0x98, which sits beside +0x9C and reads as the companion fix. Its one reader multiplies it
 *    by frame time, so it is an APPROACH RATE, not a size. Scaling it would make every animated
 *    control snap faster at 4K.
 *
 * The measurements are in README.md and HUD-FINDING.md. Read them before touching either. */

static const uint8_t hud_hook_expected[HUD_HOOK_SIZE] = {
    0xD9, 0x00,                          /* fld  dword ptr [eax]      */
    0xD9, 0x9E, 0x9C, 0x00, 0x00, 0x00   /* fstp dword ptr [esi+0x9C] */
};

static int32_t g_reference_width = 640;

/* THE STUB READS THROUGH OUR POINTER, NEVER THE ENGINE'S CAMERA GLOBAL. That global is not
 * always NULL-or-a-camera, and dereferencing garbage from a stub is an access violation with
 * nothing to catch it.
 *
 * Do not sample the width onto a timer either: the pause menu renders the world into a
 * sub-rectangle and the viewport IS that rectangle while the menu is drawn. A stub that finds
 * zero falls through unscaled, which is also the right answer at the menus. See README.md. */
static volatile uintptr_t g_camera;

static void *build_stub(uintptr_t stub_address, uintptr_t return_address)
{
    uint8_t buffer[64];
    emit_t  emit;
    size_t  to_plain;
    size_t  to_done;

    emit_init(&emit, buffer, sizeof(buffer));

    emit_u8(&emit, 0x53);                                    /* push ebx                     */
    emit_u8(&emit, 0x8B); emit_u8(&emit, 0x1D);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_camera);         /* mov ebx,[g_camera]           */
    emit_u8(&emit, 0x85); emit_u8(&emit, 0xDB);              /* test ebx,ebx                 */
    to_plain = emit_jcc_rel8(&emit, 0x74);                   /* je plain                     */

    emit_u8(&emit, 0xD9); emit_u8(&emit, 0x00);              /* fld dword ptr [eax]          */
    emit_u8(&emit, 0x68); emit_u32(&emit, (uint32_t)g_reference_width);  /* push <reference> */
    emit_u8(&emit, 0xDB); emit_u8(&emit, 0x83);
    emit_u32(&emit, CAMERA_VIEWPORT_W);                      /* fild dword ptr [ebx+0x254]   */
    emit_u8(&emit, 0xDA); emit_u8(&emit, 0x34); emit_u8(&emit, 0x24);   /* fidiv dword [esp] */
    emit_u8(&emit, 0x83); emit_u8(&emit, 0xC4); emit_u8(&emit, 0x04);   /* add esp,4         */
    emit_u8(&emit, 0xDE); emit_u8(&emit, 0xC9);              /* fmulp st(1),st(0)            */
    to_done = emit_jcc_rel8(&emit, 0xEB);                    /* jmp done                     */

    emit_patch_rel8(&emit, to_plain);
    emit_u8(&emit, 0xD9); emit_u8(&emit, 0x00);              /* plain: fld dword ptr [eax]   */

    emit_patch_rel8(&emit, to_done);
    emit_u8(&emit, 0x5B);                                    /* done: pop ebx                */
    emit_u8(&emit, 0xD9); emit_u8(&emit, 0x9E); emit_u32(&emit, 0x9Cu); /* fstp [esi+0x9C]   */
    emit_jump_rel32(&emit, stub_address, return_address);

    if (emit_overflowed(&emit)) {
        return NULL;
    }
    memcpy((void *)stub_address, buffer, emit_size(&emit));
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)stub_address, emit_size(&emit));
    return (void *)stub_address;
}

/* Logging only. The stub does not read anything this function touches. */
static void on_camera(const camera_view_t *view)
{
    log_info("camera validated, viewport %dx%d -> scale %.4f",
             (int)view->viewport_width, (int)view->viewport_height,
             (double)view->viewport_width / (double)g_reference_width);
}

static void on_rfl_loaded(uintptr_t rfl_base)
{
    uintptr_t      hook = rfl_site(rfl_base, HUD_HOOK_RVA);
    uintptr_t      stub_address;
    patch_result_t result;

    if (!patch_validate_bytes(hook, hud_hook_expected, HUD_HOOK_SIZE)) {
        log_error("rfl+%X does not hold the expected fld/fstp pair, not installing",
                  HUD_HOOK_RVA);
        return;
    }

    stub_address = (uintptr_t)trampoline_alloc(64);
    if (stub_address == 0) {
        log_error("could not allocate the stub");
        return;
    }
    if (build_stub(stub_address, rfl_site(rfl_base, HUD_RETURN_RVA)) == NULL) {
        log_error("the stub did not fit its buffer, not installing");
        return;
    }

    result = patch_write_jump(hook, (const void *)stub_address, HUD_HOOK_SIZE);
    if (result != PATCH_RESULT_OK) {
        log_error("could not branch to the stub - %s", patch_result_text(result));
        return;
    }

    log_info("installed: rfl+%X -> stub at %08X, scale = viewportWidth / %d",
             HUD_HOOK_RVA, (unsigned)stub_address, g_reference_width);
}

void hud_scaling_install(void)
{
    log_init(PLUGIN_SECTION, false);

    if (!ini_read_bool(PLUGIN_SECTION, "Enabled", true)) {
        log_info("Enabled=0, doing nothing");
        return;
    }
    if (!host_image_resolve()) {
        log_error("the host image could not be resolved; refusing to touch anything");
        return;
    }

    g_reference_width = ini_read_int(PLUGIN_SECTION, "ReferenceWidth", 640);
    if (g_reference_width < 320 || g_reference_width > 4096) {
        log_warning("ReferenceWidth=%ld is outside 320..4096, using 640",
                    (long)g_reference_width);
        g_reference_width = 640;
    }

    /* Started before the hook exists, so the slot is already populated by the time the first
     * GUI is built. Until then it is zero and the HUD is drawn stock. */
    if (!camera_track(250, &g_camera, on_camera)) {
        log_error("could not start the camera watch; the HUD would never be scaled");
        return;
    }

    /* Fellowship.rfl is not loaded yet: the loader calls us at the host's entry point. */
    if (!module_watch_when_loaded(fellowship_rfl_module_name(), on_rfl_loaded, 60000)) {
        log_error("could not start the module watch");
    }
}

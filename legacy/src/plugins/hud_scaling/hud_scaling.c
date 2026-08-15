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

/* The control's pixels-per-unit, read from authored property 0x1C and stored raw:
 *
 *   rfl+789A7   fld  dword ptr [eax]          the authored value, 3.0 for the slider
 *   rfl+789A9   fstp dword ptr [esi+0x9C]     stored with no resolution term at all
 *
 * Eight bytes, which is three more than a branch needs, so the pair is relocated whole.
 *
 * WIDTH, not height: this factor governs a horizontal extent. Text is the opposite case and
 * scales by height, which is why text_scaling is a separate plugin with a separate reference -
 * two different references is correct here, not an inconsistency. */
#define HUD_HOOK_RVA   0x789A7u
#define HUD_RETURN_RVA 0x789AFu
#define HUD_HOOK_SIZE  8u

/* THE OTHER HALF OF THE SAME DECISION - INVESTIGATED, MEASURED, NOT APPLIED
 *
 * rfl+78950 sets a control's two scalars, and it has two ways of doing it:
 *
 *     edi = get_template(this)
 *     if (edi) {                                  <- rfl+78987
 *         this->[0x98] = property 0x1B
 *         this->[0x9C] = property 0x1C            <- rfl+789A7, the hook below
 *     } else {                                    <- rfl+789B1
 *         this->[0x98] = 5.0f
 *         this->[0x9C] = 1.0f                     <- rfl+789BB
 *     }
 *
 * A control built without a template gets a pixels-per-unit of exactly 1, which never changed
 * with the resolution - so that branch looked like the reason the in-game HUD stayed small while
 * the templated slider scaled. A second hook at rfl+789BB was written, shipped and measured.
 *
 * IT CHANGED NOTHING. From the screenshots, against a 640x480 baseline:
 *
 *     health bar width      104 -> 598 px     x5.75      but it is natively a percentage of
 *     health bar height       6 ->   6 px     x1.00      the width: 16.3% at 640, 15.6% at 4K
 *     circle width           30 ->  29 px     x1.00
 *     circle height          29 ->  31 px     x1.00
 *
 * The circle is the same size in PIXELS at both resolutions, so it never passes through a
 * control's pixels-per-unit at all and nothing this plugin does can reach it. The in-game HUD is
 * positioned by percentage and sized in fixed texels - the same shape of bug as the inventory
 * cell art in _FixEnhancers/docs/12, and a different draw path from this one.
 *
 * So the hook came back out. The finding is kept here because the branch IS unscaled and someone
 * will find it again; what is written down with it is that fixing it does not fix the HUD.
 *
 * AND +0x98 IS A RATE, NOT A SIZE. It sits beside +0x9C, comes from the adjacent authored
 * property, and is set by the same function in the same two branches, so it reads as the obvious
 * companion fix. Its one reader says otherwise:
 *
 *     rfl+78BD7   fld   [this+0xA4]          target
 *                 fsub  [this+0xA8]          - current
 *                 fld   [frame_time]
 *     rfl+78BEB   fmul  [this+0x98]          * THIS
 *                 fmul  st(1)                * the difference
 *
 * Frame time multiplied by a difference is an approach rate, and 5.0 is a sensible one. Scaling
 * it by 6 at 4K would make every animated control snap six times faster, and nothing about that
 * symptom would look like a size bug. */

static const uint8_t hud_hook_expected[HUD_HOOK_SIZE] = {
    0xD9, 0x00,                          /* fld  dword ptr [eax]      */
    0xD9, 0x9E, 0x9C, 0x00, 0x00, 0x00   /* fstp dword ptr [esi+0x9C] */
};

static int32_t g_reference_width = 640;

/* OUR POINTER TO THE CAMERA, NOT THE ENGINE'S
 *
 * The stub reads the viewport width at the moment it runs, which is what the working version
 * did. Sampling it onto a timer instead was tried, in this plugin and in text_scaling, and it is
 * wrong for a specific reason: the pause menu renders the world into a sub-rectangle and the
 * camera's viewport IS that rectangle while the menu is drawn, so a value sampled a quarter of a
 * second earlier is a different number from the one the engine is using.
 *
 * What must not come back is the version that read the ENGINE's camera global. That global is
 * not always NULL-or-a-camera: a crash log from a second install reported a horizontal field of
 * view of 180.000 degrees through it, which only happens when the floats behind it are garbage,
 * and dereferencing that from a stub is an access violation with nothing to catch it.
 *
 * So the stub dereferences THIS. It is our variable. It is zero until a camera has passed every
 * check in camera_read(), it returns to zero the moment one stops passing, and a stub that finds
 * zero falls through unscaled - which is also the right answer at the menus, where a GUI built
 * with a divide-by-nothing would be a crash on the title screen. */
static volatile uintptr_t g_camera;

/* push ebx / ebx = our validated camera / if none, fall through unscaled
 *     fld [eax] ; st0 *= viewportWidth / reference
 *   plain:
 *     fld [eax]
 *   done:
 *     pop ebx ; fstp [esi+0x9C] ; jmp back */
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
        log_error("rfl+%X does not hold the expected fld/fstp pair - not installing",
                  HUD_HOOK_RVA);
        return;
    }

    stub_address = (uintptr_t)trampoline_alloc(64);
    if (stub_address == 0) {
        log_error("could not allocate the stub");
        return;
    }
    if (build_stub(stub_address, rfl_site(rfl_base, HUD_RETURN_RVA)) == NULL) {
        log_error("the stub did not fit its buffer - not installing");
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
        log_error("could not start the camera watch - the HUD would never be scaled");
        return;
    }

    /* Fellowship.rfl is not loaded yet: the loader calls us at the host's entry point. */
    if (!module_watch_when_loaded(FELLOWSHIP_RFL_MODULE, on_rfl_loaded, 60000)) {
        log_error("could not start the module watch");
    }
}

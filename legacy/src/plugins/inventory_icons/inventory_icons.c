#include "inventory_icons.h"

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

#define PLUGIN_SECTION "inventory_icons"

#define ICON_HOOK_RVA    0x7A42Du
#define ICON_RETURN_RVA  0x7A435u
#define ICON_HOOK_SIZE   8u

/* The rfl's own global holding the camera. An RFL address, so it is relative to the rfl's base,
 * not the executable's, the one place in this project where that distinction bites. */
#define RFL_CAMERA_GLOBAL_RVA (RFL_INTERFACE_GLOBAL - 0x10000000u)

static const uint8_t icon_hook_expected[ICON_HOOK_SIZE] = {
    0xD9, 0x5C, 0x24, 0x20,   /* fstp dword ptr [esp+0x20] */
    0xD9, 0x44, 0x24, 0x40    /* fld  dword ptr [esp+0x40] */
};

static void *build_stub(uintptr_t stub_address, uintptr_t return_address, uintptr_t camera_global)
{
    uint8_t buffer[64];
    emit_t  emit;
    size_t  to_restore;

    emit_init(&emit, buffer, sizeof(buffer));

    emit_bytes(&emit, icon_hook_expected, 4);                 /* fstp [esp+0x20] relocated  */
    emit_u8(&emit, 0x51);                                     /* push ecx                   */
    emit_u8(&emit, 0x8B); emit_u8(&emit, 0x0D); emit_u32(&emit, (uint32_t)camera_global);
    emit_u8(&emit, 0x85); emit_u8(&emit, 0xC9);               /* test ecx,ecx               */
    to_restore = emit_jcc_rel8(&emit, 0x74);                  /* je restore                 */

    emit_u8(&emit, 0x68); emit_u32(&emit, 128u);              /* push 128                   */
    emit_u8(&emit, 0xD9); emit_u8(&emit, 0x81); emit_u32(&emit, CAMERA_FOCAL);
    emit_u8(&emit, 0xDA); emit_u8(&emit, 0x89); emit_u32(&emit, CAMERA_DEVICE_W);
    emit_u8(&emit, 0xDA); emit_u8(&emit, 0x34); emit_u8(&emit, 0x24);  /* fidiv dword [esp] */
    emit_u8(&emit, 0x83); emit_u8(&emit, 0xC4); emit_u8(&emit, 0x04);  /* add esp,4         */
    /* esp is one push deeper than the engine's, so its [esp+0x2C] and [esp+0x20] are ours
     * at +0x30 and +0x24. */
    emit_u8(&emit, 0xD9); emit_u8(&emit, 0x54); emit_u8(&emit, 0x24); emit_u8(&emit, 0x30);
    emit_u8(&emit, 0xD9); emit_u8(&emit, 0x5C); emit_u8(&emit, 0x24); emit_u8(&emit, 0x24);

    emit_patch_rel8(&emit, to_restore);
    emit_u8(&emit, 0x59);                                     /* restore: pop ecx           */
    emit_bytes(&emit, icon_hook_expected + 4, 4);             /* fld [esp+0x40] relocated   */
    emit_jump_rel32(&emit, stub_address, return_address);

    if (emit_overflowed(&emit)) {
        return NULL;
    }
    memcpy((void *)stub_address, buffer, emit_size(&emit));
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)stub_address, emit_size(&emit));
    return (void *)stub_address;
}

static void on_rfl_loaded(uintptr_t rfl_base)
{
    uintptr_t hook = rfl_site(rfl_base, ICON_HOOK_RVA);
    uintptr_t stub_address;

    if (!patch_validate_bytes(hook, icon_hook_expected, ICON_HOOK_SIZE)) {
        log_error("rfl+%X does not hold the expected fstp/fld pair, not installing",
                  ICON_HOOK_RVA);
        return;
    }

    stub_address = (uintptr_t)trampoline_alloc(64);
    if (stub_address == 0) {
        log_error("could not allocate the stub");
        return;
    }
    if (build_stub(stub_address, rfl_site(rfl_base, ICON_RETURN_RVA),
                   rfl_base + RFL_CAMERA_GLOBAL_RVA) == NULL) {
        log_error("the stub did not fit its buffer, not installing");
        return;
    }
    if (patch_write_jump(hook, (const void *)stub_address, ICON_HOOK_SIZE)
        != PATCH_RESULT_OK) {
        log_error("could not branch to the stub");
        return;
    }

    log_info("installed: rfl+%X -> stub at %08X, distance bases = focal * W / 128",
             ICON_HOOK_RVA, (unsigned)stub_address);
}

void inventory_icons_install(void)
{
    log_init(PLUGIN_SECTION, false);

    /* Off by default, and the log says why rather than leaving somebody to wonder. */
    if (!ini_read_bool(PLUGIN_SECTION, "Enabled", false)) {
        log_info("Enabled=0. Only needed alongside a FOV mod that rewrites the engine's focal "
                 "numerator, CameraFieldOfView=-1.0 in Fellowship.ini. With a stock numerator "
                 "there is nothing to correct.");
        return;
    }
    if (!host_image_resolve()) {
        log_error("the host image could not be resolved; refusing to touch anything");
        return;
    }

    if (!module_watch_when_loaded(FELLOWSHIP_RFL_MODULE, on_rfl_loaded, 60000)) {
        log_error("could not start the module watch");
    }
}

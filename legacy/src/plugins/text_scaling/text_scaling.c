#include "text_scaling.h"

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

#define PLUGIN_SECTION "text_scaling"
#define STUB_CAPACITY  96u

static int32_t g_reference_height = 480;

/* OUR POINTER TO THE CAMERA, NOT THE ENGINE'S
 *
 * The stubs read the viewport height at the moment they run, and they have to. The pause menu
 * renders the world into a sub-rectangle; the camera's viewport IS that rectangle while the menu
 * is drawn. Sampling the scale on a timer instead was tried, and it rendered the menu's glyphs
 * at stock height against 4.5x width - squat and stretched, the exact failure these seven hooks
 * exist to avoid. Measured from the screenshot: capital G, 17 px tall and 86 px wide.
 *
 * What must not come back is the version that read the ENGINE's camera global. That global is
 * not always NULL-or-a-camera: a crash log from a second install showed field_of_view reading a
 * horizontal field of view of 180.000 degrees through it, which only happens when the floats
 * behind it are garbage, and dereferencing that from a stub is an access violation with nothing
 * to catch it and nowhere to report it. The integer stubs had a second way to die on top -
 * `idiv` faults outright when the quotient does not fit, which a nonsense numerator guarantees.
 *
 * So the stubs dereference THIS. It is our variable, in our data section. It is zero until a
 * camera has passed every check in camera_read(), it goes back to zero the moment one stops
 * passing, and a stub that finds zero falls through unscaled - the unmodified game. The pointer
 * is live; the trust is not blind.
 *
 * That validation also closes the `idiv` overflow for free: camera_read() will not publish a
 * camera whose viewport height is outside 64..32768, so eax * height / reference stays small. */
static volatile uintptr_t g_camera;

/* ------------------------------------------------------------------ the two scaling idioms */

/* FLOAT: st(0) *= viewportHeight / reference, guarded on our slot holding a camera.
 * Clobbers ebx, which the caller has already pushed. */
static void emit_float_scale(emit_t *emit)
{
    size_t to_skip;

    emit_u8(emit, 0x8B); emit_u8(emit, 0x1D);
    emit_u32(emit, (uint32_t)(uintptr_t)&g_camera);           /* mov ebx,[g_camera]         */
    emit_u8(emit, 0x85); emit_u8(emit, 0xDB);                 /* test ebx,ebx               */
    to_skip = emit_jcc_rel8(emit, 0x74);                      /* je skip                    */
    emit_u8(emit, 0x68); emit_u32(emit, (uint32_t)g_reference_height);
    emit_u8(emit, 0xDB); emit_u8(emit, 0x83); emit_u32(emit, CAMERA_VIEWPORT_H);
    emit_u8(emit, 0xDA); emit_u8(emit, 0x34); emit_u8(emit, 0x24);   /* fidiv dword [esp]   */
    emit_u8(emit, 0x83); emit_u8(emit, 0xC4); emit_u8(emit, 0x04);   /* add esp,4           */
    emit_u8(emit, 0xDE); emit_u8(emit, 0xC9);                 /* fmulp st(1),st(0)          */
    emit_patch_rel8(emit, to_skip);
}

/* INTEGER: eax = eax * viewportHeight / reference. Clobbers ecx and edx. */
static void emit_int_scale(emit_t *emit)
{
    size_t to_skip;

    emit_u8(emit, 0x8B); emit_u8(emit, 0x0D);
    emit_u32(emit, (uint32_t)(uintptr_t)&g_camera);           /* mov ecx,[g_camera]         */
    emit_u8(emit, 0x85); emit_u8(emit, 0xC9);                 /* test ecx,ecx               */
    to_skip = emit_jcc_rel8(emit, 0x74);                      /* je skip                    */
    emit_u8(emit, 0xF7); emit_u8(emit, 0xA9); emit_u32(emit, CAMERA_VIEWPORT_H); /* imul     */
    emit_u8(emit, 0xB9); emit_u32(emit, (uint32_t)g_reference_height);           /* mov ecx  */
    emit_u8(emit, 0xF7); emit_u8(emit, 0xF9);                 /* idiv ecx                   */
    emit_patch_rel8(emit, to_skip);
}

/* ------------------------------------------------------------------------------ the sites */

typedef void (*stub_builder_t)(emit_t *emit);

typedef struct site {
    const char    *name;
    uint32_t       hook_rva;
    uint8_t        original[14];
    size_t         original_size;
    stub_builder_t build;
} site_t;

/* Three calls through the interface vtable that each return a pixels-per-glyph-unit scale.
 * The call is relocated, then its result is scaled. */
static void build_scaled_call(emit_t *emit)
{
    emit_u8(emit, 0xFF); emit_u8(emit, 0x92); emit_u32(emit, 0xA4u);  /* call [edx+0xA4]    */
    emit_u8(emit, 0x53);                                              /* push ebx           */
    emit_float_scale(emit);
    emit_u8(emit, 0x5B);                                              /* pop ebx            */
}

/* The glyph's HEIGHT scale is not a call: it is a hard-coded `push 1.0f`. The 1.0f is pushed
 * anyway so the stack frame stays byte-identical, then overwritten in place - which is why this
 * one ends `fstp [esp+4]` rather than leaving a value on the FPU stack. Getting this wrong is
 * what made the first version of this fix render squat, stretched glyphs, and replacing it with
 * a `push` of a timer-sampled float brought that same failure straight back. */
static void build_glyph_height(emit_t *emit)
{
    size_t to_skip;

    emit_u8(emit, 0x68); emit_u32(emit, 0x3F800000u);         /* push 1.0f (relocated)      */
    emit_u8(emit, 0x53);                                      /* push ebx                   */
    emit_u8(emit, 0x8B); emit_u8(emit, 0x1D);
    emit_u32(emit, (uint32_t)(uintptr_t)&g_camera);
    emit_u8(emit, 0x85); emit_u8(emit, 0xDB);
    to_skip = emit_jcc_rel8(emit, 0x74);
    emit_u8(emit, 0x68); emit_u32(emit, (uint32_t)g_reference_height);
    emit_u8(emit, 0xDB); emit_u8(emit, 0x83); emit_u32(emit, CAMERA_VIEWPORT_H);
    emit_u8(emit, 0xDA); emit_u8(emit, 0x34); emit_u8(emit, 0x24);
    emit_u8(emit, 0x83); emit_u8(emit, 0xC4); emit_u8(emit, 0x04);
    emit_u8(emit, 0xD9); emit_u8(emit, 0x5C); emit_u8(emit, 0x24); emit_u8(emit, 0x04);
    emit_patch_rel8(emit, to_skip);
    emit_u8(emit, 0x5B);                                      /* pop ebx                    */
}

/* DrawString advances the pen past a space by adding font[+0x10] raw. */
static void build_draw_space(emit_t *emit)
{
    emit_u8(emit, 0x50); emit_u8(emit, 0x52);                 /* push eax / push edx        */
    emit_u8(emit, 0x8B); emit_u8(emit, 0x45); emit_u8(emit, 0x10);   /* mov eax,[ebp+0x10]  */
    emit_u8(emit, 0x51);                                      /* push ecx                   */
    emit_int_scale(emit);
    emit_u8(emit, 0x59);                                      /* pop ecx                    */
    emit_u8(emit, 0x01); emit_u8(emit, 0xC3);                 /* add ebx,eax                */
    emit_u8(emit, 0x5A); emit_u8(emit, 0x58);                 /* pop edx / pop eax          */
    emit_u8(emit, 0x89); emit_u8(emit, 0x5C); emit_u8(emit, 0x24); emit_u8(emit, 0x10);
}

/* MeasureString does the same thing from a different register, so the two disagree unless both
 * are scaled. [esp+0x10] becomes [esp+0x18] because two registers have been pushed. */
static void build_measure_space(emit_t *emit)
{
    emit_u8(emit, 0x50); emit_u8(emit, 0x52);
    emit_u8(emit, 0x8B); emit_u8(emit, 0x4C); emit_u8(emit, 0x24); emit_u8(emit, 0x18);
    emit_u8(emit, 0x8B); emit_u8(emit, 0x41); emit_u8(emit, 0x10);   /* mov eax,[ecx+0x10]  */
    emit_u8(emit, 0x51);
    emit_int_scale(emit);
    emit_u8(emit, 0x59);
    emit_u8(emit, 0x01); emit_u8(emit, 0xC3);
    emit_u8(emit, 0x5A); emit_u8(emit, 0x58);
}

/* GetLineHeight returns font[+0x0C] behind a validity check.
 *
 * Hooked from 0x63CA0 rather than the obvious 0x63CA9, and that is not a preference: the
 * function's own `je` targets an address INSIDE where a five-byte branch at 0x63CA9 would sit.
 * So the zero-check is reimplemented here instead of being jumped over. */
static void build_line_height(emit_t *emit)
{
    size_t to_done;

    emit_u8(emit, 0x8B); emit_u8(emit, 0x4E); emit_u8(emit, 0x24);   /* mov ecx,[esi+0x24]  */
    emit_u8(emit, 0x33); emit_u8(emit, 0xC0);                 /* xor eax,eax                */
    emit_u8(emit, 0x85); emit_u8(emit, 0xC9);                 /* test ecx,ecx               */
    to_done = emit_jcc_rel8(emit, 0x74);                      /* je done                    */
    emit_u8(emit, 0x8B); emit_u8(emit, 0x46); emit_u8(emit, 0x0C);   /* mov eax,[esi+0x0C]  */
    emit_u8(emit, 0x51); emit_u8(emit, 0x52);                 /* push ecx / push edx        */
    emit_int_scale(emit);
    emit_u8(emit, 0x5A); emit_u8(emit, 0x59);                 /* pop edx / pop ecx          */
    emit_patch_rel8(emit, to_done);
    emit_u8(emit, 0x5F); emit_u8(emit, 0x5E);                 /* pop edi / pop esi          */
}

static const site_t sites[] = {
    { "glyph height scale", 0x648B2,
      { 0x68, 0x00, 0x00, 0x80, 0x3F }, 5, build_glyph_height },
    { "glyph width scale",  0x648C8,
      { 0xFF, 0x92, 0xA4, 0x00, 0x00, 0x00 }, 6, build_scaled_call },
    { "pen advance",        0x64917,
      { 0xFF, 0x92, 0xA4, 0x00, 0x00, 0x00 }, 6, build_scaled_call },
    { "measured width",     0x64B2A,
      { 0xFF, 0x92, 0xA4, 0x00, 0x00, 0x00 }, 6, build_scaled_call },
    { "DrawString space",   0x64779,
      { 0x03, 0x5D, 0x10, 0x89, 0x5C, 0x24, 0x10 }, 7, build_draw_space },
    { "MeasureString space", 0x64A0B,
      { 0x8B, 0x4C, 0x24, 0x10, 0x03, 0x59, 0x10 }, 7, build_measure_space },
    { "line height",        0x63CA0,
      { 0x8B, 0x4E, 0x24, 0x33, 0xC0, 0x85, 0xC9, 0x74, 0x03,
        0x8B, 0x46, 0x0C, 0x5F, 0x5E }, 14, build_line_height },
};

#define SITE_COUNT (sizeof(sites) / sizeof(sites[0]))

/* Logging only. The stubs do not read anything this function touches. */
static void on_camera(const camera_view_t *view)
{
    log_info("camera validated, viewport %dx%d -> scale %.4f",
             (int)view->viewport_width, (int)view->viewport_height,
             (double)view->viewport_height / (double)g_reference_height);
}

static bool install_site(const site_t *site, uintptr_t rfl_base)
{
    uintptr_t hook = rfl_site(rfl_base, site->hook_rva);
    uintptr_t stub_address;
    uint8_t   buffer[STUB_CAPACITY];
    emit_t    emit;

    if (!patch_validate_bytes(hook, site->original, site->original_size)) {
        log_error("  rfl+%-6X %-20s unexpected bytes, skipped", site->hook_rva, site->name);
        return false;
    }

    stub_address = (uintptr_t)trampoline_alloc(STUB_CAPACITY);
    if (stub_address == 0) {
        log_error("  rfl+%-6X %-20s no memory for the stub", site->hook_rva, site->name);
        return false;
    }

    emit_init(&emit, buffer, sizeof(buffer));
    site->build(&emit);
    emit_jump_rel32(&emit, stub_address, hook + site->original_size);

    if (emit_overflowed(&emit)) {
        log_error("  rfl+%-6X %-20s stub did not fit", site->hook_rva, site->name);
        return false;
    }

    memcpy((void *)stub_address, buffer, emit_size(&emit));
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)stub_address, emit_size(&emit));

    if (patch_write_jump(hook, (const void *)stub_address, site->original_size)
        != PATCH_RESULT_OK) {
        log_error("  rfl+%-6X %-20s could not branch to the stub", site->hook_rva, site->name);
        return false;
    }

    log_info("  rfl+%-6X %-20s -> %08X (%u bytes)", site->hook_rva, site->name,
             (unsigned)stub_address, (unsigned)emit_size(&emit));
    return true;
}

static void on_rfl_loaded(uintptr_t rfl_base)
{
    size_t index;
    size_t applied = 0;

    for (index = 0; index < SITE_COUNT; ++index) {
        if (install_site(&sites[index], rfl_base)) {
            ++applied;
        }
    }

    if (applied == SITE_COUNT) {
        log_info("all %u hooks installed, scale = viewportHeight / %ld",
                 (unsigned)SITE_COUNT, (long)g_reference_height);
    } else {
        /* Partial is genuinely bad here and the log should not be cheerful about it. Text laid
         * out at one size and drawn at another is worse than text that is uniformly too small. */
        log_error("PARTIAL: %u of %u hooks. Text will be laid out at one size and drawn at "
                  "another. Set Enabled=0 and restart.", (unsigned)applied, (unsigned)SITE_COUNT);
    }
}

void text_scaling_install(void)
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

    g_reference_height = ini_read_int(PLUGIN_SECTION, "ReferenceHeight", 480);
    if (g_reference_height < 240 || g_reference_height > 4096) {
        log_warning("ReferenceHeight=%ld is outside 240..4096, using 480",
                    (long)g_reference_height);
        g_reference_height = 480;
    }

    /* Started before the hooks exist, so the slot is already populated by the time the first
     * glyph is drawn. Until then it is zero and the stubs fall through unscaled. */
    if (!camera_track(250, &g_camera, on_camera)) {
        log_error("could not start the camera watch - text would never be scaled");
        return;
    }

    if (!module_watch_when_loaded(FELLOWSHIP_RFL_MODULE, on_rfl_loaded, 60000)) {
        log_error("could not start the module watch");
    }
}

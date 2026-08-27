#include "view_distance.h"

#include "common/channel.h"
#include "common/engine_sites.h"
#include "common/host_image.h"
#include "common/memory.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/patch.h"

#include <windows.h>

#include <stdint.h>
#include <string.h>

#define PLUGIN_SECTION "view_distance"

static float g_visibility_cells = 120.0f;

/* Two floats where there was one. Both started at the same enormous value, which is what
 * "no far plane" and "ignore the authored fade" both amount to, but a slider on one of them
 * would have moved the other as well. */
static float g_far_plane   = 1.0e19f;
static float g_object_fade = 1.0e19f;

/* What the engine had before we repointed anything, so a far plane slider has somewhere to
 * return to. Sampled at install, because after the repoint nothing reads it any more. */
static float g_engine_far_plane = 1.0e19f;

/* Which controls this run actually installed. The menu can only move what the ini asked for:
 * a plugin that patches sites nobody switched on is a plugin that lies about what it does. */
#define INSTALLED_FAR_PLANE   0x1u
#define INSTALLED_FADE_CAP    0x2u
#define INSTALLED_CELLS       0x4u
#define INSTALLED_OBJECT_FADE 0x8u
#define INSTALLED_PRELOAD     0x10u
static uint32_t g_installed;

/* The ini's own values. The menu does not withdraw a request on purpose, but a torn read or a
 * value outside the range the channel accepts makes one look withdrawn, and the right answer
 * then is what the ini asked for, never whatever the slider last showed. */
static float    g_ini_cells;
static float    g_ini_fade;
static uint32_t g_ini_flags;

/* fld [0x5432AC], the visibility distance in cells. Seven readers, all the same instruction
 * form, so all seven are repointed together or the engine disagrees with itself about how far
 * away the world ends. The operand is at instruction + 2. */
static const uint32_t visibility_readers[] = {
    0x458A63u, 0x485D0Au, 0x485E4Cu, 0x49AC23u, 0x4A13A3u, 0x4A1E81u, 0x4A25DFu
};

/* fld [0x5432B8], the far plane, in the culling frustum and again in the software clipper. */
static const uint32_t far_plane_readers[] = { 0x4A5C22u, 0x494808u };

typedef struct branch_site {
    const char *name;
    uint32_t    preferred_va;
    uint8_t     expected[2];
    uint8_t     replacement[2];
    size_t      size;
} branch_site_t;

static bool apply_branches(const branch_site_t *sites, size_t count)
{
    size_t index;
    bool   all_ok = true;

    for (index = 0; index < count; ++index) {
        uintptr_t      address = exe_site(sites[index].preferred_va);
        patch_result_t result  = patch_write_expect(address, sites[index].expected,
                                                    sites[index].replacement,
                                                    sites[index].size);
        if (result == PATCH_RESULT_OK) {
            log_info("  %08X  %s", (unsigned)address, sites[index].name);
        } else {
            log_error("  %08X  %s - %s", (unsigned)address, sites[index].name,
                      patch_result_text(result));
            all_ok = false;
        }
    }
    return all_ok;
}

/* Rewrites `fld [reg+0xC4]` as `fld [absolute]`, and `mov ecx,[reg+0xC4]` as `mov ecx,[absolute]`.
 * Both are six bytes in each form, so the instruction after is untouched and no boundary moves.
 * The absolute is filled in at run time because it is the address of our own static. */
static patch_result_t repoint_field_read(uint32_t preferred_va, const uint8_t expected[6],
                                         uint8_t opcode_a, uint8_t opcode_b, const void *target)
{
    uint8_t   replacement[6];
    uint32_t  absolute = (uint32_t)(uintptr_t)target;

    replacement[0] = opcode_a;
    replacement[1] = opcode_b;
    memcpy(replacement + 2, &absolute, sizeof(absolute));

    return patch_write_expect(exe_site(preferred_va), expected, replacement,
                              sizeof(replacement));
}

static void apply_far_plane(void)
{
    size_t index;

    /* Read before the repoint, not after: once these instructions point at our float nothing
     * reads the engine's own value again, and a slider needs somewhere to return to. */
    if (!memory_read(exe_site(0x005432B8u), &g_engine_far_plane, sizeof(g_engine_far_plane))) {
        log_warning("the engine's own far plane could not be read; the menu will offer %g "
                    "as its off position", (double)g_engine_far_plane);
    }

    log_info("far plane -> our own float (%g), %u reader(s)",
             (double)g_far_plane, (unsigned)(sizeof(far_plane_readers) /
                                           sizeof(far_plane_readers[0])));
    for (index = 0; index < sizeof(far_plane_readers) / sizeof(far_plane_readers[0]); ++index) {
        uintptr_t      operand = exe_site(far_plane_readers[index]) + 2u;
        patch_result_t result  = patch_repoint_operand(operand, 0x005432B8u,
                                                       (uint32_t)(uintptr_t)&g_far_plane);
        if (result != PATCH_RESULT_OK) {
            log_error("  far plane reader %u - %s", (unsigned)index,
                      patch_result_text(result));
        }
    }
}

static void apply_visibility(float cells)
{
    size_t index;

    g_visibility_cells = cells;
    log_info("visibility -> %g cells (%g units), %u reader(s)",
             (double)cells, (double)cells * 2048.0,
             (unsigned)(sizeof(visibility_readers) / sizeof(visibility_readers[0])));

    for (index = 0; index < sizeof(visibility_readers) / sizeof(visibility_readers[0]); ++index) {
        uintptr_t      operand = exe_site(visibility_readers[index]) + 2u;
        patch_result_t result  = patch_repoint_operand(operand, EXE_VISIBILITY_CELLS,
                                                       (uint32_t)(uintptr_t)&g_visibility_cells);
        if (result != PATCH_RESULT_OK) {
            log_error("  visibility reader %u - %s", (unsigned)index,
                      patch_result_text(result));
        }
    }
}

static void apply_object_fade(void)
{
    static const uint8_t fld_edi[6] = { 0xD9, 0x87, 0xC4, 0x00, 0x00, 0x00 };
    static const uint8_t fld_esi[6] = { 0xD9, 0x86, 0xC4, 0x00, 0x00, 0x00 };
    static const uint8_t mov_ecx[6] = { 0x8B, 0x8F, 0xC4, 0x00, 0x00, 0x00 };

    static const branch_site_t bypass_min[] = {
        { "child objects bypass the min()", 0x458AD2, { 0x75, 0x06 }, { 0xEB, 0x06 }, 2 },
    };

    unsigned failed = 0;

    log_info("per-object fade distance ignored (%g)", (double)g_object_fade);

    /* Counted, not discarded. Some of these landing and the rest not leaves objects reading
     * their fade from two different places, which looks like a rendering fault and is not one. */
    if (repoint_field_read(0x485D04, fld_edi, 0xD9, 0x05, &g_object_fade) != PATCH_RESULT_OK) {
        ++failed;   /* cull: fade value  */
    }
    if (repoint_field_read(0x485DC3, fld_edi, 0xD9, 0x05, &g_object_fade) != PATCH_RESULT_OK) {
        ++failed;   /* cull: band start  */
    }
    if (repoint_field_read(0x485E52, mov_ecx, 0x8B, 0x0D, &g_object_fade) != PATCH_RESULT_OK) {
        ++failed;   /* alpha: fade value */
    }
    if (repoint_field_read(0x485E7B, fld_edi, 0xD9, 0x05, &g_object_fade) != PATCH_RESULT_OK) {
        ++failed;   /* alpha: band start */
    }
    if (repoint_field_read(0x458ABF, fld_esi, 0xD9, 0x05, &g_object_fade) != PATCH_RESULT_OK) {
        ++failed;   /* child objects     */
    }
    if (failed != 0) {
        log_error("PARTIAL, %u of the 5 fade reads did not take. The chain is now inconsistent; "
                  "set Enabled=0 and restart.", failed);
    }

    apply_branches(bypass_min, sizeof(bypass_min) / sizeof(bypass_min[0]));
}


/* ---------------------------------------------------------------- the live path

   The dev menu publishes a request on the channel and this plugin prefers it, the same
   arrangement field_of_view already has with the camera. Two of the five controls are floats
   the patched instructions already read, so changing them costs one store. The other three
   are branches, and each is written as a SINGLE aligned store: at 0x485D25, 0x485E71 and
   0x458AD2 only the opcode byte differs between the engine's form and ours, and 0x485B04 is
   two bytes at an even address. Nothing here can be caught half written by a thread that is
   executing it. */

static const uint32_t fade_cap_sites[] = { 0x00485D25u, 0x00485E71u };

static void set_byte(uint32_t va, uint8_t value)
{
    (void)patch_write_bytes(exe_site(va), &value, sizeof(value));
}

static void live_apply(float cells, float fade, uint32_t flags)
{
    if ((g_installed & INSTALLED_CELLS) != 0u) {
        g_visibility_cells = cells;
    }
    if ((g_installed & INSTALLED_OBJECT_FADE) != 0u) {
        g_object_fade = fade;
    }
    if ((g_installed & INSTALLED_FAR_PLANE) != 0u) {
        g_far_plane = ((flags & VIEW_DISTANCE_FLAG_FAR_PLANE) != 0u) ? 1.0e19f
                                                                     : g_engine_far_plane;
    }
    if ((g_installed & INSTALLED_FADE_CAP) != 0u) {
        uint8_t opcode = ((flags & VIEW_DISTANCE_FLAG_FADE_CAP) != 0u) ? 0xEBu : 0x75u;
        size_t  index;

        for (index = 0; index < sizeof(fade_cap_sites) / sizeof(fade_cap_sites[0]); ++index) {
            set_byte(fade_cap_sites[index], opcode);
        }
    }
    if ((g_installed & INSTALLED_PRELOAD) != 0u) {
        static const uint8_t ours[2]   = { 0x90, 0x90 };
        static const uint8_t engine[2] = { 0x75, 0x15 };
        const uint8_t       *bytes     = ((flags & VIEW_DISTANCE_FLAG_PRELOAD) != 0u)
                                             ? ours : engine;

        (void)patch_write_bytes(exe_site(0x00485B04u), bytes, 2);
    }
}

static DWORD WINAPI live_thread(LPVOID parameter)
{
    channel_block_t *channel = channel_open();
    bool             taken   = false;

    (void)parameter;
    if (channel == NULL) {
        log_warning("the channel could not be opened; the dev menu cannot reach this plugin");
        return 0;
    }

    for (;;) {
        float    cells;
        float    fade;
        uint32_t flags;

        if (channel_read_view_distance(channel, &cells, &fade, &flags)) {
            live_apply(cells, fade, flags);
            if (!taken) {
                taken = true;
                log_info("the dev menu has taken the draw distance");
            }
        } else if (taken) {
            /* Released. Back to what the ini asked for, not to whatever the slider was
             * showing when the menu closed. */
            live_apply(g_ini_cells, g_ini_fade, g_ini_flags);
            taken = false;
            log_info("the dev menu released it; back to %g cells", (double)g_ini_cells);
        }

        Sleep(16);
    }
}

void view_distance_install(void)
{
    static const branch_site_t fade_cap[] = {
        { "object fade ignores the visibility cap", 0x485D25, { 0x75, 0x06 }, { 0xEB, 0x06 }, 2 },
        { "ditto, alpha ramp",                      0x485E71, { 0x75, 0x06 }, { 0xEB, 0x06 }, 2 },
    };
    static const branch_site_t preload[] = {
        { "request object resources regardless of distance",
          0x485B04, { 0x75, 0x15 }, { 0x90, 0x90 }, 2 },
    };

    float cells;

    log_init(PLUGIN_SECTION, false);

    if (!ini_read_bool(PLUGIN_SECTION, "Enabled", true)) {
        log_info("Enabled=0, the engine keeps its own draw distances");
        return;
    }
    if (!host_image_resolve()) {
        log_error("the host image could not be resolved; refusing to touch anything");
        return;
    }

    if (ini_read_bool(PLUGIN_SECTION, "FarPlane", true)) {
        apply_far_plane();
        g_installed |= INSTALLED_FAR_PLANE;
        g_ini_flags |= VIEW_DISTANCE_FLAG_FAR_PLANE;
    }
    if (ini_read_bool(PLUGIN_SECTION, "FadeIgnoresCap", true)) {
        log_info("object fade no longer clamped to the visibility distance");
        apply_branches(fade_cap, sizeof(fade_cap) / sizeof(fade_cap[0]));
        g_installed |= INSTALLED_FADE_CAP;
        g_ini_flags |= VIEW_DISTANCE_FLAG_FADE_CAP;
    }

    cells = ini_read_float(PLUGIN_SECTION, "VisibilityCells", 120.0f);
    if (cells > 0.0f) {
        /* The engine's own value is 80. Below that this plugin would be making things worse for
         * no reason, and above about 200 the cell walk costs more than the extra scenery is
         * worth; neither is enforced, but both are worth saying once in the log. */
        if (cells < 80.0f) {
            log_warning("VisibilityCells=%g is BELOW the engine's own 80; you are reducing "
                        "the draw distance", (double)cells);
        }
        apply_visibility(cells);
        g_installed |= INSTALLED_CELLS;
    }
    g_ini_cells = cells;

    if (ini_read_bool(PLUGIN_SECTION, "IgnoreObjectFade", true)) {
        apply_object_fade();
        g_installed |= INSTALLED_OBJECT_FADE;
    }
    g_ini_fade = g_object_fade;

    if (ini_read_bool(PLUGIN_SECTION, "PreloadResources", true)) {
        log_info("object resources requested regardless of distance");
        apply_branches(preload, sizeof(preload) / sizeof(preload[0]));
        g_installed |= INSTALLED_PRELOAD;
        g_ini_flags |= VIEW_DISTANCE_FLAG_PRELOAD;
    }

    /* Nothing to listen for if the ini switched everything off, and a thread that can never
     * do anything is a thread nobody should have to wonder about in a debugger. */
    if (g_installed != 0u) {
        HANDLE thread = CreateThread(NULL, 0, live_thread, NULL, 0, NULL);

        if (thread != NULL) {
            CloseHandle(thread);
        } else {
            log_warning("the live thread could not be started; the ini values still apply");
        }
    }

    log_info("installed");
}

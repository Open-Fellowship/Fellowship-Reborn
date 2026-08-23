#include "game_speed.h"

#include "common/engine_sites.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"

#include <windows.h>

#include <string.h>

#define PLUGIN_SECTION "game_speed"

/* .rdata, so it is one float and no instruction boundary to respect. The key and the plugin
 * keep the patcher's names. */
#define TIMESTEP_VA 0x0051C764u

void game_speed_install(void)
{
    static const uint8_t expected[4] = { 0x6F, 0x12, 0x03, 0x3B };   /* 0.002f */

    uintptr_t      address;
    float          wanted;
    uint8_t        replacement[4];
    patch_result_t result;

    log_init(PLUGIN_SECTION, false);

    if (!ini_read_bool(PLUGIN_SECTION, "Enabled", true)) {
        log_info("Enabled=0, the engine keeps its own timestep");
        return;
    }
    if (!host_image_resolve()) {
        log_error("the host image could not be resolved; refusing to touch anything");
        return;
    }

    wanted = ini_read_float(PLUGIN_SECTION, "Timestep", 0.0001f);
    /* Below about a microsecond the step stops being a step and starts being a way to spend the
     * whole frame inside the simulation loop; above the engine's own 0.002 this plugin is making
     * things worse than not installing it. */
    if (wanted < 0.000001f || wanted > 0.002f) {
        log_warning("Timestep=%g is outside 0.000001..0.002, using 0.0001", (double)wanted);
        wanted = 0.0001f;
    }

    address = exe_site(TIMESTEP_VA);
    memcpy(replacement, &wanted, sizeof(replacement));

    result = patch_write_expect(address, expected, replacement, sizeof(expected));
    if (result == PATCH_RESULT_OK) {
        log_info("timestep %08X: 0.002 -> %g", (unsigned)address, (double)wanted);
    } else {
        log_error("timestep %08X - %s", (unsigned)address, patch_result_text(result));
    }
}

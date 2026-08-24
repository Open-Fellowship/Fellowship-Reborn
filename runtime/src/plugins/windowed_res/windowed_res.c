#include "windowed_res.h"

#include "common/engine_sites.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"

#include <windows.h>

#include <string.h>

#define PLUGIN_SECTION "windowed_res"

#define WIDTH_IMMEDIATE_VA  0x004BC4A5u
#define HEIGHT_IMMEDIATE_VA 0x004BC4AFu
#define LIST_LIMIT_VA       0x004BC5A3u
#define MODE_BRANCH_VA      0x004BC4FFu

void windowed_res_install(void)
{
    static const uint8_t branch_unlocked[2] = { 0x90, 0xE9 };   /* resolution_unlock's version */
    static const uint8_t branch_original[2] = { 0x0F, 0x84 };

    int32_t   width;
    int32_t   height;
    uintptr_t address;

    log_init(PLUGIN_SECTION, false);

    if (!ini_read_bool(PLUGIN_SECTION, "Enabled", false)) {
        log_info("Enabled=0, the game opens its window at whatever size it chose");
        return;
    }
    if (!host_image_resolve()) {
        log_error("the host image could not be resolved; refusing to touch anything");
        return;
    }

    width  = ini_read_int(PLUGIN_SECTION, "Width", 640);
    height = ini_read_int(PLUGIN_SECTION, "Height", 480);
    if (width < 640 || height < 480 || width > 16384 || height > 16384) {
        log_error("Width=%ld Height=%ld is outside 640x480 .. 16384x16384, not installing. "
                  "The engine rejects anything under 640x480 a few instructions later anyway.",
                  (long)width, (long)height);
        return;
    }

    {
        /* The two immediates the engine loads its default window size from. Both are
         * checked before either is written: a build that disagrees about one of them is
         * not a build this plugin understands, and half a window size is worse than none. */
        static const uint8_t expected_width[4]  = { 0x80, 0x02, 0x00, 0x00 };   /* 640 */
        static const uint8_t expected_height[4] = { 0xE0, 0x01, 0x00, 0x00 };   /* 480 */

        uintptr_t width_site  = exe_site(WIDTH_IMMEDIATE_VA);
        uintptr_t height_site = exe_site(HEIGHT_IMMEDIATE_VA);

        if (!patch_validate_bytes(width_site, expected_width, sizeof(expected_width)) ||
            !patch_validate_bytes(height_site, expected_height, sizeof(expected_height))) {
            log_error("the default window size is not the 640x480 this build expects, "
                      "not installing");
            return;
        }
        if (patch_write_bytes(width_site, &width, sizeof(width)) != PATCH_RESULT_OK) {
            log_error("the window width could not be written, the game keeps its own size");
            return;
        }
        if (patch_write_bytes(height_site, &height, sizeof(height)) != PATCH_RESULT_OK) {
            /* The width is already changed at this point. Put it back: a window 640 wide and
             * whatever tall the ini asked for is a size nobody chose. */
            patch_write_bytes(width_site, expected_width, sizeof(expected_width));
            log_error("the window height could not be written. The width has been put back "
                      "and the game keeps its own size");
            return;
        }
        log_info("  %08X  window width  -> %ld", (unsigned)width_site, (long)width);
        log_info("  %08X  window height -> %ld", (unsigned)height_site, (long)height);
    }

    {
        static const uint8_t expected_limit[1]    = { 0x24 };
        static const uint8_t replacement_limit[1] = { 0x0C };
        address = exe_site(LIST_LIMIT_VA);
        if (patch_write_expect(address, expected_limit, replacement_limit, 1)
            == PATCH_RESULT_OK) {
            log_info("  %08X  mode list limit 36 -> 12", (unsigned)address);
        }
    }

    /* The one that steps on another plugin. Say so rather than doing it quietly. */
    address = exe_site(MODE_BRANCH_VA);
    if (patch_validate_bytes(address, branch_unlocked, sizeof(branch_unlocked))) {
        log_warning("%08X was unlocked by resolution_unlock; putting the original branch back. "
                    "These two plugins want opposite things here, run one or the other.",
                    (unsigned)address);
        patch_write_bytes(address, branch_original, sizeof(branch_original));
    }

    log_info("installed");
}

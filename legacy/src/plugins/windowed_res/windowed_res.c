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

/*   0x4BC49E   C7 05 74 5C 56 00 80 02 00 00   mov dword [0x565C74], 0x280   default width  640
 *   0x4BC4A8   C7 05 78 5C 56 00 E0 01 00 00   mov dword [0x565C78], 0x1E0   default height 480
 *   0x4BC5A1   83 FF 24                        cmp edi, 0x24    mode-list limit, 36 -> 12
 *   0x4BC4FF   the je resolution_unlock made unconditional, put back
 */
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

    address = exe_site(WIDTH_IMMEDIATE_VA);
    if (patch_write_bytes(address, &width, sizeof(width)) == PATCH_RESULT_OK) {
        log_info("  %08X  window width  -> %ld", (unsigned)address, (long)width);
    }
    address = exe_site(HEIGHT_IMMEDIATE_VA);
    if (patch_write_bytes(address, &height, sizeof(height)) == PATCH_RESULT_OK) {
        log_info("  %08X  window height -> %ld", (unsigned)address, (long)height);
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

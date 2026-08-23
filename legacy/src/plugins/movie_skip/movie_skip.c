#include "movie_skip.h"

#include "common/engine_sites.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/patch.h"
#include "common/platform.h"

#include <windows.h>

#include <stdint.h>

#define PLUGIN_SECTION "movie_skip"

#define MOVIE_UPDATE_SITE_VA 0x0047BA29u

void movie_skip_install(void)
{
    /* mov eax,[esi+0Ch] / and eax,3 / cmp al,3 / jne rel32, the whole state check, verified
     * before the first three bytes of it are rewritten. */
    static const uint8_t expected[10]   = { 0x8B, 0x46, 0x0C, 0x83, 0xE0, 0x03, 0x3C, 0x03,
                                            0x0F, 0x85 };
    /* jmp short 0x47BA43 ; nop */
    static const uint8_t replacement[3] = { 0xEB, 0x18, 0x90 };

    uintptr_t      address;
    patch_result_t result;

    log_init(PLUGIN_SECTION, false);

    if (!ini_read_bool(PLUGIN_SECTION, "Enabled", platform_is_wine())) {
        log_info("Enabled=0, the engine plays its movies as it always did");
        return;
    }
    if (platform_is_wine()) {
        log_info("this is WINE %s; the Windows Media path does not complete here, so this is on "
                 "unless the ini says otherwise", platform_wine_version());
    }
    if (!host_image_resolve()) {
        log_error("the host image could not be resolved; refusing to touch anything");
        return;
    }

    address = exe_site(MOVIE_UPDATE_SITE_VA);

    /* Verify the full ten-byte signature, write only the first three. */
    if (!patch_validate_bytes(address, expected, sizeof(expected))) {
        log_error("%08X, not the MoviePC::Update state check this plugin was measured against; "
                  "leaving it alone", (unsigned)address);
        return;
    }
    result = patch_write_expect(address, expected, replacement, sizeof(replacement));
    if (result != PATCH_RESULT_OK) {
        log_error("%08X - %s", (unsigned)address, patch_result_text(result));
        return;
    }

    log_info("%08X  MoviePC::Update -> \"this movie is over\" on its first tick", (unsigned)address);
    log_info("  every movie now reports completion through the engine's own not-ready path: the "
             "callback fires, the frame-mode bit clears, the media manager forgets it, and "
             "whoever was waiting for the opening movies to end gets told that they have.");
}

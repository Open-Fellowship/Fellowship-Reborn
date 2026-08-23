#include "model_lod.h"

#include "common/engine_sites.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/patch.h"

#include <windows.h>

#define PLUGIN_SECTION "model_lod"

void model_lod_install(void)
{
    static const uint8_t finer_expected[2]   = { 0x75, 0x54 };
    static const uint8_t finer_replace[2]    = { 0x90, 0x90 };
    static const uint8_t coarser_expected[1] = { 0x7A };
    static const uint8_t coarser_replace[1]  = { 0xEB };

    patch_result_t finer;
    patch_result_t coarser;

    log_init(PLUGIN_SECTION, false);

    /* On by default: the project ships an improved picture out of the box, and this is one of the
     * cheapest parts of it. It does cost frame rate in crowded scenes, so it has a switch. */
    if (!ini_read_bool(PLUGIN_SECTION, "Enabled", true)) {
        log_info("Enabled=0, models keep the engine's own LOD stepping");
        return;
    }
    if (!host_image_resolve()) {
        log_error("the host image could not be resolved; refusing to touch anything");
        return;
    }

    finer = patch_write_expect(exe_site(0x485B97), finer_expected, finer_replace,
                               sizeof(finer_expected));
    coarser = patch_write_expect(exe_site(0x485C46), coarser_expected, coarser_replace,
                                 sizeof(coarser_expected));

    if (finer == PATCH_RESULT_OK && coarser == PATCH_RESULT_OK) {
        log_info("models pinned to their finest LOD");
    } else {
        /* Half of this patch is worse than none: one branch settled and the other not means the
         * engine can step coarser and never come back. Say so loudly. */
        log_error("PARTIAL, finer %s, coarser %s. The LOD chain is now inconsistent; "
                  "set Enabled=0 and restart.",
                  patch_result_text(finer), patch_result_text(coarser));
    }
}

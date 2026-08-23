#include "edge_popin.h"

#include "common/engine_sites.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/patch.h"

#include <windows.h>

#define PLUGIN_SECTION "edge_popin"

typedef struct site {
    const char *name;
    uint32_t    preferred_va;
    uint8_t     expected[6];
    uint8_t     replacement[6];
    size_t      size;
} site_t;

static const site_t sites[] = {
    { "objects use the real view frustum", 0x485867,
      { 0x75, 0x06 },                   { 0xEB, 0x06 },                   2 },
    { "guard rect left/top  -1024 -> -32768", 0x48B984,
      { 0x00, 0xFC, 0xFF, 0xFF },       { 0x00, 0x80, 0xFF, 0xFF },       4 },
    { "guard rect right/bot   3072 -> 32768", 0x48B992,
      { 0x00, 0x0C, 0x00, 0x00 },       { 0x00, 0x80, 0x00, 0x00 },       4 },
};

void edge_popin_install(void)
{
    size_t index;
    size_t applied = 0;

    log_init(PLUGIN_SECTION, false);

    if (!ini_read_bool(PLUGIN_SECTION, "Enabled", true)) {
        log_info("Enabled=0, doing nothing");
        return;
    }
    if (!host_image_resolve()) {
        log_error("the host image could not be resolved; refusing to touch anything");
        return;
    }

    for (index = 0; index < sizeof(sites) / sizeof(sites[0]); ++index) {
        const site_t  *site    = &sites[index];
        uintptr_t      address = exe_site(site->preferred_va);
        patch_result_t result  = patch_write_expect(address, site->expected,
                                                    site->replacement, site->size);
        if (result == PATCH_RESULT_OK) {
            ++applied;
            log_info("%08X  %s", (unsigned)address, site->name);
        } else {
            log_error("%08X  %s - %s", (unsigned)address, site->name,
                      patch_result_text(result));
        }
    }

    log_info("%u of %u site(s) applied", (unsigned)applied,
             (unsigned)(sizeof(sites) / sizeof(sites[0])));
}

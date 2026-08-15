#include "resolution_unlock.h"

#include "common/engine_sites.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/patch.h"

#include <windows.h>

#define PLUGIN_SECTION "resolution_unlock"

/* Every site verified against Fellowship.exe, No-CD, 2,133,459 bytes.
 *
 *   0x4BC4FF   0F 84 AD 00 00 00   je 0x4BC5B2
 *              -> 90 E9 AD 00 00 00   nop ; jmp 0x4BC5B2
 *
 *      The displacement is unchanged and still correct: the jmp now starts one byte later and
 *      is one byte shorter, so 0x4BC500+5+0xAD is the same 0x4BC5B2 the je reached.
 *
 *   0x4BC61C   0F 85 83 00 00 00   jne 0x4BC6A5
 *              -> displacement 0, so it falls through to 0x4BC622
 *
 *   0x4BC62D   7A 78               jp 0x4BC6A7
 *              -> displacement 0, so it falls through to 0x4BC62F
 */
typedef struct site {
    const char *name;
    uint32_t    preferred_va;
    uint8_t     expected[6];
    uint8_t     replacement[6];
    size_t      size;
} site_t;

static const site_t sites[] = {
    { "mode accepted unconditionally", 0x4BC4FF,
      { 0x0F, 0x84, 0xAD, 0x00, 0x00, 0x00 },
      { 0x90, 0xE9, 0xAD, 0x00, 0x00, 0x00 }, 6 },
    { "first reject branch defused",   0x4BC61C,
      { 0x0F, 0x85, 0x83, 0x00, 0x00, 0x00 },
      { 0x0F, 0x85, 0x00, 0x00, 0x00, 0x00 }, 6 },
    { "second reject branch defused",  0x4BC62D,
      { 0x7A, 0x78 },
      { 0x7A, 0x00 },                         2 },
};

void resolution_unlock_install(void)
{
    size_t index;
    size_t applied = 0;

    log_init(PLUGIN_SECTION, false);

    if (!ini_read_bool(PLUGIN_SECTION, "Enabled", true)) {
        log_info("Enabled=0, the options screen keeps the engine's own mode filter");
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
            log_info("  %08X  %s", (unsigned)address, site->name);
        } else {
            log_error("  %08X  %s - %s", (unsigned)address, site->name,
                      patch_result_text(result));
        }
    }

    if (applied == sizeof(sites) / sizeof(sites[0])) {
        log_info("installed - the mode list is no longer filtered");
    } else {
        log_error("PARTIAL: %u of 3. Some modes will be offered and others rejected, which is "
                  "worse than either. Set Enabled=0 and restart.", (unsigned)applied);
    }
}

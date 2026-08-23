#include "black_screen.h"

#include "common/engine_sites.h"
#include "common/host_image.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"

#include <windows.h>

#include <stdint.h>

#define PLUGIN_SECTION "black_screen"

/* The whole decision, opcodes included, so that a build whose code is arranged differently is
 * refused rather than written into. `mov eax, imm32` alone would be far too weak a claim: the
 * byte 0xB8 occurs thousands of times in this executable and most of them are not this. */
#define SIGNATURE_VA   0x0043D2B6u
#define IMMEDIATE_VA   0x0043D2BCu

static const uint8_t signature[] = {
    0x83, 0xF9, 0x08,        /* cmp ecx,8       is the caller asking for 8-bit? */
    0x75, 0x06,              /* jne +6          no: fall through to the 16-bit branch */
    0xB8                     /* mov eax, imm32  yes: this is the answer */
};

#define D3DFMT_P8  41u       /* 8-bit paletted. NVIDIA dropped it; AMD still carries it. */
#define D3DFMT_L8  50u       /* 8-bit luminance. Every driver supports it. */

void black_screen_install(void)
{
    uintptr_t      signature_site;
    uintptr_t      immediate_site;
    uint32_t       format = 0;
    patch_result_t result;

    log_init(PLUGIN_SECTION, false);

    /* NO SWITCH. This one reads the constant before it writes and declines on anything it does
     * not recognise: a copy already answering D3DFMT_L8 is left alone and told so, and a value
     * that is neither 41 nor 50 is treated as a different build rather than as a bug. There is
     * nothing an `Enabled` key protects against that those two checks do not, and the mistake it
     * invites, leaving it off on an NVIDIA card, is a black screen at load with no clue as to
     * why, which is the exact failure this plugin exists to prevent.
     *
     * A plugin is still switched off the way every plugin is: delete its DLL from plugins. */
    if (!host_image_resolve()) {
        log_error("the host image could not be resolved; refusing to touch anything");
        return;
    }

    signature_site = exe_site(SIGNATURE_VA);
    immediate_site = exe_site(IMMEDIATE_VA);

    if (!patch_validate_bytes(signature_site, signature, sizeof(signature))) {
        log_error("%08X is not the 8-bit branch of the format mapper on this build, "
                  "refusing to write", (unsigned)SIGNATURE_VA);
        return;
    }
    if (!memory_read_u32(immediate_site, &format)) {
        log_error("could not read the format constant at %08X", (unsigned)IMMEDIATE_VA);
        return;
    }

    if (format == D3DFMT_L8) {
        /* The expected outcome on the build this project targets, and the reason this plugin is
         * a guard rather than a fix. Said plainly, so that nobody reading a log concludes their
         * black screen was dealt with here when it was not. */
        log_info("%08X already answers D3DFMT_L8 (50) for 8-bit, nothing to do on this copy",
                 (unsigned)IMMEDIATE_VA);
        log_info("  either a file patcher has been here before, or this executable shipped that "
                 "way. Nothing is wrong; a pristine copy holds 41 and gets corrected above.");
        return;
    }

    if (format != D3DFMT_P8) {
        log_warning("%08X answers %lu for 8-bit, which is neither D3DFMT_P8 (41) nor "
                    "D3DFMT_L8 (50). Leaving it alone; an unrecognised value is more likely a "
                    "different build than a bug this plugin understands.",
                    (unsigned)IMMEDIATE_VA, (unsigned long)format);
        return;
    }

    result = patch_repoint_operand(immediate_site, D3DFMT_P8, D3DFMT_L8);
    if (result != PATCH_RESULT_OK) {
        log_error("could not correct the format constant - %s", patch_result_text(result));
        return;
    }

    log_info("%08X  8-bit format  D3DFMT_P8 (41) -> D3DFMT_L8 (50)", (unsigned)IMMEDIATE_VA);
    log_info("  NVIDIA dropped support for paletted textures, so the 8-bit path was asking for "
             "a format the driver refuses and the game hung on a black screen at load. AMD "
             "cards still accept 41, which is why the bug follows the graphics card.");
}

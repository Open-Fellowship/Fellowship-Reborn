#include "cd_check.h"

#include "common/engine_sites.h"
#include "common/host_image.h"
#include "common/logging.h"
#include "common/patch.h"

#include <windows.h>

#define PLUGIN_SECTION "cd_check"

#define CD_CHECK_CALL_VA 0x00406439u

/* __cdecl and takes no arguments, exactly like the patcher's own stub. The caller cleans up with
 * `add esp,8` immediately afterwards, so the two arguments it pushed are its own problem and this
 * function must not touch them. */
static int __cdecl always_present(void)
{
    return 1;
}

void cd_check_install(void)
{
    uintptr_t      address;
    patch_result_t result;

    log_init(PLUGIN_SECTION, false);

    /* No Enabled key, and do not add one: a key whose only purpose is to disarm a patch
     * invites turning off the one that was working. patch_redirect_call already declines when
     * the call site is not there. See README.md. */
    if (!host_image_resolve()) {
        log_error("the host image could not be resolved; refusing to touch anything");
        return;
    }

    address = exe_site(CD_CHECK_CALL_VA);
    result  = patch_redirect_call(address, (const void *)always_present);
    if (result == PATCH_RESULT_OK) {
        log_info("%08X redirected to a stub returning 1; the callee itself is untouched",
                 (unsigned)address);
    } else {
        log_error("%08X - %s", (unsigned)address, patch_result_text(result));
    }
}

#include "cd_check.h"

#include "common/engine_sites.h"
#include "common/host_image.h"
#include "common/ini.h"
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

    /* Off by default. A No-CD executable does not need it, and that is the common case for this
     * game; turning it on where it is not needed just means one more patch that can be wrong. */
    if (!ini_read_bool(PLUGIN_SECTION, "Enabled", false)) {
        log_info("Enabled=0. Not needed on a No-CD executable, which is the usual case.");
        return;
    }
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

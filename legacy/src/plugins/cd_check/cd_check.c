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

    /* NO SWITCH. This used to carry an `Enabled` key that defaulted to off, on the reasoning that
     * a No-CD executable does not need the patch and one more patch is one more thing that can be
     * wrong. Both halves of that turned out to be the wrong worry.
     *
     * patch_redirect_call verifies the opcode is E8 before it writes, so on a copy where this
     * call is not there any more the plugin declines and says so. There is nothing for a switch
     * to protect against that the validation does not already handle, and a key that exists only
     * to disarm a patch invites turning off the one that was working.
     *
     * A plugin is still switched off the way every plugin is: delete its DLL from plugins\. */
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

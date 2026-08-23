#include "template_plugin.h"

#include "common/engine_types.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"

#include <windows.h>

#define PLUGIN_SECTION "template_plugin"

void template_plugin_install(void)
{
    char greeting[128];

    log_init(PLUGIN_SECTION, false);

    if (!ini_read_bool(PLUGIN_SECTION, "Enabled", true)) {
        log_info("Enabled=0, doing nothing");
        return;
    }

    if (!host_image_resolve()) {
        /* The only honest thing to do. Every offset this project owns assumes a 32-bit PE, so a
         * host we could not identify is a host we must not write to. */
        log_error("the host image could not be resolved; refusing to touch anything");
        return;
    }

    log_info("installed");
    log_info("  host image   %08X .. %08X",
             (unsigned)host_image_base(), (unsigned)host_image_end());
    log_info("  code section %08X + %08X",
             (unsigned)host_image_text(), (unsigned)host_image_text_size());

    /* Worth logging even though it is always the same answer at install time: it is the fact that
     * catches out everybody who tries to patch the rfl from here and cannot work out why the
     * pattern never matches. */
    if (GetModuleHandleA(FELLOWSHIP_RFL_MODULE) == NULL) {
        log_info("  %s not loaded yet, as expected at entry-point time", FELLOWSHIP_RFL_MODULE);
    } else {
        log_info("  %s already at %08X", FELLOWSHIP_RFL_MODULE,
                 (unsigned)(uintptr_t)GetModuleHandleA(FELLOWSHIP_RFL_MODULE));
    }

    ini_read_string(PLUGIN_SECTION, "Greeting", "hello from plugins\\",
                    greeting, sizeof(greeting));
    log_info("  Greeting = %s", greeting);
}

#include "fog_toggle.h"

#include "common/engine_sites.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/patch.h"
#include "common/trampoline.h"

#include <windows.h>

#include <stdint.h>
#include <string.h>

#define PLUGIN_SECTION "fog_toggle"

/* SetFogEnable's first two instructions. Ten bytes, which is more than the five a branch needs,
 * so they are relocated into the stub whole and nothing is left half-overwritten.
 *
 *   0x48BEF0   8B 81 66 01 00 00    mov eax,[ecx+0x166]
 *   0x48BEF6   8B 54 24 04          mov edx,[esp+4]      <- the BOOL the engine passed
 *   0x48BEFA   ...                  first instruction the stub jumps back to
 */
#define FOG_HOOK_VA   0x0048BEF0u
#define FOG_RETURN_VA 0x0048BEFAu
#define FOG_HOOK_SIZE 10u

static const uint8_t fog_hook_expected[FOG_HOOK_SIZE] = {
    0x8B, 0x81, 0x66, 0x01, 0x00, 0x00,
    0x8B, 0x54, 0x24, 0x04
};

/* 1 = the engine's own value is passed through, so the level's fog behaves normally.
 * 0 = the argument is zeroed on its way past, so fog is off. */
static volatile uint8_t g_fog_allowed = 1;

static int      g_toggle_key;
static uint8_t  g_key_was_down;

/* Called from the stub, on the game's own thread, once per SetFogEnable. Edge-detected, so
 * holding the key toggles once rather than every frame. */
static void __cdecl fog_poll(void)
{
    uint8_t down = (GetAsyncKeyState(g_toggle_key) & 0x8000) != 0;

    if (down != g_key_was_down) {
        g_key_was_down = down;
        if (down) {
            g_fog_allowed = (uint8_t)(g_fog_allowed ? 0 : 1);
        }
    }
}

/* The stub, thirty-five bytes:
 *
 *     60                    pushad
 *     9C                    pushfd
 *     E8 rel32              call fog_poll
 *     9D                    popfd
 *     61                    popad
 *     8B 81 66 01 00 00     mov eax,[ecx+0x166]     relocated
 *     8B 54 24 04           mov edx,[esp+4]         relocated
 *     80 3D &flag 00        cmp byte ptr [flag],0
 *     75 02                 jne keep
 *     31 D2                 xor edx,edx             fog off: the argument becomes FALSE
 *   keep:
 *     E9 rel32              jmp 0x48BEFA
 *
 * pushad alone would not do: fog_poll returns with the flags set by whatever it did last, and
 * the relocated instructions are followed by our own cmp/jne. pushfd/popfd keeps the two
 * separate. The stack is back to the engine's own esp before `mov edx,[esp+4]` executes, which
 * is what makes reading the argument at +4 still correct. */
static void *build_stub(uintptr_t return_address)
{
    uint8_t *stub;
    uint8_t  code[35];
    uint32_t value;
    int32_t  displacement;

    stub = (uint8_t *)trampoline_alloc(sizeof(code));
    if (stub == NULL) {
        return NULL;
    }

    code[0] = 0x60;
    code[1] = 0x9C;
    code[2] = 0xE8;
    displacement = (int32_t)((uintptr_t)&fog_poll - ((uintptr_t)stub + 7u));
    memcpy(code + 3, &displacement, sizeof(displacement));
    code[7] = 0x9D;
    code[8] = 0x61;

    memcpy(code + 9, fog_hook_expected, FOG_HOOK_SIZE);   /* the relocated originals */

    code[19] = 0x80;
    code[20] = 0x3D;
    value = (uint32_t)(uintptr_t)&g_fog_allowed;
    memcpy(code + 21, &value, sizeof(value));
    code[25] = 0x00;

    code[26] = 0x75;
    code[27] = 0x02;
    code[28] = 0x31;
    code[29] = 0xD2;

    code[30] = 0xE9;
    displacement = (int32_t)(return_address - ((uintptr_t)stub + sizeof(code)));
    memcpy(code + 31, &displacement, sizeof(displacement));

    memcpy(stub, code, sizeof(code));
    FlushInstructionCache(GetCurrentProcess(), stub, sizeof(code));
    return stub;
}

/* A handful of names, so the ini does not have to carry a virtual-key number. F1..F4 avoid the
 * game's own cheat keys, which the shipped README binds from F5 to F12. */
static int resolve_key(const char *name)
{
    if (_stricmp(name, "F1") == 0) return VK_F1;
    if (_stricmp(name, "F2") == 0) return VK_F2;
    if (_stricmp(name, "F3") == 0) return VK_F3;
    if (_stricmp(name, "F4") == 0) return VK_F4;
    return 0;
}

void fog_toggle_install(void)
{
    char           key_name[32];
    uintptr_t      hook;
    void          *stub;
    patch_result_t result;

    log_init(PLUGIN_SECTION, false);

    if (!ini_read_bool(PLUGIN_SECTION, "Enabled", true)) {
        log_info("Enabled=0, doing nothing");
        return;
    }
    if (!host_image_resolve()) {
        log_error("the host image could not be resolved; refusing to touch anything");
        return;
    }

    ini_read_string(PLUGIN_SECTION, "Key", "F1", key_name, sizeof(key_name));
    g_toggle_key = resolve_key(key_name);
    if (g_toggle_key == 0) {
        log_error("Key=%s is not one of F1 F2 F3 F4 - not installing. The game binds F5 to F12 "
                  "to its own cheats, which is why the choice is narrow.", key_name);
        return;
    }

    g_fog_allowed = (uint8_t)(ini_read_bool(PLUGIN_SECTION, "StartWithFog", true) ? 1 : 0);

    hook = exe_site(FOG_HOOK_VA);
    if (!patch_validate_bytes(hook, fog_hook_expected, FOG_HOOK_SIZE)) {
        log_error("%08X does not hold SetFogEnable's prologue - not installing", (unsigned)hook);
        return;
    }

    stub = build_stub(exe_site(FOG_RETURN_VA));
    if (stub == NULL) {
        log_error("could not allocate the stub");
        return;
    }

    result = patch_write_jump(hook, stub, FOG_HOOK_SIZE);
    if (result != PATCH_RESULT_OK) {
        log_error("could not branch to the stub - %s", patch_result_text(result));
        return;
    }

    log_info("installed: %08X -> stub at %08X, %s toggles fog, starting %s",
             (unsigned)hook, (unsigned)(uintptr_t)stub, key_name,
             g_fog_allowed ? "ON" : "OFF");
}

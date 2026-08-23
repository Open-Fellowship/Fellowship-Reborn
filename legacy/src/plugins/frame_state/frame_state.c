#include "frame_state.h"

#include "common/emit.h"
#include "common/engine_sites.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/trampoline.h"

#include <windows.h>

#include <stdint.h>
#include <string.h>

#define PLUGIN_SECTION "frame_state"

/* The mode word the per-frame function switches on, and the counter the full frame increments. */
#define FRAME_MODE_VA     0x0053EE84u
#define FRAME_COUNTER_VA  0x0054417Cu

#define MODE_SETTER_VA    0x004049F0u
#define SETTER_PROLOGUE   5u

#define POLL_INTERVAL_MS  20u
#define REPORT_INTERVAL   (1000u / POLL_INTERVAL_MS)   /* one summary a second */

static uintptr_t g_mode_address;
static uintptr_t g_counter_address;
static bool      g_watching = true;

/* Named for what it does to the frame rather than for what it is called in the engine, because
 * the second of those is not knowable from here and the first is the whole question. */
static const char *what_it_means(uint32_t mode)
{
    if ((mode & 8u) != 0u) {
        return "the per-frame function RETURNS WITHOUT DRAWING";
    }
    if (mode == 0u) {
        return "before the game has started, no frame work at all";
    }
    return "the full frame, drawing";
}

static void describe(const char *when, uint32_t mode, uint32_t counter)
{
    log_info("%s mode %08X (%s%s%s%s) - %s, engine frame counter %u", when, mode,
             (mode & 1u) ? "1" : "-",
             (mode & 2u) ? "2" : "-",
             (mode & 4u) ? "4 not minimised" : "- MINIMISED",
             (mode & 8u) ? " 8" : "",
             what_it_means(mode), counter);
}

/* ------------------------------------------------------------------- who asked for the change */

static const char *name_of(uint32_t address)
{
    static char text[64];
    uintptr_t   exe  = host_image_base();
    HMODULE     rfl  = GetModuleHandleA("Fellowship.rfl");
    MEMORY_BASIC_INFORMATION region;

    /* The two modules whose offsets mean something to this project get named that way, because
     * "Fellowship.exe+0049F0" can be looked up in the binary and "0x4049F0" cannot once anything
     * relocates. */
    if (exe != 0 && address >= exe && address < exe + 0x200000u) {
        wsprintfA(text, "Fellowship.exe+%06X", (unsigned)(address - exe));
        return text;
    }
    if (rfl != NULL && address >= (uintptr_t)rfl && address < (uintptr_t)rfl + 0x200000u) {
        wsprintfA(text, "Fellowship.rfl+%06X", (unsigned)(address - (uintptr_t)rfl));
        return text;
    }

    memset(&region, 0, sizeof(region));
    if (VirtualQuery((LPCVOID)(uintptr_t)address, &region, sizeof(region)) != 0 &&
        region.AllocationBase != NULL) {
        char path[MAX_PATH];

        if (GetModuleFileNameA((HMODULE)region.AllocationBase, path, sizeof(path)) != 0) {
            const char *name   = path;
            const char *cursor;

            for (cursor = path; *cursor != '\0'; ++cursor) {
                if (*cursor == '\\' || *cursor == '/') {
                    name = cursor + 1;
                }
            }
            wsprintfA(text, "%s+%06X", name,
                      (unsigned)(address - (uintptr_t)region.AllocationBase));
            return text;
        }
    }

    wsprintfA(text, "%08X", (unsigned)address);
    return text;
}

/* Called from the stub with the return address of whoever called the setter, and the mode being
 * asked for. Runs on the game's own thread, so it does as little as it can get away with. */
static void __cdecl on_set_mode(uint32_t caller, uint32_t value)
{
    uint32_t current = 0;

    if (!memory_read_u32(g_mode_address, &current) || current == value) {
        return;
    }

    log_info("mode %08X -> %08X asked for by %s%s", current, value, name_of(caller),
             ((value & 8u) != 0u && (current & 8u) == 0u)
                 ? "   <- this is the one that stops the drawing" : "");
}

static void *build_stub(uintptr_t stub_address, uintptr_t resume_address)
{
    uint8_t buffer[64];
    emit_t  emit;

    emit_init(&emit, buffer, sizeof(buffer));
    emit_u8(&emit, 0x60);                                   /* pushad                       */
    emit_u8(&emit, 0x9C);                                   /* pushfd                       */
    emit_u8(&emit, 0xFF); emit_u8(&emit, 0x74);
    emit_u8(&emit, 0x24); emit_u8(&emit, 0x28);             /* push dword ptr [esp+0x28] mode */
    emit_u8(&emit, 0xFF); emit_u8(&emit, 0x74);
    emit_u8(&emit, 0x24); emit_u8(&emit, 0x28);             /* push dword ptr [esp+0x28] ret  */
    emit_u8(&emit, 0xE8);
    emit_u32(&emit, (uint32_t)((uintptr_t)&on_set_mode - (stub_address + emit_size(&emit) + 4)));
    emit_u8(&emit, 0x83); emit_u8(&emit, 0xC4);
    emit_u8(&emit, 0x08);                                   /* add esp, 8                   */
    emit_u8(&emit, 0x9D);                                   /* popfd                        */
    emit_u8(&emit, 0x61);                                   /* popad                        */

    /* The displaced prologue, run here instead of there. */
    emit_u8(&emit, 0x53);                                   /* push ebx                     */
    emit_u8(&emit, 0x8B); emit_u8(&emit, 0x5C);
    emit_u8(&emit, 0x24); emit_u8(&emit, 0x08);             /* mov ebx, dword ptr [esp+8]   */

    emit_jump_rel32(&emit, stub_address, resume_address);

    if (emit_overflowed(&emit)) {
        return NULL;
    }
    memcpy((void *)stub_address, buffer, emit_size(&emit));
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)stub_address, emit_size(&emit));
    return (void *)stub_address;
}

static void watch_the_setter(void)
{
    static const uint8_t prologue[SETTER_PROLOGUE] = { 0x53, 0x8B, 0x5C, 0x24, 0x08 };

    uintptr_t      setter = exe_site(MODE_SETTER_VA);
    uintptr_t      stub;
    uint8_t        found[SETTER_PROLOGUE];
    patch_result_t result;

    if (!memory_read(setter, found, sizeof(found)) ||
        memcmp(found, prologue, sizeof(found)) != 0) {
        log_warning("%08X does not start with the prologue this expects, not watching the "
                    "setter", (unsigned)setter);
        return;
    }

    stub = (uintptr_t)trampoline_alloc(64);
    if (stub == 0 || build_stub(stub, setter + SETTER_PROLOGUE) == NULL) {
        log_error("the stub could not be built, not watching the setter");
        return;
    }

    result = patch_write_jump(setter, (const void *)stub, SETTER_PROLOGUE);
    if (result != PATCH_RESULT_OK) {
        log_error("%08X - %s", (unsigned)setter, patch_result_text(result));
        return;
    }

    log_info("watching the mode setter at %08X through a stub at %08X, so every change says who "
             "asked for it", (unsigned)setter, (unsigned)stub);
}

static DWORD WINAPI poll_thread(void *unused)
{
    uint32_t last_mode      = 0;
    uint32_t last_counter   = 0;
    uint32_t counter_at_tick = 0;
    bool     have_mode      = false;
    unsigned ticks          = 0;
    unsigned quiet_seconds  = 0;

    (void)unused;

    while (g_watching) {
        uint32_t mode    = 0;
        uint32_t counter = 0;

        Sleep(POLL_INTERVAL_MS);

        if (!memory_read_u32(g_mode_address, &mode) ||
            !memory_read_u32(g_counter_address, &counter)) {
            continue;
        }

        if (!have_mode) {
            have_mode = true;
            last_mode = mode;
            describe("first reading:", mode, counter);
        } else if (mode != last_mode) {
            log_info("mode %08X -> %08X", last_mode, mode);
            describe("  now", mode, counter);
            last_mode = mode;
        }

        /* The counter is the engine's own answer to "did a frame happen", which is a different
         * question from "was one presented" and the difference is where a black screen lives. */
        if (++ticks >= REPORT_INTERVAL) {
            ticks = 0;

            if (counter == counter_at_tick) {
                ++quiet_seconds;
                if (quiet_seconds == 1u || quiet_seconds == 5u || quiet_seconds == 30u) {
                    log_warning("the engine's own frame counter has not moved for %u second(s), "
                                "stuck at %u, mode %08X - %s", quiet_seconds, counter, mode,
                                what_it_means(mode));
                }
            } else {
                if (quiet_seconds >= 1u) {
                    log_info("the frame counter is moving again (%u, +%u)", counter,
                             counter - counter_at_tick);
                }
                quiet_seconds = 0;
            }
            counter_at_tick = counter;
        }

        last_counter = counter;
        (void)last_counter;
    }
    return 0;
}

void frame_state_install(void)
{
    HANDLE thread;

    log_init(PLUGIN_SECTION, false);

    if (!ini_read_bool(PLUGIN_SECTION, "Enabled", false)) {
        log_info("Enabled=0. A diagnostic, not a fix, turn it on when the game is running and "
                 "not drawing.");
        return;
    }
    if (!host_image_resolve()) {
        log_error("the host image could not be resolved; refusing to read anything");
        return;
    }

    g_mode_address    = exe_site(FRAME_MODE_VA);
    g_counter_address = exe_site(FRAME_COUNTER_VA);

    /* The only thing here that writes to the game: five bytes of prologue redirected so that a
     * change of mode can name its author. Off with WatchSetter=0 for anyone who wants a plugin
     * that reads and nothing else. */
    if (ini_read_bool(PLUGIN_SECTION, "WatchSetter", true)) {
        watch_the_setter();
    }

    thread = CreateThread(NULL, 0, poll_thread, NULL, 0, NULL);
    if (thread == NULL) {
        log_error("could not start the polling thread");
        return;
    }
    CloseHandle(thread);

    log_info("watching the frame mode at %08X and the frame counter at %08X, every %u ms",
             (unsigned)g_mode_address, (unsigned)g_counter_address, POLL_INTERVAL_MS);
}

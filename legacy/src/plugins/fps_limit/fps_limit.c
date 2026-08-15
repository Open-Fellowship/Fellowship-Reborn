#include "fps_limit.h"

#include "common/emit.h"
#include "common/engine_sites.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/patch.h"
#include "common/trampoline.h"

#include <windows.h>
#include <mmsystem.h>          /* timeBeginPeriod */

#include <stdint.h>
#include <string.h>

#define PLUGIN_SECTION "fps_limit"

/* The engine's own per-frame call, and the function it reaches. */
#define FRAME_CALL_VA   0x004BCA19u
#define FRAME_TARGET_VA 0x00404630u

enum limiter_mode {
    MODE_SLEEP  = 0,   /* cheapest, coarsest: hands the CPU back and accepts the jitter   */
    MODE_SPIN   = 1,   /* tightest, most expensive: busy-waits the whole gap              */
    MODE_HYBRID = 2    /* sleeps the bulk, spins the tail. The default, and the right one */
};

static LONGLONG g_frequency;
static LONGLONG g_period_ticks;   /* how long one frame should take */
static LONGLONG g_next_frame;     /* the counter value the next frame may begin at */
static int      g_mode = MODE_HYBRID;
static LONGLONG g_spin_margin;    /* hybrid: stop sleeping this far out and spin instead */

static LONGLONG now_ticks(void)
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return counter.QuadPart;
}

/* Runs once per frame, before the engine's own frame function.
 *
 * The resync below is not an optimisation, it is the difference between a limiter and a bug. If
 * the process is suspended - alt-tab, a level load, a breakpoint - the target time falls far into
 * the past, and a limiter that simply keeps adding one period would then run unthrottled for as
 * many frames as it was behind, trying to "catch up" on time that no longer exists. When the gap
 * exceeds a few frames the schedule is abandoned and restarted from now.
 */
static void __cdecl fps_limit_tick(void)
{
    LONGLONG current = now_ticks();

    if (g_next_frame == 0) {
        g_next_frame = current + g_period_ticks;
        return;
    }

    for (;;) {
        LONGLONG remaining = g_next_frame - current;
        if (remaining <= 0) {
            break;
        }
        if (g_mode == MODE_SPIN || remaining <= g_spin_margin) {
            /* Spinning is the only way to hit a sub-millisecond deadline on Windows, and it is
             * genuinely expensive, which is why only the tail of the wait is spent here. */
            YieldProcessor();
        } else {
            DWORD milliseconds = (DWORD)(((remaining - g_spin_margin) * 1000) / g_frequency);
            if (milliseconds > 0) {
                Sleep(milliseconds);
            } else if (g_mode == MODE_SLEEP) {
                break;             /* nothing left worth sleeping for, and we do not spin */
            }
        }
        current = now_ticks();
    }

    g_next_frame += g_period_ticks;

    if (current - g_next_frame > g_period_ticks * 4) {
        g_next_frame = current + g_period_ticks;
    }
}

/* pushad / pushfd / call fps_limit_tick / popfd / popad / jmp <engine frame function>
 *
 * Fourteen bytes. The tail jump is what makes this safe without knowing the callee's calling
 * convention: the stack is exactly as the engine left it, so the original returns to 0x4BCA1E
 * by itself. */
static void *build_stub(uintptr_t stub_address, uintptr_t original)
{
    uint8_t buffer[32];
    emit_t  emit;

    emit_init(&emit, buffer, sizeof(buffer));
    emit_u8(&emit, 0x60);                                     /* pushad */
    emit_u8(&emit, 0x9C);                                     /* pushfd */
    emit_u8(&emit, 0xE8);
    emit_u32(&emit, (uint32_t)((uintptr_t)&fps_limit_tick - (stub_address + emit_size(&emit) + 4)));
    emit_u8(&emit, 0x9D);                                     /* popfd  */
    emit_u8(&emit, 0x61);                                     /* popad  */
    emit_jump_rel32(&emit, stub_address, original);

    if (emit_overflowed(&emit)) {
        return NULL;
    }
    memcpy((void *)stub_address, buffer, emit_size(&emit));
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)stub_address, emit_size(&emit));
    return (void *)stub_address;
}

void fps_limit_install(void)
{
    LARGE_INTEGER  frequency;
    float          target;
    int32_t        mode;
    uintptr_t      call_site;
    uintptr_t      stub_address;
    patch_result_t result;

    log_init(PLUGIN_SECTION, false);

    if (!ini_read_bool(PLUGIN_SECTION, "Enabled", true)) {
        log_info("Enabled=0, the frame rate is left uncapped");
        return;
    }
    if (!host_image_resolve()) {
        log_error("the host image could not be resolved; refusing to touch anything");
        return;
    }

    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart == 0) {
        log_error("no high-resolution timer on this machine - not installing");
        return;
    }
    g_frequency = frequency.QuadPart;

    target = ini_read_float(PLUGIN_SECTION, "MaxFPS", 60.0f);
    if (target < 10.0f || target > 1000.0f) {
        log_warning("MaxFPS=%g is outside 10..1000, using 60", (double)target);
        target = 60.0f;
    }
    g_period_ticks = (LONGLONG)((double)g_frequency / (double)target);

    mode = ini_read_int(PLUGIN_SECTION, "Mode", MODE_HYBRID);
    if (mode < MODE_SLEEP || mode > MODE_HYBRID) {
        log_warning("Mode=%ld is not 0, 1 or 2 - using 2 (hybrid)", (long)mode);
        mode = MODE_HYBRID;
    }
    g_mode = (int)mode;

    /* 1.5 ms. Sleep(1) routinely overshoots by a millisecond or more depending on what else has
     * asked for a finer timer, so the spin has to start further out than the error it is there
     * to absorb. */
    g_spin_margin = (g_frequency * 3) / 2000;
    if (g_mode == MODE_SPIN) {
        g_spin_margin = g_period_ticks;    /* spin the whole gap */
    }

    /* Without this, Sleep(1) can be Sleep(15). Released at process exit, which for a game is the
     * only lifetime that matters. */
    timeBeginPeriod(1);

    call_site = exe_site(FRAME_CALL_VA);

    stub_address = (uintptr_t)trampoline_alloc(32);
    if (stub_address == 0) {
        log_error("could not allocate the stub");
        return;
    }
    if (build_stub(stub_address, exe_site(FRAME_TARGET_VA)) == NULL) {
        log_error("the stub did not fit its buffer - not installing");
        return;
    }

    result = patch_redirect_call(call_site, (const void *)stub_address);
    if (result != PATCH_RESULT_OK) {
        log_error("%08X - %s", (unsigned)call_site, patch_result_text(result));
        return;
    }

    log_info("installed: %08X -> stub at %08X -> %08X",
             (unsigned)call_site, (unsigned)stub_address, (unsigned)exe_site(FRAME_TARGET_VA));
    log_info("  %g fps (%lld ticks/frame at %lld Hz), mode %d (%s)",
             (double)target, (long long)g_period_ticks, (long long)g_frequency, g_mode,
             (g_mode == MODE_SLEEP) ? "sleep" : (g_mode == MODE_SPIN) ? "spin" : "hybrid");
}

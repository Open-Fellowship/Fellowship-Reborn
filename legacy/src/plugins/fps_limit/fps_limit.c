#include "fps_limit.h"

#include "common/channel.h"
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

static DWORD    g_period_milliseconds;   /* the longest single wait this may ever perform */
static unsigned g_resyncs;
static unsigned g_calls;
static LONGLONG g_installed_at;

/* The live target, published by the dev menu's slider and preferred over the ini value while it
 * is there. Same arrangement as field_of_view and the field of view slider: one writer for the
 * engine, one writer for the request, and a plugin whose partner is not installed reads a block
 * nobody ever writes to. */
static channel_block_t *g_channel;
static uint32_t         g_seen_serial;
static float            g_target_fps;
static bool             g_uncapped;

/* A drag of the menu's slider publishes a new value on every frame it moves, and the first
 * version of this logged each one. One sweep of the track wrote about four hundred lines. The
 * rates were all correct and the log was useless, which is the same thing as being wrong.
 *
 * So the change is applied immediately and the LINE waits for the value to stop moving. */
static LONGLONG g_settle_at;      /* when the current value stops counting as still moving */
static bool     g_settle_pending;
#define SETTLE_MS 400

static LONGLONG now_ticks(void)
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return counter.QuadPart;
}

/* One place computes everything that depends on the target, so changing it at run time cannot
 * leave the period and the margin disagreeing with each other. `fps` of 0 means uncapped.
 *
 * The schedule is thrown away rather than adjusted. A target that has just moved says nothing
 * useful about when the next frame is due, and the resync below would have discarded it on the
 * following frame anyway, doing it here means one frame of the old rate instead of two. */
static void apply_target(float fps)
{
    g_target_fps = fps;
    g_uncapped   = (fps <= 0.0f);
    g_next_frame = 0;

    if (g_uncapped) {
        return;
    }

    g_period_ticks        = (LONGLONG)((double)g_frequency / (double)fps);
    g_period_milliseconds = (DWORD)((g_period_ticks * 1000) / g_frequency);
    if (g_period_milliseconds == 0) {
        g_period_milliseconds = 1;
    }

    /* 1.5 ms. Sleep(1) routinely overshoots by a millisecond or more depending on what else has
     * asked for a finer timer, so the spin has to start further out than the error it is there
     * to absorb. */
    g_spin_margin = (g_frequency * 3) / 2000;
    if (g_mode == MODE_SPIN) {
        g_spin_margin = g_period_ticks;    /* spin the whole gap */
    }
}

/* Polled once a frame rather than pushed, because the menu runs on the game's thread inside
 * EndScene and this runs on the same thread at the top of the frame: there is no moment where a
 * push would be cheaper, and a poll needs no agreement about who is allowed to call whom.
 *
 * The serial is the whole test. Reading it is one aligned load, and while it has not moved this
 * costs nothing and the target is left exactly as the ini set it. */
static void poll_target(void)
{
    float value;

    /* The settled value goes in the log, once, however many intermediate ones went past on the
     * way to it. Checked before the serial, because the last change of a drag is followed by no
     * further publishing at all and would otherwise never be reported. */
    if (g_settle_pending && now_ticks() >= g_settle_at) {
        g_settle_pending = false;
        if (g_uncapped) {
            log_info("target: uncapped");
        } else {
            log_info("target: %g fps (%lld ticks/frame)", (double)g_target_fps,
                     (long long)g_period_ticks);
        }
    }

    if (g_channel == NULL || g_channel->frame_target_serial == g_seen_serial) {
        return;
    }
    g_seen_serial = g_channel->frame_target_serial;

    if (!channel_read_frame_target(g_channel, &value)) {
        return;
    }
    if (value == g_target_fps) {
        return;
    }

    /* Applied now, said later. Every further change inside the settle window pushes the deadline
     * out again, so a drag costs one line and a preset button costs one line. */
    apply_target(value);
    g_settle_pending = true;
    g_settle_at      = now_ticks() + (g_frequency * SETTLE_MS) / 1000;
}

/* Said out loud the first few times and then counted in silence. A resync is normal after a level
 * load and is a symptom worth reading when it happens every frame, so the first ones go in the log
 * with the size of the error and the rest do not fill the file. */
static void report_resync(LONGLONG gap)
{
    ++g_resyncs;

    if (g_resyncs <= 3u) {
        LONGLONG elapsed = (g_installed_at != 0)
                               ? ((now_ticks() - g_installed_at) * 1000 / g_frequency) : 0;

        log_info("resync %u: the target was %lld ms %s. The schedule restarts from now. "
                 "(call %u, %lld ms after installing, if those two numbers say this site is "
                 "reached far more often than the frame rate, that is the reason.)",
                 g_resyncs,
                 (long long)((gap < 0 ? -gap : gap) * 1000 / g_frequency),
                 (gap < 0) ? "in the past" : "in the future",
                 g_calls, (long long)elapsed);
    } else if (g_resyncs == 4u) {
        log_info("resync: happening often enough that further ones are counted, not logged");
    }
}

/* Runs once per frame, before the engine's own frame function.
 *
 * The resync is not an optimisation, it is the difference between a limiter and a hang, and it has
 * to look BOTH ways.
 *
 * Behind is the obvious case: the process is suspended, or a level loads, the target falls into
 * the past, and a limiter that keeps adding one period would run unthrottled for as many frames
 * as it was behind, catching up on time that no longer exists.
 *
 * AHEAD is the case that cost a week on a Steam Deck. This hook is one call site, and the engine
 * is under no obligation to reach it exactly once per drawn frame, during start-up it goes round
 * far more often than that, with nothing being presented. Every one of those calls used to add a
 * whole frame period to the target while barely any real time passed, so the schedule ran away
 * into the future, one Sleep grew to several seconds, and the game sat in it with a black screen
 * and a message loop still answering. A limiter can be late. It must never be early by more than
 * a frame, and no single wait here may exceed one period.
 */
static void __cdecl fps_limit_tick(void)
{
    LONGLONG current;
    LONGLONG gap;

    ++g_calls;

    poll_target();
    if (g_uncapped) {
        return;
    }

    current = now_ticks();

    if (g_next_frame == 0) {
        g_next_frame = current + g_period_ticks;
        return;
    }

    gap = g_next_frame - current;                    /* positive: the target is in the future */
    if (gap > g_period_ticks || gap < -(g_period_ticks * 4)) {
        report_resync(gap);
        g_next_frame = current + g_period_ticks;
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
            if (milliseconds > g_period_milliseconds) {
                milliseconds = g_period_milliseconds;   /* one frame, whatever the arithmetic says */
            }
            if (milliseconds > 0) {
                Sleep(milliseconds);
            } else if (g_mode == MODE_SLEEP) {
                break;             /* nothing left worth sleeping for, and we do not spin */
            }
        }
        current = now_ticks();
    }

    g_next_frame += g_period_ticks;
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
        log_error("no high-resolution timer on this machine, not installing");
        return;
    }
    g_frequency = frequency.QuadPart;

    target = ini_read_float(PLUGIN_SECTION, "MaxFPS", 60.0f);
    if (target != 0.0f && (target < 10.0f || target > 1000.0f)) {
        log_warning("MaxFPS=%g is not 0 and not within 10..1000, using 60", (double)target);
        target = 60.0f;
    }

    /* Mode is read before the target, because apply_target() consults it: in spin mode the
     * margin is the whole period rather than the tail of it. */
    mode = ini_read_int(PLUGIN_SECTION, "Mode", MODE_HYBRID);
    if (mode < MODE_SLEEP || mode > MODE_HYBRID) {
        log_warning("Mode=%ld is not 0, 1 or 2, using 2 (hybrid)", (long)mode);
        mode = MODE_HYBRID;
    }
    g_mode = (int)mode;

    apply_target(target);

    /* Opened even when the target is a cap, because the menu may hand us a different one at any
     * point. NULL is not an error: it means nobody to talk to, and the ini value stands. */
    g_channel = channel_open();

    /* Without this, Sleep(1) can be Sleep(15). Released at process exit, which for a game is the
     * only lifetime that matters. */
    timeBeginPeriod(1);
    g_installed_at = now_ticks();

    call_site = exe_site(FRAME_CALL_VA);

    stub_address = (uintptr_t)trampoline_alloc(32);
    if (stub_address == 0) {
        log_error("could not allocate the stub");
        return;
    }
    if (build_stub(stub_address, exe_site(FRAME_TARGET_VA)) == NULL) {
        log_error("the stub did not fit its buffer, not installing");
        return;
    }

    result = patch_redirect_call(call_site, (const void *)stub_address);
    if (result != PATCH_RESULT_OK) {
        log_error("%08X - %s", (unsigned)call_site, patch_result_text(result));
        return;
    }

    log_info("installed: %08X -> stub at %08X -> %08X",
             (unsigned)call_site, (unsigned)stub_address, (unsigned)exe_site(FRAME_TARGET_VA));
    if (g_uncapped) {
        log_info("  uncapped (MaxFPS=0), mode %d; the hook is in place and waits for nothing "
                 "until the menu asks for a rate", g_mode);
    } else {
        log_info("  %g fps (%lld ticks/frame at %lld Hz), mode %d (%s)",
                 (double)target, (long long)g_period_ticks, (long long)g_frequency, g_mode,
                 (g_mode == MODE_SLEEP) ? "sleep" : (g_mode == MODE_SPIN) ? "spin" : "hybrid");
    }
    log_info("  the dev menu's frame rate slider overrides this while the game runs");
}

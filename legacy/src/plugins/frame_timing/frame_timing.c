#include "frame_timing.h"

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

#define PLUGIN_SECTION "frame_timing"

/* The thunk every clock read in the engine goes through, and the fourteen that belong to the
 * Timer class. Only these fourteen move: the other forty-six callers of the thunk are loading
 * timeouts and progress bars that must keep counting in milliseconds. */
#define TICK_THUNK_VA 0x004C12B0u

static const uint32_t g_tick_sites[] = {
    0x0040CF47u,                /* Timer::Reset                        */
    0x0040CFB3u,                /* Timer::Mark                         */
    0x0040CFC7u, 0x0040CFE4u,   /* Timer::Resume                       */
    0x0040D037u, 0x0040D05Au,   /* Timer::Save                         */
    0x0040D0DAu, 0x0040D105u,   /* Timer::Load                         */
    0x0040D156u, 0x0040D17Fu,   /* Timer::Tick, the frame delta itself */
    0x0040D1C3u,                /* Timer::GetFramerate                 */
    0x0040D229u,                /* Timer::SetTimeScale                 */
    0x0040D246u,                /* Timer::GetElapsed                   */
    0x0040D276u                 /* Timer::GetCurrentTime               */
};
#define TICK_SITE_COUNT (sizeof(g_tick_sites) / sizeof(g_tick_sites[0]))

/* The two constants that say what a tick is worth, in both directions.
 *
 * SECONDS_PER_TICK_IMM is the immediate inside the constructor rather than the field it writes:
 *
 *     0040CF1D   C7 46 10 6F 12 83 3A   mov dword ptr [esi+0x10], 0x3A83126F   ; 0.001f
 *
 * Patching the field at 0x0053EE68 directly would mean winning a race with the constructor,
 * which runs from 0x00403CC4 during start-up. Patching the immediate means the constructor
 * installs our value for us, whenever it happens to run. */
#define SECONDS_PER_TICK_IMM 0x0040CF20u
#define TICKS_PER_SECOND_VA  0x0051C774u

/* The Timer instance, and the fields that hold a raw tick. Everything else in the class is a
 * float or a counter and is unit independent. */
#define TIMER_OBJECT_VA    0x0053EE58u
#define TIMER_LAST_TICK    0x00u   /* the previous frame                                   */
#define TIMER_FPS_BASE     0x04u   /* origin for the frame rate sample                     */
#define TIMER_MARK_TICK    0x08u   /* where a pause started                                */
#define TIMER_SEC_PER_TICK 0x10u   /* float, what the constructor writes                   */
#define TIMER_FPS_MARK     0x14u   /* float, seconds at the last sample                    */
#define TIMER_FPS_COUNT    0x18u   /* int, frames until the next sample                    */
#define TIMER_CURRENT      0x1Cu   /* float, accumulated seconds                           */
#define TIMER_TIME_BASE    0x20u   /* float, seconds at the last time-scale change         */
#define TIMER_SCALE_TICK   0x24u   /* origin the accumulator counts from                   */

/* The once-per-frame call this plugin borrows for its watchdog. fps_limit already owns
 * 0x004BCA19, and this has to run between whole frames rather than inside Tick, so it takes the
 * engine's own call to UpdateTime instead. */
#define FRAME_CALL_VA   0x004046CEu
#define FRAME_TARGET_VA 0x00408F00u

/* Rate limits. Below 1 kHz this is not an improvement on GetTickCount, and above 1 MHz the
 * thirty-two bit span is under an hour and a half, which is not a frame rate fix, it is a fault
 * with a delay on it. */
#define RATE_MIN        1000u
#define RATE_MAX     1000000u
#define RATE_DEFAULT  100000u

/* Re-anchor with a quarter of the span still in hand. See the comment on the watchdog. */
#define REANCHOR_AT 0xC0000000u

static LONGLONG  g_qpc_frequency;
static LONGLONG  g_qpc_origin;
static uint32_t  g_rate = RATE_DEFAULT;
static unsigned  g_reanchors;

/* Resolved and checked once at install, not once a frame. The Timer lives in the executable's
 * own .data, which is mapped and writable from the moment the process exists, so there is nothing
 * about it that can become true later, and VirtualQuery on every frame to re-learn that would be
 * a syscall in the one path that has to stay cheap. 0 means the watchdog is not running. */
static uintptr_t g_timer;

/* ---------------------------------------------------------------------------- the counter
 *
 * Called from engine code, in the slot GetTickCount used to occupy, so it obeys the same rules:
 * the result in EAX, EBX ESI EDI EBP left alone, and nothing left on the x87 stack. A C function
 * with no arguments returning uint32_t satisfies all three by construction, and taking no
 * arguments means __stdcall and __cdecl assemble identically; there is no stack to clean either
 * way. That is why there is no stub here, unlike the frame hook below.
 *
 * The arithmetic is done in two halves rather than as (delta * rate) / frequency, because the
 * single-expression form overflows a 64-bit product after a few weeks of uptime on a fast
 * counter. Splitting it costs one extra divide and can never overflow: the remainder is smaller
 * than the frequency, so the second product is bounded by frequency * rate.
 */
static uint32_t __stdcall hires_ticks(void)
{
    LARGE_INTEGER      counter;
    unsigned long long delta;
    unsigned long long whole;
    unsigned long long part;

    if (!QueryPerformanceCounter(&counter)) {
        return GetTickCount();       /* cannot happen on anything this game runs on */
    }

    delta = (unsigned long long)(counter.QuadPart - g_qpc_origin);
    whole = (delta / (unsigned long long)g_qpc_frequency) * (unsigned long long)g_rate;
    part  = ((delta % (unsigned long long)g_qpc_frequency) * (unsigned long long)g_rate)
            / (unsigned long long)g_qpc_frequency;

    return (uint32_t)(whole + part);
}

/* ------------------------------------------------------------------------------ the watchdog
 *
 * A rebase would be pointless, and it is worth saying why because it was the first design.
 *
 * The engine only ever computes `now, stored`, and a common offset subtracted from both is
 * invisible to a difference, modular arithmetic sees to that whether or not either side wraps.
 * So sliding our own output down does not buy a single tick of headroom. What has to fit in
 * thirty-two bits is the SPAN: the oldest origin the Timer is still counting from, which is
 * +0x24, set at start-up, at a level load and at a savegame load. At 100 kHz that span is 11.9
 * hours of one uninterrupted level.
 *
 * So instead of moving our numbers, this moves the engine's origin, exactly the way the engine
 * moves it itself in SetTimeScale at 0x0040D220: carry the accumulated seconds at +0x1C into the
 * time base at +0x20 and start counting again from now. The accumulator comes out at the same
 * value it went in, so nothing on screen can tell it happened.
 *
 * The frame rate sample gets the same treatment. Its countdown is reloaded rather than left
 * alone, so that the next sample is eight frames away instead of zero seconds wide.
 */
static void reanchor(uintptr_t timer, uint32_t now)
{
    float    current = 0.0f;
    float    zero    = 0.0f;
    uint32_t reload  = 8u;

    if (!memory_read(timer + TIMER_CURRENT, &current, sizeof(current))) {
        return;
    }

    memcpy((void *)(timer + TIMER_TIME_BASE),  &current, sizeof(current));
    memcpy((void *)(timer + TIMER_SCALE_TICK), &now,     sizeof(now));

    memcpy((void *)(timer + TIMER_FPS_MARK),   &zero,   sizeof(zero));
    memcpy((void *)(timer + TIMER_FPS_BASE),   &now,    sizeof(now));
    memcpy((void *)(timer + TIMER_FPS_COUNT),  &reload, sizeof(reload));
    memcpy((void *)(timer + TIMER_MARK_TICK),  &now,    sizeof(now));

    ++g_reanchors;
    if (g_reanchors <= 3u) {
        log_info("re-anchored the timer at %08X - %u so far. The engine's accumulated time is "
                 "carried across, so this is not visible in game.", (unsigned)now, g_reanchors);
    }
}

static void __cdecl frame_timing_frame(void)
{
    uint32_t origin;
    uint32_t now;

    if (g_timer == 0) {
        return;
    }

    /* Two loads and a compare, every frame, for the rest of the session. Everything expensive
     * about this check was done once at install. */
    origin = *(const volatile uint32_t *)(g_timer + TIMER_SCALE_TICK);
    now    = hires_ticks();

    if ((uint32_t)(now - origin) >= REANCHOR_AT) {
        reanchor(g_timer, now);
    }
}

/* pushad / pushfd / call frame_timing_frame / popfd / popad / jmp UpdateTime
 *
 * The engine loads ECX with the engine object at 0x004046C9, one instruction before the call
 * being diverted, so every register has to come back exactly as it went in. The tail jump leaves
 * the stack as the engine built it, so UpdateTime returns to 0x004046D3 by itself and its
 * calling convention never has to be known. */
static void *build_stub(uintptr_t stub_address, uintptr_t original)
{
    uint8_t buffer[32];
    emit_t  emit;

    emit_init(&emit, buffer, sizeof(buffer));
    emit_u8(&emit, 0x60);                                     /* pushad */
    emit_u8(&emit, 0x9C);                                     /* pushfd */
    emit_u8(&emit, 0xE8);
    emit_u32(&emit, (uint32_t)((uintptr_t)&frame_timing_frame
                               - (stub_address + emit_size(&emit) + 4)));
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

/* The five bytes a call site must currently hold. rel32 is a difference between two addresses in
 * the same image, so it is the same whether the module sits at its preferred base or not, and
 * can be computed from the preferred addresses alone. */
static void expected_call(uint32_t site_va, uint8_t out[5])
{
    uint32_t relative = TICK_THUNK_VA - (site_va + 5u);

    out[0] = 0xE8;
    out[1] = (uint8_t)(relative & 0xFFu);
    out[2] = (uint8_t)((relative >> 8) & 0xFFu);
    out[3] = (uint8_t)((relative >> 16) & 0xFFu);
    out[4] = (uint8_t)((relative >> 24) & 0xFFu);
}

/* Every site is checked before any site is written.
 *
 * A partial application is worse here than no application at all: the Timer would be reading
 * some of its origins in milliseconds and the rest in hundred-thousandths, and the differences
 * it takes between them would be nonsense rather than merely coarse. So this refuses as a set. */
static bool all_sites_match(void)
{
    size_t index;

    for (index = 0; index < TICK_SITE_COUNT; ++index) {
        uint8_t expected[5];

        expected_call(g_tick_sites[index], expected);
        if (!patch_validate_bytes(exe_site(g_tick_sites[index]), expected, sizeof(expected))) {
            log_error("%08X does not hold a call to the tick thunk, refusing the whole set",
                      (unsigned)g_tick_sites[index]);
            return false;
        }
    }
    return true;
}

void frame_timing_install(void)
{
    static const uint8_t seconds_per_tick_old[4] = { 0x6F, 0x12, 0x83, 0x3A };   /* 0.001f  */
    static const uint8_t ticks_per_second_old[4] = { 0x00, 0x00, 0x7A, 0x44 };   /* 1000.0f */

    LARGE_INTEGER  frequency;
    LARGE_INTEGER  origin;
    int32_t        configured;
    float          seconds_per_tick;
    float          ticks_per_second;
    uint8_t        replacement[4];
    uintptr_t      stub_address;
    patch_result_t result;
    size_t         index;

    log_init(PLUGIN_SECTION, false);

    if (!ini_read_bool(PLUGIN_SECTION, "Enabled", true)) {
        log_info("Enabled=0, the engine keeps GetTickCount as its frame clock");
        return;
    }
    if (!host_image_resolve()) {
        log_error("the host image could not be resolved; refusing to touch anything");
        return;
    }
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0
        || !QueryPerformanceCounter(&origin)) {
        log_error("no high-resolution counter on this machine, not installing");
        return;
    }

    configured = ini_read_int(PLUGIN_SECTION, "TickRate", (int32_t)RATE_DEFAULT);
    if (configured < (int32_t)RATE_MIN || configured > (int32_t)RATE_MAX) {
        log_warning("TickRate=%ld is outside %u..%u, using %u",
                    (long)configured, RATE_MIN, RATE_MAX, RATE_DEFAULT);
        configured = (int32_t)RATE_DEFAULT;
    }
    g_rate          = (uint32_t)configured;
    g_qpc_frequency = frequency.QuadPart;
    g_qpc_origin    = origin.QuadPart;

    if (!all_sites_match()) {
        return;
    }

    /* The two constants first. If one of them fails the call sites are still untouched and the
     * engine is exactly as it was, which is the only ordering where a failure halfway leaves a
     * game that still runs correctly. */
    seconds_per_tick = 1.0f / (float)g_rate;
    memcpy(replacement, &seconds_per_tick, sizeof(replacement));
    result = patch_write_expect(exe_site(SECONDS_PER_TICK_IMM), seconds_per_tick_old,
                                replacement, sizeof(replacement));
    if (result != PATCH_RESULT_OK) {
        log_error("%08X (ticks to seconds) - %s", SECONDS_PER_TICK_IMM,
                  patch_result_text(result));
        return;
    }

    ticks_per_second = (float)g_rate;
    memcpy(replacement, &ticks_per_second, sizeof(replacement));
    result = patch_write_expect(exe_site(TICKS_PER_SECOND_VA), ticks_per_second_old,
                                replacement, sizeof(replacement));
    if (result != PATCH_RESULT_OK) {
        log_error("%08X (seconds to ticks) - %s", TICKS_PER_SECOND_VA,
                  patch_result_text(result));
        return;
    }

    for (index = 0; index < TICK_SITE_COUNT; ++index) {
        result = patch_redirect_call(exe_site(g_tick_sites[index]),
                                     (const void *)(uintptr_t)&hires_ticks);
        if (result != PATCH_RESULT_OK) {
            /* Validated as a set above, so reaching here means something changed underneath us
             * between the check and the write. Say which one and stop; the log then names the
             * exact site that has to be looked at. */
            log_error("%08X - %s. %u of %u sites were already moved and the engine is now "
                      "inconsistent; restart the game.",
                      (unsigned)g_tick_sites[index], patch_result_text(result),
                      (unsigned)index, (unsigned)TICK_SITE_COUNT);
            return;
        }
    }

    /* ---- the watchdog.
     *
     * A convenience, not a correctness requirement: without it the game runs perfectly until the
     * span runs out. So every failure below is a warning and the counter stays.
     *
     * The Timer's own memory is checked here and never again. It is the executable's .data, which
     * is mapped and writable for the life of the process, so there is nothing about it that can
     * become true later, and making it writable now means the re-anchor, which happens once in
     * nine hours, is not the thing that discovers a protection problem. */
    stub_address = (uintptr_t)trampoline_alloc(32);
    if (stub_address == 0 || build_stub(stub_address, exe_site(FRAME_TARGET_VA)) == NULL) {
        log_warning("could not build the watchdog stub; the counter is installed, but a single "
                    "level played for more than %.1f hours will jump",
                    (double)0xFFFFFFFFu / (double)g_rate / 3600.0);
    } else if (!memory_is_readable_range(exe_site(TIMER_OBJECT_VA), TIMER_SCALE_TICK + 4u)
               || !memory_make_writable(exe_site(TIMER_OBJECT_VA), TIMER_SCALE_TICK + 4u)) {
        log_warning("the timer at %08X is not readable and writable; the counter is installed "
                    "without the watchdog", TIMER_OBJECT_VA);
    } else {
        result = patch_redirect_call(exe_site(FRAME_CALL_VA), (const void *)stub_address);
        if (result != PATCH_RESULT_OK) {
            log_warning("%08X (watchdog) - %s. The counter is installed without it.",
                        FRAME_CALL_VA, patch_result_text(result));
        } else {
            /* Set last, and only on the path where everything worked. The frame function reads
             * this and does nothing while it is zero, so a half-installed watchdog never runs. */
            g_timer = exe_site(TIMER_OBJECT_VA);
        }
    }

    log_info("frame clock moved from GetTickCount to QueryPerformanceCounter");
    log_info("  %u ticks per second (%g s per tick), counter runs at %lld Hz",
             g_rate, (double)seconds_per_tick, (long long)g_qpc_frequency);
    log_info("  %u call sites redirected, 0040CF20 and 0051C774 rewritten",
             (unsigned)TICK_SITE_COUNT);
    log_info("  span before the timer re-anchors itself: %.1f hours",
             (double)REANCHOR_AT / (double)g_rate / 3600.0);
}

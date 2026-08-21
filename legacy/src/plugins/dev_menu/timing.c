#include "timing.h"

#include "common/engine_sites.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"

#include <windows.h>

#include <math.h>
#include <stdint.h>
#include <string.h>

/* fps_limit's section, not ours. See the note at the top of timing.h. */
#define LIMIT_SECTION "fps_limit"

/* Four seconds at sixty. Long enough that a single hitch does not dominate the window and short
 * enough that the numbers still answer "what is it doing now" rather than "what did it do at
 * some point since the level loaded". */
#define WINDOW 240u

static channel_block_t *g_channel;
static float            g_target;        /* 0 = uncapped */
static float            g_saved_target;  /* what the ini says, so the button can dim */

static float    g_window[WINDOW];
static unsigned g_next;
static unsigned g_filled;

void timing_init(channel_block_t *channel)
{
    g_channel = channel;

    g_saved_target = ini_read_float(LIMIT_SECTION, "MaxFPS", 60.0f);
    if (g_saved_target != 0.0f
        && (g_saved_target < TIMING_FPS_LOW || g_saved_target > TIMING_FPS_HIGH)) {
        /* Outside what the slider can reach. Kept as the saved value so the button still reports
         * honestly, but the slider starts somewhere it can actually sit. */
        g_target = 60.0f;
    } else {
        g_target = g_saved_target;
    }
}

/* The engine rewrites this at the top of every frame from Timer::Tick, so sampling once per
 * EndScene gives exactly one reading per frame and no interpolation is needed or wanted. */
void timing_sample(void)
{
    float delta = 0.0f;

    if (!host_image_resolve()) {
        return;
    }
    if (!memory_read(exe_site(EXE_FRAME_DELTA), &delta, sizeof(delta))) {
        return;
    }

    /* A NaN would poison every average taken afterwards, and the exponent test is the only check
     * that catches one without depending on which C dialect this is compiled as - a comparison
     * against a NaN quietly answers false to everything. Same test camera.c settles on. */
    {
        uint32_t bits;
        memcpy(&bits, &delta, sizeof(bits));
        if (((bits >> 23) & 0xFFu) == 0xFFu) {
            return;
        }
    }
    if (delta <= 0.0f || delta > 10.0f) {
        return;
    }

    g_window[g_next] = delta * 1000.0f;
    g_next           = (g_next + 1u) % WINDOW;
    if (g_filled < WINDOW) {
        ++g_filled;
    }
}

float timing_target(void)
{
    return g_target;
}

void timing_set_target(float fps)
{
    g_target = fps;
    channel_publish_frame_target(g_channel, fps);
}

bool timing_saved(void)
{
    return (float)fabs((double)(g_target - g_saved_target)) < 0.5f;
}

bool timing_save(void)
{
    /* Whole frames per second. A rate is chosen off a slider by eye and nobody wants 73.4 in
     * their configuration file, so the value that goes in is the value the readout shows. */
    if (!ini_write_float(LIMIT_SECTION, "MaxFPS", g_target, 0)) {
        log_warning("could not write MaxFPS to %s", ini_path());
        return false;
    }
    g_saved_target = g_target;
    log_info("saved MaxFPS=%g to %s", (double)g_target, ini_path());
    return true;
}

bool timing_stats(float *lowest_ms, float *average_ms, float *highest_ms, unsigned *frames)
{
    float    lowest;
    float    highest;
    double   total = 0.0;
    unsigned index;

    if (g_filled < 8u) {
        return false;
    }

    lowest  = g_window[0];
    highest = g_window[0];
    for (index = 0; index < g_filled; ++index) {
        float value = g_window[index];

        if (value < lowest)  { lowest  = value; }
        if (value > highest) { highest = value; }
        total += (double)value;
    }

    if (lowest_ms   != NULL) { *lowest_ms   = lowest; }
    if (highest_ms  != NULL) { *highest_ms  = highest; }
    if (average_ms  != NULL) { *average_ms  = (float)(total / (double)g_filled); }
    if (frames      != NULL) { *frames      = g_filled; }
    return true;
}

float timing_engine_fps(void)
{
    float value = 0.0f;

    if (!host_image_resolve()) {
        return 0.0f;
    }
    if (!memory_read(exe_site(EXE_FRAME_FPS), &value, sizeof(value))) {
        return 0.0f;
    }
    if (!(value > 0.0f && value < 100000.0f)) {
        return 0.0f;
    }
    return value;
}

unsigned timing_tick_rate(void)
{
    float seconds_per_tick = 0.0f;

    if (!host_image_resolve()) {
        return 0u;
    }
    if (!memory_read(exe_site(EXE_TIMER_OBJECT) + TIMER_SECONDS_PER_TICK,
                     &seconds_per_tick, sizeof(seconds_per_tick))) {
        return 0u;
    }
    if (!(seconds_per_tick > 0.0f && seconds_per_tick <= 1.0f)) {
        return 0u;   /* the Timer has not been constructed yet, or this is not it */
    }
    return (unsigned)((1.0f / seconds_per_tick) + 0.5f);
}

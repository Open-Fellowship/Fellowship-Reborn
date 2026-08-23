#include "timing.h"

#include "common/engine_sites.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"

#include <windows.h>

#include <math.h>

/* fps_limit's section, not ours. See the note at the top of timing.h. */
#define LIMIT_SECTION "fps_limit"

static channel_block_t *g_channel;
static float            g_target;        /* 0 = uncapped */
static float            g_saved_target;  /* what the ini says, so the button can dim */

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

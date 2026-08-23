/* timing.h: the frame rate control on the fix enhancers page.
 *
 * Publishes a target to fps_limit over the channel, and writes it into the ini on request. It
 * writes [fps_limit], another plugin's section, which is the menu acting as the player rather
 * than as a plugin minding its own business.
 *
 * It reads the frame rate and the Timer's ticks-to-seconds constant back out of the engine, so
 * the page reports what is true instead of asserting what was asked for. See README.md.
 */
#ifndef DEV_MENU_TIMING_H
#define DEV_MENU_TIMING_H

#include "common/channel.h"

#include <stdbool.h>

/* The band the slider covers. 0 is not on the track, uncapped is its own button, because a
 * slider that means "off" at one end is a slider you turn off by accident. */
#define TIMING_FPS_LOW   30.0f
#define TIMING_FPS_HIGH 300.0f

/* Reads [fps_limit] MaxFPS for the slider's starting position. `channel` may be NULL, in which
 * case the slider still moves and nothing is listening, which is what happens when fps_limit is
 * not installed. */
void timing_init(channel_block_t *channel);

/* 0 means uncapped. */
float timing_target(void);
void  timing_set_target(float fps);

/* True when the current target is what the ini already says, so the save button can say whether
 * it would do anything before it is pressed. */
bool timing_saved(void);

/* Writes [fps_limit] MaxFPS. False means the file was not written and nothing should claim it
 * was. */
bool timing_save(void);

/* What the engine's own counter says, resampled every eight frames. */
float timing_engine_fps(void);

/* Ticks per second the Timer is counting in: 1000 for the engine's own GetTickCount, or whatever
 * frame_timing set. 0 when the constant could not be read at all. */
unsigned timing_tick_rate(void);

#endif /* DEV_MENU_TIMING_H */

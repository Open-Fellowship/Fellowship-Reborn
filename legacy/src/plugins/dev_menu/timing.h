/* timing.h: the frame rate control on the fix enhancers page, and the readout that proves it.
 *
 * TWO HALVES, AND THE SECOND ONE IS THE POINT.
 *
 * The control is small: a target rate that gets published to fps_limit over the channel, and a
 * button that writes it into the ini so the next launch starts there. It writes [fps_limit],
 * which is another plugin's section, and that is deliberate rather than an oversight of the rule
 * in ini.h - the menu is the player acting, not a plugin minding its own business.
 *
 * The readout is here because "smooth as silk" is not something to hand someone and ask whether
 * it feels better. What the engine actually simulates is one float at 0x00543284, read by every
 * animation, physics and effect site in the game, and this samples it every frame and shows what
 * it has been doing. On a stock clock the numbers are unmistakable: the delta is quantised to
 * the 15.6 ms system tick, so at a 60 fps cap the two rates beat at 4 Hz and the window shows a
 * minimum of 0 ms against a maximum of 31. With frame_timing installed the same window collapses
 * to a fraction of a millisecond either side of the target.
 *
 * It also reads the Timer's own ticks-to-seconds constant, so the page can say which clock is
 * running without asking the plugin that changed it, and without believing anything about load
 * order.
 */
#ifndef DEV_MENU_TIMING_H
#define DEV_MENU_TIMING_H

#include "common/channel.h"

#include <stdbool.h>

/* The band the slider covers. 0 is not on the track - uncapped is its own button, because a
 * slider that means "off" at one end is a slider you turn off by accident. */
#define TIMING_FPS_LOW   30.0f
#define TIMING_FPS_HIGH 300.0f

/* Reads [fps_limit] MaxFPS for the slider's starting position. `channel` may be NULL, in which
 * case the slider still moves and nothing is listening, which is what happens when fps_limit is
 * not installed. */
void timing_init(channel_block_t *channel);

/* Once per frame, from the EndScene hook, whether or not the menu is open. The window has to be
 * a picture of the game running, not of the game running with a menu over it. */
void timing_sample(void);

/* 0 means uncapped. */
float timing_target(void);
void  timing_set_target(float fps);

/* True when the current target is what the ini already says, so the save button can say whether
 * it would do anything before it is pressed. */
bool timing_saved(void);

/* Writes [fps_limit] MaxFPS. False means the file was not written and nothing should claim it
 * was. */
bool timing_save(void);

/* Statistics over the last few seconds of frames, in milliseconds of engine delta. False before
 * enough frames have gone by to say anything. */
bool timing_stats(float *lowest_ms, float *average_ms, float *highest_ms, unsigned *frames);

/* What the engine's own counter says, resampled every eight frames. */
float timing_engine_fps(void);

/* Ticks per second the Timer is counting in: 1000 for the engine's own GetTickCount, or whatever
 * frame_timing set. 0 when the constant could not be read at all. */
unsigned timing_tick_rate(void);

#endif /* DEV_MENU_TIMING_H */

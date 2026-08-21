/* timing.h: the frame rate control on the fix enhancers page.
 *
 * A target rate that gets published to fps_limit over the channel, and a button that writes it
 * into the ini so the next launch starts there. It writes [fps_limit], which is another plugin's
 * section, and that is deliberate rather than an oversight of the rule in ini.h - the menu is the
 * player acting, not a plugin minding its own business.
 *
 * It reads two numbers back out of the engine so the page can report rather than assert: the
 * frame rate the engine's own counter arrived at, and the Timer's ticks-to-seconds constant,
 * which says which clock is running without asking the plugin that changed it and without
 * believing anything about load order.
 *
 * THERE WAS A THIRD THING HERE. A 240-frame ring of the delta at 0x00543284, shown as low, mean,
 * high and a spread percentage, which is how frame_timing was demonstrated in the first place:
 * on a stock clock at a 60 fps cap it read 0 ms against 31 ms because the two rates beat at 4 Hz,
 * and afterwards it collapsed to a hundredth of a millisecond either side of the target. It came
 * out once the fix was proven, because a per-frame sample that nothing reads is a VirtualQuery
 * every frame for a number nobody looks at. If it is ever wanted again, measuring from OUTSIDE
 * the process is the better shape anyway: the same address, read against the frame counter at
 * 0x0054417C so there is exactly one sample per frame and no aliasing.
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

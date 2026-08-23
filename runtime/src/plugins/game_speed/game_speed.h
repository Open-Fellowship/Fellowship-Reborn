/* game_speed.h: the floor the engine puts under a frame delta.
 *
 * Ported from the community patcher ([Options] FixGameSpeedTiedToFPS). One constant at
 * 0x51C764, 0.002 becoming 0.0001. It is NOT a fixed timestep; this engine has none. It is the
 * lower clamp on the measured frame delta, and lowering it stops the engine inventing time it
 * did not spend.
 *
 * frame_timing removes the cause this treats. Both are useful, and README.md says why.
 */
#ifndef GAME_SPEED_H
#define GAME_SPEED_H

void game_speed_install(void);

#endif /* GAME_SPEED_H */

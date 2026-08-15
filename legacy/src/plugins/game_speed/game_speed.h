/* game_speed.h: the simulation's fixed timestep, which is too coarse at modern frame rates.
 *
 * Ported from the community patcher ([Options] FixGameSpeedTiedToFPS). It is one constant:
 *
 *     0x51C764   0.002   ->   0.0001
 *
 * A twentyfold finer step. The name the patcher gives it is the symptom rather than the cause -
 * with a step this coarse relative to the frame time, how much simulation happens per frame
 * depends on the frame rate, so the game runs at a different speed on different hardware.
 */
#ifndef GAME_SPEED_H
#define GAME_SPEED_H

void game_speed_install(void);

#endif /* GAME_SPEED_H */

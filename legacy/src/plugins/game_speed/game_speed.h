/* game_speed.h: the floor the engine puts under a frame delta.
 *
 * Ported from the community patcher ([Options] FixGameSpeedTiedToFPS). It is one constant:
 *
 *     0x51C764   0.002   ->   0.0001
 *
 * WHAT THAT CONSTANT ACTUALLY IS. This header used to call it "the simulation's fixed timestep".
 * It is not one, and there is no fixed timestep anywhere in this engine: every consumer of frame
 * time multiplies by the delta at 0x00543284 directly, plain Euler, thirty-one call sites of
 * `v -= g*dt` and `p += v*dt`. 0x51C764 is the lower clamp applied to that delta once per frame:
 *
 *     00408F3C   fld   dword ptr [0x51C764]      ; 0.002
 *     00408F42   fcomp st(1)                     ; against the measured delta
 *     00408F4F   fld   dword ptr [0x51C764]      ; taken when the measurement was smaller
 *     00408F55   fst   dword ptr [edi]           ; -> 0x00543284
 *
 * WHY LOWERING IT FIXES THE SPEED. The engine reads its clock with GetTickCount, which cannot
 * see a frame shorter than about 15.6 ms, so above roughly 64 fps most frames measure ZERO
 * milliseconds. The floor then hands the simulation 2 ms that did not happen, and it does it for
 * most of the frames drawn. The game gains time, which is what "game speed tied to FPS" is. At
 * 0.0001 the invention is twenty times smaller and the symptom goes away.
 *
 * WHY THIS IS STILL A PATCH AND NOT THE FIX. It treats the consequence. frame_timing removes the
 * cause by giving the Timer a counter that can measure a frame, after which the delta is real and
 * this floor almost never fires. Both are worth having: with a real clock the floor is what still
 * catches a frame genuinely faster than it.
 */
#ifndef GAME_SPEED_H
#define GAME_SPEED_H

void game_speed_install(void);

#endif /* GAME_SPEED_H */

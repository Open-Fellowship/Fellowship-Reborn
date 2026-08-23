/* fps_limit.h: cap the frame rate.
 *
 * Ported from the community patcher ([Options] EnableFPSLimiter / FPSLimiterMaxFPS /
 * FPSLimiterMode). The HOOK is the part that was ported: the call at 0x4BCA19 is diverted and
 * then TAIL-JUMPS to 0x404630, which leaves the stack as the engine built it, so the original
 * returns to 0x4BCA1E by itself and its calling convention never has to be known.
 *
 * MaxFPS=0 means uncapped: the hook stays installed and waits for nothing, so the dev menu can
 * still hand it a rate later. README.md says why this, frame_timing and game_speed are three
 * different fixes.
 */
#ifndef FPS_LIMIT_H
#define FPS_LIMIT_H

void fps_limit_install(void);

#endif /* FPS_LIMIT_H */

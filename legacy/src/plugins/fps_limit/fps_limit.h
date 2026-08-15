/* fps_limit.h: cap the frame rate.
 *
 * Ported from the community patcher ([Options] EnableFPSLimiter / FPSLimiterMaxFPS /
 * FPSLimiterMode) - the HOOK is ported, the limiter itself is written here rather than
 * transcribed, and that distinction is deliberate. What was worth copying is where he put it:
 *
 *     0x4BCA19   call 0x404630        once per frame
 *
 * His hook diverts that call's displacement to a function of his own, does the waiting, and then
 * TAIL-JUMPS to 0x404630 rather than calling it. That is the detail worth keeping. A jmp leaves
 * the stack exactly as the engine built it, so the original function returns straight to
 * 0x4BCA1E and neither its calling convention nor its argument count has to be known. Detouring
 * its prologue instead would have meant knowing both.
 *
 * WHY A LIMITER AT ALL, given game_speed exists. They are different problems. game_speed makes
 * the simulation step fine enough that the physics stop depending on frame time. This stops the
 * frame time from being 800 microseconds in the first place, which is what makes an uncapped
 * 2002 engine spin a modern GPU at full power to draw the Shire twelve hundred times a second.
 */
#ifndef FPS_LIMIT_H
#define FPS_LIMIT_H

void fps_limit_install(void);

#endif /* FPS_LIMIT_H */

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
 * WHY A LIMITER AT ALL, given the other two timing plugins. Three different problems.
 *
 *   frame_timing  gives the engine a clock fine enough to measure a frame with at all. Without
 *                 it the delta is quantised to 15.6 ms and no cap can be smooth.
 *   game_speed    lowers the floor the engine applies to that delta, so a frame faster than the
 *                 floor stops being reported as slower than it was.
 *   fps_limit     stops the frame time being 800 microseconds in the first place, which is what
 *                 makes an uncapped 2002 engine spin a modern GPU at full power to draw the
 *                 Shire twelve hundred times a second.
 *
 * MaxFPS=0 means uncapped: the hook stays installed and waits for nothing, so the dev menu can
 * still hand it a rate later. The menu's slider is preferred over the ini value whenever one has
 * been published, exactly as field_of_view prefers the menu's field of view.
 */
#ifndef FPS_LIMIT_H
#define FPS_LIMIT_H

void fps_limit_install(void);

#endif /* FPS_LIMIT_H */

/* frame_state.h: what the engine thinks it is doing, once per poll, in the log.
 *
 * A DIAGNOSTIC. It writes nothing to the game.
 *
 * There is a mode word at 0x53EE84 and the whole per-frame function is a switch on it:
 *
 *     00404630  mov  eax,[0x53EE84]
 *     00404638  test al,8
 *     0040463B  je   0x404688        bit 3 clear: carry on into the real work
 *               ... the bit 3 path updates a few subsystems and RETURNS WITHOUT DRAWING
 *     00404688  test eax,eax
 *     0040468A  jne  0x4046C9        non-zero: the full frame, which increments 0x54417C
 *               ... zero: the pre-start path
 *
 * So "the game is running but nothing is being presented" has an answer inside the process that
 * no amount of looking at Direct3D can give: which of those three branches the engine is taking,
 * and whether its own frame counter is moving. This plugin reads those two numbers on a thread of
 * its own and writes down every change.
 *
 * What the bits are known to mean, from the code that writes them:
 *
 *   bit 2 (4)  the window is not minimised. WM_SIZE with SIZE_MINIMIZED clears it at 0x4BCD27
 *              and SIZE_RESTORED sets it again at 0x4BCD74.
 *   bit 1 (2)  entering it runs a one-off setup in the setter at 0x4049F0.
 *   bit 3 (8)  the per-frame function returns without drawing. This is the interesting one.
 */
#ifndef FRAME_STATE_H
#define FRAME_STATE_H

void frame_state_install(void);

#endif /* FRAME_STATE_H */

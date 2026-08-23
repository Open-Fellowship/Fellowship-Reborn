/* frame_state.h: what the engine thinks it is doing, once per poll, in the log.
 *
 * A DIAGNOSTIC. It reads two engine globals, the mode word at 0x53EE84 and the frame counter at
 * 0x54417C, and writes down every change. The mode word is what decides whether the per-frame
 * function draws at all. See README.md.
 */
#ifndef FRAME_STATE_H
#define FRAME_STATE_H

void frame_state_install(void);

#endif /* FRAME_STATE_H */

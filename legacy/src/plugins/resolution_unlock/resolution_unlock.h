/* resolution_unlock.h: let the options screen offer every display mode the card reports.
 *
 * Ported from the community patcher (Fellowship.dll, [Options] UnlockResolutions), decoded from
 * its own installer table rather than guessed at: it writes four values, and all four sit inside
 * the routine at 0x4BC4xx-0x4BC6xx that filters the DirectDraw mode list into the list the
 * options screen shows.
 *
 * Three of them neutralise a rejection branch WITHOUT changing its length, by zeroing the branch
 * displacement rather than the opcode; a `jne +0x83` becomes `jne +0`, which falls through to
 * the next instruction. That is a neater trick than NOPping the instruction and it is worth
 * keeping rather than "improving": the instruction boundary is untouched, so nothing downstream
 * can shift.
 *
 * The 640x480 minimum the engine checks a few instructions later (`cmp edx,0x280` /
 * `cmp ebx,0x1E0`) is deliberately left alone. Modes below that were rejected for a reason.
 */
#ifndef RESOLUTION_UNLOCK_H
#define RESOLUTION_UNLOCK_H

void resolution_unlock_install(void);

#endif /* RESOLUTION_UNLOCK_H */

/* resolution_unlock.h: let the options screen offer every display mode the card reports.
 *
 * Ported from the community patcher (Fellowship.dll, [Options] UnlockResolutions), decoded from
 * its own installer table. Three writes inside the mode-list filter at 0x4BC4xx-0x4BC6xx, two of
 * them zeroing a branch displacement instead of its opcode so no instruction boundary moves.
 * Do not "tidy" those into NOPs. The 640x480 minimum is left alone. See README.md.
 */
#ifndef RESOLUTION_UNLOCK_H
#define RESOLUTION_UNLOCK_H

void resolution_unlock_install(void);

#endif /* RESOLUTION_UNLOCK_H */

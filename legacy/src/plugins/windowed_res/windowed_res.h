/* windowed_res.h: choose the size of the window the game opens in.
 *
 * Ported from the community patcher ([Options] ForceCustomWindowedRes / CustomWindowedResWidth /
 * CustomWindowedResHeight).
 *
 * IT CONFLICTS WITH resolution_unlock BY DESIGN, and the patcher has the same conflict: two of
 * the five values it writes put back the exact bytes resolution_unlock changes at 0x4BC4FF. The
 * patcher gets away with it by applying them in order in one function. Here they are two DLLs,
 * and the loader loads alphabetically, so `windowed_res` lands after `resolution_unlock` and wins
 *, the same outcome, reached less obviously. Rather than rely on that, this plugin says so in
 * the log when it overwrites the other one's work.
 */
#ifndef WINDOWED_RES_H
#define WINDOWED_RES_H

void windowed_res_install(void);

#endif /* WINDOWED_RES_H */

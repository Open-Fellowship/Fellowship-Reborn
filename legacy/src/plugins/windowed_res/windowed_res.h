/* windowed_res.h: choose the size of the window the game opens in.
 *
 * IT CONFLICTS WITH resolution_unlock BY DESIGN: two of the values it writes put back the exact
 * bytes resolution_unlock changes at 0x4BC4FF. The loader loads alphabetically, so this lands
 * second and wins. That is relied on nowhere; the plugin checks and says so in the log when it
 * overwrites the other one's work. See README.md.
 */
#ifndef WINDOWED_RES_H
#define WINDOWED_RES_H

void windowed_res_install(void);

#endif /* WINDOWED_RES_H */

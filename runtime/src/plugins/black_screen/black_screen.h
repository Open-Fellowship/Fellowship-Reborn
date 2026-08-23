/* black_screen.h: the 8-bit texture format the renderer asks the driver for.
 *
 * A stock build answers D3DFMT_P8 (41), an 8-bit paletted format NVIDIA no longer supports, so
 * the texture is never created and the game hangs on a black screen at load. This corrects the
 * constant at 0x43D2BC to D3DFMT_L8 (50), and only on a build that actually holds the broken
 * value.
 *
 * The site, the evidence that fixes the enum, and why this is a guard and not a fix on the
 * development machine, are in README.md.
 */
#ifndef BLACK_SCREEN_H
#define BLACK_SCREEN_H

void black_screen_install(void);

#endif /* BLACK_SCREEN_H */

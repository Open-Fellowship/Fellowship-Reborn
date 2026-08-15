/* black_screen.h: the 8-bit texture format the renderer asks the driver for.
 *
 * Ported from the community patcher, where the option is a single dword write - Fellowship.dll
 * pushes 0x43D2BC and the value 0x32 - and where, on the build this project targets, it changes
 * nothing at all. See the README for the measurement. This plugin exists as a GUARD: it reads
 * the value first, corrects it only when a build actually holds the broken one, and says which
 * of those two happened.
 *
 * The site is a bit depth to D3DFORMAT mapper:
 *
 *     0043D2B0   mov  ecx,[esp+4]        the bit depth
 *     0043D2B4   xor  eax,eax
 *     0043D2B6   cmp  ecx,8
 *     0043D2B9   jne  0x43D2C1
 *     0043D2BB   mov  eax,0x32           <- the 8-bit answer, 50 = D3DFMT_L8
 *     0043D2C0   ret
 *     0043D2C1   cmp  ecx,0x10           16-bit: 23, 24, 25, 26, 29
 *
 * Those 16-bit answers are R5G6B5, X1R5G5B5, A1R5G5B5, A4R4G4B4 and A8R3G3B2, which is what
 * fixes the enum as D3DFORMAT beyond argument.
 *
 * D3DFMT_P8 is 41: an 8-bit PALETTED texture. NVIDIA dropped support for it; AMD still carries
 * it. So a stock game on an NVIDIA card asks for a format the driver will not give it, fails to
 * create the texture, and hangs on a black screen at load - while the identical game on an AMD
 * card starts fine. That is the bug the patcher's option is named after, and it is why it is a
 * hardware-dependent one rather than a Windows-version one.
 *
 * D3DFMT_L8, 50, is 8-bit luminance, and every driver supports it.
 *
 * WHY THIS IS A GUARD AND NOT A FIX HERE: the executable this project is built against already
 * answers 50. It has been through a file patcher at some point, and every copy and backup on the
 * development machine descends from that one. A pristine install still holds 41, which is what
 * this plugin is for.
 */
#ifndef BLACK_SCREEN_H
#define BLACK_SCREEN_H

void black_screen_install(void);

#endif /* BLACK_SCREEN_H */

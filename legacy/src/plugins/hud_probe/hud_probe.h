/* hud_probe.h: watch every authored-property read and name the callers.
 *
 * NOT A FIX. It patches nothing the game reads and changes no behaviour; it records where the
 * engine asks for its authored values and writes that to the log on a key press. It exists to
 * answer one question - which code builds the in-game HUD - and should be switched off once it
 * has.
 *
 * THE SITE
 *
 * Every authored value in this engine is fetched through one function, and it is in the
 * EXECUTABLE rather than the rfl, which is why an rfl plugin could never reach the HUD:
 *
 *     0044E6E0   push esi / push edi
 *                mov edi,[esp+0x10]      the -1 that means "no sub-index"
 *                je  0044E70F
 *     0044E70F   mov eax,[esp+0xC]       the property index
 *                mov edx,[esi+8]         the object's flat value array
 *                lea eax,[edx+eax*4]     return &values[index]
 *
 * Found by breakpointing the one call site we already knew - rfl+789A4, the control class asking
 * for property 0x1C - and reading the vtable entry it dispatched through.
 *
 * WHAT IT RECORDS
 *
 * The return address and the index, deduplicated, plus the value as both a float and an integer.
 * The HUD's callers will be the ones fetching values that match the pixel-authored properties in
 * HUD-FINDING.md - MeterULPosX 18, MeterWidth 100, LBXOffset 7 and the rest.
 */
#ifndef HUD_PROBE_H
#define HUD_PROBE_H

void hud_probe_install(void);

#endif /* HUD_PROBE_H */

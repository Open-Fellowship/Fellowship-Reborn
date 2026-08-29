/* texture_probe.h: what a GUIControl_Texture draw actually computes.
 *
 * A DIAGNOSTIC. It reads and logs and changes nothing. Three attempts at scaling this path
 * failed because which local feeds the destination and which feeds the source rectangle was
 * inferred from a decompile instead of measured. This prints both, per control, so the next
 * attempt starts from numbers.
 */
#ifndef TEXTURE_PROBE_H
#define TEXTURE_PROBE_H

void texture_probe_install(void);

#endif /* TEXTURE_PROBE_H */

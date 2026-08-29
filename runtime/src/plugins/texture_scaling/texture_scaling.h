/* texture_scaling.h: GUIControl_Texture assigns a texel count into a screen pixel field.
 *
 * The control keeps a source rectangle in texels at +0x70/+0x74 and an on-screen size in pixels
 * at +0x40/+0x44, and its setup copies one straight into the other with no resolution term. The
 * engine had already computed a correct percentage-of-screen rectangle one call earlier and this
 * discards it, which is why the mouse pointer is 32x32 device pixels at every resolution when its
 * own data asks for 20% of the screen.
 *
 * DIFFERENT CLASS from hud_scaling, not an extension of it. That plugin stores to [esi+0x9C],
 * offset 156, and this control is 128 bytes; the offset is past the end of the object and the
 * texture path never reads it. See HUD-FINDING.md section 8.
 */
#ifndef TEXTURE_SCALING_H
#define TEXTURE_SCALING_H

void texture_scaling_install(void);

#endif /* TEXTURE_SCALING_H */

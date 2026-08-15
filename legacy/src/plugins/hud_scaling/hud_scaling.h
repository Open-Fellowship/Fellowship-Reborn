/* hud_scaling.h: GUI elements are sized in pixels authored for 640x480 and never scaled.
 *
 * Measured, not guessed. The volume slider bar is 315 pixels long at 640x480, at 800x600 and at
 * 3840x2160; the checkbox mark is 18x7 at all three. POSITIONS, by contrast, fit an exact affine
 * law - 0.25 * W + 7 and so on - with zero residual. So the containers scale and the contents do
 * not, and at 640x480 the two agree because that is what the interface was authored against.
 *
 * The engine's own property names say it out loud: "Cell Width (Screen %)" next to
 * "Width (texels)", "Space From Right Edge of Screen (%)" next to "Bar Y Offset (px)".
 */
#ifndef HUD_SCALING_H
#define HUD_SCALING_H

void hud_scaling_install(void);

#endif /* HUD_SCALING_H */

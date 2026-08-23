/* hud_scaling.h: GUI elements are sized in pixels authored for 640x480 and never scaled.
 *
 * Measured, not guessed: the volume slider bar is 315 pixels long at 640x480, 800x600 and
 * 3840x2160 alike, while positions fit an exact affine law. Containers scale, contents do not.
 *
 * WIDTH, not height: this factor governs a horizontal extent. text_scaling is height-based, and
 * two different references is correct. See README.md.
 */
#ifndef HUD_SCALING_H
#define HUD_SCALING_H

void hud_scaling_install(void);

#endif /* HUD_SCALING_H */

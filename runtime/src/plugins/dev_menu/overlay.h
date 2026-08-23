/* overlay.h: coloured rectangles on top of the frame, and text made out of them.
 *
 * NO TEXTURES, DELIBERATELY. A font atlas would mean CreateTexture, a lock and an upload, stage
 * state, a pool choice, and code to rebuild all of it after a device Reset: a lot of surface area
 * for a dev menu, and every bit of it a new way to break the game's rendering on a machine
 * nobody here can test.
 *
 * So glyphs are rasterised once with GDI into bitmasks and drawn as untextured rectangles, one
 * quad per run of lit pixels, in a single DrawPrimitiveUP. Nothing is allocated on the device, so
 * there is nothing to lose when it resets. See README.md.
 */
#ifndef DEV_MENU_OVERLAY_H
#define DEV_MENU_OVERLAY_H

#include <stdbool.h>

/* Rasterises the font and allocates the vertex batch. Safe to call more than once; the second
 * call does nothing. Returns false if either step failed, in which case nothing draws. */
bool overlay_prepare(int pixel_height);

/* Height of one line of text at scale 1, in pixels. Zero before overlay_prepare(). */
int overlay_line_height(void);

/* Width the string would occupy, for right-aligning and for hit-testing. */
int overlay_text_width(const char *text, int scale);

void overlay_begin(void);
void overlay_rect(int x, int y, int width, int height, unsigned colour);
void overlay_text(int x, int y, int scale, unsigned colour, const char *text);

/* A 1px frame drawn as four rectangles, because a dev menu wants edges. */
void overlay_frame(int x, int y, int width, int height, int thickness, unsigned colour);

/* Sets the states it needs, draws everything queued since overlay_begin(), and puts every state
 * it touched back the way it found it. */
void overlay_flush(void *device);

/* True when the last frame overflowed the batch, shown in the menu rather than hidden, because
 * a dev tool that silently drops what it was asked to draw is worse than one that admits it. */
bool overlay_overflowed(void);

#endif /* DEV_MENU_OVERLAY_H */

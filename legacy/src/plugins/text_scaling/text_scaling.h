/* text_scaling.h: the font renderer draws at a fixed pixel size at every resolution.
 *
 * Seven hooks, because text is not one number. The glyph quad has a width scale AND a height
 * scale and they are set in different places; the pen advance between glyphs is a third; the
 * measured width that centring depends on is a fourth; the space character is advanced by a raw
 * font field in TWO more places, one in DrawString and one in MeasureString; and the line height
 * is a seventh.
 *
 * That count is not over-engineering, it is what the first four-hook version cost. Scaling the
 * glyph and its advance but not the space advance produced words with no gaps; adding the space
 * but not the line height produced text that overflowed its own boxes. Each of those was a
 * screenshot from a real session.
 *
 * HEIGHT, not width. Glyphs have to scale uniformly or they stretch, so the reference is
 * viewportHeight / 480. hud_scaling is width-based because it governs a horizontal extent. Two
 * different references is correct.
 */
#ifndef TEXT_SCALING_H
#define TEXT_SCALING_H

void text_scaling_install(void);

#endif /* TEXT_SCALING_H */

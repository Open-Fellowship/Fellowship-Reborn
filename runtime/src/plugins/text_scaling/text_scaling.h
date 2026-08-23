/* text_scaling.h: the font renderer draws at a fixed pixel size at every resolution.
 *
 * Seven hooks, because text is not one number: glyph width and height are set in different
 * places, the pen advance is a third, the measured width centring depends on is a fourth, the
 * space advance appears in two more, and line height is a seventh. Fewer than seven was wrong in
 * a different way each time.
 *
 * HEIGHT, not width: glyphs must scale uniformly or they stretch. See README.md.
 */
#ifndef TEXT_SCALING_H
#define TEXT_SCALING_H

void text_scaling_install(void);

#endif /* TEXT_SCALING_H */

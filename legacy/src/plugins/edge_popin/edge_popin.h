/* edge_popin.h: the renderer's guard rect is a hard-coded 3072 x 1024 box.
 *
 * The only fix in this project that is a genuine engine BUG rather than a preference, and the
 * only one that should ship switched on unconditionally. It is also harmless at 640x480, because
 * there the rect is bigger than the screen and nothing is ever outside it.
 */
#ifndef EDGE_POPIN_H
#define EDGE_POPIN_H

void edge_popin_install(void);

#endif /* EDGE_POPIN_H */

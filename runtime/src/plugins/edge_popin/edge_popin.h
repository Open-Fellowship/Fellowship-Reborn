/* edge_popin.h: the renderer's guard rect is a hard-coded 3072 x 1024 box.
 *
 * The one fix here that is a genuine engine bug and not a preference, so it ships on
 * unconditionally. Harmless at 640x480. See README.md.
 */
#ifndef EDGE_POPIN_H
#define EDGE_POPIN_H

void edge_popin_install(void);

#endif /* EDGE_POPIN_H */

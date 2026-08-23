/* borderless.h: the game keeps its full-screen size and stops taking the screen exclusively.
 *
 * Exclusive full screen loses its window to the focus, and this engine draws nothing while its
 * window is down. A windowed device makes no such bargain. The presentation parameters are
 * rewritten on their way into CreateDevice and Reset, at COM vtable positions and not addresses,
 * so this works against wined3d, DXVK and the retail Microsoft runtime alike.
 *
 * OFF BY DEFAULT, INCLUDING UNDER WINE: it stops dev_menu working on a Steam Deck. See README.md.
 */
#ifndef BORDERLESS_H
#define BORDERLESS_H

void borderless_install(void);

#endif /* BORDERLESS_H */

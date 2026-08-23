/* borderless.h: the game keeps its full-screen size and stops taking the screen exclusively.
 *
 * Exclusive full screen is a bargain with the display: the game gets the mode it asks for, and in
 * exchange the window is at the mercy of the focus. Lose the foreground for an instant and the
 * runtime takes the window down with it, and this engine draws nothing while its window is down.
 * On a desktop that is a mild annoyance when you alt-tab. Under Wine, where a compositor or the
 * desktop window can take focus a moment after the mode change, it is the difference between a
 * game and a black screen.
 *
 * A windowed device makes no such bargain. So the presentation parameters are rewritten on their
 * way into CreateDevice and Reset - windowed, at the size of the desktop - and the window itself
 * is restyled to a borderless popup covering the screen. The game is told nothing: it asked for
 * 1280x800 and it gets a 1280x800 back buffer filling the screen, which is what it wanted.
 *
 * The parameters are intercepted at the same two COM vtable positions env_probe watches, so this
 * works against wined3d, DXVK and the retail Microsoft runtime alike and depends on no address in
 * the game.
 */
#ifndef BORDERLESS_H
#define BORDERLESS_H

void borderless_install(void);

#endif /* BORDERLESS_H */

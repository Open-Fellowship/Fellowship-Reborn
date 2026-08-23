/* level_select.h: New Game opens the developers' level list.
 *
 * A port of the community rfl edit, two bytes. The screen it reaches is the game's own, shipping
 * in every copy of Fellowship.rfl, and it is filled from LevelList.txt beside Fellowship.exe.
 *
 * The first plugin here that finds its site by SIGNATURE and not by address, because it is the
 * first with two builds to be right about. See README.md.
 */
#ifndef LEVEL_SELECT_H
#define LEVEL_SELECT_H

void level_select_install(void);

#endif /* LEVEL_SELECT_H */

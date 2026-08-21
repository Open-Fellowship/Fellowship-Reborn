/* level_select.h: New Game opens the developers' level list.
 *
 * A port of the community rfl edit, which is two bytes. The screen it reaches is the game's own,
 * finished and shipping in every copy of Fellowship.rfl; the list in it comes from LevelList.txt,
 * the plain text file already sitting next to Fellowship.exe.
 *
 * This is the first plugin here that finds its site by SIGNATURE rather than by address, because
 * it is the first one with two builds to be right about. See level_select.c.
 */
#ifndef LEVEL_SELECT_H
#define LEVEL_SELECT_H

void level_select_install(void);

#endif /* LEVEL_SELECT_H */

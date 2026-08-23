/* view_distance.h: how far the engine bothers to draw, and when it starts fading things out.
 *
 * Five independent preferences, each its own ini key. NONE of this is a bug fix: the 2002
 * defaults are correct for 2002 hardware and every key trades frame rate for draw distance.
 * See README.md.
 */
#ifndef VIEW_DISTANCE_H
#define VIEW_DISTANCE_H

void view_distance_install(void);

#endif /* VIEW_DISTANCE_H */

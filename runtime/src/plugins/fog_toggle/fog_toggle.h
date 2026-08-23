/* fog_toggle.h: turn distance fog off and on while the game runs.
 *
 * The engine calls SetFogEnable(BOOL) with the level author's choice; forcing that argument to
 * zero turns the fog off. Bound to a key so it can be flipped standing still. See README.md.
 */
#ifndef FOG_TOGGLE_H
#define FOG_TOGGLE_H

void fog_toggle_install(void);

#endif /* FOG_TOGGLE_H */

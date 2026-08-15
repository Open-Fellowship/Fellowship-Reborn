/* fog_toggle.h: turn distance fog off and on while the game runs.
 *
 * The engine calls SetFogEnable(BOOL) with the value the level author chose. Forcing that
 * argument to zero turns the fog off; leaving it alone gives the game back. Doing it from a key
 * rather than a config value is the point: the fog is what hides the draw distance, so being
 * able to flip it while standing still is how you tell whether view_distance is doing anything.
 */
#ifndef FOG_TOGGLE_H
#define FOG_TOGGLE_H

void fog_toggle_install(void);

#endif /* FOG_TOGGLE_H */

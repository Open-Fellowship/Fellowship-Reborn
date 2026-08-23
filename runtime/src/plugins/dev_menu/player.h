/* player.h: finding the local player's game object, and changing its size.
 *
 * The engine offers no way to ask for either: no authored property for scale among the 4,262 the
 * ObjectDef table defines, and the debug command object accepts eight commands, none about size.
 * So this reads the object out of the rfl's own globals and writes to it directly, which is
 * heavier than anything else in this plugin and is why every step is validated on every call.
 *
 * The lookup is lifted from the shared head of three rfl getters, and the class id 0x1000e makes
 * it a real identification rather than a hopeful cast. One instruction of that sequence is NOT
 * reproduced: the original calls back into the engine and discards the result, which would add
 * the risk this file exists to avoid. See README.md.
 */
#ifndef DEV_MENU_PLAYER_H
#define DEV_MENU_PLAYER_H

#include <stdbool.h>
#include <stdint.h>

/* The local player's game object, or 0 if anything failed to validate, which is the normal
 * answer in a menu, on a loading screen, or before a level exists.
 *
 * `why` gets a word naming the first check that failed, for a one-shot diagnostic. Nothing is
 * cached: the object moves between levels and a stale pointer here is a crash. */
uintptr_t player_object(const char **why);

/* The player's world transform is a packed 3x3 at +0x00F8; scaling it scales the player.
 * Position is +0x00EC, the camera multiplier +0x011C.
 *
 * KNOWN SIDE EFFECT: the camera zooms and tilts with the player, because it sits at
 * playerPos + playerMatrix * (0, TrackHeight, -TrackDist) and is downstream of the same matrix.
 * There is no render-only transform to scale instead. README.md has the correction and the three
 * attempts that established the authored numbers cannot be reached. */

bool player_apply_size(float girth, float height, const char **why);

#endif /* DEV_MENU_PLAYER_H */

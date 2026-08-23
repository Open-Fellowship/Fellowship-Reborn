/* player.h: finding the local player's game object, and changing its size.
 *
 * The engine offers no way to ask for either. There is no authored property for a character's
 * scale anywhere in the 4,262 the ObjectDef table defines, and the debug command object accepts
 * exactly eight commands, none of which is about size. So this reads the object out of the rfl's
 * own globals and writes to it directly, which is a heavier thing to do than anything else in
 * this plugin, and is why every step is validated on every call.
 *
 * WHERE THE OBJECT COMES FROM
 *
 * Not guessed. `Fellowship.rfl` has three near-identical getters at 0x1005e9d0, 0x1005ea50 and
 * 0x1005eb40 whose first 0x5a bytes are byte for byte the same, and that shared head IS this
 * lookup:
 *
 *     MOV EAX,[0x101326cc]              the level's object manager
 *     MOV EBX,[EAX + 0xb8]              the local player's game object
 *     MOV SI,[EBX + 0xc]                its ObjectDef index, 0xffff for none
 *     MOV EDI,[0x101326e4]              the ObjectDef entry list
 *     CMP ESI,[EDI + 0xc]               against its count
 *     MOV ECX,[EDI + 0x4]               its entry array
 *     LEA EAX,[ESI + ESI*0x8]           index * 9
 *     LEA EAX,[ECX + EAX*0x4]           ... * 4, so a 36-byte stride
 *     CMP [EAX + 0x4],0x1000e           the entry's class id: Player
 *
 * 0x1000e is the id the ObjectDef table gives the class named `Player`, so the class check is a
 * real identification rather than a hopeful cast. If it does not hold, this refuses.
 *
 * One instruction of that sequence is deliberately NOT reproduced: the original calls
 * `[[EDI]+0x10]` on the entry list before indexing it, and discards the result. We only read the
 * entry's class id, and calling into the engine to satisfy a read we are already validating would
 * be adding the risk this file exists to avoid.
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

/* ----------------------------------------------------------------------------- the size cheat
 *
 * The player's world transform is a packed 3x3 at **+0x00F8** of the game object, and scaling it
 * scales the player. Its position is at +0x00EC and the camera multiplier at +0x011C; see
 * README.md beside this file for how each was established and what is still open.
 *
 * The engine has no notion of a character's size otherwise. There is no such property among the
 * 4,262 the ObjectDef table defines; the whole schema was searched, and the debug command object
 * accepts exactly eight commands, none about size. Writing this matrix is the only way in.
 *
 * KNOWN SIDE EFFECT: the camera zooms with the player, and tilts down as it closes. It sits at
 * `playerPos + playerMatrix * (0, TrackHeight, -TrackDist)`, so scaling the matrix scales both the
 * distance and the height of that offset by the same factor.
 *
 * There is no render-only transform to scale instead. The model scales BECAUSE its world matrix is
 * computed from +0x00F8 each frame, and the camera scales for the same reason; both are
 * downstream of one source, so there is nothing upstream to scale separately.
 *
 * The two authored numbers the offset is built from cannot be reached from memory either; the
 * README records the three attempts that establish that. What CAN be corrected is the result,
* see "holding the camera still" below.
 */

bool player_apply_size(float girth, float height, const char **why);

#endif /* DEV_MENU_PLAYER_H */

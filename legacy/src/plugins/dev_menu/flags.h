/* flags.h: the engine's own developer flags, all 124 of them.
 *
 * Fellowship.exe carries a complete debug menu that no shipping build's UI reaches. It is not a
 * leftover string table: the entries are registered at run time with names, the values live in an
 * array the renderer and the object system actually read, and there is a setter that performs
 * each flag's side effects. Wireframe, full bright, bounding volumes, occlusion, engine stats,
 * screenshots, cache dumps.
 *
 * This module is the access, not the interface. It reads names and values and asks the engine to
 * change one; where they appear on screen is dev_menu.c's business.
 */
#ifndef DEV_MENU_FLAGS_H
#define DEV_MENU_FLAGS_H

#include <stdbool.h>
#include <stdint.h>

#define FLAG_COUNT 124

/* The object and its two arrays, checked now. False means the list should be drawn dead. */
bool flags_available(void);

/* The engine's own name for the flag, or NULL where the array has a hole - indices 71, 95, 96
 * and 99 to 101 are unnamed in this build. */
const char *flags_name(int index);

bool flags_value(int index, int32_t *out);

/* Goes through the engine's setter rather than writing the array, because a third of these have
 * side effects: 13 sets two other flags, the screenshot one takes a screenshot, the cache ones
 * flush caches. Writing the array directly would set the number and do none of the work.
 *
 * MUST be called on the game's own thread, which here means from inside the EndScene hook.
 */
bool flags_set(int index, int32_t value);

/* What the game's own menu does when this entry is pressed. Recovered from the dispatcher's case
 * map rather than guessed: see flags.c. */
typedef enum flag_kind {
    FLAG_TOGGLE = 0,   /* 0 and 1, and whatever else that flag's case does alongside */
    FLAG_CYCLE,        /* steps through 0..range-1 */
    FLAG_ACTION,       /* fires something; the value is not the point */
    FLAG_NUMBER        /* holds a number that means something: the teleport coordinates */
} flag_kind_t;

flag_kind_t flags_kind(int index);
int         flags_cycle_range(int index);   /* how many values a FLAG_CYCLE steps through */

/* Press the entry, exactly as the engine's own debug menu presses it: dispatcher 0x411BC0, which
 * knows for each entry whether it is a switch, a cycle, or a command, and which runs the
 * side effects that make the flag mean anything.
 *
 * MUST be called on the game's own thread, so from inside the EndScene hook.
 */
bool flags_activate(int index);

#endif /* DEV_MENU_FLAGS_H */

/* cheats.h: the game's own cheat commands, called the way the game calls them.
 *
 * Every entry of the engine's unreachable debug menu ends in the same two instructions: a command
 * string on the stack and one vtable slot on one global object. This is that call and nothing
 * else. No game state is written directly. See README.md.
 */
#ifndef DEV_MENU_CHEATS_H
#define DEV_MENU_CHEATS_H

#include <stdbool.h>

typedef enum cheat_id {
    CHEAT_FLY = 0,
    CHEAT_INVINCIBLE,
    CHEAT_MRCLEAN,
    CHEAT_FULL_HEALTH,
    CHEAT_DROP,
    CHEAT_INVISIBLE_WALLS,
    CHEAT_SUICIDE,
    CHEAT_COUNT
} cheat_id_t;

const char *cheat_label(cheat_id_t id);

/* True for the two the engine's own debug menu tracks a state for. The rest are one-shot: they
 * happen and there is nothing to be on or off about. */
bool cheat_is_toggle(cheat_id_t id);

/* What we last sent, for the two toggles. NOT read back from the game, which offers no way to
 * ask, so it is what the menu believes, and the menu says so. */
bool cheat_believed_state(cheat_id_t id);

/* The command object and its vtable entry, checked now. False means the button should be dead
 * this frame rather than hopeful. */
bool cheats_available(void);

/* MUST be called on the game's own thread, which for this plugin means from inside the EndScene
 * hook. Returns false if anything failed to validate, in which case nothing was called. */
bool cheat_send(cheat_id_t id);

#endif /* DEV_MENU_CHEATS_H */

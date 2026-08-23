/* env_probe.h: what machine is this, and what did Direct3D say?
 *
 * A diagnostic, not a fix, and it changes nothing the game does. It exists because a black screen
 * on somebody else's machine is otherwise a conversation of guesses: the log says every plugin
 * installed, the game says nothing at all, and the two facts that would settle it, which
 * Direct3D 8 implementation is in the process, and what happened when the game asked it for a
 * device, are invisible from both ends.
 *
 * On a Steam Deck, or anything else running through Wine, this is the difference between "it
 * hangs" and "CreateDevice was asked for 1280x800 X8R8G8B8 fullscreen and answered
 * D3DERR_NOTAVAILABLE".
 */
#ifndef ENV_PROBE_H
#define ENV_PROBE_H

void env_probe_install(void);

#endif /* ENV_PROBE_H */

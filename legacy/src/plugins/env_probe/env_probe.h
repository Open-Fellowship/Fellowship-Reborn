/* env_probe.h: what machine is this, and what did Direct3D say?
 *
 * A diagnostic, not a fix. It changes nothing the game does. It records which Direct3D 8
 * implementation is in the process and what happened when the game asked it for a device, which
 * are the two facts a black screen on somebody else's machine otherwise hides. See README.md.
 */
#ifndef ENV_PROBE_H
#define ENV_PROBE_H

void env_probe_install(void);

#endif /* ENV_PROBE_H */

/* hud_probe.h: watch every authored-property read and name the callers.
 *
 * NOT A FIX. It patches nothing the game reads and changes no behaviour. It hooks the universal
 * authored-value getter at 0044E6E0, which runs roughly 230,000 times a second, so the recording
 * path must stay a bounded handful of instructions with no allocation and no lock. Switch it off
 * once it has answered the question you turned it on for. See README.md.
 */
#ifndef HUD_PROBE_H
#define HUD_PROBE_H

void hud_probe_install(void);

#endif /* HUD_PROBE_H */

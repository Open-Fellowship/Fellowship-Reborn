/* dinput8_proxy.h: the six exports a dinput8.dll is expected to have.
 *
 * dinput8 is the right slot because d3d8 is already taken: the community wrapper lives there in
 * essentially every installation, and a loader must be the one thing in the folder that cannot
 * break anything else. DINPUT8 is imported for one function and is not a KnownDLL, so the
 * application directory wins over System32.
 *
 * IF THE GAME FOLDER ALREADY HOLDS A dinput8.dll, rename it to dinput8_orig.dll; do not
 * overwrite it. Every export is forwarded to whatever the chain resolves to. See README.md.
 */
#ifndef LOADER_DINPUT8_PROXY_H
#define LOADER_DINPUT8_PROXY_H

#include <windows.h>

/* Resolves and loads the chain target. Safe to call more than once; only the first call works.
 * Must NOT be called from DllMain. */
void dinput8_proxy_open_chain(void);

#endif /* LOADER_DINPUT8_PROXY_H */

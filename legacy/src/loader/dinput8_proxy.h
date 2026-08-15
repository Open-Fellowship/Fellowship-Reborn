/* dinput8_proxy.h: the six exports a dinput8.dll is expected to have.
 *
 * ==============================================================================================
 * WHY dinput8.dll IS THE RIGHT SLOT, AND WHY d3d8.dll IS NOT
 *
 * Fellowship.exe (No-CD, 2,133,459 bytes) imports from fourteen libraries. Two of them are
 * candidates for a proxy and only one of them is free:
 *
 *     d3d8.dll       1 function   Direct3DCreate8       ALREADY TAKEN
 *     DINPUT8.dll    1 function   DirectInput8Create    <- ours
 *
 * The d3d8 slot is where the community wrapper already lives in essentially every installation
 * of this game, alongside its d3d8.ini. Taking it would mean either displacing a component
 * players depend on or chaining through it, and the entire point of a loader is that it is the
 * one thing in the folder that cannot break anything else.
 *
 * DINPUT8 is the mirror image of the slot the sibling project uses on Phantom Menace. It is
 * imported for exactly one function, it is not a KnownDLL, so the application directory wins over
 * System32, and no graphics wrapper wants it.
 *
 * ==============================================================================================
 * THE CHAIN
 *
 * We take the dinput8.dll name, so whatever used to answer to it must be given a new one. The
 * chain target is resolved in this order and the result is logged:
 *
 *   1. `ChainDll` from [loader] in open_fellowship.ini (absolute, or relative to the game folder)
 *   2. <game folder>\dinput8_orig.dll
 *   3. <system directory>\dinput8.dll
 *
 * If the game folder already holds a dinput8.dll - an input wrapper, an ASI loader - rename it to
 * dinput8_orig.dll rather than overwriting it. Every export is forwarded to it and it keeps
 * working.
 */
#ifndef LOADER_DINPUT8_PROXY_H
#define LOADER_DINPUT8_PROXY_H

#include <windows.h>

/* Resolves and loads the chain target. Safe to call more than once; only the first call works.
 * Must NOT be called from DllMain. */
void dinput8_proxy_open_chain(void);

#endif /* LOADER_DINPUT8_PROXY_H */

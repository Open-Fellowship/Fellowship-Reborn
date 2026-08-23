/* plugin_loader.h: load every DLL in <game>\plugins and give each one its entry point.
 *
 * The loader knows no engine addresses, patches no bytes and has no opinion about what a plugin
 * does. It enumerates in sorted order, LoadLibrary's each one, calls open_fellowship_install if
 * present, and writes a line per plugin. A DLL without that export is loaded anyway and noted.
 *
 * ORDER ENCODES NO DEPENDENCIES, and there is NO HOOK CHAINING MECHANISM. Where two plugins hook
 * the same Direct3D vtable slot they chain by convention only: each saves the pointer it found
 * and calls through it, so load order decides which runs first and nothing enforces that they
 * agree. See README.md.
 */
#ifndef LOADER_PLUGIN_LOADER_H
#define LOADER_PLUGIN_LOADER_H

/* Idempotent. Must NOT be called from DllMain; it calls LoadLibrary. */
void plugin_loader_run_once(void);

#endif /* LOADER_PLUGIN_LOADER_H */

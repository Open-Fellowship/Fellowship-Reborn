/* plugin_loader.h: load every DLL in <game>\plugins and give each one its entry point.
 *
 * The loader is a jumping-off point and nothing else. It knows no engine addresses, patches no
 * bytes and has no opinion about what a plugin does. It:
 *
 *   1. enumerates <game>\<PluginDirectory>\*.dll in sorted order, so the sequence is
 *      reproducible rather than dependent on the file system;
 *   2. LoadLibrary's each one;
 *   3. calls its `open_fellowship_install` export if it has one;
 *   4. writes one line per plugin to the shared log.
 *
 * A DLL without that export is loaded anyway and noted as such: an ordinary third-party DLL is a
 * legitimate thing to want in there.
 *
 * Order does not encode dependencies. Plugins are independent by construction: none calls into
 * another, and where two of them detour the same engine function, common/detour.c chains them so
 * the result is the same whichever loaded first.
 */
#ifndef LOADER_PLUGIN_LOADER_H
#define LOADER_PLUGIN_LOADER_H

/* Idempotent. Must NOT be called from DllMain; it calls LoadLibrary. */
void plugin_loader_run_once(void);

#endif /* LOADER_PLUGIN_LOADER_H */

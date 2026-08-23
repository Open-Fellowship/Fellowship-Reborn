/* plugin_entry.h: the contract between the loader and a plugin DLL.
 *
 *     void open_fellowship_install(void);
 *
 * The loader calls it AFTER LoadLibrary returns, outside the loader lock, with the main image
 * mapped. DllMain may not scan, read files or load anything, which is why this step exists.
 *
 * AT INSTALL TIME THE RFL IS NOT LOADED. A plugin patching Fellowship.rfl must wait for it; see
 * module_watch.h. A DLL without this export is still loaded and noted. See README.md.
 */
#ifndef COMMON_PLUGIN_ENTRY_H
#define COMMON_PLUGIN_ENTRY_H

#define OPEN_FELLOWSHIP_ENTRY_NAME "open_fellowship_install"

#define OPEN_FELLOWSHIP_ENTRY __declspec(dllexport) void __cdecl open_fellowship_install(void)

#endif /* COMMON_PLUGIN_ENTRY_H */

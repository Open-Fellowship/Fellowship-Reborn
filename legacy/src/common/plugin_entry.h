/* plugin_entry.h: the contract between the loader and a plugin DLL.
 *
 *     void fellowship_reborn_install(void);
 *
 * The loader calls it AFTER LoadLibrary returns, outside the loader lock, with the main image
 * mapped. DllMain may not scan, read files or load anything, which is why this step exists.
 *
 * AT INSTALL TIME THE RFL IS NOT LOADED. A plugin patching Fellowship.rfl must wait for it; see
 * module_watch.h. A DLL without this export is still loaded and noted. See README.md.
 */
#ifndef COMMON_PLUGIN_ENTRY_H
#define COMMON_PLUGIN_ENTRY_H

#define FELLOWSHIP_REBORN_ENTRY_NAME "fellowship_reborn_install"

#define FELLOWSHIP_REBORN_ENTRY __declspec(dllexport) void __cdecl fellowship_reborn_install(void)

#endif /* COMMON_PLUGIN_ENTRY_H */

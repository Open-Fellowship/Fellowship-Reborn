/* plugin_entry.h: the contract between the loader and a plugin DLL.
 *
 * A plugin exports exactly one function:
 *
 *     void open_fellowship_install(void);
 *
 * The loader calls it AFTER LoadLibrary has returned, which means it runs OUTSIDE the loader
 * lock with the main image fully mapped. That is the entire reason the loader exists as a
 * separate step from DllMain: DllMain may not scan, may not read files and may not load
 * anything, and a patch installer wants to do all three.
 *
 * A DLL in the plugins folder WITHOUT this export is still loaded; the loader notes that it has
 * no entry point and moves on. An ordinary third-party DLL is a legitimate thing to put in
 * there, and this is how it keeps working.
 *
 * WHAT A PLUGIN MUST NOT ASSUME AT INSTALL TIME
 *
 * The trigger fires at the host executable's entry point, before the CRT has run and long before
 * the game has loaded Fellowship.rfl. A plugin that patches the exe can do its work immediately;
 * a plugin that patches the rfl must wait for it, because GetModuleHandleA("Fellowship.rfl")
 * returns NULL at install time. Take a frame hook, or hook LoadLibrary, and do the work when the
 * module is actually there.
 */
#ifndef COMMON_PLUGIN_ENTRY_H
#define COMMON_PLUGIN_ENTRY_H

#define OPEN_FELLOWSHIP_ENTRY_NAME "open_fellowship_install"

#define OPEN_FELLOWSHIP_ENTRY __declspec(dllexport) void __cdecl open_fellowship_install(void)

#endif /* COMMON_PLUGIN_ENTRY_H */

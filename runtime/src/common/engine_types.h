/* engine_types.h: the assumptions every other file in this tree is allowed to make.
 *
 * The engine is 32-bit. Every offset and every pointer written into engine memory assumes a
 * 4-byte pointer, and getting that wrong is not a compile error on its own. The assertion below
 * is the cheapest place to find out. See README.md.
 */
#ifndef COMMON_ENGINE_TYPES_H
#define COMMON_ENGINE_TYPES_H

#include <stddef.h>
#include <stdint.h>

_Static_assert(sizeof(void *) == 4,
               "Fellowship Reborn targets a 32-bit game. Configure with: cmake -S . -B build -A Win32");
_Static_assert(sizeof(float) == 4, "engine floats are IEEE 754 single precision");

/* Fellowship.exe, No-CD, 2,133,459 bytes. Both bases are the PREFERRED bases from the PE
 * headers; nothing here may assume the module landed there. Ask host_image_base() or
 * GetModuleHandle instead. */
#define FELLOWSHIP_EXE_PREFERRED_BASE ((uintptr_t)0x00400000u)
#define FELLOWSHIP_RFL_PREFERRED_BASE ((uintptr_t)0x10000000u)

/* The game's own module names, as they appear in the loaded module list. Fellowship.rfl is a
 * PE32 DLL despite the extension; GetModuleHandleA("Fellowship.rfl") is the correct way to find
 * it and it is loaded well after our entry-point trigger fires, so a plugin that needs it must
 * wait rather than resolve it at install time. */
#define FELLOWSHIP_EXE_MODULE "Fellowship.exe"
#define FELLOWSHIP_RFL_MODULE "Fellowship.rfl"

/* What the retail engine is called once engine/'s proxy has taken its name. Do not compare
 * against these two by hand: ask fellowship_rfl_module_name() in common/module_watch.h,
 * which picks the right one and is correct whether or not the proxy is installed. */
#define FELLOWSHIP_RFL_PROXIED_MODULE "Fellowship.orig.rfl"

/* The virtual screen is always 128 units wide, so the authored field of view is horizontal and
 * the 640x480 interface maps at 5 pixels per unit. Several plugins need these. See README.md.
 *
 *     camera+0x228  halfW = 64.0                 always
 *     camera+0x22C  halfH = 64.0 * H / W         48.0 at 4:3, 36.0 at 16:9
 *     focal         = 64.0 / tan(fov * pi/360)
 */
#define ENGINE_VIRTUAL_HALF_WIDTH   64.0f
#define ENGINE_VIRTUAL_WIDTH       128.0f
#define ENGINE_AUTHORED_WIDTH      640.0f
#define ENGINE_AUTHORED_HEIGHT     480.0f
#define ENGINE_AUTHORED_PX_PER_UNIT  5.0f   /* 640 / 128 */

#endif /* COMMON_ENGINE_TYPES_H */

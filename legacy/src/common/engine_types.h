/* engine_types.h: the assumptions every other file in this tree is allowed to make.
 *
 * The Riot Engine build we target is 32-bit. Every offset, every operand repoint and every
 * pointer written into engine memory in this project assumes a 4-byte pointer. Getting that
 * wrong does not produce a compile error on its own, it produces a 64-bit DLL that cannot be
 * loaded by the game at all, or worse, a structure whose fields have quietly moved.
 *
 * The assertion below is the cheapest possible place to find that out.
 */
#ifndef COMMON_ENGINE_TYPES_H
#define COMMON_ENGINE_TYPES_H

#include <stddef.h>
#include <stdint.h>

_Static_assert(sizeof(void *) == 4,
               "OpenFellowship targets a 32-bit game. Configure with: cmake -S . -B build -A Win32");
_Static_assert(sizeof(float) == 4, "engine floats are IEEE 754 single precision");

/* Fellowship.exe, No-CD, 2,133,459 bytes. Both bases are the PREFERRED bases from the PE
 * headers; nothing in this tree may assume the module actually landed there. Ask
 * host_image_base() or GetModuleHandle instead. They are here because the byte-patch tooling in
 * _FixEnhancers speaks in absolute addresses and the arithmetic to convert has to live
 * somewhere obvious. */
#define FELLOWSHIP_EXE_PREFERRED_BASE ((uintptr_t)0x00400000u)
#define FELLOWSHIP_RFL_PREFERRED_BASE ((uintptr_t)0x10000000u)

/* The game's own module names, as they appear in the loaded module list. Fellowship.rfl is a
 * PE32 DLL despite the extension; GetModuleHandleA("Fellowship.rfl") is the correct way to find
 * it and it is loaded well after our entry-point trigger fires, so a plugin that needs it must
 * wait rather than resolve it at install time. */
#define FELLOWSHIP_EXE_MODULE "Fellowship.exe"
#define FELLOWSHIP_RFL_MODULE "Fellowship.rfl"

/* What the retail engine is called once engine/'s proxy has taken its name. Do not compare
 * against these two by hand: ask fellowship_rfl_module_name() in common/module_watch.h, which
 * picks the right one and is correct whether or not the proxy is installed. */
#define FELLOWSHIP_RFL_PROXIED_MODULE "Fellowship.orig.rfl"

/* The engine's virtual screen is always 128 units wide, so the authored 640x480 interface maps
 * at exactly 5 pixels per unit and the field of view is horizontal. Several plugins need these
 * and none of them should be spelling them out again.
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

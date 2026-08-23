/* engine_sites.h: the engine addresses this project has proven, in one place.
 *
 * ==============================================================================================
 * THESE ARE PREFERRED-BASE ADDRESSES, NOT RUNTIME ONES
 *
 * Every constant here is written the way the byte-patch tooling in _FixEnhancers speaks: an
 * absolute address in a module loaded at its preferred base. Nothing may use one directly.
 * Convert first:
 *
 *     exe_site(0x48BEF0)   ->  host_image_base() + (0x48BEF0, 0x400000)
 *     rfl_site(base, 0x789A7)
 *
 * ==============================================================================================
 * THE TARGET BUILD
 *
 * Fellowship.exe, No-CD, 2,133,459 bytes. Fellowship.rfl, 1,372,160 bytes. Every site below was
 * verified against those two files byte for byte, and every plugin re-verifies the bytes it is
 * about to overwrite before it writes them, so a different build declines rather than corrupts.
 *
 * Signature scanning is the upgrade path and is deliberately not here yet: it is worth doing when
 * a second build turns up to test against, and worth nothing before then.
 */
#ifndef COMMON_ENGINE_SITES_H
#define COMMON_ENGINE_SITES_H

#include "common/engine_types.h"
#include "common/host_image.h"

#include <stdint.h>

static inline uintptr_t exe_site(uint32_t preferred_va)
{
    return host_image_base() + (preferred_va - (uint32_t)FELLOWSHIP_EXE_PREFERRED_BASE);
}

static inline uintptr_t rfl_site(uintptr_t rfl_base, uint32_t rva)
{
    return rfl_base + rva;
}

/* ---------------------------------------------------------------------------- Fellowship.exe */

/* The active camera. Everything about field of view, viewport and projection hangs off it. */
#define EXE_ACTIVE_CAMERA_PTR   0x00544064u   /* -> camera object, NULL outside a level */
#define CAMERA_PROJ_X           0x03Cu        /* -focal / halfW */
#define CAMERA_PROJ_Y           0x040u        /* -focal / halfH */
#define CAMERA_NEG_FOCAL        0x230u
#define CAMERA_HALF_W           0x228u        /* 64.0 always */
#define CAMERA_HALF_H           0x22Cu        /* 64.0 * H / W */
#define CAMERA_FOCAL            0x248u        /* NUM / tan(fov * pi/360) */
#define CAMERA_VIEWPORT_W       0x254u        /* int */
#define CAMERA_VIEWPORT_H       0x258u        /* int */
#define CAMERA_DEVICE_W         0x234u        /* int, what GetAspect reads */
#define CAMERA_DEVICE_H         0x238u        /* int */

/* The renderer, and the Direct3D 8 device hanging off it. Established from the engine's own
 * call at 0047BDDD:
 *
 *     mov  ecx,[0x54743C]        the renderer
 *     mov  eax,[ecx+0x166]       -> IDirect3DDevice8*      (an unaligned field, but its own)
 *     mov  edx,[eax]             its vtable
 *     call dword ptr [edx+0x8c]  EndScene, index 35
 *
 * The exe imports exactly one Direct3D symbol, Direct3DCreate8, and calls +0x3C, +0x88 and +0x8C
 * on this same object, Present, BeginScene, EndScene at indices 15, 34 and 35. Three hits on
 * the published ordering at three different indices is what fixes the interface. */
#define EXE_RENDERER_PTR        0x0054743Cu
#define RENDERER_D3D_DEVICE     0x166u

/* The visibility distance in cells, read by seven `fld` sites. */
#define EXE_VISIBILITY_CELLS    0x005432ACu

/* ------------------------------------------------------------------------------ frame timing
 *
 * The engine object at 0x00543280 holds the numbers the whole game reads for time, and the Timer
 * instance at 0x0053EE58 is what produces them. Written up in full in plugins/frame_timing.
 *
 * Neither the delta nor the frame rate has a WRITE anywhere in the image, and that is not a
 * puzzle; both are written through pointers. UpdateTime at 0x00408F00 does `lea edi,[esi+4]`
 * and hands that to Timer::Tick; 0x00409000 does `add ecx,0x14` and hands that to
 * Timer::GetFramerate. Searching for a store to either address finds nothing at all.
 *
 * Read only, from this side. The delta is overwritten at the top of every frame, so writing to
 * it achieves nothing that survives to be noticed. */
#define EXE_FRAME_DELTA         0x00543284u   /* float, seconds, 31 read sites                */
#define EXE_FRAME_DELTA_RECIP   0x00543288u   /* float, 1.0 / delta                           */
#define EXE_FRAME_FPS           0x00543294u   /* float, resampled every 8 frames              */
#define EXE_GAME_TIME           0x00543364u   /* float, seconds, 26 read sites                */
#define EXE_MAX_FRAME_DELTA     0x00543368u   /* float, 0.1: the slowest frame the sim admits */

/* The Timer's ticks-to-seconds constant. 0.001 means the engine is still on GetTickCount; any
 * other value means frame_timing has moved it, and 1/value is the rate. Reading this is how a
 * tool tells which clock is running without asking the plugin that changed it. */
#define EXE_TIMER_OBJECT        0x0053EE58u
#define TIMER_SECONDS_PER_TICK  0x010u

/* ---------------------------------------------------------------------------- Fellowship.rfl */

/* The rfl's own pointer to the active camera. Same object as EXE_ACTIVE_CAMERA_PTR, reached
 * through the rfl's global rather than the exe's, which is what the shipped stubs use. */
#define RFL_INTERFACE_GLOBAL    0x10132698u

#endif /* COMMON_ENGINE_SITES_H */

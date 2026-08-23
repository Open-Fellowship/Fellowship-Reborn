#include "movie_skip.h"

#include "common/engine_sites.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/patch.h"
#include "common/platform.h"

#include <windows.h>

#include <stdint.h>

#define PLUGIN_SECTION "movie_skip"

/* Why the first version was not enough
 * ------------------------------------
 *
 * v1 patched MoviePC "begin playback" (0x47B9B0) to return 0 without setting bit 3 of the frame
 * mode word. That stops the engine from *pausing its drawing*, but it does not stop the movie.
 *
 * The rfl does not call the movie object directly. It calls the media manager (the object at
 * 0x5403A0, vtable 0x51EB40), slot 17 (0x47AB30), which is:
 *
 *     manager->current = movie;              [manager+0x230]
 *     movie->slot10();
 *     return movie->Begin(a, b);             <- v1 made this return 0, nothing else changed
 *
 * and every frame, from BOTH the bit-3 branch (0x404672) and the normal frame (0x47F258),
* the manager ticks its current movie (0x47AB70):
 *
 *     if (current && (current->state & 3) != 2)
 *         if (current->Update() == 0) { current->slot11(0); current = NULL; }
 *
 * Update (slot 23, 0x47BA20) is where the Windows Media reader is actually created and opened:
 *
 *     0047BAB7  call WMCreateReader                 -> on failure: return E_FAIL
 *     0047BADF  QueryInterface(IWMReaderAdvanced2)  -> on failure: return E_FAIL
 *     0047BB0A  OpenStream(this+0x50, this+0x54)    -> on failure: return E_FAIL
 *     0047BB44  WaitForSingleObject(opened, INFINITE)
 *     ...       spin at 0047BBBC until the first sample arrives
 *
 * Every one of those failure returns leaves the movie as "current", leaves the frame-mode bit
 * alone, and NEVER calls the completion callback ([this+0x38])(ctx, ...). Whoever asked for the
 * movie is still waiting for it to end. With v1 on, the engine draws every frame, and draws the
 * nothing that exists before the opening sequence has handed over to the main menu.
 *
 * On Wine those calls fail (or worse: never complete). WMCreateReaderPriv is only as good as the
 * 32-bit GStreamer behind it, and even with a codec the game's own IStream (0x51ECB0) is a
 * forward-only, double-buffered window, STREAM_SEEK_END returns STG_E_INVALIDFUNCTION at
 * 0x47C530, and STREAM_SEEK_SET outside the two buffered ranges is "MISSED OUR BUFFER RANGE" at
 * 0x47C6AB, while winegstreamer's reader wants random access. If it did get past open it
 * would then busy-wait at 0x47BBBC for a sample that may never come.
 *
 * What v2 does instead
 * --------------------
 *
 * The engine already has a "this movie is not going to play" path, the first thing Update checks:
 *
 *     0047BA3F  cmp [ecx],ebx ; je 47BA81         stream not ready ->
 *     0047BA43  if (cb) { cb(ctx,0); cb(ctx,1); }  <- report it finished, the way the engine does
 *     0047BA5E  clear bit 3 of 0053EE84
 *     0047BA77  return 0                            <- manager stops and forgets the movie
 *
 * So Update is made to take that path unconditionally, on its first tick after Begin, before it
 * has dereferenced anything:
 *
 *     0047BA29  8B 46 0C   mov eax,[esi+0Ch]   ->   EB 18 90   jmp 47BA43 ; nop
 *
 * All four pushes and the `mov esi,ecx` have already happened at 0x47BA29, and 0x47BA43 uses esi
 * as `this` and ends in the function's own epilogue, so the stack is exactly as the engine
 * expects. Begin (0x47B9B0) is left alone: it returns 1, sets bit 3 for one frame, and the very
 * next tick reports the movie over and clears the bit, which is precisely what happens on
 * Windows when a movie resource is missing.
 */
#define MOVIE_UPDATE_SITE_VA 0x0047BA29u

void movie_skip_install(void)
{
    /* mov eax,[esi+0Ch] / and eax,3 / cmp al,3 / jne rel32, the whole state check, verified
     * before the first three bytes of it are rewritten. */
    static const uint8_t expected[10]   = { 0x8B, 0x46, 0x0C, 0x83, 0xE0, 0x03, 0x3C, 0x03,
                                            0x0F, 0x85 };
    /* jmp short 0x47BA43 ; nop */
    static const uint8_t replacement[3] = { 0xEB, 0x18, 0x90 };

    uintptr_t      address;
    patch_result_t result;

    log_init(PLUGIN_SECTION, false);

    if (!ini_read_bool(PLUGIN_SECTION, "Enabled", platform_is_wine())) {
        log_info("Enabled=0, the engine plays its movies as it always did");
        return;
    }
    if (platform_is_wine()) {
        log_info("this is WINE %s; the Windows Media path does not complete here, so this is on "
                 "unless the ini says otherwise", platform_wine_version());
    }
    if (!host_image_resolve()) {
        log_error("the host image could not be resolved; refusing to touch anything");
        return;
    }

    address = exe_site(MOVIE_UPDATE_SITE_VA);

    /* Verify the full ten-byte signature, write only the first three. */
    if (!patch_validate_bytes(address, expected, sizeof(expected))) {
        log_error("%08X, not the MoviePC::Update state check this plugin was measured against; "
                  "leaving it alone", (unsigned)address);
        return;
    }
    result = patch_write_expect(address, expected, replacement, sizeof(replacement));
    if (result != PATCH_RESULT_OK) {
        log_error("%08X - %s", (unsigned)address, patch_result_text(result));
        return;
    }

    log_info("%08X  MoviePC::Update -> \"this movie is over\" on its first tick", (unsigned)address);
    log_info("  every movie now reports completion through the engine's own not-ready path: the "
             "callback fires, the frame-mode bit clears, the media manager forgets it, and "
             "whoever was waiting for the opening movies to end gets told that they have.");
}

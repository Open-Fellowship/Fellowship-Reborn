/* frame_timing.h: give the engine's frame clock a resolution worth measuring frames with.
 *
 * ==============================================================================================
 * WHAT IS WRONG
 *
 * The engine has a Timer class at 0x0040CF10..0x0040D29D with one global instance at 0x0053EE58,
 * and it reads the clock with GetTickCount. Every one of its fourteen clock reads goes through
 * the same five-byte thunk:
 *
 *     004C12B0   jmp dword ptr [GetTickCount]
 *
 * GetTickCount advances with the system clock interrupt, about every 15.625 ms. The frame delta
 * is the difference between two of its readings:
 *
 *     0040D150  Timer::Tick(float *out)
 *     0040D156    call 004C12B0                ; now
 *     0040D15F    sub  ecx, edx                ; now - this->lastTick
 *     0040D171    fild qword [esp+4]
 *     0040D175    fmul dword [esi+0x28]        ; timeScale
 *     0040D178    fmul dword [esi+0x10]        ; 0.001f, ticks to seconds
 *     0040D17B    fstp dword [edx]             ; -> *out, which is 0x00543284
 *
 * So above about 64 fps most frames measure ZERO milliseconds and every fifteenth measures
 * fifteen or sixteen. At a 60 fps cap the two rates beat against each other at exactly 4 Hz:
 * four times a second the world takes a 31 ms step and then a 0 ms one. That is the judder, and
 * no limiter tuning touches it, because the clock reading the frames is coarser than the frames.
 *
 * The engine's own answer to the zero frames is a floor of 0.002 s at 0x0051C764, which invents
 * simulation time that did not happen and is why the stock game speeds up when it is unlocked.
 * game_speed lowers that floor. This plugin removes the reason it exists.
 *
 * ==============================================================================================
 * WHAT THIS DOES
 *
 * QueryPerformanceCounter instead, at a configurable rate, defaulting to 100 kHz.
 *
 * Timer::Tick does not divide by a frequency - it MULTIPLIES by a ticks-to-seconds constant it
 * keeps at +0x10. So the class does not have to be rewritten, or even understood by the patch:
 * give it a finer counter and tell it what a tick is now worth, and every function in it keeps
 * working with its own arithmetic untouched. Three kinds of edit:
 *
 *   1. The fourteen `call 004C12B0` sites inside the class are redirected to hires_ticks().
 *      Call sites and not the thunk, because forty-six OTHER callers of that thunk are loading
 *      timeouts and progress bars that must keep counting in milliseconds. Turning a ten second
 *      timeout into a ten millisecond one is the bug this distinction exists to avoid.
 *
 *   2. 0040CF20, the immediate inside the constructor that puts 0.001f into +0x10, becomes
 *      1/rate. Patching the constructor's immediate rather than the field itself avoids having
 *      to win a race with the constructor, which runs from 00403CC4 during start-up.
 *
 *   3. 0051C774, the 1000.0f that Timer::Reset uses to go the other way, becomes the rate.
 *
 * The FPS readout is fixed by the same change and needs no work of its own: Timer::GetFramerate
 * at 0040D1B0 measures its interval with the same counter.
 *
 * ==============================================================================================
 * THE THIRTY-TWO BIT WRAP, WHICH IS NOT THEORETICAL
 *
 * The counter is a DWORD and the engine zero-extends the difference before converting it:
 *
 *     0040D161   mov dword ptr [esp+8], 0      ; the high half, forced to zero
 *     0040D171   fild qword ptr [esp+4]
 *
 * so a wrap does not produce a negative delta, it produces an enormous positive one. At 100 kHz
 * that happens after 11.9 hours. The frame delta itself would survive it, because UpdateTime
 * clamps at 0.1 s, but game time at 0x00543364 would jump by half a day and the twenty-six
 * effect sites that read it would go with it.
 *
 * So this plugin takes its own once-per-frame hook at 004046CE, the call to UpdateTime, and
 * rebases before the counter gets near the top: it subtracts a fixed amount from its own output
 * and from the same amount from every tick the Timer has stored, so every difference the engine
 * computes is unchanged. 004046CE is used rather than 004BCA19 because fps_limit already owns
 * that one, and because this has to happen between whole frames rather than inside Tick.
 *
 * ==============================================================================================
 * SAVEGAMES
 *
 * 0040D030 writes `now - this->+4` into the save stream and 0040D0A0 reads it back, so a save
 * carries an elapsed tick count in whatever unit the timer was using. A save made with this
 * plugin and loaded without it, or the reverse, gets ONE wrong frame rate reading before the
 * next eight-frame sample corrects it. Game time is stored as float seconds and is unit
 * independent, so nothing else crosses over. Worth knowing, not worth engineering around.
 */
#ifndef FRAME_TIMING_H
#define FRAME_TIMING_H

void frame_timing_install(void);

#endif /* FRAME_TIMING_H */

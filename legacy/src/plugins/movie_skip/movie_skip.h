/* movie_skip.h: the engine's movies never start, so nothing waits for them to end.
 *
 * This game plays its opening sequence through the WINDOWS MEDIA FORMAT runtime. `WMVCore.DLL` is
 * in the executable's import table, the player class names itself in its own error strings
 * ("MoviePC::Init() Not Fixed Samples!!!"), and the rfl authors the list as "Opening Movies".
 *
 * There is a global mode word at 0x53EE84 that the per-frame function switches on, and starting a
 * movie sets bit 3 of it:
 *
 *     0047B9C5  mov cl, byte ptr [0x53EE84]
 *     0047B9CB  mov eax, 8
 *     0047B9D0  test al, cl
 *     0047B9D2  jne 0x47B9DA
 *     0047B9D4  or  dword ptr [0x53EE84], eax        <- "a movie is playing"
 *     0047B9DA  mov eax, 1                           <- and playback started
 *     0047B9DF  ret 8
 *
 * With that bit set the per-frame function returns without drawing, and the movie's own update
 * clears it again when playback ends, at 0x47BA72, 0x47BC4D and 0x47BCB8, three exits of the
 * same function.
 *
 * On Wine that end never comes. `wmvcore` is a stub there, so the movie neither plays nor fails,
 * bit 3 stays on, and the game sits with a healthy Direct3D device, a window with the foreground,
 * a message loop still answering, and nothing on screen. That is the Steam Deck black screen, and
 * it took a frame counter, a thread sampler and a hook on the mode setter to find.
 *
 * The fix is five bytes: the function above is made to report that playback did not start, so the
 * bit is never set and the game goes straight on to what follows the movie. Nothing is deleted
 * and no file is touched.
 */
#ifndef MOVIE_SKIP_H
#define MOVIE_SKIP_H

void movie_skip_install(void);

#endif /* MOVIE_SKIP_H */

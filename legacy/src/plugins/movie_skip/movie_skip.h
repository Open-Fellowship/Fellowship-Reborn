/* movie_skip.h: every movie reports "finished" on its first tick, through the engine's own
 * path for a movie that could not be loaded.
 *
 * The game plays its opening sequence through the Windows Media Format runtime, which is a stub
 * under Wine, so the movie neither plays nor fails. Bit 3 of the mode word at 0x53EE84 stays
 * set, the per-frame function returns without drawing, and the game sits with a healthy Direct3D
 * device and nothing on screen. That was the Steam Deck black screen.
 *
 * Three bytes at 0x47BA29 send MoviePC::Update down that path, which fires the completion
 * callback the rfl is waiting on. Begin at 0x47B9B0 is deliberately left alone. See README.md.
 */
#ifndef MOVIE_SKIP_H
#define MOVIE_SKIP_H

void movie_skip_install(void);

#endif /* MOVIE_SKIP_H */

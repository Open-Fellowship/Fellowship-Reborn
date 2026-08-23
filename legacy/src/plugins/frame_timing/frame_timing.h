/* frame_timing.h: give the engine's frame clock a resolution worth measuring frames with.
 *
 * The engine's Timer class reads GetTickCount, which advances about every 15.625 ms, so above
 * roughly 64 fps most frames measure zero milliseconds and every fifteenth measures fifteen or
 * sixteen. This redirects the class's fourteen clock reads to QueryPerformanceCounter and
 * retunes the two constants that convert its ticks to seconds. Tick multiplies by a
 * ticks-to-seconds constant at +0x10 instead of dividing by a frequency, which is what makes
 * that possible without rewriting the class.
 *
 * The site list is in frame_timing.c. The measurements, the reason the thunk itself is left
 * alone, the thirty-two bit wrap and the savegame crossover are in README.md.
 */
#ifndef FRAME_TIMING_H
#define FRAME_TIMING_H

void frame_timing_install(void);

#endif /* FRAME_TIMING_H */

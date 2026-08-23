/* screen_test.h: paints the whole back buffer a solid colour, every frame, and cycles it.
 *
 * A DIAGNOSTIC, and the crudest one in this project on purpose.
 *
 * When the log says frames are being presented and the screen is black, there are exactly two
 * possibilities and no amount of further logging separates them: the game is drawing black, or
 * the picture is not reaching the display. This settles it in one run and needs no keyboard, no
 * overlay, no font and no input, which matters on a handheld.
 *
 * Red, green, blue, one second each. If the screen flashes colours, the display path works and
 * the game is drawing nothing into a perfectly good surface. If it stays black, the presented
 * image never reaches the panel and nothing inside the process can fix that.
 */
#ifndef SCREEN_TEST_H
#define SCREEN_TEST_H

void screen_test_install(void);

#endif /* SCREEN_TEST_H */

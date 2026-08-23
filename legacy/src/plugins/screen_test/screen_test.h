/* screen_test.h: paints the whole back buffer a solid colour, every frame, and cycles it.
 *
 * A diagnostic, and the crudest one here on purpose: no keyboard, no overlay, no font, no input,
 * which is what makes it usable on a handheld. It separates "the game is drawing black" from
 * "the picture is not reaching the display". See README.md.
 */
#ifndef SCREEN_TEST_H
#define SCREEN_TEST_H

void screen_test_install(void);

#endif /* SCREEN_TEST_H */

/* dev_menu.h: an overlay for changing things while the game runs.
 *
 * The first control is the field of view, because it is the one value in this project that is
 * genuinely a matter of taste rather than a bug, and setting it by editing an ini and restarting
 * is a poor way to find out what you like.
 *
 * WHAT THIS PLUGIN DOES TO THE GAME WHEN THE MENU IS CLOSED: nothing. The Direct3D hook is not
 * installed until the toggle key is pressed for the first time, so an install that never opens
 * the menu is an install where this DLL read an ini and started a thread.
 *
 * WHO WRITES THE CAMERA: not this plugin. The slider publishes a target through
 * `common/channel.h` and `field_of_view` applies it, keeping one writer for the camera and one
 * for the request. Two plugins writing the same field on two different timers is exactly the
 * fight the loader exists to make unnecessary.
 */
#ifndef DEV_MENU_H
#define DEV_MENU_H

void dev_menu_install(void);

#endif /* DEV_MENU_H */

/* dev_menu.h: an overlay for changing things while the game runs.
 *
 * The Direct3D hook is not installed until the toggle key is pressed, so an install that never
 * opens the menu is one where this DLL read an ini and started a thread.
 *
 * THIS PLUGIN DOES NOT WRITE THE CAMERA. The slider publishes a target through common/channel.h
 * and field_of_view applies it: one writer for the camera, one for the request. See README.md.
 */
#ifndef DEV_MENU_H
#define DEV_MENU_H

void dev_menu_install(void);

#endif /* DEV_MENU_H */

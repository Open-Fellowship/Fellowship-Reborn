/* inventory_icons.h: make the inventory's item models immune to whatever sets the camera's FOV.
 *
 * ONLY USEFUL ALONGSIDE A FOV MOD THAT REWRITES THE ENGINE'S FOCAL-LENGTH NUMERATOR, which is
 * why it ships off. With a stock numerator there is nothing to fix, and this plugin's arithmetic
 * reduces to exactly what the engine already does.
 *
 * The engine computes focal = NUM / tan(fov * pi/360) with NUM = 64.0 at 0x520A90. The community
 * patcher (CameraFieldOfView=-1.0) rewrites NUM to 64.0 / (0.75 * W/H), a correct Hor+
 * widescreen correction. But rfl+7A2D5, which places the item models, computes its own distance
 * bases from the UNPATCHED 64. The two then disagree by exactly 64/48 = 4/3 at 16:9 and agree
 * exactly at 4:3, which is why every item icon lands at 0.75x its correct offset from screen
 * centre and 0.75x its correct size on a widescreen monitor, and is perfect at 640x480.
 *
 * This makes the rfl read the camera's ACTUAL focal length instead of assuming, so it is correct
 * whatever is in that numerator.
 */
#ifndef INVENTORY_ICONS_H
#define INVENTORY_ICONS_H

void inventory_icons_install(void);

#endif /* INVENTORY_ICONS_H */

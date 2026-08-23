/* inventory_icons.h: make the inventory's item models immune to whatever sets the camera's FOV.
 *
 * ONLY USEFUL ALONGSIDE A FOV MOD THAT REWRITES THE ENGINE'S FOCAL-LENGTH NUMERATOR, which is
 * why it ships off: with a stock numerator its arithmetic reduces to what the engine already
 * does. README.md compares it with the alternative, which is the better default.
 */
#ifndef INVENTORY_ICONS_H
#define INVENTORY_ICONS_H

void inventory_icons_install(void);

#endif /* INVENTORY_ICONS_H */

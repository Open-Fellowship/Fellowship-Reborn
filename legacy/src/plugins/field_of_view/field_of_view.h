/* field_of_view.h: hold the VERTICAL field of view constant as the screen gets wider.
 *
 * ==============================================================================================
 * WHY THIS IS NOT THE OBVIOUS PATCH, AND WHY THAT MATTERS
 *
 * The engine computes
 *
 *     focal = NUM / tan(fov * pi/360)
 *
 * with NUM a qword constant at 0x520A90 holding 64.0, and 64.0 is halfW: the virtual screen is
 * always 128 units wide, so the authored field of view is HORIZONTAL and the vertical field is
 * whatever falls out of halfH = 64 * H / W. At 16:9 that means you see LESS of the world
 * vertically than the game was designed around, not more.
 *
 * The community patcher fixes this by rewriting NUM to 64.0 / (0.75 * W/H) at three sites. That
 * is a correct Hor+ correction and it has one non-obvious consequence: Fellowship.rfl+7A2D5,
 * which places the inventory item models, computes its own geometry from the UNPATCHED 64. The
 * two then disagree by exactly 64/48 = 4/3 at 16:9, and every item model renders at 0.75x its
 * correct offset from screen centre and 0.75x its correct size. That was measured, twice, from
 * position and from size independently.
 *
 * So this plugin does NOT touch NUM. It sets the FOCAL LENGTH instead, which is icon-safe by
 * construction: the inventory sets its own 20-degree field of view, does its arithmetic from
 * that same 20 degrees, and restores the previous value through GetFOV/SetFOV when it is done.
 * The world's field of view never enters the icon calculation at all.
 *
 * Setting focal alone is not enough; the renderer uses projX and projY, which are only
 * recomputed inside SetViewport, so every term SetViewport derives from focal is written too.
 */
#ifndef FIELD_OF_VIEW_H
#define FIELD_OF_VIEW_H

void field_of_view_install(void);

#endif /* FIELD_OF_VIEW_H */

/* field_of_view.h: hold the VERTICAL field of view constant as the screen gets wider.
 *
 * This sets the camera's FOCAL LENGTH. It does NOT touch the numerator at 0x520A90 the community
 * patcher rewrites, because Fellowship.rfl+7A2D5 computes the inventory icons' geometry from the
 * unpatched value and the two then disagree by 4/3 at 16:9.
 *
 * Setting focal alone is not enough: the renderer uses projX and projY, recomputed only inside
 * SetViewport, so every term SetViewport derives from focal is written too. See README.md.
 */
#ifndef FIELD_OF_VIEW_H
#define FIELD_OF_VIEW_H

void field_of_view_install(void);

#endif /* FIELD_OF_VIEW_H */

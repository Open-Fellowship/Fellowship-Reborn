/* view_distance.h: how far the engine bothers to draw, and when it starts fading things out.
 *
 * Four independent preferences, each its own ini key, because they cost different amounts and
 * players want different combinations:
 *
 *   FarPlane          the far clip plane, in the culling frustum and the software clipper
 *   FadeIgnoresCap    object fade stops being clamped to the visibility distance
 *   VisibilityCells   how many cells the engine considers at all (the engine's own units)
 *   IgnoreObjectFade  per-object authored fade distance ignored entirely
 *   PreloadResources  object resources requested regardless of distance (NPC pop-in)
 *
 * NONE of this is a bug fix. The 2002 defaults are correct for 2002 hardware and every key here
 * trades frame rate for draw distance. They are grouped in one DLL because they are one decision
 * with one failure mode; "the world draws further and the frame rate drops", and splitting them
 * into five DLLs would mean five READMEs saying the same paragraph.
 */
#ifndef VIEW_DISTANCE_H
#define VIEW_DISTANCE_H

void view_distance_install(void);

#endif /* VIEW_DISTANCE_H */

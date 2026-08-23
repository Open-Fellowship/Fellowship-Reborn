/* camera.h: read the active camera, or refuse.
 *
 * WHY THIS FILE EXISTS
 *
 * `EXE_ACTIVE_CAMERA_PTR` was documented as "NULL outside a level", and three plugins were
 * written against that promise: field_of_view read the focal length through it, hud_scaling and
 * text_scaling generated stubs that dereferenced it on every GUI control and every glyph.
 *
 * The promise does not hold on every machine. A crash log from a second install showed
 *
 *     [field_of_view] baseline focal 76.2722, horizontal 180.000 deg
 *
 * A horizontal field of view of exactly 180 degrees means 2*atan(halfW/focal) saturated, which
 * means halfW read back as something astronomical, so the pointer was NOT null, and what it
 * pointed at was NOT a camera. A stub that reads `[ebx+0x254]` off that pointer is an access
 * violation, and a plugin that writes a focal length derived from it is a corrupt projection
 * matrix. Both were observed.
 *
 * So: never trust the pointer, and never trust the fields. Everything in this header validates
 * before it returns, and the callers are written so that "no trustworthy camera" means "behave
 * exactly like the unmodded game" rather than "carry on with nonsense".
 *
 * The corollary, and the reason this is more than a null check: engine memory must not be
 * dereferenced from a generated stub on a hot path at all, because a stub cannot check anything
 * cheaply and cannot report what it found. Read the camera HERE, on a poll thread, validate it
 * here, and let the stubs multiply by a plain float in our own data section.
 */
#ifndef COMMON_CAMERA_H
#define COMMON_CAMERA_H

#include <stdbool.h>
#include <stdint.h>

typedef struct camera_view {
    uintptr_t object;            /* the camera itself, already validated */
    int32_t   viewport_width;    /* +0x254 */
    int32_t   viewport_height;   /* +0x258 */
    int32_t   device_width;      /* +0x234 */
    int32_t   device_height;     /* +0x238 */
    float     half_w;            /* +0x228, 64.0 on a sane camera */
    float     half_h;            /* +0x22C, 64.0 * H / W */
    float     focal;             /* +0x248 */
} camera_view_t;

/* Fills `out` and returns true only when every field passed its range check. False means either
 * there is no camera right now (the menus) or what is there cannot be believed; the caller must
 * not be able to tell those apart, because the correct response to both is to do nothing. */
bool camera_read(camera_view_t *out);

typedef void (*camera_watch_callback_t)(const camera_view_t *view);

/* Polls until a camera validates, calls `on_change` then, and again whenever the viewport
 * dimensions change. Returns false if the thread could not be started. */
bool camera_watch(unsigned interval_ms, camera_watch_callback_t on_change);

/* THE SAME POLL, BUT PUBLISHING A POINTER RATHER THAN A NUMBER.
 *
 * Some values genuinely have to be read at the moment they are used, not sampled on a timer. The
 * text and HUD scales are the example that taught us so: the pause menu renders the world into a
 * sub-rectangle, the camera's viewport IS that rectangle while the menu is drawn, and a stub that
 * multiplies by a number sampled a quarter of a second ago is using the wrong one.
 *
 * So the stub reads through a pointer, but never the engine's own global, which is the thing
 * that turned out not to be trustworthy. `slot` is OUR variable. It is zero until a camera has
 * passed every check in camera_read(), and it goes back to zero the moment one stops passing.
 * A stub that finds zero there falls through and does nothing, which is the unmodified game.
 *
 * `on_change` may be NULL; when given, it is called on first validation and on every viewport
 * change, for logging. */
bool camera_track(unsigned interval_ms, volatile uintptr_t *slot,
                  camera_watch_callback_t on_change);

#endif /* COMMON_CAMERA_H */

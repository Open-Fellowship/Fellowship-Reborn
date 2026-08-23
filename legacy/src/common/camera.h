/* camera.h: read the active camera, or refuse.
 *
 * NEVER TRUST THE ENGINE'S CAMERA POINTER AND NEVER TRUST ITS FIELDS. It is documented as
 * NULL-outside-a-level and that does not hold on every machine; a second install reported a
 * horizontal field of view of 180 degrees through it. Everything here validates before it
 * returns, and callers treat "no trustworthy camera" as "behave like the unmodded game".
 *
 * Engine memory must not be dereferenced from a generated stub on a hot path: a stub cannot
 * check anything cheaply and cannot report what it found. See README.md.
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

/* The same poll, publishing a validated POINTER instead of a number, for values that cannot be
 * sampled onto a timer. `slot` is the caller's own variable: zero until a camera has passed every
 * check, zero again the moment one stops passing, and a stub that finds zero falls through and
 * does nothing. `on_change` may be NULL. See README.md. */
bool camera_track(unsigned interval_ms, volatile uintptr_t *slot,
                  camera_watch_callback_t on_change);

#endif /* COMMON_CAMERA_H */

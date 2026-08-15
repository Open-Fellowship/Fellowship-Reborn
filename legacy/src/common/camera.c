#include "common/camera.h"

#include "common/engine_sites.h"
#include "common/host_image.h"
#include "common/memory.h"

#include <windows.h>

#include <string.h>

/* The camera structure is read as far out as +0x258, so the whole span is checked once rather
 * than each field being hoped for individually. */
#define CAMERA_SPAN 0x260u

/* Plausible bounds. These are deliberately generous - the job is to reject uninitialised memory
 * and stale pointers, not to second-guess someone's monitor. 64x64 is below the engine's own
 * 640x480 floor and 32768 is past any display that exists, so anything in between is allowed. */
#define DIMENSION_MIN 64
#define DIMENSION_MAX 32768

/* halfW is 64.0 on every camera this engine builds, because the virtual screen is 128 units
 * wide. halfH is 64.0 * H / W, so 6.4 covers 20:1 and 640 covers 1:10. */
#define HALF_MIN   1.0f
#define HALF_MAX   4096.0f

/* focal = NUM / tan(fov * pi/360). At NUM 64 that is 32.5 at a 90-degree field and 3667 at
 * one degree. Outside 1 .. 1e6 nothing sane produced it. */
#define FOCAL_MIN  1.0f
#define FOCAL_MAX  1000000.0f

static camera_watch_callback_t g_callback;
static unsigned                g_interval_ms = 250;
static volatile uintptr_t     *g_slot;

/* isfinite() is not reliably available in every C dialect this may be compiled under, and a
 * comparison against a NaN silently answers false to everything, so the exponent is checked
 * directly. All ones means infinity or NaN; nothing else does. */
static bool float_in_range(float value, float low, float high)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    if (((bits >> 23) & 0xFFu) == 0xFFu) {
        return false;
    }
    return value >= low && value <= high;
}

static bool dimension_ok(int32_t value)
{
    return value >= DIMENSION_MIN && value <= DIMENSION_MAX;
}

/* The first dword of an object with virtual functions is its vtable pointer, and a vtable both
 * lives in the host image and holds addresses in the host image. Two indirections, both checked.
 *
 * Checking against one known vtable address would be stronger, and is wrong: the engine has more
 * than one camera class and only one of them was ever dumped. "Points at a table of code
 * addresses inside Fellowship.exe" is the strongest claim that is true of all of them, and it
 * already rejects every value uninitialised memory is likely to hold. */
static bool looks_like_an_object(uintptr_t object)
{
    uint32_t vtable;
    uint32_t first_method;

    if (!memory_read_u32(object, &vtable)) {
        return false;
    }
    if (!memory_is_inside_image((uintptr_t)vtable, sizeof(uint32_t) * 4u)) {
        return false;
    }
    if (!memory_read_u32((uintptr_t)vtable, &first_method)) {
        return false;
    }
    return memory_is_inside_image((uintptr_t)first_method, 1u);
}

/* halfH/halfW against height/width, with a factor of two either way. Written as two
 * multiplications so that neither a zero dimension nor a denormal half can divide. */
static bool aspect_agrees(const camera_view_t *view, int32_t width, int32_t height)
{
    float claimed = view->half_h * (float)width;
    float actual  = view->half_w * (float)height;

    return claimed >= actual * 0.5f && claimed <= actual * 2.0f;
}

bool camera_read(camera_view_t *out)
{
    camera_view_t view;
    uint32_t      pointer;

    memset(&view, 0, sizeof(view));

    if (!memory_read_u32(exe_site(EXE_ACTIVE_CAMERA_PTR), &pointer) || pointer == 0) {
        return false;
    }
    if (!memory_is_readable_range((uintptr_t)pointer, CAMERA_SPAN)) {
        return false;
    }
    if (!looks_like_an_object((uintptr_t)pointer)) {
        return false;
    }

    view.object = (uintptr_t)pointer;
    memory_read(view.object + CAMERA_VIEWPORT_W, &view.viewport_width,  sizeof(int32_t));
    memory_read(view.object + CAMERA_VIEWPORT_H, &view.viewport_height, sizeof(int32_t));
    memory_read(view.object + CAMERA_DEVICE_W,   &view.device_width,    sizeof(int32_t));
    memory_read(view.object + CAMERA_DEVICE_H,   &view.device_height,   sizeof(int32_t));
    memory_read(view.object + CAMERA_HALF_W,     &view.half_w,          sizeof(float));
    memory_read(view.object + CAMERA_HALF_H,     &view.half_h,          sizeof(float));
    memory_read(view.object + CAMERA_FOCAL,      &view.focal,           sizeof(float));

    if (!dimension_ok(view.viewport_width) || !dimension_ok(view.viewport_height)) {
        return false;
    }
    if (!dimension_ok(view.device_width) || !dimension_ok(view.device_height)) {
        return false;
    }
    if (!float_in_range(view.half_w, HALF_MIN, HALF_MAX)) {
        return false;
    }
    if (!float_in_range(view.half_h, HALF_MIN, HALF_MAX)) {
        return false;
    }
    if (!float_in_range(view.focal, FOCAL_MIN, FOCAL_MAX)) {
        return false;
    }

    /* A last cross-check no individual range can make: halfH/halfW IS the aspect ratio, so it has
     * to agree with the rectangle the camera claims to be rendering into. A camera caught half
     * way through SetViewport - old dimensions, new halves, or the reverse - passes every test
     * above and fails this one.
     *
     * Either rectangle is accepted, because the viewport and the device disagree legitimately
     * whenever the game renders into a sub-rect, and a factor of two of slack is left in on top:
     * this is here to reject garbage, not to police a rounding difference. */
    if (!aspect_agrees(&view, view.viewport_width, view.viewport_height)
        && !aspect_agrees(&view, view.device_width, view.device_height)) {
        return false;
    }

    *out = view;
    return true;
}

static DWORD WINAPI watch_thread(LPVOID parameter)
{
    camera_view_t last;
    bool          seen = false;

    (void)parameter;
    memset(&last, 0, sizeof(last));

    for (;;) {
        camera_view_t view;

        if (camera_read(&view)) {
            /* Publish first, announce second. A stub reading the slot wants the pointer as soon
             * as it is good; nothing is waiting on the log line. */
            if (g_slot != NULL) {
                *g_slot = view.object;
            }
            if (g_callback != NULL
                && (!seen
                    || view.viewport_width  != last.viewport_width
                    || view.viewport_height != last.viewport_height)) {
                seen = true;
                last = view;
                g_callback(&view);
            }
        } else if (g_slot != NULL) {
            /* Withdrawn the moment it stops being believable, so a stub can never be holding a
             * pointer this file would refuse today. */
            *g_slot = 0;
        }
        Sleep(g_interval_ms);
    }

    /* Not reached: the thread lives as long as the process. There is no result to hand back and
     * nothing to release, and a plugin DLL is never unloaded. The return is here because the
     * signature demands one, not because control can arrive at it. */
    return 0;
}

bool camera_watch(unsigned interval_ms, camera_watch_callback_t on_change)
{
    if (on_change == NULL) {
        return false;
    }
    return camera_track(interval_ms, NULL, on_change);
}

bool camera_track(unsigned interval_ms, volatile uintptr_t *slot,
                  camera_watch_callback_t on_change)
{
    HANDLE thread;

    if (slot == NULL && on_change == NULL) {
        return false;
    }
    g_slot     = slot;
    g_callback = on_change;
    if (interval_ms >= 50u && interval_ms <= 5000u) {
        g_interval_ms = interval_ms;
    }

    thread = CreateThread(NULL, 0, watch_thread, NULL, 0, NULL);
    if (thread == NULL) {
        return false;
    }
    CloseHandle(thread);
    return true;
}

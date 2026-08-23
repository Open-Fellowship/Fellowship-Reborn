#include "field_of_view.h"
#include "common/compiler.h"

#include "common/camera.h"
#include "common/channel.h"
#include "common/engine_sites.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"

#include <windows.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#define PLUGIN_SECTION "field_of_view"

/* The aspect the game was authored for. The 4:3-equivalent vertical field of a 70-degree
 * horizontal is 55.4129 degrees, and that is the number this plugin reproduces at every aspect
 * when VerticalFOV is left at 0. */
#define AUTHORED_ASPECT 0.75

/* Below this, the camera is not the world camera. The inventory renders its item models through
 * the same camera object at the item's own ModelFOV, 20 degrees for the ones measured, and
 * writing the world's focal length over that would put every icon in the wrong place. */
#define WORLD_FOV_FLOOR 40.0

/* Above this, it is not a field of view either. This ceiling is the lesson from the crash log
 * that prompted the rewrite:
 *
 *     [field_of_view] baseline focal 76.2722, horizontal 180.000 deg
 *     [field_of_view] applied: focal 76.2722 -> 347937712601931479777280.0000
 *
 * 2*atan(halfW/focal) reaching exactly 180 degrees means halfW came back astronomical, so what
 * was being read was not a camera. The old code had a floor and no ceiling, so a saturated angle
 * sailed through, was latched as the baseline, permanently, it never re-sampled, and a focal
 * length of 3.5e23 went into the projection matrix. The whole span between the floor and this
 * ceiling is a real field of view; nothing outside it is. */
#define WORLD_FOV_CEILING 170.0

/* A focal length this plugin is willing to write. At halfW 64 these are fields of view of about
 * 179 and 0.07 degrees, so the bound rejects arithmetic accidents without rejecting anything a
 * person could mean. */
#define FOCAL_MIN 1.0
#define FOCAL_MAX 100000.0

static double g_target_vertical;    /* degrees; 0 until the baseline has been sampled */
static channel_block_t *g_channel;  /* dev_menu's request, when there is one */
static double g_announced_request;  /* so a slider drag does not fill the log */
static float  g_baseline_focal;
static DWORD  g_interval_ms = 400;

/* While dev_menu's slider is actually asking for a value, this thread has to keep up with a
 * dragging hand rather than with a setting that changes once a session. 400 ms of that is a
 * picture that lurches twice a second; 16 is a picture that follows the knob.
 *
 * It costs nothing when nobody is dragging, because the fast interval is only used while the
 * channel holds a request. The work per tick is a camera validation and one float write. */
#define SLIDER_INTERVAL_MS 16u

static bool g_slider_active;
static bool   g_complained;         /* one refusal message per run, not one per tick */

static double to_degrees(double radians) { return radians * 180.0 / 3.14159265358979323846; }
static double to_radians(double degrees) { return degrees * 3.14159265358979323846 / 180.0; }

/* Every term SetViewport derives from focal, written together. Writing focal on its own changes
 * nothing on screen: the renderer reads projX and projY, and those are only recomputed when the
 * viewport is rebuilt. */
static bool write_focal(const camera_view_t *view, float focal)
{
    float negated = -focal;

    if (view->half_w == 0.0f || view->half_h == 0.0f) {
        return false;
    }
    if (!memory_make_writable(view->object + CAMERA_PROJ_X, 0x260)) {
        return false;
    }
    *(float *)(view->object + CAMERA_FOCAL)     = focal;
    *(float *)(view->object + CAMERA_NEG_FOCAL) = negated;
    *(float *)(view->object + CAMERA_PROJ_X)    = negated / view->half_w;
    *(float *)(view->object + CAMERA_PROJ_Y)    = negated / view->half_h;
    return true;
}

static void complain_once(const char *what, double value)
{
    if (!g_complained) {
        g_complained = true;
        log_warning("%s (%.4f), leaving the field of view alone. If this is the only line this "
                    "plugin ever prints, set Enabled=0; it means the camera on this machine is "
                    "not where the plugin expects it.", what, value);
    }
}

OF_NORETURN_THREAD_BEGIN
static DWORD WINAPI poll_thread(LPVOID parameter)  /* never returns */
{
    bool announced = false;

    (void)parameter;

    for (;;) {
        camera_view_t view;

        /* camera_read() is the gate. It refuses a null pointer, a pointer that does not look
         * like an object, a structure that is not fully readable, dimensions outside 64..32768,
         * halves and a focal length outside their plausible ranges, and an aspect ratio that
         * disagrees with the rectangle the camera says it is rendering into. Everything below
         * this line is arithmetic on numbers that have already been believed. */
        if (camera_read(&view)) {
            double horizontal = 2.0 * to_degrees(atan((double)view.half_w / (double)view.focal));

            if (horizontal >= WORLD_FOV_FLOOR && horizontal <= WORLD_FOV_CEILING) {
                if (g_baseline_focal == 0.0f) {
                    /* Sampled once, from the game's own value, and never re-sampled: deriving
                     * the target from the current focal after we have already written it would
                     * compound on every tick. Which is exactly why the sample has to be
                     * trustworthy; a bad one is permanent. */
                    double target = g_target_vertical;

                    if (target <= 0.0) {
                        target = 2.0 * to_degrees(
                            atan(AUTHORED_ASPECT * (double)view.half_w / (double)view.focal));
                    }
                    if (target < 1.0 || target > 179.0) {
                        complain_once("the vertical field of view worked out impossible", target);
                        Sleep(g_interval_ms);
                        continue;
                    }

                    g_baseline_focal  = view.focal;
                    g_target_vertical = target;
                    log_info("baseline focal %.4f, horizontal %.3f deg -> holding vertical at "
                             "%.4f deg", (double)view.focal, horizontal, g_target_vertical);
                }

                {
                    /* dev_menu's slider outranks the ini and the automatic value, and only while
                     * it is asking. Release it there and this falls straight back to whatever
                     * was chosen at install time, no restart, and no second writer: the slider
                     * publishes a number, this plugin remains the only thing that writes the
                     * camera. */
                    double target = g_target_vertical;
                    float  requested;

                    g_slider_active = channel_read_field_of_view(g_channel, &requested) != false;

                    if (g_slider_active) {
                        target = (double)requested;
                        if (fabs(target - g_announced_request) > 0.5) {
                            g_announced_request = target;
                            log_info("target %.4f deg (from dev_menu)", target);
                        }
                    } else if (g_announced_request != 0.0) {
                        g_announced_request = 0.0;
                        log_info("target %.4f deg (dev_menu released it)", g_target_vertical);
                    }

                    {
                    double wanted = (double)view.half_h / tan(to_radians(target) * 0.5);

                    /* The last gate, and not redundant: the target is sane and half_h is sane,
                     * and a focal length still has to be checked, because it is the number that
                     * actually reaches the projection matrix. */
                    if (!(wanted >= FOCAL_MIN && wanted <= FOCAL_MAX)) {
                        complain_once("the focal length worked out impossible", wanted);
                    } else if (fabs(wanted - (double)view.focal) > (double)view.focal * 0.001) {
                        if (write_focal(&view, (float)wanted) && !announced) {
                            announced = true;
                            log_info("applied: focal %.4f -> %.4f, horizontal %.3f -> %.3f deg",
                                     (double)view.focal, wanted, horizontal,
                                     2.0 * to_degrees(atan((double)view.half_w / wanted)));
                        }
                    }
                    }
                }
            }
        }
        Sleep(g_slider_active ? SLIDER_INTERVAL_MS : g_interval_ms);
    }

    /* Not reached. The thread lives for as long as the process: there is nothing to tidy up and
     * nobody to hand a result to. */
    return 0;
}
OF_NORETURN_THREAD_END

void field_of_view_install(void)
{
    HANDLE thread;
    float  configured;
    int32_t interval;

    log_init(PLUGIN_SECTION, false);

    if (!ini_read_bool(PLUGIN_SECTION, "Enabled", false)) {
        log_info("Enabled=0, the field of view is left as the game sets it");
        return;
    }
    if (!host_image_resolve()) {
        log_error("the host image could not be resolved; refusing to touch anything");
        return;
    }

    configured = ini_read_float(PLUGIN_SECTION, "VerticalFOV", 0.0f);
    if (configured > 1.0f && configured < 179.0f) {
        g_target_vertical = (double)configured;
        log_info("holding the vertical field of view at %.4f deg (from the ini)",
                 g_target_vertical);
    } else if (configured != 0.0f) {
        log_warning("VerticalFOV=%g is outside 1..179, falling back to automatic",
                    (double)configured);
    }

    /* Opened whether or not dev_menu is installed. With no partner this is a block nobody ever
     * writes to, and channel_read_field_of_view keeps answering false. */
    g_channel = channel_open();

    interval = ini_read_int(PLUGIN_SECTION, "IntervalMs", 400);
    if (interval >= 50 && interval <= 5000) {
        g_interval_ms = (DWORD)interval;
    }

    /* A thread rather than a hook. The value has to be re-applied after every level load and
     * after anything else that rebuilds the viewport, and there is no single engine function
     * that means "the viewport was rebuilt" without also meaning several other things. */
    thread = CreateThread(NULL, 0, poll_thread, NULL, 0, NULL);
    if (thread == NULL) {
        log_error("could not start the poll thread");
        return;
    }
    CloseHandle(thread);

    log_info("installed, re-applying every %u ms, and every %u while dev_menu's slider is asking",
             (unsigned)g_interval_ms, (unsigned)SLIDER_INTERVAL_MS);
}

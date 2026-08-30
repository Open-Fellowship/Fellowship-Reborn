#include "texture_scaling.h"

#include "common/camera.h"
#include "common/emit.h"
#include "common/engine_sites.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/module_watch.h"
#include "common/patch.h"
#include "common/trampoline.h"

#include <windows.h>

#include <stdint.h>
#include <string.h>

#define PLUGIN_SECTION "texture_scaling"

/* The mouse pointer is a GUIControl_Texture, and every one of them draws its art at the size of
 * its source rectangle in texels. At 3840x2160 the pointer is 32x32 device pixels while its own
 * data asks for 20% of the screen.
 *
 * The engine already has the mechanism to fix this and simply never uses it. Texture::Render, in
 * Fellowship.exe at 0x0043F1E0, takes a destination scale pair as its last two arguments and
 * computes the drawn extent as source * scale:
 *
 *     0043F391  fmul [esp+0xf8]     destination width  = source width  * scaleX
 *     0043F3AA  fmul [esp+0xfc]     destination height = source height * scaleY
 *
 * The pair comes from the control at +0x78 and +0x7C. The constructor writes 1.0 to both and
 * only the save slot thumbnails ever call SetScale, so every other control is 1:1 for ever.
 *
 * So there is nothing to patch in the draw. Find the pointer control, write the scale into the
 * two floats the engine already reads, and the art scales with the filtering switched on for
 * free, because Texture::Render selects that on whether the pair is exactly 1.0. */

/* The GUI manager storing its pointer control. ebp holds the object, or zero when the
 * construction above it failed, and six bytes is the whole instruction. */
#define CURSOR_RVA        0x67083u
#define CURSOR_RETURN_RVA 0x67089u
#define CURSOR_SIZE       6u

static const uint8_t cursor_expected[CURSOR_SIZE] = {
    0x89, 0xAE, 0x90, 0x00, 0x00, 0x00   /* mov [esi+0x90], ebp */
};

#define CONTROL_SCALE_X 0x78u
#define CONTROL_SCALE_Y 0x7Cu

/* SECOND SITE, a different class and a different mechanism with the same disease.
 *
 * FUN_1007B1A0 is slot 21 of the HUD Texture family, which draws the small circle under the
 * health bar. Its draw computes the scale as destination over source and takes the destination
 * from +0x40 and +0x44, so correcting those corrects the quad. That was never true on the
 * pointer path, where the source and the destination were the same number.
 *
 * The fallback below runs when the authored size is 1.0 or less, which for these objects it
 * always is, and copies the texture texel dimensions into the on-screen size:
 *
 *     1007B2A1  fxch st(1)             st0 = width, st1 = height
 *     1007B2A3  fstp [edi+0x40]        on-screen WIDTH  := texel width
 *     1007B2A6  fstp [edi+0x44]        on-screen HEIGHT := texel height
 *
 * MULTIPLY, never delete. Removing the copy leaves the authored size, which is zero for these,
 * and the element disappears instead of scaling. */
#define HUD_RVA        0x7B2A3u
#define HUD_RETURN_RVA 0x7B2A9u
#define HUD_SIZE       6u

static const uint8_t hud_expected[HUD_SIZE] = {
    0xD9, 0x5F, 0x40,        /* fstp [edi+0x40] */
    0xD9, 0x5F, 0x44         /* fstp [edi+0x44] */
};

/* The unscaled texel dimensions, captured by the stub before it multiplies, so the poll can
 * re-derive, never compound. */
static float g_hud_base_w = 1.0f;
static float g_hud_base_h = 1.0f;

/* Live scale, 1.0 until a camera validates. The stub multiplies by these, so a HUD built before
 * the camera is ready is left alone and the poll corrects it afterwards. */
static float g_scale_x = 1.0f;
static float g_scale_y = 1.0f;


#define HUD_ROWS 8

typedef struct hud_entry {
    uintptr_t control;
    float     base_w;
    float     base_h;
} hud_entry_t;

static hud_entry_t   g_hud[HUD_ROWS];
static volatile LONG g_hud_count;

/* THIRD SITE, the One Ring icon under the purple bar. Its own class, its own setup, and the same
 * disease a third time.
 *
 * FUN_1007ABB0 reads RingXSize and RingYSize, properties 26 and 27, both authored in texels with
 * a default of 64, and stores them straight into the on-screen size. Its draw re-reads the source
 * from the property table on every frame and takes only the destination from +0x40 and +0x44, so
 * correcting the setup is sufficient, exactly as it is for the HUD Texture family.
 *
 *     1007ACA1  mov [edi+0x44], ecx    on-screen HEIGHT := texel height
 *     1007ACA4  mov ecx, edi           the this for the call at 1007ACA9
 *     1007ACA6  mov [edi+0x40], eax    on-screen WIDTH  := texel width
 *
 * Eight bytes, and the middle instruction is not decoration: leaving it out sends the call below
 * a stale this. These are integer moves of float bit patterns, so the stub routes them through
 * the FPU, and does not multiply integers. */
#define RING_RVA        0x7ACA1u
#define RING_RETURN_RVA 0x7ACA9u
#define RING_SIZE       8u

static const uint8_t ring_expected[RING_SIZE] = {
    0x89, 0x4F, 0x44,        /* mov [edi+0x44], ecx */
    0x8B, 0xCF,              /* mov ecx, edi        */
    0x89, 0x47, 0x40         /* mov [edi+0x40], eax */
};

/* FOURTH SITE, the health bar and the purple ring meter, and it is not a fourth mechanism. The
 * bars reach the SAME Texture::Render as the mouse pointer, and hand it a scale pair nailed to
 * one:
 *
 *     10079356  push 0xbf800000     arg10 = -1.0, the "use the X scale for Y" sentinel
 *     10079377  push 0x3f800000     arg9  =  1.0
 *
 * That is why the bar height never moves. It also explains the shape of the old measurement: the
 * width grew 104 to 598 at 4K, a ratio of 5.75, because a bar's LENGTH comes from its value and
 * its container, while the thickness comes through this scale and this scale is 1.0.
 *
 * So only arg10 is replaced, and arg9 is left at 1.0. Scaling X here as well would multiply a
 * width that already carries the resolution.
 *
 * EIGHT sites, and the eighth is the one that shows. The other seven are the frame and its
 * pieces, in the three draw slots both bar classes share. rfl+79200 is in FUN_100791C0, the
 * variable meter's OWN slot, and it draws the coloured fill. Patching the seven without it gives
 * a correctly thick frame with a thin green line sitting inside it, which is exactly what it
 * looked like.
 *
 * Each is a whole five byte push, which is exactly the size of a jump. No instruction is split
 * and none has a neighbour that has to be carried across.
 *
 * ATTEMPTED AND DISPROVED FIRST: scaling [edi+0x44] in the setup at 100790E8. The hook installed,
 * the arithmetic was right, and the log proved it: incoming 18.0, written 81.0, and nothing on
 * screen changed. That field is not what the bar draws from. */
static const uint32_t bar_scale_sites[] = {
    /* rfl+79200 was here, and it does not belong. FUN_100791C0 is not a draw, it forwards to
     * FUN_10078CA0, and the -1.0 at that site is argument four of the fill rather than a render
     * scale. Passing the height ratio there drove both bars to a value width of exactly 27
     * against a track of 600, which read as a bar stuck near empty. */
    0x79356u, 0x793B5u, 0x793FBu,       /* FUN_10079230, the frame         */
    0x79592u, 0x795FCu, 0x79649u,       /* FUN_10079450                    */
    0x797E4u                            /* FUN_100796A0                    */
};

static const uint8_t bar_scale_expected[5] = { 0x68, 0x00, 0x00, 0x80, 0xBF };

/* SEVENTH SITE, the map screen's indicator circle and its stars.
 *
 * Map GUI is 30 properties, and its geometry sits at class indices 19 to 26: the indicator at
 * 121 by 118 texels and a star at 19 by 19. Those are source rectangle texels and are left alone.
 * The map's own corner textures already fill the screen, which is why only the icons on top of it
 * look wrong.
 *
 * Both draws hand nine arguments to slot +0x58, read live off the stack rather than guessed:
 *
 *     arg1 637.000   arg2 392.406      where it goes, in screen pixels
 *     arg3 135.000   arg4   3.000      where it comes from in the atlas
 *     arg5 121.000   arg6 118.000      how big it is, in texels
 *     arg8   1.000   arg9  -1.000      the X scale, and Y taking its value from X
 *
 * The extent is the source multiplied by the scale, grown from arg1 and arg2 as the top left
 * corner. So scaling alone moves an icon's centre by half its growth, and the circle and the star
 * differ hugely in size, 121 against 19, which pushed them about 177 pixels apart at 4.5 while
 * both were "correctly" scaled. That was measured by holding this patch off: with it off, the
 * circle sits on the star.
 *
 * So the scale and the position are set together, at the call, where every argument is already on
 * the stack:
 *
 *     x -= w * (k - 1) / 2
 *     y -= h * (k - 1) / 2
 *
 * which grows each icon about its own centre and leaves the two of them on top of each other at
 * any resolution.
 *
 *     1002d66b  mov ecx,ebp / push edx / call [eax+0x58]     the star, six bytes
 *     1002d6ef  mov ecx,ebp / call [edx+0x58]                the indicator, five
 *
 * The star pushes its last argument after the mov, so in both cases the adjustment happens once
 * everything is on the stack.
 *
 * Confirmed from two directions before anything was written. A byte scan for the property reads
 * put the strongest candidate at 1002D4E4 to 1002D6AF, and hud_probe then recorded rfl+2D6BE,
 * 2D69A, 2D528 and 2D517 reading indices 19, 21, 25 and 26 on a 30 property object, 295 hits
 * each, which is once a frame while the map is open. */
#define MAP_STAR_RVA        0x2D66Bu
#define MAP_STAR_RETURN_RVA 0x2D671u
#define MAP_STAR_SIZE       6u

#define MAP_IND_RVA         0x2D6EFu
#define MAP_IND_RETURN_RVA  0x2D6F4u
#define MAP_IND_SIZE        5u

static const uint8_t map_star_expected[MAP_STAR_SIZE] = {
    0x8B, 0xCD,                  /* mov ecx,ebp      */
    0x52,                        /* push edx         */
    0xFF, 0x50, 0x58             /* call [eax+0x58]  */
};

static const uint8_t map_ind_expected[MAP_IND_SIZE] = {
    0x8B, 0xCD,                  /* mov ecx,ebp      */
    0xFF, 0x52, 0x58             /* call [edx+0x58]  */
};

/* Sets the scale and pulls the destination back by half the growth, so the icon grows about its
 * centre. Declines anything that is not the argument list this was measured against. */
static void __cdecl centre_map_icon(uintptr_t args)
{
    float a[9];
    float k = g_scale_y;

    if (k <= 1.0f || !memory_is_readable_range(args, sizeof(a))) {
        return;
    }
    memcpy(a, (const void *)args, sizeof(a));

    /* arg8 is the X scale and arg9 the sentinel that makes Y follow it. */
    if (a[7] != 1.0f || a[8] != -1.0f || a[4] <= 0.0f || a[5] <= 0.0f) {
        return;
    }
    a[0] -= a[4] * (k - 1.0f) * 0.5f;
    a[1] -= a[5] * (k - 1.0f) * 0.5f;
    a[7]  = k;

    if (memory_make_writable(args, sizeof(a))) {
        memcpy((void *)args, a, sizeof(a));
    }
}


/* EIGHTH SITE, the save and load slot pictures.
 *
 * LoadSave GUI is 53 properties and its icon geometry is class indices 28 to 31, 64 by 64
 * texels. Two places read them, measured: rfl+73C2F and its neighbours five times, once per save
 * slot, and rfl+7386C once for the New Save entry. Both then set the scale on the picture:
 *
 *     10073cc9  mov eax,[esp+0x14]     an aspect ratio, worked out above
 *     10073ccd  push 0x3f800000        y, and it is a hard 1.0
 *     10073cd2  push eax               x
 *     10073ce0  call 1006C730          SetScale(x, y)
 *
 * This is the one place in the game that writes that scale pair itself rather than leaving the
 * constructor's 1.0 in place, so the picture is corrected for aspect and not for resolution.
 *
 * Measured with this patch held off, the game draws the picture like this:
 *
 *     source 113.78 x 64.00   scale 1.7778 x 1.0000   drawn 202.3 x 64.0
 *
 * The saved thumbnail is 64 texels square. The game works out the viewport aspect and applies it
 * to the SOURCE rectangle, 64 * 1.7778 giving 113.78, and then hands the same ratio to SetScale
 * as well. Two things are wrong with that. The ratio lands twice, and widening the source makes
 * it sample fifty texels past the edge of a 64 texel texture, so half the picture is nothing at
 * all. Drawing it wider only stretches the empty part: with the source left alone at 113.78 the
 * picture came out 512 wide with 288 of content in it, and 288 is 64 * 4.5.
 *
 * The widening cannot simply be removed. Tried, at both of its sites, and the pictures vanished
 * altogether: that value feeds more than the source rectangle.
 *
 * So the source is left exactly as the game builds it and the ratio goes back onto the scale,
 * where it belongs:
 *
 *     scale = (ratio * k, k)
 *
 * The picture is then drawn at 64 * ratio * k by 64 * k, which is 512 x 288 here, and that is
 * what it measured on screen. The quad reaches further than that, out to 113.78 * ratio * k,
 * but there is no texture past 64 texels so none of it is picture.
 *
 * Which is why the layout box is worked out from the height rather than the width. The saved
 * thumbnail is square, 64 by 64, so its height is the true extent of the art on both axes, and
 * sizing the row from the widened source reserved room for emptiness. k is the height ratio, and
 * 288 of 2160 is 13.3 per cent, which is what the stock game's own rows measure.
 *
 * Nine bytes, both instructions relocated. The load reads through esp and the stub has not
 * pushed anything at that point, so the offset still means what it did. */
static const uint32_t save_icon_sites[] = {
    0x73916u,       /* the New Save entry */
    0x73CC9u        /* once per save slot */
};

#define SAVE_ICON_SIZE 9u

static const uint8_t save_icon_expected[SAVE_ICON_SIZE] = {
    0x8B, 0x44, 0x24, 0x14,          /* mov eax,[esp+0x14]  */
    0x68, 0x00, 0x00, 0x80, 0x3F     /* push 1.0            */
};

/* No spare register at the site, so the scaled ratio goes out to memory and back. */
static float g_save_temp = 1.0f;

/* FUN_1006C890 is slot +0x5C of the GUIControl_Texture vtable at 100F0668, and the README's byte
 * scan found exactly one reference to it in the whole image, so it belongs to this class alone.
 * The hook goes at the point it builds its rectangle rather than at its entry:
 *
 *     1006c909  fld  [esi+0x3c]        the position
 *     1006c90c  fadd [esi+0x74]        plus the source height
 *
 * esi is the control. The entry is too early by one call: the position is resolved by the call
 * at 1006c8c2, so a clamp at the entry works from a position up to a frame stale. Measured, a
 * picture clamped for y 1367.2 was drawn at 1385.6 and reached eighteen pixels past the list. */
#define DRAW_RVA        0x6C909u
#define DRAW_RETURN_RVA 0x6C90Fu
#define DRAW_SIZE       6u

static const uint8_t draw_expected[DRAW_SIZE] = {
    0xD9, 0x46, 0x3C,                    /* fld  [esi+0x3c]  */
    0xD8, 0x46, 0x74                     /* fadd [esi+0x74]  */
};

/* The picture controls, so the layout box can be grown where the texel size lands. Held loosely
 * and every read guarded, the same as the objective boxes. */
#define SAVE_ROWS 128

typedef struct save_entry {
    uintptr_t control;
    float     base_w;      /* the drawn size, once the source is known */
    float     base_h;
    float     src_h;       /* the source height it was derived from */
} save_entry_t;

static save_entry_t g_save[SAVE_ROWS];
static volatile LONG g_save_seen;

/* The list clips its text to itself but not this picture, so a row hanging over the bottom edge
 * drew outside the list. Rows are much taller now, so they no longer divide evenly into the list
 * and one is usually partial.
 *
 * The bottom edge is read from the list every time, through the control's own parent chain, so
 * this holds at any resolution and any number of rows. Nothing here is tuned. The source is
 * cropped by the same fraction as the height, so the picture is cut off rather than squashed.
 *
 * This runs from the draw rather than the poll. On the poll it was a quarter second behind, which
 * showed as the picture clipping wrongly for a few frames while the list was being scrolled. */
static void __cdecl clamp_save_picture(uintptr_t control)
{
    LONG seen = g_save_seen;
    LONG i;

    if (control == 0 || !memory_is_readable_range(control, 0x80u)) {
        return;
    }
    if (seen > SAVE_ROWS) {
        seen = SAVE_ROWS;
    }

    for (i = 0; i < seen; ++i) {
        uintptr_t row  = 0;
        uintptr_t list = 0;
        float     want_h;
        float     want_src;

        if (g_save[i].control != control || g_save[i].base_h <= 0.0f) {
            continue;
        }
        want_h   = g_save[i].base_h;
        want_src = g_save[i].src_h;

        memcpy(&row, (const void *)(control + 0x5Cu), sizeof(uint32_t));
        if (row != 0 && memory_is_readable_range(row, 0x60u)) {
            memcpy(&list, (const void *)(row + 0x5Cu), sizeof(uint32_t));
        }
        if (list != 0 && memory_is_readable_range(list, 0x48u)) {
            float ly    = 0.0f;
            float lh    = 0.0f;
            float sy    = 0.0f;

            memcpy(&ly,    (const void *)(list + 0x3Cu), sizeof(ly));
            memcpy(&lh,    (const void *)(list + 0x44u), sizeof(lh));
            memcpy(&sy,    (const void *)(control + CONTROL_SCALE_Y), sizeof(sy));

            /* The picture's own y, read and never written. An earlier version took the top
             * from the row and moved the picture to suit, which drifted: the next frame read
             * back the value this had just written. It also assumed the picture sat exactly on
             * its row, which one sample happened to show and the rest do not. Measured, it sits
             * about 183 below, so clipping from the row's y cut in far too late.
             *
             * Only the bottom edge is clipped, because that needs no move: the height shrinks
             * and the source shrinks with it, so the picture is cut off rather than squashed,
             * and its position is left entirely to the engine. */
            if (lh > 0.0f && sy > 0.0f) {
                float top = 0.0f;

                memcpy(&top, (const void *)(control + 0x3Cu), sizeof(top));
                {
                    float room = (ly + lh) - top;

                    if (room < 0.0f) {
                        room = 0.0f;
                    }
                    if (room < want_h) {
                        want_h   = room;
                        want_src = want_h / sy;
                    }
                }
            }
        }

        /* A row entirely past an edge keeps nothing of itself: the source goes to zero as well,
         * so a stale rectangle cannot draw one more frame of picture. */
        if (want_h <= 0.0f) {
            want_h   = 0.0f;
            want_src = 0.0f;
        }
        if (memory_make_writable(control + 0x40u, 8u)) {
            memcpy((void *)(control + 0x40u), &g_save[i].base_w, sizeof(float));
            memcpy((void *)(control + 0x44u), &want_h, sizeof(float));
        }
        if (memory_make_writable(control + 0x74u, 4u)) {
            memcpy((void *)(control + 0x74u), &want_src, sizeof(float));
        }

        return;
    }
}

/* Scrolling rebuilds rows, so this is called again and again for controls that are already on
 * screen. A plain ring wrapped and dropped live ones, and the picture that fell out drew
 * unclipped for a frame or two, which is what the flicker down the edge of the list was.
 *
 * So a control already held keeps its slot and its measurements, and only genuinely new ones
 * take a slot, oldest first. */
static void __cdecl record_save_icon(uintptr_t control)
{
    LONG n;
    LONG i;

    if (control == 0) {
        return;
    }
    n = g_save_seen;
    if (n > SAVE_ROWS) {
        n = SAVE_ROWS;
    }
    for (i = 0; i < n; ++i) {
        if (g_save[i].control == control) {
            return;
        }
    }
    n = InterlockedIncrement(&g_save_seen) - 1;

    g_save[(uint32_t)n % SAVE_ROWS].control = control;
    g_save[(uint32_t)n % SAVE_ROWS].base_w  = 0.0f;
    g_save[(uint32_t)n % SAVE_ROWS].base_h  = 0.0f;
    g_save[(uint32_t)n % SAVE_ROWS].src_h   = 0.0f;
}

/* The coloured fill, which is a different draw from the seven above.
 *
 * FUN_10078CA0 builds the fill's quad and hands it to FUN_10066600. Both the arguments and the
 * controls' own fields were read live rather than worked out from a decompile:
 *
 *     health quad     x 122.20   y 115.00   w  27.00   h  6.00
 *     health box  +38 x 115.20  +3C 108.00  +40 613.00  +44 18.00
 *
 * The fill is inset 7 from the top of its box and 6 high inside a box 18 high. That is centred,
 * and it stays centred at every resolution, so the layout is not the bug. The frame is rendered
 * four and a half times taller than the 18 its own box says it is, by the seven pushes above,
 * and a correctly placed fill inside an oversized frame reads as one pinned to the top edge.
 *
 * The height and the offset are both scaled at rfl+667A3, where the height is loaded for the
 * draw. build_fill_scale_stub has the arithmetic, and why it needs no register.
 *
 * Eight callers draw filled rectangles through FUN_10066600, and scaling all of them would reach
 * menu backgrounds and screen fades, so the two calls that draw a bar raise a flag around
 * themselves and the scaling reads it. Every other caller is left alone.
 *
 * The scale pair inside that draw looks like the obvious lever and is not. A positive ratio in
 * the Y slot gives the right thickness in the wrong place, and the same ratio negated gives the
 * right place at the old thickness. A flip would have given a thick bar either way, so the slot
 * holds a sentinel rather than a sign: every negative means what -1.0 means, which is to take Y
 * from X. That pair can never scale one axis alone. */
#define FILL_SCALE_RVA        0x667A3u
#define FILL_SCALE_RETURN_RVA 0x667A9u
#define FILL_SCALE_SIZE       6u

static const uint8_t fill_scale_expected[FILL_SCALE_SIZE] = {
    0xD9, 0x44, 0x24, 0x10,      /* fld dword ptr [esp+0x10] */
    0x8B, 0x3E                   /* mov edi,[esi]            */
};

/* Both bar calls are the same two instructions, and neither needs relocating anywhere awkward:
 *
 *     10078de7  mov ecx, edi
 *     10078de9  call 0x10066600
 *
 * The call is relative, so only the three bytes up to its displacement are worth checking. */
static const uint32_t fill_call_sites[] = { 0x78DE7u, 0x78E2Bu };

static const uint8_t fill_call_expected[3] = { 0x8B, 0xCF, 0xE8 };

#define FILL_CALL_SIZE  7u
#define FILL_CALLEE_RVA 0x66600u

/* Raised only while a bar fill is in the middle of being drawn. */
static uint32_t g_in_bar_fill;

/* The scale stub is building an argument list and has no register it may touch, so the flags go
 * out to memory and come back rather than riding the stack. */
static uint32_t g_fill_flags;


/* SIXTH SITE, the objective tick box, and it needs no arithmetic either, because it turns out
 * to be a GUIControl_Texture exactly like the pointer.
 *
 * The class is Quest HUD, 31 properties, and the box geometry is class indices 22 to 25 for the
 * unchecked icon and 26 to 29 for the completed one, every one of them (tx). Those are source
 * rectangle texels, so scaling the properties themselves would grow the sampled region and smear
 * the art, which is how the first two attempts listed at the bottom of the README failed. They
 * are left alone. The control is built here:
 *
 *     1003f592  push 0x80              a 0x80 byte control, so +0x78 and +0x7C are inside it
 *     1003f59b  call operator new
 *     1003f5d1  mov ecx,edi
 *     1003f5d3  call 1006C5D0          the GUIControl_Texture constructor
 *     1003f5d8  mov [esp+0x14],eax     the finished control
 *
 * Wrapping that call is five bytes and hands back the control the moment it exists. The scale
 * pair does the rest, as it does for the pointer, and filtering comes free the same way.
 *
 * This was measured rather than read. hud_probe, extended to record the object's property count
 * alongside the caller, caught rfl+3F57F reading index 24 on a 31 property object. Two earlier
 * guesses were wrong and both looked convincing: Quest GUI, whose 'texture info' group carries
 * properties actually named Unchecked-Box and Checked-Box, is initialised with defaults and then
 * never read by anything; and a run of indices 15 to 22 found by scanning bytes belonged to some
 * other class entirely. An index means nothing without the class that owns it. */
#define QUEST_RVA        0x3F5D3u
#define QUEST_RETURN_RVA 0x3F5D8u
#define QUEST_SIZE       5u
#define QUEST_CALLEE_RVA 0x6C5D0u

static const uint8_t quest_expected[QUEST_SIZE] = { 0xE8, 0xF8, 0xCF, 0x02, 0x00 };

/* And the layout box, which is a second site because the drawn size and the layout size are not
 * the same field. Scaling only the art left the objective's text starting where a 19 texel box
 * would have ended, on top of it.
 *
 * The layout box cannot be written when the control is built: measured there, +0x40 and +0x44
 * hold 3840 x 2160, the screen, not the art. FUN_1006C750 copies the texel size in afterwards:
 *
 *     1006c84f  mov ecx,[edi+0x70]     the source width, in texels
 *     1006c852  push 0                 belongs to the call at 1006c869
 *     1006c854  mov [edi+0x40],ecx     the layout box takes the texel count
 *     1006c857  mov edx,[edi+0x74]
 *     1006c85a  mov [edi+0x44],edx
 *
 * The twelve bytes after that copy are three plain moves, so the hook goes there rather than in
 * among the stores, where an interleaved push has caught this file out before. The row lays out
 * after this returns, which is what makes it early enough.
 *
 * This function belongs to every GUIControl_Texture, the mouse pointer included, so it acts only
 * on a control this plugin recorded being built for an objective line. */
#define QLAYOUT_RVA        0x6C85Du
#define QLAYOUT_RETURN_RVA 0x6C869u
#define QLAYOUT_SIZE       12u

static const uint8_t qlayout_expected[QLAYOUT_SIZE] = {
    0x8B, 0x06,                                      /* mov eax,[esi]                */
    0x8B, 0xCE,                                      /* mov ecx,esi                  */
    0xC7, 0x44, 0x24, 0x1C, 0xFF, 0xFF, 0xFF, 0xFF   /* mov [esp+0x1c],-1            */
};

/* One control per objective line, and the list is rebuilt whenever the objectives change, so
 * these are held loosely: a ring that overwrites the oldest, and every read guarded, because a
 * control from the previous list is freed memory. */
#define QUEST_ROWS 12

typedef struct quest_entry {
    uintptr_t control;
    float     base_w;      /* the layout box as the engine built it, zero until it is seen */
    float     base_h;
} quest_entry_t;

static quest_entry_t g_quest[QUEST_ROWS];
static volatile LONG g_quest_seen;
static volatile LONG g_quest_announced;

/* Written by the stub, read by the poll. Whatever the GUI manager last built. */
static volatile uintptr_t g_cursor;
static volatile uintptr_t g_camera;

static int32_t g_reference_width  = 640;
static int32_t g_reference_height = 480;

/* __cdecl, from the stub, after it has already stored the scaled values. Records the control and
 * the dimensions it was built from, so the poll can put the right numbers back when the camera
 * validates later than the HUD is built. */
static void __cdecl remember_hud(uintptr_t control)
{
    LONG count = g_hud_count;
    LONG i;

    if (control == 0) {
        return;
    }
    for (i = 0; i < count && i < HUD_ROWS; ++i) {
        if (g_hud[i].control == control) {
            g_hud[i].base_w = g_hud_base_w;
            g_hud[i].base_h = g_hud_base_h;
            return;
        }
    }
    if (count >= HUD_ROWS) {
        return;
    }
    g_hud[count].control = control;
    g_hud[count].base_w  = g_hud_base_w;
    g_hud[count].base_h  = g_hud_base_h;
    InterlockedExchange(&g_hud_count, count + 1);
    log_info("control %08X built from %.0f x %.0f texels",
             (unsigned)control, (double)g_hud_base_w, (double)g_hud_base_h);
}

/* Wraps one of the two bar fill calls so the shared draw can tell who is asking. The flag is
 * lowered on the way out, and `mov` leaves the flags alone, so the `test ebx,ebx` that follows
 * the call still reads what the call left it. */
static void *build_fill_call_stub(uintptr_t stub_address, uintptr_t return_address,
                                  uintptr_t callee)
{
    uint8_t buffer[64];
    emit_t  emit;

    emit_init(&emit, buffer, sizeof(buffer));

    emit_u8(&emit, 0xC7); emit_u8(&emit, 0x05);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_in_bar_fill);
    emit_u32(&emit, 1u);                                     /* mov dword [flag], 1          */

    emit_u8(&emit, 0x8B); emit_u8(&emit, 0xCF);              /* mov ecx, edi                 */
    emit_u8(&emit, 0xE8);
    emit_u32(&emit, (uint32_t)(callee - (stub_address + (uintptr_t)emit_size(&emit) + 4u)));

    emit_u8(&emit, 0xC7); emit_u8(&emit, 0x05);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_in_bar_fill);
    emit_u32(&emit, 0u);                                     /* mov dword [flag], 0          */

    emit_jump_rel32(&emit, stub_address, return_address);

    if (emit_overflowed(&emit)) {
        return NULL;
    }
    memcpy((void *)stub_address, buffer, emit_size(&emit));
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)stub_address, emit_size(&emit));
    return (void *)stub_address;
}

/* Fills a forward branch in once its target is known, so the two paths cannot drift apart. */
static void fill_branch(uint8_t *buffer, size_t at, size_t target)
{
    uint32_t rel = (uint32_t)(target - (at + 4u));

    buffer[at]     = (uint8_t)(rel & 0xFFu);
    buffer[at + 1] = (uint8_t)((rel >> 8) & 0xFFu);
    buffer[at + 2] = (uint8_t)((rel >> 16) & 0xFFu);
    buffer[at + 3] = (uint8_t)((rel >> 24) & 0xFFu);
}

/* Scales the fill's height and its offset down from the top of its own box.
 *
 * Every bar is laid out the same way, measured from the controls themselves: a box 18 high, a
 * fill 6 high, inset 7 from the top. That is already centred, and it stays centred at any
 * resolution, right up until the frame is rendered four and a half times taller than the 18 the
 * box says it is. The fill is then correct and the frame around it is not, which reads as a fill
 * pinned to the top edge.
 *
 * So the offset is scaled by the same ratio the frame is:
 *
 *     y = box_y + ratio * (y - box_y)
 *
 * The box top is a field on the control, and the control is still in edi here, one instruction
 * ahead of the mov that overwrites it. Nothing is pushed while esp has to be the engine's, so
 * both the load and the store go through esp directly, and no register is touched at all. Either
 * path leaves exactly one value on the FPU stack, which is what the load it replaced did.
 *
 * This is the whole bar family, so the loading bar is carried along with the two in the corners.
 *
 * An earlier version of this centred against the parent box instead, which never once ran: these
 * controls have no parent, so FUN_10066600 skips the branch that fetches one. */
static void *build_fill_scale_stub(uintptr_t stub_address, uintptr_t return_address)
{
    uint8_t buffer[128];
    emit_t  emit;
    size_t  skip_all;
    size_t  done;

    emit_init(&emit, buffer, sizeof(buffer));

    /* Relocated first, before anything this stub does can move esp. */
    emit_u8(&emit, 0xD9); emit_u8(&emit, 0x44); emit_u8(&emit, 0x24); emit_u8(&emit, 0x10);

    emit_u8(&emit, 0x9C);                                    /* pushfd                       */
    emit_u8(&emit, 0x8F); emit_u8(&emit, 0x05);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_fill_flags);     /* pop dword [saved]            */

    emit_u8(&emit, 0x83); emit_u8(&emit, 0x3D);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_in_bar_fill);
    emit_u8(&emit, 0x00);                                    /* cmp dword [flag], 0          */
    emit_u8(&emit, 0x0F); emit_u8(&emit, 0x84);
    skip_all = emit_size(&emit);
    emit_u32(&emit, 0u);

    emit_u8(&emit, 0xD8); emit_u8(&emit, 0x0D);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_scale_y);        /* fmul dword [g_scale_y]       */

    emit_u8(&emit, 0xD9); emit_u8(&emit, 0x44); emit_u8(&emit, 0x24); emit_u8(&emit, 0x18);
    emit_u8(&emit, 0xD8); emit_u8(&emit, 0x67); emit_u8(&emit, 0x3C);   /* fsub [edi+0x3c]   */
    emit_u8(&emit, 0xD8); emit_u8(&emit, 0x0D);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_scale_y);        /* fmul dword [g_scale_y]       */
    emit_u8(&emit, 0xD8); emit_u8(&emit, 0x47); emit_u8(&emit, 0x3C);   /* fadd [edi+0x3c]   */
    emit_u8(&emit, 0xD9); emit_u8(&emit, 0x5C); emit_u8(&emit, 0x24); emit_u8(&emit, 0x18);

    done = emit_size(&emit);

    emit_u8(&emit, 0xFF); emit_u8(&emit, 0x35);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_fill_flags);
    emit_u8(&emit, 0x9D);                                    /* popfd                        */
    emit_u8(&emit, 0x8B); emit_u8(&emit, 0x3E);              /* mov edi,[esi], relocated     */

    emit_jump_rel32(&emit, stub_address, return_address);

    if (emit_overflowed(&emit)) {
        return NULL;
    }
    fill_branch(buffer, skip_all, done);

    if (emit_overflowed(&emit)) {
        return NULL;
    }
    memcpy((void *)stub_address, buffer, emit_size(&emit));
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)stub_address, emit_size(&emit));
    return (void *)stub_address;
}

static void __cdecl record_quest_icon(uintptr_t control)
{
    LONG n;

    if (control == 0 || !memory_is_readable_range(control, CONTROL_SCALE_Y + 4u)) {
        return;
    }
    n = InterlockedIncrement(&g_quest_seen) - 1;
    g_quest[(uint32_t)n % QUEST_ROWS].control = control;

    /* Nothing is written here. At this point +0x40 and +0x44 hold the screen size, not the art,
     * so there is nothing yet worth scaling. The layout box is dealt with by fix_quest_layout,
     * once the texel size has actually been copied into it. */
    g_quest[(uint32_t)n % QUEST_ROWS].base_w = 0.0f;
    g_quest[(uint32_t)n % QUEST_ROWS].base_h = 0.0f;
}

/* Runs just after the texel size has been copied into the layout box, for every
 * GUIControl_Texture in the game, so it does nothing unless this is one of the controls recorded
 * above. The row lays itself out after this returns, which is the whole point of the timing. */
static void __cdecl fix_quest_layout(uintptr_t control)
{
    LONG seen = g_quest_seen;
    LONG i;

    if (control == 0 || !memory_is_readable_range(control, 0x80u)) {
        return;
    }

    /* The save pictures, whose rows size themselves to this box. Scaling the drawn picture and
     * not the box left it spilling out of its row and over the text beside it. */
    {
        LONG saved = g_save_seen;
        LONG j;

        if (saved > SAVE_ROWS) {
            saved = SAVE_ROWS;
        }
        for (j = 0; j < saved; ++j) {
            if (g_save[j].control != control) {
                continue;
            }
            {
                float w = 0.0f;
                float h = 0.0f;

                memcpy(&w, (const void *)(control + 0x70u), sizeof(w));
                memcpy(&h, (const void *)(control + 0x74u), sizeof(h));

                if (w > 0.0f && h > 0.0f) {
                    float sx = 0.0f;
                    float sy = 0.0f;

                    /* The drawn size, and the two axes no longer share a factor. */
                    memcpy(&sx, (const void *)(control + CONTROL_SCALE_X), sizeof(sx));
                    memcpy(&sy, (const void *)(control + CONTROL_SCALE_Y), sizeof(sy));

                    /* h on both axes: the art is square and w has the emptiness in it. */
                    g_save[j].base_w = h * sx;
                    g_save[j].base_h = h * sy;
                    g_save[j].src_h  = h;

                    if (memory_make_writable(control + 0x40u, 8u)) {
                        memcpy((void *)(control + 0x40u), &g_save[j].base_w, sizeof(float));
                        memcpy((void *)(control + 0x44u), &g_save[j].base_h, sizeof(float));
                    }
                }
            }
            return;
        }
    }
    if (seen > QUEST_ROWS) {
        seen = QUEST_ROWS;
    }
    for (i = 0; i < seen; ++i) {
        if (g_quest[i].control != control) {
            continue;
        }
        {
            float w = 0.0f;
            float h = 0.0f;

            /* The source rectangle, in texels, which is what the copy above just used. */
            memcpy(&w, (const void *)(control + 0x70u), sizeof(w));
            memcpy(&h, (const void *)(control + 0x74u), sizeof(h));

            if (w > 0.0f && h > 0.0f) {
                g_quest[i].base_w = w;
                g_quest[i].base_h = h;

                if (memory_make_writable(control + 0x40u, 8u)) {
                    float sw = w * g_scale_x;
                    float sh = h * g_scale_y;

                    memcpy((void *)(control + 0x40u), &sw, sizeof(sw));
                    memcpy((void *)(control + 0x44u), &sh, sizeof(sh));
                }
                if (InterlockedExchange(&g_quest_announced, 1) == 0) {
                    log_info("tick box %08X is %.2f x %.2f texels, laid out at %.2f x %.2f",
                             (unsigned)control, (double)w, (double)h,
                             (double)(w * g_scale_x), (double)(h * g_scale_y));
                }
            }
        }
        return;
    }
}

/* Reports the control, then performs the three moves it displaced. Those are relocated with esp
 * exactly as the engine had it, because the last of them writes through esp. */
static void *build_qlayout_stub(uintptr_t stub_address, uintptr_t return_address)
{
    uint8_t buffer[64];
    emit_t  emit;

    emit_init(&emit, buffer, sizeof(buffer));

    emit_u8(&emit, 0x60);                                    /* pushad                       */
    emit_u8(&emit, 0x9C);                                    /* pushfd                       */
    emit_u8(&emit, 0x57);                                    /* push edi, the control        */
    emit_u8(&emit, 0xE8);
    emit_u32(&emit, (uint32_t)((uintptr_t)&fix_quest_layout -
                               (stub_address + (uintptr_t)emit_size(&emit) + 4u)));
    emit_u8(&emit, 0x83); emit_u8(&emit, 0xC4); emit_u8(&emit, 0x04);
    emit_u8(&emit, 0x9D);                                    /* popfd                        */
    emit_u8(&emit, 0x61);                                    /* popad                        */

    {
        unsigned i;

        for (i = 0; i < QLAYOUT_SIZE; ++i) {
            emit_u8(&emit, qlayout_expected[i]);
        }
    }

    emit_jump_rel32(&emit, stub_address, return_address);

    if (emit_overflowed(&emit)) {
        return NULL;
    }
    memcpy((void *)stub_address, buffer, emit_size(&emit));
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)stub_address, emit_size(&emit));
    return (void *)stub_address;
}

/* Calls the constructor the site was going to call, then keeps what it returns. eax is the
 * control and pushad carries it across the recording untouched. */
static void *build_quest_stub(uintptr_t stub_address, uintptr_t return_address, uintptr_t callee)
{
    uint8_t buffer[64];
    emit_t  emit;

    emit_init(&emit, buffer, sizeof(buffer));

    emit_u8(&emit, 0xE8);
    emit_u32(&emit, (uint32_t)(callee - (stub_address + (uintptr_t)emit_size(&emit) + 4u)));

    emit_u8(&emit, 0x60);                                    /* pushad                       */
    emit_u8(&emit, 0x9C);                                    /* pushfd                       */
    emit_u8(&emit, 0x50);                                    /* push eax, the control        */
    emit_u8(&emit, 0xE8);
    emit_u32(&emit, (uint32_t)((uintptr_t)&record_quest_icon -
                               (stub_address + (uintptr_t)emit_size(&emit) + 4u)));
    emit_u8(&emit, 0x83); emit_u8(&emit, 0xC4); emit_u8(&emit, 0x04);
    emit_u8(&emit, 0x9D);                                    /* popfd                        */
    emit_u8(&emit, 0x61);                                    /* popad                        */

    emit_jump_rel32(&emit, stub_address, return_address);

    if (emit_overflowed(&emit)) {
        return NULL;
    }
    memcpy((void *)stub_address, buffer, emit_size(&emit));
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)stub_address, emit_size(&emit));
    return (void *)stub_address;
}

/* Records the picture control and leaves the game's ratio scaled by k in eax, with k pushed
 * where the hard 1.0 was, so the aspect lands once and on the destination. */
static void *build_save_icon_stub(uintptr_t stub_address, uintptr_t return_address)
{
    uint8_t buffer[64];
    emit_t  emit;

    emit_init(&emit, buffer, sizeof(buffer));

    /* First, while esp is still the engine's. */
    emit_u8(&emit, 0xD9); emit_u8(&emit, 0x44); emit_u8(&emit, 0x24); emit_u8(&emit, 0x14);
    emit_u8(&emit, 0xD8); emit_u8(&emit, 0x0D);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_scale_y);        /* the ratio times k            */
    emit_u8(&emit, 0xD9); emit_u8(&emit, 0x1D);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_save_temp);
    emit_u8(&emit, 0xA1); emit_u32(&emit, (uint32_t)(uintptr_t)&g_save_temp);

    emit_u8(&emit, 0xFF); emit_u8(&emit, 0x35);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_scale_y);        /* push dword [g_scale_y]       */

    /* edi is the control, and the row it sits in has to grow with the picture. popad puts eax
     * back exactly as it was set above. */
    emit_u8(&emit, 0x60);                                    /* pushad                       */
    emit_u8(&emit, 0x9C);                                    /* pushfd                       */
    emit_u8(&emit, 0x57);                                    /* push edi                     */
    emit_u8(&emit, 0xE8);
    emit_u32(&emit, (uint32_t)((uintptr_t)&record_save_icon -
                               (stub_address + (uintptr_t)emit_size(&emit) + 4u)));
    emit_u8(&emit, 0x83); emit_u8(&emit, 0xC4); emit_u8(&emit, 0x04);
    emit_u8(&emit, 0x9D);                                    /* popfd                        */
    emit_u8(&emit, 0x61);                                    /* popad                        */

    emit_jump_rel32(&emit, stub_address, return_address);

    if (emit_overflowed(&emit)) {
        return NULL;
    }
    memcpy((void *)stub_address, buffer, emit_size(&emit));
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)stub_address, emit_size(&emit));
    return (void *)stub_address;
}


/* The relocated bytes come in two halves: whatever has to run before the arguments are complete,
 * then the call itself. pushad puts the argument list at esp+0x24. */
static void *build_map_stub(uintptr_t stub_address, uintptr_t return_address,
                            const uint8_t *before, unsigned before_len,
                            const uint8_t *after, unsigned after_len)
{
    uint8_t buffer[64];
    emit_t  emit;
    unsigned i;

    emit_init(&emit, buffer, sizeof(buffer));

    for (i = 0; i < before_len; ++i) {
        emit_u8(&emit, before[i]);
    }

    emit_u8(&emit, 0x60);                                    /* pushad                       */
    emit_u8(&emit, 0x9C);                                    /* pushfd                       */
    emit_u8(&emit, 0x8D); emit_u8(&emit, 0x44); emit_u8(&emit, 0x24); emit_u8(&emit, 0x24);
    emit_u8(&emit, 0x50);                                    /* push eax, the argument list  */
    emit_u8(&emit, 0xE8);
    emit_u32(&emit, (uint32_t)((uintptr_t)&centre_map_icon -
                               (stub_address + (uintptr_t)emit_size(&emit) + 4u)));
    emit_u8(&emit, 0x83); emit_u8(&emit, 0xC4); emit_u8(&emit, 0x04);
    emit_u8(&emit, 0x9D);                                    /* popfd                        */
    emit_u8(&emit, 0x61);                                    /* popad                        */

    for (i = 0; i < after_len; ++i) {
        emit_u8(&emit, after[i]);
    }

    emit_jump_rel32(&emit, stub_address, return_address);

    if (emit_overflowed(&emit)) {
        return NULL;
    }
    memcpy((void *)stub_address, buffer, emit_size(&emit));
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)stub_address, emit_size(&emit));
    return (void *)stub_address;
}

/* Replaces a constant push with a push of our live scale. Two instructions, eleven bytes. */
/* Every write to a control happens from here, at its own draw, with the control alive.
 *
 * It used to happen from the 250ms poll as well, over pointers remembered when the control was
 * built. memory_is_readable_range only says the page is mapped, not that the object is still
 * there, so once a menu closed and its controls were freed this carried on writing floats into
 * whatever the heap had put in their place. That corrupts the heap, and the crash lands later,
 * on opening a menu rather than on closing one.
 *
 * Nothing here runs unless the engine is drawing the control, so a freed one is never touched.
 * The tables are read and never used as a write target on their own. */
static void __cdecl on_texture_draw(uintptr_t control)
{
    LONG  seen;
    LONG  i;
    float x = g_scale_x;
    float y = g_scale_y;

    if (control == 0 || !memory_is_readable_range(control, 0x80u)) {
        return;
    }

    /* The objective tick boxes: the scale pair, and the layout box the row reserves from. */
    seen = g_quest_seen;
    if (seen > QUEST_ROWS) {
        seen = QUEST_ROWS;
    }
    for (i = 0; i < seen; ++i) {
        if (g_quest[i].control != control) {
            continue;
        }
        if (x > 1.0f && memory_make_writable(control + CONTROL_SCALE_X, 8u)) {
            memcpy((void *)(control + CONTROL_SCALE_X), &x, sizeof(x));
            memcpy((void *)(control + CONTROL_SCALE_Y), &y, sizeof(y));
        }
        if (g_quest[i].base_w > 0.0f && memory_make_writable(control + 0x40u, 8u)) {
            float w = g_quest[i].base_w * x;
            float h = g_quest[i].base_h * y;

            memcpy((void *)(control + 0x40u), &w, sizeof(w));
            memcpy((void *)(control + 0x44u), &h, sizeof(h));
        }
        break;
    }

    clamp_save_picture(control);
}

/* Runs where the picture's draw reads its own geometry, with the control in esi, and performs
 * the two displaced instructions afterwards. Neither is position dependent. */
static void *build_draw_stub(uintptr_t stub_address, uintptr_t return_address)
{
    uint8_t buffer[64];
    emit_t  emit;

    emit_init(&emit, buffer, sizeof(buffer));

    emit_u8(&emit, 0x60);                                    /* pushad                       */
    emit_u8(&emit, 0x9C);                                    /* pushfd                       */
    emit_u8(&emit, 0x56);                                    /* push esi, the control        */
    emit_u8(&emit, 0xE8);
    emit_u32(&emit, (uint32_t)((uintptr_t)&on_texture_draw -
                               (stub_address + (uintptr_t)emit_size(&emit) + 4u)));
    emit_u8(&emit, 0x83); emit_u8(&emit, 0xC4); emit_u8(&emit, 0x04);
    emit_u8(&emit, 0x9D);                                    /* popfd                        */
    emit_u8(&emit, 0x61);                                    /* popad                        */

    {
        unsigned i;

        for (i = 0; i < DRAW_SIZE; ++i) {
            emit_u8(&emit, draw_expected[i]);
        }
    }

    emit_jump_rel32(&emit, stub_address, return_address);

    if (emit_overflowed(&emit)) {
        return NULL;
    }
    memcpy((void *)stub_address, buffer, emit_size(&emit));
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)stub_address, emit_size(&emit));
    return (void *)stub_address;
}

static void *build_bar_scale_stub(uintptr_t stub_address, uintptr_t return_address)
{
    uint8_t buffer[32];
    emit_t  emit;

    emit_init(&emit, buffer, sizeof(buffer));

    emit_u8(&emit, 0xFF); emit_u8(&emit, 0x35);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_scale_y);        /* push dword [g_scale_y]       */
    emit_jump_rel32(&emit, stub_address, return_address);

    if (emit_overflowed(&emit)) {
        return NULL;
    }
    memcpy((void *)stub_address, buffer, emit_size(&emit));
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)stub_address, emit_size(&emit));
    return (void *)stub_address;
}

static void *build_hud_stub(uintptr_t stub_address, uintptr_t return_address)
{
    uint8_t buffer[64];
    emit_t  emit;

    emit_init(&emit, buffer, sizeof(buffer));

    /* st0 is the width and st1 the height, both texel counts, after the fxch above. */
    emit_u8(&emit, 0xD9); emit_u8(&emit, 0x15);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_hud_base_w);
    emit_u8(&emit, 0xD8); emit_u8(&emit, 0x0D);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_scale_x);
    emit_u8(&emit, 0xD9); emit_u8(&emit, 0x5F); emit_u8(&emit, 0x40);

    emit_u8(&emit, 0xD9); emit_u8(&emit, 0x15);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_hud_base_h);
    emit_u8(&emit, 0xD8); emit_u8(&emit, 0x0D);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_scale_y);
    emit_u8(&emit, 0xD9); emit_u8(&emit, 0x5F); emit_u8(&emit, 0x44);

    emit_u8(&emit, 0x60);                                    /* pushad                       */
    emit_u8(&emit, 0x9C);                                    /* pushfd                       */
    emit_u8(&emit, 0x57);                                    /* push edi, the control        */
    emit_u8(&emit, 0xE8);
    emit_u32(&emit, (uint32_t)((uintptr_t)&remember_hud -
                               (stub_address + (uintptr_t)emit_size(&emit) + 4u)));
    emit_u8(&emit, 0x83); emit_u8(&emit, 0xC4); emit_u8(&emit, 0x04);
    emit_u8(&emit, 0x9D);                                    /* popfd                        */
    emit_u8(&emit, 0x61);                                    /* popad                        */
    emit_jump_rel32(&emit, stub_address, return_address);

    if (emit_overflowed(&emit)) {
        return NULL;
    }
    memcpy((void *)stub_address, buffer, emit_size(&emit));
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)stub_address, emit_size(&emit));
    return (void *)stub_address;
}

/* The ring stores through the same two fields as the HUD textures, so it joins the same table
 * and the same poll corrects it. */
static void *build_ring_stub(uintptr_t stub_address, uintptr_t return_address)
{
    uint8_t buffer[96];
    emit_t  emit;

    emit_init(&emit, buffer, sizeof(buffer));

    /* The two texel counts arrive as float bit patterns in ecx and eax. */
    emit_u8(&emit, 0x89); emit_u8(&emit, 0x0D);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_hud_base_h);     /* mov [base_h], ecx            */
    emit_u8(&emit, 0xA3);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_hud_base_w);     /* mov [base_w], eax            */

    emit_u8(&emit, 0xD9); emit_u8(&emit, 0x05);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_hud_base_h);     /* fld  [base_h]                */
    emit_u8(&emit, 0xD8); emit_u8(&emit, 0x0D);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_scale_y);        /* fmul [scale_y]               */
    emit_u8(&emit, 0xD9); emit_u8(&emit, 0x5F); emit_u8(&emit, 0x44);

    emit_u8(&emit, 0xD9); emit_u8(&emit, 0x05);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_hud_base_w);     /* fld  [base_w]                */
    emit_u8(&emit, 0xD8); emit_u8(&emit, 0x0D);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_scale_x);        /* fmul [scale_x]               */
    emit_u8(&emit, 0xD9); emit_u8(&emit, 0x5F); emit_u8(&emit, 0x40);

    emit_u8(&emit, 0x60);                                    /* pushad                       */
    emit_u8(&emit, 0x9C);                                    /* pushfd                       */
    emit_u8(&emit, 0x57);                                    /* push edi                     */
    emit_u8(&emit, 0xE8);
    emit_u32(&emit, (uint32_t)((uintptr_t)&remember_hud -
                               (stub_address + (uintptr_t)emit_size(&emit) + 4u)));
    emit_u8(&emit, 0x83); emit_u8(&emit, 0xC4); emit_u8(&emit, 0x04);
    emit_u8(&emit, 0x9D);                                    /* popfd                        */
    emit_u8(&emit, 0x61);                                    /* popad                        */

    /* AFTER popad, never before: popad would put the old ecx back and the call below the hook
     * would receive a stale this. */
    emit_u8(&emit, 0x8B); emit_u8(&emit, 0xCF);              /* mov ecx, edi                 */
    emit_jump_rel32(&emit, stub_address, return_address);

    if (emit_overflowed(&emit)) {
        return NULL;
    }
    memcpy((void *)stub_address, buffer, emit_size(&emit));
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)stub_address, emit_size(&emit));
    return (void *)stub_address;
}

static void *build_stub(uintptr_t stub_address, uintptr_t return_address)
{
    uint8_t buffer[32];
    emit_t  emit;

    emit_init(&emit, buffer, sizeof(buffer));

    emit_bytes(&emit, cursor_expected, CURSOR_SIZE);         /* the relocated store          */
    emit_u8(&emit, 0x89); emit_u8(&emit, 0x2D);
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_cursor);         /* mov [g_cursor], ebp          */
    emit_jump_rel32(&emit, stub_address, return_address);

    if (emit_overflowed(&emit)) {
        return NULL;
    }
    memcpy((void *)stub_address, buffer, emit_size(&emit));
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)stub_address, emit_size(&emit));
    return (void *)stub_address;
}

/* Polled, not written once, because the manager rebuilds its pointer control and the
 * constructor puts 1.0 back every time it does. An aligned four byte float store is atomic on
 * x86, so the render thread reading these mid-write is not a hazard. */
static DWORD WINAPI hold_scale(LPVOID parameter)
{
    float announced_x = 0.0f;

    (void)parameter;
    for (;;) {
        uintptr_t control = g_cursor;
        uintptr_t camera  = g_camera;

        if (control != 0 && camera != 0 &&
            memory_is_readable_range(control, CONTROL_SCALE_Y + 4u)) {
            int32_t viewport_w = 0;
            int32_t viewport_h = 0;

            if (memory_read(camera + CAMERA_VIEWPORT_W, &viewport_w, sizeof(viewport_w)) &&
                memory_read(camera + CAMERA_VIEWPORT_H, &viewport_h, sizeof(viewport_h)) &&
                viewport_w > 0 && viewport_h > 0) {
                float x = (float)viewport_w / (float)g_reference_width;
                float y = (float)viewport_h / (float)g_reference_height;

                if (memory_make_writable(control + CONTROL_SCALE_X, 8u)) {
                    memcpy((void *)(control + CONTROL_SCALE_X), &x, sizeof(x));
                    memcpy((void *)(control + CONTROL_SCALE_Y), &y, sizeof(y));

                    if (x != announced_x) {
                        announced_x = x;
                        log_info("pointer control %08X scaled %.4f x %.4f",
                                 (unsigned)control, (double)x, (double)y);
                    }
                }

                /* The HUD textures, re-derived from the dimensions they were built with, so a
                 * repeated pass cannot compound. */
                g_scale_x = x;
                g_scale_y = y;
                {
                    LONG n = g_hud_count;
                    LONG i;

                    for (i = 0; i < n && i < HUD_ROWS; ++i) {
                        uintptr_t c = g_hud[i].control;
                        float     w = g_hud[i].base_w * x;
                        float     h = g_hud[i].base_h * y;

                        if (c != 0 && memory_is_readable_range(c, 0x48u) &&
                            memory_make_writable(c + 0x40u, 8u)) {
                            memcpy((void *)(c + 0x40u), &w, sizeof(w));
                            memcpy((void *)(c + 0x44u), &h, sizeof(h));
                        }
                    }
                }

            }
        }
        Sleep(250);
    }
}

static void on_rfl_loaded(uintptr_t rfl_base)
{
    uintptr_t site = rfl_site(rfl_base, CURSOR_RVA);
    uintptr_t stub_address;
    HANDLE    thread;

    if (!patch_validate_bytes(site, cursor_expected, CURSOR_SIZE)) {
        log_error("rfl+%X is not the store this was measured against, not installing",
                  CURSOR_RVA);
        return;
    }
    stub_address = (uintptr_t)trampoline_alloc(32);
    if (stub_address == 0) {
        log_error("could not allocate the stub");
        return;
    }
    if (build_stub(stub_address, rfl_site(rfl_base, CURSOR_RETURN_RVA)) == NULL) {
        log_error("the stub did not fit its buffer, not installing");
        return;
    }
    if (patch_write_jump(site, (const void *)stub_address, CURSOR_SIZE) != PATCH_RESULT_OK) {
        log_error("could not branch to the stub");
        return;
    }

    /* Independent of the pointer hook: if this site does not match, that fix still works. */
    {
        uintptr_t hud = rfl_site(rfl_base, HUD_RVA);

        if (patch_validate_bytes(hud, hud_expected, HUD_SIZE)) {
            uintptr_t hud_stub = (uintptr_t)trampoline_alloc(64);

            if (hud_stub != 0 &&
                build_hud_stub(hud_stub, rfl_site(rfl_base, HUD_RETURN_RVA)) != NULL &&
                patch_write_jump(hud, (const void *)hud_stub, HUD_SIZE) == PATCH_RESULT_OK) {
                log_info("  and rfl+%X -> stub at %08X, the HUD texture size",
                         HUD_RVA, (unsigned)hud_stub);
            } else {
                log_warning("the HUD texture hook could not be installed");
            }
        } else {
            log_warning("rfl+%X is not the HUD texture store this expects", HUD_RVA);
        }
    }

    /* Independent again: any of the three can fail without taking the others with it. */
    {
        uintptr_t ring = rfl_site(rfl_base, RING_RVA);

        if (patch_validate_bytes(ring, ring_expected, RING_SIZE)) {
            uintptr_t ring_stub = (uintptr_t)trampoline_alloc(96);

            if (ring_stub != 0 &&
                build_ring_stub(ring_stub, rfl_site(rfl_base, RING_RETURN_RVA)) != NULL &&
                patch_write_jump(ring, (const void *)ring_stub, RING_SIZE) == PATCH_RESULT_OK) {
                log_info("  and rfl+%X -> stub at %08X, the ring icon size",
                         RING_RVA, (unsigned)ring_stub);
            } else {
                log_warning("the ring icon hook could not be installed");
            }
        } else {
            log_warning("rfl+%X is not the ring icon store this expects", RING_RVA);
        }
    }

    {
        size_t   index;
        unsigned done = 0;
        unsigned n    = (unsigned)(sizeof(bar_scale_sites) / sizeof(bar_scale_sites[0]));

        /* All seven checked before any is written. They are one behaviour, and half a bar frame
         * scaled is worse to look at than none of it. */
        for (index = 0; index < n; ++index) {
            if (!patch_validate_bytes(rfl_site(rfl_base, bar_scale_sites[index]),
                                      bar_scale_expected, sizeof(bar_scale_expected))) {
                log_warning("rfl+%X is not the scale push this expects; the bars are left alone",
                            bar_scale_sites[index]);
                n = 0;
                break;
            }
        }
        for (index = 0; index < n; ++index) {
            uintptr_t push_site = rfl_site(rfl_base, bar_scale_sites[index]);
            uintptr_t stub      = (uintptr_t)trampoline_alloc(32);

            if (stub != 0 &&
                build_bar_scale_stub(stub, push_site + sizeof(bar_scale_expected)) != NULL &&
                patch_write_jump(push_site, (const void *)stub,
                                 sizeof(bar_scale_expected)) == PATCH_RESULT_OK) {
                done++;
            }
        }
        if (n != 0) {
            log_info("  and %u of %u bar scale pushes now read the live height ratio", done, n);
        }
    }

    {
        uintptr_t star = rfl_site(rfl_base, MAP_STAR_RVA);
        uintptr_t ind  = rfl_site(rfl_base, MAP_IND_RVA);

        /* Both checked before either is written: one icon re-centred and the other not would put
         * them further apart than leaving both alone. */
        if (patch_validate_bytes(star, map_star_expected, MAP_STAR_SIZE) &&
            patch_validate_bytes(ind, map_ind_expected, MAP_IND_SIZE)) {
            uintptr_t star_stub = (uintptr_t)trampoline_alloc(64);
            uintptr_t ind_stub  = (uintptr_t)trampoline_alloc(64);
            unsigned  done = 0;

            if (star_stub != 0 &&
                build_map_stub(star_stub, rfl_site(rfl_base, MAP_STAR_RETURN_RVA),
                               map_star_expected, 3u, map_star_expected + 3u, 3u) != NULL &&
                patch_write_jump(star, (const void *)star_stub,
                                 MAP_STAR_SIZE) == PATCH_RESULT_OK) {
                done++;
            }
            if (ind_stub != 0 &&
                build_map_stub(ind_stub, rfl_site(rfl_base, MAP_IND_RETURN_RVA),
                               map_ind_expected, 2u, map_ind_expected + 2u, 3u) != NULL &&
                patch_write_jump(ind, (const void *)ind_stub,
                                 MAP_IND_SIZE) == PATCH_RESULT_OK) {
                done++;
            }
            log_info("  and %u of 2 map icons scale about their centres", done);
        } else {
            log_warning("the map icon draws are not what this expects; the map is left alone");
        }
    }

    {
        size_t   index;
        unsigned done = 0;
        unsigned n    = (unsigned)(sizeof(save_icon_sites) / sizeof(save_icon_sites[0]));

        /* Both checked before either is written. The two sites are byte for byte identical and
         * draw the same kind of picture, so scaling one without the other would look like a bug
         * rather than a fix. */
        for (index = 0; index < n; ++index) {
            if (!patch_validate_bytes(rfl_site(rfl_base, save_icon_sites[index]),
                                      save_icon_expected, SAVE_ICON_SIZE)) {
                log_warning("rfl+%X is not a save picture scale push; the menu is left alone",
                            save_icon_sites[index]);
                n = 0;
                break;
            }
        }
        for (index = 0; index < n; ++index) {
            uintptr_t icon = rfl_site(rfl_base, save_icon_sites[index]);
            uintptr_t stub = (uintptr_t)trampoline_alloc(64);

            if (stub != 0 &&
                build_save_icon_stub(stub, icon + SAVE_ICON_SIZE) != NULL &&
                patch_write_jump(icon, (const void *)stub,
                                 SAVE_ICON_SIZE) == PATCH_RESULT_OK) {
                done++;
            }
        }
        if (n != 0) {
            log_info("  and %u of %u save pictures now scale on both axes", done, n);
        }
    }

    {
        uintptr_t draw = rfl_site(rfl_base, DRAW_RVA);

        if (patch_validate_bytes(draw, draw_expected, DRAW_SIZE)) {
            uintptr_t stub = (uintptr_t)trampoline_alloc(64);

            if (stub != 0 &&
                build_draw_stub(stub, rfl_site(rfl_base, DRAW_RETURN_RVA)) != NULL &&
                patch_write_jump(draw, (const void *)stub, DRAW_SIZE) == PATCH_RESULT_OK) {
                log_info("  and rfl+%X -> stub at %08X, the save picture clip",
                         DRAW_RVA, (unsigned)stub);
            } else {
                log_warning("the save picture clip could not be installed");
            }
        } else {
            log_warning("rfl+%X is not the texture draw this expects", DRAW_RVA);
        }
    }


    {
        uintptr_t scale = rfl_site(rfl_base, FILL_SCALE_RVA);
        size_t    index;
        unsigned  wrapped = 0;
        unsigned  n = (unsigned)(sizeof(fill_call_sites) / sizeof(fill_call_sites[0]));

        /* Both calls are checked before either is written, and the scale push is checked before
         * any of it. A bar whose fill scaled on one of its two draws would flicker, which is
         * worse to look at than a fill left exactly as the game drew it. */
        for (index = 0; index < n; ++index) {
            if (!patch_validate_bytes(rfl_site(rfl_base, fill_call_sites[index]),
                                      fill_call_expected, sizeof(fill_call_expected))) {
                log_warning("rfl+%X is not the bar fill call this expects; the fill is left alone",
                            fill_call_sites[index]);
                n = 0;
                break;
            }
        }
        if (n != 0 && !patch_validate_bytes(scale, fill_scale_expected,
                                            sizeof(fill_scale_expected))) {
            log_warning("rfl+%X is not the fill scale push this expects; the fill is left alone",
                        FILL_SCALE_RVA);
            n = 0;
        }
        for (index = 0; index < n; ++index) {
            uintptr_t call = rfl_site(rfl_base, fill_call_sites[index]);
            uintptr_t stub = (uintptr_t)trampoline_alloc(64);

            if (stub != 0 &&
                build_fill_call_stub(stub, call + FILL_CALL_SIZE,
                                     rfl_site(rfl_base, FILL_CALLEE_RVA)) != NULL &&
                patch_write_jump(call, (const void *)stub,
                                 FILL_CALL_SIZE) == PATCH_RESULT_OK) {
                wrapped++;
            }
        }
        if (n != 0 && wrapped == n) {
            uintptr_t stub = (uintptr_t)trampoline_alloc(128);

            if (stub != 0 &&
                build_fill_scale_stub(stub, rfl_site(rfl_base, FILL_SCALE_RETURN_RVA)) != NULL &&
                patch_write_jump(scale, (const void *)stub,
                                 FILL_SCALE_SIZE) == PATCH_RESULT_OK) {
                log_info("  and rfl+%X -> stub at %08X, the bar fill, behind both of its calls",
                         FILL_SCALE_RVA, (unsigned)stub);
            } else {
                log_warning("the bar fill scale could not be installed");
            }
        } else if (n != 0) {
            log_warning("only %u of %u bar fill calls were wrapped; the fill is left alone",
                        wrapped, n);
        }
    }

    {
        uintptr_t quest = rfl_site(rfl_base, QUEST_RVA);

        if (patch_validate_bytes(quest, quest_expected, QUEST_SIZE)) {
            uintptr_t stub = (uintptr_t)trampoline_alloc(64);

            if (stub != 0 &&
                build_quest_stub(stub, rfl_site(rfl_base, QUEST_RETURN_RVA),
                                 rfl_site(rfl_base, QUEST_CALLEE_RVA)) != NULL &&
                patch_write_jump(quest, (const void *)stub, QUEST_SIZE) == PATCH_RESULT_OK) {
                log_info("  and rfl+%X -> stub at %08X, the objective tick boxes",
                         QUEST_RVA, (unsigned)stub);
            } else {
                log_warning("the objective tick box hook could not be installed");
            }
        } else {
            log_warning("rfl+%X is not the tick box constructor call this expects", QUEST_RVA);
        }
    }

    {
        uintptr_t layout = rfl_site(rfl_base, QLAYOUT_RVA);

        if (patch_validate_bytes(layout, qlayout_expected, QLAYOUT_SIZE)) {
            uintptr_t stub = (uintptr_t)trampoline_alloc(64);

            if (stub != 0 &&
                build_qlayout_stub(stub, rfl_site(rfl_base, QLAYOUT_RETURN_RVA)) != NULL &&
                patch_write_jump(layout, (const void *)stub, QLAYOUT_SIZE) == PATCH_RESULT_OK) {
                log_info("  and rfl+%X -> stub at %08X, the tick box layout",
                         QLAYOUT_RVA, (unsigned)stub);
            } else {
                log_warning("the tick box layout hook could not be installed");
            }
        } else {
            log_warning("rfl+%X is not the texel copy this expects", QLAYOUT_RVA);
        }
    }


    thread = CreateThread(NULL, 0, hold_scale, NULL, 0, NULL);
    if (thread != NULL) {
        CloseHandle(thread);
    } else {
        log_error("could not start the scale thread; nothing would ever be written");
        return;
    }

    log_info("installed: rfl+%X -> stub at %08X, waiting for the pointer control",
             CURSOR_RVA, (unsigned)stub_address);
}

void texture_scaling_install(void)
{
    log_init(PLUGIN_SECTION, false);

    if (!ini_read_bool(PLUGIN_SECTION, "Enabled", false)) {
        log_info("Enabled=0, the mouse pointer stays the size of its own texture");
        return;
    }
    if (!host_image_resolve()) {
        log_error("the host image could not be resolved; refusing to touch anything");
        return;
    }

    g_reference_width  = (int32_t)ini_read_int(PLUGIN_SECTION, "ReferenceWidth", 640);
    g_reference_height = (int32_t)ini_read_int(PLUGIN_SECTION, "ReferenceHeight", 480);
    if (g_reference_width < 64 || g_reference_height < 64) {
        log_error("ReferenceWidth=%ld ReferenceHeight=%ld is not a resolution anything was "
                  "authored against, not installing",
                  (long)g_reference_width, (long)g_reference_height);
        return;
    }

    if (!camera_track(250, &g_camera, NULL)) {
        log_error("could not start the camera watch; nothing would ever be scaled");
        return;
    }

    if (!module_watch_when_loaded(FELLOWSHIP_RFL_MODULE, on_rfl_loaded, 60000)) {
        log_error("could not start the module watch");
    }
}

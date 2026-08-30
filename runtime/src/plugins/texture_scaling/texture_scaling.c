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

/* Engine objects live on the heap, and heap memory is already writable: nothing here ever needed
 * VirtualProtect, which is for patching code.
 *
 * Calling it anyway, once per control per frame, is what crashed the game. Every call carves
 * another protection range out of the heap's region, and after enough of them the memory manager
 * gives way. The fault lands inside ntdll rather than in this file, which is what made it look
 * like anything but a plugin bug:
 *
 *     CRASH  code C0000005  at 77930932  in ntdll.dll+50932   reading 00000004
 *     ebx 0FDFBCC8, which is a control+0x40, the address last handed to VirtualProtect
 *
 * It showed on a machine running at 120 fps and not on one at 60, because the rate of the calls
 * is what decides how quickly it falls over. */
/* A control's address is not enough to identify it.
 *
 * These tables are never cleared, so within a couple of levels every slot holds the address of a
 * freed control. Freed GUIControl_Texture objects are all the same 0x80 bytes from the same heap
 * bucket, so a dead entry's address is the MOST likely one the allocator hands back for the next
 * one. A live, unrelated control would then be claimed by a dead entry and drawn with its
 * geometry: the mouse pointer at a tick box's size, or a save picture blanked outright.
 *
 * So the source rectangle width is recorded alongside the address and re-checked here. Nothing
 * this plugin does ever writes +0x70, so it stays what the engine put there, and a different
 * control almost never has the same source width as the one the entry was recorded for. */
static bool still_the_same_control(uintptr_t control, float recorded_src_w)
{
    float now = 0.0f;

    if (recorded_src_w <= 0.0f) {
        return true;              /* not measured yet, nothing to disagree with */
    }
    memcpy(&now, (const void *)(control + 0x70u), sizeof(now));
    return now == recorded_src_w;
}

static bool control_writable(uintptr_t address, size_t size)
{
    return address != 0 && size != 0;
}



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

/* NINTH SITE, the scroll arrows on every list in the game.
 *
 * Scroll Buttons is its own class, 11 properties, and the arrow is 30 by 17 texels. Two calls
 * draw it, one per arrow, both to FUN_10066AE0:
 *
 *     1006a9d4  mov ecx,esi / call FUN_10066AE0
 *     1006aa0a  mov ecx,esi / call FUN_10066AE0
 *
 * That function hands the real draw its scale as a RATIO of two of its own arguments:
 *
 *     (*param_6 / *param_3,  param_6[1] / param_3[1])
 *
 * and both point at the same 30 by 17, so the scale is exactly 1.0 and the arrow is drawn one
 * texel to one pixel.
 *
 * Three attempts missed this. Arguments nine and ten look like a scale pair and are ignored on
 * this path, arg8 being zero. Scaling what arg3 points at does nothing, because the ratio divides
 * it out. Scaling what arg6 points at does nothing either, for the same reason and worse: it is
 * the SAME rectangle, so the source grew and the arrows moved instead.
 *
 * So arg6 is pointed at a rectangle of this plugin's own, k times the source, leaving the source
 * itself alone. The ratio then comes out k and the art is unchanged. */
#define ARROW_HOOK_SIZE 7u

/* The two sites are the two arrows, and which is which matters for the anchor: the first draws
 * at y 216 and the second at y 1754, measured. */
static const struct {
    uint32_t rva;
    uint8_t  grows_up;      /* the lower arrow keeps its bottom edge */
} arrow_sites[] = {
    { 0x6A9D4u, 0u },       /* the upper arrow */
    { 0x6AA0Au, 1u }        /* the lower arrow */
};

static const uint8_t arrow_expected[3] = { 0x8B, 0xCE, 0xE8 };   /* mov ecx,esi ; call */

/* Handed to the engine for the duration of one call, on one thread. */
static float g_arrow_extent[2];

static void __cdecl scale_arrow(uintptr_t args, uint32_t grows_up)
{
    float     k = g_scale_y;
    uintptr_t source = 0;
    uintptr_t ours;
    float     wh[2];

    if (k <= 1.0f || !memory_is_readable_range(args, 0x18u)) {
        return;
    }
    memcpy(&source, (const void *)(args + 0x08u), sizeof(source));      /* arg3 */
    if (source == 0 || !memory_is_readable_range(source, sizeof(wh))) {
        return;
    }
    memcpy(wh, (const void *)source, sizeof(wh));
    if (wh[0] <= 0.0f || wh[1] <= 0.0f) {
        return;
    }
    g_arrow_extent[0] = wh[0] * k;
    g_arrow_extent[1] = wh[1] * k;

    ours = (uintptr_t)&g_arrow_extent[0];
    memcpy((void *)(args + 0x14u), &ours, sizeof(ours));                /* arg6 */

    /* And move the corner so the arrow grows INWARD, because it sits in the corner of a frame
     * and anything else pushes it through one edge or the other.
     *
     * Growing from the top left corner put both arrows outside; centring them, which is what the
     * map icons want, left each straddling its corner half in and half out. Both arrows are on
     * the right, so the right edge is held in both cases, and each holds the edge it is tucked
     * against: the upper one its top, the lower one its bottom. */
    {
        float x = 0.0f;
        float y = 0.0f;
        float growth_x = wh[0] * (k - 1.0f);
        float growth_y = wh[1] * (k - 1.0f);

        memcpy(&x, (const void *)(args + 0x0Cu), sizeof(x));            /* arg4 */
        memcpy(&y, (const void *)(args + 0x10u), sizeof(y));            /* arg5 */
        x -= growth_x;
        if (grows_up != 0) {
            y -= growth_y;
        }
        memcpy((void *)(args + 0x0Cu), &x, sizeof(x));
        memcpy((void *)(args + 0x10u), &y, sizeof(y));
    }
}

/* The check boxes in every options menu: Invert Y-Axis, Always Free-Look, Stereo Sound, Show
 * Subtitles, Reduce Detail. They are a class of their own, Checkbox Control, 34 properties, with
 * a Checkbox params group carrying Checkbox X Size and Checkbox Y Size at class relative 31 and
 * 32. hud_probe named the readers: index 31 at rfl+69B9C, a thousand hits over a thousand frames.
 *
 * That draw ends at rfl+69C27 in a call to FUN_10066AE0, which is the SAME helper the scroll
 * arrows go through, and it fails the same way. Tracing the pushes:
 *
 *     69c20  push edx        arg6 = esp+0x1C
 *     69c17  lea  edx,[esp+0x34]
 *     69c15  push edx        arg3 = esp+0x1C
 *
 * both arguments point at one rectangle, the size pair, and the helper works its scale out as
 * arg6 divided by arg3, so it is 1.0 whatever those properties say. The two push 0x3f800000 at
 * 69bf6 and 69bfb are arguments nine and ten, which that helper ignores.
 *
 * So arg6 is pointed at a rectangle of our own, k times the source, and the source is left alone.
 * Unlike the arrows the box grows about its centre: it floats beside its label with room on
 * either side, so it has no edge it must stay behind. */
#define CHECKBOX_RVA       0x69C27u
#define CHECKBOX_HOOK_SIZE 7u

static const uint8_t checkbox_expected[3] = { 0x8B, 0xCF, 0xE8 };  /* mov ecx,edi ; call */

/* Handed to the engine for the duration of one call, on one thread. */
static float g_checkbox_extent[2];

static void __cdecl scale_checkbox(uintptr_t args)
{
    float     k = g_scale_y;
    uintptr_t source = 0;
    uintptr_t ours;
    float     wh[2];

    if (k <= 1.0f || !memory_is_readable_range(args, 0x18u)) {
        return;
    }
    memcpy(&source, (const void *)(args + 0x08u), sizeof(source));      /* arg3 */
    if (source == 0 || !memory_is_readable_range(source, sizeof(wh))) {
        return;
    }
    memcpy(wh, (const void *)source, sizeof(wh));
    if (wh[0] <= 0.0f || wh[1] <= 0.0f) {
        return;
    }
    g_checkbox_extent[0] = wh[0] * k;
    g_checkbox_extent[1] = wh[1] * k;

    ours = (uintptr_t)&g_checkbox_extent[0];
    memcpy((void *)(args + 0x14u), &ours, sizeof(ours));                /* arg6 */

    /* One factor on both axes, because the art is square and the two properties are already a
     * percentage of width against a percentage of height. */
    {
        float x = 0.0f;
        float y = 0.0f;

        memcpy(&x, (const void *)(args + 0x0Cu), sizeof(x));            /* arg4 */
        memcpy(&y, (const void *)(args + 0x10u), sizeof(y));            /* arg5 */
        x -= wh[0] * (k - 1.0f) * 0.5f;
        y -= wh[1] * (k - 1.0f) * 0.5f;
        memcpy((void *)(args + 0x0Cu), &x, sizeof(x));
        memcpy((void *)(args + 0x10u), &y, sizeof(y));
    }
}

/* The border around every framed box: the save list, the resolution list, the key list.
 *
 * GUI Border is a class of twelve properties and all of them are texels, read together at
 * rfl+65EDA to rfl+65F97 as indices 0, 11, 6, 5, 10, 9, 4, 3, 2, 1 and then 7 and 8: a 38 by 38
 * region of texture at (81, 1), 9 texel corners, 5 texel sides. Scaling those would be the same
 * mistake the bars taught: the destination and the source come out of the same numbers, so
 * growing one grows the region sampled and the art smears.
 *
 * The engine has its own knob for this. Every framed GUI class carries a Border group, and its
 * second property is `Border Scaling`, key `BorderSize`, a float defaulting to 1.0. One small
 * function reads it:
 *
 *     10065bb7  push 0xc                index 12, Border Texture
 *     10065bc0  cmp  ebx,-1             no texture, no border, nothing to do
 *     10065bca  push 0xd                index 13, Border Scaling
 *     10065bd1  mov  ecx,[eax]          the value
 *     10065bdf  call [edx+0x20]         handed on with the texture
 *
 * so the whole border enters through one pair of arguments, and the scale is a multiplier rather
 * than a size. The hook performs those three instructions itself with the factor applied, which
 * costs nothing extra since none of them is position dependent. */
#define BORDER_RVA       0x65BD1u
#define BORDER_HOOK_SIZE 6u

static const uint8_t border_expected[BORDER_HOOK_SIZE] = {
    0x8B, 0x08,                          /* mov ecx,[eax]  */
    0x8B, 0x16,                          /* mov edx,[esi]  */
    0x8B, 0xC1                           /* mov eax,ecx    */
};

/* Handed to the engine for the duration of one call, on one thread. */
static float g_border_scale = 1.0f;

static void __cdecl scale_border(uintptr_t value)
{
    float k = g_scale_y;
    float v = 1.0f;

    if (value == 0 || !memory_is_readable_range(value, sizeof(v))) {
        g_border_scale = 1.0f;
        return;
    }
    memcpy(&v, (const void *)value, sizeof(v));
    if (k > 1.0f && v > 0.0f) {
        v *= k;
    }
    g_border_scale = v;
}

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

    if (control_writable(args, sizeof(a))) {
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
/* The two sites do not keep the control in the same register, which is easy to miss because
 * they are byte for byte identical at the hook itself:
 *
 *     073910  mov ebp,eax / xor ebp,ebp     the New Save entry, control in EBP
 *     073CC4  mov edi,eax / xor edi,edi     the per slot loop,  control in EDI
 *
 * and each then does its own `mov ecx,&lt;that register&gt;` for the SetScale call. Pushing edi at
 * both recorded the container instead of the picture for New Save, so that one was never grown
 * or clipped, and a pointer that is not a control sat in the table for the rest of the run. */
static const struct {
    uint32_t rva;
    uint8_t  push_control;   /* 0x55 push ebp, 0x57 push edi */
} save_icon_sites[] = {
    { 0x73916u, 0x55u },    /* the New Save entry */
    { 0x73CC9u, 0x57u }     /* once per save slot */
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

/* The same draw, a little further on, where it clips itself to its parent.
 *
 *     1006c94d  call [edx+0x44]         the parent clip rectangle, four floats
 *     1006c950  mov  ecx,eax
 *     1006c952  fld  [ecx]              left,   against the control own left
 *     1006c96d  fld  [ecx+0x4]          top
 *     1006c989  fld  [ecx+0x8]          right
 *     1006c9a5  fld  [ecx+0xc]          bottom
 *
 * and having narrowed the rectangle it moves the source origin to match:
 *
 *     1006c9e2  fadd [esi+0x6c]         the source v, plus whatever the top lost
 *     1006c9e9  fadd [esi+0x68]         and the same for u
 *
 * So the engine already crops a top edge properly, source origin and all. The reason it never
 * happened here is the parent: a picture parent is its ROW, and the row travels with the
 * picture, so clipping to it cuts nothing. The list is one link further up.
 *
 * The hook therefore changes the rectangle, not the control. Nothing else is touched, and in
 * particular the position at +0x3c is left alone. */
#define CLIP_RVA        0x6C950u
#define CLIP_RETURN_RVA 0x6C958u
#define CLIP_SIZE       8u

static const uint8_t clip_expected[CLIP_SIZE] = {
    0x8B, 0xC8,                          /* mov ecx,eax           */
    0xD9, 0x01,                          /* fld [ecx]             */
    0xD9, 0x44, 0x24, 0x30               /* fld [esp+0x30]        */
};

/* The picture controls, so the layout box can be grown where the texel size lands. Held loosely
 * and every read guarded, the same as the objective boxes. */
#define SAVE_ROWS 128

typedef struct save_entry {
    uintptr_t control;
    float     src_h;       /* the source, as the engine built it, before any clip shrank it */
    float     src_w;       /* never written by this plugin, so it doubles as the identity check */
    float     base_v;      /* the source v origin the engine set, before any crop of ours */
    float     true_y;      /* the position the engine laid out, kept while the crop borrows it */
    bool      moved;       /* whether the position needs putting back this draw */
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
/* Called only from on_texture_draw, which has already validated the control. Re-checking here
 * would be a second VirtualQuery per texture control per frame, and VirtualQuery takes the
 * process address space lock. */
/* Crops the save picture to the list, at the top edge and the bottom.
 *
 * It has to be done here because the engine cannot do it for a control whose scale is not 1.
 * The draw builds the control rectangle as position plus SOURCE size:
 *
 *     1006c909  fld [esi+0x3c] ; fadd [esi+0x74]      bottom = y + source height
 *
 * and then intersects it with the parent clip rectangle, which is in screen pixels. Stock those
 * are the same number, because nothing is scaled. Here the source is 64 and the picture is 288
 * on screen, so the two disagree by the scale, and the intersection is nonsense: while the top
 * row slid into place, y + 64 was still above the list top and the clipped height came out
 * NEGATIVE, then over the last 64 pixels it went positive with the source advancing four and a
 * half times too fast. That is the drop, the picture appearing from nothing and expanding.
 *
 * So the crop is worked out here in screen pixels, converted to texels once, and the engine
 * clip is disarmed in take_over_clip below.
 *
 * The position at +0x3c is moved, which earlier attempts could not do because the value looked
 * like the scroll animation itself. It is not: the animation lives on the LIST, at +0xB4, which
 * carries the scroll origin and decays under friction by 0.917 a frame. The row position is
 * derived from it. Even so, nothing is left behind: take_over_clip runs later in this same draw
 * and puts the engine value back, after the rectangle has been taken from it and before it is
 * used again, so the field is only ever different from the engine version for the few
 * instructions in between. */
static void __cdecl clamp_save_picture(uintptr_t control)
{
    LONG seen = g_save_seen;
    LONG i;

    if (seen > SAVE_ROWS) {
        seen = SAVE_ROWS;
    }

    for (i = 0; i < seen; ++i) {
        uintptr_t row  = 0;
        uintptr_t list = 0;
        float     scale_x = 0.0f;
        float     scale_y = 0.0f;
        float     y        = 0.0f;
        float     screen_h;
        float     full_w;
        float     visible_top;
        float     visible_bottom;
        float     cut;
        float     drawn;
        float     source_v;
        float     source_h;

        if (g_save[i].control != control || g_save[i].src_h <= 0.0f ||
            !still_the_same_control(control, g_save[i].src_w)) {
            continue;
        }

        memcpy(&scale_x, (const void *)(control + CONTROL_SCALE_X), sizeof(scale_x));
        memcpy(&scale_y, (const void *)(control + CONTROL_SCALE_Y), sizeof(scale_y));
        if (scale_y <= 0.0f) {
            return;
        }

        /* The art is square, so the source height is the true extent on both axes and the
         * recorded width carries the empty region past the edge of the texture. */
        full_w   = g_save[i].src_h * scale_x;
        screen_h = g_save[i].src_h * scale_y;

        memcpy(&y, (const void *)(control + 0x3Cu), sizeof(y));
        visible_top    = y;
        visible_bottom = y + screen_h;

        memcpy(&row, (const void *)(control + 0x5Cu), sizeof(uint32_t));
        if (row != 0 && memory_is_readable_range(row, 0x60u)) {
            memcpy(&list, (const void *)(row + 0x5Cu), sizeof(uint32_t));
        }
        if (list != 0 && memory_is_readable_range(list, 0x48u)) {
            float ly = 0.0f;
            float lh = 0.0f;

            memcpy(&ly, (const void *)(list + 0x3Cu), sizeof(ly));
            memcpy(&lh, (const void *)(list + 0x44u), sizeof(lh));
            if (lh > 0.0f) {
                if (visible_top < ly) {
                    visible_top = ly;
                }
                if (visible_bottom > ly + lh) {
                    visible_bottom = ly + lh;
                }
            }
        }

        g_save[i].true_y = y;
        g_save[i].moved  = false;

        /* A row entirely past an edge keeps nothing of itself, and its position is left alone. */
        if (visible_bottom <= visible_top) {
            float none = 0.0f;

            memcpy((void *)(control + 0x74u), &none, sizeof(none));
            memcpy((void *)(control + 0x6Cu), &g_save[i].base_v, sizeof(float));
            memcpy((void *)(control + 0x40u), &full_w, sizeof(full_w));
            memcpy((void *)(control + 0x44u), &screen_h, sizeof(screen_h));
            return;
        }

        cut      = visible_top - y;
        drawn    = visible_bottom - visible_top;
        source_h = drawn / scale_y;

        /* The engine will add (its own clipped top minus the position) to the source origin,
         * and once the position is back to the engine value that difference is the cut in
         * SCREEN pixels. The cut belongs in texels, so what is stored here is the texel figure
         * less the pixel one, and the sum comes out right. */
        source_v = g_save[i].base_v + cut / scale_y - cut;

        memcpy((void *)(control + 0x3Cu), &visible_top, sizeof(visible_top));
        memcpy((void *)(control + 0x6Cu), &source_v, sizeof(source_v));
        memcpy((void *)(control + 0x74u), &source_h, sizeof(source_h));

        /* The layout box keeps the FULL height. It is what the row sizes itself from, and a
         * crop is a thing that happens to one frame of drawing, not to the space the row
         * reserves. Writing the cropped figure here made a partially visible row rebuild itself
         * shorter. */
        memcpy((void *)(control + 0x40u), &full_w, sizeof(full_w));
        memcpy((void *)(control + 0x44u), &screen_h, sizeof(screen_h));

        g_save[i].moved = true;
        return;
    }
}

/* Runs later in the same draw, just after the parent has handed back its clip rectangle.
 *
 * Two things happen here, and both are the second half of the crop above.
 *
 * The rectangle is opened out so it cannot cut anything. It is in screen pixels and the
 * rectangle it would be intersected with is in texels, so any cut it makes is wrong by the
 * scale. clamp_save_picture has already done the intersection properly.
 *
 * And the position is put back to the engine value. The draw has taken its copy by now, at
 * 1006c915, so the crop is already in the rectangle; what remains is the source origin, which
 * the engine works out as (clipped top - position), and that reads correctly against the
 * restored value. Nothing of ours is left in the control when the draw returns. */
static void __cdecl take_over_clip(uintptr_t rect, uintptr_t control)
{
    LONG  seen = g_save_seen;
    LONG  i;
    float wide = 1.0e9f;
    float back = -1.0e9f;

    if (rect == 0 || control == 0 || !memory_is_readable_range(rect, 16u) ||
        !memory_is_readable_range(control, 0x80u)) {
        return;
    }

    if (seen > SAVE_ROWS) {
        seen = SAVE_ROWS;
    }
    for (i = 0; i < seen; ++i) {
        if (g_save[i].control != control || !still_the_same_control(control, g_save[i].src_w)) {
            continue;
        }
        memcpy((void *)(rect + 0x0u), &back, sizeof(back));
        memcpy((void *)(rect + 0x4u), &back, sizeof(back));
        memcpy((void *)(rect + 0x8u), &wide, sizeof(wide));
        memcpy((void *)(rect + 0xCu), &wide, sizeof(wide));
        if (g_save[i].moved) {
            memcpy((void *)(control + 0x3Cu), &g_save[i].true_y, sizeof(float));
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
    g_save[(uint32_t)n % SAVE_ROWS].src_h   = 0.0f;
    g_save[(uint32_t)n % SAVE_ROWS].src_w   = 0.0f;
    g_save[(uint32_t)n % SAVE_ROWS].base_v  = 0.0f;
    g_save[(uint32_t)n % SAVE_ROWS].true_y  = 0.0f;
    g_save[(uint32_t)n % SAVE_ROWS].moved   = false;
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
#define QUEST_SIZE       5u
#define QUEST_CALLEE_RVA 0x6C5D0u

/* Two places build one of these boxes, and they are the same three instructions in both:
 *
 *     1003f5d1 / 1003fc2b  mov ecx,edi          edi is the control, from the allocation above
 *     1003f5d3 / 1003fc2d  call 1006C5D0        the GUIControl_Texture constructor
 *
 * The first is the line the game draws in the corner while you play. The second is the objective
 * menu's own, which reads its geometry from Quest GUI rather than Quest HUD: hud_probe caught
 * rfl+3FBBA, 3FBCB, 3FBE0 and 3FBED reading indices 15 to 18, the Unchecked-Box position and
 * size, on a 29 property object.
 *
 * Only the opcode is checked, and the displacement is resolved and compared against the
 * constructor rather than matched as bytes, because the two sites are at different distances
 * from it. */
static const uint32_t quest_sites[] = { 0x3F5D3u, 0x3FC2Du };

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
    float     base_w;      /* the source, in texels, once it is known */
    float     base_h;
    float     src_w;       /* what the source measured when this entry was recorded */
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
/* Called from the setup stub, on the game thread, with the control alive in hand.
 *
 * If the scale is already known the size is written here and now and the control is not
 * remembered at all. Only a HUD built before a camera has validated needs to be, and that is a
 * one-off at startup: the poll applies those, and holding a pointer for at most a quarter second
 * during the loading screen is the whole of the exposure.
 *
 * A slot the poll has finished with is reused. It used to append at a high water mark that never
 * came down, so once eight controls had ever been recorded the table was full of zeroes and the
 * HUD correction silently stopped working for the rest of the session. */
static void __cdecl remember_hud(uintptr_t control)
{
    LONG count = g_hud_count;
    LONG i;
    LONG slot  = -1;

    if (control == 0) {
        return;
    }

    if (g_scale_x > 1.0f && g_scale_y > 1.0f && g_hud_base_w > 0.0f && g_hud_base_h > 0.0f &&
        memory_is_readable_range(control, 0x48u)) {
        float w = g_hud_base_w * g_scale_x;
        float h = g_hud_base_h * g_scale_y;

        memcpy((void *)(control + 0x40u), &w, sizeof(w));
        memcpy((void *)(control + 0x44u), &h, sizeof(h));
        log_info("control %08X built from %.0f x %.0f texels",
                 (unsigned)control, (double)g_hud_base_w, (double)g_hud_base_h);
        return;
    }

    for (i = 0; i < count && i < HUD_ROWS; ++i) {
        if (g_hud[i].control == control) {
            g_hud[i].base_w = g_hud_base_w;
            g_hud[i].base_h = g_hud_base_h;
            return;
        }
        if (g_hud[i].control == 0 && slot < 0) {
            slot = i;
        }
    }
    if (slot < 0) {
        if (count >= HUD_ROWS) {
            return;
        }
        slot = count;
    }
    g_hud[slot].base_w  = g_hud_base_w;
    g_hud[slot].base_h  = g_hud_base_h;
    g_hud[slot].control = control;
    if (slot == count) {
        InterlockedExchange(&g_hud_count, count + 1);
    }
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
    g_quest[(uint32_t)n % QUEST_ROWS].src_w  = 0.0f;
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

                /* Only the source is recorded here, and only once per picture.
                 *
                 * The height read is control+0x74, which is the field the clip writes the cropped
                 * source into, so re-reading it every time turned a partial clip into a permanent
                 * one. +0x70 is never written by this plugin, so a genuine change of picture in
                 * the same control shows up there and is the signal to measure again.
                 *
                 * The SIZE is deliberately not worked out here. This runs when the texture is
                 * set, which can be before the menu calls SetScale, and a size captured then is
                 * built from a scale pair that is still 1.0 and never corrected: the pictures
                 * come out at 64 by 64 and the rows stay short. The size is derived at the draw
                 * instead, from whatever the pair holds by then. */
                if (w > 0.0f && h > 0.0f && g_save[j].src_w != w) {
                    /* The source origin comes from the engine, and only the first time. After
                     * that the field holds whatever the crop last put there. It is zero for
                     * every picture measured so far, but reading it costs nothing and a picture
                     * packed into a shared texture would not be. */
                    if (g_save[j].src_w == 0.0f) {
                        memcpy(&g_save[j].base_v, (const void *)(control + 0x6Cu), sizeof(float));
                    }
                    g_save[j].src_h = h;
                    g_save[j].src_w = w;
                }

                /* And the layout box, written HERE, because the row lays itself out when the
                 * list is built and that is before anything is drawn. Deriving it at the draw
                 * instead was correct arithmetic arriving too late: the pictures came out the
                 * right 512 by 288 inside rows that had already sized themselves to 64.
                 *
                 * It comes from the recorded source rather than the field below, which the clip
                 * shrinks, and from the pair as it stands now, which the menu has already set. */
                if (g_save[j].src_h > 0.0f) {
                    float sx = 0.0f;
                    float sy = 0.0f;

                    memcpy(&sx, (const void *)(control + CONTROL_SCALE_X), sizeof(sx));
                    memcpy(&sy, (const void *)(control + CONTROL_SCALE_Y), sizeof(sy));

                    if (sx > 0.0f && sy > 0.0f && control_writable(control + 0x40u, 8u)) {
                        float lw = g_save[j].src_h * sx;
                        float lh = g_save[j].src_h * sy;

                        memcpy((void *)(control + 0x40u), &lw, sizeof(lw));
                        memcpy((void *)(control + 0x44u), &lh, sizeof(lh));
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
                g_quest[i].src_w  = w;

                if (control_writable(control + 0x40u, 8u)) {
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
static void *build_save_icon_stub(uintptr_t stub_address, uintptr_t return_address,
                                  uint8_t push_control)
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
    emit_u8(&emit, push_control);                            /* push the control's register  */
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

    /* The pointer, which is a GUIControl_Texture and so passes through here like the rest. Its
     * constructor puts 1.0 back every time the manager rebuilds it, which is why it is written
     * again on every draw rather than once. */
    if (control == g_cursor && x > 1.0f) {
        memcpy((void *)(control + CONTROL_SCALE_X), &x, sizeof(x));
        memcpy((void *)(control + CONTROL_SCALE_Y), &y, sizeof(y));
    }

    /* The objective tick boxes: the scale pair, and the layout box the row reserves from. */
    seen = g_quest_seen;
    if (seen > QUEST_ROWS) {
        seen = QUEST_ROWS;
    }
    for (i = 0; i < seen; ++i) {
        if (g_quest[i].control != control ||
            !still_the_same_control(control, g_quest[i].src_w)) {
            continue;
        }
        if (x > 1.0f && control_writable(control + CONTROL_SCALE_X, 8u)) {
            memcpy((void *)(control + CONTROL_SCALE_X), &x, sizeof(x));
            memcpy((void *)(control + CONTROL_SCALE_Y), &y, sizeof(y));
        }
        if (g_quest[i].base_w > 0.0f && control_writable(control + 0x40u, 8u)) {
            float w = g_quest[i].base_w * x;
            float h = g_quest[i].base_h * y;

            memcpy((void *)(control + 0x40u), &w, sizeof(w));
            memcpy((void *)(control + 0x44u), &h, sizeof(h));
        }
        break;
    }

    clamp_save_picture(control);
}

/* Every argument is on the stack by now, so arg1 sits at esp+0x24 once pushad and pushfd have
 * gone on. The call's target is read from its own displacement rather than assumed. */
static void *build_arrow_stub(uintptr_t stub_address, uintptr_t return_address, uintptr_t callee,
                              uint8_t grows_up)
{
    uint8_t buffer[64];
    emit_t  emit;

    emit_init(&emit, buffer, sizeof(buffer));

    emit_u8(&emit, 0x60);                                    /* pushad                       */
    emit_u8(&emit, 0x9C);                                    /* pushfd                       */
    emit_u8(&emit, 0x8D); emit_u8(&emit, 0x44); emit_u8(&emit, 0x24); emit_u8(&emit, 0x24);
    emit_u8(&emit, 0x6A); emit_u8(&emit, grows_up);          /* push which edge to hold      */
    emit_u8(&emit, 0x50);                                    /* push eax, the argument list  */
    emit_u8(&emit, 0xE8);
    emit_u32(&emit, (uint32_t)((uintptr_t)&scale_arrow -
                               (stub_address + (uintptr_t)emit_size(&emit) + 4u)));
    emit_u8(&emit, 0x83); emit_u8(&emit, 0xC4); emit_u8(&emit, 0x08);
    emit_u8(&emit, 0x9D);                                    /* popfd                        */
    emit_u8(&emit, 0x61);                                    /* popad                        */

    emit_u8(&emit, 0x8B); emit_u8(&emit, 0xCE);              /* mov ecx,esi                  */
    emit_u8(&emit, 0xE8);
    emit_u32(&emit, (uint32_t)(callee - (stub_address + (uintptr_t)emit_size(&emit) + 4u)));

    emit_jump_rel32(&emit, stub_address, return_address);

    if (emit_overflowed(&emit)) {
        return NULL;
    }
    memcpy((void *)stub_address, buffer, emit_size(&emit));
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)stub_address, emit_size(&emit));
    return (void *)stub_address;
}

/* eax points at the property value on entry and esi is the control. The three displaced
 * instructions are performed here rather than replayed, so that ecx picks up the scaled figure
 * instead of the one the property holds. */
static void *build_border_stub(uintptr_t stub_address, uintptr_t return_address)
{
    uint8_t buffer[64];
    emit_t  emit;

    emit_init(&emit, buffer, sizeof(buffer));

    emit_u8(&emit, 0x60);                                    /* pushad                       */
    emit_u8(&emit, 0x9C);                                    /* pushfd                       */
    emit_u8(&emit, 0x50);                                    /* push eax, the value          */
    emit_u8(&emit, 0xE8);
    emit_u32(&emit, (uint32_t)((uintptr_t)&scale_border -
                               (stub_address + (uintptr_t)emit_size(&emit) + 4u)));
    emit_u8(&emit, 0x83); emit_u8(&emit, 0xC4); emit_u8(&emit, 0x04);
    emit_u8(&emit, 0x9D);                                    /* popfd                        */
    emit_u8(&emit, 0x61);                                    /* popad                        */

    emit_u8(&emit, 0x8B); emit_u8(&emit, 0x0D);              /* mov ecx,[g_border_scale]     */
    emit_u32(&emit, (uint32_t)(uintptr_t)&g_border_scale);
    emit_u8(&emit, 0x8B); emit_u8(&emit, 0x16);              /* mov edx,[esi]                */
    emit_u8(&emit, 0x8B); emit_u8(&emit, 0xC1);              /* mov eax,ecx                  */

    emit_jump_rel32(&emit, stub_address, return_address);

    if (emit_overflowed(&emit)) {
        return NULL;
    }
    memcpy((void *)stub_address, buffer, emit_size(&emit));
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)stub_address, emit_size(&emit));
    return (void *)stub_address;
}

/* As the arrow stub, but the displaced instruction loads ecx from edi rather than esi. */
static void *build_checkbox_stub(uintptr_t stub_address, uintptr_t return_address,
                                 uintptr_t callee)
{
    uint8_t buffer[64];
    emit_t  emit;

    emit_init(&emit, buffer, sizeof(buffer));

    emit_u8(&emit, 0x60);                                    /* pushad                       */
    emit_u8(&emit, 0x9C);                                    /* pushfd                       */
    emit_u8(&emit, 0x8D); emit_u8(&emit, 0x44); emit_u8(&emit, 0x24); emit_u8(&emit, 0x24);
    emit_u8(&emit, 0x50);                                    /* push eax, the argument list  */
    emit_u8(&emit, 0xE8);
    emit_u32(&emit, (uint32_t)((uintptr_t)&scale_checkbox -
                               (stub_address + (uintptr_t)emit_size(&emit) + 4u)));
    emit_u8(&emit, 0x83); emit_u8(&emit, 0xC4); emit_u8(&emit, 0x04);
    emit_u8(&emit, 0x9D);                                    /* popfd                        */
    emit_u8(&emit, 0x61);                                    /* popad                        */

    emit_u8(&emit, 0x8B); emit_u8(&emit, 0xCF);              /* mov ecx,edi                  */
    emit_u8(&emit, 0xE8);
    emit_u32(&emit, (uint32_t)(callee - (stub_address + (uintptr_t)emit_size(&emit) + 4u)));

    emit_jump_rel32(&emit, stub_address, return_address);

    if (emit_overflowed(&emit)) {
        return NULL;
    }
    memcpy((void *)stub_address, buffer, emit_size(&emit));
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)stub_address, emit_size(&emit));
    return (void *)stub_address;
}

/* Runs where the picture's draw reads its own geometry, with the control in esi, and performs
 * the two displaced instructions afterwards. Neither is position dependent. */
/* eax is the clip rectangle the parent just handed back and esi is the control, and pushad
 * leaves both alone, so the stub reads them where they lie. esp is restored before the relocated
 * fld [esp+0x30] runs. */
static void *build_clip_stub(uintptr_t stub_address, uintptr_t return_address)
{
    uint8_t buffer[64];
    emit_t  emit;

    emit_init(&emit, buffer, sizeof(buffer));

    emit_u8(&emit, 0x60);                                    /* pushad                       */
    emit_u8(&emit, 0x9C);                                    /* pushfd                       */
    emit_u8(&emit, 0x56);                                    /* push esi, the control        */
    emit_u8(&emit, 0x50);                                    /* push eax, the clip rectangle */
    emit_u8(&emit, 0xE8);
    emit_u32(&emit, (uint32_t)((uintptr_t)&take_over_clip -
                               (stub_address + (uintptr_t)emit_size(&emit) + 4u)));
    emit_u8(&emit, 0x83); emit_u8(&emit, 0xC4); emit_u8(&emit, 0x08);
    emit_u8(&emit, 0x9D);                                    /* popfd                        */
    emit_u8(&emit, 0x61);                                    /* popad                        */

    {
        unsigned i;

        for (i = 0; i < CLIP_SIZE; ++i) {
            emit_u8(&emit, clip_expected[i]);
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
        camera_view_t view;

        /* camera_read, not a raw read of a remembered pointer. It checks the vtable, bounds the
         * viewport, range checks the halves and the focal, and rejects a camera caught midway
         * through SetViewport. Reading the fields directly and accepting anything above zero let
         * a torn or dead camera through, and the scale it produced went straight into every
         * generated stub. */
        if (camera_read(&view) && view.viewport_width > 0 && view.viewport_height > 0) {
            float x = (float)view.viewport_width / (float)g_reference_width;
            float y = (float)view.viewport_height / (float)g_reference_height;

            g_scale_x = x;
            g_scale_y = y;

            if (x != announced_x) {
                announced_x = x;
                log_info("viewport %ldx%ld -> scale %.4f x %.4f",
                         (long)view.viewport_width, (long)view.viewport_height,
                         (double)x, (double)y);
            }

            /* The only controls left here are those built before a camera validated, when the
             * scale was still 1.0. Each is written once and its slot released, so the pointer is
             * held for at most one pass. Everything else is written at its own draw, where the
             * engine has just handed the control over alive. */
            {
                LONG n = g_hud_count;
                LONG i;

                for (i = 0; i < n && i < HUD_ROWS; ++i) {
                    uintptr_t c = g_hud[i].control;
                    float     w = g_hud[i].base_w * x;
                    float     h = g_hud[i].base_h * y;

                    if (c != 0 && w > 0.0f && h > 0.0f &&
                        memory_is_readable_range(c, 0x48u)) {
                        memcpy((void *)(c + 0x40u), &w, sizeof(w));
                        memcpy((void *)(c + 0x44u), &h, sizeof(h));
                        g_hud[i].control = 0;
                    }
                }
            }
        }
        Sleep(250);
    }
}

static void on_rfl_loaded(uintptr_t rfl_base)
{
    HANDLE thread;

    /* The pointer is one group of nine and is not allowed to speak for the rest. This used to
     * return from the whole function when its site did not match, which took the eight groups
     * below it down with it, every one of which would have validated on its own. The README says
     * the groups are independent; this is what makes that true. */
    {
        uintptr_t site = rfl_site(rfl_base, CURSOR_RVA);
        uintptr_t stub_address;

        if (!patch_validate_bytes(site, cursor_expected, CURSOR_SIZE)) {
            log_warning("rfl+%X is not the store this was measured against; the pointer is left "
                        "alone and everything else is installed as normal", CURSOR_RVA);
        } else if ((stub_address = (uintptr_t)trampoline_alloc(32)) == 0 ||
                   build_stub(stub_address, rfl_site(rfl_base, CURSOR_RETURN_RVA)) == NULL ||
                   patch_write_jump(site, (const void *)stub_address,
                                    CURSOR_SIZE) != PATCH_RESULT_OK) {
            log_warning("the pointer hook could not be installed; everything else is unaffected");
        } else {
            log_info("installed: rfl+%X -> stub at %08X, waiting for the pointer control",
                     CURSOR_RVA, (unsigned)stub_address);
        }
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
            if (!patch_validate_bytes(rfl_site(rfl_base, save_icon_sites[index].rva),
                                      save_icon_expected, SAVE_ICON_SIZE)) {
                log_warning("rfl+%X is not a save picture scale push; the menu is left alone",
                            save_icon_sites[index].rva);
                n = 0;
                break;
            }
        }
        for (index = 0; index < n; ++index) {
            uintptr_t icon = rfl_site(rfl_base, save_icon_sites[index].rva);
            uintptr_t stub = (uintptr_t)trampoline_alloc(64);

            if (stub != 0 &&
                build_save_icon_stub(stub, icon + SAVE_ICON_SIZE,
                                     save_icon_sites[index].push_control) != NULL &&
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
        uintptr_t clip = rfl_site(rfl_base, CLIP_RVA);

        if (patch_validate_bytes(clip, clip_expected, CLIP_SIZE)) {
            uintptr_t stub = (uintptr_t)trampoline_alloc(64);

            if (stub != 0 &&
                build_clip_stub(stub, rfl_site(rfl_base, CLIP_RETURN_RVA)) != NULL &&
                patch_write_jump(clip, (const void *)stub, CLIP_SIZE) == PATCH_RESULT_OK) {
                log_info("  and rfl+%X -> stub at %08X, the save picture crop",
                         CLIP_RVA, (unsigned)stub);
            } else {
                log_warning("the save picture crop could not be installed");
            }
        } else {
            log_warning("rfl+%X is not the parent clip this expects", CLIP_RVA);
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
        uintptr_t callee = rfl_site(rfl_base, QUEST_CALLEE_RVA);
        size_t    index;
        unsigned  done = 0;
        unsigned  n    = (unsigned)(sizeof(quest_sites) / sizeof(quest_sites[0]));

        for (index = 0; index < n; ++index) {
            uintptr_t site = rfl_site(rfl_base, quest_sites[index]);
            uint8_t   opcode = 0;
            int32_t   displacement = 0;

            memcpy(&opcode, (const void *)site, sizeof(opcode));
            memcpy(&displacement, (const void *)(site + 1u), sizeof(displacement));

            if (opcode != 0xE8u || site + QUEST_SIZE + (uintptr_t)displacement != callee) {
                log_warning("rfl+%X does not call the control constructor; the boxes it builds "
                            "are left alone", quest_sites[index]);
                continue;
            }
            {
                uintptr_t stub = (uintptr_t)trampoline_alloc(64);

                if (stub != 0 &&
                    build_quest_stub(stub, site + QUEST_SIZE, callee) != NULL &&
                    patch_write_jump(site, (const void *)stub, QUEST_SIZE) == PATCH_RESULT_OK) {
                    done++;
                }
            }
        }
        log_info("  and %u of %u objective box builders record what they make", done, n);
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


    {
        size_t   index;
        unsigned done = 0;
        unsigned n    = (unsigned)(sizeof(arrow_sites) / sizeof(arrow_sites[0]));

        for (index = 0; index < n; ++index) {
            uintptr_t arrow = rfl_site(rfl_base, arrow_sites[index].rva);
            int32_t   displacement = 0;

            if (!patch_validate_bytes(arrow, arrow_expected, sizeof(arrow_expected))) {
                log_warning("rfl+%X is not a scroll arrow draw; that arrow is left alone",
                            arrow_sites[index].rva);
                continue;
            }
            memcpy(&displacement, (const void *)(arrow + 3u), sizeof(displacement));
            {
                uintptr_t stub = (uintptr_t)trampoline_alloc(64);

                if (stub != 0 &&
                    build_arrow_stub(stub, arrow + ARROW_HOOK_SIZE,
                                     arrow + ARROW_HOOK_SIZE + (uintptr_t)displacement,
                                     arrow_sites[index].grows_up) != NULL &&
                    patch_write_jump(arrow, (const void *)stub,
                                     ARROW_HOOK_SIZE) == PATCH_RESULT_OK) {
                    done++;
                }
            }
        }
        log_info("  and %u of %u scroll arrows size themselves to the screen", done, n);
    }

    {
        uintptr_t box = rfl_site(rfl_base, CHECKBOX_RVA);
        int32_t   displacement = 0;

        if (patch_validate_bytes(box, checkbox_expected, sizeof(checkbox_expected))) {
            uintptr_t stub = (uintptr_t)trampoline_alloc(64);

            memcpy(&displacement, (const void *)(box + 3u), sizeof(displacement));
            if (stub != 0 &&
                build_checkbox_stub(stub, box + CHECKBOX_HOOK_SIZE,
                                    box + CHECKBOX_HOOK_SIZE + (uintptr_t)displacement) != NULL &&
                patch_write_jump(box, (const void *)stub,
                                 CHECKBOX_HOOK_SIZE) == PATCH_RESULT_OK) {
                log_info("  and rfl+%X -> stub at %08X, the options menu check boxes",
                         CHECKBOX_RVA, (unsigned)stub);
            } else {
                log_warning("the options menu check boxes could not be installed");
            }
        } else {
            log_warning("rfl+%X is not the check box draw this expects", CHECKBOX_RVA);
        }
    }

    {
        uintptr_t border = rfl_site(rfl_base, BORDER_RVA);

        if (patch_validate_bytes(border, border_expected, BORDER_HOOK_SIZE)) {
            uintptr_t stub = (uintptr_t)trampoline_alloc(64);

            if (stub != 0 &&
                build_border_stub(stub, border + BORDER_HOOK_SIZE) != NULL &&
                patch_write_jump(border, (const void *)stub,
                                 BORDER_HOOK_SIZE) == PATCH_RESULT_OK) {
                log_info("  and rfl+%X -> stub at %08X, the box borders",
                         BORDER_RVA, (unsigned)stub);
            } else {
                log_warning("the box borders could not be installed");
            }
        } else {
            log_warning("rfl+%X is not the border scale this expects", BORDER_RVA);
        }
    }

    thread = CreateThread(NULL, 0, hold_scale, NULL, 0, NULL);
    if (thread != NULL) {
        CloseHandle(thread);
    } else {
        log_error("could not start the scale thread; nothing would ever be written");
        return;
    }

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

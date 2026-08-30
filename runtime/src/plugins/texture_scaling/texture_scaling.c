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

    if (control == 0 || !memory_is_readable_range(control, 0x78u)) {
        return;
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

/* Replaces a constant push with a push of our live scale. Two instructions, eleven bytes. */
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

                /* The objective tick boxes. A scale, not a derived size, so writing it again
                 * over the same control is idempotent and cannot compound. */
                {
                    LONG seen = g_quest_seen;
                    LONG i;

                    if (seen > QUEST_ROWS) {
                        seen = QUEST_ROWS;
                    }
                    for (i = 0; i < seen; ++i) {
                        uintptr_t c = g_quest[i].control;

                        if (c == 0 || !memory_is_readable_range(c, CONTROL_SCALE_Y + 4u)) {
                            continue;
                        }

                        /* The drawn extent is the source multiplied by this pair. */
                        if (memory_make_writable(c + CONTROL_SCALE_X, 8u)) {
                            memcpy((void *)(c + CONTROL_SCALE_X), &x, sizeof(x));
                            memcpy((void *)(c + CONTROL_SCALE_Y), &y, sizeof(y));
                        }

                        /* And the layout box, which is what the row around it reserves space
                         * from. Scaling the drawn size alone leaves the text of the objective
                         * starting where a 19 texel box would have ended, on top of it.
                         *
                         * Captured once, the first time it is seen non zero, and re-derived from
                         * that every pass, so this cannot compound. */
                        if (g_quest[i].base_w == 0.0f) {
                            float w = 0.0f;
                            float h = 0.0f;

                            memcpy(&w, (const void *)(c + 0x40u), sizeof(w));
                            memcpy(&h, (const void *)(c + 0x44u), sizeof(h));
                            if (w > 0.0f && h > 0.0f) {
                                g_quest[i].base_w = w;
                                g_quest[i].base_h = h;
                            }
                        }
                        if (g_quest[i].base_w > 0.0f && memory_make_writable(c + 0x40u, 8u)) {
                            float w = g_quest[i].base_w * x;
                            float h = g_quest[i].base_h * y;

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

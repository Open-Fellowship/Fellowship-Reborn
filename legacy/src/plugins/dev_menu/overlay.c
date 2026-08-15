#include "overlay.h"
#include "d3d8_min.h"

#include "common/logging.h"

#include <windows.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FIRST_GLYPH   32
#define LAST_GLYPH    126
#define GLYPH_COUNT   (LAST_GLYPH - FIRST_GLYPH + 1)
#define MAX_GLYPH_W   24
#define MAX_GLYPH_H   32

/* 8000 quads is roughly ten lines of forty characters with room to spare, at a scale where each
 * lit pixel is its own rectangle. Allocated once, never resized: a draw path that can fail to
 * allocate halfway through a frame is a draw path that can fail in front of the user. */
#define MAX_QUADS     8000
#define MAX_VERTICES  (MAX_QUADS * 6)

typedef struct glyph {
    int      width;
    uint32_t rows[MAX_GLYPH_H];   /* bit n of rows[y] = column n is lit */
} glyph_t;

static glyph_t          g_glyphs[GLYPH_COUNT];
static int              g_line_height;
static bool             g_ready;
static overlay_vertex_t *g_vertices;
static int              g_count;
static bool             g_overflowed;

/* --------------------------------------------------------------------------- font rasterising */

/* GDI once, at install time, on our own thread - not in the draw path. The result is a table of
 * bitmasks; GDI is never touched again and no device object is created. */
static bool rasterise(int pixel_height)
{
    BITMAPINFO   info;
    HDC          dc      = NULL;
    HBITMAP      bitmap  = NULL;
    HFONT        font    = NULL;
    HGDIOBJ      old_bmp = NULL;
    HGDIOBJ      old_fnt = NULL;
    uint32_t    *pixels  = NULL;
    bool         ok      = false;
    int          index;

    if (pixel_height < 8)  { pixel_height = 8; }
    if (pixel_height > MAX_GLYPH_H) { pixel_height = MAX_GLYPH_H; }

    dc = CreateCompatibleDC(NULL);
    if (dc == NULL) {
        return false;
    }

    memset(&info, 0, sizeof(info));
    info.bmiHeader.biSize        = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth       = MAX_GLYPH_W;
    info.bmiHeader.biHeight      = -MAX_GLYPH_H;    /* negative: top-down, so y is y */
    info.bmiHeader.biPlanes      = 1;
    info.bmiHeader.biBitCount    = 32;
    info.bmiHeader.biCompression = BI_RGB;

    bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, (void **)&pixels, NULL, 0);
    if (bitmap == NULL || pixels == NULL) {
        goto done;
    }

    /* A fixed-pitch face, so the menu lines up in columns. FF_MODERN|FIXED_PITCH lets the font
     * mapper pick whatever the machine actually has rather than naming one that may not exist. */
    font = CreateFontA(pixel_height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       NONANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
    if (font == NULL) {
        goto done;
    }

    old_bmp = SelectObject(dc, bitmap);
    old_fnt = SelectObject(dc, font);
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkColor(dc, RGB(0, 0, 0));
    SetBkMode(dc, OPAQUE);

    for (index = 0; index < GLYPH_COUNT; ++index) {
        char  character = (char)(FIRST_GLYPH + index);
        SIZE  extent;
        RECT  cell;
        int   x;
        int   y;

        cell.left = 0; cell.top = 0; cell.right = MAX_GLYPH_W; cell.bottom = MAX_GLYPH_H;
        memset(pixels, 0, (size_t)MAX_GLYPH_W * MAX_GLYPH_H * sizeof(uint32_t));
        ExtTextOutA(dc, 0, 0, ETO_OPAQUE, &cell, &character, 1, NULL);
        GdiFlush();

        if (!GetTextExtentPoint32A(dc, &character, 1, &extent)) {
            extent.cx = pixel_height / 2;
        }
        g_glyphs[index].width = (extent.cx > 0 && extent.cx <= MAX_GLYPH_W)
                                ? extent.cx : pixel_height / 2;

        for (y = 0; y < MAX_GLYPH_H; ++y) {
            uint32_t row = 0;
            for (x = 0; x < MAX_GLYPH_W && x < 32; ++x) {
                if ((pixels[(size_t)y * MAX_GLYPH_W + x] & 0x00FFFFFFu) != 0) {
                    row |= (1u << x);
                }
            }
            g_glyphs[index].rows[y] = row;
        }
    }

    g_line_height = pixel_height;
    ok = true;

done:
    if (old_fnt) { SelectObject(dc, old_fnt); }
    if (old_bmp) { SelectObject(dc, old_bmp); }
    if (font)    { DeleteObject(font); }
    if (bitmap)  { DeleteObject(bitmap); }
    DeleteDC(dc);
    return ok;
}

bool overlay_prepare(int pixel_height)
{
    if (g_ready) {
        return true;
    }
    if (!rasterise(pixel_height)) {
        log_error("could not rasterise the overlay font");
        return false;
    }
    g_vertices = (overlay_vertex_t *)calloc(MAX_VERTICES, sizeof(overlay_vertex_t));
    if (g_vertices == NULL) {
        log_error("could not allocate the overlay vertex batch");
        return false;
    }
    g_ready = true;
    return true;
}

int overlay_line_height(void)
{
    return g_line_height;
}

/* ------------------------------------------------------------------------------- the batch */

void overlay_begin(void)
{
    g_count      = 0;
    g_overflowed = false;
}

void overlay_rect(int x, int y, int width, int height, unsigned colour)
{
    overlay_vertex_t *v;
    float             left   = (float)x - 0.5f;   /* the half-pixel offset D3D9-era rasterising */
    float             top    = (float)y - 0.5f;   /* wants, so edges land on pixel centres      */
    float             right  = left + (float)width;
    float             bottom = top + (float)height;
    int               i;

    if (!g_ready || width <= 0 || height <= 0) {
        return;
    }
    if (g_count + 6 > MAX_VERTICES) {
        g_overflowed = true;
        return;
    }

    v = g_vertices + g_count;
    v[0].x = left;  v[0].y = top;
    v[1].x = right; v[1].y = top;
    v[2].x = left;  v[2].y = bottom;
    v[3].x = right; v[3].y = top;
    v[4].x = right; v[4].y = bottom;
    v[5].x = left;  v[5].y = bottom;
    for (i = 0; i < 6; ++i) {
        v[i].z      = 0.0f;
        v[i].rhw    = 1.0f;
        v[i].colour = colour;
    }
    g_count += 6;
}

void overlay_frame(int x, int y, int width, int height, int thickness, unsigned colour)
{
    overlay_rect(x, y, width, thickness, colour);
    overlay_rect(x, y + height - thickness, width, thickness, colour);
    overlay_rect(x, y, thickness, height, colour);
    overlay_rect(x + width - thickness, y, thickness, height, colour);
}

int overlay_text_width(const char *text, int scale)
{
    int width = 0;

    if (!g_ready || text == NULL) {
        return 0;
    }
    if (scale < 1) { scale = 1; }

    for (; *text != '\0'; ++text) {
        unsigned char c = (unsigned char)*text;
        if (c < FIRST_GLYPH || c > LAST_GLYPH) {
            c = ' ';
        }
        width += g_glyphs[c - FIRST_GLYPH].width * scale;
    }
    return width;
}

/* Each row of each glyph becomes one rectangle per RUN of lit pixels rather than one per pixel.
 * On this font that is about a third of the quads, for four lines of code. */
void overlay_text(int x, int y, int scale, unsigned colour, const char *text)
{
    int pen = x;

    if (!g_ready || text == NULL) {
        return;
    }
    if (scale < 1) { scale = 1; }

    for (; *text != '\0'; ++text) {
        unsigned char  c = (unsigned char)*text;
        const glyph_t *glyph;
        int            row;

        if (c < FIRST_GLYPH || c > LAST_GLYPH) {
            c = ' ';
        }
        glyph = &g_glyphs[c - FIRST_GLYPH];

        for (row = 0; row < MAX_GLYPH_H; ++row) {
            uint32_t bits = glyph->rows[row];
            int      column = 0;

            while (bits != 0) {
                int start;
                int length;

                while (column < 32 && (bits & (1u << column)) == 0) {
                    ++column;
                }
                if (column >= 32) {
                    break;
                }
                start = column;
                while (column < 32 && (bits & (1u << column)) != 0) {
                    ++column;
                }
                length = column - start;
                bits &= ~(((length >= 32) ? 0xFFFFFFFFu : ((1u << length) - 1u)) << start);

                overlay_rect(pen + start * scale, y + row * scale,
                             length * scale, scale, colour);
            }
        }
        pen += glyph->width * scale;
    }
}

/* -------------------------------------------------------------------------------- the draw */

/* Every state this function sets is read back first and put back afterwards. The game is in the
 * middle of its own frame; leaving alpha blending on, or the vertex shader pointing at our FVF,
 * would corrupt whatever it draws next and the symptom would look nothing like this plugin. */
#define SAVED_STATE_COUNT 12

static const DWORD saved_states[SAVED_STATE_COUNT] = {
    D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHATESTENABLE, D3DRS_SRCBLEND, D3DRS_DESTBLEND,
    D3DRS_CULLMODE, D3DRS_ALPHABLENDENABLE, D3DRS_FOGENABLE, D3DRS_STENCILENABLE,
    D3DRS_CLIPPING, D3DRS_LIGHTING, D3DRS_SHADEMODE
};

static const DWORD wanted_states[SAVED_STATE_COUNT] = {
    FALSE, FALSE, FALSE, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA,
    D3DCULL_NONE, TRUE, FALSE, FALSE,
    TRUE, FALSE, D3DSHADE_GOURAUD
};

void overlay_flush(void *device)
{
    void                   **vtable;
    d3d8_get_render_state_t  get_state;
    d3d8_set_render_state_t  set_state;
    d3d8_set_stage_state_t   set_stage;
    d3d8_get_stage_state_t   get_stage;
    DWORD                    previous[SAVED_STATE_COUNT];
    DWORD                    previous_stage[4];
    DWORD                    previous_shader = 0;
    void                    *previous_texture = NULL;
    int                      i;

    if (!g_ready || g_count == 0 || device == NULL) {
        return;
    }

    vtable    = d3d8_vtable(device);
    get_state = (d3d8_get_render_state_t)vtable[D3D8_GETRENDERSTATE];
    set_state = (d3d8_set_render_state_t)vtable[D3D8_SETRENDERSTATE];
    get_stage = (d3d8_get_stage_state_t)vtable[D3D8_GETTEXTURESTAGESTATE];
    set_stage = (d3d8_set_stage_state_t)vtable[D3D8_SETTEXTURESTAGESTATE];

    for (i = 0; i < SAVED_STATE_COUNT; ++i) {
        previous[i] = 0;
        get_state(device, saved_states[i], &previous[i]);
        set_state(device, saved_states[i], wanted_states[i]);
    }

    get_stage(device, 0, D3DTSS_COLOROP,   &previous_stage[0]);
    get_stage(device, 0, D3DTSS_COLORARG1, &previous_stage[1]);
    get_stage(device, 0, D3DTSS_ALPHAOP,   &previous_stage[2]);
    get_stage(device, 0, D3DTSS_ALPHAARG1, &previous_stage[3]);
    set_stage(device, 0, D3DTSS_COLOROP,   D3DTOP_SELECTARG1);
    set_stage(device, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    set_stage(device, 0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1);
    set_stage(device, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);

    ((d3d8_get_texture_t)vtable[D3D8_GETTEXTURE])(device, 0, &previous_texture);
    ((d3d8_set_texture_t)vtable[D3D8_SETTEXTURE])(device, 0, NULL);

    ((d3d8_get_vertex_shader_t)vtable[D3D8_GETVERTEXSHADER])(device, &previous_shader);
    ((d3d8_set_vertex_shader_t)vtable[D3D8_SETVERTEXSHADER])(device, OVERLAY_FVF);

    ((d3d8_draw_primitive_up_t)vtable[D3D8_DRAWPRIMITIVEUP])(
        device, D3DPT_TRIANGLELIST, (UINT)(g_count / 3), g_vertices,
        (UINT)sizeof(overlay_vertex_t));

    ((d3d8_set_vertex_shader_t)vtable[D3D8_SETVERTEXSHADER])(device, previous_shader);

    /* GetTexture added a reference; putting it back and releasing ours keeps the count even. */
    ((d3d8_set_texture_t)vtable[D3D8_SETTEXTURE])(device, 0, previous_texture);
    if (previous_texture != NULL) {
        typedef ULONG (STDMETHODCALLTYPE *release_t)(void *);
        ((release_t)(*(void ***)previous_texture)[2])(previous_texture);
    }

    set_stage(device, 0, D3DTSS_COLOROP,   previous_stage[0]);
    set_stage(device, 0, D3DTSS_COLORARG1, previous_stage[1]);
    set_stage(device, 0, D3DTSS_ALPHAOP,   previous_stage[2]);
    set_stage(device, 0, D3DTSS_ALPHAARG1, previous_stage[3]);

    for (i = 0; i < SAVED_STATE_COUNT; ++i) {
        set_state(device, saved_states[i], previous[i]);
    }
}

bool overlay_overflowed(void)
{
    return g_overflowed;
}

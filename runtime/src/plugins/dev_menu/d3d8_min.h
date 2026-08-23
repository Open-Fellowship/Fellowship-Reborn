/* d3d8_min.h: as much of IDirect3DDevice8 as this plugin calls, and not one method more.
 *
 * DO NOT REPLACE THIS WITH #include <d3d8.h>. That header shipped with the old DirectX SDK and
 * is not in the modern Windows SDK, so a clean machine would not find it. The methods used here
 * are declared by vtable index instead, which is what a COM call is anyway.
 *
 * The indices are pinned by the executable's own calls at 0x0047BDE3. See README.md.
 */
#ifndef DEV_MENU_D3D8_MIN_H
#define DEV_MENU_D3D8_MIN_H

#include <windows.h>

/* ------------------------------------------------------------------- vtable indices we call */

#define D3D8_ENDSCENE               35
#define D3D8_GETVIEWPORT            41
#define D3D8_SETRENDERSTATE         50
#define D3D8_GETRENDERSTATE         51
#define D3D8_GETTEXTURE             60
#define D3D8_SETTEXTURE             61
#define D3D8_GETTEXTURESTAGESTATE   62
#define D3D8_SETTEXTURESTAGESTATE   63
#define D3D8_DRAWPRIMITIVEUP        72

/* DrawPrimitiveUP UNBINDS stream 0, and DrawIndexedPrimitiveUP unbinds the index buffer with it.
 * That is documented behaviour and not a side effect anyone would guess at, so the four calls
 * needed to put them back are here. */
#define D3D8_SETSTREAMSOURCE        83
#define D3D8_GETSTREAMSOURCE        84
#define D3D8_SETINDICES             85
#define D3D8_GETINDICES             86
#define D3D8_GETVERTEXSHADER        77
#define D3D8_SETVERTEXSHADER        76

/* The highest index this plugin touches, and therefore how much of the vtable has to be readable
 * before any of it is believed. */
#define D3D8_VTABLE_ENTRIES_USED    87

/* ------------------------------------------------------------------------- the method shapes */

typedef HRESULT (STDMETHODCALLTYPE *d3d8_end_scene_t)(void *device);
typedef HRESULT (STDMETHODCALLTYPE *d3d8_get_viewport_t)(void *device, void *viewport);
typedef HRESULT (STDMETHODCALLTYPE *d3d8_set_render_state_t)(void *device, DWORD state,
                                                             DWORD value);
typedef HRESULT (STDMETHODCALLTYPE *d3d8_get_render_state_t)(void *device, DWORD state,
                                                             DWORD *value);
typedef HRESULT (STDMETHODCALLTYPE *d3d8_set_texture_t)(void *device, DWORD stage, void *texture);
typedef HRESULT (STDMETHODCALLTYPE *d3d8_get_texture_t)(void *device, DWORD stage,
                                                        void **texture);
typedef HRESULT (STDMETHODCALLTYPE *d3d8_set_stage_state_t)(void *device, DWORD stage, DWORD type,
                                                            DWORD value);
typedef HRESULT (STDMETHODCALLTYPE *d3d8_get_stage_state_t)(void *device, DWORD stage, DWORD type,
                                                            DWORD *value);
typedef HRESULT (STDMETHODCALLTYPE *d3d8_get_stream_source_t)(void *self, UINT stream,
                                                              void **buffer, UINT *stride);
typedef HRESULT (STDMETHODCALLTYPE *d3d8_set_stream_source_t)(void *self, UINT stream,
                                                              void *buffer, UINT stride);
typedef HRESULT (STDMETHODCALLTYPE *d3d8_get_indices_t)(void *self, void **buffer, UINT *base);
typedef HRESULT (STDMETHODCALLTYPE *d3d8_set_indices_t)(void *self, void *buffer, UINT base);

typedef HRESULT (STDMETHODCALLTYPE *d3d8_draw_primitive_up_t)(void *device, DWORD primitive_type,
                                                              UINT primitive_count,
                                                              const void *vertices,
                                                              UINT stride);
typedef HRESULT (STDMETHODCALLTYPE *d3d8_set_vertex_shader_t)(void *device, DWORD handle);
typedef HRESULT (STDMETHODCALLTYPE *d3d8_get_vertex_shader_t)(void *device, DWORD *handle);

typedef struct d3d8_viewport {
    DWORD x;
    DWORD y;
    DWORD width;
    DWORD height;
    float min_z;
    float max_z;
} d3d8_viewport_t;

/* ------------------------------------------------------------------------------- enumerators */

#define D3DRS_ZENABLE               7
#define D3DRS_SHADEMODE             9
#define D3DRS_ZWRITEENABLE          14
#define D3DRS_ALPHATESTENABLE       15
#define D3DRS_SRCBLEND              19
#define D3DRS_DESTBLEND             20
#define D3DRS_CULLMODE              22
#define D3DRS_ALPHABLENDENABLE      27
#define D3DRS_FOGENABLE             28
#define D3DRS_STENCILENABLE         52
#define D3DRS_CLIPPING              136
#define D3DRS_LIGHTING              137

#define D3DCULL_NONE                1
#define D3DSHADE_GOURAUD            2
#define D3DBLEND_SRCALPHA           5
#define D3DBLEND_INVSRCALPHA        6

#define D3DTSS_COLOROP              1
#define D3DTSS_COLORARG1            2
#define D3DTSS_ALPHAOP              4
#define D3DTSS_ALPHAARG1            5

#define D3DTOP_SELECTARG1           2
#define D3DTA_DIFFUSE               0

#define D3DPT_TRIANGLELIST          4

#define D3DFVF_XYZRHW               0x004u
#define D3DFVF_DIFFUSE              0x040u
#define OVERLAY_FVF                 (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)

/* One screen-space vertex: already projected, so nothing this plugin draws depends on any
 * transform the game happens to have set. */
typedef struct overlay_vertex {
    float x;
    float y;
    float z;
    float rhw;
    DWORD colour;
} overlay_vertex_t;

static __inline void **d3d8_vtable(void *device)
{
    return *(void ***)device;
}

#endif /* DEV_MENU_D3D8_MIN_H */

// Direct3D 8 capability structure and the cap bits render/device_caps.cpp reads.
//
// The portable VC6 toolchain has no DirectX SDK, so D3DCAPS8 is spelled out
// here. The layout is the real one from d3d8caps.h and the field offsets are
// load bearing: the disassembly addresses these members by displacement, so a
// wrong offset shows up immediately as a wrong displacement.

#ifndef RENDER_DEVICE_CAPS_H
#define RENDER_DEVICE_CAPS_H

// D3DDEVTYPE
enum D3DDEVTYPE
{
    D3DDEVTYPE_HAL = 1,
    D3DDEVTYPE_REF = 2,
    D3DDEVTYPE_SW  = 3
};

struct D3DCAPS8
{
    D3DDEVTYPE    DeviceType;                 // 0x00
    unsigned int  AdapterOrdinal;             // 0x04
    unsigned long Caps;                       // 0x08
    unsigned long Caps2;                      // 0x0c
    unsigned long Caps3;                      // 0x10
    unsigned long PresentationIntervals;      // 0x14
    unsigned long CursorCaps;                 // 0x18
    unsigned long DevCaps;                    // 0x1c
    unsigned long PrimitiveMiscCaps;          // 0x20
    unsigned long RasterCaps;                 // 0x24
    unsigned long ZCmpCaps;                   // 0x28
    unsigned long SrcBlendCaps;               // 0x2c
    unsigned long DestBlendCaps;              // 0x30
    unsigned long AlphaCmpCaps;               // 0x34
    unsigned long ShadeCaps;                  // 0x38
    unsigned long TextureCaps;                // 0x3c
    unsigned long TextureFilterCaps;          // 0x40
    unsigned long CubeTextureFilterCaps;      // 0x44
    unsigned long VolumeTextureFilterCaps;    // 0x48
    unsigned long TextureAddressCaps;         // 0x4c
    unsigned long VolumeTextureAddressCaps;   // 0x50
    unsigned long LineCaps;                   // 0x54
    unsigned long MaxTextureWidth;            // 0x58
    unsigned long MaxTextureHeight;           // 0x5c
    unsigned long MaxVolumeExtent;            // 0x60
    unsigned long MaxTextureRepeat;           // 0x64
    unsigned long MaxTextureAspectRatio;      // 0x68
    unsigned long MaxAnisotropy;              // 0x6c
    float         MaxVertexW;                 // 0x70
    float         GuardBandLeft;              // 0x74
    float         GuardBandTop;               // 0x78
    float         GuardBandRight;             // 0x7c
    float         GuardBandBottom;            // 0x80
    float         ExtentsAdjust;              // 0x84
    unsigned long StencilCaps;                // 0x88
    unsigned long FVFCaps;                    // 0x8c
    unsigned long TextureOpCaps;              // 0x90
    unsigned long MaxTextureBlendStages;      // 0x94
    unsigned long MaxSimultaneousTextures;    // 0x98
    unsigned long VertexProcessingCaps;       // 0x9c
    unsigned long MaxActiveLights;            // 0xa0
    unsigned long MaxUserClipPlanes;          // 0xa4
    unsigned long MaxVertexBlendMatrices;     // 0xa8
    unsigned long MaxVertexBlendMatrixIndex;  // 0xac
    float         MaxPointSize;               // 0xb0
    unsigned long MaxPrimitiveCount;          // 0xb4
    unsigned long MaxVertexIndex;             // 0xb8
    unsigned long MaxStreams;                 // 0xbc
    unsigned long MaxStreamStride;            // 0xc0
    unsigned long VertexShaderVersion;        // 0xc4
    unsigned long MaxVertexShaderConst;       // 0xc8
    unsigned long PixelShaderVersion;         // 0xcc
    float         MaxPixelShaderValue;        // 0xd0
};

// D3DDEVCAPS
const unsigned long D3DDEVCAPS_HWTRANSFORMANDLIGHT = 0x00010000;
const unsigned long D3DDEVCAPS_HWRASTERIZATION     = 0x00080000;

// D3DPTEXTURECAPS
const unsigned long D3DPTEXTURECAPS_PERSPECTIVE = 0x00000001;
const unsigned long D3DPTEXTURECAPS_POW2        = 0x00000002;
const unsigned long D3DPTEXTURECAPS_ALPHA       = 0x00000004;
const unsigned long D3DPTEXTURECAPS_SQUAREONLY  = 0x00000020;

// D3DPRASTERCAPS
const unsigned long D3DPRASTERCAPS_DITHER          = 0x00000001;
const unsigned long D3DPRASTERCAPS_ANTIALIASEDGES  = 0x00001000;
const unsigned long D3DPRASTERCAPS_ZBIAS           = 0x00004000;
const unsigned long D3DPRASTERCAPS_ZBUFFERLESSHSR  = 0x00008000;

// D3DPTADDRESSCAPS
const unsigned long D3DPTADDRESSCAPS_WRAP  = 0x00000001;
const unsigned long D3DPTADDRESSCAPS_CLAMP = 0x00000004;

// D3DCAPS2
const unsigned long D3DCAPS2_FULLSCREENGAMMA   = 0x00020000;
const unsigned long D3DCAPS2_CANCALIBRATEGAMMA = 0x00100000;

// D3DTEXOPCAPS
const unsigned long D3DTEXOPCAPS_BUMPENVMAP          = 0x00200000;
const unsigned long D3DTEXOPCAPS_BUMPENVMAPLUMINANCE = 0x00400000;

long DeviceCapsCheck(const D3DCAPS8 *caps, unsigned int *engineCaps);

#endif

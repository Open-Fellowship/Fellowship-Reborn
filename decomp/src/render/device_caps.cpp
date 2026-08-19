// 0x0048b820 (Fellowship.exe), 252 bytes - accept or reject a D3D8 device and
// translate its capabilities into the engine's own capability word.
//
// __cdecl free function (bare RET, both arguments read from [ESP+4]/[ESP+8]).
// A pure leaf: no calls, no data references, no relocations at all, so every
// one of the 252 bytes carries information.
//
// The first argument is a D3DCAPS8. Two independent facts establish that: the
// test *param_1 != 1 is DeviceType != D3DDEVTYPE_HAL, and the failure return
// 0x8876086b is D3DERR_INVALIDDEVICE, MAKE_D3DHRESULT(2155). The four gates at
// the top reject anything that is not a hardware rasteriser with perspective
// correction and texture alpha; everything after them is a straight bit
// translation into *engineCaps.
//
// The second argument is the ENGINE's capability bitfield, not a D3D one.
// Nothing establishes what its bits mean, so the constants below are named
// only for the D3D capability that sets each one and the numeric values are
// the whole of what is known about them.

#include "device_caps.h"

// MAKE_D3DHRESULT(2155).
const long D3DERR_INVALIDDEVICE = (long)0x8876086bL;
const long D3D_OK               = 0;

// Bits of the engine's capability word. Unnamed in the sense that matters:
// only the condition that sets each is established, which is what the name
// records. Nothing here is a Direct3D constant.
const unsigned int kEngineCapPow2TexturesOnly   = 0x00000001;  // D3DPTEXTURECAPS_POW2
const unsigned int kEngineCapSquareTexturesOnly = 0x00000002;  // D3DPTEXTURECAPS_SQUAREONLY
const unsigned int kEngineCapZBufferlessHsr     = 0x00000020;  // D3DPRASTERCAPS_ZBUFFERLESSHSR
const unsigned int kEngineCapAntialiasEdges     = 0x00000040;  // D3DPRASTERCAPS_ANTIALIASEDGES
const unsigned int kEngineCapDither             = 0x00000080;  // D3DPRASTERCAPS_DITHER
const unsigned int kEngineCapZBias              = 0x00000100;  // D3DPRASTERCAPS_ZBIAS
const unsigned int kEngineCapClamp              = 0x00000200;  // D3DPTADDRESSCAPS_CLAMP
const unsigned int kEngineCapWrap               = 0x00000400;  // D3DPTADDRESSCAPS_WRAP
const unsigned int kEngineCapBumpEnvMap         = 0x00000800;  // D3DTEXOPCAPS_BUMPENVMAP
const unsigned int kEngineCapBumpEnvMapLuminance= 0x00001000;  // D3DTEXOPCAPS_BUMPENVMAPLUMINANCE
const unsigned int kEngineCapHardwareTnL        = 0x00020000;  // D3DDEVCAPS_HWTRANSFORMANDLIGHT
const unsigned int kEngineCapFullScreenGamma    = 0x00040000;  // D3DCAPS2_FULLSCREENGAMMA
const unsigned int kEngineCapCalibrateGamma     = 0x00080000;  // D3DCAPS2_CANCALIBRATEGAMMA


long DeviceCapsCheck(const D3DCAPS8 *caps, unsigned int *engineCaps)
{
    if ((caps->DevCaps & D3DDEVCAPS_HWRASTERIZATION) == 0) {
        return D3DERR_INVALIDDEVICE;
    }
    if (caps->DeviceType != D3DDEVTYPE_HAL) {
        return D3DERR_INVALIDDEVICE;
    }
    if ((caps->TextureCaps & D3DPTEXTURECAPS_ALPHA) == 0) {
        return D3DERR_INVALIDDEVICE;
    }
    if ((caps->TextureCaps & D3DPTEXTURECAPS_PERSPECTIVE) == 0) {
        return D3DERR_INVALIDDEVICE;
    }

    *engineCaps = 0;

    // A plain store, not an OR: the disassembly writes the constant 2 here.
    if ((caps->TextureCaps & D3DPTEXTURECAPS_SQUAREONLY) != 0) {
        *engineCaps = kEngineCapSquareTexturesOnly;
    }
    if ((caps->TextureCaps & D3DPTEXTURECAPS_POW2) != 0) {
        *engineCaps |= kEngineCapPow2TexturesOnly;
    }
    if ((caps->TextureOpCaps & D3DTEXOPCAPS_BUMPENVMAP) != 0) {
        *engineCaps |= kEngineCapBumpEnvMap;
    }
    if ((caps->TextureOpCaps & D3DTEXOPCAPS_BUMPENVMAPLUMINANCE) != 0) {
        *engineCaps |= kEngineCapBumpEnvMapLuminance;
    }
    if ((caps->RasterCaps & D3DPRASTERCAPS_ZBUFFERLESSHSR) != 0) {
        *engineCaps |= kEngineCapZBufferlessHsr;
    }
    if ((caps->RasterCaps & D3DPRASTERCAPS_DITHER) != 0) {
        *engineCaps |= kEngineCapDither;
    }
    if ((caps->RasterCaps & D3DPRASTERCAPS_ZBIAS) != 0) {
        *engineCaps |= kEngineCapZBias;
    }
    if ((caps->RasterCaps & D3DPRASTERCAPS_ANTIALIASEDGES) != 0) {
        *engineCaps |= kEngineCapAntialiasEdges;
    }
    if ((caps->TextureAddressCaps & D3DPTADDRESSCAPS_CLAMP) != 0) {
        *engineCaps |= kEngineCapClamp;
    }
    if ((caps->TextureAddressCaps & D3DPTADDRESSCAPS_WRAP) != 0) {
        *engineCaps |= kEngineCapWrap;
    }
    if ((caps->Caps2 & D3DCAPS2_FULLSCREENGAMMA) != 0) {
        *engineCaps |= kEngineCapFullScreenGamma;
    }
    if ((caps->Caps2 & D3DCAPS2_CANCALIBRATEGAMMA) != 0) {
        *engineCaps |= kEngineCapCalibrateGamma;
    }
    if ((caps->DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) != 0) {
        *engineCaps |= kEngineCapHardwareTnL;
    }

    return D3D_OK;
}

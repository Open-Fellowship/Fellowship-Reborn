// 0x0043d2b0 (Fellowship.exe), 150 bytes - pick a D3DFORMAT for a surface.
//
// __cdecl free function (bare RET, both arguments read from [ESP+4]/[ESP+8]).
// A pure leaf: no calls, no data references, no relocations at all.
//
// It takes a requested bit depth and the engine's own surface-description
// flags word and returns a Direct3D 8 D3DFORMAT enumerator, or 0
// (D3DFMT_UNKNOWN) when the depth is not one it handles. This is the function
// the black_screen plugin patches, so the purpose is established independently
// of the disassembly.
//
// The flags word is the engine's, not D3D's. Only the bits tested here are
// established, and each is named for the format it selects. The 0xf0000 field
// behaves as a small enumerated selector rather than as independent bits: the
// code compares the whole field for equality, and the three values it
// recognises pick, in order, a 1-bit, 4-bit and 8-bit alpha channel. That
// reading is why the names below say "alpha", but it is inference from the
// chosen formats - the field's wider purpose, and the meaning of the values it
// does not test (0x20000, and everything above 0x80000), are unestablished.

// Direct3D 8 D3DFORMAT values, spelled out so this file needs no SDK header.
enum D3DFormat
{
    D3DFMT_UNKNOWN    = 0,
    D3DFMT_R8G8B8     = 0x14,
    D3DFMT_A8R8G8B8   = 0x15,
    D3DFMT_X8R8G8B8   = 0x16,
    D3DFMT_R5G6B5     = 0x17,
    D3DFMT_X1R5G5B5   = 0x18,
    D3DFMT_A1R5G5B5   = 0x19,
    D3DFMT_A4R4G4B4   = 0x1a,
    D3DFMT_A8R3G3B2   = 0x1d,
    D3DFMT_P8         = 0x29,
    D3DFMT_YUY2       = 0x32595559  // 'YUY2' FOURCC
};

// Bits of the engine's surface flags word that this function reads.
const unsigned int kSurfaceFlagYuy2      = 0x00004000;  // 32bpp -> YUY2 instead of RGB
const unsigned int kSurfaceAlphaMask     = 0x000f0000;  // selector field, tested whole
const unsigned int kSurfaceAlpha1        = 0x00010000;
const unsigned int kSurfaceAlpha4        = 0x00040000;
const unsigned int kSurfaceAlpha8        = 0x00080000;
const unsigned int kSurfaceFlagNoGreen6  = 0x00200000;  // 16bpp -> X1R5G5B5 instead of R5G6B5


int PixelFormatFromDepth(int bitsPerPixel, unsigned int surfaceFlags)
{
    int format = D3DFMT_UNKNOWN;

    if (bitsPerPixel == 8) {
        return D3DFMT_P8;
    }

    if (bitsPerPixel == 16) {
        unsigned int alpha = surfaceFlags & kSurfaceAlphaMask;
        if (alpha == kSurfaceAlpha1) {
            return D3DFMT_A1R5G5B5;
        }
        if (alpha == kSurfaceAlpha4) {
            return D3DFMT_A4R4G4B4;
        }
        if (alpha == kSurfaceAlpha8) {
            return D3DFMT_A8R3G3B2;
        }
        // The compiler if-converts this pair of returns into the branchless
        // AND / NEG / SBB / NEG / ADD 0x17 the original holds; writing the
        // addition out as `0x17 + (bit != 0)` instead gives SHR / AND 1.
        if ((surfaceFlags & kSurfaceFlagNoGreen6) != 0) {
            return D3DFMT_X1R5G5B5;
        }
        return D3DFMT_R5G6B5;
    }

    if (bitsPerPixel == 24) {
        return D3DFMT_R8G8B8;
    }

    if (bitsPerPixel == 32) {
        if ((surfaceFlags & kSurfaceFlagYuy2) != 0) {
            return D3DFMT_YUY2;
        }
        unsigned int alpha = surfaceFlags & kSurfaceAlphaMask;
        if (alpha == 0) {
            return D3DFMT_X8R8G8B8;
        }
        if (alpha == kSurfaceAlpha8) {
            format = D3DFMT_A8R8G8B8;
        }
    }

    return format;
}

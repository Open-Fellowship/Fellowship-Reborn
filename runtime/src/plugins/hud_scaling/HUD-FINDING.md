# The in-game HUD is authored in pixels

**Status: root cause found, not yet fixed.** This is why the health bar, the ring and the small
circle stay tiny at 4K while the menu sliders scale, and why the *spacing* between them is wrong
as well as their size.

## Not the same bug as the menu controls

`hud_scaling` fixes the menu by scaling a control's pixels-per-unit at `rfl+789A7`. That hook was
extended to the untemplated branch at `rfl+789BB` on the theory that the HUD went that way. It
was shipped and measured and it changed nothing:

| | 640x480 | 3840x2160 | ratio |
|---|---|---|---|
| health bar width | 104 | 598 | 5.75 |
| health bar height | 6 | 6 | **1.00** |
| circle width | 30 | 29 | **1.00** |
| circle height | 29 | 31 | **1.00** |

The circle is the same size in *pixels* at both resolutions. It never passes through a control's
pixels-per-unit at all.

## What it actually is

The engine's property tables carry display names, and the HUD's say what they are outright:

```
1010C624  MeterULPosX   float  18    "X Position (px)"
1010C638  MeterULPosY   float  12    "Y Position (px)"
1010C64C  MeterWidth    float  100   "Width"
1010C660  MeterHeight   float  50    "Height"
1010C2F0  LBXOffset     float  7     "Left Bar X Offset (px)"
1010C354  IYOffset      float  -6    "Indicator Y Offset (px)"
```

**(px).** Every piece of HUD geometry is an authored pixel count against the 640x480 the game was
laid out for, and nothing multiplies it by anything. There is no missing scale term to find,
because there was never a scale term.

That explains both halves of what you see. The elements are small because their *sizes* are fixed
pixels, and the gaps between them are wrong because the *offsets* are fixed pixels too; the ring
sits `LBXOffset` from the bar whatever the screen is.

## The map

The group descriptor array at `1010DCE8 .. 1010E0A8` holds `{name*, count, table*}` records, and
the property tables hold `{display*, key*, type, default, flags}` at 20 bytes each. Walking both
gives the complete list: **45 pixel-authored properties across 15 groups**, out of 400 properties
in the HUD classes.

| group | table | property | index | default | authored as |
|---|---|---|---|---|---|
| `Textured Bar` | `1010C098` | `ULPosX` | 1 | 0 | Textured Bar X Position (px) |
| `Textured Bar` | `1010C098` | `ULPosY` | 2 | 0 | Textured Bar Y Position (px) |
| `Left Frame` | `1010C124` | `LFPosX` | 0 | 2 | Left Frame X Position (px) |
| `Left Frame` | `1010C124` | `LFPosY` | 1 | 5 | Left Frame Y Position (px) |
| `Left Frame` | `1010C124` | `LFSizeX` | 2 | 22 | Left Frame X Size (px) |
| `Left Frame` | `1010C124` | `LFSizeY` | 3 | 18 | Left Frame Y Size (px) |
| `Texture : Center Frame` | `1010C174` | `CFPosX` | 0 | 25 | Center Frame X Position (px) |
| `Texture : Center Frame` | `1010C174` | `CFPosY` | 1 | 5 | Center Frame Y Position (px) |
| `Texture : Center Frame` | `1010C174` | `CFSizeX` | 2 | 19 | Center Frame X Size (px) |
| `Texture : Right Frame` | `1010C1B0` | `RFPosX` | 0 | 45 | Right Frame X Position (px) |
| `Texture : Right Frame` | `1010C1B0` | `RFPosY` | 1 | 5 | Right Frame Y Position (px) |
| `Texture : Right Frame` | `1010C1B0` | `RFSizeX` | 2 | 21 | Right Frame X Size (px) |
| `Texture : Left Glass` | `1010C1EC` | `LGPosX` | 0 | 8 | Left Glass X Position (px) |
| `Texture : Left Glass` | `1010C1EC` | `LGPosY` | 1 | 26 | Left Glass Y Position (px) |
| `Texture : Left Glass` | `1010C1EC` | `LGSizeX` | 2 | 16 | Left Glass X Size (px) |
| `Texture : Left Glass` | `1010C1EC` | `LGSizeY` | 3 | 7 | Left Glass Y Size (px) |
| `Texture : Center Glass` | `1010C23C` | `CGPosX` | 0 | 25 | Center Glass X Position (px) |
| `Texture : Center Glass` | `1010C23C` | `CGPosY` | 1 | 26 | Center Glass Y Position (px) |
| `Texture : Right Glass` | `1010C264` | `RGPosX` | 0 | 45 | Right Glass X Position (px) |
| `Texture : Right Glass` | `1010C264` | `RGPosY` | 1 | 26 | Right Glass Y Position (px) |
| `Texture : Right Glass` | `1010C264` | `RGSizeX` | 2 | 16 | Right Glass X Size (px) |
| `Texture : Indicator` | `1010C2A0` | `IPosX` | 0 | 67 | Indicator X Position (px) |
| `Texture : Indicator` | `1010C2A0` | `IPosY` | 1 | 1 | Indicator Y Position (px) |
| `Texture : Indicator` | `1010C2A0` | `ISizeX` | 2 | 7 | Indicator X Size (px) |
| `Texture : Indicator` | `1010C2A0` | `ISizeY` | 3 | 27 | Indicator Y Size (px) |
| `Offsets` | `1010C2F0` | `LBXOffset` | 0 | 7 | Left Bar X Offset (px) |
| `Offsets` | `1010C2F0` | `RBXOffset` | 1 | 6 | Right Bar X Offset (px) |
| `Offsets` | `1010C2F0` | `BYOffset` | 2 | 7 | Bar Y Offset (px) |
| `Offsets` | `1010C2F0` | `LGXOffset` | 3 | 5 | Left Glass X Offset (px) |
| `Offsets` | `1010C2F0` | `GYOffset` | 4 | 6 | Glass Y Offset (px) |
| `Offsets` | `1010C2F0` | `IYOffset` | 5 | -6 | Indicator Y Offset (px) |
| `HUD Texture` | `1010C368` | `XOffset` | 1 | 0 | X-Offset (texels) |
| `HUD Texture` | `1010C368` | `YOffset` | 2 | 0 | Y-Offset (texels) |
| `HUD Texture` | `1010C368` | `XSize` | 3 | 0 | Width (texels) |
| `HUD Texture` | `1010C368` | `YSize` | 4 | 0 | Height (texels) |
| `Frame Texture Info` | `1010C3E0` | `ULPosX` | 0 | 18 | Frame Texture X Position (px) |
| `Frame Texture Info` | `1010C3E0` | `ULPosY` | 1 | 12 | Frame Texture Y Position (px) |
| `Offsets` | `1010C444` | `LBXOffset` | 0 | 2 | Left Bar X Offset (px) |
| `Offsets` | `1010C444` | `LBYOffset` | 1 | 2 | Left Bar Y Offset (px) |
| `Frame Texture Info` | `1010C5D4` | `FrameULPosX` | 0 | 18 | X Position (px) |
| `Frame Texture Info` | `1010C5D4` | `FrameULPosY` | 1 | 12 | Y Position (px) |
| `Meter Texture Info` | `1010C624` | `MeterULPosX` | 0 | 18 | X Position (px) |
| `Meter Texture Info` | `1010C624` | `MeterULPosY` | 1 | 12 | Y Position (px) |
| `Halo` | `1010C6EC` | `HaloULPosX` | 0 | 18 | X Position (px) |
| `Halo` | `1010C6EC` | `HaloULPosY` | 1 | 12 | Y Position (px) |

The other 355 are colours, alphas, speeds, fonts, counts and texture references. Scaling those
would be wrong: `MeterInterpolateSpeed` is seconds, `MeterCriticalPerc` is a percentage,
`NumUnits` is a count.

## Why this is the good news

The game's own metadata says which properties are pixels. A fix does not have to hard-code a list
of addresses and hope: it can walk the descriptor array at install time, collect the indices whose
display name ends in `(px)` or `(texels)`, and scale exactly those when they are fetched. The
engine tells us what to scale.

## What has to be settled first

**Where the fetch happens.** The property read is `mov ecx,[obj+8] / push -1 / push <index> /
call [vtable+8]`, and the index is relative to the group. One hook on that call, with a set of
(group, index) pairs built from the tables, would cover all 45, but the group identity has to be
recoverable at the call, which is not yet established.

**Whether to scale by width, height or both.** A position in x and a size in x want the width
ratio; y wants height. The display names distinguish them (`X Position (px)` vs `Y Position
(px)`), so the metadata answers this too, but at 16:9 the two ratios differ by 1.33 and picking
one for everything would stretch the art.

**Whether the texture art survives it.** These are texel sizes into a HUD atlas. Scaling the
destination rectangle by 6 will magnify a 22x18 frame texture to 132x108, and it will look like a
22x18 texture magnified by 6. That is still better than a hairline, but it is worth seeing before
committing to it.

## ATTEMPTED AND DISPROVED: scaling at the getter, keyed on (caller, index)

`hud_probe` recorded 341 (caller, index) pairs during three seconds of play. Cross-referencing
the indices against the property table above named 74 that looked like pixel geometry, and
`hud_geometry` scaled exactly those as the getter handed them over.

**It hung the game on a black screen.** The trace named the last site it survived:

```
[hud_geometry]   site 24  rfl+659AB idx 12  0.000 -> 0.000
```

and disassembling that caller explains everything:

```
0659A4   push 0xc                  the index
0659A8   call dword ptr [eax+8]    the getter
0659AB   mov  esi,[eax]            read as an INTEGER
0659B5   jl   bail                 if (id < 0)
0659BA   jge  bail                 if (id >= manager->count)
0659CA   lea  eax,[esi+esi*8]      id * 36 -> &table[id]
```

Index 12 is `RFSizeX` **only for the class whose table this document lists**. At `rfl+659AB` it is
a template ID used as an array subscript. **Property indices are relative to the object's class**,
and mapping all 74 through one table meant roughly half were integers. An ID of `3`, read as a
float, is the denormal `4.2e-45`; scaled by 4.5 it comes back as integer `13`: a different
template, or a subscript off the end of a 36-byte-stride table.

Site 24 itself was harmless because zero scales to zero. The next one was not.

**That plugin has been deleted.** What is worth keeping is this page: the approach is one a
reasonable person arrives at from the evidence, and the reason it cannot work is not obvious
until you disassemble a caller. `hud_probe` is still in the tree, because the probe itself worked
and is how any future attempt starts.

## What would actually work

The class has to be identified **at the getter**, not inferred from the caller. Two things make
that tractable:

* the properties object is in `ecx` at the getter, and if it carries a pointer to its class
  descriptor then the group tables can be walked at install time and the pixel indices collected
  *per class*;
* the property record already carries a **type** field, 2 is float, 1 is int. Scaling anything
  whose declared type is not float is a bug by construction, and would have caught this before it
  ever ran.

## The probe that would settle the first point

Breakpoint the property fetch with the index on the stack, log `(index, return address)` while the
HUD builds, and match the return addresses against the groups. That is the same method that named
`rfl+7A2D5` for the inventory icons, the one that worked when three theories had already failed.

# The in-game HUD is authored in pixels

**Status: root cause found. One route is now closed for good, and a better one is open.**

This is why the health bar, the ring and the small circle stay tiny at 4K while the menu sliders
scale, and why the *spacing* between them is wrong as well as their size.

Read the whole page before acting on any part of it. An earlier version of this document proposed
a fix that **cannot work**, and the reason is in section 6.

## Not the same bug as the menu controls

`hud_scaling` fixes the menu by scaling a control's pixels-per-unit at `rfl+789A7`. That hook was
extended to the untemplated branch at `rfl+789BB` on the theory that the HUD went that way. It was
shipped and measured and it changed nothing:

| | 640x480 | 3840x2160 | ratio |
|---|---|---|---|
| health bar width | 104 | 598 | 5.75 |
| health bar height | 6 | 6 | **1.00** |
| circle width | 30 | 29 | **1.00** |
| circle height | 29 | 31 | **1.00** |

The circle is the same size in *pixels* at both resolutions. It never passes through a control's
pixels-per-unit at all.

## 1. What it actually is

The engine's property tables carry display names, and the HUD's say what they are outright:

```
1010C624  MeterULPosX   float  18    "X Position (px)"
1010C638  MeterULPosY   float  12    "Y Position (px)"
1010C2F0  LBXOffset     float  7     "Left Bar X Offset (px)"
1010C354  IYOffset      float  -6    "Indicator Y Offset (px)"
```

**(px).** Every piece of HUD geometry is an authored pixel count against the 640x480 the game was
laid out for, and nothing multiplies it by anything. There is no missing scale term to find,
because there was never a scale term.

That explains both halves of what you see. The elements are small because their *sizes* are fixed
pixels, and the gaps between them are wrong because the *offsets* are fixed pixels too; the ring
sits `LBXOffset` from the bar whatever the screen is.

## 2. The map, corrected

An earlier version of this page gave the group descriptor array as `1010DCE8 .. 1010E0A8` and
counted 45 pixel properties in 80 groups. **Those bounds were wrong by a factor of six.** The
numbers were right for the window they were taken from, and the window was 16% of the file.

| structure | address | stride | count |
|---|---|---|---|
| group descriptors `{name*, count, table*}` | `1010C980 .. 1010E0A8` | 12 | **494 groups** |
| property records `{display*, key*, type, default, extra}` | in `.data` | 20 | **2,378 properties** |
| class descriptors `{id, ?, ?, flags, name*, nprops, ngroups, groups*}` | `1010F0A0 .. 10112240` | 32 | **397 classes** |
| class group-pointer arrays | `1010E0A8 .. 1010F0A0` | 4 | 1,022 pointers |

`1010DCE8` is not the start of anything. It is the first entry of the group-pointer array belonging
to one class, `User Interface Properties`, which happens to own a contiguous run at the tail.

Checks that the parse is right, not merely plausible:

* all 1,022 class group pointers land on the 12-byte grid, none point outside it, and no group in
  the range is unreferenced;
* for **all 397** classes, `nprops` equals the sum of its groups' `count`, with no exceptions;
* the last property table, `1010C958` count 2, ends exactly where the descriptor array begins.

The array at `10112240` is **not** more classes. It is the message-name table (`"Off"`,
`"Triggered"`, `"Reset"`, `"Fire"`), registered by `rfl+4C250` and exported as
`GetMessageInterface`. The class array is registered by `rfl+4C210` as `GetObjectDefInterface`,
`0x18D` = 397 entries. Counting past `10112240` inflates the class list with message names.

## 3. There are four unit spellings, not two

| suffix | properties | groups |
|---|---|---|
| `(px)` | 43 | 16 |
| **`(tx)`** | **50** | **8** |
| `(texels)` | 6 | 2 |
| `(pixels)` | 3 | 3 |
| | **102 total** | 28 |

**`(tx)` is the largest family and an earlier version of this page missed it entirely.** It is also
the family that owns most of what testers actually report: `Ring Icon`, `Quest HUD`, `texture info`
(the objective boxes), `Load/Save GUI Icons`, `Scroll Buttons`, `GUI Border`, `GUI Texture`.

A fix that collects "display name ends in `(px)` or `(texels)`", as this page once proposed, would
find 49 of 102 and would miss the ring, the objective boxes and the save icons.

Twenty-five further properties are authored as **percentages** of screen width or height, in
`Size/Position`, `Positioning` and `Basic HUD`. Those already scale. `Size/Position` is inherited by
every GUI control class, which is the shape of the whole problem: **the container scales and the
art drawn inside it from a texel rectangle does not.**

`Map GUI` (`10109730`), which owns the map screen's indicator circle and its stars, carries **no
unit suffix at all**. Any fix driven purely by display names will silently skip it.

## 4. The type field, and why it matters more than anything else here

The property record's type word is 32 bits: the **low byte** is the base type and the bits above it
are modifiers. The engine itself masks it, at `Fellowship.exe 0x0044E81F`:

```
0044E818  mov ebx, dword ptr [ebx + 8]     ; the type word
0044E81F  and ebx, 0xff                    ; LOW BYTE is the type
0044E843  cmp ebx, 3                       ; 3 -> resolve a name to a template id
0044E84F  cmp ebx, 0xf                     ; 0xF -> colour, ORs 0xff000000
```

`1` is int and `2` is float, and there are thirteen further codes, **all integer-valued**: object
and template references, models, sounds, animations, textures, enums, strings, colours, message
ids. Observed modifier bits above the low byte include `0x1000`, `0x2000`, `0x3000`, `0x4000` and
`0x5000`, so `type == 2` is wrong and `(type & 0xFF) == 2` is right.

Of the 102 screen-space properties, **101 are declared float**. The one exception is
`Screen Adjustment GUI` `Adjustment Granularity (px)`, base type 1, which is a granularity rather
than a coordinate and is correctly excluded by the same test.

## 5. ATTEMPTED AND DISPROVED: scaling at the getter, keyed on (caller, index)

`hud_probe` recorded 341 (caller, index) pairs during three seconds of play. Cross-referencing the
indices against one class's table named 74 that looked like pixel geometry, and `hud_geometry`
scaled exactly those as the getter handed them over.

**It hung the game on a black screen.** The trace named the last site it survived:

```
[hud_geometry]   site 24  rfl+659AB idx 12  0.000 -> 0.000
```

Index 12 is `RFSizeX` only for the class this page originally listed. At `rfl+659AB` the class is
`HUD Meter`, where class-relative index 12 is:

```
101087B8   'Border Texture'   key 'BorderTexture'
           typeword 0x00002003  ->  base type 3, an object reference
           default 0x10132604   extra 0x00010134 (the class id of 'GUI Border')
```

A template id, stored as an integer, read as a float, scaled, and then used as an array subscript:

```
0659A4   push 0xc                  the index
0659A8   call dword ptr [eax+8]    the getter
0659AB   mov  esi,[eax]            read as an INTEGER
0659CA   lea  eax,[esi+esi*8]      id * 36 -> &table[id]
```

An id of `3` read as a float is the denormal `4.2e-45`; scaled by 4.5 it comes back as integer `13`.
A different template, or a subscript off the end of a 36-byte-stride table.

**A `(type & 0xFF) == 2` gate rejects `Border Texture` before anything is scaled, and would have
prevented this.** Whatever is built next, that test is a hard precondition on every value touched.

`hud_geometry` has been deleted. `hud_probe` remains, because the probe worked.

## 6. DISPROVED: the class cannot be identified at the getter

The previous version of this page closed by saying the class had to be identified at the getter
and not inferred from the caller, and suggested the properties object might carry a pointer to
its class descriptor. **It does not.** This is settled, not open.

First, the getter is **not in the rfl**. `Fellowship.rfl` imports only `KERNEL32` and exports
`GetBaseRFLInterface`, `GetObjectDefInterface` and friends; the property system is engine-side. The
getter is `Fellowship.exe 0x0044E6E0`, `__thiscall`, and `0x0044E6E0` appears exactly once in the
whole image, at `0x0051DA98`, slot 2 of the vtable at `0x0051DA90`. No derived class duplicates it.

```
0044E6E0  push esi
0044E6E1  push edi
0044E6E2  mov edi, [esp+0x10]      ; sub-index, -1 for a scalar
0044E6E6  mov esi, ecx             ; this
0044E6E8  cmp edi, -1
0044E6EB  je  0x44e70f
...
0044E70F  mov eax, [esp+0xc]       ; index
0044E713  mov edx, [esi+8]         ; the value array
0044E718  lea eax, [edx+eax*4]     ; &values[index]
0044E71B  ret 8
```

It touches `esi`, `[esi]` and `[esi+8]`. It never reads `[esi+4]`.

The object is **12 bytes**:

| offset | contents |
|---|---|
| `+0x00` | vtable, `0x0051DA90` |
| `+0x04` | element count |
| `+0x08` | `dword*` value array |

Proved three ways. The constructor at `0x0044ED60` writes exactly those three fields and returns.
The pool allocator at `0x0044F87D` strides `add ebx, 0xc`. And the initialiser `0x0044E720`
*receives* the class descriptor as an argument, reads `nprops`, `ngroups` and the group array out
of it, writes the defaults through the object's own vtable, and **never stores it**.

There is no class id, no descriptor pointer, no name and no owner back-pointer. The class is
visible for the duration of one call and then discarded.

The obvious fallback fails too. `[ecx+4]`, the property count, is a partial discriminator available
free at the getter. Of the 86 float pixel properties it is unambiguous for 60 and **collides for
26**, and the collisions land exactly where this project cares:

```
nprop=44 idx=37   HUD Meter 'Frame Texture X Position (px)'  vs  Particle Fountain 'Blending Mode'
nprop=44 idx=22   HUD Radial Meter 'X Position (px)'         vs  Particle Fountain 'Force Simple'
nprop=26 idx=24   HUD Texture 'Width (texels)'               vs  Standard Material 'Detail Texture'
```

Same failure as `(caller, index)`, only rarer, which makes it worse and not better.

## 7. The property route that would work, if it is ever needed

Learn the mapping at initialisation instead of guessing it at read time.

`Fellowship.exe 0x0044E720` is `__thiscall Init(objdef, classdesc)`. It receives the object in `ecx`
and the class descriptor as its second argument, read at `0044E754`, `0044E763` and `0044E772`.
Hooking it gives an `object -> class` map with no inference at all. From the class, and because
`nprops` provably equals the sum of the group counts for all 397 classes, a class-relative index is
the cumulative offset of the group plus the index within it. Precompute per object a bitmap of the
indices whose display name carries a screen-space unit **and** whose `(type & 0xFF) == 2`, and the
getter hook becomes a bitmap test.

Two things to settle before building it:

* every properties object the HUD reads must actually pass through `0x0044E720`. Objects
  deserialised from a level or a save may take another path, and that has not been checked;
* the sparse-override subclass has its own getter at `0x0044EFD0` and its own init at `0x0044F070`.
  Both would need hooking. `hud_probe` currently covers only `0x0044E6E0`.

## 8. The better first move: GUIControl_Texture assigns texels into pixels

This is a different path, a different class, and it is far better conditioned than anything above.

`GUIControl_Texture` (vtable `0x100F0668`, constructor `0x1006C5D0`) keeps a **source rectangle in
texels** at `+0x70`/`+0x74` and an **on-screen size in pixels** at `+0x40`/`+0x44`. In the setup
function `FUN_1006C750`:

```
1006C84F  8B 4F 70   mov ecx, [edi+0x70]     SrcSizeX, in TEXELS
1006C852  6A 00      push 0                  belongs to the call at 1006C869
1006C854  89 4F 40   mov [edi+0x40], ecx     screen pixel WIDTH := texel width
1006C857  8B 57 74   mov edx, [edi+0x74]
1006C85A  89 57 44   mov [edi+0x44], edx     screen pixel HEIGHT := texel height
```

A texel count assigned straight into a screen-pixel field, with no scale term.

**Note the `push 0` at `1006C852`.** It sits between the load and its store and belongs to the call
at `1006C869`. Anything that relocates this region must carry it across in order; treating the site
as "two mov pairs" and lifting ten bytes splits an instruction and unbalances the stack.

The damning part: `FUN_10064DA0`, vtable slot 20, runs **immediately before** this from the same
constructor at `rfl+6C60C`, and does the correct thing:

```
rfl+64E05  push -1 ; push 3     YPos  'Y Position (% of screen height)'
rfl+64E23  push -1 ; push 1     XPos  'X Position (% of screen width)'
rfl+64E47  push -1 ; push 5     YSize 'Y Size (% of screen height)'
rfl+64E9A  push -1 ; push 4     XSize 'X Size (% of screen width)'
           * [DAT_10132698+0x234] / [+0x238]     the live viewport
           * 0.01
rfl+64EF7  call [vtable+0x4c]   set the rect
```

**The engine computes a correct percentage-of-screen rectangle and then throws it away.**

The shipped data confirms the authored intent. In `Common/Interface/Interface.odu` at `0x75CA`, the
object `Default Pointer Texture` is class `GUI Texture Control` with `XSize`/`YSize` = **20.0 / 20.0
percent** and `SrcSizeX`/`SrcSizeY` = **32 / 32**. The texture is `Mouse Pointer`, 32x32 8bpp. So
the cursor is 32x32 device pixels at every resolution, 5% of the width at 640x480 and 0.83% at
3840x2160, and its authored 20% is dead data.

The draw, `FUN_1006C890`, then reads the **source** size again for the destination rectangle rather
than using `+0x40`/`+0x44`:

```
1006C909  D9 46 3C   fld  [esi+0x3c]      y
1006C90C  D8 46 74   fadd [esi+0x74]      + SrcSizeY   -> y1
1006C90F  D9 46 38   fld  [esi+0x38]      x
1006C912  D8 46 70   fadd [esi+0x70]      + SrcSizeX   -> x1
```

So a fix must reach **both** sites. Correcting only the setup fixes the clip rectangle and leaves
the quad exactly as small as it was.

Both draw instructions are `fadd` with an 8-bit displacement, so pointing them at the destination
fields is a **single byte** each: `74` becomes `44`, `70` becomes `40`. One-byte writes are atomic,
which is the whole reason this path is safer than the property route.

`hud_scaling` cannot reach any of this, for two independent reasons. The pointer control is
`operator new(0x80)`, 128 bytes, and `hud_scaling` stores to `[esi+0x9C]`, offset 156, past the end
of the object. And the texture path never reads `+0x98` or `+0x9C` at all.

`FUN_1006C750` and `FUN_1006C890` serve **every** `GUIControl_Texture`. That is the opportunity and
the risk together: one hook may correct much of the fixed-size interface art, and it may also
change things nobody asked it to. It has to be measured against a 640x480 baseline before it is
believed, exactly as the untemplated branch was.

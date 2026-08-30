# texture_scaling

**Produces:** `texture_scaling.dll`. Patches `Fellowship.rfl`. **On by default.**

Interface art is drawn at the size of its own texture, in texels, so at 3840x2160 the mouse
pointer is 32 device pixels across and the One Ring icon is 64. Four classes have the same
disease, and this fixes all four.

| element | class | site |
|---|---|---|
| mouse pointer | `GUIControl_Texture` | `rfl+67083` |
| the circle under the health bar | `HUD Texture` | `rfl+7B2A3` |
| the One Ring icon | `Ring Icon` | `rfl+7ACA1` |
| the bar frames | `HUD Variable Meter` | seven pushes, `rfl+79356` to `rfl+797E4` |
| the bar fills | `HUD Variable Meter` | `rfl+78DE7`, `rfl+78E2B`, `rfl+667A3` |
| the objective tick boxes | `GUIControl_Texture` | `rfl+3F5D3`, `rfl+6C85D` |
| the map indicator and stars | `Map GUI` | `rfl+2D636`, `rfl+2D6D1` |
| the save and load pictures | `LoadSave GUI` | `rfl+73916`, `rfl+73CC9`, `rfl+6C890` |

The groups are independent. Any one can fail to match without taking the others down, and the log
says which. The bar fill is all or nothing within itself: if either call site or the draw does not
validate, the fill is left exactly as the game drew it rather than scaled on one of its two
draws, which would flicker.

## The shape of the bug, three times over

Every one of these classes sets its on-screen size equal to its texture's texel dimensions, and
its draw then computes a scale as destination over source. The two are equal, so the scale is
1.0 at every resolution and the art is one texel to one pixel for ever.

The elements are small because their *sizes* are fixed, and the spacing between them is wrong
because their *offsets* are fixed too. Only the size is addressed here.

## The pointer needs no arithmetic at all

`Texture::Render` lives in `Fellowship.exe` at `0x0043F1E0`, reached through slot `+0x5C` of the
texture vtable at `0x0051D3A0`. Its last two arguments are a destination scale pair, and the
drawn extent is the source multiplied by them:

```
0043F391  fmul [esp+0xf8]     destination width  = source width  * scaleX
0043F3AA  fmul [esp+0xfc]     destination height = source height * scaleY
```

The pair is `control+0x78` and `control+0x7C`. The constructor writes `1.0` to both, and the only
code in the game that ever changes them is the save slot thumbnail path, which sets
`SetScale(ratio, 1.0)`. Every other control is 1:1 by construction.

**So the pointer is not fixed by patching the draw.** The six bytes at `rfl+67083` exist only to
learn which object the GUI manager built as its pointer; the fix is two float writes into fields
the engine already reads every frame. Filtering comes free, because `Texture::Render` chooses
between point and linear sampling on whether that pair is exactly `1.0`.

That was measured, not deduced. A probe caught a save slot passing `arg9 = 1.778`, and
`64 * 1.7778 = 113.78`, exactly the destination width the engine had clamped away. The scale is
the ratio the clamp discards, handed to the callee to put back.

## The other two multiply, and must never delete

Both write the texel dimensions straight into the on-screen size:

```
1007B2A1  fxch st(1)             HUD Texture: st0 = width, st1 = height
1007B2A3  fstp [edi+0x40]
1007B2A6  fstp [edi+0x44]

1007ACA1  mov [edi+0x44], ecx    Ring Icon: height
1007ACA4  mov ecx, edi           the this for the call at 1007ACA9
1007ACA6  mov [edi+0x40], eax    width
```

**Multiply, never remove.** The authored percentage size for these objects is effectively zero,
which is why the copy exists at all. Deleting it makes the element vanish instead of scaling.

`1007ACA4` is not decoration. It is the `this` for the call immediately below, so the ring's
eight bytes are relocated in order, and the stub re-emits it **after** its `popad`, which would
otherwise put the old `ecx` back.

Unlike the pointer, these two draws re-read their source from the property table every frame and
take only the destination from `+0x40`/`+0x44`, so correcting the setup is sufficient.

## Built before the camera exists

The HUD is constructed before a camera has validated, so the stubs multiply by a live pair that
starts at `1.0` and a HUD built too early is simply left alone. Each stub records its control and
the texel dimensions it was built from, and a 250ms poll re-derives `base * scale` afterwards.

Re-derived, never accumulated: a second pass over the same control recomputes from the same base
and cannot compound. The pointer is held the same way, because the manager rebuilds it and the
constructor puts `1.0` back each time.

The camera comes from the slot `common/camera.c` publishes, never the engine's own pointer. See
the 180 degree crash in `common/README.md`.

## Configuration: `[texture_scaling]`

| Key | Default | |
|---|---|---|
| `Enabled` | `1` | |
| `ReferenceWidth` | `640` | the resolution the art was authored against |
| `ReferenceHeight` | `480` | |

## What has been seen, and what has not

Verified on one machine at **3840x2160**: all three elements scale, the log reports the pointer at
`6.0000 x 4.5000` and the two HUD controls built from `32 x 32` and `64 x 64` texels, and the
before and after screenshots differ exactly as expected.

**Not verified:** any other resolution, and 640x480 in particular, where every ratio is 1.0 and
the result must be identical to stock. Nothing here is pixel-measured the way `hud_scaling` was.

The art is also a ceiling. The pointer is a 32x32 8-bit bitmap; magnified six times it reads as a
magnified 32x32 bitmap. A replacement texture is the only route to genuinely crisp art at 4K, and
its source rectangle would need updating to match.

## The bars are laid out correctly, and framed wrongly

The bars are not a size bug at all, which is why they took so long. Every bar control carries its
own box, and the numbers came off the live controls rather than a decompile:

```
+38  x 115.20     +3C  y 108.00
+40  w 613.00     +44  h  18.00
+B4  600.00       the full track width
```

The fill is drawn at `(122.20, 115.00, 27, 6)`: inset 7 from the box's left and top, 6 high inside
a box 18 high. That is centred, and it stays centred at any resolution. The layout was never
wrong.

What is wrong is that the frame is *rendered* four and a half times taller than the 18 its own box
says it is, by the seven scale pushes above. A correctly placed fill inside an oversized frame
reads as a fill pinned to the top edge. So the fill's height and its offset from the top of its
own box are scaled by the same ratio the frame is:

```
y = box_y + ratio * (y - box_y)
```

at `rfl+667A3`, where the height is loaded for the draw. The control is still in `edi` there, one
instruction ahead of the `mov` that overwrites it, so the box is in reach and the stub touches no
register at all. Both the load and the store go through `esp` while it is still the engine's.

This is the whole bar family, so the loading bar is carried along with the two in the corners.

`FUN_10066600` draws filled rectangles for eight callers, including menu backgrounds, so the two
calls that draw a bar raise a flag around themselves and the scaling reads it. Everything else
that function draws is untouched.

## The objective tick box needs two writes, not one

The box beside each objective line is a `GUIControl_Texture`, the same class as the pointer, so
its art scales through the same pair at `+0x78` and `+0x7C` and its `(tx)` source rectangle is
never touched. It is built one per objective line:

```
1003f592  push 0x80              a 0x80 byte control
1003f5d3  call 1006C5D0          the GUIControl_Texture constructor
1003f5d8  mov [esp+0x14],eax     the finished control
```

Wrapping that call keeps the control. Scaling the art alone, though, leaves the objective's text
starting where a 23 texel box would have ended, on top of it, because the drawn extent and the
layout box are different fields. The row reserves space from `+0x40` and `+0x44`.

Those cannot be written when the control is built. Measured there they hold `3840 x 2160`, the
screen. `FUN_1006C750` copies the texel size in afterwards, and the hook goes on the twelve plain
bytes after that copy, at `rfl+6C85D`, because the copy itself has a `push 0` interleaved that
belongs to a later call. The row lays out after that returns, which is what makes it early enough:
writing the same value from the 250ms poll changed nothing, because by then the text already had
its position.

That function serves every `GUIControl_Texture`, the pointer included, so it acts only on a
control this plugin recorded being built for an objective line.

Measured at 3840x2160: `23.00 x 23.00 texels, laid out at 138.00 x 103.50`.

## The map icons scale about their centres

`Map GUI` is 30 properties with its geometry at class indices 19 to 26: the indicator at 121 by
118 texels, a star at 19 by 19. The map's own corner textures already fill the screen, so only the
icons drawn on top of it look wrong.

Both draws hand nine arguments to slot `+0x58`, read live off the stack rather than worked out
from the disassembly:

```
arg1 637.000   arg2 392.406      where it goes, in screen pixels
arg3 135.000   arg4   3.000      where it comes from in the atlas
arg5 121.000   arg6 118.000      how big it is, in texels
arg8   1.000   arg9  -1.000      the X scale, and Y taking its value from X
```

The extent is the source multiplied by the scale, grown from `arg1` and `arg2` as the **top left
corner**. So scaling alone moves an icon's centre by half its growth. The circle and the star
differ hugely in size, 121 against 19, so at 4.5 their centres moved 212 and 33 pixels
respectively and they ended up about 177 pixels apart while both were "correctly" scaled.

That was not spotted when the icons were first scaled, because both were the right size and only
the size was checked. It was confirmed by holding the patch off: with it off, the circle sits on
the star.

So the scale and the position are set together, at the call:

```
x -= w * (k - 1) / 2
y -= h * (k - 1) / 2
```

which grows each icon about its own centre, from its own size, at any resolution.

```
1002d66b  mov ecx,ebp / push edx / call [eax+0x58]     the star, six bytes
1002d6ef  mov ecx,ebp / call [edx+0x58]                the indicator, five
```

The star pushes its last argument after the `mov`, so in both cases the adjustment happens once
every argument is on the stack. The stub declines anything whose `arg8` is not `1.0` and `arg9`
not the `-1.0` sentinel, so other draws through the same slot are left alone.

Found by measurement in both directions: a byte scan for the property reads gave fifteen candidate
windows with `1002D4E4` to `1002D6AF` the strongest, and `hud_probe` recorded `rfl+2D6BE`,
`2D69A`, `2D528` and `2D517` reading indices 19, 21, 25 and 26 on a 30 property object at 295 hits
each, once a frame while the map is open.

## The save pictures, and the aspect applied twice

Measured with the patch held off, the game draws a save slot picture like this:

```
source 113.78 x 64.00   scale 1.7778 x 1.0000   drawn 202.3 x 64.0
```

The saved thumbnail is 64 texels square. The menu works out the viewport aspect, applies it to the
**source** rectangle, `64 * 1.7778` giving `113.78`, and then hands the same ratio to `SetScale` as
well. So the ratio lands twice, and the widened source samples fifty texels past the edge of a 64
texel texture. This is the one place in the game that writes that scale pair itself rather than
leaving the constructor's `1.0`, which is why it is also the only place this shows.

The widening cannot simply be removed. Tried, at both of its sites, and the pictures vanished
altogether: that value feeds more than the source rectangle. So the source is left as the game
builds it and the ratio goes back onto the scale where it belongs, `(ratio * k, k)`, with `k` the
height ratio. The picture is then drawn at `512 x 288` here, and the quad reaching further than
that is empty because there is no texture past 64 texels.

The layout box is worked out from the **height** on both axes, because the art is square and the
width has the emptiness in it. Sizing the row from the widened source reserved room for nothing.
`288` of `2160` is 13.3 per cent, which is what the stock game's own rows measure.

### Clipping the picture to its list

The row is now 288 tall rather than 108, so rows no longer divide evenly into the list and one is
usually partial. The list clips its text to itself but not this picture, so it is clipped here,
against the list read live through the control's parent chain:

```
room = list bottom - picture top
```

with the source cropped by the same fraction, so the picture is cut off rather than squashed.
Every number is read at the time of the draw, so it holds at any resolution and any row count.

**Where the hook goes matters more than the arithmetic.** It is at `rfl+6C909`, the instruction
that reads the position to build the rectangle:

```
1006c909  fld  [esi+0x3c]        the position
1006c90c  fadd [esi+0x74]        plus the source height
```

not at the function's entry. The entry is one call too early: `1006c8c2` resolves the position,
so a clamp before it works from a value up to a frame old. That was measured rather than guessed.
A picture clamped for `y 1367.2` produced a source of `17.79`, and `17.79 * 4.5` is `80`, exactly
`1447.2 - 1367.2`, so the arithmetic was right; by the time the rectangle was built `y` was
`1385.6` and it reached eighteen pixels past the list.

Five explanations were wrong before that one and are recorded so they are not tried again: the
table evicting live controls (real, fixed, not this), a cached offset between picture and row
(real, removed, not this), the row's `y` as the reference (the offset is not constant), the list
rectangle being scrolling content rather than the visible box (it is the visible box, `259.2` to
`1447.2`), and the position never being final at the draw entry (it is final one call in).

## A scaled control cannot be clipped by the engine

This is the reason the save pictures fought back for so long, and it is worth stating on its own
because it applies to anything else here that gets a scale.

`FUN_1006C890` builds the control rectangle out of the position and the SOURCE size:

```
1006c909  fld  [esi+0x3c] ; fadd [esi+0x74]     bottom = y + source height
1006c90f  fld  [esi+0x38] ; fadd [esi+0x70]     right  = x + source width
```

and then intersects it with the rectangle its parent hands back:

```
1006c94d  call [edx+0x44]                       the parent clip rectangle, four floats
1006c96d  fld  [ecx+0x4]                        top, against the control own top
```

having narrowed it, it moves the source origin by whatever the edges lost:

```
1006c9e2  fadd [esi+0x6c]                       source v, plus what the top gave up
1006c9e9  fadd [esi+0x68]                       and the same for u
```

That is a correct crop, origin and all, and it needs no help from anyone. It is also only correct
when the scale is 1, because the rectangle it builds is in texels and the rectangle it intersects
with is in screen pixels, and stock those are the same number.

A save picture is 64 texels drawn at 288 pixels. So while the top row slid into place, `y + 64`
was still above the list top and `bottom - top` came out NEGATIVE; over the last 64 pixels of the
slide it turned positive with the source advancing four and a half times too fast. On screen the
picture appeared out of nothing and expanded into place. The same mismatch is why the bottom edge
never clipped either, and why it needed a clamp written by hand.

There is no way to fix this by correcting the clip rectangle. The clipped top is used twice, once
as the source origin and once as the screen position, and one value cannot be right for both when
the two spaces differ. So the rectangle is opened out at `rfl+6C950` until it cannot cut anything,
and the crop is worked out in screen pixels, converted to texels once, and written to `+0x3c`,
`+0x6c` and `+0x74`.

Moving `+0x3c` had been ruled out twice before on the grounds that it was the scroll animation and
writing it would end the scroll. It is not. The animation is on the LIST, at `+0xB4`, which holds
the scroll origin and decays under friction by a factor of `0.917` a frame:

```
259.200 -> 169.177 -> 86.707 -> 11.061 -> -58.278     deltas 90.0, 82.5, 75.6, 69.3
```

and clamps at the list top rather than rubber banding past it. The row position is derived from
that each frame. Even so the field is put back: `take_over_clip` restores it later in the same
draw, after the rectangle has been taken from it at `1006c915` and before the source origin is
computed from it at `1006c9c1`, so it differs from the engine value for a few instructions and
nothing that runs later can see the difference. Because the restore happens before the source
origin is worked out, the engine adds the cut in screen pixels; `+0x6c` is therefore set to the
texel figure MINUS the pixel one, and the sum lands correctly.

The layout box at `+0x40` and `+0x44` keeps the full height throughout. A crop is something that
happens to one frame of drawing; the space a row reserves is not a function of how much of it
happens to be on screen this frame.

## What this does NOT reach

`FUN_1006C890` is exclusive to `GUIControl_Texture`, proven by a byte scan of the whole image
finding exactly one reference to it. The map screen icons and the save slot thumbnails are `(px)`
and `(tx)` property geometry on a different path again.

`HUD-FINDING.md` has the full map of which element belongs to which family.

## Attempts that did not work, and why

Kept because each is a reasonable idea and the reason it fails is not obvious.

1. **Scaling the destination corners early**, at `rfl+6C909`. The source span is derived from those
   corners further down, so growing them grows the sampled region and the texture smears.
2. **Lifting the two size clamps** at `rfl+6CA1D` and `rfl+6CA49`. Same fault, more directly: they
   bound the source, not the destination.
3. **Scaling the corners late**, at `rfl+6CA5B`. By then the width has already been consumed and
   the value is dead.
4. **Scaling in the setup only**, at `rfl+6C84F`. It runs once, before the camera validates, so it
   multiplied by 1.0 and nothing changed.
5. **Treating `rfl+6C84F` as two `mov` pairs.** There is a `push 0` between them belonging to a
   later call; lifting ten bytes splits an instruction and unbalances the stack.

6. **Scaling `[edi+0x44]` in the bar setup**, at `rfl+790E8`. The hook installed and the log proved
   the arithmetic: 18.0 in, 81.0 written, nothing on screen. That field is not what the bar draws
   from.
7. **Property 29**, at `rfl+78D36`, taken for a thickness. The disassembly stores it to `[esp+0x28]`
   and *compares* it at `10078D71` to choose between properties `0x16` and `0x15`. It is the low
   health colour threshold. Patching it would have silently moved the colour change.
8. **Centring against the parent box**, using the rect `FUN_10066600` fetches at `rfl+666E7`. It
   never ran once: these controls have no parent, so that branch is skipped entirely.
9. **The scale pair inside the fill draw.** A positive ratio in the Y slot gives the right
   thickness in the wrong place; the same ratio negated gives the right place at the old
   thickness. A flip would have given a thick bar either way, so that slot is a sentinel and every
   negative means what -1.0 means, which is to take Y from X. Scaling X works and drags Y with it,
   so the pair can never scale one axis alone.

11. **The `GUI Border` strips**, four `push` sites in `FUN_10066860`, the box outline. They patch
    cleanly and change nothing, because that function never runs: a hook on its entry reported
    zero boxes across a whole session. Correctly patched code on a dead path.
12. **`Quest GUI` for the tick box.** Its `texture info` group carries properties actually named
    `Unchecked-Box X Size (tx)` and `Checked-Box X Size (tx)`, and `HUD-FINDING.md` calls that
    group the objective boxes. `hud_probe` showed the class is initialised with defaults and then
    never read by anything. The live class is `Quest HUD`, 31 properties, and its box is 23 by 23
    texels rather than the 19 by 16 those defaults describe.
13. **A site picked from a byte scan**, `rfl+68BF6`, where indices 15 to 22 appear in ascending
    order exactly as the tick box would read them. It belongs to a different class. An index means
    nothing without the class that owns it, which is what `hud_probe`'s own README says, and the
    tool now records the property count so that a caller can be tied to a class.

And one that was worse than not working, because it looked like it worked:

10. **`rfl+79200` counted as an eighth frame push.** It matches `push 0xbf800000` like the other
    seven, but `FUN_100791C0` is not a draw. It forwards to `FUN_10078CA0`, and that constant is
    argument four of the fill, not a render scale. Feeding the height ratio into it drove both
    bars to a value width of exactly 27 against a track of 600, which reads as a bar stuck near
    empty. The frames looked right, so it passed as correct for as long as nobody put a number on
    the fill. Any experiment run on the fill while that site was live is untrustworthy, which
    includes an earlier attempt at `rfl+78DD7` that should have worked and did not.

14. **Raising the clip rectangle top to the list.** The idea was right and the site was right, and
    it did nothing at all: a probe on 32 consecutive samples showed the rectangle already arriving
    as `max(row top, list top)`, so the raise never once fired. The parent had been doing that part
    correctly the whole time. What it could not do was use the result, for the reason in the
    section above.
15. **Reading a picture position and concluding it was drawn there.** The first probe showed the
    picture at `y 169.2` against a list top of `259.2` and that was taken as proof it drew outside
    the box. It did not; it was clipped. A field read from an object says what the field holds, not
    what reached the screen.

Every one of them came from reading a decompile, or from matching a byte pattern without checking
what the value was for. Each fix came from measurement: a probe for the pointer, and the controls'
own fields for the bars. `texture_probe` is still in the tree for that reason.

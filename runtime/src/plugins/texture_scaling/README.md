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

## What this does NOT reach

`FUN_1006C890` is exclusive to `GUIControl_Texture`, proven by a byte scan of the whole image
finding exactly one reference to it. The map screen icons and the save slot thumbnails are `(px)`
and `(tx)` property geometry on a different path again.

`HUD-FINDING.md` has the full map of which element belongs to which family.

## Five attempts that did not work, and why

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

Every one of them came from reading a decompile, or from matching a byte pattern without checking
what the value was for. Each fix came from measurement: a probe for the pointer, and the controls'
own fields for the bars. `texture_probe` is still in the tree for that reason.

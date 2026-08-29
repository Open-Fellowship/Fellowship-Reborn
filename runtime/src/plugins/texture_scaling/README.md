# texture_scaling

**Produces:** `texture_scaling.dll`. Patches `Fellowship.rfl`. **On by default.**

Interface art is drawn at the size of its own texture, in texels, so at 3840x2160 the mouse
pointer is 32 device pixels across and the One Ring icon is 64. Three separate classes have the
same disease, and this fixes all three.

| element | class | site |
|---|---|---|
| mouse pointer | `GUIControl_Texture` | `rfl+67083` |
| the circle under the health bar | `HUD Texture` | `rfl+7B2A3` |
| the One Ring icon | `Ring Icon` | `rfl+7ACA1` |

The three hooks are independent. Any one of them can fail to match without taking the others
down, and the log says which.

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

## What this does NOT reach

`FUN_1006C890` is exclusive to `GUIControl_Texture`, proven by a byte scan of the whole image
finding exactly one reference to it. The objective boxes, the map screen icons and the save slot
thumbnails are `(px)` and `(tx)` property geometry on a different path again, and the health and
purple bars are a fourth class, `HUD Variable Meter`, whose setup is `FUN_100791C0`.

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

Every one of them came from reading a decompile. The fix came from a probe. `texture_probe` is
still in the tree for that reason.

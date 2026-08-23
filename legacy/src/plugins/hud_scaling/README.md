# hud_scaling

**Produces:** `hud_scaling.dll`. Patches `Fellowship.rfl`.

GUI elements are sized in pixels authored for 640x480 and never scaled, so at 4K the volume
slider is a hairline and the checkbox marks are specks.

## The measurement

Pixel-exact, from PNG captures with numpy, not by eye:

| | 640x480 | 800x600 | 3840x2160 |
|---|---|---|---|
| slider bar length | 315 | 315 | 315 |
| slider bar thickness | 6 | 6 | 6 |
| checkbox mark | 18 x 7 | 18 x 7 | 18 x 7 |

Positions, by contrast, fit an exact affine law with zero residual: `0.25 * W + 7`,
`0.40 * H + 39`, and so on. So containers scale and contents do not, and at 640x480 the two agree
because that is the resolution the interface was authored against.

This is a **resolution** bug, not an aspect one. At 800x600 the 315-pixel bar is already 39% of
the width where it was 49% at 640x480, 20% wrong, just too subtle to notice.

## The site

```
rfl+789A7   fld  dword ptr [eax]          the authored value, 3.0 for the slider
rfl+789A9   fstp dword ptr [esi+0x9C]     stored with no resolution term at all
```

Eight bytes, relocated whole into a stub that multiplies by `viewportWidth / 640` on the way past.

## The stub reads through OUR pointer, never the engine's

The first version generated a stub that loaded the engine's active-camera pointer, checked it
against NULL, and read `[camera+0x254]` through it, once per GUI control the game builds. That
is safe only for as long as the pointer is either NULL or a camera, and on a second install it
was neither. From the same run's log:

```
[field_of_view] baseline focal 76.2722, horizontal 180.000 deg
```

`2*atan(halfW/focal)` reaching exactly 180 degrees means `halfW` came back astronomical, off a
pointer that was not NULL. A stub cannot check for that cheaply and has nowhere to report it; it
just takes the access violation. The game crashed with this plugin enabled.

The second version over-corrected: it sampled the scale onto a `float` on a poll thread and had
the stub multiply by that. No engine pointer, no crash, and the wrong number. The pause menu
renders the world into a sub-rectangle, and **the camera's viewport IS that rectangle while the
menu is drawn**, so a value sampled a quarter of a second earlier belongs to a different
viewport. In `text_scaling`, where the same change was made, it showed up as glyphs at stock
height against 4.5x width: measured from the screenshot, a capital G 17 px tall and 86 px wide.

So the read is live again, and the pointer is ours. `common/camera.c` polls, validates; the
pointer has to look like an object, meaning a vtable inside the host image whose first entry is
also inside the host image, then the whole 0x260-byte span, the dimensions, the halves, the focal
length, and the aspect ratio against the rectangle the camera claims to be rendering into, and
publishes the result into a variable in this DLL. It publishes zero when a camera stops
validating, and the stub falls through unscaled on zero, which is also the right answer at the
menus where a GUI built with a divide-by-nothing would be a crash on the title screen.

The engine's global is never dereferenced from generated code again. Ours is, and ours is only
ever a pointer that passed every check.

**Width, not height.** This factor governs a horizontal extent. Text is the opposite case and
scales by height; that is `text_scaling`, with its own reference. Two different references is
correct, not an inconsistency.

## The untemplated branch: tried, measured, taken back out

`rfl+78950` sets a control's two scalars, and it has two ways of doing it:

```
edi = get_template(this)
if (edi) {                              <- rfl+78987
    this->[0x98] = property 0x1B
    this->[0x9C] = property 0x1C        <- rfl+789A7   hooked
} else {                                <- rfl+789B1
    this->[0x98] = 5.0f
    this->[0x9C] = 1.0f                 <- rfl+789BB   NOT hooked
}
```

A control built without a template gets a pixels-per-unit of exactly 1, which never changed with
the resolution, so this looked like the reason the in-game HUD stayed small while the templated
slider scaled. A second hook was written, shipped, and measured against a 640x480 baseline:

| | 640x480 | 3840x2160 | ratio |
|---|---|---|---|
| health bar width | 104 | 598 | 5.75 |
| health bar height | 6 | 6 | **1.00** |
| circle width | 30 | 29 | **1.00** |
| circle height | 29 | 31 | **1.00** |

**It changed nothing.** The circle is the same size in *pixels* at both resolutions, so it never
passes through a control's pixels-per-unit and nothing this plugin does can reach it. The bar's
width tracks the screen because it is natively a percentage of it, 16.3% at 640, 15.6% at 4K,
not because anything scaled it.

The in-game HUD is **positioned by percentage and sized in fixed texels**, which is the same
shape of bug as the inventory cell art, on a different draw path from
this one. Solving the circle's centre across both resolutions gives the affine law with the fixed
pixel term that makes the spacing read wrong:

```
x = 0.0298*W + 16.4        y = 0.105*H + 14.4
```

So the hook came out again. The branch **is** unscaled and someone will find it a second time;
what is recorded alongside it is that fixing it does not fix the HUD.

## `+0x98` is a rate, not a size

Worth keeping whatever happens to the branch above. `+0x98` sits beside `+0x9C`, comes from the
adjacent authored property, and is set by the same function in the same two branches, it reads
as the obvious companion fix. Its one reader says otherwise:

```
rfl+78BD7   fld   [this+0xA4]      target
            fsub  [this+0xA8]      - current
            fld   [frame_time]
rfl+78BEB   fmul  [this+0x98]      * THIS
            fmul  st(1)            * the difference
```

Frame time multiplied by a difference is an **approach rate**, and 5.0 is a sensible one. Scaling
it by 6 at 4K would make every animated control snap six times faster, and nothing about that
symptom would look like a size bug.

## Still unsolved: the in-game HUD

The bar, the ring and the circle are drawn somewhere this plugin does not reach. The next step is
to name that path, not guess at it: breakpoint the texture bind for the circle and log the
caller, the way `rfl+7A2D5` was found for the inventory icons. It may not even be in the rfl,
the exe has its own HUD code and nothing here has touched it.

## Configuration: `[hud_scaling]`

| Key | Default | |
|---|---|---|
| `Enabled` | `1` | |
| `ReferenceWidth` | `640` | larger makes everything smaller |

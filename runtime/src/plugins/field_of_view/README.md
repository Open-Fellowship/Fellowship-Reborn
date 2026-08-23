# field_of_view

**Produces:** `field_of_view.dll`. **On by default.** Turn it off if you run the community patcher's own `CameraFieldOfView`.

Holds the **vertical** field of view constant as the screen gets wider, so a 16:9 monitor shows
more of the world horizontally instead of less of it vertically.

## The problem

The engine's field of view is horizontal. `focal = NUM / tan(fov * pi/360)`, and `NUM` is `64.0`,
which is `halfW`, because the virtual screen is always 128 units wide. The vertical field is
whatever falls out of `halfH = 64 * H / W`:

| | `halfH` | horizontal | vertical |
|---|---|---|---|
| 640x480 | 48.0 | 70.000 | 55.413 |
| 3840x2160, stock | 36.0 | 70.000 | **43.0** |
| 3840x2160, this plugin | 36.0 | 86.067 | 55.413 |

So on a widescreen monitor the stock engine crops the top and bottom off the 4:3 view. This
plugin gives that back.

## Why it sets the focal length and not the constant

The community patcher (`Fellowship.dll`, `CameraFieldOfView=-1.0`) does the same job by rewriting
`NUM` to `64.0 / (0.75 * W/H)` at three sites. That is a correct correction, and it breaks the
inventory: `Fellowship.rfl+7A2D5` places the item models using its own copy of the arithmetic,
hard-assuming the unpatched `64`. The two disagree by exactly `64/48 = 4/3` at 16:9, and every
item icon lands at 0.75x its correct offset from screen centre, at 0.75x its correct size.

That was measured twice, from position and from size independently, on PNG captures:

| | |
|---|---|
| position, solved from two icons on each axis | 0.7477, 0.752, 0.750, 0.744 |
| size, from the key icon at matched scale | 77 px at 4K against 104 expected, so 0.74 |

Position and size shrinking by the *same* factor is the signature of a depth error, not
two separate bugs: the model is placed 4/3x too far away.

The number that fixes it in place is the focal length itself. It read `272.2215`, and

```
focal * tan(fov/2) = 48.0000     exactly, at both fov 20 and fov 70
```

The engine computes `focal = NUM / tan(fov * pi/360)` with `NUM` a qword at `0x520A90`, which is
`64.0` in an unpatched file. 64 is `halfW`, because the engine's virtual screen is always 128
units wide, which is also why the authored field of view is horizontal. The patcher rewrites that
operand at `0x4A4DEE`, `0x4A55DE` and `0x4A5630` to its own qword holding `64.0 / (0.75 * W/H)`,
which is 64 at 4:3 and 48 at 16:9. Hence the 4/3.

Setting the **focal length** avoids it entirely. The inventory sets its own 20-degree field of
view, does its geometry from that same 20 degrees, and restores the previous value through
`GetFOV`/`SetFOV` afterwards, so the world's field of view never enters the icon calculation.

**Do not run this plugin and `CameraFieldOfView=-1.0` together.** Set `CameraFieldOfView=0` in
`Fellowship.ini`. They are two answers to the same question and the patcher's answer is the one
that moves the icons.

## Implementation notes

Writing `focal` alone does nothing visible: the renderer reads `projX` and `projY`, and those are
recomputed only when the viewport is rebuilt. The plugin writes every term `SetViewport` derives
from focal (`+0x248`, `+0x230`, `+0x03C`, `+0x040`), so the change lands on the next frame.

It re-applies on a timer, because the value has to survive every level load, and it skips any
tick where the camera's horizontal field of view reads below 40 degrees. That floor is not
arbitrary: the inventory renders its item models through this same camera object at the item's
own `ModelFOV`, 20 degrees for the ones measured, and writing the world's focal length over that
would put every icon in the wrong place; the exact bug this plugin exists to avoid causing.

## The ceiling, and the crash that put it there

There was a floor and no ceiling, and a log from a second install showed what that costs:

```
[field_of_view] baseline focal 76.2722, horizontal 180.000 deg -> holding vertical at 180.0000 deg
[field_of_view] applied: focal 76.2722 -> 347937712601931479777280.0000, horizontal 180.000 -> 0.000 deg
```

`2*atan(halfW/focal)` reaching *exactly* 180 degrees means `halfW` read back astronomical, so the
camera pointer was not NULL and what it pointed at was not a camera. 180 sailed over the
40-degree floor, was latched as the baseline, permanently, since the baseline is sampled once
and never re-sampled, and a focal length of 3.5e23 went into the projection matrix.

Three things changed. The camera now comes from `common/camera.c`, which validates the pointer,
the readable span, the dimensions, the halves, the focal length and the aspect ratio before any
of it is believed. The accepted field of view is a band, 40 to 170 degrees, and not a floor.
And the target angle and the resulting focal length are each range-checked before anything is
written, because the focal length is the number that actually reaches the renderer.

If this plugin only ever prints a refusal, the camera on that machine is not where it expects it.
Set `Enabled=0`; nothing else depends on it.

## Configuration: `[field_of_view]`

| Key | Default | |
|---|---|---|
| `Enabled` | `1` | |
| `VerticalFOV` | `0` | degrees. `0` = automatic: reproduce the 4:3-equivalent of whatever the game set |
| `IntervalMs` | `400` | how often the value is re-applied |

## Two speeds

`IntervalMs` is how often this re-applies the field of view when nothing is driving it, and 400
is right for that: the value only changes when a level loads or the resolution does.

It is wrong by a factor of twenty-five for a slider. While dev_menu is actually asking for a
value (the channel holds a request), this polls every **16 ms** instead, so the picture follows
the knob and does not lurch after it twice a second. The fast interval is only used while a
request is live, and the work per tick is one camera validation and one float write, so a session
where nobody opens the menu is unaffected.

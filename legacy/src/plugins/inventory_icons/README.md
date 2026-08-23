# inventory_icons

**Produces:** `inventory_icons.dll`. **Off by default, and usually should stay off.**

Item models in the inventory render away from their cells, toward the centre of the screen, and
smaller, at 16:9, and only at 16:9.

## Not a game bug

It is two patches disagreeing. The engine computes `focal = NUM / tan(fov * pi/360)` with `NUM`
the qword `64.0` at `0x520A90`. The community patcher, driven by `CameraFieldOfView=-1.0` in
`Fellowship.ini`, rewrites `NUM` to `64.0 / (0.75 * W/H)` at three sites: `0x4A4DEE`,
`0x4A55DE`, `0x4A5630`. That is a correct Hor+ widescreen correction.

But `Fellowship.rfl+7A2D5`, which places the item models, computes its own distance bases from
the **unpatched** `64`:

```
dXb = (W * 0.5) / tan(fovX * 0.5)
dYb = (H * 0.5) / tan(fovY * 0.5)
```

The two disagree by exactly `64/48 = 4/3` at 16:9 and agree exactly at 4:3. Hence: perfect at
640x480 and 800x600, broken at 1280x720 and 3840x2160, the only bug in this project that is
aspect-driven, not resolution-driven.

## The measurement

```
icon offset from screen centre  =  correct * 0.75
icon rendered size              =  correct * 0.75
```

Position, solved independently from two icons on each axis: 0.7477, 0.752, 0.750, 0.744. Size,
from the key icon's length at matched scale: 77 px at 4K against 104 expected, 0.74. Position and
size shrinking by the *same* factor is the signature of a depth error, the model placed 4/3x too
far away. One scalar, not two bugs.

## The fix

`dXb = dYb = focal * W / 128`, taken from the camera's **actual** focal length instead of
recomputed from the FOV. Substituting the stock focal reduces that to `(W*0.5)/tan(fovX*0.5)`,
the original expression, so with no FOV mod present this plugin changes nothing at all. Verified
numerically: at 640x480 the ratio new/old is 1.00000.

The two instructions relocated into the stub are at `rfl+7A42D` (`fstp dword ptr [esp+0x20]`,
storing `dYb`) and `rfl+7A431` (`fld dword ptr [esp+0x40]`, the model's X extent), eight
bytes taken whole. Between them the stub overwrites both distance bases, and everything
downstream derives from those two, so one correction fixes position and size together.

Checked against a live dump at 4K: `dXb` 10888.861 -> 8166.645, predicted pixel X moving from
3180.0 (measured icon at ~3188) to exactly 3600.0, which is the cell centre.

## Which fix to actually use

Two ways to close the disagreement, and they are not equivalent:

1. **This plugin**, alongside the patcher's FOV correction.
2. **`CameraFieldOfView=0`** in `Fellowship.ini`, which stops the rewriting entirely, plus
   `field_of_view.dll` to get the widescreen field of view back a different way, by setting the
   focal length, which the inventory never reads.

Option 2 is the better default and is why this ships off. `field_of_view`'s README has the
comparison.

## Configuration: `[inventory_icons]`

| Key | Default | |
|---|---|---|
| `Enabled` | `0` | |

# Where the camera lives

What was established while looking for a way to move the camera independently of the player. The
answer was mostly negative and is written down because it rules out three approaches that look
reasonable from the outside.

Measured on `Fellowship.exe`, No-CD, 2,133,459 bytes, from 1,373 frames of play captured out of
the running process.

## The camera object does not hold the camera

`0x00544064` points at the active camera. `common/camera.c` validates the first `0x260` bytes of
it, and that span holds the projection terms and a frustum. It does **not** hold the camera's
world position: across 1,373 frames, no float in it correlates with the player's position, which
moved 21,823 units on x alone during the capture.

The frustum sits at `+0xE8`, six planes of four floats, normal then distance:

```
n = ( 0.6664  0.0000 -0.7456)  d = 0.0        left
n = (-0.6664  0.0000 -0.7456)  d = 0.0        right
n = ( 0.0000 -0.8463 -0.5326)  d = 0.0        top
n = ( 0.0000  0.8463 -0.5326)  d = 0.0        bottom
n = ( 0.0000  0.0000 -1.0000)  d = 32.0       near
n = ( 0.0000  0.0000  1.0000)  d = -1e19      far
```

All four side planes have `d = 0`, so they pass through the origin. **This frustum is in view
space**, where the camera is the origin by definition, so no position appears in it.

The side planes carry the field of view directly: `acos(0.6664)` is 48.2 degrees, so 96.4 full
horizontally, which is what `field_of_view` reports it set. The far plane holding `-1e19` is
`view_distance`'s own value. Two independent confirmations that the layout is read correctly.

## The engine renders camera relative

The view matrix reaches Direct3D through `IDirect3DDevice8::SetTransform`, vtable index 37, and it
carries **rotation only**. Its translation row reads `(0, 0, -1)`:

```
-0.757  -0.427   0.494   0.000
 0.000   0.757   0.654   0.000
-0.654   0.495  -0.573   0.000
-0.000  -0.000  -1.000   1.000
```

The camera position recovered from that is the origin, while the player stood at
`(414612, 215589, 362963)`.

So the camera's world position is subtracted on the CPU and baked into each object's world matrix
before anything is submitted. Roughly 512 world matrices per 1,100 `SetTransform` calls.

That is forced, not eccentric. At coordinates around 400,000 a 32-bit float has about 0.03
units of precision, so absolute world positions would visibly shake. Every large-world engine of
the period does this.

## Culling follows the field of view, not the frustum planes

Writing wider planes into `camera+0xE8` changes nothing. The engine rebuilds them before it culls,
so they are an output to read, not an input to steer.

Widening the field of view does work. With the engine set to a 140 degree vertical field while
Direct3D was handed a 64 degree projection, geometry that had been missing at the edges was drawn
and the popping stopped.

## What that rules out

**The mouse turns the body, not a separate camera angle.** Standing still and sweeping the mouse
moves row 0 of the player's 3x3 at `+0x00F8` through its full range, as walking does. There
is no independent view angle to write.

**Rotating the view matrix is not enough on its own.** It works for terrain, and the interface is
unaffected because the 2D passes set the view to identity. But non-player characters end up in the
wrong places and the landscape textures slide, because the engine has already positioned those on
the CPU against its own camera. Anything driven from the end of the pipeline disagrees with
whatever was decided before it.

**A free or orbiting camera therefore has to move the engine's camera**, which means the player's
matrix at `+0x00F8`, and then stop the character's own model turning with it.

## Fly mode is a player state, not a camera mode

The `fly` cheat resolves to a state change on the Player subobject at object `+0xC8`. State lives
at `+0x14`, fly is state 4, and there is a push and pop pair around a stack of previous states at
`+0x18`, `+0x1C` and `+0x20`. Entering fly constructs a controller at Player `+0xFC` and leaving
destroys it.

So flying moves the player entity and the camera follows as it always does. There is no detached
camera here to borrow.

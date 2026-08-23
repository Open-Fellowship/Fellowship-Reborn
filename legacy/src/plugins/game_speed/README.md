# game_speed

**Produces:** `game_speed.dll`. On by default.

One float in `.rdata` at `0x51C764`, `0.002` becoming `0.0001`. Ported from the community
patcher's `FixGameSpeedTiedToFPS`.

## What the constant is

This README used to say it was the simulation's fixed timestep. It is not one, and there is no
fixed timestep anywhere in this engine. Every consumer of frame time multiplies by the delta at
`0x00543284` directly, plain Euler, thirty-one call sites of `v -= g*dt` and `p += v*dt`.

`0x51C764` is the lower clamp the engine applies to that delta once per frame, inside
`Engine::UpdateTime`:

```
00408F3C   fld   dword ptr [0x51C764]      ; 0.002
00408F42   fcomp st(1)                     ; against the measured delta
00408F4F   fld   dword ptr [0x51C764]      ; taken when the measurement was smaller
00408F55   fst   dword ptr [edi]           ; -> 0x00543284, what the game reads
```

## Why lowering it fixes the speed

The engine reads its clock with `GetTickCount`, which advances about every 15.6 ms. Above roughly
64 fps most frames therefore measure **zero** milliseconds, and the floor hands the simulation
2 ms that did not happen. It does that for most of the frames drawn, so the game clock gains
time, which is what "game speed tied to FPS" describes. At `0.0001` the invention is twenty times
smaller and the symptom goes away.

## Why this is not the fix

It treats the consequence. `frame_timing` removes the cause, by giving the Timer a counter that
can measure a frame at all, after which the delta is real and this floor almost never fires.

Both are worth having. With a fine clock the floor is what still catches a frame that is
genuinely faster than it, which above about 500 fps is a real case rather than a measurement
artefact.

Because the target is `.rdata` and not code, this is the simplest patch in the whole tree: one
float, no instruction boundary to respect and no length to match. It is also the only one where
the value is worth experimenting with, which is why it is exposed rather than hard-coded.

## Configuration: `[game_speed]`

| Key | Default | |
|---|---|---|
| `Enabled` | `1` | |
| `Timestep` | `0.0001` | seconds. The engine's own value is `0.002`. Clamped to `0.000001 .. 0.002` |

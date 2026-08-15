# game_speed

**Produces:** `game_speed.dll`. On by default.

The simulation runs on a fixed timestep of `0.002` seconds, held as a single float in `.rdata` at
`0x51C764`. At modern frame rates that step is coarse enough that how much simulation happens per
frame depends on the frame rate - so the game runs at a different speed on different hardware.

Ported from the community patcher's `FixGameSpeedTiedToFPS`, which writes `0.0001`: twenty times
finer.

Because the target is `.rdata` and not code, this is the simplest patch in the whole tree - one
float, no instruction boundary to respect and no length to match. It is also the only one where
the value is worth experimenting with, which is why it is exposed rather than hard-coded.

## Configuration: `[game_speed]`

| Key | Default | |
|---|---|---|
| `Enabled` | `1` | |
| `Timestep` | `0.0001` | seconds. The engine's own value is `0.002`. Clamped to `0.000001 .. 0.002` |

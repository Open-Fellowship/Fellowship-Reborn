# model_lod

**Produces:** `model_lod.dll`. **On by default.** Costs frame rate in crowded scenes.

Pins every model to its finest level of detail by settling the two branches that step the LOD
chain: `0x485B97` (`75 54` -> `90 90`) and `0x485C46` (`7A` -> `EB`).

A preference, not a fix. The engine's thresholds were chosen for 1024x768 on 2002 hardware, so at
4K a model drops to its coarse form while it is still large on screen. Turning that off costs
frame rate in crowded scenes, which is why it ships off.

If only one of the two writes succeeds the plugin logs `PARTIAL` as an error rather than a
warning, because a half-applied LOD patch is worse than none: the chain can step coarser and
never step back.

## Configuration: `[model_lod]`

| Key | Default | |
|---|---|---|
| `Enabled` | `1` | |

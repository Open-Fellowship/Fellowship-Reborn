# view_distance

**Produces:** `view_distance.dll`. **Off by default.**

How far the engine bothers to draw, and when it starts fading things out. Five independent
switches, none of them a bug fix: the 2002 defaults are correct for 2002 hardware, and every key
here trades frame rate for draw distance.

They live in one DLL because they are one decision with one failure mode; the world draws
further and the frame rate drops, and five separate DLLs would need five READMEs saying this
same paragraph.

## Configuration: `[view_distance]`

| Key | Default | |
|---|---|---|
| `Enabled` | `0` | master switch |
| `FarPlane` | `1` | far clip plane effectively removed, in both the culling frustum and the software clipper |
| `FadeIgnoresCap` | `1` | object fade no longer clamped to the visibility distance |
| `VisibilityCells` | `120` | the engine's own value is `80`. One cell is 2048 units |
| `IgnoreObjectFade` | `1` | per-object authored fade distance ignored |
| `PreloadResources` | `1` | object resources requested regardless of distance; this is the NPC pop-in |

## How the constants work

The engine reads its visibility distance with seven identical `fld [0x5432AC]` instructions and
its far plane with two `fld [0x5432B8]`. Rather than write new values over the engine's own
floats, which other code may also read, the plugin **repoints the operands** at two floats
inside this DLL. `VisibilityCells` is therefore just a number in the ini, with no cave, no
recompile and nothing to undo.

`IgnoreObjectFade` is slightly different: it rewrites `fld [edi+0xC4]` as `fld [absolute]`, six
bytes in both forms, so no instruction boundary moves.

All seven visibility readers are repointed together. Repointing some and not others would leave
the engine disagreeing with itself about where the world ends, which shows up as objects fading
in a band that no longer matches anything.

## Untested

Verified in Hobbiton only. **Bree, Moria and Rivendell** remain untested with everything on, and
they are where a frame-rate cost would show up first.

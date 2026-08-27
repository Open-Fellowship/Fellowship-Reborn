# view_distance

**Produces:** `view_distance.dll`. **On by default.** The first thing to turn off if the frame rate will not hold.

How far the engine bothers to draw, and when it starts fading things out. Five independent
switches, none of them a bug fix: the 2002 defaults are correct for 2002 hardware, and every key
here trades frame rate for draw distance.

They live in one DLL because they are one decision with one failure mode; the world draws
further and the frame rate drops, and five separate DLLs would need five READMEs saying this
same paragraph.

## Configuration: `[view_distance]`

| Key | Default | |
|---|---|---|
| `Enabled` | `1` | master switch |
| `FarPlane` | `1` | far clip plane effectively removed, in both the culling frustum and the software clipper |
| `FadeIgnoresCap` | `1` | object fade no longer clamped to the visibility distance |
| `VisibilityCells` | `113` | the engine's own value is `80`. One cell is 2048 units |
| `IgnoreObjectFade` | `1` | per-object authored fade distance ignored |
| `PreloadResources` | `1` | object resources requested regardless of distance; this is the NPC pop-in |

## How the constants work

The engine reads its visibility distance with seven identical `fld [0x5432AC]` instructions and
its far plane with two `fld [0x5432B8]`. Writing new values over the engine's own floats would
reach other code that reads them too, so the plugin **repoints the operands** at two floats
inside this DLL. `VisibilityCells` is therefore just a number in the ini, with no cave, no
recompile and nothing to undo.

The two floats live in this DLL's own static data, so no space has to be found inside the
game's `.text` and no second fix can want the same cave. One replaces the engine's authored
`80.0`; the other is large enough that every comparison against it fails, which is what
ignoring the authored fade distance means here.

`IgnoreObjectFade` is slightly different: it rewrites `fld [edi+0xC4]` as `fld [absolute]`, six
bytes in both forms, so no instruction boundary moves.

All seven visibility readers are repointed together. Repointing some and not others would leave
the engine disagreeing with itself about where the world ends, which shows up as objects fading
in a band that no longer matches anything.

## The dev menu can drive it

Every site this plugin patches reads either one of its own two floats or a branch it wrote, so
the values can be changed while the game runs without patching anything a second time.
`dev_menu` publishes a request on the channel and this plugin prefers it, the same arrangement
`field_of_view` already has with the camera. Releasing is not offered: the page holds the
settings until the game restarts, and then the ini applies again.

Only what the ini installed can be moved. `FarPlane=0` means those two readers were never
repointed, so the menu's far plane toggle has nothing to switch and does nothing. A plugin that
patched sites nobody asked for to make a menu look complete would be lying about what it does.

| menu control | what it writes |
|---|---|
| `Distance` | the visibility float the seven readers already use |
| `Fade` | the object fade float, in cells, multiplied by 2048 into units |
| `far plane` | swaps the far plane float between enormous and the engine's own, sampled at install |
| `fade cap` | one opcode byte at `0x485D25` and `0x485E71` |
| `preload` | two bytes at `0x485B04` |

### Why the toggles cannot be caught half written

Three of them change **one byte**: `75` becomes `EB`, and the displacement after it is the same
in both forms. A single-byte store is atomic on x86. The fourth changes two bytes at `0x485B04`,
an even address, so it is an aligned 16-bit store and atomic as well. No thread executing those
addresses can observe a partly written instruction.

### Why `IgnoreObjectFade` is a slider and not a toggle

Turning it off means putting `fld [edi+0xC4]` back where `fld [absolute]` now sits. Both forms
are six bytes and only the leading `D9` matches, so five bytes change, and there is no five-byte
atomic store. The float behind the repoint is atomic, so the menu offers a distance instead of
an on and off. The slider cannot reach the engine's real behaviour, which is a distance authored
per object: for that, set `IgnoreObjectFade=0` and restart.

## 113 is the ceiling, and it was measured

At **114 cells and above** the engine puts collision where nothing is drawn. The player walks
into invisible walls, which is worst in a maze: Moria's **Labyrinth** and **3 Passages** fill
with them. At 113 both levels behave. The plugin shipped `120` until this was found, so every
install before that was affected.

The mechanism is not established. 113 has no obvious significance in cells or in units, and the
tempting arithmetic is a trap: a cell is `0x800` units, so every even count lands on a round
`0x1000` boundary and none of them mean anything. What is known is the number and where it was
taken, in both levels, with the slider on the dev menu's Fellowship Reborn page.

Nothing says the limit is the same everywhere. It was found in the two tightest interiors in the
game, which is where phantom collision is impossible to miss; in open country you would walk
through the same bad cells and rarely touch one. Twenty levels have never been checked.

## Untested

Verified in Hobbiton, and in Labyrinth and 3 Passages for the ceiling above. **Bree, Rivendell**
and the rest of Moria remain untested with everything on, and they are where a frame-rate cost
would show up first.

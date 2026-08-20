# OpenFellowship

A reverse engineering and preservation project for the PC version of
*The Lord of the Rings: The Fellowship of the Ring* (Surreal Software, 2002,
Riot Engine).

The aim is the same as its sibling project [OpenPhantom](https://github.com/OpenPhantom/OpenPhantom):
documented, maintainable source that preserves the original game's behaviour on
modern systems, and a modding surface that does not require anybody to patch
bytes by hand ever again.

## Status

| Part | State |
|---|---|
| `legacy/` | **Working.** Loader plus 18 plugins, built and tested on a retail install at 3840x2160. |
| `architecture/` | Notes only. |
| `engine/` | Not started. |
| `editor/` | Not started. |
| `installer/` | Not started. |

## What `legacy/` is

The retail game, running as it always did, with a loader beside it and one DLL
per independent fix in `plugins\`. Nothing is decompiled and nothing is
recompiled; the original executable is patched in memory at run time.

This replaces the file-patching approach it grew out of. Every fix so far
(`_FixEnhancers`) rewrote bytes inside `Fellowship.exe` and `Fellowship.rfl`,
which works but means a fix cannot be turned off without restoring a backup,
two fixes cannot be shipped independently, and a game update invalidates
everything at once. A loader and a folder of plugins fixes all three.

### What it fixes today

On by default:

| | |
|---|---|
| `black_screen` | 8-bit textures ask for `D3DFMT_P8`, which NVIDIA dropped support for and AMD did not. This is why a stock install hangs on a black screen on an NVIDIA card. |
| `edge_popin` | the renderer's guard rect is a hard-coded 3072px box, so above 3072 pixels wide the screen culls its own edges. The one genuine engine bug in the set. |
| `hud_scaling` | menu control sizes are authored for 640x480 and never scale |
| `text_scaling` | all in-game text is drawn at a fixed pixel size. Seven hooks, because text is not one number |
| `resolution_unlock` | the options screen stops filtering the display mode list |
| `game_speed` | the simulation timestep, too coarse at modern frame rates |
| `fps_limit` | frame cap |
| `fog_toggle` | distance fog on and off while the game runs |
| `dev_menu` | an in-game overlay: a live field-of-view slider, the game's own cheats as buttons, and the engine's own 124-entry developer flag menu that no shipping build reaches |
| `level_select` | New Game opens the game's own level list, a finished screen that ships in every rfl and that nothing reaches. Two bytes, found by signature |

Off by default, and documented in the ini: `view_distance`, `model_lod`,
`field_of_view`, `inventory_icons`, `cd_check`, `windowed_res`, `hud_probe`.

### What it does not fix

The in-game HUD (health bar, ring, the small circle) is authored in pixels for
640x480 and stays that size at any resolution. It is a different draw path from
the menus and `hud_scaling` cannot reach it. Two approaches have been tried and
disproved; the write-up, with the disassembly that killed each one, is in
`legacy/src/plugins/hud_scaling/HUD-FINDING.md`.

## House rules

**Every plugin verifies before it writes.** Each site checks the exact bytes it
is about to change and declines, loudly, if they are not what was expected. A
different build of the game gets "not installed" in the log rather than a
corrupted executable.

**A generated stub dereferences nothing but our own data.** Engine memory is
read on a poll thread, where it can be validated and a refusal can be logged. A
stub cannot check anything cheaply and has nowhere to report what it found; when
it is wrong, it is an access violation on a hot path. That rule was written
after a crash caused by breaking it.

**Measure, then patch.** Every fix has a measurement behind it in its own
README, and the ones that were tried and did not work are written down too, with
the evidence that killed them. That record is the point: it is the difference
between a contributor spending an evening rediscovering something and reading
one page.

## Building

32-bit only, MSVC only, deliberately. See `legacy/README.md`.

```
cd legacy
cmake -S . -B build -A Win32
cmake --build build --config Release
```

Verified clean on MSVC 19.38 (VS2022) and 19.50 (VS2026) under `/W4 /WX`. `-A Win32` on its own
takes the newest Visual Studio installed; pin an older one with `-G "Visual Studio 17 2022"`.
Plugins in installs predating this note were built with MinGW GCC 13, under which the strict flags
never applied - `legacy/README.md` says what that hid and why it cannot recur.

Everything lands in `build\dist\`, laid out exactly as it installs. Copy
`dinput8.dll`, `open_fellowship.ini` and `plugins\` next to `Fellowship.exe`.
To uninstall, delete `dinput8.dll`.

## Target build

`Fellowship.exe`, No-CD, **2,133,459 bytes**, ImageBase `0x400000`.
`Fellowship.rfl` is a **PE32 DLL** despite the extension, ImageBase `0x10000000`.

The engine is 32-bit and every offset in this tree assumes it.
`legacy/src/common/engine_types.h` asserts it at compile time.

## Licence

MIT. See `LICENSE`. Not affiliated with or endorsed by any rights holder;
all trademarks belong to their respective owners.

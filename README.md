# OpenFellowship

A reverse engineering and preservation project for the PC version of
*The Lord of the Rings: The Fellowship of the Ring* (Surreal Software, 2002,
Riot Engine).

The aim is documented, maintainable source that preserves the original game's
behaviour on modern systems, and a modding surface that does not require anybody
to patch bytes by hand ever again.

## Scope

Five things, in the order they matter. Everything in this repository should be
justifiable as one of them, and a change that is not is a change that needs a
better reason than "it was interesting".

**1. Fix the game.** The bugs that are genuinely the game's, not the hardware's.
A stock install hangs on a black screen on NVIDIA cards; the renderer culls its
own screen edges above 3072 pixels; a shipped level-select screen is reachable
by nothing. These are defects with a right answer, and fixing one is finished
work rather than a matter of taste.

**2. Modern settings.** Make a 2002 game behave on 2026 hardware, frame rate,
resolution, HUD and text scaling, field of view, and controller support. The
game was authored for 640x480 and a fixed timestep, and most of what looks
broken at 4K is that assumption showing through rather than anything rotten.

**3. Modding.** Tools that let people build models, maps and sounds for this
game without reverse engineering it first. The Blender extension in `editor/`
already reads geometry, animation, levels and textures, and writes them back.
That is also why the object model is documented rather than merely used: 397
classes and 4,262 properties with the developers' own names on them is a modding
surface, and it was a research artifact for about a day. Sound and the interface
strings are the gaps.

**4. Restore content.** Cut content that is still in the files, content that
exists in the console ports and not the PC one, and a mod folder on the main
menu that can load and unload additions without anybody editing an ini by hand.
The engine turns out to be full of things that shipped and are unreachable,
`level_select` is one, the 124-entry developer flag menu is another.

**5. Cheats and the dev menu.** The engine's own debug tooling, put back where a
player can reach it, plus additions in the same spirit. This is last on the list
and it is not an afterthought: it is the part that makes the rest testable, and
several of the findings the other four pillars rest on came out of building it.

## Status

| Part | State |
|---|---|
| `legacy/` | **Working.** Loader plus 24 plugins, built and tested on a retail install at 3840x2160. Covers pillars 1, 2, 4 and 5. |
| `architecture/` | Notes only. |
| `engine/` | Experimental, on a branch. A drop-in `Fellowship.rfl` that forwards to the retail engine, with four of its static registries served from generated code. |
| `editor/` | **Started.** The Blender extension (models, animation, levels, textures) lives here. Pillar 3. |
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

Against the five pillars: `black_screen`, `edge_popin` and `level_select` are **1**;
`hud_scaling`, `text_scaling`, `resolution_unlock`, `game_speed`, `fps_limit`, `windowed_res`,
`field_of_view`, `model_lod` and `view_distance` are **2**; `level_select` and `dev_menu`'s engine
flag page reach content that shipped and nothing else can, which is **4**; `dev_menu` and
`fog_toggle` are **5**. Pillar **3** is not served by any plugin; it lives in `editor/`, because
modding is a tooling problem rather than a runtime one.

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

| `movie_skip` | the opening movies go through a Windows Media runtime Wine only stubs, so under Proton the game waits behind a black screen for an end that never comes. Three bytes, and it goes straight on |

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
`dinput8.dll`, `fix_enhancers.ini` and `plugins\` next to `Fellowship.exe`.
To uninstall, delete `dinput8.dll`.

## Target build

`Fellowship.exe`, No-CD, **2,133,459 bytes**, ImageBase `0x400000`.
`Fellowship.rfl` is a **PE32 DLL** despite the extension, ImageBase `0x10000000`.

The engine is 32-bit and every offset in this tree assumes it.
`legacy/src/common/engine_types.h` asserts it at compile time.

## Licence

MIT. See `LICENSE`. Not affiliated with or endorsed by any rights holder;
all trademarks belong to their respective owners.

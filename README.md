# Fellowship Reborn

A reverse engineering and preservation project for the PC version of
*The Lord of the Rings: The Fellowship of the Ring* (Surreal Software, 2002,
Riot Engine).

The aim is documented, maintainable source that preserves the original game's
behaviour on modern systems, and a modding surface that does not require anybody
to patch bytes by hand ever again.

## Scope

Five things, in the order they matter. Everything in this repository should be
justifiable as one of them, and a change that is none of them needs a
better reason than "it was interesting".

**1. Fix the game.** The bugs that are genuinely the game's, not the hardware's.
A stock install hangs on a black screen on NVIDIA cards; the renderer culls its
own screen edges above 3072 pixels; a shipped level-select screen is reachable
by nothing. These are defects with a right answer, and fixing one is finished
work, not a matter of taste.

**2. Modern settings.** Make a 2002 game behave on 2026 hardware, frame rate,
resolution, HUD and text scaling, field of view, and controller support. The
game was authored for 640x480 and a fixed timestep, and most of what looks
broken at 4K is that assumption showing through, not anything rotten.

**3. Modding.** Tools that let people build models, maps and sounds for this
game without reverse engineering it first. The Blender extension in `tools/`
already reads geometry, animation, levels and textures, and writes them back.
That is also why the object model is documented and not merely used: 397
classes and 4,262 properties with the developers' own names on them is a modding
surface, and it was a research artifact for about a day. Sound and the interface
strings are the gaps.

**4. Restore content.** Cut content that is still in the files, content that
exists in the console ports and not the PC one, and a mod folder on the main
menu that can load and unload additions without anybody editing an ini by hand.
The engine turns out to be full of things that shipped and are unreachable,
`level_select` is one, the 124-entry developer flag menu is another.

**5. Cheats and the dev menu.** The engine's own debug tooling, put back where a
player can reach it, plus additions in the same spirit. It is last on the list
without being an afterthought: it is the part that makes the rest testable, and
several of the findings the other four pillars rest on came out of building it.

## Status

| Part | State |
|---|---|
| `runtime/` | **Working.** Loader plus 24 plugins, built and tested on a retail install at 3840x2160. Covers pillars 1, 2, 4 and 5. |
| `decomp/` | **Started.** Toolchain identified and reproduced; 58 functions match byte for byte. The object model is fully decoded. |
| `documentation/` | The toolchain, the matching method, the object model, and the ordinal map. |
| `architecture/` | Notes only. |
| `engine/` | **Experimental.** A drop-in `Fellowship.rfl` that forwards to the retail engine, with four of its static registries served from generated code. Runs the retail game. |
| `tools/` | **Started.** The Blender extension (models, animation, levels, textures) lives here. Pillar 3. |
| `installer/` | Not started. |

## What `runtime/` is

The retail game, running as it always did, with a loader beside it and one DLL
per independent fix in `plugins\`. Nothing is decompiled and nothing is
recompiled; the original executable is patched in memory at run time.

This replaces the file-patching approach it grew out of. Every fix so far
rewrote bytes inside `Fellowship.exe` and `Fellowship.rfl`,
which works but means a fix cannot be turned off without restoring a backup,
two fixes cannot be shipped independently, and a game update invalidates
everything at once. A loader and a folder of plugins fixes all three.

### What it fixes today

Against the five pillars: `black_screen`, `edge_popin` and `level_select` are **1**;
`hud_scaling`, `text_scaling`, `resolution_unlock`, `game_speed`, `fps_limit`, `windowed_res`,
`field_of_view`, `model_lod` and `view_distance` are **2**; `level_select` and `dev_menu`'s engine
flag page reach content that shipped and nothing else can, which is **4**; `dev_menu` and
`fog_toggle` are **5**. Pillar **3** is not served by any plugin; it lives in `tools/`, because
modding is a tooling problem, not a runtime one.

On by default:

| | |
|---|---|
| `edge_popin` | the renderer's guard rect is a hard-coded 3072px box, so above 3072 pixels wide the screen culls its own edges. The one genuine engine bug in the set. |
| `hud_scaling` | menu control sizes are authored for 640x480 and never scale |
| `text_scaling` | all in-game text is drawn at a fixed pixel size. Seven hooks, because text is not one number |
| `resolution_unlock` | the options screen stops filtering the display mode list |
| `game_speed` | the simulation timestep, too coarse at modern frame rates |
| `fps_limit` | frame cap |
| `fog_toggle` | distance fog on and off while the game runs |
| `dev_menu` | an in-game overlay: a live field-of-view slider, the game's own cheats as buttons, and the engine's own 124-entry developer flag menu that no shipping build reaches |
| `level_select` | New Game opens the game's own level list, a finished screen that ships in every rfl and that nothing reaches. Two bytes, found by signature |
| `view_distance` | how far the engine bothers to draw. Not a bug fix: the original distances were chosen for 2002 hardware. The first thing to turn off if the frame rate will not hold |
| `model_lod` | pins models to their finest LOD. Costs frame rate in crowded scenes, and is the second thing to turn off |
| `field_of_view` | the camera's field of view. Needs `CameraFieldOfView=0` in `Fellowship.ini`, because that option and this plugin are two answers to the same question |
| `frame_timing` | gives the engine a high resolution clock, so the frame delta stops being quantised to 15.6 ms. This is the one that makes it smooth |

Always on, with no switch, because a key whose only purpose is to disarm a working patch
invites turning off the one that was working:

| | |
|---|---|
| `black_screen` | 8-bit textures ask for `D3DFMT_P8`, which NVIDIA dropped support for and AMD did not. This is why a stock install hangs on a black screen on an NVIDIA card |
| `cd_check` | the disc check, redirected to a stub that answers yes. The callee itself is untouched |

Decided at run time:

| | |
|---|---|
| `movie_skip` | the opening movies go through a Windows Media runtime Wine only stubs, so under Proton the game waits behind a black screen for an end that never comes. Three bytes. On under Wine and Proton, off on Windows, and `Enabled` in the ini overrides both |

Off by default: `borderless`, `windowed_res`, `inventory_icons`, and the four diagnostics
`env_probe`, `frame_state`, `screen_test` and `hud_probe`. Each says in the ini what it is
for and what turning it on costs. `template_plugin` is the copy-me skeleton for a new
plugin and does nothing.

That is every plugin in the tree. If this list and
[runtime/dist/fellowship_reborn.ini](runtime/dist/fellowship_reborn.ini) ever disagree, the
ini is right: it is what actually ships.

### What it does not fix

The in-game HUD (health bar, ring, the small circle) is authored in pixels for
640x480 and stays that size at any resolution. It is a different draw path from
the menus and `hud_scaling` cannot reach it. Two approaches have been tried and
disproved; the write-up, with the disassembly that killed each one, is in
`runtime/src/plugins/hud_scaling/HUD-FINDING.md`.

## House rules

**Every plugin verifies before it writes.** Each site checks the exact bytes it
is about to change and declines, loudly, if they are not what was expected. A
different build of the game gets "not installed" in the log, not a
corrupted executable.

**A generated stub dereferences nothing but our own data.** Engine memory is
read on a poll thread, where it can be validated and a refusal can be logged. A
stub cannot check anything cheaply and has nowhere to report what it found; when
it is wrong, it is an access violation on a hot path. That rule was written
after a crash caused by breaking it.

**Measure, then patch.** Every fix has a measurement behind it in its own
README, and the ones that were tried and did not work are written down too, with
the evidence that killed them. That record is the difference between a contributor
spending an evening rediscovering something and reading one page.

## Building

32-bit only, MSVC only. See `runtime/README.md` for why.

```
cd runtime
cmake -S . -B build -A Win32
cmake --build build --config Release
```

Verified clean on MSVC 19.38 (VS2022) and 19.50 (VS2026) under `/W4 /WX`. `-A Win32` on its own
takes the newest Visual Studio installed; pin an older one with `-G "Visual Studio 17 2022"`.
Plugins in installs predating this note were built with MinGW GCC 13, under which the strict flags
never applied; `runtime/README.md` says what that hid and why it cannot recur.

Everything lands in `build\dist\`, laid out as it installs. Copy
`dinput8.dll`, `fellowship_reborn.ini` and `plugins\` next to `Fellowship.exe`.
To uninstall, delete `dinput8.dll`.

## Target build

`Fellowship.exe`, No-CD, **2,133,459 bytes**, ImageBase `0x400000`.
`Fellowship.rfl` is a **PE32 DLL** despite the extension, ImageBase `0x10000000`.

The engine is 32-bit and every offset in this tree assumes it.
`runtime/src/common/engine_types.h` asserts it at compile time.

Both were built by **Visual C++ 6.0 with the Processor Pack**, linker 6.00, `/MD`, no `/GS`,
optimised `/O2 /Gy /GX`. `documentation/TOOLCHAIN.md` has the Rich-header measurement behind that,
the confirmation from the Processor Pack's own `C2.DLL`, and how to assemble the toolchain
without installing anything.

`decomp/` is where that gets used: source which, compiled by that toolchain, reproduces the
original's bytes exactly. That is not the engine itself, but how `engine/` gets verified. A subsystem
is matched byte for byte to prove it is understood completely, and only then written properly.
58 functions match today, including the whole `Vector3` class; `python decomp/build.py` re-checks
them all and reports coverage against an honest denominator.

`engine/` is the beginning of replacing the engine module itself rather than patching it from
outside. See [BRANCH-NOTES.md](BRANCH-NOTES.md) for what exists, what is verified and what is not.

## Licence

MIT, with one exception. See `LICENSE`.

The Blender extension in `tools/blender/fotr_importer/` is **GPL-3.0-or-later**, declared in
its own `blender_manifest.toml`. Blender's extensions platform requires it of anything that
imports `bpy`, so the add-on cannot carry the same licence as the rest of the tree. Nothing
outside that directory is affected, and nothing in the runtime links against it.

Not affiliated with or endorsed by any rights holder; all trademarks belong to their
respective owners.

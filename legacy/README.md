# legacy

Fixes for the retail engine of *The Lord of the Rings: The Fellowship of the Ring*
(Surreal Software, 2002, Riot Engine).

One loader plus one DLL per independent fix. Nothing is decompiled and nothing is recompiled: the
original executable is patched in memory at run time, and every fix can be added or removed by
moving one file.

## Install layout

```
<game folder>\
    Fellowship.exe
    Fellowship.rfl
    dinput8.dll               <- the loader
    open_fellowship.ini       <- configuration, optional
    open_fellowship.log       <- written at run time
    plugins\
        template_plugin.dll
        ...
```

## Build

32-bit only. `src/common/engine_types.h` asserts it at compile time, and CMake refuses at
configure time.

```
cmake -S . -B build -A Win32
cmake --build build --config Release
```

Everything lands in `build\dist\`, laid out exactly as it installs.

Verified clean on **MSVC 19.38 (VS2022)** and **19.50 (VS2026)**, x86, `/W4 /WX`. If CMake is left
to choose, it takes the newest Visual Studio installed; pass `-G "Visual Studio 17 2022"` to pin an
older one.

MSVC is the only supported toolchain, deliberately. The loader's entry-point stub is
`__declspec(naked)` inline assembly and the proxy's export names come from `/EXPORT` pragmas;
supporting a second compiler would mean a second implementation of both, and two implementations
of the most safety-critical code in the tree is a poor trade for build convenience.

### If you find MinGW-built DLLs in an install

You will, in older ones. Every plugin shipped before this note was built by **MinGW GCC 13**, and
that is worth knowing rather than hiding, because it explains something about the code.

The strict flags above live inside `if(MSVC)` in `CMakeLists.txt`, so under GCC none of them
applied. `/W4 /WX` did not exist, and constructs MSVC rejects outright went unnoticed - most
sharply `typedef void (__thiscall *fn)(...)`, which GCC accepts in C as an extension and MSVC does
not accept at all. The result was a tree that built cleanly every day under the toolchain the
README said was unsupported, and had never once built under the one it said was required.

That is no longer a choice anyone has to make: **MSYS2 has dropped its 32-bit `mingw32`
environment**, so there is no i686 GCC to go back to. MSVC is now the only toolchain that can build
this project, and as of this note it does, from clean, on both versions above.

The four things that had to change are worth naming, since all four were real:

| | |
|---|---|
| C4702 | four poll threads whose `for (;;)` never exits, so the mandatory `return` is provably unreachable. It is a code-generator warning attributed to the end of the function, so `#pragma warning(suppress)` at the return does not reach it - only a whole-function region does. `src/common/compiler.h` holds that idiom once, with the reasoning |
| C4456 | an inner `index` shadowing the tab loop's in `dev_menu.c` |
| `__thiscall` | not accepted on a function-pointer typedef in C. Now `__fastcall` with a dead EDX parameter, which on x86 is exact rather than approximate: `this` in ECX either way, the real arguments in the same stack slots, callee cleanup either way, and `__thiscall` never reads EDX |
| generator | `-A Win32` alone silently selects the newest Visual Studio present |

## Why a loader and not a byte patcher

This replaces the approach it grew out of. The `_FixEnhancers` work rewrote bytes inside
`Fellowship.exe` and `Fellowship.rfl` directly. That works, and several non-trivial fixes came out
of it, but it has three costs a loader does not:

* a fix cannot be turned off without restoring a backup;
* two fixes cannot be shipped, tested or blamed independently;
* one changed game file invalidates every patch at once.

The fixes already proven by that work are being ported into plugins here. The engineering write-ups
that justify each one live in `_FixEnhancers/docs` and are the reference for what each plugin has
to reproduce.

## The plugins

| | patches | default | |
|---|---|---|---|
| `edge_popin` | exe | **on** | the guard rect is a hard-coded 3072 px box, so 4K culls its own edges. The only genuine engine bug here |
| `fog_toggle` | exe | **on** | F1 turns distance fog off and on while the game runs |
| `view_distance` | exe | off | far plane, visibility cells, object fade, resource preload. Five switches, all costing frame rate |
| `model_lod` | exe | off | pins models to their finest LOD |
| `field_of_view` | exe | off | holds the *vertical* field of view constant as the screen widens |
| `hud_scaling` | rfl | **on** | GUI sizes are authored in 640x480 pixels and never scale. Menu controls only - the in-game HUD is a different draw path, see its README |
| `text_scaling` | rfl | **on** | all in-game text is drawn at a fixed pixel size. Seven hooks |
| `inventory_icons` | rfl | off | only needed alongside a FOV mod that rewrites the focal numerator |
| `black_screen` | exe | **on** | 8-bit textures ask for D3DFMT_P8, which NVIDIA dropped. Reads before it writes |
| `hud_probe` | exe | off | a diagnostic, not a fix: records which code reads which authored value |
| `dev_menu` | draws | **on** | an in-game overlay, toggled with the key under Escape. Hooks nothing until you press it |
| `template_plugin` | nothing | **on** | the loader contract's own test |

Each has its own README next to its source, with the measurement that justifies it.

## What is here now

| | |
|---|---|
| `src/common/` | host image geometry, logging, ini, memory, patch, trampoline, emit, module watch, camera, channel |
| `src/loader/` | `dinput8.dll`. Loads `plugins\`, patches nothing itself. See its README |
| `src/plugins/` | one directory per plugin |
| `dist/` | `open_fellowship.ini`, copied into the build output |

`camera.c` earns its place by being the answer to a crash. Three plugins were written against the
comment "`EXE_ACTIVE_CAMERA_PTR` is NULL outside a level", and on a second install it was neither
NULL nor a camera. The rule that came out of it, and that the tree now follows:

> **A generated stub dereferences nothing but our own data.** Read engine memory on a poll
> thread, where it can be validated and a refusal can be logged, and let the stub multiply by a
> value in the plugin's own data section. A stub cannot check anything cheaply and has nowhere to
> report what it found; when it is wrong, it is an access violation on a hot path.

Still to come in `common`: `signature.c`, so sites are found by what the code *is* rather than by
address, and `detour.c`, so two plugins can hook the same engine function without the second one
overwriting the first. Neither is needed by anything here yet - no two plugins currently share a
site - but both are needed before this tree has many more plugins in it.

## From byte patches to plugins

Every fix here was first proved as a direct byte patch on `Fellowship.exe` or `Fellowship.rfl`,
and those patchers are still in `_FixEnhancers/patches` with the write-ups that justify them in
`_FixEnhancers/docs`. Two things changed on the way in:

**Constants moved out of the game.** The byte patches had to find unused space inside `.text` -
the zero region at `0x51B302`, the slack past the rfl's VirtualSize - to hold a float or a stub.
A plugin uses its own static or its own allocated page instead, so `VisibilityCells` is a number
in an ini rather than a recompile, and two fixes can never want the same cave.

**Everything re-verifies before it writes.** Each site checks the exact bytes it is about to
overwrite and declines, loudly, if they are not what was expected. A different build of the game
therefore gets "not installed" in the log rather than a corrupted executable.

## Coexistence

Two things are already in most installations of this game and both must keep working:

* **`d3d8.dll`**, a graphics wrapper. Untouched: the loader deliberately takes the `dinput8`
  slot instead. See `src/loader/dinput8_proxy.h` for why.
* **`Fellowship.dll` + `FellowshipPatcher.exe`**, the community patcher. It rewrites operands
  inside the executable at run time, including the field-of-view numerator at `0x520A90`
  (`0x4A4DEE`, `0x4A55DE`, `0x4A5630`). Any plugin touching the camera has to know that; the
  inventory-icon investigation in `_FixEnhancers/docs/12` is the worked example of what happens
  when two patches disagree about the same constant.

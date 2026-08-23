# Where the plugins patch, and what those functions are

Every address `runtime/` writes to, mapped to the function that contains it.

This exists because the two halves of the project answer different questions about the same
bytes. A plugin proves what a site *does* - `black_screen` shows that the dword at `0x0043D2BC`
decides whether the game renders at all - but says nothing about the function around it. A
decompiled function proves what the code *is*. Pointing the second at the sites the first already
found is the cheapest way to make them agree, because the behaviour is established before the
matching starts and a wrong reading of the disassembly has something to fail against.

All addresses are virtual addresses in `Fellowship.exe`, image base `0x00400000`.

## The image

| section | range | |
|---|---|---|
| `.text` | `0x00401000` - `0x0051c000` | code, 1.16 MB |
| `.rdata` | `0x0051c000` - `0x0052d60c` | constants; the float pools the view-distance and speed patches edit |
| `.data` | `0x0052e000` - `0x00567000` | globals; the debug-menu and message objects |
| `.idata` | `0x00567000` - `0x00568a02` | import thunks |
| `.cms_t` / `.cms_d` | `0x0056a000` - `0x00606d6c` | **SecuROM.** Not Surreal's code |

No plugin patches inside the SecuROM range, which is worth stating: every site below is game code.
The wrapper inflates the function census - 4,351 functions in the exe includes it - so exe coverage
percentages are pessimistic by however much of `.cms_t` Ghidra turned into functions.

## Functions containing a patch site

`calls` is the number of distinct call targets, so `0` means a leaf. Leaves are the cheap
decompilation targets; the call count is roughly how much surrounding structure has to be declared
before a function will compile at all.

| function | size | calls | patched by | status |
|---|---|---|---|---|
| `0x0043d2b0` | 150 | 0 | `black_screen` at `0x43d2b6`, `0x43d2bc` | **matched** - `PixelFormatFromDepth` |
| `0x00411ba0` | 29 | 0 | `dev_menu` at `0x411ba0` | **matched** - `DebugMenuFlags::GetValue` |
| `0x0044e670` | 103 | 5 | neighbour of `hud_probe`'s site | **matched** - `GlobalArray::SetCount` |
| `0x0048b820` | 252 | 0 | neighbour of `edge_popin`'s guard rect | **matched** - `DeviceCapsCheck` |
| `0x0048bdd0` | 40 | 0 | neighbour of `fog_toggle`'s hook | **matched** - `RenderGate::Gate` |
| `0x00404630` | 933 | 28 | `fps_limit` - the per-frame function it tail-jumps | |
| `0x004063d0` | 382 | 4 | `cd_check` - the caller that tests the result | |
| `0x00411800` | 760 | 6 | `dev_menu` - `SetFlag` | |
| `0x00458a60` | 354 | 1 | `view_distance` | |
| `0x004855c0` | 1115 | 12 | `edge_popin` - the frustum-vs-guard-rect branch | |
| `0x00485b60` | 346 | 2 | `model_lod` - both branches | |
| `0x00485cf0` | 276 | 2 | `view_distance` | |
| `0x00485e40` | 232 | 1 | `view_distance` | |
| `0x00494490` | 1045 | 1 | `view_distance` | |
| `0x0049ab60` | 393 | 4 | `view_distance` | |
| `0x004a24f0` | 732 | 9 | `view_distance` | |
| `0x004a4ce0` | 360 | 2 | `inventory_icons` | |
| `0x004a5bf0` | 424 | 4 | `view_distance` | |
| `0x004bc450` | 643 | 3 | `resolution_unlock` (3 sites) **and** `windowed_res` (3 sites) | |
| `0x004bc880` | 465 | 18 | `fps_limit` - the once-per-frame call site | |
| `0x004bd2c0` | 451 | 7 | `cd_check` - the disc check itself, which the plugin does not modify | |

Two things fall out of the table.

**`0x004bc450` is the mode-setting function**, and it is the single busiest patch site in the
project: `resolution_unlock` defuses three branches in it and `windowed_res` edits three more.
643 bytes with only three calls, so it is mostly its own logic. It is the highest-value target
left, and also the one where a byte-for-byte match would retire the most guesswork, because six
separate patches currently rest on reading its branches correctly.

**`view_distance` patches nine sites across eight functions.** That is not one clamp with one
constant; it is the same distance test written out in eight places. Decompiling two or three of
them would establish whether they are copies of one inlined helper or genuinely independent code,
which decides whether the plugin's nine-site sweep is the right shape or a symptom.

## Sites Ghidra never turned into functions

Eleven patch sites are in code that auto-analysis left as raw bytes - no function starts there and
none contains them:

| address | plugin | |
|---|---|---|
| `0x0044e6e0` | `hud_probe` | the getter it hooks, and its return at `0x0044e6e6` |
| `0x0048b984` | `edge_popin` | guard rect left/top, and right/bottom at `0x0048b992` |
| `0x0048bef0` | `fog_toggle` | the fog hook, and its return at `0x0048befa` |
| `0x00411bc0` | `dev_menu` | the menu entry dispatcher |
| `0x004a55de` | `inventory_icons` | two sites, with `0x004a5630` |
| `0x004a13a3` | `view_distance` | two sites, with `0x004a1e81` |

These are gaps in the analysis, not gaps in the game. A function Ghidra misses is usually one
nothing calls directly - reached only through a vtable or a jump table - and every one of these was
found by the plugin work rather than by the disassembler, which is a fair measure of how much the
two approaches see that the other does not.

They cannot be decompilation targets until they are functions. Fixing that means forcing
disassembly at each address in the Ghidra project and creating a function there, then re-running
the export. Until then the census undercounts, and the four plugins above are patching code that
the decompilation side cannot yet see.


## What the first five said

All five matched, and two of them are unmasked - no relocations at all, so every byte was compared
and the match cannot be an artefact of the comparison.

**`0x0043d2b0` is the pixel format table** (`PixelFormatFromDepth`, 150 bytes, 0 masked). It maps a
bit depth plus the engine's own surface-flag word to a `D3DFORMAT`. This is exactly what
`black_screen` says it is: at 8 bits per pixel it returns `D3DFMT_P8`, and `0x0043d2bc` is that
immediate's operand. The rest of the table is now readable - 24-bit gives `D3DFMT_R8G8B8`, 32-bit
gives `X8R8G8B8`, `A8R8G8B8` or the `YUY2` FOURCC depending on flags, and 16-bit picks between
`R5G6B5`, `X1R5G5B5`, `A1R5G5B5`, `A4R4G4B4` and `A8R3G3B2`. The alpha-width field is `0xf0000`,
compared whole; the meaning of its `0x20000` value and of everything above `0x80000` is untested.

**`0x0048b820` is the renderer's feature detection** (`DeviceCapsCheck`, 252 bytes, 0 masked). It
takes a `D3DCAPS8`, refuses anything that is not a `D3DDEVTYPE_HAL` hardware rasteriser with
texture alpha and perspective correction - returning `D3DERR_INVALIDDEVICE` - and otherwise
translates fourteen D3D capability bits into the engine's own capability word. Among them are
`D3DTEXOPCAPS_BUMPENVMAP` and `_BUMPENVMAPLUMINANCE`, so bump mapping is detected here, and
`D3DCAPS2_FULLSCREENGAMMA` and `_CANCALIBRATEGAMMA`. The D3D side is fully named; **the engine
capability word's bits are not** - each is named only for the D3D capability that sets it, and
nothing yet says what the engine does with any of them. That word is the obvious next thread: find
its readers and the fourteen bits acquire meanings.

**`0x0044e670` is an array resize on the Win32 global heap** (`GlobalArray::SetCount`, 103 bytes).
Not a plugin site itself - it is the function immediately before the getter `hud_probe` hooks - but
it settled the rule for every Win32 call in the exe: imports must be declared
`__declspec(dllimport)`, which is what produces the six-byte `CALL dword ptr [thunk]` the game uses
instead of a five-byte `CALL rel32` into a linker stub.

**`0x00411ba0` is the debug menu's value getter** (`DebugMenuFlags::GetValue`, 29 bytes) - the
counterpart to the setter `dev_menu` calls. It confirms the plugin's reading of the layout: a dword
array at `+0xe0` indexed directly, with index `0x2f` special-cased to the standalone global at
`0x00543434`. Why that one index is separate is still unestablished; only that the getter and
setter agree on it.

**`0x0048bdd0` is a gate of some kind** (`RenderGate::Gate`, 40 bytes, 0 masked) and the name is
provisional. It asks a subobject a question through vtable slot 35, returns 0 if the answer is
non-zero, and otherwise bumps a counter and returns 1. It sits below the fog-state code
`fog_toggle` hooks, which is where it was found and is the only reason it is filed under render.
Nothing in it says what is being gated or what the counter counts. The byte-for-byte match is
solid; the naming is a placeholder and the file says so.

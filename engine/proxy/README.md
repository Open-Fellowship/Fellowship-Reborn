# The engine proxy

A drop-in replacement for `Fellowship.rfl` that forwards every call to the retail engine.

It changes nothing about how the game plays, and that is the point. It establishes the seam. Once
the host is talking to this DLL instead of the retail one, a function moves from *forwarded* to
*reimplemented* by changing one function body, and the game keeps running the whole way. There is
never a point where the project has half an engine and no way to test it.

This is the phase-one skeleton that [OpenJones3D](https://github.com/smlu/OpenJones3D) describes:
start with everything thunking into the original, replace incrementally, and only at the very end
stop needing the original at all.

## Why the rfl and not the exe

`Fellowship.rfl` is a DLL with exactly **eleven exports**, and everything the executable gets from
the engine arrives through them. They return interface structures rather than being called
piecemeal, so the boundary between host and engine is unusually narrow, one file, eleven names.

It is also the half that matters. The game logic lives in the rfl: the ObjectDef registry, the
property system, the Player class, the behaviours. The exe is the renderer, input and window host,
and it reads a total of nineteen authored properties in the entire binary against the rfl's 2,409.

## Install

    Fellowship.exe
    Fellowship.rfl        <- build/dist/Fellowship.rfl, this DLL
    Fellowship.orig.rfl   <- the retail engine, renamed

Nothing else moves. Keep a copy of the retail `Fellowship.rfl` somewhere safe before renaming it;
the hashes of a pristine one are in [TOOLCHAIN.md](../../documentation/TOOLCHAIN.md).

If the renamed original is missing, this DLL fails at the first exported call and says so in
`fellowship_reborn_engine.log` beside itself, rather than returning null interfaces. A null interface
surfaces later as an unexplained crash inside the host, which is a much worse thing to debug than a
refusal at startup.

## Build

    cmake -S engine -B engine/build -A Win32
    cmake --build engine/build --config Release

32-bit only, this DLL loads the retail 32-bit module into its own process. Static CRT, so it needs
no redistributable in a 2002 game folder.

## Verifying it is a faithful stand-in

The export table must match the retail module exactly: same names, same ordinals, undecorated. Two
of the ordinals are not in the order the names suggest, `IsObjectMoveNode` is 7 and
`IsObjectPortal` is 8, which is the kind of thing that would break silently if a host imported by
ordinal.

```
python - <<'PY'
import sys, struct; sys.path.insert(0, "decomp/tools")
from classdump import Image
def exports(path):
    img = Image(path); d = img.d
    ob = struct.unpack_from("<I", d, 0x3c)[0] + 24
    off = img.off(img.base + struct.unpack_from("<II", d, ob + 96)[0])
    f = struct.unpack_from("<IIHHIIIIIII", d, off)
    no, oo = img.off(img.base + f[9]), img.off(img.base + f[10])
    return {img.string(img.base + struct.unpack_from("<I", d, no + i*4)[0]):
            f[5] + struct.unpack_from("<H", d, oo + i*2)[0] for i in range(f[7])}
print(exports(r"<retail>\Fellowship.rfl") == exports(r"engine\build\dist\Fellowship.rfl"))
PY
```

## Moving a function across

1. Write it in a module beside `predicates.c`, named for its subsystem.
2. Delete the forwarding body in `proxy.c`; the linker will take yours.
3. Rebuild, run the game, confirm nothing changed.

**Prefer functions that were matched byte for byte first.** `decomp/manifest.tsv` lists them, and a
reimplementation seeded from verified source is a much stronger claim than one written to look
right. The two predicates already here are pure (no state, no globals, no calls) so if the game
misbehaves after installing this, the cause is the proxy mechanism rather than them.

## What is implemented

| export | |
|---|---|
| `IsObjectPortal` | **ours**, from `decomp/src/objectdef/predicates.cpp`, matched, 0 relocations |
| `IsObjectMoveNode` | **ours**, same |
| `IsObjectLight` | forwarded, matched, but calls two functions inside the retail image that are not reachable by name |
| `GetBaseRFLInterface` | forwarded |
| `GetLandTypeInterface` | **ours**, `objectdef/landtype_table.c`, 192 records. Switchable: `LandTypes=0` |
| `GetMessageInterface` | **ours**, `objectdef/message_table.c`, 54 records. Switchable: `Messages=0` |
| `GetObjTypeInterface` | **ours**, `objectdef/objtype_table.c`, generated and verified the same way. Switchable: `ObjTypes=0` |
| `GetObjectDefInterface` | **ours**, `objectdef/objectdef_table.c`, generated from the retail image and verified field by field against it. Switchable: `ObjectDefs=0` |
| `RiotDllGetID` | forwarded |
| `RiotDllType` | forwarded |
| `DllMain` | ours, trivially |

The two `RiotDll*` functions are six bytes each and return constants (`1` and `0x80010003`), so they
could be ours today. They are left forwarded on purpose, and the reason has changed since the seam
was first proved: they are the *first* thing the host asks the module, so while they forward, a
missing `Fellowship.orig.rfl` is reported before the game gets anywhere. Taking them over would move
that failure later and make it quieter.

### All four registries

The engine publishes four static registries, and they are one mechanism rather than four. Each has an
initialiser that is two `mov dword [global], imm32` stores and a return, registered in the C++
static-initialiser table at `0x100fd004` and run at DLL load; the matching export returns the address
of the global pair. Scanning `.text` for that shape finds **exactly four**, which is also exactly the
number of exports returning a `{count, table}` pair, so the set is complete rather than as far as
anyone looked.

| registry | records | record |
|---|---:|---|
| ObjectDef | 397 | the class table, with 494 property groups and 4,262 properties beneath it |
| LandType | 192 | `{name, flags}` |
| Message | 54 | `{name, id}`, what a level's triggers send, ids sparse above `0x100` |
| ObjType | 19 | `{id, name}`, **the field order is the reverse of the other two** |

None of the four initialisers is a function as far as Ghidra is concerned: they sit in the 220 KB of
`.text` it never turned into one. That is why they took finding, and it is measured rather than
guessed, see `decomp/census.tsv`.

All four are generated by the same tool and checked by the same comparison:

    python decomp/tools/objdefgen.py --emit   engine/objectdef
    python decomp/tools/objdefgen.py --verify engine/objectdef

`--verify` compiles the generated C and compares **9,334 non-pointer fields** against the retail
image, following the pointers rather than requiring two linkers to agree on addresses.

## Switching a system off

Every reimplemented system has a key in `fellowship_reborn_engine.ini` beside the DLL:

```ini
[engine]
ObjectDefs=1     ; 1 = ours, 0 = forward to the retail module
ObjTypes=1
LandTypes=1
Messages=1
```

Missing file or missing key means on. That is not a convenience but a way for a regression to get
attributed. A reimplementation can be faithful in every field a comparison can reach and still
change behaviour through something the comparison cannot see, and when that happens the only cheap
question is whether turning it off stops it. That has to be answerable without a rebuild, in one
sitting, by whoever noticed.

It has already earned itself once, for a fault that turned out not to be ours at all: NPCs stopped
walking their paths, and the cause was a missing `LevelList.txt` in the install.

## Not yet established

How the host finds the module. `Fellowship.exe` has no static import of the engine and contains no
`.rfl` string, so the name is built rather than spelled out, most likely from the executable's own
path, since the retail pair share a stem. The exe does hold `RiotDllType` and `RiotDllGetID` as
`GetProcAddress` strings and a `SOFTWARE\Surreal\Riot Engine` registry key, so there may be a
configured path involved as well. **This has not been confirmed by reading the loading code**, and
until it is, the installation above is the arrangement most likely to work rather than the one known
to.

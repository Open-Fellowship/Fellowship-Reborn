# What Fellowship.rfl exports

`Fellowship.rfl` is a DLL, and it has an export table. Eleven functions, exported **by name and
undecorated**, which is to say: eleven real names, chosen by the developers, attached to eleven
known addresses.

That is a small number and a large fact. The images carry no debug directory, no RTTI and no linker
map, so the decompilation work has been inventing every name it uses. These eleven are not
invented, and neither is the vocabulary they imply.

| ordinal | address | size | name |
|---:|---|---:|---|
| 1 | `0x1000d750` | 6 | `GetBaseRFLInterface` |
| 2 | `0x100486a0` | 6 | `GetLandTypeInterface` |
| 3 | `0x1004c270` | 6 | `GetMessageInterface` |
| 4 | `0x1004c2b0` | 6 | `GetObjTypeInterface` |
| 5 | `0x1004c230` | 6 | `GetObjectDefInterface` |
| 6 | `0x1000d760` | 163 | `IsObjectLight` |
| 7 | `0x1000d820` | 16 | `IsObjectMoveNode` |
| 8 | `0x1000d810` | 16 | `IsObjectPortal` |
| 9 | `0x10024a50` | 6 | `RiotDllGetID` |
| 10 | `0x1000d740` | 6 | `RiotDllType` |
| 11 | `0x1000d720` | 17 | `DllMain` |

## The vocabulary

**ObjectDef** is the engine's own name for the class registry described in
[OBJECT-MODEL.md](OBJECT-MODEL.md). `GetObjectDefInterface` is six bytes (`mov eax, 0x10132874;
ret`) returning the address of a pair of globals that an initialiser fills in with the record
count and the table base. So what that document calls "the class registry" should be called the
**ObjectDef table**, and its 397 entries are ObjectDefs.

The other four getters have the same six-byte shape and return four more interface globals:

| | returns | |
|---|---|---|
| `GetBaseRFLInterface` | `0x10132684` | |
| `GetLandTypeInterface` | `0x10132858` | |
| `GetMessageInterface` | `0x1013286c` | |
| `GetObjectDefInterface` | `0x10132874` | count `0x18d` = 397, table `0x1010f0a0` |
| `GetObjTypeInterface` | `0x1013287c` | count `0x13` = 19, table `0x10129aa0` |

**ObjType is a second registry, and it is the ObjectDef table's missing column.** Its initialiser
is at `0x1004c290`, the same two-store shape as the ObjectDef one, publishing 19 records at
`0x10129aa0`. The records are **8 bytes** (`{u32 id, char *name}`) not the ObjectDef 32-byte
layout, so reading them that way produced ids `1, 5, 9, 0xd, 0x11` and then garbage.

The nineteen categories:

> Material · Don't Use · Player · System · Debug · Flying Enemy · Ground Enemy · Ground Object ·
> Projectile · Item · Powerup · Special Effect · Damage Effect · Light Source · HUD · Attack ·
> Behavior · GUI · Root Behavior

**Every ObjectDef names one of them**, in the field at `+0x04` that
[OBJECT-MODEL.md](OBJECT-MODEL.md) previously reported as an unknown small int. All 397 classes
resolve, and the groupings are exact, not merely plausible: type 3 "Player" contains one
class and it is `Player`; type 14 "Light Source" contains four and they are precisely the four that
the exported `IsObjectLight` predicate tests; "Behavior" and "Root Behavior" split the behaviour
classes; the four material classes and `Footsteps` are the whole of "Material".

`RiotDllType` returns the constant `0x80000103` and nothing else; `RiotDllGetID` returns `1`.
Both are six bytes and neither reads anything, so they are compile-time answers, the handshake a
Riot Engine host uses to ask a module what it is and which one it is. `DllMain` is 17 bytes and
does one thing: it stores its first argument, the module handle, into a global at `0x10132688` and
returns 1.

## They confirm the ObjectDef id mapping

The three `IsObject…` predicates each take an ObjectDef class id and test it against a fixed set.
They are tiny, and they are named, so what they test is not open to interpretation:

```
IsObjectPortal      cmp ecx, 0x10108 ; setz al
IsObjectMoveNode    cmp ecx, 0x10140 ; setz al
IsObjectLight       cmp against 0x1000b, 0x1000c, 0x1002c, 0x1002d, and more
```

Under the id assignment [OBJECT-MODEL.md](OBJECT-MODEL.md) arrives at, those resolve to:

| | |
|---|---|
| `0x10108` | Portal |
| `0x10140` | Move Node Object |
| `0x1000b` `0x1000c` `0x1002c` `0x1002d` | Static Light, Dynamic Light, Static Spot Light, Dynamic Spot Light |

A function called `IsObjectLight` testing exactly the four classes whose names end in "Light", and
nothing else. `IsObjectPortal` testing exactly `Portal`. `IsObjectMoveNode` testing exactly
`Move Node Object`.

This matters because the ObjectDef record can be read with the id one field out, and it still
parses, dense ids, sane names, every field in a plausible place. The only thing that catches the
error is meaning. Three separate lines of evidence now agree on the same assignment: properties
named after their own target class, the table base the engine publishes in its own initialiser, and
these three named predicates. They were arrived at independently and they do not disagree.

## Why there are only eleven

Everything else the engine does is reached through the interface structures, not through
exported functions, so `GetObjectDefInterface` has no callers inside the rfl itself, the
host executable and the level editor call it, the DLL does not call itself. That is also why the
export table is worth so little and so much at once: eleven names is nearly nothing, but they name
the doors, and the vocabulary behind them, ObjectDef, ObjType, LandType, Message, is the
developers' own.

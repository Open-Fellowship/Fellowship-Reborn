# The object model is named in the binary

`Fellowship.rfl` carries a registry of every level object type the engine knows, by name, with a
full property schema attached. It is plain data in `.data`, so reading it needs no decompilation
and no disassembler - `decomp/tools/classdump.py` parses the image directly.

This matters beyond convenience. The decompilation work has been operating on the rule that
**every name is invented**, because the images carry no debug directory, no RTTI and no linker
map. For engine internals that still holds: `Vector3`, `Matrix`, `Handle` are all guesses that
describe behaviour. For **game object classes it is now false.** `Player`, `Nazgul`, `Balrog`,
`BarrowWight`, `FellBeast`, `OldManWillow`, `Tolkien NPC` and 43 `…Behavior` classes are
the developers' own names, and so is every one of the 4,262 property keys hanging off them.

## What is there

| | |
|---|---|
| records in the table | 397 |
| live classes | 218 |
| retired slots, named `Don't Use` | 179 |
| properties across all classes | 4,262 |
| largest class | `Player`, 166 properties in 32 groups |

The 179 retired slots are worth noticing on their own. They are not padding - each is a real
32-byte record with an id, kept in place so that ids never shift, and they cluster: `0x010075`
through `0x010100` is one unbroken run of 140. Something occupied that range and was removed
wholesale, and the ids around it - the `…Behavior` classes - say what kind of thing it was.

Full index: [generated/class-index.md](generated/class-index.md).
Machine-readable, everything: [generated/object-model.json](generated/object-model.json).

## The format

A 32-byte record per class, contiguous, starting at `0x1010F0A0`, 397 of them:

| offset | | |
|---|---|---|
| `+0x00` | id | `0x00010001` upward, dense, in table order |
| `+0x04` | ObjType | one of the nineteen categories in the ObjType registry - see [RFL-EXPORTS.md](RFL-EXPORTS.md) |
| `+0x08` | | small int, meaning unestablished. 0 in 189 of 397; range 0-135 |
| `+0x0c` | flags | `0x?00004??`, meaning unestablished |
| `+0x10` | name | `char *` |
| `+0x14` | properties | total across all groups |
| `+0x18` | groups | number of property groups |
| `+0x1c` | group array | pointer to `groups` pointers |

Neither the base nor the count is guessed. The engine publishes both, in an initialiser that is
exactly two stores followed by a return:

```
1004c210  c7 05 74281310 8d010000   mov dword [0x10132874], 0x18d          397 records
1004c21a  c7 05 78281310 a0f01010   mov dword [0x10132878], 0x1010f0a0     the table
1004c224  c3                        ret
```

with a getter at `0x1004c230` returning the address of that pair. This matters for more than
tidiness - see below.

**The id precedes the name rather than following it.** Read the record the other way round - which
is the obvious reading, since the name is the field that catches the eye - and the record still
parses, the ids are still dense, and every class reference in the whole table resolves to the class
*next door*. Nothing about the shape of the data reveals the error.

What reveals it is a property named after its own target. `Footsteps` must accept the class named
`Footsteps`; `RegionList` must accept `Map Region`; `HitReactions` must accept `Hit Reaction`;
`WaveBumps` must accept `Wave Bump`. Only one of the two boundaries satisfies all four. Under the
correct one, 84% of the 694 object references in the table resolve to a named class; the remaining
16% point at ids outside this table and are reported raw.

The engine's own initialiser then settles it independently: the base it publishes is `0x1010f0a0`,
which is the boundary the reference semantics force and not the one the eye picks out. Two
unrelated lines of evidence, same answer.

Each group is 12 bytes - name, count, pointer to `count` property records - and each property
record is 20 bytes:

| offset | | |
|---|---|---|
| `+0x00` | label | `char *`, the editor's display name |
| `+0x04` | key | `char *`, the serialisation name used in level data |
| `+0x08` | type | base type in the low 12 bits, a modifier in the high nibble |
| `+0x0c` | default | interpreted per type |
| `+0x10` | constraint | the enum's value names, or the class id a reference accepts |

Base types were identified from what the default and constraint fields hold, not guessed: floats
carry float bit patterns, colours carry `0x00RRGGBB`, channels default to `-1`, enums carry a
comma-separated value list, and every resource type defaults to the same pointer into the
zero-filled tail of `.data` - which is to say, unset. Of the high-nibble modifiers only `1` is
established, as "list"; every property carrying it is named as a plural. The others appear on
references and are reported raw rather than named.

## What it does not tell you

**There is no member offset in a property record.** The schema names the engine's data model; it
says nothing about the C++ object layout. It does not say whether a property is a real member of a
C++ class or an entry in a keyed bag, and nothing here distinguishes the two.

It also says nothing about methods. No vtable, no function pointers, no sizes. A class with 166
properties might be a large C++ object or a small one holding a serialised blob, and the registry
cannot tell you which.

So this is a vocabulary, not a layout. It means that when a decompiled function is found to touch
the player's health, there is a real name - `InitialHealth`, `MaxHealth`, `CriticalHealthPerc` - to
attach to it instead of an invented one. That is worth a great deal and it is not the same thing as
knowing where those fields live.

## The Player class

166 properties in 32 groups, the largest real class in the game. Full listing:
[generated/player-properties.md](generated/player-properties.md).

The group names are the most compact description of the player's capabilities that exists anywhere:

> Health · Melee Attack Positions · Incoming Messages · Health Regeneration · Hit Reactions ·
> Death · Basic Movement · Basic Movement Animations · Idle State Parameters · Jump/Fall ·
> Jump Related Animations · Ladder Climbing · Ledge Grabbing · Object Interaction ·
> Collision Detection · Item Interaction · Interaction Object Properties · Push/Pull ·
> Camera Options · Targeting Parameters · Meters · Combat Related · Rolling Animations ·
> The One Ring · Speech · Conversation distances · Facial Expressions · Head & Neck · Magic ·
> Team Information · PC Movement Animations · PC Controls Configuration

The first three groups - Health, Melee Attack Positions, Incoming Messages - have their descriptors
in a different pool from the other twenty-nine, and the same three appear on `NPC`, `Nazgul`,
`Balrog` and the rest. They are inherited from a shared base, and the group array splices the base
class's groups in ahead of the class's own. That is the one structural fact about inheritance the
registry does expose.

## Regenerating

```
python decomp/tools/classdump.py                          summary
python decomp/tools/classdump.py Player                   one class in full
python decomp/tools/classdump.py --tables documentation/generated
python decomp/tools/classdump.py --json documentation/generated/object-model.json
```

Everything under `generated/` comes out of the image; edit the tool, not the output.


## How property values are actually reached

**Settled: values are addressed by ordinal through a virtual accessor, not by member offset.** The
whole engine reads a property with one call shape:

```
MOV  ECX, [<owner> + <slot>]     the property-value block
MOV  EDX, [ECX]                  its vtable
PUSH -1                          element index; 0 for the list forms
PUSH <ordinal>                   the property's flat index in its class's schema
CALL [EDX + 8]                   -> void*, a pointer to the value
```

The ordinal is the property's position in the flat listing of its class - group by group, in table
order, exactly what `classdump.py` prints. Three owner slots for the block are attested:
`record + 0x08`, `gameobject + 0x14`, and one other structure's `+0x08`.

### How that was established

Not by reading the accessor - its implementation is not in the rfl. By four independent checks
against a schema the code never sees.

**One property, two different integers.** `0x10015a90` tests the class id and then fetches ordinal
`0x73` on the Player branch and `0x49` on the NPC-family branch. Player's ordinal 115 is `JawChan`;
NPC, Nazgul, Balrog, BarrowWight, FellBeast and Tolkien NPC all have `JawChan` at ordinal 73. Both
are type *channel*, defaulting to `none`. The function is a jaw-channel getter, and two unrelated
numbers name the same property in two different classes.

**Types match, in order.** `0x1002c1c0` reads ordinal 0 as a float, ordinal 1 as an int compared
against exactly 0 and 1, ordinal 2 as a boolean, and then sends ordinal 3 to the message interface -
destroying the object first if ordinal 2 was set. `Health Threshold Trigger` is
`HealthThreshold` float 50.0, `ThresholdMethod` enum {Below, Above}, `DestroyWhenSent` enum
{No, Yes}, `Message` message. Four ordinals, four types, and the behaviour matches the names.

**The same again, on a bigger class.** `0x100148c0` compares ordinals 3 and 4 as ints, 7, 8, 9 and
11 with `FCOMP`, and 5 and 6 as ints. In `Line of Dialog` those are `CameraPosition` and
`CameraTransition` (enums), `OverShoulderDistBack/Over`, `SideDistMult`, `TransitionSlerpSpeed`
(floats), and `Speaker`/`Listener` (object links). It compares exactly the camera properties of two
lines of dialogue and skips the animation and sound ones.

**A falsifiable ceiling, and it holds.** Scanning the rfl's `.text` for that call shape finds
**1,441 sites, with ordinals 0 through 162 and nothing above**. The largest class in the whole
ObjectDef table is `Player` at 166 properties, so the highest legal ordinal is 165. Had the pushed
integer been anything other than a schema ordinal there was no reason for it to respect a bound
derived from a table in a different section. It does. The distribution decays smoothly from 96
sites at ordinal 1 down to one or two above 0x60, which is what a schema-ordinal histogram should
look like and not what an enum or a flag word would.

The ceiling also identifies code by arithmetic. Only two classes have more than 140 properties, and
only `Player` has more than 154, so **ordinal 162 can only be Player code**: `HKFireAttackSpell`,
read at `0x100611cd` inside the 5,394-byte function at `0x10060c90`.

### What this means for the Player class

**Player is more tractable than it looked.** Its 166 properties are not 166 unknown member offsets
waiting to be recovered - they are ordinals 0 to 165 on one generic accessor, and the ObjectDef
table already gives every one of them a name, a type and a default. Recovering what a Player
property read *is* now amounts to reading the immediate that was pushed.

What that does **not** give is the Player object's runtime state, which is a separate thing from
its authored properties. Fields written directly to the Player subobject at `gameobject + 0xc8`
reach at least `+0x3b0`, so there are roughly 0x3b4 bytes of state that the schema says nothing
about. Both exist; they are not the same storage, and only the second has to be recovered by hand.

### Still open

* The accessor's implementation is in `Fellowship.exe`, not the rfl, so whether the engine backs it
  with a flat variant array, a typed struct plus an offset table, or a switch is invisible from
  here. From the rfl's side the addressing is ordinal-based either way.
* 28 of the 36-byte record's bytes are never touched by any caller examined. Confirmed so far:
  `+0x04` the ObjectDef class id, `+0x08` the property-value block. `+0x00` and `+0x0c` upward are
  unknown.
* Whether `gameobject + 0x14` is just a cached copy of `record + 0x08`. Every use is consistent
  with it; nothing proves it. The writer is likely in the exe's spawn path.

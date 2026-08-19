# The ordinal map

Every authored-property read in `Fellowship.rfl`, located and counted. Built by
`decomp/tools/ordmap.py`; the generated listing is
[generated/ordinal-map.md](generated/ordinal-map.md).

The engine reads a property with one instruction shape, described in
[OBJECT-MODEL.md](OBJECT-MODEL.md): push an element index, push the property's ordinal in its
class's schema, call the accessor at `[vtable+8]`. The ordinal is a fact you can read straight off
the instruction. Pair it with a class and it names a property, because
[generated/class-index.md](generated/class-index.md) lists every class's properties in exactly that
order.

| | |
|---|---|
| property reads found | 2,409 in 760 functions |
| reads outside any function | 0 |
| candidate sites rejected | 1 |
| reads that carry a class | 301 |
| functions resolving to exactly one class | 43 |
| functions fitting no single class | 10 |

## What is solid and what is not

**The ordinals are exact.** The scanner anchors on the `CALL [reg+8]` and takes the two nearest
preceding pushes, because the pushes are frequently not adjacent - `PUSH -1 / ADD EAX,0x14 /
PUSH 2` is common, and an earlier version of this tool that demanded `6A FF 6A xx` back to back
found barely half the sites. The element index is required to be `-1` or `0`, which is what stops
`PUSH -1 / PUSH -1` being misread as ordinal 255.

One site was rejected for an ordinal above any class's property count. That single rejection is
worth more than it looks: it is the whole falsification test still passing. If the pushed integer
were anything but a schema ordinal there would be no reason for 1,555 of them to respect a bound
set by a table in a different section.

**Attributing a class automatically does not work, and the reason is worth recording.** Three
approaches were tried and two of them produce confident, wrong, plausible-looking property names:

*Attributing by the class id compared in the function body* fails because the compared object and
the read object are usually different. `0x1002c1c0` is a threshold trigger: it tests whether the
object that crossed it is a `Player` - a genuine `CMP EDX,0x1000e` - and then reads its **own**
`HealthThreshold`, `ThresholdMethod` and `DestroyWhenSent`. Attributing its reads to `Player` names
ordinals 0, 1 and 2 as `InitialHealth`, `MaxHealth` and `Difficulty`. Every one of those is a real
Player property, the types are plausible, and all three are wrong.

*Attributing by type fingerprint* - matching the schema's declared types against whether the result
is dereferenced with `FLD` - works, but only when it is anchored rather than searched for. A first
attempt looked 24 bytes past the call, caught neighbouring floating-point code, and classified the
same ordinal in `0x100148c0` as float at one site and integer at another. Anchored to **exactly the
two bytes after the call** - `D9 00`, `FLD dword ptr [EAX]` - it is exact, and it is the single most
useful constraint the tool has.

So the tool applies two tests, both arithmetic rather than inference:

* **count** - a class must have more properties than the highest ordinal the function uses
* **type** - an ordinal loaded with `FLD` must be declared `float` in that class

Together they resolve 19 functions to exactly one class and put a class on 175 reads, against 15 for
the count test alone. The type test is what separates `Player` from `Control Input Names`: all 154
of the latter's properties are strings, and no string is loaded with `FLD`.

Their *failures* are informative too. Eight functions now fit **no** class, and they are the 4KB to
8KB ones. A function reading one ordinal as a float and another as an integer where no single class
declares both that way is reading properties of **several** classes - a dispatcher. The tool says so
rather than picking a winner.

**The ordinal ceiling** remains as a special case: `Player` has 166 properties and the next largest
has 154, so any ordinal above 154 is Player and nothing else, whatever the types say.

Everything the two tests do not settle is reported unattributed, with the shortlist, and left for a
person to read. That is most of it.

## The Player region

The shortlist turns out to be sharply informative anyway, because high ordinals cluster in address
space. Fifteen functions have ordinals that fit only `Player` or `Control Input Names`, and
**fourteen of them lie in one contiguous stretch**:

```
0x10055650 .. 0x100621a2
```

`Control Input Names` is a key-binding table read by settings screens, not by a 5,394-byte function
performing thirty property reads. Taking that region as Player's implementation is judgement rather
than proof, but it is well-supported judgement, and it is falsifiable: decompiling any function in
it and finding it reads `LeftWindowsKey` rather than `MaxMana` would refute it immediately.

| | |
|---|---|
| region | `0x10055650` - `0x100621a2` |
| functions | 102 |
| bytes | 35,923 |
| leaf functions | 23 |
| functions under 200 bytes | 63 |

That is the answer to "how big is the Player class", and it is a far smaller number than the
property count suggested. **35,923 bytes in 102 functions**, of which 63 are under 200 bytes -
comfortably worker-sized, and the same size class as everything matched so far. The three functions
the ceiling names outright are `0x1005a1c0` (704 bytes, ordinal 165), `0x1005ddc0` (1,186 bytes,
ordinal 159) and `0x10060c90` (5,394 bytes, thirty reads including ordinal 165).

The one outlier, `0x10015a90` at 144 bytes, sits far outside the region and reads ordinals 115 and
73. It is a shared accessor - the same function serves Player and the NPC family, which is exactly
why its two ordinals differ - and it is the reason per-function class attribution had to be
abandoned.

### What decompiling six of them showed

Six functions in the region were matched byte for byte. The result is more interesting than a clean
confirmation, and it sharpens what the region claim is actually worth.

| function | | |
|---|---|---|
| `0x10057b60` | `Player::GetMaxPurity` | **Player, proven** - ordinal 113, `FLD` |
| `0x1005c4e0` | `Player::GetMaxMana` | **Player, proven** - ordinal 139, `FLD` |
| `0x10060210` | `Player::ClearUpperBodyPitch` | **Player, all but forced** - ordinal 129 tested against -1 |
| `0x10057030` | `PropertyOwner::GetScaledPercentage` | undetermined, three readings survive |
| `0x1005ad00` | `GetRightPropertiesDef` | **not Player** |
| `0x10058360` | `PropertyObject::ObjectRefPropertyChanged` | undetermined |

The two proven ones are proven by the ordinal, not by the address. Ordinal 139 is the fork this
document named in advance: `Control Input Names` has `LeftWindowsKey` there, `Player` has `MaxMana`,
and the function ends `FLD dword ptr [EAX]`. A string is not loaded with a float instruction. The
prediction was made before the function was decompiled and it held.

`0x1005ad00` is the one that matters. Its property value is dereferenced as a signed integer and
used as a bounds-checked index into the ObjectDef entry list. `Player` ordinal 34 is `JumpVerVel`,
a float defaulting to 12000.0, whose bit pattern is 1,178,304,512 - which fails that bounds check on
every possible input. **Were it Player, the function would be unconditionally dead code.** It is an
object-reference property; the best candidates are `Ranged Weapon` and `Melee Weapon`, whose ordinal
34 is `RightProperties`, pointing at `Player Weapon Properties` - in a group literally named
"Player", which is the likeliest reason it sits in this address range at all.

So: **residence in the region is not evidence.** The region is where to look; the ordinal and its
type are what decide. A function there may operate on some other object entirely, and
`0x1005ad00` does. `0x10057030` narrowed from 65 candidate classes to three readings on the strength
of a x0.01 only being meaningful on a 0-100 percentage - `Transparency` across 42 GUI classes,
`MaxSlowDown` on `HitReactBehavior`, or `CriticalHealthPerc` on `Player` - and could not be settled
further.

Files under `decomp/src/player/` hold only the functions whose class is established. The rest live
under `objectdef/`, because a directory should not assert what its contents decline to.

## The reads that used to be outside any function

Roughly a third of all property reads sat in code Ghidra's auto-analysis never turned into
functions, and were dropped. That was the same gap already recorded for the exe in
[EXE-PATCH-SITES.md](EXE-PATCH-SITES.md), where eleven plugin patch sites landed in unanalysed
bytes - and it mattered more than a third: the reads with the least evidence about them were
precisely the ones getting no function context to attribute them with.

It is closed. `ExportFunctions.java` now has a `census` mode that accounts for every byte of every
executable section and records each run of loose code, along with whether it begins exactly where a
function body ends. `ordmap.py` feeds those runs back in: a run that continues a body is merged into
it - which is also the better attribution, since the class id being tested is usually in the head
rather than the tail - and a run standing on its own becomes a function in its own right. **Reads
outside any function are now zero**, and the total went from 1,555 to 2,323.

The size of the gap is in [`decomp/census.tsv`](../decomp/census.tsv): 220,016 bytes across 1,932
runs in the rfl, a fifth of `.text`.

**The second undercount was in the scanner itself**, and is now mostly closed too. It required the
element index to be pushed as an immediate - `6A FF` or `6A 00`. At `0x10058360` VC6 had a zero live
in EBP and emitted `PUSH EBP` instead, so that site was invisible. The scanner now also accepts one
specific shape: the ordinal push immediately preceded by a one-byte `PUSH r32`, which is a list read
at a computed index. That is 86 more reads, taking the total to 2,409.

It is deliberately that narrow. Accepting "one immediate push before the call" in general matches
far too much, and these sites do give up the element-index test, so it is worth being explicit about
what still constrains them: the ordinal ceiling. No class has more than 166 properties, so a misread
shows up as an impossible ordinal, and `ordmap.py --check` asserts that none appears. That is the
test the whole reading rests on, and it still passes.


## The exe barely reads properties at all

Running the same scan over `Fellowship.exe` finds **19 property reads in total**, against 2,409 in
the rfl. Whatever the exe is, it is not where authored game data is consumed. That is consistent
with everything else found so far - the ObjectDef table, the class predicates and the accessor are
all in the rfl, and the exe holds the renderer, the input and the window. The game logic lives in
the engine module, and so does the Player.

## Using it

```
python decomp/tools/ordmap.py                       summary, and the narrowest shortlists
python decomp/tools/ordmap.py Player                every function that could be reading Player
python decomp/tools/ordmap.py --function 1005c500   one function's reads
python decomp/tools/ordmap.py --tables documentation/generated
python decomp/tools/ordmap.py --image exe           the same over Fellowship.exe
```

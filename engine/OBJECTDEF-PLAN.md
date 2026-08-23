> **Carried out.** This plan was executed; `GetObjectDefInterface` is served from
> `objectdef/objectdef_table.c` and verified against the retail image. Kept as the reasoning that
> led there, not as outstanding work. The result is documented in [proxy/README.md](proxy/README.md).

# Plan: implement `GetObjectDefInterface` in the proxy

The first *system* the engine layer would own, beyond the two pure predicates it owns now.

## Why this one

* **The data already exists.** `decomp/tools/classdump.py` extracts all 397 classes and 4,262
  properties; `decomp/tools/odudump.py` proves the ordering is right by decoding every authored
  value in the game against it. Nothing has to be reverse-engineered first.
* **It is the foundation.** Every other engine subsystem reads through this registry. Owning it
  gives later work somewhere to stand.
* **It fails visibly.** The game finds its classes at startup or it does not.
* **It is verifiable to the same standard as the decompilation.** See below: this is the part that
  makes it worth doing properly, not approximately.

## What the interface actually is

Fully established, and smaller than it sounds:

```
GetObjectDefInterface()   ->  &g_objectDefInterface

g_objectDefInterface  =  { u32 count; ObjectDef *table; }      at 0x10132874
```

Written once by an initialiser at `0x1004c210` (two stores, `count = 0x18d` and
`table = 0x1010f0a0`) and read by the six-byte getter at `0x1004c230`. **Those two globals are
referenced by exactly three instructions in the whole engine**, the two stores and the getter's
load. Nothing else touches them.

The table itself lives in `.data` but is **fully initialised on disk**: all 397 records, every
pointer resolved at link time. There is no runtime construction to reproduce. This is static data
behind a getter, so it is a tractable first system and not a rewrite.

### The structures to emit

```
ObjectDef, 32 bytes                     PropertyGroup, 12 bytes
  +0x00  u32    id                        +0x00  char *name
  +0x04  u32    ObjType                   +0x04  u32    count
  +0x08  u32    ** unestablished **       +0x08  Property *properties
  +0x0c  u32    flags  ** unestablished **
  +0x10  char  *name                     Property, 20 bytes
  +0x14  u32    property count             +0x00  char *label      editor display name
  +0x18  u32    group count                +0x04  char *key        serialisation name
  +0x1c  PropertyGroup **groups            +0x08  u32   type
                                           +0x0c  u32   default
                                           +0x10  u32   constraint  enum names, or accepted class id
```

Note the group array is a table of **pointers** to groups, not groups inline, and that a class's
first groups are frequently shared with other classes, `Player`, `NPC`, `Nazgul` and the rest all
point at the same three base-class groups. A generator that emits each group once and references it
is reproducing the original's structure; one that copies them is not.

## How it gets verified

**Byte-compare the generated table against the retail image**, with pointer fields masked, exactly
the technique `decomp/tools/matchtool.py` uses for code, applied to data. Every non-pointer field
(id, ObjType, counts, type codes, defaults, constraints, flags) must match the retail `.data`
verbatim, and the pointer fields must resolve to strings and structures that match in turn.

That makes this checkable to the same standard as a matched function, not "the game seems to
start", and it means the two unestablished fields below get carried across correctly whether or not
anyone knows what they mean.

## Steps

1. **`classdump.py --emit-c`**: generate the table as C: strings, property arrays, group records,
   the group pointer arrays, the 397 ObjectDefs, and the interface struct. Deduplicate the shared
   groups. Mechanical; the reader already has every field.
2. **A verifier** that compares the generated structures against the retail image field by field
   with pointers masked, and refuses on any mismatch.
3. **Implement the export** in the proxy: return `&g_objectDefInterface` instead of forwarding, and
   delete the forwarding body. One line in `proxy.c`, per the file's existing pattern.
4. **Run.** The failure mode to expect is not a crash but the host reporting `RFL initialization
   fail`, or objects failing to resolve their classes.

## Unknowns, and what to do about each

**`ObjectDef+0x08`**, meaning unestablished. Range 0-135, zero in 189 of 397 classes, equal to
neither the property count nor the group count. Emit the retail value verbatim; the verifier proves
it was carried across. It does not need to be understood to be reproduced.

**`ObjectDef+0x0c`, the flags**, same treatment. Values look like `0x?00004??`.

**Whether any consumer writes to the records.** The engine does not; the table is in `.data`, but
the only three instructions that touch the interface globals are the two stores and the getter.
Whether the host executable or the level editor mutates a record through the returned pointer is
**not established**. If they do, a table in read-only memory would fault; emitting it as writable
data costs nothing and avoids the question.

**The constraint field on 16% of object references** names a class id that is not in this table.
Those resolve to nothing today and would resolve to nothing after this change, which is the correct
behaviour, but it means there is a second class-id space somewhere that has never been found.

## What this does not do

It does not implement any *behaviour*. The registry is a lookup table; the code that walks it,
creates objects from it and reads properties through it all stays in the retail module and keeps
being reached by forwarding. This is one system, chosen because it is self-contained, and the
temptation to follow the first consumer into the engine should be resisted until it works.

## Effort

The generator and verifier are the bulk of it and are both mechanical. The risk is concentrated in
one place: whether a faithful table is enough, or whether some consumer depends on the retail
table's *addresses*, not its contents. That is unknowable in advance and cheap to test, which
is the right shape for a first system.

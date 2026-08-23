# Branch notes: `experimental/engine-layer`

**This branch is a snapshot, not a proposal.** It exists so the work is not sitting on one
machine. Nothing here is asking to be merged, and the main line of the project (`runtime/`,
patching the retail engine from outside) is unaffected by all of it.

Read this before reading anything else in `decomp/` or `engine/`. The individual documents are
accurate about their own subject and say nothing about how far the whole thing has got, which is
the question most people will actually have.

## The short version

The game runs through a DLL we wrote. Everything it *does* is still the 2002 code.

That sentence is the honest summary of the branch. What was built is a seam and the tooling
around it, plus every piece of static data the engine publishes. What was not built is any
behaviour.

## What is actually here

### `engine/`: the seam

A drop-in `Fellowship.rfl` that exports the same eleven names as the retail module, loads the
original renamed to `Fellowship.orig.rfl`, and forwards to it. Installed on a retail copy, the
game plays normally. That is the whole point of it: a function moves from forwarded to ours by
changing one function body, and the game keeps running the whole way.

Six of the eleven exports are ours:

| | |
|---|---|
| `IsObjectPortal`, `IsObjectMoveNode` | 16 bytes each, byte-matched against the retail code |
| `GetObjectDefInterface` | 397 classes, 494 property groups, 4,262 properties |
| `GetObjTypeInterface` | 19 categories |
| `GetLandTypeInterface` | 192 land types |
| `GetMessageInterface` | 54 messages |

The four registries are **generated from the retail image and verified field by field against
it**, 9,334 non-pointer fields, all identical, with pointer fields masked and then followed.
`python decomp/tools/objdefgen.py --verify engine/objectdef` re-runs that.

Every reimplemented system has an off switch in `fellowship_reborn_engine.ini` beside the DLL, so a
regression can be attributed in one sitting without a rebuild. That has already earned itself once,
on a fault that turned out not to be ours (a missing `LevelList.txt`).

**Be clear about what a registry is.** It is data. Each one is published by a function that is two
stores and a return, so a faithful table *is* the implementation and there is no behaviour to
reproduce. Serving four of them is real work and it is not engine work.

Still forwarded, and correctly so: `GetBaseRFLInterface` returns a C++ object with a 66-entry
vtable, which is where the engine actually lives.

### `decomp/`: the verifier and the tooling

Source that, compiled by VC++ 6.0 with the Processor Pack, reproduces the original's bytes
exactly. **58 of 69 manifest entries match.** This is not the deliverable; it is how a claim about
the engine gets proved before it is written properly.

Also here, and more useful day to day:

* `tools/classdump.py`: the class registry, straight out of the image
* `tools/odudump.py`: authored property values out of `.odu` databases, cross-checked against the
  engine's own embedded schema across 5,838 records
* `tools/ordmap.py`: every property read in an image, 2,409 of them, attributed where the evidence
  supports it and left unattributed where it does not
* `tools/corpus.py`: every function in both images, flat and greppable
* `tools/ExportFunctions.java`: the Ghidra side, including the `census` mode below

### `documentation/`

`OBJECT-MODEL.md`, `ORDINAL-MAP.md`, `TOOLCHAIN.md`, `MATCHING.md`, `RFL-EXPORTS.md`,
`EXE-PATCH-SITES.md`, and generated tables under `generated/`.

## Three corrections made on this branch

Worth recording because each was a number the project had been quoting, and each was wrong in the
direction that flattered us.

**The coverage denominator was 32% too small.** Summing what Ghidra put inside a function body is
not the size of an image's code. In an optimised C++ image a great deal is reached only through a
vtable or a jump table, so nothing calls it at an address the analyser can follow and it never
becomes a function. In the rfl that is 220,016 bytes across 1,932 runs, a fifth of `.text`. The
whole job is **12,040 functions and 1.91 MB**, not 9,313 and 1.5 MB.

**695 function bodies stop early**, usually at a mid-body `INT3`, so a size taken from Ghidra for
one of them is short and a "match" against it would be a match against part of a function. No entry
in `manifest.tsv` is affected. That was checked, not assumed.

**768 property reads were being dropped**, precisely the ones in the loose code above. Feeding the
census's orphan runs back into `ordmap.py` took that to zero and the total from 1,555 to 2,409.

My own first cut of the census had a bug of the same shape, jumping to `getBody().getMaxAddress()`
swallows the hole in a discontiguous body, which showed up as a disagreement with the independent
export. It now agrees exactly with the export on both counts for both images, which is the only
reason to trust it.

## Honest scale

Measured, not estimated:

| | |
|---|---|
| `Fellowship.rfl` | 6,199 functions, 914 KB of code |
| Polymorphic classes | 244, with 1,345 distinct virtual functions |
| Translation units | 234 C++ objects in the Rich header, Surreal's source-file count |
| Host↔engine surface | 11 exports + 66 vtable slots = 77 entry points |
| Behaviour replaced so far | **32 bytes** |

The 77 is the number that makes this tractable: we owe the exe those behaviours, not Surreal's
internal decomposition. Everything beneath is ours to structure however we like.

## If someone picks this up

The next milestone that would mean anything is **the property accessor**. There are 2,409 read
sites going through one function shape, and all four registries the data lives in are already ours.
Implementing it behaviourally would give us the entire authored-data path end to end, it is
bounded, and it is the most-exercised code in the engine so it self-tests immediately. It would be
the first thing on this branch that actually does something.

Two things that will waste your time if you do not know them:

* `decomp/export/` is gitignored and must be regenerated: see `decomp/README.md`. It is 99 MB.
* The corpus only knows about **functions**, and a fifth of `.text` is not in one. A search for
  callers that finds nothing may mean the caller is in that code. And an address with no callers
  may be the body behind an incremental-link thunk: the linker emits a five-byte `jmp` and
  everything calls *that*, five bytes earlier. Both of these cost me an hour on this branch.

## What was not touched

`runtime/` is unchanged except for one thing. `module_watch.c` now resolves the engine module as
`Fellowship.orig.rfl` if it exists and `Fellowship.rfl` otherwise, so plugins find the real engine
whether or not the proxy is installed. Without it, installing the proxy silently broke every plugin
that hooks the rfl: `text_scaling` went from seven hooks to zero.

That change is backwards compatible and is arguably worth having on `development` regardless of
what happens to the rest of this branch.

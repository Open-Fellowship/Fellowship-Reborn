# engine

Replacing the Riot Engine module, so the game can eventually run without the 2002 binary.

**Started.** [`proxy/`](proxy/README.md) is a drop-in `Fellowship.rfl` that forwards every call to
the retail engine. It changes nothing observable, which is the point: it establishes the seam, and
from here a function moves from forwarded to reimplemented one at a time with the game running
throughout.

## The approach, and why it changed

This layer was originally going to wait for `decomp/` to prove the engine byte for byte. That is
still the most rigorous way to know a reading is right, and it stays in use — but it is not a route
to a playable game. After a concentrated effort `decomp/` stands at 58 matched functions of roughly
9,300, about 0.3%. Byte-matching is a *proof technique*, not the deliverable.

So this layer is a **behavioural reimplementation**: code that does what the engine does, verified
against the retail module by running it, not by comparing bytes. The model is
[OpenJones3D](https://github.com/smlu/OpenJones3D), which reached 87% of a comparable engine by
starting with thunks into the original and replacing them incrementally.

`decomp/` continues, but as a verifier rather than a producer. When a function's behaviour is
subtle or load-bearing, matching it byte for byte is still the cheapest way to be sure — and it has
repeatedly earned that. In one session it caught a calling convention that was `__thiscall` and not
`__stdcall`, a return type that was not `void`, a compiler flag missing from the whole project, a
member that had to be `volatile`, and three separate cases where properties were attributed to the
wrong class. **Every one of those would have compiled cleanly and run "fine" in a behavioural
reimplementation, and been quietly wrong.**

## What this layer inherits

Not starting from nothing, which is why the estimate is years rather than a decade:

* **The object model.** All 397 game object classes and their 4,262 properties, by the developers'
  own names, with types and defaults — see [OBJECT-MODEL.md](../documentation/OBJECT-MODEL.md).
* **The property mechanism.** Authored data is addressed by schema ordinal through one virtual
  accessor; [ORDINAL-MAP.md](../documentation/ORDINAL-MAP.md) locates all 2,323 reads in the rfl.
* **Real names for the engine's own entry points**, from the rfl's export table —
  [RFL-EXPORTS.md](../documentation/RFL-EXPORTS.md).
* **58 functions verified byte for byte**, listed in `decomp/manifest.tsv`. These seed the
  reimplementation rather than being discarded.
* **The data formats**, from the Blender importer: SRSC archives, models, textures, skeletons,
  animations, terrain and object placements.
* **Working hook infrastructure** in `runtime/`, a loader and 24 plugins that already patch the
  retail engine in place.

## Layout

    proxy/      the drop-in Fellowship.rfl and its forwarding table

Modules are added beside `proxy/` as subsystems come across, named for what they are rather than
for where they sit in the retail image. Add one when there is something to put in it, not in
anticipation.

## Relationship to runtime/

`runtime/` patches the retail engine from outside and is what makes the game playable today. This
layer replaces the engine from inside. They are independent by design — nothing here links against
`runtime/`'s shared library — because the two have opposite lifetimes: `runtime/` becomes unnecessary
exactly as this layer becomes complete.

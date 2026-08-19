# Byte-for-byte matching

The goal: C source that, compiled by the original toolchain, produces the exact bytes the
shipped image holds. Not equivalent code — the same code generation, instruction for instruction.

`TOOLCHAIN.md` establishes which compiler. This is the harness that proves a candidate matches.
The tool is `decomp/tools/matchtool.py`, driven by `decomp/build.py`; `decomp/README.md` covers
the day-to-day workflow.

## Why you cannot just diff

A freshly compiled `.obj` and the shipped image disagree everywhere the linker touched, even
when the code generation is identical:

```
in the .obj                        in the image
e8 00 00 00 00   call <unresolved> e8 1b 00 00 00   call 0x401030
a3 00 00 00 00   mov [?], eax      a3 00 b0 40 00   mov [0x40b000], eax
```

Both fields are placeholders the linker fills in, each accompanied by a relocation record. Diff
raw and every call site, every global and every string address reads as a mismatch, drowning the
real differences.

So `matchtool.py` reads the `.obj`'s relocation table and blanks those operand fields on **both**
sides before comparing. What survives is pure code generation: instruction selection, register
allocation, scheduling, stack layout. That is what the compiler and its switches decide, and
that is what has to match.

The best first targets are small leaf functions with no calls and no float constants. They carry
no relocations at all, so nothing is masked and a match means every byte agreed.

## Using it

```
matchtool.py obj     <objfile>                    list the functions in an object
matchtool.py obj     <objfile> <symbol>           one function's bytes and relocations
matchtool.py image   <pe> <va> <size>             bytes at a virtual address
matchtool.py compare <pe> <va> <objfile> <symbol> the real thing
```

`compare` exits 0 on a match and 1 on a mismatch, so it drops straight into a loop over
candidate compiler switches.

A leading underscore is tried automatically, so `leaf_add` finds the `_leaf_add` that cdecl
actually emits. C++ names have to be given decorated — `matchtool.py obj <objfile>` with no
symbol lists what is really in there.

A mismatch prints an annotated hex diff, `..` for masked operands and `^^` under differing bytes:

```
  0000  orig  8b 4c 24 08 8b 44 24 04 8d 04
        ours  6b 44 24 08 03 03 44 24 04 c3
              ^^ ^^       ^^ ^^ ^^ ^^ ^^ ^^
```

That one is `/O2` against `/O1`: `lea` versus `imul` for the same `b * 3`. Flags show up as
plainly as this, which is the point — the Rich header cannot tell us the optimisation switches,
and diffing is how they get recovered.

## It was validated before it was trusted

COFF is the same format in VC6 and in VS2026, so the object-parsing half was proven on the
toolchain already installed, before VC6 was available:

| check | result |
|---|---|
| relocation offsets, hand-verified against the disassembly | `DIR32 +0x06`, `REL32 +0x0b`, `DIR32 +0x11`, all exact |
| a linked function vs its own unlinked `.obj`, no relocations | MATCH, 14 bytes |
| a linked function vs its own unlinked `.obj`, 3 relocations | MATCH, 25 bytes, 12 masked |
| `/O2` image vs `/O1` object | MISMATCH, 8 of 10 bytes |

The third row is the one that matters: the image held resolved addresses where the object held
zeros, and it still matched once masked. The fourth exists because a comparison tool that only
ever says MATCH is worse than none.

Worth re-running after any edit to the parser. These were run by hand against objects built with
`cl /nologo /c /O2 /Gy`.

That validation was done before VC6 was available, and it paid for itself. When the real
toolchain arrived, three of the first four functions matched immediately — which was believable
precisely because the harness had already been shown to say MISMATCH when it should.

## Status

Working, and proven against the real thing. `Fellowship.rfl` is the target: no SecuROM wrapper,
a genuine 2002 timestamp, and the bulk of the game logic.

| | |
|---|---|
| Toolchain | VC++ 6.0 + Processor Pack, assembled portable. `TOOLCHAIN.md` |
| Switches | `/O2 /Gy`, recovered by sweeping |
| Matched | 4 of 4 in `decomp/src/vector3.cpp`. `FIRST-TARGETS.md` |
| Harness | `decomp/build.py`, manifest-driven. `decomp/README.md` |

Function boundaries still come from Ghidra, and lengths must exclude the trailing padding —
which is `90` in these images, not `cc`.


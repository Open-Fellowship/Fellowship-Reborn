# decomp

Source that, compiled by the original toolchain, produces the exact bytes the shipped game
holds. Not equivalent code: the same code generation, instruction for instruction.

**This is not the engine itself.** It is how the engine gets verified. A subsystem is decompiled here
and matched byte for byte, which proves it is understood completely, not approximately,
and only then is it written properly in `engine/`. Matching is the evidence; `engine/` is the
deliverable.

## Progress, and how it is measured

```
python build.py --status
```

Progress is **computed from `manifest.tsv`, never written into source files.** A count at the top
of a `.cpp` goes stale the moment anything changes, duplicates what the manifest already knows,
and puts two people in contention over a line neither of them owns. The manifest is the single
record; everything else is derived from it.

Two numbers get reported, because they answer different questions and only one of them is honest
on its own:

| | |
|---|---|
| **by source file** | how much of what we have *attempted* is finished. Trends to 100% by construction, the manifest only contains functions somebody chose to take on. Useful for spotting a file with something still open in it, useless as a measure of the project |
| **coverage of each image** | how much of the binary is actually accounted for, against `census.tsv`. This is the real number |

`census.tsv` holds the denominators, produced by running `ExportFunctions.java` in `census` mode
over a whole image:

| | in a function | | loose code | | truncated |
|---|---:|---:|---:|---:|---:|
| | functions | bytes | functions | bytes | bytes |
| `Fellowship.rfl` | 4,962 | 694,205 | 1,237 | 220,016 | 14,291 |
| `Fellowship.exe` | 4,351 | 811,612 | 1,490 | 183,359 | 24,369 |

Those are the whole job: **12,040 functions and 1.91 MB of code** if every one were matched.

### Why there are two columns and not one

Summing what Ghidra put inside a function body is not the size of the image's code, and for a long
time this file assumed it was. In an optimised C++ image a great deal of code is reached only
through a vtable or a jump table, so nothing calls it at an address the analyser can follow and it
never becomes a function at all. In the rfl that is **220,016 bytes across 1,932 runs**, a fifth of
`.text`, and 32% on top of the body total.

Reporting only the body total therefore overstated our coverage by about a third. The correction is
not cosmetic: the whole purpose of this number is to be the one figure in the project that cannot
flatter us, and it was flattering us.

A little under a tenth of that loose code is a different problem. `truncated` counts runs that begin
exactly where a function body ends, which means the body stopped early, usually at a mid-body
`INT3`. Those bytes belong to the function in front of them, so a size taken from Ghidra for such a
function is short, and a "match" against a short size would be a match against part of a function.
**No entry in `manifest.tsv` is currently affected**, which was checked, not assumed.

The exe's `.cms_t` (SecuROM, 184,320 bytes) holds no code at rest, the census finds it entirely
zero-filled, so it contributes nothing to any figure here.

Coverage is reported by function count *and* by bytes, and the gap between the two is worth
watching. At the time of writing it is 0.66% of functions but 0.39% of bytes, because everything
matched so far has been small. Byte coverage lagging function coverage is the signal that the easy
ones are being taken first, expected, and not a problem, but not something to let a flattering
percentage hide either.

## The corpus

Every function in an image, exported and folded into three flat files, so a question about the
engine is a grep, not an afternoon:

```
python decomp/tools/corpus.py --build rfl        # from export/rfl-all
python decomp/tools/corpus.py "GetPropertyFloat" # search the decompilation
python decomp/tools/corpus.py --asm "fdivr"      # search the disassembly
python decomp/tools/corpus.py --calls 1004c210   # every caller of an address
```

`export/` is gitignored and regenerated, never committed: it is 28 MB of JSON and 17 MB of
flat files, all derived from the images. The recipe is in `tools/ExportFunctions.java`.

Two cautions, both of which have already bitten. The corpus only knows about functions, so a
`--calls` that finds nothing may mean the caller is in the loose code above, not that nothing
calls it. And an address that looks uncalled may simply be the body behind an incremental-link
thunk: the linker emits a five-byte `jmp` and everything calls *that*, five bytes earlier.

## Status

| | matched | |
|---|---|---|
| `math\vector3.cpp` | **22 / 22** | the whole `Vector3` class, in both images |
| `math\matrix.cpp` | 5 / 6 | a 3x3 matrix at offset 0x0c; `operator*` is 4 bytes of 82 away |
| `math\matrix3.cpp` | **1 / 1** | a *different* 3x3 class, at offset 0 |
| `core\array.cpp` | **1 / 1** | apply a `__thiscall` method across a strided array |
| `core\handle.cpp` | **1 / 1** | resolve a one-based handle through a global registry |
| `core\record_state.cpp` | **1 / 1** | a constructor |
| `core\value_filter.cpp` | **1 / 1** | a virtual predicate |
| `level\levellist.cpp` | 1 / 2 | the `LevelList.txt` machinery; `Reserve` differs only by register choice |

**33 of 35**, across both binaries.

The five `core\` and `matrix3` entries were done as one round of five leaf functions, all five
matched, and every claim was re-verified here before it went into the manifest. What that round
taught is below.

### The exe and the rfl share source

Every function matched in the rfl was found byte-identical in `Fellowship.exe` as well. That is
to know before organising anything: source is **not** split by image, because one file
legitimately reproduces functions in both. The `image` column in `manifest.tsv` is per function,
not per file, which is what makes that work, and it means a function matched once can be claimed
twice.

Eleven of the entries above cost nothing beyond locating them: search the exe for the bytes of an
already-matched rfl function, and where the hit is unique, that is the same function at a new
address. Do that after any round.

Two caveats on that trick. A raw search only finds **relocation-free** functions:
`Vector3::operator/=` loads the `1.0f` constant from `.rdata` and its address differs between the
images, so the bytes differ even though the code is identical. Masking that one operand and
searching again found it immediately, at `0x00449600`, the same masking `matchtool.py` does, just
applied to a search. Worth reaching for whenever a function you expect to find does not turn up.

`Vector3::Normalized` is absent from the exe even masked, so it really is not there, unused in
that binary, or inlined into its callers.

And a short generic body can appear more than once: `Vector3::operator=` is a 25-byte three-dword
copy occurring four times in the exe, so it identifies nothing and was left out and not
guessed at.

`levellist.cpp` is the first file with real relocations (string addresses, a call to the CRT,
and five Win32 imports) so the relocation masking is now proven against the game itself and not
only against a synthetic test.

Six of those ten matched on the first attempt, once the two conventions the class follows were
known. They are worth stating plainly, because they generalise to everything else this codebase
does with floats:

* **A function returning a `Vector3` by value uses a named local, assigned in reverse member
  order**, `r.z`, then `r.y`, then `r.x`. This is forced by the x87 stack: all three components
  are computed before any is stored, and `FSTP` pops from the top, so storing x first means
  pushing x last.
* **The class declares an explicit copy constructor.** Nothing calls it out of line and it
  appears nowhere in the original binary, but without it the compiler builds a temporary and
  copies instead of constructing into the caller's return buffer.

Two more came out of the matrix class, and both are about reading the code generation to recover
something the stack layout cannot tell you:

* **Integer `MOV`s where you expected the FPU mean the destination is a reference, not a return
  value.** A named local is the return buffer under RVO, nothing can alias it, and the compiler
  keeps the values in FPU registers. A reference it cannot prove unaliased forces
  memory-to-memory moves, and a plain float copy then becomes an integer move. `Matrix::GetColumn`
  is `void GetColumn(Vector3 &out, int i)`, and only the instruction selection says so: a hidden
  return pointer at `[esp+4]` and an out-parameter at `[esp+4]` have identical stack layouts.
* **`a + b + c` lets the compiler reassociate; separate `+=` statements do not.** VC6 reassociates
  float addition at `/O2`, so a single expression gives it a free choice and it does not always
  pick what the original picked. Accumulating with `+=` makes each step a sequence point and takes
  the choice away. This moved `Matrix::operator*` from 35 bytes differing to 4.

`documentation/FIRST-TARGETS.md` has how those two were found, and the fourteen theories that
were wrong on the way.

## Layout

```
manifest.tsv       one line per function: image, address, size, source, expected result, symbol
build.py           compiles every source and checks every function - the integration check
try.py             compiles one source and checks one function - the working loop
CONVENTIONS.md     what the original compiler does, established by matching
src\
    math\          vector3.h/.cpp, matrix.h/.cpp
    level\         levellist.cpp
tools\
    matchtool.py      compares a compiled function against the original, relocation-aware
    richdump.py       reads a PE's Rich header - which compiler built it, and which build
    ExportFunctions.java   Ghidra headless: dumps a range of functions to JSON
build\
    all\           objects from build.py, mirroring src\
    obj\           objects from try.py, mirroring src\ - kept apart so an integration
                   run cannot read an object another is part-way through writing
```

Paths in `manifest.tsv` are relative to `src\` and may name a subdirectory. Both build scripts
mirror that structure under `build\`, so two modules are free to hold a same-named file.

### How modules are grouped, and how much that means

**The original layout is not recoverable.** All four things that could have carried it are gone:
the rfl has no debug directory at all, the exe's CODEVIEW record is 60 zero bytes, there are no
`__FILE__` strings, and there is no RTTI because `/GR` was off. No linker map shipped. So we do
not know what the files were called, what the classes were called, or which functions shared a
file.

What the Rich header does give is a **count**: the rfl was linked from roughly 246 C++ objects,
120 C objects and 30 MASM objects. Around 396 translation units. That is a useful check on any
reconstruction, a tree with forty files, or four thousand, is wrong.

So modules here are grouped by **inferred subsystem**, from two kinds of evidence:

* **address clustering**: the linker keeps an object's functions together, so a run of adjacent
  functions is usually one translation unit. It is a hint, not proof: ten `Vector3` methods sit
  together at `0x10002230` but two more are away at `0x100044e0`, which is either linker ordering
  or evidence they lived in a different file
* **shared types**: functions taking the same `this` layout belong together
* **same-`this` call chains**: a `__thiscall` method restores its own `this` with
  `MOV ECX,ESI` immediately before calling another method on the same object. That proves
  the callee is invoked on an object of the caller's class, so a class established for one
  function carries to the other. `decomp\tools\ordmap.py` does this, and
  `--check` guards it against over-claiming
* **cross-function codegen dependencies**: proof, not a hint; see below

Add a module when there is evidence of a distinct subsystem, not in anticipation of one. Empty
folders named for subsystems we have guessed at would be inventing structure we cannot justify.

**This is almost always organisation, not constraint (but not quite always, and the
exception is what makes file grouping recoverable at all.** With `/Gy` every function is its
own COMDAT, so which `.cpp` a function lives in usually has no effect on its bytes) which is why `math\vector3.cpp` matches
12 of 12 while certainly not being the original file, under a name we made up. Layout is
organisation, not constraint.

The exception is when the compiler needs to know what a callee DOES, not merely how to
call it. `Player::ShouldRegenerate` at `0x100570d0` compares two float-returning calls and
keeps the first result on the x87 stack across the second, ending in `FCOMPP`. VC6 only
does that when the callee is **defined above the caller in the same translation unit**.
With `Player::GetCriticalHealth` merely declared it spills to a stack slot and compares
against memory instead, 76 bytes instead of 67, everything after the first call shifted.
That one ruled out `double` returns, operand order, every spelling of the comparison,
inline helpers, free versus member versus virtual callees, C with `__fastcall`, and a
twenty-switch flag sweep before finding it.

So those three functions shared a source file, and that is not inferred from address
clustering; it is forced by the bytes. Wherever two functions in a call relationship pass
a float result, the grouping is recoverable the same way. It is a narrow case, and it is
the only proof of original file structure the project has. It would only start to matter if the goal were relinking a whole
matching image, which it is not.

### Names

**Every name in `src\` is invented**, `Vector3`, `Matrix`, `LevelList`, `IsBackupPath`, every
member and every parameter. Nothing in these binaries carries the originals. Names are chosen to
describe what the code demonstrably does and nothing more; where a member's purpose is not
established, the comment says so instead of the name implying it. Treat them as our labels, not
as recovered fact.

## Nothing here is ever linked

Worth being explicit, because the manifest can read as though a source file builds an image. It
does not. **The rfl and the exe are reference data, never build outputs.**

```
math\vector3.cpp
      |
      |  cl /c            compiled ONCE
      v
build\all\math\vector3.obj
      |
      +--> extract ?dot@... , compare against rfl at 0x10002380   -> match
      +--> extract ?dot@... , compare against exe at 0x004011d0   -> match
```

One compile, many comparisons. A manifest row does not claim "this source builds that image", it
claims "this function, compiled by the original toolchain, is byte for byte what sits at that
address", and that claim can be made about as many addresses in as many images as you like.

The same bytes satisfy both images because a function body is position-independent: the linker
decides where it lands, not what it contains. Everything that *does* depend on placement (call
targets, absolute addresses) is exactly what `matchtool.py` masks on both sides.

## Running it

```
python build.py                    everything in the manifest
python build.py math/vector3.cpp   one source file
python build.py --flags "/Ox /Gy"  override the switches, to sweep them
python try.py math/vector3.cpp 0x10002380      one function, the working loop
```

Three paths come from the environment, and the defaults in `build.py` are the ones on the
machine this was developed on:

| | |
|---|---|
| `VC6` | the portable toolchain, `bin\`, `include\`, `lib\`. See `documentation/TOOLCHAIN.md` |
| `RFL` | a **pristine** `Fellowship.rfl` |
| `EXE` | a **pristine** `Fellowship.exe` |

Pristine matters. Both images on a normal install have been through the `_FixEnhancers` byte
patcher, and a patched one mismatches for reasons that have nothing to do with the source.
`documentation/TOOLCHAIN.md` has the hashes and a one-dword test.

## The manifest

`expect` is what makes this a harness, not a script:

| | |
|---|---|
| `match` | it matched when last run. If it stops matching, that is a **regression** and is reported as one |
| `todo` | not reproduced yet. If it starts matching, that is progress, and the line should be updated |

So adding a function means adding a `todo` line and working until `build.py` reports it as newly
matching. Nothing has to be edited in the build script itself.

## Exporting a range to work on

`tools\ExportFunctions.java` is a Ghidra **headless** script. It writes one JSON file per
function plus an `index.json`, so the decompilation work itself never has to touch Ghidra: no
GUI and nothing to drop out mid-session, and several people can work from the same export at
once.

```
set G=<ghidra>\support\analyzeHeadless.bat

rem first time for an image - imports and analyses it, which takes a few minutes
%G% <projdir> <proj> -import Fellowship.rfl ^
    -scriptPath <repo>\decomp\tools -postScript ExportFunctions.java <outdir> 0x10072800 0x10073000

rem afterwards - reuses the analysed program, seconds
%G% <projdir> <proj> -process Fellowship.rfl -noanalysis ^
    -scriptPath <repo>\decomp\tools -postScript ExportFunctions.java <outdir> 0x10072800 0x10073000
```

The two addresses bound what to export; omit them for the whole image, which for the rfl means
thousands of files. A range at a time matches how the work actually goes.

Each file carries what a reader needs and raw disassembly does not:

| | |
|---|---|
| `size` | the body **excluding** trailing padding, exactly what `manifest.tsv` wants |
| `calling_convention` | `__thiscall` vs `__cdecl`, which decides the signature |
| `disassembly` | with raw bytes alongside each instruction |
| `decompiled` | Ghidra's C, the most useful single field |
| `calls`, `data` | resolved call targets and string literals |

`index.json` flags every function as leaf or not. **Leaf functions are the ones to hand out
first**, no calls means no relocations, so nothing is masked and a match means every byte
agreed.

It is a Java script and not Python, on purpose: headless runs Java with no setup, whereas
PyGhidra needs Ghidra launched a particular way and a pip package installed. Ghidra prints
`Module manifest file error` warnings for any installed extensions during headless runs; they are
harmless.

## Dividing the work

The work divides cleanly because the pass/fail is mechanical: a function either produces matching
bytes or it does not, so nothing needs reviewing for plausibility.

```
tools\ExportFunctions.java   ->  export the range, once
CONVENTIONS.md               ->  what the compiler does, read this first
try.py                       ->  the working loop, one function
build.py                     ->  integration, run once over everything
```

**Scout before spreading out.** The expensive part is discovering a codebase convention, and one
discovery unblocks dozens of functions. Starting on an unexplored class just produces several
copies of the same confusion. Do two or three functions of a new class first; once those match,
the rest usually match on the first attempt.

**One source file at a time.** `try.py` writes to `build\obj\<source-stem>.obj`, so different
source files never collide, but the same source file worked on twice will. `build.py` writes to
`build\all\`, so an integration run cannot land on an object something else is mid-way through.

**Prefer leaf functions.** `index.json` flags them. No calls means no relocations, so nothing is
masked and a match means every byte agreed.

**Stop after about eight attempts.** A function that has not matched by then is usually stuck on
something source cannot reach, and further attempts buy nothing. Wall clock scales with attempts
almost exactly, so that cap is a time cap as well.

**Export a tight range.** Each function's JSON is read in full, and a 26 KB record for a
500-byte function is real cost.

### What the first round taught

Five leaf functions of 33 to 42 bytes, one at a time. All five matched, most on the first or
second attempt, and the hardest took six.

What it bought beyond the five functions was **six new conventions**, now in `CONVENTIONS.md`,
several of them general enough to apply across both images: literal store order, branch polarity,
struct packing in the addressing mode, the `DEC`/`JS` loop guard, pointer-to-member for
`__thiscall` callbacks, and vtable slot placement. The conventions are the compounding asset; the
functions are almost a side effect.

## Adding a function

1. Find its address and its real length in Ghidra, the length **excluding** trailing padding.
   Padding in these images is `90`, not `cc`.
2. Add a `todo` line to `manifest.tsv`. The symbol is the decorated name; if you are unsure of
   the mangling, `python tools\matchtool.py obj build\<name>.obj` lists what is really there,
   and `matchtool.py` will also resolve an unambiguous substring.
3. Write the candidate into the matching file under `src\`.
4. `python build.py <source>` until it matches, then set `expect` to `match`.

Prefer leaf functions with no calls and no float constants first: they carry no relocations, so
nothing is masked and a match means every byte agreed.

## What a mismatch is telling you

`matchtool.py` blanks the operand at every relocation site on both sides before comparing,
because a `call rel32` is a placeholder in a `.obj` and a resolved address in the image. What
survives is pure code generation, and it repays reading closely: the four functions above
came out of exactly this:

* **An extra instruction, or one in the wrong place**, usually means the source shape is nearly
  right. `operator+` came down to a single `FXCH` caused by the order the three sums were
  computed in.
* **A temporary that the original does not have** means the compiler is not applying the return
  value optimisation, and the return statement's shape is wrong.
* **`fld`/`fstp` where the original has `mov`** means the FPU is being involved in what the
  original treats as a bit copy.
* **Everything different** usually means the optimisation switches are wrong, not the source.
  `/O2` is correct for this game; `/Og` and `/Od` match nothing.

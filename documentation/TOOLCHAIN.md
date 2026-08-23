# Which compiler built the game

**Answer:** Microsoft Visual C++ 6.0, linker 6.00, with the **Processor Pack** back end for C++.
32-bit, `/MD` against `MSVCRT.DLL`, no `/GS`.

This matters for one reason: a byte-for-byte decompilation has to be compiled by the same
toolchain that emitted the original. Plain VC6 SP6 will not reproduce this code generation, and
anything from VC7 onward is not close. The measurement is `decomp/tools/richdump.py`, next to this file.

## Where the evidence is

Every Microsoft-linked PE carries an undocumented **Rich header** between the DOS stub and the PE
signature. It records, for every object file handed to the linker, which tool emitted it and that
tool's exact build number, including tools that left no other trace. It is the most precise
compiler fingerprint a stripped release binary carries.

| | `Fellowship.exe` | `Fellowship.rfl` |
|---|---|---|
| `e_lfanew` | `0x150` | `0x100` |
| `DanS` / `Rich` | `0x80` / `0x130` | `0x80` / `0xe0` |
| Rich XOR key | `0xe836d09a` | `0x0c624b27` |
| Rich checksum | valid | valid |

The Rich header's XOR key doubles as a checksum over the bytes preceding it. Both files
recompute correctly, so both headers are intact and the decode below is trustworthy and not
a plausible-looking misalignment.

## Read the rfl, not the exe

The two files disagree, and the exe is the one lying.

| | `Fellowship.rfl` | `Fellowship.exe` |
|---|---|---|
| Timestamp | `0x3db49963` = **2002-10-22 00:18:43 UTC** | `0x21544c46` = the ASCII bytes **`FLT!`** |
| Rich entries | 10, every one VC6-family | 20, of which 80 objects are **VC7.0-era** |
| Debug directory | empty, fully stripped | a CODEVIEW entry, no PDB path |
| Sections | `.text .rdata .data .rsrc .reloc` | the same plus `.cms_t`, `.cms_d`, a second `.idata` |

`.cms_t` and `.cms_d` are SecuROM's sections. The 80 VC7-era objects (prodIDs `0x1c`/`0x1d`,
build 9178) appear **only** in the exe, alongside them, and are absent from the rfl entirely.
They are the protection wrapper's own code, compiled by whoever wrapped the build, not by
Surreal. Counting them as game toolchain would put the answer a whole compiler generation out.

The `FLT!` timestamp is the No-CD crack's tag, written over the COFF `TimeDateStamp`. That field
sits outside the range the Rich checksum covers, so the header still verifies. The
rfl's timestamp was never touched and dates the build to two months before the game shipped.

So the rfl is the clean read: unprotected, correctly dated, and holding the bulk of the game
logic.

## What the rfl's Rich header says

```
 prodID   build   count  tool
------- ------- -------  ------------------------------------------
   0x31    9044     234  C++ back end ("UTC 12.2")
   0x0a    8047     110  C back end
   0x01       0      77  import records, linker-generated
   0x0e    7299      30  MASM 6.13
   0x0b    8047      10  C++ back end
   0x0a    8447      10  C back end          [VC6 SP5 + Processor Pack]
   0x13    8034       5  linker
   0x0b    8966       2  C++ back end
   0x06    1735       1  Cvtres 5.00
   0x04    8447       1  Linker 6.00         [VC6 SP5 + Processor Pack]
```

The load-bearing entry is the linker: **build 8447, VC6 SP5 with the Processor Pack**. It is
corroborated independently by the Optional Header's `MajorLinkerVersion`, a documented PE field,
which reads `6.00` in both files.

The compiler split is the interesting part. The 110 C objects came from the stock 12.0 back end;
the 234 C++ objects, the bulk of the game, came from build 9044, the updated `C2.DLL` the
Processor Pack installs. A VC6 install *with the PP applied* produces exactly that split. A
stock VC6 install cannot.

The 30 MASM 6.13 objects are hand-written assembly and will need assembling, not compiling.

## Corroboration from the other direction

The exe's entry point at `0x50477e` is a textbook VC6 `mainCRTStartup`, not the VC7+
`__tmainCRTStartup` template:

* `__set_app_type(2)`, then `_adjust_fdiv` (the Pentium FDIV workaround, still referenced)
* `initterm(&DAT_0052e670, &DAT_0052e674)`: the `.CRT$XI*` C initialisers, called **before**
  `__getmainargs`, and a second `initterm(&DAT_0052e000, &DAT_0052e66c)` for the `.CRT$XC*` C++
  initialisers called **after**. That split and ordering is VC6's `crt0dat.c`.
* the SEH frame is set up through `except_handler3`, not `_except_handler4_common`, so there is
  no `/GS` security cookie. `/GS` did not exist until VC7.1 and was not default until VC8.

The imports agree: `_CIfmod`, `__getmainargs`, `_XcptFilter`, `_controlfp`, `_acmdln`,
`_onexit`, `_splitpath`, `_makepath`, all resolved against **`MSVCRT.DLL`**. That is the
dynamic-CRT (`/MD`) import set for VC6; VC7 would bind `MSVCR70.DLL` instead. `_CIfmod` in
particular is a VC4/5/6-only x87 helper that later compilers inline.

## Target settings, as far as they are known

| | |
|---|---|
| Compiler | VC++ 6.0 **with the Processor Pack** |
| Linker | 6.00, build 8447 |
| CRT | `/MD`, `MSVCRT.DLL` |
| Buffer checks | none, `/GS` did not exist |
| Architecture | 32-bit, `IMAGE_BASE` `0x400000` (exe) / `0x10000000` (rfl) |
| Assembler | MASM 6.13 for the 30 hand-written objects |

Optimisation flags are **not** settled by any of this. The Rich header records which tool ran,
never which switches it ran under. `/O2` versus `/Ox`, the inlining level, and whether
`/GL`-style whole-program work was on all have to be recovered by compiling candidates and
diffing output against the original, which is the normal way this is done, and the reason
getting the compiler itself right first is worth the effort.

## What is not settled

**The prodID names are reconstructed, not documented.** Microsoft has never published the
prodID → tool mapping; the table in `decomp/tools/richdump.py` is the community's. The build numbers
themselves are read straight out of the file, and the linker version comes from a documented PE
structure, so neither depends on that table. `decomp/tools/richdump.py` prints unmapped prodIDs raw rather
than guessing.

**Build 9044 is the Processor Pack. This is now confirmed, not inferred.** It was the least
certain claim on this page; it was settled by extracting `vcpp5.exe` and reading the version off
the file:

```
c2.dll   13.00.9044.0     <- the Processor Pack's C++ back end
```

That is an exact match for the 234 C++ objects in the rfl, and it also explains the odd prodID:
the Processor Pack's back end self-reports as **13.00**, a VC7-lineage `C2.DLL` grafted onto a
VC6 front end, so it is recorded under its own prodID instead of sharing VC6's. It was
then confirmed a second time by compiling: three of the four functions in `FIRST-TARGETS.md`
matched byte for byte on the first attempt.

## The pristine baseline

Byte-matching needs an exe that has never been through the `_FixEnhancers` file patcher.
Diffing against a patched one shows those patches as false differences.

```
Fellowship.exe  2,133,459  sha256 e7475f14c43b80fc1f5562dd809c9951761531fbccd6785b3905beb0bd5e781e
Fellowship.rfl  1,372,160  sha256 035a4abd6d086640fc956d8fbc9369d52b623293824dfabf3d000f584720fbd3
```

Both live in `K:\OPEN FELLOWSHIP\reference\` on the development machine. The
exe there is byte-identical to `K:\OPEN FELLOWSHIP\clean no cd\Fellowship.exe`.

**The test for pristine is one dword.** `black_screen`'s site at VA `0x43D2BC`, file offset
`0x3d2bc`, is the `D3DFORMAT` returned for 8-bit textures. In `.text` here the RVA and the file
offset coincide, so no translation is needed:

| value | | |
|---|---|---|
| **41** | `D3DFMT_P8` | pristine, never patched |
| 50 | `D3DFMT_L8` | already through the file patcher |

That is cheaper and more certain than a hash comparison against an unknown copy, and it is the
first thing to check on any exe offered as a reference. `runtime/src/plugins/black_screen/README.md`
is where the site comes from.

Measured across the copies on the development machine:

```
41  K:\OPEN FELLOWSHIP\clean no cd\                    <- the reference above
41  K:\OPEN FELLOWSHIP\reference\  byte-identical to it
50  C:\Program Files (x86)\Surreal\Fellowship\         215 bytes patched
50  K:\OPEN FELLOWSHIP\fellowship exe no fog no view distance\  154 bytes patched
```

**The rfl needs checking separately, and has no such one-dword test.** The two rfl copies differ
by 349 bytes. The patched one carries seven `e9 xx xx xx xx` + `90` detours in `.text` (at
`0x10063ca0`, `0x10064779`, `0x100648b2`, `0x100648c8`, `0x10064917`, `0x10064a0b`, `0x10064b2a`
and `0x100789a7`) jumping into stubs written at `0x100ec140`-`0x100ec32d`, which the pristine
copy leaves as zeros. That is the zero-slack technique `runtime/README.md` describes, so the
direction is never ambiguous: **real code becoming `e9 …` + NOP padding is the patch**, and a
zeroed slack region is the pristine state. Hash against the reference above instead of guessing.

All four carry the identical Rich header, the same XOR key `0xe836d09a` and the same valid
checksum, so all four descend from one original link and everything above holds for any of them.
Only the `.text` patches differ.

"Pristine" here means unpatched by this project, **not** un-cracked. The clean exe still carries
the `FLT!` timestamp and the `.cms_t`/`.cms_d` SecuROM sections, as every No-CD build does. That
is another reason the rfl, which has neither, is the better reference of the two files.

## The toolchain, without installing anything

VC6 does not need to be installed. It is `cl.exe` plus three DLLs, driven by three environment
variables, and the VS6 install media stores everything **loose and uncompressed**: no cabs to
unpack for the compiler itself.

```
vc6-portable\
    bin\        from  <CD1>\VC98\BIN\           all of it
                plus  <CD1>\COMMON\MSDEV98\BIN\MSPDB60.DLL   (cl and link both need it)
                then overlay the Processor Pack's C2.DLL and ML.EXE over the CD's
    include\    from  <CD1>\VC98\INCLUDE\
    lib\        from  <CD1>\VC98\LIB\
```

71 MB in total. Set `PATH` to `bin`, `INCLUDE` to `include`, `LIB` to `lib`, nothing touches the
registry, and it can sit next to the repo or on a stick. `decomp\build.py` takes the
directory in `%VC6%` and does exactly this.

The Processor Pack (`vcpp5.exe`) and SP5 (`vs6sp5.exe`) are IExpress/WEXTRACT self-extractors
wrapping a CAB. They can be opened without running them: locate the embedded CAB by its `MSCF`
header, carve it out using the `cbCabinet` length in that header, and hand it to Windows'
`expand`. Validate the whole header before trusting an `MSCF` hit: the stub's own data contains
false positives.

The assembled result, and what it means:

| | version | |
|---|---|---|
| `CL.EXE`, `C1.DLL`, `C1XX.DLL` | 12.00.8168 | front end, from the CD |
| **`C2.DLL`** | **13.00.9044** | **back end, from the Processor Pack, the one that matters** |
| `LINK.EXE`, `MSPDB60.DLL` | 6.00.8168 | from the CD |

Two gaps against the game's own Rich header, neither of which has blocked anything:

* the game's linker is **8447**, this is 8168. **The linker never runs during function matching**,
  `matchtool.py` compares a `.obj` against the image, so `link.exe` is not involved. It would
  only matter if the whole image were relinked, and it accounts for one object in the rfl.
* the game's C front end is **8047**, older than this 8168. It has not mattered so far: the back
  end decides code generation, and the matches confirm it. If a C file ever refuses to match
  while its C++ neighbours do, this is the first thing to suspect.

`vs6sp5.exe` does contain compiler binaries (`link.exe` in `VS6sp54.cab`, `cl.exe`/`c1.dll`/
`c1xx.dll` in `VS6sp55.cab`) but they are inside a **spanned** cab set that `expand` would not
walk to completion, and that `expand -D` cannot even list. Since the linker is not on the
critical path, this was left alone, not solved.

## Reproducing it

```
python decomp/tools/richdump.py "<game folder>\Fellowship.rfl"
```

No third-party packages. Takes any number of PE paths, PE32 or PE32+.

The checksum logic was validated before it was trusted here: it reports *valid* on
`System32\notepad.exe`, `SysWOW64\notepad.exe` and `System32\winver.exe`, all known-good
Microsoft builds. That check is worth re-running after any edit to the parser, because a wrong
checksum seed produces confident-looking entries that are silently garbage, which is how the
first run of this tool reported the exe's header as tampered with when it was not.


# What the original compiler does

Conventions established by matching functions byte for byte, not by reading about the toolchain.
Every one of them was found the hard way, and they have held on every function matched so far.
They are the compounding asset of this work: a convention learned once usually unblocks a dozen
functions that would otherwise each cost an afternoon.

The toolchain itself, and how to reproduce it, is in `documentation/TOOLCHAIN.md`. How to add a
function is in `README.md` beside this file.

## Conventions this codebase follows

Found the hard way. Assume they hold; they have on every function so far.

**A function returning a class by value uses a named local assigned in reverse member order.**

```cpp
Vector3 r;
r.z = ...;   r.y = ...;   r.x = ...;   return r;
```

The x87 stack forces it: all components are computed before any is stored, and `FSTP` pops from
the top, so storing x first means pushing x last. Writing `return Vector3(a, b, c)` instead
evaluates forwards and pays an `FXCH`, if the original *has* an `FXCH`, use that form instead.

**Classes returned by value declare an explicit copy constructor**, even though nothing calls it
and it appears nowhere in the binary. Without it the compiler builds a temporary and copies
rather than constructing into the caller's return buffer.

**`a + b + c` lets the compiler reassociate float arithmetic; separate `+=` or `*=` statements do not.**
This is not only addition. `pct * 0.01 * scale` written as one expression let VC6 emit the two
`FMUL`s in the opposite order to the original; two `*=` statements pinned it. If the original groups
or orders the terms differently from your expression, accumulate instead:

```cpp
r.z = m[2][1]*v.y;  r.z += m[2][2]*v.z;  r.z += m[2][0]*v.x;
```

**Flags are `/O2 /Gy /GX` and are settled.** Do not sweep them. `/Og` and `/Od` match nothing.

`/GX` was added late, after `0x1005c500` turned out to carry a C++ exception-handling frame that
cannot be produced without it. It emits nothing at all in a function with no destructible local,
which is why the first 34 matches never revealed it and why adding it changed none of them. If your
function has a local object with a destructor, expect an EH prologue - three pushed words and a
`FS:[0]` link - and note that the frame has **no real EBP**: `__CxxFrameHandler` reconstructs one as
the registration node plus 12, so unwind funclet offsets read as *entry ESP minus n*.

**Store order is literal.** VC6 at `/O2` emits stores in source order and does not group them by
value. So the order in the disassembly *is* the order of the assignments, a bulk initialiser is
read off directly rather than guessed at. Two or more runs of strictly **descending** offsets,
one run per distinct constant, is a chained assignment:

```cpp
m[0][0] = m[1][1] = m[2][2] = 1.0f;    // chained assignment runs right to left
m[0][1] = m[0][2] = /* ... */ = 0.0f;
```

**Branch polarity follows source order.** `if (x == 0) return 0;` puts the zero-return inline
after a `JNZ`; the original's `JZ` to a tail `xor eax,eax / ret` means the *non-zero* case is in
the `if` body and `return 0` comes last. Match the shape Ghidra printed.

**Struct packing shows up in the addressing mode.** A stride that is not a power of two - a
`LEA r,[r+r*2]` before the index scale, or a displacement like `-6` - means the struct is packed.
`#pragma pack(2)` on a `{void*; unsigned short;}` gives stride 6; unpacked it pads to 8 and the
scale becomes `*8`. Packing also restores the `AND EAX,0FFFFh` zero-extension that VC6 elides
when the scale is a power of two.

**`DEC r` / `JS` as a loop guard** means the source decrements the counter itself,
`while (--count >= 0)`. Writing `for (i = count - 1; i >= 0; --i)` adds a redundant
`TEST EAX,EAX`. A `LEA EDI,[EAX+1]` after the guard is the compiler's own counted-loop rewrite,
not something to write.

**A `__thiscall` callback is spelled as a pointer-to-member.** This toolchain rejects the
`__thiscall` keyword (C4234). With the class fully defined, MSVC uses the 4-byte
single-inheritance representation, and `(p->*m)()` lowers to `MOV ECX,p / CALL m` with no thunk.

**Two virtuals declared, not defined, put a call at `[vtable+4]`.** Declaring virtual functions
without bodies is enough to place a vtable slot; only the out-of-line non-virtual method gets
emitted. A predicate returning `int` gives `MOV EAX,1` / `XOR EAX,EAX`; `bool` gives a byte-sized
`SETcc` instead.

**Reaching an authored property goes through the slot's ADDRESS, not the slot.** The engine reads
game data by ordinal through a virtual accessor - see `documentation\ORDINAL-MAP.md`. The natural
spelling folds and loses three to five bytes:

```cpp
object->properties->GetValue(ord, elem);   // MOV ECX,[EAX+0x14]        WRONG
```

**Which spelling is right is written in the disassembly, so read it before choosing.**

* `MOV ECX,[reg+0x14] / MOV EDX,[ECX]` - the field HOLDS a pointer to the block. The direct
  spelling `owner->properties->GetValue(ord, elem)` is correct and is what you want
* `ADD EAX,0x14 / MOV EAX,[EAX] / MOV ECX,EAX` - three instructions where the first form takes one.
  The block is reached through the field's ADDRESS, and the direct spelling folds to the short form
  and loses three bytes

```cpp
ReadProperty(&object->properties, ord, elem);              // inline helper over a slot address
((PropertyOwner *)((char *)object + 0x14))->Get(ord, elem);  // a subobject at +0x14
```

Two of the matched functions needed the second form and one needed the first, so neither is the default,
count the instructions. `ObjectDefEntry+0x08` and `PropertyOwner+0x14` both hold pointers and take
the direct form; the subobject case in `player\stats.cpp` does not.

**A returned flag that is a live local, not a comparison.** If the original saves EBP, zeroes it at
entry, pushes EBP where a constant 0 is wanted, and merges its arms with a `JMP` to a single
epilogue, the source declared a flag and assigned it:

```cpp
int changed = 0;
...
if (a != b) changed = 1;
return changed;
```

Every comparison-shaped source - `return a != b;`, either branch polarity of the two-return form,
the ternary, a `bool` return, a cast - folds instead to `XOR ECX,ECX / SETNE CL`, and because that
tail is short VC6 then duplicates the tail and the epilogue into both arms and drops EBP from the
frame entirely. The flag also supplies the enregistered zero for any call argument that needs one.

**A float that survives a call needs the callee defined above it, in the same file.** If the original
holds a float-returning call's result on the x87 stack across a second call and ends in `FCOMPP`,
VC6 only emits that when it can see the callee's body. Merely declaring it makes the compiler spill
to a stack slot and compare against memory - a `PUSH ECX`, an `FSTP`, a wider `FCOMP` and a matching
`POP ECX` in every epilogue, so everything after the first call shifts.

Where a function being matched calls one already matched elsewhere in the tree, copy that matched
source in above it instead of declaring it. That is not
duplication for its own sake: it tells you the two were in one translation unit originally, which is
the only direct evidence of the original file structure this project has found. It is worth recording when that happens.

**An ObjectDef class id is `unsigned`.** A `switch` or comparison over one lowers with `JA`
(`0F 87`); declaring the field `int` gives `JG` (`0F 8F`). One byte, usually at the top of a binary
search where nothing else moves, so it is easy to stare past.

**`break` and `return <n>` are not interchangeable at the end of a `case`,** even when the switch is
followed by exactly `return <n>`. Written as `m_field = 1; return 1;` VC6 sees one constant serving
both the store and the return value and emits `MOV EAX,1 / MOV [reg+off],EAX` - four bytes shorter
than `MOV dword ptr [reg+off],1`. The trap is that it only bites the case whose stored value equals
the returned one, so four arms of a five-arm switch match and one does not, and everything after it
shifts. `m_field = 1; break;` emits the store with its immediate and tail-duplicates the epilogue,
which is what the original has.

**A sparse `switch` lowers to a `SUB`/`JZ` chain, and a dense one to a binary search.**
`SUB EAX,0x10025 / JZ ... / SUB EAX,0xc / JZ ...` is a `switch` with fall-through cases, not
`if (a || b || c)` - the latter gives a `CMP`/`JZ` chain and different bytes. A larger switch picks a
median value, tests it, and recurses; the case bodies are then placed by fall-through rather than in
source order.

**A `SETcc` followed by a redundant `MOV EAX,<reg>` and `TEST EAX,EAX` is an inline function
boundary.** Every single-exit spelling folds the comparison into the branch and the `SETcc`
disappears entirely. The shape that reproduces it is an inline helper with **two** returns, where
the second exit is what forces the value into a register:

```cpp
__inline int IsDefPlayer(unsigned short index)
{
    if (index != kNone)
        return g_list->GetEntry(index)->m_class_id == kPlayer;
    return 0;                       // this second return is the whole point
}
```

Note also that a plain `static` helper is **not** inlined by this compiler at `/O2` - it emits a
real `CALL`. `__inline` or `__forceinline` is required.

**A property value that must stay in ST(0) needs an inlined float-RETURNING helper.** Written in
line, `*(float *)ReadProperty(...) * k` makes VC6 load `k` first and fold the dereference into the
`FMUL` - and it does that whichever order you write the operands, and whether `k` is a literal, a
`const float`, an `extern const float` or an `extern float`. Wrapping the dereference so the helper
returns a float is what leaves the value on the stack and lets the scale fold the other way:

```cpp
__inline float ReadFloatProperty(PropertyBlock **slot, int ordinal)
{
    return *(float *)ReadProperty(slot, ordinal, -1);
}
```

It settled four sites at once, after every operand order and every spelling
of the constant.

**A pair of floats coming back from a call is an out-parameter, not a by-value return.** A real
by-value return of a two-float class needs an explicit copy constructor before VC6 will use the
hidden-pointer convention at all, and it then emits that copy at the call site - which the original
does not have.

**VC6 picks the `FNSTSW` mask from the comparison operator AS WRITTEN, and does not canonicalise.**
Four forms, none interchangeable, so `!(a > b)` and `a <= b` are different bytes:

| written | emitted |
|---|---|
| `a > b` | `TEST AH,5` |
| `a >= b` | `TEST AH,0x41` |
| `a < b` | `AND EAX,0x4100` |
| `a <= b` | `AND EAX,0x100` |

Read the mask off the original and write that operator. It settled four of six comparisons
in a single function this way.

**No copy at a call site means the callee took a REFERENCE out-parameter, not a by-value return.**
VC6 never elides the copy in `T x = f();` for a memory-returned class - it builds a temporary and
copies it, every time. So if the original has no copy after the call, the source did not return by
value. `const T &x = f();` is worse: it materialises a second temporary.

Related, and it costs attempts to discover: **VC6 will not inline a `__inline` function that returns
a class or a reference to one** at `/O2`, even a three-store one. Only value-returning helpers
inline, which is why the float-returning property helper above works and a `Vector3`-returning one
does not.

**A Win32 import needs `__declspec(dllimport)`, and that is not optional.** With it, a call
compiles to `FF 15 <addr>` - six bytes, indirect through the import thunk - which is what the game
has. Without it VC6 emits `E8 rel32` into a linker-generated jump stub: wrong instruction, wrong
length, and everything after the first call shifts. Declare them yourself rather than including
`<windows.h>`, which changes what the compiler sees:

```cpp
extern "C" __declspec(dllimport) void * __stdcall GlobalLock(void *hMem);
```

`dllimport` has a second effect worth knowing, because it looks like register allocation and is
not. It makes the thunk slot an ordinary variable, so two calls to the same import share one load
of it - `MOV EBX,[addr]` then `CALL EBX` twice - and the `PUSH EBX`/`POP EBX` pair lands *inside*
whichever block does the calling rather than in the prologue. That falls out of `dllimport`; do not
try to write it.

**A virtual whose `this` arrives on the stack is `__stdcall` - but check what clobbered ECX
first.** At a call site reading `PUSH EAX / MOV ECX,[EAX] / CALL dword ptr [ECX+0x8c]`, the object
is the pushed leftmost argument and the `MOV ECX` is only the vtable load, which is what forced the
object onto the stack. Declare that slot `virtual T __stdcall`.

The absence of a `MOV ECX` before the call is **not** on its own evidence of `__stdcall`. In
`MOV EAX,[ESI] / PUSH 0 / PUSH EDI / CALL dword ptr [EAX+0x10]` the vtable went to EAX, ECX still
holds `this` from the function's own entry, and VC6 has simply dropped a reload it knows is
redundant. That is a plain `__thiscall` virtual and the pushes are its arguments. The rule only
applies when something has actually overwritten ECX.

**`NEG / SBB r,r / NEG` is an if-conversion, not an arithmetic bool.** VC6 emits it when the source
branches and both arms return:

```cpp
if (flags & 0x200000) return 0x18;
return 0x17;
```

Writing the arithmetic form `0x17 + ((flags & 0x200000) != 0)` gives `SHR EAX,21 / AND EAX,1`
instead, which is shorter and shifts every following byte. So do `!!x`, `(bool)x`, a named bool
local, and the ternary `x ? 1 : 0` - all of them peephole to the shift. Two returns is the only
shape that produces the `SBB` sequence.

**`=` and `|=` are literal too.** A plain `MOV dword ptr [EAX],2` where the surrounding code uses
`OR` means the source assigned rather than or-ed, even when the value is provably zero at that
point and the two are equivalent. Write what the instruction says.

**A call you cannot identify is still matchable.** `CALL rel32` and `CALL dword ptr [addr]` both
carry a relocation, so the operand is masked on both sides and only the opcode and the argument
setup have to agree. Declare the callee `extern` with the argument count and calling convention the
disassembly shows, name it for what the site does with it, and record that the identity
is unestablished. A call through an import thunk - `CALL dword ptr [0x0056xxxx]` in the exe - is a
`__stdcall` Win32 API, and those you can usually name outright from the argument count and what
happens to the result.

## Reading a mismatch

`try.py` prints the original above your bytes with `^^` under the differences and `..` where a
relocation operand was blanked on both sides. Masked bytes carry no information, ignore them.

| what you see | what it means |
|---|---|
| integer `MOV` where you have `FLD`/`FSTP` | the destination is a **reference**, not a return value. The function takes an out-parameter. A hidden return pointer and an out-parameter have identical stack layouts, so only the instruction selection tells you |
| a temporary the original does not have | the compiler is not applying the return value optimisation, your return statement's shape is wrong |
| one extra instruction, e.g. `FXCH` | the source is nearly right; the evaluation order is off |
| terms grouped differently | reassociation, use `+=` |
| everything different | your reading of the function is wrong. Re-read the disassembly rather than permuting the source |
| identical instructions, different registers | register allocation. Not reachable from source, so stop here |

## When a match does not mean anything

`matchtool` blanks relocated operands on both sides, which is what makes a comparison meaningful
for a function full of calls. But in a very small function the relocations can be nearly all of
it. `mov eax, <address>; ret` is six bytes, four of them a masked relocation, so *any* source
returning *any* pointer matches it, and the match tells you nothing about whether you identified
the right global.

If the masked count is a large fraction of the size, record that rather than claiming a
result. `try.py` prints both numbers.

## Signatures

Recover them from the disassembly, not from Ghidra's guess:

* `ECX` holds `this` and the function ends `RET n` → `__thiscall` member function
* bare `RET` with arguments on the stack → `__cdecl`, so a free function or a static
* `RET 8` with `[esp+4]` written through at the end → either a by-value return (hidden pointer)
  or an out-parameter. Integer `MOV`s mean out-parameter; see the table above
* a `const` method mangles differently, so if the symbol is not found try the other one

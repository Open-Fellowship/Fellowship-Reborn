# First matching targets

> **Result: all four match byte for byte, with `/O2 /Gy`.** The source is
> `decomp/src/vector3.cpp`; `python decomp/build.py` re-checks it.
>
> Everything below the disassembly of each function was written before any of it was compiled.
> Three matched on the first attempt. `operator+` took nineteen more, and what it cost is
> written up at the bottom, which is the most useful part of this page.

Four functions from `Fellowship.rfl`, picked to be the first things compiled against the
original. See `MATCHING.md` for the harness and `TOOLCHAIN.md` for the compiler.

They were chosen because they are the cleanest possible test of the compiler and its switches:

* **leaf**: no `call`, so nothing depends on another function matching first
* **no float constants**: every operand is `[ecx+n]` or `[eax+n]`, register-indirect, so there
  are **zero relocations**. Nothing is masked, and a match means every single byte agreed
* **small**: 25 to 35 bytes, so a mismatch is readable by eye
* **one cluster**: `0x10002230`-`0x100023a0` is a run of vector operations, almost certainly one
  translation unit. If a switch fixes one it should fix all four, which is far stronger evidence
  than any single function passing

They are also identical in the patched and pristine rfl, so nothing here depends on which copy
is loaded.

All four are `__thiscall`: `ecx` is `this`, and the callee pops its stack arguments (`ret 4`,
`ret 8`). The class is three floats, call it `Vector3` until the real name turns up.

## `0x10002230`: 25 bytes, copy assignment

```
8b c1              mov  eax, ecx
8b 4c 24 04        mov  ecx, [esp+4]
8b 11              mov  edx, [ecx]
89 10              mov  [eax], edx
8b 51 04           mov  edx, [ecx+4]
89 50 04           mov  [eax+4], edx
8b 49 08           mov  ecx, [ecx+8]
89 48 08           mov  [eax+8], ecx
c2 04 00           ret  4
```

`eax` is set to `this` first and never clobbered, so the return value is `this`.

```cpp
Vector3 &Vector3::operator=(const Vector3 &v)
{
    x = v.x;  y = v.y;  z = v.z;
    return *this;
}
```

**MATCH, 25 bytes.** The prediction on this one was wrong and worth recording. The three floats
are copied with *integer* `mov` and not `fld`/`fstp`, and that was read as the signature of
an **implicitly generated** copy-assignment operator, the guess being that the class declares
no `operator=` at all and the compiler synthesises a memberwise copy. That was unnecessary
caution: the hand-written body above, `x = v.x; y = v.y; z = v.z; return *this;`, produces those
integer `mov`s exactly. VC6 copies a float member as a bit pattern and does not involve the FPU
unless arithmetic requires it.

## `0x10002250`: 31 bytes, `operator-=`

```
8b 44 24 04        mov  eax, [esp+4]
d9 01              fld  dword ptr [ecx]
d8 20              fsub dword ptr [eax]
d9 19              fstp dword ptr [ecx]
d9 41 04           fld  dword ptr [ecx+4]
d8 60 04           fsub dword ptr [eax+4]
d9 59 04           fstp dword ptr [ecx+4]
d9 41 08           fld  dword ptr [ecx+8]
d8 60 08           fsub dword ptr [eax+8]
d9 59 08           fstp dword ptr [ecx+8]
c2 04 00           ret  4
```

`eax` holds the argument at the `ret`, not `this`, so this returns **void**, not the
`Vector3&` that `operator-=` conventionally returns.

```cpp
void Vector3::operator-=(const Vector3 &v)
{
    x -= v.x;  y -= v.y;  z -= v.z;
}
```

## `0x100022d0`: 35 bytes, `operator+`

```
8b 44 24 08        mov  eax, [esp+8]
d9 40 08           fld  dword ptr [eax+8]
d8 41 08           fadd dword ptr [ecx+8]
d9 40 04           fld  dword ptr [eax+4]
d8 41 04           fadd dword ptr [ecx+4]
d9 00              fld  dword ptr [eax]
d8 01              fadd dword ptr [ecx]
8b 44 24 04        mov  eax, [esp+4]
d9 18              fstp dword ptr [eax]
d9 58 04           fstp dword ptr [eax+4]
d9 58 08           fstp dword ptr [eax+8]
c2 08 00           ret  8
```

Two stack arguments and `ret 8`: `[esp+4]` is the hidden return-value pointer that a
by-value struct return uses, `[esp+8]` is `v`. It is left in `eax` at the `ret`, as the
convention requires.

```cpp
Vector3 Vector3::operator+(const Vector3 &v) const
{
    return Vector3(x + v.x, y + v.y, z + v.z);
}
```

Note the order: z, y, x are computed and pushed, then popped back x, y, z. That reversal is
forced by the x87 being a stack, and it is a good sign the shape of the source is right.

## `0x10002380`: 27 bytes, dot product

```
8b 44 24 04        mov  eax, [esp+4]
d9 40 08           fld  dword ptr [eax+8]
d8 49 08           fmul dword ptr [ecx+8]
d9 40 04           fld  dword ptr [eax+4]
d8 49 04           fmul dword ptr [ecx+4]
de c1              faddp st(1), st
d9 00              fld  dword ptr [eax]
d8 09              fmul dword ptr [ecx]
de c1              faddp st(1), st
c2 04 00           ret  4
```

Returns a float in `st(0)`.

```cpp
float Vector3::dot(const Vector3 &v) const
{
    return x * v.x + y * v.y + z * v.z;
}
```

## Running them

Once VC++ 6.0 with the Processor Pack is installed:

```
cl /nologo /c /O2 /Gy vector3.cpp
python documentation\matchtool.py compare "<pristine>\Fellowship.rfl" 0x10002380 vector3.obj "?dot@Vector3@@QBEMABV1@@Z"
```

C++ names must be given decorated; `matchtool.py obj vector3.obj` lists what is actually in the
object. Use the **pristine** rfl, `TOOLCHAIN.md` has the hash.

## What each one measures

| | tested | result |
|---|---|---|
| `operator=` | whether the copy operator is written or synthesised | **MATCH**, written, see above |
| `operator-=` | plain float codegen, no return value in play | **MATCH** |
| `dot` | x87 scheduling and expression evaluation order | **MATCH** |
| `operator+` | by-value struct return and the hidden return pointer | **mismatch, 31 of 35** |

Padding between them is `90` (NOP), not `cc` (int3), the release-build convention, and a
check that the boundaries above are right.

## The flags are settled

Swept against all four functions:

| flags | |
|---|---|
| **`/O2 /Gy`** | **3 of 4**, and `/Ox`, `/O2` alone and `/Ox /Ob2 /Gy` are indistinguishable from it |
| `/O1 /Gy`, `/Oxs /Gy` | 3 of 4, but `operator+` is *worse* (34 of 35) |
| `/Og /Gy`, `/Ot /Og /Gy` | 0 of 4 |
| `/Od /Gy` | 0 of 4 |

So `/O2` is right, and the remaining failure is a **source-shape** problem, not a switch.
`/Gy` cannot be confirmed or denied by this: it decides COMDAT packaging, not code generation,
and the game's own objects were compiled with it or without it identically as far as these four
bytes-on-disk are concerned.

## Solved: `operator+`, and what it cost

The answer, after nineteen attempts:

```cpp
Vector3 Vector3::operator+(const Vector3 &v) const
{
    Vector3 r;
    r.z = z + v.z;      // reverse member order, and it is load-bearing
    r.y = y + v.y;
    r.x = x + v.x;
    return r;
}
```

with an **explicit copy constructor** declared on the class:

```cpp
Vector3(const Vector3 &o) : x(o.x), y(o.y), z(o.z) {}
```

Two things had to be right at once, so neither alone ever got close.

**The copy constructor.** Without it the compiler builds a temporary and copies it into the
caller's return buffer. Declaring it (even though nothing ever calls it out of line, and the
original contains no such function) makes the compiler construct directly into the return
buffer instead. It changes code generated *elsewhere*, which is not where you would look. This
took the diff from 31 of 35 bytes to 28.

**The reverse assignment order**, which took 28 to 6. The x87 stack is the reason. All three
sums are computed before any is stored, so they sit on the FPU stack together, and `FSTP` pops
from the top. Storing x first therefore requires pushing x *last*, which requires evaluating
z, y, x in that order. Writing the assignments in that order produces it directly. The
constructor-call form evaluates x, y, z and pays for it with an `FXCH`:

```
orig   d9 40 08 …  z, y, x   ->  st(0) = x, stores straight out
ours   d9 00    …  x, y, z   ->  st(0) = z, then  d9 ca  fxch st(2)
```

The last 6 bytes were one instruction's placement (whether `mov eax,[esp+4]` sat between the
final `fld` and `fadd` or after both) and that fell out on its own once the source used a
named local, not a returned temporary. The named-local form is NRVO; the constructor
form is RVO, and VC6 schedules the two differently.

### What was ruled out getting there

Worth recording, because every one of these was a plausible theory and all of them were wrong:

| tried | result |
|---|---|
| `return Vector3(x+v.x, …)`, member-init-list ctor | 31 of 35 |
| ctor with an assignment body instead of an init list | 31 |
| a named local *without* the copy constructor | 31 |
| operand order swapped, `v.x + x` | 31 |
| no default constructor declared | 31 |
| `__forceinline` on the constructor | 31 |
| `operator+` inline in the class body | inlines away entirely |
| **a fourth member, `Vector3` being 16 bytes, not 12** | 31 |
| `float v[3]` array storage instead of named members | 31 |
| a two-argument constructor taking both operands | 31 |
| `operator+` as a non-const member | 31 |
| `operator+` as a free `__stdcall` function | 33 |
| `/GX`, `/GR`, `/Gr`, `/GX /GR`, `/Gf` | 31 |
| `/Op` | 33, worse |
| `/G3`, `/G4`, `/G5`, `/G6`, `/GB`, processor scheduling targets | 31, all identical |

The 16-byte class theory is worth singling out. It was the leading hypothesis and it was wrong.
The reasoning behind it still holds (the other three functions only ever touch offsets 0, 4 and
8, so they genuinely cannot distinguish a 12-byte class from a larger one) but `operator+`
turned out not to distinguish it either. `Vector3` being exactly three floats remains an
assumption that nothing here has tested.

## Superseded: the RVO investigation

The original keeps all three sums on the x87 stack at once and writes them straight into the
caller's return buffer. Every candidate tried so far builds a temporary on the stack first and
then bit-copies it across:

```
ours    83 ec 0c           sub esp,12         <- a temporary
        d9 5c 24 00        fstp [esp]            each sum stored into it
        89 11 / 89 51 04   mov [ecx],edx      <- then copied to the return buffer

orig    d9 40 08 d8 41 08  fld/fadd  z        <- all three held on the FPU stack
        d9 40 04 d8 41 04  fld/fadd  y
        d9 00   d8 01      fld/fadd  x
        8b 44 24 04        mov eax,[esp+4]    <- the return buffer
        d9 18 d9 58 04 …   fstp/fstp/fstp     <- stored directly into it
```

The original also evaluates right to left (z, y, x), which is argument-push order; ours
evaluates left to right. Both point the same way: the original inlines the constructor **into
the return buffer** and skips the temporary, the return value optimisation, and VC6 is not
doing that for any source shape tried yet.

Ruled out, all still 31 of 35:

| | |
|---|---|
| `return Vector3(x+v.x, …)` with a member-init-list ctor | the current source |
| ctor with an assignment body instead of an init list | |
| a named local, `Vector3 r(…); return r;` | |
| operand order swapped, `v.x + x` | |
| no default constructor declared | |
| `__forceinline` on the constructor | |
| `operator+` defined inline in the class body | inlines away; forced out of line via a member pointer |

Switches ruled out too, all still 31 of 35 with the other three still matching:
`/GX` (exception handling, the most promising guess, on the theory that it changes how VC6
treats temporaries), `/GR`, `/Gr`, `/GX /GR`, `/Gf`. `/Op` makes it worse, 33 of 35.

So it is neither the return formulation nor any switch tried. What is left, roughly in order of
promise:

* **the class is bigger than three floats.** Everything here assumes `Vector3` is exactly
  `{float x, y, z;}`. If the real class has a fourth member, a base, or a virtual, the return
  buffer handling changes. The other three functions only ever touch offsets 0, 4 and 8, so they
  cannot distinguish a 12-byte class from a larger one, they would match either way. This is
  the assumption the evidence is weakest on and it is not contradicted by anything.
* **the constructor is not what is being called.** A two-argument or copy constructor, or a
  static factory, would change how the result is built.
* **`operator+` is a free function.** `ecx` holding `this` argues against it, but a
  `__thiscall`-declared free function would look the same.

Worth remembering that this is one function out of four, and the three that matched are enough
to have settled the toolchain and the optimisation level. `operator+` is a question about the
shape of the original class, not about the compiler.

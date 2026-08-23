# black_screen

**Produces:** `black_screen.dll`. Patches `Fellowship.exe`. Always on, and it has no configuration.

A stock install hangs on a black screen at load **on NVIDIA cards** and starts perfectly well on
AMD ones. That is not a Windows version problem or a wrapper problem; it is one constant in the
executable, and which vendor's driver you have decides whether it matters.

## The site

`0x43D2B0` maps a bit depth to a `D3DFORMAT`:

```
0043D2B0   mov  ecx,[esp+4]        the bit depth
0043D2B4   xor  eax,eax
0043D2B6   cmp  ecx,8
0043D2B9   jne  0x43D2C1
0043D2BB   mov  eax,0x32           <- the 8-bit answer
0043D2C0   ret
0043D2C1   cmp  ecx,0x10           16-bit: 23, 24, 25, 26, 29
```

The 16-bit branch is what identifies the enum beyond argument: 23, 24, 25, 26 and 29 are
`R5G6B5`, `X1R5G5B5`, `A1R5G5B5`, `A4R4G4B4` and `A8R3G3B2`, in order. Nothing else numbers its
formats that way.

So the 8-bit answer is a `D3DFORMAT` too:

| | | |
|---|---|---|
| `D3DFMT_P8` | 41 | 8-bit **paletted**. NVIDIA dropped support. AMD still carries it. |
| `D3DFMT_L8` | 50 | 8-bit luminance. Every driver supports it. |

A stock build answers 41. On an NVIDIA card the driver refuses the format, the texture is never
created, and the game sits on a black screen, on an AMD card the same executable is fine. One
byte decides it.

## Why this is a guard, not a fix

The executable this project is built against **already answers 50**, so on the development
machine this plugin has nothing to do, and it says so rather than claiming a fix it did not make:

```
[black_screen] 0043D2BC already answers D3DFMT_L8 (50) for 8-bit - nothing to do on this copy
```

Every copy and backup on that machine descends from one that had been through a file patcher.
A pristine install still holds 41, and that is who this plugin is for:

```
[black_screen] 0043D2BC  8-bit format  D3DFMT_P8 (41) -> D3DFMT_L8 (50)
```

The community patcher ports across as an unconditional write of 50 (`Fellowship.dll+0xB9D`
pushes `0x43D2BC`, then `0x32`). Reading first costs nothing and turns a silent no-op into a line
that tells you which of the two situations you are in.

An unrecognised value, neither 41 nor 50, is left alone and warned about, on the grounds that
it is far more likely to be a different build than a bug this plugin understands. The six-byte
signature covering `cmp ecx,8 / jne / mov eax` is checked first for the same reason: the opcode
`0xB8` alone occurs thousands of times in this executable.

## No configuration

There is no `[black_screen]` section and no `Enabled` key.

The plugin reads the constant before it writes and declines on anything it does not recognise: a
copy already answering `D3DFMT_L8` is left alone and told so, and a value that is neither 41 nor
50 is treated as a different build rather than as a bug. A switch protected nothing those two
checks did not already handle, and the mistake it invited, leaving it off on an NVIDIA card, is a
black screen at load with no clue as to why. That is the exact failure this plugin exists to
prevent.

Delete `black_screen.dll` from the plugins folder if you want it gone. That is how every plugin
here is switched off.

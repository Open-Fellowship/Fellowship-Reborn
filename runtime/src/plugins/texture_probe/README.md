# texture_probe

**Produces:** `texture_probe.dll`. Patches `Fellowship.rfl`. **Off by default.**

A DIAGNOSTIC. It reads and prints and changes nothing the game draws.

Five attempts at scaling the interface art failed, every one of them reasoned from a decompile and
every one of them wrong about which local held what. This prints the numbers instead. It is the
tool that found the answer, and it is kept for the next time.

## What it records

Two hooks inside `FUN_1006C890`, the `GUIControl_Texture` draw.

**`rfl+6CA5B`**, eight instructions past the two size clamps, where everything the draw computed
is still live. Per distinct control: the position, the `+0x40`/`+0x44` pair, the source rectangle,
the texture's own dimensions, and twenty-four words of the surrounding stack.

**`rfl+6CBA2`**, the blit call itself. The full argument list, deduplicated, so the same control
drawn every frame is one row.

Every value is printed as a float, a signed integer and hex. Which of the three a slot holds is
usually the question being asked, and guessing it is how the earlier attempts went wrong.

## How to use it

Set `Enabled=1`, start the game, and reach the screen you want to understand. Press **F3** once to
begin recording, wait a second or two, press **F3** again to stop and print. Press it again to
clear and start over.

Nothing appears on screen. Everything goes to `fellowship_reborn.log`.

## What it found

The measurement that ended the search. A save slot passed `arg9 = 1.778` to the blit, and
`64 * 1.7778 = 113.78`, exactly the destination width the engine had clamped away one step
earlier. `arg9` and `arg10` are a destination scale pair the engine already threads through the
call and never uses, which is what `texture_scaling` now writes.

The stack dump settled the layout at the same time: `esp+20`/`esp+1C` are the destination near
corner, `esp+38`/`esp+3C` the far corner, and `esp+28` the source span, already clamped. Both a
cursor and a save slot satisfied `far = near + width`, which is what proved destination and
source are separate words, not one value doing two jobs.

## Every read is guarded

The first version dereferenced the control, the texture it names and the stack with a raw `memcpy`
and no validation, and took the game down the moment recording started. Every read now goes
through `memory_is_readable_range` first.

`hud_probe` gets away without that because it only records two integers and never follows a
pointer. Copying its shape without noticing that difference is what broke this one.

A correction that came out of the same run: `control+0x64` is a texture **id**, not a pointer.
The values are small integers, `0xB0130` for the pointer's texture, so reading `id + 40` as an
address gives nothing. The real dimensions are in the stack dump instead.

## Configuration: `[texture_probe]`

| Key | Default | |
|---|---|---|
| `Enabled` | `0` | a diagnostic, on only while hunting something |
| `DumpKey` | `114` | `VK_F3`. A raw virtual-key code |

## What it does not cover

Only `GUIControl_Texture`, which is the mouse pointer and the save and load icons. The in-game
HUD families reach `FUN_1007B2E0`, `FUN_1007ACE0` and `FUN_100791C0`, and none of them pass
through either hook here. A probe for those would need its own sites.

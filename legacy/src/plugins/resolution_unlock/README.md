# resolution_unlock

**Produces:** `resolution_unlock.dll`. On by default.

The options screen offers only a handful of modes, often a single 4:3 one, because the engine
filters the DirectDraw mode list before showing it. This removes the filter.

Ported from the community patcher's `UnlockResolutions` option, decoded out of its own installer
table, not guessed at.

## The three writes

| | original | patched | |
|---|---|---|---|
| `0x4BC4FF` | `0F 84 AD 00 00 00`, `je 0x4BC5B2` | `90 E9 AD 00 00 00`, `nop ; jmp 0x4BC5B2` | accept unconditionally |
| `0x4BC61C` | `0F 85 83 00 00 00`, `jne 0x4BC6A5` | displacement `0` | falls through to `0x4BC622` |
| `0x4BC62D` | `7A 78`, `jp 0x4BC6A7` | displacement `0` | falls through to `0x4BC62F` |

The last two should be understood, not "tidied up": they neutralise a branch by zeroing
its **displacement**, not its opcode. A `jne +0x83` becomes `jne +0`, which jumps to the very next
instruction, so the branch still executes, still takes the same number of bytes, and nothing
downstream shifts. NOPping the instruction would have worked too and would have been more fragile.

The first one keeps the same displacement for a subtler reason: the `jmp` starts one byte later
and is one byte shorter than the `je` it replaces, so `0x4BC500 + 5 + 0xAD` lands on the
same `0x4BC5B2`.

## What is left alone

The engine checks `cmp edx, 0x280` and `cmp ebx, 0x1E0` (640x480) a few instructions later.
Those stay. Modes below that were rejected for a reason and nothing here needs them.

## Configuration: `[resolution_unlock]`

| Key | Default | |
|---|---|---|
| `Enabled` | `1` | |

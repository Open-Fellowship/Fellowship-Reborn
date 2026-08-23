# windowed_res

**Produces:** `windowed_res.dll`. **Off by default.**

Sets the size of the window the game opens in, by replacing the two hard-coded immediates:

```
0x4BC49E   mov dword [0x565C74], 0x280      default width  640
0x4BC4A8   mov dword [0x565C78], 0x1E0      default height 480
```

As bytes those are `C7 05 74 5C 56 00 80 02 00 00` and `C7 05 78 5C 56 00 E0 01 00 00`.

It also drops the mode-list limit at `0x4BC5A1` (`83 FF 24`, `cmp edi, 0x24`) from 36 entries to 12, which is what the community
patcher's `ForceCustomWindowedRes` does.

## It conflicts with resolution_unlock, and so does the original

Two of the five values the patcher writes for this option put back the exact bytes
`UnlockResolutions` changed at `0x4BC4FF`. The patcher gets away with it because both run in one
function, in order. Here they are separate DLLs and the loader loads alphabetically, so
`windowed_res` lands after `resolution_unlock` and wins, the same outcome, reached far less
obviously.

This plugin checks whether `resolution_unlock` has been there, without relying on filenames, and
**logs a warning** when it undoes its work. Run one or the other, not both.

## Configuration: `[windowed_res]`

| Key | Default | |
|---|---|---|
| `Enabled` | `0` | |
| `Width` | `640` | |
| `Height` | `480` | |

Anything under 640x480 is refused: the engine rejects it a few instructions later regardless.

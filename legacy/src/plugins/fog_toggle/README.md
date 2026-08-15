# fog_toggle

**Produces:** `fog_toggle.dll`.

**F1** turns distance fog off and on while the game runs.

## Why a key and not a setting

The fog is what hides the draw distance. Being able to flip it standing still, without restarting,
is how you tell whether `view_distance` is actually doing anything or whether you are looking at
a grey wall either way. That was its original purpose and it is still the reason it exists.

## How

`SetFogEnable(BOOL)` at `0x48BEF0` is the one place the engine passes the level author's fog
choice to Direct3D. The plugin relocates its first ten bytes into a stub and, on the way past,
either leaves the argument alone or zeroes it:

```
pushad / pushfd
call fog_poll                    reads the key, edge-detected
popfd / popad
mov eax,[ecx+0x166]              relocated
mov edx,[esp+4]                  relocated - the BOOL
cmp byte ptr [allowed],0
jne keep
xor edx,edx                      fog off
keep:
jmp 0x48BEFA
```

Ten bytes are relocated rather than five, because the branch needs five and the second
instruction ends at ten; taking the whole pair means nothing is left half-overwritten. `pushfd`
is not decoration: `fog_poll` returns with whatever flags it last set, and the `cmp`/`jne` two
instructions later is ours.

The stub lives in memory this plugin allocates. The byte-patch version of this fix had to find
free space inside the game's own `.text` and put the stub there; a loader removes that need,
along with the risk of two fixes wanting the same cave.

## Configuration: `[fog_toggle]`

| Key | Default | |
|---|---|---|
| `Enabled` | `1` | |
| `Key` | `F1` | one of `F1` `F2` `F3` `F4` |
| `StartWithFog` | `1` | `0` starts with fog already off |

The choice of key is narrow on purpose: the game's own README binds F5 through F12 to its
cheats (fly, tele, heal, invisowalls and the rest), and a fix that silently steals one of those
is a bug report nobody will diagnose.

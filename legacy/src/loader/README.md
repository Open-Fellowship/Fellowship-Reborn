# loader

**Produces:** `dinput8.dll`, goes **next to `Fellowship.exe`**, not into `plugins\`.

The loader patches nothing. It is a jumping-off point: it loads every DLL in `plugins\` and hands
each one its entry point.

## Why the `dinput8` slot

`Fellowship.exe` (No-CD, 2,133,459 bytes) imports from fourteen libraries. Two are single-function
imports and therefore candidates for a proxy:

| | functions imported | |
|---|---|---|
| `d3d8.dll` | 1, `Direct3DCreate8` | **already taken** |
| `DINPUT8.dll` | 1, `DirectInput8Create` | ours |

The `d3d8` slot is where the community graphics wrapper already lives in essentially every
installation, alongside its `d3d8.ini`. Taking it would mean displacing something players depend
on. `DINPUT8` is free, is not a KnownDLL, so the application directory wins over System32, and
no wrapper wants it. It is the same reasoning that put the sibling project on `dinput.dll`.

## When it loads the plugins

`dinput8.dll` is a static import of `Fellowship.exe`, so its `DllMain` runs before a single
instruction of the game. Loading the plugins *there* is what must not happen: `LoadLibrary` under
the loader lock is how deadlocks are made. But nothing has to be loaded there. `DllMain` only
writes five bytes:

```
save the first 5 bytes of the host's entry point, write `jmp our_stub` over them
the stub:  restore those 5 bytes, load the plugins, jump back to the entry point
```

Restoring before jumping back is what makes this safe without decoding an instruction: the entry
point is re-executed from its first byte, so it does not matter that the five bytes may end
mid-instruction. Only one thread exists at that point. The address comes from the PE header
(`AddressOfEntryPoint`), not from a byte pattern, so it cannot be wrong on another build.

**Why not simply `DirectInput8Create`.** Because graphics startup runs before input startup. By
the time the game asks for input, `Direct3DCreate8` has already returned, the display mode is
chosen and the camera's viewport has been built at least once, so anything that has to be in
place before the first `SetViewport` would install too late. `DirectInput8Create` is still wired
up as a fallback: `plugin_loader_run_once()` is idempotent, so whichever trigger fires first wins.

The stub is fifteen bytes:

```
60              pushad
9C              pushfd
E8 rel32        call restore_and_load
9D              popfd
61              popad
FF 25 imm32     jmp dword ptr [saved entry point]
```

The final branch is indirect, through the saved address rather than relative to the stub, so it
stays correct however the linker places it.

## Configuration: `[loader]`

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `1` | `0` loads nothing at all; the game runs exactly as before |
| `PluginDirectory` | `plugins` | Relative to `Fellowship.exe` |
| `ChainDll` | *(empty)* | Explicit forward target, absolute or relative to the game folder |

## The first lines of the log

Which two files this is, read off disk before anything else runs:

```
[loader] host C:\...\Fellowship.exe
[loader] Fellowship.exe 2133459 bytes  (the build this project targets)
[loader] Fellowship.rfl 1372160 bytes  (the build this project targets)
```

Every site in this project was measured against that pair. A plugin that declines on a different
pair is behaving correctly, and this is what lets a reader tell that apart from a plugin that is
broken. The two other builds anyone actually has are named rather than called unexpected:

| | | |
|---|---|---|
| `Fellowship.exe` | 2,137,555 | retail, SafeDisc. Its code is encrypted on disk and only decrypted once the game is running, so every byte check made at the entry point fails and every exe plugin declines. |
| `Fellowship.rfl` | 1,306,624 | pre-1.1. Different addresses; eight of the nine rfl sites used here are not in that build at all. |

Anything else gets "not a build this project has been measured against". A mismatched pair also
gets the order that fixes it: install, apply the official v1.1 patch, then put the 1.1 No-CD
executable in. The No-CD goes last, because the patch replaces the executable.

The rfl's size is read from the file rather than from the loaded module, because at this point
the game has not loaded it yet and will not for several seconds.

## The chain

We take the `dinput8.dll` name, so whatever answered to it before needs a new one. Resolution
order, each step logged:

1. `ChainDll`
2. `<game folder>\dinput8_orig.dll`
3. `<system directory>\dinput8.dll`

**If your game folder already has a `dinput8.dll`**, an input wrapper, an ASI loader, rename it
to `dinput8_orig.dll` rather than overwriting it. Every export is forwarded to it and it keeps
working.

## Load order

Alphabetical, so the sequence is reproducible rather than dependent on the file system.
**Order encodes no dependencies:** no plugin calls into another. Where two of them hook the same
Direct3D vtable slot they chain by convention, each saving the pointer it found and calling
through it, so order decides which runs first and nothing enforces that they agree.

At most 64 DLLs; beyond that the surplus is skipped and reported.

## Limitations

* A DLL without an `open_fellowship_install` export is loaded anyway and noted as such. That is
  deliberate: an ordinary third-party DLL is a legitimate thing to put in `plugins\`.
* Plugins are never unloaded. Hooks hold pointers into them for the life of the process, and
  there is no supported way to remove one from the middle of a chain.
* If something has already redirected the host's entry point, `FellowshipPatcher`, an ASI
  loader, a debugger, the early trigger declines rather than guessing what that other thing
  intended, and the `DirectInput8Create` fallback becomes the only trigger.

## Testing status

**Not yet compiled with MSVC, and not yet run in game.** Both are the next step.

What has been checked, and by what: every translation unit except `early_trigger.c` compiles
clean at `-Wall -Wextra -Werror` (that one is excluded because its stub is MSVC inline assembly,
which no other compiler parses). An earlier revision of this tree, before it was narrowed to
MSVC, did link and produced a `pei-i386` DLL exporting all six names undecorated,
`DirectInput8Create`, `GetdfDIJoystick`, `DllCanUnloadNow`, `DllGetClassObject`,
`DllRegisterServer`, `DllUnregisterServer`, and the fifteen-byte stub above was disassembled from
its emitted form and matched instruction for instruction. That is evidence about the design, not
about the exact bytes MSVC will produce, and this section should be rewritten the moment there is
a real MSVC build to describe.

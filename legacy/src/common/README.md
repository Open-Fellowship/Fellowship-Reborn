# common

The shared library every plugin and the loader link against. It is a static library, so each DLL
carries its own copy: state in here is private to the module that linked it, and one plugin
having resolved the host image does nothing for the next.

Nothing in `common` knows what any fix means. It answers where the process is, whether a write is
safe, what the ini says, and where to log it.

## The target build

`Fellowship.exe`, No-CD, 2,133,459 bytes. `Fellowship.rfl`, 1,372,160 bytes. Every site in the
tree was verified against those two files byte for byte, and every plugin re-verifies the bytes it
is about to overwrite, so a different build declines instead of corrupting.

The engine is 32-bit. Every offset, operand repoint and pointer written into engine memory assumes
a 4-byte pointer. Getting that wrong produces no compile error on its own, only a 64-bit DLL the
game cannot load, or a structure whose fields have quietly moved, so `engine_types.h` asserts it.

## Addresses are preferred-base, never runtime

Every constant in `engine_sites.h` is an absolute address in a module loaded at its preferred
base, which is how the byte-patch tooling this grew out of expressed them. Nothing may use one
directly. Convert first:

```
exe_site(0x48BEF0)        ->  host_image_base() + (0x48BEF0 - 0x400000)
rfl_site(base, 0x789A7)
```

Signature scanning is the upgrade path and is not here yet. It is worth building when a second
build exists to test it against, and worth nothing before then. `level_select` is the exception
and shows the direction.

`host_image.h` has one responsibility: where the main executable is, where its code section is,
and which directory it came from. `host_image_resolve()` is idempotent and must be called before
anything else there returns anything useful. The loader does that once from `DllMain`, which is
safe under the loader lock because it only reads PE headers that are already mapped.

## Writing to engine memory

`patch.h` exists to make three habits cheap, all of which were expensive to learn.

**Validate before writing.** `patch_write_expect` reads the current bytes and refuses when they
are not what the caller expected. That check is also what makes a patch idempotent: a second run
finds the new bytes, not the expected old ones, and declines.

**Write the whole word, not a byte of it.** Poking one byte of a little-endian immediate is how a
limit you meant to lower becomes one you raised.

**Repoint, do not assume.** `patch_repoint_operand` rewrites an instruction's 32-bit absolute
address field and refuses when the field does not currently hold the address the caller expected,
which is what stops a patch landing on a different build's operand.

`memory.h` enforces the companion rule: validate the **complete** range you are about to touch.
Checking only the first address is not enough for a structure or an array. An engine table can
start in committed memory and end past the last committed page, and the read that finds out is the
one that kills the process.

## Assembling stubs

Several fixes splice into the middle of an engine function rather than its prologue, so they
cannot use a conventional detour: the relocated instructions, the new arithmetic and the jump back
all have to be laid out together. `emit.h` does that layout. `emit_label` and `emit_patch_rel8` fix
up a short branch after the fact instead of demanding the distance in advance, and
`emit_overflowed` makes running out of buffer a checkable condition rather than a silent stack
smash. The failure mode it exists to prevent is a jump into the middle of an instruction.

## Reading the camera

`EXE_ACTIVE_CAMERA_PTR` was documented as "NULL outside a level", and three plugins were written
against that promise: `field_of_view` read the focal length through it, `hud_scaling` and
`text_scaling` generated stubs that dereferenced it on every GUI control and every glyph.

The promise does not hold on every machine. A crash log from a second install showed

```
[field_of_view] baseline focal 76.2722, horizontal 180.000 deg
```

A horizontal field of view of exactly 180 degrees means `2*atan(halfW/focal)` saturated, so
`halfW` read back astronomical: the pointer was not NULL and what it pointed at was not a camera.
A stub reading `[ebx+0x254]` off that pointer is an access violation, and a plugin writing a focal
length derived from it is a corrupt projection matrix. Both were observed.

So nothing trusts the pointer and nothing trusts the fields. `camera_read` validates before it
returns, and callers are written so that "no trustworthy camera" means "behave exactly like the
unmodded game".

The corollary is the reason this is more than a null check: engine memory must not be dereferenced
from a generated stub on a hot path at all, because a stub cannot check anything cheaply and
cannot report what it found.

**Validating a candidate.** The first dword of an object with virtual functions is its vtable
pointer, and a vtable both lives in the host image and holds addresses in the host image. Two
indirections, both checked. Checking against one known vtable address would be stronger and is
wrong: the engine has more than one camera class and only one was ever dumped. "Points at a table
of code addresses inside `Fellowship.exe`" is the strongest claim true of all of them, and it
already rejects anything uninitialised memory is likely to hold.

The last cross-check is one no individual range can make: `halfH/halfW` **is** the aspect ratio,
so it has to agree with the rectangle the camera claims to be rendering into. A camera caught half
way through `SetViewport`, with old dimensions and new halves or the reverse, passes every other
test and fails this one. Either rectangle is accepted, because the viewport and the device
disagree legitimately whenever the game renders into a sub-rect, and a factor of two of slack is
left on top. This rejects garbage; it does not police a rounding difference.

**Two ways to use it.** Some values can be sampled onto a timer. Some cannot: the pause menu
renders the world into a sub-rectangle and the camera's viewport **is** that rectangle while the
menu is drawn, so a scale sampled a quarter of a second earlier is the wrong number. For those,
`camera_track` puts a validated pointer into a variable owned by the plugin. It is zero until a
camera has passed every check, and returns to zero the moment one stops passing; a stub that finds
zero falls through and does nothing, which is the unmodified game.

## The virtual screen

The engine's virtual screen is always 128 units wide, so the authored 640x480 interface maps at
exactly 5 pixels per unit and the authored field of view is horizontal:

```
camera+0x228   halfW = 64.0                  always
camera+0x22C   halfH = 64.0 * H / W          48.0 at 4:3, 36.0 at 16:9
focal          = 64.0 / tan(fov * pi/360)
```

Several plugins need those and none of them should be spelling them out again.

## The channel

Plugins are independent DLLs. Nothing loads anything else, nothing exports anything to anything
else, and deleting one cannot break another. That is also a problem the first time two of them
want the same engine field.

`dev_menu` is that first time: its field-of-view slider and the `field_of_view` plugin both want
to write the camera's focal length, and whichever ran last would win, so a drag would be undone
400 ms later by the poll thread. They do not both write. `dev_menu` **publishes** a target and
`field_of_view` **prefers** it over its own ini value when one is present. One writer for the
camera, one writer for the request.

The block is a named file mapping rather than an exported symbol, because a mapping needs no load
order between the two DLLs: either can create it, either can open it, and a plugin whose partner
is not installed reads a block nobody ever writes to. The name carries the process id, so two
copies of the game do not talk to each other.

**The mapping is a whole page**, not `sizeof(channel_block_t)`. It used to be the structure's own
size, and adding the frame rate field showed why that was wrong: `CreateFileMappingA` on an
existing name fails outright when the requested size is larger than the existing object, so a new
DLL beside an old one could not open the block at all. A fixed page means every field added after
that one costs nothing.

**Zero means different things in the two fields, on purpose.** For the frame rate, zero is a
request: it means uncapped. For the field of view it is a withdrawal. "No cap" is a thing a person
chooses; "no opinion about the field of view" is not. A serial of zero is what means nobody has
ever published.

## Configuration

One file, `<game>\fix_enhancers.ini`, one section per DLL. A plugin passes its own section name on
every call and therefore cannot read or overwrite another plugin's key by accident. `ini.c` knows
nothing about what any key means; range checks, NaN handling and semantic validation belong to the
plugin that owns the value.

**Two file names.** It used to be `open_fellowship.ini`, and that name is still accepted when the
new one is absent, because renaming a configuration file otherwise reverts everybody who already
had one to the built-in defaults, silently, with every key simply not being found. When both
exist the new name wins outright and the old one is not read: a half-read configuration is harder
to diagnose than a wrong one.

**Inline comments.** `GetPrivateProfileString` returns everything after the `=` verbatim, comment
and all, and this project walked into that with its own documentation:

```
LogMessages=1                ; Mirrors what the engine prints...
```

comes back as `1                ; Mirrors what the engine prints...`. The numeric readers survived
it because `strtol` and `strtod` stop at the first space, which is why `KeyCode=192` with a comment
always worked. The boolean reader compared the whole string against `"1"` and quietly fell back to
its default, so **every documented boolean in the shipped ini was ignored**, which is why
`LogMessages` appeared to do nothing however many times it was set. A comment is now a `;` or `#`
at the start of the value or following whitespace, and trailing whitespace goes with it.

`ini_read_*` needs to tell "the key says nothing" from "there is no key", which
`GetPrivateProfileString` cannot do on its own, so it passes a sentinel default nobody would type.
The sentinel is split across two string literals deliberately: `"\x02absent"` would be read as the
single hex escape `\x02a`, a different character, and an error under `/WX`.

## Logging

Every module appends to `<game>\fix_enhancers.log`. The prefix is set once by `log_init` and
written in front of every line automatically, so a call site cannot forget it and two plugins
cannot drift apart in how they spell their own name:

```
[loader]      plugin hud_scaling.dll         loaded at 10000000, calling open_fellowship_install
[hud_scaling] WARNING: control_apply_scale did not match, HUD left alone
```

The loader calls `log_init("loader", true)` first and truncates; every plugin calls
`log_init("<plugin>", false)` and appends. Each line is flushed, because the interesting case is
the one where the process dies immediately afterwards.

## Waiting for the rfl

The loader calls a plugin at the host's entry point, before the CRT has run and long before the
game has loaded its game-code DLL. A plugin patching `Fellowship.exe` can work immediately; one
patching `Fellowship.rfl` cannot, because `GetModuleHandleA("Fellowship.rfl")` returns NULL at
install time.

`module_watch` is the wait. It starts one thread, polls for the module, and calls back once with
the base address. The callback runs on that thread and not a game thread, which is acceptable here
for a specific reason: the rfl is loaded during start-up, before the first frame, so the code being
patched is not yet executing. A plugin wanting to patch something already running every frame
needs a different tool.

It polls rather than hooking `LoadLibrary`, because the honest comparison is not "poll versus
elegant", it is "poll versus one more inline hook installed before the CRT has initialised".

**Seen once is not ready.** `GetModuleHandleA` answers as soon as the module is in the loader's
list, and a plugin that patches on that first sighting is racing whatever the loader has left to
do. That bit us: with a seventeenth plugin in the folder the timing shifted by one poll,
`text_scaling` won the race by 25 ms, and one of its seven sites came back "unexpected bytes" on
the same rfl, at the same base, that had installed cleanly the run before. So the module has to be
seen and then still be there, unchanged, a full settle later. Two hundred milliseconds during a
five-second load costs nothing and closes the window that produced a PARTIAL install.

## Windows or Wine

Two fixes here are wrong on Windows and necessary on Wine: the opening movies go through a runtime
Wine only stubs, and exclusive full screen behaves differently there. A default that has to be
typed into an ini is a default nobody gets, so the question is asked directly. Wine exports
`wine_get_version` from `ntdll` and Windows does not. That is the documented way to ask, it is a
fact and not a heuristic, and it is answered once and remembered.

## The plugin contract

A plugin exports exactly one function:

```c
void open_fellowship_install(void);
```

The loader calls it **after** `LoadLibrary` has returned, so it runs outside the loader lock with
the main image fully mapped. That is the entire reason the loader exists as a step separate from
`DllMain`: `DllMain` may not scan, may not read files and may not load anything, and a patch
installer wants to do all three.

A DLL in the plugins folder without that export is still loaded. The loader notes it has no entry
point and moves on, because an ordinary third-party DLL is a legitimate thing to put there.

## Compiler notes

`compiler.h` holds the places where a newer MSVC has to be told something, with the reasoning
attached, so the next person to hit one finds an explanation instead of a bare `#pragma`.

**C4702.** Every poll thread here is a `for (;;)` that never breaks with a trailing `return` that
exists only because the thread signature demands one. MSVC 19.50 proves the return unreachable and
`/WX` turns that into an error. C4702 comes from the code generator rather than the parser, so it
is attributed to the end of the function and `#pragma warning(suppress)` at the return does not
reach it. Only a whole-function region does, which is what `OF_NORETURN_THREAD_BEGIN` and
`OF_NORETURN_THREAD_END` are. Deleting the return and marking the function `__declspec(noreturn)`
does not work, because `LPTHREAD_START_ROUTINE` requires the DWORD return and a noreturn function
cannot be assigned to it without a cast that hides more than it explains.

**Calling engine methods from C.** MSVC accepts `__thiscall` on a function-pointer typedef in C++
but not in C; under `/permissive-` with C11 it is not a keyword at all and a declaration using it
stops parsing at the `*`. So the convention is spelled `__fastcall` with a dead second parameter,
which on x86 is exact rather than approximate:

```
__thiscall (this, a, b)          this -> ECX, a and b on the stack, callee cleans
__fastcall (this, dead, a, b)    this -> ECX, dead -> EDX, a and b on the stack, callee cleans
```

Same register for `this`, same stack slots for the real arguments, same cleanup. EDX is
caller-saved and `__thiscall` never reads it, so the value put there is discarded. Pass
`OF_THISCALL_EDX` to say that at the call site instead of leaving a bare NULL to be wondered at.
There is no macro for the declaration itself, because spelling `__fastcall` out where it is used is
clearer than hiding a calling convention behind a name.

## Frame timing globals

The engine object at `0x00543280` holds the numbers the whole game reads for time, and the Timer
instance at `0x0053EE58` produces them. `plugins/frame_timing` has the full account.

Neither the delta nor the frame rate has a write anywhere in the image, and that is not a puzzle:
both are written through pointers. `UpdateTime` at `0x00408F00` does `lea edi,[esi+4]` and hands
that to `Timer::Tick`; `0x00409000` does `add ecx,0x14` and hands that to `Timer::GetFramerate`.
Searching for a store to either address finds nothing at all. Read only from this side, and the
delta is overwritten at the top of every frame, so writing to it achieves nothing that survives.

## The renderer and the device

Established from the engine's own call at `0x0047BDDD`:

```
mov  ecx,[0x54743C]        the renderer
mov  eax,[ecx+0x166]       -> IDirect3DDevice8*   (an unaligned field, but its own)
mov  edx,[eax]             its vtable
call dword ptr [edx+0x8c]  EndScene, index 35
```

The executable imports exactly one Direct3D symbol, `Direct3DCreate8`, and calls `+0x3C`, `+0x88`
and `+0x8C` on that same object: `Present`, `BeginScene` and `EndScene` at indices 15, 34 and 35.
Three hits on the published ordering at three different indices fix the interface.

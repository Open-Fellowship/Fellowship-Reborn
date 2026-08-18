# env_probe

**Produces:** `env_probe.dll`. **A diagnostic. It changes nothing.** On by default.

A black screen on somebody else's machine is otherwise a conversation of guesses. The log says
every plugin installed, the game says nothing at all, and the two facts that would settle it are
invisible from both ends: which Direct3D 8 is actually in the process, and what happened when the
game asked it for a device.

## What it writes down

**The platform**, at install time. Wine exports `wine_get_version` from `ntdll` and Windows does
not, so this is a fact rather than a guess, and the Proton and Steam environment variables come
with it:

```
[env_probe] running under WINE 9.0 - Proton, a Steam Deck, or a Linux desktop
[env_probe]   SteamDeck                1
[env_probe]   SteamGameId              12345
[env_probe]   PROTON_USE_WINED3D       1
[env_probe]   4 processor(s), 15854 MB of memory
```

**Which d3d8 it is**, once the game loads it. `wined3d`, DXVK's `d8vk` and the community wrapper
are three very different things wearing the same file name, and the path tells them apart. A
`d3d8.dll` found in the game folder gets a warning of its own, because under Proton that is a
translation layer stacked on a translation layer and a common cause of exactly this symptom.

**What the game asked for, and what it got.** This is the one that matters:

```
[env_probe] CreateDevice: adapter 0, type 1, 1280x800 X8R8G8B8, FULLSCREEN, refresh 0, flags 00000040
[env_probe]   depth D24S8, 1 back buffer(s), swap effect 1, multisample 0
[env_probe]   -> D3D_OK, device 0A41C220
```

and when it fails, which is the case worth catching:

```
[env_probe]   -> D3DERR_NOTAVAILABLE (8876086A). The game has no device to draw into; this is
                what a black screen looks like from in here.
[env_probe]   it was asking for a FULLSCREEN mode. Try [windowed_res] Enabled=1 with your
                screen's size, or switch [resolution_unlock] off so the game stops offering modes
                the driver will not give it.
```

A refused device leaves the game running with nothing to draw into. From outside that is a black
screen and no other symptom - no crash, no message, the process still alive. It is the single
most useful line this project can produce about a machine nobody here owns.

**The window the device is attached to.** A windowed device asked for `0x0` is not a mistake: it
means "the size of the window". So the window is the number that matters, and a window with an
empty client area is a black screen with a perfectly healthy device in it:

```
[env_probe]   focus window   00020062, client 1280x800, window 1280x800 at 0,0, visible
```

**Whether frames are actually being produced.** `Reset` is where a windowed device becomes a
fullscreen one and is the second place a device can be lost; `Present` is the proof that the game
is drawing at all. Both go through the device vtable, slots 14 and 15:

```
[env_probe] first Present -> D3D_OK. The game is drawing.
[env_probe] 600 frames presented, still D3D_OK
```

**And when the frames stop.** A log that just stops is ambiguous, and the case worth catching
leaves no line behind on its own - `Present` cannot report that it was not called. So a thread of
our own ticks every five seconds for the life of the process, says nothing while the frame counter
is moving, and speaks up when it stops. It reports twice, at the start of the stall and thirty
seconds in, and re-arms if frames start again, so a wedged game says so twice rather than five
hundred times:

```
[env_probe] 5 seconds after the device was created and NOT ONE frame has been presented. The game
            is not drawing at all - it is stuck before its render loop rather than drawing into
            something invisible.
[env_probe]   window now    00020062, client 1280x800, window 1280x800 at 0,0, visible
[env_probe]   main thread is still pumping messages, so the process is alive and waiting on
              something else
```

and when frames were flowing first, it says that instead, which is a different bug entirely:

```
[env_probe] 812 frames presented and then nothing for 5 seconds. The game STOPPED drawing rather
            than never starting, so whatever went wrong happened after the device was working.
```

That `main thread` line is `WM_NULL` with a one second timeout, which only completes if the game's
main thread is running its message loop. Alive and pumping but not drawing is a different bug from
alive and blocked, and nothing else in the log tells them apart.

**Who has the foreground, by name.** Once the game's own window turns out to be minimised, the
only question left is what took the focus away from it, so the foreground window is named rather
than numbered:

```
[env_probe]   window now     0002009E, client 160x24, window 160x24 at -32000,-32000, minimised
[env_probe] ERROR: the game's own window is MINIMISED. A fullscreen Direct3D device takes its
            window down with it when it loses the foreground, and this engine stops drawing while
            it is minimised.
[env_probe]   foreground window 00010020, class "...", title "...", process 42 - another process
```

`-32000,-32000` is where a minimised window lives, and a 160x24 client area is a title bar. Both
are worth recognising on sight.

**And where that thread actually is.** A game that answers messages and does not draw is waiting
for something and will not say what, so the stall report asks the only witness there is - the
instruction pointer of the thread that presented the last frame:

```
[env_probe]   the drawing thread is at ntdll.dll+04A1C3
[env_probe]     on the stack: winmm.dll+00B2F0
[env_probe]     on the stack: Fellowship.exe+0BCA1E
[env_probe]   a second later it is at ntdll.dll+04A1C3
```

Addresses are named module plus offset so they can be looked up in the binary without knowing
where it loaded, and the sample is taken twice a second apart, because a thread sitting still and
a thread going round a loop are different bugs and one sample cannot tell them apart.

The thread is suspended only long enough to copy its registers. Nothing is read and nothing is
logged while it is stopped: a thread frozen inside the runtime's own lock, by a diagnostic that
then wants that lock, is how a diagnostic becomes the bug. The stack is walked after it is
running again, which is safe precisely because this code only runs when nothing is moving. The
frame pointer chain is tried first and the stack is read for anything that looks like a return
address when there is no chain to follow, which is the usual case in a release build.

## How it attaches

`Direct3DCreate8` is hooked **at the import slot**, by walking the host's import directory rather
than by an address, because the game calls it exactly once and the slot is the only place that is
reliably true of. The returned `IDirect3D8` then has one vtable entry replaced, `CreateDevice` at
slot 15, which logs and forwards.

Both are COM vtable positions rather than addresses in this game, so neither depends on the build
and both are correct against wined3d, DXVK and the retail Microsoft runtime alike.

The adapter is asked to identify itself before anything is hooked, through `GetAdapterIdentifier`
at slot 5, which is how wined3d and DXVK end up naming themselves in the log.

The device that comes back has two of its own entries replaced the same way, `Reset` at 14 and
`Present` at 15, which is why nothing is hooked until there is a device to hook.

## Configuration: `[env_probe]`

| Key | Default | |
|---|---|---|
| `Enabled` | `1` | on by default: the machine that needs it is always somebody else's |

# dev_menu

**Produces:** `dev_menu.dll`. Draws an overlay. On by default, but inert until you press the key.

An in-game menu for the values that are a matter of taste rather than a bug. Field of view is the
first, because deciding what you like by editing an ini and restarting is a poor way to decide.

Toggle: **the key immediately under Escape** (`VK_OEM_3`, backquote on US and UK layouts).
The game's own cheats are F5-F12 and `fog_toggle` took F1, so that position was free.

## Nothing is hooked until you open it

The Direct3D hook is installed on the first press of the toggle key, not at startup. An install
where nobody ever opens the menu is one where this DLL read an ini and started a thread. That is
a deliberate property: it means adding this plugin cannot change how the game renders unless you
ask it to.

## Finding the device

The engine hands it to us. From `Fellowship.exe`:

```
0047BDDD  mov  ecx, dword ptr [0x54743c]    the renderer
0047BDE3  mov  eax, dword ptr [ecx + 0x166] -> IDirect3DDevice8*
0047BDEA  mov  edx, dword ptr [eax]         its vtable
0047BDEC  call dword ptr [edx + 0x8c]       EndScene
```

The executable imports exactly one Direct3D symbol, `Direct3DCreate8`, and calls `+0x3C`, `+0x88`
and `+0x8C` on that same object, indices 15, 34 and 35, which are `Present`, `BeginScene` and
`EndScene` in the published `IDirect3DDevice8` ordering. Three hits at three different indices is
what fixes the interface; one would have been a guess.

Before anything is called through it, `find_device` checks: the renderer pointer is readable, the
device pointer is readable, the vtable is readable for every entry this plugin uses, **each entry
is a readable address**, and **all of them live in the same module**. A structure that merely
happens to begin with a plausible pointer fails the last two.

The hook itself is one aligned pointer-sized store into the vtable, which is atomic on x86, a
render thread calling `EndScene` at that instant gets either the old function or ours, never half
of each.

## Why there is no texture

The obvious way to draw text in D3D8 is a font atlas: glyphs in a texture, one textured quad per
character. That means `CreateTexture`, a lock and an upload, texture stage state, a pool choice,
and code to rebuild all of it after a device `Reset`, a lot of surface area for a dev menu, and
every bit of it a new way to break the game's rendering on a machine we cannot test.

So the glyphs are rasterised **once**, with GDI, into plain bitmasks, at the moment you first open
the menu and on our own thread. Drawing turns each row of each glyph into one rectangle per *run*
of lit pixels, and the entire overlay goes out as a single `DrawPrimitiveUP` of pre-transformed
vertices. Nothing is allocated on the device, so there is nothing to lose when the device resets.

Every render state it sets is read back first and restored afterwards, twelve states, four
texture stage states, the texture and the vertex shader. The game is mid-frame when we draw;
leaving alpha blending on would corrupt whatever it drew next, and the symptom would look nothing
like this plugin.

## Who writes the camera

Not this plugin. The slider **publishes** a target through `common/channel.h`, and
`field_of_view` applies it, keeping one writer for the camera and one for the request. Two
plugins writing the same field on two different timers is the fight the loader exists to make
unnecessary: without this, a drag would be undone 400 ms later by the poll thread.

`automatic` hands it back. `field_of_view` then returns to whatever it was doing before, with no
restart, and logs the handover both ways.

**If `[field_of_view] Enabled=0`, the slider has nothing to drive.** The menu still opens and
still reports what the camera reads back, so the live focal length tells you immediately whether
anything is listening.

## Input: three attempts, and why it ended at DirectInput

Two approaches were tried on real hardware before this one, and both are worth recording because
both looked right.

**`GetCursorPos`.** The menu drew perfectly and could not be clicked. That is the documented
behaviour, not a bug: the game acquires the mouse through DirectInput in **exclusive** mode, and
an exclusively acquired mouse stops moving the system cursor and stops generating window
messages. `GetCursorPos` returns the same frozen point forever.

**Raw input.** `WM_INPUT` sits underneath DirectInput, which is exactly why it looked like the
answer. It also failed, for a reason that is easy to miss: **only one window per raw input device
class per _process_ may be registered**, and DirectInput8 registers the mouse itself inside the
game's process. Its registration replaces ours and the messages never arrive.

**Our own DirectInput device**, which is what ships. Exclusive access by one application prevents
other applications acquiring *exclusively*, and nothing else, so this plugin opens its own mouse
and reads the same hardware the game reads, through the same API, without taking anything from
it. It is opened when the menu opens and released when it closes.

It asks for **exclusive** first. If the game is not holding the mouse exclusively we get it, and
the game stops seeing the movement while the menu is up, which is what a menu should do. If it
is, we share, and the game keeps reacting to the same movement. The menu says which one it got:

```
mouse   DirectInput (exclusive)      the world stays still while you drag
mouse   DirectInput (shared)         the world moves too
```

Raw input is kept as a second string, because it costs nothing and a different setup may not have
the registration conflict.

Nothing here comes from an SDK. The GUIDs are the published DirectInput values and the data
format is a documented structure; `dinput8.lib` would supply both, but declaring them keeps this
buildable against nothing but the Windows SDK, the same reasoning as `d3d8_min.h`. The three
structure sizes are asserted at compile time, because a data format of the wrong size is rejected
by DirectInput with no clue as to why.

## Muting the game while the menu is open

Asking DirectInput for the mouse exclusively is the polite way to stop the game seeing it, and on
this game it does not work: the game got there first and holds it exclusively itself, so our
request is refused and we share. Sharing means the menu works *and* the world swings around
underneath it, which is worse than either extreme.

So the game's own reads are silenced at the source. `Fellowship.exe` imports exactly **one**
symbol from `DINPUT8.dll`, `DirectInput8Create`, by name, at `0x005675A8` in `.idata`, and a
plugin installed at the entry point runs long before the game calls it. Rewriting that import
slot gives us the interface the game is handed; the interface gives us `CreateDevice`; and
`CreateDevice` gives us the mouse device itself. From there one vtable entry decides whether the
game hears anything.

```
DirectInput8Create  ->  IDirectInput8::CreateDevice  ->  the mouse device
                                                          GetDeviceState  zeroed while open
                                                          GetDeviceData   emptied while open
```

Three details that matter:

* **Everything forwards.** While the menu is closed the game reads its mouse exactly as it always
  did. The cost of the intercept existing is a jump.
* **The check is on the device, not the vtable.** DirectInput gives every device of a class the
  same vtable, so silencing the vtable outright would take the keyboard with it, and our own
  mouse, which comes from the same DLL.
* **`GetDeviceData` is forwarded and *then* emptied**, never skipped. The buffered events still
  have to be drained or DirectInput's queue overflows, and the game would fault the moment the
  menu closed.

This is the one part of the plugin installed at startup rather than on first press, and it has to
be: the game calls `DirectInput8Create` once, early, and a slot rewritten afterwards is a slot
nobody will read again. Set `TakeMouse=0` to skip it entirely.

The panel says which state it is in:

```
mouse   DirectInput (shared)   game muted
mouse   DirectInput (shared)   game still reading
```

Arrow keys nudge by 0.25 deg and always work, whatever the mouse is doing.

## Configuration: `[dev_menu]`

| Key | Default | |
|---|---|---|
| `Enabled` | `1` | the hook is still not installed until you press the key |
| `KeyCode` | `192` | `VK_OEM_3`, the key under Escape. A raw virtual-key code |
| `FontHeight` | `16` | 10-32. Raise it at 4K |

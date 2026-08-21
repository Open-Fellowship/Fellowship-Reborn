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

## What DrawPrimitiveUP does behind your back

Worth its own heading, because it cost a debugging session and the symptom pointed nowhere near
the cause: the lights and lens flares in Hobbiton blew up into white blobs whenever the overlay
was on screen, and the game eventually crashed.

`IDirect3DDevice8::DrawPrimitiveUP` **sets vertex stream 0 to NULL when it returns**, and
`DrawIndexedPrimitiveUP` does the same to the index buffer. It is documented and it is easy to
miss, because nothing about drawing a few untextured quads suggests it would unbind the caller's
geometry.

This engine binds its stream once and reuses it across draws. So every draw after the overlay's,
in that same frame, ran with no stream bound and drew whatever the runtime happened to have -
which is exactly what a light sprite becoming a screen-filling white blob looks like.

Both are saved before the draw and put back after, and the references the getters add are
released. The render states, the texture stage states, the stage 0 texture and the vertex shader
were already handled; this was the one that was not.

The general lesson for anything else drawn into this game: it is not enough to restore the states
you SET. You have to restore the ones the draw call itself changes.

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

## The cheats

The engine has a full debug menu that no shipping build's UI reaches, and every entry in it ends
in the same two instructions:

```
00411C50   mov  ecx,[0x544070]        the command object
           push 0x52F630              "fly"
           mov  eax,[ecx]             its vtable
           call [eax+0x68]            __thiscall (this, const char *command)
```

Eight commands, one call site shape, one object, one slot. So the buttons here do not write a
flag or set a health value: they ask the game in its own words, exactly as its own debug menu
does.

| button | command | |
|---|---|---|
| Fly | `fly` | toggle |
| Invincible | `tim` | toggle |
| Kill enemies | `mrclean` | one shot |
| Full health | `heal` | one shot |
| Drop | `drop` | one shot |
| Invisible walls | `invisowalls` | one shot |
| Suicide | `bye` | one shot |

Which two are toggles is taken from the engine rather than from taste. In the debug menu's own
handler, `fly` and `tim` are the only entries followed by

```
xor ecx,ecx / test esi,esi / sete cl / mov esi,ecx
```

which is a displayed state being flipped. The other six just fire.

**"on" and "off" are what was last sent, not what is true.** The command is fire-and-forget and
the engine offers no way to ask, so the menu says believed rather than pretending to know. Load a
save with fly already on and the button will disagree with the game until it is pressed once.

Teleport is deliberately absent. Its command is `tele %d %d %d` and it needs three coordinates,
which is a text field, and this menu has no text field yet.

### Why this is safe to call

The string is not retained by the callee, and that is not an assumption. One of the eight sites,
teleport at `0x411C93`, formats into a **stack buffer** and passes a pointer to it, which a
callee that kept the pointer would be reading long after the frame died. A string literal from
this DLL is therefore fine.

The call happens inside the EndScene hook, which is the game's own thread inside its own frame.
That is the only reason calling engine code from a plugin is reasonable here at all; a button
handled on a key-polling thread would be calling into the engine from underneath it.

Before every call the object pointer, its vtable, the slot and the target are each read and
checked, and the target has to be code inside `Fellowship.exe`. The buttons are dead, and drawn
dead, until a level is loaded and all of that passes. This is the same rule the camera work
landed on after an unvalidated global crashed a machine that was not the development one.

### The hotkeys are gone

`Fellowship.dll` bound these to F5 through F12 under `EnableCheatHotKeys`. Nothing in this
project binds them, so with that DLL out of the folder the function keys do nothing and the menu
is the only way in. Worth knowing: that same option is what applies the level-select rfl edit in
Blank's DLL, so switching it off there also switches that off. `level_select` in this project
does that edit on its own.

## The engine flags page

The second tab is the game's **own** developer menu: 124 flags that the retail executable
registers at run time, names and all, and that no shipping UI reaches.

```
0x5449A8            the object, in the executable's data. Not a pointer to chase.
  + 0xD4    ->      const char *names[124]
  + 0xE0    ->      int32_t     values[124]
0x411BA0            getter, __thiscall (index)
0x411800            setter, __thiscall (index, value)
0x40FE30            where they are registered and defaulted
```

The names come out of the engine every frame rather than from a table copied into this plugin, so
they are right by construction and a flag the game changes by itself is shown changing. Six of
them (71, 95, 96, 99 to 101) are registered but never named, and read as unnamed rather than as
a string at address four.

### Pressing a row presses the entry

Clicking a row does what the game's own menu does when you press that entry, through the same
dispatcher:

```
00411BC0   mov  edi,ecx                    the flag object
           push ebp                        the index
           call [[edi]]                    getter: the current value
           cmp  ebp,0x6F
           movzx ecx, byte [0x4120C0+ebp]  a case number per entry
           jmp  [0x41205C + ecx*4]         25 cases
00412046   push esi / push ebp             the common tail:
           call 0x411800                   SetFlag(index, newValue)
```

Twenty-five cases covering a hundred and twelve entries, and reading them is what turns this page
from numbers into controls:

| case | entries | what pressing it does |
|---|---|---|
| `0x41202A` | 67 | the default: value becomes its opposite. A plain switch |
| `0x411FBD` | 21 | switch, **and sets flag 0x39 with it** - the statistics rows, which do not display at all without their master flag |
| `0x411F2E` `0x411F57` `0x411F8A` | 3 | wireframe, strips, render groups: a switch that also sets 0x0F, 0x39, 0x3D and 0x45 around it |
| `0x411DA5` `0x411DB6` | 0, 51, 16 | cycle 0 to 2 |
| `0x411BF2` | 1 | cycle 0 to 6 |
| `0x411C03` `0x411FD2` `0x411DC7` | 2, 47, 108 | cycle 0 to 3; 47 also writes `0x543434`, the one value the getter special-cases |
| `0x411D28` `0x411D56` `0x411D84` | 88, 89, 90 | the profiler switches, each clearing the other's global |
| `0x411FE2` | 23 | hardware lighting: switches, then talks to the device |
| `0x411E4A` | 59 | takes a screenshot. An action; the value is not the point |
| eight command cases | 95-107 | the cheats, sending the same command strings as the buttons on the other page |

That table is why "set the number to 1" was the wrong primitive. Twenty-one of these entries need
a second flag set alongside them, and three more need four.

So a row is a name and a switch, the same shape as the cheat buttons: **on** in green, **off**,
or **run** for the eight that fire something rather than hold a state. Pressing it calls the
dispatcher and the engine decides what that means.

### Three entries are numbers, not switches

99, 100 and 101 are X, Y and Z: the destination the **Teleport** entry (98) reads when it builds
`tele %d %d %d`. They are the only entries here holding a number that means something in the
world rather than a mode, and the dispatcher's default case would flatten one to 0 or 1 - which
is what pressing the row used to do, throwing the coordinate away.

So those three rows have a field instead of a switch. Click it and type; Enter sets it, the `-`
and `+` boxes nudge by one, and the value is not written to the engine until Enter, so a
half-typed number never reaches it. Set the three, then press Teleport.

**And they are not in the values array.** Setting `values[99..101]` and pressing Teleport does
nothing at all, which is what happened the first time. Teleport reads three fields out of the
flag object itself:

```
00411C7A   mov eax,[edi+0x11C]        edi is the flag object, 0x5449A8
           mov ecx,[edi+0x118]
           mov edx,[edi+0x114]
           push eax / push ecx / push edx
           push 0x52F618              "tele %d %d %d"
           push <stack buffer>
           call sprintf
           call [edx+0x68]            the command object, as every other cheat does
```

Arguments push right to left, so the first `%d` is `+0x114` and the last is `+0x11C`: X, Y, Z in
that order. The object is static, so those are three fixed addresses - `0x544ABC`, `0x544AC0`,
`0x544AC4` - and the values array never enters into it. The fields read and write there, which
means what the menu shows is the number Teleport will actually use.

Escape does cancel the edit, but it also opens the game's pause menu, so clicking the field again
is the better way out. The game still sees the keystrokes while you type - the menu mutes the
mouse, not the keyboard - which is worth knowing if a digit is bound to something in your setup.

### Seven entries hold a range

Those get a second line underneath with the value and a pair of steppers:

```
  1  Messages Colors                              on
     3 of 0-6                                  -   +
```

Those steppers are the raw number, through `0x411800` rather than a write into the array, since
that function runs its own per-flag side effects. They wrap at the range the engine's own case
uses, so they cannot leave a flag holding a value it has no case for.

Rows are therefore not all the same height. The page is laid out once per frame into a table that
the drawing and the hit testing both read, and the layout runs in the input pass as well, because
input happens before the draw and a page that has just changed would otherwise be clicked at the
previous page's rectangles for one frame.

Worth knowing before hunting a flag that appears to do nothing: several are PS2-era leftovers
this build never reads. They still toggle, because the default case toggles everything; nothing
looks at the result. `_FixEnhancers/docs/02` names 10, 11, 12 and 14 among them.

The page shows 48 flags at a time, in two columns, with the row count following the window
height. The vertex batch in `overlay.c` was raised from 8,000 quads to 48,000 for this: a page of
this list is upward of a thousand glyphs, and a glyph is one quad per run of lit pixels per row.

## The engine messages box

Turning **Engine Debug Messages** on and seeing nothing happen is not a broken flag. The engine
prints plenty; on this build the object it prints to draws nothing. The text is real, the sink is
not.

So the sink is borrowed. Everything the engine prints goes through one object held at
`0x543784`, and always in one of two shapes:

```
004040EB   mov  eax,[0x543784]
           push 0x52E720            "RIOT Engine core initialized."
           push eax                 the object, as the FIRST STACK ARGUMENT
           mov  ecx,[eax]
           call [ecx+8]             print(self, text)

0040B88D   push edx                 an argument
           push 0x52E9D8            "Object Totals: (%d objs)"
           push eax                 the object again
           call [ecx+0x20]          printf(self, format, ...)
           add  esp,8               the caller cleans up
```

Sixty-five call sites between those two entries. `this` goes on the stack rather than in `ecx`
and the caller cleans up, which is how MSVC compiles a member function taking varargs - and it is
what lets both be hooked with ordinary C functions, no naked thunks and no assembler.

Both are replaced in the vtable, the text is kept in a ring of the last 256 lines, and the
original is called afterwards, so the engine still does whatever it did. The variadic one is
forwarded as an already-formatted string, since C gives no portable way to pass varargs along.

**The formatting is ours, not the CRT's.** Handing the engine's format strings to `vsnprintf` is
not safe: they are this engine's own, several carry a leading control byte, and any conversion
the CRT reads differently from the way the engine's own printf reads it means a value taken as a
pointer and dereferenced. So the walk is written out here - known conversions only, one at a
time, every `%s` pointer checked for readability before it is touched, and anything unrecognised
copied through as literal text **without consuming an argument**. Guessing at an unknown
conversion is how the rest of a line turns into garbage, or worse.

### It is NOT driven by flag 0

Flag 0, "Engine Debug Messages", turns on the engine's **own** message display, and that display
is broken on this build: it corrupts the lighting and then takes the game down. Measured the hard
way, with the box wired to it.

Capturing does not need that flag. The hooks see every message whatever it says, so the box has
its own switch in the menu's top bar - `engine messages: on` - and flag 0 is left at 0. Turning
the box on also sets flag 0 back to 0 if something else set it, so a stray click on the flags
page cannot bring the broken display back while the box is up.

### Capture starts at start-up, not when the box opens

Nearly everything this engine prints, it prints **while loading**. The first version hooked at
the moment the box was switched on, which meant every interesting line had already gone past and
the only way to see anything was to quit to the main menu and load again.

So capture and display are separate. The hooks go in from the poll thread as soon as the engine
has an object to hook, long before anybody presses anything, and the box is just a view of a ring
that has been filling since the game started. `CaptureMessages=0` in the ini turns the capture
off entirely for anyone who wants nothing hooked.

`LogMessages=1` is the same ring written to `open_fellowship.log` as well, and it exists for the
case the box cannot serve: a machine where the screen never comes on. A box you cannot see is no
help at all in working out why you cannot see it, and what the engine said in the seconds before
it stopped is usually the whole answer. It is off by default because it is a great deal of text,
and the per-frame statistics are left out of it deliberately - at sixty frames a second they would
be the entire file within a minute.

### Four slots, and slot 0 is the one that matters in game

`0x00`, the object's first virtual method, is the **per-frame** printf, and missing it is why
turning on "Display Num Lights" changed nothing while the loading messages arrived perfectly:

```
0041398F   mov  eax,[0x543784]
           fstp qword ptr [esp]      the frame rate, as a double
           push 0x52F838             "FPS: %5.2f"
           push eax
           call [ecx]                <- slot 0, not 8, not 0x20

00413A0E   push 0x52F818             "TEX: %dkb/%dkb"
           call [edx]

00413A93   push 0x52F7F4             "XYZ: %d,%d,%d"
           call [edi]
```

Every statistics row the debug flags switch on goes through it, once a frame each. That is what
makes those flags visible at all on a build whose own display draws nothing.

It also revealed that the statistics are **not events and must not be logged as such**. The
engine prints its whole information block every frame whatever the flags say - the flags decide
what its own display DRAWS, not what it prints - so as a scrolling list it is thousands of copies
of the same eight lines, and every real message is buried within a second of the game starting.

So slot 0's lines go into a **live table**, keyed on the text before the colon, each row replaced
in place as it arrives. `FPS: 57.14` overwrites `FPS: 61.02` rather than joining it. Everything
else goes into the ring, which stays a log of things that happened.

Putting the statistics into the ring as well was tried and taken back out. It prints the same
eight values twice, once as live rows and once as a waterfall underneath, and of 15,554 lines in
the ring roughly 15,000 were the frame rate. The channels can mute them, but the default has to be
the useful one.

The cost, stated so it is not a mystery later: a one-off line arriving through slot 0 lives only
as long as its live row, a second and a half. Nothing seen so far arrives that way.

A live row that stops arriving disappears after a second and a half, so a display flag going off
removes its row rather than leaving the last value sitting there for ever.

### The channels page

Capture stays indiscriminate and the CHOOSING happens in the menu, on a third tab.

Every line is filed under the text before its colon - `FPS`, `TEX`, `Waypoint`, `Behavior
Changer`, `Loading Objects from save file` - and each of those keys becomes a channel with its
own switch and a count of how many times it has been seen. The box shows a line only if its
channel is on.

A channel that is switched **off is not recorded at all**, not merely hidden, so muting something
noisy frees the ring rather than just the view. The header counts both: `seen` is everything that
came past, `kept` is what is in the ring.

**The list is not hard-coded.** It builds itself from what the engine actually prints, so a
message this project has never seen still turns up with a switch beside it, and playing a
different level grows the page. New channels start visible; `all` and `none` are at the bottom.

This is the right place for the decision. The engine's own flags decide what the engine tries to
DRAW, and on this build that answer is mostly nothing; these switches decide what WE draw, out of
everything it prints, and that is a question the engine never had an opinion about.

The box draws at double the menu's text size and is wider, because it is read from a normal
sitting distance while playing rather than leaned into like the menu.

### The other slots

`0x0C` is a second printf on the same object and it is the one carrying the warnings:

```
00404070   mov  eax,[0x543784]
           push 0x52E740          "Input initialization failed"
           push eax
           call [ecx+0xC]

0042B6EB   push eax               the index
           push 0x537A78          "LOAD/SAVE: Invalid Object Pointer Table Index: %d"
           push ecx
           call [edx+0xC]
```

"RFL initialization failed!", "Run always list is corrupted", "An object which has been freed is
trying to be saved!" all go through it. Missing it meant missing exactly the lines worth having.

Slots `0x14` and `0x18` are **not** text - `0x14` takes two numbers and `0x18` takes none; they
are the display's own positioning and clearing. Left alone.

The box draws at the bottom of the screen **whether the menu is open or closed**, because a log
you can only read while a menu covers the game is not much of a log. With the menu open there is
a clear button; with it closed the box is display only.

One ordering note: the message ring is written from whichever thread is loading or running and
read on the render thread, so it is the one place in this plugin that takes a lock. It is held
for a `memcpy`.

The hook needs the menu to have been opened once in the session, because that is when the overlay
hook and the font exist at all.

## The open fellowship page

Two sliders, each with a reset. **Height** scales the player up and down, **Width** scales it
across on top of whatever height is set. The camera holds its distance through both and the mouse
still moves it normally. With both at 1.00 the plugin writes once to restore and then stops
touching the object, so an idle page costs the game nothing.

Everything on the other three tabs asks the engine to do something it already knows how to do: the
cheats are its own commands, the flags are its own debug menu. This one does not. The engine has no
notion of a character's size, so this page reaches into the player's object and writes to it, which
is a heavier thing than anything else in this plugin and is why every step is validated on every
call.

### Finding the player

`Fellowship.rfl` has three near-identical getters at `0x1005e9d0`, `0x1005ea50` and `0x1005eb40`
whose first `0x5a` bytes are byte for byte the same, and that shared head is the lookup:

```
MOV EAX,[0x101326cc]        the level's object manager
MOV EBX,[EAX + 0xb8]        the local player's game object
MOV SI,[EBX + 0xc]          its ObjectDef index, 0xffff for none
MOV EDI,[0x101326e4]        the ObjectDef entry list
CMP ESI,[EDI + 0xc]         against its count
MOV ECX,[EDI + 0x4]         its entry array
LEA EAX,[ESI + ESI*0x8]     index * 9
LEA EAX,[ECX + EAX*0x4]     ... * 4, so a 36-byte stride
CMP [EAX + 0x4],0x1000e     the entry's class id: Player
```

`0x1000e` is the id the ObjectDef table gives the class named `Player`, so the last line is a real
identification rather than a hopeful cast. Every step is revalidated on every call and nothing is
cached, because the object moves between levels and a stale pointer here is a crash.

One instruction is deliberately not reproduced. The original calls `[[EDI]+0x10]` on the entry list
before indexing it and discards the result. This only reads the class id, and calling into the
engine to satisfy a read that is already being validated would add the risk the module exists to
avoid.

### The object's transform

```
+00EC   the world position, in world units
+00F8   a packed 3x3 transform, rows at +F8 / +104 / +110
+011C   three floats, always exactly (1, 1, 1)
```

Scaling the 3x3 scales the model. There is no property for size anywhere in the 4,262 the ObjectDef
table defines, and the debug command object at `0x544070` accepts exactly eight commands, none
about size, so writing this matrix is the only way in.

The rows are basis vectors and row 1 is the up axis: it reads `(0, 1, 0)` in every sample while
rows 0 and 2 turn as the player turns. So row 1 carries height and the other two carry width, which
is what makes a wide character possible rather than only a big one.

There is an ambiguity here that deliberately does not matter. Whether the rows are the object's
basis vectors or the world's is not established, and a yaw-only matrix cannot tell them apart. Both
horizontal rows always get the same factor, so the two readings produce the same shape. It would
only matter if width were split into separate front-to-back and side-to-side numbers, which is a
thing to establish first rather than assume.

### Why the camera does not follow

The camera sits at `playerPos + playerMatrix * (0, TrackHeight, -TrackDist)`, so scaling the matrix
scales that offset and the view zooms in and tilts down as the player shrinks.

**The three floats at `+0x011C` are a camera-distance multiplier, not a model scale.** Writing them
moves the camera and leaves the model alone, which is the opposite of what their position after a
rotation matrix suggests. They sit permanently at `(1, 1, 1)` because nothing in the shipped game
ever writes them. Which gives:

> The camera's distance is proportional to the transform's scale multiplied by the vector at
> `+0x011C`, per axis.

Per axis matters. The `TrackDist` term rides on row 2, a horizontal row carrying width, while the
`TrackHeight` term rides on row 1 carrying height. Two different scales, so one reciprocal cannot
cancel both. The vector gets `(1/width, 1/height, 1/width)`, each axis the reciprocal of whatever
scaled it. Neither write touches direction, so the mouse still swings the view around normally.

This is a property of every object in the engine, not just the player.

### What the value block is not

`TrackDist` and `TrackHeight` are Player ordinals 77 and 78, authored properties with defaults of
2000.0 and 500.0. Dividing them would be the obvious way to hold the camera still, and it does not
work: the entry's value block is at `+0x08`, which is established, but its internal layout is not.

| attempt | result |
|---|---|
| index it as `block + ordinal * 4` | `0.0` for both properties |
| search it for the schema defaults, 2000 and 500 | neither present in 4 KB |
| search it for any adjacent (distance, height) float pair | no pair in 2 KB |

The third settles it. Because the two ordinals are adjacent, a flat four-byte layout puts their
values four bytes apart wherever the block begins, so that search assumed nothing about the base
and still found nothing. The block is not a flat array of floats. The accessor that would settle it
properly is in `Fellowship.exe` at `0x44E6E0`, the one `hud_probe` hooks.

### Other fields in the same object

Found while looking for the transform, and recorded because they are the kind of thing somebody
will want next:

| | |
|---|---|
| `[+0C8] +00A0` | a rotation about X, roughly 10 degrees. `+0xC8` is the Player subobject, so this is the upper-body pitch |
| `+0x0A78` | a yaw that swings continuously while the body's changes only when you turn. The camera |
| ten matrices at a 0x130 stride | identical, axis-aligned. An array of bones or attachment points |
| `[+024]`, `[+078]` | live unit quaternions. Animation data |

### Limits

Non-uniform scaling does not renormalise normals, so lighting on an extreme build is slightly off.
That is inherent to the technique.

Collision does not follow the render transform. A very wide character still fits through a normal
doorway.

### Safety

The write happens from inside the EndScene hook, on the game's own thread in its own frame, and the
range is checked committed and writable first. The matrix is renormalised before being scaled, so
applying it every frame is a hold rather than a multiplication, and it survives the engine
rewriting the matrix from animation. A row that has collapsed means this is no longer an
orientation matrix, so the module stops rather than writing into whatever replaced it.

## Configuration: `[dev_menu]`

| Key | Default | |
|---|---|---|
| `Enabled` | `1` | the hook is still not installed until you press the key |
| `KeyCode` | `192` | `VK_OEM_3`, the key under Escape. A raw virtual-key code |
| `FontHeight` | `16` | 10-32. Raise it at 4K |

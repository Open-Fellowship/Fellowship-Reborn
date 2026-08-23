# frame_state

**Produces:** `frame_state.dll`. **A diagnostic. It changes nothing.** Off by default.

`env_probe` can prove that Direct3D is healthy and that no frames are being presented. Those two
facts together leave exactly one question, and it is a question about the engine rather than the
driver: **is the game running its frame and failing to show it, or is it not running a frame at
all?**

The engine answers that itself, in two globals.

## The mode word

The whole per-frame function is a switch on one dword:

```
00404630  mov  eax,[0x53EE84]
00404638  test al,8
0040463B  je   0x404688         bit 3 clear: on into the real work
          ... the bit 3 path updates a few subsystems and RETURNS WITHOUT DRAWING
00404688  test eax,eax
0040468A  jne  0x4046C9         non-zero: the full frame
          ... zero: the pre-start path, before there is a game to draw
```

What the bits mean comes from the code that writes them, not from guesswork. The setter is at
`0x4049F0` and it is the only thing in the executable that stores to `0x53EE84`:

| bit | | |
|---|---|---|
| `1` | | set together with `4` when the game proper starts (`push 5` at `0x42C51D`) |
| `2` | | entering it runs a one-off setup inside the setter |
| `4` | not minimised | `WM_SIZE` / `SIZE_MINIMIZED` clears it at `0x4BCD27`, `SIZE_RESTORED` sets it again at `0x4BCD74` |
| `8` | | the per-frame function returns without drawing |

## The frame counter

`0x54417C` is incremented once per full frame, at `0x4046F1`, by the engine itself. It is a
different question from "was a frame presented", and the gap between those two answers is exactly
where a black screen hides:

- counter moving, nothing presented: the game is drawing into something you cannot see
- counter still, mode has bit 3: the game has decided not to draw, and the mode says so
- counter still, mode is normal: it is stuck inside the frame, and the thread sample in
  `env_probe` will say where

## What it writes

Every change of the mode word, and a warning when the counter stops:

```
[frame_state] watching the frame mode at 0053EE84 and the frame counter at 0054417C, every 20 ms
[frame_state] first reading: mode 00000000 (----) - before the game has started, no frame work at all, engine frame counter 0
[frame_state] mode 00000000 -> 00000005
[frame_state]   now mode 00000005 (1-4 not minimised) - the full frame, drawing, engine frame counter 0
[frame_state] the engine's own frame counter has not moved for 1 second(s), stuck at 10, mode 0000000D - the per-frame function RETURNS WITHOUT DRAWING
```

Reading, on a thread of its own, every 20 ms. It never writes to the game and never hooks
anything, which is why it is safe to leave on while chasing something and pointless to leave on
afterwards.

## Who asked for the change

Reading the executable does not find the code that turns bit 3 on. One function writes that word,
the setter at `0x4049F0`, and no cross-reference to it pushes a value with bit 3 in it - it
arrives computed in a register from a caller nothing static reaches. So the running game is asked
instead.

The setter's prologue is exactly five bytes, which is exactly a jump:

```
004049F0  53              push ebx
004049F1  8B 5C 24 08     mov  ebx, dword ptr [esp+8]
```

Those five bytes are redirected to a stub that saves everything, hands the requested mode and
**the return address of whoever asked** to a small C function, restores everything, runs the two
displaced instructions and jumps back. The caller is then named as a module plus offset, which is
a thing that can be looked up in the binary:

```
[frame_state] mode 00000001 -> 00000009 asked for by Fellowship.exe+0BCD74   <- this is the one
                                                                                that stops the drawing
```

This is the one thing in this plugin that writes to the game, and `WatchSetter=0` turns it off for
anyone who wants a diagnostic that only reads.

## Configuration: `[frame_state]`

| Key | Default | |
|---|---|---|
| `Enabled` | `0` | on only while hunting a game that runs and does not draw |
| `WatchSetter` | `1` | redirect the mode setter's prologue so every change names its caller |

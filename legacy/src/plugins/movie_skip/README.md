# movie_skip

**Produces:** `movie_skip.dll`. Off by default on Windows, **on by default under Wine**.

Three bytes that make every movie report "finished" on its first tick, through the engine's own
path for a movie that could not be loaded. The game never waits on a movie again, and, the part
that mattered, whoever asked for the movie is told that it ended.

## Why this exists

This was the Steam Deck black screen, and it took the whole diagnostic set plus a second look at
the disassembly to get right.

`env_probe` proved the graphics were healthy: device created, `D3D_OK`, frames presented, window
1280x800 and holding the foreground. `frame_state` then read the engine's own mode word and frame
counter and produced the line that mattered:

```
[frame_state] mode 00000001 -> 00000009
[frame_state]   now mode 00000009 - the per-frame function RETURNS WITHOUT DRAWING, counter 9
```

Nine frames, then bit 3 goes on and stays on. Bit 3 is "a movie is playing"; the engine sets it in
MoviePC's Begin (slot 15, `0x47B9B0`) and clears it when playback ends. On Wine the end never
came.

## Bit 3

Starting a movie sets it, in Begin:

```
0047B9C5  mov cl, byte ptr [0x53EE84]
0047B9CB  mov eax, 8
0047B9D0  test al, cl
0047B9D2  jne 0x47B9DA
0047B9D4  or  dword ptr [0x53EE84], eax     <- "a movie is playing"
0047B9DA  mov eax, 1                        <- and playback started
0047B9DF  ret 8
```

With the bit set the per-frame function returns without drawing. The movie'''s own update clears
it again when playback ends, at `0x47BA72`, `0x47BC4D` and `0x47BCB8`, three exits of the same
function. On Wine that end never comes.

## The first version, and why it was not enough

v1 patched Begin to return 0 without setting bit 3. The engine then drew every frame, and the
main menu was still black. The reason is that the rfl does not talk to the movie object directly.
It calls the **media manager** (object `0x5403A0`, vtable `0x51EB40`), slot 17 at `0x47AB30`:

```
manager->current = movie          ; [0x5403A0+0x230]
movie->slot10()
return movie->Begin(a, b)         ; <- v1 made this return 0, and nothing else changed
```

and every frame, from the bit-3 branch (`0x404672`) **and** from the normal frame (`0x47F258`),
the manager ticks its current movie (`0x47AB70`):

```
if (current && (current->state & 3) != 2)
    if (current->Update() == 0) { current->slot11(0); current = NULL; }
```

`Update` (slot 23, `0x47BA20`) is where the Windows Media reader is really created and driven,
and it is driven by the object's state, not by bit 3:

```
0047BAB7  call WMCreateReader                  fail -> return E_FAIL
0047BADF  QueryInterface(IWMReaderAdvanced2)   fail -> return E_FAIL
0047BB0A  OpenStream(IStream this+50, cb this+54, ctx this)
                                               fail -> return E_FAIL
0047BB44  WaitForSingleObject(opened event, INFINITE)
0047BBBC  cmp [esi+0B0h],0 / je self           busy-wait for the first sample
```

Every one of those `E_FAIL` returns leaves the movie as "current", leaves the frame-mode bit
alone, and **never fires the completion callback** `([this+0x38])(ctx, ...)`, which the rfl
handed in through slot 22 (`0x47A990`). So with v1 on, the engine drew, the Windows Media path
still ran from Update every frame, failed silently inside Wine, and the rfl's opening-movie
sequence sat waiting for a completion that never came. Nothing had handed over to the Main Menu
GUI, so what was drawn each frame was empty. Healthy device, thousands of `D3D_OK` presents,
engine log silent, black. Identical under wined3d and DXVK, and with `black_screen` on or off,
because the renderer was never the problem.

## Why Wine cannot play these movies anyway

The movies are Windows Media (WMV7/8 video, `WMV1`/`WMV2` fourccs are in the exe), stored as
chunk type `0x342` inside the `.vdu` archives, and read through `WMVCore.DLL`
(`WMCreateReaderPriv`, `IWMReaderAdvanced2::OpenStream`) from the game's own `IStream`
(vtable `0x51ECB0`). Two things stack up on Wine:

* `WMCreateReaderPriv` is only as good as the 32-bit GStreamer behind it. A Lutris prefix on
  SteamOS very often has none, so reader creation fails before the game's stream is touched,
which matches "no error of any kind" in the engine log.
* Even with a codec, the game's `IStream` is a forward-only, double-buffered window:
  `STREAM_SEEK_END` returns `STG_E_INVALIDFUNCTION` at `0x47C530`, and `STREAM_SEEK_SET` outside
  the two buffered ranges is "MoviePC::Seek == MISSED OUR BUFFER RANGE" at `0x47C6AB`. Windows'
  WMVCore reads ASF sequentially and lives with that; winegstreamer's reader wants random access.
  And past open, the engine busy-waits at `0x47BBBC` for a first sample that may never come.

Skipping is the practical route on a handheld.

## What it does

The engine already has a "this movie is not going to play" path, the first thing Update checks:

```
0047BA3F  cmp [ecx],ebx / je 47BA81           stream not ready ->
0047BA43  if (cb) { cb(ctx,0); cb(ctx,1); }   report finished, the way the engine does
0047BA5E  clear bit 3 of 0053EE84
0047BA77  return 0                            manager stops and forgets the movie
```

Update is made to take that path unconditionally, before it dereferences anything:

```
0047BA29  8B 46 0C   mov eax,[esi+0Ch]   ->   EB 18 90   jmp 47BA43 ; nop
```

At `0x47BA29` all four pushes and `mov esi,ecx` have already happened, and `0x47BA43` uses esi as
`this` and ends in the function's own epilogue, so the stack is as the engine expects.
Begin (`0x47B9B0`) is left alone: it returns 1, sets bit 3 for one frame, and the very next tick
reports the movie over and clears the bit, precisely what happens on Windows when a movie
resource is missing (see the rfl's own "No Ring Death Movie! ... Proceeding with normal, boring
death.").

The ten-byte state-check signature at the site is verified before the first three bytes of it are
rewritten. A build that does not have it gets "not the MoviePC::Update state check this plugin
was measured against" and is left alone.

## What to expect

* Opening movies, in-game cutscenes and the death movies end instantly.
* If the Main Menu background was a looping movie on Windows, it is black on the Deck:
  `Interface.vdu` is 4.9 MB of `0x342` chunks and they are skipped like everything else.
* With `frame_state` on, bit 3 goes on and off again within a frame or two of frame 9, rather
  than staying on.

## Configuration: `[movie_skip]`

| Key | Default | |
|---|---|---|
| `Enabled` | `1` under Wine, `0` on Windows | on for Linux, Proton, a Steam Deck, or anyone who does not want to sit through the opening again |

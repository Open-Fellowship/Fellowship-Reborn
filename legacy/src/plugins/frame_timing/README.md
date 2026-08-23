# frame_timing

**Produces:** `frame_timing.dll`. On by default.

The engine's frame clock is `GetTickCount`. This moves it to `QueryPerformanceCounter`.

Not ported from anywhere. Everything below was read out of the shipped executable.

## The measurement

The engine has a Timer class at `0x0040CF10`..`0x0040D29D` with one global instance at
`0x0053EE58`, and every clock read in it goes through the same five-byte thunk at `0x004C12B0`,
which is `jmp dword ptr [GetTickCount]`. The frame delta is the difference between two readings:

```
0040D150  Timer::Tick(float *out)
0040D156    call 004C12B0                ; now
0040D15F    sub  ecx, edx                ; now - this->lastTick
0040D171    fild qword [esp+4]
0040D175    fmul dword [esi+0x28]        ; timeScale
0040D178    fmul dword [esi+0x10]        ; 0.001f, ticks to seconds
0040D17B    fstp dword [edx]             ; -> 0x00543284
```

`GetTickCount` advances with the system clock interrupt, about every 15.625 ms. So above roughly
64 fps most frames measure **zero** milliseconds and every fifteenth measures fifteen or sixteen.

At a 60 fps cap the two rates beat against each other at exactly 4 Hz. Four times a second the
world takes a 31 ms step and then a 0 ms one, and between those it alternates 15 and 16 against a
real 16.67. That is the judder, and no amount of tuning the limiter touches it, because the clock
reading the frames is coarser than the frames.

The engine's own answer to the zero frames is a floor of `0.002` s at `0x0051C764`, which invents
simulation time that did not happen and is why the stock game speeds up when it is unlocked.
`game_speed` lowers that floor. This plugin removes the reason it exists.

The high-resolution path was half-built and never wired up. The Timer's constructor already asks
for the performance counter's frequency and stores it at `+0x0C`:

```
0040CF10   call 004C12E0        ; QueryPerformanceFrequency -> float
0040CF18   fstp [ecx+0x0C]      ; stored, and never read again
0040CF1D   mov  [ecx+0x10], 3A83126F   ; 0.001f, used instead
```

## What this changes

`Timer::Tick` never divides by a frequency, it **multiplies** by a ticks-to-seconds constant it
keeps at `+0x10`. So the class does not have to be rewritten, or even understood by the patch:
give it a finer counter and tell it what a tick is now worth, and every function in it keeps
working with its own arithmetic untouched.

| | |
|---|---|
| fourteen `call 004C12B0` sites | redirected to a `QueryPerformanceCounter` of ours |
| `0040CF20` | the constructor's `0.001f` immediate becomes `1/rate` |
| `0051C774` | the `1000.0f` `Timer::Reset` uses to go the other way becomes `rate` |

The sites are `40CF47 40CFB3 40CFC7 40CFE4 40D037 40D05A 40D0DA 40D105 40D156 40D17F 40D1C3
40D229 40D246 40D276`. **Call sites and not the thunk**, because forty-six other callers of that
thunk are loading timeouts and progress bars that must keep counting in milliseconds. Turning a
ten second timeout into a ten millisecond one is the bug that distinction exists to avoid.

All fourteen are validated before any of them is written. A partial application would be worse
than none: the Timer would be reading some of its origins in milliseconds and the rest in
hundred-thousandths, and the differences it takes between them would be nonsense, not
merely coarse.

The constructor's immediate is patched, not the field it writes, so there is no race to win
with the constructor, which runs from `0x00403CC4` during start-up.

The frame rate readout is fixed by the same change and needed no work of its own:
`Timer::GetFramerate` at `0x0040D1B0` measures its own interval with the same counter. On a stock
clock at high frame rates its eight-frame sample can span zero milliseconds and divide by zero.

## The thirty-two bit span

The counter is a DWORD and the engine zero-extends the difference before converting it:

```
0040D161   mov dword ptr [esp+8], 0      ; the high half, forced to zero
0040D171   fild qword ptr [esp+4]
```

so a wrap does not produce a negative delta, it produces an enormous positive one. The
frame delta itself would survive that, because `UpdateTime` clamps at 0.1 s, but game time
at `0x00543364` would jump by half a day, and the twenty-six effect sites that read it would
go with it.

**A rebase does not help, though it was the first design.** The engine
only ever computes `now - stored`, and a common offset subtracted from both is invisible to a
difference, whether or not either side wraps. What has to fit in thirty-two bits is the **span**:
the oldest origin the Timer is still counting from, which is `+0x24`, set at start-up, at a level
load and at a savegame load. At 100 kHz that span is 11.9 hours of one uninterrupted level.

So the plugin takes its own once-per-frame hook at `0x004046CE`, the engine's call to
`UpdateTime`, and moves the engine's origin before the span runs out, the same way the engine
moves it itself in `SetTimeScale` at `0x0040D220`: carry the accumulated seconds at `+0x1C` into
the time base at `+0x20` and start counting again from now. The accumulator comes out at the value
it went in, so nothing on screen can tell it happened.

`0x004046CE` and not `0x004BCA19`, because `fps_limit` already owns that one, and because this
has to happen between whole frames, not inside `Tick`.

## Savegames

`0x0040D030` writes `now - this->+4` into the save stream and `0x0040D0A0` reads it back, so a
save carries an elapsed tick count in whatever unit the timer was using. A save made with this
plugin and loaded without it, or the reverse, gets **one** wrong frame rate reading before the
next eight-frame sample corrects it. Game time is stored as float seconds and is unit independent,
so nothing else crosses over.

## How it is built

**The counter is a plain C function.** `hires_ticks` is called from engine code, in the slot
`GetTickCount` used to occupy, so it obeys the same rules: the result in `EAX`, `EBX` `ESI` `EDI`
`EBP` left alone, and nothing left on the x87 stack. A function taking no arguments and returning
`uint32_t` satisfies all three by construction, and with no arguments `__stdcall` and `__cdecl`
assemble identically, because there is no stack to clean either way. So the fourteen sites call
it directly and it needs no stub.

**The tick arithmetic is split in two.** Written as `(delta * rate) / frequency` the product
overflows sixty-four bits after a few weeks of uptime on a fast counter. It is computed as a
whole part plus a remainder part, which costs one extra divide and cannot overflow: the remainder
is smaller than the frequency, so the second product is bounded by `frequency * rate`.

**The frame hook does need a stub**, because it runs between whole frames:

```
pushad / pushfd / call frame_timing_frame / popfd / popad / jmp UpdateTime
```

The engine loads `ECX` with the engine object at `0x004046C9`, one instruction before the call
being diverted, so every register has to come back as it went in. The tail jump leaves the stack
as the engine built it, so `UpdateTime` returns to `0x004046D3` by itself and its calling
convention never has to be known.

**The expected bytes are computed, not stored.** A `rel32` is a difference between two addresses
in the same image, so it is identical whether the module sits at its preferred base or not, and
the five bytes each site must hold can be derived from the preferred addresses alone.

**The Timer address is resolved once at install.** It lives in the executable's own `.data`,
mapped and writable from the moment the process exists, so nothing about it can become true
later. A `VirtualQuery` every frame to re-learn that would be a syscall in the one path that has
to stay cheap.

The constructor's immediate sits at `0x0040CF20`, inside

```
0040CF1D   C7 46 10 6F 12 83 3A   mov dword ptr [esi+0x10], 0x3A83126F   ; 0.001f
```

and the field it writes is `0x0053EE68`. When the watchdog re-anchors, the frame rate sample is
reloaded to eight frames instead of being left at zero, so the next sample is eight frames away
and not zero seconds wide. It re-anchors with a quarter of the span still in hand.

## What it does not fix

Variable-step Euler is not frame rate independent. Even with a perfect delta, a jump arc
integrated in small steps lands very slightly differently from one integrated in large steps. The
game will be smooth, it will run at the correct speed, and it will be consistent for any given
cap. It will not make 30 fps and 300 fps produce identical physics.

Doing that means a fixed-step accumulator around the update, which needs each of the thirty-one
delta consumers sorted into simulation and presentation first. That is a separate project.

## Checking it

The dev menu's **fix enhancers** tab reads the Timer's `+0x10` back and says which clock is
running, so whether this installed is answered by the engine, not by a log line.

For the frame times themselves, read `0x00543284` from outside the process against the frame
counter at `0x0054417C`, which gives exactly one sample per frame with no aliasing. Measured that
way over 1800 frames of play at a 90 fps target:

| | |
|---|---|
| mean | 11.111 ms |
| p1 / p50 / p99 | 11.100 / 11.110 / 11.120 ms |
| standard deviation | 0.121 ms, 1.09% of the mean |
| frame to frame change | 0.019 ms, 0.17% |
| within 5% of the mean | 99.4% of frames |
| engine game time vs wall clock | 19.9885 s against 19.9889 s |

1785 of those 1800 frames land in one 0.25 ms histogram bin. The last row is the other half of
the job: smooth and *correct*, which the floor at `0x51C764` could never give on its own, because
the only way it stopped the delta being zero was to invent time.

Re-quantising the same real frame times onto the 15.625 ms grid `GetTickCount` moves on, which is
what the engine would have reported for the identical run, gives a standard deviation of 7.086 ms
and 29% of frames reporting 0 ms. That is a derivation from measured frame times, not a
second measurement, so read it as the shape of the old behaviour and not as a reading.

## Configuration: `[frame_timing]`

| Key | Default | |
|---|---|---|
| `Enabled` | `1` | |
| `TickRate` | `100000` | ticks per second. Clamped to `1000 .. 1000000` |

Higher is finer and costs range, because the span before the timer re-anchors itself is
`2^32 / TickRate` seconds.

| `TickRate` | resolution | span | quantisation at 60 fps |
|---|---|---|---|
| `1000` | 1 ms | 49.7 days | 6% (and this is the engine's own behaviour with extra steps) |
| `10000` | 0.1 ms | 4.97 days | 0.6% |
| `100000` | 0.01 ms | 11.9 hours | 0.06% |
| `1000000` | 1 us | 71 minutes | 0.006% |

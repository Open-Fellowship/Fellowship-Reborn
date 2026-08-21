# fps_limit

**Produces:** `fps_limit.dll`. On by default, 60 fps.

Caps the frame rate. Ported from the community patcher's `EnableFPSLimiter`, with one distinction
worth being explicit about: **the hook is ported, the limiter is not.**

## What was worth copying

```
0x4BCA19   call 0x404630        once per frame
```

The patcher redirects that call's displacement to a function of its own, does the waiting, and
then **tail-jumps** to `0x404630` rather than calling it. That is the part worth keeping. A `jmp`
leaves the stack exactly as the engine built it, so the original function returns straight to
`0x4BCA1E` on its own and neither its calling convention nor its argument count ever has to be
known. Detouring its prologue instead would have required both.

The stub is fourteen bytes:

```
60          pushad
9C          pushfd
E8 rel32    call fps_limit_tick
9D          popfd
61          popad
E9 rel32    jmp 0x404630
```

## What was written fresh

The waiting. Three modes, because they are genuinely different trades and not preferences:

| `Mode` | | |
|---|---|---|
| `0` | sleep | cheapest and coarsest. Hands the CPU back and accepts the jitter |
| `1` | spin | tightest and most expensive. Busy-waits the whole gap |
| `2` | hybrid | **default.** Sleeps the bulk, spins the last 1.5 ms |

Two details that are not obvious:

**The spin margin is 1.5 ms, not 1 ms.** `Sleep(1)` routinely overshoots by a millisecond or more
depending on what else on the machine has asked for a finer timer, so the spin has to start
further out than the error it exists to absorb.

**The schedule resyncs in both directions, and the second direction is not optional.**

Behind is the obvious case. If the process is suspended - alt-tab, a level load, a debugger - the
target ends up far in the past, and a limiter that just kept adding one period would run
completely unthrottled for as many frames as it was behind, catching up on time that no longer
exists. Four frames of lateness abandons the schedule and restarts it from now.

**Ahead is the case that hung a Steam Deck.** The line at the top of this file says "once per
frame" and that is where the bug lived. It is once per frame *while the game is drawing*. During
start-up the engine reaches that call site far more often than it presents anything, and every one
of those calls added a whole frame period to the target while almost no real time passed. The
schedule ran away into the future, the computed sleep grew from milliseconds to seconds, and the
game sat inside a single `Sleep` with a black screen, a healthy Direct3D device and a message loop
still politely answering. Ten frames, then nothing, identically on every run.

It was found by `env_probe` suspending the stalled thread and writing down where it was:

```
[env_probe]   the drawing thread is at ntdll.dll+00C5C8
[env_probe]     called from fps_limit.dll+001649
```

So the target may now never be more than one period ahead of the clock, and no single wait may
exceed one period whatever the arithmetic says. A limiter is allowed to be late. It is not allowed
to be early.

The first three resyncs are logged with the size of the error, the call count and the time since
install, because a resync on every call means this site is being reached far more often than the
frame rate, and that is worth reading rather than silently absorbing.

`timeBeginPeriod(1)` is called at install, because without it `Sleep(1)` can be `Sleep(15)`.

## The live target

The dev menu's frame rate slider publishes a target over the shared channel, and this prefers it
over the ini value whenever one has been published. Same arrangement as `field_of_view` and the
field of view slider: one writer for the engine, one writer for the request, and a plugin whose
partner is not installed reads a block nobody ever writes to.

It is polled once a frame from the top of the hook rather than pushed, because the menu runs on
the game's thread inside `EndScene` and this runs on the same thread at the top of the frame.
There is no moment where a push would be cheaper, and a poll needs no agreement about who is
allowed to call whom. While the serial has not moved the poll is one aligned load.

`MaxFPS=0` means uncapped. The hook stays installed and waits for nothing, so the menu can still
hand it a rate later without a restart.

## Not the same thing as the other two

Three plugins, three different problems.

| | |
|---|---|
| `frame_timing` | gives the engine a clock fine enough to measure a frame at all. Without it the delta is quantised to 15.6 ms and no cap can be smooth |
| `game_speed` | lowers the floor the engine puts under that delta, so a fast frame stops being reported as a slow one |
| `fps_limit` | stops the frame time being 800 microseconds in the first place |

Run all three.

## Configuration: `[fps_limit]`

| Key | Default | |
|---|---|---|
| `Enabled` | `1` | |
| `MaxFPS` | `60` | `0` for uncapped, otherwise clamped to `10 .. 1000`. The dev menu overrides this while the game runs, and its save button writes back here |
| `Mode` | `2` | `0` sleep, `1` spin, `2` hybrid |

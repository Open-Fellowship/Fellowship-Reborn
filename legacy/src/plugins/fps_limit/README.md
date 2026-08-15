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

**The schedule resyncs when it falls far behind.** If the process is suspended - alt-tab, a level
load, a debugger - the target time ends up far in the past. A limiter that just kept adding one
period would then run completely unthrottled for as many frames as it was behind, trying to catch
up on time that no longer exists. When the gap exceeds four frames the schedule is abandoned and
restarted from now. That is the difference between a limiter and a bug.

`timeBeginPeriod(1)` is called at install, because without it `Sleep(1)` can be `Sleep(15)`.

## Not the same thing as game_speed

`game_speed` makes the simulation step fine enough that the physics stop depending on frame time.
This stops the frame time being 800 microseconds in the first place. Run both.

## Configuration: `[fps_limit]`

| Key | Default | |
|---|---|---|
| `Enabled` | `1` | |
| `MaxFPS` | `60` | clamped to `10 .. 1000` |
| `Mode` | `2` | `0` sleep, `1` spin, `2` hybrid |

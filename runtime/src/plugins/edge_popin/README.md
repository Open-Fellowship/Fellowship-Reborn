# edge_popin

**Produces:** `edge_popin.dll`.

Geometry disappears from the outer edges of the screen at high resolutions, worst at 4K, and the
missing band gets wider the wider the window is.

## The bug

Before drawing an object the renderer tests its screen-space bounds against a guard rect whose
corners are hard-coded immediates: `(-1024, -1024)` to `(3072, 3072)`. That was a generous margin
around a 640x480 or 1024x768 screen. At 3840 pixels wide the right edge of the screen is *past*
the right edge of the guard rect, so everything out there fails the test and is never drawn.

This is a real bug, not a preference, and it is the one fix here that should always be on. At
640x480 the patch changes nothing observable, because at that size nothing was ever outside the
rect to begin with.

## What it writes

| Address | | |
|---|---|---|
| `0x485867` | `75 06` -> `EB 06` | objects use the real view frustum, not the guard rect |
| `0x48B984` | `-1024` -> `-32768` | guard rect left/top |
| `0x48B992` | `3072` -> `32768` | guard rect right/bottom |

The new corners are the extremes of the 16-bit range the engine's screen coordinates live in, so
the test cannot fail for a reason unrelated to what is actually on screen.

## Configuration: `[edge_popin]`

| Key | Default | |
|---|---|---|
| `Enabled` | `1` | |

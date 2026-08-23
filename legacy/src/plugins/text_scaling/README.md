# text_scaling

**Produces:** `text_scaling.dll`. Patches `Fellowship.rfl`.

All in-game text is drawn at a fixed pixel size, so at 4K subtitles and menu labels are unreadably
small.

## Seven hooks, and why it is not fewer

Text is not one number. Each of these was found because the previous version looked wrong in a
screenshot:

| | site | |
|---|---|---|
| glyph height scale | `rfl+648B2` | a hard-coded `push 1.0f` |
| glyph width scale | `rfl+648C8` | `call [edx+0xA4]` |
| pen advance | `rfl+64917` | `call [edx+0xA4]` |
| measured width | `rfl+64B2A` | `call [edx+0xA4]`, drives centring |
| DrawString space | `rfl+64779` | space advanced by `font[+0x10]` raw |
| MeasureString space | `rfl+64A0B` | the same field, a different register |
| line height | `rfl+63CA0` | `font[+0x0C]` returned raw |

Scaling the glyph width but not its height rendered squat, stretched letters. Scaling the glyph
and its advance but not the space produced words with no gaps. Adding the space but not the line
height produced text overflowing its own boxes. Four hooks was not "nearly right", it was wrong
in a different way each time.

## The stubs read through OUR pointer, never the engine's

The first version put the engine's active-camera pointer in every one of the seven stubs and read
`[camera+0x258]` through it. On a second install that pointer was neither NULL nor a camera and
the game crashed; `hud_scaling`'s README has the log line that proves it. The integer stubs had a
second way to die on top of the access violation - `idiv` faults outright when the quotient does
not fit, which a garbage numerator guarantees.

The second version sampled the scale onto a poll thread and had the stubs multiply by a plain
float. That removed the crash and broke the text, because **the pause menu renders the world into
a sub-rectangle and the camera's viewport is that rectangle while the menu is drawn**. A scale
sampled a quarter of a second earlier is the full-screen one. Measured from the screenshot: a
capital G 17 px tall and 86 px wide, width scaled by 4.5, height not scaled at all. Squat,
stretched glyphs, which is the exact failure these seven hooks exist to prevent.

The read is live again and the pointer is ours. `common/camera.c` validates a candidate camera
completely and publishes it into a variable in this DLL, zeroing it the moment one stops
validating; the stubs branch over the scaling when they find zero. The `idiv` overflow closes
with it: a camera whose viewport height is outside 64..32768 never gets published, so
`eax * height / reference` stays small.

## Two details worth keeping

**The glyph height scale is a `push 1.0f`, not a call.** The stub pushes the 1.0f anyway, so the
stack frame stays byte-identical, then overwrites it in place with `fstp [esp+4]`. Getting this
wrong is what made the first version of this fix render squat, stretched glyphs, and swapping it
for a `push` of a timer-sampled float brought that same failure straight back, for a different
reason. It is the most fragile of the seven and the one to check first when text looks wrong.

**Line height is hooked from `rfl+63CA0`, not the obvious `rfl+63CA9`.** The function's own `je`
targets an address *inside* where a five-byte branch at `63CA9` would sit, so the zero-check is
reimplemented in the stub rather than jumped over.

## Height, not width

Glyphs must scale uniformly or they stretch, so the reference is `viewportHeight / 480`.
`hud_scaling` is width-based because it governs a horizontal extent. Two different references is
correct, not an inconsistency.

## Partial installs

If some hooks take and others do not, the plugin logs `PARTIAL` as an **error**. Text laid out at
one size and drawn at another is worse than text that is uniformly too small, and the log should
not be cheerful about it.

## Configuration: `[text_scaling]`

| Key | Default | |
|---|---|---|
| `Enabled` | `1` | |
| `ReferenceHeight` | `480` | larger makes text smaller |

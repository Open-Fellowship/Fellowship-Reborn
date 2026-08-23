# screen_test

**Produces:** `screen_test.dll`. A diagnostic. Off by default.

Paints the whole back buffer a solid colour every frame and cycles it: red, green, blue, one
second each.

## What it settles

When the log says frames are being presented and the screen is black, there are two possibilities
and no amount of further logging separates them. Either the game is drawing black, or the picture
is not reaching the display.

| what you see | what it means |
|---|---|
| the screen flashes colours | the display path works. The game is drawing nothing into a perfectly good surface |
| the screen stays black | the presented image never reaches the panel, and nothing inside the process can fix that |

That is the whole plugin. It is the crudest diagnostic here on purpose: no keyboard, no overlay,
no font, no input. On a handheld, where the machine showing the fault is not the machine you are
sitting at, that matters more than it sounds.

## How it works

Hooks `CreateDevice` to learn the device, then `Clear` and `Present` on the device vtable. Every
frame it clears to the current colour and lets the engine's own present go through unchanged.
Nothing else in the game is touched, and the engine's own drawing still happens; it is simply
covered.

It shares the `CreateDevice` slot with `borderless` and `env_probe`, and the `Present` slot with
`env_probe`. There is no chaining mechanism, so each saves the pointer it found and calls through
it. That works because nothing is ever uninstalled, but it means running all three at once is
untested.

## Configuration: `[screen_test]`

| Key | Default | |
|---|---|---|
| `Enabled` | `0` | on means the game is unplayable while it runs. That is the point |

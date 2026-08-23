# borderless

**Produces:** `borderless.dll`. **Off by default**, including under Wine. See the note below.

The game keeps the size it asked for and stops taking the screen exclusively.

## Why it is off, including under Wine

It used to switch itself on under Wine, and the reasoning for that still holds: exclusive full
screen loses its window to the focus there, and this engine draws nothing while its window is
down.

**It stops `dev_menu` working on a Steam Deck.** Measured on hardware, reproduced, and fixed by
turning this off. Both plugins reach the same Direct3D device: this one rewrites the presentation
parameters through `CreateDevice` and `Reset` and re-asserts the window shape four times a second
from a keeper thread, while `dev_menu` hooks `EndScene` and takes the mouse through DirectInput in
exclusive mode. Which of those interactions breaks the menu is **not established**.

So it waits. A plugin that is on by default with a known failure attached is worse than one that
is off with a note explaining what to fix. Turn it on if a lost window is costing you more than
the menu is worth.

## Why


Exclusive full screen is a bargain with the display. The game gets the mode it asks for, and in
exchange the window's fate belongs to the focus: lose the foreground for an instant and the
runtime takes the window down, and **this engine draws nothing while its window is down**. On
Windows that is the mild annoyance of alt-tabbing. Under Wine, where the desktop window can take
the focus a moment after a mode change, it is the difference between a game and a black screen:

```
[env_probe]   window now  0002009E, client 160x24, window 160x24 at -32000,-32000, minimised
[env_probe]   foreground window 00010020, class "#32769" - another process
```

`#32769` is the desktop. `-32000,-32000` is where a minimised window lives.

A windowed device makes no such bargain. Nothing minimises it, nothing takes a display mode away
from it, and alt-tab is instant, not a mode change in each direction.

## What it does

Two interceptions, at COM vtable positions and not addresses in this game, so this is correct
against wined3d, DXVK and the retail Microsoft runtime alike:

| | |
|---|---|
| `IDirect3D8::CreateDevice`, slot 15 | rewrite the parameters before the device exists |
| `IDirect3DDevice8::Reset`, slot 14 | and again every time the game changes mode |

In both, `Windowed` becomes true and the refresh rate and presentation interval are cleared,
because a refresh rate on a windowed device is refused, not ignored. **The back buffer keeps
the size the game asked for.** The window itself is then restyled: `WS_POPUP`, no caption, no
frame, positioned at the origin at the size of the screen.

The size is the game's business and only the exclusive mode is this plugin's. The first
version overrode the back buffer to the size of the screen, and that was a bug: the engine's
viewport stays the size of the mode it chose, so everything it draws lands in one corner of a
larger surface and the rest is never written to. From outside that is a mostly black screen
with the game in the top left.

The game is told nothing. It asked for 1280x800 and it gets a 1280x800 back buffer filling the
screen, which is what it wanted in the first place.

`Direct3DCreate8` is hooked at the import slot, and each plugin that hooks it keeps whatever it
found and forwards, so this and `env_probe` chain in load order instead of fighting over it.

## Configuration: `[borderless]`

| Key | Default | |
|---|---|---|
| `Enabled` | `0` | off everywhere. `1` turns it on |
| `Width` | `0` | 0 = the width of the desktop |
| `Height` | `0` | 0 = the height of the desktop |

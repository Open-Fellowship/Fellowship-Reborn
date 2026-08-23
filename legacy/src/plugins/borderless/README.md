# borderless

**Produces:** `borderless.dll`. On under Wine, off on Windows, unless the ini says otherwise.

The game keeps the size it asked for and stops taking the screen exclusively.

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
from it, and alt-tab is instant rather than a mode change in each direction.

## What it does

Two interceptions, at COM vtable positions rather than addresses in this game, so this is correct
against wined3d, DXVK and the retail Microsoft runtime alike:

| | |
|---|---|
| `IDirect3D8::CreateDevice`, slot 15 | rewrite the parameters before the device exists |
| `IDirect3DDevice8::Reset`, slot 14 | and again every time the game changes mode |

In both, `Windowed` becomes true, the back buffer becomes the size of the desktop, and the refresh
rate is cleared, because a refresh rate on a windowed device is refused rather than ignored. The
window itself is then restyled: `WS_POPUP`, no caption, no frame, positioned at the origin at the
size of the screen.

The game is told nothing. It asked for 1280x800 and it gets a 1280x800 back buffer filling the
screen, which is what it wanted in the first place.

`Direct3DCreate8` is hooked at the import slot, and each plugin that hooks it keeps whatever it
found and forwards, so this and `env_probe` chain in load order rather than fight over it.

## Configuration: `[borderless]`

| Key | Default | |
|---|---|---|
| `Enabled` | auto | absent means on under Wine and off on Windows. `1` forces it on, `0` off |
| `Width` | `0` | 0 = the width of the desktop |
| `Height` | `0` | 0 = the height of the desktop |

# level_select

**Produces:** `level_select.dll`. Patches `Fellowship.rfl`. Two bytes. On by default.

New Game opens the developers' level list instead of starting the configured first level. The
screen is not drawn by this plugin and is not a mod: it is a finished GUI class in every copy of
`Fellowship.rfl`, with seventeen authored properties, its own listbox, its own error messages and
a reader for `LevelList.txt`.

```
LevelSelectGUI                        the class
Level Select Screen                   its display name
Level Selection Screen                its property group
Level Listbox / Level Listbox Text Buttons / Listbox 'Level' Text Properties
LevelList.txt                         what fills it in
Level List could not be read!  Level Selection GUI cannot be created!
```

## The two bytes

The main menu's New Game handler asks whether a level was configured to start:

```
mov  eax,[ebp+0x70]              "Level to Load for New Game"
test eax,eax
je   <level list>                not configured -> the level selection screen
...                              configured     -> load that level
```

Retail configures one, so the branch behind it is never taken. The edit turns the conditional
into an unconditional jump, in six bytes so nothing after it moves:

```
0F 84 D6 00 00 00     je  +0xD6
90 E9 D6 00 00 00     nop / jmp +0xD6
```

The displacement is untouched. That is the shape of the community hex edit that has been in
circulation for years, and it is what this plugin does at run time instead, so no game file is
modified.

`LevelList.txt` is the other half and always has been. It ships with the game, one level path per
line, and renaming its entries is how the proper level names come to appear in the menu list.
That list is this screen. Without a readable file the engine prints its error and no screen is
created, patch or no patch.

## This one scans

`engine_sites.h` says signature scanning is the upgrade path, to be done once there is a second
build to be right about. There is now:

| Fellowship.rfl | size | the branch |
|---|---|---|
| retail, fresh install | 1,306,624 | `rfl+0x75B7F` |
| the build this project targets | 1,372,160 | `rfl+0x75FAF` |

Same eleven bytes, addresses 0x430 apart. A hard-coded address would have been correct about
exactly one of the two, and would have written the other one's `mov`/`test` pair into something
unrelated. So the plugin searches the rfl's code section for

```
8B 45 70 85 C0 0F 84 D6 00 00 00
```

and requires **exactly one** match before writing. Two matches means the sequence identifies
nothing and it refuses; the search covers every byte and does not stop at the first hit, which
is the only way to know that.

If no stock match is found it asks the second question instead of giving up: does this copy
already carry the edit? A rfl that has been through the old file patcher, or that came out of a
release which shipped an edited one, answers yes, and the log says so plainly instead of claiming
a fix it did not make.

```
[level_select] rfl+75FB4 already carries the edit, nothing to do on this copy
[level_select] rfl+75B84  je -> nop/jmp: New Game now opens the level list
```

## What this does not do

`Fellowship.dll` v0.92e has an `EnableMapSelection` key in `Fellowship.ini` and it does nothing
at all. The DLL reads it into a global at `+0x7240` and never reads that global again: one
instruction in the whole file touches it, the store at `+0x11FE`, where every other option there
has a matching `cmp [option],0`. The name is the only part of the feature that was written. The
edit above is what actually enables the screen, and it lives in the rfl, not in that DLL.

## Configuration: `[level_select]`

| Key | Default | |
|---|---|---|
| `Enabled` | `1` | `0` makes New Game start the configured level, as a stock game does |

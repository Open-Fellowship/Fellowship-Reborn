# The game on Wine, Proton and the Steam Deck

Everything below was established on a Steam Deck running a Lutris prefix, WINE 10.0 and 11.0, an
AMD Custom APU 0932 (RADV VANGOGH), against both Wine's builtin `d3d8` and DXVK's. It is written
down because most of it cost a night to learn and none of it is guessable.

## What is fixed

`movie_skip` switches itself **on under Wine and off on Windows**: `platform_is_wine()` asks
`ntdll` for `wine_get_version`, which Windows does not export, so it is a fact rather than a
guess. The ini overrides either way. The other two fixes are right on every platform.

### `movie_skip` - the engine waits for ever on a movie Wine cannot play

The opening sequence goes through **`WMVCore.DLL`**, the Windows Media Format runtime, which Wine
only stubs. The engine sets a "a movie is playing" flag, stops drawing, and waits for an end that
never comes:

```
0047B9D4  or  dword ptr [0x53EE84], 8      "a movie is playing"
00404638  test al,8                        the per-frame function's first question
0040463B  je  0x404688                     ... and if the bit is set it returns without drawing
```

Symptom: **exactly ten frames presented and then nothing**, identically on every run, with a
healthy device, a correct window and a message loop still answering. Three bytes at `0x47BA29`
end it; the mechanism is in "The main menu: solved" below.

### `fps_limit` - the limiter ran away into the future

The frame limiter only ever looked one way. Falling *behind* is the obvious case and was handled.
Running *ahead* was not: the hook is one call site, and the engine is under no obligation to reach
it once per drawn frame - during start-up it goes round far more often than that, with nothing
being presented. Every one of those calls added a whole frame period to the target while almost no
real time passed, so the schedule ran away, a single `Sleep` grew to several seconds, and the game
sat in it behind a black screen. The limiter now resyncs in both directions and reports the first
few resyncs with the call count, so a site reached far more often than the frame rate says so.

### `ini` inline comments - our own configuration file was lying

`GetPrivateProfileString` returns everything after the `=`, comment and all. The numeric readers
survived it because `strtol` stops at the first space. The boolean reader compared the whole
string against `"1"` and silently fell back to its default, so **every documented boolean in the
shipped ini was ignored** - which is why `LogMessages=1` did nothing for a week of runs. Fixed in
`common/ini.c`.

## The main menu: solved

The last stretch turned out to be `movie_skip` itself. Its first version stopped Begin from
setting the "movie playing" bit, so the engine drew every frame - but the movie was still
"current" in the media manager, whose per-frame tick kept running MoviePC::Update, which kept
creating and failing the Windows Media reader in silence and **never fired the completion
callback** the rfl was waiting on. The opening-movie sequence never handed over to the Main Menu
GUI, so what was drawn each frame was empty. Every observation above falls out of that: healthy
device, thousands of `D3D_OK` presents, four lines of engine log then nothing, identical with
wined3d and DXVK and with `black_screen` on or off.

The fix is three bytes at `0x47BA29` that make Update take the engine's own "this movie could
not be loaded" path on its first tick: callback fires, bit clears, manager forgets the movie,
and the game goes to its menu. The mechanism, the addresses and why Wine cannot play these
movies at all (32-bit GStreamer, and the game's forward-only `IStream` against a reader that
wants random access) are in `src/plugins/movie_skip/README.md`.

Confirmed on the Steam Deck: with `movie_skip` v2 the game reaches the main menu and plays.

## What is still open

- The movies themselves. They are WMV7/8 in ASF, stored as chunk type `0x342` inside the `.vdu`
  archives, and skipping them means no opening, no cutscenes and, if the menu background was a
  loop, a black one. The clean route is a `movie_player` plugin that hooks the same Begin /
  Update / callback seam, plays a converted file from a `Movies\` folder and presents it on the
  D3D8 device - the same shape as OpenPhantom's `fmv_player`.

## The instruments

The diagnostics that found all of this - `env_probe`, `frame_state`, `screen_test` - and the
`borderless` experiment are not on this branch. They are kept on `steamdeck-diagnostics`, which
is this branch plus those four plugins, for the next time the screen is the thing that is broken.

# Contributing to OpenFellowship

## The first rule

**Verify the bytes before you write them.** Every patch reads what is currently
at the site and refuses if it is not what was expected. That is what makes a
patch idempotent, and it is what lets a different build of the game decline
cleanly instead of being corrupted.

`common/patch.c` provides the forms that do this: `patch_write_expect`,
`patch_repoint_operand`, `patch_redirect_call` and `patch_validate_bytes`. Do
not reimplement `VirtualProtect` or poke bytes directly from a plugin. Write
the whole word, never one byte of a little-endian immediate.

A site that does not match disables that one patch and says so in the log. It
never guesses, and it never writes anyway.

## Addresses today, signatures later

Sites are currently found by **absolute address**, written as preferred-base
addresses and converted at run time by `exe_site` and `rfl_site`. This is a
deliberate position, not an oversight, and `common/engine_sites.h` states it:
signature scanning is worth building when there is a second build to test it
against, and worth nothing before then.

The one target build is `Fellowship.exe`, No-CD, 2,133,459 bytes, and
`Fellowship.rfl`, 1,372,160 bytes. Every address in the tree was verified
against those two files.

`level_select` is the exception that shows the direction: it finds its site by
signature, because the retail rfl and the targeted one put the same branch
`0x430` apart. Write new sites so they can migrate: read an address out of a
matched operand rather than embedding it twice, and do not hand-roll a scanner
inside a plugin.

## Measure before you patch

Every fix here that went in on a plausible theory rather than a measurement had
to be reverted. If you cannot state the law your fix implements as an equation
with numbers on both sides, the fix is not ready.

Say which claim you are making. Compiled, run in game, and measured are three
different things, and there is no automated test suite here to stand in for the
last two.

## Style

C11, MSVC, 32-bit only, `/W4 /WX`. Each file opens with a comment saying what it
is for and, where the answer is not obvious, why it exists at all rather than
what it does line by line.

Every hardcoded address, offset, opcode and unusual constant needs its
explanation next to it. A comment that no longer matches the code under it is a
bug, and it gets fixed or deleted in the same change.

## One plugin per behaviour

Plugins never call into each other. Each links its own copy of `common/`, so
state in there is private to the DLL, and one plugin having resolved the host
image does nothing for the next.

**There is no hook chaining mechanism.** Where two plugins hook the same
Direct3D vtable slot they chain by convention: each saves the pointer it found
and calls through it, so installation order decides the order they run in and
nothing enforces that they cooperate. Three plugins currently share
`CreateDevice` this way, and `env_probe` and `screen_test` share `Present`.

That works because nothing is ever uninstalled. It is not a mechanism, and a
plugin that assumes the pointer it saved is the real Direct3D function will be
wrong. If you add a hook, save what you find and call through it.

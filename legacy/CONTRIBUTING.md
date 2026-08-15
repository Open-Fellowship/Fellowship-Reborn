# Contributing to legacy

## A plugin is one behaviour

If it can be turned off on its own, it is its own DLL. If turning it off would break something
else, they are the same DLL.

Plugins never call into each other. Where two of them want the same engine function,
`common/detour.c` chains them, so the result is the same whichever loaded first and neither has to
know the other exists.

## Starting one

Copy `src/plugins/template_plugin`, rename the directory, the two files and the section name, and
add it to `src/plugins/CMakeLists.txt`. The directory name, the DLL name, the ini section and the
log prefix are all the same word. That is what lets a player match a line in the log to a file on
disk without a lookup table.

## What install time is, and is not

The loader calls `open_fellowship_install` at the host executable's **entry point**: outside the
loader lock, image fully mapped, before the CRT has run.

* Patching `Fellowship.exe`, do it immediately.
* Patching `Fellowship.rfl`, **you cannot yet.** The rfl is a DLL the game loads later;
  `GetModuleHandleA("Fellowship.rfl")` returns NULL at install time. Wait for it.

## Rules that were learned the expensive way

**Signatures, never addresses.** Find code by what it is. Where a pattern would have to embed an
absolute address, use the address-free form and read the address out of the matched operand.
A pattern that matches zero times, or more than once when one was expected, disables that one
patch and says so in the log. It never guesses.

**Validate before writing.** Read the current value and refuse when it is not what you expected.
That check is also what makes a patch idempotent: a second run finds the new value, not the old
one, and declines.

**Write the whole word, not a byte of it.** Poking one byte of a little-endian immediate is how
you turn a limit you meant to lower into one you raised.

**Measure before you theorise.** State the law your fix implements as an equation with numbers on
both sides. Every fix in this project that went in on a plausible-sounding theory instead had to
be reverted; three of them are written up in `_FixEnhancers/docs` precisely so the next person
does not repeat them.

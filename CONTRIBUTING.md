# Contributing to OpenFellowship

## The one rule

**Signatures, never addresses.** A byte pattern that describes what the code
*is* survives a different build of the executable; a hard-coded address does
not. Where a pattern would have to embed an absolute address, prefer the
address-free form and read the address out of the matched operand.

A pattern that matches zero times, or more than once when one was expected,
disables that one patch and says so in the log. It never guesses.

## The second rule

**Measure before you patch.** Every fix in this project that went in on a
plausible theory rather than a measurement had to be reverted. The write-ups
in `_FixEnhancers/docs` record several of them. If you cannot state the law
your fix implements as an equation with numbers on both sides, the fix is not
ready.

## Style

C11. Warnings are errors. Each file opens with a comment saying what it is for
and, where the answer is not obvious, why it exists at all rather than what it
does line by line.

One plugin per independent behaviour. Plugins do not call into each other; two
plugins that detour the same engine function are chained by `common/detour.c`
so the result is the same whichever loaded first.

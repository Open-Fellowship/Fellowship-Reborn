# Contributing to legacy

## A plugin is one behaviour

If it can be turned off on its own, it is its own DLL. If turning it off would break something
else, they are the same DLL.

Plugins never call into each other. Each links its own copy of `common/`, so state in there is
private to the DLL: another plugin having resolved the host image does nothing for yours.

There is no hook chaining mechanism. Where two plugins hook the same Direct3D vtable slot they
chain by convention, each saving the pointer it found and calling through it, so installation
order decides which runs first. Three plugins share `CreateDevice` that way today. It works
because nothing is ever uninstalled, and it is not something to rely on: save what you find and
call through it.

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

**Addresses today, signatures later.** Sites are found by absolute address, written at the
preferred base and converted by `exe_site` or `rfl_site`. That is deliberate: signature scanning
is worth building when a second build exists to test it against, and worth nothing before then.
`level_select` is the exception and shows the direction, because the retail rfl and the targeted
one put the same branch `0x430` apart.

Write new sites so they can migrate. Read an address out of a matched operand rather than
embedding it twice, and do not hand-roll a scanner inside a plugin.

**Validate before writing.** Read the current value and refuse when it is not what you expected.
That check is also what makes a patch idempotent: a second run finds the new value, not the old
one, and declines.

**Write the whole word, not a byte of it.** Poking one byte of a little-endian immediate is how
you turn a limit you meant to lower into one you raised.

**Measure before you theorise.** State the law your fix implements as an equation with numbers on
both sides. Every fix in this project that went in on a plausible-sounding theory instead had to
be reverted. Where a plugin has a disproved hypothesis behind it, that record lives next to the
plugin: `hud_scaling/HUD-FINDING.md` is the model, with the disassembly that killed each
approach.

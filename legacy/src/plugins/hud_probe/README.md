# hud_probe

**Produces:** `hud_probe.dll`. **A diagnostic, not a fix. Off by default.**

It changes nothing the game reads. It records *which code asks for which authored value*, and
writes that to the log on a key press. Turn it on when you are hunting something, and off again
when you have found it.

## The site

Every authored value in this engine, every position, size, colour, count, texture reference,
in the menus and the HUD alike, is fetched through **one function**, and that function is in
`Fellowship.exe` rather than `Fellowship.rfl`:

```
0044E6E0   push esi / push edi
           mov edi,[esp+0x10]      the -1 that means "no sub-index"
           je  0044E70F
0044E70F   mov eax,[esp+0xC]       the property index
           mov edx,[esi+8]         the object's flat value array
           lea eax,[edx+eax*4]     return &values[index]
```

That location is not in the file anywhere you can read it: it is a vtable entry, reached as
`call dword ptr [vtable+8]`. It was found by breakpointing the one call site already known,
`rfl+789A4`, the control class asking for property `0x1C`, and reading the entry it dispatched
through. `_FixEnhancers/tools/Fellowship_GETTER_Hunt.CT` is that breakpoint, kept for the next
time a vtable needs naming.

## Why this is a DLL and not a Cheat Engine script

The getter runs roughly **230,000 times a second**. A scripted breakpoint on it makes the game
unplayable long before it produces a useful sample. Native, the recording path is a hash into a
fixed 2048-entry table with one probe, no chaining, no allocation and no lock, a bounded handful
of instructions, because anything heavier at that call rate changes the thing it is measuring.

A hash collision drops a pair rather than growing the table. That costs a line of a report; a
lock would cost the frame rate.

## Reading the report

`F2` starts recording, `F2` again writes it and stops:

```
  caller                   index  hits
  Fellowship.rfl+65BBE     12     4366
  Fellowship.exe+4F034     16     11808
  ...
---- 341 distinct (caller, index) pairs ----
```

**The index is relative to the object's class**, and that is the trap this tool has already
sprung once. Index 12 is `RFSizeX` for one class and a template ID for another; a caller list
cannot be turned into a property list without knowing the class. `hud_scaling/HUD-FINDING.md` has
the full account, including the disassembly of the caller that proved it.

So the report tells you *where to look*, not *what the value means*. Treat it as a set of
addresses to disassemble.

## Configuration: `[hud_probe]`

| Key | Default | |
|---|---|---|
| `Enabled` | `0` | it hooks the busiest function in the engine; opt in deliberately |
| `DumpKey` | `113` | `VK_F2`. Press to start, press again to write the report |

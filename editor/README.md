# editor

Tools for the game's own data formats.

**Not started.** What is known so far:

* **SRSC containers** (`Common/Interface/Interface.odu`, `Interface.xdu`)
  hold the interface strings. A string record is `uint16 len` followed by
  `len` bytes, NUL-padded, with the invariant `(2 + len) % 4 == 0`.
* Item, level and GUI properties are named in plain text inside
  `Fellowship.rfl` (`Cell Width (Screen %)`, `Model To Use In Inventory`,
  `ModelFOV`, `Inventory Scale`, and several hundred more).

# tools

Tools for the game's own data formats. This is where pillar 3, modding, lives.

## `blender/fotr_importer`

A Blender extension that reads the Riot Engine's model, animation, level and
texture formats. **Version 1.7.4**, Blender 5.0 or newer.

Install it with **Edit ▸ Preferences ▸ Get Extensions ▸ ⌄ ▸ Install from Disk...**
and pick the `fotr_importer` folder. Its own `README.md` covers what it reads and
where the game keeps it; `fotr_modding_reference.html` beside it is the format
reference.

| | |
|---|---|
| `fotr/model.py`, `mesh.py`, `strip.py` | geometry, including the triangle strips the engine stores |
| `fotr/anim.py` | animation out of the matching `.adu` |
| `fotr/level.py` | levels |
| `fotr/texture.py` | textures |
| `fotr/srsc.py`, `database.py` | the SRSC container and the databases inside it |
| `fotr/klass.py` | the class records that say what a database entry *is* |
| `fotr/write.py` | writing, which is what makes this a modding tool rather than a viewer |

## Still to do here

The importer covers geometry, animation and textures. What it does not yet cover,
and what modding needs before it is a pipeline and not just a converter:

* **Sound.** No reader.
* **The interface strings.** SRSC containers (`Common/Interface/Interface.odu`,
  `Interface.xdu`) hold them. A string record is `uint16 len` followed by `len`
  bytes, NUL-padded, with the invariant `(2 + len) % 4 == 0`.
* **Authored object properties.** Item, level and GUI properties are named in
  plain text inside `Fellowship.rfl`: `Cell Width (Screen %)`,
  `Model To Use In Inventory`, `ModelFOV`, `Inventory Scale`, and several hundred
  more. The engine registers 397 classes and 4,262 properties this way, with the
  developers' own names on them, which is the vocabulary a modding tool needs to
  present. Reading it is solved; exposing it through the importer is not.
* **A mod folder the game reads**, so additions can be loaded and unloaded from
  the main menu without editing an ini by hand. That is pillar 4 as much as 3.

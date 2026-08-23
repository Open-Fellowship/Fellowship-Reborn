# LOTR: Fellowship of the Ring (Riot Engine) importer for Blender

Imports meshes, textures, skeletons and animations straight out of the retail game
data of *The Lord of the Rings: The Fellowship of the Ring* (Surreal Software, 2002),
which runs on the Riot Engine, the same engine as *Drakan: Order of the Flame*.

## Install (Blender 5.0+)

Drag `fotr_riot_importer.zip` into a Blender window, or
**Edit ▸ Preferences ▸ Get Extensions ▸ ⌄ ▸ Install from Disk...**

Blender 5.0 or newer. Earlier versions are not supported: 4.3 in particular renders
cutout transparency incorrectly, so foliage and hair come out as opaque black cards
even though the imported data is right.

## Use

**File ▸ Import ▸ LOTR Fellowship Model (.mdu)**

Point it at any `.mdu` in the game folder and pick a model from the dropdown:

| Archive | What's inside |
|---|---|
| `Common\NPC\NPCs.mdu` | every character, Legolas, Gimli, Balrog, Cave Troll, orcs, villagers |
| `Common\Resources\Resources.mdu` | weapons, items, inventory props |
| `Common\System\System.mdu` | Frodo, Gandalf and Aragorn |
| `World Common\World Common\World Common.mdu` | shared trees, rocks, grass, barrels, carts |
| `Levels\<World>\<Level>\<Level>.mdu` | that level's buildings, trees and props |

Type part of a name into **Find** to narrow the model list (`legolas`, `orc`, `house`),
then pick the one you want from **Model** and import it. One character at a time is the
default. Tick **Import Every Match** only when you want the whole filtered list at once
(an empty Find plus that box brings in all 238 Hobbiton props in about five seconds).

Textures are picked up automatically from the matching `.tdu` next to the `.mdu`;
animations from the matching `.adu`. A model can also point into a sibling database (the
scarecrow's wooden pole comes from `World Common.tdu`), and those are followed
through the folder's `.db` manifests.

If the `.tdu` is not beside the `.mdu`, or a sibling archive cannot be reached from
where the file sits, the import still succeeds but says so:

    Imported 1 model - no Shire Common.tdu beside this .mdu, so nothing is textured
    Imported 1 model - 2 textures could not be found; they are in World Common.tdu

Moving an archive into a backup folder is the usual cause. Earlier versions returned
untextured materials without comment, which reads as a broken importer when the cause is a
misplaced file.

## Importing a whole map

**File ▸ Import ▸ LOTR Fellowship Level (.lvl)**

Point it at a level file (`Levels\Shire\Hobbiton\Hobbiton.lvl`) and you get the map:
the terrain heightmap with its per-cell textures, and every building, tree, fence and
prop standing where the game puts it. Hobbiton is roughly 950 objects on 83k terrain
triangles and takes about eight seconds.

A level's `.mdu` is only the prop *library*, everything piled at the origin. The `.lvl`
is what places it. Models come from wherever the level's classes point, so importing
Hobbiton pulls from World Common, Shire Common and NPCs automatically by following the
`.db` manifests.

| Option | What it does |
|---|---|
| Terrain / Objects | either half can be imported alone |
| Skip Markers | drops AI waypoints, trigger volumes, sound emitters, light markers |
| Skip Collision Volumes | drops the invisible walls the engine collides against but never draws |
| Skip Hidden Objects | drops objects the level itself flags as not visible |
| Share Mesh Data | repeated props reuse one mesh, so 85 flowers cost one flower |

Terrain arrives split by layer type, so `<Level>_floor` is the ground and `<Level>_between`
is the water. The water layers are flat (Hobbiton's river varies by less than two units
across the whole map) and carry a single environment-reflection texture that the engine
applies as a runtime reflection, not through the stored UVs. They import with that
texture mapped flat, which reads as a tiled pattern. Give that one object a glass or
water shader and the river looks right.

**File ▸ Import ▸ LOTR Fellowship Textures (.tdu)** dumps a whole texture archive to PNG.

## Putting textures back into the game

**File ▸ Export ▸ LOTR Fellowship Textures (.tdu)**

Two modes.

**Match By Name** replaces every texture whose name matches an image. Use it when you
extracted an archive, edited some of the files, and want them all put back. Images come
from either:

* **This .blend** (the default): import a model or level with textures, edit them right
  there in Texture Paint or the Image Editor, then export. Nothing goes through disk.
* **Folder**: dump an archive to PNG, edit in any external editor, point the exporter at
  the folder. Keep the filenames the dumper gave them.

**Replace One** puts one image into one texture, whatever either of them is called. This
is what you want when you have brought your own texture in from elsewhere (a downloaded
material, something painted in Substance, an asset-pack `T_Whatever_BC.png`), and you want
it on a game prop. Type part of a name into **Find** to narrow the list (an archive holds
up to 172 textures), pick the texture, pick the image, export.

**Re-skin A Model** is usually the one you want. Instead of hunting through the archive,
pick the model you are re-skinning (**Find** narrows that list too), and the **Slot**
dropdown shows only the textures that model actually uses, named and sized:

    slot 0 - b_pumpkin_top   (128x128)
    slot 1 - a_pumpkin_skin  (32x32)

Choose one slot to replace just that, or **Every slot** to put the same image across the
whole model. Every slot is the right answer when your Blender mesh has a single material
but the prop you are overwriting had several: the mesh export puts every face on slot 0,
and this makes the other slots agree. Each destination is resampled to its own size, so
one picture covers a 128×128 and a 32×32 slot in the same write.

**A model's textures are not always all in one archive.** The scarecrow is the clearest
example: three of its five slots are in `Shire Common.tdu`, but its wooden pole
(`c_good_wood`) and `b_shiney_brown` come from `World Common.tdu`. The Slot list shows all
five and labels the outsiders:

    Every slot in this archive (3 of 5)
    slot 0 - c_good_wood         (lives in World Common.tdu)
    slot 1 - c_dirtycanvas       (128x128)
    slot 2 - c_dirtycanvas_edge  (128x128)
    slot 3 - b_dirtycanvas_head  (128x128)
    slot 4 - b_shiney_brown      (lives in World Common.tdu)

Every slot writes the three it can reach and says so, naming the others and where they
live. Open that archive and repaint them there, but check what else uses them first,
because shared world textures like `c_good_wood` are on a great many props.

Slot matching uses the database id as well as the texture id. The two numbering spaces
overlap, so matching on the id alone can aim a write at an unrelated local texture that
happens to share a number.

*Fit To Original Size* (on by default) resamples your image to whatever size that texture
already is. It is worth leaving on: it keeps the record the same length so the write goes
in place, and 256×256 is the largest texture the game itself uses, so a 4K source is
mostly wasted bytes.

**Match By Name writes back everything it recognises**, and that includes images left over
from an earlier import. If your `.blend` still holds the stock `c_dirtycanvas_edge` from
when you imported the scarecrow, a Match By Name write will put the stock one back and undo
whatever you had painted into the archive. Three ways to stop that:

* **Everything That Matches**: the old behaviour, right when you extracted an archive,
  edited some files and want them all put back.
* **Only What The Selection Uses**: only textures used by the materials on the selected
  objects. Select the prop you are re-skinning and nothing else can be touched.
* **Only Names Containing**: type `pumpkin` and only pumpkin textures are considered.

The exporter also names every texture it wrote in the status bar, so an accidental revert
is visible the moment it happens, not the next time you load the level.

A texture record belongs to the archive, not to a model, and nothing in the `.tdu` says
who else is using it. After writing, the exporter checks the `.mdu` beside it and tells
you if the texture you just replaced is on other models too: repainting `b_pumpkin_top`
to re-skin the Pumpkin also re-skins `pumkins_group`, which is better to hear now than
in game.

What it does and does not touch:

* Textures whose image still matches the archive are left alone. Edit one file out of
  172 and exactly one record is rewritten.
* An edit at the original resolution keeps the record the same size, so it is patched
  in place and every other byte of the archive is untouched. A resize triggers a full
  rebuild instead, which is still exact but moves things around.
* The original is copied to `<name>.tdu.bak` before the first write.
* **Verify After Writing** reads the archive back and compares each replaced texture
  against the image it came from, reporting the worst channel error. It should be 0.

Each texture is re-encoded into its original format: same bit depth, same palette
size, same colour key, same flags. For palettised textures the palette is rebuilt from
your image by median cut over all four channels, and a texture that uses a colour key
keeps its transparent index exactly where the record says it is, so cut-outs survive.

Round-tripping all 720 textures in the game through decode → encode → decode is
pixel-identical, and rebuilding all 39 archives with no changes reproduces them byte
for byte.

## Putting meshes back into the game

**File ▸ Export ▸ LOTR Fellowship Model (.mdu)**, with the mesh selected. Selecting the
armature of a rigged character counts, and it finds the mesh under it.

A mesh imported by this add-on remembers the archive and the model it came from, so the
file browser opens already pointing at them and you can go straight to Export. A mesh that
does not (one imported before v1.6, appended from another file, or built from scratch)
needs the archive picking and the **Overwrite Model** dropdown setting; it will refuse
and not guess. Use **Find** to narrow the list, and set **LOD** and **Scale** to match
however the mesh was built.

Two ways to write, picked automatically unless you override them. Automatic writes
vertices only when the vertex count still matches, and falls back to the whole mesh when it
does not:

* **Moved Vertices Only**: positions and normals, nothing else. The face list, UVs, rig,
  weights and every other LOD are left as they were, so this is the safe mode and
  the only one available for rigged characters. Sculpt, nudge, reproportion: anything that
  moves vertices without adding or removing them.
* **Whole Mesh**: vertices, faces, UVs, material assignments and bounds. Use it when you
  have added, deleted or re-cut geometry. Refused on rigged and multi-LOD models, because
  their vertex weights and LOD ranges are indexed against the old face list and there is no
  way to keep them honest.

**Texture slots.** A model's faces do not carry a texture, they carry a *slot number*, and
the model's header says which game texture each slot points at. Blender material slots map
onto those in order, so **a mesh with one material puts every face on slot 0**, whatever
slot 0 happens to be. That is the single most confusing thing about re-skinning, because
slot 0 is not always in the archive you are working in:

    Pumpkin     slot 0  b_pumpkin_top       Shire Common.tdu   <- repaint works
    scarecrow   slot 0  c_good_wood         World Common.tdu   <- repaint here does nothing

The export panel now lists the target model's slots with their texture names, marking the
ones that live in another archive, and **Texture Slots** decides what to do about it:

* **Leave As They Are**: keep the model's list, map materials onto it in order. The old
  behaviour.
* **Point Everything At One**: give the model a single slot, chosen from a list that
  starts with the model's own slots and then offers every texture in the `.tdu` beside the
  archive (with a Find box). Every face goes to slot 0. This is the right answer for a
  one-material mesh, and it is what the retail single-texture props look like.
* **One Per Material**: one slot per Blender material, each matched to a texture by image
  or material name, falling back to the model's old slot where nothing matches and saying
  which ones it guessed at.

If the texture you point at lives in another database, the exporter says so, and you do not
find out in game.

**More materials than the model has slots.** A face carries a slot *number*, and the engine
looks that up in the model's texture list, so 20 materials against a 3-entry list means
faces asking for slot 19 of an array with 3 in it. That is refused, not written,
with a message saying which mode to use instead, because the symptom in game is a crash or
garbage, with nothing pointing back at the export. **One Per Material** grows the
list to match; **Point Everything At One** collapses it to a single slot.

The most textures any shipped model declares is 14, so past that you are outside anything
proven to work and the exporter says so: it writes the file, but tells you it is untested
territory.

**Strips are rebuilt, not dropped.** Every model the game draws carries a `0x0211` record:
the triangle strips the renderer actually pushes. A whole-mesh write regenerates it from your
mesh, grouped by texture and cut into windows of at most 224 vertices, because a strip index
is a single byte. The exporter reports how many groups and vertex spans it produced.

Versions up to 1.6.5 deleted that record instead, on the belief that it was an optional cache
of merged coplanar faces. It is not, and deleting it produced the long twisted ribbons that
looked like a culling bug, changed with screen resolution, and made the model's apparent
vertex limit move every time it was measured. There is no "vertex budget" rule in this
add-on any more, because there was never a real one, only a fallback path being overrun. If
you decimated a model to get it to draw, you can put the detail back.

**Keep a .bak** copies the archive once before the first write. **Verify** reads the file
back and reports the worst vertex error, which should be zero. **Dry Run** reports what would
change and writes nothing.

Faces are grouped by texture on the way out. Every LOD in the retail data bar one,
842 of 843, uses each texture in exactly one unbroken run, and a mesh that returns
to a texture it has already used comes back with faces missing. Blender has no
reason to order faces that way, so the exporter sorts them and says so in the report.

Everything is triangulated on the way out. There is not one quad in the retail
data (all 517135 polygons the game ships are triangles), and a quad written into
a model record comes back in game as long twisted shards, so a mesh modelled
elsewhere is cut down automatically. UVs and material assignments follow each
fragment.

The export warns, and does not refuse, when a mesh is much larger than the
model it replaces, or heavier than anything the game ships. The biggest retail
model is the Dark Rider at 3684 vertices and 6783 triangles; nothing proves that
is a hard ceiling, but it is the only evidence there is.

Things to know:

* Move or rotate the *object* and the model exports moved. Leave it alone and the export is
  positionally exact.
* Materials map back to the model's texture slots by the order they were imported in, so
  reassigning faces between the materials already on the mesh works. Adding a brand new
  texture does not: the texture list lives in a record the exporter does not touch yet.
* About thirty models in the retail data ship with duplicate faces, which Blender cannot
  hold: Sam loses 12 of his 3103. Those models can still take vertex edits; the report
  says how many faces Blender has versus the model so you know nothing went missing.

### Options that matter

* **LOD**: characters ship 2-3 detail levels in one archive. 0 is the good one.
* **Max Animations**: a character references up to ~24 animations; the default imports
  the first 8. Use **Animation Filter** (`walk`, `attack`, `death`...) to pick.
* **Texture Folder**: PNGs are written here, defaulting to Blender's temp folder.
  Pack Into .blend keeps everything self-contained.
* **Use Alpha**: on by default in both importers. Leaf cards, hair and capes are cutouts;
  with it off they render as opaque slabs.
* **Combine Into One Object**: with Import Every Match on, merges the whole batch into a
  single mesh with one shared material list. Rigged characters stay separate so their
  skeletons and animations survive.
* **Put In A Collection**: groups the import under a collection named after the archive.
* Animations carry root motion, as the game authored them.

## Notes

* Object placements carry the engine's own conventions: rotations compose in XYZ order
  and yaw is offset by -90 degrees. Without that offset every door, window and building
  facade sits a quarter turn out of true, which is easy to miss because most props are trees
  and rocks with no obvious front.
* Triangles are wound clockwise-front in the game data and are reversed on import, so
  normals point outward and terrain faces up.
* Transparency comes in two flavours and they are treated differently. Foliage, hair and
  faces use a binary cutout (a colour-keyed palette index, or one-bit alpha) and are
  clipped; cobwebs and lens flares carry graduated alpha and are blended. Blending a
  cutout is what makes beards and leaf cards break up into shards in the viewport.

* Every `0x0203`, `0x0204` and `0x0207` record in the game, 1592 of them, round-trips
  through the writers byte for byte, so an unedited export changes nothing it should not.
* Models named `*_collider` hold no drawable geometry, only bounding volumes, so
  they are skipped and counted in the import report.
* The `fotr/` subpackage is plain Python with no Blender dependency, so it can be used
  as a standalone library for scripting extractions.

## Format credit

The Drakan-era SRSC container was documented by Zalasus in the OpenDrakan project.
Fellowship uses a later revision (`0x0101`) whose model, texture and animation records
differ; those differences were reverse engineered from the retail data and are
documented in the source comments.

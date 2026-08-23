# architecture

Notes on how the Riot Engine is put together, independent of any one fix.

The pieces worth reading first:

* **The virtual screen.** The engine lays out in a space that is always 128
  units wide. `camera+0x228` (`halfW`) is 64.0 at every resolution;
  `camera+0x22C` (`halfH`) is `64 * H / W`, so 48.0 at 4:3 and 36.0 at 16:9.
  128 x 96 is exactly 640 x 480 divided by 5, which is the space the interface
  was authored in and the reason so much of the HUD only holds together at 4:3.

* **Field of view is horizontal.** `focal = 64.0 / tan(fov * pi/360)`, and the
  64.0 is `halfW`. The vertical field is whatever falls out of `halfH`.

* **The GUI unit system.** Every control carries a pixels-per-unit factor at
  `+0x9C` and a size at `+0x98`. Controls built from a template take both from
  authored properties; controls without one get the hard-coded 640x480
  constants `5.0` and `1.0` and therefore never scale.

* **`Fellowship.rfl` is game code, not data.** It is an ordinary PE32 DLL
  holding the GUI, the inventory, the item system and the quest logic, reaching
  the engine through vtables handed to it at load.

* **The camera is not where it looks.** `camera.md` has the measurements: the
  camera object holds no world position, the engine renders camera relative
  because of float precision at 400,000-unit coordinates, and culling follows
  the field of view rather than the frustum planes.

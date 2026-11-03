# Task 0375: VRC Light Volumes baked in Blendity (round 398)

## Asked
"Have a toggle so I can choose to bake the data in Blendity if I wanted to and is safely exported into Blendity" (plan
cozy-fluttering-corbato, round 398): the toggle (round 397's Bake in Blendity) on, Blendity bakes the volume and the
data reaches Unity safely; volumes and their data come back into Blendity safely.

## Built
- **The bake** (`render/light_volume_bake`, `editor/light_volume_bake_ui.cpp`): Generate Lighting's last phase. Each
  Light Volume with Bake in Blendity on bakes its voxel grid (the package's voxel centres) as probes with the same path
  tracer, lights (Mixed and Baked) and settings as the scene's probe volume, in slices of the frame's budget - its
  validity checked in slices too: a voxel the probe check would move out of geometry is marked inside (1, Unity's
  sense) and left at its centre for the package to fill. A grid past 2,000,000 voxels is refused, said. Each probe's
  ambient cube is fitted to the package's SH L1 - `L0 + L1 . n`, Unity's SphericalHarmonicsL2 in its shader-constant
  form (irradiance / pi, what Blendity's probes hold): irradiance has no odd band past 1, so L1 along an axis is
  exactly half its positive less its negative, and L0 the six directions' mean. ADR 0097 records the format.
- **Saved safely**: `LightVolumes.bin` beside the scene's lightmaps - per volume its object id, grid, whether it is
  Unity's read back, the rotation it was baked at, CRC-32 and floats; a volume that isn't all numbers isn't saved, one
  whose checksum doesn't match isn't loaded. Saved with the scene whenever the bakes changed, read when it opens (with
  or without lightmaps); Clear Baked Data and a cancel take them and the run's state with them.
- **To Unity, safely**: Live Link's `lightvolumebake` (after the volume's own message; again when the bakes change;
  never with the toggle off, never Unity's own read back, never for another grid than the volume's) carries the floats
  in Unity's axes and voxel order (X mirrored), the grid, the baked rotation and their checksum. The bridge takes it
  only when the volume there has that grid, the size and checksum match and every value is a number, the Unity scene is
  saved and the volume has its Manager; a bake already there (its checksum kept in the project's Library) writes
  nothing. Unity's own bake is copied into a `Backup` folder beside its textures before it is first replaced (a bake of
  Blendity's isn't backed up again), then the package saves it as its own custom bake (`SaveCustomProbesBaked`, with the
  Manager's dilation and denoise settings), the volume's Bake goes off and its baked rotation is the one sent. Refused,
  nothing is written and the console says why.
- **Back into Blendity**: a Unity scene's Light Volume whose three textures are in the project comes in with Unity's
  bake, read from the Texture3D assets (RGBAHalf, GraphicsFormat 48 or TextureFormat 17; the package's L1 scale undone,
  X mirrored back), marked as Unity's - shown, never sent back.
- **The preview**: a selected volume's voxel dots take their voxel's baked light (L0) - Blendity's bake, or Unity's read
  back. (Lighting the Scene view's objects from the volumes is not done: the Game view and Unity show the bake.)

## Checked
- The real package (3.0.1, a copy of Test World): the recorded session's hall baked in Blendity (6 x 3 x 6 voxels) saved
  by the package - three textures, Bake off, nothing refused; voxel 0's L0 0.31494 there for 0.315 sent; the same data
  with one byte changed refused by its checksum. With the Manager's Denoise on (its default) the package smooths the
  bake as it smooths its own - up to 11% of the brightest light on this small grid; the harness turns it off so the
  fixture is Blendity's data exactly.
- The round trip: the package's three textures (kept in `tests/fixtures/light_volume_unity`) read back as the bake
  Blendity sent - 108 voxels, at most 0.03% of the brightest light apart.
- tests_lighting: the fit (a linear field back exactly, validity in Unity's sense); Unity's axes and back; textures
  read back; the file (version 2's source and rotation) and its checksum refusing a changed byte, a NaN and a grid past
  the cap refused; Generate Lighting baking a volume with the toggle on (not one with it off, nor a 300 x 50 x 300 m
  one past the cap) - its L0 and L1 up against the scene's own probe volume's at the same point within 10% (0.774
  and 0.767, 0.101 and 0.099).
- The Opus review's fixes: validity had been inverted (the package would have darkened voxels by walls), L1 25% weak,
  every resend rewritten and backed up, no voxel cap, the probes moved through thin walls, a cancel or Clear Baked Data
  leaving the bakes, Unity's bakes sent back as Blendity's, the file's load and save gaps, the Manager's settings.
- tests_livelink: the message at the sync (Unity's order, its checksum its floats'), again when the bakes change, none
  with the toggle off; the recorded session re-recorded with the bake.
- tests_assets: a Unity scene's Light Volume with its textures imported with Unity's bake.

# ADR 0097: VRC Light Volumes - Blendity's component, its bakes on disk and on the wire

- **Status:** accepted (rounds 397-398, tasks 0374-0375)
- **Context:** the user asked for VRChat worlds' VRC Light Volumes (red.sim.lightvolumes 3, MIT, in Unity) with a toggle
  to bake them in Blendity, "safely exported" to Unity and back. The package stores a volume's lighting as SH L1 in three
  RGBAHalf 3D textures it writes itself, and offers custom bakers an internal entry, `LightVolumeBaker.SaveCustomProbesBaked`.
  A Blendity bake must reach those textures without bypassing the package's own packing, dilation and checks, and
  without risking a Unity bake the user had.

## Decision
- **The data model:** a `LightVolumeComponent` holds the package's authoring fields (no lighting). Bakes live beside the
  scene's lightmaps in `LightVolumes.bin` - magic "BLVS", a version (2), then per volume its object id, grid, whether it
  is Unity's bake read back, the world rotation it was baked at, a CRC-32 and its floats (L0, L1 per colour, validity
  in Unity's sense: 0 open, 1 inside geometry). A volume whose CRC doesn't match isn't loaded; one that isn't all
  numbers isn't saved. Version 1 files (the first build's validity and L1) are refused: bake again.
- **On the wire:** Live Link's `lightvolume` (the fields; "none" taken off) and `lightvolumebake` (the floats attached in
  Unity's axes and voxel order, the grid, the baked rotation, the CRC - of the floats only: the rotation travels in the
  message's JSON beside them). A bake is sent only for a volume with Bake in
  Blendity on, whose grid is the bake's, and never when it is Unity's read back.
- **In Unity** the bridge writes only through the package: its components by its own Undo and setup routines, its bake
  by `SaveCustomProbesBaked` (the Manager's dilation and denoise settings), after checking the grid, size, CRC and numbers
  and that the scene is saved. Unity's own bake is copied to a Backup folder before it is first replaced; a bake already
  there (its CRC and the textures' sizes and times kept in the project's Library) is not written again.
- **Units:** L0 + L1 . n is irradiance / pi (Unity's SphericalHarmonicsL2 in its shader-constant form, what Blendity's
  probes hold), fitted exactly from the probe's six directions (L0 their mean, L1 half the difference of opposites).

## Consequences
- The package's internal entry is a dependency on its version (3.0.1 checked): the bridge reaches it by reflection and
  refuses, saying why, when it is missing.
- Blendity can read Unity's bakes (Texture3D YAML) but never sends them back as its own.

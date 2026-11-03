# Task 0374: VRC Light Volumes - the component, Live Link and import (round 397)

## Asked
"For the world we need to support VRC LightVolume" and "a toggle so I can choose to bake the data in Blendity" (plan
cozy-fluttering-corbato, rounds 397-398). Light Volumes 3.0.1 (red.sim.lightvolumes, MIT) is installed in Test World.

## Built
- **The Light Volume component** (Component > Rendering > Light Volume; `scene.h` `LightVolumeComponent`): the package's
  LightVolumeInstance as Blendity holds it - a unit cube about its object, sized and turned by the object's transform as
  the package's is; Dynamic, Additive, Color, Intensity, Smooth Blending, Exposure, Shadows, Highlights, Bake, Adaptive
  Resolution, Voxels Per Unit, Resolution; and **Bake in Blendity** (the toggle; its bake is round 398). Its grid
  (`light_volume_resolution`) is the package's: the box's size times Voxels Per Unit, each axis rounded half to even,
  at least 1. Drawn as an amber box, its voxel centres when selected.
- **A light's Light Volume option** (point, spot and area): a Point Light Volume beside Unity's light.
- **The Budget** lists a world's Light Volumes and Light Volume lights against what one Manager holds at once (32 and
  128), past them in the over colour.
- **Live Link** sends a volume (`lightvolume`: its fields; Unity's Bake off when Blendity bakes it; "none" when taken
  off) and the light's option in its fields. The Unity bridge (`LiveLinkLightVolumes.cs`) makes the package's own
  components - through UdonSharp's Undo helper in a world project, the proxy's fields copied to Udon - and runs the
  package's scene setup (`LightVolumeSceneSetup.OnboardHierarchy`), which registers them with the scene's Light Volume
  Manager and makes one when there is none. All by name and reflection: the bridge compiles without the package.
  After every change and move the bridge runs the package's own refresh (`LightVolumeTools.ApplyRuntimeState`, the
  light's `EditorApplyAuthoringData`, the proxy copied to Udon) - the link's edits publish no change event for the
  package's updater (review 397: a moved volume had lit its old box). A volume taken off in Blendity stays in Unity (it
  may hold Unity's bake); a light's Light Volume turned off (or the light made directional) switches its Point Light
  Volume off. A Point Light Volume's brightness is Unity's Intensity times 100 (the package's default 100 at Unity's
  1) - an approximation; an area light's size there is its object's scale (the package reads it from the transform).
- **Unity import**: a scene's or prefab's LightVolumeInstance comes in as Blendity's Light Volume with its fields, a
  PointLightVolumeInstance turns its light's option on (after the components: the light may come later in the file);
  both stay Engine Components, written back as they were (named for the package even when its scripts aren't in the
  project). A disabled Point Light Volume turns nothing on; one with no Unity light (the package's own light) is
  counted by the Budget from its Engine Component.
- **Live Link's message keys** take 5 bits for the kind (review 397: the 16th kind had spilled into the id, and two
  volumes on neighbouring ids replaced each other's change in one tick).
- **Godot, Unreal, Blender**: their bridges ignore the message; the component is saved with the scene.

## Checked
- The real package in a copy of Test World (`run_sdk_tests.py`): the recorded session's Hall Volume became a
  LightVolumeInstance (intensity 1.25, its 6 x 3 x 6 box, registered), the Lamp a PointLightVolumeInstance (registered),
  one Light Volume Manager made by the package's setup; the world's SDK checks list no error.
- The real package's derived data after the session: the volume's inverse world matrix takes its centre to 1.5e-8 m
  from the origin, the light's position its transform's - the package's refresh ran.
- tests_livelink: the grid (half to even: 2.5 to 2, 3.5 to 4; a huge box bounded), the save (the light's option too),
  the Budget's rows (the package's own lights from Unity, a disabled one not), the message at the sync, again when
  changed, "none" taken off, four volumes changed in one tick all arriving; the recorded session re-recorded.
- tests_assets: a scene's Light Volume and Point Light Volume read in.

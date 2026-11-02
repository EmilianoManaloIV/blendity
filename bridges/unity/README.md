# Blendity Live Link for Unity

A Unity Editor package that keeps GameObjects in the open scene in step with the scene open in Blendity, as you work
(task 0174; the protocol is [docs/LIVELINK.md](../../docs/LIVELINK.md)). One way: Blendity is the source.

## Install

Unity 2022.3 LTS or newer.

- **From Blendity:** Window > Live Link > Install a Bridge > Unity Project..., and pick the project's folder: the package
  goes into its `Packages` folder (an embedded package Unity loads when the project opens).
- **By hand:** `python tools/package_bridges.py` makes `com.blendity.livelink-<version>.tgz` (Package Manager > + > Add
  package from tarball) and `BlendityLiveLink-<version>.unitypackage` (Assets > Import Package > Custom Package); or
  Add package from disk with `bridges/unity/com.blendity.livelink/package.json`. Every file has its `.meta`
  (`tools/unity_meta.py`), as an immutable package needs.

## Use

Nothing to do: with Auto Connect on (Window > Blendity Live Link; on by default) it connects whenever Blendity's Live
Link runs - Blendity starts it with itself and writes `~/.blendity/livelink.json` saying where - and after Blendity
closes or restarts it waits for it again. Disconnect holds it off until Connect; a port overrides the file's.

The objects appear under a GameObject "Blendity Live Link" in the open scene. Each carries a `BlendityLink` with its
Blendity id; nothing else in the scene is touched. Blendity is Unity's own left-handed Y up, so positions, rotations
and scales come across as the same numbers.

| Blendity | Unity |
|---|---|
| objects, hierarchy, transforms | GameObjects with the same parents and local transforms |
| meshes, modifiers applied | Meshes (a submesh per material slot; 32-bit indices past 65,535 vertices) |
| vertices moving (Edit Mode drags, skinning, physics) | the mesh's vertices moved in place, normals worked out again (hard edges stay hard) |
| PBR materials and their maps | URP Lit, or Standard in the built-in pipeline (colours made gamma), opaque, cutout or transparent, culled or double-sided; Base, Normal, Metallic and Emission maps from the pixels Live Link sends (round 377) |
| Toon materials | the same Lit material, and a `BlendityToon` component holding every toon field per slot for your own toon shader |
| lights | Directional, Point, Spot, Rectangle and Disc lights with the same intensity, range and angles (Unity's area lights are baked only: they light nothing until you bake) |
| cameras | Cameras (disabled, so the Game view keeps yours) with the same field of view, clipping and physical lens |
| the look (round 376) | RenderSettings (Trilight ambient, the procedural or panoramic skybox, linear fog); colours made gamma; a Blendity Look object keeping the scene's own lighting (Restore Scene Lighting in its menu); in a URP project a global Volume with ColorAdjustments, Tonemapping, Bloom and Vignette (turn Post Processing on for the camera) |
| armatures and bones | empty GameObjects posed as the bones; a skinned mesh (round 378) is a SkinnedMeshRenderer on them, its keys as blend shapes |
| clips (round 379) | an AnimationClip asset in Assets/Blendity Live Link/Clips (localPosition, localRotation, localScale curves) |
| LODs and colliders (round 380) | an LOD Group (the renderer, then `<name>_LOD<n>` children); Box, Sphere, Capsule and Mesh Colliders |
| VRChat setup (round 381) | with VRChat's SDK: the Avatar Descriptor on the avatar root (view, lip sync, eye look), PhysBones under "Blendity PhysBones", a Scene Descriptor when the scene has none; static flags for Contribute GI either way. Tested with stand-ins (`VrcStandIns`, `run_replay_tests.py`) |

## Test

- In Unity: add `"testables": ["com.blendity.livelink"]` to the project's `Packages/manifest.json` (a package's tests
  show only then), then Window > General > Test Runner > EditMode > `RecordedSessionLeavesTheScene` replays the
  recorded session in the repository (`tests/fixtures/livelink_stream.bin`).
- `ConnectsByItselfAndAppliesTheSession` connects with no port typed: start `py tools/livelink_fake_server.py --dir
  <folder>` first and give Unity the same `BLENDITY_PREFS_DIR=<folder>`; without a fake server it is skipped.
- **Run (2026-10-29, Unity 6000.6.7f1, batch mode):** both pass (`Unity -batchmode -nographics -projectPath <project>
  -runTests -testPlatform EditMode`), the package imports without a warning. The run found two faults, fixed:
  `TryAutoConnect` called while connected dropped the link and started over, and opening another scene left the
  linked objects behind with the old one - the link now starts again in the new scene.
- `LiveSessionTests.FollowsALiveBlendity` follows a running Blendity: Blendity's suite runs it with
  `BLENDITY_TEST_BRIDGE` (see [LIVELINK.md](../../docs/LIVELINK.md#testing-a-bridge-live-round-318)). Run on
  2026-10-29: LIVE OK, the longest editor frame 33.3 ms while a 32,514-vertex mesh deformed. A project made by
  Install a Bridge compiled the package with no error.
- Without Unity: `ReplayCheck/ReplayCheck.cs` runs the package's framing, JSON and glTF reading on the same session
  with any C# compiler (its header has the command); `ReplayCheck.exe --live <python> <repository's blendity folder>`
  finds the stand-in Live Link (`tools/livelink_fake_server.py`) through the discovery file twice, as the package does.

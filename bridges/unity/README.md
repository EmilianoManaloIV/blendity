# Blendity Live Link for Unity

A Unity Editor package that keeps GameObjects in the open scene in step with the scene open in Blendity, as you work
(task 0174; the protocol is [docs/LIVELINK.md](../../docs/LIVELINK.md)). One way: Blendity is the source.

## Install

Unity 2022.3 LTS or newer. Window > Package Manager > + > Add package from disk, and pick
`bridges/unity/com.blendity.livelink/package.json` in the Blendity repository.

## Use

1. In Blendity: Window > Live Link > Start (or the console's `livelink start`). It shows the port, 40100 by default.
2. In Unity: Window > Blendity Live Link > Port, then Connect.

The objects appear under a GameObject "Blendity Live Link" in the open scene. Each carries a `BlendityLink` with its
Blendity id; nothing else in the scene is touched. Blendity is Unity's own left-handed Y up, so positions, rotations
and scales come across as the same numbers.

| Blendity | Unity |
|---|---|
| objects, hierarchy, transforms | GameObjects with the same parents and local transforms |
| meshes, modifiers applied | Meshes (a submesh per material slot; 32-bit indices past 65,535 vertices) |
| vertices moving (Edit Mode drags, skinning, physics) | the mesh's vertices moved in place, normals worked out again (hard edges stay hard) |
| PBR materials and their base and normal maps | URP Lit, or Standard in the built-in pipeline, opaque, cutout or transparent, culled or double-sided |
| Toon materials | the same Lit material, and a `BlendityToon` component holding every toon field per slot for your own toon shader |
| lights | Directional, Point, Spot, Rectangle and Disc lights with the same intensity, range and angles (Unity's area lights are baked only: they light nothing until you bake) |
| cameras | Cameras (disabled, so the Game view keeps yours) with the same field of view, clipping and physical lens |
| armatures and bones | empty GameObjects posed as the bones (skinned meshes come across as moving vertices) |

## Test

- In Unity: add `"testables": ["com.blendity.livelink"]` to the project's `Packages/manifest.json` (a package's tests
  show only then), then Window > General > Test Runner > EditMode > `RecordedSessionLeavesTheScene` replays the
  recorded session in the repository (`tests/fixtures/livelink_stream.bin`).
- Without Unity: `ReplayCheck/ReplayCheck.cs` runs the package's framing, JSON and glTF reading on the same session
  with any C# compiler (its header has the command).

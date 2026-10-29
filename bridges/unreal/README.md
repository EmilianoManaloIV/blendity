# Blendity Live Link for Unreal

An Unreal Engine 5.3+ editor plugin (C++) that keeps actors in the open level in step with the scene open in Blendity,
as you work (task 0277; the protocol is [docs/LIVELINK.md](../../docs/LIVELINK.md)). One way: Blendity is the source.

**Status:** written; its protocol layer is compiled and tested in Blendity's own suite on every build; the plugin itself
has not yet been built or run in Unreal (its automation test below runs once Unreal is installed).

## Install

The project must be a C++ project (a content-only one: Tools > New C++ Class once), so Unreal can build the plugin.

- **From Blendity:** Window > Live Link > Install a Bridge > Unreal Project..., and pick the folder with the
  `.uproject`: the plugin goes into `Plugins/BlendityLiveLink` and is enabled in the `.uproject`.
- **By hand:** `python tools/package_bridges.py` makes `BlendityLiveLink-unreal-<version>.zip`; unzip it into the
  project's `Plugins` folder and enable it in Edit > Plugins.

Unreal builds it when the project opens.

## Use

With Auto Connect on (Window > Blendity Live Link; on by default) it connects whenever Blendity's Live Link runs -
Blendity starts it with itself and writes `~/.blendity/livelink.json` saying where - and after Blendity closes or
restarts it waits for it again. Disconnect holds it off until Connect; a port overrides the file's.

The actors appear in a folder "Blendity Live Link" of the World Outliner, transient (the level isn't changed by them).
glTF's (x, y, z) in metres is Unreal's (100 z, -100 x, 100 y) in centimetres: Blendity's front is Unreal's +X.

| Blendity | Unreal |
|---|---|
| objects, hierarchy, transforms | actors attached as the objects are parented |
| meshes, modifiers applied | transient static meshes, a section per material slot |
| vertices moving | the mesh rebuilt with the moved positions, normals worked out again |
| materials | dynamic instances of the engine's BasicShapeMaterial with the base colour (textures, metallic and roughness not yet) |
| lights | directional, point and spot lights attached to their objects |
| cameras | camera actors, Blendity's vertical field of view turned into Unreal's horizontal one at 16:9 |
| armatures and bones | empty actors posed as the bones (skinned meshes come across as moving vertices) |

## Test

- In Blendity's suite (every build, Windows and Linux): `tests_round297` replays the recorded session through
  `BlendityLinkProtocol.cpp`, checks every mesh, the deform, the axes and the discovery file.
- In Unreal: Session Frontend > Automation > `Blendity.LiveLink.Replay`, or
  `UnrealEditor-Cmd <project> -ExecCmds="Automation RunTests Blendity.LiveLink.Replay; Quit" -unattended -nullrhi`.

# Blendity Live Link for Unreal

An Unreal Engine 5.3+ editor plugin (C++) that keeps actors in the open level in step with the scene open in Blendity,
as you work (task 0277; the protocol is [docs/LIVELINK.md](../../docs/LIVELINK.md)). One way: Blendity is the source.

**Status:** tested in Unreal 5.8.4 (2026-10-29). BuildPlugin builds it, `Blendity.LiveLink.Replay` passes with no
warning, and in the editor it connected by itself, through the discovery file, to a stand-in Live Link. Its protocol
layer is also compiled and tested in Blendity's own suite on every build. Building it needs the .NET Framework SDK 4.6
or newer (Visual Studio Installer > Individual components > .NET Framework 4.8.1 SDK), and a short package path: Unreal
refuses paths over 260 characters (`-Package=C:\uebl\pkg` works).

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
- **Live:** `Blendity.LiveLink.LiveSession` checks what the plugin's own connection made in the editor's level from a
  running Blendity. Blendity's suite runs it with `BLENDITY_TEST_BRIDGE`, with a short `BLENDITY_TEST_DIR` (see
  [LIVELINK.md](../../docs/LIVELINK.md#testing-a-bridge-live-round-318)). Run on 2026-10-29: LIVE OK.
- **What the live run found:**
  - The link dropped whenever Blendity was quiet. A non-blocking peek with nothing to read also succeeds with 0 bytes,
    so the plugin took it for a closed connection, and reconnected and re-synced every 2 seconds. It now asks the
    socket whether it is readable with nothing pending.
  - A deform of a 32,514-vertex mesh took 259 ms a frame. It now changes the kept mesh description's positions in
    place, builds without CPU data or collision, and builds once more with them half a second after the deforms
    stop: 145 ms. Unreal's own static-mesh build (about 75 ms) is most of what is left.
- **Install a Bridge:** a content-only project with the plugin from Install a Bridge builds
  (`Build.bat UnrealEditor Win64 Development -Project=<project>`), opens, and its replay test passes.

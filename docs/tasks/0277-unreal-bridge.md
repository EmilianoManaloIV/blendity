# 0277: the Unreal bridge, a C++ editor plugin

- **Status:** written (round 297); the protocol layer tested in Blendity's suite, the plugin after Unreal is installed
- **Requirements:** R-10
- **Decisions:** ADR 0055, ADR 0086
- **Model:** main session
- **PR:** #229

## Asked
Task 0274's request for Unreal; the user chose a C++ editor plugin.

## What changed
- `bridges/unreal/BlendityLiveLink/` for Unreal 5.3+: `BlendityLiveLink.uplugin`, an Editor module with a dock tab
  (Window > Blendity Live Link: Auto Connect, a port, Connect / Disconnect, the status), a non-blocking `FSocket` client
  ticked by `FTSTicker` with a 20 ms budget, the discovery file read every 2 seconds while not connected.
- **The protocol layer in standard C++** (`BlendityLinkProtocol.h/.cpp`: the framing, a JSON reader, the glTF binary
  reader, deforms with normals worked out again, the axes, the discovery file, the hello). glTF's (x, y, z) in metres is
  Unreal's (100 z, -100 x, 100 y) in centimetres: a mirror, so a rotation's quaternion is (-qz, qx, -qy, qw), scale
  follows the axes and the winding is reversed.
- The applier: transient actors in a "Blendity Live Link" folder of the open level, meshes as transient static meshes
  built from `FMeshDescription` (a deform rebuilds them), materials as dynamic instances of the engine's
  BasicShapeMaterial (its colour), lights and cameras as attached actors (Unreal's +X is Blendity's front: no turn),
  the vertical field of view turned into Unreal's horizontal one at 16:9.
- An automation test, `Blendity.LiveLink.Replay`, replays the session into a new editor world.
- After the review: a failed spawn checked before use, the materials held by `TStrongObjectPtr` (freed with the
  applier, not rooted for ever), an adopted actor's kind read from its tag (not respawned), WorkspaceMenuStructure and
  SlateApplication declared.

## Tests
- In Blendity's suite on every build (`tests_round297`, Live Link group): the recorded session framed whole by the
  plugin's reader (in two pieces), 5 meshes read with every `_VERT` in range, the normals worked out again agreeing with
  the file's at 878 of 878 vertices, the deform applied; the Box at (300, 100, 200) cm; a turn mapped then turned equal
  to turned then mapped; the discovery file and the hello.
- In Unreal (written, run after it is installed): the automation test above.

## Not done
- Compiled and run in Unreal: not yet. Textures, metallic and roughness (BasicShapeMaterial has a colour only); a
  content-only project needs a C++ class once so Unreal can build the plugin.

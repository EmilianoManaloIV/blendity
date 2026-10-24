# 0174: the Unity bridge for Live Link

- **Status:** done (round 193), its Unity-side code unrun (no Unity on the PC it was written on)
- **Requirements:** R-06 (file IO with the engines)
- **Decisions:** ADR 0055 (the protocol, one way)
- **Model:** main session
- **PR:** #138 (stacked on #137)

## Goal
The user: a Unity "bridge" applying Blendity's changes in real time (one way), after Blender's (task 0173).

## Design
`bridges/unity/com.blendity.livelink/`, a Unity 2022.3 package:
- `Editor/LiveLinkStream.cs`, `MiniJson.cs`, `GlbReader.cs`: no UnityEngine, C# 5 syntax - the framing, a JSON reader
  (Unity's JsonUtility reads only into fixed classes), the glTF reader into Unity's layout. Blendity is Unity's
  left-handed Y up, so mirroring glTF's X back (and the winding, and V) gives Blendity's own numbers: a vertex is
  (-x, y, z), a rotation (x, -y, -z, w), a scale as it is. The binary's split vertices stay (Unity's vertices carry
  their normals and UVs); `VertexOf` keeps each one's mesh vertex, so a `deform` moves them.
- `Editor/LiveLinkApplier.cs`: one handler per message; GameObjects under a root "Blendity Live Link" with a
  `BlendityLink` (the id, the kind, the armature of a bone); meshes with a submesh per slot; materials as URP Lit or
  Standard (base colour, metallic, smoothness = 1 - roughness, emission, base and normal maps loaded from their paths);
  a Toon material's fields as JSON per slot in a `BlendityToon` component; lights with Blendity's intensity as Unity's
  (they are the same quantity); cameras disabled so the Game view stays the project's.
- `Editor/LiveLinkClient.cs`: a TcpClient read from `EditorApplication.update`, at most 20 ms of applying a frame;
  `Editor/LiveLinkWindow.cs`: Window > Blendity Live Link.
- `Runtime/BlendityLink.cs`: the two components. `Tests/Editor/ReplayTests.cs`: an EditMode test replaying the recorded
  session. `bridges/unity/ReplayCheck/ReplayCheck.cs`: the UnityEngine-free parts on the session with any C# compiler.

The roadmap's "own GLB reader" is `GlbReader.cs`; Unity's glTFast would also read the binaries but drops the
`_VERT` attribute a deform needs.

## Acceptance criteria
1. The framing, JSON and glTF reading replay the recorded session: every message, every mesh's `_VERT` within its
   vertex count, the deform applied.
2. In Unity, the EditMode test leaves the Box at (1, 2, 3) with its 12 triangles and the Cel material with its
   BlendityToon, the Sphere gone, the bone turned 30 degrees.

An Opus review found `BlendityToon` sharing a file with another class (Unity needs a component's script in its own
file), the window's buttons changing between layout and repaint, `LightType.Rectangle` missing before Unity 2023.1,
a bad port leaving a half-made connection, normals recalculated with seams after deforms, transparency and culling
ignored, the domain reload ending the link silently and a blocking connect; all fixed (the normals now summed per
group of vertices that shared a mesh vertex and a normal, their winding checked by ReplayCheck against the file's).

## Results
- ReplayCheck compiled with the .NET Framework's csc (C# 5) and run on tests/fixtures/livelink_stream.bin: REPLAY OK,
  32 messages; meshes of 121, 24, 910, 144 and 24 vertices (121, 8, 362, 48, 8 in the mesh), the deform applied, and
  the normals worked out again from the positions agreeing with the file's at every vertex.
- Criterion 2 is not verified: Unity isn't installed on this PC, so `LiveLinkApplier`, `LiveLinkClient`,
  `LiveLinkWindow` and the EditMode test have not been compiled or run. They are written against Unity 2022.3's API;
  the first run in Unity is the check (README, Test).

## Tests
`Tests/Editor/ReplayTests.cs` (in Unity, by hand) and `ReplayCheck` (any C# compiler); the session itself is checked by
the C++ suite ("livelink: the recorded session ...").

# 0175: the Godot and Unreal bridges for Live Link (specs)

- **Status:** specified (round 194); each bridge gets its own task when started
- **Requirements:** R-06 (file IO with the engines)
- **Decisions:** ADR 0055 (the protocol, one way)
- **Model:** main session
- **PR:** #139 (stacked on #138)

## Goal
The user: Godot and Unreal bridges after Blender's and Unity's, on the same protocol. This task writes down what each
one does so it can be built against `blendity-livelink/1` unchanged; the Blender (0173) and Unity (0174) bridges are
the worked examples, and `tests/fixtures/livelink_stream.bin` is the test input for both.

## Godot 4 (`bridges/godot/addons/blendity_livelink/`)
- **Form:** an editor plug-in (`plugin.cfg`, `plugin.gd` an `EditorPlugin`) with a dock: Port, Connect / Disconnect,
  status. GDScript; no GDExtension (nothing to build per platform).
- **Connection:** `StreamPeerTCP.connect_to_host("127.0.0.1", port)`, polled from the plug-in's `_process` with a 20 ms
  budget a frame (`Time.get_ticks_usec()`), reading only while a message is incomplete (as the Blender bridge does),
  so Blendity's queue keeps replacing stale updates.
- **Framing:** a `PackedByteArray` buffer, `find(10)` for the newline, `JSON.parse_string` for the line, `bin` bytes
  sliced after it.
- **Axes:** Godot is right-handed Y up like glTF: positions, rotations (`Quaternion(x, y, z, w)`) and scales as they
  come. Godot's cameras and lights look down -Z: a camera or light gets a child `Node3D` turned 180 degrees about Y
  that carries the `Camera3D` / `Light3D` (Blendity's look along +Z).
- **Objects:** `Node3D` children of a root "BlendityLiveLink" in the edited scene, each with the metadata
  `blendity_id`; kinds as `MeshInstance3D`, the camera / light holder above, `Node3D` for armatures, bones and empties.
  Parents set when both exist; the sync's end removes what wasn't in it.
- **Meshes:** read the glTF binary with an own reader (as Unity's: `GLTFDocument.append_from_buffer` builds a whole
  scene and drops `_VERT`), into an `ArrayMesh` with a surface per primitive (`extras.slot`); keep `_VERT` per vertex
  for deforms (`surface_update_vertex_region` on the vertex buffer, or rebuild the surface's arrays).
- **Materials:** `StandardMaterial3D` (albedo, metallic, roughness, emission and its energy, base and normal map loaded
  with `Image.load_from_file`, cull mode from doubleSided, transparency from alphaMode); a Toon material's fields in
  `set_meta("blendity_toon", ...)`, and `diffuse_mode = DIFFUSE_TOON`, `specular_mode = SPECULAR_TOON` (Godot's own
  cel shading) as the default look.
- **Lights / cameras:** `DirectionalLight3D` (light_energy = pi x intensity, Godot's sun is in lux-like units only
  with physical light units off: documented as an approximation), `OmniLight3D` / `SpotLight3D` (range, spot_angle =
  half Blendity's cone), area lights as `OmniLight3D` (Godot has none); `Camera3D` (fov vertical, near, far,
  `PROJECTION_ORTHOGONAL` with size = 2 x ortho size).
- **Test:** `bridges/godot/smoke_test.gd`, run with `godot --headless --script`, replays the recorded session and checks
  what `bridges/blender/smoke_test.py` checks (in Godot's axes).

## Unreal 5 (`bridges/unreal/BlendityLiveLink/`)
- **Form:** an editor plug-in (`BlendityLiveLink.uplugin`, module type Editor, C++) with a tab (Window > Blendity Live
  Link): Port, Connect / Disconnect, status. Unreal's own Live Link framework is for animation streams to running
  subjects; this bridge edits the level, so it is a plain editor module.
- **Connection:** `FSocket` from `ISocketSubsystem` (TCP, 127.0.0.1), non-blocking, polled from an `FTSTicker` with a
  20 ms budget a tick, reading only while a message is incomplete.
- **Framing / JSON:** the same framing; `FJsonSerializer` for the line.
- **Axes:** Unreal is left-handed Z up in centimetres. Convert from glTF's axes with exactly the basis Unreal's own glTF
  importer uses (the Interchange glTF translator's vector and quaternion conversions; read them in the engine source
  when the bridge is built rather than from memory), times 100 for centimetres. Cameras and lights look along +X in
  Unreal; check that Blendity's +Z forward lands there through the basis, and add a fixed local turn if not. The smoke
  test's Box at Blendity (1, 2, 3) is the check.
- **Objects:** `AStaticMeshActor` (meshes), `ACameraActor`, `ADirectionalLight` / `APointLight` / `ASpotLight` /
  `ARectLight`, `AActor` with a scene component for empties and bones; each tagged `Blendity:<id>`; attached with
  `AttachToActor(KeepRelative)`; all in a folder "Blendity Live Link" of the World Outliner.
- **Meshes:** an own glTF reader into `FMeshDescription` (polygon groups per slot) built into a transient `UStaticMesh`
  (`BuildFromMeshDescriptions`); deforms rebuild from the kept description with moved vertex positions (a dynamic
  mesh component, `UDynamicMeshComponent`, is the faster route for frequent deforms and is the default for objects
  that have received a deform).
- **Materials:** a `UMaterialInstanceDynamic` of a small master material shipped in the plug-in's Content (BaseColor,
  Metallic, Roughness, Emissive, base and normal textures loaded from their paths with `FImageUtils`); toon fields in
  the actor's tags or an `UBlendityToonData` asset user data.
- **Lights / cameras:** intensity converted to Unreal's units (a directional light in lux: pi x intensity as
  Blendity's irradiance; point / spot in candela matched at 1 m as the MoonRay export does); `UCameraComponent` field
  of view converted from vertical to Unreal's horizontal at the level viewport's aspect.
- **Test:** an automation test (`IMPLEMENT_SIMPLE_AUTOMATION_TEST`, editor context) replaying the recorded session.

## What the protocol needed from these specs
Nothing new: both read `object`, `transform`, `pose`, `mesh` (glTF with `_VERT`), `deform`, `material`, `light`,
`camera`, `delete`, `play`, `sync` / `sync_end` as Blender and Unity do.

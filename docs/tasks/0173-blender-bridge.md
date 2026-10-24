# 0173: the Blender bridge for Live Link

- **Status:** done (round 192)
- **Requirements:** R-06 (file IO with Blender)
- **Decisions:** ADR 0055 (the protocol, one way)
- **Model:** main session
- **PR:** #137 (stacked on #136)

## Goal
The user: a Blender "bridge" applying Blendity's changes in real time (one way). Blendity's side is task 0172.

## Design
`bridges/blender/blendity_livelink/`, a Blender add-on (and a 4.2+ extension: `blender_manifest.toml`):
- `stream.py`: the framing (lines and `bin` attachments), pure Python, for a socket and a recorded file alike.
- `glb.py`: Live Link's glTF binaries read with numpy (Blender ships it): accessors, and `MeshData`, which folds each
  binary vertex back onto its mesh vertex (`_VERT`) so the Blender mesh has Blendity's own vertices - the ones a
  `deform` moves.
- `apply.py`: `Applier`, one method per message type. Objects in a collection "Blendity Live Link", found again by the
  custom property `blendity_id`; parents set when both exist (a child may arrive first); a sync's end removes what
  Blendity no longer has; glTF's axes to Blender's as Blender's glTF importer does (point (x, -z, y), quaternion
  (w, x, -z, y), scale (x, z, y)); cameras and lights turned so their -Z is Blendity's +Z forward, their children
  compensated through the parent inverse. Meshes through `foreach_set` (vertices, triangle loops, material slots,
  UVs); every face smooth with the edges where the binary split a vertex by its normal marked sharp, so Blender works
  the normals out again when a deform moves the vertices (`vertices.co`, when the topology id and vertex count
  match); triangles with two corners on one vertex dropped. An object the user deleted in Blender (or a reference undo
  invalidated) is forgotten and made again by its next object message; opening another file disconnects. A parent's
  inactive state hides its children, as in Blendity. Materials as a
  Principled BSDF with image textures from the map paths (the base map times the base colour, its alpha times the
  factor, a cutout as a step at alphaCutoff); a Toon material as an EEVEE cel setup (Shader to RGB, a
  constant ramp of Toon Bands steps from Toon Threshold, shadow colour x albedo below), its fields kept as the custom
  property `blendity_toon`. Lights in Cycles' units matched to Blendity's path tracer as the MoonRay export matches
  them (a sun gives pi x intensity W/m2; a point or spot pi x intensity at 1 m through Unity's range curve, P = 4 pi
  that; an area light radiance pi x intensity over its panel as the object's scale stretches it); a camera's or light's scale
  kept in glTF's axes (its turned frame's). Bones as empties (skins arrive as deforms).
- `__init__.py`: the connection read by a `bpy.app.timers` callback, non-blocking, at most 20 ms of applying a tick;
  what isn't read stays in the socket, so Blendity's queue replaces stale updates (ADR 0055). View3D > Sidebar >
  Blendity: Port, Connect / Disconnect, status.

The roadmap planned to import meshes with `bpy.ops.import_scene.gltf` into a staging collection. The add-on reads them
itself instead: the importer splits vertices by normal and UV, so a `deform` (positions per mesh vertex) would have
nothing to land on, and an operator needs a window context a timer doesn't have.

## Acceptance criteria
1. A recorded session replayed in headless Blender leaves the right scene: every object, the deleted one gone, the
   moved object where it was moved (in Blender's axes), the recoloured toon material with its cel setup, the moved
   vertex, the camera's angle and direction, the sun's strength, the bone hierarchy and the pose.
2. The add-on connects to a running Blendity and receives the sync with every mesh.
3. Blender's own extension validator accepts the manifest.

An Opus review found the scale of cameras and lights swapped in Y and Z, stale references stopping the link, the base
colour's tint and cutouts lost, normals frozen after deforms, children of inactive parents shown, and a quadratic
parent search; all fixed, the smoke test checking the cube's 12 sharp edges and a deleted object coming back.

## Results
- `blender -b --factory-startup --python-exit-code 1 -P bridges/blender/smoke_test.py` on Blender 5.2.2: SMOKE OK,
  32 messages, 9 objects (tests/fixtures/livelink_stream.bin).
- Live: Blendity headless with `livelink start`, the add-on registered in a headless Blender, `Connect` and its timer
  tick run by hand: the sync's 7 objects and 5 meshes (the 386-vertex subdivided cube included) arrived.
- `blender --command extension validate bridges/blender/blendity_livelink`: the manifest parses.

## Tests
`bridges/blender/smoke_test.py` (run by hand: Blender isn't in CI); the stream it replays is checked by the C++ suite
("livelink: the recorded session ...", tests/test_livelink.cpp).

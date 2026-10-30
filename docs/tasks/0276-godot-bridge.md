# 0276: the Godot bridge

- **Status:** written (round 296); its tests run once Godot is installed
- **Requirements:** R-10
- **Decisions:** ADR 0055, ADR 0086
- **Model:** main session
- **PR:** #229

## Asked
Task 0274's request for Godot: an add-on that follows Blendity and connects by itself (task 0175 had only a spec).

## What changed
- `bridges/godot/addons/blendity_livelink/` for Godot 4.2+: `plugin.cfg`, an `EditorPlugin` with a dock (Auto Connect,
  a port, Connect / Disconnect, the status and counts), a `StreamPeerTCP` client polled from `_process` with a 20 ms
  budget (a connection never waited for on the frame), the discovery file read every 2 seconds while not connected.
- Godot's axes are glTF's, so positions, rotations, normals and UVs come as they are; the winding is reversed into
  Godot's clockwise front faces. Meshes are ArrayMeshes built from the glTF binary by the add-on's own reader (one
  surface per material slot, `_VERT` kept for deforms, normals worked out again over normal groups as the Unity bridge
  does); materials as StandardMaterial3D (colour, metallic, roughness, emission, alpha mode, double-sided, base and
  normal maps); lights and cameras as children turned 180 degrees about Y (Blendity's front is +Z); objects under a
  "Blendity Live Link" node of the edited scene, owned by it so they show in the Scene dock; an earlier session's nodes
  adopted by their ids.
- `bridges/godot/project.godot`: a tiny project holding the add-on, so its tests run headless.

## Tests (written, run after Godot is installed)
- `smoke_test.gd`: the recorded session through the framing and the applier; the seven objects, the Sphere deleted, the
  Box at (-1, 2, 3) with 12 triangles, its Cel material recoloured with the toon fields kept, one vertex moved by the
  deform, the camera's 60 degrees looking at the scene, the sun, the bone turned 30 degrees.
- `auto_connect_test.gd -- <python>`: against the stand-in, as the Blender test does.

## Not done
- Run in Godot: nothing here has been run yet. Skinning in Godot (bones are plain nodes; a skinned mesh's vertices come
  as deforms).

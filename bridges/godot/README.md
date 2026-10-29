# Blendity Live Link for Godot

A Godot 4.2+ editor add-on that keeps nodes in the edited scene in step with the scene open in Blendity, as you work
(task 0276; the protocol is [docs/LIVELINK.md](../../docs/LIVELINK.md)). One way: Blendity is the source.

**Status:** written, not yet run in Godot (its tests below run once Godot is installed).

## Install

- **From Blendity:** Window > Live Link > Install a Bridge > Godot Project..., and pick the project's folder: the add-on
  goes into `addons/blendity_livelink` and is enabled in `project.godot`.
- **By hand:** `python tools/package_bridges.py` makes `blendity_livelink-godot-<version>.zip`; unzip it into the
  project (it holds `addons/blendity_livelink`), then Project > Project Settings > Plugins > enable Blendity Live Link.

## Use

Open a 3D scene. With Auto Connect on (the Blendity dock, right, beside the Inspector; on by default) it connects
whenever Blendity's Live Link runs - Blendity starts it with itself and writes `~/.blendity/livelink.json` saying where -
and after Blendity closes or restarts it waits for it again. Disconnect holds it off until Connect; a port overrides
the file's.

The objects appear under a node "Blendity Live Link" of the edited scene, each with its Blendity id as the metadata
`blendity_id`. Godot's axes are glTF's (right-handed, Y up, metres), so the numbers come as Blendity sends them.

| Blendity | Godot |
|---|---|
| objects, hierarchy, transforms | Node3D / MeshInstance3D with the same parents and local transforms |
| meshes, modifiers applied | ArrayMeshes, a surface per material slot |
| vertices moving | the surfaces rebuilt with the moved positions, normals worked out again (hard edges stay hard) |
| PBR materials, base and normal maps | StandardMaterial3D, opaque, scissor or alpha, culled or double-sided |
| Toon materials | the same StandardMaterial3D, every toon field in its metadata `blendity_toon` |
| lights | DirectionalLight3D, OmniLight3D, SpotLight3D (an area light as an omni) |
| cameras | Camera3D with the same vertical field of view, clipping and orthographic size |
| armatures and bones | plain nodes posed as the bones (skinned meshes come across as moving vertices) |

## Test

The folder `bridges/godot` is a tiny project holding the add-on:

    godot --headless --path bridges/godot --script res://smoke_test.gd
    godot --headless --path bridges/godot --script res://auto_connect_test.gd -- <python>

`smoke_test.gd` replays the recorded session (`tests/fixtures/livelink_stream.bin`); `auto_connect_test.gd` connects to
`tools/livelink_fake_server.py`, a stand-in Live Link, through the discovery file, and again after it restarts.

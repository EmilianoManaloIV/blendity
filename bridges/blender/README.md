# Blendity Live Link for Blender

An add-on that keeps a collection of the open Blender scene in step with the scene open in Blendity, as you work
(task 0173; the protocol is [docs/LIVELINK.md](../../docs/LIVELINK.md)). One way: Blendity is the source.

## Install

Blender 4.2 or newer (tested with 5.2.2).

1. Zip the folder `blendity_livelink` (the folder itself, not its contents).
2. In Blender: Edit > Preferences > Add-ons > the drop-down arrow > Install from Disk, and pick the zip. On 4.2 and
   newer it installs as an extension (the folder carries `blender_manifest.toml`).
3. Enable "Blendity Live Link".

## Use

1. In Blendity: Window > Live Link > Start (or the console's `livelink start`). It shows the port, 40100 by default.
2. In Blender: View3D > Sidebar (N) > Blendity > Port, then Connect.

The objects appear in a collection "Blendity Live Link". Each has its Blendity id as the custom property
`blendity_id`; nothing else in the file is touched. Disconnect leaves them there; connecting again brings them up to
date (and removes what Blendity deleted meanwhile).

## What comes across

| Blendity | Blender |
|---|---|
| objects, hierarchy, transforms | objects with the same parents (Y up turned to Z up, as Blender's glTF importer does) |
| meshes, modifiers applied | meshes with Blendity's own vertices, triangulated, UVs, custom normals, a material per slot |
| vertices moving (Edit Mode drags, skinning, physics) | the mesh's vertices moved in place |
| PBR materials and their texture files | Principled BSDF with image textures |
| Toon materials | a cel setup (Shader to RGB, a constant ramp of Toon Bands, the shadow colour): EEVEE; every toon field is in the material's custom property `blendity_toon` |
| lights | sun, point, spot and area lights; strength as Blendity's path tracer gives it (a sun of intensity 1 is pi W/m2) |
| cameras | the same vertical field of view, clipping, orthographic size, physical lens |
| armatures and bones | empties posed as the bones (skinned meshes come across as moving vertices) |

## Test

`smoke_test.py` replays a recorded session (`tests/fixtures/livelink_stream.bin`, which the C++ suite records and
checks) through the add-on in a headless Blender and checks the scene it leaves:

```
blender -b --factory-startup --python-exit-code 1 -P bridges/blender/smoke_test.py
```

# Blendity Live Link for Blender

An add-on that keeps a collection of the open Blender scene in step with the scene open in Blendity, as you work
(task 0173; the protocol is [docs/LIVELINK.md](../../docs/LIVELINK.md)). One way: Blendity is the source.

## Install

Blender 4.2 or newer (tested with 5.2.2: both tests below, and on 2026-10-29 a live connect to a running Blendity).

- **From Blendity:** Window > Live Link > Install a Bridge > Blender. Blendity finds Blender, copies the add-on into its
  extensions and has Blender enable it.
- **By hand:** `python tools/package_bridges.py` makes `dist/bridges/blendity_livelink-<version>.zip`; in Blender, Edit >
  Preferences > Get Extensions > the drop-down arrow > Install from Disk, and pick it.

## Use

Nothing to do: with Auto Connect on (the add-on's preferences; on by default) it connects whenever Blendity's Live Link
runs - Blendity starts it with itself and writes `~/.blendity/livelink.json` saying where - and after Blendity closes
or restarts it waits for it again. View3D > Sidebar (N) > Blendity shows the state ("Connected to Blendity
(SampleScene)"). Disconnect holds it off until Connect; a port in the preferences overrides the file's.

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

`auto_connect_test.py` (task 0275) runs the add-on against `tools/livelink_fake_server.py`, a stand-in Live Link:
connected with no port typed, the session applied, found again after a restart on another port, the hello sent.

    blender -b --factory-startup --python-exit-code 1 -P bridges/blender/auto_connect_test.py

`smoke_test.py` replays a recorded session (`tests/fixtures/livelink_stream.bin`, which the C++ suite records and
checks) through the add-on in a headless Blender and checks the scene it leaves:

```
blender -b --factory-startup --python-exit-code 1 -P bridges/blender/smoke_test.py
```

`live_session_test.py` follows a running Blendity: Blendity's suite runs it with `BLENDITY_TEST_BRIDGE` (see
[LIVELINK.md](../../docs/LIVELINK.md#testing-a-bridge-live-round-318)). Run on 2026-10-29: LIVE OK, the longest tick
4.3 ms while a 32,514-vertex mesh deformed.

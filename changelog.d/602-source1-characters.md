## 2026-11-19 (round 602): Import classic Source characters and materials

**Asked** Bring installed Source Filmmaker characters into a Blendity project for posing and rendering.

**Added (task 0584, ADR 0154)**

- Classic MDL v48/v49 with VVD v4 and VTX v7 companions. The default bodygroups, skin family and base LOD become a native mesh, with authored normals, UVs, joint hierarchy, bind matrices and weights. A missing companion names its virtual path.
- Recognized ValveBiped and TF-style humanoids keep their bone names. Their arm and leg bones point toward the next joint for native Auto IK. Static props import as meshes.
- Native editable materials keep base colour, diffuse texture, normal map, and alpha test or blend settings from supported VMTs. Missing and unsupported resources are named in the import report.
- An edited material keeps its project file when a missing VMT appears in a later search directory. Same-name textures from different virtual paths remain distinct across reimport.

**Checked** The focused Windows group passed 528 checks in 17 cases, including two editor reimports with a saved material edit. A separate compiled SFM Scout probe passed 64 checks covering material assignments, converted texture pixels and weighted deformation of both arms and legs.

**Checked** The focused Linux suite passed 204 checks, including a native asymmetric cutout render, weighted posing, ValveBiped Auto IK and marker Auto-Rig. The SFM Scout demo imported 7,814 faces, 78 joints, four material slots and four converted textures. Neutral bind displacement stayed under `0.6 µm`. EyeRefract used native fallback materials and reported its unsupported maps.

<!-- README -->
replace: MikuMikuDance characters; **Live Link** to Blender, Unity, Godot and Unreal.
with: MikuMikuDance characters; classic Source Filmmaker models with materials and bones; **Live Link** to Blender, Unity, Godot and Unreal.

<!-- docs/ARCHITECTURE.md -->
after: | `import_pmx_file`, `mmd_bone_name`
| `import_source1_model`, `import_source1_material` (`scene/source1_model.cpp`, `scene/source1_material.cpp`) | Read classic MDL, VVD, VTX, VMT and VTF through `SourceContent` into a native `ImportResult` with mesh, skin and editable material data. | Valve Source SDK 2013 and classic Source interoperability fixtures | ADR 0154, task 0584 |

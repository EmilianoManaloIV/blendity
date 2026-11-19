## 2026-11-19 (round 604): Compiled Source 2 characters and materials

**Asked:** Import Half-Life: Alyx and Counter-Strike 2 Workshop characters for posing and rendering in Blendity.

**Added (task 0586, ADR 0154)**
- Compiled Source 2 model and mesh resources through the mounted content resolver, with Source bone names, bind transforms, remapped vertex weights, UVs and face material slots.
- Compiled Source 2 materials with editable base color, normal, roughness, metalness, emission and cutout settings. Texture paths resolve through mounts and use the native import asset workflow.
- Path-specific diagnostics for missing resources, unsupported versions, texture encodings and shader features. Meshoptimizer and Zstandard streams use Blender's existing libraries.
- Full-width visibility masks preserve each model's authored mesh groups, including maps with unsigned 64-bit masks.
- Material and texture identities follow their compiled virtual paths and conversion roles through reimport.
- The material reader extracts authored metalness channels, keeps R8 red-only, and names unsupported anisotropic gloss. Embedded images and aggregate resource and geometry allocations are validated before staging.

**Checked:** 436 focused Windows checks, real CS2 SAS and SWAT characters, two Alyx add-on humanoids, the Alyx ApertureVR cutout prop, a CS2 Workshop prop with 64-bit mesh masks, and two genuine VTEX resources with embedded images. Task 0586 records measured bind drift, texture checks and reimport identity coverage. Pass 0588 tracks joined editor verification.

<!-- README -->
replace: MikuMikuDance characters; **Live Link** to Blender, Unity, Godot and Unreal.
with: MikuMikuDance characters, and compiled Source 2 characters and materials from mounted Half-Life: Alyx and Counter-Strike 2 content; **Live Link** to Blender, Unity, Godot and Unreal.

<!-- docs/USABILITY.md -->
after: | Rigged characters imported as statues
| Compiled Half-Life: Alyx and Counter-Strike 2 characters and textures needed conversion before import. | Import from mounted Source content to get the character's bones and skin, editable materials and project textures. |

<!-- docs/ARCHITECTURE.md -->
after: | `scene/import.*` |
| `scene/source2_resource.*`, `scene/source2_model.*`, `scene/source2_material.*` | Compiled Source 2 directories, binary KV3, render geometry, skeletons, materials and textures through SourceContent | ValveResourceFormat, SourceIO and the Khronos BPTC specification | ADR 0154, task 0586 |

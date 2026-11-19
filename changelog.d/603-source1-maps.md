## 2026-11-19 (round 603): Import classic Source maps

**Asked:** Bring a compiled classic Source map into Blendity with its terrain, materials and placed props.

**Added (task 0585, ADR 0154)**
- Import world surfaces, displaced terrain and brush models from classic VBSP. Brush entities and static props keep their placement. Repeated static props share their model mesh.
- Keep compiled brush surfaces with retraced edges and use the BSP's indexed triangles where provided.
- Brush entities retain their placements with mixed-case property names or shortened origin and angle vectors.
- Resolve materials and props against the BSP pakfile and the mounted content roots. A missing prop keeps its placement as a magenta marker and reports its path.

**Checked:** The Windows map tests passed 1,703 checks. Offline reopen retained all 47 placements from the SFM Warehouse item and all 40 displacement grids from Valve's Lockdown map. A generated map retained the expected packed texture over a conflicting mounted file. Task 0585 records the fixtures and commands.

<!-- README -->
after: The full list, stage by stage,
Classic Source BSP import brings in editable world geometry, terrain, brush models and static props.

<!-- docs/USABILITY.md -->
after: | A .blend or an Unreal level couldn't be brought in without exporting by hand.
| Classic Source maps had to be converted before use in Blendity. (round 603) | Import a classic BSP with its world, displaced terrain, brush models and placed static props. Mount the game or Workshop content to resolve the map's external assets. |

<!-- docs/ARCHITECTURE.md -->
after: | `find_texture`, `save_embedded`, `derive_channel`, `complete_materials`
| `import_source1_map` (`scene/source1_map.cpp`) | Builds native meshes and instances from classic BSP faces, displacements, brush models and static prop placements. Layers the map's pakfile over the selected mounts for material and model resolution. | Valve Source SDK (task 0585, ADR 0154) |

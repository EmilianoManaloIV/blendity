## 2026-11-19 (round 605): Import compiled Source 2 maps

**Asked:** Bring compiled Alyx and CS2 world geometry and its placed props into Blendity.

**Added (task 0587, ADR 0154)**
- Follow the map's world and world-node references to load standalone meshes, placed props and aggregate draws. Keep repeated meshes shared. Convert the placements and geometry to metres in Blendity's axes.
- Apply whole-model material overrides to the affected placements. Import notes name unsupported material skins and per-draw overrides and the resources that use them.
- Keep placement tints in reusable materials and snapshot aggregate LOD 0. Report the compiled paths of omitted baked lightmaps and the path and version of an unsupported map resource.
- Show a magenta placeholder at a missing prop's placement and report its compiled resource path.

**Checked:** Eight map cases passed 550 checks on a fresh Windows release build. They cover material assignments and pixels, tint, repeated rotated props, aggregate LOD and draw selection, missing and unsupported dependencies, and stable source IDs across cache moves, placement reordering and changed pixels. With meshoptimizer enabled, the real Alyx map *Bad Kitty* imported all 84 placements and 111,182 faces. The installed CS2 graphics settings map imported all 70 placements and 147,105 faces, with 206 texture paths from the mounted game archives.

<!-- README -->
after: MikuMikuDance characters;
Import compiled Source 2 world geometry and placed props from mounted content.

<!-- docs/ARCHITECTURE.md -->
after: | `find_texture`, `save_embedded`, `derive_channel`, `complete_materials`
| `import_source2_map` (`scene/source2_map.cpp`) (task 0587, ADR 0154) | Walks compiled worlds and world nodes, places shared mesh and model draw resources, and emits visible placeholders with missing virtual paths | [ValveResourceFormat world nodes](https://github.com/ValveResourceFormat/ValveResourceFormat/blob/8d9f7d9da62a1a0189c6d00050e9653c762656de/Renderer/Renderer/World/WorldNodeLoader.cs) |

<!-- docs/USABILITY.md -->
after: | An imported FBX came in grey:
| A compiled Source 2 map depends on props that may be in another local content root. (round 605) | Import follows the mounted references. A missing prop gets a magenta placeholder and its compiled path appears in the import notes. |

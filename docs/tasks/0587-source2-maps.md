# 0587 Alyx and CS2 worlds

- **Status:** independently approved and verified in the joined Windows and Linux Workshop suites. [Cross-title results](0588-workshop-verification.md).
- **Requirements:** R-6, [pass 0582](0582-workshop-import.md)
- **Decisions:** [ADR 0154](../decisions/0154-steam-workshop-import.md)
- **Implementation:** dedicated workstream, then independent testing and review
- **PR:** pending

## Goal and ownership
Follow compiled map resources through world nodes, world geometry and referenced static props using the shared resource reader.

Owned paths or surfaces: src/scene/source2_map*, dedicated world tests. Coordinate registration and shared interface changes through the mounting round.

## Acceptance and verification
For each title verify world geometry, scale and rotated repeated props from a packed map. Verify a missing optional prop has a visible placeholder and named warning. Derive independent tests from these criteria. Use scratch projects, run the Windows and Linux suites and relevant import/editor stress checks, and record exact commands and results. Cite repository revisions, primary format references and fixture provenance for consequential claims.

## Implementation evidence

- Map imports propagate canonical material and decoded texture identities from the shared Source 2 readers. Tint variants use the base material's source identity and the exact tint bits. The generated missing-renderable material has one semantic identity. Joined reimport verification is pending.
- The implementation starts from Blendity `f83be355`. ValveResourceFormat revision `8d9f7d9da62a1a0189c6d00050e9653c762656de` provides the direct interoperability references for [map and manifest resolution](https://github.com/ValveResourceFormat/ValveResourceFormat/blob/8d9f7d9da62a1a0189c6d00050e9653c762656de/ValveResourceFormat/IO/Extract/MapExtract.cs), [world-node prefixes](https://github.com/ValveResourceFormat/ValveResourceFormat/blob/8d9f7d9da62a1a0189c6d00050e9653c762656de/ValveResourceFormat/Resource/ResourceTypes/World.cs), [scene objects](https://github.com/ValveResourceFormat/ValveResourceFormat/blob/8d9f7d9da62a1a0189c6d00050e9653c762656de/Renderer/Renderer/World/WorldNodeLoader.cs), [aggregate draw calls](https://github.com/ValveResourceFormat/ValveResourceFormat/blob/8d9f7d9da62a1a0189c6d00050e9653c762656de/Renderer/Renderer/SceneAggregate.cs), and [placement matrices](https://github.com/ValveResourceFormat/ValveResourceFormat/blob/8d9f7d9da62a1a0189c6d00050e9653c762656de/ValveResourceFormat/Serialization/KeyValues/KVObjectExtensions.cs).
- `tests/test_source2_map.cpp` generates original VPKs in the test scratch directory. The Alyx-style fixture exercises individual scene objects. The CS2-style fixture exercises instanced aggregate draw calls. Both check position and scale in metres, the rotated prop's axes, mesh sharing and a positioned placeholder with its missing resource path. Their geometry, layouts and test files are original.
- The map reader imports scene renderables and aggregate fragments. For a scene model, the first whole-model material override replaces the material slots on that placement and keeps its mesh shared. Import notes name unsupported material skins and per-draw overrides along with the affected resource. Per-draw and standalone mesh overrides, material skins, clutter, baked lighting and runtime entities require further work. [ValveResourceFormat world-node overrides](https://github.com/ValveResourceFormat/ValveResourceFormat/blob/8d9f7d9da62a1a0189c6d00050e9653c762656de/ValveResourceFormat/Resource/ResourceTypes/WorldNode.cs#L174-L237)
- Scene-object and aggregate tints produce reusable material variants on shared meshes. The importer converts placement RGB from sRGB before multiplying it into the linear base color and applies the tint alpha separately. The map snapshots aggregate LOD 0 and keeps fragments outside an LOD setup. Import notes list the compiled paths for omitted baked lightmaps. The reader accepts resource version 1 for compiled maps, worlds and world nodes and reports the path and version of an unsupported resource. [ValveResourceFormat scene-object tint](https://github.com/ValveResourceFormat/ValveResourceFormat/blob/8d9f7d9da62a1a0189c6d00050e9653c762656de/Renderer/Renderer/World/WorldNodeLoader.cs), [tint conversion in the surface shader](https://github.com/ValveResourceFormat/ValveResourceFormat/blob/8d9f7d9da62a1a0189c6d00050e9653c762656de/Renderer/Shaders/common/surface_base_vs.slang#L122-L130), [aggregate tint and LOD masks](https://github.com/ValveResourceFormat/ValveResourceFormat/blob/8d9f7d9da62a1a0189c6d00050e9653c762656de/Renderer/Renderer/SceneAggregate.cs), and [baked lightmap references](https://github.com/ValveResourceFormat/ValveResourceFormat/blob/8d9f7d9da62a1a0189c6d00050e9653c762656de/Renderer/Renderer/World/WorldLoader.cs).
## Verification and review

The independent tester exercised both placement layouts, source-to-native units and axes, geometry sharing, an omitted prop, UVs, material slots, texture pixels and cutout alpha. Additional cases cover nonzero aggregate draw indices, tint conversion, whole-model overrides, aggregate LOD, missing lightmaps and unknown resource versions. The identity regression reverses placements, changes texture pixels and moves the cache, then checks the corresponding source IDs for original materials, overrides, textures and tint variants.

| Check | Result and evidence |
|---|---|
| Fresh Windows release, MSVC 14.51.36231 | Eight cases, 550 checks, zero failures. Built with `C:\blab\build-low.bat C:\blab\0587-map-tests\identity release tests map605identity`. Ran `C:\blab\0587-map-tests\identity\run.bat` with isolated project, trash, preferences and cache. Log in `C:/blab/0587-map-tests/identity/windows-tests.log`. |
| Earlier Linux development check | Three cases, 166 checks, zero failures. Built with `bash /c/blab/0587-coordination/lock_linux.sh /mnt/c/blwt605/src/core/jobs.cpp /mnt/c/blwt605/src/core/cpu.cpp` and ran `../gcc/tests` from the isolated `run` directory. Log in `C:/blab/0587-coordination/linux-unit.log`. Hosted Linux verification of the final revision is pending. |
| Independent code review | Approved the map source blob `05b64c0ce3362550e222f46359dcc952062f141c` and test blob `1fbebb3fdd41efc34fa59b1c0cd53e50e6c13a0f`. Earlier findings on tint color space, LOD, baked texture diagnostics and unsupported versions are resolved. |

### Real map probes

Each Windows probe used the identity build's `run.bat --probe-meshopt <archive> <virtual entry> [additional archives...]` with a fresh-ABI executable. Source 2 Viewer CLI 21.1 supplied the independent placement and draw counts. The source archives remain in scratch.

| Source | Expected and observed result |
|---|---|
| Half-Life: Alyx, author-published [Bad Kitty compiled map](https://github.com/thenayr/bad-kitty-half-life-alyx/blob/b58c1570570a16a7469153ab09da1b972359471d/game/hlvr/maps/bad-kitty.vpk), SHA-256 `1ee8268a1952b78c9262464c0be59d3cb19edc2ebf65f5e39112428c72d471ac` | `maps/bad-kitty.vmap_c` imported all 84 placements, 87 meshes and 111,182 faces with zero placeholders. Base-game materials were unavailable, and the import named the missing paths. Log in `C:/blab/0587-map-tests/identity/windows-real-alyx.log`. An earlier meshoptimizer-enabled Linux probe produced the same geometry. Without meshoptimizer the Windows probe loaded four placements and produced 80 named placeholders. |
| CS2, Steam app 730 build 26445667, `graphics_settings.vpk`, SHA-256 `748dd8a80618f980f7c69e211346dd0c9f9d4c90fa72a4a8816bc99dd8f1a036` | `maps/graphics_settings.vmap_c` contains 19 scene objects and 51 aggregate fragments. All 70 imported, yielding 147,105 faces with zero placeholders. Mounting `game/csgo/pak01_dir.vpk` and `game/core/pak01_dir.vpk` after the map produced 95 materials and 206 texture paths. Diagnostics include omitted baked lightmaps and unsupported shader features. Log in `C:/blab/0587-map-tests/identity/windows-real-cs2-mounted.log`. |
| CS2, [upstream nametag fixture](https://github.com/ValveResourceFormat/ValveResourceFormat/blob/8d9f7d9da62a1a0189c6d00050e9653c762656de/Tests/Files/small_map_with_material.vpk), SHA-256 `d8550c1761e4baf12e30b00b6ab62c86ab3ba932b0ef4a4da40ef47c663a9eea` | `maps/ui/nametag.vmap_c` imported the expected four vertices, two faces, one material and three texture files with zero warnings. Log in `C:/blab/0587-map-tests/identity/windows-real-nametag.log`. Test assets are excluded from the [upstream code license](https://github.com/ValveResourceFormat/ValveResourceFormat/blob/8d9f7d9da62a1a0189c6d00050e9653c762656de/README.md#license). |

The Hub owns the joined platform suites, editor stress, cross-title save/reopen verification and publication.

## Documentation
Own `changelog.d/605-source2-maps.md`. Include verified outcomes and applicable shared-document sections under ADR 0123. Supply user-facing changes to the Learn lesson and guide owners. Mark this spec done with test, review and PR evidence at closeout.

# 0582 Steam Workshop asset import pass

**Status** Approved pass specification. [ADR 0154](../decisions/0154-steam-workshop-import.md) records the architecture for owner review before implementation.

## Round specifications

- [0583 Content mounting, import UI and Learn lesson](0583-workshop-content.md)
- [0584 Classic characters and materials](0584-source1-characters.md)
- [0585 Classic maps](0585-source1-maps.md)
- [0586 Source 2 characters and materials](0586-source2-characters.md)
- [0587 Source 2 maps](0587-source2-maps.md)
- [0588 Fixtures, guides and end-to-end verification](0588-workshop-verification.md)

Each round owns its changelog fragment and records verified results before closeout. The hub folds shared documentation after merge under ADR 0123. The guide owner verifies the compiled manuals and records compatibility separately for each title.

## User workflow

Subscribe to a model or map in Steam and let Steam finish downloading it. In Blendity, choose File > Import > Source Engine Assets and select the installed item folder, a loose model/map, or a VPK. List supported entries by relative path so the user can choose among multiple models or maps. Show an actionable explanation for an empty item or an unsupported resource version. Steam handles acquisition. Blendity reads local files.

Resolve companions in the selected item and search additional user-selected Workshop or game content roots for dependencies. Show missing virtual paths and let the user add a root and retry. Convert the selected content and resolved dependencies into project-owned assets. Save and reopen after moving the source folder away. Pose and render the imported character in its map.

## Acceptance and delivery

- For humanoids in each target title, exercise manual bone posing on the imported skin and Auto IK on Selected Skeleton through the editor. Move hand and foot targets on both sides and verify the intended weighted vertices follow. On a fresh unskinned copy of the imported humanoid mesh, use marker-based Humanoid Auto-Rig and verify binding and posing. Check undo and save/reopen, and confirm importing a generic prop adds no humanoid controls. Cite the fixture, naming convention and result per title. See the [Rigging window](../../src/editor/rigging_window.cpp), [marker tool](../../src/editor/auto_rig_tool.cpp) and [Auto IK decision and checks](0048-ik.md).

- Exercise classic SFM, Half-Life: Alyx and Counter-Strike 2 separately, with a skinned character and textured compiled map containing placed props for each. Verify bind pose, joint movement, scale, orientation, material assignment, and offline reopen. Preserve edits on explicit reimport.
- Test a multi-model Workshop folder, a map in a VPK, content split across two mounted roots, and missing companions. Use scratch projects and redistributable fixtures. Record real-item verification independently for each title, including item URL or ID, provenance, version and remaining gaps.
- Divide work into shared mounting/import UX, classic models/materials, classic maps, Source 2 models/materials, and Source 2 maps. The fixture round owns shared fixtures and cross-title acceptance. Each product round has its own sequential implementer, independent tester and reviewer. The hub owns integration and CI, and the owner merges.
- Record references in every role handoff. Cite the specific source that supports each format decision and distinguish upstream behavior from the Blendity test that verified the result.

## References

- Steam documents subscription, installation and local item access in its [Workshop implementation guide](https://partner.steamgames.com/doc/features/workshop/implementation#ItemInstallation).
- [SourceIO's format and game coverage](https://github.com/REDxEYE/SourceIO#supported-formats-v530) is the interoperability reference. Its coverage table alone does not establish Blendity support.
- Blendity's [character import decision](../decisions/0047-imported-characters.md) defines project-owned materials and character assets. The [import interface](../../src/scene/import.h) exposes nodes, skins, materials and import diagnostics.

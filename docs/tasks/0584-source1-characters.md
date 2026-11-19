# 0584 Classic SFM characters and materials

- **Status:** approved
- **Requirements:** R-6, [pass 0582](0582-workshop-import.md)
- **Decisions:** [ADR 0154](../decisions/0154-steam-workshop-import.md)
- **Implementation:** dedicated workstream, then independent testing and review
- **PR:** pending

## Goal and ownership
Read MDL, VVD and VTX companions, default skin and bodygroup, VMT materials and VTF textures into native meshes, skins and materials.

Owned paths or surfaces: src/scene/source1_model* and source1_material*, dedicated model tests. Coordinate registration and shared interface changes through the mounting round.

## Acceptance and verification
A posed joint moves the expected weighted vertices. An asymmetric mesh renders with correct winding, scale, UVs and alpha. A missing geometry companion reports its virtual path. Derive independent tests from these criteria. Use scratch projects, run the Windows and Linux suites and relevant import/editor stress checks, and record exact commands and results. Cite repository revisions, primary format references and fixture provenance for consequential claims.

## Documentation
Own `changelog.d/602-source1-characters.md`. Include verified outcomes and applicable shared-document sections under ADR 0123. Supply user-facing changes to the Learn lesson and guide owners. Mark this spec done with test, review and PR evidence at closeout.

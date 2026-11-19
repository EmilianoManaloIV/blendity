# 0586 Alyx and CS2 characters and materials

- **Status:** approved
- **Requirements:** R-6, [pass 0582](0582-workshop-import.md)
- **Decisions:** [ADR 0154](../decisions/0154-steam-workshop-import.md)
- **Implementation:** dedicated workstream, then independent testing and review
- **PR:** pending

## Goal and ownership
Read compiled model, mesh, skeleton, material and texture resources for both target titles through the shared content resolver.

Owned paths or surfaces: src/scene/source2_model*, source2_resource* and source2_material*, dedicated resource/model tests. Coordinate registration and shared interface changes through the mounting round.

## Acceptance and verification
For each title separately verify bind pose, a weighted joint, a cutout material and detached-source reopen. Report unsupported resource versions. Derive independent tests from these criteria. Use scratch projects, run the Windows and Linux suites and relevant import/editor stress checks, and record exact commands and results. Cite repository revisions, primary format references and fixture provenance for consequential claims.

## Documentation
Own `changelog.d/604-source2-characters.md`. Include verified outcomes and applicable shared-document sections under ADR 0123. Supply user-facing changes to the Learn lesson and guide owners. Mark this spec done with test, review and PR evidence at closeout.

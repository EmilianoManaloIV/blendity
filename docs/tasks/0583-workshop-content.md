# 0583 Workshop content selection and mounting

- **Status:** approved
- **Requirements:** R-6, [pass 0582](0582-workshop-import.md)
- **Decisions:** [ADR 0154](../decisions/0154-steam-workshop-import.md)
- **Implementation:** planning task for the shared interface, then independent testing and review
- **PR:** pending

## Goal and ownership
Select an installed item folder, loose entry or multipart VPK. Resolve map-embedded content first, followed by ordered user mounts. Return the chosen virtual path and its provider.

Owned paths or surfaces: src/scene/source_content.*, editor import picker and import dispatch, shared build registration, src/editor/lessons.cpp. Coordinate registration and shared interface changes through the mounting round.

## Acceptance and verification
A two-model folder lists both paths. Duplicate paths resolve by mount order. Adding a second root satisfies a named missing dependency. Cancel leaves the scene intact. Derive independent tests from these criteria. Use scratch projects, run the Windows and Linux suites and relevant import/editor stress checks, and record exact commands and results. Cite repository revisions, primary format references and fixture provenance for consequential claims.

## Documentation
Own `changelog.d/601-workshop-content.md`. Include verified outcomes and applicable shared-document sections under ADR 0123. Supply user-facing changes to the Learn lesson and guide owners. Mark this spec done with test, review and PR evidence at closeout.

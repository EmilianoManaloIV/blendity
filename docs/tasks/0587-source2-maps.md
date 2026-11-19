# 0587 Alyx and CS2 worlds

- **Status:** approved
- **Requirements:** R-6, [pass 0582](0582-workshop-import.md)
- **Decisions:** [ADR 0154](../decisions/0154-steam-workshop-import.md)
- **Implementation:** dedicated workstream, then independent testing and review
- **PR:** pending

## Goal and ownership
Follow compiled map resources through world nodes, world geometry and referenced static props using the shared resource reader.

Owned paths or surfaces: src/scene/source2_map*, dedicated world tests. Coordinate registration and shared interface changes through the mounting round.

## Acceptance and verification
For each title verify world geometry, scale and rotated repeated props from a packed map. Verify a missing optional prop has a visible placeholder and named warning. Derive independent tests from these criteria. Use scratch projects, run the Windows and Linux suites and relevant import/editor stress checks, and record exact commands and results. Cite repository revisions, primary format references and fixture provenance for consequential claims.

## Documentation
Own `changelog.d/605-source2-maps.md`. Include verified outcomes and applicable shared-document sections under ADR 0123. Supply user-facing changes to the Learn lesson and guide owners. Mark this spec done with test, review and PR evidence at closeout.

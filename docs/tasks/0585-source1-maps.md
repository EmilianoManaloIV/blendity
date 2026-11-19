# 0585 Classic SFM maps

- **Status:** approved
- **Requirements:** R-6, [pass 0582](0582-workshop-import.md)
- **Decisions:** [ADR 0154](../decisions/0154-steam-workshop-import.md)
- **Implementation:** dedicated workstream, then independent testing and review
- **PR:** pending

## Goal and ownership
Import BSP world geometry, displacements, brush models and static props. Resolve embedded materials and reuse model data for repeated props.

Owned paths or surfaces: src/scene/source1_map*, dedicated BSP tests. Coordinate registration and shared interface changes through the mounting round.

## Acceptance and verification
A map with a displacement and two differently transformed instances retains geometry, material assignments and both transforms after reopening. Derive independent tests from these criteria. Use scratch projects, run the Windows and Linux suites and relevant import/editor stress checks, and record exact commands and results. Cite repository revisions, primary format references and fixture provenance for consequential claims.

## Documentation
Own `changelog.d/603-source1-maps.md`. Include verified outcomes and applicable shared-document sections under ADR 0123. Supply user-facing changes to the Learn lesson and guide owners. Mark this spec done with test, review and PR evidence at closeout.

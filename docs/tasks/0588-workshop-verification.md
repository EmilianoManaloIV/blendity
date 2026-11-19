# 0588 Workshop fixtures, guides and end-to-end verification

- **Status:** approved
- **Requirements:** R-6, [pass 0582](0582-workshop-import.md)
- **Decisions:** [ADR 0154](../decisions/0154-steam-workshop-import.md)
- **Implementation:** dedicated workstream, then independent testing and review
- **PR:** pending

## Goal and ownership
Own redistributable cross-title fixtures, provenance, compatibility matrix, user guide and compiled book verification.

Owned paths or surfaces: tests/fixtures/source_workshop/, dedicated integration tests, docs/book/ user and architecture chapters, Workshop compatibility guide. Coordinate registration and shared interface changes through the mounting round.

## Acceptance and verification
For each title select a downloaded-item layout, import character and map, pose, render, save, move the source away, reopen and render again. Record real-item evidence or mark pending. Rebuild affected books and inspect the changed pages. Derive independent tests from these criteria. Use scratch projects, run the Windows and Linux suites and relevant import/editor stress checks, and record exact commands and results. Cite repository revisions, primary format references and fixture provenance for consequential claims.

## Documentation
Own `changelog.d/606-workshop-verification.md`. Include verified outcomes and applicable shared-document sections under ADR 0123. Supply user-facing changes to the Learn lesson and guide owners. Mark this spec done with test, review and PR evidence at closeout.

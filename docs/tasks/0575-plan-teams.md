# 0575 — Dispatch independent work directly to the hub (round 593)

- **Status** — in review under the owner's approved direction for this and future plans
- **Deliverable** — reusable dispatch guidance, an AGENTS.md reference and the round fragment
- **Owned files** — this spec, AGENTS.md, docs/handoff/METHOD.md and changelog.d/593-plan-teams.md

## Acceptance criteria

1. Every approved plan maps deliverables to file owners, dependencies, acceptance checks and shared resources. Independent task chats report directly to the parent hub by default. Each workstream lead has an explicit coordination need.
2. Verify the dispatch choice against two unrelated example plans. Preserve one owner per file, runtime capacity limits and sequential, independent implementation, testing and review.
3. The parent hub retains the review gate, single stack top, commits and publication, machine scheduling and ongoing worker and PR health. The owner retains plan approval and merges.
4. AGENTS.md links to the procedure and the round fragment describes the change. Check the links, fragment and diff. This round changes documentation only.

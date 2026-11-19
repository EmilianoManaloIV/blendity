# 0589 Parallel verification and evidence collection

- **Status:** independently tested and reviewed; the Hub publishes the verified candidate
- **Decision:** extend the compact handoffs of task 0581
- **Files:** project handoff method, Codex handoff, evidence helper and round 607 fragment

## Acceptance

1. A producer records a dependency change once and informs its consumer. The Hub receives actionable changes and review-ready results.
2. A first connected reader exercises a small end-to-end test. Full joined verification uses a named revision, compatible build outputs and separate process evidence where persistence matters.
3. Each role gets a fresh, task-specific brief with source and evidence pointers. Testing and review stay independent.
4. A script summarizes native logs and CI status with source identity, counts, evidence hashes or CI links. Failed, missing and mismatched evidence stays distinguishable from successful tests. Validate locally without modifying active workflows.

## Verification

Six native log fixtures covered success, an unexpected count, failed assertions, an interrupted
process, zero checks and multiple summary lines. The hosted collector accepted the matching SHA
for [ARM64 run 44583737244](https://github.com/EmilianoManaloIV/blendity/actions/runs/44583737244)
and rejected a different SHA for that same run. The fragment check and Git whitespace check passed.

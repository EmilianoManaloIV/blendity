## 2026-11-20 (round 625): Preserve integration PR evidence on refresh

**Asked:** Bring integration pull requests into line with the project PR template and preserve the owner's notes and reviewers' evidence.

**Fixed (task 0607, ADR 0132)**
- New integration PRs use the five template sections. Refreshes update the carried PRs, stack heads and omission reasons in a marked summary while preserving surrounding content and attribution.
- Existing unmarked descriptions retain their manual content on migration. The script stops before pushing or editing if it cannot read the existing body or finds ambiguous summary markers. It re-reads the body immediately before editing and leaves any newer description intact if the comparison fails.

**Checked:** `bash tools/dev/stack_selftest.sh` passed 112 checks each on Windows (Git Bash) and Linux (WSL) against a local origin and fake `gh`. The task record is `docs/tasks/0607-integration-pr-template.md`.

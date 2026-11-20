## 2026-11-18 (round 588): Blendity's project workflow in Codex

**Asked:** Carry the project-owned Claude workflow into a clean Codex checkout.

**Changed (task 0570)**

- Linked the Codex roles, project Git rules, owner approval gates and cold-start instructions from the engineering handoff.
- Mapped ten project deny entries and recorded the responsibilities of the existing workflow and machine helpers.
- Aligned task, review and attribution guidance for future work and added lightweight configuration checks to CI.
- Added the owner-approved hub cleanup for verified merged task branches with checks for the exact remote and head.

<!-- README -->
replace: and [CLAUDE.md](CLAUDE.md) (engineering standards).
with: and [AGENTS.md](AGENTS.md) (engineering standards and handoff).
replace: Blendity is written with Anthropic's Claude through Claude Code, under a human's direction and review; the program
with: Anthropic's Claude Code helped build Blendity through 1.0.0. Development continues with OpenAI Codex under the owner's direction and review. The program

# 0006: Subtree publishing and human-merged pull requests

- **Status:** accepted (2026-10-09)
- **Requirements:** C-05, Q-03

## Context
The local git root is `blender-main/` (Blender's checkout beside `blendity/`); GitHub
(EmilianoManaloIV/blendity) holds only `blendity/`. Until now each round was committed locally and its
`blendity/` tree pushed straight to `main` and `blendity` through the `blendity-root` branch, so no review
or CI gate stood before `main`.

## Decision
- Work is published as a subtree commit (`git commit-tree "<commit>:blendity" -p blendity-root`) pushed to
  a branch `task/NNNN-slug`, and a pull request is opened against `main`.
- `main` is protected on GitHub: pull requests only, the Windows, Linux and macOS CI jobs must pass, one
  approval, no force pushes or deletions. The human merges.
- After a merge, `blendity-root` is moved to `origin/main` so the next subtree commit builds on it.
- Agents never push to `main` / `blendity`, force-push or merge (enforced in `.claude/settings.json`).

## Consequences
- Every change to `main` has passed CI and been looked at by the human.
- One more step per round (open the PR, merge on GitHub, sync `blendity-root`).
- The legacy `blendity` branch on GitHub stops tracking `main`; it can be retired or protected the same way.

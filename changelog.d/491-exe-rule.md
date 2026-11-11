## 2026-11-11 (round 491): The local exe is rebuilt when a PR is published

**Asked:** "Can you build each .exe after each implementation and tests pass? Not necessarily when the PR goes green?" -
then "Yes, lets do it that way from now on." and, for `CLAUDE.md`, "Yes, i want that change too."

**Changed**
- `CLAUDE.md` (Build and run): the user's local `dist\windows\Blendity.exe` is rebuilt when a round's PR is published
  (review done, the full local chain passed, the commit pushed) instead of when its CI is green; never before review or
  from uncommitted code; the user is told which rounds in the exe are not yet green.

**Checked:** documentation only - no code, no tests changed; `tools/dev/changelog.py check` passes.

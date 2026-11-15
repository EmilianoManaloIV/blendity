## 2026-11-15 (round 516): CLAUDE.md - nothing waits on a PR

**Asked:** "I thought PR's shouldn't be wait upon in development?" then "Make it a common rule that operations should
not wait on PRs." and, of how it reaches the repository, "a" (a small PR of its own).

**Changed**
- `CLAUDE.md`, workflow item 7: no round, fix, write-up or verification is held until a PR is merged or published;
  work builds on the open PR's head and publishes stacked on it, and work that needs several open stacks builds on a
  local merge of their tops and publishes as the single top of the joined stack. Only the user's merges wait on the
  user.

**Checked:** documentation only - no code, test or build file changed.

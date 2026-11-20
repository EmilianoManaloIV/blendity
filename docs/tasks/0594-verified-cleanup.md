# 0594. Retire completed worktrees and task branches

**Status:** Assigned for round 612. The owner's workspace cleanup request authorizes ordinary removal of verified stale worktrees and merged branches. The Hub reviews the exact candidate manifest before removal.

## Procedure and acceptance

1. Enumerate registered worktrees, local heads and remote heads against the current published main revision. Record each candidate path, exact head, ownership, reason and merge proof in scratch. Verify open PRs and active build, editor and dashboard locations before removal.
2. Preserve the owner's checkout, parked tasks 0562 and 0563, protected folders, current rounds, active tools, retained evidence, changes and commits without verified ancestry. Recheck the worktree status including ignored entries immediately before removal.
3. Remove a clean candidate only through ordinary Git worktree removal. Remove a local round branch only after its exact tip remains reachable from main and all checkouts have released it. Use the project's checked merged-PR helper for each eligible remote task branch.
4. Report actual before/after counts, occupied file bytes reclaimed and retained exceptions. Verify the remaining worktrees and refs. This task changes development-machine state and has no editor runtime benchmark.

References: [machine handoff](../handoff/MACHINE.md), [project Git rules](../../AGENTS.md), [merged branch verification](../../tools/dev/cleanup_task_branch.py).

## Outcome

The initial manifest covered 153 worktrees, 162 local heads and 170 remote heads at `bf7e9c3f`. The Hub released seven clean checkouts and 24 checkouts whose extra files were verified compiler, linker or packaging outputs. The scan checked the disposable paths, protected directory contents against Git objects, filesystem links, actual tracked and ignored status, process references, and measured allocated file bytes before ordinary Git worktree removal.

The 31 retired checkouts released 61,712,774,408 allocated file bytes (57.47 GiB). The Hub retired 59 local branches and 97 remote task branches. The closing inventory found 123 worktrees, 104 local heads and 74 remote heads; another round had added one active diagnostic checkout and branch locally and on GitHub.

The remote helper declined 47 candidate branches. In 39 cases GitHub's recorded PR merge lacked the required two parents, and in eight cases the second-parent ancestry check failed. Ordinary local branch removal also declined two refs with upstream relationships. Those refs, parked tasks 0562/0563, and checkouts with unique data, historical test reports, links, or ongoing consumers were retained.

The closing audit compared both inventories against every receipt and rechecked the merge ancestry of all 97 retired remote heads. It found every reported path and ref absent and no unexplained disappearance. The scratch evidence is in `C:/blab/cleanup612/`: `manifest.json`, `rebuildable.json`, the three receipt files, and `after.json`. The recovered allocation excludes shared files and Git object storage.

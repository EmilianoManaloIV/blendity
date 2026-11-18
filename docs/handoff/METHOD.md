# The method that worked

How Blendity's changes were made, in the order a change meets each step. The rules themselves are in
`AGENTS.md`; this file says how they were applied and why each one exists. Where a rule came from a loss, the
loss is in `PITFALLS.md`.

## Rounds and passes
- A **round** is one request-sized change: one spec (`docs/tasks/NNNN-slug.md`), one commit, one pull request,
  one changelog fragment. Rounds are numbered in the order they start (1 to 574 so far); specs have their own
  numbers. A PR's title names both ("... (round 565, task 0547)").
- A **pass** is a group of rounds the owner approved as one plan: a goal, the rounds with their specs, a
  file-ownership table and the budgets to meet. It ends with a **write-up round** that measures the result
  against the plan, folds the fragments and lists follow-ups. Approving the plan approves its specs; a choice
  the plan did not foresee goes back to the owner.
- Each round's times were logged (started, checks started, checks done, published). A typical round took 1.3 to
  1.6 hours; the log is how the workflow changes below were judged.

## One round: spec, implement, test, review, PR
1. **Spec.** From `docs/tasks/TEMPLATE.md`: what was asked, what is in and out, numbered acceptance criteria
   each of which a test can check, the files to touch, the stress sections to run, a responsiveness criterion
   where the feature does work per frame. An ADR beside it when the choice is architectural.
2. **Implement** (the implementer role). Only the spec's files. Build both ways (Blender's libraries and
   dependency-free), then the round's own test group in **both** builds before anything long runs.
3. **Test** (the test-engineer role). Tests decided from the acceptance criteria before reading the diff; UI
   behaviour through real events; edge cases the spec implies. A bug found is left as a failing test and
   reported, not fixed by the tester.
4. **Review** (the code-reviewer role), **before** the long verification, not after it: a review that finds a
   real fault after a 50-minute chain costs a second chain. Read-only; findings ranked; approve only with no
   blockers or majors. Fixes go in, and a second review looks at the fixes.
5. **Verify.** The round's test groups and `editor`, on Windows (both builds) and Linux
   (`TEST_GROUPS="a,b" bash tools/dev/chain.sh`, or the same steps by hand from a worktree); the stress sections
   for geometry changes; the audits (`tools/test_coverage_map.py --check`, `tools/sweep_commands.py --check`,
   `py tools/dev/changelog.py check`). The whole suite, macOS and the sanitizers are CI's.
6. **Publish.** Merge the stack's current top into the round's branch (build the tests again if the merge
   touched test files), commit, push `task/NNNN-slug`, open the PR against `main` with the template, run
   `bash tools/dev/stack_check.sh`, read the checks, tell the coordinating task the PR number and head.
7. **After.** Read CI once it has run; a red is fixed on the top of the stack, never on a lower PR. Read review
   comments. The owner merges.

Work is pipelined: while one round's checks run, the next round's spec and patches are written. Nothing waits
on a merge.

## Changelog fragments, folded at a pass's end
A round never edits `CHANGELOG.md`, `README.md` or the four shared docs (`docs/USABILITY.md`, `tools/README.md`,
`docs/ARCHITECTURE.md`, `docs/PERFORMANCE.md`). It adds `changelog.d/<round>-<slug>.md`: the entry as it will
read in the changelog, plus sections of `after:` / `replace:` / `append:` operations for the README and the
shared docs (`changelog.d/README.md` has the format). The pass's write-up runs
`py tools/dev/changelog.py fold --checks LIBS TESTS`, which applies every fragment or nothing, sets the README's
check count and deletes the fragments; that commit carries a `Docs-fold: yes` trailer, and the `docs-fragments`
CI job rejects any other commit that edits those files. One fold runs at a time. Before fragments (ADR 0123),
every pair of parallel PRs conflicted in the same six files.

## One stack, one top
- Every PR's base is `main`. A round's commit is built on the previous open round's, so the newest PR's branch
  contains all of them: merging that **top** PR with a merge commit merges every PR below it, and GitHub marks
  them merged.
- `bash tools/dev/stack_check.sh` lists each stack and fails when one has more than one top; `FIX=1` merges
  stray heads into the newest PR. It runs after every publish and every push, and as the `stack` CI job.
- With several tasks publishing, the rule is: whoever is ready publishes on the **current** single top
  (`stack_check.sh` names it), merging that head first. Two publishing at once make two tops for a few minutes;
  the later one merges the other in.
- The PR body's "Merging this also merges" line names what it carries. The owner then needs one merge per pass.
- A stack is never rebased or squashed, and a fix never lands on a lower PR (it would sit outside the stack).

## File ownership for parallel work
Parallel tasks conflict where they share files, so the plan decides the split before work starts:
- a read-only sweep maps each piece to the files it will touch (source, tests, docs);
- the plan carries the table (piece, round, spec, what, files owned) and a check against the work already open;
- file-disjoint pieces run in parallel, each in its own worktree; pieces that meet in a file go to one task in
  sequence; a task needing a file outside its row asks first, and the coordinator checks who owns it;
- known limits are stated, not hidden: a few list files (include blocks, the Learn lessons, the command table,
  the test group list) are touched by nearly every feature - those merge as "keep both" (see `PITFALLS.md`
  before doing one); predictions can be wrong; file-disjoint pieces can still break each other, which CI on the
  joined top catches.
Pass 28's table (eight tasks, rounds 562 to 570) is the model; Pass 27 ran ten.

## The machine's shared resources
One 32-core machine with 32 GB ran up to thirteen sessions. Four things are shared and each has a rule
(`MACHINE.md` has the commands, `tools/dev/machine/` the scripts):
- **Windows build slots.** Two compiles at a time, machine-wide, first come first served, one slot per owner,
  taken and dropped inside the same command as the build (`build-low.bat`, which also runs the build at
  below-normal priority so the owner's own use of the machine stays smooth). Tests need no slot.
- **The WSL lock.** One Linux compile at a time, on ten cores (`taskset -c 0-9`): WSL has 15 GB. No sanitizer
  builds locally at all; CI's sanitizer shards judge.
- **The graphics-card slot.** Card work (the `gpu` test group, bakes, card stress rows) is one at a time,
  granted by the coordinator in so many words ("CARD YOURS" / "CARD FREE") and one grant at a time - an open
  "take it when ready" produced a double grant.
- **Timed windows.** A measurement needs a quiet machine. Builds and untimed tests of different tasks may
  overlap freely; timed runs may not overlap anything. The procedure that finally held:
  1. each task puts its measured exe, DLLs and `shaders/` folder in its own folder and sends the coordinator one
     script to run;
  2. the coordinator runs `timed-window.sh <log> <scripts...>`, which **takes both Windows build slots and the
     WSL lock itself**, keeps them alive, waits for a minute of no compiler or test process, runs the scripts one
     after another at normal priority, and samples every five seconds for any build or test process ("TAINT"
     lines);
  3. a window with taint lines is discarded and rerun; a clean one is believed;
  4. tasks check `win-lock.sh show` before starting any test, stress or card run: an owner named `hub-window*`,
     holding or queued, means start nothing new.
  Holding the locks replaced asking tasks to hold: a message can sit unread in a task's queue while that task
  starts a build. Measurements are before/after pairs in the same window, and for card rows on the same exe with
  a switch where possible, with `nvidia-smi` sampled beside the run (the card's power state moves card timings
  four to nine times).

## The owner's exe, rebuilt on every publish
The owner tries finished work at once, so `dist\windows\Blendity.exe` in the main checkout always holds
`origin/main` plus the newest published branch of every open pass:
1. in a scratch worktree (`C:/blwtexe`), check out `origin/main` and merge each open top (`git merge --no-edit`);
   a conflict between passes is resolved locally and never pushed, or that pass is left out and the owner told;
2. build the app there with a build slot, with Blender's libraries (and build the tests too after a keep-both
   merge);
3. copy the exe and `dist/windows/shaders/` into the main checkout; if Blendity is running, rename the running
   exe aside first, never kill it; delete the old copies once nothing runs them;
4. smoke-test headless in a scratch project;
5. tell the owner in one line what the exe contains and which rounds in it are not green on CI yet.
It is rebuilt at publish, not at CI green (that reaches the owner one to three hours sooner) and not after a
merge (the exe already has that work). Never during a timed window.

## The hourly check and the board
- **Hourly**, while parallel tasks run, the coordinator looks for stalls (a worktree that has not changed, a
  build queued behind a dead slot holder), reads CI on the open PRs with `gh pr checks`, routes each red to the
  task that owns the file, and frees stale locks whose owners have nothing running.
- **The board** was a page listing every round of the open passes: round, spec, task, state (spec / implementing
  / testing / review / chain / published / green / merged), PR and times. Each task updated its own rows; the
  coordinator checked it. Under Codex any equivalent will do - a pinned issue, a project view, or a file outside
  the repository; the facts it shows can all be read from `gh pr list`, `stack_check.sh` and `docs/tasks/`.
- Alongside it the coordinator kept two plain logs: who holds the card and which timed window is queued, and
  what each exe rebuild contained. Short dated lines, appended, turned out to be all that was needed.
- The coordinator stays quiet: it speaks to the owner for a decision only they can make (a blocker, a real choice
  between options, a stack ready to review) and otherwise answers when asked.

## Long tasks
- A fresh task per pass, started after the previous pass's write-up; before stopping, a task writes a short
  resume note (what is published, what is in the worktree, what is next). Switching mid-batch costs more than
  finishing it.
- A task that reports to another puts the result first (the PR link, the head commit), then the detail.
- Helper scripts that prove useful go into `tools/dev/`, with their paths as settings at the top; scratch
  folders are never uploaded.

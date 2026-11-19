# Blendity - engineering standards for agents

This file is for any coding agent working on Blendity (ChatGPT Codex reads it by this name). It restates
`CLAUDE.md`, the file the project was built under, without the parts that only one tool understands; where the
two differ on a rule, tell the owner and fix the file that is wrong. The handoff that goes with it is in
`docs/handoff/` (start at `docs/handoff/README.md`).

Blendity is a C++17 editor that rebuilds Blender's modeling, UV, material and rendering ideas behind a
Unity-style editor (Hierarchy, Inspector, Scene view, Play mode), with SketchUp / UModeler /
Plasticity-style drawing and Push/Pull. Read `docs/requirements.md` (what it must do),
`docs/ARCHITECTURE.md` (how it is built) and `docs/decisions/` (why) before changing anything
non-trivial.

Words used below: **the owner** is the human who owns the repository, approves plans and merges. A **round**
is one change with one spec, one commit and one pull request. A **pass** is a group of rounds planned and
approved together, ending in a write-up round.

## Project map
- `src/core` math (Unity conventions), logging, filesystem, jobs, CPU features
- `src/platform` window + input (Win32 / X11 / Cocoa)
- `src/image`, `src/render` codecs, canvas, rasterizer, path tracer, GPU devices
- `src/scene` Mesh (Blender layout), `meshops` operators (`mesh_tools*.cpp`), scene, IO, modifiers
- `src/editor` IMGUI toolkit, docking, Scene view, tools (`draw_tool.cpp`, `scene_view.cpp`), panels, keymaps
- `tests/test_<group>.cpp` unit checks (one file a subsystem, `test_common.h` shared; `test_main.cpp` runs them);
  `stress/stress_main.cpp` stress / robustness sections
- `extern/` code copied from Blender's tree; `licenses/` notices; `docs/` design documentation
- `tools/` audits, benchmarks and the round workflow's scripts (`tools/dev/`, `tools/dev/README.md`)
- Not ours to edit without asking: `Assets/` (the owner's project), `Renders/`, `Screenshots/`, `research/`

## Build and run
- Windows (from Git Bash, absolute path): `cmd //c "C:\\...\\blendity\\build.bat release all|app|tests|stress"`.
  Never combine it with an exported `MSYS_NO_PATHCONV` in the same shell (the build then does nothing and exits 0).
- Linux / macOS: `./build.sh release|debug all|app|tests|stress` (on the development machine through WSL:
  `wsl -e bash -lc "cd /mnt/c/.../blendity && ./build.sh release all"`). `EXTRA_FLAGS` adds compiler
  and linker flags (sanitizers). Sanitizer builds need `BLENDITY_NO_LIBS=1` (UBSan's type checks can't
  link against the prebuilt Jolt, which has no RTTI) - as CI builds them.
- The owner often has `dist\windows\Blendity.exe` open (LNK1104 when linking): rename the running exe
  aside (`Blendity.old.exe`) and build into place. **Never kill the owner's Blendity** (it may hold unsaved work).
- Optional Blender libraries come from `../blender/lib/<platform>`; `BLENDITY_NO_LIBS=1` forces the
  dependency-free build that CI uses. Both must keep building.
- The owner's local `dist\windows\Blendity.exe` stays current (the owner, 2026-11-11): the coordinating task
  rebuilds it from `origin/main` plus every open pass's newest published branch when a round's PR is
  **published** (review done, full local chain passed, commit pushed) - it does not wait for CI. Never earlier:
  not before review, not from uncommitted code. Each time, one line to the owner on what the exe contains and
  which rounds in it are not yet green on CI; a fault CI finds later means a rebuild after the fix. Not while a
  timed stage runs.
- Headless runs: `Blendity.exe --headless-screenshot out.png --cmd "..."` with `BLENDITY_PROJECT` and
  `BLENDITY_TRASH` pointing at a scratch copy, never the real `Assets/`.
- The development machine's limits and helper scripts (build slots, the WSL lock, timed windows) are in
  `docs/handoff/MACHINE.md`; on that machine they are rules, not advice.

## Testing rules
- Every behaviour change or bug fix gets a unit test in the `tests/test_<group>.cpp` of its subsystem, named
  for what the user sees, in the current round's group at the end of the file. Prefer real events
  (`step_frame_headless` with mouse / key events) for UI behaviour, `meshops` calls for geometry. Run one with
  `BLENDITY_TEST_FILTER="part of name"`, a file's with `--group <name>` (`--list-groups`).
- A unit test pins a behaviour; a measurement is a stress row (ADR 0056): a test may print a time, it never
  asserts one, and a sweep keeps a sample of its cases in the suite with the whole sweep in stress. A spike's
  test goes when its ADR is written. `python tools/test_coverage_map.py --check` must find no console command,
  keymap action or window without a test.
- Before a change is done: the round's test groups and `editor` pass locally on Windows and Linux
  (`TEST_GROUPS="a,b" bash tools/dev/chain.sh`), and the full unit suite passes on CI (ADR 0134). The round's
  groups are those whose tests it changed and those that test the code it changed (code behind a `BL_WITH_*`
  switch included: CI builds without the libraries); a round that cannot name them runs the whole suite
  locally (the chain without `TEST_GROUPS`).
- A timed run (a measurement, a timed bake, a chain with `POSING=1`) takes a slot from the coordinating task
  first: one holder at a time, nothing else builds or runs meanwhile (`docs/handoff/METHOD.md`, "Timed windows").
- Geometry changes also run the matching stress sections (`blendity_stress --only pushpull |
  modifiers_ngon | curved | editor`). Problem counts and "faces newly on top of each other" counts must
  not rise without an explanation in the round's changelog entry. Delete the report files a run creates and
  restore `stress/results/latest.md` with `git checkout`.
- Tests and stress run in a scratch project and trash (`BLENDITY_PROJECT`, `BLENDITY_TRASH`); nothing a
  test does may touch the owner's files.
- A test that disagrees with the code is evidence, not a nuisance: don't loosen a check without saying why
  in the test.

## Code standards
- C++17, MSVC + GCC + Clang. GCC needs explicit types in generic lambdas that call `get<T>()`.
- Match the surrounding code: naming, comment density (comments say why, in plain words), idiom.
- Keep nesting shallow (the owner's review of PR #50): pull inner blocks out into named functions, turn
  conditions into early returns / `continue`; about three levels inside a function at most.
- Meshes are copy-on-write: re-read `MeshPtr` after an edit; `mesh_make_mutable` before writing.
- Edit Mode operators that take numbers go through `LastOp` (`edit_op_redoable` + `run_last_op`) so F9
  can re-run them from `before`; their numbers live in F9 / the drag helper, not in the Inspector.
- Every tool is reachable from the console (`editop`, `edittool`, `drawpoint`, ...) so tests and scripts
  can drive it.
- No new dependency unless Blender itself bundles it, and then only behind a `BL_WITH_*` switch with a
  dependency-free fallback (ADR 0001). Source is GPL-2.0-or-later with an SPDX header in every file; the
  binaries are GPL-3.0-or-later (ADR 0002).
- Keep geometry robust: degenerate faces, non-manifold edges, open surfaces, huge / tiny scales and NaN
  inputs must leave a structurally valid mesh or refuse cleanly.
- Keep the editor responsive: nothing long on the main thread (kernel builds, bakes, big evaluations go to the
  background with progress or a fallback); measure first-use costs with cold caches, not only warm runs.
- A user-facing feature adds or extends a lesson in the Learn tab (`src/editor/lessons.cpp`: Try-it actions
  and questions).

## Documentation duties (every change)
- `changelog.d/<round>-<slug>.md`: one entry per round (asked / fixed / changed / added / checked, stress numbers),
  plus a `<!-- README -->` section with the feature-paragraph line when user-facing behaviour changes, and a section
  for each shared doc the round adds to: `<!-- docs/USABILITY.md -->`, `<!-- tools/README.md -->`,
  `<!-- docs/ARCHITECTURE.md -->`, `<!-- docs/PERFORMANCE.md -->` (format: `changelog.d/README.md`). **A round never
  edits `CHANGELOG.md`, `README.md` (nor its unit-check count) or those four shared docs** (ADR 0123: parallel
  passes' PRs conflicted there). The pass write-up runs `py tools/dev/changelog.py fold --checks LIBS TESTS` once the
  pass's PRs are on `main` and commits it with a `Docs-fold: yes` trailer; the coordinating task runs one fold at a
  time. The `docs-fragments` CI job fails any other commit that edits those files.
- `docs/ARCHITECTURE.md`: a row for new subsystems or functions others build on - in the fragment's section.
- `docs/USABILITY.md`: a "fixed" row for user-facing problems solved - in the fragment's section.
- `docs/decisions/`: an ADR for any architectural choice (new subsystem, data-model change, new
  dependency, change to a documented convention).
- `docs/tasks/`: the task spec the work was done against; mark it done.

## Workflow and approval gates
1. **Plan** (the planning task, on the strongest model available): write `docs/tasks/NNNN-slug.md` (template in `docs/tasks/`) and an ADR if the decision is
   architectural. **The owner approves the spec (and ADR) before code is written.** When the owner has approved
   a pass's plan, the specs of that plan count as approved (the owner's standing instruction, 2026-11-10; not in
   `CLAUDE.md`); anything that leaves the plan goes back to the owner.
2. **Implement**: the implementer role for routine, well-specified work; the planning task itself for hard
   geometry or cross-cutting changes.
3. **Test**: the test-engineer role writes tests from the spec's acceptance criteria.
4. **Review**: the code-reviewer role reviews the diff against the spec, the ADRs and this file.
5. **Merge**: push a branch, open a PR (template in `.github/`), CI must pass. **The owner reviews and
   merges on GitHub**, unless the owner asks an agent to merge: then only after CI passes on all four jobs,
   with a merge commit (`gh pr merge N --merge`), never squash or rebase of a stack. A go-ahead to merge is for
   that one time. Never use `--admin`, never turn on auto-merge.
6. **Every PR targets `main`** (`--base main`), never another PR's branch (the owner, 2026-11-08). A round's
   commit may build on the previous round's, but its PR's base is `main`. **Guarantee:** merging the top PR
   of a stack merges every PR below it (GitHub marks them merged; use a merge commit). Several stacks may be
   open at once, one per pass; each must have exactly one top. After every publish and every push to a PR
   branch run `bash tools/dev/stack_check.sh` (`FIX=1` merges a stack's stray heads into its newest PR);
   the `stack` CI job runs the same check. The body's "Merging this also merges" line names the PRs it carries.
   **The integration PR** (ADR 0132): when several stacks are open, `bash tools/dev/integrate.sh` merges the top of
   every stack whose four required jobs are green into `integration/<n>` (from `main`, merge commits only) and opens
   one PR on `main` that carries them all - the one the owner merges; `CHECK=1` only reports what it would merge and
   what conflicts. It is the one allowed top over several stacks; every stack it carries must be in it up to that
   stack's own top (`stack_check.sh` fails otherwise: run `integrate.sh` again after a push). Each PR still targets
   `main` and can still be merged on its own.
7. **Nothing waits on a PR** (the owner, 2026-11-15). No round, fix, write-up or verification is held until a PR is
   merged or published: build on the open PR's head and publish stacked on it; work that needs several open stacks
   builds on a local merge of their tops and publishes as the single top of the joined stack (say which stacks it
   joins in the body). Only the owner's merges wait on the owner.

Play the roles one after another; don't fan out. (Parallel tasks on file-disjoint rounds, below, are the one
exception the owner asked for, 2026-11-10 and 2026-11-14; within a round the roles never run side by side.) For a one-line fix, implement in the planning
task and still run the review.

### The three roles
Under Claude Code these were three sub-agents (`.claude/agents/implementer.md`, `test-engineer.md`,
`code-reviewer.md`: the full text is there). Under Codex one task plays them in turn, or a separate task plays
one; either way the role's rules hold while it is being played. The point of the split is independence: the
tests are decided from the spec before the diff is read, and the review is not written by the mind that is
defending the change. If one task plays all three, start each role by re-reading the spec, and say in the PR
that the review was not independent.

**Implementer** - implements exactly one approved spec.
- Input: `docs/tasks/NNNN-slug.md` with status approved. "If the status is not approved, stop and say so."
- Makes the change the spec asks for in the files it names, reuses existing functions, matches the style; adds
  tests for each acceptance criterion; builds on Windows, and on Linux "when the change touches platform code,
  headers, templates or anything GCC-sensitive"; runs the round's tests and the stress sections the spec names;
  writes the changelog fragment; sets the task's status to "in review".
- Boundaries: "No architectural changes the spec doesn't ask for: no new subsystems, data-model changes, new
  dependencies or changes to documented conventions. If the spec turns out to need one, stop and report."
  Does not edit `.github/`, `.claude/`, `docs/decisions/` or the owner's folders. "Don't commit, push, open PRs
  or touch git history. Don't kill processes." "Don't weaken or delete existing tests to make them pass; report
  the conflict instead." "If the same approach fails twice, stop and report what you learned rather than trying
  variations blindly."
- Reports: files changed and why; build results per platform, check totals, stress numbers; what it could not
  do and what it saw that is out of scope.

**Test engineer** - finds out whether the change does what the spec says, independently of how it was made.
- "Read the spec and its acceptance criteria **first** and decide what to test before reading the diff, so the
  tests check the requirement rather than restate the code."
- At least one test per acceptance criterion, named for what the user sees; the edge cases the spec implies:
  "degenerate and zero-area faces, non-manifold edges, open surfaces, curved and non-planar regions, huge / tiny
  scales, NaN or empty inputs, undo / F9 re-runs, and both keymap presets where shortcuts are involved". UI
  behaviour through `step_frame_headless` with real mouse / key / wheel / text events. For geometry tools,
  extends the matching stress section and runs it.
- Boundaries: "Edit only `tests/` and `stress/`. Never change `src/`, even to fix an obvious bug - report it."
  A failing test that shows a bug stays in place, marked in the report. No existing check loosened or deleted
  without the reason written beside it. "Don't commit, push or touch git history."
- Reports: tests added and what each pins; results; for each failure the input, expected and actual result, and
  whether the code or the spec looks wrong.

**Code reviewer** - read-only: "You do not write or edit files; you read, build, run tests and report."
- Reads the spec, the ADRs it cites, this file, "and the surrounding code of every changed function - not only
  the diff lines".
- Checks, in order: the **spec** (every criterion met and tested, nothing unrequested slipped in);
  **correctness** (index invalidation after `delete_faces` / `dissolve_*` / `merge_*`, copy-on-write, selection
  vectors resized with the mesh, `LastOp` / F9 re-runs from `before`, undo steps); **robustness** (degenerate
  input, NaN, scales, empty selections); **portability** (MSVC / GCC / Clang, the dependency-free build);
  **performance** (per-frame work in drags, O(n^2) loops, needless copies); **tests** (would fail without the
  change, no check loosened without a reason); **docs and decisions** (the fragment, `changelog.py check`, an
  ADR when needed; a round editing `CHANGELOG.md` / `README.md` directly is a finding unless it is a fold);
  **safety** (nothing touches `Assets/`, nothing pushes to `main`, no new dependency); nesting depth.
- Output: findings, most severe first, each with severity (blocker / major / minor / nit), `file:line`, a
  concrete failure scenario and a suggested fix, marked confirmed or suspected. Verdict **approve** or
  **changes needed**: "Approve only when there are no blockers or majors."
- Boundaries: "Never edit, write, commit, push, merge or approve a PR on GitHub. The human merges." "Never
  review your own fixes as if they were independent; if you were asked to fix something, say so." "Report what you see, not what the author hoped; don't
  soften findings to reach 'approve'."

### One coordinating task and parallel tasks
Under Claude Code a long-lived "hub" session took the owner's requests, started "worker" sessions and relayed
their reports. The same shape under Codex:
- **One coordinating task** talks to the owner, writes the pass's plan, starts the parallel tasks, keeps the
  list of open PRs and what waits on the owner, grants the graphics card and timed windows, rebuilds the owner's
  exe at each publish, and runs one changelog fold at a time. It does no round work itself and reports in a
  line or two unless the owner asks to plan.
- **Parallel tasks** each do rounds in their own worktree and publish on the current single top of the stack.
- Coordinate candidate revisions, build reuse and timed batches through the [parallel verification procedure](docs/handoff/METHOD.md#parallel-verification).
- Every plan carries a **file-ownership table**: each piece mapped to the source, test and doc files it may
  touch, and an overlap check against the work already running. Pieces that share no file run in parallel;
  pieces that share a file go to one task, in sequence; if everything meets in one file, don't parallelize and
  say so. A task that needs a file outside its row stops and asks the coordinating task.
- Rounds that share files within one task may be built and verified together, but each still gets its own
  commit and its own PR.
- An approved plan is the go-ahead for everything it needs (starting tasks, writing its specs); merges,
  releases and the safety rules stay the owner's.

## Git and publishing
- The development machine's git root is `blender-main/`; GitHub (EmilianoManaloIV/blendity) holds only
  `blendity/`, published through the `blendity-root` subtree branch. A clone of the GitHub repository has
  `blendity/`'s content at its root and needs no subtree step: branch from the stack's top (or `main`), commit, push.
- Commit only when the owner asks (an approved plan that ends in a PR is such an ask: the owner's standing
  instruction, 2026-11-10; not in `CLAUDE.md`). Never commit the owner's
  `Assets/Scenes/SampleScene.scene`, `Assets/Materials/` or `Assets/Exports/`.
- Commit messages end with a `Co-Authored-By:` trailer naming the AI tool that wrote the change (the history's
  commits name Claude; use your own tool's line), and PR bodies say they were written with it
  (`docs/AI_DISCLOSURE.md`).
- Publish a task to a PR branch, never to `main` or `blendity`. From the `blender-main/` checkout:
  ```
  git commit -m "$msg"                                   # on the local blendity branch
  sub=$(git commit-tree "$(git rev-parse HEAD):blendity" -p blendity-root -m "$msg")
  git push origin "$sub:refs/heads/task/NNNN-slug"
  gh pr create --repo EmilianoManaloIV/blendity --head task/NNNN-slug --base main --fill
  ```
  With an open stack the parent is the stack's top, not `blendity-root` (`tools/dev/publish.sh` takes `BASE`);
  from a worktree of a published branch it is a plain commit and `git push origin HEAD:refs/heads/task/NNNN-slug`.
  On the development machine `gh` is `"/c/Program Files/GitHub CLI/gh.exe"` from Git Bash (it isn't on its PATH);
  in PowerShell it is on the PATH. Read CI with `gh pr checks N`, `gh run list` / `gh run view --log-failed`, not
  by polling the anonymous API (60 requests an hour).
- After every push: read the PR's checks and fix what is red, including checks only CI runs (the sanitizer
  shards, the `docs-fragments` and `stack` jobs); read review comments on the open PRs at a task's start, after
  each push and before calling it done.
- `main` is protected: PRs only, `windows` / `linux` / `macos` / `linux-sanitizers` must pass on a branch
  up to date with `main`, no force pushes.
- After a merge: `git fetch origin main && git update-ref refs/heads/blendity-root origin/main`.
- No force pushes, no history rewrites. Branches are deleted only once merged into `main` (the owner asked,
  2026-10-13: `git push origin --delete` the merged `task/*` branches after each merge). Never push a fix onto a
  lower PR of a stack: it goes on the top.
- **Releases are the owner's.** Never push a `v*` tag, delete or republish a release unasked. When asked: bump
  `VERSION` in a PR that is on `main` before the tag (the app reads it), tag `main`'s merge commit, let
  `release.yml` publish, and do not set GitHub's pre-release flag (the README's `releases/latest/download/...`
  links skip flagged releases).

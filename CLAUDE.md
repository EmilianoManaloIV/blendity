# Blendity - engineering standards

Claude Code uses this guide and the [original worker contracts](.claude/agents/). For Codex, start
with [AGENTS.md](AGENTS.md) and the [project setup and workflow inventory](docs/handoff/CODEX.md).
Both guides follow the same owner's approval, publishing and machine rules.

Blendity is a C++17 editor that rebuilds Blender's modeling, UV, material and rendering ideas behind a
Unity-style editor (Hierarchy, Inspector, Scene view, Play mode), with SketchUp / UModeler /
Plasticity-style drawing and Push/Pull. Read `docs/requirements.md` (what it must do),
`docs/ARCHITECTURE.md` (how it is built) and `docs/decisions/` (why) before changing anything
non-trivial.

## Project map
- `src/core` math (Unity conventions), logging, filesystem, jobs, CPU features
- `src/platform` window + input (Win32 / X11 / Cocoa)
- `src/image`, `src/render` codecs, canvas, rasterizer, path tracer, GPU devices
- `src/scene` Mesh (Blender layout), `meshops` operators (`mesh_tools*.cpp`), scene, IO, modifiers
- `src/editor` IMGUI toolkit, docking, Scene view, tools (`draw_tool.cpp`, `scene_view.cpp`), panels, keymaps
- `tests/test_<group>.cpp` unit checks (one file a subsystem, `test_common.h` shared; `test_main.cpp` runs them);
  `stress/stress_main.cpp` stress / robustness sections
- `extern/` code copied from Blender's tree; `licenses/` notices; `docs/` design documentation
- Keep the user's `Assets/`, `Renders/`, `Screenshots/` and `research/` outside round edits, staging and verification.

## Build and run
- Windows (from Bash, absolute path): `cmd //c "C:\\...\\blendity\\build.bat release all|app|tests|stress"`.
  Never combine it with an exported `MSYS_NO_PATHCONV` in the same shell.
- Linux / macOS: `./build.sh release|debug all|app|tests|stress` (on this machine through WSL:
  `wsl -e bash -lc "cd /mnt/c/.../blendity && ./build.sh release all"`). `EXTRA_FLAGS` adds compiler
  and linker flags (sanitizers). Sanitizer builds need `BLENDITY_NO_LIBS=1` (UBSan's type checks can't
  link against the prebuilt Jolt, which has no RTTI) - as CI builds them.
- The user often has `dist\windows\Blendity.exe` open (LNK1104 when linking): rename the running exe
  aside (`Blendity.old.exe`) and build into place. Never kill the user's Blendity.
- Optional Blender libraries come from `../blender/lib/<platform>`; `BLENDITY_NO_LIBS=1` forces the
  dependency-free build that CI uses. Both must keep building.
- The user's local `dist\windows\Blendity.exe` stays current (the user, 2026-11-11): the hub rebuilds it from
  `origin/main` plus every open pass's newest published branch when a round's PR is **published** (review done,
  full local chain passed, commit pushed) - it does not wait for CI. Never earlier: not before review, not from
  uncommitted code. Each time, one line to the user on what the exe contains and which rounds in it are not yet
  green on CI; a fault CI finds later means a rebuild after the fix. Not while a timed stage runs.
- Headless runs: `Blendity.exe --headless-screenshot out.png --cmd "..."` with `BLENDITY_PROJECT` and
  `BLENDITY_TRASH` pointing at a scratch copy, never the real `Assets/`.

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
- A timed run (a measurement, a timed bake, a chain with `POSING=1`) takes a slot from the hub first:
  one holder at a time, nothing else builds or runs meanwhile. See [the machine's limits](docs/handoff/MACHINE.md).
- Geometry changes also run the matching stress sections (`blendity_stress --only pushpull |
  modifiers_ngon | curved | editor`). Problem counts and "faces newly on top of each other" counts must
  not rise without an explanation in the round's changelog entry. Delete the report files a run creates and restore
  `stress/results/latest.md` with `git checkout`.
- Tests and stress run in a scratch project and trash (`BLENDITY_PROJECT`, `BLENDITY_TRASH`); nothing a
  test does may touch the user's files.
- A test that disagrees with the code is evidence, not a nuisance: don't loosen a check without saying why
  in the test.

## Code standards
- C++17, MSVC + GCC + Clang. GCC needs explicit types in generic lambdas that call `get<T>()`.
- Match the surrounding code: naming, comment density (comments say why, in plain words), idiom.
- Meshes are copy-on-write: re-read `MeshPtr` after an edit; `mesh_make_mutable` before writing.
- Edit Mode operators that take numbers go through `LastOp` (`edit_op_redoable` + `run_last_op`) so F9
  can re-run them from `before`; their numbers live in F9 / the drag helper, not in the Inspector.
- Every tool is reachable from the console (`editop`, `edittool`, `drawpoint`, ...) so tests and scripts
  can drive it.
- No new dependency unless Blender itself bundles it, and then only behind a `BL_WITH_*` switch with a
  dependency-free fallback (ADR 0001). Source is GPL-2.0-or-later with an SPDX header in every file; the binaries are GPL-3.0-or-later (ADR 0002).
- Keep geometry robust: degenerate faces, non-manifold edges, open surfaces, huge / tiny scales and NaN
  inputs must leave a structurally valid mesh or refuse cleanly.

## Documentation duties (every change)
- `changelog.d/<round>-<slug>.md`: one entry per round (asked / fixed / changed / added / checked, stress numbers),
  plus a `<!-- README -->` section with the feature-paragraph line when user-facing behaviour changes, and a section
  for each shared doc the round adds to: `<!-- docs/USABILITY.md -->`, `<!-- tools/README.md -->`,
  `<!-- docs/ARCHITECTURE.md -->`, `<!-- docs/PERFORMANCE.md -->` (format: `changelog.d/README.md`). **A round never
  edits `CHANGELOG.md`, `README.md` (nor its unit-check count) or those four shared docs** (ADR 0123: parallel
  passes' PRs conflicted there). The pass write-up runs `py tools/dev/changelog.py fold --checks LIBS TESTS` once the
  pass's PRs are on `main` and commits it with a `Docs-fold: yes` trailer; the hub runs one fold at a time. The
  `docs-fragments` CI job fails any other commit that edits those files.
- `docs/ARCHITECTURE.md`: a row for new subsystems or functions others build on - in the fragment's section.
- `docs/USABILITY.md`: a "fixed" row for user-facing problems solved - in the fragment's section.
- `docs/decisions/`: an ADR for any architectural choice (new subsystem, data-model change, new
  dependency, change to a documented convention).
- `docs/tasks/`: the task spec the work was done against; mark it done.

## Workflow and approval gates
1. **Plan** (the main session on the strongest available model): write `docs/tasks/NNNN-slug.md` from the
   task template before implementation and an ADR for architectural decisions. Record the owner's assignment
   or approved plan as authorization for in-scope work. Follow the [shared workflow](AGENTS.md#workflow-and-approval-gates)
   through final review, including hub routing for material scope changes and owner-reserved decisions.
2. **Implement**: use the `implementer` agent for routine, well-specified work. The planning session
   handles hard geometry, architecture or cross-cutting changes.
3. **Test**: the independent `test-engineer` writes tests from the spec's acceptance criteria.
4. **Review**: the independent `code-reviewer` reviews the diff against the spec, ADRs and project rules.
5. **Merge**: the hub publishes the branch and checks CI. **The owner reviews and merges on GitHub.**
   The owner uses a merge commit for stacks. Agents do not merge PRs, use `--admin` or enable auto-merge.
6. **Every PR targets `main`** (`--base main`), never another PR's branch (the user, 2026-11-08). A round's
   commit may build on the previous round's, but its PR's base is `main`. **Guarantee:** merging the top PR
   of a stack merges every PR below it (GitHub marks them merged; use a merge commit). Several stacks may be
   open at once, one per pass; each must have exactly one top. After every publish and every push to a PR
   branch run `bash tools/dev/stack_check.sh` (`FIX=1` merges a stack's stray heads into its newest PR);
   the `stack` CI job runs the same check. The body's "Merging this also merges" line names the PRs it carries.
   **The integration PR** (ADR 0132): when several stacks are open, `bash tools/dev/integrate.sh` merges the top of
   every stack whose four required jobs are green into `integration/<n>` (from `main`, merge commits only) and opens
   one PR on `main` that carries them all - the one the user merges; `CHECK=1` only reports what it would merge and
   what conflicts. It is the one allowed top over several stacks; every stack it carries must be in it up to that
   stack's own top (`stack_check.sh` fails otherwise: run `integrate.sh` again after a push). Each PR still targets
   `main` and can still be merged on its own.
7. **Nothing waits on a PR** (the user, 2026-11-15). No round, fix, write-up or verification is held until a PR is
   merged or published: build on the open PR's head and publish stacked on it; work that needs several open stacks
   builds on a local merge of their tops and publishes as the single top of the joined stack (say which stacks it
   joins in the body). Only the user's merges wait on the user.

Run agents one after another; don't fan out. For a one-line fix, implement in the main session and still
run the reviewer.

## Git and publishing
- The git root is `blender-main/`; GitHub (EmilianoManaloIV/blendity) holds only `blendity/`, published
  through the `blendity-root` subtree branch.
- An assigned round or approved plan that ends in a PR authorizes the hub to commit. Keep all four private folders
  outside staging and commits.
- Name the AI tool that helped write the change in the `Co-Authored-By` trailer and PR disclosure.
  See [AI disclosure](docs/AI_DISCLOSURE.md).
- Publish a task to a PR branch, never to `main` or `blendity`:
  ```
  git commit -m "$msg"                                   # on the local blendity branch
  sub=$(git commit-tree "$(git rev-parse HEAD):blendity" -p blendity-root -m "$msg")
  git push origin "$sub:refs/heads/task/NNNN-slug"
  gh pr create --repo EmilianoManaloIV/blendity --head task/NNNN-slug --base main --fill
  ```
  From the Bash tool, call `gh` as `"/c/Program Files/GitHub CLI/gh.exe"` (it isn't on Git Bash's PATH);
  in PowerShell it is on the PATH. Read CI with `gh run list` / `gh run view --log-failed`, not by
  polling the anonymous API (60 requests an hour).
- `main` is protected: PRs only, `windows` / `linux` / `macos` / `linux-sanitizers` must pass on a branch
  up to date with `main`, no force pushes.
- After a merge: `git fetch origin main && git update-ref refs/heads/blendity-root origin/main`.
- Agents do not force-push, rewrite history or run `git reset --hard`. The owner authorizes only the hub to
  delete verified merged `task/*` refs through the [cleanup helper](tools/dev/cleanup_task_branch.py).
  Role workers and direct branch-deletion commands remain restricted. See the
  [project command policy](docs/handoff/CODEX.md#command-policy).
- Get the owner's explicit consent for the specific release tag or release action. Follow the
  [release procedure](AGENTS.md#git-and-publishing).

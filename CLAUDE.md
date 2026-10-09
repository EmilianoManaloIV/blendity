# Blendity - engineering standards

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
- `tests/test_main.cpp` unit checks; `stress/stress_main.cpp` stress / robustness sections
- `extern/` code copied from Blender's tree; `licenses/` notices; `docs/` design documentation
- Not ours to edit without asking: `Assets/` (the user's project), `Renders/`, `Screenshots/`, `research/`

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
- Headless runs: `Blendity.exe --headless-screenshot out.png --cmd "..."` with `BLENDITY_PROJECT` and
  `BLENDITY_TRASH` pointing at a scratch copy, never the real `Assets/`.

## Testing rules
- Every behaviour change or bug fix gets a unit test in `tests/test_main.cpp`, named for what the user
  sees, in the current round's group. Prefer real events (`step_frame_headless` with mouse / key events)
  for UI behaviour, `meshops` calls for geometry. Run one with `BLENDITY_TEST_FILTER="part of name"`.
- Before a change is done: the full unit suite passes on Windows and Linux.
- Geometry changes also run the matching stress sections (`blendity_stress --only pushpull |
  modifiers_ngon | curved | editor`). Problem counts and "faces newly on top of each other" counts must
  not rise without an explanation in the CHANGELOG. Delete the report files a run creates and restore
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
- `CHANGELOG.md`: one entry per round (asked / fixed / changed / added / checked, stress numbers).
- `README.md`: the unit-check count and the feature paragraph when user-facing behaviour changes.
- `docs/ARCHITECTURE.md`: a row for new subsystems or functions others build on.
- `docs/USABILITY.md`: a "fixed" row for user-facing problems solved.
- `docs/decisions/`: an ADR for any architectural choice (new subsystem, data-model change, new
  dependency, change to a documented convention).
- `docs/tasks/`: the task spec the work was done against; mark it done.

## Workflow and approval gates
1. **Plan** (main session, Opus): write `docs/tasks/NNNN-slug.md` (template in `docs/tasks/`) and an ADR
   if the decision is architectural. **The human approves the spec (and ADR) before code is written.**
2. **Implement**: the `implementer` agent (Sonnet) for routine, well-specified work; the main Opus
   session for hard geometry or cross-cutting changes.
3. **Test**: the `test-engineer` agent (Sonnet) writes tests from the spec's acceptance criteria.
4. **Review**: the `code-reviewer` agent (Opus) reviews the diff against the spec, ADRs and this file.
5. **Merge**: push a branch, open a PR (template in `.github/`), CI must pass. **The human reviews and
   merges on GitHub.** Agents never merge.

Run agents one after another; don't fan out. For a one-line fix, implement in the main session and still
run the reviewer.

## Git and publishing
- The git root is `blender-main/`; GitHub (EmilianoManaloIV/blendity) holds only `blendity/`, published
  through the `blendity-root` subtree branch.
- Commit only when the user asks. Never commit the user's `Assets/Scenes/SampleScene.scene`,
  `Assets/Materials/` or `Assets/Exports/`.
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
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
- `main` is protected: PRs only, `windows` / `linux` / `macos` must pass, no force pushes.
- After the human merges: `git fetch origin main && git update-ref refs/heads/blendity-root origin/main`.
- No force pushes, no branch deletions, no history rewrites.

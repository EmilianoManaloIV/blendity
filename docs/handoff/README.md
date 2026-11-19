# Handoff: Blendity at 1.0.0

Written 2026-11-18, the day Blendity ships as 1.0.0. The project was built in about six weeks (2026-10-05 to
2026-11-18) by one owner working with Claude Code sessions: 574 rounds of work in 28 passes, each round a spec, a
commit and a pull request the owner reviewed and merged. From here the work continues in a ChatGPT Codex project.
This folder is what a new agent, or a person, needs in order to carry on without the sessions' memory.

## Where to start
1. [AGENTS.md](../../AGENTS.md) holds the standing rules. Open the Blendity project root and verify discovery
   using the [Codex setup and migration inventory](CODEX.md).
2. This file, then `METHOD.md` (how a change goes from a request to a merged PR) and `PITFALLS.md` (what cost
   time, so it need not cost it again).
3. `OPEN-WORK.md`: the follow-ups nobody has taken, each with its files and evidence, ready to become tasks.
4. `MACHINE.md`: the owner's development machine - toolchains, limits, scratch folders and the helper scripts
   (copied into `tools/dev/machine/`). Only needed when working on that machine.
5. Before changing code: `docs/requirements.md`, `docs/ARCHITECTURE.md`, and the ADRs the area cites.

## What 1.0.0 contains
The README's feature section is the full list; `docs/MANUAL.md` walks through it. In short:
- A Unity-style editor: Hierarchy, Inspector, Project, Scene and Game views, docking, Play mode, two keymap
  presets, a console that reaches every tool, and the Learn tab (interactive lessons, `src/editor/lessons.cpp`).
- Blender's modeling behind it: meshes in Blender's layout, Edit Mode tools, modifiers, Booleans, UV unwrapping,
  sculpting, weight and vertex paint, LoopTools; SketchUp-style drawing and Push/Pull; the Blockout object
  (rooms, openings, doors and windows from primitives).
- Rendering: a CPU rasterizer, a CPU path tracer, Vulkan views on any card, an OptiX path on NVIDIA cards, baked
  lighting (lightmaps, probes, Shadowmask), ray-traced shadows in the views, Gaussian splats, panoramic cameras.
- Rigging and animation: skins, IK, humanoid posing, clips; skinning on the graphics card (Pass 26).
- Files: OBJ, FBX, glTF, STL, PLY, USDA in and out, Unity packages in, splats (PLY, SPZ); a Unity Live Link.
- Three programs per build: `Blendity`, `blendity_tests` (over 100,000 unit checks in about 3,700 tests; the
  README holds the exact count) and `blendity_stress` (robustness sweeps and timed rows).
- Two builds that must both keep working: with Blender's prebuilt libraries (`../blender/lib/<platform>`) and
  dependency-free (`BLENDITY_NO_LIBS=1`, what CI builds).
- Installers for Windows, macOS (Apple chips) and Linux, built by `release.yml` from a `v*` tag.

The release itself (the `VERSION` bump, the tag, the installers) is described by Pass 28's write-up, the top PR
of the stack this handoff was published on, and by the Releases page; this folder does not repeat it.

## What is open
- **Follow-ups:** `OPEN-WORK.md`. None blocks the release; the largest are the graphics card's power state
  holding posing frames in a world over 16.7 ms, and a handful of testability gaps.
- **Decisions that are the owner's:** listed at the end of `OPEN-WORK.md` (for example whether round 565's
  kept fix, at 1.2x, stays under ADR 0112's 1.3x bar; the backlog of merged `task/*` branches the agents were
  not allowed to delete; old worktrees).
- **The integration PR** (`tools/dev/integrate.sh`, ADR 0132) was a trial; Passes 26 to 28 used one top per
  stack instead. The rule in `AGENTS.md` stands until the owner decides (`OPEN-WORK.md`).

## Where each kind of knowledge lives
| What | Where |
|---|---|
| The rules | `AGENTS.md` (`CLAUDE.md` retains the Claude Code guide) |
| What the program must do | `docs/requirements.md` |
| How it is built; which function to build on | `docs/ARCHITECTURE.md`, `docs/Blendity-Architecture.pdf` |
| Why it is built that way | `docs/decisions/` (ADRs up to 0153; its README lists them) |
| What each round was asked and did | `docs/tasks/NNNN-slug.md` (the spec, with its status and results) |
| What changed, round by round, with check counts | `CHANGELOG.md`; rounds not yet folded: `changelog.d/` |
| Measurements and budgets | `docs/PERFORMANCE.md`, `docs/perf/` (the sweeps and verifications) |
| User-facing problems found and fixed | `docs/USABILITY.md`, `docs/hci/` |
| How to use it | `docs/MANUAL.md`, `docs/guides/lighting.md`, `docs/LIVELINK.md`, the Learn tab |
| The audits and benchmarks | `tools/README.md` |
| The round workflow's scripts | `tools/dev/README.md` |
| The development machine's helpers | `tools/dev/machine/`, `MACHINE.md` here |
| How AI was used | `docs/AI_DISCLOSURE.md` |
| Licences of copied and linked code | `licenses/`, `CREDITS.md`, ADRs 0001 and 0002 |
| The three roles | `.codex/agents/*.toml` and the original `.claude/agents/*.md` contracts |
| CI | `.github/workflows/` (`build.yml`, `docs.yml`, `stack.yml`, `release.yml`, `moonray.yml`); shards in `tools/dev/test_shards.py` |

What is **not** in the repository and was not carried over: the sessions' private memory notes and scratch
folders (their lessons are in `PITFALLS.md`; their open items in `OPEN-WORK.md`), the live board the hub kept
(a private page listing each round's state; `gh pr list` and `stack_check.sh` give the same facts), and anything
from the owner's `Assets/`.

## First hour, suggested
1. Clone, build both ways, run one test group (`AGENTS.md`, "Build and run"; `--list-groups`).
2. `bash tools/dev/stack_check.sh` and `gh pr list`: what is open, and that each stack has one top.
3. `python tools/test_coverage_map.py --check`, `python tools/sweep_commands.py --check`,
   `py tools/dev/changelog.py check`: the three checks that most often surprise a publisher.
4. Pick a small item from `OPEN-WORK.md`, write its spec from `docs/tasks/TEMPLATE.md`, and take it through
   `METHOD.md` once before starting anything parallel.

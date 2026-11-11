# tools/dev: the round workflow's scripts

The scripts the AI sessions (and anyone repeating a round by hand) use to take a change from the dependency-free tree to a
stacked pull request. They lived in a session's scratch folder until round 446; they are kept here so a new session
finds them. Nothing here is built or shipped.

Every path is a setting at the top of a script, overridable by environment variables:

| Setting | Environment | Default (the development machine's) |
|---|---|---|
| The dependency-free tree | `BL_W` | `W:/blendity` |
| The Blender-libraries clone | `BL_MAIN` | `C:/Users/trick/Downloads/blender-main/blendity` |
| The Linux worktree under WSL | `BL_WSL_TREE` | the WSL path in `chain.sh` |
| Unity (for the SideRail comparison) | `BL_UNITY` | `C:/Program Files/Unity/Hub/Editor/2022.3.22f1/Editor/Unity.exe` |
| SideRail's Blendity copy | `BL_SIDERAIL` | `C:/Users/trick/Downloads/SideRail Blendity` |
| A project holding the imported avatar (`POSING=1`) | `BL_SHINANO` | `C:/bltest399/default_project` |
| The avatar's Unity package (`POSING=1`) | `BL_SHINANO_PACKAGE` | `C:/Users/trick/Downloads/Shinano_ver1.02/Shinano_ver1.02/Shinano.unitypackage` |
| A prop of the world to move (`POSING=1`) | `BL_POSING_PROP` | `Stool` |

## The scripts

| Script | What it does |
|---|---|
| `chain.sh` | One round's verification: builds the dependency-free tree's tests and stress, copies the change into the libraries clone (`copy_w_to_main.py`), builds its tests, app and stress, runs both suites (`tools/run_suites.py`), the Linux build and suite under WSL, and the audits. `LMQ=1` adds the bake-quality section, `SIDERAIL=1` the SideRail comparison against Unity (with `--pages --noise`: the lightmap pages object by object, and a second bake at `lighting seed 1` for the bake's grain and blotch, round 463). `POSING=1` adds the posing stage (round 468): the stress sections `posing` and `animation` through `tools/perf_suite.py --check` against the committed baseline, then `tools/bench_world.py --posing` on the avatar alone, the world alone and the avatar standing in the world, each on a copy the tool makes under the logs' folder (the projects are never written) - timed, so nothing else may build or run meanwhile. `TAG=NNN OUT=C:/blsuitesNNN bash chain.sh`; its logs go to `C:/blcmpNNN/` (never the drive's root: a failed redirect there means the chain never runs). A running `Blendity.exe` is renamed aside, never closed. |
| `publish.sh` | Commits the libraries clone's tree on its local branch, then the `blendity/` subtree onto `BASE` on GitHub as `BRANCH`, and opens the pull request against `main` (`BASE` only picks the parent commit). `BRANCH= BASE= MSG=<commit message file> PR=<body file> TITLE= bash publish.sh`. Refuses a staged experiment hook. |
| `stack_check.sh` | The stack guarantee (the owner, 2026-11-08): merging the top pull request of a stack merges every one below it. Lists each stack (open PRs sharing commits not on `main`) and fails one with more than one top; `FIX=1` merges a stack's stray heads into its newest PR's branch (a merge commit, pushed). An integration PR (head `integration/<n>`) is the one allowed top over several stacks: each stack it carries must be in it up to the stack's own top, a stack wholly outside it is only noted. Run from the libraries clone after every publish and every push to a PR branch; `.github/workflows/stack.yml` runs the same check on every pull request. |
| `integrate.sh` | The integration PR (ADR 0132): from `origin/main`, merges the top branch of every open stack whose required jobs (`windows`, `linux`, `macos`, `linux-sanitizers`) are green - merge commits only, made with `git merge-tree`, so no working tree is touched - pushes `integration/<n>` and opens a PR on `main` whose body names every PR it carries. While that PR is open a later run continues its branch (fast-forward) and rewrites the body. `CHECK=1` only reports what would be merged and what conflicts; a conflict stops the run before anything is pushed and names the two PRs. `SKIP="372"` leaves a stack out, `NEW=1` starts the next number from `main`. It never merges a PR. |
| `stack_lib.sh` | What the two share (sourced): the open PRs (`GH` names the gh to call), their grouping into stacks, each stack's tops. |
| `stack_selftest.sh` | Their self-test, on a throwaway repository with a local bare origin and a stand-in gh (never GitHub). The `stack` CI job runs it. |
| `copy_w_to_main.py` | The dependency-free tree's changes (against HEAD) into the libraries clone, checked byte for byte. |
| `patchlib.py` | `patch(path, [(old, new)])`: each old text must occur exactly once; newlines kept. The rounds' patch scripts import it (write them with an editor, not a shell heredoc: heredocs drop backslashes). |
| `fill_checks.py` | Fills a round's `CHECKS_*` placeholders in its changelog fragment (`changelog.d/`) and extra files from a chain's numbers. README's count is the fold's. |
| `changelog.py` | Changelog fragments (ADR 0123): `check` validates `changelog.d/`; `fold --checks LIBS TESTS` puts the fragments into `CHANGELOG.md` (highest round first) and their edits into `README.md` and the shared docs (`docs/USABILITY.md`, `tools/README.md`, `docs/ARCHITECTURE.md`, `docs/PERFORMANCE.md`: `after:`, `replace:` / `with:`, `append:`), sets the count and deletes them, all or nothing (`--dry-run`); `same-render OLD NEW` compares two READMEs as they render; `--self-test` runs its cases in temporary folders. Format: `changelog.d/README.md`. |
| `docs_check.sh` | Fails when one of a branch's own commits edits `CHANGELOG.md`, `README.md` or a shared doc without a `Docs-fold: yes` trailer (for the shared docs: commits that descend from the round that added them to the rule), and runs `changelog.py check`. `bash docs_check.sh [BASE [HEAD]]`; the `docs-fragments` job (`.github/workflows/docs.yml`) runs it on every PR, `stack_check.sh` reports it for every open PR. |
| `views.py` | Pass 18's view rows: Shinano's Scene view (and Scene + Game) and 50,000 cubes' Scene view, windowed on the card through `tools/bench_world.py` (`BL_SHINANO` names the project holding the imported prefab). `py tools/dev/views.py <Blendity.exe> <out dir> [label]`. Run it with nothing else building. |
| `siderail_stations.json` | The four SideRail camera stations the bake comparison renders (`tools/unity_compare.py world ...`). |
| `dircompare.py` | Unity's direction maps against a Blendity bake's direction pages (the length's spread over lit texels, round 427 / 438). |

## Rules the scripts assume
- Never run Python from standard input (`py -`): it waits for input that never comes. Write a file and run it.
- One round, one commit and one pull request, against `main`, its commit on the previous round's (the newest PR carries
  every open one: `stack_check.sh`); CI checked once per push; the owner
  reviews before anything is merged.
- When several stacks are green at once, `integrate.sh` builds the one PR the owner merges (ADR 0132); each PR can
  still be merged on its own. Check first with `CHECK=1`.
- A round never edits `CHANGELOG.md`, `README.md`, `docs/USABILITY.md`, `tools/README.md`, `docs/ARCHITECTURE.md` or
  `docs/PERFORMANCE.md` (ADR 0123): its entry, and any line for those files, go in
  `changelog.d/<round>-<slug>.md`. The pass write-up runs `py tools/dev/changelog.py fold --checks LIBS TESTS` after
  the pass's PRs have merged into `main`, and commits the result with a `Docs-fold: yes` trailer. The hub runs one
  write-up (one fold) at a time; a pass without a write-up gets a one-commit fold PR.
- See `docs/PERFORMANCE.md` and the round specs in `docs/tasks/` for what each round measured.

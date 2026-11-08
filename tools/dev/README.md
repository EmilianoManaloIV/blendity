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

## The scripts

| Script | What it does |
|---|---|
| `chain.sh` | One round's verification: builds the dependency-free tree's tests and stress, copies the change into the libraries clone (`copy_w_to_main.py`), builds its tests, app and stress, runs both suites (`tools/run_suites.py`), the Linux build and suite under WSL, and the audits. `LMQ=1` adds the bake-quality section, `SIDERAIL=1` the SideRail comparison against Unity. `TAG=NNN OUT=C:/blsuitesNNN bash chain.sh`; its logs go to `C:/blcmpNNN/` (never the drive's root: a failed redirect there means the chain never runs). A running `Blendity.exe` is renamed aside, never closed. |
| `publish.sh` | Commits the libraries clone's tree on its local branch, then the `blendity/` subtree onto `BASE` on GitHub as `BRANCH`, and opens the pull request against `main` (`BASE` only picks the parent commit). `BRANCH= BASE= MSG=<commit message file> PR=<body file> TITLE= bash publish.sh`. Refuses a staged experiment hook. |
| `stack_check.sh` | The stack guarantee (the owner, 2026-11-08): merging the newest open pull request merges every open one below it. Lists the open PRs whose head is not in the newest PR's history; `FIX=1` merges them into the newest PR's branch (a merge commit, pushed). Run from the libraries clone after every publish and every push to a PR branch; `.github/workflows/stack.yml` runs the same check on every pull request. |
| `copy_w_to_main.py` | The dependency-free tree's changes (against HEAD) into the libraries clone, checked byte for byte. |
| `patchlib.py` | `patch(path, [(old, new)])`: each old text must occur exactly once; newlines kept. The rounds' patch scripts import it (write them with an editor, not a shell heredoc: heredocs drop backslashes). |
| `fill_checks.py` | Fills a round's `CHECKS_*` placeholders in the CHANGELOG and the README's check count from a chain's numbers. |
| `views.py` | Pass 18's view rows: Shinano's Scene view (and Scene + Game) and 50,000 cubes' Scene view, windowed on the card through `tools/bench_world.py` (`BL_SHINANO` names the project holding the imported prefab). `py tools/dev/views.py <Blendity.exe> <out dir> [label]`. Run it with nothing else building. |
| `siderail_stations.json` | The four SideRail camera stations the bake comparison renders (`tools/unity_compare.py world ...`). |
| `dircompare.py` | Unity's direction maps against a Blendity bake's direction pages (the length's spread over lit texels, round 427 / 438). |

## Rules the scripts assume
- Never run Python from standard input (`py -`): it waits for input that never comes. Write a file and run it.
- One round, one commit and one pull request, against `main`, its commit on the previous round's (the newest PR carries
  every open one: `stack_check.sh`); CI checked once per push; the owner
  reviews before anything is merged.
- See `docs/PERFORMANCE.md` and the round specs in `docs/tasks/` for what each round measured.

# Pitfalls: every lesson that cost time

Each entry: what happened, why, and what to do instead. Dates are 2026. Most of these were paid for more than
once before they were written down; the ones marked **(repeat)** kept happening after that.

## Merging and the stack

**A keep-both merge drops a shared closing `});` and `}`.** When two branches each append a test block at the
same place, git's conflict block holds each side's block *without* its last two lines: those are identical, so
git keeps them once, after the conflict. Concatenating "ours" and "theirs" leaves the first function open. The
app still builds; the tests fail to compile on every platform ("function definition is not allowed here"). On
11-17 two merges were pushed this way without building, and another round had merged on top before CI said so.
*Do:* after any keep-both resolution put the shared tail after the first block too, then **build the tests**,
not only the app, before pushing - merges included, and the local merges made for the owner's exe.

**`stress/sweep_commands.inc` goes stale (repeat: #443, #462, #466).** A new console command or test command
changes which example `tools/sweep_commands.py` picks, the generated table no longer matches, and the Linux
unit job exits 1 with no failing test named. *Do:* run `python tools/sweep_commands.py --check` before every
push and regenerate when it complains. Making the pick stable is in `OPEN-WORK.md`.

**A fix pushed onto a lower PR of a stack** lands outside the stack and needs its own CI cycle and merge.
*Do:* fixes go on the top.

**Stacked PRs based on each other's branches** (October): when the bottom PR merged and its branch was deleted,
GitHub retargeted the ones above onto the next surviving branch, and merging them landed everything there
instead of on `main`; another time the deletion closed the PR above. *Do:* every PR's base is `main`; before
merging a stack bottom-up, check `gh pr list --json baseRefName`. Merging only the top avoids it altogether.

**A commit parented on `main` while an earlier round is open** shows both rounds in the PR's diff. *Do:* parent
it on the open round's head; the base stays `main`.

**Parallel passes conflicted in the same doc files** (CHANGELOG, README, USABILITY, tools/README, ARCHITECTURE,
PERFORMANCE): three passes cost four merge rounds in a day. *Do:* changelog fragments (`METHOD.md`); never edit
those six files in a round.

**Adjacent `#include` lines and list files** (the Learn lessons, the command table, the test group list)
conflict between otherwise disjoint rounds. They are keep-both merges: see the first entry.

**A test file at MSVC's limit.** `tests/test_blockout.cpp` hit C1128 (too many sections). *Do:* a new test block
for a full group goes in a new `tests/*.cpp`, declared and called from the group's runner.

## Tests that lied

**Tests read the owner's preferences and a shared scratch folder.** `blendity_tests` used one
`%TEMP%\blendity_tests` folder for every run on the machine, never emptied (one test found 2,054 leftover
materials), and the headless editors read the owner's real `~/.blendity/prefs.txt`. A card test read 2.281/255
in some runs and 0.203 in others. Rounds 550 and 556 fixed both defaults (a folder per run; scratch prefs).
*Do:* still give each run its own `BLENDITY_TEST_DIR` and `BLENDITY_PREFS_DIR`; do not set
`BLENDITY_CACHE_DIR` for ordinary runs; never let two runs share a test dir (one wiped the other's log on
11-18). `BLENDITY_PROJECT` / `BLENDITY_TRASH` are for the app and stress; the test runner sets its own.

**Wall-clock tests fail under load (repeat).** Tests that counted frames in real time, raced a render, relied
on a toast's three seconds, or made two clicks less than 0.35 s apart by accident passed alone and failed in a
sanitizer shard, at below-normal priority, or beside another build. Rounds 517, 568 and 569 moved them to a
pinned clock and a fixed number of stepped frames. *Do:* pin the editor's clock (`set_clock_for_test`), step
frames, assert behaviour; a time is printed or is a stress row, never asserted (ADR 0056). Play's physics and
`anim_tick` still read the real clock (`OPEN-WORK.md`): pause and step them. A failure seen under load is rerun
alone before it is believed - and a failure that goes away alone is still a test to fix.

**A frame where nothing changed is not drawn**, even after `mark_changed`: a test counting draws must change
something each frame.

**Limits copied from one build's measurement.** The libraries build, the dependency-free build and Linux differ
slightly; a bound must be a principled one. A round's group must pass in **both** builds before the long checks
(one card test failed only with the libraries, found after a 90-minute chain).

**`blendity_tests --help` runs the whole suite**, and `Blendity.exe --help` opens the editor and blocks. Use
`--list-groups` / `--group`.

**A second build relinking `blendity_tests.exe` while a run holds it** fails with LNK1104, and the script then
quietly tests the old binary. *Do:* one build-and-test job per tree; check the new test's name appears in the
log.

**Editing sources while a build compiles them** leaves objects built from a half-edited header (an ASan
stack overflow in the editor's constructor, 10-11). `build.sh` compares mtimes only; `tests/*.h` are not in its
header check at all (after editing only a test header, `touch tests/*.cpp`). A copied tree that keeps old
mtimes can be older than its objects.

**Stress cleanup deleted tracked reports.** Delete only the report files the run just wrote (by the timestamp it
prints), never a pattern; `git status` before committing.

**Run tests from the tree** (fixtures are relative paths), and Linux tests with `stdbuf -o0`, or an abort loses
the output.

## The machine

**Thirteen builds at once ran the machine out of memory** (11-16, 01:24): 333 compiler processes, links stalled,
then the desktop app and WSL restarted and every build in flight was lost. *Do:* the two Windows build slots
(`MACHINE.md`). One slot per task at a time; the two builds of a round go one after the other.

**WSL has 15 GB.** One Linux build at `nproc` jobs trips the memory killer, and `wsl -e` then answers
"Catastrophic failure" while the distribution still lists as running. Sanitizer compiles need about 1.9 GB a
test file and took WSL down even on five cores. *Do:* `taskset -c 0-9`, one Linux compile at a time under the
WSL lock, **no local sanitizer builds**. Before `wsl --shutdown`, test whether WSL answers: on 11-14 a shutdown
of a WSL that had already recovered killed another task's build. Only the coordinator shuts it down.

**Stale and orphaned locks (repeat).**
- Stopping the shell that runs `take; build; drop` kills only the shell: the build goes on and `drop` never
  runs, so the slot stays held and blocks everyone. `build-low.bat` now takes and drops inside the same `cmd`
  as the build.
- Builds that hung at launch (a Git Bash `start` quoting problem) held both slots for an hour. The locks now
  reclaim a slot older than 90 minutes, or 10 minutes old with no compiler running (WSL: 2 hours / 10 minutes).
- The reclaim then bit back: the timed window's first slot was reclaimed as stale while it waited for the
  second. `timed-window.sh` keeps its owner files touched from the first take.
- Stopping a lock-taking script leaves its `take` loop alive; that orphan later took the WSL lock and a window
  sat stuck for half an hour. *Do:* after stopping any such script, look for leftover `*-lock.sh take`
  processes and end them; `win-lock.sh show` before and after.
- After a crash or restart, drop locks whose owners have nothing running, and check each worktree for
  half-made edits before restarting its task.

**Stopping a background job does not stop its children.** A stopped check chain kept running its tests and then
its WSL stage. *Do:* list the processes by command line and end the job's own ones - never the owner's
`Blendity.exe`.

**A HOLD message arrives late.** The coordinator asked every task to start nothing, then opened a timed window;
one task's message sat in its queue while it started a build, and the window's numbers were spoiled (11-18).
The same queueing made a "CARD YOURS" appear lost, so two runs overlapped. *Do:* the slot-holding window
(`METHOD.md`); one explicit card grant at a time.

**A wait loop that greps a note for `DONE`** matched the note's own sentence ("... until this says DONE").
Never write the word a script waits for in the text it reads.

**A timed folder without `shaders/`.** A stress exe copied to a timed folder with its DLLs but not
`dist/windows/shaders/` showed 22 ms against 9 ms before; the difference was the missing shader cache. A tree
that only ever built tests or stress has no such folder. *Do:* copy it, and check with `ls` before sending the
command.

**The card's power state.** At light load the RTX 4070 SUPER sits in P8 (210 MHz); the same frame costs about
1 ms with the clock up and 10 to 13 ms there, and two runs of one build differ by up to 3 ms a row. A first
"fix" looked like 2x on the card's own timestamps and measured nothing. *Do:* sample
`nvidia-smi --query-gpu=pstate,clocks.gr,utilization.gpu -lms 250` beside every card measurement, compare A/B on
one exe with a switch, take two before-runs to see the spread. Never change the owner's driver or power
settings.

**Pipeline caches make card start-up times warm.** NVIDIA's driver caches pipelines per executable path. For a
cold number, copy the exe to a new name and clear the SPIR-V cache. An unoptimised shader path looked free
when warm and cost about 105 s when cold.

**Machine load drifts about 20 % between runs.** A speedup is two binaries measured in the same session, not
today against yesterday.

**A worktree outside the main checkout does not find `../blender/lib`** and silently builds without the
libraries. Set `BLENDITY_LIBDIR`. Sub-agents' automatic worktrees checked out an old default commit with no
`blendity/` folder: make worktrees by hand.

## Shell and scripting on Windows

**Never run Python from standard input** (`py -`, with or without a heredoc): it waits for input that never
comes until the tool's timeout. It happened five times in one session and three more in another before a hook
refused it. *Do:* write the script to a file, run `py "<full path>"`; one-liners with `py -c`. The bare
`python` name on this machine is the Store stub; use `py` or the full path.

**Heredocs mangle backslashes.** A patch script written through a shell heredoc (even a quoted one) had its
`\n` turned into real newlines, so a C++ `printf("...\n")` landed in the source broken across two lines (rounds
175 and 176; builds broke twice). The same happens to `\n` in `sed` / `perl` replacements, and a `perl s|..|`
whose pattern contains `\|` becomes alternation. *Do:* any text with a backslash goes through a file-writing
tool or a patch file (`tools/dev/patchlib.py`: each old text must occur exactly once).

**`MSYS_NO_PATHCONV` exported in the shell that calls `build.bat`** makes `cmd //c` print its banner, build
nothing and exit 0. Set it only in a subshell; check the exe's timestamp after a build.

**Git Bash cannot pass `ProgramFiles(x86)`** (a name with parentheses), so `build.bat` could not find Visual
Studio; `build-low.bat` sets it. A bare `/mnt/c/...` argument is rewritten by Git Bash: run WSL scripts as
`wsl -e bash -lc "bash /mnt/c/..."`. `bash` reserves `GROUPS` (hence `TEST_GROUPS`).

**Renaming a running exe aside.** Windows lets a running exe be renamed, not overwritten: rename
`Blendity.exe` to `Blendity.old.exe` (or `.old1`, ...) and build or copy into place. Never kill it. Afterwards
delete the old copies once `tasklist` shows nothing running them - the owner asked to keep only the newest.

**Polling GitHub's anonymous API** exhausts 60 requests an hour and then hangs silently. Use `gh`. A failing
check can also be a stale push run while the PR run passed.

## CI

**GCC 15 / ubuntu-26.04.** GitHub's `ubuntu-latest` moved from 24.04 (g++ 13.2) to 26.04 (g++ 15.2) between
one PR's run and `main`'s (11-16): `main` went red on Linux with no change of ours - a segfault in the curved
stress section and two unit tests - and that blocked every open PR's Linux check. GCC 15 exposed real
undefined behaviour and uninitialised reads. They were fixed (round 549, with a throwaway CI branch printing
backtraces) rather than pinning the image. *Do:* when every PR turns red in the same job at once, look at
`main` and the runner image first. The local WSL is now also Ubuntu 26.04 with g++ 15.2, so most of it
reproduces locally.

**GitHub macOS runner shortages.** macOS jobs were repeatedly lost ("runner lost"), queued for a long time, or
timed out on the larger trees (the stress step's 15-minute alarm). They are reruns, not faults - but read the log before deciding,
because macOS also found real clock-dependent tests. Raising that alarm or sharding macOS is in `OPEN-WORK.md`.

**Checks only CI runs.** The sanitizer shards (ASan + UBSan), the `docs-fragments` job, the `stack` job and the
bridges job's `tools/unity_meta.py --check` are not in a local run; on 11-01 they had been red for days while
local suites were green. *Do:* `gh pr checks` after every push and again before calling a round done. A new
test group must be added to `tools/dev/test_shards.py` or its check fails.

**CI runners have 3 (macOS) to 5 (Linux) threads.** Reproduce runner-only behaviour with `taskset -c 0-4`.

## Releases

- **`VERSION` on `main` before the tag.** The app reads `VERSION`; the bump is a PR, merged, and the tag goes
  on `main`'s merge commit. `release.yml` publishes from the tag.
- **No pre-release flag.** The README's `releases/latest/download/...` links skip releases flagged as
  pre-release: they returned 404 under v0.10.0 for that reason.
- **Retarget stacked bases before merging a bottom PR** (`gh pr edit N --base main`): deleting a merged branch
  closes PRs based on it.
- **Releases are the owner's call.** An earlier 1.0.0 attempt was walked back; never tag, delete or republish
  unasked. Moving a published tag is a question for the owner, not a step.
- Deleting merged `task/*` branches was refused to the agents by the permission system more than once; the
  owner does it or allows it.

## Working with the owner

- **Merges are the owner's click.** A "you may merge" was given once or twice and was for that time only.
- **Review comments, not only CI.** A review comment on PR #50 (deep nesting) sat unread for hours because only
  checks were being read.
- **Idle waits.** A whole night once idled on check waits; since then the next round is written while the
  current one verifies.
- **A plan put up for approval holds only that plan**, said in plain text first.
- **Quiet coordination.** No unprompted status reports; speak for a decision only the owner can make.
- **The main thread.** A first card pipeline build on the UI thread froze the editor for 80 s and Windows closed
  it as hung (10-11). Every spec since has a responsiveness criterion, measured cold.

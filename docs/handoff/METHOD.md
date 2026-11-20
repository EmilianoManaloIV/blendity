# The method that worked

How Blendity's changes were made, in the order a change meets each step. The rules themselves are in
`AGENTS.md`; this file says how they were applied and why each one exists. Where a rule came from a loss, the
loss is in `PITFALLS.md`.

## Rounds and passes
- A **round** is one request-sized change: one spec (`docs/tasks/NNNN-slug.md`), one commit, and one changelog fragment. Publish independently reviewable work as described in the owner's publication rules in AGENTS.md. Supporting work travels with its deliverable. Rounds are numbered in the order they start; specs have their own
  numbers. A PR's title names both ("... (round 565, task 0547)").
- A **pass** is a group of rounds the owner approved as one plan: a goal, the rounds with their specs, a
  file-ownership table and the budgets to meet. It ends with a **write-up round** that measures the result
  against the plan, folds the fragments and lists follow-ups. An owner assignment or approved plan authorizes
  the round's routine implementation decisions and in-scope follow-ups through completion.
- Each round's times were logged (started, checks started, checks done, published). A typical round took 1.3 to
  1.6 hours; the log is how the workflow changes below were judged.

## One round: spec, implement, test, review, PR
1. **Spec.** From `docs/tasks/TEMPLATE.md`: what was asked, what is in and out, numbered acceptance criteria
   each of which a test can check, the files to touch, the stress sections to run, a responsiveness criterion
   where the feature does work per frame. Write it before implementation, record the assignment or plan
   that authorizes it and mark it approved for dispatch. Add an ADR for architectural decisions.
2. **Implement** (the implementer role). Only the spec's files. For app changes, build both ways (Blender's
   libraries and dependency-free), then run the round's own test group in **both** builds before anything long
   runs. For project metadata, run the spec's lightweight checks.
3. **Test** (the test-engineer role). Tests decided from the acceptance criteria before reading the diff; UI
   behaviour through real events; edge cases the spec implies. A bug found is left as a failing test and
   reported, not fixed by the tester.
4. **Review** (the code-reviewer role), **before** the long verification, not after it: a review that finds a
   real fault after a 50-minute chain costs a second chain. Read-only; findings ranked; approve only with no
   blockers or majors. Fixes go in, and a second review looks at the fixes.
5. **Verify.** The round's test groups and `editor`, on Windows (both builds) and Linux
   (set the scratch paths to the Windows, full-library Windows, and WSL-visible Linux trees first; set `win_blender_bundle` to the Windows x64 bundle containing `tbb/include` and `tbb/lib/tbb12.lib`. The default is `../blender/lib/windows_x64` relative to the full-library tree; `TAG="verify-$(date +%s)-$$" BL_W="$win_candidate" BL_MAIN="$win_libs_candidate" BL_WSL_TREE="$linux_candidate" BLENDITY_LIBDIR="$win_blender_bundle" TEST_GROUPS="modeling,editor" bash tools/dev/chain.sh`, or the same steps by hand from a worktree); the stress sections
   for geometry changes; the audits (`tools/test_coverage_map.py --check`, `tools/sweep_commands.py --check`,
   `py tools/dev/changelog.py check`). The whole suite, macOS and the sanitizers are CI's.
6. **Publish.** The hub merges the stack's current top into the round's branch (build the tests again if the merge
   touched test files), commit, push `task/NNNN-slug`, open the PR against `main` with the template, run
   `bash tools/dev/stack_check.sh`, read the checks, tell the coordinating task the PR number and head.
7. **After.** The hub monitors the published head, addresses review comments, fixes red checks on the top
   of the stack and verifies CI through completion. The owner conducts final review and alone merges the PR.
   For a blocked round or rejected candidate, record the criterion, observed result and evidence, along
   with any dependency needed to resume.

Work is pipelined: while one round's checks run, the next round's spec and patches are written. Nothing waits
on a merge.

## Documentation template audit

Use the applicable format from the start for each current deliverable, including Hub and subsystem work.
Keep its required headings, metadata and applicable sections. Fill them with the actual scope, checks,
evidence and outcome. Give a reason for an inapplicable field, and identify the remaining action for pending
evidence. Keep accepted ADRs and released records as historical evidence.

| Artifact | Authoritative format and what to check |
|---|---|
| Task spec | [Task template](../tasks/TEMPLATE.md) and [status lifecycle](../tasks/README.md#lifecycle). Check the numbered filename and title, all six metadata fields, and the section order from Goal through Outcome. Record approval in Authorization and use the lifecycle status for the current stage. Set `done` only after the owner merges. Keep detailed results under Outcome or in linked evidence. Add the PR link and published head when available. |
| ADR when architectural | [ADR template and applicability](../decisions/README.md). Propose a numbered record and obtain owner approval before dependent code. Check status, date, requirements, Context, Decision and Consequences. Cite the ADR from the spec. A later numbered ADR supersedes an accepted decision. |
| Round changelog fragment | [Fragment format](../../changelog.d/README.md#a-fragment). Check that the heading date and round, filename, task reference and verification agree. Include the README section for user-facing behavior and each relevant shared-doc section with its fold operation. Record why stress or other checks did not apply if the format calls for them. |
| PR body | [PR template](../../.github/pull_request_template.md). Fill Task, What changed, Tests, Review and Documentation with evidence for the submitted candidate. State the AI tool and its role. For stacks, list each carried PR on the required “Merging this also merges” line. |
| Pass plan and role handoff | Use the [pass file-ownership table](#file-ownership-for-parallel-work), with dependencies and acceptance evidence routed through the [compact handoff](CODEX.md#compact-handoffs). |
| Subsystem records | Follow the referenced local format in addition to the round's formats. For example, use the [HCI artifact instructions](../hci/README.md) and [study protocol](../hci/study-protocol.md) for HCI records, or the test and report procedure named in the spec for performance evidence. |

1. **Author:** Before handoff, compare each document you own against the format above. Where applicable, verify
   that requirement and ADR references resolve, acceptance criteria have checkable outcomes, and measured
   results and review receipts identify the candidate they cover.
2. **Tester and reviewer:** The tester checks documentation criteria from the spec when applicable. The
   reviewer compares changed docs with their templates and checks the content and cited evidence. Report
   missing or inaccurate fields with their file and section in the normal review handoff.
3. **Hub:** Audit your own docs and the joined candidate before publication. List the touched paths with
   `git diff --name-only origin/main...HEAD` and, for an uncommitted candidate, also inspect `git status --short`.
   Apply the checks above to the current task docs, related fragments and any ADRs or subsystem records.
   Inspect the committed gate range with `git log --no-merges --oneline origin/main..HEAD` and run
   `bash tools/dev/docs_check.sh origin/main HEAD` from the published-tree worktree. It checks that range
   for protected shared-doc edits and checks the working tree's fragments. Template content needs human or
   agent inspection. Draft the PR body from the template, then check the rendered body and head after
   publication or a body regeneration. Update task status, PR links and verification outcomes as they change.

## Codex worker dispatch

The parent chat remains the hub. In a project-aware Codex session, dispatch the named
`implementer`, `test-engineer` and `code-reviewer` from [`.codex/agents/`](../../.codex/agents/)
separately and in that order. On a surface with task and model fields but no named-agent selector,
use a fresh worker for each role. Pass its role name, developer instructions, assigned worktree
and spec in the task, and set its listed model and reasoning effort where the fields are available.
For testing, pass the spec before sharing the diff. For review, pass the final diff and test results.
The hub records the owner assignment or approved plan, schedules shared resources and handles publishing. For
an in-scope review fix, the hub restores the spec's status to `approved` under the existing
approval and hands the fix back to the implementer. The implementer returns the spec to
`in review`. The hub repeats the affected verification and independent review before publishing.

### Direct task chats for independent work

For every approved plan, the parent hub records each deliverable, file owner, dependencies, acceptance
checks and shared resource needs on the board. By default each independent deliverable gets a task
chat and worktree and reports directly to the hub. Assign rounds that edit the same file to one
owner in sequence. Use a temporary workstream lead only when several tasks need material coordination.
Record what the lead must coordinate. For example, file-import and export-validation changes with
disjoint files and acceptance checks can report directly. In a UI plan, keyboard navigation and
accessibility may depend on a common focus contract. A lead can sequence that contract and coordinate
joint acceptance while each file keeps one owner.

Schedule roles as runtime capacity becomes available. Within each round, implementation finishes
before a separate test engineer derives checks from the spec and then sees the diff. An independent
reviewer receives the final candidate and results. The task chat or lead dispatches these roles
where supported, or asks the hub to dispatch them. Record assignments on the board and release
workers and reservations after handoff.

Child chats forward material scope changes and owner-reserved decisions with their rationale to the parent hub. Only the hub consolidates the questions and asks the owner once.

Use the [compact handoff format](CODEX.md#compact-handoffs) for dispatch, progress and results.

In every Claude and Codex handoff, workers, leads and the hub attach the references they actually used to
each consequential claim. Identify repository evidence by path and symbol or revision. Link upstream claims
to the primary source and give the version when relevant. For a measured or reproduced result, identify the
fixture, command, run or scratch evidence that demonstrates it. Label any unverified assumption explicitly.
The reviewer checks that each cited source supports the specific claim. The hub carries those references
into the task spec, ADR or PR wherever it records the claim.

The task chat or workstream lead sends the parent hub the candidate revision, verification evidence,
review verdict, blockers and resource requests. The hub owns the review gate and re-review, the
shared stack and its commits and publication, and all build, GPU and timed reservations. It monitors
worker progress, stale reservations, CI and PR feedback, routes fixes and keeps the owner's executable
current under the publish procedure. The owner approves plans and merges PRs.

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

- Group supporting specs, tests, findings and documentation with the deliverable or the pass write-up.
  Give an independently reviewable change its own round PR in the stack. Present the cumulative top to the
  owner for final review and merge.
- Every PR's base is `main`. A round's commit is built on the previous open round's, so the newest PR's branch
  contains all of them: merging that **top** PR with a merge commit merges every PR below it, and GitHub marks
  them merged.
- `bash tools/dev/stack_check.sh` lists each stack and fails when one has more than one top; `FIX=1` merges
  stray heads into the newest PR. It runs after every publish and every push, and as the `stack` CI job.
- Workers hand verified rounds to the hub for publication. The hub checks the **current** single top with
  `stack_check.sh`, brings the candidate onto that head and publishes it. The hub checks the resulting stack
  after each push.
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

## Parallel verification

The coordinator keeps one scratch ledger for the pass. Each round's row names its worker, owned files, candidate
revision (including any uncommitted diff), checks due or completed, artifact and report paths, and pending build,
card or timing reservations. Mark the next round ready to publish. Workers prepare scratch fixtures, commands
and expected results before requesting a reservation.

1. **Develop and review.** Run focused checks as the code changes. Once the code and regression tests settle,
   identify the candidate for independent review and final verification. A review fix gets a second review.
   Mark affected builds and check results stale, rebuild affected targets and run their checks on the new candidate.
2. **Choose the executable.** Record its source revision or hashes and establish that all inputs to that target
   match, including test or stress source, generated files, compiler and linker versions and options, libraries,
   and the runtime DLLs and shaders deployed with it. Record the binary hash or equivalent build provenance.
   Rebuild when an input differs or its equivalence is unknown. Run each changed tool's or document's own checks.
3. **Choose the measurement.** Compare binary and runtime configuration, backend, workload and scale, input
   data, process context, cache state and timing conditions. Record the original report, section coverage and
   exit status. Reuse only a successful run matching every condition of the required measurement, including
   before/after pairing when required. A quick run, another backend, a different process context or a tainted
   window requires a fresh measurement for the requested configuration.
4. **Batch ready workloads.** Workers give the coordinator a runnable scratch script for each workload.
   The coordinator alone schedules compatible scripts together in a quiet window using the procedure below.
   Record each child's exit status and verify its expected report and sections. Resume at the first missing or
   failed workload and repeat any affected pair. A tainted window invalidates every timing in it. Release the
   hardware promptly, then let waiting workers resume. [Machine limits and helpers](MACHINE.md) govern reservations.
5. **Publish the verified top.** The coordinator checks the current top and the actual combined candidate.
   Run the required checks for that combined tree as specified in the round and stack procedures. Each reviewed,
   verified round can publish on the current single top while unrelated work continues. Read the PR's checks
   and review comments. The owner merges.

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
2. build the release app there with a Windows slot, MSVC 14.51 and the owner's Blender libraries.
   Use the [incremental local build](MACHINE.md#repeated-local-editor-builds) and its persistent cache.
   After a keep-both merge, request `app-tests` and verify both executables from the reported package;
3. use the verified package's executable, shaders and runtime assets as deployment inputs. Protect the running
   owner's executable using the existing rename-aside procedure. Keep the previous executable until it closes;
4. smoke-test headless in a scratch project;
5. tell the owner in one line what the exe contains and which rounds in it are not green on CI yet.
It is rebuilt at publish, not at CI green (that reaches the owner one to three hours sooner) and not after a
merge (the exe already has that work). Never during a timed window.

## Standing Hub workflow changes

When the owner changes how Blendity work is conducted, the hub records the new default in the owning project
instruction and reconciles any older conflicting guidance. Record the scope and any explicit exceptions.
Carry the change into active worker briefs and applicable tooling. Keep task-specific choices and one-time
permissions scoped to their task. On a new session, read the current project instructions and handoff.

For shared dependencies, early smoke tests and verification of a specific revision, follow
[Parallel verification and evidence](#parallel-verification-and-evidence).

## The hourly check and the board

Follow the [parallel verification procedure](#parallel-verification-and-evidence) for evidence routing.
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
- The hub assigns task chats from each approved plan. Before stopping, each chat records what is published,
  what remains in its worktree and what comes next.
- Before publication, report the candidate revision and review verdict first. After publication, lead with the PR link and head commit.
- Helper scripts that prove useful go into `tools/dev/`, with their paths as settings at the top; scratch
  folders are never uploaded.

## Parallel verification and evidence

- **Route a change once.** In each pass with shared dependencies, record producer, consumer,
  public interface or fixture, candidate revision and evidence link in a shared scratch index.
  The producer updates its entry and alerts the affected consumer. Resource owners notify the next
  scheduled worker after release. That worker acquires the machine lock or waits for the Hub's explicit
  GPU grant before starting. Escalate to the Hub when ownership or order changes,
  another team is blocked, an owner decision is required, or the candidate is ready for review.
  Routine acknowledgments and duplicate status reports add no evidence.
- **Exercise the first connected path early.** As soon as one representative input can traverse a
  new subsystem, run a small test through its normal entry point. For an importer, load one textured
  model into a scratch project, save it and reopen in a fresh process with its sources detached.
  Record the revision and result while independent readers continue.
- **Freeze the joined candidate for full verification.** Producers identify their exact ready
  inputs. The verifier records one combined revision and runs required suites on that revision.
  Rebuild the app and tests with fresh objects after a shared layout or ABI change. On later
  edits, identify invalidated evidence and repeat the affected checks and required final gates.
- **Give each role a fresh brief.** Supply its task, worktree, candidate, owned paths, acceptance
  criteria and source/evidence pointers. The tester derives cases from the spec before reading the
  diff. The reviewer receives the tested diff and evidence, including any gaps.
- **Collect evidence mechanically.** The runner records the tested SHA and real process exit status
  with one native log per invocation. Use the [evidence helper](../../tools/dev/verification_evidence.py)
  with `native --revision SHA --group NAME --exit-code EXIT --log PATH` and add `--expected COUNT`
  when the fixture has a known total. For a hosted run use `ci --revision SHA --repo OWNER/REPO --run ID`.
  Keep the JSON receipt and logs in scratch. The native SHA comes from the build/run manifest.
  Check the manifest when attributing a result to a candidate. Inspect the first failed step for
  a causal diagnostic. Preserve the last verified result if evidence collection is unavailable.
  Report changed outcomes with supporting links. The hourly health check remains the fallback.

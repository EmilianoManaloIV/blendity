#!/bin/bash
# Self-test of stack_check.sh and integrate.sh (task 0465, ADR 0132) on a throwaway repository with a local bare
# "origin" in a temporary folder. It never reaches GitHub: GH points at a stand-in gh that answers `pr list` and
# `pr checks` / `pr view` from files and writes down `pr create` / `pr edit`. The `stack` CI job runs it.
# Usage: bash tools/dev/stack_selftest.sh
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(mktemp -d)
trap 'rm -rf "$ROOT"' EXIT
W=$ROOT/w; O=$ROOT/o.git; F=$ROOT/gh
FAKE=$ROOT/fakegh
cat > "$FAKE" <<'EOF'
#!/bin/bash
# A stand-in gh: open PRs in $FAKE_GH/open ("number branch"), past integration branches in all, a PR's jobs in
# checks/<number> ("job<TAB>bucket"), what was asked to be written in log and body.
D=$FAKE_GH
head=; body_file=; base=; state=open
a=("$@")
for ((i = 0; i < $#; i++)); do
  case "${a[i]}" in
    --head) head=${a[i+1]} ;; --body-file) body_file=${a[i+1]} ;; --base) base=${a[i+1]} ;; --state) state=${a[i+1]} ;;
  esac
done
case "$1 $2" in
  "pr list")
    [ -f "$D/down" ] && exit 1
    if [ "$state" = all ]; then cat "$D/all"; cut -d' ' -f2 "$D/open"; else cat "$D/open"; fi ;;
  "pr checks") cat "$D/checks/$3" 2>/dev/null; exit 8 ;;
  "pr view")
    [ ! -f "$D/body-down" ] || exit 1
    cat "$D/body" || exit 1
    if [ -f "$D/owner-edit-after-view" ]; then
      cat "$D/owner-edit-after-view" >> "$D/body"
      rm "$D/owner-edit-after-view"
    fi
    if [ -f "$D/body-down-after-view" ]; then mv "$D/body-down-after-view" "$D/body-down"; fi ;;
  "pr create")
    cp "$body_file" "$D/body" || exit 2
    n=$(cat "$D/next"); echo $((n + 1)) > "$D/next"
    echo "$n $head" >> "$D/open"
    echo "create #$n head=$head base=$base" >> "$D/log"; echo "https://example.invalid/pull/$n" ;;
  "pr edit") cp "$body_file" "$D/body" && echo "edit #$3" >> "$D/log" ;;
  *) echo "fake gh: unexpected $*" >&2; exit 2 ;;
esac
EOF
chmod +x "$FAKE"

passed=0; failed=0
t() {  # t NAME COMMAND...: one check; the last run's output is shown when it fails
  local name=$1; shift
  if "$@"; then passed=$((passed + 1)); echo "[ok] $name"; return 0; fi
  failed=$((failed + 1)); echo "[FAIL] $name"; echo "$OUT" | sed 's/^/       | /'
}
has() { grep -qF -- "$1" <<< "$OUT"; }
lacks() { ! has "$1"; }
rc() { [ "$RC" = "$1" ]; }
logged() { grep -qF -- "$1" "$F/log"; }
unlogged() { ! logged "$1"; }
body_has() { grep -qF -- "$1" "$F/body"; }
body_lacks() { ! body_has "$1"; }
template_headings() { diff -u <(grep '^## ' "$HERE/../../.github/pull_request_template.md" | tr -d '\r') <(grep '^## ' "$F/body"); }
pushed() { [ -n "$(git -C "$O" for-each-ref "refs/heads/$1")" ]; }
unpushed() { ! pushed "$1"; }
holds() { git -C "$O" merge-base --is-ancestor "$2" "$1"; }          # holds BRANCH COMMIT-ISH
merges_only() { [ -z "$(git -C "$O" rev-list --first-parent --no-merges "main..$1")" ]; }
untouched() { [ -z "$(git -C "$W" status --porcelain)" ] && [ "$(git -C "$W" worktree list | wc -l)" = 1 ]; }

run() {  # run SCRIPT [VAR=value...]: OUT and RC
  local script=$1; shift
  OUT=$(cd "$W" && env -u CI -u FIX -u CHECK -u NEW -u SKIP -u ANY -u REQUIRED -u BL_AI_COAUTHOR GH="$FAKE" FAKE_GH="$F" REPO=test/test BL_MAIN="$W" "$@" \
          bash "tools/dev/$script" 2>&1)
  RC=$?
}
checks() {  # checks NUMBER BUCKET: the four required jobs in that state, and a job that is not required, failing
  local j
  : > "$F/checks/$1"
  for j in windows linux macos linux-sanitizers; do printf '%s\t%s\n' "$j" "$2" >> "$F/checks/$1"; done
  printf 'docs-fragments\tfail\n' >> "$F/checks/$1"
}
commit() {  # commit BRANCH FROM FILE [TEXT]: BRANCH = FROM plus one commit, pushed
  git -C "$W" checkout -q -B "$1" "$2"
  printf '%s\n' "${4:-$1}" > "$W/$3"
  git -C "$W" add -A; git -C "$W" commit -qm "$1: $3"; git -C "$W" push -q origin "$1"
}
pr() {  # pr BRANCH FROM FILE [TEXT]: that commit as a new open PR, its jobs green
  commit "$@"
  local n; n=$(cat "$F/next"); echo $((n + 1)) > "$F/next"
  echo "$n $1" >> "$F/open"; checks "$n" pass
}
fresh() {  # an origin with main, a clone with the scripts in tools/dev, no PRs
  rm -rf "$W" "$O" "$F"
  mkdir -p "$F/checks"; : > "$F/open"; : > "$F/all"; : > "$F/log"; : > "$F/body"; echo 1 > "$F/next"
  git init -q --bare -b main "$O"
  git init -q -b main "$W"
  git -C "$W" config user.name test; git -C "$W" config user.email test@example.invalid
  git -C "$W" config core.autocrlf false; git -C "$W" config commit.gpgsign false
  mkdir -p "$W/tools/dev"
  cp "$HERE/stack_lib.sh" "$HERE/stack_check.sh" "$HERE/integrate.sh" "$HERE/docs_check.sh" "$W/tools/dev/"
  echo "# Changelog" > "$W/CHANGELOG.md"; printf 'one\ntwo\nthree\n' > "$W/shared.txt"
  git -C "$W" add -A; git -C "$W" commit -qm base
  git -C "$W" remote add origin "$O"; git -C "$W" push -q origin main
}
three() {  # stack A = #1 a1 <- #2 a2, stack B = #3 b1, stack C = #4 c1
  fresh
  pr a1 main a1.txt; pr a2 a1 a2.txt; pr b1 main b1.txt; pr c1 main c1.txt
}

# --- stack_check.sh without an integration PR: as before -------------------------------------------------------------
fresh
run stack_check.sh
t "no open pull requests: nothing to check" has "no open pull requests"
three
run stack_check.sh
t "three stacks, one top each: the check passes" rc 0
t "a stack's line names its PRs and the PR that merges it" has "stack: #1 #2 -> OK: merging #2 merges all of it"
t "a lone PR is its own stack" has "stack: #3 -> OK: merging #3 merges all of it"
pr a3 a1 a3.txt
run stack_check.sh
t "a stack with two tops fails" rc 1
t "... and says which" has "stack: #1 #2 #5 -> FAIL: no single top (tops: #2 #5)"
run stack_check.sh FIX=1
t "FIX=1 merges the stray top into the stack's newest PR" has "FIXED: #5 now carries its whole stack"
run stack_check.sh
t "... after which the stack has one top" has "stack: #1 #2 #5 -> OK: merging #5 merges all of it"
touch "$F/down"
run stack_check.sh
t "gh failing is an error, not 'no open pull requests'" rc 1
rm "$F/down"

# --- integrate.sh ----------------------------------------------------------------------------------------------------
three
run integrate.sh CHECK=1
t "CHECK=1 with three green stacks: exit 0" rc 0
t "CHECK=1 says what it would merge" has "stack: #1 #2 -> would merge #2 (a2): green, no conflict"
t "CHECK=1 names the branch it would make" has "integration/1: new, from origin/main"
t "CHECK=1 lists every PR that would be carried" has "integration/1 carries #1, #2, #3 and #4"
t "CHECK=1 pushes nothing" unpushed integration/1
t "CHECK=1 opens no pull request" unlogged create
t "CHECK=1 leaves the working tree and the worktree list alone" untouched

run integrate.sh BL_AI_COAUTHOR="Test assistant <assistant@example.invalid>"
t "a real run on a local origin succeeds" rc 0
t "it pushes integration/1" pushed integration/1
t "integration/1 holds every stack's top" eval 'holds integration/1 a2 && holds integration/1 b1 && holds integration/1 c1'
t "integration/1 is main plus merge commits only" merges_only integration/1
t "it opens one PR on main from integration/1" logged "create #5 head=integration/1 base=main"
t "a fresh PR uses all five template sections" template_headings
t "the PR's body has the 'Merging this also merges' line for every PR carried" body_has "**Merging this also merges #1, #2, #3 and #4.**"
t "the real run leaves the working tree and the worktree list alone" untouched
t "the merge commits name the actual assistant" eval 'git -C "$O" log -1 --format=%B integration/1 | grep -Fxq "Co-Authored-By: Test assistant <assistant@example.invalid>"'
t "the integration PR names the actual assistant" body_has "AI assistance for this run: Test assistant."
run stack_check.sh
t "stack_check accepts the integration PR as the one top over the stacks" rc 0
t "... and says each stack is in it" has "in integration #5: merging #5 merges all of it"
t "... while each stack still has its own top" has "stack: #1 #2 -> OK: merging #2 merges all of it"
sed -i '/^## Tests$/a\
\
Owner verified the combined preview.' "$F/body"
sed -i '/^## Review$/a\
\
Independent verdict: approved for this head.' "$F/body"
run integrate.sh
t "a second run with nothing new pushes nothing" has "integration/1 is up to date: nothing pushed"
t "... and opens no second PR" eval '[ "$(grep -c create "$F/log")" = 1 ]'
t "unchanged refresh keeps the five sections and manual evidence" eval 'template_headings && body_has "Owner verified the combined preview." && body_has "Independent verdict: approved for this head."'
t "an unassisted refresh keeps previous AI attribution" body_has "AI assistance for this run: Test assistant."
checks 3 pending
run integrate.sh SKIP=3
t "SKIP of a stack already carried says it does nothing and names NEW=1" has "stack: #3 -> already in integration/1: SKIP of #3 does nothing, a carried stack cannot be taken out (NEW=1 SKIP=\"3\""
t "... and the stack stays in the branch" holds integration/1 b1
checks 3 pass

# A PR pushed on top of a carried stack: the integration PR is stale until integrate.sh runs again.
pr a3 a2 a3.txt
checks 6 pending
run stack_check.sh
t "a top pushed onto a carried stack, its jobs still running: the check passes" rc 0
t "... with a note of what the integration PR holds, lacks and waits for" has "held in part: integration #5 holds #1 and #2 but not #6 - waiting for #6 to be green (windows: pending"
t "... and no failure line" lacks "FAIL"
old=$(git -C "$O" rev-parse integration/1)
run integrate.sh
t "integrate.sh leaves the stack's new top out while it is not green" eval 'has "stack: #1 #2 #6 -> left out: #6 is not green" && [ "$(git -C "$O" rev-parse integration/1)" = "$old" ]'
t "... but still refreshes the PR summary: what is left out has changed" eval 'logged "edit #5" && body_has "#6 not green"'
t "... saying so" has "integration/1 is up to date: nothing pushed, #5's body refreshed"
checks 6 pass
run stack_check.sh
t "once that top is green and still missing from the integration PR, the check fails" rc 1
t "... naming what the integration PR holds and what it lacks" has "FAIL: integration #5 holds #1 and #2 but not #6, and #6 is green"
t "... and the command that repairs it" has "run bash tools/dev/integrate.sh"
old=$(git -C "$O" rev-parse integration/1)
run integrate.sh
t "integrate.sh continues the open integration PR's branch" has "integration/1: the open integration PR #5, continued from"
t "... with a fast-forward: the old tip is inside the new one" holds integration/1 "$old"
t "... which now holds the new top" holds integration/1 a3
t "... still merge commits only" merges_only integration/1
t "... and refreshes the original PR instead of opening another" eval 'logged "edit #5" && [ "$(grep -c create "$F/log")" = 1 ]'
t "the body's line now names the new PR too" body_has "**Merging this also merges #1, #2, #3, #4 and #6.**"
t "changed refresh keeps the five sections and manual evidence" eval 'template_headings && body_has "Owner verified the combined preview." && body_has "Independent verdict: approved for this head."'
t "a carried stack no longer appears as omitted" body_lacks "#6 not green"
run stack_check.sh
t "after which stack_check passes again" rc 0

# main moves under the integration PR.
commit main main m.txt
run integrate.sh
t "main having moved is merged into the integration branch" has "main: has moved, merged in"
t "... so the branch is up to date with main" holds integration/1 main

# NEW=1 and an older integration PR left open.
run integrate.sh NEW=1
t "NEW=1 starts the next number from main" pushed integration/2
t "... as a second PR" logged "create #7 head=integration/2 base=main"
run stack_check.sh
t "two open integration PRs: the newest counts, the older is reported" has "integration: #5 (integration/1) is superseded by #7 - close it"
t "... without failing the check" rc 0

# The number is past every integration branch there has been, closed ones too.
three
echo "integration/4" > "$F/all"; echo "task/0001-x" >> "$F/all"
run integrate.sh CHECK=1
t "the next number is past a closed integration PR's" has "integration/5: new, from origin/main"

# Which stacks are green.
three
checks 3 pending
run integrate.sh
t "a stack whose jobs are pending is left out, with the reason" has "stack: #3 -> left out: #3 is not green (windows: pending, linux: pending, macos: pending, linux-sanitizers: pending)"
t "... and is not in the branch" eval '! holds integration/1 b1'
t "... while the green stacks are" eval 'holds integration/1 a2 && holds integration/1 c1'
t "the body says what was left out" body_has "#3: #3 not green"
t "a job that is not required may fail: the other stacks merged" body_has "**Merging this also merges #1, #2 and #4.**"
run stack_check.sh
t "stack_check notes a stack the integration PR leaves out" has "not in integration #5 (left out; it merges on its own)"
t "... and passes: a whole stack may stay outside" rc 0
three
checks 3 pass; printf 'linux\tfail\n' >> "$F/checks/3"
run integrate.sh CHECK=1
t "a job listed twice (push and pull_request) with one row failing is not green" has "left out: #3 is not green (linux: fail pass)"
three
printf 'windows\tpass\nlinux\tpass\nmacos\tpass\n' > "$F/checks/4"
run integrate.sh CHECK=1
t "a required job with no result is not green" has "left out: #4 is not green (linux-sanitizers: no result)"
three
cat "$F/checks/3" "$F/checks/3" > "$F/checks/x"; mv "$F/checks/x" "$F/checks/3"
run integrate.sh CHECK=1
t "a job listed twice, every row passing, leaves no stack out" eval 'rc 0 && has "integration/1 carries #1, #2, #3 and #4" && lacks "left out"'
checks 4 pending
run integrate.sh CHECK=1 ANY=1
t "CHECK=1 ANY=1 reports the merge of a stack that is not green yet" has "stack: #4 -> would merge #4 (c1): jobs not read (ANY=1), no conflict"
run integrate.sh ANY=1
t "ANY=1 without CHECK=1 changes nothing: the stack is left out" eval 'has "stack: #4 -> left out: #4 is not green" && ! holds integration/1 c1'
three
cat "$F/checks/3" "$F/checks/3" > "$F/checks/x"; mv "$F/checks/x" "$F/checks/3"
run integrate.sh CHECK=1
t "a job listed twice, both passing, is green" has "stack: #3 -> would merge #3 (b1)"
three
checks 1 fail
run integrate.sh CHECK=1
t "only the stack's top decides: a lower PR's own jobs do not" has "stack: #1 #2 -> would merge #2 (a2)"
run integrate.sh SKIP="1 4"
t "SKIP leaves out the stack holding the PR it names" has "stack: #1 #2 -> left out: SKIP names #1"
t "... so only the rest is carried" body_has "**Merging this also merges #3.**"
three
pr a3 a1 a3.txt
run integrate.sh CHECK=1
t "a stack with no single top is left out and stack_check named" has "stack: #1 #2 #5 -> left out: no single top"
fresh
pr a1 main a1.txt; checks 1 pending
run integrate.sh
t "no green stack: nothing to integrate, nothing pushed" eval 'has "nothing to integrate" && unpushed integration/1 && unlogged create'

# Conflicts.
fresh
pr a1 main shared.txt A; pr b1 main shared.txt B; pr c1 main c1.txt
run integrate.sh
t "two PRs that change the same lines stop the run" rc 1
t "the conflict names the two PRs and the file" has "CONFLICT: #2 (b1) and #1 (a1): shared.txt"
t "... nothing is pushed" unpushed integration/1
t "... no PR is opened" unlogged create
t "... no merge is left half done: working tree clean, no extra worktree" untouched
t "... and it says so" has "stopped: nothing pushed"
t "... and where two PRs' conflict is settled: on the integration branch, or SKIP" eval 'has "Settle it on the integration branch" && has "into integration/1" && has "SKIP="'
run integrate.sh CHECK=1
t "CHECK=1 reports the conflict and exits 1" eval 'rc 1 && has "CONFLICT: #2 (b1) and #1 (a1): shared.txt"'
t "CHECK=1 goes on to the stacks after it" has "stack: #3 -> would merge #3 (c1)"
t "CHECK=1 still pushes nothing" unpushed integration/1
run integrate.sh SKIP=2
t "leaving one of the two out lets the rest through" eval 'rc 0 && body_has "**Merging this also merges #1 and #3.**"'
# The other way: the two merged by hand on the integration branch, which the next run continues.
fresh
pr a1 main shared.txt A; pr b1 main shared.txt B; pr c1 main c1.txt
git -C "$W" checkout -q -B integration/1 a1
git -C "$W" merge -q --no-ff -m "b1 by hand" b1 >/dev/null 2>&1
echo AB > "$W/shared.txt"; git -C "$W" add -A; git -C "$W" commit -qm "Merge b1 by hand"
git -C "$W" push -q origin integration/1; git -C "$W" checkout -q main
echo "4 integration/1" >> "$F/open"; echo 5 > "$F/next"
run integrate.sh
t "a conflict settled by hand on the integration branch: the next run continues it" eval 'rc 0 && has "stack: #1 -> already in integration/1" && has "stack: #2 -> already in integration/1"'
t "... adds the stack that was waiting behind the conflict" holds integration/1 c1
t "... keeps the hand-made merge" eval '[ "$(git -C "$O" show integration/1:shared.txt)" = AB ]'
t "... and the body names all three" body_has "**Merging this also merges #1, #2 and #3.**"
fresh
pr a1 main shared.txt A
commit main main shared.txt M
run integrate.sh
t "a PR that no longer merges with main names main" has "CONFLICT: #1 (a1) and main"
t "... and says to merge main into that PR's branch" has "Merge main into that pull request's branch and settle it there"
t "... and nothing is pushed" unpushed integration/1
fresh
pr a1 main shared.txt A; pr b1 main b1.txt
run integrate.sh
commit main main shared.txt M
run integrate.sh
t "main moving into a conflict with the integration branch names the PR it carries" has "main: CONFLICT: main and #3 (integration/1) - of the PRs it carries: #1 (a1): shared.txt"
t "... and stops" rc 1

# merge-tree failing for another reason than a conflict (an old git) is an error, not a conflict to name.
fresh
pr a1 main shared.txt A; pr b1 main shared.txt B
mkdir -p "$ROOT/oldgit"
cat > "$ROOT/oldgit/git" <<EOF
#!/bin/bash
# A git whose merge-tree fails (129, as one too old for --write-tree does) when asked whether two refs conflict; the rest is the real git.
if [ "\$1" = merge-tree ] && [[ " \$* " != *" --name-only "* ]]; then echo "usage: git merge-tree <base-tree> <branch1> <branch2>" >&2; exit 129; fi
exec "$(command -v git)" "\$@"
EOF
chmod +x "$ROOT/oldgit/git"
run integrate.sh PATH="$ROOT/oldgit:$PATH"
t "git merge-tree failing without a conflict is reported as an error" eval 'rc 2 && has "git merge-tree failed"'
t "... never as a conflict between pull requests" lacks "CONFLICT"
t "... and nothing is pushed" unpushed integration/1

# Legacy and hand-written bodies can be brought forward. A failed or ambiguous read must preserve both PR and tip.
three
checks 4 pending
run integrate.sh
cat > "$F/body" <<'EOF'
**Merging this also merges #1, #2 and #3.**

The integration PR (ADR 0132): every open stack whose required jobs (windows linux macos linux-sanitizers) were green when `tools/dev/integrate.sh` ran, merged from main with merge commits only. Each pull request it carries can still be merged on its own instead.
- #2 `a2` at 1234567
- #3 `b1` at 2345678

Left out:
-  #4: #4 not green (windows: pending)

AI assistance for this run: Previous assistant.

### Owner notes

Reviewed the viewport changes. Follow-up: verify the inspector at 200%.
EOF
old=$(git -C "$O" rev-parse integration/1)
checks 4 pass
touch "$F/body-down"
run integrate.sh
t "an unreadable body leaves the integration tip and description untouched" eval 'rc 1 && has "could not read #5" && unlogged "edit #5" && [ "$(git -C "$O" rev-parse integration/1)" = "$old" ]'
rm "$F/body-down"
printf '\nOwner updated the review during refresh.\n' > "$F/owner-edit-after-view"
run integrate.sh
t "a concurrent owner edit survives an advancing integration tip" eval 'rc 1 && body_has "Owner updated the review during refresh." && unlogged "edit #5" && holds integration/1 c1'
t "a concurrent edit reports that the branch advanced before the body edit was skipped" eval 'has "body changed during refresh" && has "integration/1 already advanced"'
run integrate.sh
t "a legacy description adopts the template and current stack summary" eval 'rc 0 && template_headings && body_has "**Merging this also merges #1, #2, #3 and #4.**" && body_lacks "merges #1, #2 and #3" && body_lacks "#4 not green"'
t "legacy migration retains owner notes and attribution" eval 'body_has "Reviewed the viewport changes. Follow-up: verify the inspector at 200%." && body_has "AI assistance for this run: Previous assistant."'
t "retry uses the owner note added during the previous refresh" body_has "Owner updated the review during refresh."
cp "$F/body" "$ROOT/before-refresh"
: > "$F/log"
run integrate.sh
t "repeated refresh leaves a current body byte-for-byte and skips editing" eval 'rc 0 && cmp -s "$ROOT/before-refresh" "$F/body" && unlogged "edit #5"'
sed -i '/^<!-- integrate.sh: end stack summary -->$/d' "$F/body"
cp "$F/body" "$ROOT/before-refresh"
run integrate.sh
t "an unmatched marker leaves the description untouched and reports the error" eval 'rc 1 && has "ambiguous stack summary" && cmp -s "$ROOT/before-refresh" "$F/body" && unlogged "edit #5"'

cat > "$F/body" <<'EOF'
## Task

Owner-authored reference.

## What changed

Owner-authored summary.

## Tests

- [x] Combined preview checked.

## Review

- [x] Independent review recorded.

## Documentation

- [x] Carried specs checked.
EOF
run integrate.sh
t "an existing unmarked template gains the summary and retains completed checklists" eval 'rc 0 && template_headings && body_has "**Merging this also merges #1, #2, #3 and #4.**" && body_has "- [x] Combined preview checked." && body_has "- [x] Independent review recorded." && body_has "- [x] Carried specs checked."'

: > "$F/log"
cp "$F/body" "$ROOT/before-refresh"
touch "$F/body-down-after-view"
run integrate.sh BL_AI_COAUTHOR="Test assistant <assistant@example.invalid>"
t "an unreadable final check on an unchanged tip leaves the original body intact" eval 'rc 1 && has "could not re-read #5" && has "nothing pushed" && cmp -s "$ROOT/before-refresh" "$F/body" && unlogged "edit #5"'
rm -f "$F/body-down" "$F/body-down-after-view"
printf '\n' > "$F/owner-edit-after-view"
printf '\n' >> "$ROOT/before-refresh"
run integrate.sh BL_AI_COAUTHOR="Test assistant <assistant@example.invalid>"
t "the final check detects a trailing newline added to the body" eval 'rc 1 && has "body changed during refresh" && cmp -s "$ROOT/before-refresh" "$F/body" && unlogged "edit #5"'

echo "stack self-test: $passed passed, $failed failed"
[ $failed = 0 ]

#!/bin/bash
# Self-test of stack_check.sh and integrate.sh (task 0465, ADR 0132) on a throwaway repository with a local bare
# "origin" in a temporary folder. It never reaches GitHub: GH points at a stand-in gh that answers `pr list` and
# `pr checks` from files and writes down `pr create` / `pr edit`. The `stack` CI job runs it.
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
head=; body=; base=; state=open
a=("$@")
for ((i = 0; i < $#; i++)); do
  case "${a[i]}" in
    --head) head=${a[i+1]} ;; --body) body=${a[i+1]} ;; --base) base=${a[i+1]} ;; --state) state=${a[i+1]} ;;
  esac
done
case "$1 $2" in
  "pr list")
    [ -f "$D/down" ] && exit 1
    if [ "$state" = all ]; then cat "$D/all"; cut -d' ' -f2 "$D/open"; else cat "$D/open"; fi ;;
  "pr checks") cat "$D/checks/$3" 2>/dev/null; exit 8 ;;
  "pr create")
    n=$(cat "$D/next"); echo $((n + 1)) > "$D/next"
    echo "$n $head" >> "$D/open"; printf '%s\n' "$body" > "$D/body"
    echo "create #$n head=$head base=$base" >> "$D/log"; echo "https://example.invalid/pull/$n" ;;
  "pr edit") printf '%s\n' "$body" > "$D/body"; echo "edit #$3" >> "$D/log" ;;
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
pushed() { [ -n "$(git -C "$O" for-each-ref "refs/heads/$1")" ]; }
unpushed() { ! pushed "$1"; }
holds() { git -C "$O" merge-base --is-ancestor "$2" "$1"; }          # holds BRANCH COMMIT-ISH
merges_only() { [ -z "$(git -C "$O" rev-list --first-parent --no-merges "main..$1")" ]; }
untouched() { [ -z "$(git -C "$W" status --porcelain)" ] && [ "$(git -C "$W" worktree list | wc -l)" = 1 ]; }

run() {  # run SCRIPT [VAR=value...]: OUT and RC
  local script=$1; shift
  OUT=$(cd "$W" && env -u CI -u FIX -u CHECK -u NEW -u SKIP -u ANY -u REQUIRED GH="$FAKE" FAKE_GH="$F" REPO=test/test BL_MAIN="$W" "$@" \
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

run integrate.sh
t "a real run on a local origin succeeds" rc 0
t "it pushes integration/1" pushed integration/1
t "integration/1 holds every stack's top" eval 'holds integration/1 a2 && holds integration/1 b1 && holds integration/1 c1'
t "integration/1 is main plus merge commits only" merges_only integration/1
t "it opens one PR on main from integration/1" logged "create #5 head=integration/1 base=main"
t "the PR's body has the 'Merging this also merges' line for every PR carried" body_has "**Merging this also merges #1, #2, #3 and #4.**"
t "the real run leaves the working tree and the worktree list alone" untouched
t "the merge commits carry the Co-Authored-By line" eval 'git -C "$O" log -1 --format=%B integration/1 | grep -q "^Co-Authored-By: "'
run stack_check.sh
t "stack_check accepts the integration PR as the one top over the stacks" rc 0
t "... and says each stack is in it" has "in integration #5: merging #5 merges all of it"
t "... while each stack still has its own top" has "stack: #1 #2 -> OK: merging #2 merges all of it"
run integrate.sh
t "a second run with nothing new pushes nothing" has "integration/1 is up to date: nothing pushed"
t "... and edits no PR" unlogged "edit #5"

# A PR pushed on top of a carried stack: the integration PR is stale until integrate.sh runs again.
pr a3 a2 a3.txt
run stack_check.sh
t "a stack whose top is not in the integration PR fails the check" rc 1
t "... naming what the integration PR holds and what it lacks" has "FAIL: integration #5 holds #1 and #2 but not #6"
t "... and the command that repairs it" has "run bash tools/dev/integrate.sh"
old=$(git -C "$O" rev-parse integration/1)
run integrate.sh
t "integrate.sh continues the open integration PR's branch" has "integration/1: the open integration PR #5, continued from"
t "... with a fast-forward: the old tip is inside the new one" holds integration/1 "$old"
t "... which now holds the new top" holds integration/1 a3
t "... still merge commits only" merges_only integration/1
t "... and rewrites the PR's body instead of opening another" eval 'logged "edit #5" && [ "$(grep -c create "$F/log")" = 1 ]'
t "the body's line now names the new PR too" body_has "**Merging this also merges #1, #2, #3, #4 and #6.**"
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
run integrate.sh CHECK=1
t "CHECK=1 reports the conflict and exits 1" eval 'rc 1 && has "CONFLICT: #2 (b1) and #1 (a1): shared.txt"'
t "CHECK=1 goes on to the stacks after it" has "stack: #3 -> would merge #3 (c1)"
t "CHECK=1 still pushes nothing" unpushed integration/1
run integrate.sh SKIP=2
t "leaving one of the two out lets the rest through" eval 'rc 0 && body_has "**Merging this also merges #1 and #3.**"'
fresh
pr a1 main shared.txt A
commit main main shared.txt M
run integrate.sh
t "a PR that no longer merges with main names main" has "CONFLICT: #1 (a1) and main"
t "... and nothing is pushed" unpushed integration/1
fresh
pr a1 main shared.txt A; pr b1 main b1.txt
run integrate.sh
commit main main shared.txt M
run integrate.sh
t "main moving into a conflict with the integration branch names the PR it carries" has "main: CONFLICT: main and #3 (integration/1) - of the PRs it carries: #1 (a1): shared.txt"
t "... and stops" rc 1

echo "stack self-test: $passed passed, $failed failed"
[ $failed = 0 ]

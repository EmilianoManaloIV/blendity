#!/bin/bash
# The integration PR (ADR 0132, task 0465): one pull request on main that carries every open stack whose required
# jobs are green, so the user merges once instead of once a stack with a rebuild of the others in between. From
# origin/main it merges each such stack's top branch - merge commits only, nothing rebased or squashed - pushes
# `integration/<n>` and opens a PR on main whose body has the "Merging this also merges #..." line for every PR it
# carries. While an integration PR is open, a later run continues its branch (main merged in when main has moved,
# then the tops it lacks; a fast-forward push, the PR's body rewritten); NEW=1 starts the next number from main.
# The merges are made with `git merge-tree`, in the object store: no working tree is touched, so a conflict leaves
# nothing half merged. A conflict stops the run before anything is pushed and names the two pull requests.
# Every PR still targets main and can still be merged on its own; this script never merges a PR.
# Usage: bash tools/dev/integrate.sh            (build or update the integration PR)
#        CHECK=1 bash tools/dev/integrate.sh    (only report what would be merged and what conflicts; exit 1 on a conflict)
#        CHECK=1 ANY=1 bash tools/dev/integrate.sh   (the same report as if every stack were green: what conflicts today)
#        SKIP="372 373" ...                     (leave out the stacks holding these PRs)
#        NEW=1 ...                              (a fresh integration/<n+1> from main, not the open one)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)   # before the cd: from the libraries clone the toplevel is blender-main/
. "$HERE/stack_lib.sh"
cd "${BL_MAIN:-$(git rev-parse --show-toplevel)}"
stack_load
[ $COUNT -eq 0 ] && { echo "no open pull requests to integrate"; exit 0; }
stack_group

# green NUMBER: the pull request's required jobs all passed (pr_green; WHY says what is missing). CHECK=1 ANY=1 does
# not read them.
green() {
  WHY=
  if [ "$CHECK" = "1" ] && [ "$ANY" = "1" ]; then return 0; fi
  pr_green "$1"
}

# try_merge REF MESSAGE: $tip becomes a merge commit of $tip and REF. 1 on a conflict, with FILES the paths in it.
try_merge() {
  local out rc=0
  out=$(git merge-tree --write-tree --name-only --no-messages "$tip" "$1") || rc=$?
  if [ $rc = 0 ]; then
    tip=$(git commit-tree "$(echo "$out" | head -1)" -p "$tip" -p "$1" -m "$2

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>")
    return 0
  fi
  [ $rc = 1 ] || { echo "integrate: git merge-tree failed on $1" >&2; exit 2; }
  FILES=$(echo "$out" | tail -n +2 | tr '\n' ' ')
  return 1
}

# conflicts_with A B: the two do not merge on their own. merge-tree exits 1 for a conflict; anything else it fails
# with (an old git without --write-tree, a bad ref) is an error, not a conflict to name.
conflicts_with() {
  local rc=0
  git merge-tree --write-tree --no-messages "$1" "$2" >/dev/null 2>&1 || rc=$?
  [ $rc = 0 ] && return 1
  [ $rc = 1 ] && return 0
  echo "integrate: git merge-tree failed on $1 and $2 (exit $rc; it needs git 2.38 or newer)" >&2
  exit 2
}

# Which branch: the open integration PR's (the newest), or the next number - past every integration branch there
# has been, open, merged or closed.
CUR=
if [ ${#IN[@]} -gt 0 ] && [ "$NEW" != "1" ]; then
  CUR=${IN[-1]}; BRANCH=${IB[-1]}
  tip=$(git rev-parse "origin/$BRANCH")
  echo "$BRANCH: the open integration PR #$CUR, continued from $(git rev-parse --short "$tip")"
else
  past=$("$GH" pr list --repo "$REPO" --state all --limit 1000 --json headRefName --jq '.[].headRefName') ||
    { echo "integrate: could not list the pull requests ($GH pr list failed)" >&2; exit 1; }
  max=0
  for n in $(echo "$past" | tr -d '\r') $(git for-each-ref --format='%(refname:lstrip=3)' refs/remotes/origin/integration/); do
    k=${n#integration/}
    if [ "$k" != "$n" ] && [[ $k =~ ^[0-9]+$ ]] && [ "$k" -gt "$max" ]; then max=$k; fi
  done
  BRANCH=integration/$((max + 1))
  tip=$(git rev-parse origin/main)
  echo "$BRANCH: new, from origin/main $(git rev-parse --short "$tip")"
fi
start=$tip

# stop WHERE: a conflict ends a real run here (CHECK=1 goes on, to report the rest). WHERE says who settles it: two
# pull requests that are not on main can only be settled where both are - on the integration branch, by the later
# pass's worker (ADR 0132) - while a pull request that no longer merges with main settles that on its own branch.
conflicts=0
stop() {
  conflicts=1
  [ "$CHECK" = "1" ] && return 0
  if [ "$1" = main ]; then
    how="Merge main into that pull request's branch and settle it there"
  else
    how="Settle it on the integration branch: the later pass's worker merges the two by hand into $BRANCH (a merge commit, pushed; a pull request on main opened for it if it has none), then run this again"
  fi
  echo "stopped: nothing pushed, no pull request opened or changed. $how - or leave one out with SKIP=\"<number>\"."
  exit 1
}

if ! git merge-base --is-ancestor origin/main "$tip"; then
  if try_merge origin/main "Merge main into $BRANCH"; then
    echo "main: has moved, merged in"
  else
    with=
    for ((i = 0; i < COUNT; i++)); do
      git merge-base --is-ancestor "origin/${B[i]}" "$tip" || continue
      if conflicts_with origin/main "origin/${B[i]}"; then with="$with #${N[i]} (${B[i]})"; fi
    done
    echo "main: CONFLICT: main and #$CUR ($BRANCH)${with:+ - of the PRs it carries:$with}: $FILES"
    stop
  fi
fi

merged=(); left=()
for ((root = 0; root < COUNT; root++)); do
  [ "$(stack_root $root)" != "$root" ] && continue
  stack_of $root
  if [ ${#TOPS[@]} -ne 1 ]; then
    echo "stack:$LIST -> left out: no single top (run bash tools/dev/stack_check.sh)"
    left+=("$LIST: no single top")
    continue
  fi
  t=${TOPS[0]}
  skipped=
  for i in "${MEMBERS[@]}"; do
    case " $SKIP " in *" ${N[i]} "*) skipped=${N[i]} ;; esac
  done
  if git merge-base --is-ancestor "origin/${B[t]}" "$tip"; then
    # The branch only moves forward, so a stack it carries cannot be taken out again: SKIP says so instead of
    # passing for done.
    if [ -n "$skipped" ]; then
      echo "stack:$LIST -> already in $BRANCH: SKIP of #$skipped does nothing, a carried stack cannot be taken out (NEW=1 SKIP=\"$skipped\" starts a new integration branch without it)"
    else
      echo "stack:$LIST -> already in $BRANCH"
    fi
    merged+=($t)
    continue
  fi
  if [ -n "$skipped" ]; then
    echo "stack:$LIST -> left out: SKIP names #$skipped"
    left+=("$LIST: left out by hand")
    continue
  fi
  if ! green "${N[t]}"; then
    echo "stack:$LIST -> left out: #${N[t]} is not green ($WHY)"
    left+=("$LIST: #${N[t]} not green ($WHY)")
    continue
  fi
  if try_merge "origin/${B[t]}" "Merge ${B[t]} (#${N[t]}) into $BRANCH"; then
    if [ "$CHECK" = "1" ]; then word="would merge"; else word="merged"; fi
    if [ "$ANY" = "1" ] && [ "$CHECK" = "1" ]; then state="jobs not read (ANY=1)"; else state=green; fi
    echo "stack:$LIST -> $word #${N[t]} (${B[t]}): $state, no conflict"
    merged+=($t)
    continue
  fi
  # Name the other side: main when the branch no longer merges with main itself, else the first PR already in that
  # it does not merge with, else all of them together.
  other=
  if conflicts_with origin/main "origin/${B[t]}"; then other="main (it has moved under the branch)"; fi
  for m in "${merged[@]}"; do
    [ -n "$other" ] && break
    if conflicts_with "origin/${B[m]}" "origin/${B[t]}"; then other="#${N[m]} (${B[m]})"; fi
  done
  echo "stack:$LIST -> CONFLICT: #${N[t]} (${B[t]}) and ${other:-the pull requests merged before it, taken together}: $FILES"
  left+=("$LIST: #${N[t]} conflicts with ${other:-the others}")
  case "$other" in main*) stop main ;; *) stop ;; esac
done

carried=()
for ((i = 0; i < COUNT; i++)); do
  if git merge-base --is-ancestor "origin/${B[i]}" "$tip"; then carried+=("${N[i]}"); fi
done
if [ ${#carried[@]} -eq 0 ]; then echo "nothing to integrate: no stack is green"; exit $conflicts; fi
echo "$BRANCH carries $(pr_join "${carried[@]}")"
if [ "$CHECK" = "1" ]; then
  echo "CHECK=1: nothing pushed, no pull request opened or changed"
  exit $conflicts
fi
body="**Merging this also merges $(pr_join "${carried[@]}").**

The integration PR (ADR 0132): every open stack whose required jobs ($REQUIRED) were green when \`tools/dev/integrate.sh\` ran, merged from main with merge commits only. Each pull request it carries can still be merged on its own instead."
for m in "${merged[@]}"; do
  body="$body
- #${N[m]} \`${B[m]}\` at $(git rev-parse --short "origin/${B[m]}")"
done
if [ ${#left[@]} -gt 0 ]; then
  body="$body

Left out:"
  for l in "${left[@]}"; do
    body="$body
- $l"
  done
fi
# Nothing new to push: the body is still rewritten, since what is left out and why may have changed.
if [ "$tip" = "$start" ]; then
  if [ -n "$CUR" ]; then
    "$GH" pr edit "$CUR" --repo "$REPO" --body "$body" >/dev/null
    echo "$BRANCH is up to date: nothing pushed, #$CUR's body refreshed"
  else
    echo "$BRANCH is up to date: nothing pushed"
  fi
  exit 0
fi
git push -q origin "$tip:refs/heads/$BRANCH"
if [ -n "$CUR" ]; then
  "$GH" pr edit "$CUR" --repo "$REPO" --body "$body" >/dev/null
  echo "updated #$CUR ($BRANCH at $(git rev-parse --short "$tip"))"
else
  "$GH" pr create --repo "$REPO" --head "$BRANCH" --base main --title "Integration ${BRANCH#integration/}: the green stacks in one merge" --body "$body"
fi
echo "now run bash tools/dev/stack_check.sh"

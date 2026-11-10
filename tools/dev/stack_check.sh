#!/bin/bash
# The stack guarantee (the user, 2026-11-08): merging the top pull request of a stack merges every pull request below
# it. Several stacks may be open at once (one per pass, the user 2026-11-08): open PRs that share commits not yet on
# main form one stack, and each stack must have exactly one top - a PR whose history holds every other PR of its stack.
# This lists each stack and any PR its top lacks; with FIX=1 it merges the stack's other heads into its newest PR's
# branch (a merge commit, pushed) so the guarantee holds again. Run it after every publish and every push to a PR
# branch; the `stack` CI job runs it too (CI=1: check only).
# Usage: bash tools/dev/stack_check.sh          (check only; exit 1 when a stack has more than one top)
#        FIX=1 bash tools/dev/stack_check.sh    (also repair)
set -e
REPO=${REPO:-EmilianoManaloIV/blendity}
GH=gh
command -v gh >/dev/null 2>&1 || GH="/c/Program Files/GitHub CLI/gh.exe"
HERE=$(cd "$(dirname "$0")" && pwd)   # before the cd: from the libraries clone the toplevel is blender-main/
cd "${BL_MAIN:-$(git rev-parse --show-toplevel)}"
git fetch -q origin '+refs/heads/*:refs/remotes/origin/*'

mapfile -t ROWS < <("$GH" pr list --repo "$REPO" --state open --base main --json number,headRefName \
                     --jq 'sort_by(.number) | .[] | "\(.number) \(.headRefName)"')
[ ${#ROWS[@]} -eq 0 ] && { echo "no open pull requests"; exit 0; }
N=(); B=()
for r in "${ROWS[@]}"; do N+=("${r%% *}"); B+=("${r#* }"); done
COUNT=${#N[@]}

# Group: two PRs are in one stack when their merge base is not on main (they share unmerged commits).
G=(); for ((i = 0; i < COUNT; i++)); do G[i]=$i; done
find() { local x=$1; while [ "${G[x]}" != "$x" ]; do x=${G[x]}; done; echo "$x"; }
for ((i = 0; i < COUNT; i++)); do
  for ((j = i + 1; j < COUNT; j++)); do
    mb=$(git merge-base "origin/${B[i]}" "origin/${B[j]}" 2>/dev/null) || continue
    git merge-base --is-ancestor "$mb" origin/main && continue
    a=$(find $i); b=$(find $j); [ "$a" != "$b" ] && G[b]=$a
  done
done

fail=0
for ((root = 0; root < COUNT; root++)); do
  [ "$(find $root)" != "$root" ] && continue
  members=(); for ((i = 0; i < COUNT; i++)); do [ "$(find $i)" = "$root" ] && members+=($i); done
  tops=()
  for i in "${members[@]}"; do
    inside=0
    for j in "${members[@]}"; do
      [ "$i" = "$j" ] && continue
      git merge-base --is-ancestor "origin/${B[i]}" "origin/${B[j]}" && { inside=1; break; }
    done
    [ $inside = 0 ] && tops+=($i)
  done
  list=""; for i in "${members[@]}"; do list="$list #${N[i]}"; done
  if [ ${#tops[@]} -eq 1 ]; then
    echo "stack:$list -> OK: merging #${N[${tops[0]}]} merges all of it"
    continue
  fi
  newest=${members[-1]}
  echo "stack:$list -> FAIL: no single top (tops:$(for t in "${tops[@]}"; do printf ' #%s' "${N[t]}"; done))"
  if [ "$FIX" != "1" ] || [ -n "$CI" ]; then fail=1; continue; fi
  T=$(mktemp -d)
  git worktree add -q --detach "$T" "origin/${B[newest]}"
  for t in "${tops[@]}"; do
    [ "$t" = "$newest" ] && continue
    git -C "$T" merge --no-ff --no-edit -m "Merge ${B[t]} so #${N[newest]} carries every PR of its stack

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" "origin/${B[t]}" || {
      echo "CONFLICT merging ${B[t]} - resolve by hand in $T"; exit 1; }
  done
  git -C "$T" push -q origin "HEAD:refs/heads/${B[newest]}"
  git worktree remove --force "$T"
  echo "FIXED: #${N[newest]} now carries its whole stack"
done
# Changelog fragments (ADR 0123): a PR whose own commits edit CHANGELOG.md / README.md outside a fold conflicts with the
# other passes' PRs. Reported here only; the `docs-fragments` CI job is the one that fails the PR.
if [ -z "$CI" ]; then
  for ((i = 0; i < COUNT; i++)); do
    out=$(NO_FRAGMENTS=1 bash "$HERE/docs_check.sh" origin/main "origin/${B[i]}" 2>&1) ||
      echo "docs: #${N[i]}: $(echo "$out" | grep -m1 '::error::' | sed 's/^::error:://')"
  done
fi
[ $fail = 1 ] && echo "run FIX=1 bash tools/dev/stack_check.sh"
exit $fail

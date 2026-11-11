#!/bin/bash
# The stack guarantee (the user, 2026-11-08): merging the top pull request of a stack merges every pull request below
# it. Several stacks may be open at once (one per pass, the user 2026-11-08): open PRs that share commits not yet on
# main form one stack, and each stack must have exactly one top - a PR whose history holds every other PR of its stack.
# This lists each stack and any PR its top lacks; with FIX=1 it merges the stack's other heads into its newest PR's
# branch (a merge commit, pushed) so the guarantee holds again. Run it after every publish and every push to a PR
# branch; the `stack` CI job runs it too (CI=1: check only).
# An integration PR (head `integration/<n>`, made by integrate.sh, ADR 0132) is the one allowed top over several
# stacks: it is not a stack of its own, and every stack it carries must be in it up to that stack's own top - a stack
# it holds only part of fails (a PR was pushed after it was built: run integrate.sh again). A stack it holds nothing
# of is only noted: integrate.sh leaves out a stack that is not green. The newest open integration PR is the one that
# counts; an older one still open is reported as superseded. FIX=1 never touches an integration branch.
# Usage: bash tools/dev/stack_check.sh          (check only; exit 1 when a stack has more than one top)
#        FIX=1 bash tools/dev/stack_check.sh    (also repair)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)   # before the cd: from the libraries clone the toplevel is blender-main/
. "$HERE/stack_lib.sh"
cd "${BL_MAIN:-$(git rev-parse --show-toplevel)}"
stack_load
[ $COUNT -eq 0 ] && [ ${#IN[@]} -eq 0 ] && { echo "no open pull requests"; exit 0; }
stack_group

INT=; INTB=
if [ ${#IN[@]} -gt 0 ]; then
  INT=${IN[-1]}; INTB=${IB[-1]}
  for ((k = 0; k < ${#IN[@]} - 1; k++)); do
    echo "integration: #${IN[k]} (${IB[k]}) is superseded by #$INT - close it"
  done
fi

# carried ROOT's stack against the integration PR: all of it inside, none of it, or a part (the failure).
integration_holds() {
  local i in=() out=()
  for i in "${MEMBERS[@]}"; do
    if git merge-base --is-ancestor "origin/${B[i]}" "origin/$INTB"; then in+=("${N[i]}"); else out+=("${N[i]}"); fi
  done
  if [ ${#out[@]} -eq 0 ]; then echo "  in integration #$INT: merging #$INT merges all of it"; return 0; fi
  if [ ${#in[@]} -eq 0 ]; then echo "  not in integration #$INT (left out; it merges on its own)"; return 0; fi
  echo "  FAIL: integration #$INT holds $(pr_join "${in[@]}") but not $(pr_join "${out[@]}")"
  return 1
}

fail=0; stale=0
for ((root = 0; root < COUNT; root++)); do
  [ "$(stack_root $root)" != "$root" ] && continue
  stack_of $root
  if [ ${#TOPS[@]} -eq 1 ]; then
    echo "stack:$LIST -> OK: merging #${N[${TOPS[0]}]} merges all of it"
    if [ -n "$INT" ]; then integration_holds || stale=1; fi
    continue
  fi
  newest=${MEMBERS[-1]}
  echo "stack:$LIST -> FAIL: no single top (tops:$(for t in "${TOPS[@]}"; do printf ' #%s' "${N[t]}"; done))"
  if [ "$FIX" != "1" ] || [ -n "$CI" ]; then fail=1; continue; fi
  T=$(mktemp -d)
  git worktree add -q --detach "$T" "origin/${B[newest]}"
  for t in "${TOPS[@]}"; do
    [ "$t" = "$newest" ] && continue
    git -C "$T" merge --no-ff --no-edit -m "Merge ${B[t]} so #${N[newest]} carries every PR of its stack

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" "origin/${B[t]}" || {
      echo "CONFLICT merging ${B[t]} - resolve by hand in $T"; exit 1; }
  done
  git -C "$T" push -q origin "HEAD:refs/heads/${B[newest]}"
  git worktree remove --force "$T"
  echo "FIXED: #${N[newest]} now carries its whole stack"
  if [ -n "$INT" ]; then integration_holds || stale=1; fi
done
# Changelog fragments (ADR 0123): a PR whose own commits edit CHANGELOG.md / README.md / the shared docs outside a fold
# conflicts with the other passes' PRs. Reported here only; the `docs-fragments` CI job is the one that fails the PR.
if [ -z "$CI" ]; then
  for ((i = 0; i < COUNT; i++)); do
    out=$(NO_FRAGMENTS=1 bash "$HERE/docs_check.sh" origin/main "origin/${B[i]}" 2>&1) ||
      echo "docs: #${N[i]}: $(echo "$out" | grep -m1 '::error::' | sed 's/^::error:://')"
  done
fi
if [ $fail = 1 ]; then echo "run FIX=1 bash tools/dev/stack_check.sh"; fi
if [ $stale = 1 ]; then echo "run bash tools/dev/integrate.sh (it brings integration #$INT up to its stacks' tops)"; fail=1; fi
exit $fail

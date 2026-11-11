#!/bin/bash
# What stack_check.sh and integrate.sh share (sourced, not run): the open pull requests on main and their grouping
# into stacks. Two PRs are in one stack when they share commits that are not on main yet; a stack's top is a PR whose
# history no other PR of the stack holds. An integration PR (head `integration/<n>`, ADR 0132) is not a member of any
# stack: it would join them all, so it is listed apart.
# GH names the gh to call (the self-test puts a stand-in there, so no test ever reaches GitHub); REPO the repository.
REPO=${REPO:-EmilianoManaloIV/blendity}
if [ -z "$GH" ]; then
  GH=gh
  command -v gh >/dev/null 2>&1 || GH="/c/Program Files/GitHub CLI/gh.exe"
fi

# stack_load: fetches origin and reads the open PRs on main, oldest first. N[i] / B[i]: number and branch of the
# ordinary ones (COUNT of them); IN[k] / IB[k]: the integration PRs.
stack_load() {
  local rows r
  git fetch -q origin '+refs/heads/*:refs/remotes/origin/*'
  rows=$("$GH" pr list --repo "$REPO" --state open --base main --limit 200 --json number,headRefName \
           --jq 'sort_by(.number) | .[] | "\(.number) \(.headRefName)"') ||
    { echo "stack: could not list the open pull requests ($GH pr list failed)" >&2; return 1; }
  N=(); B=(); IN=(); IB=()
  while IFS= read -r r; do
    r=${r%$'\r'}
    [ -z "$r" ] && continue
    case "${r#* }" in
      integration/*) IN+=("${r%% *}"); IB+=("${r#* }") ;;
      *) N+=("${r%% *}"); B+=("${r#* }") ;;
    esac
  done <<< "$rows"
  COUNT=${#N[@]}
  return 0
}

# stack_root I: the index that stands for I's stack (after stack_group).
stack_root() {
  local x=$1
  while [ "${G[x]}" != "$x" ]; do x=${G[x]}; done
  echo "$x"
}

# stack_group: two PRs are in one stack when their merge base is not on main (they share unmerged commits).
stack_group() {
  local i j mb a b
  G=()
  for ((i = 0; i < COUNT; i++)); do G[i]=$i; done
  for ((i = 0; i < COUNT; i++)); do
    for ((j = i + 1; j < COUNT; j++)); do
      mb=$(git merge-base "origin/${B[i]}" "origin/${B[j]}" 2>/dev/null) || continue
      git merge-base --is-ancestor "$mb" origin/main && continue
      a=$(stack_root $i); b=$(stack_root $j)
      if [ "$a" != "$b" ]; then G[b]=$a; fi
    done
  done
  return 0
}

# stack_of ROOT: MEMBERS = the indexes of that stack's PRs (oldest first), TOPS = those no other member holds,
# LIST = " #a #b" for messages.
stack_of() {
  local i j inside
  MEMBERS=(); TOPS=(); LIST=""
  for ((i = 0; i < COUNT; i++)); do
    if [ "$(stack_root $i)" = "$1" ]; then MEMBERS+=($i); LIST="$LIST #${N[i]}"; fi
  done
  for i in "${MEMBERS[@]}"; do
    inside=0
    for j in "${MEMBERS[@]}"; do
      [ "$i" = "$j" ] && continue
      git merge-base --is-ancestor "origin/${B[i]}" "origin/${B[j]}" && { inside=1; break; }
    done
    if [ $inside = 0 ]; then TOPS+=($i); fi
  done
  return 0
}

# pr_join 1 2 3: "#1, #2 and #3".
pr_join() {
  local out="" n=$# i=0 a
  for a in "$@"; do
    i=$((i + 1))
    if [ $i = 1 ]; then out="#$a"; elif [ $i = "$n" ]; then out="$out and #$a"; else out="$out, #$a"; fi
  done
  echo "$out"
}

#!/bin/bash
# The stack guarantee (the user, 2026-11-08): merging the newest open pull request merges every open pull request
# below it. Every PR targets main, so that holds exactly when each open PR's head commit is in the newest PR's
# history. This lists the ones that aren't; with FIX=1 it merges them into the newest PR's branch (a merge commit,
# pushed) so the guarantee holds again. Run it after every publish and every push to a PR branch.
# Usage: bash tools/dev/stack_check.sh          (check only; exit 1 when a PR is missing)
#        FIX=1 bash tools/dev/stack_check.sh    (also repair)
set -e
REPO=${REPO:-EmilianoManaloIV/blendity}
GH=gh
command -v gh >/dev/null 2>&1 || GH="/c/Program Files/GitHub CLI/gh.exe"
cd "${BL_MAIN:-C:/Users/trick/Downloads/blender-main}"
git fetch -q origin

PRS=$("$GH" pr list --repo "$REPO" --state open --base main --json number,headRefName --jq 'sort_by(.number) | .[] | "\(.number) \(.headRefName)"')
[ -z "$PRS" ] && { echo "no open pull requests"; exit 0; }
TOP=$(echo "$PRS" | tail -1)
TOP_N=${TOP%% *}
TOP_B=${TOP#* }

missing=""
while read -r n b; do
  [ "$n" = "$TOP_N" ] && continue
  if git merge-base --is-ancestor "origin/$b" "origin/$TOP_B"; then
    echo "#$n ($b): merges with #$TOP_N"
    continue
  fi
  echo "#$n ($b): NOT in #$TOP_N's history"
  missing="$missing $b"
done <<< "$PRS"

[ -z "$missing" ] && { echo "OK: merging #$TOP_N merges every open PR"; exit 0; }
[ "$FIX" != "1" ] && { echo "FAIL: run FIX=1 bash tools/dev/stack_check.sh"; exit 1; }

T=$(mktemp -d)
git worktree add -q --detach "$T" "origin/$TOP_B"
for b in $missing; do
  git -C "$T" merge --no-ff --no-edit -m "Merge $b so #$TOP_N carries every open PR

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" "origin/$b" || {
    echo "CONFLICT merging $b - resolve by hand in $T"; exit 1; }
done
git -C "$T" push -q origin "HEAD:refs/heads/$TOP_B"
git worktree remove --force "$T"
echo "FIXED: #$TOP_N now carries every open PR"

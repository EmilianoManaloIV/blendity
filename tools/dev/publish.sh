#!/bin/bash
# Commits and publishes the Blendity subtree from the original development checkout.
# Usage: BRANCH=task/NNNN-x BASE=task/MMMM-y MSG=commit.txt PR=pr.md TITLE="..." bash tools/dev/publish.sh
set -e

M=${BL_MAIN:-C:/Users/trick/Downloads/blender-main/blendity}
GH=${GH:-gh}
case "$BRANCH" in
  task/?*) ;;
  *) echo "REFUSED: publish.sh requires a task/* branch" >&2; exit 1 ;;
esac

cd "$M"
project=$(pwd -P)
root=$(git rev-parse --show-toplevel)
expected=$(cd "$root/blendity" && pwd -P)
if [ "$project" != "$expected" ]; then
  echo "REFUSED: publish.sh requires the original checkout's blendity/ directory" >&2
  exit 1
fi
cd "$root"
git check-ref-format "refs/heads/$BRANCH"

# Stage and check a disposable index. Hold the normal index lock so another Git
# command cannot change the real index between reading it and installing the commit's index.
index=$(git rev-parse --path-format=absolute --git-path index)
lock="$index.lock"
staging=$(mktemp -d "$(dirname -- "$index")/publish.XXXXXX")
candidate="$staging/index"
owns_lock=0
cleanup() {
  if [ "$owns_lock" = 1 ]; then rm -f -- "$lock"; fi
  rm -f -- "$candidate" "$candidate.lock" "$staging/diff" "$staging/machine-local"
  rmdir -- "$staging"
}
trap cleanup EXIT
if ! (set -o noclobber; : > "$lock") 2>/dev/null; then
  echo "REFUSED: the publication index is locked" >&2
  exit 1
fi
owns_lock=1
if [ -f "$index" ]; then cp -- "$index" "$candidate"; fi
export GIT_INDEX_FILE="$candidate"

if ! git diff --cached --quiet -- ':(top,icase)blendity/Assets' ':(top,icase)blendity/Renders' \
  ':(top,icase)blendity/Screenshots' ':(top,icase)blendity/research'; then
  echo "REFUSED: owner files are staged or the index could not be checked" >&2
  exit 1
fi
if ! git diff --cached --quiet -- . ':(exclude,top)blendity'; then
  echo "REFUSED: changes outside the Blendity subtree are staged" >&2
  exit 1
fi

allowed=(blendity/.gitignore blendity/.codex/agents blendity/.codex/rules blendity/AGENTS.md
  blendity/CLAUDE.md blendity/CREDITS.md blendity/.github blendity/src blendity/tests blendity/docs
  blendity/stress blendity/tools blendity/CHANGELOG.md blendity/README.md blendity/changelog.d
  blendity/bridges blendity/packaging)
paths=()
for path in "${allowed[@]}"; do
  if [ -e "$path" ] || [ -L "$path" ]; then
    paths+=("$path")
  else
    tracked=$(git ls-files -- "$path")
    if [ -n "$tracked" ]; then paths+=("$path"); fi
  fi
done
if [ "${#paths[@]}" = 0 ]; then
  echo "REFUSED: no eligible publication paths" >&2
  exit 1
fi
git add -A -- "${paths[@]}"

# Inspect the whole outgoing subtree, including unchanged entries already tracked.
# Remove missing local entries from the candidate without reading their file contents.
machine_local=()
omit_local=()
for path in blendity/.claude/settings.local.json blendity/.claude/scheduled_tasks.lock blendity/.claude/worktrees; do
  machine_local+=(":(top,icase)$path")
  omit_local+=(":(exclude,top,icase)$path")
done
git ls-files --cached -z -- "${machine_local[@]}" > "$staging/machine-local"
while IFS= read -r -d '' path; do
  if [ -e "$path" ] || [ -L "$path" ]; then
    echo "REFUSED: machine-local Claude files remain in the publication candidate" >&2
    exit 1
  fi
  git update-index --force-remove -- "$path"
done < "$staging/machine-local"
remaining=$(git ls-files --cached -- "${machine_local[@]}")
if [ -n "$remaining" ]; then
  echo "REFUSED: machine-local Claude paths remain in the publication index" >&2
  exit 1
fi

git diff --cached -- blendity "${omit_local[@]}" > "$staging/diff"
if grep -qE '^\+.*(EXPERIMENT|BLENDITY_EXP_)' "$staging/diff"; then
  echo "REFUSED: an experiment hook is staged" >&2
  exit 1
elif [ "$?" -ne 1 ]; then
  echo "REFUSED: the staged experiment check failed" >&2
  exit 1
fi
reviewed_tree=$(git write-tree)
git commit -q -F "$MSG"
commit=$(git rev-parse --verify 'HEAD^{commit}')
committed_tree=$(git rev-parse --verify "$commit^{tree}")
if [ "$committed_tree" != "$reviewed_tree" ]; then
  echo "REFUSED: the local commit differs from the checked publication candidate" >&2
  exit 1
fi
T=$(git rev-parse --verify "$commit:blendity")
mv -f -- "$candidate" "$index"
rm -f -- "$lock"
owns_lock=0
unset GIT_INDEX_FILE

git fetch -q origin "$BASE"
C=$(git commit-tree "$T" -p "origin/$BASE" -F "$MSG")
git push -q origin "$C:refs/heads/$BRANCH"
"$GH" pr create --base main --head "$BRANCH" --title "$TITLE" --body-file "$PR"

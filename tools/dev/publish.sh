#!/bin/bash
# A round's pull request (tools/dev/README.md): commits the libraries clone's tree on its local branch, then the
# blendity/ subtree onto BASE on GitHub as BRANCH, and opens the pull request.
# Usage: BRANCH=task/NNNN-x BASE=task/MMMM-y MSG=commit.txt PR=pr.md TITLE="..." bash tools/dev/publish.sh
set -e
M=${BL_MAIN:-C:/Users/trick/Downloads/blender-main/blendity}
cd "$M/.."
git add -A blendity/.github blendity/src blendity/tests blendity/docs blendity/stress blendity/tools blendity/CHANGELOG.md blendity/README.md blendity/bridges blendity/packaging 2>/dev/null || true
git status --short blendity | grep -v "^??" | head -40
if git diff --cached -- blendity | grep -E "^\+.*(EXPERIMENT|BLENDITY_EXP_)" ; then echo "REFUSED: an experiment hook is staged"; git reset -q; exit 1; fi
git commit -q -F "$MSG"
T=$(git rev-parse HEAD:blendity)
git fetch -q origin "$BASE"
C=$(git commit-tree "$T" -p "origin/$BASE" -F "$MSG")
git push -q origin "$C:refs/heads/$BRANCH"
gh pr create --base "$BASE" --head "$BRANCH" --title "$TITLE" --body-file "$PR"

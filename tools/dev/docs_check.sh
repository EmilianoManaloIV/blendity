#!/bin/bash
# Changelog fragments (tasks 0443 and 0465, ADR 0123): a round never edits CHANGELOG.md, README.md or the shared docs
# (SHARED_DOCS below), it adds changelog.d/<round>-<slug>.md, so the PRs of passes running in parallel never conflict
# there. This fails when one of the branch's own commits (BASE..HEAD, merge commits skipped) edits one of those files
# without a "Docs-fold: yes" trailer - the fold of a pass write-up (tools/dev/changelog.py fold prints the trailer) -
# and checks every fragment of the working tree is well formed (NO_FRAGMENTS=1 skips that, for refs that aren't the
# working tree). The `docs-fragments` CI job runs it on every PR. BASE and HEAD must be published trees (blendity at
# the root, as origin/* and task/* are): the libraries clone's local `blendity` branch keeps the files under blendity/
# and is refused, not passed.
# Usage: bash tools/dev/docs_check.sh [BASE [HEAD]]     (default origin/main HEAD)
set -e
cd "$(dirname "$0")/../.."
BASE=${1:-origin/main}
HEAD=${2:-HEAD}
for ref in "$BASE" "$HEAD"; do
  git rev-parse --verify -q "$ref^{commit}" >/dev/null || { echo "::error::docs_check: no commit '$ref'"; exit 1; }
done
# The shared docs joined the rule in round 490 (task 0465; tools/dev/changelog.py holds the same list). A commit
# written before that reached its own history may still edit them: SHARED_RULE is the oldest commit on BASE in which
# this script gained the list, and a commit that does not descend from it is exempt for these four files. A commit
# made after its branch merged main is checked. No such commit on BASE (the round that adds the list): none is exempt.
SHARED_DOCS="docs/USABILITY.md tools/README.md docs/ARCHITECTURE.md docs/PERFORMANCE.md"
SHARED_RULE=$(git log -S'SHARED_DOCS=' --format=%H "$BASE" -- ':(top)tools/dev/docs_check.sh' | tail -1)
# touched COMMIT PATH...: those of the paths the commit changes, on one line. :(top) - the paths are at the root of
# the published tree whatever folder this runs from.
touched() {
  local c=$1
  shift
  git diff-tree --no-commit-id --name-only -r "$c" -- "${@/#/:(top)}" | tr '\n' ' '
}
git cat-file -e "$HEAD:CHANGELOG.md" 2>/dev/null ||
  { echo "::error::docs_check: $HEAD is not a published tree (no CHANGELOG.md at its root)"; exit 1; }
COMMITS=$(git rev-list --no-merges "$BASE..$HEAD")
bad=0
for c in $COMMITS; do
  files=$(touched "$c" CHANGELOG.md README.md)
  shared=$(touched "$c" $SHARED_DOCS)
  if [ -n "$shared" ] && [ -n "$SHARED_RULE" ] && ! git merge-base --is-ancestor "$SHARED_RULE" "$c"; then
    echo "exempt: $(git log -1 --format='%h %s' "$c" | cut -c1-80) was written before the shared docs joined the rule (edits $shared)"
    shared=
  fi
  files=$files$shared
  [ -z "$files" ] && continue
  if git log -1 --format=%B "$c" | tr -d '\r' | grep -qiE '^Docs-fold:[[:space:]]*yes[[:space:]]*$'; then
    echo "ok: $(git log -1 --format='%h %s' "$c" | cut -c1-80) folds ($files)"
    continue
  fi
  echo "::error::$(git log -1 --format='%h %s' "$c" | cut -c1-80) edits ${files}without a 'Docs-fold: yes' trailer - put the entry and the docs' lines in changelog.d/<round>-<slug>.md instead (ADR 0123)"
  bad=1
done
if [ "$NO_FRAGMENTS" != "1" ]; then
  # python3 may be the Microsoft Store stub on Windows (on PATH, prints "Python was not found"): use one that runs.
  PY=
  for p in python3 py python; do "$p" -c '' >/dev/null 2>&1 && { PY=$p; break; }; done
  [ -z "$PY" ] && { echo "::error::docs_check: no Python to run changelog.py check"; exit 1; }
  "$PY" tools/dev/changelog.py check || bad=1
fi
[ $bad = 0 ] && echo "docs_check: no direct CHANGELOG.md / README.md / shared docs edits in $BASE..$HEAD"
exit $bad

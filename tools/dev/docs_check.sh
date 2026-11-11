#!/bin/bash
# Changelog fragments (task 0443, ADR 0123): a round never edits CHANGELOG.md or README.md, it adds
# changelog.d/<round>-<slug>.md, so the PRs of passes running in parallel never conflict there. This fails when one of
# the branch's own commits (BASE..HEAD, merge commits skipped) edits either file without a "Docs-fold: yes" trailer -
# the fold of a pass write-up (tools/dev/changelog.py fold prints the trailer) - and checks every fragment of the working
# tree is well formed (NO_FRAGMENTS=1 skips that, for refs that aren't the working tree). The `docs-fragments` CI job
# runs it on every PR. BASE and HEAD must be published trees (blendity at the root, as origin/* and task/* are): the
# libraries clone's local `blendity` branch keeps the files under blendity/ and is refused, not passed.
# Usage: bash tools/dev/docs_check.sh [BASE [HEAD]]     (default origin/main HEAD)
set -e
cd "$(dirname "$0")/../.."
BASE=${1:-origin/main}
HEAD=${2:-HEAD}
for ref in "$BASE" "$HEAD"; do
  git rev-parse --verify -q "$ref^{commit}" >/dev/null || { echo "::error::docs_check: no commit '$ref'"; exit 1; }
done
git cat-file -e "$HEAD:CHANGELOG.md" 2>/dev/null ||
  { echo "::error::docs_check: $HEAD is not a published tree (no CHANGELOG.md at its root)"; exit 1; }
COMMITS=$(git rev-list --no-merges "$BASE..$HEAD")
# A commit written before the rule reached its own history is exempt (the owner, 2026-11-10; ADR 0123's amendment): one
# that does not descend from the commit that added this script to BASE. Rounds published while the rule was being
# merged edited CHANGELOG.md the old way, and history is not rewritten; a commit made after its branch merged main is
# checked. RULE: the oldest commit on BASE that added this script (none: nothing is exempt).
RULE=$(git log --diff-filter=A --format=%H "$BASE" -- ':(top)tools/dev/docs_check.sh' | tail -1)
bad=0
for c in $COMMITS; do
  # :(top) - the paths are at the root of the published tree whatever folder this runs from.
  files=$(git diff-tree --no-commit-id --name-only -r "$c" -- ':(top)CHANGELOG.md' ':(top)README.md' | tr '\n' ' ')
  [ -z "$files" ] && continue
  if [ -n "$RULE" ] && ! git merge-base --is-ancestor "$RULE" "$c"; then
    echo "exempt: $(git log -1 --format='%h %s' "$c" | cut -c1-80) was written before the rule (edits $files)"
    continue
  fi
  if git log -1 --format=%B "$c" | tr -d '\r' | grep -qiE '^Docs-fold:[[:space:]]*yes[[:space:]]*$'; then
    echo "ok: $(git log -1 --format='%h %s' "$c" | cut -c1-80) folds ($files)"
    continue
  fi
  echo "::error::$(git log -1 --format='%h %s' "$c" | cut -c1-80) edits ${files}without a 'Docs-fold: yes' trailer - put the entry in changelog.d/<round>-<slug>.md instead (ADR 0123)"
  bad=1
done
if [ "$NO_FRAGMENTS" != "1" ]; then
  # python3 may be the Microsoft Store stub on Windows (on PATH, prints "Python was not found"): use one that runs.
  PY=
  for p in python3 py python; do "$p" -c '' >/dev/null 2>&1 && { PY=$p; break; }; done
  [ -z "$PY" ] && { echo "::error::docs_check: no Python to run changelog.py check"; exit 1; }
  "$PY" tools/dev/changelog.py check || bad=1
fi
[ $bad = 0 ] && echo "docs_check: no direct CHANGELOG.md / README.md edits in $BASE..$HEAD"
exit $bad

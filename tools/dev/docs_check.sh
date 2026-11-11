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
# written before the rule reached main may still edit them: SHARED_SINCE is the committer date of the commit on BASE's
# first-parent line in which this script gained the list (the merge that brought it to main), and a commit dated
# earlier is exempt for these four files. A date, not ancestry: a branch that never merges main is still checked for
# what it commits afterwards. While the rule is not on BASE yet (this round's own PR, or a newer copy of this script
# run on an older branch, as stack_check.sh does) there is no such date: then a commit is checked only when it
# descends from the commit of HEAD's own history that brought the list (SHARED_RULE; none: every commit is exempt).
SHARED_DOCS="docs/USABILITY.md tools/README.md docs/ARCHITECTURE.md docs/PERFORMANCE.md"
gained() { git log "$@" -S'SHARED_DOCS=' --format=%H -- ':(top)tools/dev/docs_check.sh' | tail -1; }
SHARED_SINCE=; SHARED_RULE=
landed=$(gained -m --first-parent "$BASE")
if [ -n "$landed" ]; then SHARED_SINCE=$(git log -1 --format=%ct "$landed"); else SHARED_RULE=$(gained "$HEAD"); fi
# shared_exempt COMMIT: the commit may edit the shared docs the old way.
shared_exempt() {
  if [ -n "$SHARED_SINCE" ]; then [ "$(git log -1 --format=%ct "$1")" -lt "$SHARED_SINCE" ]; return; fi
  [ -z "$SHARED_RULE" ] || ! git merge-base --is-ancestor "$SHARED_RULE" "$1"
}
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
# The shared docs, in a loop of their own, so their start date is the only thing that exempts a commit for them
# (whatever the loop above exempts for CHANGELOG.md and README.md).
for c in $COMMITS; do
  shared=$(touched "$c" $SHARED_DOCS)
  [ -z "$shared" ] && continue
  if shared_exempt "$c"; then
    echo "exempt: $(git log -1 --format='%h %s' "$c" | cut -c1-80) was written before the shared docs joined the rule (edits $shared)"
    continue
  fi
  if git log -1 --format=%B "$c" | tr -d '\r' | grep -qiE '^Docs-fold:[[:space:]]*yes[[:space:]]*$'; then
    echo "ok: $(git log -1 --format='%h %s' "$c" | cut -c1-80) folds ($shared)"
    continue
  fi
  echo "::error::$(git log -1 --format='%h %s' "$c" | cut -c1-80) edits ${shared}without a 'Docs-fold: yes' trailer - put the docs' lines in changelog.d/<round>-<slug>.md instead (ADR 0123)"
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

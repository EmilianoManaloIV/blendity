#!/bin/bash
# The integration PR (ADR 0132, task 0465): one pull request on main that carries every open stack whose required
# jobs are green, so the user merges once instead of once a stack with a rebuild of the others in between. From
# origin/main it merges each such stack's top branch - merge commits only, nothing rebased or squashed - pushes
# `integration/<n>` and opens a PR on main whose body has the "Merging this also merges #..." line for every PR it
# carries. While an integration PR is open, a later run continues its branch (main merged in when main has moved,
# then the tops it lacks; a fast-forward push, the generated stack summary refreshed). NEW=1 starts the next
# number from main.
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
    tip=$(git commit-tree "$(echo "$out" | head -1)" -p "$tip" -p "$1" -m "$2$STACK_AI_TRAILER")
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
begin='<!-- integrate.sh: begin stack summary -->'
end='<!-- integrate.sh: end stack summary -->'
summary="$begin
**Merging this also merges $(pr_join "${carried[@]}").**

Stack tops currently carried:"
for m in "${merged[@]}"; do
  summary="$summary
- #${N[m]} \`${B[m]}\` at $(git rev-parse --short "origin/${B[m]}")"
done
if [ ${#left[@]} -gt 0 ]; then
  summary="$summary

Left out:"
  for l in "${left[@]}"; do
    summary="$summary
- $l"
  done
fi
summary="$summary
$end"

# Old runs generated an unmarked summary. Remove only the recognizable summary at the start of such a body.
# Attribution and all remaining text are retained, including headings, checklists and reviewer notes.
legacy_notes() {
  awk '
    { line = $0; sub(/\r$/, "", line) }
    NR == 1 { first = $0; next }
    NR == 2 { second = $0; next }
    NR == 3 {
      legacy = first ~ /^\*\*Merging this also merges #.*\.\*\*\r?$/ && second ~ /^\r?$/ &&
        line ~ /^The integration PR \(ADR 0132\): every open stack whose required jobs / &&
        line ~ /Each pull request it carries can still be merged on its own instead\.$/
      if (legacy) { section = "tops"; next }
      printf "%s\n%s\n", first, second
    }
    section == "tops" && line ~ /^- #[0-9]+ `[^`]+` at [0-9a-f]+$/ { next }
    section != "" && line == "" { next }
    section == "tops" && line == "Left out:" { section = "left"; next }
    section == "left" && line ~ /^- ( #[0-9]+)+: (no single top|left out by hand|#[0-9]+ not green \(.*\))$/ { next }
    { section = ""; print }
    END { if (NR == 1) print first; if (NR == 2) printf "%s\n%s\n", first, second }
  '
}

# The fresh body uses the headings from .github/pull_request_template.md. Integration evidence for individual
# rounds lives on their PRs. The combined head has its own checks and can collect manual evidence here.
fresh_body() {
  cat <<EOF
## Task

\`docs/tasks/0465-integration-prs.md\` (ADR 0132, requirements C-05 and Q-03).

## What changed

Combines open stacks on main with merge commits. Each carried pull request remains independently mergeable.

$summary

## Tests

Required checks for newly included stack tops: $REQUIRED. See this PR's checks for the combined head.

## Review

See the carried pull requests for their independent reviews. The owner reviews and merges this PR.

## Documentation

See the carried pull requests for their task specs, changelog fragments and applicable ADRs.
See \`docs/AI_DISCLOSURE.md\` for attribution.
EOF
}

read_body_snapshot() {
  "$GH" pr view "$CUR" --repo "$REPO" --json body --jq .body || return
  # Keep trailing newlines for the comparison despite command substitution.
  printf '.'
}

previous=
if [ -n "$CUR" ]; then
  snapshot=$(read_body_snapshot) ||
    { echo "integrate: could not read #$CUR's body; nothing pushed or changed" >&2; exit 1; }
  previous=$(printf '%s' "${snapshot%.}")
fi

# Replace only a complete, unique pair of markers. A damaged or duplicated boundary must not swallow notes.
# A previously hand-written template can keep its original content and gain a generated summary.
headings=$(printf '%s\n' "$previous" | sed -n 's/\r$//; /^## \(Task\|What changed\|Tests\|Review\|Documentation\)$/p')
template_headings=$(printf '%s\n' '## Task' '## What changed' '## Tests' '## Review' '## Documentation')
has_markers=0
if printf '%s\n' "$previous" | sed 's/\r$//' | grep -Fxq -e "$begin" -e "$end"; then has_markers=1; fi
if [ "$has_markers" = 1 ] || [ "$headings" = "$template_headings" ]; then
  body=$(SUMMARY="$summary" awk -v begin="$begin" -v end="$end" -v has_markers="$has_markers" '
    { line = $0; sub(/\r$/, "", line) }
    line == begin { if (++begins != 1 || ends) exit 1; print ENVIRON["SUMMARY"]; next }
    line == end { if (++ends != 1 || !begins) exit 1; next }
    begins > ends { next }
    { print }
    !has_markers && line == "## What changed" { printf "\n%s\n", ENVIRON["SUMMARY"] }
    END { if (has_markers && (begins != 1 || ends != 1)) exit 1 }
  ' <<< "$previous") || { echo "integrate: ambiguous stack summary in #$CUR; nothing pushed or changed" >&2; exit 1; }
else
  body=$(fresh_body)
  notes=$(legacy_notes <<< "$previous")
  if [[ "$notes" == *[![:space:]]* ]]; then
    body="$body

### Previous PR description

$notes"
  fi
fi
if [ -n "$BL_AI_COAUTHOR" ]; then
  attribution="AI assistance for this run: ${BL_AI_COAUTHOR%% <*}."
  if ! printf '%s\n' "$body" | sed 's/\r$//' | grep -Fxq -- "$attribution"; then
    body="$body

$attribution"
  fi
fi
# An unchanged tip can still have new omission reasons. Skip the edit if the whole body is already current.
if [ "$body" != "$previous" ]; then
  body_file=$(mktemp)
  trap 'rm -f "$body_file"' EXIT
  printf '%s\n' "$body" > "$body_file"
fi

# gh edits the whole description. Re-read immediately before sending the edit to catch newer manual evidence.
# A concurrent edit after this final read can still race with the write.
edit_body() {
  local latest reason progress
  if ! latest=$(read_body_snapshot); then
    reason="could not re-read #$CUR's body"
  elif [ "$latest" != "$snapshot" ]; then
    reason="#$CUR's body changed during refresh"
  else
    "$GH" pr edit "$CUR" --repo "$REPO" --body-file "$body_file" >/dev/null
    return
  fi
  progress="nothing pushed"
  if [ "$tip" != "$start" ]; then
    progress="$BRANCH already advanced to $(git rev-parse --short "$tip")"
  fi
  echo "integrate: $reason; skipped the body edit; $progress" >&2
  return 1
}

if [ "$tip" = "$start" ]; then
  if [ -n "$CUR" ] && [ "$body" != "$previous" ]; then
    edit_body
    echo "$BRANCH is up to date: nothing pushed, #$CUR's body refreshed"
  else
    echo "$BRANCH is up to date: nothing pushed"
  fi
  exit 0
fi
git push -q origin "$tip:refs/heads/$BRANCH"
if [ -n "$CUR" ]; then
  if [ "$body" != "$previous" ]; then edit_body; fi
  echo "updated #$CUR ($BRANCH at $(git rev-parse --short "$tip"))"
else
  "$GH" pr create --repo "$REPO" --head "$BRANCH" --base main --title "Integration ${BRANCH#integration/}: the green stacks in one merge" --body-file "$body_file"
fi
echo "now run bash tools/dev/stack_check.sh"

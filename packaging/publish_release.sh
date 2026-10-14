#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Publishes one build as a GitHub Release (tasks 0079, 0082), from the release workflow:
#   packaging/publish_release.sh <tag> <version> <pr number or ""> <folder with the built files>
# - The release lists only what a person downloads: the three installers, under names without the version (so the
#   README's links, releases/latest/download/<name>, always fetch the newest), and SHA256SUMS.txt. The Windows name
#   keeps the "Blendity-Setup-" start that builds before task 0082 look for.
# - Everything else - each build's libraries archives and the Linux .tar.gz - goes to one "libraries" release, named
#   by the build's tag; Blendity's updater finds its own there. Kept: the archives of tagged releases and of the newest
#   KEEP_PR_BUILDS PR builds.
# - A merged PR's build is a GitHub pre-release, never "Latest". A suffixed tag (v0.11.0-beta.1) is a GitHub
#   pre-release too. Other tags are releases (GitHub picks the Latest); v0.x ones are titled and described as
#   pre-releases of the program, since 1.0 isn't out.
# GitHub adds "Source code" archives to every release by itself; they can't be left out (and the GPL wants the source
# at hand anyway). The install guide in the notes tells people they don't need them.
set -euo pipefail
shopt -s nullglob
TAG="$1" VERSION="$2" PR="$3" IN="$4"
REPO="${GITHUB_REPOSITORY:?}"
EXTRAS=libraries
KEEP_PR_BUILDS=20
cd "$(dirname "$0")/.."
out=$(mktemp -d)
extras=$(mktemp -d)
work=$(mktemp -d)  # (the notes)

# The built files, sorted: installers (renamed) to $out, the rest (named by tag) to $extras.
sort_files() {
  local f n platform
  for f in "$IN"/*; do
    n=$(basename "$f")
    case "$n" in
      Blendity-Setup-*.exe) cp "$f" "$out/Blendity-Setup-Windows.exe" ;;
      *-x86_64.AppImage) cp "$f" "$out/Blendity-linux-x86_64.AppImage" ;;
      *-macos.dmg) cp "$f" "$out/Blendity-macos.dmg" ;;
      blendity-libs-*)  # blendity-libs-<platform>-<version>.<ext> -> blendity-libs-<platform>-<tag>.<ext>
        platform=${n#blendity-libs-}
        platform=${platform%%-*}
        cp "$f" "$extras/blendity-libs-$platform-$TAG${n#*"-$VERSION"}" ;;
      *-linux-x86_64.tar.gz) cp "$f" "$extras/blendity-linux-x86_64-$TAG.tar.gz" ;;
      *) echo "not published: $n" ;;
    esac
  done
  local missing=0 want
  for want in Blendity-Setup-Windows.exe Blendity-linux-x86_64.AppImage Blendity-macos.dmg; do
    [ -f "$out/$want" ] || { echo "missing installer: $want" >&2; missing=1; }
  done
  return $missing
}

# One checksum list for everything this build published, wherever it went (the updater checks downloads against it).
write_checksums() {
  local files=("$out"/* "$extras"/*)
  { (cd "$out" && sha256sum -- *); [ -z "$(ls -A "$extras")" ] || (cd "$extras" && sha256sum -- *); } > "$work/sums.txt"
  mv "$work/sums.txt" "$out/SHA256SUMS.txt"
  echo "checksums for ${#files[@]} files"
}

upload_extras() {
  local files=("$extras"/*)
  [ ${#files[@]} -gt 0 ] || { echo "no libraries archives to publish"; return 0; }
  gh release view "$EXTRAS" --repo "$REPO" > /dev/null 2>&1 ||
    gh release create "$EXTRAS" --repo "$REPO" --target "${GITHUB_SHA:-main}" --prerelease --latest=false \
      --title "Libraries for Blendity (downloaded by Blendity itself)" \
      --notes "Blender's libraries for each Blendity build, which Blendity downloads by itself when you choose to use them, and the Linux program as a plain folder (.tar.gz). You don't need anything here to install Blendity: see https://github.com/$REPO#download"
  gh release upload "$EXTRAS" --repo "$REPO" --clobber "${files[@]}"
}

publish_pr() {
  gh pr view "$PR" --repo "$REPO" --json title,body --jq '"## " + .title + "\n\n" + .body' > "$work/notes.md"
  { echo; cat packaging/install-notes.md; } >> "$work/notes.md"
  gh release create "$TAG" --repo "$REPO" --target "${GITHUB_SHA:-main}" --prerelease --latest=false \
    --title "Blendity $VERSION (PR #$PR)" --notes-file "$work/notes.md" "$out"/*
}

# The newest release's Linux folder under a fixed name too (the README links to it).
copy_linux_folder() {
  local tgz="$extras/blendity-linux-x86_64-$TAG.tar.gz"
  [ -f "$tgz" ] || return 0
  cp "$tgz" "$extras/blendity-linux-x86_64.tar.gz"
  gh release upload "$EXTRAS" --repo "$REPO" --clobber "$extras/blendity-linux-x86_64.tar.gz"
}

publish_tag() {
  local flags=() title="Blendity $VERSION" lead="Blendity $VERSION."
  case "$VERSION" in
    *-*) flags=(--prerelease --latest=false)
         title="Blendity $VERSION (beta)"
         lead="A beta of Blendity $VERSION: try it before it's released." ;;
    0.*) title="Blendity $VERSION (pre-release)"
         lead="A pre-release of Blendity: it works, but it is still being refined." ;;
  esac
  # What changed since the previous tag (GitHub's own summary of the merged PRs), before the install guide - the part
  # Blendity's update dialog shows.
  gh api "repos/$REPO/releases/generate-notes" -f tag_name="$TAG" --jq .body > "$work/changes.md" || : > "$work/changes.md"
  { echo "$lead"; echo; cat "$work/changes.md"; echo; cat packaging/install-notes.md; } > "$work/notes.md"
  gh release create "$TAG" --repo "$REPO" "${flags[@]}" --title "$title" --notes-file "$work/notes.md" "$out"/*
  [[ "$VERSION" == *-* ]] || copy_linux_folder
}

# Keeps the archives of tagged releases and the newest PR builds; the rest go (GitHub allows 1000 files a release).
prune_extras() {
  local releases keep n t
  releases=$(gh release list --repo "$REPO" --limit 1000 --json tagName,createdAt --jq 'sort_by(.createdAt) | reverse | .[].tagName')
  keep=$( (grep -v '^build-pr' <<< "$releases" || true; grep '^build-pr' <<< "$releases" | head -n "$KEEP_PR_BUILDS" || true) )
  gh release view "$EXTRAS" --repo "$REPO" --json assets --jq '.assets[].name' | while read -r n; do
    case "$n" in
      blendity-libs-*) t=${n#blendity-libs-}; t=${t#*-} ;;
      blendity-linux-x86_64-*) t=${n#blendity-linux-x86_64-} ;;
      *) continue ;;
    esac
    t=${t%.zip}
    t=${t%.tar.gz}
    grep -qxF -- "$t" <<< "$keep" && continue
    echo "removing $n (its build is gone or older than the newest $KEEP_PR_BUILDS PR builds)"
    gh release delete-asset "$EXTRAS" "$n" --repo "$REPO" --yes < /dev/null
  done
}

sort_files
write_checksums
upload_extras  # (first, so a release is never seen without its libraries)
if [ -n "$PR" ]; then publish_pr; else publish_tag; fi
prune_extras

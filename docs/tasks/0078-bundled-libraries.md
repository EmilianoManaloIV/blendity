# 0078: Blender's libraries come with Blendity

- **Status:** done
- **Requirements:** R-01
- **Decisions:** ADR 0001 (amended by ADR 0032's consequences: the installers carry the libraries)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
The user (2026-10-14): "make sure other dependencies should be included when installing the project. The user
shouldn't really have to worry on downloading blender on their own."

## Scope
- In: `packaging/fetch_blender_libs.sh <platform>` - sparse, shallow, blob-less clones of only the folders Blendity
  links, at commits pinned in `packaging/blender-libs.txt`, into Blender's layout (reusing a clone already there),
  plus Blender's colour config when missing; the release workflow fetches (cached by the pins) and builds all three
  installers with the libraries, checking each build has them; the macOS bundle carries `lib/` and `datafiles/`;
  the build scripts and README point at the script.
- Out: CI's build.yml stays dependency-free (it tests the fallbacks); Windows on ARM.

## Acceptance criteria
1. The release workflow's three jobs build with Blender's libraries (Embree, OIDN, OpenColorIO, Jolt, FFmpeg) and the
   installed or mounted copies start.
2. The script reuses an existing checkout at the pinned commit without changing it.

## As built
- Checked here: the script on this machine's Windows checkout (unchanged, 10 s); a pinned fetch of one folder in a
  scratch clone. The full fetches and the three installers are checked by the release workflow on the PR.
- macOS with Blender's libraries is built only on CI (no Mac here).

# 0605: Reduce first-open Textures directory overhead

- **Status:** in review
- **Authorization:** Owner-approved interactive performance work under audit 0600, round 623. Investigate the current editor and remove verified redundant work in Textures discovery.
- **Requirements:** R-04, Q-04, Q-06
- **Decisions:** ADR 0017 (procedural textures), ADR 0056 (measurement), ADR 0123 (changelog fragments). No new decision needed.
- **Implementation:** project implementer, with independent test-engineer and code-reviewer
- **PR:** [#501](https://github.com/EmilianoManaloIV/blendity/pull/501) against main. Initial published head `d6af4056d5b103d9a5aad648d8301217d394f21d`.

## Goal

Open an existing 2,000-texture library sooner while preserving the list, paths, and editing behavior. The first usable frame target is 100 ms.

## Scope

Profile directory discovery, asset reads and decoding, filtering, and visible thumbnail evaluation on base 66feaeb3. Remove repeat attribute queries where enumeration already supplies sufficient information. Progressive presentation, texture lifecycle changes, and cache changes are outside this round.

## Likely files

- `src/editor/texture_window.cpp` for directory discovery and canonical path handling.
- `tests/test_assets.cpp` for Windows junction and equal-basename behavior.
- This spec and `changelog.d/623-texture-directory.md` for evidence and handoff.

## Acceptance criteria

1. Profile the current library build with the existing 2,000-file fixture and quantify discovery, loading, and visible thumbnail costs at both default and two-worker settings.
2. Verify the same 2,000 sorted paths in each fresh production run. Run three interleaved uninstrumented pairs at each worker limit. Report first usable frame, list completeness, following frame, p95, warm reopen, and working set. Require a reproducible improvement and record any remaining gap to 100 ms.
3. Preserve case-insensitive search, equal basenames in separate directories, exact selection and thumbnails, symlinks and junctions, and malformed, missing and repaired assets. Verify applicable behavior in Windows and Linux groups.

## Tests and checks to run

- Repeat profile and production pairs in separate hub-owned quiet windows with CPU viewport at 1600 × 900. Validate fixture digests before and after each batch.
- Run `BLENDITY_TEST_FILTER="round 623"` against the Windows reference and candidate, then Windows libraries and Linux dependency-free `assets` and `editor` groups. The hub runs Windows dependency-free verification on the combined tree.
- Check the diff and changelog fragment and obtain independent test and code review.

## Risks and edge cases

Directory enumeration reports a reparse point's attributes. The scan checks its target before traversing a directory and uses canonical fallback for special files. An outside-project alias retains its original absolute path. Native search-open failures fall back to the filesystem iterator. Keep depth limits, hidden-name filtering, extension case handling and deterministic sorting. Treat every timing as a fresh-process result with an uncontrolled operating-system file cache.

## Documentation to update

Record the result in `changelog.d/623-texture-directory.md` with a performance section for the hub's documentation fold. Record baseline and candidate results, scope limits, and verification receipts here.

## Outcome

Round 617 measured 72.5–86.3 ms of discovery before parallel browser loading and achieved median first opens of 169.0 ms at default parallelism and 244.7 ms with two workers after the loading change. The round 623 profile on base 66feaeb3 remeasured the separate costs. Each diagnostic repeat made 2,000 Windows attribute queries. [Earlier round](0599-cold-textures.md) · [Current profile log](C:/blab/0623/profile-window.hub.out) · [Quiet window](C:/blab/pass32-interactive01.log)

| Workers | Discovery, ms | Per-file attribute queries, ms | Batch load, ms | 160 visible thumbnails, ms |
|---|---|---|---|---|
| Default | 66.6 / 68.0 / 68.3 | 59.0 / 60.2 / 60.1 | 63.3 / 63.7 / 65.3 | 36.2 / 36.8 / 38.0 |
| Two | 63.8 / 69.0 / 64.2 | 56.4 / 61.0 / 56.9 | 124.1 / 128.5 / 119.6 | 52.8 / 54.0 / 52.8 |

The candidate uses attributes returned by Windows directory enumeration to classify ordinary files. It follows reparse directories through the existing filesystem status check and preserves fallback canonicalization. POSIX uses the original filesystem classification. [Windows enumeration contract](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findnextfilew)

| Workers | First usable and complete, before (ms) | First usable and complete, after (ms) | Median improvement |
|---|---|---|---|
| Default | 170.3 / 177.6 / 180.6 | 123.2 / 122.4 / 124.0 | 177.6 → 123.2 ms (30.6%) |
| Two | 244.9 / 242.6 / 241.5 | 194.6 / 193.4 / 191.9 | 242.6 → 193.4 ms (20.3%) |

| Workers | Median next frame, before → after (ms) | Median p95 of 25-frame sequence (ms) | Median warm reopen (ms) | Median working set before open (MB) | Median working set after open (MB) |
|---|---|---|---|---|---|
| Default | 3.7 → 3.7 | 3.7 → 4.4 | 3.7 → 3.6 | 73.5 → 73.7 | 77.2 → 77.6 |
| Two | 3.7 → 3.8 | 3.7 → 4.1 | 3.3 → 3.3 | 63.3 → 63.3 | 66.0 → 66.0 |

Both median first opens remain over 100 ms by 23.2 and 93.4 ms respectively. The full list appears on the first Textures frame. The p95 sequence includes the first open and 24 subsequent frames. The production reference SHA-256 begins `53f3222d` and the candidate begins `8fc460cb`. [Production receipts](C:/blab/0623/paired-wj0v4qdl/receipts.json) · [Build provenance](C:/blab/0623/object-provenance.json) · [Production timing window](C:/blab/pass32-verify03.log)

Independent tests cover Windows junction aliases and same-name selection in path order. The Windows reference passed all 17 focused checks. An SDK macro collision in the joined tests was corrected and the fixed test source compiled with MSVC 14.51. Independent static review approved. Windows libraries passed 3,204 assets and 3,868 editor checks, including the real junction and same-name cases. The Linux dependency-free combined build passed 3,166 assets and 3,860 editor checks. Both 623 source blobs in the Linux build archive match this worktree, and the Linux log exercises the same-name case and the earlier symlink, malformed-file and repair checks. The production window closed with zero taint markers. The hub owns combined Windows dependency-free verification and the subsequent published CI checks. [Windows assets](C:/blab/0623/candidate-assets-b4420327f8264b7cb91bf5b1d529d54d/run.log) · [Windows editor](C:/blab/0623/candidate-editor-daf963066b1340b7826986cc5a3f0698/run.log) · [Linux assets](C:/blab/pass32/joined5/joined-linux-assets.log) · [Linux editor](C:/blab/pass32/joined5/joined-linux-editor.log) · [Combined blob manifest](C:/blab/pass32/joined5/joined-blobs.json)

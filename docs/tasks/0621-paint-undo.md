# 0621: Restore painted textures on Undo

- **Status:** in review
- **Authorization:** Pass 35 Hub assignment, round 639, baseline 68dafe59.
- **Requirements:** Q-05
- **Decisions:** [ADR 0081](../decisions/0081-projection-painting-and-tile-undo.md) for the existing tile-based paint history.
- **Implementation:** project implementer.
- **PR:** local handoff to Hub.

## Goal
Undo the first stroke on a fill-only surface and see its original texture in the Textures browser. Redo restores the painted result.

## Scope
- In: reproduce the reported mouse gesture and distinguish persisted document, history targeting and preview failures. Fix the demonstrated cause.
- Out: procedural evaluation, palette and lessons, performance changes.

## Likely files
- Paint transaction in `src/editor/paint.cpp` and `src/editor/editor.h`, its layer reflector in `src/texture/procedural_texture.cpp`, and the thumbnail cache in `src/editor/texture_window.cpp`.
- Paint/undo event regressions in `tests/test_editor.cpp`.

## Acceptance criteria
1. A real first stroke on a fill-only texture adds a Paint layer. Undo Paint empties that layer and restores the initial composite and card. Redo restores the stroke. Preserve the established separate Add Paint Layer history step (tests_round266 and the VR sweep contract).
2. Consecutive strokes undo and redo one gesture at a time. Another texture keeps its original contents and card.
3. Persisted assets and preview agree after each edit. Identify the failing baseline step and verify the same sequence on the candidate.
4. A Paint layer saved with the maximum representable stamp accepts another stroke. Undo and Redo restore the exact stamp and pixels for each state. A legacy texture with no stamp loads at zero.
5. Hide the Textures window after the second stroke, Undo and paint a different second stroke. Reopening the window shows the replacement pixels on the card and in the selected preview after the surface worker finishes. The neighboring card stays unchanged.
6. After a settled stroke, paint and immediately Undo before an intervening draw. Reopening the selected full preview shows the restored state.

## Tests and checks to run
Run focused regression, affected and editor groups on Windows and Linux with scratch project and trash and official machine slots. Independent tester verifies the connected path; independent reviewer checks the final diff. No geometry or timing changes.

## Risks and edge cases
An automatically created layer, a stale thumbnail and a different active undo target can produce similar symptoms. Compare exact saved state and visible pixels.

## Documentation to update
Round 639 changelog fragment with README and USABILITY sections, plus this spec and the Hub receipt.

## Outcome

On baseline `68dafe59`, a mouse click painted a fill-only texture. Clicking the status-bar Undo button logged **Undo Paint**, but the card kept its painted digest on both following frames. A separate Save and export restored the original albedo byte for byte. Redo restored the painted albedo. The PNGs and layer stack showed that the history action reached the intended texture. The neighboring texture kept its pixels. [Mouse replay and card crops](C:/blab/pass35-hub/0621-evidence/baseline-click-npxto3w4/result.json) · [Saved layers and exports](C:/blab/pass35-hub/0621-evidence/disk-baseline-fbni86ck/disk-hashes.json)

The Paint layer's stroke stamp was missing from the texture hash and saved `.ptex`. Forwarding the reflector's serialization mode gives the thumbnail a different identity when a stroke is applied or undone. Undo Paint still empties the automatic Paint layer. The separate Undo Add Paint Layer step removes it. A loaded maximum stamp now wraps to zero on the next stroke, and Undo restores its exact previous value. An older texture with no stamp starts at zero. [Layer reflector](../../src/texture/procedural_texture.cpp) · [Paint history](../../src/editor/paint.cpp)

Independent review found another way to display stale pixels. After Undo and a replacement stroke while Textures was hidden, both branches had stamp 2. The cached card showed 196 red pixels. Reopening the saved scene showed the correct 98 red and 98 green. Thumbnails now also check the texture's mutation version. When a replacement stroke matches a cached full output or pending evaluation for that asset, the editor discards that result so its usual worker can rebuild the selected preview. [Reviewer reproduction](C:/blab/pass35-hub/0621-evidence/review-hidden-card-v0suez80/result.json) · [Thumbnail cache](../../src/editor/texture_window.cpp)

Independent review also reproduced a stale full preview after a second dab immediately followed by Undo. The brush had already painted into the cached albedo for the restored stamp. Stroke completion and tile Undo/Redo now invalidate that surface's matching cached result and cancel a matching in-flight evaluation. The normal worker rebuilds the preview. In the final replay, the erased mark was gray again and the retained mark stayed red. The complete 296 × 296 preview, thumbnail and six saved Paint images matched a fresh reopen. [Immediate Undo reproduction](C:/blab/pass35-hub/0621-evidence/review-v3-fast-undo-hg5q3xx2/result.json) · [Corrected replay](C:/blab/pass35-hub/0621-evidence/independent-v4-immediate-27qhpnky/result.json)

The baseline event regression had 20 failed checks. The final Windows MSVC 14.51 and Linux GCC 15.2 dependency-free builds each passed all 150 focused checks, including maximum-stamp rollover, immediate Undo, hidden replacement, and an unchanged neighbor. The independent tester used actual mouse controls for Paint, Undo, Redo and Save. The standalone replays compared the card and selected preview against fresh-process reopens, and compared persisted layers and pixels at the corresponding history steps. [Windows checks](C:/blab/pass35-hub/0621-evidence/v4-win-focused/tests.log) · [Linux checks](C:/blab/pass35-hub/0621-evidence/v4-linux-focus.log) · [Hidden replacement](C:/blab/pass35-hub/0621-evidence/independent-v4-hidden-lhwdi4db/result.json) · [Nine-state replay](C:/blab/pass35-hub/0621-evidence/independent-v4-ordinary-ofitl9mu/result.json)

The final editor, assets, render and VR groups passed 47,960 checks on Windows and 47,962 on Linux, with zero failures. The independent reviewer approved all six acceptance criteria and closed both preview findings. The changelog check passed and the coverage audit found all 398 command, keymap and window entries covered. The Hub owns joined verification and publication. [Windows groups](C:/blab/pass35-hub/0621-evidence/v4-win-groups/tests.log) · [Linux groups](C:/blab/pass35-hub/0621-evidence/v4-linux-editor-assets-render-vr.log) · [Independent tester](C:/blab/pass35-hub/0621-evidence/independent-test.md) · [Independent review](C:/blab/pass35-hub/0621-evidence/review-v4.md)

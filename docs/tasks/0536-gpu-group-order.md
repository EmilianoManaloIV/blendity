# 0536: Card tests that passed alone and failed in a group run - a slow run, not a leak (round 555)

- **Status:** done
- **Requirements:** the suite's reliability (ADR 0056)
- **Decisions:** none needed
- **Model:** main session (the HCI worker)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/444 - base `main`, its own

## Goal
Three `gpu` tests passed alone and failed inside a full `--group gpu` run on M1's, Pass 26's and R1's trees: "gpu views: the
editor's own lighting ... and a mirrored object", round 508's "Generate Lighting with the views on the card", and "gpu views:
... a click picks what is there now". Find what an earlier test leaves behind and fix it at the source.

## What was found
- On main the full `--group gpu` run passes all three (115 tests, fresh folder); with two other groups drawing on the card at
  once, too. Not an earlier test's state, not another card user.
- The failing runs were slow: Pass 26's log has quick tests at 30 times their time (339 ms -> 9,045 ms), the editor's lighting
  2.268 levels apart where a calm run has 0.191, round 508's view "not still" for 69 frames where a calm run has 11-16.
- These tests take a picture or click after a fixed number of frames, and meshes go up to the card a few a frame (round 458's
  queue): in a starved run - below-normal priority while other sessions build at normal - they are still going up when the
  picture is taken. Reproduced: the `gpu` group at low priority fails round 508's test with the same message.
- The click test's "a click where the Cube was" check (the one M1's slow run failed) clicked one pixel several times: two
  presses within 0.35 s and 5 px are a double click, which frames the selection - the camera flew to the moved Cube
  (printed: from x 3 to x 39) and the next click rightly picked it. The pick itself is right; a fast run passed because the
  camera had not moved far yet.

## Scope
- In: `r555_card_settle(ed)` in `tests/test_gpu.cpp` - steps until no mesh waits for the card, no texture streams and the Scene and Game views are the same
  picture two frames running (20 s at most), used where the three tests trusted a fixed count of frames (`r122_views`, round
  508's scene before the bake, the landed picture and the one-frame reference, the click test's frames before a click), and
  `r555_single_click` - the click test's clicks wait the double-click time out, each a click of its own. The runner's
  `BLENDITY_TEST_RANGE=lo-hi` / `BLENDITY_TEST_ALSO=text` (a slice of a group in its order, plus named tests: to bisect a
  leak), kept with a runner test.
- Out: the three card-against-CPU tests (textured materials, overlays, Lanczos): round 551.

## Acceptance criteria
1. The `gpu` group at low priority passes the three tests (it failed round 508's before), and so does a normal run.
2. The `gpu` group as usual passes them; the runner's new option has a test.

## Tests and checks to run
- `--group gpu` at `/low` priority and normally (the CARD slot); `--group runner`; Linux `runner`.

## Findings for other owners (not changed here)
- Round 508: in one low-priority run "no frame mixes the old and new lighting" failed (`mixed` above 0) with the view
  settled - possibly a real ADR 0140 breach when the editor is starved. It passed in the final runs.
- Round 412: "F12 on a slow card ... twenty frames" failed once at low priority only (a fixed count of frames, like the
  three here). It passed in the final runs.
- The editor's own lighting read 2.27 / 255 in other workers' group runs because tests read the user's real `prefs.txt`
  without `BLENDITY_PREFS_DIR` (the hub's finding). Group runs need a scratch preferences folder; the settle now also waits
  for the Game view, which that test compares.

## Risks and edge cases
- A view that never settles (something moving every frame) waits the 20 s and says so; the test's own checks then judge.

## Documentation to update
- `changelog.d/555-gpu-group-order.md`; this task.

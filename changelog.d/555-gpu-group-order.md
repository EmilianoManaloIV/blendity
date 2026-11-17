## 2026-11-17 (round 555): card tests that failed only in a slow run wait for the card (task 0536)

**Asked** (the hub): three `gpu` tests passed alone and failed in a full group run on three workers' trees - find what an
earlier test leaves behind.

**Fixed (tests)**
- Nothing was left behind: on main the full group passes them, with other groups drawing on the card at once too. The
  failing runs were slow (quick tests at 30 times their time): "gpu views: the editor's own lighting ... and a mirrored
  object", round 508's "Generate Lighting with the views on the card" and "gpu views: ... a click picks what is there now"
  took a picture or clicked after a fixed number of frames, while meshes still went up to the card a few a frame. A run at
  low priority reproduced round 508's failure. They now wait for the card to catch up (`r555_card_settle`: no mesh waiting,
  no texture streaming, the Scene and Game views the same two frames running, 20 s at most). The click test also clicked one pixel several times in
  quick succession - a double click, which frames the selection: the camera flew to the moved Cube and the next click
  rightly picked it. Its clicks are single clicks now (they wait the double-click time out); the pick itself was right.
- **The runner:** `BLENDITY_TEST_RANGE=lo-hi` runs the tests numbered lo to hi in their order (each printed with its number)
  and `BLENDITY_TEST_ALSO=text` adds the tests whose name holds it - a slice of a group, then the test that fails after it.

**Checked:** the `gpu` group (a scratch test folder and preferences each run) at low priority (it failed round 508's test
before) and at normal priority: 1050 checks, the three tests pass at both (the editor's own lighting: 0.210 / 255 apart,
99.97 % within 3); the 4 failures are round 551's three card-against-CPU tests, as on main. `runner` on Windows and Linux,
59 checks. Two findings from earlier low-priority runs, for their owners and not changed here: round 508's "no frame
mixes the old and new lighting" failed once with the view settled (a possible ADR 0140 breach when starved), and round
412's "F12 on a slow card ... twenty frames" failed once at low priority only. Neither failed in the final runs.
The editor's own lighting's 2.27 / 255 in other workers' group runs came from tests reading the user's real preferences
(no `BLENDITY_PREFS_DIR`; the hub found it); the settle now waits for the Game view too, which that test compares.

<!-- tools/README.md -->
append:

## Round 555: a slice of a test group

- `BLENDITY_TEST_RANGE=lo-hi` and `BLENDITY_TEST_ALSO=text` (round 555): a run of `blendity_tests` takes only the tests numbered lo to hi, in their order, plus those whose name contains text - to find which earlier test leaves state behind for a later one (bisect lo-hi).

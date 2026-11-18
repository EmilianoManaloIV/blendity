# 0505: The timed check (Pass 26, round 522)

- **Status:** done (round 522, published)
- **Requirements:** Q-04, Q-06
- **Decisions:** ADR 0146 (and its round 520 / 521 amendments), ADR 0112 (the 1.3x bar), ADR 0056 (a measurement is a stress row)
- **Model:** main session
- **PR:** stacked on #458 (round 521); base main

## Goal
Measure, in a timed window the hub grants on the owner's machine, every row of spec 0501's target table with rounds
518-521 in (#458's head, frozen in `C:\blcmp518\exe521`), against round 518's baseline of main 068207d9
(`C:\blcmp518\base518`, `bench518`). What still misses 16.7 ms is fixed under ADR 0112's bar or named with the part of its
frame that holds it - including the two gaps ADR 0146's round 520 amendment left to this round (the vertex deform every
frame; F12 / the path tracers posing at their snapshot on the main thread).

## The runs (`C:\blcmp518\timed522.bat`, normal priority, in the window only)
1. `blendity_stress --only posing` and `--only animation` on the card (the stand-in alone and in its world), the CPU rows
   alongside (no slower than round 518's).
2. `tools/bench_world.py --posing` with the owner's avatar in the owner's world (a fresh copy of the prepared scratch
   project, `C:\blcmp518\bench522\prepared`, prepared untimed for this exe).

## What is written
- `stress/results/baseline.md` (or the section's own results file): the after rows beside 518's.
- A table in this spec: each target row, before / after, alone and in the world, met or not; for a miss, its frame's parts
  from the stress row's breakdown and what holds it.
- The vertex deform (skin setup + skinning) and F12's first frame of a posed character: whether either holds a row.
- Any fix found under ADR 0112's bar becomes its own round (a follow-up), not mixed into the measurement.

## Acceptance criteria
1. Every target row measured in the window, before and after, from the same machine.
2. Each row met, or named with its cause.
3. The CPU rows no slower than round 518's.
4. Stress problem counts not up; the report files deleted and `stress/results/latest.md` restored.

## Results (2026-11-17, the hub's windows: 21:50-22:00 on #458's build, 22:13-22:15 on the fix below)
Stress `posing` on the card, ms a frame (the stand-in; "world" = in its world stand-in). 518 = main 068207d9, 521 = #458,
522 = with this round's fix. Over-budget rows in the section: 42 -> 30 -> 27; `animation`: unchanged.

| row | alone 518 | 521 | 522 | world 518 | 521 | 522 |
|---|---|---|---|---|---|---|
| Rotate gizmo on the chest (FK) | 37.3 | 6.9 | **6.9** | 42.6 | 11.2 | **11.1** |
| Move gizmo on the left hand's IK target | 11.6 | 5.2 | **5.1** | 15.9 | 9.3 | **9.4** |
| a Puppet handle (AutoPosing on) | 55.6 | 12.5 | **12.3** | 62.5 | 20.2 | 20.8 |
| G on the left hand's IK target | 15.3 | 5.3 | **5.4** | 19.4 | 12.2 | **13.1** |
| R on the chest | 47.3 | 7.4 | **6.8** | 57.2 | 14.9 | **16.0** |
| a scrub, Scene view only | 47.3 | 8.7 | **7.9** | 60.6 | 18.9 | 20.5 |
| the clip playing, Scene view only | 17.9 | 11.3 | **10.0** | 17.7 | 17.3 | 17.8 |
| Play with the springs | 45.6 | 28.2 | **9.7** | 70.7 | 48.9 | 24.7 |
| a scrub, Scene + Game view | 70.1 | 29.1 | **10.8** | 89.5 | 40.5 | 23.9 |
| the clip playing, Scene + Game view | 20.6 | 18.0 | **11.8** | 35.8 | 19.0 | 18.1 |

Settings rows in the world (the IK-target drag unless named), 518 -> 522: Record on 30.3 -> 16.9; Preview off 28.4 -> 18.8;
the Game view beside 28.3 -> **14.0** (its clip 41.1 -> **12.4**); Other Views Follow Drags 45.9 -> 29.2; trails / onion
skins / zoetrope 33.8 -> 23.8 (their scrub 95.6 -> 37.9); Centre of Mass 30.3 -> 20.5 (its clip **13.5**); the Avatar window
26.8 -> 20.8; the Rigging window 28.1 -> 18.7; Realtime GI Manual 17.9 -> **14.3**; Live Link 31.1 -> 22.3 (its clip
**16.2**); surface snapping 27.7 -> 21.4; an F12 Live Preview 30.5 -> 20.0. Not targeted: the path-traced view 811 -> 769,
Realtime GI Automatic 72.5 -> 68.9 (neither slower).

The owner's avatar in the owner's world (`tools/bench_world.py --posing`, #458's build, before the fix): FK chest 39.1 ->
17.1 headless, 50.0 -> 16.9 windowed; scrub (Scene view) 38.0 -> 15.4 windowed; the IK rows 10-16 ms; the two-view rows
(39.8 / 42.0 headless) were measured before the fix, which takes their Game view's skins to the card.

The CPU views (no card): every row as fast or faster than 518's (FK chest 29.9 -> 14.4, scrub 30.4 -> 14.4, Play 28.2 -> 13.8).

### The fix this round made (ADR 0112: 2.4-6.5x on the two-view and Play rows)
`render_deferred` turns the flatten view on for every Game view gather, and round 519 kept a skin off the card whenever it
was on - so every skin in the Game view was posed on the CPU and sent whole (21 meshes, 8 MB a frame of the stand-in). A
Camera Flatten only moves the item's matrix; the exclusion is now the view-shaped meshes alone (`render_view.cpp`), with a
test (`tests_round522`) that fails on the old rule.

### What holds the rows still over 16.7 ms
Every row alone on the card meets it. In the world, the misses' frames go to the card drawing the world, not to skinning
(skinning, palettes and uploads are under 2 ms in each): the Scene view's frame 14-17 ms of which the card's render call
6-11 ms and its lighting 3-6 ms (Puppet 20.8, scrubs 20.5 / 23.9, the clip 17.8, Preview off 18.8); two views or Other Views
Follow Drags draw the world twice (Play 24.7: the Game view's render call 15 ms; Follow Drags 29.2); trails / onion skins /
zoetrope draw the extra poses (37.9); the windows (Avatar, Rigging), surface snapping and F12's Live Preview add their own
work on top of the world's draw. Named for the Speed / render stream: the world's draw on the card (the render call and the
lighting pass) is what a whole-body pose in the world now waits on.

### ADR 0146's round 520 gaps
- The vertex deform every frame: skin setup + skinning 1.5-2.4 ms in the drag rows; no row alone is held by it (all met),
  and in the world it is under the world's draw. It stays (the amendment stands).
- F12 / the path tracers posing at their snapshot on the main thread: F12's Live Preview row 30.5 -> 20.0 with its frame in
  the world's draw, not the snapshot; it stays as amended.

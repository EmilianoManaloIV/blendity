## 2026-11-17 (round 522): Pass 26's timed check, and the Game view's skins on the card

**Asked:** time every row of Pass 26's target table on the owner's machine (task 0505) - the stand-in alone and in its world,
the owner's avatar in the owner's world - against round 518's baseline, and fix or name what still misses 16.7 ms.

**Fixed:** every skin in the Game view was posed on the CPU and sent whole (21 meshes, 8 MB a frame of the stand-in): the
Game view turns the flatten view on for every gather, and round 519 kept a skin off the card whenever it was on. A Camera
Flatten only moves an item's matrix, so only view-shaped meshes (view-angle shape keys, a camera-aligned lattice) stay off
the card now. Alone on the card: a scrub with the Game view beside 29.1 -> 10.8 ms, Play with the springs 28.2 -> 9.7, the
clip with two views 18.0 -> 11.8.

**Checked (the hub's timed windows):** stress `posing` on the card, 518 -> 522, alone: the chest's Rotate gizmo 37.3 -> 6.9,
a Puppet handle 55.6 -> 12.3, R on the chest 47.3 -> 6.8, a scrub 47.3 -> 7.9, a two-view scrub 70.1 -> 10.8, Play 45.6 ->
9.7 - every row alone meets 16.7 ms; in the world: the chest 42.6 -> 11.1, the IK target 15.9 -> 9.4, R 57.2 -> 16.0, the Game
view beside 28.3 -> 14.0; over-budget rows in the section 42 -> 27. The owner's avatar in the owner's world: the chest 50.0
-> 16.9 windowed, a scrub 38.0 -> 15.4. The CPU rows all as fast or faster (the chest 29.9 -> 14.4). Still over, in the world:
a Puppet handle 20.8, scrubs 20.5 / 23.9, the clip 17.8, Play 24.7 and most setting rows (17-29) - their frames are the card
drawing the world (its render call 6-15 ms and lighting 3-9 ms), not skinning, named for the render stream. `animation`
unchanged. `tests_round522` (the Game view's gather makes card skins; a Camera Flatten's item stays one, and the CPU fallback its view takes poses it); animation 5,035, editor
3,869, render 40,472, gpu on the card 1,510 - 0 failed. `stress/results/baseline.md`'s posing section is the timed run's.

<!-- docs/USABILITY.md -->
after: | Posing an avatar inside a baked world made the card's instances
| The Game view posed every skinned character on the CPU and sent it to the card whole each frame, so a scrub or Play with the Game view open ran at 30-50 ms. (round 522) | The Game view skins on the card like the Scene view: a scrub with both views 10.8 ms, Play with the springs 9.7 ms on the stand-in. |

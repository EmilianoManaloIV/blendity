# 0206: playback without the bookkeeping

- **Status:** done (round 226)
- **Requirements:** R-07, Q-04, R-13; task 0203's rank 3
- **Decisions:** none
- **Model:** main session
- **PR:** #171 (stacked on #170)

## What was there
After task 0204, playing 1,000 keyframed objects took 11.7 ms a frame against Blender's 11.3. Timed inside the
Animation window's playback, each frame:
- **About 10 ms in `anim_snapshot_shown`.** It looks up every animated number of the clip's objects (9,000 values
  and 3,000 tracks) into a map keyed by strings. Record uses it to tell an edit from the clip, and it ran every frame
  whether Record was on or not.
- **About 3 ms in `apply_clip`.** It found each track's object by searching its parent's children by name: half a
  million name compares under a thousand children.

## What changed
- **The snapshot is taken only while Record is on.** Turning Record on takes one. Anything that records with a stale
  one looks again first, as it already did after a change of root or clip. An edit made while Record was off is no
  longer keyed by the first edit after Record is turned on, which is Unity's behaviour too.
- **`apply_clip` finds a many-track clip's objects in one walk** of the branches its tracks name. It follows the same
  objects as the search did: the first child of a name. Paths a walk can't spell (an empty name, a name with a
  '/') are left to the search. A big rig whose clip keys a few bones walks only those few branches.

## Measured (a 2560 x 1369 window, a frame)

| Row | Before | After | Blender 5.2.2 |
|---|---|---|---|
| Playing 1,000 keyframed objects | 11.7 ms | 4.0 ms | 11.3 ms |

Of the 4.0 ms: the UI 2.0, the Scene view 1.6, presenting 0.4.

## Checked
- tests/test_animation.cpp `tests_round226`:
  - a 300-track clip poses the first child of a duplicated name;
  - a nested path whose first parent lacks the child finds nothing, as before;
  - the root's own track ("") moves the root, and a child with an empty name is left alone;
  - after playing with Record off, Record turned on keys a move.
- The animation group passes.
- An Opus review found, all fixed:
  - the one-walk lookup following a second object of a name where the search found none;
  - the root's own track lost to a child with an empty name;
  - a name with a '/' reachable where the search couldn't reach it;
  - a big rig with few keyed bones walked whole every frame (now only the branches the tracks name).

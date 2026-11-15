# 0500: Four timing checks that fail under load, made behavioural (round 517)

- **Status:** done (round 517)
- **Requirements:** Q-06 (responsiveness), the suite's reliability (ADR 0056)
- **Decisions:** ADR 0056 (a unit test pins a behaviour; a measurement is a stress row) - none new
- **Model:** main session (the HCI worker)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/417 (base `main`, its own)

## Goal
Three checks in the `livelink` and `vr` groups (and a fourth found on the way) assert wall-clock times and failed when the machine was busy with other
work (round 512's runs: a different one each run). Each keeps what it is for - the behaviour - and stops asserting a time;
a time worth keeping is printed, and recorded as a stress row where none has it yet.

## Scope
- In:
  1. `tests/test_livelink.cpp` "Live Link's clips as things change (review 379)": the three cases (another clip chosen, an
     axis the clip doesn't key moved, a keyed axis moved while playing) waited a fixed 300 ms and read what had come. A send
     is now waited for (up to a deadline, frames stepped) where one is expected, and the no-send case waits past twice the
     rate limit; the counts and the clip asset checked as before.
  2. `tests/test_livelink.cpp` "Live Link never pauses the editor (round 382)": the average / worst tick in milliseconds is
     printed, not asserted; what "never pauses" means as behaviour stays asserted (every frame's deform and pose sent, the
     clips sent while keyed). The tick's time goes to a new stress row in the `livelink` section: the same scene (four
     clients, a 100,000-vertex mesh deformed, a 50-bone rig posed, a clip keyed every frame).
  3. `tests/test_vr.cpp` "VR's arc against a big mesh (round 384 review)": "the BVH 20 times faster than every triangle" is
     printed, not asserted; the test counts the nodes a segment's box reaches in the BVH (its public nodes) and asserts a
     few - the name's "in a few node visits" - with the 40 hits agreeing as before.
  4. `tests/test_livelink.cpp` "Live Link keeps going while a long job runs (round 382)" (found by this round's own runs
     under load): the skin bind could finish and land on the first frame, before any frame sent the Box's move. The bind
     is held running (`hold_skin_job_for_test`, round 354) until the move has reached the client (10 s at most), then
     let go and checked to land.
- Out: the BVH's code, Live Link's code, any other test.

## Acceptance criteria
1. No check in these four tests compares a wall-clock time; each prints the time it measured.
2. Each test says in a comment why its time check went and what it checks instead.
3. `livelink` and `vr` pass three times in a row on two builds (Windows with Blender's libraries, dependency-free or Linux)
   while the machine is loaded (another build or suite running).
4. `blendity_stress --only livelink` has the new row.

## Tests and checks to run
- `--group livelink`, `--group vr`, three times each, two builds, under load; `blendity_stress --only livelink`.

## Risks and edge cases
- A wait for a send must still fail when nothing is sent: a deadline, not a loop without end.
- The node count must be a property of the BVH, not of this mesh's luck: a bound relative to the node total.

## Documentation to update
- `changelog.d/517-timing-checks-to-behaviour.md`; this task's write-up.

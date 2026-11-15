## 2026-11-15 (round 517): four timing checks that failed under load, made behavioural (task 0500)

**Asked** (the hub, from round 512's runs): the checks that failed only when the machine was busy - a different one each run -
follow ADR 0056: a unit test pins a behaviour, a measurement is a stress row.

**Changed (tests)**
- **Live Link's clips as things change (review 379):** it waited a fixed 300 ms and read what had come; on a busy machine the
  send came later. A send it expects is waited for (frames stepped, up to 10 s), then 0.6 s more - over twice the 0.25 s rate limit - to
  catch a second; where none is expected, the same 0.6 s. The counts and the clip's asset are checked as before.
- **Live Link never pauses the editor (round 382):** the tick's milliseconds (8 on average, 25 at most, 40 for a topology
  change) are printed, not asserted; every frame's deform and pose, and the clips while keyed, are still checked as sent.
  The time is a new row of the stress section `livelink`: four clients, a 100,000-vertex mesh deformed, a 50-bone rig posed
  and a clip keyed every frame.
- **VR's arc against a big mesh (round 384 review):** "the BVH 20 times faster than trying every triangle" is printed, not
  asserted; the test walks the hierarchy as a search does and checks that a segment's box reaches a small part of its nodes
  (the "few node visits" of its name), with the 40 hits agreeing as before.
- **Live Link keeps going while a long job runs (round 382)** (found by this round's runs under load): the skin bind could
  land on the first frame, before a frame had sent the Box's move. The bind is held running (round 354's hook) until the
  move has reached the client, then let go and checked to land.

**Checked:** `livelink` and `vr` three times each on Windows with Blender's libraries and on Linux, the two running at once
(each the other's load), 0 failed every time (365 and 116 / 118 checks); before the fourth fix, its test had failed once in
three on Windows under the same load. The BVH check: a segment's box reaches at most 75-76 of 131,071 nodes. The new
stress row (`blendity_stress --only livelink --quick`, both builds): the tick 1.05 ms on average, 2.14 ms at most on Windows.
One code review (approve; its hardening taken: the clip worker waited for before the counts, a time deadline for the
bind's landing, the test's name and comments, the stress row's null clip and its own editor).

Left for another round: "Live Link with a surface painted dab after dab (review 382)" still asserts its tick (8 / 40 ms).

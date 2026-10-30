# The think-aloud study: round 282's status

**Not run.** The protocol (`study-protocol.md`) needs six artists recruited and paid, two facilitators and a reference
2-in-1; none of that can be done from inside the code, and no participant has used this build. Nothing below is a
result: no SUS score, task time or observation from a person exists for this pass, and none is quoted anywhere.

What is ready for the first run:
- **The welcome's origin picker** (0261) is step 1 of the script: a fresh `BLENDITY_PREFS_DIR` shows it.
- **`--hci-log <file>`** (0252) writes every toast, refusal, hint change, undo label and layout change with a time.
- **The three tasks** are the walkthroughs' pipelines: Task A is `walkthrough-model-uv-material-light-render.md`,
  Task B `walkthrough-rig-animate-sequence-export.md`, Task C `walkthrough-strokes-scatter-sky.md`; each observation
  maps to a row there.
- **The predictions to test**, from the walkthroughs' after-scores: participants should get through Task B's keying
  and Task A's lighting without an assist, and should stall at Task A's step 1 (finding a cube outside the Create
  layout), step 6 (Adjust Last outside Edit Mode) and Task C's steps 3, 4 and 6 (foliage features in the console).
  A stall elsewhere is a finding the walkthroughs missed.

How to run it: per `study-protocol.md`; one folder `study-<date>/` with `p1.md` to `p6.md` and `summary.md` (SUS per
person, mean and range; median time per task; the top five observations mapped to walkthrough rows).

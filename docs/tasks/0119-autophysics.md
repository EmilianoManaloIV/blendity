# 0119: AutoPhysics

- **Status:** done
- **Requirements:** R-13
- **Decisions:** ADR 0043
- **Model:** main session (Opus)
- **PR:** (with task 0118)

## Goal
The whole clip made physically possible, as Cascadeur's AutoPhysics does it: the frames in contact (take-offs,
landings, leans) as well as the air, using task 0118's solve.

## Scope
- In: `body::autophysics_prepare` / `autophysics_finish` / `autophysics`, in three steps so the solve runs off the
  editor's thread:
  - **Prepare (editor thread, it poses the scene):** samples the clip, the center of mass and momentum per frame, and
    each contact as its effector's tip (its mean over the contact).
  - **Solve (anywhere):** `solve_centroidal`, which can be cancelled.
  - **Finish (editor thread):**
    - the hips keyed every frame, moved by the center's correction and turned about it by the momentum's;
    - the turn left at the end taken back over the frames in contact only (nothing could turn the body back in the
      air), so the last pose is the animator's;
    - the feet's and hands' targets held during their contacts, following the body again over 4 frames after;
    - frames green, blue or red as a flight's;
    - nothing is written when a frame would move more than 1 m.
- In: the Animation window's **AutoPhysics** button ("Solving..." while the worker runs). It shares Flight's ghost,
  frame colours and **Apply** (one undo step). A result is dropped if the clip, or the character, changed while it was
  solving. The console: `body physics show | apply | off`.
- In: a Learn section, "AutoPhysics: the whole clip".
- Out:
  - frame weights per frame (Cascadeur's): all frames weigh the same; the setting exists in `CentroidalInput`;
  - heel and toe contacts for a foot's support polygon;
  - settings in the UI (friction and the weights keep their defaults).

## Acceptance criteria
1. A jump keyed in straight lines comes back with a parabola in the air and the foot held through its contacts, and
   the keyed clip realises the solved center.
2. A clip that is already possible comes back green and unmoved. Missing contacts, or contacts on bones that aren't IK
   ends, are explained. A solve can be stopped.
3. In the editor:
   - the solve runs while the editor keeps drawing frames;
   - the result is shown without being written, and Apply is one undo step;
   - a clip edited during the solve isn't shown;
   - Off stops the solve.

## As built
- After the review:
  - the editor keeps drawing frames while a solve runs, so its result is picked up without input;
  - a result arriving in Play waits until Play ends;
  - a result for a clip gone with its scene (undo, another scene) is never shown;
  - Flight stops a solve under way;
  - a contact pushes at its tip's mean position over the contact.
- Tests: round 138. The jump: the body moves up to 0.33 m (the keyed peak was higher than its 0.67 s in the air
  allows), 27 frames blue, none red. The flight falls at g, the keyed clip realises the center exactly, and the foot is
  held to 1e-5 m. Standing: 0.08 mm, all green. In the editor the solve finished after 5 editor frames.

# 0126: Footsteps along a path

- **Status:** done
- **Requirements:** R-13
- **Decisions:** ADR 0045
- **Model:** main session (Opus)
- **PR:** (stacked on tasks 0124, 0125)

## Goal
A character walked along a curve on footprints, as 3ds Max's Biped footsteps: prints laid along the path, landing on
the ground under them, each one moved and turned by hand, and the body solved from them as ordinary keys.

## Scope
- In: `steps::plan(rig, style, path, fps, ground, keep)`:
  - prints alternate along the curve at half the style's cycle length, both feet together at the start and the end;
  - the first and last steps shorter (starting, stopping), steps shorter in turns (down to half at 90 degrees over a
    step), each foot to its own side of the path;
  - each print on the ground under it (`scene_raycast` over mesh renderers, the character, its prints and the path
    skipped), turned with the slope;
  - hand-moved prints (by side and landing frame) kept.
- In: `steps::walk` and `pose_on`: each foot planted on its print while down and arcing to the next (higher over a
  step up), with the heel-toe roll of Walk Cycle; the pelvis over the stance feet at their height, kept within every
  foot's reach, with the gait's sway, bounce, twist and arm swing; keyed through `gait::bake` with contacts.
- In: the `Footprint` component (side, frame; where it was planned) on an object per print, under an object with the
  `Footsteps` component (the character, the path, the style). Saved with the scene.
- In: the Rigging window's **Footsteps** section (Path, Walk Along Path, Update, Re-plan), the prints drawn in the Scene
  view (blue left, red right, the landing frame), `walk path <curve> | walk steps update | walk steps replan`. With a
  print selected, Update and Re-plan act on its character. All only with the Footsteps assistant on.
- Out: four-legged footsteps (Walk Cycle covers quadrupeds), turning on the spot, side steps.

## Acceptance criteria
1. A straight path: every foot lands on its print within 1 mm; planted feet don't slide; the first and last steps
   are short.
2. A curved path: the inner foot's steps are shorter; no print on the wrong side.
3. Box stairs: every print on a step's top, the body rising.
4. In the editor: refused while off; a hand-moved print survives Re-plan and the walk follows it; one undo step; saved
   and loaded with the scene.

## As built
- Tests: round 146.
- Update walks every print as it is, so a moved print changes the frames around it only: each frame's pose depends on
  the prints the feet are on and going to, not on the rest.
- After review:
  - a print dragged beyond the leg's reach pulls the pelvis towards it (the foot lands on it: 0.0 mm at 0.8 m out);
  - a print only turned or retimed is the animator's too, and Re-plan keeps a moved print after the style or path
    changes (matched to the nearest new print of its foot, not by frame; one with none near is reported);
  - the clip is made before the prints are replaced (a failure changes nothing); two objects with the character's
    name are refused (prints find their character by name, as clips and IK targets do); a renamed path is reported;
  - a first print retimed past frame 0 is stood on until then; prints retimed closer than a swing start the swing
    when the foot lands;
  - the ground grid's cells grow with a huge ground (at most about 256k cells).

# 0280: the learned pose prior

- **Status:** done (round 300)
- **Requirements:** R-08
- **Decisions:** ADR 0087
- **Model:** main session
- **PR:** #230

## Asked
Task 0279's request: a learned model that poses the whole body from a dragged hand, foot, hips or head, under exact
IK (the user's choice).

## What changed
- **`tools/posemodel/train.py`**: from the CMU data the inbetweener uses (cmu30.npz, 22 joints, 30 fps; subjects 5-9
  held out), every frame made canonical (the hips over the origin facing +Z) and a random set of its goals hidden: the
  hands, the feet, the head relative to the hips and the hips' height, any subset. A residual multilayer perceptron
  (4.85 million parameters) answers every joint's turn as 6D. Trained here for 40,000 steps (26 minutes on an RTX 4070
  SUPER); exported to ONNX (19 MB) with `posemodel.json`.
- **Measured on the held-out subjects** (`runs/full/result.json`):

| goals given | joint error | goal error |
|---|---|---|
| all | 2.0 cm | 0.8 cm |
| hands and feet | 2.9 cm | 0.85 cm |
| feet and the hips' height | 7.3 cm | 0.6 cm |
| one hand | 10.3 cm | 0.4 cm |
| none (the mean pose) | 13.1 cm | - |

  A pose takes 0.13 ms in ONNX Runtime on the CPU (0.28 ms through Blendity, the fastest of five).
- **The runtime** (`src/scene/inbetween.*`): the inbetweener's rig mapping made public (`pose_mapping`,
  `rig_to_frame`, `frame_to_rig`, `world_to_data`), and `load_prior` beside `load_runtime` (one thread: a pose is too
  small for more). The prior's files belong to the AI download (`own_file`), so an install or a removal handles them.
- **Puppet Posing** (`src/scene/puppet.*`): when the dragged bone is one of the prior's ends (a hand, a foot, the head, the
  hips) the goals are made canonical, the prior's pose put on the rig, then AutoPose's solve runs from it with the pins
  held as pins where they were, place and turn (`autopose::Settings::pin_poses`), and a hand nobody dragged as a light
  goal where it was - the goals met to the millimetre, the rest bending as the data's bodies do. An elbow or the spine
  dragged, or no prior: AutoPose alone, as before.
- **In the drag** (`autopose_ui.cpp`): the prior loaded on a thread the first time AutoPosing is used (a session takes
  a moment to make), then used by every drag. At 0.3 ms a pose it runs on the drag itself: a worker would cost more
  latency than it saves (ADR 0087 said a worker; the measurement decided).
- **FK controls**: a bone whose FK control turns it gets the control turned with it, and the control is keyed instead
  of the bone (the control sets the bone's turn every frame: before, the pose snapped back).
- **The AI download**: `tools/inbetween/package.py --prior <run>` adds the prior to the archives. Built here; publishing
  them stays the maintainer's decision.

## Tests (`tests_round300`, animation group)
A stand-in prior answering the rig's own pose puts it back to 0.00 mm (the canonical frame and back are exact); with it
a dragged hand lands 0.55 mm from its goal with the feet 0.07 mm off; a goal 1.2 m past reach leaves the feet 0.31 mm
and 0.00 degrees off; without a prior, AutoPose alone, said. A spine with FK controls posed by dragging the head keeps its
pose after the FK solve (0.000 degrees off), the controls keyed, the bones not. With the trained prior
(`BLENDITY_AI_TEST_INSTALL`): loaded, 0.27 ms a pose, a hand to the knee 0.16 mm off, the feet 0.47 mm off, the spine bent
15.8 degrees.

## After the review (Opus)
The pins had become ordinary goals (the feet could slide on a far reach): now pins with their place and turn from
before. Every drag went through the prior, an elbow's too: now only the prior's ends. The FK control was found by name
anywhere in the scene and even when switched off: now in the bone's own character, enabled. A failed load is said once;
`autopose prior on|off` and the Puppet menu's Use the Pose Prior switch it.

## Not done
- The prior isn't in the published AI download until the maintainer uploads the new archives.
- Fingers keep their keys; the prior poses the body.

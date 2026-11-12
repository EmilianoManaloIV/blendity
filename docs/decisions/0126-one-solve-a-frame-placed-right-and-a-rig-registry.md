# ADR 0126: One constraint solve a frame, after the tools and before the views; rig lookups from a registry

- **Status:** accepted; decision 1 built in round 472 (task 0448), decisions 2 to 4 not built - see "As built" (approved by the user 2026-11-10: "I approve all specs from all passes")
- **Builds on:** ADR 0115 (the scene's change counter), ADR 0124 (`below_version`), task 0048 (the per-frame IK pass)
- **Requirements:** Q-04 (interactive editor), Q-06 (responsiveness)

## Context
Round 468 measured two things about how a rig is solved and looked up (docs/PERFORMANCE.md, "Pass 22"):

- **The pose is drawn a frame late.** `Editor::frame_body` solves the constraints (editor.cpp:631) and then draws
  the windows; the Scene view's tools - the gizmo on an IK target, G / R / S - move their object while the view is
  drawn, after that solve. The view shows the handle where the mouse is and the limb where the solver had it one frame
  earlier: solving once more after a frame of an IK-target drag moves the hand by up to LATE_MM mm, on LATE_FRAMES of
  the frames.
- **The solve itself is cheap** (CONSTRAINTS_MS ms a frame on the owner's avatar: 28 VRChat constraints, NAME_WALKS
  name lookups a frame that walk the hierarchy; with IK handles added IK_MS ms) - the plan's whole-scene walks are
  behind live counters and cost little. What costs are the lookups AROUND it, each a walk of every object, asked per
  bone or per frame: `ik_drives` (per bone by Record and by Preview, by G / R and the gizmo's press), `ik_driven_bones`
  (every frame by the rig overlay: OVERLAY_MS ms), `autopose_pins` (up to three walks with string work, every frame
  while AutoPosing is on: PINS_MS ms), `pole_joint` per pole drawn, `fk_control_of` / `humanoid::fk_control` per bone.
- Round 468 also fixed a crash that came from a rig cache keyed by value: an undo swaps the scene for a copy.

## Decision
1. **The solve follows the tools.** A frame solves the constraints where it does today (animation and scripts have
   written by then) and again right after a view's tools have run and before that view gathers what it draws, when
   a transform under a rig moved in between (`below_version` of the rig's root, ADR 0124). The second solve is the
   keyed one: it costs a comparison when nothing moved. The picture then shows the pose solved for the target it
   shows.
2. **A rig registry on the Scene** (`Scene::rigs()`), built on first use and rebuilt when the scene's structure
   changes (a structure stamp bumped by adopt, destroy, reparent, rename, add / remove component - not by transforms)
   - never kept across a Scene (`serial`): the armatures and their bones; the IK, FK-control and VRChat constraints
   with their targets, poles and sources resolved to objects; each control's bone, each pole's joint, each bone's FK
   control; the bones an IK chain turns.
3. **Asked from the registry**: `solve_constraints` (no `for_each`, no name walk once resolved), `ik_drives`,
   `ik_driven_bones`, `pole_joint`, `fk_control_of`, `humanoid::fk_control`, `autopose_pins`, and Play's scans for
   springs, locomotion and skins.
4. **At rest a constraint reports no move**: a tolerance on what it writes that holds far from the origin (round
   468's "does an untouched window settle" rows stay 0 at 5,000 m).

## Consequences
- A dragged IK target and its limb are drawn together (LATE_MM mm -> 0).
- The lookups around a solve stop walking the scene: Record, the rig overlay and AutoPosing's pins lose their walks
  (the numbers are task 0448's and 0450's).
- A structural change rebuilds the registry (one walk of the scene: REGISTRY_MS ms on SideRail with the avatar);
  posing never does.
- A kept pointer into the scene is valid only for the Scene it was made for and while the structure stamp holds; the
  registry is the one place that rule lives for rigs.

## As built (round 472)
- **Decision 1 is built for the Scene view's tools**: `Editor::solve_after_tools` runs right after
  `scene_tool_updates` when `edit_epoch::transforms()` moved since before them, not in Play, and counts as a move
  (`bump_moved`). It is a plain comparison on the scene's transform counter, not `below_version` of each rig's root:
  `solve_constraints` already costs a hash per chain when what moved drives nothing. On the stand-in, before: the hand up to
  16.5 mm behind its target on 30 of 35 frames of a drag; after: 0.000 mm on all 35. The second solve doubles the
  constraints on a drag (0.24 -> 0.48 ms a frame on the owner's avatar). A drag in the
  Inspector is drawn in another window and is still a frame late.
- **Decisions 2 to 4 (the registry, its users, the tolerance at rest) are not built.** Round 471's runs put the
  constraints at 0.2 ms a frame on the owner's avatar (46 to 92 name walks a frame), under the 0.3 ms line task 0448
  set for building a part. The lookups around the solve that were measured over it (`ik_driven_bones` in the rig
  overlay, `autopose_pins`) belong to task 0450.
- **What round 472 keeps instead, beside this ADR's subject:** a clip's bindings to objects (`bind_clip`), kept on
  the clip per root while the clip's `layout`, the track count, the structure stamp less the applies' own bumps and
  a signature of every object under the root hold. A cache keyed by value across scenes was round 468's crash: these
  hold raw object pointers only while the structure stamp says no object was made or destroyed, and the signature
  catches a name written past it.

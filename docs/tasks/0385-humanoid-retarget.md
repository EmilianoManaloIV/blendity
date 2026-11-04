# Task 0385: Humanoid retargeting like Mecanim (round 408)

- **Status:** built
- **Requirements:** R-unity-parity (the 2026-11-03 plan: round 408, moved from the earlier 403), Q-tests, Q-docs
- **Decisions:** ADR 0103
- **Model:** main session (Fable)
- **PR:** stacked on #306 (round 407)

## Asked
A clip made on one humanoid rig played by another, as Unity's Mecanim moves a Humanoid clip between avatars: by what
each body part does, not by bone names.

## Built
- **`retarget_clip`** (`src/scene/retarget.*`): both rigs' parts from `humanoid::map` (the 21 parts Puppet Posing
  knows, on any naming scheme); at every frame of the source clip (sampled at its rate; the source rig posed by the clip
  and put back) each part's turn from its rest, in the rig's own space, is given to the target's part over its own rest -
  the parent part's pose below it, the rests of bones between (twist bones) kept - and keyed as the target bone's local
  Rotation; the hips' move from their rest scaled by the two rigs' hip heights and keyed as Position. Both rigs are measured in
  their rest (every bone put at its rest first, then back - the map reads a rig as it stands, and a rig the clip had
  posed would measure as that pose). An A-posed rig's arm drop against a T-posed one's (`humanoid::Map::arm_drop`) is
  taken out of the upper arms' turn about the body's forward axis - Mecanim's semantics: a T-posed clip's rest frame
  means arms horizontal, so an A-posed target's arms rise to horizontal there, and an A-posed clip's rest hangs a
  T-posed target's arms. Parts the target lacks, and the source's blend shape
  tracks, are left out and said.
- **The Animation window's Retarget menu** (`Editor::retarget_targets`: the scene's other humanoid rigs, by their
  character root) and **`anim retarget <object>`** (`Editor::retarget_open_clip`): the open clip onto the rig picked - a
  clip of the target's own saved beside the source's as "<clip> (<target>).anim", set on the target's Animation
  component and opened in the window on it.

## Acceptance criteria
1. A Mixamo-named T-pose rig's clip (an arm raised, the head turned, the hips moved and turned) onto a VRoid-named rig
   twice its size: every part found by its own name, the parts' turns from their rests equal in the rigs' spaces, the
   hips' move doubled, the target's own paths, the rate and length kept.
2. A T rig's rest frame onto an A rig puts the A rig's arms horizontal (and an elbow's bend carries); the A rig's rest
   frame onto the T rig hangs its arms by the A drop - Mecanim's reading of the two poses; both rigs measured at rest
   whatever pose they stood in.
3. The editor: the Retarget menu lists the other rigs; `anim retarget` makes, assigns and opens the target's clip.
4. The suites, the audits.

## Review (Opus) and what it changed
Each rig gets a body frame at rest (up from the hips to the head, right across the upper legs, forward across them), so
a turn is carried from the source body's frame to the target body's whatever way either armature is turned (a Blender
export's -90 about X, a VRM 0.x facing -Z); the hips' height is above the lowest foot or toe, so a BVH whose hips rest
at the origin lands on the floor; the A/T fix turns the target's whole arm at its rest (the elbow had kinked); the
parent pose comes from the target's real hierarchy (the nearest ancestor posed this frame, the rests of the bones between
kept); each part's Euler keys the turn nearest the frame before; only the clip's
bone tracks pose the source while sampling (its blend shapes and components untouched); the keys reduced to what the
curves need; the frame loop capped; the Retarget menu's targets found once as it opens, by the character root, the
clip's own rig and a root holding it refused; the editor test keys by the bone's name (it had keyed nothing).

## Not done
- Mecanim's muscle space (per-bone axes and limits): the turns are carried as they are, so a rig whose bones roll
  differently about their length from the source's (a Rigify rig against a Mixamo one, say) carries the roll with the
  turn. The humanoid map's parts only: fingers, eyes and jaw keep their keys; IK feet (foot planting) are not solved.
- FK control rigs and constraints are not solved while sampling: a clip keyed on Rig Controls (Puppet Posing's) carries
  nothing - bake it to the bones first.
- Unity's Avatar asset (the human description's bone list) is not read: the parts come from the names and the rig's
  shape, as Puppet Posing finds them.

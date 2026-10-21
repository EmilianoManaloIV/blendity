# 0153: Realistic hair and soft-tissue physics for Spring Bones

- **Status:** done (round 171: strands, wind, fitted colliders, separation; round 172: soft tissue, jiggle, hair
  from a mesh)
- **Requirements:** R-13
- **Decisions:** ADR 0049
- **Model:** main session
- **PR:** #116 (round 171, stacked on #115), #117 (round 172)

## Goal
The user: realistic hair and soft-tissue physics (breasts, thighs, buttocks, belly) on imported characters, research
based. Hair that keeps its curl and parts into strands, moves in the wind and stays out of the body; soft tissue
that trails the body's acceleration, stays in a cone and keeps its volume.

## Scope
- In (round 171): `SpringBone::bend`, `root_to_tip`, `wind`, `separate` (reflected, saved, label defaults); the bend
  constraint and the root-to-tip fade in `step_chain`; the `Wind` component (`strength`, `turbulence`, `gusts`,
  `spherical`; the object's forward or its position; Add Component > Scripts; a gizmo line in the Scene view's
  icons is out); `body_colliders` / `fitted_radii` from the skinned meshes' vertex groups, cached per armature; strand
  separation over every stepped chain with a uniform grid, the rotations written after it; the lesson "Strands".
- In (round 172): the "Soft" label (acceleration-driven lag, a cone limit, volume by counter-scale; `lag`, `limit`,
  `volume` fields beside Stiffness and Damping, which are the spring); the soft words in `secondary_label`; the
  `JiggleModifier`; `hair rig` on the console and Rigging > Rig Hair from Mesh; lessons "Soft tissue" and "Rig
  hair from its mesh".
- Out: a Cosserat twist constraint; self-collision inside a strand; a ground plane; the collider overlay.

## Acceptance criteria (round 171)
1. Segment lengths stay within 0.1 % under a 2 m/s move and stop (the old chain's) with bend 0.5.
2. A curled chain (each bone 40 degrees to the one before) with bend 1 under gravity keeps its curl (the tip within
   10 % of the curled hang's distance from straight down); with bend 0 it hangs within 2 % of straight.
3. root_to_tip 1 lags the tip more than root_to_tip 0 under the same move; the root bone's lag is the same.
4. A Wind of 5 m/s along +X deflects a hanging chain's tip by over 5 cm toward +X; 0 m/s doesn't; a chain's wind 0
   doesn't; turbulence varies the deflection over time (its standard deviation over 2 s above 1 mm).
5. A skinned tube of radius 0.2 m on a 1 m bone gives a fitted capsule of radius within 10 % of 0.2 m, where the
   old seventh gave 0.143 m; a chain hanging into the tube rests outside it.
6. Two parallel chains 1 cm apart with radius 2 cm and `separate` end at least 3.5 cm apart; without `separate`
   they stay 1 cm apart.
7. The same hang at 30, 60 and 144 fps within the old tolerance with bend, wind and separation on; a bake twice
   gives the same keys; no NaN after 1,000 random 10 m jerks.
8. The settings and the Wind survive saving and loading.

## Acceptance criteria (round 172)
1. A Soft bone on a body bobbing 5 cm at 3 Hz trails its animated place by over 2 mm more than with Lag 0, stays
   within its Limit (25 degrees; 5 with Limit 5), keeps the product of its scales within 1 % of 1, and is within
   1 mm of its place a second after the bobbing stops; a reset puts its scale back to 1; Lag, Limit and Volume
   save and load.
2. A Jiggle modifier on a cube's top half moves it under a 4 Hz shake by up to its Limit and not past it, moves
   nothing of the bottom half, and is at rest a second after.
3. Twelve hair cards rigged give twelve chains of three bones under a hair rig, the mesh skinned to them with
   every vertex's weights summing to 1, undone in one step; a cube cut into four locks gives four chains of two.
4. The soft names: Breast_L, chest_jiggle, Belly, ThighSoft.L, 左胸 and J_Sec_L_Bust1 read as Soft; the body's
   bones (Chest, Hips, LeftUpLeg) still as the body's; 尻尾 a tail, 胸リボン cloth.
5. A landing (a 10 cm drop over three frames) throws a Soft bone further down with Lag 0.5 than with Lag 0 by
   over 3 mm; a body already moving at 1 m/s when Play starts gives no kick (Lag 1 no further off than Lag 0 in
   the first quarter second).
6. A two-bone soft chain keeps its first bone's scale at 1 and stretches its last; a bake writes that stretch as
   Scale keys and gives the scale back; Volume 0 lets the product of the scales follow the stretch.
7. 100 chains of 3 bones (every tip within its neighbours' radii) step under 0.5 ms a frame with separation and
   collision in a release build.
8. The Jiggle modifier evaluated for both views at one clock gives one offset; a half-weighted vertex moves half.
9. A skinned hair mesh rigs under the character's Head bone (found by the whole word or 頭) with the rig scaled by
   2: the chains' tips end at the cards' tips, the body's group is gone, every weight sum is 1, the hair bones see
   the body's capsule; a posed head is refused; forty cards into eight locks.

## After the review (round 172)
- The Jiggle modifier reset itself when the Scene and the Game view evaluated it at one clock, squared the group's
  weights (the stack masks a deform modifier by its group already), jiggled the whole mesh when its group was
  missing, and didn't run with the Physics assistant off: fixed; Springiness renamed Stiffness; its help says it
  isn't baked.
- The soft drive read a speed as an acceleration on its second step (a kick when Play starts on a moving body)
  and kept its memory through a teleport; the counter-scale went on every bone of a chain (shearing children) and
  the base scale was captured once (an animated scale was overwritten; a relabelled chain stayed stretched); the
  bake dropped the stretch. Fixed: primed over two steps, forgotten on a jump, the last bone only, the base
  re-taken when something else sets the scale, `unstretch` on relabel / removal, Scale keys baked. Soft chains
  default to no collision (the torso's fitted capsule sat on the bust bone).
- The names: 尻 matched 尻尾 and 胸 matched 胸リボン (now the tail and the ribbon come first); MMD's Japanese branch
  had no soft case.
- The hair rig: `armature_of` stopped at the nested hair armature (no colliders), bone lengths were in world units
  (wrong under a scaled armature), the rest was taken from a posed head, a skinned mesh with no head found was
  re-parented to nothing, the old armature's groups stayed (weights summed to 2), every card became a lock no
  matter how many (a 400-card style gave 1,200 bones), the envelope bind weighted cards to their neighbours'
  bones, bone names repeated across meshes, "head" matched Forehead. All fixed; the function split into
  `assign_locks`, `lock_joints`, `build_lock_chain`, `bind_hair_skin`.
- Speed: `is_body_bone` cached by name, one chain walk a step, the separation pass cell by cell on a sorted array
  with each pair once by squared distance, over a dense grid of the tips' box (8.6 ms to 0.4 ms a frame for 100 chains of 3 bones; 1.7 ms for ten-bone chains).
- The spec's `mass` / `springiness` landed as `lag` with Stiffness and Damping as the spring, on the chain and the
  Jiggle alike (the scope line says so now).

## After the review (round 171)
- The bend held a world-space rotation (a curl unwound out of its plane): the animated angle is carried through the
  simulated bone's swing; no bend after a zero-length bone; Root To Tip fades by length along the chain.
- Separate fought the animated pose (hair roots sit closer than their radii) and ran after collision: the animated
  distance is the limit, the capsules are kept after the push, NaN tips are skipped.
- The fitted radii: one bone per vertex in one pass (the old rule double-counted ties and cost bones x vertices x
  groups), the narrowest skin per bone, the skin's armature resolved as the skin resolves it, a key over the bones'
  rest / names / lengths and the skins' ids, versions and relative matrices (it went stale on undo and rest edits).
- The wind's clock is per scene and a bake runs its own (one global clock ran every scene's wind at once and a bake
  moved Play's); the drag converges at any substep length; the gusts are worked out once a substep.
- `separate_strands` and `fitted_radii` split into shallow helpers; `closest_on_segment` and `push_out_of_capsules`
  shared by the step and the separation; `ChainStep` returned by the step instead of recomputed.
- Tests added for the lengths under a move, a curl held out of plane, strands laid out closer than their radii, the
  hair defaults, the frame-rate tolerance restored to the old one, and every Wind field saved.

# 0153: Realistic hair and soft-tissue physics for Spring Bones

- **Status:** in progress (round 171: strands, wind, fitted colliders, separation; round 172: soft tissue, jiggle,
  hair from a mesh)
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
- In (round 172): the "Soft" label (acceleration-driven lag, a cone limit, volume by counter-scale; `mass`,
  `springiness`, `limit`, `volume` fields); the soft words in `secondary_label`; the `Jiggle` deform modifier;
  `hair rig` on the console and Rigging > Rig Hair from Mesh; lessons "Realistic hair" and "Soft tissue".
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

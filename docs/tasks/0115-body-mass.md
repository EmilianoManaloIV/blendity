# 0115: The body's masses and center of mass

- **Status:** done
- **Requirements:** R-13
- **Decisions:** ADR 0042 (new)
- **Model:** main session (Opus)
- **PR:** (with tasks 0116, 0117)

## Goal
The user asked: "Is there a way to implement body simulation for animation like in Cascadeur." Everything Cascadeur's
physics does starts from the body's masses: weigh each bone, then follow the center of mass through a clip.

## Scope
- In: `body::build`:
  - each deforming bone's mass, center and inertia from the volume of the skinned mesh it moves, in tetrahedra
    against the bone's own middle, shared by the corners' weights;
  - capsules round the bones without a skin;
  - scaled to `Armature::body_mass` (75 kg, in the Inspector).
- In: `body::state` (the center of mass and the whole body's inertia at any pose, the rig root's motion included) and
  `com_trajectory` / `for_each_frame` (a clip posed frame by frame with IK solved, the scene put back after).
- In: the Animation window's Center of Mass button: the path over the clip, where it is at the playhead, the point
  below it. The console's `body show on|off`, `body mass [kg]` (the heaviest bones listed), `body com [frame]`.
- Out: per-bone mass overrides; volumes of meshes that aren't closed (their bones weigh what their open shells close
  to).

## Acceptance criteria
1. Two closed boxes skinned to two bones weigh 25 and 50 of 75 kg, with their centers and box inertias.
2. The center of mass and the whole inertia follow a pose (parallel axes); no skin gives capsules on the axis.
3. A clip's trajectory follows the rig's motion; the scene is as it was afterwards.

## As built
- Tests: round 137.

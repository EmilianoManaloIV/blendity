# 0120: Secondary motion: spring bones, found automatically

- **Status:** done
- **Requirements:** R-13
- **Decisions:** ADR 0042 (corrections written as keys)
- **Model:** main session (Opus)
- **PR:** (with tasks 0118, 0119)

## Goal
Hair, ears, tails and cloth that follow the body: they lag, swing past and settle, without being keyed by hand. The
user asked for the chains to be found automatically:
- "auto select hair bone, ear bone, cloth bone sequences automatically. Or recursively if I select a parent bone that
  is the start of that specific chain";
- "make sure that I can also deselect or uncatogorize the chains if it's unneeded".

## Scope
- In: the **SpringBone** component on a chain's first bone.
  - **Fields:** a label (hair, ear, tail, cloth, each with its own defaults), stiffness, damping, gravity, radius and
    collide.
  - **The chain:** the run of single children below its first bone, up to a fork, a body bone or another chain.
  - **The step (`spring::step`):** verlet tips pulled towards the animation's pose, with lengths kept, and kept
    outside capsules along the body's bones. Substeps are at most 1/120 s, and stiffness and damping are per 1/60 s,
    so any frame rate behaves the same.
- In: **live in Play**, stepped by the editor after IK (the pose as drawn), starting afresh when Play starts or the
  chain's start jumps far (a teleport).
- In: **Bake into Clip** (`spring::bake`).
  - The clip is simulated frame by frame from each chain bone's rest, with its own rotation keys set aside, so baking
    again gives the same keys.
  - The chain bones' rotations are keyed, and the chains' live simulation is turned off.
  - One undo step.
- In: **finding chains**.
  - **Names:** a bone's name is split into words: the namespace dropped, and split at separators, case changes and
    digits.
  - **Body bones:** spine, limbs, hands, feet, fingers, toes, head, eyes, jaw (Mixamo, Unity, Rigify, VRM `J_Bip_`).
  - **Secondary parts, by label:**
    - hair, including VRM's `J_Sec_`, `kami` and `ahoge`;
    - ear, including `mimi`;
    - tail, including `shippo` and tentacles;
    - cloth: skirt, cape, coat, sleeve, scarf, ribbon, strap, chain, rope, wing, jiggle, dyn, phys and spring, and
      `sukato`.
  - **Find (`spring::find`):** every chain starting at a non-body bone that hangs off the body.
    - A fork's bone is the strands' shared root: each branch is its own chain.
    - A chain's label is the first name along it, or above it up to the body, that says one.
    - Named chains are ticked; unnamed extras of two bones or more are listed unticked; one-bone extras aren't
      offered.
- In: **the user's choice wins.**
  - Untick a chain, change its label, or set it to **None**.
  - **None or Remove** marks the chain's first bone "Not a Spring Chain" (a Bone field), so Find doesn't offer it
    again. **Show Ignored** lists those chains and **Unignore** brings one back.
  - **Add Spring Bones** on a selected bone works recursively. Its chains are found as above. On a body bone, it takes
    the named chains below it.
  - **Remove** on a chain, any of its bones, or a parent takes the springs off everything under it, in one undo step.
- In: the Rigging window's **Spring Bones** section, the console's `spring find [all] | apply |
  tick <bone> on|off | label <bone> hair|ear|tail|cloth|none | add <bone> [label] | remove <bone> | ignore <bone> |
  unignore <bone> | bake | list`, and a Learn section.
- Out: collision with the skinned mesh itself (the bones' capsules stand in), wind, and chains joined sideways (cloth
  as a sheet).

## Acceptance criteria
1. Names are read right across rig conventions; Find on a Mixamo body with hair (a root and five strands), ears, a tail
   and a skirt picks exactly those, labelled, with an unnamed extra unticked and no limb, finger or toe.
2. From a selected hair root: five chains; a body bone under it is left out.
3. At rest a chain stays at rest; a hip that stops makes the tail swing past and settle; collision keeps it out of a
   thigh (and without it, it goes through).
4. Baking twice gives the same keys; the scene is as it was after.
5. In the editor: an unticked or None chain gets no spring; a None chain isn't offered again until Unignore; Remove on a
   parent is one undo step, undone whole; a label changes the settings; labels and the ignored mark are saved.
6. Springs run in Play; a baked (disabled) one doesn't.

## As built
- Tests: round 139. A stopping hip swings the tail 1.7 cm past at 30, 60 and 144 frames a second alike: the chain's
  start is moved across the substeps, not all at once.
- After the review:
  - Find's rows keep bone ids;
  - one capsule list per armature, without any chain's bones;
  - springs step after IK;
  - Add keeps tuned and None chains, and takes a middle bone's whole chain;
  - None takes off only its own chain;
  - names are resolved in the selected skeleton;
  - bakes include disabled springs and pick Euler angles continuously;
  - zero-length bones are held;
  - teleports restart the chain.

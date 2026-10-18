# 0042: The body for physics in animation: masses from the skin, corrections written as keys

- **Status:** accepted (tasks 0115-0117; the user asked for "body simulation for animation like in Cascadeur" and chose
  the center of mass and ballistics, the full AutoPhysics solve, secondary motion, an active ragdoll and AutoPosing)
- **Date:** 2026-10-18
- **Requirements:** R-13

## Context
Cascadeur's physics mostly corrects keyframed animation. It weighs each body part, follows the center of mass, makes
flight ballistic and keeps angular momentum. It shows the corrected motion as a ghost, with frames green, blue or red,
before the animator applies it. Blendity's characters are armatures with skinned meshes and IK limbs, animated by
clips of Position / Rotation / Scale keys per bone.

## Decision
- **Masses come from the skin.** Each deforming bone weighs the volume of the skinned mesh it moves:
  - each triangle forms a tetrahedron with the middle of each bone its corners follow, shared by their weights;
  - each bone's own middle, rather than the mesh's, keeps the error at a limb's open seam small;
  - the whole is scaled to `Armature::body_mass` (75 kg by default);
  - each bone's center and inertia come from the same tetrahedra;
  - an armature without a skin gets capsules round its bones (a seventh of each length in radius).
- **The body is fixed when it is built.** Each bone's rest world matrix is taken then, and a pose is measured as the
  bone's world matrix times that matrix's inverse. That way the rig root's own motion counts too.
- **Contacts live in the clip** (`contact` lines in the .anim):
  - found ones are replaced when contacts are found again; ones set by hand are kept;
  - pinning keys the IK target of a limb in contact; an FK end is reported, not pinned.
- **Corrections are ordinary keys.** A flight's correction is worked out as a copy of the clip:
  - the hips' Position and Rotation, and the IK targets outside the hips, keyed on every frame of the flight plus
    its take-off and landing frames;
  - the copy is shown as a ghost and in frame colours;
  - Apply replaces the clip's tracks with it as one undo step.

  Nothing is simulated at playback: the clip stays the animator's to edit.
- **Flight physics:**
  - the center of mass follows the parabola under gravity through the take-off and landing centers, at their times;
  - the body turns about its center so the angular momentum stays the one of the first frame in the air
    (Δω = I⁻¹(L₀ − L), integrated);
  - by default the turn left at landing is spread back over the flight, so the landing pose is the animator's;
  - a take-off over 15 m/s, or a correction over 1 m, is marked impossible (red) and not written.

## Consequences
- Physics needs no new dependency and no new data beyond contacts. Undo, saving and the dope sheet work unchanged.
- A body is rebuilt when the clip, the mass or a skinned mesh changes (the overlay's key); editing the rest pose
  needs the body rebuilt (it is, on the next change of those).
- Keeping the landing pose trades exact momentum for an unchanged landing. The settings can turn that off.
- The later tasks (the full solve, spring bones, the ragdoll, AutoPosing) build on this body and these contacts.

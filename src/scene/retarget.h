// SPDX-License-Identifier: GPL-2.0-or-later
// Humanoid retargeting (round 408, task 0385; ADR 0103): a clip made on one humanoid rig played by another, the way
// Unity's Mecanim moves a Humanoid clip between avatars - by what each body part does, not by bone names. Each rig's
// parts come from humanoid::map (hips, spine, chest, neck, head, shoulders, arms, hands, legs, feet, toes); at every
// frame the source part's turn from its rest is carried from the source body's frame to the target body's (each rig's
// up from hips to head, right across the legs, whatever way its armature is turned) and given to the target part over
// its own rest; the hips' height above the floor and their moves are scaled by the two rigs' hip heights; an A-posed
// rig's arm drop against a T-posed one's turns the target's whole arm at its rest, so a T-posed clip's rest frame means
// arms horizontal on either rig, as Mecanim reads it.
#pragma once
#include "animation.h"
#include <string>
#include <vector>

namespace bl {

struct Scene;
struct GameObject;

struct RetargetResult {
  int parts = 0, frames = 0;  // the parts carried (of the source's found ones), the frames sampled
  float hip_scale = 1.0f;     // the target's hip height over the source's: the hips' moves are scaled by it
  float arm_drop_src[2] = {0, 0}, arm_drop_dst[2] = {0, 0};  // (the humanoid map's: each rig's upper arms below the horizontal as it stands)
  std::vector<std::string> notes;
};

/* `src` played by `src_root`'s rig onto `dst_root`'s rig as `out` (the target's own paths; Position for the hips and
 * Rotation for every part found on both rigs, sampled every frame and reduced to the keys a curve needs; the clip's
 * rate, length and loop kept). Both rigs are measured at their rests and put back as they stood; only the clip's bone
 * tracks pose the source while sampling. False with a note when either root has no humanoid rig, or the two are one.
 * Not carried, said: tracks that aren't bone turns or moves (blend shapes, components), parts the target lacks; FK
 * control rigs and IK are not solved while sampling (a clip keyed on Rig Controls carries nothing). */
bool retarget_clip(const Scene &scene, const AnimationClip &src, GameObject &src_root, GameObject &dst_root, AnimationClip &out, RetargetResult &r);

}  // namespace bl

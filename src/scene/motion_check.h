// SPDX-License-Identifier: GPL-2.0-or-later
// Motion Check, the smooth brush and Lock in Place (task 0284, ADR 0087): what Marionette's clean-up tools do, on
// Blendity's clips. The check finds a planted foot sliding, a joint popping (one frame's turn far faster than its
// neighbours') and a joint shaking (its turn's second difference flipping sign frame after frame); each finding names a
// fix - Lock in Place for a slide, smoothing for a pop or a shake. Smoothing averages each joint's turn (as quaternions,
// one hemisphere) and place over the keyed frames around, Gaussian-weighted. Lock in Place holds an end where it is at
// the range's start (a whole-body IK solve each frame, keyed into the bones), blended in and out.
#pragma once

#include "animation.h"
#include "autopose.h"
#include "body_physics.h"

#include <string>
#include <vector>

namespace bl::motion {

struct Issue {
  enum Kind { Sliding, Pop, Shaking } kind = Sliding;
  std::string bone;   // its name
  int from = 0, to = 0;
  float amount = 0.0f;  // a slide's drift (cm), a pop's turn in that frame (degrees), a shake's mean jerk (degrees per frame^2)
  std::string text() const;
};

struct CheckSettings {
  float slide_cm = 1.0f;       // a planted foot drifting further than this
  float pop_deg = 20.0f;       // a frame's turn above this ...
  float pop_ratio = 4.0f;      // ... and this many times its neighbours'
  float shake_deg = 1.5f;      // a mean |second difference| above this, its sign flipping
  int shake_min_frames = 6;
};

/* The clip on the rig under `anim_root` checked frame by frame (posed, IK solved, the scene put back after). */
std::vector<Issue> check(Scene &scene, GameObject &anim_root, const AnimationClip &clip, const CheckSettings &s = CheckSettings());

/* Smoothing: each of `paths`' Rotation and Position keys in [from, to] moved toward the Gaussian average of the keys
 * within `radius` frames (strength 0..1). Keys written in place, none added. The keys changed. */
size_t smooth(AnimationClip &clip, const std::vector<std::string> &paths, float from, float to, float strength, float radius);

/* Lock in Place: `bone`'s head held where it is at `from` through `to`, eased in over `blend` frames before and out
 * over `blend` after, the body solved to it each frame (the rig's other feet pinned) and keyed. The frames keyed. */
int lock_in_place(Scene &scene, GameObject &anim_root, AnimationClip &clip, GameObject &bone, int from, int to, int blend);

}  // namespace bl::motion

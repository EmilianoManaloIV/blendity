// SPDX-License-Identifier: GPL-2.0-or-later
// Footsteps along a path (task 0126, as 3ds Max Biped's footsteps): a character walks a curve on footprints - each
// footprint an object of its own (a Footprint component) the animator moves and turns with the gizmo like any
// object - landing on the ground under them (stairs, slopes), and the body is solved from them: the pelvis through
// the stance feet, the swing feet arcing from print to print, the gait's sway, twist and arms on top. Written as keys
// (with contacts) like Walk Cycle's.
#pragma once
#include "../core/math.h"
#include "animation.h"
#include "curve.h"
#include "gait.h"
#include "scene.h"
#include <string>
#include <vector>

namespace bl {

/* One footprint: on its own object (its place and turn are the object's), drawn as a sole in the Scene view. */
struct Footprint : ComponentBase<Footprint> {
  static constexpr const char *kName = "Footprint";
  int side = 0;          // 0 left, 1 right (as the rig's legs)
  int leg = -1;          // which leg (a quadruped's four); -1: the first on `side`
  float frame = 0.0f;    // when the foot lands
  Vec3 planned;          // where the planner put it (moved since: the animator's, kept by Re-plan)
  float sole = 0.08f;    // the ankle's height over the sole (the object is at the ankle; the sole is drawn below it)
  float planned_yaw = 0.0f, planned_frame = 0.0f;  // the turn and landing frame the planner gave it
  void reflect(Reflector &r) override;
};

/* On the object holding a character's footprints (its children): whose they are, the path they were laid along and
 * the style they were walked in. */
struct Footsteps : ComponentBase<Footsteps> {
  static constexpr const char *kName = "Footsteps";
  std::string character;  // the animated character's name
  std::string path;       // the curve's name
  std::string style;      // gait::style_text of the style walked (files and undo)
  void reflect(Reflector &r) override;
};

namespace steps {

struct Plan {
  struct Print {
    int side = 0;
    Vec3 pos;
    float yaw = 0.0f;      // degrees about +Y from +Z (the way the foot points)
    Vec3 normal{0, 1, 0};  // the ground's
    float frame = 0.0f;    // landing
    bool kept = false;     // a hand-placed print kept by Re-plan
  };
  std::vector<Print> prints;  // in landing order; the first two are the start stance, the last two the stop
  int frames = 0;             // the walk's length
  std::string problem;        // why not ("" when planned)
  int dropped = 0;            // kept prints with no print of their foot near them any more (left out)
};

/* Footprints along `path` for `rig` walking in `style` at `fps`: alternate feet at the style's step, shorter in turns (by
 * the curvature) and for the first and last two steps; each landed on the ground under it (`ground`: false where
 * there is none - the print then stays on the path's height). `keep`: hand-moved prints (by side and order) to keep. */
Plan plan(const gait::Rig &rig, const gait::Style &style, const CurvePath &path, float fps,
          const std::function<bool(Vec3 at, Vec3 &ground, Vec3 &normal)> &ground, const std::vector<Plan::Print> &keep = {});

/* The body walking on `prints`: the gait's pose each frame from the prints (pelvis through the stance feet at the
 * ground's height, swing arcs, sway, twist, arms), keyed like gait::generate. Frames written; 0 with `error`. */
int walk(Scene &scene, GameObject &anim_root, const gait::Rig &rig, const gait::Style &style, const Plan &prints, float fps, const AnimationClip &clip,
         AnimationClip &out, std::string &error);

/* The pose at a frame of a planned walk (for the tests and the ghost). */
gait::Pose pose_on(const gait::Rig &rig, const gait::Style &style, const Plan &p, float frame, float fps);

}  // namespace steps
}  // namespace bl

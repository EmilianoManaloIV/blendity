// SPDX-License-Identifier: GPL-2.0-or-later
// Keyframe animation, Unity's way (task 0059, ADR 0029): AnimationClip assets (Assets/Animations/<name>.anim) hold
// float tracks bound by path below the animated root; the Animation component plays one in Play; the editor's
// Animation window (a dopesheet) edits them.
#pragma once

#include "scene.h"

#include <memory>
#include <string>
#include <vector>

namespace bl {

enum AnimInterp : int { AnimAuto = 0, AnimLinear = 1, AnimConstant = 2 };
/* A key's tangents (Unity's key tangent modes, task 0061): Clamped Auto works them out from the neighbours; Free
 * Smooth has one slope both sides; Flat is level; Broken has its own slope on each side. */
enum AnimTangent : int { TanAuto = 0, TanFree = 1, TanFlat = 2, TanBroken = 3 };

struct AnimKey {
  float frame = 0.0f;
  float value = 0.0f;
  int interp = AnimAuto;  // how the curve leaves this key for the next (a curve, a line, or held)
  int tangent = TanAuto;
  float in_slope = 0.0f, out_slope = 0.0f;  // value per frame (Free Smooth: both the same; Broken: each its own)
};

/* One number of one object: its path below the animated root ("" the root itself, "Hips/Spine" a bone), the property
 * ("Position", "Rotation" (Euler degrees), "Scale", or "Component.Field") and the axis (0..2 for a vector or colour). */
struct AnimTrack {
  std::string path, property;
  int axis = 0;
  std::vector<AnimKey> keys;  // sorted by frame, at most one per frame
  float sample(float frame) const;
  /* A key at `frame` (replacing the value of one there, and its interpolation unless `keep_interp`); returns its index.
   * Non-finite frames or values are refused (returns SIZE_MAX). */
  size_t set_key(float frame, float value, int interp = AnimAuto, bool keep_interp = true);
  size_t put_key(const AnimKey &k);  // the whole key (tangents too), replacing one on its frame
  int key_at(float frame) const;  // the key on that frame (within a hundredth of one), or -1
  /* Key i's slopes as the curve uses them (value per frame): what its tangent mode makes of them. */
  float in_tangent(size_t i) const;
  float out_tangent(size_t i) const;
  std::string label() const;      // "Hips : Rotation.y"
};

struct AnimationClip {
  std::string name = "Clip", asset_path;
  float frame_rate = 60.0f;
  float length = 60.0f;  // frames; at least the last key's
  bool loop = true;
  std::vector<AnimTrack> tracks;
  uint64_t version = 1;  // bumped by every edit (saving, undo snapshots)
  void touch() { version++; }
  float end_frame() const;  // the length, or the last key if later
  AnimTrack *find(const std::string &path, const std::string &property, int axis);
  AnimTrack &track(const std::string &path, const std::string &property, int axis);  // made when missing
};
using AnimationClipPtr = std::shared_ptr<AnimationClip>;

std::string save_clip_text(const AnimationClip &c);
bool load_clip_text(const std::string &text, AnimationClip &c);

/* The clip assets, shared like the materials: loaded on first use, written back by save_dirty_clip_assets. */
AnimationClipPtr clip_asset(const std::string &path);  // nullptr if missing
AnimationClipPtr create_clip_asset(const std::string &path, const std::string &name);
size_t save_dirty_clip_assets();
size_t retarget_clip_assets(const std::string &from, const std::string &to);  // a .anim (or folder) moved
size_t forget_clip_assets(const std::string &path);  // a .anim (or folder) deleted: not written back
void clear_clip_assets();

/* Position, Rotation and Scale: the Transform's numbers (the rest are "Component.Field"). */
bool is_transform_property(const std::string &property);
/* The object a track names below `root` (nullptr when it's gone), and the path of `g` below `root`. */
GameObject *anim_target(GameObject &root, const std::string &path);
std::string anim_path(const GameObject &root, const GameObject &g);
/* Reading and writing one animated number. */
bool read_anim_value(GameObject &g, const std::string &property, int axis, float &out);
bool write_anim_value(GameObject &g, const std::string &property, int axis, float value);
/* The whole clip at `frame` onto the hierarchy under `root`. */
void apply_clip(const AnimationClip &clip, GameObject &root, float frame);
/* A time in frames, wrapped: 0 Loop, 1 Once (holds the end), 2 Ping Pong, 3 Clamp Forever. */
float wrap_frame(float frame, float end, int wrap);

/* Unity's legacy Animation component: plays its clip in Play. */
struct Animation : ComponentBase<Animation> {
  static constexpr const char *kName = "Animation";
  std::string clip;  // the .anim asset, project-relative
  bool play_automatically = true;
  float speed = 1.0f;
  int wrap = 0;  // Loop, Once, Ping Pong, Clamp Forever
  float time = 0.0f;  // seconds since it started (Play only)
  bool playing = false;
  void reflect(Reflector &r) override;
  void start(PlayContext &ctx) override;
  void update(PlayContext &ctx) override;
};

}  // namespace bl

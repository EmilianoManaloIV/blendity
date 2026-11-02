// SPDX-License-Identifier: GPL-2.0-or-later
// Keyframe animation, Unity's way (task 0059, ADR 0029): AnimationClip assets (Assets/Animations/<name>.anim) hold
// float tracks bound by path below the animated root; the Animation component plays one in Play; the editor's
// Animation window (a dopesheet) edits them.
#pragma once

#include "scene.h"

#include <cmath>
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
  /* Where a bone's tip rests on something (task 0116, Cascadeur's fulcrum points): found from the motion, or set by hand
   * (kept when contacts are found again). Frames inclusive. */
  struct Contact {
    std::string bone;
    float from = 0.0f, to = 0.0f;
    bool manual = false;
  };
  std::vector<Contact> contacts;
  /* The active ragdoll's events (task 0121): an impulse on a bone at a frame (N s, world), the motors off or on from a
   * frame (a fall, a recovery). */
  struct RagdollEvent {
    enum Kind { Impulse = 0, MotorsOff = 1, MotorsOn = 2 };
    int kind = Impulse;
    float frame = 0.0f;
    std::string bone;  // (Impulse)
    Vec3 impulse;
  };
  std::vector<RagdollEvent> ragdoll;
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
/* Round 335: a property that switches rather than blends - a switch, a whole number, a list's choice, Active: its keys
 * are made Constant (Blender does so for booleans and enums). */
bool anim_value_stepped(GameObject &g, const std::string &property);
/* The whole clip at `frame` onto the hierarchy under `root`. */
/* `hold` > 1: animation on 2s, 3s... - the poses sampled at the held frame; a camera's tracks (an object with a Camera)
 * at `frame` itself unless `hold_cameras` (round 340: anime holds the characters, the camera moves every frame). */
void apply_clip(const AnimationClip &clip, GameObject &root, float frame, int hold = 1, bool hold_cameras = false, float hold_from = 0.0f);
/* A camera, or what carries one (a camera rig's empty): what moves every frame while the rest holds (round 340). */
bool camera_rig(const GameObject &g);
/* The Euler angles (degrees, Unity's order) for q nearest `near`: each angle by whole turns, and the second solution
 * (180 - x, y + 180, z + 180). A chain of keys converted this way never wraps (tasks 0137, 0139). */
Vec3 euler_near(Quat q, Vec3 near);
/* Keys a straight line already gives (within `tolerance` of the line between their neighbours) dropped: a clip baked a
 * frame at a time keeps only the frames that matter (Unity's importer does the same). The first and last stay. */
void reduce_keys(std::vector<AnimKey> &keys, float tolerance);
/* The frame rate key times fit best (keys on whole frames at 24, 25, 30, 50, 60 or 120 fps), else 30 (task 0139). */
float best_frame_rate(const std::vector<float> &times);
/* A time in frames, wrapped: 0 Loop, 1 Once (holds the end), 2 Ping Pong, 3 Clamp Forever. */
float wrap_frame(float frame, float end, int wrap);
/* Animation on 2s or 3s (task 0149): the frame held to multiples of `hold` (1: every frame; 2: a new pose every other
 * frame, as hand-drawn animation is shot). */
inline float hold_frame(float frame, int hold) { return hold > 1 ? std::floor(frame / (float)hold + 1e-4f) * (float)hold : frame; }  // (+1e-4: 1.9999 frames is frame 2)
/* A render range's frame held from the range's first frame, so a range starting at 3 on 2s shows 3, 3, 5, 5. */
inline float hold_frame_from(float first, float frame, int hold) { return first + hold_frame(frame - first, hold); }

/* Unity's legacy Animation component: plays its clip in Play. */
struct Animation : ComponentBase<Animation> {
  static constexpr const char *kName = "Animation";
  std::string clip;  // the .anim asset, project-relative
  bool play_automatically = true;
  float speed = 1.0f;
  int wrap = 0;  // Loop, Once, Ping Pong, Clamp Forever
  int hold = 1;  // on 1s, 2s or 3s (task 0149): the pose changes every hold frames of the clip, the rest of the scene moves on
  bool hold_camera = false;  // (round 340) a camera the clip animates is held too (off: it moves every frame)
  float time = 0.0f;  // seconds since it started (Play only)
  bool playing = false;
  void reflect(Reflector &r) override;
  void start(PlayContext &ctx) override;
  void update(PlayContext &ctx) override;
};

}  // namespace bl

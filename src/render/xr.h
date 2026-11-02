// SPDX-License-Identifier: GPL-2.0-or-later
/* VR through OpenXR (round 383, task 0360): a headset sees the scene at 1:1, standing where the Scene view looks.
 *
 * - The stereo maths, here and tested in every build: an eye's pose (OpenXR's right-handed, Y up, -Z forward) made
 *   Blendity's view matrix (left-handed, +Z forward: Z mirrored), its four field-of-view angles made an off-centre
 *   projection in Blendity's convention (depth 0..1), and where the headset's stage sits in the scene.
 * - A session's state machine (Session) against a Runtime: OpenXR's events and frame loop as a few calls, so a stub
 *   runtime drives it in tests. The real one (xr_openxr.cpp, behind BL_WITH_OPENXR) loads openxr_loader.dll at run
 *   time - no headset, no runtime or no loader leaves VR off and says why - and shows each eye's pixels through
 *   Direct3D 11 swapchains (Windows; elsewhere VR says it isn't there yet).
 * Blender's intern/ghost/intern/GHOST_Xr*.cc is the reference for the session's life. */
#pragma once

#include "../core/math.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace bl::xr {

/* A pose in OpenXR's space: right-handed, Y up, -Z forward, metres. */
struct Pose {
  Quat orientation;
  Vec3 position;
};
/* An eye's field of view: its four angles from straight ahead, radians (left and down negative). */
struct Fov {
  float left = -0.8f, right = 0.8f, up = 0.8f, down = -0.8f;
};

/* OpenXR's point and turn in Blendity's axes (Z mirrored). */
Vec3 to_blendity(Vec3 p);
Quat to_blendity(Quat q);
/* An eye's view matrix in Blendity's convention, from its pose in the stage. */
Mat4 eye_view(const Mat4 &world_from_stage, const Pose &eye);
/* An eye's world position. */
Vec3 eye_position(const Mat4 &world_from_stage, const Pose &eye);
/* An off-centre projection from the four angles: left-handed, depth 0..1, as Mat4::perspective (which it equals for a
 * symmetric field). */
Mat4 eye_projection(const Fov &fov, float znear, float zfar);

/* A hand as the runtime tracks it (OpenXR's space: the stage's) and what its controller says. */
struct Hand {
  bool tracked = false;
  Pose aim;   // the pointing ray: from position along -Z
  Pose grip;  // where the hand holds the controller
  float trigger = 0.0f, squeeze = 0.0f;  // 0..1
  Vec2 stick;                            // -1..1 each way (+y forward)
  bool primary = false;                  // A / X (or the menu button)
};

/* One eye of a frame: where it looks from, what it sees, how big its image is. */
struct Eye {
  Pose pose;
  Fov fov;
  int width = 0, height = 0;
};

/* What a session needs from OpenXR, as a few calls (the real loader's, or a test's stub). */
class Runtime {
 public:
  virtual ~Runtime() = default;
  /* An instance, a system (a headset), a session and the eyes' swapchains at `scale` of the runtime's recommended size;
   * false with `why` when there is none. */
  virtual bool create(float scale, std::string &why) = 0;
  enum class Event { None, Ready, Stopping, Exiting, LossPending, Focused, Visible };
  virtual Event poll_event() = 0;
  virtual bool begin_session(std::string &why) = 0;
  virtual void end_session() = 0;
  virtual void destroy() = 0;
  /* A frame: whether the runtime wants it drawn, and the eyes where they are for its display time. */
  virtual bool wait_frame(bool &should_render) = 0;
  virtual bool begin_frame() = 0;
  virtual bool locate_eyes(std::vector<Eye> &eyes) = 0;
  /* Eye `i`'s image (width x height, 0xAARRGGBB rows top down, sRGB-encoded) into its swapchain; false when it wasn't
   * taken (the frame then ends without the layer). */
  virtual bool submit_eye(int i, const uint32_t *pixels, int width, int height) = 0;
  /* Round 384: the hands at the frame's display time, after the controllers' input is read (false: none tracked). */
  virtual bool locate_hands(Hand hands[2]) {
    (void)hands;
    return false;
  }
  /* The frame shown (with the eyes' layer when `rendered`). */
  virtual bool end_frame(bool rendered) = 0;
  virtual std::string name() const = 0;
  /* How far the runtime's origin is below the eyes' standing height: 0 for a stage (the floor), 1.6 m for a local space
   * (the head where it started). */
  virtual float standing_lift() const { return 0.0f; }
};

/* The OpenXR runtime when this build has it and the loader is there (nullptr: VR off, `why` says so). */
std::unique_ptr<Runtime> open_runtime(std::string &why);

/* A VR session's life: Off, then Starting (created, waiting for Ready), Running (frames), Stopping, back to Off; Lost when
 * the runtime went away. The editor calls tick() each frame with what draws an eye. */
class Session {
 public:
  enum class State { Off, Starting, Running, Stopping, Lost };
  /* Draws eye `i` into `pixels` (width x height, 0xAARRGGBB): its view and projection in Blendity's convention, its
   * world position. */
  using DrawEye = std::function<void(int i, const Mat4 &view, const Mat4 &proj, Vec3 eye, int width, int height, uint32_t *pixels)>;

  explicit Session(std::unique_ptr<Runtime> rt) : rt_(std::move(rt)) {}
  ~Session() { stop(); }
  bool start(std::string &why);
  void stop();
  /* Events, then a frame when running: the hands found, then `stage` asked where the stage stands now (the hands may
   * have moved it - review 384: the world had lagged the controllers a frame), then each eye drawn by `draw`. */
  using StageNow = std::function<Mat4()>;
  void tick(const StageNow &stage, const DrawEye &draw);
  void tick(const Mat4 &world_from_stage, const DrawEye &draw) {
    tick([&] { return world_from_stage; }, draw);
  }
  State state() const { return state_; }
  const std::string &why() const { return why_; }
  uint64_t frames() const { return frames_; }      // frames ended (drawn or not)
  uint64_t rendered() const { return rendered_; }  // frames whose eyes were drawn
  const std::vector<Eye> &last_eyes() const { return eyes_; }
  /* Round 384: the hands as the last frame found them (left, right), and the head (the eyes' middle) in the stage. */
  const Hand &hand(int i) const { return hands_[i & 1]; }
  Pose head() const;
  std::string runtime_name() const { return rt_ ? rt_->name() : std::string(); }
  float standing_lift() const { return rt_ ? rt_->standing_lift() : 0.0f; }
  float znear = 0.05f, zfar = 1000.0f;  // metres (a hand's length in front of the eyes, a kilometre away)
  /* Eyes drawn at this share of the runtime's recommended size, each way (CPU rendering: half is a quarter of the
   * work); set before start(). */
  float resolution = 0.5f;

 private:
  void events();
  void frame(const StageNow &stage, const DrawEye &draw);
  std::unique_ptr<Runtime> rt_;
  State state_ = State::Off;
  std::string why_;
  uint64_t frames_ = 0, rendered_ = 0;
  std::vector<Eye> eyes_;
  std::vector<uint32_t> pixels_;
  Hand hands_[2];
};

const char *state_name(Session::State s);

}  // namespace bl::xr

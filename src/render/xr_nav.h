// SPDX-License-Identifier: GPL-2.0-or-later
/* Moving around in VR (round 384, task 0361): where the headset's stage sits in the scene, and how the hands move it -
 * teleport along an arc, smooth move and snap turn on the sticks, grabbing the world with one hand (move) or both (move,
 * turn about the vertical and scale), the scale snapping to 1:1 near it, and a tape measure between the hands. All of it
 * pure: recorded hand poses drive it in tests. */
#pragma once

#include "xr.h"

#include <functional>

namespace bl::xr {

/* The stage in the scene: world_from_stage = T(origin) * R(yaw about Y) * S(scale) * T(0, lift, 0). origin.y is the
 * floor's height (0, or what a grab lifted it to); `lift` raises the runtime's origin to a standing height (a seated
 * runtime's is the head: 1.6 m) - the eyes, the hands, the arc and the grabs all go through it (review 384). */
struct Stage {
  Vec3 origin;
  float yaw = 0.0f;    // degrees
  float scale = 1.0f;  // 1: life size; 2: the world looks half as big (the user twice as big in it)
  float lift = 0.0f;   // metres, in the stage
  Mat4 matrix() const;
};
constexpr float kMinScale = 0.01f, kMaxScale = 100.0f;  // (a grab's scale kept in this range, as `vr scale` is)
constexpr float kMinTwoHandSpan = 0.05f;                // (hands closer than this grab as one: their angle and distance are noise)

/* A point of the stage (OpenXR's axes) in the world. */
Vec3 stage_point(const Stage &s, Vec3 p_xr);
/* The stage's yaw-only turn of an OpenXR direction into the world. */
Vec3 stage_dir(const Stage &s, Vec3 d_xr);

/* The teleport arc: from the hand's aim, a throw at `speed` m/s falling at `gravity` m/s^2 (both in the world's metres,
 * scaled with the stage), in `steps` segments of `dt` s each, until `hit` finds something along a segment (it returns
 * the first hit between a and b and its surface's normal) or the ground plane y = 0. The points walked, and the landing:
 * none when the arc ends on a wall or a slope steeper than kMinFloorNormalY allows (review 384: it had stood you on
 * walls). */
struct Arc {
  std::vector<Vec3> points;
  bool landed = false;
  Vec3 at;
};
using HitFn = std::function<bool(Vec3 a, Vec3 b, Vec3 &hit, Vec3 &normal)>;
constexpr float kMinFloorNormalY = 0.7f;  // (a landing's normal at least this upright: about 45 degrees of slope)
Arc teleport_arc(const Stage &s, const Hand &h, const HitFn &hit, float speed = 7.0f, float gravity = 9.8f, int steps = 60, float dt = 0.05f);
/* The stage moved so the head (its stage position) stands over `target`: its feet on target's height. */
Stage teleport(const Stage &s, Vec3 head_xr, Vec3 target);

/* The left stick walks: forward and sideways relative to where the head looks, level, at `speed` m/s (the world's, times
 * the scale) for `dt` s. */
Stage smooth_move(const Stage &s, Quat head_xr_turn, Vec2 stick, float speed, float dt);
/* The right stick's snap turn: `degrees` about the head (it stays where it is in the world). */
Stage snap_turn(const Stage &s, Vec3 head_xr, float degrees);

/* A grab: the hands' stage positions when it began and the stage then. One hand moves the world with it; two also turn
 * it about the vertical by the angle between them and scale it by how far apart they come (apart: the world grows). */
struct Grab {
  bool active = false;
  bool two = false;
  Stage start;
  Vec3 a0, b0;          // stage positions (OpenXR's axes) at the start: a0 alone for one hand
  bool turning = true;  // two hands far enough apart across the floor to turn the world
  Stage last;           // the stage the last update gave
};
Grab grab_begin(const Stage &s, Vec3 a_xr, const Vec3 *b_xr);
/* The stage from the grab's start: the same for the same hands. */
Stage grab_update(const Grab &g, Vec3 a_xr, const Vec3 *b_xr);
/* The same, frame by frame: when the hands' span across the floor drops under kMinTwoHandSpan (one hand over the other)
 * or grows past it again, the grab starts again from where the world is, so it never jumps back to the start's turn
 * (re-check 384). */
Stage grab_update(Grab &g, Vec3 a_xr, const Vec3 *b_xr);
/* A scale within `snap` of 1:1 becomes 1:1 (the grab's scale snaps as it passes life size). */
float snap_scale(float scale, float snap = 0.05f);

/* Round 384 (review): triangles in a bounding-volume hierarchy, for the arc's segments - the nearest crossing of a
 * segment and the triangle's normal, in a few node visits however big the mesh. */
struct SegmentBvh {
  struct Node {
    Vec3 lo, hi;
    uint32_t first = 0, count = 0, right = 0;  // a leaf: triangles [first, first + count); else children first and right
  };
  std::vector<Vec3> pos;
  std::vector<uint32_t> tris, order;
  std::vector<Node> nodes;
  void build(const std::vector<Vec3> &positions, const std::vector<uint32_t> &indices);
  /* The nearest crossing of a -> b: its fraction along it and the triangle's normal (facing a). */
  bool first_hit(Vec3 a, Vec3 b, float &t, Vec3 &normal) const;
};

/* The tape measure: the world distance between two stage points. */
float measure(const Stage &s, Vec3 a_xr, Vec3 b_xr);

}  // namespace bl::xr

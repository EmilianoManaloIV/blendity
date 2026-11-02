// SPDX-License-Identifier: GPL-2.0-or-later
// Painting in VR (round 386, task 0363): the right controller's ray paints the object in Paint mode through the paint
// pipeline (editor/paint.cpp, ADR 0083) - a world dab square to the ray where it meets the surface, the trigger's
// pressure as the pen's, the brush's tip, colour, channels, symmetry and undo as at the desk. The right grip erases
// (it doesn't grab while painting; the left still does), the right stick sizes the brush, the left stick's sideways sets
// its opacity, A undoes the last paint stroke. `vr paint [on|off] | size <cm> | opacity <0-1>`.
#include "editor.h"

#include <cmath>

namespace bl {

namespace {

constexpr float kPaintStart = 0.1f, kPaintEnd = 0.05f;  // (the trigger past the first starts a stroke, under the second ends it)
constexpr float kPaintReach = 20.0f;                    // (metres of ray, at life size)
constexpr float kMinRadius = 0.005f, kMaxRadius = 0.5f, kRadiusStep = 1.5f;
constexpr float kOpacityStep = 0.1f, kMinOpacity = 0.05f;
constexpr float kFlickAt = 0.7f, kFlickRest = 0.3f;
constexpr int kMaxDabsAFrame = 64;
constexpr int kRingSegments = 24;
constexpr float kEraseGrip = 0.5f;  // (the right grip past this erases)

/* -1, 0 or 1 when a stick's axis is flicked past kFlickAt from rest. */
int flicked(float axis, bool &armed) {
  if (std::fabs(axis) < kFlickRest) armed = true;
  if (!armed || std::fabs(axis) < kFlickAt) return 0;
  armed = false;
  return axis > 0 ? 1 : -1;
}

uint32_t rgba(Vec3 c) {
  auto ch = [](float v) { return (uint32_t)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
  return 0xFF000000u | (ch(c.z) << 16) | (ch(c.y) << 8) | ch(c.x);
}

}  // namespace

/* The ray's frame in the world: where it starts, its direction (the aim's -Z), and the disc's right and down. */
void Editor::vr_paint_frame(const xr::Hand &r, Vec3 &from, Vec3 &axis, Vec3 &right, Vec3 &down) const {
  from = xr::stage_point(vr_stage_, r.aim.position);
  axis = normalize(xr::stage_dir(vr_stage_, r.aim.orientation.rotate({0, 0, -1})));
  right = normalize(xr::stage_dir(vr_stage_, r.aim.orientation.rotate({1, 0, 0})));
  down = normalize(xr::stage_dir(vr_stage_, r.aim.orientation.rotate({0, -1, 0})));
}

/* The painted object's segment BVH (its base mesh, as the brush projects; made on a worker): with `fresh_only` only one
 * made for the mesh as it is now (its triangles index the render mesh the dab rasterizes). */
const xr::SegmentBvh *Editor::vr_paint_bvh(bool fresh_only) {
  GameObject *g = paint_ ? scene_->find(paint_obj_) : nullptr;
  const MeshFilter *mf = g ? g->mesh_filter() : nullptr;
  if (!mf || !mf->mesh) return nullptr;
  int building = vr_bvh_harvest();
  const xr::SegmentBvh *bvh = vr_bvh_of(*mf->mesh, building);
  if (!fresh_only || !bvh) return bvh;
  const VrBvh &c = vr_bvh_[mf->mesh->uid.v];
  const bool fresh = !c.job.valid() && c.version == mf->mesh->version && c.deform == mf->mesh->deform_version && c.tris == mf->mesh->render_mesh().tri_count();
  return fresh ? bvh : nullptr;
}

/* Where a ray (world) meets the painted object, within kPaintReach times the scale. */
bool Editor::vr_paint_ray(Vec3 from, Vec3 axis, Vec3 &hit) {
  const xr::SegmentBvh *bvh = vr_paint_bvh(false);
  GameObject *g = bvh ? scene_->find(paint_obj_) : nullptr;
  if (!g) return false;
  const Vec3 to = from + axis * (kPaintReach * vr_stage_.scale);
  const Mat4 inv = g->world_matrix().inverse();
  float t;
  Vec3 n;
  if (!bvh->first_hit(inv.point(from), inv.point(to), t, n)) return false;
  hit = from + (to - from) * t;
  return true;
}

/* The painting hands, each frame VR runs with painting on: the brush's size and opacity, A's undo, the stroke. Paint
 * turned off at the desk (Esc, another object, another scene) turns VR's painting off too (review 386). */
void Editor::vr_paint_hands(const xr::Hand &l, const xr::Hand &r) {
  if (!paint_ || scene_.get() != paint_scene_ || !scene_->find(paint_obj_)) {  // (re-check 386: another scene too)
    vr_paint_end();
    vr_paint_ = vr_pt_aimed_ = false;
    return Log::info("VR paint: off - Paint was turned off at the desk");
  }
  const int size = flicked(r.stick.y, vr_pt_flick_size_);
  if (r.tracked && size) vr_paint_radius_ = std::clamp(size > 0 ? vr_paint_radius_ * kRadiusStep : vr_paint_radius_ / kRadiusStep, kMinRadius, kMaxRadius);
  const int opacity = flicked(l.stick.x, vr_pt_flick_opacity_);
  if (l.tracked && opacity) paint_brush_.opacity = std::clamp(paint_brush_.opacity + opacity * kOpacityStep, kMinOpacity, 1.0f);
  const bool undo_now = r.primary && !vr_primary_was_ && !vr_pt_stroking_;
  vr_primary_was_ = r.primary;
  if (undo_now) vr_paint_undo();
  Vec3 from, axis, right, down;
  vr_paint_frame(r, from, axis, right, down);
  vr_pt_aimed_ = r.tracked && vr_paint_ray(from, axis, vr_pt_hit_);
  const bool painting = r.tracked && r.trigger > (vr_pt_stroking_ ? kPaintEnd : kPaintStart);
  const bool erase = r.squeeze > kEraseGrip;
  if (!painting || erase != vr_pt_erase_) vr_paint_end();  // (the grip squeezed or let go mid-stroke: a stroke of the other kind from here)
  if (painting) vr_paint_to(r);
}

/* A stroke's start where the ray meets the surface: refused while a desk stroke is under way (it is the desk's). */
void Editor::vr_paint_begin(const xr::Hand &r) {
  if (paint_stroking_ || !vr_pt_aimed_ || !paint_begin_stroke_images()) return;
  vr_pt_erase_ = r.squeeze > kEraseGrip;
  paint_erase_ = paint_brush_.erase || vr_pt_erase_;
  vr_pt_stroking_ = true;
  Vec3 from, axis, right, down;
  vr_paint_frame(r, from, axis, right, down);
  vr_paint_dab(vr_pt_hit_, axis, right, down, r.trigger);
  vr_pt_last_ = vr_pt_hit_;
}

/* A dab at a point of the surface, square to the controller's ray, the triangles near it from the BVH when it is the
 * mesh's own. */
void Editor::vr_paint_dab(Vec3 hit, Vec3 axis, Vec3 right, Vec3 down, float trigger) {
  const xr::SegmentBvh *bvh = vr_paint_bvh(true);
  NearFn near;
  if (bvh) near = [bvh](Vec3 c, float rad, std::vector<uint32_t> &tris) {
    bvh->near(c, rad, tris);
    return true;
  };
  paint_world_dab(hit, axis, right, down, vr_paint_radius_ * vr_stage_.scale, trigger, near);
}

/* The stroke on to where the ray meets the surface now: the rays between last frame's and this one's, each hit again -
 * dabs where they meet the surface, spaced by the brush's spacing (at most kMaxDabsAFrame a frame), none where they
 * miss (review 386: a stroke that left the surface and came back had painted a straight bridge across it). */
void Editor::vr_paint_to(const xr::Hand &r) {
  Vec3 from, axis, right, down;
  vr_paint_frame(r, from, axis, right, down);
  const Vec3 last_from = vr_pt_from_, last_axis = vr_pt_axis_, last_right = vr_pt_right_, last_down = vr_pt_down_;
  vr_pt_from_ = from, vr_pt_axis_ = axis, vr_pt_right_ = right, vr_pt_down_ = down;
  if (!vr_pt_stroking_) return vr_paint_begin(r);
  const float step = std::max(1e-4f, vr_paint_radius_ * vr_stage_.scale * std::max(0.05f, paint_brush_.spacing));
  /* How far the ray swept: its start moved, and its turn at the distance of the last dab (re-check 386: off the
   * surface every frame had cast 64 rays, the ray still or not). */
  const float turn = std::acos(std::clamp(dot(axis, last_axis), -1.0f, 1.0f));
  const float swept = length(from - last_from) + turn * length(vr_pt_last_ - from);
  const float len = vr_pt_aimed_ ? std::max(length(vr_pt_hit_ - vr_pt_last_), swept) : swept;
  if (len < step) return;
  const int k = std::min(kMaxDabsAFrame, std::max(1, (int)(len / step)));
  for (int i = 1; i <= k && paint_stroking_; i++) {
    const float f = i / (float)k;
    const Vec3 ax = normalize(last_axis + (axis - last_axis) * f);
    Vec3 hit;
    if (!vr_paint_ray(last_from + (from - last_from) * f, ax, hit)) continue;
    vr_paint_dab(hit, ax, normalize(last_right + (right - last_right) * f), normalize(last_down + (down - last_down) * f), r.trigger);  // (each dab square to the ray that found it - re-check 386)
    vr_pt_last_ = hit;
  }
}

/* The stroke's end: one undo step, as the desk's. */
void Editor::vr_paint_end() {
  if (!vr_pt_stroking_) return;
  vr_pt_stroking_ = false;
  paint_end_stroke();
}

/* A: the last step undone when it is a paint stroke ("Paint" or "Erase"). */
void Editor::vr_paint_undo() {
  commit_undo();
  const bool stroke = !undo_.empty() && (undo_.back().label == "Paint" || undo_.back().label == "Erase");
  if (!stroke) return Log::info("VR paint: the last step is not a paint stroke - undo it at the desk");
  undo();
}

/* What painting draws into an eye: the ray to where it meets the surface (1 m when it meets nothing), the brush's ring
 * there square to the ray (red when the grip erases), its size and opacity. */
void Editor::vr_paint_overlay(const std::function<void(Vec3, Vec3, uint32_t, float)> &line) {
  const xr::Hand &r = vr_->hand(1);
  if (!r.tracked) return;
  Vec3 from, axis, right, down;
  vr_paint_frame(r, from, axis, right, down);
  const uint32_t ink = r.squeeze > kEraseGrip ? 0xFF3030FFu : rgba(paint_brush_.color);
  line(from, vr_pt_aimed_ ? vr_pt_hit_ : from + axis * vr_stage_.scale, ink, 1.5f);
  if (!vr_pt_aimed_) return;
  const float by_pressure = paint_brush_.pressure == 2 && vr_pt_stroking_ ? std::max(0.1f, pen_pressure(std::clamp(r.trigger, 0.0f, 1.0f))) : 1.0f;
  const float radius = vr_paint_radius_ * vr_stage_.scale * by_pressure;
  for (int k = 0; k < kRingSegments; k++) {
    const float a0 = k * 6.2831853f / kRingSegments, a1 = (k + 1) * 6.2831853f / kRingSegments;
    const Vec3 p0 = vr_pt_hit_ + (right * std::cos(a0) + down * std::sin(a0)) * radius, p1 = vr_pt_hit_ + (right * std::cos(a1) + down * std::sin(a1)) * radius;
    line(p0 - axis * 0.002f, p1 - axis * 0.002f, ink, 2.0f);
  }
}

/* vr paint [on|off] | size <cm> | opacity <0-1> */
void Editor::vr_paint_command(const std::vector<std::string> &t) {
  const std::string arg = t.size() > 2 ? to_lower(t[2]) : "";
  const float number = t.size() > 3 ? (float)std::atof(t[3].c_str()) : NAN;
  if (arg == "size" && !(number > 0 && std::isfinite(number))) return Log::warn("vr paint size wants a radius in centimetres");
  if (arg == "opacity" && !(number >= 0 && number <= 1)) return Log::warn("vr paint opacity wants 0 to 1");
  if (arg == "size") vr_paint_radius_ = std::clamp(number / 100.0f, kMinRadius, kMaxRadius);
  if (arg == "opacity") paint_brush_.opacity = std::max(kMinOpacity, number);
  if (arg != "" && arg != "on" && arg != "off" && arg != "size" && arg != "opacity") return Log::warn("vr paint wants on, off, size <cm> or opacity <0-1>");
  const bool setting = arg == "size" || arg == "opacity";
  const bool on = setting ? vr_paint_ : arg.empty() ? !vr_paint_ : arg == "on";
  if (on && !paint_) {
    vr_paint_ = false;
    return Log::warn("VR paint: turn Paint on for an object first (select it, then the Paint tab or `paint on`)");
  }
  if (!on) vr_paint_end();
  if (on && vr_sk_drawing_) vr_sketch_commit();  // (review 386: a sketch stroke under way is kept)
  vr_paint_ = on;
  if (on) vr_sketch_ = false, vr_tape_ = false;  // (one brush at a time; the tape's buttons undo while painting)
  if (!vr_paint_ && !setting) return Log::info("VR paint: off");
  Log::info("VR paint: %s; radius %.1f cm, opacity %.0f%%", vr_paint_ ? "on - the right trigger paints, the grip erases" : "off", vr_paint_radius_ * 100.0f, paint_brush_.opacity * 100.0f);
}

}  // namespace bl

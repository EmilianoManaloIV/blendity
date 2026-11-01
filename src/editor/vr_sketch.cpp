// SPDX-License-Identifier: GPL-2.0-or-later
// Sketching in VR (round 385, task 0362): the right controller draws strokes in the air into the scene's Stroke object
// (scene/stroke_object.h, ADR 0084) - ribbons that turn with the hand, or tubes - their width the trigger's pressure, as
// Quill and Gravity Sketch draw. A palette of 8 colours on the left stick, the brush's size on the right stick, the right
// primary (A) undoes the last stroke, the left primary (X on Touch, A on Index, the menu button on Vive and WMR)
// switches ribbon and tube. Strokes go into VR's own Stroke object, "VR Sketch". `vr sketch [on|off] | tube [on|off] | colour <1-8> | brush <cm>`.
#include "editor.h"

#include "../scene/stroke_object.h"

#include <cmath>

namespace bl {

namespace {

constexpr float kTwoPi = 6.2831853f;

constexpr float kDrawStart = 0.1f, kDrawEnd = 0.05f;  // (the trigger past the first starts a stroke, under the second ends it)
constexpr float kMinPressureWidth = 0.15f;             // (a light touch still draws this share of the brush)
constexpr float kTipReach = 0.05f;                     // (the brush's tip, metres ahead of the controller's aim)
constexpr float kFlick = 0.7f, kFlickRest = 0.3f;      // (a stick past the first is a flick; back under the second, another may come)
constexpr float kBrushStep = 1.5f, kMinBrush = 0.001f, kMaxBrush = 0.5f;
constexpr float kPaletteRing = 0.06f, kPaletteDot = 0.008f, kPaletteDotPx = 6.0f;  // (the palette round the left hand: metres, metres, pixels)
constexpr const char *kVrSketchName = "VR Sketch";

/* The palette: black, white, then the rainbow's six. */
const Vec3 kPalette[kVrPaletteSize] = {{0.05f, 0.05f, 0.05f}, {0.95f, 0.95f, 0.95f}, {0.85f, 0.15f, 0.12f}, {0.95f, 0.55f, 0.1f},
                                       {0.95f, 0.85f, 0.15f}, {0.2f, 0.65f, 0.25f},  {0.15f, 0.4f, 0.85f},  {0.55f, 0.25f, 0.75f}};

uint32_t to_rgba(Vec3 c) {
  auto ch = [](float v) { return (uint32_t)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
  return 0xFF000000u | (ch(c.z) << 16) | (ch(c.y) << 8) | ch(c.x);
}

/* -1, 0 or 1 when a stick's axis is flicked past kFlick from rest; `armed` is whether it came back to rest since. */
int flick(float axis, bool &armed) {
  if (std::fabs(axis) < kFlickRest) armed = true;
  if (!armed || std::fabs(axis) < kFlick) return 0;
  armed = false;
  return axis > 0 ? 1 : -1;
}

}  // namespace

Vec3 vr_palette_colour(int i) { return kPalette[std::clamp(i, 0, kVrPaletteSize - 1)]; }

/* The brush's tip in the world: kTipReach ahead of the aim. */
Vec3 Editor::vr_sketch_tip(const xr::Hand &h) const { return xr::stage_point(vr_stage_, h.aim.position + h.aim.orientation.rotate({0, 0, -kTipReach})); }

/* The sketching hands, each frame VR runs with sketching on: draw, undo, the palette, the brush's size, ribbon or tube.
 * A squeeze ends a stroke (the grab moves the world instead). */
void Editor::vr_sketch_hands(const xr::Hand &l, const xr::Hand &r, int held) {
  const int colour = flick(l.stick.x, vr_sk_flick_colour_);
  if (l.tracked && colour) vr_colour_ = (vr_colour_ + colour + kVrPaletteSize) % kVrPaletteSize;
  const int size = flick(r.stick.y, vr_sk_flick_size_);
  if (r.tracked && size) vr_brush_ = std::clamp(size > 0 ? vr_brush_ * kBrushStep : vr_brush_ / kBrushStep, kMinBrush, kMaxBrush);
  if (l.primary && !vr_sk_x_was_) vr_tube_ = !vr_tube_;
  vr_sk_x_was_ = l.primary;
  const bool undo_now = r.primary && !vr_primary_was_ && !vr_sk_drawing_;
  vr_primary_was_ = r.primary;
  if (undo_now) vr_sketch_undo();
  const bool drawing = r.tracked && !held && r.trigger > (vr_sk_drawing_ ? kDrawEnd : kDrawStart);
  if (!drawing && vr_sk_drawing_) vr_sketch_commit();
  if (drawing) vr_sketch_sample(r);
}

/* A point of the stroke under way: where the tip is, the controller's up, the width from the pressure - when the tip
 * moved a quarter of the width (2 mm at least, in the world's scale) since the last. */
void Editor::vr_sketch_sample(const xr::Hand &r) {
  const Vec3 tip = vr_sketch_tip(r);
  const float s = vr_stage_.scale;
  const float w = vr_brush_ * s * (kMinPressureWidth + (1.0f - kMinPressureWidth) * std::clamp(r.trigger, 0.0f, 1.0f));
  vr_sk_drawing_ = true;
  if (!vr_sk_pts_.empty() && length(tip - vr_sk_pts_.back()) < std::max(0.002f * s, w * 0.25f)) return;
  vr_sk_pts_.push_back(tip);
  vr_sk_ups_.push_back(normalize(xr::stage_dir(vr_stage_, r.aim.orientation.rotate({0, 1, 0}))));
  vr_sk_w_.push_back(w);
}

/* The material slot of a palette colour on the Stroke object: its "Sketch <n>" material, made on first use. */
int Editor::vr_sketch_slot(GameObject &g, int colour) {
  MeshRenderer *mr = g.get<MeshRenderer>();
  if (!mr) return 0;
  const std::string name = strprintf("Sketch %d", colour + 1);
  for (size_t i = 0; i < mr->materials.size(); i++)
    if (mr->materials[i] && mr->materials[i]->name == name) return (int)i;
  mr->materials.push_back(make_material(name, vr_palette_colour(colour)));
  return (int)mr->materials.size() - 1;
}

/* VR's own Stroke object, "VR Sketch" at the origin with Own normals (review 385: a desktop Canopy object's normals had
 * shaded VR ribbons); the desktop tool keeps its own. Past vr_sk_chunk_ vertices it is full and another is made: adding
 * a stroke copies its object's mesh, so the cost stays bounded however much is drawn. */
GameObject *Editor::vr_sketch_object() {
  GameObject *g = vr_sk_obj_ ? scene_->find(vr_sk_obj_) : nullptr;
  const MeshFilter *mf = g ? g->mesh_filter() : nullptr;
  const bool full = mf && mf->mesh && mf->mesh->positions.size() >= vr_sk_chunk_;
  if (g && g->get<Strokes>() && g->name.rfind(kVrSketchName, 0) == 0 && !full) return g;  // (re-check 385: an undo frees its id for the desk's next object)
  const uint64_t desktop = strokeobj_id_;
  g = strokeobj_object(true, true);
  strokeobj_id_ = desktop;
  if (!g) return nullptr;
  g->name = full ? strprintf("%s %d", kVrSketchName, ++vr_sk_parts_) : kVrSketchName;
  g->get<Strokes>()->normals = 0;
  vr_sk_obj_ = g->id;
  return g;
}

/* The stroke's end: its points (two or more) become a ribbon or a tube on VR's Stroke object, in its space - its
 * geometry added to the mesh (not every stroke built again) - one undo step. */
void Editor::vr_sketch_commit() {
  std::vector<Vec3> pts = std::move(vr_sk_pts_), ups = std::move(vr_sk_ups_);
  std::vector<float> ws = std::move(vr_sk_w_);
  vr_sk_pts_.clear(), vr_sk_ups_.clear(), vr_sk_w_.clear();
  vr_sk_drawing_ = false;
  if (pts.size() < 2) return;
  commit_undo();  // (re-check 385: a desk change still pending is its own step, never this stroke's)
  GameObject *g = vr_sketch_object();
  Strokes *s = g ? g->get<Strokes>() : nullptr;
  MeshFilter *mf = g ? g->mesh_filter() : nullptr;
  if (!s || !mf) return Log::warn("VR sketch: \"%s\" has no Strokes or mesh - the stroke is dropped", kVrSketchName);
  const Mat4 inv = g->world_matrix().inverse();
  const float to_local = length(inv.dir({1, 0, 0}));
  StrokeRibbon rb;
  rb.tube = vr_tube_, rb.material = vr_sketch_slot(*g, vr_colour_);
  rb.width = ws[0] * to_local;
  rb.facing = rb.surface_normal = normalize(inv.dir(ups[0]));
  for (size_t i = 0; i < pts.size(); i++) {
    rb.points.push_back(inv.point(pts[i]));
    rb.ups.push_back(normalize(inv.dir(ups[i])));
    rb.widths.push_back(ws[i] * to_local);
  }
  s->ribbons.push_back(std::move(rb));
  mf->mesh = mf->mesh ? s->with_last_ribbon(*mf->mesh) : s->build_mesh();
  mark_changed(vr_tube_ ? "VR Tube" : "VR Stroke");
}

/* A: the last step undone when it is a VR stroke (review 385: a desktop edit is not undone unseen from the headset). */
void Editor::vr_sketch_undo() {
  if (playing_) return Log::info("VR sketch: in Play, strokes go at Stop - nothing to undo");
  commit_undo();
  const bool stroke = !undo_.empty() && (undo_.back().label == "VR Stroke" || undo_.back().label == "VR Tube");
  if (!stroke) return Log::info("VR sketch: the last step is not a VR stroke - undo it at the desk");
  undo();
}

/* What sketching draws into an eye: the stroke under way, the tip and the brush's size at the right hand, the palette
 * round the left (the colour chosen ringed) and the tube's mark. */
void Editor::vr_sketch_overlay(const std::function<void(Vec3, Vec3, uint32_t, float)> &line) {
  const uint32_t ink = to_rgba(vr_palette_colour(vr_colour_));
  for (size_t i = 1; i < vr_sk_pts_.size(); i++) line(vr_sk_pts_[i - 1], vr_sk_pts_[i], ink, 3.0f);
  const xr::Hand &l = vr_->hand(0), &r = vr_->hand(1);
  if (r.tracked) {
    const Vec3 tip = vr_sketch_tip(r), side = xr::stage_dir(vr_stage_, r.aim.orientation.rotate({1, 0, 0})) * (vr_brush_ * vr_stage_.scale * 0.5f);
    line(tip - side, tip + side, ink, vr_tube_ ? 4.0f : 2.0f);
  }
  if (!l.tracked) return;
  const Vec3 centre = xr::stage_point(vr_stage_, l.grip.position);
  const float rad = kPaletteRing * vr_stage_.scale, dot_r = kPaletteDot * vr_stage_.scale;
  for (int k = 0; k < kVrPaletteSize; k++) {
    const float ang = k * kTwoPi / kVrPaletteSize;
    const Vec3 at = centre + xr::stage_dir(vr_stage_, l.grip.orientation.rotate({std::cos(ang), 0, std::sin(ang)})) * rad;
    const float big = k == vr_colour_ ? 2.0f : 1.0f;
    line(at - Vec3(dot_r * big, 0, 0), at + Vec3(dot_r * big, 0, 0), to_rgba(vr_palette_colour(k)), kPaletteDotPx * big);
  }
}

/* on / off / nothing (a toggle) into `flag`; false for anything else. */
static bool on_off(const std::string &arg, bool &flag) {
  if (arg != "" && arg != "on" && arg != "off") return false;
  flag = arg.empty() ? !flag : arg == "on";
  return true;
}

/* vr sketch [on|off] | tube [on|off] | colour <1-8> | brush <cm> */
void Editor::vr_sketch_command(const std::vector<std::string> &t) {
  const std::string verb = to_lower(t[1]);
  const std::string arg = t.size() > 2 ? to_lower(t[2]) : "";
  const float number = (float)std::atof(arg.c_str());
  const bool colour = verb == "colour" || verb == "color";
  if (verb == "sketch" && !on_off(arg, vr_sketch_)) return Log::warn("vr sketch wants on or off");
  if (verb == "tube" && !on_off(arg, vr_tube_)) return Log::warn("vr tube wants on or off");
  if (colour && !(number >= 1 && number <= kVrPaletteSize)) return Log::warn("vr colour wants 1 to %d", kVrPaletteSize);
  if (verb == "brush" && !(number > 0 && std::isfinite(number))) return Log::warn("vr brush wants a width in centimetres");
  if (colour) vr_colour_ = (int)number - 1;
  if (verb == "brush") vr_brush_ = std::clamp(number / 100.0f, kMinBrush, kMaxBrush);
  if (vr_sketch_) vr_tape_ = false;  // (the tape's buttons undo and switch the tube while sketching)
  if (!vr_sketch_ && vr_sk_drawing_) vr_sketch_commit();
  Log::info("VR sketch: %s; %s, colour %d, brush %.1f cm", vr_sketch_ ? "on - the right trigger draws" : "off", vr_tube_ ? "tubes" : "ribbons", vr_colour_ + 1,
            vr_brush_ * 100.0f);
}

}  // namespace bl

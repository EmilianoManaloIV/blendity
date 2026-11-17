// SPDX-License-Identifier: GPL-2.0-or-later
// Vertex Paint mode (task 0525, ADR 0152): a brush paints a mesh's active colour attribute - per vertex or per face
// corner - as Blender's Vertex Paint mode does (editors/sculpt_paint/mesh/paint_vertex.cc): Draw with its blend modes
// (Mix, Add, Subtract, Multiply, Lighten, Darken), Blur, Average and Smear; falloff curves; X mirror; Front Faces Only;
// the automasking of task 0519, shared with Weight Paint (paint_automask.cpp). Without Accumulate, a stroke changes an
// element at most by one dab's strength, from its colour when the stroke began (as Weight Paint, and Blender).
#include "editor.h"
#include "../core/core.h"
#include "../core/jobs.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace bl {

const char *const kVertexPaintTools[4] = {"Draw", "Blur", "Average", "Smear"};
const char *const kVertexPaintBlends[6] = {"Mix", "Add", "Subtract", "Multiply", "Lighten", "Darken"};

static float clamp01(float v) { return finite_bits(v) ? std::max(0.0f, std::min(v, 1.0f)) : 0.0f; }
/* A layer's colour channel: finite and not negative, but not clamped to 1 - a float layer keeps HDR colours (Blender's
 * float colour attributes do; an import may bring them). Alpha stays 0..1. */
static float colour_channel(float v) { return finite_bits(v) ? std::max(0.0f, v) : 0.0f; }
static Vec4 layer_colour(Vec4 c) { return Vec4(colour_channel(c.x), colour_channel(c.y), colour_channel(c.z), clamp01(c.w)); }

/* Blender's float colour blends (IMB_blend: blend_color_mix_float and the others), the colour taking `a` of the paint;
 * alpha moves towards the paint's under Mix only. */
static Vec4 blend_colour(int mode, Vec4 c, Vec4 p, float a) {
  auto ch = [&](float cv, float pv) {
    switch (mode) {
      case 1: return cv + pv * a;                               // Add
      case 2: return cv - pv * a;                               // Subtract
      case 3: return cv * ((1.0f - a) + pv * a);                // Multiply
      case 4: return cv + std::max(0.0f, pv - cv) * a;          // Lighten
      case 5: return cv + std::min(0.0f, pv - cv) * a;          // Darken
      default: return cv + (pv - cv) * a;                       // Mix
    }
  };
  return layer_colour(Vec4(ch(c.x, p.x), ch(c.y, p.y), ch(c.z, p.z), mode == 0 ? c.w + (p.w - c.w) * a : c.w));
}

/* The painted mesh, only while it is still the one the mode started on (a new scene or an undo that swapped it ends
 * it). */
Mesh *Editor::vp_mesh() {
  if (!vertex_paint_ || scene_.get() != vp_scene_) return nullptr;
  GameObject *g = scene_->find(vp_obj_);
  MeshFilter *mf = g ? g->mesh_filter() : nullptr;
  return mf && mf->mesh ? mf->mesh.get() : nullptr;
}

/* The active layer, when it fits its domain (nullptr: none to paint). */
static ColorAttribute *active_layer(Mesh &m) {
  if (m.active_color < 0 || m.active_color >= (int)m.color_attributes.size()) return nullptr;
  ColorAttribute &c = m.color_attributes[(size_t)m.active_color];
  return m.color_fits(c) ? &c : nullptr;
}

bool Editor::set_vertex_paint(bool on) {
  if (on && vertex_paint_ && active_ == vp_obj_ && scene_.get() == vp_scene_) return true;
  if (vertex_paint_) {  // off (or onto another object)
    vp_abort_stroke();
    vertex_paint_ = false;
    if (shading_ == Shading::Solid) shading_ = vp_saved_shading_;
    vp_obj_ = 0;
    vp_scene_ = nullptr;
    if (!on) Log::info("Vertex Paint off");
  }
  if (!on) return true;
  GameObject *g = active_object();
  MeshFilter *mf = g ? g->mesh_filter() : nullptr;
  if (!mf || !mf->mesh) {
    Log::warn("Vertex Paint: select a mesh object first");
    return false;
  }
  if (blockout_refuses_edit_mode(g)) return false;
  if (playing_) {
    refuse("Vertex Paint: stop Play first", "stop", "Stop Play");
    return false;
  }
  if (edit_mode_) exit_edit_mode();  // one mode at a time
  if (sculpt_) set_sculpt(false);
  if (paint_) set_paint(false);
  if (weight_paint_) set_weight_paint(false);
  if (!active_layer(*mf->mesh)) {  // something to paint: a white Point layer, as Blender adds "Color" on entering
    Mesh &m = *mesh_make_mutable(mf->mesh);
    m.prune_colors();
    if (m.color_attributes.empty()) m.color_attributes.push_back(m.make_color("Color", ColorAttribute::Domain::Point, Vec4(1, 1, 1, 1)));
    if (m.active_color < 0) m.active_color = 0;
    if (m.render_color < 0) m.render_color = m.active_color;
    m.touch();
    mark_changed("Add Color Attribute");
  }
  vertex_paint_ = true;
  vp_obj_ = g->id;
  vp_scene_ = scene_.get();
  vp_saved_shading_ = shading_;
  shading_ = Shading::Solid;  // the colours as vertex colours, lit (Blender's Vertex Paint shows them in Solid)
  vp_mh_.forget();
  vp_version_++;
  const Mesh &m = *mf->mesh;
  Log::info("Vertex Paint on '%s', layer '%s'", g->name.c_str(), m.color_attributes[(size_t)m.active_color].name.c_str());
  return true;
}

bool Editor::vp_begin_stroke() {
  GameObject *g = vp_mesh() ? scene_->find(vp_obj_) : nullptr;
  MeshFilter *mf = g ? g->mesh_filter() : nullptr;
  if (!mf || !mf->mesh) return false;
  Mesh &m = *mesh_make_mutable(mf->mesh);  // the snapshot keeps the colours before the stroke (undo)
  m.prune_colors();
  m.dropped_colors.clear();  // (nothing an edit did: the stroke paints what fits)
  ColorAttribute *c = active_layer(m);
  if (!c) return false;
  vp_mh_.prepare(m, vp_brush_.mirror_x);
  vp_start_ = c->rgba;
  vp_alpha_.assign(c->rgba.size(), 0.0f);
  vp_stroke_flip_ = ui_.in.ctrl() || ui_.in.eraser;
  vp_am_.started = false;
  vp_stroke_mesh_ = &m;
  vp_stroking_ = true;
  vp_dabbed_ = false;
  vp_smear_dir_ = Vec2(0, 0);
  return true;
}

void Editor::vp_end_stroke() {
  if (!vp_stroking_) return;
  vp_stroking_ = false;
  Mesh *m = vp_mesh();
  if (!vp_dabbed_ || !m || m != vp_stroke_mesh_) return;  // nothing painted: no undo step
  m->touch();
  vp_mh_.forget();
  mark_changed("Vertex Paint");  // the whole stroke, one undo step
}

void Editor::vp_abort_stroke() {
  vp_stroking_ = false;
  vp_stroke_mesh_ = nullptr;
}

/* The per-vertex colour a tool reads: the vertex's own (Point) or the mean of its corners' (Corner). */
static std::vector<Vec4> vertex_colours(const Mesh &m, const ColorAttribute &c) {
  if (c.domain == ColorAttribute::Domain::Point) return c.rgba;
  std::vector<Vec4> sum(m.vert_count(), Vec4(0, 0, 0, 0));
  std::vector<int> n(m.vert_count(), 0);
  for (size_t k = 0; k < m.corner_verts.size() && k < c.rgba.size(); k++) {
    const uint32_t v = m.corner_verts[k];
    if (v >= sum.size()) continue;
    sum[v] = sum[v] + c.rgba[k];
    n[v]++;
  }
  for (size_t v = 0; v < sum.size(); v++) sum[v] = n[v] ? sum[v] * (1.0f / (float)n[v]) : Vec4(1, 1, 1, 1);
  return sum;
}

/* What each vertex under the brush moves towards, for Blur, Average and Smear (has[v]: it has one). */
struct ToolTargets {
  std::vector<Vec4> target;
  std::vector<uint8_t> has;
};

/* Average: the colour under the whole brush, weighted by the amount (Blender's Average brush). */
static void average_targets(const std::vector<Vec4> &vc, const std::vector<float> &amount, ToolTargets &t) {
  Vec4 sum(0, 0, 0, 0);
  float wsum = 0.0f;
  for (size_t v = 0; v < vc.size(); v++)
    if (amount[v] > 0.0f) sum = sum + vc[v] * amount[v], wsum += amount[v];
  if (!(wsum > 0.0f)) return;
  for (size_t v = 0; v < vc.size(); v++) t.target[v] = sum * (1.0f / wsum), t.has[v] = amount[v] > 0.0f;
}

/* Blur: each vertex towards its neighbours' mean. */
static void blur_targets(const std::vector<Vec4> &vc, const std::vector<float> &amount, const PaintMeshHelpers &h, ToolTargets &t) {
  for (size_t v = 0; v < vc.size(); v++) {
    const uint32_t k0 = v < h.nb_off.size() ? h.nb_off[v] : 0, k1 = v + 1 < h.nb_off.size() ? h.nb_off[v + 1] : k0;
    if (!(amount[v] > 0.0f) || k1 <= k0) continue;
    Vec4 s(0, 0, 0, 0);
    for (uint32_t k = k0; k < k1; k++) s = s + vc[h.nb[k]];
    t.target[v] = s * (1.0f / (float)(k1 - k0)), t.has[v] = 1;
  }
}

/* Smear: each vertex takes the colour of the neighbour behind it along the stroke's screen direction (Blender: the one
 * most opposite the stroke) - nothing before the stroke has moved. */
void Editor::vp_smear_targets(const Mesh &m, const Mat4 &W, const std::vector<Vec4> &vc, const std::vector<float> &amount, ToolTargets &t) {
  if (vp_smear_dir_.x == 0.0f && vp_smear_dir_.y == 0.0f) return;
  for (size_t v = 0; v < vc.size(); v++) {
    const uint32_t k0 = v < vp_mh_.nb_off.size() ? vp_mh_.nb_off[v] : 0, k1 = v + 1 < vp_mh_.nb_off.size() ? vp_mh_.nb_off[v + 1] : k0;
    Vec2 sv;
    float z;
    if (!(amount[v] > 0.0f) || k1 <= k0 || !scene_r3d_.project(W.point(m.positions[v]), sv, z)) continue;
    float best = 0.0f;
    for (uint32_t k = k0; k < k1; k++) {
      Vec2 sn;
      if (!scene_r3d_.project(W.point(m.positions[vp_mh_.nb[k]]), sn, z)) continue;
      const Vec2 dv = sn - sv;
      const float l = length(dv);
      const float along = l > 0.0f ? -(dv.x * vp_smear_dir_.x + dv.y * vp_smear_dir_.y) / l : 0.0f;
      if (along > best) best = along, t.target[v] = vc[vp_mh_.nb[k]], t.has[v] = 1;
    }
  }
}

/* Draw: the brush colour blended in, from each element's colour when the stroke began, at most one dab's worth.
 * Ctrl (or the pen's eraser) at the stroke's start: Mix paints the inverse colour, Add and Subtract swap, Lighten and
 * Darken swap - Blendity's own reading of Ctrl, matching Weight Paint's; Blender paints its secondary colour there. */
bool Editor::vp_draw_dab(const Mesh &m, ColorAttribute &layer, const std::vector<float> &amount) {
  Vec4 paint(clamp01(vp_brush_.color.x), clamp01(vp_brush_.color.y), clamp01(vp_brush_.color.z), clamp01(vp_brush_.alpha));
  int blend = std::max(0, std::min(vp_brush_.blend, 5));
  if (vp_stroke_flip_ && blend == 0) paint = Vec4(1.0f - paint.x, 1.0f - paint.y, 1.0f - paint.z, paint.w);
  if (vp_stroke_flip_ && (blend == 1 || blend == 2)) blend = 3 - blend;
  if (vp_stroke_flip_ && (blend == 4 || blend == 5)) blend = 9 - blend;
  const bool corner = layer.domain == ColorAttribute::Domain::Corner;
  bool changed = false;
  for (size_t e = 0; e < layer.rgba.size(); e++) {
    const size_t v = corner ? (e < m.corner_verts.size() ? m.corner_verts[e] : SIZE_MAX) : e;
    if (v >= amount.size() || amount[v] <= vp_alpha_[e]) continue;  // within this stroke, the strongest dab so far decides
    vp_alpha_[e] = amount[v];
    const Vec4 nc = blend_colour(blend, vp_start_[e], paint, vp_alpha_[e]);
    changed = changed || std::memcmp(&nc, &layer.rgba[e], sizeof(Vec4)) != 0;
    layer.rgba[e] = nc;
  }
  return changed;
}

/* One dab at (x, y) in the Scene view's own pixels. Draw blends the brush colour in; Blur, Average and Smear move each
 * element towards a colour read from the mesh before the dab - those three build up dab after dab, as Blender's do. */
void Editor::vp_dab(float x, float y) {
  Mesh *mp = vp_mesh();
  GameObject *g = mp ? scene_->find(vp_obj_) : nullptr;
  if (!mp || !g || mp != vp_stroke_mesh_) {
    vp_abort_stroke();
    return;
  }
  Mesh &m = *mp;
  ColorAttribute *layer = active_layer(m);
  if (!layer || vp_start_.size() != layer->rgba.size() || vp_mh_.normals.size() != m.vert_count()) return;
  const float pen = std::isfinite(ui_.in.pressure) ? std::max(0.0f, std::min(ui_.in.pressure, 1.0f)) : 1.0f;
  PaintDab d;
  d.mesh = &m, d.world = &g->world_matrix(), d.x = x, d.y = y, d.radius = std::max(1.0f, vp_brush_.radius * ui_.scale);
  d.strength = clamp01(vp_brush_.strength) * pen;
  d.falloff = vp_brush_.falloff, d.front_only = vp_brush_.front_only, d.mirror_x = vp_brush_.mirror_x, d.automask = &vp_brush_, d.obj = vp_obj_;
  std::vector<float> amount, mask;
  if (!paint_dab_amounts(d, vp_mh_, vp_am_, amount, mask)) return;
  bool changed = false;
  if (vp_brush_.tool == 0) changed = vp_draw_dab(m, *layer, amount);
  else {
    const std::vector<Vec4> vc = vertex_colours(m, *layer);  // before this dab
    ToolTargets t;
    t.target.assign(m.vert_count(), Vec4(0, 0, 0, 0));
    t.has.assign(m.vert_count(), 0);
    if (vp_brush_.tool == 1) blur_targets(vc, amount, vp_mh_, t);
    else if (vp_brush_.tool == 2) average_targets(vc, amount, t);
    else vp_smear_targets(m, g->world_matrix(), vc, amount, t);
    const bool corner = layer->domain == ColorAttribute::Domain::Corner;
    for (size_t e = 0; e < layer->rgba.size(); e++) {
      const size_t v = corner ? (e < m.corner_verts.size() ? m.corner_verts[e] : SIZE_MAX) : e;
      if (v >= t.has.size() || !t.has[v] || !(amount[v] > 0.0f)) continue;
      const Vec4 nc = layer_colour(lerp(layer->rgba[e], t.target[v], amount[v]));
      changed = changed || std::memcmp(&nc, &layer->rgba[e], sizeof(Vec4)) != 0;
      layer->rgba[e] = nc;
    }
  }
  if (changed) {
    vp_dabbed_ = true;
    vp_version_++;
  }
}

/* From the last dab to (x, y), a dab every quarter radius (a fast drag doesn't skip); the stroke's direction kept for
 * Smear. */
void Editor::vp_stroke_to(float x, float y, bool first) {
  const float step = std::max(1.0f, vp_brush_.radius * ui_.scale * 0.25f);
  if (first) {
    vp_dab(x, y);
    vp_last_ = Vec2(x, y);
    return;
  }
  const Vec2 to(x, y), d = to - vp_last_;
  const float len = length(d);
  if (len < step) return;
  vp_smear_dir_ = d * (1.0f / len);
  const int k = std::min(256, (int)(len / step));
  for (int i = 1; i <= k && vp_stroking_; i++) vp_dab(vp_last_.x + d.x * (i / (float)k), vp_last_.y + d.y * (i / (float)k));
  vp_last_ = to;
}

bool Editor::vp_update(const Recti &view) {
  if (!vertex_paint_) return false;
  auto &in = ui_.in;
  /* One mode at a time: another mode entered (Sculpt, Paint, Weight Paint, Edit Mode) or Play ends this one, wherever
   * it was entered from. */
  if (!vp_mesh() || sculpt_ || paint_ || weight_paint_ || edit_mode_ || playing_) {
    set_vertex_paint(false);
    return false;
  }
  const float x = (float)(in.mx - view.x), y = (float)(in.my - view.y);
  if (!vp_stroking_) {
    if (scene_hovered_ && in.key_pressed[platform::KEY_ESCAPE] && !ui_.wants_keyboard()) {
      set_vertex_paint(false);  // Esc leaves the mode
      return true;
    }
    if (scene_hovered_ && in.pressed[0] && !in.alt() && !ui_.any_active() && vp_begin_stroke()) {
      vp_stroke_to(x, y, true);
      return true;
    }
    return false;
  }
  if (in.down[0]) vp_stroke_to(x, y, false);
  else vp_end_stroke();
  return true;
}

void Editor::vp_draw(const Recti &view) {
  if (!vertex_paint_ || !scene_hovered_) return;
  auto &u = ui_;
  const float R = std::max(1.0f, vp_brush_.radius * u.scale);
  const Vec3 c = vp_brush_.color;
  const uint32_t rgb = (uint32_t)(clamp01(c.x) * 255.0f) << 16 | (uint32_t)(clamp01(c.y) * 255.0f) << 8 | (uint32_t)(clamp01(c.z) * 255.0f);
  draw_brush_ring((float)u.in.mx, (float)u.in.my, R, u.in.ctrl() || u.in.eraser, Color::hex(0xFFFFFF, 200));
  u.canvas.circle((float)u.in.mx, (float)u.in.my, R * std::max(0.05f, std::min(1.0f, vp_brush_.strength)), Color::hex(rgb, 200), 1.0f);
  (void)view;
}

/* The painted layer's colours per render vertex, for the Scene view (rebuilt when the colours or the render mesh
 * change). A Corner layer's corners that share a render vertex show the last one's colour. */
const std::vector<Vec3> *Editor::vp_colors(const Mesh &m, const RenderMesh &rm) {
  if (m.active_color < 0 || m.active_color >= (int)m.color_attributes.size()) return nullptr;
  const ColorAttribute &c = m.color_attributes[(size_t)m.active_color];
  if (!m.color_fits(c)) return nullptr;
  if (vp_colors_serial_ == rm.serial && vp_colors_version_ == vp_version_ && vp_colors_.size() == rm.positions.size()) return &vp_colors_;
  vp_colors_.assign(rm.positions.size(), Vec3(1, 1, 1));
  const bool corner = c.domain == ColorAttribute::Domain::Corner;
  for (size_t k = 0; k < rm.indices.size() && k < rm.tri_corner.size(); k++) {
    const uint32_t cn = rm.tri_corner[k];
    if (cn >= m.corner_verts.size() || rm.indices[k] >= vp_colors_.size()) continue;
    const size_t e = corner ? cn : m.corner_verts[cn];
    if (e < c.rgba.size()) vp_colors_[rm.indices[k]] = Vec3(c.rgba[e].x, c.rgba[e].y, c.rgba[e].z);
  }
  vp_colors_serial_ = rm.serial, vp_colors_version_ = vp_version_;
  return &vp_colors_;
}

/* vpaint on|off|toggle; vpbrush <draw|blur|average|smear> [radius] [strength]; vpblend <mix|add|subtract|multiply|
 * lighten|darken>; vpcolor r g b [a] (linear, 0..1); vpset falloff <name> | mirror on|off | front on|off;
 * vpautomask (paint_automask.cpp); vpstroke x y x y ... (window pixels: one stroke). */
void Editor::vertex_paint_command(const std::vector<std::string> &t) {
  auto arg = [&](size_t i, const std::string &def) { return t.size() > i ? t[i] : def; };
  auto num = [&](size_t i, float &out) {  // a whole, finite number, or nothing changes
    if (t.size() <= i) return false;
    char *e = nullptr;
    const float v = std::strtof(t[i].c_str(), &e);
    if (!e || e == t[i].c_str() || *e || !finite_bits(v)) return false;
    out = v;
    return true;
  };
  const std::string &c = t[0];
  if (c == "vpaint") {
    const std::string v = to_lower(arg(1, "toggle"));
    set_vertex_paint(v == "on" ? true : v == "off" ? false : !vertex_paint_);
    return;
  }
  if (c == "vpbrush") {
    const std::string tool = to_lower(arg(1, ""));
    for (int i = 0; i < 4; i++)
      if (tool == to_lower(kVertexPaintTools[i])) vp_brush_.tool = i;
    float v = 0.0f;
    if (num(2, v)) vp_brush_.radius = std::max(1.0f, std::min(v, 1000.0f));
    if (num(3, v)) vp_brush_.strength = std::max(0.0f, std::min(v, 1.0f));
    return;
  }
  if (c == "vpblend") {
    for (int i = 0; i < 6; i++)
      if (to_lower(arg(1, "")) == to_lower(kVertexPaintBlends[i])) vp_brush_.blend = i;
    return;
  }
  if (c == "vpcolor") {
    float r, gg, b, a = vp_brush_.alpha;
    if (!num(1, r) || !num(2, gg) || !num(3, b)) {
      Log::warn("vpcolor r g b [a]: linear values 0..1");
      return;
    }
    num(4, a);
    vp_brush_.color = Vec3(clamp01(r), clamp01(gg), clamp01(b));
    vp_brush_.alpha = clamp01(a);
    return;
  }
  if (c == "vpset") {
    const std::string what = to_lower(arg(1, "")), val = to_lower(arg(2, ""));
    if (what == "falloff")
      for (int i = 0; i < 6; i++) {
        if (val == to_lower(kWeightFalloffs[i])) vp_brush_.falloff = i;
      }
    else if (what == "mirror") vp_brush_.mirror_x = val == "on";
    else if (what == "front") vp_brush_.front_only = val == "on";
    else Log::warn("vpset falloff <curve> | mirror on|off | front on|off");
    return;
  }
  if (c == "vpautomask") {
    paint_automask_command(vp_brush_, t, "Vertex Paint");
    return;
  }
  if (c == "vpstroke") {
    if (!vertex_paint_ || t.size() < 3 || !vp_begin_stroke()) {
      Log::warn("vpstroke: turn Vertex Paint on and give screen points");
      return;
    }
    const Recti v = scene_rect_;
    for (size_t i = 1; i + 1 < t.size() && vp_stroking_; i += 2) {
      const float x = (float)std::atof(t[i].c_str()) - v.x, y = (float)std::atof(t[i + 1].c_str()) - v.y;
      vp_stroke_to(x, y, i == 1);
    }
    vp_end_stroke();
  }
}

void Editor::carry_colors(MeshPtr &mesh, const Mesh *before, uint64_t obj) {
  if (!mesh) return;
  /* Layers the operator left unfit (the wrong size, or emptied by a renumbering that met them stale - so a count
   * coincidence can't make one fit on the wrong vertices) come back by position; a layer of the right size was carried
   * by the operator itself and is left exactly as it is. */
  bool unfit = false;
  for (const ColorAttribute &c : mesh->color_attributes) unfit = unfit || !mesh->color_fits(c);
  if (!unfit && mesh->dropped_colors.empty()) return;
  Mesh &m = *mesh_make_mutable(mesh);
  if (before) meshops::transfer_point_colors(*before, m, false);
  else m.prune_colors();
  if (!m.dropped_colors.empty()) {
    std::string names;
    for (const std::string &n : m.dropped_colors) names += (names.empty() ? "" : ", ") + n;
    colors_dropped_note_ = strprintf("Colour layer %s dropped by the last edit: it couldn't follow how the edit rebuilt the mesh. Undo brings it back.",
                                     names.c_str());
    colors_dropped_obj_ = obj;
    Log::warn("%s", colors_dropped_note_.c_str());
    m.dropped_colors.clear();
  }
  vp_version_++;
  m.touch();
}

/* colorattr add <name> [point|corner]; colorattr remove|active|render <name>; colorattr rename <old> <new>;
 * colorattr fill r g b [a] (the active layer) - the active object's mesh, each one undo step (ADR 0152). */
void Editor::color_attribute_command(const std::vector<std::string> &t) {
  GameObject *g = vertex_paint_ && vp_mesh() ? scene_->find(vp_obj_) : active_object();
  MeshFilter *mf = g ? g->mesh_filter() : nullptr;
  if (!mf || !mf->mesh) {
    Log::warn("colorattr: select a mesh object first");
    return;
  }
  const std::string op = t.size() > 1 ? to_lower(t[1]) : "", name = t.size() > 2 ? t[2] : "";
  const Mesh &cm = *mf->mesh;
  const int at = cm.find_color(name);
  float v[4] = {1, 1, 1, 1};
  bool fill_ok = op == "fill" && cm.active_color >= 0 && cm.active_color < (int)cm.color_attributes.size() && t.size() >= 5;
  for (int k = 0; fill_ok && k < 4 && (size_t)(2 + k) < t.size(); k++) {
    char *e = nullptr;
    v[k] = std::strtof(t[(size_t)(2 + k)].c_str(), &e);
    fill_ok = e && e != t[(size_t)(2 + k)].c_str() && !*e && finite_bits(v[k]);
  }
  /* Checked before the mesh is copied: a refused command changes nothing. */
  const bool ok = (op == "add" && !name.empty() && at < 0) || (op == "remove" && at >= 0) || ((op == "active" || op == "render") && at >= 0) ||
                  (op == "rename" && at >= 0 && t.size() > 3 && !t[3].empty() && cm.find_color(t[3]) < 0) || fill_ok;
  if (!ok) {
    Log::warn("colorattr add <name> [point|corner] | remove|active|render <name> | rename <old> <new> | fill r g b [a]");
    return;
  }
  Mesh &m = *mesh_make_mutable(mf->mesh);
  if (op == "add") {
    const bool corner = t.size() > 3 && to_lower(t[3]) == "corner";
    m.color_attributes.push_back(m.make_color(name, corner ? ColorAttribute::Domain::Corner : ColorAttribute::Domain::Point, Vec4(1, 1, 1, 1)));
    m.active_color = (int)m.color_attributes.size() - 1;
    if (m.render_color < 0) m.render_color = m.active_color;
  }
  else if (op == "remove") {
    m.color_attributes.erase(m.color_attributes.begin() + at);
    auto after = [&](int i) { return i == at ? (m.color_attributes.empty() ? -1 : 0) : i > at ? i - 1 : i; };
    m.active_color = after(m.active_color), m.render_color = after(m.render_color);
  }
  else if (op == "active" || op == "render") (op == "active" ? m.active_color : m.render_color) = at;
  else if (op == "rename") m.color_attributes[(size_t)at].name = t[3];
  else
    for (Vec4 &c : m.color_attributes[(size_t)m.active_color].rgba) c = layer_colour(Vec4(v[0], v[1], v[2], v[3]));
  m.touch();
  vp_version_++;
  mark_changed("Color Attribute");
}

}  // namespace bl

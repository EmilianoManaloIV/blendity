// SPDX-License-Identifier: GPL-2.0-or-later
// Weight Paint mode (task 0046): a brush paints a vertex group of one mesh, shown in Blender's blue-green-red weight
// colours. Blender: editors/sculpt_paint/paint_weight.cc (brush blend modes Add, Subtract, Mix ("Draw") and Blur;
// falloff curves; X mirror; Front Faces Only; without Accumulate a stroke changes a vertex at most by one dab's
// strength, from its weight when the stroke began).
#include "editor.h"
#include "../core/core.h"
#include "../core/jobs.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace bl {

const char *const kWeightBrushModes[4] = {"Add", "Subtract", "Draw", "Blur"};
const char *const kWeightFalloffs[6] = {"Smooth", "Sphere", "Root", "Sharp", "Linear", "Constant"};

/* Blender's weight colours: blue 0, cyan 0.25, green 0.5, yellow 0.75, red 1. */
Vec3 weight_color(float w) {
  w = finite_bits(w) ? std::max(0.0f, std::min(w, 1.0f)) : 0.0f;
  static const Vec3 stops[5] = {{0, 0, 1}, {0, 1, 1}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}};
  const float x = w * 4.0f;
  const int i = std::min(3, (int)x);
  return stops[i] + (stops[i + 1] - stops[i]) * (x - (float)i);
}

/* The proportional-editing curves (1 at the centre, 0 at the radius). */
static float falloff(int kind, float t) {
  t = std::max(0.0f, std::min(1.0f, 1.0f - t));
  switch (kind) {
    case 1: return std::sqrt(std::max(0.0f, 2.0f * t - t * t));
    case 2: return std::sqrt(t);
    case 3: return t * t;
    case 4: return t;
    case 5: return 1.0f;
    default: return t * t * (3.0f - 2.0f * t);
  }
}

/* The painted mesh, only while it is still the one the mode started on: a new scene, or an undo that swapped the
 * scene, ends it. */
Mesh *Editor::wp_mesh() {
  if (!weight_paint_ || scene_.get() != wp_scene_) return nullptr;
  GameObject *g = scene_->find(wp_obj_);
  MeshFilter *mf = g ? g->mesh_filter() : nullptr;
  return mf && mf->mesh ? mf->mesh.get() : nullptr;
}

int Editor::wp_group(const Mesh &m) const {
  return m.vertex_groups.empty() ? -1 : std::max(0, std::min(wp_group_, (int)m.vertex_groups.size() - 1));
}

bool Editor::set_weight_paint(bool on) {
  if (on && weight_paint_ && active_ == wp_obj_ && scene_.get() == wp_scene_) return true;
  if (weight_paint_) {  // off (or onto another object)
    wp_abort_stroke();
    weight_paint_ = false;
    if (shading_ == Shading::Solid) shading_ = wp_saved_shading_;  // unless the user chose another meanwhile
    wp_obj_ = 0;
    wp_scene_ = nullptr;
    if (!on) Log::info("Weight Paint off");
  }
  if (!on) return true;
  GameObject *g = active_object();
  MeshFilter *mf = g ? g->mesh_filter() : nullptr;
  if (!mf || !mf->mesh) {
    Log::warn("Weight Paint: select a mesh object first");
    return false;
  }
  if (playing_) {
    Log::warn("Weight Paint: stop Play first");
    return false;
  }
  if (edit_mode_) exit_edit_mode();
  if (sculpt_) set_sculpt(false);  // (task 0236: one mode at a time)
  if (mf->mesh->vertex_groups.empty()) vgroup_op("add", "", "", 1.0f);  // something to paint
  weight_paint_ = true;
  wp_obj_ = g->id;
  wp_last_active_ = active_;  // (the mesh: no bone to follow yet)
  wp_scene_ = scene_.get();
  wp_group_ = std::max(0, std::min(active_vgroup_, (int)mf->mesh->vertex_groups.size() - 1));
  wp_saved_shading_ = shading_;
  shading_ = Shading::Solid;  // weights show as vertex colours, Gouraud-shaded (Blender's weight view is lit the same way)
  wp_cache_mesh_ = nullptr;
  wp_version_++;
  Log::info("Weight Paint on '%s', group '%s'", g->name.c_str(), mf->mesh->vertex_groups[(size_t)wp_group_].name.c_str());
  return true;
}

/* The per-mesh helpers a stroke needs: normals, neighbours and the X-mirror map, kept while the mesh is unchanged. */
void Editor::wp_prepare(const Mesh &m) {
  if (wp_cache_mesh_ == &m && wp_cache_version_ == m.version && wp_normals_.size() == m.vert_count() && wp_mirror_done_ == wp_brush_.mirror_x)
    return;
  wp_normals_ = meshops::vertex_normals(m);
  meshops::vertex_neighbors(m, wp_nb_off_, wp_nb_);
  wp_mirror_.assign(m.vert_count(), UINT32_MAX);
  if (wp_brush_.mirror_x && m.vert_count()) {
    /* Twins across the object's X = 0 plane, within a hundredth of a typical edge (not of the whole size: one stray
     * far vertex must not put every vertex in one cell). */
    std::vector<float> lens;
    for (size_t f = 0; f < m.face_count() && lens.size() < 4096; f++)
      for (uint32_t k = 0, n = m.face_size(f); k < n; k++) {
        const float l = length(m.positions[m.face_verts(f)[k]] - m.positions[m.face_verts(f)[(k + 1) % n]]);
        if (finite_bits(l) && l > 0.0f) lens.push_back(l);
      }
    float typical = 1.0f;
    if (!lens.empty()) {
      std::nth_element(lens.begin(), lens.begin() + lens.size() / 2, lens.end());
      typical = lens[lens.size() / 2];
    }
    const float eps = std::max(1e-7f, typical * 0.01f);
    std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
    grid.reserve(m.vert_count());
    auto key = [&](Vec3 p) { return meshops::grid_key(meshops::grid_cell(p.x / eps), meshops::grid_cell(p.y / eps), meshops::grid_cell(p.z / eps)); };
    for (uint32_t i = 0; i < (uint32_t)m.vert_count(); i++) grid[key(m.positions[i])].push_back(i);
    for (uint32_t i = 0; i < (uint32_t)m.vert_count(); i++) {
      const Vec3 q(-m.positions[i].x, m.positions[i].y, m.positions[i].z);
      const int64_t cx = meshops::grid_cell(q.x / eps), cy = meshops::grid_cell(q.y / eps), cz = meshops::grid_cell(q.z / eps);
      float best = eps * eps;
      for (int dz = -1; dz <= 1; dz++)
        for (int dy = -1; dy <= 1; dy++)
          for (int dx = -1; dx <= 1; dx++) {
            auto it = grid.find(meshops::grid_key(cx + dx, cy + dy, cz + dz));
            if (it == grid.end()) continue;
            for (uint32_t j : it->second)
              if (length_sq(m.positions[j] - q) <= best) best = length_sq(m.positions[j] - q), wp_mirror_[i] = j;
          }
    }
  }
  wp_cache_mesh_ = &m;
  wp_cache_version_ = m.version;
  wp_mirror_done_ = wp_brush_.mirror_x;
}

bool Editor::wp_begin_stroke() {
  GameObject *g = wp_mesh() ? scene_->find(wp_obj_) : nullptr;
  MeshFilter *mf = g ? g->mesh_filter() : nullptr;
  if (!mf || !mf->mesh || mf->mesh->vertex_groups.empty()) return false;
  Mesh &m = *mesh_make_mutable(mf->mesh);  // the snapshot keeps the weights before the stroke (undo)
  for (VertexGroup &vg : m.vertex_groups) vg.weights.resize(m.vert_count(), 0.0f);
  wp_prepare(m);
  wp_start_ = m.vertex_groups[(size_t)wp_group(m)].weights;  // a stroke's changes are measured from here (no accumulation)
  wp_alpha_.assign(m.vert_count(), 0.0f);
  wp_stroke_mesh_ = &m;
  wp_stroking_ = true;
  wp_dabbed_ = false;
  return true;
}

void Editor::wp_end_stroke() {
  if (!wp_stroking_) return;
  wp_stroking_ = false;
  Mesh *m = wp_mesh();
  if (!wp_dabbed_ || !m || m != wp_stroke_mesh_) return;  // nothing painted: no undo step (and no rebuild)
  m->touch();  // deformers limited to the group re-read it
  wp_cache_mesh_ = nullptr;  // (a new version: the helpers are made again next stroke)
  mark_changed("Weight Paint");  // the whole stroke, one undo step
}

/* A stroke cut off by something that replaced the mesh or the scene (an undo, a new scene, Play): nothing more is
 * written, and it makes no undo step. */
void Editor::wp_abort_stroke() {
  wp_stroking_ = false;
  wp_stroke_mesh_ = nullptr;
}

/* One dab at (x, y) in the Scene view's own pixels. */
void Editor::wp_dab(float x, float y) {
  Mesh *mp = wp_mesh();
  GameObject *g = mp ? scene_->find(wp_obj_) : nullptr;
  if (!mp || !g || mp != wp_stroke_mesh_ || mp->vertex_groups.empty()) {
    wp_abort_stroke();
    return;
  }
  Mesh &m = *mp;
  std::vector<float> &w = m.vertex_groups[(size_t)wp_group(m)].weights;
  if (w.size() != m.vert_count() || wp_normals_.size() != m.vert_count() || wp_start_.size() != w.size()) return;
  const Mat4 &W = g->world_matrix();
  const Mat4 N = W.inverse().transposed();
  const float R = std::max(1.0f, wp_brush_.radius * ui_.scale);
  const float pen = std::isfinite(ui_.in.pressure) ? std::max(0.0f, std::min(ui_.in.pressure, 1.0f)) : 1.0f;  // (a pen presses lighter)
  const float strength = finite_bits(wp_brush_.strength) ? std::max(0.0f, std::min(wp_brush_.strength, 1.0f)) * pen : 0.0f;
  const float target = finite_bits(wp_brush_.weight) ? std::max(0.0f, std::min(wp_brush_.weight, 1.0f)) : 1.0f;
  const Vec3 eye = cam_.position(), fwd = cam_.forward();
  const size_t n = m.vert_count();
  std::vector<float> amount(n, 0.0f);  // how much each vertex takes this dab (0: outside the brush)
  JobSystem::global().parallel_for((int64_t)n, 2048, [&](int64_t b, int64_t e) {
    for (int64_t v = b; v < e; v++) {
      const Vec3 p = W.point(m.positions[(size_t)v]);
      Vec2 sp;
      float z;
      if (!scene_r3d_.project(p, sp, z)) continue;
      const float d = length(Vec2(sp.x - x, sp.y - y));
      if (!(d < R)) continue;
      if (wp_brush_.front_only) {  // facing the camera (Blender: Front Faces Only)
        const Vec3 nw = N.dir(wp_normals_[(size_t)v]);
        const Vec3 to_cam = cam_.ortho ? -fwd : eye - p;
        if (!(dot(nw, to_cam) > 0.0f)) continue;
      }
      amount[(size_t)v] = strength * falloff(wp_brush_.falloff, d / R);
    }
  });
  /* Mirror X: a twin takes the larger of its own and its partner's amount (each pair painted once). */
  if (wp_brush_.mirror_x)
    for (size_t v = 0; v < n; v++)
      if (amount[v] > 0.0f && v < wp_mirror_.size() && wp_mirror_[v] != UINT32_MAX) {
        const uint32_t t = wp_mirror_[v];
        amount[t] = std::max(amount[t], amount[v]);
      }
  bool changed = false;
  if (wp_brush_.mode == 3) {  // Blur: towards the neighbours' average, dab after dab
    const std::vector<float> before = w;
    for (size_t v = 0; v < n; v++) {
      const float a = amount[v];
      const uint32_t k0 = v < wp_nb_off_.size() ? wp_nb_off_[v] : 0, k1 = v + 1 < wp_nb_off_.size() ? wp_nb_off_[v + 1] : k0;
      if (a <= 0.0f || k1 <= k0) continue;
      float sum = 0.0f;
      for (uint32_t k = k0; k < k1; k++) sum += before[wp_nb_[k]];
      const float nw = std::max(0.0f, std::min(1.0f, before[v] + (sum / (float)(k1 - k0) - before[v]) * a));
      changed = changed || nw != w[v];
      w[v] = nw;
    }
  }
  else
    for (size_t v = 0; v < n; v++) {
      if (amount[v] <= wp_alpha_[v]) continue;  // within this stroke, the strongest dab so far decides
      wp_alpha_[v] = amount[v];
      const float a = wp_alpha_[v], s = wp_start_[v];
      float nw = wp_brush_.mode == 0 ? s + a * target : wp_brush_.mode == 1 ? s - a * target : s + (target - s) * a;
      nw = std::max(0.0f, std::min(nw, 1.0f));
      changed = changed || nw != w[v];
      w[v] = nw;
    }
  if (changed) {
    wp_dabbed_ = true;
    wp_version_++;
  }
}

/* From the last dab to (x, y), a dab every quarter radius (a fast drag doesn't skip). */
void Editor::wp_stroke_to(float x, float y, bool first) {
  const float step = std::max(1.0f, wp_brush_.radius * ui_.scale * 0.25f);
  if (first) {
    wp_dab(x, y);
    wp_last_ = Vec2(x, y);
    return;
  }
  const Vec2 to(x, y), d = to - wp_last_;
  const float len = length(d);
  if (len < step) return;
  const int k = std::min(256, (int)(len / step));
  for (int i = 1; i <= k && wp_stroking_; i++) wp_dab(wp_last_.x + d.x * (i / (float)k), wp_last_.y + d.y * (i / (float)k));
  wp_last_ = to;
}

bool Editor::wp_update(const Recti &view) {
  if (!weight_paint_) return false;
  auto &in = ui_.in;
  if (!wp_mesh()) {  // the object is gone, or another scene
    set_weight_paint(false);
    return false;
  }
  /* A bone selected while painting paints its own group, the one named after it (Blender: selecting a bone in
   * Weight Paint mode; task 0052). */
  if (active_ != wp_last_active_ && !wp_stroking_) {  // (a change mid-stroke is taken when the stroke ends)
    wp_last_active_ = active_;
    const GameObject *a = active_object();
    if (a && a->get<Bone>()) {
      const int gi = wp_mesh()->find_group(a->name);
      if (gi >= 0) {
        wp_group_ = active_vgroup_ = gi;
        wp_version_++;
        ui_.redraw = true;
      }
      else Log::info("Weight Paint: the mesh has no group named '%s' (Rigging > Skin > Bind adds one per bone)", a->name.c_str());
    }
  }
  const float x = (float)(in.mx - view.x), y = (float)(in.my - view.y);
  if (!wp_stroking_) {
    if (scene_hovered_ && in.key_pressed[platform::KEY_ESCAPE] && !ui_.wants_keyboard()) {
      set_weight_paint(false);  // Esc leaves the mode
      return true;
    }
    if (scene_hovered_ && in.pressed[0] && !in.alt() && !ui_.any_active() && wp_begin_stroke()) {
      wp_stroke_to(x, y, true);
      return true;
    }
    return false;
  }
  if (in.down[0]) wp_stroke_to(x, y, false);
  else wp_end_stroke();
  return true;
}

void Editor::wp_draw(const Recti &view) {
  if (!weight_paint_ || !scene_hovered_) return;
  auto &u = ui_;
  const float R = std::max(1.0f, wp_brush_.radius * u.scale);
  u.canvas.circle((float)u.in.mx, (float)u.in.my, R, Color::hex(0xFFFFFF, 200), 1.5f);
  u.canvas.circle((float)u.in.mx, (float)u.in.my, R * std::max(0.05f, std::min(1.0f, wp_brush_.strength)), Color::hex(0xFFFFFF, 90), 1.0f);
  (void)view;
}

/* The painted object's weight colours per render vertex, for the Scene view (rebuilt when the weights, the group or
 * the render mesh change). */
const std::vector<Vec3> *Editor::wp_colors(const Mesh &m, const RenderMesh &rm) {
  const int gi = wp_group(m);
  if (gi < 0 || m.vertex_groups[(size_t)gi].weights.size() != m.vert_count()) return nullptr;
  if (wp_colors_serial_ == rm.serial && wp_colors_version_ == wp_version_ && wp_colors_group_ == gi && wp_colors_.size() == rm.positions.size())
    return &wp_colors_;
  const std::vector<float> &w = m.vertex_groups[(size_t)gi].weights;
  wp_colors_.assign(rm.positions.size(), weight_color(0.0f));
  for (size_t k = 0; k < rm.indices.size() && k < rm.tri_corner.size(); k++) {
    const uint32_t c = rm.tri_corner[k];
    if (c < m.corner_verts.size() && m.corner_verts[c] < w.size()) wp_colors_[rm.indices[k]] = weight_color(w[m.corner_verts[c]]);
  }
  wp_colors_serial_ = rm.serial, wp_colors_version_ = wp_version_, wp_colors_group_ = gi;
  return &wp_colors_;
}

/* weightpaint on|off|toggle; wpbrush <add|subtract|draw|blur> [radius] [strength] [weight]; wpfalloff <name>;
 * wpmirror on|off; wpfront on|off; wpstroke x y x y ... (window pixels: one stroke); wpnormalize; wpclean [threshold]. */
void Editor::weight_paint_command(const std::vector<std::string> &t) {
  auto arg = [&](size_t i, const std::string &def) { return t.size() > i ? t[i] : def; };
  const std::string &c = t[0];
  if (c == "weightpaint") {
    const std::string v = to_lower(arg(1, "toggle"));
    set_weight_paint(v == "on" ? true : v == "off" ? false : !weight_paint_);
    return;
  }
  if (c == "wpbrush") {
    const std::string mode = to_lower(arg(1, ""));
    for (int i = 0; i < 4; i++)
      if (mode == to_lower(kWeightBrushModes[i])) wp_brush_.mode = i;
    if (t.size() > 2) wp_brush_.radius = std::max(1.0f, std::min((float)std::atof(t[2].c_str()), 1000.0f));
    if (t.size() > 3) wp_brush_.strength = std::max(0.0f, std::min((float)std::atof(t[3].c_str()), 1.0f));
    if (t.size() > 4) wp_brush_.weight = std::max(0.0f, std::min((float)std::atof(t[4].c_str()), 1.0f));
    return;
  }
  if (c == "wpfalloff") {
    for (int i = 0; i < 6; i++)
      if (to_lower(arg(1, "")) == to_lower(kWeightFalloffs[i])) wp_brush_.falloff = i;
    return;
  }
  if (c == "wpmirror" || c == "wpfront") {
    const bool on = to_lower(arg(1, "on")) == "on";
    (c == "wpmirror" ? wp_brush_.mirror_x : wp_brush_.front_only) = on;
    return;
  }
  if (c == "wpstroke") {
    if (!weight_paint_ || t.size() < 3 || !wp_begin_stroke()) {
      Log::warn("wpstroke: turn Weight Paint on and give screen points");
      return;
    }
    const Recti v = scene_rect_;
    for (size_t i = 1; i + 1 < t.size() && wp_stroking_; i += 2) {
      const float x = (float)std::atof(t[i].c_str()) - v.x, y = (float)std::atof(t[i + 1].c_str()) - v.y;
      wp_stroke_to(x, y, i == 1);
    }
    wp_end_stroke();
    return;
  }
  GameObject *g = weight_paint_ ? (wp_mesh() ? scene_->find(wp_obj_) : nullptr) : active_object();
  Mesh *mp = g && g->mesh_filter() && g->mesh_filter()->mesh ? mesh_make_mutable(g->mesh_filter()->mesh).get() : nullptr;
  if (!mp || mp->vertex_groups.empty()) {
    Log::warn("%s: a mesh with vertex groups first", c.c_str());
    return;
  }
  Mesh &m = *mp;
  for (VertexGroup &vg : m.vertex_groups) vg.weights.resize(m.vert_count(), 0.0f);
  const int active = weight_paint_ ? wp_group(m) : std::max(0, std::min(active_vgroup_, (int)m.vertex_groups.size() - 1));
  if (c == "wpnormalize") {
    /* Blender: Normalize All with Lock Active: the active group's weight stays, the others share what is left. */
    for (size_t v = 0; v < m.vert_count(); v++) {
      float others = 0.0f;
      for (size_t k = 0; k < m.vertex_groups.size(); k++)
        if ((int)k != active) others += m.vertex_groups[k].weights[v];
      if (others <= 0.0f) continue;
      const float room = std::max(0.0f, 1.0f - m.vertex_groups[(size_t)active].weights[v]);
      for (size_t k = 0; k < m.vertex_groups.size(); k++)
        if ((int)k != active) m.vertex_groups[k].weights[v] *= room / others;
    }
    mark_changed("Normalize All");
  }
  else if (c == "wpclean") {  // Blender: Clean (weights at or below the threshold to 0)
    const float th = std::max(0.0f, std::min((float)std::atof(arg(1, "0").c_str()), 1.0f));
    for (VertexGroup &vg : m.vertex_groups)
      for (float &x : vg.weights)
        if (x <= th) x = 0.0f;
    mark_changed("Clean Vertex Groups");
  }
  m.touch();
  wp_version_++;
}

}  // namespace bl

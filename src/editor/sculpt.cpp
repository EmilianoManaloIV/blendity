// SPDX-License-Identifier: GPL-2.0-or-later
// Sculpt Mode (task 0236, ADR 0074): brushes that touch only what is under them. A stroke builds a uniform grid over
// the mesh's vertices; a dab reads the object and depth under the cursor from the Scene view's planes, turns the
// screen radius into a world radius at that depth and walks the cells inside it; the vertices there move along their
// normals (Draw), toward their neighbours (Smooth) or with the drag (Grab). The mesh is written in place, positions
// only; the stroke's undo is a delta of the touched vertices. Blender: editors/sculpt_paint/sculpt.cc (the brush
// radius from the cursor's depth, SCULPT_vertex_neighbors, the draw / smooth / grab brushes); ZBrush: the circle on
// screen is the brush.
#include "editor.h"
#include "../core/core.h"

#include <algorithm>
#include <cmath>

namespace bl {

const char *const kSculptBrushes[3] = {"Draw", "Smooth", "Grab"};
const char *const kSculptPressure[3] = {"Off", "Strength", "Size"};

namespace {

constexpr float kDrawStep = 0.25f;     // (a full-strength Draw dab moves the centre this many radii)
constexpr float kSmoothStep = 0.5f;    // (a full-strength Smooth dab goes this far toward the neighbours' mean)
constexpr float kDabSpacing = 0.25f;   // (radii between dabs along a drag)
constexpr int kMaxDabsPerMove = 64;
constexpr float kSizeStep = 1.25f;     // (sculpt size + / -)

/* The proportional-editing curves (1 at the centre, 0 at the radius), Weight Paint's. */
float brush_falloff(int kind, float t) {
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

uint64_t cell_key(Vec3 p, float cell) {
  return meshops::grid_key(meshops::grid_cell(p.x / cell), meshops::grid_cell(p.y / cell), meshops::grid_cell(p.z / cell));
}

/* The neighbours of v along its faces' edges, each once. */
void vertex_ring(const Mesh &m, uint32_t v, std::vector<uint32_t> &out) {
  out.clear();
  const Mesh::VertexFaces &vf = m.vertex_faces();
  for (uint32_t k = vf.offsets[v]; k < vf.offsets[v + 1]; k++) {
    const size_t f = vf.faces[k];
    const uint32_t n = m.face_size(f), *fv = m.face_verts(f);
    for (uint32_t c = 0; c < n; c++) {
      if (fv[c] != v) continue;
      for (uint32_t nb : {fv[(c + n - 1) % n], fv[(c + 1) % n]})
        if (std::find(out.begin(), out.end(), nb) == out.end()) out.push_back(nb);
    }
  }
}

}  // namespace

/* ---- the mode ---- */

/* The sculpted mesh, only while it is still the one the mode started on (another scene, or an undo that swapped it,
 * ends the mode). */
Mesh *Editor::sculpt_mesh() {
  if (!sculpt_ || scene_.get() != sculpt_scene_) return nullptr;
  GameObject *g = scene_->find(sculpt_obj_);
  MeshFilter *mf = g ? g->mesh_filter() : nullptr;
  return mf && mf->mesh ? mf->mesh.get() : nullptr;
}

bool Editor::set_sculpt(bool on) {
  if (on && sculpt_ && active_ == sculpt_obj_ && scene_.get() == sculpt_scene_) return true;
  if (sculpt_) {
    sculpt_abort_stroke();
    sculpt_ = false;
    sculpt_obj_ = 0;
    sculpt_scene_ = nullptr;
    sculpt_cache_mesh_ = nullptr;
    if (!on) Log::info("Sculpt off");
  }
  if (!on) return true;
  GameObject *g = active_object();
  MeshFilter *mf = g ? g->mesh_filter() : nullptr;
  if (!mf || !mf->mesh) {
    Log::warn("Sculpt: select a mesh object first");
    return false;
  }
  if (playing_) {
    Log::warn("Sculpt: stop Play first");
    return false;
  }
  if (edit_mode_) exit_edit_mode();
  if (weight_paint_) set_weight_paint(false);
  if (sketch_.active) sketch_end();
  commit_undo();  // (a pending change is a step of its own, before the mesh is made the mode's)
  if (!sculpt_own_mesh(g, mf)) return false;
  sculpt_ = true;
  sculpt_obj_ = g->id;
  sculpt_scene_ = scene_.get();
  sculpt_cache_mesh_ = nullptr;
  Log::info("Sculpt on '%s': drag over it (Shift smooths, Alt carves; Esc leaves)", g->name.c_str());
  ui_.redraw = true;
  return true;
}

/* The sculpted mesh shared with no other live object (a duplicate, the Mesh field): when another holds it, the copy is
 * an ordinary undo step (commit_undo makes the stable snapshot share the copy). A mesh only the snapshots share is left
 * shared, on purpose (ADR 0074): a delta step writes it in place, so a snapshot restored above a stroke still holds the
 * stroke, and the delta steps below it take the positions back in order. */
bool Editor::sculpt_own_mesh(GameObject *g, MeshFilter *mf) {
  if (!g || !mf || !mf->mesh) return false;
  size_t holders = 0;
  scene_->for_each([&](GameObject &o) {
    const MeshFilter *f = o.mesh_filter();
    holders += f && f->mesh == mf->mesh ? 1 : 0;
  });
  if (holders <= 1) return true;
  mesh_make_mutable(mf->mesh);
  mark_changed("Sculpt: Own Mesh");
  commit_undo();  // (the step: the object given a mesh of its own)
  return true;
}

/* ---- the brush's place ---- */

/* The world point under the Scene view pixel (x, y) when the sculpted object is there: the depth plane's value turned
 * into a distance along the pixel's ray by bisection on the renderer's own projection (the depth grows with the
 * distance whatever the projection's form), forty halvings from a millimetre to ten kilometres. False over nothing or
 * another object. */
bool Editor::sculpt_hit(const Recti &view, float x, float y, Vec3 &world, float &dist) {
  const int ix = (int)x, iy = (int)y;
  if (ix < 0 || iy < 0 || ix >= view.w || iy >= view.h) return false;
  ensure_scene_planes();  // (task 0204: the GPU view keeps them on the card until a CPU reader asks)
  if (scene_rt_.id_at(ix, iy) != (uint32_t)sculpt_obj_) return false;
  const float z = scene_rt_.depth_at(ix, iy);
  const Ray ray = scene_r3d_.screen_ray(x, y);
  auto depth_at = [&](float t, float &out) {
    Vec2 s;
    return scene_r3d_.project(ray.origin + ray.dir * t, s, out);
  };
  float lo = 1e-3f, hi = 1e4f, zlo, zhi;
  if (!depth_at(lo, zlo) || !depth_at(hi, zhi)) return false;
  if ((z - zlo) * (z - zhi) > 0.0f) return false;  // (the depth isn't between the near and the far sample)
  const bool grows = zhi > zlo;
  for (int i = 0; i < 40; i++) {
    const float mid = 0.5f * (lo + hi);
    float zm;
    if (!depth_at(mid, zm)) return false;
    if ((zm < z) == grows) lo = mid;
    else hi = mid;
  }
  dist = 0.5f * (lo + hi);
  if (!(dist > 1e-4f) || !std::isfinite(dist)) return false;
  world = ray.origin + ray.dir * dist;
  return true;
}

/* The brush's radius in world units at a distance: the circle on screen is what the user set. */
float Editor::sculpt_world_radius(float px, float dist) const {
  const float h = (float)std::max(1, scene_rect_.h);
  if (cam_.ortho) return px * (2.0f * cam_.distance * std::tan(cam_.fov * 0.5f * kDeg2Rad)) / h;  // (the view's height in world units)
  return px * 2.0f * dist * std::tan(cam_.fov * 0.5f * kDeg2Rad) / h;
}

/* The grid: every vertex in the cell of its position (object space), built at a stroke's start. */
void Editor::sculpt_build_grid(const Mesh &m, float cell) {
  sculpt_grid_.cell = std::max(1e-6f, cell);
  sculpt_grid_.verts = m.vert_count();
  sculpt_grid_.cells.clear();
  sculpt_grid_.cells.reserve(m.vert_count() / 4 + 16);
  for (uint32_t v = 0; v < (uint32_t)m.vert_count(); v++) sculpt_grid_.cells[cell_key(m.positions[v], sculpt_grid_.cell)].push_back(v);
  sculpt_cache_mesh_ = &m;
}

/* One cell's vertices within the sphere (a vertex is filed where the stroke began; an id past the mesh is skipped). */
void Editor::sculpt_gather_cell(const Mesh &m, int64_t x, int64_t y, int64_t z, Vec3 centre, float r2, std::vector<uint32_t> &ids, std::vector<float> &d) const {
  auto it = sculpt_grid_.cells.find(meshops::grid_key(x, y, z));
  if (it == sculpt_grid_.cells.end()) return;
  for (uint32_t v : it->second) {
    if (v >= m.vert_count()) continue;
    const float dd = length_sq(m.positions[v] - centre);
    if (dd > r2) continue;
    ids.push_back(v), d.push_back(std::sqrt(dd));
  }
}

/* The vertices within `radius` of `centre` (object space) and their distances. The cells walked reach `reach` past the
 * radius: the farthest a vertex moved this stroke, since the grid files each where the stroke began. */
void Editor::sculpt_gather(const Mesh &m, Vec3 centre, float radius, std::vector<uint32_t> &ids, std::vector<float> &d) const {
  ids.clear(), d.clear();
  const float cell = sculpt_grid_.cell, reach = radius + sculpt_stroke_move_;
  const int64_t x0 = meshops::grid_cell((centre.x - reach) / cell), x1 = meshops::grid_cell((centre.x + reach) / cell);
  const int64_t y0 = meshops::grid_cell((centre.y - reach) / cell), y1 = meshops::grid_cell((centre.y + reach) / cell);
  const int64_t z0 = meshops::grid_cell((centre.z - reach) / cell), z1 = meshops::grid_cell((centre.z + reach) / cell);
  const float r2 = radius * radius;
  for (int64_t z = z0; z <= z1; z++)
    for (int64_t y = y0; y <= y1; y++)
      for (int64_t x = x0; x <= x1; x++) sculpt_gather_cell(m, x, y, z, centre, r2, ids, d);
}

/* A face's area-weighted normal as it is now (Newell; the faces wind counter-clockwise seen from outside). */
static Vec3 face_newell(const Mesh &m, size_t f) {
  const uint32_t fn = m.face_size(f), *fv = m.face_verts(f);
  Vec3 n(0.0f);
  for (uint32_t c = 0; c < fn; c++) {
    const Vec3 a = m.positions[fv[c]], b = m.positions[fv[(c + 1) % fn]];
    n += Vec3((a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y));
  }
  return n;
}

/* The vertex's normal from its faces as they are now. */
Vec3 Editor::sculpt_vertex_normal(const Mesh &m, uint32_t v) const {
  const Mesh::VertexFaces &vf = m.vertex_faces();
  Vec3 n(0.0f);
  for (uint32_t k = vf.offsets[v]; k < vf.offsets[v + 1]; k++) n += face_newell(m, vf.faces[k]);
  const float len = length(n);
  return len > 1e-20f ? n / len : Vec3(0.0f, 1.0f, 0.0f);
}


/* ---- the stroke ---- */

bool Editor::sculpt_begin_stroke(const Recti &view, float x, float y) {
  Mesh *m = sculpt_mesh();
  GameObject *g = m ? scene_->find(sculpt_obj_) : nullptr;
  if (!m || !g) return false;
  Vec3 hit;
  float dist;
  if (!sculpt_hit(view, x, y, hit, dist)) return false;
  commit_undo();  // (a pending change is a step before the stroke's)
  if (!sculpt_own_mesh(g, mf_of(g))) return false;  // (a duplicate made since shares the mesh: the object gets its own)
  m = sculpt_mesh();
  if (!m) return false;
  const float scale = std::max(1e-6f, length(g->world_matrix().dir({1.0f, 0.0f, 0.0f})));  // (the object's scale along X: a sculpted object is scaled evenly or near it)
  const float r_local = sculpt_world_radius(sculpt_brush_.radius * ui_.scale, dist) / scale;
  sculpt_build_grid(*m, r_local);  // (every stroke: the vertices are where the last stroke left them)
  sculpt_stroke_move_ = 0.0f;
  sculpt_touched_.assign(m->vert_count(), 0);
  sculpt_delta_ = std::make_shared<SculptDelta>();
  sculpt_delta_->obj = sculpt_obj_;
  sculpt_grab_ = SculptGrab{};
  sculpt_grab_.px0 = {x, y};
  sculpt_grab_.dist = dist;
  sculpt_stroke_mesh_ = m;
  sculpt_stroking_ = true;
  sculpt_dabbed_ = false;
  return true;
}

MeshFilter *Editor::mf_of(GameObject *g) { return g ? g->mesh_filter() : nullptr; }

/* The pen lifted: the stroke is one undo step - a delta of the touched vertices, no snapshot (ADR 0074). */
void Editor::sculpt_end_stroke() {
  if (!sculpt_stroking_) return;
  sculpt_stroking_ = false;
  Mesh *m = sculpt_mesh();
  if (!sculpt_dabbed_ || !m || m != sculpt_stroke_mesh_ || !sculpt_delta_) return;
  /* The step keeps the vertices that moved; a stroke that moved nothing makes none. */
  SculptDelta &d = *sculpt_delta_;
  size_t kept = 0;
  for (size_t i = 0; i < d.ids.size(); i++) {
    if (std::memcmp(&m->positions[d.ids[i]], &d.before[i], sizeof(Vec3)) == 0) continue;
    d.ids[kept] = d.ids[i], d.before[kept] = d.before[i];
    kept++;
  }
  d.ids.resize(kept), d.before.resize(kept);
  if (kept == 0) return;
  for (uint32_t v : d.ids) d.after.push_back(m->positions[v]);
  commit_undo();  // (a change made during the stroke - a key's - is its own step, before this one)
  UndoState s;
  s.delta = std::move(sculpt_delta_);
  s.label = std::string("Sculpt ") + kSculptBrushes[std::max(0, std::min(2, sculpt_brush_.kind))];
  s.bytes = s.delta->ids.size() * (sizeof(uint32_t) + 2 * sizeof(Vec3));
  undo_.push_back(std::move(s));
  trim_undo();
  redo_.clear();
  scene_dirty_ = true;
  settle_meshes();  // (ADR 0034: the tangents, now that the positions have settled)
  ui_.redraw = true;
}

/* A stroke cut off (an undo, a new scene, the mode left): what it moved goes back - written in place, there is no
 * snapshot to throw it away with - and no step is made. */
void Editor::sculpt_abort_stroke() {
  Mesh *m = sculpt_stroking_ ? sculpt_mesh() : nullptr;
  if (m && m == sculpt_stroke_mesh_ && sculpt_delta_) {
    const SculptDelta &d = *sculpt_delta_;
    for (size_t i = 0; i < d.ids.size(); i++)
      if (d.ids[i] < m->vert_count()) m->positions[d.ids[i]] = d.before[i];
    if (!d.ids.empty()) m->deform_version++;
  }
  sculpt_stroking_ = false;
  sculpt_stroke_mesh_ = nullptr;
  sculpt_delta_.reset();
}

/* A touched vertex's position before its first dab goes into the stroke's delta. */
void Editor::sculpt_record(const Mesh &m, const std::vector<uint32_t> &ids) {
  for (uint32_t v : ids) {
    if (sculpt_touched_[v]) continue;
    sculpt_touched_[v] = 1;
    sculpt_delta_->ids.push_back(v);
    sculpt_delta_->before.push_back(m.positions[v]);
  }
}

/* One dab at (x, y) in the Scene view's pixels with the pen's pressure. */
void Editor::sculpt_dab(const Recti &view, float x, float y, float pressure) {
  Mesh *mp = sculpt_mesh();
  GameObject *g = mp ? scene_->find(sculpt_obj_) : nullptr;
  if (!mp || !g || mp != sculpt_stroke_mesh_ || sculpt_touched_.size() != mp->vert_count()) {
    sculpt_abort_stroke();
    return;
  }
  Mesh &m = *mp;
  const bool shift = ui_.in.shift(), alt = ui_.in.alt();
  const int kind = shift ? 1 : std::max(0, std::min(2, sculpt_brush_.kind));
  pressure = std::isfinite(pressure) ? std::max(0.0f, std::min(1.0f, pressure)) : 1.0f;
  const float strength = std::max(0.0f, std::min(1.0f, sculpt_brush_.strength)) * (sculpt_brush_.pressure == 1 ? pressure : 1.0f);
  const float px_radius = std::max(1.0f, sculpt_brush_.radius * ui_.scale * (sculpt_brush_.pressure == 2 ? std::max(0.1f, pressure) : 1.0f));
  const Mat4 &W = g->world_matrix();
  const Mat4 inv = W.inverse();
  const float scale = std::max(1e-6f, length(W.dir({1.0f, 0.0f, 0.0f})));
  if (m.vert_count() != sculpt_grid_.verts) {  // (the mesh changed under the stroke: it ends)
    sculpt_abort_stroke();
    return;
  }
  std::vector<uint32_t> ids;
  std::vector<float> dist_local, w;
  float r_local = 0.0f;
  Vec3 hit_local;
  if (kind == 2 && !sculpt_grab_.ids.empty()) {  // (Grab: the set taken at the first dab)
    sculpt_grab(m, inv, sculpt_grab_.ids, sculpt_grab_.weights, {x, y});
    m.deform_version++;
    sculpt_dabbed_ = true;
    ui_.redraw = true;
    return;
  }
  Vec3 hit;
  float dist;
  if (!sculpt_hit(view, x, y, hit, dist)) return;  // (over nothing, or another object)
  hit_local = inv.point(hit);
  r_local = sculpt_world_radius(px_radius, dist) / scale;
  if (r_local > sculpt_grid_.cell * 4.0f || r_local < sculpt_grid_.cell * 0.25f) sculpt_build_grid(m, r_local);  // (a brush grown or shrunk far: the cells follow)
  sculpt_gather(m, hit_local, r_local, ids, dist_local);
  sculpt_last_gathered_ = ids.size();  // (tests: before Front Faces Only)
  if (sculpt_brush_.front_only) {  // (facing the camera)
    const Vec3 eye_local = inv.point(cam_.position()), fwd_local = inv.dir(cam_.forward());
    std::vector<uint32_t> kept;
    std::vector<float> kept_d;
    for (size_t i = 0; i < ids.size(); i++) {
      const Vec3 n = sculpt_vertex_normal(m, ids[i]);
      const Vec3 to_cam = cam_.ortho ? -fwd_local : eye_local - m.positions[ids[i]];
      if (!(dot(n, to_cam) > 0.0f)) continue;
      kept.push_back(ids[i]), kept_d.push_back(dist_local[i]);
    }
    ids.swap(kept), dist_local.swap(kept_d);
  }
  w.resize(ids.size());
  for (size_t i = 0; i < ids.size(); i++) w[i] = strength * brush_falloff(sculpt_brush_.falloff, dist_local[i] / std::max(1e-9f, r_local));
  sculpt_last_ids_ = ids, sculpt_last_hit_ = hit_local, sculpt_last_radius_ = r_local;
  if (ids.empty()) return;
  sculpt_record(m, ids);
  if (kind == 0) sculpt_draw_brush(m, ids, w, r_local, sculpt_brush_.carve != alt);
  else if (kind == 1) sculpt_smooth(m, ids, w);
  else {
    sculpt_grab_.ids = ids, sculpt_grab_.weights = w;
    sculpt_grab_.start.clear();
    for (uint32_t v : ids) sculpt_grab_.start.push_back(m.positions[v]);
    sculpt_grab_.px0 = {x, y};
    sculpt_grab_.dist = dist;
  }
  for (size_t i = 0; i < ids.size(); i++) sculpt_stroke_move_ = std::max(sculpt_stroke_move_, length(m.positions[ids[i]] - sculpt_delta_->before[sculpt_touched_index(ids[i])]));
  m.deform_version++;
  sculpt_dabbed_ = true;
  ui_.redraw = true;
}

/* The delta's slot of a touched vertex (every id in `ids` was recorded before the move). */
size_t Editor::sculpt_touched_index(uint32_t v) const {
  const auto at = std::find(sculpt_delta_->ids.begin(), sculpt_delta_->ids.end(), v);
  return (size_t)(at - sculpt_delta_->ids.begin());
}

/* Draw: along each vertex's normal (worked out before the move), a quarter radius at full strength; carving the other
 * way. */
void Editor::sculpt_draw_brush(Mesh &m, const std::vector<uint32_t> &ids, const std::vector<float> &w, float r_local, bool carve) {
  std::vector<Vec3> normals(ids.size());
  for (size_t i = 0; i < ids.size(); i++) normals[i] = sculpt_vertex_normal(m, ids[i]);
  const float sign = carve ? -1.0f : 1.0f;
  for (size_t i = 0; i < ids.size(); i++) m.positions[ids[i]] += normals[i] * (sign * w[i] * kDrawStep * r_local);
}

/* Smooth: a Laplacian step toward the neighbours' mean (Blender's smooth brush; it shrinks a convex region a little
 * - a spike's smoothing costs under a percent of a sphere's volume in the test). */
void Editor::sculpt_smooth(Mesh &m, const std::vector<uint32_t> &ids, const std::vector<float> &w) {
  std::vector<uint32_t> ring;
  std::vector<Vec3> target(ids.size());
  for (size_t i = 0; i < ids.size(); i++) {
    vertex_ring(m, ids[i], ring);
    Vec3 sum(0.0f);
    for (uint32_t nb : ring) sum += m.positions[nb];
    target[i] = ring.empty() ? m.positions[ids[i]] : sum / (float)ring.size();
  }
  for (size_t i = 0; i < ids.size(); i++) m.positions[ids[i]] += (target[i] - m.positions[ids[i]]) * (w[i] * kSmoothStep);
}

/* Grab: the set taken at the first dab moves with the pointer in the view plane, from where it started, each by its
 * weight. */
void Editor::sculpt_grab(Mesh &m, const Mat4 &inv, const std::vector<uint32_t> &ids, const std::vector<float> &w, Vec2 px) {
  const Vec2 d = px - sculpt_grab_.px0;
  const float per_px = sculpt_world_radius(1.0f, sculpt_grab_.dist);  // (world units a pixel at the grab's depth)
  const Vec3 move_world = cam_.right() * (d.x * per_px) - cam_.rotation().rotate({0, 1, 0}) * (d.y * per_px);
  const Vec3 move_local = inv.dir(move_world);
  for (size_t i = 0; i < ids.size() && i < sculpt_grab_.start.size(); i++) m.positions[ids[i]] = sculpt_grab_.start[i] + move_local * w[i];
  for (size_t i = 0; i < ids.size(); i++) sculpt_stroke_move_ = std::max(sculpt_stroke_move_, length(move_local * w[i]));
}

/* From the last dab to (x, y), a dab every quarter radius. */
void Editor::sculpt_stroke_to(const Recti &view, float x, float y, bool first) {
  const float pen = std::isfinite(ui_.in.pressure) ? ui_.in.pressure : 1.0f;
  if (first) {
    sculpt_dab(view, x, y, pen);
    sculpt_last_px_ = {x, y};
    return;
  }
  const float step = std::max(1.0f, sculpt_brush_.radius * ui_.scale * kDabSpacing);
  const Vec2 to(x, y), d = to - sculpt_last_px_;
  const float len = length(d);
  if (len < step) return;
  const int k = std::min(kMaxDabsPerMove, (int)(len / step));
  for (int i = 1; i <= k && sculpt_stroking_; i++) sculpt_dab(view, sculpt_last_px_.x + d.x * (i / (float)k), sculpt_last_px_.y + d.y * (i / (float)k), pen);
  sculpt_last_px_ = to;
}

/* The mode's frame: a press over the mesh begins a stroke, the drag dabs, the release ends it; Esc leaves. */
bool Editor::sculpt_update(const Recti &view) {
  if (!sculpt_) return false;
  auto &in = ui_.in;
  if (!sculpt_mesh()) {
    set_sculpt(false);
    return false;
  }
  const float x = (float)(in.mx - view.x), y = (float)(in.my - view.y);
  if (!sculpt_stroking_) {
    if (scene_hovered_ && in.key_pressed[platform::KEY_ESCAPE] && !ui_.wants_keyboard()) {
      set_sculpt(false);
      in.key_pressed[platform::KEY_ESCAPE] = false;
      return true;
    }
    if (!(scene_hovered_ && in.pressed[0] && !ui_.any_active() && !qm_.open)) return false;
    if (!sculpt_begin_stroke(view, x, y)) return false;  // (over nothing: the click is the view's)
    ui_.consume_click();
    sculpt_stroke_to(view, x, y, true);
    return true;
  }
  if (in.down[0]) sculpt_stroke_to(view, x, y, false);
  else sculpt_end_stroke();
  return true;
}

/* The brush: a circle of its screen radius and a smaller one for the strength. */
void Editor::sculpt_draw(const Recti &view) {
  if (!sculpt_ || !scene_hovered_) return;
  auto &u = ui_;
  const float R = std::max(1.0f, sculpt_brush_.radius * u.scale);
  u.canvas.circle((float)u.in.mx, (float)u.in.my, R, Color::hex(0xFFFFFF, 200), 1.5f);
  u.canvas.circle((float)u.in.mx, (float)u.in.my, R * std::max(0.05f, std::min(1.0f, sculpt_brush_.strength)), Color::hex(0xFFFFFF, 90), 1.0f);
  (void)view;
}

/* ---- undo ---- */

/* A delta step applied in place: the positions before (undo) or after (redo) into the object's mesh, which the stable
 * snapshot shares (ADR 0074). A mesh that no longer fits the step (replaced, renumbered) drops it with a log line. */
void Editor::apply_sculpt_delta(UndoState s, std::vector<UndoState> &into, const char *verb, bool undo) {
  GameObject *g = scene_->find(s.delta->obj);
  MeshFilter *mf = g ? g->mesh_filter() : nullptr;
  if (!mf || !mf->mesh) {
    Log::warn("%s %s: the sculpted object is gone", verb, s.label.c_str());
    return;
  }
  Mesh &m = *mf->mesh;
  for (uint32_t v : s.delta->ids)
    if (v >= m.vert_count()) {
      Log::warn("%s %s: the mesh changed since (a step that no longer fits)", verb, s.label.c_str());
      return;
    }
  const std::vector<Vec3> &src = undo ? s.delta->before : s.delta->after;
  for (size_t i = 0; i < s.delta->ids.size(); i++) m.positions[s.delta->ids[i]] = src[i];
  m.deform_version++;
  sculpt_cache_mesh_ = nullptr;  // (the grid is stale)
  settle_meshes();
  scene_dirty_ = true;
  Log::info("%s %s", verb, s.label.c_str());
  into.push_back(std::move(s));
  ui_.redraw = true;
}

/* ---- the Tools window and the console ---- */

/* draw_sculpt_tools: panels.cpp (the Inspector's reflector lives there). */

/* sculpt on|off | brush draw|smooth|grab | size <px>|+|- | strength <0-1> | falloff <name> | carve on|off|toggle |
 * front on|off | pressure off|strength|size | dab <x> <y> [pressure] | stroke <x0> <y0> <x1> <y1> [n] */
void Editor::sculpt_command(const std::vector<std::string> &t) {
  auto arg = [&](size_t i, const std::string &d) { return t.size() > i ? to_lower(t[i]) : d; };
  const std::string v = arg(1, "on");
  if (v == "on" || v == "off") {
    set_sculpt(v == "on");
    return;
  }
  if (v == "brush") {
    const std::string b = arg(2, "draw");
    sculpt_brush_.kind = b == "smooth" ? 1 : b == "grab" ? 2 : 0;
  }
  else if (v == "size") {
    const std::string s = arg(2, "50");
    if (s == "+") sculpt_brush_.radius = std::min(500.0f, sculpt_brush_.radius * kSizeStep);
    else if (s == "-") sculpt_brush_.radius = std::max(1.0f, sculpt_brush_.radius / kSizeStep);
    else sculpt_brush_.radius = std::max(1.0f, std::min(500.0f, (float)std::atof(s.c_str())));
  }
  else if (v == "strength") sculpt_brush_.strength = std::max(0.0f, std::min(1.0f, (float)std::atof(arg(2, "0.5").c_str())));
  else if (v == "falloff") {
    const std::string f = arg(2, "smooth");
    for (int i = 0; i < 6; i++)
      if (f == to_lower(kWeightFalloffs[i])) sculpt_brush_.falloff = i;
  }
  else if (v == "carve") sculpt_brush_.carve = arg(2, "on") == "toggle" ? !sculpt_brush_.carve : arg(2, "on") == "on";
  else if (v == "front") sculpt_brush_.front_only = arg(2, "on") == "on";
  else if (v == "pressure") {
    const std::string p = arg(2, "strength");
    sculpt_brush_.pressure = p == "off" ? 0 : p == "size" ? 2 : 1;
  }
  else if (v == "dab" && t.size() > 3) {  // (a stroke of one dab, in the Scene view's pixels)
    if (!sculpt_ && !set_sculpt(true)) return;
    const float x = (float)std::atof(t[2].c_str()), y = (float)std::atof(t[3].c_str()), p = t.size() > 4 ? (float)std::atof(t[4].c_str()) : 1.0f;
    if (!sculpt_begin_stroke(scene_rect_, x, y)) {
      Log::warn("sculpt dab: nothing of the sculpted object under (%g, %g)", x, y);
      return;
    }
    sculpt_dab(scene_rect_, x, y, p);
    sculpt_end_stroke();
  }
  else if (v == "stroke" && t.size() > 5) {  // (n dabs from one point to another)
    if (!sculpt_ && !set_sculpt(true)) return;
    const float x0 = (float)std::atof(t[2].c_str()), y0 = (float)std::atof(t[3].c_str()), x1 = (float)std::atof(t[4].c_str()), y1 = (float)std::atof(t[5].c_str());
    const int n = std::max(1, std::min(1000, t.size() > 6 ? std::atoi(t[6].c_str()) : 8));
    if (!sculpt_begin_stroke(scene_rect_, x0, y0)) {
      Log::warn("sculpt stroke: nothing of the sculpted object under (%g, %g)", x0, y0);
      return;
    }
    for (int i = 0; i <= n && sculpt_stroking_; i++) sculpt_dab(scene_rect_, x0 + (x1 - x0) * (i / (float)n), y0 + (y1 - y0) * (i / (float)n), 1.0f);
    sculpt_end_stroke();
  }
  else {
    Log::info("usage: sculpt on|off | brush draw|smooth|grab | size <px>|+|- | strength <0-1> | falloff smooth|sphere|root|sharp|linear|constant | carve on|off|toggle | front on|off | pressure off|strength|size | dab <x> <y> [pressure] | stroke <x0> <y0> <x1> <y1> [n]");
    return;
  }
  ui_.redraw = true;
}

}  // namespace bl

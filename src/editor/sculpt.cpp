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
#include "../image/image.h"

#include <algorithm>
#include <cmath>

namespace bl {

const char *const kSculptBrushes[10] = {"Draw", "Smooth", "Grab", "Clay Buildup", "Inflate", "Flatten", "Pinch", "Crease", "Scrape", "Snake Hook"};
const char *const kSculptPressure[3] = {"Off", "Strength", "Size"};
const char *const kSculptStrokes[5] = {"Dots", "Drag Dot", "Drag Rect", "Freehand", "Spray"};

namespace {

constexpr float kDrawStep = 0.25f;     // (a full-strength Draw dab moves the centre this many radii)
constexpr float kSmoothStep = 0.5f;    // (a full-strength Smooth dab goes this far toward the neighbours' mean)
constexpr float kDabSpacing = 0.25f;   // (radii between dabs along a drag)
constexpr int kMaxDabsPerMove = 64;
constexpr float kSizeStep = 1.25f;     // (sculpt size + / -)
constexpr float kPinchStep = 0.5f;     // (a full-strength Pinch dab takes a vertex this far toward the centre)
constexpr float kClayHeight = 0.25f;   // (Clay Buildup's layer: this many radii above the area's plane)
constexpr int kSprayDabs = 8;          // (Spray: dabs per step, scattered in the radius)
constexpr int kBrushKinds = 10, kStrokeKinds = 5;
enum { kDraw, kSmooth, kGrab, kClay, kInflate, kFlatten, kPinch, kCrease, kScrape, kSnake };
enum { kStrokeDots, kStrokeDragDot, kStrokeDragRect, kStrokeFreehand, kStrokeSpray };

float next_random(uint32_t &s) {  // (0..1; the stroke's own generator, the same on every platform)
  s = s * 1664525u + 1013904223u;
  return (float)(s >> 8) / 16777216.0f;
}

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
  sculpt_grab_sets_.clear();
  sculpt_snake_set_ = false;
  sculpt_dab_px_.clear();
  sculpt_prev_px_ = {x, y};
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
  s.label = std::string("Sculpt ") + kSculptBrushes[std::max(0, std::min(kBrushKinds - 1, sculpt_brush_.kind))];
  s.bytes = s.delta->ids.size() * (sizeof(uint32_t) + 2 * sizeof(Vec3));
  undo_.push_back(std::move(s));
  trim_undo();
  redo_.clear();
  scene_dirty_ = true;
  settle_meshes();  // (ADR 0034: the tangents, now that the positions have settled)
  ui_.redraw = true;
}

/* A stroke cut off (an undo, a new scene, the mode left): what it moved goes back and no step is made. */
void Editor::sculpt_abort_stroke() {
  if (sculpt_stroking_) sculpt_restore_delta();
  sculpt_stroking_ = false;
  sculpt_stroke_mesh_ = nullptr;
  sculpt_delta_.reset();
}

/* A touched vertex's position before its first dab goes into the stroke's delta; its slot (+1) is kept per vertex so a
 * later dab finds it at once. */
void Editor::sculpt_record(const Mesh &m, const std::vector<uint32_t> &ids) {
  for (uint32_t v : ids) {
    if (sculpt_touched_[v]) continue;
    sculpt_delta_->ids.push_back(v);
    sculpt_delta_->before.push_back(m.positions[v]);
    sculpt_touched_[v] = (uint32_t)sculpt_delta_->ids.size();  // (the slot + 1)
  }
}

/* What the stroke moved goes back - written in place, there is no snapshot to throw it away with (an abort, the Drag
 * Dot's re-placing). The mesh must still be the stroke's. */
void Editor::sculpt_restore_delta() {
  Mesh *m = sculpt_mesh();
  if (!m || m != sculpt_stroke_mesh_ || !sculpt_delta_) return;
  const SculptDelta &d = *sculpt_delta_;
  for (size_t i = 0; i < d.ids.size(); i++)
    if (d.ids[i] < m->vert_count()) m->positions[d.ids[i]] = d.before[i];
  if (!d.ids.empty()) m->deform_version++;
}

/* One dab at (x, y) in the Scene view's pixels with the pen's pressure: the instances symmetry asks for are gathered
 * and worked out against the positions before the dab, then their moves are added up (Blender's proxies), so a vertex
 * two instances share - on a mirror plane, at the radial axis - moves by the sum and the seam stays a seam. */
void Editor::sculpt_dab(const Recti &view, float x, float y, float pressure) {
  Mesh *mp = sculpt_mesh();
  GameObject *g = mp ? scene_->find(sculpt_obj_) : nullptr;
  if (!mp || !g || mp != sculpt_stroke_mesh_ || sculpt_touched_.size() != mp->vert_count()) {
    sculpt_abort_stroke();
    return;
  }
  Mesh &m = *mp;
  const bool shift = ui_.in.shift(), alt = ui_.in.alt();
  const int kind = shift ? kSmooth : std::max(0, std::min(kBrushKinds - 1, sculpt_brush_.kind));
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
  const Vec2 px(x, y), drag_px = px - sculpt_prev_px_;  // (the stroke's direction here: the alpha turns to it, Snake Hook pulls along it)
  sculpt_dab_px_.push_back(px);
  sculpt_prev_px_ = px;
  if (kind == kGrab && !sculpt_grab_sets_.empty()) {  // (Grab: the sets taken at the first dab follow the pointer)
    sculpt_grab_follow(m, inv, px);
    return;
  }
  Vec3 hit_local;
  float dist = 0.0f;
  if (kind == kSnake && sculpt_snake_set_) {  // (Snake Hook: the stroke's place moves with the pointer at the stroke's depth, past the silhouette too)
    sculpt_snake_pos_ += sculpt_pointer_move_local(inv, drag_px, sculpt_grab_.dist);
    hit_local = sculpt_snake_pos_, dist = sculpt_grab_.dist;
  }
  else {
    Vec3 hit;
    if (!sculpt_hit(view, x, y, hit, dist)) return;  // (over nothing, or another object)
    hit_local = inv.point(hit);
    if (kind == kSnake) sculpt_snake_pos_ = hit_local, sculpt_snake_set_ = true, sculpt_grab_.dist = dist;
  }
  const float r_local = sculpt_world_radius(px_radius, dist) / scale;
  if (r_local > sculpt_grid_.cell * 4.0f || r_local < sculpt_grid_.cell * 0.25f) sculpt_build_grid(m, r_local);  // (a brush grown or shrunk far: the cells follow)
  if (kind == kGrab) sculpt_grab_sets_.clear(), sculpt_grab_.px0 = px, sculpt_grab_.dist = dist;
  /* Every instance gathered against the same positions, then its moves summed. */
  struct Instance {
    SculptSym sym;
    std::vector<uint32_t> ids;
    std::vector<float> w;
  };
  std::vector<Instance> instances;
  for (const SculptSym &sym : sculpt_symmetries()) {
    Instance in{sym, {}, {}};
    const size_t gathered = sculpt_weights(m, inv, sym, sym.apply(hit_local), r_local, strength, drag_px, in.ids, in.w);
    if (sym.identity()) sculpt_last_ids_ = in.ids, sculpt_last_hit_ = hit_local, sculpt_last_radius_ = r_local, sculpt_last_gathered_ = gathered;
    if (!in.ids.empty()) instances.push_back(std::move(in));
  }
  if (instances.empty()) return;
  std::unordered_map<uint32_t, Vec3> moves;
  for (const Instance &in : instances) {
    sculpt_record(m, in.ids);
    if (kind == kGrab) {  // (the set is taken now and follows the pointer from here)
      SculptGrabSet set;
      set.sym = in.sym, set.ids = in.ids, set.weights = in.w;
      for (uint32_t v : in.ids) set.start.push_back(m.positions[v]);
      sculpt_grab_sets_.push_back(std::move(set));
      continue;
    }
    std::vector<Vec3> was;
    for (uint32_t v : in.ids) was.push_back(m.positions[v]);
    sculpt_apply_brush(m, inv, in.sym, in.ids, in.w, r_local, kind, sculpt_brush_.carve != alt, drag_px);
    for (size_t i = 0; i < in.ids.size(); i++) {
      moves[in.ids[i]] += m.positions[in.ids[i]] - was[i];
      m.positions[in.ids[i]] = was[i];  // (the next instance works against the same positions)
    }
  }
  for (const auto &mv : moves) {
    m.positions[mv.first] += mv.second;
    sculpt_stroke_move_ = std::max(sculpt_stroke_move_, length(m.positions[mv.first] - sculpt_delta_->before[sculpt_touched_index(mv.first)]));
  }
  m.deform_version++;
  sculpt_dabbed_ = true;
  ui_.redraw = true;
}

/* The pointer's move as a move in the object's space at a depth (the camera's right and up there). */
Vec3 Editor::sculpt_pointer_move_local(const Mat4 &inv, Vec2 d, float dist) const {
  const float per_px = sculpt_world_radius(1.0f, dist);  // (world units a pixel at that depth)
  return inv.dir(cam_.right() * (d.x * per_px) - cam_.rotation().rotate({0, 1, 0}) * (d.y * per_px));
}

/* Grab's sets follow the pointer from where they started, each by its mirrored pull; a vertex in two sets moves by
 * the sum. */
void Editor::sculpt_grab_follow(Mesh &m, const Mat4 &inv, Vec2 px) {
  const Vec3 move = sculpt_pointer_move_local(inv, px - sculpt_grab_.px0, sculpt_grab_.dist);
  std::unordered_map<uint32_t, std::pair<Vec3, Vec3>> at;  // (vertex: its start, the sum of its pulls)
  for (const SculptGrabSet &set : sculpt_grab_sets_) {
    const Vec3 pull = set.sym.dir(move);
    for (size_t i = 0; i < set.ids.size(); i++) {
      auto &e = at[set.ids[i]];
      e.first = set.start[i], e.second += pull * set.weights[i];
    }
  }
  for (const auto &kv : at) {
    m.positions[kv.first] = kv.second.first + kv.second.second;
    sculpt_stroke_move_ = std::max(sculpt_stroke_move_, length(kv.second.second));
  }
  m.deform_version++;
  sculpt_dabbed_ = true;
  ui_.redraw = true;
}

/* ---- symmetry ---- */

Vec3 Editor::SculptSym::apply(Vec3 p) const {
  const Vec3 q(p.x * sign.x, p.y * sign.y, p.z * sign.z);
  if (angle == 0.0f) return q;
  const float c = std::cos(angle), s = std::sin(angle);
  return {q.x * c + q.z * s, q.y, -q.x * s + q.z * c};
}

Vec3 Editor::SculptSym::dir(Vec3 d) const { return apply(d); }  // (a linear map: a direction follows the same way)

bool Editor::SculptSym::identity() const { return sign.x > 0.0f && sign.y > 0.0f && sign.z > 0.0f && angle == 0.0f; }

/* The dab's instances: itself, then its mirrors across the object's chosen planes (every combination, a bit mask over
 * X, Y, Z), each turned about the Y axis `radial` times. Mirror X with Mirror Z is a half turn: with an even Radial the
 * same place is dabbed twice (Blender does the same). */
std::vector<Editor::SculptSym> Editor::sculpt_symmetries() const {
  std::vector<SculptSym> out;
  const int n = std::max(1, std::min(64, sculpt_brush_.radial));
  const int allowed = (sculpt_brush_.sym_x ? 1 : 0) | (sculpt_brush_.sym_y ? 2 : 0) | (sculpt_brush_.sym_z ? 4 : 0);
  for (int mask = 0; mask < 8; mask++) {
    if (mask & ~allowed) continue;
    for (int k = 0; k < n; k++) {
      SculptSym s;
      s.sign = {mask & 1 ? -1.0f : 1.0f, mask & 2 ? -1.0f : 1.0f, mask & 4 ? -1.0f : 1.0f};
      s.angle = 2.0f * kPi * (float)k / (float)n;
      out.push_back(s);
    }
  }
  return out;
}

/* One instance's brush on its vertices (Grab is handled by the dab: it takes a set instead). */
void Editor::sculpt_apply_brush(Mesh &m, const Mat4 &inv, const SculptSym &sym, const std::vector<uint32_t> &ids, const std::vector<float> &w, float r_local, int kind, bool carve, Vec2 drag_px) {
  switch (kind) {
    case kDraw: sculpt_draw_brush(m, ids, w, r_local, carve); break;
    case kSmooth: sculpt_smooth(m, ids, w); break;
    case kClay: sculpt_clay(m, ids, w, r_local, carve); break;
    case kInflate: sculpt_inflate(m, ids, w, r_local, carve); break;
    case kFlatten: sculpt_flatten(m, ids, w, false); break;
    case kScrape: sculpt_flatten(m, ids, w, true); break;
    case kPinch: sculpt_pinch(m, ids, w, r_local, false); break;
    case kCrease: sculpt_pinch(m, ids, w, r_local, true); break;
    case kSnake: sculpt_snake(m, inv, sym, ids, w, drag_px); break;
    default: break;
  }
}

/* The vertices an instance touches and how much: the falloff by distance, the alpha's value where the vertex lies in
 * the brush's disc (the camera's right and up in the object's space, mirrored with the instance, turned so the image's
 * right runs along the stroke unless `alpha noturn`), Front Faces Only against the camera mirrored with the instance
 * (Blender flips the view normal per symmetry pass). Returns how many were within the radius before that filter. */
size_t Editor::sculpt_weights(const Mesh &m, const Mat4 &inv, const SculptSym &sym, Vec3 hit_local, float r_local, float strength, Vec2 drag_px, std::vector<uint32_t> &ids,
                              std::vector<float> &w) const {
  std::vector<float> dist_local;
  sculpt_gather(m, hit_local, r_local, ids, dist_local);
  const size_t gathered = ids.size();
  const Vec3 eye_local = sym.apply(inv.point(cam_.position())), fwd_local = sym.dir(inv.dir(cam_.forward()));
  Vec3 right = normalize(sym.dir(inv.dir(cam_.right()))), up = normalize(sym.dir(inv.dir(cam_.rotation().rotate({0, 1, 0}))));
  if (!sculpt_alpha_.rgba8.empty() && sculpt_brush_.alpha_turn && length(drag_px) > 0.5f) {  // (the image's right along the stroke)
    const Vec2 d = drag_px / length(drag_px);
    const Vec3 r2 = right * d.x - up * d.y, u2 = right * d.y + up * d.x;
    right = r2, up = u2;
  }
  std::vector<uint32_t> kept;
  std::vector<float> kw;
  for (size_t i = 0; i < ids.size(); i++) {
    const Vec3 p = m.positions[ids[i]];
    if (sculpt_brush_.front_only) {
      const Vec3 n = sculpt_vertex_normal(m, ids[i]);
      const Vec3 to_cam = cam_.ortho ? -fwd_local : eye_local - p;
      if (!(dot(n, to_cam) > 0.0f)) continue;
    }
    float weight = strength * brush_falloff(sculpt_brush_.falloff, dist_local[i] / std::max(1e-9f, r_local));
    if (!sculpt_alpha_.rgba8.empty()) weight *= sculpt_alpha_at({dot(p - hit_local, right) / r_local, dot(p - hit_local, up) / r_local});
    kept.push_back(ids[i]), kw.push_back(weight);
  }
  ids.swap(kept), w.swap(kw);
  return gathered;
}

/* The alpha's value at (u, v) in -1..1 across the brush's disc: the image's luminance, 0 outside it. */
float Editor::sculpt_alpha_at(Vec2 uv) const {
  const Bitmap &a = sculpt_alpha_;
  if (a.width <= 0 || a.height <= 0) return 1.0f;
  if (uv.x < -1.0f || uv.x > 1.0f || uv.y < -1.0f || uv.y > 1.0f) return 0.0f;
  const int x = std::max(0, std::min(a.width - 1, (int)((uv.x * 0.5f + 0.5f) * a.width))), y = std::max(0, std::min(a.height - 1, (int)((0.5f - uv.y * 0.5f) * a.height)));
  const uint8_t *px = &a.rgba8[((size_t)y * a.width + x) * 4];
  return (px[0] + px[1] + px[2]) / (3.0f * 255.0f);
}

/* An alpha from an image file (its luminance: white moves, black doesn't); an empty path clears it. */
bool Editor::sculpt_load_alpha(const std::string &path) {
  sculpt_brush_.alpha.clear();
  sculpt_alpha_ = Bitmap{};
  if (path.empty()) return true;
  std::string err;
  Bitmap b;
  const std::string full = fs::exists(path) ? path : fs::join(assets_dir_, path);
  if (!load_image(full, b, err)) {
    Log::warn("sculpt alpha: %s (%s)", err.empty() ? "no image there" : err.c_str(), full.c_str());
    return false;
  }
  if (b.rgba8.empty() && !b.rgbaf.empty()) {  // (an HDR or EXR: its linear floats as bytes)
    b.rgba8.resize(b.rgbaf.size());
    for (size_t i = 0; i < b.rgbaf.size(); i++) b.rgba8[i] = (uint8_t)std::lround(std::max(0.0f, std::min(1.0f, b.rgbaf[i])) * 255.0f);
  }
  if (b.rgba8.empty()) {
    Log::warn("sculpt alpha: the image has no pixels (%s)", full.c_str());
    return false;
  }
  sculpt_alpha_ = std::move(b);
  sculpt_brush_.alpha = path;
  Log::info("Sculpt: the alpha is %s (%d x %d)", path.c_str(), sculpt_alpha_.width, sculpt_alpha_.height);
  return true;
}

/* The brush area's centre and mean normal, every vertex under the brush alike (Blender's area plane: Clay Buildup,
 * Flatten and Scrape work against it). */
void Editor::sculpt_area(const Mesh &m, const std::vector<uint32_t> &ids, Vec3 &centre, Vec3 &normal) const {
  Vec3 c(0.0f), n(0.0f);
  float wsum = 0.0f;
  for (uint32_t v : ids) c += m.positions[v], n += sculpt_vertex_normal(m, v), wsum += 1.0f;
  centre = wsum > 0.0f ? c / wsum : c;
  const float len = length(n);
  normal = len > 1e-12f ? n / len : Vec3(0.0f, 1.0f, 0.0f);
}

/* Clay Buildup: toward a plane a quarter radius above the area's plane (below it when carving) - layers that stay
 * flat rather than a bump that keeps rising. */
void Editor::sculpt_clay(Mesh &m, const std::vector<uint32_t> &ids, const std::vector<float> &w, float r_local, bool carve) {
  Vec3 c, n;
  sculpt_area(m, ids, c, n);
  const float sign = carve ? -1.0f : 1.0f, height = kClayHeight * r_local * sign;
  for (size_t i = 0; i < ids.size(); i++) {
    const float h = dot(m.positions[ids[i]] - c, n);  // (above the plane)
    const float want = height - h;                    // (the way to the layer's top)
    if (want * sign <= 0.0f) continue;                // (already there or past it)
    m.positions[ids[i]] += n * (want * std::min(1.0f, w[i]));
  }
}

/* Inflate: each vertex along its own normal (Draw along the area's; a thin part swells both ways). */
void Editor::sculpt_inflate(Mesh &m, const std::vector<uint32_t> &ids, const std::vector<float> &w, float r_local, bool carve) {
  std::vector<Vec3> normals(ids.size());
  for (size_t i = 0; i < ids.size(); i++) normals[i] = sculpt_vertex_normal(m, ids[i]);
  const float sign = carve ? -1.0f : 1.0f;
  for (size_t i = 0; i < ids.size(); i++) m.positions[ids[i]] += normals[i] * (sign * w[i] * kDrawStep * r_local);
}

/* Flatten: toward the area's plane from both sides; Scrape (Trim Dynamic): only what stands above it comes down. */
void Editor::sculpt_flatten(Mesh &m, const std::vector<uint32_t> &ids, const std::vector<float> &w, bool scrape) {
  Vec3 c, n;
  sculpt_area(m, ids, c, n);
  for (size_t i = 0; i < ids.size(); i++) {
    const float h = dot(m.positions[ids[i]] - c, n);
    if (scrape && h <= 0.0f) continue;
    m.positions[ids[i]] -= n * (h * std::min(1.0f, w[i]));
  }
}

/* Pinch: toward the centre, in the area's plane; Crease (Dam Standard): a pinch that also pushes in, cutting a line. */
void Editor::sculpt_pinch(Mesh &m, const std::vector<uint32_t> &ids, const std::vector<float> &w, float r_local, bool crease) {
  Vec3 c, n;
  sculpt_area(m, ids, c, n);
  for (size_t i = 0; i < ids.size(); i++) {
    Vec3 to = c - m.positions[ids[i]];
    to -= n * dot(to, n);  // (in the plane)
    m.positions[ids[i]] += to * (w[i] * kPinchStep);
    if (crease) m.positions[ids[i]] -= n * (w[i] * kDrawStep * 0.5f * r_local);
  }
}

/* Snake Hook: the vertices under the stroke's place pulled along the pointer's move since the last dab (the place itself
 * moves with the pointer at the stroke's depth, so a strand follows the drag out past the silhouette). */
void Editor::sculpt_snake(Mesh &m, const Mat4 &inv, const SculptSym &sym, const std::vector<uint32_t> &ids, const std::vector<float> &w, Vec2 drag_px) {
  const Vec3 move_local = sym.dir(sculpt_pointer_move_local(inv, drag_px, sculpt_grab_.dist));
  for (size_t i = 0; i < ids.size(); i++) m.positions[ids[i]] += move_local * w[i];
}

/* The delta's slot of a touched vertex (every id a brush moved was recorded first). */
size_t Editor::sculpt_touched_index(uint32_t v) const { return (size_t)sculpt_touched_[v] - 1; }

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


/* From the last dab to (x, y) by the stroke type: Dots and Freehand dab along the way (every quarter radius, or at every
 * move), Spray scatters dabs in the radius, Drag Dot moves one dab with the pointer, Drag Rect waits for the release.
 * The lazy mouse drags the brush on a string: the pointer must get `lazy` px away before the brush follows. */
void Editor::sculpt_stroke_to(const Recti &view, float x, float y, bool first) {
  const float pen = std::isfinite(ui_.in.pressure) ? ui_.in.pressure : 1.0f;
  Vec2 to(x, y);
  if (first) sculpt_lazy_px_ = to, sculpt_press_px_ = to, sculpt_prev_px_ = to;
  if (sculpt_brush_.lazy > 0.0f && !first) {
    const Vec2 d = to - sculpt_lazy_px_;
    const float l = length(d);
    if (l > sculpt_brush_.lazy) sculpt_lazy_px_ += d * (1.0f - sculpt_brush_.lazy / l);
    to = sculpt_lazy_px_;
  }
  const int type = std::max(0, std::min(kStrokeKinds - 1, sculpt_brush_.stroke));
  if (type == kStrokeDragRect) {  // (one stamp at the release: nothing yet)
    sculpt_last_px_ = to;
    return;
  }
  if (type == kStrokeDragDot) {  // (one dab that follows the pointer: the last one taken back, put down again)
    if (!first && sculpt_delta_ && length(to - sculpt_last_px_) >= 1.0f) {
      sculpt_restore_delta();
      sculpt_delta_->ids.clear(), sculpt_delta_->before.clear();
      std::fill(sculpt_touched_.begin(), sculpt_touched_.end(), 0u);
      sculpt_grab_sets_.clear();
      sculpt_dab(view, to.x, to.y, pen);
    }
    else if (first) sculpt_dab(view, to.x, to.y, pen);
    sculpt_last_px_ = to;
    return;
  }
  if (first) {
    if (type == kStrokeSpray) sculpt_spray(view, to.x, to.y, pen);
    else sculpt_dab(view, to.x, to.y, pen);
    sculpt_last_px_ = to;
    return;
  }
  const float step = type == kStrokeFreehand ? 1.0f : std::max(1.0f, sculpt_brush_.radius * ui_.scale * kDabSpacing);
  const Vec2 d = to - sculpt_last_px_;
  const float len = length(d);
  if (len < step) return;
  const int k = type == kStrokeFreehand ? 1 : std::min(kMaxDabsPerMove, std::max(1, (int)(len / step)));  // (Freehand: one dab a move)
  for (int i = 1; i <= k && sculpt_stroking_; i++) {
    const Vec2 at = sculpt_last_px_ + d * (i / (float)k);
    if (type == kStrokeSpray) sculpt_spray(view, at.x, at.y, pen);
    else sculpt_dab(view, at.x, at.y, pen);
  }
  sculpt_last_px_ = to;
}

/* Spray: dabs scattered in the brush's circle, each a random fraction of the size and strength (Grab and Snake Hook
 * would read the scatter as a drag: they dab once). */
void Editor::sculpt_spray(const Recti &view, float x, float y, float pressure) {
  if (sculpt_brush_.kind == kGrab || sculpt_brush_.kind == kSnake) {
    sculpt_dab(view, x, y, pressure);
    return;
  }
  const SculptBrush saved = sculpt_brush_;
  const float R = saved.radius * ui_.scale;
  for (int i = 0; i < kSprayDabs && sculpt_stroking_; i++) {
    const float a = next_random(sculpt_rng_) * 2.0f * kPi, r = std::sqrt(next_random(sculpt_rng_)) * R;
    sculpt_brush_.radius = saved.radius * (0.25f + 0.5f * next_random(sculpt_rng_));
    sculpt_brush_.strength = saved.strength * (0.3f + 0.7f * next_random(sculpt_rng_));
    sculpt_dab(view, x + std::cos(a) * r, y + std::sin(a) * r, pressure);
  }
  sculpt_brush_.radius = saved.radius, sculpt_brush_.strength = saved.strength;
}

/* Drag Rect's stamp at the release: one dab at the press point, as wide as the drag and turned to it. */
void Editor::sculpt_stroke_end_shape(const Recti &view) {
  if (std::max(0, std::min(kStrokeKinds - 1, sculpt_brush_.stroke)) != kStrokeDragRect || !sculpt_stroking_) return;
  const Vec2 d = sculpt_last_px_ - sculpt_press_px_;
  const float len = length(d);
  if (len < 2.0f) return;
  const SculptBrush saved = sculpt_brush_;
  sculpt_brush_.radius = len / std::max(0.1f, ui_.scale);
  sculpt_prev_px_ = sculpt_press_px_ - d;  // (so the drag's direction turns the alpha)
  sculpt_dab(view, sculpt_press_px_.x, sculpt_press_px_.y, 1.0f);
  sculpt_brush_ = saved;
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
  else {
    sculpt_stroke_end_shape(view);
    sculpt_end_stroke();
  }
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
    std::string b;
    for (size_t i = 2; i < t.size(); i++) b += (i > 2 ? " " : "") + to_lower(t[i]);
    int kind = -1;
    for (int i = 0; i < kBrushKinds; i++)
      if (b == to_lower(kSculptBrushes[i]) || (b.size() > 2 && to_lower(kSculptBrushes[i]).rfind(b, 0) == 0)) kind = kind < 0 ? i : kind;
    if (b == "clay") kind = kClay;
    if (b == "snake" || b == "hook") kind = kSnake;
    if (kind < 0) {
      Log::info("usage: sculpt brush draw|smooth|grab|clay|inflate|flatten|pinch|crease|scrape|snake");
      return;
    }
    sculpt_brush_.kind = kind;
  }
  else if (v == "symmetry") {  // sculpt symmetry x|y|z|off | radial <n>  (task 0237)
    const std::string s = arg(2, "x");
    if (s == "off") sculpt_brush_.sym_x = sculpt_brush_.sym_y = sculpt_brush_.sym_z = false, sculpt_brush_.radial = 1;
    else if (s == "radial") sculpt_brush_.radial = std::max(1, std::min(64, std::atoi(arg(3, "6").c_str())));
    else if (s == "x") sculpt_brush_.sym_x = arg(3, "on") == "toggle" ? !sculpt_brush_.sym_x : arg(3, "on") != "off";
    else if (s == "y") sculpt_brush_.sym_y = arg(3, "on") == "toggle" ? !sculpt_brush_.sym_y : arg(3, "on") != "off";
    else if (s == "z") sculpt_brush_.sym_z = arg(3, "on") == "toggle" ? !sculpt_brush_.sym_z : arg(3, "on") != "off";
    else Log::info("usage: sculpt symmetry x|y|z [on|off] | radial <n> | off");
  }
  else if (v == "lazy") sculpt_brush_.lazy = std::max(0.0f, std::min(300.0f, (float)std::atof(arg(2, "0").c_str())));
  else if (v == "alpha") {
    std::string path;
    for (size_t i = 2; i < t.size(); i++) path += (i > 2 ? " " : "") + t[i];
    if (path.empty()) {
      Log::info("usage: sculpt alpha <image>|off|turn|noturn (now: %s)", sculpt_brush_.alpha.empty() ? "none" : sculpt_brush_.alpha.c_str());
      return;
    }
    if (to_lower(path) == "turn") sculpt_brush_.alpha_turn = true;
    else if (to_lower(path) == "noturn") sculpt_brush_.alpha_turn = false;
    else if (!sculpt_load_alpha(to_lower(path) == "off" ? "" : path)) return;
  }
  else if (v == "stroke" && t.size() == 3) {  // sculpt stroke dots|dot|rect|freehand|spray
    const std::string s = arg(2, "dots");
    const int type = s == "dots" ? kStrokeDots : s == "dot" ? kStrokeDragDot : s == "rect" ? kStrokeDragRect : s == "freehand" ? kStrokeFreehand : s == "spray" ? kStrokeSpray : -1;
    if (type < 0) {
      Log::info("usage: sculpt stroke dots|dot|rect|freehand|spray");
      return;
    }
    sculpt_brush_.stroke = type;
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
    Log::info("usage: sculpt on|off | brush draw|smooth|grab|clay|inflate|flatten|pinch|crease|scrape|snake | size <px>|+|- | strength <0-1> | falloff <name> | carve on|off|toggle | front on|off | pressure off|strength|size | symmetry x|y|z|radial <n>|off | lazy <px> | alpha <image>|off|turn|noturn | stroke dots|dot|rect|freehand|spray | dab <x> <y> [pressure] | stroke <x0> <y0> <x1> <y1> [n]");
    return;
  }
  ui_.redraw = true;
}

}  // namespace bl

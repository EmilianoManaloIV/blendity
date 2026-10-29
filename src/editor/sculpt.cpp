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
#include "../scene/uv.h"
#include "../scene/dynamesh.h"

#include <algorithm>
#include <cmath>

namespace bl {

const char *const kSculptBrushes[11] = {"Draw", "Smooth", "Grab", "Clay Buildup", "Inflate", "Flatten", "Pinch", "Crease", "Scrape", "Snake Hook", "Mask"};
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
constexpr int kBrushKinds = 11, kStrokeKinds = 5;
/* Task 0238: a vertex counts as masked above this; the mask's darkening in the view; a Ctrl click (not a drag) in px. */
constexpr float kMaskedAbove = 0.5f, kMaskDarkening = 0.6f, kClickPx = 4.0f;
const Vec3 kMaskDarkColor(0.12f);
enum { kDraw, kSmooth, kGrab, kClay, kInflate, kFlatten, kPinch, kCrease, kScrape, kSnake, kMask };
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

/* Every vertex of the face masked above a half (an empty face is not). */
bool face_all_masked(const Mesh &m, size_t f) {
  if (!m.mask_fits()) return false;
  const uint32_t n = m.face_size(f), *fv = m.face_verts(f);
  if (n == 0) return false;
  for (uint32_t c = 0; c < n; c++)
    if (!(m.sculpt_mask[fv[c]] > kMaskedAbove)) return false;
  return true;
}

/* Any vertex masked at all (an all-zero mask is no mask: nothing to draw or to protect). */
bool mask_any(const Mesh &m) {
  return m.mask_fits() && std::any_of(m.sculpt_mask.begin(), m.sculpt_mask.end(), [](float v) { return v > 0.0f; });
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
  if (paint_) set_paint(false);  // (task 0245: one mode at a time)
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
    if (v < sculpt_hidden_vert_.size() && sculpt_hidden_vert_[v]) continue;  // (every face of it hidden: task 0238)
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
  sculpt_cage_ = sculpt_subdiv_levels() > 0;  // (decided once a stroke: no evaluation per dab)
  if (sculpt_cage_) sculpt_cage_hit(x, y, hit, dist);
  commit_undo();  // (a pending change is a step before the stroke's)
  if (!sculpt_own_mesh(g, mf_of(g))) return false;  // (a duplicate made since shares the mesh: the object gets its own)
  m = sculpt_mesh();
  if (!m) return false;
  const float scale = std::max(1e-6f, length(g->world_matrix().dir({1.0f, 0.0f, 0.0f})));  // (the object's scale along X: a sculpted object is scaled evenly or near it)
  const float r_local = sculpt_world_radius(sculpt_brush_.radius * ui_.scale, dist) / scale;
  sculpt_build_grid(*m, r_local);  // (every stroke: the vertices are where the last stroke left them)
  sculpt_build_hidden(*m);
  sculpt_stroke_move_ = 0.0f;
  sculpt_touched_.assign(m->vert_count(), 0);
  sculpt_delta_ = std::make_shared<SculptDelta>();
  sculpt_delta_->obj = sculpt_obj_;
  /* Ctrl, or the Mask brush: a mask stroke (Alt takes the mask away, Shift with the Mask brush blurs it). */
  const bool masking = ui_.in.ctrl() || sculpt_brush_.kind == kMask;
  sculpt_mask_mode_ = !masking ? kMaskNone : ui_.in.alt() ? kMaskTake : ui_.in.shift() && !ui_.in.ctrl() ? kMaskBlur : kMaskAdd;
  sculpt_delta_->mask = masking;
  if (masking && !m->mask_fits()) m->sculpt_mask.assign(m->vert_count(), 0.0f), sculpt_mask_version_++;  // (in place, the snapshots' too: all zero is no mask)
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
  sculpt_dyn_.reset();
  const int stroke_type = std::max(0, std::min(kStrokeKinds - 1, sculpt_brush_.stroke));
  if (sculpt_dyntopo_ && !masking && stroke_type != kStrokeDragDot) sculpt_dyn_begin(g);  // (task 0242: the stroke tessellates; it is a snapshot step. Drag Dot re-places its one dab: it would leave a trail)
  else if (sculpt_dyntopo_ && !masking) Log::info("Dyntopo: not with the Drag Dot stroke (its one dab moves; the tessellation would stay behind)");
  sculpt_stroke_mesh_ = sculpt_mesh();  // (the mesh may have been made the object's own)
  return true;
}

/* A Dyntopo stroke (ADR 0078): the mesh made the object's own first (a snapshot keeps the mesh as it was), the
 * tessellator built over it; the stroke writes positions and topology in place and commits at the pen's lift. Refused,
 * with a log line, with a Subdivision Surface over the mesh (the cage would be tessellated, not the surface). */
bool Editor::sculpt_dyn_begin(GameObject *g) {
  if (sculpt_subdiv_levels() > 0) {
    Log::warn("Dyntopo: turn Dynamic Subdivision off first (the brush would tessellate the cage, not the surface)");
    return false;
  }
  MeshFilter *mf = mf_of(g);
  if (!mf || !mf->mesh) return false;
  sculpt_dyn_before_ = mf->mesh;
  mesh_make_mutable(mf->mesh);  // (the snapshot keeps the mesh as it was: the stroke's undo)
  mark_changed("Sculpt (Dyntopo)");
  Mesh *own = mf->mesh.get();
  sculpt_dyn_ = std::make_unique<meshops::Dyntopo>(*own);
  sculpt_dyn_split_ = sculpt_dyn_collapsed_ = 0;
  sculpt_cache_mesh_ = nullptr;
  sculpt_build_grid(*own, sculpt_grid_.cell);  // (over the owned mesh)
  return true;
}

/* The face under the Scene view pixel: the pixel's ray's first triangle of the base render mesh (hidden faces have none
 * there), UINT32_MAX off the mesh. */
uint32_t Editor::sculpt_face_under(float x, float y) {
  GameObject *g = scene_->find(sculpt_obj_);
  Mesh *m = sculpt_mesh();
  if (!g || !m) return UINT32_MAX;
  const Mat4 inv = g->world_matrix().inverse();
  const Ray ray = scene_r3d_.screen_ray(x, y);
  const Ray lr{inv.point(ray.origin), normalize(inv.dir(ray.dir))};
  const RenderMesh &rm = m->render_mesh();
  float best = 1e30f;
  uint32_t face = UINT32_MAX;
  for (size_t t = 0; t < rm.tri_count() && t < rm.tri_face.size(); t++) {
    const float d = ray_triangle(lr, rm.positions[rm.indices[t * 3]], rm.positions[rm.indices[t * 3 + 1]], rm.positions[rm.indices[t * 3 + 2]]);
    if (d > 0.0f && d < best) best = d, face = rm.tri_face[t];
  }
  return face;
}

/* The live face whose centre is nearest to p (a tombstoned face has one vertex: its centre is that vertex - skipped by
 * its size). */
uint32_t Editor::sculpt_face_nearest(const Mesh &m, Vec3 p) const {
  uint32_t best = UINT32_MAX;
  float best_d = 1e30f;
  for (uint32_t f = 0; f < (uint32_t)m.face_count(); f++) {
    const uint32_t n = m.face_size(f), *fv = m.face_verts(f);
    if (n < 3 || fv[0] == fv[1]) continue;
    Vec3 c(0.0f);
    for (uint32_t k = 0; k < n; k++) c += m.positions[fv[k]];
    const float d = length_sq(c / (float)n - p);
    if (d < best_d) best_d = d, best = f;
  }
  return best;
}

/* One instance's region brought to the detail size before the brush moves it: the vertices within the radius, their
 * faces triangulated, long edges split (the new vertices join the stroke's grid and its touched slots), short ones
 * collapsed. */
void Editor::sculpt_dyn_refine(Mesh &m, const SculptSym &sym, Vec3 hit_local, float r_local) {
  if (!sculpt_dyn_) return;
  std::vector<uint32_t> ids, made;
  std::vector<float> dist;
  sculpt_gather(m, sym.apply(hit_local), r_local, ids, dist);
  /* The face under the cursor seeds the real instance (a mesh coarser than the brush); a mirrored instance is seeded by
   * the face nearest the mirrored centre of that face, so both sides tessellate alike. */
  uint32_t seed = sculpt_dyn_seed_;
  if (!sym.identity() && seed != UINT32_MAX && seed < m.face_count()) {
    Vec3 c(0.0f);
    for (uint32_t k = 0; k < m.face_size(seed); k++) c += m.positions[m.face_verts(seed)[k]];
    seed = sculpt_face_nearest(m, sym.apply(c / (float)std::max(1u, m.face_size(seed))));
  }
  if (ids.empty() && seed == UINT32_MAX) return;
  const meshops::Dyntopo::Result r = sculpt_dyn_->refine(ids, sym.apply(hit_local), r_local, std::max(1e-6f, sculpt_dyntopo_detail_) * r_local, made, seed);
  sculpt_dyn_split_ += r.split, sculpt_dyn_collapsed_ += r.collapsed;
  if (sculpt_dyn_->faces_changed()) m.touch(), sculpt_dyn_->clear_changed();  // (the mesh's own tables - the vertex-face table the normals read - follow the new ids now, not at the dab's end)
  for (uint32_t v : made) sculpt_grid_.cells[cell_key(m.positions[v], sculpt_grid_.cell)].push_back(v);
  sculpt_grid_.verts = m.vert_count();
  sculpt_touched_.resize(m.vert_count(), 0);
  if (!sculpt_hidden_vert_.empty()) sculpt_hidden_vert_.resize(m.vert_count(), 0);
  if (m.sculpt_mask.size() != m.vert_count() && !m.sculpt_mask.empty()) m.sculpt_mask.resize(m.vert_count(), 0.0f);
}

/* The pen lifted on a Dyntopo stroke: the tombstones compacted, the step committed (no delta: the snapshot is it). */
void Editor::sculpt_dyn_end(Mesh &m) {
  if (!sculpt_dyn_) return;
  const bool topology = sculpt_dyn_->tombstones() > 0 || sculpt_dyn_split_ > 0 || sculpt_dyn_collapsed_ > 0;
  sculpt_dyn_->finish();
  sculpt_dyn_.reset();
  GameObject *g = scene_->find(sculpt_obj_);
  MeshFilter *mf = mf_of(g);
  const bool nothing = !topology && !(sculpt_dabbed_ && sculpt_stroke_move_ > 0.0f);
  if (nothing && mf && mf->mesh.get() == &m && sculpt_dyn_before_) {  // (a stroke that changed nothing: the mesh as it was comes back, no step)
    mf->mesh = sculpt_dyn_before_;
    pending_change_ = false;
    sculpt_dyn_before_.reset();
    sculpt_cache_mesh_ = nullptr;
    return;
  }
  sculpt_dyn_before_.reset();
  if (topology) Log::info("Dyntopo: %zu edges split, %zu collapsed; %zu vertices now", sculpt_dyn_split_, sculpt_dyn_collapsed_, m.vert_count());
  m.touch();
  commit_undo();
  sculpt_cache_mesh_ = nullptr;
  settle_meshes();
  scene_dirty_ = true;
  ui_.redraw = true;
}

MeshFilter *Editor::mf_of(GameObject *g) { return g ? g->mesh_filter() : nullptr; }

/* The pen lifted: the stroke is one undo step - a delta of the touched vertices, no snapshot (ADR 0074). */
void Editor::sculpt_end_stroke() {
  if (!sculpt_stroking_) return;
  sculpt_stroking_ = false;
  Mesh *m = sculpt_mesh();
  if (sculpt_dyn_ && m) {  // (task 0242: a Dyntopo stroke is its snapshot step)
    sculpt_dyn_end(*m);
    sculpt_delta_.reset();
    return;
  }
  if (!sculpt_dabbed_ || !m || m != sculpt_stroke_mesh_ || !sculpt_delta_) return;
  /* The step keeps the vertices that changed; a stroke that changed nothing makes none. */
  const bool mask = sculpt_delta_->mask;
  if (sculpt_keep_changed(*m, *sculpt_delta_) == 0) return;
  commit_undo();  // (a change made during the stroke - a key's - is its own step, before this one)
  UndoState s;
  s.delta = std::move(sculpt_delta_);
  static const char *const kMaskModes[] = {"", "Mask", "Unmask", "Blur Mask"};
  s.label = mask ? kMaskModes[std::max(1, std::min(3, (int)sculpt_mask_mode_))] : std::string("Sculpt ") + kSculptBrushes[std::max(0, std::min(kBrushKinds - 1, sculpt_brush_.kind))];
  s.bytes = s.delta->ids.size() * (sizeof(uint32_t) + (mask ? 2 * sizeof(float) : 2 * sizeof(Vec3)));
  undo_.push_back(std::move(s));
  undo_serial_++;
  trim_undo();
  redo_.clear();
  scene_dirty_ = true;
  settle_meshes();  // (ADR 0034: the tangents, now that the positions have settled)
  ui_.redraw = true;
}

/* The delta pared to the vertices the stroke changed (positions, or the mask's floats), their `after` filled in;
 * returns how many. */
size_t Editor::sculpt_keep_changed(const Mesh &m, SculptDelta &d) const {
  size_t kept = 0;
  if (d.mask) {
    for (size_t i = 0; i < d.ids.size(); i++) {
      if (d.ids[i] >= m.sculpt_mask.size() || m.sculpt_mask[d.ids[i]] == d.mask_before[i]) continue;
      d.ids[kept] = d.ids[i], d.mask_before[kept] = d.mask_before[i];
      kept++;
    }
    d.ids.resize(kept), d.mask_before.resize(kept);
    for (uint32_t v : d.ids) d.mask_after.push_back(m.sculpt_mask[v]);
    return kept;
  }
  for (size_t i = 0; i < d.ids.size(); i++) {
    if (std::memcmp(&m.positions[d.ids[i]], &d.before[i], sizeof(Vec3)) == 0) continue;
    d.ids[kept] = d.ids[i], d.before[kept] = d.before[i];
    kept++;
  }
  d.ids.resize(kept), d.before.resize(kept);
  for (uint32_t v : d.ids) d.after.push_back(m.positions[v]);
  return kept;
}

/* A stroke cut off (an undo, a new scene, the mode left): what it moved goes back and no step is made. */
void Editor::sculpt_abort_stroke() {
  if (sculpt_stroking_ && sculpt_dyn_) {  // (a Dyntopo stroke cut off keeps what it did, as a step: its mesh is already its own)
    Mesh *m = sculpt_mesh();
    if (m) sculpt_dyn_end(*m);
    sculpt_dyn_.reset();
    sculpt_stroking_ = false;
    sculpt_stroke_mesh_ = nullptr;
    sculpt_delta_.reset();
    return;
  }
  if (sculpt_stroking_) sculpt_restore_delta();
  sculpt_gesture_ = false;
  sculpt_stroking_ = false;
  sculpt_stroke_mesh_ = nullptr;
  sculpt_delta_.reset();
}

/* A touched vertex's position before its first dab goes into the stroke's delta; its slot (+1) is kept per vertex so a
 * later dab finds it at once. */
void Editor::sculpt_record(const Mesh &m, const std::vector<uint32_t> &ids) {
  SculptDelta &d = *sculpt_delta_;
  for (uint32_t v : ids) {
    if (sculpt_touched_[v]) continue;
    d.ids.push_back(v);
    if (d.mask) d.mask_before.push_back(v < m.sculpt_mask.size() ? m.sculpt_mask[v] : 0.0f);
    else d.before.push_back(m.positions[v]);
    sculpt_touched_[v] = (uint32_t)d.ids.size();  // (the slot + 1)
  }
}

/* What the stroke moved goes back - written in place, there is no snapshot to throw it away with (an abort, the Drag
 * Dot's re-placing). The mesh must still be the stroke's. */
void Editor::sculpt_restore_delta() {
  Mesh *m = sculpt_mesh();
  if (!m || m != sculpt_stroke_mesh_ || !sculpt_delta_) return;
  const SculptDelta &d = *sculpt_delta_;
  if (d.mask) {
    for (size_t i = 0; i < d.ids.size(); i++)
      if (d.ids[i] < m->sculpt_mask.size()) m->sculpt_mask[d.ids[i]] = d.mask_before[i];
    sculpt_mask_version_++;
    return;
  }
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
  const int kind = sculpt_mask_mode_ ? kMask : shift ? kSmooth : std::max(0, std::min(kBrushKinds - 1, sculpt_brush_.kind));
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
    if (sculpt_cage_) sculpt_cage_hit(x, y, hit, dist);  // (a Subdivision Surface drawn over the mesh: the point moved onto the cage - task 0239)
    hit_local = inv.point(hit);
    if (kind == kSnake) sculpt_snake_pos_ = hit_local, sculpt_snake_set_ = true, sculpt_grab_.dist = dist;
  }
  const float r_local = sculpt_world_radius(px_radius, dist) / scale;
  if (r_local > sculpt_grid_.cell * 4.0f || r_local < sculpt_grid_.cell * 0.25f) sculpt_build_grid(m, r_local);  // (a brush grown or shrunk far: the cells follow)
  if (kind == kGrab) sculpt_grab_sets_.clear(), sculpt_grab_.px0 = px, sculpt_grab_.dist = dist;
  const bool holds_set = kind == kGrab || kind == kSnake;  // (Grab and Snake Hook keep their sets: they tessellate at their first dab only)
  if (sculpt_dyn_ && kind != kMask && (!holds_set || sculpt_dab_px_.size() == 1)) {  // (task 0242: every instance tessellated, then every instance gathered and moved against the same positions)
    sculpt_dyn_seed_ = sculpt_face_under(x, y);
    for (const SculptSym &sym : sculpt_symmetries()) sculpt_dyn_refine(m, sym, hit_local, r_local);
  }
  /* Every instance gathered against the same positions, then its moves summed. */
  struct Instance {
    SculptSym sym;
    std::vector<uint32_t> ids;
    std::vector<float> w;
  };
  std::vector<Instance> instances;
  for (const SculptSym &sym : sculpt_symmetries()) {
    Instance in{sym, {}, {}};
    const size_t gathered = sculpt_weights(m, inv, sym, sym.apply(hit_local), r_local, strength, drag_px, kind != kMask, in.ids, in.w);
    if (sym.identity()) sculpt_last_ids_ = in.ids, sculpt_last_hit_ = hit_local, sculpt_last_radius_ = r_local, sculpt_last_gathered_ = gathered;
    if (!in.ids.empty()) instances.push_back(std::move(in));
  }
  if (instances.empty()) return;
  std::unordered_map<uint32_t, Vec3> moves;
  for (const Instance &in : instances) {
    sculpt_record(m, in.ids);
    if (kind == kMask) {  // (the mask painted, not the surface)
      sculpt_mask_apply(m, in.ids, in.w);
      continue;
    }
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
  if (!moves.empty()) m.deform_version++;  // (after a Dyntopo tessellation the version was bumped already: the render mesh is rebuilt either way)
  sculpt_dabbed_ = true;
  ui_.redraw = true;
}

/* The mask under an instance: added to (up to 1), taken from (down to 0), or blurred toward the neighbours' mean, by
 * the instance's weights. */
void Editor::sculpt_mask_apply(Mesh &m, const std::vector<uint32_t> &ids, const std::vector<float> &w) {
  std::vector<float> &mk = m.sculpt_mask;
  if (mk.size() != m.vert_count()) return;
  if (sculpt_mask_mode_ != kMaskBlur) {
    const float sign = sculpt_mask_mode_ == kMaskTake ? -1.0f : 1.0f;
    for (size_t i = 0; i < ids.size(); i++) mk[ids[i]] = std::max(0.0f, std::min(1.0f, mk[ids[i]] + sign * w[i]));
    sculpt_mask_version_++;
    return;
  }
  std::vector<uint32_t> ring;
  std::vector<float> target(ids.size());
  for (size_t i = 0; i < ids.size(); i++) {
    vertex_ring(m, ids[i], ring);
    float sum = 0.0f;
    for (uint32_t nb : ring) sum += mk[nb];
    target[i] = ring.empty() ? mk[ids[i]] : sum / (float)ring.size();
  }
  for (size_t i = 0; i < ids.size(); i++) mk[ids[i]] += (target[i] - mk[ids[i]]) * std::min(1.0f, w[i]);
  sculpt_mask_version_++;
}

/* Per vertex, whether every face of it is hidden (such a vertex is never gathered); empty when nothing is hidden. */
void Editor::sculpt_build_hidden(const Mesh &m) {
  sculpt_hidden_vert_.clear();
  if (!m.any_hidden()) return;
  const Mesh::VertexFaces &vf = m.vertex_faces();
  sculpt_hidden_vert_.assign(m.vert_count(), 0);
  for (uint32_t v = 0; v + 1 < vf.offsets.size(); v++) {
    if (vf.offsets[v + 1] == vf.offsets[v]) continue;  // (a loose vertex is never hidden)
    bool all = true;
    for (uint32_t k = vf.offsets[v]; k < vf.offsets[v + 1] && all; k++) all = m.face_hidden(vf.faces[k]);
    sculpt_hidden_vert_[v] = all ? 1 : 0;
  }
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
size_t Editor::sculpt_weights(const Mesh &m, const Mat4 &inv, const SculptSym &sym, Vec3 hit_local, float r_local, float strength, Vec2 drag_px, bool use_mask,
                              std::vector<uint32_t> &ids, std::vector<float> &w) const {
  const bool masked = use_mask && m.mask_fits();  // (task 0238: a held vertex is left out, a half-held one moves half)
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
    if (masked) {
      const float free = 1.0f - std::max(0.0f, std::min(1.0f, m.sculpt_mask[ids[i]]));
      if (free <= 0.0f) continue;
      weight *= free;
    }
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
      sculpt_delta_->clear();  // (the mask's floats too: a mask stroke's Drag Dot)
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
  if (sculpt_remesh_ && in.key_pressed[platform::KEY_ESCAPE] && !ui_.wants_keyboard()) {  // (task 0241: Esc stops the remesh first)
    sculpt_remesh_cancel();
    in.key_pressed[platform::KEY_ESCAPE] = false;
    return true;
  }
  if (!sculpt_stroking_ && !sculpt_gesture_) {
    if (scene_hovered_ && in.key_pressed[platform::KEY_ESCAPE] && !ui_.wants_keyboard()) {
      set_sculpt(false);
      in.key_pressed[platform::KEY_ESCAPE] = false;
      return true;
    }
    if (!(scene_hovered_ && in.pressed[0] && !ui_.any_active() && !qm_.open)) return false;
    if (in.ctrl() && in.shift()) {  // (Ctrl+Shift: the group under the pointer alone; off the model, everything shown - ZBrush's)
      ui_.consume_click();
      if (!sculpt_isolate_at(view, x, y)) sculpt_hide_op("none", "");
      return true;
    }
    if (sculpt_begin_stroke(view, x, y)) {
      ui_.consume_click();
      sculpt_stroke_to(view, x, y, true);
      return true;
    }
    if (!in.ctrl()) return false;  // (over nothing: the click is the view's)
    ui_.consume_click();  // (Ctrl off the model: a click inverts the mask, a drag masks a rectangle)
    sculpt_gesture_ = true, sculpt_gesture_from_ = {x, y};
    return true;
  }
  if (sculpt_gesture_) {
    if (in.key_pressed[platform::KEY_ESCAPE]) {  // (the gesture dropped: no invert, no rectangle)
      sculpt_gesture_ = false;
      in.key_pressed[platform::KEY_ESCAPE] = false;
      return true;
    }
    if (!in.down[0]) sculpt_gesture_end(x, y);
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
  if (!sculpt_gesture_) return;
  const Vec2 a = sculpt_gesture_from_ + Vec2((float)view.x, (float)view.y), b((float)u.in.mx, (float)u.in.my);  // (the rectangle being masked)
  const Recti r{(int)std::min(a.x, b.x), (int)std::min(a.y, b.y), (int)std::fabs(b.x - a.x), (int)std::fabs(b.y - a.y)};
  u.canvas.rect_outline(r, Color::hex(0xFFFFFF, 220), 1);
}

/* Ctrl released off the model: a click inverts the mask, a drag masks the rectangle. */
void Editor::sculpt_gesture_end(float x, float y) {
  sculpt_gesture_ = false;
  const Vec2 to(x, y);
  if (length(to - sculpt_gesture_from_) < kClickPx * std::max(0.5f, ui_.scale)) sculpt_mask_op("invert", {});
  else sculpt_rect_mask(sculpt_gesture_from_, to);
}

/* ---- the mask, the groups, hiding (task 0238): ordinary steps on a mesh of the object's own ---- */

/* The sculpted (or the active) object's mesh for an operation on the whole of it: a stroke under way ends, the mesh is
 * made the object's own (a snapshot step: the mask and the groups are not written into the snapshots' mesh), the
 * change is marked. Null, with a log line, when there is no mesh. */
Mesh *Editor::sculpt_mesh_for_op(const char *label) {
  GameObject *g = sculpt_ ? scene_->find(sculpt_obj_) : active_object();
  MeshFilter *mf = mf_of(g);
  if (!mf || !mf->mesh) {
    Log::warn("%s: select a mesh object first", label);
    return nullptr;
  }
  sculpt_end_stroke();  // (a stroke under way - a shortcut mid-stroke - is kept as its own step)
  sculpt_gesture_ = false;
  commit_undo();
  mesh_make_mutable(mf->mesh);
  sculpt_cache_mesh_ = nullptr;
  mark_changed(label);
  sculpt_mask_version_++;
  ui_.redraw = true;
  return mf->mesh.get();
}

/* sculpt mask clear|fill|invert|blur|sharpen|grow|shrink|rect <x0> <y0> <x1> <y1>: the whole mask at once. */
bool Editor::sculpt_mask_op(const std::string &op, const std::vector<std::string> &t) {
  static const char *const kOps[] = {"clear", "fill", "invert", "blur", "sharpen", "grow", "shrink", "rect"};
  int which = -1;
  for (int i = 0; i < 8; i++)
    if (op == kOps[i]) which = i;
  if (which < 0) {
    Log::info("usage: sculpt mask clear|fill|invert|blur|sharpen|grow|shrink|rect <x0> <y0> <x1> <y1>");
    return false;
  }
  if (which == 7) {
    if (t.size() < 7) {
      Log::info("usage: sculpt mask rect <x0> <y0> <x1> <y1> (Scene view pixels)");
      return false;
    }
    sculpt_rect_mask({(float)std::atof(t[3].c_str()), (float)std::atof(t[4].c_str())}, {(float)std::atof(t[5].c_str()), (float)std::atof(t[6].c_str())});
    return true;
  }
  static const char *const kLabels[] = {"Clear Mask", "Fill Mask", "Invert Mask", "Blur Mask", "Sharpen Mask", "Grow Mask", "Shrink Mask"};
  Mesh *m = sculpt_mesh_for_op(kLabels[which]);
  if (!m) return false;
  std::vector<float> &mk = m->sculpt_mask;
  if (!m->mask_fits()) mk.assign(m->vert_count(), 0.0f);
  sculpt_build_hidden(*m);  // (a hidden vertex keeps its mask through every operation)
  auto hidden = [&](uint32_t v) { return v < sculpt_hidden_vert_.size() && sculpt_hidden_vert_[v]; };
  if (which >= 3) {
    sculpt_mask_ring_op(*m, which);
    return true;
  }
  for (uint32_t v = 0; v < (uint32_t)m->vert_count(); v++) {
    if (hidden(v)) continue;
    mk[v] = which == 0 ? 0.0f : which == 1 ? 1.0f : 1.0f - mk[v];
  }
  return true;
}

/* Blur (3), sharpen (4), grow (5), shrink (6): each vertex against its ring, from the mask as it was. */
void Editor::sculpt_mask_ring_op(Mesh &m, int which) {
  std::vector<float> &mk = m.sculpt_mask;
  const std::vector<float> was = mk;
  std::vector<uint32_t> ring;
  for (uint32_t v = 0; v < (uint32_t)m.vert_count(); v++) {
    if (v < sculpt_hidden_vert_.size() && sculpt_hidden_vert_[v]) continue;
    vertex_ring(m, v, ring);
    if (ring.empty()) continue;
    float sum = 0.0f, lo = was[v], hi = was[v];
    for (uint32_t nb : ring) sum += was[nb], lo = std::min(lo, was[nb]), hi = std::max(hi, was[nb]);
    const float mean = sum / (float)ring.size();
    mk[v] = which == 3 ? mean : which == 4 ? std::max(0.0f, std::min(1.0f, was[v] + (was[v] - mean))) : which == 5 ? hi : lo;
  }
}

/* The vertices whose projection falls in the Scene view rectangle a-b (and face the camera, with Front Faces Only)
 * are masked fully - ZBrush's MaskRect. */
void Editor::sculpt_rect_mask(Vec2 a, Vec2 b) {
  Mesh *m = sculpt_mesh_for_op("Mask Rectangle");
  GameObject *g = m ? (sculpt_ ? scene_->find(sculpt_obj_) : active_object()) : nullptr;
  if (!m || !g) return;
  if (!m->mask_fits()) m->sculpt_mask.assign(m->vert_count(), 0.0f);
  sculpt_build_hidden(*m);
  const float x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x), y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
  const Mat4 &W = g->world_matrix();
  const Vec3 eye_local = W.inverse().point(cam_.position()), fwd_local = W.inverse().dir(cam_.forward());
  for (uint32_t v = 0; v < (uint32_t)m->vert_count(); v++) {
    if (v < sculpt_hidden_vert_.size() && sculpt_hidden_vert_[v]) continue;
    Vec2 px;
    float z;
    if (!scene_r3d_.project(W.point(m->positions[v]), px, z) || px.x < x0 || px.x > x1 || px.y < y0 || px.y > y1) continue;
    if (sculpt_brush_.front_only) {
      const Vec3 to_cam = cam_.ortho ? -fwd_local : eye_local - m->positions[v];
      if (!(dot(sculpt_vertex_normal(*m, v), to_cam) > 0.0f)) continue;
    }
    m->sculpt_mask[v] = 1.0f;
  }
}

/* sculpt group islands|material|mask|merge: the polygroups from the mesh's pieces, its materials, its mask, or all one. */
bool Editor::sculpt_group_op(const std::string &op) {
  if (op != "islands" && op != "material" && op != "mask" && op != "merge") {
    Log::info("usage: sculpt group islands|material|mask|merge");
    return false;
  }
  {
    const GameObject *g = sculpt_ ? scene_->find(sculpt_obj_) : active_object();
    const MeshFilter *mf = mf_of(const_cast<GameObject *>(g));
    if (op == "mask" && (!mf || !mf->mesh || !mask_any(*mf->mesh))) {  // (nothing masked: no group, no step)
      Log::info("sculpt group mask: paint a mask first");
      return false;
    }
  }
  Mesh *m = sculpt_mesh_for_op(op == "islands" ? "Groups from Islands" : op == "material" ? "Groups from Materials" : op == "mask" ? "Group from Mask" : "Merge Groups");
  if (!m) return false;
  const size_t nf = m->face_count();
  std::vector<uint8_t> hidden(nf, 0);
  for (size_t f = 0; f < nf; f++) hidden[f] = m->face_hidden(f) ? 1 : 0;
  if (op == "merge") m->face_group.assign(nf, 0);
  if (op == "material") {
    m->face_group.resize(nf);
    for (size_t f = 0; f < nf; f++) m->face_group[f] = m->material_of(f);
  }
  if (op == "islands") {
    std::vector<int> island;
    Mesh joined = *m;  // (connected through edges, seams ignored: `true` splits at seams, `false` counts UV islands - a group a face without UVs)
    joined.seams.clear();
    uvops::compute_islands(joined, nullptr, true, island);
    m->face_group.resize(nf);
    for (size_t f = 0; f < nf; f++) m->face_group[f] = f < island.size() ? std::max(0, island[f]) : 0;
  }
  if (op == "mask") sculpt_groups_from_mask(*m);
  for (size_t f = 0; f < nf; f++)
    if (hidden[f]) m->set_face_hidden(f, true);  // (what was hidden stays hidden, in its new group)
  m->touch();
  return true;
}

/* The faces whose every vertex is masked above a half: a new group, the next number. */
void Editor::sculpt_groups_from_mask(Mesh &m) {
  const size_t nf = m.face_count();
  m.face_group.resize(nf, 0);
  int next = 0;
  for (size_t f = 0; f < nf; f++) next = std::max(next, std::min(Mesh::kMaxGroup - 1, m.group_of(f)) + 1);
  for (size_t f = 0; f < nf; f++)
    if (face_all_masked(m, f)) m.face_group[f] = next;
}

/* sculpt hide group <n>|masked|unmasked|all|none|invert; sculpt show all: hidden faces leave the render mesh. */
bool Editor::sculpt_hide_op(const std::string &what, const std::string &arg) {
  const bool known = what == "group" || what == "masked" || what == "unmasked" || what == "all" || what == "none" || what == "invert";
  if (!known || (what == "group" && arg.empty())) {
    Log::info("usage: sculpt hide group <n>|masked|unmasked|all|none|invert | sculpt show all");
    return false;
  }
  Mesh *m = sculpt_mesh_for_op(what == "none" ? "Show All" : "Hide Faces");
  if (!m) return false;
  const int group = std::atoi(arg.c_str());
  for (size_t f = 0; f < m->face_count(); f++) {
    const bool hide = sculpt_hide_wanted(*m, f, what, group);
    if (what == "group" && !hide) continue;  // (hiding one group leaves the others as they are)
    m->set_face_hidden(f, hide);
  }
  m->touch();
  return true;
}

/* Whether `sculpt hide <what>` hides face f: its group, every face, none, the other way round, or by its mask (masked:
 * every vertex above a half; unmasked: none is). */
bool Editor::sculpt_hide_wanted(const Mesh &m, size_t f, const std::string &what, int group) const {
  if (what == "group") return m.group_of(f) == group;
  if (what == "all") return true;
  if (what == "none") return false;
  if (what == "invert") return !m.face_hidden(f);
  if (what == "masked") return face_all_masked(m, f);
  const uint32_t n = m.face_size(f), *fv = m.face_verts(f);  // (unmasked)
  for (uint32_t c = 0; c < n; c++)
    if (m.mask_fits() && m.sculpt_mask[fv[c]] > kMaskedAbove) return false;
  return true;
}

/* ---- Dynamic Subdivision (task 0239, ADR 0075) ---- */

/* The sculpted (or active) object's Subdivision Surface levels, 0 without one or with it off. */
int Editor::sculpt_subdiv_levels() const {
  const GameObject *g = sculpt_ ? scene_->find(sculpt_obj_) : active_object();
  const SubdivisionSurface *sub = g ? g->get<SubdivisionSurface>() : nullptr;
  return sub && sub->enabled ? std::max(0, sub->levels) : 0;
}

/* A Subdivision Surface on the object at `levels` (1-3), added, set or - at 0 - removed: one ordinary step. The brush
 * keeps working the cage; the surface follows each dab through the chain. */
void Editor::sculpt_set_subdiv(int levels) {
  GameObject *g = sculpt_ ? scene_->find(sculpt_obj_) : active_object();
  if (!mf_of(g)) {
    Log::warn("sculpt subdiv: select a mesh object first");
    return;
  }
  levels = std::max(0, std::min(3, levels));
  if (sculpt_subdiv_levels() == levels) return;
  sculpt_end_stroke();
  commit_undo();
  SubdivisionSurface *sub = g->get<SubdivisionSurface>();
  if (levels == 0) {
    if (sub) g->remove_component(sub);
    mark_changed("Dynamic Subdivision Off");
    return;
  }
  if (!sub) sub = g->add<SubdivisionSurface>(), sub->smooth = true;  // (a new one Catmull-Clark; a Simple one stays Simple)
  sub->levels = levels, sub->enabled = true;
  mark_changed("Dynamic Subdivision");
  Log::info("Dynamic Subdivision: level %d (the brush works the cage)", levels);
}

/* With a Subdivision Surface drawn over the mesh, the planes' point lies on the subdivided surface - inside the cage for
 * Catmull-Clark - and a small brush around it would reach no cage vertex. The brush works the cage, so the point is
 * moved onto it: the pixel's ray's first hit on the base mesh's triangles (Blender sculpts against the base mesh's
 * PBVH the same way). Only for a subdivision (another modifier's surface - a posed armature's, a displacement's - is
 * not a cage around the base), decided once a stroke (`sculpt_cage_`): nothing is evaluated per dab. False, and
 * nothing changed, when the ray misses the cage. */
bool Editor::sculpt_cage_hit(float x, float y, Vec3 &world, float &dist) {
  GameObject *g = scene_->find(sculpt_obj_);
  Mesh *m = sculpt_mesh();
  if (!g || !m) return false;
  const Mat4 &W = g->world_matrix();
  const Mat4 inv = W.inverse();
  const Ray ray = scene_r3d_.screen_ray(x, y);
  const Ray lr{inv.point(ray.origin), normalize(inv.dir(ray.dir))};
  const RenderMesh &rm = m->render_mesh();
  float best = 1e30f;
  for (size_t t = 0; t < rm.tri_count(); t++) {
    const float d = ray_triangle(lr, rm.positions[rm.indices[t * 3]], rm.positions[rm.indices[t * 3 + 1]], rm.positions[rm.indices[t * 3 + 2]]);
    if (d > 0.0f && d < best) best = d;
  }
  if (best >= 1e30f) return false;
  world = W.point(lr.origin + lr.dir * best);
  dist = length(world - ray.origin);
  return true;
}

/* ---- DynaMesh (task 0240, ADR 0076) ---- */

/* The sculpted (or active) object's mesh retopologized evenly at `resolution` cells along its longest side, as one
 * ordinary step (a topology change: a snapshot); the mode stays on and the next stroke rebuilds its grid. */
bool Editor::sculpt_dynamesh(int resolution, int smooth) {
  GameObject *g = sculpt_ ? scene_->find(sculpt_obj_) : active_object();
  MeshFilter *mf = mf_of(g);
  if (!mf || !mf->mesh) {
    Log::warn("DynaMesh: select a mesh object first");
    return false;
  }
  if (edit_mode_) exit_edit_mode();  // (as set_sculpt: a selection of the old mesh must not outlive it)
  if (weight_paint_) set_weight_paint(false);
  sculpt_end_stroke();
  sculpt_gesture_ = false;
  commit_undo();
  Mesh out;
  meshops::DynaMeshStats st;
  std::string why;
  if (!meshops::dynamesh(*mf->mesh, resolution, smooth, out, &st, &why)) {
    Log::warn("DynaMesh: %s", why.c_str());
    return false;
  }
  auto m = std::make_shared<Mesh>(std::move(out));
  mf->mesh = m;  // (the snapshot keeps the old mesh: an ordinary step)
  sculpt_cache_mesh_ = nullptr;
  sculpt_stroke_mesh_ = nullptr;
  mark_changed(strprintf("DynaMesh %d", resolution));
  Log::info("DynaMesh %d: %zu vertices / %zu faces -> %zu / %zu quads (a cell of %.4f, %zu cells merged; columns %.1f ms, nets %.1f ms, relax %.1f ms, %.1f ms in all)", resolution, st.verts_in,
            st.faces_in, st.verts_out, st.faces_out, st.cell, st.filled_cells, st.ms_columns, st.ms_nets, st.ms_smooth, st.ms_total);
  ui_.redraw = true;
  return true;
}

/* ---- Quad Remesh (task 0241, ADR 0077) ---- */

/* QuadriFlow on the sculpted (or active) object's mesh, on a worker: the mesh copied, the editor's frames go on, the
 * result lands through sculpt_remesh_poll as one ordinary step. One job at a time. */
bool Editor::sculpt_quad_remesh(const meshops::QuadRemeshParams &p) {
  if (!meshops::quad_remesh_available()) {
    Log::warn("Quad Remesh: this build has no QuadriFlow (it needs Blender's Eigen among the libraries)");
    return false;
  }
  if (sculpt_remesh_) {
    Log::warn("Quad Remesh: one is running (sculpt remesh cancel stops it)");
    return false;
  }
  if (playing_) {
    Log::warn("Quad Remesh: stop Play first");
    return false;
  }
  GameObject *g = sculpt_ ? scene_->find(sculpt_obj_) : active_object();
  MeshFilter *mf = mf_of(g);
  if (!mf || !mf->mesh) {
    Log::warn("Quad Remesh: select a mesh object first");
    return false;
  }
  if (edit_mode_) exit_edit_mode();
  if (weight_paint_) set_weight_paint(false);
  sculpt_end_stroke();
  sculpt_gesture_ = false;
  commit_undo();
  auto job = std::make_unique<RemeshJob>();
  job->obj = g->id;
  job->mesh_uid = mf->mesh->uid.v;
  job->version = mf->mesh->version, job->deform_version = mf->mesh->deform_version, job->mask_version = sculpt_mask_version_;
  job->input = mesh_copy_bare(*mf->mesh);  // (what the worker reads, without the render caches: a smaller stall at the start)
  job->params = p;
  RemeshJob *raw = job.get();
  job->thread = std::thread([raw] {  // (quad_remesh catches what QuadriFlow throws; nothing here logs - the main thread does)
    raw->ok = meshops::quad_remesh(*raw->input, raw->params, raw->out, &raw->stats, &raw->why, &raw->cancel);
    raw->done.store(true, std::memory_order_release);
  });
  sculpt_remesh_ = std::move(job);
  Log::info("Quad Remesh: working toward %d faces on a worker (Esc or `sculpt remesh cancel` stops it)", p.target_faces);
  ui_.redraw = true;
  return true;
}

/* Each frame: a finished job lands as a step when its mesh is still the one it started from, or is dropped with a
 * log line. */
void Editor::sculpt_remesh_poll() {
  if (!sculpt_remesh_ || !sculpt_remesh_->done.load(std::memory_order_acquire)) return;
  std::unique_ptr<RemeshJob> job = std::move(sculpt_remesh_);
  if (job->thread.joinable()) job->thread.join();
  if (job->cancel.load(std::memory_order_relaxed)) {  // (cancelled late, after QuadriFlow's last check: the result is not wanted)
    Log::info("Quad Remesh: cancelled - the result is dropped");
    return;
  }
  if (!job->ok) {
    Log::warn("Quad Remesh: %s", job->why.c_str());
    return;
  }
  GameObject *g = scene_->find(job->obj);
  MeshFilter *mf = mf_of(g);
  const bool same = mf && mf->mesh && mf->mesh->uid.v == job->mesh_uid && mf->mesh->version == job->version && mf->mesh->deform_version == job->deform_version &&
                    sculpt_mask_version_ == job->mask_version;
  if (!same) {
    Log::warn("Quad Remesh: the mesh changed while it worked (a stroke, a mask, an undo, another scene) - the result is dropped");
    return;
  }
  if ((edit_mode_ && edit_obj_ == job->obj) || weight_paint_ || playing_) {  // (a mode entered meanwhile holds the old mesh's selections and weights)
    Log::warn("Quad Remesh: Edit Mode, Weight Paint or Play began while it worked - the result is dropped");
    return;
  }
  sculpt_end_stroke();
  commit_undo();
  auto m = std::make_shared<Mesh>(std::move(job->out));
  mf->mesh = m;
  sculpt_cache_mesh_ = nullptr;
  sculpt_stroke_mesh_ = nullptr;
  mark_changed(strprintf("Quad Remesh %d", job->params.target_faces));
  Log::info("Quad Remesh %d: %zu vertices / %zu faces -> %zu / %zu quads in %.0f ms", job->params.target_faces, job->stats.verts_in, job->stats.faces_in, job->stats.verts_out, job->stats.faces_out,
            job->stats.ms_total);
  ui_.redraw = true;
}

/* The running job told to stop (QuadriFlow reads the flag between its stages); its result is dropped when it ends. */
void Editor::sculpt_remesh_cancel() {
  if (!sculpt_remesh_) return;
  sculpt_remesh_->cancel.store(true, std::memory_order_relaxed);
  Log::info("Quad Remesh: cancelling (it stops at the next stage)");
}

/* How many faces of the object's mesh are hidden (the status bar says so outside Sculpt Mode too). */
size_t Editor::hidden_face_count(const GameObject *g) const {
  const MeshFilter *mf = mf_of(const_cast<GameObject *>(g));
  if (!mf || !mf->mesh) return 0;
  size_t n = 0;
  for (int32_t v : mf->mesh->face_group) n += v < 0 ? 1 : 0;
  return n;
}

/* Ctrl+Shift+click on the model: the group of the face under the pointer alone (every other group hidden). False
 * off the model. */
bool Editor::sculpt_isolate_at(const Recti &view, float x, float y) {
  Mesh *m = sculpt_mesh();
  GameObject *g = m ? scene_->find(sculpt_obj_) : nullptr;
  Vec3 hit;
  float dist;
  if (!m || !g || !sculpt_hit(view, x, y, hit, dist)) return false;
  (void)hit, (void)dist;
  /* The face under the pointer: the ray through the render mesh (hidden faces have no triangles in it). */
  const Mat4 inv = g->world_matrix().inverse();
  const Ray ray = scene_r3d_.screen_ray(x, y);
  const Ray lr{inv.point(ray.origin), normalize(inv.dir(ray.dir))};
  const RenderMesh &rm = m->render_mesh();
  float best = 1e30f;
  size_t best_f = SIZE_MAX;
  for (size_t t = 0; t < rm.tri_count() && t < rm.tri_face.size(); t++) {
    const float d = ray_triangle(lr, rm.positions[rm.indices[t * 3]], rm.positions[rm.indices[t * 3 + 1]], rm.positions[rm.indices[t * 3 + 2]]);
    if (d > 0.0f && d < best) best = d, best_f = rm.tri_face[t];
  }
  if (best_f == SIZE_MAX || best_f >= m->face_count()) return true;  // (the plane said the object, the mesh said nothing: no change)
  const int group = m->group_of(best_f);
  Mesh *mm = sculpt_mesh_for_op("Isolate Group");
  if (!mm) return false;
  for (size_t f = 0; f < mm->face_count(); f++) mm->set_face_hidden(f, mm->group_of(f) != group);
  mm->touch();
  return true;
}

/* The Scene view's colours for the sculpted mesh while the mask or the groups are shown: the material's colour pulled
 * toward a dark grey by each vertex's mask, a hue per group (per render vertex: a boundary vertex takes the last face
 * written). Cached per render mesh build, mask version, what is shown and the colour. */
bool Editor::sculpt_colors_wanted(const Mesh &m) const {
  return sculpt_ && ((sculpt_show_mask_ && mask_any(m)) || (sculpt_show_groups_ && !m.face_group.empty()));
}

/* A polygroup's colour (task 0269): Okabe and Ito's palette, told apart under the common colour-vision deficiencies,
 * then the same eight lighter and darker - past eight groups the lightness changes too, which every viewer sees. */
Vec3 sculpt_group_color(int g) {
  static const Vec3 kOkabeIto[8] = {{0.902f, 0.624f, 0.000f}, {0.337f, 0.706f, 0.914f}, {0.000f, 0.620f, 0.451f}, {0.941f, 0.894f, 0.259f},
                                    {0.000f, 0.447f, 0.698f}, {0.835f, 0.369f, 0.000f}, {0.800f, 0.475f, 0.655f}, {0.600f, 0.600f, 0.600f}};
  const unsigned k = (unsigned)std::max(0, g);
  const Vec3 c = kOkabeIto[k % 8];
  const unsigned tier = (k / 8) % 3;
  if (tier == 1) return c * 0.55f + Vec3(0.45f);  // lighter
  if (tier == 2) return c * 0.55f;                 // darker
  return c;
}

const std::vector<Vec3> *Editor::sculpt_colors(const Mesh &m, const RenderMesh &rm, Vec3 albedo) {
  const int flags = (sculpt_show_mask_ && mask_any(m) ? 1 : 0) | (sculpt_show_groups_ && !m.face_group.empty() ? 2 : 0);
  if (!flags) return nullptr;
  const bool same_albedo = std::memcmp(&sculpt_colors_albedo_, &albedo, sizeof(Vec3)) == 0;
  if (sculpt_colors_serial_ == rm.build_serial && sculpt_colors_version_ == sculpt_mask_version_ && sculpt_colors_flags_ == flags && same_albedo &&  // (the build, not the in-place serial: the colours don't read positions)
      sculpt_colors_.size() == rm.positions.size())
    return &sculpt_colors_;
  sculpt_colors_.assign(rm.positions.size(), albedo);
  for (size_t k = 0; k < rm.indices.size() && k < rm.tri_corner.size(); k++) {
    const uint32_t c = rm.tri_corner[k], rv = rm.indices[k];
    if (c >= m.corner_verts.size() || rv >= sculpt_colors_.size()) continue;
    Vec3 col = albedo;
    if ((flags & 2) && k / 3 < rm.tri_face.size()) col = sculpt_group_color(m.group_of(rm.tri_face[k / 3]));
    if (flags & 1) {
      const uint32_t v = m.corner_verts[c];
      const float mk = kMaskDarkening * std::max(0.0f, std::min(1.0f, v < m.sculpt_mask.size() ? m.sculpt_mask[v] : 0.0f));
      col = col * (1.0f - mk) + kMaskDarkColor * mk;
    }
    sculpt_colors_[rv] = col;
  }
  sculpt_colors_serial_ = rm.build_serial, sculpt_colors_version_ = sculpt_mask_version_, sculpt_colors_flags_ = flags, sculpt_colors_albedo_ = albedo;
  return &sculpt_colors_;
}

/* ---- undo ---- */

/* A delta step applied in place: the positions - or a mask stroke's floats (task 0238) - before (undo) or after (redo)
 * into the object's mesh, which the stable snapshot shares (ADR 0074). A mesh that no longer fits the step (replaced,
 * renumbered) drops it with a log line. */
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
  if (s.delta->mask) {  // (task 0238: the mask's floats, in place as the positions are)
    if (!m.mask_fits()) m.sculpt_mask.assign(m.vert_count(), 0.0f);
    const std::vector<float> &mk = undo ? s.delta->mask_before : s.delta->mask_after;
    for (size_t i = 0; i < s.delta->ids.size(); i++) m.sculpt_mask[s.delta->ids[i]] = mk[i];
    sculpt_mask_version_++;
  }
  else {
    const std::vector<Vec3> &src = undo ? s.delta->before : s.delta->after;
    for (size_t i = 0; i < s.delta->ids.size(); i++) m.positions[s.delta->ids[i]] = src[i];
    m.deform_version++;
    sculpt_cache_mesh_ = nullptr;  // (the grid is stale)
    settle_meshes();
  }
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
      Log::info("usage: sculpt brush draw|smooth|grab|clay|inflate|flatten|pinch|crease|scrape|snake|mask");
      return;
    }
    sculpt_brush_.kind = kind;
  }
  else if (v == "mask") sculpt_mask_op(arg(2, ""), t);  // (task 0238)
  else if (v == "group") sculpt_group_op(arg(2, ""));
  else if (v == "groups") sculpt_show_groups_ = arg(2, "on") == "toggle" ? !sculpt_show_groups_ : arg(2, "on") != "off";
  else if (v == "maskview") sculpt_show_mask_ = arg(2, "on") == "toggle" ? !sculpt_show_mask_ : arg(2, "on") != "off";
  else if (v == "hide") sculpt_hide_op(arg(2, ""), arg(3, "0"));
  else if (v == "show") sculpt_hide_op("none", "");
  else if (v == "isolate" && t.size() > 3) {  // (Ctrl+Shift+click's test hook, in the Scene view's pixels)
    if (!sculpt_isolate_at(scene_rect_, (float)std::atof(t[2].c_str()), (float)std::atof(t[3].c_str()))) sculpt_hide_op("none", "");
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
  else if (v == "lazy") sculpt_brush_.lazy = arg(2, "0") == "toggle" ? (sculpt_brush_.lazy > 0.0f ? 0.0f : 30.0f) : std::max(0.0f, std::min(300.0f, (float)std::atof(arg(2, "0").c_str())));
  else if (v == "dynamesh") {  // sculpt dynamesh [resolution] [smooth]  (task 0240)
    auto number = [](const std::string &a) { return !a.empty() && std::all_of(a.begin(), a.end(), [](char ch) { return ch >= '0' && ch <= '9'; }); };
    if ((t.size() > 2 && !number(t[2])) || (t.size() > 3 && !number(t[3]))) {
      Log::info("usage: sculpt dynamesh [resolution 8-256] [smooth 0-10]");
      return;
    }
    if (t.size() > 2) sculpt_dyna_res_ = std::max(8, std::min(256, std::atoi(t[2].c_str())));
    if (t.size() > 3) sculpt_dyna_smooth_ = std::max(0, std::min(10, std::atoi(t[3].c_str())));
    sculpt_dynamesh(sculpt_dyna_res_, sculpt_dyna_smooth_);
  }
  else if (v == "dyntopo") {  // sculpt dyntopo on|off|toggle|<fraction of the brush radius>  (task 0242)
    const std::string a = arg(2, "toggle");
    if (a == "on" || a == "off" || a == "toggle") sculpt_dyntopo_ = a == "on" ? true : a == "off" ? false : !sculpt_dyntopo_;
    else {
      const float d = (float)std::atof(a.c_str());
      if (!(d > 0.0f)) {
        Log::info("usage: sculpt dyntopo on|off|toggle|<detail 0.05-1, of the brush radius>");
        return;
      }
      sculpt_dyntopo_detail_ = std::max(0.05f, std::min(1.0f, d)), sculpt_dyntopo_ = true;
    }
    Log::info("Dyntopo %s (detail %.2f of the brush radius)", sculpt_dyntopo_ ? "on" : "off", sculpt_dyntopo_detail_);
  }
  else if (v == "remesh") {  // sculpt remesh [faces] [sharp|nosharp] [boundary|noboundary] [adaptive|uniform] [seed <n>] | cancel  (task 0241)
    if (arg(2, "") == "cancel") {
      sculpt_remesh_cancel();
      return;
    }
    meshops::QuadRemeshParams q = sculpt_remesh_params_;
    for (size_t i = 2; i < t.size(); i++) {
      const std::string a = to_lower(t[i]);
      const bool number = !a.empty() && std::all_of(a.begin(), a.end(), [](char ch) { return ch >= '0' && ch <= '9'; });
      if (number) q.target_faces = std::max(10, std::min(2000000, std::atoi(a.c_str())));
      else if (a == "sharp") q.preserve_sharp = true;
      else if (a == "nosharp") q.preserve_sharp = false;
      else if (a == "boundary") q.preserve_boundary = true;
      else if (a == "noboundary") q.preserve_boundary = false;
      else if (a == "adaptive") q.adaptive_scale = true;
      else if (a == "uniform") q.adaptive_scale = false;
      else if (a == "seed" && i + 1 < t.size()) q.seed = std::atoi(t[++i].c_str());
      else {
        Log::info("usage: sculpt remesh [faces] [sharp|nosharp] [boundary|noboundary] [adaptive|uniform] [seed <n>] | cancel");
        return;
      }
    }
    sculpt_remesh_params_ = q;
    sculpt_quad_remesh(q);
  }
  else if (v == "subdiv") {  // sculpt subdiv <n>|off|+|-  (task 0239)
    const std::string a = arg(2, "1");
    const bool number = !a.empty() && std::all_of(a.begin(), a.end(), [](char ch) { return ch >= '0' && ch <= '9'; });
    if (a != "off" && a != "+" && a != "-" && !number) {
      Log::info("usage: sculpt subdiv <0-3>|off|+|-");
      return;
    }
    sculpt_set_subdiv(a == "off" ? 0 : a == "+" ? sculpt_subdiv_levels() + 1 : a == "-" ? sculpt_subdiv_levels() - 1 : std::atoi(a.c_str()));
  }
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
    Log::info("usage: sculpt on|off | brush draw|smooth|grab|clay|inflate|flatten|pinch|crease|scrape|snake | size <px>|+|- | strength <0-1> | falloff <name> | carve on|off|toggle | front on|off | pressure off|strength|size | symmetry x|y|z|radial <n>|off | lazy <px> | alpha <image>|off|turn|noturn | stroke dots|dot|rect|freehand|spray | subdiv <n>|off|+|- | dynamesh [resolution] [smooth] | dyntopo on|off|toggle|<detail> | remesh [faces] [sharp|nosharp] [boundary|noboundary] [adaptive|uniform] [seed <n>]|cancel | mask clear|fill|invert|blur|sharpen|grow|shrink|rect <x0> <y0> <x1> <y1> | group islands|material|mask|merge | groups on|off | maskview on|off | hide group <n>|masked|unmasked|all|none|invert | show all | dab <x> <y> [pressure] | stroke <x0> <y0> <x1> <y1> [n]");
    return;
  }
  ui_.redraw = true;
}

}  // namespace bl

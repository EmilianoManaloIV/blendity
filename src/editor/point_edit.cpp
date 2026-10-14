// SPDX-License-Identifier: GPL-2.0-or-later
// Editing a Curve's and a Lattice's points with Edit Mode's keys (task 0054): Extrude grows a curve from its ends,
// Loop Cut inserts a curve point under the mouse or adds a lattice row, Merge at Center joins curve points, Delete
// removes curve points or a whole lattice row, Alt+C toggles a curve's Closed. Blender: Curve Edit Mode's Extrude
// (E), Subdivide, Toggle Cyclic (Alt+C) and the Lattice's resolution; Unity has no curves of its own.
#include "editor.h"

#include "../scene/curve.h"
#include "../scene/scene.h"

#include <algorithm>
#include <cmath>

namespace bl {

namespace {

/* The path sample nearest each control point, in order along the path (the curve passes through its points). */
std::vector<size_t> point_samples(const Curve &c, const CurvePath &path) {
  std::vector<size_t> at;
  size_t from = 0;
  /* Each point's sample lies within about a segment's samples of the last one's (a window, not the rest of the path:
   * 4096 points at resolution 256 would be billions of distances on one keypress). */
  const size_t res = c.type == 2 ? 1 : (size_t)std::max(1, c.resolution);
  for (const CurvePoint &p : c.points) {
    size_t best = from;
    float bd = 1e30f;
    const size_t end = std::min(path.pos.size(), from + 2 * res + 2);
    for (size_t k = from; k < end; k++) {
      const float d = length_sq(path.pos[k] - p.position);
      if (d < bd) bd = d, best = k;
    }
    at.push_back(best);
    from = best;
  }
  return at;
}

float dist_to_segment_2d(Vec2 p, Vec2 a, Vec2 b, float *t_out = nullptr) {
  const Vec2 ab = b - a;
  const float l2 = dot(ab, ab);
  const float t = l2 > 0.0f ? std::max(0.0f, std::min(1.0f, dot(p - a, ab) / l2)) : 0.0f;
  if (t_out) *t_out = t;
  return length(p - (a + ab * t));
}

}  // namespace

bool Editor::curve_extrude(GameObject *g, const std::vector<int> &pts) {
  Curve *c = g ? g->curve() : nullptr;
  if (!c || c->closed || c->points.empty()) {
    Log::warn("Extrude: select an end point of an open curve");
    return false;
  }
  const int n = (int)c->points.size();
  bool first = false, last = false;
  for (int i : pts) first = first || i == 0, last = last || i == n - 1;
  if (!first && !last) {
    Log::warn("Extrude: select an end point of the curve (the first or the last)");
    return false;
  }
  if (n + (first && last && n > 1 ? 2 : 1) > Curve::kMaxPoints) {
    Log::warn("Extrude: '%s' has the most points a curve can have (%d)", g->name.c_str(), Curve::kMaxPoints);
    return false;
  }
  /* The new point continues the curve's direction at that end, as far as the last segment. */
  auto beyond = [&](int end, int prev) {
    const Vec3 e = c->points[(size_t)end].position;
    const Vec3 d = n > 1 ? e - c->points[(size_t)prev].position : Vec3(1, 0, 0);
    return e + (length(d) > 1e-6f ? d : Vec3(1, 0, 0));
  };
  std::vector<int> sel;
  if (last) {
    CurvePoint p = c->points.back();
    p.position = beyond(n - 1, n - 2);
    c->points.push_back(p);
    sel.push_back((int)c->points.size() - 1);
  }
  if (first && !(last && n == 1)) {
    CurvePoint p = c->points.front();
    p.position = beyond(0, 1);
    c->points.insert(c->points.begin(), p);
    for (int &s : sel) s++;
    sel.push_back(0);
  }
  pt_obj_ = g->id;
  pt_sel_ = sel;
  mark_changed("Extrude Curve Point");
  return true;
}

bool Editor::curve_cut(GameObject *g, int segment, const Vec3 *at_local) {
  Curve *c = g ? g->curve() : nullptr;
  if (!c || c->points.size() < 2 || (int)c->points.size() >= Curve::kMaxPoints) return false;
  const int n = (int)c->points.size(), segs = c->closed ? n : n - 1;
  if (segment < 0 || segment >= segs) return false;
  Vec3 p;
  if (at_local) p = *at_local;
  else {
    /* The curve's own path half way between the two points (in samples), not the straight midpoint. */
    const CurvePath &path = c->local_path();
    const std::vector<size_t> at = point_samples(*c, path);
    const size_t a = at[(size_t)segment];
    const size_t b = segment + 1 < n ? at[(size_t)segment + 1] : path.pos.size() - 1;  // (closed: the wrap's end)
    p = path.valid() && b > a ? path.pos[(a + b) / 2] : (c->points[(size_t)segment].position + c->points[(size_t)((segment + 1) % n)].position) * 0.5f;
  }
  CurvePoint np = c->points[(size_t)segment];
  const CurvePoint &nb = c->points[(size_t)((segment + 1) % n)];
  np.position = p;
  np.tilt = (np.tilt + nb.tilt) * 0.5f;
  np.radius = (np.radius + nb.radius) * 0.5f;
  c->points.insert(c->points.begin() + segment + 1, np);
  pt_obj_ = g->id;
  pt_sel_ = {segment + 1};
  mark_changed("Loop Cut Curve");
  return true;
}

bool Editor::curve_merge(GameObject *g, const std::vector<int> &pts) {
  Curve *c = g ? g->curve() : nullptr;
  if (!c || pts.size() < 2) {
    Log::warn("Merge at Center: select two or more points of a curve");
    return false;
  }
  std::vector<int> s;
  for (int i : pts)
    if (i >= 0 && i < (int)c->points.size()) s.push_back(i);  // (the console passes what it was given)
  std::sort(s.begin(), s.end());
  s.erase(std::unique(s.begin(), s.end()), s.end());
  if (s.size() < 2) {
    Log::warn("Merge at Center: select two or more points of the curve");
    return false;
  }
  if ((int)c->points.size() - (int)s.size() + 1 < 2) {
    Log::warn("Merge at Center: a curve keeps at least two points");
    return false;
  }
  Vec3 mid(0.0f);
  for (int i : s) mid += c->points[(size_t)i].position;
  c->points[(size_t)s[0]].position = mid / (float)s.size();
  for (size_t k = s.size(); k-- > 1;) c->points.erase(c->points.begin() + s[k]);
  pt_obj_ = g->id;
  pt_sel_ = {s[0]};
  mark_changed("Merge Curve Points");
  return true;
}

bool Editor::lattice_resize(GameObject *g, int axis, int delta) {
  Lattice *l = g ? g->get<Lattice>() : nullptr;
  if (!l || axis < 0 || axis > 2) return false;
  int n[3] = {l->points_u, l->points_v, l->points_w};
  const int want = std::max(2, std::min(n[axis] + delta, Lattice::kMaxRes));
  if (want == n[axis]) {
    Log::warn("Lattice: %s already %d points along %c", delta > 0 ? "has the most," : "has the fewest,", n[axis], "UVW"[axis]);
    return false;
  }
  n[axis] = want;
  l->resample(n[0], n[1], n[2]);  // the shape kept (linear resampling, Blender's resize)
  pt_sel_.clear();
  mark_changed(delta > 0 ? "Loop Cut Lattice" : "Delete Lattice Row");
  return true;
}

/* The lattice axis whose row the selected points fill (a full plane of points at one index along it), or -1. */
int Editor::lattice_selected_row_axis(const Lattice &l, const std::vector<int> &pts) const {
  const int n[3] = {l.points_u, l.points_v, l.points_w};
  for (int axis = 0; axis < 3; axis++) {
    const int plane = n[0] * n[1] * n[2] / n[axis];
    if ((int)pts.size() != plane) continue;
    int index = -1;
    bool ok = true;
    for (int i : pts) {
      const int c[3] = {i % n[0], (i / n[0]) % n[1], i / (n[0] * n[1])};
      if (index < 0) index = c[axis];
      ok = ok && c[axis] == index;
    }
    if (ok) return axis;
  }
  return -1;
}

/* Ctrl+R over a curve or lattice (the active object, its points shown): the segment or edge under the mouse. */
bool Editor::point_loop_cut_at(const Recti &view, int mx, int my) {
  GameObject *g = active_object();
  if (!g || edit_mode_ || !is_selected(g->id)) return false;
  const Mat4 &w = g->world_matrix();
  auto screen = [&](Vec3 local, Vec2 &out) {
    float z;
    if (!scene_r3d_.project(w.point(local), out, z)) return false;
    out = Vec2(view.x + out.x, view.y + out.y);
    return true;
  };
  const Vec2 m((float)mx, (float)my);
  const float reach = (float)ui_.px(12);
  if (Curve *c = g->curve()) {
    const CurvePath &path = c->local_path();
    if (!path.valid() || c->points.size() < 2) return false;
    /* The path sample under the mouse, then which segment holds it. */
    /* The path span under the mouse (a closed path's closing span too) and where along it: the point goes there,
     * not on a sample (a Poly curve's spans are its whole segments). */
    const size_t m_samples = path.pos.size();
    size_t best = 0;
    float bd = 1e30f, bt = 0.5f;
    for (size_t k = 0; k < m_samples; k++) {
      const size_t k2 = k + 1 < m_samples ? k + 1 : (path.closed ? 0 : m_samples);
      if (k2 >= m_samples || k2 == k) continue;
      Vec2 a, b;
      if (!screen(path.pos[k], a) || !screen(path.pos[k2], b)) continue;
      float t;
      const float d = dist_to_segment_2d(m, a, b, &t);
      if (d < bd) bd = d, best = k, bt = t;
    }
    if (bd > reach) return false;
    const size_t best2 = best + 1 < m_samples ? best + 1 : 0;
    const Vec3 where = lerp(path.pos[best], path.pos[best2], std::max(0.05f, std::min(0.95f, bt)));  // never on a point
    const std::vector<size_t> at = point_samples(*c, path);
    int seg = (int)c->points.size() - (c->closed ? 1 : 2);
    for (size_t i = 0; i + 1 < at.size(); i++)
      if (best >= at[i] && best < at[i + 1]) seg = (int)i;
    return curve_cut(g, seg, &where);
  }
  if (Lattice *l = g->get<Lattice>()) {
    const int n[3] = {l->points_u, l->points_v, l->points_w};
    int axis = -1;
    float bd = 1e30f;
    for (int i = 0; i < l->size(); i++) {
      const int c[3] = {i % n[0], (i / n[0]) % n[1], i / (n[0] * n[1])};
      const int step[3] = {1, n[0], n[0] * n[1]};
      for (int ax = 0; ax < 3; ax++) {
        if (c[ax] + 1 >= n[ax]) continue;
        Vec2 a, b;
        if (!screen(l->control_point(i), a) || !screen(l->control_point(i + step[ax]), b)) continue;
        const float d = dist_to_segment_2d(m, a, b);
        if (d < bd) bd = d, axis = ax;
      }
    }
    if (axis < 0 || bd > reach) return false;
    return lattice_resize(g, axis, +1);
  }
  return false;
}

/* Edit Mode's keys on a curve's or lattice's points, outside Edit Mode (handle_shortcuts asks first). */
bool Editor::point_shortcut(const std::string &id) {
  if (edit_mode_) return false;
  GameObject *g = active_object();
  if (id == "mesh.extrude" && g && is_selected(g->id) && g->get<Bone>()) return bone_extrude(g);  // (a bone: grow one from its tip)
  if (!g || !is_selected(g->id) || !g->point_source()) return false;
  std::vector<int> pts;
  GameObject *pg = point_target(pts);
  if (id == "points.toggle_closed") {
    Curve *c = g->curve();
    if (!c) return false;
    c->closed = !c->closed;
    mark_changed(c->closed ? "Close Curve" : "Open Curve");
    return true;
  }
  if (id == "mesh.loop_cut") return scene_hovered_ && point_loop_cut_at(scene_rect_, ui_.in.mx, ui_.in.my);
  if (!pg) return false;  // the rest act on selected points
  if (id == "mesh.extrude") return curve_extrude(pg, pts);
  if (id == "mesh.merge_center") return curve_merge(pg, pts);
  if (id == "edit.delete") {
    if (pg->curve()) {
      if ((int)pg->curve()->points.size() - (int)pts.size() < 2) {
        Log::warn("Delete: a curve keeps at least two points");
        return true;  // (the key is used: the curve stays)
      }
      delete_selected();
      return true;
    }
    if (const Lattice *l = pg->get<Lattice>()) {
      const int axis = lattice_selected_row_axis(*l, pts);
      if (axis < 0) {
        Log::warn("Delete: select a whole row of the lattice's points (or no points, to delete the lattice)");
        return true;
      }
      lattice_resize(pg, axis, -1);
      return true;
    }
  }
  return false;
}

}  // namespace bl

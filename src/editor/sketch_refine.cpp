// SPDX-License-Identifier: GPL-2.0-or-later
// The drawing over the model (task 0226, ADR 0066): a picture-plane layer remembers the camera it was drawn in and
// fades as the view turns away from it; corrections on a scene canvas are made in the plane's metres (a true arc, a
// true circle); a stroke is projected onto the face under it, traced into a knife cut or a curve; the model's edges
// are drawn as an overlay to draw against, and the ruler and the ellipse guide snap to its edges and circular faces.
#include "editor.h"

#include <algorithm>
#include <cmath>

namespace bl {

/* ------------------------------------------------------------------ the layer's view */

/* The camera the layer's picture-plane strokes were drawn in: remembered at the first of them, and again when the
 * layer has none left (erased, cleared, undone). Drawing on it from far away - where its strokes have faded out -
 * says so: the stroke is kept and shows from the layer's view. */
void Editor::sketch_record_layer_view(SketchLayer &layer) {
  const Sketch &s = scene_->sketch;
  bool any = false;
  for (const SketchStroke &st : s.strokes) any = any || (st.canvas == 0 && st.layer >= 0 && st.layer < (int)s.layers.size() && &s.layers[(size_t)st.layer] == &layer);
  if (layer.has_view && any) {
    if (sketch_layer_fade(layer) <= 0.0f) Log::warn("Sketch: this layer was drawn from another view and has faded out here - sketch view goes back to it, or add a layer");
    return;
  }
  layer.has_view = true;
  layer.view_yaw = cam_.yaw, layer.view_pitch = cam_.pitch, layer.view_fov = cam_.fov, layer.view_dist = cam_.distance;
  layer.view_pivot = cam_.pivot;
}

/* How much of a layer shows from the camera as it is: all of it within 5 degrees of the view it was drawn in, none
 * past 30 (a construction drawing is true from its view); a layer with no view shows everywhere. */
float Editor::sketch_layer_fade(const SketchLayer &layer) const {
  if (!layer.has_view) return 1.0f;
  SceneCamera then = cam_;
  then.yaw = layer.view_yaw, then.pitch = layer.view_pitch;
  const float c = std::max(-1.0f, std::min(1.0f, dot(then.forward(), cam_.forward())));
  const float deg = std::acos(c) * 180.0f / kPi;
  return std::max(0.0f, std::min(1.0f, 1.0f - (deg - 5.0f) / 25.0f));
}

/* Back to the active layer's view (sketch view): the camera as it was when the layer was begun. */
bool Editor::sketch_layer_view() {
  Sketch &s = scene_->sketch;
  if (s.layers.empty()) return false;
  const SketchLayer &layer = s.layer_at(s.active_layer);
  if (!layer.has_view) {
    Log::info("Sketch: this layer has no view yet (its first stroke on the picture plane remembers the camera)");
    return false;
  }
  cam_.ortho = false;
  cam_.fov = layer.view_fov;
  cam_.animate_to(layer.view_pivot, layer.view_yaw, layer.view_pitch, layer.view_dist, clock_now());
  ui_.redraw = true;
  return true;
}

/* ------------------------------------------------------------- corrections in the plane */

/* Pixels per metre on the view round a point of a scene canvas: along the plane's axis the view foreshortens most
 * (a reach in pixels must hold in metres along either axis). */
float Editor::sketch_px_per_metre(const SketchCanvas &c, Vec2 uv) const {
  const Vec3 p = c.origin + c.u * uv.x + c.v * uv.y;
  Vec2 a, bu, bv;
  float za, zu, zv;
  if (!scene_r3d_.project(p, a, za) || !scene_r3d_.project(p + normalize(c.u) * 0.1f, bu, zu) || !scene_r3d_.project(p + normalize(c.v) * 0.1f, bv, zv)) return 100.0f;
  return std::max(1.0f, std::min(length(bu - a), length(bv - a)) / 0.1f);
}

/* A stroke on a scene canvas, corrected in the plane (task 0226). The corners are found on the view, where the hand
 * drew them (a plane seen obliquely scales a pixel's wobble differently along the stroke, which a search on the
 * plane took for corners); then each piece's pen points are sent through the view's rays onto the plane and the
 * piece is fitted there, in the plane's metres scaled to the view's pixels at the piece - so a half circle drawn in
 * perspective is an arc on the plane (on the view it is an ellipse's arc), a circle a circle. The pieces chain as on
 * the picture plane; a line's free ends join other strokes' ends on the canvas; a line along the plane's axes or a
 * construction line lies on it. The symmetry guide is the view's and does not apply here. */
void Editor::sketch_stroke_end_on_plane(const Recti &view, const SketchCanvas &canvas, int level, float mean_w, const SketchStroke &base) {
  auto &g = sketch_;
  Sketch &s = scene_->sketch;
  /* The pen's points on the plane, with their place along the pen's path. */
  std::vector<Vec2> uv;
  std::vector<float> along, pressure;
  float walked = 0.0f;
  for (size_t i = 0; i < g.pen.size(); i++) {
    if (i > 0) walked += length(g.pen[i] - g.pen[i - 1]);
    Vec2 h;
    if (!sketch_plane_hit(view, canvas, g.pen[i], h)) continue;
    uv.push_back(h), along.push_back(walked), pressure.push_back(g.pressure[i]);
  }
  if (uv.size() < 2) {
    Log::warn("Sketch: the stroke is off the canvas's plane (past its horizon)");
    return;
  }
  std::vector<SketchStroke> made;
  auto with = [&](const std::vector<Vec2> &pts, float w) {
    SketchStroke st = base;
    st.canvas = canvas.id;
    for (const Vec2 &p : pts) st.pts.push_back({p.x, p.y, w});
    return st;
  };
  if (level == 0) {
    SketchStroke st = with(uv, mean_w);
    st.kind = StrokeKind::Freehand;
    for (size_t i = 0; i < st.pts.size(); i++) st.pts[i].w = pressure[i];
    made.push_back(st);
  }
  else {
    /* The pieces on the view, each one's span along the pen's path by its ends. */
    const std::vector<sketch::Fit> on_view = sketch::correct(g.pen, level);
    auto place = [&](Vec2 p, float from) {  // (where along the pen's path a view point lies, looking on from `from`)
      float best = 1e30f, at = from, w2 = 0.0f;
      for (size_t i = 0; i < g.pen.size(); i++) {
        if (i > 0) w2 += length(g.pen[i] - g.pen[i - 1]);
        if (w2 < from - 1e-3f) continue;
        const float d = length(g.pen[i] - p);
        if (d < best) best = d, at = w2;
      }
      return at;
    };
    const bool whole_closed = on_view.size() == 1 && on_view[0].closed;
    std::vector<sketch::Fit> fits;
    float s0 = 0.0f;
    for (size_t k = 0; k < on_view.size(); k++) {
      const sketch::Fit &piece = on_view[k];
      const bool last = k + 1 == on_view.size();
      const float s1 = whole_closed || (last && on_view.size() > 1 && length(piece.pts.back() - on_view[0].pts.front()) < 1.0f) ? walked : place(piece.pts.back(), s0);
      std::vector<Vec2> plane;
      Vec2 end0, end1;
      const bool have0 = sketch_plane_hit(view, canvas, piece.pts.front(), end0), have1 = sketch_plane_hit(view, canvas, piece.pts.back(), end1);
      if (have0 && !whole_closed) plane.push_back(end0);
      for (size_t i = 0; i < uv.size(); i++)
        if (along[i] >= s0 - 1e-3f && along[i] <= s1 + 1e-3f) plane.push_back(uv[i]);
      if (have1 && !whole_closed) plane.push_back(end1);
      s0 = s1;
      if (plane.size() < 2) continue;
      /* The view decided the piece's kind, where the hand drew it: a line is its two ends on the plane; an arc or an
       * ellipse is fitted again on the plane - with room, since a plane seen obliquely stretches a pixel's wobble -
       * and is a curve when it won't fit; a curve or a freehand piece is the points as they are. */
      sketch::Fit f;
      if (piece.kind == StrokeKind::Line && have0 && have1) {
        f.kind = StrokeKind::Line;
        f.pts = {end0, end1};
      }
      else if (piece.kind == StrokeKind::Arc || piece.kind == StrokeKind::Ellipse) {
        const float ppm = sketch_px_per_metre(canvas, plane[plane.size() / 2]);
        float len_px = 0.0f;
        for (size_t i = 1; i < piece.pts.size(); i++) len_px += length(piece.pts[i] - piece.pts[i - 1]);
        const float tol = 2.0f * std::max(4.0f, std::min(14.0f, len_px * 0.04f)) * (level >= 2 ? 1.0f : 0.5f);
        std::vector<Vec2> px;
        for (const Vec2 &p : plane) px.push_back(p * ppm);
        f = sketch::fit_piece(px, tol, piece.closed, 3.0f);
        if (f.kind == StrokeKind::Line) f = sketch::fit_curve(px, 3.0f), f.closed = piece.closed;  // (the view saw it bend)
        for (Vec2 &p : f.pts) p = p / ppm;
        f.center = f.center / ppm, f.rx /= ppm, f.ry /= ppm;
      }
      else {
        f.kind = piece.kind;
        f.closed = piece.closed;
        f.pts = plane;
      }
      fits.push_back(f);
    }
    if (fits.empty()) return;
    const bool ring = fits.size() > 1 && !fits.front().closed && length(fits.front().pts.front() - fits.back().pts.back()) < 1.0f / sketch_px_per_metre(canvas, uv[0]);
    for (size_t i = 1; i < fits.size(); i++) {  // (moved whole to start where the piece before ends)
      const Vec2 d = fits[i - 1].pts.back() - fits[i].pts.front();
      for (Vec2 &p : fits[i].pts) p += d;
      fits[i].center += d;
    }
    if (ring) fits.back().pts.back() = fits.front().pts.front();
    for (const sketch::Fit &f : fits) {
      SketchStroke st = with(f.pts, mean_w);
      st.kind = f.kind;
      st.closed = f.closed;
      st.center = f.center, st.rx = f.rx, st.ry = f.ry, st.angle = f.angle, st.start = f.start, st.sweep = f.sweep;
      if (f.kind == StrokeKind::Freehand)
        for (size_t i = 0; i < st.pts.size(); i++) st.pts[i].w = pressure[std::min(pressure.size() - 1, i * pressure.size() / std::max<size_t>(1, st.pts.size()))];
      made.push_back(st);
    }
    for (size_t i = 0; i < uv.size(); i++) made[0].raw.push_back({uv[i].x, uv[i].y, pressure[i]});
    /* A line's free ends (the chain's first start and last end) onto other strokes' ends on this canvas, within 8 px
     * of the view. */
    const float reach = (float)ui_.px(8) / sketch_px_per_metre(canvas, uv[uv.size() / 2]);
    if (!ring && !made.front().closed) {
      SketchPoint *free_ends[2] = {made.front().kind == StrokeKind::Line ? &made.front().pts.front() : nullptr,
                                   made.back().kind == StrokeKind::Line ? &made.back().pts.back() : nullptr};
      for (SketchPoint *end : free_ends) {
        if (!end) continue;
        for (const SketchStroke &o : s.strokes) {
          if (o.canvas != canvas.id || o.pts.empty()) continue;
          for (const SketchPoint &e : {o.pts.front(), o.pts.back()})
            if (length(Vec2{e.x, e.y} - Vec2{end->x, end->y}) < reach) end->x = e.x, end->y = e.y;
        }
      }
    }
    sketch_construction_lines(made, reach);
    sketch_snap_to_plane_guides(canvas, made, reach);
  }
  for (SketchStroke &st : made) s.strokes.push_back(std::move(st));
  mark_changed(made.size() > 1 ? "Sketch Strokes" : "Sketch Stroke");
  ui_.redraw = true;
}

/* ------------------------------------------------------------------ project and trace */

/* The last picture-plane stroke that shows pushed onto the face under its middle, along the view: it becomes a stroke
 * on that face's canvas (a line drawn on the surface). The canvas the pen draws on stays as it was. */
bool Editor::sketch_project() {
  Sketch &s = scene_->sketch;
  const Recti view = scene_rect_;
  for (size_t i = s.strokes.size(); i-- > 0;) {
    SketchStroke &st = s.strokes[i];
    if (st.canvas != 0 || st.pts.empty() || !sketch_stroke_shown(st)) continue;
    if (st.layer >= 0 && st.layer < (int)s.layers.size() && sketch_layer_fade(s.layers[(size_t)st.layer]) <= 0.0f) continue;  // (faded out here)
    const SketchPoint &mid = st.pts[st.pts.size() / 2];
    const Vec2 px = sketch_to_px(view, {mid.x, mid.y});
    SketchCanvas c;
    if (!sketch_face_canvas(view, (int)px.x, (int)px.y, c)) {
      Log::warn("sketchproject: no face under the stroke's middle");
      return false;
    }
    std::vector<SketchPoint> on;
    for (const SketchPoint &p : st.pts) {
      Vec2 uv;
      if (!sketch_plane_hit(view, c, sketch_to_px(view, {p.x, p.y}), uv)) break;
      on.push_back({uv.x, uv.y, p.w});
    }
    if (on.size() != st.pts.size()) {
      Log::warn("sketchproject: the stroke leaves the face's plane");
      return false;
    }
    const int active = s.active_canvas;
    const int id = sketch_add_canvas(c);
    s.active_canvas = active;
    st.pts = on;
    st.canvas = id;
    st.raw.clear();
    st.center = {}, st.rx = st.ry = 0.0f;
    if (st.kind == StrokeKind::Arc || st.kind == StrokeKind::Ellipse) st.kind = StrokeKind::Curve;
    mark_changed("Project Stroke");
    Log::info("Stroke projected onto %s", s.canvas_at(id)->name.c_str());
    return true;
  }
  Log::warn("sketchproject: draw a stroke on the picture plane over the model first (sketchcanvas view)");
  return false;
}

/* The face of a mesh a canvas was made from, as it is now: the stored face when it still lies in the canvas's plane
 * and holds the point, else the face in that plane that holds it (faces are renumbered by cuts, objects move). -1
 * when none does. */
long Editor::sketch_face_on_canvas(const Mesh &m, const Mat4 &w, const SketchCanvas &c, Vec3 at) const {
  const Vec3 n = normalize(cross(c.u, c.v));
  auto holds = [&](size_t f) {
    const uint32_t *fv = m.face_verts(f);
    const uint32_t nf = m.face_size(f);
    if (nf < 3) return false;
    const Vec3 fn = normalize(w.dir(m.face_normal(f)));
    if (std::fabs(dot(fn, n)) < 0.999f) return false;
    if (std::fabs(dot(w.point(m.positions[fv[0]]) - c.origin, n)) > 1e-3f * std::max(1.0f, length(at))) return false;
    /* Inside the polygon, in the plane's frame (the crossing rule). */
    auto uvof = [&](Vec3 p) { return Vec2{dot(p - c.origin, c.u) / std::max(1e-9f, dot(c.u, c.u)), dot(p - c.origin, c.v) / std::max(1e-9f, dot(c.v, c.v))}; };
    const Vec2 q = uvof(at);
    bool inside = false;
    for (uint32_t i = 0, j = nf - 1; i < nf; j = i++) {
      const Vec2 a = uvof(w.point(m.positions[fv[i]])), b = uvof(w.point(m.positions[fv[j]]));
      if ((a.y > q.y) != (b.y > q.y) && q.x < (b.x - a.x) * (q.y - a.y) / (b.y - a.y) + a.x) inside = !inside;
    }
    return inside;
  };
  if (c.face >= 0 && (size_t)c.face < m.face_count() && holds((size_t)c.face)) return c.face;
  for (size_t f = 0; f < m.face_count(); f++)
    if (holds(f)) return (long)f;
  return -1;
}

/* The last stroke on a face canvas: as a knife cut (a closed outline - the stroke's or the chain it closes -
 * imprinted as a face inside the face; a line across the face, split where it meets the face's edges), or as a Curve
 * object through its points. */
bool Editor::sketch_trace(bool as_curve) {
  Sketch &s = scene_->sketch;
  for (size_t i = s.strokes.size(); i-- > 0;) {
    SketchStroke &st = s.strokes[i];
    const SketchCanvas *c = st.canvas ? s.canvas_at(st.canvas) : nullptr;
    if (!c || c->kind != 1 || st.pts.size() < 2) continue;
    std::vector<SketchPoint> outline;
    const bool closed = sketch_outline_from(i, outline);
    std::vector<Vec3> world;
    for (const SketchPoint &p : closed ? outline : st.pts) world.push_back(c->origin + c->u * p.x + c->v * p.y);
    if (as_curve) return sketch_trace_curve(world, closed);
    GameObject *g = c->object ? scene_->find(c->object) : nullptr;
    MeshFilter *mf = g ? g->get<MeshFilter>() : nullptr;
    if (!mf || !mf->mesh) {
      Log::warn("sketchtrace edge: the stroke must lie on a face canvas (sketchcanvas face, or sketchproject)");
      return false;
    }
    Vec3 mid(0.0f);
    for (const Vec3 &p : world) mid += p;
    const long face = sketch_face_on_canvas(*mf->mesh, g->world_matrix(), *c, mid / (float)world.size());
    if (face < 0) {
      Log::warn("sketchtrace edge: no face of %s lies under the stroke any more", g->name.c_str());
      return false;
    }
    return sketch_trace_cut(*g, (size_t)face, world, closed);
  }
  Log::warn("sketchtrace: no stroke on a scene canvas");
  return false;
}

bool Editor::sketch_trace_curve(const std::vector<Vec3> &world, bool closed) {
  if (edit_mode_) exit_edit_mode();
  GameObject *g = scene_->create("Traced Curve");
  Curve *cv = g->add<Curve>();
  cv->type = 2;  // (Poly: through the points as drawn)
  cv->closed = closed;
  g->set_world_position(world.front());
  for (const Vec3 &p : world) {
    if ((int)cv->points.size() >= Curve::kMaxPoints) break;
    cv->points.push_back({p - world.front(), 0.0f, 1.0f});
  }
  select(g->id);
  mark_changed("Trace into Curve");
  Log::info("Traced into a curve of %d points", (int)cv->points.size());
  return true;
}

/* A line across a face cut in: where the line (in the face's plane) meets the face's outline, two new vertices and
 * the face split between them - of the outline's crossings taken in pairs (a notched face has several spans), the
 * pair round the stroke's middle; a closed outline imprinted inside the face. The mesh is as it was when a cut
 * fails. */
bool Editor::sketch_trace_cut(GameObject &g, size_t face, const std::vector<Vec3> &world, bool closed) {
  select(g.id);
  if (!edit_mode_ || edit_obj_ != g.id) {
    if (edit_mode_) exit_edit_mode();
    enter_edit_mode();
  }
  MeshPtr *mp = edit_mesh_ptr();
  if (!mp || !*mp || face >= (*mp)->face_count()) return false;
  const Mat4 inv = g.world_matrix().inverse();
  std::vector<Vec3> local;
  for (const Vec3 &p : world) local.push_back(inv.point(p));
  Mesh &m = *mesh_make_mutable(*mp);
  const Mesh before = m;
  auto failed = [&](const char *why) {
    m = before;
    Log::warn("sketchtrace edge: %s", why);
    return false;
  };
  if (closed && local.size() >= 3) {
    std::string err;
    const long inner = meshops::imprint_loop(m, face, local, &err);
    if (inner < 0) return failed(err.empty() ? "the outline could not be imprinted" : err.c_str());
    m.touch();
    vert_sel_.assign(m.vert_count(), 0);
    face_sel_.assign(m.face_count(), 0);
    face_sel_[(size_t)inner] = 1;
    elem_ = EditElement::Face;
    mark_changed("Trace into Cut");
    Log::info("Outline imprinted as face %ld", inner);
    return true;
  }
  /* The line's crossings of the face's outline, in the face's plane. */
  const Vec3 a = local.front(), b = local.back();
  const Vec3 n = m.face_normal(face);
  Vec3 d = b - a;
  d = d - n * dot(d, n);
  if (length(d) < 1e-6f) return failed("the stroke has no length across the face");
  d = normalize(d);
  const Vec3 side = normalize(cross(n, d));  // (across the line, in the plane)
  const uint32_t *fv = m.face_verts(face);
  const uint32_t nf = m.face_size(face);
  struct Hit {
    float along;
    uint32_t va, vb;
    float t;
  };
  std::vector<Hit> hits;
  for (uint32_t k = 0; k < nf; k++) {
    const uint32_t va = fv[k], vb = fv[(k + 1) % nf];
    const float sa = dot(m.positions[va] - a, side), sb = dot(m.positions[vb] - a, side);
    if ((sa < 0) == (sb < 0)) continue;
    const float t = sa / (sa - sb);
    const Vec3 p = m.positions[va] + (m.positions[vb] - m.positions[va]) * t;
    hits.push_back({dot(p - a, d), va, vb, t});
  }
  if (hits.size() < 2) return failed("the line does not cross the face");
  std::sort(hits.begin(), hits.end(), [](const Hit &x, const Hit &y) { return x.along < y.along; });
  /* The crossings pair off into the spans of the face along the line; the span round the stroke's middle. */
  const float mid = dot(b - a, d) * 0.5f;
  size_t pair = 0;
  float best = 1e30f;
  for (size_t k = 0; k + 1 < hits.size(); k += 2) {
    const float gap = mid < hits[k].along ? hits[k].along - mid : mid > hits[k + 1].along ? mid - hits[k + 1].along : 0.0f;
    if (gap < best) best = gap, pair = k;
  }
  auto vertex_at = [&](const Hit &h) {
    if (h.t < 1e-4f) return h.va;
    if (h.t > 1.0f - 1e-4f) return h.vb;
    return meshops::split_edge(m, h.va, h.vb, h.t);
  };
  const Hit h1 = hits[pair], h2 = hits[pair + 1];
  const uint32_t v1 = vertex_at(h1), v2 = vertex_at(h2);
  if (v1 == v2 || !meshops::split_face(m, face, v1, v2)) return failed("those points are already joined by an edge");
  m.touch();
  vert_sel_.assign(m.vert_count(), 0);
  face_sel_.assign(m.face_count(), 0);
  edge_sel_.clear();
  mark_changed("Trace into Cut");
  Log::info("The stroke cut across the face (%zu faces now)", m.face_count());
  return true;
}

/* ------------------------------------------------------------------ the overlay and the guides from the model */

/* The selected mesh objects' edges over the view, thin, to draw against (outside Edit Mode, which draws its own). */
void Editor::sketch_draw_overlay(const Recti &view) {
  if (!sketch_.active || !sketch_.overlay || edit_mode_) return;
  auto &u = ui_;
  const uint32_t col = Color::hex(0xFFFFFF, 90);
  const float px1 = (float)u.px(1.0f);
  size_t drawn = 0;
  for (uint64_t id : selection_) {
    const GameObject *g = scene_->find(id);
    const MeshFilter *mf = g ? g->get<MeshFilter>() : nullptr;
    if (!mf || !mf->mesh) continue;
    const Mesh &m = *mf->mesh;
    const Mat4 &w = g->world_matrix();
    for (const auto &e : m.edge_cache()) {
      if (drawn++ > 20000) return;  // (a dense mesh: the first edges only)
      Vec2 a, b;
      if (!sketch_segment_px(view, w.point(m.positions[e.first]), w.point(m.positions[e.second]), a, b)) continue;
      u.canvas.line(a.x, a.y, b.x, b.y, col, px1);
    }
  }
}

/* The nearest edge (within 10 px) of the mesh under a view point, or of the selected meshes: the ruler along it. */
bool Editor::sketch_ruler_from_edge(const Recti &view, int mx, int my) {
  ensure_scene_planes();
  const uint32_t id = scene_rt_.id_at(mx - view.x, my - view.y);
  std::vector<uint64_t> candidates;
  if (id && scene_->find(id)) candidates.push_back(id);
  for (uint64_t sel : selection_)
    if (sel != id) candidates.push_back(sel);
  const Vec2 mouse((float)(mx - view.x), (float)(my - view.y));
  float best = (float)ui_.px(10);
  Vec2 pa, pb;
  bool have = false;
  for (uint64_t cid : candidates) {
    const GameObject *g = scene_->find(cid);
    const MeshFilter *mf = g ? g->get<MeshFilter>() : nullptr;
    if (!mf || !mf->mesh) continue;
    const Mesh &m = *mf->mesh;
    const Mat4 &w = g->world_matrix();
    for (const auto &e : m.edge_cache()) {
      Vec2 sa, sb;
      float za, zb;
      if (!scene_r3d_.project(w.point(m.positions[e.first]), sa, za) || !scene_r3d_.project(w.point(m.positions[e.second]), sb, zb)) continue;
      const Vec2 d = sb - sa;
      const float l2 = dot(d, d);
      const float t = l2 > 1e-6f ? std::max(0.0f, std::min(1.0f, dot(mouse - sa, d) / l2)) : 0.0f;
      const float dist = length(sa + d * t - mouse);
      if (dist < best && l2 > 1.0f) best = dist, pa = sa, pb = sb, have = true;
    }
  }
  if (!have) return false;
  sketch_.guide = 1;
  sketch_.ruler[0] = sketch_to_canvas(view, pa + Vec2{(float)view.x, (float)view.y});
  sketch_.ruler[1] = sketch_to_canvas(view, pb + Vec2{(float)view.x, (float)view.y});
  sketch_.placed = true;
  return true;
}

/* The face under a view point with eight or more corners round a circle: the ellipse guide on its picture. */
bool Editor::sketch_ellipse_from_face(const Recti &view, int mx, int my) {
  SketchCanvas c;
  if (!sketch_face_canvas(view, mx, my, c)) return false;
  const GameObject *g = scene_->find(c.object);
  const MeshFilter *mf = g ? g->get<MeshFilter>() : nullptr;
  if (!mf || !mf->mesh || c.face < 0) return false;
  const Mesh &m = *mf->mesh;
  const uint32_t nf = m.face_size((size_t)c.face);
  if (nf < 8) {
    Log::warn("sketchguide ellipse face: that face has %u corners, not a circle's", nf);
    return false;
  }
  const Mat4 &w = g->world_matrix();
  const uint32_t *fv = m.face_verts((size_t)c.face);
  std::vector<Vec2> pts;
  Vec2 mean{0, 0};
  for (uint32_t i = 0; i < nf; i++) {
    Vec2 s;
    float z;
    if (!scene_r3d_.project(w.point(m.positions[fv[i]]), s, z)) return false;
    pts.push_back(s + Vec2{(float)view.x, (float)view.y});
    mean += pts.back();
  }
  mean = mean / (float)nf;
  float radius = 0.0f;
  for (const Vec2 &p : pts) radius += length(p - mean);
  radius /= (float)nf;
  sketch::Fit f;
  if (!sketch::fit_ellipse(pts, std::max(2.0f, radius * 0.05f), f)) {  // (a face that is no ellipse's picture)
    Log::warn("sketchguide ellipse face: that face's corners are not on an ellipse");
    return false;
  }
  sketch_.guide = 2;
  sketch_.ell_c = sketch_to_canvas(view, f.center);
  sketch_.ell_rx = f.rx / (float)std::max(1, view.h), sketch_.ell_ry = f.ry / (float)std::max(1, view.h), sketch_.ell_angle = f.angle;
  sketch_.placed = true;
  return true;
}

/* sketchproject | sketchtrace edge|curve: the drawing into the model. */
bool Editor::sketch_refine_command(const std::vector<std::string> &t) {
  if (t.empty()) return false;
  const std::string c = to_lower(t[0]);
  if (c != "sketchproject" && c != "sketchtrace") return false;
  if (playing_) return true;
  if (c == "sketchproject") sketch_project();
  else {
    const std::string v = t.size() > 1 ? to_lower(t[1]) : "edge";
    if (v == "edge" || v == "cut") sketch_trace(false);
    else if (v == "curve") sketch_trace(true);
    else Log::info("usage: sketchtrace edge|curve");
  }
  ui_.redraw = true;
  return true;
}

}  // namespace bl

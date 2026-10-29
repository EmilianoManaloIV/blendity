// SPDX-License-Identifier: GPL-2.0-or-later
// The Strokes tool (ADR 0084, task 0248): a brush that paints geometry. A dab on a surface adds a card of the Stroke
// object (a quad facing the camera, the surface's normal kept for shading); a drag with the ribbon kind sweeps its
// points into a strip at the release. Each stroke is one undo step: the component's list changes and the object's mesh
// is rebuilt from it (a small mesh: copy on write stores it with the step).
#include "editor.h"

#include "../core/core.h"
#include "../scene/raycast.h"
#include "../scene/stroke_object.h"

#include <cmath>

namespace bl {

void Editor::set_strokeobj(bool on) {
  if (on == strokeobj_) return;
  if (on) {
    if (paint_) set_paint(false);
    if (sculpt_) set_sculpt(false);
    if (scatter_) set_scatter(false);
  }
  else strokeobj_ribbon_end();
  strokeobj_ = on;
  strokeobj_stroking_ = false;
  ui_.redraw = true;
  Log::info("Strokes: %s", on ? "on - a click paints a card, a drag a ribbon (Kind: Modeling Tools > Strokes; Esc leaves)" : "off");
}

/* The Stroke object edited: the one chosen (or the active object when it has a Strokes component), else a new one at the
 * origin when `make`. */
GameObject *Editor::strokeobj_object(bool make) {
  GameObject *g = strokeobj_id_ ? scene_->find(strokeobj_id_) : nullptr;
  if (g && g->get<Strokes>()) return g;
  GameObject *a = active_object();
  if (a && a->get<Strokes>()) {
    strokeobj_id_ = a->id;
    return a;
  }
  if (!make) return nullptr;
  std::string name = "Strokes";
  for (int k = 1; scene_->find_by_name(name); k++) name = strprintf("Strokes.%03d", k);
  g = scene_->create(name);
  g->set_local_position(Vec3()), g->set_local_rotation(Quat()), g->set_local_scale(Vec3(1.0f));  // (at the origin)
  g->add<Strokes>();
  g->add<MeshFilter>()->mesh = std::make_shared<Mesh>();
  MeshRenderer *mr = g->add<MeshRenderer>();
  MaterialPtr m = make_material("Strokes", Vec3(0.35f, 0.55f, 0.25f));  // (single-sided: every card has its back face, with the same custom normal)
  if (!strokeobj_alpha_.empty()) m->base_map = TextureRef{strokeobj_alpha_, false}, m->surface = (int)MaterialSurface::Cutout;
  mr->materials.push_back(m);
  strokeobj_id_ = g->id;
  return g;
}

/* The surface under a view pixel (any rendered mesh but Stroke objects: strokes go on surfaces, not on each other): the
 * point, its normal and the facing (toward the camera along the pixel's ray). */
bool Editor::strokeobj_hit(const Recti &, float x, float y, Vec3 &point, Vec3 &normal, Vec3 &facing) {
  const Ray ray = scene_r3d_.screen_ray(x, y);
  SceneHit hit;
  const uint64_t self = strokeobj_id_;
  if (!scene_raycast(*scene_, ray.origin, ray.dir, 1e5f, hit, [&](const GameObject &g) { return g.id == self || g.get<Strokes>() != nullptr; })) return false;
  point = hit.point, normal = hit.normal, facing = normalize(ray.dir) * -1.0f;
  return true;
}

void Editor::strokeobj_rebuild(GameObject &g, const char *label) {
  Strokes *s = g.get<Strokes>();
  MeshFilter *mf = g.mesh_filter();
  if (!s || !mf) return;
  mf->mesh = s->build_mesh();
  mark_changed(label);
}

/* A card at the dab. */
bool Editor::strokeobj_dab(const Recti &view, float x, float y) {
  Vec3 p, n, f;
  if (!strokeobj_hit(view, x, y, p, n, f)) return false;
  GameObject *g = strokeobj_object(true);
  Strokes *s = g->get<Strokes>();
  const Mat4 inv = g->world_matrix().inverse();
  StrokeCard c;
  c.center = inv.point(p + n * 0.002f);  // (a hair off the surface: no z-fight with it)
  c.facing = normalize(inv.point(p + f) - inv.point(p));
  c.surface_normal = normalize(inv.point(p + n) - inv.point(p));
  c.up = Vec3(0.0f, 1.0f, 0.0f);
  c.size = std::max(0.001f, strokeobj_size_);
  s->cards.push_back(c);
  strokeobj_rebuild(*g, "Stroke Card");
  return true;
}

/* A ribbon's point while the drag goes on (spaced by a quarter of the width). */
void Editor::strokeobj_ribbon_point(const Recti &view, float x, float y) {
  Vec3 p, n, f;
  if (!strokeobj_hit(view, x, y, p, n, f)) return;
  if (strokeobj_points_.empty()) strokeobj_facing_ = f, strokeobj_surface_n_ = n;
  else if (length(p - strokeobj_points_.back()) < std::max(0.001f, strokeobj_width_) * 0.25f) return;
  strokeobj_points_.push_back(p + n * 0.002f);
}

/* The drag's end: the points become a ribbon (two or more). */
void Editor::strokeobj_ribbon_end() {
  std::vector<Vec3> pts = std::move(strokeobj_points_);
  strokeobj_points_.clear();
  if (pts.size() < 2) return;
  GameObject *g = strokeobj_object(true);
  Strokes *s = g->get<Strokes>();
  const Mat4 inv = g->world_matrix().inverse();
  StrokeRibbon rb;
  rb.width = std::max(0.001f, strokeobj_width_);
  rb.facing = normalize(inv.point(pts[0] + strokeobj_facing_) - inv.point(pts[0]));
  rb.surface_normal = normalize(inv.point(pts[0] + strokeobj_surface_n_) - inv.point(pts[0]));
  for (const Vec3 &p : pts) rb.points.push_back(inv.point(p));
  s->ribbons.push_back(std::move(rb));
  strokeobj_rebuild(*g, "Stroke Ribbon");
}

/* Make Canopy: `count` cards on a shell of `radius` round `centre` (world), each facing out from the centre, the
 * object's Normals set to Canopy there - a bush or a tree's crown from flat cards (the hand-painted foliage trick). */
int Editor::strokeobj_canopy_fill(Vec3 centre, float radius, int count) {
  GameObject *g = strokeobj_object(true);
  if (!g->get<Strokes>()->cards.empty() || !g->get<Strokes>()->ribbons.empty()) strokeobj_id_ = 0, g = strokeobj_object(true);  // (a canopy has its own centre: its own object)
  Strokes *s = g->get<Strokes>();
  const Mat4 inv = g->world_matrix().inverse();
  const int n = std::max(1, std::min(20000, count));
  const float size = std::max(0.01f, radius * 2.2f / std::sqrt((float)n));  // (cards sized to cover the shell about twice over)
  for (int i = 0; i < n; i++) {  // a Fibonacci sphere: even over the shell
    const float t = (i + 0.5f) / (float)n, phi = std::acos(1.0f - 2.0f * t), theta = 3.883222077f * i;
    const Vec3 d(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
    StrokeCard c;
    c.center = inv.point(centre + d * radius);
    c.facing = normalize(inv.point(centre + d * (radius + 1.0f)) - c.center);
    c.surface_normal = c.facing;
    c.up = Vec3(0.0f, 1.0f, 0.0f);
    c.size = size;
    s->cards.push_back(c);
  }
  s->canopy = inv.point(centre);
  s->normals = 2;
  strokeobj_rebuild(*g, "Make Canopy");
  return n;
}

/* Grass: upright cards of `height` dropped onto the surface under the view point, `density` a square metre of the
 * disc of `radius`, kept off a Paint layer's painted texels when the scatter mask is set; their normals point up
 * (the canopy at the centre far below), so a patch shades as one. */
int Editor::strokeobj_grass(const Recti &view, float x, float y, float density, float height, float radius) {
  Vec3 p, n, f;
  if (!strokeobj_hit(view, x, y, p, n, f)) return 0;
  const Ray ray = scene_r3d_.screen_ray(x, y);
  SceneHit hit;
  const uint64_t self = strokeobj_id_;
  auto skip = [&](const GameObject &g) { return g.id == self || g.get<Strokes>() != nullptr; };
  if (!scene_raycast(*scene_, ray.origin, ray.dir, 1e5f, hit, skip)) return 0;
  GameObject *surface_obj = const_cast<GameObject *>(hit.object);
  GameObject *g = strokeobj_object(true);
  if (g->get<Strokes>()->normals != 2 && (!g->get<Strokes>()->cards.empty() || !g->get<Strokes>()->ribbons.empty())) strokeobj_id_ = 0, g = strokeobj_object(true);  // (grass points up: its own object unless this one already does)
  Strokes *s = g->get<Strokes>();
  const Mat4 inv = g->world_matrix().inverse();
  const Vec3 nn = normalize(n);
  Vec3 tangent = std::fabs(nn.y) < 0.9f ? normalize(cross(Vec3(0.0f, 1.0f, 0.0f), nn)) : Vec3(1.0f, 0.0f, 0.0f);
  const Vec3 bitangent = cross(nn, tangent);
  const float r = std::max(0.01f, radius), area = 3.14159265f * r * r;
  const double asked = (double)std::max(0.0f, density) * area;
  if (asked > 20000.0) {
    Log::warn("strokeobj grass: %.0f blades asked for one dab (the cap is 20,000: a smaller radius or density)", asked);
    return 0;
  }
  const int want = std::max(1, (int)std::lround(asked));
  int placed = 0;
  for (int i = 0; i < want; i++) {
    const float a = i * 2.399963f, rr = std::sqrt((i + 0.5f) / (float)want) * r;  // (the golden angle: even, no rows)
    const Vec3 q = p + tangent * (std::cos(a) * rr) + bitangent * (std::sin(a) * rr);
    SceneHit drop;
    if (!scene_raycast(*scene_, q + nn * (r * 2.0f), nn * -1.0f, r * 4.0f, drop, skip) || drop.object != hit.object) continue;
    if (scatter_masked(*surface_obj, drop.point)) continue;
    StrokeCard c;
    const float h = std::max(0.01f, height);
    c.center = inv.point(drop.point + Vec3(0.0f, h * 0.5f, 0.0f));
    const float yaw = a * 0.5f;
    c.facing = normalize(inv.point(drop.point + Vec3(std::cos(yaw), 0.0f, std::sin(yaw))) - inv.point(drop.point));
    c.surface_normal = normalize(inv.point(drop.point + drop.normal) - inv.point(drop.point));
    c.up = Vec3(0.0f, 1.0f, 0.0f);
    c.size = h;
    s->cards.push_back(c);
    placed++;
  }
  if (!placed) return 0;
  s->canopy = inv.point(p - Vec3(0.0f, 1000.0f, 0.0f));  // (far below: every blade's normal points up; a canopy of its own object keeps its centre)
  s->normals = 2;
  strokeobj_rebuild(*g, "Grass");
  return placed;
}

bool Editor::strokeobj_update(const Recti &view) {
  if (!strokeobj_) return false;
  auto &in = ui_.in;
  const float x = (float)(in.mx - view.x), y = (float)(in.my - view.y);
  if (!strokeobj_stroking_) {
    if (scene_hovered_ && in.key_pressed[platform::KEY_ESCAPE] && !ui_.wants_keyboard()) {
      set_strokeobj(false);
      in.key_pressed[platform::KEY_ESCAPE] = false;
      return true;
    }
    if (!(scene_hovered_ && in.pressed[0] && !ui_.any_active() && !qm_.open)) return false;
    Vec3 p, n, f;
    if (!strokeobj_hit(view, x, y, p, n, f)) return false;  // (over nothing: the click is the view's)
    ui_.consume_click();
    strokeobj_stroking_ = true;
    strokeobj_last_px_ = {x, y};
    if (strokeobj_kind_ == 0) strokeobj_dab(view, x, y);
    else strokeobj_points_.clear(), strokeobj_ribbon_point(view, x, y);
    return true;
  }
  if (in.down[0]) {
    if (strokeobj_kind_ == 1) strokeobj_ribbon_point(view, x, y);
    else {  // cards along the drag, one a size's worth of pixels
      const float step = std::max(4.0f, strokeobj_size_ * 200.0f * ui_.scale);
      if (length(Vec2(x, y) - strokeobj_last_px_) >= step) strokeobj_dab(view, x, y), strokeobj_last_px_ = {x, y};
    }
    return true;
  }
  strokeobj_stroking_ = false;
  if (strokeobj_kind_ == 1) strokeobj_ribbon_end();
  return true;
}

void Editor::strokeobj_draw(const Recti &view) {
  if (!strokeobj_ || !scene_hovered_) return;
  auto &u = ui_;
  const Vec2 c((float)u.in.mx, (float)u.in.my);
  if (c.x < view.x || c.y < view.y || c.x > view.right() || c.y > view.bottom()) return;
  const float r = std::max(3.0f, (strokeobj_kind_ == 0 ? strokeobj_size_ : strokeobj_width_) * 100.0f * u.scale);
  u.canvas.circle(c.x, c.y, r, u.theme.accent);
  if (strokeobj_points_.size() >= 2)
    for (size_t i = 1; i < strokeobj_points_.size(); i++) {
      Vec2 a, b;
      float da, db;
      if (scene_r3d_.project(strokeobj_points_[i - 1], a, da) && scene_r3d_.project(strokeobj_points_[i], b, db)) u.canvas.line(a.x + view.x, a.y + view.y, b.x + view.x, b.y + view.y, u.theme.accent);
    }
}

/* strokeobj on|off | new | card | ribbon | size s | width w | normals own|surface|canopy | canopy [x y z] | alpha <image>|off |
 * dab x y | stroke x0 y0 x1 y1 [n] | count */
void Editor::strokeobj_command(const std::vector<std::string> &t) {
  auto arg = [&](size_t i, const std::string &d) { return t.size() > i ? to_lower(t[i]) : d; };
  auto num = [&](size_t i, float d) { return t.size() > i ? (float)std::atof(t[i].c_str()) : d; };
  const std::string v = arg(1, "on");
  if (v == "on" || v == "off") set_strokeobj(v == "on");
  else if (v == "new") {
    strokeobj_id_ = 0;
    select(strokeobj_object(true)->id);
    mark_changed("New Stroke Object");
  }
  else if (v == "card") strokeobj_kind_ = 0;
  else if (v == "ribbon") strokeobj_kind_ = 1;
  else if (v == "size") strokeobj_size_ = std::max(0.001f, std::min(100.0f, num(2, 0.2f)));
  else if (v == "width") strokeobj_width_ = std::max(0.001f, std::min(100.0f, num(2, 0.05f)));
  else if (v == "normals" || v == "canopy") {
    GameObject *g = strokeobj_object(true);
    Strokes *s = g->get<Strokes>();
    if (v == "normals") {
      const std::string w = arg(2, "own");
      s->normals = w == "surface" ? 1 : w == "canopy" ? 2 : 0;
    }
    else {
      if (t.size() >= 5) s->canopy = Vec3(num(2, 0.0f), num(3, 0.0f), num(4, 0.0f));
      else {  // the centre of the cards and ribbon points
        Vec3 sum;
        size_t n = 0;
        for (const StrokeCard &c : s->cards) sum += c.center, n++;
        for (const StrokeRibbon &rb : s->ribbons)
          for (const Vec3 &p : rb.points) sum += p, n++;
        if (n) s->canopy = sum / (float)n;
      }
      s->normals = 2;
    }
    strokeobj_rebuild(*g, "Stroke Normals");
  }
  else if (v == "alpha") {
    strokeobj_alpha_ = arg(2, "off") == "off" ? std::string() : t[2];
    GameObject *g = strokeobj_object(false);
    MeshRenderer *mr = g ? g->get<MeshRenderer>() : nullptr;
    if (mr && !mr->materials.empty() && mr->materials[0]) {
      Material &m = *mr->materials[0];
      m.base_map = strokeobj_alpha_.empty() ? TextureRef{} : TextureRef{strokeobj_alpha_, false};
      m.surface = strokeobj_alpha_.empty() ? (int)MaterialSurface::Opaque : (int)MaterialSurface::Cutout;
      m.touch();
      mark_changed("Stroke Alpha");
    }
  }
  else if (v == "dab") {
    if (!strokeobj_) set_strokeobj(true);
    if (!strokeobj_dab(scene_rect_, num(2, 0.0f), num(3, 0.0f))) Log::warn("strokeobj dab: nothing under (%g, %g)", num(2, 0.0f), num(3, 0.0f));
  }
  else if (v == "stroke") {  // a ribbon along a line of the view
    if (!strokeobj_) set_strokeobj(true);
    const float x0 = num(2, 0.0f), y0 = num(3, 0.0f), x1 = num(4, x0), y1 = num(5, y0);
    const int n = std::max(1, std::min(256, (int)num(6, 16.0f)));
    strokeobj_points_.clear();
    for (int i = 0; i <= n; i++) strokeobj_ribbon_point(scene_rect_, x0 + (x1 - x0) * i / (float)n, y0 + (y1 - y0) * i / (float)n);
    const size_t pts = strokeobj_points_.size();
    strokeobj_ribbon_end();
    if (pts < 2) Log::warn("strokeobj stroke: fewer than two points landed on a surface");
  }
  else if (v == "canopyfill") {  // strokeobj canopyfill [radius] [count] [x y z]: a shell of cards round the canopy centre (or the point given)
    GameObject *g = strokeobj_object(true);
    const Strokes *s = g->get<Strokes>();
    Vec3 centre = g->world_matrix().point(s->canopy);
    if (t.size() >= 7) centre = Vec3(num(4, 0.0f), num(5, 0.0f), num(6, 0.0f));
    const int n = strokeobj_canopy_fill(centre, std::max(0.01f, num(2, 1.0f)), (int)num(3, 200.0f));
    Log::info("Strokes: a canopy of %d cards round (%.2f, %.2f, %.2f)", n, centre.x, centre.y, centre.z);
  }
  else if (v == "grass") {  // strokeobj grass [density] [height] [radius] [x y]: upright cards on the surface under the view point
    if (!strokeobj_) set_strokeobj(true);
    const float density = num(2, 40.0f), height = num(3, 0.25f), radius = num(4, 1.0f);
    const float x = t.size() > 6 ? num(5, 0.0f) : scene_rect_.w * 0.5f, y = t.size() > 6 ? num(6, 0.0f) : scene_rect_.h * 0.5f;
    const int n = strokeobj_grass(scene_rect_, x, y, density, height, radius);
    if (!n) Log::warn("strokeobj grass: nothing under (%g, %g), or every blade masked", x, y);
    else Log::info("Strokes: %d blade(s) of grass", n);
  }
  else if (v == "count") {
    GameObject *g = strokeobj_object(false);
    const Strokes *s = g ? g->get<Strokes>() : nullptr;
    Log::info("Strokes: %zu card(s), %zu ribbon(s)%s", s ? s->cards.size() : (size_t)0, s ? s->ribbons.size() : (size_t)0, s ? "" : " (no Stroke object yet)");
  }
  else Log::warn("strokeobj on|off | new | card | ribbon | size s | width w | normals own|surface|canopy | canopy [x y z] | canopyfill [r] [n] [x y z] | grass [density] [height] [radius] [x y] | alpha <image>|off | dab x y | stroke x0 y0 x1 y1 [n] | count");
}

/* Task 0264: the cards of the scene's Stroke object, for the tests. */
size_t Editor::strokeobj_cards_for_test() {
  GameObject *g = strokeobj_object(false);
  const Strokes *s = g ? g->get<Strokes>() : nullptr;
  return s ? s->cards.size() : 0;
}

}  // namespace bl

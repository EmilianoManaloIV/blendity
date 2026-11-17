// SPDX-License-Identifier: GPL-2.0-or-later
// Snap to Object Origins (round 535, task 0515; Blender 5.3 added the same snap target): an object's origin - its pivot
// point, the one the Move gizmo and the Hierarchy show - becomes something the Move gizmo's V snap, a modal G with Ctrl
// and the drawing tools can land on. Off by default (`snap origins`, Tools > Snapping), so nothing else changes. One
// projection per object, run only with the setting on and the snap's key held.
#include "editor.h"

#include <algorithm>
#include <unordered_set>

namespace bl {

namespace {
void collect_subtree(GameObject *g, std::unordered_set<uint64_t> &out) {
  out.insert(g->id);
  for (GameObject *c : g->children) collect_subtree(c, out);
}
}  // namespace

/* `snap origins [on|off]`: no value toggles; it says where it stands. */
void Editor::snap_command(const std::vector<std::string> &t) {
  if (t.size() < 2 || to_lower(t[1]) != "origins") {
    Log::warn("snap: 'snap origins [on|off]'");
    return;
  }
  const std::string v = t.size() > 2 ? to_lower(t[2]) : std::string();
  const bool on = v == "on" || v == "1" || v == "true", off = v == "off" || v == "0" || v == "false";
  if (!v.empty() && !on && !off) {  // (a typo leaves it as it was rather than switching it off)
    Log::warn("snap origins: '%s' is not on or off - still %s", v.c_str(), snap_origins_ ? "on" : "off");
    return;
  }
  snap_origins_ = v.empty() ? !snap_origins_ : on;
  Log::info("Snap to Object Origins: %s", snap_origins_ ? "on" : "off");
}

bool Editor::nearest_origin_on_screen(const Recti &view, int mx, int my, float radius, bool skip_moving, Vec3 &out, bool selected_only,
                                      const std::function<bool(GameObject &, const Vec3 &)> &accept) {
  std::unordered_set<uint64_t> sel;
  if (skip_moving || selected_only)
    for (GameObject *g : selected_objects(false)) collect_subtree(g, sel);  // (a child of the selection moves along with it)
  const Vec2 mouse((float)(mx - view.x), (float)(my - view.y));
  float best = radius;
  bool found = false;
  scene_->for_each([&](GameObject &g) {
    if (!g.active_in_hierarchy() || blockout_uses_as_piece(g)) return;
    const bool is_sel = sel.count(g.id) > 0;
    if ((skip_moving && is_sel) || (selected_only && !is_sel)) return;
    const Vec3 p = g.world_position();
    Vec2 s;
    float z;
    if (!scene_r3d_.project(p, s, z)) return;  // (behind the camera never projects)
    const float d = length(s - mouse);
    if (d >= best || (accept && !accept(g, p))) return;
    best = d, out = p, found = true;
  });
  return found;
}

/* A vertex found by the V snap competes with the origins by their distance to the mouse on screen. */
void Editor::prefer_origin_target(const Recti &view, int mx, int my, float radius, bool anchor, bool skip_moving, bool &found, Vec3 &target) {
  if (!snap_origins_) return;
  const Vec2 mouse((float)(mx - view.x), (float)(my - view.y));
  float limit = radius;
  Vec2 s;
  float z;
  if (found && scene_r3d_.project(target, s, z)) limit = std::min(limit, length(s - mouse));
  Vec3 origin;
  if (!nearest_origin_on_screen(view, mx, my, limit, skip_moving, origin, anchor)) return;
  found = true;
  target = origin;
}

bool Editor::modal_origin_snap(const Recti &view, const Vec3 &lock_axis, Vec3 &move) {
  if (!snap_origins_) return false;
  ModalTransform &t = xf_;
  const float reach = (float)ui_.px(16);
  Vec3 origin;
  if (!nearest_origin_on_screen(view, ui_.in.mx, ui_.in.my, reach, !t.edit, origin)) return false;
  Vec3 delta = origin - t.pivot;
  if (t.axis >= 0 && !t.plane) delta = lock_axis * dot(delta, lock_axis);  // an axis lock keeps the part along it
  else if (t.axis >= 0) delta = delta - lock_axis * dot(delta, lock_axis);  // a plane lock, the part in the plane
  move = delta;
  return true;
}

/* An origin the eye sees: nothing between the eye and it. Another object's origin lies inside its own solid, so that
 * solid is left out of the ray; the edited object's is not - its origin seen through the front face being drawn on must
 * not pull the shape's first point inside the solid (review 535). */
bool Editor::origin_visible(GameObject &owner, Vec3 origin) {
  Vec2 s;
  float z;
  if (!scene_r3d_.project(origin, s, z)) return false;
  const Ray ray = scene_r3d_.screen_ray(s.x, s.y);
  const Vec3 dir = normalize(ray.dir);
  const float to_origin = dot(origin - ray.origin, dir);
  std::vector<GameObject *> skip;
  if (&owner != edit_object()) skip.push_back(&owner);
  Vec3 hit, n;
  if (!raycast_surface({ray.origin, dir}, hit, n, skip)) return true;
  return length(hit - ray.origin) >= to_origin - 1e-4f * std::max(1.0f, to_origin);
}

bool Editor::draw_origin_hit(const Recti &view, int mx, int my, DrawHit &h) {
  if (!snap_origins_) return false;
  /* The same tolerance Face Center uses: with a drawing plane set only an origin lying in it counts; and only one in sight. */
  auto usable = [&](GameObject &g, const Vec3 &o) {
    const bool in_plane = !draw_.has_plane || std::fabs(dot(o - draw_.plane_p, draw_.plane_n)) < 1e-4f * std::max(1.0f, length(o));
    return in_plane && origin_visible(g, o);
  };
  Vec3 origin;
  if (!nearest_origin_on_screen(view, mx, my, (float)ui_.px(10), false, origin, false, usable)) return false;
  h.ok = true;
  h.world = origin;
  h.label = "Object Origin";
  h.color = Color::hex(0xFF70D0);
  return true;
}

}  // namespace bl

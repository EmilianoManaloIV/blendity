// SPDX-License-Identifier: GPL-2.0-or-later
// Editing a bone's shape in the Scene view (task 0066): handles on the selected bone's head and tail (Blender's Edit
// Mode for armatures, without the mode), and Extrude (E) that grows a child bone from the tip and lets the mouse
// place its tail. A bone is its object's origin (the head) and +Y times its length (the tail); moving either end
// re-aims the bone, keeps its children where they are, and makes the result its rest (as Apply as Rest).
#include "editor.h"

#include "../scene/scene.h"

#include <algorithm>
#include <cmath>

namespace bl {

Vec3 Editor::bone_head(const GameObject &g) const { return g.world_position(); }

Vec3 Editor::bone_tail(const GameObject &g) const {
  const Bone *b = g.get<Bone>();
  return g.world_matrix().point(Vec3(0, b ? b->length : 1.0f, 0));
}

/* The bone whose ends show handles: the active object, selected, a bone, outside Edit Mode and Play. */
GameObject *Editor::bone_edit_target() {
  GameObject *g = active_object();
  if (!g || !g->get<Bone>() || !is_selected(g->id) || edit_mode_ || playing_ || weight_paint_ || autorig_.active) return nullptr;
  return g;
}

/* Puts the bone's head at `head` and its tail at `tail` (world): the object moves to the head, turns the least so its
 * +Y points at the tail (the roll is kept), takes the distance as its length; the children stay where they were, and
 * the bone and its children take the result as their rest pose. */
void Editor::set_bone_ends(GameObject &g, Vec3 head, Vec3 tail, const Quat *base) {
  Bone *b = g.get<Bone>();
  const Vec3 d = tail - head;
  const float len = length(d);
  if (!b || !(len > 1e-4f) || !std::isfinite(len)) return;
  struct Kept {
    GameObject *obj;
    Vec3 pos;
    Quat rot;
  };
  std::vector<Kept> kids;
  for (GameObject *c : g.children) kids.push_back({c, c->world_position(), c->world_rotation()});
  const Quat rot = base ? *base : g.world_rotation();  // (a drag turns from where it started: the roll doesn't drift)
  const Vec3 y = normalize(rot.rotate(Vec3(0, 1, 0)));
  g.set_world_position(head);
  g.set_world_rotation(normalize(rotation_between(y, d / len) * rot));
  /* The length is in the bone's own units: a scaled armature (a 0.01 FBX import) has world lengths 100 times smaller. */
  b->length = len / bone_world_unit(g);
  for (const Kept &k : kids) {
    k.obj->set_world_position(k.pos);
    k.obj->set_world_rotation(k.rot);
  }
  b->set_rest(g.local());
  for (const Kept &k : kids)
    if (Bone *cb = k.obj->get<Bone>()) cb->set_rest(k.obj->local());
}

/* Where the mouse points on the plane through `p` facing the camera. */
bool Editor::bone_drag_point(const Recti &view, Vec3 p, Vec3 &out) {
  const Ray r = scene_r3d_.screen_ray((float)(ui_.in.mx - view.x), (float)(ui_.in.my - view.y));
  const Vec3 n = cam_.forward();
  const float den = dot(r.dir, n);
  if (std::fabs(den) < 1e-6f) return false;
  const float t = dot(p - r.origin, n) / den;
  if (!(t > 0.0f) || !std::isfinite(t)) return false;
  out = r.origin + r.dir * t;
  return true;
}

/* The handles: drawn on the selected bone; a press on one starts dragging that end (Ctrl: snaps to the grid). True
 * while it has the mouse (the gizmo and selection keep away). */
/* Which handle is under the mouse: 1 the tail; 0 the head, with Shift held (the gizmo sits on the head: a plain press
 * there is the gizmo's); -1 none. */
int Editor::bone_handle_hot(const Recti &view, const GameObject &g) {
  auto &in = ui_.in;
  if (!scene_hovered_) return -1;
  const Vec3 ends[2] = {bone_head(g), bone_tail(g)};
  for (int k = 1; k >= 0; k--) {
    Vec2 s;
    float z;
    if (!scene_r3d_.project(ends[k], s, z)) continue;
    const float x = (float)view.x + s.x, y = (float)view.y + s.y;
    if (std::hypot(x - (float)in.mx, y - (float)in.my) > (float)ui_.px(7)) continue;
    if (k == 0 && !in.shift()) continue;
    return k;
  }
  return -1;
}

/* The handles' input (before the gizmo's, so a pressed handle is the bone's): a press on one starts dragging that end.
 * True while a handle has the mouse. */
bool Editor::bone_handles_update(const Recti &view) {
  auto &u = ui_;
  auto &in = u.in;
  if (bone_drag_.bone && bone_drag_update(view)) return true;
  GameObject *g = show_bones_ && !puppet_control_selected() ? bone_edit_target() : nullptr;  // (round 346: a control's bone - its handles not over the control)
  if (!g) return false;
  const int hot = bone_handle_hot(view, *g);
  if (hot < 0 || !in.pressed[0] || in.alt() || u.any_active()) return hot >= 0;
  const Vec3 head = bone_head(*g), tail = bone_tail(*g);
  Vec3 at;
  const Vec3 grab = (hot ? tail : head);
  bone_drag_ = {g->id, hot, false, head, tail, g->world_rotation(), bone_drag_point(view, grab, at) ? grab - at : Vec3(0.0f)};
  in.pressed[0] = false;  // (taken: the gizmo and selection don't see it)
  return true;
}

/* The handles drawn over the picture: the tail blue, the head orange (Shift+drag), the one under the mouse white. */
void Editor::draw_bone_handles(const Recti &view) {
  auto &u = ui_;
  GameObject *g = bone_drag_.bone ? scene_->find(bone_drag_.bone) : bone_edit_target();
  if (!g || !g->get<Bone>() || !show_bones_ || (!bone_drag_.bone && puppet_control_selected())) return;  // (the Bones overlay layer off: no handles either, task 0146)
  const int hot = bone_drag_.bone ? bone_drag_.end : bone_handle_hot(view, *g);
  const Vec3 ends[2] = {bone_head(*g), bone_tail(*g)};
  u.canvas.push_clip(view);
  for (int k = 0; k < 2; k++) {
    Vec2 s;
    float z;
    if (!scene_r3d_.project(ends[k], s, z)) continue;
    const float x = (float)view.x + s.x, y = (float)view.y + s.y;
    const bool over = hot == k;
    u.canvas.fill_circle(x, y, (float)u.px(over ? 6 : 5), Color::hex(0x101010));
    u.canvas.fill_circle(x, y, (float)u.px(over ? 5 : 4), over ? Color::hex(0xFFFFFF) : k ? Color::hex(0x5AA9FF) : Color::hex(0xFFB347));
    if (u.record_buttons) u.button_rects[k ? "bone_tail" : "bone_head"] = Recti{(int)x - 4, (int)y - 4, 8, 8};  // (tests)
  }
  u.canvas.pop_clip();
}

/* A handle (or an extruded bone's tail) following the mouse. A held drag ends when the button comes up; the modal
 * one of Extrude ends with a click or Enter, and Esc or a right-click takes the new bone away again. */
bool Editor::bone_drag_update(const Recti &view) {
  auto &in = ui_.in;
  GameObject *g = scene_->find(bone_drag_.bone);
  if (!g || !g->get<Bone>()) return (void)(bone_drag_ = BoneDrag{}), false;
  if (bone_drag_.modal && (in.key_pressed[platform::KEY_ESCAPE] || in.pressed[1])) {
    in.key_pressed[platform::KEY_ESCAPE] = false;
    in.pressed[1] = false;
    bone_drag_ = BoneDrag{};
    delete_bone_extruded(g);
    return true;
  }
  const bool done = bone_drag_.modal ? ((in.pressed[0] && scene_hovered_) || in.key_pressed[platform::KEY_ENTER]) : !in.down[0];
  Vec3 p;
  const Vec3 anchor = bone_drag_.end ? bone_drag_.tail0 : bone_drag_.head0;
  if ((scene_hovered_ || !bone_drag_.modal) && bone_drag_point(view, anchor, p)) {
    p += bone_drag_.offset;  // (where it was grabbed, not the handle's centre jumping to the mouse)
    if (in.ctrl() && !bone_drag_.modal) p = Vec3(std::round(p.x / snap_move_) * snap_move_, std::round(p.y / snap_move_) * snap_move_, std::round(p.z / snap_move_) * snap_move_);
    if (bone_drag_.end) set_bone_ends(*g, bone_head(*g), p, &bone_drag_.rot0);
    else set_bone_ends(*g, p, bone_drag_.tail0, &bone_drag_.rot0);
    scene_dirty_ = true;
    /* One undo step: a held drag commits when the button comes up; the extrude's commit waits for its click (the
     * frame loop holds the step while it runs). */
    mark_changed(bone_drag_.modal ? "Extrude Bone" : bone_drag_.end ? "Move Bone Tail" : "Move Bone Head");
  }
  if (done) {
    if (bone_drag_.modal) in.pressed[0] = false, in.key_pressed[platform::KEY_ENTER] = false;
    bone_drag_ = BoneDrag{};
  }
  return true;
}

/* Extrude (E) on a bone: a child from its tip, half its length along it, whose tail then follows the mouse until a
 * click (Blender's E in an armature's Edit Mode). */
bool Editor::bone_extrude(GameObject *g) {
  const Bone *pb = g ? g->get<Bone>() : nullptr;
  if (!pb || playing_) return false;
  GameObject *b = add_bone(g, std::max(0.05f, pb->length * 0.5f));
  if (!b) return false;
  b->set_local_rotation(Quat());
  b->get<Bone>()->set_rest(b->local());
  bone_drag_ = {b->id, 1, true, bone_head(*b), bone_tail(*b), b->world_rotation(), Vec3(0.0f)};
  Log::info("Extruded '%s' from '%s': move the mouse to place its tip, click to keep it (Esc cancels)", b->name.c_str(), g->name.c_str());
  return true;
}

/* Esc on an extrude: the new bone goes and the step it was making is dropped (the scene is as it was before it). */
void Editor::delete_bone_extruded(GameObject *g) {
  GameObject *parent = g->parent;
  scene_->destroy(g);
  if (parent) select(parent->id);
  scene_dirty_ = true;
  pending_change_ = false;
}

/* bone head|tail <bone> x y z | bone extrude <bone> (the console's bone editing, task 0066). */
bool Editor::bone_edit_command(const std::vector<std::string> &t) {
  const std::string verb = t.size() > 1 ? to_lower(t[1]) : "";
  if (verb != "head" && verb != "tail" && verb != "extrude") return false;
  GameObject *g = t.size() > 2 ? scene_->find_by_name(t[2]) : nullptr;
  if (!g || !g->get<Bone>()) {
    Log::warn("bone %s: no bone '%s'", verb.c_str(), t.size() > 2 ? t[2].c_str() : "");
    return true;
  }
  if (verb == "extrude") {
    if (bone_extrude(g)) bone_drag_ = BoneDrag{};  // (from the console: no mouse to follow)
    return true;
  }
  if (t.size() < 6) {
    Log::warn("bone %s <bone> x y z", verb.c_str());
    return true;
  }
  const Vec3 p((float)std::atof(t[3].c_str()), (float)std::atof(t[4].c_str()), (float)std::atof(t[5].c_str()));
  if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return true;
  if (verb == "tail") set_bone_ends(*g, bone_head(*g), p);
  else set_bone_ends(*g, p, bone_tail(*g));
  mark_changed(verb == "tail" ? "Move Bone Tail" : "Move Bone Head");
  return true;
}

}  // namespace bl

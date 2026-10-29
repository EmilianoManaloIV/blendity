// SPDX-License-Identifier: GPL-2.0-or-later
// Puppet Posing's handles (task 0281, ADR 0087): while AutoPosing is on, the hands, the feet, the head and the hips of
// the selected rig are dots in the Scene view. Drag one and the body follows (the pose prior, then exact IK); click one
// to pin it where it is (filled) or free it again (hollow). No bone needs selecting and no gizmo needs finding: a
// puppet's strings, as Marionette and Cascadeur offer them.
#include "editor.h"

#include "../scene/humanoid.h"

namespace bl {

namespace {
GameObject *rig_above(GameObject *g) {
  for (GameObject *a = g; a; a = a->parent)
    if (a->get<Armature>()) return a;
  return nullptr;
}
constexpr humanoid::Part kHandleParts[] = {humanoid::LHand, humanoid::RHand, humanoid::LFoot, humanoid::RFoot, humanoid::Head, humanoid::Hips};
}  // namespace

/* The handles: the rig's ends while AutoPosing is on. */
const std::vector<uint64_t> &Editor::puppet_handles() {
  static const std::vector<uint64_t> none;
  return autopose_.on ? rig_ends() : none;
}

/* The hands, the feet, the head and the hips of the rig above the active object, mapped again only when the rig
 * changes (the handles and the trails). */
const std::vector<uint64_t> &Editor::rig_ends() {
  GameObject *arm = rig_above(active_object());
  if (!arm) {
    puppet_.handles.clear(), puppet_.arm = 0;
    return puppet_.handles;
  }
  const uint64_t key = scene_->serial;  // (the rig's bones change with the scene's structure, not with its pose)
  if (arm->id == puppet_.arm && key == puppet_.key) return puppet_.handles;
  puppet_.arm = arm->id, puppet_.key = key;
  puppet_.handles.clear();
  const humanoid::Map m = humanoid::map(*scene_, *arm);
  for (humanoid::Part p : kHandleParts)
    if (m.part[p]) puppet_.handles.push_back(m.part[p]->id);
  return puppet_.handles;
}

/* Pinned: locked by the artist, or held by AutoPose's own rule (the feet, the contacts at the playhead). */
bool Editor::puppet_pinned(GameObject &arm, uint64_t id) {
  if (std::find(autopose_.locks.begin(), autopose_.locks.end(), id) != autopose_.locks.end()) return true;
  for (GameObject *p : autopose_pins(arm, arm))
    if (p->id == id) return true;
  return false;
}

void Editor::draw_puppet_handles() {
  GameObject *arm = rig_above(active_object());
  if (!arm) return;
  const auto &handles = puppet_handles();
  const std::vector<GameObject *> pins = autopose_pins(*arm, *arm);  // (once a frame, not once a handle)
  for (uint64_t id : handles) {
    GameObject *g = scene_->find(id);
    if (!g) continue;
    const bool locked = std::find(autopose_.locks.begin(), autopose_.locks.end(), id) != autopose_.locks.end();
    const bool pinned = locked || std::any_of(pins.begin(), pins.end(), [&](GameObject *p) { return p->id == id; });
    const bool hot = id == puppet_.dragging || id == puppet_.hover;
    const uint32_t ring = hot ? Color::hex(0xFFE060, 255) : Color::hex(0xFFFFFF, 235);
    scene_r3d_.point(g->world_position(), (float)ui_.px(hot ? 9 : 8), ring, false);
    scene_r3d_.point(g->world_position(), (float)ui_.px(5), pinned ? Color::hex(0xE04848, 255) : Color::hex(0x202020, 200), false);
  }
}

/* The Scene view's input for the handles: a press on one starts its drag (or, released where it was, toggles its pin). */
bool Editor::puppet_handles_update(const Recti &view) {
  auto &in = ui_.in;
  const auto &handles = puppet_handles();
  if (handles.empty()) return puppet_.dragging = 0, false;
  const float x = (float)(in.mx - view.x), y = (float)(in.my - view.y);
  puppet_.hover = 0;
  float best = (float)ui_.px(12);
  for (uint64_t id : handles) {
    GameObject *g = scene_->find(id);
    Vec2 sp;
    float depth = 0.0f;
    if (!g || !scene_r3d_.project(g->world_position(), sp, depth)) continue;
    if (ui_.record_buttons) ui_.button_rects["puppet:" + g->name] = Recti{view.x + (int)sp.x - ui_.px(6), view.y + (int)sp.y - ui_.px(6), ui_.px(12), ui_.px(12)};
    const float d = length(Vec2(sp.x - x, sp.y - y));
    if (d < best) best = d, puppet_.hover = id;
  }
  if (puppet_.dragging) return puppet_drag(view, x, y);
  if (!(scene_hovered_ && in.pressed[0] && puppet_.hover && !in.alt() && !ui_.any_active())) return false;
  GameObject *g = scene_->find(puppet_.hover);
  puppet_.dragging = puppet_.hover;
  puppet_.start = g->world_position();
  puppet_.press = Vec2(x, y);
  puppet_.moved = false;
  puppet_.normal = scene_r3d_.screen_ray(x, y).dir;  // (the drag moves the end in the plane facing the view)
  ui_.consume_click();
  return true;
}

bool Editor::puppet_drag(const Recti &, float x, float y) {
  auto &in = ui_.in;
  GameObject *g = scene_->find(puppet_.dragging);
  if (!g) return puppet_.dragging = 0, false;
  if (length(Vec2(x, y) - puppet_.press) > (float)ui_.px(3)) puppet_.moved = true;
  if (puppet_.moved && in.down[0]) {
    const Ray r = scene_r3d_.screen_ray(x, y);
    const float den = dot(puppet_.normal, r.dir);
    if (std::fabs(den) > 1e-6f) autopose_gizmo(g->id, r.origin + r.dir * (dot(puppet_.normal, puppet_.start - r.origin) / den));
  }
  if (in.down[0]) return true;
  const uint64_t id = puppet_.dragging;
  puppet_.dragging = 0;
  if (puppet_.moved) {
    if (autopose_.bone) autopose_end();
    return true;
  }
  GameObject *arm = rig_above(g);
  const bool locked = std::find(autopose_.locks.begin(), autopose_.locks.end(), id) != autopose_.locks.end();
  if (arm && !locked && puppet_pinned(*arm, id)) return Log::info("AutoPose: %s is held by AutoPose (a foot, or a contact here)", g->name.c_str()), true;
  autopose_command({"autopose", locked ? "unlock" : "lock", g->name});
  return true;
}

}  // namespace bl

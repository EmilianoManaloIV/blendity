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
constexpr const char *kHandleNames[] = {"Left Hand", "Right Hand", "Left Foot", "Right Foot", "Head", "Hips"};
constexpr const char *kHandleWords[] = {"lhand", "rhand", "lfoot", "rfoot", "head", "hips"};
constexpr int kHandles = 6;

std::string trimmed(std::string s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == ';')) s.pop_back();
  while (!s.empty() && s.front() == ' ') s.erase(0, 1);
  return s;
}

/* The bone named for handle `k` in an Armature's Puppet Handles, or "". */
std::string handle_bone_named(const std::string &list, int k) {
  const std::string key = std::string(kHandleNames[k]) + "=";
  size_t at = 0;
  while (at < list.size()) {
    size_t end = list.find(';', at);
    if (end == std::string::npos) end = list.size();
    const std::string item = trimmed(list.substr(at, end - at));
    if (item.compare(0, key.size(), key) == 0) return trimmed(item.substr(key.size()));
    at = end + 1;
  }
  return "";
}

/* The list with handle `k` set to `bone` ("" takes it out). */
std::string with_handle_bone(const std::string &list, int k, const std::string &bone) {
  std::string out;
  for (int i = 0; i < kHandles; i++) {
    const std::string b = i == k ? bone : handle_bone_named(list, i);
    if (!b.empty()) out += (out.empty() ? "" : "; ") + std::string(kHandleNames[i]) + "=" + b;
  }
  return out;
}

GameObject *bone_in_rig(GameObject &arm, const std::string &name) {
  if (name.empty()) return nullptr;
  std::vector<GameObject *> todo{&arm};
  while (!todo.empty()) {
    GameObject *g = todo.back();
    todo.pop_back();
    if (g != &arm && g->name == name && g->get<Bone>()) return g;
    for (GameObject *c : g->children) todo.push_back(c);
  }
  return nullptr;
}
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
  const Armature *a = arm->get<Armature>();
  const std::string set = a ? a->puppet_bones : std::string();
  /* (the rig's bones change with the scene's structure, not with its pose; the handles set by hand with their list) */
  const uint64_t key = scene_->serial ^ (std::hash<std::string>()(set) * 0x9E3779B97F4A7C15ull);
  if (arm->id == puppet_.arm && key == puppet_.key) return puppet_.handles;
  puppet_.arm = arm->id, puppet_.key = key;
  puppet_.handles.clear();
  const humanoid::Map m = humanoid::map(*scene_, *arm);
  for (int k = 0; k < kHandles; k++) {
    puppet_.found[k] = m.part[kHandleParts[k]] ? m.part[kHandleParts[k]]->id : 0;
    GameObject *own = bone_in_rig(*arm, handle_bone_named(set, k));  // (set by hand: round 324)
    const uint64_t id = own ? own->id : puppet_.found[k];
    puppet_.part[k] = id;
    if (id && std::find(puppet_.handles.begin(), puppet_.handles.end(), id) == puppet_.handles.end()) puppet_.handles.push_back(id);
  }
  return puppet_.handles;
}

/* autopose handle <part> <bone>|auto: a handle put on a bone by hand, or found again (round 324). Part: lhand rhand
 * lfoot rfoot head hips, or the handle's name. */
void Editor::puppet_set_handle(int k, const std::string &bone) {
  GameObject *arm = rig_above(active_object());
  Armature *a = arm ? arm->get<Armature>() : nullptr;
  if (!a) return Log::warn("autopose handle: select a bone of the rig (or its armature) first");
  if (!bone.empty() && to_lower(bone) != "auto" && !bone_in_rig(*arm, bone)) return Log::warn("autopose handle: %s has no bone named '%s'", arm->name.c_str(), bone.c_str());
  a->puppet_bones = with_handle_bone(a->puppet_bones, k, to_lower(bone) == "auto" ? std::string() : bone);
  mark_changed("Puppet Handle Bone");
  rig_ends();
  GameObject *now = scene_->find(puppet_.part[k]);
  Log::info("Puppet Posing: the %s handle is on %s%s", kHandleNames[k], now ? now->name.c_str() : "nothing", to_lower(bone) == "auto" || bone.empty() ? " (found)" : "");
}

int Editor::puppet_handle_index(const std::string &w) {
  for (int k = 0; k < kHandles; k++)
    if (to_lower(w) == kHandleWords[k] || to_lower(w) == to_lower(kHandleNames[k])) return k;
  return -1;
}

/* Animation > Puppet > Handle Bones: each handle's bone - the selected one, or the one the analysis finds. */
void Editor::draw_puppet_handle_items() {
  GameObject *arm = rig_above(active_object());
  if (!arm) {
    ui_.menu_item("(select a rig's bone)", nullptr, false, false);
    return;
  }
  rig_ends();
  GameObject *sel = selected_bone();
  const Armature *a = arm->get<Armature>();
  for (int k = 0; k < kHandles; k++) {
    GameObject *now = scene_->find(puppet_.part[k]), *found = scene_->find(puppet_.found[k]);
    const bool by_hand = a && !handle_bone_named(a->puppet_bones, k).empty();
    const std::string label = strprintf("%s: %s%s", kHandleNames[k], now ? now->name.c_str() : "(none)", by_hand ? " (set)" : "");
    ui_.submenu(label, ui_.px(260), [this, k, sel, found, by_hand] {
      const std::string use = sel ? "Use the Selected Bone (" + sel->name + ")" : std::string("Use the Selected Bone (select one)");
      if (ui_.menu_item(use, nullptr, false, sel != nullptr)) puppet_set_handle(k, sel->name);
      ui_.tooltip("This handle drags the bone selected in the Scene view or the Hierarchy, whatever the rig's analysis found.");
      const std::string automatic = std::string("Found Automatically (") + (found ? found->name : "none") + ")";
      if (ui_.menu_item(automatic, nullptr, !by_hand)) puppet_set_handle(k, "auto");
      ui_.tooltip("The bone the rig's analysis finds for it (by the hierarchy's shape and the bones' names).");
    });
  }
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

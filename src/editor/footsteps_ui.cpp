// SPDX-License-Identifier: GPL-2.0-or-later
// Footsteps along a path in the editor (task 0126, as 3ds Max Biped's footsteps): `walk path <curve>` plans footprints
// along a curve - each an object of its own, moved and turned with the gizmo - and keys the character walking on them;
// Update walks the prints as they are now, Re-plan lays them out along the path again keeping the ones moved by hand.
// An assistant: off unless switched on (Preferences > Animation Assistants), and what it makes is ordinary keys.
#include "editor.h"
#include "../scene/footsteps.h"
#include "../scene/raycast.h"
#include <algorithm>
#include <cmath>

namespace bl {

namespace {
const float kPiF = 3.14159265f;

bool under(const GameObject *g, const GameObject *root) {
  for (; g; g = g->parent)
    if (g == root) return true;
  return false;
}

/* A print's turn: its yaw about +Y, then tilted onto the ground's slope. */
Quat print_turn(const steps::Plan::Print &p) {
  const Quat yaw = Quat::axis_angle(Vec3(0, 1, 0), p.yaw * kPiF / 180.0f);
  return normalize(rotation_between(Vec3(0, 1, 0), p.normal) * yaw);
}

/* A footprint object read back as a print: its place, the way it points and its up. */
steps::Plan::Print print_of(const GameObject &g, const Footprint &f) {
  steps::Plan::Print p;
  const Quat q = g.world_rotation();
  const Vec3 fwd = q.rotate(Vec3(0, 0, 1)), up = q.rotate(Vec3(0, 1, 0));
  p.side = f.side, p.pos = g.world_position(), p.frame = f.frame, p.normal = normalize(up);
  p.yaw = std::atan2(fwd.x, fwd.z) * 180.0f / kPiF;
  float turned = p.yaw - f.planned_yaw;
  while (turned > 180.0f) turned -= 360.0f;
  while (turned < -180.0f) turned += 360.0f;
  /* (the animator's: moved, turned or retimed since the planner put it there) */
  p.kept = length(p.pos - f.planned) > 1e-3f || std::fabs(turned) > 0.5f || std::fabs(p.frame - f.planned_frame) > 0.01f;
  return p;
}

/* Where the planner put a footprint (kept with the print: Re-plan keeps a hand-edited one edited). */
steps::Plan::Print planned_of(const Footprint &f) {
  steps::Plan::Print p;
  p.side = f.side, p.pos = f.planned, p.yaw = f.planned_yaw, p.frame = f.planned_frame;
  return p;
}

/* The planned record of the print of `side` standing at `pos` under `group` (a kept print's own); none: `fallback`. */
steps::Plan::Print planned_at(const GameObject *group, const steps::Plan::Print &fallback) {
  if (!group) return fallback;
  for (const GameObject *c : group->children) {
    const Footprint *f = c->get<Footprint>();
    if (f && f->side == fallback.side && length(c->world_position() - fallback.pos) < 1e-4f) return planned_of(*f);
  }
  return fallback;
}
}  // namespace

/* With a print (or its group) selected - moved by hand just now - the character it belongs to selected again, so Update
 * and Re-plan act on the character's clip. */
void Editor::steps_focus() {
  for (GameObject *a = active_object(); a; a = a->parent) {
    const Footsteps *fs = a->get<Footsteps>();
    if (!fs) continue;
    if (GameObject *c = scene_->find_by_name(fs->character)) select(c->id);
    return;
  }
}

/* The footsteps group of the animated character (nullptr when it has none). */
GameObject *Editor::steps_group() {
  GameObject *root = anim_root();
  if (!root) return nullptr;
  for (const auto &o : scene_->objects())
    if (const Footsteps *fs = o->get<Footsteps>(); fs && fs->character == root->name) return o.get();
  return nullptr;
}

/* The prints under `group`, in landing order (a frame's left foot first), and where the planner put each. */
std::vector<steps::Plan::Print> Editor::steps_prints(GameObject &group, std::vector<steps::Plan::Print> *planned) {
  std::vector<std::pair<steps::Plan::Print, steps::Plan::Print>> both;
  for (GameObject *c : group.children)
    if (const Footprint *f = c->get<Footprint>()) both.push_back({print_of(*c, *f), planned_of(*f)});
  std::stable_sort(both.begin(), both.end(), [](const auto &a, const auto &b) {
    return a.first.frame != b.first.frame ? a.first.frame < b.first.frame : a.first.side < b.first.side;
  });
  std::vector<steps::Plan::Print> prints;
  for (const auto &p : both) {
    prints.push_back(p.first);
    if (planned) planned->push_back(p.second);
  }
  return prints;
}

/* The animated character, when footprints can find it by its name (not two objects with that name). */
GameObject *Editor::steps_root() {
  GameObject *root = anim_root();
  if (!root) return Log::warn("Footsteps: select a character with a skeleton"), nullptr;
  int named = 0;
  for (const auto &o : scene_->objects()) named += o->name == root->name;
  if (named > 1)
    return Log::warn("Footsteps: two objects are named '%s' - rename one (footprints find their character by name)", root->name.c_str()), nullptr;
  return root;
}

/* The ground under a point for the planner: the scene's meshes, not the character, its prints or the path. */
std::function<bool(Vec3, Vec3 &, Vec3 &)> Editor::steps_ground(const GameObject *root, const GameObject *path) {
  const Scene *s = scene_.get();
  return [s, root, path](Vec3 at, Vec3 &point, Vec3 &normal) {
    SceneHit hit;
    auto skip = [&](const GameObject &g) { return under(&g, root) || &g == path || g.get<Footprint>() || g.get<Footsteps>(); };
    if (!scene_raycast(*s, at + Vec3(0, 1.5f, 0), Vec3(0, -1, 0), 4.0f, hit, skip)) return false;
    point = hit.point, normal = hit.normal.y < 0.0f ? hit.normal * -1.0f : hit.normal;
    return true;
  };
}

/* The character keyed walking on `plan` (made on a copy first: a failure changes nothing) and the prints laid out as
 * objects under its footsteps group - one undo step. `planned`: where the planner put each print (a kept one's
 * original place, turn and frame, so it stays the animator's). */
bool Editor::steps_apply(const steps::Plan &plan, const std::vector<steps::Plan::Print> &planned, const std::string &path_name, const char *label) {
  GameObject *root = steps_root();
  const gait::Rig r = walk_rig();
  if (!root) return false;
  if (!r.valid()) return Log::warn("Footsteps: %s", r.problem.c_str()), false;
  AnimationClipPtr clip = anim_clip();
  const AnimationClip base = clip ? *clip : AnimationClip();
  const float fps = clip && clip->frame_rate > 0.0f ? clip->frame_rate : 30.0f;
  AnimationClip out;
  std::string error;
  const int frames = steps::walk(*scene_, *root, r, walk_.settings.style, plan, fps, base, out, error);
  if (!frames) return Log::warn("Footsteps: %s", error.c_str()), false;
  if (!clip) clip = anim_new_clip(root, root->name + " Footsteps");  // (before the prints change: no clip, nothing changed)
  if (!clip) return false;
  /* The prints as objects: the group made again (its children are the prints, nothing else). */
  if (GameObject *old = steps_group()) scene_->destroy(old);
  GameObject *group = scene_->create(root->name + " Footsteps");
  Footsteps *fs = group->add<Footsteps>();
  fs->character = root->name, fs->path = path_name, fs->style = gait::style_text(walk_.settings.style);
  for (size_t i = 0; i < plan.prints.size(); i++) {
    const steps::Plan::Print &p = plan.prints[i];
    GameObject *g = scene_->create(strprintf("%s %d", p.side ? "R" : "L", (int)std::lround(p.frame)), group);
    g->set_world_position(p.pos);
    g->set_world_rotation(print_turn(p));
    Footprint *f = g->add<Footprint>();
    const steps::Plan::Print &at = i < planned.size() ? planned[i] : p;
    f->side = p.side, f->frame = p.frame, f->sole = r.sole;
    f->planned = at.pos, f->planned_yaw = at.yaw, f->planned_frame = at.frame;
    if (!p.kept) f->planned_yaw = print_of(*g, *f).yaw;  // (as read back from the object, tilted onto its slope)
  }
  anim_edit_keys(label, [&](AnimationClip &c) {
    c.tracks = out.tracks, c.contacts = out.contacts, c.length = std::max(c.length, out.length), c.frame_rate = out.frame_rate;
  });
  Log::info("Footsteps: %zu prints, %d frames - move or turn a print and Update, or Re-plan to lay them out again (moved prints kept)",
            plan.prints.size(), frames);
  return true;
}

/* Walk Along Path: prints along the curve at the style's step, the character keyed on them. */
void Editor::steps_walk_path(const std::string &curve_name, bool keep_moved) {
  if (!assist_require(Assist::Footsteps)) return;
  if (playing_) return refuse("Footsteps: stop Play first", "stop", "Stop Play");
  GameObject *root = steps_root();
  if (!root) return;
  GameObject *curve = scene_->find_by_name(curve_name);
  const Curve *cv = curve ? curve->curve() : nullptr;
  if (!cv && keep_moved)
    return Log::warn("Footsteps: the path '%s' is gone or renamed - pick it in Rigging > Footsteps > Path, then Walk Along Path", curve_name.c_str());
  if (!cv) return Log::warn("Footsteps: no curve named '%s' (Add > Curve > Path, then walk path <its name>)", curve_name.c_str());
  const gait::Rig r = walk_rig();
  if (!r.valid()) return Log::warn("Footsteps: %s", r.problem.c_str());
  AnimationClipPtr clip = anim_clip();
  const float fps = clip && clip->frame_rate > 0.0f ? clip->frame_rate : 30.0f;
  GameObject *old = keep_moved ? steps_group() : nullptr;
  std::vector<steps::Plan::Print> keep;
  if (old)
    for (const steps::Plan::Print &p : steps_prints(*old))
      if (p.kept) keep.push_back(p);
  const steps::Plan plan = steps::plan(r, walk_.settings.style, sample_curve(*cv, curve->world_matrix()), fps, steps_ground(root, curve), keep);
  if (!plan.problem.empty()) return Log::warn("Footsteps: %s", plan.problem.c_str());
  if (plan.dropped) Log::warn("Footsteps: %d print%s moved by hand had no step of that foot near them any more and were laid out again", plan.dropped,
                              plan.dropped == 1 ? "" : "s");
  /* A kept print's planned record stays the one the planner first made (so it is still the animator's). */
  std::vector<steps::Plan::Print> planned;
  for (const steps::Plan::Print &p : plan.prints) planned.push_back(p.kept ? planned_at(old, p) : p);
  steps_apply(plan, planned, curve->name, keep_moved ? "Re-plan Footsteps" : "Walk Along Path");
}

/* Update: the character walks the prints where they are now (moved, turned, retimed by hand). */
void Editor::steps_update() {
  if (!assist_require(Assist::Footsteps)) return;
  if (playing_) return refuse("Footsteps: stop Play first", "stop", "Stop Play");
  GameObject *group = steps_group();
  if (!group) return refuse("Footsteps: the character has no footprints - Rigging > Footsteps > Walk Along Path makes them", "window Rigging", "Open Rigging");
  steps::Plan plan;
  std::vector<steps::Plan::Print> planned;
  plan.prints = steps_prints(*group, &planned);
  if (plan.prints.size() < 3) return Log::warn("Footsteps: fewer than three prints to walk on");
  const float gap = plan.prints.back().frame - plan.prints[plan.prints.size() - 2].frame;
  plan.frames = (int)std::ceil(plan.prints.back().frame + std::max(1.0f, gap) * 0.5f);
  const std::string path = group->get<Footsteps>()->path;
  steps_apply(plan, planned, path, "Update Footsteps");
}

/* walk path <curve> | walk steps update | walk steps replan */
void Editor::steps_command(const std::vector<std::string> &t) {
  if (!assist_require(Assist::Footsteps)) return;
  steps_focus();
  const std::string sub = t.size() > 1 ? to_lower(t[1]) : "";
  if (sub == "path" && t.size() > 2) return steps_walk_path(t[2], false);
  const std::string what = t.size() > 2 ? to_lower(t[2]) : "";
  if (what == "update") return steps_update();
  if (what == "replan") {
    GameObject *g = steps_group();
    if (!g) return refuse("Footsteps: the character has no footprints - Rigging > Footsteps > Walk Along Path makes them", "window Rigging", "Open Rigging");
    return steps_walk_path(g->get<Footsteps>()->path, true);
  }
  Log::warn("walk path <curve> | walk steps update | walk steps replan");
}

/* The Rigging window's Footsteps section: the path, Walk Along Path, Update and Re-plan. */
void Editor::draw_steps_section(ui::Layout &lay) {
  auto &u = ui_;
  GameObject *group = steps_group();
  std::string curve = group ? group->get<Footsteps>()->path : steps_path_;
  if (curve.empty()) {
    GameObject *a = active_object();
    if (a && a->curve()) curve = a->name;
  }
  draw_reflected([&](Reflector &rf) {
    rf.object("Path", steps_path_, "Curve");
    rf.help("The curve to walk along (Add > Curve > Path). The style is Walk Cycle's.");
  }, lay);
  if (steps_path_.empty()) steps_path_ = curve;
  Recti row = lay.row(u.row_h() + u.px(2));
  const int w = (row.w - u.px(16)) / 3;
  if (u.button({row.x + u.px(4), row.y, w, row.h}, "Walk Along Path")) steps_walk_path(steps_path_, false);
  u.tooltip("Footprints along the path at the style's step (shorter in turns, starting and stopping), each on the ground under it;\n"
            "the character keyed walking on them. Console: walk path <curve>.");
  if (!group) return;
  if (u.button({row.x + u.px(8) + w, row.y, w, row.h}, "Update")) steps_command({"walk", "steps", "update"});
  u.tooltip("Walks the prints where they are now: move, turn or retime one (its Frame) and Update. Console: walk steps update.");
  if (u.button({row.x + u.px(12) + 2 * w, row.y, w, row.h}, "Re-plan")) steps_command({"walk", "steps", "replan"});
  u.tooltip("Lays the prints out along the path again (after the path or the style changed), keeping the ones moved by hand.\n"
            "Console: walk steps replan.");
}

/* The prints in the Scene view: a sole each (left blue, right red, selected orange) at the print's ground - the print's
 * object is at the ankle, `sole` above it. */
void Editor::draw_footprint_soles() {
  if (!assist_on(Assist::Footsteps)) return;
  for (const auto &o : scene_->objects()) {
    const GameObject &g = *o;
    const Footprint *f = g.get<Footprint>();
    if (!f || !g.active_in_hierarchy()) continue;
    const Mat4 &m = g.world_matrix();
    const uint32_t col = is_selected(g.id) ? Color::hex(0xF7941D) : f->side ? Color::hex(0xE04848, 230) : Color::hex(0x3C8CE6, 230);
    const float y = 0.005f - f->sole;  // (just above the ground)
    const Vec3 c[6] = {m.point(Vec3(-0.04f, y, -0.06f)), m.point(Vec3(0.04f, y, -0.06f)), m.point(Vec3(0.05f, y, 0.12f)),
                       m.point(Vec3(0.0f, y, 0.2f)), m.point(Vec3(-0.05f, y, 0.12f)), m.point(Vec3(-0.04f, y, -0.06f))};
    for (int i = 0; i < 5; i++) scene_r3d_.line(c[i], c[i + 1], col, false);
  }
}

/* Each print's landing frame over its sole. */
void Editor::draw_footprint_frames(const Recti &view) {
  if (!assist_on(Assist::Footsteps)) return;
  auto &u = ui_;
  for (const auto &o : scene_->objects()) {
    const GameObject &g = *o;
    const Footprint *f = g.get<Footprint>();
    if (!f || !g.active_in_hierarchy()) continue;
    const Mat4 &m = g.world_matrix();
    Vec2 s;
    float z;
    if (!scene_r3d_.project(m.point(Vec3(0, 0.005f - f->sole, 0.05f)), s, z)) continue;
    const std::string t = strprintf("%d", (int)std::lround(f->frame));
    u.canvas.text(u.font, view.x + (int)s.x - u.font.text_width(t) / 2, view.y + (int)s.y - u.font.line_height() / 2, t, Color::hex(0xFFFFFF, 230));
  }
}

}  // namespace bl

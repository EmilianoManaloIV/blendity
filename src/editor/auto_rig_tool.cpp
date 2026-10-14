// SPDX-License-Identifier: GPL-2.0-or-later
// The marker auto-rig in the editor (task 0058): Rigging > Auto-Rig turns the Scene view to a front (or side) view of
// the mesh; each click puts the next marker inside the body under the mouse, as Mixamo does; Build Rig fits the
// skeleton (scene/auto_rig.cpp) and binds the mesh with inpainted weights (task 0057).
#include "editor.h"

#include "../scene/auto_rig.h"
#include "../scene/scene.h"

#include <algorithm>
#include <cmath>

namespace bl {

using ui::Icon;

namespace {

const int kPickRadius = 9;  // unscaled pixels round a marker that grab it

/* Where a ray enters and leaves the mesh, in order (its triangles, in world space). A ray through an edge or a corner
 * hits each triangle there: one crossing, not several. */
std::vector<float> crossings(const GameObject &g, const Ray &ray) {
  std::vector<float> ts;
  const Mesh *m = g.evaluated_mesh();
  if (!m) return ts;
  const Mat4 inv = g.world_matrix().inverse();
  const Ray lr{inv.point(ray.origin), inv.dir(ray.dir)};
  const RenderMesh &rm = m->render_mesh();
  for (size_t t = 0; t < rm.tri_count(); t++) {
    const float d = ray_triangle(lr, rm.positions[rm.indices[t * 3]], rm.positions[rm.indices[t * 3 + 1]], rm.positions[rm.indices[t * 3 + 2]]);
    if (d > 0.0f) ts.push_back(d);
  }
  std::sort(ts.begin(), ts.end());
  ts.erase(std::unique(ts.begin(), ts.end(), [](float a, float b) { return b - a <= 1e-5f * (1.0f + std::fabs(a)); }), ts.end());
  return ts;
}

}  // namespace

/* The middle of the first part of `g` the ray passes through: between where it enters and where it leaves (an arm in a
 * front view, a near leg in a side view - not the far one behind it). False when it misses. */
bool Editor::body_midpoint(GameObject *g, const Ray &ray, Vec3 &out) {
  const std::vector<float> ts = crossings(*g, ray);
  if (ts.empty()) return false;
  const float t = ts.size() > 1 ? 0.5f * (ts[0] + ts[1]) : ts[0];
  out = ray.origin + ray.dir * t;
  return true;
}

bool Editor::autorig_start(GameObject *mesh, RigKind kind) {
  if (playing_) {
    Log::warn("Auto-Rig: stop Play first (a rig made in Play is gone on Stop)");
    return false;
  }
  if (!mesh || !mesh->mesh_filter() || !mesh->mesh_filter()->mesh || mesh->get<Bone>()) {
    Log::warn("Auto-Rig: select the character's mesh first");
    return false;
  }
  set_weight_paint(false);  // (the brush would take the clicks)
  if (edit_mode_) exit_edit_mode();
  /* The same mesh and kind as the last build: its markers again (Mixamo keeps them), to adjust and build again. */
  const bool again = autorig_last_.mesh == mesh->id && autorig_last_.kind == kind;
  autorig_ = again ? autorig_last_ : AutoRigSession{};
  autorig_.active = true;
  autorig_.kind = kind;
  autorig_.mesh = mesh->id;
  autorig_.dragging.clear();
  /* A front view of a person (facing +Z, so the camera looks down -Z), a left-side view of an animal; orthographic,
   * framed on the mesh. */
  autorig_set_view(kind == RigKind::Humanoid ? "Front" : "Left");
  dock_open(WindowKind::Rigging);
  Log::info("Auto-Rig (%s): click the %s", kind == RigKind::Humanoid ? "humanoid" : "quadruped", autorig_next().c_str());
  return true;
}

std::string Editor::autorig_next() const {
  std::string missing;
  if (!autorig_.active || markers_complete(autorig_.kind, autorig_.markers, &missing)) return "";
  return missing;
}

void Editor::autorig_place(const std::string &name, Vec3 at) {
  GameObject *mesh = scene_->find(autorig_.mesh);
  if (!autorig_.active || !mesh) return;
  autorig_.markers[name] = at;
  autorig_.order.erase(std::remove(autorig_.order.begin(), autorig_.order.end(), name), autorig_.order.end());
  autorig_.order.push_back(name);
  if (!autorig_.symmetry) return;
  /* Symmetry: the twin follows, mirrored across the mesh's middle. */
  for (const RigMarker &k : rig_markers(autorig_.kind)) {
    if (name != k.name || !k.mirror) continue;
    const float mid = mesh->world_bounds().center().x;
    autorig_.markers[k.mirror] = Vec3(2.0f * mid - at.x, at.y, at.z);
  }
}

void Editor::autorig_remove_last() {
  if (autorig_.order.empty()) return;
  const std::string last = autorig_.order.back();
  autorig_.order.pop_back();
  autorig_.markers.erase(last);
  const auto &ms = rig_markers(autorig_.kind);
  const auto k = std::find_if(ms.begin(), ms.end(), [&](const RigMarker &x) { return last == x.name; });
  if (k == ms.end() || !k->mirror || !autorig_.symmetry) return;
  const bool twin_by_hand = std::find(autorig_.order.begin(), autorig_.order.end(), k->mirror) != autorig_.order.end();
  if (!twin_by_hand) autorig_.markers.erase(k->mirror);  // (its mirrored twin goes with it)
}

void Editor::autorig_cancel() {
  if (autorig_.active) Log::info("Auto-Rig: cancelled");
  autorig_ = AutoRigSession{};
}

/* Build Rig: the skeleton from the markers (one undo step), then the mesh bound to it with inpainted weights (its own
 * step, in the background on a large mesh). */
bool Editor::autorig_build() {
  GameObject *mesh = scene_->find(autorig_.mesh);
  if (!autorig_.active || !mesh || playing_) {
    Log::warn("Auto-Rig: start it with a mesh selected (Rigging > Auto-Rig), out of Play");
    return false;
  }
  if (skin_job_) {
    Log::warn("Auto-Rig: a skin solve is still running (Rigging window: Cancel, or wait)");
    return false;
  }
  std::string err;
  GameObject *root = build_rig_from_markers(*scene_, autorig_.kind, autorig_.markers, mesh->world_bounds(), &err);
  if (!root) {
    Log::warn("Auto-Rig: %s", err.c_str());
    return false;
  }
  SkinModifier *sk = mesh->get<SkinModifier>();
  if (!sk) sk = mesh->add<SkinModifier>();
  sk->object = root->name;
  nest_armature(root, mesh);  // (inside the mesh's rig root, as Unity lays out a humanoid: task 0065)
  expanded_.insert(root->id);
  mark_changed("Auto-Rig");
  autorig_last_ = autorig_;
  autorig_last_.active = false;
  autorig_ = AutoRigSession{};
  Log::info("Auto-Rig: '%s' built; binding '%s'", root->name.c_str(), mesh->name.c_str());
  skin_bind(mesh);
  if (!skin_job_) mark_changed("Auto-Rig");  // (bound at once: one step, named for the rig)
  return true;
}

/* The Scene view while placing: a click puts the next marker; a press on a marker drags it. Alt (navigation) passes. */
bool Editor::autorig_update(const Recti &view) {
  if (!autorig_.active) return false;
  auto &u = ui_;
  auto &in = u.in;
  GameObject *mesh = scene_->find(autorig_.mesh);
  if (!mesh) {
    autorig_cancel();
    return false;
  }
  if (!autorig_.dragging.empty() && !in.down[0]) autorig_.dragging.clear();
  /* Esc ends the session, unless something else (a text field, a G / R / S) has it. */
  if (in.key_pressed[platform::KEY_ESCAPE] && scene_hovered_ && !u.wants_keyboard() && !xf_.active && !modal_.active) {
    in.key_pressed[platform::KEY_ESCAPE] = false;
    autorig_cancel();
    return false;
  }
  if (!scene_hovered_ || in.alt()) return !autorig_.dragging.empty();
  const std::string near = autorig_marker_at(in.mx, in.my);
  if (!near.empty() || !autorig_next().empty()) u.cursor = platform::Cursor::Hand;
  const bool pressed = in.pressed[0];  // (consume_click clears it)
  if (pressed) {
    u.consume_click();
    in.double_clicked[0] = false;  // (two quick markers aren't a double-click: that would frame the selection)
    autorig_.dragging = near;  // (empty: placing the next one)
  }
  if (!(pressed || (!autorig_.dragging.empty() && in.down[0]))) return !autorig_.dragging.empty();
  const std::string name = autorig_.dragging.empty() ? autorig_next() : autorig_.dragging;
  if (name.empty()) return true;
  const Ray r = scene_r3d_.screen_ray((float)(in.mx - view.x), (float)(in.my - view.y));
  Vec3 p;
  /* Away from the front view, a marker being dragged keeps its depth along the view (it slides on the plane through
   * it facing the camera): the side view sets a knee's depth without jumping to the nearer leg. */
  auto it = autorig_.markers.find(name);
  if (!pressed || !near.empty())
    if (autorig_.view != "Front" && it != autorig_.markers.end() && bone_drag_point(view, it->second, p)) {
      autorig_place(name, p);
      autorig_.dragging = name;
      return true;
    }
  if (!body_midpoint(mesh, {r.origin, normalize(r.dir)}, p)) {
    if (pressed) Log::warn("Auto-Rig: click on the mesh to put the %s marker", name.c_str());
    return true;
  }
  autorig_place(name, p);
  autorig_.dragging = name;  // (held down after placing, it follows the mouse)
  return true;
}

/* The marker drawn under the mouse, if any. */
std::string Editor::autorig_marker_at(int mx, int my) {
  std::string best;
  float best_d = (float)ui_.px(kPickRadius);
  for (const auto &kv : autorig_.markers) {
    int x = 0, y = 0;
    if (!project_to_window(kv.second, x, y)) continue;
    const float d = std::hypot((float)(x - mx), (float)(y - my));
    if (d <= best_d) best_d = d, best = kv.first;
  }
  return best;
}

/* A marker's colour by the part of the body it is on (Maya's and Blender's rig colours): the left side blue, the right
 * red, the middle (head, spine, hips) yellow, a tail purple. Placed: full; a mirrored twin: lighter; not yet placed:
 * dark. */
uint32_t Editor::autorig_marker_color(const std::string &m, int state) const {
  auto has = [&](const char *s) { return m.find(s) != std::string::npos; };
  uint32_t r = 0xFF, g = 0xD2, b = 0x1F;  // the middle
  if (has("Left")) r = 0x3D, g = 0x7B, b = 0xFF;
  else if (has("Right")) r = 0xFF, g = 0x3B, b = 0x3B;
  else if (has("Tail")) r = 0xB0, g = 0x6B, b = 0xFF;
  auto mix = [](uint32_t c, uint32_t to, float t) { return (uint32_t)((float)c + ((float)to - (float)c) * t); };
  if (state == 2) r = mix(r, 255, 0.45f), g = mix(g, 255, 0.45f), b = mix(b, 255, 0.45f);
  if (state == 0) r = mix(r, 0x30, 0.65f), g = mix(g, 0x30, 0.65f), b = mix(b, 0x30, 0.65f);
  return Color::hex((r << 16) | (g << 8) | b);
}

/* The orthographic view to place in, framed on the mesh: Front looks at a figure's face (it faces +Z), Back at its back,
 * Left and Right at its sides, Top down on it. Markers keep their depth (the middle of the body along the view ray), so
 * placing a few in the side view fixes how deep a knee or an elbow sits. */
bool Editor::autorig_set_view(const std::string &view_in) {
  GameObject *mesh = scene_->find(autorig_.mesh);
  if (!autorig_.active || !mesh) return false;
  std::string v = to_lower(view_in);
  float yaw = 180.0f, pitch = 0.0f;
  if (v == "back") yaw = 0.0f;
  else if (v == "left") yaw = 90.0f;
  else if (v == "right") yaw = -90.0f;
  else if (v == "top") pitch = 89.9f;
  else if (v != "front") return Log::warn("autorig view: front, back, left, right or top, not '%s'", view_in.c_str()), false;
  const AABB b = mesh->world_bounds();
  const float side = (v == "left" || v == "right") ? b.max.z - b.min.z : b.max.x - b.min.x;
  const float half = 0.55f * std::max(v == "top" ? b.max.z - b.min.z : b.max.y - b.min.y, side);
  cam_.ortho = true;
  cam_.animate_to(b.center(), yaw, pitch, std::max(0.1f, half) / std::tan(cam_.fov * 0.5f * kDeg2Rad), now_seconds());
  v[0] = (char)std::toupper((unsigned char)v[0]);
  autorig_.view = v;
  return true;
}

/* The markers over the Scene view, coloured by body part (twins lighter), the one being dragged white. */
void Editor::draw_autorig_markers(const Recti &view) {
  if (!autorig_.active) return;
  auto &u = ui_;
  u.canvas.push_clip(view);
  for (const auto &kv : autorig_.markers) {
    int x = 0, y = 0;
    if (!project_to_window(kv.second, x, y)) continue;
    const bool by_hand = std::find(autorig_.order.begin(), autorig_.order.end(), kv.first) != autorig_.order.end();
    const uint32_t c = kv.first == autorig_.dragging ? 0xFFFFFFFF : autorig_marker_color(kv.first, by_hand ? 1 : 2);
    u.canvas.fill_circle((float)x, (float)y, (float)u.px(5), c);
    u.canvas.circle((float)x, (float)y, (float)u.px(6), Color::hex(0x101010));
  }
  const std::string next = autorig_next();
  const Recti tip{view.x + u.px(8), view.bottom() - u.row_h() - u.px(8), view.w - u.px(16), u.row_h()};
  u.label(tip, next.empty() ? "Auto-Rig: every marker placed - Build Rig in the Rigging window (drag a marker to move it)"
                            : "Auto-Rig: click the " + next + "  (Alt+drag to orbit; Esc cancels)",
          u.theme.text_bright);
  u.canvas.pop_clip();
}

/* The Rigging window's Auto-Rig section: start, the diagram with the marker to place next, Symmetry, Build. */
void Editor::draw_autorig_section(ui::Layout &lay, GameObject *mesh_obj) {
  auto &u = ui_;
  if (!autorig_.active) {
    const Recti row = lay.row(u.row_h() + u.px(2));
    const int bw = (row.w - u.px(12)) / 2;
    const Recti a{row.x + u.px(4), row.y, bw, row.h}, b{a.right() + u.px(4), row.y, bw, row.h};
    if (u.button(a, "Humanoid")) autorig_start(mesh_obj, RigKind::Humanoid);
    u.tooltip("Mixamo's markers (chin, wrists, elbows, knees, groin) on the selected mesh; a skeleton fitted to them, IK on\n"
              "the limbs, and the mesh bound with inpainted weights. Console: autorig humanoid.");
    if (u.button(b, "Quadruped")) autorig_start(mesh_obj, RigKind::Quadruped);
    u.tooltip("Markers on a four-legged animal seen from its left side (nose, skull, withers, hips, tail, paws, knees and\n"
              "hocks). Console: autorig quadruped.");
    if (!mesh_obj) u.label(lay.row(), "Select the character's mesh first.", u.theme.text_dim);
    return;
  }
  draw_autorig_diagram(lay);
  {
    Recti vr = lay.row(u.row_h() + u.px(2));
    const char *views[] = {"Front", "Left", "Right", "Back", "Top"};
    const int vw = (vr.w - u.px(8) - u.px(3) * 4) / 5;
    for (int k = 0; k < 5; k++) {
      const Recti br{vr.x + u.px(4) + k * (vw + u.px(3)), vr.y, vw, vr.h};
      if (u.button(br, views[k], autorig_.view == views[k])) autorig_set_view(views[k]);
    }
    u.tooltip("Place markers from any side: each keeps its depth through the body, so a side view fixes a knee's or an elbow's depth.\n"
              "Console: autorig view front|left|right|back|top.");
  }
  const std::string next = autorig_next();
  u.label(lay.row(), next.empty() ? "Every marker placed. Drag one in the Scene view to move it." : "Next: click the " + next + " on the mesh.",
          u.theme.text_bright);
  Recti row = lay.row(u.row_h() + u.px(2));
  const int bw = (row.w - u.px(16)) / 3;
  const Recti sym{row.x + u.px(4), row.y, bw, row.h}, back{sym.right() + u.px(4), row.y, bw, row.h}, cancel{back.right() + u.px(4), row.y, bw, row.h};
  if (u.button(sym, autorig_.symmetry ? "Symmetry: On" : "Symmetry: Off")) autorig_.symmetry = !autorig_.symmetry;
  u.tooltip("On: each left or right marker places its twin, mirrored across the mesh's middle (place one side).");
  if (u.button(back, "Remove Last")) autorig_remove_last();
  if (u.button(cancel, "Cancel")) autorig_cancel();
  const Recti b = lay.row(u.row_h() + u.px(4));
  const Recti br{b.x + u.px(4), b.y, b.w - u.px(8), b.h};
  if (!next.empty()) {
    u.frame(br, Color::hex(0x2A2A2A), u.theme.border, u.px(3));
    u.label(br, "Build Rig", u.theme.text_dim, ui::Align::Center);
  }
  else if (u.button(br, "Build Rig")) autorig_build();
  u.tooltip("The skeleton from the markers, IK on each limb, and the mesh bound to it with inpainted weights.");
}

/* A small figure with the markers on it, coloured by body part; the next one ringed, unplaced ones dark. */
void Editor::draw_autorig_diagram(ui::Layout &lay) {
  auto &u = ui_;
  const Recti area = lay.row(u.px(150));
  const Recti fig{area.x + area.w / 2 - u.px(75), area.y + u.px(4), u.px(150), area.h - u.px(8)};
  u.canvas.fill_rect(fig, Color::hex(0x202020));
  const std::vector<RigMarker> &ms = rig_markers(autorig_.kind);
  auto at = [&](float dx, float dy) { return Vec2((float)fig.x + dx * (float)fig.w, (float)fig.y + dy * (float)fig.h); };
  auto pos = [&](const char *n) {
    for (const RigMarker &k : ms)
      if (std::string(n) == k.name) return at(k.dx, k.dy);
    return at(0.5f, 0.5f);
  };
  auto line = [&](Vec2 a, Vec2 b) { u.canvas.line(a.x, a.y, b.x, b.y, Color::hex(0x777777), 2.0f); };
  if (autorig_.kind == RigKind::Humanoid) {
    const Vec2 neck = at(0.5f, 0.24f);
    u.canvas.circle(at(0.5f, 0.07f).x, at(0.5f, 0.07f).y, (float)fig.w * 0.06f, Color::hex(0x777777), 2.0f);
    line(pos("Chin"), pos("Groin"));
    for (const char *s : {"Left", "Right"}) {
      const std::string S = s;
      line(neck, pos((S + "Elbow").c_str()));
      line(pos((S + "Elbow").c_str()), pos((S + "Wrist").c_str()));
      line(pos("Groin"), pos((S + "Knee").c_str()));
      line(pos((S + "Knee").c_str()), Vec2(pos((S + "Knee").c_str()).x, at(0, 0.97f).y));
    }
  }
  else {
    for (auto [a, b] : {std::pair<const char *, const char *>{"Nose", "Skull"}, {"Skull", "Withers"}, {"Withers", "Hips"}, {"Hips", "TailBase"},
                        {"TailBase", "TailTip"}, {"Withers", "FrontLeftKnee"}, {"FrontLeftKnee", "FrontLeftPaw"}, {"Hips", "HindLeftHock"},
                        {"HindLeftHock", "HindLeftPaw"}})
      line(pos(a), pos(b));
  }
  const std::string next = autorig_next();
  for (const RigMarker &k : ms) {
    const Vec2 p = at(k.dx, k.dy);
    const bool placed = autorig_.markers.count(k.name) != 0, is_next = next == k.name;
    const uint32_t c = autorig_marker_color(k.name, placed || is_next ? 1 : 0);
    if (is_next) u.canvas.circle(p.x, p.y, (float)u.px(9), 0xFFFFFFFF, 2.0f);  // (the next one ringed; no animation: idle stays idle)
    u.canvas.fill_circle(p.x, p.y, (float)u.px(is_next ? 6 : 4), c);
  }
  u.label({fig.x + u.px(4), fig.y + u.px(2), fig.w, u.row_h()}, autorig_.view + " view", u.theme.text_dim);
  u.tooltip("The markers to place, coloured by body part: left blue, right red, the middle yellow, a tail purple; the ringed\n"
            "one is next, dark ones aren't placed yet. A humanoid starts from the front (its left on your right), a quadruped\n"
            "from its left side.");
}

/* autorig humanoid|quadruped [mesh], autorig mark <marker> <x> <y> <z>, autorig symmetry on|off, autorig build,
 * autorig cancel (task 0058). */
void Editor::autorig_command(const std::vector<std::string> &t) {
  auto arg = [&](size_t i, const std::string &def) { return t.size() > i ? t[i] : def; };
  const std::string verb = to_lower(arg(1, ""));
  if (verb == "humanoid" || verb == "quadruped") {
    autorig_start(t.size() > 2 ? scene_->find_by_name(t[2]) : active_object(), verb == "humanoid" ? RigKind::Humanoid : RigKind::Quadruped);
    return;
  }
  if (verb == "mark" && t.size() >= 6) {
    const auto &ms = rig_markers(autorig_.kind);
    const bool known = std::any_of(ms.begin(), ms.end(), [&](const RigMarker &k) { return t[2] == k.name; });
    if (!autorig_.active || !known) {
      Log::warn("autorig mark: %s", autorig_.active ? ("no marker named '" + t[2] + "' (names are as the diagram's: Chin, LeftWrist, ...)").c_str()
                                                     : "start it first: autorig humanoid|quadruped");
      return;
    }
    autorig_place(t[2], Vec3((float)std::atof(t[3].c_str()), (float)std::atof(t[4].c_str()), (float)std::atof(t[5].c_str())));
    return;
  }
  if (verb == "view") {
    autorig_set_view(arg(2, "front"));
    return;
  }
  if (verb == "symmetry") {
    autorig_.symmetry = to_lower(arg(2, "on")) != "off";
    return;
  }
  if (verb == "build") {
    autorig_build();
    return;
  }
  if (verb == "cancel") {
    autorig_cancel();
    return;
  }
  Log::warn("autorig humanoid|quadruped [mesh], autorig mark <marker> <x> <y> <z>, autorig symmetry on|off, autorig build, autorig cancel");
}

}  // namespace bl

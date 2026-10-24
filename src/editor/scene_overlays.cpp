// SPDX-License-Identifier: GPL-2.0-or-later
// The Scene view's overlays: the overlay bar and its layers, the grid, the wires of curves, lattices, bones and controls,
// the light / camera / effector icons, the origins and the view gizmo (task 0177: moved out of scene_view.cpp as they were).
#include "../scene/curve.h"
#include "editor.h"
#include "../core/core.h"
#include "../core/jobs.h"
#include <algorithm>
#include <map>
#include <array>
#include <unordered_map>
#include <cmath>
#include <cstring>

namespace bl {

using namespace platform;
using ui::Icon;

void Editor::draw_scene_overlay_bar(const Recti &bar) {
  auto &u = ui_;
  u.canvas.fill_rect(bar, u.theme.bar);
  u.canvas.hline(bar.x, bar.right(), bar.bottom() - 1, u.theme.border);
  int h = bar.h - u.px(6), y = bar.y + u.px(3), x = bar.x + u.px(6);
  static const char *modes[] = {"Wireframe", "Solid", "Shaded", "Shaded Wireframe"};
  int m = (int)shading_;
  if (u.combo(u.id("shading"), {x, y, u.px(170), h}, m, modes, 4)) shading_ = (Shading)m;
  u.tooltip("Draw mode (Blender: Z pie).\nSolid = fast vertex lighting (Workbench).\n"
            "Shaded = materials, textures, shadows (EEVEE / Unity Shaded).\n"
            "Path tracing: F12, the Render window, or the Camera Preview's Rendered toggle.");
  x += u.px(176);
  if (u.icon_button({x, y, h, h}, Icon::Lightbulb, scene_lighting_, "Scene lighting on/off.\nOff = headlight from the camera, like Blender's Studio lighting.")) scene_lighting_ = !scene_lighting_;
  x += h + u.px(2);
  if (u.icon_button({x, y, h, h}, Icon::Grid, show_grid_, "Toggle grid (Blender: Overlays > Floor).")) show_grid_ = !show_grid_;
  x += h + u.px(2);
  if (u.icon_button({x, y, h, h}, Icon::Move, show_gizmos_, "Toggle Gizmos / transform handles.")) show_gizmos_ = !show_gizmos_;
  x += h + u.px(2);
  if (u.icon_button({x, y, h, h}, Icon::Chart, show_stats_, "Statistics overlay (Blender: Overlays > Statistics).")) show_stats_ = !show_stats_;
  x += h + u.px(2);
  /* The overlay layers (task 0146): a menu of checks, the icons beside it the quick toggles for three of them. */
  Recti ovr{x, y, u.px(78), h};
  overlay_rects_["button"] = ovr;
  bool every = true;
  for (const OverlayLayer &l : overlay_layers()) every = every && *l.flag;
  if (u.button(ovr, "Overlays", !every)) u.open_popup(u.id("scene_overlays"), {ovr.x, ovr.bottom(), ovr.w, 0});
  u.tooltip("Which overlays the Scene view draws: bones, rig controls, light and camera icons, curve and lattice points,\n"
            "origins, footprints, the selection outline, grid, gizmos and statistics (Blender: the Overlays popover).\n"
            "Hide the bones to see a character's mesh with its IK handles still there. Console: overlay <layer> on|off.");
  u.popup(u.id("scene_overlays"), u.px(250), [this] { draw_overlays_menu(); });
  x += ovr.w + u.px(2);
  if (u.icon_button({x, y, h, h}, Icon::Camera, scene_filters_,
                    "Camera filters: show the Main Camera's filters (e.g. Retro Console Filter) in the Shaded view.\n"
                    "Console: filters on / off."))
    scene_filters_ = !scene_filters_;
  x += h + u.px(10);
  /* Blender-style Edit Mode toggle - Unity has no built-in mesh editing (ProBuilder adds it). */
  int ew = u.font.text_width("Edit Mode (Tab)") + h + u.px(12);
  Recti er{bar.right() - ew - u.px(6), y, ew, h};
  bool can_edit = edit_mode_ || (active_object() && active_object()->get<MeshFilter>());
  bool hot = u.hovered(er);
  uint32_t bg = edit_mode_ ? u.theme.accent : (hot && can_edit ? u.theme.button_hover : u.theme.button);
  u.frame(er, bg, u.theme.border, u.px(3));
  const uint32_t ink = !can_edit ? u.theme.text_dim : edit_mode_ ? 0xFFFFFFFF : u.theme.text_bright;  // (white on the accent only)
  u.draw_icon(Icon::Vertex, {er.x + u.px(5), er.y + u.px(3), h - u.px(6), h - u.px(6)}, ink);
  u.canvas.text(u.font, er.x + h + u.px(2), er.y + (h - u.font.line_height()) / 2, "Edit Mode (Tab)", ink);
  if (hot && can_edit && u.in.pressed[0]) { if (edit_mode_) exit_edit_mode(); else enter_edit_mode(); }
  u.label({er.x - u.px(150), y, u.px(140), h}, cam_.ortho ? "Iso" : "Persp", u.theme.text_dim, ui::Align::Right);
  (void)x;
}

/* overlay list | overlay all [on|off] | overlay <bones|controls|icons|points|origins|footprints|outline|grid|gizmos|stats>
 * [on|off|toggle]: the Scene view's overlay layers (task 0146). Its own function: the command chain in
 * run_console_command is at MSVC's nesting limit. */
void Editor::overlay_command(const std::vector<std::string> &t) {
  auto arg = [&](size_t i, const std::string &def) { return t.size() > i ? t[i] : def; };
  const std::string what = to_lower(arg(1, "list")), v = to_lower(arg(2, "toggle"));
  if (what == "controls" && v == "size") {  // overlay controls size <x>: the handles' size preference (task 0156)
    rig_control_scale_ = std::max(0.1f, std::min(5.0f, (float)std::atof(arg(3, "1").c_str())));
    Log::info("Rig control size: %.2f", rig_control_scale_);
    return;
  }
  std::vector<OverlayLayer> layers = overlay_layers();
  if (what == "list") {
    for (const OverlayLayer &l : layers) Log::info("  %-10s %s  %s", l.key, *l.flag ? "on " : "off", l.label);
    return;
  }
  const bool on = v == "on" || v == "1" || v == "true", off_ = v == "off" || v == "0" || v == "false";
  if (!on && !off_ && v != "toggle") {
    Log::warn("overlay %s: on, off or toggle", what.c_str());
    return;
  }
  bool every = true;  // "all" alone: on unless all are on already (the menu's All Overlays)
  for (const OverlayLayer &l : layers) every = every && *l.flag;
  std::string set;
  for (const OverlayLayer &l : layers)
    if (what == "all" || what == l.key) {
      *l.flag = on ? true : off_ ? false : what == "all" ? !every : !*l.flag;
      set += std::string(set.empty() ? "" : ", ") + l.key + (*l.flag ? " on" : " off");
    }
  if (set.empty()) Log::warn("overlay: all, bones, controls, icons, points, origins, footprints, outline, grid, gizmos, stats or list; controls size <x>");
  else Log::info("Overlays: %s", set.c_str());
}

std::vector<Editor::OverlayLayer> Editor::overlay_layers() {
  return {{"bones", "Bones", "The armatures' bones (an Armature's own Show Bones hides one rig). Hidden bones can't be clicked or dragged.", &show_bones_},
          {"controls", "Rig Controls (IK / FK handles)", "The rig's control shapes: IK targets, poles and FK handles, posed by dragging.", &show_rig_controls_},
          {"icons", "Light, Camera & Effector Icons", "The icons that stand for lights, cameras, winds and the Play-mode movers (clicked to select them); a selected Wind shows its arrow.", &show_icons_},
          {"points", "Curve & Lattice Points", "Curves, lattices and the active one's control points.", &show_points_},
          {"origins", "Object Origins", "The dot at each selected object's origin.", &show_origins_},
          {"footprints", "Footprints & Contacts", "The Footsteps tool's soles and frames, and where feet touch the ground.", &show_footprints_},
          {"outline", "Selection Outline", "The orange outline round the selected objects (children lighter).", &show_outline_},
          {"grid", "Grid", "The floor grid and its axes.", &show_grid_},
          {"gizmos", "Gizmos", "The transform handles and the Camera Preview.", &show_gizmos_},
          {"stats", "Statistics", "Object, vertex, face and timing counts in the corner.", &show_stats_}};
}

/* The Overlays menu (task 0146): All Overlays, then one check per layer. Runs at the frame's end, as popups do. */
void Editor::draw_overlays_menu() {
  auto &u = ui_;
  std::vector<OverlayLayer> layers = overlay_layers();
  bool every = true;
  for (const OverlayLayer &l : layers) every = every && *l.flag;
  const bool all_clicked = u.menu_item("All Overlays", nullptr, every);
  overlay_rects_["All Overlays"] = u.last_widget();
  u.tooltip("Every layer on; click again to hide them all (the picture alone).");
  if (all_clicked)
    for (const OverlayLayer &l : layers) *l.flag = !every;
  u.menu_separator();
  for (const OverlayLayer &l : layers) {
    const bool clicked = u.menu_item(l.label, nullptr, *l.flag);
    overlay_rects_[l.label] = u.last_widget();
    u.tooltip(l.help);
    if (clicked) *l.flag = !*l.flag;
  }
}

bool Editor::overlay_shown_for_test(const std::string &key) {
  for (const OverlayLayer &l : overlay_layers())
    if (key == l.key) return *l.flag;
  return false;
}

void Editor::draw_grid(Renderer3D &r3d) {
  /* Unity-like adaptive floor grid on XZ, faded with distance. */
  Vec3 eye = cam_.position();
  float h = std::max(std::fabs(eye.y), cam_.distance * 0.35f);
  float lg = std::log10(std::max(h, 0.01f));
  float level = std::floor(lg);
  float frac = lg - level;
  float s = std::pow(10.0f, level);  // minor spacing: 1 unit when the camera is 1-10 units up
  int n = 60;
  Vec3 c = cam_.ortho ? cam_.pivot : eye;
  float cx = std::round(c.x / (s * 10)) * s * 10, cz = std::round(c.z / (s * 10)) * s * 10;
  float extent = n * s;
  float fade_far = s * 55.0f;
  auto seg_line = [&](Vec3 a, Vec3 b, float alpha) {
    const int segs = 12;
    for (int k = 0; k < segs; k++) {
      Vec3 p0 = lerp(a, b, k / (float)segs), p1 = lerp(a, b, (k + 1) / (float)segs);
      Vec3 mid = (p0 + p1) * 0.5f;
      float d = length(Vec3(mid.x - c.x, 0, mid.z - c.z));
      float f = alpha * saturate(1.0f - d / fade_far);
      if (f > 0.01f) r3d.line(p0, p1, Color::with_alpha(0xFFFFFFFF, f), true, 1e-5f);
    }
  };
  for (int i = -n; i <= n; i++) {
    float o = i * s;
    bool major = (i % 10) == 0;
    float a = major ? 0.22f : 0.13f * (1.0f - frac);
    if (a < 0.01f) continue;
    float x = cx + o, z = cz + o;
    if (std::fabs(x) < s * 0.01f) seg_line({x, 0, cz - extent}, {x, 0, cz + extent}, 0.0f);
    else seg_line({x, 0, cz - extent}, {x, 0, cz + extent}, a);
    if (std::fabs(z) < s * 0.01f) seg_line({cx - extent, 0, z}, {cx + extent, 0, z}, 0.0f);
    else seg_line({cx - extent, 0, z}, {cx + extent, 0, z}, a);
  }
  /* World axes on the floor: X red, Z blue (Blender draws X red / Y green). */
  r3d.line({cx - extent, 0, 0}, {cx + extent, 0, 0}, Color::hex(0xDB3E1D, 150), true, 1e-5f);
  r3d.line({0, 0, cz - extent}, {0, 0, cz + extent}, Color::hex(0x3A7AF8, 150), true, 1e-5f);
}

void Editor::render_overlays(const Recti &) {
  if (overlays_extra_for_test_) overlays_extra_for_test_(scene_r3d_);  // (a test's own lines, dots and outlines first)
  draw_body_overlay();  // (the center of mass over the clip, when shown: task 0115)
  if (show_footprints_) draw_footprint_soles();  // (task 0126)
  draw_ai_overlay();       // (task 0130)
  /* Wireframes. */
  bool all_wire = shading_ == Shading::Wireframe || shading_ == Shading::ShadedWireframe;
  scene_->for_each([&](GameObject &g) {
    if (!g.active_in_hierarchy()) return;
    auto *mr = g.get<MeshRenderer>();
    if (!mr || !mr->enabled) return;
    if (edit_mode_ && g.id == edit_obj_) return;
    const Mesh *m = g.evaluated_mesh();
    if (!m) return;
    const Mat4 &w = g.world_matrix();
    if (!m->loose_edges.empty() && !all_wire && !mr->show_wireframe && mr->display_as == 0) {
      /* Wire edges have no faces to shade: always drawn (Blender does too). */
      const uint32_t lc = is_selected(g.id) ? kSelectOrange : Color::hex(0x202020, 220);
      for (uint64_t k : m->loose_edges)
        if ((k >> 32) < m->vert_count() && (k & 0xFFFFFFFF) < m->vert_count())
          scene_r3d_.line(w.point(m->positions[(size_t)(k >> 32)]), w.point(m->positions[(size_t)(k & 0xFFFFFFFF)]), lc, true, 2e-4f);
    }
    if (!all_wire && !mr->show_wireframe && mr->display_as == 0) return;
    uint32_t col = is_selected(g.id) ? kSelectOrange : (shading_ == Shading::Wireframe ? Color::hex(0xD8D8D8, 200) : Color::hex(0x101010, 150));
    if (mr->display_as != 0 && !is_selected(g.id)) col = Color::hex(0xC8C8C8, 220);  // Blender draws wire objects light
    bool depth = shading_ != Shading::Wireframe;
    if (mr->display_as == 2) {  // Bounds: the box's 12 edges
      const AABB b = m->render_mesh().bounds;
      Vec3 c[8];
      for (int k = 0; k < 8; k++) c[k] = w.point({k & 1 ? b.max.x : b.min.x, k & 2 ? b.max.y : b.min.y, k & 4 ? b.max.z : b.min.z});
      static const int kE[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
      for (auto &e : kE) scene_r3d_.line(c[e[0]], c[e[1]], col, depth, 2e-4f);
      return;
    }
    const auto &edges = m->edge_cache();
    if (edges.size() > 400000) return;  // keep the editor responsive on enormous meshes
    for (auto &e : edges) scene_r3d_.line(w.point(m->positions[e.first]), w.point(m->positions[e.second]), col, depth, 2e-4f);
  });

  /* Probe volume (Lighting > Show Probes): each probe a dot in the light it holds (seen from above),
   * the ones inside geometry red. */
  if (scene_->lighting.show_probes && !probes_.empty()) {
    const size_t n = probes_.probe_count(), step = std::max<size_t>(1, n / 50000);
    const Vec3 up(0, 1, 0), sky = env_.irradiance(up);
    for (size_t i = 0; i < n; i += step) {
      const bool live = probe_frame_.use_live && probes_.live_ok[i];
      const Vec3 c = (live ? probes_.live[i] : probes_.baked[i]).eval(up, sky);
      const uint32_t col = probes_.valid[i] ? to_display_pixel(c, ViewTransform::Standard, 0.0f) : Color::hex(0xFF3030);
      scene_r3d_.point(probes_.position[i], std::max(1.5f, (float)ui_.px(2.5f)), col, true);
    }
  }

  /* Selection outline (orange, children lighter), Unity & Blender style. */
  if (show_outline_ && !edit_mode_ && !selection_.empty() && shading_ != Shading::Wireframe) {
    std::vector<uint32_t> sel, kids;
    std::function<void(GameObject *)> add_kids = [&](GameObject *g) {
      for (GameObject *c : g->children) { kids.push_back((uint32_t)c->id); add_kids(c); }
    };
    for (GameObject *g : selected_objects(false)) {
      sel.push_back((uint32_t)g->id);
      add_kids(g);
    }
    std::sort(kids.begin(), kids.end());
    std::sort(sel.begin(), sel.end());
    if (!kids.empty()) scene_r3d_.outline_ids(kids, kChildOrange, std::max(1, ui_.px(1.5f)));
    scene_r3d_.outline_ids(sel, kSelectOrange, std::max(1, ui_.px(2)));
  }
}

/* Whether the card will take this frame's overlays (task 0192): it is open with their pipelines, and nothing shown
 * sends the view to the CPU anyway (the probes' dots: their colours come from the frame's render, after this). */
bool Editor::overlays_card_expected() const {
  if (!gpu_view_wanted() || !gpu_view_ || !gpu_view_->can_draw_overlays() || overlays_off_gpu_for_test_) return false;
  return !(scene_->lighting.show_probes && !probes_.empty());
}

/* Task 0192: the Scene view's overlays recorded before its picture (the grid and render_overlays: all in the world),
 * so the card can draw them with it; drawn here instead when it doesn't (draw_scene_overlays). */
void Editor::record_scene_overlays(const Recti &view, bool on) {
  scene_overlays_.clear();
  scene_overlays_recorded_ = on;
  if (!on) return;
  ScopedTimer t;
  scene_r3d_.record_overlays(&scene_overlays_);
  if (show_grid_) draw_grid(scene_r3d_);
  render_overlays(view);
  scene_r3d_.record_overlays(nullptr);
  prof_.ms_overlays += t.ms();
}

void Editor::draw_scene_overlays(const Recti &view) {
  if (!scene_overlays_recorded_) {
    if (show_grid_) draw_grid(scene_r3d_);
    render_overlays(view);
    return;
  }
  if (!overlays_on_gpu_) scene_r3d_.replay(scene_overlays_);
}

/* The overlays as the card takes them (task 0192): lines, dots and outlines, and the runs of each in record order. */
void Editor::overlays_to_frame(gpu::RasterFrame &fr) const {
  using Op = Renderer3D::OverlayRecord::Op;
  fr.overlay_lines.reserve(scene_overlays_.ops.size());
  fr.overlay_points.reserve(scene_overlays_.ops.size() / 8);
  auto run = [&](uint32_t kind, size_t at) {
    if (fr.overlay_runs.empty() || fr.overlay_runs.back().kind != kind) fr.overlay_runs.push_back({kind, (uint32_t)at, 0});
    fr.overlay_runs.back().count++;
  };
  for (const Op &op : scene_overlays_.ops) {
    if (op.kind == Op::Outline) {
      run(2, fr.outlines.size());
      gpu::OverlayOutline o;
      o.ids.assign(scene_overlays_.ids.begin() + op.first, scene_overlays_.ids.begin() + op.first + op.count);
      o.color = op.color, o.width = (int)op.value;
      fr.outlines.push_back(std::move(o));
      continue;
    }
    if (op.kind == Op::Point) {
      run(1, fr.overlay_points.size());
      gpu::OverlayPoint p;
      p.p[0] = op.a.x, p.p[1] = op.a.y, p.p[2] = op.a.z, p.color = op.color, p.radius = op.value, p.depth_test = op.depth_test ? 1u : 0u;
      fr.overlay_points.push_back(p);
      continue;
    }
    run(0, fr.overlay_lines.size());
    gpu::OverlayLine l;
    l.a[0] = op.a.x, l.a[1] = op.a.y, l.a[2] = op.a.z, l.b[0] = op.b.x, l.b[1] = op.b.y, l.b[2] = op.b.z;
    /* (A negative bias means "no depth test" on the card: a depth-tested line's bias is never below 0 here.) */
    l.color = op.color, l.bias = op.depth_test ? std::max(0.0f, op.value) : -1.0f;
    fr.overlay_lines.push_back(l);
  }
}

/* Edit-mode overlay: edges + vertices (Blender's edit-mesh overlay). */
void Editor::render_edit_cage() {
  GameObject *g = edit_mode_ ? edit_object() : nullptr;
  if (!g) return;
  const Mesh &m = **edit_mesh_ptr();
  const Mat4 &w = g->world_matrix();
  vert_sel_.resize(m.vert_count(), 0);
  face_sel_.resize(m.face_count(), 0);
  /* N-gon mode (SketchUp): edges inside a flat region are hidden, as SketchUp hides coplanar edges. */
  if (ngon_mode_) ngon_cache(m);
  if (cage_on_gpu_) return;  // (the card drew the edges and the dots into the picture: tasks 0100, 0192)
  /* The cage, recorded in parallel (task 0089: 100,000 edges of a 50k-face mesh were half the overlay's time), from the
   * vertices placed in the world once each rather than once per edge. */
  std::vector<Vec3> wp(m.vert_count());
  JobSystem::global().parallel_for((int64_t)wp.size(), 4096, [&](int64_t a, int64_t b) {
    for (int64_t i = a; i < b; i++) wp[(size_t)i] = w.point(m.positions[(size_t)i]);
  });
  const auto &edges = m.edge_cache();
  scene_r3d_.lines(edges.size(), [&](size_t k, Vec3 &a, Vec3 &b, uint32_t &col) {
    const auto &e = edges[k];
    col = cage_edge_color(m, e.first, e.second);
    a = wp[e.first], b = wp[e.second];
    return col != 0;
  }, true, kCageBias);
  if (elem_ == EditElement::Vertex) {
    scene_r3d_.points(wp.size(), cage_dot_radius(), [&](size_t i, Vec3 &p, uint32_t &col) {
      p = wp[i];
      col = cage_dot_color(i);
      return true;
    }, true);
    return;
  }
  if (elem_ != EditElement::Face) return;
  std::vector<Vec3> pos;
  std::vector<uint32_t> col;
  const float r = edit_face_dots(m, w, pos, col);
  scene_r3d_.points(pos.size(), r, [&](size_t i, Vec3 &p, uint32_t &c) {
    p = pos[i], c = col[i];
    return true;
  }, true);
}

/* Face mode's dots, their radius returned (task 0192: the CPU's overlay and the card's cage both draw them): one at each
 * face's centre, or one per flat region in N-gon mode (the "face" SketchUp would show), orange when selected. */
float Editor::edit_face_dots(const Mesh &m, const Mat4 &w, std::vector<Vec3> &pos, std::vector<uint32_t> &col) {
  const uint32_t on = Color::hex(0xFFA733), off = Color::hex(0x202020);
  face_sel_.resize(m.face_count(), 0);
  if (ngon_mode_) {
    std::vector<uint8_t> rsel(ngon_region_center_.size(), 0);
    for (size_t f = 0; f < m.face_count() && f < ngon_region_.size(); f++)
      if (face_sel_[f] && ngon_region_[f] >= 0) rsel[(size_t)ngon_region_[f]] = 1;
    pos.resize(ngon_region_center_.size()), col.resize(ngon_region_center_.size());
    for (size_t r = 0; r < ngon_region_center_.size(); r++) pos[r] = w.point(ngon_region_center_[r]), col[r] = rsel[r] ? on : off;
    return ui_.px(2.5f);
  }
  pos.resize(m.face_count()), col.resize(m.face_count());
  JobSystem::global().parallel_for((int64_t)m.face_count(), 4096, [&](int64_t a, int64_t b) {
    for (int64_t f = a; f < b; f++) pos[(size_t)f] = w.point(m.face_center((size_t)f)), col[(size_t)f] = face_sel_[(size_t)f] ? on : off;
  });
  return ui_.px(2);
}

void Editor::draw_curve(const GameObject &g, const Curve &cv) {
  const bool sel = is_selected(g.id);
  const CurvePath &path = cv.local_path();  // sampled once per change, placed here by the matrix
  const Mat4 &w = g.world_matrix();
  const uint32_t col = sel ? kSelectOrange : Color::hex(0x101010, 200);
  for (size_t i = 1; i < path.pos.size(); i++) scene_r3d_.line(w.point(path.pos[i - 1]), w.point(path.pos[i]), col, false);
  if (path.closed && path.pos.size() > 2) scene_r3d_.line(w.point(path.pos.back()), w.point(path.pos.front()), col, false);
}

/* A lattice's cage: each point joined to its next along U, V and W (task 0043). */
void Editor::draw_lattice(const GameObject &g, const Lattice &l) {
  const int n = l.control_point_count();
  if (!n) return;
  const Mat4 &w = g.world_matrix();
  const uint32_t col = is_selected(g.id) ? kSelectOrange : Color::hex(0x101010, 200);
  const int nu = l.points_u, nv = l.points_v;
  for (int i = 0; i < n; i++) {
    const int iu = i % nu, iv = (i / nu) % nv, iw = i / (nu * nv);
    const Vec3 p = w.point(l.control_point(i));
    if (iu + 1 < nu) scene_r3d_.line(p, w.point(l.control_point(i + 1)), col, false);
    if (iv + 1 < nv) scene_r3d_.line(p, w.point(l.control_point(i + nu)), col, false);
    if (iw + 1 < l.points_w) scene_r3d_.line(p, w.point(l.control_point(i + nu * nv)), col, false);
  }
}

/* The active curve's or lattice's points: selected ones orange, the rest white; the first one larger (a curve's start). */
void Editor::draw_control_points(const Recti &view, const GameObject &g, const Component &pc) {
  auto &u = ui_;
  for (int i = 0; i < pc.control_point_count(); i++) {
    Vec2 sp;
    float z;
    if (!scene_r3d_.project(g.world_matrix().point(pc.control_point(i)), sp, z)) continue;
    const bool on = pt_obj_ == g.id && std::find(pt_sel_.begin(), pt_sel_.end(), i) != pt_sel_.end();
    const float r = (float)u.px(i == 0 && g.curve() ? 5 : 4);  // a curve's start is larger
    u.canvas.fill_circle(view.x + sp.x, view.y + sp.y, r + 1.0f, Color::hex(0x000000, 200));
    u.canvas.fill_circle(view.x + sp.x, view.y + sp.y, r, on ? kSelectOrange : Color::hex(0xF0F0F0));
  }
}

/* Blender's octahedral bone: from the head (the object's origin) along +Y by its length, widest a tenth of the way. */
void Editor::draw_bone(const GameObject &g, const Bone &b) {
  const Armature *arm = bone_armature(g);
  if (arm && !arm->show_bones) return;
  const bool over = !arm || arm->in_front;  // In Front: over the meshes; off: hidden behind them
  const Mat4 &w = g.world_matrix();
  const float L = finite_bits(b.length) ? std::max(1e-4f, b.length) : 1.0f, r = L * 0.1f;
  const Vec3 head = w.point(Vec3(0.0f)), tail = w.point(Vec3(0, L, 0));
  Vec3 ring[4];
  for (int k = 0; k < 4; k++) {
    const float a = k * kPi * 0.5f;
    ring[k] = w.point(Vec3(std::cos(a) * r, L * 0.1f, std::sin(a) * r));
  }
  /* IK's bones tinted (Blender draws them yellowish): a chain turned by its control, not posed by hand. */
  const uint32_t col = is_selected(g.id) ? kSelectOrange : ik_bones_frame_.count(g.id) ? Color::hex(0xB8A040, 230) : Color::hex(0x202020, 220);
  for (int k = 0; k < 4; k++) {
    scene_r3d_.line(head, ring[k], col, !over);
    scene_r3d_.line(ring[k], tail, col, !over);
    scene_r3d_.line(ring[k], ring[(k + 1) % 4], col, !over);
  }
}

/* The icon an object shows in the Scene view (task 0146's layer; task 0158: the effectors too), None for none. One
 * role lookup an object (a virtual call per component); the effectors' casts run only on an Effector. */
ui::Icon Editor::scene_icon_of(const GameObject &g) {
  if (g.light()) return Icon::Light;
  if (g.camera()) return Icon::Camera;
  if (!g.find_role(Component::RenderRole::Effector)) return Icon::None;
  if (g.get<Wind>()) return Icon::Wind;
  if (g.get<Rotator>()) return Icon::Spin;
  return g.get<Oscillator>() ? Icon::Wave : Icon::None;
}

/* A selected Wind's wire in the selection's orange (task 0158). */
void Editor::draw_wind_gizmo(const GameObject &g, const Wind &w) {
  std::vector<std::pair<Vec3, Vec3>> segs;
  wind_gizmo_segments(g, w, segs);
  for (auto &sg : segs) scene_r3d_.line(sg.first, sg.second, kSelectOrange, false);
}

void Editor::draw_scene_icons(const Recti &view) {
  auto &u = ui_;
  ik_bones_frame_ = ik_driven_bones();
  scene_->for_each([&](GameObject &g) {
    /* Each kind on its own overlay layer (task 0146). */
    if (const Component *bc = show_bones_ ? g.find_role(Component::RenderRole::Bone) : nullptr; bc && g.active_in_hierarchy()) draw_bone(g, *static_cast<const Bone *>(bc));
    if (show_rig_controls_ && RigControl::live.load(std::memory_order_relaxed) && g.active_in_hierarchy())
      if (const RigControl *rc = g.get<RigControl>()) draw_rig_control(g, *rc);
    if (const Component *pc = show_points_ ? g.point_source() : nullptr; pc && g.active_in_hierarchy()) {
      if (const Curve *cv = g.curve()) draw_curve(g, *cv);
      if (const Lattice *lt = g.lattice()) draw_lattice(g, *lt);
      if (g.id == active_ && !edit_mode_) draw_control_points(view, g, *pc);
    }
    if (!show_icons_) return;
    const ui::Icon icon = scene_icon_of(g);  // (the role lookups first: every object, every frame)
    if (icon == Icon::None || !g.active_in_hierarchy()) return;
    auto *l = icon == Icon::Light ? g.light() : nullptr;
    auto *c = icon == Icon::Camera ? g.camera() : nullptr;
    bool sel = is_selected(g.id);
    if (const Wind *w = sel && icon == Icon::Wind ? g.get<Wind>() : nullptr) draw_wind_gizmo(g, *w);  // (before the icon's projection: an arrow can show with its origin off screen)
    Vec2 sp;
    float z;
    if (!scene_r3d_.project(g.world_position(), sp, z)) return;
    float x = view.x + sp.x, y = view.y + sp.y;
    int s = u.px(22);
    u.canvas.fill_circle(x, y, s * 0.62f, Color::hex(sel ? 0x6A4A1A : 0x202020, 170));
    const uint32_t tint = l ? Color::hex(0xFFE27A) : c ? Color::hex(0xDDDDDD) : Color::hex(0x8FD8FF);  // (effectors: a sky blue)
    u.draw_icon(icon, {(int)x - s / 2, (int)y - s / 2, s, s}, sel ? kSelectOrange : tint);
    if (sel && l && l->type == 0) {
      Vec3 p = g.world_position(), d = g.world_rotation().rotate({0, 0, 1});
      scene_r3d_.line(p, p + d * 2.0f, Color::hex(0xFFE27A), false);
    }
    if (sel && l && l->type == 2) {
      /* The spot's cone out to its range, and the inner (full brightness) cone. */
      const RenderLight rl = to_render_light(g, *l);
      const Vec3 p = rl.position, d = rl.direction, r = rl.right, up = cross(d, r);
      const float len = std::min(l->range, 6.0f);
      for (int ring = 0; ring < 2; ring++) {
        const float c = ring ? rl.cos_inner : rl.cos_outer, s = std::sqrt(std::max(0.0f, 1.0f - c * c));
        const float rad = len * s / std::max(1e-3f, c);
        const uint32_t col = Color::hex(0xFFE27A, ring ? 110 : 220);
        Vec3 prev;
        for (int k = 0; k <= 32; k++) {
          const float a = k / 32.0f * 2.0f * kPi;
          const Vec3 q = p + d * len + (r * std::cos(a) + up * std::sin(a)) * rad;
          if (k) scene_r3d_.line(prev, q, col, false);
          if (!ring && k % 8 == 0) scene_r3d_.line(p, q, col, false);
          prev = q;
        }
      }
    }
    if (sel && l && l->type == 3) {
      /* The area's rectangle (or disc), and its normal: it lights the side the arrow points to. */
      const RenderLight rl = to_render_light(g, *l);
      const Vec3 p = rl.position, d = rl.direction, r = rl.right, up = cross(d, r);
      const uint32_t col = Color::hex(0xFFE27A);
      Vec3 prev;
      const int n = rl.disk ? 32 : 4;
      for (int k = 0; k <= n; k++) {
        Vec3 q;
        if (rl.disk) {
          const float a = k / (float)n * 2.0f * kPi;
          q = p + r * (std::cos(a) * rl.width * 0.5f) + up * (std::sin(a) * rl.height * 0.5f);
        }
        else {
          const float sx = (k % 4 == 1 || k % 4 == 2) ? 0.5f : -0.5f, sy = (k % 4 >= 2) ? 0.5f : -0.5f;
          q = p + r * (sx * rl.width) + up * (sy * rl.height);
        }
        if (k) scene_r3d_.line(prev, q, col, false);
        prev = q;
      }
      scene_r3d_.line(p, p + d * std::max(0.5f, 0.5f * (rl.width + rl.height)), col, false);
    }
    if (sel && c) {
      /* Camera frustum (FoCG ch. 8.5). */
      Quat q = g.world_rotation();
      Vec3 p = g.world_position();
      float far_d = std::min(c->far_clip, 4.0f);
      float aspect = c->image_aspect(scene_->render.width / (float)std::max(1, scene_->render.height));
      float tan_v = c->orthographic ? 0.0f : std::tan(c->vertical_fov_deg(aspect) * 0.5f * kDeg2Rad);
      float sx = c->physical && !c->orthographic ? c->shift_x * 2.0f : 0.0f, sy = c->physical && !c->orthographic ? c->shift_y * 2.0f : 0.0f;
      auto rect_at = [&](float d, Vec3 *out) {  // the image rectangle at distance d (lens shift included)
        float hh = c->orthographic ? c->ortho_size : tan_v * d, hw = hh * aspect;
        float cx = sx * hw, cy = sy * hh;
        out[0] = {cx - hw, cy - hh, d};
        out[1] = {cx + hw, cy - hh, d};
        out[2] = {cx + hw, cy + hh, d};
        out[3] = {cx - hw, cy + hh, d};
      };
      Vec3 corners[4];
      rect_at(far_d, corners);
      for (int i = 0; i < 4; i++) {
        Vec3 a = p + q.rotate(corners[i]), b = p + q.rotate(corners[(i + 1) % 4]);
        Vec3 o = c->orthographic ? p + q.rotate({corners[i].x, corners[i].y, 0}) : p;
        scene_r3d_.line(o, a, Color::hex(0xDDDDDD, 200), false);
        scene_r3d_.line(a, b, Color::hex(0xDDDDDD, 200), false);
      }
      if (c->dof && !c->orthographic) {
        /* The focus plane (Blender draws it as Limits > Focus). */
        Vec3 fc[4];
        rect_at(c->focus_distance, fc);
        for (int i = 0; i < 4; i++)
          scene_r3d_.line(p + q.rotate(fc[i]), p + q.rotate(fc[(i + 1) % 4]), Color::hex(0xF0C040, 200), false);
      }
    }
  });
}

bool Editor::project_to_view(const Recti &view, Vec3 p, Vec2 &s) {
  float z;
  if (!scene_r3d_.project(p, s, z)) return false;
  s.x += view.x;
  s.y += view.y;
  return true;
}

/* Object origins (Blender's origin dots): where each selected object's pivot
 * is. While the Inspector's Origin row is in use, a marker shows where Set
 * Origin would move it, before Apply. */
void Editor::draw_origins(const Recti &view) {
  auto &u = ui_;
  auto to_screen = [&](Vec3 p, Vec2 &s) { return project_to_view(view, p, s); };
  u.canvas.push_clip(view);
  if (!playing_)
    for (GameObject *g : selected_objects(false)) {
      if (edit_mode_ && g->id != edit_obj_) continue;
      Vec2 s;
      if (!to_screen(g->world_position(), s)) continue;
      const bool active = g->id == active_;
      u.canvas.fill_circle(s.x, s.y, (float)u.px(4.5f), 0xFF000000);
      u.canvas.fill_circle(s.x, s.y, (float)u.px(3.0f), active ? Color::hex(0xFFA733) : Color::hex(0xE07020));
    }
  GameObject *a = active_object();
  Vec3 c;
  if (origin_hover_ && a && !playing_ && origin_target(*a, origin_mode_, origin_target_, c)) {
    const Mat4 &w = a->world_matrix();
    const Vec3 now = a->world_position();
    /* Geometry to Origin moves the mesh, not the pivot: its new centre lands on the origin. */
    const Vec3 to = origin_mode_ == 8 ? now : w.point(c);
    const Vec3 from = origin_mode_ == 8 ? w.point(c) : now;
    Vec2 s0, s1;
    if (to_screen(from, s0) && to_screen(to, s1)) {
      const Vec2 d = s1 - s0;
      const float len = length(d);
      for (float t = 0; t < len; t += 10.0f) {
        const Vec2 p = s0 + d * (t / std::max(1.0f, len)), q = s0 + d * (std::min(len, t + 5.0f) / std::max(1.0f, len));
        u.canvas.line(p.x, p.y, q.x, q.y, Color::hex(0x40D0FF, 200), (float)u.px(1.0f));
      }
      const float r = (float)u.px(7);
      const Vec2 dia[4] = {{s1.x, s1.y - r}, {s1.x + r, s1.y}, {s1.x, s1.y + r}, {s1.x - r, s1.y}};
      u.canvas.fill_polygon(dia, 4, Color::hex(0x40D0FF, 220));
      u.canvas.line(s1.x - r * 1.8f, s1.y, s1.x + r * 1.8f, s1.y, 0xFF000000, 1.0f);
      u.canvas.line(s1.x, s1.y - r * 1.8f, s1.x, s1.y + r * 1.8f, 0xFF000000, 1.0f);
      const std::string label = origin_mode_ == 8 ? "Geometry moves to the origin" : "New origin";
      const int tw = u.font.text_width(label) + u.px(10);
      Recti box{(int)s1.x + u.px(12), (int)s1.y - u.row_h() - u.px(4), tw, u.row_h()};
      u.canvas.fill_round_rect(box, u.px(3), Color::hex(0x202020, 220));
      u.label(box, label, Color::hex(0x40D0FF), ui::Align::Center);
    }
  }
  /* Edit Origin: what a click would snap to, and how to get out. */
  if (origin_edit_ && !edit_mode_ && !playing_) {
    if (selection_.empty()) origin_edit_ = false;
    Vec3 p;
    std::string what;
    Vec2 s1, s0;
    if (a && scene_hovered_ && drag_ == Drag::None && gizmo_hot_ < 0 && origin_snap_target(view, u.in.mx, u.in.my, p, what) && to_screen(p, s1)) {
      if (to_screen(a->world_position(), s0)) u.canvas.line(s0.x, s0.y, s1.x, s1.y, Color::hex(0x40D0FF, 160), (float)u.px(1.0f));
      const float r = (float)u.px(6);
      const Vec2 dia[4] = {{s1.x, s1.y - r}, {s1.x + r, s1.y}, {s1.x, s1.y + r}, {s1.x - r, s1.y}};
      u.canvas.fill_polygon(dia, 4, Color::hex(0x40D0FF, 230));
      const std::string label = "Snap origin to " + what;
      const int tw = u.font.text_width(label) + u.px(10);
      Recti box{(int)s1.x + u.px(12), (int)s1.y - u.row_h() - u.px(4), tw, u.row_h()};
      u.canvas.fill_round_rect(box, u.px(3), Color::hex(0x202020, 220));
      u.label(box, label, Color::hex(0x40D0FF), ui::Align::Center);
    }
    const std::string hint = "Editing the origin: drag the handles, or click a vertex, edge or face to snap it there.  Esc: done";
    const int hw = u.font.text_width(hint) + u.px(16);
    Recti hb{view.x + (view.w - hw) / 2, view.y + u.px(8), hw, u.row_h()};
    u.canvas.fill_round_rect(hb, u.px(3), Color::hex(0x103040, 230));
    u.label(hb, hint, Color::hex(0x9FE4FF), ui::Align::Center);
  }
  origin_hover_ = false;  // the Inspector sets it again while its row is in use
  u.canvas.pop_clip();
}

/* The scene gizmo's area, label included: clicks there belong to the gizmo,
 * not to selection (they used to start a box select and never reach it). */
Recti Editor::view_gizmo_rect(const Recti &view) const {
  const int size = ui_.px(84);
  const float cx = view.right() - size * 0.62f, cy = view.y + size * 0.62f;
  return {(int)(cx - size * 0.6f), (int)(cy - size * 0.6f), (int)(size * 1.2f), (int)(size * 1.2f) + ui_.row_h() + ui_.px(4)};
}

void Editor::draw_view_gizmo(const Recti &view) {
  auto &u = ui_;
  int size = u.px(84);
  float cx = view.right() - size * 0.62f, cy = view.y + size * 0.62f;
  float rad = size * 0.36f;
  Mat4 v = cam_.view();
  struct Ax { Vec3 dir; uint32_t col; const char *label; float yaw, pitch; bool positive; };
  Ax axes[6] = {{{1, 0, 0}, u.theme.axis_x, "x", -90, 0, true}, {{-1, 0, 0}, u.theme.axis_x, "", 90, 0, false},
                {{0, 1, 0}, u.theme.axis_y, "y", 0, 90, true},  {{0, -1, 0}, u.theme.axis_y, "", 0, -90, false},
                {{0, 0, 1}, u.theme.axis_z, "z", 180, 0, true}, {{0, 0, -1}, u.theme.axis_z, "", 0, 0, false}};
  int order[6] = {0, 1, 2, 3, 4, 5};
  float depth[6];
  Vec2 pos[6];
  for (int i = 0; i < 6; i++) {
    Vec3 d = v.dir(axes[i].dir);
    pos[i] = {cx + d.x * rad, cy - d.y * rad};
    depth[i] = d.z;
  }
  std::sort(order, order + 6, [&](int a, int b) { return depth[a] > depth[b]; });
  Recti area = view_gizmo_rect(view);
  bool hover_area = u.hovered(area);
  int hot = -1;
  float best = (float)u.px(11);
  for (int i = 0; i < 6; i++) {
    float d = length(Vec2((float)u.in.mx, (float)u.in.my) - pos[i]);
    if (hover_area && d < best) { best = d; hot = i; }
  }
  bool hot_center = hover_area && hot < 0 && length(Vec2((float)u.in.mx, (float)u.in.my) - Vec2(cx, cy)) < u.px(10);
  u.canvas.fill_circle(cx, cy, rad + u.px(14), Color::hex(0x000000, hover_area ? 50 : 25));
  for (int k = 0; k < 6; k++) {
    int i = order[k];
    const Ax &a = axes[i];
    uint32_t col = hot == i ? u.theme.axis_hot : a.col;
    if (a.positive) {
      u.canvas.line(cx, cy, pos[i].x, pos[i].y, col, (float)u.px(2));
      u.canvas.fill_circle(pos[i].x, pos[i].y, (float)u.px(8), col);
      u.canvas.text(u.font, (int)pos[i].x - u.font.text_width(a.label) / 2, (int)pos[i].y - u.font.line_height() / 2 - 1, a.label, Color::hex(0x101010));
    }
    else {
      u.canvas.fill_circle(pos[i].x, pos[i].y, (float)u.px(6), hot == i ? u.theme.axis_hot : Color::hex(0xB0B0B0, 200));
    }
  }
  u.canvas.fill_round_rect({(int)cx - u.px(6), (int)cy - u.px(6), u.px(12), u.px(12)}, u.px(2), hot_center ? u.theme.axis_hot : Color::hex(0xCFCFCF));
  /* Unity's label: the view's name when it looks along an axis, and the projection. */
  const Vec3 fwd = cam_.forward();
  const char *name = nullptr;
  static const char *const names[6] = {"Left", "Right", "Bottom", "Top", "Back", "Front"};
  const Vec3 dirs[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, -1}, {0, 0, 1}};
  for (int i = 0; i < 6; i++)
    if (dot(fwd, dirs[i]) > 0.999f) name = names[i];
  const std::string label = strprintf("< %s%s", name ? name : "", name ? (cam_.ortho ? "" : " (Persp)") : (cam_.ortho ? "Iso" : "Persp"));
  Recti lr{(int)(cx - size * 0.5f), (int)(cy + rad + u.px(10)), size, u.row_h()};
  const bool hot_label = u.hovered(lr);
  {
    const int tw = u.font.text_width(label) + u.px(10);
    u.canvas.fill_round_rect({lr.x + (lr.w - tw) / 2, lr.y + u.px(1), tw, lr.h - u.px(2)}, u.px(3), Color::hex(0x000000, hot_label ? 150 : 100));
  }
  u.label(lr, label, hot_label ? u.theme.text_bright : u.theme.text, ui::Align::Center);
  if (hover_area && u.in.pressed[0] && drag_ == Drag::None) {
    if (hot_label) cam_.ortho = !cam_.ortho;  // Unity: click the label to switch projection
    else if (hot >= 0) {
      cam_.animate_to(cam_.pivot, axes[hot].yaw, axes[hot].pitch, cam_.distance, now_seconds());
      cam_.ortho = true;
    }
    else cam_.ortho = !cam_.ortho;
    u.consume_click();
  }
  if (hover_area) u.tooltip("Scene Gizmo: click an axis to look along it, click the centre to toggle Perspective/Isometric.\nBlender: the navigation gizmo / numpad 1, 3, 7 and 5.");
}

}  // namespace bl

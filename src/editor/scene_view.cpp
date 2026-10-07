// SPDX-License-Identifier: GPL-2.0-or-later
// Scene view & Game view.
//   Navigation  : Unity SceneView controls; Blender's equivalents live in
//                 blender/source/blender/editors/space_view3d/view3d_navigate_*.cc
//   Picking     : id buffer (blender/source/blender/editors/space_view3d/view3d_select.cc)
//   Gizmos      : Unity handles; Blender's are in blender/source/blender/editors/gizmo_library
//                 and blender/source/blender/editors/transform
//   Edit Mode   : Blender's mesh editing (blender/source/blender/editors/mesh)
#include "editor.h"

#include "../core/core.h"

#include <algorithm>
#include <map>
#include <array>
#include <unordered_map>
#include <cmath>

namespace bl {

using namespace platform;
using ui::Icon;

static const uint32_t kSelectOrange = Color::hex(0xF7941D);
static const uint32_t kChildOrange = Color::hex(0xC28A4A);

/* ===================================================================== */
/* Scene view                                                             */
/* ===================================================================== */

void Editor::draw_scene_view(const Recti &r) {
  auto &u = ui_;
  int bh = u.row_h() + u.px(4);
  Recti bar{r.x, r.y, r.w, bh};
  Recti view{r.x, r.y + bh, r.w, r.h - bh};
  scene_rect_ = view;
  draw_scene_overlay_bar(bar);
  if (view.w < 8 || view.h < 8) return;
  scene_hovered_ = u.hovered(view) && !u.any_popup_open() && !(last_op_valid() && last_op_rect_.contains(u.in.mx, u.in.my));
  scene_navigation(view);

  /* Gizmo math and picking need this frame's camera matrices before we
   * render, so transforms applied by a drag show up in the same frame. */
  scene_rt_.attach(fb_, view);
  scene_r3d_.begin(&scene_rt_, cam_.view(), cam_.proj(view.w / (float)view.h), LightingEnv(), raster_opt_);
  bool gizmo_busy = gizmo_update(view);

  auto &in = u.in;
  /* Selection clicks / box select (LMB without Alt). Uses last frame's id buffer. */
  if (drag_ == Drag::None && scene_hovered_ && in.pressed[0] && !in.alt() && !gizmo_busy && tool_ != Tool::View &&
      !u.any_active()) {
    drag_ = Drag::Box;
    drag_x_ = in.mx;
    drag_y_ = in.my;
  }
  /* Double-click an edge selects its edge loop (ProBuilder convention; Blender
   * uses Alt+click, which Unity reserves for orbiting). Shift adds. */
  if (edit_mode_ && drag_ == Drag::Box && in.double_clicked[0] && elem_ != EditElement::Face) {
    edit_select_loop(view, in.mx, in.my, in.shift());
    drag_ = Drag::None;
  }
  Recti box{std::min(drag_x_, in.mx), std::min(drag_y_, in.my), std::abs(in.mx - drag_x_), std::abs(in.my - drag_y_)};
  bool box_visible = false;
  if (drag_ == Drag::Box) {
    bool moved = box.w > u.px(4) || box.h > u.px(4);
    box_visible = moved;
    if (in.released[0] || !in.down[0]) {
      int mode = (in.shift() || in.ctrl()) ? SEL_TOGGLE : SEL_REPLACE;
      if (moved) {
        if (edit_mode_) edit_box_select(view, box, mode == SEL_TOGGLE ? SEL_ADD : SEL_REPLACE);
        else box_select(view, box, mode == SEL_TOGGLE ? SEL_ADD : SEL_REPLACE);
      }
      else {
        if (edit_mode_) edit_pick(view, in.mx, in.my, mode);
        else pick(view, in.mx, in.my, mode);
      }
      drag_ = Drag::None;
      box_visible = false;
    }
  }
  /* Double-click frames the selection (Unity: double-click in Hierarchy; Blender: numpad '.'). */
  if (scene_hovered_ && in.double_clicked[0] && !edit_mode_ && active_) frame_selected();

  render_scene_view(view);
  if (box_visible) {
    u.canvas.fill_rect(box.intersect(view), Color::hex(0x3A79BB, 40));
    u.canvas.rect_outline(box, Color::hex(0x6FA3DD));
  }

  draw_scene_icons(view);
  if (show_gizmos_) draw_gizmo(view);
  draw_view_gizmo(view);

  /* Statistics overlay (Blender: Viewport Overlays > Statistics). */
  if (show_stats_) {
    std::vector<std::string> lines;
    size_t verts = 0, faces = 0;
    for (GameObject *g : selected_objects(false))
      if (const Mesh *m = g->evaluated_mesh()) { verts += m->vert_count(); faces += m->face_count(); }
    if (edit_mode_ && edit_object()) {
      const Mesh &m = **edit_mesh_ptr();
      size_t vs = std::count(vert_sel_.begin(), vert_sel_.end(), 1), fsel = std::count(face_sel_.begin(), face_sel_.end(), 1);
      lines.push_back(strprintf("Edit Mode  %s%s", elem_ == EditElement::Vertex ? "(Vertex)" : elem_ == EditElement::Edge ? "(Edge)" : "(Face)",
                                proportional_ ? "  Proportional" : ""));
      lines.push_back(strprintf("Vertices  %zu / %zu", vs, m.vert_count()));
      lines.push_back(strprintf("Faces     %zu / %zu", fsel, m.face_count()));
    }
    else {
      lines.push_back(strprintf("Objects   %zu / %zu", selection_.size(), scene_->object_count()));
      if (!selection_.empty()) lines.push_back(strprintf("Selected  %zu verts, %zu faces", verts, faces));
    }
    lines.push_back(strprintf("Triangles %zu (%zu drawn)", scene_stats_.tris_submitted, scene_stats_.tris_rasterized));
    lines.push_back(strprintf("Render    %.2f ms  (%d culled)", scene_stats_.ms_total, scene_stats_.objects_culled));
    if (shading_ == Shading::Rendered)
      lines.push_back(strprintf("Path tracing  %d / %d samples  %.1f Mrays/s", vp_pt_.samples(),
                                scene_->render.viewport_samples, vp_pt_.stats().mrays_per_s()));
    if (shading_ == Shading::Shaded) lines.push_back(strprintf("Shading   %.2f ms (deferred PBR)", scene_stats_.ms_shade));
    int y = view.y + u.px(8);
    for (auto &l : lines) {
      u.canvas.text(u.font, view.x + u.px(11), y + 1, l, Color::hex(0x000000, 160));
      u.canvas.text(u.font, view.x + u.px(10), y, l, Color::hex(0xFFFFFF, 230));
      y += u.font.line_height();
    }
  }
  if (show_gizmos_ && !playing_) draw_camera_preview(view);
  if (edit_mode_ && last_op_valid()) draw_last_op_panel(view);
  else last_op_rect_ = Recti{};
  if (playing_) u.canvas.rect_outline(view, Color::hex(0x3A79BB, 120), u.px(2));
}

void Editor::draw_scene_overlay_bar(const Recti &bar) {
  auto &u = ui_;
  u.canvas.fill_rect(bar, Color::hex(0x2F2F2F));
  u.canvas.hline(bar.x, bar.right(), bar.bottom() - 1, u.theme.border);
  int h = bar.h - u.px(6), y = bar.y + u.px(3), x = bar.x + u.px(6);
  static const char *modes[] = {"Wireframe", "Solid", "Shaded", "Rendered (Path Traced)", "Shaded Wireframe"};
  int m = (int)shading_;
  if (u.combo(u.id("shading"), {x, y, u.px(170), h}, m, modes, 5)) shading_ = (Shading)m;
  u.tooltip("Draw mode (Blender: Z pie).\nSolid = fast vertex lighting (Workbench).\n"
            "Shaded = materials, textures, shadows (EEVEE / Unity Shaded).\n"
            "Rendered = progressive path tracing (Cycles).");
  x += u.px(176);
  if (u.icon_button({x, y, h, h}, Icon::Lightbulb, scene_lighting_, "Scene lighting on/off.\nOff = headlight from the camera, like Blender's Studio lighting.")) scene_lighting_ = !scene_lighting_;
  x += h + u.px(2);
  if (u.icon_button({x, y, h, h}, Icon::Grid, show_grid_, "Toggle grid (Blender: Overlays > Floor).")) show_grid_ = !show_grid_;
  x += h + u.px(2);
  if (u.icon_button({x, y, h, h}, Icon::Move, show_gizmos_, "Toggle Gizmos / transform handles.")) show_gizmos_ = !show_gizmos_;
  x += h + u.px(2);
  if (u.icon_button({x, y, h, h}, Icon::Chart, show_stats_, "Statistics overlay (Blender: Overlays > Statistics).")) show_stats_ = !show_stats_;
  x += h + u.px(10);
  /* Blender-style Edit Mode toggle - Unity has no built-in mesh editing (ProBuilder adds it). */
  int ew = u.font.text_width("Edit Mode (Tab)") + h + u.px(12);
  Recti er{bar.right() - ew - u.px(6), y, ew, h};
  bool can_edit = edit_mode_ || (active_object() && active_object()->get<MeshFilter>());
  bool hot = u.hovered(er);
  uint32_t bg = edit_mode_ ? u.theme.accent : (hot && can_edit ? u.theme.button_hover : u.theme.button);
  u.frame(er, bg, u.theme.border, u.px(3));
  u.draw_icon(Icon::Vertex, {er.x + u.px(5), er.y + u.px(3), h - u.px(6), h - u.px(6)}, can_edit ? 0xFFFFFFFF : u.theme.text_dim);
  u.canvas.text(u.font, er.x + h + u.px(2), er.y + (h - u.font.line_height()) / 2, "Edit Mode (Tab)", can_edit ? 0xFFFFFFFF : u.theme.text_dim);
  if (hot && can_edit && u.in.pressed[0]) { if (edit_mode_) exit_edit_mode(); else enter_edit_mode(); }
  u.label({er.x - u.px(150), y, u.px(140), h}, cam_.ortho ? "Iso" : "Persp", u.theme.text_dim, ui::Align::Right);
  (void)x;
}

void Editor::scene_navigation(const Recti &view) {
  auto &u = ui_;
  auto &in = u.in;
  double now = now_seconds();
  float k_pan = (2.0f * cam_.distance * std::tan(cam_.fov * 0.5f * kDeg2Rad)) / std::max(1, view.h);

  if (drag_ == Drag::None && scene_hovered_ && !u.any_active()) {
    if (in.pressed[1]) {
      drag_ = in.alt() ? Drag::Zoom : Drag::Fly;
      fly_last_ = now;
      fly_accel_ = 1.0f;
    }
    else if (in.pressed[2]) drag_ = Drag::Pan;
    else if (in.pressed[0] && in.alt()) drag_ = in.ctrl() ? Drag::Pan : Drag::Orbit;
    else if (in.pressed[0] && tool_ == Tool::View) drag_ = Drag::ViewTool;
    if (drag_ != Drag::None) {
      cam_.animating = false;
      drag_x_ = in.mx;
      drag_y_ = in.my;
    }
  }
  int dx = in.dx(), dy = in.dy();
  switch (drag_) {
    case Drag::Fly: {
      if (!in.down[1]) { drag_ = Drag::None; break; }
      u.cursor = Cursor::Move;
      Vec3 pos = cam_.position();
      cam_.yaw += dx * 0.25f;
      cam_.pitch = clampf(cam_.pitch + dy * 0.25f, -89.9f, 89.9f);
      /* WASD + QE fly, Shift = faster, holding accelerates (Unity scene view). */
      float dt = (float)std::min(0.1, now - fly_last_);
      fly_last_ = now;
      Vec3 mv(0.0f);
      if (in.key_down[KEY_W]) mv += cam_.forward();
      if (in.key_down[KEY_S]) mv -= cam_.forward();
      if (in.key_down[KEY_D]) mv += cam_.right();
      if (in.key_down[KEY_A]) mv -= cam_.right();
      if (in.key_down[KEY_E]) mv += Vec3(0, 1, 0);
      if (in.key_down[KEY_Q]) mv -= Vec3(0, 1, 0);
      if (in.wheel_y != 0) cam_.fly_speed = clampf(cam_.fly_speed * std::pow(1.2f, in.wheel_y), 0.01f, 100.0f);
      if (length_sq(mv) > 0) {
        fly_accel_ = std::min(fly_accel_ + dt * 2.0f, 6.0f);
        pos += normalize(mv) * (4.0f * cam_.fly_speed * fly_accel_ * (in.shift() ? 3.0f : 1.0f) * dt);
      }
      else fly_accel_ = 1.0f;
      cam_.pivot = pos + cam_.forward() * cam_.distance;
      break;
    }
    case Drag::Orbit:
      if (!in.down[0]) { drag_ = Drag::None; break; }
      u.cursor = Cursor::Move;
      cam_.yaw += dx * 0.3f;
      cam_.pitch = clampf(cam_.pitch + dy * 0.3f, -89.9f, 89.9f);
      break;
    case Drag::Pan:
    case Drag::ViewTool:
      if (!in.down[2] && !in.down[0]) { drag_ = Drag::None; break; }
      u.cursor = Cursor::Hand;
      cam_.pivot -= cam_.right() * (dx * k_pan);
      cam_.pivot += cam_.up() * (dy * k_pan);
      break;
    case Drag::Zoom:
      if (!in.down[1]) { drag_ = Drag::None; break; }
      cam_.distance = clampf(cam_.distance * std::exp((dx - dy) * -0.008f), 0.01f, 100000.0f);
      break;
    default: break;
  }
  if (scene_hovered_ && drag_ == Drag::None && in.wheel_y != 0) {
    cam_.animating = false;
    cam_.distance = clampf(cam_.distance * std::pow(0.88f, in.wheel_y), 0.01f, 100000.0f);
  }
}

Camera *Editor::main_camera(const Scene &s, GameObject **owner) {
  Camera *best = nullptr;
  GameObject *bo = nullptr;
  s.for_each_ordered([&](GameObject &g, int) {
    if (!g.active_in_hierarchy()) return;
    auto *c = g.get<Camera>();
    if (!c || !c->enabled) return;
    if (!best || (g.name == "Main Camera" && bo->name != "Main Camera")) { best = c; bo = &g; }
  });
  if (owner) *owner = bo;
  return best;
}

static LightingEnv build_env(const Scene &s) {
  LightingEnv env;
  env.sky = s.environment.sky;
  env.equator = s.environment.equator;
  env.ground = s.environment.ground;
  s.for_each([&](GameObject &g) {
    if (!g.active_in_hierarchy()) return;
    auto *l = g.get<Light>();
    if (!l || !l->enabled) return;
    RenderLight rl;
    rl.type = l->type == 1 ? RenderLight::Point : RenderLight::Directional;
    rl.direction = normalize(g.world_rotation().rotate({0, 0, 1}));
    rl.position = g.world_position();
    rl.color = l->color;
    rl.intensity = l->intensity;
    rl.range = l->range;
    env.lights.push_back(rl);
  });
  return env;
}

void Editor::submit_scene(Renderer3D &r3d, const Scene &s, bool game, LightingEnv &, Vec3) {
  s.for_each([&](GameObject &g) {
    if (!g.active_in_hierarchy()) return;
    auto *mr = g.get<MeshRenderer>();
    auto *mf = g.get<MeshFilter>();
    if (!mr || !mr->enabled || !mf || !mf->mesh) return;
    DrawItem it;
    bool editing = !game && edit_mode_ && g.id == edit_obj_;
    const Mesh *m = editing ? mf->mesh.get() : g.evaluated_mesh();
    if (!m) return;
    it.mesh = &m->render_mesh(editing);
    it.model = g.world_matrix();
    const Material &mat0 = *mr->material(0);
    it.albedo = mat0.base_color;
    it.specular = mat0.specular * 0.5f;
    it.unlit = mat0.unlit;
    it.double_sided = mat0.double_sided || editing;
    it.materials = &mr->materials;
    it.receive_shadows = mr->receive_shadows;
    it.id = (uint32_t)g.id;
    if (editing && elem_ == EditElement::Face) it.face_highlight = &face_sel_;
    r3d.add(it);
  });
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

void Editor::render_scene_view(const Recti &view) {
  scene_rt_.attach(fb_, view);
  float aspect = view.w / (float)std::max(1, view.h);
  Mat4 v = cam_.view(), p = cam_.proj(aspect);
  if (shading_ == Shading::Rendered) render_pathtraced_view(view);
  else if (shading_ == Shading::Shaded || shading_ == Shading::ShadedWireframe) {
    render_deferred(scene_r3d_, scene_rt_, v, p, cam_.position(), false, scene_lighting_, nullptr);
    scene_stats_ = scene_r3d_.stats();
  }
  else render_solid(view, v, p);
  if (show_grid_) draw_grid(scene_r3d_);
  render_overlays(view);
}

void Editor::render_solid(const Recti &, const Mat4 &v, const Mat4 &p) {
  LightingEnv env = build_env(*scene_);
  env.camera_pos = cam_.position();
  if (!scene_lighting_) {
    env.lights.clear();
    RenderLight head;
    head.direction = normalize(cam_.forward() + cam_.up() * -0.3f + cam_.right() * 0.2f);
    head.intensity = 0.85f;
    env.lights.push_back(head);
    env.sky = env.equator = env.ground = Vec3(0.35f);
  }
  scene_r3d_.begin(&scene_rt_, v, p, env, raster_opt_);
  scene_r3d_.clear_sky((p * v).inverse(), {0.33f, 0.47f, 0.68f}, {0.70f, 0.74f, 0.79f}, {0.27f, 0.26f, 0.25f});
  if (shading_ != Shading::Wireframe) {
    submit_scene(scene_r3d_, *scene_, false, env, env.camera_pos);
    scene_r3d_.flush();
  }
  scene_stats_ = scene_r3d_.stats();
}

void Editor::render_overlays(const Recti &) {
  /* Wireframes. */
  bool all_wire = shading_ == Shading::Wireframe || shading_ == Shading::ShadedWireframe;
  scene_->for_each([&](GameObject &g) {
    if (!g.active_in_hierarchy()) return;
    auto *mr = g.get<MeshRenderer>();
    if (!mr || !mr->enabled) return;
    if (edit_mode_ && g.id == edit_obj_) return;
    if (!all_wire && !mr->show_wireframe) return;
    const Mesh *m = g.evaluated_mesh();
    if (!m) return;
    const Mat4 &w = g.world_matrix();
    uint32_t col = is_selected(g.id) ? kSelectOrange : (shading_ == Shading::Wireframe ? Color::hex(0xD8D8D8, 200) : Color::hex(0x101010, 150));
    bool depth = shading_ != Shading::Wireframe;
    const auto &edges = m->edge_cache();
    if (edges.size() > 400000) return;  // keep the editor responsive on enormous meshes
    for (auto &e : edges) scene_r3d_.line(w.point(m->positions[e.first]), w.point(m->positions[e.second]), col, depth, 2e-4f);
  });

  /* Selection outline (orange, children lighter), Unity & Blender style. */
  if (!edit_mode_ && !selection_.empty() && shading_ != Shading::Wireframe) {
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

  /* Edit-mode overlay: edges + vertices (Blender's edit-mesh overlay). */
  if (edit_mode_) {
    if (GameObject *g = edit_object()) {
      const Mesh &m = **edit_mesh_ptr();
      const Mat4 &w = g->world_matrix();
      vert_sel_.resize(m.vert_count(), 0);
      face_sel_.resize(m.face_count(), 0);
      for (auto &e : m.edge_cache()) {
        bool s = vert_sel_[e.first] && vert_sel_[e.second];
        bool seam = !m.seams.empty() && m.is_seam(e.first, e.second);
        uint32_t col = s ? Color::hex(0xFFA733) : (seam ? Color::hex(0xFF3030) : Color::hex(0x0A0A0A, 220));
        scene_r3d_.line(w.point(m.positions[e.first]), w.point(m.positions[e.second]), col, true, 5e-4f);
      }
      if (elem_ == EditElement::Vertex) {
        float rad = std::max(1.5f, ui_.px(2.5f) * 1.0f);
        for (size_t i = 0; i < m.vert_count(); i++)
          scene_r3d_.point(w.point(m.positions[i]), rad, vert_sel_[i] ? Color::hex(0xFFA733) : Color::hex(0x000000), true);
      }
      else if (elem_ == EditElement::Face) {
        for (size_t f = 0; f < m.face_count(); f++)
          scene_r3d_.point(w.point(m.face_center(f)), ui_.px(2), face_sel_[f] ? Color::hex(0xFFA733) : Color::hex(0x202020), true);
      }
    }
  }
}

void Editor::draw_scene_icons(const Recti &view) {
  auto &u = ui_;
  scene_->for_each([&](GameObject &g) {
    if (!g.active_in_hierarchy()) return;
    auto *l = g.get<Light>();
    auto *c = g.get<Camera>();
    if (!l && !c) return;
    Vec2 sp;
    float z;
    if (!scene_r3d_.project(g.world_position(), sp, z)) return;
    float x = view.x + sp.x, y = view.y + sp.y;
    bool sel = is_selected(g.id);
    int s = u.px(22);
    u.canvas.fill_circle(x, y, s * 0.62f, Color::hex(sel ? 0x6A4A1A : 0x202020, 170));
    u.draw_icon(l ? Icon::Light : Icon::Camera, {(int)x - s / 2, (int)y - s / 2, s, s}, sel ? kSelectOrange : (l ? Color::hex(0xFFE27A) : Color::hex(0xDDDDDD)));
    if (sel && l && l->type == 0) {
      Vec3 p = g.world_position(), d = g.world_rotation().rotate({0, 0, 1});
      scene_r3d_.line(p, p + d * 2.0f, Color::hex(0xFFE27A), false);
    }
    if (sel && c) {
      /* Camera frustum (FoCG ch. 8.5). */
      Quat q = g.world_rotation();
      Vec3 p = g.world_position();
      float far_d = std::min(c->far_clip, 4.0f);
      float aspect = game_rect_.h > 0 ? game_rect_.w / (float)game_rect_.h : 16.0f / 9.0f;
      float hh = c->orthographic ? c->ortho_size : std::tan(c->fov * 0.5f * kDeg2Rad) * far_d, hw = hh * aspect;
      Vec3 corners[4] = {{-hw, -hh, far_d}, {hw, -hh, far_d}, {hw, hh, far_d}, {-hw, hh, far_d}};
      for (int i = 0; i < 4; i++) {
        Vec3 a = p + q.rotate(corners[i]), b = p + q.rotate(corners[(i + 1) % 4]);
        Vec3 o = c->orthographic ? p + q.rotate({corners[i].x, corners[i].y, 0}) : p;
        scene_r3d_.line(o, a, Color::hex(0xDDDDDD, 200), false);
        scene_r3d_.line(a, b, Color::hex(0xDDDDDD, 200), false);
      }
    }
  });
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
  Recti area{(int)(cx - size * 0.6f), (int)(cy - size * 0.6f), (int)(size * 1.2f), (int)(size * 1.4f)};
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
  u.label({(int)(cx - size * 0.5f), (int)(cy + rad + u.px(10)), size, u.row_h()}, cam_.ortho ? "< Iso" : "< Persp", u.theme.text, ui::Align::Center);
  if (hover_area && u.in.pressed[0] && drag_ == Drag::None) {
    if (hot >= 0) {
      cam_.animate_to(cam_.pivot, axes[hot].yaw, axes[hot].pitch, cam_.distance, now_seconds());
      cam_.ortho = true;
    }
    else cam_.ortho = !cam_.ortho;
    u.consume_click();
  }
  if (hover_area) u.tooltip("Scene Gizmo: click an axis to look along it, click the centre to toggle Perspective/Isometric.\nBlender: the navigation gizmo / numpad 1, 3, 7 and 5.");
}

/* ===================================================================== */
/* Picking                                                                */
/* ===================================================================== */

void Editor::pick(const Recti &view, int mx, int my, int mode) {
  /* Icon gizmos first (lights & cameras have no pixels in the id buffer). */
  uint64_t hit = 0;
  float best = (float)ui_.px(14);
  scene_->for_each([&](GameObject &g) {
    if (!g.active_in_hierarchy() || (!g.get<Light>() && !g.get<Camera>())) return;
    Vec2 sp;
    float z;
    if (!scene_r3d_.project(g.world_position(), sp, z)) return;
    float d = length(Vec2(view.x + sp.x - mx, view.y + sp.y - my));
    if (d < best) { best = d; hit = g.id; }
  });
  if (!hit) {
    uint32_t id = scene_rt_.id_at(mx - view.x, my - view.y);
    if (id)
      if (GameObject *g = scene_->find(id)) hit = g->id;
  }
  if (hit) select(hit, mode);
  else if (mode == SEL_REPLACE) clear_selection();
}

void Editor::box_select(const Recti &view, Recti box, int mode) {
  if (mode == SEL_REPLACE) clear_selection();
  Recti b = box.intersect(view);
  std::vector<uint32_t> ids;
  for (int y = b.y; y < b.bottom(); y++)
    for (int x = b.x; x < b.right(); x++) {
      uint32_t id = scene_rt_.id_at(x - view.x, y - view.y);
      if (id && (ids.empty() || ids.back() != id)) ids.push_back(id);
    }
  std::sort(ids.begin(), ids.end());
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  for (uint32_t id : ids)
    if (GameObject *g = scene_->find(id)) select(g->id, SEL_ADD);
  scene_->for_each([&](GameObject &g) {
    if (!g.get<Light>() && !g.get<Camera>()) return;
    Vec2 sp;
    float z;
    if (scene_r3d_.project(g.world_position(), sp, z) && box.contains((int)(view.x + sp.x), (int)(view.y + sp.y))) select(g.id, SEL_ADD);
  });
}

void Editor::frame_selected() {
  AABB b;
  if (edit_mode_ && edit_object()) {
    GameObject *g = edit_object();
    const Mesh &m = **edit_mesh_ptr();
    for (size_t i = 0; i < m.vert_count(); i++)
      if (i < vert_sel_.size() && vert_sel_[i]) b.add(g->world_matrix().point(m.positions[i]));
    if (!b.valid()) b = g->world_bounds();
  }
  else {
    for (GameObject *g : selected_objects(false)) {
      b.add(g->world_bounds());
      for (GameObject *c : g->children) b.add(c->world_bounds());
    }
  }
  if (!b.valid()) {
    /* Nothing selected: frame everything (Blender: Home / View All). */
    scene_->for_each([&](GameObject &g) { if (g.get<MeshFilter>()) b.add(g.world_bounds()); });
    if (!b.valid()) return;
  }
  float radius = std::max(0.25f, length(b.extent()));
  float dist = radius / std::sin(cam_.fov * 0.5f * kDeg2Rad) * 1.1f;
  cam_.animate_to(b.center(), cam_.yaw, cam_.pitch, dist, now_seconds());
}

/* ===================================================================== */
/* Transform gizmo (Unity handles)                                        */
/* ===================================================================== */

namespace {
enum GizmoHandle {
  H_MOVE_X = 0, H_MOVE_Y, H_MOVE_Z, H_PLANE_X, H_PLANE_Y, H_PLANE_Z, H_MOVE_FREE,
  H_ROT_X = 10, H_ROT_Y, H_ROT_Z, H_ROT_VIEW,
  H_SCALE_X = 20, H_SCALE_Y, H_SCALE_Z, H_SCALE_ALL
};
float dist_point_segment(Vec2 p, Vec2 a, Vec2 b) {
  Vec2 d = b - a;
  float l2 = dot(d, d);
  float t = l2 > 0 ? clampf(dot(p - a, d) / l2, 0, 1) : 0;
  return length(p - (a + d * t));
}
bool point_in_quad(Vec2 p, const Vec2 *q) {
  bool in = false;
  for (int i = 0, j = 3; i < 4; j = i++)
    if (((q[i].y > p.y) != (q[j].y > p.y)) && (p.x < (q[j].x - q[i].x) * (p.y - q[i].y) / (q[j].y - q[i].y) + q[i].x)) in = !in;
  return in;
}
}  // namespace

bool Editor::gizmo_update(const Recti &view) {
  auto &u = ui_;
  auto &in = u.in;
  gizmo_hot_ = -1;
  if (!show_gizmos_ || tool_ == Tool::View || (drag_ != Drag::None && drag_ != Drag::Gizmo)) return false;

  /* Targets & pivot. */
  GameObject *eo = edit_mode_ ? edit_object() : nullptr;
  std::vector<GameObject *> objs;
  Vec3 pivot;
  if (eo) {
    const Mesh &m = **edit_mesh_ptr();
    AABB b;
    Vec3 sum(0.0f);
    int n = 0;
    for (size_t i = 0; i < m.vert_count() && i < vert_sel_.size(); i++)
      if (vert_sel_[i]) {
        Vec3 w = eo->world_matrix().point(m.positions[i]);
        sum += w;
        b.add(w);
        n++;
      }
    if (!n) return false;
    pivot = pivot_center_ ? b.center() : sum / (float)n;
  }
  else {
    objs = selected_objects(true);
    if (objs.empty()) return false;
    if (pivot_center_) {
      AABB b;
      for (GameObject *g : objs) b.add(g->world_bounds());
      pivot = b.valid() ? b.center() : objs.back()->world_position();
    }
    else {
      GameObject *a = active_object();
      pivot = (a ? a : objs.back())->world_position();
    }
  }
  if (drag_ == Drag::Gizmo) pivot = gizmo_pivot_;
  Quat orient;
  GameObject *ref = eo ? eo : active_object();
  if ((space_local_ || tool_ == Tool::Scale) && ref) orient = ref->world_rotation();
  if (drag_ == Drag::Gizmo) orient = gizmo_orient_;
  Vec3 axis[3] = {orient.rotate({1, 0, 0}), orient.rotate({0, 1, 0}), orient.rotate({0, 0, 1})};

  /* Screen-constant size. */
  float dist = cam_.ortho ? cam_.distance : std::max(0.001f, dot(pivot - cam_.position(), cam_.forward()));
  float world_per_px = 2.0f * dist * std::tan(cam_.fov * 0.5f * kDeg2Rad) / std::max(1, view.h);
  float size = world_per_px * u.px(95);
  gizmo_size_ = size;
  if (drag_ != Drag::Gizmo) {
    gizmo_pivot_ = pivot;
    gizmo_orient_ = orient;
  }

  auto proj = [&](Vec3 p, Vec2 &s) {
    float z;
    bool ok = scene_r3d_.project(p, s, z);
    s.x += view.x;
    s.y += view.y;
    return ok;
  };
  Vec2 m{(float)in.mx, (float)in.my};
  Vec2 ps;
  if (!proj(pivot, ps)) return false;
  bool do_move = tool_ == Tool::Move || tool_ == Tool::Transform;
  bool do_rot = tool_ == Tool::Rotate || tool_ == Tool::Transform;
  bool do_scale = tool_ == Tool::Scale || tool_ == Tool::Transform;
  float thr = (float)u.px(7);

  /* Hover test (skip while dragging). */
  if (drag_ != Drag::Gizmo && scene_hovered_ && !u.any_active()) {
    float best = 1e9f;
    auto consider = [&](int h, float d) {
      if (d < thr && d < best) { best = d; gizmo_hot_ = h; }
    };
    if (do_scale) {
      Vec2 c;
      if (proj(pivot, c) && std::fabs(m.x - c.x) < u.px(8) && std::fabs(m.y - c.y) < u.px(8)) consider(H_SCALE_ALL, 0.0f);
    }
    if (do_move) {
      for (int k = 0; k < 3; k++) {
        Vec2 a, b;
        proj(pivot, a);
        proj(pivot + axis[k] * size, b);
        consider(H_MOVE_X + k, dist_point_segment(m, a, b) + 0.5f);
        /* plane handle: small square spanned by the other two axes */
        int i1 = (k + 1) % 3, i2 = (k + 2) % 3;
        Vec2 q[4];
        proj(pivot + (axis[i1] * 0.18f + axis[i2] * 0.18f) * size, q[0]);
        proj(pivot + (axis[i1] * 0.38f + axis[i2] * 0.18f) * size, q[1]);
        proj(pivot + (axis[i1] * 0.38f + axis[i2] * 0.38f) * size, q[2]);
        proj(pivot + (axis[i1] * 0.18f + axis[i2] * 0.38f) * size, q[3]);
        if (point_in_quad(m, q)) consider(H_PLANE_X + k, 1.0f);
      }
      if (tool_ == Tool::Move && length(m - ps) < u.px(7)) consider(H_MOVE_FREE, 0.2f);
    }
    if (do_scale && tool_ == Tool::Scale) {
      for (int k = 0; k < 3; k++) {
        Vec2 a, b;
        proj(pivot, a);
        proj(pivot + axis[k] * size, b);
        consider(H_SCALE_X + k, dist_point_segment(m, a, b) + 0.5f);
      }
    }
    if (do_rot) {
      float rscale = tool_ == Tool::Transform ? 1.25f : 1.0f;
      Vec3 to_cam = cam_.ortho ? -cam_.forward() : normalize(cam_.position() - pivot);
      for (int k = 0; k < 3; k++) {
        Vec3 u1 = normalize(cross(axis[k], std::fabs(axis[k].y) < 0.9f ? Vec3(0, 1, 0) : Vec3(1, 0, 0)));
        Vec3 u2 = cross(axis[k], u1);
        Vec2 prev;
        bool has_prev = false;
        for (int s = 0; s <= 64; s++) {
          float a = s / 64.0f * 2 * kPi;
          Vec3 d = u1 * std::cos(a) + u2 * std::sin(a);
          if (dot(d, to_cam) < -0.05f) { has_prev = false; continue; }
          Vec2 sp;
          proj(pivot + d * size * rscale, sp);
          if (has_prev) consider(H_ROT_X + k, dist_point_segment(m, prev, sp));
          prev = sp;
          has_prev = true;
        }
      }
      if (tool_ == Tool::Rotate) {
        float rr = size * 1.15f / world_per_px;
        consider(H_ROT_VIEW, std::fabs(length(m - ps) - rr));
      }
    }
  }

  /* Start drag. */
  if (drag_ != Drag::Gizmo && gizmo_hot_ >= 0 && in.pressed[0] && !in.alt()) {
    drag_ = Drag::Gizmo;
    gizmo_axis_ = gizmo_hot_;
    gizmo_press_x_ = in.mx;
    gizmo_press_y_ = in.my;
    gizmo_live_delta_ = Vec3(0.0f);
    gizmo_live_angle_ = 0;
    gizmo_live_scale_ = Vec3(1.0f);
    gizmo_starts_.clear();
    gizmo_vert_starts_.clear();
    if (eo) {
      const Mesh &mm = **edit_mesh_ptr();
      for (size_t i = 0; i < mm.vert_count(); i++) gizmo_vert_starts_.push_back(eo->world_matrix().point(mm.positions[i]));
      compute_proportional_weights(eo->world_matrix());
    }
    else {
      for (GameObject *g : objs) gizmo_starts_.push_back({g->id, g->world_matrix(), g->world_position(), g->world_rotation(), g->local().scale});
    }
    Ray ray = scene_r3d_.screen_ray((float)(in.mx - view.x), (float)(in.my - view.y));
    int h = gizmo_axis_;
    if (h <= H_MOVE_Z) gizmo_start_hit_ = pivot + axis[h] * closest_on_line_to_ray(pivot, axis[h], ray);
    else if (h <= H_MOVE_FREE) {
      Vec3 n = h == H_MOVE_FREE ? cam_.forward() : axis[h - H_PLANE_X];
      float t;
      gizmo_start_hit_ = ray_plane(ray, pivot, n, t) ? ray.origin + ray.dir * t : pivot;
    }
    gizmo_start_angle_ = std::atan2((float)in.my - ps.y, (float)in.mx - ps.x);
  }

  /* Apply drag. */
  if (drag_ == Drag::Gizmo) {
    u.cursor = Cursor::Move;
    /* Proportional editing: the wheel resizes the radius mid-drag (Blender). */
    if (eo && proportional_ && in.wheel_y != 0) {
      prop_radius_ = clampf(prop_radius_ * std::pow(1.15f, in.wheel_y), 0.001f, 10000.0f);
      compute_proportional_weights(eo->world_matrix());
    }
    auto vw = [&](size_t i) { return i < gizmo_vert_w_.size() ? gizmo_vert_w_[i] : 0.0f; };
    gizmo_hot_ = gizmo_axis_;
    bool snap = snap_ || in.ctrl();
    Ray ray = scene_r3d_.screen_ray((float)(in.mx - view.x), (float)(in.my - view.y));
    int h = gizmo_axis_;
    const char *label = "Move";
    if (h <= H_MOVE_FREE) {
      Vec3 hit;
      if (h <= H_MOVE_Z) hit = pivot + axis[h] * closest_on_line_to_ray(pivot, axis[h], ray);
      else {
        Vec3 n = h == H_MOVE_FREE ? cam_.forward() : axis[h - H_PLANE_X];
        float t;
        hit = ray_plane(ray, pivot, n, t) ? ray.origin + ray.dir * t : gizmo_start_hit_;
      }
      Vec3 delta = hit - gizmo_start_hit_;
      if (snap) {
        /* Snap the delta in gizmo space (Unity increment snapping). */
        Vec3 local{dot(delta, axis[0]), dot(delta, axis[1]), dot(delta, axis[2])};
        for (int k = 0; k < 3; k++) local[k] = std::round(local[k] / snap_move_) * snap_move_;
        delta = axis[0] * local.x + axis[1] * local.y + axis[2] * local.z;
      }
      gizmo_live_delta_ = delta;
      if (eo) {
        MeshPtr &mp = *edit_mesh_ptr();
        Mesh &mm = *mesh_make_mutable(mp);
        Mat4 inv = eo->world_matrix().inverse();
        for (size_t i = 0; i < mm.vert_count() && i < gizmo_vert_starts_.size(); i++)
          if (float k = vw(i)) mm.positions[i] = inv.point(gizmo_vert_starts_[i] + delta * k);
        mm.touch();
      }
      else
        for (auto &s : gizmo_starts_)
          if (GameObject *g = scene_->find(s.id)) g->set_world_position(s.pos + delta);
    }
    else if (h <= H_ROT_VIEW) {
      label = "Rotate";
      Vec3 ax = h == H_ROT_VIEW ? cam_.forward() : axis[h - H_ROT_X];
      /* Screen-space angle; sign derived by projecting a tiny rotation. */
      float a = std::atan2((float)in.my - ps.y, (float)in.mx - ps.x);
      float da = a - gizmo_start_angle_;
      while (da > kPi) da -= 2 * kPi;
      while (da < -kPi) da += 2 * kPi;
      Vec3 u1 = normalize(cross(ax, std::fabs(ax.y) < 0.9f ? Vec3(0, 1, 0) : Vec3(1, 0, 0)));
      Vec2 s0, s1;
      proj(pivot + u1 * size, s0);
      proj(pivot + Quat::axis_angle(ax, 0.05f).rotate(u1) * size, s1);
      float sa0 = std::atan2(s0.y - ps.y, s0.x - ps.x), sa1 = std::atan2(s1.y - ps.y, s1.x - ps.x);
      float ds = sa1 - sa0;
      while (ds > kPi) ds -= 2 * kPi;
      while (ds < -kPi) ds += 2 * kPi;
      float sign = ds >= 0 ? 1.0f : -1.0f;
      /* Accumulate so multiple turns work. */
      gizmo_live_angle_ += sign * da;
      gizmo_start_angle_ = a;
      float angle = gizmo_live_angle_;
      if (snap) angle = std::round(angle * kRad2Deg / snap_rot_) * snap_rot_ * kDeg2Rad;
      Quat q = Quat::axis_angle(ax, angle);
      if (eo) {
        MeshPtr &mp = *edit_mesh_ptr();
        Mesh &mm = *mesh_make_mutable(mp);
        Mat4 inv = eo->world_matrix().inverse();
        for (size_t i = 0; i < mm.vert_count() && i < gizmo_vert_starts_.size(); i++)
          if (float k = vw(i)) /* proportional: the angle fades with the weight */
            mm.positions[i] = inv.point(pivot + (k < 1.0f ? Quat::axis_angle(ax, angle * k) : q).rotate(gizmo_vert_starts_[i] - pivot));
        mm.touch();
      }
      else
        for (auto &s : gizmo_starts_)
          if (GameObject *g = scene_->find(s.id)) {
            g->set_world_rotation(normalize(q * s.rot));
            if (pivot_center_ || gizmo_starts_.size() > 1) g->set_world_position(pivot + q.rotate(s.pos - pivot));
          }
    }
    else {
      label = "Scale";
      Vec3 f(1.0f);
      if (h == H_SCALE_ALL) {
        float k = 1.0f + (in.mx - gizmo_press_x_) / (float)u.px(110);
        f = Vec3(std::max(0.001f, k));
      }
      else {
        int k = h - H_SCALE_X;
        Vec2 tip;
        proj(pivot + axis[k] * size, tip);
        Vec2 sa = tip - ps;
        float len = std::max(1.0f, length(sa));
        float along = dot(Vec2((float)(in.mx - gizmo_press_x_), (float)(in.my - gizmo_press_y_)), sa / len);
        f[k] = 1.0f + along / len;
      }
      if (snap)
        for (int k = 0; k < 3; k++) f[k] = std::round(f[k] / snap_scale_) * snap_scale_;
      gizmo_live_scale_ = f;
      if (eo) {
        MeshPtr &mp = *edit_mesh_ptr();
        Mesh &mm = *mesh_make_mutable(mp);
        Mat4 inv = eo->world_matrix().inverse();
        for (size_t i = 0; i < mm.vert_count() && i < gizmo_vert_starts_.size(); i++)
          if (float k = vw(i)) {
            Vec3 d = gizmo_vert_starts_[i] - pivot;
            Vec3 fk = Vec3(1.0f) + (f - Vec3(1.0f)) * k;
            Vec3 l{dot(d, axis[0]) * fk.x, dot(d, axis[1]) * fk.y, dot(d, axis[2]) * fk.z};
            mm.positions[i] = inv.point(pivot + axis[0] * l.x + axis[1] * l.y + axis[2] * l.z);
          }
        mm.touch();
      }
      else
        for (auto &s : gizmo_starts_)
          if (GameObject *g = scene_->find(s.id)) {
            g->set_local_scale(s.scale * f);
            if (gizmo_starts_.size() > 1) {
              Vec3 d = s.pos - pivot;
              Vec3 l{dot(d, axis[0]) * f.x, dot(d, axis[1]) * f.y, dot(d, axis[2]) * f.z};
              g->set_world_position(pivot + axis[0] * l.x + axis[1] * l.y + axis[2] * l.z);
            }
          }
    }
    mark_changed(label);
    if (!in.down[0]) {
      drag_ = Drag::None;
      gizmo_axis_ = -1;
      /* Faces dropped onto another face of the mesh merge into it. */
      if (edit_mode_ && auto_fuse_ && elem_ == EditElement::Face)
        if (MeshPtr *mp = edit_mesh_ptr())
          if (try_auto_fuse(*mesh_make_mutable(*mp))) {
            (*mp)->touch();
            mark_changed("Fuse on Contact");
          }
    }
    return true;
  }
  return gizmo_hot_ >= 0;
}

void Editor::draw_gizmo(const Recti &view) {
  auto &u = ui_;
  if (tool_ == Tool::View) return;
  bool has_target = edit_mode_ ? std::find(vert_sel_.begin(), vert_sel_.end(), 1) != vert_sel_.end() : !selection_.empty();
  if (!has_target) return;
  Vec3 pivot = gizmo_pivot_;
  Quat orient = gizmo_orient_;
  float size = gizmo_size_;
  Vec3 axis[3] = {orient.rotate({1, 0, 0}), orient.rotate({0, 1, 0}), orient.rotate({0, 0, 1})};
  uint32_t cols[3] = {u.theme.axis_x, u.theme.axis_y, u.theme.axis_z};
  int active = drag_ == Drag::Gizmo ? gizmo_axis_ : gizmo_hot_;
  auto proj = [&](Vec3 p, Vec2 &s) {
    float z;
    bool ok = scene_r3d_.project(p, s, z);
    s.x += view.x;
    s.y += view.y;
    return ok;
  };
  Vec2 c;
  if (!proj(pivot, c)) return;
  float th = (float)std::max(2, u.px(2.5f));
  u.canvas.push_clip(view);
  bool do_move = tool_ == Tool::Move || tool_ == Tool::Transform;
  bool do_rot = tool_ == Tool::Rotate || tool_ == Tool::Transform;
  bool do_scale = tool_ == Tool::Scale || tool_ == Tool::Transform;
  Vec3 to_cam = cam_.ortho ? -cam_.forward() : normalize(cam_.position() - pivot);
  if (do_rot) {
    float rscale = tool_ == Tool::Transform ? 1.25f : 1.0f;
    for (int k = 0; k < 3; k++) {
      Vec3 u1 = normalize(cross(axis[k], std::fabs(axis[k].y) < 0.9f ? Vec3(0, 1, 0) : Vec3(1, 0, 0)));
      Vec3 u2 = cross(axis[k], u1);
      uint32_t col = active == H_ROT_X + k ? u.theme.axis_hot : cols[k];
      Vec2 prev;
      bool has_prev = false;
      for (int s = 0; s <= 72; s++) {
        float a = s / 72.0f * 2 * kPi;
        Vec3 d = u1 * std::cos(a) + u2 * std::sin(a);
        bool front = dot(d, to_cam) >= -0.05f;
        Vec2 sp;
        proj(pivot + d * size * rscale, sp);
        if (has_prev && front) u.canvas.line(prev.x, prev.y, sp.x, sp.y, col, th);
        prev = sp;
        has_prev = front;
      }
    }
    if (tool_ == Tool::Rotate) {
      Vec2 r0;
      proj(pivot + cam_.right() * size * 1.15f, r0);
      float rr = length(r0 - c);
      u.canvas.circle(c.x, c.y, rr, active == H_ROT_VIEW ? u.theme.axis_hot : Color::hex(0xFFFFFF, 160), th * 0.8f);
      u.canvas.circle(c.x, c.y, length(r0 - c) / 1.15f, Color::hex(0x888888, 90), 1.0f);
    }
  }
  if (do_move) {
    for (int k = 0; k < 3; k++) {
      int i1 = (k + 1) % 3, i2 = (k + 2) % 3;
      Vec2 q[4];
      proj(pivot + (axis[i1] * 0.18f + axis[i2] * 0.18f) * size, q[0]);
      proj(pivot + (axis[i1] * 0.38f + axis[i2] * 0.18f) * size, q[1]);
      proj(pivot + (axis[i1] * 0.38f + axis[i2] * 0.38f) * size, q[2]);
      proj(pivot + (axis[i1] * 0.18f + axis[i2] * 0.38f) * size, q[3]);
      uint32_t col = active == H_PLANE_X + k ? u.theme.axis_hot : cols[k];
      u.canvas.fill_polygon(q, 4, Color::with_alpha(col, 0.35f));
      for (int e = 0; e < 4; e++) u.canvas.line(q[e].x, q[e].y, q[(e + 1) % 4].x, q[(e + 1) % 4].y, col, 1.2f);
    }
  }
  for (int k = 0; k < 3; k++) {
    if (!do_move && !(do_scale && tool_ == Tool::Scale)) break;
    Vec2 tip;
    proj(pivot + axis[k] * size, tip);
    bool hot = active == H_MOVE_X + k || active == H_SCALE_X + k;
    uint32_t col = hot ? u.theme.axis_hot : cols[k];
    /* Fade axes pointing at the camera (Unity hides them). */
    float facing = std::fabs(dot(axis[k], to_cam));
    if (facing > 0.985f) col = Color::with_alpha(col, 0.25f);
    u.canvas.line(c.x, c.y, tip.x, tip.y, col, th);
    Vec2 d = tip - c;
    float len = length(d);
    if (len < 1) continue;
    d = d / len;
    Vec2 n{-d.y, d.x};
    float hs = (float)u.px(7);
    if (do_move) u.canvas.fill_triangle(tip + d * (hs * 2.0f), tip + n * hs, tip - n * hs, col);
    else u.canvas.fill_rect({(int)(tip.x - hs * 0.8f), (int)(tip.y - hs * 0.8f), (int)(hs * 1.6f), (int)(hs * 1.6f)}, col);
  }
  if (do_scale) {
    int s = u.px(9);
    u.canvas.fill_rect({(int)c.x - s / 2, (int)c.y - s / 2, s, s}, active == H_SCALE_ALL ? u.theme.axis_hot : Color::hex(0xDDDDDD));
  }
  else if (tool_ == Tool::Move) {
    u.canvas.circle(c.x, c.y, (float)u.px(5), active == H_MOVE_FREE ? u.theme.axis_hot : Color::hex(0xDDDDDD), 1.5f);
  }
  u.canvas.pop_clip();
  /* Live readout (helps connect the numbers to the Inspector). */
  if (drag_ == Drag::Gizmo) {
    std::string t;
    if (gizmo_axis_ <= H_MOVE_FREE) t = strprintf("Move  %.3f, %.3f, %.3f", gizmo_live_delta_.x, gizmo_live_delta_.y, gizmo_live_delta_.z);
    else if (gizmo_axis_ <= H_ROT_VIEW) t = strprintf("Rotate  %.1f deg", gizmo_live_angle_ * kRad2Deg);
    else t = strprintf("Scale  %.3f, %.3f, %.3f", gizmo_live_scale_.x, gizmo_live_scale_.y, gizmo_live_scale_.z);
    if (snap_ || u.in.ctrl()) t += "  (snap)";
    Recti tr{u.in.mx + u.px(16), u.in.my + u.px(16), u.font.text_width(t) + u.px(12), u.row_h()};
    u.frame(tr, Color::hex(0x1E1E1E, 220), 0, u.px(3));
    u.canvas.text(u.font, tr.x + u.px(6), tr.y + u.px(3), t, u.theme.text_bright);
  }
}

/* ===================================================================== */
/* Edit mode                                                              */
/* ===================================================================== */

GameObject *Editor::edit_object() {
  if (!edit_mode_) return nullptr;
  GameObject *g = scene_->find(edit_obj_);
  if (!g) return nullptr;
  auto *mf = g->get<MeshFilter>();
  return mf && mf->mesh ? g : nullptr;
}

MeshPtr *Editor::edit_mesh_ptr() {
  GameObject *g = scene_->find(edit_obj_);
  return g ? &g->get<MeshFilter>()->mesh : nullptr;
}

void Editor::enter_edit_mode() {
  if (playing_) return;
  GameObject *g = active_object();
  if (!g || !g->get<MeshFilter>() || !g->get<MeshFilter>()->mesh) {
    Log::warn("Edit Mode needs an active object with a MeshFilter");
    return;
  }
  edit_mode_ = true;
  edit_obj_ = g->id;
  const Mesh &m = *g->get<MeshFilter>()->mesh;
  vert_sel_.assign(m.vert_count(), 0);
  face_sel_.assign(m.face_count(), 0);
  Log::info("Edit Mode on '%s' (Blender workflow): 1 = vertices, 3 = faces, Ctrl+E extrude, Ctrl+I inset, Tab to exit.", g->name.c_str());
}

void Editor::exit_edit_mode() {
  edit_mode_ = false;
  edit_obj_ = 0;
}

void Editor::sync_vert_face_selection(bool from_faces) {
  GameObject *g = edit_object();
  if (!g) return;
  const Mesh &m = **edit_mesh_ptr();
  vert_sel_.resize(m.vert_count(), 0);
  face_sel_.resize(m.face_count(), 0);
  if (from_faces) {
    std::fill(vert_sel_.begin(), vert_sel_.end(), 0);
    for (size_t f = 0; f < m.face_count(); f++)
      if (face_sel_[f])
        for (uint32_t k = 0; k < m.face_size(f); k++) vert_sel_[m.face_verts(f)[k]] = 1;
  }
  else {
    for (size_t f = 0; f < m.face_count(); f++) {
      bool all = m.face_size(f) > 0;
      for (uint32_t k = 0; k < m.face_size(f); k++) all = all && vert_sel_[m.face_verts(f)[k]];
      face_sel_[f] = all;
    }
  }
}

void Editor::edit_select_all(bool sel) {
  if (!edit_object()) return;
  std::fill(vert_sel_.begin(), vert_sel_.end(), sel ? 1 : 0);
  std::fill(face_sel_.begin(), face_sel_.end(), sel ? 1 : 0);
}

void Editor::edit_pick(const Recti &view, int mx, int my, int mode) {
  GameObject *g = edit_object();
  if (!g) return;
  const Mesh &m = **edit_mesh_ptr();
  vert_sel_.resize(m.vert_count(), 0);
  face_sel_.resize(m.face_count(), 0);
  const Mat4 &w = g->world_matrix();
  if (mode == SEL_REPLACE) edit_select_all(false);
  if (elem_ == EditElement::Vertex) {
    int best = -1;
    float bd = (float)ui_.px(12);
    for (size_t i = 0; i < m.vert_count(); i++) {
      Vec2 s;
      float z;
      if (!scene_r3d_.project(w.point(m.positions[i]), s, z)) continue;
      float d = length(Vec2(view.x + s.x - mx, view.y + s.y - my));
      if (d >= bd) continue;
      if (z > scene_rt_.depth_at((int)s.x, (int)s.y) + 2e-3f) continue;  // hidden behind something
      bd = d;
      best = (int)i;
    }
    if (best >= 0) vert_sel_[best] = mode == SEL_TOGGLE ? !vert_sel_[best] : 1;
    sync_vert_face_selection(false);
  }
  else if (elem_ == EditElement::Edge) {
    uint32_t a, b;
    if (edit_pick_edge(view, mx, my, a, b)) {
      uint8_t on = mode == SEL_TOGGLE ? !(vert_sel_[a] && vert_sel_[b]) : 1;
      vert_sel_[a] = vert_sel_[b] = on;
    }
    sync_vert_face_selection(false);
  }
  else {
    Ray ray = scene_r3d_.screen_ray((float)(mx - view.x), (float)(my - view.y));
    Mat4 inv = w.inverse();
    Ray lr{inv.point(ray.origin), normalize(inv.dir(ray.dir))};
    const RenderMesh &rm = m.render_mesh(true);
    float bt = 1e30f;
    int bf = -1;
    if (ray_aabb(lr, rm.bounds) >= 0)
      for (size_t t = 0; t < rm.tri_count(); t++) {
        float d = ray_triangle(lr, rm.positions[rm.indices[t * 3]], rm.positions[rm.indices[t * 3 + 1]], rm.positions[rm.indices[t * 3 + 2]]);
        if (d > 0 && d < bt) { bt = d; bf = (int)rm.tri_face[t]; }
      }
    if (bf >= 0) face_sel_[bf] = mode == SEL_TOGGLE ? !face_sel_[bf] : 1;
    sync_vert_face_selection(true);
  }
}

void Editor::edit_box_select(const Recti &view, Recti box, int mode) {
  GameObject *g = edit_object();
  if (!g) return;
  const Mesh &m = **edit_mesh_ptr();
  vert_sel_.resize(m.vert_count(), 0);
  face_sel_.resize(m.face_count(), 0);
  if (mode == SEL_REPLACE) edit_select_all(false);
  const Mat4 &w = g->world_matrix();
  auto visible_in_box = [&](Vec3 p) {
    Vec2 s;
    float z;
    if (!scene_r3d_.project(w.point(p), s, z)) return false;
    if (!box.contains((int)(view.x + s.x), (int)(view.y + s.y))) return false;
    return z <= scene_rt_.depth_at((int)s.x, (int)s.y) + 2e-3f;
  };
  if (elem_ != EditElement::Face) {
    for (size_t i = 0; i < m.vert_count(); i++)
      if (visible_in_box(m.positions[i])) vert_sel_[i] = 1;
    sync_vert_face_selection(false);
  }
  else {
    for (size_t f = 0; f < m.face_count(); f++)
      if (visible_in_box(m.face_center(f))) face_sel_[f] = 1;
    sync_vert_face_selection(true);
  }
}

void Editor::edit_op(const std::string &op) {
  GameObject *g = edit_object();
  if (!g) return;
  MeshPtr &mp = *edit_mesh_ptr();
  Mesh &m = *mesh_make_mutable(mp);
  vert_sel_.resize(m.vert_count(), 0);
  face_sel_.resize(m.face_count(), 0);
  if (elem_ != EditElement::Face) {
    /* Derive faces from vertices like Blender does when switching modes. */
    for (size_t f = 0; f < m.face_count(); f++) {
      bool all = true;
      for (uint32_t k = 0; k < m.face_size(f); k++) all = all && vert_sel_[m.face_verts(f)[k]];
      face_sel_[f] = all;
    }
  }
  size_t nsel = std::count(face_sel_.begin(), face_sel_.end(), 1);
  if (edit_op_redoable(op)) {
    if ((op == "extrude" || op == "inset" || op == "push_through") && !nsel) {
      Log::warn("Select faces first (press 3 for face mode)");
      return;
    }
    LastOp L;
    L.op = op;
    L.obj = g->id;
    L.before = std::make_shared<Mesh>(m);
    L.vsel = vert_sel_;
    L.fsel = face_sel_;
    L.elem = elem_;
    L.fuse = auto_fuse_;
    if (op == "extrude") L.amount = extrude_dist_;
    else if (op == "inset") L.amount = inset_amount_;
    else if (op == "bevel") { L.amount = bevel_width_; L.segments = bevel_segments_; }
    else if (op == "bridge") L.segments = bridge_segments_;
    else if (op == "subdivide_edges") L.segments = subdivide_cuts_;
    else if (op == "loopcut") { L.segments = loop_cuts_; L.amount = loop_slide_; }
    else if (op == "smooth") L.amount = smooth_factor_;
    last_op_ = std::move(L);
    run_last_op(true);
    return;
  }
  if (op == "dissolve" || op == "connect" || op == "collapse") {
    size_t n = op == "dissolve" ? meshops::dissolve_edges(m, vert_sel_)
               : op == "connect" ? meshops::connect_vertices(m, vert_sel_)
                                 : meshops::collapse_edges(m, vert_sel_);
    if (!n) {
      Log::warn(op == "connect" ? "Connect: select two vertices of one face that aren't neighbours (Blender: J)"
                                : "Select edges first (press 2 for edge mode)");
      return;
    }
    vert_sel_.resize(m.vert_count(), 0);
    face_sel_.assign(m.face_count(), 0);
    sync_vert_face_selection(false);
    Log::info("%s: %zu %s", op == "dissolve" ? "Dissolved" : op == "connect" ? "Connected" : "Collapsed", n,
              op == "connect" ? "face split(s)" : "edge(s)");
  }
  else if (op == "fuse") {
    if (!try_auto_fuse(m)) { Log::warn("Fuse: the selected faces don't lie on another face of the mesh"); return; }
  }
  else if (op == "delete") {
    if (elem_ != EditElement::Face) {
      /* Vertex mode: faces using a selected vertex; edge mode: faces using a selected edge. */
      for (size_t f = 0; f < m.face_count(); f++) {
        const uint32_t *v = m.face_verts(f);
        uint32_t n = m.face_size(f);
        bool any = false;
        for (uint32_t k = 0; k < n; k++)
          any = any || (elem_ == EditElement::Vertex ? vert_sel_[v[k]] : vert_sel_[v[k]] && vert_sel_[v[(k + 1) % n]]);
        face_sel_[f] = any;
      }
    }
    meshops::delete_faces(m, face_sel_);
    vert_sel_.assign(m.vert_count(), 0);
    face_sel_.assign(m.face_count(), 0);
  }
  else if (op == "smooth") {
    meshops::smooth_laplacian(m, smooth_factor_, 1, &vert_sel_);
  }
  else if (op == "fill") {
    if (!meshops::fill(m, vert_sel_)) { Log::warn("Fill needs 3 or more selected vertices (Blender: F)"); return; }
    face_sel_.resize(m.face_count(), 0);
    face_sel_.back() = 1;
    Log::info("Filled a %u-sided face", m.face_size(m.face_count() - 1));
  }
  else if (op == "merge_center") {
    size_t n = meshops::merge_at_center(m, vert_sel_);
    if (!n) { Log::warn("Select 2 or more vertices to merge"); return; }
    vert_sel_.assign(m.vert_count(), 0);
    face_sel_.assign(m.face_count(), 0);
    Log::info("Merged %zu vertices at center", n + 1);
  }
  else if (op == "recalc_normals") {
    meshops::recalc_normals_outside(m);
    Log::info("Recalculated normals (outside)");
  }
  else if (op == "loopcut" || op == "select_loop") {
    /* Uses the selected edge (exactly two adjacent selected vertices). */
    uint32_t a = UINT32_MAX, b = UINT32_MAX;
    for (auto &e : m.edge_cache())
      if (vert_sel_[e.first] && vert_sel_[e.second]) {
        if (a != UINT32_MAX) { a = UINT32_MAX; break; }
        a = e.first;
        b = e.second;
      }
    if (a == UINT32_MAX) { Log::warn("Select exactly one edge (press 2 for edge mode)"); return; }
    if (op == "loopcut") {
      auto made = meshops::loop_cut(m, a, b, loop_cuts_, loop_slide_);
      if (made.empty()) { Log::warn("Loop Cut needs quads around the edge"); return; }
      vert_sel_.assign(m.vert_count(), 0);
      for (uint32_t v : made) vert_sel_[v] = 1;
      Log::info("Loop Cut: %zu new vertices", made.size());
    }
    else {
      auto loop = meshops::edge_loop(m, a, b);
      for (uint32_t v : loop) vert_sel_[v] = 1;
      sync_vert_face_selection(false);
      Log::info("Edge loop: %zu vertices", loop.size());
      return;  // selection only
    }
    face_sel_.assign(m.face_count(), 0);
    sync_vert_face_selection(false);
  }
  m.touch();
  mark_changed("Edit: " + op);
}

void Editor::set_edit_element(EditElement e) {
  EditElement old = elem_;
  elem_ = e;
  if (e == EditElement::Face) sync_vert_face_selection(false);
  else if (old == EditElement::Face) sync_vert_face_selection(true);
}

bool Editor::edit_pick_edge(const Recti &view, int mx, int my, uint32_t &a, uint32_t &b) {
  GameObject *g = edit_object();
  if (!g) return false;
  const Mesh &m = **edit_mesh_ptr();
  const Mat4 &w = g->world_matrix();
  Vec2 mp((float)(mx - view.x), (float)(my - view.y));
  float bd = (float)ui_.px(10);
  bool found = false;
  for (auto &e : m.edge_cache()) {
    Vec2 s0, s1;
    float z0, z1;
    if (!scene_r3d_.project(w.point(m.positions[e.first]), s0, z0) || !scene_r3d_.project(w.point(m.positions[e.second]), s1, z1)) continue;
    Vec2 d = s1 - s0;
    float l2 = dot(d, d);
    float t = l2 > 1e-6f ? clampf(dot(mp - s0, d) / l2, 0.0f, 1.0f) : 0.0f;
    Vec2 c = s0 + d * t;
    float dist = length(mp - c);
    if (dist >= bd) continue;
    /* NDC depth is affine in screen space, so lerping it along the edge is exact. */
    if (z0 + (z1 - z0) * t > scene_rt_.depth_at((int)c.x, (int)c.y) + 2e-3f) continue;
    bd = dist;
    a = e.first;
    b = e.second;
    found = true;
  }
  return found;
}

void Editor::edit_select_loop(const Recti &view, int mx, int my, bool add) {
  GameObject *g = edit_object();
  uint32_t a, b;
  if (!g || !edit_pick_edge(view, mx, my, a, b)) return;
  const Mesh &m = **edit_mesh_ptr();
  vert_sel_.resize(m.vert_count(), 0);
  if (!add) edit_select_all(false);
  auto loop = meshops::edge_loop(m, a, b);
  for (uint32_t v : loop) vert_sel_[v] = 1;
  if (elem_ == EditElement::Face) elem_ = EditElement::Edge;
  sync_vert_face_selection(false);
  Log::info("Edge loop: %zu vertices", loop.size());
}

void Editor::edit_loop_cut_at(const Recti &view, int mx, int my) {
  GameObject *g = edit_object();
  uint32_t a, b;
  if (!g) return;
  if (!edit_pick_edge(view, mx, my, a, b)) {
    edit_op("loopcut");  // fall back to the selected edge
    return;
  }
  /* Cut at the hovered edge: select it and run the (re-adjustable) operator. */
  const Mesh &m = **edit_mesh_ptr();
  vert_sel_.assign(m.vert_count(), 0);
  vert_sel_[a] = vert_sel_[b] = 1;
  face_sel_.assign(m.face_count(), 0);
  if (elem_ == EditElement::Face) elem_ = EditElement::Edge;
  edit_op("loopcut");
}

void Editor::compute_proportional_weights(const Mat4 &world) {
  (void)world;
  const size_t n = gizmo_vert_starts_.size();
  gizmo_vert_w_.assign(n, 0.0f);
  for (size_t i = 0; i < n && i < vert_sel_.size(); i++) gizmo_vert_w_[i] = vert_sel_[i] ? 1.0f : 0.0f;
  if (!proportional_ || prop_radius_ <= 0) return;
  /* Distance to the nearest selected vertex through a uniform grid with
   * cell = radius, so only 27 cells are visited per vertex instead of every
   * selected vertex (Blender: editors/transform, proportional editing). */
  const float r = prop_radius_, inv = 1.0f / r;
  auto cell = [&](Vec3 p) { return std::array<int, 3>{(int)std::floor(p.x * inv), (int)std::floor(p.y * inv), (int)std::floor(p.z * inv)}; };
  auto key = [](int x, int y, int z) {
    return ((uint64_t)(uint32_t)(x * 73856093) ^ ((uint64_t)(uint32_t)(y * 19349663) << 21) ^ ((uint64_t)(uint32_t)(z * 83492791) << 42));
  };
  std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
  for (size_t i = 0; i < n; i++)
    if (gizmo_vert_w_[i] > 0) {
      auto c = cell(gizmo_vert_starts_[i]);
      grid[key(c[0], c[1], c[2])].push_back((uint32_t)i);
    }
  if (grid.empty()) return;
  for (size_t i = 0; i < n; i++) {
    if (gizmo_vert_w_[i] > 0) continue;
    Vec3 p = gizmo_vert_starts_[i];
    auto c = cell(p);
    float best = r * r;
    for (int dz = -1; dz <= 1; dz++)
      for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
          auto it = grid.find(key(c[0] + dx, c[1] + dy, c[2] + dz));
          if (it == grid.end()) continue;
          for (uint32_t j : it->second) best = std::min(best, length_sq(gizmo_vert_starts_[j] - p));
        }
    if (best >= r * r) continue;
    float t = 1.0f - std::sqrt(best) / r;  // 1 at the selection, 0 at the radius
    float wgt;
    switch (prop_falloff_) {
      case 1: wgt = std::sqrt(std::max(0.0f, 2.0f * t - t * t)); break;  // sphere
      case 2: wgt = std::sqrt(t); break;                                  // root
      case 3: wgt = t * t; break;                                         // sharp
      case 4: wgt = t; break;                                             // linear
      case 5: wgt = 1.0f; break;                                          // constant
      default: wgt = t * t * (3.0f - 2.0f * t); break;                    // smooth
    }
    gizmo_vert_w_[i] = wgt;
  }
}

/* ===================================================================== */
/* Game view                                                              */
/* ===================================================================== */

void Editor::draw_game_view(const Recti &r) {
  auto &u = ui_;
  int bh = u.row_h() + u.px(4);
  Recti bar{r.x, r.y, r.w, bh};
  u.canvas.fill_rect(bar, Color::hex(0x2F2F2F));
  u.label({bar.x + u.px(8), bar.y, u.px(80), bar.h}, "Display 1", u.theme.text);
  u.label({bar.x + u.px(90), bar.y, u.px(110), bar.h}, "Free Aspect", u.theme.text_dim);
  int h = bh - u.px(6);
  Recti sr{bar.right() - u.px(70), bar.y + u.px(3), u.px(62), h};
  if (u.button(sr, "Stats", game_stats_overlay_)) game_stats_overlay_ = !game_stats_overlay_;
  u.tooltip("Rendering statistics (Unity: Game view > Stats).");
  Recti view{r.x, r.y + bh, r.w, r.h - bh};
  game_rect_ = view;
  if (view.w < 8 || view.h < 8) return;
  GameObject *owner = nullptr;
  Camera *cam = main_camera(*scene_, &owner);
  if (!cam) {
    u.canvas.fill_rect(view, 0xFF000000);
    u.label(view, "Display 1  -  No cameras rendering", u.theme.text, ui::Align::Center);
    return;
  }
  ScopedTimer t;
  game_rt_.attach(fb_, view);
  float aspect = view.w / (float)view.h;
  Quat q = owner->world_rotation();
  Vec3 eye = owner->world_position();
  Mat4 v = Mat4::look_at(eye, eye + q.rotate({0, 0, 1}), q.rotate({0, 1, 0}));
  Mat4 p = cam->orthographic ? Mat4::ortho(cam->ortho_size, aspect, cam->near_clip, cam->far_clip)
                             : Mat4::perspective(cam->fov * kDeg2Rad, aspect, cam->near_clip, cam->far_clip);
  /* The Game view always uses the full material pipeline with shadows (Unity Game view). */
  render_deferred(game_r3d_, game_rt_, v, p, eye, true, true, cam);
  game_stats_ = game_r3d_.stats();
  if (game_stats_overlay_) {
    Recti box{view.right() - u.px(250), view.y + u.px(8), u.px(240), u.row_h() * 6 + u.px(10)};
    u.frame(box, Color::hex(0x101010, 200), 0, u.px(4));
    float ms = (float)t.ms();
    std::string lines[] = {"Statistics",
                           strprintf("CPU: render %.2f ms  (%.0f FPS)", ms, ms > 0 ? 1000.0f / ms : 0.0f),
                           strprintf("Tris: %zu  drawn: %zu", game_stats_.tris_submitted, game_stats_.tris_rasterized),
                           strprintf("Objects: %d  culled: %d", game_stats_.objects_submitted, game_stats_.objects_culled),
                           strprintf("Screen: %dx%d", view.w, view.h),
                           strprintf("Threads: %s", raster_opt_.multithreaded ? "all cores" : "1")};
    for (int i = 0; i < 6; i++) u.canvas.text(u.font, box.x + u.px(8), box.y + u.px(5) + i * u.row_h(), lines[i], i == 0 ? u.theme.text_bright : u.theme.text);
  }
  if (playing_ && focused_ != WindowKind::Game) {
    u.label({view.x, view.bottom() - u.row_h() - u.px(6), view.w, u.row_h()}, "Click the Game view to give it keyboard focus (WASD / arrows drive PlayerController)", Color::hex(0xFFFFFF, 200), ui::Align::Center);
  }
}

/* ===================================================================== */
/* Adjust Last Operation (Blender's redo panel, F9)                       */
/* ===================================================================== */

bool Editor::edit_op_redoable(const std::string &op) const {
  return op == "extrude" || op == "inset" || op == "bevel" || op == "bridge" || op == "push_through" || op == "subdivide_edges" ||
         op == "loopcut" || op == "smooth";
}

bool Editor::last_op_valid() {
  if (last_op_.op.empty() || !edit_mode_ || edit_obj_ != last_op_.obj) return false;
  MeshPtr *mp = edit_mesh_ptr();
  return mp && mp->get() == last_op_.result && (*mp)->version == last_op_.result_version;
}

bool Editor::try_auto_fuse(Mesh &m) {
  face_sel_.resize(m.face_count(), 0);
  if (std::find(face_sel_.begin(), face_sel_.end(), 1) == face_sel_.end()) return false;
  std::vector<uint8_t> fs = face_sel_;
  if (!meshops::fuse_contacts(m, fs)) return false;
  face_sel_.assign(m.face_count(), 0);
  vert_sel_.assign(m.vert_count(), 0);
  Log::info("Fused: the selection touched another face, so the two were joined (the touching area became an opening)");
  return true;
}

void Editor::run_last_op(bool first) {
  LastOp &L = last_op_;
  GameObject *g = scene_->find(L.obj);
  MeshFilter *mf = g ? g->get<MeshFilter>() : nullptr;
  if (!mf || !L.before) {
    L.op.clear();
    return;
  }
  /* Start again from the mesh as it was before the operator. */
  mf->mesh = std::make_shared<Mesh>(*L.before);
  Mesh &m = *mf->mesh;
  m.version = L.before->version + 1 + (++redo_serial_);  // never reuse an old version number
  vert_sel_ = L.vsel;
  face_sel_ = L.fsel;
  elem_ = L.elem;
  std::string err;
  bool ok = true;
  const std::string &op = L.op;
  if (op == "extrude" || op == "inset") {
    if (op == "extrude") meshops::extrude_faces(m, face_sel_, L.amount);
    else meshops::inset_faces(m, face_sel_, L.amount);
    vert_sel_.assign(m.vert_count(), 0);
    sync_vert_face_selection(true);
  }
  else if (op == "bevel") ok = meshops::bevel_edges(m, vert_sel_, face_sel_, L.amount, L.segments, &err);
  else if (op == "bridge") {
    ok = meshops::bridge(m, vert_sel_, face_sel_, L.segments, L.twist, L.smooth, L.path, &err);
    if (ok && L.elem == EditElement::Face) sync_vert_face_selection(true);
  }
  else if (op == "push_through") {
    ok = meshops::push_through(m, face_sel_, L.segments, &err);
    if (ok) sync_vert_face_selection(true);
  }
  else if (op == "subdivide_edges") {
    ok = meshops::subdivide_edges(m, vert_sel_, L.segments) > 0;
    if (!ok) err = "Select edges first (press 2 for edge mode)";
    face_sel_.assign(m.face_count(), 0);
    sync_vert_face_selection(false);
  }
  else if (op == "loopcut") {
    uint32_t a = UINT32_MAX, b = UINT32_MAX;
    for (auto &e : m.edge_cache())
      if (e.first < vert_sel_.size() && e.second < vert_sel_.size() && vert_sel_[e.first] && vert_sel_[e.second]) {
        if (a != UINT32_MAX) {
          a = UINT32_MAX;
          break;
        }
        a = e.first;
        b = e.second;
      }
    std::vector<uint32_t> made;
    if (a != UINT32_MAX) made = meshops::loop_cut(m, a, b, L.segments, L.amount);
    ok = !made.empty();
    if (!ok) err = a == UINT32_MAX ? "Select exactly one edge (press 2 for edge mode)" : "Loop Cut needs quads around the edge";
    vert_sel_.assign(m.vert_count(), 0);
    for (uint32_t v : made) vert_sel_[v] = 1;
    face_sel_.assign(m.face_count(), 0);
    sync_vert_face_selection(false);
  }
  else if (op == "smooth") {
    meshops::smooth_laplacian(m, L.amount, std::max(1, L.segments), &vert_sel_);
  }
  if (!ok) {
    mf->mesh = L.before;  // back to the untouched mesh (copied on the next edit)
    vert_sel_ = L.vsel;
    face_sel_ = L.fsel;
    Log::warn("%s", err.c_str());
    L.message = err;
    if (first) {
      L.op.clear();
      return;
    }
  }
  else {
    L.message.clear();
    vert_sel_.resize(m.vert_count(), 0);
    face_sel_.resize(m.face_count(), 0);
    /* The panel's Move: shift the result in X/Y/Z (global or local) and along
     * the selection's normal, like adjusting Blender's extrude afterwards. */
    if (length_sq(L.move) > 0 || L.along_normal != 0) {
      Vec3 n(0.0f);
      for (size_t f = 0; f < m.face_count(); f++)
        if (face_sel_[f]) n += m.face_normal(f);
      if (length_sq(n) < 1e-12f) {
        std::vector<Vec3> vn = meshops::vertex_normals(m);
        for (size_t v = 0; v < m.vert_count(); v++)
          if (vert_sel_[v]) n += vn[v];
      }
      n = length_sq(n) > 1e-12f ? normalize(n) : Vec3(0.0f);
      Vec3 d = (L.orientation == 0 ? g->world_matrix().inverse().dir(L.move) : L.move) + n * L.along_normal;
      for (size_t v = 0; v < m.vert_count(); v++)
        if (vert_sel_[v]) m.positions[v] += d;
    }
    if (L.fuse && op == "extrude") try_auto_fuse(m);
    m.touch();
  }
  L.result = mf->mesh.get();
  L.result_version = mf->mesh->version;
  std::string label = "Edit: " + op;
  if (first) {
    if (ok) Log::info("%s done. Adjust it in the panel at the bottom left of the Scene view (F9).", op.c_str());
  }
  else if (!pending_change_ && !undo_.empty() && undo_.back().label == label) {
    /* Amend the operator's undo step instead of stacking a new one. */
    stable_ = std::move(undo_.back().scene);
    undo_.pop_back();
  }
  mark_changed(label);
}

void Editor::draw_last_op_panel(const Recti &view) {
  auto &u = ui_;
  LastOp &L = last_op_;
  static const std::map<std::string, std::string> titles = {
      {"extrude", "Extrude Faces"},    {"inset", "Inset Faces"},
      {"bevel", "Bevel"},              {"bridge", "Bridge"},
      {"push_through", "Push Through"}, {"subdivide_edges", "Subdivide Edges"},
      {"loopcut", "Loop Cut"},         {"smooth", "Smooth Vertices"}};
  auto it = titles.find(L.op);
  std::string title = it == titles.end() ? L.op : it->second;
  const int rh = u.row_h(), pad = u.px(6), w = std::min(u.px(270), view.w - u.px(20));
  /* Rows: the operator's own fields, then Move, Along Normal, Orientation. */
  int fields = L.op == "extrude" || L.op == "bevel" || L.op == "loopcut" || L.op == "smooth" ? 2 : L.op == "bridge" ? 4 : 1;
  int rows = last_op_open_ ? fields + 3 + (L.message.empty() ? 0 : 1) : 0;
  int h = rh + u.px(6) + rows * (rh + u.px(2)) + (rows ? pad : 0);
  Recti r{view.x + u.px(10), view.bottom() - h - u.px(10), w, h};
  last_op_rect_ = r;
  u.frame(r, Color::hex(0x262626, 235), Color::hex(0x101010), u.px(4));
  Recti hr{r.x + pad, r.y + u.px(3), r.w - pad * 2, rh};
  int a = u.font.line_height() - u.px(4);
  u.draw_icon(last_op_open_ ? ui::Icon::ArrowDown : ui::Icon::ArrowRight, {hr.x, hr.y + (rh - a) / 2, a, a}, u.theme.text);
  u.label({hr.x + a + u.px(6), hr.y, hr.w - a, rh}, title, u.theme.text_bright);
  if (u.hovered(hr) && u.in.pressed[0]) {
    last_op_open_ = !last_op_open_;
    u.consume_click();
  }
  u.tooltip("Adjust Last Operation (Blender: F9). Changing a value re-runs the operator\non the mesh as it was before it, as one undo step.");
  if (!last_op_open_) return;
  u.push_id("last_op");
  int y = hr.bottom() + u.px(3);
  const int lw = r.w * 2 / 5;
  bool changed = false;
  auto row = [&](const char *label) {
    Recti rr{r.x + pad, y, r.w - pad * 2, rh};
    y += rh + u.px(2);
    u.label({rr.x, rr.y, lw - u.px(4), rr.h}, label, u.theme.text);
    return Recti{rr.x + lw, rr.y, rr.w - lw, rr.h};
  };
  auto fl = [&](const char *label, float &v, float speed, float mn, float mx) {
    changed |= u.float_field(u.id(label), row(label), v, speed, mn, mx);
  };
  auto ifield = [&](const char *label, int &v, int mn, int mx) { changed |= u.int_field(u.id(label), row(label), v, mn, mx); };
  if (L.op == "extrude") {
    fl("Distance", L.amount, 0.01f, -1000.0f, 1000.0f);
    Recti cr = row("Fuse on Contact");
    changed |= u.checkbox({cr.x, cr.y, cr.h, cr.h}, L.fuse);
    u.tooltip("When the extruded face ends on another face of the mesh, join them\n(the touching area becomes an opening, like Bridge).");
  }
  else if (L.op == "inset") fl("Thickness", L.amount, 0.005f, 0.0f, 1.0f);
  else if (L.op == "bevel") {
    fl("Width", L.amount, 0.005f, 0.0001f, 1000.0f);
    ifield("Segments", L.segments, 1, 64);
  }
  else if (L.op == "bridge") {
    ifield("Segments", L.segments, 1, 256);
    ifield("Twist", L.twist, -1000, 1000);
    fl("Smoothness", L.smooth, 0.01f, 0.0f, 10.0f);
    static const char *paths[] = {"Auto", "Outside (handle)", "Inside (tunnel)"};
    changed |= u.combo(u.id("path"), row("Path"), L.path, paths, 3);
  }
  else if (L.op == "push_through") ifield("Segments", L.segments, 1, 256);
  else if (L.op == "subdivide_edges") ifield("Number of Cuts", L.segments, 1, 100);
  else if (L.op == "loopcut") {
    ifield("Number of Cuts", L.segments, 1, 64);
    fl("Slide", L.amount, 0.01f, 0.0f, 1.0f);
  }
  else if (L.op == "smooth") {
    fl("Factor", L.amount, 0.01f, 0.0f, 1.0f);
    ifield("Repeat", L.segments, 1, 100);
  }
  changed |= u.vec3_field(u.id("move"), row("Move X Y Z"), L.move, 0.01f);
  u.tooltip("Moves the result afterwards (Blender's Move in the operator panel).");
  fl("Along Normal", L.along_normal, 0.01f, -1000.0f, 1000.0f);
  u.tooltip("Moves the result along the average normal of the selected faces.");
  static const char *orients[] = {"Global", "Local"};
  changed |= u.combo(u.id("orient"), row("Orientation"), L.orientation, orients, 2);
  if (!L.message.empty()) u.label({r.x + pad, y, r.w - pad * 2, rh}, L.message, u.theme.warning);
  u.pop_id();
  if (changed) run_last_op(false);
}

}  // namespace bl

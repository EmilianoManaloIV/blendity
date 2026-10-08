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
  scene_hovered_ = u.hovered(view) && !u.any_popup_open() && !(last_op_valid() && last_op_rect_.contains(u.in.mx, u.in.my)) &&
                   !cam_preview_rect_.contains(u.in.mx, u.in.my) && !view_gizmo_rect(view).contains(u.in.mx, u.in.my);
  scene_navigation(view);

  /* Gizmo math and picking need this frame's camera matrices before we
   * render, so transforms applied by a drag show up in the same frame. */
  scene_rt_.attach(fb_, view);
  scene_r3d_.begin(&scene_rt_, cam_.view(), cam_.proj(view.w / (float)view.h), LightingEnv(), raster_opt_);
  bool gizmo_busy = gizmo_update(view);
  /* A running Push/Pull owns the mouse until it is confirmed or cancelled. */
  if (pushpull_update(view)) gizmo_busy = true;
  if (modal_update(view)) gizmo_busy = true;
  if (transform_update(view)) gizmo_busy = true;
  if (focus_pick_update(view)) gizmo_busy = true;
  if (knife_update(view)) gizmo_busy = true;
  if (draw_update(view)) gizmo_busy = true;

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
  draw_origins(view);
  knife_draw(view);
  draw_preview(view);
  if (show_gizmos_) draw_gizmo(view);
  draw_pushpull(view);
  draw_modal(view);
  draw_transform(view);
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
    if (shading_ == Shading::Rendered) {
      lines.push_back(strprintf("Path tracing  %d / %d samples", vp_pt_.samples(), scene_->render.viewport_samples));
      lines.push_back("Device    " + vp_pt_.device_summary());
    }
    if (shading_ == Shading::Shaded) lines.push_back(strprintf("Shading   %.2f ms (deferred PBR)", scene_stats_.ms_shade));
    int y = view.y + u.px(8);
    for (auto &l : lines) {
      u.canvas.text(u.font, view.x + u.px(11), y + 1, l, Color::hex(0x000000, 160));
      u.canvas.text(u.font, view.x + u.px(10), y, l, Color::hex(0xFFFFFF, 230));
      y += u.font.line_height();
    }
  }
  if (show_gizmos_ && !playing_) draw_camera_preview(view);
  else cam_preview_rect_ = Recti{};
  if (edit_mode_ && last_op_valid() && !last_op_hidden_) draw_last_op_panel(view);
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
    if (in.pressed[1] && !pp_.active && !modal_.active && !xf_.active) {  // a right-click cancels a running Push/Pull, Inset or Bevel instead
      drag_ = in.alt() ? Drag::Zoom : Drag::Fly;
      fly_last_ = now;
      fly_accel_ = 1.0f;
    }
    else if (in.pressed[2]) drag_ = Drag::Pan;
    else if (in.pressed[0] && in.alt()) {
      drag_ = in.ctrl() ? Drag::Pan : Drag::Orbit;
      alt_click_ = true;  // a click without moving selects a loop (Blender's Alt+click) instead
      alt_click_ring_ = in.ctrl();
    }
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
      if (!in.down[0]) {
        alt_click_release(view);
        drag_ = Drag::None;
        break;
      }
      u.cursor = Cursor::Move;
      cam_.yaw += dx * 0.3f;
      cam_.pitch = clampf(cam_.pitch + dy * 0.3f, -89.9f, 89.9f);
      break;
    case Drag::Pan:
    case Drag::ViewTool:
      if (!in.down[2] && !in.down[0]) {
        alt_click_release(view);
        drag_ = Drag::None;
        break;
      }
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
  if (scene_hovered_ && drag_ == Drag::None && in.wheel_y != 0 && !modal_.active) {  // a running Bevel takes the wheel
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
    env.lights.push_back(to_render_light(g, *l));
  });
  return env;
}

void Editor::submit_scene(Renderer3D &r3d, const Scene &s, bool game, LightingEnv &, Vec3) {
  s.for_each([&](GameObject &g) {
    if (!g.active_in_hierarchy()) return;
    auto *mr = g.get<MeshRenderer>();
    auto *mf = g.get<MeshFilter>();
    if (!mr || !mr->enabled || !mf || !mf->mesh) return;
    if (game ? !mr->show_in_renders : mr->display_as != 0) return;  // wire / bounds: drawn as overlays
    DrawItem it;
    bool editing = !game && edit_mode_ && g.id == edit_obj_;
    /* Edit Mode shows the modifiers set to Show in Edit Mode over the cage; renders use Show in Renders. */
    const Mesh *m = g.evaluated_mesh(editing ? 2 : game ? 1 : 0);
    if (editing && m != mf->mesh.get()) editing = false;  // a modifier result: shade it normally
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
      /* N-gon mode (SketchUp): edges inside a flat region are hidden, as SketchUp hides coplanar edges. */
      if (ngon_mode_) ngon_cache(m);
      for (auto &e : m.edge_cache()) {
        bool s = edge_is_selected(e.first, e.second);
        if (ngon_mode_ && !s && ngon_hidden_.count(Mesh::edge_key(e.first, e.second))) continue;
        bool seam = !m.seams.empty() && m.is_seam(e.first, e.second);
        uint32_t col = s ? Color::hex(0xFFA733) : (seam ? Color::hex(0xFF3030) : Color::hex(0x0A0A0A, 220));
        scene_r3d_.line(w.point(m.positions[e.first]), w.point(m.positions[e.second]), col, true, 5e-4f);
      }
      if (elem_ == EditElement::Vertex) {
        float rad = std::max(1.5f, ui_.px(2.5f) * 1.0f);
        for (size_t i = 0; i < m.vert_count(); i++)
          scene_r3d_.point(w.point(m.positions[i]), rad, vert_sel_[i] ? Color::hex(0xFFA733) : Color::hex(0x000000), true);
      }
      else if (elem_ == EditElement::Face && ngon_mode_) {
        /* One dot per flat region: the "face" SketchUp would show. */
        std::vector<uint8_t> rsel(ngon_region_center_.size(), 0);
        for (size_t f = 0; f < m.face_count() && f < ngon_region_.size(); f++)
          if (face_sel_[f] && ngon_region_[f] >= 0) rsel[(size_t)ngon_region_[f]] = 1;
        for (size_t r = 0; r < ngon_region_center_.size(); r++)
          scene_r3d_.point(w.point(ngon_region_center_[r]), ui_.px(2.5f), rsel[r] ? Color::hex(0xFFA733) : Color::hex(0x202020), true);
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

/* The nearest mesh surface along a world-space ray (what the mouse points at). */
bool Editor::raycast_scene(const Ray &ray, Vec3 &hit) {
  float best = 1e30f;
  scene_->for_each([&](GameObject &g) {
    if (!g.active_in_hierarchy()) return;
    auto *mr = g.get<MeshRenderer>();
    const Mesh *m = mr && mr->enabled && mr->display_as == 0 ? g.evaluated_mesh() : nullptr;
    if (!m) return;
    const Mat4 &w = g.world_matrix();
    const Mat4 inv = w.inverse();
    Ray lr{inv.point(ray.origin), inv.dir(ray.dir)};  // unnormalised: t stays a world parameter
    const RenderMesh &rm = m->render_mesh();
    if (ray_aabb({lr.origin, normalize(lr.dir)}, rm.bounds) < 0) return;
    for (size_t t = 0; t < rm.tri_count(); t++) {
      const float d = ray_triangle(lr, rm.positions[rm.indices[t * 3]], rm.positions[rm.indices[t * 3 + 1]], rm.positions[rm.indices[t * 3 + 2]]);
      if (d > 0 && d < best) best = d;
    }
  });
  if (best >= 1e30f) return false;
  hit = ray.origin + ray.dir * best;
  return true;
}

/* Turns a light so its +Z (the way it shines) points at `target`, keeping its
 * X horizontal (Unity's LookAt), and makes sure its range reaches. A point
 * light has no direction: only the range changes. */
void Editor::aim_light(GameObject &g, Vec3 target) {
  Light *l = g.get<Light>();
  if (!l) return;
  const Vec3 pos = g.world_position();
  const float dist = length(target - pos);
  if (dist < 1e-5f) return;
  const Vec3 d = (target - pos) / dist;
  if (l->type != 1) {
    /* Shortest turn from +Z to d, then a twist that levels the X axis. */
    const Vec3 z(0, 0, 1);
    Vec3 axis = cross(z, d);
    Quat q1 = length(axis) > 1e-6f ? Quat::axis_angle(normalize(axis), std::acos(clampf(dot(z, d), -1.0f, 1.0f)))
                                   : (dot(z, d) > 0 ? Quat() : Quat::axis_angle({0, 1, 0}, kPi));
    Vec3 want = cross(Vec3(0, 1, 0), d);
    if (length(want) < 1e-5f) want = cross(Vec3(1, 0, 0), d);  // straight up or down: any level X
    want = normalize(want);
    const Vec3 have = q1.rotate({1, 0, 0});
    const float twist = std::atan2(dot(cross(have, want), d), dot(have, want));
    g.set_world_rotation(normalize(Quat::axis_angle(d, twist) * q1));
  }
  if (l->type != 0 && l->range < dist * 1.25f) l->range = dist * 1.25f;  // directional lights have no range
  Log::info("Aimed '%s' at the point %.2f m away", g.name.c_str(), dist);
  mark_changed("Aim Light");
}

/* The Camera's focus eyedropper: the next click in the Scene view sets the
 * focus distance to that point's depth in front of the camera. */
bool Editor::focus_pick_update(const Recti &view) {
  if (!focus_pick_cam_) return false;
  auto &u = ui_;
  auto &in = u.in;
  GameObject *g = scene_->find(focus_pick_cam_);
  Camera *cam = g ? g->get<Camera>() : nullptr;
  Light *light = g && !cam ? g->get<Light>() : nullptr;
  if ((!cam && !light) || in.key_pressed[platform::KEY_ESCAPE]) {
    focus_pick_cam_ = 0;
    return false;
  }
  if (light) {
    /* A light's eyedropper: aim it at the clicked point (Blender: a Track To
     * constraint, or Light > Point At; Unity: Transform.LookAt). */
    if (!scene_hovered_) return false;
    u.cursor = Cursor::Hand;
    if (!in.pressed[0]) return true;
    u.consume_click();
    Vec3 p;
    const Ray r = scene_r3d_.screen_ray((float)(in.mx - view.x), (float)(in.my - view.y));
    if (!raycast_scene({r.origin, normalize(r.dir)}, p)) {
      Log::warn("Aim Light: click on a surface");
      return true;
    }
    aim_light(*g, p);
    focus_pick_cam_ = 0;
    return true;
  }
  if (!scene_hovered_) return false;
  u.cursor = Cursor::Hand;  // picking
  if (!in.pressed[0]) return true;
  u.consume_click();
  Vec3 p;
  const Ray r = scene_r3d_.screen_ray((float)(in.mx - view.x), (float)(in.my - view.y));
  if (!raycast_scene({r.origin, normalize(r.dir)}, p)) {
    Log::warn("Pick Focus Point: click on a surface");
    return true;
  }
  const Vec3 fwd = normalize(g->world_rotation().rotate({0, 0, 1}));
  const float d = dot(p - g->world_position(), fwd);
  if (d <= 0.0f) {
    Log::warn("Pick Focus Point: that point is behind the camera");
    return true;
  }
  cam->focus_distance = d;
  cam->dof = true;
  focus_pick_cam_ = 0;
  Log::info("Focus distance: %.3f m", d);
  mark_changed("Pick Focus Point");
  return true;
}

/* Object origins (Blender's origin dots): where each selected object's pivot
 * is. While the Inspector's Origin row is in use, a marker shows where Set
 * Origin would move it, before Apply. */
void Editor::draw_origins(const Recti &view) {
  auto &u = ui_;
  auto to_screen = [&](Vec3 p, Vec2 &s) {
    float z;
    if (!scene_r3d_.project(p, s, z)) return false;
    s.x += view.x;
    s.y += view.y;
    return true;
  };
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

/* ===================================================================== */
/* Picking                                                                */
/* ===================================================================== */

/* Edit Origin: put the meshes and children back in the world while the gizmo
 * moves or turns their objects, so only the origins change. */
void Editor::compensate_origin_drag() {
  for (auto &s : gizmo_starts_) {
    GameObject *g = scene_->find(s.id);
    if (!g) continue;
    const Mat4 corr = g->world_matrix().inverse() * s.world;
    for (auto &ms : origin_mesh_starts_)
      if (ms.first == s.id && ms.second)
        if (auto *mf = g->get<MeshFilter>()) {
          auto m = std::make_shared<Mesh>(*ms.second);
          for (Vec3 &p : m->positions) p = corr.point(p);
          m->version = ms.second->version + 1 + (++redo_serial_);
          m->touch();
          mf->mesh = m;
        }
  }
  for (auto &c : origin_child_starts_)
    if (GameObject *ch = scene_->find(c.first)) ch->set_world_matrix(c.second);
}

/* What a click snaps the origin to: the nearest vertex, else an edge's
 * midpoint, else the centre of the face under the mouse (the active object). */
bool Editor::origin_snap_target(const Recti &view, int mx, int my, Vec3 &world, std::string &what) {
  GameObject *a = active_object();
  if (!a) return false;
  const Mesh *m = a->evaluated_mesh();
  if (!m || m->positions.empty() || m->vert_count() > 400000) return false;
  const Mat4 &w = a->world_matrix();
  const Vec2 mouse((float)(mx - view.x), (float)(my - view.y));
  auto screen = [&](Vec3 p, Vec2 &s) {
    float z;
    return scene_r3d_.project(w.point(p), s, z);
  };
  float best = (float)ui_.px(12);
  bool found = false;
  for (const Vec3 &p : m->positions) {
    Vec2 s;
    if (screen(p, s) && length(s - mouse) < best) {
      best = length(s - mouse);
      world = w.point(p);
      found = true;
    }
  }
  if (found) {
    what = "Vertex";
    return true;
  }
  best = (float)ui_.px(12);
  for (auto &e : m->edge_cache()) {
    const Vec3 mid = (m->positions[e.first] + m->positions[e.second]) * 0.5f;
    Vec2 s;
    if (screen(mid, s) && length(s - mouse) < best) {
      best = length(s - mouse);
      world = w.point(mid);
      found = true;
    }
  }
  if (found) {
    what = "Edge Midpoint";
    return true;
  }
  const Ray ray = scene_r3d_.screen_ray(mouse.x, mouse.y);
  const Mat4 inv = w.inverse();
  const Ray lr{inv.point(ray.origin), inv.dir(ray.dir)};
  const RenderMesh &rm = m->render_mesh();
  float t_best = 1e30f;
  uint32_t face = UINT32_MAX;
  for (size_t t = 0; t < rm.tri_count(); t++) {
    const float d = ray_triangle(lr, rm.positions[rm.indices[t * 3]], rm.positions[rm.indices[t * 3 + 1]], rm.positions[rm.indices[t * 3 + 2]]);
    if (d > 0 && d < t_best) {
      t_best = d;
      face = t < rm.tri_face.size() ? rm.tri_face[t] : UINT32_MAX;
    }
  }
  if (face == UINT32_MAX || face >= m->face_count()) return false;
  world = w.point(m->face_center(face));
  what = "Face Center";
  return true;
}

void Editor::pick(const Recti &view, int mx, int my, int mode) {
  if (origin_edit_ && !edit_mode_ && mode == SEL_REPLACE) {
    Vec3 p;
    std::string what;
    if (origin_snap_target(view, mx, my, p, what)) {
      set_origin(5, p);  // Origin to Point
      Log::info("Origin snapped to the %s", to_lower(what).c_str());
      return;
    }
  }
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
  if (!hit) hit = pick_wire_object(view, mx, my, ui_.px(6));
  if (!hit) {
    uint32_t id = scene_rt_.id_at(mx - view.x, my - view.y);
    if (id)
      if (GameObject *g = scene_->find(id)) hit = g->id;
  }
  if (hit) select(hit, mode);
  else if (mode == SEL_REPLACE) clear_selection();
}

/* Wire and Bounds objects have no pixels in the id buffer: pick them by
 * their drawn edges, like Blender selects a wire cutter by clicking a line. */
uint64_t Editor::pick_wire_object(const Recti &view, int mx, int my, int radius) {
  uint64_t hit = 0;
  float best = (float)radius;
  scene_->for_each([&](GameObject &g) {
    if (!g.active_in_hierarchy()) return;
    auto *mr = g.get<MeshRenderer>();
    if (!mr || !mr->enabled || mr->display_as == 0) return;
    const Mesh *m = g.evaluated_mesh();
    if (!m) return;
    const Mat4 &w = g.world_matrix();
    auto seg = [&](Vec3 a, Vec3 b) {
      Vec2 sa, sb;
      float za, zb;
      if (!scene_r3d_.project(w.point(a), sa, za) || !scene_r3d_.project(w.point(b), sb, zb)) return;
      const Vec2 p((float)(mx - view.x), (float)(my - view.y)), d = sb - sa;
      const float len2 = dot(d, d);
      const float t = len2 > 0 ? clampf(dot(p - sa, d) / len2, 0.0f, 1.0f) : 0.0f;
      const float dist = length(p - (sa + d * t));
      if (dist < best) {
        best = dist;
        hit = g.id;
      }
    };
    if (mr->display_as == 2) {
      const AABB b = m->render_mesh().bounds;
      Vec3 c[8];
      for (int k = 0; k < 8; k++) c[k] = {k & 1 ? b.max.x : b.min.x, k & 2 ? b.max.y : b.min.y, k & 4 ? b.max.z : b.min.z};
      static const int kE[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
      for (auto &e : kE) seg(c[e[0]], c[e[1]]);
    }
    else
      for (auto &e : m->edge_cache()) seg(m->positions[e.first], m->positions[e.second]);
  });
  return hit;
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
  if (!show_gizmos_ || tool_ == Tool::View || pp_.active || modal_.active || xf_.active || knife_.active || draw_.active || (drag_ != Drag::None && drag_ != Drag::Gizmo)) return false;

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
  if (origin_edit_ && !eo) {
    /* Editing origins: the handles sit on the active object's origin. */
    GameObject *a = active_object();
    pivot = (a ? a : objs.back())->world_position();
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
  bool do_scale = (tool_ == Tool::Scale || tool_ == Tool::Transform) && !(origin_edit_ && !eo);  // an origin has no size
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
    if (eo && in.shift()) {
      /* Shift + drag: extrude first, then the handle moves, turns or scales the new
       * geometry - scaling a face this way makes a new face inside it (an inset), as
       * Blender's E then S does and ProBuilder / UModeler's Shift-drag. */
      MeshPtr &mp = *edit_mesh_ptr();
      Mesh &mm = *mesh_make_mutable(mp);
      sync_vert_face_selection(elem_ == EditElement::Face);
      bool made = false;
      if (elem_ == EditElement::Face && std::count(face_sel_.begin(), face_sel_.end(), 1) > 0) {
        meshops::extrude_faces(mm, face_sel_, 0.0f);
        sync_vert_face_selection(true);
        made = true;
      }
      else if (elem_ != EditElement::Face) {
        const std::unordered_set<uint64_t> edges = edge_sel_;
        meshops::EdgeSelectionScope scope(elem_ == EditElement::Edge ? &edges : nullptr);
        made = meshops::extrude_verts_edges(mm, vert_sel_) > 0;
        face_sel_.assign(mm.face_count(), 0);
        if (elem_ == EditElement::Edge) edges_from_verts();
      }
      if (made) {
        mm.touch();
        mark_changed("Extrude (Shift + drag)");
      }
    }
    if (eo) {
      const Mesh &mm = **edit_mesh_ptr();
      for (size_t i = 0; i < mm.vert_count(); i++) gizmo_vert_starts_.push_back(eo->world_matrix().point(mm.positions[i]));
      compute_proportional_weights(eo->world_matrix());
    }
    else {
      for (GameObject *g : objs) gizmo_starts_.push_back({g->id, g->world_matrix(), g->world_position(), g->world_rotation(), g->local().scale});
    }
    origin_mesh_starts_.clear();
    origin_child_starts_.clear();
    if (origin_edit_ && !eo)
      for (GameObject *g : objs) {
        if (auto *mf = g->get<MeshFilter>()) origin_mesh_starts_.push_back({g->id, mf->mesh});
        for (GameObject *c : g->children) origin_child_starts_.push_back({c->id, c->world_matrix()});
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
            if (gizmo_starts_.size() > 1 || pivot_center_) {  // around the gizmo, wherever it is
              Vec3 d = s.pos - pivot;
              Vec3 l{dot(d, axis[0]) * f.x, dot(d, axis[1]) * f.y, dot(d, axis[2]) * f.z};
              g->set_world_position(pivot + axis[0] * l.x + axis[1] * l.y + axis[2] * l.z);
            }
          }
    }
    if (origin_edit_ && !eo) {
      /* Only the origins move: the meshes (and children) are put back where they were in the world. */
      compensate_origin_drag();
      label = h <= H_MOVE_FREE ? "Move Origin" : "Rotate Origin";
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
  edge_sel_.clear();
  Log::info("Edit Mode on '%s': 1 vertices, 2 edges, 3 faces - each mode has its own tools (Mesh menu, Inspector). P Push/Pull faces, Tab to exit.", g->name.c_str());
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
  if (elem_ == EditElement::Edge) edges_from_verts();
}

bool Editor::edge_is_selected(uint32_t a, uint32_t b) const {
  if (elem_ == EditElement::Edge) return edge_sel_.count(Mesh::edge_key(a, b)) > 0;
  return a < vert_sel_.size() && b < vert_sel_.size() && vert_sel_[a] && vert_sel_[b];
}

void Editor::edges_from_verts() {
  edge_sel_.clear();
  if (!edit_object()) return;
  const Mesh &m = **edit_mesh_ptr();
  for (auto &e : m.edge_cache())
    if (e.first < vert_sel_.size() && e.second < vert_sel_.size() && vert_sel_[e.first] && vert_sel_[e.second])
      edge_sel_.insert(Mesh::edge_key(e.first, e.second));
}

void Editor::verts_from_edges() {
  if (!edit_object()) return;
  const Mesh &m = **edit_mesh_ptr();
  vert_sel_.assign(m.vert_count(), 0);
  face_sel_.assign(m.face_count(), 0);
  for (auto &e : m.edge_cache())
    if (edge_sel_.count(Mesh::edge_key(e.first, e.second))) vert_sel_[e.first] = vert_sel_[e.second] = 1;
  /* A face is selected when all of its edges are (Blender's edge-mode flush). */
  for (size_t f = 0; f < m.face_count(); f++) {
    bool all = m.face_size(f) > 0;
    for (uint32_t k = 0; k < m.face_size(f) && all; k++)
      all = edge_sel_.count(Mesh::edge_key(m.face_verts(f)[k], m.face_verts(f)[(k + 1) % m.face_size(f)])) > 0;
    face_sel_[f] = all;
  }
}

void Editor::edit_select_all(bool sel) {
  if (!edit_object()) return;
  std::fill(vert_sel_.begin(), vert_sel_.end(), sel ? 1 : 0);
  std::fill(face_sel_.begin(), face_sel_.end(), sel ? 1 : 0);
  if (sel) edges_from_verts();
  else edge_sel_.clear();
}

void Editor::edit_pick(const Recti &view, int mx, int my, int mode) {
  last_op_hidden_ = true;  // selecting something else puts the last operator's panel away
  GameObject *g = edit_object();
  if (!g) return;
  const Mesh &m = **edit_mesh_ptr();
  vert_sel_.resize(m.vert_count(), 0);
  face_sel_.resize(m.face_count(), 0);
  const Mat4 &w = g->world_matrix();
  /* Clicking another object works as it does outside Edit Mode (Unity): a mesh
   * becomes the one being edited (same vertex / edge / face mode); a light or
   * camera icon leaves Edit Mode and selects it. */
  {
    uint64_t other = 0;
    float best = (float)ui_.px(14);
    scene_->for_each([&](GameObject &o) {
      if (!o.active_in_hierarchy() || (!o.get<Light>() && !o.get<Camera>())) return;
      Vec2 sp;
      float z;
      if (!scene_r3d_.project(o.world_position(), sp, z)) return;
      float d = length(Vec2(view.x + sp.x - mx, view.y + sp.y - my));
      if (d < best) { best = d; other = o.id; }
    });
    if (other) {
      exit_edit_mode();
      select(other, mode);
      return;
    }
    const uint32_t id = scene_rt_.id_at(mx - view.x, my - view.y);
    GameObject *o = id ? scene_->find(id) : nullptr;
    if (o && o->id != edit_obj_ && o->get<MeshFilter>() && o->get<MeshFilter>()->mesh) {
      const EditElement keep = elem_;
      exit_edit_mode();
      select(o->id);
      enter_edit_mode();
      set_edit_element(keep);
      Log::info("Editing '%s'", o->name.c_str());
      return;
    }
  }
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
    /* The edge itself, not its two ends: two opposite sides of a quad stay two edges. */
    if (edit_pick_edge(view, mx, my, a, b)) {
      const uint64_t k = Mesh::edge_key(a, b);
      if (mode == SEL_TOGGLE && edge_sel_.count(k)) edge_sel_.erase(k);
      else edge_sel_.insert(k);
    }
    verts_from_edges();
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
    if (bf >= 0 && ngon_mode_) {
      /* N-gon mode: the whole flat region is one face (SketchUp). */
      std::vector<uint8_t> region;
      meshops::coplanar_region(m, (size_t)bf, kNgonAngle, region);
      const uint8_t to = mode == SEL_TOGGLE ? !face_sel_[bf] : 1;
      for (size_t f = 0; f < region.size(); f++)
        if (region[f]) face_sel_[f] = to;
    }
    else if (bf >= 0) face_sel_[bf] = mode == SEL_TOGGLE ? !face_sel_[bf] : 1;
    sync_vert_face_selection(true);
  }
}

void Editor::edit_box_select(const Recti &view, Recti box, int mode) {
  last_op_hidden_ = true;
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
  if (elem_ == EditElement::Edge) {
    for (auto &e : m.edge_cache())
      if (visible_in_box(m.positions[e.first]) && visible_in_box(m.positions[e.second])) edge_sel_.insert(Mesh::edge_key(e.first, e.second));
    verts_from_edges();
  }
  else if (elem_ != EditElement::Face) {
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
  /* In edge mode the tools read exactly the selected edges. */
  const std::unordered_set<uint64_t> edges = edge_sel_;
  meshops::EdgeSelectionScope scope(elem_ == EditElement::Edge ? &edges : nullptr);
  if (elem_ == EditElement::Edge) verts_from_edges();
  else if (elem_ != EditElement::Face) {
    /* Derive faces from vertices like Blender does when switching modes. */
    for (size_t f = 0; f < m.face_count(); f++) {
      bool all = true;
      for (uint32_t k = 0; k < m.face_size(f); k++) all = all && vert_sel_[m.face_verts(f)[k]];
      face_sel_[f] = all;
    }
  }
  size_t nsel = std::count(face_sel_.begin(), face_sel_.end(), 1);
  if (edit_op_redoable(op)) {
    if ((op == "extrude" || op == "inset" || op == "push_through" || op == "push_pull" || op == "poke" || op == "extrude_individual") && !nsel) {
      Log::warn("Select faces first (press 3 for face mode)");
      return;
    }
    LastOp L;
    L.op = op;
    L.obj = g->id;
    L.before = std::make_shared<Mesh>(m);
    L.vsel = vert_sel_;
    L.fsel = face_sel_;
    L.esel = edges;
    L.elem = elem_;
    L.fuse = auto_fuse_;
    if (op == "extrude") L.amount = extrude_dist_;
    else if (op == "inset") L.amount = inset_amount_;
    else if (op == "bevel") { L.amount = bevel_width_; L.segments = bevel_segments_; L.clamp = bevel_clamp_; }
    else if (op == "bridge") L.segments = bridge_segments_;
    else if (op == "subdivide_edges") L.segments = subdivide_cuts_;
    else if (op == "loopcut") { L.segments = loop_cuts_; L.amount = loop_slide_; }
    else if (op == "smooth") L.amount = smooth_factor_;
    else if (op == "poke") L.amount = 0.0f;
    else if (op == "extrude_individual") L.amount = extrude_dist_;
    else if (op == "shrink_fatten") L.amount = 0.1f;
    else if (op == "to_sphere") L.amount = 1.0f;
    else if (op == "randomize") { L.amount = 0.05f; L.segments = 1; }
    else if (op == "push_pull") L.amount = pp_last_distance_ != 0 ? pp_last_distance_ : extrude_dist_;
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
  else if (op == "spin") {
    static const Vec3 kAx[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const size_t n = meshops::spin(m, vert_sel_, Vec3(0.0f), kAx[std::max(0, std::min(spin_axis_, 2))], spin_angle_, std::max(1, spin_steps_));
    if (!n) {
      Log::warn("Spin: select edges or vertices first (a profile; the object's origin is the centre)");
      return;
    }
    face_sel_.assign(m.face_count(), 0);
    if (elem_ == EditElement::Edge) edges_from_verts();
    Log::info("Spin: %.0f degrees in %d steps", spin_angle_, spin_steps_);
  }
  else if (op == "follow") {
    size_t face = SIZE_MAX;
    for (size_t f = 0; f < face_sel_.size() && face == SIZE_MAX; f++)
      if (face_sel_[f]) face = f;
    std::string err;
    if (face == SIZE_MAX || !meshops::follow(m, face, &err)) {
      Log::warn("Follow: %s", face == SIZE_MAX ? "select the face to sweep (and draw its path as wire edges)" : err.c_str());
      return;
    }
    vert_sel_.assign(m.vert_count(), 0);
    face_sel_.assign(m.face_count(), 0);
    Log::info("Follow: swept along the path");
  }
  else if (op == "slice") {
    /* Through the selection's centre (else the origin), across the chosen axis or the view. */
    Vec3 c(0.0f);
    int k = 0;
    for (size_t v = 0; v < vert_sel_.size() && v < m.vert_count(); v++)
      if (vert_sel_[v]) c += m.positions[v], k++;
    if (k) c = c / (float)k;
    static const Vec3 kAx[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const Vec3 n = slice_axis_ < 3 ? kAx[slice_axis_] : g->world_matrix().inverse().dir(cam_.forward());
    const size_t n_cut = meshops::slice(m, c, n, slice_clear_);
    if (!n_cut && !slice_clear_) {
      Log::warn("Slice: the plane doesn't cross the mesh");
      return;
    }
    vert_sel_.assign(m.vert_count(), 0);
    face_sel_.assign(m.face_count(), 0);
    edge_sel_.clear();
    Log::info("Slice: %zu cut(s)", n_cut);
  }
  else if (op == "select_similar_normal" || op == "select_similar_area" || op == "select_by_material") {
    std::vector<size_t> sel;
    for (size_t f = 0; f < face_sel_.size() && f < m.face_count(); f++)
      if (face_sel_[f]) sel.push_back(f);
    if (sel.empty()) {
      Log::warn("Select a face first");
      return;
    }
    auto area = [&](size_t f) {  /* half the Newell vector's length */
      Vec3 s(0.0f);
      const uint32_t *v = m.face_verts(f);
      for (uint32_t i = 0; i < m.face_size(f); i++) s += cross(m.positions[v[i]], m.positions[v[(i + 1) % m.face_size(f)]]);
      return 0.5f * length(s);
    };
    for (size_t f = 0; f < m.face_count(); f++)
      for (size_t s : sel) {
        bool same = false;
        if (op == "select_similar_normal") same = dot(m.face_normal(f), m.face_normal(s)) > std::cos(5.0f * kDeg2Rad);
        else if (op == "select_similar_area") {
          const float a = area(f), b = area(s);
          same = std::fabs(a - b) <= 0.05f * std::max(a, b) + 1e-9f;
        }
        else same = m.material_of(f) == m.material_of(s);
        if (same) {
          face_sel_[f] = 1;
          break;
        }
      }
    sync_vert_face_selection(true);
    return;
  }
  else if (op == "select_sharp") {
    /* Blender's Select Sharp Edges: switches to edge mode with them selected. */
    if (elem_ != EditElement::Edge) set_edit_element(EditElement::Edge);
    edge_sel_.clear();
    for (uint64_t k : meshops::sharp_edges_by_angle(m, seam_angle_, true)) edge_sel_.insert(k);
    verts_from_edges();
    Log::info("Selected %zu sharp edge(s) (> %.0f deg)", edge_sel_.size(), seam_angle_);
    return;
  }
  else if (op == "dissolve_limited") {
    const size_t n = meshops::dissolve_limited(m, kNgonAngle);
    if (!n) {
      Log::info("Merge Coplanar: nothing to merge (no flat regions of several faces)");
      return;
    }
    vert_sel_.assign(m.vert_count(), 0);
    face_sel_.assign(m.face_count(), 0);
    edge_sel_.clear();
    Log::info("Merge Coplanar: %zu edges / vertices dissolved, %zu faces left", n, m.face_count());
  }
  else if (op == "extrude_edges") {
    const size_t n = meshops::extrude_verts_edges(m, vert_sel_);
    if (!n) {
      Log::warn("Extrude: select vertices or edges first");
      return;
    }
    face_sel_.assign(m.face_count(), 0);
    if (elem_ == EditElement::Edge) edges_from_verts();
    Log::info("Extruded %zu %s", n, elem_ == EditElement::Edge ? "edge(s)" : "edge(s) / vertices");
  }
  else if (op == "delete") {
    if (elem_ != EditElement::Face) {
      /* Vertex mode: faces using a selected vertex; edge mode: faces using a selected edge. */
      for (size_t f = 0; f < m.face_count(); f++) {
        const uint32_t *v = m.face_verts(f);
        uint32_t n = m.face_size(f);
        bool any = false;
        for (uint32_t k = 0; k < n; k++)
          any = any || (elem_ == EditElement::Vertex ? vert_sel_[v[k]] : meshops::edge_selected(v[k], v[(k + 1) % n], vert_sel_));
        face_sel_[f] = any;
      }
      /* Wire edges: those at a selected vertex, or selected themselves. */
      m.loose_edges.erase(std::remove_if(m.loose_edges.begin(), m.loose_edges.end(),
                                         [&](uint64_t k) {
                                           const uint32_t a = (uint32_t)(k >> 32), b = (uint32_t)(k & 0xFFFFFFFF);
                                           if (a >= vert_sel_.size() || b >= vert_sel_.size()) return true;
                                           return elem_ == EditElement::Vertex ? (vert_sel_[a] || vert_sel_[b]) != 0
                                                                               : meshops::edge_selected(a, b, vert_sel_);
                                         }),
                          m.loose_edges.end());
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
    m.prune_loose_edges();  // wire edges the new face now uses
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
  else if (op == "triangulate_faces" || op == "tris_to_quads" || op == "flip_faces" || op == "duplicate" || op == "split" ||
           op == "dissolve_faces") {
    if (!nsel) {
      Log::warn("Select faces first (press 3 for face mode)");
      return;
    }
    size_t n = op == "triangulate_faces" ? meshops::triangulate_faces(m, face_sel_)
               : op == "tris_to_quads"   ? meshops::tris_to_quads(m, face_sel_)
               : op == "flip_faces"      ? meshops::flip_faces(m, face_sel_)
               : op == "duplicate"       ? meshops::duplicate_faces(m, face_sel_)
               : op == "split"           ? meshops::split_faces(m, face_sel_)
                                         : meshops::dissolve_faces(m, face_sel_);
    vert_sel_.assign(m.vert_count(), 0);
    face_sel_.resize(m.face_count(), 0);
    sync_vert_face_selection(true);
    Log::info("%s: %zu", op.c_str(), n);
    if (op == "duplicate") {
      /* Blender's Shift+D: the copy is selected, ready to move (W). */
      m.touch();
      mark_changed("Edit: " + op);
      tool_ = Tool::Move;
      Log::info("Duplicated - move it with the gizmo (W)");
      return;
    }
  }
  else if (op == "dissolve_vertices") {
    size_t n = meshops::dissolve_vertices(m, vert_sel_);
    if (!n) {
      Log::warn("Select vertices to dissolve first (press 1 for vertex mode)");
      return;
    }
    face_sel_.assign(m.face_count(), 0);
    Log::info("Dissolved vertices");
  }
  else if (op == "edge_split") {
    if (!meshops::edge_split(m, vert_sel_)) {
      Log::warn("Select edges between two faces first (press 2 for edge mode)");
      return;
    }
    vert_sel_.resize(m.vert_count(), 0);
    face_sel_.assign(m.face_count(), 0);
    sync_vert_face_selection(false);
    Log::info("Edge Split");
  }
  else if (op == "select_linked" || op == "select_more" || op == "select_less" || op == "select_invert" ||
           op == "select_non_manifold" || op == "select_ring") {
    /* Selection only: no undo step, nothing touched. */
    if (op == "select_ring") {
      std::vector<uint64_t> sel(edge_sel_.begin(), edge_sel_.end());
      if (elem_ != EditElement::Edge || sel.empty()) {
        Log::warn("Select Edge Ring: select an edge first (press 2 for edge mode)");
        return;
      }
      for (uint64_t k : sel)
        for (auto &e : meshops::edge_ring_edges(m, (uint32_t)(k >> 32), (uint32_t)(k & 0xFFFFFFFF))) edge_sel_.insert(Mesh::edge_key(e.first, e.second));
      verts_from_edges();
      return;
    }
    if (elem_ == EditElement::Face && op != "select_non_manifold") {
      /* Faces: through the vertices, then back to whole faces. */
      sync_vert_face_selection(true);
    }
    if (op == "select_linked") meshops::select_linked(m, vert_sel_);
    else if (op == "select_more") meshops::grow_selection(m, vert_sel_, true);
    else if (op == "select_less") meshops::grow_selection(m, vert_sel_, false);
    else if (op == "select_invert") {
      if (elem_ == EditElement::Face) {
        for (auto &f : face_sel_) f = !f;
        sync_vert_face_selection(true);
        return;
      }
      if (elem_ == EditElement::Edge) {
        std::unordered_set<uint64_t> inv;
        for (auto &e : m.edge_cache())
          if (!edge_sel_.count(Mesh::edge_key(e.first, e.second))) inv.insert(Mesh::edge_key(e.first, e.second));
        edge_sel_ = inv;
        verts_from_edges();
        return;
      }
      for (auto &v : vert_sel_) v = !v;
    }
    else meshops::select_non_manifold(m, vert_sel_);
    sync_vert_face_selection(false);  // faces: those whose corners are all selected
    return;
  }
  else if (op == "loopcut" || op == "select_loop") {
    /* Uses the selected edge (exactly two adjacent selected vertices). */
    uint32_t a = UINT32_MAX, b = UINT32_MAX;
    for (auto &e : m.edge_cache())
      if (meshops::edge_selected(e.first, e.second, vert_sel_)) {
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

/* Edit Mode operators by selection mode (ProBuilder's Vertex / Edge / Face
 * actions, Blender's Vertex / Edge / Face menus). The Mesh menu, the
 * Inspector and the shortcuts all offer only the current mode's operators. */
const std::vector<EditOpInfo> &edit_op_table() {
  enum { V = 1, E = 2, F = 4, ALL = 7 };
  static const std::vector<EditOpInfo> ops = {
      {"extrude_edges", "Extrude", "E", "Extrude the selected vertices and edges, then move them: an edge grows a face, a lone vertex\n"
       "a new edge (start a shape from one vertex). Blender: E in vertex / edge mode.", V | E},
      {"merge_center", "Merge at Center", "Alt+M", "Weld the selected vertices at their centre. Blender: M > At Center.", V},
      {"connect", "Connect Vertex Path", "J", "Split a face between two selected vertices that aren't neighbours. Blender: J.", V},
      {"fill", "Make Face", "Alt+F", "Make a face from the selected vertices (in edge mode: fill the selected hole). Blender: F.", V | E},
      {"smooth", "Smooth Vertices", "", "Laplacian smoothing of the selected vertices. Blender: Smooth Vertices.", V},
      {"dissolve_vertices", "Dissolve Vertices", "Ctrl+X", "Remove the selected vertices; the faces around each one merge (a vertex on a straight edge just goes).\nBlender: Dissolve Vertices.", V},
      {"bevel", "Bevel", "Ctrl+B", "Round off the selected edges with a strip of faces. Blender: Ctrl+B.", E},
      {"loopcut", "Loop Cut", "Ctrl+R", "Cut the quad ring across the selected edge (or hover an edge and press Ctrl+R). Blender: Ctrl+R.", E},
      {"select_loop", "Select Edge Loop", "Double-click", "Extend the selected edge to its whole loop. Blender: Alt+click.", E},
      {"subdivide_edges", "Subdivide", "", "Split the selected edges into equal parts. Blender: Subdivide.", E},
      {"dissolve", "Dissolve", "Ctrl+X", "Remove the selected edges, merging the faces on both sides. Blender: Dissolve Edges.", E},
      {"collapse", "Collapse", "", "Merge each selected edge (or connected group) into one vertex at its centre. Blender: Collapse.", E},
      {"mark_seam", "Mark Seam", "", "UV unwrapping cuts the mesh along these edges. Blender: Mark Seam.", E},
      {"clear_seam", "Clear Seam", "", "Remove UV seams from the selected edges.", E},
      {"spin", "Spin / Lathe", "", "Turn the selected edges (or vertices) round the object's axis into a surface of revolution.\n"
       "UModeler: Lathe. Blender: Spin.", V | E},
      {"follow", "Follow", "", "Sweep the selected face along a path of wire edges that starts at it (draw the path with the Polyline tool).\n"
       "UModeler: Follow. SketchUp: Follow Me.", F},
      {"select_similar_normal", "Similar Normal", "", "Select the faces facing the same way as the selected ones. Blender: Select Similar > Normal.", F},
      {"select_similar_area", "Similar Area", "", "Select the faces of about the same area. Blender: Select Similar > Area.", F},
      {"select_by_material", "Same Material", "", "Select every face using the selected faces' material slots. Blender: Material > Select.", F},
      {"seams_from_sharp", "Seams from Sharp", "", "Mark a seam on every sharp edge: faces meeting at more than the Sharp Angle, or edges marked sharp.\n"
       "Blender: Select Sharp Edges, then Mark Seam.", E},
      {"slice", "Slice", "", "Cut the mesh with a plane through the selection (or the origin) across the Slice Axis.\n"
       "UModeler: Slice. Blender: Bisect.", ALL},
      {"select_sharp", "Select Sharp Edges", "", "Select the edges whose faces meet at more than the Sharp Angle (and edges marked sharp).\n"
       "Blender: Select > Select Sharp Edges.", ALL},
      {"mark_sharp", "Mark Sharp", "", "Smooth shading stops at these edges (a hard edge). Blender: Edge > Mark Sharp.", E},
      {"clear_sharp", "Clear Sharp", "", "Let smooth shading run across the selected edges again.", E},
      {"edge_split", "Edge Split", "", "Disconnect the faces along the selected edges (hard edges, or to pull pieces apart). Blender: Edge Split.", E},
      {"select_ring", "Select Edge Ring", "", "Extend the selected edges across their quads (the rungs of a ladder). Blender: Ctrl+Alt+click.", E},
      {"push_pull", "Push/Pull", "P", "Move the selected faces along their normal like SketchUp: sides stretch into coplanar\nneighbours, pushing to the far side makes a hole, pulling onto a face joins it.\nMove the mouse or type a distance, click to confirm, Esc to cancel.", F},
      {"extrude", "Extrude", "Ctrl+E", "Extrude the selected faces along their normals. Blender: E.", F},
      {"inset", "Inset", "Ctrl+I", "Make a smaller copy of each selected face inside it. Blender: I.", F},
      {"push_through", "Push Through", "Alt+P", "Cut a hole through the object along the selected face's normal.\nTip: Inset first so the hole has a rim.", F},
      {"fuse", "Fuse onto Face", "", "Join selected faces that lie on another face of the mesh (the touching area becomes an opening).", F},
      {"extrude_individual", "Extrude Individual", "", "Extrude each selected face on its own, with its own walls. Blender: Extrude Individual Faces.", F},
      {"poke", "Poke Faces", "", "Split each selected face into a fan of triangles around its centre. Blender: Face > Poke Faces.", F},
      {"triangulate_faces", "Triangulate", "Ctrl+T", "Split the selected faces into triangles. Blender: Ctrl+T.", F},
      {"tris_to_quads", "Tris to Quads", "Alt+J", "Join pairs of selected triangles into quads where they are nearly flat. Blender: Alt+J.", F},
      {"dissolve_faces", "Dissolve Faces", "Ctrl+X", "Merge each connected group of selected faces into one face. Blender: Dissolve Faces.", F},
      {"flip_faces", "Flip Normals", "", "Turn the selected faces around (their normals point the other way). Blender: Mesh > Normals > Flip.", F},
      {"duplicate", "Duplicate", "Shift+D", "Copy the selected faces in place; the copy is selected, ready to move. Blender: Shift+D. Unity: Ctrl+D.", F},
      {"split", "Split", "", "Detach the selected faces from the rest of the mesh (same place, own vertices). Blender: Y.", F},
      {"shade_smooth", "Shade Smooth", "", "Smooth shading on the selected faces only (Blender: Face > Shade Smooth).\nIt stops at flat faces and at edges marked sharp.", F},
      {"shade_flat", "Shade Flat", "", "Flat (faceted) shading on the selected faces only.", F},
      {"bridge", "Bridge", "Ctrl+Shift+B", "Join two selected faces, or two holes, with a tube - at any angle. Blender: Bridge Edge Loops.", E | F},
      {"delete", "Delete", "Del", "Delete the selected elements (and the faces that use them). Blender: X.", ALL},
      {"recalc_normals", "Recalculate Normals", "Shift+N", "Make every face point outward. Blender: Mesh > Normals > Recalculate Outside.", ALL},
      {"shrink_fatten", "Shrink/Fatten", "Alt+S", "Move the selected vertices along their normals (F9 sets the distance). Blender: Alt+S.", ALL},
      {"to_sphere", "To Sphere", "Shift+Alt+S", "Pull the selected vertices onto a sphere around their centre (F9 sets how far). Blender: Shift+Alt+S.", ALL},
      {"randomize", "Randomize", "", "Jitter the selected vertices (F9: amount and seed). Blender: Transform > Randomize.", ALL},
      {"knife", "Knife / Line", "K", "Draw a line across a face to split it: click a corner or an edge, then another on the same face.\n"
       "Snaps to endpoints (green), midpoints (cyan) and edges (red). SketchUp: Line tool. Blender: K.", ALL},
      {"dissolve_limited", "Merge Coplanar", "", "Turn every flat region into one n-gon and drop corners on straight edges.\n"
       "Blender: Delete > Limited Dissolve. SketchUp shows flat regions this way.", ALL},
      {"origin_to_selection", "Origin to Selection", "", "Move the object's origin to the centre of the selected elements (the mesh stays put).\n"
       "Blender: Shift+S > Cursor to Selected, then Set Origin > Origin to 3D Cursor.", ALL},
      {"select_linked", "Select Linked", "Ctrl+L", "Select everything connected to the selection. Blender: Ctrl+L.", ALL},
      {"select_more", "Select More", "Ctrl+=", "Grow the selection by one ring. Blender: Ctrl+Numpad +.", ALL},
      {"select_less", "Select Less", "Ctrl+-", "Shrink the selection by one ring. Blender: Ctrl+Numpad -.", ALL},
      {"select_invert", "Invert Selection", "Ctrl+Shift+I", "Select what isn't, deselect what is. Blender: Ctrl+I.", ALL},
      {"select_non_manifold", "Select Non-Manifold", "", "Select holes and edges shared by more than two faces. Blender: Select > All by Trait > Non Manifold.", ALL},
  };
  return ops;
}

static int element_bit(EditElement e) { return e == EditElement::Vertex ? 1 : e == EditElement::Edge ? 2 : 4; }

const EditOpInfo *find_edit_op(const std::string &op) {
  for (const EditOpInfo &i : edit_op_table())
    if (op == i.op) return &i;
  return nullptr;
}

bool Editor::edit_op_available(const std::string &op) const {
  const EditOpInfo *i = find_edit_op(op);
  return !i || (i->elements & element_bit(elem_));
}

/* An operator chosen from the menu, the Inspector or a shortcut. */
void Editor::edit_tool(const std::string &op) {
  if (!edit_mode_) return;
  if (!edit_op_available(op)) {
    const EditOpInfo *i = find_edit_op(op);
    const char *mode = (i->elements & 4) ? "a face" : (i->elements & 2) ? "an edge" : "a vertex";
    const char *key = (i->elements & 4) ? "3" : (i->elements & 2) ? "2" : "1";
    Log::warn("%s is %s operation: press %s to switch modes", i->label, mode, key);
    return;
  }
  if (op == "push_pull") pushpull_begin();
  else if (op == "origin_to_selection") set_origin(6);
  else if (op == "knife") knife_begin();
  else if (op == "mark_seam" || op == "clear_seam" || op == "seams_from_sharp") uv_op(op);
  else if (op == "mark_sharp" || op == "clear_sharp" || op == "shade_smooth" || op == "shade_flat") mesh_op(op);
  else edit_op(op);
}

void Editor::set_edit_element(EditElement e) {
  EditElement old = elem_;
  elem_ = e;
  if (e == EditElement::Face) sync_vert_face_selection(false);
  else if (old == EditElement::Face) sync_vert_face_selection(true);
  if (e == EditElement::Edge && old != EditElement::Edge) edges_from_verts();  // from here on edges are kept themselves
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
  if (elem_ == EditElement::Face) {
    /* Face mode: the face loop - the quads between the clicked edge's ring (Blender). */
    face_sel_.resize(m.face_count(), 0);
    const auto ring = meshops::edge_ring_edges(m, a, b);
    std::unordered_set<uint64_t> ring_keys;
    for (auto &e : ring) ring_keys.insert(Mesh::edge_key(e.first, e.second));
    size_t n = 0;
    for (size_t f = 0; f < m.face_count(); f++) {
      int hits = 0;
      for (uint32_t i = 0; i < m.face_size(f); i++)
        hits += ring_keys.count(Mesh::edge_key(m.face_verts(f)[i], m.face_verts(f)[(i + 1) % m.face_size(f)])) ? 1 : 0;
      if (hits >= 2 || (hits == 1 && ring.size() == 1)) {
        face_sel_[f] = 1;
        n++;
      }
    }
    sync_vert_face_selection(true);
    Log::info("Face loop: %zu faces", n);
    return;
  }
  auto loop = meshops::edge_loop(m, a, b);
  if (elem_ == EditElement::Edge) {
    /* The loop's own edges (consecutive vertices), not every edge among them. */
    std::unordered_set<uint64_t> all;
    for (auto &e : m.edge_cache()) all.insert(Mesh::edge_key(e.first, e.second));
    for (size_t i = 0; i < loop.size(); i++) {
      const uint64_t k = Mesh::edge_key(loop[i], loop[(i + 1) % loop.size()]);
      if (all.count(k)) edge_sel_.insert(k);
    }
    verts_from_edges();
  }
  else {
    for (uint32_t v : loop) vert_sel_[v] = 1;
    sync_vert_face_selection(false);
  }
  Log::info("Edge loop: %zu vertices", loop.size());
}

/* Blender's Ctrl+Alt+click: the edge ring through the edge under the mouse
 * (the rungs of the ladder); in face mode the same face loop as Alt+click. */
void Editor::edit_select_ring(const Recti &view, int mx, int my, bool add) {
  if (elem_ == EditElement::Face) {
    edit_select_loop(view, mx, my, add);
    return;
  }
  GameObject *g = edit_object();
  uint32_t a, b;
  if (!g || !edit_pick_edge(view, mx, my, a, b)) return;
  const Mesh &m = **edit_mesh_ptr();
  vert_sel_.resize(m.vert_count(), 0);
  if (!add) edit_select_all(false);
  const auto ring = meshops::edge_ring_edges(m, a, b);
  if (elem_ == EditElement::Edge) {
    for (auto &e : ring) edge_sel_.insert(Mesh::edge_key(e.first, e.second));
    verts_from_edges();
  }
  else {
    for (auto &e : ring) vert_sel_[e.first] = vert_sel_[e.second] = 1;
    sync_vert_face_selection(false);
  }
  Log::info("Edge ring: %zu edges", ring.size());
}

void Editor::alt_click_release(const Recti &view) {
  if (!alt_click_) return;
  alt_click_ = false;
  auto &in = ui_.in;
  if (!edit_mode_ || std::abs(in.mx - drag_x_) > ui_.px(3) || std::abs(in.my - drag_y_) > ui_.px(3)) return;  // it was an orbit / pan
  if (alt_click_ring_) edit_select_ring(view, drag_x_, drag_y_, in.shift());
  else edit_select_loop(view, drag_x_, drag_y_, in.shift());
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
  if (elem_ != EditElement::Edge) set_edit_element(EditElement::Edge);
  edge_sel_ = {Mesh::edge_key(a, b)};
  verts_from_edges();
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
  /* A camera with its own aspect ratio is letterboxed (Unity: Game view aspect). */
  Recti img = view;
  const float aspect = cam->image_aspect(view.w / (float)view.h);
  if (cam->aspect_mode != 0) {
    if (view.w / (float)view.h > aspect) img.w = std::max(8, (int)std::lround(view.h * aspect));
    else img.h = std::max(8, (int)std::lround(view.w / aspect));
    img.x = view.x + (view.w - img.w) / 2;
    img.y = view.y + (view.h - img.h) / 2;
    u.canvas.fill_rect(view, 0xFF000000);
  }
  game_rt_.attach(fb_, img);
  Quat q = owner->world_rotation();
  Vec3 eye = owner->world_position();
  Mat4 v = Mat4::look_at(eye, eye + q.rotate({0, 0, 1}), q.rotate({0, 1, 0}));
  Mat4 p = cam->projection(aspect);
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
         op == "loopcut" || op == "smooth" || op == "push_pull" || op == "poke" || op == "extrude_individual" ||
         op == "shrink_fatten" || op == "to_sphere" || op == "randomize";
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
  if (first) last_op_hidden_ = false;  // a new operator shows its panel again
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
  meshops::EdgeSelectionScope scope(L.elem == EditElement::Edge ? &L.esel : nullptr);
  std::string err;
  bool ok = true;
  const std::string &op = L.op;
  if (op == "extrude" || op == "inset") {
    if (op == "extrude") meshops::extrude_faces(m, face_sel_, L.amount);
    else meshops::inset_faces(m, face_sel_, L.amount);
    vert_sel_.assign(m.vert_count(), 0);
    sync_vert_face_selection(true);
  }
  else if (op == "bevel") ok = meshops::bevel_edges(m, vert_sel_, face_sel_, L.amount, L.segments, &err, L.clamp);
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
      if (meshops::edge_selected(e.first, e.second, vert_sel_)) {
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
  else if (op == "push_pull") {
    ok = meshops::push_pull(m, face_sel_, L.amount, L.fuse, nullptr, &err);
    if (ok) sync_vert_face_selection(true);
  }
  else if (op == "smooth") {
    meshops::smooth_laplacian(m, L.amount, std::max(1, L.segments), &vert_sel_);
  }
  else if (op == "poke") {
    ok = meshops::poke_faces(m, face_sel_, L.amount) > 0;
    if (!ok) err = "Select faces first (press 3 for face mode)";
    vert_sel_.assign(m.vert_count(), 0);
    sync_vert_face_selection(true);
  }
  else if (op == "extrude_individual") {
    ok = meshops::extrude_individual(m, face_sel_, L.amount) > 0;
    if (!ok) err = "Select faces first (press 3 for face mode)";
    vert_sel_.assign(m.vert_count(), 0);
    sync_vert_face_selection(true);
  }
  else if (op == "shrink_fatten" || op == "to_sphere" || op == "randomize") {
    ok = std::count(vert_sel_.begin(), vert_sel_.end(), 1) > 0;
    if (!ok) err = "Select vertices first";
    else if (op == "shrink_fatten") meshops::shrink_fatten(m, vert_sel_, L.amount);
    else if (op == "to_sphere") meshops::to_sphere(m, vert_sel_, L.amount);
    else meshops::randomize(m, vert_sel_, L.amount, (uint32_t)std::max(1, L.segments));
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
      {"loopcut", "Loop Cut"},         {"smooth", "Smooth Vertices"},
      {"push_pull", "Push/Pull"},     {"poke", "Poke Faces"},
      {"extrude_individual", "Extrude Individual Faces"}, {"shrink_fatten", "Shrink/Fatten"},
      {"to_sphere", "To Sphere"},      {"randomize", "Randomize"}};
  auto it = titles.find(L.op);
  std::string title = it == titles.end() ? L.op : it->second;
  const int rh = u.row_h(), pad = u.px(6), w = std::min(u.px(270), view.w - u.px(20));
  /* Rows: the operator's own fields, then Move, Along Normal, Orientation. */
  int fields = L.op == "bevel" ? 3 : L.op == "randomize" ? 2 : L.op == "extrude" || L.op == "loopcut" || L.op == "smooth" || L.op == "push_pull" ? 2 : L.op == "bridge" ? 4 : 1;
  int rows = last_op_open_ ? fields + 3 + (L.message.empty() ? 0 : 1) : 0;
  int h = rh + u.px(6) + rows * (rh + u.px(2)) + (rows ? pad : 0);
  Recti r{view.x + u.px(10), view.bottom() - h - u.px(10), w, h};
  last_op_rect_ = r;
  u.frame(r, Color::hex(0x262626, 235), Color::hex(0x101010), u.px(4));
  Recti hr{r.x + pad, r.y + u.px(3), r.w - pad * 2, rh};
  int a = u.font.line_height() - u.px(4);
  u.draw_icon(last_op_open_ ? ui::Icon::ArrowDown : ui::Icon::ArrowRight, {hr.x, hr.y + (rh - a) / 2, a, a}, u.theme.text);
  u.label({hr.x + a + u.px(6), hr.y, hr.w - a - rh, rh}, title, u.theme.text_bright);
  /* x: put the panel away (F9 brings it back; so does the next operator). */
  Recti xr{hr.right() - rh, hr.y, rh, rh};
  const bool x_hot = u.hovered(xr);
  u.draw_icon(ui::Icon::Close, xr.shrink(u.px(4)), x_hot ? u.theme.text_bright : u.theme.text_dim);
  if (x_hot && u.in.pressed[0]) {
    last_op_hidden_ = true;
    u.consume_click();
    return;
  }
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
    Recti cr = row("Clamp Overlap");
    changed |= u.checkbox({cr.x, cr.y, cr.h, cr.h}, L.clamp);
    u.tooltip("On: the whole bevel narrows evenly so no new vertices pass each other on short edges.\nOff: the exact width, which can fold over. Blender: Clamp Overlap.");
  }
  else if (L.op == "bridge") {
    ifield("Segments", L.segments, 1, 256);
    ifield("Twist", L.twist, -1000, 1000);
    fl("Smoothness", L.smooth, 0.01f, 0.0f, 10.0f);
    static const char *paths[] = {"Auto", "Outside (handle)", "Inside (tunnel)"};
    changed |= u.combo(u.id("path"), row("Path"), L.path, paths, 3);
  }
  else if (L.op == "push_through") ifield("Segments", L.segments, 1, 256);
  else if (L.op == "push_pull") {
    fl("Distance", L.amount, 0.01f, -10000.0f, 10000.0f);
    u.tooltip("Type an exact distance (local units). Negative pushes in; past the far side\n"
              "it makes a hole, onto a face in front it joins it.");
    Recti cr = row("Merge Coplanar");
    changed |= u.checkbox({cr.x, cr.y, cr.h, cr.h}, L.fuse);
    u.tooltip("On: sides that continue a neighbouring face stretch it (SketchUp).\n"
              "Off: always add walls (SketchUp's Ctrl).");
  }
  else if (L.op == "subdivide_edges") ifield("Number of Cuts", L.segments, 1, 100);
  else if (L.op == "poke") fl("Offset", L.amount, 0.01f, -1000.0f, 1000.0f);
  else if (L.op == "extrude_individual") fl("Distance", L.amount, 0.01f, -1000.0f, 1000.0f);
  else if (L.op == "shrink_fatten") fl("Distance", L.amount, 0.005f, -1000.0f, 1000.0f);
  else if (L.op == "to_sphere") fl("Factor", L.amount, 0.01f, 0.0f, 1.0f);
  else if (L.op == "randomize") {
    fl("Amount", L.amount, 0.005f, 0.0f, 1000.0f);
    ifield("Seed", L.segments, 1, 100000);
  }
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

/* ===================================================================== */
/* Push/Pull tool (SketchUp)                                              */
/* ===================================================================== */

/* ===================================================================== */
/* Drag to adjust (Blender's modal Inset I and Bevel Ctrl+B)              */
/* ===================================================================== */

/* The operator runs at once; then the mouse's distance from the selection's
 * centre sets its amount until a click or Enter (Esc / right-click undoes
 * it). Inset: toward the centre makes it thicker, as in Blender. Bevel:
 * away from the centre widens it, and the wheel changes the segments. Shift
 * is fine control; typing a number sets the amount exactly. */
void Editor::modal_begin(const std::string &op) {
  if (modal_.active || pp_.active) return;
  if (!edit_mode_ || !edit_op_available(op)) {
    edit_tool(op);  // explains which mode it needs
    return;
  }
  const float start = op == "inset" ? 0.0f : bevel_width_;
  edit_op(op);
  if (!last_op_valid() || last_op_.op != op) return;  // refused (nothing selected, ...)
  GameObject *g = edit_object();
  const LastOp &L = last_op_;
  const Mesh &before = *L.before;
  /* The centre of what was selected, on screen. */
  Vec3 c(0.0f);
  int k = 0;
  for (size_t v = 0; v < before.vert_count() && v < L.vsel.size(); v++)
    if (L.vsel[v]) {
      c += before.positions[v];
      k++;
    }
  if (!k)
    for (size_t f = 0; f < before.face_count() && f < L.fsel.size(); f++)
      if (L.fsel[f]) {
        c += before.face_center(f);
        k++;
      }
  const Mat4 &w = g->world_matrix();
  const Vec3 wc = w.point(k ? c / (float)k : Vec3(0.0f));
  Vec2 s;
  float z;
  modal_ = ModalAdjust{};
  modal_.op = op;
  modal_.center = scene_r3d_.project(wc, s, z) ? Vec2(scene_rect_.x + s.x, scene_rect_.y + s.y)
                                                : Vec2(scene_rect_.x + scene_rect_.w * 0.5f, scene_rect_.y + scene_rect_.h * 0.5f);
  const Vec2 m0((float)ui_.in.mx, (float)ui_.in.my);
  modal_.start_len = std::max((float)ui_.px(24), length(m0 - modal_.center));
  const float world_per_px = cam_.ortho ? cam_.distance * 2.0f / std::max(1, scene_rect_.h)
                                        : length(cam_.position() - wc) * 2.0f * std::tan(cam_.fov * 0.5f * kDeg2Rad) / std::max(1, scene_rect_.h);
  const float units = std::max(1e-6f, length(w.dir({1, 0, 0})));  // world length of one mesh unit
  modal_.px_size = world_per_px / units;
  modal_.start_amount = start;
  modal_.amount = start;
  modal_.active = true;
  last_op_.amount = start;
  run_last_op(false);
}

void Editor::modal_finish(bool keep) {
  if (!modal_.active) return;
  modal_.active = false;
  if (keep) {
    if (modal_.op == "inset") inset_amount_ = last_op_.amount;
    else if (modal_.op == "bevel") {
      bevel_width_ = last_op_.amount;
      bevel_segments_ = last_op_.segments;
    }
    Log::info("%s: %.4g", modal_.op == "inset" ? "Inset" : "Bevel", last_op_.amount);
    return;
  }
  /* Cancelled: the mesh as it was, and no undo step left behind. */
  LastOp &L = last_op_;
  if (GameObject *g = scene_->find(L.obj))
    if (MeshFilter *mf = g->get<MeshFilter>()) mf->mesh = L.before;
  vert_sel_ = L.vsel;
  face_sel_ = L.fsel;
  edge_sel_ = L.esel;
  const std::string label = "Edit: " + L.op;
  if (pending_change_ && pending_label_ == label) pending_change_ = false;
  else if (!undo_.empty() && undo_.back().label == label) {
    stable_ = std::move(undo_.back().scene);
    undo_.pop_back();
  }
  L.op.clear();
  Log::info("%s cancelled", modal_.op == "inset" ? "Inset" : "Bevel");
}

bool Editor::modal_update(const Recti &view) {
  if (!modal_.active) return false;
  auto &u = ui_;
  auto &in = u.in;
  if (!edit_mode_ || !last_op_valid() || last_op_.op != modal_.op) {  // undone or left Edit Mode
    modal_.active = false;
    return false;
  }
  if (in.key_pressed[platform::KEY_ESCAPE] || in.pressed[1]) {
    modal_finish(false);
    u.consume_click();
    return true;
  }
  for (char c : in.text)
    if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == '*' || c == '/' || c == '(' || c == ')') modal_.typed += c;
  in.text.clear();
  if (in.key_pressed[platform::KEY_BACKSPACE] && !modal_.typed.empty()) modal_.typed.pop_back();
  float amount;
  double typed = 0;
  const float d = length(Vec2((float)in.mx, (float)in.my) - modal_.center);
  if (!modal_.typed.empty() && ui::eval_number(modal_.typed, typed)) amount = (float)typed;
  else {
    /* Shift: a tenth of the speed from where it was pressed (Blender). */
    if (in.shift() && !modal_.precise) {
      modal_.precise = true;
      modal_.precise_from = modal_.amount;
      modal_.precise_len = d;
    }
    else if (!in.shift() && modal_.precise) {
      modal_.precise = false;
      modal_.start_amount = modal_.amount;
      modal_.start_len = d;
    }
    const float from = modal_.precise ? modal_.precise_from : modal_.start_amount;
    const float base = modal_.precise ? modal_.precise_len : modal_.start_len;
    const float speed = modal_.precise ? 0.1f : 1.0f;
    if (modal_.op == "inset") amount = from + (base - d) / std::max(1.0f, modal_.start_len) * speed;
    else amount = from + (d - base) * modal_.px_size * speed;
    if (in.ctrl()) amount = std::round(amount / (modal_.op == "inset" ? 0.05f : snap_move_)) * (modal_.op == "inset" ? 0.05f : snap_move_);
  }
  amount = modal_.op == "inset" ? std::max(0.0f, std::min(0.99f, amount)) : std::max(0.0f, amount);
  if (!std::isfinite(amount)) amount = 0.0f;
  bool changed = amount != last_op_.amount;
  if (modal_.op == "bevel" && in.wheel_y != 0) {
    last_op_.segments = std::max(1, std::min(64, last_op_.segments + (in.wheel_y > 0 ? 1 : -1)));
    in.wheel_y = 0;  // the wheel adjusts segments instead of zooming
    changed = true;
  }
  if (changed) {
    modal_.amount = amount;
    last_op_.amount = amount;
    run_last_op(false);
  }
  u.redraw = true;
  if (in.pressed[0] || in.key_pressed[platform::KEY_ENTER]) {
    modal_finish(true);
    u.consume_click();
  }
  return true;
}

void Editor::draw_modal(const Recti &view) {
  if (!modal_.active) return;
  auto &u = ui_;
  u.canvas.push_clip(view);
  /* A dashed line from the centre to the cursor, as Blender draws it. */
  const Vec2 m((float)u.in.mx, (float)u.in.my);
  const Vec2 d = m - modal_.center;
  const float len = length(d);
  for (float t = 0; t < len; t += 12.0f) {
    Vec2 a = modal_.center + d * (t / std::max(1.0f, len)), b = modal_.center + d * (std::min(len, t + 6.0f) / std::max(1.0f, len));
    u.canvas.line(a.x, a.y, b.x, b.y, Color::hex(0xFFFFFF, 160), (float)u.px(1.0f));
  }
  std::string text = modal_.op == "inset" ? strprintf("Inset  %.3f", last_op_.amount)
                                          : strprintf("Bevel  width %.3f   segments %d", last_op_.amount, last_op_.segments);
  if (!modal_.typed.empty()) text += "   [" + modal_.typed + "]";
  int tw = u.font.text_width(text) + u.px(12);
  Recti box{u.in.mx + u.px(16), u.in.my + u.px(12), tw, u.row_h()};
  u.canvas.fill_round_rect(box, u.px(3), Color::hex(0x202020, 230));
  u.label(box, text, u.theme.text_bright, ui::Align::Center);
  std::string hint = modal_.op == "inset" ? "Inset:  move toward the centre to thicken, or type  |  Shift fine  |  Ctrl snap  |  Click / Enter confirm  |  Esc cancel"
                                          : "Bevel:  move away to widen, or type  |  Wheel segments  |  Shift fine  |  Ctrl snap  |  Click / Enter confirm  |  Esc cancel";
  int hw = u.font.text_width(hint) + u.px(16);
  Recti hb{view.x + (view.w - hw) / 2, view.bottom() - u.row_h() - u.px(10), hw, u.row_h()};
  u.canvas.fill_round_rect(hb, u.px(3), Color::hex(0x202020, 220));
  u.label(hb, hint, u.theme.text, ui::Align::Center);
  u.canvas.pop_clip();
}

/* Push/Pull is a modal operator like Blender's G or E: P starts it on the
 * selected faces, the mouse (or a typed number) sets the distance, and a
 * click or Enter confirms; Esc or a right-click cancels. */
void Editor::pushpull_begin() {
  if (pp_.active) return;
  GameObject *g = edit_object();
  if (!edit_mode_ || !g) {
    Log::warn("Push/Pull works on faces: select an object, press Tab, then 3 for face mode");
    return;
  }
  if (elem_ != EditElement::Face) {
    Log::warn("Push/Pull is a face operation: press 3 for face mode and select faces");
    return;
  }
  const Mesh &m = **edit_mesh_ptr();
  face_sel_.resize(m.face_count(), 0);
  vert_sel_.resize(m.vert_count(), 0);
  if (std::count(face_sel_.begin(), face_sel_.end(), 1) == 0) {
    Log::warn("Push/Pull: select one face (or a connected group of faces) first");
    return;
  }
  {
    /* Same check push_pull makes: one connected region with one outline. */
    Mesh probe = m;
    std::vector<uint8_t> fs = face_sel_;
    std::string err;
    if (!meshops::push_pull(probe, fs, 0.0f, true, nullptr, &err)) {
      Log::warn("Push/Pull: %s", err.c_str());
      return;
    }
  }
  pp_ = PushPullDrag{};
  pp_.obj = g->id;
  pp_.before = std::make_shared<Mesh>(m);
  pp_.fsel = face_sel_;
  /* The axis: the region's area-weighted normal, as the object's transform moves it. */
  Vec3 nl(0.0f), centre(0.0f);
  float area = 0.0f;
  for (size_t k = 0; k < m.face_count(); k++) {
    if (!pp_.fsel[k]) continue;
    const uint32_t *fv = m.face_verts(k);
    const uint32_t n = m.face_size(k);
    Vec3 c(0.0f), an(0.0f);
    for (uint32_t i = 0; i < n; i++) {
      c += m.positions[fv[i]];
      an += cross(m.positions[fv[i]], m.positions[fv[(i + 1) % n]]);  // Newell: 2 x area x normal
    }
    float a = std::max(1e-12f, length(an) * 0.5f);
    nl += m.face_normal(k) * a;
    centre += c / (float)n * a;
    area += a;
  }
  size_t first = std::find(pp_.fsel.begin(), pp_.fsel.end(), 1) - pp_.fsel.begin();
  nl = length_sq(nl) > 1e-20f ? normalize(nl) : m.face_normal(first);
  const Mat4 &w = g->world_matrix();
  Vec3 wd = w.dir(nl);
  pp_.units = std::max(1e-6f, length(wd));
  pp_.axis = wd / pp_.units;
  pp_.origin = w.point(centre / std::max(area, 1e-12f));
  auto &in = ui_.in;
  pp_.start = closest_on_line_to_ray(pp_.origin, pp_.axis,
                                     scene_r3d_.screen_ray((float)(in.mx - scene_rect_.x), (float)(in.my - scene_rect_.y)));
  pp_.lim = meshops::push_pull_limits(m, pp_.fsel);
  pp_.keep = in.ctrl();
  pp_.active = true;
  ui_.redraw = true;
}

void Editor::pushpull_cancel() {
  if (!pp_.active) return;
  if (GameObject *g = scene_->find(pp_.obj))
    if (MeshFilter *mf = g->get<MeshFilter>()) mf->mesh = pp_.before;
  face_sel_ = pp_.fsel;
  vert_sel_.assign(pp_.before->vert_count(), 0);
  sync_vert_face_selection(true);
  pp_.active = false;
  Log::info("Push/Pull cancelled");
}

void Editor::pushpull_apply(float distance) {
  GameObject *g = scene_->find(pp_.obj);
  MeshFilter *mf = g ? g->get<MeshFilter>() : nullptr;
  if (!mf || !pp_.before) return;
  /* Always from the mesh as it was at the start, so moving back and forth is exact. */
  mf->mesh = std::make_shared<Mesh>(*pp_.before);
  Mesh &m = *mf->mesh;
  m.version = pp_.before->version + 1 + (++redo_serial_);
  std::vector<uint8_t> fs = pp_.fsel;
  std::string err;
  pp_.distance = distance;
  pp_.refused.clear();
  if (!meshops::push_pull(m, fs, distance, !pp_.keep, &pp_.result, &err)) {
    mf->mesh = pp_.before;
    pp_.distance = 0.0f;
    pp_.refused = err;
    return;
  }
  m.touch();
  face_sel_ = fs;
  face_sel_.resize(m.face_count(), 0);
  vert_sel_.assign(m.vert_count(), 0);
  sync_vert_face_selection(true);
}

void Editor::pushpull_finish() {
  pp_.active = false;
  GameObject *g = scene_->find(pp_.obj);
  MeshFilter *mf = g ? g->get<MeshFilter>() : nullptr;
  if (!mf || std::fabs(pp_.distance) < 1e-7f) {
    if (mf) mf->mesh = pp_.before;
    face_sel_ = pp_.fsel;
    return;
  }
  /* Hand it to Adjust Last Operation, so the distance can be changed afterwards. */
  LastOp L;
  L.op = "push_pull";
  L.obj = pp_.obj;
  L.before = pp_.before;
  L.fsel = pp_.fsel;
  L.vsel.assign(pp_.before->vert_count(), 0);
  L.elem = EditElement::Face;
  L.amount = pp_.distance;
  L.fuse = !pp_.keep;
  L.result = mf->mesh.get();
  L.result_version = mf->mesh->version;
  last_op_ = std::move(L);
  pp_last_distance_ = pp_.distance;
  last_op_hidden_ = false;
  static const char *what[] = {"moved", "extruded", "made a hole through the object", "joined to the face in front"};
  Log::info("Push/Pull: %.4g (%s)", pp_.distance * pp_.units, what[(int)pp_.result]);
  mark_changed("Edit: push_pull");
}

bool Editor::pushpull_update(const Recti &view) {
  if (!pp_.active) return false;
  auto &u = ui_;
  auto &in = u.in;
  GameObject *g = scene_->find(pp_.obj);
  if (!g || !edit_mode_ || edit_obj_ != pp_.obj) {  // the object went away (undo, play, ...)
    pp_.active = false;
    return false;
  }
  if (in.key_pressed[platform::KEY_ESCAPE] || in.pressed[1]) {
    pushpull_cancel();
    u.consume_click();
    return true;
  }
  /* Typed distance (Blender's numeric input): digits and an expression. */
  for (char c : in.text)
    if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == '*' || c == '/' || c == '(' || c == ')') pp_.typed += c;
  in.text.clear();
  if (in.key_pressed[platform::KEY_BACKSPACE] && !pp_.typed.empty()) pp_.typed.pop_back();
  float d;
  double typed = 0;
  if (!pp_.typed.empty() && ui::eval_number(pp_.typed, typed)) d = (float)typed / pp_.units;
  else {
    Ray ray = scene_r3d_.screen_ray((float)(in.mx - view.x), (float)(in.my - view.y));
    d = (closest_on_line_to_ray(pp_.origin, pp_.axis, ray) - pp_.start) / pp_.units;
    /* Snap to the far side (hole) and to the face in front (join) within
     * ~10 pixels, and to the grid increment when snapping is on. */
    float world_per_px = cam_.ortho ? cam_.distance * 2.0f / view.h
                                    : length(cam_.position() - pp_.origin) * 2.0f * std::tan(cam_.fov * 0.5f * kDeg2Rad) / view.h;
    float tol = world_per_px * u.px(10) / pp_.units;
    if (snap_) d = std::round(d * pp_.units / snap_move_) * snap_move_ / pp_.units;
    if (pp_.lim.through > 0 && std::fabs(-d - pp_.lim.through) < tol) d = -pp_.lim.through;
    if (pp_.lim.contact > 0 && std::fabs(d - pp_.lim.contact) < tol) d = pp_.lim.contact;
  }
  if (!std::isfinite(d)) d = 0.0f;
  bool keep = in.ctrl();
  if (d != pp_.distance || keep != pp_.keep) {
    pp_.keep = keep;
    pushpull_apply(d);
  }
  u.redraw = true;
  if (in.pressed[0] || in.key_pressed[platform::KEY_ENTER]) {
    pushpull_finish();
    u.consume_click();
  }
  return true;
}

void Editor::draw_pushpull(const Recti &view) {
  if (!pp_.active) return;
  auto &u = ui_;
  auto proj = [&](Vec3 p, Vec2 &s) {
    float z;
    bool ok = scene_r3d_.project(p, s, z);
    s.x += view.x;
    s.y += view.y;
    return ok;
  };
  const uint32_t col = Color::hex(0xF0A030);
  /* The axis and the live distance, next to the cursor. */
  Vec2 a, b;
  float L = std::max(0.5f, std::fabs(pp_.distance * pp_.units) + 1.0f);
  u.canvas.push_clip(view);
  if (proj(pp_.origin - pp_.axis * L, a) && proj(pp_.origin + pp_.axis * L, b))
    u.canvas.line(a.x, a.y, b.x, b.y, Color::hex(0x3A7AF8, 200), (float)u.px(1.5f));
  static const char *modes[] = {"Push/Pull", "Push/Pull", "Hole through", "Join to face"};
  std::string text = pp_.typed.empty() ? strprintf("%s  %.3f%s", modes[(int)pp_.result], pp_.distance * pp_.units, pp_.keep ? "  (new face)" : "")
                                       : strprintf("%s  [%s]  = %.3f", modes[(int)pp_.result], pp_.typed.c_str(), pp_.distance * pp_.units);
  int tw = u.font.text_width(text) + u.px(12), th = u.row_h();
  Recti box{u.in.mx + u.px(16), u.in.my + u.px(12), tw, th};
  u.canvas.fill_round_rect(box, u.px(3), Color::hex(0x202020, 230));
  u.label(box, text, pp_.result == meshops::PushPullResult::Hole || pp_.result == meshops::PushPullResult::Joined ? col : u.theme.text_bright,
          ui::Align::Center);
  if (!pp_.refused.empty()) {
    std::string why = pp_.refused.rfind("Push/Pull: ", 0) == 0 ? pp_.refused.substr(11) : pp_.refused;
    Recti wb{box.x, box.bottom() + u.px(2), u.font.text_width(why) + u.px(12), th};
    u.canvas.fill_round_rect(wb, u.px(3), Color::hex(0x202020, 230));
    u.label(wb, why, u.theme.warning, ui::Align::Center);
  }
  /* Blender's header hint for a running operator. */
  std::string hint = "Push/Pull:  move the mouse or type a distance  |  Click / Enter confirm  |  Esc / Right-click cancel  |  Ctrl new face";
  int hw = u.font.text_width(hint) + u.px(16);
  Recti hb{view.x + (view.w - hw) / 2, view.bottom() - u.row_h() - u.px(10), hw, u.row_h()};
  u.canvas.fill_round_rect(hb, u.px(3), Color::hex(0x202020, 220));
  u.label(hb, hint, u.theme.text, ui::Align::Center);
  u.canvas.pop_clip();
}

}  // namespace bl

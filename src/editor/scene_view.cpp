// SPDX-License-Identifier: GPL-2.0-or-later
// Scene view & Game view.
//   Navigation  : Unity SceneView controls; Blender's equivalents live in
//                 blender/source/blender/editors/space_view3d/view3d_navigate_*.cc
//   Picking     : id buffer (blender/source/blender/editors/space_view3d/view3d_select.cc)
//   Gizmos      : Unity handles; Blender's are in blender/source/blender/editors/gizmo_library
//                 and blender/source/blender/editors/transform
//   Edit Mode   : Blender's mesh editing (blender/source/blender/editors/mesh)
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


/* ===================================================================== */
/* Scene view                                                             */
/* ===================================================================== */

void Editor::draw_scene_view(const Recti &r) {
  auto &u = ui_;
  int bh = u.row_h() + u.px(4);
  Recti bar{r.x, r.y, r.w, bh};
  Recti view{r.x, r.y + bh, r.w, r.h - bh};
  scene_rect_ = view;
  scene_drawn_ = true;
  draw_scene_overlay_bar(bar);
  if (view.w < 8 || view.h < 8) {  // (the dock doesn't paint this view's content: it paints every pixel itself)
    u.canvas.fill_rect(view, u.theme.panel);
    return;
  }
  scene_hovered_ = u.hovered(view) && !u.any_popup_open() && !(last_op_valid() && last_op_rect_.contains(u.in.mx, u.in.my)) &&
                   !cam_preview_rect_.contains(u.in.mx, u.in.my) && !view_gizmo_rect(view).contains(u.in.mx, u.in.my);
  const bool quick_menu_took = wheel_took_;  // (task 0230: the Wheel's frame step took the press or release)
  /* Task 0232: an interaction begun in another window's Scene view (a drag, a stroke, a gizmo, G / R / S, a brush) is
   * that window's: this view draws, and leaves the tools alone until it ends. */
  const bool other_owns = interaction_window_ >= 0 && interaction_window_ != active_window_;
  if (!quick_menu_took && !other_owns) scene_navigation(view);
  if (!other_owns) view_gizmo_drag(view);  // (a drag begun on the Scene Gizmo or its hand / magnifier: task 0216)
  update_pilot_camera();  // flying a camera: it follows the view

  /* Gizmo math and picking need this frame's camera matrices before we
   * render, so transforms applied by a drag show up in the same frame. */
  scene_rt_.attach(fb_, view);
  scene_r3d_.begin(&scene_rt_, cam_.view(), cam_.proj(view.w / (float)view.h), LightingEnv(), raster_opt_);
  bool gizmo_busy = quick_menu_took || other_owns;
  if (!other_owns) gizmo_busy = scene_tool_updates(view, gizmo_busy);
  if (!other_owns) interaction_window_ = scene_interacting() ? active_window_ : -1;

  auto &in = u.in;
  /* (Task 0217's pen held still is the frame's now, with a finger's: wheel_frame, task 0230.) */
  /* Task 0214: in Tablet Mode a finger the gizmo and the tools didn't take orbits (a tap still selects). */
  if (finger_navigates() && drag_ == Drag::None && scene_hovered_ && in.pressed[0] && !gizmo_busy && !u.any_active()) finger_orbit_start();
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
    if (in.cancelled) drag_ = Drag::None, box_visible = false;  // (a finger's press taken back: task 0213)
    if (drag_ == Drag::Box && (in.released[0] || !in.down[0])) {
      int mode = (in.shift() || in.ctrl()) ? SEL_TOGGLE : SEL_REPLACE;
      if (moved) {
        if (edit_mode_) edit_box_select(view, box, mode == SEL_TOGGLE ? SEL_ADD : SEL_REPLACE);
        else box_select(view, box, mode == SEL_TOGGLE ? SEL_ADD : SEL_REPLACE);
      }
      else click_select(view, in.mx, in.my, mode);
      if (moved && !edit_mode_) pt_sel_.clear();
      drag_ = Drag::None;
      box_visible = false;
    }
  }
  /* Esc drops a curve's point selection (the object stays selected). */
  if (!pt_sel_.empty() && scene_hovered_ && drag_ == Drag::None && in.key_pressed[platform::KEY_ESCAPE] && !u.wants_keyboard())
    pt_sel_.clear();
  /* Double-click frames the selection (Unity: double-click in Hierarchy; Blender: numpad '.'). A finger's double tap
   * in Tablet Mode does it as it lifts (task 0214). */
  if (scene_hovered_ && in.double_clicked[0] && !edit_mode_ && !sculpt_ && active_ && !finger_orbit_) frame_selected();  // (task 0236: two dabs close together aren't a frame)

  {
    /* The 3D layer is reused while nothing it shows has changed (an idle frame, the mouse over a panel,
     * a caret blinking). Never while piloting, path tracing or playing. */
    ScopedTimer rt;
    const bool cacheable = !piloted_camera() && !(playing_ && !paused_);
    const uint64_t key = cacheable ? scene_view_key(view) : 0;
    const bool hit = view_cache_hit(scene_cache_, key, view);
    if (hit && !cache_verify_) prof_.view_cache_hits++;
    else {
      std::vector<float> depth0;
      std::vector<uint32_t> ids0;
      if (hit) {  // (verify mode: what picking would have read - task 0204: from the card, if it still has them)
        ensure_scene_planes();
        depth0 = scene_rt_.depth, ids0 = scene_rt_.ids;
      }
      render_scene_view(view);
      prof_.view_renders++;
      if (hit && view_cache_verify(scene_cache_, view, "Scene view") && (depth0 != scene_rt_.depth || ids0 != scene_rt_.ids)) {
        prof_.cache_mismatches++;
        Log::warn("Render cache: the Scene view's depth or ids would have been stale");
        note_render_cache_mismatch();
      }
      view_cache_store(scene_cache_, key, view);
    }
    prof_.ms_scene3d += rt.ms();
  }
  if (box_visible) {
    u.canvas.fill_rect(box.intersect(view), Color::hex(0x3A79BB, 40));
    u.canvas.rect_outline(box, Color::hex(0x6FA3DD));
  }

  /* Piloting a camera, the view is that camera's game view: none of the editor's overlays. */
  const bool piloting = piloted_camera() != nullptr;
  if (!piloting) {
    draw_scene_icons(view);
    if (show_origins_) draw_origins(view);
    if (show_footprints_) draw_footprint_frames(view);
    draw_guides(view);
    draw_zfight_overlay(view);
    draw_overlap_overlay(view);
  }
  draw_pilot_frame(view);
  knife_draw(view);
  draw_preview(view);
  sketch_bind_scene(view, scene_hovered_);  // (task 0233: the camera may have moved since the update)
  sketch_draw(view);  // (task 0223: the sketch over the picture, under the gizmos)
  if (show_gizmos_ && !piloting) draw_gizmo(view);
  wp_draw(view);
  sculpt_draw(view);  // (task 0236)
  draw_autorig_markers(view);
  draw_bone_handles(view);  // (after the picture: drawn before it, it would be painted over)
  draw_pushpull(view);
  draw_modal(view);
  draw_transform(view);
  if (!piloting) draw_view_gizmo(view);

  /* Statistics overlay (Blender: Viewport Overlays > Statistics). */
  if (show_stats_ && !piloting) {
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
    lines.push_back(strprintf("Editor    %.0f fps (cap %s), frame %.1f ms", measured_fps_, max_fps_ ? std::to_string(max_fps_).c_str() : "none", frame_ms_));
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

/* Which navigation a press in the Scene view starts, by the editor's feel (task 0063), and the button that holds it:
 * Unity: RMB look + fly, Alt+LMB orbit (Alt+Ctrl+LMB pan), MMB pan, Alt+RMB zoom. Unreal: as Unity, Alt+MMB pans and
 * LMB+RMB pans. Blender: MMB orbit, Shift+MMB pan, Ctrl+MMB zoom (RMB and Alt+LMB stay menus and loop select). Maya:
 * Alt+LMB tumble, Alt+MMB track, Alt+RMB dolly (RMB alone is Maya's marking menu: no fly). */
std::pair<Editor::Drag, int> Editor::nav_drag_for_press() const {
  const auto &in = ui_.in;
  const bool unity_like = nav_style_ == NavStyle::Unity || nav_style_ == NavStyle::Unreal;
  /* Task 0216: a pen with its side button held (Windows sends it as the right button) navigates in every feel - orbit,
   * Shift pans, Ctrl zooms - as the middle button does in Blender's pen setup. */
  const int barrel_button = in.pressed[1] ? 1 : in.pressed[0] ? 0 : -1;
  if (sculpt_ && in.pressed[0] && !in.barrel) return {Drag::None, 0};  // (task 0236: the left button is the brush's, Shift and Alt its smooth and carve)
  if (in.pen && in.barrel && barrel_button >= 0 && !pp_.active && !modal_.active && !xf_.active && !bone_drag_.modal)
    return {in.shift() ? Drag::Pan : in.ctrl() ? Drag::Zoom : Drag::Orbit, barrel_button};
  if (in.pressed[1] && !pp_.active && !modal_.active && !xf_.active && !bone_drag_.modal) {  // a right-click cancels a running Push/Pull, Inset, Bevel or bone Extrude instead
    if (nav_style_ == NavStyle::Unreal && in.down[0]) return {Drag::Pan, 1};
    if (in.alt() && nav_style_ != NavStyle::Blender) return {Drag::Zoom, 1};
    return {unity_like ? Drag::Fly : Drag::None, 1};
  }
  if (in.pressed[2]) {
    if (nav_style_ == NavStyle::Maya) return {in.alt() ? Drag::Pan : Drag::None, 2};  // (plain MMB is Maya's manipulator drag)
    if (nav_style_ != NavStyle::Blender || in.shift()) return {Drag::Pan, 2};
    return {in.ctrl() ? Drag::Zoom : Drag::Orbit, 2};
  }
  if (in.pressed[0] && in.alt()) {
    if (nav_style_ == NavStyle::Blender) return {Drag::AltClick, 0};  // (Blender: Alt+click selects a loop; the view stays)
    const bool pan = nav_style_ == NavStyle::Unity && in.ctrl();
    return {pan ? Drag::Pan : Drag::Orbit, 0};
  }
  if (in.pressed[0] && tool_ == Tool::View) return {Drag::ViewTool, 0};
  return {Drag::None, 0};
}

/* The Scene view's tools, in their order of precedence (the sketch tool first: while sketching the pen draws, over
 * gizmos and tools, task 0223; a pressed bone handle before the gizmo, task 0066; the Weight Paint brush, a running
 * Push/Pull, the modal and transform operators, the focus pick, the auto-rig, the Knife, the Draw tools). True when one
 * of them owns the mouse this frame. */
bool Editor::scene_tool_updates(const Recti &view, bool busy) {
  sketch_bind_scene(view, scene_hovered_);  // (task 0233: the tool works in this view)
  if (sketch_update(view)) busy = true;
  if (bone_handles_update(view)) busy = true;
  if (gizmo_update(view)) busy = true;
  if (wp_update(view)) busy = true;
  if (sculpt_update(view)) busy = true;  // (task 0236)
  if (pushpull_update(view)) busy = true;
  if (modal_update(view)) busy = true;
  if (transform_update(view)) busy = true;
  if (focus_pick_update(view)) busy = true;
  if (autorig_update(view)) busy = true;
  if (knife_update(view)) busy = true;
  if (draw_update(view)) busy = true;
  return busy;
}

void Editor::scene_navigation(const Recti &view) {
  auto &u = ui_;
  auto &in = u.in;
  double now = clock_now();
  float k_pan = (2.0f * cam_.distance * std::tan(cam_.fov * 0.5f * kDeg2Rad)) / std::max(1, view.h);
  finger_glide(now);

  if (nav_style_ == NavStyle::Unreal && drag_ == Drag::Box && in.pressed[1]) {  // Unreal: LMB then RMB held pans too
    drag_ = Drag::Pan;
    drag_button_ = 1;
  }
  if (barrel_nav_ && drag_ != Drag::None && !in.down[drag_button_]) barrel_nav_ = false;  // (a click without moving: the Wheel's frame step opened it, task 0230)
  if (drag_ == Drag::None && scene_hovered_ && !u.any_active()) {
    std::tie(drag_, drag_button_) = nav_drag_for_press();
    barrel_nav_ = drag_ != Drag::None && in.pen && in.barrel;
    if (drag_ == Drag::Fly) {
      fly_last_ = now;
      fly_accel_ = 1.0f;
    }
    /* A pen's side button that came as the left button: its press is navigation's alone - no tool, bone handle or
     * brush after this takes it, and no Alt+click loop select (task 0216). */
    const bool barrel_left = drag_ != Drag::None && drag_button_ == 0 && in.pen && in.barrel;
    if (barrel_left) in.pressed[0] = false;
    if (!barrel_left && drag_button_ == 0 && in.alt() && (drag_ == Drag::Orbit || drag_ == Drag::Pan || drag_ == Drag::AltClick)) {
      alt_click_ = true;  // a click without moving selects a loop (Blender's Alt+click) instead
      alt_click_ring_ = in.ctrl();
    }
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
      if (nav_style_ == NavStyle::Unreal && in.pressed[0]) {  // Unreal: both buttons held pan
        drag_ = Drag::Pan;
        break;
      }
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
      if (!in.down[drag_button_]) {
        if (finger_orbit_) finger_orbit_end(view, now);
        else if (drag_button_ == 0) alt_click_release(view);
        drag_ = Drag::None;
        break;
      }
      u.cursor = Cursor::Move;
      if (finger_orbit_ && !finger_orbit_move(dx, dy, now)) break;  // (a tap's jitter doesn't turn the view)
      cam_.yaw += dx * 0.3f;
      cam_.pitch = clampf(cam_.pitch + dy * 0.3f, -89.9f, 89.9f);
      break;
    case Drag::Pan:
    case Drag::ViewTool:
      if (nav_style_ == NavStyle::Unreal && drag_ == Drag::Pan && drag_button_ == 1 && !in.down[0] && in.down[1]) {
        drag_ = Drag::Fly;  // (Unreal: LMB let go, RMB still held: looking round again)
        fly_last_ = now;
        break;
      }
      if (!in.down[drag_button_]) {
        if (drag_button_ == 0) alt_click_release(view);
        drag_ = Drag::None;
        break;
      }
      u.cursor = Cursor::Hand;
      cam_.pivot -= cam_.right() * (dx * k_pan);
      cam_.pivot += cam_.up() * (dy * k_pan);
      break;
    case Drag::AltClick:
      if (!in.down[0]) {
        if (!in.cancelled) alt_click_release(view);
        alt_click_ = false;
        drag_ = Drag::None;
      }
      break;
    case Drag::Zoom:
      if (!in.down[drag_button_]) { drag_ = Drag::None; break; }
      cam_.distance = clampf(cam_.distance * std::exp((dx - dy) * -0.008f), 0.01f, 100000.0f);
      break;
    default: break;
  }
  if (scene_hovered_ && drag_ == Drag::None && in.wheel_y != 0 && !modal_.active) {  // a running Bevel takes the wheel
    cam_.animating = false;
    if (pilot_cam_ && in.ctrl()) {
      /* Piloting a camera: Ctrl + wheel zooms the lens (its field of view), not the position. */
      cam_.fov = clampf(cam_.fov * std::pow(0.93f, in.wheel_y), 1.0f, 170.0f);
    }
    else {
      /* Dolly toward the pivot; while piloting, the camera keeps looking at the same point. */
      const Vec3 eye = cam_.position();
      cam_.distance = clampf(cam_.distance * std::pow(0.88f, in.wheel_y), 0.01f, 100000.0f);
      (void)eye;
    }
  }
  if (scene_hovered_ && drag_ == Drag::None && !modal_.active) touch_navigate(k_pan);
  if (pilot_cam_ && in.key_pressed[platform::KEY_ESCAPE] && !pp_.active && !modal_.active && !xf_.active && !draw_.active && !knife_.active)
    toggle_pilot_camera();
}

/* Two fingers on a touch screen or pad (task 0075), in every feel: pinch zooms toward the pivot, a two-finger drag
 * pans the view with the fingers, a twist turns it round the pivot (as tablets' 3D apps do: Nomad, Shapr3D). */
void Editor::touch_navigate(float k_pan) {
  const auto &in = ui_.in;
  const bool zoom = std::isfinite(in.touch_zoom) && in.touch_zoom > 0.0f && in.touch_zoom != 1.0f;
  if (!zoom && in.touch_pan_x == 0.0f && in.touch_pan_y == 0.0f && in.touch_rotate == 0.0f) return;
  cam_.animating = false;
  if (zoom) cam_.distance = clampf(cam_.distance / in.touch_zoom, 0.01f, 100000.0f);
  if (std::isfinite(in.touch_pan_x) && std::isfinite(in.touch_pan_y)) {
    cam_.pivot -= cam_.right() * (in.touch_pan_x * k_pan);
    cam_.pivot += cam_.up() * (in.touch_pan_y * k_pan);
  }
  if (std::isfinite(in.touch_rotate)) cam_.yaw -= in.touch_rotate;
  ui_.redraw = true;
}

/* A click's selection at (x, y): Edit Mode's elements, else a point of a curve, else an object. */
void Editor::click_select(const Recti &view, int x, int y, int mode) {
  if (edit_mode_) {
    edit_pick(view, x, y, mode);
    return;
  }
  if (point_pick(view, x, y, mode)) return;
  pt_sel_.clear();
  pick(view, x, y, mode);
}

/* ---- a finger in Tablet Mode (task 0214) ---- */

void Editor::finger_orbit_start() {
  const auto &in = ui_.in;
  drag_ = Drag::Orbit;
  drag_button_ = 0;
  finger_orbit_ = true;
  finger_double_ = in.double_clicked[0];
  finger_press_x_ = in.mx, finger_press_y_ = in.my;
  finger_travel_ = finger_vel_yaw_ = finger_vel_pitch_ = 0.0f;
  finger_last_ = clock_now();
  glide_yaw_ = glide_pitch_ = 0.0f;
  cam_.animating = false;
}

/* The finger's speed, smoothed over the last few moves (the glide starts from it). False while it is still within a
 * tap's reach (8 px): the view doesn't turn for a tap's jitter. */
bool Editor::finger_orbit_move(int dx, int dy, double now) {
  const bool orbiting = finger_travel_ >= ui_.px(8);
  finger_travel_ += std::abs((float)dx) + std::abs((float)dy);
  const float dt = (float)std::max(now - finger_last_, 1.0 / 240.0);
  finger_last_ = now;
  finger_vel_yaw_ = finger_vel_yaw_ * 0.5f + dx * 0.3f / dt * 0.5f;
  finger_vel_pitch_ = finger_vel_pitch_ * 0.5f + dy * 0.3f / dt * 0.5f;
  return orbiting;
}

/* Let go: a tap selects where it touched (a double tap frames the selection); a flick still moving glides on. A
 * press taken back (a second finger) does neither. */
void Editor::finger_orbit_end(const Recti &view, double now) {
  finger_orbit_ = false;
  const auto &in = ui_.in;
  if (in.cancelled) return;
  if (finger_travel_ < ui_.px(8)) {
    /* A double tap: Edit Mode's edge loop, as a double-click selects it, else the selection framed. */
    if (finger_double_ && edit_mode_ && elem_ != EditElement::Face) edit_select_loop(view, finger_press_x_, finger_press_y_, in.shift());
    else if (finger_double_) frame_selected();
    else click_select(view, finger_press_x_, finger_press_y_, (in.shift() || in.ctrl()) ? SEL_TOGGLE : SEL_REPLACE);
    return;
  }
  const bool still_moving = now - finger_last_ < 0.06;  // (a finger that stopped before lifting doesn't glide)
  const float speed = std::hypot(finger_vel_yaw_, finger_vel_pitch_);
  if (!still_moving || speed < 60.0f) return;
  const float cap = std::min(1.0f, 720.0f / speed);  // (at most two turns a second)
  glide_yaw_ = finger_vel_yaw_ * cap, glide_pitch_ = finger_vel_pitch_ * cap;
  glide_last_ = now;
}

/* The glide: it slows by about 4x every half second and stops at the next touch, press, wheel or gesture. */
void Editor::finger_glide(double now) {
  if (glide_yaw_ == 0.0f && glide_pitch_ == 0.0f) return;
  const auto &in = ui_.in;
  const bool touched = in.pressed[0] || in.pressed[1] || in.pressed[2] || in.wheel_y != 0.0f || in.touch_zoom != 1.0f ||
                       in.touch_pan_x != 0.0f || in.touch_pan_y != 0.0f || in.touch_rotate != 0.0f;
  /* (A glide whose view wasn't drawn for a while - another tab over it - ends rather than jumping on.) */
  if (touched || drag_ != Drag::None || now - glide_last_ > 0.2) {
    glide_yaw_ = glide_pitch_ = 0.0f;
    return;
  }
  const float dt = (float)std::min(now - glide_last_, 0.1);
  glide_last_ = now;
  cam_.yaw += glide_yaw_ * dt;
  cam_.pitch = clampf(cam_.pitch + glide_pitch_ * dt, -89.9f, 89.9f);
  const float decay = std::exp(-2.8f * dt);
  glide_yaw_ *= decay, glide_pitch_ *= decay;
  if (std::hypot(glide_yaw_, glide_pitch_) < 4.0f) glide_yaw_ = glide_pitch_ = 0.0f;
  ui_.redraw = true;
}

void Editor::set_active_camera(GameObject *g) {
  const uint64_t id = g ? g->id : 0;
  if (id == scene_->active_camera) return;
  scene_->active_camera = id;
  Log::info("Active Camera: %s", g ? g->name.c_str() : "none (\"Main Camera\" by name, else the first camera)");
  mark_changed("Set Active Camera");
}

Camera *Editor::main_camera(const Scene &s, GameObject **owner) {
  Camera *best = nullptr;
  GameObject *bo = nullptr;
  /* Rendering a camera sequence: the camera whose turn it is. */
  if (render_camera_override_ && &s == scene_.get())
    if (GameObject *g = s.find(render_camera_override_))
      if (Camera *c = g->camera())
        if (c->enabled && g->active_in_hierarchy()) {
          if (owner) *owner = g;
          return c;
        }
  /* The scene's Active Camera (ADR 0016), when it still is one. */
  if (s.active_camera)
    if (GameObject *g = s.find(s.active_camera))
      if (Camera *c = g->camera())
        if (c->enabled && g->active_in_hierarchy()) {
          if (owner) *owner = g;
          return c;
        }
  s.for_each_ordered([&](GameObject &g, int) {
    auto *c = g.camera();  // (the role lookup first: it runs for every object, every frame)
    if (!c || !c->enabled || !g.active_in_hierarchy()) return;
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
    if (!l || !l->enabled || l->mode == 2) return;  // a Baked light isn't rasterized (task 0021), in Solid mode either
    env.lights.push_back(to_render_light(g, *l));
  });
  return env;
}

void Editor::submit_scene(Renderer3D &r3d, const Scene &s, bool game, LightingEnv &, Vec3) {
  for (const DrawItem &it : solid_items(s, game)) r3d.add(it);
}

/* What Solid shading draws (submit_scene's items): each shown mesh with its materials, Edit Mode's cage mesh with its face
 * highlight, Weight Paint's mesh in its weight colours. */
std::vector<DrawItem> Editor::solid_items(const Scene &s, bool game) {
  std::vector<DrawItem> items;
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
    if (!game && weight_paint_ && g.id == wp_obj_) {  // Weight Paint: the mesh being painted, in its weight colours
      it.mesh = &mf->mesh->render_mesh();
      it.vertex_colors = wp_colors(*mf->mesh, *it.mesh);
      it.unlit = false;
      it.materials = nullptr;  // (with materials, Solid shades per pixel from them: the weight colours are the point here)
    }
    items.push_back(it);
  });
  return items;
}

void Editor::render_scene_view(const Recti &view) {
  scene_planes_slot_ = -1;  // (task 0204: whoever draws this frame writes its planes; the card says so again if it keeps them)
  scene_rt_.attach(fb_, view);
  float aspect = view.w / (float)std::max(1, view.h);
  Mat4 v = cam_.view(), p = cam_.proj(aspect);
  if (GameObject *pg = piloted_camera()) {
    /* Piloting: the camera's game view in its frame (its filters, lens, exposure, depth of field,
     * the scene's lights, whatever the Shading mode), a plain passepartout around it. The pilot
     * keeps the view and the camera in step, so the editor's projection over the whole view
     * matches the camera's over the frame: its depth and ids go into the Scene view's planes
     * and picking and the transform tools keep working. */
    const Camera *pc = pg->get<Camera>();
    const Recti fr = pilot_frame_rect(view, *pc).intersect(view);
    for (int y = 0; y < scene_rt_.height; y++)
      std::fill(scene_rt_.color + (size_t)y * scene_rt_.stride, scene_rt_.color + (size_t)y * scene_rt_.stride + scene_rt_.width, 0xFF1C1C1Cu);
    std::fill(scene_rt_.depth.begin(), scene_rt_.depth.end(), 1.0f);
    std::fill(scene_rt_.ids.begin(), scene_rt_.ids.end(), 0u);
    if (fr.w >= 2 && fr.h >= 2) {
      pilot_rt_.attach(fb_, fr);
      const Quat q = pg->world_rotation();
      const Vec3 eye = pg->world_position();
      const Mat4 cv = Mat4::look_at(eye, eye + q.rotate({0, 0, 1}), q.rotate({0, 1, 0}));
      const float fa = fr.w / (float)fr.h;
      const Mat4 cp = pc->projection(fa);
      render_camera(scene_r3d_, pilot_rt_, scene_ft_, cv, cp, eye, q.rotate({0, 0, 1}), pg, pc, fa);
      scene_stats_ = scene_r3d_.stats();
      /* Ids as they are; depth re-expressed in the editor's projection (its near / far differ from
       * the camera's), so the tools' visibility tests compare like with like. */
      const Mat4 to_editor = (p * v) * (cp * cv).inverse();
      const int ox = fr.x - view.x, oy = fr.y - view.y, fw = pilot_rt_.width, fh = pilot_rt_.height;
      JobSystem::global().parallel_for(fh, 32, [&](int64_t y0, int64_t y1) {
        for (int64_t y = y0; y < y1; y++) {
          const size_t s = (size_t)y * fw, d = (size_t)(y + oy) * scene_rt_.width + ox;
          std::memcpy(&scene_rt_.ids[d], &pilot_rt_.ids[s], sizeof(uint32_t) * fw);
          for (int x = 0; x < fw; x++) {
            const float z = pilot_rt_.depth[s + x];
            if (z >= 1.0f) continue;  // nothing drawn: stays 1
            const float nx = 2.0f * (x + 0.5f) / fw - 1.0f, ny = 1.0f - 2.0f * (y + 0.5f) / fh;
            const Vec4 e = to_editor * Vec4(nx, ny, z, 1.0f);
            scene_rt_.depth[d + x] = e.w != 0.0f ? std::max(0.0f, std::min(1.0f, e.z / e.w)) : 1.0f;
          }
        }
      });
    }
    scene_r3d_.rebind(&scene_rt_, v, p);  // the tools project with the editor's view, over the whole view
    return;
  }
  cage_on_gpu_ = false;
  overlays_on_gpu_ = false;
  const bool shaded = shading_ == Shading::Shaded || shading_ == Shading::ShadedWireframe;
  GameObject *fowner = nullptr;
  if (shaded && scene_filters_) main_camera(*scene_, &fowner);
  const bool filtered = fowner && !camera_filters(fowner).empty();
  /* On the card the Scene view draws at full size (task 0100): when its last frame drew there, this one will too. */
  const bool full = !filtered && scene_full_size();  // (Solid and Wireframe too since task 0195)
  record_scene_overlays(view, full && overlays_card_expected());  // (task 0192: for the card to draw with the picture)
  if (shaded) {
    scene_on_gpu_ = false;
    render_scaled(scene_r3d_, scene_rt_, scene_vs_, v, p, [&](RenderTarget &t) {
      if (filtered)  // the main camera's filters on the editor's view
        render_camera(scene_r3d_, t, scene_ft_, v, p, cam_.position(), cam_.forward(), fowner, nullptr, aspect, false, scene_lighting_);
      else {
        scene_gpu_call_ = true;
        scene_full_call_ = full;
        render_deferred(scene_r3d_, t, v, p, cam_.position(), false, scene_lighting_, nullptr);
        scene_gpu_call_ = scene_full_call_ = false;
      }
    }, fowner, true, true, full);
    scene_stats_ = scene_r3d_.stats();
  }
  else {  // (Solid and Wireframe: on the card too, task 0195)
    scene_on_gpu_ = false;
    render_scaled(scene_r3d_, scene_rt_, scene_vs_, v, p, [&](RenderTarget &t) {
      scene_gpu_call_ = true;
      scene_full_call_ = full;
      render_solid(t, v, p);
      scene_gpu_call_ = scene_full_call_ = false;
    }, nullptr, false, true, full);
  }
  ScopedTimer ot;
  scene_r3d_.begin_overlay_batch();  // the grid and the wireframes are thousands of lines: drawn in parallel bands
  draw_scene_overlays(view);  // (here unless the card drew them: task 0192)
  render_edit_cage();
  scene_r3d_.end_overlay_batch();
  prof_.ms_overlays += ot.ms();
}

void Editor::render_solid(RenderTarget &rt, const Mat4 &v, const Mat4 &p) {
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
  scene_r3d_.begin(&rt, v, p, env, raster_opt_);
  /* Task 0195: on the card when it can - the same items, the headlight, the editor's sky (scene_lights false: the
   * editor's background), no shadow map, as here; Gouraud (the default) lit per vertex by shade()'s formulas. */
  std::vector<DrawItem> items;
  if (shading_ != Shading::Wireframe) items = solid_items(*scene_, false);
  if (raster_opt_.shade != ShadeMode::DepthOnly && gpu_view_may_draw(rt, env, items, nullptr) &&
      gpu_view_draw(rt, v, p, env.camera_pos, env, items, nullptr, false, nullptr, raster_opt_)) {
    size_t tris = 0;
    for (const DrawItem &it : items) tris += it.mesh ? it.mesh->tri_count() : 0;
    scene_r3d_.note_submitted((int)items.size(), tris);
    scene_stats_ = scene_r3d_.stats();
    return;
  }
  scene_r3d_.clear_sky((p * v).inverse(), {0.33f, 0.47f, 0.68f}, {0.70f, 0.74f, 0.79f}, {0.27f, 0.26f, 0.25f});
  for (const DrawItem &it : items) scene_r3d_.add(it);
  if (!items.empty()) scene_r3d_.flush();
  scene_stats_ = scene_r3d_.stats();
}

GameObject *Editor::point_target(std::vector<int> &points) {
  points.clear();
  if (edit_mode_ || !pt_obj_ || pt_obj_ != active_ || pt_sel_.empty()) return nullptr;
  GameObject *g = scene_->find(pt_obj_);
  const Component *pc = g ? g->point_source() : nullptr;
  if (!pc) return nullptr;
  for (int i : pt_sel_)
    if (i >= 0 && i < pc->control_point_count()) points.push_back(i);
  return points.empty() ? nullptr : g;
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

/* ===================================================================== */
/* Picking                                                                */
/* ===================================================================== */

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
  cam_.animate_to(b.center(), cam_.yaw, cam_.pitch, dist, clock_now());
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
  if (!show_gizmos_ || weight_paint_ || sculpt_ || autorig_.active || tool_ == Tool::View || pp_.active || modal_.active || xf_.active || knife_.active || draw_.active || (drag_ != Drag::None && drag_ != Drag::Gizmo)) return false;

  /* Targets & pivot. */
  GameObject *eo = edit_mode_ ? edit_object() : nullptr;
  std::vector<int> cpts;  // a curve's selected control points (task 0042)
  GameObject *co = eo ? nullptr : point_target(cpts);
  std::vector<GameObject *> objs;
  Vec3 pivot;
  if (co) {
    const Component &pc = *co->point_source();
    AABB b;
    Vec3 sum(0.0f);
    for (int i : cpts) {
      const Vec3 w = co->world_matrix().point(pc.control_point(i));
      sum += w;
      b.add(w);
    }
    pivot = pivot_center_ ? b.center() : sum / (float)cpts.size();
  }
  else if (eo) {
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
  if (origin_edit_ && !eo && !co) {
    /* Editing origins: the handles sit on the active object's origin. */
    GameObject *a = active_object();
    pivot = (a ? a : objs.back())->world_position();
  }
  if (drag_ == Drag::Gizmo) pivot = gizmo_pivot_;
  Quat orient;
  GameObject *ref = eo ? eo : active_object();
  if ((space_local_ || tool_ == Tool::Scale) && ref) orient = ref->world_rotation();
  /* Edit Mode, Local: the selection's own axes, so a shape drawn on a slope or turned on its
   * face is moved, turned and scaled along its own sides and normal. */
  if (eo && (space_local_ || tool_ == Tool::Scale)) {
    Quat f;
    if (edit_selection_frame(f)) orient = f;
  }
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
        consider(H_MOVE_X + k, dist_to_segment_2d(m, a, b) + 0.5f);
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
        consider(H_SCALE_X + k, dist_to_segment_2d(m, a, b) + 0.5f);
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
          if (has_prev) consider(H_ROT_X + k, dist_to_segment_2d(m, prev, sp));
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

  /* Holding V (Unity's vertex snapping): a press anywhere starts a free move
   * from the selection's vertex nearest the mouse. */
  if (drag_ != Drag::Gizmo && in.key_down[platform::KEY_V] && do_move && scene_hovered_ && !u.wants_keyboard()) gizmo_hot_ = H_MOVE_FREE;
  /* A bone an IK chain turns would only be turned back (task 0048): its target is what moves it. */
  if (drag_ != Drag::Gizmo && gizmo_hot_ >= 0 && in.pressed[0] && !in.alt() && !eo && !co)
    for (GameObject *g : objs)
      if (g->get<Bone>() && ik_drives(*scene_, *g)) {
        Log::warn("'%s' is turned by IK: %s its target instead", g->name.c_str(), ik_end_driven(*scene_, *g) ? "turn" : "move");
        gizmo_hot_ = -1;
        break;
      }
  /* Start drag. */
  if (drag_ != Drag::Gizmo && gizmo_hot_ >= 0 && in.pressed[0] && !in.alt()) {
    drag_ = Drag::Gizmo;
    gizmo_typed_.clear();
    autopose_.bone = 0;  // (a drag cut short before - a modal opened - starts AutoPosing afresh, from the pose now)
    autopose_.start.clear();
    gizmo_axis_ = gizmo_hot_;
    gizmo_press_x_ = in.mx;
    gizmo_press_y_ = in.my;
    gizmo_live_delta_ = Vec3(0.0f);
    gizmo_live_angle_ = 0;
    gizmo_live_scale_ = Vec3(1.0f);
    gizmo_starts_.clear();
    gizmo_vert_starts_.clear();
    /* V held: vertex snapping from the selection's vertex nearest the mouse. */
    gizmo_vsnap_ = in.key_down[platform::KEY_V] && gizmo_hot_ <= H_MOVE_FREE;
    if (gizmo_vsnap_) {
      if (eo) {
        const Mesh &mm = **edit_mesh_ptr();
        float best = 1e30f;
        for (size_t i = 0; i < mm.vert_count() && i < vert_sel_.size(); i++) {
          if (!vert_sel_[i]) continue;
          Vec2 s;
          float z;
          const Vec3 wv = eo->world_matrix().point(mm.positions[i]);
          if (!scene_r3d_.project(wv, s, z)) continue;
          const float d = length(Vec2(view.x + s.x - in.mx, view.y + s.y - in.my));
          if (d < best) best = d, gizmo_vsnap_anchor_ = wv;
        }
        gizmo_vsnap_ = best < 1e29f;
      }
      else gizmo_vsnap_ = nearest_vertex_on_screen(view, in.mx, in.my, 1e9f, true, false, gizmo_vsnap_anchor_);
    }
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
    else if (co)
      for (int i : cpts) gizmo_vert_starts_.push_back(co->world_matrix().point(co->point_source()->control_point(i)));
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

  /* A curve's or lattice's points: each from where it started (world), written back in the object's space. */
  auto apply_points = [&](const std::function<Vec3(Vec3)> &to) {
    Component &pc = *co->point_source();
    const Mat4 inv = co->world_matrix().inverse();
    for (size_t k = 0; k < cpts.size() && k < gizmo_vert_starts_.size(); k++) pc.set_control_point(cpts[k], inv.point(to(gizmo_vert_starts_[k])));
  };
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
      const bool surface = !eo && in.ctrl() && in.shift() && !origin_edit_;
      if (snap && !surface && !gizmo_vsnap_) {
        /* Snap the delta in gizmo space (Unity increment snapping). */
        Vec3 local{dot(delta, axis[0]), dot(delta, axis[1]), dot(delta, axis[2])};
        for (int k = 0; k < 3; k++) local[k] = std::round(local[k] / snap_move_) * snap_move_;
        delta = axis[0] * local.x + axis[1] * local.y + axis[2] * local.z;
      }
      gizmo_snap_shown_ = false;
      if (gizmo_vsnap_ && in.key_down[platform::KEY_V]) {
        /* Vertex snapping (Unity's V): the anchor vertex lands on the vertex under the mouse. */
        Vec3 target;
        ScopedTimer st;
        const bool found = nearest_vertex_on_screen(view, in.mx, in.my, (float)u.px(24), false, !eo, target);
        prof_.ms_snap_pick += st.ms();
        if (found) {
          delta = target - gizmo_vsnap_anchor_;
          gizmo_snap_shown_ = true;
          gizmo_snap_point_ = target;
        }
      }
      else if (surface && !gizmo_starts_.empty()) {
        /* Surface snapping (Unity's Ctrl+Shift): rest the objects on what is under the mouse. */
        std::vector<GameObject *> moving;
        for (auto &s : gizmo_starts_)
          if (GameObject *g = scene_->find(s.id)) moving.push_back(g);
        const Ray r2 = scene_r3d_.screen_ray((float)(in.mx - view.x), (float)(in.my - view.y));
        Vec3 p, n;
        ScopedTimer st;
        const bool hit = !moving.empty() && raycast_surface({r2.origin, normalize(r2.dir)}, p, n, moving);
        prof_.ms_snap_pick += st.ms();
        if (hit) {
          GameObject *lead = moving.back();
          const GizmoStart &ls = gizmo_starts_.back();
          Quat rot;
          const Vec3 to = rest_on_surface(lead, p, n, ls.rot, surface_align_, rot);
          delta = to - ls.pos;
          if (surface_align_) lead->set_world_rotation(rot);
          gizmo_snap_shown_ = true;
          gizmo_snap_point_ = p;
        }
      }
      if (h <= H_MOVE_Z) typed_axis_move(axis[h], delta);  // (task 0190: type 2.5 while dragging: exactly 2.5 along the axis)
      gizmo_live_delta_ = delta;
      if (eo) {
        ProfileScope prof{prof_.ms_edit_apply, {}};
        MeshPtr &mp = *edit_mesh_ptr();
        Mesh &mm = *mesh_make_mutable(mp);
        Mat4 inv = eo->world_matrix().inverse();
        for (size_t i = 0; i < mm.vert_count() && i < gizmo_vert_starts_.size(); i++)
          if (float k = vw(i)) mm.positions[i] = inv.point(gizmo_vert_starts_[i] + delta * k);
        mm.touch();
        repair_moved_rings(mm);
      }
      else if (co)
        apply_points([&](Vec3 s) { return s + delta; });
      else if (gizmo_starts_.size() == 1 && autopose_gizmo(gizmo_starts_[0].id, gizmo_starts_[0].pos + delta)) {
        /* (AutoPosing: the whole body posed so the bone gets there) */
      } else
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
        ProfileScope prof{prof_.ms_edit_apply, {}};
        MeshPtr &mp = *edit_mesh_ptr();
        Mesh &mm = *mesh_make_mutable(mp);
        Mat4 inv = eo->world_matrix().inverse();
        for (size_t i = 0; i < mm.vert_count() && i < gizmo_vert_starts_.size(); i++)
          if (float k = vw(i)) /* proportional: the angle fades with the weight */
            mm.positions[i] = inv.point(pivot + (k < 1.0f ? Quat::axis_angle(ax, angle * k) : q).rotate(gizmo_vert_starts_[i] - pivot));
        mm.touch();
        repair_moved_rings(mm);
      }
      else if (co)
        apply_points([&](Vec3 s) { return pivot + q.rotate(s - pivot); });
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
        ProfileScope prof{prof_.ms_edit_apply, {}};
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
        repair_moved_rings(mm);
      }
      else if (co)
        apply_points([&](Vec3 s) {
          const Vec3 d = s - pivot;
          return pivot + axis[0] * (dot(d, axis[0]) * f.x) + axis[1] * (dot(d, axis[1]) * f.y) + axis[2] * (dot(d, axis[2]) * f.z);
        });
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
    if (co) label = h <= H_MOVE_FREE ? "Move Points" : h <= H_ROT_VIEW ? "Rotate Points" : "Scale Points";
    else if (origin_edit_ && !eo) {
      /* Only the origins move: the meshes (and children) are put back where they were in the world. */
      compensate_origin_drag();
      label = h <= H_MOVE_FREE ? "Move Origin" : "Rotate Origin";
    }
    mark_changed(label);
    if (!in.down[0]) {
      drag_ = Drag::None;
      gizmo_axis_ = -1;
      if (autopose_.bone) autopose_end();  // (the pose keyed at the playhead: the same undo step as the drag)
      /* Faces dropped onto another face of the mesh merge into it (not when the press was taken back: task 0213). */
      if (edit_mode_ && auto_fuse_ && elem_ == EditElement::Face && !ui_.in.cancelled)
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
  if (tool_ == Tool::View || weight_paint_ || sculpt_) return;  // Weight Paint, Sculpt: the brush, not the gizmo
  bool has_target = edit_mode_ ? std::find(vert_sel_.begin(), vert_sel_.end(), 1) != vert_sel_.end() : !selection_.empty();
  if (!has_target) return;
  /* Snapping markers: the vertex / surface point a move is snapping to, and with V
   * held (before dragging) the vertex the move would start from - Unity's square. */
  {
    Vec3 mark;
    bool show = false;
    if (drag_ == Drag::Gizmo && gizmo_snap_shown_) mark = gizmo_snap_point_, show = true;
    else if (drag_ != Drag::Gizmo && u.in.key_down[platform::KEY_V] && scene_hovered_ && !edit_mode_)
      show = nearest_vertex_on_screen(view, u.in.mx, u.in.my, 1e9f, true, false, mark);
    Vec2 s;
    float z;
    if (show && scene_r3d_.project(mark, s, z)) {
      const float x = view.x + s.x, y = view.y + s.y, h = (float)u.px(5);
      u.canvas.rect_outline({(int)(x - h), (int)(y - h), (int)(2 * h), (int)(2 * h)}, Color::hex(0xFFE040), u.px(2));
    }
  }
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
  if (weight_paint_) set_weight_paint(false);  // one mode at a time
  if (sculpt_) set_sculpt(false);
  if (playing_) return;
  if (pilot_cam_) toggle_pilot_camera();  // piloting hides the editor's overlays: editing needs them back
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

std::vector<uint32_t> Editor::selected_edge_chain(const Mesh &m, Vec3 near) const {
  /* Edge mode: the selected edges; otherwise edges (and wire edges) with both ends selected. */
  std::unordered_map<uint32_t, std::vector<uint32_t>> nb;
  auto add = [&](uint32_t a, uint32_t b) {
    nb[a].push_back(b);
    nb[b].push_back(a);
  };
  if (elem_ == EditElement::Edge)
    for (uint64_t k : edge_sel_) add((uint32_t)(k >> 32), (uint32_t)(k & 0xFFFFFFFF));
  else {
    auto sel = [&](uint32_t v) { return v < vert_sel_.size() && vert_sel_[v]; };
    std::unordered_set<uint64_t> seen;
    for (auto &e : m.edge_cache())
      if (sel(e.first) && sel(e.second) && seen.insert(Mesh::edge_key(e.first, e.second)).second) add(e.first, e.second);
    for (uint64_t k : m.loose_edges)
      if (sel((uint32_t)(k >> 32)) && sel((uint32_t)(k & 0xFFFFFFFF)) && seen.insert(k).second) add((uint32_t)(k >> 32), (uint32_t)(k & 0xFFFFFFFF));
  }
  uint32_t start = UINT32_MAX;
  float best = 1e30f;
  for (auto &kv : nb) {
    if (kv.second.size() > 2) return {};  // branches: not one chain
    if (kv.second.size() == 1 && length(m.positions[kv.first] - near) < best) best = length(m.positions[kv.first] - near), start = kv.first;
  }
  if (start == UINT32_MAX && !nb.empty()) start = nb.begin()->first;  // a closed loop: open it anywhere
  if (start == UINT32_MAX) return {};
  std::vector<uint32_t> path = {start};
  for (uint32_t prev = UINT32_MAX, cur = start;;) {
    uint32_t next = UINT32_MAX;
    for (uint32_t w : nb[cur])
      if (w != prev && std::find(path.begin(), path.end(), w) == path.end()) next = w;
    if (next == UINT32_MAX) break;
    path.push_back(next);
    prev = cur;
    cur = next;
  }
  return path;
}

bool Editor::edit_selection_frame(Quat &out) {
  GameObject *g = edit_object();
  if (!g || !edit_mesh_ptr()) return false;
  const Mesh &m = **edit_mesh_ptr();
  const Mat4 &w = g->world_matrix();
  /* The faces: selected ones (any mode, a face counts when all its corners are selected). */
  Vec3 n(0.0f), longest(0.0f);
  float best = 0.0f;
  for (size_t f = 0; f < m.face_count(); f++) {
    bool sel = f < face_sel_.size() && face_sel_[f];
    if (!sel && elem_ != EditElement::Face) {
      sel = true;
      for (uint32_t k = 0; k < m.face_size(f) && sel; k++) sel = m.face_verts(f)[k] < vert_sel_.size() && vert_sel_[m.face_verts(f)[k]];
    }
    if (!sel) continue;
    const Vec3 fn = w.dir(m.face_normal(f));
    n += fn;
    for (uint32_t k = 0; k < m.face_size(f); k++) {
      const Vec3 e = w.dir(m.positions[m.face_verts(f)[(k + 1) % m.face_size(f)]] - m.positions[m.face_verts(f)[k]]);
      if (length(e) > best * 1.0001f) best = length(e), longest = e;
    }
  }
  if (length(n) < 1e-9f) return false;
  n = normalize(n);
  Vec3 x = longest - n * dot(longest, n);
  if (length(x) < 1e-6f) {
    x = g->world_rotation().rotate({1, 0, 0});
    x = x - n * dot(x, n);
    if (length(x) < 1e-3f) x = g->world_rotation().rotate({0, 0, 1}) - n * dot(g->world_rotation().rotate({0, 0, 1}), n);
  }
  if (length(x) < 1e-6f) return false;
  x = normalize(x);
  /* Keep X pointing the object's way where it can (the longest edge has no direction of its own). */
  if (dot(x, g->world_rotation().rotate({1, 0, 0})) < -1e-4f) x = -x;
  out = quat_from_axes(x, n, cross(x, n));
  return true;
}

bool Editor::repair_moved_rings(Mesh &m) {
  std::vector<uint8_t> moved(m.vert_count(), 0);
  for (size_t v = 0; v < moved.size() && v < vert_sel_.size(); v++) moved[v] = vert_sel_[v];
  std::vector<uint8_t> dropped;
  if (!meshops::repair_rings(m, moved, &dropped)) return false;
  /* The selection follows the faces that stayed; the ring's new faces come last, unselected. */
  std::vector<uint8_t> sel;
  for (size_t f = 0; f < dropped.size(); f++)
    if (!dropped[f]) sel.push_back(f < face_sel_.size() ? face_sel_[f] : 0);
  sel.resize(m.face_count(), 0);
  face_sel_ = std::move(sel);
  return true;
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

/* An Edit Mode cage edge's colour: selected, a seam, or plain; 0 when N-gon mode hides it (inside a flat region). */
uint32_t Editor::cage_edge_color(const Mesh &m, uint32_t a, uint32_t b) const {
  const bool s = edge_is_selected(a, b);
  if (ngon_mode_ && !s && ngon_hidden_.count(Mesh::edge_key(a, b))) return 0;
  const bool seam = !m.seams.empty() && m.is_seam(a, b);
  return s ? Color::hex(0xFFA733) : (seam ? Color::hex(0xFF3030) : Color::hex(0x0A0A0A, 220));
}

uint32_t Editor::cage_dot_color(size_t v) const { return v < vert_sel_.size() && vert_sel_[v] ? Color::hex(0xFFA733) : Color::hex(0x000000); }

float Editor::cage_dot_radius() const { return std::max(1.5f, ui_.px(2.5f) * 1.0f); }

bool Editor::edge_is_selected(uint32_t a, uint32_t b) const {
  if (elem_ == EditElement::Edge) return edge_sel_.count(Mesh::edge_key(a, b)) > 0;
  return a < vert_sel_.size() && b < vert_sel_.size() && vert_sel_[a] && vert_sel_[b];
}

void Editor::edges_from_verts() {
  edge_sel_.clear();
  if (!edit_object()) return;
  const Mesh &m = **edit_mesh_ptr();
  edge_sel_.reserve(m.edge_cache().size());  // (task 0208: no rehashing as it fills)
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
  /* Task 0208: the edges' own selection only in Edge mode, the one that reads it (switching to it works it out from the
   * vertices): every edge of a 500k-face mesh into a set took 168 ms, and the Scene view's key walked it every frame. */
  if (sel && elem_ == EditElement::Edge) edges_from_verts();
  else edge_sel_.clear();
}

/* Auto Smooth: an operator that leaves more shallow-angle edges (a curved surface)
 * on a flat-shaded mesh turns on smooth shading by angle - curves look round,
 * hard edges stay hard. Meshes shaded per face are left as they are. */
void Editor::auto_smooth_after(Mesh *m, size_t before) {
  if (!m || !auto_smooth_applies(*m)) return;
  if (meshops::shallow_edges(*m, auto_smooth_angle_) <= before) return;
  meshops::shade_auto_smooth(*m, auto_smooth_angle_);
  /* Still the last operator's result (shading is part of it): a modal Bevel / Inset carries on. */
  if (last_op_.result == m) last_op_.result_version = m->version;
  Log::info("Auto Smooth: curved surface shaded smooth (edges sharper than %.0f degrees stay hard)", auto_smooth_angle_);
}

/* The operators that change the selection and nothing else (task 0166): they read the mesh the undo step shares -
 * no copy, so the views don't see a "new" mesh and only the Scene view's picture, whose cage shows the selection,
 * draws again. */
static bool selection_only_op(const std::string &op) {
  return op == "select_similar_normal" || op == "select_similar_area" || op == "select_by_material" || op == "select_sharp" ||
         op == "select_overlapping" || op == "select_coplanar" || op == "select_linked" || op == "select_more" || op == "select_less" ||
         op == "select_invert" || op == "select_non_manifold" || op == "select_ring" || op == "select_loop";
}

void Editor::edit_op(const std::string &op_in) {
  /* Extrude with Individual on is Extrude Individual Faces (one button, one switch). */
  const std::string op = op_in == "extrude" && extrude_individual_ && elem_ == EditElement::Face ? std::string("extrude_individual") : op_in;
  GameObject *g = edit_object();
  if (!g) return;
  if (op == "select_overlaps") {
    overlap_select();
    return;
  }
  if (op == "merge_overlaps") {
    overlap_merge();
    return;
  }
  MeshPtr &mp = *edit_mesh_ptr();
  if (edit_op_redoable(op)) {
    begin_redoable_op(op, *g, mp);
    return;
  }
  if (selection_only_op(op)) {
    select_op(op, *g, *mp);
    return;
  }
  edit_op_rest(op, *g, mp);
}

size_t Editor::edit_selection_faces(const Mesh &m) {
  vert_sel_.resize(m.vert_count(), 0);
  face_sel_.resize(m.face_count(), 0);
  if (elem_ == EditElement::Edge) verts_from_edges();
  else if (elem_ != EditElement::Face) {
    /* Derive faces from vertices like Blender does when switching modes. */
    for (size_t f = 0; f < m.face_count(); f++) {
      bool all = true;
      for (uint32_t k = 0; k < m.face_size(f); k++) all = all && vert_sel_[m.face_verts(f)[k]];
      face_sel_[f] = all;
    }
  }
  return (size_t)std::count(face_sel_.begin(), face_sel_.end(), 1);
}

bool Editor::auto_smooth_applies(const Mesh &m) const {
  /* Auto Smooth is for a mesh shaded flat as a whole: one already smooth, or shaded per face by the user, is left alone. */
  return auto_smooth_ && !m.smooth && m.face_smooth.empty();
}

size_t Editor::shallow_before(const Mesh &m) const {
  /* The count costs about 4 ms on 50k faces (23 before task 0165), so it is only taken when it can decide anything. */
  return auto_smooth_applies(m) ? meshops::shallow_edges(m, auto_smooth_angle_) : (size_t)-1;
}

void Editor::note_shallow_before(LastOp &L) const {
  L.shallow_before = shallow_before(*L.before);
  L.shallow_angle = auto_smooth_angle_;
  L.shallow_on = auto_smooth_;
}

size_t Editor::shallow_before_of(const LastOp &L) const {
  /* The count noted when the operator ran, unless Auto Smooth's settings changed since (an F9 re-run after
   * `autosmooth on 60`): then it is taken again, so a re-run never compares against another angle's count. */
  const bool same = L.shallow_angle == auto_smooth_angle_ && L.shallow_on == auto_smooth_;
  return same ? L.shallow_before : shallow_before(*L.before);
}

/* The mesh before the operator stays as it is - the pointer the undo step holds, never written (copy-on-write, ADR
 * 0003) - and run_last_op copies it once for the result. Before task 0165 the operator copied the mesh three times
 * (a mutable copy, the `before` copy, the copy run_last_op works on). */
/* Whether an operator needs selected faces (Merge by Distance works on everything when nothing is selected). */
static bool needs_selected_faces(const std::string &op) {
  return op == "extrude" || op == "inset" || op == "push_through" || op == "push_pull" || op == "poke" || op == "extrude_individual" || op == "grid" ||
         op == "array" || op == "taper" || op == "recess" || op == "shell" || op == "draft";
}

void Editor::begin_redoable_op(const std::string &op, GameObject &g, MeshPtr &mp) {
  ProfileScope prof{prof_.ms_edit_op, {}};  // (run_last_op's own scope counts only the re-runs)
  note_edit_selection_before();
  const Mesh &m = *mp;
  /* In edge mode the tools read exactly the selected edges. */
  const std::unordered_set<uint64_t> edges = edge_sel_;
  const size_t nsel = edit_selection_faces(m);
  if (needs_selected_faces(op) && !nsel) {
    Log::warn("Select faces first (press 3 for face mode)");
    return;
  }
  LastOp L;
  L.op = op;
  L.obj = g.id;
  L.before = mp;
  note_shallow_before(L);
  L.vsel = vert_sel_;
  L.fsel = face_sel_;
  L.esel = edges;
  L.elem = elem_;
  L.fuse = auto_fuse_;
  fill_last_op_settings(L, op);
  last_op_ = std::move(L);
  run_last_op(true);
}

/* The tool settings the operator starts from (F9 adjusts them afterwards). */
void Editor::fill_last_op_settings(LastOp &L, const std::string &op) const {
  if (op == "extrude") L.amount = extrude_dist_;
  else if (op == "merge_distance") {
    L.amount = merge_dist_;
    L.individual = false;  // F9: Unselected
  }
  else if (op == "inset") {
    L.individual = inset_individual_;
    L.amount = inset_individual_ ? inset_amount_ : inset_thickness_;
  }
  else if (op == "bevel") { L.amount = bevel_width_; L.segments = bevel_segments_; L.clamp = bevel_clamp_; L.profile = bevel_profile_; }
  else if (op == "bridge") L.segments = bridge_segments_;
  else if (op == "subdivide_edges") L.segments = subdivide_cuts_;
  else if (op == "loopcut") { L.segments = loop_cuts_; L.amount = loop_slide_; }
  else if (op == "smooth") L.amount = smooth_factor_;
  else if (op == "poke") L.amount = 0.0f;
  else if (op == "extrude_individual") L.amount = extrude_dist_;
  else if (op == "shrink_fatten") L.amount = 0.1f;
  else if (op == "to_sphere") L.amount = 1.0f;
  else if (op == "randomize") { L.amount = 0.05f; L.segments = 1; }
  else if (op == "push_pull") {
    L.amount = pp_last_distance_ != 0 ? pp_last_distance_ : extrude_dist_;
    L.individual = pp_individual_;
  }
  else if (op == "grid") { L.segments = grid_cols_; L.count = grid_rows_; }
  else if (op == "pipe") { L.amount = pipe_radius_; L.segments = pipe_sides_; }
  else if (op == "array") { L.count = array_count_; L.amount = array_spacing_; L.axis = array_axis_; }
  else if (op == "sketch_box" || op == "sketch_cylinder") { L.size = last_op_size_; L.segments = 24; }  // (task 0225)
  else if (op == "taper") { L.amount = taper_distance_; L.amount2 = taper_scale_; }
  else if (op == "recess") { L.amount = recess_depth_; L.amount2 = recess_border_; }
  else if (op == "shell" || op == "thicken") L.amount = shell_thickness_;
  else if (op == "draft") L.amount = draft_angle_;
}

/* The one selected edge (exactly two adjacent selected vertices) Loop Cut and Select Loop start from; false when
 * there is none or more than one (the caller says so). */
bool Editor::selected_edge(const Mesh &m, uint32_t &a, uint32_t &b) const {
  a = b = UINT32_MAX;
  for (auto &e : m.edge_cache())
    if (meshops::edge_selected(e.first, e.second, vert_sel_)) {
      if (a != UINT32_MAX) { a = UINT32_MAX; break; }
      a = e.first;
      b = e.second;
    }
  return a != UINT32_MAX;
}

/* Select Similar Normal / Area, Select By Material: the faces like the selected ones. */
void Editor::select_similar_op(const std::string &op, const Mesh &m) {
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

/* Select Sharp Edges (Blender's): edge mode with them selected. */
void Editor::select_sharp_op(const Mesh &m) {
  /* Blender's Select Sharp Edges: switches to edge mode with them selected. */
  if (elem_ != EditElement::Edge) set_edit_element(EditElement::Edge);
  edge_sel_.clear();
  for (uint64_t k : meshops::sharp_edges_by_angle(m, seam_angle_, true)) edge_sel_.insert(k);
  verts_from_edges();
  Log::info("Selected %zu sharp edge(s) (> %.0f deg)", edge_sel_.size(), seam_angle_);
  return;
}

/* Select Z-Fighting: the faces the check finds on top of each other. */
void Editor::select_overlapping_op(GameObject &g, const Mesh &m) {
  /* The Z-fighting check on this mesh; its faces selected (the list is in Modeling Tools). */
  const size_t n = zfight_scan(false);
  zfight_from_panel_ = false;  // outlines only while editing this mesh
  set_edit_element(EditElement::Face);
  face_sel_.assign(m.face_count(), 0);
  for (const ZFightIssue &z : zfight_) {
    if (z.obj_a == g.id && z.face_a < face_sel_.size()) face_sel_[z.face_a] = 1;
    if (z.obj_b == g.id && z.face_b < face_sel_.size()) face_sel_[z.face_b] = 1;
  }
  sync_vert_face_selection(true);
  Log::info(n ? "Select Z-Fighting: %zu pair(s) of faces on top of each other selected" : "Select Z-Fighting: none found", n);
  return;
}

/* Select Coplanar: the flat regions around the selected faces. */
void Editor::select_coplanar_op(const Mesh &m) {
  /* Selection only, and it only adds. In vertex or edge mode, the faces whose corners are all selected are
   * the start, and the faces found add their corners (and edges) to what is selected. */
  vert_sel_.resize(m.vert_count(), 0);
  if (elem_ != EditElement::Face) {
    face_sel_.assign(m.face_count(), 0);
    for (size_t f = 0; f < m.face_count(); f++) {
      bool all = m.face_size(f) > 0;
      for (uint32_t k = 0; k < m.face_size(f); k++) all = all && vert_sel_[m.face_verts(f)[k]];
      face_sel_[f] = all;
    }
  }
  bool any = false;
  for (uint8_t f : face_sel_) any = any || f;
  if (!any) {
    Log::warn("Select Coplanar: select a face first (or all the corners of one)");
    return;
  }
  const size_t added = meshops::select_coplanar(m, face_sel_);
  if (elem_ == EditElement::Face) sync_vert_face_selection(true);
  else
    for (size_t f = 0; f < m.face_count(); f++) {
      if (!face_sel_[f]) continue;
      const uint32_t *fv = m.face_verts(f);
      const uint32_t ns = m.face_size(f);
      for (uint32_t k = 0; k < ns; k++) {
        vert_sel_[fv[k]] = 1;
        if (elem_ == EditElement::Edge) edge_sel_.insert(Mesh::edge_key(fv[k], fv[(k + 1) % ns]));
      }
    }
  Log::info("Select Coplanar: %zu face%s added", added, added == 1 ? "" : "s");
  return;
}

/* Select Linked / More / Less / Invert / Non-Manifold / Edge Ring: through the vertices. */
void Editor::select_grow_op(const std::string &op, const Mesh &m) {
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

/* Select Edge Loop from the one selected edge. */
void Editor::select_loop_op(const Mesh &m) {
  uint32_t a, b;
  if (!selected_edge(m, a, b)) {
    Log::warn("Select exactly one edge (press 2 for edge mode)");
    return;
  }
  auto loop = meshops::edge_loop(m, a, b);
  for (uint32_t v : loop) vert_sel_[v] = 1;
  sync_vert_face_selection(false);
  Log::info("Edge loop: %zu vertices", loop.size());
}

/* The operators that change the selection and nothing else (selection_only_op lists them): they read the mesh the
 * undo step shares. The F9 panel ends here as it did before task 0166 (its operator's result is no longer the
 * latest edit), and the Profiler's operator row counts them. */
void Editor::select_op(const std::string &op, GameObject &g, const Mesh &m) {
  ProfileScope prof{prof_.ms_edit_op, {}};
  edit_selection_faces(m);  // (faces from the vertices or the edges, as every operator starts)
  last_op_.result = nullptr;
  if (op == "select_similar_normal" || op == "select_similar_area" || op == "select_by_material") return select_similar_op(op, m);
  if (op == "select_sharp") return select_sharp_op(m);
  if (op == "select_overlapping") return select_overlapping_op(g, m);
  if (op == "select_coplanar") return select_coplanar_op(m);
  if (op == "select_loop") return select_loop_op(m);
  select_grow_op(op, m);
}

/* The operators that aren't redoable: on a mutable copy of the mesh (shared with the undo step until now). */
void Editor::edit_op_rest(const std::string &op, GameObject &g_ref, MeshPtr &mp) {
  GameObject *g = &g_ref;
  note_edit_selection_before();
  ProfileScope prof{prof_.ms_edit_op, {}};
  Mesh &m = *mesh_make_mutable(mp);
  struct AutoSmooth {
    Editor &ed;
    size_t before;
    ~AutoSmooth() {
      if (MeshPtr *p = ed.edit_mesh_ptr())
        if (*p) ed.auto_smooth_after(p->get(), before);
    }
  } auto_smooth{*this, shallow_before(m)};
  /* In edge mode the tools read exactly the selected edges. */
  const std::unordered_set<uint64_t> edges = edge_sel_;
  meshops::EdgeSelectionScope scope(elem_ == EditElement::Edge ? &edges : nullptr);
  const size_t nsel = edit_selection_faces(m);
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
    /* UModeler's Follow: the face along a path - wire edges drawn from it, or edges of the mesh
     * picked after it (face mode: the face; edge mode: the path's edges; Follow again). */
    std::string err;
    if (elem_ == EditElement::Edge) {
      if (!follow_pick_.active || follow_pick_.obj != g->id) {
        Log::warn("Follow: first select the face to sweep (face mode) and press Follow, then select the path's edges here");
        return;
      }
      size_t face = SIZE_MAX;
      for (size_t f = 0; f < m.face_count() && face == SIZE_MAX; f++)
        if (length(m.face_center(f) - follow_pick_.center) < 1e-4f * std::max(1.0f, length(follow_pick_.center)) &&
            dot(normalize(m.face_normal(f)), follow_pick_.normal) > 0.999f)
          face = f;
      /* The selected edges as one chain, from the end nearest the face. The face's own sides don't
       * count: switching from face mode leaves them selected. */
      std::unordered_set<uint64_t> own;
      if (face != SIZE_MAX)
        for (uint32_t k = 0; k < m.face_size(face); k++)
          own.insert(Mesh::edge_key(m.face_verts(face)[k], m.face_verts(face)[(k + 1) % m.face_size(face)]));
      std::unordered_map<uint32_t, std::vector<uint32_t>> nb;
      for (uint64_t k : edge_sel_) {
        if (own.count(k)) continue;
        const uint32_t a = (uint32_t)(k >> 32), b = (uint32_t)(k & 0xFFFFFFFF);
        nb[a].push_back(b);
        nb[b].push_back(a);
      }
      uint32_t start = UINT32_MAX;
      float best = 1e30f;
      bool chain = !nb.empty();
      for (auto &kv : nb) {
        chain = chain && kv.second.size() <= 2;
        if (kv.second.size() == 1 && length(m.positions[kv.first] - follow_pick_.center) < best)
          best = length(m.positions[kv.first] - follow_pick_.center), start = kv.first;
      }
      if (face == SIZE_MAX || !chain || start == UINT32_MAX) {
        Log::warn(face == SIZE_MAX ? "Follow: the picked face is gone (pick it again in face mode)"
                                   : "Follow: select one open chain of edges for the path");
        return;
      }
      const std::vector<uint32_t> path = meshops::walk_wire_path(nb, start);
      if (!meshops::follow_path(m, face, path, &err)) {
        Log::warn("Follow: %s", err.c_str());
        return;
      }
      follow_pick_.active = false;
      vert_sel_.assign(m.vert_count(), 0);
      face_sel_.assign(m.face_count(), 0);
      edge_sel_.clear();
      Log::info("Follow: swept along %zu picked edge(s)", path.size() - 1);
    }
    else {
      size_t face = SIZE_MAX;
      for (size_t f = 0; f < face_sel_.size() && face == SIZE_MAX; f++)
        if (face_sel_[f]) face = f;
      if (face == SIZE_MAX) {
        Log::warn("Follow: select the face to sweep");
        return;
      }
      if (!meshops::follow(m, face, &err)) {
        /* No wire path from it: remember the face and let the path's edges be picked next. */
        follow_pick_ = {g->id, m.face_center(face), normalize(m.face_normal(face)), true};
        Log::info("Follow: face picked. Now select the path's edges (2 for edge mode) and press Follow again - or draw the path "
                  "from the face as wire edges (Polyline)");
        return;
      }
      vert_sel_.assign(m.vert_count(), 0);
      face_sel_.assign(m.face_count(), 0);
      Log::info("Follow: swept along the path");
    }
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
  else if (op == "dissolve_limited") {
    /* The selected faces only (any mode: faces whose corners are all selected), else the whole mesh. */
    const bool only_sel = nsel > 0;
    std::vector<Vec3> sel_pts;
    if (only_sel)
      for (size_t f = 0; f < m.face_count(); f++)
        if (face_sel_[f])
          for (uint32_t k = 0; k < m.face_size(f); k++) sel_pts.push_back(m.positions[m.face_verts(f)[k]]);
    const std::vector<uint8_t> mask = face_sel_;
    const size_t n = meshops::dissolve_limited(m, kNgonAngle, only_sel ? &mask : nullptr);
    if (!n) {
      Log::info(only_sel ? "Merge Coplanar: nothing to merge among the selected faces (they need flat neighbours in the selection)"
                         : "Merge Coplanar: nothing to merge (no flat regions of several faces)");
      return;
    }
    vert_sel_.assign(m.vert_count(), 0);
    face_sel_.assign(m.face_count(), 0);
    edge_sel_.clear();
    if (only_sel) {
      /* The merged faces stay selected: those whose corners were all corners of the selection. */
      const float eps = 1e-5f * std::max(1.0f, length(m.bounds().extent()));
      for (size_t f = 0; f < m.face_count(); f++) {
        bool inside = true;
        for (uint32_t k = 0; k < m.face_size(f) && inside; k++) {
          bool found = false;
          for (const Vec3 &p : sel_pts) found = found || length(p - m.positions[m.face_verts(f)[k]]) < eps;
          inside = found;
        }
        face_sel_[f] = inside;
      }
      sync_vert_face_selection(true);
      if (elem_ == EditElement::Edge) edges_from_verts();
    }
    Log::info("Merge Coplanar%s: %zu edges / vertices dissolved, %zu faces left", only_sel ? " (selected faces)" : "", n, m.face_count());
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
  else if (op == "delete_loose") {
    const bool any = std::find(vert_sel_.begin(), vert_sel_.end(), 1) != vert_sel_.end();
    const meshops::LooseCounts c = meshops::delete_loose(m, true, true, false, any ? &vert_sel_ : nullptr);
    if (!c.verts && !c.edges) {
      Log::info("Delete Loose: nothing loose %s", any ? "in the selection" : "in the mesh");
      return;
    }
    vert_sel_.assign(m.vert_count(), 0);
    if (elem_ == EditElement::Face) sync_vert_face_selection(true);
    else {
      face_sel_.assign(m.face_count(), 0);
      if (elem_ == EditElement::Edge) edges_from_verts();
    }
    Log::info("Delete Loose: removed %zu vertices and %zu wire edges", c.verts, c.edges);
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
  else if (op == "fill" || op == "smart_fill") {
    /* Make Face: one operation for Blender's F and Smart Fill. Open loops inside the selection
     * (holes, rings of wire edges; every hole when nothing is selected) are closed - flat ones
     * with a face, bent ones with a fan, cracks with no area welded shut. Selected vertices that
     * are no loop of open edges (loose points, a chain) become one face through them. */
    const bool any = std::find(vert_sel_.begin(), vert_sel_.end(), 1) != vert_sel_.end();
    const size_t f0 = m.face_count();
    const meshops::SmartFillResult r = meshops::smart_fill(m, any ? &vert_sel_ : nullptr);
    if (r.loops || r.welded) {
      vert_sel_.assign(m.vert_count(), 0);
      face_sel_.assign(m.face_count(), 0);
      for (size_t f = std::min(f0, m.face_count()); f < m.face_count(); f++) face_sel_[f] = 1;
      sync_vert_face_selection(true);
      if (elem_ == EditElement::Edge) edges_from_verts();
      std::string what = strprintf("Make Face: %zu hole(s) closed", r.loops);
      if (r.fans) what += strprintf(" (%zu bent, filled as fans)", r.fans);
      if (r.welded) what += strprintf(", %zu crack(s) welded shut", r.welded);
      if (r.no_area) what += strprintf(", %zu loop(s) with no area left as they are", r.no_area);
      Log::info("%s", what.c_str());
    }
    else if (any && meshops::fill(m, vert_sel_)) {
      m.prune_loose_edges();  // wire edges the new face now uses
      face_sel_.assign(m.face_count(), 0);
      face_sel_.back() = 1;
      sync_vert_face_selection(true);
      if (elem_ == EditElement::Edge) edges_from_verts();
      Log::info("Make Face: a %u-sided face through the selected vertices", m.face_size(m.face_count() - 1));
    }
    else {
      Log::warn(any ? (r.no_area ? "Make Face: the selected open edges enclose no area (they lie on a line)"
                                 : "Make Face: select 3 or more vertices, or the edges / faces around a hole")
                    : "Make Face: no holes to close (select vertices to make a face through them)");
      return;
    }
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
  m.touch();
  mark_changed("Edit: " + op);
}

/* Edit Mode operators by selection mode (ProBuilder's Vertex / Edge / Face
 * actions, Blender's Vertex / Edge / Face menus). The Mesh menu, the
 * Inspector and the shortcuts all offer only the current mode's operators. */
const char *const kEditGroups[kEditGroupCount] = {"Select", "Create & Extrude", "Cut & Divide", "Merge & Clean Up", "Deform",
                                                     "Shading & UV", "Hard Surface", "Other"};

int edit_op_group(const std::string &op) {
  if (starts_with(op, "select_")) return 0;
  static const char *create[] = {"push_pull", "extrude", "extrude_edges", "inset", "push_through", "extrude_individual", "bridge", "fill",
                                 "poke", "duplicate", "split", "spin", "follow", "shell", "thicken", "draft", "fuse"};
  static const char *cut[] = {"loopcut", "knife", "subdivide_edges", "connect", "slice", "bevel", "edge_split"};
  static const char *clean[] = {"merge_center", "merge_distance", "merge_overlaps", "select_overlaps", "collapse", "dissolve", "dissolve_vertices", "dissolve_faces", "dissolve_limited", "delete",
                                "delete_loose", "tris_to_quads", "triangulate_faces", "recalc_normals", "flip_faces"};
  static const char *deform[] = {"smooth", "shrink_fatten", "to_sphere", "randomize"};
  static const char *shading[] = {"shade_smooth", "shade_flat", "mark_sharp", "clear_sharp", "mark_seam", "clear_seam", "seams_from_sharp"};
  for (const char *o : create)
    if (op == o) return 1;
  for (const char *o : cut)
    if (op == o) return 2;
  for (const char *o : clean)
    if (op == o) return 3;
  for (const char *o : deform)
    if (op == o) return 4;
  for (const char *o : shading)
    if (op == o) return 5;
  for (const char *o : {"grid", "pipe", "array", "taper", "recess"})
    if (op == o) return 6;
  return 7;
}

const std::vector<EditOpPair> &edit_op_pairs() {
  static const std::vector<EditOpPair> p = {
      {"mark_seam", "clear_seam", "Mark", "Clear", "Seam"},
      {"mark_sharp", "clear_sharp", "Mark", "Clear", "Sharp"},
      {"shade_smooth", "shade_flat", "Smooth", "Flat", "Shade"},
      {"triangulate_faces", "tris_to_quads", "Tris", "Quads", "To"},
      {"select_more", "select_less", "More", "Less", "Grow"},
  };
  return p;
}

const std::vector<EditOpInfo> &edit_op_table() {
  enum { V = 1, E = 2, F = 4, ALL = 7 };
  static const std::vector<EditOpInfo> ops = {
      {"extrude_edges", "Extrude", "E", "Extrude the selected vertices and edges, then move them: an edge grows a face, a lone vertex\n"
       "a new edge (start a shape from one vertex). Blender: E in vertex / edge mode.", V | E},
      {"merge_center", "Merge at Center", "Alt+M", "Weld the selected vertices at their centre. Blender: M > At Center.", V},
      {"connect", "Connect Vertex Path", "J", "Split a face between two selected vertices that aren't neighbours. Blender: J.", V},
      {"fill", "Make Face", "Alt+F", "Close the holes and wire loops in the selection with faces (every hole when nothing is selected):\n"
       "flat ones get one face, bent ones a fan, cracks with no area are welded shut. Selected vertices that are\n"
       "no loop (loose points, a chain) become one face through them. Blender: F and Fill Holes. SketchUp: closing a loop.", ALL},
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
      {"follow", "Follow", "", "Sweep the selected face along a path (UModeler: Follow; SketchUp: Follow Me): wire edges drawn from it\n"
       "with the Polyline tool, or - with no such path - pick the face, then select the path's edges in edge mode and\n"
       "press Follow again.", F | E},
      {"select_similar_normal", "Similar Normal", "", "Select the faces facing the same way as the selected ones. Blender: Select Similar > Normal.", F},
      {"select_similar_area", "Similar Area", "", "Select the faces of about the same area. Blender: Select Similar > Area.", F},
      {"select_by_material", "Same Material", "", "Select every face using the selected faces' material slots. Blender: Material > Select.", F},
      {"seams_from_sharp", "Seams from Sharp", "", "Mark a seam on every sharp edge: faces meeting at more than the Sharp Angle, or edges marked sharp.\n"
       "Blender: Select Sharp Edges, then Mark Seam.", E},
      {"shell", "Shell", "", "Hollow the solid: the selected faces open up and the rest becomes a wall of the Thickness (F9 adjusts it).\n"
       "Plasticity / CAD: Shell.", F},
      {"draft", "Draft", "", "Tilt the selected side faces by the Angle (F9) so they narrow toward the top (a mould's draft).\n"
       "Plasticity: Draft Face.", F},
      {"thicken", "Thicken", "", "Give the whole surface a Thickness, set in F9 (a flat face becomes a slab). Plasticity: Thicken.", ALL},
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
      {"inset", "Inset", "Ctrl+I", "Make a smaller copy of the selected faces inside them: each face on its own (Individual),\n"
       "or the whole selection as one face (Inset Individual off). F9 switches between them. Blender: I.", F},
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
      {"merge_distance", "Merge by Distance", "", "Weld the selected vertices that are closer than the Merge Distance (all of them if nothing\n"
       "is selected). F9: the distance, and Unselected to weld onto unselected vertices too. Blender: Merge > By Distance.", ALL},
      {"select_overlapping", "Select Z-Fighting", "", "Find faces on top of each other (they flicker: z-fighting) and select them;\n"
       "Modeling Tools > Check: Z-Fighting lists them and removes the ones that can go.", ALL},
      {"grid", "Grid", "", "Cut each selected quad into a grid of Grid Columns x Grid Rows (neighbours are split to match).\n"
       "Click it for the helper: the wheel sets columns, Shift+wheel rows (F9 afterwards).", F},
      {"pipe", "Pipe", "", "A round tube of Pipe Radius along the selected edges (a chain), or along wire edges drawn with the\n"
       "Polyline tool; capped at both ends. Helper: the mouse sets the radius, the wheel the sides.", V | E},
      {"array", "Array", "", "Repeat the selected faces Array Count times, Array Spacing apart along the selection's X / Y / Z\n"
       "(Array Axis). Helper: the mouse sets the spacing, the wheel the count, X / Y / Z the axis.", F},
      {"taper", "Taper Extrude", "", "Extrude the selected faces by Taper Distance with the end scaled by Taper Scale (a frustum).\n"
       "Helper: the mouse sets the distance, the wheel the end's scale.", F},
      {"recess", "Recess / Plate", "", "Inset the selection as one panel by Recess Border, then push its middle in by Recess Depth\n"
       "(negative: a raised plate). Helper: the mouse sets the depth, the wheel the border.", F},
      {"select_overlaps", "Select Overlapping", "", "Select vertices lying on top of each other and edges running along each other\n"
       "(shown live as yellow rings and magenta lines; Modeling Tools > Check: Overlapping Vertices & Edges).", ALL},
      {"merge_overlaps", "Merge Overlapping", "", "Weld vertices lying on top of each other (doubled edges on them become one).", ALL},
      {"delete_loose", "Delete Loose", "", "Remove vertices and edges no face uses (wire edges, stray points), within the selection,\n"
       "or in the whole mesh when nothing is selected. Blender: Mesh > Clean Up > Delete Loose.", ALL},
      {"recalc_normals", "Recalculate Normals", "Shift+N", "Make every face point outward. Blender: Mesh > Normals > Recalculate Outside.", ALL},
      {"shrink_fatten", "Shrink/Fatten", "Alt+S", "Move the selected vertices along their normals (F9 sets the distance). Blender: Alt+S.", ALL},
      {"to_sphere", "To Sphere", "Shift+Alt+S", "Pull the selected vertices onto a sphere around their centre (F9 sets how far). Blender: Shift+Alt+S.", ALL},
      {"randomize", "Randomize", "", "Jitter the selected vertices (F9: amount and seed). Blender: Transform > Randomize.", ALL},
      {"knife", "Knife / Line", "K", "Draw a line across a face to split it: click a corner or an edge, then another on the same face.\n"
       "Snaps to endpoints (green), midpoints (cyan) and edges (red). SketchUp: Line tool. Blender: K.", ALL},
      {"dissolve_limited", "Merge Coplanar", "", "Turn every flat region into one n-gon and drop corners on straight edges - within the selected\n"
       "faces only when some are selected (the rest of the mesh keeps its edges), the whole mesh otherwise.\n"
       "Blender: Delete > Limited Dissolve. SketchUp shows flat regions this way.", ALL},
      {"origin_to_selection", "Origin to Selection", "", "Move the object's origin to the centre of the selected elements (the mesh stays put).\n"
       "Blender: Shift+S > Cursor to Selected, then Set Origin > Origin to 3D Cursor.", ALL},
      {"select_linked", "Select Linked", "Ctrl+L", "Select everything connected to the selection. Blender: Ctrl+L.", ALL},
      {"select_more", "Select More", "Ctrl+=", "Grow the selection by one ring. Blender: Ctrl+Numpad +.", ALL},
      {"select_less", "Select Less", "Ctrl+-", "Shrink the selection by one ring. Blender: Ctrl+Numpad -.", ALL},
      {"select_invert", "Invert Selection", "Ctrl+Shift+I", "Select what isn't, deselect what is. Blender: Ctrl+I.", ALL},
      {"select_non_manifold", "Select Non-Manifold", "", "Select holes and edges shared by more than two faces. Blender: Select > All by Trait > Non Manifold.", ALL},
      {"select_coplanar", "Select Coplanar", "", "Add every face lying in the same plane as a selected face (the same direction and the same\n"
       "height), joined to it or not. Blender: Select > Select Similar > Coplanar.", ALL},
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
  else if (op == "grid" || op == "pipe" || op == "array" || op == "taper" || op == "recess") modal_begin(op);  // the drag helper
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
  const float r = prop_radius_;
  auto cell = [&](Vec3 p) {
    return std::array<int64_t, 3>{meshops::grid_cell((double)p.x / r), meshops::grid_cell((double)p.y / r), meshops::grid_cell((double)p.z / r)};
  };
  auto key = meshops::grid_key;
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
  u.canvas.fill_rect(bar, u.theme.bar);
  {
    /* Which camera the view shows: the scene's Active Camera (Unity: the Display dropdown; Blender: the
     * scene camera). Choosing one here sets it. */
    std::vector<GameObject *> &cams = game_cam_objs_;
    std::vector<std::string> &names = game_cam_names_;
    std::vector<const char *> &opts = game_cam_opts_;
    cams.clear();
    names.clear();
    opts.clear();
    scene_->for_each_ordered([&](GameObject &g, int) {  // the cameras the view could show: enabled, on an active object
      if (Camera *c = g.camera(); c && c->enabled && g.active_in_hierarchy()) cams.push_back(&g);
    });
    GameObject *shown = nullptr;
    main_camera(*scene_, &shown);
    int cur = -1;
    for (size_t i = 0; i < cams.size(); i++) {
      const bool by_rule = cams[i] == shown && cams[i]->id != scene_->active_camera && !render_camera_override_;
      names.push_back(cams[i]->name + (by_rule ? "  (by name)" : ""));
      if (cams[i] == shown) cur = (int)i;
    }
    for (auto &n : names) opts.push_back(n.c_str());
    if (opts.empty()) opts.push_back("No cameras");
    int pick = std::max(0, cur);
    if (u.combo(u.id("game_camera"), {bar.x + u.px(6), bar.y + u.px(3), u.px(170), bar.h - u.px(6)}, pick, opts.data(), (int)opts.size()) && !cams.empty() &&
        pick >= 0 && pick < (int)cams.size() && cams[(size_t)pick]->id != scene_->active_camera)
      set_active_camera(cams[(size_t)pick]);
    u.tooltip("The camera this view shows: the scene's Active Camera (Blender: the scene camera; Unity: MainCamera).\n"
              "Without one, \"Main Camera\" by name, else the first camera.");
  }
  if (bar.w > u.px(460)) u.label({bar.x + u.px(182), bar.y, u.px(110), bar.h}, "Free Aspect", u.theme.text_dim);  // (not under the buttons)
  int h = bh - u.px(6);
  {
    /* Composition guides of the camera shown (task 0033). */
    GameObject *shown = nullptr;
    Camera *gc = main_camera(*scene_, &shown);
    Recti gr{bar.right() - u.px(144), bar.y + u.px(3), u.px(68), h};
    game_guides_rects_["button"] = gr;
    if (u.button(gr, "Guides", gc && gc->show_guides) && gc) u.open_popup(u.id("game_guides"), {gr.x, gr.bottom(), gr.w, 0});
    u.tooltip("Composition guides over the camera's frame: rule of thirds, golden ratio, safe areas...\n"
              "(Blender: Camera > Viewport Display > Composition Guides). Never in renders.");
    u.popup(u.id("game_guides"), u.px(200), [this, gid = shown ? shown->id : 0] {
      auto &u = ui_;
      /* (Run at the frame's end: the camera by id, as it is then - deleted or swapped by undo, no menu.) */
      GameObject *go = gid ? scene_->find(gid) : nullptr;
      Camera *gc = go ? go->get<Camera>() : nullptr;
      if (!gc) return;
      auto item = [&](const char *label, bool &v) {
        const bool clicked = u.menu_item(label, nullptr, v);
        game_guides_rects_[label] = u.last_widget();
        if (clicked) {
          v = !v;
          if (&v != &gc->show_guides && v) gc->show_guides = true;  // picking a guide shows the guides
          mark_changed(std::string("Guides: ") + label);
        }
      };
      item("Show Guides", gc->show_guides);
      u.menu_separator();
      item("Thirds", gc->guide_thirds);
      item("Center", gc->guide_center);
      item("Center Diagonal", gc->guide_center_diagonal);
      item("Golden Ratio", gc->guide_golden);
      item("Golden Triangle A", gc->guide_golden_tri_a);
      item("Golden Triangle B", gc->guide_golden_tri_b);
      item("Harmonious Triangle A", gc->guide_harmony_a);
      item("Harmonious Triangle B", gc->guide_harmony_b);
      item("Title Safe", gc->guide_title_safe);
      item("Action Safe", gc->guide_action_safe);
    });
  }
  Recti sr{bar.right() - u.px(70), bar.y + u.px(3), u.px(62), h};
  if (u.button(sr, "Stats", game_stats_overlay_)) game_stats_overlay_ = !game_stats_overlay_;
  u.tooltip("Rendering statistics (Unity: Game view > Stats).");
  Recti view{r.x, r.y + bh, r.w, r.h - bh};
  game_rect_ = view;
  if (view.w < 8 || view.h < 8) {  // (the dock doesn't paint this view's content: it paints every pixel itself)
    u.canvas.fill_rect(view, u.theme.panel);
    return;
  }
  GameObject *owner = nullptr;
  Camera *cam = main_camera(*scene_, &owner);
  if (!cam) {
    u.canvas.fill_rect(view, 0xFF000000);
    u.label(view, "No cameras rendering", u.theme.text, ui::Align::Center);
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
  /* The Game view always uses the full material pipeline with shadows (Unity Game view),
   * through the camera's filters. */
  {
    /* While playing it changes every frame; otherwise the picture is reused until the scene, the
     * camera or the view's size changes. */
    ScopedTimer rt;
    const bool animating = playing_ && !paused_;
    /* (Paused and stepped: the play time moves a frame and the grain's seed with it, so it is in the key.) */
    const uint64_t time_key = playing_ ? (uint64_t)(int64_t)std::floor(std::fmod((double)play_time_, 86400.0) * 24.0) * 0x9E3779B97F4A7C15ull : 0;
    const uint64_t key =
        animating ? 0 : camera_render_hash(owner, cam) ^ raster_opt_key() ^ time_key ^ ((uint64_t)img.w << 48) ^ ((uint64_t)img.h << 32) ^ ((uint64_t)img.x << 16) ^ (uint64_t)img.y;
    const bool hit = view_cache_hit(game_cache_, key, img);
    if (hit && !cache_verify_) prof_.view_cache_hits++;
    else if (!hit && hold_other_views() && view_cache_show(game_cache_, img)) prof_.views_held++;  // (task 0103: drawn on release)
    else {
      render_scaled(game_r3d_, game_rt_, game_vs_, v, p, [&](RenderTarget &t) {
        render_camera(game_r3d_, t, game_ft_, v, p, eye, q.rotate({0, 0, 1}), owner, cam, aspect, true, true, playing_);  // paused: the same time, the same grain
      }, owner, false, false);  // (nothing picks in the Game view: no full-size depth and ids)
      game_stats_ = game_r3d_.stats();
      prof_.view_renders++;
      if (hit) view_cache_verify(game_cache_, img, "Game view");
      view_cache_store(game_cache_, key, img);
    }
    prof_.ms_game += rt.ms();
  }
  /* Composition guides over the camera's frame: the image, or a filter's letterboxed frame inside it. */
  {
    Recti frame = img;
    const FilterStack &fs = camera_filters(owner);
    if (!fs.empty()) {
      int iw, ih;
      Recti d;
      filter_layout(fs, img.w, img.h, iw, ih, d);
      frame = {img.x + d.x, img.y + d.y, d.w, d.h};
    }
    game_guides_frame_ = frame;
    draw_camera_guides(*cam, frame);
  }
  /* Task 0233: the sketch tool in the Game view - the pen draws in the game camera's perspective (Play stopped or
   * paused: a running game takes the keys and the clicks). */
  if (sketch_.active && !playing_) {
    sketch_bind_game(img, *cam, *owner, v, p, u.hovered(img) && !u.any_popup_open());
    sketch_update(img);
    sketch_draw(img);
  }
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
  return op == "extrude" || op == "inset" || op == "bevel" || op == "bridge" || op == "push_through" || op == "subdivide_edges" || op == "merge_distance" ||
         op == "loopcut" || op == "smooth" || op == "push_pull" || op == "poke" || op == "extrude_individual" ||
         op == "shrink_fatten" || op == "to_sphere" || op == "randomize" || op == "grid" || op == "pipe" || op == "array" || op == "taper" ||
         op == "recess" || op == "shell" || op == "thicken" || op == "draft" || op == "sketch_box" || op == "sketch_cylinder";
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
  double unused = 0;
  ProfileScope prof{first ? unused : prof_.ms_edit_op, {}};  // (a first run is timed by begin_redoable_op, which includes it)
  /* Start again from the mesh as it was before the operator: the one copy an operator makes (task 0165), without
   * its caches (task 0185: most operators change the topology, and the copied render meshes were most of the copy). */
  mf->mesh = mesh_copy_bare(*L.before);
  Mesh &m = *mf->mesh;
  struct AutoSmooth {
    Editor &ed;
    MeshFilter *mf;
    const Mesh *shared;  // the mesh before the operator: a failed re-run puts it back, and it is never written
    size_t before;
    ~AutoSmooth() {
      if (mf->mesh && mf->mesh.get() != shared) ed.auto_smooth_after(mf->mesh.get(), before);
    }
  } auto_smooth{*this, mf, L.before.get(), shallow_before_of(L)};
  m.version = L.before->version + 1 + (++redo_serial_);  // never reuse an old version number
  vert_sel_ = L.vsel;
  face_sel_ = L.fsel;
  elem_ = L.elem;
  meshops::EdgeSelectionScope scope(L.elem == EditElement::Edge ? &L.esel : nullptr);
  std::string err;
  bool ok = true;
  const std::string &op = L.op;
  if (op == "merge_distance") {
    /* The selection (every vertex when nothing is selected), Blender's way. */
    std::vector<uint8_t> sel = vert_sel_;
    sel.resize(m.vert_count(), 0);
    if (std::find(sel.begin(), sel.end(), 1) == sel.end()) std::fill(sel.begin(), sel.end(), (uint8_t)1);
    const size_t n = meshops::merge_by_distance_selected(m, std::max(0.0f, L.amount), sel, L.individual);
    vert_sel_.assign(m.vert_count(), 0);
    face_sel_.assign(m.face_count(), 0);
    L.message = strprintf("Removed %zu vertices", n);
    Log::info("Merge by Distance: removed %zu vertices", n);
  }
  else if (op == "extrude" || op == "inset") {
    if (op == "extrude") meshops::extrude_faces(m, face_sel_, L.amount);
    else if (L.individual) meshops::inset_faces(m, face_sel_, L.amount);
    else meshops::inset_region(m, face_sel_, std::max(0.0f, L.amount));
    vert_sel_.assign(m.vert_count(), 0);
    sync_vert_face_selection(true);
  }
  else if (op == "bevel") ok = meshops::bevel_edges(m, vert_sel_, face_sel_, L.amount, L.segments, &err, L.clamp, L.profile);
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
    uint32_t a, b;
    const bool one = selected_edge(m, a, b);
    std::vector<uint32_t> made;
    if (one) made = meshops::loop_cut(m, a, b, L.segments, L.amount);
    ok = !made.empty();
    if (!ok) err = one ? "Loop Cut needs quads around the edge" : "Select exactly one edge (press 2 for edge mode)";
    vert_sel_.assign(m.vert_count(), 0);
    for (uint32_t v : made) vert_sel_[v] = 1;
    face_sel_.assign(m.face_count(), 0);
    sync_vert_face_selection(false);
  }
  else if (op == "grid") {
    ok = meshops::grid_faces(m, face_sel_, L.segments, L.count) > 0;
    if (!ok) err = "Grid: select quads (4-sided faces)";
    vert_sel_.assign(m.vert_count(), 0);
    sync_vert_face_selection(true);
  }
  else if (op == "pipe") {
    const std::unordered_set<uint64_t> keep_edges = edge_sel_;
    edge_sel_ = L.esel;
    const std::vector<uint32_t> path = selected_edge_chain(m, Vec3(0.0f));
    edge_sel_ = keep_edges;
    ok = meshops::pipe(m, path, L.amount, L.segments, &err);
    face_sel_.assign(m.face_count(), 0);
    vert_sel_.assign(m.vert_count(), 0);
    if (L.elem == EditElement::Edge) edge_sel_.clear();
  }
  else if (op == "sketch_box" || op == "sketch_cylinder") {  // (task 0225: the whole mesh from its numbers)
    sketch_build_mesh(op, m, L.size, L.segments);
    vert_sel_.assign(m.vert_count(), 0);
    face_sel_.assign(m.face_count(), 0);
  }
  else if (op == "array") {
    Quat frame;
    if (!edit_selection_frame(frame)) frame = Quat();
    Vec3 ax(0.0f);
    ax[std::max(0, std::min(2, L.axis))] = 1.0f;
    const Vec3 off = g->world_matrix().inverse().dir(frame.rotate(ax)) * L.amount;
    ok = meshops::array_faces(m, face_sel_, L.count, off) > 0 || L.count <= 1;
    if (!ok) err = "Array: select faces first";
    vert_sel_.assign(m.vert_count(), 0);
    sync_vert_face_selection(true);
  }
  else if (op == "taper") {
    meshops::extrude_taper(m, face_sel_, L.amount, L.amount2);
    vert_sel_.assign(m.vert_count(), 0);
    sync_vert_face_selection(true);
  }
  else if (op == "recess") {
    meshops::recess(m, face_sel_, L.amount2, L.amount);
    vert_sel_.assign(m.vert_count(), 0);
    sync_vert_face_selection(true);
  }
  else if (op == "shell" || op == "thicken") {
    std::vector<uint8_t> open(m.face_count(), 0);
    if (op == "shell") open = face_sel_;
    ok = meshops::shell(m, open, L.amount, &err);
    vert_sel_.assign(m.vert_count(), 0);
    face_sel_.assign(m.face_count(), 0);
    edge_sel_.clear();
  }
  else if (op == "draft") {
    ok = meshops::draft(m, face_sel_, L.amount, {0, 1, 0});
    if (!ok) err = "Draft: select side faces (faces facing along the object's Y have no draft)";
  }
  else if (op == "push_pull") {
    ok = meshops::push_pull_multi(m, face_sel_, L.amount, L.fuse, L.individual, nullptr, &err);
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
  if (ok) m.adopt_caches(*L.before);  // (an operator that only moved vertices: the render meshes follow in place)
  if (!ok) {
    /* Back to the untouched mesh, as a copy (task 0165): the very pointer would bring back an earlier render-cache key
     * whose picture was drawn with state the key doesn't hold (verify mode found it after an Esc). */
    mf->mesh = mesh_copy(*L.before);
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
      {"to_sphere", "To Sphere"},      {"randomize", "Randomize"},
      {"grid", "Grid"},                {"pipe", "Pipe"},
      {"array", "Array"},              {"taper", "Taper Extrude"},
      {"sketch_box", "Box from Sketch"}, {"sketch_cylinder", "Cylinder / Cone from Sketch"},
      {"recess", "Recess / Plate"},   {"shell", "Shell"},
      {"thicken", "Thicken"},          {"draft", "Draft"}};
  auto it = titles.find(L.op);
  std::string title = it == titles.end() ? L.op : it->second;
  const int rh = u.row_h(), pad = u.px(6), w = std::min(u.px(270), view.w - u.px(20));
  /* Rows: the operator's own fields, then Move, Along Normal, Orientation. */
  int fields = L.op == "bevel" || L.op == "sketch_cylinder" ? 4 : L.op == "push_pull" || L.op == "array" || L.op == "sketch_box" ? 3 :
               L.op == "grid" || L.op == "pipe" || L.op == "taper" || L.op == "recess" ? 2 : L.op == "randomize" ? 2 : L.op == "extrude" || L.op == "loopcut" || L.op == "smooth" ? 2 : L.op == "bridge" ? 4 : 1;
  int rows = last_op_open_ ? fields + 3 + (L.message.empty() ? 0 : 1) : 0;
  int h = rh + u.px(6) + rows * (rh + u.px(2)) + (rows ? pad : 0);
  Recti r{view.x + u.px(10), view.bottom() - h - u.px(10), w, h};
  last_op_rect_ = r;
  u.frame(r, (u.theme.popup & 0x00FFFFFFu) | (235u << 24), u.theme.border, u.px(4));
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
  else if (L.op == "merge_distance") {
    fl("Merge Distance", L.amount, 0.0005f, 0.0f, 1000.0f);
    Recti cr = row("Unselected");
    changed |= u.checkbox({cr.x, cr.y, cr.h, cr.h}, L.individual);
    u.tooltip("Also weld selected vertices onto unselected ones nearby (Blender: Merge > By Distance > Unselected).");
  }
  else if (L.op == "inset") {
    Recti cr = row("Individual");
    if (u.checkbox({cr.x, cr.y, cr.h, cr.h}, L.individual)) {
      changed = true;
      L.amount = L.individual ? inset_amount_ : inset_thickness_;  // each mode keeps its own amount
    }
    u.tooltip("On: each selected face gets its own inset. Off: the selection is inset as one face\n"
              "(only its outline moves in). Blender: Inset Faces > Individual.");
    if (L.individual) fl("Amount", L.amount, 0.005f, 0.0f, 1.0f);
    else fl("Thickness", L.amount, 0.005f, 0.0f, 1000.0f);
  }
  else if (L.op == "bevel") {
    fl("Width", L.amount, 0.005f, 0.0001f, 1000.0f);
    ifield("Segments", L.segments, 1, 64);
    fl("Profile", L.profile, 0.01f, 0.0f, 1.0f);
    u.tooltip("The shape across the bevel (2+ segments): 0.5 round, 0.25 flat, toward 1 convex (out to a square\n"
              "corner), toward 0 concave (scooped in). Blender: Profile. Alt+wheel while dragging the bevel.");
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
  else if (L.op == "shell" || L.op == "thicken") fl("Thickness", L.amount, 0.005f, 0.0001f, 1000.0f);
  else if (L.op == "draft") fl("Angle", L.amount, 0.25f, -80.0f, 80.0f);
  else if (L.op == "grid") {
    ifield("Columns", L.segments, 1, 64);
    ifield("Rows", L.count, 1, 64);
  }
  else if (L.op == "pipe") {
    fl("Radius", L.amount, 0.005f, 0.0001f, 10000.0f);
    ifield("Sides", L.segments, 3, 128);
  }
  else if (L.op == "array") {
    ifield("Count", L.count, 1, 256);
    fl("Spacing", L.amount, 0.01f, -10000.0f, 10000.0f);
    static const char *kAx[] = {"X", "Y (normal)", "Z"};
    changed |= u.combo(u.id("array_axis"), row("Axis"), L.axis, kAx, 3);
    u.tooltip("Along the selection's own axes (the gizmo's Local axes in Edit Mode): Y is its normal.");
  }
  else if (L.op == "taper") {
    fl("Distance", L.amount, 0.01f, -10000.0f, 10000.0f);
    fl("End Scale", L.amount2, 0.01f, 0.0f, 100.0f);
  }
  else if (L.op == "sketch_box") {
    fl("Length X", L.size.x, 0.01f, -10000.0f, 10000.0f);
    fl("Length Y", L.size.y, 0.01f, -10000.0f, 10000.0f);
    fl("Length Z", L.size.z, 0.01f, -10000.0f, 10000.0f);
  }
  else if (L.op == "sketch_cylinder") {
    fl("Base Radius", L.size.x, 0.005f, 0.0001f, 10000.0f);
    fl("Top Radius", L.size.z, 0.005f, 0.0f, 10000.0f);
    fl("Height", L.size.y, 0.01f, 0.001f, 10000.0f);
    ifield("Sides", L.segments, 3, 128);
  }
  else if (L.op == "recess") {
    fl("Depth", L.amount, 0.005f, -10000.0f, 10000.0f);
    u.tooltip("Positive: pushed in (a recess). Negative: pulled out (a raised plate).");
    fl("Border", L.amount2, 0.005f, 0.0f, 10000.0f);
  }
  else if (L.op == "push_pull") {
    fl("Distance", L.amount, 0.01f, -10000.0f, 10000.0f);
    u.tooltip("Type an exact distance (local units). Negative pushes in; past the far side\n"
              "it makes a hole, onto a face in front it joins it.");
    Recti cr = row("Merge Coplanar");
    changed |= u.checkbox({cr.x, cr.y, cr.h, cr.h}, L.fuse);
    u.tooltip("On: sides that continue a neighbouring face stretch it (SketchUp).\n"
              "Off: always add walls (SketchUp's Ctrl).");
    Recti ir = row("Each Face");
    if (u.checkbox({ir.x, ir.y, ir.h, ir.h}, L.individual)) {
      changed = true;
      pp_individual_ = L.individual;
    }
    u.tooltip("On: every selected face along its own normal. Off: each connected group along its normal.");
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
  if (changed) {
    run_last_op(false);
    remember_last_op_settings();  // the next use of the tool starts from these
  }
}

/* The helper's values become the tool's settings for next time (the Inspector no longer holds them). */
void Editor::remember_last_op_settings() {
  const LastOp &L = last_op_;
  const std::string &op = L.op;
  if (op == "extrude" || op == "extrude_individual") extrude_dist_ = L.amount;
  else if (op == "inset") (L.individual ? inset_amount_ : inset_thickness_) = L.amount, inset_individual_ = L.individual;
  else if (op == "bevel") bevel_width_ = L.amount, bevel_segments_ = L.segments, bevel_profile_ = L.profile, bevel_clamp_ = L.clamp;
  else if (op == "bridge") bridge_segments_ = L.segments;
  else if (op == "subdivide_edges") subdivide_cuts_ = L.segments;
  else if (op == "loopcut") loop_cuts_ = L.segments, loop_slide_ = L.amount;
  else if (op == "smooth") smooth_factor_ = L.amount;
  else if (op == "merge_distance") merge_dist_ = L.amount;
  else if (op == "push_pull") pp_individual_ = L.individual;
  else if (op == "grid") grid_cols_ = L.segments, grid_rows_ = L.count;
  else if (op == "pipe") pipe_radius_ = L.amount, pipe_sides_ = L.segments;
  else if (op == "array") array_count_ = L.count, array_spacing_ = L.amount, array_axis_ = L.axis;
  else if (op == "sketch_box" || op == "sketch_cylinder") last_op_size_ = L.size;
  else if (op == "taper") taper_distance_ = L.amount, taper_scale_ = L.amount2;
  else if (op == "recess") recess_depth_ = L.amount, recess_border_ = L.amount2;
  else if (op == "shell" || op == "thicken") shell_thickness_ = L.amount;
  else if (op == "draft") draft_angle_ = L.amount;
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
  const float start = op == "inset"    ? 0.0f
                      : op == "array"  ? array_spacing_
                      : op == "pipe"   ? pipe_radius_
                      : op == "taper"  ? taper_distance_
                      : op == "recess" ? recess_depth_
                      : op == "grid"   ? 0.0f
                                       : bevel_width_;
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
    if (modal_.op == "inset") (last_op_.individual ? inset_amount_ : inset_thickness_) = last_op_.amount;
    else if (modal_.op == "bevel") {
      bevel_width_ = last_op_.amount;
      bevel_segments_ = last_op_.segments;
      bevel_profile_ = last_op_.profile;
    }
    else if (modal_.op == "grid") grid_cols_ = last_op_.segments, grid_rows_ = last_op_.count;
    else if (modal_.op == "pipe") pipe_radius_ = last_op_.amount, pipe_sides_ = last_op_.segments;
    else if (modal_.op == "array") array_count_ = last_op_.count, array_spacing_ = last_op_.amount, array_axis_ = last_op_.axis;
    else if (modal_.op == "taper") taper_distance_ = last_op_.amount, taper_scale_ = last_op_.amount2;
    else if (modal_.op == "recess") recess_depth_ = last_op_.amount, recess_border_ = last_op_.amount2;
    Log::info("%s: %.4g (F9 adjusts it)", modal_.op.c_str(), last_op_.amount);
    return;
  }
  /* Cancelled: the mesh as it was, and no undo step left behind. */
  LastOp &L = last_op_;
  if (GameObject *g = scene_->find(L.obj))
    if (MeshFilter *mf = g->get<MeshFilter>()) mf->mesh = mesh_copy(*L.before);  // (a copy: see run_last_op's failure)
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
  Log::info("%s cancelled", modal_.op.c_str());
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
    if (modal_.op == "inset" && last_op_.individual) amount = from + (base - d) / std::max(1.0f, modal_.start_len) * speed;
    else if (modal_.op == "inset") amount = std::max(0.0f, from + (base - d) * modal_.px_size * speed);
    else amount = from + (d - base) * modal_.px_size * speed;
    if (in.ctrl()) amount = std::round(amount / (modal_.op == "inset" ? 0.05f : snap_move_)) * (modal_.op == "inset" ? 0.05f : snap_move_);
  }
  if (modal_.op == "grid") amount = 0.0f;  // the wheel does it all
  else if (modal_.op == "inset") amount = std::max(0.0f, std::min(0.99f, amount));
  else if (modal_.op == "pipe") amount = std::max(1e-4f, amount);
  else if (modal_.op != "array" && modal_.op != "taper" && modal_.op != "recess") amount = std::max(0.0f, amount);
  if (!std::isfinite(amount)) amount = 0.0f;
  bool changed = amount != last_op_.amount;
  if (modal_.op == "bevel" && in.wheel_y != 0 && in.alt()) {
    /* Alt+wheel: the profile, in twentieths (concave below 0.5, convex above). */
    last_op_.profile = clampf(std::round((last_op_.profile + (in.wheel_y > 0 ? 0.05f : -0.05f)) * 20.0f) / 20.0f, 0.0f, 1.0f);
    in.wheel_y = 0;
    changed = true;
  }
  if (in.wheel_y != 0 && modal_.op != "bevel" && modal_.op != "inset") {
    const int dir = in.wheel_y > 0 ? 1 : -1;
    LastOp &L = last_op_;
    if (modal_.op == "array") L.count = std::max(1, std::min(256, L.count + dir));
    else if (modal_.op == "grid" && in.shift()) L.count = std::max(1, std::min(64, L.count + dir));
    else if (modal_.op == "grid") L.segments = std::max(1, std::min(64, L.segments + dir));
    else if (modal_.op == "pipe") L.segments = std::max(3, std::min(128, L.segments + dir));
    else if (modal_.op == "taper") L.amount2 = std::max(0.0f, std::round((L.amount2 + dir * 0.05f) * 20.0f) / 20.0f);
    else if (modal_.op == "recess") L.amount2 = std::max(0.0f, L.amount2 + dir * std::max(1e-3f, L.amount2 * 0.1f));
    in.wheel_y = 0;
    changed = true;
  }
  if (modal_.op == "array" && !u.wants_keyboard()) {
    const int keys[3] = {platform::KEY_X, platform::KEY_Y, platform::KEY_Z};
    for (int a = 0; a < 3; a++)
      if (in.key_pressed[keys[a]]) {
        in.key_pressed[keys[a]] = false;
        last_op_.axis = a;
        changed = true;
      }
  }
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
  const LastOp &LO = last_op_;
  static const char *kAxis[] = {"X", "Y", "Z"};
  std::string text = modal_.op == "inset"    ? strprintf("Inset  %.3f", LO.amount)
                     : modal_.op == "array"  ? strprintf("Array  count %d   spacing %.3f   axis %s", LO.count, LO.amount, kAxis[std::max(0, std::min(2, LO.axis))])
                     : modal_.op == "grid"   ? strprintf("Grid  %d columns x %d rows", LO.segments, LO.count)
                     : modal_.op == "pipe"   ? strprintf("Pipe  radius %.3f   sides %d", LO.amount, LO.segments)
                     : modal_.op == "taper"  ? strprintf("Taper  distance %.3f   end scale %.2f", LO.amount, LO.amount2)
                     : modal_.op == "recess" ? strprintf("Recess  depth %.3f   border %.3f%s", LO.amount, LO.amount2, LO.amount < 0 ? "  (plate)" : "")
                                             : strprintf("Bevel  width %.3f   segments %d   profile %.2f", LO.amount, LO.segments, LO.profile);
  if (!modal_.typed.empty()) text += "   [" + modal_.typed + "]";
  int tw = u.font.text_width(text) + u.px(12);
  Recti box{u.in.mx + u.px(16), u.in.my + u.px(12), tw, u.row_h()};
  u.canvas.fill_round_rect(box, u.px(3), Color::hex(0x202020, 230));
  u.label(box, text, u.theme.text_bright, ui::Align::Center);
  std::string hint =
      modal_.op == "inset"    ? "Inset:  move toward the centre to thicken, or type  |  Shift fine  |  Ctrl snap  |  Click / Enter confirm  |  Esc cancel"
      : modal_.op == "array"  ? "Array:  move for the spacing, or type  |  Wheel count  |  X / Y / Z axis  |  Shift fine  |  Click / Enter  |  Esc"
      : modal_.op == "grid"   ? "Grid:  Wheel columns  |  Shift+Wheel rows  |  Click / Enter confirm  |  Esc cancel"
      : modal_.op == "pipe"   ? "Pipe:  move for the radius, or type  |  Wheel sides  |  Shift fine  |  Click / Enter  |  Esc"
      : modal_.op == "taper"  ? "Taper:  move for the distance, or type  |  Wheel end scale  |  Shift fine  |  Click / Enter  |  Esc"
      : modal_.op == "recess" ? "Recess:  move for the depth, or type (negative: a plate)  |  Wheel border  |  Shift fine  |  Click / Enter  |  Esc"
                              : "Bevel:  move away to widen, or type  |  Wheel segments  |  Alt+Wheel profile  |  Shift fine  |  Ctrl snap  |  Click / Enter  |  Esc";
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
    Log::warn("Push/Pull: select faces first");
    return;
  }
  {
    /* The same checks push_pull makes (each group: one connected region with one outline). */
    Mesh probe = m;
    std::vector<uint8_t> fs = face_sel_;
    std::string err;
    if (!meshops::push_pull_multi(probe, fs, 0.0f, true, pp_individual_, nullptr, &err)) {
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
  if (!meshops::push_pull_multi(m, fs, distance, !pp_.keep, pp_individual_, &pp_.result, &err)) {
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
  note_shallow_before(L);
  L.fsel = pp_.fsel;
  L.vsel.assign(pp_.before->vert_count(), 0);
  L.elem = EditElement::Face;
  L.amount = pp_.distance;
  L.fuse = !pp_.keep;
  L.individual = pp_individual_;
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

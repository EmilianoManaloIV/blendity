// SPDX-License-Identifier: GPL-2.0-or-later
// The Room tool (task 0455, round 479): drag a rectangle - or, with the Circle option, a circle from its centre - on the ground or on a
// face of any mesh, then move the mouse for the height and click: the result is a room (or, with Solid, an Add or Subtract box or
// cylinder) in the active Blockout, turned to the surface and standing on it. The tool stays on for the next room; Esc leaves it. The
// piece is made by room_make (blockout_editor.cpp), which `blockout room` calls with the same numbers.
#include "editor.h"

#include "../core/core.h"
#include "../scene/raycast.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace bl {

namespace {

/* The axis a footprint's first side lies along: a horizontal one on a wall, the world's X laid into a floor or a ceiling. */
Vec3 surface_axis(Vec3 n) {
  if (std::fabs(n.y) > 0.9f) return normalize(Vec3(1.0f, 0.0f, 0.0f) - n * n.x);
  return normalize(cross(Vec3(0.0f, 1.0f, 0.0f), n));
}

bool is_number_word(const std::string &w) {
  return !w.empty() && (std::isdigit((unsigned char)w[0]) || w[0] == '-' || w[0] == '.' || w[0] == '+');
}

}  // namespace

void Editor::set_room_tool(bool on) {
  if (on == room_.active) return;
  if (!on) {
    room_.active = false, room_.stage = 0, room_.typed.clear();
    ui_.redraw = true;
    Log::info("Room tool: off");
    return;
  }
  if (playing_ || !scene_) return Log::warn("Room tool: not while playing - stop Play first");
  if (edit_mode_) exit_edit_mode();
  draw_.active = false, knife_.active = false;
  if (paint_) set_paint(false);
  if (sculpt_) set_sculpt(false);
  if (strokeobj_) set_strokeobj(false);
  if (scatter_) set_scatter(false);
  room_.active = true, room_.stage = 0, room_.typed.clear();
  ui_.redraw = true;
  Log::info("Room tool: drag a rectangle on the ground or a face%s, then move the mouse for the height and click  |  Esc leaves", room_.circle ? " (a circle from its centre)" : "");
}

/* The surface under a view pixel: the nearest face of any rendered mesh, else the ground plane. */
bool Editor::room_surface_hit(const Recti &view, int mx, int my, Vec3 &p, Vec3 &n) {
  const Ray ray = scene_r3d_.screen_ray((float)(mx - view.x), (float)(my - view.y));
  SceneHit hit;
  const bool have = scene_raycast(*scene_, ray.origin, normalize(ray.dir), 1e5f, hit);
  float tg = 0.0f;
  const bool ground = ray_plane(ray, Vec3(0.0f), Vec3(0.0f, 1.0f, 0.0f), tg);
  if (have && (!ground || hit.distance <= tg)) {
    p = hit.point, n = normalize(hit.normal);
    return finite_bits(p.x) && finite_bits(p.y) && finite_bits(p.z) && length(n) > 0.5f;
  }
  if (!ground) return false;
  p = ray.origin + ray.dir * tg, n = Vec3(0.0f, 1.0f, 0.0f);
  return finite_bits(p.x) && finite_bits(p.y) && finite_bits(p.z);
}

/* The grid, on a surface that lies square to a world axis (the ground, a wall of a room): the two coordinates in the surface. Ctrl: none. */
Vec3 Editor::room_snap_point(Vec3 p, Vec3 n) const {
  if (ui_.in.ctrl()) return p;
  const float step = std::max(1e-3f, snap_move_);
  for (int a = 0; a < 3; a++) {
    if (std::fabs(n[a]) < 0.9999f) continue;
    for (int b = 0; b < 3; b++)
      if (b != a) p[b] = std::round(p[b] / step) * step;
    return p;
  }
  return p;
}

void Editor::room_extent(float &du, float &dz) const {
  const Vec3 d = room_.corner - room_.origin;
  if (room_.circle) {
    du = length(d), dz = 0.0f;
    return;
  }
  du = dot(d, room_.u), dz = dot(d, cross(room_.u, room_.normal));
}

std::vector<Vec3> Editor::room_outline(float height) const {
  float du, dz;
  room_extent(du, dz);
  const Vec3 n = room_.normal, u = room_.u, z = cross(u, n), lift = n * height;
  std::vector<Vec3> out;
  if (room_.circle) {
    for (int k = 0; k < 32; k++) {
      const float a = 2.0f * kPi * (float)k / 32.0f;
      out.push_back(room_.origin + lift + u * (du * std::cos(a)) + z * (du * std::sin(a)));
    }
    return out;
  }
  const Vec3 o = room_.origin + lift;
  out = {o, o + u * du, o + u * du + z * dz, o + z * dz};
  return out;
}

/* The tool's input: a press starts the footprint, the release fixes it (a drag of no area makes nothing), the height follows the mouse
 * (or is typed) and a click makes the room. Esc leaves; the right button drops the room in progress. True while it holds the mouse. */
bool Editor::room_update(const Recti &view) {
  if (!room_.active) return false;
  auto &u = ui_;
  auto &in = u.in;
  RoomTool &r = room_;
  if (playing_ || edit_mode_ || !scene_) {
    r.active = false, r.stage = 0;
    return false;
  }
  if (in.key_pressed[platform::KEY_ESCAPE] && !u.wants_keyboard()) {
    in.key_pressed[platform::KEY_ESCAPE] = false;
    set_room_tool(false);
    return true;
  }
  if (in.pressed[1] && r.stage > 0) {
    in.pressed[1] = false;
    r.stage = 0, r.typed.clear();
    return true;
  }
  const Ray ray = scene_r3d_.screen_ray((float)(in.mx - view.x), (float)(in.my - view.y));
  if (r.stage == 0) {
    if (!(scene_hovered_ && in.pressed[0] && !in.alt() && !u.any_active() && !qm_.open)) return false;
    Vec3 p, n;
    u.consume_click();
    if (!room_surface_hit(view, in.mx, in.my, p, n)) {
      Log::warn("Room: click on a surface or the ground");
      return true;
    }
    r.normal = n;
    r.u = surface_axis(n);
    r.origin = r.corner = room_snap_point(p, n);
    r.stage = 1, r.height = 0.0f, r.typed.clear();
    return true;
  }
  if (r.stage == 1) {
    float t;
    if (ray_plane(ray, r.origin, r.normal, t)) r.corner = room_snap_point(ray.origin + ray.dir * t, r.normal);
    u.redraw = true;
    if (in.down[0]) return true;
    float du, dz;
    room_extent(du, dz);
    const bool empty = r.circle ? std::fabs(du) < 1e-3f : std::fabs(du) < 1e-3f || std::fabs(dz) < 1e-3f;
    r.stage = empty ? 0 : 2;  // (a drag of no area makes nothing: back to waiting)
    r.height = 0.0f, r.typed.clear();
    return true;
  }
  /* The height: typed (digits and an expression, as Push/Pull's distance), else where the mouse is along the normal. */
  for (char c : in.text)
    if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == '*' || c == '/' || c == '(' || c == ')') r.typed += c;
  in.text.clear();
  if (in.key_pressed[platform::KEY_BACKSPACE] && !r.typed.empty()) r.typed.pop_back();
  float du, dz;
  room_extent(du, dz);
  const Vec3 centre = r.circle ? r.origin : r.origin + r.u * (du * 0.5f) + cross(r.u, r.normal) * (dz * 0.5f);
  double typed = 0.0;
  float h;
  if (!r.typed.empty() && ui::eval_number(r.typed, typed)) h = (float)typed;
  else {
    h = closest_on_line_to_ray(centre, r.normal, ray);
    if (!in.ctrl()) h = std::round(h / std::max(1e-3f, snap_move_)) * std::max(1e-3f, snap_move_);
  }
  r.height = finite_bits(h) ? h : 0.0f;
  u.redraw = true;
  if (!((in.pressed[0] && scene_hovered_ && !u.any_active()) || in.key_pressed[platform::KEY_ENTER])) return true;
  u.consume_click();
  in.key_pressed[platform::KEY_ENTER] = false;
  const Vec3 origin = r.origin, up = r.u, n = r.normal;
  const float height = r.height;
  const bool circle = r.circle;
  r.stage = 0, r.typed.clear();  // (the tool stays on for the next room)
  room_make(origin, up, n, du, dz, height, circle);
  return true;
}

/* The footprint and the room's box over the picture, the numbers beside the mouse; before the first press, a ground marker under it. */
void Editor::room_draw(const Recti &view) {
  if (!room_.active) return;
  auto &u = ui_;
  const RoomTool &r = room_;
  u.canvas.push_clip(view);
  auto line = [&](Vec3 a, Vec3 b, uint32_t col) {
    Vec2 sa, sb;
    if (!project_to_view(view, a, sa) || !project_to_view(view, b, sb)) return;
    u.canvas.line(sa.x, sa.y, sb.x, sb.y, 0xFF000000, (float)u.px(2.5f));
    u.canvas.line(sa.x, sa.y, sb.x, sb.y, col, (float)u.px(1.2f));
  };
  const uint32_t yellow = Color::hex(0xFFD040), blue = Color::hex(0x4A8CFF);
  std::string label;
  if (r.stage == 0 && scene_hovered_) {
    const Ray ray = scene_r3d_.screen_ray((float)(u.in.mx - view.x), (float)(u.in.my - view.y));
    float t;
    Vec2 s;
    if (ray_plane(ray, Vec3(0.0f), Vec3(0.0f, 1.0f, 0.0f), t) && project_to_view(view, room_snap_point(ray.origin + ray.dir * t, Vec3(0.0f, 1.0f, 0.0f)), s)) {
      u.canvas.fill_circle(s.x, s.y, (float)u.px(5.5f), 0xFF000000);
      u.canvas.fill_circle(s.x, s.y, (float)u.px(4), yellow);
    }
  }
  if (r.stage > 0) {
    const std::vector<Vec3> base = room_outline(0.0f);
    for (size_t i = 0; i < base.size(); i++) line(base[i], base[(i + 1) % base.size()], yellow);
    float du, dz;
    room_extent(du, dz);
    label = r.circle ? strprintf("radius %.2f m", std::fabs(du)) : strprintf("%.2f x %.2f m", std::fabs(du), std::fabs(dz));
    if (r.stage == 2) {
      const float shown = r.solid ? r.height : std::fabs(r.height);  // (a room is built upward whatever the mouse does)
      const std::vector<Vec3> top = room_outline(shown);
      for (size_t i = 0; i < top.size(); i++) line(top[i], top[(i + 1) % top.size()], blue);
      const size_t step = r.circle ? 4 : 1;
      for (size_t i = 0; i < top.size(); i += step) line(base[i], top[i], blue);
      label = r.typed.empty() ? strprintf("height %.3f m", r.height) : strprintf("height [%s] = %.3f m", r.typed.c_str(), r.height);
    }
    const int tw = u.font.text_width(label) + u.px(12);
    const Recti box{u.in.mx + u.px(16), u.in.my + u.px(12), tw, u.row_h()};
    u.canvas.fill_round_rect(box, u.px(3), Color::hex(0x202020, 230));
    u.label(box, label, u.theme.text_bright, ui::Align::Center);
  }
  u.canvas.pop_clip();
}

/* blockout room <x0 z0 x1 z1 height> | room circle <x z radius height>: what the tool's two drags make, on the ground plane, with the tool's options. */
void Editor::room_command(const std::vector<std::string> &t) {
  const bool circle = t.size() > 2 && to_lower(t[2]) == "circle";
  const size_t first = circle ? 3 : 2, want = circle ? 4 : 5;
  const std::string usage = "blockout room <x0 z0 x1 z1 height> | blockout room circle <x z radius height> (metres on the ground; options: blockout roomopt)";
  if (t.size() != first + want) return Log::warn("%s", usage.c_str());
  float v[5] = {};
  for (size_t i = 0; i < want; i++) {
    if (!is_number_word(t[first + i])) return Log::warn("%s", usage.c_str());
    v[i] = (float)std::atof(t[first + i].c_str());
    if (!finite_bits(v[i])) return Log::warn("%s", usage.c_str());
  }
  const Vec3 up(0.0f, 1.0f, 0.0f), x(1.0f, 0.0f, 0.0f);
  if (circle) {
    if (v[2] <= 0.0f) return Log::warn("Room: a radius is above 0 metres - nothing made");
    room_make(Vec3(v[0], 0.0f, v[1]), x, up, v[2], 0.0f, v[3], true);
  }
  else room_make(Vec3(v[0], 0.0f, v[1]), x, up, v[2] - v[0], v[3] - v[1], v[4], false);
}

/* blockout roomopt circle|solid|subtract on|off | wall <metres>: the tool's options (as its bar sets them). */
void Editor::room_options_command(const std::vector<std::string> &t) {
  const std::string key = t.size() > 2 ? to_lower(t[2]) : "", value = t.size() > 3 ? to_lower(t[3]) : "";
  const bool on = value == "on" || value == "true" || value == "1" || value == "yes", off = value == "off" || value == "false" || value == "0" || value == "no";
  if (key == "wall") {
    const float m = (float)std::atof(value.c_str());
    if (!is_number_word(value) || !finite_bits(m) || m < 0.0f || m > 1000.0f) return Log::warn("blockout roomopt wall <metres>: 0 to 1000");
    room_.wall = m;
    return;
  }
  if ((key != "circle" && key != "solid" && key != "subtract") || (!on && !off)) return Log::warn("blockout roomopt circle|solid|subtract on|off | wall <metres>");
  (key == "circle" ? room_.circle : key == "solid" ? room_.solid : room_.subtract) = on;
  ui_.redraw = true;
}

/* The tool's switch and its options, in the Modeling Tools window (Create > Blockout). */
void Editor::draw_room_tools(ui::Layout &lay) {
  auto &u = ui_;
  Recti row = lay.row(u.row_h() + u.px(2));
  if (u.button({row.x + u.px(4), row.y, row.w - u.px(8), row.h}, room_.active ? "Exit Room Tool" : "Room Tool", room_.active)) set_room_tool(!room_.active);
  u.tooltip("Drag a rectangle on the ground or a face, move the mouse for the height, click: a room of the active Blockout stands there.\n"
            "Ctrl: no snapping. Esc leaves. Console: tool room, blockout room.");
  struct Opt { const char *label, *tip; bool *value; };
  for (const Opt &o : {Opt{"Circle Footprint", "Drag a circle from its centre: a cylinder room.", &room_.circle},
                       Opt{"Solid Piece", "A plain box or cylinder instead of a room.", &room_.solid},
                       Opt{"Subtract", "A solid cuts out of the Blockout instead of adding to it.", &room_.subtract}}) {
    row = lay.row(u.row_h() + u.px(2));
    if (u.button({row.x + u.px(4), row.y, row.w - u.px(8), row.h}, o.label, *o.value)) *o.value = !*o.value;
    u.tooltip(o.tip);
  }
  row = lay.row();
  u.label({row.x + u.px(4), row.y, row.w / 2, row.h}, "Room Wall Thickness");
  u.float_field(u.id("tools_room_wall"), {row.x + row.w / 2, row.y, row.w / 2 - u.px(6), row.h}, room_.wall, 0.01f, 0.0f, 1000.0f);
  u.tooltip("A new room's wall, floor and ceiling, in metres.");
}

}  // namespace bl

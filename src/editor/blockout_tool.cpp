// SPDX-License-Identifier: GPL-2.0-or-later
// The Draw Piece tool (the Room tool: task 0455, round 479): drag a rectangle - or, for the round shapes, a circle from its centre - on the
// ground or on a face of any mesh, then move the mouse for the height and click: the result is the piece the Shape dropdown names (a room,
// a box, a cylinder, an outline, a wedge, stairs or an arch) in the active Blockout, turned to the surface and standing on it. For the two
// outline shapes (round 480) the footprint is any outline: click its corners on the surface, close it on the first corner or with Enter, then
// pull the height as before. The tool stays on for the next piece; Esc leaves it. The piece is made by room_make (blockout_editor.cpp), which
// `blockout room` and `blockout draw` call with the same numbers.
// Round 495 (task 0479): the tool draws any piece - a room, a solid, a wedge, stairs, an arch - with any operation, and a piece
// drawn on a face of a Blockout lands in that Blockout; `blockout draw` is the same from the console.
// Round 496 (task 0480): the Modeling Tools window has one Shape dropdown (draw_room_tools) in place of the footprint ticks and the Draw: and
// Operation: rows - the Operation is Auto (pushed in subtracts), Shift while the height is pulled makes the other of Add and Subtract, and the
// console's `roomopt operation` can still fix one. While a footprint is dragged and a height pulled the Scene view shows the piece itself as a
// wireframe (room_draw_preview), made by the function room_make makes the piece with (blockout_drawn_piece); R turns a wedge, stairs or arch
// a quarter round the surface's normal, and by default they rise (an arch spans) along the rectangle's longer side.
#include "editor.h"

#include "blockout_pick.h"

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

/* The tool's Piece choice by its word: a room, a solid of the footprint's shape, or a wedge, stairs or an arch (whose footprint is
 * the rectangle). False: no such word. */
template <class Tool>
bool set_draw_piece(const std::string &word, Tool &r) {
  const int piece = word == "wedge" ? 3 : word == "stairs" ? 4 : word == "arch" ? 5 : 0;
  if (word != "room" && word != "solid" && !piece) return false;
  r.solid = word != "room";
  if (!r.solid) r.forget();  // (a room has no such choices)
  if (piece) r.circle = r.polyline = false;
  r.piece = piece;
  return true;
}

/* The tool's Operation choice by its word; a room has none, and nothing is set for one. False: no such word. */
template <class Tool>
bool set_draw_operation(const std::string &word, Tool &r) {
  if (word != "auto" && word != "add" && word != "subtract" && word != "intersect") return false;
  if (!r.solid) return true;
  r.subtract = word == "subtract";
  r.operation = word == "add" ? 0 : word == "intersect" ? 2 : -1;
  return true;
}

}  // namespace

int blockout_draw_shape(bool solid, bool circle, bool polyline, int piece) {
  const int footprint = circle ? 1 : polyline ? 2 : 0;
  if (!solid) return footprint;
  return piece >= 3 ? piece + 3 : 3 + footprint;  // (3 wedge, 4 stairs, 5 arch -> entries 6, 7, 8)
}

int Editor::room_tool_shape_for_test() const { return blockout_draw_shape(room_.solid, room_.circle, room_.polyline, room_.piece); }

int blockout_draw_operation(bool solid, int chosen, bool subtract, bool flip, float height, bool nothing_to_cut) {
  if (!solid) return 0;
  if (chosen == 2) return 2;
  const int op = chosen == 0 ? 0 : subtract ? 1 : height < 0.0f && !nothing_to_cut ? 1 : 0;  // (Auto: by the direction)
  return flip ? 1 - op : op;
}

bool blockout_nothing_to_cut(const GameObject &blockout) {
  std::vector<const GameObject *> pieces;
  blockout_piece_objects(blockout, pieces);  // (the pieces that are on)
  return std::none_of(pieces.begin(), pieces.end(), [](const GameObject *p) {
    const BlockoutPiece *bp = p->get<BlockoutPiece>();
    return bp->operation == 0 || bp->is_room();
  });
}

uint64_t blockout_at_surface(const Scene &scene, Vec3 point, Vec3 normal) {
  const bool finite = finite_bits(point.x) && finite_bits(point.y) && finite_bits(point.z) && finite_bits(normal.x) && finite_bits(normal.y) && finite_bits(normal.z);
  if (!finite || !(length(normal) > 1e-6f)) return 0;
  const Vec3 n = normalize(normal);
  SceneHit hit;
  /* A short ray back onto the face from just before it. */
  if (!scene_raycast(scene, point + n * 0.02f, n * -1.0f, 0.05f, hit) || !hit.object) return 0;
  return hit.object->get<Blockout>() ? hit.object->id : 0;
}

void Editor::set_room_tool(bool on) {
  if (on == room_.active) return;
  if (!on) {
    room_.active = false, room_.stage = 0, room_.typed.clear(), room_.points.clear();
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
  if (!room_.solid) room_.forget();  // (the Draw and Operation choices hold with Solid only)
  ui_.redraw = true;
  if (room_.polyline) Log::info("Room tool: click the corners of an outline on the ground or a face, close it on the first corner or with Enter, then move the mouse for the height and click  |  Esc leaves");
  else Log::info("Room tool: drag a rectangle on the ground or a face%s, then move the mouse for the height and click  |  Esc leaves", room_.circle ? " (a circle from its centre)" : "");
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
    /* (Each coordinate written out, not through operator[] on the local: see along_axis in blockout_object.cpp.) */
    auto snapped = [&](int b, float v) { return b == a ? v : std::round(v / step) * step; };
    return Vec3(snapped(0, p.x), snapped(1, p.y), snapped(2, p.z));
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
  if (room_.stage >= 2 && !room_.points.empty()) {  // (a polyline's: its corners, lifted)
    for (const Vec3 &p : room_.points) out.push_back(p + lift);
    return out;
  }
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

/* Whether the mouse is on the polyline's first corner (a click there closes it): within a few pixels on the screen, three corners or more. */
bool Editor::room_polyline_closes(const Recti &view) {
  if (room_.points.size() < 3) return false;
  Vec2 s;
  if (!project_to_view(view, room_.points[0], s)) return false;
  return std::hypot(s.x - (float)ui_.in.mx, s.y - (float)ui_.in.my) <= (float)ui_.px(9);
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
    r.stage = 0, r.typed.clear(), r.points.clear();
    return true;
  }
  /* Round 496: R turns the piece's direction a quarter round the surface's normal while one is in progress (elsewhere R keeps its keymap meaning;
   * Shift+R is the tool's own switch). Only a wedge, stairs and an arch face a way; the choice is kept for the next piece of that shape. */
  const bool plain_r = !in.ctrl() && !in.shift() && !in.alt();
  if ((r.stage == 1 || r.stage == 2) && plain_r && in.key_pressed[platform::KEY_R] && !u.wants_keyboard()) {
    in.key_pressed[platform::KEY_R] = false;
    if (r.solid && !r.circle && !r.polyline && r.piece >= 3) r.turns[r.piece - 3] = (r.turns[r.piece - 3] + 1) & 3;
    u.redraw = true;
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
    /* Round 495: a piece drawn on a face of a Blockout is that Blockout's - the face under the press itself (the ground may lie
     * before it, and then the surface is the ground). */
    SceneHit under;
    const bool face = scene_raycast(*scene_, ray.origin, normalize(ray.dir), 1e5f, under) && under.object && under.object->get<Blockout>() &&
                      length(under.point - p) <= 1e-3f * (1.0f + length(p));
    room_.surface = face ? under.object->id : 0;
    r.height = 0.0f, r.typed.clear(), r.points.clear();
    if (r.polyline) r.points.push_back(r.origin), r.stage = 3;  // (the first corner: the others are clicked)
    else r.stage = 1;
    return true;
  }
  if (r.stage == 3) {
    /* The corners: each click adds one on the surface's plane, a click on the first (or Enter) closes the outline, Backspace takes one back. */
    float t;
    if (ray_plane(ray, r.origin, r.normal, t)) r.corner = room_snap_point(ray.origin + ray.dir * t, r.normal);
    u.redraw = true;
    const bool keys = !u.wants_keyboard();  // (a text field has them: Enter and Backspace typed there are not the outline's)
    if (keys && in.key_pressed[platform::KEY_BACKSPACE]) {
      in.key_pressed[platform::KEY_BACKSPACE] = false;
      r.points.pop_back();
      if (r.points.empty()) r.stage = 0;
      return true;
    }
    const bool enter = keys && in.key_pressed[platform::KEY_ENTER];
    const bool click = in.pressed[0] && scene_hovered_ && !in.alt() && !u.any_active();
    if (!enter && !click) return true;
    if (click) u.consume_click();
    if (enter) in.key_pressed[platform::KEY_ENTER] = false;
    if (enter || room_polyline_closes(view)) {
      if (r.points.size() < 3) {
        Log::warn("Room: an outline needs three corners at least - click more");
        return true;
      }
      r.stage = 2, r.height = 0.0f, r.typed.clear();  // (closed: the height next)
      return true;
    }
    /* (A click on a corner that is there already adds nothing - the first one too, while three are not there yet to close on it.) */
    const bool there = std::any_of(r.points.begin(), r.points.end(), [&](Vec3 p) { return length(r.corner - p) <= 1e-3f; });
    if (!there) r.points.push_back(r.corner);
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
  Vec3 centre = r.circle ? r.origin : r.origin + r.u * (du * 0.5f) + cross(r.u, r.normal) * (dz * 0.5f);
  if (!r.points.empty()) {  // (a polyline: the height is read along the line through its middle)
    centre = Vec3(0.0f);
    for (const Vec3 &p : r.points) centre = centre + p * (1.0f / (float)r.points.size());
  }
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
  std::vector<Vec3> corners;
  corners.swap(r.points);
  r.stage = 0, r.typed.clear();  // (the tool stays on for the next room)
  r.flip = in.shift();  // (Shift held at the click: the other of Add and Subtract - a room has no operation and ignores it)
  if (!corners.empty()) room_make_outline(corners, up, n, height);
  else room_make(origin, up, n, du, dz, height, circle);
  r.flip = false;
  return true;
}

/* The footprint and the room's box over the picture, the numbers beside the mouse; before the first press, a ground marker under it. */
void Editor::room_draw(const Recti &view) {
  if (!room_.active) return;
  room_.preview = DrawnPiece(), room_.preview_lines = 0;  // (set again below while a piece is in progress)
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
  if (r.stage == 3) {
    /* The corners so far, joined, and the next one at the mouse; the first corner is marked so the closing click is seen. */
    for (size_t i = 0; i + 1 < r.points.size(); i++) line(r.points[i], r.points[i + 1], yellow);
    line(r.points.back(), r.corner, yellow);
    Vec2 s;
    if (project_to_view(view, r.points[0], s)) {
      u.canvas.fill_circle(s.x, s.y, (float)u.px(6.5f), 0xFF000000);
      u.canvas.fill_circle(s.x, s.y, (float)u.px(5), room_polyline_closes(view) ? Color::hex(0xFFFFFF) : yellow);
    }
    label = strprintf("%zu corners  (click the first or Enter to close)", r.points.size());
    const int tw = u.font.text_width(label) + u.px(12);
    const Recti box{u.in.mx + u.px(16), u.in.my + u.px(12), tw, u.row_h()};
    u.canvas.fill_round_rect(box, u.px(3), Color::hex(0x202020, 230));
    u.label(box, label, u.theme.text_bright, ui::Align::Center);
  }
  else if (r.stage > 0) {
    const std::vector<Vec3> base = room_outline(0.0f);
    for (size_t i = 0; i < base.size(); i++) line(base[i], base[(i + 1) % base.size()], yellow);
    float du, dz;
    room_extent(du, dz);
    label = r.circle ? strprintf("radius %.2f m", std::fabs(du)) : strprintf("%.2f x %.2f m", std::fabs(du), std::fabs(dz));
    if (!r.points.empty()) label = strprintf("%zu corners", r.points.size());
    int now = 0;
    if (r.stage == 2) {
      label = r.typed.empty() ? strprintf("height %.3f m", r.height) : strprintf("height [%s] = %.3f m", r.typed.c_str(), r.height);
      /* What the piece will do - Auto: pushed in it subtracts, where the Blockout it goes into (room_target's) has something to cut. */
      static const char *const does[] = {"Add", "Subtract", "Intersect"};
      GameObject *on = scene_->find(room_.surface), *sel = active_object();
      const GameObject *into = on && on->get<Blockout>() ? on : sel && sel->get<Blockout>() ? sel : sel ? blockout_of_piece(*sel) : nullptr;
      if (r.solid) {
        const bool nothing = !into || blockout_nothing_to_cut(*into), held = u.in.shift();
        now = blockout_draw_operation(r.solid, r.operation, r.subtract, held, r.height, nothing);
        const int other = blockout_draw_operation(r.solid, r.operation, r.subtract, !held, r.height, nothing);
        label += std::string("  ") + does[now];
        if (other != now) label += std::string("  (Shift: ") + does[other] + ")";
      }
    }
    room_draw_preview(view, now);
    if (r.stage == 2 && !r.points.empty()) {  // (an outline: its footprint lifted, as a plain box of lines)
      const float shown = r.solid ? r.height : std::fabs(r.height);  // (a room is built upward whatever the mouse does)
      const std::vector<Vec3> top = room_outline(shown);
      for (size_t i = 0; i < top.size(); i++) line(top[i], top[(i + 1) % top.size()], blue);
      for (size_t i = 0; i < top.size(); i++) line(base[i], top[i], blue);
    }
    if (r.preview.directional) label += "  R: turn";
    const int tw = u.font.text_width(label) + u.px(12);
    const Recti box{u.in.mx + u.px(16), u.in.my + u.px(12), tw, u.row_h()};
    u.canvas.fill_round_rect(box, u.px(3), Color::hex(0x202020, 230));
    u.label(box, label, u.theme.text_bright, ui::Align::Center);
  }
  room_.shown = label;
  u.canvas.pop_clip();
}

/* The piece the drags describe, drawn as a wireframe by the code that draws a selected piece's outline (round 496): the numbers come from
 * blockout_drawn_piece, the function room_make makes the piece with, so the picture is the piece. While the footprint is dragged the height is
 * provisional (1 m, a room 2.5 m) and the lines faint; while the height is pulled it is the true one, in the operation's colour, with an arrow on the
 * footprint for the way a wedge, stairs or an arch faces. An outline's footprint keeps the plain lifted box (the caller's). */
void Editor::room_draw_preview(const Recti &view, int op) {
  RoomTool &r = room_;
  if (!r.points.empty() || r.polyline) return;
  auto &u = ui_;
  float du, dz;
  room_extent(du, dz);
  const bool fixed = r.stage == 2;
  const float height = fixed ? r.height : r.solid ? 1.0f : 2.5f;
  if (fixed && std::fabs(height) < 1e-3f) return;
  const int kind = r.solid && !r.circle ? r.piece : 0;
  const DrawnPiece d = blockout_drawn_piece(r.solid, r.circle, kind, op, r.origin, r.u, r.normal, du, dz, height, 1.0f, room_turn(kind));
  r.preview = d;
  const uint32_t rgb = blockout_operation_colour(op, d.room) & 0x00FFFFFFu;
  const uint32_t col = rgb | ((fixed ? 255u : 110u) << 24), shell = rgb | ((fixed ? 140u : 60u) << 24);
  auto line = [&](Vec3 a, Vec3 b, uint32_t c) {
    Vec2 sa, sb;
    if (!project_to_view(view, a, sa) || !project_to_view(view, b, sb)) return;
    if (fixed) u.canvas.line(sa.x, sa.y, sb.x, sb.y, 0xFF000000, (float)u.px(2.5f));
    u.canvas.line(sa.x, sa.y, sb.x, sb.y, c, (float)u.px(fixed ? 1.2f : 1.0f));
  };
  const BlockoutPiece bp = blockout_piece_from_drawn(d, r.wall);
  const Mat4 world = Mat4::trs(d.centre, d.rotation, Vec3(1.0f));
  blockout_piece_wire(bp, world, false, [&](Vec3 a, Vec3 b) { r.preview_lines++, line(a, b, col); });
  blockout_piece_wire(bp, world, true, [&](Vec3 a, Vec3 b) { r.preview_lines++, line(a, b, shell); });
  if (!d.directional) return;
  /* The arrow: a wedge or stairs along the middle of the footprint, an arch from the feet's middle across the span. */
  const bool arch = kind == 5;
  const Vec3 from = arch ? d.foot : d.foot - d.rise * d.reach, to = from + d.rise * (arch ? d.reach : 2.0f * d.reach);
  const Vec3 side = cross(r.normal, d.rise) * (d.reach * 0.2f), back = d.rise * (d.reach * 0.3f);
  line(from, to, col);
  line(to, to - back + side, col);
  line(to, to - back - side, col);
}

/* blockout room <x0 z0 x1 z1 height> | room circle <x z radius height>: what the tool's two drags make, on the ground plane, with the tool's options. */
void Editor::room_command(const std::vector<std::string> &t) {
  /* The surface of a footprint the tool has in progress is the tool's: a command run meanwhile puts it back. */
  struct KeepSurface {
    uint64_t &slot;
    uint64_t surface;
    explicit KeepSurface(uint64_t &s) : slot(s), surface(s) {}
    ~KeepSurface() { slot = surface; }
  } keep(room_.surface);
  room_.surface = 0;  // (the ground: the selected Blockout, or a new one - `draw` looks its surface up)
  /* The turns the tool remembers (R) are for the tool's own pieces: a command makes the default direction, whatever the shape, and puts them back. */
  struct KeepTurns {
    int (&slot)[3];
    int turns[3];
    explicit KeepTurns(int (&s)[3]) : slot(s) {
      for (int i = 0; i < 3; i++) turns[i] = s[i], s[i] = 0;
    }
    ~KeepTurns() {
      for (int i = 0; i < 3; i++) slot[i] = turns[i];
    }
  } keep_turns(room_.turns);
  if (t.size() > 1 && to_lower(t[1]) == "draw") {
    /* blockout draw <piece> <operation> at <x y z> normal <x y z> size <du dz height>: what the tool's drags make on that surface
     * point, whatever the tool's own options are (they are put back). A cylinder: du its radius. */
    const char *usage = "blockout draw room|box|cylinder|wedge|stairs|arch add|subtract|intersect|auto at <x y z> normal <x y z> size <du dz height> "
                        "(metres; a cylinder: du its radius; on a face of a Blockout the piece is that Blockout's)";
    if (t.size() != 16 || to_lower(t[4]) != "at" || to_lower(t[8]) != "normal" || to_lower(t[12]) != "size") return Log::warn("%s", usage);
    float v[9] = {};
    const size_t at[9] = {5, 6, 7, 9, 10, 11, 13, 14, 15};
    for (size_t i = 0; i < 9; i++) {
      v[i] = (float)std::atof(t[at[i]].c_str());
      if (!is_number_word(t[at[i]]) || !finite_bits(v[i])) return Log::warn("%s", usage);
    }
    const Vec3 p(v[0], v[1], v[2]), n(v[3], v[4], v[5]);
    if (!(length(n) > 1e-6f)) return Log::warn("blockout draw: the normal has no length - nothing made");
    const std::string piece = to_lower(t[2]), op = to_lower(t[3]);
    const RoomTool tool = room_;
    room_.circle = piece == "cylinder", room_.polyline = false;
    /* (A box and a cylinder are the tool's Solid by the footprint; "solid" itself is not a word here - it would not say which.) */
    const bool known = set_draw_piece(piece == "box" || piece == "cylinder" ? "solid" : piece == "solid" ? "" : piece, room_) && set_draw_operation(op, room_);
    if (known && !room_.solid && op != "auto") Log::warn("blockout draw: a room has no operation - '%s' is not used", op.c_str());
    if (known) {
      const Vec3 unit = normalize(n);
      room_.surface = blockout_at_surface(*scene_, p, unit);
      room_make(p, surface_axis(unit), unit, v[6], v[7], v[8], room_.circle);
    }
    else Log::warn("%s", usage);
    room_ = tool;
    return;
  }
  if (t.size() > 2 && to_lower(t[2]) == "outline") {
    /* blockout room outline <height> <x z> <x z> <x z> ...: the Polyline option's room, its corners on the ground. */
    const std::string outline_usage = "blockout room outline <height> <x z> <x z> <x z> ... (3 to 256 corners, metres on the ground)";
    if (t.size() < 3 + 1 + 6 || (t.size() - 4) % 2 != 0 || (t.size() - 4) / 2 > 256) return Log::warn("%s", outline_usage.c_str());
    std::vector<Vec3> corners;
    float height = 0.0f;
    for (size_t i = 3; i < t.size(); i++) {
      if (!is_number_word(t[i]) || !finite_bits((float)std::atof(t[i].c_str()))) return Log::warn("%s", outline_usage.c_str());
      const float v = (float)std::atof(t[i].c_str());
      if (i == 3) height = v;
      else if (i % 2 == 0) corners.push_back(Vec3(v, 0.0f, 0.0f));
      else corners.back().z = v;
    }
    room_make_outline(corners, Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), height);
    return;
  }
  const bool circle = t.size() > 2 && to_lower(t[2]) == "circle";
  const size_t first = circle ? 3 : 2, want = circle ? 4 : 5;
  const std::string usage = "blockout room <x0 z0 x1 z1 height> | blockout room circle <x z radius height> | blockout room outline <height> <x z> <x z> <x z> ... (metres on the ground; options: blockout roomopt)";
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
  const char *usage = "blockout roomopt circle|polyline|solid|subtract on|off | piece room|solid|wedge|stairs|arch | operation auto|add|subtract|intersect | wall <metres>";
  if (key == "piece" || key == "operation") {
    /* Round 495: what is drawn, and what a solid does to its Blockout (an operation holds while a solid is the piece). */
    const bool known = key == "piece" ? set_draw_piece(value, room_) : set_draw_operation(value, room_);
    if (!known) return Log::warn("%s", usage);
    if (key == "operation" && !room_.solid) return Log::warn("blockout roomopt operation: a room has no operation - choose a solid first (blockout roomopt piece solid|wedge|stairs|arch)");
    if (!room_.polyline && room_.stage == 3) room_.points.clear(), room_.stage = 0;
    ui_.redraw = true;
    return;
  }
  if ((key != "circle" && key != "solid" && key != "subtract" && key != "polyline") || (!on && !off)) return Log::warn("%s", usage);
  (key == "circle" ? room_.circle : key == "polyline" ? room_.polyline : key == "solid" ? room_.solid : room_.subtract) = on;
  if (key == "polyline" && off) room_.points.clear(), room_.stage = room_.stage == 3 ? 0 : room_.stage;
  /* (A wedge, stairs or an arch has the rectangle for its footprint: another footprint is a plain solid's. The Subtract tick is
   * the Operation row's Subtract; off it is Auto again.) */
  if ((key == "circle" || key == "polyline") && on) room_.piece = 0;
  if (key == "subtract") room_.operation = -1;
  if (!room_.solid) room_.forget();  // (Solid switched off: the choices that hold only with it are forgotten)
  ui_.redraw = true;
}

namespace {

/* The Shape dropdown's nine entries - what the tool draws - and the tool's fields each one sets (round 496): a shape names its footprint,
 * so a Round Room and a Cylinder are the circle, the Outline ones the polyline, and the rest the rectangle. */
constexpr int kRoomShapeCount = 9;
const char *const kRoomShapeNames[kRoomShapeCount] = {"Room", "Round Room", "Outline Room", "Box", "Cylinder", "Outline", "Wedge", "Stairs", "Arch"};
const char *const kRoomShapeTips = "What the tool draws.\n"
                               "Room, Round Room, Outline Room: walls, a floor and a ceiling round the footprint (a rectangle, a circle from its centre, any outline).\n"
                               "Box, Cylinder, Outline: a plain solid by the same footprints. Pulled out it adds, pushed into a face it cuts; Shift while pulling "
                               "makes the other one.\n"
                               "Wedge, Stairs, Arch: over the dragged rectangle.";

template <class Tool>
void set_shape(Tool &r, int index) {
  r.solid = index >= 3;
  r.circle = index == 1 || index == 4;
  r.polyline = index == 2 || index == 5;
  r.piece = index >= 6 ? index - 3 : 0;
  if (!r.solid) r.forget();  // (a room has no such choices)
}

/* The words of an operation a console command fixed ("Add", "Subtract", "Intersect"); nullptr: Auto, or a room that has none. */
template <class Tool>
const char *fixed_operation_word(const Tool &r) {
  if (!r.solid) return nullptr;
  return r.operation == 0 ? "Add" : r.operation == 2 ? "Intersect" : r.subtract ? "Subtract" : nullptr;
}

}  // namespace

/* The Door Style and Window Style dropdowns under the Blockout buttons (round 498 had the window's above Draw Piece; round 499 puts both with the Door and Window buttons
 * they belong to): the style the next Door or Window made from a button or a menu has, the last one chosen. The console's `create Blockout Piece: Door` and `blockout add
 * door <style>` do not read them: the first makes Opening Only, the second says its own. */
void Editor::draw_blockout_style_rows(ui::Layout &lay) {
  auto &u = ui_;
  struct Row { const char *label, *id, *key, *tip; int *style; const char *const *names; int count; };
  const Row rows[] = {
      {"Door Style", "tools_door_style", "tools:door_style",
       "What the next Door (the button above, GameObject > Blockout) adds to the wall besides the hole: a frame and a leaf, a leaf that slides, a round head, or only the blocked-out\n"
       "space of a revolving door. Opening Only is the plain cut. The numbers (Leaf, Open Amount, Hinge Side...) are in the Inspector and Adjust Last Operation. Console: blockout add door <style>.",
       &blockout_door_style_, kBlockoutDoorStyleNames, kBlockoutDoorStyles},
      {"Window Style", "tools_window_style", "tools:window_style",
       "What the next Window (the button above, GameObject > Blockout) adds to the wall besides the hole: a frame, bars, a sill board and a pane, in the\n"
       "style chosen. Opening Only is the plain cut. The numbers (Frame Width, Panes...) are in the Inspector and Adjust Last Operation. Console: blockout add window <style>.",
       &blockout_window_style_, kBlockoutWindowStyleNames, kBlockoutWindowStyles}};
  for (const Row &r : rows) {
    const Recti row = lay.row(u.row_h() + u.px(2));
    u.label({row.x + u.px(4), row.y, row.w / 2, row.h}, r.label);
    const Recti style_rect{row.x + row.w / 2, row.y, row.w / 2 - u.px(6), row.h};
    if (u.record_buttons) u.button_rects[r.key] = style_rect;
    u.combo(u.id(r.id), style_rect, *r.style, r.names, r.count);
    u.tooltip(r.tip);
  }
}

/* The tool's switch and its options, in the Modeling Tools window (Create > Blockout): the switch, the Shape and, for a room, its wall. */
void Editor::draw_room_tools(ui::Layout &lay) {
  auto &u = ui_;
  Recti row = lay.row(u.row_h() + u.px(2));
  const std::string k = key("tool.room");
  const Recti sw{row.x + u.px(4), row.y, row.w - u.px(8), row.h};
  if (u.button(sw, k.empty() ? "Draw Piece" : "Draw Piece  (" + k + ")", room_.active)) set_room_tool(!room_.active);
  if (u.record_buttons) u.button_rects["tools:draw_piece"] = sw;
  u.tooltip("Drag a rectangle on the ground or a face, move the mouse for the height, click: the Shape chosen below stands there.\n"
            "Drawn on a face of a Blockout it becomes a piece of that Blockout; pushed into the face it goes in (a box: a pocket).\n"
            "Ctrl: no snapping. Shift while pulling the height: the other of Add and Subtract. Esc leaves. Console: tool room, blockout room, blockout draw.");
  row = lay.row();
  u.label({row.x + u.px(4), row.y, row.w / 2, row.h}, "Shape");
  int shape = blockout_draw_shape(room_.solid, room_.circle, room_.polyline, room_.piece);
  const Recti shape_rect{row.x + row.w / 2, row.y, row.w / 2 - u.px(6), row.h};
  if (u.record_buttons) u.button_rects["tools:shape"] = shape_rect;
  if (u.combo(u.id("tools_room_shape"), shape_rect, shape, kRoomShapeNames, kRoomShapeCount)) {
    set_shape(room_, shape);
    if (!room_.polyline && room_.stage == 3) room_.points.clear(), room_.stage = 0;
    ui_.redraw = true;
  }
  u.tooltip(kRoomShapeTips);
  /* An operation a console command fixed stays visible, and a click lets go of it (Auto again). */
  if (const char *fixed = fixed_operation_word(room_)) {
    row = lay.row();
    const Recti line{row.x + u.px(4), row.y, row.w - u.px(8), row.h};
    u.label(line, std::string("Operation: ") + fixed + " (fixed) - click to clear", u.theme.text_dim);
    if (u.record_buttons) u.button_rects["tools:operation_line"] = line;
    if (u.hovered(line) && u.in.pressed[0]) {
      room_.operation = -1, room_.subtract = false;
      u.consume_click();
      u.redraw = true;
    }
    u.tooltip("The Operation is fixed: it holds whichever way the height is pulled. Click this line: Auto again, where pulling out adds and pushing in subtracts (Shift while\n"
              "pulling flips it for one piece). Only the console fixes it - blockout roomopt operation add|subtract|intersect - there is no button for it here.");
  }
  if (blockout_draw_shape(room_.solid, room_.circle, room_.polyline, room_.piece) > 2) return;  // (only a room has a wall)
  row = lay.row();
  u.label({row.x + u.px(4), row.y, row.w / 2, row.h}, "Room Wall Thickness");
  u.float_field(u.id("tools_room_wall"), {row.x + row.w / 2, row.y, row.w / 2 - u.px(6), row.h}, room_.wall, 0.01f, 0.0f, 1000.0f);
  if (u.record_buttons) u.button_rects["tools:room_wall"] = {row.x + row.w / 2, row.y, row.w / 2 - u.px(6), row.h};
  u.tooltip("A new room's wall, floor and ceiling, in metres.");
}

}  // namespace bl

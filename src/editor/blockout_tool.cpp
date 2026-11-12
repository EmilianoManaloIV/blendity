// SPDX-License-Identifier: GPL-2.0-or-later
// The Room tool (task 0455, round 479): drag a rectangle - or, with the Circle option, a circle from its centre - on the ground or on a
// face of any mesh, then move the mouse for the height and click: the result is a room (or, with Solid, an Add or Subtract box or
// cylinder) in the active Blockout, turned to the surface and standing on it. With the Polyline option (round 480) the footprint is any
// outline: click its corners on the surface, close it on the first corner or with Enter, then pull the height as before. The tool stays on for the next room; Esc leaves it. The
// piece is made by room_make (blockout_editor.cpp), which `blockout room` calls with the same numbers.
// Round 495 (task 0479): the tool draws any piece - a room, a solid, a wedge, stairs, an arch - with any operation, and a piece
// drawn on a face of a Blockout lands in that Blockout; `blockout draw` is the same from the console.
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
bool set_draw_piece(const std::string &word, bool &solid, bool &circle, bool &polyline) {
  const int piece = word == "wedge" ? 3 : word == "stairs" ? 4 : word == "arch" ? 5 : 0;
  if (word != "room" && word != "solid" && !piece) return false;
  solid = word != "room";
  if (!solid) blockout_draw_forget();  // (a room has no such choices)
  if (piece) circle = polyline = false;
  blockout_draw_choice().piece = piece;
  return true;
}

/* The tool's Operation choice by its word; a room has none, and nothing is set for one. False: no such word. */
bool set_draw_operation(const std::string &word, bool solid, bool &subtract) {
  if (word != "auto" && word != "add" && word != "subtract" && word != "intersect") return false;
  if (!solid) return true;
  subtract = word == "subtract";
  blockout_draw_choice().operation = word == "add" ? 0 : word == "intersect" ? 2 : -1;
  return true;
}

/* The Operation chosen, as the row shows it: 0 Auto, 1 Add, 2 Subtract, 3 Intersect. */
int draw_operation_row(bool solid, bool subtract) {
  if (!solid) return 0;
  const int chosen = blockout_draw_choice().operation;
  return chosen == 0 ? 1 : chosen == 2 ? 3 : subtract ? 2 : 0;
}

}  // namespace

BlockoutDrawChoice &blockout_draw_choice() {
  static BlockoutDrawChoice choice;
  return choice;
}

void blockout_draw_forget() {
  blockout_draw_choice().piece = 0, blockout_draw_choice().operation = -1;
}

int blockout_draw_operation(bool solid, bool subtract, float height, bool nothing_to_cut) {
  if (!solid) return 0;
  const int chosen = blockout_draw_choice().operation;
  if (chosen == 0 || chosen == 2) return chosen;
  if (subtract) return 1;
  return height < 0.0f && !nothing_to_cut ? 1 : 0;  // (Auto, by the direction)
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
  if (!room_.solid) blockout_draw_forget();  // (the Draw and Operation choices hold with Solid only: blockout_pick.h)
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
    blockout_draw_choice().surface = face ? under.object->id : 0;
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
  if (!corners.empty()) room_make_outline(corners, up, n, height);
  else room_make(origin, up, n, du, dz, height, circle);
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
    if (r.stage == 2) {
      const float shown = r.solid ? r.height : std::fabs(r.height);  // (a room is built upward whatever the mouse does)
      const std::vector<Vec3> top = room_outline(shown);
      for (size_t i = 0; i < top.size(); i++) line(top[i], top[(i + 1) % top.size()], blue);
      const size_t step = r.circle && r.points.empty() ? 4 : 1;
      for (size_t i = 0; i < top.size(); i += step) line(base[i], top[i], blue);
      label = r.typed.empty() ? strprintf("height %.3f m", r.height) : strprintf("height [%s] = %.3f m", r.typed.c_str(), r.height);
      /* What the piece will do - Auto: pushed in it subtracts, where the Blockout it goes into (room_target's) has something to cut. */
      static const char *const does[] = {"Add", "Subtract", "Intersect"};
      GameObject *on = scene_->find(blockout_draw_choice().surface), *sel = active_object();
      const GameObject *into = on && on->get<Blockout>() ? on : sel && sel->get<Blockout>() ? sel : sel ? blockout_of_piece(*sel) : nullptr;
      if (r.solid) label += std::string("  ") + does[blockout_draw_operation(r.solid, r.subtract, r.height, !into || blockout_nothing_to_cut(*into))];
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
  /* The surface of a footprint the tool has in progress is the tool's: a command run meanwhile puts it back. */
  struct KeepSurface {
    uint64_t surface = blockout_draw_choice().surface;
    ~KeepSurface() { blockout_draw_choice().surface = surface; }
  } keep;
  blockout_draw_choice().surface = 0;  // (the ground: the selected Blockout, or a new one - `draw` looks its surface up)
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
    const BlockoutDrawChoice chosen = blockout_draw_choice();
    room_.circle = piece == "cylinder", room_.polyline = false;
    /* (A box and a cylinder are the tool's Solid by the footprint; "solid" itself is not a word here - it would not say which.) */
    const bool known = set_draw_piece(piece == "box" || piece == "cylinder" ? "solid" : piece == "solid" ? "" : piece, room_.solid, room_.circle, room_.polyline) &&
                       set_draw_operation(op, room_.solid, room_.subtract);
    if (known && !room_.solid && op != "auto") Log::warn("blockout draw: a room has no operation - '%s' is not used", op.c_str());
    if (known) {
      const Vec3 unit = normalize(n);
      blockout_draw_choice().surface = blockout_at_surface(*scene_, p, unit);
      room_make(p, surface_axis(unit), unit, v[6], v[7], v[8], room_.circle);
    }
    else Log::warn("%s", usage);
    room_ = tool;
    blockout_draw_choice() = chosen;
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
    const bool known = key == "piece" ? set_draw_piece(value, room_.solid, room_.circle, room_.polyline) : set_draw_operation(value, room_.solid, room_.subtract);
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
  if ((key == "circle" || key == "polyline") && on) blockout_draw_choice().piece = 0;
  if (key == "subtract") blockout_draw_choice().operation = -1;
  if (!room_.solid) blockout_draw_forget();  // (Solid switched off: the choices that hold only with it are forgotten)
  ui_.redraw = true;
}

/* The tool's switch and its options, in the Modeling Tools window (Create > Blockout). */
void Editor::draw_room_tools(ui::Layout &lay) {
  auto &u = ui_;
  Recti row = lay.row(u.row_h() + u.px(2));
  if (u.button({row.x + u.px(4), row.y, row.w - u.px(8), row.h}, room_.active ? "Exit Draw Piece (Room) Tool" : "Draw Piece (Room) Tool", room_.active)) set_room_tool(!room_.active);
  u.tooltip("Drag a rectangle on the ground or a face, move the mouse for the height, click: a room - or the piece chosen below - stands there.\n"
            "Drawn on a face of a Blockout it becomes a piece of that Blockout; pushed into the face it goes in (a Subtract box: a pocket).\n"
            "Ctrl: no snapping. Esc leaves. Console: tool room, blockout room, blockout draw.");
  struct Opt { const char *label, *tip; bool *value; };
  for (const Opt &o : {Opt{"Circle Footprint", "Drag a circle from its centre: a cylinder room.", &room_.circle},
                       Opt{"Polyline Footprint", "Click the corners of any outline on the ground or a face (close it on the first corner or with Enter, Backspace takes one back), then pull the height.", &room_.polyline}}) {
    row = lay.row(u.row_h() + u.px(2));
    if (u.button({row.x + u.px(4), row.y, row.w - u.px(8), row.h}, o.label, *o.value)) {
      *o.value = !*o.value;
      if (*o.value) blockout_draw_choice().piece = 0;  // (a wedge, stairs or an arch has the rectangle for its footprint)
    }
    u.tooltip(o.tip);
  }
  /* Round 495: what is drawn, and what a solid does to the Blockout it lands in. */
  struct Pick { const char *label, *word, *tip; };
  const int piece = !room_.solid ? 0 : blockout_draw_choice().piece == 0 ? 1 : blockout_draw_choice().piece - 1;
  int at = 0;
  for (const Pick &o : {Pick{"Draw: Room", "room", "A room: walls, a floor and a ceiling round the footprint."},
                        Pick{"Draw: Solid", "solid", "A plain box, cylinder or outline - by the footprint - instead of a room."},
                        Pick{"Draw: Wedge", "wedge", "A ramp over the dragged rectangle: tall where the drag began."},
                        Pick{"Draw: Stairs", "stairs", "Stairs over the dragged rectangle, climbing the way it was dragged."},
                        Pick{"Draw: Arch", "arch", "An arch lying in the surface, as wide as the rectangle's first side, pushed in or pulled out: Subtract cuts an arched opening."}}) {
    row = lay.row(u.row_h() + u.px(2));
    if (u.button({row.x + u.px(4), row.y, row.w - u.px(8), row.h}, o.label, piece == at++)) command(std::string("blockout roomopt piece ") + o.word);
    u.tooltip(o.tip);
  }
  const int operation = draw_operation_row(room_.solid, room_.subtract);
  at = 0;
  for (const Pick &o : {Pick{"Operation: Auto", "auto", "By the direction: pushed into the surface the solid cuts (Subtract), pulled out it adds."},
                        Pick{"Operation: Add", "add", "The solid adds to the Blockout whichever way it is pulled."},
                        Pick{"Operation: Subtract", "subtract", "The solid cuts out of the Blockout."},
                        Pick{"Operation: Intersect", "intersect", "The Blockout keeps only what is inside the solid."}}) {
    row = lay.row(u.row_h() + u.px(2));
    const bool on = room_.solid && operation == at++;
    const Recti r{row.x + u.px(4), row.y, row.w - u.px(8), row.h};
    if (u.button_enabled(r, o.label, room_.solid, "A room has no operation: choose Draw: Solid, Wedge, Stairs or Arch first.", on)) command(std::string("blockout roomopt operation ") + o.word);
    if (room_.solid) u.tooltip(o.tip);
  }
  row = lay.row();
  u.label({row.x + u.px(4), row.y, row.w / 2, row.h}, "Room Wall Thickness");
  u.float_field(u.id("tools_room_wall"), {row.x + row.w / 2, row.y, row.w / 2 - u.px(6), row.h}, room_.wall, 0.01f, 0.0f, 1000.0f);
  u.tooltip("A new room's wall, floor and ceiling, in metres.");
}

}  // namespace bl

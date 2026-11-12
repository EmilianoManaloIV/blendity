// SPDX-License-Identifier: GPL-2.0-or-later
// Handles on the active Blockout piece (task 0455, round 479): a small square on each face of a box, the cap and side of a cylinder,
// the surface of a sphere, a room's air and (one more, outside the nearest wall) its wall, a door's ends, head and sill. A drag
// moves the face under the mouse along its own line and leaves the opposite one where it is (Shift: both, about the centre; Ctrl:
// no snapping). It follows bone_edit.cpp's handles: dispatched before the gizmo, so a handle under the mouse wins. The drag only
// changes the piece's component and transform - the next frame's update_blockouts sees the new hash and builds on a worker
// (round 477): nothing here builds or waits for a build.
#include "editor.h"

#include "../core/core.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace bl {

namespace {

bool finite3(Vec3 v) { return finite_bits(v.x) && finite_bits(v.y) && finite_bits(v.z); }

/* The place of a plane square to the handle's line (metres from `origin` along `dir`) where another piece of the Blockout has a face
 * or a corner: dragging the handle there lays its face flush with that piece's face, or on the line of its edge. */
void snap_candidates(const GameObject &bo, uint64_t skip, Vec3 origin, Vec3 dir, std::vector<float> &out, uint64_t only = 0) {
  std::vector<const GameObject *> pieces;
  blockout_piece_objects(bo, pieces);
  for (const GameObject *p : pieces) {
    if (only ? p->id != only : p->id == skip) continue;  // (`only`: the places of that one piece's corners)
    const BlockoutPiece *bp = p->get<BlockoutPiece>();
    Mat4 frame = p->world_matrix();
    Vec3 lo0, hi0;
    if (bp->opening != 0) {
      lo0 = bp->box_size() * -0.5f, hi0 = bp->box_size() * 0.5f;
      frame = frame * bp->shape_offset();
    }
    else blockout_piece_bounds(*bp, lo0, hi0);  // (the piece's own box, whatever its shape: a wedge's, an outline's, a mesh's)
    if (!finite3(lo0) || !finite3(hi0)) continue;
    const Vec3 h = (hi0 - lo0) * 0.5f, centre = (hi0 + lo0) * 0.5f;
    auto add_box = [&](Vec3 lo, Vec3 hi) {
      for (int k = 0; k < 8; k++) {
        const Vec3 c((k & 1) ? hi.x : lo.x, (k & 2) ? hi.y : lo.y, (k & 4) ? hi.z : lo.z);
        const float t = dot(frame.point(c) - origin, dir);
        if (finite_bits(t)) out.push_back(t);
      }
    };
    add_box(lo0, hi0);
    if (!bp->is_room()) continue;
    const GameObject *owner = blockout_of_piece(*p);
    RoomGrowth g;
    if (owner && blockout_room_growth(bp->to_piece(p->id, blockout_piece_matrix(*p, *owner)), g))
      add_box({centre.x - h.x - (float)g.x, centre.y - h.y - (float)g.down, centre.z - h.z - (float)g.z},
              {centre.x + h.x + (float)g.x, centre.y + h.y + (float)g.up, centre.z + h.z + (float)g.z});
  }
}

/* What the hint line and the label beside the handle say: the handle's name and the size it is changing. */
std::string handle_text(const BlockoutHandle &h, const BlockoutPiece &p) {
  float v = 0.0f;
  switch (h.kind) {
    case BlockoutHandle::BoxFace: v = p.size[h.axis]; break;
    case BlockoutHandle::CylCap: v = p.height; break;
    case BlockoutHandle::CylRadius:
    case BlockoutHandle::SphereRadius: v = p.radius; break;
    case BlockoutHandle::Thickness: v = p.wall; break;
    case BlockoutHandle::OpenWidth: v = p.width; break;
    case BlockoutHandle::OpenHead: v = p.head; break;
    case BlockoutHandle::OpenSill: v = p.sill; break;
    case BlockoutHandle::OutlinePoint:
    case BlockoutHandle::OutlineAdd: {
      const size_t i = (size_t)std::max(0, h.axis);
      if (i < p.outline.size()) return strprintf("%s  %.3f, %.3f m", h.name.c_str(), p.outline[i].x, p.outline[i].y);
      break;
    }
  }
  return strprintf("%s  %.3f m", h.name.c_str(), v);
}

bool same_numbers(const BlockoutPiece &a, const BlockoutPiece &b) {
  if (a.outline.size() != b.outline.size()) return false;
  for (size_t i = 0; i < a.outline.size(); i++)
    if (a.outline[i].x != b.outline[i].x || a.outline[i].y != b.outline[i].y) return false;
  return a.size.x == b.size.x && a.size.y == b.size.y && a.size.z == b.size.z && a.radius == b.radius && a.height == b.height && a.wall == b.wall &&
         a.width == b.width && a.head == b.head && a.sill == b.sill;
}

}  // namespace

/* The active piece that shows handles: selected, in a Blockout, outside Edit Mode and Play, with no other tool holding the mouse. */
GameObject *Editor::blockout_handle_target() {
  if (!scene_ || playing_ || edit_mode_ || sculpt_ || paint_ || weight_paint_ || scatter_ || strokeobj_ || autorig_.active || room_.active) return nullptr;
  if (tool_ == Tool::View || xf_.active || modal_.active || pp_.active || knife_.active || draw_.active || drag_ != Drag::None) return nullptr;
  GameObject *g = active_object();
  if (!g || !is_selected(g->id) || !g->active_in_hierarchy()) return nullptr;
  const BlockoutPiece *bp = g->get<BlockoutPiece>();
  if (!bp || !bp->enabled || !blockout_of_piece(*g)) return nullptr;
  return g;
}

/* The piece's handles as the camera sees them: a round shape's side and a room's nearest wall are on the viewer's side. */
void Editor::blockout_handle_view(const GameObject &g, std::vector<BlockoutHandle> &out, bool visible_only) {
  out.clear();
  const BlockoutPiece *bp = g.get<BlockoutPiece>();
  if (!bp) return;
  const Mat4 w = g.world_matrix();
  const Vec3 toward = w.inverse().point(cam_.position());
  RoomGrowth grow;
  bool has_growth = false;
  if (bp->is_room())
    if (const GameObject *bo = blockout_of_piece(g)) has_growth = blockout_room_growth(bp->to_piece(g.id, blockout_piece_matrix(g, *bo)), grow);
  blockout_handles(*bp, finite3(toward) ? toward : Vec3(0.0f), has_growth ? &grow : nullptr, out);
  if (!visible_only) return;
  /* A handle whose line runs within about 10 degrees of the view ray cannot be pulled with the mouse (every ray reads the same place on it):
   * not drawn and not hit - a box's top and bottom seen from straight above. */
  const Vec3 eye = cam_.position();
  out.erase(std::remove_if(out.begin(), out.end(),
                           [&](const BlockoutHandle &h) {
                             if (blockout_handle_planar(h)) return false;  // (dragged in a plane: any view can pull it)
                             const Vec3 way = w.dir(h.dir);
                             const Vec3 view = cam_.ortho ? cam_.forward() : w.point(h.at) - eye;
                             const float lw = length(way), lv = length(view);
                             if (!(lw > 0.0f) || !(lv > 0.0f)) return false;
                             return std::fabs(dot(way, view)) / (lw * lv) > 0.985f;
                           }),
            out.end());
}

int Editor::blockout_handle_hot(const Recti &view, const GameObject &g, const std::vector<BlockoutHandle> &hs) {
  if (!scene_hovered_) return -1;
  const auto &in = ui_.in;
  const Mat4 w = g.world_matrix();
  int hot = -1;
  float best = (float)ui_.px(7);
  for (size_t i = 0; i < hs.size(); i++) {
    Vec2 s;
    float z;
    if (!scene_r3d_.project(w.point(hs[i].at), s, z)) continue;
    const float d = std::hypot((float)view.x + s.x - (float)in.mx, (float)view.y + s.y - (float)in.my);
    if (d <= best) best = d, hot = (int)i;
  }
  return hot;
}

/* The drag's start: the piece as it is, and the handle's line in the world. False when the piece is flat or not finite. */
bool Editor::blockout_handle_begin(GameObject &g, const BlockoutHandle &h, BlockoutDrag &d) {
  const BlockoutPiece *bp = g.get<BlockoutPiece>();
  const GameObject *bo = blockout_of_piece(g);
  if (!bp || !bo) return false;
  d = BlockoutDrag{};
  d.piece = g.id, d.handle = h, d.before = *bp, d.local0 = g.local(), d.world0 = g.world_matrix();
  const Vec3 way = d.world0.dir(h.dir);
  const float len = length(way);
  if (!(len > 1e-9f) || !finite_bits(len)) return false;
  d.scale_dir = len;
  d.dir_w = way * (1.0f / len);
  d.origin_w = d.world0.point(h.at);
  const float to_bo = length(blockout_piece_matrix(g, *bo).dir(h.dir));
  d.to_blockout = to_bo > 0.0f && finite_bits(to_bo) ? to_bo : 1.0f;
  return finite3(d.origin_w) && finite3(d.dir_w);
}

/* The piece with the handle moved `metres` (world metres along its way out) from where the drag took it. Both the component and the
 * transform are made from the start every time, so ten changes leave no drift. */
void Editor::blockout_handle_move(GameObject &g, const BlockoutDrag &d, float metres, bool both) {
  BlockoutPiece *bp = g.get<BlockoutPiece>();
  if (!bp) return;
  const Vec3 shift = blockout_handle_apply(*bp, d.before, d.handle, metres / d.scale_dir, both, d.to_blockout);
  if (length_sq(shift) > 0.0f) g.set_world_position(d.world0.point(shift));
  else g.set_local(d.local0);
}

/* Where along the handle's line the face goes, given where the mouse's line is (`raw`, world metres from the grab): another piece's
 * face or corner within a few pixels, kept until the mouse is twice as far from it; else the grid step. Ctrl: none of it. */
float Editor::blockout_snap_t(const Recti &, const GameObject &g, float raw) {
  BlockoutDrag &d = blockout_drag_;
  if (ui_.in.ctrl()) {
    d.snapped = false;
    return raw;
  }
  auto screen = [&](float t, Vec2 &s) {
    float z;
    return scene_r3d_.project(d.origin_w + d.dir_w * t, s, z);
  };
  Vec2 mouse;
  if (screen(raw, mouse)) {
    const float reach = (float)ui_.px(8);
    Vec2 held;
    if (d.snapped) {
      if (screen(d.snap_t, held) && length(held - mouse) < 2.0f * reach) return d.snap_t;
      d.snapped = false;
    }
    if (const GameObject *bo = blockout_of_piece(g)) {
      std::vector<float> ts;
      snap_candidates(*bo, g.id, d.origin_w, d.dir_w, ts);
      float best = reach, at = 0.0f;
      bool found = false;
      for (float c : ts) {
        Vec2 s;
        if (!screen(c, s)) continue;
        const float dist = length(s - mouse);
        if (dist < best) best = dist, at = c, found = true;
      }
      if (found) {
        d.snapped = true, d.snap_t = at;
        return at;
      }
    }
  }
  const float step = std::max(1e-3f, snap_move_);
  for (int a = 0; a < 3; a++) {
    if (std::fabs(d.dir_w[a]) < 0.9999f) continue;
    const float x0 = d.origin_w[a], target = std::round((x0 + d.dir_w[a] * raw) / step) * step;  // (the face's own world coordinate on the grid)
    return (target - x0) / d.dir_w[a];
  }
  return std::round(raw / step) * step;
}

bool Editor::blockout_move_snap(const Recti &, const GameObject &lead, Vec3 start_pos, Vec3 axis, float &d) {
  const GameObject *bo = blockout_of_piece(lead);
  if (!bo || !finite_bits(d) || !(length(axis) > 0.5f)) return false;
  Vec2 a, b;
  float z;
  if (!scene_r3d_.project(start_pos, a, z) || !scene_r3d_.project(start_pos + axis, b, z)) return false;
  const float px_per_m = length(b - a);
  if (!(px_per_m > 1e-3f)) return false;
  const float reach = (float)ui_.px(8) / px_per_m;
  const float moved = dot(lead.world_position() - start_pos, axis);
  std::vector<float> own, others;
  snap_candidates(*bo, 0, start_pos, axis, own, lead.id);     // (the piece's corners along the axis, where it is now)
  snap_candidates(*bo, lead.id, start_pos, axis, others);     // (every other piece's)
  float best = reach, at = d;
  for (float o : own)
    for (float c : others) {
      const float cand = c - (o - moved);  // (this corner, as the drag began, lands in that plane)
      if (std::fabs(cand - d) < best) best = std::fabs(cand - d), at = cand;
    }
  if (best >= reach) return false;
  d = at;
  return true;
}

/* Esc or the right button: the piece is as it was, byte for byte, and no undo step is made. */
void Editor::blockout_drag_cancel(GameObject *g) {
  if (g) {
    if (BlockoutPiece *bp = g->get<BlockoutPiece>()) *bp = blockout_drag_.before;
    g->set_local(blockout_drag_.local0);
  }
  const bool was_dirty = blockout_drag_.was_dirty;
  blockout_drag_ = BlockoutDrag{};
  pending_change_ = false;
  scene_dirty_ = was_dirty;  // (a cancelled drag leaves a saved scene saved)
  edit_epoch::bump();  // (the walk looks again: the pieces are back at what the mesh was built from)
  ui_.redraw = true;
}

bool Editor::blockout_drag_update(const Recti &view) {
  auto &in = ui_.in;
  BlockoutDrag &d = blockout_drag_;
  GameObject *g = scene_->find(d.piece);
  if (!g || !g->get<BlockoutPiece>() || playing_ || edit_mode_) {
    blockout_drag_ = BlockoutDrag{};
    return false;
  }
  if (in.key_pressed[platform::KEY_ESCAPE] || in.pressed[1]) {
    in.key_pressed[platform::KEY_ESCAPE] = false;
    in.pressed[1] = false;
    blockout_drag_cancel(g);
    return true;
  }
  const Ray ray = scene_r3d_.screen_ray((float)(in.mx - view.x), (float)(in.my - view.y));
  const float s = d.planar ? 0.0f : closest_on_line_to_ray(d.origin_w, d.dir_w, ray);
  if (!d.armed && std::hypot((float)in.mx - d.press_px.x, (float)in.my - d.press_px.y) >= (float)ui_.px(3)) d.armed = true;  // (a click is not a drag: no snap may resize it)
  if (d.planar) {
    if (d.armed) blockout_planar_move(*g, ray);
  }
  else if (finite_bits(s) && d.armed) {
    d.t = blockout_snap_t(view, *g, s - d.grab);
    if (d.t != 0.0f || d.moved) {  // (a press and release without a move changes nothing and makes no step)
      d.moved = true;
      blockout_handle_move(*g, d, d.t, in.shift());
      d.text = handle_text(d.handle, *g->get<BlockoutPiece>());
      mark_changed("Resize Blockout Piece");
    }
  }
  ui_.cursor = platform::Cursor::Move;
  ui_.redraw = true;
  if (!in.down[0]) blockout_drag_ = BlockoutDrag{};  // (the step commits once the button is up)
  return true;
}

/* An outline's point under the mouse: the ray meets the plane the point lies in (the piece's XZ plane at the point's height); the point goes
 * where the mouse is (kept the same distance from it as it was when taken), on the grid - the world's, when the plane is level - unless Ctrl. */
void Editor::blockout_planar_move(GameObject &g, const Ray &ray) {
  BlockoutDrag &d = blockout_drag_;
  BlockoutPiece *bp = g.get<BlockoutPiece>();
  float t = 0.0f;
  if (!bp || !ray_plane(ray, d.origin_w, d.dir_w, t) || !finite_bits(t)) return;
  Vec3 at = ray.origin + ray.dir * t + d.grab_off;
  d.snapped = false;
  if (!ui_.in.ctrl()) {
    const float step = std::max(1e-3f, snap_move_);
    if (std::fabs(d.dir_w.y) > 0.9999f) at = Vec3(std::round(at.x / step) * step, at.y, std::round(at.z / step) * step);  // (a level plane: the world's grid)
    else {
      const Vec3 own = d.world0.inverse().point(at);
      at = d.world0.point(Vec3(std::round(own.x / step) * step, own.y, std::round(own.z / step) * step));
    }
  }
  const Vec3 own = d.world0.inverse().point(at);
  if (!finite3(own)) return;
  blockout_outline_move(*bp, d.base, d.index, own.x, own.z);
  d.moved = true;
  d.text = handle_text(BlockoutHandle{BlockoutHandle::OutlinePoint, d.index, Vec3(0.0f), Vec3(0.0f, 1.0f, 0.0f), "p" + std::to_string(d.index)}, *bp);
  mark_changed("Edit Blockout Outline");
}

/* A press on a handle starts its drag (before the gizmo sees it); hovering one keeps the gizmo off it. True while a handle has the mouse. */
bool Editor::blockout_handles_update(const Recti &view) {
  auto &u = ui_;
  auto &in = u.in;
  if (blockout_drag_.active && blockout_drag_update(view)) return true;
  GameObject *g = blockout_handle_target();
  if (!g) return false;
  std::vector<BlockoutHandle> hs;
  blockout_handle_view(*g, hs);
  const int hot = blockout_handle_hot(view, *g, hs);
  if (hot < 0 || !in.pressed[0] || in.alt() || u.any_active()) return hot >= 0;
  BlockoutDrag d;
  if (!blockout_handle_begin(*g, hs[(size_t)hot], d)) return true;
  const Ray ray = scene_r3d_.screen_ray((float)(in.mx - view.x), (float)(in.my - view.y));
  if (blockout_handle_planar(d.handle)) {
    /* An outline's point: taken where it is; a "+" first adds the point (the click is one undo step) and then drags it. */
    d.planar = true, d.index = d.handle.axis, d.base = d.before;
    BlockoutPiece *bp = g->get<BlockoutPiece>();
    float t = 0.0f;
    d.grab_off = ray_plane(ray, d.origin_w, d.dir_w, t) && finite_bits(t) ? d.origin_w - (ray.origin + ray.dir * t) : Vec3(0.0f);
    if (d.handle.kind == BlockoutHandle::OutlineAdd) {
      blockout_outline_insert(d.base, d.handle.axis);
      if (d.base.outline.size() == d.before.outline.size()) return true;  // (256 corners: no more)
      d.index = d.handle.axis + 1;
      d.handle = BlockoutHandle{BlockoutHandle::OutlinePoint, d.index, Vec3(0.0f), Vec3(0.0f, 1.0f, 0.0f), "p" + std::to_string(d.index)};
      d.grab_off = Vec3(0.0f);
      *bp = d.base;
      d.moved = true;
      mark_changed("Edit Blockout Outline");
    }
  }
  else {
    d.grab = closest_on_line_to_ray(d.origin_w, d.dir_w, ray);
    if (!finite_bits(d.grab)) d.grab = 0.0f;
  }
  d.active = true;
  d.press_px = Vec2((float)in.mx, (float)in.my);
  d.was_dirty = scene_dirty_;
  d.text = handle_text(d.handle, d.before);
  blockout_drag_ = d;
  in.pressed[0] = false;  // (taken: the gizmo and the selection do not see it)
  return true;
}

/* The handles drawn over the picture: squares, the one under the mouse white; while dragging, the size beside the handle. */
void Editor::draw_blockout_handles(const Recti &view) {
  auto &u = ui_;
  GameObject *g = blockout_drag_.active ? scene_->find(blockout_drag_.piece) : blockout_handle_target();
  if (!g || !g->get<BlockoutPiece>()) return;
  std::vector<BlockoutHandle> hs;
  blockout_handle_view(*g, hs, !blockout_drag_.active);  // (the one in hand is drawn whichever way the camera looks)
  const Mat4 w = g->world_matrix();
  int hot = -1;
  if (blockout_drag_.active) {
    for (size_t i = 0; i < hs.size(); i++)
      if (hs[i].name == blockout_drag_.handle.name) hot = (int)i;
  }
  else hot = blockout_handle_hot(view, *g, hs);
  u.canvas.push_clip(view);
  for (size_t i = 0; i < hs.size(); i++) {
    Vec2 s;
    float z;
    if (!scene_r3d_.project(w.point(hs[i].at), s, z)) continue;
    const float x = (float)view.x + s.x, y = (float)view.y + s.y;
    const bool over = hot == (int)i;
    const bool wall = hs[i].kind == BlockoutHandle::Thickness;
    const bool add = hs[i].kind == BlockoutHandle::OutlineAdd;
    const float r = (float)u.px(over ? 6 : add ? 4 : 5);
    u.canvas.fill_rect({(int)(x - r - 1), (int)(y - r - 1), (int)(2 * r + 2), (int)(2 * r + 2)}, Color::hex(0x101010));
    u.canvas.fill_rect({(int)(x - r), (int)(y - r), (int)(2 * r), (int)(2 * r)}, over ? Color::hex(0xFFFFFF) : wall ? Color::hex(0xF4BC02) : add ? Color::hex(0x7BD88F) : Color::hex(0x5AA9FF));
    if (u.record_buttons) u.button_rects["blockout_handle:" + hs[i].name] = Recti{(int)x - 5, (int)y - 5, 10, 10};  // (tests)
    if (!blockout_drag_.active || !over) continue;
    const std::string text = blockout_drag_.text + (blockout_drag_.snapped ? "  (snapped)" : "");
    const int tw = u.font.text_width(text) + u.px(12);
    const Recti box{(int)x + u.px(12), (int)y + u.px(10), tw, u.row_h()};
    u.canvas.fill_round_rect(box, u.px(3), Color::hex(0x202020, 230));
    u.label(box, text, u.theme.text_bright, ui::Align::Center);
  }
  u.canvas.pop_clip();
}

/* blockout handle <side> <metres> [both]: the selected piece's side moved that far out (as the handle would; negative goes in). */
void Editor::blockout_handle_command(const std::vector<std::string> &t) {
  GameObject *g = active_object();
  BlockoutPiece *bp = g ? g->get<BlockoutPiece>() : nullptr;
  if (!bp || !blockout_of_piece(*g)) return Log::warn("blockout handle <side> <metres>: select a Blockout piece first");
  const std::string usage = "blockout handle <side> <metres> [both] - sides: +x -x +y -y +z -z (a box, a room), top bottom side (a cylinder), radius (a sphere), "
                            "wall (a room), width head sill (a door or window), +z -z (an arch's depth), top bottom (an outline's height)";
  if (t.size() < 4) return Log::warn("%s", usage.c_str());
  const std::string num = t[3];
  const bool is_number = !num.empty() && (std::isdigit((unsigned char)num[0]) || num[0] == '-' || num[0] == '.' || num[0] == '+');
  const float metres = (float)std::atof(num.c_str());
  if (!is_number || !finite_bits(metres)) return Log::warn("%s", usage.c_str());
  std::vector<BlockoutHandle> hs;
  blockout_handle_view(*g, hs, false);  // (a script reaches every handle, whichever way the camera looks)
  const BlockoutHandle *h = blockout_handle_named(hs, t[2]);
  if (!h) return Log::warn("blockout handle: this piece has no '%s' handle (%s)", t[2].c_str(), usage.c_str());
  if (blockout_handle_planar(*h)) return Log::warn("blockout handle: an outline's points move in a plane - set them with blockout outline <x z> <x z> ...");
  BlockoutDrag d;
  if (!blockout_handle_begin(*g, *h, d)) return Log::warn("blockout handle: the piece is flat or not finite");
  blockout_handle_move(*g, d, metres, t.size() > 4 && to_lower(t[4]) == "both");
  const bool changed = !same_numbers(d.before, *bp) || g->local().position.x != d.local0.position.x || g->local().position.y != d.local0.position.y ||
                       g->local().position.z != d.local0.position.z;
  if (!changed) return;  // (no undo step for nothing)
  mark_changed("Resize Blockout Piece");
  update_blockouts(true);
}

}  // namespace bl

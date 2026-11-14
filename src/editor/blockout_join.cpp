// SPDX-License-Identifier: GPL-2.0-or-later
// Blockouts beside each other (round 501, task 0485, ADR 0129's amendment): a moved Blockout, a moved piece and a dragged face handle
// snap to the face planes and corners of the OTHER Blockouts in the scene; a Blockout whose result touches another's shows "touches
// 'Name'" and a Join Blockouts button; `blockout join` moves the other Blockouts' pieces under one Blockout, each keeping its place
// in the world, and removes the emptied objects. Nothing here builds a mesh: the join asks for the one build every Blockout command asks for.
#include "editor.h"

#include "../core/core.h"

#include <algorithm>
#include <cmath>

namespace bl {

namespace {

bool finite3(Vec3 v) { return finite_bits(v.x) && finite_bits(v.y) && finite_bits(v.z); }

/* A box in the world: its eight corners (bit k of the index picks the high side of x, y, z) and the axis-aligned box round them. */
struct WBox {
  Vec3 c[8];
  Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
  bool ok = true;
};

WBox make_box(const Mat4 &frame, Vec3 lo, Vec3 hi) {
  WBox b;
  for (int k = 0; k < 8; k++) {
    b.c[k] = frame.point(Vec3((k & 1) ? hi.x : lo.x, (k & 2) ? hi.y : lo.y, (k & 4) ? hi.z : lo.z));
    b.ok = b.ok && finite3(b.c[k]);
    b.lo = vmin(b.lo, b.c[k]);
    b.hi = vmax(b.hi, b.c[k]);
  }
  return b;
}

/* The boxes a Blockout's enabled pieces occupy, as the same-Blockout snap takes them (blockout_handles.cpp): each piece's own box, and a
 * room's shell grown by its walls. `solid_only`: the matter alone - an Add piece (no opening) or a room's shell. */
void world_boxes(const GameObject &bo, bool solid_only, std::vector<WBox> &out) {
  std::vector<const GameObject *> pieces;
  blockout_piece_objects(bo, pieces);
  for (const GameObject *p : pieces) {
    const BlockoutPiece *bp = p->get<BlockoutPiece>();
    const bool solid = bp->is_room() || (bp->operation == (int)blockout::Op::Add && bp->opening == 0);
    if (solid_only && !solid) continue;
    Mat4 frame = p->world_matrix();
    Vec3 lo, hi;
    if (bp->opening != 0) {
      lo = bp->box_size() * -0.5f, hi = bp->box_size() * 0.5f;
      frame = frame * bp->shape_offset();
    }
    else blockout_piece_bounds(*bp, lo, hi);
    if (!finite3(lo) || !finite3(hi)) continue;
    const Vec3 h = (hi - lo) * 0.5f, centre = (hi + lo) * 0.5f;
    RoomGrowth g;
    const GameObject *owner = bp->is_room() ? blockout_of_piece(*p) : nullptr;
    const bool shell = owner && blockout_room_growth(bp->to_piece(p->id, blockout_piece_matrix(*p, *owner)), g);
    auto add = [&](Vec3 a, Vec3 b) {
      const WBox x = make_box(frame, a, b);
      if (x.ok) out.push_back(x);
    };
    if (!(solid_only && bp->is_room())) add(lo, hi);  // (the matter of a room is its shell minus its air, below)
    if (!shell) continue;
    const Vec3 slo(centre.x - h.x - (float)g.x, centre.y - h.y - (float)g.down, centre.z - h.z - (float)g.z);
    const Vec3 shi(centre.x + h.x + (float)g.x, centre.y + h.y + (float)g.up, centre.z + h.z + (float)g.z);
    if (!solid_only) {
      add(slo, shi);
      continue;
    }
    /* The wall, floor and ceiling slabs: a Blockout standing inside the room's air is not touching it. A slab with no thickness holds no matter. */
    constexpr float kThin = 1e-6f;
    if (lo.y - slo.y > kThin) add(slo, Vec3(shi.x, lo.y, shi.z));   // floor
    if (shi.y - hi.y > kThin) add(Vec3(slo.x, hi.y, slo.z), shi);   // ceiling
    if (lo.x - slo.x > kThin) add(slo, Vec3(lo.x, shi.y, shi.z));   // -X wall
    if (shi.x - hi.x > kThin) add(Vec3(hi.x, slo.y, slo.z), shi);   // +X wall
    if (lo.z - slo.z > kThin) add(slo, Vec3(shi.x, shi.y, lo.z));   // -Z wall
    if (shi.z - hi.z > kThin) add(Vec3(slo.x, slo.y, hi.z), shi);   // +Z wall
  }
}

/* Whether two boxes touch or overlap: the separating-axis test on their face normals and edge crosses, `tol` metres of slack (the boxes
 * may be turned, scaled and even sheared by what is above them: they are parallelepipeds, not always boxes). */
bool boxes_touch(const WBox &a, const WBox &b, float tol) {
  for (int k = 0; k < 3; k++)
    if (a.lo[k] > b.hi[k] + tol || b.lo[k] > a.hi[k] + tol) return false;
  const Vec3 ea[3] = {a.c[1] - a.c[0], a.c[2] - a.c[0], a.c[4] - a.c[0]};
  const Vec3 eb[3] = {b.c[1] - b.c[0], b.c[2] - b.c[0], b.c[4] - b.c[0]};
  auto separated = [&](Vec3 axis, float scale) {
    const float len = length(axis);
    if (!(len > 1e-4f * scale) || !finite_bits(len)) return false;  // (parallel edges name no axis)
    axis = axis * (1.0f / len);
    float amin = 1e30f, amax = -1e30f, bmin = 1e30f, bmax = -1e30f;
    for (int k = 0; k < 8; k++) {
      const float x = dot(a.c[k], axis), y = dot(b.c[k], axis);
      amin = std::min(amin, x), amax = std::max(amax, x), bmin = std::min(bmin, y), bmax = std::max(bmax, y);
    }
    return amin > bmax + tol || bmin > amax + tol;
  };
  for (int i = 0; i < 3; i++) {
    const int j = (i + 1) % 3;
    if (separated(cross(ea[i], ea[j]), length(ea[i]) * length(ea[j]))) return false;
    if (separated(cross(eb[i], eb[j]), length(eb[i]) * length(eb[j]))) return false;
  }
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      if (separated(cross(ea[i], eb[j]), length(ea[i]) * length(eb[j]))) return false;
  /* A box with no thickness (an edge of no length) has fewer edge crosses than the test needs: two flat pieces in one plane, turned against each
   * other, are only told apart by the in-plane normals of their edges. Those are tried for every edge against every face normal. */
  auto flat = [](const Vec3 *e) {
    const float longest = std::max(length(e[0]), std::max(length(e[1]), length(e[2])));
    return std::min(length(e[0]), std::min(length(e[1]), length(e[2]))) <= 1e-6f * std::max(longest, 1e-12f);
  };
  if (flat(ea) || flat(eb)) {
    std::vector<Vec3> normals;
    for (const Vec3 *e : {ea, eb})
      for (int i = 0; i < 3; i++) normals.push_back(cross(e[i], e[(i + 1) % 3]));
    for (const Vec3 &n : normals)
      for (const Vec3 *e : {ea, eb})
        for (int i = 0; i < 3; i++)
          if (separated(cross(e[i], n), length(e[i]) * length(n))) return false;
  }
  return true;
}

AABB bounds_of(const std::vector<WBox> &v) {
  AABB r;
  for (const WBox &b : v) r.add(b.lo), r.add(b.hi);
  return r;
}

bool overlap(const AABB &a, const AABB &b, float tol) {
  if (!a.valid() || !b.valid()) return false;
  for (int k = 0; k < 3; k++)
    if (a.min[k] > b.max[k] + tol || b.min[k] > a.max[k] + tol) return false;
  return true;
}

constexpr float kTouchTol = 1e-3f;  // (a millimetre: two walls laid flush by a float's rounding touch)

/* The Blockouts at or below `g` (a moving object may hold several, or be one). */
void collect_blockouts(const GameObject &g, std::vector<const GameObject *> &out) {
  if (g.get<Blockout>()) out.push_back(&g);
  for (const GameObject *c : g.children) collect_blockouts(*c, out);
}

/* The piece objects of a Blockout, enabled or not, in folders or not: not into another Blockout. */
void collect_pieces(GameObject &n, std::vector<GameObject *> &out) {
  for (GameObject *c : n.children) {
    if (c->get<Blockout>()) continue;
    if (c->get<BlockoutPiece>()) out.push_back(c);
    collect_pieces(*c, out);
  }
}

/* A world matrix whose three axes have a length and every number is finite: pieces can be given places under it. */
bool solid_matrix(const Mat4 &m) {
  for (int a = 0; a < 3; a++) {
    const float len = length(m.dir(Vec3(a == 0, a == 1, a == 2)));
    if (!finite_bits(len) || !(len > 1e-9f)) return false;
  }
  return finite3(m.translation());
}

Vec3 axis_lengths(const Mat4 &m) { return Vec3(length(m.dir({1, 0, 0})), length(m.dir({0, 1, 0})), length(m.dir({0, 0, 1}))); }

/* Whether the object's own place in the world (its world matrix) is a number. */
bool finite_place(const GameObject &g) {
  const Mat4 m = g.world_matrix();
  return finite3(m.translation()) && finite3(m.dir({1, 0, 0})) && finite3(m.dir({0, 1, 0})) && finite3(m.dir({0, 0, 1}));
}

bool off_one(const Vec3 &s) {
  for (int a = 0; a < 3; a++)
    if (s[a] != 0.0f && std::fabs(std::fabs(s[a]) - 1.0f) > 1e-6f) return true;  // (a flat axis, 0, has nothing to move)
  return false;
}

/* Whether the object's own turn leaves its axes off the world's (a piece turned 30 degrees): what an uneven scale above it would shear. */
bool turned(const GameObject &g) {
  const Quat q = g.local().rotation;
  for (const Vec3 axis : {Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1)}) {
    const Vec3 v = q.rotate(axis);
    for (int a = 0; a < 3; a++) {
      const float m = std::fabs(v[a]);
      if (m > 1e-3f && m < 1.0f - 1e-3f) return true;
    }
  }
  return false;
}

/* Whether the object or any piece object below it is turned (folders walked; another Blockout is not entered). */
bool any_turned_below(const GameObject &g) {
  if (turned(g)) return true;
  for (const GameObject *c : g.children)
    if (!c->get<Blockout>() && any_turned_below(*c)) return true;
  return false;
}

}  // namespace

/* ===================================================================== */
/* The other Blockouts' places, for the snaps                              */
/* ===================================================================== */

/* The corners of every box of every live Blockout in the scene but the `excluded` ones (sorted ids), in the world. Kept while the drag that
 * asks lasts: the same excluded set and scene, the last use in the frame before, and no press of the mouse (a press is a new drag: collected
 * again, so a piece moved since is seen). */
const std::vector<Vec3> &Editor::blockout_targets(std::vector<uint64_t> excluded) {
  std::sort(excluded.begin(), excluded.end());
  BlockoutCross &c = blockout_cross_;
  const void *sc = scene_ ? static_cast<const void *>(&*scene_) : nullptr;
  /* Not keyed on edit_epoch::structure(): the Move gizmo's own mark_changed bumps it every frame of a drag. The targets are collected again at
   * every press of the mouse (a new drag - the gizmo snaps in its press frame too), after a frame in which nothing asked, and when the scene, its
   * object count or the excluded set differ. Within one drag they are kept: another Blockout moved meanwhile by playback or a script is seen at
   * the next press, not before. */
  const uint64_t objects = scene_ ? (uint64_t)scene_->object_count() : 0;
  const bool fresh = c.scene == sc && c.structure == objects && c.excluded == excluded && frames_ >= c.frame && frames_ - c.frame <= 1 && !ui_.in.pressed[0];
  c.frame = frames_;
  if (fresh) return c.points;
  c.scene = sc, c.structure = objects, c.excluded = excluded;
  c.points.clear();
  c.axis_valid = false;
  if (!scene_) return c.points;
  std::vector<WBox> boxes;
  scene_->for_each([&](GameObject &g) {
    if (!g.get<Blockout>() || !g.active_in_hierarchy() || std::binary_search(excluded.begin(), excluded.end(), g.id)) return;
    boxes.clear();
    world_boxes(g, false, boxes);
    for (const WBox &b : boxes)
      for (int k = 0; k < 8; k++) c.points.push_back(b.c[k]);
  });
  return c.points;
}

/* The places along a line (metres from `origin` along `dir`) where another Blockout has a face or a corner: a piece moved, or a face handle
 * dragged, lands there as it lands in its own Blockout's pieces (those come first in the list and win a tie). */
void Editor::blockout_cross_candidates(const GameObject &own_blockout, Vec3 origin, Vec3 dir, std::vector<float> &out) {
  const std::vector<Vec3> &pts = blockout_targets({own_blockout.id});
  for (const Vec3 &p : pts) {
    const float t = dot(p - origin, dir);
    if (finite_bits(t)) out.push_back(t);
  }
}

/* A moved Blockout (or several) along `axis`: `d` metres from where the drag began, moved so a corner of its result lands in the plane of a
 * corner of another Blockout within the snap's pixel reach (the piece snap's, blockout_move_snap). `roots`: the objects moving; the Blockouts
 * in them are the moving ones, the others the targets. `lead` and `start_pos`: the lead object and where it stood when the drag began.
 * `marker` (when given) gets the world place of the corner that landed, for the gizmo's snap mark. True when it snapped. */
bool Editor::blockout_whole_snap(const GameObject &lead, Vec3 start_pos, Vec3 axis, const std::vector<uint64_t> &roots, float &d, Vec3 *marker) {
  if (!scene_ || !finite_bits(d) || !(length(axis) > 0.5f)) return false;
  std::vector<const GameObject *> moving;
  for (uint64_t id : roots)
    if (const GameObject *g = scene_->find(id)) collect_blockouts(*g, moving);
  if (moving.empty()) return false;
  Vec2 a, b;
  float z;
  if (!scene_r3d_.project(start_pos, a, z) || !scene_r3d_.project(start_pos + axis, b, z)) return false;
  const float px_per_m = length(b - a);
  if (!(px_per_m > 1e-3f)) return false;
  const float reach = (float)ui_.px(8) / px_per_m;
  std::vector<uint64_t> ids;
  for (const GameObject *m : moving) ids.push_back(m->id);
  for (uint64_t id : roots)  // (a piece moved with them belongs to a Blockout that is no target: it would snap to itself)
    if (const GameObject *g = scene_->find(id))
      if (const GameObject *o = blockout_of_piece(*g)) ids.push_back(o->id);
  const std::vector<Vec3> &pts = blockout_targets(ids);
  if (pts.empty()) return false;
  BlockoutCross &c = blockout_cross_;
  if (!c.axis_valid || !(c.axis.x == axis.x && c.axis.y == axis.y && c.axis.z == axis.z)) {
    c.sorted.clear();
    for (const Vec3 &p : pts) {
      const float t = dot(p, axis);
      if (finite_bits(t)) c.sorted.push_back(t);
    }
    std::sort(c.sorted.begin(), c.sorted.end());
    c.axis = axis, c.axis_valid = true;
  }
  if (c.sorted.empty()) return false;
  const float moved = dot(lead.world_position() - start_pos, axis);
  std::vector<WBox> own;
  for (const GameObject *m : moving) world_boxes(*m, false, own);
  float best = reach, at = d;
  Vec3 corner(0.0f);  // (the moving corner that lands, where it is now)
  for (const WBox &box : own)
    for (int k = 0; k < 8; k++) {
      const float from = dot(box.c[k], axis) - moved;  // (this corner as the drag began)
      auto it = std::lower_bound(c.sorted.begin(), c.sorted.end(), from + d);
      for (int side = 0; side < 2; side++) {  // (the target just above and just below where the corner would land)
        if (side == 0 && it == c.sorted.end()) continue;
        if (side == 1 && it == c.sorted.begin()) continue;
        const float cand = (side == 0 ? *it : *(it - 1)) - from;
        if (std::fabs(cand - d) < best) best = std::fabs(cand - d), at = cand, corner = box.c[k];
      }
    }
  if (best >= reach) return false;
  d = at;
  if (marker) *marker = corner + axis * (at - moved);  // (that corner once the move is made: it lies in the other Blockout's plane)
  return true;
}

/* The Move gizmo's axis drag (scene_view.cpp): the objects it moves, the last one the lead. Not for a piece (its own snap runs): for the rest, when a Blockout is among them. */
bool Editor::blockout_gizmo_snap(Vec3 axis, float &along) {
  if (gizmo_starts_.empty() || !gizmo_typed_.empty()) return false;  // (a typed distance is exact: nothing snaps it and no mark is drawn for a place it is not at)
  const GameObject *lead = scene_ ? scene_->find(gizmo_starts_.back().id) : nullptr;
  if (!lead || lead->get<BlockoutPiece>()) return false;
  std::vector<uint64_t> ids;
  for (const GizmoStart &s : gizmo_starts_) ids.push_back(s.id);
  Vec3 at;
  if (!blockout_whole_snap(*lead, gizmo_starts_.back().pos, axis, ids, along, &at)) return false;
  gizmo_snap_shown_ = true, gizmo_snap_point_ = at;  // (the mark the vertex and surface snaps draw)
  return true;
}

/* The modal G's axis move (modal_transform.cpp): the same, on the objects it holds. */
bool Editor::blockout_modal_snap(Vec3 axis, float &along) {
  if (xf_.objects.empty() || !scene_) return false;
  const auto &last = xf_.objects.back();
  const GameObject *lead = scene_->find(last.first);
  if (!lead) return false;
  if (lead->get<BlockoutPiece>()) return blockout_move_snap(Recti{}, *lead, last.second.translation(), axis, along);  // (a piece: its own snap, as the Move gizmo's - now with the other Blockouts' too)
  std::vector<uint64_t> ids;
  for (const auto &o : xf_.objects) ids.push_back(o.first);
  return blockout_whole_snap(*lead, last.second.translation(), axis, ids, along);
}

/* ===================================================================== */
/* Touching                                                               */
/* ===================================================================== */

/* The Blockouts whose result touches or overlaps `a`'s: their matter's world bounds meet (a millimetre of slack) and some solid piece of one
 * (an Add piece, a room with its shell) meets one of the other's. Kept for the edit epoch: the Inspector asks every frame. */
void Editor::blockout_touching(const GameObject &a, std::vector<GameObject *> &out) {
  out.clear();
  if (!scene_) return;
  BlockoutTouch &c = blockout_touch_;
  const void *sc = static_cast<const void *>(&*scene_);
  if (c.scene != sc || c.epoch != edit_epoch::now()) c.scene = sc, c.epoch = edit_epoch::now(), c.by.clear();
  auto found = c.by.find(a.id);
  if (found == c.by.end()) {
    std::vector<uint64_t> ids;
    std::vector<WBox> mine, theirs;
    world_boxes(a, true, mine);
    const AABB mine_bounds = bounds_of(mine);
    if (!mine.empty())
      scene_->for_each([&](GameObject &g) {
        if (&g == &a || !g.get<Blockout>() || !g.active_in_hierarchy()) return;
        theirs.clear();
        world_boxes(g, true, theirs);
        if (!overlap(mine_bounds, bounds_of(theirs), kTouchTol)) return;
        for (const WBox &x : mine)
          for (const WBox &y : theirs)
            if (boxes_touch(x, y, kTouchTol)) return ids.push_back(g.id);
      });
    found = c.by.emplace(a.id, std::move(ids)).first;
  }
  for (uint64_t id : found->second)
    if (GameObject *g = scene_->find(id)) out.push_back(g);
}

bool Editor::blockout_touches(const GameObject &a, const GameObject &b) {
  std::vector<GameObject *> v;
  blockout_touching(a, v);
  return std::find(v.begin(), v.end(), &b) != v.end();
}

/* ===================================================================== */
/* The Inspector's row and the Mesh menu's entry                          */
/* ===================================================================== */

/* The Blockouts that would join: the selected ones and those of the selected pieces; one alone counts when something touches it. */
bool Editor::blockout_join_available() {
  if (!scene_ || playing_ || edit_mode_) return false;  // (the join refuses in Play and in Edit Mode: not offered there)
  std::vector<GameObject *> bos;
  for (GameObject *g : selected_objects(false)) {
    GameObject *b = g->get<Blockout>() ? g : blockout_of_piece(*g);
    if (b && std::find(bos.begin(), bos.end(), b) == bos.end()) bos.push_back(b);
  }
  if (bos.size() >= 2) return true;
  std::vector<GameObject *> touching;
  if (bos.size() == 1) blockout_touching(*bos[0], touching);
  return !touching.empty();
}

/* "touches 'A'", "touches 'A', 'B'", "touches 'A', 'B' and 3 more", then the button; nothing when no other Blockout is touched. */
void Editor::draw_blockout_join_row(GameObject &g, Blockout &, ui::Layout &lay) {
  auto &u = ui_;
  if (playing_ || edit_mode_ || !scene_) return;
  std::vector<GameObject *> touching;
  blockout_touching(g, touching);
  if (touching.empty()) return;
  std::string text = "touches '" + touching[0]->name + "'";
  if (touching.size() >= 2) text += ", '" + touching[1]->name + "'";
  if (touching.size() > 2) text += strprintf(" and %zu more", touching.size() - 2);
  const Recti note = lay.row(u.row_h() + u.px(2));
  u.label({note.x + u.px(4), note.y, note.w - u.px(8), note.h}, text, u.theme.text_dim);
  u.tooltip(text);
  if (u.record_buttons) u.button_rects["shown:" + text] = note;
  const Recti row = lay.row(u.row_h() + u.px(2));
  if (u.button({row.x + u.px(4), row.y, row.w - u.px(8), row.h}, "Join Blockouts"))
    defer([this] { run_console_command("blockout join"); });  // (after the panel's loop: it removes the objects being drawn)
  u.tooltip("Moves the pieces of the Blockouts that touch this one under it, each keeping its place in the world, and removes the emptied objects: one mesh, the shared\n"
            "walls merged. The settings that differ (Field Cell, Texture Scale, class materials...) take this Blockout's. One Undo step. Console: blockout join [name, name ...].");
}

/* ===================================================================== */
/* Join                                                                   */
/* ===================================================================== */

/* `blockout join [name, name ...]`: with names, those Blockouts into the active one; without, the other selected Blockouts (and those of the selected
 * pieces) into the active one; with one selected, every Blockout that touches it, and those that touch those. */
void Editor::blockout_join_command(const std::vector<std::string> &t) {
  if (!scene_) return;
  auto owner_of = [](GameObject *g) -> GameObject * {
    if (!g) return nullptr;
    if (g->get<Blockout>() || g->get<BlockoutBaked>()) return g;
    return blockout_of_piece(*g);
  };
  auto add = [](std::vector<GameObject *> &v, GameObject *g) {
    if (g && std::find(v.begin(), v.end(), g) == v.end()) v.push_back(g);
  };
  std::vector<GameObject *> chosen;
  GameObject *active = owner_of(active_object());
  if (t.size() > 2) {
    /* The names are the rest of the line, separated by commas (a name has spaces: "Blockout (1)"); one name needs no comma. For the old way - names
     * separated by spaces - a line that names no object as a whole is read word by word. */
    std::string rest;
    for (size_t i = 2; i < t.size(); i++) rest += (i > 2 ? " " : "") + t[i];
    std::vector<std::string> names = split_list(rest, ',');
    if (names.size() == 1 && !scene_->find_by_name(names[0]) && split_ws(names[0]).size() > 1) names = split_ws(names[0]);
    for (const std::string &n : names) {
      GameObject *g = owner_of(scene_->find_by_name(n));
      if (!g) return Log::warn("blockout join: there is no Blockout named '%s' (names are separated by commas)", n.c_str());
      add(chosen, g);
    }
  }
  else
    for (GameObject *g : selected_objects(false)) add(chosen, owner_of(g));
  if (!active || (t.size() <= 2 && std::find(chosen.begin(), chosen.end(), active) == chosen.end())) active = chosen.empty() ? nullptr : chosen.front();
  if (!active) return Log::warn("blockout join [name ...]: select the Blockouts to join (the active one takes the others), or name them");
  std::vector<GameObject *> others;
  for (GameObject *g : chosen)
    if (g != active) others.push_back(g);
  if (others.empty() && t.size() <= 2 && active->get<Blockout>()) {  // (one Blockout selected: everything that touches it, and what touches that)
    std::vector<GameObject *> frontier{active};
    for (size_t i = 0; i < frontier.size(); i++) {
      std::vector<GameObject *> touching;
      blockout_touching(*frontier[i], touching);
      for (GameObject *g : touching)
        if (g != active && std::find(others.begin(), others.end(), g) == others.end()) others.push_back(g), frontier.push_back(g);
    }
    if (others.empty()) return Log::warn("blockout join: '%s' touches no other Blockout - select the Blockouts to join with it, or name them (blockout join <name> ...)", active->name.c_str());
  }
  blockout_join(*active, others);
}

/* The Blockouts `others` joined into `active`: see the header. One undo step. False (with a warning that says why) when nothing was joined. */
bool Editor::blockout_join(GameObject &active, std::vector<GameObject *> others) {
  if (!scene_) return false;
  if (playing_) {
    Log::warn("Join Blockouts: leave Play mode first");
    return false;
  }
  if (edit_mode_) {
    Log::warn("Join Blockouts: leave Edit Mode first");
    return false;
  }
  std::vector<GameObject *> list;
  for (GameObject *o : others)
    if (o && o != &active && std::find(list.begin(), list.end(), o) == list.end()) list.push_back(o);
  std::vector<GameObject *> all{&active};
  all.insert(all.end(), list.begin(), list.end());
  for (const GameObject *g : all)
    if (g->get<BlockoutBaked>()) {
      Log::warn("Join Blockouts: '%s' is baked to a mesh - Unbake first (blockout unbake)", g->name.c_str());
      return false;
    }
  for (const GameObject *g : all)
    if (!g->get<Blockout>()) {
      Log::warn("Join Blockouts: '%s' is not a Blockout", g->name.c_str());
      return false;
    }
  if (list.empty()) {
    Log::warn("Join Blockouts: needs two or more Blockouts - select them, or one that touches another");
    return false;
  }
  for (const GameObject *o : list)
    if (scene_->is_ancestor(o, &active)) {
      Log::warn("Join Blockouts: '%s' holds '%s' in the Hierarchy: the pieces of the outer one cannot go under the inner one - move one out of the other first", o->name.c_str(), active.name.c_str());
      return false;
    }
  for (const GameObject *g : all)  // (a zero or infinite scale axis: the pieces that come in would get places that are not numbers)
    if (!solid_matrix(g->world_matrix())) {
      Log::warn("Join Blockouts: '%s' has a scale or place that is zero or not finite - fix it first", g->name.c_str());
      return false;
    }
  blockout_jobs_wait();  // (the other Blockouts' meshes are adopted before they go; what touches what is read from the pieces)
  std::vector<std::string> apart;
  for (GameObject *o : list) {
    std::vector<GameObject *> touching;
    blockout_touching(*o, touching);
    const bool meets = std::any_of(touching.begin(), touching.end(), [&](GameObject *g) { return std::find(all.begin(), all.end(), g) != all.end(); });
    if (!meets) apart.push_back("'" + o->name + "'");
  }

  Blockout *ab = active.get<Blockout>();
  const Vec3 active_axes = axis_lengths(active.world_matrix());
  std::vector<std::string> differed, dropped;
  size_t pieces_moved = 0, modifiers_lost = 0, material_limits = 0, folded = 0, absorbed = 0, not_finite = 0;
  bool any_turned = false;
  auto differ = [&](bool yes, const char *what) {
    if (yes && std::find(differed.begin(), differed.end(), what) == differed.end()) differed.push_back(what);
  };
  for (GameObject *o : list) {
    /* ADR 0135: the other Blockout's scale goes into its pieces' numbers first, so no piece is sheared by what it moves under. */
    BlockoutScaleSet set;
    blockout_scale_collect({o}, set);
    bool scaled = false;
    /* (Only a scale on the Blockout or on a folder asks for the fold: a piece's own leftover Scale - what ADR 0135 leaves on an oval cylinder - is the
     * piece's, and a join that adds no scale must not rewrite it. Round 501's second review.) */
    for (const BlockoutScaleSet::Entry &e : set.entries) scaled = scaled || (e.role == BlockoutScaleSet::Carrier && off_one(e.local0.scale));
    if (scaled) {
      blockout_scale_apply(*scene_, set, Mat4::identity(), true);
      edit_epoch::bump();
      folded++;
    }
    const Blockout *ob = o->get<Blockout>();
    differ(ob->cell != ab->cell, "Field Cell");
    differ(ob->blend_detail != ab->blend_detail, "Blend Detail");
    differ(ob->texture_scale != ab->texture_scale, "Texture Scale");
    differ(ob->method != ab->method, "Method");
    differ(ob->default_wall != ab->default_wall, "Default Wall");
    /* Where the class materials differ, a piece that had no material of its own for that class keeps its old Blockout's: no face changes colour.
     * An empty path in a piece means "the Blockout's", so an old Blockout's default material cannot be said: those faces take the active one's. */
    std::vector<GameObject *> pieces;
    collect_pieces(*o, pieces);
    for (int k = 0; k < kBlockoutClasses; k++) {
      if (ob->material[k] == ab->material[k]) continue;
      differ(true, "class materials");
      for (GameObject *p : pieces) {
        BlockoutPiece *bp = p->get<BlockoutPiece>();
        if (!bp->has_class(k) || !bp->material[k].empty()) continue;
        if (ob->material[k].empty()) material_limits++;
        else bp->material[k] = ob->material[k];
      }
    }
    for (const auto &c : o->components) modifiers_lost += c->is_modifier();
    pieces_moved += pieces.size();
    /* A room's wall, floor and ceiling and a Blend radius are metres in the Blockout's space: under the active Blockout the same numbers are another
     * length in the world when the two Blockouts' world scales differ. They are scaled by the ratio (B's world axes, after its own scale went into its
     * pieces, over the active one's) so their WORLD size is what it was; an uneven ratio takes the rule ADR 0135's fold uses (a wall: the root of the X and
     * Z factors; floor and ceiling: Y; a blend: the smallest). */
    const Vec3 b_axes = axis_lengths(o->world_matrix());
    const Vec3 ratio(b_axes.x / active_axes.x, b_axes.y / active_axes.y, b_axes.z / active_axes.z);
    const float wall_k = std::sqrt(ratio.x * ratio.z), blend_k = std::min(ratio.x, std::min(ratio.y, ratio.z));
    auto thick = [](float &v, float k) {
      if (std::fabs(k - 1.0f) > 1e-6f && finite_bits(k) && finite_bits(v)) v = std::min(1000.0f, std::max(0.0f, v * k));
    };
    for (GameObject *p : pieces) {
      BlockoutPiece *bp = p->get<BlockoutPiece>();
      if (bp->room) thick(bp->wall, wall_k), thick(bp->floor, ratio.y), thick(bp->ceiling, ratio.y);
      if (bp->blend > 0.0f && finite_bits(blend_k) && std::fabs(blend_k - 1.0f) > 1e-6f && finite_bits(bp->blend)) bp->blend = std::min(100.0f, bp->blend * blend_k);
    }
    for (const auto &c : o->components) {
      const std::string type = c->type_name();
      if (!c->is_modifier() && type != "Blockout" && type != "MeshFilter" && type != "MeshRenderer" && type != "Transform" &&
          std::find(dropped.begin(), dropped.end(), type) == dropped.end())
        dropped.push_back(type);
    }
    const std::vector<GameObject *> kids = o->children;  // (a copy: the list changes as they move)
    for (GameObject *kid : kids) {
      const bool finite = finite_place(*kid);  // (its own place: keeping it rewrites only this object's transform - what is below keeps its numbers either way)
      /* In B's order, after the active Blockout's own children, the place in the world kept. A place that is not a number cannot be kept: that object
       * keeps its own numbers (no NaN reaches the others) and is counted. */
      const Vec3 scale0 = kid->local().scale;
      scene_->set_parent(kid, &active, -1, finite);
      if (!finite) {
        not_finite++;
        continue;
      }
      /* The split of the matrix may leave 0.99999994 where the piece had 1 (or put a mirror's sign on another axis and turn the piece to match: that sign
       * is kept). Where an axis is the old length to within rounding, it gets the old length back - with the sign the split chose, never the old one. */
      Transform fixed = kid->local();
      for (int a = 0; a < 3; a++)
        if (std::fabs(std::fabs(fixed.scale[a]) - std::fabs(scale0[a])) < 1e-5f) fixed.scale[a] = std::copysign(std::fabs(scale0[a]), fixed.scale[a]);
      if (fixed.scale.x != kid->local().scale.x || fixed.scale.y != kid->local().scale.y || fixed.scale.z != kid->local().scale.z) kid->set_local_scale(fixed.scale);
      bool rescaled = false;  // (the move put a scale on it that it did not have: the two Blockouts' frames differ in size)
      for (int a = 0; a < 3; a++) rescaled = rescaled || std::fabs(fixed.scale[a]) != std::fabs(scale0[a]);
      /* The active Blockout's scale (and its parents') now stands over the piece: what the split of the matrix left as the piece's own Scale goes into its
       * numbers as Apply Scale to Size does (a mirror's sign stays; a mesh piece and a plain object keep theirs), so a piece carries its size in its
       * numbers (ADR 0135). Under an even scale every box and wall keeps its world size and the Scale is 1 1 1; under an uneven one a piece square to
       * the Blockout's axes does too, a turned one is sheared by it, as the Blockout's own turned pieces are. */
      BlockoutScaleSet fold;
      blockout_scale_collect({kid}, fold);
      if (rescaled) {  // (not for a Scale the piece already had: with no difference between the frames the join leaves every number as it was)
        blockout_scale_apply(*scene_, fold, Mat4::identity(), true);
        edit_epoch::bump();
        absorbed++;
      }
      any_turned = any_turned || any_turned_below(*kid);
    }
    blockout_unwant(o->id);
    scene_->destroy(o);
  }

  expanded_.insert(active.id);
  select(active.id);
  update_blockouts(true);
  mark_changed("Join Blockouts");
  Log::info("Joined %zu Blockout%s into '%s': %zu pieces moved under it, each in its place in the world%s", list.size(), list.size() == 1 ? "" : "s", active.name.c_str(), pieces_moved,
            folded ? strprintf(" (%zu scaled one%s went into its pieces' numbers first)", folded, folded == 1 ? "" : "s").c_str() : "");
  if (absorbed) Log::info("Join Blockouts: the scale of '%s' (and of what is above it) went into the numbers of %zu moved branch(es): their size is in their numbers, Scale 1 1 1", active.name.c_str(), absorbed);
  if (!apart.empty()) {
    std::string names;
    for (const std::string &n : apart) names += (names.empty() ? "" : ", ") + n;
    Log::info("Join Blockouts: %s did not touch the others - joined as asked, they stay apart in the result", names.c_str());
  }
  if (!differed.empty()) {
    std::string names;
    for (const std::string &n : differed) names += (names.empty() ? "" : ", ") + n;
    Log::info("Join Blockouts: %s differed - '%s' settings are used", names.c_str(), active.name.c_str());
  }
  if (material_limits)
    Log::info("Join Blockouts: %zu piece class(es) had the default material through their old Blockout: they take '%s' class material now (a piece's empty override means the Blockout's)", material_limits, active.name.c_str());
  if (modifiers_lost) Log::info("Join Blockouts: %zu modifier(s) on the removed Blockouts are gone with them; '%s' modifiers, if it has any, now apply to the pieces that came in", modifiers_lost, active.name.c_str());
  if (!dropped.empty()) {
    std::string names;
    for (const std::string &n : dropped) names += (names.empty() ? "" : ", ") + n;
    Log::info("Join Blockouts: the removed Blockouts' other components (%s) are deleted with them", names.c_str());
  }
  if (not_finite) Log::warn("Join Blockouts: %zu object(s) had a place that is not finite: they moved under '%s' keeping their own numbers", not_finite, active.name.c_str());
  std::vector<BlockoutUnevenScale> uneven;
  blockout_uneven_scales(active, uneven);
  if (any_turned && !uneven.empty())
    Log::warn("Join Blockouts: '%s' is scaled unevenly: turned pieces are sheared - the moved pieces that are turned share that (Apply Scale to Pieces, blockout applyscale, takes the scale into the pieces)", active.name.c_str());
  return true;
}

}  // namespace bl

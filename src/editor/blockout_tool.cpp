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
#include <functional>

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

/* ===================================================================== */
/* A door or a window drawn on a wall (round 513, task 0495)              */
/* ===================================================================== */

/* RoomTool::piece for the two openings (editor.h lists 0, 3, 4, 5): negative, so that no test of `piece >= 3` elsewhere takes one for a
 * wedge, stairs or an arch. With Solid on, as the other pieces. */
constexpr int kPieceDoor = -1, kPieceWindow = -2;
constexpr float kMinOpening = 0.2f;  // metres: the smallest door or window the tool draws, each way
constexpr float kSillSlack = 0.05f;  // metres: how far under its floor a window's drag may start (the sill is raised to the floor)
constexpr float kDoorReach = 0.6f;   // metres: how far above its floor the drag of a door may start (the door's bottom snaps down to the floor)

int opening_of(int piece) { return piece == kPieceDoor ? 1 : piece == kPieceWindow ? 2 : 0; }  // 1 door, 2 window, 0 neither

bool finite3(Vec3 v) { return finite_bits(v.x) && finite_bits(v.y) && finite_bits(v.z); }

const char *const kNoWall = "A door or window goes in a wall of a Blockout: press on a room's wall or on the side of a piece (the ground and other meshes have none).";

/* A wall face of a Blockout, as the tool reads it in the Blockout's space (a turned Blockout is not special: its up is the Blockout's own +Y). */
struct OpeningWall {
  Vec3 point, normal, u;  // a point of the face; its normal out of the matter (horizontal); the axis along it, to the right of a viewer facing it
  float u_lo = 0, u_hi = 0, v_lo = 0, v_hi = 0;  // the face's extent along u and up (y): the rectangle a drag is held to
  float end_inset = 0, top_inset = 0;  // a room's wall and ceiling thickness: a drag is held that far inside the face, so a jamb never cuts the wall that meets this one
  uint64_t piece_id = 0;               // the piece that made the face (an id: the scene may be swapped by an undo; the floor under it is blockout_opening_feet's to say)
};

/* What the planning functions need from the editor: the scene, who owns the wall cache, the lean a wall may have, and the floor an opening of a width stands on
 * (Editor::blockout_opening_feet, passed in because it is a member). */
struct OpeningEnv {
  Scene &scene;
  const void *owner;
  bool cached;  // may the wall kept by the drag in progress stand for the one at this point (only that drag's own preview and release)
  float lean;  // sin of the greatest lean from vertical (Editor::kOpeningWallLean)
  std::function<bool(const BlockoutHit &, float, float &)> feet;
};

struct WallTri {
  float u[3], v[3];
  float u_lo, u_hi, v_lo, v_hi;
  bool taken = false;
};

/* The wall face of `bo` at a world point whose normal is `world_n`: false with `why` (a sentence saying what is wrong and what to do) when it is a floor,
 * a ceiling, a sloped face, the inside of an opening, or no face of the Blockout at all. The face is found in the Blockout's mesh: the coplanar
 * triangles that join the one under the point make its rectangle. */
bool find_opening_wall(const OpeningEnv &env, GameObject &bo, Vec3 world_p, Vec3 world_n, OpeningWall &w, std::string &why) {
  Scene &scene = env.scene;
  const Blockout *b = bo.get<Blockout>();
  const MeshFilter *mf = bo.get<MeshFilter>();
  const Mat4 inv = bo.world_matrix().inverse();
  const Vec3 p = inv.point(world_p);
  Vec3 n = inv.dir(world_n);
  if (!b || !mf || !mf->mesh || !finite3(p) || !finite3(n) || !(length(n) > 1e-6f)) return why = kNoWall, false;
  n = normalize(n);
  if (std::fabs(n.y) > 0.9f) {
    why = n.y > 0.0f ? "A door or window goes in a wall, and this face is a floor: drag on an upright wall instead."
                     : "A door or window goes in a wall, and this face is a ceiling: drag on an upright wall instead.";
    return false;
  }
  if (std::fabs(n.y) > env.lean) {  // (as far from vertical as blockout_stand_opening holds the Door and Window buttons to)
    why = strprintf("That wall leans %.1f degrees: a door or window stands in an upright wall - drag on a vertical face instead.", (double)(std::asin(std::min(1.0f, std::fabs(n.y))) * 57.29578f));
    return false;
  }
  const Vec3 face_n = n;  // (the face's own normal: its triangles are coplanar to it, a wall leaning a degree or two is not to its horizontal part)
  n.y = 0.0f;
  n = normalize(n);
  const Vec3 u = normalize(cross(Vec3(0.0f, 1.0f, 0.0f), n));
  const Mesh &m = *mf->mesh;
  const BlockoutState &s = b->state;
  if (s.faces_mesh != &m || s.faces_version != m.version || s.faces.piece.size() != m.face_count())
    return why = "The Blockout is still being built: try again in a moment.", false;
  const RenderMesh &rm = m.render_mesh();
  const float pu = dot(p, u), pv = p.y;
  std::vector<WallTri> tris;
  uint32_t face = UINT32_MAX;
  bool face_facing = false;
  float lo_u = 0, hi_u = 0, lo_v = 0, hi_v = 0;
  for (size_t t = 0; t < rm.tri_count() && t < rm.tri_face.size(); t++) {
    const Vec3 a = rm.positions[rm.indices[t * 3]], c1 = rm.positions[rm.indices[t * 3 + 1]], c2 = rm.positions[rm.indices[t * 3 + 2]];
    Vec3 tn = cross(c1 - a, c2 - a);
    if (!(length(tn) > 1e-12f)) continue;
    tn = normalize(tn);
    if (std::fabs(dot(tn, face_n)) < 0.995f || std::fabs(dot(a - p, face_n)) > 5e-3f) continue;  // (on the face's plane only)
    WallTri wt;
    const Vec3 c[3] = {a, c1, c2};
    for (int k = 0; k < 3; k++) wt.u[k] = dot(c[k], u), wt.v[k] = c[k].y;
    wt.u_lo = std::min({wt.u[0], wt.u[1], wt.u[2]}), wt.u_hi = std::max({wt.u[0], wt.u[1], wt.u[2]});
    wt.v_lo = std::min({wt.v[0], wt.v[1], wt.v[2]}), wt.v_hi = std::max({wt.v[0], wt.v[1], wt.v[2]});
    /* The press is in this triangle when it is on the inner side of all three edges, to a hair (the press is on a shared edge as often as not). */
    float side[3];
    for (int k = 0; k < 3; k++) {
      const int j = (k + 1) % 3;
      side[k] = (wt.u[j] - wt.u[k]) * (pv - wt.v[k]) - (wt.v[j] - wt.v[k]) * (pu - wt.u[k]);
    }
    const float eps = 2e-3f * (std::fabs(wt.u_hi - wt.u_lo) + std::fabs(wt.v_hi - wt.v_lo) + 1.0f);
    const bool inside = (side[0] >= -eps && side[1] >= -eps && side[2] >= -eps) || (side[0] <= eps && side[1] <= eps && side[2] <= eps);
    if (inside) {
      const bool facing = dot(tn, face_n) > 0.0f;  // (the piece named is the one whose face looks at the viewer, where two lie on one plane)
      if (face == UINT32_MAX) lo_u = wt.u_lo, hi_u = wt.u_hi, lo_v = wt.v_lo, hi_v = wt.v_hi;
      else lo_u = std::min(lo_u, wt.u_lo), hi_u = std::max(hi_u, wt.u_hi), lo_v = std::min(lo_v, wt.v_lo), hi_v = std::max(hi_v, wt.v_hi);
      if (face == UINT32_MAX || (facing && !face_facing)) face = rm.tri_face[t], face_facing = facing;
      wt.taken = true;
    }
    tris.push_back(wt);
  }
  if (face == UINT32_MAX || face >= s.faces.piece.size()) return why = kNoWall, false;
  /* The face is every coplanar triangle that touches what is found so far (a wall with a doorway cut in it is one face round the hole). */
  for (int round = 0; round < 64; round++) {
    bool grew = false;
    for (WallTri &wt : tris) {
      if (wt.taken || wt.u_hi < lo_u - 1e-3f || wt.u_lo > hi_u + 1e-3f || wt.v_hi < lo_v - 1e-3f || wt.v_lo > hi_v + 1e-3f) continue;
      wt.taken = grew = true;
      lo_u = std::min(lo_u, wt.u_lo), hi_u = std::max(hi_u, wt.u_hi), lo_v = std::min(lo_v, wt.v_lo), hi_v = std::max(hi_v, wt.v_hi);
    }
    if (!grew) break;
  }
  const GameObject *owner = scene.find(blockout_piece_owner(s.faces.piece[face]));
  const BlockoutPiece *bp = owner ? owner->get<BlockoutPiece>() : nullptr;
  if (!bp) return why = kNoWall, false;
  if (bp->opening) return why = "That is the inside of a door or window: draw on the wall round it.", false;
  if (!(hi_u > lo_u) || !(hi_v > lo_v)) return why = kNoWall, false;
  w.point = p, w.normal = n, w.u = u;
  w.u_lo = lo_u, w.u_hi = hi_u, w.v_lo = lo_v, w.v_hi = hi_v;
  /* The walls that meet this one are in the way only from outside: on the room's inner face the drag may reach the air's corner and ceiling (the normal points into the air). */
  const Vec3 centre = blockout_piece_matrix(*owner, bo).point(Vec3(0.0f));
  const bool outer = bp->is_room() && finite3(centre) && dot(n, p - centre) > 0.0f;
  w.end_inset = outer && finite_bits(bp->wall) ? std::max(0.0f, std::min(10.0f, bp->wall)) : 0.0f;
  w.top_inset = outer && finite_bits(bp->ceiling) ? std::max(0.0f, std::min(10.0f, bp->ceiling)) : 0.0f;
  w.piece_id = owner->id;
  return true;
}

/* One wall at a time is kept while it is dragged on: the preview asks every frame and the release asks again, and the wall's triangle scan is not done twice for the same press
 * (a different press, Blockout, mesh or version asks the scan again). Only a found wall is kept. */
struct WallCache {
  const void *owner = nullptr;
  uint64_t bo = 0;
  const Mesh *mesh = nullptr;
  uint64_t version = 0;
  Vec3 at, normal;
  OpeningWall wall;
};
WallCache g_wall_cache;
bool g_release_uses_cache = false;  // set while the tool's release runs the console words: that drag's own wall may be used
void drop_wall_cache() { g_wall_cache = WallCache(); }

bool wall_at(const OpeningEnv &env, GameObject &bo, Vec3 at, Vec3 normal, OpeningWall &w, std::string &why) {
  const MeshFilter *mf = bo.get<MeshFilter>();
  const Mesh *mesh = mf && mf->mesh ? mf->mesh.get() : nullptr;
  WallCache &c = g_wall_cache;
  if (env.cached && mesh && c.owner == env.owner && c.bo == bo.id && c.mesh == mesh && c.version == mesh->version && length(c.at - at) < 1e-6f && length(c.normal - normal) < 1e-6f) {
    w = c.wall;
    return true;
  }
  return find_opening_wall(env, bo, at, normal, w, why);  // (nothing is kept here: only a press keeps the wall, for its own drag)
}

/* The wall's axes in the world, for a normal read from the surface: the Blockout's up (not the room's - the cause of the #405 fix Pass 23
 * is making to the Door and Window buttons' placement), the right of a viewer facing the wall. */
bool wall_axes(const GameObject &bo, Vec3 world_n, Vec3 &u, Vec3 &up) {
  const Mat4 m = bo.world_matrix();
  up = m.dir(Vec3(0.0f, 1.0f, 0.0f));
  if (!finite3(up) || !(length(up) > 1e-6f)) return false;
  up = normalize(up);
  Vec3 flat = world_n - up * dot(world_n, up);
  if (!finite3(flat) || !(length(flat) > 0.3f)) return false;
  flat = normalize(flat);
  u = normalize(cross(up, flat));
  return finite3(u);
}

/* What a drag on a wall makes: the piece's numbers in the Blockout's units, where its wall point is, and the rectangle the preview draws (world). */
struct OpeningPlan {
  bool ok = false;
  std::string why;    // when !ok: the sentence for the status line (what is wrong, what to do)
  std::string brief;  // and the short form beside the mouse
  std::string note;   // a drag that ran past the wall's edge, or a sill raised to the floor: said once the piece is made
  OpeningWall wall;
  float feet = 0;     // the height the opening stands on (blockout_opening_feet: the one place the floor under a wall is decided)
  float width = 0, head = 0, sill = 0;  // the piece's numbers: Head and Sill are measured from the feet
  Vec3 centre;        // Blockout space: a point of the wall plane, at the rectangle's middle along the wall
  Vec3 rect[4];       // the dragged rectangle on the wall, in the world
  bool have_rect = false;
  float width_m = 0, height_m = 0;
};

/* The opening a drag from the world point `a` to `b` on the wall `n` (the surface's normal) at `a` makes. `kind`: 1 door, 2 window. Everything is read
 * in the Blockout's space; a Blockout's even scale turns its units into metres for the sizes the user is told. The drag is held to the wall's face, inside the
 * thickness of the walls that meet it at each end and under the ceiling. The floor is `env.feet`'s. */
OpeningPlan plan_opening(const OpeningEnv &env, GameObject &bo, int kind, Vec3 a, Vec3 b, Vec3 n) {
  OpeningPlan plan;
  const Mat4 m = bo.world_matrix();
  const float scale = length(m.dir(Vec3(1.0f, 0.0f, 0.0f)));
  const char *not_finite = "That drag has no size the Blockout can hold (the numbers are not finite): press and drag again on the wall.";
  if (!finite3(a) || !finite3(b) || !finite3(n) || !finite_bits(scale) || !(scale > 1e-6f)) {
    plan.why = not_finite, plan.brief = "not finite";
    return plan;
  }
  if (!wall_at(env, bo, a, n, plan.wall, plan.why)) {
    plan.brief = "no wall here";
    return plan;
  }
  const OpeningWall &w = plan.wall;
  const Mat4 inv = m.inverse();
  const Vec3 la = inv.point(a), lb = inv.point(b);
  const float lim = 1e-3f;
  /* The room: held inside its end walls and under its ceiling, when the face is wider and taller than that (a face too narrow for it is held to its own edges). */
  const bool in_u = w.u_hi - w.u_lo > 2.0f * w.end_inset + 4.0f * lim, in_v = w.v_hi - w.v_lo > w.top_inset + 4.0f * lim;
  const float u_min = w.u_lo + (in_u ? w.end_inset : 0.0f), u_max = w.u_hi - (in_u ? w.end_inset : 0.0f), v_min = w.v_lo, v_max = w.v_hi - (in_v ? w.top_inset : 0.0f);
  auto held = [&](float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); };
  const float ua = held(dot(la, w.u), u_min, u_max), ub = held(dot(lb, w.u), u_min, u_max);
  const float va = held(la.y, v_min, v_max), vb = held(lb.y, v_min, v_max);
  const bool clamped = std::fabs(ua - dot(la, w.u)) > lim || std::fabs(ub - dot(lb, w.u)) > lim || std::fabs(va - la.y) > lim || std::fabs(vb - lb.y) > lim;
  const float lo_u = std::min(ua, ub), hi_u = std::max(ua, ub), lo_v = std::min(va, vb), hi_v = std::max(va, vb);
  auto on_wall = [&](float uu, float vv) { return m.point(w.point + w.u * (uu - dot(w.point, w.u)) + Vec3(0.0f, vv - w.point.y, 0.0f)); };
  auto fill_rect = [&](float bottom) {
    plan.rect[0] = on_wall(lo_u, bottom), plan.rect[1] = on_wall(hi_u, bottom), plan.rect[2] = on_wall(hi_u, hi_v), plan.rect[3] = on_wall(lo_u, hi_v);
    plan.have_rect = true;
    plan.width_m = (hi_u - lo_u) * scale, plan.height_m = (hi_v - bottom) * scale;
  };
  fill_rect(lo_v);
  plan.centre = w.point + w.u * (0.5f * (lo_u + hi_u) - dot(w.point, w.u));
  BlockoutHit hit;
  hit.piece = w.piece_id, hit.point = plan.centre, hit.normal = w.normal;
  if (!env.feet || !env.feet(hit, hi_u - lo_u, plan.feet) || !finite_bits(plan.feet)) {
    plan.why = "A door or window can't stand on this wall: its piece is flat or its numbers are not finite.";
    plan.brief = "no floor here";
    return plan;
  }
  const float feet = plan.feet;
  const char *name = kind == 1 ? "door" : "window";
  const float least = kMinOpening / scale, top = hi_v;
  if (kind == 1 && (lo_v - feet) * scale > kDoorReach) {
    plan.why = strprintf("A door stands on the floor, and this drag starts %.2f m above it: start the drag at the floor, or choose Window for an opening up the wall.", (lo_v - feet) * scale);
    plan.brief = "a door starts at the floor";
    return plan;
  }
  /* A window's sill is the drag's lower edge. Below the floor it cannot be: a few centimetres under are raised to it (and the status says so), more is refused. */
  const float under = feet - lo_v;
  if (kind == 2 && under * scale > kSillSlack) {
    plan.why = strprintf("The floor under this wall is %.2f m above the lower edge of the drag: start the window at the floor or higher up the wall.", under * scale);
    plan.brief = "starts under the floor";
    return plan;
  }
  const float bottom = kind == 1 ? feet : std::max(lo_v, feet);
  if (hi_u - lo_u < least || top - bottom < least) {
    const bool floor_short = kind == 1 && top - feet < least && hi_u - lo_u >= least;
    plan.why = floor_short ? strprintf("A door has to reach up from the floor, and this drag ends %.2f m above it: drag up the wall, at least %.2f m.", (top - feet) * scale, kMinOpening)
                           : strprintf("Too small: a %s is at least %.2f x %.2f m and this drag is %.2f x %.2f m - drag a larger rectangle on the wall.", name, kMinOpening, kMinOpening,
                                       (hi_u - lo_u) * scale, std::max(0.0f, top - bottom) * scale);
    plan.brief = floor_short ? "ends below a door's reach" : strprintf("too small (least %.2f m)", kMinOpening);
    return plan;
  }
  fill_rect(bottom);
  plan.width = hi_u - lo_u, plan.sill = kind == 1 ? 0.0f : bottom - feet, plan.head = top - feet;
  if (clamped) plan.note = "the drag ran past the wall's edge and was held to the wall";
  if (kind == 2 && under > lim) plan.note += std::string(plan.note.empty() ? "" : "; ") + strprintf("the sill was raised %.0f cm to the floor", (double)(under * scale * 100.0f));
  plan.ok = finite_bits(plan.width) && finite_bits(plan.head) && finite_bits(plan.sill) && finite3(plan.centre);
  if (!plan.ok) plan.why = not_finite, plan.brief = "not finite";
  return plan;
}

/* The grid, in the wall's own frame: the two coordinates in the wall (along it and up it) rounded to `step`. (room_snap_point snaps only a
 * surface that lies square to a world axis; a turned room's wall or a turned Blockout's does not.) */
Vec3 snap_in_wall(Vec3 p, Vec3 u, Vec3 up, float step) {
  const float su = dot(p, u), sv = dot(p, up);
  return p + u * (std::round(su / step) * step - su) + up * (std::round(sv / step) * step - sv);
}

/* The console's words for a drag, which the tool sends when the button is let go (so that what the tool makes is what `blockout draw door` makes). */
std::vector<std::string> opening_words(int kind, Vec3 at, Vec3 normal, float du, float dv) {
  auto f = [](float v) { return strprintf("%.9g", (double)v); };
  return {"blockout", "draw", kind == 1 ? "door" : "window", "auto", "at", f(at.x), f(at.y), f(at.z), "normal", f(normal.x), f(normal.y), f(normal.z), "size", f(du), f(dv), "0"};
}

/* A press for a door or window: on a wall face of `bo` (the Blockout under the pointer, nullptr: none) at the world point `p` with the surface's normal `n`.
 * The footprint then lies in the wall: width along it, height up the Blockout's up, held inside the face (a press snapped past the face's edge is not on it).
 * The wall found is kept for the preview and the release (wall_at). `step` the grid, 0 for none. False with `why`: nothing began. */
template <class Tool>
bool begin_opening_drag(Tool &r, const OpeningEnv &env, GameObject *bo, Vec3 p, Vec3 n, float step, std::string &why) {
  OpeningWall wall;
  if (!bo) return why = kNoWall, false;
  if (!find_opening_wall(env, *bo, p, n, wall, why)) return false;
  const Mat4 m = bo->world_matrix();
  const Vec3 wn = normalize(m.dir(wall.normal));
  Vec3 u, up;
  if (!wall_axes(*bo, wn, u, up)) return why = kNoWall, false;
  Vec3 at = step > 0.0f ? snap_in_wall(p, u, up, step) : p;
  const Vec3 l = m.inverse().point(at);
  const float inset = 1e-3f;
  auto inside = [&](float v, float lo, float hi) { return hi - lo > 4.0f * inset ? std::max(lo + inset, std::min(hi - inset, v)) : 0.5f * (lo + hi); };
  const float uu = inside(dot(l, wall.u), wall.u_lo, wall.u_hi), vv = inside(l.y, wall.v_lo, wall.v_hi);
  at = m.point(wall.point + wall.u * (uu - dot(wall.point, wall.u)) + Vec3(0.0f, vv - wall.point.y, 0.0f));
  if (!finite3(at)) return why = kNoWall, false;
  r.normal = wn, r.u = u;
  r.origin = r.corner = at;
  r.surface = bo->id;
  r.height = 0.0f, r.typed.clear(), r.points.clear();
  r.stage = 1;
  const MeshFilter *mf = bo->get<MeshFilter>();
  if (mf && mf->mesh) {
    WallCache &c = g_wall_cache;
    c.owner = env.owner, c.bo = bo->id, c.mesh = mf->mesh.get(), c.version = mf->mesh->version, c.at = at, c.normal = wn, c.wall = wall;
  }
  return true;
}

/* Whether a choice of piece turns a door or window on or off: a footprint in progress is for the piece that began it and is dropped (stage 0, no corners) - a
 * click would make a piece of the other kind from it. */
template <class Tool>
struct DropOnSwitch {
  Tool &r;
  int was;
  explicit DropOnSwitch(Tool &t) : r(t), was(t.solid ? opening_of(t.piece) : 0) {}
  ~DropOnSwitch() {
    if ((r.solid ? opening_of(r.piece) : 0) == was) return;
    r.stage = 0, r.typed.clear(), r.points.clear();
    drop_wall_cache();
  }
};

/* The tool's Piece choice by its word: a room, a solid of the footprint's shape, or a wedge, stairs or an arch (whose footprint is
 * the rectangle), or (round 513) a door or window. False: no such word. */
template <class Tool>
bool set_draw_piece(const std::string &word, Tool &r) {
  DropOnSwitch<Tool> drop(r);
  const int piece = word == "wedge" ? 3 : word == "stairs" ? 4 : word == "arch" ? 5 : word == "door" ? kPieceDoor : word == "window" ? kPieceWindow : 0;
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

int Editor::room_tool_shape_for_test() const { return opening_of(room_.solid ? room_.piece : 0) ? 9 + opening_of(room_.piece) : blockout_draw_shape(room_.solid, room_.circle, room_.polyline, room_.piece); }

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
  if (hit.object->get<Blockout>()) return hit.object->id;
  /* Round 547: a window's or door's Frame and Glass are objects of their own; a face of one is the surface of the Blockout its opening stands in. */
  const BlockoutLeaf *part = hit.object->get<BlockoutLeaf>();
  const GameObject *opening = part && hit.object->parent && hit.object->parent->get<BlockoutPiece>() ? hit.object->parent : nullptr;
  const GameObject *owner = opening ? blockout_of_piece(*opening) : nullptr;
  return owner ? owner->id : 0;
}

void Editor::set_room_tool(bool on) {
  if (on == room_.active) return;
  if (!on) {
    room_.active = false, room_.stage = 0, room_.typed.clear(), room_.points.clear();
    drop_wall_cache();
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
  if (room_.solid && opening_of(room_.piece)) Log::info("Room tool: drag a rectangle on a wall of a Blockout, and the %s stands in it  |  Esc leaves", opening_of(room_.piece) == 1 ? "door" : "window");
  else if (room_.polyline) Log::info("Room tool: click the corners of an outline on the ground or a face, close it on the first corner or with Enter, then move the mouse for the height and click  |  Esc leaves");
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
    drop_wall_cache();
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
    drop_wall_cache();
    return true;
  }
  /* Round 496: R turns the piece's direction a quarter round the surface's normal while one is in progress (elsewhere R keeps its keymap meaning;
   * Shift+R is the tool's own switch). Only a wedge, stairs and an arch face a way; the choice is kept for the next piece of that shape. */
  const bool plain_r = !in.ctrl() && !in.shift() && !in.alt();
  if ((r.stage == 1 || r.stage == 2) && plain_r && in.key_pressed[platform::KEY_R] && !u.wants_keyboard()) {
    in.key_pressed[platform::KEY_R] = false;
    if (r.solid && opening_of(r.piece)) Log::info("R does nothing for a door or window: it turns with its wall (draw it on another wall to face the other way).");
    else if (r.solid && !r.circle && !r.polyline && r.piece >= 3) r.turns[r.piece - 3] = (r.turns[r.piece - 3] + 1) & 3;
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
    /* Round 495: a piece drawn on a face of a Blockout is that Blockout's - the face under the press itself (the ground may lie
     * before it, and then the surface is the ground). */
    SceneHit under;
    const bool face = scene_raycast(*scene_, ray.origin, normalize(ray.dir), 1e5f, under) && under.object && under.object->get<Blockout>() &&
                      length(under.point - p) <= 1e-3f * (1.0f + length(p));
    if (r.solid && opening_of(r.piece)) {
      /* Round 513: a door or window starts on a wall face of a Blockout and says why when the press is anywhere else. */
      std::string why;
      const OpeningEnv env{*scene_, &room_, false, kOpeningWallLean, nullptr};  // (the press needs no floor)
      if (!begin_opening_drag(r, env, face ? scene_->find(under.object->id) : nullptr, p, n, u.in.ctrl() ? 0.0f : std::max(1e-3f, snap_move_), why)) Log::warn("%s", why.c_str());
      return true;
    }
    r.normal = n;
    r.u = surface_axis(n);
    r.origin = r.corner = room_snap_point(p, n);
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
    const int opening = r.solid ? opening_of(r.piece) : 0;
    const Vec3 up = cross(r.normal, r.u);  // (an opening's: the Blockout's up; the footprint of any other piece has no up)
    if (ray_plane(ray, r.origin, r.normal, t)) {
      const Vec3 on_plane = ray.origin + ray.dir * t;
      r.corner = !opening ? room_snap_point(on_plane, r.normal) : in.ctrl() ? on_plane : snap_in_wall(on_plane, r.u, up, std::max(1e-3f, snap_move_));
    }
    u.redraw = true;
    if (in.down[0]) return true;
    if (opening) {
      /* Let go: the console's `blockout draw door|window` makes it, from the same numbers - one way to make one, and what the tool makes is what a script makes. */
      const float du = dot(r.corner - r.origin, r.u), dv = dot(r.corner - r.origin, up);
      const Vec3 origin = r.origin, normal = r.normal;
      r.stage = 0, r.typed.clear();
      if (!(std::fabs(du) >= 1e-3f && std::fabs(dv) >= 1e-3f)) {
        drop_wall_cache();
        Log::warn("A door or window is drawn by dragging a rectangle on a wall: press at one corner, drag to the opposite corner and let go.");
        return true;
      }
      g_release_uses_cache = true;  // (this drag's own wall, kept at its press; any other caller of the words finds the wall again)
      room_command(opening_words(opening, origin, normal, du, dv));
      g_release_uses_cache = false;
      drop_wall_cache();
      return true;
    }
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
  if (r.solid && r.piece < 0) {  // (backstop: a door or window is no piece room_make draws; the choice of one drops a footprint in progress, so this is not reached)
    r.stage = 0, r.points.clear();
    Log::warn("A door or window is drawn by dragging a rectangle on a wall: choose a box, room or other shape to draw that.");
    return true;
  }
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
  const int opening = r.solid ? opening_of(r.piece) : 0;
  if (r.stage == 0 && scene_hovered_ && !opening) {  // (an opening starts on a wall, not on the ground: no ground marker for it)
    const Ray ray = scene_r3d_.screen_ray((float)(u.in.mx - view.x), (float)(u.in.my - view.y));
    float t;
    Vec2 s;
    if (ray_plane(ray, Vec3(0.0f), Vec3(0.0f, 1.0f, 0.0f), t) && project_to_view(view, room_snap_point(ray.origin + ray.dir * t, Vec3(0.0f, 1.0f, 0.0f)), s)) {
      u.canvas.fill_circle(s.x, s.y, (float)u.px(5.5f), 0xFF000000);
      u.canvas.fill_circle(s.x, s.y, (float)u.px(4), yellow);
    }
  }
  if (opening && r.stage > 0) {
    /* Round 513: the opening's rectangle in the wall, in a Subtract's colour (it cuts), with its size beside the mouse; a drag that would be refused says why. */
    GameObject *bo = scene_->find(r.surface);
    const OpeningEnv env{*scene_, &room_, true, kOpeningWallLean, [this, bo](const BlockoutHit &h, float w, float &y) { return bo && blockout_opening_feet(*bo, h, w, y); }};
    const OpeningPlan plan = bo ? plan_opening(env, *bo, opening, r.origin, r.corner, r.normal) : OpeningPlan();
    const uint32_t cut = (blockout_operation_colour(1, false) & 0x00FFFFFFu) | 0xFF000000u;  // (as room_draw_preview takes the colour of an operation)
    if (plan.have_rect)
      for (int k = 0; k < 4; k++) line(plan.rect[k], plan.rect[(k + 1) % 4], cut), room_.preview_lines++;
    label = strprintf("%.2f x %.2f m  %s", plan.width_m, plan.height_m, opening == 1 ? "Door" : "Window");
    if (!plan.ok) label += "  - " + (plan.brief.empty() ? std::string("no wall here") : plan.brief);
    else if (!plan.note.empty()) label += "  (held to the wall)";
    const int tw = u.font.text_width(label) + u.px(12);
    const Recti box{u.in.mx + u.px(16), u.in.my + u.px(12), tw, u.row_h()};
    u.canvas.fill_round_rect(box, u.px(3), Color::hex(0x202020, 230));
    u.label(box, label, u.theme.text_bright, ui::Align::Center);
  }
  else if (r.stage == 3) {
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
  /* Round 513: a door or window chosen as the tool's shape is no piece room_make draws: the console's footprint commands below make the plain solid. */
  struct KeepPiece {
    int &slot;
    int piece;
    explicit KeepPiece(int &s) : slot(s), piece(s) {
      if (s < 0) s = 0;
    }
    ~KeepPiece() { slot = piece; }
  } keep_piece(room_.piece);
  if (t.size() > 1 && to_lower(t[1]) == "draw") {
    /* blockout draw <piece> <operation> at <x y z> normal <x y z> size <du dz height>: what the tool's drags make on that surface
     * point, whatever the tool's own options are (they are put back). A cylinder: du its radius. */
    const char *usage = "blockout draw room|box|cylinder|wedge|stairs|arch add|subtract|intersect|auto at <x y z> normal <x y z> size <du dz height> "
                        "(metres; a cylinder: du its radius; on a face of a Blockout the piece is that Blockout's) | "
                        "blockout draw door|window auto at <x y z> normal <x y z> size <width height 0> (a rectangle on a wall of a Blockout: from the point, width to the right "
                        "of a viewer facing the wall, height up; the wall's normal is the face's)";
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
    if (piece == "door" || piece == "window") {
      /* Round 513: what the Draw Piece tool makes when a rectangle is dragged on a wall - the same numbers, the same piece. `at` is one corner of the rectangle
       * on the wall's face; the size runs along the wall to the right of a viewer facing it and up the Blockout's up (negative: the other way). */
      if (op != "auto") return Log::warn("%s", usage);  // (an opening has no operation: it always cuts)
      const int kind = piece == "door" ? 1 : 2;
      GameObject *bo = scene_->find(blockout_at_surface(*scene_, p, normalize(n)));
      if (!bo) return Log::warn("blockout draw %s: %s", piece.c_str(), kNoWall);
      if (const Blockout *b = bo->get<Blockout>(); b && !b->state.hash_valid) blockout_jobs_wait(bo->id);
      Vec3 across, up;
      const bool axes = wall_axes(*bo, n, across, up);  // (a floor's or a ceiling's normal has none: the plan then says what that face is)
      const OpeningEnv env{*scene_, &room_, g_release_uses_cache, kOpeningWallLean, [this, bo](const BlockoutHit &h, float w, float &y) { return blockout_opening_feet(*bo, h, w, y); }};
      const OpeningPlan plan = plan_opening(env, *bo, kind, p, axes ? p + across * v[6] + up * v[7] : p, n);
      if (!plan.ok) return Log::warn("%s", plan.why.c_str());
      if (!axes) return Log::warn("blockout draw %s: %s", piece.c_str(), kNoWall);
      const std::string name = unique_object_name(kind == 1 ? "Door" : "Window");
      GameObject *g = scene_->create(name, bo);
      BlockoutPiece *bp = g->add<BlockoutPiece>();
      bp->shape = 0, bp->operation = 1, bp->opening = kind;
      const int style = kind == 1 ? blockout_door_style_ : blockout_window_style_;
      if (style != 0) blockout_opening_set_style(*bp, style);
      bp->width = plan.width, bp->sill = plan.sill, bp->head = plan.head;  // (after the style: a Round window's own Head is the drag's)
      if (kind == 1 && bp->style == kDoorRevolving) bp->drum_radius = std::max(1.0f, 0.625f * bp->width);
      /* The Door and Window buttons' placement: the wall's thickness, the turn to it and the feet. The opening's up is the Blockout's, as round 499's fix places the
       * buttons' openings, not the room's. */
      BlockoutHit wall;
      wall.piece = plan.wall.piece_id, wall.point = plan.centre, wall.normal = plan.wall.normal;
      std::string why;
      if (!blockout_stand_opening(*bo, *g, wall, &why)) {
        scene_->destroy(std::vector<GameObject *>{g});
        return Log::warn("blockout draw %s: can't be put there - %s", piece.c_str(), why.empty() ? "the wall's piece is flat or its numbers are not finite" : why.c_str());
      }
      /* (Head and Sill are measured from the feet the plan took from blockout_opening_feet - the code blockout_stand_opening stood the origin on.) */
      expanded_.insert(bo->id);
      select(g->id);
      update_blockouts(true);
      mark_changed(std::string("Create Blockout ") + (kind == 1 ? "Door" : "Window"));
      record_blockout_last(g);
      Log::info("%s %.2f x %.2f m drawn on the wall%s%s", kind == 1 ? "Door" : "Window", plan.width_m, plan.height_m, plan.note.empty() ? "" : ": ", plan.note.c_str());
      return;
    }
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
  DropOnSwitch<decltype(room_)> drop(room_);  // (a choice that turns a door or window on or off drops the footprint in progress)
  const std::string key = t.size() > 2 ? to_lower(t[2]) : "", value = t.size() > 3 ? to_lower(t[3]) : "";
  const bool on = value == "on" || value == "true" || value == "1" || value == "yes", off = value == "off" || value == "false" || value == "0" || value == "no";
  if (key == "wall") {
    const float m = (float)std::atof(value.c_str());
    if (!is_number_word(value) || !finite_bits(m) || m < 0.0f || m > 1000.0f) return Log::warn("blockout roomopt wall <metres>: 0 to 1000");
    room_.wall = m;
    return;
  }
  const char *usage = "blockout roomopt circle|polyline|solid|subtract on|off | piece room|solid|wedge|stairs|arch|door|window | operation auto|add|subtract|intersect | wall <metres>";
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
/* Round 513: the dropdown's rows are the nine shapes, a heading and the two openings. A row's number is its place in this list, the heading
 * counted (the toolkit's combo indexes its options array); the nine keep their numbers (blockout_draw_shape's), the openings are 10 and 11. */
constexpr int kRoomShapeRows = 12, kRowDoor = 10, kRowWindow = 11;
const char *const kRoomShapeNames[kRoomShapeRows] = {"Room", "Round Room", "Outline Room", "Box", "Cylinder", "Outline", "Wedge", "Stairs", "Arch",
                                                     "\001Openings", "Door", "Window"};
const char *const kRoomShapeTips = "What the tool draws.\n"
                               "Room, Round Room, Outline Room: walls, a floor and a ceiling round the footprint (a rectangle, a circle from its centre, any outline).\n"
                               "Box, Cylinder, Outline: a plain solid by the same footprints. Pulled out it adds, pushed into a face it cuts; Shift while pulling "
                               "makes the other one.\n"
                               "Wedge, Stairs, Arch: over the dragged rectangle.\n"
                               "Door, Window: drag a rectangle on a wall of a Blockout and it stands in that wall, upright, the size of the drag, in the Door Style or Window Style "
                               "chosen below. A door's bottom sits on the floor; a window's sill is the drag's lower edge.";

/* The dropdown's row for the tool's fields. */
int shape_row(bool solid, bool circle, bool polyline, int piece) {
  if (solid && piece == kPieceDoor) return kRowDoor;
  if (solid && piece == kPieceWindow) return kRowWindow;
  return blockout_draw_shape(solid, circle, polyline, piece);
}

template <class Tool>
void set_shape(Tool &r, int index) {
  DropOnSwitch<Tool> drop(r);
  if (index == kRowDoor || index == kRowWindow) {
    r.solid = true, r.circle = r.polyline = false;
    r.piece = index == kRowDoor ? kPieceDoor : kPieceWindow;
    return;
  }
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
  /* Round 500: ONE dropdown for the architectural pieces and one Add button (not nine more buttons: round 496's lesson). */
  {
    const Recti row = lay.row(u.row_h() + u.px(2));
    /* The label takes what its text needs and the dropdown the rest; when the longest name still does not fit beside the label (Linux's wider font in a narrow pane at
     * UI scale 1.5 clipped "Spiral Stairs"), the label stands alone and the dropdown shares the next row with Add - still two rows, so nothing below moves out of
     * a short window. */
    int widest = 0;
    for (int k = 1; k < kBlockoutPresets; k++) widest = std::max(widest, u.font.text_width(kBlockoutPresetNames[k]));
    const int label_w = std::min(row.w / 2, u.font.text_width("Architecture") + u.px(12));
    const bool beside = widest + u.px(28) <= row.w - label_w - u.px(10);
    u.label({row.x + u.px(4), row.y, beside ? label_w : row.w - u.px(8), row.h}, "Architecture");
    const Recti add_row = lay.row(u.row_h() + u.px(2));
    const int add_w = beside ? add_row.w - u.px(8) : u.font.text_width("Add") + u.px(24);
    const Recti add{add_row.x + add_row.w - u.px(4) - add_w, add_row.y, add_w, add_row.h};
    const Recti pick = beside ? Recti{row.x + u.px(4) + label_w, row.y, row.w - label_w - u.px(10), row.h} : Recti{add_row.x + u.px(4), add_row.y, add_row.w - add_w - u.px(12), add_row.h};
    if (u.record_buttons) u.button_rects["tools:architecture"] = pick;
    blockout_architecture_ = std::min(kBlockoutPresets - 2, std::max(0, blockout_architecture_));
    u.combo(u.id("tools_architecture"), pick, blockout_architecture_, kBlockoutPresetNames + 1, kBlockoutPresets - 1);
    u.tooltip("Column, Pilaster, Beam, Railing, Roof, Stairs L, Stairs U, Spiral Stairs or Arcade: one piece with numbers (Inspector, Adjust Last Operation, handles). GameObject > Blockout has them too.\n"
              "Console: blockout add column|pilaster|beam|railing|roof|stairs l|stairs u|stairs spiral|arcade [numbers].");
    if (u.record_buttons) u.button_rects["tools:architecture_add"] = add;
    if (u.button(add, "Add")) {
      const std::string kind = std::string("Blockout Piece: ") + (blockout_architecture_ == kPresetSpiral - 1 ? "Stairs Spiral" : kBlockoutPresetNames[blockout_architecture_ + 1]);
      create_object(kind);
    }
    u.tooltip("Makes the piece chosen in Architecture under the selected Blockout (a new Blockout when none is selected). A Pilaster stands against the wall under the mouse or the selected room's first wall.");
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
  int shape = shape_row(room_.solid, room_.circle, room_.polyline, room_.piece);
  const Recti shape_rect{row.x + row.w / 2, row.y, row.w / 2 - u.px(6), row.h};
  if (u.record_buttons) u.button_rects["tools:shape"] = shape_rect;
  if (u.combo(u.id("tools_room_shape"), shape_rect, shape, kRoomShapeNames, kRoomShapeRows)) {
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

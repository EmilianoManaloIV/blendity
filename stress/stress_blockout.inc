// SPDX-License-Identifier: GPL-2.0-or-later
// Stress section `blockout` (round 476, task 0452, ADR 0128): what Blockout's kernel costs and how exact it is, by its
// two paths, at 10, 100 and 400 pieces; and a sweep of hostile arrangements. Round 478 (task 0454): the same building
// built from room pieces, and a sweep of rooms and Intersect pieces. Included by stress_main.cpp.
// BLENDITY_BLOCKOUT_DUMP=<folder> also writes the 10- and 100-piece interiors as OBJ files, one a path.
#include "../src/scene/blockout.h"

namespace bkstress {

using bl::blockout::Op;
using bl::blockout::Path;
using bl::blockout::Piece;
using bl::blockout::Shape;

/* The same numbers every run. */
struct Rand {
  uint32_t s;
  float next() {  // 0..1
    s = s * 1664525u + 1013904223u;
    return (float)(s >> 8) / 16777216.0f;
  }
  float in(float lo, float hi) { return lo + (hi - lo) * next(); }
  int pick(int n) { return std::min(n - 1, (int)(next() * (float)n)); }
};

static Piece box(uint64_t id, Vec3 size, Vec3 at, Op op = Op::Add, Quat turn = Quat()) {
  Piece p;
  p.id = id, p.shape = Shape::Box, p.op = op, p.size = size;
  p.to_blockout = Mat4::trs(at, turn, Vec3(1.0f));
  return p;
}

/* Rooms on a square grid: an Add box (4.2 x 3.2 x 4.2 m) with a Subtract box inside it (3.8 x 2.8 x 3.8 m: walls,
 * floor and ceiling 0.2 m), 4 m apart so neighbours share their walls, door cuts (0.9 x 2.1 m) through the first
 * `doors` shared walls. Every piece touches the next: one cluster. */
static std::vector<Piece> interior(int n, int doors) {
  std::vector<Piece> p;
  uint64_t id = 1;
  for (int j = 0; j < n; j++)
    for (int i = 0; i < n; i++) {
      p.push_back(box(id++, {4.2f, 3.2f, 4.2f}, {4.0f * (float)i, 0.0f, 4.0f * (float)j}));
      p.push_back(box(id++, {3.8f, 2.8f, 3.8f}, {4.0f * (float)i, 0.0f, 4.0f * (float)j}, Op::Subtract));
    }
  for (int j = 0; j < n && doors > 0; j++)
    for (int i = 0; i + 1 < n && doors > 0; i++, doors--) p.push_back(box(id++, {0.6f, 2.1f, 0.9f}, {4.0f * (float)i + 2.0f, -0.35f, 4.0f * (float)j}, Op::Subtract));
  for (int j = 0; j + 1 < n && doors > 0; j++)
    for (int i = 0; i < n && doors > 0; i++, doors--) p.push_back(box(id++, {0.9f, 2.1f, 0.6f}, {4.0f * (float)i, -0.35f, 4.0f * (float)j + 2.0f}, Op::Subtract));
  return p;
}
/* The same building from room pieces (round 478): a Merge room of 3.8 x 2.8 x 3.8 m of air with 0.2 m all round for each room - its
 * shell is the 4.2 x 3.2 x 4.2 m box and its air the box cut out - and the same door cuts. 64 pieces for `interior 100`. */
static std::vector<Piece> interior_rooms(int n, int doors) {
  std::vector<Piece> p;
  uint64_t id = 1;
  for (int j = 0; j < n; j++)
    for (int i = 0; i < n; i++) {
      Piece room = box(id++, {3.8f, 2.8f, 3.8f}, {4.0f * (float)i, 0.0f, 4.0f * (float)j});
      room.room = true, room.wall = room.floor = room.ceiling = 0.2f;
      p.push_back(room);
    }
  const std::vector<Piece> boxes = interior(n, doors);
  for (size_t k = (size_t)n * (size_t)n * 2; k < boxes.size(); k++) {
    p.push_back(boxes[k]);
    p.back().id = id++;
  }
  return p;
}
static double interior_volume(int n, int doors) {
  const double side = 4.0 * n + 0.2;
  return side * side * 3.2 - (double)n * n * 3.8 * 3.8 * 2.8 - (double)doors * 0.9 * 2.1 * 0.2;
}

/* Boxes, cylinders and spheres in turn, every fourth a Subtract, turned at odd angles: in piles of four that don't
 * touch each other (`spread` 6: many clusters), or all through one spot (`spread` 0: a heap). */
static std::vector<Piece> piles(int count, float spread, uint32_t seed) {
  Rand r{seed};
  std::vector<Piece> p;
  const int side = std::max(1, (int)std::ceil(std::sqrt((double)((count + 3) / 4))));
  for (int i = 0; i < count; i++) {
    const int pile = i / 4;
    const Vec3 centre(spread * (float)(pile % side), 0.0f, spread * (float)(pile / side));
    Piece q;
    q.id = (uint64_t)i + 1;
    q.shape = (Shape)(i % 3);
    q.op = i % 4 == 3 ? Op::Subtract : Op::Add;
    q.size = {r.in(0.8f, 1.6f), r.in(0.8f, 1.6f), r.in(0.8f, 1.6f)};
    q.radius = r.in(0.4f, 0.8f), q.height = r.in(0.8f, 1.6f);
    const Vec3 at = centre + Vec3(r.in(-0.6f, 0.6f), r.in(-0.6f, 0.6f), r.in(-0.6f, 0.6f));
    q.to_blockout = Mat4::trs(at, Quat::euler({r.in(0, 360), r.in(0, 360), r.in(0, 360)}), Vec3(1.0f));
    p.push_back(q);
  }
  return p;
}

struct Run {
  bool ok = false;
  Mesh mesh;
  bl::blockout::Faces faces;
  bl::blockout::Report rep;
  double ms = 0.0;
};

/* The kernel as round 477 will call it: on a thread of its own, inside JobSystem::run_background. */
static Run on_worker(const std::vector<Piece> &pieces, const bl::blockout::Settings &st, bl::blockout::Cache *cache = nullptr) {
  Run r;
  std::thread t([&] {
    JobSystem::global().run_background([&] {
      ScopedTimer tm;
      r.ok = bl::blockout::build(pieces, st, r.mesh, &r.faces, &r.rep, cache);
      r.ms = tm.ms();
    });
  });
  t.join();
  return r;
}

static double median(std::vector<double> v) {
  std::sort(v.begin(), v.end());
  return v.empty() ? 0.0 : v[v.size() / 2];
}

/* The plane of a piece's flat side (a box's six, a cylinder's two caps); false for a side that is not one plane. */
static bool side_plane(const Piece &p, uint32_t side, Vec3 &on, Vec3 &n) {
  int axis = 1;
  float at = 0.0f;
  /* A room's shell sides lie the wall, floor or ceiling past the air's (the stress rooms have no scale: a thickness is then the same in the
   * piece's own space). */
  float grow = 0.0f;
  if (p.room && side >= bl::blockout::kSideShell) {
    side -= bl::blockout::kSideShell;
    grow = 1.0f;
  }
  if (p.shape == Shape::Box && side < 6) {
    axis = (int)side / 2, at = (side & 1 ? 0.5f : -0.5f) * std::fabs(p.size[axis]);
    if (grow > 0.0f) at += (axis == 1 ? (side & 1 ? (p.open_top ? 0.0f : p.ceiling) : -p.floor) : (side & 1 ? p.wall : -p.wall));
  }
  else if (p.shape == Shape::Cylinder && (side == 1 || side == 2)) {
    at = (side == 2 ? 0.5f : -0.5f) * std::fabs(p.height);
    if (grow > 0.0f) at += side == 2 ? (p.open_top ? 0.0f : p.ceiling) : -p.floor;
  }
  else return false;
  Vec3 local(0.0f), dir(0.0f);
  local[axis] = at, dir[axis] = 1.0f;
  on = p.to_blockout.point(local);
  n = normalize(p.to_blockout.inverse().transposed().dir(dir));
  return true;
}

/* The farthest any corner of a flat face lies from the side the face names (the small faces of an unresolved place
 * are not flat faces: the kernel says which are). */
static double flatness(const Run &r, const std::vector<Piece> &pieces) {
  std::unordered_map<uint64_t, const Piece *> by_id;
  for (const Piece &p : pieces) by_id[p.id] = &p;
  double worst = 0.0;
  for (size_t f = 0; f < r.mesh.face_count(); f++) {
    const auto it = by_id.find(r.faces.piece[f]);
    Vec3 on, n;
    if (!r.faces.flat[f] || it == by_id.end() || !side_plane(*it->second, r.faces.side[f], on, n)) continue;
    for (uint32_t i = 0; i < r.mesh.face_size(f); i++) worst = std::max(worst, (double)std::fabs(dot(n, r.mesh.positions[r.mesh.face_verts(f)[i]] - on)));
  }
  return worst;
}

/* The farthest a corner of `ref` lies from the nearest corner of `m` (a grid of cells finds the near ones). */
static double corner_error(const Mesh &ref, const Mesh &m) {
  const float cell = 0.5f;
  std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
  auto key = [&](Vec3 p, int dx, int dy, int dz) {
    const int64_t x = (int64_t)std::floor(p.x / cell) + dx, y = (int64_t)std::floor(p.y / cell) + dy, z = (int64_t)std::floor(p.z / cell) + dz;
    return (uint64_t)((x * 73856093) ^ (y * 19349663) ^ (z * 83492791));
  };
  for (uint32_t i = 0; i < m.positions.size(); i++) grid[key(m.positions[i], 0, 0, 0)].push_back(i);
  double worst = 0.0;
  for (const Vec3 &p : ref.positions) {
    double best = cell;
    for (int dz = -1; dz <= 1; dz++)
      for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
          const auto it = grid.find(key(p, dx, dy, dz));
          if (it == grid.end()) continue;
          for (uint32_t i : it->second) best = std::min(best, (double)length(m.positions[i] - p));
        }
    worst = std::max(worst, best);
  }
  return worst;
}

/* Whether a face has no area: it is thinner - its area over its longest edge - than eight steps of a 32-bit position
 * at the coordinate `far`. (In doubles, and as a width: three points in a line that the floats hold a step off the
 * line are still caught, and a piece scaled to a thousandth is not all "no area" by its own small faces.) */
static bool no_area(const Mesh &m, size_t f, double far) {
  const uint32_t *v = m.face_verts(f);
  const uint32_t k = m.face_size(f);
  double twice[3] = {0, 0, 0}, longest = 0.0;
  for (uint32_t i = 0; i < k; i++) {
    const Vec3 o = m.positions[v[0]], p = m.positions[v[i]], q = m.positions[v[(i + 1) % k]];
    const double a[3] = {(double)p.x - o.x, (double)p.y - o.y, (double)p.z - o.z}, b[3] = {(double)q.x - o.x, (double)q.y - o.y, (double)q.z - o.z};
    twice[0] += a[1] * b[2] - a[2] * b[1], twice[1] += a[2] * b[0] - a[0] * b[2], twice[2] += a[0] * b[1] - a[1] * b[0];
    longest = std::max(longest, std::sqrt(((double)q.x - p.x) * ((double)q.x - p.x) + ((double)q.y - p.y) * ((double)q.y - p.y) + ((double)q.z - p.z) * ((double)q.z - p.z)));
  }
  const double area = 0.5 * std::sqrt(twice[0] * twice[0] + twice[1] * twice[1] + twice[2] * twice[2]);
  return !(longest > 0.0) || area / longest < 8.0 * 1.1920929e-7 * far;
}
static double farthest(const Mesh &m) {
  double far = 0.0;
  for (const Vec3 &p : m.positions) far = std::max(far, (double)std::max(std::fabs(p.x), std::max(std::fabs(p.y), std::fabs(p.z))));
  return far;
}

/* What is wrong with a result, or nothing. */
static std::string problem(const Mesh &m) {
  const std::string bad = ppstress::invalid(m);
  if (!bad.empty()) return bad;
  if (!ppstress::closed_manifold(m)) return "open or pinched";
  if (m.face_count() && ppstress::signed_volume(m) <= 0.0) return "inside out";
  return "";
}

static void dump(const std::string &name, const Mesh &m) {
  const char *dir = std::getenv("BLENDITY_BLOCKOUT_DUMP");
  if (!dir || !*dir) return;
  fs::make_dirs(dir);
  ExportItem item;
  item.name = name, item.mesh = &m;
  std::string err;
  if (!export_file(fs::join(dir, name + ".obj"), {item}, export_defaults((int)ExportFormat::OBJ), &err)) std::printf("  dump %s: %s\n", name.c_str(), err.c_str());
}

struct Scene3 {
  std::string name;
  std::vector<Piece> pieces;
  double volume = 0.0;  // the closed form, when there is one
  size_t moved = 0;     // the piece a row moves
  Vec3 step;            // ...and by how much
  bool verdict = false;
};

static std::string sci(double v) { return v == 0.0 ? "0" : strprintf("%.1e", v); }

/* One scene by one path: the row. */
static void row(Report &rep, const Options &o, const Scene3 &sc, Path path, float cell, const Mesh *crisp) {
  bl::blockout::Settings st;
  st.path = path, st.cell = cell, st.max_cells = 64000000;
  const int reps = o.quick ? 1 : 5;
  std::vector<double> whole, moved;
  Run last;
  for (int i = 0; i < reps; i++) {
    last = on_worker(sc.pieces, st);
    whole.push_back(last.ms);
  }
  const std::string label = sc.name + (path == Path::Crisp ? std::string(", crisp") : strprintf(", field cell %g", cell));
  if (!last.ok) {
    rep.row({label, "refused: " + last.rep.why, "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-"});
    return;
  }
  /* One piece moved, the cache warm: back and forth, so every build is a change. */
  bl::blockout::Cache cache;
  std::vector<Piece> pieces = sc.pieces;
  (void)on_worker(pieces, st, &cache);
  Run after;
  for (int i = 0; i < reps; i++) {
    const float way = i % 2 == 0 ? 1.0f : -1.0f;
    pieces[sc.moved].to_blockout.m[12] += way * sc.step.x, pieces[sc.moved].to_blockout.m[13] += way * sc.step.y, pieces[sc.moved].to_blockout.m[14] += way * sc.step.z;
    after = on_worker(pieces, st, &cache);
    moved.push_back(after.ms);
    if (after.ok) continue;
    rep.row({label, f2(median(whole)), "refused after the move: " + after.rep.why, "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-"});
    return;
  }
  ScopedTimer rt;
  const size_t tris = after.mesh.render_mesh().tri_count();
  const double render = rt.ms(), moved_ms = median(moved), drawn = moved_ms + render;
  const std::string bad = problem(last.mesh);
  const double vol = ppstress::signed_volume(last.mesh);
  std::string vol_err = "-";
  if (sc.volume > 0.0) vol_err = strprintf("%.4f", 100.0 * std::fabs(vol - sc.volume) / sc.volume);
  else if (crisp && path == Path::Field) vol_err = strprintf("%.4f", 100.0 * std::fabs(vol - ppstress::signed_volume(*crisp)) / std::max(1e-9, ppstress::signed_volume(*crisp)));
  const bl::blockout::Report &r = last.rep;
  rep.row({label, f2(median(whole)), f2(moved_ms), f2(render), f2(drawn), f2(r.ms_solids), f2(path == Path::Crisp ? r.ms_boolean : r.ms_grid), f2(r.ms_faces),
           std::to_string(r.clusters.size()), std::to_string(last.mesh.face_count()), std::to_string(tris), std::to_string(last.mesh.vert_count()), num((double)r.cells),
           vol_err, sci(flatness(last, sc.pieces)), crisp && path == Path::Field ? sci(corner_error(*crisp, last.mesh)) : "-",
           std::to_string(r.unresolved), !bad.empty() ? "PROBLEM: " + bad : !sc.verdict ? "-" : drawn <= 100.0 ? "ok" : "OVER"});
  std::string file = label;
  for (char &ch : file)
    if (ch == ' ' || ch == ',') ch = '_';
  if (sc.pieces.size() <= 100 && sc.name.rfind("interior", 0) == 0) dump(file, last.mesh);
}

/* A hostile arrangement: a few pieces, some snapped to a quarter-metre grid (faces in one plane are then the rule),
 * some turned, some tiny or huge, some not usable at all. `boxes`: only boxes of half a metre and more on that grid,
 * turned by right angles - what a floor plan is made of, and what a cell of 0.08 m resolves. */
static std::vector<Piece> hostile(uint32_t seed, bool boxes) {
  Rand r{seed * 2654435761u + 12345u};
  std::vector<Piece> p;
  const int count = 2 + r.pick(8);
  const bool snapped = boxes || r.pick(3) == 0;
  for (int i = 0; i < count; i++) {
    Piece q;
    q.id = (uint64_t)i + 1;
    q.shape = boxes ? Shape::Box : (Shape)r.pick(3);
    q.op = r.pick(4) == 0 ? Op::Subtract : Op::Add;
    auto size = [&]() { return snapped ? 0.25f * (float)((boxes ? 2 : 1) + r.pick(8)) : r.in(0.3f, 2.0f); };
    q.size = {size(), size(), size()};
    q.radius = size() * 0.5f, q.height = size();
    q.segments = 3 + r.pick(14), q.rings = 2 + r.pick(8);
    Vec3 at = snapped ? Vec3(0.25f * (float)(r.pick(9) - 4), 0.25f * (float)(r.pick(9) - 4), 0.25f * (float)(r.pick(9) - 4)) : Vec3(r.in(-1, 1), r.in(-1, 1), r.in(-1, 1));
    Quat turn = snapped ? Quat::euler({90.0f * (float)r.pick(4), 90.0f * (float)r.pick(4), 0.0f}) : Quat::euler({r.in(0, 360), r.in(0, 360), r.in(0, 360)});
    Vec3 scale(1.0f);
    const int odd = boxes ? 9 + r.pick(31) : r.pick(40);  // (a floor plan's boxes are all usable and plain)
    if (odd == 0) q.size.x = 0.0f;                                    // no size
    if (odd == 1) q.size.y = std::numeric_limits<float>::quiet_NaN();  // not a number
    if (odd == 2) at.x = std::numeric_limits<float>::infinity();
    if (odd == 3) scale = {1.0f, 0.0f, 1.0f};                          // flattened
    if (odd == 4) scale = {-1.0f, 1.0f, 1.0f};                         // mirrored
    if (odd == 5) scale = {1e-3f, 1e-3f, 1e-3f};                       // tiny
    if (odd == 6) scale = {30.0f, 30.0f, 30.0f};                       // huge
    if (odd == 7) scale = {3.0f, 0.2f, 1.0f};                          // uneven
    q.to_blockout = Mat4::trs(at, turn, scale);
    if (odd == 8) q.to_blockout.m[4] = 0.7f;                           // sheared
    p.push_back(q);
  }
  return p;
}

/* Round 480 (test engineer): meshes as pieces, good and hostile - a torus, a cube, an icosphere, a concave prism, an open cube, an inside-out torus, two cubes
 * crossing in one mesh, a cube cut apart along an edge loop, a cube whose faces share no point, a cube with a corner that is not a number, an empty mesh, one
 * triangle, a two-sided sheet, a flat grid - among boxes, rooms, wedges, cylinders and spheres, some turned, mirrored or stretched. */
struct MeshLib {
  std::vector<MeshPtr> meshes;
  std::vector<const char *> names;
  static MeshPtr soup(const std::vector<Vec3> &pos, const std::vector<std::vector<uint32_t>> &faces) {
    MeshPtr m = std::make_shared<Mesh>();
    m->positions = pos;
    for (const std::vector<uint32_t> &f : faces) {
      for (uint32_t v : f) m->corner_verts.push_back(v);
      m->face_offsets.push_back((uint32_t)m->corner_verts.size());
    }
    return m;
  }
  static std::vector<std::vector<uint32_t>> faces_of(const Mesh &m) {
    std::vector<std::vector<uint32_t>> out;
    for (size_t f = 0; f < m.face_count(); f++) out.emplace_back(m.face_verts(f), m.face_verts(f) + m.face_size(f));
    return out;
  }
  void add(const char *name, MeshPtr m) { names.push_back(name), meshes.push_back(std::move(m)); }
  MeshLib() {
    add("torus", primitives::torus(0.5f, 0.2f, 24, 12));
    add("cube", primitives::cube(1.0f));
    add("ico sphere", primitives::ico_sphere(0.5f, 2));
    {  // (an L prism: concave top and bottom faces)
      const std::vector<Vec2> pts = {{0, 0}, {2, 0}, {2, 1}, {1, 1}, {1, 2}, {0, 2}};
      std::vector<Vec3> pos;
      for (const Vec2 &q : pts) pos.push_back(Vec3(q.x, 0.0f, q.y)), pos.push_back(Vec3(q.x, 1.5f, q.y));
      std::vector<std::vector<uint32_t>> faces;
      std::vector<uint32_t> bottom, top;
      const uint32_t n = (uint32_t)pts.size();
      for (uint32_t i = 0; i < n; i++) {
        const uint32_t j = (i + 1) % n;
        faces.push_back({2 * i, 2 * j, 2 * j + 1, 2 * i + 1});
        bottom.push_back(2 * (n - 1 - i)), top.push_back(2 * i + 1);
      }
      faces.push_back(bottom), faces.push_back(top);
      add("L prism", soup(pos, faces));
    }
    {
      MeshPtr open = primitives::cube(1.0f);
      open->corner_verts.resize(open->face_offsets[open->face_count() - 1]);
      open->face_offsets.pop_back();
      add("open cube", open);
    }
    {
      MeshPtr flipped = mesh_copy(*primitives::torus(0.5f, 0.2f, 24, 12));
      meshops::flip_normals(*flipped);
      add("inside out torus", flipped);
    }
    {
      const MeshPtr c = primitives::cube(1.0f);
      std::vector<Vec3> pos = c->positions;
      for (const Vec3 &p : c->positions) pos.push_back(p + Vec3(0.4f, 0.2f, 0.1f));
      std::vector<std::vector<uint32_t>> faces = faces_of(*c);
      for (std::vector<uint32_t> f : faces_of(*c)) {
        for (uint32_t &v : f) v += (uint32_t)c->positions.size();
        faces.push_back(f);
      }
      add("two crossing cubes", soup(pos, faces));
    }
    {
      const MeshPtr c = primitives::cube(1.0f);
      std::vector<Vec3> pos = c->positions;
      for (const Vec3 &p : c->positions) pos.push_back(p);
      std::vector<std::vector<uint32_t>> faces = faces_of(*c);
      for (size_t f = 3; f < faces.size(); f++)
        for (uint32_t &v : faces[f]) v += (uint32_t)c->positions.size();
      add("split cube", soup(pos, faces));
    }
    {
      const MeshPtr c = primitives::cube(1.0f);
      std::vector<Vec3> pos;
      std::vector<std::vector<uint32_t>> faces;
      for (size_t f = 0; f < c->face_count(); f++) {
        std::vector<uint32_t> face;
        for (uint32_t i = 0; i < c->face_size(f); i++) face.push_back((uint32_t)pos.size()), pos.push_back(c->positions[c->face_verts(f)[i]]);
        faces.push_back(face);
      }
      add("unwelded cube", soup(pos, faces));
    }
    {
      MeshPtr nan = primitives::cube(1.0f);
      nan->positions[3].y = std::numeric_limits<float>::quiet_NaN();
      add("cube with a NaN corner", nan);
    }
    add("empty", std::make_shared<Mesh>());
    add("one triangle", soup({Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0)}, {{0, 1, 2}}));
    add("two-sided sheet", soup({Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(1, 0, 1), Vec3(0, 0, 1)}, {{0, 1, 2, 3}, {3, 2, 1, 0}}));
    add("flat grid", primitives::plane(1.0f, 2));
  }
};
static const MeshLib &mesh_lib() {
  static const MeshLib lib;
  return lib;
}

static std::vector<Piece> mesh_hostile(uint32_t seed) {
  const MeshLib &lib = mesh_lib();
  Rand r{seed * 2654435761u + 977u};
  std::vector<Piece> out;
  const int count = 2 + r.pick(7);
  for (int i = 0; i < count; i++) {
    const int kind = r.pick(12);
    Piece p;
    p.id = (uint64_t)i + 1;
    if (kind < 3) p.shape = Shape::Box, p.size = {0.5f * (float)(2 + r.pick(5)), 0.5f * (float)(2 + r.pick(5)), 0.5f * (float)(2 + r.pick(5))};
    else if (kind == 3) p.shape = Shape::Sphere, p.radius = 0.5f + 0.25f * (float)r.pick(4), p.segments = 12, p.rings = 6;
    else if (kind == 4) p.shape = Shape::Wedge, p.size = {1.0f, 1.0f, 2.0f};
    else if (kind == 5) p.shape = Shape::Box, p.size = {2.5f, 2.0f, 2.5f}, p.room = true;
    else {
      const size_t k = (size_t)r.pick((int)lib.meshes.size());
      p.shape = Shape::Mesh, p.mesh = lib.meshes[k], p.mesh_key = (uint64_t)k + 1;
    }
    const int op = r.pick(5);
    if (!p.room) p.op = op == 0 ? Op::Subtract : op == 1 && p.shape != Shape::Mesh ? Op::Subtract : Op::Add;
    Vec3 scale(1.0f);
    const int odd = r.pick(10);
    if (odd == 0) scale = Vec3(-1.0f, 1.0f, 1.0f);
    if (odd == 1) scale = Vec3(2.0f, 0.5f, 1.0f);
    const Vec3 at(0.25f * (float)(r.pick(13) - 6), 0.25f * (float)(r.pick(13) - 6), 0.25f * (float)(r.pick(13) - 6));
    const Quat turn = odd == 2 ? Quat::euler({r.in(0, 360), r.in(0, 360), r.in(0, 360)}) : Quat::euler({90.0f * (float)r.pick(4), 90.0f * (float)r.pick(4), 0.0f});
    p.to_blockout = Mat4::trs(at, turn, scale);
    out.push_back(p);
  }
  return out;
}

/* The sweep's kinds of arrangement. */
static const char *const kWhat[6] = {", boxes on a 0.25 m grid", ", the same boxes turned as a whole", ", any pieces", ", rooms and Intersect pieces", ", wedges, stairs, arches and outlines",
                                     ", hostile meshes as pieces among boxes, rooms and wedges"};
static const char *const kWhatShort[6] = {", boxes", ", boxes turned", "", ", rooms", ", new shapes", ", mesh pieces"};

/* Round 480 (task 0456): wedges, stairs, arches and outlines among boxes, on the 0.25 m grid and turned by right angles, with the hostile numbers
 * of the task - 0, 1 and 257 steps, an angle of 0, of 720 and not a number, an inner radius above the outer, an outline of three points in a line,
 * of 256 and of 300 points, crossing itself, with a point twice, with a corner that is not a number or of two corners only, a wedge with a side
 * of no length - and the same shapes as rooms (the wedge and the outline) and as Subtract pieces. A piece it cannot use is skipped by name. */
static std::vector<Piece> shapes_hostile(uint32_t seed) {
  Rand r{seed * 3266489917u + 668265263u};
  std::vector<Piece> p;
  const int count = 2 + r.pick(5);
  const float nan = std::numeric_limits<float>::quiet_NaN();
  for (int i = 0; i < count; i++) {
    Piece q;
    q.id = (uint64_t)i + 1;
    const int kind = i == 0 && r.pick(2) == 0 ? 0 : r.pick(6);  // (a box now and then to stand the others on or cut from)
    auto size = [&]() { return 0.5f * (float)(1 + r.pick(6)); };
    q.size = {size(), size(), size()};
    q.height = size();
    q.radius = size();
    q.op = r.pick(4) == 0 ? Op::Subtract : Op::Add;
    if (kind == 0) q.shape = Shape::Box;
    if (kind == 1 || kind == 2) {  // a wedge, a room with a sloped ceiling
      q.shape = Shape::Wedge;
      if (r.pick(5) == 0) q.room = true, q.wall = q.floor = q.ceiling = 0.25f * (float)(1 + r.pick(2)), q.keep_walls = r.pick(3) == 0, q.open_top = r.pick(4) == 0;
      if (r.pick(25) == 0) q.size.z = 0.0f;
    }
    if (kind == 3) {  // stairs
      q.shape = Shape::Stairs;
      q.steps = 1 + r.pick(10);
      const int odd = r.pick(8);
      if (odd == 0) q.steps = 0;
      if (odd == 1) q.steps = 1;
      if (odd == 2) q.steps = 257;
      q.fill_under = r.pick(2) == 0;
    }
    if (kind == 4) {  // an arch
      q.shape = Shape::Arch;
      q.inner_radius = q.radius * 0.5f, q.angle = 180.0f, q.segments = 3 + r.pick(14);
      const int odd = r.pick(10);
      if (odd == 0) q.angle = 0.0f;
      if (odd == 1) q.angle = 720.0f;
      if (odd == 2) q.inner_radius = q.radius * 1.5f;
      if (odd == 3) q.angle = 360.0f;
      if (odd == 4) q.angle = nan;
      if (odd == 5) q.inner_radius = 0.0f;
    }
    if (kind == 5) {  // an outline: an L, a corridor with a bend, a rectangle, or something hostile
      q.shape = Shape::Outline;
      const int what = r.pick(12);
      const float a = size() + 1.0f, b = size() + 1.0f;
      if (what < 4) q.outline = {{0, 0}, {2 * a, 0}, {2 * a, a}, {a, a}, {a, 2 * a}, {0, 2 * a}};  // (an L)
      else if (what < 6) q.outline = {{0, 0}, {3 * a, 0}, {3 * a, b}, {b, b}, {b, 3 * a}, {0, 3 * a}};
      else if (what < 8) q.outline = {{0, 0}, {a, 0}, {a, b}, {0, b}};
      else if (what == 8) q.outline = {{0, 0}, {a, 0}, {2 * a, 0}};                       // three points in a line
      else if (what == 9) q.outline = {{0, 0}, {a, b}, {a, 0}, {0, b}};                     // crossing itself
      else if (what == 10) {                                                                // 256 points on a circle (and 300 when the numbers are odd)
        const int n = r.pick(2) == 0 ? 256 : 300;
        for (int k = 0; k < n; k++) q.outline.push_back({a * std::cos(2.0f * kPi * (float)k / (float)n), a * std::sin(2.0f * kPi * (float)k / (float)n)});
      }
      else {
        q.outline = {{0, 0}, {a, 0}, {a, 0}, {a, b}, {0, b}};  // a point twice
        if (r.pick(2) == 0) q.outline[2] = {nan, 0.0f};
        if (r.pick(3) == 0) q.outline.resize(2);
      }
      if (r.pick(6) == 0 && q.outline.size() >= 3) q.room = true, q.wall = q.floor = q.ceiling = 0.25f * (float)(1 + r.pick(2)), q.keep_walls = r.pick(3) == 0;
    }
    const Vec3 at(0.25f * (float)(r.pick(9) - 4), 0.25f * (float)(r.pick(9) - 4), 0.25f * (float)(r.pick(9) - 4));
    const Quat turn = Quat::euler({90.0f * (float)r.pick(4), 90.0f * (float)r.pick(4), 90.0f * (float)r.pick(4)});
    q.to_blockout = Mat4::trs(at, turn, Vec3(1.0f));
    p.push_back(q);
  }
  return p;
}

/* Round 478: rooms on the 0.25 m grid - Merge and Keep, open-topped, with no thickness, thicker than the room is wide, round ones - among
 * boxes that add, subtract and (at most two) intersect, some turned by right angles, a few unusable or unevenly scaled. */
static std::vector<Piece> rooms_hostile(uint32_t seed) {
  Rand r{seed * 2246822519u + 3266489917u};
  std::vector<Piece> p;
  const int count = 2 + r.pick(6);
  int intersects = 0;
  for (int i = 0; i < count; i++) {
    Piece q;
    q.id = (uint64_t)i + 1;
    auto size = [&]() { return 0.5f * (float)(1 + r.pick(6)); };
    q.size = {size(), size(), size()};
    const int kind = r.pick(10);
    if (kind < 5) {
      q.room = true;
      q.keep_walls = r.pick(3) == 0;
      q.open_top = r.pick(4) == 0;
      const int thick = r.pick(7);
      auto t = [&]() { return thick == 0 ? 0.0f : thick == 1 ? 3.0f : 0.25f * (float)(1 + r.pick(2)); };  // (none, thicker than the room, a quarter or a half metre)
      q.wall = t(), q.floor = t(), q.ceiling = t();
      if (r.pick(6) == 0) q.shape = Shape::Cylinder, q.radius = 0.25f * (float)(2 + r.pick(5)), q.height = size(), q.segments = 3 + r.pick(14);
    }
    else if (kind < 7) q.op = Op::Add;
    else if (kind < 9) q.op = Op::Subtract;
    else if (intersects++ < 2) q.op = Op::Intersect, q.size = {3.0f + size(), 3.0f + size(), 3.0f + size()};
    const Vec3 at(0.25f * (float)(r.pick(9) - 4), 0.25f * (float)(r.pick(9) - 4), 0.25f * (float)(r.pick(9) - 4));
    Quat turn = Quat::euler({90.0f * (float)r.pick(4), 90.0f * (float)r.pick(4), 0.0f});
    Vec3 scale(1.0f);
    const int odd = r.pick(30);
    if (odd == 0) q.size.x = 0.0f;
    if (odd == 1) q.wall = std::numeric_limits<float>::quiet_NaN();
    if (odd == 2) q.wall = -1.0f;
    if (odd == 3) scale = {1.0f, 0.0f, 1.0f};
    if (odd == 4) scale = {-1.0f, 1.0f, 1.0f};
    if (odd == 5) scale = {2.0f, 0.5f, 1.0f};
    q.to_blockout = Mat4::trs(at, turn, scale);
    p.push_back(q);
  }
  return p;
}

/* Arrangement `seed` of a kind: boxes of a floor plan; the same, the whole plan turned by an odd angle (planes at an
 * angle that several pieces share); any pieces. */
static std::vector<Piece> arrangement(uint32_t seed, int what) {
  if (what == 3) return rooms_hostile(seed);
  if (what == 4) return shapes_hostile(seed);
  if (what == 5) return mesh_hostile(seed);
  std::vector<Piece> pieces = hostile(seed, what < 2);
  if (what != 1) return pieces;
  Rand r{seed * 40503u + 977u};
  const Quat turn = Quat::euler({r.in(0, 360), r.in(0, 360), r.in(0, 360)});
  const Mat4 whole = Mat4::trs(Vec3(0.0f), turn, Vec3(1.0f));
  for (Piece &p : pieces) p.to_blockout = whole * p.to_blockout;
  return pieces;
}

/* One arrangement of the sweep alone (BLENDITY_BLOCKOUT_SEED): its pieces, its faces that lie on top of each other,
 * its mesh written out when BLENDITY_BLOCKOUT_DUMP names a folder. */
static void list_arrangement(uint32_t seed, Path path, int what, const std::vector<Piece> &pieces, const bl::blockout::Settings &st) {
  Mesh m;
  bl::blockout::Report r;
  bl::blockout::Faces named;
  std::fflush(stdout);
  (void)bl::blockout::build(pieces, st, m, &named, &r);
  std::vector<std::pair<uint32_t, uint32_t>> pairs;
  const AABB box = m.bounds();
  meshops::overlapping_faces(m, 1e-4f * length(box.max - box.min), &pairs);
  std::printf("  seed %u %s%s: %zu pieces, %zu faces, %zu unresolved, %zu pairs on top of each other\n", seed, path == Path::Crisp ? "crisp" : "field", kWhatShort[what], pieces.size(),
              m.face_count(), r.unresolved, pairs.size());
  for (const Piece &p : pieces) {
    const Vec3 x = p.to_blockout.column(0), y = p.to_blockout.column(1), z = p.to_blockout.column(2), t = p.to_blockout.translation();
    std::printf("    piece %llu: %s %s size %g %g %g radius %g height %g segments %d rings %d at %g %g %g, axes (%g %g %g) (%g %g %g) (%g %g %g)\n", (unsigned long long)p.id,
                p.shape == Shape::Box ? "box" : p.shape == Shape::Cylinder ? "cylinder" : p.shape == Shape::Sphere ? "sphere" : p.shape == Shape::Wedge ? "wedge" : p.shape == Shape::Stairs ? "stairs" : p.shape == Shape::Arch ? "arch" : p.shape == Shape::Outline ? "outline" : "mesh", p.room ? "room" : p.op == Op::Add ? "add" : p.op == Op::Subtract ? "subtract" : "intersect", p.size.x, p.size.y, p.size.z,
                p.radius, p.height, p.segments, p.rings, t.x, t.y, t.z, x.x, x.y, x.z, y.x, y.y, y.z, z.x, z.y, z.z);
    if (p.room) std::printf("      room: wall %g floor %g ceiling %g%s%s\n", p.wall, p.floor, p.ceiling, p.keep_walls ? ", keep walls" : ", merge", p.open_top ? ", open top" : "");
  }
  for (size_t i = 0; i < pairs.size() && i < 12; i++)
    for (uint32_t f : {pairs[i].first, pairs[i].second}) {
      const Vec3 n = m.face_normal(f), c0 = m.face_center(f);
      std::printf("    %s face %u: piece %llu side %u flat %d, %u corners, normal %.3f %.3f %.3f, centre %.3f %.3f %.3f\n", f == pairs[i].first ? "pair" : "  on", f,
                  (unsigned long long)named.piece[f], named.side[f], (int)named.flat[f], m.face_size(f), n.x, n.y, n.z, c0.x, c0.y, c0.z);
      for (uint32_t k = 0; k < m.face_size(f) && k < 12; k++) {
        const Vec3 p = m.positions[m.face_verts(f)[k]];
        std::printf("        point %u: %.5f %.5f %.5f\n", m.face_verts(f)[k], p.x, p.y, p.z);
      }
    }
  /* Its faces of no area (the first dozen). */
  size_t listed = 0;
  const double far = farthest(m);
  for (size_t f = 0; f < m.face_count() && listed < 12; f++) {
    const uint32_t *v = m.face_verts(f);
    if (!no_area(m, f, far)) continue;
    listed++;
    std::printf("    no area, face %zu: piece %llu side %u flat %d:", f, (unsigned long long)named.piece[f], named.side[f], (int)named.flat[f]);
    for (uint32_t k = 0; k < m.face_size(f) && k < 8; k++) std::printf(" %u (%.6f %.6f %.6f)", v[k], m.positions[v[k]].x, m.positions[v[k]].y, m.positions[v[k]].z);
    std::printf("\n");
  }
  dump(strprintf("seed%u_%s_%d", seed, path == Path::Crisp ? "crisp" : "field", what), m);
  std::fflush(stdout);
}

/* What the sweep counts of one kind of arrangement. */
struct Tally {
  size_t built = 0, skipped = 0, refused = 0, problems = 0, overlaps = 0, ridges = 0, no_area = 0, unresolved = 0, unchecked = 0;
  std::string first;
};

/* A face's plane from its corners, in doubles: the unit normal, how wide the face is (twice its area over its longest
 * edge) and how far its corners are off the plane. */
struct FacePlane {
  double n[3] = {0, 0, 0}, width = 0.0, off = 0.0;
};

static FacePlane face_plane(const Mesh &m, uint32_t f) {
  FacePlane out;
  const uint32_t *v = m.face_verts(f);
  const uint32_t k = m.face_size(f);
  const Vec3 o = m.positions[v[0]];
  double twice[3] = {0, 0, 0}, longest = 0.0;
  for (uint32_t i = 0; i < k; i++) {
    const Vec3 p = m.positions[v[i]], q = m.positions[v[(i + 1) % k]];
    const double a[3] = {(double)p.x - o.x, (double)p.y - o.y, (double)p.z - o.z}, b[3] = {(double)q.x - o.x, (double)q.y - o.y, (double)q.z - o.z};
    twice[0] += a[1] * b[2] - a[2] * b[1], twice[1] += a[2] * b[0] - a[0] * b[2], twice[2] += a[0] * b[1] - a[1] * b[0];
    longest = std::max(longest, std::sqrt((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]) + (b[2] - a[2]) * (b[2] - a[2])));
  }
  const double len = std::sqrt(twice[0] * twice[0] + twice[1] * twice[1] + twice[2] * twice[2]);
  if (!(len > 0.0) || !(longest > 0.0)) return out;
  for (int c = 0; c < 3; c++) out.n[c] = twice[c] / len;
  out.width = len / longest;
  for (uint32_t i = 0; i < k; i++) {
    const Vec3 p = m.positions[v[i]];
    out.off = std::max(out.off, std::fabs(((double)p.x - o.x) * out.n[0] + ((double)p.y - o.y) * out.n[1] + ((double)p.z - o.z) * out.n[2]));
  }
  return out;
}

/* Two flat faces that meet at a ridge: back to back, their planes 0.3 degrees and more apart. They are in two planes
 * that cross in a line, and neither lies on the other; the check for faces on top of each other names them all the
 * same where one is a strip along that line narrow enough to stay within its distance for "one plane".
 *
 * Why this is told apart from a doubled wall (round 480; the code review's conditions are the three below). The first
 * whole sweep of the new shapes on the exact path read 2 problems of 2,000, both a flat side against a round room's
 * wall - an outline of 256 points, its facets 1.4 degrees apart - along the ridge between two facets:
 * - seed 475: a stairs' side touches the ridge. The ridge is a float's rounding past the side at one end (a
 *   millionth of a metre) and on it at the other: a needle of the side 2.5 m long and 0.18 mm wide is left, back to
 *   back with the two facets - a closed crack a micron thick.
 * - seed 1337: a cutter's end 0.04 mm short of the ridge (the room's inner outline is mitred: 0.5 m * (1 / cos(pi /
 *   256) - 1) = 0.038 mm past the circle - the pieces' own numbers, no rounding): a skin of wall 0.04 mm thick and
 *   3.1 mm wide is left between the cutter's end and the facets.
 * Both results are closed and right for what was asked. The kernel is as it was: only this count changed.
 *
 * What keeps the exemption narrow:
 * - back to back only. The check takes two such faces for one plane within a hundred-thousandth of the result's size,
 *   and wants every corner of one that near the other's plane: at an angle of 5e-3 and more the strip can be no wider
 *   than four thousandths of the size (2 cm of a 5 m result). Faces that look the same way are given ten times the
 *   distance (20 cm), and such a pair inside a closed result is a pocket or a doubled skin: a problem as before.
 * - both faces flat to that distance, and a tenth of a millimetre wide or more: the angle is then the planes' own, not
 *   the rounding of a needle's corners (a float at 4 m is 2.4e-7 m apart: under 2.4e-3 of angle over 0.1 mm).
 * - the pieces' round sides here are 256 and 300 facets: 0.0105 and more between neighbours, twice the threshold.
 * Counted in a column of its own. Two faces of one plane - a doubled wall - are a problem as before. */
static bool meet_at_a_ridge(const Mesh &m, uint32_t a, uint32_t b, double size) {
  const FacePlane pa = face_plane(m, a), pb = face_plane(m, b);
  if (std::min(pa.width, pb.width) < 1e-4 || std::max(pa.off, pb.off) > 1e-5 * size) return false;
  const double along = pa.n[0] * pb.n[0] + pa.n[1] * pb.n[1] + pa.n[2] * pb.n[2];
  const double c[3] = {pa.n[1] * pb.n[2] - pa.n[2] * pb.n[1], pa.n[2] * pb.n[0] - pa.n[0] * pb.n[2], pa.n[0] * pb.n[1] - pa.n[1] * pb.n[0]};
  return along < 0.0 && std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]) > 5e-3;
}

/* One result judged. A problem: it is open, pinched or inside out, or two of its flat faces lie on top of each
 * other. Small faces of an unresolved place on top of others, flat faces that meet at a ridge, and faces of no area,
 * are counted apart. */
static void judge(uint32_t seed, const char *name, const Mesh &m, const bl::blockout::Faces &named, const bl::blockout::Report &r, Tally &t) {
  t.built++, t.skipped += r.skipped.size(), t.unresolved += r.unresolved;
  std::string bad = problem(m);
  std::vector<std::pair<uint32_t, uint32_t>> pairs;
  const AABB bb = m.bounds();
  const float size = length(bb.max - bb.min);
  if (m.face_count() && m.face_count() < 20000) meshops::overlapping_faces(m, 1e-4f * size, &pairs);
  else if (m.face_count()) t.unchecked++;  // (the pairwise check would take minutes: said in a note)
  bool small = false, ridge = false;
  for (const auto &pr : pairs) {
    const bool flat = named.flat[pr.first] && named.flat[pr.second];
    const bool at_ridge = flat && meet_at_a_ridge(m, pr.first, pr.second, size);
    if (flat && !at_ridge && bad.empty()) bad = "flat faces on top of each other";
    small = small || !flat;
    ridge = ridge || at_ridge;
  }
  size_t flat_area = 0;
  const double far = farthest(m);
  for (size_t f = 0; f < m.face_count(); f++) flat_area += no_area(m, f, far);
  t.problems += !bad.empty(), t.overlaps += small, t.ridges += ridge, t.no_area += flat_area > 0;
  if (!bad.empty() && t.first.empty()) t.first = strprintf("seed %u: %s", seed, bad.c_str());
  if (!std::getenv("BLENDITY_BLOCKOUT_DEBUG") || (bad.empty() && !small && !ridge && !flat_area && !r.unresolved)) return;
  std::printf("  seed %u (%s): %s%s%s%s%s\n", seed, name, bad.c_str(), small ? " small faces on top of others;" : "", ridge ? " flat faces meeting at a ridge;" : "", flat_area ? strprintf(" %zu faces of no area;", flat_area).c_str() : "",
              r.unresolved ? strprintf(" %zu unresolved", r.unresolved).c_str() : "");
}

/* The sweep: hostile arrangements on each path; a refusal is not a problem. */
static void sweep(Report &rep, const Options &o, bool crisp) {
  const char *one = std::getenv("BLENDITY_BLOCKOUT_SEED");  // (one arrangement alone, its pieces and its overlapping faces listed)
  const uint32_t cases = one ? (uint32_t)std::max(1, std::atoi(one)) : o.quick ? 200 : 2000;
  const char *cell_env = std::getenv("BLENDITY_BLOCKOUT_CELL");  // (the sweep at another cell, to see what a finer grid resolves)
  const float sweep_cell = cell_env && std::atof(cell_env) > 0.0 ? (float)std::atof(cell_env) : 0.08f;
  rep.table({"sweep", "arrangements", "built", "pieces skipped", "refused", "problems", "with small faces on top of others", "with flat faces meeting at a ridge", "with faces of no area", "unresolved places"});
  for (int kind = 0; kind < 12; kind++) {
    const Path path = kind < 6 ? Path::Crisp : Path::Field;
    const int what = kind % 6;
    if (path == Path::Crisp && !crisp) continue;
    const std::string name = std::string(path == Path::Crisp ? "crisp" : "field") + kWhatShort[what];
    Tally t;
    for (uint32_t seed = 1; seed <= cases; seed++) {
      if (one && (uint32_t)std::atoi(one) != seed) continue;
      const std::vector<Piece> pieces = arrangement(seed, what);
      bl::blockout::Settings st;
      st.path = path, st.cell = sweep_cell;
      Mesh m;
      bl::blockout::Report r;
      bl::blockout::Faces named;
      if (one) list_arrangement(seed, path, what, pieces, st);
      if (bl::blockout::build(pieces, st, m, &named, &r)) {
        judge(seed, name.c_str(), m, named, r, t);
        continue;
      }
      t.refused++;
      if (std::getenv("BLENDITY_BLOCKOUT_DEBUG")) std::printf("  seed %u (%s) refused: %s\n", seed, name.c_str(), r.why.c_str());
    }
    rep.row({std::string(path == Path::Crisp ? "crisp" : strprintf("field cell %g", sweep_cell)) + kWhat[what], std::to_string(cases), std::to_string(t.built), std::to_string(t.skipped),
             std::to_string(t.refused), std::to_string(t.problems), std::to_string(t.overlaps), std::to_string(t.ridges), std::to_string(t.no_area), std::to_string(t.unresolved)});
    if (!t.first.empty()) rep.note(name + ": the first problem is " + t.first);
    if (t.unchecked) rep.note(name + strprintf(": %zu results of 20,000 faces or more were not checked for faces on top of each other", t.unchecked));
  }
}

/* Round 477 (ADR 0129, task 0453): the Blockout object live in a headless editor with workers on, as the window runs it. `interior 100`
 * as objects (an Add and a Subtract box for each room, a door cut through each shared wall); the first door moved a little every
 * frame. The frame is the main thread's whole step_frame_headless (hash, swap, first render mesh of a landed result, the Scene
 * view); a landing is timed from the first move whose mesh had not yet been shown. The builds started, landed and cancelled, and the
 * kernel calls made on the main thread (none), say what the frames paid. */
static void live_row(Report &rep, const Options &o, bool cancel_stale, bool header, int gi = -1) {
  Editor ed;
  ed.init_headless(1280, 720);
  ed.set_async_jobs_for_test(true);
  ed.set_blockout_cancel_stale_for_test(cancel_stale);
  std::vector<GameObject *> roots;
  ed.scene().for_each([&](GameObject &g) {
    if (!g.parent) roots.push_back(&g);
  });
  ed.scene().destroy(roots);
  GameObject *bo = ed.scene().create("Blockout");
  bo->add<Blockout>();
  bo->add<MeshFilter>()->mesh = std::make_shared<Mesh>();
  bo->add<MeshRenderer>()->contribute_gi = gi == 1;  // (round 482: the rows with a Blockout that takes part in baked lighting - the Scene view lit, the realtime lightmap laid out for it)
  if (gi == 1) {  // (the worst case for the realtime lightmap layout: Realtime GI on and following every change, a sun to light the view)
    ed.scene().create("Sun")->add<Light>();
    ed.scene().lighting.realtime_gi = true;
    ed.scene().lighting.update_mode = 1;
  }
  const std::vector<Piece> src = interior(6, 28);
  std::vector<GameObject *> objs;
  for (const Piece &p : src) {
    GameObject *g = ed.scene().create("Piece", bo);
    BlockoutPiece *bp = g->add<BlockoutPiece>();
    bp->operation = p.op == Op::Subtract ? 1 : 0;
    bp->size = p.size;
    g->set_local_position(p.to_blockout.translation());
    objs.push_back(g);
  }
  ed.command("blockout wait");
  for (int i = 0; i < 3; i++) ed.step_frame_headless();  // (the first mesh's render mesh and the views' first frames)
  GameObject *door = objs[(size_t)6 * 6 * 2];
  const Vec3 home = door->local().position;
  const uint64_t started0 = ed.blockout_started_for_test(), landed0 = ed.blockout_landed_for_test(), cancelled0 = ed.blockout_cancelled_for_test(),
                 main0 = ed.blockout_main_builds_for_test();
  const int frames = o.quick ? 30 : 120;
  std::vector<double> frame_ms, landing_ms;
  uint64_t landed = landed0;
  double oldest = -1.0;
  const double start = now_seconds();
  for (int f = 0; f < frames; f++) {
    const double moved_at = now_seconds();
    if (oldest < 0.0) oldest = moved_at;
    door->set_local_position(home + Vec3(0.0f, 0.0f, 0.02f * (float)(f % 30)));
    ScopedTimer t;
    ed.step_frame_headless();
    frame_ms.push_back(t.ms());
    if (std::getenv("BLENDITY_BLOCKOUT_LIVE_DEBUG")) std::printf("  live frame %d: %.2f ms (started %llu, landed %llu)\n", f, frame_ms.back(), (unsigned long long)ed.blockout_started_for_test(), (unsigned long long)ed.blockout_landed_for_test());
    if (ed.blockout_landed_for_test() != landed) {
      landed = ed.blockout_landed_for_test();
      landing_ms.push_back((now_seconds() - oldest) * 1000.0);
      oldest = -1.0;
    }
    /* (A window's frames come every 16.7 ms: the drag lasts as long as a user's would, not as long as a headless frame's millisecond.) */
    const double due = start + 0.0167 * (double)(f + 1) - now_seconds();
    if (due > 0.0) std::this_thread::sleep_for(std::chrono::duration<double>(due));
  }
  const double drag_end = now_seconds();
  ed.command("blockout wait");
  const double settle_ms = (now_seconds() - drag_end) * 1000.0;
  std::sort(frame_ms.begin(), frame_ms.end());
  const double worst = frame_ms.empty() ? 0.0 : frame_ms.back();
  const double land_median = median(landing_ms), land_worst = landing_ms.empty() ? 0.0 : *std::max_element(landing_ms.begin(), landing_ms.end());
  const uint64_t main_builds = ed.blockout_main_builds_for_test() - main0;
  const bool ok = worst <= 16.7 && !landing_ms.empty() && land_median <= 100.0 && main_builds == 0;
  if (header)
    rep.table({"live: interior 100 in the editor, a door moved every frame for " + std::to_string(frames) + " frames (one every 16.7 ms)", "frame median ms", "frame worst ms",
               "move to landing median ms", "move to landing worst ms", "landings during the drag", "wait after the last move ms", "builds started", "landed", "overtaken by a newer move",
               "main-thread builds", "verdict"});
  const std::string what = gi >= 0 ? std::string(gi == 1 ? "Realtime GI on (Automatic), a sun, Contribute GI on" : "Contribute GI off, Realtime GI off, no sun") + ": the running build finishes and lands, then the newest starts"
                                   : cancel_stale ? "a newer snapshot cancels the running build (not shipped)" : "the running build finishes and lands, then the newest starts (ADR 0129)";
  rep.row({what, f2(median(frame_ms)), f2(worst), landing_ms.empty() ? "none" : f2(land_median), landing_ms.empty() ? "none" : f2(land_worst),
           std::to_string(landing_ms.size()), f2(settle_ms), std::to_string(ed.blockout_started_for_test() - started0), std::to_string(ed.blockout_landed_for_test() - landed0),
           std::to_string(ed.blockout_cancelled_for_test() - cancelled0), std::to_string(main_builds), ok ? "ok" : "OVER"});
}

/* Round 479 (task 0455): a face handle dragged, as real mouse events, in `interior 100` with workers on. The first piece (a room's Add
 * shell) is selected and its +X handle taken; the mouse then moves a pixel further each frame for 120 frames (one every 16.7 ms) and
 * lets go. Ctrl is held (snapping off): with it on, most frames leave the face on the same grid line - no change, no build - and a
 * landing would be timed from a mouse move that changed nothing. The frame is the main thread's whole step_frame_headless, with the handles' hit test and drawing in it; the landing is
 * timed as the live row does. The verdict is the live row's: no frame over 16.7 ms, a landing within 100 ms, no kernel call on the
 * main thread. */
static void handle_row(Report &rep, const Options &o) {
  Editor ed;
  ed.init_headless(1280, 720);
  ed.set_async_jobs_for_test(true);
  std::vector<GameObject *> roots;
  ed.scene().for_each([&](GameObject &g) {
    if (!g.parent) roots.push_back(&g);
  });
  ed.scene().destroy(roots);
  GameObject *bo = ed.scene().create("Blockout");
  bo->add<Blockout>();
  bo->add<MeshFilter>()->mesh = std::make_shared<Mesh>();
  bo->add<MeshRenderer>();
  const std::vector<Piece> src = interior(6, 28);
  std::vector<GameObject *> objs;
  for (const Piece &p : src) {
    GameObject *g = ed.scene().create("Piece", bo);
    BlockoutPiece *bp = g->add<BlockoutPiece>();
    bp->operation = p.op == Op::Subtract ? 1 : 0;
    bp->size = p.size;
    g->set_local_position(p.to_blockout.translation());
    objs.push_back(g);
  }
  const Vec3 at = objs[0]->world_position();
  ed.command(strprintf("camera 25 60 14 %g %g %g", at.x, at.y, at.z));
  ed.select_object(objs[0]->id);
  ed.command("blockout wait");
  for (int i = 0; i < 3; i++) ed.step_frame_headless();
  auto mouse = [](platform::EventType t, int x, int y) {
    platform::Event e;
    e.type = t, e.x = x, e.y = y, e.mods = platform::MOD_CTRL;
    return e;
  };
  int hx = 0, hy = 0;
  const bool found = ed.blockout_handle_pixel_for_test("+x", hx, hy);
  rep.table({"live: a face handle (the first shell box's +X face, Ctrl held: no snapping) dragged for " + std::string(o.quick ? "30" : "120") + " frames in interior 100 (one every 16.7 ms)", "frame median ms", "frame worst ms",
             "move to landing median ms", "move to landing worst ms", "landings during the drag", "wait after the last move ms", "builds started", "landed", "main-thread builds", "verdict"});
  if (!found) {
    rep.row({"no +X handle on the first piece in view", "", "", "", "", "", "", "", "", "", "OVER"});
    return;
  }
  const uint64_t started0 = ed.blockout_started_for_test(), landed0 = ed.blockout_landed_for_test(), main0 = ed.blockout_main_builds_for_test();
  ed.step_frame_headless({mouse(platform::EventType::MouseMove, hx, hy)});
  ed.step_frame_headless({mouse(platform::EventType::MouseDown, hx, hy)});
  const int frames = o.quick ? 30 : 120;
  std::vector<double> frame_ms, landing_ms;
  uint64_t landed = landed0;
  double oldest = -1.0;
  const double start = now_seconds();
  for (int f = 0; f < frames; f++) {
    const double moved_at = now_seconds();
    if (oldest < 0.0) oldest = moved_at;
    ScopedTimer t;
    ed.step_frame_headless({mouse(platform::EventType::MouseMove, hx + 1 + f % 40, hy)});
    frame_ms.push_back(t.ms());
    if (ed.blockout_landed_for_test() != landed) {
      landed = ed.blockout_landed_for_test();
      landing_ms.push_back((now_seconds() - oldest) * 1000.0);
      oldest = -1.0;
    }
    const double due = start + 0.0167 * (double)(f + 1) - now_seconds();
    if (due > 0.0) std::this_thread::sleep_for(std::chrono::duration<double>(due));
  }
  const double drag_end = now_seconds();
  ed.step_frame_headless({mouse(platform::EventType::MouseUp, hx + 1, hy)});
  ed.command("blockout wait");
  const double settle_ms = (now_seconds() - drag_end) * 1000.0;
  std::sort(frame_ms.begin(), frame_ms.end());
  const double worst = frame_ms.empty() ? 0.0 : frame_ms.back();
  const double land_median = median(landing_ms), land_worst = landing_ms.empty() ? 0.0 : *std::max_element(landing_ms.begin(), landing_ms.end());
  const uint64_t main_builds = ed.blockout_main_builds_for_test() - main0;
  const bool ok = worst <= 16.7 && !landing_ms.empty() && land_median <= 100.0 && main_builds == 0;
  rep.row({"the running build finishes and lands, then the newest starts (ADR 0129)", f2(median(frame_ms)), f2(worst), landing_ms.empty() ? "none" : f2(land_median),
           landing_ms.empty() ? "none" : f2(land_worst), std::to_string(landing_ms.size()), f2(settle_ms), std::to_string(ed.blockout_started_for_test() - started0),
           std::to_string(ed.blockout_landed_for_test() - landed0), std::to_string(main_builds), ok ? "ok" : "OVER"});
}

/* Round 481 (ADR 0130, task 0457): a blend is meshed in a box of its own and stitched to the rest. What one rounded column costs in
 * a building - `interior 100` as rooms, a column 0.25 m thick from floor to ceiling in the first room with a Blend radius of 0.15 m -
 * at each detail, against the same building with the column unblended; and the metaball case, where the boxes are the whole cluster. */
static Piece blend_column(float blend) {
  Piece p;
  p.id = 9001, p.shape = Shape::Cylinder, p.radius = 0.25f, p.height = 2.8f, p.segments = 24, p.blend = blend;
  p.to_blockout = Mat4::translate({1.0f, 0.0f, 1.0f});
  return p;
}

static void blend_rows(Report &rep, const Options &o) {
  static const char *const kDetail[3] = {"Low", "Medium", "High"};
  rep.table({"a blended column in interior 100 (rooms), radius 0.15 m", "ms / build", "boxes", "tries again", "built whole", "blend cell", "grid cells", "faces", "of them blended", "result", "verdict (100 ms)"});
  for (int detail = -1; detail < 3; detail++) {
    std::vector<Piece> pieces = interior_rooms(6, 28);
    pieces.push_back(blend_column(detail < 0 ? 0.0f : 0.15f));
    bl::blockout::Settings st;
    st.blend_detail = std::max(0, detail);
    std::vector<double> ms;
    Run r;
    for (int i = 0; i < (o.quick ? 1 : 5); i++) {
      r = on_worker(pieces, st);
      ms.push_back(r.ms);
    }
    size_t blended = 0, boxes = 0, retries = 0, whole = 0, cells = 0;
    float cell = 0.0f;
    for (uint32_t side : r.faces.side) blended += side == bl::blockout::kSideBlend;
    for (const bl::blockout::ClusterReport &c : r.rep.clusters) boxes += c.blend_regions, retries += c.seam_retries, whole += c.blend_whole, cells += c.cells, cell = std::max(cell, c.blend_cell);
    const std::string bad = r.ok ? problem(r.mesh) : "refused: " + r.rep.why;
    const double m = median(ms);
    rep.row({detail < 0 ? "the column unblended" : kDetail[detail], f2(m), std::to_string(boxes), std::to_string(retries), std::to_string(whole), detail < 0 ? "-" : strprintf("%.4f", cell),
             std::to_string(cells), std::to_string(r.mesh.face_count()), std::to_string(blended), bad.empty() ? "closed" : "PROBLEM: " + bad, detail < 0 ? "-" : m <= 100.0 ? "ok" : "over"});
  }
  rep.table({"blobs: 40 spheres and boxes, every one with a Blend radius of 0.2 m", "ms / build", "boxes", "built whole", "blend cell", "grid cells", "faces", "result"});
  for (int detail = 0; detail < 3; detail++) {
    std::vector<Piece> pieces = piles(o.quick ? 12 : 40, 3.0f, 11u);
    for (Piece &p : pieces) p.blend = 0.2f;
    bl::blockout::Settings st;
    st.blend_detail = detail, st.max_cells = 64000000;
    std::vector<double> ms;
    Run r;
    for (int i = 0; i < (o.quick ? 1 : 3); i++) {
      r = on_worker(pieces, st);
      ms.push_back(r.ms);
    }
    size_t boxes = 0, whole = 0, cells = 0;
    float cell = 0.0f;
    for (const bl::blockout::ClusterReport &c : r.rep.clusters) boxes += c.blend_regions, whole += c.blend_whole, cells += c.cells, cell = std::max(cell, c.blend_cell);
    const std::string bad = r.ok ? problem(r.mesh) : "refused: " + r.rep.why;
    rep.row({kDetail[detail], f2(median(ms)), std::to_string(boxes), std::to_string(whole), strprintf("%.4f", cell), std::to_string(cells), std::to_string(r.mesh.face_count()),
             bad.empty() ? "closed" : "PROBLEM: " + bad});
  }
}

/* The hostile arrangements of "any pieces" with Blend radii on them: none, below the detail, ordinary, larger than the piece, not a
 * number. The same judge as the sweep's; and how the seams went. (500 arrangements a path, 50 quick: an arrangement with a blend
 * costs a fine grid, where the sweep's cost a boolean.) */
static void blend_sweep(Report &rep, const Options &o, bool crisp) {
  const char *one = std::getenv("BLENDITY_BLOCKOUT_BLEND_SEED");
  const uint32_t cases = one ? (uint32_t)std::max(1, std::atoi(one)) : o.quick ? 50 : 500;
  rep.table({"sweep with Blend radii", "arrangements", "built", "refused", "problems", "with small faces on top of others", "with faces of no area", "boxes stitched", "clusters built whole",
             "clusters whose seam would not close", "tries again", "pieces below the detail", "clusters built without their blends"});
  for (int k = 0; k < 2; k++) {
    const Path path = k == 0 ? Path::Auto : Path::Field;
    if (k == 0 && !crisp) continue;  // (without the exact path Auto is the grid: the second row)
    const std::string name = path == Path::Auto ? "the rest on the exact path" : "the rest on the grid (cell 0.08)";
    Tally t;
    size_t boxes = 0, whole = 0, fallback = 0, retries = 0, below = 0, dropped = 0;
    for (uint32_t seed = 1; seed <= cases; seed++) {
      if (one && (uint32_t)std::atoi(one) != seed) continue;
      std::vector<Piece> pieces = hostile(seed, false);
      Rand r{seed * 40503u + 481u};
      for (Piece &p : pieces) {
        const int pick = r.pick(10);
        p.blend = pick < 3 ? 0.0f : pick == 3 ? 0.001f : pick < 8 ? 0.05f + 0.05f * (float)r.pick(6) : pick == 8 ? 5.0f : std::numeric_limits<float>::quiet_NaN();
      }
      bl::blockout::Settings st;
      st.path = path, st.cell = 0.08f, st.blend_detail = r.pick(3);
      Mesh m;
      bl::blockout::Report rp;
      bl::blockout::Faces named;
      if (!bl::blockout::build(pieces, st, m, &named, &rp)) {
        t.refused++;
        if (std::getenv("BLENDITY_BLOCKOUT_DEBUG")) std::printf("  blend seed %u (%s) refused: %s\n", seed, name.c_str(), rp.why.c_str());
        continue;
      }
      judge(seed, name.c_str(), m, named, rp, t);
      for (const bl::blockout::ClusterReport &c : rp.clusters)
        if (c.blend_dropped && std::getenv("BLENDITY_BLOCKOUT_DEBUG")) std::printf("  blend seed %u (%s) built without its blends: %s\n", seed, name.c_str(), c.seam_why.c_str());
      for (const bl::blockout::ClusterReport &c : rp.clusters) boxes += c.blend_regions, whole += c.blend_whole, fallback += c.blend_fallback, retries += c.seam_retries, below += c.blend_below, dropped += c.blend_dropped;
    }
    rep.row({name, std::to_string(cases), std::to_string(t.built), std::to_string(t.refused), std::to_string(t.problems), std::to_string(t.overlaps), std::to_string(t.no_area), std::to_string(boxes),
             std::to_string(whole), std::to_string(fallback), std::to_string(retries), std::to_string(below), std::to_string(dropped)});
    if (!t.first.empty()) rep.note(name + ": the first problem is " + t.first);
    if (t.unchecked) rep.note(name + strprintf(": %zu results of 20,000 faces or more were not checked for faces on top of each other", t.unchecked));
  }
}

/* Round 482: what the slots, the UVs and the shading add to a build - one pass over the result's faces, made on the worker with the mesh. interior rooms 100
 * with the six materials and a piece override on every tenth piece; the timed part is blockout_finish_mesh alone, against the build it follows. */
static void surface_row(Report &rep, const Options &o) {
  rep.table({"slots, UVs and shading on the result (round 482): interior rooms 100", "build ms", "slots + UVs ms", "share of the build %", "faces", "corners", "slots", "verdict"});
  for (int n : o.quick ? std::vector<int>{28} : std::vector<int>{28, 112}) {
    const int rooms = n == 28 ? 6 : 12;
    const std::vector<Piece> pieces = interior_rooms(rooms, n);
    bl::blockout::Settings st;
    std::vector<double> build_ms, finish_ms;
    size_t faces = 0, corners = 0, slot_count = 0;
    for (int i = 0; i < (o.quick ? 1 : 5); i++) {
      Run r = on_worker(pieces, st);
      if (!r.ok) continue;
      bl::BlockoutSurface surface;
      surface.scale = 0.5f;
      for (int k = 0; k < bl::kBlockoutClasses; k++) surface.materials[k] = strprintf("Assets/Materials/Stress%d.mat", k);
      for (size_t p = 0; p < pieces.size(); p++) {
        bl::BlockoutSurface::Piece sp;
        sp.id = pieces[p].id;
        if (p % 10 == 0) sp.materials[bl::kClassWalls] = "Assets/Materials/StressOverride.mat", sp.offset_u = 0.25f, sp.rotation = 15.0f;
        surface.pieces.push_back(sp);
      }
      ScopedTimer t;
      const std::vector<std::string> slots = bl::blockout_finish_mesh(r.mesh, r.faces, pieces, surface);
      finish_ms.push_back(t.ms()), build_ms.push_back(r.ms);
      faces = r.mesh.face_count(), corners = r.mesh.corner_count(), slot_count = slots.size();
    }
    const double b = median(build_ms), f = median(finish_ms);
    rep.row({strprintf("interior rooms %d", n == 28 ? 100 : 400), f2(b), f2(f), strprintf("%.1f", b > 0.0 ? 100.0 * f / b : 0.0), std::to_string(faces), std::to_string(corners),
             std::to_string(slot_count), "-"});
  }
}

}  // namespace bkstress

static void test_blockout(Report &rep, const Options &o) {
  using namespace bkstress;
  const bool crisp = bl::blockout::crisp_available();
  rep.title("Blockout kernel (round 476, ADR 0128)",
            strprintf("blockout::build by its two paths: crisp (Manifold; %s) and field (the grid, at the cells named). `interior N`: rooms with 0.2 m walls "
                      "and doors, N pieces, one cluster. `scatter N`: N boxes, cylinders and spheres in piles of four. `heap 100`: 100 of them through one "
                      "spot. Timed as the editor will call it: on a thread of its own inside JobSystem::run_background. ms / build: the whole Blockout, the "
                      "median of %d. moved: after one piece moved, the cache warm. until drawn: that plus the result's first render mesh; the verdict is "
                      "ok up to 100 ms on the 100-piece rows.",
                      crisp ? "in this build" : "not in this build", o.quick ? 1 : 5));

  /* (The live row alone: BLENDITY_BLOCKOUT_LIVE=1; BLENDITY_BLOCKOUT_LIVE_DEBUG=1 lists its frames.) */
  if (std::getenv("BLENDITY_BLOCKOUT_LIVE")) return live_row(rep, o, false, true), live_row(rep, o, true, false);
  /* (One arrangement of the sweep alone, or the sweep alone - at another cell, say: no timed rows.) */
  if (std::getenv("BLENDITY_BLOCKOUT_SEED") || std::getenv("BLENDITY_BLOCKOUT_SWEEP")) return sweep(rep, o, crisp);
  /* The first build in the process, before anything else here has run the kernel. */
  std::vector<Scene3> scenes;
  const int sizes[3][3] = {{2, 2, 10}, {6, 28, 100}, {12, 112, 400}};
  for (const int *sz : sizes) {
    if (o.quick && sz[2] > 100) continue;
    Scene3 sc;
    sc.name = strprintf("interior %d", sz[2]);
    sc.pieces = interior(sz[0], sz[1]);
    sc.volume = interior_volume(sz[0], sz[1]);
    sc.moved = (size_t)sz[0] * (size_t)sz[0] * 2;  // (the first door, slid along its wall)
    sc.step = {0.0f, 0.0f, 0.3f};
    sc.verdict = sz[2] == 100;
    scenes.push_back(sc);
    /* The same building from room pieces (round 478): the rooms come first, the doors after them. */
    Scene3 rooms;
    rooms.name = strprintf("interior rooms %d", sz[2]);
    rooms.pieces = interior_rooms(sz[0], sz[1]);
    rooms.volume = sc.volume;
    rooms.moved = (size_t)sz[0] * (size_t)sz[0];
    rooms.step = sc.step;
    rooms.verdict = sz[2] == 100;
    scenes.push_back(rooms);
  }
  /* Round 480: a staircase against the first room's outer wall and an arch through the last room's, added to interior 100 - the kernel's cost follows the
   * solids (8 steps and 12 blocks more), not the pieces. */
  {
    Scene3 sc;
    sc.name = "interior 100 + stairs + arch";
    sc.pieces = interior(6, 28);
    Piece stairs;
    stairs.id = sc.pieces.size() + 1, stairs.shape = Shape::Stairs, stairs.size = {2.0f, 1.5f, 3.0f}, stairs.steps = 8;
    stairs.to_blockout = Mat4::trs({-3.1f, -0.85f, 2.0f}, Quat(), Vec3(1.0f));
    Piece arch;
    arch.id = sc.pieces.size() + 2, arch.shape = Shape::Arch, arch.radius = 1.5f, arch.inner_radius = 1.0f, arch.angle = 180.0f, arch.segments = 12, arch.height = 0.4f;
    arch.to_blockout = Mat4::trs({23.4f, -1.6f, 20.0f}, Quat(), Vec3(1.0f));
    sc.moved = sc.pieces.size();  // (the staircase, slid along the wall)
    sc.pieces.push_back(stairs);
    sc.pieces.push_back(arch);
    sc.step = {0.0f, 0.0f, 0.3f};
    sc.verdict = !o.quick;
    scenes.push_back(sc);
  }
  /* Round 480 (test engineer): a 10k-face mesh as a piece in interior 100 (built from room pieces, so an Add piece standing in a room stays): a torus of 128 x 80
   * quads lying on the first room's floor, sunk 0.1 m into it, and slid 0.2 m along x between builds. The row is a measurement: the verdict is "-". */
  {
    Scene3 sc;
    sc.name = "interior rooms 100 + a 10k-face mesh";
    sc.pieces = interior_rooms(6, 28);
    Piece m;
    m.id = sc.pieces.size() + 1, m.shape = Shape::Mesh, m.mesh = bl::primitives::torus(0.5f, 0.2f, 128, 80), m.mesh_key = 1;
    m.to_blockout = Mat4::trs({0.0f, -1.1f, 0.0f}, Quat(), Vec3(2.0f));
    sc.moved = sc.pieces.size();
    sc.pieces.push_back(m);
    sc.step = {0.2f, 0.0f, 0.0f};
    scenes.push_back(sc);
  }
  for (int n : {10, 100, 400}) {
    if (o.quick && n > 100) continue;
    Scene3 sc;
    sc.name = strprintf("scatter %d", n);
    sc.pieces = piles(n, 6.0f, 7u);
    sc.step = {0.3f, 0.0f, 0.0f};
    sc.verdict = n == 100;
    scenes.push_back(sc);
  }
  if (!o.quick) {
    Scene3 sc;
    sc.name = "heap 100";
    sc.pieces = piles(100, 0.0f, 11u);
    sc.step = {0.3f, 0.0f, 0.0f};
    scenes.push_back(sc);
  }
  rep.table({"first build in the process", "ms", "then (median of 3)"});
  for (Path path : {Path::Crisp, Path::Field}) {
    if (path == Path::Crisp && !crisp) continue;
    bl::blockout::Settings st;
    st.path = path, st.cell = 0.1f;
    const std::vector<Piece> pieces = interior(6, 28);
    const Run first = on_worker(pieces, st);
    std::vector<double> then;
    for (int i = 0; i < 3; i++) then.push_back(on_worker(pieces, st).ms);
    rep.row({path == Path::Crisp ? "interior 100, crisp" : "interior 100, field cell 0.1", f2(first.ms), f2(median(then))});
  }

  rep.table({"row", "ms / build", "moved ms", "render mesh ms", "until drawn ms", "solids ms", "boolean or grid ms", "faces ms", "clusters", "faces", "triangles", "points",
             "cells", "volume error %", "flatness m", "corner error m", "unresolved", "verdict"});
  const std::vector<float> cells = o.quick ? std::vector<float>{0.1f} : std::vector<float>{0.2f, 0.1f, 0.05f};
  for (const Scene3 &sc : scenes) {
    Run reference;
    if (crisp) {
      bl::blockout::Settings st;
      st.path = Path::Crisp;
      reference = on_worker(sc.pieces, st);
      row(rep, o, sc, Path::Crisp, 0.1f, nullptr);
    }
    for (float cell : cells) row(rep, o, sc, Path::Field, cell, crisp && reference.ok ? &reference.mesh : nullptr);
  }

  /* Round 478: the room-built building is the box-pair one - the same faces, points and volume on each path. */
  rep.table({"interior 100 from rooms against from box pairs", "faces (boxes / rooms)", "points (boxes / rooms)", "volume (boxes / rooms)", "pieces (boxes / rooms)", "verdict"});
  for (Path path : {Path::Crisp, Path::Field}) {
    if (path == Path::Crisp && !crisp) continue;
    bl::blockout::Settings st;
    st.path = path, st.cell = 0.1f;
    const std::vector<Piece> boxes = interior(6, 28), rooms = interior_rooms(6, 28);
    const Run a = on_worker(boxes, st), b = on_worker(rooms, st);
    const double va = a.ok ? ppstress::signed_volume(a.mesh) : 0.0, vb = b.ok ? ppstress::signed_volume(b.mesh) : 0.0;
    const bool same = a.ok && b.ok && a.mesh.face_count() == b.mesh.face_count() && a.mesh.vert_count() == b.mesh.vert_count() && std::fabs(va - vb) <= 1e-4 * std::max(1.0, std::fabs(va));
    rep.row({path == Path::Crisp ? "crisp" : "field cell 0.1", strprintf("%zu / %zu", a.mesh.face_count(), b.mesh.face_count()), strprintf("%zu / %zu", a.mesh.vert_count(), b.mesh.vert_count()),
             strprintf("%.4f / %.4f", va, vb), strprintf("%zu / %zu", boxes.size(), rooms.size()), same ? "same" : "DIFFERS"});
  }

  /* The same build from the calling thread, with every thread. */
  rep.table({"called from the main thread", "ms / build"});
  for (Path path : {Path::Crisp, Path::Field}) {
    if (path == Path::Crisp && !crisp) continue;
    bl::blockout::Settings st;
    st.path = path, st.cell = 0.1f;
    const std::vector<Piece> pieces = interior(6, 28);
    std::vector<double> ms;
    for (int i = 0; i < (o.quick ? 1 : 5); i++) {
      Mesh m;
      ScopedTimer t;
      (void)bl::blockout::build(pieces, st, m);
      ms.push_back(t.ms());
    }
    rep.row({path == Path::Crisp ? "interior 100, crisp" : "interior 100, field cell 0.1", f2(median(ms))});
  }

  /* A Blend radius on every fifth piece of the piles: the field path, whatever the build. */
  rep.table({"blended: scatter 100, a Blend radius of 0.25 m on every fifth piece", "ms / build", "clusters", "faces", "of them blended", "triangles", "points", "result"});
  for (float cell : cells) {
    std::vector<Piece> pieces = piles(o.quick ? 20 : 100, 6.0f, 7u);
    for (size_t i = 0; i < pieces.size(); i += 5) pieces[i].blend = 0.25f;
    bl::blockout::Settings st;
    st.cell = cell, st.max_cells = 64000000;
    std::vector<double> ms;
    Run r;
    for (int i = 0; i < (o.quick ? 1 : 3); i++) {
      r = on_worker(pieces, st);
      ms.push_back(r.ms);
    }
    size_t blended = 0;
    for (uint32_t side : r.faces.side) blended += side == bl::blockout::kSideBlend;
    const std::string bad = r.ok ? problem(r.mesh) : "refused: " + r.rep.why;
    rep.row({strprintf("cell %g", cell), f2(median(ms)), std::to_string(r.rep.clusters.size()), std::to_string(r.mesh.face_count()), std::to_string(blended),
             std::to_string(r.ok ? r.mesh.render_mesh().tri_count() : 0), std::to_string(r.mesh.vert_count()), bad.empty() ? "closed" : "PROBLEM: " + bad});
  }

  live_row(rep, o, false, true);
  live_row(rep, o, true, false);
  handle_row(rep, o);
  blend_rows(rep, o);
  surface_row(rep, o);
  live_row(rep, o, false, true, 0);  // (round 482: the same drag with the Blockout in baked lighting, against without)
  live_row(rep, o, false, false, 1);
  sweep(rep, o, crisp);
  blend_sweep(rep, o, crisp);
}

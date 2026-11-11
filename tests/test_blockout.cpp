// SPDX-License-Identifier: GPL-2.0-or-later
// Blockout's kernel (round 476, task 0452, ADR 0128): pieces built into one closed mesh, by the crisp path (Manifold,
// where the build has Blender's libraries) and by the field path (every build). No editor here: blockout::build is a
// function of its pieces.
#include "test_common.h"

#include "../src/scene/blockout.h"

#include <array>
#include <limits>

using namespace bl;

namespace {

using blockout::Op;
using blockout::Path;
using blockout::Piece;
using blockout::Shape;

Piece box_piece(uint64_t id, Vec3 size, Vec3 at, Op op = Op::Add, Quat turn = Quat()) {
  Piece p;
  p.id = id, p.shape = Shape::Box, p.op = op, p.size = size;
  p.to_blockout = Mat4::trs(at, turn, Vec3(1.0f));
  return p;
}
Piece cylinder_piece(uint64_t id, float radius, float height, int segments, Vec3 at, Op op = Op::Add, Quat turn = Quat()) {
  Piece p;
  p.id = id, p.shape = Shape::Cylinder, p.op = op, p.radius = radius, p.height = height, p.segments = segments;
  p.to_blockout = Mat4::trs(at, turn, Vec3(1.0f));
  return p;
}
Piece sphere_piece(uint64_t id, float radius, int segments, int rings, Vec3 at, Op op = Op::Add) {
  Piece p;
  p.id = id, p.shape = Shape::Sphere, p.op = op, p.radius = radius, p.segments = segments, p.rings = rings;
  p.to_blockout = Mat4::translate(at);
  return p;
}

/* The paths this build has: the field path always, the crisp path with Blender's libraries. */
std::vector<Path> r476_paths() {
  std::vector<Path> p{Path::Field};
  if (blockout::crisp_available()) p.push_back(Path::Crisp);
  return p;
}
const char *r476_name(Path p) { return p == Path::Crisp ? "crisp" : p == Path::Field ? "field" : "auto"; }

struct R476Built {
  bool ok = false;
  Mesh mesh;
  blockout::Faces faces;
  blockout::Report report;
};
R476Built r476_build(const std::vector<Piece> &pieces, Path path, float cell = 0.1f, blockout::Cache *cache = nullptr) {
  R476Built b;
  blockout::Settings st;
  st.path = path, st.cell = cell;
  b.ok = blockout::build(pieces, st, b.mesh, &b.faces, &b.report, cache);
  return b;
}

/* Faces of no area: thinner - their area over their longest edge - than eight steps of a 32-bit position at the
 * mesh's farthest coordinate. (In doubles, and as a width: three points in a line that the floats hold a step off
 * the line, metres from the origin, are still caught; a small piece's own small faces are not.) */
size_t r476_no_area(const Mesh &m) {
  double far = 0.0;
  for (const Vec3 &p : m.positions) far = std::max(far, (double)std::max(std::fabs(p.x), std::max(std::fabs(p.y), std::fabs(p.z))));
  const double limit = 8.0 * 1.1920929e-7 * far;
  size_t n = 0;
  for (size_t f = 0; f < m.face_count(); f++) {
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
    n += !(longest > 0.0) || area / longest < limit;
  }
  return n;
}

/* A result a user could keep: closed, every edge on two faces, facing out, no two faces on top of each other, no
 * face of no area. */
bool r476_sound(const Mesh &m, const char *what) {
  std::string why;
  const bool valid = structurally_valid(m, &why), closed = closed_manifold(m);
  const size_t over = overlapping_face_pairs(m), fight = meshops::zfight_pairs(m).size(), thin = r476_no_area(m);
  const bool out = m.face_count() == 0 || signed_volume(m) > 0.0f;
  if (!valid || !closed || over || fight || thin || !out)
    std::printf("    %s: valid %d (%s) closed %d overlapping %zu zfight %zu no area %zu outward %d\n", what, (int)valid, why.c_str(), (int)closed, over, fight, thin, (int)out);
  return valid && closed && !over && !fight && !thin && out;
}

double r476_volume(const Mesh &m) {
  double v = 0;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t *fv = m.face_verts(f);
    for (uint32_t i = 1; i + 1 < m.face_size(f); i++) {
      const Vec3 a = m.positions[fv[0]], b = m.positions[fv[i]], c = m.positions[fv[i + 1]];
      v += (double)a.x * ((double)b.y * c.z - (double)b.z * c.y) + (double)a.y * ((double)b.z * c.x - (double)b.x * c.z) + (double)a.z * ((double)b.x * c.y - (double)b.y * c.x);
    }
  }
  return v / 6.0;
}

/* How far the faces that name a box's side lie from that side, at worst (the pieces here are boxes). */
double r476_flatness(const R476Built &b, const std::vector<Piece> &pieces) {
  double worst = 0.0;
  for (size_t f = 0; f < b.mesh.face_count(); f++) {
    const Piece *p = nullptr;
    for (const Piece &q : pieces)
      if (q.id == b.faces.piece[f]) p = &q;
    if (!p || p->shape != Shape::Box || b.faces.side[f] > 5) continue;
    const int axis = (int)b.faces.side[f] / 2;
    Vec3 local(0.0f);
    local[axis] = (b.faces.side[f] & 1 ? 0.5f : -0.5f) * p->size[axis];
    Vec3 dir(0.0f);
    dir[axis] = 1.0f;
    const Vec3 on = p->to_blockout.point(local), n = normalize(p->to_blockout.inverse().transposed().dir(dir));
    for (uint32_t i = 0; i < b.mesh.face_size(f); i++) worst = std::max(worst, (double)std::fabs(dot(n, b.mesh.positions[b.mesh.face_verts(f)[i]] - on)));
  }
  return worst;
}

bool r476_same_mesh(const R476Built &a, const R476Built &b) {
  return a.mesh.positions.size() == b.mesh.positions.size() && a.mesh.corner_verts == b.mesh.corner_verts && a.mesh.face_offsets == b.mesh.face_offsets &&
         std::memcmp(a.mesh.positions.data(), b.mesh.positions.data(), a.mesh.positions.size() * sizeof(Vec3)) == 0 && a.faces.piece == b.faces.piece &&
         a.faces.side == b.faces.side;
}

/* The same faces, whatever their order and whichever corner each begins at: every face of one result is in the other
 * with the same corners (to a hundredth of a millimetre). What "the field path gives the crisp path's faces" means.
 * (The pieces the faces name are not compared: where faces of two pieces lie in one plane on top of each other, which
 * of the two the result's face names is each path's own choice.) */
bool r476_same_shape(const R476Built &a, const R476Built &b) {
  auto faces_of = [](const R476Built &r) {
    std::vector<std::vector<int64_t>> out;
    for (size_t f = 0; f < r.mesh.face_count(); f++) {
      const uint32_t n = r.mesh.face_size(f);
      std::vector<std::array<int64_t, 3>> corners;
      for (uint32_t i = 0; i < n; i++) {
        const Vec3 p = r.mesh.positions[r.mesh.face_verts(f)[i]];
        corners.push_back({(int64_t)std::llround((double)p.x * 1e5), (int64_t)std::llround((double)p.y * 1e5), (int64_t)std::llround((double)p.z * 1e5)});
      }
      const size_t first = (size_t)(std::min_element(corners.begin(), corners.end()) - corners.begin());
      std::vector<int64_t> key;
      for (uint32_t i = 0; i < n; i++)
        for (int64_t v : corners[(first + i) % n]) key.push_back(v);
      out.push_back(std::move(key));
    }
    std::sort(out.begin(), out.end());
    return out;
  };
  return a.mesh.vert_count() == b.mesh.vert_count() && faces_of(a) == faces_of(b);
}

/* A piece placed by its axes written out - with the last-bit noise a float matrix of right angles carries. */
Piece r476_placed(Piece p, Vec3 at, Vec3 x, Vec3 y, Vec3 z) {
  p.to_blockout = Mat4::identity();
  for (int i = 0; i < 3; i++) p.to_blockout.m[i] = x[i], p.to_blockout.m[4 + i] = y[i], p.to_blockout.m[8 + i] = z[i], p.to_blockout.m[12 + i] = at[i];
  return p;
}

/* Rooms on a square grid: an Add box with a Subtract box inside it (walls, floor and ceiling 0.2 m), 4 m apart so
 * neighbours share their walls, and door cuts through the first `doors` shared walls. One cluster. */
std::vector<Piece> r476_interior(int n, int doors) {
  std::vector<Piece> p;
  uint64_t id = 1;
  for (int j = 0; j < n; j++)
    for (int i = 0; i < n; i++) {
      p.push_back(box_piece(id++, {4.2f, 3.2f, 4.2f}, {4.0f * (float)i, 0.0f, 4.0f * (float)j}));
      p.push_back(box_piece(id++, {3.8f, 2.8f, 3.8f}, {4.0f * (float)i, 0.0f, 4.0f * (float)j}, Op::Subtract));
    }
  for (int j = 0; j < n && doors > 0; j++)
    for (int i = 0; i + 1 < n && doors > 0; i++, doors--)
      p.push_back(box_piece(id++, {0.6f, 2.1f, 0.9f}, {4.0f * (float)i + 2.0f, -0.35f, 4.0f * (float)j}, Op::Subtract));
  for (int j = 0; j + 1 < n && doors > 0; j++)
    for (int i = 0; i < n && doors > 0; i++, doors--)
      p.push_back(box_piece(id++, {0.9f, 2.1f, 0.6f}, {4.0f * (float)i, -0.35f, 4.0f * (float)j + 2.0f}, Op::Subtract));
  return p;
}
double r476_interior_volume(int n, int doors) {
  const double side = 4.0 * n + 0.2;
  return side * side * 3.2 - (double)n * n * 3.8 * 3.8 * 2.8 - (double)doors * 0.9 * 2.1 * 0.2;
}

}  // namespace

static void tests_round476() {
  test("blockout: one box is six faces and eight corners, on each path", [] {
    for (Path path : r476_paths()) {
      const std::vector<Piece> pieces{box_piece(7, {2, 1, 3}, {0.25f, 0.5f, -0.125f})};
      const R476Built b = r476_build(pieces, path);
      CHECK(b.ok);
      CHECK(r476_sound(b.mesh, r476_name(path)));
      CHECK(b.mesh.face_count() == 6);
      CHECK(b.mesh.vert_count() == 8);
      CHECK_NEAR(r476_volume(b.mesh), 6.0, 6.0e-5);
      CHECK(r476_flatness(b, pieces) <= 1e-5 * 3.75);
      CHECK(b.faces.piece.size() == 6 && b.faces.side.size() == 6);
      unsigned sides = 0;
      for (size_t f = 0; f < b.faces.side.size(); f++) {
        CHECK(b.faces.piece[f] == 7);
        if (b.faces.side[f] < 6) sides |= 1u << b.faces.side[f];
      }
      CHECK(sides == 63u);  // (each of the six sides named once)
      CHECK(b.report.clusters.size() == 1 && b.report.clusters[0].path == path && b.report.built == 1);
    }
  });

  test("blockout: a turned box keeps its six faces and its corners", [] {
    for (Path path : r476_paths())
      for (Vec3 turn : {Vec3(0, 30, 0), Vec3(0, 45, 0), Vec3(20, 30, 40), Vec3(45, 0, 45)}) {
        const std::vector<Piece> pieces{box_piece(1, {2, 1, 3}, {0.3f, 0.1f, 0.2f}, Op::Add, Quat::euler(turn))};
        const R476Built b = r476_build(pieces, path);
        CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
        CHECK(b.mesh.face_count() == 6);
        CHECK(b.mesh.vert_count() == 8);
        CHECK_NEAR(r476_volume(b.mesh), 6.0, 6.0e-4);
        CHECK(r476_flatness(b, pieces) <= 1e-5 * 3.75);
        /* Each corner of the piece is a corner of the result. */
        double worst = 0.0;
        for (int i = 0; i < 8; i++) {
          const Vec3 corner = pieces[0].to_blockout.point({i & 1 ? 1.0f : -1.0f, i & 2 ? 0.5f : -0.5f, i & 4 ? 1.5f : -1.5f});
          double best = 1e9;
          for (const Vec3 &p : b.mesh.positions) best = std::min(best, (double)length(p - corner));
          worst = std::max(worst, best);
        }
        CHECK(worst <= 1e-5 * 3.75);
      }
  });

  test("blockout: two overlapping boxes are one solid of the closed-form volume", [] {
    for (Path path : r476_paths()) {
      const std::vector<Piece> pieces{box_piece(1, {2, 2, 2}, {0, 0, 0}), box_piece(2, {2, 2, 2}, {1, 0.5f, 0.25f})};
      const R476Built b = r476_build(pieces, path);
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      CHECK_NEAR(r476_volume(b.mesh), 16.0 - 1.0 * 1.5 * 1.75, 16.0e-5);
      CHECK(r476_flatness(b, pieces) <= 1e-5 * 5.0);
      CHECK(b.report.clusters.size() == 1 && b.report.clusters[0].pieces == 2);
    }
  });

  test("blockout: a box cut by a box leaves a pocket whose faces name the cutter", [] {
    for (Path path : r476_paths()) {
      const std::vector<Piece> pieces{box_piece(1, {4, 2, 4}, {0, 0, 0}), box_piece(2, {1, 1, 1}, {0.5f, 1.0f, 0.25f}, Op::Subtract)};
      const R476Built b = r476_build(pieces, path);
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      CHECK_NEAR(r476_volume(b.mesh), 32.0 - 0.5, 32.0e-5);
      size_t of_cutter = 0;
      for (size_t f = 0; f < b.mesh.face_count(); f++) {
        if (b.faces.piece[f] != 2) continue;
        of_cutter++;
        CHECK(b.faces.side[f] != 3);  // (its top is above the box: no face there)
      }
      CHECK(of_cutter == 5);  // (four walls and a floor)
      CHECK(r476_flatness(b, pieces) <= 1e-5 * 6.0);
    }
  });

  test("blockout: a cut flush with a face goes through it, and a hole right through is a hole", [] {
    for (Path path : r476_paths()) {
      /* A pocket whose top is exactly the box's top: open, with no skin over it. */
      R476Built b = r476_build({box_piece(1, {4, 2, 4}, {0, 0, 0}), box_piece(2, {1, 1, 1}, {0, 0.5f, 0}, Op::Subtract)}, path);
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      CHECK_NEAR(r476_volume(b.mesh), 32.0 - 1.0, 32.0e-5);
      /* A cutter from below the floor to above the top: a hole, the solid a ring. */
      b = r476_build({box_piece(1, {4, 2, 4}, {0, 0, 0}), box_piece(2, {1, 4, 1}, {0, 0, 0}, Op::Subtract)}, path);
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      CHECK_NEAR(r476_volume(b.mesh), 32.0 - 2.0, 32.0e-5);
      CHECK(euler_characteristic(b.mesh) == 0);
    }
  });

  test("blockout: boxes that share a face become one box", [] {
    for (Path path : r476_paths()) {
      const R476Built b = r476_build({box_piece(1, {2, 1, 1}, {0, 0, 0}), box_piece(2, {2, 1, 1}, {2, 0, 0})}, path);
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      CHECK(b.mesh.face_count() == 6);
      CHECK(b.mesh.vert_count() == 8);
      CHECK_NEAR(r476_volume(b.mesh), 4.0, 4.0e-5);
      for (size_t f = 0; f < b.mesh.face_count(); f++)
        if (b.faces.side[f] >= 2) CHECK(b.faces.piece[f] == 1);  // (merged faces name the first piece)
    }
  });

  test("blockout: a door cut leaves the wall one face, a window leaves faces without holes", [] {
    for (Path path : r476_paths()) {
      /* A wall 4 x 3 x 0.25 with a door from the floor up: the wall's two big faces stay one face each. */
      R476Built b = r476_build({box_piece(1, {4, 3, 0.25f}, {0, 1.5f, 0}), box_piece(2, {1, 2, 1}, {0.5f, 1.0f, 0}, Op::Subtract)}, path);
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      CHECK_NEAR(r476_volume(b.mesh), 3.0 - 0.5, 3.0e-5);
      size_t front = 0;
      for (size_t f = 0; f < b.mesh.face_count(); f++) {
        if (b.faces.piece[f] != 1 || b.faces.side[f] != 5) continue;
        front++;
        CHECK(b.mesh.face_size(f) == 8);
      }
      CHECK(front == 1);
      /* A window in the middle of the wall: the wall's face has a hole, the result's faces have none. */
      b = r476_build({box_piece(1, {4, 3, 0.25f}, {0, 1.5f, 0}), box_piece(2, {1, 1, 1}, {0.5f, 1.5f, 0}, Op::Subtract)}, path);
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      CHECK_NEAR(r476_volume(b.mesh), 3.0 - 0.25, 3.0e-5);
      CHECK(euler_characteristic(b.mesh) == 0);
      CHECK(bad_triangulations(b.mesh) == 0);
    }
  });

  test("blockout: pieces that cannot be used are skipped and named, the others built", [] {
    for (Path path : r476_paths()) {
      std::vector<Piece> pieces{box_piece(1, {1, 1, 1}, {0, 0, 0}), box_piece(2, {1, kNaN, 1}, {0.5f, 0, 0}), box_piece(3, {0, 1, 1}, {0.5f, 0, 0}),
                                box_piece(4, {1, 1, 1}, {0.5f, 0, 0}), box_piece(5, {1, 1, 1}, {kInf, 0, 0})};
      pieces[3].to_blockout = Mat4::scale({1, 0, 1});
      const R476Built b = r476_build(pieces, path);
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      CHECK(b.mesh.face_count() == 6);
      CHECK(b.report.skipped.size() == 4);
      if (b.report.skipped.size() == 4) {
        CHECK(b.report.skipped[0].id == 2 && b.report.skipped[0].why.find("finite") != std::string::npos);
        CHECK(b.report.skipped[1].id == 3 && b.report.skipped[1].why.find("no size") != std::string::npos);
        CHECK(b.report.skipped[2].id == 4 && b.report.skipped[2].why.find("flattens") != std::string::npos);
        CHECK(b.report.skipped[3].id == 5);
      }
    }
  });

  test("blockout: nothing left, a hollow, a mirrored and a sheared piece all come out closed", [] {
    for (Path path : r476_paths()) {
      R476Built b = r476_build({box_piece(1, {1, 1, 1}, {0, 0, 0}), box_piece(2, {2, 2, 2}, {0, 0, 0}, Op::Subtract)}, path);
      CHECK(b.ok && b.mesh.face_count() == 0);
      b = r476_build({box_piece(2, {2, 2, 2}, {0, 0, 0}, Op::Subtract)}, path);
      CHECK(b.ok && b.mesh.face_count() == 0);
      b = r476_build({box_piece(1, {2, 2, 2}, {0, 0, 0}), box_piece(2, {1, 1, 1}, {0, 0, 0}, Op::Subtract)}, path);
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      CHECK_NEAR(r476_volume(b.mesh), 7.0, 7.0e-5);
      CHECK(b.mesh.face_count() == 12 && euler_characteristic(b.mesh) == 4);  // (two shells)
      b = r476_build({box_piece(1, {2, 2, 2}, {0, 0, 0}), box_piece(2, {1, 1, 1}, {0.1f, 0.2f, 0})}, path);
      CHECK(b.ok && b.mesh.face_count() == 6);  // (a piece wholly inside another adds nothing)
      std::vector<Piece> odd{box_piece(1, {2, 1, 3}, {0, 0, 0}), box_piece(2, {1, 1, 1}, {1, 0.5f, 0})};
      odd[0].to_blockout = Mat4::scale({-1, 1, 1});
      odd[1].to_blockout.m[4] = 0.5f;  // (x leans with y)
      b = r476_build(odd, path);
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
    }
  });

  test("blockout: the same pieces give the same bytes, on one thread or all, from the cache or from scratch", [] {
    for (Path path : r476_paths()) {
      std::vector<Piece> pieces = r476_interior(2, 2);
      pieces.push_back(cylinder_piece(90, 0.6f, 1.2f, 12, {30, 0, 0}));
      pieces.push_back(box_piece(91, {1, 1, 1}, {30.3f, 0.2f, 0.1f}, Op::Add, Quat::euler({10, 20, 30})));
      const R476Built a = r476_build(pieces, path), again = r476_build(pieces, path);
      CHECK(a.ok && again.ok && r476_same_mesh(a, again));
      JobSystem::global().set_max_threads(1);
      const R476Built one = r476_build(pieces, path);
      JobSystem::global().set_max_threads(1 << 30);
      CHECK(one.ok && r476_same_mesh(a, one));
      blockout::Cache cache;
      const R476Built first = r476_build(pieces, path, 0.1f, &cache);
      CHECK(first.ok && r476_same_mesh(a, first) && first.report.built == 2 && first.report.cached == 0 && cache.size() == 2);
      pieces.back().to_blockout.m[12] += 0.125f;  // (the box of the second pile moves)
      const R476Built moved = r476_build(pieces, path, 0.1f, &cache), scratch = r476_build(pieces, path);
      CHECK(moved.ok && moved.report.built == 1 && moved.report.cached == 1);
      CHECK(moved.report.clusters.size() == 2 && moved.report.clusters[0].cached && !moved.report.clusters[1].cached);
      CHECK(scratch.ok && r476_same_mesh(moved, scratch));
      CHECK(!r476_same_mesh(moved, a));
    }
  });

  test("blockout: piles apart are clusters apart, a piece across them makes one", [] {
    for (Path path : r476_paths()) {
      std::vector<Piece> pieces{box_piece(1, {1, 1, 1}, {0, 0, 0}), box_piece(2, {1, 1, 1}, {0.5f, 0, 0}), box_piece(3, {1, 1, 1}, {100, 0, 0}),
                                box_piece(4, {1, 1, 1}, {100.5f, 0, 0})};
      R476Built b = r476_build(pieces, path);
      CHECK(b.ok && b.report.clusters.size() == 2 && r476_sound(b.mesh, r476_name(path)));
      pieces.push_back(box_piece(5, {101, 0.5f, 0.5f}, {50, 0, 0}));
      b = r476_build(pieces, path);
      CHECK(b.ok && b.report.clusters.size() == 1 && b.report.clusters[0].pieces == 5 && r476_sound(b.mesh, r476_name(path)));
    }
  });

  test("blockout: a cancelled build, and a crisp one without Manifold, leave the mesh alone", [] {
    const std::vector<Piece> pieces{box_piece(1, {1, 1, 1}, {0, 0, 0})};
    Mesh out = *primitives::cube(2.0f);
    const uint64_t version = out.version;
    const size_t faces_before = out.face_count();
    blockout::Faces faces;
    faces.piece = {42};
    blockout::Report rep;
    std::atomic<bool> cancel{true};
    CHECK(!blockout::build(pieces, blockout::Settings(), out, &faces, &rep, nullptr, &cancel));
    CHECK(rep.why == "cancelled" && out.version == version && out.face_count() == faces_before && faces.piece.size() == 1);
    blockout::Settings crisp;
    crisp.path = Path::Crisp;
    const bool built = blockout::build(pieces, crisp, out, &faces, &rep);
    CHECK(built == blockout::crisp_available());
    if (!built) CHECK(rep.why.find("Manifold") != std::string::npos && out.face_count() == faces_before && faces.piece.size() == 1);
    blockout::Settings any;
    CHECK(blockout::build(pieces, any, out, &faces, &rep) && out.face_count() == 6);
    CHECK(rep.clusters.size() == 1 && rep.clusters[0].path == (blockout::crisp_available() ? Path::Crisp : Path::Field));
  });

  test("blockout: four rooms with two doors have their closed-form volume and flat walls", [] {
    for (Path path : r476_paths()) {
      const std::vector<Piece> pieces = r476_interior(2, 2);
      const R476Built b = r476_build(pieces, path, 0.1f);
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      const double want = r476_interior_volume(2, 2);
      CHECK_NEAR(r476_volume(b.mesh), want, want * (path == Path::Crisp ? 1e-5 : 1e-4));
      CHECK(r476_flatness(b, pieces) <= 1e-5 * 12.0);
      CHECK(b.report.clusters.size() == 1 && b.report.clusters[0].pieces == 10);
      if (path == Path::Field) CHECK(b.report.unresolved == 0);
      std::printf("    %s: %zu faces from %zu, %zu points, %.1f ms\n", r476_name(path), b.mesh.face_count(), b.report.faces_in, b.mesh.vert_count(), b.report.ms_total);
    }
  });

  test("blockout: a box with a cylinder and a sphere is closed, its volume the solids'", [] {
    for (Path path : r476_paths()) {
      const std::vector<Piece> pieces{box_piece(1, {2, 1, 2}, {0, 0, 0}), cylinder_piece(2, 0.5f, 2.0f, 8, {0.25f, 0.5f, 0.125f}),
                                      sphere_piece(3, 0.75f, 8, 4, {-0.5f, 0.25f, -0.25f}, Op::Subtract)};
      const R476Built b = r476_build(pieces, path, 0.05f);
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      if (path == Path::Field && blockout::crisp_available()) {
        const R476Built crisp = r476_build(pieces, Path::Crisp);
        CHECK_NEAR(r476_volume(b.mesh), r476_volume(crisp.mesh), 0.01 * r476_volume(crisp.mesh));
      }
      CHECK(r476_volume(b.mesh) > 3.0 && r476_volume(b.mesh) < 5.0);
    }
  });

  test("blockout: a sphere and a cylinder alone keep their own facets", [] {
    for (Path path : r476_paths()) {
      R476Built b = r476_build({sphere_piece(1, 0.5f, 16, 8, {0.1f, 0.2f, 0.3f})}, path, 0.03f);
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      std::printf("    %s sphere: %zu faces, %zu points, %zu unresolved\n", r476_name(path), b.mesh.face_count(), b.mesh.vert_count(), b.report.unresolved);
      CHECK(b.mesh.face_count() == 16 * 8);
      CHECK(b.mesh.vert_count() == 16 * 7 + 2);
      b = r476_build({cylinder_piece(1, 0.5f, 1.0f, 12, {0.1f, 0.2f, 0.3f}, Op::Add, Quat::euler({20, 30, 40}))}, path, 0.05f);
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      std::printf("    %s cylinder: %zu faces, %zu points, %zu unresolved\n", r476_name(path), b.mesh.face_count(), b.mesh.vert_count(), b.report.unresolved);
      CHECK(b.mesh.face_count() == 14);
      CHECK(b.mesh.vert_count() == 24);
    }
  });

  test("blockout: a cylinder's rim less than a cell in front of a box's face leaves that face lying once, not folded over itself", [] {
    /* (The stress sweep's arrangement 195. The rim runs 0.06 to 0.14 m in front of the box's face and never touches
     * it; at a cell of 0.08 m the grid joins the two there. The face's outline used to run out to where the cap's
     * plane meets the face's - 0.24 m away, where the cylinder is not - and back across itself.) */
    const Piece box = r476_placed(box_piece(1, {0.351164f, 0.648395f, 1.08951f}, {0, 0, 0}), {-0.320881f, 0.296423f, 0.974077f}, {-0.653867f, 0.471097f, 0.592052f},
                                  {0.420207f, 0.876839f, -0.233622f}, {-0.629193f, 0.0960267f, -0.771294f});
    const Piece near_it = r476_placed(cylinder_piece(3, 0.626332f, 1.27721f, 16, {0, 0, 0}), {0.302218f, 0.576783f, -0.120792f}, {-0.0792819f, -0.519753f, -0.85063f},
                                      {-0.825488f, 0.512589f, -0.236264f}, {0.558823f, 0.683454f, -0.469689f});
    const Piece through = r476_placed(cylinder_piece(4, 0.872093f, 1.93712f, 5, {0, 0, 0}), {0.907803f, 0.155963f, 0.104302f}, {0.316207f, 0.561984f, 0.764321f},
                                      {-0.924735f, 0.362491f, 0.116043f}, {-0.211845f, -0.743488f, 0.634308f});
    const R476Built b = r476_build({box, near_it, through}, Path::Field, 0.08f);
    CHECK(b.ok);
    std::string why;
    CHECK(structurally_valid(b.mesh, &why));
    CHECK(closed_manifold(b.mesh));
    CHECK(overlapping_face_pairs(b.mesh) == 0);
    CHECK(b.report.unresolved > 0);  // (it is a place the grid does not resolve, and the report says so)
    std::printf("    field, a rim near a face: %zu faces, %zu unresolved\n", b.mesh.face_count(), b.report.unresolved);
  });

  test("blockout: boxes that touch edge to edge, or corner to corner, stay two boxes with nothing between", [] {
    for (Path path : r476_paths()) {
      /* One above and behind the other: they share part of an edge, and nothing else. */
      R476Built b = r476_build({box_piece(1, {2.25f, 1, 0.5f}, {-1, 1, -1}), box_piece(2, {1.25f, 1.5f, 0.5f}, {-0.25f, -0.25f, -0.5f})}, path, 0.08f);
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      std::printf("    %s edge to edge: %zu faces, %zu points, %zu unresolved\n", r476_name(path), b.mesh.face_count(), b.mesh.vert_count(), b.report.unresolved);
      CHECK(b.mesh.face_count() == 12);
      CHECK(b.report.unresolved == 0);
      CHECK_NEAR(r476_volume(b.mesh), 2.25 * 0.5 + 1.25 * 1.5 * 0.5, 2.1e-5);
      b = r476_build({box_piece(1, {1, 1, 1}, {0, 0, 0}), box_piece(2, {1, 1, 1}, {1, 1, 1})}, path, 0.08f);
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      std::printf("    %s corner to corner: %zu faces, %zu points, %zu unresolved\n", r476_name(path), b.mesh.face_count(), b.mesh.vert_count(), b.report.unresolved);
      CHECK(b.mesh.face_count() == 12);
      CHECK(b.report.unresolved == 0);
      CHECK_NEAR(r476_volume(b.mesh), 2.0, 2.0e-5);
    }
  });

  test("blockout: a floor with pits and columns in rows, some at its rim, keeps each flat side one region", [] {
    /* (Holes in a row have their edges on one line, and so have the bridges that join them into the outline: each
     * hole must be joined to the next one, not all of them to the first. Pits at the rim are notches, the one at
     * the corner takes the corner; a column at the rim shares the floor's side.) */
    for (Path path : r476_paths()) {
      std::vector<Piece> pieces{box_piece(1, {6, 1, 6}, {0, 0, 0})};
      uint64_t id = 2;
      for (int j = 0; j < 4; j++)
        for (int i = 0; i < 4; i++) {
          const Vec3 at(-2.75f + 1.5f * (float)i, 0.0f, -2.75f + 1.5f * (float)j);
          if ((i + j) % 2 == 0) pieces.push_back(box_piece(id++, {0.5f, 3, 0.5f}, at, Op::Subtract));
          else pieces.push_back(box_piece(id++, {0.5f, 2, 0.5f}, at + Vec3(0, 1, 0)));
        }
      const R476Built b = r476_build(pieces, path);
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      std::printf("    %s floor with 8 pits and 8 columns: %zu faces, %zu unresolved\n", r476_name(path), b.mesh.face_count(), b.report.unresolved);
      CHECK(b.report.unresolved == 0);
      CHECK_NEAR(r476_volume(b.mesh), 36.0 - 8 * 0.25 + 8 * 0.375, 37.0e-5);
      size_t tops = 0;  // (the floor's top is one region: its faces are few, and every one of them is flat)
      for (size_t f = 0; f < b.mesh.face_count(); f++) {
        CHECK(b.faces.flat[f] == 1);
        tops += b.faces.piece[f] == 1 && b.faces.side[f] == 3;
      }
      CHECK(tops > 0 && tops < 40);
    }
  });

  test("blockout: two overlapping boxes are twelve faces and twenty corners, the same by the grid as by the exact boolean", [] {
    /* (Criterion 3's target for the field path: the crisp path's faces and vertices.) Each box loses a corner
     * inside the other and three of its faces a rectangle; the six lines where they cross give six corners. */
    const std::vector<Piece> pieces{box_piece(1, {2, 2, 2}, {0, 0, 0}), box_piece(2, {2, 2, 2}, {1, 0.5f, 0.25f})};
    const R476Built field = r476_build(pieces, Path::Field);
    CHECK(field.ok && r476_sound(field.mesh, "field"));
    CHECK(field.mesh.face_count() == 12 && field.mesh.vert_count() == 20 && field.report.unresolved == 0);
    if (!blockout::crisp_available()) return;
    const R476Built crisp = r476_build(pieces, Path::Crisp);
    CHECK(crisp.ok && crisp.mesh.face_count() == 12 && crisp.mesh.vert_count() == 20);
    CHECK(r476_same_shape(field, crisp));
  });

  test("blockout: rooms turned 30 and 45 degrees keep their walls flat, their faces few and their volume", [] {
    /* (Planes at an angle that several pieces share. The crisp path hands the building to Manifold in the frame it
     * is square to, so its shared planes are as exact as along the axes - a door's sill and the floor it reaches,
     * two cutters' sides in one plane, left a strip of wall under the door before.) */
    for (Path path : r476_paths())
      for (Vec3 angles : {Vec3(0, 30, 0), Vec3(0, 45, 0), Vec3(20, 30, 40)}) {
        const Quat turn = Quat::euler(angles);
        std::vector<Piece> pieces = r476_interior(2, 2);
        for (Piece &p : pieces) p.to_blockout = Mat4::trs(turn.rotate(p.to_blockout.translation()), turn, Vec3(1.0f));
        const R476Built b = r476_build(pieces, path, 0.05f);
        CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
        std::printf("    %s rooms turned %g %g %g: %zu faces, %zu points, %zu unresolved\n", r476_name(path), angles.x, angles.y, angles.z, b.mesh.face_count(), b.mesh.vert_count(),
                    b.report.unresolved);
        CHECK(b.mesh.face_count() == 34 && b.mesh.vert_count() == 56 && b.report.unresolved == 0);
        if (path == Path::Crisp) CHECK(b.report.clusters[0].turned);  // (handed over in the frame the rooms are square to)
        CHECK(r476_flatness(b, pieces) <= 1e-5 * 12.0);
        CHECK_NEAR(r476_volume(b.mesh), r476_interior_volume(2, 2), r476_interior_volume(2, 2) * 2e-5);
      }
  });

  test("blockout: turned 30 degrees, a cut flush with a face still goes through it and a cutter that only touches still takes nothing", [] {
    /* (Found by the second review of round 476: at an angle two solids' faces in one plane did not coincide to the
     * bit, and what Manifold made of them hung on the rounding. Turned as a whole, the pieces are now handed over
     * in the frame they are square to; the test after this one turns them against the rest of their cluster.) */
    for (Path path : r476_paths())
      for (Vec3 angles : {Vec3(0, 30, 0), Vec3(20, 30, 40)}) {
        const Quat turn = Quat::euler(angles);
        auto turned = [&](std::vector<Piece> pieces) {
          for (Piece &p : pieces) p.to_blockout = Mat4::trs(turn.rotate(p.to_blockout.translation()), turn, Vec3(1.0f));
          return pieces;
        };
        /* A pocket whose top is exactly the box's top: open, with no skin over it. */
        R476Built b = r476_build(turned({box_piece(1, {4, 2, 4}, {0, 0, 0}), box_piece(2, {1, 1, 1}, {0, 0.5f, 0}, Op::Subtract)}), path, 0.05f);
        CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
        CHECK_NEAR(r476_volume(b.mesh), 32.0 - 1.0, 32.0e-5);
        /* (Five of the box's faces, its top - a ring round the pocket - cut in four, the pocket's four walls and floor.) */
        CHECK(b.mesh.face_count() == 14 && b.mesh.vert_count() == 16 && b.report.unresolved == 0);
        /* A cutter that lies against the box's side from outside. */
        b = r476_build(turned({box_piece(1, {2, 2, 2}, {0, 0, 0}), box_piece(2, {1, 1, 1}, {1.5f, 0, 0}, Op::Subtract)}), path, 0.05f);
        CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
        CHECK(b.mesh.face_count() == 6 && b.mesh.vert_count() == 8 && b.report.unresolved == 0);
        CHECK_NEAR(r476_volume(b.mesh), 8.0, 8.0e-5);
        for (uint64_t piece : b.faces.piece) CHECK(piece == 1);
        /* Two boxes that share a face, and a cut that ends on that face from inside the first. */
        b = r476_build(turned({box_piece(1, {2, 1, 1}, {0, 0, 0}), box_piece(2, {2, 1, 1}, {2, 0, 0}), box_piece(3, {0.5f, 0.5f, 0.5f}, {0.75f, 0, 0}, Op::Subtract)}), path, 0.05f);
        CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
        CHECK_NEAR(r476_volume(b.mesh), 4.0 - 0.125, 4.0e-5);
      }
  });

  test("blockout: an annex at an angle to the building it stands by joins, cuts and touches as one along the axes would", [] {
    /* (Found by the third review of round 476.) Rooms along the axes and, in the same cluster, an annex turned 30
     * degrees: the cluster's frame is the rooms', so the annex's shared planes stay at an angle to it, and the
     * crisp path's last resort - moving a side by a hair, by what shares its plane - is what decides there:
     * two boxes that share a face (sides out, to overlap), a pocket flush with a top (the cutter past it), a cutter
     * that only touches a side (clear of it), a cut that ends on the plane where the two boxes meet (not moved),
     * and a cutter under a cutter (out, into each other: one pit, no skin between). The field path knows nothing of
     * hairs: it must give the same faces. */
    const Quat turn = Quat::euler({0, 0, 30});
    auto annex = [&](Piece p) {
      p.to_blockout = Mat4::trs(turn.rotate(p.to_blockout.translation()), turn, Vec3(1.0f));
      return p;
    };
    std::vector<Piece> pieces = r476_interior(2, 2);
    /* (The rooms stand by the annex's big box, in the empty corner of its bounds: pieces whose bounds meet are one
     * cluster, and nothing of the two touches.) */
    for (Piece &p : pieces) p.to_blockout = Mat4::translate(Vec3(-7.9f, 3.0f, 0.0f)) * p.to_blockout;
    pieces.push_back(annex(box_piece(21, {4, 2, 4}, {0, 0, 0})));
    pieces.push_back(annex(box_piece(22, {2, 2, 4}, {-3, 0, 0})));
    pieces.push_back(annex(box_piece(23, {1, 1, 1}, {0, 0.5f, 0}, Op::Subtract)));           // a pocket flush with the top
    pieces.push_back(annex(box_piece(24, {1, 0.5f, 1}, {0, -0.25f, 0}, Op::Subtract)));      // ...and a cutter under it
    pieces.push_back(annex(box_piece(25, {1, 1, 1}, {2.5f, 0, 0}, Op::Subtract)));           // touches a side from outside
    pieces.push_back(annex(box_piece(26, {0.5f, 0.5f, 0.5f}, {-2.25f, 0, 0}, Op::Subtract)));  // ends on the plane the two boxes share
    /* (Found by the fourth review.) What a cutter's side does goes by the faces it lies on, not by all its plane
     * holds: a box against the same side as the touching cutter, but away from it; and a pocket flush with the
     * underside from inside, beside a box that touches the underside from below. */
    pieces.push_back(annex(box_piece(27, {2, 2, 1}, {3, 0, 1.5f})));
    pieces.push_back(annex(box_piece(28, {1, 1, 1}, {1, -1.5f, 1})));
    pieces.push_back(annex(box_piece(29, {1, 0.5f, 1}, {-1.25f, -0.75f, -1.25f}, Op::Subtract)));
    const double volume = r476_interior_volume(2, 2) + 32.0 + 16.0 + 4.0 + 1.0 - 1.0 - 0.5 - 0.125 - 0.5;
    const R476Built field = r476_build(pieces, Path::Field, 0.05f);
    CHECK(field.ok && field.report.clusters.size() == 1 && r476_sound(field.mesh, "field"));
    CHECK(field.report.unresolved == 0);
    CHECK_NEAR(r476_volume(field.mesh), volume, volume * 2e-5);
    for (uint64_t piece : field.faces.piece) CHECK(piece != 25);  // (the cutter that only touches names no face)
    if (!blockout::crisp_available()) return;
    const R476Built crisp = r476_build(pieces, Path::Crisp);
    CHECK(crisp.ok && crisp.report.clusters.size() == 1 && r476_sound(crisp.mesh, "crisp"));
    CHECK(!crisp.report.clusters[0].turned);  // (the rooms' frame, so the annex's shared planes are at an angle to it: else this test tests nothing)
    std::printf("    an annex at an angle: crisp %zu faces, %zu points, %zu unresolved; field %zu faces, %zu points\n", crisp.mesh.face_count(), crisp.mesh.vert_count(),
                crisp.report.unresolved, field.mesh.face_count(), field.mesh.vert_count());
    CHECK(crisp.report.unresolved == 0);
    CHECK_NEAR(r476_volume(crisp.mesh), volume, volume * 2e-5);
    for (uint64_t piece : crisp.faces.piece) CHECK(piece != 25);
    CHECK(r476_same_shape(field, crisp));
  });

  test("blockout: an annex turned any way has its cutters go by the faces they lie on, not by what is beside them", [] {
    /* (From the fifth review, by reading.) Which Add faces a cutter's face lies on was asked of the faces' bounds
     * along two axes of their plane. Turned about one axis those bounds are the faces; turned any way they are
     * wider, and a box beside the touching cutter could count as one under it. The figures themselves are asked now.
     * (This arrangement came out sound before that change too: the test holds the annex's cases at a turn about
     * no axis, it does not show the fault.) The rooms stand in the empty corner of the big box's bounds, as in the
     * test above, so the cluster's frame is theirs. */
    const Quat turn = Quat::euler({20, 30, 40});
    auto annex = [&](Piece p) {
      p.to_blockout = Mat4::trs(turn.rotate(p.to_blockout.translation()), turn, Vec3(1.0f));
      return p;
    };
    std::vector<Piece> added;
    added.push_back(annex(box_piece(21, {4, 2, 4}, {0, 0, 0})));
    added.push_back(annex(box_piece(22, {2, 2, 4}, {-3, 0, 0})));
    added.push_back(annex(box_piece(25, {1, 1, 1}, {2.5f, 0, 0}, Op::Subtract)));              // touches a side from outside
    added.push_back(annex(box_piece(27, {2, 2, 1}, {3, 0, 1.5f})));                            // against that side, beside the cutter
    added.push_back(annex(box_piece(28, {1, 1, 1}, {1, -1.5f, 1})));                           // touches the underside from below
    added.push_back(annex(box_piece(29, {1, 0.5f, 1}, {-1.25f, -0.75f, -1.25f}, Op::Subtract)));  // a pocket flush with the underside
    Vec3 top(-1e9f);  // (the top corner of the big box's bounds: pieces whose bounds meet are one cluster)
    for (int k = 0; k < 8; k++) top = vmax(top, added[0].to_blockout.point(Vec3(k & 1 ? 0.5f : -0.5f, k & 2 ? 0.5f : -0.5f, k & 4 ? 0.5f : -0.5f) * added[0].size));
    std::vector<Piece> pieces = r476_interior(2, 2);
    for (Piece &p : pieces) p.to_blockout = Mat4::translate(top - Vec3(0.2f) + Vec3(2.1f, 1.6f, 2.1f)) * p.to_blockout;
    pieces.insert(pieces.end(), added.begin(), added.end());
    const double volume = r476_interior_volume(2, 2) + 32.0 + 16.0 + 4.0 + 1.0 - 0.5;
    const R476Built field = r476_build(pieces, Path::Field, 0.05f);
    CHECK(field.ok && field.report.clusters.size() == 1 && r476_sound(field.mesh, "field"));
    CHECK_NEAR(r476_volume(field.mesh), volume, volume * 0.01);
    if (!blockout::crisp_available()) return;
    const R476Built crisp = r476_build(pieces, Path::Crisp);
    CHECK(crisp.ok && crisp.report.clusters.size() == 1 && r476_sound(crisp.mesh, "crisp"));
    CHECK(!crisp.report.clusters[0].turned);
    std::printf("    an annex turned any way: crisp %zu faces, %zu points, %zu unresolved; field %zu faces, %zu unresolved\n", crisp.mesh.face_count(), crisp.mesh.vert_count(),
                crisp.report.unresolved, field.mesh.face_count(), field.report.unresolved);
    CHECK(crisp.report.unresolved == 0);
    CHECK_NEAR(r476_volume(crisp.mesh), volume, volume * 2e-5);
    for (uint64_t piece : crisp.faces.piece) CHECK(piece != 25);  // (the cutter that only touches names no face)
  });

  test("blockout: boxes on a quarter-metre grid, turned by right angles through float matrices, leave no stray faces", [] {
    /* (Two arrangements of the stress sweep, 5 and 1895, as its generator writes them: axes a last bit off the
     * right angle. A cutter touches a box from outside, flush with it; boxes share faces. The crisp path used to
     * grow every piece by a hair to make such faces meet, which left faces of no area here - and in 1895 two flat
     * faces on top of each other. Planes square to the axes now hold their corners to the bit instead, and
     * Manifold's own rule for faces that coincide applies.) */
    const std::vector<std::vector<Piece>> arrangements{
        {r476_placed(box_piece(1, {1, 1.75f, 1.5f}, {0, 0, 0}), {0.25f, -0.5f, -0.5f}, {-1, 0, 1.74846e-07f}, {-1.74846e-07f, 1.78814e-07f, -1}, {0, -1, 1.78814e-07f}),
         r476_placed(box_piece(2, {1.5f, 2, 2}, {0, 0, 0}), {-1, 0.75f, 0}, {1, 0, 0}, {0, -1, -1.74846e-07f}, {0, 1.74846e-07f, -1}),
         r476_placed(box_piece(3, {1.25f, 0.5f, 1.75f}, {0, 0, 0}, Op::Subtract), {-1, -1, 1}, {5.96046e-08f, 0, 1}, {0, 1, 0}, {-1, 0, 5.96046e-08f})},
        {r476_placed(box_piece(1, {0.5f, 1.25f, 1.5f}, {0, 0, 0}), {-1, 0.5f, -0.5f}, {5.96046e-08f, 0, 1}, {0, 1, 0}, {-1, 0, 5.96046e-08f}),
         r476_placed(box_piece(2, {1.25f, 0.75f, 1.5f}, {0, 0, 0}), {-0.75f, 0.5f, 1}, {0, 0, -1}, {-1, 1.19209e-07f, 0}, {1.78814e-07f, 1, 0}),
         r476_placed(box_piece(3, {2.25f, 1, 1}, {0, 0, 0}, Op::Subtract), {0, -1, -0.25f}, {1, 0, 0}, {0, -1, -1.74846e-07f}, {0, 1.74846e-07f, -1}),
         r476_placed(box_piece(4, {1.75f, 1.5f, 1.25f}, {0, 0, 0}, Op::Subtract), {0.5f, 0.5f, 0.25f}, {-1, 0, 1.74846e-07f}, {0, 1, 0}, {-1.74846e-07f, 0, -1}),
         r476_placed(box_piece(5, {1.25f, 2, 2.25f}, {0, 0, 0}), {-1, 0.75f, 0.75f}, {1, 0, 0}, {0, -1, -1.74846e-07f}, {0, 1.74846e-07f, -1})}};
    const size_t faces[2] = {12, 14};
    for (size_t i = 0; i < arrangements.size(); i++) {
      const R476Built field = r476_build(arrangements[i], Path::Field, 0.08f);
      CHECK(field.ok && r476_sound(field.mesh, "field"));
      CHECK(field.mesh.face_count() == faces[i] && field.report.unresolved == 0);
      if (!blockout::crisp_available()) continue;
      const R476Built crisp = r476_build(arrangements[i], Path::Crisp);
      CHECK(crisp.ok && r476_sound(crisp.mesh, "crisp"));
      std::printf("    crisp, arrangement %zu: %zu faces, %zu points, %zu unresolved (field: %zu faces, %zu points)\n", i, crisp.mesh.face_count(), crisp.mesh.vert_count(),
                  crisp.report.unresolved, field.mesh.face_count(), field.mesh.vert_count());
      CHECK(crisp.report.unresolved == 0);
      CHECK(r476_same_shape(field, crisp));
    }
  });

  test("blockout: a Blend radius far larger than its piece is taken as the piece's own size, and the grid stays small", [] {
    /* (Found by the review of round 476: the grid a blend needs grew with the radius without limit - a radius of
     * 100 m on a one-metre box asked for tens of gigabytes.) */
    for (float blend : {100.0f, 1.0e30f}) {
      std::vector<Piece> pieces{box_piece(1, {1, 1, 1}, {0, 0, 0}), box_piece(2, {1, 1, 1}, {0.75f, 0.25f, 0})};
      pieces[0].blend = blend;
      const R476Built b = r476_build(pieces, Path::Auto);
      CHECK(b.ok && structurally_valid(b.mesh) && closed_manifold(b.mesh) && signed_volume(b.mesh) > 0.0f);
      std::printf("    a blend of %g m on a 1 m box: %zu cells at a cell of %g m\n", (double)blend, b.report.cells, (double)b.report.cell);
      CHECK(b.report.cells < 200000);
    }
    /* And a cell far too fine for the budget is made larger, the report saying which. */
    std::vector<Piece> pieces{box_piece(1, {4, 4, 4}, {0, 0, 0})};
    pieces[0].blend = 2.0f;
    blockout::Settings st;
    st.cell = 0.001f, st.max_cells = 100000;
    R476Built b;
    b.ok = blockout::build(pieces, st, b.mesh, &b.faces, &b.report);
    CHECK(b.ok && closed_manifold(b.mesh) && b.report.cell > 0.001f);
  });

  test("blockout: a piece with a Blend radius melts into its neighbour, and the result is still closed", [] {
    std::vector<Piece> pieces{box_piece(1, {2, 1, 2}, {0, 0, 0}), sphere_piece(2, 0.6f, 16, 8, {0.2f, 0.7f, 0.1f})};
    pieces[1].blend = 0.25f;
    const R476Built plain = r476_build({pieces[0], sphere_piece(2, 0.6f, 16, 8, {0.2f, 0.7f, 0.1f})}, Path::Field, 0.05f);
    for (float cell : {0.2f, 0.1f, 0.05f}) {
      const R476Built b = r476_build(pieces, Path::Auto, cell);
      CHECK(b.ok && b.report.clusters.size() == 1 && b.report.clusters[0].path == Path::Field);
      CHECK(structurally_valid(b.mesh) && closed_manifold(b.mesh) && signed_volume(b.mesh) > 0.0f);
      if (cell == 0.05f) CHECK(r476_volume(b.mesh) > r476_volume(plain.mesh));  // (the join is filled in)
      size_t blend = 0;
      for (uint32_t side : b.faces.side) blend += side == blockout::kSideBlend;
      CHECK(blend > 0);
    }
  });
}

/* ---- The test engineer's round 476 tests: written from the acceptance criteria, for what the tests above leave open. ---- */

namespace {

R476Built r476e_build(const std::vector<Piece> &pieces, const blockout::Settings &st, blockout::Cache *cache = nullptr) {
  R476Built b;
  b.ok = blockout::build(pieces, st, b.mesh, &b.faces, &b.report, cache);
  return b;
}
blockout::Settings r476e_settings(Path path, float cell = 0.1f) {
  blockout::Settings st;
  st.path = path, st.cell = cell;
  return st;
}

/* A result that is closed and valid even where the field path cannot resolve the pieces (so faces on top of each
 * other are not asked about): finite, structurally valid, closed, outward (or empty), and one origin for each face. */
bool r476e_hard(const R476Built &b, const char *what) {
  bool finite = true;
  for (const Vec3 &p : b.mesh.positions) finite = finite && finite_bits(p.x) && finite_bits(p.y) && finite_bits(p.z);
  std::string why;
  const bool valid = structurally_valid(b.mesh, &why), closed = closed_manifold(b.mesh);
  const bool outward = b.mesh.face_count() == 0 || signed_volume(b.mesh) > 0.0f;
  const bool origins = b.faces.piece.size() == b.mesh.face_count() && b.faces.side.size() == b.mesh.face_count() && b.faces.flat.size() == b.mesh.face_count();
  if (!finite || !valid || !closed || !outward || !origins)
    std::printf("    %s: finite %d valid %d (%s) closed %d outward %d origins %d\n", what, (int)finite, (int)valid, why.c_str(), (int)closed, (int)outward, (int)origins);
  return finite && valid && closed && outward && origins;
}
/* The full soundness check where the grid resolved everything, the hard one where it reports an unresolved place. */
bool r476e_sound_or_hard(const R476Built &b, const char *what) {
  if (b.report.unresolved > 0) return r476e_hard(b, what);
  return r476e_hard(b, what) && r476_sound(b.mesh, what);
}

/* Faces lying in the plane of the side they name, counting only the faces that promise it (flat 1). */
double r476e_flat_flatness(const R476Built &b, const std::vector<Piece> &pieces) {
  double worst = 0.0;
  for (size_t f = 0; f < b.mesh.face_count(); f++) {
    if (!b.faces.flat[f]) continue;
    const Piece *p = nullptr;
    for (const Piece &q : pieces)
      if (q.id == b.faces.piece[f]) p = &q;
    if (!p || p->shape != Shape::Box || b.faces.side[f] > 5) continue;
    const int axis = (int)b.faces.side[f] / 2;
    Vec3 local(0.0f), dir(0.0f);
    local[axis] = (b.faces.side[f] & 1 ? 0.5f : -0.5f) * p->size[axis];
    dir[axis] = 1.0f;
    const Vec3 on = p->to_blockout.point(local), n = normalize(p->to_blockout.inverse().transposed().dir(dir));
    for (uint32_t i = 0; i < b.mesh.face_size(f); i++) worst = std::max(worst, (double)std::fabs(dot(n, b.mesh.positions[b.mesh.face_verts(f)[i]] - on)));
  }
  return worst;
}

double r476e_diagonal(const Mesh &m) {
  if (m.positions.empty()) return 0.0;
  Vec3 lo = m.positions[0], hi = lo;
  for (const Vec3 &p : m.positions) lo = vmin(lo, p), hi = vmax(hi, p);
  return length(hi - lo);
}

/* The face's unit normal (Newell), in the order the mesh lists its corners. */
Vec3 r476e_normal(const Mesh &m, size_t f) {
  Vec3 n(0.0f);
  const uint32_t *fv = m.face_verts(f);
  const uint32_t k = m.face_size(f);
  for (uint32_t i = 0; i < k; i++) {
    const Vec3 a = m.positions[fv[i]], b = m.positions[fv[(i + 1) % k]];
    n.x += (a.y - b.y) * (a.z + b.z), n.y += (a.z - b.z) * (a.x + b.x), n.z += (a.x - b.x) * (a.y + b.y);
  }
  return normalize(n);
}

size_t r476e_count_side(const R476Built &b, uint64_t piece, uint32_t side) {
  size_t n = 0;
  for (size_t f = 0; f < b.faces.piece.size(); f++) n += b.faces.piece[f] == piece && b.faces.side[f] == side;
  return n;
}

/* Builds into a mesh that holds something else; false must leave that alone, true must be sound. */
bool r476e_built_or_untouched(const std::vector<Piece> &pieces, const blockout::Settings &st, const char *what, bool *built = nullptr) {
  Mesh out = *primitives::cube(2.0f);
  const std::vector<Vec3> before = out.positions;
  blockout::Faces faces;
  faces.piece = {9};
  blockout::Report rep;
  const bool ok = blockout::build(pieces, st, out, &faces, &rep);
  if (built) *built = ok;
  if (ok) {
    R476Built b;
    b.ok = true, b.mesh = out, b.faces = faces, b.report = rep;
    return r476e_hard(b, what);
  }
  const bool same = out.positions == before && faces.piece.size() == 1 && !rep.why.empty();
  if (!same) std::printf("    %s: refused (%s) but left the mesh changed or gave no reason\n", what, rep.why.c_str());
  return same;
}

std::vector<Piece> r476e_pile(uint64_t first_id, float at_x) {
  return {box_piece(first_id, {1, 1, 1}, {at_x, 0, 0}), box_piece(first_id + 1, {1, 1, 1}, {at_x + 0.5f, 0, 0})};
}

}  // namespace

static void tests_round476_engineer() {
  /* ---- Criterion 1: closed and valid, whatever the pieces ---- */
  test("blockout: boxes side by side with different heights, or with a common top, share their plane without a seam", [] {
    for (Path path : r476_paths()) {
      R476Built b = r476e_build({box_piece(1, {2, 1, 2}, {0, 0.5f, 0}), box_piece(2, {2, 2, 2}, {2, 1, 0})}, r476e_settings(path));
      CHECK(b.ok && r476e_sound_or_hard(b, r476_name(path)));
      CHECK(b.report.unresolved == 0);
      CHECK_NEAR(r476_volume(b.mesh), 12.0, 12.0e-4);
      /* Overlapping, with the same top: two Add faces in one plane. */
      b = r476e_build({box_piece(1, {2, 2, 2}, {0, 0, 0}), box_piece(2, {2, 1, 2}, {1, 0.5f, 0})}, r476e_settings(path));
      CHECK(b.ok && r476e_sound_or_hard(b, r476_name(path)));
      CHECK(b.report.unresolved == 0);
      CHECK_NEAR(r476_volume(b.mesh), 10.0, 10.0e-4);
      size_t tops = 0;
      for (size_t f = 0; f < b.mesh.face_count(); f++) tops += r476e_normal(b.mesh, f).y > 0.99f;
      CHECK(tops == 1);  // (one top face, not two in one plane)
    }
  });

  test("blockout: a cutter that only touches a piece from outside takes nothing away", [] {
    for (Path path : r476_paths()) {
      R476Built b = r476e_build({box_piece(1, {2, 2, 2}, {0, 0, 0}), box_piece(2, {1, 1, 1}, {1.5f, 0, 0}, Op::Subtract)}, r476e_settings(path));
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      CHECK(b.mesh.face_count() == 6 && b.mesh.vert_count() == 8);
      CHECK_NEAR(r476_volume(b.mesh), 8.0, 8.0e-5);
      CHECK(r476e_count_side(b, 2, 0) + r476e_count_side(b, 2, 1) + r476e_count_side(b, 2, 2) == 0);
      /* Touching along an edge from outside. */
      b = r476e_build({box_piece(1, {2, 2, 2}, {0, 0, 0}), box_piece(2, {1, 1, 1}, {1.5f, 1.5f, 0}, Op::Subtract)}, r476e_settings(path));
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      CHECK(b.mesh.face_count() == 6);
      CHECK_NEAR(r476_volume(b.mesh), 8.0, 8.0e-5);
    }
  });

  test("blockout: a piece 0.0001 m across is left out, and named, where a mesh's 32-bit positions could not hold it", [] {
    for (Path path : r476_paths()) {
      /* 6 km from the origin a float's step is half a millimetre: the speck's two sides would be one plane in the
       * mesh, its faces on top of each other. It is skipped by name and the big box comes out whole. (Found by the
       * review of round 476: the crisp path built it, and the kernel's closed check, in doubles, passed it.) */
      const Piece huge = box_piece(1, {10000, 10000, 10000}, {0, 0, 0});
      for (Vec3 at : {Vec3(6000, 0, 0), Vec3(0, 5000, 0)}) {
        const R476Built b = r476e_build({huge, box_piece(2, {0.0001f, 0.0001f, 0.0001f}, at)}, r476e_settings(path));
        CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
        CHECK(b.mesh.face_count() == 6 && b.mesh.vert_count() == 8);
        CHECK(b.report.skipped.size() == 1 && b.report.skipped[0].id == 2 && b.report.skipped[0].why.find("too small") != std::string::npos);
      }
      /* At the origin the same speck is fine: alone, and as a cut inside the big box. (The hard check, not the full
       * one: overlapping_face_pairs measures against the mesh's size - 17 km - and would call a 0.1 mm hollow's
       * six faces one place.) */
      CHECK(r476e_built_or_untouched({box_piece(2, {0.0001f, 0.0001f, 0.0001f}, {0, 0, 0})}, r476e_settings(path), "tiny alone"));
      /* What a user could really have is kept: a 5 mm panel 2 km out, a finely faceted ball 100 m out (its shortest
       * edge 0.06 mm, a float's step there 0.008 mm). Only where that edge is under four steps is the ball left
       * out - with words that say what cures it. */
      R476Built far = r476e_build({box_piece(1, {2, 0.005f, 2}, {2000, 0, 0})}, r476e_settings(path));
      CHECK(far.ok && far.report.skipped.empty());
      far = r476e_build({sphere_piece(1, 0.1f, 256, 128, {100, 0, 0})}, r476e_settings(path, 0.05f));
      CHECK(far.ok && far.report.skipped.empty());
      far = r476e_build({sphere_piece(1, 0.1f, 256, 128, {1000, 0, 0})}, r476e_settings(path, 0.05f));
      CHECK(far.ok && far.report.skipped.size() == 1 && far.report.skipped[0].why.find("faceting") != std::string::npos);
      CHECK(r476e_built_or_untouched({huge, box_piece(2, {0.0001f, 0.0001f, 0.0001f}, {0, 0, 0}, Op::Subtract)}, r476e_settings(path), "tiny cut"));
    }
  });

  test("blockout: pieces 100,000 m from the origin still build closed, with no faces on top of each other", [] {
    for (Path path : r476_paths()) {
      const std::vector<Piece> pieces{box_piece(1, {2, 2, 2}, {100000, 0, 0}), box_piece(2, {2, 2, 2}, {100001, 0.5f, 0.25f}),
                                      box_piece(3, {1, 4, 1}, {100000.5f, 0, 0}, Op::Subtract)};
      const R476Built b = r476e_build(pieces, r476e_settings(path));
      CHECK(b.ok && b.report.skipped.empty());  // (nothing in the pieces asks for a refusal: a float's step there is 8 mm)
      CHECK(r476e_hard(b, r476_name(path)) && r476_sound(b.mesh, r476_name(path)));
    }
  });

  test("blockout: a piece with NaN or infinity in its matrix is skipped and named, the rest builds", [] {
    for (Path path : r476_paths()) {
      std::vector<Piece> pieces{box_piece(1, {1, 1, 1}, {0, 0, 0}), box_piece(2, {1, 1, 1}, {0.5f, 0, 0}), box_piece(3, {1, 1, 1}, {0.25f, 0, 0}),
                                cylinder_piece(4, 0.5f, 1.0f, 8, {0, 0, 0})};
      pieces[1].to_blockout.m[13] = kNaN;
      pieces[2].to_blockout.m[0] = kInf;
      pieces[3].to_blockout.m[5] = kNaN;
      const R476Built b = r476e_build(pieces, r476e_settings(path));
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      CHECK(b.mesh.face_count() == 6);
      CHECK(b.report.skipped.size() == 3);
      if (b.report.skipped.size() != 3) continue;
      CHECK(b.report.skipped[0].id == 2 && b.report.skipped[1].id == 3 && b.report.skipped[2].id == 4);
      for (const blockout::Skipped &s : b.report.skipped) CHECK(!s.why.empty());
      /* When every piece is unusable there is nothing to show, and nothing wrong with that. */
      const R476Built none = r476e_build({pieces[1], pieces[2]}, r476e_settings(path));
      CHECK(none.ok && none.mesh.face_count() == 0 && none.report.skipped.size() == 2 && none.faces.piece.empty());
    }
  });

  test("blockout: no pieces at all gives an empty mesh, and a radius of zero or too few sides is skipped", [] {
    for (Path path : r476_paths()) {
      R476Built b = r476e_build({}, r476e_settings(path));
      CHECK(b.ok && b.mesh.face_count() == 0 && b.faces.piece.empty() && b.report.clusters.empty() && b.report.skipped.empty());
      b = r476e_build({cylinder_piece(1, 0.0f, 1.0f, 12, {0, 0, 0}), sphere_piece(2, 0.0f, 12, 6, {0, 0, 0}), box_piece(3, {1, 1, 1}, {3, 0, 0})}, r476e_settings(path));
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      CHECK(b.mesh.face_count() == 6 && b.report.skipped.size() == 2);
    }
  });

  test("blockout: two pieces with the same id still build closed, and every face names that id", [] {
    for (Path path : r476_paths()) {
      const R476Built b = r476e_build({box_piece(5, {2, 2, 2}, {0, 0, 0}), box_piece(5, {2, 2, 2}, {1, 0.5f, 0.25f}), box_piece(5, {1, 1, 1}, {0, 0, 0}, Op::Subtract)},
                                      r476e_settings(path));
      CHECK(b.ok && r476e_hard(b, r476_name(path)));
      for (uint64_t id : b.faces.piece) CHECK(id == 5);
      CHECK(b.mesh.face_count() > 0);
      CHECK_NEAR(r476_volume(b.mesh), 16.0 - 1.5 * 1.75 - 1.0, 16.0 * 0.01);  // (the cutter lies inside: a hollow)
    }
  });

  test("blockout: a slab with sixteen holes cut through it has sixteen handles and the closed-form volume", [] {
    for (Path path : r476_paths()) {
      std::vector<Piece> pieces{box_piece(1, {6, 1, 6}, {0, 0, 0})};
      uint64_t id = 2;
      for (int j = 0; j < 4; j++)
        for (int i = 0; i < 4; i++) pieces.push_back(box_piece(id++, {0.5f, 3, 0.5f}, {-2.25f + 1.5f * (float)i, 0, -2.25f + 1.5f * (float)j}, Op::Subtract));
      const R476Built b = r476e_build(pieces, r476e_settings(path));
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      std::printf("    %s slab: vol %.5f chi %d unresolved %zu faces %zu\n", r476_name(path), r476_volume(b.mesh), euler_characteristic(b.mesh), b.report.unresolved, b.mesh.face_count());
      CHECK_NEAR(r476_volume(b.mesh), 36.0 - 16 * 0.25, path == Path::Crisp ? 32.0e-5 : 0.32);  // (the field path: 1 percent)
      CHECK(euler_characteristic(b.mesh) == 2 - 2 * 16);
      /* (This failed when it was written: holes in a row were all bridged to the first of them, the face maker gave
       * the two big faces up, and the slab came out in 6,468 small faces with a volume 0.45 percent short. Fixed in
       * the same round - bridge_hole takes the nearest hole in line.) */
      CHECK(b.report.unresolved == 0);
      CHECK(b.mesh.face_count() < 400);  // (top and bottom one face each less holes, 16 x 4 walls, 16 x 2 pieces of rim)
    }
  });

  test("blockout: a cylinder and a sphere with the fewest and the most sides build closed alone", [] {
    for (Path path : r476_paths()) {
      for (int sides : {3, 4, 256}) {
        const R476Built b = r476e_build({cylinder_piece(1, 0.5f, 1.0f, sides, {0.25f, 0.5f, 0.125f})}, r476e_settings(path));
        CHECK(b.ok && r476e_hard(b, r476_name(path)));
        if (path == Path::Crisp) CHECK(b.mesh.face_count() == (size_t)sides + 2 && b.mesh.vert_count() == (size_t)(2 * sides));
        CHECK(b.mesh.face_count() > 0);
      }
      for (const std::pair<int, int> sr : {std::pair<int, int>(3, 2), std::pair<int, int>(4, 2), std::pair<int, int>(256, 128), std::pair<int, int>(3, 128)}) {
        const R476Built b = r476e_build({sphere_piece(1, 1.0f, sr.first, sr.second, {0, 0, 0})}, r476e_settings(path, 0.05f));
        CHECK(b.ok && r476e_hard(b, r476_name(path)));
        if (path == Path::Crisp) CHECK(b.mesh.face_count() == (size_t)(sr.first * sr.second) && b.mesh.vert_count() == (size_t)(sr.first * (sr.second - 1) + 2));
        CHECK(b.mesh.face_count() > 0);
      }
    }
  });

  /* ---- Criterion 2: the crisp path is exact (the libraries build only) ---- */
  test("blockout: crisp, a cylinder of s sides is s + 2 faces and 2 s corners, a sphere its own faces", [] {
    if (!blockout::crisp_available()) return;
    for (int sides : {3, 5, 12, 24, 256}) {
      const R476Built b = r476e_build({cylinder_piece(1, 0.75f, 1.5f, sides, {0.25f, 0.5f, 0.125f}, Op::Add, Quat::euler({20, 30, 40}))}, r476e_settings(Path::Crisp));
      CHECK(b.ok && r476_sound(b.mesh, "crisp cylinder"));
      CHECK(b.mesh.face_count() == (size_t)sides + 2);
      CHECK(b.mesh.vert_count() == (size_t)(2 * sides));
    }
    for (const std::pair<int, int> sr : {std::pair<int, int>(3, 2), std::pair<int, int>(16, 8), std::pair<int, int>(24, 12)}) {
      const R476Built b = r476e_build({sphere_piece(1, 0.75f, sr.first, sr.second, {0.25f, 0.5f, 0.125f})}, r476e_settings(Path::Crisp));
      CHECK(b.ok && r476_sound(b.mesh, "crisp sphere"));
      CHECK(b.mesh.face_count() == (size_t)(sr.first * sr.second));
      CHECK(b.mesh.vert_count() == (size_t)(sr.first * (sr.second - 1) + 2));
    }
  });

  test("blockout: crisp, boxes along the axes at eighths lie exactly on their sides", [] {
    if (!blockout::crisp_available()) return;
    const std::vector<std::vector<Piece>> arrangements{
        {box_piece(1, {2, 2, 2}, {0, 0, 0}), box_piece(2, {2, 2, 2}, {1, 0.5f, 0.25f})},
        {box_piece(1, {4, 2, 4}, {0, 0, 0}), box_piece(2, {1, 1, 1}, {0.5f, 1.0f, 0.25f}, Op::Subtract)},
        {box_piece(1, {4, 2, 4}, {0, 0, 0}), box_piece(2, {1, 4, 1}, {0.125f, 0, -0.375f}, Op::Subtract)},
        {box_piece(1, {2, 1, 1}, {0, 0, 0}), box_piece(2, {2, 1, 1}, {2, 0, 0}), box_piece(3, {1.5f, 3, 0.25f}, {1, 1, 0}, Op::Subtract)},
        r476_interior(2, 2)};
    for (const std::vector<Piece> &pieces : arrangements) {
      const R476Built b = r476e_build(pieces, r476e_settings(Path::Crisp));
      CHECK(b.ok && r476_sound(b.mesh, "crisp exact"));
      CHECK(r476_flatness(b, pieces) == 0.0);
    }
  });

  test("blockout: crisp, the volumes of the spec's list are the closed form", [] {
    if (!blockout::crisp_available()) return;
    auto volume = [](const std::vector<Piece> &pieces) { return r476_volume(r476e_build(pieces, r476e_settings(Path::Crisp)).mesh); };
    CHECK_NEAR(volume({box_piece(1, {2, 2, 2}, {0, 0, 0}), box_piece(2, {2, 2, 2}, {1, 0.5f, 0.25f})}), 16.0 - 1.5 * 1.75, 16.0e-5);
    CHECK_NEAR(volume({box_piece(1, {4, 2, 4}, {0, 0, 0}), box_piece(2, {1, 1, 1}, {0.5f, 1.0f, 0.25f}, Op::Subtract)}), 32.0 - 0.5, 32.0e-5);
    CHECK_NEAR(volume({box_piece(1, {4, 2, 4}, {0, 0, 0}), box_piece(2, {1, 4, 1}, {0, 0, 0}, Op::Subtract)}), 32.0 - 2.0, 32.0e-5);
    CHECK_NEAR(volume({box_piece(1, {4, 2, 4}, {0, 0, 0}), box_piece(2, {1, 1, 1}, {0, 0.5f, 0}, Op::Subtract)}), 32.0 - 1.0, 32.0e-5);
    CHECK_NEAR(volume({box_piece(1, {2, 1, 2}, {0, 0.5f, 0}), box_piece(2, {2, 2, 2}, {2, 1, 0}), box_piece(3, {1, 1, 1}, {4.5f, 0.5f, 0})}), 4.0 + 8.0 + 1.0, 13.0e-5);
    CHECK_NEAR(volume(r476_interior(2, 2)), r476_interior_volume(2, 2), r476_interior_volume(2, 2) * 1e-5);
  });

  /* ---- Criterion 3: the field path ---- */
  test("blockout: field, boxes turned 30 and 45 degrees in an arrangement keep every flat face on its side", [] {
    for (Path path : r476_paths()) {
      const std::vector<Piece> pieces{box_piece(1, {2, 1, 3}, {0, 0, 0}, Op::Add, Quat::euler({0, 30, 0})),
                                      box_piece(2, {1, 2, 1}, {0.8f, 0.3f, 0.5f}, Op::Add, Quat::euler({0, 45, 0})),
                                      box_piece(3, {1.5f, 1, 1.5f}, {-0.9f, 0, 0.8f}, Op::Add, Quat::euler({20, 30, 40})),
                                      box_piece(4, {0.5f, 0.5f, 0.5f}, {0.2f, 0.3f, 0.1f}, Op::Subtract, Quat::euler({0, 45, 0}))};
      const R476Built b = r476e_build(pieces, r476e_settings(path, 0.05f));
      CHECK(b.ok && r476e_sound_or_hard(b, r476_name(path)));
      CHECK(r476e_flat_flatness(b, pieces) <= 1e-5 * r476e_diagonal(b.mesh));
      size_t flat = 0;
      for (uint8_t f : b.faces.flat) flat += f;
      CHECK(flat > 0);  // (the check above is not empty)
      const double v = r476_volume(b.mesh);
      CHECK(v > 6.0 && v < 6.0 + 2.0 + 2.25);
      if (path == Path::Field && blockout::crisp_available()) CHECK_NEAR(v, r476_volume(r476e_build(pieces, r476e_settings(Path::Crisp)).mesh), 0.01 * v);
    }
  });

  test("blockout: field, boxes apart turned any way have the volume of the solids within 1 percent", [] {
    for (Path path : r476_paths()) {
      const std::vector<Piece> pieces{box_piece(1, {2, 1, 3}, {0, 0, 0}, Op::Add, Quat::euler({0, 30, 0})), box_piece(2, {1, 2, 1}, {10, 0, 0}, Op::Add, Quat::euler({0, 45, 0})),
                                      box_piece(3, {1, 1, 4}, {0, 10, 0}, Op::Add, Quat::euler({33, 17, 71}))};
      const R476Built b = r476e_build(pieces, r476e_settings(path, 0.05f));
      CHECK(b.ok && r476e_sound_or_hard(b, r476_name(path)));
      CHECK(b.report.clusters.size() == 3);
      CHECK_NEAR(r476_volume(b.mesh), 6.0 + 2.0 + 4.0, 12.0 * 0.01);
      CHECK(r476e_flat_flatness(b, pieces) <= 1e-5 * 15.0);
    }
  });

  test("blockout: field, parts thinner than the cell stay closed, and the flat faces away from them stay flat", [] {
    for (Path path : r476_paths()) {
      /* A wall 0.02 m thick on a floor, at a cell of 0.1 m. */
      const std::vector<Piece> wall{box_piece(1, {4, 0.2f, 4}, {0, 0, 0}), box_piece(2, {4, 3, 0.02f}, {0, 1.6f, 0.5f})};
      R476Built b = r476e_build(wall, r476e_settings(path, 0.1f));
      CHECK(b.ok && r476e_hard(b, r476_name(path)));
      CHECK(r476e_flat_flatness(b, wall) <= 1e-5 * r476e_diagonal(b.mesh));
      std::printf("    %s thin wall: %zu faces, %zu unresolved, volume %.4f (floor 3.2, wall 0.24 above the floor)\n", r476_name(path), b.mesh.face_count(), b.report.unresolved,
                  r476_volume(b.mesh));
      /* The wall alone, and a box 0.03 m across (it may vanish; what is left is closed). */
      const std::vector<Piece> alone{box_piece(1, {4, 3, 0.02f}, {0, 1.5f, 0})};
      b = r476e_build(alone, r476e_settings(path, 0.1f));
      CHECK(b.ok && r476e_hard(b, r476_name(path)));
      CHECK(r476e_flat_flatness(b, alone) <= 1e-5 * 5.0);
      const std::vector<Piece> speck{box_piece(1, {0.03f, 0.03f, 0.03f}, {0.2f, 0.1f, 0.3f})};
      b = r476e_build(speck, r476e_settings(path, 0.1f));
      CHECK(b.ok && r476e_hard(b, r476_name(path)));
      CHECK(r476e_flat_flatness(b, speck) <= 1e-5 * 0.1);
      /* A slot 0.02 m wide cut into a block. */
      const std::vector<Piece> slot{box_piece(1, {2, 2, 2}, {0, 0, 0}), box_piece(2, {0.02f, 3, 1}, {0.3f, 0, 0}, Op::Subtract)};
      b = r476e_build(slot, r476e_settings(path, 0.1f));
      CHECK(b.ok && r476e_hard(b, r476_name(path)));
      CHECK(r476e_flat_flatness(b, slot) <= 1e-5 * 4.0);
    }
  });

  test("blockout: field, a cell budget too small for the surface makes the cell larger, and the result is sound", [] {
    const std::vector<Piece> pieces = r476_interior(1, 0);
    blockout::Settings st = r476e_settings(Path::Field, 0.05f);
    st.max_cells = 2000;
    const R476Built b = r476e_build(pieces, st);
    CHECK(b.ok && r476e_hard(b, "small budget"));
    CHECK(b.report.clusters.size() == 1 && b.report.clusters[0].cell > 0.05f);
    CHECK(b.report.cell > 0.05f);
    CHECK(b.report.cells <= 4 * (size_t)st.max_cells);  // (the budget is a budget, not a hint)
    CHECK(r476e_flat_flatness(b, pieces) <= 1e-5 * r476e_diagonal(b.mesh));
    /* The same pieces with room for the cell asked for: the cell is the one asked for. */
    const R476Built roomy = r476e_build(pieces, r476e_settings(Path::Field, 0.1f));
    CHECK(roomy.ok && roomy.report.clusters.size() == 1 && roomy.report.clusters[0].cell > 0.0999f && roomy.report.clusters[0].cell < 0.1001f);
    /* A budget of one cell is silly, and still not a crash or an open mesh. */
    st.max_cells = 1;
    CHECK(r476e_built_or_untouched(pieces, st, "budget of one"));
  });

  /* ---- Criterion 4: the same bytes ---- */
  test("blockout: a warm cache after a piece was added or removed equals a build from scratch", [] {
    for (Path path : r476_paths()) {
      std::vector<Piece> pieces = r476e_pile(1, 0);
      for (const Piece &p : r476e_pile(3, 100)) pieces.push_back(p);
      blockout::Cache cache;
      CHECK(r476e_build(pieces, r476e_settings(path), &cache).ok);
      /* A piece added to the second pile. */
      pieces.push_back(box_piece(5, {1, 1, 1}, {101, 0, 0}));
      R476Built warm = r476e_build(pieces, r476e_settings(path), &cache);
      R476Built scratch = r476e_build(pieces, r476e_settings(path));
      CHECK(warm.ok && warm.report.built == 1 && warm.report.cached == 1 && r476_same_mesh(warm, scratch));
      /* A piece taken away from the first pile (the pile is now one piece). */
      pieces.erase(pieces.begin() + 1);
      warm = r476e_build(pieces, r476e_settings(path), &cache);
      scratch = r476e_build(pieces, r476e_settings(path));
      CHECK(warm.ok && warm.report.built == 1 && warm.report.cached == 1 && r476_same_mesh(warm, scratch));
      /* A whole pile taken away: the one left comes from the cache. */
      pieces.erase(pieces.begin());
      warm = r476e_build(pieces, r476e_settings(path), &cache);
      scratch = r476e_build(pieces, r476e_settings(path));
      CHECK(warm.ok && warm.report.built == 0 && warm.report.cached == 1 && r476_same_mesh(warm, scratch));
      /* The very same pieces again: nothing is built. */
      warm = r476e_build(pieces, r476e_settings(path), &cache);
      CHECK(warm.ok && warm.report.built == 0 && warm.report.cached == 1 && r476_same_mesh(warm, scratch));
    }
  });

  test("blockout: a cache used with another cell or another path does not hand back the old result", [] {
    const std::vector<Piece> pieces{box_piece(1, {2, 2, 2}, {0, 0, 0}), box_piece(2, {2, 2, 2}, {1, 0.5f, 0.25f}), sphere_piece(3, 0.6f, 12, 6, {0.5f, 1.2f, 0})};
    blockout::Cache cache;
    const R476Built coarse = r476e_build(pieces, r476e_settings(Path::Field, 0.2f), &cache);
    const R476Built fine = r476e_build(pieces, r476e_settings(Path::Field, 0.07f), &cache);
    const R476Built fine_scratch = r476e_build(pieces, r476e_settings(Path::Field, 0.07f));
    CHECK(coarse.ok && fine.ok);
    CHECK(fine.report.cached == 0 && fine.report.built == 1);
    CHECK(r476_same_mesh(fine, fine_scratch));
    CHECK(fine.report.clusters.size() == 1 && fine.report.clusters[0].cell < 0.0701f && fine.report.clusters[0].cell > 0.0699f);
    /* Back to the first cell: the cache may or may not still hold it, but the bytes are the first build's. */
    const R476Built back = r476e_build(pieces, r476e_settings(Path::Field, 0.2f), &cache);
    CHECK(back.ok && r476_same_mesh(back, coarse));
    if (!blockout::crisp_available()) return;
    const R476Built crisp = r476e_build(pieces, r476e_settings(Path::Crisp, 0.2f), &cache);
    const R476Built crisp_scratch = r476e_build(pieces, r476e_settings(Path::Crisp, 0.2f));
    CHECK(crisp.ok && crisp.report.cached == 0 && crisp.report.clusters[0].path == Path::Crisp && r476_same_mesh(crisp, crisp_scratch));
  });

  test("blockout: one thread and all threads give the same bytes with many clusters", [] {
    for (Path path : r476_paths()) {
      std::vector<Piece> pieces;
      for (int i = 0; i < 12; i++) {
        pieces.push_back(box_piece(10 + 3 * (uint64_t)i, {1, 1, 1}, {20.0f * (float)i, 0, 0}, Op::Add, Quat::euler({0, 7.0f * (float)i, 0})));
        pieces.push_back(cylinder_piece(11 + 3 * (uint64_t)i, 0.4f, 1.2f, 9, {20.0f * (float)i + 0.4f, 0.1f, 0.1f}));
        pieces.push_back(sphere_piece(12 + 3 * (uint64_t)i, 0.3f, 10, 5, {20.0f * (float)i + 0.2f, 0.4f, 0}, i % 2 ? Op::Subtract : Op::Add));
      }
      const R476Built many = r476e_build(pieces, r476e_settings(path));
      JobSystem::global().set_max_threads(1);
      const R476Built one = r476e_build(pieces, r476e_settings(path));
      JobSystem::global().set_max_threads(1 << 30);
      CHECK(many.ok && one.ok && many.report.clusters.size() == 12 && r476_same_mesh(many, one));
      CHECK(r476e_hard(many, r476_name(path)));
    }
  });

  /* ---- Criterion 5: clusters ---- */
  test("blockout: clusters follow the order of their pieces, and a piece moved from one pile to another moves a cluster's worth", [] {
    for (Path path : r476_paths()) {
      /* The far pile (three pieces) listed first, the near one (two) after. */
      std::vector<Piece> pieces{box_piece(1, {1, 1, 1}, {100, 0, 0}), box_piece(2, {1, 1, 1}, {100.5f, 0, 0}), box_piece(3, {1, 1, 1}, {101, 0, 0}),
                                box_piece(4, {1, 1, 1}, {0, 0, 0}), box_piece(5, {1, 1, 1}, {0.5f, 0, 0})};
      R476Built b = r476e_build(pieces, r476e_settings(path));
      CHECK(b.ok && b.report.clusters.size() == 2);
      if (b.report.clusters.size() != 2) continue;
      CHECK(b.report.clusters[0].pieces == 3 && b.report.clusters[1].pieces == 2);
      CHECK(!b.faces.piece.empty() && b.faces.piece.front() <= 3 && b.faces.piece.back() >= 4);  // (the faces lie in the clusters' order)
      /* Piece 3 moves to the other pile: 2 + 3 pieces, still two clusters, and both are new. */
      blockout::Cache cache;
      CHECK(r476e_build(pieces, r476e_settings(path), &cache).ok);
      pieces[2].to_blockout = Mat4::translate({1, 0, 0});
      b = r476e_build(pieces, r476e_settings(path), &cache);
      const R476Built scratch = r476e_build(pieces, r476e_settings(path));
      CHECK(b.ok && b.report.clusters.size() == 2 && b.report.built == 2 && b.report.cached == 0);
      CHECK(r476_same_mesh(b, scratch));
      CHECK(b.report.clusters[0].pieces == 2 && b.report.clusters[1].pieces == 3);
    }
  });

  /* ---- Criterion 6: refusals ---- */
  test("blockout: a build cancelled from another thread returns false and leaves the mesh alone, or finishes", [] {
    const std::vector<Piece> pieces = r476_interior(4, 12);
    for (int delay_ms : {0, 3, 30}) {
      Mesh out = *primitives::cube(2.0f);
      const std::vector<Vec3> before = out.positions;
      const uint64_t version = out.version;
      blockout::Faces faces;
      faces.piece = {42};
      blockout::Report rep;
      std::atomic<bool> cancel{false};
      std::thread stopper([&cancel, delay_ms] {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        cancel.store(true);
      });
      const bool ok = blockout::build(pieces, r476e_settings(Path::Field, 0.05f), out, &faces, &rep, nullptr, &cancel);
      stopper.join();
      if (!ok) {
        CHECK(rep.why == "cancelled");
        CHECK(out.version == version && out.positions == before && faces.piece.size() == 1 && faces.piece[0] == 42);
        continue;
      }
      R476Built b;
      b.ok = true, b.mesh = out, b.faces = faces, b.report = rep;
      CHECK(r476e_hard(b, "finished before the cancel"));
    }
  });

  test("blockout: a build cancelled while it is at work stops and says so", [] {
    /* (The test above passes even if a cancel is only ever seen before the build begins. Here the flag is set a
     * moment after the build starts, on ever larger buildings, until one build is cut short inside its cluster -
     * which must happen long before the list ends on any machine. No time is asserted.) */
    for (Path path : r476_paths()) {
      bool caught = false;
      for (int rooms = 6; rooms <= 30 && !caught; rooms += 4) {
        const std::vector<Piece> pieces = r476_interior(rooms, rooms * (rooms - 1));
        for (int delay_ms : {1, 4, 12}) {
          Mesh out = *primitives::cube(2.0f);
          const std::vector<Vec3> before = out.positions;
          blockout::Report rep;
          std::atomic<bool> cancel{false};
          std::thread stopper([&cancel, delay_ms] {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            cancel.store(true);
          });
          const bool ok = blockout::build(pieces, r476e_settings(path, 0.05f), out, nullptr, &rep, nullptr, &cancel);
          stopper.join();
          if (ok) continue;
          CHECK(rep.why == "cancelled" && out.positions == before);
          /* (build() asks the flag once more when all the work is done: only a refusal that names the cluster it
           * cut short shows the flag was seen inside the work.) */
          caught = caught || rep.stopped_inside >= 1;
          if (caught) break;
        }
      }
      CHECK(caught);
    }
  });

  test("blockout: every skipped piece is named once with its reason, whatever its shape", [] {
    for (Path path : r476_paths()) {
      std::vector<Piece> pieces{box_piece(1, {1, 1, 1}, {0, 0, 0}), cylinder_piece(2, kNaN, 1.0f, 8, {0, 0, 0}), sphere_piece(3, kInf, 8, 4, {0, 0, 0}),
                                cylinder_piece(4, 0.5f, 0.0f, 8, {0.5f, 0, 0}), box_piece(5, {-1, 1, 1}, {0.5f, 0, 0}), box_piece(6, {1, 1, 1}, {0.5f, 0, 0})};
      pieces[5].to_blockout = Mat4::scale({0, 1, 1});
      pieces[0].blend = kNaN;
      const R476Built b = r476e_build(pieces, r476e_settings(path));
      CHECK(b.ok);
      std::vector<uint64_t> seen;
      for (const blockout::Skipped &s : b.report.skipped) {
        seen.push_back(s.id);
        CHECK(!s.why.empty());
      }
      /* Piece 1 has a Blend radius that is not a number: skipped too, or built as a plain one - never a closed lie. */
      CHECK(r476e_hard(b, r476_name(path)));
      for (uint64_t id : {2, 3, 4, 6}) CHECK(std::count(seen.begin(), seen.end(), id) == 1);
      CHECK(std::count(seen.begin(), seen.end(), 5) <= 1);
    }
  });

  /* ---- Criterion 7: faces know where they came from ---- */
  test("blockout: every side of a box is named on the face that lies on it", [] {
    const Vec3 want[6] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    for (Path path : r476_paths()) {
      const R476Built b = r476e_build({box_piece(7, {2, 1, 3}, {0.25f, 0.5f, -0.125f})}, r476e_settings(path));
      CHECK(b.ok && r476e_hard(b, r476_name(path)));
      CHECK(b.mesh.face_count() == 6);
      for (size_t f = 0; f < b.mesh.face_count() && f < b.faces.side.size(); f++) {
        CHECK(b.faces.side[f] < 6 && b.faces.flat[f] == 1);
        if (b.faces.side[f] >= 6) continue;
        CHECK(dot(r476e_normal(b.mesh, f), want[b.faces.side[f]]) > 0.999f);
      }
    }
  });

  test("blockout: a cylinder's faces name its side, its bottom and its top, a sphere's all name 0", [] {
    for (Path path : r476_paths()) {
      const R476Built b = r476e_build({cylinder_piece(3, 0.5f, 1.0f, 12, {0.25f, 0.5f, 0.125f})}, r476e_settings(path, 0.05f));
      CHECK(b.ok && r476e_hard(b, r476_name(path)));
      CHECK(r476e_count_side(b, 3, 0) == 12 && r476e_count_side(b, 3, 1) == 1 && r476e_count_side(b, 3, 2) == 1);
      for (size_t f = 0; f < b.mesh.face_count() && f < b.faces.side.size(); f++) {
        const Vec3 n = r476e_normal(b.mesh, f);
        if (b.faces.side[f] == 0) CHECK(std::fabs(n.y) < 1e-3f);
        if (b.faces.side[f] == 1) CHECK(n.y < -0.999f);
        if (b.faces.side[f] == 2) CHECK(n.y > 0.999f);
      }
      const R476Built s = r476e_build({sphere_piece(4, 0.5f, 16, 8, {0.1f, 0.2f, 0.3f})}, r476e_settings(path, 0.03f));
      CHECK(s.ok && r476e_hard(s, r476_name(path)));
      CHECK(s.mesh.face_count() == 16 * 8);
      for (size_t f = 0; f < s.faces.side.size(); f++) CHECK(s.faces.side[f] == 0 && s.faces.piece[f] == 4);
    }
  });

  test("blockout: a pocket cut by a cylinder names the cylinder's side and bottom, the box keeps its top", [] {
    for (Path path : r476_paths()) {
      const R476Built b = r476e_build({box_piece(1, {4, 2, 4}, {0, 0, 0}), cylinder_piece(2, 0.5f, 1.0f, 12, {0.5f, 1.0f, 0.25f}, Op::Subtract)}, r476e_settings(path, 0.05f));
      CHECK(b.ok && r476e_hard(b, r476_name(path)));
      CHECK(r476e_count_side(b, 2, 0) > 0 && r476e_count_side(b, 2, 1) > 0 && r476e_count_side(b, 2, 2) == 0);  // (its top is above the box: no face there)
      CHECK(r476e_count_side(b, 1, 3) >= 1);
      for (size_t f = 0; f < b.mesh.face_count() && f < b.faces.side.size(); f++)
        if (b.faces.piece[f] == 2 && b.faces.side[f] == 1) CHECK(r476e_normal(b.mesh, f).y > 0.999f);  // (the floor of the pocket faces up)
      /* The cylinder cut out of the box's side: its faces still name it, none name the cutter's top. */
      CHECK_NEAR(r476_volume(b.mesh), 32.0 - 0.375, 0.04);  // (a 12-gon of radius 0.5 is 0.75 m2, 0.5 m deep)
    }
  });

  test("blockout: the Faces arrays are as long as the mesh has faces, empty when nothing is left", [] {
    for (Path path : r476_paths()) {
      const std::vector<std::vector<Piece>> arrangements{r476_interior(2, 2), {box_piece(1, {1, 1, 1}, {0, 0, 0}), box_piece(2, {2, 2, 2}, {0, 0, 0}, Op::Subtract)},
                                                         {sphere_piece(1, 1.0f, 8, 4, {0, 0, 0}), box_piece(2, {1, 1, 1}, {0.5f, 0.5f, 0.5f}, Op::Subtract)}, {}};
      for (const std::vector<Piece> &pieces : arrangements) {
        const R476Built b = r476e_build(pieces, r476e_settings(path));
        CHECK(b.ok && b.faces.piece.size() == b.mesh.face_count() && b.faces.side.size() == b.mesh.face_count() && b.faces.flat.size() == b.mesh.face_count());
      }
    }
  });
}

/* ===================================================================== */
/* Round 477 (task 0453, ADR 0129): the Blockout object, live                */
/* ===================================================================== */

namespace {

using R477ET = platform::EventType;

platform::Event r477_ev(R477ET t, int x, int y, int mods = 0) {
  platform::Event e;
  e.type = t, e.x = x, e.y = y, e.mods = mods;
  return e;
}
platform::Event r477_key(int key, bool down = true) {
  platform::Event e;
  e.type = down ? R477ET::KeyDown : R477ET::KeyUp, e.key = key;
  return e;
}

/* Byte for byte: the positions and the faces. */
bool r477_same(const Mesh &a, const Mesh &b) {
  return a.positions.size() == b.positions.size() && a.face_offsets == b.face_offsets && a.corner_verts == b.corner_verts &&
         (a.positions.empty() || std::memcmp(a.positions.data(), b.positions.data(), a.positions.size() * sizeof(Vec3)) == 0);
}

GameObject *r477_find(Editor &ed, const char *name = "Blockout") { return ed.scene().find_by_name(name); }
const Mesh *r477_mesh(GameObject *g) {
  const MeshFilter *mf = g ? g->get<MeshFilter>() : nullptr;
  return mf && mf->mesh ? mf->mesh.get() : nullptr;
}

/* What blockout::build makes of the Blockout's snapshot with no cache and no editor: the mesh the editor's must equal. */
bool r477_scratch(const GameObject &g, Mesh &out) {
  std::vector<Piece> pieces;
  blockout::Settings st;
  blockout_snapshot(g, pieces, st);
  return blockout::build(pieces, st, out);
}
bool r477_matches(Editor &ed, const char *name = "Blockout") {
  GameObject *g = r477_find(ed, name);
  const Mesh *m = r477_mesh(g);
  Mesh ref;
  return g && m && r477_scratch(*g, ref) && r477_same(ref, *m);
}

/* A Blockout object made by hand (no editor command): the object, its mesh and renderer. */
GameObject *r477_blockout(Editor &ed, const char *name = "Blockout") {
  GameObject *g = ed.scene().create(name);
  g->add<Blockout>();
  g->add<MeshFilter>()->mesh = std::make_shared<Mesh>();
  g->add<MeshRenderer>();
  return g;
}
GameObject *r477_piece(Editor &ed, GameObject &parent, const char *name, int shape, int op, Vec3 at, Vec3 size = Vec3(1.0f), float radius = 0.5f, float height = 1.0f) {
  GameObject *g = ed.scene().create(name, &parent);
  BlockoutPiece *p = g->add<BlockoutPiece>();
  p->shape = shape, p->operation = op, p->size = size, p->radius = radius, p->height = height;
  g->set_local_position(at);
  return g;
}

/* Rooms on a square grid as the stress section builds them: an Add box with a Subtract box inside, doors through shared walls. */
std::vector<GameObject *> r477_rooms(Editor &ed, GameObject &bo, int n, int doors) {
  std::vector<GameObject *> out;
  for (int j = 0; j < n; j++)
    for (int i = 0; i < n; i++) {
      const Vec3 at(4.0f * (float)i, 0.0f, 4.0f * (float)j);
      out.push_back(r477_piece(ed, bo, "Shell", 0, 0, at, {4.2f, 3.2f, 4.2f}));
      out.push_back(r477_piece(ed, bo, "Room", 0, 1, at, {3.8f, 2.8f, 3.8f}));
    }
  for (int j = 0; j < n && doors > 0; j++)
    for (int i = 0; i + 1 < n && doors > 0; i++, doors--)
      out.push_back(r477_piece(ed, bo, "Door", 0, 1, {4.0f * (float)i + 2.0f, -0.35f, 4.0f * (float)j}, {0.6f, 2.1f, 0.9f}));
  return out;
}

std::vector<GameObject *> r477_all_pieces(GameObject &bo) {
  std::vector<GameObject *> out;
  std::function<void(GameObject &)> walk = [&](GameObject &g) {
    for (GameObject *c : g.children) {
      if (c->get<Blockout>()) continue;
      if (c->get<BlockoutPiece>()) out.push_back(c);
      walk(*c);
    }
  };
  walk(bo);
  return out;
}

/* The log lines written since `mark` (r477_mark's), joined; and how many of them warn. */
size_t r477_mark() {
  std::vector<LogEntry> tmp;
  return Log::fetch((size_t)1 << 40, tmp);
}
std::string r477_log_since(size_t mark, size_t *warnings = nullptr) {
  std::vector<LogEntry> v;
  Log::fetch(mark, v);
  std::string s;
  if (warnings) *warnings = 0;
  for (const LogEntry &e : v) {
    s += e.text + "\n";
    if (warnings && e.level != LogLevel::Info) (*warnings)++;
  }
  return s;
}

/* Nothing in the scene: the default scene's plane, cube and teapot would be floors and occluders of their own. */
void r477_empty_scene(Editor &ed) {
  std::vector<GameObject *> roots;
  ed.scene().for_each([&](GameObject &g) {
    if (!g.parent) roots.push_back(&g);
  });
  ed.scene().destroy(roots);
  ed.clear_selection_for_test();
}

void r477_settle(Editor &ed, int frames = 2) {
  for (int i = 0; i < frames; i++) ed.step_frame_headless();
}

void r477_click(Editor &ed, int x, int y, int mods = 0) {
  ed.step_frame_headless({r477_ev(R477ET::MouseMove, x, y, mods)});
  ed.step_frame_headless({r477_ev(R477ET::MouseDown, x, y, mods)});
  ed.step_frame_headless({r477_ev(R477ET::MouseUp, x, y, mods)});
}

/* A menu reached by real events: each key but the last is a menubar entry clicked or a submenu hovered, the last is clicked. */
bool r477_menu(Editor &ed, const std::vector<std::string> &keys) {
  for (size_t i = 0; i < keys.size(); i++) {
    Recti r;
    for (int k = 0; k < 4 && !ed.button_rect_for_test(keys[i], r); k++) ed.step_frame_headless();
    if (!ed.button_rect_for_test(keys[i], r)) return false;
    const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    if (i + 1 < keys.size() && keys[i].rfind("submenu:", 0) == 0) {
      ed.step_frame_headless({r477_ev(R477ET::MouseMove, cx, cy)});
      ed.step_frame_headless();
      continue;
    }
    for (R477ET t : {R477ET::MouseMove, R477ET::MouseDown, R477ET::MouseUp}) ed.step_frame_headless({r477_ev(t, cx, cy)});
    ed.step_frame_headless();
  }
  return true;
}

size_t r477_count(Editor &ed, bool pieces) {
  size_t n = 0;
  ed.scene().for_each([&](GameObject &g) { n += pieces ? g.get<BlockoutPiece>() != nullptr : g.get<Blockout>() != nullptr; });
  return n;
}

/* The newest piece made: the active object. */
const BlockoutPiece *r477_active_piece(Editor &ed) {
  GameObject *g = ed.active_object_for_test();
  return g ? g->get<BlockoutPiece>() : nullptr;
}

/* Pixels near a colour (the packing is 0xAARRGGBB; lines are anti-aliased and blended, so a band). */
size_t r477_near(const std::vector<uint32_t> &px, uint32_t rgb, int tol) {
  size_t n = 0;
  const int r = (int)((rgb >> 16) & 255), g = (int)((rgb >> 8) & 255), b = (int)(rgb & 255);
  for (uint32_t p : px)
    n += std::abs((int)((p >> 16) & 255) - r) <= tol && std::abs((int)((p >> 8) & 255) - g) <= tol && std::abs((int)(p & 255) - b) <= tol;
  return n;
}

struct R477Rand {
  uint32_t s;
  float next() {
    s = s * 1664525u + 1013904223u;
    return (float)(s >> 8) / 16777216.0f;
  }
  float in(float lo, float hi) { return lo + (hi - lo) * next(); }
  int pick(int n) { return std::min(n - 1, (int)(next() * (float)n)); }
};

}  // namespace

static void tests_round477() {
  /* ---- Making one ---- */
  test("blockout: 'blockout new' makes an object with a closed box mesh, its one piece selected, and the piece has no mesh or collider", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.command("blockout new");
    GameObject *bo = r477_find(ed);
    CHECK(bo && bo->get<Blockout>() && bo->get<MeshFilter>() && bo->get<MeshRenderer>());
    const Mesh *m = r477_mesh(bo);
    CHECK(m && m->face_count() == 6 && structurally_valid(*m) && closed_manifold(*m) && normals_outward(*m));
    GameObject *box = r477_find(ed, "Box");
    CHECK(box && box->parent == bo && box->get<BlockoutPiece>() && !box->get<MeshFilter>() && !box->get<ColliderComponent>() && !box->get<MeshRenderer>());
    CHECK(ed.active_object_for_test() == box && ed.selection_size_for_test() == 1);
    CHECK(box && box->get<BlockoutPiece>()->size.x == 2.0f && box->get<BlockoutPiece>()->size.y == 2.0f && box->get<BlockoutPiece>()->size.z == 2.0f);
    CHECK(r477_matches(ed));
    CHECK(ed.blockout_main_builds_for_test() >= 1);  // (no workers in a unit test: built on the spot)
  });

  test("blockout: 'blockout add' puts a box, cylinder or sphere that adds or subtracts under the selected Blockout, with the sizes it was given", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.command("blockout new");
    ed.command("blockout add cylinder subtract 0.4 3");
    const BlockoutPiece *cyl = r477_active_piece(ed);
    CHECK(cyl && cyl->shape == 1 && cyl->operation == 1 && cyl->radius == 0.4f && cyl->height == 3.0f);
    CHECK(ed.active_object_for_test() && ed.active_object_for_test()->parent == r477_find(ed));
    ed.command("blockout add sphere 0.7");
    const BlockoutPiece *sph = r477_active_piece(ed);
    CHECK(sph && sph->shape == 2 && sph->operation == 0 && sph->radius == 0.7f);
    ed.command("blockout add box 0.5 0.25 0.75");  // (a piece selected: the new one goes under the same Blockout)
    const BlockoutPiece *box = r477_active_piece(ed);
    CHECK(box && box->shape == 0 && box->size.x == 0.5f && box->size.y == 0.25f && box->size.z == 0.75f);
    CHECK(r477_count(ed, true) == 4 && r477_count(ed, false) == 1);
    CHECK(r477_matches(ed));
    const size_t before = r477_count(ed, true);
    ed.command("blockout add pyramid");  // (a shape it doesn't know: refused, nothing made)
    CHECK(r477_count(ed, true) == before);
  });

  test("blockout: 'blockout add' with nothing selected makes a new Blockout with that piece, and the piece is one undo step", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.clear_selection_for_test();
    r477_settle(ed);
    const size_t depth = ed.undo_depth_for_test();
    ed.command("blockout add sphere 1");
    CHECK(r477_count(ed, false) == 1 && r477_count(ed, true) == 1);
    r477_settle(ed);
    CHECK(ed.undo_depth_for_test() == depth + 1);
    GameObject *bo = r477_find(ed);
    CHECK(bo && r477_mesh(bo) && r477_mesh(bo)->face_count() > 0);
    ed.command("undo");
    r477_settle(ed);
    CHECK(r477_count(ed, false) == 0 && r477_count(ed, true) == 0);
    ed.command("redo");
    r477_settle(ed);
    CHECK(r477_count(ed, false) == 1 && r477_count(ed, true) == 1 && r477_matches(ed));
  });

  /* ---- Criterion 1: every way of changing the pieces gives the mesh of a build from scratch ---- */
  test("blockout: a piece moved, turned, scaled, resized, reshaped, added, deleted, disabled or re-parented gives the mesh of a build from scratch; undo and redo give each state's mesh", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.command("blockout new");
    GameObject *bo = r477_find(ed);
    r477_piece(ed, *bo, "Cutter", 1, 1, {0.3f, 0.0f, 0.2f}, Vec3(1.0f), 0.4f, 3.0f);
    r477_piece(ed, *bo, "Ball", 2, 0, {1.2f, 0.5f, 0.0f}, Vec3(1.0f), 0.7f);
    ed.scene().create("Folder", bo);
    ed.mark_changed_for_test("setup");
    r477_settle(ed);
    CHECK(r477_matches(ed));
    std::vector<std::string> labels;
    std::vector<Mesh> after;  // (each state's mesh, by a build from scratch)
    auto step = [&](const char *label, const std::function<void()> &act) {
      act();
      ed.mark_changed_for_test(label);
      r477_settle(ed);
      const bool ok = r477_matches(ed);
      if (!ok) std::printf("    '%s': the editor's mesh differs from a build from scratch\n", label);
      CHECK(ok);
      Mesh ref;
      r477_scratch(*r477_find(ed), ref);
      labels.push_back(label), after.push_back(std::move(ref));
    };
    auto piece = [&](const char *name) { return r477_find(ed, name); };  // (an undo makes the objects again: found by name each time)
    step("move", [&] { piece("Ball")->set_local_position({1.0f, 0.6f, 0.3f}); });
    step("turn", [&] { piece("Cutter")->set_local_euler({20.0f, 30.0f, 10.0f}); });
    step("scale evenly", [&] { piece("Box")->set_local_scale({1.2f, 1.2f, 1.2f}); });
    step("scale unevenly", [&] { piece("Ball")->set_local_scale({1.0f, 0.6f, 1.4f}); });
    step("size", [&] { piece("Box")->get<BlockoutPiece>()->size = {2.0f, 1.5f, 2.5f}; });
    step("radius", [&] { piece("Cutter")->get<BlockoutPiece>()->radius = 0.55f; });
    step("shape", [&] { piece("Ball")->get<BlockoutPiece>()->shape = 1; });
    step("operation", [&] { piece("Ball")->get<BlockoutPiece>()->operation = 1; });
    step("add", [&] { r477_piece(ed, *r477_find(ed), "Extra", 0, 0, {-1.0f, 0.2f, 0.3f}, {0.6f, 0.6f, 0.6f}); });
    step("disable", [&] { piece("Extra")->active = false; });
    step("enable", [&] { piece("Extra")->active = true; });
    step("component disabled", [&] { piece("Extra")->get<BlockoutPiece>()->enabled = false; });
    step("component enabled", [&] { piece("Extra")->get<BlockoutPiece>()->enabled = true; });
    step("under an empty child", [&] { ed.scene().set_parent(piece("Extra"), piece("Folder"), -1, true); });
    step("out of the Blockout", [&] { ed.scene().set_parent(piece("Extra"), nullptr, -1, true); });
    step("back in", [&] { ed.scene().set_parent(piece("Extra"), r477_find(ed), -1, true); });
    step("delete", [&] { ed.scene().destroy(piece("Extra")); });
    /* Undo: each step back restores the mesh of the state before it. */
    for (size_t i = after.size(); i-- > 1;) {
      ed.command("undo");
      r477_settle(ed);
      const Mesh *m = r477_mesh(r477_find(ed));
      const bool ok = m && r477_same(*m, after[i - 1]) && r477_matches(ed);
      if (!ok) std::printf("    undo of '%s': the mesh is not the one before it\n", labels[i].c_str());
      CHECK(ok);
    }
    for (size_t i = 1; i < after.size(); i++) {
      ed.command("redo");
      r477_settle(ed);
      const Mesh *m = r477_mesh(r477_find(ed));
      const bool ok = m && r477_same(*m, after[i]) && r477_matches(ed);
      if (!ok) std::printf("    redo of '%s': the mesh is not the one after it\n", labels[i].c_str());
      CHECK(ok);
    }
  });

  test("blockout: a piece moved into another Blockout and out of the first changes both meshes to builds from scratch", [] {
    Editor ed;
    ed.init_headless(900, 600);
    GameObject *a = r477_blockout(ed, "Blockout");
    GameObject *b = r477_blockout(ed, "Blockout B");
    b->set_local_position({10.0f, 0.0f, 0.0f});
    r477_piece(ed, *a, "Wall", 0, 0, {0, 0, 0}, {2, 2, 2});
    GameObject *slab = r477_piece(ed, *a, "Slab", 0, 0, {0.5f, 1.5f, 0}, {1, 1, 1});
    r477_piece(ed, *b, "Floor", 0, 0, {0, 0, 0}, {3, 1, 3});
    r477_settle(ed);
    CHECK(r477_matches(ed, "Blockout") && r477_matches(ed, "Blockout B"));
    const size_t faces_a = r477_mesh(a)->face_count(), faces_b = r477_mesh(b)->face_count();
    ed.scene().set_parent(slab, b, -1, true);
    r477_settle(ed);
    CHECK(r477_matches(ed, "Blockout") && r477_matches(ed, "Blockout B"));
    CHECK(r477_mesh(a)->face_count() == 6 && r477_mesh(b)->face_count() != faces_b);
    std::printf("    faces: A %zu -> %zu, B %zu -> %zu\n", faces_a, r477_mesh(a)->face_count(), faces_b, r477_mesh(b)->face_count());
    ed.scene().set_parent(slab, a, -1, true);
    r477_settle(ed);
    CHECK(r477_matches(ed, "Blockout") && r477_matches(ed, "Blockout B") && r477_mesh(a)->face_count() == faces_a && r477_mesh(b)->face_count() == faces_b);
  });

  test("blockout: moving, turning and scaling the Blockout itself keeps the same mesh and builds nothing", [] {
    Editor ed;
    ed.init_headless(900, 600);
    GameObject *bo = r477_blockout(ed);
    GameObject *parent = ed.scene().create("Holder");
    ed.scene().set_parent(bo, parent, -1, true);
    r477_piece(ed, *bo, "Wall", 0, 0, {0, 0, 0}, {2, 2, 2});
    r477_piece(ed, *bo, "Cut", 1, 1, {0.2f, 0.1f, 0}, Vec3(1.0f), 0.4f, 3.0f);
    r477_settle(ed);
    const Mesh *built = r477_mesh(bo);
    const uint64_t builds = ed.blockout_main_builds_for_test();
    CHECK(built && builds >= 1);
    bo->set_local_position({3.0f, 1.0f, -2.0f});
    r477_settle(ed);
    bo->set_local_euler({10.0f, 70.0f, 5.0f});
    r477_settle(ed);
    bo->set_local_scale({2.0f, 0.5f, 1.5f});
    r477_settle(ed);
    parent->set_local_euler({0.0f, 33.0f, 0.0f});
    parent->set_local_scale({1.3f, 1.3f, 1.3f});
    r477_settle(ed);
    CHECK(r477_mesh(bo) == built);
    CHECK(ed.blockout_main_builds_for_test() == builds);
    CHECK(r477_matches(ed));
  });

  test("blockout: a piece under a turned, unevenly scaled parent is built where it stands in the Blockout, and a mirrored parent keeps the mesh closed", [] {
    Editor ed;
    ed.init_headless(900, 600);
    GameObject *bo = r477_blockout(ed);
    r477_piece(ed, *bo, "Wall", 0, 0, {0, 0, 0}, {3, 2, 3});
    GameObject *group = ed.scene().create("Group", bo);
    group->set_local_euler({0.0f, 40.0f, 0.0f});
    group->set_local_scale({1.5f, 0.8f, 1.0f});
    group->set_local_position({0.5f, 0.2f, 0.0f});
    r477_piece(ed, *group, "Cut", 0, 1, {0.2f, 0.0f, 0.0f}, {0.6f, 0.6f, 4.0f});
    r477_settle(ed);
    CHECK(r477_matches(ed));
    const Mesh *m = r477_mesh(bo);
    CHECK(m && closed_manifold(*m) && structurally_valid(*m) && signed_volume(*m) > 1.0f);
    group->set_local_scale({-1.5f, 0.8f, 1.0f});  // (a mirror)
    r477_settle(ed);
    m = r477_mesh(bo);
    CHECK(r477_matches(ed) && m && closed_manifold(*m) && structurally_valid(*m) && signed_volume(*m) > 1.0f);
  });

  test("blockout: a Blockout's pieces are the enabled pieces active below it, depth first, down to but not into another Blockout, each at its matrix in the Blockout's space", [] {
    Editor ed;
    ed.init_headless(900, 600);
    GameObject *bo = r477_blockout(ed);
    bo->set_local_position({7.0f, 0.0f, 0.0f});
    bo->set_local_scale({2.0f, 2.0f, 2.0f});
    GameObject *a = r477_piece(ed, *bo, "A", 0, 0, {0, 0, 0});
    GameObject *folder = ed.scene().create("Folder", bo);
    folder->set_local_position({0.0f, 5.0f, 0.0f});
    GameObject *b = r477_piece(ed, *folder, "B", 0, 0, {1.0f, 0.0f, 0.0f});
    GameObject *c = r477_piece(ed, *b, "C", 1, 1, {0.0f, 0.0f, 2.0f});
    GameObject *inner = r477_blockout(ed, "Inner");
    ed.scene().set_parent(inner, bo, -1, false);
    GameObject *inner_piece = r477_piece(ed, *inner, "InnerPiece", 2, 0, {0, 0, 0});
    GameObject *off = r477_piece(ed, *bo, "Off", 0, 0, {0, 3, 0});
    off->active = false;
    r477_piece(ed, *off, "UnderOff", 0, 0, {0, 0, 0});
    GameObject *disabled = r477_piece(ed, *bo, "Disabled", 0, 0, {0, -3, 0});
    disabled->get<BlockoutPiece>()->enabled = false;
    GameObject *under = r477_piece(ed, *disabled, "UnderDisabled", 0, 0, {0, 0, 1.0f});
    std::vector<Piece> pieces;
    blockout::Settings st;
    blockout_snapshot(*bo, pieces, st);
    CHECK(pieces.size() == 4);
    if (pieces.size() != 4) return;
    CHECK(pieces[0].id == a->id && pieces[1].id == b->id && pieces[2].id == c->id && pieces[3].id == under->id);
    CHECK(pieces[1].shape == Shape::Box && pieces[2].shape == Shape::Cylinder && pieces[2].op == Op::Subtract);
    /* In the Blockout's space: the chain of local matrices below it, whatever the Blockout's own transform. */
    CHECK_NEAR(pieces[1].to_blockout.m[12], 1.0f, 1e-6f);
    CHECK_NEAR(pieces[1].to_blockout.m[13], 5.0f, 1e-6f);
    CHECK_NEAR(pieces[2].to_blockout.m[12], 1.0f, 1e-6f);
    CHECK_NEAR(pieces[2].to_blockout.m[14], 2.0f, 1e-6f);
    CHECK_NEAR(pieces[3].to_blockout.m[13], -3.0f, 1e-6f);
    std::vector<Piece> in;
    blockout_snapshot(*inner, in, st);
    CHECK(in.size() == 1 && in[0].id == inner_piece->id && in[0].shape == Shape::Sphere);
    CHECK(blockout_of_piece(*c) == bo && blockout_of_piece(*inner_piece) == inner && blockout_of_piece(*bo) == nullptr);
    /* The hash changes with a piece's bits and not with the Blockout's place. */
    const uint64_t h = blockout_snapshot_hash(pieces, st);
    bo->set_local_position({-1.0f, 2.0f, 3.0f});
    std::vector<Piece> again;
    blockout_snapshot(*bo, again, st);
    CHECK(blockout_snapshot_hash(again, st) == h);
    a->set_local_position({0.0f, 0.0f, 1e-3f});
    blockout_snapshot(*bo, again, st);
    CHECK(blockout_snapshot_hash(again, st) != h);
  });

  /* ---- Criterion 2: save and load ---- */
  test("blockout: a saved scene of five pieces opens with the same mesh bytes, and the rebuild does not mark the scene changed", [] {
    Editor ed;
    ed.init_headless(900, 600);
    GameObject *bo = r477_blockout(ed);
    r477_piece(ed, *bo, "Hall", 0, 0, {0, 0, 0}, {4, 2.5f, 3});
    r477_piece(ed, *bo, "Pocket", 0, 1, {0.3f, 0.2f, 0}, {3, 2, 2});
    r477_piece(ed, *bo, "Pillar", 1, 0, {-1.0f, 0, 0.3f}, Vec3(1.0f), 0.25f, 2.0f);
    r477_piece(ed, *bo, "Dome", 2, 0, {1.0f, 1.2f, 0.0f}, Vec3(1.0f), 0.6f);
    r477_piece(ed, *bo, "Window", 0, 1, {2.0f, 0.2f, 0.0f}, {1.0f, 0.8f, 0.6f});
    r477_settle(ed);
    const Mesh built = *r477_mesh(bo);
    CHECK(built.face_count() > 6 && r477_matches(ed));
    const std::string dir = ed.resolve_asset_path_for_test("Assets/Scenes");
    fs::make_dirs(dir);
    const std::string path = fs::join(dir, "r477_blockout.scene");
    ed.command("save " + path);
    CHECK(fs::exists(path));
    ed.command("newscene");
    r477_settle(ed);
    CHECK(r477_find(ed) == nullptr);
    ed.command("open " + path);
    GameObject *loaded = r477_find(ed);
    CHECK(loaded && r477_count(ed, true) == 5);
    const Mesh *shown = r477_mesh(loaded);
    CHECK(shown && r477_same(*shown, built));  // (shown at once: the saved mesh)
    r477_settle(ed, 3);
    loaded = r477_find(ed);
    CHECK(loaded && r477_mesh(loaded) && r477_same(*r477_mesh(loaded), built) && r477_matches(ed));  // (and rebuilt from the pieces to the same bytes)
    CHECK(!ed.scene_dirty_for_test());
    const BlockoutPiece *pillar = r477_find(ed, "Pillar") ? r477_find(ed, "Pillar")->get<BlockoutPiece>() : nullptr;
    CHECK(pillar && pillar->shape == 1 && pillar->radius == 0.25f && pillar->height == 2.0f);
  });

  /* ---- Criterion 3: a drag does no build on the main thread ---- */
  test("blockout: dragging a piece with workers on builds nothing on the main thread, shows only meshes some snapshot of the drag built, and ends on the build from scratch", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.set_async_jobs_for_test(true);
    GameObject *bo = r477_blockout(ed);
    const std::vector<GameObject *> rooms = r477_rooms(ed, *bo, 3, 4);
    GameObject *door = rooms.back();
    const Vec3 home = door->local().position;
    ed.command("blockout wait");
    CHECK(r477_matches(ed));
    const Mesh start = *r477_mesh(bo);
    /* The meshes the drag's snapshots build from scratch. */
    std::vector<Mesh> refs;
    refs.push_back(start);
    for (int k = 1; k <= 30; k++) {
      door->set_local_position(home + Vec3(0.0f, 0.0f, 0.02f * (float)k));
      Mesh ref;
      CHECK(r477_scratch(*bo, ref));
      refs.push_back(std::move(ref));
    }
    door->set_local_position(home);
    ed.command("blockout wait");
    const uint64_t main0 = ed.blockout_main_builds_for_test(), started0 = ed.blockout_started_for_test(), cancelled0 = ed.blockout_cancelled_for_test();
    ed.hold_blockout_landing_for_test(true);  // (the first half of the drag: builds finish but the poll leaves them, as a slow build's)
    size_t shown_ok = 0, frames = 0;
    bool listed = false;
    for (int k = 1; k <= 30; k++) {
      if (k == 16) ed.hold_blockout_landing_for_test(false);
      door->set_local_position(home + Vec3(0.0f, 0.0f, 0.02f * (float)k));
      ed.step_frame_headless();
      frames++;
      for (const Editor::JobView &j : ed.jobs_for_test()) listed = listed || j.label.rfind("Building ", 0) == 0;
      const Mesh *m = r477_mesh(bo);
      bool known = m && m->face_count() > 0;
      bool any = false;
      for (size_t r = 0; r < refs.size() && known && !any; r++) any = r477_same(*m, refs[r]);
      shown_ok += known && any;
    }
    CHECK(shown_ok == frames);  // (never empty, never half: always a mesh some snapshot built)
    CHECK(ed.blockout_main_builds_for_test() == main0);
    CHECK(ed.blockout_started_for_test() > started0);
    CHECK(ed.blockout_cancelled_for_test() > cancelled0);
    CHECK(listed);  // (the tray lists "Building Blockout")
    std::printf("    30 frames: %llu build(s) started, %llu cancelled or superseded, %llu landed, main-thread builds %llu\n",
                (unsigned long long)(ed.blockout_started_for_test() - started0), (unsigned long long)(ed.blockout_cancelled_for_test() - cancelled0),
                (unsigned long long)ed.blockout_landed_for_test(), (unsigned long long)(ed.blockout_main_builds_for_test() - main0));
    ed.command("blockout wait");
    CHECK(r477_matches(ed) && r477_same(*r477_mesh(bo), refs.back()));
    CHECK(!ed.blockout_jobs_running_for_test());
  });

  test("blockout: with a newer snapshot cancelling the running build (the measurement's other policy), a drag still shows only meshes some snapshot built, and ends on the build from scratch", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.set_async_jobs_for_test(true);
    ed.set_blockout_cancel_stale_for_test(true);
    GameObject *bo = r477_blockout(ed);
    const std::vector<GameObject *> rooms = r477_rooms(ed, *bo, 3, 4);
    GameObject *door = rooms.back();
    const Vec3 home = door->local().position;
    ed.command("blockout wait");
    std::vector<Mesh> refs;
    refs.push_back(*r477_mesh(bo));
    for (int k = 1; k <= 20; k++) {
      door->set_local_position(home + Vec3(0.0f, 0.0f, 0.03f * (float)k));
      Mesh ref;
      CHECK(r477_scratch(*bo, ref));
      refs.push_back(std::move(ref));
    }
    door->set_local_position(home);
    ed.command("blockout wait");
    const uint64_t landed0 = ed.blockout_landed_for_test();
    size_t shown_ok = 0;
    for (int k = 1; k <= 20; k++) {
      door->set_local_position(home + Vec3(0.0f, 0.0f, 0.03f * (float)k));
      ed.step_frame_headless();
      const Mesh *m = r477_mesh(bo);
      bool any = false;
      for (size_t r = 0; r < refs.size() && m && !any; r++) any = r477_same(*m, refs[r]);
      shown_ok += any;
    }
    CHECK(shown_ok == 20);
    ed.command("blockout wait");
    CHECK(r477_matches(ed) && r477_same(*r477_mesh(bo), refs.back()) && ed.blockout_landed_for_test() > landed0 && ed.blockout_main_builds_for_test() == 0);
  });

  test("blockout: the Move gizmo dragged with real mouse events moves a piece and the mesh follows to the build from scratch, without a build on the main thread", [] {
    Editor ed;
    r34_open(ed);
    r477_empty_scene(ed);
    ed.command("camera 0 20 6 0 0.5 0");  // (the X arrow points straight right on screen)
    ed.command("tool move");
    ed.set_async_jobs_for_test(true);
    GameObject *bo = r477_blockout(ed);
    bo->set_local_position({0.0f, 0.5f, 0.0f});
    r477_piece(ed, *bo, "Wall", 0, 0, {0, 0, 0}, {4, 2, 4});
    GameObject *cut = r477_piece(ed, *bo, "Cut", 0, 1, {0.0f, 0.0f, 0.0f}, {1, 1, 6});
    ed.command("blockout wait");
    ed.mark_changed_for_test("setup");
    ed.select_for_test(cut->id, false);
    r34_steps(ed, 3);
    const uint64_t main0 = ed.blockout_main_builds_for_test();
    const Vec3 before = cut->local().position;
    r34_gizmo_drag_x(ed, cut->world_position(), 60);
    CHECK(cut->local().position.x > before.x + 0.05f);
    r34_steps(ed, 2);
    ed.command("blockout wait");
    CHECK(r477_matches(ed) && ed.blockout_main_builds_for_test() == main0);
    ed.command("undo");
    r34_steps(ed, 2);
    ed.command("blockout wait");
    cut = r477_find(ed, "Cut");
    CHECK(cut && std::fabs(cut->local().position.x - before.x) < 1e-4f && r477_matches(ed));
  });

  test("blockout: a build asked for while another runs supersedes it, the cache comes back, and the landed mesh is the newest snapshot's, also after an undo", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.set_async_jobs_for_test(true);
    GameObject *bo = r477_blockout(ed);
    const std::vector<GameObject *> rooms = r477_rooms(ed, *bo, 3, 4);
    ed.command("blockout wait");
    for (int i = 0; i < 5; i++) {
      rooms.back()->set_local_position(rooms.back()->local().position + Vec3(0.0f, 0.0f, 0.05f));
      ed.step_frame_headless();
    }
    ed.command("blockout wait");
    CHECK(!ed.blockout_jobs_running_for_test() && r477_matches(ed));
    /* Undo while a job runs: the hash decides - the mesh landed is the restored state's. */
    ed.mark_changed_for_test("moved");
    ed.step_frame_headless();
    r477_find(ed, "Door")->set_local_position(r477_find(ed, "Door")->local().position + Vec3(0.0f, 0.0f, 0.5f));
    ed.mark_changed_for_test("moved more");
    ed.step_frame_headless();
    ed.command("undo");
    ed.step_frame_headless();
    ed.command("blockout wait");
    CHECK(r477_matches(ed) && !ed.blockout_jobs_running_for_test());
  });

  /* ---- Criterion 4: Play mode collides with the result ---- */
  test("blockout: a box dropped in Play rests on a Blockout floor and, with Blender's libraries, falls through its Subtract hole; no piece collides", [] {
    Editor ed;
    ed.init_headless(900, 600);
    r477_empty_scene(ed);
    GameObject *bo = r477_blockout(ed);
    r477_piece(ed, *bo, "Floor", 0, 0, {0.0f, -0.2f, 0.0f}, {6.0f, 0.4f, 6.0f});
    r477_piece(ed, *bo, "Hole", 0, 1, {0.0f, -0.2f, 0.0f}, {2.0f, 1.0f, 2.0f});
    r477_settle(ed);
    CHECK(r477_mesh(bo) && r477_mesh(bo)->face_count() > 6);
    for (GameObject *p : r477_all_pieces(*bo)) CHECK(!p->get<MeshFilter>() && !p->get<ColliderComponent>() && !p->get<MeshRenderer>());
    auto dropper = [&](const char *name, Vec3 at) {
      GameObject *g = ed.scene().create(name);
      g->add<MeshFilter>()->mesh = primitives::cube();
      g->add<MeshRenderer>();
      g->add<Rigidbody>()->bounciness = 0.0f;
      g->set_local_scale({0.5f, 0.5f, 0.5f});
      g->set_local_position(at);
      return g;
    };
    GameObject *over_floor = dropper("OverFloor", {2.2f, 1.5f, 0.0f});
    GameObject *over_hole = dropper("OverHole", {0.0f, 1.5f, 0.0f});
    PlayContext ctx;
    ed.scene().start(ctx);
    ctx.dt = 1.0f / 60.0f;
    for (int i = 0; i < 240; i++) ed.scene().update(ctx);
    std::printf("    on the floor: y %.3f; over the hole: y %.3f (Jolt %s)\n", over_floor->world_position().y, over_hole->world_position().y,
                physics_jolt_available() ? "on" : "off: the built-in physics sees each static mesh as its bounds");
    CHECK(over_floor->world_position().y > 0.1f && over_floor->world_position().y < 0.4f);
    if (physics_jolt_available()) CHECK(over_hole->world_position().y < -1.0f);
  });

  test("blockout: Play starts from the mesh the pieces make now, even with a build running on a worker", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.set_async_jobs_for_test(true);
    GameObject *bo = r477_blockout(ed);
    const std::vector<GameObject *> rooms = r477_rooms(ed, *bo, 2, 1);
    ed.step_frame_headless();
    rooms.back()->set_local_position(rooms.back()->local().position + Vec3(0.0f, 0.0f, 0.3f));
    ed.step_frame_headless();
    ed.command("play");
    CHECK(r477_matches(ed) && !ed.blockout_jobs_running_for_test());
    ed.command("stop");
  });

  /* ---- Criterion 5: export ---- */
  test("blockout: an OBJ and a GLB of the scene hold the Blockout's mesh and nothing for its pieces", [] {
    Editor ed;
    ed.init_headless(900, 600);
    r477_empty_scene(ed);
    ed.command("blockout new");
    ed.command("blockout add cylinder subtract 0.5 3");
    ed.command("blockout add sphere 0.4");
    r477_settle(ed);
    GameObject *bo = r477_find(ed);
    const Mesh *m = r477_mesh(bo);
    CHECK(m && m->face_count() > 6);
    const std::string dir = ed.resolve_asset_path_for_test("Assets/Exports");
    fs::make_dirs(dir);
    const std::string obj = fs::join(dir, "r477_blockout.obj"), glb = fs::join(dir, "r477_blockout.glb");
    fs::remove_file(obj), fs::remove_file(glb);
    ed.command("export obj all " + obj);
    ed.command("export glb all " + glb);
    r477_settle(ed, 5);
    for (int i = 0; i < 400 && (!fs::exists(obj) || !fs::exists(glb) || ed.file_export_running_for_test()); i++) ed.step_frame_headless();
    std::string text, bin;
    CHECK(fs::read_file(obj, text) && fs::read_file(glb, bin));
    size_t verts = 0, objects = 0;
    std::istringstream is(text);
    for (std::string line; std::getline(is, line);) {
      if (line.rfind("v ", 0) == 0) verts++;
      if (line.rfind("o ", 0) == 0 || line.rfind("g ", 0) == 0) objects++;
    }
    CHECK(m && verts == m->vert_count() && objects == 1);
    CHECK(text.find("Blockout") != std::string::npos);
    for (const char *piece : {"Box", "Subtract Cylinder", "Sphere"}) CHECK(text.find(std::string("o ") + piece) == std::string::npos && text.find(std::string("g ") + piece) == std::string::npos);
    CHECK(bin.find("Blockout") != std::string::npos);
    for (const char *piece : {"\"Box\"", "\"Subtract Cylinder\"", "\"Sphere\""}) CHECK(bin.find(piece) == std::string::npos);
  });

  test("blockout: an export asked for with a build still running waits for it and writes the mesh the pieces make now", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.set_async_jobs_for_test(true);
    r477_empty_scene(ed);
    GameObject *bo = r477_blockout(ed);
    const std::vector<GameObject *> rooms = r477_rooms(ed, *bo, 2, 1);
    ed.command("blockout wait");
    rooms.back()->set_local_position(rooms.back()->local().position + Vec3(0.0f, 0.0f, 0.4f));
    const std::string dir = ed.resolve_asset_path_for_test("Assets/Exports");
    fs::make_dirs(dir);
    const std::string obj = fs::join(dir, "r477_wait.obj");
    fs::remove_file(obj);
    ed.command("export obj all " + obj);
    for (int i = 0; i < 2000 && (!fs::exists(obj) || ed.file_export_running_for_test()); i++) ed.step_frame_headless();
    r477_settle(ed, 3);
    std::string text;
    CHECK(fs::read_file(obj, text));
    Mesh ref;
    CHECK(r477_scratch(*bo, ref));
    size_t verts = 0;
    std::istringstream is(text);
    for (std::string line; std::getline(is, line);) verts += line.rfind("v ", 0) == 0;
    CHECK(verts == ref.vert_count());
  });

  /* ---- Criterion 6: clicking the result selects the piece ---- */
  test("blockout: a click on a face a Subtract cylinder cut selects the cylinder, on the box's outer face the box, Shift adds the other; a click after the pieces are gone selects the Blockout", [] {
    Editor ed;
    ed.init_headless(1600, 1000);
    r477_empty_scene(ed);
    ed.command("camera 25 60 9 0 0 0");
    GameObject *bo = r477_blockout(ed);
    bo->set_world_position({0.0f, 0.0f, 0.0f});
    GameObject *wall = r477_piece(ed, *bo, "Block", 0, 0, {0, 0, 0}, {4, 2, 4});
    GameObject *cut = r477_piece(ed, *bo, "Pocket", 1, 1, {0.0f, 1.0f, 0.0f}, Vec3(1.0f), 1.0f, 1.0f);
    r34_steps(ed, 4);
    CHECK(r477_matches(ed) && r477_mesh(bo)->face_count() > 6);
    int px = 0, py = 0;
    CHECK(ed.project_to_window({0.0f, 0.5f, 0.0f}, px, py));  // (the pocket's floor, the cylinder's bottom)
    ed.clear_selection_for_test();
    r477_click(ed, px, py);
    r34_steps(ed, 2);
    CHECK(ed.selection_size_for_test() == 1 && ed.active_object_for_test() == cut);
    int tx = 0, ty = 0;
    CHECK(ed.project_to_window({1.7f, 1.0f, 1.7f}, tx, ty));  // (the box's top, outside the pocket)
    r477_click(ed, tx, ty);
    r34_steps(ed, 2);
    CHECK(ed.selection_size_for_test() == 1 && ed.active_object_for_test() == wall);
    ed.clear_selection_for_test();  // (the box's gizmo sits on the pocket: nothing selected, then the pocket, then Shift on the top - away from the cylinder's gizmo)
    r477_click(ed, px, py);
    r34_steps(ed, 2);
    CHECK(ed.selection_size_for_test() == 1 && ed.active_object_for_test() == cut);
    r477_click(ed, tx, ty, platform::MOD_SHIFT);
    r34_steps(ed, 2);
    const std::vector<GameObject *> sel = ed.selected_objects_for_test();
    CHECK(sel.size() == 2 && std::find(sel.begin(), sel.end(), wall) != sel.end() && std::find(sel.begin(), sel.end(), cut) != sel.end());
    /* The pieces gone, the build not yet landed (held): the mesh on screen belongs to no piece. */
    ed.set_async_jobs_for_test(true);
    ed.hold_blockout_landing_for_test(true);
    ed.scene().destroy(std::vector<GameObject *>{wall, cut});
    ed.clear_selection_for_test();
    r477_click(ed, tx, ty);
    r34_steps(ed, 2);
    CHECK(ed.selection_size_for_test() == 1 && ed.active_object_for_test() == bo);
    ed.hold_blockout_landing_for_test(false);
  });

  /* ---- Criterion 7: Edit Mode is refused ---- */
  test("blockout: Tab and 'edit' on a Blockout or one of its pieces stay in Object mode with the hint, the mesh untouched; a parametric shape still enters Edit Mode", [] {
    Editor ed;
    ed.init_headless(1000, 700);
    ed.command("blockout new");
    r477_settle(ed);
    GameObject *bo = r477_find(ed);
    const Mesh *mesh = r477_mesh(bo);
    const uint64_t version = mesh ? mesh->version : 0;
    const Recti r = ed.scene_view_rect();
    auto press_tab = [&] {
      ed.step_frame_headless({r477_ev(R477ET::MouseMove, r.x + r.w / 2, r.y + r.h / 2)});
      ed.step_frame_headless({r477_key(platform::KEY_TAB)});
      ed.step_frame_headless({r477_key(platform::KEY_TAB, false)});
      ed.step_frame_headless();
    };
    ed.clear_toasts_for_test();
    press_tab();  // (the piece is selected)
    CHECK(!ed.in_edit_mode());
    bool hint = false;
    for (const Editor::Toast &t : ed.toasts_for_test()) hint = hint || t.text.find("rebuilt from its pieces") != std::string::npos;
    CHECK(hint);
    ed.select_for_test(bo->id, false);
    const size_t mark = r477_mark();
    press_tab();
    CHECK(!ed.in_edit_mode());
    ed.command("edit");
    CHECK(!ed.in_edit_mode());
    ed.command("edit face");
    CHECK(!ed.in_edit_mode());
    const std::string said = r477_log_since(mark);
    CHECK(said.find("A Blockout's mesh is rebuilt from its pieces: move or resize the pieces.") != std::string::npos);
    CHECK(r477_mesh(bo) == mesh && mesh && mesh->version == version);
    ed.command("sculpt on");
    CHECK(!ed.sculpt_mode_for_test());
    ed.command("weightpaint on");
    CHECK(!ed.weight_paint_active_for_test());
    /* A parametric shape is still editable. */
    ed.command("create Shape: Box");
    r477_settle(ed);
    press_tab();
    CHECK(ed.in_edit_mode());
    press_tab();
    CHECK(!ed.in_edit_mode());
  });

  test("blockout: Apply Modifiers does not turn a Blockout with a modifier into an ordinary mesh", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.command("blockout new");
    r477_settle(ed);
    GameObject *bo = r477_find(ed);
    ed.select_for_test(bo->id, false);
    ed.command("component MirrorModifier");
    const Mesh *mesh = r477_mesh(bo);
    ed.command("applymods");
    r477_settle(ed);
    CHECK(r477_find(ed) && r477_find(ed)->get<Blockout>() && r477_find(ed)->get<MirrorModifier>() && r477_mesh(r477_find(ed)) == mesh);
  });

  /* ---- Criterion 8: robust ---- */
  test("blockout: a Blockout with no piece has an empty mesh and draws without a warning each frame", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.command("blockout new");
    r477_settle(ed);
    GameObject *bo = r477_find(ed);
    ed.scene().destroy(r477_find(ed, "Box"));
    ed.mark_changed_for_test("delete");
    ed.select_for_test(bo->id, false);
    const size_t mark = r477_mark();
    r477_settle(ed, 10);
    size_t warnings = 99;
    r477_log_since(mark, &warnings);
    const Mesh *m = r477_mesh(bo);
    CHECK(m && m->face_count() == 0 && m->positions.empty());
    CHECK(warnings == 0);
    CHECK(r477_matches(ed));
    ed.command("blockout info");
    CHECK(r477_log_since(mark).find("No pieces") != std::string::npos);
  });

  test("blockout: a piece with a NaN size or a zero scale is skipped and named in 'blockout info' and the Inspector; the rest builds", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.record_buttons_for_test(true);
    ed.command("blockout new");
    GameObject *bo = r477_find(ed);
    r477_piece(ed, *bo, "Nan Piece", 0, 0, {3, 0, 0}, {kNaN, 1, 1});
    GameObject *flat = r477_piece(ed, *bo, "Flat Piece", 0, 0, {-3, 0, 0}, {1, 1, 1});
    flat->set_local_scale({0.0f, 1.0f, 1.0f});
    ed.select_for_test(bo->id, false);
    r477_settle(ed, 3);
    const Mesh *m = r477_mesh(bo);
    CHECK(m && m->face_count() == 6 && r477_matches(ed));  // (the 2 m box alone)
    const size_t mark = r477_mark();
    ed.command("blockout info");
    const std::string said = r477_log_since(mark);
    CHECK(said.find("Skipped 'Nan Piece'") != std::string::npos && said.find("Skipped 'Flat Piece'") != std::string::npos);
    CHECK(said.find("1 of 3 pieces used") != std::string::npos);
    bool nan_line = false, flat_line = false;
    for (const auto &kv : ed.ui_for_test().button_rects) {
      nan_line = nan_line || kv.first.rfind("shown:Skipped 'Nan Piece'", 0) == 0;
      flat_line = flat_line || kv.first.rfind("shown:Skipped 'Flat Piece'", 0) == 0;
    }
    CHECK(nan_line && flat_line);
  });

  test("blockout: Method Exact in a build without Blender's libraries keeps the last mesh and says there is no Manifold, once", [] {
    if (blockout::crisp_available()) return;  // (the libraries' build runs the exact path; there is nothing to refuse)
    Editor ed;
    ed.init_headless(1200, 800);
    ed.record_buttons_for_test(true);
    ed.command("blockout new");
    GameObject *bo = r477_find(ed);
    const Mesh *kept = r477_mesh(bo);
    CHECK(kept && kept->face_count() == 6);
    ed.select_for_test(bo->id, false);
    const size_t mark = r477_mark();
    ed.command("blockout set method exact");
    r477_settle(ed, 12);
    CHECK(r477_mesh(bo) == kept);
    const std::string said = r477_log_since(mark);
    size_t first = said.find("Manifold"), count = 0;
    for (size_t at = first; at != std::string::npos; at = said.find("Manifold", at + 1)) count++;
    CHECK(first != std::string::npos && count == 1);
    bool shown = false;
    for (const auto &kv : ed.ui_for_test().button_rects) shown = shown || (kv.first.rfind("shown:Refused", 0) == 0 && kv.first.find("Manifold") != std::string::npos);
    CHECK(shown);
    ed.command("blockout set method auto");
    r477_settle(ed, 2);
    CHECK(r477_matches(ed));
  });

  test("blockout: 200 random edits (add, delete, move, undo, redo, re-parent, disable) with workers on end, after 'blockout wait', on the build from scratch every time", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.set_async_jobs_for_test(true);
    ed.command("blockout new");
    ed.command("blockout wait");
    GameObject *bo = r477_find(ed);
    ed.scene().create("Folder", bo);
    ed.mark_changed_for_test("setup");
    r477_settle(ed);
    const size_t base = ed.undo_depth_for_test();
    R477Rand rnd{477u};
    int bad = 0, checks = 0;
    const uint64_t main0 = ed.blockout_main_builds_for_test();
    for (int i = 0; i < 200; i++) {
      GameObject *b = r477_find(ed);
      if (!b) break;
      std::vector<GameObject *> ps = r477_all_pieces(*b);
      GameObject *folder = r477_find(ed, "Folder");
      const int op = rnd.pick(7);
      bool did = false;
      if (op == 0) {
        GameObject *at = folder && rnd.pick(3) == 0 ? folder : b;
        const int shape = rnd.pick(3), sub = rnd.pick(4) == 0 ? 1 : 0;
        const Vec3 pos(rnd.in(-1.5f, 1.5f), rnd.in(-1, 1), rnd.in(-1.5f, 1.5f)), size(rnd.in(0.5f, 1.5f), rnd.in(0.5f, 1.5f), rnd.in(0.5f, 1.5f));
        r477_piece(ed, *at, "P", shape, sub, pos, size, rnd.in(0.3f, 0.8f), rnd.in(0.5f, 1.5f));
        did = true;
      }
      else if (op == 1 && !ps.empty()) {
        ed.scene().destroy(ps[(size_t)rnd.pick((int)ps.size())]);
        did = true;
      }
      else if (op == 2 && !ps.empty()) {
        ps[(size_t)rnd.pick((int)ps.size())]->set_local_position({rnd.in(-1.5f, 1.5f), rnd.in(-1, 1), rnd.in(-1.5f, 1.5f)});
        did = true;
      }
      else if (op == 3 && ed.undo_depth_for_test() > base) {
        ed.command("undo");
        did = true;
      }
      else if (op == 4) {
        ed.command("redo");
        did = true;
      }
      else if (op == 5 && !ps.empty() && folder) {
        GameObject *p = ps[(size_t)rnd.pick((int)ps.size())];
        ed.scene().set_parent(p, p->parent == b ? folder : b, -1, false);
        did = true;
      }
      else if (op == 6 && !ps.empty()) {
        GameObject *p = ps[(size_t)rnd.pick((int)ps.size())];
        p->active = !p->active;
        did = true;
      }
      if (!did) continue;
      if (op != 3 && op != 4) ed.mark_changed_for_test("random edit");
      for (int f = rnd.pick(3); f > 0; f--) ed.step_frame_headless();
      ed.command("blockout wait");
      checks++;
      if (!r477_matches(ed)) bad++;
    }
    std::printf("    %d checks, %d differ from a build from scratch, builds on the main thread %llu\n", checks, bad,
                (unsigned long long)(ed.blockout_main_builds_for_test() - main0));
    CHECK(checks >= 150 && bad == 0);
    CHECK(ed.blockout_main_builds_for_test() == main0);
  });

  /* ---- The Inspector, the console, the menus, the tray ---- */
  test("blockout: the Inspector shows a Blockout's Field Cell and Method, a piece's fields for its shape, the status line, and no Blend Radius", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.record_buttons_for_test(true);
    ed.command("blockout new");
    GameObject *bo = r477_find(ed);
    ed.select_for_test(bo->id, false);
    r477_settle(ed, 3);
    auto has = [&](const std::string &key) {
      for (const auto &kv : ed.ui_for_test().button_rects)
        if (kv.first.rfind(key, 0) == 0) return true;
      return false;
    };
    CHECK(has("field:Field Cell"));
    CHECK(has("shown:1 of 1 pieces used, 6 faces"));
    /* (Only numbers record a key: a piece's Shape, Operation and Size are not among them.) */
    ed.select_for_test(r477_find(ed, "Box")->id, false);
    ed.record_buttons_for_test(true);
    r477_settle(ed, 3);
    CHECK(!has("field:Radius") && !has("field:Height") && !has("field:Segments") && !has("field:Rings") && !has("field:Blend Radius"));
    r477_find(ed, "Box")->get<BlockoutPiece>()->shape = 2;
    ed.record_buttons_for_test(true);
    r477_settle(ed, 3);
    CHECK(has("field:Radius") && has("field:Segments") && has("field:Rings") && !has("field:Height") && !has("field:Blend Radius"));
    r477_find(ed, "Box")->get<BlockoutPiece>()->shape = 1;
    ed.record_buttons_for_test(true);
    r477_settle(ed, 3);
    CHECK(has("field:Radius") && has("field:Height") && has("field:Segments") && !has("field:Rings") && !has("field:Blend Radius"));
  });

  test("blockout: 'set BlockoutPiece.Radius' and a clone carry a piece's fields like any component's", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.command("blockout new");
    ed.command("blockout add cylinder 0.5 2");
    ed.command("set BlockoutPiece.Radius 0.8");
    ed.command("set BlockoutPiece.Segments 12");
    GameObject *cyl = ed.active_object_for_test();
    CHECK(cyl && cyl->get<BlockoutPiece>() && cyl->get<BlockoutPiece>()->radius == 0.8f && cyl->get<BlockoutPiece>()->segments == 12);
    r477_settle(ed);
    CHECK(r477_matches(ed));
    if (!cyl || !cyl->get<BlockoutPiece>()) return;
    auto copy = cyl->get<BlockoutPiece>()->clone();
    CHECK(static_cast<BlockoutPiece *>(copy.get())->radius == 0.8f && static_cast<BlockoutPiece *>(copy.get())->shape == 1);
    ed.select_for_test(r477_find(ed)->id, false);
    ed.command("set Blockout.FieldCell 0.2");
    GameObject *bo = r477_find(ed);
    CHECK(bo && bo->get<Blockout>()->cell == 0.2f);
    r477_settle(ed);
    CHECK(r477_matches(ed));
  });

  test("blockout: 'blockout set cell' and 'blockout set method' change the selected Blockout and rebuild it", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.command("blockout new");
    ed.command("blockout add sphere 1");
    GameObject *bo = r477_find(ed);
    const uint64_t builds = ed.blockout_main_builds_for_test();
    ed.command("blockout set cell 0.25");
    CHECK(bo->get<Blockout>()->cell == 0.25f && ed.blockout_main_builds_for_test() > builds);
    CHECK(r477_matches(ed));
    ed.command("blockout set method field");
    CHECK(bo->get<Blockout>()->method == 2 && r477_matches(ed));
    ed.command("blockout set method auto");
    CHECK(bo->get<Blockout>()->method == 0);
    const size_t mark = r477_mark();
    ed.command("blockout set colour 1");
    CHECK(r477_log_since(mark).find("blockout set cell") != std::string::npos);
  });

  test("blockout: 'blockout rebuild' forgets what was built and the cache and builds again to the same mesh; 'blockout info' prints the status; 'help blockout' lists the verbs", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.command("blockout new");
    ed.command("blockout add cylinder subtract 0.5 3");
    GameObject *bo = r477_find(ed);
    const Mesh before = *r477_mesh(bo);
    const uint64_t builds = ed.blockout_main_builds_for_test();
    ed.command("blockout rebuild");
    CHECK(ed.blockout_main_builds_for_test() == builds + 1);
    CHECK(r477_same(*r477_mesh(bo), before) && !bo->get<Blockout>()->state.status.path.empty());
    const size_t mark = r477_mark();
    ed.command("blockout info");
    std::string said = r477_log_since(mark);
    CHECK(said.find("Blockout 'Blockout'") != std::string::npos && said.find("2 of 2 pieces used") != std::string::npos && said.find(" faces, ") != std::string::npos);
    const size_t mark2 = r477_mark();
    ed.command("help blockout");
    said = r477_log_since(mark2);
    for (const char *verb : {"blockout new", "blockout add", "blockout set", "blockout rebuild", "blockout wait", "blockout info"}) CHECK(said.find(verb) != std::string::npos);
  });

  test("blockout: 'blockout wait' joins the running builds so the next command sees the mesh", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.set_async_jobs_for_test(true);
    ed.command("blockout new");
    ed.command("blockout add sphere 1.4");
    ed.command("blockout wait");
    CHECK(!ed.blockout_jobs_running_for_test() && r477_matches(ed) && r477_mesh(r477_find(ed))->face_count() > 6);
    CHECK(ed.blockout_main_builds_for_test() == 0 && ed.blockout_landed_for_test() >= 1);
  });

  test("blockout: GameObject > Blockout offers a new Blockout and an Add or Subtract Box, Cylinder or Sphere, each one undo step", [] {
    Editor ed;
    ed.init_headless(1500, 1000);
    ed.record_buttons_for_test(true);
    r477_settle(ed, 3);
    CHECK(r477_menu(ed, {"menubar:GameObject", "submenu:Blockout", "menu:New Blockout"}));
    CHECK(r477_count(ed, false) == 1 && r477_count(ed, true) == 1);
    r477_settle(ed);
    CHECK(ed.undo_label_for_test(0).find("Blockout") != std::string::npos);
    struct Row {
      const char *label;
      int shape, op;
    };
    for (const Row &row : {Row{"Add Box", 0, 0}, Row{"Add Cylinder", 1, 0}, Row{"Add Sphere", 2, 0}, Row{"Subtract Box", 0, 1}, Row{"Subtract Cylinder", 1, 1}, Row{"Subtract Sphere", 2, 1}}) {
      const size_t pieces = r477_count(ed, true), depth = ed.undo_depth_for_test();
      CHECK(r477_menu(ed, {"menubar:GameObject", "submenu:Blockout", std::string("menu:") + row.label}));
      r477_settle(ed);
      const BlockoutPiece *p = r477_active_piece(ed);
      const bool ok = p && p->shape == row.shape && p->operation == row.op && r477_count(ed, true) == pieces + 1 && r477_count(ed, false) == 1 && ed.undo_depth_for_test() == depth + 1;
      if (!ok) std::printf("    GameObject > Blockout > %s: not as asked\n", row.label);
      CHECK(ok);
    }
    CHECK(r477_matches(ed));
    ed.command("undo");
    r477_settle(ed);
    CHECK(r477_count(ed, true) == 6);
  });

  test("blockout: the Hierarchy's + menu offers the same Blockout entries, and Add Cylinder with nothing selected builds a Blockout with a cylinder", [] {
    Editor ed;
    ed.init_headless(1500, 1000);
    ed.record_buttons_for_test(true);
    ed.clear_selection_for_test();
    r477_settle(ed, 3);
    const Recti h = ed.window_rect_for_test(WindowKind::Hierarchy);
    const ui::Context &u = ed.ui_for_test();
    const int bh = u.row_h() + u.px(6), hh = bh - u.px(8);
    const int cx = h.x + u.px(5) + (hh + u.px(16)) / 2, cy = h.y + u.px(4) + hh / 2;
    r477_click(ed, cx, cy);
    r477_settle(ed);
    CHECK(r477_menu(ed, {"submenu:Blockout", "menu:Add Cylinder"}));
    r477_settle(ed);
    const BlockoutPiece *p = r477_active_piece(ed);
    CHECK(p && p->shape == 1 && r477_count(ed, false) == 1 && r477_count(ed, true) == 1);
    r477_click(ed, cx, cy);
    r477_settle(ed);
    CHECK(r477_menu(ed, {"submenu:Blockout", "menu:Subtract Sphere"}));
    r477_settle(ed);
    p = r477_active_piece(ed);
    CHECK(p && p->shape == 2 && p->operation == 1 && r477_count(ed, true) == 2 && r477_count(ed, false) == 1);
  });

  test("blockout: the Modeling Tools window has New Blockout and Add / Subtract buttons that make them", [] {
    Editor ed;
    ed.init_headless(1500, 1000);
    ed.command("layout Default");
    ed.command("window Modeling Tools");
    ed.command("alltools on");
    ed.record_buttons_for_test(true);
    r477_settle(ed, 4);
    Recti r;
    CHECK(ed.button_rect_for_test("New Blockout", r));
    if (!ed.button_rect_for_test("New Blockout", r)) return;
    r477_click(ed, r.x + r.w / 2, r.y + r.h / 2);
    r477_settle(ed);
    CHECK(r477_count(ed, false) == 1 && r477_count(ed, true) == 1);
    for (const char *label : {"Add Box", "Subtract Cylinder"}) {
      ed.record_buttons_for_test(true);
      r477_settle(ed, 2);
      CHECK(ed.button_rect_for_test(label, r));
      if (!ed.button_rect_for_test(label, r)) continue;
      r477_click(ed, r.x + r.w / 2, r.y + r.h / 2);
      r477_settle(ed);
    }
    CHECK(r477_count(ed, true) == 3 && r477_count(ed, false) == 1);
    const BlockoutPiece *p = r477_active_piece(ed);
    CHECK(p && p->shape == 1 && p->operation == 1);
  });

  test("blockout: a build on a worker is in the job tray as 'Building <name>' while it runs", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.set_async_jobs_for_test(true);
    GameObject *bo = r477_blockout(ed, "Hall");
    r477_rooms(ed, *bo, 3, 4);
    ed.hold_blockout_landing_for_test(true);
    ed.step_frame_headless();
    bool listed = false;
    for (int i = 0; i < 5; i++) {
      ed.step_frame_headless();
      for (const Editor::JobView &j : ed.jobs_for_test()) listed = listed || j.label == "Building Hall";
    }
    CHECK(listed);
    ed.hold_blockout_landing_for_test(false);
    ed.command("blockout wait");
    bool still = false;
    for (const Editor::JobView &j : ed.jobs_for_test()) still = still || j.label == "Building Hall";
    CHECK(!still && r477_matches(ed, "Hall"));
  });

  test("blockout: a selected Add piece shows as a wire in the selection colour and a selected Subtract piece in the warning colour; nothing is drawn for pieces nothing selects", [] {
    Editor ed;
    r34_open(ed);
    r477_empty_scene(ed);
    GameObject *bo = r477_blockout(ed);
    bo->set_world_position({0.0f, 0.0f, 0.0f});
    GameObject *wall = r477_piece(ed, *bo, "Wall", 0, 0, {0, 0, 0}, {2, 2, 2});
    GameObject *cut = r477_piece(ed, *bo, "Cut", 1, 1, {0, 0, 0}, Vec3(1.0f), 0.4f, 3.0f);
    ed.command("camera 25 25 8 0 0 0");
    ed.clear_selection_for_test();
    r34_steps(ed, 4);
    auto orange_yellow = [&](GameObject *sel) {
      if (sel) ed.select_for_test(sel->id, false);
      else ed.clear_selection_for_test();
      r34_steps(ed, 4);
      const std::vector<uint32_t> px = r34_crop(ed, ed.scene_view_rect());
      return std::make_pair(r477_near(px, 0xF7941D, 14), r477_near(px, 0xF4BC02, 14));
    };
    const auto none = orange_yellow(nullptr), box_sel = orange_yellow(wall), cut_sel = orange_yellow(cut), all_sel = orange_yellow(bo);
    std::printf("    wire pixels (orange, yellow): nothing %zu/%zu, the Add box %zu/%zu, the Subtract cylinder %zu/%zu, the Blockout %zu/%zu\n", none.first, none.second,
                box_sel.first, box_sel.second, cut_sel.first, cut_sel.second, all_sel.first, all_sel.second);
    CHECK(none.second == 0);
    CHECK(box_sel.first > none.first + 20 && box_sel.second == 0);
    CHECK(cut_sel.second > 20 && cut_sel.first <= none.first);
    CHECK(all_sel.second > 0);
  });

  test("blockout: the Learn tab has the lesson 'Blockout: pieces that build one mesh' with a Try-it of commands that run and two questions", [] {
    int found = -1;
    for (int i = 0; i < lesson_count(); i++)
      if (std::string(lesson(i).title) == "Blockout: pieces that build one mesh") found = i;
    CHECK(found >= 0);
    if (found < 0) return;
    const std::string body = lesson(found).body;
    size_t questions = 0, cmds = 0;
    std::istringstream is(body);
    Editor ed;
    ed.init_headless(900, 600);
    for (std::string line; std::getline(is, line);) {
      if (line.rfind("? ", 0) == 0) questions++;
      if (line.rfind("! cmd:", 0) != 0) continue;
      cmds++;
      ed.command(line.substr(6, line.find(" | ") - 6));
    }
    CHECK(questions == 2 && cmds >= 3);
    CHECK(r477_find(ed) && r477_count(ed, true) == 2 && r477_matches(ed));
  });
}

/* ===================================================================== */
/* Round 477, the test engineer's: lifetime of the workers, undo with held builds, duplicates, picks,   */
/* hostile numbers, refusals, Play, export, the tray (written from the acceptance criteria and ADR 0129) */
/* ===================================================================== */

namespace {

platform::Event te_key(int key, int mods, bool down = true) {
  platform::Event e;
  e.type = down ? R477ET::KeyDown : R477ET::KeyUp, e.key = key, e.mods = mods;
  return e;
}

/* A key with the mouse over the Scene view, as a user presses it. */
void te_press(Editor &ed, int key, int mods = 0) {
  const Recti r = ed.scene_view_rect();
  ed.step_frame_headless({r477_ev(R477ET::MouseMove, r.x + r.w / 2, r.y + r.h / 2)});
  ed.step_frame_headless({te_key(key, mods)});
  ed.step_frame_headless({te_key(key, mods, false)});
  ed.step_frame_headless();
}

bool te_matches(GameObject *g) {
  const Mesh *m = r477_mesh(g);
  Mesh ref;
  return g && m && r477_scratch(*g, ref) && r477_same(ref, *m);
}

std::vector<GameObject *> te_blockouts(Editor &ed) {
  std::vector<GameObject *> out;
  ed.scene().for_each([&](GameObject &g) {
    if (g.get<Blockout>()) out.push_back(&g);
  });
  return out;
}

std::vector<uint64_t> te_piece_ids(GameObject &bo) {
  std::vector<uint64_t> v;
  for (GameObject *p : r477_all_pieces(bo)) v.push_back(p->id);
  return v;
}

bool te_has(const std::vector<uint64_t> &v, uint64_t id) { return std::find(v.begin(), v.end(), id) != v.end(); }

std::string te_scene_path(Editor &ed, const char *file) {
  const std::string dir = ed.resolve_asset_path_for_test("Assets/Scenes");
  fs::make_dirs(dir);
  return fs::join(dir, file);
}

/* A window sized for picking, an empty scene and the camera (yaw pitch distance pivot). */
void te_view(Editor &ed, const char *camera) {
  ed.init_headless(1600, 1000);
  r477_empty_scene(ed);
  ed.command(camera);
}

/* A click on a world point with nothing selected (so no gizmo is in the way); false when the point is off the view. */
bool te_click_world(Editor &ed, Vec3 w, int mods = 0) {
  int x = 0, y = 0;
  if (!ed.project_to_window(w, x, y)) return false;
  ed.clear_selection_for_test();
  r477_click(ed, x, y, mods);
  r34_steps(ed, 2);
  return true;
}

/* The pocket scene: a 4 x 2 x 4 block with a Subtract cylinder (radius 1, 1 m tall) centred on its top, so the pocket's
 * floor is the plane y = 0.5. */
GameObject *te_pocket_blockout(Editor &ed, const char *name, GameObject **wall, GameObject **cut) {
  GameObject *bo = r477_blockout(ed, name);
  GameObject *w = r477_piece(ed, *bo, "Block", 0, 0, {0, 0, 0}, {4, 2, 4});
  GameObject *c = r477_piece(ed, *bo, "Pocket", 1, 1, {0, 1, 0}, Vec3(1.0f), 1.0f, 1.0f);
  if (wall) *wall = w;
  if (cut) *cut = c;
  return bo;
}

/* A Blockout whose id is `id` (objects made and destroyed to pad the scene's counter), so two scenes can hold the same one. */
GameObject *te_blockout_at_id(Editor &ed, uint64_t id, const char *name = "Blockout") {
  std::vector<GameObject *> pads;
  while (ed.scene().next_id() < id) pads.push_back(ed.scene().create("pad"));
  GameObject *bo = r477_blockout(ed, name);
  ed.scene().destroy(pads);
  return bo;
}

GameObject *te_find_piece(GameObject &bo, int shape) {
  for (GameObject *p : r477_all_pieces(bo))
    if (p->get<BlockoutPiece>()->shape == shape) return p;
  return nullptr;
}

size_t te_warnings_since(size_t mark) {
  size_t w = 0;
  r477_log_since(mark, &w);
  return w;
}

}  // namespace

static void tests_round477_te() {
  /* ---- The worker's lifetime ---- */
  test("blockout: a New Scene made while a build runs never shows the old scene's mesh on the new scene's Blockout of the same id", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.set_async_jobs_for_test(true);
    r477_empty_scene(ed);
    GameObject *bo = te_blockout_at_id(ed, 500);
    const uint64_t id = bo->id;
    const std::vector<GameObject *> rooms = r477_rooms(ed, *bo, 3, 4);
    ed.command("blockout wait");
    const Mesh old = *r477_mesh(bo);
    CHECK(old.face_count() > 24);
    ed.hold_blockout_landing_for_test(true);
    rooms.back()->set_local_position(rooms.back()->local().position + Vec3(0.0f, 0.0f, 0.3f));
    ed.mark_changed_for_test("move");
    Mesh moved;  // (what the running build makes)
    CHECK(r477_scratch(*bo, moved) && !r477_same(moved, old));
    r477_settle(ed, 3);
    CHECK(ed.blockout_jobs_running_for_test());
    ed.command("newscene");
    r477_empty_scene(ed);
    GameObject *nb = te_blockout_at_id(ed, 500);
    r477_piece(ed, *nb, "Only", 0, 0, {0, 0, 0}, {2, 2, 2});
    CHECK(nb->id == id);  // (the same id as the old scene's Blockout: the case that matters)
    ed.hold_blockout_landing_for_test(false);
    bool old_shown = false;
    for (int i = 0; (i < 8 || ed.blockout_jobs_running_for_test()) && i < 300; i++) {
      ed.step_frame_headless();
      const Mesh *m = r477_mesh(r477_find(ed));
      if (m && (r477_same(*m, old) || r477_same(*m, moved))) old_shown = true;
    }
    if (old_shown) std::printf("    the new scene's Blockout showed the old scene's mesh (%zu faces)\n", old.face_count());
    CHECK(!old_shown);
    ed.command("blockout wait");
    CHECK(r477_matches(ed) && !ed.blockout_jobs_running_for_test());
    CHECK(r477_mesh(r477_find(ed)) && r477_mesh(r477_find(ed))->face_count() == 6);
  });

  test("blockout: Open Scene while a build runs never shows the previous scene's mesh on the opened scene's Blockout", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.set_async_jobs_for_test(true);
    r477_empty_scene(ed);
    GameObject *bo = r477_blockout(ed);
    r477_piece(ed, *bo, "Hall", 0, 0, {0, 0, 0}, {2, 2, 2});
    ed.command("blockout wait");
    r477_settle(ed, 2);
    const std::string path = te_scene_path(ed, "r477te_open.scene");
    ed.command("save " + path);
    for (int i = 0; i < 400 && (!fs::exists(path) || ed.save_job_running_for_test()); i++) ed.step_frame_headless();  // (the file is there before the save job has written all of it)
    r477_settle(ed, 3);
    const std::vector<GameObject *> rooms = r477_rooms(ed, *bo, 3, 4);
    ed.command("blockout wait");
    const Mesh big = *r477_mesh(bo);
    ed.hold_blockout_landing_for_test(true);
    rooms.back()->set_local_position(rooms.back()->local().position + Vec3(0.0f, 0.0f, 0.3f));
    ed.mark_changed_for_test("move");
    Mesh moved;  // (what the running build makes)
    CHECK(r477_scratch(*bo, moved) && !r477_same(moved, big));
    r477_settle(ed, 3);
    CHECK(ed.blockout_jobs_running_for_test());
    const uint64_t old_id = bo->id;
    CHECK(ed.open_scene_for_test(path));
    for (int i = 0; i < 400 && r477_count(ed, true) != 1; i++) ed.step_frame_headless();
    CHECK(r477_count(ed, true) == 1);
    CHECK(r477_find(ed) && r477_find(ed)->id == old_id);  // (the scene file keeps ids: the same-id case)
    ed.hold_blockout_landing_for_test(false);
    bool old_shown = false;
    for (int i = 0; i < 8 || ed.blockout_jobs_running_for_test(); i++) {
      ed.step_frame_headless();
      const Mesh *m = r477_mesh(r477_find(ed));
      if (m && (r477_same(*m, big) || r477_same(*m, moved))) old_shown = true;
      if (i > 300) break;  // (a build takes some frames: the loop watches until it is done)
    }
    if (old_shown) std::printf("    the opened scene's Blockout showed the previous scene's mesh (%zu faces)\n", big.face_count());
    CHECK(!old_shown);
    ed.command("blockout wait");
    CHECK(r477_matches(ed) && r477_mesh(r477_find(ed))->face_count() == 6);
  });

  test("blockout: deleting a Blockout while its build runs, then undoing the delete, ends with no job and the build from scratch", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.set_async_jobs_for_test(true);
    r477_empty_scene(ed);
    GameObject *bo = r477_blockout(ed);
    r477_rooms(ed, *bo, 3, 4);
    ed.command("blockout wait");
    ed.mark_changed_for_test("built");
    r477_settle(ed);
    ed.hold_blockout_landing_for_test(true);
    GameObject *door = r477_find(ed, "Door");
    door->set_local_position(door->local().position + Vec3(0.0f, 0.0f, 0.3f));
    ed.mark_changed_for_test("moved");
    r477_settle(ed, 3);
    CHECK(ed.blockout_jobs_running_for_test());
    ed.scene().destroy(r477_find(ed));
    ed.mark_changed_for_test("deleted");
    r477_settle(ed, 2);
    ed.hold_blockout_landing_for_test(false);
    r477_settle(ed, 4);
    ed.command("blockout wait");
    CHECK(!ed.blockout_jobs_running_for_test() && r477_find(ed) == nullptr);
    ed.command("undo");
    r477_settle(ed, 2);
    CHECK(r477_find(ed) != nullptr);
    ed.command("blockout wait");
    CHECK(r477_matches(ed) && !ed.blockout_jobs_running_for_test());
    ed.command("undo");
    r477_settle(ed, 2);
    ed.command("blockout wait");
    CHECK(r477_matches(ed) && !ed.blockout_jobs_running_for_test());
    ed.command("redo");
    ed.command("redo");
    r477_settle(ed, 2);
    ed.command("blockout wait");
    CHECK(r477_find(ed) == nullptr && !ed.blockout_jobs_running_for_test());
  });

  test("blockout: an editor closed with a build running and one waiting joins them and exits cleanly", [] {
    bool reached = false;
    {
      Editor ed;
      ed.init_headless(900, 600);
      ed.set_async_jobs_for_test(true);
      GameObject *bo = r477_blockout(ed);
      const std::vector<GameObject *> rooms = r477_rooms(ed, *bo, 3, 4);
      r477_settle(ed, 2);
      rooms.back()->set_local_position(rooms.back()->local().position + Vec3(0.0f, 0.0f, 0.3f));
      ed.hold_blockout_landing_for_test(true);
      r477_settle(ed, 2);
      CHECK(ed.blockout_jobs_running_for_test());
      reached = true;
    }
    CHECK(reached);
  });

  test("blockout: an undo back to the mesh already shown cancels the build running for the state undone: nothing lands and the mesh stays", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.set_async_jobs_for_test(true);
    r477_empty_scene(ed);
    GameObject *bo = r477_blockout(ed);
    r477_rooms(ed, *bo, 3, 4);
    ed.command("blockout wait");
    ed.mark_changed_for_test("base");
    r477_settle(ed, 2);
    const Mesh base = *r477_mesh(r477_find(ed));
    const uint64_t landed0 = ed.blockout_landed_for_test();
    ed.hold_blockout_landing_for_test(true);
    GameObject *door = r477_find(ed, "Door");
    door->set_local_position(door->local().position + Vec3(0.0f, 0.0f, 0.3f));
    ed.mark_changed_for_test("moved");
    r477_settle(ed, 3);
    CHECK(ed.blockout_jobs_running_for_test());
    ed.command("undo");
    r477_settle(ed, 2);
    ed.hold_blockout_landing_for_test(false);
    r477_settle(ed, 3);
    ed.command("blockout wait");
    const bool same = r477_same(*r477_mesh(r477_find(ed)), base);
    CHECK(same && r477_matches(ed) && !ed.blockout_jobs_running_for_test());
    if (ed.blockout_landed_for_test() != landed0) std::printf("    %llu build(s) landed after the undo (the pieces were back at the shown mesh)\n", (unsigned long long)(ed.blockout_landed_for_test() - landed0));
    CHECK(ed.blockout_landed_for_test() == landed0);
    CHECK(ed.blockout_main_builds_for_test() == 0);
  });

  test("blockout: undo, undo, redo, redo with builds held and then released lands each state's mesh and ends on the last", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.set_async_jobs_for_test(true);
    r477_empty_scene(ed);
    GameObject *bo = r477_blockout(ed);
    r477_rooms(ed, *bo, 2, 1);
    ed.command("blockout wait");
    ed.mark_changed_for_test("base");
    r477_settle(ed, 2);
    const Mesh m_base = *r477_mesh(r477_find(ed));
    GameObject *door = r477_find(ed, "Door");
    door->set_local_position(door->local().position + Vec3(0.0f, 0.0f, 0.2f));
    ed.mark_changed_for_test("A");
    r477_settle(ed, 2);
    ed.command("blockout wait");
    const Mesh m_a = *r477_mesh(r477_find(ed));
    CHECK(!r477_same(m_a, m_base));
    ed.hold_blockout_landing_for_test(true);
    door = r477_find(ed, "Door");
    door->set_local_position(door->local().position + Vec3(0.0f, 0.0f, 0.4f));
    ed.mark_changed_for_test("B");
    r477_settle(ed, 3);
    for (const char *step : {"undo", "undo", "redo", "redo"}) {
      ed.command(step);
      r477_settle(ed, 2);
    }
    ed.hold_blockout_landing_for_test(false);
    ed.command("blockout wait");
    CHECK(r477_matches(ed) && !ed.blockout_jobs_running_for_test());
    ed.command("undo");
    ed.command("blockout wait");
    CHECK(r477_same(*r477_mesh(r477_find(ed)), m_a));
    ed.command("undo");
    ed.command("blockout wait");
    CHECK(r477_same(*r477_mesh(r477_find(ed)), m_base));
    ed.command("redo");
    ed.command("blockout wait");
    CHECK(r477_same(*r477_mesh(r477_find(ed)), m_a));
    ed.command("redo");
    ed.command("blockout wait");
    CHECK(r477_matches(ed));
  });

  test("blockout: 120 random edits, undos, redos and hold / release of the landing with workers on always end on the build from scratch", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.set_async_jobs_for_test(true);
    r477_empty_scene(ed);
    GameObject *bo = r477_blockout(ed);
    r477_rooms(ed, *bo, 2, 1);
    ed.command("blockout wait");
    ed.mark_changed_for_test("base");
    r477_settle(ed, 2);
    const size_t base = ed.undo_depth_for_test();
    R477Rand rnd{4770u};
    bool held = false;
    int checks = 0, bad = 0;
    for (int i = 0; i < 120; i++) {
      GameObject *b = r477_find(ed);
      if (!b) break;
      const std::vector<GameObject *> ps = r477_all_pieces(*b);
      const int op = rnd.pick(9);
      if (op <= 1 && !ps.empty()) {
        ps[(size_t)rnd.pick((int)ps.size())]->set_local_position({rnd.in(-6, 6), rnd.in(-1, 1), rnd.in(-6, 6)});
        ed.mark_changed_for_test("move");
      }
      else if (op == 2 && !ps.empty()) {
        ps[(size_t)rnd.pick((int)ps.size())]->get<BlockoutPiece>()->size.x = rnd.in(0.5f, 4.0f);
        ed.mark_changed_for_test("size");
      }
      else if (op == 3 && ed.undo_depth_for_test() > base) ed.command("undo");
      else if (op == 4) ed.command("redo");
      else if (op == 5) held = !held, ed.hold_blockout_landing_for_test(held);
      else if (op == 6)
        for (int f = rnd.pick(4); f > 0; f--) ed.step_frame_headless();
      else if (op >= 7) {
        ed.command("blockout wait");
        checks++;
        if (!te_matches(r477_find(ed))) bad++;
      }
      ed.step_frame_headless();
    }
    ed.hold_blockout_landing_for_test(false);
    ed.command("blockout wait");
    checks++;
    if (!te_matches(r477_find(ed))) bad++;
    std::printf("    %d checks, %d differ from a build from scratch, main-thread builds %llu\n", checks, bad, (unsigned long long)ed.blockout_main_builds_for_test());
    CHECK(checks >= 10 && bad == 0);
    CHECK(ed.blockout_main_builds_for_test() == 0 && !ed.blockout_jobs_running_for_test());
  });

  test("blockout: a piece moved from one Blockout to another while both build lands both meshes as builds from scratch", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.set_async_jobs_for_test(true);
    r477_empty_scene(ed);
    GameObject *a = r477_blockout(ed, "Alpha");
    r477_rooms(ed, *a, 3, 4);
    GameObject *b = r477_blockout(ed, "Beta");
    b->set_local_position({60.0f, 0.0f, 0.0f});
    r477_rooms(ed, *b, 2, 1);
    ed.command("blockout wait");
    ed.hold_blockout_landing_for_test(true);
    GameObject *door = r477_find(ed, "Door");
    ed.scene().set_parent(door, r477_find(ed, "Beta"), -1, true);
    ed.mark_changed_for_test("moved over");
    r477_settle(ed, 3);
    ed.hold_blockout_landing_for_test(false);
    GameObject *shell = r477_find(ed, "Shell");
    shell->set_local_position(shell->local().position + Vec3(0.0f, 0.2f, 0.0f));
    ed.mark_changed_for_test("shell");
    r477_settle(ed, 2);
    ed.command("blockout wait");
    CHECK(r477_matches(ed, "Alpha") && r477_matches(ed, "Beta") && !ed.blockout_jobs_running_for_test());
  });

  test("blockout: 'blockout rebuild' and 'blockout set cell' while a build runs end on the build from scratch with no job left", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.set_async_jobs_for_test(true);
    r477_empty_scene(ed);
    GameObject *bo = r477_blockout(ed);
    const std::vector<GameObject *> rooms = r477_rooms(ed, *bo, 3, 4);
    ed.command("blockout wait");
    ed.hold_blockout_landing_for_test(true);
    rooms.back()->set_local_position(rooms.back()->local().position + Vec3(0.0f, 0.0f, 0.3f));
    ed.mark_changed_for_test("move");
    r477_settle(ed, 2);
    ed.command("blockout rebuild");
    r477_settle(ed, 2);
    ed.command("blockout set cell 0.25");
    ed.command("blockout set method field");
    ed.hold_blockout_landing_for_test(false);
    ed.command("blockout wait");
    CHECK(r477_matches(ed) && !ed.blockout_jobs_running_for_test());
    CHECK(r477_find(ed)->get<Blockout>()->cell == 0.25f && r477_find(ed)->get<Blockout>()->method == 2);
    const uint64_t started = ed.blockout_started_for_test();
    ed.command("blockout rebuild");
    ed.command("blockout wait");
    CHECK(ed.blockout_started_for_test() == started + 1 && r477_matches(ed));
    CHECK(ed.blockout_main_builds_for_test() == 0);
  });

  /* ---- Duplicates ---- */
  test("blockout: Ctrl+D on a Blockout makes a clone with its own pieces, ids, cache and faces' origins, and editing the clone leaves the original's mesh alone", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    r477_empty_scene(ed);
    GameObject *wall = nullptr, *cut = nullptr;
    GameObject *bo = te_pocket_blockout(ed, "Blockout", &wall, &cut);
    r477_piece(ed, *bo, "Ball", 2, 0, {2.5f, 1.0f, 0.0f}, Vec3(1.0f), 0.6f);
    ed.mark_changed_for_test("setup");
    r477_settle(ed, 2);
    const Mesh original = *r477_mesh(bo);
    ed.select_for_test(bo->id, false);
    te_press(ed, platform::KEY_D, platform::MOD_CTRL);
    const std::vector<GameObject *> all = te_blockouts(ed);
    CHECK(all.size() == 2);
    if (all.size() != 2) return;
    GameObject *clone = all[0] == bo ? all[1] : all[0];
    CHECK(r477_all_pieces(*clone).size() == 3);
    const std::vector<uint64_t> mine = te_piece_ids(*bo), theirs = te_piece_ids(*clone);
    bool disjoint = true;
    for (uint64_t id : theirs) disjoint = disjoint && !te_has(mine, id);
    CHECK(disjoint && theirs.size() == 3);
    r477_settle(ed, 3);
    CHECK(te_matches(bo) && te_matches(clone));
    Blockout *ob = bo->get<Blockout>(), *cb = clone->get<Blockout>();
    CHECK(cb->state.cache && cb->state.cache.get() != ob->state.cache.get());
    bool faces_own = cb->state.faces.piece.size() == r477_mesh(clone)->face_count();
    for (uint64_t id : cb->state.faces.piece) faces_own = faces_own && te_has(theirs, id);
    bool faces_orig = ob->state.faces.piece.size() == r477_mesh(bo)->face_count();
    for (uint64_t id : ob->state.faces.piece) faces_orig = faces_orig && te_has(mine, id);
    CHECK(faces_own && faces_orig);
    CHECK(r477_same(*r477_mesh(bo), original));
    /* Editing the clone's piece changes the clone only. */
    GameObject *cpocket = te_find_piece(*clone, 1);
    CHECK(cpocket != nullptr);
    if (!cpocket) return;
    cpocket->get<BlockoutPiece>()->radius = 0.5f;
    ed.mark_changed_for_test("clone edit");
    r477_settle(ed, 3);
    CHECK(te_matches(bo) && te_matches(clone));
    CHECK(r477_same(*r477_mesh(bo), original) && !r477_same(*r477_mesh(clone), original));
    ed.command("undo");
    ed.command("undo");
    r477_settle(ed, 3);
    CHECK(te_blockouts(ed).size() == 1 && te_matches(te_blockouts(ed)[0]));
  });

  test("blockout: Ctrl+D on one piece makes one more piece under the same Blockout, one undo step, and the mesh is the build from scratch", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    r477_empty_scene(ed);
    GameObject *wall = nullptr, *cut = nullptr;
    GameObject *bo = te_pocket_blockout(ed, "Blockout", &wall, &cut);
    ed.mark_changed_for_test("setup");
    r477_settle(ed, 2);
    ed.select_for_test(cut->id, false);
    const size_t depth = ed.undo_depth_for_test();
    te_press(ed, platform::KEY_D, platform::MOD_CTRL);
    CHECK(r477_all_pieces(*bo).size() == 3 && r477_count(ed, false) == 1);
    CHECK(ed.undo_depth_for_test() == depth + 1);
    GameObject *fresh = ed.active_object_for_test();
    CHECK(fresh && fresh != cut && fresh->get<BlockoutPiece>() && fresh->parent == bo && fresh->id != cut->id);
    r477_settle(ed, 3);
    CHECK(te_matches(bo));
    if (fresh) fresh->set_local_position(fresh->local().position + Vec3(1.0f, 0.0f, 0.0f));
    ed.mark_changed_for_test("move the copy");
    r477_settle(ed, 3);
    CHECK(te_matches(bo));
    ed.command("undo");
    ed.command("undo");
    r477_settle(ed, 3);
    CHECK(r477_find(ed) && r477_all_pieces(*r477_find(ed)).size() == 2 && te_matches(r477_find(ed)));
  });

  test("blockout: a clicked piece of a duplicated Blockout is the duplicate's own piece, never the original's", [] {
    Editor ed;
    te_view(ed, "camera 25 60 14 4 0 0");
    GameObject *wall = nullptr, *cut = nullptr;
    GameObject *bo = te_pocket_blockout(ed, "Blockout", &wall, &cut);
    r34_steps(ed, 4);
    ed.select_for_test(bo->id, false);
    te_press(ed, platform::KEY_D, platform::MOD_CTRL);
    const std::vector<GameObject *> all = te_blockouts(ed);
    CHECK(all.size() == 2);
    if (all.size() != 2) return;
    GameObject *clone = all[0] == bo ? all[1] : all[0];
    clone->set_world_position({8.0f, 0.0f, 0.0f});
    ed.mark_changed_for_test("move the clone");
    r34_steps(ed, 4);
    CHECK(te_matches(bo) && te_matches(clone));
    GameObject *cpocket = te_find_piece(*clone, 1);
    CHECK(cpocket && cpocket != cut);
    CHECK(te_click_world(ed, clone->world_matrix().point({0.0f, 0.5f, 0.0f})));
    CHECK(ed.selection_size_for_test() == 1 && ed.active_object_for_test() == cpocket);
    CHECK(te_click_world(ed, bo->world_matrix().point({0.0f, 0.5f, 0.0f})));
    CHECK(ed.selection_size_for_test() == 1 && ed.active_object_for_test() == cut);
    CHECK(te_click_world(ed, clone->world_matrix().point({1.7f, 1.0f, 1.7f})));
    CHECK(ed.selection_size_for_test() == 1 && ed.active_object_for_test() && ed.active_object_for_test()->parent == clone);
  });

  test("blockout: until a duplicate's own build lands, a click on it selects nothing of the original's (with workers on)", [] {
    Editor ed;
    te_view(ed, "camera 25 60 14 4 0 0");
    ed.set_async_jobs_for_test(true);
    GameObject *wall = nullptr, *cut = nullptr;
    GameObject *bo = te_pocket_blockout(ed, "Blockout", &wall, &cut);
    ed.command("blockout wait");
    r34_steps(ed, 4);
    ed.hold_blockout_landing_for_test(true);
    ed.select_for_test(bo->id, false);
    te_press(ed, platform::KEY_D, platform::MOD_CTRL);
    const std::vector<GameObject *> all = te_blockouts(ed);
    CHECK(all.size() == 2);
    if (all.size() != 2) return;
    GameObject *clone = all[0] == bo ? all[1] : all[0];
    clone->set_world_position({8.0f, 0.0f, 0.0f});
    ed.mark_changed_for_test("move the clone");
    r34_steps(ed, 4);
    CHECK(ed.blockout_jobs_running_for_test());
    CHECK(te_click_world(ed, clone->world_matrix().point({0.0f, 0.5f, 0.0f})));
    GameObject *got = ed.active_object_for_test();
    const bool original_piece = got == wall || got == cut;
    if (original_piece) std::printf("    a click on the duplicate's pocket selected the ORIGINAL's piece '%s' (the faces' origins are still the original's ids)\n", got->name.c_str());
    CHECK(!original_piece);
    ed.hold_blockout_landing_for_test(false);
    ed.command("blockout wait");
    CHECK(te_matches(bo) && te_matches(clone));
    CHECK(te_click_world(ed, clone->world_matrix().point({0.0f, 0.5f, 0.0f})));
    CHECK(ed.active_object_for_test() == te_find_piece(*clone, 1));
  });

  /* ---- Picking ---- */
  test("blockout: a click selects the right piece through a Blockout that is moved, turned and unevenly scaled, with the piece under an empty folder", [] {
    Editor ed;
    te_view(ed, "camera 25 60 12 3 0 -1");
    GameObject *bo = r477_blockout(ed);
    bo->set_world_position({3.0f, 0.0f, -1.0f});
    bo->set_local_euler({0.0f, 30.0f, 0.0f});
    bo->set_local_scale({1.5f, 1.5f, 1.5f});
    GameObject *wall = r477_piece(ed, *bo, "Block", 0, 0, {0, 0, 0}, {4, 2, 4});
    GameObject *folder = ed.scene().create("Folder", bo);
    folder->set_local_position({0.5f, 0.0f, 0.0f});
    GameObject *cut = r477_piece(ed, *folder, "Pocket", 1, 1, {-0.5f, 1.0f, 0.0f}, Vec3(1.0f), 1.0f, 1.0f);
    r34_steps(ed, 4);
    CHECK(te_matches(bo));
    CHECK(te_click_world(ed, bo->world_matrix().point({0.0f, 0.5f, 0.0f})));
    CHECK(ed.selection_size_for_test() == 1 && ed.active_object_for_test() == cut);
    CHECK(te_click_world(ed, bo->world_matrix().point({1.7f, 1.0f, 1.7f})));
    CHECK(ed.selection_size_for_test() == 1 && ed.active_object_for_test() == wall);
    const uint64_t builds = ed.blockout_main_builds_for_test();
    bo->set_local_scale({2.0f, 1.0f, 0.7f});  // (uneven: the mesh moves with the object, no rebuild)
    bo->set_local_euler({10.0f, 75.0f, 5.0f});
    r34_steps(ed, 4);
    CHECK(ed.blockout_main_builds_for_test() == builds);
    CHECK(te_click_world(ed, bo->world_matrix().point({0.0f, 0.5f, 0.0f})));
    CHECK(ed.selection_size_for_test() == 1 && ed.active_object_for_test() == cut);
    /* An inactive Blockout is not drawn: a click where it was selects nothing. */
    bo->active = false;
    r34_steps(ed, 3);
    CHECK(te_click_world(ed, bo->world_matrix().point({0.0f, 0.5f, 0.0f})));
    CHECK(ed.selection_size_for_test() == 0);
  });

  /* ---- Odd hierarchies ---- */
  test("blockout: a Blockout inside another, a piece that is also a Blockout and a piece with no Blockout above it make valid meshes, draw, and warn nothing", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    r477_empty_scene(ed);
    GameObject *outer = r477_blockout(ed, "Outer");
    r477_piece(ed, *outer, "OuterBox", 0, 0, {0, 0, 0}, {2, 2, 2});
    GameObject *inner = r477_blockout(ed, "Inner");
    ed.scene().set_parent(inner, outer, -1, false);
    inner->set_local_position({5.0f, 0.0f, 0.0f});
    r477_piece(ed, *inner, "InnerBall", 2, 0, {0, 0, 0}, Vec3(1.0f), 0.8f);
    GameObject *both = r477_blockout(ed, "Both");
    both->add<BlockoutPiece>();
    ed.scene().set_parent(both, outer, -1, false);
    both->set_local_position({0.0f, 5.0f, 0.0f});
    r477_piece(ed, *both, "BothBox", 0, 0, {0, 0, 0}, {1, 1, 1});
    GameObject *stray = ed.scene().create("Stray");
    stray->add<BlockoutPiece>();
    const size_t mark = r477_mark();
    r477_settle(ed, 4);
    for (GameObject *g : {outer, inner, both, stray}) {
      ed.select_for_test(g->id, false);
      r477_settle(ed, 3);
    }
    CHECK(te_matches(outer) && te_matches(inner) && te_matches(both));
    CHECK(r477_mesh(outer)->face_count() == 6 && r477_mesh(inner)->face_count() > 6 && r477_mesh(both)->face_count() == 6);
    std::vector<Piece> ps;
    blockout::Settings st;
    blockout_snapshot(*outer, ps, st);
    CHECK(ps.size() == 1);  // (the inner Blockout and the object that is a Blockout are not the outer's pieces)
    CHECK(blockout_of_piece(*both) == outer && blockout_of_piece(*stray) == nullptr);
    CHECK(!stray->get<MeshFilter>());  // (nothing builds for a piece with no Blockout)
    ed.select_for_test(stray->id, false);
    ed.command("edit");
    CHECK(!ed.in_edit_mode());
    CHECK(te_warnings_since(mark) <= 1);  // (the Edit Mode hint is the only warning allowed)
  });

  test("blockout: a Blockout whose component is disabled keeps a valid mesh, draws and picks without a warning, and builds again with the component on", [] {
    Editor ed;
    te_view(ed, "camera 25 60 9 0 0 0");
    GameObject *wall = nullptr, *cut = nullptr;
    GameObject *bo = te_pocket_blockout(ed, "Blockout", &wall, &cut);
    r34_steps(ed, 4);
    bo->get<Blockout>()->enabled = false;
    cut->set_local_position({0.5f, 1.0f, 0.0f});
    ed.mark_changed_for_test("move with the Blockout component off");
    const size_t mark = r477_mark();
    r34_steps(ed, 6);
    const Mesh *m = r477_mesh(bo);
    CHECK(m && structurally_valid(*m));
    std::printf("    component off: the mesh %s the pieces\n", te_matches(bo) ? "follows" : "does not follow");
    CHECK(te_warnings_since(mark) == 0);
    ed.select_for_test(bo->id, false);
    r34_steps(ed, 3);
    bo->get<Blockout>()->enabled = true;
    ed.mark_changed_for_test("on");
    r34_steps(ed, 3);
    CHECK(te_matches(bo));
  });

  /* ---- Hostile numbers ---- */
  test("blockout: hostile numbers in a piece's fields or the Blockout's settings leave a valid mesh and at most one warning, not one a frame", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.command("blockout new");
    GameObject *bo = r477_find(ed);
    BlockoutPiece *pb = r477_find(ed, "Box")->get<BlockoutPiece>();
    BlockoutPiece *pc = r477_piece(ed, *bo, "Cyl", 1, 0, {3, 0, 0}, Vec3(1.0f), 0.5f, 2.0f)->get<BlockoutPiece>();
    BlockoutPiece *ps = r477_piece(ed, *bo, "Sph", 2, 0, {-3, 0, 0}, Vec3(1.0f), 0.7f)->get<BlockoutPiece>();
    Blockout *b = bo->get<Blockout>();
    r477_settle(ed);
    auto reset = [&] {
      pb->size = {2, 2, 2}, pb->shape = 0, pb->operation = 0, pb->blend = 0.0f;
      pc->radius = 0.5f, pc->height = 2.0f, pc->segments = 24, pc->shape = 1, pc->operation = 0, pc->blend = 0.0f;
      ps->radius = 0.7f, ps->segments = 24, ps->rings = 12, ps->shape = 2, ps->operation = 0;
      b->cell = 0.1f, b->method = 0;
    };
    struct Case {
      std::string what;
      std::function<void()> apply;
    };
    std::vector<Case> cases;
    for (float v : {-2.0f, 0.0f, 1e30f, -1e30f, kNaN, kInf, -kInf, 1e-30f}) {
      const std::string s = strprintf("%g", (double)v);
      const bool huge = std::fabs(v) == 1e30f;  // (a 1e6 m piece takes 1.6 s a build: only some fields get it in the suite)
      cases.push_back({"box size.x " + s, [=] { pb->size.x = v; }});
      if (!huge) cases.push_back({"box size (all) " + s, [=] { pb->size = {v, v, v}; }});
      cases.push_back({"cylinder radius " + s, [=] { pc->radius = v; }});
      cases.push_back({"cylinder height " + s, [=] { pc->height = v; }});
      if (!huge) cases.push_back({"sphere radius " + s, [=] { ps->radius = v; }});
      cases.push_back({"blend " + s, [=] { pb->blend = v; }});
      cases.push_back({"field cell " + s, [=] { b->cell = v; }});
    }
    const int big = std::numeric_limits<int>::max(), small = std::numeric_limits<int>::min();
    for (int v : {0, -4, 100000, big, small}) {
      const std::string s = std::to_string(v);
      cases.push_back({"cylinder segments " + s, [=] { pc->segments = v; }});
      cases.push_back({"sphere segments " + s, [=] { ps->segments = v; }});
      cases.push_back({"sphere rings " + s, [=] { ps->rings = v; }});
    }
    for (int v : {-1, 7, big, small}) {
      const std::string s = std::to_string(v);
      cases.push_back({"shape " + s, [=] { pc->shape = v; }});
      cases.push_back({"operation " + s, [=] { pc->operation = v; pb->operation = 0; }});
      cases.push_back({"method " + s, [=] { b->method = v; }});
    }
    int failed = 0;
    for (const Case &c : cases) {
      reset();
      c.apply();
      ed.mark_changed_for_test("hostile");
      const auto t0 = std::chrono::steady_clock::now();
      const size_t first = r477_mark();
      r477_settle(ed, 2);
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      if (ms > 1000.0) std::printf("    (slow: '%s' took %.0f ms to build)\n", c.what.c_str(), ms);  // (printed, never asserted: ADR 0056)
      const size_t early = te_warnings_since(first);
      const size_t mark = r477_mark();
      r477_settle(ed, 5);
      const size_t late = te_warnings_since(mark);
      const Mesh *m = r477_mesh(bo);
      bool finite = m != nullptr;
      for (size_t i = 0; m && i < m->positions.size(); i++) finite = finite && std::isfinite(m->positions[i].x) && std::isfinite(m->positions[i].y) && std::isfinite(m->positions[i].z);
      Mesh ref;
      const bool scratch_ok = r477_scratch(*bo, ref);
      const bool ok = m && finite && structurally_valid(*m) && early <= 1 && late == 0 && (!scratch_ok || r477_same(ref, *m));
      if (!ok) {
        std::printf("    '%s': mesh %s, finite %d, valid %d, warnings %zu then %zu, scratch %s, matches %d\n", c.what.c_str(), m ? "present" : "missing", (int)finite,
                    (int)(m && structurally_valid(*m)), early, late, scratch_ok ? "builds" : "refuses", (int)(scratch_ok && m && r477_same(ref, *m)));
        failed++;
      }
      CHECK(ok);
    }
    std::printf("    %zu hostile settings tried, %d went wrong\n", cases.size(), failed);
  });

  test("blockout: 'blockout set cell' with nan, inf, negative, zero, tiny, huge or a word, and a piece of zero size, never leave a broken mesh", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.command("blockout new");
    ed.command("blockout add sphere 1.4");  // (a sphere poking out of the 2 m box: its own faces show)
    GameObject *bo = r477_find(ed);
    ed.select_for_test(bo->id, false);
    for (const char *v : {"nan", "inf", "-1", "0", "1e-9", "1e30", "abc", "-0", "0.00001"}) {
      const size_t mark = r477_mark();
      ed.command(std::string("blockout set cell ") + v);
      r477_settle(ed, 3);
      const Mesh *m = r477_mesh(r477_find(ed));
      /* (A cell of 10 m is coarser than the 2.8 m sphere: the field path then keeps nothing, an empty mesh the build from scratch makes too.) */
      const bool coarse = r477_find(ed)->get<Blockout>()->cell > 1.0f;
      const bool ok = m && structurally_valid(*m) && (coarse || m->face_count() > 6) && te_matches(r477_find(ed)) && te_warnings_since(mark) <= 1;
      if (!ok)
        std::printf("    cell '%s' (now %g): mesh %s, %zu faces, valid %d, matches a build from scratch %d, warnings %zu\n", v, (double)r477_find(ed)->get<Blockout>()->cell,
                    m ? "present" : "missing", m ? m->face_count() : (size_t)0, (int)(m && structurally_valid(*m)), (int)te_matches(r477_find(ed)), te_warnings_since(mark));
      CHECK(ok);
    }
    for (const char *v : {"banana", "9", "-3", ""}) {
      ed.command(std::string("blockout set method ") + v);
      r477_settle(ed, 2);
      CHECK(r477_mesh(r477_find(ed)) && structurally_valid(*r477_mesh(r477_find(ed))));
    }
    ed.command("blockout set method auto");
    ed.command("blockout add box 0 0 0");
    ed.command("blockout add cylinder 0 0");
    ed.command("blockout add sphere 0");
    ed.command("blockout add box 1e30 1e30 1e30");
    ed.command("blockout add box nan 1 1");
    r477_settle(ed, 3);
    ed.command("blockout wait");
    const size_t mark = r477_mark();
    ed.command("blockout info");
    const std::string said = r477_log_since(mark);
    CHECK(said.find("Skipped") != std::string::npos);
    CHECK(r477_mesh(r477_find(ed)) && structurally_valid(*r477_mesh(r477_find(ed))) && te_matches(r477_find(ed)));
  });

  /* ---- Save, open, old scenes ---- */
  test("blockout: a scene saved while a build runs opens with its pieces, rebuilds to the build from scratch and is not marked changed", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.set_async_jobs_for_test(true);
    r477_empty_scene(ed);
    GameObject *bo = r477_blockout(ed);
    const std::vector<GameObject *> rooms = r477_rooms(ed, *bo, 3, 4);
    ed.command("blockout wait");
    ed.mark_changed_for_test("built");
    ed.hold_blockout_landing_for_test(true);
    rooms.back()->set_local_position(rooms.back()->local().position + Vec3(0.0f, 0.0f, 0.3f));
    ed.mark_changed_for_test("moved");
    r477_settle(ed, 3);
    CHECK(ed.blockout_jobs_running_for_test());
    const Vec3 moved = rooms.back()->local().position;
    const size_t npieces = r477_all_pieces(*bo).size();
    const std::string path = te_scene_path(ed, "r477te_save.scene");
    fs::remove_file(path);
    ed.command("save " + path);
    for (int i = 0; i < 400 && (!fs::exists(path) || ed.save_job_running_for_test()); i++) ed.step_frame_headless();  // (the file is there before the save job has written all of it)
    r477_settle(ed, 3);
    CHECK(fs::exists(path));
    ed.hold_blockout_landing_for_test(false);
    ed.command("blockout wait");
    CHECK(r477_matches(ed));
    ed.command("newscene");
    CHECK(ed.open_scene_for_test(path));
    for (int i = 0; i < 400 && !r477_find(ed); i++) ed.step_frame_headless();
    GameObject *loaded = r477_find(ed);
    CHECK(loaded && r477_all_pieces(*loaded).size() == npieces);
    r477_settle(ed, 3);
    ed.command("blockout wait");
    loaded = r477_find(ed);
    CHECK(loaded && te_matches(loaded) && !ed.scene_dirty_for_test() && !ed.blockout_jobs_running_for_test());
    GameObject *door = r477_all_pieces(*loaded).empty() ? nullptr : r477_all_pieces(*loaded).back();
    CHECK(door && std::fabs(door->local().position.z - moved.z) < 1e-5f);
  });

  test("blockout: a scene with no Blockout is left alone - no MeshFilter added, nothing built, with workers off and on, and after a save and open", [] {
    Editor ed;
    ed.init_headless(900, 600);
    GameObject *stray = ed.scene().create("OnlyAPiece");
    stray->add<BlockoutPiece>();
    size_t objects = 0, filters = 0, renderers = 0;
    auto count = [&](size_t &o, size_t &f, size_t &r) {
      o = f = r = 0;
      ed.scene().for_each([&](GameObject &g) {
        o++;
        f += g.get<MeshFilter>() != nullptr;
        r += g.get<MeshRenderer>() != nullptr;
      });
    };
    count(objects, filters, renderers);
    for (int async = 0; async < 2; async++) {
      ed.set_async_jobs_for_test(async == 1);
      r477_settle(ed, 6);
      ed.command("blockout wait");
      ed.command("blockout rebuild");
      size_t o = 0, f = 0, r = 0;
      count(o, f, r);
      CHECK(o == objects && f == filters && r == renderers);
    }
    CHECK(ed.blockout_main_builds_for_test() == 0 && ed.blockout_started_for_test() == 0 && !ed.blockout_jobs_running_for_test());
    CHECK(!stray->get<MeshFilter>());
    const std::string path = te_scene_path(ed, "r477te_plain.scene");
    ed.command("save " + path);
    for (int i = 0; i < 400 && (!fs::exists(path) || ed.save_job_running_for_test()); i++) ed.step_frame_headless();  // (the file is there before the save job has written all of it)
    r477_settle(ed, 3);
    ed.command("newscene");
    CHECK(ed.open_scene_for_test(path));
    for (int i = 0; i < 400 && !r477_find(ed, "OnlyAPiece"); i++) ed.step_frame_headless();
    r477_settle(ed, 4);
    size_t o = 0, f = 0, r = 0;
    count(o, f, r);
    CHECK(o == objects && f == filters && r == renderers);
  });

  /* ---- Edit Mode and the other writers of a mesh ---- */
  test("blockout: Tab is refused on a Blockout in every keymap preset (where a parametric shape enters Edit Mode by Tab)", [] {
    for (const char *preset : {"Unity", "Blender", "Maya", "SketchUp"}) {
      Editor ed;
      ed.init_headless(1000, 700);
      ed.command(std::string("keymap ") + preset);
      ed.command("blockout new");
      r477_settle(ed, 2);
      GameObject *bo = r477_find(ed);
      const Mesh *mesh = r477_mesh(bo);
      const size_t pieces = r477_count(ed, true);
      for (int who = 0; who < 2; who++) {
        ed.select_for_test(who == 0 ? bo->id : r477_find(ed, "Box")->id, false);
        te_press(ed, platform::KEY_TAB);
        if (!ed.in_edit_mode()) continue;
        std::printf("    keymap %s: Tab entered Edit Mode on the %s\n", preset, who == 0 ? "Blockout" : "piece");
        CHECK(!ed.in_edit_mode());
        ed.command("edit off");
      }
      CHECK(r477_mesh(r477_find(ed)) == mesh && r477_count(ed, true) == pieces);
      ed.command("create Shape: Box");
      r477_settle(ed, 2);
      te_press(ed, platform::KEY_TAB);
      const bool shape_enters = ed.in_edit_mode();
      if (!shape_enters) std::printf("    keymap %s: Tab does not enter Edit Mode for a shape either (the preset's own binding)\n", preset);
      ed.command("edit off");
    }
  });

  test("blockout: every command route into a mode that writes the mesh (edit, esel, vsel, edittool, editop, draw, sculpt, weight paint, paint) leaves a Blockout and a piece as they were", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.command("blockout new");
    ed.command("blockout add cylinder subtract 0.5 3");
    r477_settle(ed, 2);
    GameObject *bo = r477_find(ed);
    const Mesh *mesh = r477_mesh(bo);
    const uint64_t version = mesh->version;
    const size_t objects = ed.scene().object_count();
    for (int who = 0; who < 2; who++) {
      const char *routes[] = {"edit", "edit vertex", "edit face", "edit edge", "edit face all", "esel 0 1 1 2", "vsel 0 1 2", "edittool extrude", "edittool fill",
                              "editop extrude", "editop fill", "editop bevel", "sculpt on", "weightpaint on", "draw rectangle", "draw polyline", "pushpull individual on"};
      for (const char *route : routes) {
        if (who == 1 && std::string(route).rfind("draw", 0) == 0) continue;  // (a piece has no mesh: Draw makes a new object to draw in)
        ed.select_for_test(who == 0 ? bo->id : r477_find(ed, "Subtract Cylinder")->id, false);
        ed.command(route);
        r477_settle(ed, 1);
        const bool still = !ed.in_edit_mode() && !ed.sculpt_mode_for_test() && !ed.weight_paint_active_for_test() && r477_mesh(r477_find(ed)) == mesh && mesh->version == version &&
                           ed.scene().object_count() == objects;
        if (!still) std::printf("    '%s' on the %s changed the mode, the mesh or the objects\n", route, who == 0 ? "Blockout" : "piece");
        CHECK(still);
        ed.command("edit off");
        ed.command("sculpt off");
        ed.command("weightpaint off");
      }
    }
    CHECK(r477_matches(ed));
    /* Texture paint is allowed: it must not crash and must not leave the mesh other than the pieces' next build. */
    ed.select_for_test(bo->id, false);
    ed.command("paint on");
    r477_settle(ed, 2);
    ed.command("paint off");
    r477_settle(ed, 2);
    GameObject *cyl = r477_find(ed, "Subtract Cylinder");
    cyl->set_local_position(cyl->local().position + Vec3(0.1f, 0.0f, 0.0f));
    ed.mark_changed_for_test("nudge");
    r477_settle(ed, 3);
    CHECK(r477_matches(ed));
  });

  test("blockout: Subdivide, Join, Separate, Boolean, Apply Modifiers and the other mesh operators on a Blockout never leave a mesh its pieces do not make", [] {
    struct Op {
      const char *command;
      bool with_other;
    };
    const Op ops[] = {{"subdivide", false}, {"subdivide 2", false}, {"meshop subdivide", false}, {"meshop smooth", false}, {"meshop triangulate", false},
                      {"meshop mirror_x", false}, {"meshop apply_transform", false}, {"meshop shade_auto_smooth", false}, {"meshop delete_loose", false},
                      {"meshop smart_fill", false}, {"meshop merge", false}, {"applymods", false}, {"separate loose", false}, {"separate selection", false},
                      {"join", true}, {"boolean difference", true}, {"boolean union", true}, {"boolean intersect", true}, {"boolean difference modifier", true}};
    int wrong = 0;
    for (const Op &op : ops) {
      for (int who = 0; who < 2; who++) {
        Editor ed;
        ed.init_headless(1200, 800);
        r477_empty_scene(ed);
        GameObject *bo = r477_blockout(ed);
        GameObject *wall = r477_piece(ed, *bo, "Wall", 0, 0, {0, 0, 0}, {2, 2, 2});
        r477_piece(ed, *bo, "Far", 0, 0, {6, 0, 0}, {2, 2, 2});  // (a second, separate part for Separate)
        GameObject *cut = r477_piece(ed, *bo, "Cut", 1, 1, {0, 0, 0}, Vec3(1.0f), 0.4f, 4.0f);
        GameObject *other = ed.scene().create("Other");
        other->add<MeshFilter>()->mesh = primitives::cube();
        other->add<MeshRenderer>();
        other->set_local_position({0.0f, 5.0f, 0.0f});
        r477_settle(ed, 3);
        const size_t blockouts = r477_count(ed, false), pieces = r477_count(ed, true);
        GameObject *target = who == 0 ? bo : cut;
        if (op.with_other) ed.select_for_test(other->id, false), ed.select_for_test(target->id, true);
        else ed.select_for_test(target->id, false);
        const size_t mark = r477_mark();
        ed.command(op.command);
        r477_settle(ed, 4);
        ed.command("blockout wait");
        (void)wall;
        std::vector<GameObject *> now = te_blockouts(ed);
        bool ok = now.size() == blockouts && r477_count(ed, true) == pieces;
        for (GameObject *g : now) ok = ok && te_matches(g);
        if (!ok) {
          wrong++;
          std::printf("    '%s' with the %s selected: the Blockout%s ends %s\n", op.command, who == 0 ? "Blockout" : "piece", op.with_other ? " and another mesh" : "",
                      now.size() != blockouts ? strprintf("with %zu Blockout objects instead of %zu (an object was made from its mesh)", now.size(), blockouts).c_str()
                                              : "with a mesh its pieces do not make (until a piece changes)");
        }
        CHECK(ok);
        /* Whatever the operator did, the next change of a piece must rebuild to the pieces' mesh, and nothing may be left half. */
        now = te_blockouts(ed);
        for (GameObject *g : now) {
          std::vector<GameObject *> ps = r477_all_pieces(*g);
          if (ps.empty()) continue;
          ps.front()->set_local_position(ps.front()->local().position + Vec3(0.05f, 0.0f, 0.0f));
        }
        ed.mark_changed_for_test("nudge");
        r477_settle(ed, 3);
        ed.command("blockout wait");
        for (GameObject *g : te_blockouts(ed)) CHECK(te_matches(g));
        (void)mark;
      }
    }
    std::printf("    %d of %zu operator and selection pairs left a Blockout whose mesh is not its pieces'\n", wrong, sizeof(ops) / sizeof(ops[0]) * 2);
  });

  /* ---- Play ---- */
  test("blockout: Play started with a build held waits for it, nothing rebuilds while playing, and the scene after Stop has the right mesh", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.set_async_jobs_for_test(true);
    r477_empty_scene(ed);
    GameObject *bo = r477_blockout(ed);
    r477_rooms(ed, *bo, 2, 1);
    ed.command("blockout wait");
    ed.mark_changed_for_test("built");
    r477_settle(ed, 2);
    ed.hold_blockout_landing_for_test(true);
    GameObject *door = r477_find(ed, "Door");
    door->set_local_position(door->local().position + Vec3(0.0f, 0.0f, 0.3f));
    ed.mark_changed_for_test("moved");
    r477_settle(ed, 3);
    ed.command("play");
    CHECK(r477_matches(ed) && !ed.blockout_jobs_running_for_test());
    const Mesh *during = r477_mesh(r477_find(ed));
    const uint64_t started = ed.blockout_started_for_test();
    r477_find(ed, "Door")->set_local_position({9.0f, 0.0f, 0.0f});
    r477_settle(ed, 5);
    CHECK(r477_mesh(r477_find(ed)) == during && ed.blockout_started_for_test() == started);  // (nothing rebuilds while playing)
    ed.command("stop");
    r477_settle(ed, 3);
    ed.hold_blockout_landing_for_test(false);
    ed.command("blockout wait");
    CHECK(r477_find(ed) && te_matches(r477_find(ed)) && !ed.blockout_jobs_running_for_test());
    GameObject *back = r477_find(ed, "Door");
    CHECK(back && std::fabs(back->local().position.x - 9.0f) > 1.0f);  // (the pre-Play pieces)
    /* Play and Stop with no build pending leave the mesh as it was. */
    const Mesh before = *r477_mesh(r477_find(ed));
    ed.command("play");
    ed.command("stop");
    r477_settle(ed, 3);
    ed.command("blockout wait");
    CHECK(r477_same(*r477_mesh(r477_find(ed)), before));
  });

  /* ---- Export ---- */
  test("blockout: an export of a selection that holds only a piece writes none of the piece, and one of the Blockout writes its mesh", [] {
    Editor ed;
    ed.init_headless(900, 600);
    r477_empty_scene(ed);
    ed.command("blockout new");
    ed.command("blockout add cylinder subtract 0.5 3");
    r477_settle(ed, 2);
    GameObject *bo = r477_find(ed);
    const std::string dir = ed.resolve_asset_path_for_test("Assets/Exports");
    fs::make_dirs(dir);
    const std::string piece_obj = fs::join(dir, "r477te_piece.obj"), whole_obj = fs::join(dir, "r477te_whole.obj");
    fs::remove_file(piece_obj), fs::remove_file(whole_obj);
    ed.select_for_test(r477_find(ed, "Box")->id, false);
    ed.select_for_test(r477_find(ed, "Subtract Cylinder")->id, true);
    ed.command("export obj " + piece_obj);
    for (int i = 0; i < 60 && (!fs::exists(piece_obj) || ed.file_export_running_for_test()); i++) ed.step_frame_headless();
    std::string text;
    size_t verts = 0;
    if (fs::exists(piece_obj) && fs::read_file(piece_obj, text)) {
      std::istringstream is(text);
      for (std::string line; std::getline(is, line);) verts += line.rfind("v ", 0) == 0;
    }
    CHECK(verts == 0 && text.find("Subtract Cylinder") == std::string::npos);
    ed.select_for_test(bo->id, false);
    ed.command("export obj " + whole_obj);
    for (int i = 0; i < 400 && (!fs::exists(whole_obj) || ed.file_export_running_for_test()); i++) ed.step_frame_headless();
    r477_settle(ed, 4);
    std::string whole;
    CHECK(fs::read_file(whole_obj, whole));
    verts = 0;
    std::istringstream is(whole);
    for (std::string line; std::getline(is, line);) verts += line.rfind("v ", 0) == 0;
    CHECK(verts == r477_mesh(bo)->vert_count());
  });

  test("blockout: a piece with a NaN, infinite or huge position, scale or turn is skipped and named, the rest builds, and a Blockout scaled to zero or NaN keeps its mesh", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.record_buttons_for_test(true);
    ed.command("blockout new");
    GameObject *bo = r477_find(ed);
    GameObject *nan_at = r477_piece(ed, *bo, "NanAt", 0, 0, {kNaN, 0, 0}, {1, 1, 1});
    GameObject *inf_at = r477_piece(ed, *bo, "InfAt", 0, 0, {kInf, 0, 0}, {1, 1, 1});
    GameObject *inf_scale = r477_piece(ed, *bo, "InfScale", 0, 0, {4, 0, 0}, {1, 1, 1});
    inf_scale->set_local_scale({kInf, 1.0f, 1.0f});
    GameObject *nan_turn = r477_piece(ed, *bo, "NanTurn", 0, 0, {-4, 0, 0}, {1, 1, 1});
    nan_turn->set_local_euler({kNaN, 0.0f, 0.0f});
    GameObject *huge = r477_piece(ed, *bo, "Huge", 0, 0, {0, 0, 4}, {1, 1, 1});
    huge->set_local_scale({1e20f, 1e20f, 1e20f});
    (void)nan_at, (void)inf_at;
    ed.select_for_test(bo->id, false);
    const size_t mark = r477_mark();
    r477_settle(ed, 4);
    const Mesh *m = r477_mesh(bo);
    CHECK(m && structurally_valid(*m) && te_matches(bo));
    bool finite = m != nullptr;
    for (size_t i = 0; m && i < m->positions.size(); i++) finite = finite && std::isfinite(m->positions[i].x) && std::isfinite(m->positions[i].y) && std::isfinite(m->positions[i].z);
    CHECK(finite);
    CHECK(te_warnings_since(mark) <= 1);
    ed.command("blockout info");
    const std::string said = r477_log_since(mark);
    for (const char *name : {"NanAt", "InfAt", "InfScale", "NanTurn"}) {
      const bool named = said.find(std::string("Skipped '") + name + "'") != std::string::npos;
      if (!named) std::printf("    '%s' is not named as skipped in 'blockout info'\n", name);
      CHECK(named);
    }
    /* The Blockout itself flattened or NaN: the pieces' matrices are relative to it, so nothing about the mesh changes. */
    const Mesh built = *m;
    const uint64_t builds = ed.blockout_main_builds_for_test();
    bo->set_local_scale({0.0f, 0.0f, 0.0f});
    r477_settle(ed, 3);
    bo->set_local_scale({kNaN, 1.0f, 1.0f});
    r477_settle(ed, 3);
    bo->set_local_position({kNaN, kNaN, kNaN});
    r477_settle(ed, 3);
    CHECK(ed.blockout_main_builds_for_test() == builds && r477_same(*r477_mesh(bo), built));
    bo->set_local_scale({1.0f, 1.0f, 1.0f});
    bo->set_local_position({0.0f, 0.0f, 0.0f});
    r477_settle(ed, 3);
    CHECK(te_matches(bo));
  });

  test("blockout: the Inspector's status line says 'Building...' while the build runs and the finished status after it lands", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.set_async_jobs_for_test(true);
    ed.record_buttons_for_test(true);
    r477_empty_scene(ed);
    GameObject *bo = r477_blockout(ed, "Hall");
    r477_rooms(ed, *bo, 3, 4);
    ed.select_for_test(bo->id, false);
    ed.hold_blockout_landing_for_test(true);
    r477_settle(ed, 4);
    auto has = [&](const std::string &key) {
      for (const auto &kv : ed.ui_for_test().button_rects)
        if (kv.first.rfind(key, 0) == 0) return true;
      return false;
    };
    ed.record_buttons_for_test(true);
    r477_settle(ed, 2);
    CHECK(has("shown:Building..."));
    CHECK(has("shown:Not built yet"));
    ed.hold_blockout_landing_for_test(false);
    ed.command("blockout wait");
    ed.record_buttons_for_test(true);
    r477_settle(ed, 3);
    CHECK(!has("shown:Building...") && !has("shown:Not built yet") && has("shown:") && has("shown:") );
    bool pieces_line = false;
    for (const auto &kv : ed.ui_for_test().button_rects) pieces_line = pieces_line || (kv.first.rfind("shown:", 0) == 0 && kv.first.find("pieces used") != std::string::npos);
    CHECK(pieces_line);
  });

  /* ---- The job tray ---- */
  test("blockout: 'jobs' lists a running Blockout build as 'Building <name>' that cannot be cancelled, and 'jobs cancel' leaves it running without a crash", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.set_async_jobs_for_test(true);
    GameObject *bo = r477_blockout(ed, "Hall");
    r477_rooms(ed, *bo, 3, 4);
    ed.hold_blockout_landing_for_test(true);
    r477_settle(ed, 3);
    CHECK(ed.blockout_jobs_running_for_test());
    size_t mark = r477_mark();
    ed.command("jobs");
    std::string said = r477_log_since(mark);
    CHECK(said.find("Building Hall") != std::string::npos && said.find("cannot be cancelled") != std::string::npos);
    mark = r477_mark();
    ed.command("jobs cancel Building Hall");
    said = r477_log_since(mark);
    CHECK(said.find("cannot be cancelled") != std::string::npos && ed.blockout_jobs_running_for_test());
    ed.command("jobs cancel Nothing By That Name");
    CHECK(ed.blockout_jobs_running_for_test());
    ed.hold_blockout_landing_for_test(false);
    ed.command("blockout wait");
    mark = r477_mark();
    ed.command("jobs");
    CHECK(r477_log_since(mark).find("Building Hall") == std::string::npos && r477_matches(ed, "Hall"));
  });
}

/* Round 477, the code review's findings: each was a fault before its fix. */
static void r477_until_built(Editor &ed) {  // (the workers return; nothing is adopted - no frame, no poll)
  for (int i = 0; i < 20000 && !ed.blockout_jobs_done_for_test(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

static void tests_round477_review() {
  test("blockout: a build that finishes in the frame the gizmo moved its piece on is still shown, and the piece's new place is built next", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.set_async_jobs_for_test(true);
    r477_empty_scene(ed);
    GameObject *bo = r477_blockout(ed);
    r477_piece(ed, *bo, "Wall", 0, 0, {0, 0, 0}, {2, 2, 2});
    GameObject *cut = r477_piece(ed, *bo, "Cut", 1, 1, {0, 0, 0}, Vec3(1.0f), 0.4f, 4.0f);
    ed.command("blockout wait");
    ed.hold_blockout_landing_for_test(true);
    cut->set_local_position({0.3f, 0.0f, 0.0f});
    ed.mark_changed_for_test("drag");
    ed.step_frame_headless();  // (the frame asks for the piece at 0.3 ...)
    Mesh at_03;
    CHECK(r477_scratch(*bo, at_03));
    cut->set_local_position({0.5f, 0.0f, 0.0f});  // (... and its Scene view, drawn after the asking, moves the piece on)
    r477_until_built(ed);
    const uint64_t landed0 = ed.blockout_landed_for_test();
    ed.hold_blockout_landing_for_test(false);
    ed.blockout_poll_for_test();  // (the frame's poll: the build of 0.3 is done, the piece is at 0.5, nothing newer was asked for yet)
    CHECK(ed.blockout_landed_for_test() == landed0 + 1);
    CHECK(r477_mesh(bo) && r477_same(*r477_mesh(bo), at_03));  // (one build behind, shown)
    CHECK(ed.blockout_jobs_running_for_test());                // (0.5 started at once)
    ed.command("blockout wait");
    CHECK(r477_matches(ed) && ed.blockout_main_builds_for_test() == 0);
  });

  test("blockout: a mesh that lands a frame after its worker finished is drawn by the Scene view at once - no change the views' counter missed", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.set_async_jobs_for_test(true);
    r477_empty_scene(ed);
    GameObject *bo = r477_blockout(ed);
    r477_piece(ed, *bo, "Wall", 0, 0, {0, 0, 0}, {2, 2, 2});
    GameObject *cut = r477_piece(ed, *bo, "Cut", 1, 1, {0, 0, 0}, Vec3(1.0f), 0.4f, 4.0f);
    ed.command("blockout wait");
    for (int i = 0; i < 10; i++) ed.step_frame_headless();
    const uint64_t misses0 = ed.scene_hash_misses_for_test();
    ed.hold_blockout_landing_for_test(true);
    cut->set_local_position({0.4f, 0.0f, 0.0f});
    ed.mark_changed_for_test("move");
    ed.step_frame_headless();
    r477_until_built(ed);      // (the worker's own touch() of the new mesh is behind us)
    ed.step_frame_headless();  // (a frame whose views were drawn with the old mesh after it)
    ed.hold_blockout_landing_for_test(false);
    for (int i = 0; i < 20; i++) ed.step_frame_headless();  // (past the every-8th-frame check of the scene's hash)
    CHECK(r477_matches(ed));
    CHECK(ed.scene_hash_misses_for_test() == misses0);
  });

  test("blockout: Geometry to Origin, the UV operators and Paint leave a Blockout's mesh as its pieces make it, each with the hint", [] {
    Editor ed;
    ed.init_headless(900, 600);
    r477_empty_scene(ed);
    GameObject *bo = r477_blockout(ed);
    r477_piece(ed, *bo, "Wall", 0, 0, {1, 0, 0}, {2, 2, 2});
    r477_settle(ed, 2);
    ed.select_for_test(bo->id, false);
    const Mesh *mesh = r477_mesh(bo);
    const uint64_t version = mesh->version;
    for (const char *line : {"origin geometry", "uv unwrap", "uv smart", "uv pack", "paint on"}) {
      const size_t mark = r477_mark();
      ed.command(line);
      r477_settle(ed, 2);
      const bool hinted = r477_log_since(mark).find("rebuilt from its pieces") != std::string::npos;
      const bool same = r477_mesh(bo) == mesh && mesh->version == version && r477_matches(ed);
      if (!hinted || !same) std::printf("    '%s': %s%s\n", line, hinted ? "" : "no hint ", same ? "" : "the mesh changed");
      CHECK(hinted && same);
    }
  });
}

static void tests_round477_review2() {
  test("blockout: DynaMesh, Quad Remesh, Bake Textures, Mesh Maps, shape keys, vertex groups and Subdivide on a worker leave a Blockout's mesh as its pieces make it", [] {
    for (int workers = 0; workers < 2; workers++) {
      Editor ed;
      ed.init_headless(900, 600);
      ed.set_async_jobs_for_test(workers == 1);
      r477_empty_scene(ed);
      GameObject *bo = r477_blockout(ed);
      r477_piece(ed, *bo, "Wall", 0, 0, {1, 0, 0}, {2, 2, 2});
      r477_settle(ed, 2);
      ed.command("blockout wait");
      ed.select_for_test(bo->id, false);
      const Mesh *mesh = r477_mesh(bo);
      const uint64_t version = mesh->version;
      for (const char *line : {"sculpt dynamesh 32", "sculpt remesh 200", "baketex 256", "meshmaps 256", "shape add Puff", "vgroup add Top", "subdivide"}) {
        const size_t mark = r477_mark();
        ed.command(line);
        r477_settle(ed, 3);
        const std::string said = r477_log_since(mark);
        const bool quiet = std::string(line) == "save" || std::string(line) == "sculpt remesh 200";  // (save writes nothing into the mesh; Quad Remesh may say the build has no QuadriFlow first)
        const bool hinted = quiet || said.find("rebuilt from its pieces") != std::string::npos;
        const Mesh *now = r477_mesh(bo);
        const bool same = now == mesh && now->version == version && now->uvs.empty() && now->shape_keys.empty() && now->vertex_groups.empty() && r477_matches(ed);
        if (!hinted || !same) std::printf("    '%s' (workers %s): %s%s\n", line, workers ? "on" : "off", hinted ? "" : "no hint ", same ? "" : "the mesh changed");
        CHECK(hinted && same);
      }
      CHECK(!ed.mesh_job_started_for_test());
    }
  });

  test("blockout: 'save' right after a script moved a piece writes the mesh the pieces make now", [] {
    Editor ed;
    ed.init_headless(900, 600);
    r477_empty_scene(ed);
    GameObject *bo = r477_blockout(ed);
    GameObject *wall = r477_piece(ed, *bo, "Wall", 0, 0, {0, 0, 0}, {2, 2, 2});
    r477_settle(ed, 2);
    wall->set_local_position({3.0f, 0.0f, 0.0f});  // (no frame between this and the save)
    ed.mark_changed_for_test("move");
    const std::string dir = ed.resolve_asset_path_for_test("Assets/Scenes");
    fs::make_dirs(dir);
    const std::string path = fs::join(dir, "r477_save_now.scene");
    ed.command("save " + path);
    CHECK(r477_matches(ed));
    fs::remove_file(path);
  });
}

/* ------------------------------------------------------------------ round 478 (task 0454): rooms and Intersect */
namespace {

Piece r478_room(uint64_t id, Vec3 air, Vec3 at, bool keep = false, float thick = 0.2f) {
  Piece p = box_piece(id, air, at);
  p.room = true, p.keep_walls = keep, p.wall = p.floor = p.ceiling = thick;
  return p;
}

size_t r478_faces_of(const R476Built &b, uint64_t id) {
  size_t n = 0;
  for (uint64_t p : b.faces.piece) n += p == id;
  return n;
}

/* Corners of the result strictly inside the box lo..hi (a hundredth of a millimetre in from its sides). */
size_t r478_corners_inside(const Mesh &m, Vec3 lo, Vec3 hi) {
  size_t n = 0;
  const float e = 1e-5f;
  for (const Vec3 &p : m.positions) n += p.x > lo.x + e && p.x < hi.x - e && p.y > lo.y + e && p.y < hi.y - e && p.z > lo.z + e && p.z < hi.z - e;
  return n;
}

}  // namespace

static void tests_round478() {
  test("blockout rooms: one box room is two shells - 12 faces, 16 corners - with its inside named floor, ceiling and walls", [] {
    for (Path path : r476_paths()) {
      const std::vector<Piece> pieces{r478_room(5, {4, 3, 4}, {0.5f, 0.25f, -1.0f})};
      const R476Built b = r476_build(pieces, path);
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      CHECK(b.mesh.face_count() == 12);
      CHECK(b.mesh.vert_count() == 16);
      CHECK_NEAR(r476_volume(b.mesh), 4.4 * 3.4 * 4.4 - 48.0, 2e-3);
      CHECK(r476_flatness(b, pieces) <= 1e-5 * 7.0);
      unsigned inside = 0, outside = 0;
      int floors = 0, ceilings = 0, walls = 0, outs = 0;
      for (size_t f = 0; f < b.faces.side.size(); f++) {
        CHECK(b.faces.piece[f] == 5);
        const uint32_t side = b.faces.side[f];
        if (side < 6) inside |= 1u << side;
        if (side >= blockout::kSideShell && side < blockout::kSideShell + 6) outside |= 1u << (side - blockout::kSideShell);
        const blockout::SideClass cls = blockout::side_class(pieces[0], side);
        floors += cls == blockout::SideClass::Floor, ceilings += cls == blockout::SideClass::Ceiling, walls += cls == blockout::SideClass::Wall, outs += cls == blockout::SideClass::Outside;
      }
      CHECK(inside == 63u && outside == 63u);
      CHECK(floors == 1 && ceilings == 1 && walls == 4 && outs == 6);
      /* The floor is the face at the air's bottom. */
      for (size_t f = 0; f < b.mesh.face_count(); f++)
        if (blockout::side_class(pieces[0], b.faces.side[f]) == blockout::SideClass::Floor) CHECK_NEAR(b.mesh.positions[b.mesh.face_verts(f)[0]].y, 0.25f - 1.5f, 1e-5f);
      CHECK(b.report.clusters.size() == 1 && b.report.clusters[0].pieces == 1);
    }
  });

  test("blockout rooms: a cylinder room's wall is as thick as set between its facets, and its volume is the shell's less the air's", [] {
    for (Path path : r476_paths()) {
      Piece p = cylinder_piece(1, 2.0f, 3.0f, 16, {0, 0, 0});
      p.room = true, p.wall = 0.3f, p.floor = 0.2f, p.ceiling = 0.1f;
      const R476Built b = r476_build({p}, path, 0.05f);
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      const double pi = 3.14159265358979323846, n = 16.0;
      const double outer = 2.0 + 0.3 / std::cos(pi / n);
      const double area = [&](double r) { return 0.5 * n * r * r * std::sin(2.0 * pi / n); }(outer), air = 0.5 * n * 4.0 * std::sin(2.0 * pi / n);
      CHECK_NEAR(r476_volume(b.mesh), area * 3.3 - air * 3.0, 2e-3);
      CHECK(b.mesh.face_count() == 2 * 18);
      float top = -1e9f, bottom = 1e9f;
      for (const Vec3 &q : b.mesh.positions) top = std::max(top, q.y), bottom = std::min(bottom, q.y);
      CHECK_NEAR(top, 1.6f, 1e-5f);
      CHECK_NEAR(bottom, -1.7f, 1e-5f);
    }
  });

  test("blockout rooms: two Merge rooms whose air overlaps are one room - no wall, no face, where both have air", [] {
    for (Path path : r476_paths()) {
      const std::vector<Piece> pieces{r478_room(1, {4, 3, 4}, {0, 0, 0}), r478_room(2, {4, 3, 4}, {3, 0, 0})};
      const R476Built b = r476_build(pieces, path);
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      CHECK_NEAR(r476_volume(b.mesh), (2 * 4.4 * 3.4 * 4.4 - 1.4 * 3.4 * 4.4) - (96.0 - 12.0), 3e-3);
      CHECK(r478_corners_inside(b.mesh, {-2, -1.5f, -2}, {5, 1.5f, 2}) == 0);
      CHECK(b.mesh.face_count() == 12);  // (one long room: its faces merge across the two)
    }
  });

  test("blockout rooms: Merge rooms side by side keep the wall between them; rooms that only touch stand wall to wall with nothing between", [] {
    for (Path path : r476_paths()) {
      const R476Built shared = r476_build({r478_room(1, {4, 3, 4}, {0, 0, 0}), r478_room(2, {4, 3, 4}, {4.2f, 0, 0})}, path);
      CHECK(shared.ok && r476_sound(shared.mesh, r476_name(path)));
      CHECK_NEAR(r476_volume(shared.mesh), 2 * 4.4 * 3.4 * 4.4 - 0.2 * 3.4 * 4.4 - 96.0, 3e-3);
      CHECK(shared.mesh.face_count() == 6 + 12);
      const R476Built touch = r476_build({r478_room(1, {4, 3, 4}, {0, 0, 0}), r478_room(2, {4, 3, 4}, {4.4f, 0, 0})}, path);
      CHECK(touch.ok && r476_sound(touch.mesh, r476_name(path)));
      CHECK_NEAR(r476_volume(touch.mesh), 2 * (4.4 * 3.4 * 4.4 - 48.0), 3e-3);
      CHECK(touch.mesh.face_count() == 6 + 12);
      CHECK(r478_corners_inside(touch.mesh, {2.0f, -1.7f, -2.2f}, {2.4f, 1.7f, 2.2f}) == 0);
    }
  });

  test("blockout rooms: a Keep room keeps all its walls - inside a Merge room, across the wall between two, and inside another Keep room", [] {
    for (Path path : r476_paths()) {
      const R476Built in = r476_build({r478_room(1, {8, 3, 8}, {0, 0, 0}), r478_room(2, {2, 2, 2}, {0.5f, -0.3f, 0.25f}, true)}, path);
      CHECK(in.ok && r476_sound(in.mesh, r476_name(path)));
      CHECK_NEAR(r476_volume(in.mesh), (8.4 * 3.4 * 8.4 - 192.0) + (2.4 * 2.4 * 2.4 - 8.0), 5e-3);
      CHECK(r478_faces_of(in, 2) >= 11);  // (six inside; its outside's bottom lies on the Merge room's floor and is one with it)

      /* Across the wall: the wall does not run through the Keep room's air. */
      const std::vector<Piece> across{r478_room(1, {4, 3, 4}, {0, 0, 0}), r478_room(2, {4, 3, 4}, {4.2f, 0, 0}), r478_room(3, {2, 2, 2}, {2.1f, -0.3f, 0}, true)};
      const R476Built b = r476_build(across, path);
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      CHECK_NEAR(r476_volume(b.mesh), (2 * 4.4 * 3.4 * 4.4 - 0.2 * 3.4 * 4.4 - 96.0) - 0.2 * 2 * 2 + (2.4 * 2.4 * 2.4 - 8.0) - (2.4 * 2.4 - 4.0) * 0.2, 4e-3);
      CHECK(r478_corners_inside(b.mesh, {1.1f, -1.3f, -1}, {3.1f, 0.7f, 1}) == 0);

      const R476Built two = r476_build({r478_room(1, {2, 2, 2}, {0, 0, 0}, true), r478_room(2, {2, 2, 2}, {1.5f, 0, 0}, true)}, path);
      CHECK(two.ok && r476_sound(two.mesh, r476_name(path)));
      CHECK_NEAR(r476_volume(two.mesh), 2 * (2.4 * 2.4 * 2.4 - 8.0) - 1.584, 2e-3);
    }
  });

  test("blockout rooms: an Add piece standing in a room stays - a column from floor to ceiling is one solid with both", [] {
    for (Path path : r476_paths()) {
      const std::vector<Piece> pieces{r478_room(1, {4, 3, 4}, {0, 0, 0}), box_piece(2, {0.5f, 3.0f, 0.5f}, {0.5f, 0, 0.25f})};
      const R476Built b = r476_build(pieces, path);
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      CHECK_NEAR(r476_volume(b.mesh), 4.4 * 3.4 * 4.4 - 48.0 + 0.75, 2e-3);
      size_t column = 0;
      for (size_t f = 0; f < b.mesh.face_count(); f++) {
        if (b.faces.piece[f] != 2) continue;
        column++;
        CHECK(b.faces.side[f] != 2 && b.faces.side[f] != 3);  // (no face where it meets the floor and the ceiling)
      }
      CHECK(column == 4);
    }
  });

  test("blockout rooms: a door through the wall between two rooms opens one into the other, and a window leaves a sill", [] {
    for (Path path : r476_paths()) {
      std::vector<Piece> pieces{r478_room(1, {4, 3, 4}, {0, 0, 0}), r478_room(2, {4, 3, 4}, {4.2f, 0, 0}), box_piece(3, {0.6f, 2.1f, 0.9f}, {2.1f, -0.45f, 0}, Op::Subtract)};
      const R476Built door = r476_build(pieces, path);
      CHECK(door.ok && r476_sound(door.mesh, r476_name(path)));
      CHECK_NEAR(r476_volume(door.mesh), 2 * 4.4 * 3.4 * 4.4 - 0.2 * 3.4 * 4.4 - 96.0 - 0.2 * 2.1 * 0.9, 3e-3);
      CHECK(r478_faces_of(door, 3) == 3);  // (two jambs and a head: the cut reaches the floor, so there is no step)
      for (size_t f = 0; f < door.mesh.face_count(); f++)
        if (door.faces.piece[f] == 3) CHECK(blockout::side_class(pieces[2], door.faces.side[f]) == blockout::SideClass::Cut);
      pieces[2] = box_piece(3, {0.6f, 1.0f, 0.9f}, {2.1f, -0.1f, 0}, Op::Subtract);
      const R476Built window = r476_build(pieces, path);
      CHECK(window.ok && r476_sound(window.mesh, r476_name(path)));
      CHECK(r478_faces_of(window, 3) == 4);
      CHECK_NEAR(r476_volume(window.mesh), 2 * 4.4 * 3.4 * 4.4 - 0.2 * 3.4 * 4.4 - 96.0 - 0.2 * 1.0 * 0.9, 3e-3);
    }
  });

  test("blockout rooms: an open-topped room has no ceiling and its walls end flat at the air's top", [] {
    for (Path path : r476_paths()) {
      Piece p = r478_room(1, {4, 3, 4}, {0, 0, 0});
      p.open_top = true;
      const R476Built b = r476_build({p}, path);
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      CHECK_NEAR(r476_volume(b.mesh), 4.4 * 3.2 * 4.4 - 48.0, 2e-3);
      float top = -1e9f;
      for (const Vec3 &q : b.mesh.positions) top = std::max(top, q.y);
      CHECK_NEAR(top, 1.5f, 1e-5f);
      for (size_t f = 0; f < b.mesh.face_count(); f++) CHECK(blockout::side_class(p, b.faces.side[f]) != blockout::SideClass::Ceiling);
    }
  });

  test("blockout: an Intersect piece keeps what is inside it, of every cluster; two keep what is inside both", [] {
    for (Path path : r476_paths()) {
      std::vector<Piece> pieces{r478_room(1, {4, 3, 4}, {0, 0, 0}), r478_room(2, {4, 3, 4}, {4.2f, 0, 0}), box_piece(3, {3, 10, 10}, {2.1f, 0, 0}, Op::Intersect),
                                box_piece(9, {1, 1, 1}, {100, 0, 0})};
      const R476Built one = r476_build(pieces, path);
      CHECK(one.ok && r476_sound(one.mesh, r476_name(path)));
      CHECK_NEAR(r476_volume(one.mesh), 3 * 3.4 * 4.4 - 2 * 1.4 * 12.0, 2e-3);
      CHECK(r478_faces_of(one, 9) == 0);  // (the box far away is outside the Intersect piece: gone)
      size_t cut = 0;
      for (size_t f = 0; f < one.mesh.face_count(); f++) {
        if (one.faces.piece[f] != 3) continue;
        cut++;
        CHECK(one.faces.side[f] == 0 || one.faces.side[f] == 1);
        CHECK(blockout::side_class(pieces[2], one.faces.side[f]) == blockout::SideClass::Cut);
      }
      CHECK(cut >= 2);
      float lo = 1e9f, hi = -1e9f;
      for (const Vec3 &q : one.mesh.positions) lo = std::min(lo, q.x), hi = std::max(hi, q.x);
      CHECK_NEAR(lo, 0.6f, 1e-5f);
      CHECK_NEAR(hi, 3.6f, 1e-5f);
      pieces.push_back(box_piece(4, {10, 2, 10}, {2.1f, 0, 0}, Op::Intersect));
      const R476Built both = r476_build(pieces, path);
      CHECK(both.ok && r476_sound(both.mesh, r476_name(path)));
      CHECK_NEAR(r476_volume(both.mesh), 2 * 1.4 * 0.4 * 2.0 + 0.2 * 2.0 * 4.4, 1e-3);
    }
  });

  test("blockout: an Intersect piece that ends on a room's floor plane takes the floor and leaves no sheet behind", [] {
    for (Path path : r476_paths()) {
      /* The floor slab lies below the plane the Intersect piece starts at: nothing of it is inside. */
      const std::vector<Piece> pieces{r478_room(1, {4, 3, 4}, {0, 0, 0}), box_piece(2, {10, 10, 10}, {0, 3.5f, 0}, Op::Intersect)};
      const R476Built b = r476_build(pieces, path);
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      CHECK_NEAR(r476_volume(b.mesh), 4.4 * 3.2 * 4.4 - 48.0, 2e-3);
      float low = 1e9f;
      for (const Vec3 &q : b.mesh.positions) low = std::min(low, q.y);
      CHECK_NEAR(low, -1.5f, 1e-5f);
      /* ...and one that ends on the ceiling's underside, with a second one beside it. */
      const std::vector<Piece> two{r478_room(1, {4, 3, 4}, {0, 0, 0}), box_piece(2, {10, 10, 10}, {0, -3.5f, 0}, Op::Intersect), box_piece(3, {10, 10, 10}, {3, 0, 0}, Op::Intersect)};
      const R476Built c = r476_build(two, path);
      CHECK(c.ok && r476_sound(c.mesh, r476_name(path)));
      CHECK_NEAR(r476_volume(c.mesh), 4.2 * 3.2 * 4.4 - 4.0 * 3.0 * 4.0, 2e-3);
    }
  });

  test("blockout: an Intersect piece that starts at any of a room's six inside planes leaves no sheet of the slab behind it", [] {
    /* (Manifold kept the slab as a sheet of no thickness when the plane was square to X, and only then.) */
    for (Path path : r476_paths())
      for (int axis = 0; axis < 3; axis++)
        for (float way : {-1.0f, 1.0f}) {
          Vec3 at(0.0f);
          at[axis] = way * (axis == 1 ? 3.5f : 3.0f);  // (a 10 m box whose near side is the air's side)
          const R476Built b = r476_build({r478_room(1, {4, 3, 4}, {0, 0, 0}), box_piece(2, {10, 10, 10}, at, Op::Intersect)}, path);
          const bool sound = b.ok && r476_sound(b.mesh, r476_name(path));
          if (!sound) std::printf("    axis %d, way %+.0f\n", axis, way);
          CHECK(sound);
          CHECK(b.mesh.face_count() == 14);
          CHECK_NEAR(r476_volume(b.mesh), axis == 1 ? 4.4 * 3.2 * 4.4 - 48.0 : 4.2 * 3.4 * 4.4 - 48.0, 2e-3);
        }
  });

  test("blockout rooms: rooms at other floor heights, a floor of no thickness and rooms sharing a wall's plane leave no face of no thickness", [] {
    for (Path path : r476_paths()) {
      for (float lift : {0.1f, 3.2f}) {
        const R476Built b = r476_build({r478_room(1, {4, 3, 4}, {0, 0, 0}), r478_room(2, {4, 3, 4}, {4.2f, lift, 0})}, path);
        CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      }
      Piece bare_floor = r478_room(1, {4, 3, 4}, {0, 0, 0});
      bare_floor.floor = 0.0f;
      const R476Built open = r476_build({bare_floor}, path);
      CHECK(open.ok && r476_sound(open.mesh, r476_name(path)));
      CHECK_NEAR(r476_volume(open.mesh), 4.4 * 3.2 * 4.4 - 48.0, 2e-3);
      /* The second room's air ends where the first one's shell does. */
      const R476Built plane = r476_build({r478_room(1, {4, 3, 4}, {0, 0, 0}), r478_room(2, {4, 3, 4}, {4.0f, 0, 0})}, path);
      CHECK(plane.ok && r476_sound(plane.mesh, r476_name(path)));
      CHECK(r478_corners_inside(plane.mesh, {-2, -1.5f, -2}, {6, 1.5f, 2}) == 0);  // (the airs touch: no wall is left between)
    }
  });

  test("blockout rooms: hostile rooms - no thickness, a wall thicker than the room, a negative or NaN thickness, squashed and mirrored - are closed or say why", [] {
    for (Path path : r476_paths()) {
      Piece bare = r478_room(1, {4, 3, 4}, {0, 0, 0}, false, 0.0f);
      const R476Built none = r476_build({bare}, path);
      CHECK(none.ok && none.mesh.face_count() == 0);
      CHECK(none.report.notes.size() == 1 && none.report.notes[0].id == 1 && none.report.skipped.empty());
      /* ...but its air still clears a Merge room it overlaps. */
      const R476Built clears = r476_build({r478_room(2, {4, 3, 4}, {0, 0, 0}), r478_room(1, {2, 1, 2}, {2, -1, 0}, false, 0.0f)}, path);
      CHECK(clears.ok && r476_sound(clears.mesh, r476_name(path)));
      CHECK_NEAR(r476_volume(clears.mesh), 4.4 * 3.4 * 4.4 - 48.0 - 0.2 * 1.0 * 2.0, 2e-3);

      const R476Built thick = r476_build({r478_room(1, {1, 1, 1}, {0, 0, 0}, false, 5.0f)}, path);
      CHECK(thick.ok && r476_sound(thick.mesh, r476_name(path)));
      CHECK_NEAR(r476_volume(thick.mesh), 11.0 * 11.0 * 11.0 - 1.0, 0.2);

      Piece odd = r478_room(1, {4, 3, 4}, {0, 0, 0});
      odd.wall = -3.0f, odd.floor = std::numeric_limits<float>::quiet_NaN(), odd.ceiling = std::numeric_limits<float>::infinity();
      const R476Built safe = r476_build({odd}, path);
      CHECK(safe.ok && safe.report.skipped.empty() && safe.mesh.face_count() == 0);  // (each read as 0: a room of no thickness)

      Piece squashed = r478_room(1, {4, 3, 4}, {0, 0, 0});
      squashed.to_blockout = Mat4::trs({1, 2, 3}, Quat::euler({0, 30, 0}), {-0.5f, 2.0f, 1.5f});
      const R476Built sq = r476_build({squashed}, path, 0.025f);
      CHECK(sq.ok && r476_sound(sq.mesh, r476_name(path)));
      CHECK(sq.mesh.face_count() == 12);
      /* The wall is 0.2 m in the Blockout's space whatever the scale: the shell is the scaled air plus 0.4 a side. */
      CHECK_NEAR(r476_volume(sq.mesh), (2.0 + 0.4) * (6.0 + 0.4) * (6.0 + 0.4) - 2.0 * 6.0 * 6.0, 5e-3);
    }
  });

  test("blockout rooms: the same rooms give the same bytes, and a build after nothing moved comes from the cache", [] {
    for (Path path : r476_paths()) {
      const std::vector<Piece> pieces{r478_room(1, {4, 3, 4}, {0, 0, 0}), r478_room(2, {4, 3, 4}, {4.2f, 0, 0}), r478_room(3, {2, 2, 2}, {2.1f, -0.3f, 0}, true),
                                      box_piece(4, {0.6f, 2.1f, 0.9f}, {2.1f, -0.45f, 1.4f}, Op::Subtract), box_piece(5, {100, 2.5f, 100}, {0, 0, 0}, Op::Intersect),
                                      r478_room(6, {2, 2, 2}, {40, 0, 0})};
      blockout::Cache cache;
      const R476Built a = r476_build(pieces, path), b = r476_build(pieces, path, 0.1f, &cache), c = r476_build(pieces, path, 0.1f, &cache);
      CHECK(a.ok && b.ok && c.ok && r476_sound(a.mesh, r476_name(path)));
      CHECK(r476_same_mesh(a, b) && r476_same_mesh(a, c));
      CHECK(b.report.built == 2 && c.report.cached == 2 && c.report.built == 0);
      /* A thickness is part of what a cluster was built from. */
      std::vector<Piece> thicker = pieces;
      thicker[5].wall = 0.3f;
      const R476Built d = r476_build(thicker, path, 0.1f, &cache);
      CHECK(d.ok && d.report.built == 1 && d.report.cached == 1);
      CHECK(!r476_same_mesh(a, d));
    }
  });

  test("blockout rooms: the field path gives the crisp path's faces on rooms, Keep rooms, doors and an Intersect piece", [] {
    if (!blockout::crisp_available()) return;
    const std::vector<std::vector<Piece>> cases{
        {r478_room(1, {4, 3, 4}, {0, 0, 0})},
        {r478_room(1, {4, 3, 4}, {0, 0, 0}), r478_room(2, {4, 3, 4}, {3, 0, 0})},
        {r478_room(1, {4, 3, 4}, {0, 0, 0}), r478_room(2, {4, 3, 4}, {4.2f, 0, 0}), box_piece(3, {0.6f, 2.1f, 0.9f}, {2.1f, -0.45f, 0}, Op::Subtract)},
        {r478_room(1, {4, 3, 4}, {0, 0, 0}), r478_room(2, {4, 3, 4}, {4.2f, 0, 0}), r478_room(3, {2, 2, 2}, {2.1f, -0.3f, 0}, true)},
        {r478_room(1, {4, 3, 4}, {0, 0, 0}), r478_room(2, {4, 3, 4}, {4.2f, 0, 0}), box_piece(3, {3, 10, 10}, {2.1f, 0, 0}, Op::Intersect)},
        {r478_room(1, {4, 3, 4}, {0, 0, 0}), box_piece(2, {0.5f, 3.0f, 0.5f}, {0.5f, 0, 0.25f})},
    };
    for (size_t i = 0; i < cases.size(); i++) {
      const R476Built crisp = r476_build(cases[i], Path::Crisp), field = r476_build(cases[i], Path::Field, 0.05f);
      const bool same = crisp.ok && field.ok && r476_same_shape(crisp, field);
      if (!same) std::printf("    case %zu: crisp %zu faces / %zu points, field %zu / %zu\n", i, crisp.mesh.face_count(), crisp.mesh.vert_count(), field.mesh.face_count(), field.mesh.vert_count());
      CHECK(same);
    }
  });
}

/* ------------------------------------------------------------------ round 478: the object, the editor, the console */
namespace {

/* What a component's reflect() says: each field's name, whether it was greyed, and the help given for it. */
struct R478Probe : Reflector {
  struct Seen {
    std::string name, help;
    bool greyed = false;
  };
  std::vector<Seen> seen;
  bool grey = false;
  void note(const char *n) { seen.push_back({n, "", grey}); }
  void field(const char *n, float &, float, float, float) override { note(n); }
  void field(const char *n, int &, int, int) override { note(n); }
  void field(const char *n, bool &) override { note(n); }
  void field(const char *n, Vec3 &) override { note(n); }
  void color(const char *n, Vec3 &) override { note(n); }
  void enumeration(const char *n, int &, const char *const *, int) override { note(n); }
  void text(const char *n, std::string &) override { note(n); }
  void mesh(const char *, MeshPtr &) override {}
  void help(const char *t) override {
    if (!seen.empty()) seen.back().help = t;
  }
  void greyed(bool on) override { grey = on; }
  const Seen *find(const char *name) const {
    for (const Seen &s : seen)
      if (s.name == name) return &s;
    return nullptr;
  }
};

/* A Blockout at the world origin in an empty scene, selected, with the camera on it. */
GameObject *r478_scene(Editor &ed, const char *camera = "camera 30 20 12 0 0 0") {
  r477_empty_scene(ed);
  ed.command(camera);
  GameObject *bo = r477_blockout(ed);
  ed.select_for_test(bo->id, false);
  return bo;
}

/* A room as the console makes it, selected afterwards: the first piece. */
GameObject *r478_room_by_command(Editor &ed, GameObject &bo, const char *line = "blockout add room box 4 3 4") {
  ed.select_for_test(bo.id, false);
  ed.command(line);
  r477_settle(ed, 2);
  return ed.active_object_for_test();
}

size_t r478_faces_named(const Blockout &b, uint64_t id) {
  size_t n = 0;
  for (uint64_t p : b.state.faces.piece) n += p == id;
  return n;
}

Vec3 r478_forward(const GameObject &g) { return g.local().rotation.rotate({0.0f, 0.0f, 1.0f}); }

}  // namespace

static void tests_round478_object() {
  /* ---- The component and the Inspector ---- */
  test("blockout rooms: a room's Wall, Floor, Ceiling, Walls and Open Top show only for a room, its Operation is greyed with the reason in its help, an opening's Width, Head and Sill only for an opening", [] {
    BlockoutPiece plain;
    R478Probe a;
    plain.reflect(a);
    CHECK(a.find("Room") && a.find("Opening") && a.find("Operation") && !a.find("Wall") && !a.find("Floor") && !a.find("Ceiling") && !a.find("Walls") && !a.find("Open Top") &&
          !a.find("Width") && !a.find("Head") && !a.find("Sill") && !a.find("Depth"));
    CHECK(a.find("Operation") && !a.find("Operation")->greyed && a.find("Size"));
    BlockoutPiece room;
    room.room = true;
    R478Probe b;
    room.reflect(b);
    CHECK(b.find("Wall") && b.find("Floor") && b.find("Ceiling") && b.find("Walls") && b.find("Open Top") && b.find("Room") && !b.find("Opening") && !b.find("Width"));
    const R478Probe::Seen *op = b.find("Operation");
    CHECK(op && op->greyed && op->help.find("ignored") != std::string::npos);
    CHECK(b.find("Shape") && !b.find("Shape")->greyed && b.find("Wall") && !b.find("Wall")->greyed);  // (only the Operation is greyed)
    CHECK(b.find("Walls") && b.find("Walls")->help.find("Merge with neighbours") != std::string::npos);
    BlockoutPiece door;
    door.opening = 1;
    R478Probe c;
    door.reflect(c);
    CHECK(c.find("Width") && c.find("Head") && c.find("Sill") && c.find("Depth") && c.find("Opening") && !c.find("Room") && !c.find("Wall") && !c.find("Operation") && !c.find("Size") &&
          !c.find("Shape"));
    /* Files, undo and `set` still see every field. */
    struct All : R478Probe {
      bool all_fields() const override { return true; }
    } all;
    plain.reflect(all);
    for (const char *n : {"Room", "Wall", "Floor", "Ceiling", "Walls", "Open Top", "Opening", "Width", "Head", "Sill", "Depth", "Operation", "Size"}) CHECK(all.find(n));
    Blockout bo;
    R478Probe d;
    bo.reflect(d);
    CHECK(d.find("Default Wall Thickness") && d.find("Default Wall Thickness")->help.find("new room") != std::string::npos);
  });

  test("blockout rooms: the Inspector draws a piece's room fields only once it is a room, an opening's only for a door, and the status line names a room of no thickness", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.record_buttons_for_test(true);
    ed.command("blockout new");
    GameObject *bo = r477_find(ed);
    GameObject *box = r477_find(ed, "Box");
    auto has = [&](const std::string &key) {
      for (const auto &kv : ed.ui_for_test().button_rects)
        if (kv.first.rfind(key, 0) == 0) return true;
      return false;
    };
    auto look = [&](GameObject *g) {
      ed.select_for_test(g->id, false);
      ed.record_buttons_for_test(true);
      r477_settle(ed, 3);
    };
    look(bo);
    CHECK(has("field:Default Wall Thickness"));
    look(box);
    CHECK(has("field:Room") && !has("field:Wall") && !has("field:Floor") && !has("field:Ceiling") && !has("field:Open Top") && !has("field:Width") && !has("greyed:Operation"));
    BlockoutPiece *bp = box->get<BlockoutPiece>();
    bp->room = true;
    look(box);
    CHECK(has("field:Wall") && has("field:Floor") && has("field:Ceiling") && has("field:Open Top") && has("greyed:Operation") && !has("field:Width"));
    bp->room = false, bp->opening = 1;
    look(box);
    CHECK(has("field:Width") && has("field:Head") && has("field:Sill") && has("field:Depth") && !has("field:Room") && !has("field:Wall") && !has("greyed:Operation"));
    bp->opening = 0, bp->room = true, bp->wall = bp->floor = bp->ceiling = 0.0f;
    look(bo);
    r477_settle(ed, 3);
    CHECK(has("shown:'Box': the room has no wall, floor or ceiling thickness"));
  });

  test("blockout rooms: 'set BlockoutPiece.Wall' and a clone carry the room fields, and the mesh follows", [] {
    Editor ed;
    ed.init_headless(900, 600);
    r477_empty_scene(ed);
    ed.clear_selection_for_test();
    ed.command("blockout add room box 4 3 4");
    GameObject *room = ed.active_object_for_test();
    CHECK(room && room->get<BlockoutPiece>() && room->get<BlockoutPiece>()->room);
    if (!room) return;
    const Mesh thin = *r477_mesh(r477_find(ed));
    ed.command("set BlockoutPiece.Wall 0.4");
    ed.command("set BlockoutPiece.Walls 1");
    ed.command("set BlockoutPiece.OpenTop 1");
    const BlockoutPiece *bp = room->get<BlockoutPiece>();
    CHECK(bp->wall == 0.4f && bp->walls == 1 && bp->open_top);
    r477_settle(ed, 2);
    CHECK(r477_matches(ed) && !r477_same(thin, *r477_mesh(r477_find(ed))));
    auto copy = room->get<BlockoutPiece>()->clone();
    const BlockoutPiece *c = static_cast<BlockoutPiece *>(copy.get());
    CHECK(c->room && c->wall == 0.4f && c->walls == 1 && c->open_top && c->floor == 0.2f);
  });

  /* ---- The console ---- */
  test("blockout rooms: 'blockout add room' makes a room with the Blockout's Default Wall Thickness all round, one undo step, closed; 'room cylinder' a round one", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.command("blockout new");
    GameObject *bo = r477_find(ed);
    ed.select_for_test(bo->id, false);
    ed.command("set Blockout.DefaultWallThickness 0.3");
    CHECK(bo->get<Blockout>()->default_wall == 0.3f);
    const size_t depth = ed.undo_depth_for_test();
    ed.command("blockout add room");
    r477_settle(ed, 2);
    const BlockoutPiece *room = r477_active_piece(ed);
    CHECK(room && room->room && room->shape == 0 && room->size.x == 4.0f && room->size.y == 3.0f && room->size.z == 4.0f);
    CHECK(room && room->wall == 0.3f && room->floor == 0.3f && room->ceiling == 0.3f && room->walls == 0 && !room->open_top);
    CHECK(ed.undo_depth_for_test() == depth + 1 && ed.active_object_for_test()->parent == bo);
    ed.command("blockout add room cylinder 1.5 2.5");
    r477_settle(ed, 2);
    const BlockoutPiece *round = r477_active_piece(ed);
    CHECK(round && round->room && round->shape == 1 && round->radius == 1.5f && round->height == 2.5f && round->wall == 0.3f);
    CHECK(r477_matches(ed) && r477_count(ed, true) == 3);
    ed.command("undo");
    r477_settle(ed, 2);
    CHECK(r477_count(ed, true) == 2);
    /* Alone in a new Blockout: one room is two shells - 12 faces. */
    r477_empty_scene(ed);
    ed.command("blockout add room box 4 3 4");
    const Mesh *m = r477_mesh(r477_find(ed));
    CHECK(m && m->face_count() == 12 && r476_sound(*m, "one room"));
    r477_empty_scene(ed);
    ed.command("blockout add room cylinder 2 3");
    const Mesh *cm = r477_mesh(r477_find(ed));
    CHECK(cm && cm->face_count() >= 40 && r476_sound(*cm, "a round room"));
    const size_t mark = r477_mark();
    ed.command("blockout add room sphere");  // (no sphere rooms yet: refused, said)
    CHECK(r477_count(ed, true) == 1 && te_warnings_since(mark) >= 1);
  });

  test("blockout rooms: 'blockout add box intersect' keeps only what is inside it, and 'blockout set wall|floor|ceiling|walls|opentop' change the selected rooms", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.command("blockout new");
    ed.command("blockout add box intersect 1 10 10");
    const BlockoutPiece *cut = r477_active_piece(ed);
    CHECK(cut && cut->operation == 2 && cut->shape == 0 && !cut->room && cut->size.x == 1.0f);
    CHECK(r477_matches(ed));
    CHECK_NEAR(r476_volume(*r477_mesh(r477_find(ed))), 4.0, 0.2);  // (the 2 m box cut to a metre of it)
    ed.command("blockout add intersect cylinder 1 3");
    CHECK(r477_active_piece(ed) && r477_active_piece(ed)->operation == 2 && r477_active_piece(ed)->shape == 1);
    r477_empty_scene(ed);
    ed.command("blockout add room");
    GameObject *room = ed.active_object_for_test();
    const BlockoutPiece *bp = room ? room->get<BlockoutPiece>() : nullptr;
    CHECK(bp != nullptr);
    if (!bp) return;
    ed.command("blockout set wall 0.35");
    ed.command("blockout set floor 0.1");
    ed.command("blockout set ceiling 0.4");
    CHECK(bp->wall == 0.35f && bp->floor == 0.1f && bp->ceiling == 0.4f);
    ed.command("blockout set walls keep");
    CHECK(bp->walls == 1);
    ed.command("blockout set walls merge");
    CHECK(bp->walls == 0);
    ed.command("blockout set opentop on");
    CHECK(bp->open_top);
    r477_settle(ed, 2);
    CHECK(r477_matches(ed));
    ed.command("blockout set opentop off");
    CHECK(!bp->open_top);
    const size_t mark = r477_mark();
    ed.command("blockout set wall abc");
    ed.command("blockout set walls maybe");
    ed.command("blockout set opentop sideways");
    CHECK(te_warnings_since(mark) == 3 && bp->wall == 0.35f && bp->walls == 0 && !bp->open_top);
    /* Two selected rooms change together; a piece that is not a room is left alone, and with no room selected it says so. */
    ed.command("blockout add room");
    GameObject *second = ed.active_object_for_test();
    ed.select_for_test(room->id, false);
    ed.select_for_test(second->id, true);
    ed.command("blockout set wall 0.6");
    CHECK(room->get<BlockoutPiece>()->wall == 0.6f && second->get<BlockoutPiece>()->wall == 0.6f);
    ed.select_for_test(r477_find(ed)->id, false);
    const size_t mark2 = r477_mark();
    ed.command("blockout set wall 0.1");
    CHECK(te_warnings_since(mark2) == 1 && room->get<BlockoutPiece>()->wall == 0.6f);
    const size_t mark3 = r477_mark();
    ed.command("help blockout");
    const std::string said = r477_log_since(mark3);
    for (const char *verb : {"add room", "add door|window", "intersect", "wall|floor|ceiling", "opentop"}) CHECK(said.find(verb) != std::string::npos);
  });

  /* ---- Doors and windows ---- */
  test("blockout rooms: 'blockout add door' puts a door in the middle of the selected room's first wall, turned to it and standing on the floor, and the wall is cut", [] {
    Editor ed;
    ed.init_headless(900, 600);
    r477_empty_scene(ed);
    GameObject *bo = r477_blockout(ed);
    bo->set_world_position({0.0f, 0.0f, 0.0f});
    GameObject *room = r478_room_by_command(ed, *bo);
    CHECK(room && room->get<BlockoutPiece>() && room->get<BlockoutPiece>()->room);
    if (!room) return;
    room->set_local_position({0.0f, 0.0f, 0.0f});
    r477_settle(ed, 2);
    const double closed = r476_volume(*r477_mesh(bo));
    const size_t depth = ed.undo_depth_for_test();
    ed.command("blockout add door");
    r477_settle(ed, 2);
    GameObject *door = ed.active_object_for_test();
    const BlockoutPiece *dp = door ? door->get<BlockoutPiece>() : nullptr;
    CHECK(dp && dp->opening == 1 && !dp->room && dp->width == 0.9f && dp->head == 2.1f && dp->sill == 0.0f && door->parent == bo);
    CHECK(ed.undo_depth_for_test() == depth + 1);
    if (!dp) return;
    /* The first wall is the air's -X side: the wall's middle at x = -2.1, the door's feet on the floor (y = -1.5), turned to look through the wall along X. */
    const Vec3 at = door->local().position, fwd = r478_forward(*door);
    CHECK_NEAR(at.x, -2.1f, 1e-4f);
    CHECK_NEAR(at.y, -1.5f, 1e-4f);
    CHECK_NEAR(at.z, 0.0f, 1e-4f);
    CHECK_NEAR(std::fabs(fwd.x), 1.0f, 1e-4f);
    CHECK_NEAR(dp->depth, 0.4f, 1e-5f);  // (the wall's 0.2 m and 0.1 m either side)
    r477_settle(ed, 2);
    CHECK(r477_matches(ed));
    const Blockout &b = *bo->get<Blockout>();
    CHECK(r478_faces_named(b, door->id) >= 3);
    const Mesh &m = *r477_mesh(bo);
    CHECK(r476_sound(m, "a room with a door"));
    CHECK_NEAR(r476_volume(m), closed - 0.2 * 0.9 * 2.1, 0.12);
    for (size_t f = 0; f < b.state.faces.piece.size(); f++)
      if (b.state.faces.piece[f] == door->id) CHECK(blockout::side_class(door->get<BlockoutPiece>()->to_piece(door->id, door->local_matrix()), b.state.faces.side[f]) == blockout::SideClass::Cut);
    /* Moving it afterwards is moving any piece: it follows the origin, which is the floor line. */
    door->set_local_position(door->local().position + Vec3(0.0f, 0.0f, 1.0f));
    r477_settle(ed, 3);
    CHECK(r477_matches(ed) && r478_faces_named(b, door->id) >= 3);
    /* Changing the sill moves the cut up from the floor line without moving the object. */
    const Vec3 stayed = door->get<BlockoutPiece>() ? door->local().position : Vec3();
    door->get<BlockoutPiece>()->sill = 0.9f;
    r477_settle(ed, 3);
    CHECK(door->local().position == stayed && r477_matches(ed));
    CHECK(r478_faces_named(b, door->id) >= 4);  // (now a window: a sill, two jambs and a head)
  });

  test("blockout rooms: 'blockout add window' is 1.2 m wide from a sill at 0.9 m to a head at 2 m, with the numbers it was given taking over; with no room to put it in it says so and makes nothing", [] {
    Editor ed;
    ed.init_headless(900, 600);
    r477_empty_scene(ed);
    GameObject *bo = r477_blockout(ed);
    bo->set_world_position({0.0f, 0.0f, 0.0f});
    GameObject *room = r478_room_by_command(ed, *bo);
    CHECK(room);
    if (!room) return;
    room->set_local_position({0.0f, 0.0f, 0.0f});
    r477_settle(ed, 2);
    ed.command("blockout add window");
    GameObject *win = ed.active_object_for_test();
    const BlockoutPiece *wp = win ? win->get<BlockoutPiece>() : nullptr;
    CHECK(wp && wp->opening == 2 && wp->width == 1.2f && wp->head == 2.0f && wp->sill == 0.9f);
    CHECK(win && std::fabs(win->local().position.y + 1.5f) < 1e-4f);
    r477_settle(ed, 2);
    CHECK(r477_matches(ed) && r478_faces_named(*bo->get<Blockout>(), win->id) >= 4);
    ed.command("blockout add window 2 1.6 0.5");
    wp = r477_active_piece(ed);
    CHECK(wp && wp->width == 2.0f && wp->head == 1.6f && wp->sill == 0.5f);
    ed.command("blockout add door 1 2.3");
    wp = r477_active_piece(ed);
    CHECK(wp && wp->opening == 1 && wp->width == 1.0f && wp->head == 2.3f && wp->sill == 0.0f);
    /* No room: a Blockout of boxes alone. */
    Editor ed2;
    ed2.init_headless(900, 600);
    ed2.command("blockout new");
    const size_t pieces = r477_count(ed2, true), depth = ed2.undo_depth_for_test();
    const size_t mark = r477_mark();
    ed2.command("blockout add door");
    ed2.command("blockout add window");
    CHECK(r477_count(ed2, true) == pieces && ed2.undo_depth_for_test() == depth && te_warnings_since(mark) == 2);
    ed2.clear_selection_for_test();
    const size_t mark2 = r477_mark();
    ed2.command("blockout add door");  // (nothing selected: there is no Blockout to put one in)
    CHECK(r477_count(ed2, true) == pieces && te_warnings_since(mark2) == 1);
  });

  test("blockout rooms: with the mouse over a room's wall in the Scene view a door goes on that wall - turned to it, at the spot, on the floor - and with the mouse elsewhere on the first wall", [] {
    Editor ed;
    te_view(ed, "camera 30 20 12 0 0 0");
    GameObject *bo = r477_blockout(ed);
    bo->set_world_position({0.0f, 0.0f, 0.0f});
    GameObject *room = r478_room_by_command(ed, *bo);
    CHECK(room);
    if (!room) return;
    room->set_local_position({0.0f, 0.0f, 0.0f});
    r477_settle(ed, 3);
    /* A wall the camera sees from outside (the first wall, -X, is not it). */
    const Vec2 centre{(float)ed.scene_view_rect().w * 0.5f, (float)ed.scene_view_rect().h * 0.5f};
    const Ray eye = ed.screen_ray_for_test(centre);
    struct Wall {
      Vec3 normal, at;
    };
    const Wall walls[] = {{{0, 0, 1}, {0.7f, 0.2f, 2.2f}}, {{0, 0, -1}, {0.7f, 0.2f, -2.2f}}, {{1, 0, 0}, {2.2f, 0.2f, 0.7f}}};
    const Wall *seen = nullptr;
    for (const Wall &w : walls)
      if (!seen && dot(w.normal, eye.origin - w.at) > 1.0f) seen = &w;
    CHECK(seen != nullptr);
    if (!seen) return;
    int px = 0, py = 0;
    CHECK(ed.project_to_window(seen->at, px, py));
    ed.step_frame_headless({r477_ev(R477ET::MouseMove, px, py)});
    ed.step_frame_headless({r477_ev(R477ET::MouseMove, px, py)});
    ed.command("blockout add door");
    GameObject *door = ed.active_object_for_test();
    CHECK(door && door->get<BlockoutPiece>() && door->get<BlockoutPiece>()->opening == 1);
    if (!door || !door->get<BlockoutPiece>()) return;
    const Vec3 at = door->local().position, fwd = r478_forward(*door);
    CHECK(std::fabs(dot(fwd, seen->normal)) > 0.99f);                                          // (looking through that wall)
    const float along = seen->normal.z != 0.0f ? at.z : at.x;                                  // (in the wall's middle: the wall's face is at 2.2, its middle at 2.1)
    CHECK_NEAR(std::fabs(along), 2.1f, 0.03f);
    const float across = seen->normal.z != 0.0f ? at.x : at.z;                                 // (where the mouse was)
    CHECK_NEAR(across, 0.7f, 0.15f);
    CHECK_NEAR(at.y, -1.5f, 1e-3f);
    r477_settle(ed, 2);
    CHECK(r477_matches(ed) && r478_faces_named(*bo->get<Blockout>(), door->id) >= 3);
    /* With the mouse parked over the Hierarchy the same command takes the selected room's first wall. */
    ed.select_for_test(room->id, false);
    const Recti h = ed.window_rect_for_test(WindowKind::Hierarchy);
    ed.step_frame_headless({r477_ev(R477ET::MouseMove, h.x + h.w / 2, h.y + h.h - 10)});
    ed.step_frame_headless();
    ed.command("blockout add door");
    GameObject *second = ed.active_object_for_test();
    CHECK(second && second != door && std::fabs(second->local().position.x + 2.1f) < 1e-3f);
    /* Over the roof, no wall is under the mouse: the first wall again. */
    int fx = 0, fy = 0;
    CHECK(ed.project_to_window({0.5f, 1.7f, 0.5f}, fx, fy));  // (the room's top, seen from above: a ceiling is no wall)
    ed.select_for_test(room->id, false);
    ed.step_frame_headless({r477_ev(R477ET::MouseMove, fx, fy)});
    ed.step_frame_headless({r477_ev(R477ET::MouseMove, fx, fy)});
    ed.command("blockout add window");
    GameObject *third = ed.active_object_for_test();
    CHECK(third && third != second && third->get<BlockoutPiece>() && third->get<BlockoutPiece>()->opening == 2 && std::fabs(third->local().position.x + 2.1f) < 1e-3f);
  });

  test("blockout rooms: a door put in a turned and scaled room is turned with it and stands on its floor", [] {
    Editor ed;
    ed.init_headless(900, 600);
    r477_empty_scene(ed);
    GameObject *bo = r477_blockout(ed);
    bo->set_world_position({0.0f, 0.0f, 0.0f});
    GameObject *room = r478_room_by_command(ed, *bo);
    CHECK(room);
    if (!room) return;
    room->set_local_position({1.0f, 0.5f, -2.0f});
    room->set_local_rotation(Quat::euler({0.0f, 90.0f, 0.0f}));
    room->set_local_scale({1.0f, 2.0f, 1.0f});
    r477_settle(ed, 2);
    ed.select_for_test(room->id, false);
    ed.command("blockout add door");
    GameObject *door = ed.active_object_for_test();
    CHECK(door && door->get<BlockoutPiece>() && door->get<BlockoutPiece>()->opening == 1);
    if (!door) return;
    /* The air is 4 x 6 x 4 now (Y scaled): its floor is at 0.5 - 3 = -2.5; the first wall, the room's -X, faces along Z after the turn. */
    const Vec3 at = door->local().position, fwd = r478_forward(*door);
    CHECK_NEAR(at.y, -2.5f, 1e-3f);
    CHECK(std::fabs(fwd.z) > 0.99f);  // (the room's X now runs along Z)
    r477_settle(ed, 2);
    CHECK(r477_matches(ed) && r478_faces_named(*bo->get<Blockout>(), door->id) >= 3);
  });

  /* ---- The menus ---- */
  test("blockout: GameObject > Blockout offers Room (Box), Room (Cylinder), Door and Window, each one undo step", [] {
    Editor ed;
    ed.init_headless(1500, 1000);
    ed.record_buttons_for_test(true);
    r477_settle(ed, 3);
    CHECK(r477_menu(ed, {"menubar:GameObject", "submenu:Blockout", "menu:New Blockout"}));
    r477_settle(ed);
    struct Row {
      const char *label;
      bool room;
      int shape, opening;
    };
    for (const Row &row : {Row{"Room (Box)", true, 0, 0}, Row{"Room (Cylinder)", true, 1, 0}, Row{"Door", false, 0, 1}, Row{"Window", false, 0, 2}}) {
      const size_t pieces = r477_count(ed, true), depth = ed.undo_depth_for_test();
      CHECK(r477_menu(ed, {"menubar:GameObject", "submenu:Blockout", std::string("menu:") + row.label}));
      r477_settle(ed);
      const BlockoutPiece *p = r477_active_piece(ed);
      const bool ok = p && p->room == row.room && p->shape == row.shape && p->opening == row.opening && r477_count(ed, true) == pieces + 1 && ed.undo_depth_for_test() == depth + 1;
      if (!ok) std::printf("    GameObject > Blockout > %s: not as asked\n", row.label);
      CHECK(ok);
    }
    CHECK(r477_count(ed, false) == 1 && r477_matches(ed));
  });

  test("blockout: the Hierarchy's + menu and the Modeling Tools window offer the room entries too", [] {
    Editor ed;
    ed.init_headless(1500, 1000);
    ed.record_buttons_for_test(true);
    ed.clear_selection_for_test();
    r477_settle(ed, 3);
    const Recti h = ed.window_rect_for_test(WindowKind::Hierarchy);
    const ui::Context &u = ed.ui_for_test();
    const int bh = u.row_h() + u.px(6), hh = bh - u.px(8);
    const int cx = h.x + u.px(5) + (hh + u.px(16)) / 2, cy = h.y + u.px(4) + hh / 2;
    r477_click(ed, cx, cy);
    r477_settle(ed);
    CHECK(r477_menu(ed, {"submenu:Blockout", "menu:Room (Cylinder)"}));
    r477_settle(ed);
    const BlockoutPiece *p = r477_active_piece(ed);
    CHECK(p && p->room && p->shape == 1 && r477_count(ed, false) == 1);
    Editor tools;
    tools.init_headless(1500, 1000);
    tools.command("layout Default");
    tools.command("window Modeling Tools");
    tools.command("alltools on");
    tools.record_buttons_for_test(true);
    r477_settle(tools, 4);
    for (const char *label : {"Room (Box)", "Door"}) {
      tools.record_buttons_for_test(true);
      r477_settle(tools, 2);
      Recti r;
      CHECK(tools.button_rect_for_test(label, r));
      if (!tools.button_rect_for_test(label, r)) continue;
      r477_click(tools, r.x + r.w / 2, r.y + r.h / 2);
      r477_settle(tools);
      if (std::string(label) == "Room (Box)") {
        const BlockoutPiece *room = r477_active_piece(tools);
        CHECK(room && room->room && r477_count(tools, false) == 1);
      }
    }
    const BlockoutPiece *door = r477_active_piece(tools);
    CHECK(door && door->opening == 1 && r477_count(tools, true) == 2);
  });

  /* ---- Scenes ---- */
  test("blockout rooms: a scene saved with round 477's fields only opens unchanged - the room fields at their defaults, the mesh the same", [] {
    Editor ed;
    ed.init_headless(900, 600);
    r477_empty_scene(ed);
    GameObject *bo = r477_blockout(ed);
    r477_piece(ed, *bo, "Wall", 0, 0, {0, 0, 0}, {3, 2, 3});
    r477_piece(ed, *bo, "Pocket", 1, 1, {0.2f, 0.5f, 0.0f}, Vec3(1.0f), 0.5f, 3.0f);
    r477_piece(ed, *bo, "Ball", 2, 0, {1.0f, 1.0f, 0.0f}, Vec3(1.0f), 0.6f);
    r477_settle(ed, 3);
    ed.command("blockout wait");
    const Mesh before = *r477_mesh(bo);
    std::string text = save_scene_text(ed.scene());
    CHECK(text.find("Open_Top") != std::string::npos);  // (this build writes them...)
    std::istringstream is(text);
    std::string old;
    for (std::string line; std::getline(is, line);) {
      bool added = false;  // ...and round 477 did not: left out, as its files lack them
      for (const char *key : {"Room ", "Wall ", "Floor ", "Ceiling ", "Walls ", "Open_Top ", "Opening ", "Width ", "Head ", "Sill ", "Depth ", "Default_Wall_Thickness "})
        added = added || line.rfind(std::string("  ") + key, 0) == 0;
      if (!added) old += line + "\n";
    }
    CHECK(old.size() < text.size() && old.find("Open_Top") == std::string::npos);
    const std::string path = te_scene_path(ed, "r478_old.scene");
    CHECK(fs::write_file(path, old));
    ed.command("newscene");
    CHECK(ed.open_scene_for_test(path));
    for (int i = 0; i < 400 && !r477_find(ed); i++) ed.step_frame_headless();
    r477_settle(ed, 4);
    ed.command("blockout wait");
    GameObject *loaded = r477_find(ed);
    CHECK(loaded && r477_all_pieces(*loaded).size() == 3);
    for (GameObject *p : r477_all_pieces(*loaded)) {
      const BlockoutPiece *bp = p->get<BlockoutPiece>();
      CHECK(!bp->room && bp->opening == 0 && bp->wall == 0.2f && bp->floor == 0.2f && bp->ceiling == 0.2f && bp->walls == 0 && !bp->open_top && bp->width == 0.9f);
    }
    CHECK(loaded && loaded->get<Blockout>()->default_wall == 0.2f);
    CHECK(loaded && r477_mesh(loaded) && r477_same(before, *r477_mesh(loaded)) && te_matches(loaded));
    fs::remove_file(path);
  });

  test("blockout rooms: a scene with rooms, a door, an Intersect piece and a Default Wall Thickness saves and opens with every field and the same mesh", [] {
    Editor ed;
    ed.init_headless(900, 600);
    r477_empty_scene(ed);
    GameObject *bo = r477_blockout(ed);
    bo->get<Blockout>()->default_wall = 0.25f;
    GameObject *a = r477_piece(ed, *bo, "Hall", 0, 0, {0, 0, 0}, {4, 3, 4});
    BlockoutPiece *ap = a->get<BlockoutPiece>();
    ap->room = true, ap->wall = 0.3f, ap->floor = 0.15f, ap->ceiling = 0.35f, ap->walls = 1, ap->open_top = true;
    GameObject *d = r477_piece(ed, *bo, "Entry", 0, 0, {-2.15f, -1.5f, 0}, Vec3(1.0f));
    BlockoutPiece *dp = d->get<BlockoutPiece>();
    dp->opening = 2, dp->width = 1.1f, dp->head = 1.9f, dp->sill = 0.8f, dp->depth = 0.45f;
    GameObject *i = r477_piece(ed, *bo, "Clip", 0, 2, {0, 0, 0}, {10, 2.5f, 10});
    (void)i;
    r477_settle(ed, 3);
    ed.command("blockout wait");
    const Mesh before = *r477_mesh(bo);
    const std::string path = te_scene_path(ed, "r478_rooms.scene");
    ed.command("save " + path);
    for (int k = 0; k < 400 && (!fs::exists(path) || ed.save_job_running_for_test()); k++) ed.step_frame_headless();
    r477_settle(ed, 3);
    ed.command("newscene");
    CHECK(ed.open_scene_for_test(path));
    for (int k = 0; k < 400 && !r477_find(ed); k++) ed.step_frame_headless();
    r477_settle(ed, 4);
    ed.command("blockout wait");
    GameObject *loaded = r477_find(ed);
    CHECK(loaded && loaded->get<Blockout>()->default_wall == 0.25f);
    const GameObject *hall = r477_find(ed, "Hall"), *entry = r477_find(ed, "Entry"), *clip = r477_find(ed, "Clip");
    CHECK(hall && entry && clip);
    if (!hall || !entry || !clip) return;
    const BlockoutPiece *h = hall->get<BlockoutPiece>(), *e = entry->get<BlockoutPiece>();
    CHECK(h->room && h->wall == 0.3f && h->floor == 0.15f && h->ceiling == 0.35f && h->walls == 1 && h->open_top);
    CHECK(e->opening == 2 && e->width == 1.1f && e->head == 1.9f && e->sill == 0.8f && e->depth == 0.45f);
    CHECK(clip->get<BlockoutPiece>()->operation == 2);
    CHECK(r477_mesh(loaded) && r477_same(before, *r477_mesh(loaded)) && te_matches(loaded));
    fs::remove_file(path);
  });

  /* ---- The wires ---- */
  test("blockout rooms: a selected room draws its air and, fainter, its shell; an Intersect piece draws in a violet of its own", [] {
    Editor ed;
    r34_open(ed);
    r477_empty_scene(ed);
    GameObject *bo = r477_blockout(ed);
    bo->set_world_position({0.0f, 0.0f, 0.0f});
    bo->get<MeshRenderer>()->enabled = false;  // (the wires alone)
    GameObject *room = r477_piece(ed, *bo, "Room", 0, 0, {0, 0, 0}, {2, 1.5f, 2});
    room->get<BlockoutPiece>()->room = true;
    GameObject *clip = r477_piece(ed, *bo, "Clip", 0, 2, {0, 0, 0}, {6, 6, 6});
    ed.command("camera 25 25 9 0 0 0");
    auto shot = [&](GameObject *sel) {
      ed.clear_selection_for_test();  // (fields were set by hand: a picture of nothing selected first makes the views draw again)
      ed.mark_changed_for_test("wires");
      r34_steps(ed, 2);
      if (sel) ed.select_for_test(sel->id, false);
      r34_steps(ed, 4);
      return r34_crop(ed, ed.scene_view_rect());
    };
    auto differing = [](const std::vector<uint32_t> &a, const std::vector<uint32_t> &b) {
      size_t n = 0;
      for (size_t i = 0; i < a.size() && i < b.size(); i++) n += a[i] != b[i];
      return n;
    };
    const std::vector<uint32_t> none = shot(nullptr);
    room->get<BlockoutPiece>()->wall = room->get<BlockoutPiece>()->floor = room->get<BlockoutPiece>()->ceiling = 0.0f;
    const std::vector<uint32_t> bare = shot(room);  // (no thickness: the shell is the air - one wire)
    room->get<BlockoutPiece>()->wall = room->get<BlockoutPiece>()->floor = room->get<BlockoutPiece>()->ceiling = 0.5f;
    const std::vector<uint32_t> thick = shot(room);
    const size_t air = differing(none, bare), shell = differing(bare, thick);
    std::printf("    room wires: air %zu pixels, the shell beyond it %zu\n", air, shell);
    CHECK(air > 100 && shell > 100);
    CHECK(r477_near(thick, 0xF7941D, 14) > 20);  // (the air in the selection colour, at full strength)
    room->get<BlockoutPiece>()->open_top = true;
    const std::vector<uint32_t> open = shot(room);
    CHECK(differing(thick, open) > 20);  // (no ceiling: the shell's top edges go)
    const size_t violet_none = r477_near(shot(nullptr), 0xB06CF5, 14), violet_clip = r477_near(shot(clip), 0xB06CF5, 14);
    CHECK(violet_none == 0 && violet_clip > 20 && r477_near(shot(clip), 0xF4BC02, 14) == 0);
  });

  test("blockout rooms: a room's shell wire reaches past its air by the wall, the floor and the ceiling as the kernel grows it", [] {
    Piece p;
    p.room = true, p.size = {4, 3, 4}, p.wall = 0.2f, p.floor = 0.3f, p.ceiling = 0.4f;
    p.to_blockout = Mat4::identity();
    RoomGrowth g;
    CHECK(blockout_room_growth(p, g));
    CHECK_NEAR(g.x, 0.2, 1e-6);
    CHECK_NEAR(g.z, 0.2, 1e-6);
    CHECK_NEAR(g.down, 0.3, 1e-6);
    CHECK_NEAR(g.up, 0.4, 1e-6);
    p.open_top = true;
    CHECK(blockout_room_growth(p, g) && g.up == 0.0);
    p.open_top = false;
    p.to_blockout = Mat4::trs({0, 0, 0}, Quat::euler({0, 30, 0}), {2.0f, 4.0f, 0.5f});  // (a thickness is metres in the Blockout's space: divided by the scale)
    CHECK(blockout_room_growth(p, g));
    CHECK_NEAR(g.x, 0.1, 1e-5);
    CHECK_NEAR(g.z, 0.4, 1e-5);
    CHECK_NEAR(g.down, 0.075, 1e-5);
    CHECK_NEAR(g.up, 0.1, 1e-5);
    p.shape = Shape::Cylinder, p.segments = 16;
    CHECK(blockout_room_growth(p, g) && g.x > 0.1);  // (a cylinder's wall is measured at its facets' middles)
    p.room = false;
    CHECK(!blockout_room_growth(p, g));
    p.room = true;
    p.to_blockout = Mat4::trs({0, 0, 0}, Quat(), {1.0f, 0.0f, 1.0f});
    CHECK(!blockout_room_growth(p, g));
  });

  test("blockout rooms: an opening's size is worked out from its Width, Head and Sill and its Depth, and a head under the sill is skipped by name", [] {
    BlockoutPiece door;
    door.opening = 1, door.width = 0.9f, door.head = 2.1f, door.sill = 0.0f, door.depth = 0.4f;
    const Piece p = door.to_piece(7, Mat4::identity());
    CHECK(p.shape == Shape::Box && p.op == Op::Subtract && !p.room);
    CHECK_NEAR(p.size.x, 0.9f, 1e-6f);
    CHECK_NEAR(p.size.y, 2.1f, 1e-6f);
    CHECK_NEAR(p.size.z, 0.4f, 1e-6f);
    CHECK_NEAR(p.to_blockout.point({0, 0, 0}).y, 1.05f, 1e-6f);  // (the box's middle between sill and head above the origin)
    door.opening = 2, door.sill = 0.9f, door.head = 2.0f;
    const Piece w = door.to_piece(7, Mat4::translate({1, 2, 3}));
    CHECK_NEAR(w.size.y, 1.1f, 1e-6f);
    CHECK_NEAR(w.to_blockout.point({0, 0, 0}).y, 2.0f + 1.45f, 1e-6f);
    door.head = 0.5f;  // (under the sill)
    blockout::Report rep;
    Mesh out;
    std::vector<Piece> pieces{door.to_piece(7, Mat4::identity())};
    CHECK(blockout::build(pieces, blockout::Settings(), out, nullptr, &rep) && rep.skipped.size() == 1 && rep.skipped[0].id == 7);
  });
}

/* ------------------------------------------------------------------ round 478: the test engineer's (independent of the implementation) */
#include <atomic>
#include <cfloat>
#include <chrono>
#include <thread>

namespace {

/* Matter in a room's own box: shell less air (a box room of equal thicknesses). */
double r478e_matter(Vec3 air, double t = 0.2) { return (air.x + 2 * t) * (air.y + 2 * t) * (air.z + 2 * t) - (double)air.x * air.y * air.z; }

/* Corners of a mesh: the bounds. */
void r478e_bounds(const Mesh &m, Vec3 &lo, Vec3 &hi) {
  lo = Vec3(1e30f), hi = Vec3(-1e30f);
  for (const Vec3 &p : m.positions) {
    lo = Vec3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
    hi = Vec3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
  }
}

/* Whether a result is a mesh a user could keep, or the build said why not. */
bool r478e_ok_or_named(const R476Built &b, const char *what) {
  if (!b.ok) return !b.report.why.empty();
  if (b.mesh.face_count() == 0) return true;
  return r476_sound(b.mesh, what);
}

/* How many faces of a box-only result do not lie in the plane of the side they name, or face the wrong way. A face's
 * side is the air's (the room's inside: the face looks into the air), the shell's (outside) or the box's own (an Add
 * and an Intersect face look out of the box, a Subtract's into it). Axis-aligned unit-scale pieces only. */
size_t r478e_wrong_planes(const R476Built &b, const std::vector<Piece> &pieces, size_t *not_flat = nullptr) {
  size_t wrong = 0;
  if (not_flat) *not_flat = 0;
  for (size_t f = 0; f < b.mesh.face_count(); f++) {
    const Piece *p = nullptr;
    for (const Piece &q : pieces)
      if (q.id == b.faces.piece[f]) p = &q;
    if (!p) {
      wrong++;
      continue;
    }
    const uint32_t s = b.faces.side[f];
    const bool shell = s >= blockout::kSideShell;
    const uint32_t t = shell ? s - blockout::kSideShell : s;
    if (t > 5 || !b.faces.flat[f]) {
      if (not_flat) (*not_flat)++;
      if (t > 5) wrong++;
      continue;
    }
    const int axis = (int)t / 2;
    const float sign = (t & 1) ? 1.0f : -1.0f;
    const Vec3 at = p->to_blockout.point(Vec3(0.0f));
    float half = p->size[axis] * 0.5f, expect_normal = sign;
    if (p->room && shell) {
      const float grow = axis == 1 ? (sign < 0 ? p->floor : (p->open_top ? 0.0f : p->ceiling)) : p->wall;
      half += grow;
    } else if (p->room) {
      expect_normal = -sign;
    } else if (p->op == Op::Subtract) {
      expect_normal = -sign;
    }
    const float where = at[axis] + sign * half;
    Vec3 n(0.0f);
    const uint32_t k = b.mesh.face_size(f);
    const uint32_t *v = b.mesh.face_verts(f);
    double worst = 0.0;
    for (uint32_t i = 0; i < k; i++) {
      const Vec3 a = b.mesh.positions[v[i]], c = b.mesh.positions[v[(i + 1) % k]];
      n.x += (a.y - c.y) * (a.z + c.z), n.y += (a.z - c.z) * (a.x + c.x), n.z += (a.x - c.x) * (a.y + c.y);
      worst = std::max(worst, (double)std::fabs(a[axis] - where));
    }
    const float len = std::sqrt(dot(n, n));
    const bool facing = len > 0.0f && n[axis] / len * expect_normal > 0.99f;
    if (worst > 1e-4 || !facing) {
      std::printf("    face %zu names piece %llu side %u: off its plane by %g, facing %d\n", f, (unsigned long long)p->id, s, worst, (int)facing);
      wrong++;
    }
  }
  return wrong;
}

Piece r478e_turned(Piece p, Vec3 at, Vec3 euler_degrees) {
  const Quat q = Quat::euler(euler_degrees);
  p.to_blockout = Mat4::trs(q.rotate(at), q, Vec3(1.0f));
  return p;
}

}  // namespace

static void tests_round478_engineer() {
  /* ---- Rooms turned, far away, small, odd ---- */
  test("blockout rooms (te): a room turned to an odd angle is still 12 faces with a wall of the set thickness, alone and with a neighbour", [] {
    /* (The field path needs a cell of about a sixth of the wall on a turned room: 0.03 m for a 0.2 m wall.) */
    for (Path path : r476_paths())
      for (const Vec3 turn : {Vec3(0, 33, 0), Vec3(20, 0, 0), Vec3(11, 47, 29)}) {
        const R476Built one = r476_build({r478e_turned(r478_room(1, {4, 3, 4}, {0, 0, 0}), {0, 0, 0}, turn)}, path, 0.03f);
        const bool good = one.ok && r476_sound(one.mesh, r476_name(path));
        if (!good) std::printf("    one room turned %g %g %g\n", turn.x, turn.y, turn.z);
        CHECK(good);
        CHECK(one.mesh.face_count() == 12);
        CHECK_NEAR(r476_volume(one.mesh), r478e_matter({4, 3, 4}), 0.03);
        /* Two overlapping, both turned together: the same airs and shells, turned. */
        const R476Built two = r476_build({r478e_turned(r478_room(1, {4, 3, 4}, {0, 0, 0}), {0, 0, 0}, turn), r478e_turned(r478_room(2, {4, 3, 4}, {0, 0, 0}), {3, 0, 0}, turn)}, path, 0.03f);
        CHECK(two.ok && r476_sound(two.mesh, r476_name(path)));
        CHECK_NEAR(r476_volume(two.mesh), 26.704, 0.05);
      }
  });

  test("blockout rooms (te): a room a kilometre from the origin is the same room; a hundred kilometres away is closed or named, never broken", [] {
    for (Path path : r476_paths()) {
      const R476Built km = r476_build({r478_room(1, {4, 3, 4}, {1000, 0, 0}), r478_room(2, {4, 3, 4}, {1004.2f, 0, 0})}, path);
      CHECK(km.ok && r476_sound(km.mesh, r476_name(path)));
      CHECK(km.mesh.face_count() == 6 + 12);
      CHECK_NEAR(r476_volume(km.mesh), 2 * 4.4 * 3.4 * 4.4 - 0.2 * 3.4 * 4.4 - 96.0, 0.05);
      const R476Built far = r476_build({r478_room(1, {4, 3, 4}, {100000, 0, 0})}, path);
      CHECK(r478e_ok_or_named(far, r476_name(path)));
      if (far.ok && far.mesh.face_count() > 0) CHECK(structurally_valid(far.mesh) && closed_manifold(far.mesh));
    }
  });

  test("blockout rooms (te): a closet of half a metre built onto a hall's outer wall is closed and has its own air", [] {
    for (Path path : r476_paths()) {
      const std::vector<Piece> pieces{r478_room(1, {10, 3, 10}, {0, 0, 0}), r478_room(2, {0.5f, 0.5f, 0.5f}, {5.45f, -1.2f, 0})};
      const R476Built b = r476_build(pieces, path, 0.025f);
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      CHECK_NEAR(r476_volume(b.mesh), r478e_matter({10, 3, 10}) + 0.7 * 0.9 * 0.9 - 0.125, 5e-3);
      CHECK(r478e_ok_or_named(b, "closet"));
    }
  });

  test("blockout rooms (te): three Merge rooms meeting at a corner are one space with no wall between them", [] {
    for (Path path : r476_paths()) {
      const std::vector<Piece> pieces{r478_room(1, {4, 3, 4}, {0, 0, 0}), r478_room(2, {4, 3, 4}, {3, 0, 0}), r478_room(3, {4, 3, 4}, {0, 0, 3})};
      const R476Built b = r476_build(pieces, path);
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      CHECK_NEAR(r476_volume(b.mesh), 45.76 * 3.4 - 40.0 * 3.0, 4e-3);
      CHECK(r478_corners_inside(b.mesh, {-2, -1.5f, -2}, {5, 1.5f, 2}) == 0 && r478_corners_inside(b.mesh, {-2, -1.5f, -2}, {2, 1.5f, 5}) == 0);
    }
  });

  test("blockout rooms (te): rooms of every kind in one scene - pieces in any order give the same shape", [] {
    for (Path path : r476_paths()) {
      std::vector<Piece> pieces{r478_room(1, {4, 3, 4}, {0, 0, 0}), r478_room(2, {4, 3, 4}, {4.2f, 0, 0}), r478_room(3, {2, 2, 2}, {2.1f, -0.3f, 0}, true),
                                box_piece(4, {0.6f, 2.1f, 0.9f}, {2.1f, -0.45f, 1.7f}, Op::Subtract), box_piece(5, {0.5f, 3.0f, 0.5f}, {-0.8f, 0, 0.5f}),
                                box_piece(6, {10, 10, 10}, {4, 0, 0}, Op::Intersect)};
      pieces[1].open_top = true;
      const R476Built a = r476_build(pieces, path);
      CHECK(a.ok && r476_sound(a.mesh, r476_name(path)));
      std::vector<Piece> reversed(pieces.rbegin(), pieces.rend()), turned(pieces.begin() + 2, pieces.end());
      turned.insert(turned.end(), pieces.begin(), pieces.begin() + 2);
      for (const std::vector<Piece> *other : {&reversed, &turned}) {
        const R476Built b = r476_build(*other, path);
        const bool same = b.ok && r476_same_shape(a, b);
        if (!same) std::printf("    %s: another order gave %zu faces, not %zu\n", r476_name(path), b.mesh.face_count(), a.mesh.face_count());
        CHECK(same);
      }
    }
  });

  /* ---- Rooms inside rooms ---- */
  test("blockout rooms (te): a Keep room wholly inside another Keep room's air floats with all its walls; one inside a Merge room's wall leaves a sealed pocket", [] {
    for (Path path : r476_paths()) {
      const R476Built floating = r476_build({r478_room(1, {8, 4, 8}, {0, 0, 0}, true), r478_room(2, {2, 2, 2}, {0, 0, 0}, true)}, path);
      CHECK(floating.ok && r476_sound(floating.mesh, r476_name(path)));
      CHECK_NEAR(r476_volume(floating.mesh), r478e_matter({8, 4, 8}) + r478e_matter({2, 2, 2}), 5e-3);
      CHECK(floating.mesh.face_count() == 24);
      /* A pocket of air inside the wall (x 2.05..2.15 of the wall 2.0..2.2), its own walls reaching 0.05 out of it. */
      Piece pocket = r478_room(2, {0.1f, 0.1f, 0.1f}, {2.1f, 0, 0});
      pocket.wall = pocket.floor = pocket.ceiling = 0.1f;
      const R476Built in_wall = r476_build({r478_room(1, {4, 3, 4}, {0, 0, 0}), pocket}, path, 0.025f);
      CHECK(in_wall.ok && r476_sound(in_wall.mesh, r476_name(path)));
      CHECK_NEAR(r476_volume(in_wall.mesh), r478e_matter({4, 3, 4}) + 0.3 * 0.3 * 0.05 - 0.001, 3e-3);
    }
  });

  test("blockout rooms (te): a Merge room inside a Keep room's air has no walls of its own - the Keep room's air removes them", [] {
    /* ADR 0128's order: the Merge shells less every room's air (a Keep room's too), so a Merge room in a Keep room's air leaves nothing. */
    for (Path path : r476_paths()) {
      const std::vector<Piece> keep_alone{r478_room(1, {8, 3, 8}, {0, 0, 0}, true)};
      const std::vector<Piece> both{r478_room(1, {8, 3, 8}, {0, 0, 0}, true), r478_room(2, {2, 2, 2}, {0.5f, -0.3f, 0.25f})};
      const R476Built a = r476_build(keep_alone, path), b = r476_build(both, path);
      CHECK(a.ok && b.ok && r476_sound(b.mesh, r476_name(path)));
      CHECK(r476_same_shape(a, b));
    }
  });

  test("blockout rooms (te): a door wider than the wall is long cuts the whole wall; a door cut in the air or far away changes nothing", [] {
    for (Path path : r476_paths()) {
      const std::vector<Piece> base{r478_room(1, {4, 3, 4}, {0, 0, 0})};
      std::vector<Piece> wide = base;
      wide.push_back(box_piece(2, {0.6f, 2.1f, 10.0f}, {2.1f, -0.45f, 0}, Op::Subtract));
      const R476Built w = r476_build(wide, path);
      CHECK(w.ok && r476_sound(w.mesh, r476_name(path)));
      /* The cut leaves the box x 1.8..2.2, z of the whole shell, y -1.5..0.6, less the air in it. */
      CHECK_NEAR(r476_volume(w.mesh), r478e_matter({4, 3, 4}) - (0.4 * 4.4 * 2.1 - 0.2 * 4.0 * 2.1), 4e-3);
      const R476Built plain = r476_build(base, path);
      for (const Piece &miss : {box_piece(2, {0.6f, 2.1f, 0.9f}, {0, 0, 0}, Op::Subtract), box_piece(2, {0.6f, 2.1f, 0.9f}, {50, 0, 0}, Op::Subtract)}) {
        std::vector<Piece> v = base;
        v.push_back(miss);
        const R476Built b = r476_build(v, path);
        CHECK(b.ok && r476_same_shape(plain, b) && r478_faces_of(b, 2) == 0);
      }
      /* A door through a Keep room's wall opens it to the outside. */
      Piece keep = r478_room(1, {4, 3, 4}, {0, 0, 0}, true);
      const R476Built opened = r476_build({keep, box_piece(2, {0.6f, 2.1f, 0.9f}, {2.1f, -0.45f, 0}, Op::Subtract)}, path);
      CHECK(opened.ok && r476_sound(opened.mesh, r476_name(path)));
      CHECK_NEAR(r476_volume(opened.mesh), r478e_matter({4, 3, 4}) - 0.2 * 0.9 * 2.1, 3e-3);
    }
  });

  test("blockout rooms (te): a cylinder room overlapping a box room is one space; their shared air has no wall in it", [] {
    for (Path path : r476_paths()) {
      Piece cyl = cylinder_piece(2, 2.0f, 3.0f, 16, {3, 0, 0});
      cyl.room = true;
      const std::vector<Piece> pieces{r478_room(1, {4, 3, 4}, {0, 0, 0}), cyl};
      const R476Built b = r476_build(pieces, path, 0.05f);
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      CHECK(r478_corners_inside(b.mesh, {-2, -1.5f, -2}, {2, 1.5f, 2}) == 0);
      const double box_only = r478e_matter({4, 3, 4});
      CHECK(r476_volume(b.mesh) > box_only - 1e-3);
      CHECK(r476_volume(b.mesh) < box_only + 40.0);
    }
  });

  test("blockout rooms (te): an open-topped room under another room's floor is closed by that floor", [] {
    for (Path path : r476_paths()) {
      Piece lower = r478_room(1, {4, 3, 4}, {0, 0, 0});
      lower.open_top = true;
      const R476Built b = r476_build({lower, r478_room(2, {4, 3, 4}, {0, 3.2f, 0})}, path);
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      CHECK_NEAR(r476_volume(b.mesh), (4.4 * 3.2 * 4.4 - 48.0) + r478e_matter({4, 3, 4}), 4e-3);
      CHECK(b.report.clusters.size() == 1);
      /* Nothing of the lower room's air lies in the upper room's floor. */
      CHECK(r478_corners_inside(b.mesh, {-2, -1.5f, -2}, {2, 1.5f, 2}) == 0);
    }
  });

  /* ---- Intersect ---- */
  test("blockout rooms (te): an Intersect piece that holds everything changes nothing; one that misses everything leaves an empty mesh, not a refusal", [] {
    for (Path path : r476_paths()) {
      const std::vector<Piece> base{r478_room(1, {4, 3, 4}, {0, 0, 0}), r478_room(2, {4, 3, 4}, {4.2f, 0, 0}), box_piece(3, {0.6f, 2.1f, 0.9f}, {2.1f, -0.45f, 0}, Op::Subtract)};
      const R476Built plain = r476_build(base, path);
      std::vector<Piece> all = base;
      all.push_back(box_piece(9, {1000, 1000, 1000}, {0, 0, 0}, Op::Intersect));
      const R476Built everything = r476_build(all, path);
      CHECK(everything.ok && r476_sound(everything.mesh, r476_name(path)) && r476_same_shape(plain, everything));
      CHECK(r478_faces_of(everything, 9) == 0);
      /* Exactly the two shells' box: its sides are the outer walls' own planes. */
      std::vector<Piece> flush = base;
      flush.push_back(box_piece(9, {8.8f, 3.4f, 4.4f}, {2.1f, 0, 0}, Op::Intersect));
      const R476Built same = r476_build(flush, path);
      CHECK(same.ok && r476_sound(same.mesh, r476_name(path)) && r476_same_shape(plain, same));
      std::vector<Piece> none = base;
      none.push_back(box_piece(9, {1, 1, 1}, {500, 0, 0}, Op::Intersect));
      const R476Built nothing = r476_build(none, path);
      CHECK(nothing.ok && nothing.mesh.face_count() == 0 && nothing.report.why.empty() && nothing.faces.piece.empty());
      /* The air's own box: what is inside it is no matter at all. */
      std::vector<Piece> air = {r478_room(1, {4, 3, 4}, {0, 0, 0}), box_piece(9, {4, 3, 4}, {0, 0, 0}, Op::Intersect)};
      const R476Built empty_air = r476_build(air, path);
      CHECK(empty_air.ok && empty_air.mesh.face_count() == 0 && empty_air.report.why.empty());
      /* Two that do not overlap: nothing is inside both. */
      std::vector<Piece> apart = base;
      apart.push_back(box_piece(8, {2, 10, 10}, {-1, 0, 0}, Op::Intersect));
      apart.push_back(box_piece(9, {2, 10, 10}, {6, 0, 0}, Op::Intersect));
      const R476Built two = r476_build(apart, path);
      CHECK(two.ok && two.mesh.face_count() == 0 && two.report.why.empty());
      /* An Intersect piece alone. */
      const R476Built alone = r476_build({box_piece(9, {1, 1, 1}, {0, 0, 0}, Op::Intersect)}, path);
      CHECK(alone.ok && alone.mesh.face_count() == 0);
    }
  });

  test("blockout rooms (te): a door inside an Intersect piece still cuts; an Add piece half inside one is trimmed to it", [] {
    for (Path path : r476_paths()) {
      const R476Built cut = r476_build({r478_room(1, {4, 3, 4}, {0, 0, 0}), r478_room(2, {4, 3, 4}, {4.2f, 0, 0}), box_piece(3, {3, 10, 10}, {2.1f, 0, 0}, Op::Intersect),
                                        box_piece(4, {0.6f, 2.1f, 0.9f}, {2.1f, -0.45f, 0}, Op::Subtract)},
                                       path);
      CHECK(cut.ok && r476_sound(cut.mesh, r476_name(path)));
      CHECK_NEAR(r476_volume(cut.mesh), 3 * 3.4 * 4.4 - 2 * 1.4 * 12.0 - 0.2 * 2.1 * 0.9, 3e-3);
      CHECK(r478_faces_of(cut, 4) >= 3);
      const R476Built half = r476_build({box_piece(1, {4, 1, 1}, {1, 0, 0}), box_piece(2, {2, 10, 10}, {0, 0, 0}, Op::Intersect)}, path);
      CHECK(half.ok && r476_sound(half.mesh, r476_name(path)));
      CHECK_NEAR(r476_volume(half.mesh), 2.0, 1e-4);
    }
  });

  /* ---- Names of sides ---- */
  test("blockout rooms (te): every face of a result with rooms, a Keep room, an open top, a door, a column and an Intersect piece lies in the plane of the side it names", [] {
    for (Path path : r476_paths()) {
      std::vector<Piece> pieces{r478_room(1, {4, 3, 4}, {0, 0, 0}), r478_room(2, {4, 3, 4}, {4.2f, 0, 0}), r478_room(3, {2, 2, 2}, {2.1f, -0.3f, 0}, true),
                                box_piece(4, {0.6f, 2.1f, 0.9f}, {2.1f, -0.45f, 1.7f}, Op::Subtract), box_piece(5, {0.5f, 3.0f, 0.5f}, {-0.8f, 0, 0.5f}),
                                box_piece(6, {10, 10, 10}, {4, 0, 0}, Op::Intersect)};
      pieces[1].open_top = true;
      const R476Built b = r476_build(pieces, path);
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      size_t not_flat = 0;
      CHECK(r478e_wrong_planes(b, pieces, &not_flat) == 0);
      if (path == Path::Crisp) CHECK(not_flat == 0);
      /* Every named side's class is what the side is. */
      for (size_t f = 0; f < b.mesh.face_count(); f++) {
        const blockout::SideClass c = blockout::side_class(*std::find_if(pieces.begin(), pieces.end(), [&](const Piece &p) { return p.id == b.faces.piece[f]; }), b.faces.side[f]);
        if (b.faces.piece[f] == 4 || b.faces.piece[f] == 6) CHECK(c == blockout::SideClass::Cut);
        if (b.faces.piece[f] == 5) CHECK(c == blockout::SideClass::Outside);
      }
    }
  });

  /* ---- The cache, cancelling, hostile numbers ---- */
  test("blockout rooms (te): changing only a thickness, Keep walls or Open top rebuilds that room's cluster and no other", [] {
    using Edit = void (*)(Piece &);
    const std::vector<std::pair<const char *, Edit>> edits{{"wall", [](Piece &p) { p.wall = 0.3f; }},       {"floor", [](Piece &p) { p.floor = 0.3f; }},
                                                           {"ceiling", [](Piece &p) { p.ceiling = 0.3f; }}, {"keep walls", [](Piece &p) { p.keep_walls = true; }},
                                                           {"open top", [](Piece &p) { p.open_top = true; }}};
    for (Path path : r476_paths())
      for (const auto &e : edits) {
        const std::vector<Piece> base{r478_room(1, {4, 3, 4}, {0, 0, 0}), r478_room(2, {4, 3, 4}, {40, 0, 0}), r478_room(3, {4, 3, 4}, {43, 0, 0})};
        blockout::Cache cache;
        const R476Built a = r476_build(base, path, 0.1f, &cache);
        std::vector<Piece> edited = base;
        e.second(edited[1]);
        const R476Built b = r476_build(edited, path, 0.1f, &cache), fresh = r476_build(edited, path);
        const bool good = a.ok && b.ok && b.report.built == 1 && b.report.cached == 1 && r476_same_mesh(b, fresh) && !r476_same_mesh(a, b);
        if (!good) std::printf("    %s (%s): built %zu cached %zu, same as fresh %d, changed %d\n", e.first, r476_name(path), b.report.built, b.report.cached, (int)r476_same_mesh(b, fresh), (int)!r476_same_mesh(a, b));
        CHECK(good);
      }
  });

  test("blockout rooms (te): cancelling a build with rooms refuses as cancelled and leaves the mesh it was given untouched", [] {
    for (Path path : r476_paths()) {
      std::vector<Piece> pieces;
      for (int i = 0; i < 6; i++)
        for (int j = 0; j < 6; j++) pieces.push_back(r478_room((uint64_t)(i * 6 + j + 1), {4, 3, 4}, {4.0f * (float)i, 0, 4.0f * (float)j}));
      blockout::Settings st;
      st.path = path;
      std::atomic<bool> flag(true);
      blockout::Report rep;
      const R476Built seed = r476_build({r478_room(1, {1, 1, 1}, {0, 0, 0})}, Path::Field); Mesh out = seed.mesh;
      const size_t before = out.face_count();
      CHECK(!blockout::build(pieces, st, out, nullptr, &rep, nullptr, &flag) && rep.why == "cancelled" && out.face_count() == before);
      /* Cancelled while running: either it finished first or it refused cleanly, and the mesh is whole either way. */
      std::atomic<bool> later(false);
      std::thread stopper([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        later = true;
      });
      Mesh out2;
      blockout::Report rep2;
      const bool done = blockout::build(pieces, st, out2, nullptr, &rep2, nullptr, &later);
      stopper.join();
      CHECK(done ? (out2.face_count() > 0 && closed_manifold(out2)) : (rep2.why == "cancelled" && out2.face_count() == 0));
    }
  });

  test("blockout rooms (te): hostile numbers in a room's thicknesses, size and Intersect pieces give a valid mesh or a named refusal, and the other rooms stay", [] {
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    for (Path path : r476_paths())
      for (float bad : {nan, inf, -inf, -1.0f, 1e30f, FLT_MAX, 1e-30f, 1e6f})
        for (int field = 0; field < 4; field++) {
          Piece hostile = r478_room(1, {4, 3, 4}, {0, 0, 0});
          (field == 0 ? hostile.wall : field == 1 ? hostile.floor : field == 2 ? hostile.ceiling : hostile.size.x) = bad;
          hostile.open_top = field == 2;
          const R476Built b = r476_build({hostile, r478_room(2, {2, 2, 2}, {30, 0, 0})}, path);
          const bool good = r478e_ok_or_named(b, r476_name(path));
          if (!good) std::printf("    %s: field %d = %g\n", r476_name(path), field, bad);
          CHECK(good);
        }
    for (Path path : r476_paths()) {
      Piece bad_cut = box_piece(9, {nan, 1, 1}, {0, 0, 0}, Op::Intersect);
      const R476Built b = r476_build({r478_room(1, {4, 3, 4}, {0, 0, 0}), bad_cut}, path);
      CHECK(r478e_ok_or_named(b, "NaN Intersect piece"));
      if (b.ok) CHECK(b.report.skipped.size() == 1 && b.report.skipped[0].id == 9);
    }
  });
}

/* ---- The object, the editor, the console ---- */
namespace {

GameObject *r478e_room_scene(Editor &ed, GameObject **bo_out = nullptr, bool at_origin = true) {
  r477_empty_scene(ed);
  GameObject *bo = r477_blockout(ed);
  ed.select_for_test(bo->id, false);
  if (bo_out) *bo_out = bo;
  ed.command("blockout add room box 4 3 4");
  r477_settle(ed, 2);
  GameObject *room = ed.active_object_for_test();
  if (room && at_origin) room->set_local_position({0.0f, 0.0f, 0.0f});  // (Undo goes back to where the command put it, so the Undo tests leave it)
  r477_settle(ed, 2);
  return room;
}

}  // namespace

static void tests_round478_engineer_object() {
  test("blockout rooms (te): Undo and Redo walk every room field back and forth with the mesh", [] {
    Editor ed;
    ed.init_headless(900, 600);
    GameObject *room0 = r478e_room_scene(ed, nullptr, false);
    CHECK(room0 && room0->get<BlockoutPiece>());
    if (!room0) return;
    /* (Undo may rebuild the scene's objects: everything is found again by name and parent.) */
    auto mesh_now = [&]() -> Mesh { ed.command("blockout wait"); r477_settle(ed, 3); return *r477_mesh(r477_find(ed)); };
    auto room_now = [&]() { return r477_all_pieces(*r477_find(ed)).front(); };
    const Mesh base = mesh_now();
    for (const char *line : {"set BlockoutPiece.Wall 0.4", "set BlockoutPiece.Floor 0.5", "set BlockoutPiece.Ceiling 0.1", "set BlockoutPiece.Walls 1", "set BlockoutPiece.OpenTop 1"}) {
      ed.select_for_test(room_now()->id, false);
      const size_t depth = ed.undo_depth_for_test();
      const Mesh before = mesh_now();
      ed.command(line);
      const Mesh after = mesh_now();
      CHECK(ed.undo_depth_for_test() == depth + 1);
      ed.command("undo");
      const Mesh undone = mesh_now();
      const bool back = r477_same(undone, before) && r477_matches(ed);
      if (!back) std::printf("    '%s': after Undo the mesh has %zu points, before it had %zu\n", line, undone.positions.size(), before.positions.size());
      CHECK(back);
      ed.command("redo");
      const Mesh redone = mesh_now();
      CHECK(r477_same(redone, after) && r477_matches(ed));
    }
    for (int i = 0; i < 5; i++) ed.command("undo");
    CHECK(r477_same(mesh_now(), base));
  });

  test("blockout rooms (te): adding a door is one step; Undo takes the door and the wall closes, Redo brings both back", [] {
    Editor ed;
    ed.init_headless(900, 600);
    GameObject *room = r478e_room_scene(ed, nullptr, false);
    if (!room) { CHECK(false); return; }
    auto mesh_now = [&]() -> Mesh { ed.command("blockout wait"); r477_settle(ed, 3); return *r477_mesh(r477_find(ed)); };
    const Mesh closed = mesh_now();
    const size_t pieces = r477_count(ed, true);
    ed.command("blockout add door");
    const Mesh opened = mesh_now();
    CHECK(r477_count(ed, true) == pieces + 1 && !r477_same(opened, closed));
    ed.command("undo");
    const Mesh undone = mesh_now();
    if (!(r477_count(ed, true) == pieces && r477_same(undone, closed) && r477_matches(ed)))
      std::printf("    after Undo: %zu pieces (was %zu), mesh %zu faces (closed %zu), matches pieces %d, same as closed %d\n", r477_count(ed, true), pieces, undone.face_count(), closed.face_count(), (int)r477_matches(ed), (int)r477_same(undone, closed));
    CHECK(r477_count(ed, true) == pieces && r477_same(undone, closed) && r477_matches(ed));
    ed.command("redo");
    const Mesh redone = mesh_now();
    CHECK(r477_count(ed, true) == pieces + 1 && r477_same(redone, opened) && r477_matches(ed));
  });

  test("blockout rooms (te): duplicating a room puts a twin on top of it that changes nothing; duplicating a door cuts the same hole", [] {
    Editor ed;
    ed.init_headless(900, 600);
    GameObject *bo = nullptr;
    GameObject *room = r478e_room_scene(ed, &bo);
    if (!room) { CHECK(false); return; }
    ed.command("blockout add door");
    r477_settle(ed, 3);
    GameObject *door = ed.active_object_for_test();
    const double volume = r476_volume(*r477_mesh(bo));
    const size_t pieces = r477_count(ed, true);
    for (GameObject *g : {room, door}) {
      ed.select_for_test(g->id, false);
      ed.command("duplicate");
      r477_settle(ed, 3);
      CHECK(r477_count(ed, true) > pieces);
      CHECK(r477_matches(ed));
      CHECK_NEAR(r476_volume(*r477_mesh(bo)), volume, 5e-3);
      CHECK(r476_sound(*r477_mesh(bo), "after duplicate"));
    }
  });

  test("blockout rooms (te): deleting the room a door stood in leaves the door cutting nothing and the Blockout empty, closed and matching", [] {
    Editor ed;
    ed.init_headless(900, 600);
    GameObject *bo = nullptr;
    GameObject *room = r478e_room_scene(ed, &bo);
    if (!room) { CHECK(false); return; }
    ed.command("blockout add door");
    r477_settle(ed, 3);
    GameObject *door = ed.active_object_for_test();
    ed.scene().destroy(room);
    r477_settle(ed, 3);
    CHECK(r477_matches(ed) && r477_mesh(bo)->face_count() == 0);
    CHECK(door && r478_faces_named(*bo->get<Blockout>(), door->id) == 0);
  });

  test("blockout rooms (te): a disabled room leaves the Blockout, a room under an empty goes with the empty, and Play and Stop keep the rooms", [] {
    Editor ed;
    ed.init_headless(900, 600);
    GameObject *bo = nullptr;
    GameObject *room = r478e_room_scene(ed, &bo);
    if (!room) { CHECK(false); return; }
    GameObject *group = ed.scene().create("Group", bo);
    group->set_local_position({20.0f, 2.0f, 0.0f});
    GameObject *second = r477_piece(ed, *group, "Room2", 0, 0, {0, 0, 0}, {4, 3, 4});
    second->get<BlockoutPiece>()->room = true;
    r477_settle(ed, 3);
    CHECK(r477_matches(ed));
    Vec3 lo, hi;
    r478e_bounds(*r477_mesh(bo), lo, hi);
    CHECK_NEAR(hi.x, 22.2f, 1e-3f);
    CHECK_NEAR(lo.y, -1.7f, 1e-3f);
    CHECK_NEAR(r476_volume(*r477_mesh(bo)), 2 * r478e_matter({4, 3, 4}), 5e-3);
    room->active = false;
    r477_settle(ed, 3);
    CHECK(r477_matches(ed));
    r478e_bounds(*r477_mesh(bo), lo, hi);
    CHECK_NEAR(lo.x, 17.8f, 1e-3f);
    CHECK_NEAR(r476_volume(*r477_mesh(bo)), r478e_matter({4, 3, 4}), 5e-3);
    room->active = true;
    r477_settle(ed, 3);
    const Mesh both = *r477_mesh(bo);
    ed.command("play");
    r477_settle(ed, 3);
    bo = r477_find(ed);
    CHECK(bo && r477_matches(ed) && r477_same(*r477_mesh(bo), both));
    ed.command("stop");
    r477_settle(ed, 3);
    bo = r477_find(ed);  // (Stop puts the scene back from its copy: the old pointers are gone)
    CHECK(bo && r477_matches(ed) && r477_same(*r477_mesh(bo), both));
    CHECK(bo && r477_all_pieces(*bo).size() == 2);
  });

  test("blockout rooms (te): bad console input for rooms, doors and 'blockout set' warns and changes nothing", [] {
    Editor ed;
    ed.init_headless(900, 600);
    GameObject *bo = nullptr;
    GameObject *room = r478e_room_scene(ed, &bo);
    if (!room) { CHECK(false); return; }
    const size_t pieces = r477_count(ed, true);
    const Mesh before = *r477_mesh(bo);
    for (const char *line : {"blockout add room box abc", "blockout add room box 4 3", "blockout add room box -1 3 4", "blockout add room box nan 3 4", "blockout add room cylinder 0 2",
                             "blockout add room cylinder -1 2", "blockout add room sphere 1", "blockout add door abc", "blockout add door -1 2", "blockout add door 1 nan",
                             "blockout add window 1 -2", "blockout add door 1 2 nan", "blockout set wall", "blockout set wall -0.5", "blockout set wall nan", "blockout set floor inf",
                             "blockout set ceiling x", "blockout set walls", "blockout set opentop", "blockout add box intersect abc", "blockout add intersect nonsense"}) {
      bo = r477_find(ed);
      room = r477_all_pieces(*bo).front();
      ed.select_for_test(room->id, false);
      const size_t mark = r477_mark();
      const size_t count = r477_count(ed, true);
      ed.command(line);
      r477_settle(ed, 3);
      size_t warnings = 0;
      r477_log_since(mark, &warnings);
      bo = r477_find(ed);
      room = r477_all_pieces(*bo).front();
      const BlockoutPiece *bp = room->get<BlockoutPiece>();
      const bool fields_sane = std::isfinite(bp->wall) && std::isfinite(bp->floor) && std::isfinite(bp->ceiling) && bp->wall >= 0.0f && bp->floor >= 0.0f && bp->ceiling >= 0.0f;
      const bool made_nothing = r477_count(ed, true) == count;
      /* Either it warned and left the scene alone, or it did something valid: a piece, if made, has finite sensible numbers. */
      const bool ok = fields_sane && r477_matches(ed) && (made_nothing ? warnings >= 1 || r477_same(before, *r477_mesh(bo)) : true);
      if (!ok || (!made_nothing && warnings == 0 && false)) std::printf("    '%s': warnings %zu, made %zu piece(s), sane %d\n", line, warnings, r477_count(ed, true) - count, (int)fields_sane);
      CHECK(ok);
      if (!made_nothing) {
        const BlockoutPiece *np = r477_active_piece(ed);
        if (np && !(np->size.x > 0.0f && np->radius > 0.0f)) std::printf("    '%s' made a piece: shape %d size %g %g %g radius %g opening %d width %g head %g sill %g\n", line, np->shape, np->size.x, np->size.y, np->size.z, np->radius, np->opening, np->width, np->head, np->sill);
        CHECK(np && std::isfinite(np->size.x) && std::isfinite(np->size.y) && std::isfinite(np->size.z) && np->size.x > 0.0f && np->radius > 0.0f);
        ed.command("undo");
        r477_settle(ed, 2);
      }
    }
    CHECK(r477_count(ed, true) == pieces);
  });

  test("blockout rooms (te): 'blockout set wall' on a mixed selection changes the rooms and leaves the box, the door and the Blockout alone", [] {
    Editor ed;
    ed.init_headless(900, 600);
    GameObject *bo = nullptr;
    GameObject *room = r478e_room_scene(ed, &bo);
    if (!room) { CHECK(false); return; }
    GameObject *plain = r477_piece(ed, *bo, "Plain", 0, 0, {20, 0, 0}, {2, 2, 2});
    GameObject *other = r477_piece(ed, *bo, "Room2", 0, 0, {10, 0, 0}, {2, 2, 2});
    other->get<BlockoutPiece>()->room = true;
    r477_settle(ed, 2);
    ed.select_for_test(room->id, false);
    ed.select_for_test(plain->id, true);
    ed.select_for_test(other->id, true);
    ed.select_for_test(bo->id, true);
    ed.command("blockout set wall 0.45");
    r477_settle(ed, 3);
    CHECK(room->get<BlockoutPiece>()->wall == 0.45f && other->get<BlockoutPiece>()->wall == 0.45f);
    CHECK(plain->get<BlockoutPiece>()->wall == 0.2f && !plain->get<BlockoutPiece>()->room && r477_matches(ed));
  });

  test("blockout rooms (te): the same room edits give the same mesh with workers on and off", [] {
    Mesh made[2];
    for (int workers = 0; workers < 2; workers++) {
      Editor ed;
      ed.init_headless(900, 600);
      ed.set_async_jobs_for_test(workers == 1);
      r477_empty_scene(ed);
      GameObject *bo = r477_blockout(ed);
      ed.select_for_test(bo->id, false);
      ed.command("blockout add room box 4 3 4");
      ed.command("blockout add door");
      ed.command("blockout set wall 0.3");
      ed.command("blockout set opentop on");
      r477_settle(ed, 3);
      ed.command("blockout wait");
      r477_settle(ed, 3);
      CHECK(r477_matches(ed));
      made[workers] = *r477_mesh(bo);
    }
    CHECK(r477_same(made[0], made[1]));
  });
}

/* ------------------------------------------------------------------ round 478: the code review's findings */
static void tests_round478_review() {
  test("blockout: an Intersect piece exactly as wide as a room names the room's two ends on both paths", [] {
    std::vector<R476Built> built;
    const std::vector<Piece> pieces{r478_room(1, {4, 3, 4}, {0, 0, 0}), box_piece(2, {4.4f, 10, 10}, {0, 0, 0}, Op::Intersect)};
    for (Path path : r476_paths()) {
      built.push_back(r476_build(pieces, path));
      const R476Built &b = built.back();
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      CHECK(b.mesh.face_count() == 12);
      CHECK_NEAR(r476_volume(b.mesh), 4.4 * 3.4 * 4.4 - 48.0, 2e-3);
      size_t ends = 0;
      for (size_t f = 0; f < b.mesh.face_count(); f++) {
        if (b.faces.piece[f] != 2) continue;
        ends++;
        CHECK(std::fabs(std::fabs(b.mesh.positions[b.mesh.face_verts(f)[0]].x) - 2.2f) < 1e-5f);
      }
      if (ends != 2) std::printf("    %s: %zu faces name the Intersect piece\n", r476_name(path), ends);
      CHECK(ends == 2);  // (the two faces in its planes are its own: Cut for materials, whichever path built them)
    }
    if (built.size() == 2) CHECK(r476_same_shape(built[0], built[1]));
  });

  test("blockout rooms: a room of which nothing came out is named in the report's notes", [] {
    for (Path path : r476_paths()) {
      /* A Merge room wholly in a Keep room's air has no wall left: every room's air clears a Merge room's shell. */
      const R476Built in = r476_build({r478_room(1, {8, 3, 8}, {0, 0, 0}, true), r478_room(2, {2, 2, 2}, {0.5f, 0, 0.25f})}, path);
      CHECK(in.ok && r476_sound(in.mesh, r476_name(path)));
      CHECK(in.report.notes.size() == 1 && in.report.notes[0].id == 2 && in.report.skipped.empty());
      /* A room that did come out is not named. */
      const R476Built fine = r476_build({r478_room(1, {4, 3, 4}, {0, 0, 0})}, path);
      CHECK(fine.ok && fine.report.notes.empty());
    }
    /* The grid, a wall of a centimetre at a cell of ten: the room is not there, and the report says so. */
    const R476Built thin = r476_build({r478_room(7, {4, 3, 4}, {0, 0, 0}, false, 0.01f)}, Path::Field, 0.1f);
    CHECK(thin.ok);
    if (thin.mesh.face_count() == 0) CHECK(thin.report.notes.size() == 1 && thin.report.notes[0].id == 7);
  });

  test("blockout: a Blend radius on an Intersect piece rounds nothing - the room stays twelve flat faces", [] {
    Piece trim = box_piece(2, {20, 20, 20}, {0, 0, 0}, Op::Intersect);
    trim.blend = 0.5f;
    for (Path path : r476_paths()) {
      const R476Built b = r476_build({r478_room(1, {4, 3, 4}, {0, 0, 0}), trim}, path);
      CHECK(b.ok && r476_sound(b.mesh, r476_name(path)));
      CHECK(b.mesh.face_count() == 12);
    }
  });

  test("blockout: a cluster no Intersect piece reaches is reported by the path the others took", [] {
    blockout::Settings st;
    Mesh m;
    blockout::Report rep;
    CHECK(blockout::build({box_piece(1, {1, 1, 1}, {0, 0, 0}), box_piece(2, {1, 1, 1}, {50, 0, 0}), box_piece(3, {4, 4, 4}, {0, 0, 0}, Op::Intersect)}, st, m, nullptr, &rep));
    CHECK(rep.clusters.size() == 2 && m.face_count() == 6);
    for (const blockout::ClusterReport &c : rep.clusters) CHECK(c.path == (blockout::crisp_available() ? Path::Crisp : Path::Field));
  });

  test("blockout rooms: a door added to a cylinder room of five sides, with no wall under the mouse, cuts through its first wall", [] {
    for (const char *line : {"blockout add room cylinder 2 3", "blockout add room box 4 3 4"}) {
      Editor ed;
      ed.init_headless(900, 600);
      r477_empty_scene(ed);
      GameObject *bo = r477_blockout(ed);
      bo->set_world_position({0.0f, 0.0f, 0.0f});
      GameObject *room = r478_room_by_command(ed, *bo, line);
      CHECK(room);
      if (!room) return;
      if (BlockoutPiece *bp = room->get<BlockoutPiece>()) bp->segments = 5;  // (a box does not read it)
      ed.mark_changed_for_test("segments");
      r477_settle(ed, 2);
      ed.select_for_test(room->id, false);
      ed.command("blockout add door");
      GameObject *door = ed.active_object_for_test();
      CHECK(door && door->get<BlockoutPiece>() && door->get<BlockoutPiece>()->opening == 1);
      if (!door) return;
      r477_settle(ed, 2);
      const size_t named = r478_faces_named(*bo->get<Blockout>(), door->id);
      if (named < 3) std::printf("    '%s': %zu faces name the door\n", line, named);
      CHECK(r477_matches(ed) && named >= 3);  // (two jambs and a head at least: it went through)
    }
  });

  test("blockout rooms: a door put where two rooms stand wall to wall goes through both walls", [] {
    Editor ed;
    ed.init_headless(900, 600);
    r477_empty_scene(ed);
    GameObject *bo = r477_blockout(ed);
    bo->set_world_position({0.0f, 0.0f, 0.0f});
    GameObject *left = r478_room_by_command(ed, *bo), *right = r478_room_by_command(ed, *bo);
    CHECK(left && right);
    if (!left || !right) return;
    left->set_local_position({-4.4f, 0.0f, 0.0f});  // (air 4 m wide, walls 0.2: the shells touch at x = -2.2)
    right->set_local_position({0.0f, 0.0f, 0.0f});
    ed.mark_changed_for_test("place");
    r477_settle(ed, 2);
    ed.select_for_test(right->id, false);
    ed.command("blockout add door");  // (its first wall is -X: the one against the other room)
    GameObject *door = ed.active_object_for_test();
    CHECK(door && door->get<BlockoutPiece>());
    if (!door || !door->get<BlockoutPiece>()) return;
    CHECK_NEAR(door->get<BlockoutPiece>()->depth, 0.4f + 0.2f, 1e-2f);
    CHECK_NEAR(door->local().position.x, -2.2f, 1e-2f);
    r477_settle(ed, 2);
    /* Open from one room's air to the other's: a corner of the result at each air's wall plane, within the door. */
    const Mesh *mesh = bo->get<MeshFilter>()->mesh.get();
    bool at_left = false, at_right = false;
    for (const Vec3 &p : mesh->positions) {
      if (std::fabs(p.z) > 0.5f) continue;
      at_left = at_left || std::fabs(p.x + 2.4f) < 1e-4f;
      at_right = at_right || std::fabs(p.x + 2.0f) < 1e-4f;
    }
    CHECK(r477_matches(ed) && at_left && at_right);
  });

  test("blockout rooms: 'blockout set wall' to the thickness the rooms already have adds no undo step", [] {
    Editor ed;
    ed.init_headless(900, 600);
    r477_empty_scene(ed);
    GameObject *bo = r477_blockout(ed);
    GameObject *room = r478_room_by_command(ed, *bo);
    CHECK(room);
    if (!room) return;
    ed.select_for_test(room->id, false);
    ed.command("blockout set wall 0.35");
    const size_t steps = ed.undo_depth_for_test();
    ed.command("blockout set wall 0.35");
    CHECK(ed.undo_depth_for_test() == steps);
    ed.command("blockout set wall 0.4");
    CHECK(ed.undo_depth_for_test() >= steps && room->get<BlockoutPiece>()->wall == 0.4f);  // (a real change still goes through; like steps may be one)
  });
}

/* ===================================================================== */
/* Round 479 (task 0455): handles on the pieces, snapping, the Room tool, F9  */
/* (the tool's and the handles' real-event tests live here with the helpers   */
/* they share, not in tests_editor.cpp)                                       */
/* ===================================================================== */

namespace {

/* A window sized for picking, an empty scene, the camera and a Blockout at the origin with one piece, selected. */
GameObject *r479_piece_scene(Editor &ed, const char *camera, int shape, Vec3 at, Vec3 size, GameObject **bo_out = nullptr) {
  te_view(ed, camera);
  GameObject *bo = r477_blockout(ed);
  bo->set_world_position({0.0f, 0.0f, 0.0f});
  GameObject *p = r477_piece(ed, *bo, shape == 0 ? "Box" : shape == 1 ? "Cylinder" : "Sphere", shape, 0, at, size, 0.5f, 1.0f);
  ed.select_for_test(p->id, false);
  ed.mark_changed_for_test("test scene");  // (an Undo goes back to this scene, not to the one before the test made it)
  r477_settle(ed, 3);
  if (bo_out) *bo_out = bo;
  return p;
}

void r479_press(Editor &ed, int x, int y, int mods = 0) {
  ed.step_frame_headless({r477_ev(R477ET::MouseMove, x, y, mods)});
  ed.step_frame_headless({r477_ev(R477ET::MouseDown, x, y, mods)});
}
void r479_move(Editor &ed, int x, int y, int mods = 0) { ed.step_frame_headless({r477_ev(R477ET::MouseMove, x, y, mods)}); }
void r479_release(Editor &ed, int x, int y, int mods = 0) {
  ed.step_frame_headless({r477_ev(R477ET::MouseUp, x, y, mods)});
  r477_settle(ed, 3);
}
void r479_right_click(Editor &ed, int x, int y) {
  platform::Event down = r477_ev(R477ET::MouseDown, x, y), up = r477_ev(R477ET::MouseUp, x, y);
  down.button = up.button = 1;
  ed.step_frame_headless({down});
  ed.step_frame_headless({up});
}

/* A handle taken, pulled to the pixel of a world point and let go: false when the handle or the point is off the screen. */
bool r479_pull(Editor &ed, const char *handle, Vec3 to, int mods = 0, bool *snapped = nullptr) {
  int hx = 0, hy = 0, tx = 0, ty = 0;
  if (!ed.blockout_handle_pixel_for_test(handle, hx, hy) || !ed.project_to_window(to, tx, ty)) return false;
  r479_press(ed, hx, hy, mods);
  if (!ed.blockout_dragging_for_test()) return false;
  r479_move(ed, tx, ty, mods);
  if (snapped) *snapped = ed.blockout_drag_snapped_for_test();
  r479_release(ed, tx, ty, mods);
  return true;
}

/* A number typed into one of the Adjust Last panel's fields (the panel must be showing and the buttons recorded). */
bool r479_type(Editor &ed, const std::string &key, const std::string &text) {
  Recti r;
  for (int i = 0; i < 4 && !ed.button_rect_for_test(key, r); i++) ed.step_frame_headless();
  if (!ed.button_rect_for_test(key, r)) return false;
  const int x = r.x + r.w / 2, y = r.y + r.h / 2;
  for (R477ET t : {R477ET::MouseMove, R477ET::MouseDown, R477ET::MouseUp, R477ET::MouseDown, R477ET::MouseUp}) ed.step_frame_headless({r477_ev(t, x, y)});
  ed.step_frame_headless({te_key(platform::KEY_A, platform::MOD_CTRL)});
  for (char c : text) {
    platform::Event e;
    e.type = R477ET::Text, e.codepoint = (uint32_t)c;
    ed.step_frame_headless({e});
  }
  ed.step_frame_headless({r477_key(platform::KEY_ENTER)});
  r477_settle(ed, 2);
  return true;
}

const char *const kR479Camera = "camera 30 40 12 0 0 0";

bool r479_bits_equal(const Vec3 &a, const Vec3 &b) { return std::memcmp(&a, &b, sizeof(Vec3)) == 0; }

/* The newest piece made: the active object's component, nullptr when it is none. */
BlockoutPiece *r479_active(Editor &ed) {
  GameObject *g = ed.active_object_for_test();
  return g ? g->get<BlockoutPiece>() : nullptr;
}

}  // namespace

static void tests_round479_handles() {
  test("blockout handles: pulling a box's +X handle 1 m outward with the mouse makes it 1 m wider, moves it half a metre and leaves the -X face in its plane - one undo step, the mesh follows", [] {
    Editor ed;
    GameObject *bo = nullptr;
    GameObject *box = r479_piece_scene(ed, kR479Camera, 0, {0, 0, 0}, {2, 2, 2}, &bo);
    const size_t depth = ed.undo_depth_for_test();
    CHECK(r479_pull(ed, "+x", {2.0f, 0.0f, 0.0f}));
    const BlockoutPiece *bp = box->get<BlockoutPiece>();
    CHECK_NEAR(bp->size.x, 3.0f, 1e-4f);
    CHECK(bp->size.y == 2.0f && bp->size.z == 2.0f);
    CHECK_NEAR(box->local().position.x, 0.5f, 1e-4f);
    CHECK_NEAR(box->world_matrix().point({-bp->size.x * 0.5f, 0.0f, 0.0f}).x, -1.0f, 1e-5f);
    CHECK(ed.undo_depth_for_test() == depth + 1);
    CHECK(r477_matches(ed) && r477_mesh(bo)->bounds().max.x > 1.9f);
    ed.command("undo");
    r477_settle(ed, 3);
    box = ed.scene().find_by_name("Box");
    CHECK(box && box->get<BlockoutPiece>()->size.x == 2.0f && box->local().position.x == 0.0f);
  });

  test("blockout handles: on a piece turned 30 degrees and scaled 2 in X the handle moves the face under the mouse: the size changes by the drag divided by the scale, the -X face stays", [] {
    Editor ed;
    GameObject *box = r479_piece_scene(ed, kR479Camera, 0, {0, 0, 0}, {2, 2, 2});
    box->set_local_rotation(Quat::axis_angle({0.0f, 1.0f, 0.0f}, 30.0f * kDeg2Rad));
    box->set_local_scale({2.0f, 1.0f, 1.0f});
    r477_settle(ed, 3);
    const Mat4 w0 = box->world_matrix();
    const Vec3 face0 = w0.point({-1.0f, 0.0f, 0.0f}), out = normalize(w0.dir({1.0f, 0.0f, 0.0f}));
    CHECK(r479_pull(ed, "+x", w0.point({1.0f, 0.0f, 0.0f}) + out * 1.0f, platform::MOD_CTRL));  // (Ctrl: no snapping, the drag is the pixel's)
    const BlockoutPiece *bp = box->get<BlockoutPiece>();
    CHECK_NEAR(bp->size.x, 2.5f, 0.03f);  // (1 world metre is 0.5 own units at scale 2)
    CHECK(length(box->world_matrix().point({-bp->size.x * 0.5f, 0.0f, 0.0f}) - face0) < 1e-4f);
    CHECK(bp->size.y == 2.0f && bp->size.z == 2.0f);
  });

  test("blockout handles: Shift pulls both opposite faces about the centre", [] {
    Editor ed;
    GameObject *box = r479_piece_scene(ed, kR479Camera, 0, {0, 0, 0}, {2, 2, 2});
    CHECK(r479_pull(ed, "+x", {2.0f, 0.0f, 0.0f}, platform::MOD_SHIFT));
    const BlockoutPiece *bp = box->get<BlockoutPiece>();
    CHECK_NEAR(bp->size.x, 4.0f, 1e-4f);
    CHECK(box->local().position.x == 0.0f);
  });

  test("blockout handles: a cylinder's top and bottom keep the other cap, its side changes the radius, a sphere's handle its radius", [] {
    Editor ed;
    GameObject *bo = nullptr;
    GameObject *cyl = r479_piece_scene(ed, kR479Camera, 1, {0, 0, 0}, Vec3(1.0f), &bo);  // (radius 0.5, height 1)
    const BlockoutPiece *bp = cyl->get<BlockoutPiece>();
    ed.command("blockout handle top 1");
    CHECK_NEAR(bp->height, 2.0f, 1e-5f);
    CHECK_NEAR(cyl->local().position.y, 0.5f, 1e-5f);
    CHECK_NEAR(cyl->local().position.y - bp->height * 0.5f, -0.5f, 1e-5f);  // (the bottom stayed)
    ed.command("blockout handle bottom 0.5");
    CHECK_NEAR(bp->height, 2.5f, 1e-5f);
    CHECK_NEAR(cyl->local().position.y + bp->height * 0.5f, 1.5f, 1e-5f);  // (the top stayed)
    ed.command("blockout handle side 0.25");
    CHECK_NEAR(bp->radius, 0.75f, 1e-5f);
    CHECK_NEAR(bp->height, 2.5f, 1e-5f);
    r477_settle(ed, 3);
    CHECK(r477_matches(ed));
    CHECK(r479_pull(ed, "top", {0.0f, 2.5f, 0.0f}));  // (the same by the mouse: the top from 1.5 to 2.5)
    CHECK_NEAR(bp->height, 3.5f, 0.02f);
    Editor ed2;
    GameObject *sph = r479_piece_scene(ed2, kR479Camera, 2, {0, 0, 0}, Vec3(1.0f));
    ed2.command("blockout handle radius 0.5");
    CHECK_NEAR(sph->get<BlockoutPiece>()->radius, 1.0f, 1e-5f);
    CHECK(sph->local().position.x == 0.0f && sph->local().position.y == 0.0f);
  });

  test("blockout handles: a room's face handles move its air and the wall handle changes the wall thickness and nothing else", [] {
    Editor ed;
    ed.init_headless(1600, 1000);
    GameObject *bo = nullptr;
    GameObject *room = r478e_room_scene(ed, &bo);
    CHECK(room && room->get<BlockoutPiece>());
    if (!room) return;
    const BlockoutPiece *bp = room->get<BlockoutPiece>();
    ed.command("blockout handle +x 1");
    CHECK_NEAR(bp->size.x, 5.0f, 1e-5f);
    CHECK_NEAR(room->local().position.x - bp->size.x * 0.5f, -2.0f, 1e-5f);
    CHECK(bp->wall == 0.2f && bp->floor == 0.2f && bp->ceiling == 0.2f && bp->size.y == 3.0f && bp->size.z == 4.0f);
    ed.command("blockout handle wall 0.1");
    CHECK_NEAR(bp->wall, 0.3f, 1e-5f);
    CHECK(bp->floor == 0.2f && bp->ceiling == 0.2f && bp->size.x == 5.0f && bp->size.y == 3.0f && bp->size.z == 4.0f);
    CHECK_NEAR(room->local().position.x, 0.5f, 1e-5f);
    ed.command("blockout handle -y 0.5");
    CHECK_NEAR(bp->size.y, 3.5f, 1e-5f);
    CHECK_NEAR(room->local().position.y, -0.25f, 1e-5f);
    r477_settle(ed, 3);
    CHECK(r477_matches(ed));
  });

  test("blockout handles: a door's handles change its width about the centre, its head and its sill", [] {
    Editor ed;
    ed.init_headless(1600, 1000);
    GameObject *bo = nullptr;
    GameObject *room = r478e_room_scene(ed, &bo);
    if (!room) { CHECK(false); return; }
    ed.command("blockout add door");
    GameObject *door = ed.active_object_for_test();
    BlockoutPiece *bp = door ? door->get<BlockoutPiece>() : nullptr;
    CHECK(bp && bp->opening == 1);
    if (!bp) return;
    const Vec3 at = door->local().position;
    ed.command("blockout handle width 0.1");
    CHECK_NEAR(bp->width, 1.1f, 1e-5f);
    ed.command("blockout handle head 0.2");
    CHECK_NEAR(bp->head, 2.3f, 1e-5f);
    ed.command("blockout handle sill 0.1");  // (the sill handle points down: a door's sill is 0 and stays there)
    CHECK(bp->sill == 0.0f);
    ed.command("blockout handle sill -0.5");
    CHECK_NEAR(bp->sill, 0.5f, 1e-5f);
    CHECK(r479_bits_equal(door->local().position, at));
  });

  test("blockout handles: dragged through zero the size stops at a small positive number and the opposite face stays; bad input changes nothing", [] {
    Editor ed;
    GameObject *box = r479_piece_scene(ed, kR479Camera, 0, {0, 0, 0}, {2, 2, 2});
    const BlockoutPiece *bp = box->get<BlockoutPiece>();
    ed.command("blockout handle +x -5");
    CHECK(bp->size.x == kBlockoutMinSize);
    CHECK_NEAR(box->local().position.x - bp->size.x * 0.5f, -1.0f, 1e-5f);
    ed.command("undo");
    r477_settle(ed, 3);
    box = ed.scene().find_by_name("Box");
    bp = box->get<BlockoutPiece>();
    const size_t depth = ed.undo_depth_for_test();
    for (const char *line : {"blockout handle", "blockout handle +x", "blockout handle +x nan", "blockout handle +x abc", "blockout handle +w 1", "blockout handle wall 1",
                             "blockout handle +x inf", "blockout handle top 1"}) {
      const size_t mark = r477_mark();
      ed.command(line);
      size_t warnings = 0;
      r477_log_since(mark, &warnings);
      if (warnings == 0) std::printf("    '%s' did not warn\n", line);
      CHECK(warnings >= 1);
      CHECK(bp->size.x == 2.0f && bp->size.y == 2.0f && bp->size.z == 2.0f && box->local().position.x == 0.0f);
    }
    CHECK(ed.undo_depth_for_test() == depth);
  });

  test("blockout handles: Esc and the right button put the piece back byte for byte and leave no undo step", [] {
    for (int how = 0; how < 2; how++) {
      Editor ed;
      GameObject *box = r479_piece_scene(ed, kR479Camera, 0, {0, 0, 0}, {2, 2, 2});
      const BlockoutPiece before = *box->get<BlockoutPiece>();
      const Transform t0 = box->local();
      const size_t depth = ed.undo_depth_for_test();
      int hx = 0, hy = 0, tx = 0, ty = 0;
      CHECK(ed.blockout_handle_pixel_for_test("+x", hx, hy) && ed.project_to_window({2.0f, 0.0f, 0.0f}, tx, ty));
      r479_press(ed, hx, hy);
      r479_move(ed, tx, ty);
      CHECK(ed.blockout_dragging_for_test() && box->get<BlockoutPiece>()->size.x > 2.5f);
      if (how == 0) {
        ed.step_frame_headless({r477_key(platform::KEY_ESCAPE)});
        ed.step_frame_headless({r477_key(platform::KEY_ESCAPE, false)});
      }
      else r479_right_click(ed, tx, ty);
      CHECK(!ed.blockout_dragging_for_test());
      r479_release(ed, tx, ty);
      const BlockoutPiece *bp = box->get<BlockoutPiece>();
      CHECK(r479_bits_equal(bp->size, before.size) && bp->radius == before.radius && bp->height == before.height && bp->wall == before.wall);
      CHECK(r479_bits_equal(box->local().position, t0.position) && std::memcmp(&box->local().rotation, &t0.rotation, sizeof(Quat)) == 0 &&
            r479_bits_equal(box->local().scale, t0.scale));
      CHECK(ed.undo_depth_for_test() == depth);
      CHECK(r477_matches(ed));
    }
  });

  test("blockout handles: pressing a handle and letting go without moving makes no undo step", [] {
    Editor ed;
    r479_piece_scene(ed, kR479Camera, 0, {0, 0, 0}, {2, 2, 2});
    const size_t depth = ed.undo_depth_for_test();
    int hx = 0, hy = 0;
    CHECK(ed.blockout_handle_pixel_for_test("+y", hx, hy));
    r479_press(ed, hx, hy);
    r479_release(ed, hx, hy);
    CHECK(ed.undo_depth_for_test() == depth);
  });

  test("blockout handles: the hint line names the drag while a handle is held", [] {
    Editor ed;
    r479_piece_scene(ed, kR479Camera, 0, {0, 0, 0}, {2, 2, 2});
    int hx = 0, hy = 0, tx = 0, ty = 0;
    CHECK(ed.blockout_handle_pixel_for_test("+x", hx, hy) && ed.project_to_window({1.5f, 0.0f, 0.0f}, tx, ty));
    r479_press(ed, hx, hy);
    r479_move(ed, tx, ty);
    ed.set_scene_hovered_for_test(true);
    CHECK(ed.statusbar_hint_row_for_test() == "blockout handle");
    CHECK(ed.statusbar_hint_for_test().find("Esc") != std::string::npos && ed.blockout_drag_text_for_test().find("+x") != std::string::npos);
    r479_release(ed, tx, ty);
  });
}

static void tests_round479_snapping() {
  /* Two boxes, A at the origin (x from -1 to 1) and B 2.35 away at its middle (its -X face at 2.35). */
  auto two_boxes = [](Editor &ed, GameObject **bo, GameObject **b) {
    te_view(ed, "camera 20 55 22 3 0 0");
    *bo = r477_blockout(ed);
    (*bo)->set_world_position({0.0f, 0.0f, 0.0f});
    r477_piece(ed, **bo, "A", 0, 0, {0, 0, 0}, {2, 2, 2});
    *b = r477_piece(ed, **bo, "B", 0, 0, {3.35f, 0, 0}, {2, 2, 2});
    ed.select_for_test((*b)->id, false);
    r477_settle(ed, 3);
  };
  auto face_x = [](const GameObject &g, float side) { return g.world_matrix().point({side * g.get<BlockoutPiece>()->size.x * 0.5f, 0.0f, 0.0f}).x; };

  test("blockout handles: a face pulled to within a few pixels of another piece's face lands in its plane, and the result has no face between them (on each path)", [] {
    for (int path = 0; path < 3; path++) {  // (Auto, Exact, Field: the flush case on each path the build has)
      if (path == 1 && !blockout::crisp_available()) continue;
      Editor ed;
      GameObject *bo = nullptr, *b = nullptr;
      te_view(ed, "camera 20 55 22 3 0 0");
      bo = r477_blockout(ed);
      bo->set_world_position({0.0f, 0.0f, 0.0f});
      bo->get<Blockout>()->method = path;
      r477_piece(ed, *bo, "A", 0, 0, {0, 0, 0}, {2, 2, 2});
      b = r477_piece(ed, *bo, "B", 0, 0, {3.35f, 0, 0}, {2, 2, 2});
      ed.select_for_test(b->id, false);
      r477_settle(ed, 3);
      bool snapped = false;
      CHECK(r479_pull(ed, "-x", {1.0f, 0.0f, 0.0f}, 0, &snapped));
      CHECK(snapped);
      const BlockoutPiece *bp = b->get<BlockoutPiece>();
      CHECK_NEAR(b->world_matrix().point({-bp->size.x * 0.5f, 0.0f, 0.0f}).x, 1.0f, 1e-5f);  // (in A's +X plane)
      r477_settle(ed, 3);
      CHECK(r477_matches(ed));
      const Mesh *m = r477_mesh(bo);
      if (!(m && m->face_count() == 6)) std::printf("    path %d: %zu faces\n", path, m ? m->face_count() : (size_t)0);
      CHECK(m && m->face_count() == 6);  // (one box: nothing between the two)
    }
  });

  test("blockout handles: Ctrl turns snapping off - the face lands where the mouse is, not in the other piece's plane", [=] {
    Editor ed;
    GameObject *bo = nullptr, *b = nullptr;
    two_boxes(ed, &bo, &b);
    bool snapped = true;
    CHECK(r479_pull(ed, "-x", {1.1f, 0.0f, 0.0f}, platform::MOD_CTRL, &snapped));
    CHECK(!snapped);
    CHECK(std::fabs(face_x(*b, -1.0f) - 1.1f) < 0.04f && std::fabs(face_x(*b, -1.0f) - 1.0f) > 1e-3f);
  });

  test("blockout handles: with nothing near, the face lands on the grid step", [=] {
    Editor ed;
    GameObject *bo = nullptr, *b = nullptr;
    two_boxes(ed, &bo, &b);
    CHECK(r479_pull(ed, "+x", {6.1f, 0.0f, 0.0f}));
    CHECK_NEAR(face_x(*b, 1.0f), 6.0f, 1e-4f);  // (the step is 0.25 m: 6.1 lands on 6.0)
    CHECK(r477_matches(ed));
  });

  test("blockout handles: a snap is held until the mouse leaves it by twice the snap distance, and the piece being dragged is no candidate", [=] {
    Editor ed;
    GameObject *bo = nullptr, *b = nullptr;
    two_boxes(ed, &bo, &b);
    int hx = 0, hy = 0, sx = 0, sy = 0, nx = 0, ny = 0, fx = 0, fy = 0;
    CHECK(ed.blockout_handle_pixel_for_test("-x", hx, hy) && ed.project_to_window({1.0f, 0.0f, 0.0f}, sx, sy) && ed.project_to_window({1.1f, 0.0f, 0.0f}, nx, ny) &&
          ed.project_to_window({1.9f, 0.0f, 0.0f}, fx, fy));
    r479_press(ed, hx, hy);
    r479_move(ed, sx, sy);
    CHECK(ed.blockout_drag_snapped_for_test());
    const float held = face_x(*b, -1.0f);
    r479_move(ed, nx, ny);  // (a little away from the plane: still held)
    CHECK(ed.blockout_drag_snapped_for_test() && std::fabs(face_x(*b, -1.0f) - held) < 1e-6f);
    r479_move(ed, fx, fy);  // (far from it: let go, back on the grid)
    CHECK(!ed.blockout_drag_snapped_for_test() || std::fabs(face_x(*b, -1.0f) - held) > 0.1f);
    r479_release(ed, fx, fy);
  });
}

static void tests_round479_gizmo_snap() {
  test("blockout handles: a piece moved along an axis with the transform gizmo lands a face in the plane of another piece's face when it comes within a few pixels", [] {
    Editor ed;
    te_view(ed, "camera 20 55 22 3 0 0");
    GameObject *bo = r477_blockout(ed);
    bo->set_world_position({0.0f, 0.0f, 0.0f});
    r477_piece(ed, *bo, "A", 0, 0, {0, 0, 0}, {2, 2, 2});
    GameObject *b = r477_piece(ed, *bo, "B", 0, 0, {3.3f, 0, 0}, {2, 2, 2});  // (B's -X face is at 2.3; A's +X face at 1)
    ed.select_for_test(b->id, false);
    ed.mark_changed_for_test("test scene");
    r477_settle(ed, 3);
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    CHECK(ed.project_to_window({3.3f, 0.0f, 0.0f}, x0, y0) && ed.project_to_window({2.0f, 0.0f, 0.0f}, x1, y1));  // (B's centre; where it would be flush)
    const int pixels = x1 - x0;  // (the gizmo's X handle follows the mouse along the screen)
    ed.command("tool move");
    ed.command("perf drag gizmo x " + std::to_string(pixels) + " frames 8");
    r477_settle(ed, 40);
    const float face = b->world_matrix().point({-1.0f, 0.0f, 0.0f}).x;
    std::printf("    B's -X face ended at x = %.4f (A's +X face is at 1)\n", face);
    CHECK_NEAR(face, 1.0f, 1e-4f);
    CHECK(r477_matches(ed));
  });
}

static void tests_round479_room_tool() {
  /* The Room tool's two drags on the ground: from (-2, 0, -1) to (2, 0, 1), then 3 m up. */
  auto ground_scene = [](Editor &ed) {
    te_view(ed, "camera 10 70 14 0 0 0");
    ed.command("tool room");
    r477_settle(ed, 2);
  };
  auto drags = [](Editor &ed, Vec3 a, Vec3 b, Vec3 top, bool make = true) {
    int ax = 0, ay = 0, bx = 0, by = 0, cx = 0, cy = 0;
    if (!ed.project_to_window(a, ax, ay) || !ed.project_to_window(b, bx, by) || !ed.project_to_window(top, cx, cy)) return false;
    r479_press(ed, ax, ay);
    r479_move(ed, bx, by);
    r479_release(ed, bx, by);
    if (!make) return true;
    r479_move(ed, cx, cy);
    r477_click(ed, cx, cy);
    r477_settle(ed, 3);
    return true;
  };

  test("blockout room tool: two drags on the ground make a room of the dragged size standing on it, in a new Blockout, as one undo step; the tool stays on", [=] {
    Editor ed;
    ground_scene(ed);
    CHECK(ed.room_tool_active_for_test());
    const size_t depth = ed.undo_depth_for_test();
    CHECK(drags(ed, {-2.0f, 0.0f, -1.0f}, {2.0f, 0.0f, 1.0f}, {0.0f, 3.0f, 0.0f}));
    GameObject *bo = r477_find(ed);
    BlockoutPiece *bp = r479_active(ed);
    CHECK(bo && bp && bp->room && bp->shape == 0 && blockout_of_piece(*ed.active_object_for_test()) == bo);
    if (!bp) return;
    CHECK_NEAR(bp->size.x, 4.0f, 1e-3f);
    CHECK_NEAR(bp->size.y, 3.0f, 1e-3f);
    CHECK_NEAR(bp->size.z, 2.0f, 1e-3f);
    const GameObject *room = ed.active_object_for_test();
    CHECK_NEAR(room->world_position().y - bp->size.y * 0.5f, 0.0f, 1e-3f);  // (the floor's top - the air's bottom - is on the ground)
    CHECK_NEAR(room->world_position().x, 0.0f, 1e-3f);
    CHECK_NEAR(room->world_position().z, 0.0f, 1e-3f);
    CHECK(r477_count(ed, false) == 1 && r477_count(ed, true) == 1 && r477_matches(ed) && r477_mesh(bo)->face_count() > 0);
    CHECK(ed.undo_depth_for_test() == depth + 1);
    CHECK(ed.room_tool_active_for_test() && ed.room_tool_stage_for_test() == 0);
    ed.command("undo");
    r477_settle(ed, 3);
    CHECK(r477_count(ed, true) == 0);
  });

  test("blockout room tool: the Circle option makes a cylinder room from the centre and the radius", [=] {
    Editor ed;
    ground_scene(ed);
    ed.command("blockout roomopt circle on");
    CHECK(drags(ed, {0.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {0.0f, 3.0f, 0.0f}));
    const BlockoutPiece *bp = r479_active(ed);
    CHECK(bp && bp->room && bp->shape == 1);
    if (!bp) return;
    CHECK_NEAR(bp->radius, 2.0f, 1e-3f);
    CHECK_NEAR(bp->height, 3.0f, 1e-3f);
    CHECK_NEAR(ed.active_object_for_test()->world_position().y, 1.5f, 1e-3f);
  });

  test("blockout room tool: on the top face of a box the room stands on that face, in the box's Blockout, turned to the surface", [=] {
    Editor ed;
    GameObject *bo = nullptr;
    GameObject *box = r479_piece_scene(ed, "camera 10 60 8 0 1 0", 0, {0, 0, 0}, {2, 2, 2}, &bo);
    (void)box;
    ed.command("tool room");
    r477_settle(ed, 2);
    CHECK(drags(ed, {-0.5f, 1.0f, -0.5f}, {0.5f, 1.0f, 0.5f}, {0.0f, 2.0f, 0.0f}));
    const GameObject *room = ed.active_object_for_test();
    const BlockoutPiece *bp = room ? room->get<BlockoutPiece>() : nullptr;
    CHECK(bp && bp->room && room != box && blockout_of_piece(*room) == bo);
    if (!bp) return;
    CHECK_NEAR(bp->size.x, 1.0f, 1e-3f);
    CHECK_NEAR(bp->size.z, 1.0f, 1e-3f);
    CHECK_NEAR(bp->size.y, 1.0f, 1e-3f);
    CHECK_NEAR(room->world_position().y - bp->size.y * 0.5f, 1.0f, 1e-3f);  // (the floor's top is the box's top)
    CHECK(r477_count(ed, false) == 1 && r477_matches(ed));
  });

  test("blockout room tool: a drag of no area makes nothing and leaves no undo step; Esc leaves the tool and the right button drops the room in progress", [=] {
    Editor ed;
    ground_scene(ed);
    const size_t depth = ed.undo_depth_for_test();
    CHECK(drags(ed, {1.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 1.0f}, {1.0f, 3.0f, 1.0f}, false));
    CHECK(ed.room_tool_stage_for_test() == 0 && r477_count(ed, true) == 0 && ed.undo_depth_for_test() == depth);
    CHECK(drags(ed, {-2.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {0.0f, 3.0f, 0.0f}, false));  // (a line: no area)
    CHECK(ed.room_tool_stage_for_test() == 0 && r477_count(ed, true) == 0);
    CHECK(drags(ed, {-2.0f, 0.0f, -1.0f}, {2.0f, 0.0f, 1.0f}, {0.0f, 3.0f, 0.0f}, false));
    CHECK(ed.room_tool_stage_for_test() == 2);
    int rx = 0, ry = 0;
    CHECK(ed.project_to_window({0.0f, 2.0f, 0.0f}, rx, ry));
    r479_right_click(ed, rx, ry);
    CHECK(ed.room_tool_stage_for_test() == 0 && ed.room_tool_active_for_test() && r477_count(ed, true) == 0);
    ed.step_frame_headless({r477_key(platform::KEY_ESCAPE)});
    ed.step_frame_headless({r477_key(platform::KEY_ESCAPE, false)});
    CHECK(!ed.room_tool_active_for_test());
    CHECK(ed.undo_depth_for_test() == depth);
  });

  test("blockout room tool: a Solid option makes a plain box that adds or subtracts, not a room", [=] {
    Editor ed;
    ground_scene(ed);
    ed.command("blockout roomopt solid on");
    ed.command("blockout roomopt subtract on");
    CHECK(drags(ed, {-1.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 1.0f}, {0.0f, 2.0f, 0.0f}));
    const BlockoutPiece *bp = r479_active(ed);
    CHECK(bp && !bp->room && bp->operation == 1 && bp->shape == 0);
    if (!bp) return;
    CHECK_NEAR(bp->size.y, 2.0f, 1e-3f);
  });

  test("blockout room tool: the console makes what the two drags make - blockout room, blockout room circle - and refuses numbers that are no room", [] {
    Editor ed;
    ed.init_headless(1600, 1000);
    r477_empty_scene(ed);
    ed.command("blockout room 0 0 4 3 2.5");
    const BlockoutPiece *bp = r479_active(ed);
    CHECK(bp && bp->room && bp->shape == 0);
    if (!bp) return;
    CHECK_NEAR(bp->size.x, 4.0f, 1e-5f);
    CHECK_NEAR(bp->size.y, 2.5f, 1e-5f);
    CHECK_NEAR(bp->size.z, 3.0f, 1e-5f);
    const Vec3 at = ed.active_object_for_test()->world_position();
    CHECK_NEAR(at.x, 2.0f, 1e-5f);
    CHECK_NEAR(at.y, 1.25f, 1e-5f);
    CHECK_NEAR(at.z, 1.5f, 1e-5f);
    ed.command("blockout room circle 8 8 1.5 3");
    const BlockoutPiece *c = r479_active(ed);
    CHECK(c && c->shape == 1 && c->room && std::fabs(c->radius - 1.5f) < 1e-5f && std::fabs(c->height - 3.0f) < 1e-5f);
    r477_settle(ed, 3);
    CHECK(r477_count(ed, false) == 1 && r477_count(ed, true) == 2 && r477_matches(ed));
    const size_t pieces = r477_count(ed, true), depth = ed.undo_depth_for_test();
    for (const char *line : {"blockout room", "blockout room 0 0 4 3", "blockout room 0 0 0 3 2", "blockout room 0 0 4 3 0", "blockout room 0 0 4 3 nan", "blockout room a b c d e",
                             "blockout room circle 1 1 0 2", "blockout room circle 1 1 -1 2", "blockout room circle 1 1 1", "blockout room 0 0 inf 3 2", "blockout roomopt", "blockout roomopt wall -1",
                             "blockout roomopt circle maybe", "blockout roomopt nonsense on"}) {
      const size_t mark = r477_mark();
      ed.command(line);
      size_t warnings = 0;
      r477_log_since(mark, &warnings);
      if (warnings == 0 || r477_count(ed, true) != pieces) std::printf("    '%s': warnings %zu, pieces %zu (was %zu)\n", line, warnings, r477_count(ed, true), pieces);
      CHECK(warnings >= 1 && r477_count(ed, true) == pieces);
    }
    CHECK(ed.undo_depth_for_test() == depth);
  });

  test("blockout room tool: it is reachable - the console, the toolbar button, Shift+R in every preset - and the hint line describes each state", [] {
    for (const char *preset : {"Unity", "Unreal", "Blender", "Maya", "3ds Max", "SketchUp", "Adobe"}) {
      const Keymap k = keymap_preset(preset);
      const auto it = k.find("tool.room");
      CHECK(it != k.end());
      if (it == k.end()) continue;
      for (const KeyChord &c : it->second) CHECK(keymap_conflicts(k, "tool.room", c).empty());
    }
    Editor ed;
    te_view(ed, "camera 10 70 14 0 0 0");
    ed.record_buttons_for_test(true);
    r477_settle(ed, 2);
    Recti r;
    CHECK(ed.button_rect_for_test("toolbar:room", r));
    r477_click(ed, r.x + r.w / 2, r.y + r.h / 2);
    CHECK(ed.room_tool_active_for_test());
    ed.set_scene_hovered_for_test(true);
    CHECK(ed.statusbar_hint_row_for_test() == "room tool" && ed.statusbar_hint_for_test().find("drag a rectangle") != std::string::npos);
    int ax = 0, ay = 0, bx = 0, by = 0;
    CHECK(ed.project_to_window({-2.0f, 0.0f, -1.0f}, ax, ay) && ed.project_to_window({2.0f, 0.0f, 1.0f}, bx, by));
    r479_press(ed, ax, ay);
    ed.set_scene_hovered_for_test(true);
    CHECK(ed.room_tool_stage_for_test() == 1 && ed.statusbar_hint_for_test().find("release") != std::string::npos);
    r479_move(ed, bx, by);
    r479_release(ed, bx, by);
    ed.set_scene_hovered_for_test(true);
    CHECK(ed.room_tool_stage_for_test() == 2 && ed.statusbar_hint_for_test().find("height") != std::string::npos);
    ed.step_frame_headless({r477_key(platform::KEY_ESCAPE)});  // (Esc leaves the tool; Shift+R, the Unity preset's key, brings it back)
    ed.step_frame_headless({r477_key(platform::KEY_ESCAPE, false)});
    CHECK(!ed.room_tool_active_for_test());
    te_press(ed, platform::KEY_R, platform::MOD_SHIFT);
    CHECK(ed.room_tool_active_for_test());
    ed.command("tool room");
    CHECK(ed.room_tool_active_for_test());
  });
}

static void tests_round479_f9() {
  test("blockout F9: the piece just made - by blockout add, a menu entry and the Room tool - has its sizes, thicknesses and operation in the panel, each re-run from where it started (no drift after ten changes)", [] {
    Editor ed;
    te_view(ed, "camera 10 70 14 0 0 0");
    ed.record_buttons_for_test(true);
    ed.command("blockout new");
    r477_settle(ed, 3);  // (its own step: the room's Undo goes back to this)
    ed.command("blockout add room box 4 3 4");
    r477_settle(ed, 3);
    CHECK(ed.obj_last_valid() && ed.obj_last_for_test().kind == "blockout");
    GameObject *room = ed.active_object_for_test();
    BlockoutPiece *bp = room ? room->get<BlockoutPiece>() : nullptr;
    CHECK(bp && bp->room);
    if (!bp) return;
    const float bottom = room->world_position().y - bp->size.y * 0.5f;
    const size_t steps = ed.undo_steps_for_test();
    CHECK(r479_type(ed, "adjust:Width", "6"));
    CHECK_NEAR(bp->size.x, 6.0f, 1e-4f);
    CHECK(bp->size.y == 3.0f && bp->size.z == 4.0f);
    CHECK(r479_type(ed, "adjust:Height", "5"));
    CHECK_NEAR(bp->size.y, 5.0f, 1e-4f);
    CHECK_NEAR(room->world_position().y - bp->size.y * 0.5f, bottom, 1e-4f);  // (the floor stayed)
    CHECK(r479_type(ed, "adjust:Wall", "0.4"));
    CHECK_NEAR(bp->wall, 0.4f, 1e-5f);
    CHECK(r479_type(ed, "adjust:Depth", "7"));
    CHECK_NEAR(bp->size.z, 7.0f, 1e-4f);
    CHECK(ed.undo_steps_for_test() == steps);  // (the same step, amended)
    for (int i = 1; i <= 10; i++) {
      CHECK(r479_type(ed, "adjust:Width", std::to_string(3 + i)));
      CHECK_NEAR(bp->size.x, (float)(3 + i), 1e-4f);
      CHECK(r479_type(ed, "adjust:Height", std::to_string(2 + i)));
      CHECK_NEAR(room->world_position().y - bp->size.y * 0.5f, bottom, 1e-4f);
    }
    CHECK(room->world_position().x == 0.0f || std::fabs(room->world_position().x) < 1e-4f);
    r477_settle(ed, 3);
    CHECK(r477_matches(ed));
    ed.command("undo");
    r477_settle(ed, 3);
    CHECK(r477_count(ed, true) == 1);  // (one Ctrl+Z goes back to before the room was added)
    /* A menu entry's piece and a plain box's operation. */
    ed.command("create Blockout Piece: Room Cylinder");
    r477_settle(ed, 3);
    CHECK(ed.obj_last_valid() && ed.obj_last_for_test().kind == "blockout" && ed.obj_last_for_test().n == 1);
    CHECK(r479_type(ed, "adjust:Radius", "1.5"));
    CHECK_NEAR(r479_active(ed)->radius, 1.5f, 1e-4f);
    ed.command("blockout add box 1 1 1");
    r477_settle(ed, 3);
    CHECK(ed.obj_last_valid());
    CHECK(r479_type(ed, "adjust:Operation", "1"));
    CHECK(r479_active(ed)->operation == 1);
  });

  test("blockout F9: a piece made by the Room tool can be adjusted, and anything else committed ends the panel", [] {
    Editor ed;
    te_view(ed, "camera 10 70 14 0 0 0");
    ed.record_buttons_for_test(true);
    ed.command("blockout room 0 0 4 3 2");
    r477_settle(ed, 3);
    CHECK(ed.obj_last_valid());
    CHECK(r479_type(ed, "adjust:Height", "4"));
    CHECK_NEAR(r479_active(ed)->size.y, 4.0f, 1e-4f);
    CHECK_NEAR(ed.active_object_for_test()->world_position().y - 2.0f, 0.0f, 1e-4f);
    ed.command("blockout set wall 0.3");
    r477_settle(ed, 3);
    CHECK(!ed.obj_last_valid());
  });
}

static void tests_round479_lesson() {
  test("blockout handles: the Learn tab has the lesson 'Dragging rooms out' with a Try-it of commands that run and three questions", [] {
    int found = -1;
    for (int i = 0; i < lesson_count(); i++)
      if (std::string(lesson(i).title) == "Dragging rooms out") found = i;
    CHECK(found >= 0);
    if (found < 0) return;
    const std::string body = lesson(found).body;
    size_t questions = 0, cmds = 0;
    std::istringstream is(body);
    Editor ed;
    ed.init_headless(900, 600);
    for (std::string line; std::getline(is, line);) {
      if (line.rfind("? ", 0) == 0) questions++;
      if (line.rfind("! cmd:", 0) != 0) continue;
      cmds++;
      ed.command(line.substr(6, line.find(" | ") - 6));
      r477_settle(ed, 2);
    }
    CHECK(questions == 3 && cmds >= 4);
    CHECK(r477_find(ed) && r477_count(ed, false) == 1 && r477_count(ed, true) == 2 && r477_matches(ed) && ed.room_tool_active_for_test());
    const BlockoutPiece *room = nullptr;
    for (GameObject *p : r477_all_pieces(*r477_find(ed)))
      if (p->get<BlockoutPiece>()->room) room = p->get<BlockoutPiece>();
    CHECK(room && std::fabs(room->wall - 0.3f) < 1e-5f);
  });
}

static void tests_round479() {
  tests_round479_lesson();
  tests_round479_gizmo_snap();
  tests_round479_handles();
  tests_round479_snapping();
  tests_round479_room_tool();
  tests_round479_f9();
}

/* ===================================================================== */
/* Round 479, the test engineer's: written from the task's criteria and risks */
/* ===================================================================== */

namespace {

/* A box piece's face (side -1 or +1 along its own X) in the world. */
Vec3 r479e_face(const GameObject &g, float side) { return g.world_matrix().point({side * g.get<BlockoutPiece>()->size.x * 0.5f, 0.0f, 0.0f}); }

/* The Room tool's three steps on the ground (or a surface): press, drag, release, then the height's click. */
bool r479e_room_drags(Editor &ed, Vec3 a, Vec3 b, Vec3 top, const char *typed = nullptr) {
  int ax = 0, ay = 0, bx = 0, by = 0, cx = 0, cy = 0;
  if (!ed.project_to_window(a, ax, ay) || !ed.project_to_window(b, bx, by) || !ed.project_to_window(top, cx, cy)) return false;
  r479_press(ed, ax, ay);
  r479_move(ed, bx, by);
  r479_release(ed, bx, by);
  r479_move(ed, cx, cy);
  if (typed)
    for (const char *c = typed; *c; c++) {
      platform::Event e;
      e.type = R477ET::Text, e.codepoint = (uint32_t)*c;
      ed.step_frame_headless({e});
    }
  r477_click(ed, cx, cy);
  r477_settle(ed, 3);
  return true;
}

size_t r479e_warnings(Editor &ed, const std::string &line) {
  const size_t mark = r477_mark();
  ed.command(line);
  size_t w = 0;
  r477_log_since(mark, &w);
  return w;
}

}  // namespace

static void tests_round479_engineer_handles() {
  test("blockout handles (te): under a turned, scaled and MIRRORED parent the face under the mouse moves a metre outward and the opposite face stays", [] {
    Editor ed;
    te_view(ed, kR479Camera);
    GameObject *bo = r477_blockout(ed);
    GameObject *par = ed.scene().create("Mirror", bo);
    par->set_local_rotation(Quat::axis_angle({0.0f, 1.0f, 0.0f}, 20.0f * kDeg2Rad));
    par->set_local_scale({-1.5f, 1.0f, 1.0f});
    GameObject *box = r477_piece(ed, *par, "Box", 0, 0, {0, 0, 0}, {3, 3, 3});
    ed.select_for_test(box->id, false);
    ed.mark_changed_for_test("test scene");
    r477_settle(ed, 3);
    const Vec3 far0 = r479e_face(*box, -1.0f), near0 = r479e_face(*box, 1.0f);
    const Vec3 out = normalize(near0 - far0);
    CHECK(r479_pull(ed, "+x", near0 + out * 1.0f, platform::MOD_CTRL));
    const BlockoutPiece *bp = box->get<BlockoutPiece>();
    CHECK(length(r479e_face(*box, -1.0f) - far0) < 1e-4f);
    const float world_move = dot(r479e_face(*box, 1.0f) - near0, out);
    std::printf("    mirrored: size.x %.4f, the +X face moved %.4f m outward\n", bp->size.x, world_move);
    CHECK_NEAR(world_move, 1.0f, 0.05f);
    CHECK(bp->size.y == 3.0f && bp->size.z == 3.0f);
  });

  test("blockout handles (te): a Blockout that is itself scaled 2 and turned 40 degrees - a world metre is half an own unit and the opposite face stays", [] {
    Editor ed;
    te_view(ed, kR479Camera);
    GameObject *bo = r477_blockout(ed);
    bo->set_local_rotation(Quat::axis_angle({0.0f, 1.0f, 0.0f}, 40.0f * kDeg2Rad));
    bo->set_local_scale(Vec3(2.0f));
    GameObject *box = r477_piece(ed, *bo, "Box", 0, 0, {0, 0, 0}, {2, 2, 2});
    ed.select_for_test(box->id, false);
    ed.mark_changed_for_test("test scene");
    r477_settle(ed, 3);
    const Vec3 far0 = r479e_face(*box, -1.0f), near0 = r479e_face(*box, 1.0f);
    const Vec3 out = normalize(near0 - far0);
    CHECK(r479_pull(ed, "+x", near0 + out * 1.0f, platform::MOD_CTRL));
    CHECK_NEAR(box->get<BlockoutPiece>()->size.x, 2.5f, 0.03f);
    CHECK(length(r479e_face(*box, -1.0f) - far0) < 1e-4f);
    CHECK(r477_matches(ed));
  });

  test("blockout handles (te): dragged by the mouse far past the opposite face the size stops at the small positive minimum and the opposite face stays", [] {
    Editor ed;
    GameObject *box = r479_piece_scene(ed, kR479Camera, 0, {0, 0, 0}, {2, 2, 2});
    const Vec3 far0 = r479e_face(*box, -1.0f);
    CHECK(r479_pull(ed, "+x", {-4.0f, 0.0f, 0.0f}, platform::MOD_CTRL));
    const BlockoutPiece *bp = box->get<BlockoutPiece>();
    CHECK(bp->size.x == kBlockoutMinSize);
    CHECK(length(r479e_face(*box, -1.0f) - far0) < 1e-4f);
    CHECK(box->world_matrix().dir({1.0f, 0.0f, 0.0f}).x > 0.0f);  // (not turned inside out)
    r477_settle(ed, 3);
    CHECK(r477_matches(ed));
  });

  test("blockout handles (te): every face handle of a box, by real mouse events, grows its own side by what was dragged and leaves the opposite face", [] {
    for (const char *name : {"+x", "-x", "+y", "-y", "+z", "-z"}) {
      Editor ed;
      GameObject *box = r479_piece_scene(ed, kR479Camera, 0, {0, 0, 0}, {4, 4, 4});
      const int axis = name[1] - 'x';
      Vec3 dir(0.0f);
      dir[axis] = name[0] == '+' ? 1.0f : -1.0f;
      const Vec3 grab = dir * 2.0f, opposite = dir * -2.0f;
      CHECK(r479_pull(ed, name, grab + dir * 1.0f, platform::MOD_CTRL));
      const BlockoutPiece *bp = box->get<BlockoutPiece>();
      std::printf("    handle %s: size %.3f %.3f %.3f\n", name, bp->size.x, bp->size.y, bp->size.z);
      CHECK_NEAR(bp->size[axis], 5.0f, 0.06f);
      CHECK(length(box->world_matrix().point(Vec3(0.0f)) - Vec3(0.0f)) > 0.0f);
      const float other_a = bp->size[(axis + 1) % 3], other_b = bp->size[(axis + 2) % 3];
      CHECK(other_a == 4.0f && other_b == 4.0f);
      /* the opposite face: own coordinate -dir*size/2 + position stays at `opposite` */
      Vec3 far = box->local().position;
      far[axis] -= (name[0] == '+' ? 1.0f : -1.0f) * bp->size[axis] * 0.5f;
      CHECK(length(far - opposite) < 1e-4f);
    }
  });

  test("blockout handles (te): Shift on every console handle grows both opposite sides about the centre (box, cylinder, room)", [] {
    Editor ed;
    GameObject *box = r479_piece_scene(ed, kR479Camera, 0, {1, 2, 3}, {2, 2, 2});
    for (const char *name : {"+x", "-x", "+y", "-y", "+z", "-z"}) {
      const BlockoutPiece before = *box->get<BlockoutPiece>();
      const Vec3 at = box->local().position;
      ed.command(std::string("blockout handle ") + name + " 0.5 both");
      const int axis = name[1] - 'x';
      CHECK_NEAR(box->get<BlockoutPiece>()->size[axis], before.size[axis] + 1.0f, 1e-5f);
      CHECK(r479_bits_equal(box->local().position, at));
    }
    Editor ed2;
    GameObject *cyl = r479_piece_scene(ed2, kR479Camera, 1, {0, 1, 0}, Vec3(1.0f));
    for (const char *name : {"top", "bottom"}) {
      const float h = cyl->get<BlockoutPiece>()->height;
      ed2.command(std::string("blockout handle ") + name + " 0.5 both");
      CHECK_NEAR(cyl->get<BlockoutPiece>()->height, h + 1.0f, 1e-5f);
      CHECK(cyl->local().position.y == 1.0f);
    }
    Editor ed3;
    ed3.init_headless(1600, 1000);
    GameObject *bo = nullptr;
    GameObject *room = r478e_room_scene(ed3, &bo);
    if (!room) { CHECK(false); return; }
    const Vec3 at = room->local().position;
    const float wall = room->get<BlockoutPiece>()->wall;
    ed3.command("blockout handle +z 1 both");
    CHECK_NEAR(room->get<BlockoutPiece>()->size.z, 6.0f, 1e-5f);
    CHECK(r479_bits_equal(room->local().position, at) && room->get<BlockoutPiece>()->wall == wall);
  });

  test("blockout handles (te): the wall handle never takes the wall below zero or past the cap, and a door's head and sill never cross", [] {
    Editor ed;
    ed.init_headless(1600, 1000);
    GameObject *bo = nullptr;
    GameObject *room = r478e_room_scene(ed, &bo);
    if (!room) { CHECK(false); return; }
    BlockoutPiece *rp = room->get<BlockoutPiece>();
    ed.command("blockout handle wall -5");
    CHECK(rp->wall == 0.0f);
    ed.command("blockout handle wall 5000");
    CHECK(std::isfinite(rp->wall) && rp->wall <= 1000.0f && rp->wall > 0.0f);
    r477_settle(ed, 3);
    ed.command("blockout add door");
    GameObject *door = ed.active_object_for_test();
    BlockoutPiece *dp = door ? door->get<BlockoutPiece>() : nullptr;
    CHECK(dp && dp->opening == 1);
    if (!dp) return;
    ed.command("blockout handle head -10");
    CHECK(dp->head > dp->sill);
    ed.command("blockout handle head 1");
    ed.command("blockout handle sill -50");
    std::printf("    door: sill %.3f head %.3f\n", dp->sill, dp->head);
    CHECK(dp->head > dp->sill && dp->sill >= 0.0f);
    ed.command("blockout handle width -50");
    CHECK(dp->width == kBlockoutMinSize);
  });

  test("blockout handles (te): a piece scaled to zero, a huge scale and a tiny scale - the console handle refuses or keeps the opposite face", [] {
    Editor ed;
    GameObject *box = r479_piece_scene(ed, kR479Camera, 0, {0, 0, 0}, {2, 2, 2});
    box->set_local_scale({0.0f, 1.0f, 1.0f});
    const BlockoutPiece before = *box->get<BlockoutPiece>();
    CHECK(r479e_warnings(ed, "blockout handle +x 1") >= 1);
    CHECK(box->get<BlockoutPiece>()->size.x == before.size.x && box->local().position.x == 0.0f);
    for (float s : {1e-3f, 1e3f}) {
      box->set_local_scale({s, 1.0f, 1.0f});
      box->get<BlockoutPiece>()->size = {2, 2, 2};
      box->set_local_position({0, 0, 0});
      const Vec3 far0 = r479e_face(*box, -1.0f);
      ed.command("blockout handle +x 1");
      const BlockoutPiece *bp = box->get<BlockoutPiece>();
      std::printf("    scale %g: size.x %g\n", s, bp->size.x);
      CHECK(std::isfinite(bp->size.x) && bp->size.x >= kBlockoutMinSize);
      CHECK(length(r479e_face(*box, -1.0f) - far0) < 1e-3f * std::max(1.0f, s));
    }
  });

  test("blockout handles (te): Undo and Redo of a drag restore the component and the transform byte for byte", [] {
    Editor ed;
    GameObject *box = r479_piece_scene(ed, kR479Camera, 0, {0, 0, 0}, {2, 2, 2});
    const BlockoutPiece before = *box->get<BlockoutPiece>();
    const Transform t0 = box->local();
    CHECK(r479_pull(ed, "+x", {3.0f, 0.0f, 0.0f}));
    const Vec3 size1 = box->get<BlockoutPiece>()->size, pos1 = box->local().position;
    CHECK(size1.x > 2.5f);
    ed.command("undo");
    r477_settle(ed, 3);
    box = ed.scene().find_by_name("Box");
    CHECK(box && r479_bits_equal(box->get<BlockoutPiece>()->size, before.size) && r479_bits_equal(box->local().position, t0.position));
    CHECK(r477_matches(ed));
    ed.command("redo");
    r477_settle(ed, 3);
    box = ed.scene().find_by_name("Box");
    CHECK(box && r479_bits_equal(box->get<BlockoutPiece>()->size, size1) && r479_bits_equal(box->local().position, pos1));
    CHECK(r477_matches(ed));
  });

  test("blockout handles (te): Esc after the face snapped to several planes puts the piece back byte for byte", [] {
    Editor ed;
    te_view(ed, "camera 20 55 22 3 0 0");
    GameObject *bo = r477_blockout(ed);
    r477_piece(ed, *bo, "A", 0, 0, {0, 0, 0}, {2, 2, 2});
    r477_piece(ed, *bo, "C", 0, 0, {0, 0, 5}, {2.5f, 2, 2});
    GameObject *b = r477_piece(ed, *bo, "B", 0, 0, {3.35f, 0, 0}, {2, 2, 2});
    ed.select_for_test(b->id, false);
    ed.mark_changed_for_test("test scene");
    r477_settle(ed, 3);
    const BlockoutPiece before = *b->get<BlockoutPiece>();
    const Transform t0 = b->local();
    const size_t depth = ed.undo_depth_for_test();
    int hx = 0, hy = 0;
    CHECK(ed.blockout_handle_pixel_for_test("-x", hx, hy));
    r479_press(ed, hx, hy);
    for (float x : {1.0f, 1.25f, 2.0f, 1.25f, 1.0f}) {
      int px = 0, py = 0;
      CHECK(ed.project_to_window({x, 0.0f, 0.0f}, px, py));
      r479_move(ed, px, py);
    }
    CHECK(ed.blockout_dragging_for_test());
    ed.step_frame_headless({r477_key(platform::KEY_ESCAPE)});
    ed.step_frame_headless({r477_key(platform::KEY_ESCAPE, false)});
    r479_release(ed, hx, hy);
    CHECK(!ed.blockout_dragging_for_test() && !ed.blockout_drag_snapped_for_test());
    CHECK(r479_bits_equal(b->get<BlockoutPiece>()->size, before.size) && r479_bits_equal(b->local().position, t0.position));
    CHECK(ed.undo_depth_for_test() == depth);
    CHECK(r477_matches(ed));
  });

  test("blockout handles (te): the piece deleted by a command in the middle of a drag - the mouse-up finds no drag, no crash, the mesh is the rest", [] {
    Editor ed;
    GameObject *bo = nullptr;
    GameObject *box = r479_piece_scene(ed, kR479Camera, 0, {0, 0, 0}, {2, 2, 2}, &bo);
    r477_piece(ed, *bo, "Keep", 0, 0, {8, 0, 0}, {2, 2, 2});
    int hx = 0, hy = 0, tx = 0, ty = 0;
    CHECK(ed.blockout_handle_pixel_for_test("+x", hx, hy) && ed.project_to_window({3.0f, 0.0f, 0.0f}, tx, ty));
    r479_press(ed, hx, hy);
    r479_move(ed, tx, ty);
    CHECK(ed.blockout_dragging_for_test());
    (void)box;
    ed.command("delete");
    r479_move(ed, tx + 5, ty);
    r479_release(ed, tx + 5, ty);
    CHECK(!ed.blockout_dragging_for_test());
    CHECK(!ed.scene().find_by_name("Box"));
    r477_settle(ed, 3);
    CHECK(r477_matches(ed));
  });

  test("blockout handles (te): two pieces selected - the handles are the active one's and only it changes", [] {
    Editor ed;
    GameObject *bo = nullptr;
    GameObject *a = r479_piece_scene(ed, kR479Camera, 0, {0, 0, 0}, {2, 2, 2}, &bo);
    GameObject *b = r477_piece(ed, *bo, "Other", 0, 0, {8, 0, 0}, {2, 2, 2});
    ed.select_for_test(b->id, false);
    ed.select_for_test(a->id, true);  // (a is active)
    r477_settle(ed, 2);
    CHECK(r479_pull(ed, "+x", {3.0f, 0.0f, 0.0f}));
    CHECK(a->get<BlockoutPiece>()->size.x > 2.5f);
    CHECK(b->get<BlockoutPiece>()->size.x == 2.0f && b->local().position.x == 8.0f);
  });

  test("blockout handles (te): a disabled piece, an inactive piece and Play show no handles; entering Play mid-drag ends the drag without a crash", [] {
    Editor ed;
    GameObject *box = r479_piece_scene(ed, kR479Camera, 0, {0, 0, 0}, {2, 2, 2});
    int x = 0, y = 0;
    CHECK(ed.blockout_handle_pixel_for_test("+x", x, y));
    box->get<BlockoutPiece>()->enabled = false;
    CHECK(!ed.blockout_handle_pixel_for_test("+x", x, y));
    box->get<BlockoutPiece>()->enabled = true;
    box->active = false;
    CHECK(!ed.blockout_handle_pixel_for_test("+x", x, y));
    box->active = true;
    int tx = 0, ty = 0;
    CHECK(ed.blockout_handle_pixel_for_test("+x", x, y) && ed.project_to_window({3.0f, 0.0f, 0.0f}, tx, ty));
    r479_press(ed, x, y);
    r479_move(ed, tx, ty);
    CHECK(ed.blockout_dragging_for_test());
    ed.command("play");
    r477_settle(ed, 3);
    CHECK(!ed.blockout_dragging_for_test());
    CHECK(!ed.blockout_handle_pixel_for_test("+x", x, y) || !ed.playing_for_test());
    r479_release(ed, tx, ty);
    ed.command("stop");
    r477_settle(ed, 3);
    CHECK(!ed.playing_for_test() && !ed.blockout_dragging_for_test());
    CHECK(r477_matches(ed));
  });

  test("blockout handles (te): a drag with workers on - the mesh at the end is the build from scratch of the final pieces", [] {
    Editor ed;
    ed.set_async_jobs_for_test(true);
    GameObject *bo = nullptr;
    GameObject *box = r479_piece_scene(ed, kR479Camera, 0, {0, 0, 0}, {2, 2, 2}, &bo);
    r477_piece(ed, *bo, "Cut", 1, 1, {0, 0, 0}, Vec3(1.0f), 0.4f, 6.0f);
    ed.command("blockout wait");
    int hx = 0, hy = 0;
    CHECK(ed.blockout_handle_pixel_for_test("+x", hx, hy));
    r479_press(ed, hx, hy);
    for (float x : {2.5f, 3.0f, 3.5f, 4.0f, 3.0f}) {
      int px = 0, py = 0;
      CHECK(ed.project_to_window({x, 0.0f, 0.0f}, px, py));
      r479_move(ed, px, py);
    }
    int ex = 0, ey = 0;
    CHECK(ed.project_to_window({3.0f, 0.0f, 0.0f}, ex, ey));
    r479_release(ed, ex, ey);
    ed.command("blockout wait");
    r477_settle(ed, 3);
    ed.command("blockout wait");
    CHECK(box->get<BlockoutPiece>()->size.x > 3.5f);
    CHECK(r477_matches(ed));
  });

  test("blockout handles (te): a handle and the transform gizmo - the handle at the face wins the press, and the gizmo's X arrow is still reachable from the centre", [] {
    Editor ed;
    GameObject *box = r479_piece_scene(ed, kR479Camera, 0, {0, 0, 0}, {2, 2, 2});
    ed.command("tool move");
    r477_settle(ed, 2);
    const Vec3 at = box->local().position;
    int hx = 0, hy = 0;
    CHECK(ed.blockout_handle_pixel_for_test("+x", hx, hy));
    r479_press(ed, hx, hy);
    CHECK(ed.blockout_dragging_for_test());
    r479_release(ed, hx, hy);
    CHECK(r479_bits_equal(box->local().position, at));  // (the gizmo did not move the piece)
    ed.command("perf drag gizmo x 40 frames 4");
    r477_settle(ed, 3);
    CHECK(box->local().position.x != at.x || box->local().position.z != at.z || box->local().position.y != at.y);
  });
}

static void tests_round479_engineer_snapping() {
  test("blockout handles (te): pieces of ANOTHER Blockout are no snap candidates, the same Blockout's are", [] {
    for (int same = 0; same < 2; same++) {
      Editor ed;
      te_view(ed, "camera 20 55 22 3 0 0");
      GameObject *bo = r477_blockout(ed, "Blockout");
      GameObject *other = same ? bo : r477_blockout(ed, "Other");
      GameObject *b = r477_piece(ed, *bo, "B", 0, 0, {3.35f, 0, 0}, {2, 2, 2});
      r477_piece(ed, *other, "A", 0, 0, {0, 0, 0}, {2.2f, 2, 2});  // (its +X face is at 1.1, off the 0.25 grid)
      ed.select_for_test(b->id, false);
      ed.mark_changed_for_test("test scene");
      r477_settle(ed, 3);
      bool snapped = false;
      CHECK(r479_pull(ed, "-x", {1.1f, 0.0f, 0.0f}, 0, &snapped));
      const float face = r479e_face(*b, -1.0f).x;
      std::printf("    %s Blockout: face at %.4f, snapped %d\n", same ? "same" : "another", face, (int)snapped);
      if (same) CHECK(snapped && std::fabs(face - 1.1f) < 1e-4f);
      else CHECK(!snapped && std::fabs(face - 1.0f) < 1e-4f);
    }
  });

  test("blockout handles (te): between two planes 6 cm apart the face stays on the plane it took - no jitter while the mouse crosses both", [] {
    Editor ed;
    te_view(ed, "camera 20 55 22 3 0 0");
    GameObject *bo = r477_blockout(ed);
    r477_piece(ed, *bo, "A", 0, 0, {0, 0, 0}, {2, 2, 2});
    r477_piece(ed, *bo, "C", 0, 0, {0, 0, 5}, {2.12f, 2, 2});  // (+X face at 1.06)
    GameObject *b = r477_piece(ed, *bo, "B", 0, 0, {3.35f, 0, 0}, {2, 2, 2});
    ed.select_for_test(b->id, false);
    ed.mark_changed_for_test("test scene");
    r477_settle(ed, 3);
    int hx = 0, hy = 0;
    CHECK(ed.blockout_handle_pixel_for_test("-x", hx, hy));
    r479_press(ed, hx, hy);
    bool first = true;
    float held = 0.0f;
    for (float x : {1.0f, 1.03f, 1.06f, 1.03f, 1.0f, 1.06f, 1.02f}) {
      int px = 0, py = 0;
      CHECK(ed.project_to_window({x, 0.0f, 0.0f}, px, py));
      r479_move(ed, px, py);
      const float face = r479e_face(*b, -1.0f).x;
      CHECK(ed.blockout_drag_snapped_for_test());
      if (first) held = face, first = false;
      if (std::fabs(face - held) > 1e-5f) std::printf("    mouse at %.2f: face %.4f, held %.4f\n", x, face, held);
      CHECK(std::fabs(face - held) < 1e-5f);
    }
    r479_release(ed, hx, hy);
  });

  test("blockout handles (te): a snap to a corner - a face dragged level with the top of a lower piece lands in that corner's plane", [] {
    Editor ed;
    te_view(ed, "camera 30 40 12 0 0 0");
    GameObject *bo = r477_blockout(ed);
    r477_piece(ed, *bo, "Low", 0, 0, {0, 0, 0}, {2, 1.3f, 2});  // (its top corner plane is y = 0.65, off the grid)
    GameObject *b = r477_piece(ed, *bo, "B", 0, 0, {6, 0, 0}, {2, 3, 2});
    ed.select_for_test(b->id, false);
    ed.mark_changed_for_test("test scene");
    r477_settle(ed, 3);
    bool snapped = false;
    CHECK(r479_pull(ed, "-y", {6.0f, -0.65f, 0.0f}, 0, &snapped));
    const Vec3 bottom = b->world_matrix().point({0.0f, -b->get<BlockoutPiece>()->size.y * 0.5f, 0.0f});
    std::printf("    -Y face at y = %.4f, snapped %d\n", bottom.y, (int)snapped);
    CHECK(snapped && std::fabs(bottom.y + 0.65f) < 1e-4f);
  });
}

static void tests_round479_engineer_room() {
  test("blockout room tool (te): a rectangle dragged in each of the four directions makes the same room (negative extents)", [] {
    const Vec3 corners[4][2] = {{{-2, 0, -1}, {2, 0, 1}}, {{2, 0, 1}, {-2, 0, -1}}, {{-2, 0, 1}, {2, 0, -1}}, {{2, 0, -1}, {-2, 0, 1}}};
    for (int k = 0; k < 4; k++) {
      Editor ed;
      te_view(ed, "camera 10 70 14 0 0 0");
      ed.command("tool room");
      r477_settle(ed, 2);
      CHECK(r479e_room_drags(ed, corners[k][0], corners[k][1], {0.0f, 3.0f, 0.0f}));
      const BlockoutPiece *bp = r479_active(ed);
      CHECK(bp && bp->room);
      if (!bp) continue;
      const Vec3 at = ed.active_object_for_test()->world_position();
      std::printf("    direction %d: size %.3f %.3f %.3f at %.3f %.3f %.3f\n", k, bp->size.x, bp->size.y, bp->size.z, at.x, at.y, at.z);
      CHECK_NEAR(bp->size.x, 4.0f, 1e-3f);
      CHECK_NEAR(bp->size.y, 3.0f, 1e-3f);
      CHECK_NEAR(bp->size.z, 2.0f, 1e-3f);
      CHECK(std::fabs(at.x) < 1e-3f && std::fabs(at.z) < 1e-3f && std::fabs(at.y - 1.5f) < 1e-3f);
      CHECK(r477_matches(ed));
    }
  });

  test("blockout room tool (te): the height typed as a number is the room's height, and two rooms in a row each undo as one step", [] {
    Editor ed;
    te_view(ed, "camera 10 70 14 0 0 0");
    ed.command("tool room");
    r477_settle(ed, 2);
    const size_t depth = ed.undo_depth_for_test();
    CHECK(r479e_room_drags(ed, {-4.0f, 0.0f, -1.0f}, {-1.0f, 0.0f, 1.0f}, {-2.0f, 2.0f, 0.0f}, "2.5"));
    const BlockoutPiece *first = r479_active(ed);
    CHECK(first && first->room);
    if (!first) return;
    CHECK_NEAR(first->size.y, 2.5f, 1e-3f);
    CHECK(ed.undo_depth_for_test() == depth + 1 && ed.room_tool_active_for_test() && ed.room_tool_stage_for_test() == 0);
    CHECK(r479e_room_drags(ed, {1.0f, 0.0f, -1.0f}, {4.0f, 0.0f, 1.0f}, {2.0f, 3.0f, 0.0f}));
    CHECK(ed.undo_depth_for_test() == depth + 2);
    CHECK(r477_count(ed, true) == 2 && r477_count(ed, false) == 1 && r477_matches(ed));  // (the second goes into the first's Blockout)
    ed.command("undo");
    r477_settle(ed, 3);
    CHECK(r477_count(ed, true) == 1 && r477_matches(ed));
    ed.command("undo");
    r477_settle(ed, 3);
    CHECK(r477_count(ed, true) == 0);
  });

  test("blockout room tool (te): the room goes into the Blockout of the selected piece, else the selected Blockout, else a new one", [] {
    Editor ed;
    te_view(ed, "camera 10 70 14 0 0 0");
    GameObject *bo1 = r477_blockout(ed, "Blockout");
    GameObject *bo2 = r477_blockout(ed, "Second");
    r477_piece(ed, *bo1, "P1", 0, 0, {-20, 0, 0}, {2, 2, 2});
    GameObject *p2 = r477_piece(ed, *bo2, "P2", 0, 0, {20, 0, 0}, {2, 2, 2});
    GameObject *plain = ed.scene().create("Plain");
    r477_settle(ed, 3);
    ed.select_for_test(p2->id, false);
    ed.command("blockout room 0 0 3 3 2");
    CHECK(blockout_of_piece(*ed.active_object_for_test()) == bo2);
    ed.select_for_test(bo1->id, false);
    ed.command("blockout room 0 5 3 8 2");
    CHECK(blockout_of_piece(*ed.active_object_for_test()) == bo1);
    const size_t blockouts = r477_count(ed, false);
    ed.select_for_test(plain->id, false);
    ed.command("blockout room 0 10 3 13 2");
    GameObject *made = ed.active_object_for_test();
    CHECK(made && made->get<BlockoutPiece>() && r477_count(ed, false) == blockouts + 1);
    GameObject *owner = made ? blockout_of_piece(*made) : nullptr;
    CHECK(owner && owner != bo1 && owner != bo2);
    r477_settle(ed, 3);
    CHECK(r477_matches(ed, "Blockout") && r477_matches(ed, "Second"));
  });

  test("blockout room tool (te): the wall option 0 and a huge wall make a piece with those numbers and leave a structurally sound scene", [] {
    Editor ed;
    ed.init_headless(1600, 1000);
    r477_empty_scene(ed);
    ed.command("blockout roomopt wall 0");
    ed.command("blockout room 0 0 4 3 2");
    const BlockoutPiece *bp = r479_active(ed);
    CHECK(bp && bp->room && bp->wall == 0.0f && bp->floor == 0.0f && bp->ceiling == 0.0f);
    r477_settle(ed, 3);
    CHECK(r477_matches(ed));
    ed.command("blockout roomopt wall 1000");
    ed.command("blockout room 10 0 14 3 2");
    bp = r479_active(ed);
    CHECK(bp && bp->room && bp->wall == 1000.0f);
    r477_settle(ed, 3);
    ed.command("blockout wait");
    const Mesh *m = r477_mesh(r477_find(ed));
    CHECK(m != nullptr);
    CHECK(ed.undo_depth_for_test() >= 2);
  });

  test("blockout room tool (te): a solid Add and a solid Subtract by the tool's two drags, a Circle solid is a plain cylinder", [] {
    for (int sub = 0; sub < 2; sub++) {
      Editor ed;
      te_view(ed, "camera 10 70 14 0 0 0");
      ed.command("blockout roomopt solid on");
      ed.command(std::string("blockout roomopt subtract ") + (sub ? "on" : "off"));
      ed.command("blockout roomopt circle on");
      ed.command("tool room");
      r477_settle(ed, 2);
      CHECK(r479e_room_drags(ed, {0.0f, 0.0f, 0.0f}, {1.5f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f}));
      const BlockoutPiece *bp = r479_active(ed);
      CHECK(bp && !bp->room && bp->shape == 1 && bp->operation == sub);
      if (bp) CHECK_NEAR(bp->radius, 1.5f, 1e-3f);
    }
  });

  test("blockout room tool (te): a Room tool drag on a slanted face stands the room on that face, turned to it", [] {
    Editor ed;
    te_view(ed, "camera 12 50 14 0 1 0");
    GameObject *bo = r477_blockout(ed);
    GameObject *slab = r477_piece(ed, *bo, "Slab", 0, 0, {0, 0, 0}, {6, 1, 6});
    slab->set_local_rotation(Quat::axis_angle({0.0f, 0.0f, 1.0f}, 20.0f * kDeg2Rad));
    ed.select_for_test(slab->id, false);
    ed.mark_changed_for_test("test scene");
    r477_settle(ed, 3);
    ed.command("tool room");
    r477_settle(ed, 2);
    const Mat4 w = slab->world_matrix();
    const Vec3 a = w.point({-0.5f, 0.5f, -0.5f}), b = w.point({0.5f, 0.5f, 0.5f}), top = w.point({0.0f, 1.5f, 0.0f});
    CHECK(r479e_room_drags(ed, a, b, top));
    const GameObject *room = ed.active_object_for_test();
    const BlockoutPiece *bp = room ? room->get<BlockoutPiece>() : nullptr;
    CHECK(bp && bp->room && room != slab);
    if (!bp) return;
    const Vec3 up = normalize(room->world_matrix().dir({0.0f, 1.0f, 0.0f})), face_n = normalize(w.dir({0.0f, 1.0f, 0.0f}));
    std::printf("    room up (%.3f %.3f %.3f) vs face normal (%.3f %.3f %.3f)\n", up.x, up.y, up.z, face_n.x, face_n.y, face_n.z);
    CHECK(dot(up, face_n) > 0.99f);
    CHECK(r477_matches(ed));
  });

  test("blockout room tool (te): a new scene in the middle of the height step leaves no half-made room that a click could finish in the new scene", [] {
    Editor ed;
    te_view(ed, "camera 10 70 14 0 0 0");
    ed.command("tool room");
    r477_settle(ed, 2);
    CHECK(r479e_room_drags(ed, {-2.0f, 0.0f, -1.0f}, {2.0f, 0.0f, 1.0f}, {0.0f, 3.0f, 0.0f}) == true);
    int ax = 0, ay = 0, bx = 0, by = 0;
    CHECK(ed.project_to_window({-2.0f, 0.0f, -1.0f}, ax, ay) && ed.project_to_window({2.0f, 0.0f, 1.0f}, bx, by));
    r479_press(ed, ax, ay);
    r479_move(ed, bx, by);
    r479_release(ed, bx, by);
    CHECK(ed.room_tool_stage_for_test() == 2);
    ed.command("newscene");
    r477_settle(ed, 3);
    std::printf("    after newscene: tool active %d, stage %d\n", (int)ed.room_tool_active_for_test(), ed.room_tool_stage_for_test());
    const bool stale = ed.room_tool_active_for_test() && ed.room_tool_stage_for_test() == 2;
    CHECK(!ed.room_tool_active_for_test() || ed.room_tool_stage_for_test() == 0);
    if (stale) {  // (what the stale stage does: a click in the empty new scene finishes the old scene's room)
      int cx = 0, cy = 0;
      if (ed.project_to_window({0.0f, 2.0f, 0.0f}, cx, cy)) r477_click(ed, cx, cy);
      r477_settle(ed, 3);
      std::printf("    a click in the new scene made %zu piece(s) from the old footprint\n", r477_count(ed, true));
    }
  });
}

static void tests_round479_engineer_console() {
  test("blockout handle (te): with nothing, the Blockout itself or a plain object selected it warns and changes nothing", [] {
    Editor ed;
    te_view(ed, kR479Camera);
    GameObject *bo = r477_blockout(ed);
    GameObject *plain = ed.scene().create("Plain");
    r477_settle(ed, 2);
    ed.clear_selection_for_test();
    const size_t depth = ed.undo_depth_for_test();
    CHECK(r479e_warnings(ed, "blockout handle +x 1") >= 1);
    ed.select_for_test(bo->id, false);
    CHECK(r479e_warnings(ed, "blockout handle +x 1") >= 1);
    ed.select_for_test(plain->id, false);
    CHECK(r479e_warnings(ed, "blockout handle +x 1") >= 1);
    CHECK(ed.undo_depth_for_test() == depth);
  });

  test("blockout handle (te): words, nan text, zero and a metre count of zero - zero changes nothing and adds no undo step", [] {
    Editor ed;
    GameObject *box = r479_piece_scene(ed, kR479Camera, 0, {0, 0, 0}, {2, 2, 2});
    const size_t depth = ed.undo_depth_for_test();
    ed.command("blockout handle +x 0");
    CHECK(box->get<BlockoutPiece>()->size.x == 2.0f && ed.undo_depth_for_test() == depth);
    for (const char *line : {"blockout handle +x one", "blockout handle +x NaN", "blockout handle +x 1e999", "blockout handle bogus 1", "blockout handle -q 1"}) {
      const size_t w = r479e_warnings(ed, line);
      if (w == 0) std::printf("    '%s' did not warn\n", line);
      CHECK(w >= 1);
      CHECK(box->get<BlockoutPiece>()->size.x == 2.0f);
    }
    CHECK(ed.undo_depth_for_test() == depth);
  });

  test("blockout room (te): negative height makes a room standing up (solid: a negative height goes down), a footprint with nan or huge numbers is refused or finite", [] {
    Editor ed;
    ed.init_headless(1600, 1000);
    r477_empty_scene(ed);
    ed.command("blockout room 0 0 4 3 -2");
    const BlockoutPiece *bp = r479_active(ed);
    CHECK(bp && bp->room && std::fabs(bp->size.y - 2.0f) < 1e-5f);
    if (bp) CHECK(ed.active_object_for_test()->world_position().y > 0.0f);
    const size_t pieces = r477_count(ed, true);
    CHECK(r479e_warnings(ed, "blockout room 0 0 4 3 nan") >= 1);
    CHECK(r479e_warnings(ed, "blockout room circle 0 0 nan 2") >= 1);
    CHECK(r477_count(ed, true) == pieces);
    ed.command("blockout room 0 0 1e30 3 2");
    r477_settle(ed, 2);
    GameObject *g = ed.active_object_for_test();
    CHECK(g && g->get<BlockoutPiece>());
    if (g && g->get<BlockoutPiece>() && r477_count(ed, true) > pieces) {
      const Vec3 s = g->get<BlockoutPiece>()->size;
      CHECK(std::isfinite(s.x) && std::isfinite(s.y) && std::isfinite(s.z));
    }
  });
}

static void tests_round479_engineer_f9() {
  test("blockout F9 (te): a piece deleted after it was made ends the panel; an Undo ends it; another piece made after it takes the panel and leaves the first alone", [] {
    Editor ed;
    te_view(ed, "camera 10 70 14 0 0 0");
    ed.record_buttons_for_test(true);
    ed.command("blockout new");
    r477_settle(ed, 3);
    ed.command("blockout add room box 4 3 4");
    r477_settle(ed, 3);
    GameObject *room = ed.active_object_for_test();
    CHECK(room && ed.obj_last_valid());
    if (!room) return;
    const uint64_t room_id = room->id;
    ed.command("blockout add box 1 1 1");
    r477_settle(ed, 3);
    CHECK(ed.obj_last_valid());
    GameObject *box = ed.active_object_for_test();
    CHECK(box && box->id != room_id);
    CHECK(r479_type(ed, "adjust:Width", "9"));
    CHECK_NEAR(box->get<BlockoutPiece>()->size.x, 9.0f, 1e-4f);
    GameObject *room_now = ed.scene().find(room_id);
    CHECK(room_now && room_now->get<BlockoutPiece>()->size.x == 4.0f);
    ed.command("undo");
    r477_settle(ed, 3);
    CHECK(!ed.obj_last_valid());
    ed.command("blockout add box 2 2 2");
    r477_settle(ed, 3);
    CHECK(ed.obj_last_valid());
    ed.command("delete");
    r477_settle(ed, 3);
    std::printf("    after deleting the piece the panel is %s\n", ed.obj_last_valid() ? "still showing" : "gone");
    CHECK(!ed.obj_last_valid());
  });

  test("blockout F9 (te): a door's Width, Head and Sill rows re-run from where it started and keep Head above Sill", [] {
    Editor ed;
    ed.init_headless(1600, 1000);
    GameObject *bo = nullptr;
    GameObject *room = r478e_room_scene(ed, &bo);
    if (!room) { CHECK(false); return; }
    ed.record_buttons_for_test(true);
    ed.command("blockout add door");
    r477_settle(ed, 3);
    GameObject *door = ed.active_object_for_test();
    BlockoutPiece *dp = door ? door->get<BlockoutPiece>() : nullptr;
    CHECK(dp && dp->opening == 1 && ed.obj_last_valid());
    if (!dp) return;
    const Vec3 at = door->local().position;
    CHECK(r479_type(ed, "adjust:Width", "1.4"));
    CHECK_NEAR(dp->width, 1.4f, 1e-4f);
    CHECK(r479_type(ed, "adjust:Head", "1.8"));
    CHECK_NEAR(dp->head, 1.8f, 1e-4f);
    CHECK(r479_type(ed, "adjust:Sill", "2.5"));
    std::printf("    door after Sill 2.5: sill %.3f head %.3f\n", dp->sill, dp->head);
    CHECK(dp->head > dp->sill);
    CHECK(r479_bits_equal(door->local().position, at));
    r477_settle(ed, 3);
    CHECK(r477_matches(ed));
  });
}

static void tests_round479_engineer() {
  tests_round479_engineer_handles();
  tests_round479_engineer_snapping();
  tests_round479_engineer_room();
  tests_round479_engineer_console();
  tests_round479_engineer_f9();
}

/* ===================================================================== */
/* Round 479: the code review's findings                                  */
/* ===================================================================== */

namespace {

/* A character typed as a user types it: the key, the text it makes, the key up. */
void r479r_type(Editor &ed, int key, char c) {
  ed.step_frame_headless({r477_key(key)});
  platform::Event e;
  e.type = R477ET::Text, e.codepoint = (uint32_t)c;
  ed.step_frame_headless({e});
  ed.step_frame_headless({r477_key(key, false)});
}
void r479r_key(Editor &ed, int key) {
  ed.step_frame_headless({r477_key(key)});
  ed.step_frame_headless({r477_key(key, false)});
}

/* A round handle taken and pulled 40 pixels away from the piece's centre on the screen, then let go. False when it has no handle in view. */
bool r479r_pull_round(Editor &ed, const char *handle) {
  int hx = 0, hy = 0, cx = 0, cy = 0;
  if (!ed.blockout_handle_pixel_for_test(handle, hx, hy) || !ed.project_to_window({0.0f, 0.0f, 0.0f}, cx, cy)) return false;
  const float dx = (float)(hx - cx), dy = (float)(hy - cy), len = std::sqrt(dx * dx + dy * dy);
  if (len < 3.0f) return false;
  const int tx = hx + (int)std::lround(dx / len * 40.0f), ty = hy + (int)std::lround(dy / len * 40.0f);
  r479_press(ed, hx, hy);
  if (!ed.blockout_dragging_for_test()) return false;
  r479_move(ed, (hx + tx) / 2, (hy + ty) / 2);
  r479_move(ed, tx, ty);
  r479_release(ed, tx, ty);
  return true;
}

}  // namespace

static void tests_round479_review() {
  test("blockout room tool: typing a height and correcting it with Backspace does not delete the room made before, a typed '.' leaves the view alone and Ctrl+Z in the middle of a handle drag changes nothing", [] {
    Editor ed;
    te_view(ed, "camera 10 70 14 0 0 0");
    ed.command("blockout room 0 0 4 3 2");
    r477_settle(ed, 3);
    const uint64_t first = ed.active_object_for_test() ? ed.active_object_for_test()->id : 0;
    ed.command("tool room");
    r477_settle(ed, 2);
    int ax = 0, ay = 0, bx = 0, by = 0;
    CHECK(ed.project_to_window({6.0f, 0.0f, -1.0f}, ax, ay) && ed.project_to_window({8.0f, 0.0f, 1.0f}, bx, by));
    r479_press(ed, ax, ay);
    r479_move(ed, bx, by);
    r479_release(ed, bx, by);
    CHECK(ed.room_tool_stage_for_test() == 2);
    const Vec3 eye = ed.scene_eye();
    r479r_type(ed, platform::KEY_PERIOD, '.');
    r479r_key(ed, platform::KEY_BACKSPACE);
    r479r_type(ed, platform::KEY_3, '3');
    r479r_key(ed, platform::KEY_BACKSPACE);
    r479r_type(ed, platform::KEY_2, '2');
    CHECK(ed.scene().find(first) && r477_count(ed, true) == 1);  // (the first room is still there)
    CHECK(length(ed.scene_eye() - eye) < 1e-6f);
    r479r_key(ed, platform::KEY_ENTER);
    r477_settle(ed, 3);
    const BlockoutPiece *bp = r479_active(ed);
    CHECK(bp && ed.active_object_for_test()->id != first && r477_count(ed, true) == 2);
    if (bp) CHECK_NEAR(bp->size.y, 2.0f, 1e-3f);
    CHECK(ed.scene().find(first));

    Editor ed2;
    GameObject *box = r479_piece_scene(ed2, kR479Camera, 0, {0, 0, 0}, {2, 2, 2});
    const size_t depth = ed2.undo_depth_for_test();
    int hx = 0, hy = 0, tx = 0, ty = 0;
    CHECK(ed2.blockout_handle_pixel_for_test("+x", hx, hy) && ed2.project_to_window({2.0f, 0.0f, 0.0f}, tx, ty));
    r479_press(ed2, hx, hy);
    r479_move(ed2, tx, ty);
    ed2.step_frame_headless({te_key(platform::KEY_Z, platform::MOD_CTRL)});
    ed2.step_frame_headless({te_key(platform::KEY_Z, platform::MOD_CTRL, false)});
    CHECK(ed2.blockout_dragging_for_test() && box->get<BlockoutPiece>()->size.x > 2.5f);  // (the drag goes on)
    r479_release(ed2, tx, ty);
    CHECK(ed2.undo_depth_for_test() == depth + 1 && box->get<BlockoutPiece>()->size.x > 2.5f);
  });

  test("blockout handles: a sphere's radius handle and a cylinder's side handle with the camera level can be pulled with the mouse, a box seen from straight above shows no top or bottom handle, and the gizmo's centre on a sphere is still pressable", [] {
    {
      Editor ed;
      GameObject *sph = r479_piece_scene(ed, kR479Camera, 2, {0, 0, 0}, Vec3(1.0f));
      CHECK(r479r_pull_round(ed, "radius"));
      CHECK(sph->get<BlockoutPiece>()->radius > 0.6f);
      CHECK(r477_matches(ed));
    }
    {
      Editor ed;
      GameObject *cyl = r479_piece_scene(ed, "camera 30 0 12 0 0 0", 1, {0, 0, 0}, Vec3(1.0f));
      CHECK(r479r_pull_round(ed, "side"));
      CHECK(cyl->get<BlockoutPiece>()->radius > 0.6f);
    }
    {
      Editor ed;
      r479_piece_scene(ed, "camera 0 89 12 0 0 0", 0, {0, 0, 0}, {2, 2, 2});
      int x = 0, y = 0;
      CHECK(!ed.blockout_handle_pixel_for_test("+y", x, y) && !ed.blockout_handle_pixel_for_test("-y", x, y));  // (their lines are the viewer's own)
      CHECK(ed.blockout_handle_pixel_for_test("+x", x, y));
    }
    {
      Editor ed;
      r479_piece_scene(ed, kR479Camera, 2, {0, 0, 0}, Vec3(1.0f));
      int cx = 0, cy = 0;
      CHECK(ed.project_to_window({0.0f, 0.0f, 0.0f}, cx, cy));
      r479_press(ed, cx, cy);
      CHECK(!ed.blockout_dragging_for_test());  // (the centre is the gizmo's, not a handle's)
      r479_release(ed, cx, cy);
    }
  });

  test("blockout handles: pressing a handle and letting go without moving changes nothing, on a box off the grid and with another piece's plane a few pixels away", [] {
    Editor ed;
    GameObject *box = r479_piece_scene(ed, kR479Camera, 0, {0, 0, 0}, {2.1f, 2, 2});
    const size_t depth = ed.undo_depth_for_test();
    int hx = 0, hy = 0;
    CHECK(ed.blockout_handle_pixel_for_test("+x", hx, hy));
    r479_press(ed, hx, hy);
    r479_release(ed, hx, hy);
    CHECK(box->get<BlockoutPiece>()->size.x == 2.1f && box->local().position.x == 0.0f && ed.undo_depth_for_test() == depth);
    Editor ed2;
    te_view(ed2, "camera 20 55 22 3 0 0");
    GameObject *bo = r477_blockout(ed2);
    bo->set_world_position({0.0f, 0.0f, 0.0f});
    r477_piece(ed2, *bo, "A", 0, 0, {0, 0, 0}, {2, 2, 2});
    GameObject *b = r477_piece(ed2, *bo, "B", 0, 0, {2.1f, 0, 0}, {2, 2, 2});  // (B's -X face at 1.1, A's +X face at 1: about 4 pixels apart)
    ed2.select_for_test(b->id, false);
    ed2.mark_changed_for_test("test scene");
    r477_settle(ed2, 3);
    const size_t depth2 = ed2.undo_depth_for_test();
    CHECK(ed2.blockout_handle_pixel_for_test("-x", hx, hy));
    r479_press(ed2, hx, hy);
    r479_release(ed2, hx, hy);
    CHECK(b->get<BlockoutPiece>()->size.x == 2.0f && b->local().position.x == 2.1f && ed2.undo_depth_for_test() == depth2);
  });

  test("blockout room tool: a solid dug downward keeps the surface where it is when F9 changes its height; a Blockout scaled 2 gets a room of the dragged size, an unevenly scaled one a warning and nothing", [] {
    {
      Editor ed;
      te_view(ed, "camera 10 70 14 0 0 0");
      ed.record_buttons_for_test(true);
      ed.command("tool room");
      ed.command("blockout roomopt solid on");
      ed.command("blockout roomopt subtract on");
      r477_settle(ed, 2);
      int ax = 0, ay = 0, bx = 0, by = 0, cx = 0, cy = 0;
      CHECK(ed.project_to_window({-1.0f, 0.0f, -1.0f}, ax, ay) && ed.project_to_window({1.0f, 0.0f, 1.0f}, bx, by) && ed.project_to_window({0.0f, -2.0f, 0.0f}, cx, cy));
      r479_press(ed, ax, ay);
      r479_move(ed, bx, by);
      r479_release(ed, bx, by);
      r479_move(ed, cx, cy);
      r477_click(ed, cx, cy);
      r477_settle(ed, 3);
      const GameObject *g = ed.active_object_for_test();
      const BlockoutPiece *bp = r479_active(ed);
      CHECK(bp && bp->operation == 1 && !bp->room);
      if (!bp) return;
      CHECK_NEAR(g->world_position().y + bp->size.y * 0.5f, 0.0f, 1e-3f);  // (its top is the surface)
      CHECK(ed.obj_last_valid() && r479_type(ed, "adjust:Height", "1"));
      CHECK_NEAR(bp->size.y, 1.0f, 1e-4f);
      CHECK_NEAR(ed.active_object_for_test()->world_position().y + bp->size.y * 0.5f, 0.0f, 1e-3f);
    }
    {
      Editor ed;
      te_view(ed, kR479Camera);
      GameObject *bo = r477_blockout(ed);
      bo->set_local_scale({2.0f, 2.0f, 2.0f});
      GameObject *p = r477_piece(ed, *bo, "Box", 0, 0, {0, 0, 0}, {1, 1, 1});
      ed.select_for_test(p->id, false);
      r477_settle(ed, 2);
      ed.command("blockout room 0 0 4 3 2");
      const GameObject *room = ed.active_object_for_test();
      const BlockoutPiece *bp = r479_active(ed);
      CHECK(bp && bp->room && room != p);
      if (!bp) return;
      CHECK_NEAR(bp->size.x * 2.0f, 4.0f, 1e-4f);
      CHECK_NEAR(bp->size.y * 2.0f, 2.0f, 1e-4f);
      CHECK_NEAR(bp->size.z * 2.0f, 3.0f, 1e-4f);
      CHECK_NEAR(room->world_position().y - bp->size.y, 0.0f, 1e-3f);  // (the air's bottom, half its own height down at scale 2, is on the ground)
      CHECK_NEAR(bp->wall * 2.0f, 0.2f, 1e-5f);  // (the tool's 0.2 m is the wall's world thickness)
      CHECK_NEAR(bp->floor * 2.0f, 0.2f, 1e-5f);
    }
    {
      Editor ed;
      te_view(ed, kR479Camera);
      GameObject *bo = r477_blockout(ed);
      bo->set_local_scale({2.0f, 1.0f, 1.0f});
      GameObject *p = r477_piece(ed, *bo, "Box", 0, 0, {0, 0, 0}, {1, 1, 1});
      ed.select_for_test(p->id, false);
      r477_settle(ed, 2);
      const size_t mark = r477_mark();
      ed.command("blockout room 0 0 4 3 2");
      size_t warnings = 0;
      r477_log_since(mark, &warnings);
      CHECK(warnings >= 1 && r477_count(ed, true) == 1);
    }
    {
      Editor ed;  // (a Blockout turned 45 degrees under a parent scaled (2, 1, 1): its axes are not square to each other)
      te_view(ed, kR479Camera);
      GameObject *par = ed.scene().create("Par");
      par->set_local_scale({2.0f, 1.0f, 1.0f});
      GameObject *bo = ed.scene().create("Blockout", par);
      bo->add<Blockout>();
      bo->add<MeshFilter>()->mesh = std::make_shared<Mesh>();
      bo->add<MeshRenderer>();
      bo->set_local_rotation(Quat::axis_angle({0.0f, 1.0f, 0.0f}, 45.0f * kDeg2Rad));
      GameObject *p = r477_piece(ed, *bo, "Box", 0, 0, {0, 0, 0}, {1, 1, 1});
      ed.select_for_test(p->id, false);
      r477_settle(ed, 2);
      const size_t mark = r477_mark();
      ed.command("blockout room 0 0 4 3 2");
      size_t warnings = 0;
      r477_log_since(mark, &warnings);
      CHECK(warnings >= 1 && r477_count(ed, true) == 1);
    }
  });

  test("blockout handles: Esc in the middle of a drag leaves a saved scene saved", [] {
    Editor ed;
    r479_piece_scene(ed, kR479Camera, 0, {0, 0, 0}, {2, 2, 2});
    ed.clear_scene_dirty_for_test();
    int hx = 0, hy = 0, tx = 0, ty = 0;
    CHECK(ed.blockout_handle_pixel_for_test("+x", hx, hy) && ed.project_to_window({2.0f, 0.0f, 0.0f}, tx, ty));
    r479_press(ed, hx, hy);
    r479_move(ed, tx, ty);
    CHECK(ed.scene_dirty_for_test());  // (the drag marked it)
    ed.step_frame_headless({r477_key(platform::KEY_ESCAPE)});
    ed.step_frame_headless({r477_key(platform::KEY_ESCAPE, false)});
    r479_release(ed, tx, ty);
    CHECK(!ed.scene_dirty_for_test());
    ed.mark_changed_for_test("an edit");  // (an unsaved scene stays unsaved: the drag's Esc does not clean it)
    r477_settle(ed, 3);
    CHECK(ed.scene_dirty_for_test());
    r479_press(ed, hx, hy);
    r479_move(ed, tx, ty);
    ed.step_frame_headless({r477_key(platform::KEY_ESCAPE)});
    ed.step_frame_headless({r477_key(platform::KEY_ESCAPE, false)});
    r479_release(ed, tx, ty);
    CHECK(ed.scene_dirty_for_test());
  });

  test("blockout room tool: switching to another dock tab while a height is being typed or a handle is held ends both, and Ctrl+Z works again", [] {
    {
      Editor ed;
      te_view(ed, "camera 10 70 14 0 0 0");
      ed.command("tool room");
      r477_settle(ed, 2);
      int ax = 0, ay = 0, bx = 0, by = 0;
      CHECK(ed.project_to_window({-2.0f, 0.0f, -1.0f}, ax, ay) && ed.project_to_window({2.0f, 0.0f, 1.0f}, bx, by));
      r479_press(ed, ax, ay);
      r479_move(ed, bx, by);
      r479_release(ed, bx, by);
      ed.command("blockout room 8 0 10 2 2");  // (a room made before: the step Ctrl+Z undoes)
      r477_settle(ed, 3);
      r479r_type(ed, platform::KEY_3, '3');
      CHECK(ed.room_tool_stage_for_test() == 2);
      ed.command("window Game");
      r477_settle(ed, 4);
      CHECK(ed.room_tool_stage_for_test() == 0);
      const size_t pieces = r477_count(ed, true);
      ed.step_frame_headless({te_key(platform::KEY_Z, platform::MOD_CTRL)});
      ed.step_frame_headless({te_key(platform::KEY_Z, platform::MOD_CTRL, false)});
      r477_settle(ed, 3);
      CHECK(r477_count(ed, true) < pieces);
    }
    {
      Editor ed;
      GameObject *box = r479_piece_scene(ed, kR479Camera, 0, {0, 0, 0}, {2, 2, 2});
      (void)box;
      int hx = 0, hy = 0, tx = 0, ty = 0;
      CHECK(ed.blockout_handle_pixel_for_test("+x", hx, hy) && ed.project_to_window({2.0f, 0.0f, 0.0f}, tx, ty));
      r479_press(ed, hx, hy);
      r479_move(ed, tx, ty);
      CHECK(ed.blockout_dragging_for_test());
      const size_t depth = ed.undo_depth_for_test();
      ed.command("window Game");
      r477_settle(ed, 4);
      CHECK(!ed.blockout_dragging_for_test());
      ed.step_frame_headless({te_key(platform::KEY_Z, platform::MOD_CTRL)});
      ed.step_frame_headless({te_key(platform::KEY_Z, platform::MOD_CTRL, false)});
      r477_settle(ed, 3);
      CHECK(ed.undo_depth_for_test() < depth);  // (Ctrl+Z reaches the undo stack again)
    }
  });

  test("blockout room tool: with the Scene view in a second window and the main window on the Game tab, a room is still drawn there (the footprint is not dropped because the main window shows no Scene view)", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.step_frame_headless();
    ed.command("dock duplicate Scene");
    ed.step_frame_headless();
    EditorWindow *w = ed.window_for_test(1);
    CHECK(w != nullptr);
    if (!w) return;
    ed.step_window_headless(1, {});
    ed.command("window Game");  // (the main window's Scene tab goes behind Game: only the second window draws a Scene view)
    ed.command("tool room");
    for (int i = 0; i < 3; i++) ed.step_window_headless(1, {});
    CHECK(!ed.scene_drawn_for_test());
    const size_t pieces = r477_count(ed, true);
    const int sx = w->fb.width / 2, sy = w->fb.height / 2 + 40;
    ed.step_window_headless(1, {r477_ev(R477ET::MouseMove, sx, sy)});
    ed.step_window_headless(1, {r477_ev(R477ET::MouseDown, sx, sy)});
    for (int i = 1; i <= 6; i++) ed.step_window_headless(1, {r477_ev(R477ET::MouseMove, sx + 15 * i, sy + 8 * i)});
    CHECK(ed.room_tool_stage_for_test() == 1);
    ed.step_window_headless(1, {r477_ev(R477ET::MouseUp, sx + 90, sy + 48)});
    for (int i = 0; i < 4; i++) ed.step_window_headless(1, {});  // (each window frame runs the main frame too)
    CHECK(ed.room_tool_stage_for_test() == 2);
    ed.step_window_headless(1, {r477_ev(R477ET::MouseMove, sx + 90, sy - 60)});
    ed.step_window_headless(1, {r477_ev(R477ET::MouseDown, sx + 90, sy - 60)});
    ed.step_window_headless(1, {r477_ev(R477ET::MouseUp, sx + 90, sy - 60)});
    for (int i = 0; i < 3; i++) ed.step_window_headless(1, {});
    std::printf("    pieces %zu -> %zu, stage %d\n", pieces, r477_count(ed, true), ed.room_tool_stage_for_test());
    CHECK(r477_count(ed, true) == pieces + 1 && ed.room_tool_stage_for_test() == 0);
  });
}

void run_blockout_tests() {
  tests_round476();
  tests_round476_engineer();
  tests_round477();
  tests_round477_te();
  tests_round477_review();
  tests_round477_review2();
  tests_round478();
  tests_round478_object();
  tests_round478_engineer();
  tests_round478_engineer_object();
  tests_round478_review();
  tests_round479();
  tests_round479_engineer();
  tests_round479_review();
}

// SPDX-License-Identifier: GPL-2.0-or-later
// Blockout's kernel (round 476, task 0452, ADR 0128): pieces built into one closed mesh, by the crisp path (Manifold,
// where the build has Blender's libraries) and by the field path (every build). No editor here: blockout::build is a
// function of its pieces.
#include "test_common.h"

#include "../src/scene/blockout.h"

#include <array>

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

void run_blockout_tests() {
  tests_round476();
  tests_round476_engineer();
}

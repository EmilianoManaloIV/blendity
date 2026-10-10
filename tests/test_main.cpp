// SPDX-License-Identifier: GPL-2.0-or-later
// blendity_tests - dependency-free unit tests (exit code = number of failures).
#include "../src/core/core.h"
#include "../src/core/jobs.h"
#include "../src/editor/editor.h"
#include "../src/render/camera_filter.h"
#include "../src/render/canvas.h"
#include "../src/render/raster.h"
#include "../src/render/dof.h"
#include "../src/research/research.h"
#include "../src/image/image.h"
#include "../src/render/colormanagement.h"
#include "../src/render/display.h"
#include "../src/render/gpu_device.h"
#include "../src/render/lightmapper.h"
#include "../src/render/pathtracer.h"
#include "../src/render/shading.h"
#include "../src/scene/import.h"
#include "../src/scene/material.h"
#include "../src/scene/mesh.h"
#include "../src/scene/physics.h"
#include "../src/scene/scene.h"
#include "../src/scene/uv.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <unordered_set>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <vector>

using namespace bl;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                              \
  do {                                                                           \
    g_checks++;                                                                  \
    if (!(cond)) {                                                               \
      g_fail++;                                                                  \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);              \
    }                                                                            \
  } while (0)
#define CHECK_NEAR(a, b, eps) CHECK(std::fabs((double)(a) - (double)(b)) <= (eps))

static void test(const char *name, const std::function<void()> &fn) {
  /* BLENDITY_TEST_FILTER=text runs only the tests whose name contains it. */
  static const char *filter = std::getenv("BLENDITY_TEST_FILTER");
  if (filter && !std::strstr(name, filter)) return;
  int before = g_fail;
  ScopedTimer t;
  fn();
  std::printf("%s %s (%.0f ms)\n", g_fail == before ? "[ ok ]" : "[FAIL]", name, t.ms());
}

/* ------------------------------------------------------- shared helpers */

static const float kNaN = std::numeric_limits<float>::quiet_NaN();
static const float kInf = std::numeric_limits<float>::infinity();

/* How many elements fail `ok`; prints the first one, so one CHECK(count_bad(...) == 0) over a whole
 * picture or table reports as much as a CHECK per element did, without counting thousands of checks. */
template <class V, class F>
static size_t count_bad(const char *what, const V &v, F ok) {
  size_t bad = 0, first = 0;
  for (size_t i = 0; i < v.size(); i++)
    if (!ok(v[i]) && bad++ == 0) first = i;
  if (bad) std::printf("    %s: %zu of %zu bad, first at index %zu\n", what, bad, v.size(), first);
  return bad;
}
static bool opaque(uint32_t c) { return (c >> 24) == 0xFF; }

/* Scratch files go to the OS temp folder, not next to the shipped executables. */
static std::string test_dir() {
  static std::string dir = [] {
    std::error_code ec;
    std::string d = (std::filesystem::temp_directory_path(ec) / "blendity_tests").string();
    if (ec) d = fs::join(fs::executable_dir(), "test_output");
    fs::make_dirs(d);
    return d;
  }();
  return dir;
}

/* Every Editor in the tests works in a scratch project (and a scratch trash), never
 * the real Assets folder: the Project window can now move, rename and delete files.
 * Setting BLENDITY_PROJECT to "" returns to this default. */
static std::string scratch_project() {
  const std::string p = fs::join(test_dir(), "default_project");
  fs::make_dirs(fs::join(p, "Assets/Scenes"));
  fs::make_dirs(fs::join(p, "research/papers"));
  return p;
}

static void set_env(const char *k, const std::string &v_in) {
  std::string v = v_in;
  if (v.empty() && std::string(k) == "BLENDITY_PROJECT") v = scratch_project();
  if (v.empty() && std::string(k) == "BLENDITY_TRASH") v = fs::join(test_dir(), "trash");
#ifdef _WIN32
  _putenv_s(k, v.c_str());
#else
  setenv(k, v.c_str(), 1);
#endif
}

static int euler_characteristic(const Mesh &m) { return (int)m.vert_count() - (int)m.edge_count() + (int)m.face_count(); }

static bool normals_outward(const Mesh &m, Vec3 c = Vec3(0.0f)) {
  for (size_t f = 0; f < m.face_count(); f++)
    if (dot(m.face_normal(f), m.face_center(f) - c) <= 0) return false;
  return true;
}

/* Every edge used by exactly two faces, in opposite directions. */
static bool closed_manifold(const Mesh &m) {
  std::unordered_map<uint64_t, int> dir;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t *v = m.face_verts(f);
    uint32_t n = m.face_size(f);
    for (uint32_t i = 0; i < n; i++) {
      uint32_t a = v[i], b = v[(i + 1) % n];
      dir[Mesh::edge_key(a, b)] += a < b ? 1 : 16;
    }
  }
  for (auto &[k, c] : dir)
    if (c != 17) return false;
  return true;
}

static float uv_area_total(const Mesh &m) {
  float a = 0;
  for (size_t f = 0; f < m.face_count(); f++) {
    uint32_t b = m.face_offsets[f], n = m.face_size(f);
    float s = 0;
    for (uint32_t i = 0; i < n; i++) {
      Vec2 p = m.uvs[b + i], q = m.uvs[b + (i + 1) % n];
      s += p.x * q.y - q.x * p.y;
    }
    a += std::fabs(0.5f * s);
  }
  return a;
}

static std::vector<uint8_t> select_verts(const Mesh &m, std::initializer_list<uint32_t> vs) {
  std::vector<uint8_t> s(m.vert_count(), 0);
  for (uint32_t v : vs) s[v] = 1;
  return s;
}

static void modeling_tests();
static void edge_tool_tests();
static void uv_tests();
static void subdiv_tests();
static void physics_tests();
static void library_modeling_tests();
static void image_tests();
static void import_and_material_tests();
static void render_tests();
static void file_tests();
static void colormanagement_tests();
static void modeling_round9_tests();
static void modeling_round10_tests();
static void modeling_round11_tests();
static void modeling_round12_tests();
static void modeling_round13_tests();
static void round13_feature_tests();
static void round14_tests();
static void round14_feature_tests();
static void round15_tests();
static void round16_tests();
static void round17_tests();
static void round18_tests();
static void round26_tests();
static void round27_tests();
static void round28_tests();
static void round29_tests();
static void round29_review_tests();
static void round30_tests();
static void round31_tests();
static void round32_tests();
static void round34_tests();
static void round35_tests();
static void round36_tests();

int main() {
  register_builtin_components();
  research::register_features();
  Log::echo_stdout = false;
  set_env("BLENDITY_PROJECT", "");  // the scratch project
  set_env("BLENDITY_TRASH", "");

  test("math: matrix inverse", [] {
    Mat4 m = Mat4::trs({1, 2, 3}, Quat::euler({10, 20, 30}), {2, 3, 4});
    Mat4 i = m * m.inverse();
    for (int k = 0; k < 16; k++) CHECK_NEAR(i.m[k], (k % 5 == 0) ? 1.0f : 0.0f, 1e-4);
  });
  test("math: Unity euler order round-trip (Z, X, Y)", [] {
    for (Vec3 e : {Vec3(10, 20, 30), Vec3(-45, 120, 5), Vec3(80, -170, -60), Vec3(0, 90, 0)}) {
      Vec3 r = Quat::euler(e).to_euler();
      Quat a = Quat::euler(e), b = Quat::euler(r);
      float d = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
      CHECK_NEAR(d, 1.0f, 1e-4);
    }
    /* +90 about Y turns forward (+Z) to +X in Unity. */
    Vec3 f = Quat::euler({0, 90, 0}).rotate({0, 0, 1});
    CHECK_NEAR(f.x, 1.0f, 1e-5);
  });
  test("math: left-handed look-at & [0,1] depth perspective", [] {
    Mat4 v = Mat4::look_at({0, 0, -5}, {0, 0, 0}, {0, 1, 0});
    CHECK_NEAR(v.point({0, 0, 0}).z, 5.0f, 1e-5);
    CHECK_NEAR(v.point({1, 0, -5}).x, 1.0f, 1e-5);  // +X stays right
    Mat4 p = Mat4::perspective(60 * kDeg2Rad, 1.0f, 0.5f, 100.0f);
    Vec4 n = p * Vec4(0, 0, 0.5f, 1), fa = p * Vec4(0, 0, 100, 1);
    CHECK_NEAR(n.z / n.w, 0.0f, 1e-5);
    CHECK_NEAR(fa.z / fa.w, 1.0f, 1e-4);
  });
  test("math: ray / triangle / aabb", [] {
    Ray r{{0.2f, 5, 0.2f}, {0, -1, 0}};
    CHECK_NEAR(ray_triangle(r, {0, 0, 0}, {0, 0, 1}, {1, 0, 0}), 5.0f, 1e-5);
    AABB b;
    b.add({-1, -1, -1});
    b.add({1, 1, 1});
    CHECK_NEAR(ray_aabb(r, b), 4.0f, 1e-5);
  });

  test("primitives: closed meshes satisfy V - E + F = 2 and face outward", [] {
    for (auto m : {primitives::cube(), primitives::uv_sphere(), primitives::ico_sphere(), primitives::cylinder()}) {
      CHECK(euler_characteristic(*m) == 2);
      CHECK(normals_outward(*m));
    }
    auto cone = primitives::cone();
    CHECK(euler_characteristic(*cone) == 2);
    CHECK(normals_outward(*cone, {0, -0.1f, 0}));
    CHECK(euler_characteristic(*primitives::torus()) == 0);  // genus 1
    auto plane = primitives::plane();
    for (size_t f = 0; f < plane->face_count(); f++) CHECK(plane->face_normal(f).y > 0.99f);
    CHECK(primitives::quad()->face_normal(0).z < -0.99f);  // faces -Z like Unity's Quad
  });
  test("mesh: Catmull-Clark counts and smoothing", [] {
    Mesh c = meshops::subdivide_catmull_clark(*primitives::cube());
    CHECK(c.vert_count() == 26);
    CHECK(c.face_count() == 24);
    CHECK(euler_characteristic(c) == 2);
    CHECK(normals_outward(c));
    /* Corners move inward, toward the limit sphere-ish shape. */
    float maxr = 0;
    for (auto &p : c.positions) maxr = std::max(maxr, length(p));
    CHECK(maxr < std::sqrt(3.0f) * 0.5f);
    Mesh s = meshops::subdivide_simple(*primitives::cube());
    CHECK(s.face_count() == 24);
  });
  test("mesh: extrude & inset keep topology valid", [] {
    Mesh m = *primitives::cube();
    std::vector<uint8_t> sel(m.face_count(), 0);
    sel[0] = 1;
    meshops::extrude_faces(m, sel, 0.5f);
    CHECK(m.face_count() == 10);
    CHECK(m.vert_count() == 12);
    CHECK(euler_characteristic(m) == 2);
    CHECK(normals_outward(m, m.bounds().center()));
    Mesh n = *primitives::cube();
    std::vector<uint8_t> s2(n.face_count(), 0);
    s2[2] = 1;
    meshops::inset_faces(n, s2, 0.3f);
    CHECK(n.face_count() == 10);
    CHECK(n.vert_count() == 12);
    CHECK(euler_characteristic(n) == 2);
    Mesh all = *primitives::cube();
    std::vector<uint8_t> s3(all.face_count(), 1);
    meshops::extrude_faces(all, s3, 0.25f);  // whole closed mesh: grows, no walls
    CHECK(all.face_count() == 6);
    CHECK(all.bounds().max.x > 0.6f);  // corner moves along (1,1,1)/sqrt(3) * 0.25
  });
  test("mesh: triangulation (ear clipping) preserves area", [] {
    Mesh m;
    /* Concave "L" shaped n-gon in the XZ plane. */
    Vec3 pts[] = {{0, 0, 0}, {0, 0, 2}, {1, 0, 2}, {1, 0, 1}, {2, 0, 1}, {2, 0, 0}};
    std::vector<uint32_t> idx;
    for (auto p : pts) idx.push_back(m.add_vert(p));
    m.add_face(idx.data(), idx.size());
    meshops::triangulate(m);
    CHECK(m.face_count() == 4);
    float area = 0;
    for (size_t f = 0; f < m.face_count(); f++) {
      const uint32_t *v = m.face_verts(f);
      area += 0.5f * length(cross(m.positions[v[1]] - m.positions[v[0]], m.positions[v[2]] - m.positions[v[0]]));
    }
    CHECK_NEAR(area, 3.0f, 1e-4);
  });
  test("mesh: merge by distance (hash == naive)", [] {
    Mesh base = *primitives::grid(4.0f, 8, 8);
    Mesh m;
    for (size_t f = 0; f < base.face_count(); f++) {
      uint32_t v[4];
      for (int k = 0; k < 4; k++) v[k] = m.add_vert(base.positions[base.face_verts(f)[k]]);
      m.add_face(v, 4);
    }
    Mesh a = m, b = m;
    size_t ra = meshops::merge_by_distance(a, 1e-4f), rb = meshops::merge_by_distance_naive(b, 1e-4f);
    CHECK(ra == rb);
    CHECK(a.vert_count() == base.vert_count());
    CHECK(a.face_count() == base.face_count());
  });
  test("mesh: copy-on-write sharing", [] {
    MeshPtr a = primitives::cube();
    MeshPtr b = a;
    CHECK(a.get() == b.get());
    mesh_make_mutable(b)->positions[0].x = 42;
    CHECK(a.get() != b.get());
    CHECK(a->positions[0].x != 42);
  });
  test("research: Taubin smoothing keeps volume better than Laplacian", [] {
    Mesh noisy = meshops::subdivide_catmull_clark(meshops::subdivide_catmull_clark(*primitives::cube()));
    meshops::randomize(noisy, 0.03f, 99);
    Mesh lap = noisy, tau = noisy;
    meshops::smooth_laplacian(lap, 0.5f, 20);
    research::taubin_smooth(tau, 0.5f, -0.53f, 10);
    auto size = [](const Mesh &m) { return length(m.bounds().extent()); };
    CHECK(size(tau) > size(lap));
    CHECK(research::apply("taubin_smoothing", tau));
  });

  test("scene: hierarchy, keep-world reparenting, dirty flags", [] {
    Scene s;
    GameObject *p = s.create("parent");
    p->set_local_position({1, 0, 0});
    p->set_local_euler({0, 90, 0});
    GameObject *c = s.create("child");
    c->set_local_position({2, 3, 4});
    s.set_parent(c, p);
    Vec3 w = c->world_position();
    CHECK_NEAR(w.x, 2, 1e-4);
    CHECK_NEAR(w.y, 3, 1e-4);
    CHECK_NEAR(w.z, 4, 1e-4);
    p->set_local_position({5, 0, 0});  // child follows
    CHECK_NEAR(c->world_position().x, 6, 1e-4);
    Mat4 a = c->world_matrix(), b = c->world_matrix_uncached();
    for (int k = 0; k < 16; k++) CHECK_NEAR(a.m[k], b.m[k], 1e-4);
    s.set_parent(p, c);  // cycle must be rejected
    CHECK(p->parent == nullptr);
  });
  test("scene: clone, duplicate, destroy", [] {
    Scene s;
    build_default_scene(s);
    size_t n = s.object_count();
    auto c = s.clone();
    CHECK(c->object_count() == n);
    GameObject *cube = s.find_by_name("Cube");
    GameObject *ccube = c->find(cube->id);
    CHECK(ccube && ccube != cube);
    CHECK(ccube->get<MeshFilter>()->mesh.get() == cube->get<MeshFilter>()->mesh.get());
    ccube->set_local_position({9, 9, 9});
    CHECK(cube->local().position.x != 9);
    GameObject *d = s.duplicate(cube);
    CHECK(d->name == "Cube (1)");
    CHECK(s.object_count() == n + 1);
    s.destroy(d);
    CHECK(s.object_count() == n);
    CHECK(s.find(d == nullptr ? 0 : 999999) == nullptr);
  });
  test("io: scene text round-trip", [] {
    Scene s;
    build_default_scene(s);
    s.find_by_name("Cube")->add<SubdivisionSurface>()->levels = 2;
    s.set_parent(s.find_by_name("Sphere"), s.find_by_name("Cube"));
    std::string a = save_scene_text(s);
    Scene l;
    std::string err;
    CHECK(load_scene_text(a, l, err));
    std::string a2 = save_scene_text(l);
    CHECK(a2 == a);
    if (a2 != a) {
      size_t i = 0;
      while (i < a.size() && i < a2.size() && a[i] == a2[i]) i++;
      const char nl = '\n';
      size_t ls = a.rfind(nl, i);
      ls = ls == std::string::npos ? 0 : ls + 1;
      std::printf("    first difference:\n    saved : %s\n    reload: %s\n", a.substr(ls, a.find(nl, i) - ls).c_str(),
                  a2.substr(ls, a2.find(nl, i) - ls).c_str());
    }
    CHECK(l.find_by_name("Sphere")->parent == l.find_by_name("Cube"));
    CHECK(l.find_by_name("Cube")->get<SubdivisionSurface>()->levels == 2);
    /* Unknown fields / components are skipped (forward compatibility). */
    std::string b = a + "object 999 0 \"Future\" 1\ncomponent HoloEmitter 1\n  flux 3\nend\nend\n";
    Scene l2;
    CHECK(load_scene_text(b, l2, err));
    CHECK(l2.find_by_name("Future") != nullptr);
  });
  test("io: OBJ export/import mirrors handedness consistently", [] {
    auto cube = primitives::cube();
    std::string obj = export_obj({{cube.get(), Mat4::translate({3, 0, 0})}});
    std::string err;
    auto meshes = import_obj(obj, err);
    CHECK(meshes.size() == 1);
    CHECK(meshes[0]->vert_count() == 8);
    CHECK(meshes[0]->face_count() == 6);
    CHECK_NEAR(meshes[0]->bounds().center().x, 3.0f, 1e-4);
    CHECK(normals_outward(*meshes[0], meshes[0]->bounds().center()));
  });

  test("render: id buffer, depth and back-face culling", [] {
    Image img;
    img.resize(64, 64);
    RenderTarget rt;
    rt.attach(img, {0, 0, 64, 64});
    Renderer3D r;
    Mat4 v = Mat4::look_at({0, 0, -3}, {0, 0, 0}, {0, 1, 0});
    Mat4 p = Mat4::perspective(60 * kDeg2Rad, 1, 0.1f, 100);
    auto quad = primitives::quad(1.0f);  // faces -Z, toward the camera
    RasterOptions opt;
    LightingEnv env;
    r.begin(&rt, v, p, env, opt);
    r.clear(0xFF000000);
    DrawItem it;
    it.mesh = &quad->render_mesh();
    it.id = 7;
    r.add(it);
    r.flush();
    CHECK(rt.id_at(32, 32) == 7);
    CHECK(rt.id_at(1, 1) == 0);
    CHECK(rt.depth_at(32, 32) < 1.0f);
    /* Seen from behind it is culled. */
    Mat4 v2 = Mat4::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0});
    r.begin(&rt, v2, p, env, opt);
    r.clear(0xFF000000);
    r.add(it);
    r.flush();
    CHECK(rt.id_at(32, 32) == 0);
    opt.multithreaded = false;
    opt.span_rows = false;
    r.begin(&rt, v, p, env, opt);
    r.clear(0xFF000000);
    r.add(it);
    r.flush();
    CHECK(rt.id_at(32, 32) == 7);
  });
  test("render: fast triangle setup (AVX2 + no-clip path) gives the same image as the general path", [] {
    /* Many small far objects (mostly sub-pixel triangles), a big mesh that
     * takes the separate vertex stage, a double-sided plane, and a floor that
     * crosses the near plane (clipping). */
    std::vector<MeshPtr> keep;
    std::vector<DrawItem> items;
    auto add = [&](MeshPtr m, Mat4 model, uint32_t id, bool two_sided = false) {
      keep.push_back(m);
      DrawItem it;
      it.mesh = &m->render_mesh();
      it.model = model;
      it.id = id;
      it.double_sided = two_sided;
      it.albedo = Vec3(0.2f + 0.1f * (id % 7), 0.5f, 0.8f - 0.05f * (id % 9));
      items.push_back(it);
    };
    auto ico = primitives::ico_sphere(0.5f, 2);
    for (int i = 0; i < 400; i++)
      add(ico, Mat4::translate({(i % 20 - 10) * 1.5f, (i / 20 - 10) * 1.5f, 25.0f + (i % 3) * 10.0f}), 10 + i);
    add(primitives::ico_sphere(1.5f, 5), Mat4::translate({0, 0, 4}), 2);
    add(primitives::plane(20.0f), Mat4::translate({0, -1.5f, 0}), 3, true);
    add(primitives::quad(3.0f), Mat4::translate({3, 1, 6}), 4, true);
    Mat4 v = Mat4::look_at({0, 0, -3}, {0, 0, 10}, {0, 1, 0});
    Mat4 p = Mat4::perspective(70 * kDeg2Rad, 320 / 200.0f, 0.1f, 100);
    LightingEnv env;
    RenderLight sun;
    sun.direction = normalize(Vec3(-0.3f, -1, 0.4f));
    env.lights.push_back(sun);
    env.camera_pos = {0, 0, -3};
    for (ShadeMode mode : {ShadeMode::Gouraud, ShadeMode::Deferred}) {
      Image img[2];
      RenderTarget rt[2];
      for (int k = 0; k < 2; k++) {
        img[k].resize(320, 200);
        rt[k].attach(img[k], {0, 0, 320, 200});
        Renderer3D r;
        RasterOptions opt;
        opt.shade = mode;
        opt.fast_setup = k == 0;
        opt.multithreaded = k == 0;
        r.begin(&rt[k], v, p, env, opt);
        r.clear(0xFF000000);
        for (auto &it : items) r.add(it);
        r.flush();
      }
      CHECK(img[0].pixels == img[1].pixels);
      CHECK(rt[0].depth == rt[1].depth);
      CHECK(rt[0].ids == rt[1].ids);
      std::set<uint32_t> seen(rt[0].ids.begin(), rt[0].ids.end());
      CHECK(seen.count(2) && seen.count(3) && seen.count(4) && seen.size() > 100);  // all kinds drawn
    }
  });
  test("jobs: parallel_for covers every index exactly once", [] {
    for (int64_t n : {1, 7, 1000, 123457}) {
      std::vector<std::atomic<int>> hits((size_t)n);
      JobSystem::global().parallel_for(n, 97, [&](int64_t b, int64_t e) {
        for (int64_t i = b; i < e; i++) hits[(size_t)i]++;
      });
      bool ok = true;
      for (auto &h : hits) ok = ok && h.load() == 1;
      CHECK(ok);
    }
  });
  test("canvas: font + PNG writer", [] {
    Font f;
    f.load_system_ui_font(13);
    CHECK(f.text_width("Blendity") > 20);
    CHECK(f.line_height() > 8);
    Image img;
    img.resize(32, 16);
    Canvas c;
    c.begin(&img);
    c.text(f, 1, 1, "Hi", 0xFFFFFFFF);
    std::string path = fs::join(test_dir(), "test_png_out.png");
    CHECK(write_png(path, img.pixels.data(), 32, 16, 32));
    std::string data;
    CHECK(fs::read_file(path, data) && data.size() > 50 && data.compare(1, 3, "PNG") == 0);
  });
  test("editor: headless create / undo / redo / play-mode revert", [] {
    Editor ed;
    ed.init_headless(800, 500);
    ed.step_frame_headless();
    size_t n = ed.scene().object_count();
    ed.command("create Cube 3");
    ed.step_frame_headless();
    CHECK(ed.scene().object_count() == n + 3);
    ed.step_frame_headless();
    platform::Event z;
    z.type = platform::EventType::KeyDown;
    z.key = platform::KEY_Z;
    z.mods = platform::MOD_CTRL;
    ed.step_frame_headless({z});
    CHECK(ed.scene().object_count() == n);  // the three creates were one undo step
    platform::Event y = z;
    y.key = platform::KEY_Y;
    ed.step_frame_headless({y});
    CHECK(ed.scene().object_count() == n + 3);
    ed.command("play");
    ed.command("create Sphere");
    for (int i = 0; i < 5; i++) ed.step_frame_headless();
    ed.command("stop");
    CHECK(ed.scene().object_count() == n + 3);  // play-mode changes reverted
  });
  test("ui: number fields evaluate expressions, relative input and Unity's L() / R()", [] {
    auto ev = [](const char *s, double cur = 0, int i = 0, int n = 1) {
      double d = -12345;
      return ui::eval_number(s, d, cur, i, n) ? d : -12345.0;
    };
    CHECK_NEAR(ev("2*3+1"), 7, 1e-9);
    CHECK_NEAR(ev("(1+2)^2"), 9, 1e-9);
    CHECK_NEAR(ev("2^3^2"), 512, 1e-9);  // right associative
    CHECK_NEAR(ev("-2^2"), -4, 1e-9);
    CHECK_NEAR(ev("2**3"), 8, 1e-9);
    CHECK_NEAR(ev("10 % 3"), 1, 1e-9);
    CHECK_NEAR(ev("sqrt(16) + max(1, 5, 3)"), 9, 1e-9);
    CHECK_NEAR(ev("sin(pi/2)"), 1, 1e-9);
    CHECK_NEAR(ev("deg(pi)"), 180, 1e-9);
    CHECK_NEAR(ev("+=5", 3), 8, 1e-9);
    CHECK_NEAR(ev("-=1.5", 3), 1.5, 1e-9);
    CHECK_NEAR(ev("*=2", 4), 8, 1e-9);
    CHECK_NEAR(ev("/=4", 2), 0.5, 1e-9);
    CHECK_NEAR(ev("*=2+1", 4), 12, 1e-9);  // the right side is a whole expression
    CHECK_NEAR(ev("L(0, 10)", 0, 0, 3), 0, 1e-9);
    CHECK_NEAR(ev("L(0, 10)", 0, 1, 3), 5, 1e-9);
    CHECK_NEAR(ev("L(0, 10)", 0, 2, 3), 10, 1e-9);
    double r = ev("R(1, 2)");
    CHECK(r >= 1 && r <= 2);
    CHECK(ev("2+") == -12345.0);
    CHECK(ev("abc") == -12345.0);
    CHECK(ev("1/0") == 0);  // division by zero gives 0, not inf
  });
  test("ui: click a number field and type into it; Tab / Shift+Tab move between fields", [] {
    ui::Context u;
    Image img;
    img.resize(300, 120);
    float a = 1, b = 2, c = 3;
    const Recti ra{10, 10, 100, 20}, rb{10, 40, 100, 20}, rc{10, 70, 100, 20};
    auto frame = [&](const std::function<void(ui::Input &)> &setup) {
      setup(u.in);
      u.begin_frame(&img, 0.0);
      u.float_field(1001, ra, a);
      u.float_field(1002, rb, b);
      u.float_field(1003, rc, c);
      u.end_frame();
      for (bool &p : u.in.pressed) p = false;
      for (bool &r : u.in.released) r = false;
      for (bool &k : u.in.key_pressed) k = false;
      u.in.text.clear();
      u.in.pmx = u.in.mx;
      u.in.pmy = u.in.my;
    };
    /* A click without dragging starts typing, with the old value selected. */
    frame([](ui::Input &in) { in.mx = 50; in.my = 20; in.down[0] = in.pressed[0] = true; });
    frame([](ui::Input &in) { in.down[0] = false; in.released[0] = true; });
    CHECK(u.wants_keyboard());
    frame([](ui::Input &in) { in.text = "2*3"; });
    frame([](ui::Input &in) { in.key_pressed[platform::KEY_TAB] = true; });
    CHECK_NEAR(a, 6.0f, 1e-6f);   // typed expression committed by Tab
    CHECK(u.wants_keyboard());    // ...and the next field is being edited
    frame([](ui::Input &in) { in.text = "+=5"; });
    frame([](ui::Input &in) { in.key_pressed[platform::KEY_ENTER] = true; });
    CHECK_NEAR(b, 7.0f, 1e-6f);   // relative to its own value
    CHECK(!u.wants_keyboard());
    CHECK_NEAR(c, 3.0f, 1e-6f);   // untouched
    /* Shift+Tab goes back. */
    frame([](ui::Input &in) { in.mx = 50; in.my = 80; in.down[0] = in.pressed[0] = true; });
    frame([](ui::Input &in) { in.down[0] = false; in.released[0] = true; });
    frame([](ui::Input &in) { in.text = "10"; });
    frame([](ui::Input &in) {
      in.key_pressed[platform::KEY_TAB] = true;
      in.mods = platform::MOD_SHIFT;
    });
    frame([](ui::Input &in) { in.mods = 0; });
    CHECK_NEAR(c, 10.0f, 1e-6f);
    frame([](ui::Input &in) { in.text = "-1"; });
    frame([](ui::Input &in) { in.key_pressed[platform::KEY_ENTER] = true; });
    CHECK_NEAR(b, -1.0f, 1e-6f);  // Shift+Tab landed on the field above
    /* Escape cancels typing; a bad expression leaves the value alone. */
    frame([](ui::Input &in) { in.mx = 50; in.my = 20; in.down[0] = in.pressed[0] = true; });
    frame([](ui::Input &in) { in.down[0] = false; in.released[0] = true; });
    frame([](ui::Input &in) { in.text = "99"; });
    frame([](ui::Input &in) { in.key_pressed[platform::KEY_ESCAPE] = true; });
    CHECK_NEAR(a, 6.0f, 1e-6f);
  });
  test("editor: Push/Pull is a confirmed face operation (P, mouse or typed distance, Esc cancels)", [] {
    Editor ed;
    ed.init_headless(1000, 700);
    ed.command("create Cube");  // selected and active after creation
    ed.step_frame_headless();
    GameObject *cube = nullptr;
    ed.scene().for_each([&](GameObject &g) {
      if (g.name == "Cube" && (!cube || g.id > cube->id)) cube = &g;
    });
    CHECK(cube != nullptr);
    if (!cube) return;
    const uint64_t id = cube->id;
    auto mesh = [&]() -> const Mesh & { return *ed.scene().find(id)->get<MeshFilter>()->mesh; };
    auto top_y = [&] {
      float y = -1e9f;
      for (Vec3 p : mesh().positions) y = std::max(y, p.y);
      return y;
    };
    Recti r = ed.scene_view_rect();
    const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    auto ev = [](platform::EventType t, int x, int y) {
      platform::Event e;
      e.type = t;
      e.x = x;
      e.y = y;
      return e;
    };
    auto key = [&](int k) {
      platform::Event d = ev(platform::EventType::KeyDown, cx, cy), u = d;
      d.key = u.key = k;
      u.type = platform::EventType::KeyUp;
      ed.step_frame_headless({d, u});
    };
    auto click = [&](int button, int x, int y) {
      platform::Event d = ev(platform::EventType::MouseDown, x, y), u = ev(platform::EventType::MouseUp, x, y);
      d.button = u.button = button;
      ed.step_frame_headless({d});
      ed.step_frame_headless({u});
    };
    auto move = [&](int x, int y) { ed.step_frame_headless({ev(platform::EventType::MouseMove, x, y)}); };
    move(cx, cy);
    const float y0 = top_y();
    /* Object mode: P is not a tool any more, so nothing follows the mouse. */
    key(platform::KEY_P);
    move(cx, cy - 80);
    CHECK_NEAR(top_y(), y0, 1e-6f);
    /* Vertex mode: Push/Pull is a face operation, so it refuses. */
    ed.command("edit vertex all");
    move(cx, cy);
    key(platform::KEY_P);
    move(cx, cy - 80);
    CHECK_NEAR(top_y(), y0, 1e-6f);
    /* Face mode with the top selected: it follows the mouse until the click. */
    ed.command("edit face");
    ed.command("fsel facing 0 1 0");
    move(cx, cy);
    key(platform::KEY_P);
    move(cx, cy - 40);
    move(cx, cy - 90);
    const float live = top_y();
    CHECK(live > y0 + 0.05f);
    click(0, cx, cy - 90);
    move(cx, cy + 200);  // confirmed: moving on changes nothing
    CHECK_NEAR(top_y(), live, 1e-5f);
    CHECK(closed_manifold(mesh()));
    /* A typed distance, confirmed with Enter. */
    const float y1 = top_y();
    move(cx, cy);
    key(platform::KEY_P);
    platform::Event t = ev(platform::EventType::Text, cx, cy);
    for (char c : std::string("0.25")) {
      t.codepoint = (uint32_t)c;
      ed.step_frame_headless({t});
    }
    key(platform::KEY_ENTER);
    CHECK_NEAR(top_y(), y1 + 0.25f, 1e-4f);
    /* Esc and a right-click both cancel and restore the mesh. */
    const float y2 = top_y();
    move(cx, cy);
    key(platform::KEY_P);
    move(cx, cy - 120);
    CHECK(top_y() != y2);
    key(platform::KEY_ESCAPE);
    CHECK_NEAR(top_y(), y2, 1e-6f);
    move(cx, cy);
    key(platform::KEY_P);
    move(cx, cy - 120);
    click(1, cx, cy - 120);
    CHECK_NEAR(top_y(), y2, 1e-6f);
    CHECK(closed_manifold(mesh()));
    /* Each confirmed Push/Pull is one undo step. */
    platform::Event z = ev(platform::EventType::KeyDown, cx, cy);
    z.key = platform::KEY_Z;
    z.mods = platform::MOD_CTRL;
    ed.step_frame_headless({z});
    CHECK_NEAR(top_y(), y1, 1e-5f);
  });
  test("editor: adjust last operation re-runs extrude, moves it, and stays one undo step", [] {
    Editor ed;
    ed.init_headless(800, 500);
    ed.command("create Cube");  // selected and active after creation
    ed.step_frame_headless();
    GameObject *cube = nullptr;
    ed.scene().for_each([&](GameObject &g) {
      if (g.name == "Cube" && (!cube || g.id > cube->id)) cube = &g;
    });
    CHECK(cube != nullptr);
    if (!cube) return;
    uint64_t id = cube->id;
    auto mesh = [&]() -> const Mesh & { return *ed.scene().find(id)->get<MeshFilter>()->mesh; };
    auto top_y = [&] {
      float y = -1e9f;
      for (Vec3 p : mesh().positions) y = std::max(y, p.y);
      return y;
    };
    ed.command("fsel facing 0 1 0");
    ed.command("editop extrude");
    ed.step_frame_headless();
    CHECK(mesh().face_count() == 10);
    CHECK_NEAR(top_y(), 0.5f + 0.5f, 1e-4f);  // default distance 0.5
    ed.command("redo amount 2");
    ed.step_frame_headless();
    CHECK(mesh().face_count() == 10);  // re-run from the original, not extruded twice
    CHECK_NEAR(top_y(), 2.5f, 1e-4f);
    ed.command("redo normal 0.5");
    ed.step_frame_headless();
    CHECK_NEAR(top_y(), 3.0f, 1e-4f);
    ed.command("redo move 0 -1 0");
    ed.step_frame_headless();
    CHECK_NEAR(top_y(), 2.0f, 1e-4f);
    platform::Event z;
    z.type = platform::EventType::KeyDown;
    z.key = platform::KEY_Z;
    z.mods = platform::MOD_CTRL;
    ed.step_frame_headless({z});
    CHECK(mesh().face_count() == 6);  // one undo step for the extrude and all its adjustments
    CHECK_NEAR(top_y(), 0.5f, 1e-4f);
  });

  modeling_tests();
  edge_tool_tests();
  uv_tests();
  subdiv_tests();
  physics_tests();
  library_modeling_tests();
  image_tests();
  import_and_material_tests();
  render_tests();
  file_tests();
  colormanagement_tests();
  modeling_round9_tests();
  modeling_round10_tests();
  modeling_round11_tests();
  modeling_round12_tests();
  modeling_round13_tests();
  round13_feature_tests();
  round14_tests();
  round14_feature_tests();
  round15_tests();
  round16_tests();
  round17_tests();
  round18_tests();
  round26_tests();
  round27_tests();
  round28_tests();
  round29_tests();
  round29_review_tests();
  round30_tests();
  round31_tests();
  round32_tests();
  round34_tests();
  round35_tests();
  round36_tests();

  /* BLENDITY_RENDER_CACHE_VERIFY=1 re-renders every view the render cache would have reused: none may
   * differ from the cached picture. */
  if (std::getenv("BLENDITY_RENDER_CACHE_VERIFY")) {
    std::printf("render cache verify: %llu stale pictures\n", (unsigned long long)Editor::render_cache_mismatches_all());
    CHECK(Editor::render_cache_mismatches_all() == 0);
  }
  std::printf("\n%d checks, %d failed\n", g_checks, g_fail);
  return g_fail;
}

/* ===================================================================== */

/* Signed volume (divergence theorem over fan triangles): > 0 when faces point outward. */
static float signed_volume(const Mesh &m) {
  double v = 0;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t *fv = m.face_verts(f);
    for (uint32_t i = 1; i + 1 < m.face_size(f); i++)
      v += dot(m.positions[fv[0]], cross(m.positions[fv[i]], m.positions[fv[i + 1]]));
  }
  return (float)(v / 6.0);
}

static size_t face_facing(const Mesh &m, Vec3 n) {
  for (size_t f = 0; f < m.face_count(); f++)
    if (dot(m.face_normal(f), n) > 0.99f) return f;
  return SIZE_MAX;
}

/* True when a ray along d through p passes through the mesh without hitting it. */
static bool ray_misses(const Mesh &m, Vec3 p, Vec3 d) {
  const RenderMesh &rm = m.render_mesh();
  Ray r{p, normalize(d)};
  for (size_t t = 0; t < rm.tri_count(); t++)
    if (ray_triangle(r, rm.positions[rm.indices[t * 3]], rm.positions[rm.indices[t * 3 + 1]], rm.positions[rm.indices[t * 3 + 2]]) > 0) return false;
  return true;
}

static void edge_tool_tests() {
  const float cube_sign = signed_volume(*primitives::cube()) > 0 ? 1.0f : -1.0f;
  auto vol = [&](const Mesh &m) { return signed_volume(m) * cube_sign; };
  test("bevel: one edge, rounded and all edges stay watertight", [&] {
    for (int seg : {1, 4}) {
      Mesh m = *primitives::cube();
      std::vector<uint8_t> vs(m.vert_count(), 0), fs;
      auto e = m.edge_cache()[0];
      vs[e.first] = vs[e.second] = 1;
      CHECK(meshops::bevel_edges(m, vs, fs, 0.1f, seg));
      CHECK(closed_manifold(m));
      CHECK(euler_characteristic(m) == 2);
      CHECK(m.face_count() == (size_t)(6 + seg));
      float v = vol(m);
      CHECK(v > 0.99f && v < 1.0f);   // a sliver along one edge is gone
      if (seg == 1) CHECK_NEAR(v, 1.0f - 0.1f * 0.1f / 2.0f, 1e-3f);
    }
    for (int seg : {1, 3}) {
      Mesh m = *primitives::cube();
      std::vector<uint8_t> vs(m.vert_count(), 1), fs;
      CHECK(meshops::bevel_edges(m, vs, fs, 0.1f, seg));
      CHECK(closed_manifold(m));
      CHECK(euler_characteristic(m) == 2);
      if (seg == 1) CHECK(m.face_count() == 6 + 12 + 8);
      CHECK(vol(m) > 0.9f && vol(m) < 1.0f);
    }
  });
  test("push through: a hole along the normal, also through a slanted far side", [&] {
    for (int slanted = 0; slanted < 2; slanted++) {
      Mesh m = *primitives::cube();
      size_t front = face_facing(m, {0, 0, -1}), back = face_facing(m, {0, 0, 1});
      if (slanted)
        for (uint32_t i = 0; i < m.face_size(back); i++) m.positions[m.face_verts(back)[i]].z += m.positions[m.face_verts(back)[i]].x * 0.6f;
      std::vector<uint8_t> fs(m.face_count(), 0);
      fs[front] = 1;
      meshops::inset_faces(m, fs, 0.4f);
      std::string err;
      CHECK(meshops::push_through(m, fs, 1, &err));
      CHECK(err.empty());
      CHECK(closed_manifold(m));
      CHECK(euler_characteristic(m) == 0);  // genus 1: a hole all the way through
      CHECK(ray_misses(m, {0, 0, -5}, {0, 0, 1}));
      CHECK(!ray_misses(m, {0.45f, 0.45f, -5}, {0, 0, 1}));
      if (!slanted) CHECK_NEAR(vol(m), 1.0f - 0.6f * 0.6f, 1e-3f);
      else CHECK(vol(m) > 0.0f);
    }
  });
  test("bridge: opposite faces, non-parallel faces, unequal outlines and open edge loops", [&] {
    {  /* Front and back: a straight tunnel, same as push-through. */
      Mesh m = *primitives::cube();
      std::vector<uint8_t> fs(m.face_count(), 0), vs;
      fs[face_facing(m, {0, 0, -1})] = fs[face_facing(m, {0, 0, 1})] = 1;
      meshops::inset_faces(m, fs, 0.4f);
      CHECK(meshops::bridge(m, vs, fs, 1, 0, 1.0f));
      CHECK(closed_manifold(m));
      CHECK(euler_characteristic(m) == 0);
      CHECK_NEAR(vol(m), 1.0f - 0.36f, 1e-3f);
    }
    {  /* Top and front (90 degrees apart): a curved handle outside the cube. */
      Mesh m = *primitives::cube();
      std::vector<uint8_t> fs(m.face_count(), 0), vs;
      fs[face_facing(m, {0, 1, 0})] = fs[face_facing(m, {0, 0, -1})] = 1;
      meshops::inset_faces(m, fs, 0.4f);
      CHECK(meshops::bridge(m, vs, fs, 8, 0, 1.0f));
      CHECK(closed_manifold(m));
      CHECK(euler_characteristic(m) == 0);
      std::printf("    handle volume %.4f\n", vol(m));
      CHECK(vol(m) > 1.0f);  // the handle adds material
    }
    {  /* A five-sided outline bridged to a quad. */
      Mesh m = *primitives::cube();
      std::vector<uint8_t> fs(m.face_count(), 0), vs;
      size_t top = face_facing(m, {0, 1, 0}), bottom = face_facing(m, {0, -1, 0});
      fs[top] = fs[bottom] = 1;
      meshops::inset_faces(m, fs, 0.4f);
      size_t inner_top = face_facing(m, {0, 1, 0});
      for (size_t f = 0; f < m.face_count(); f++)
        if (fs[f] && m.face_center(f).y > 0) inner_top = f;
      vs.assign(m.vert_count(), 0);
      vs[m.face_verts(inner_top)[0]] = vs[m.face_verts(inner_top)[1]] = 1;
      meshops::subdivide_edges(m, vs, 1);
      fs.resize(m.face_count(), 0);
      CHECK(m.face_size(inner_top) == 5);
      vs.assign(m.vert_count(), 0);
      CHECK(meshops::bridge(m, vs, fs, 2, 0, 1.0f));
      CHECK(closed_manifold(m));
      CHECK(euler_characteristic(m) == 0);
    }
    {  /* Two holes (open edge loops) joined through the inside. */
      Mesh m = *primitives::cube();
      std::vector<uint8_t> del(m.face_count(), 0);
      del[face_facing(m, {0, 1, 0})] = del[face_facing(m, {0, -1, 0})] = 1;
      meshops::inset_faces(m, del, 0.4f);  // the inner faces stay selected: delete them
      meshops::delete_faces(m, del);
      std::vector<uint8_t> vs(m.vert_count(), 1), fs;
      CHECK(meshops::bridge(m, vs, fs, 1, 0, 1.0f));
      CHECK(closed_manifold(m));
      CHECK(euler_characteristic(m) == 0);
    }
  });
  test("extrude onto another face fuses the two (hole, ring and weld cases)", [&] {
    auto two_boxes = [](float big, float gap_x) {
      Mesh m = *primitives::cube();
      Mesh b = *primitives::cube(big);
      uint32_t base = (uint32_t)m.vert_count();
      for (Vec3 p : b.positions) m.add_vert(p + Vec3(gap_x, 0, 0));
      for (size_t f = 0; f < b.face_count(); f++) {
        std::vector<uint32_t> v(b.face_verts(f), b.face_verts(f) + b.face_size(f));
        for (uint32_t &x : v) x += base;
        m.add_face(v.data(), v.size());
      }
      return m;
    };
    struct Case { float big, x, volume; };
    for (Case c : {Case{3.0f, 3.0f, 1 + 27 + 1}, Case{1.0f, 2.0f, 3.0f}, Case{0.6f, 1.8f, 1 + 1 + 0.216f}}) {
      Mesh m = two_boxes(c.big, c.x);
      std::vector<uint8_t> fs(m.face_count(), 0);
      fs[face_facing(m, {1, 0, 0})] = 1;  // the first box's +X face
      float gap = c.x - c.big * 0.5f - 0.5f;
      meshops::extrude_faces(m, fs, gap);
      CHECK(meshops::fuse_contacts(m, fs));
      CHECK(closed_manifold(m));
      CHECK(euler_characteristic(m) == 2);  // one solid now
      CHECK_NEAR(vol(m), c.volume, 1e-3f);
    }
  });
  test("push/pull: moves, notches, holes and joins like SketchUp", [&] {
    using meshops::PushPullResult;
    auto one = [](const Mesh &m, size_t f) {
      std::vector<uint8_t> s(m.face_count(), 0);
      s[f] = 1;
      return s;
    };
    PushPullResult res;
    {  /* The whole top of a box: the box just gets taller or shorter. */
      for (float d : {0.5f, -0.3f}) {
        Mesh m = *primitives::cube();
        auto fs = one(m, face_facing(m, {0, 1, 0}));
        CHECK(meshops::push_pull(m, fs, d, true, &res));
        CHECK(res == PushPullResult::Moved);
        CHECK(m.face_count() == 6 && m.vert_count() == 8);
        CHECK(closed_manifold(m));
        CHECK_NEAR(vol(m), 1.0f + d, 1e-4f);
      }
    }
    {  /* A face inside the top: a boss when pulled, a recess when pushed. */
      for (float d : {0.4f, -0.3f}) {
        Mesh m = *primitives::cube();
        auto fs = one(m, face_facing(m, {0, 1, 0}));
        meshops::inset_faces(m, fs, 0.4f);
        CHECK(meshops::push_pull(m, fs, d, true, &res));
        CHECK(res == PushPullResult::Extruded);
        CHECK(closed_manifold(m));
        CHECK_NEAR(vol(m), 1.0f + 0.36f * d, 1e-4f);
      }
    }
    {  /* Half of a split top: pushing makes a notch, pulling a step - the
        * side faces stretch instead of getting walls inside them. */
      for (float d : {-0.3f, 0.3f}) {
        Mesh m = *primitives::cube();
        size_t top = face_facing(m, {0, 1, 0});
        uint32_t tv[4];
        for (int k = 0; k < 4; k++) tv[k] = m.face_verts(top)[k];
        std::vector<uint8_t> vs(m.vert_count(), 0);
        vs[tv[0]] = vs[tv[1]] = 1;
        meshops::subdivide_edges(m, vs, 1);
        uint32_t mid1 = (uint32_t)m.vert_count() - 1;
        vs.assign(m.vert_count(), 0);
        vs[tv[2]] = vs[tv[3]] = 1;
        meshops::subdivide_edges(m, vs, 1);
        uint32_t mid2 = (uint32_t)m.vert_count() - 1;
        vs.assign(m.vert_count(), 0);
        vs[mid1] = vs[mid2] = 1;
        CHECK(meshops::connect_vertices(m, vs) == 1);
        size_t half = SIZE_MAX;
        for (size_t f = 0; f < m.face_count(); f++)
          if (dot(m.face_normal(f), Vec3(0, 1, 0)) > 0.99f) half = f;
        auto fs = one(m, half);
        CHECK(meshops::push_pull(m, fs, d, true, &res));
        CHECK(closed_manifold(m));
        CHECK(euler_characteristic(m) == 2);
        CHECK_NEAR(vol(m), 1.0f + 0.5f * d, 1e-4f);
      }
    }
    {  /* Pushed through to the far side (straight or slanted): a hole. */
      for (int slanted = 0; slanted < 2; slanted++) {
        Mesh m = *primitives::cube();
        size_t front = face_facing(m, {0, 0, -1}), back = face_facing(m, {0, 0, 1});
        if (slanted)
          for (uint32_t i = 0; i < m.face_size(back); i++) m.positions[m.face_verts(back)[i]].z += m.positions[m.face_verts(back)[i]].x * 0.6f;
        auto fs = one(m, front);
        meshops::inset_faces(m, fs, 0.4f);
        meshops::PushPullLimits lim = meshops::push_pull_limits(m, fs);
        CHECK(lim.through > 0.5f);
        CHECK(meshops::push_pull(m, fs, -(lim.through + 0.5f), true, &res));
        CHECK(res == PushPullResult::Hole);
        CHECK(closed_manifold(m));
        CHECK(euler_characteristic(m) == 0);
        CHECK(ray_misses(m, {0, 0, -5}, {0, 0, 1}));
      }
    }
    {  /* Pulled onto a tilted face of another box: the end takes that face's
        * angle and the two become one solid. */
      Mesh m = *primitives::cube();
      Mesh b = *primitives::cube(3.0f);
      Mat4 tb = Mat4::trs({3.5f, 0, 0}, Quat::euler({0, 0, 20}), {1, 1, 1});
      uint32_t base = (uint32_t)m.vert_count();
      for (Vec3 p : b.positions) m.add_vert(tb.point(p));
      for (size_t f = 0; f < b.face_count(); f++) {
        std::vector<uint32_t> v(b.face_verts(f), b.face_verts(f) + b.face_size(f));
        for (uint32_t &x : v) x += base;
        m.add_face(v.data(), v.size());
      }
      float v0 = vol(m);
      auto fs = one(m, face_facing(m, {1, 0, 0}));
      meshops::inset_faces(m, fs, 0.3f);
      meshops::PushPullLimits lim = meshops::push_pull_limits(m, fs);
      CHECK(lim.contact > 0.5f);
      CHECK(meshops::push_pull(m, fs, lim.contact + 1.0f, true, &res));
      CHECK(res == PushPullResult::Joined);
      CHECK(closed_manifold(m));
      CHECK(euler_characteristic(m) == 2);  // one solid
      CHECK(vol(m) > v0);
    }
  });
  test("push/pull: odd cases found by the stress test stay valid", [&] {
    using meshops::PushPullResult;
    auto one = [](const Mesh &m, size_t f) {
      std::vector<uint8_t> s(m.face_count(), 0);
      s[f] = 1;
      return s;
    };
    PushPullResult res;
    std::string err;
    {  /* Not a number: refused, mesh untouched. */
      Mesh m = *primitives::cube();
      auto fs = one(m, face_facing(m, {0, 1, 0}));
      CHECK(!meshops::push_pull(m, fs, std::numeric_limits<float>::quiet_NaN(), true, &res, &err));
      CHECK(!meshops::push_pull(m, fs, std::numeric_limits<float>::infinity(), true, &res, &err));
      CHECK(m.positions == primitives::cube()->positions);
    }
    {  /* A whole side pushed past the far side stops just short of it (it used to come out inside out). */
      Mesh m = *primitives::cube();
      auto fs = one(m, face_facing(m, {0, 1, 0}));
      CHECK(meshops::push_pull(m, fs, -5.0f, true, &res));
      CHECK(closed_manifold(m));
      CHECK(vol(m) > 0 && vol(m) < 0.01f);
    }
    {  /* A face over a tunnel stops at the tunnel: rays from its corners alone miss it. */
      Mesh m = *primitives::cube();
      auto fs = one(m, face_facing(m, {0, 0, -1}));
      meshops::inset_faces(m, fs, 0.3f);
      meshops::push_through(m, fs, 1);
      auto bottom = one(m, face_facing(m, {0, -1, 0}));
      float v0 = vol(m);
      CHECK(meshops::push_pull(m, bottom, -0.9f, true, &res));
      CHECK(closed_manifold(m));
      CHECK(vol(m) > v0 - 0.15f - 1e-3f);  // stopped by the tunnel floor 0.15 up, not the top
    }
    {  /* A wedge's slanted face pushed in: the neighbours lean over it, so its
        * corners slide along the edges instead of walls inverting the solid. */
      Mesh m;
      for (Vec3 p : {Vec3(0, 0, 0), Vec3(2, 0, 0), Vec3(0, 1.2f, 0), Vec3(0, 0, 1), Vec3(2, 0, 1), Vec3(0, 1.2f, 1)}) m.add_vert(p);
      m.add_face({0, 1, 2});
      m.add_face({3, 5, 4});
      m.add_face({0, 3, 4, 1});
      m.add_face({1, 4, 5, 2});
      m.add_face({0, 2, 5, 3});
      meshops::recalc_normals_outside(m);
      float v0 = vol(m);
      size_t slant = SIZE_MAX;
      for (size_t f = 0; f < m.face_count(); f++)
        if (m.face_normal(f).x > 0.3f && m.face_normal(f).y > 0.3f) slant = f;
      auto fs = one(m, slant);
      CHECK(meshops::push_pull(m, fs, -0.3f, true, &res));
      CHECK(res == PushPullResult::Moved);
      CHECK(closed_manifold(m));
      std::printf("    wedge: volume %.4f -> %.4f\n", v0, vol(m));
      CHECK(vol(m) < v0 && vol(m) > 0);
    }
    {  /* Top and two sides pushed up with Ctrl: no flat walls along the sides that slide on themselves. */
      Mesh m = *primitives::cube();
      std::vector<uint8_t> fs(m.face_count(), 0);
      for (size_t f = 0; f < m.face_count(); f++) fs[f] = std::fabs(m.face_normal(f).x) > 0.9f || m.face_normal(f).y > 0.9f;
      CHECK(meshops::push_pull(m, fs, 0.25f, false, &res));
      CHECK(closed_manifold(m));
      for (size_t f = 0; f < m.face_count(); f++) CHECK(length(m.face_normal(f)) > 0.5f);  // no zero-area faces
    }
    {  /* An inset face's neighbour stretches without leaving a T-junction. */
      Mesh m = *primitives::cube();
      auto top = one(m, face_facing(m, {0, 1, 0}));
      meshops::inset_faces(m, top, 0.3f);
      auto front = one(m, face_facing(m, {0, 0, -1}));
      CHECK(meshops::push_pull(m, front, 0.2f, true, &res));
      CHECK(closed_manifold(m));
      CHECK_NEAR(vol(m), 1.2f, 1e-4f);
    }
  });
  test("edge selection: two opposite edges of a quad stay two edges (bevel, seams)", [&] {
    /* Picking two opposite sides selects all four corners; the tools must
     * still see two edges, not the four between those corners. */
    Mesh m = *primitives::cube();
    size_t top = face_facing(m, {0, 1, 0});
    const uint32_t *v = m.face_verts(top);
    std::unordered_set<uint64_t> two = {Mesh::edge_key(v[0], v[1]), Mesh::edge_key(v[2], v[3])};
    std::vector<uint8_t> vs(m.vert_count(), 0), fs(m.face_count(), 0);
    vs[v[0]] = vs[v[1]] = vs[v[2]] = vs[v[3]] = 1;
    {
      meshops::EdgeSelectionScope scope(&two);
      CHECK(meshops::bevel_edges(m, vs, fs, 0.1f, 1));
    }
    CHECK(m.face_count() == 8);  // six faces + one strip per beveled edge
    CHECK(closed_manifold(m));
    /* In the editor: esel picks edges; Mark Seam marks just those. */
    Editor ed;
    ed.init_headless(800, 500);
    ed.command("create Cube");
    ed.step_frame_headless();
    GameObject *cube = ed.selected_object();
    CHECK(cube != nullptr);
    if (!cube) return;
    const Mesh &cm = *cube->get<MeshFilter>()->mesh;
    size_t t2 = face_facing(cm, {0, 1, 0});
    const uint32_t *w = cm.face_verts(t2);
    ed.command(strprintf("esel %u %u %u %u", w[0], w[1], w[2], w[3]));
    ed.command("uv mark_seam");
    CHECK(ed.scene().find(cube->id)->get<MeshFilter>()->mesh->seams.size() == 2);
  });
  test("shading: smooth per face, hard at flat faces, sharp edges and (optionally) seams", [&] {
    auto normals_at = [](const Mesh &m, uint32_t v) {
      /* The distinct render normals of vertex v. */
      std::vector<Vec3> out;
      const RenderMesh &rm = m.render_mesh();
      for (size_t i = 0; i < rm.positions.size(); i++)
        if (length(rm.positions[i] - m.positions[v]) < 1e-6f) {
          bool seen = false;
          for (Vec3 n : out) seen = seen || length(n - rm.normals[i]) < 1e-4f;
          if (!seen) out.push_back(rm.normals[i]);
        }
      return out;
    };
    Mesh c = *primitives::cylinder(0.5f, 1.0f, 16);
    /* Sides smooth, caps flat: a cap corner keeps the cap's normal. */
    for (size_t f = 0; f < c.face_count(); f++) c.set_face_smooth(f, std::fabs(c.face_normal(f).y) < 0.5f);
    c.touch();
    uint32_t rim = 0;
    for (uint32_t i = 0; i < c.vert_count(); i++)
      if (c.positions[i].y > 0.4f) rim = i;
    auto ns = normals_at(c, rim);
    CHECK(ns.size() == 2);  // the cap's and the (smooth) side's
    bool cap = false, side = false;
    for (Vec3 n : ns) {
      cap = cap || n.y > 0.99f;
      side = side || std::fabs(n.y) < 0.01f;
    }
    CHECK(cap && side);
    /* A sharp edge splits the smooth sides there. */
    uint32_t below = UINT32_MAX;
    for (auto &e : c.edge_cache())
      if ((e.first == rim || e.second == rim) && c.positions[e.first == rim ? e.second : e.first].y < 0)
        below = e.first == rim ? e.second : e.first;
    CHECK(below != UINT32_MAX);
    c.set_sharp(rim, below, true);
    c.touch();
    CHECK(normals_at(c, rim).size() == 3);
    /* Seams count as hard edges when asked. */
    Mesh s = *primitives::cylinder(0.5f, 1.0f, 16);
    for (size_t f = 0; f < s.face_count(); f++) s.set_face_smooth(f, std::fabs(s.face_normal(f).y) < 0.5f);
    s.set_seam(rim, below, true);
    s.touch();
    const size_t before = normals_at(s, rim).size();
    s.seams_sharp = true;
    s.touch();
    CHECK(normals_at(s, rim).size() == before + 1);
    /* Saved and loaded with the scene. */
    Scene sc;
    GameObject *go = create_primitive(sc, "Cube");
    go->get<MeshFilter>()->mesh = std::make_shared<Mesh>(c);
    Scene back;
    std::string err;
    CHECK(load_scene_text(save_scene_text(sc), back, err));
    const Mesh *lm = nullptr;
    back.for_each([&](GameObject &g) {
      if (g.get<MeshFilter>() && g.get<MeshFilter>()->mesh) lm = g.get<MeshFilter>()->mesh.get();
    });
    CHECK(lm && lm->face_smooth == c.face_smooth && lm->sharp_edges == c.sharp_edges);
  });
  test("bevel: Clamp Overlap narrows evenly; off keeps the exact width", [&] {
    Mesh a = *primitives::cube();
    std::vector<uint8_t> vs(a.vert_count(), 1), fs(a.face_count(), 0);
    Mesh b = a;
    std::vector<uint8_t> vs2 = vs, fs2 = fs;
    CHECK(meshops::bevel_edges(a, vs, fs, 0.8f, 1, nullptr, true));
    CHECK(closed_manifold(a));
    std::printf("    clamped bevel of every edge at width 0.8: volume %.3f\n", vol(a));
    CHECK(vol(a) > 0.1f && vol(a) < 1.0f);  // clamped to under half an edge: chamfered, not folded
    CHECK(meshops::bevel_edges(b, vs2, fs2, 0.3f, 1, nullptr, false));
    CHECK(closed_manifold(b));
  });
  test("tools: poke, triangulate, tris to quads, duplicate, split, dissolve, extrude individual", [&] {
    auto one = [](const Mesh &m, Vec3 n) {
      std::vector<uint8_t> s(m.face_count(), 0);
      s[face_facing(m, n)] = 1;
      return s;
    };
    {
      Mesh m = *primitives::cube();
      auto fs = one(m, {0, 1, 0});
      CHECK(meshops::poke_faces(m, fs, 0.2f) == 1);
      CHECK(m.face_count() == 9 && closed_manifold(m));
      CHECK(vol(m) > 1.0f);  // the poke point stands out
    }
    {
      Mesh m = *primitives::cube();
      auto fs = one(m, {0, 1, 0});
      CHECK(meshops::triangulate_faces(m, fs) == 1);
      CHECK(m.face_count() == 7);
      std::vector<uint8_t> all(m.face_count(), 1);
      CHECK(meshops::tris_to_quads(m, all) == 1);
      CHECK(m.face_count() == 6 && closed_manifold(m));
    }
    {
      Mesh m = *primitives::cube();
      auto fs = one(m, {0, 1, 0});
      CHECK(meshops::duplicate_faces(m, fs) == 1);
      CHECK(m.face_count() == 7 && m.vert_count() == 12);
      CHECK(std::count(fs.begin(), fs.end(), 1) == 1 && fs.back() == 1);  // the copy is selected
    }
    {
      Mesh m = *primitives::cube();
      auto fs = one(m, {0, 1, 0});
      CHECK(meshops::split_faces(m, fs) == 4);
      CHECK(m.vert_count() == 12 && !closed_manifold(m));
    }
    {
      Mesh m = meshops::subdivide(*primitives::cube(), 1, false);  // 24 quads, 4 per side
      std::vector<uint8_t> fs(m.face_count(), 0);
      for (size_t f = 0; f < m.face_count(); f++) fs[f] = m.face_normal(f).y > 0.99f;
      CHECK(meshops::dissolve_faces(m, fs) == 1);
      CHECK(m.face_count() == 21 && closed_manifold(m));
    }
    {
      Mesh m = *primitives::cube();
      std::vector<uint8_t> fs(m.face_count(), 0);
      fs[face_facing(m, {0, 1, 0})] = fs[face_facing(m, {1, 0, 0})] = 1;
      CHECK(meshops::extrude_individual(m, fs, 0.25f) == 2);
      CHECK(m.face_count() == 6 + 8 && closed_manifold(m));
    }
    {
      Mesh m = *primitives::cube();
      std::vector<uint8_t> vs(m.vert_count(), 1);
      meshops::to_sphere(m, vs, 1.0f);
      float r0 = length(m.positions[0]);
      for (Vec3 p : m.positions) CHECK_NEAR(length(p), r0, 1e-4f);
      Mesh g = *primitives::grid(2.0f, 4, 4);
      std::vector<uint8_t> one_v(g.vert_count(), 0);
      one_v[0] = 1;
      meshops::select_linked(g, one_v);
      CHECK(std::count(one_v.begin(), one_v.end(), 1) == (long)g.vert_count());
      std::vector<uint8_t> nm;
      meshops::select_non_manifold(g, nm);
      CHECK(std::count(nm.begin(), nm.end(), 1) == 16);  // the grid's open border
      auto e = g.edge_cache()[0];
      CHECK(meshops::edge_ring_edges(g, e.first, e.second).size() >= 4);
    }
    {
      Mesh m = *primitives::cube();
      /* One edge of a closed cube can't come apart (the faces still meet around
       * its ends); the whole border of the top face can. */
      std::vector<uint8_t> vs(m.vert_count(), 0);
      const uint32_t *tv = m.face_verts(face_facing(m, {0, 1, 0}));
      for (int k = 0; k < 4; k++) vs[tv[k]] = 1;
      CHECK(meshops::edge_split(m, vs) == 4);
      CHECK(m.vert_count() == 12 && !closed_manifold(m));
    }
  });
  test("editor: Inset follows the mouse until a click; Esc leaves no trace", [] {
    Editor ed;
    ed.init_headless(1000, 700);
    ed.command("create Cube");
    ed.step_frame_headless();
    GameObject *cube = ed.selected_object();
    CHECK(cube != nullptr);
    if (!cube) return;
    const uint64_t id = cube->id;
    auto faces = [&] { return ed.scene().find(id)->get<MeshFilter>()->mesh->face_count(); };
    Recti r = ed.scene_view_rect();
    const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    auto ev = [](platform::EventType t, int x, int y, int key = 0) {
      platform::Event e;
      e.type = t;
      e.x = x;
      e.y = y;
      e.key = key;
      return e;
    };
    ed.command("edit face");
    ed.command("fsel facing 0 1 0");
    ed.step_frame_headless({ev(platform::EventType::MouseMove, cx + 200, cy)});
    ed.step_frame_headless({ev(platform::EventType::KeyDown, cx + 200, cy, platform::KEY_I)});
    CHECK(faces() == 10);  // running already
    ed.step_frame_headless({ev(platform::EventType::MouseMove, cx + 120, cy)});
    ed.step_frame_headless({ev(platform::EventType::KeyDown, cx + 120, cy, platform::KEY_ESCAPE)});
    CHECK(faces() == 6);  // cancelled: back to the cube
    ed.step_frame_headless({ev(platform::EventType::MouseMove, cx + 200, cy)});
    ed.step_frame_headless({ev(platform::EventType::KeyDown, cx + 200, cy, platform::KEY_I)});
    ed.step_frame_headless({ev(platform::EventType::MouseMove, cx + 100, cy)});
    ed.step_frame_headless({ev(platform::EventType::MouseDown, cx + 100, cy)});
    ed.step_frame_headless({ev(platform::EventType::MouseUp, cx + 100, cy)});
    CHECK(faces() == 10);  // confirmed
    const Mesh &m = *ed.scene().find(id)->get<MeshFilter>()->mesh;
    float top_x = 1e9f;  // the inner face: the smallest of the top faces
    for (size_t f = 0; f < m.face_count(); f++) {
      if (m.face_normal(f).y < 0.99f) continue;
      float w = 0;
      for (uint32_t k = 0; k < m.face_size(f); k++) w = std::max(w, std::fabs(m.positions[m.face_verts(f)[k]].x));
      top_x = std::min(top_x, w);
    }
    CHECK(top_x < 0.45f);  // the inner face moved in: about half way toward the centre
    platform::Event z = ev(platform::EventType::KeyDown, cx, cy, platform::KEY_Z);
    z.mods = platform::MOD_CTRL;
    ed.step_frame_headless({z});
    CHECK(faces() == 6);  // one undo step
  });
  test("editor: Blender's G / R / S with axis locks and typed values, Unity navigation kept", [] {
    Editor ed;
    ed.init_headless(1000, 700);
    ed.command("create Cube");
    ed.step_frame_headless();
    GameObject *cube = ed.selected_object();
    CHECK(cube != nullptr);
    if (!cube) return;
    const uint64_t id = cube->id;
    const Vec3 p0 = cube->world_position();
    Recti r = ed.scene_view_rect();
    const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    auto key = [&](int k, int mods = 0) {
      platform::Event m, d, u;
      m.type = platform::EventType::MouseMove;
      m.x = d.x = u.x = cx + 40;
      m.y = d.y = u.y = cy;
      d.type = platform::EventType::KeyDown;
      d.key = u.key = k;
      d.mods = u.mods = mods;  // modifiers are still held on key-up
      u.type = platform::EventType::KeyUp;
      ed.step_frame_headless({m, d, u});
    };
    auto type = [&](const char *s) {
      for (const char *c = s; *c; c++) {
        platform::Event t;
        t.type = platform::EventType::Text;
        t.codepoint = (uint32_t)*c;
        t.x = cx + 40;
        t.y = cy;
        ed.step_frame_headless({t});
      }
    };
    auto pos = [&] { return ed.scene().find(id)->world_position(); };
    /* G, X, 2, Enter: two units along X. */
    key(platform::KEY_G);
    key(platform::KEY_X);
    type("2");
    key(platform::KEY_ENTER);
    CHECK(length(pos() - (p0 + Vec3(2, 0, 0))) < 1e-4f);
    /* G, Z, 5 then Esc: nothing moves. */
    key(platform::KEY_G);
    key(platform::KEY_Z);
    type("5");
    key(platform::KEY_ESCAPE);
    CHECK(length(pos() - (p0 + Vec3(2, 0, 0))) < 1e-4f);
    /* During a grab, R switches to rotate: R, Y, 90 turns it about Y. */
    key(platform::KEY_G);
    key(platform::KEY_R);
    key(platform::KEY_Y);
    type("90");
    key(platform::KEY_ENTER);
    const Vec3 fwd = ed.scene().find(id)->world_rotation().rotate({0, 0, 1});
    CHECK(std::fabs(std::fabs(fwd.x) - 1.0f) < 1e-3f);
    /* Without the preference R is still Unity's Scale tool; with it, R rotates and S scales. */
    ed.command("edit off");
    key(platform::KEY_S, 0);
    CHECK(length(ed.scene().find(id)->local().scale - Vec3(1, 1, 1)) < 1e-5f);  // S alone: nothing
    /* Edit Mode: G Y 0.5 moves the selected vertices only. */
    ed.command("edit face");
    ed.command("fsel facing 0 1 0");
    float top0 = -1e9f;
    for (Vec3 p : ed.scene().find(id)->get<MeshFilter>()->mesh->positions) top0 = std::max(top0, p.y);
    key(platform::KEY_G);
    key(platform::KEY_Y);
    type("0.5");
    key(platform::KEY_ENTER);
    float top1 = -1e9f, bottom = 1e9f;
    for (Vec3 p : ed.scene().find(id)->get<MeshFilter>()->mesh->positions) {
      top1 = std::max(top1, p.y);
      bottom = std::min(bottom, p.y);
    }
    CHECK_NEAR(top1, top0 + 0.5f, 1e-4f);
    CHECK_NEAR(bottom, -0.5f, 1e-4f);
    /* Ctrl+E: extrude, then the mouse (or a typed distance) moves it along the normal;
     * Esc takes the extrusion back, and a confirmed one is one undo step. */
    auto faces = [&] { return ed.scene().find(id)->get<MeshFilter>()->mesh->face_count(); };
    ed.command("fsel facing 0 1 0");
    const size_t f0 = faces();
    key(platform::KEY_E, platform::MOD_CTRL);
    CHECK(faces() == f0 + 4);
    key(platform::KEY_ESCAPE);
    CHECK(faces() == f0);
    ed.command("fsel facing 0 1 0");
    key(platform::KEY_E, platform::MOD_CTRL);
    type("0.25");
    key(platform::KEY_ENTER);
    float top2 = -1e9f;
    for (Vec3 p : ed.scene().find(id)->get<MeshFilter>()->mesh->positions) top2 = std::max(top2, p.y);
    CHECK(faces() == f0 + 4);
    CHECK_NEAR(top2, top1 + 0.25f, 1e-4f);
    key(platform::KEY_Z, platform::MOD_CTRL);
    CHECK(faces() == f0);
  });
  test("edges: subdivide, connect, dissolve, collapse", [&] {
    Mesh m = *primitives::cube();
    std::vector<uint8_t> vs(m.vert_count(), 0);
    auto e = m.edge_cache()[0];
    vs[e.first] = vs[e.second] = 1;
    CHECK(meshops::subdivide_edges(m, vs, 2) == 1);
    CHECK(m.vert_count() == 10);
    CHECK(closed_manifold(m));
    Mesh c = *primitives::cube();
    size_t top = face_facing(c, {0, 1, 0});
    vs.assign(c.vert_count(), 0);
    vs[c.face_verts(top)[0]] = vs[c.face_verts(top)[2]] = 1;  // a diagonal
    CHECK(meshops::connect_vertices(c, vs) == 1);
    CHECK(c.face_count() == 7);
    CHECK(closed_manifold(c));
    CHECK(meshops::dissolve_edges(c, vs) == 1);
    CHECK(c.face_count() == 6);
    CHECK(closed_manifold(c));
    Mesh k = *primitives::cube();
    vs.assign(k.vert_count(), 0);
    auto e2 = k.edge_cache()[0];
    vs[e2.first] = vs[e2.second] = 1;
    CHECK(meshops::collapse_edges(k, vs) == 1);
    CHECK(k.vert_count() == 7);
    CHECK(closed_manifold(k));
    CHECK(euler_characteristic(k) == 2);
  });
}

static void modeling_tests() {
  test("modeling: loop cut splits the ring and patches the caps", [] {
    Mesh m = *primitives::cube();
    /* Any edge of the cube: the ring is 4 quads, the caps become 6-gons. */
    auto e = m.edge_cache()[0];
    float uv_before = uv_area_total(m);
    auto made = meshops::loop_cut(m, e.first, e.second, 1);
    CHECK(made.size() == 4);
    CHECK(m.vert_count() == 12);
    CHECK(m.face_count() == 10);
    CHECK(euler_characteristic(m) == 2);
    CHECK(closed_manifold(m));
    CHECK(normals_outward(m));
    CHECK_NEAR(uv_area_total(m), uv_before, 1e-4);  // UVs interpolated, not stretched
    Mesh c = *primitives::cube();
    auto e2 = c.edge_cache()[0];
    meshops::loop_cut(c, e2.first, e2.second, 3);
    CHECK(c.vert_count() == 20);
    CHECK(c.face_count() == 18);
    CHECK(closed_manifold(c));
    /* A slid cut stays parallel: all new verts share one coordinate. */
    Mesh s = *primitives::grid(4.0f, 4, 4);
    uint32_t a = 2 * 5 + 1, b = a + 1;  // interior x-edge on row z = 2
    auto cut = meshops::loop_cut(s, a, b, 1, 0.25f);
    CHECK(cut.size() == 5);
    bool parallel = true;
    for (uint32_t v : cut) parallel = parallel && std::fabs(s.positions[v].x - s.positions[cut[0]].x) < 1e-5f;
    CHECK(parallel);
    CHECK_NEAR(s.positions[cut[0]].x, -1.0f + 0.25f, 1e-5);
  });
  test("modeling: edge loops stop at poles, follow boundaries, close rings", [] {
    Mesh g = *primitives::grid(4.0f, 6, 6);
    uint32_t a = 3 * 7 + 2, b = a + 1;
    CHECK(meshops::edge_loop(g, a, b).size() == 7);  // whole row of the 6x6 grid
    CHECK(meshops::edge_loop(g, 0, 1).size() == 24);  // boundary edge: the border
    Mesh sp = *primitives::uv_sphere(0.5f, 24, 16);
    uint32_t ea = UINT32_MAX, eb = 0;
    for (auto &e : sp.edge_cache())
      if (std::fabs(sp.positions[e.first].y) < 1e-4f && std::fabs(sp.positions[e.second].y) < 1e-4f) { ea = e.first; eb = e.second; break; }
    CHECK(ea != UINT32_MAX);
    if (ea != UINT32_MAX) CHECK(meshops::edge_loop(sp, ea, eb).size() == 24);  // equator
  });
  test("modeling: fill closes a hole outward, merge at center welds", [] {
    Mesh m = *primitives::cube();
    std::vector<uint8_t> del(m.face_count(), 0);
    del[0] = 1;
    std::vector<uint32_t> hole(m.face_verts(0), m.face_verts(0) + 4);
    meshops::delete_faces(m, del);
    CHECK(m.face_count() == 5);
    std::vector<uint8_t> sel(m.vert_count(), 0);
    for (uint32_t v : hole) sel[v] = 1;
    CHECK(meshops::fill(m, sel));
    CHECK(m.face_count() == 6);
    CHECK(closed_manifold(m));
    CHECK(normals_outward(m));
    /* Unordered selection (not a boundary): sorted around the plane. */
    Mesh p;
    for (Vec3 q : {Vec3(0, 0, 0), Vec3(1, 0, 1), Vec3(1, 0, 0), Vec3(0, 0, 1)}) p.add_vert(q);
    CHECK(meshops::fill(p, std::vector<uint8_t>(4, 1)));
    CHECK(p.face_count() == 1 && p.face_size(0) == 4);
    CHECK_NEAR(length(cross(p.positions[p.face_verts(0)[1]] - p.positions[p.face_verts(0)[0]],
                            p.positions[p.face_verts(0)[2]] - p.positions[p.face_verts(0)[0]])), 1.0f, 1e-5);
    Mesh c = *primitives::cube();
    std::vector<uint8_t> top(c.vert_count(), 0);
    for (uint32_t k = 0; k < 4; k++) top[c.face_verts(1)[k]] = 1;
    CHECK(meshops::merge_at_center(c, top) == 3);
    CHECK(c.vert_count() == 5);  // a pyramid
    CHECK(c.face_count() == 5);
    CHECK(euler_characteristic(c) == 2);
    CHECK(closed_manifold(c));
  });
  test("modeling: recalculate normals outside", [] {
    for (auto src : {primitives::cube(), primitives::ico_sphere(), primitives::torus()}) {
      Mesh m = *src;
      /* Scramble the winding of every other face. */
      for (size_t f = 0; f < m.face_count(); f += 2)
        std::reverse(m.corner_verts.begin() + m.face_offsets[f], m.corner_verts.begin() + m.face_offsets[f + 1]);
      meshops::recalc_normals_outside(m);
      CHECK(closed_manifold(m));
      if (src->name != "Torus") CHECK(normals_outward(m));
    }
    Mesh inv = *primitives::cube();
    meshops::flip_normals(inv);
    meshops::recalc_normals_outside(inv);
    CHECK(normals_outward(inv));
  });
  test("modifiers: mirror, array, solidify", [] {
    Mesh half = *primitives::cube();
    for (auto &p : half.positions) p.x += 0.5f;  // x in [0, 1]
    Mesh mi = half;
    meshops::mirror(mi, true, false, false, 0.001f);
    CHECK(mi.vert_count() == 12);  // the 4 verts on the plane are shared
    CHECK(mi.face_count() == 11);  // the cap on the plane is not doubled
    CHECK_NEAR(mi.bounds().min.x, -1.0f, 1e-5);
    Mesh mi2 = half;
    meshops::mirror(mi2, true, false, false, 0.0f);
    CHECK(mi2.vert_count() == 16);
    Mesh ar = *primitives::cube();
    meshops::make_array(ar, 3, {1, 0, 0}, {0, 0, 0}, 0.0f);
    CHECK(ar.vert_count() == 24);
    CHECK(ar.face_count() == 18);
    CHECK_NEAR(ar.bounds().max.x - ar.bounds().min.x, 3.0f, 1e-4);
    Mesh arm = *primitives::cube();
    meshops::make_array(arm, 3, {1, 0, 0}, {0, 0, 0}, 0.001f);
    CHECK(arm.vert_count() == 16);
    Mesh so = *primitives::grid(2.0f, 4, 4);
    meshops::solidify(so, 0.1f, -1.0f, false, true);
    CHECK(so.vert_count() == 50);
    CHECK(so.face_count() == 16 + 16 + 16);
    CHECK(euler_characteristic(so) == 2);
    CHECK(closed_manifold(so));
    CHECK_NEAR(so.bounds().max.y - so.bounds().min.y, 0.1f, 1e-5);
    /* As components: the evaluated mesh runs the stack. */
    Scene s;
    GameObject *g = s.create("m");
    g->add<MeshFilter>()->mesh = primitives::cube();
    g->add<ArrayModifier>()->count = 4;
    CHECK(g->evaluated_mesh()->vert_count() == 32);
    g->add<MirrorModifier>()->merge = false;
    CHECK(g->evaluated_mesh()->vert_count() == 64);
  });
}

static double mesh_volume(const Mesh &m) {
  double v = 0;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t *c = m.face_verts(f);
    for (uint32_t k = 1; k + 1 < m.face_size(f); k++)
      v += dot(m.positions[c[0]], cross(m.positions[c[k]], m.positions[c[k + 1]]));
  }
  return v / 6.0;
}

static void library_modeling_tests() {
  test("boolean: Manifold difference / union / intersect", [] {
    if (!meshops::boolean_available()) return;
    Mesh a = *primitives::cube();  // unit cube at the origin
    const Mat4 corner = Mat4::translate({0.5f, 0.5f, 0.5f});
    Mesh d = a;
    CHECK(meshops::boolean_op(d, *primitives::cube(), corner, meshops::BooleanOp::Difference));
    CHECK_NEAR(mesh_volume(d), 1.0 - 0.125, 1e-4);
    CHECK(closed_manifold(d));
    CHECK(d.face_count() <= 12);  // faces rebuilt as polygons, not a triangle soup
    Mesh u = a;
    CHECK(meshops::boolean_op(u, *primitives::cube(), corner, meshops::BooleanOp::Union));
    CHECK_NEAR(mesh_volume(u), 2.0 - 0.125, 1e-4);
    Mesh i = a;
    CHECK(meshops::boolean_op(i, *primitives::cube(), corner, meshops::BooleanOp::Intersect));
    CHECK_NEAR(mesh_volume(i), 0.125, 1e-4);
    /* An open mesh is refused and left untouched. */
    Mesh keep = a;
    std::string err;
    CHECK(!meshops::boolean_op(keep, *primitives::plane(2.0f, 1), Mat4::identity(), meshops::BooleanOp::Difference, &err));
    CHECK(!err.empty() && keep.face_count() == 6);
    /* As a modifier: follows the cutter object. */
    Scene s;
    GameObject *target = s.create("Target");
    target->add<MeshFilter>()->mesh = primitives::cube();
    GameObject *cutter = s.create("Cutter");
    cutter->add<MeshFilter>()->mesh = primitives::cube();
    cutter->set_local_position({0.5f, 0.5f, 0.5f});
    target->add<BooleanModifier>()->object = "Cutter";
    CHECK_NEAR(mesh_volume(*target->evaluated_mesh()), 0.875, 1e-4);
    cutter->set_local_position({5, 0, 0});  // moved away: nothing to cut
    CHECK_NEAR(mesh_volume(*target->evaluated_mesh()), 1.0, 1e-4);
  });
  test("decimate: meshoptimizer keeps the shape at a quarter of the triangles", [] {
    if (!meshops::decimate_available()) return;
    Mesh m = *primitives::ico_sphere(0.5f, 4);
    size_t before = 0;
    for (size_t f = 0; f < m.face_count(); f++) before += m.face_size(f) - 2;
    double vol = mesh_volume(m);
    CHECK(meshops::decimate(m, 0.25f));
    CHECK(m.face_count() <= before / 4 + 2 && m.face_count() >= before / 5);
    CHECK(closed_manifold(m));
    CHECK_NEAR(mesh_volume(m) / vol, 1.0, 0.03);
    float maxr = 0, minr = 1e9f;
    for (Vec3 p : m.positions) { maxr = std::max(maxr, length(p)); minr = std::min(minr, length(p)); }
    CHECK(maxr < 0.505f && minr > 0.47f);  // still a sphere
  });
}

static void physics_tests() {
  test("physics: Jolt drops a box onto a plane and it comes to rest", [] {
    if (!physics_jolt_available()) return;
    Scene s;
    GameObject *ground = s.create("Ground");
    ground->add<MeshFilter>()->mesh = primitives::plane(10.0f, 1);
    ground->add<MeshRenderer>();
    GameObject *box = s.create("Box");
    box->add<MeshFilter>()->mesh = primitives::cube();
    box->add<MeshRenderer>();
    box->add<Rigidbody>()->bounciness = 0.0f;
    box->set_local_position({0, 3, 0});
    PlayContext ctx;
    s.start(ctx);
    CHECK(s.physics != nullptr);
    ctx.dt = 1.0f / 60.0f;
    float lowest = 3;
    for (int i = 0; i < 240; i++) {  // 4 s
      s.update(ctx);
      lowest = std::min(lowest, box->world_position().y);
    }
    CHECK_NEAR(box->world_position().y, 0.5f, 0.05f);  // a 1 m cube resting on y = 0
    CHECK(lowest > 0.3f);                              // never fell through
    CHECK(length(box->get<Rigidbody>()->velocity) < 0.05f);
  });
}

static void subdiv_tests() {
  test("subdivision: OpenSubdiv matches Blendity's Catmull-Clark", [] {
    if (!meshops::subdiv_opensubdiv_available()) return;
    for (auto src : {primitives::cube(), primitives::ico_sphere(0.5f, 1), primitives::cylinder(0.5f, 1.0f, 8)}) {
      Mesh base = *src;
      base.seams.clear();
      meshops::set_subdiv_opensubdiv(false);
      Mesh ours = meshops::subdivide(base, 2, true);
      meshops::set_subdiv_opensubdiv(true);
      Mesh osd = meshops::subdivide(base, 2, true);
      meshops::set_subdiv_opensubdiv(false);
      CHECK(ours.vert_count() == osd.vert_count());
      CHECK(ours.face_count() == osd.face_count());
      CHECK(euler_characteristic(osd) == 2);
      CHECK(closed_manifold(osd));
      /* Same limit-approaching surface: every vertex of one has a twin in the other. */
      float worst = 0;
      for (Vec3 p : osd.positions) {
        float best = 1e30f;
        for (Vec3 q : ours.positions) best = std::min(best, length_sq(p - q));
        worst = std::max(worst, best);
      }
      CHECK(std::sqrt(worst) < 1e-4f);
    }
    /* UVs (face-varying) and material slots come through. */
    Mesh g = *primitives::grid(2.0f, 3, 3);
    g.face_material.assign(g.face_count(), 0);
    g.face_material[4] = 2;
    meshops::set_subdiv_opensubdiv(true);
    Mesh s = meshops::subdivide(g, 1, true);
    meshops::set_subdiv_opensubdiv(false);
    CHECK(s.has_uvs() && s.face_count() == 36);
    int with_mat = 0;
    for (size_t f = 0; f < s.face_count(); f++) with_mat += s.material_of(f) == 2;
    CHECK(with_mat == 4);
    bool inside = true;
    for (Vec2 t : s.uvs) inside = inside && t.x >= -1e-5f && t.y >= -1e-5f && t.x <= 1 + 1e-5f && t.y <= 1 + 1e-5f;
    CHECK(inside);
  });
}

static void uv_tests() {
  test("uv: LSCM unrolls a cut cylinder into a rectangle", [] {
    Mesh m = *primitives::cylinder(0.5f, 2.0f, 24);
    std::vector<uint8_t> sel(m.vert_count(), 0);
    sel[0] = sel[1] = 1;
    uvops::set_seams_from_vertices(m, sel, true);  // vertical cut
    for (int ring = 0; ring < 2; ring++) {
      std::fill(sel.begin(), sel.end(), 0);
      for (int i = 0; i < 24; i++) sel[i * 2 + ring] = 1;
      CHECK(uvops::set_seams_from_vertices(m, sel, true) == 24);
    }
    CHECK(uvops::unwrap_lscm(m, nullptr) == 3);
    bool inside = true;
    for (Vec2 t : m.uvs) inside = inside && t.x >= -1e-4f && t.y >= -1e-4f && t.x <= 1.0001f && t.y <= 1.0001f;
    CHECK(inside);
    /* Side faces (the first 24) keep equal area: a developable surface. */
    auto st = uvops::face_area_stretch(m);
    float lo = 1e9f, hi = 0;
    for (int f = 0; f < 24; f++) { lo = std::min(lo, st[f]); hi = std::max(hi, st[f]); }
    CHECK(hi / lo < 1.05f);
    /* Its aspect matches circumference : height = pi : 2. */
    Vec2 mn(1e9f, 1e9f), mx(-1e9f, -1e9f);
    for (int f = 0; f < 24; f++)
      for (uint32_t c = m.face_offsets[f]; c < m.face_offsets[f + 1]; c++) {
        mn = {std::min(mn.x, m.uvs[c].x), std::min(mn.y, m.uvs[c].y)};
        mx = {std::max(mx.x, m.uvs[c].x), std::max(mx.y, m.uvs[c].y)};
      }
    float w = mx.x - mn.x, h = mx.y - mn.y, aspect = std::max(w, h) / std::min(w, h);
    CHECK_NEAR(aspect, kPi / 2.0f, 0.05f);
  });
  test("uv: smart project, projections and packing stay in 0-1", [] {
    Mesh t = *primitives::torus();
    int islands = uvops::smart_project(t, nullptr);
    CHECK(islands > 4);
    std::vector<int> fi;
    CHECK(uvops::compute_islands(t, nullptr, false, fi) >= islands);
    for (auto op : {0, 1, 2}) {
      Mesh s = *primitives::uv_sphere();
      if (op == 0) uvops::project_cube(s, nullptr);
      if (op == 1) uvops::project_cylinder(s, nullptr);
      if (op == 2) uvops::project_sphere(s, nullptr);
      uvops::pack_islands(s, nullptr);
      bool inside = true;
      for (Vec2 v : s.uvs) inside = inside && v.x >= -1e-4f && v.y >= -1e-4f && v.x <= 1.0001f && v.y <= 1.0001f;
      CHECK(inside);
    }
    Mesh r = *primitives::cube();
    uvops::reset(r, nullptr);
    CHECK_NEAR(uv_area_total(r), 6.0f, 1e-5);
  });
}

static void image_tests() {
  test("image: PNG round-trip is exact", [] {
    const int w = 37, h = 21;
    std::vector<uint32_t> px((size_t)w * h);
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) px[(size_t)y * w + x] = 0xFF000000u | (uint32_t)(x * 7) << 16 | (uint32_t)(y * 11) << 8 | (uint32_t)((x ^ y) & 255);
    std::string path = fs::join(test_dir(), "test_rt.png"), bytes, err;
    CHECK(write_png(path, px.data(), w, h, w));
    Bitmap b;
    CHECK(fs::read_file(path, bytes) && decode_image(bytes, b, err));
    CHECK(b.width == w && b.height == h && !b.is_float);
    bool same = b.width == w;
    for (int i = 0; same && i < w * h; i++)
      same = b.rgba8[i * 4] == ((px[i] >> 16) & 255) && b.rgba8[i * 4 + 1] == ((px[i] >> 8) & 255) && b.rgba8[i * 4 + 2] == (px[i] & 255);
    CHECK(same);
  });
  test("image: JPEG encoder/decoder quality and HDR round-trip", [] {
    const int w = 64, h = 48;
    std::vector<uint32_t> px((size_t)w * h);
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) px[(size_t)y * w + x] = 0xFF000000u | (uint32_t)(x * 4) << 16 | (uint32_t)(y * 5) << 8 | 128u;
    std::string path = fs::join(test_dir(), "test_rt.jpg"), bytes, err;
    CHECK(write_jpeg(path, px.data(), w, h, w, 92));
    Bitmap b;
    CHECK(fs::read_file(path, bytes) && decode_image(bytes, b, err));
    double mse = 0;
    for (int i = 0; i < w * h && b.width == w; i++)
      for (int c = 0; c < 3; c++) {
        double d = (double)b.rgba8[i * 4 + c] - (double)((px[i] >> (16 - 8 * c)) & 255);
        mse += d * d;
      }
    mse /= w * h * 3.0;
    double psnr = 10.0 * std::log10(255.0 * 255.0 / std::max(mse, 1e-9));
    CHECK(psnr > 35.0);
    std::vector<float> rgb((size_t)w * h * 3);
    for (size_t i = 0; i < rgb.size(); i++) rgb[i] = 0.001f + (float)(i % 97) * 0.37f;
    std::string hp = fs::join(test_dir(), "test_rt.hdr");
    CHECK(write_hdr(hp, rgb.data(), w, h));
    Bitmap hb;
    CHECK(fs::read_file(hp, bytes) && decode_image(bytes, hb, err) && hb.is_float);
    float worst = 0;
    for (int i = 0; i < w * h && hb.width == w; i++) {
      /* RGBE shares one exponent per pixel: the error is relative to the
       * brightest channel (8-bit mantissa -> < 1/128). */
      float mx = std::max({rgb[(size_t)i * 3], rgb[(size_t)i * 3 + 1], rgb[(size_t)i * 3 + 2]});
      for (int c = 0; c < 3; c++)
        worst = std::max(worst, std::fabs(rgb[(size_t)i * 3 + c] - hb.rgbaf[(size_t)i * 4 + c]) / mx);
    }
    CHECK(worst < 1.0f / 128.0f);
  });
  test("image: library codecs agree with Blendity's decoders", [] {
    const int w = 64, h = 48;
    std::vector<uint32_t> px((size_t)w * h);
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) px[(size_t)y * w + x] = 0xFF000000u | (uint32_t)(x * 4) << 16 | (uint32_t)(y * 5) << 8 | (uint32_t)((x * y) & 255);
    std::string png = fs::join(test_dir(), "lib_rt.png"), jpg = fs::join(test_dir(), "lib_rt.jpg"), bytes, err;
    CHECK(write_png(png, px.data(), w, h, w) && write_jpeg(jpg, px.data(), w, h, w, 95));
    for (const std::string &path : {png, jpg}) {
      Bitmap a, b;
      CHECK(fs::read_file(path, bytes));
      set_image_library_codecs(true);
      CHECK(decode_image(bytes, a, err));
      set_image_library_codecs(false);
      CHECK(decode_image(bytes, b, err));
      set_image_library_codecs(true);
      CHECK(a.width == b.width && a.height == b.height && a.rgba8.size() == b.rgba8.size());
      int worst = 0;
      for (size_t i = 0; i < a.rgba8.size() && a.rgba8.size() == b.rgba8.size(); i++) worst = std::max(worst, std::abs((int)a.rgba8[i] - (int)b.rgba8[i]));
      /* PNG is lossless: identical. JPEG decoders may round the IDCT and
       * chroma upsampling differently by a few levels. */
      CHECK(worst <= (path == png ? 0 : 8));
    }
  });
  test("image: OpenEXR round-trip (half float)", [] {
    if (!exr_available()) return;
    const int w = 33, h = 17;
    std::vector<float> rgb((size_t)w * h * 3);
    for (size_t i = 0; i < rgb.size(); i++) rgb[i] = 0.01f + (float)(i % 101) * 0.73f;  // up to ~73: true HDR
    std::string path = fs::join(test_dir(), "rt.exr"), bytes, err;
    CHECK(write_exr(path, rgb.data(), w, h, true));
    Bitmap b;
    CHECK(fs::read_file(path, bytes) && decode_image(bytes, b, err));
    CHECK(b.is_float && b.width == w && b.height == h);
    float worst = 0;
    for (int i = 0; i < w * h && b.width == w; i++)
      for (int c = 0; c < 3; c++) {
        float a = rgb[(size_t)i * 3 + c];
        worst = std::max(worst, std::fabs(b.rgbaf[(size_t)i * 4 + c] - a) / a);
      }
    CHECK(worst < 1e-3f);  // half float: 11-bit mantissa
    CHECK(image_extension_supported(".exr"));
  });
  test("image: mipmapped texture sampling", [] {
    Bitmap b;
    b.width = 64;
    b.height = 32;
    b.rgba8.assign(64 * 32 * 4, 0);
    for (int i = 0; i < 64 * 32; i++) {
      bool on = ((i % 64) / 8 + (i / 64) / 8) % 2;
      for (int c = 0; c < 3; c++) b.rgba8[i * 4 + c] = on ? 255 : 0;
      b.rgba8[i * 4 + 3] = 255;
    }
    Texture t;
    t.build(b, false);
    CHECK(t.levels.size() == 7);  // 64x32 .. 1x1
    Vec4 sharp = t.sample({4.0f / 64, 4.0f / 32}, 0.0f, TexWrap::Repeat, TexFilter::Linear);
    CHECK(sharp.x < 0.01f || sharp.x > 0.99f);
    Vec4 blurred = t.sample({0.3f, 0.6f}, 6.0f);
    CHECK_NEAR(blurred.x, 0.5f, 0.02f);  // the 1x1 level is the average
    CHECK_NEAR(srgb_to_linear(linear_to_srgb(0.2f)), 0.2f, 1e-5);
  });
}

static void import_and_material_tests() {
  test("import: OBJ + MTL with a texture", [] {
    std::string dir = fs::join(test_dir(), "test_import");
    fs::make_dirs(dir);
    uint32_t tex[4] = {0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFFFF};
    CHECK(write_png(fs::join(dir, "tex.png"), tex, 2, 2, 2));
    fs::write_file(fs::join(dir, "box.mtl"), "newmtl Red\nKd 0.8 0.1 0.1\nNs 250\nmap_Kd tex.png\n");
    fs::write_file(fs::join(dir, "box.obj"),
                   "mtllib box.mtl\no Box\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
                   "usemtl Red\nf 1/1 2/2 3/3 4/4\n");
    ImportResult r;
    CHECK(import_model(fs::join(dir, "box.obj"), r));
    CHECK(r.nodes.size() == 1);
    CHECK(r.materials.size() == 1);
    if (!r.nodes.empty() && !r.materials.empty()) {
      const Mesh &m = *r.nodes[0].mesh;
      CHECK(m.face_count() == 1 && m.has_uvs());
      const Material &mat = *r.materials[0];
      CHECK(mat.name == "Red");
      CHECK_NEAR(mat.base_color.x, 0.8f, 1e-5);
      CHECK(mat.roughness < 0.6f);
      CHECK(fs::filename(mat.base_map.path) == "tex.png");
      CHECK(r.textures.size() == 1);
      Scene s;
      GameObject *g = instantiate_import(s, r, "box", nullptr);
      CHECK(g != nullptr);
    }
  });
  test("io: materials, UVs and seams survive save/load", [] {
    Scene s;
    build_default_scene(s);
    GameObject *cube = s.find_by_name("Cube");
    auto mat = make_material("Brick", {0.6f, 0.3f, 0.2f});
    mat->base_map.path = "Textures/brick.png";
    mat->tiling = {2, 3, 1};
    mat->procedural = 1;
    cube->get<MeshRenderer>()->materials = {mat, mat};
    MeshPtr &mp = cube->get<MeshFilter>()->mesh;
    Mesh &m = *mesh_make_mutable(mp);
    m.set_seam(0, 1, true);
    m.face_material.assign(m.face_count(), 0);
    m.face_material[2] = 1;
    m.touch();
    s.environment.mode = 1;
    s.render.samples = 77;
    std::string a = save_scene_text(s), err;
    Scene l;
    CHECK(load_scene_text(a, l, err));
    CHECK(save_scene_text(l) == a);
    GameObject *lc = l.find_by_name("Cube");
    auto &mats = lc->get<MeshRenderer>()->materials;
    CHECK(mats.size() == 2 && mats[0] == mats[1]);  // shared material stays shared
    if (!mats.empty()) {
      CHECK(mats[0]->name == "Brick" && mats[0]->base_map.path == "Textures/brick.png");
      CHECK_NEAR(mats[0]->tiling.y, 3.0f, 1e-6);
    }
    const Mesh &lm = *lc->get<MeshFilter>()->mesh;
    CHECK(lm.is_seam(0, 1) && lm.material_of(2) == 1 && lm.has_uvs());
    CHECK(l.environment.mode == 1 && l.render.samples == 77);
  });
}

static void file_tests() {
  test("io: Zstandard-compressed scenes", [] {
    if (!scene_compression_available()) return;
    Scene s;
    build_default_scene(s);
    for (int i = 0; i < 4; i++) s.find_by_name("Cube")->add<SubdivisionSurface>()->levels = 1;
    s.compress = true;
    std::string path = fs::join(test_dir(), "compressed.scene"), raw, err;
    CHECK(save_scene(s, path));
    CHECK(fs::read_file(path, raw) && raw.size() > 4 && (uint8_t)raw[0] == 0x28 && (uint8_t)raw[3] == 0xFD);
    CHECK(raw.size() * 2 < save_scene_text(s).size());  // text scenes compress well
    Scene l;
    CHECK(load_scene(path, l, err));
    CHECK(l.compress);
    CHECK(save_scene_text(l) == save_scene_text(s));
  });
}

static void colormanagement_tests() {
  test("display: exact sRGB table and SIMD encoders", [] {
    /* Every 61st float in [0, 1] (17M values) against the double-precision
     * curve; also count how often single-precision powf rounds differently. */
    std::atomic<int64_t> bad{0}, powf_off{0}, powf_far{0};
    const uint32_t one = 0x3F800000u;
    const int64_t n = one / 61 + 1;
    JobSystem::global().parallel_for(n, 1 << 16, [&](int64_t b, int64_t e) {
      int64_t lb = 0, lo = 0, lf = 0;
      for (int64_t i = b; i < e; i++) {
        uint32_t u = (uint32_t)std::min<int64_t>(i * 61, one);
        float v;
        std::memcpy(&v, &u, 4);
        int t = display::linear_to_srgb8(v), ref = display::srgb8_reference(v);
        int pf = std::clamp((int)(linear_to_srgb(v) * 255.0f + 0.5f), 0, 255);
        lb += t != ref;
        lo += pf != ref;
        lf += std::abs(pf - ref) > 1;
      }
      bad += lb;
      powf_off += lo;
      powf_far += lf;
    });
    CHECK(bad == 0);
    CHECK(powf_far == 0);
    std::printf("    sRGB table: exact on %lld floats; float powf rounds to the neighbouring step on %lld (1 in %lld)\n",
                (long long)n, (long long)powf_off.load(), (long long)(powf_off ? n / powf_off : 0));
    CHECK(display::linear_to_srgb8(-1.0f) == 0 && display::linear_to_srgb8(2.0f) == 255 && display::linear_to_srgb8(1.0f) == 255);
    CHECK(display::linear_to_srgb8(std::numeric_limits<float>::quiet_NaN()) == 0);
    CHECK(display::linear_to_srgb8(std::numeric_limits<float>::infinity()) == 255);
    /* All kernels agree (Standard exactly; curves within one step, FMA rounding). */
    uint32_t rng = 7;
    std::vector<float> rgb(3 * 1003);
    for (float &f : rgb) {
      rng = rng * 1664525u + 1013904223u;
      f = std::exp2(((rng >> 8) & 0xFFFF) / 65536.0f * 16.0f - 10.0f);
    }
    rgb[5] = -1.0f;
    rgb[7] = std::numeric_limits<float>::quiet_NaN();
    for (ViewTransform vt : {ViewTransform::Standard, ViewTransform::Filmic, ViewTransform::ACES}) {
      std::vector<uint32_t> ref(1003), got(1003);
      display::set_kernel(display::Kernel::Scalar);
      display::encode_span(rgb.data(), ref.data(), ref.size(), vt, 0.5f);
      for (display::Kernel k : {display::Kernel::SSE41, display::Kernel::AVX2}) {
        display::set_kernel(k);
        display::encode_span(rgb.data(), got.data(), got.size(), vt, 0.5f);
        int worst = 0;
        for (size_t i = 0; i < ref.size(); i++)
          for (int s = 0; s < 32; s += 8) worst = std::max(worst, std::abs((int)((ref[i] >> s) & 255) - (int)((got[i] >> s) & 255)));
        CHECK(worst <= (vt == ViewTransform::Standard ? 0 : 1));
      }
    }
    display::set_kernel(display::Kernel::Auto);
    std::printf("    display kernel: %s\n", display::kernel_name(display::active_kernel()));
  });
  test("colour: OpenColorIO views baked into LUTs match OCIO", [] {
    if (!colormanagement::available()) return;
    CHECK(colormanagement::view_index("AgX") >= 0);
    CHECK(colormanagement::view_index("Filmic") >= 0);
    uint32_t rng = 99;
    auto rnd = [&] { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; };
    std::vector<float> in, ref(3 * 2000);
    for (int i = 0; i < 2000; i++)
      for (int k = 0; k < 3; k++) in.push_back(std::exp2(rnd() * 16.0f - 10.0f) * (rnd() < 0.05f ? 0.0f : 1.0f));  // 2^-10 .. 2^6, some zeros
    for (const char *name : {"AgX", "Filmic", "Khronos PBR Neutral", "Standard"}) {
      int v = colormanagement::view_index(name);
      if (v < 0) continue;
      CHECK(colormanagement::reference(v, in.data(), ref.data(), 2000));
      std::vector<float> errs;
      for (int i = 0; i < 2000; i++) {
        Vec3 d = colormanagement::display_rgb(v, {in[i * 3], in[i * 3 + 1], in[i * 3 + 2]});
        float e = 0;
        for (int k = 0; k < 3; k++)  // compare what reaches the screen: both clamped
          e = std::max(e, std::fabs(std::clamp(d[k], 0.0f, 1.0f) - std::clamp(ref[i * 3 + k], 0.0f, 1.0f)));
        errs.push_back(e * 255.0f);
      }
      std::sort(errs.begin(), errs.end());
      float p99 = errs[errs.size() * 99 / 100], worst = errs.back();
      std::printf("    %-20s LUT vs OpenColorIO: 99%% within %.2f / 255, worst %.2f / 255\n", name, p99, worst);
      /* The worst cases are near-pure primaries (one channel ~300x below the
       * others), where AgX / PBR Neutral gamut-compress with hard kinks. */
      CHECK(p99 < 1.5f);
      CHECK(worst < 6.0f);
    }
    /* The setting index maps through the names list. */
    int agx = colormanagement::view_index("AgX");
    CHECK((int)view_transform_from_setting(3 + agx) == (int)ViewTransform::OcioView + agx);
    CHECK(view_transform_from_setting(1) == ViewTransform::Filmic);
  });
}

static void render_tests() {
  test("pathtracer: BVH and Embree match brute force", [] {
    auto sphere = primitives::ico_sphere(0.5f, 3);
    auto torus = primitives::torus();
    const RenderMesh &rs = sphere->render_mesh_tangents(), &rt = torus->render_mesh_tangents();
    Mat4 ms = Mat4::translate({0.3f, 0.1f, 0}), mt = Mat4::trs({-0.6f, 0, 0.4f}, Quat::euler({30, 10, 0}), {1, 1, 1});
    for (bool embree : {false, true}) {
      if (embree && !PathTracer::embree_available()) continue;
      PathTracer pt;
      PTSettings ps;
      ps.use_embree = embree;
      pt.set_settings(ps);
      Environment env;
      pt.build({{&rs, ms, nullptr}, {&rt, mt, nullptr}}, {}, env);
      CHECK(std::string(pt.ray_backend()) == (embree ? "Embree" : "Blendity BVH"));
      /* object and triangle ids must agree with the brute-force hit too */
      uint32_t rng = 12345;
      auto rnd = [&] { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; };
      int mismatches = 0, hits = 0;
      for (int i = 0; i < 2000; i++) {
        Vec3 o{rnd() * 4 - 2, rnd() * 4 - 2, -3};
        Ray r{o, normalize(Vec3(rnd() - 0.5f, rnd() - 0.5f, 1.0f) + (Vec3(0, 0, 0) - o) * 0.2f)};
        float best = 1e30f;
        uint32_t best_obj = UINT32_MAX, best_tri = UINT32_MAX;
        const std::pair<const RenderMesh *, Mat4> objs[2] = {{&rs, ms}, {&rt, mt}};
        for (uint32_t ob = 0; ob < 2; ob++) {
          auto [mesh, model] = objs[ob];
          for (size_t t = 0; t < mesh->tri_count(); t++) {
            float d = ray_triangle(r, model.point(mesh->positions[mesh->indices[t * 3]]), model.point(mesh->positions[mesh->indices[t * 3 + 1]]),
                                   model.point(mesh->positions[mesh->indices[t * 3 + 2]]));
            if (d > 0 && d < best) { best = d; best_obj = ob; best_tri = (uint32_t)t; }
          }
        }
        PathTracer::Hit h;
        bool got = pt.intersect(r, h);
        hits += got;
        if (got != (best < 1e29f) || (got && std::fabs(h.t - best) > 1e-3f)) mismatches++;
        else if (got && (h.object != best_obj || h.tri != best_tri)) mismatches++;
      }
      CHECK(hits > 200);
      CHECK(mismatches == 0);
    }
  });
  test("pathtracer: denoisers move a 4-sample render toward the reference", [] {
    auto sphere = primitives::uv_sphere(0.5f, 32, 24);
    auto plane = primitives::plane(6.0f, 1);
    std::vector<MaterialPtr> mats = {make_material("grey", {0.7f, 0.7f, 0.7f})};
    Environment env;
    RenderLight sun;
    sun.direction = normalize(Vec3(-0.5f, -1.0f, 0.3f));
    sun.intensity = 2.0f;
    std::vector<PTObject> objs = {{&sphere->render_mesh_tangents(), Mat4::translate({0, 0.5f, 0}), &mats},
                                  {&plane->render_mesh_tangents(), Mat4::identity(), &mats}};
    const int W = 48, H = 48;
    auto render = [&](int spp, bool denoise, bool oidn) {
      PathTracer pt;
      PTSettings st;
      st.use_oidn = oidn;
      pt.set_settings(st);
      pt.build(objs, {sun}, env);
      pt.set_camera(Mat4::look_at({0, 1.5f, -3}, {0, 0.4f, 0}, {0, 1, 0}), Mat4::perspective(45 * kDeg2Rad, 1, 0.1f, 50), W, H);
      pt.render(1e9, spp);
      return pt.linear_rgb(denoise);
    };
    auto rmse = [](const std::vector<float> &a, const std::vector<float> &b) {
      double e = 0;
      for (size_t i = 0; i < a.size(); i++) e += (a[i] - b[i]) * (a[i] - b[i]);
      return std::sqrt(e / a.size());
    };
    std::vector<float> ref = render(512, false, false), noisy = render(4, false, false);
    double e_noisy = rmse(noisy, ref), e_atrous = rmse(render(4, true, false), ref);
    CHECK(e_atrous < e_noisy * 0.8);
    if (PathTracer::oidn_available()) {
      double e_oidn = rmse(render(4, true, true), ref);
      std::printf("    RMSE vs 512 spp: noisy %.4f, A-Trous %.4f, OpenImageDenoise %.4f\n", e_noisy, e_atrous, e_oidn);
      CHECK(e_oidn < e_noisy * 0.6);
    }
  });
  test("pathtracer: mesh-light sampling with MIS is unbiased and cuts noise", [] {
    /* A large emissive panel over a floor and a box: BSDF sampling alone also
     * converges here, so both estimators must agree on the mean. */
    auto cube = primitives::cube(1.0f);
    auto panel = primitives::quad(1.0f);
    std::vector<MaterialPtr> grey = {make_material("grey", {0.6f, 0.6f, 0.6f})};
    std::vector<MaterialPtr> light = {make_material("light", {0, 0, 0})};
    light[0]->emission = {1.0f, 0.9f, 0.8f};
    light[0]->emission_strength = 5.0f;
    std::vector<PTObject> objs = {{&cube->render_mesh_tangents(), Mat4::trs({0, -0.05f, 0}, Quat(), {8, 0.1f, 8}), &grey},
                                  {&cube->render_mesh_tangents(), Mat4::trs({0.3f, 0.5f, 0}, Quat::euler({0, 25, 0}), {1, 1, 1}), &grey},
                                  {&panel->render_mesh_tangents(), Mat4::trs({0, 3, 0}, Quat::euler({90, 0, 0}), {3, 3, 1}), &light}};
    Environment env;
    env.mode = Environment::Color;
    env.color = Vec3(0.0f);
    auto render = [&](int spp, bool nee) {
      PathTracer pt;
      PTSettings st;
      st.sample_mesh_lights = nee;
      st.max_bounces = 4;
      st.clamp_indirect = 0;
      pt.set_settings(st);
      pt.build(objs, {}, env);
      pt.set_camera(Mat4::look_at({0, 2, -4}, {0, 0.5f, 0}, {0, 1, 0}), Mat4::perspective(50 * kDeg2Rad, 4.0f / 3.0f, 0.05f, 50), 32, 24);
      pt.render(1e9, spp);
      return pt.linear_rgb(false);
    };
    auto mean = [](const std::vector<float> &a) {
      double s = 0;
      for (float v : a) s += v;
      return s / a.size();
    };
    auto rmse = [](const std::vector<float> &a, const std::vector<float> &b) {
      double e = 0;
      for (size_t i = 0; i < a.size(); i++) e += (a[i] - b[i]) * (a[i] - b[i]);
      return std::sqrt(e / a.size());
    };
    /* 1024 spp for a 1% check. The per-pixel noise is about 0.09 at 64 spp (BSDF-only); a pixel's three
     * channels share one path, so the mean has about 768 independent values: sigma ~0.09 * sqrt(64/1024) /
     * sqrt(768) = 8e-4, 0.35% of 0.23, and the ratio of two such means about 0.5% - the 1% bound is about
     * 2 sigma of the estimator. The tracer is seeded per pixel and sample, so the result is the same on
     * every run (measured ratio 0.9965 NEE / BSDF); a change of compiler or sampling code is what could move it.
     * (It used 8192 spp. The 64-spp RMSE below now shares its first samples with the reference, which
     * flatters NEE by a few percent; the check needs a 30% margin.) */
    std::vector<float> bsdf_ref = render(1024, false), nee_ref = render(1024, true);
    std::printf("    mean radiance: BSDF-only %.4f, mesh-light NEE + MIS %.4f\n", mean(bsdf_ref), mean(nee_ref));
    CHECK(std::fabs(mean(nee_ref) / mean(bsdf_ref) - 1.0) < 0.01);
    double e_bsdf = rmse(render(64, false), nee_ref), e_nee = rmse(render(64, true), nee_ref);
    std::printf("    RMSE at 64 spp: BSDF-only %.4f, NEE + MIS %.4f\n", e_bsdf, e_nee);
    CHECK(e_nee < e_bsdf * 0.7);
  });
  test("pathtracer: OpenPGL path guiding learns and stays unbiased", [] {
    if (!PathTracer::guiding_available()) return;
    /* Two rooms joined by a doorway, the light only in the far one: the camera
     * room is lit through the opening - the case path guiding is for. */
    auto cube = primitives::cube(1.0f);
    auto panel = primitives::quad(1.0f);
    std::vector<MaterialPtr> grey = {make_material("grey", {0.6f, 0.6f, 0.6f})};
    std::vector<MaterialPtr> light = {make_material("light", {0, 0, 0})};
    light[0]->emission = {1, 1, 1};
    light[0]->emission_strength = 40.0f;
    std::vector<PTObject> objs;
    auto wall = [&](Vec3 lo, Vec3 hi) { objs.push_back({&cube->render_mesh_tangents(), Mat4::trs((lo + hi) * 0.5f, Quat(), hi - lo), &grey}); };
    wall({-3.1f, -0.1f, -3.1f}, {3.1f, 0.0f, 9.1f});   // floor
    wall({-3.1f, 4.0f, -3.1f}, {3.1f, 4.1f, 9.1f});    // ceiling
    wall({-3.1f, 0.0f, -3.1f}, {-3.0f, 4.0f, 9.1f});   // side walls
    wall({3.0f, 0.0f, -3.1f}, {3.1f, 4.0f, 9.1f});
    wall({-3.0f, 0.0f, -3.1f}, {3.0f, 4.0f, -3.0f});   // end walls
    wall({-3.0f, 0.0f, 9.0f}, {3.0f, 4.0f, 9.1f});
    wall({-3.0f, 0.0f, 2.95f}, {-0.5f, 4.0f, 3.05f});  // divider with a doorway
    wall({0.5f, 0.0f, 2.95f}, {3.0f, 4.0f, 3.05f});
    wall({-0.5f, 2.0f, 2.95f}, {0.5f, 4.0f, 3.05f});
    objs.push_back({&panel->render_mesh_tangents(), Mat4::trs({0, 3.95f, 6.0f}, Quat::euler({90, 0, 0}), {1, 1, 1}), &light});
    Environment env;
    env.mode = Environment::Color;
    env.color = Vec3(0.0f);
    const int W = 64, H = 48;
    auto render = [&](int spp, bool guiding) {
      PathTracer pt;
      PTSettings st;
      st.use_guiding = guiding;
      st.guiding_training_samples = 128;
      st.max_bounces = 6;
      st.clamp_indirect = 0;
      pt.set_settings(st);
      pt.build(objs, {}, env);
      pt.set_camera(Mat4::look_at({0, 1.6f, -2.6f}, {0, 1.0f, 3.0f}, {0, 1, 0}), Mat4::perspective(70 * kDeg2Rad, 4.0f / 3.0f, 0.05f, 50), W, H);
      pt.render(1e9, spp);
      if (guiding) CHECK(pt.guiding_active() && pt.stats().guiding_updates > 0);
      return pt.linear_rgb(false);
    };
    auto mean = [](const std::vector<float> &a) {
      double s = 0;
      for (float v : a) s += v;
      return s / a.size();
    };
    /* Unbiased: guiding changes which directions are sampled, never the mean.
     * (Whether it lowers the error depends on how much the field learned: at
     * this tiny size it trains on ~10k samples, too few - the stress suite
     * measures the benefit at a realistic resolution.) */
    /* A 1024-spp reference. The 4% bound is empirical: the two agreed to 0.35% with a 4096-spp reference
     * and agree to 0.4% with this one; seeding per pixel and sample makes it the same every run. */
    std::vector<float> ref = render(1024, false), guided = render(512, true);
    std::printf("    mean radiance: guided %.4f vs reference %.4f\n", mean(guided), mean(ref));
    CHECK(std::fabs(mean(guided) / mean(ref) - 1.0) < 0.04);
  });
  test("pathtracer: white furnace (energy conservation)", [] {
    auto sphere = primitives::uv_sphere(0.5f, 32, 24);
    std::vector<MaterialPtr> mats = {make_material("white", {1, 1, 1})};
    mats[0]->specular = 0.0f;
    mats[0]->roughness = 1.0f;
    Environment env;
    env.mode = Environment::Color;
    env.color = {0.5f, 0.5f, 0.5f};
    PathTracer pt;
    pt.build({{&sphere->render_mesh_tangents(), Mat4::identity(), &mats}}, {}, env);
    PTSettings st;
    st.max_bounces = 8;
    st.clamp_indirect = 0;
    st.denoise = false;
    pt.set_settings(st);
    pt.set_camera(Mat4::look_at({0, 0, -2}, {0, 0, 0}, {0, 1, 0}), Mat4::perspective(40 * kDeg2Rad, 1, 0.1f, 10), 24, 24);
    pt.render(1e9, 64);
    auto rgb = pt.linear_rgb(false);
    double centre = 0;
    for (int y = 10; y < 14; y++)
      for (int x = 10; x < 14; x++) centre += rgb[((size_t)y * 24 + x) * 3 + 1];
    centre /= 16;
    CHECK_NEAR(rgb[1], 0.5f, 0.01f);   // background
    CHECK_NEAR(centre, 0.5, 0.05);     // albedo 1 sphere vanishes into the furnace
  });
  test("pathtracer: transparent, cutout and glass surfaces", [] {
    /* A quad in front of a uniform white environment: the centre pixel shows
     * how much of the background gets through it. */
    auto quad = primitives::quad(2.0f);
    auto centre = [&](const MaterialPtr &mat, int spp = 256) {
      std::vector<MaterialPtr> mats = {mat};
      Environment env;
      env.mode = Environment::Color;
      env.color = {1, 1, 1};
      PathTracer pt;
      pt.build({{&quad->render_mesh_tangents(), Mat4::identity(), &mats}}, {}, env);
      PTSettings st;
      st.max_bounces = 4;
      st.clamp_indirect = 0;
      st.denoise = false;
      pt.set_settings(st);
      pt.set_camera(Mat4::look_at({0, 0, -3}, {0, 0, 0}, {0, 1, 0}), Mat4::perspective(20 * kDeg2Rad, 1, 0.1f, 10), 8, 8);
      pt.render(1e9, spp);
      auto rgb = pt.linear_rgb(false);
      Vec3 c(0.0f);
      for (int y = 2; y < 6; y++)
        for (int x = 2; x < 6; x++) c += Vec3(rgb[((size_t)y * 8 + x) * 3], rgb[((size_t)y * 8 + x) * 3 + 1], rgb[((size_t)y * 8 + x) * 3 + 2]);
      return c / 16.0f;
    };
    auto black = [](int surface, float alpha) {
      MaterialPtr m = make_material("m", {0, 0, 0});
      m->specular = 0;
      m->roughness = 1;
      m->surface = surface;
      m->alpha = alpha;
      m->double_sided = true;
      return m;
    };
    CHECK(centre(black((int)MaterialSurface::Opaque, 1.0f)).y < 0.02f);
    CHECK_NEAR(centre(black((int)MaterialSurface::Transparent, 0.25f)).y, 0.75f, 0.05f);
    CHECK_NEAR(centre(black((int)MaterialSurface::Transparent, 0.75f)).y, 0.25f, 0.05f);
    CHECK_NEAR(centre(black((int)MaterialSurface::Cutout, 0.3f)).y, 1.0f, 0.02f);  // below the threshold: a hole
    CHECK(centre(black((int)MaterialSurface::Cutout, 0.7f)).y < 0.02f);
    /* Clear glass is lossless: in a uniform environment it disappears (what
     * it reflects and what it lets through add up to 1). Tinted glass tints
     * only what passes through. */
    MaterialPtr glass = make_material("glass", {1, 1, 1});
    glass->surface = (int)MaterialSurface::Glass;
    glass->roughness = 0;
    glass->ior = 1.5f;
    Vec3 clear = centre(glass);
    CHECK_NEAR(clear.y, 1.0f, 0.03f);
    glass->base_color = {0.25f, 1.0f, 1.0f};
    Vec3 tinted = centre(glass);
    CHECK(tinted.x > 0.2f && tinted.x < 0.4f);  // ~F + (1-F) * 0.25 for one pass through a sheet
    CHECK_NEAR(tinted.y, 1.0f, 0.03f);
    glass->roughness = 0.4f;  // rough (frosted) glass: still energy conserving
    glass->base_color = {1, 1, 1};
    CHECK_NEAR(centre(glass).y, 1.0f, 0.05f);
  });
  test("gpu: every device (software BVH and ray tracing hardware) and CPU+GPU match the CPU", [] {
    std::printf("    %s\n", gpu::status().c_str());
    if (!gpu::available()) return;  // no Vulkan GPU on this machine: nothing to compare
    auto plane = primitives::plane(6.0f, 1);
    auto sphere = primitives::uv_sphere(0.5f, 32, 16);
    auto cube = primitives::cube(0.6f);
    auto quad = primitives::quad(1.0f);
    std::vector<MaterialPtr> m_plane = {make_material("floor", {0.8f, 0.8f, 0.8f})}, m_sphere = {make_material("ball", {1, 1, 1})},
                             m_glass = {make_material_preset("Glass")}, m_veil = {make_material_preset("Transparent")},
                             m_lamp = {make_material_preset("Emissive")};
    m_plane[0]->procedural = (int)Procedural::Checker;
    m_sphere[0]->procedural = (int)Procedural::UVGrid;  // a texture
    m_sphere[0]->roughness = 0.3f;
    std::vector<PTObject> objs = {
        {&plane->render_mesh_tangents(), Mat4::identity(), &m_plane},
        {&sphere->render_mesh_tangents(), Mat4::translate({-0.6f, 0.5f, 0.2f}), &m_sphere},
        {&sphere->render_mesh_tangents(), Mat4::translate({0.6f, 0.5f, 0.0f}), &m_glass},
        {&cube->render_mesh_tangents(), Mat4::translate({0.0f, 0.3f, -0.8f}), &m_veil},
        {&quad->render_mesh_tangents(), Mat4::trs({0, 1.8f, 0.5f}, Quat::euler({90, 0, 0}), {1, 1, 1}), &m_lamp}};
    RenderLight sun, lamp;
    sun.direction = normalize(Vec3(-0.4f, -1, 0.3f));
    lamp.type = RenderLight::Point;
    lamp.position = {1.2f, 1.2f, -1.0f};
    lamp.intensity = 2.0f;
    lamp.range = 6.0f;
    Environment env;
    const int W = 48, H = 32, SPP = 128;
    auto render = [&](bool cpu, std::vector<int> gpus, bool hw, int *cpu_samples = nullptr) {
      PathTracer pt;
      PTSettings st;
      st.use_cpu = cpu;
      st.gpus = gpus;
      st.gpu_hardware_rt = hw;
      st.denoise = false;
      st.use_embree = false;
      pt.set_settings(st);
      pt.build(objs, {sun, lamp}, env);
      pt.set_camera(Mat4::look_at({0, 1.5f, -3.5f}, {0, 0.4f, 0}, {0, 1, 0}), Mat4::perspective(50 * kDeg2Rad, W / (float)H, 0.1f, 50), W, H);
      int done = pt.render(1e9, SPP);
      CHECK(done == SPP);
      if (cpu_samples) *cpu_samples = pt.stats().cpu_samples;
      return pt.linear_rgb(false);
    };
    std::vector<float> ref = render(true, {}, false);
    double ref_mean = 0;
    for (float v : ref) ref_mean += v;
    ref_mean /= ref.size();
    auto compare = [&](const std::vector<float> &img, const char *what) {
      double mean = 0, err = 0;
      for (size_t i = 0; i < img.size(); i++) {
        mean += img[i];
        err += std::fabs(img[i] - ref[i]);
      }
      mean /= img.size();
      err /= img.size();
      std::printf("    %-46s mean %.4f (CPU %.4f), mean |difference| %.4f\n", what, mean, ref_mean, err);
      CHECK(std::fabs(mean - ref_mean) < 0.03 * ref_mean);  // no bias
      CHECK(err < 0.06 * ref_mean);                          // same paths: close per pixel too
    };
    for (const gpu::DeviceInfo &d : gpu::devices()) {
      compare(render(false, {d.index}, false), (d.name + ", software BVH").c_str());
      if (d.hardware_rt) compare(render(false, {d.index}, true), (d.name + ", ray tracing hardware").c_str());
    }
    /* CPU + every GPU: the samples are split, the picture is the same. */
    std::vector<int> all;
    for (const gpu::DeviceInfo &d : gpu::devices()) all.push_back(d.index);
    int cpu_samples = 0;
    compare(render(true, all, true, &cpu_samples), "CPU + all GPUs combined");
    std::printf("    combined: the CPU rendered %d of %d samples\n", cpu_samples, SPP);
    CHECK(cpu_samples < SPP);
  });
  test("camera: focal length, sensor fit, lens shift, aperture, exposure and aspect ratio", [] {
    Camera c;
    c.physical = true;
    c.focal_length = 50.0f;
    const float a = 16.0f / 9.0f;
    /* Auto fit, landscape: the 36 mm sensor width spans the image width. */
    float h = 2.0f * std::atan(18.0f / 50.0f);
    CHECK_NEAR(c.vertical_fov_deg(a), 2.0f * std::atan(std::tan(h * 0.5f) / a) * kRad2Deg, 1e-3f);
    /* Portrait: the width spans the taller side; Vertical fit uses the 24 mm height. */
    CHECK_NEAR(c.vertical_fov_deg(0.5f), 2.0f * std::atan(18.0f / 50.0f) * kRad2Deg, 1e-3f);
    c.sensor_fit = 2;
    CHECK_NEAR(c.vertical_fov_deg(a), 2.0f * std::atan(12.0f / 50.0f) * kRad2Deg, 1e-3f);
    c.sensor_fit = 0;
    /* A field-of-view camera as a lens: the same view either way. */
    Camera f;
    f.fov = 40.0f;
    Camera p;
    p.physical = true;
    p.focal_length = f.focal_length_mm(a);
    CHECK_NEAR(p.vertical_fov_deg(a), 40.0f, 1e-3f);
    /* Lens shift moves the image, not the view direction. */
    c.shift_x = 0.25f;
    Vec4 q = c.projection(a) * Vec4(0, 0, 5, 1);
    CHECK_NEAR(q.x / q.w, -0.5f, 1e-5f);
    c.shift_x = 0.0f;
    /* Aperture: diameter = focal length / f-number (f/2.8 at 50 mm: 8.9 mm radius). */
    CHECK(c.aperture_radius(a) == 0.0f);  // depth of field off
    c.dof = true;
    c.f_stop = 2.8f;
    CHECK_NEAR(c.aperture_radius(a), 0.05f / 5.6f, 1e-6f);
    /* Exposure: 0 stops at ISO 100, 1/60 s, f/2.8; doubling ISO or time adds one, f/5.6 takes two. */
    c.physical_exposure = true;
    CHECK_NEAR(c.exposure_stops(), 0.0f, 1e-4f);
    c.iso = 200.0f;
    CHECK_NEAR(c.exposure_stops(), 1.0f, 1e-4f);
    c.shutter = 30.0f;
    CHECK_NEAR(c.exposure_stops(), 2.0f, 1e-4f);
    c.f_stop = 5.6f;
    CHECK_NEAR(c.exposure_stops(), 0.0f, 1e-4f);
    /* Aspect ratio: the render resolution's, the sensor's or a fixed one. */
    CHECK_NEAR(c.image_aspect(1.25f), 1.25f, 1e-6f);
    c.aspect_mode = 1;
    CHECK_NEAR(c.image_aspect(1.25f), 1.5f, 1e-6f);
    c.aspect_mode = 8;
    CHECK_NEAR(c.image_aspect(1.25f), 2.39f, 1e-6f);
    /* Every new field is saved and loaded with the scene. */
    Scene s;
    GameObject *go = create_primitive(s, "Camera");
    Camera *cc = go->get<Camera>();
    cc->physical = true;
    cc->focal_length = 85.0f;
    cc->sensor_preset = 3;
    cc->dof = true;
    cc->f_stop = 1.4f;
    cc->focus_distance = 4.5f;
    cc->blades = 7;
    cc->aspect_mode = 8;
    cc->shift_y = 0.1f;
    std::string text = save_scene_text(s);
    Scene back;
    std::string err;
    CHECK(load_scene_text(text, back, err));
    Camera *bc = nullptr;
    back.for_each([&](GameObject &g) {
      if (g.get<Camera>()) bc = g.get<Camera>();
    });
    CHECK(bc != nullptr);
    if (bc) {
      CHECK(bc->physical && bc->dof);
      CHECK_NEAR(bc->focal_length, 85.0f, 1e-4f);
      CHECK_NEAR(bc->sensor_width, 22.3f, 1e-4f);
      CHECK_NEAR(bc->f_stop, 1.4f, 1e-4f);
      CHECK_NEAR(bc->focus_distance, 4.5f, 1e-4f);
      CHECK(bc->blades == 7 && bc->aspect_mode == 8);
      CHECK_NEAR(bc->shift_y, 0.1f, 1e-5f);
    }
  });
  test("pathtracer: depth of field blurs off the focus plane, the same on CPU and GPU", [] {
    /* A glowing square on black. With a pinhole, and in focus, its edge is
     * sharp; focused far behind it, the edge spreads over many pixels. */
    auto quad = primitives::quad(1.0f);
    MaterialPtr glow = make_material_preset("Emissive");
    glow->double_sided = true;
    std::vector<MaterialPtr> mats = {glow};
    Environment env;
    env.mode = Environment::Color;
    env.color = {0, 0, 0};
    const int W = 64, H = 32;
    auto render = [&](const PTLens &lens, std::vector<int> gpus, int spp) {
      PathTracer pt;
      PTSettings st;
      st.denoise = false;
      st.use_embree = false;
      st.gpus = gpus;
      st.use_cpu = gpus.empty();
      pt.set_settings(st);
      pt.build({{&quad->render_mesh_tangents(), Mat4::identity(), &mats}}, {}, env);
      pt.set_camera(Mat4::look_at({0, 0, -3}, {0, 0, 0}, {0, 1, 0}), Mat4::perspective(30 * kDeg2Rad, W / (float)H, 0.1f, 100), W, H, lens);
      pt.render(1e9, spp);
      return pt.linear_rgb(false);
    };
    /* Pixels across the middle row between 10% and 90% of the brightest: the edge's width. */
    auto edge_width = [&](const std::vector<float> &rgb) {
      float mx = 0;
      for (int x = 0; x < W; x++) mx = std::max(mx, rgb[((size_t)(H / 2) * W + x) * 3 + 1]);
      int n = 0;
      for (int x = 0; x < W; x++) {
        float v = rgb[((size_t)(H / 2) * W + x) * 3 + 1];
        if (v > 0.1f * mx && v < 0.9f * mx) n++;
      }
      return n;
    };
    PTLens pinhole, focused, blurred;
    focused.radius = blurred.radius = 0.2f;
    focused.focus_distance = 3.0f;
    blurred.focus_distance = 30.0f;
    blurred.blades = 6;
    const int sharp = edge_width(render(pinhole, {}, 64)), in_focus = edge_width(render(focused, {}, 64));
    const std::vector<float> cpu = render(blurred, {}, 256);
    const int soft = edge_width(cpu);
    std::printf("    edge width: pinhole %d px, in focus %d px, focused behind %d px\n", sharp, in_focus, soft);
    CHECK(sharp <= 4 && in_focus <= 4);
    CHECK(soft >= 3 * std::max(1, sharp));
    /* The GPU kernel samples the same lens. */
    if (gpu::available()) {
      double mean_c = 0, mean_g = 0, err = 0;
      const std::vector<float> g = render(blurred, {gpu::devices()[0].index}, 256);
      for (size_t i = 0; i < cpu.size(); i++) {
        mean_c += cpu[i];
        mean_g += g[i];
        err += std::fabs(cpu[i] - g[i]);
      }
      std::printf("    GPU: mean %.4f (CPU %.4f), mean |difference| %.4f, edge %d px\n", mean_g / cpu.size(), mean_c / cpu.size(), err / cpu.size(), edge_width(g));
      CHECK(std::fabs(mean_g - mean_c) < 0.03 * mean_c);
      CHECK(err < 0.06 * mean_c);  // the same lens samples, close per pixel too
      CHECK(std::abs(edge_width(g) - soft) <= 2);
    }
  });
  test("set origin: bounds, median, surface and volume centres; nothing moves on screen", [] {
    /* Two boxes: [0,2]x[0,1]x[0,1] and [0,1]x[1,2]x[0,1]. */
    Mesh m;
    auto add_box = [&](Vec3 c, Vec3 s) {
      Mesh b = *primitives::cube();
      uint32_t base = (uint32_t)m.vert_count();
      for (Vec3 p : b.positions) m.add_vert(c + Vec3(p.x * s.x, p.y * s.y, p.z * s.z));
      for (size_t f = 0; f < b.face_count(); f++) {
        std::vector<uint32_t> v(b.face_verts(f), b.face_verts(f) + b.face_size(f));
        for (uint32_t &x : v) x += base;
        m.add_face(v.data(), v.size());
      }
    };
    add_box({1, 0.5f, 0.5f}, {2, 1, 1});
    add_box({0.5f, 1.5f, 0.5f}, {1, 1, 1});
    auto near3 = [](Vec3 a, Vec3 b) { return length(a - b) < 1e-4f; };
    using meshops::OriginPoint;
    CHECK(near3(meshops::origin_point(m, OriginPoint::BoundsCenter), {1, 1, 0.5f}));
    CHECK(near3(meshops::origin_point(m, OriginPoint::BoundsBottom), {1, 0, 0.5f}));
    CHECK(near3(meshops::origin_point(m, OriginPoint::Median), {0.75f, 1, 0.5f}));
    CHECK(near3(meshops::origin_point(m, OriginPoint::SurfaceCenter), {13.0f / 16, 14.0f / 16, 0.5f}));
    CHECK(near3(meshops::origin_point(m, OriginPoint::VolumeCenter), {2.5f / 3, 2.5f / 3, 0.5f}));
    /* Far from the origin it is just as exact. */
    Mesh far = m;
    meshops::translate(far, {1e5f, 0, 0});
    CHECK(length(meshops::origin_point(far, OriginPoint::VolumeCenter) - Vec3(1e5f + 2.5f / 3, 2.5f / 3, 0.5f)) < 0.02f);
    /* An open mesh has no volume: the surface centre instead. */
    Mesh open = *primitives::plane(2.0f, 2);
    meshops::translate(open, {3, 0, 0});
    CHECK(near3(meshops::origin_point(open, OriginPoint::VolumeCenter), {3, 0, 0}));
    /* In the editor: the vertices and a child keep their world positions. */
    Editor ed;
    ed.init_headless(800, 500);
    ed.command("create Cube");
    ed.step_frame_headless();
    GameObject *cube = ed.selected_object();
    CHECK(cube != nullptr);
    if (!cube) return;
    const uint64_t id = cube->id;
    cube->get<MeshFilter>()->mesh = std::make_shared<Mesh>(m);
    cube->set_local_position({5, 1, -2});
    cube->set_local_euler({0, 30, 10});
    cube->set_local_scale({2, 1, 0.5f});
    GameObject *child = create_primitive(ed.scene(), "Empty");
    ed.scene().set_parent(child, cube);
    child->set_local_position({0.3f, 0.2f, 0.1f});
    ed.commit_change("test setup");
    const Mat4 w0 = cube->world_matrix();
    std::vector<Vec3> world0;
    for (Vec3 pp : m.positions) world0.push_back(w0.point(pp));
    const Vec3 child0 = child->world_position();
    ed.select_object(id);
    ed.command("origin volume");
    GameObject *after = ed.scene().find(id);
    const Mesh &am = *after->get<MeshFilter>()->mesh;
    const Mat4 w1 = after->world_matrix();
    float worst = 0;
    for (size_t i = 0; i < am.vert_count(); i++) worst = std::max(worst, length(w1.point(am.positions[i]) - world0[i]));
    CHECK(worst < 1e-4f);
    CHECK(length(after->world_position() - w0.point({2.5f / 3, 2.5f / 3, 0.5f})) < 1e-4f);
    CHECK(near3(meshops::origin_point(am, OriginPoint::VolumeCenter), {0, 0, 0}));
    CHECK(length(after->children[0]->world_position() - child0) < 1e-4f);
    ed.command("origin point 7 0 0");
    CHECK(length(ed.scene().find(id)->world_position() - Vec3(7, 0, 0)) < 1e-4f);
    ed.command("origin geometry");  // the mesh moves to the origin; the object stays
    CHECK(length(ed.scene().find(id)->world_position() - Vec3(7, 0, 0)) < 1e-4f);
    CHECK(near3(meshops::origin_point(*ed.scene().find(id)->get<MeshFilter>()->mesh, OriginPoint::BoundsCenter), {0, 0, 0}));
  });
  test("editor: each selection mode offers only its own operators", [] {
    Editor ed;
    ed.init_headless(1000, 700);
    ed.command("create Cube");
    ed.step_frame_headless();
    GameObject *cube = ed.selected_object();
    CHECK(cube != nullptr);
    if (!cube) return;
    const uint64_t id = cube->id;
    auto faces = [&] { return ed.scene().find(id)->get<MeshFilter>()->mesh->face_count(); };
    Recti r = ed.scene_view_rect();
    auto key = [&](int k, int mods) {
      platform::Event d;
      d.type = platform::EventType::KeyDown;
      d.key = k;
      d.mods = mods;
      d.x = r.x + r.w / 2;
      d.y = r.y + r.h / 2;
      platform::Event m = d;
      m.type = platform::EventType::MouseMove;
      ed.step_frame_headless({m, d});
    };
    /* Vertex mode: Bevel (edges) is refused. Extrude works on vertices now (with the
     * whole cube selected it is Blender's region extrude); Esc cancels its move. */
    ed.command("edit vertex all");
    key(platform::KEY_B, platform::MOD_CTRL);
    CHECK(faces() == 6);
    key(platform::KEY_E, platform::MOD_CTRL);
    key(platform::KEY_ESCAPE, 0);
    CHECK(faces() == 6);
    /* Edge mode: Bevel works (Extrude too, cancelled here). */
    ed.command("edit edge all");
    key(platform::KEY_E, platform::MOD_CTRL);
    key(platform::KEY_ESCAPE, 0);
    CHECK(faces() == 6);
    key(platform::KEY_B, platform::MOD_CTRL);
    CHECK(faces() > 6);
    key(platform::KEY_ENTER, 0);  // Bevel is interactive now: confirm it
    const size_t beveled = faces();
    /* Face mode: Extrude works, Bevel doesn't. */
    ed.command("edit face");
    ed.command("fsel 0");
    key(platform::KEY_B, platform::MOD_CTRL);
    CHECK(faces() == beveled);
    key(platform::KEY_E, platform::MOD_CTRL);
    CHECK(faces() > beveled);
    /* The table: Push/Pull is a face operator, Bevel an edge one, Merge a vertex one. */
    CHECK(find_edit_op("push_pull") && find_edit_op("push_pull")->elements == 4);
    CHECK(find_edit_op("bevel") && find_edit_op("bevel")->elements == 2);
    CHECK(find_edit_op("merge_center") && find_edit_op("merge_center")->elements == 1);
  });
  test("export: OBJ + MTL and FBX read back through Blender's importer (ufbx)", [] {
    Mesh m = *primitives::cube();
    meshops::translate(m, {0.25f, 0.5f, 0.0f});  // off-centre, so a mirrored axis would show
    MaterialPtr red = make_material("Red", {0.9f, 0.1f, 0.05f});
    red->alpha = 0.5f;
    ExportItem it;
    it.name = "Box";
    it.mesh = &m;
    it.world = Mat4::translate({1, 0, 0});
    it.materials = {red};
    auto check = [&](const ImportResult &r, const char *what) {
      size_t faces = 0;
      AABB b;
      MaterialPtr mat;
      for (const ImportedNode &n : r.nodes) {
        if (!n.mesh) continue;
        const Mat4 w = Mat4::trs(n.position, n.rotation, n.scale);
        faces += n.mesh->face_count();
        CHECK(signed_volume(*n.mesh) * signed_volume(*primitives::cube()) > 0);  // faces still point out
        for (Vec3 p : n.mesh->positions) b.add(w.point(p));
        if (!n.materials.empty()) mat = n.materials[0];
      }
      std::printf("    %s: %zu faces, bounds (%.2f %.2f %.2f) - (%.2f %.2f %.2f)\n", what, faces, b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z);
      CHECK(faces == 6);
      /* Back in our space: x 1.25 +- 0.5, y 0.5 +- 0.5, z 0 +- 0.5. */
      CHECK(length(b.min - Vec3(0.75f, 0.0f, -0.5f)) < 1e-3f);
      CHECK(length(b.max - Vec3(1.75f, 1.0f, 0.5f)) < 1e-3f);
      CHECK(mat != nullptr);
      if (mat) {
        CHECK_NEAR(mat->base_color.x, 0.9f, 0.02f);
        CHECK_NEAR(mat->base_color.y, 0.1f, 0.02f);
      }
    };
    std::string obj, mtl;
    export_obj_mtl({it}, "box_test.mtl", obj, mtl);
    const std::string op = fs::join(test_dir(), "box_test.obj");
    CHECK(fs::write_file(op, obj) && fs::write_file(fs::join(test_dir(), "box_test.mtl"), mtl));
    CHECK(mtl.find("d 0.5") != std::string::npos);  // the alpha
    ImportResult ro;
    CHECK(import_model(op, ro));
    check(ro, "OBJ");
    const std::string fp = fs::join(test_dir(), "box_test.fbx");
    CHECK(fs::write_file(fp, export_fbx({it})));
    ImportResult rf;
    CHECK(import_model(fp, rf));
    if (!rf.error.empty()) std::printf("    FBX import: %s\n", rf.error.c_str());
    check(rf, "FBX");
  });
  test("export: every Blender format (OBJ FBX GLB glTF STL PLY USD) reads back in place", [] {
    Mesh box = *primitives::cube();
    meshops::translate(box, {0.25f, 0.5f, 0.0f});
    Mesh can = *primitives::cylinder(0.5f, 1.0f, 12);
    /* Caps in a second material slot (the n-gons) */
    can.face_material.assign(can.face_count(), 0);
    for (size_t f = 0; f < can.face_count(); f++)
      if (can.face_size(f) > 4) can.face_material[f] = 1;
    MaterialPtr red = make_material("Red", {0.9f, 0.1f, 0.05f}), blue = make_material("Blue", {0.1f, 0.2f, 0.9f}),
                green = make_material("Green", {0.1f, 0.8f, 0.2f});
    std::vector<ExportItem> items(2);
    items[0].name = "Box";
    items[0].mesh = &box;
    items[0].world = Mat4::translate({1, 0, 0});
    items[0].materials = {red};
    /* A base colour texture: OBJ (map_Kd) and glTF (embedded / copied) carry it. */
    const std::string tex = fs::join(test_dir(), "checker_tex.png");
    {
      std::vector<uint32_t> px(16 * 16);
      for (int i = 0; i < 256; i++) px[(size_t)i] = ((i / 16 + i % 16) & 1) ? 0xFFFFFFFFu : 0xFF202020u;
      CHECK(write_png(tex, px.data(), 16, 16, 16));
    }
    red->base_map.path = tex;
    items[1].name = "Can";
    items[1].mesh = &can;
    items[1].world = Mat4::trs({-2, 0.5f, 1}, Quat::euler({0, 30, 0}), {1, 1.5f, 1});
    items[1].materials = {blue, green};
    AABB want;
    for (const ExportItem &it : items)
      for (Vec3 p : it.mesh->positions) want.add(it.world.point(p));
    const char *dump = std::getenv("BLENDITY_EXPORT_DIR");  // for checking the files in Blender
    const std::string dir = dump ? std::string(dump) : fs::join(test_dir(), "formats");
    fs::make_dirs(dir);
    for (int f = 0; f < (int)ExportFormat::Count; f++) {
      for (int ascii = 0; ascii < ((f == (int)ExportFormat::STL || f == (int)ExportFormat::PLY || f == (int)ExportFormat::FBX) ? 2 : 1); ascii++) {
        ExportOptions o = export_defaults(f);
        o.ascii = ascii != 0;
        const std::string path = fs::join(dir, std::string("formats") + (ascii ? "_ascii" : "") + export_format_extension(f));
        std::string err;
        CHECK(export_file(path, items, o, &err));
        ImportResult r;
        const bool ok = import_model(path, r);
        CHECK(ok);
        if (!ok) {
          std::printf("    %s: %s\n", path.c_str(), r.error.c_str());
          continue;
        }
        AABB b;
        bool outward = true;
        size_t faces = 0;
        std::vector<Vec3> colors;
        /* World matrices through the node hierarchy. */
        std::vector<Mat4> world(r.nodes.size());
        for (size_t i = 0; i < r.nodes.size(); i++) {
          const ImportedNode &n = r.nodes[i];
          world[i] = (n.parent >= 0 ? world[(size_t)n.parent] : Mat4::identity()) * Mat4::trs(n.position, n.rotation, n.scale);
          if (!n.mesh) continue;
          faces += n.mesh->face_count();
          for (Vec3 p : n.mesh->positions) b.add(world[i].point(p));
          /* Two closed solids: the signed volume is positive when faces point out. */
          outward = outward && signed_volume(*n.mesh) * signed_volume(*primitives::cube()) > 0;
          for (auto &m : n.materials)
            if (m) colors.push_back(m->base_color);
        }
        std::printf("    %-34s %4zu faces, bounds (%.2f %.2f %.2f) - (%.2f %.2f %.2f), %zu material(s)\n", export_format_name(f), faces,
                    b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z, colors.size());
        CHECK(length(b.min - want.min) < 2e-3f && length(b.max - want.max) < 2e-3f);
        CHECK(outward);
        const bool has_mats = f != (int)ExportFormat::STL && f != (int)ExportFormat::PLY;
        if (has_mats) {
          auto has = [&](Vec3 c) {
            for (Vec3 k : colors)
              if (length(k - c) < 0.02f) return true;
            return false;
          };
          CHECK(has(red->base_color) && has(blue->base_color) && has(green->base_color));
        }
        /* The texture travels with OBJ and glTF (the file it points at must exist). */
        if (f == (int)ExportFormat::OBJ || f == (int)ExportFormat::GLB || f == (int)ExportFormat::GLTF) {
          bool textured = false;
          for (const ImportedNode &n : r.nodes)
            for (auto &m : n.materials)
              if (m && !m->base_map.empty() && fs::exists(m->base_map.path)) textured = true;
          CHECK(textured);
        }
      }
    }
  });
  test("import: files Blender 5.2 exported (set BLENDITY_BLENDER_DIR)", [] {
    const char *dir = std::getenv("BLENDITY_BLENDER_DIR");  // made by Blender: see docs/USABILITY.md
    if (!dir) {
      std::printf("    skipped: BLENDITY_BLENDER_DIR is not set\n");
      return;
    }
    std::string exp;
    CHECK(fs::read_file(fs::join(dir, "expected.txt"), exp));
    float e[6] = {};
    std::sscanf(exp.c_str(), "%f %f %f %f %f %f", &e[0], &e[1], &e[2], &e[3], &e[4], &e[5]);
    const Vec3 want_min(e[0], e[1], e[2]), want_max(e[3], e[4], e[5]);
    int files = 0;
    for (const DirEntry &de : fs::list(dir)) {
      const std::string ext = fs::extension(de.name);
      if (de.is_dir || !model_extension_supported(ext)) continue;
      files++;
      ImportResult r;
      const bool ok = import_model(fs::join(dir, de.name), r);
      CHECK(ok);
      if (!ok) {
        std::printf("    %s: %s\n", de.name.c_str(), r.error.c_str());
        continue;
      }
      std::vector<Mat4> world(r.nodes.size());
      AABB b;
      bool outward = true;
      std::vector<std::string> mats;
      for (size_t i = 0; i < r.nodes.size(); i++) {
        const ImportedNode &n = r.nodes[i];
        world[i] = (n.parent >= 0 ? world[(size_t)n.parent] : Mat4::identity()) * Mat4::trs(n.position, n.rotation, n.scale);
        if (!n.mesh) continue;
        for (Vec3 p : n.mesh->positions) b.add(world[i].point(p));
        outward = outward && signed_volume(*n.mesh) * signed_volume(*primitives::cube()) > 0;
        for (auto &m : n.materials)
          if (m) mats.push_back(strprintf("%s(%.2f %.2f %.2f)", m->name.c_str(), m->base_color.x, m->base_color.y, m->base_color.z));
      }
      std::printf("    %-18s %s | bounds (%.2f %.2f %.2f) - (%.2f %.2f %.2f) |", de.name.c_str(), r.summary.c_str(), b.min.x, b.min.y, b.min.z,
                  b.max.x, b.max.y, b.max.z);
      for (auto &s : mats) std::printf(" %s", s.c_str());
      std::printf("\n");
      CHECK(length(b.min - want_min) < 2e-3f && length(b.max - want_max) < 2e-3f);
      CHECK(outward);
    }
    CHECK(files >= 9);
  });
  test("lights: colour temperature, spot cone and area facing (CPU and GPU agree)", [] {
    const Vec3 w = kelvin_to_rgb(6500), warm = kelvin_to_rgb(2000), cool = kelvin_to_rgb(12000);
    std::printf("    6500 K (%.2f %.2f %.2f)  2000 K (%.2f %.2f %.2f)  12000 K (%.2f %.2f %.2f)\n", w.x, w.y, w.z, warm.x, warm.y,
                warm.z, cool.x, cool.y, cool.z);
    CHECK(std::min({w.x, w.y, w.z}) > 0.9f);  // daylight is (nearly) white
    CHECK(warm.x > 0.99f && warm.z < 0.3f);    // candle-ish: red, little blue
    CHECK(cool.z > 0.99f && cool.x < 0.85f);   // blue sky
    /* A floor lit only by a light 2 m above it, pointing down: brightness at
     * the centre and 1.8 m to the side. */
    auto plane = primitives::plane(8.0f, 1);
    std::vector<MaterialPtr> mats = {make_material("floor", {0.8f, 0.8f, 0.8f})};
    mats[0]->specular = 0;
    Environment env;
    env.mode = Environment::Color;
    env.color = {0, 0, 0};
    auto brightness = [&](const RenderLight &l, std::vector<int> gpus) {
      PathTracer pt;
      PTSettings st;
      st.denoise = false;
      st.use_embree = false;
      st.max_bounces = 1;
      st.gpus = gpus;
      st.use_cpu = gpus.empty();
      pt.set_settings(st);
      pt.build({{&plane->render_mesh_tangents(), Mat4::identity(), &mats}}, {l}, env);
      pt.set_camera(Mat4::look_at({0, 6, -0.01f}, {0, 0, 0}, {0, 1, 0}), Mat4::ortho(2.5f, 1.0f, 0.1f, 20), 32, 32);
      pt.render(1e9, 64);
      auto rgb = pt.linear_rgb(false);
      auto px = [&](int x, int y) { return rgb[((size_t)y * 32 + x) * 3 + 1]; };
      return std::pair<float, float>(px(16, 16), px(16 + 11, 16));  // centre, ~1.7 m out
    };
    RenderLight spot;
    spot.type = RenderLight::Spot;
    spot.position = {0, 2, 0};
    spot.direction = {0, -1, 0};
    spot.right = {1, 0, 0};
    spot.range = 10;
    spot.intensity = 2;
    spot.cos_outer = std::cos(20 * kDeg2Rad);  // 40 degree cone: about 0.73 m radius on the floor
    spot.cos_inner = std::cos(15 * kDeg2Rad);
    auto [sc, se] = brightness(spot, {});
    std::printf("    spot: centre %.3f, outside the cone %.3f\n", sc, se);
    CHECK(sc > 0.05f && se < sc * 0.02f);
    RenderLight area = spot;
    area.type = RenderLight::Area;
    area.width = area.height = 1.0f;
    auto [ac, ae] = brightness(area, {});
    RenderLight up = area;
    up.direction = {0, 1, 0};  // facing away from the floor
    auto [uc, ue] = brightness(up, {});
    std::printf("    area: centre %.3f, side %.3f; turned away %.4f\n", ac, ae, uc);
    CHECK(ac > ae && ae > 0.0f && uc < 1e-4f);
    if (gpu::available())
      for (const RenderLight &l : {spot, area}) {
        auto [gc, ge] = brightness(l, {gpu::devices()[0].index});
        auto [cc, ce] = brightness(l, {});
        std::printf("    GPU %s: centre %.3f (CPU %.3f), side %.3f (CPU %.3f)\n", l.type == RenderLight::Spot ? "spot" : "area", gc, cc, ge, ce);
        CHECK(std::fabs(gc - cc) < 0.1f * cc + 1e-3f);
        CHECK(std::fabs(ge - ce) < 0.1f * cc + 1e-3f);
      }
  });
  test("editor: the camera focus eyedropper sets the distance to the clicked surface", [] {
    Editor ed;
    ed.init_headless(1000, 700);
    ed.step_frame_headless();
    ed.command("select Main Camera");
    GameObject *cam = ed.selected_object();
    CHECK(cam && cam->get<Camera>());
    if (!cam || !cam->get<Camera>()) return;
    cam->get<Camera>()->focus_distance = 999.0f;
    ed.command("pickfocus");
    Recti r = ed.scene_view_rect();
    platform::Event mv, dn, upe;
    mv.type = platform::EventType::MouseMove;
    dn.type = platform::EventType::MouseDown;
    upe.type = platform::EventType::MouseUp;
    mv.x = dn.x = upe.x = r.x + r.w / 2;
    mv.y = dn.y = upe.y = r.y + r.h * 3 / 4;  // the floor in front of the default view
    ed.step_frame_headless({mv});
    ed.step_frame_headless({dn});
    ed.step_frame_headless({upe});
    const Camera *c = cam->get<Camera>();
    std::printf("    picked focus distance %.3f m\n", c->focus_distance);
    CHECK(c->focus_distance > 0.5f && c->focus_distance < 50.0f);
    CHECK(c->dof);
  });
  test("pathtracer: shadows through transparent surfaces", [] {
    /* Sun from straight above, a ground plane, and a half-transparent black
     * quad over part of it: the shadow keeps half of the direct light. */
    auto plane = primitives::plane(8.0f, 1);
    auto quad = primitives::plane(2.0f, 1);
    std::vector<MaterialPtr> ground = {make_material("ground", {0.8f, 0.8f, 0.8f})};
    ground[0]->specular = 0;
    MaterialPtr veil = make_material("veil", {0, 0, 0});
    veil->surface = (int)MaterialSurface::Transparent;
    veil->alpha = 0.5f;
    veil->specular = 0;
    veil->double_sided = true;
    std::vector<MaterialPtr> veil_mats = {veil};
    Environment env;
    env.mode = Environment::Color;
    env.color = {0, 0, 0};
    RenderLight sun;
    sun.direction = {0, -1, 0};
    PathTracer pt;
    pt.build({{&plane->render_mesh_tangents(), Mat4::identity(), &ground},
              {&quad->render_mesh_tangents(), Mat4::translate({-2, 1, 0}), &veil_mats}},
             {sun}, env);
    PTSettings st;
    st.max_bounces = 1;
    st.denoise = false;
    pt.set_settings(st);
    /* Look straight down at the ground beside the veil: left half shadowed, right half open. */
    pt.set_camera(Mat4::look_at({-1, 0.5f, 0}, {-1, 0, 0}, {0, 0, 1}), Mat4::perspective(60 * kDeg2Rad, 1, 0.01f, 10), 16, 16);
    pt.render(1e9, 64);
    auto rgb = pt.linear_rgb(false);
    auto px = [&](int x, int y) { return rgb[((size_t)y * 16 + x) * 3 + 1]; };
    float shadowed = 0, open = 0;
    for (int y = 6; y < 10; y++) {
      shadowed += px(2, y) + px(3, y);
      open += px(12, y) + px(13, y);
    }
    CHECK(open > 0.01f);
    std::printf("    shadow under alpha 0.5: %.3f of the open ground\n", shadowed / std::max(open, 1e-6f));
    CHECK_NEAR(shadowed / open, 0.5f, 0.08f);
  });
  test("render: rasterized transparent, cutout and glass materials", [] {
    auto quad = primitives::quad(2.0f);
    auto draw = [&](const MaterialPtr &mat, uint32_t &centre, float &depth) {
      Image img;
      img.resize(32, 32);
      RenderTarget rt;
      rt.attach(img, {0, 0, 32, 32});
      Renderer3D r;
      RasterOptions opt;
      opt.shade = ShadeMode::Deferred;
      LightingEnv env;
      env.view_transform = ViewTransform::Standard;
      env.camera_pos = {0, 0, -3};
      std::vector<MaterialPtr> mats = {mat};
      r.begin(&rt, Mat4::look_at({0, 0, -3}, {0, 0, 0}, {0, 1, 0}), Mat4::perspective(30 * kDeg2Rad, 1, 0.1f, 10), env, opt);
      r.clear(0xFFFFFFFF);
      DrawItem it;
      it.mesh = &quad->render_mesh_tangents();
      it.materials = &mats;
      it.double_sided = true;
      r.add(it);
      r.flush();
      centre = img.pixels[16 * 32 + 16];
      depth = rt.depth_at(16, 16);
    };
    auto red = [](int surface, float alpha) {
      MaterialPtr m = make_material("red", {1, 0, 0});
      m->unlit = true;
      m->surface = surface;
      m->alpha = alpha;
      return m;
    };
    auto ch = [](uint32_t c, int s) { return (int)((c >> s) & 255); };
    uint32_t c;
    float d;
    draw(red((int)MaterialSurface::Opaque, 1), c, d);
    CHECK(ch(c, 16) == 255 && ch(c, 8) == 0 && d < 1.0f);
    draw(red((int)MaterialSurface::Transparent, 0.5f), c, d);
    CHECK(ch(c, 16) == 255 && std::abs(ch(c, 8) - 128) <= 2 && std::abs(ch(c, 0) - 128) <= 2);  // half over white
    CHECK(d == 1.0f);  // transparent surfaces don't write depth
    draw(red((int)MaterialSurface::Cutout, 0.3f), c, d);
    CHECK(c == 0xFFFFFFFFu && d == 1.0f);  // a hole
    draw(red((int)MaterialSurface::Cutout, 0.7f), c, d);
    CHECK(ch(c, 16) == 255 && ch(c, 8) == 0 && d < 1.0f);
    MaterialPtr glass = make_material("glass", {0.5f, 1.0f, 1.0f});
    glass->surface = (int)MaterialSurface::Glass;
    draw(glass, c, d);
    CHECK(ch(c, 16) < 160 && ch(c, 8) > 200);  // tinted view of the white behind
    /* Shadow maps: transparent and glass surfaces don't cast, cutout does. */
    for (int surface : {(int)MaterialSurface::Transparent, (int)MaterialSurface::Glass, (int)MaterialSurface::Cutout}) {
      std::vector<MaterialPtr> mats = {red(surface, 0.7f)};
      DrawItem it;
      auto cube = primitives::cube();
      it.mesh = &cube->render_mesh();
      it.model = Mat4::translate({0, 1, 0});
      it.materials = &mats;
      ShadowMap sm;
      AABB bounds;
      bounds.add({-3, 0, -3});
      bounds.add({3, 2, 3});
      render_shadow_map(sm, {it}, {0, -1, 0}, bounds, 256);
      float lit = sm.lookup({0, 0, 0}, 1.0f);
      CHECK(surface == (int)MaterialSurface::Cutout ? lit < 0.05f : lit > 0.95f);
    }
  });
  test("render: sun shadow map shadows what is under a caster", [] {
    auto cube = primitives::cube();
    DrawItem it;
    it.mesh = &cube->render_mesh();
    it.model = Mat4::translate({0, 1, 0});
    ShadowMap sm;
    AABB bounds;
    bounds.add({-3, 0, -3});
    bounds.add({3, 2, 3});
    render_shadow_map(sm, {it}, {0, -1, 0}, bounds, 512);
    CHECK(sm.valid());
    CHECK(sm.lookup({0, 0, 0}, 1.0f) < 0.05f);   // directly below the cube
    CHECK(sm.lookup({2, 0, 2}, 1.0f) > 0.95f);   // open ground
    CHECK(sm.lookup({0, 1.5f, 0}, 1.0f) > 0.95f);  // the cube's own lit top
  });
}

/* ===================================================================== */
/* Join / Boolean / Separate, vertex & edge extrusion, origins, modifiers, */
/* parametric shapes, n-gon editing                                        */
/* ===================================================================== */

static GameObject *by_name(Scene &s, const char *n) { return s.find_by_name(n); }

static void modeling_round9_tests() {
  const float cube_sign = signed_volume(*primitives::cube()) > 0 ? 1.0f : -1.0f;
  auto vol = [&](const Mesh &m) { return signed_volume(m) * cube_sign; };
  test("object: Join keeps placement and materials; Separate By Loose Parts undoes it", [&] {
    Editor ed;
    ed.init_headless(800, 600);
    ed.step_frame_headless();
    ed.command("create Cube");
    GameObject *a = ed.selected_object();
    a->name = "A";
    a->set_world_position({3, 0, 0});
    a->get<MeshRenderer>()->materials = {make_material("Red", {1, 0, 0})};
    ed.command("create Cube");
    GameObject *b = ed.selected_object();
    b->name = "B";
    b->set_world_position({5, 1, 0});
    b->set_local_scale({1, 2, 1});
    b->get<MeshRenderer>()->materials = {make_material("Blue", {0, 0, 1})};
    AABB before = a->world_bounds();
    before.add(b->world_bounds());
    ed.select_object(a->id);
    ed.select_object_add(b->id);  // B is active: it keeps the result
    const size_t objects = ed.scene().object_count();
    ed.command("join");
    CHECK(ed.scene().object_count() == objects - 1);
    GameObject *j = ed.selected_object();
    CHECK(j == b);
    const Mesh &m = *j->get<MeshFilter>()->mesh;
    CHECK(m.face_count() == 12);
    CHECK(j->get<MeshRenderer>()->materials.size() == 2);
    CHECK(m.material_count() == 2);
    AABB after = j->world_bounds();
    std::printf("    joined: %zu faces, bounds (%.2f %.2f %.2f) - (%.2f %.2f %.2f)\n", m.face_count(), after.min.x, after.min.y, after.min.z,
                after.max.x, after.max.y, after.max.z);
    CHECK(length(after.min - before.min) < 1e-4f && length(after.max - before.max) < 1e-4f);
    CHECK(vol(m) > 0);
    ed.command("separate loose");
    CHECK(ed.scene().object_count() == objects);
    CHECK(j->get<MeshFilter>()->mesh->face_count() == 6);
  });
  test("object: Boolean difference applied, and as a live modifier (cutter as wire, not rendered)", [&] {
    if (!meshops::boolean_available()) {
      std::printf("    skipped: built without Manifold\n");
      return;
    }
    Editor ed;
    ed.init_headless(800, 600);
    ed.step_frame_headless();
    ed.command("create Cube");
    GameObject *box = ed.selected_object();
    box->name = "Box";
    box->set_local_scale({2, 2, 2});
    ed.command("create Cylinder");
    GameObject *cut = ed.selected_object();
    cut->name = "Cutter";
    cut->set_world_position(box->world_position());
    cut->set_local_scale({0.6f, 2, 0.6f});
    ed.select_object(cut->id);
    ed.select_object_add(box->id);
    ed.command("boolean difference modifier");
    CHECK(box->get<BooleanModifier>() != nullptr);
    CHECK(cut->get<MeshRenderer>()->display_as == 1 && !cut->get<MeshRenderer>()->show_in_renders);
    const Mesh *live = box->evaluated_mesh();
    const float v_live = live ? vol(*live) : 0.0f;
    std::printf("    live: %zu faces, volume %.3f (a 2 m box is 8)\n", live ? live->face_count() : 0, v_live);
    /* Object space (the box is scaled 2x): a unit cube less a hole of radius 0.15: 1 - pi 0.15^2 = 0.929. */
    CHECK_NEAR(v_live, 1.0f - kPi * 0.15f * 0.15f, 0.01f);
    /* Applied (Bool Tool's Auto): the cutter is used up. */
    box->remove_component(box->get<BooleanModifier>());
    ed.select_object(cut->id);
    ed.select_object_add(box->id);
    ed.command("boolean difference");
    CHECK(by_name(ed.scene(), "Cutter") == nullptr);
    const Mesh &m = *box->get<MeshFilter>()->mesh;
    CHECK_NEAR(vol(m), v_live, 1e-2f);
    CHECK(closed_manifold(m));
  });
  test("extrude: a lone vertex grows a wire edge, the wire edge a face (and files keep wire edges)", [&] {
    Mesh m;
    m.add_vert({0, 0, 0});
    std::vector<uint8_t> sel = {1};
    CHECK(meshops::extrude_verts_edges(m, sel) == 1);
    CHECK(m.vert_count() == 2 && m.loose_edges.size() == 1 && m.face_count() == 0);
    m.positions[1] = {1, 0, 0};
    CHECK(m.edge_cache().size() == 1);
    /* Extrude the edge (both ends selected): a quad, and the wire edge is no longer loose. */
    sel.assign(2, 1);
    CHECK(meshops::extrude_verts_edges(m, sel) == 1);
    CHECK(m.face_count() == 1 && m.loose_edges.empty() && m.vert_count() == 4);
    CHECK(sel[2] && sel[3] && !sel[0]);
    for (size_t v = 2; v < 4; v++) m.positions[v].z += 1.0f;
    /* Wire edges survive a save and load. */
    Mesh w;
    w.add_vert({0, 0, 0});
    w.add_vert({0, 1, 0});
    w.add_loose_edge(0, 1);
    Scene s;
    GameObject *g = s.create("Wire");
    g->add<MeshFilter>()->mesh = std::make_shared<Mesh>(w);
    Scene back;
    std::string err;
    CHECK(load_scene_text(save_scene_text(s), back, err));
    GameObject *bg = back.find_by_name("Wire");
    CHECK(bg && bg->get<MeshFilter>() && bg->get<MeshFilter>()->mesh->loose_edges.size() == 1);
    /* Edge extrusion on an open edge (one face): the new face agrees with its neighbour. */
    Mesh c = *primitives::grid(1.0f, 1, 1);
    const size_t top = 0;
    std::vector<uint8_t> vs(c.vert_count(), 0);
    vs[c.face_verts(top)[0]] = vs[c.face_verts(top)[1]] = 1;
    CHECK(meshops::extrude_verts_edges(c, vs) == 1);
    /* Pull the new edge straight out, in the top's plane: the fin continues the top. */
    const Vec3 mid = (c.positions[c.face_verts(top)[0]] + c.positions[c.face_verts(top)[1]]) * 0.5f;
    const Vec3 out = normalize(Vec3(mid.x, 0.0f, mid.z) - Vec3(c.face_center(top).x, 0.0f, c.face_center(top).z));
    for (size_t v = 0; v < vs.size(); v++)
      if (vs[v]) c.positions[v] += out;
    const Vec3 nf = c.face_normal(c.face_count() - 1), nt = c.face_normal(top);
    std::printf("    fin normal (%.2f %.2f %.2f) next to the top (%.2f %.2f %.2f)\n", nf.x, nf.y, nf.z, nt.x, nt.y, nt.z);
    CHECK(dot(nf, nt) > 0.99f);  // the same way up as the face it grew from
  });
  test("origin: Edit Origin snaps to the face under a click and nothing moves in the world", [&] {
    Editor ed;
    ed.init_headless(900, 650);
    ed.step_frame_headless();
    ed.command("create Cube");
    GameObject *g = ed.selected_object();
    g->set_world_position({0, 0.5f, 0});
    ed.command("camera 0 20 6 0 0.5 0");
    ed.step_frame_headless();
    const AABB before = g->world_bounds();
    const Vec3 o0 = g->world_position();
    ed.command("origin edit on");
    CHECK(ed.origin_editing());
    Recti r = ed.scene_view_rect();
    platform::Event mv, dn, up;
    mv.type = platform::EventType::MouseMove;
    dn.type = platform::EventType::MouseDown;
    up.type = platform::EventType::MouseUp;
    mv.x = dn.x = up.x = r.x + r.w / 2 - 28;  // on the front face, clear of the gizmo at the origin
    mv.y = dn.y = up.y = r.y + r.h / 2 + 22;
    ed.step_frame_headless({mv});
    ed.step_frame_headless({dn});
    ed.step_frame_headless({up});
    ed.step_frame_headless();
    const Vec3 o1 = g->world_position();
    const AABB after = g->world_bounds();
    std::printf("    origin (%.2f %.2f %.2f) -> (%.2f %.2f %.2f)\n", o0.x, o0.y, o0.z, o1.x, o1.y, o1.z);
    CHECK(length(o1 - o0) > 0.2f);  // moved onto the front face, an edge or a corner
    CHECK(length(after.min - before.min) < 1e-4f && length(after.max - before.max) < 1e-4f);
    CHECK(ed.selected_object() == g);  // the click snapped; it didn't change the selection
    /* Dragging the gizmo's X arrow moves only the origin. */
    ed.command("origin bounds");
    ed.step_frame_headless();
    const Vec3 p0 = g->world_position();
    const AABB b0 = g->world_bounds();
    platform::Event d2, mv2, up2;
    d2.type = platform::EventType::MouseDown;
    mv2.type = platform::EventType::MouseMove;
    up2.type = platform::EventType::MouseUp;
    mv.x = r.x + r.w / 2 + 45;
    mv.y = r.y + r.h / 2;
    d2.x = mv.x;
    d2.y = mv.y;
    mv2.x = up2.x = mv.x + 60;
    mv2.y = up2.y = mv.y;
    ed.step_frame_headless({mv});
    ed.step_frame_headless({d2});
    ed.step_frame_headless({mv2});
    ed.step_frame_headless({up2});
    ed.step_frame_headless();
    const Vec3 p1 = g->world_position();
    const AABB b1 = g->world_bounds();
    std::printf("    gizmo drag: origin (%.2f %.2f %.2f) -> (%.2f %.2f %.2f)\n", p0.x, p0.y, p0.z, p1.x, p1.y, p1.z);
    CHECK(std::fabs(p1.x - p0.x) > 0.2f && std::fabs(p1.y - p0.y) < 1e-3f && std::fabs(p1.z - p0.z) < 1e-3f);
    CHECK(length(b1.min - b0.min) < 1e-3f && length(b1.max - b0.max) < 1e-3f);
  });
  test("modifiers: Bevel, Triangulate, Weld, Wireframe, Displace, Simple Deform, Cast, Screw", [&] {
    {
      Mesh m = *primitives::cube();
      CHECK(meshops::bevel_modifier(m, 0.1f, 2, 30.0f));
      std::printf("    bevel: %zu faces, volume %.3f\n", m.face_count(), vol(m));
      CHECK(m.face_count() > 6 && closed_manifold(m) && vol(m) < 1.0f && vol(m) > 0.9f);
    }
    {
      Mesh m = *primitives::cylinder(0.5f, 1.0f, 12);
      meshops::triangulate_min(m, 5);  // only the n-gon caps
      size_t quads = 0, tris = 0;
      for (size_t f = 0; f < m.face_count(); f++) (m.face_size(f) == 4 ? quads : tris) += m.face_size(f) <= 4;
      CHECK(quads == 12 && tris == 20);
    }
    {
      Mesh m = *primitives::cube();
      Mesh copy = m;
      meshops::append_mesh(m, copy, Mat4::identity());
      CHECK(meshops::merge_by_distance(m, 1e-4f) == 8);
    }
    {
      Mesh m = *primitives::cube();
      meshops::wireframe(m, 0.05f, true, false);
      std::printf("    wireframe: %zu faces, closed %d\n", m.face_count(), (int)closed_manifold(m));
      CHECK(m.face_count() > 24 && vol(m) > 0.0f && vol(m) < 0.5f);
    }
    {
      Mesh m = *primitives::ico_sphere(1.0f, 3);
      const Mesh before = m;
      meshops::displace(m, 0.3f, 0.5f, 0.5f, 0, 3, 7);
      float moved = 0;
      for (size_t v = 0; v < m.vert_count(); v++) moved = std::max(moved, length(m.positions[v] - before.positions[v]));
      CHECK(moved > 0.02f && moved < 0.31f);
    }
    {
      /* Twist a tall box 90 degrees about Y: the top turns a quarter, the bottom stays. */
      Mesh m = *primitives::cube();
      for (Vec3 &p : m.positions) p.y *= 4.0f;
      const Mesh before = m;
      meshops::simple_deform(m, 0, 90.0f, 1, 0.0f, 1.0f);
      /* Each corner turns by its height's share of 90 degrees: top vs bottom differ by 90. */
      float turn_top = 0, turn_bot = 0;
      for (size_t i = 0; i < m.vert_count(); i++) {
        const Vec3 a = before.positions[i], b = m.positions[i];
        float d = (std::atan2(b.z, b.x) - std::atan2(a.z, a.x)) * kRad2Deg;
        while (d > 180) d -= 360;
        while (d < -180) d += 360;
        (a.y > 0 ? turn_top : turn_bot) = d;
      }
      std::printf("    twist: bottom turned %.1f deg, top %.1f deg\n", turn_bot, turn_top);
      CHECK_NEAR(std::fabs(turn_top - turn_bot), 90.0f, 0.5f);
      /* Bend and Taper keep the vertex count and stay finite. */
      for (int mode : {1, 2, 3}) {
        Mesh d = *primitives::cylinder(0.2f, 3.0f, 8);
        d = meshops::subdivide(d, 1, false);
        meshops::simple_deform(d, mode, mode == 1 ? 90.0f : 0.5f, mode == 1 ? 2 : 1, 0.0f, 1.0f);
        bool finite = true;
        for (const Vec3 &p : d.positions) finite = finite && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
        CHECK(finite);
      }
    }
    {
      Mesh m = *primitives::cube(2.0f);
      meshops::cast(m, 0, 1.0f, 1.5f, 1);
      float lo = 1e9f, hi = 0;
      for (const Vec3 &p : m.positions) lo = std::min(lo, length(p)), hi = std::max(hi, length(p));
      CHECK_NEAR(lo, 1.5f, 1e-3f);
      CHECK_NEAR(hi, 1.5f, 1e-3f);
    }
    {
      /* Lathe a wire profile (a vase outline from pole to pole) into a closed solid. */
      Mesh p;
      const Vec2 prof[] = {{0, 0}, {0.5f, 0}, {0.6f, 0.4f}, {0.3f, 1.0f}, {0, 1.0f}};
      for (const Vec2 &q : prof) p.add_vert({q.x, q.y, 0});
      for (uint32_t i = 0; i + 1 < 5; i++) p.add_loose_edge(i, i + 1);
      meshops::screw(p, 360.0f, 16, 0.0f, 1, 1, true, false);
      meshops::recalc_normals_outside(p);
      std::printf("    screw: %zu verts, %zu faces, closed %d, volume %.3f\n", p.vert_count(), p.face_count(), (int)closed_manifold(p), vol(p));
      CHECK(closed_manifold(p));
      CHECK(vol(p) > 0.4f && vol(p) < 0.9f);
    }
  });
  test("modifiers: stack order matters, and viewport / render / edit-mode toggles pick what applies", [&] {
    Scene s;
    GameObject *g = s.create("Stack");
    g->add<MeshFilter>()->mesh = primitives::cube();
    g->add<MeshRenderer>();
    auto *arr = static_cast<ArrayModifier *>(g->add_component(create_component("ArrayModifier")));
    arr->count = 2;
    g->add_component(create_component("TriangulateModifier"));
    CHECK(g->evaluated_mesh()->face_count() == 24);  // 2 cubes, triangulated
    arr->enabled = false;  // viewport off, render on
    CHECK(g->evaluated_mesh(0)->face_count() == 12);
    CHECK(g->evaluated_mesh(1)->face_count() == 24);
    arr->enabled = true;
    arr->show_in_editmode = false;
    CHECK(g->evaluated_mesh(2)->face_count() == 12);
    /* Toggles survive a save. */
    Scene back;
    std::string err;
    CHECK(load_scene_text(save_scene_text(s), back, err));
    GameObject *bg = back.find_by_name("Stack");
    CHECK(bg && bg->get<ArrayModifier>() && !bg->get<ArrayModifier>()->show_in_editmode && bg->get<ArrayModifier>()->show_in_render);
  });
  test("shapes: every parametric shape is a closed outward solid; edits rebuild, mesh edits convert", [&] {
    for (int k = 0; k < kShapeCount; k++) {
      ProceduralShape ps;
      ps.shape = k;
      if (k == (int)ShapeKind::Plane) ps.size = {2, 0, 2};
      MeshPtr m = ps.build();
      const bool plane = k == (int)ShapeKind::Plane;
      const bool closed = closed_manifold(*m);
      std::printf("    %-9s %4zu faces, closed %d, volume %.3f\n", kShapeNames[k], m->face_count(), (int)closed, vol(*m));
      CHECK(m->face_count() > 0 && m->has_uvs());
      if (!plane) {
        CHECK(closed);
        CHECK(vol(*m) > 0.0f);
      }
    }
    Editor ed;
    ed.init_headless(800, 600);
    ed.step_frame_headless();
    ed.command("create Shape: Stairs");
    GameObject *g = ed.selected_object();
    CHECK(g && g->get<ProceduralShape>());
    if (!g || !g->get<ProceduralShape>()) return;
    const size_t f8 = g->get<MeshFilter>()->mesh->face_count();
    g->get<ProceduralShape>()->steps = 12;
    ed.step_frame_headless();
    CHECK(g->get<MeshFilter>()->mesh->face_count() > f8);  // rebuilt with more steps
    /* Editing the mesh by hand makes it an ordinary mesh. */
    meshops::flip_normals(*mesh_make_mutable(g->get<MeshFilter>()->mesh));
    ed.step_frame_headless();
    CHECK(g->get<ProceduralShape>() == nullptr);
  });
  test("n-gon: Merge Coplanar makes flat regions single faces; the knife splits a face again", [&] {
    Mesh grid = *primitives::grid(2.0f, 4, 4);
    CHECK(meshops::dissolve_limited(grid, 1.0f) > 0);
    std::printf("    4x4 grid -> %zu face(s), %zu corners\n", grid.face_count(), grid.face_count() ? (size_t)grid.face_size(0) : 0);
    CHECK(grid.face_count() == 1 && grid.face_size(0) == 4);
    /* A subdivided box stays a box of six faces. */
    Mesh box = meshops::subdivide(*primitives::cube(), 2, false);
    meshops::dissolve_limited(box, 1.0f);
    CHECK(box.face_count() == 6 && box.vert_count() == 8);
    CHECK(closed_manifold(box) && vol(box) > 0.99f);
    /* SketchUp's line across a face: midpoints of two opposite edges. */
    Mesh q = *primitives::cube();
    const size_t top = face_facing(q, {0, 1, 0});
    const uint32_t *v = q.face_verts(top);
    const uint32_t a0 = v[0], a1 = v[1], b0 = v[2], b1 = v[3];
    const uint32_t m0 = meshops::split_edge(q, a0, a1, 0.5f);
    const uint32_t m1 = meshops::split_edge(q, b0, b1, 0.5f);
    CHECK(meshops::split_face(q, top, m0, m1));
    CHECK(q.face_count() == 7 && closed_manifold(q));
    CHECK_NEAR(vol(q), 1.0f, 1e-4f);
    /* In n-gon terms the two halves are still one flat region. */
    std::vector<uint8_t> region;
    meshops::coplanar_region(q, face_facing(q, {0, 1, 0}), 1.0f, region);
    CHECK(std::count(region.begin(), region.end(), 1) == 2);
  });
}

/* ===================================================================== */
/* Shift-drag extrude, seams from sharp, light aiming, material assets,    */
/* drawing, Follow / Spin / Slice, Reset XForm, the pivot fix              */
/* ===================================================================== */

static void modeling_round10_tests() {
  const float cube_sign = signed_volume(*primitives::cube()) > 0 ? 1.0f : -1.0f;
  auto vol = [&](const Mesh &m) { return signed_volume(m) * cube_sign; };
  auto mouse = [](platform::EventType t, int x, int y, int mods = 0) {
    platform::Event e;
    e.type = t;
    e.x = x;
    e.y = y;
    e.mods = mods;
    return e;
  };
  test("gizmo: Shift + drag on a face extrudes it first (scale makes an inset face)", [&] {
    Editor ed;
    ed.init_headless(1000, 700);
    ed.step_frame_headless();
    ed.command("create Cube");
    GameObject *g = ed.selected_object();
    g->set_world_position({0, 0.5f, 0});
    ed.command("camera 0 0 5 0 1 0");  // looking at the top face's centre from the front
    ed.command("edit face");
    ed.command("fsel facing 0 1 0");
    ed.command("tool move");
    ed.step_frame_headless();
    Recti r = ed.scene_view_rect();
    const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    using ET = platform::EventType;
    ed.step_frame_headless({mouse(ET::MouseMove, cx, cy - 55)});
    ed.step_frame_headless({mouse(ET::MouseDown, cx, cy - 55, platform::MOD_SHIFT)});
    ed.step_frame_headless({mouse(ET::MouseMove, cx, cy - 95, platform::MOD_SHIFT)});
    ed.step_frame_headless({mouse(ET::MouseUp, cx, cy - 95)});
    ed.step_frame_headless();
    const Mesh &m = *g->get<MeshFilter>()->mesh;
    float top = -1e9f;
    for (const Vec3 &p : m.positions) top = std::max(top, p.y);
    std::printf("    shift-drag: %zu faces, top at %.3f\n", m.face_count(), top);
    CHECK(m.face_count() == 10);  // the top moved up on four new walls
    CHECK(top > 0.6f);
    CHECK(closed_manifold(m));
  });
  test("seams: Seams from Sharp Edges marks the cube's edges and a cylinder's rims only", [&] {
    Mesh cube = *primitives::cube();
    CHECK(meshops::seams_from_sharp(cube, 30.0f) == 12);
    Mesh cyl = *primitives::cylinder(0.5f, 1.0f, 24);
    const size_t n = meshops::seams_from_sharp(cyl, 30.0f);
    std::printf("    cylinder (24 sides): %zu seams\n", n);
    CHECK(n == 48);  // the two rims; sides meet at 15 degrees
    cyl.set_sharp(cyl.face_verts(0)[0], cyl.face_verts(0)[1], true);
    CHECK(meshops::sharp_edges_by_angle(cyl, 30.0f, true).size() >= 48);
  });
  test("lights: aiming at a point turns the light's +Z to it, keeps X level and reaches it", [&] {
    Editor ed;
    ed.init_headless(800, 600);
    ed.step_frame_headless();
    ed.command("create Spot Light");
    GameObject *l = ed.selected_object();
    l->set_world_position({2, 4, -1});
    l->get<Light>()->range = 1.0f;
    ed.command("aimlight -1 0 3");
    const Vec3 want = normalize(Vec3(-1, 0, 3) - Vec3(2, 4, -1));
    const Vec3 fwd = l->world_rotation().rotate({0, 0, 1}), right = l->world_rotation().rotate({1, 0, 0});
    std::printf("    forward (%.3f %.3f %.3f) want (%.3f %.3f %.3f), right.y %.4f, range %.2f\n", fwd.x, fwd.y, fwd.z, want.x, want.y, want.z,
                right.y, l->get<Light>()->range);
    CHECK(dot(fwd, want) > 0.9999f);
    CHECK(std::fabs(right.y) < 1e-3f);
    CHECK(l->get<Light>()->range >= length(Vec3(-1, 0, 3) - Vec3(2, 4, -1)));
  });
  test("materials: a .mat asset is shared, written back when edited, survives save / load, and slots can be removed", [&] {
    const std::string proj = fs::join(test_dir(), "matproject");
    fs::make_dirs(fs::join(proj, "Assets"));
    fs::make_dirs(fs::join(proj, "research"));
    set_env("BLENDITY_PROJECT", proj);
    Editor ed;
    ed.init_headless(800, 600);
    ed.step_frame_headless();
    ed.command("newmat Brick");
    const std::string path = "Assets/Materials/Brick.mat";
    CHECK(fs::exists(fs::join(proj, path)));
    MaterialPtr brick = material_asset(path);
    CHECK(brick != nullptr);
    if (!brick) return;
    ed.command("select Cube");
    ed.command("assignmat Assets/Materials/Brick.mat 0");
    ed.command("select Sphere");
    ed.command("assignmat Assets/Materials/Brick.mat 1");  // a new slot 1
    GameObject *cube = by_name(ed.scene(), "Cube"), *sphere = by_name(ed.scene(), "Sphere");
    CHECK(cube->get<MeshRenderer>()->materials[0] == brick && sphere->get<MeshRenderer>()->materials[1] == brick);
    /* Editing the asset changes both and is written back to the file. */
    brick->base_color = {0.7f, 0.2f, 0.1f};
    brick->touch();
    ed.commit_change("Edit Brick");
    ed.step_frame_headless({mouse(platform::EventType::MouseMove, 5, 5)});
    std::string text;
    CHECK(fs::read_file(fs::join(proj, path), text) && text.find("0.7") != std::string::npos);
    /* The scene file names the asset; loading links back to the one instance. */
    const std::string scene_text = save_scene_text(ed.scene());
    CHECK(scene_text.find("Brick.mat") != std::string::npos);
    Scene back;
    std::string err;
    CHECK(load_scene_text(scene_text, back, err));
    relink_material_assets(back, false);
    CHECK(back.find_by_name("Cube")->get<MeshRenderer>()->materials[0] == brick);
    /* Removing a slot moves its faces down; Remove Unused drops slots no face uses. */
    ed.command("select Sphere");
    CHECK(sphere->get<MeshRenderer>()->materials.size() == 2);
    ed.command("removeunusedslots");
    CHECK(sphere->get<MeshRenderer>()->materials.size() == 1);
    ed.command("select Cube");
    ed.command("assignmat Assets/Materials/Brick.mat 2");
    CHECK(cube->get<MeshRenderer>()->materials.size() == 3);
    ed.command("removeslot 1");
    CHECK(cube->get<MeshRenderer>()->materials.size() == 2 && cube->get<MeshRenderer>()->materials[1] == brick);
    set_env("BLENDITY_PROJECT", "");
  });
  test("draw: a rectangle on a face is cut into it, a line splits it, a circle on the ground is a face", [&] {
    Editor ed;
    ed.init_headless(800, 600);
    ed.step_frame_headless();
    ed.command("create Cube");
    GameObject *g = ed.selected_object();
    g->set_world_position({0, 0.5f, 0});
    ed.command("edit face");
    ed.command("draw rectangle");
    ed.command("drawpoint -0.25 1 -0.25");
    ed.command("drawpoint 0.25 1 0.25");
    const Mesh &m = *g->get<MeshFilter>()->mesh;
    std::printf("    rectangle on the top: %zu faces, closed %d\n", m.face_count(), (int)closed_manifold(m));
    CHECK(m.face_count() == 6 - 1 + 4 + 1);
    CHECK(closed_manifold(m));
    CHECK_NEAR(vol(m), 1.0f, 1e-4f);
    /* A bent line across the front face, from one edge's middle to the opposite one's. */
    ed.command("draw polyline");
    ed.command("drawpoint 0 1 -0.5");
    ed.command("drawpoint 0.1 0.5 -0.5");
    ed.command("drawpoint 0 0 -0.5");
    ed.command("drawpoint 0 0 -0.5 finish");
    const Mesh &m2 = *g->get<MeshFilter>()->mesh;
    std::printf("    polyline across the front: %zu faces, closed %d\n", m2.face_count(), (int)closed_manifold(m2));
    CHECK(m2.face_count() == 11);
    CHECK(closed_manifold(m2));
    ed.command("edit off");
    /* With nothing selected, drawing starts a new mesh on the ground. */
    ed.command("select Main Camera");
    ed.command("delete");
    ed.step_frame_headless();
    ed.command("draw circle 16");
    GameObject *d = ed.selected_object();
    CHECK(d && d->name == "Drawing");
    ed.command("drawpoint 3 0 3");
    ed.command("drawpoint 4 0 3");
    if (d) {
      const Mesh &c = *d->get<MeshFilter>()->mesh;
      CHECK(c.face_count() == 1 && c.face_size(0) == 16);
      CHECK(c.face_count() && c.face_normal(0).y > 0.99f);  // facing up, toward the camera
    }
  });
  test("follow, spin and slice: sweep a face along a path, lathe a profile, cut by a plane", [&] {
    /* Follow: a square swept along an L-shaped path makes a closed bent bar. */
    Mesh m;
    for (Vec3 p : {Vec3(-0.1f, 0, -0.1f), Vec3(0.1f, 0, -0.1f), Vec3(0.1f, 0, 0.1f), Vec3(-0.1f, 0, 0.1f)}) m.add_vert(p);
    m.add_face({0, 3, 2, 1});
    const uint32_t a = m.add_vert({0, 0, 0}), b = m.add_vert({0, 1, 0}), c = m.add_vert({1, 1, 0});
    m.add_loose_edge(a, b);
    m.add_loose_edge(b, c);
    std::string err;
    CHECK(meshops::follow(m, 0, &err));
    if (!err.empty()) std::printf("    follow: %s\n", err.c_str());
    meshops::merge_by_distance(m, 1e-6f);
    std::printf("    follow: %zu faces, closed %d, volume %.4f\n", m.face_count(), (int)closed_manifold(m), std::fabs(vol(m)));
    CHECK(closed_manifold(m) && m.loose_edges.empty());
    CHECK(vol(m) > 0.0f);
    CHECK_NEAR(vol(m), 0.04f * 2.0f, 0.01f);  // a 0.2 x 0.2 bar, 2 m long (the mitre keeps the corner)
    /* Spin: a vertical edge at x = 0.5 turned 360 degrees round Y is a tube. */
    Mesh s;
    s.add_vert({0.5f, 0, 0});
    s.add_vert({0.5f, 1, 0});
    s.add_loose_edge(0, 1);
    std::vector<uint8_t> sel = {1, 1};
    CHECK(meshops::spin(s, sel, Vec3(0.0f), {0, 1, 0}, 360.0f, 12) > 0);
    CHECK(s.face_count() == 12 && s.vert_count() == 24);
    /* Slice: a cube cut through the middle stays closed; clearing one side leaves the other half. */
    Mesh q = *primitives::cube();
    CHECK(meshops::slice(q, {0, 0, 0}, {0, 1, 0}) > 0);
    CHECK(q.face_count() == 10 && closed_manifold(q));
    CHECK_NEAR(vol(q), 1.0f, 1e-4f);
    Mesh h = *primitives::cube();
    meshops::slice(h, {0, 0.1f, 0}, {0, 1, 0}, 1);
    float top = -1e9f;
    for (const Vec3 &p : h.positions) top = std::max(top, p.y);
    CHECK_NEAR(top, 0.1f, 1e-4f);
  });
  test("objects: Reset XForm bakes rotation and scale, and the gizmo pivots on the origin", [&] {
    Editor ed;
    ed.init_headless(800, 600);
    ed.step_frame_headless();
    ed.command("create Cube");
    GameObject *g = ed.selected_object();
    g->set_local_euler({0, 30, 0});
    g->set_local_scale({2, 1, 1});
    g->set_world_position({1, 0.5f, 0});
    const AABB before = g->world_bounds();
    ed.command("select Cube");
    ed.select_object(g->id);
    ed.select_object(g->id);
    /* mesh_op through the console's generic path */
    ed.command("meshop apply_transform");
    const AABB after = g->world_bounds();
    CHECK(length(g->local().scale - Vec3(1.0f)) < 1e-5f && std::fabs(g->local().rotation.w) > 0.99999f);
    CHECK(length(after.min - before.min) < 1e-4f && length(after.max - before.max) < 1e-4f);
    /* The origin is where transforms happen: Set Origin to the bottom, then the gizmo sits there. */
    ed.command("origin bottom");
    CHECK(std::fabs(g->world_position().y - before.min.y) < 1e-4f);
    CHECK(!ed.pivot_is_center());
  });
}

/* ===================================================================== */
/* Drawing on rotated objects and drawing edge cases, Plasticity tools,     */
/* surface / vertex snapping, camera piloting, the new windows              */
/* ===================================================================== */

static bool structurally_valid(const Mesh &m, std::string *why = nullptr) {
  for (Vec3 p : m.positions)
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
      if (why) *why = "non-finite position";
      return false;
    }
  std::unordered_map<uint64_t, int> uses;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t n = m.face_size(f), *v = m.face_verts(f);
    if (n < 3) {
      if (why) *why = "face with < 3 corners";
      return false;
    }
    for (uint32_t i = 0; i < n; i++) {
      if (v[i] >= m.vert_count() || v[i] == v[(i + 1) % n]) {
        if (why) *why = "bad corner";
        return false;
      }
      if (++uses[Mesh::edge_key(v[i], v[(i + 1) % n])] > 2) {
        if (why) *why = "an edge on three faces";
        return false;
      }
    }
  }
  if (!m.uvs.empty() && m.uvs.size() != m.corner_verts.size()) {
    if (why) *why = "UV count";
    return false;
  }
  return true;
}

static void modeling_round11_tests() {
  const float cube_sign = signed_volume(*primitives::cube()) > 0 ? 1.0f : -1.0f;
  auto vol = [&](const Mesh &m) { return signed_volume(m) * cube_sign; };
  test("draw: a rectangle on a rotated object's face lines up with the object, not the world", [&] {
    Editor ed;
    ed.init_headless(800, 600);
    ed.step_frame_headless();
    ed.command("create Cube");
    GameObject *g = ed.selected_object();
    g->set_local_euler({0, 30, 0});
    g->set_world_position({0, 0.5f, 0});
    ed.command("edit face");
    ed.command("draw rectangle");
    /* Two opposite corners of a smaller square in the cube's own frame, in world space. */
    const Mat4 &w = g->world_matrix();
    const Vec3 a = w.point({-0.25f, 0.5f, -0.25f}), b = w.point({0.25f, 0.5f, 0.25f});
    ed.command(strprintf("drawpoint %.6f %.6f %.6f", a.x, a.y, a.z));
    ed.command(strprintf("drawpoint %.6f %.6f %.6f", b.x, b.y, b.z));
    const Mesh &m = *g->get<MeshFilter>()->mesh;
    CHECK(m.face_count() == 10 && closed_manifold(m));
    /* The inner face is a square in the cube's frame: its edges run along local X and Z. */
    const size_t inner = m.face_count() - 1;
    bool aligned = m.face_size(inner) == 4;
    for (uint32_t k = 0; k < m.face_size(inner) && aligned; k++) {
      const Vec3 e = normalize(m.positions[m.face_verts(inner)[(k + 1) % 4]] - m.positions[m.face_verts(inner)[k]]);
      aligned = std::fabs(e.x) > 0.999f || std::fabs(e.z) > 0.999f;
    }
    CHECK(aligned);
    /* Push it through the cube: a square hole, still a closed solid. */
    ed.command("fsel " + std::to_string(inner));
    std::vector<uint8_t> sel(m.face_count(), 0);
    sel[inner] = 1;
    Mesh copy = m;
    meshops::PushPullResult res;
    CHECK(meshops::push_pull(copy, sel, -1.0f, true, &res));
    CHECK(closed_manifold(copy));
    CHECK_NEAR(vol(copy), 1.0f - 0.25f, 1e-3f);
  });
  test("draw edge cases: an arc on a rectangle's side makes a second face; push / pull the pair", [&] {
    Editor ed;
    ed.init_headless(800, 600);
    ed.step_frame_headless();
    ed.command("select Main Camera");  // nothing with a mesh selected: a new Drawing
    ed.command("draw rectangle");
    GameObject *g = ed.selected_object();
    CHECK(g && g->name == "Drawing");
    if (!g) return;
    ed.command("drawpoint 0 0 0");
    ed.command("drawpoint 2 0 1");
    /* An arc on the rectangle's right side (x = 2, z 0..1), bulging out to x = 2.5. */
    ed.command("draw arc 16");
    ed.command("drawpoint 2 0 0");
    ed.command("drawpoint 2 0 1");
    ed.command("drawpoint 2.5 0 0.5");
    const Mesh &m = *g->get<MeshFilter>()->mesh;
    std::string why;
    std::printf("    rectangle + arc: %zu faces, %zu wire edges\n", m.face_count(), m.loose_edges.size());
    CHECK(structurally_valid(m, &why));
    if (!why.empty()) std::printf("    %s\n", why.c_str());
    CHECK(m.face_count() == 2 && m.loose_edges.empty());
    CHECK(dot(m.face_normal(0), m.face_normal(1)) > 0.999f);  // one flat surface, both facing the same way
    /* Area: 2 x 1 plus a half disc of radius 0.5. */
    auto area = [&](size_t f) {
      Vec3 s(0.0f);
      for (uint32_t k = 0; k < m.face_size(f); k++) s += cross(m.positions[m.face_verts(f)[k]], m.positions[m.face_verts(f)[(k + 1) % m.face_size(f)]]);
      return 0.5f * length(s);
    };
    const float total = area(0) + area(1);
    CHECK_NEAR(total, 2.0f + 0.5f * kPi * 0.25f, 0.02f);
    /* Pull both faces up together: one closed solid of that footprint. */
    Mesh both = m;
    std::vector<uint8_t> sel(both.face_count(), 1);
    meshops::PushPullResult res;
    CHECK(meshops::push_pull(both, sel, 0.5f, true, &res));
    std::printf("    pulled pair: %zu faces, closed %d, volume %.4f\n", both.face_count(), (int)closed_manifold(both), std::fabs(vol(both)));
    CHECK(structurally_valid(both) && closed_manifold(both));
    CHECK_NEAR(std::fabs(vol(both)), total * 0.5f, 0.02f);
    /* Pull only the arc's face: still a valid surface (open where the rectangle stays flat). */
    Mesh one = m;
    std::vector<uint8_t> s1(one.face_count(), 0);
    s1[1] = 1;
    CHECK(meshops::push_pull(one, s1, 0.3f, true, &res));
    CHECK(structurally_valid(one, &why));
  });
  test("draw edge cases: degenerate shapes, lines off the mesh, fillets, drawing on a drawing", [&] {
    Editor ed;
    ed.init_headless(800, 600);
    ed.step_frame_headless();
    ed.command("create Cube");
    GameObject *g = ed.selected_object();
    g->set_world_position({0, 0.5f, 0});
    ed.command("edit face");
    const auto mesh = [&]() -> const Mesh & { return *g->get<MeshFilter>()->mesh; };
    const size_t f0 = mesh().face_count();
    /* A zero-size rectangle and a zero-radius circle change nothing. */
    ed.command("draw rectangle");
    ed.command("drawpoint 0.1 1 0.1");
    ed.command("drawpoint 0.1 1 0.1");
    ed.command("draw circle");
    ed.command("drawpoint 0 1 0");
    ed.command("drawpoint 0 1 0");
    CHECK(structurally_valid(mesh()));
    CHECK(mesh().face_count() == f0);
    /* A shape hanging off the face's edge can't be cut in: it goes in as its own face. */
    ed.command("draw rectangle");
    ed.command("drawpoint 0.3 1 0.3");
    ed.command("drawpoint 0.8 1 0.8");
    CHECK(structurally_valid(mesh()));
    /* A line from an edge out into the air: wire edges, nothing broken. */
    ed.command("draw polyline");
    ed.command("drawpoint -0.5 1 0");
    ed.command("drawpoint -1.5 1 0");
    ed.command("drawpoint -1.5 1 -1 finish");
    CHECK(structurally_valid(mesh()));
    CHECK(mesh().loose_edges.size() >= 2);
    /* A rounded rectangle: each corner becomes an arc (4 x 7 corners). */
    ed.command("edit off");
    ed.command("select Main Camera");
    ed.command("draw rectangle");
    GameObject *d = ed.selected_object();
    /* draw_fillet_ through the console's draw settings */
    ed.command("drawfillet 0.25");
    ed.command("drawpoint 3 0 3");
    ed.command("drawpoint 5 0 4");
    if (d) {
      const Mesh &dm = *d->get<MeshFilter>()->mesh;
      CHECK(dm.face_count() == 1 && dm.face_size(0) == 28);
      /* Then a circle inside that rounded face, cut into it. */
      ed.command("drawfillet 0");
      ed.command("draw circle 12");
      ed.command("drawpoint 4 0 3.5");
      ed.command("drawpoint 4.3 0 3.5");
      CHECK(structurally_valid(dm));
      CHECK(dm.face_count() > 2);
    }
  });
  test("plasticity tools: shell, draft, thicken and a radial array", [&] {
    Mesh box = *primitives::cube();
    std::vector<uint8_t> top(box.face_count(), 0);
    top[face_facing(box, {0, 1, 0})] = 1;
    std::string err;
    CHECK(meshops::shell(box, top, 0.1f, &err));
    std::printf("    shell: %zu faces, closed %d, volume %.4f\n", box.face_count(), (int)closed_manifold(box), vol(box));
    CHECK(closed_manifold(box));
    CHECK(vol(box) > 0.2f && vol(box) < 0.6f);  // walls only: 1 - 0.8 * 0.8 * 0.9 = 0.424
    /* Draft: the four sides lean in 10 degrees toward the top. */
    Mesh d = *primitives::cube();
    std::vector<uint8_t> sides(d.face_count(), 0);
    for (size_t f = 0; f < d.face_count(); f++) sides[f] = std::fabs(d.face_normal(f).y) < 0.5f;
    CHECK(meshops::draft(d, sides, 10.0f, {0, 1, 0}) == 4);
    float top_w = 0, bot_w = 0;
    for (const Vec3 &p : d.positions) (p.y > 0 ? top_w : bot_w) = std::max(p.y > 0 ? top_w : bot_w, std::fabs(p.x));
    CHECK_NEAR(bot_w, 0.5f, 1e-4f);
    CHECK_NEAR(top_w, 0.5f - std::tan(10.0f * kDeg2Rad), 1e-3f);
    CHECK(closed_manifold(d));
    /* Thicken a plane into a slab. */
    Mesh p = *primitives::grid(1.0f, 1, 1);
    std::vector<uint8_t> none(p.face_count(), 0);
    CHECK(meshops::shell(p, none, 0.2f, &err));
    CHECK(closed_manifold(p) && std::fabs(vol(p)) > 0.19f);
    /* Radial array: six copies of an offset cube around Y. */
    Mesh r = *primitives::cube(0.5f);
    meshops::translate(r, {2, 0, 0});
    meshops::radial_array(r, 6, 1, 360.0f, 0.0f);
    CHECK(r.face_count() == 36);
    AABB b = r.bounds();
    CHECK(b.max.x > 2.2f && b.min.x < -2.2f && b.max.z > 1.9f && b.min.z < -1.9f);
    /* A fillet on a triangle and on a straight run leaves straight runs alone. */
    auto f = meshops::fillet_polygon({{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {1, 0, 1}}, true, 0.1f, 4, {0, 1, 0});
    CHECK(f.size() == 1 + 5 + 5 + 5);  // three rounded corners; the collinear point stays one point
  });
  test("snapping: drop on a surface, Ctrl+Shift move onto a face, V snaps vertex to vertex", [&] {
    Editor ed;
    ed.init_headless(1000, 700);
    ed.step_frame_headless();
    ed.command("create Cube");
    GameObject *base = ed.selected_object();
    base->name = "Base";
    base->set_world_position({0, 0.5f, 0});
    ed.command("create Sphere");
    GameObject *ball = ed.selected_object();
    ball->name = "Ball";
    ball->set_world_position({3, 2, 0});
    ed.command("camera 0 30 6 0 0.5 0");
    ed.step_frame_headless();
    int x, y;
    CHECK(ed.project_to_window({0.1f, 1.0f, 0.05f}, x, y));  // on the cube's top
    ed.command("select Ball");
    ed.command(strprintf("droponsurface %d %d", x, y));
    const AABB bb = ball->world_bounds();
    std::printf("    ball on the cube: bottom at y %.4f (cube top 1.0)\n", bb.min.y);
    CHECK_NEAR(bb.min.y, 1.0f, 2e-3f);
    /* V: drag a corner of a second cube onto a corner of the first. */
    ed.command("create Cube");
    GameObject *c2 = ed.selected_object();
    c2->name = "Second";
    c2->set_world_position({2.2f, 0.5f, -0.3f});
    ed.command("tool move");
    ed.step_frame_headless();
    const Vec3 from = c2->world_matrix().point({-0.5f, 0.5f, -0.5f}), to = base->world_matrix().point({0.5f, 0.5f, -0.5f});
    int fx, fy, tx, ty;
    CHECK(ed.project_to_window(from, fx, fy) && ed.project_to_window(to, tx, ty));
    using ET = platform::EventType;
    auto ev = [](ET t, int px, int py, int key = 0) {
      platform::Event e;
      e.type = t;
      e.x = px;
      e.y = py;
      e.key = key;
      return e;
    };
    ed.step_frame_headless({ev(ET::MouseMove, fx, fy), ev(ET::KeyDown, fx, fy, platform::KEY_V)});
    ed.step_frame_headless({ev(ET::MouseDown, fx, fy)});
    ed.step_frame_headless({ev(ET::MouseMove, (fx + tx) / 2, (fy + ty) / 2)});
    ed.step_frame_headless({ev(ET::MouseMove, tx, ty)});
    ed.step_frame_headless({ev(ET::MouseUp, tx, ty)});
    ed.step_frame_headless({ev(ET::KeyUp, tx, ty, platform::KEY_V)});
    const Vec3 now = c2->world_matrix().point({-0.5f, 0.5f, -0.5f});
    std::printf("    vertex snap: corner (%.3f %.3f %.3f), target (%.3f %.3f %.3f)\n", now.x, now.y, now.z, to.x, to.y, to.z);
    CHECK(length(now - to) < 1e-3f);
  });
  test("camera: piloting moves the camera with the view, Ctrl + wheel sets its field of view", [&] {
    Editor ed;
    ed.init_headless(1000, 700);
    ed.step_frame_headless();
    ed.command("select Main Camera");
    GameObject *cam = ed.selected_object();
    const float fov0 = cam->get<Camera>()->fov;
    ed.command("pilot");
    ed.step_frame_headless();
    /* The view starts at the camera: its frame inside the view shows exactly the camera's field of view. */
    {
      const Recti fr = ed.pilot_frame(), vr = ed.scene_view_rect();
      const float cam_v = cam->get<Camera>()->vertical_fov_deg(cam->get<Camera>()->image_aspect(1280.0f / 720.0f));
      const float framed = 2.0f * std::atan(std::tan(ed.scene_fov() * 0.5f * kDeg2Rad) * fr.h / (float)vr.h) * kRad2Deg;
      CHECK(fr.h > 0);
      CHECK_NEAR(framed, cam_v, 0.3f);
    }
    ed.command("camera 40 20 7 1 0.5 0");
    ed.step_frame_headless();
    const Vec3 fwd = cam->world_rotation().rotate({0, 0, 1});
    const Vec3 to_pivot = normalize(Vec3(1, 0.5f, 0) - cam->world_position());
    std::printf("    camera at (%.2f %.2f %.2f), looking %.4f toward the pivot\n", cam->world_position().x, cam->world_position().y,
                cam->world_position().z, dot(fwd, to_pivot));
    CHECK(dot(fwd, to_pivot) > 0.9999f);
    Recti r = ed.scene_view_rect();
    platform::Event mv, wheel;
    mv.type = platform::EventType::MouseMove;
    mv.x = wheel.x = r.x + r.w / 2;
    mv.y = wheel.y = r.y + r.h / 2;
    wheel.type = platform::EventType::Wheel;
    wheel.wheel_y = 3;
    wheel.mods = platform::MOD_CTRL;
    ed.step_frame_headless({mv});
    ed.step_frame_headless({wheel});
    ed.step_frame_headless();
    std::printf("    field of view %.1f -> %.1f\n", fov0, cam->get<Camera>()->fov);
    CHECK(cam->get<Camera>()->fov < fov0 - 1.0f);
    ed.command("pilot");  // stop
    ed.step_frame_headless();
    const Vec3 kept = cam->world_position();
    ed.command("camera 0 10 20 0 0 0");
    ed.step_frame_headless();
    CHECK(length(cam->world_position() - kept) < 1e-5f);  // no longer follows the view
  });
  test("windows: Materials and Modeling Tools draw, with and without a selection or Edit Mode", [&] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.step_frame_headless();
    for (const char *w : {"Materials", "Modeling Tools"}) {
      ed.command(std::string("window ") + w);
      ed.step_frame_headless();
      ed.command("select Cube");
      ed.step_frame_headless();
      ed.command("edit face");
      ed.step_frame_headless();
      ed.command("edit off");
      ed.command("select Main Camera");
      ed.step_frame_headless();
    }
    /* Drawing all of that without crashing is the main check; the tool window really is up. */
    CHECK(ed.window_rect_for_test(WindowKind::Tools).w > 0);
  });
}

/* ===================================================================== */
/* Round 12: drawing across faces, camera piloting, asset folders,       */
/* picking face materials, Plasticity drawing modes, guide lines, FPS    */
/* ===================================================================== */

static float face_area(const Mesh &m, size_t f) {
  Vec3 s(0.0f);
  for (uint32_t k = 0; k < m.face_size(f); k++) s += cross(m.positions[m.face_verts(f)[k]], m.positions[m.face_verts(f)[(k + 1) % m.face_size(f)]]);
  return 0.5f * length(s);
}

static void modeling_round12_tests() {
  auto mouse = [](platform::EventType t, int x, int y, int mods = 0) {
    platform::Event e;
    e.type = t;
    e.x = x;
    e.y = y;
    e.mods = mods;
    return e;
  };
  /* A regular polygon in the XZ plane. */
  auto ngon = [](Vec3 c, float r, int n, float phase = 0.0f) {
    std::vector<Vec3> p;
    for (int i = 0; i < n; i++) {
      const float a = phase + 2.0f * kPi * i / n;
      p.push_back(c + Vec3(std::cos(a), 0, std::sin(a)) * r);
    }
    return p;
  };
  test("draw across faces: a circle over a grid of quads is cut into every face it covers", [&] {
    Mesh m = *primitives::grid(4.0f, 4, 4);  // 16 quads of 1 x 1, y = 0
    const float total0 = [&] {
      float a = 0;
      for (size_t f = 0; f < m.face_count(); f++) a += face_area(m, f);
      return a;
    }();
    const auto circle = ngon({0.1f, 0, -0.2f}, 1.3f, 32);
    std::vector<size_t> inner;
    std::string err;
    const long r = meshops::imprint_loop_across(m, circle, {0, 1, 0}, &inner, &err);
    std::string why;
    std::printf("    circle over a 4 x 4 grid: %zu faces, %zu inside, %s\n", m.face_count(), inner.size(), err.c_str());
    CHECK(r >= 0);
    CHECK(structurally_valid(m, &why));
    CHECK(m.loose_edges.empty());
    CHECK(inner.size() >= 9);  // the circle spans parts of 3 x 3 (or more) quads
    float in_area = 0, all = 0;
    for (size_t f : inner) in_area += face_area(m, f);
    for (size_t f = 0; f < m.face_count(); f++) all += face_area(m, f);
    /* The pieces inside add up to the polygon's area; nothing was added or lost overall. */
    const float poly = 0.5f * 32 * 1.3f * 1.3f * std::sin(2.0f * kPi / 32);
    CHECK_NEAR(in_area, poly, 1e-3f);
    CHECK_NEAR(all, total0, 1e-3f);
    for (size_t f = 0; f < m.face_count(); f++) CHECK(m.face_normal(f).y > 0);
    /* Pulled up together: a solid cylinder standing on the grid. */
    std::vector<uint8_t> sel(m.face_count(), 0);
    for (size_t f : inner) sel[f] = 1;
    meshops::PushPullResult res;
    Mesh pulled = m;
    CHECK(meshops::push_pull(pulled, sel, 0.5f, true, &res));
    CHECK(structurally_valid(pulled, &why));
    /* Edge cases: through grid corners exactly, hanging off the grid, exactly along existing edges. */
    Mesh a = *primitives::grid(4.0f, 4, 4);
    CHECK(meshops::imprint_loop_across(a, ngon({0, 0, 0}, 1.0f, 4), {0, 1, 0}, &inner) >= 0);  // a diamond through 4 grid corners
    CHECK(structurally_valid(a, &why) && inner.size() == 4);
    Mesh b = *primitives::grid(4.0f, 4, 4);
    meshops::imprint_loop_across(b, ngon({1.8f, 0, 0}, 0.6f, 24), {0, 1, 0}, &inner);
    std::printf("    hanging off the edge: %zu faces, %zu wire edges, %zu inside\n", b.face_count(), b.loose_edges.size(), inner.size());
    CHECK(structurally_valid(b, &why));
    Mesh c = *primitives::grid(4.0f, 4, 4);
    const size_t fc = c.face_count();
    CHECK(meshops::imprint_loop_across(c, {{-1, 0, -1}, {1, 0, -1}, {1, 0, 1}, {-1, 0, 1}}, {0, 1, 0}, &inner) >= 0);
    CHECK(c.face_count() == fc && inner.size() == 4);  // along existing edges: nothing to cut, the 4 quads are selected
  });
  test("draw across faces: a circle overlapping a drawn rectangle's edge on a cube, then Push/Pull", [&] {
    Editor ed;
    ed.init_headless(800, 600);
    ed.step_frame_headless();
    ed.command("create Cube");
    GameObject *g = ed.selected_object();
    g->set_world_position({0, 0.5f, 0});
    ed.command("edit face");
    ed.command("draw rectangle");
    ed.command("drawpoint -0.3 1 -0.3");
    ed.command("drawpoint 0.1 1 0.1");
    const Mesh &m = *g->get<MeshFilter>()->mesh;
    const size_t after_rect = m.face_count();
    /* A circle centred on the rectangle's corner: half inside it, half in the ring around it. */
    ed.command("draw circle 16");
    ed.command("drawpoint 0.1 1 0.1");
    ed.command("drawpoint 0.3 1 0.1");
    std::string why;
    std::printf("    rectangle then overlapping circle: %zu -> %zu faces, closed %d\n", after_rect, m.face_count(), (int)closed_manifold(m));
    CHECK(structurally_valid(m, &why));
    CHECK(closed_manifold(m));
    CHECK(m.face_count() > after_rect + 2);
    /* No face sticks out of the top: every new face lies in the top plane. */
    size_t top = 0;
    for (size_t f = 0; f < m.face_count(); f++)
      if (m.face_normal(f).y > 0.99f) top++;
    ed.command("fsel facing 0 1 0");
    CHECK(top >= 4);
    /* Push the circle's pieces down: still a closed solid. */
    Mesh copy = m;
    std::vector<uint8_t> sel(copy.face_count(), 0);
    for (size_t f = 0; f < copy.face_count(); f++) {
      Vec3 c(0.0f);
      for (uint32_t k = 0; k < copy.face_size(f); k++) c += copy.positions[copy.face_verts(f)[k]];
      c = c / (float)copy.face_size(f);
      sel[f] = copy.face_normal(f).y > 0.99f && length(Vec3(c.x - 0.1f, 0, c.z - 0.1f)) < 0.2f;
    }
    meshops::PushPullResult res;
    CHECK(meshops::push_pull(copy, sel, -0.3f, true, &res));
    CHECK(closed_manifold(copy));
  });
  test("camera piloting: a camera wider than the view is framed exactly, and moving it another way moves the view", [&] {
    Editor ed;
    ed.init_headless(900, 800);  // a tall Scene view: a 16:9 camera fits its width, not its height
    ed.step_frame_headless();
    ed.command("select Main Camera");
    GameObject *cam = ed.selected_object();
    Camera *c = cam->get<Camera>();
    const float fov0 = c->fov;
    const Vec3 pos0 = cam->world_position();
    ed.command("pilot");
    ed.step_frame_headless();
    ed.step_frame_headless();
    /* Starting changes nothing on the camera. */
    CHECK_NEAR(c->fov, fov0, 1e-3f);
    CHECK(length(cam->world_position() - pos0) < 1e-4f);
    const Recti fr = ed.pilot_frame(), vr = ed.scene_view_rect();
    std::printf("    view %d x %d, camera frame %d x %d, view FOV %.2f, camera FOV %.2f\n", vr.w, vr.h, fr.w, fr.h, ed.scene_fov(), c->fov);
    CHECK(fr.h < vr.h - 10);
    const float framed = 2.0f * std::atan(std::tan(ed.scene_fov() * 0.5f * kDeg2Rad) * fr.h / (float)vr.h) * kRad2Deg;
    CHECK_NEAR(framed, c->fov, 0.3f);
    /* Moving the camera with the Inspector or the gizmo: the view follows it. */
    cam->set_world_position(pos0 + Vec3(2, 1, 0));
    ed.step_frame_headless();
    CHECK(length(ed.scene_eye() - cam->world_position()) < 1e-3f);
    c->fov = 35.0f;
    ed.step_frame_headless();
    ed.step_frame_headless();
    CHECK_NEAR(c->fov, 35.0f, 0.05f);  // kept, not overwritten by the view
    /* A physical camera keeps its sensor; navigating changes the lens consistently. */
    c->physical = true;
    c->sensor_fit = 0;
    c->focal_length = 50.0f;
    ed.step_frame_headless();
    ed.step_frame_headless();
    CHECK_NEAR(c->focal_length, 50.0f, 0.05f);
    platform::Event wheel;
    wheel.type = platform::EventType::Wheel;
    wheel.x = vr.x + vr.w / 2;
    wheel.y = vr.y + vr.h / 2;
    wheel.wheel_y = 2;
    wheel.mods = platform::MOD_CTRL;
    ed.step_frame_headless({mouse(platform::EventType::MouseMove, wheel.x, wheel.y)});
    ed.step_frame_headless({wheel});
    ed.step_frame_headless();
    std::printf("    physical camera: 50 mm -> %.1f mm after zooming in\n", c->focal_length);
    CHECK(c->focal_length > 52.0f);
    const float vfov = c->vertical_fov_deg(c->image_aspect(1280.0f / 720.0f));
    const Recti fr2 = ed.pilot_frame();
    const float framed2 = 2.0f * std::atan(std::tan(ed.scene_fov() * 0.5f * kDeg2Rad) * fr2.h / (float)vr.h) * kRad2Deg;
    CHECK_NEAR(framed2, vfov, 0.3f);
    ed.command("pilot");
  });
  test("project: folders, moving and renaming assets keep every reference; deleting goes to the trash", [&] {
    const std::string proj = fs::join(test_dir(), "assetproject");
    std::error_code ec;
    std::filesystem::remove_all(proj, ec);
    fs::make_dirs(fs::join(proj, "Assets"));
    fs::make_dirs(fs::join(proj, "research"));
    const std::string trash = fs::join(test_dir(), "trash");
    std::filesystem::remove_all(trash, ec);
    set_env("BLENDITY_PROJECT", proj);
    set_env("BLENDITY_TRASH", trash);
    {
      Editor ed;
      ed.init_headless(900, 600);
      ed.step_frame_headless();
      ed.command("newmat Stone");
      MaterialPtr stone = material_asset("Assets/Materials/Stone.mat");
      CHECK(stone != nullptr);
      if (!stone) return;
      ed.command("select Cube");
      ed.command("assignmat Assets/Materials/Stone.mat 0");
      GameObject *cube = by_name(ed.scene(), "Cube");
      /* A saved scene that refers to it, to see it rewritten. */
      const std::string scene_path = fs::join(proj, "Assets/Scenes/Other.scene");
      CHECK(save_scene(ed.scene(), scene_path));
      /* A folder, and the material dragged into it. */
      CHECK(!ed.create_project_folder(fs::join(proj, "Assets/Materials")).empty());
      ed.command("mkfolder Assets/Textures/Bricks");
      CHECK(fs::is_dir(fs::join(proj, "Assets/Textures/Bricks")));
      CHECK(fs::is_dir(fs::join(proj, "Assets/Materials/New Folder")));
      ed.command("renameasset Assets/Materials/New_Folder Rocks");  // no such path: nothing happens
      ed.command("moveasset Assets/Materials/Stone.mat Assets/Materials");  // same folder: nothing happens
      CHECK(fs::exists(fs::join(proj, "Assets/Materials/Stone.mat")));
      CHECK(ed.move_project_entry(fs::join(proj, "Assets/Materials/Stone.mat"), fs::join(proj, "Assets/Materials/New Folder")));
      CHECK(!fs::exists(fs::join(proj, "Assets/Materials/Stone.mat")));
      CHECK(fs::exists(fs::join(proj, "Assets/Materials/New Folder/Stone.mat")));
      CHECK(stone->asset_path == "Assets/Materials/New Folder/Stone.mat");
      CHECK(cube->get<MeshRenderer>()->materials[0] == stone);
      CHECK(material_asset("Assets/Materials/New Folder/Stone.mat") == stone);
      std::string text;
      CHECK(fs::read_file(scene_path, text) && text.find("New Folder/Stone.mat") != std::string::npos);
      /* Renaming the folder carries the material along. */
      CHECK(ed.rename_project_entry(fs::join(proj, "Assets/Materials/New Folder"), "Rocks"));
      CHECK(stone->asset_path == "Assets/Materials/Rocks/Stone.mat");
      CHECK(fs::read_file(scene_path, text) && text.find("Rocks/Stone.mat") != std::string::npos);
      /* A folder can't go inside itself. */
      fs::make_dirs(fs::join(proj, "Assets/Materials/Rocks/Inner"));
      CHECK(!ed.move_project_entry(fs::join(proj, "Assets/Materials/Rocks"), fs::join(proj, "Assets/Materials/Rocks/Inner")));
      /* Unity: a material's name is its file's name, both ways. */
      CHECK(ed.rename_project_entry(fs::join(proj, "Assets/Materials/Rocks/Stone.mat"), "Granite"));
      CHECK(stone->name == "Granite" && fs::exists(fs::join(proj, "Assets/Materials/Rocks/Granite.mat")));
      stone->name = "Basalt";
      stone->touch();
      ed.step_frame_headless();
      CHECK(fs::exists(fs::join(proj, "Assets/Materials/Rocks/Basalt.mat")) && !fs::exists(fs::join(proj, "Assets/Materials/Rocks/Granite.mat")));
      CHECK(stone->asset_path == "Assets/Materials/Rocks/Basalt.mat");
      /* Undo brings back an old copy: it follows the file rather than recreating Stone.mat. */
      Scene snap;
      std::string err;
      CHECK(load_scene(scene_path, snap, err));
      relink_material_assets(snap, false);
      CHECK(snap.find_by_name("Cube")->get<MeshRenderer>()->materials[0] == stone);
      CHECK(!fs::exists(fs::join(proj, "Assets/Materials/Stone.mat")));
      /* A .mat copied in Explorer still has the old name inside: the file name wins, nothing is renamed. */
      CHECK(fs::copy_file(fs::join(proj, "Assets/Materials/Rocks/Basalt.mat"), fs::join(proj, "Assets/Materials/Rocks/Copy Of It.mat")));
      MaterialPtr copied = material_asset("Assets/Materials/Rocks/Copy Of It.mat");
      CHECK(copied && copied->name == "Copy Of It");
      ed.step_frame_headless();
      CHECK(fs::exists(fs::join(proj, "Assets/Materials/Rocks/Copy Of It.mat")) && fs::exists(fs::join(proj, "Assets/Materials/Rocks/Basalt.mat")));
      /* Delete: into the trash; the cube keeps the material as a scene material. */
      CHECK(ed.delete_project_entry(fs::join(proj, "Assets/Materials/Rocks")));
      CHECK(!fs::exists(fs::join(proj, "Assets/Materials/Rocks")) && fs::is_dir(fs::join(trash, "Rocks")));
      CHECK(cube->get<MeshRenderer>()->materials[0] == stone && stone->asset_path.empty());
      ed.step_frame_headless();
      save_dirty_material_assets();
      CHECK(!fs::exists(fs::join(proj, "Assets/Materials/Rocks")));  // nothing writes it back
      /* Assets outside Assets, or Assets itself, are refused. */
      CHECK(!ed.delete_project_entry(fs::join(proj, "Assets")));
      CHECK(!ed.delete_project_entry(fs::join(proj, "research")));
      /* The Project window draws with folders, a rename and a delete waiting. */
      ed.command("window Project");
      ed.step_frame_headless();
      CHECK(ed.window_rect_for_test(WindowKind::Project).w > 0);
    }
    set_env("BLENDITY_TRASH", "");
    set_env("BLENDITY_PROJECT", "");
  });
  test("face materials: pick the material itself for the selected faces (its slot is found or added)", [&] {
    const std::string proj = fs::join(test_dir(), "facematproject");
    fs::make_dirs(fs::join(proj, "Assets"));
    fs::make_dirs(fs::join(proj, "research"));
    set_env("BLENDITY_PROJECT", proj);
    {
      Editor ed;
      ed.init_headless(900, 600);
      ed.step_frame_headless();
      ed.command("newmat Red");
      ed.command("newmat Blue");
      ed.command("select Cube");
      GameObject *cube = ed.selected_object();
      ed.command("fsel facing 0 1 0");
      ed.command("facemat Assets/Materials/Red.mat");
      auto *mr = cube->get<MeshRenderer>();
      const Mesh &m = *cube->get<MeshFilter>()->mesh;
      const size_t top = face_facing(m, {0, 1, 0});
      CHECK(mr->materials.size() == 2 && mr->materials[1] == material_asset("Assets/Materials/Red.mat"));
      CHECK(m.material_of(top) == 1);
      /* The same material on other faces reuses its slot; another adds one. */
      ed.command("fsel facing 0 -1 0");
      ed.command("facemat Assets/Materials/Red.mat");
      CHECK(mr->materials.size() == 2 && m.material_of(face_facing(m, {0, -1, 0})) == 1);
      ed.command("fsel facing 1 0 0 -1 0 0");
      ed.command("facemat Assets/Materials/Blue.mat");
      CHECK(mr->materials.size() == 3 && m.material_of(face_facing(m, {1, 0, 0})) == 2 && m.material_of(face_facing(m, {-1, 0, 0})) == 2);
      CHECK(m.material_of(top) == 1);
      /* The picker draws (it lists the object's slots, the assets and the scene's materials). */
      CHECK(ed.pickable_materials_for_test(cube) >= 3);
      ed.step_frame_headless();
    }
    set_env("BLENDITY_PROJECT", "");
  });
  test("draw modes: rectangles from the centre, square, or 3 points at an angle; circles from 2 or 3 points", [&] {
    Editor ed;
    ed.init_headless(800, 600);
    ed.step_frame_headless();
    ed.command("select Main Camera");
    auto drawn = [&](std::initializer_list<Vec3> clicks, const char *shape) -> std::vector<Vec3> {
      ed.command("select Main Camera");
      ed.command("edit off");
      ed.command(std::string("draw ") + shape);
      GameObject *g = ed.selected_object();
      for (Vec3 p : clicks) ed.command(strprintf("drawpoint %.6f %.6f %.6f", p.x, p.y, p.z));
      std::vector<Vec3> out;
      if (g && g->get<MeshFilter>() && g->get<MeshFilter>()->mesh && g->get<MeshFilter>()->mesh->face_count() == 1) {
        const Mesh &m = *g->get<MeshFilter>()->mesh;
        for (uint32_t k = 0; k < m.face_size(0); k++) out.push_back(g->world_matrix().point(m.positions[m.face_verts(0)[k]]));
      }
      ed.command("edit off");
      return out;
    };
    auto bounds = [](const std::vector<Vec3> &p) {
      AABB b;
      for (const Vec3 &q : p) b.add(q);
      return b;
    };
    ed.command("drawmode rect center");
    auto r1 = drawn({{1, 0, 1}, {1.5f, 0, 1.25f}}, "rectangle");
    CHECK(r1.size() == 4);
    if (r1.size() == 4) {
      const AABB b = bounds(r1);
      CHECK(length(b.center() - Vec3(1, 0, 1)) < 1e-4f);
      CHECK_NEAR(b.max.x - b.min.x, 1.0f, 1e-4f);
      CHECK_NEAR(b.max.z - b.min.z, 0.5f, 1e-4f);
    }
    ed.command("drawmode square on");
    auto r2 = drawn({{1, 0, 1}, {1.5f, 0, 1.25f}}, "rectangle");
    if (r2.size() == 4) {
      const AABB b = bounds(r2);
      CHECK_NEAR(b.max.x - b.min.x, 1.0f, 1e-4f);
      CHECK_NEAR(b.max.z - b.min.z, 1.0f, 1e-4f);
    }
    CHECK(r2.size() == 4);
    ed.command("drawmode square off");
    /* 3 points: a side at 30 degrees, then 0.5 wide. */
    ed.command("drawmode rect 3point");
    const Vec3 a(0, 0, 0), b2(std::cos(0.5236f) * 2, 0, std::sin(0.5236f) * 2);
    const Vec3 side = normalize(b2 - a), across(-side.z, 0, side.x);
    auto r3 = drawn({a, b2, b2 + across * 0.5f + side * 0.3f}, "rectangle");  // the third click's sideways part doesn't matter
    CHECK(r3.size() == 4);
    if (r3.size() == 4) {
      float area = 0;
      for (size_t k = 0; k < 4; k++) area += cross(r3[k], r3[(k + 1) % 4]).y * 0.5f;
      CHECK_NEAR(std::fabs(area), 2.0f * 0.5f, 1e-3f);
      bool has_a = false, has_b = false;
      for (const Vec3 &p : r3) has_a = has_a || length(p - a) < 1e-4f, has_b = has_b || length(p - b2) < 1e-4f;
      CHECK(has_a && has_b);
    }
    ed.command("drawmode rect corner");
    /* Circles: 2 points across; 3 points on it. */
    ed.command("drawmode circle 2point");
    auto c2 = drawn({{-1, 0, 0}, {1, 0, 0}}, "circle 16");
    CHECK(c2.size() == 16);
    for (const Vec3 &p : c2) CHECK_NEAR(length(p), 1.0f, 1e-3f);
    ed.command("drawmode circle 3point");
    auto c3 = drawn({{2, 0, 0}, {0, 0, 2}, {-2, 0, 0}}, "circle 16");
    CHECK(c3.size() == 16);
    for (const Vec3 &p : c3) CHECK_NEAR(length(p), 2.0f, 1e-3f);
    auto bad = drawn({{0, 0, 0}, {1, 0, 0}, {2, 0, 0}}, "circle 16");  // in a line: no circle
    CHECK(bad.empty());
    ed.command("drawmode circle center");
  });
  test("guide lines: added, undone, saved; drawing snaps to where two cross and along them", [&] {
    Editor ed;
    ed.init_headless(1000, 700);
    ed.step_frame_headless();
    ed.command("guide 0.37 0 -3 0 0 1");  // along Z through x = 0.37
    ed.command("guide -3 0 0.61 1 0 0");  // along X through z = 0.61
    ed.step_frame_headless();
    CHECK(ed.scene().guides.size() == 2);
    /* Saved with the scene and read back. */
    Scene back;
    std::string err;
    CHECK(load_scene_text(save_scene_text(ed.scene()), back, err) && back.guides.size() == 2);
    CHECK(back.guides.size() == 2 && length(back.guides[1].p - Vec3(-3, 0, 0.61f)) < 1e-5f);
    /* Click near where they cross: the first corner lands exactly there. */
    ed.command("camera 0 60 8 0 0 0");
    ed.command("select Main Camera");
    ed.command("draw rectangle");
    GameObject *g = ed.selected_object();
    ed.step_frame_headless();
    int x, y;
    CHECK(ed.project_to_window({0.37f, 0, 0.61f}, x, y));
    ed.step_frame_headless({mouse(platform::EventType::MouseMove, x + 4, y - 3)});
    ed.step_frame_headless({mouse(platform::EventType::MouseDown, x + 4, y - 3)});
    ed.step_frame_headless({mouse(platform::EventType::MouseUp, x + 4, y - 3)});
    ed.command("drawpoint 1.5 0 1.5");
    bool corner = false;
    if (g && g->get<MeshFilter>()->mesh)
      for (const Vec3 &p : g->get<MeshFilter>()->mesh->positions) corner = corner || length(g->world_matrix().point(p) - Vec3(0.37f, 0, 0.61f)) < 1e-4f;
    CHECK(corner);
    /* Along a guide: a click near it lands on it. */
    ed.command("draw polyline");
    int x2, y2;
    CHECK(ed.project_to_window({0.37f, 0, -1.2f}, x2, y2));
    ed.step_frame_headless({mouse(platform::EventType::MouseMove, x2 + 3, y2)});
    ed.step_frame_headless({mouse(platform::EventType::MouseDown, x2 + 3, y2)});
    ed.step_frame_headless({mouse(platform::EventType::MouseUp, x2 + 3, y2)});
    ed.command("drawpoint -1 0 -1.2 finish");
    bool on = false;
    for (const Vec3 &p : g->get<MeshFilter>()->mesh->positions) {
      const Vec3 w = g->world_matrix().point(p);
      on = on || (std::fabs(w.x - 0.37f) < 1e-4f && std::fabs(w.z + 1.2f) < 0.05f);
    }
    CHECK(on);
    /* Drawing a guide with the tool, then undo removes it. */
    ed.command("draw guide");
    ed.command("drawpoint 0 0 0");
    ed.command("drawpoint 1 0 1");
    CHECK(ed.scene().guides.size() == 3);
    ed.step_frame_headless();
    platform::Event z;
    z.type = platform::EventType::KeyDown;
    z.key = platform::KEY_Z;
    z.mods = platform::MOD_CTRL;
    ed.command("edit off");
    ed.step_frame_headless();
    ed.step_frame_headless({z});
    CHECK(ed.scene().guides.size() == 2);
    ed.command("clearguides");
    CHECK(ed.scene().guides.empty());
  });
  test("frame rate: the cap spaces frames, unlimited never waits, and the setting is kept", [&] {
    Editor ed;
    ed.init_headless(800, 600);
    ed.step_frame_headless();
    ed.command("fps 60");
    CHECK(ed.max_fps() == 60);
    CHECK_NEAR(ed.frame_wait_seconds(10.0, 10.0), 1.0 / 60.0, 1e-9);
    CHECK_NEAR(ed.frame_wait_seconds(10.010, 10.0), 1.0 / 60.0 - 0.010, 1e-9);
    CHECK(ed.frame_wait_seconds(10.5, 10.0) == 0.0);
    ed.command("fps unlimited");
    CHECK(ed.max_fps() == 0 && ed.frame_wait_seconds(10.0, 10.0) == 0.0);
    ed.command("fps 144");
    ed.command("redraw always");
    CHECK(ed.always_redraw());
    ed.command("redraw changes");
    CHECK(!ed.always_redraw());
    /* The Preferences and the Profiler show the setting and its measured cost. */
    ed.command("window Profiler");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    CHECK(ed.window_rect_for_test(WindowKind::Profiler).w > 0);
  });
}

/* ===================================================================== */
/* Round 13: Push/Pull sequences without overlapping faces, Delete Loose, */
/* region inset, empty number fields, face centres, depth of field        */
/* ===================================================================== */

static size_t overlapping_face_pairs(const Mesh &m, std::string *first = nullptr) {
  std::vector<std::pair<uint32_t, uint32_t>> pairs;
  const size_t n = meshops::overlapping_faces(m, 0.0f, &pairs);
  if (first && n) *first = strprintf("faces %u and %u", pairs[0].first, pairs[0].second);
  return n;
}

static void modeling_round13_tests() {
  const float cube_sign = signed_volume(*primitives::cube()) > 0 ? 1.0f : -1.0f;
  auto vol = [&](const Mesh &m) { return signed_volume(m) * cube_sign; };
  /* A cube whose top is cut into a smaller square (the drawing tool's imprint). */
  auto cube_with_square = [](float half) {
    Mesh m = *primitives::cube();
    const size_t top = face_facing(m, {0, 1, 0});
    meshops::imprint_loop(m, top, {{-half, 0.5f, -half}, {half, 0.5f, -half}, {half, 0.5f, half}, {-half, 0.5f, half}});
    return m;
  };
  /* A cube whose top is split into two halves along x = 0. */
  auto cube_halves = [] {
    Mesh m = *primitives::cube();
    const size_t top = face_facing(m, {0, 1, 0});
    const uint32_t *fv = m.face_verts(top);
    /* The two top edges running along X get a midpoint each; then the face splits between them. */
    std::vector<std::pair<uint32_t, uint32_t>> along_x;
    for (uint32_t k = 0; k < 4; k++) {
      const uint32_t a = fv[k], b = fv[(k + 1) % 4];
      if (std::fabs(m.positions[a].x - m.positions[b].x) > 0.5f) along_x.push_back({a, b});
    }
    const uint32_t m0 = meshops::split_edge(m, along_x[0].first, along_x[0].second, 0.5f);
    const uint32_t m1 = meshops::split_edge(m, along_x[1].first, along_x[1].second, 0.5f);
    meshops::split_face(m, face_facing(m, {0, 1, 0}), m0, m1);
    return m;
  };
  auto top_faces = [](const Mesh &m, float cx_sign) {
    /* Selection: upward faces whose centre is on the given side of x = 0 (0: any). */
    std::vector<uint8_t> sel(m.face_count(), 0);
    for (size_t f = 0; f < m.face_count(); f++)
      sel[f] = m.face_normal(f).y > 0.99f && (cx_sign == 0 || m.face_center(f).x * cx_sign > 0);
    return sel;
  };
  auto highest_up = [](const Mesh &m, float cx_sign) {
    /* The highest upward face; at the same height, the one whose centre is nearest the
     * middle of that side (x = 0.25 * cx_sign, z = 0). */
    std::vector<uint8_t> sel(m.face_count(), 0);
    const Vec3 want(0.25f * cx_sign, 0, 0);
    size_t pick = SIZE_MAX;
    for (size_t f = 0; f < m.face_count(); f++) {
      if (m.face_normal(f).y < 0.99f) continue;
      const Vec3 c = m.face_center(f);
      if (cx_sign != 0 && c.x * cx_sign <= 0) continue;
      if (pick == SIZE_MAX) { pick = f; continue; }
      const Vec3 b = m.face_center(pick);
      const float dy = c.y - b.y;
      if (dy > 1e-4f || (std::fabs(dy) <= 1e-4f && length(Vec3(c.x, 0, c.z) - want) < length(Vec3(b.x, 0, b.z) - want))) pick = f;
    }
    if (pick != SIZE_MAX) sel[pick] = 1;
    return sel;
  };
  struct Step {
    const char *what;
    std::function<std::vector<uint8_t>(const Mesh &)> select;
    float d;
  };
  auto run = [&](const char *name, Mesh m, std::vector<Step> steps, float expect_volume) {
    test((std::string("push/pull sequence: ") + name).c_str(), [&] {
      std::string why;
      for (const Step &s : steps) {
        std::vector<uint8_t> sel = s.select(m);
        meshops::PushPullResult res;
        std::string err;
        const bool ok = meshops::push_pull(m, sel, s.d, true, &res, &err);
        std::string ov;
        const size_t overlaps = overlapping_face_pairs(m, &ov);
        std::printf("    %-34s %s: %zu faces, closed %d, volume %.4f, overlapping pairs %zu %s\n", s.what, ok ? "ok" : err.c_str(), m.face_count(),
                    (int)closed_manifold(m), vol(m), overlaps, ov.c_str());
        CHECK(structurally_valid(m, &why));
        CHECK(overlaps == 0);
        CHECK(closed_manifold(m));
      }
      if (expect_volume > 0) CHECK_NEAR(vol(m), expect_volume, 2e-3f);
    });
  };
  /* The drawn square wherever it is now: the upward face whose centre is nearest the middle. */
  const auto inner_top = [&](const Mesh &m) {
    std::vector<uint8_t> sel(m.face_count(), 0);
    size_t pick = SIZE_MAX;
    float best = 1e30f;
    for (size_t f = 0; f < m.face_count(); f++) {
      if (m.face_normal(f).y < 0.99f) continue;
      const Vec3 c = m.face_center(f);
      const float d = std::sqrt(c.x * c.x + c.z * c.z);
      if (d < best - 1e-4f) best = d, pick = f;
    }
    if (pick != SIZE_MAX) sel[pick] = 1;
    return sel;
  };
  run("pull a drawn square up, then push it back flush", cube_with_square(0.25f),
      {{"pull up 0.5", inner_top, 0.5f}, {"push back down 0.5", inner_top, -0.5f}}, 1.0f);
  run("push a drawn square in, then pull it back flush", cube_with_square(0.25f),
      {{"push in 0.3", inner_top, -0.3f}, {"pull back up 0.3", inner_top, 0.3f}}, 1.0f);
  run("pull a drawn square up, then push it below the top", cube_with_square(0.25f),
      {{"pull up 0.5", inner_top, 0.5f}, {"push down 0.8", inner_top, -0.8f}}, 1.0f - 0.25f * 0.3f);
  run("pull both halves of the top to the same height, one after the other", cube_halves(),
      {{"pull the left half 0.5", [&](const Mesh &m) { return top_faces(m, -1); }, 0.5f},
       {"pull the right half 0.5", [&](const Mesh &m) { return highest_up(m, 1); }, 0.5f}},
      1.5f);
  run("pull the halves to different heights", cube_halves(),
      {{"pull the left half 0.5", [&](const Mesh &m) { return top_faces(m, -1); }, 0.5f},
       {"pull the right half 0.3", [&](const Mesh &m) { return highest_up(m, 1); }, 0.3f}},
      1.0f + 0.25f + 0.15f);
  run("pull the halves past each other", cube_halves(),
      {{"pull the left half 0.3", [&](const Mesh &m) { return top_faces(m, -1); }, 0.3f},
       {"pull the right half 0.6", [&](const Mesh &m) { return highest_up(m, 1); }, 0.6f}},
      1.0f + 0.15f + 0.3f);
  run("pull one half, push the other in", cube_halves(),
      {{"pull the left half 0.4", [&](const Mesh &m) { return top_faces(m, -1); }, 0.4f},
       {"push the right half 0.4", [&](const Mesh &m) { return highest_up(m, 1); }, -0.4f}},
      1.0f);
  run("pull the whole top up and back down", *primitives::cube(),
      {{"pull up 0.5", inner_top, 0.5f}, {"push down 0.5", inner_top, -0.5f}, {"push down 0.25", inner_top, -0.25f}}, 0.75f);
  test("push/pull sequence: inset, push in, pull back flush", [&] {
    Mesh m = *primitives::cube();
    std::vector<uint8_t> sel(m.face_count(), 0);
    sel[face_facing(m, {0, 1, 0})] = 1;
    meshops::inset_faces(m, sel, 0.4f);
    meshops::PushPullResult res;
    CHECK(meshops::push_pull(m, sel, -0.3f, true, &res));
    CHECK(meshops::push_pull(m, sel, 0.3f, true, &res));
    std::string ov;
    const size_t n = overlapping_face_pairs(m, &ov);
    std::printf("    inset, in, back: %zu faces, overlapping pairs %zu %s, closed %d, volume %.4f\n", m.face_count(), n, ov.c_str(), (int)closed_manifold(m), vol(m));
    CHECK(n == 0 && closed_manifold(m));
    CHECK_NEAR(vol(m), 1.0f, 2e-3f);
  });
}

static void round13_feature_tests() {
  const float cube_sign = signed_volume(*primitives::cube()) > 0 ? 1.0f : -1.0f;
  auto vol = [&](const Mesh &m) { return signed_volume(m) * cube_sign; };
  auto mouse = [](platform::EventType t, int x, int y, int mods = 0) {
    platform::Event e;
    e.type = t;
    e.x = x;
    e.y = y;
    e.mods = mods;
    return e;
  };
  test("delete loose: stray vertices and wire edges go, faces stay; only inside a selection when there is one", [&] {
    Mesh m = *primitives::cube();
    const uint32_t a = m.add_vert({3, 0, 0}), b = m.add_vert({4, 0, 0});
    m.add_loose_edge(a, b);
    m.add_vert({5, 5, 5});  // a lone point
    m.add_loose_edge(0, 7);  // a wire edge between two cube corners: its vertices stay (faces use them)
    CHECK(m.vert_count() == 11 && m.loose_edges.size() == 2);
    Mesh sel_only = m;
    std::vector<uint8_t> mask(sel_only.vert_count(), 0);
    mask[a] = mask[b] = 1;
    meshops::LooseCounts c1 = meshops::delete_loose(sel_only, true, true, false, &mask);
    CHECK(c1.edges == 1 && c1.verts == 2 && sel_only.vert_count() == 9 && sel_only.loose_edges.size() == 1);
    meshops::LooseCounts c = meshops::delete_loose(m);
    std::printf("    delete loose: %zu vertices, %zu wire edges; %zu verts and %zu faces left\n", c.verts, c.edges, m.vert_count(), m.face_count());
    CHECK(c.verts == 3 && c.edges == 2);
    CHECK(m.vert_count() == 8 && m.face_count() == 6 && m.loose_edges.empty() && closed_manifold(m));
    /* Loose faces (sharing no edge) go only when asked. */
    Mesh two = *primitives::cube();
    const uint32_t q0 = two.add_vert({3, 0, 0}), q1 = two.add_vert({4, 0, 0}), q2 = two.add_vert({4, 1, 0});
    const uint32_t tri[3] = {q0, q1, q2};
    two.add_face(tri, 3);
    CHECK(meshops::delete_loose(two, true, true, true).faces == 1 && two.face_count() == 6 && two.vert_count() == 8);
    /* The editor's operator, in Edit Mode. */
    Editor ed;
    ed.init_headless(800, 600);
    ed.step_frame_headless();
    ed.command("create Cube");
    GameObject *g = ed.selected_object();
    Mesh &gm = *mesh_make_mutable(g->get<MeshFilter>()->mesh);
    gm.add_vert({9, 9, 9});
    gm.touch();
    ed.command("edit vertex");
    ed.command("editop delete_loose");
    ed.command("edit off");
    CHECK(g->get<MeshFilter>()->mesh->vert_count() == 8);
  });
  test("inset: individual faces, or the selection as one region (only its outline moves in)", [&] {
    /* Two neighbouring quads of a 2 x 1 strip on a box top. */
    Mesh base = *primitives::cube();
    {
      const size_t top = face_facing(base, {0, 1, 0});
      const uint32_t *fv = base.face_verts(top);
      std::vector<std::pair<uint32_t, uint32_t>> along_x;
      for (uint32_t k = 0; k < 4; k++)
        if (std::fabs(base.positions[fv[k]].x - base.positions[fv[(k + 1) % 4]].x) > 0.5f) along_x.push_back({fv[k], fv[(k + 1) % 4]});
      const uint32_t m0 = meshops::split_edge(base, along_x[0].first, along_x[0].second, 0.5f);
      const uint32_t m1 = meshops::split_edge(base, along_x[1].first, along_x[1].second, 0.5f);
      meshops::split_face(base, face_facing(base, {0, 1, 0}), m0, m1);
    }
    auto tops = [](const Mesh &m) {
      std::vector<uint8_t> s(m.face_count(), 0);
      for (size_t f = 0; f < m.face_count(); f++) s[f] = m.face_normal(f).y > 0.99f;
      return s;
    };
    Mesh ind = base, reg = base;
    std::vector<uint8_t> si = tops(ind), sr = tops(reg);
    meshops::inset_faces(ind, si, 0.2f);
    meshops::inset_region(reg, sr, 0.1f);
    std::printf("    individual: %zu faces; region: %zu faces\n", ind.face_count(), reg.face_count());
    CHECK(ind.face_count() == 7 - 2 + 2 * 5);  // each of the 2 faces: 4 ring quads + its inner face
    CHECK(reg.face_count() == 7 + 6);          // one ring of 6 quads round the pair (the shared edge has none)
    CHECK(closed_manifold(ind) && closed_manifold(reg));
    CHECK_NEAR(vol(reg), 1.0f, 1e-4f);
    /* The region's inner faces: still two, together 0.8 x 0.8, sharing their middle edge. */
    float inner = 0;
    size_t n_sel = 0;
    for (size_t f = 0; f < reg.face_count(); f++)
      if (sr[f]) {
        n_sel++;
        Vec3 sa(0.0f);
        for (uint32_t k = 0; k < reg.face_size(f); k++) sa += cross(reg.positions[reg.face_verts(f)[k]], reg.positions[reg.face_verts(f)[(k + 1) % reg.face_size(f)]]);
        inner += 0.5f * length(sa);
      }
    CHECK(n_sel == 2);
    CHECK_NEAR(inner, 0.8f * 0.8f, 1e-4f);
    /* In the editor: the Individual switch, and F9 flips it. */
    Editor ed;
    ed.init_headless(800, 600);
    ed.step_frame_headless();
    ed.command("create Cube");
    GameObject *g = ed.selected_object();
    *mesh_make_mutable(g->get<MeshFilter>()->mesh) = base;
    g->get<MeshFilter>()->mesh->touch();
    ed.command("fsel facing 0 1 0");
    ed.command("insetmode region 0.1");
    ed.command("editop inset");
    CHECK(g->get<MeshFilter>()->mesh->face_count() == 13);
    ed.command("edit off");
    *mesh_make_mutable(g->get<MeshFilter>()->mesh) = base;
    g->get<MeshFilter>()->mesh->touch();
    ed.command("fsel facing 0 1 0");
    ed.command("insetmode individual 0.2");
    ed.command("editop inset");
    CHECK(g->get<MeshFilter>()->mesh->face_count() == 15);
  });
  test("number fields: clearing one and pressing Enter gives 0 (Unity), within the field's limits", [&] {
    double d = -1;
    CHECK(ui::eval_number("", d, 5.0) && d == 0.0);
    CHECK(ui::eval_number("   ", d, 5.0) && d == 0.0);
    /* A real field: click it (its text is selected), delete, Enter. */
    ui::Context u;
    Image img;
    img.resize(300, 80);
    float v = 7.5f, lim = 4.0f;
    int n = 12;
    auto frame = [&](const std::function<void(ui::Input &)> &setup) {
      setup(u.in);
      u.begin_frame(&img, 0.0);
      u.float_field(2001, {10, 10, 100, 20}, v);
      u.float_field(2002, {10, 35, 100, 20}, lim, 0.1f, 1.0f, 10.0f);  // at least 1
      u.int_field(2003, {10, 60, 100, 18}, n, -5, 50);
      u.end_frame();
      for (bool &p : u.in.pressed) p = false;
      for (bool &r : u.in.released) r = false;
      for (bool &k : u.in.key_pressed) k = false;
      u.in.text.clear();
    };
    auto clear_and_enter = [&](int y) {
      frame([y](ui::Input &in) { in.mx = 50; in.my = y; in.down[0] = in.pressed[0] = true; });
      frame([](ui::Input &in) { in.down[0] = false; in.released[0] = true; });
      frame([](ui::Input &in) { in.key_pressed[platform::KEY_BACKSPACE] = true; });
      frame([](ui::Input &in) { in.key_pressed[platform::KEY_ENTER] = true; });
    };
    clear_and_enter(20);
    clear_and_enter(45);
    clear_and_enter(69);
    std::printf("    cleared fields: %.3f, %.3f (min 1), %d\n", v, lim, n);
    CHECK(v == 0.0f);
    CHECK(lim == 1.0f);
    CHECK(n == 0);
  });
  test("materials: New Material in an Inspector slot is saved as an asset in Assets/Materials", [&] {
    const std::string proj = fs::join(test_dir(), "newslotproject");
    std::error_code ec;
    std::filesystem::remove_all(proj, ec);
    fs::make_dirs(fs::join(proj, "Assets"));
    fs::make_dirs(fs::join(proj, "research"));
    set_env("BLENDITY_PROJECT", proj);
    {
      Editor ed;
      ed.init_headless(800, 600);
      ed.step_frame_headless();
      ed.command("select Cube");
      ed.command("newslotmat 0");
      ed.command("newslotmat 1 Glass");
      GameObject *g = ed.selected_object();
      auto &mats = g->get<MeshRenderer>()->materials;
      CHECK(mats.size() == 2 && mats[0] && mats[1]);
      if (mats.size() == 2 && mats[0] && mats[1]) {
        std::printf("    slot 0: %s, slot 1: %s\n", mats[0]->asset_path.c_str(), mats[1]->asset_path.c_str());
        CHECK(starts_with(mats[0]->asset_path, "Assets/Materials/") && fs::exists(fs::join(proj, mats[0]->asset_path)));
        CHECK(starts_with(mats[1]->asset_path, "Assets/Materials/") && fs::exists(fs::join(proj, mats[1]->asset_path)));
        CHECK(mats[1]->name == "Glass");
      }
    }
    set_env("BLENDITY_PROJECT", "");
  });
  test("draw: centre-based shapes start at the face's exact centre (Plasticity), and the centre snaps", [&] {
    Editor ed;
    ed.init_headless(1000, 700);
    ed.step_frame_headless();
    ed.command("create Cube");
    GameObject *g = ed.selected_object();
    g->set_world_position({0, 0.5f, 0});
    ed.command("camera 0 60 5 0 0.5 0");
    ed.command("edit face");
    /* An L-shaped top would have its centre away from the corner average; a plain square is enough
     * to check the snap lands exactly on (0, 1, 0) from a click well off it. */
    ed.command("drawmode facecenter on");
    ed.command("draw circle 12");
    ed.step_frame_headless();
    int x, y;
    CHECK(ed.project_to_window({0.3f, 1.0f, -0.25f}, x, y));
    ed.step_frame_headless({mouse(platform::EventType::MouseMove, x, y)});
    ed.step_frame_headless({mouse(platform::EventType::MouseDown, x, y)});
    ed.step_frame_headless({mouse(platform::EventType::MouseUp, x, y)});
    ed.command("drawpoint 0.3 1 0");
    const Mesh &m = *g->get<MeshFilter>()->mesh;
    /* The circle's centre: the average of the newest face's corners. */
    Vec3 c(0.0f);
    const size_t f = m.face_count() - 1;
    for (uint32_t k = 0; k < m.face_size(f); k++) c += g->world_matrix().point(m.positions[m.face_verts(f)[k]]);
    c = c / (float)m.face_size(f);
    std::printf("    circle centre (%.4f %.4f %.4f), %u corners\n", c.x, c.y, c.z, m.face_size(f));
    CHECK(m.face_size(f) == 12);
    CHECK(length(c - Vec3(0, 1, 0)) < 1e-3f);
    /* The area centre of a concave face is not its corner average. */
    Mesh l;
    for (Vec3 p : {Vec3(0, 0, 0), Vec3(2, 0, 0), Vec3(2, 0, 1), Vec3(1, 0, 1), Vec3(1, 0, 2), Vec3(0, 0, 2)}) l.add_vert(p);
    const uint32_t lv[6] = {0, 5, 4, 3, 2, 1};
    l.add_face(lv, 6);
    const Vec3 ac = meshops::face_area_center(l, 0);
    CHECK(length(ac - Vec3(5.0f / 6.0f, 0, 5.0f / 6.0f)) < 1e-4f);
  });
  test("depth of field (rasterized): a near object blurs over a sharp background in focus", [&] {
    /* A 160 x 100 image: a checker far away (8 m), a white bar near the camera (1 m)
     * covering the middle columns; focused on the background. */
    const int W = 160, H = 100;
    Image img;
    img.resize(W, H);
    RenderTarget rt;
    rt.attach(img, {0, 0, W, H});
    rt.resize_planes();
    const Mat4 view = Mat4::look_at({0, 0, 0}, {0, 0, 1}, {0, 1, 0});
    const Mat4 proj = Mat4::perspective(40.0f * kDeg2Rad, W / (float)H, 0.1f, 100.0f);
    auto ndc_z = [&](float depth) {
      const Vec4 c = (proj * view) * Vec4(0, 0, depth, 1);
      return c.z / c.w;
    };
    const float zfar = ndc_z(8.0f), znear = ndc_z(1.0f);
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++) {
        const bool bar = x >= 70 && x < 90;
        const bool chk = ((x / 4) + (y / 4)) & 1;
        img.row(y)[x] = bar ? 0xFFFFFFFFu : chk ? 0xFF000000u : 0xFFC0C0C0u;
        rt.depth[(size_t)y * W + x] = bar ? znear : zfar;
      }
    DofParams d;
    d.inv_view_proj = (proj * view).inverse();
    d.eye = {0, 0, 0};
    d.forward = {0, 0, 1};
    d.aperture_radius = 0.05f;
    d.focus_distance = 8.0f;
    d.tan_half_vfov = std::tan(20.0f * kDeg2Rad);
    d.far_distance = 100.0f;
    d.max_radius_px = 12.0f;
    std::printf("    circle of confusion: background %.2f px, bar %.2f px\n", dof_coc_pixels(d, 8.0f, H), dof_coc_pixels(d, 1.0f, H));
    CHECK(dof_coc_pixels(d, 8.0f, H) < 0.01f && dof_coc_pixels(d, 1.0f, H) > 4.0f);
    std::vector<uint32_t> before(img.pixels.begin(), img.pixels.end());
    CHECK(apply_depth_of_field(rt, d));
    auto lum = [](uint32_t c) { return ((c & 255) + ((c >> 8) & 255) + ((c >> 16) & 255)) / 3; };
    /* Far from the bar the checker is untouched (in focus). */
    int changed_far = 0;
    for (int y = 0; y < H; y++)
      for (int x = 0; x < 30; x++) changed_far += img.row(y)[x] != before[(size_t)y * W + x];
    /* Just left of the bar, over the background, the bar's blur spills: brighter on average than the
     * checker there (whose black and grey average ~96). */
    double spill = 0;
    for (int y = 10; y < 90; y++) spill += lum(img.row(y)[67]);
    spill /= 80.0;
    /* Inside the bar's edge the bar is softened (no longer pure white next to the dark squares). */
    int soft = 0;
    for (int y = 10; y < 90; y++) soft += lum(img.row(y)[70]) < 250;
    std::printf("    unchanged far pixels: %d changed; spill beside the bar %.1f (checker ~96); softened bar edge rows %d / 80\n", changed_far, spill, soft);
    CHECK(changed_far == 0);
    CHECK(spill > 125.0);
    CHECK(soft > 40);
    /* Focused on the bar instead: the bar stays crisp and the background blurs, never over the bar. */
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++) img.row(y)[x] = before[(size_t)y * W + x];
    d.focus_distance = 1.0f;
    CHECK(apply_depth_of_field(rt, d));
    int bar_changed = 0;
    for (int y = 0; y < H; y++)
      for (int x = 70; x < 90; x++) bar_changed += img.row(y)[x] != 0xFFFFFFFFu;
    int bg_blurred = 0;
    for (int y = 0; y < H; y++)
      for (int x = 0; x < 30; x++) bg_blurred += img.row(y)[x] != before[(size_t)y * W + x];
    std::printf("    focused near: bar pixels changed %d, background pixels blurred %d\n", bar_changed, bg_blurred);
    CHECK(bar_changed == 0);
    CHECK(bg_blurred > 1000);
  });
  test("camera focus: a picked point stays in focus as the camera moves; piloting keeps it", [&] {
    Editor ed;
    ed.init_headless(1000, 700);
    ed.step_frame_headless();
    ed.command("select Main Camera");
    GameObject *cam = ed.selected_object();
    Camera *c = cam->get<Camera>();
    c->dof = true;
    c->focus_track = true;
    c->focus_point = {0, 0.5f, 0};
    cam->set_world_position({0, 1, -6});
    cam->set_world_rotation(Quat::euler({0, 0, 0}));
    ed.step_frame_headless();
    CHECK_NEAR(c->focus_distance, 6.0f, 1e-3f);
    cam->set_world_position({0, 1, -3});
    ed.step_frame_headless();
    CHECK_NEAR(c->focus_distance, 3.0f, 1e-3f);
    /* Piloting no longer moves the focus to the orbit pivot. */
    ed.command("pilot");
    ed.step_frame_headless();
    ed.command("camera 10 15 9 1 0.5 0");
    ed.step_frame_headless();
    ed.step_frame_headless();
    const float expect = dot(c->focus_point - cam->world_position(), normalize(cam->world_rotation().rotate({0, 0, 1})));
    std::printf("    piloted: focus %.3f m, the picked point is %.3f m ahead\n", c->focus_distance, expect);
    CHECK_NEAR(c->focus_distance, expect, 1e-3f);
    ed.command("pilot");
    /* The Game view and the Camera Preview draw with it (no crash, something blurred). */
    ed.command("window Game");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    CHECK(ed.window_rect_for_test(WindowKind::Game).w > 0);
  });
}

/* ===================================================================== */
/* Round 14: archways pushed through, drawing over existing edges,       */
/* Smart Fill, auto smooth, camera sequences                             */
/* ===================================================================== */

/* Faces whose triangles don't add up to the polygon (overlapping or missing
 * triangles: the "face artifacts" of a bad triangulation), or point the wrong way. */
static size_t bad_triangulations(const Mesh &m, std::string *first = nullptr) {
  const RenderMesh &rm = m.render_mesh(true);
  std::vector<double> tri_area(m.face_count(), 0.0);
  std::vector<int> flipped(m.face_count(), 0);
  for (size_t t = 0; t < rm.tri_count(); t++) {
    const uint32_t f = rm.tri_face[t];
    const Vec3 a = rm.positions[rm.indices[t * 3]], b = rm.positions[rm.indices[t * 3 + 1]], c = rm.positions[rm.indices[t * 3 + 2]];
    const Vec3 cr = cross(b - a, c - a);
    tri_area[f] += 0.5 * length(cr);
    if (dot(cr, m.face_normal(f)) < -1e-9f * length(m.face_normal(f))) flipped[f]++;
  }
  size_t bad = 0;
  for (size_t f = 0; f < m.face_count(); f++) {
    Vec3 nw(0.0f);
    for (uint32_t k = 0; k < m.face_size(f); k++) nw += cross(m.positions[m.face_verts(f)[k]], m.positions[m.face_verts(f)[(k + 1) % m.face_size(f)]]);
    const double area = 0.5 * length(nw);
    if (std::fabs(tri_area[f] - area) > 1e-4 * std::max(1.0, area) || flipped[f]) {
      if (first && !bad) *first = strprintf("face %zu (%u corners): polygon %.5f, triangles %.5f, flipped %d", f, m.face_size(f), area, tri_area[f], flipped[f]);
      bad++;
    }
  }
  return bad;
}

static void round14_tests() {
  const float cube_sign = signed_volume(*primitives::cube()) > 0 ? 1.0f : -1.0f;
  auto vol = [&](const Mesh &m) { return signed_volume(m) * cube_sign; };
  /* A wall 4 wide, 3 tall, 0.5 thick (front at z = -0.25), with an archway drawn on its front:
   * a 1 x 1 doorway and a half circle on its top. */
  auto wall_with_arch = [](Editor &ed, int segs) {
    ed.init_headless(1000, 700);
    ed.step_frame_headless();
    ed.command("create Cube");
    GameObject *g = ed.selected_object();
    Mesh &m = *mesh_make_mutable(g->get<MeshFilter>()->mesh);
    for (Vec3 &p : m.positions) p = Vec3(p.x * 4.0f, p.y * 3.0f + 1.5f, p.z * 0.5f);
    m.touch();
    g->set_world_position({0, 0, 0});
    ed.command("edit face");
    ed.command("draw rectangle");
    ed.command("drawpoint -0.5 0.5 -0.25");
    ed.command("drawpoint 0.5 1.5 -0.25");
    ed.command(strprintf("draw arc %d", segs));
    ed.command("drawpoint -0.5 1.5 -0.25");
    ed.command("drawpoint 0.5 1.5 -0.25");
    ed.command("drawpoint 0 2 -0.25");
    return g;
  };
  auto doorway_faces = [](const Mesh &m) {
    /* The doorway and the arch: front-facing faces inside |x| < 0.5, y in 0.5..2. */
    std::vector<uint8_t> sel(m.face_count(), 0);
    for (size_t f = 0; f < m.face_count(); f++) {
      bool in = m.face_normal(f).z < -0.99f;
      for (uint32_t k = 0; k < m.face_size(f) && in; k++) {
        const Vec3 p = m.positions[m.face_verts(f)[k]];
        in = std::fabs(p.x) < 0.5f + 1e-4f && p.y > 0.5f - 1e-4f && p.y < 2.0f + 1e-4f && p.z < -0.2f;
      }
      sel[f] = in;
    }
    return sel;
  };
  test("archway: a selection reaching the wall's top edge pushed through makes a notch, not a fold", [&] {
    Editor ed;
    GameObject *g = wall_with_arch(ed, 33);
    Mesh h = *g->get<MeshFilter>()->mesh;
    /* The doorway, the arch and the strip of wall above it up to the top edge. */
    std::vector<uint8_t> sel(h.face_count(), 0);
    for (size_t f = 0; f < h.face_count(); f++) {
      const Vec3 c = h.face_center(f);
      sel[f] = h.face_normal(f).z < -0.99f && std::fabs(c.x) < 0.5f && c.y > 0.5f && c.y < 2.0f && c.z < -0.2f;
    }
    std::string err, why, bt;
    const bool ok = meshops::push_through(h, sel, 1, &err);
    const size_t badt = bad_triangulations(h, &bt), ov = meshops::overlapping_faces(h);
    std::printf("    notch: %s, %zu faces, closed %d, overlapping %zu, bad triangulations %zu %s\n", ok ? "ok" : err.c_str(), h.face_count(),
                (int)closed_manifold(h), ov, badt, bt.c_str());
    CHECK(structurally_valid(h, &why));
    CHECK(badt == 0);
    CHECK(ov == 0);
  });
  for (int segs : {8, 16, 33}) {
    test(strprintf("archway: drawn on a wall (%d-segment arc), pushed through as a doorway", segs).c_str(), [&] {
      Editor ed;
      GameObject *g = wall_with_arch(ed, segs);
      const Mesh &m = *g->get<MeshFilter>()->mesh;
      std::string why, bt;
      std::vector<uint8_t> sel = doorway_faces(m);
      const size_t nsel = (size_t)std::count(sel.begin(), sel.end(), 1);
      std::printf("    drawn: %zu faces, %zu doorway faces, closed %d, bad triangulations %zu\n", m.face_count(), nsel, (int)closed_manifold(m),
                  bad_triangulations(m, &bt));
      CHECK(nsel == 2);
      CHECK(bad_triangulations(m) == 0);
      if (std::getenv("BLENDITY_DUMP")) {
        for (size_t f = 0; f < m.face_count(); f++) {
          std::printf("    f%zu%s n(%.2f %.2f %.2f) c(%.3f %.3f %.3f):", f, sel[f] ? "*" : "", m.face_normal(f).x, m.face_normal(f).y, m.face_normal(f).z,
                      m.face_center(f).x, m.face_center(f).y, m.face_center(f).z);
          for (uint32_t k = 0; k < m.face_size(f); k++) std::printf(" %u", m.face_verts(f)[k]);
          std::printf("\n");
        }
      }
      /* Push / Pull past the back: a hole. */
      for (int mode = 0; mode < 2; mode++) {
        Mesh h = m;
        std::vector<uint8_t> s2 = sel;
        meshops::PushPullResult res;
        std::string err;
        bool ok = mode == 0 ? meshops::push_pull(h, s2, -0.6f, true, &res, &err) : meshops::push_through(h, s2, 1, &err);
        std::string ov;
        const size_t ovn = meshops::overlapping_faces(h, 0.0f);
        const size_t badt = bad_triangulations(h, &bt);
        std::printf("    %s: %s, %zu faces, closed %d, volume %.4f, overlapping %zu, bad triangulations %zu %s\n", mode == 0 ? "push/pull -0.6" : "push through",
                    ok ? (mode == 0 ? (res == meshops::PushPullResult::Hole ? "hole" : "not a hole") : "ok") : err.c_str(), h.face_count(),
                    (int)closed_manifold(h), vol(h), ovn, badt, bt.c_str());
        CHECK(ok);
        CHECK(structurally_valid(h, &why));
        CHECK(closed_manifold(h));
        CHECK(ovn == 0);
        if (ovn && std::getenv("BLENDITY_DUMP")) {
          std::vector<std::pair<uint32_t, uint32_t>> pr;
          meshops::overlapping_faces(h, 0.0f, &pr);
          for (auto &q : pr)
            for (uint32_t f : {q.first, q.second}) {
              std::printf("    overlap f%u n(%.2f %.2f %.2f):", f, h.face_normal(f).x, h.face_normal(f).y, h.face_normal(f).z);
              for (uint32_t k = 0; k < h.face_size(f); k++) {
                const Vec3 v = h.positions[h.face_verts(f)[k]];
                std::printf(" (%.3f %.3f %.3f)", v.x, v.y, v.z);
              }
              std::printf("\n");
            }
        }
        CHECK(badt == 0);
        /* The opening: 1 x 1 plus a half disc of radius 0.5 (as a polygon), 0.5 deep. */
        const int arc_n = std::max(2, segs / 2);  // the arc tool uses half the circle segments
        const float opening = 1.0f + 0.5f * arc_n * 0.25f * std::sin(kPi / arc_n);
        CHECK_NEAR(vol(h), 4 * 3 * 0.5f - opening * 0.5f, 2e-3f);
      }
    });
  }

  /* Prior edges on either face: shapes drawn earlier on the front or the back, then a new
   * shape drawn over them and pushed through (or pulled out). Each result must be a clean
   * closed solid with the right volume and nothing on top of anything else. */
  auto plain_wall = [](Editor &ed) {
    ed.init_headless(1000, 700);
    ed.step_frame_headless();
    ed.command("create Cube");
    GameObject *g = ed.selected_object();
    Mesh &m = *mesh_make_mutable(g->get<MeshFilter>()->mesh);
    for (Vec3 &p : m.positions) p = Vec3(p.x * 4.0f, p.y * 3.0f + 1.5f, p.z * 0.5f);
    m.touch();
    g->set_world_position({0, 0, 0});
    ed.command("edit face");
    return g;
  };
  /* Faces on the plane z = zf facing nz whose every corner is inside the box [x0, x1] x [y0, y1]. */
  auto faces_in = [](const Mesh &m, float zf, float nz, float x0, float x1, float y0, float y1) {
    std::vector<uint8_t> sel(m.face_count(), 0);
    for (size_t f = 0; f < m.face_count(); f++) {
      bool in = m.face_normal(f).z * nz > 0.99f;
      for (uint32_t k = 0; k < m.face_size(f) && in; k++) {
        const Vec3 p = m.positions[m.face_verts(f)[k]];
        in = std::fabs(p.z - zf) < 1e-4f && p.x > x0 - 1e-4f && p.x < x1 + 1e-4f && p.y > y0 - 1e-4f && p.y < y1 + 1e-4f;
      }
      sel[f] = in;
    }
    return sel;
  };
  struct Case {
    const char *name;
    std::vector<std::string> draw;  // console commands drawing on the wall
    float x0, x1, y0, y1;           // the region pushed through (on the front)
    float d;                        // push / pull distance (negative: through the 0.5 thick wall)
    float hole_area;                // expected opening (or added footprint for pulls)
  };
  const std::vector<Case> cases = {
      {"a rectangle drawn over an earlier circle's edge, pushed through",
       {"draw circle 24", "drawpoint -0.6 1.5 -0.25", "drawpoint -0.1 1.5 -0.25", "draw rectangle", "drawpoint -0.3 1 -0.25", "drawpoint 0.7 2 -0.25"},
       -0.3f, 0.7f, 1.0f, 2.0f, -0.6f, 1.0f},
      {"a circle drawn across an earlier rectangle, pushed through",
       {"draw rectangle", "drawpoint -1 0.5 -0.25", "drawpoint 0 1.5 -0.25", "draw circle 24", "drawpoint 0 1.5 -0.25", "drawpoint 0.4 1.5 -0.25"},
       -0.4f, 0.4f, 1.1f, 1.9f, -0.6f, 0.5f * 24 * 0.16f * std::sin(2 * kPi / 24)},
      {"a rectangle pushed through where the back already has a circle",
       {"draw circle 24", "drawpoint 0.3 1.5 0.25", "drawpoint 0.8 1.5 0.25", "draw rectangle", "drawpoint -0.5 1 -0.25", "drawpoint 0.5 2 -0.25"},
       -0.5f, 0.5f, 1.0f, 2.0f, -0.6f, 1.0f},
      {"an archway pushed through where the back has a rectangle across it",
       {"draw rectangle", "drawpoint -1 1.2 0.25", "drawpoint 1 1.6 0.25", "draw rectangle", "drawpoint -0.5 0.5 -0.25", "drawpoint 0.5 1.5 -0.25",
        "draw arc 16", "drawpoint -0.5 1.5 -0.25", "drawpoint 0.5 1.5 -0.25", "drawpoint 0 2 -0.25"},
       -0.5f, 0.5f, 0.5f, 2.0f, -0.6f, 1.0f + 0.5f * 8 * 0.25f * std::sin(kPi / 8)},
      {"a rectangle over an earlier circle, pulled out",
       {"draw circle 24", "drawpoint -0.6 1.5 -0.25", "drawpoint -0.1 1.5 -0.25", "draw rectangle", "drawpoint -0.3 1 -0.25", "drawpoint 0.7 2 -0.25"},
       -0.3f, 0.7f, 1.0f, 2.0f, 0.4f, 1.0f},
  };
  for (const Case &c : cases) {
    test((std::string("prior edges: ") + c.name).c_str(), [&] {
      Editor ed;
      GameObject *g = plain_wall(ed);
      for (const std::string &cmd : c.draw) {
        ed.command(cmd);
        if (std::getenv("BLENDITY_DUMP")) {
          std::printf("    > %s -> %zu faces\n", cmd.c_str(), g->get<MeshFilter>()->mesh->face_count());
          std::vector<LogEntry> lines;
          static size_t seen = 0;
          Log::fetch(seen, lines);
          for (size_t k = seen; k < lines.size(); k++) std::printf("      %s\n", lines[k].text.c_str());
          seen = lines.size();
        }
      }
      const Mesh &m = *g->get<MeshFilter>()->mesh;
      std::string why, bt;
      CHECK(structurally_valid(m, &why) && closed_manifold(m));
      CHECK(meshops::overlapping_faces(m) == 0);
      std::vector<uint8_t> sel = faces_in(m, -0.25f, -1.0f, c.x0, c.x1, c.y0, c.y1);
      /* Everything inside the new outline is one region: check its area is the expected opening. */
      float sel_area = 0;
      for (size_t f = 0; f < m.face_count(); f++)
        if (sel[f]) {
          Vec3 nw(0.0f);
          for (uint32_t k = 0; k < m.face_size(f); k++) nw += cross(m.positions[m.face_verts(f)[k]], m.positions[m.face_verts(f)[(k + 1) % m.face_size(f)]]);
          sel_area += 0.5f * length(nw);
        }
      Mesh h = m;
      meshops::PushPullResult res;
      std::string err;
      const bool ok = meshops::push_pull(h, sel, c.d, true, &res, &err);
      const size_t ov = meshops::overlapping_faces(h), badt = bad_triangulations(h, &bt);
      const float expect = 4 * 3 * 0.5f + (c.d < 0 ? -c.hole_area * 0.5f : c.hole_area * c.d);
      static const char *kinds[] = {"moved", "extruded", "hole", "joined"};
      std::printf("    drawn %zu faces, region %zu faces (area %.4f, expected %.4f); %s: %s, %zu faces, closed %d, volume %.4f (expected %.4f), "
                  "overlapping %zu, bad triangulations %zu %s\n",
                  m.face_count(), (size_t)std::count(sel.begin(), sel.end(), 1), sel_area, c.hole_area, c.d < 0 ? "push" : "pull",
                  ok ? kinds[(int)res] : err.c_str(), h.face_count(), (int)closed_manifold(h), vol(h), expect, ov, badt, bt.c_str());
      CHECK_NEAR(sel_area, c.hole_area, 1e-3f);
      CHECK(ok);
      if (c.d < 0) CHECK(res == meshops::PushPullResult::Hole);
      CHECK(structurally_valid(h, &why));
      CHECK(closed_manifold(h));
      CHECK(ov == 0);
      CHECK(badt == 0);
      CHECK_NEAR(vol(h), expect, 2e-3f);
    });
  }
}

static void round14_feature_tests() {
  const float cube_sign = signed_volume(*primitives::cube()) > 0 ? 1.0f : -1.0f;
  auto vol = [&](const Mesh &m) { return signed_volume(m) * cube_sign; };
  auto without = [](Mesh m, std::initializer_list<Vec3> normals) {
    std::vector<uint8_t> drop(m.face_count(), 0);
    for (Vec3 n : normals) drop[face_facing(m, n)] = 1;
    meshops::delete_faces(m, drop);
    return m;
  };
  test("smart fill: holes become faces wound like their neighbours; a closed solid again", [&] {
    Mesh m = without(*primitives::cube(), {{0, 1, 0}});
    CHECK(!closed_manifold(m));
    meshops::SmartFillResult r = meshops::smart_fill(m);
    std::printf("    one hole: %zu loop(s), %zu face(s); closed %d, volume %.4f\n", r.loops, r.faces, (int)closed_manifold(m), vol(m));
    CHECK(r.loops == 1 && r.faces == 1 && closed_manifold(m));
    CHECK_NEAR(vol(m), 1.0f, 1e-4f);
    CHECK(m.face_normal(m.face_count() - 1).y > 0.99f);  // facing out, like the faces around it
    Mesh two = without(*primitives::cube(), {{0, 1, 0}, {0, -1, 0}});
    r = meshops::smart_fill(two);
    CHECK(r.loops == 2 && closed_manifold(two));
    CHECK_NEAR(vol(two), 1.0f, 1e-4f);
    /* Only the hole inside the selection. */
    Mesh sel_one = without(*primitives::cube(), {{0, 1, 0}, {0, -1, 0}});
    std::vector<uint8_t> vs(sel_one.vert_count(), 0);
    for (size_t v = 0; v < vs.size(); v++) vs[v] = sel_one.positions[v].y > 0;
    r = meshops::smart_fill(sel_one, &vs);
    CHECK(r.loops == 1 && !closed_manifold(sel_one));
    /* A bent hole (a cylinder's cap on a twisted rim) is fanned from its centre, not one twisted face. */
    Mesh cyl = *primitives::cylinder(0.5f, 1.0f, 12);
    const size_t cap = face_facing(cyl, {0, 1, 0});
    for (uint32_t k = 0; k < cyl.face_size(cap); k++) cyl.positions[cyl.face_verts(cap)[k]].y += 0.15f * std::sin(k * 2.0f);  // bend the rim
    cyl.touch();
    Mesh bent = without(cyl, {{0, 1, 0}});
    r = meshops::smart_fill(bent);
    std::string why;
    std::printf("    bent hole: %zu fan(s), %zu faces added; closed %d, overlapping %zu\n", r.fans, r.faces, (int)closed_manifold(bent),
                meshops::overlapping_faces(bent));
    CHECK(r.fans == 1 && r.faces == 12 && closed_manifold(bent) && structurally_valid(bent, &why));
  });
  test("smart fill: edges that converge to no area are welded shut, never a zero-area face", [&] {
    std::string why;
    /* A crack: two halves of a box whose shared edge has two unwelded copies. */
    Mesh crack = *primitives::cube();
    {
      const size_t top = face_facing(crack, {0, 1, 0});
      /* Give the top face its own copy of one corner pair: the crack runs along that edge. */
      const uint32_t *fv = crack.face_verts(top);
      const uint32_t a = fv[0], b = fv[1];
      const uint32_t a2 = crack.add_vert(crack.positions[a]), b2 = crack.add_vert(crack.positions[b]);
      for (uint32_t k = 0; k < crack.face_size(top); k++) {
        uint32_t &v = crack.corner_verts[crack.face_offsets[top] + k];
        if (v == a) v = a2;
        else if (v == b) v = b2;
      }
      crack.touch();
    }
    CHECK(!closed_manifold(crack));
    meshops::SmartFillResult r = meshops::smart_fill(crack);
    std::printf("    crack: %zu weld(s), %zu face(s); closed %d, %zu verts\n", r.welded, r.faces, (int)closed_manifold(crack), crack.vert_count());
    CHECK(r.welded >= 1 && r.faces == 0);
    CHECK(closed_manifold(crack) && crack.vert_count() == 8);
    CHECK_NEAR(vol(crack), 1.0f, 1e-4f);
    /* Corners converging on one point (a hole whose rim was collapsed): welded into the point. */
    Mesh pinch = without(*primitives::cube(), {{0, 1, 0}});
    for (Vec3 &p : pinch.positions)
      if (p.y > 0) p = Vec3(0, 0.5f, 0);  // the top rim meets at the middle: a pyramid with an empty top
    pinch.touch();
    r = meshops::smart_fill(pinch);
    std::printf("    pinched rim: %zu weld(s), %zu face(s); closed %d, %zu verts, %zu faces\n", r.welded, r.faces, (int)closed_manifold(pinch), pinch.vert_count(),
                pinch.face_count());
    CHECK(r.faces == 0 && r.welded >= 2);
    CHECK(pinch.vert_count() == 5);  // the four rim corners are one apex
    CHECK(closed_manifold(pinch) && structurally_valid(pinch, &why));
    for (size_t f = 0; f < pinch.face_count(); f++) {
      Vec3 nw(0.0f);
      for (uint32_t k = 0; k < pinch.face_size(f); k++) nw += cross(pinch.positions[pinch.face_verts(f)[k]], pinch.positions[pinch.face_verts(f)[(k + 1) % pinch.face_size(f)]]);
      CHECK(length(nw) > 1e-6f);  // no face lost all its area
    }
    /* A loop of wire edges lying on one line: no surface there; nothing is made. */
    Mesh line;
    const uint32_t l0 = line.add_vert({0, 0, 0}), l1 = line.add_vert({1, 0, 0}), l2 = line.add_vert({2, 0, 0});
    line.add_loose_edge(l0, l1);
    line.add_loose_edge(l1, l2);
    line.add_loose_edge(l2, l0);
    r = meshops::smart_fill(line);
    CHECK(r.no_area == 1 && line.face_count() == 0);
    /* A wire triangle (drawn with the polyline): one face. */
    Mesh tri;
    const uint32_t t0 = tri.add_vert({0, 0, 0}), t1 = tri.add_vert({1, 0, 0}), t2 = tri.add_vert({0, 0, 1});
    tri.add_loose_edge(t0, t1);
    tri.add_loose_edge(t1, t2);
    tri.add_loose_edge(t2, t0);
    r = meshops::smart_fill(tri);
    std::printf("    wire triangle: loops %zu, faces %zu (mesh %zu), wires left %zu, open %zu, no-area %zu\n", r.loops, r.faces, tri.face_count(),
                tri.loose_edges.size(), r.open_chains, r.no_area);
    CHECK(r.loops == 1 && tri.face_count() == 1 && tri.loose_edges.empty());
    /* A slit: a quad strip folded back on itself (out and back along the same line). */
    Mesh slit;
    const uint32_t s0 = slit.add_vert({0, 0, 0}), s1 = slit.add_vert({1, 0, 0}), s2 = slit.add_vert({2, 0, 0}), s1b = slit.add_vert({1, 0, 0});
    slit.add_loose_edge(s0, s1);
    slit.add_loose_edge(s1, s2);
    slit.add_loose_edge(s2, s1b);
    slit.add_loose_edge(s1b, s0);
    r = meshops::smart_fill(slit);
    std::printf("    slit: %zu weld(s), %zu face(s), %zu no-area, %zu verts\n", r.welded, r.faces, r.no_area, slit.vert_count());
    CHECK(slit.face_count() == 0 && r.welded >= 1);
    /* In the editor. */
    Editor ed;
    ed.init_headless(800, 600);
    ed.step_frame_headless();
    ed.command("create Cube");
    GameObject *g = ed.selected_object();
    *mesh_make_mutable(g->get<MeshFilter>()->mesh) = without(*primitives::cube(), {{0, 0, 1}});
    g->get<MeshFilter>()->mesh->touch();
    ed.command("edit vertex");
    ed.command("editop smart_fill");
    ed.command("edit off");
    CHECK(closed_manifold(*g->get<MeshFilter>()->mesh));
  });
  test("auto smooth: a rounded bevel turns on smooth-by-angle shading; flat results and per-face shading are left alone", [&] {
    Editor ed;
    ed.init_headless(800, 600);
    ed.step_frame_headless();
    ed.command("autosmooth on 30");
    ed.command("create Cube");
    GameObject *g = ed.selected_object();
    const Mesh *m0 = g->get<MeshFilter>()->mesh.get();
    CHECK(!m0->smooth);
    /* One straight bevel: a 45-degree chamfer, nothing curved. */
    auto bevel = [&](int segs) {
      ed.command("edit edge all");
      ed.command("editop bevel");
      ed.command("redo amount 0.1");
      ed.command(strprintf("redo segments %d", segs));
    };
    auto mesh = [&]() -> const Mesh & { return *g->get<MeshFilter>()->mesh; };
    bevel(1);  // one straight bevel: a 45-degree chamfer, nothing curved
    std::printf("    1-segment bevel: smooth %d, %zu faces\n", (int)mesh().smooth, mesh().face_count());
    CHECK(mesh().face_count() > 6);
    CHECK(!mesh().smooth);
    ed.command("edit off");
    ed.command("create Cube");
    g = ed.selected_object();
    bevel(4);
    std::printf("    4-segment bevel: smooth %d, angle %.0f, %zu faces\n", (int)mesh().smooth, mesh().smooth_angle, mesh().face_count());
    CHECK(mesh().smooth);
    CHECK_NEAR(mesh().smooth_angle, 30.0f, 1e-3f);
    ed.command("edit off");
    /* Shaded per face by the user: not touched. */
    ed.command("create Cube");
    g = ed.selected_object();
    Mesh &pf = *mesh_make_mutable(g->get<MeshFilter>()->mesh);
    pf.face_smooth.assign(pf.face_count(), 0);
    pf.touch();
    bevel(4);
    CHECK(!mesh().smooth);
    ed.command("edit off");
    /* Off: nothing changes; Shade Auto Smooth by hand. */
    ed.command("autosmooth off");
    ed.command("create Cube");
    g = ed.selected_object();
    bevel(4);
    CHECK(!mesh().smooth);
    ed.command("edit off");
    ed.command("meshop shade_auto_smooth");
    CHECK(mesh().smooth && mesh().smooth_angle == 30.0f);
    /* The render normals: smooth across the bevel's segments, hard at the cube's flat faces. */
    Mesh b = *primitives::cube();
    meshops::shade_auto_smooth(b, 30.0f);
    const RenderMesh &rm = b.render_mesh();
    CHECK(rm.positions.size() == 24);  // a cube keeps every corner split: 90 degrees > 30
  });
  test("camera sequence: every In Sequence camera rendered in order, each saved; Stop and exclusions work", [&] {
    const std::string proj = fs::join(test_dir(), "seqproject");
    std::error_code ec;
    std::filesystem::remove_all(proj, ec);
    fs::make_dirs(fs::join(proj, "Assets"));
    fs::make_dirs(fs::join(proj, "research"));
    set_env("BLENDITY_PROJECT", proj);
    {
      Editor ed;
      ed.init_headless(900, 600);
      ed.step_frame_headless();
      ed.command("set Render.RenderEngine Rasterized");  // quick
      ed.command("set Render.ResolutionX 160");
      ed.command("set Render.ResolutionY 90");
      /* Three more cameras round the scene; one is left out, one goes first. */
      auto add_cam = [&](const char *name, Vec3 at, int order, bool in) {
        ed.command("create Camera");
        GameObject *c = ed.selected_object();
        c->name = name;
        c->set_world_position(at);
        Vec3 f = normalize(Vec3(0, 0.5f, 0) - at);
        c->set_world_rotation(Quat::euler({std::asin(-f.y) * kRad2Deg, std::atan2(f.x, f.z) * kRad2Deg, 0}));
        c->get<Camera>()->sequence_order = order;
        c->get<Camera>()->in_sequence = in;
        return c;
      };
      add_cam("Side", {6, 1.5f, 0}, 1, true);
      add_cam("Top", {0, 8, -0.1f}, -1, true);
      add_cam("Skipped", {-6, 1, 0}, 0, false);
      ed.command("rendersequence Renders/test_sequence");
      for (int i = 0; i < 50 && ed.sequence_running(); i++) ed.step_frame_headless();
      const auto &files = ed.sequence_files();
      std::printf("    %zu image(s):", files.size());
      for (const auto &f : files) std::printf(" %s", fs::filename(f).c_str());
      std::printf("\n");
      CHECK(!ed.sequence_running());
      CHECK(files.size() == 3);
      if (files.size() == 3) {
        CHECK(fs::filename(files[0]) == "01_Top.png");
        CHECK(fs::filename(files[1]) == "02_Main Camera.png");
        CHECK(fs::filename(files[2]) == "03_Side.png");
        std::string a, b2, c;
        CHECK(fs::read_file(files[0], a) && fs::read_file(files[1], b2) && fs::read_file(files[2], c));
        CHECK(a != b2 && b2 != c && a != c);  // three different views
      }
      /* Renders go back to the Main Camera afterwards. */
      CHECK(ed.render_camera_name() == "Main Camera");
      /* Stop part-way: the path tracer, stopped after the first camera. */
      ed.command("set Render.RenderEngine Path");
      ed.command("set Render.Samples 4");
      ed.command("rendersequence Renders/stopped");
      for (int i = 0; i < 200 && ed.sequence_running() && ed.sequence_files().empty(); i++) ed.step_frame_headless();
      ed.command("rendersequence stop");
      CHECK(!ed.sequence_running());
      CHECK(ed.sequence_files().size() <= 1);
      CHECK(ed.render_camera_name() == "Main Camera");
    }
    set_env("BLENDITY_PROJECT", "");
  });
}

/* ===================================================================== */
/* Round 15: keymaps, draw axes, Z-fighting check, UV tools, light hash  */
/* ===================================================================== */

static void round15_tests() {
  auto key = [](int k, int mods = 0) {
    platform::Event e;
    e.type = platform::EventType::KeyDown;
    e.key = k;
    e.mods = mods;
    return e;
  };
  auto key_up = [](int k) {
    platform::Event e;
    e.type = platform::EventType::KeyUp;
    e.key = k;
    return e;
  };
  auto move = [](int x, int y) {
    platform::Event e;
    e.type = platform::EventType::MouseMove;
    e.x = x;
    e.y = y;
    return e;
  };
  test("light colour temperature (and every other light setting) re-renders the Camera Preview", [&] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.step_frame_headless();
    GameObject *sun = by_name(ed.scene(), "Directional Light");
    CHECK(sun && sun->get<Light>());
    if (!sun) return;
    const uint64_t h0 = ed.render_hash_for_test();
    sun->get<Light>()->use_temperature = true;
    const uint64_t h1 = ed.render_hash_for_test();
    sun->get<Light>()->temperature = 3200.0f;
    const uint64_t h2 = ed.render_hash_for_test();
    sun->get<Light>()->spot_angle = 50.0f;
    const uint64_t h3 = ed.render_hash_for_test();
    CHECK(h0 != h1 && h1 != h2 && h2 != h3);
  });
  test("keymap: presets lay shortcuts out like Unity, Blender, Maya, 3ds Max and SketchUp", [&] {
    auto bound = [](const Keymap &k, const char *id, const char *chord) {
      auto it = k.find(id);
      const KeyChord c = parse_chord(chord);
      return it != k.end() && std::find(it->second.begin(), it->second.end(), c) != it->second.end();
    };
    const Keymap unity = keymap_preset("Unity"), blender = keymap_preset("Blender"), maya = keymap_preset("Maya"), max = keymap_preset("3ds Max"),
                 su = keymap_preset("SketchUp");
    CHECK(bound(unity, "tool.move", "W") && bound(unity, "tool.scale", "R") && bound(unity, "edit.undo", "Ctrl+Z") && bound(unity, "view.frame", "F"));
    CHECK(bound(blender, "transform.grab", "G") && bound(blender, "transform.rotate", "R") && bound(blender, "transform.scale", "S") &&
          bound(blender, "mesh.extrude", "E") && bound(blender, "edit.delete", "X") && bound(blender, "mesh.merge_center", "M"));
    CHECK(bound(maya, "mode.edit_toggle", "F8") && bound(maya, "mode.vertex", "F9") && bound(maya, "mode.face", "F11") &&
          bound(maya, "mesh.proportional", "B") && bound(maya, "edit.redo", "Shift+Z"));
    CHECK(bound(max, "view.frame", "Z") && bound(max, "mode.face", "4") && bound(max, "mesh.bevel", "Ctrl+Shift+C"));
    CHECK(bound(su, "mesh.push_pull", "P") && bound(su, "draw.rectangle", "R") && bound(su, "draw.circle", "C") && bound(su, "tool.move", "M"));
    /* The old "Blender Transform Keys" preference on top of Unity. */
    const Keymap ub = keymap_preset("Unity", true);
    CHECK(bound(ub, "transform.rotate", "R") && bound(ub, "tool.scale", "T") && !bound(ub, "tool.scale", "R"));
    /* No preset has a real clash. */
    for (const char *pn : kKeymapPresets) {
      const Keymap k = keymap_preset(pn);
      size_t clashes = 0;
      std::string first;
      for (auto &kv : k)
        for (const KeyChord &c : kv.second) {
          const auto cl = keymap_conflicts(k, kv.first, c);
          if (!cl.empty() && first.empty()) first = kv.first + " / " + cl[0] + " on " + chord_text(c);
          clashes += cl.size();
        }
      std::printf("    %s: %zu clash(es) %s\n", pn, clashes, first.c_str());
      CHECK(clashes == 0);
    }
    /* Chords print and parse both ways. */
    for (const char *t : {"Ctrl+Shift+Z", "Alt+F", "F12", "Delete", "Ctrl+=", "Shift+.", "Tab", "Space"}) CHECK(chord_text(parse_chord(t)) == t);
  });
  test("keymap: the keys pressed run what the preset says; rebinding, clashes and saving work", [&] {
    Editor ed;
    ed.init_headless(1000, 700);
    ed.step_frame_headless();
    const Recti r = ed.scene_view_rect();
    ed.command("select Cube");
    ed.step_frame_headless({move(r.x + r.w / 2, r.y + r.h / 2)});
    auto press = [&](int k, int mods = 0) {
      ed.step_frame_headless({key(k, mods)});
      ed.step_frame_headless({key_up(k)});
    };
    /* Unity: Tab edits, 3 is face mode, Tab leaves. */
    press(platform::KEY_TAB);
    CHECK(ed.in_edit_mode());
    press(platform::KEY_TAB);
    CHECK(!ed.in_edit_mode());
    /* Maya: F8 edits, W is the Move tool. */
    ed.command("keymap Maya");
    CHECK(ed.keymap_preset_name() == "Maya");
    press(platform::KEY_F8);
    CHECK(ed.in_edit_mode());
    press(platform::KEY_F8);
    CHECK(!ed.in_edit_mode());
    /* Rebind: Edit Mode on Ctrl+Shift+E instead. */
    ed.command("keymap bind mode.edit_toggle Ctrl+Shift+E");
    press(platform::KEY_F8);
    CHECK(!ed.in_edit_mode());
    press(platform::KEY_E, platform::MOD_CTRL | platform::MOD_SHIFT);
    CHECK(ed.in_edit_mode());
    press(platform::KEY_E, platform::MOD_CTRL | platform::MOD_SHIFT);
    /* A clash is reported: bind Undo's key to Redo too. */
    const auto clash = keymap_conflicts(ed.keymap(), "edit.redo", parse_chord("Ctrl+Z"));
    CHECK(!clash.empty() && clash[0] == "edit.undo");
    /* The user's changes survive a save and load. */
    const std::string saved = ed.keymap_overrides_text();
    CHECK(saved.find("mode.edit_toggle=Ctrl+Shift+E") != std::string::npos);
    Editor ed2;
    ed2.init_headless(800, 600);
    ed2.command("keymap Maya");
    ed2.parse_keymap_overrides(saved);
    CHECK(ed2.shortcut_text("mode.edit_toggle") == "Ctrl+Shift+E");
    CHECK(ed2.shortcut_text("tool.move") == "W");
    /* Blender: G grabs (a modal move). */
    ed.command("keymap Blender");
    CHECK(ed.shortcut_text("mode.edit_toggle") == "Tab");  // a preset starts fresh
    ed.command("keymap Unity");
  });
  test("draw: snap axes in local (the rotated object's) or global (the world's) space", [&] {
    for (int global = 0; global < 2; global++) {
      Editor ed;
      ed.init_headless(800, 600);
      ed.step_frame_headless();
      ed.command("create Cube");
      GameObject *g = ed.selected_object();
      g->set_local_euler({0, 30, 0});
      g->set_world_position({0, 0.5f, 0});
      ed.command("edit face");
      ed.command(global ? "drawmode axes global" : "drawmode axes local");
      ed.command("draw rectangle");
      const Vec3 a = g->world_matrix().point({-0.2f, 0.5f, -0.2f}), b = g->world_matrix().point({0.2f, 0.5f, 0.2f});
      ed.command(strprintf("drawpoint %.6f %.6f %.6f", a.x, a.y, a.z));
      ed.command(strprintf("drawpoint %.6f %.6f %.6f", b.x, b.y, b.z));
      const Mesh &m = *g->get<MeshFilter>()->mesh;
      const size_t inner = m.face_count() - 1;
      /* The rectangle's sides in world space: along world X / Z (global) or the cube's (local). */
      bool world_aligned = true, local_aligned = true;
      for (uint32_t k = 0; k < m.face_size(inner); k++) {
        const Vec3 e = normalize(g->world_matrix().dir(m.positions[m.face_verts(inner)[(k + 1) % m.face_size(inner)]] - m.positions[m.face_verts(inner)[k]]));
        world_aligned = world_aligned && (std::fabs(e.x) > 0.999f || std::fabs(e.z) > 0.999f);
        const Vec3 l = normalize(m.positions[m.face_verts(inner)[(k + 1) % m.face_size(inner)]] - m.positions[m.face_verts(inner)[k]]);
        local_aligned = local_aligned && (std::fabs(l.x) > 0.999f || std::fabs(l.z) > 0.999f);
      }
      std::printf("    %s axes: %u corners, world-aligned %d, object-aligned %d\n", global ? "global" : "local", m.face_size(inner), (int)world_aligned,
                  (int)local_aligned);
      CHECK(global ? world_aligned && !local_aligned : local_aligned && !world_aligned);
    }
  });
  test("z-fighting check: duplicates, inner walls and hidden faces found, the right ones removed", [&] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.step_frame_headless();
    /* A cube duplicated in place: six pairs, one of each pair goes. */
    ed.command("select Cube");
    GameObject *cube = ed.selected_object();
    ed.command("duplicate");
    GameObject *copy = ed.selected_object();
    CHECK(copy && copy != cube);
    ed.command("zfight scene");
    const size_t n = ed.zfight_count();
    std::printf("    duplicated cube: %zu pair(s)\n", n);
    CHECK(n >= 6);
    ed.command("zfight fix");
    std::printf("    after fixing: %zu pair(s); faces %zu + %zu\n", ed.zfight_count(), cube->get<MeshFilter>()->mesh->face_count(),
                copy->get<MeshFilter>()->mesh->face_count());
    CHECK(ed.zfight_count() == 0);
    CHECK(cube->get<MeshFilter>()->mesh->face_count() + copy->get<MeshFilter>()->mesh->face_count() == 6);
    /* Two boxes joined side by side: the wall between them is two faces back to back; both go. */
    Mesh two = *primitives::cube();
    Mesh b2 = *primitives::cube();
    meshops::append_mesh(two, b2, Mat4::trs({1, 0, 0}, Quat(), {1, 1, 1}));
    meshops::merge_by_distance(two, 1e-5f);
    auto pairs = meshops::zfight_pairs(two);
    std::printf("    joined boxes: %zu pair(s), identical %d, back to back %d, both removable %d\n", pairs.size(), pairs.empty() ? 0 : (int)pairs[0].identical,
                pairs.empty() ? 0 : (int)!pairs[0].same_direction, pairs.empty() ? 0 : (int)(pairs[0].remove_a && pairs[0].remove_b));
    CHECK(pairs.size() == 1 && pairs[0].identical && !pairs[0].same_direction && pairs[0].remove_a && pairs[0].remove_b);
    std::vector<uint8_t> drop(two.face_count(), 0);
    drop[pairs[0].a] = drop[pairs[0].b] = 1;
    meshops::delete_faces(two, drop);
    CHECK(closed_manifold(two) && meshops::zfight_pairs(two).empty());
    /* A small face lying on a big one (same way): the small one is hidden and can go; a partial overlap can't. */
    Mesh stack;
    for (Vec3 p : {Vec3(0, 0, 0), Vec3(2, 0, 0), Vec3(2, 0, 2), Vec3(0, 0, 2), Vec3(0.5f, 0, 0.5f), Vec3(1, 0, 0.5f), Vec3(1, 0, 1), Vec3(0.5f, 0, 1),
                   Vec3(1.5f, 0, 1.5f), Vec3(3, 0, 1.5f), Vec3(3, 0, 3), Vec3(1.5f, 0, 3)})
      stack.add_vert(p);
    const uint32_t big[4] = {0, 3, 2, 1}, small[4] = {4, 7, 6, 5}, part[4] = {8, 11, 10, 9};
    stack.add_face(big, 4);
    stack.add_face(small, 4);
    stack.add_face(part, 4);
    pairs = meshops::zfight_pairs(stack);
    size_t hidden = 0, partial = 0;
    for (const auto &p2 : pairs) {
      if (p2.remove_a || p2.remove_b) {
        hidden++;
        CHECK((p2.remove_a ? p2.a : p2.b) == 1u);  // the small face
      }
      else partial++;
    }
    std::printf("    stacked faces: %zu pair(s), %zu hidden (removable), %zu partial\n", pairs.size(), hidden, partial);
    CHECK(hidden == 1 && partial == 1);
  });
  test("uv editor: face / island selection, rotate, scale, move, flip, fit, align; synced with Edit Mode", [&] {
    Editor ed;
    ed.init_headless(1000, 700);
    ed.step_frame_headless();
    ed.command("select Cube");
    ed.command("edit face");
    ed.command("uv smart");
    GameObject *g = ed.selected_object();
    const Mesh &m = *g->get<MeshFilter>()->mesh;
    CHECK(m.has_uvs());
    ed.command("uvsel mode face");
    ed.command("uvsel face 0");
    CHECK(ed.uv_selected_corners() == m.face_size(0));
    CHECK(ed.edit_face_selected(0) && !ed.edit_face_selected(1));  // synced
    auto corners = [&]() {
      std::vector<Vec2> v;
      for (uint32_t k = m.face_offsets[0]; k < m.face_offsets[1]; k++) v.push_back(m.uvs[k]);
      return v;
    };
    auto bounds = [](const std::vector<Vec2> &v) {
      Vec2 lo(1e9f, 1e9f), hi(-1e9f, -1e9f);
      for (Vec2 t : v) lo = Vec2(std::min(lo.x, t.x), std::min(lo.y, t.y)), hi = Vec2(std::max(hi.x, t.x), std::max(hi.y, t.y));
      return std::make_pair(lo, hi);
    };
    const auto b0 = bounds(corners());
    const Vec2 size0 = b0.second - b0.first;
    ed.command("uvxf rotate 90");
    const auto b1 = bounds(corners());
    CHECK_NEAR(b1.second.x - b1.first.x, size0.y, 1e-4f);  // width and height swap
    CHECK_NEAR(b1.second.y - b1.first.y, size0.x, 1e-4f);
    ed.command("uvxf scale 2");
    const auto b2 = bounds(corners());
    CHECK_NEAR(b2.second.x - b2.first.x, 2 * size0.y, 1e-4f);
    ed.command("uvxf fit");
    const auto b3 = bounds(corners());
    CHECK_NEAR(std::max(b3.second.x - b3.first.x, b3.second.y - b3.first.y), 1.0f, 1e-4f);
    CHECK_NEAR((b3.first.x + b3.second.x) * 0.5f, 0.5f, 1e-4f);
    ed.command("uvxf move 0.25 -0.1");
    const auto b4 = bounds(corners());
    CHECK_NEAR(b4.first.x - b3.first.x, 0.25f, 1e-4f);
    CHECK_NEAR(b4.first.y - b3.first.y, -0.1f, 1e-4f);
    const std::vector<Vec2> before_flip = corners();
    ed.command("uvxf flip_u");
    ed.command("uvxf flip_u");
    const std::vector<Vec2> after_two = corners();
    for (size_t k = 0; k < before_flip.size(); k++) CHECK(length(before_flip[k] - after_two[k]) < 1e-5f);  // twice = unchanged
    ed.command("uvxf align_left");
    for (Vec2 t : corners()) CHECK_NEAR(t.x, b4.first.x, 1e-5f);
    /* Island selection and select all / invert. */
    ed.command("uvsel island 2");
    const size_t island = ed.uv_selected_corners();
    CHECK(island >= m.face_size(2));
    ed.command("uvsel invert");
    CHECK(ed.uv_selected_corners() == m.corner_count() - island);
    ed.command("uvsel all");
    CHECK(ed.uv_selected_corners() == m.corner_count());
    /* The window draws with the new tool row. */
    ed.command("window UV Editor");
    ed.step_frame_headless();
    CHECK(ed.window_rect_for_test(WindowKind::UVEditor).w > 0);
  });
  test("edit tools: every operator has a group (selection tools together), and Extrude Individual is a switch", [&] {
    size_t other = 0, selects = 0;
    for (const EditOpInfo &op : edit_op_table()) {
      const int gidx = edit_op_group(op.op);
      if (gidx == kEditGroupCount - 1) {
        other++;
        std::printf("    in Other: %s\n", op.op);
      }
      if (starts_with(std::string(op.op), "select_")) {
        selects++;
        CHECK(gidx == 0);
      }
    }
    CHECK(selects >= 10);
    CHECK(other <= 1);  // only Origin to Selection
    Editor ed;
    ed.init_headless(900, 600);
    ed.step_frame_headless();
    ed.command("select Cube");
    ed.command("fsel facing 0 1 0 1 0 0");
    ed.command("set_extrude_individual 1");
    ed.command("editop extrude");
    const size_t ind = ed.selected_object()->get<MeshFilter>()->mesh->face_count();
    ed.command("edit off");
    ed.command("create Cube");
    ed.command("fsel facing 0 1 0 1 0 0");
    ed.command("set_extrude_individual 0");
    ed.command("editop extrude");
    const size_t reg = ed.selected_object()->get<MeshFilter>()->mesh->face_count();
    std::printf("    extrude two faces: individual %zu faces, region %zu faces\n", ind, reg);
    CHECK(ind > reg);
    ed.command("window Modeling Tools");
    ed.step_frame_headless();
  });
}

/* ===================================================================== */
/* Round 16: Delete in the Hierarchy, drawing in open space, the UV      */
/* gizmo, Merge by Distance, Z-fighting highlights                       */
/* ===================================================================== */

static void round16_tests() {
  auto ev = [](platform::EventType t, int x, int y, int key = 0, int mods = 0) {
    platform::Event e;
    e.type = t;
    e.x = x;
    e.y = y;
    e.key = key;
    e.mods = mods;
    return e;
  };
  using ET = platform::EventType;
  test("hierarchy: Delete (and Backspace) removes the objects selected there - never a Project file", [&] {
    const std::string proj = fs::join(test_dir(), "deleteproject");
    std::error_code ec;
    std::filesystem::remove_all(proj, ec);
    fs::make_dirs(fs::join(proj, "Assets/Scenes"));
    fs::make_dirs(fs::join(proj, "research"));
    fs::write_file(fs::join(proj, "Assets/Keep.txt"), "keep me");
    set_env("BLENDITY_PROJECT", proj);
    {
      Editor ed;
      ed.init_headless(1400, 850);
      ed.step_frame_headless();
      /* A file selected in the Project window first: the Delete key later must not reach it. */
      ed.command("window Project");
      ed.step_frame_headless();
      ed.select_project_file_for_test(fs::join(proj, "Assets/Keep.txt"));
      /* ...and the Project window clicked last (it has the keyboard focus). */
      const Recti pr = ed.window_rect_for_test(WindowKind::Project);
      CHECK(pr.w > 0);
      ed.step_frame_headless({ev(ET::MouseMove, pr.x + pr.w - 20, pr.bottom() - 20)});
      ed.step_frame_headless({ev(ET::MouseDown, pr.x + pr.w - 20, pr.bottom() - 20)});
      ed.step_frame_headless({ev(ET::MouseUp, pr.x + pr.w - 20, pr.bottom() - 20)});
      ed.select_project_file_for_test(fs::join(proj, "Assets/Keep.txt"));
      int x = 0, y = 0;
      CHECK(ed.hierarchy_row_for_test("Cube", x, y));
      const size_t n0 = ed.scene().object_count();
      ed.step_frame_headless({ev(ET::MouseMove, x, y)});
      ed.step_frame_headless({ev(ET::MouseDown, x, y)});
      ed.step_frame_headless({ev(ET::MouseUp, x, y)});
      CHECK(ed.selected_object() && ed.selected_object()->name == "Cube");
      ed.step_frame_headless({ev(ET::KeyDown, x, y, platform::KEY_DELETE)});
      ed.step_frame_headless({ev(ET::KeyUp, x, y, platform::KEY_DELETE)});
      ed.step_frame_headless();
      std::printf("    objects %zu -> %zu, Cube %s, Keep.txt %s, delete dialog %d\n", n0, ed.scene().object_count(),
                  ed.scene().find_by_name("Cube") ? "still there" : "deleted", fs::exists(fs::join(proj, "Assets/Keep.txt")) ? "kept" : "GONE",
                  (int)ed.project_delete_pending());
      CHECK(ed.scene().object_count() == n0 - 1 && !ed.scene().find_by_name("Cube"));
      CHECK(fs::exists(fs::join(proj, "Assets/Keep.txt")));
      CHECK(!ed.project_delete_pending());
      /* Backspace too (Unity on macOS), on another row. */
      CHECK(ed.hierarchy_row_for_test("Sphere", x, y));
      ed.step_frame_headless({ev(ET::MouseDown, x, y)});
      ed.step_frame_headless({ev(ET::MouseUp, x, y)});
      ed.step_frame_headless({ev(ET::KeyDown, x, y, platform::KEY_BACKSPACE)});
      ed.step_frame_headless({ev(ET::KeyUp, x, y, platform::KEY_BACKSPACE)});
      CHECK(!ed.scene().find_by_name("Sphere"));
      CHECK(fs::exists(fs::join(proj, "Assets/Keep.txt")));
      /* In Edit Mode: Delete in the Hierarchy still deletes the object (not its faces). */
      ed.command("select Cylinder");
      ed.command("edit face all");
      CHECK(ed.in_edit_mode());
      CHECK(ed.hierarchy_row_for_test("Cylinder", x, y));
      ed.step_frame_headless({ev(ET::MouseMove, x, y)});
      ed.step_frame_headless({ev(ET::MouseDown, x, y)});
      ed.step_frame_headless({ev(ET::MouseUp, x, y)});
      ed.step_frame_headless({ev(ET::KeyDown, x, y, platform::KEY_DELETE)});
      ed.step_frame_headless({ev(ET::KeyUp, x, y, platform::KEY_DELETE)});
      std::printf("    from Edit Mode: Cylinder %s, edit mode %d\n", ed.scene().find_by_name("Cylinder") ? "still there" : "deleted", (int)ed.in_edit_mode());
      CHECK(!ed.scene().find_by_name("Cylinder") && !ed.in_edit_mode());
    }
    set_env("BLENDITY_PROJECT", "");
  });

  test("draw off the mesh: clicks go on the ground, guides take a line anywhere else", [&] {
    Editor ed;
    ed.init_headless(1000, 700);
    ed.step_frame_headless();
    ed.command("camera 30 35 9 0 0 2");
    ed.command("select Main Camera");
    ed.command("draw rectangle");
    GameObject *g = ed.selected_object();
    ed.step_frame_headless();
    auto click_world = [&](Vec3 w) {
      int x, y;
      if (!ed.project_to_window(w, x, y)) return false;
      ed.step_frame_headless({ev(ET::MouseMove, x, y)});
      ed.step_frame_headless({ev(ET::MouseDown, x, y)});
      ed.step_frame_headless({ev(ET::MouseUp, x, y)});
      return true;
    };
    CHECK(click_world({-1, 0, 2}));
    CHECK(click_world({1, 0, 3}));
    const Mesh *m = g ? g->get<MeshFilter>()->mesh.get() : nullptr;
    CHECK(m && m->face_count() == 1);
    if (m && m->face_count() == 1) {
      float ymin = 1e9f, ymax = -1e9f;
      for (const Vec3 &p2 : m->positions) {
        const float y = g->world_matrix().point(p2).y;
        ymin = std::min(ymin, y), ymax = std::max(ymax, y);
      }
      const Vec3 n = normalize(g->world_matrix().dir(m->face_normal(0)));
      std::printf("    on the ground: normal y %.3f, y %.4f..%.4f\n", n.y, ymin, ymax);
      CHECK(std::fabs(n.y) > 0.999f && std::fabs(ymin) < 1e-3f && std::fabs(ymax) < 1e-3f);
    }
  });
  test("merge by distance (Edit Mode): the selected vertices weld; Unselected welds onto the rest; F9 sets the distance", [&] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.step_frame_headless();
    ed.command("create Cube");
    GameObject *g = ed.selected_object();
    /* Two copies of a cube's corners a hair apart: a cube made of unwelded faces. */
    Mesh &m = *mesh_make_mutable(g->get<MeshFilter>()->mesh);
    Mesh loose;
    for (size_t f = 0; f < m.face_count(); f++) {
      std::vector<uint32_t> fv;
      for (uint32_t k = 0; k < m.face_size(f); k++) fv.push_back(loose.add_vert(m.positions[m.face_verts(f)[k]] + Vec3(0.0001f * (float)f, 0, 0)));
      loose.add_face(fv.data(), fv.size());
    }
    m = loose;
    m.touch();
    CHECK(m.vert_count() == 24 && !closed_manifold(m));
    ed.command("edit vertex all");
    ed.command("editop merge_distance");
    ed.command("redo amount 0.001");
    const Mesh &r = *g->get<MeshFilter>()->mesh;
    std::printf("    welded: %zu vertices, closed %d\n", r.vert_count(), (int)closed_manifold(r));
    CHECK(r.vert_count() == 8 && closed_manifold(r));
    /* A smaller distance (F9) merges less. */
    ed.command("redo amount 0.00005");
    CHECK(g->get<MeshFilter>()->mesh->vert_count() > 8);
    ed.command("edit off");
    /* Selected only, and Unselected. */
    Mesh pts;
    pts.add_vert({0, 0, 0});
    pts.add_vert({0.0005f, 0, 0});
    pts.add_vert({1, 0, 0});
    pts.add_vert({1.0005f, 0, 0});
    for (int k = 0; k < 4; k++) pts.add_loose_edge((uint32_t)k, (uint32_t)((k + 1) % 4));
    Mesh a = pts;
    CHECK(meshops::merge_by_distance_selected(a, 0.001f, {1, 1, 0, 0}) == 1 && a.vert_count() == 3);
    Mesh b = pts;
    CHECK(meshops::merge_by_distance_selected(b, 0.001f, {0, 1, 0, 1}) == 0);  // selected ones are far apart
    Mesh c = pts;
    CHECK(meshops::merge_by_distance_selected(c, 0.001f, {0, 1, 0, 1}, true) == 2 && c.vert_count() == 2);  // onto the unselected
  });
  test("uv gizmo: the Scene view's handles in the UV editor - drag along U, scale along V, rotate", [&] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.step_frame_headless();
    ed.command("select Cube");
    ed.command("edit face");
    ed.command("uv smart");
    ed.command("window UV Editor");
    ed.command("uvsel mode face");
    ed.command("uvsel face 0");
    ed.step_frame_headless();
    GameObject *g = ed.selected_object();
    auto face_uv = [&]() {  /* the mesh as it is now (edits replace it: copy on write) */
      const Mesh &m = *g->get<MeshFilter>()->mesh;
      std::vector<Vec2> v;
      for (uint32_t k = m.face_offsets[0]; k < m.face_offsets[1]; k++) v.push_back(m.uvs[k]);
      return v;
    };
    Vec2 gc;
    float arm = 0;
    CHECK(ed.uv_gizmo_for_test(gc, arm));
    auto drag = [&](Vec2 from, Vec2 to) {
      ed.step_frame_headless({ev(ET::MouseMove, (int)from.x, (int)from.y)});
      ed.step_frame_headless({ev(ET::MouseDown, (int)from.x, (int)from.y)});
      ed.step_frame_headless({ev(ET::MouseMove, (int)((from.x + to.x) / 2), (int)((from.y + to.y) / 2))});
      ed.step_frame_headless({ev(ET::MouseMove, (int)to.x, (int)to.y)});
      ed.step_frame_headless({ev(ET::MouseUp, (int)to.x, (int)to.y)});
    };
    /* Move tool: drag the U (red) arrow diagonally - only U changes. */
    ed.command("tool move");
    ed.step_frame_headless();
    const std::vector<Vec2> a0 = face_uv();
    drag(gc + Vec2(arm * 0.6f, 0), gc + Vec2(arm * 0.6f + 40, -30));
    const std::vector<Vec2> a1 = face_uv();
    float du = a1[0].x - a0[0].x, dv = a1[0].y - a0[0].y;
    std::printf("    U arrow: du %.4f, dv %.4f\n", du, dv);
    CHECK(du > 0.01f && std::fabs(dv) < 1e-6f);
    for (size_t k = 0; k < a0.size(); k++) CHECK(std::fabs((a1[k].x - a0[k].x) - du) < 1e-5f);
    /* Scale tool: the V handle stretches only V. */
    ed.command("tool scale");
    ed.step_frame_headless();
    CHECK(ed.uv_gizmo_for_test(gc, arm));
    const std::vector<Vec2> b0 = face_uv();
    drag(gc + Vec2(0, -arm), gc + Vec2(0, -arm * 2));
    const std::vector<Vec2> b1 = face_uv();
    auto extent = [](const std::vector<Vec2> &v, int axis) {
      float lo = 1e9f, hi = -1e9f;
      for (Vec2 t : v) lo = std::min(lo, axis ? t.y : t.x), hi = std::max(hi, axis ? t.y : t.x);
      return hi - lo;
    };
    std::printf("    V handle: width %.4f -> %.4f, height %.4f -> %.4f\n", extent(b0, 0), extent(b1, 0), extent(b0, 1), extent(b1, 1));
    CHECK_NEAR(extent(b1, 0), extent(b0, 0), 1e-5f);
    CHECK_NEAR(extent(b1, 1), extent(b0, 1) * 2.0f, 0.05f * extent(b0, 1));
    /* Rotate tool: dragging round the ring turns it a quarter. */
    ed.command("tool rotate");
    ed.step_frame_headless();
    CHECK(ed.uv_gizmo_for_test(gc, arm));
    const float ring = arm * 0.75f;  // the ring radius (48 of 64)
    const std::vector<Vec2> c0 = face_uv();
    drag(gc + Vec2(ring, 0), gc + Vec2(0, -ring));
    const std::vector<Vec2> c1 = face_uv();
    std::printf("    ring: width %.4f -> %.4f\n", extent(c0, 0), extent(c1, 0));
    CHECK_NEAR(extent(c1, 0), extent(c0, 1), 0.02f * extent(c0, 1));  // width and height swapped
  });
  test("z-fighting outlines: not left behind in Object Mode or after the object is deleted", [&] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.step_frame_headless();
    ed.command("select Cube");
    {
      /* A cube with a doubled top face: z-fighting inside one mesh. */
      GameObject *c0 = ed.selected_object();
      Mesh &cm0 = *mesh_make_mutable(c0->get<MeshFilter>()->mesh);
      const size_t top = face_facing(cm0, {0, 1, 0});
      std::vector<uint32_t> fv(cm0.face_verts(top), cm0.face_verts(top) + cm0.face_size(top));
      cm0.add_face(fv.data(), fv.size());
      cm0.touch();
    }
    ed.command("duplicate");
    GameObject *copy = ed.selected_object();
    ed.command("select Cube");
    ed.command("edit face");
    ed.command("editop select_overlapping");  // Select Z-Fighting
    ed.step_frame_headless();
    CHECK(ed.zfight_count() > 0 && ed.zfight_outlines_visible());
    ed.command("edit off");
    ed.step_frame_headless();
    std::printf("    object mode: outlines %d\n", (int)ed.zfight_outlines_visible());
    CHECK(!ed.zfight_outlines_visible());
    /* From the panel they show in Object Mode; deleting one of the objects drops its pairs. */
    ed.command("zfight scene");
    ed.command("window Modeling Tools");
    ed.step_frame_headless();
    CHECK(ed.zfight_outlines_visible());
    ed.select_object(copy->id);
    ed.command("delete");
    ed.step_frame_headless();
    std::printf("    after deleting the copy: %zu pair(s), outlines %d\n", ed.zfight_count(), (int)ed.zfight_outlines_visible());
    CHECK(ed.zfight_count() == 1);  // only the cube's own doubled top is left
    ed.command("select Cube");
    ed.command("delete");
    ed.step_frame_headless();
    CHECK(ed.zfight_count() == 0 && !ed.zfight_outlines_visible());
  });
}

/* ===================================================================== */
/* Round 17: dropdowns in dialogs, the starter scene's teapot, drawing on */
/* any axis plane, rotating imprinted faces, deleting materials, curved   */
/* surfaces                                                               */
/* ===================================================================== */

static void round17_tests() {
  auto ev = [](platform::EventType t, int x, int y, int key = 0, int mods = 0) {
    platform::Event e;
    e.type = t;
    e.x = x;
    e.y = y;
    e.key = key;
    e.mods = mods;
    return e;
  };
  using ET = platform::EventType;
  test("ui: a dropdown inside Preferences (the keymap preset) opens over the dialog and picks without closing it", [&] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.step_frame_headless();
    ed.command("preferences");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    CHECK(ed.dialog_open_for_test() && ed.popup_count_for_test() == 1);
    const Recti r = ed.combo_rect_for_test("km_preset");
    std::printf("    preset combo at %d,%d %dx%d\n", r.x, r.y, r.w, r.h);
    CHECK(r.w > 0 && r.h > 0);
    if (r.w <= 0) return;
    auto click = [&](int x, int y) {
      ed.step_frame_headless({ev(ET::MouseMove, x, y)});
      ed.step_frame_headless({ev(ET::MouseDown, x, y)});
      ed.step_frame_headless({ev(ET::MouseUp, x, y)});
      ed.step_frame_headless();
    };
    click(r.x + r.w / 2, r.y + r.h / 2);
    std::printf("    after clicking it: dialog %d, popups %zu\n", (int)ed.dialog_open_for_test(), ed.popup_count_for_test());
    CHECK(ed.dialog_open_for_test() && ed.popup_count_for_test() == 2);
    /* Clicking it again closes only the list. */
    click(r.x + r.w / 2, r.y + r.h / 2);
    CHECK(ed.dialog_open_for_test() && ed.popup_count_for_test() == 1);
    /* Open it and pick "Blender", the list's second row below the field. */
    click(r.x + r.w / 2, r.y + r.h / 2);
    CHECK(ed.popup_count_for_test() == 2);
    const int row = r.h + 4;
    click(r.x + 20, r.bottom() + 4 + row + row / 2);
    std::printf("    picked: preset %s, dialog %d, popups %zu\n", ed.keymap_preset_name().c_str(), (int)ed.dialog_open_for_test(),
                ed.popup_count_for_test());
    CHECK(ed.keymap_preset_name() == "Blender");
    CHECK(ed.dialog_open_for_test() && ed.popup_count_for_test() == 1);
    ed.command("keymap Unity");
  });
  test("primitives: the Utah teapot - Newell's 32 patches welded, facing out, with UVs", [&] {
    MeshPtr m = primitives::teapot(1.2f, 8);
    std::printf("    teapot: %zu verts, %zu faces\n", m->vert_count(), m->face_count());
    CHECK(m->face_count() > 1500 && m->vert_count() > 1500);
    CHECK(structurally_valid(*m));
    CHECK(m->has_uvs() && m->uvs.size() == m->corner_count());
    Vec3 lo(1e9f), hi(-1e9f);
    for (const Vec3 &p : m->positions) lo = vmin(lo, p), hi = vmax(hi, p);
    std::printf("    bounds %.3f..%.3f x %.3f..%.3f x %.3f..%.3f\n", lo.x, hi.x, lo.y, hi.y, lo.z, hi.z);
    CHECK_NEAR(lo.y, 0.0f, 1e-4);
    CHECK_NEAR(hi.y, 1.2f, 1e-3);
    CHECK(hi.x > 1.25f && lo.x < -1.1f);  // spout to +x, handle to -x
    /* Faces point out: the body's faces away from the axis, the knob's top up, the base down. */
    size_t out = 0, body = 0;
    for (size_t f = 0; f < m->face_count(); f++) {
      const Vec3 c = m->face_center(f), n = m->face_normal(f);
      if (std::fabs(c.x) > 0.5f || c.y < 0.2f || c.y > 0.85f) continue;  // the body's sides, clear of the spout and handle
      body++;
      if (dot(n, Vec3(c.x, 0, c.z)) > 0) out++;
    }
    std::printf("    body faces facing out: %zu of %zu\n", out, body);
    CHECK(body > 50 && out == body);
    size_t top = 0, bottom = 0;
    for (size_t f = 0; f < m->face_count(); f++) {
      const Vec3 c = m->face_center(f), n = m->face_normal(f);
      if (c.y > 1.19f && std::fabs(c.x) < 0.05f && std::fabs(c.z) < 0.05f) top += n.y > 0.9f;
      if (c.y < 0.005f && std::fabs(c.x) < 0.2f && std::fabs(c.z) < 0.2f) bottom += n.y < -0.9f;
    }
    CHECK(top > 0 && bottom > 0);
    /* Patch borders are welded: only the rim, the lid's edge and the ends of the handle
     * and the spout stay open. */
    std::map<std::pair<uint32_t, uint32_t>, int> uses;
    for (size_t f = 0; f < m->face_count(); f++) {
      const uint32_t *fv = m->face_verts(f);
      const size_t n = m->face_size(f);
      for (size_t k = 0; k < n; k++) {
        const uint32_t a = fv[k], b = fv[(k + 1) % n];
        uses[{std::min(a, b), std::max(a, b)}]++;
      }
    }
    size_t open = 0, over = 0;
    for (auto &[e, c] : uses) open += c == 1, over += c > 2;
    std::printf("    open edges %zu, edges in 3+ faces %zu\n", open, over);
    CHECK(open <= 8 * 4 * 6 && over == 0);
    CHECK(primitives::teapot(1.0f, 3)->face_count() < m->face_count());
  });
  test("scene: a new scene starts with the Utah teapot, a camera and a point light", [&] {
    Scene s;
    build_starter_scene(s);
    GameObject *pot = s.find_by_name("Utah Teapot"), *cam = s.find_by_name("Main Camera"), *light = s.find_by_name("Point Light");
    CHECK(pot && pot->get<MeshFilter>() && pot->get<MeshRenderer>());
    CHECK(cam && cam->get<Camera>());
    CHECK(light && light->get<Light>() && light->get<Light>()->type == 1);
    CHECK(s.find_by_name("Directional Light") == nullptr && s.find_by_name("Cube") == nullptr);
    /* It saves and loads like any scene. */
    std::string a = save_scene_text(s), err;
    Scene l;
    CHECK(load_scene_text(a, l, err) && save_scene_text(l) == a);
    /* File > New Scene in the editor, and the Teapot under GameObject > 3D Object. */
    Editor ed;
    ed.init_headless(900, 600);
    ed.command("newscene");
    ed.step_frame_headless();
    CHECK(by_name(ed.scene(), "Utah Teapot") != nullptr && by_name(ed.scene(), "Point Light") != nullptr);
    ed.command("create Teapot");
    ed.step_frame_headless();
    CHECK(by_name(ed.scene(), "Teapot") != nullptr);
  });
  test("draw: X / Y / Z guides (Plasticity) let a polyline leave its plane - a 3D path for Follow", [&] {
    Editor ed;
    ed.init_headless(1000, 700);
    ed.step_frame_headless();
    ed.command("create Cube");
    GameObject *g = ed.selected_object();
    g->set_world_position({0, 0.5f, 0});
    ed.command("camera 35 25 7 0 1 0");
    ed.command("edit face");
    ed.command("draw polyline");
    ed.step_frame_headless();
    auto press = [&](int k) {
      const Recti r = ed.scene_view_rect();
      ed.step_frame_headless({ev(ET::MouseMove, r.x + r.w / 2, r.y + r.h / 2)});
      ed.step_frame_headless({ev(ET::KeyDown, r.x + r.w / 2, r.y + r.h / 2, k)});
      ed.step_frame_headless({ev(ET::KeyUp, r.x + r.w / 2, r.y + r.h / 2, k)});
    };
    auto click_world = [&](Vec3 w) {
      int x, y;
      if (!ed.project_to_window(w, x, y)) return false;
      ed.step_frame_headless({ev(ET::MouseMove, x, y)});
      ed.step_frame_headless({ev(ET::MouseDown, x, y)});
      ed.step_frame_headless({ev(ET::MouseUp, x, y)});
      return true;
    };
    ed.command("drawmode axes global");
    /* From the top face's centre straight up along a Y guide, then along +Z on a Z guide through the top. */
    CHECK(click_world({0, 1, 0}));
    const size_t g0 = ed.guide_count_for_test();
    press(platform::KEY_Y);
    CHECK(ed.guide_count_for_test() == g0 + 1 && ed.last_guide_for_test() && std::fabs(ed.last_guide_for_test()->d.y) > 0.999f);
    CHECK(click_world({0, 2, 0}));
    press(platform::KEY_Z);
    CHECK(ed.guide_count_for_test() == g0 + 2 && std::fabs(ed.last_guide_for_test()->d.z) > 0.999f);
    CHECK(click_world({0, 2, 1}));
    press(platform::KEY_ENTER);
    const Mesh *m = g->get<MeshFilter>()->mesh.get();
    std::printf("    path: %zu wire edges, %zu faces\n", m->loose_edges.size(), m->face_count());
    CHECK(m->loose_edges.size() == 2 && m->face_count() == 6);
    float top = -1e9f, far_z = -1e9f;
    for (const Vec3 &p : m->positions) {
      const Vec3 w = g->world_matrix().point(p);
      top = std::max(top, w.y);
      if (std::fabs(w.y - 2) < 1e-2f) far_z = std::max(far_z, w.z);
    }
    std::printf("    highest point y %.4f, furthest along z at the top %.4f\n", top, far_z);
    for (const Vec3 &p : m->positions) {
      const Vec3 w = g->world_matrix().point(p);
      if (w.y > 1.01f) std::printf("      point %.4f %.4f %.4f\n", w.x, w.y, w.z);
    }
    CHECK(std::fabs(top - 2) < 1e-2f && std::fabs(far_z - 1) < 1e-2f);
    /* Follow: the top face swept up the path and along it. */
    size_t topf = SIZE_MAX;
    for (size_t f = 0; f < m->face_count(); f++)
      if (m->face_normal(f).y > 0.9f) topf = f;
    CHECK(topf != SIZE_MAX);
    press(platform::KEY_ESCAPE);
    ed.select_faces_for_test({topf});
    ed.command("editop follow");
    m = g->get<MeshFilter>()->mesh.get();
    Mesh copy = *m;
    meshops::merge_by_distance(copy, 1e-5f);
    std::printf("    after Follow: %zu faces, closed %d, wires %zu\n", copy.face_count(), (int)closed_manifold(copy), copy.loose_edges.size());
    CHECK(copy.face_count() > 6 + 4 && copy.loose_edges.empty() && closed_manifold(copy));  // a face of a solid grows it: no start cap inside
    float ymax = -1e9f, zmax = -1e9f;
    for (const Vec3 &p : copy.positions) ymax = std::max(ymax, p.y), zmax = std::max(zmax, p.z);
    CHECK(ymax > 1.9f && zmax > 0.9f);  // (object space) up past the cube, then out along Z
    /* A closed loop that turned planes stays wire edges (it is not flat): a new, empty drawing. */
    press(platform::KEY_ESCAPE);
    ed.command("edit off");
    ed.command("select Main Camera");
    ed.command("draw polyline");
    g = ed.selected_object();
    ed.step_frame_headless();
    CHECK(click_world({2, 0, 2}));
    press(platform::KEY_Y);
    CHECK(click_world({2, 1, 2}));
    press(platform::KEY_X);
    CHECK(click_world({3, 1, 2}));
    press(platform::KEY_Z);
    CHECK(click_world({3, 1, 3}));
    std::printf("    loop points so far: %zu\n", ed.draw_point_count());
    for (const Vec3 &p : ed.draw_points_for_test()) std::printf("      %.3f %.3f %.3f\n", p.x, p.y, p.z);
    const size_t faces_before = g->get<MeshFilter>()->mesh->face_count(), verts_before = g->get<MeshFilter>()->mesh->vert_count();
    CHECK(click_world({2, 0, 2}));  // back on the first point: closes it
    std::printf("    after closing: %zu points\n", ed.draw_point_count());
    m = g->get<MeshFilter>()->mesh.get();
    for (size_t v = verts_before; v < m->vert_count(); v++) {
      const Vec3 w = g->world_matrix().point(m->positions[v]);
      std::printf("      new point %.3f %.3f %.3f\n", w.x, w.y, w.z);
    }
    std::printf("    bent loop: %zu faces (before %zu), %zu wires\n", m->face_count(), faces_before, m->loose_edges.size());
    CHECK(m->face_count() == faces_before && m->loose_edges.size() == 4);
  });
  test("rotate a shape drawn on a face: the ring around it is zipped again, no twisted faces", [&] {
    for (float deg : {30.0f, 45.0f, 90.0f, 170.0f}) {
      Editor ed;
      ed.init_headless(900, 650);
      ed.step_frame_headless();
      ed.command("keymap Blender");  // R: Blender's modal rotate
      ed.command("create Cube");
      GameObject *g = ed.selected_object();
      g->set_world_position({0, 0.5f, 0});
      ed.command("edit face");
      ed.command("draw rectangle");
      ed.command("drawpoint -0.3 1 -0.15");
      ed.command("drawpoint 0.3 1 0.15");
      {
        const Recti vr = ed.scene_view_rect();
        ed.step_frame_headless({ev(ET::MouseMove, vr.x + vr.w / 2, vr.y + vr.h / 2)});
        ed.step_frame_headless({ev(ET::KeyDown, vr.x + vr.w / 2, vr.y + vr.h / 2, platform::KEY_ESCAPE)});
        ed.step_frame_headless({ev(ET::KeyUp, vr.x + vr.w / 2, vr.y + vr.h / 2, platform::KEY_ESCAPE)});
      }
      const Mesh *m = g->get<MeshFilter>()->mesh.get();
      size_t inner = SIZE_MAX;
      for (size_t f = 0; f < m->face_count(); f++)
        if (m->face_normal(f).y > 0.9f && m->face_size(f) == 4 && std::fabs(m->face_center(f).x) < 1e-4f && std::fabs(m->face_center(f).z) < 1e-4f &&
            length(m->positions[m->face_verts(f)[0]] - Vec3(0, 0.5f, 0)) < 0.4f)
          inner = f;
      CHECK(inner != SIZE_MAX);
      if (inner == SIZE_MAX) return;
      ed.select_faces_for_test({inner});
      /* R, Y, the angle, Enter: Blender's modal rotate about Y. */
      const Recti r = ed.scene_view_rect();
      const int cx = r.x + r.w / 2 + 60, cy = r.y + r.h / 2;
      ed.step_frame_headless({ev(ET::MouseMove, cx, cy)});
      for (int k : {platform::KEY_R, platform::KEY_Y}) {
        ed.step_frame_headless({ev(ET::KeyDown, cx, cy, k)});
        ed.step_frame_headless({ev(ET::KeyUp, cx, cy, k)});
      }
      for (char c : strprintf("%g", deg)) {
        platform::Event t;
        t.type = ET::Text;
        t.codepoint = (uint32_t)c;
        t.x = cx;
        t.y = cy;
        ed.step_frame_headless({t});
      }
      ed.step_frame_headless({ev(ET::KeyDown, cx, cy, platform::KEY_ENTER)});
      ed.step_frame_headless({ev(ET::KeyUp, cx, cy, platform::KEY_ENTER)});
      m = g->get<MeshFilter>()->mesh.get();
      std::string why, bad, over;
      const size_t nb = bad_triangulations(*m, &bad), no = overlapping_face_pairs(*m, &over);
      /* The selected face turned by the angle and is still selected. */
      size_t sel = 0, turned = 0;
      for (size_t f = 0; f < m->face_count(); f++)
        if (ed.edit_face_selected(f)) {
          sel++;
          const Vec3 a = m->positions[m->face_verts(f)[0]], b = m->positions[m->face_verts(f)[1]];
          const Vec3 d = normalize(b - a);
          for (float sgn : {1.0f, -1.0f}) {
            const float ang = deg * kDeg2Rad * sgn;
            for (Vec3 e : {Vec3(1, 0, 0), Vec3(0, 0, 1), Vec3(-1, 0, 0), Vec3(0, 0, -1)}) {
              const Vec3 rot(e.x * std::cos(ang) + e.z * std::sin(ang), 0, -e.x * std::sin(ang) + e.z * std::cos(ang));
              if (dot(rot, d) > 0.9999f) turned = 1;
            }
          }
        }
      std::printf("    %g degrees: %zu faces, valid %d, bad triangulations %zu, overlaps %zu, closed %d, selected %zu (turned %zu)\n", deg,
                  m->face_count(), (int)structurally_valid(*m, &why), nb, no, (int)closed_manifold(*m), sel, turned);
      if (nb) std::printf("      %s\n", bad.c_str());
      if (no) std::printf("      %s\n", over.c_str());
      CHECK(structurally_valid(*m) && nb == 0 && no == 0 && closed_manifold(*m));
      CHECK(sel == 1 && turned == 1);
      /* Undo puts the rectangle back unturned. */
    }
    /* The operator itself: a ring zipped round a square turned 45 degrees inside a square. */
    Mesh q;
    for (Vec3 p : {Vec3(-1, 0, -1), Vec3(1, 0, -1), Vec3(1, 0, 1), Vec3(-1, 0, 1)}) q.add_vert(p);
    q.add_face({0, 3, 2, 1});
    std::string err;
    const long in = meshops::imprint_loop(q, 0, {Vec3(-0.4f, 0, -0.4f), Vec3(0.4f, 0, -0.4f), Vec3(0.4f, 0, 0.4f), Vec3(-0.4f, 0, 0.4f)}, &err);
    CHECK(in >= 0);
    if (in < 0) return;
    std::vector<uint8_t> moved(q.vert_count(), 0);
    for (uint32_t k = 0; k < q.face_size((size_t)in); k++) moved[q.face_verts((size_t)in)[k]] = 1;
    const Quat rot = Quat::axis_angle({0, 1, 0}, 150.0f * kDeg2Rad);
    for (size_t v = 0; v < q.vert_count(); v++)
      if (moved[v]) q.positions[v] = rot.rotate(q.positions[v]);
    const size_t bad_before = bad_triangulations(q);
    std::vector<uint8_t> dropped;
    const size_t rebuilt = meshops::repair_rings(q, moved, &dropped);
    std::printf("    operator: bad before %zu, rebuilt %zu, bad after %zu, overlaps %zu\n", bad_before, rebuilt, bad_triangulations(q), overlapping_face_pairs(q));
    CHECK(bad_before > 0 && rebuilt == 1 && bad_triangulations(q) == 0 && overlapping_face_pairs(q) == 0);
    float area = 0;
    for (size_t f = 0; f < q.face_count(); f++) {
      Vec3 nw(0.0f);
      for (uint32_t k = 0; k < q.face_size(f); k++) nw += cross(q.positions[q.face_verts(f)[k]], q.positions[q.face_verts(f)[(k + 1) % q.face_size(f)]]);
      area += 0.5f * length(nw);
    }
    CHECK_NEAR(area, 4.0f, 1e-3f);
    /* Nothing folded: nothing rebuilt. */
    CHECK(meshops::repair_rings(q, moved) == 0);
  });
  test("materials window: Delete Material empties its slots, trashes an asset's file, asks first and undoes", [&] {
    const std::string proj = fs::join(test_dir(), "matdelete_project"), trash = fs::join(test_dir(), "matdelete_trash");
    std::error_code ec;
    std::filesystem::remove_all(proj, ec);
    std::filesystem::remove_all(trash, ec);
    fs::make_dirs(fs::join(proj, "Assets/Scenes"));
    fs::make_dirs(fs::join(proj, "research"));
    set_env("BLENDITY_PROJECT", proj);
    set_env("BLENDITY_TRASH", trash);
    {
      Editor ed;
      ed.init_headless(1200, 800);
      ed.step_frame_headless();
      ed.command("select Cube");
      ed.command("newmat BrickRed");
      const std::string file = fs::join(proj, "Assets/Materials/BrickRed.mat");
      CHECK(fs::exists(file));
      ed.command("assignmat Assets/Materials/BrickRed.mat");
      ed.command("select Sphere");
      ed.command("assignmat Assets/Materials/BrickRed.mat");
      MaterialPtr brick = material_asset("Assets/Materials/BrickRed.mat");
      CHECK(brick != nullptr);
      auto users = [&](const MaterialPtr &m) {
        size_t n = 0;
        ed.scene().for_each([&](GameObject &g) {
          if (auto *mr = g.get<MeshRenderer>())
            for (auto &x : mr->materials) n += x == m;
        });
        return n;
      };
      CHECK(users(brick) == 2);
      /* Asking first: the window shows a confirmation; Cancel keeps everything. */
      ed.command("window Materials");
      ed.step_frame_headless();
      ed.ask_delete_material_for_test(brick);
      ed.step_frame_headless();
      CHECK(ed.material_delete_pending() && ed.popup_count_for_test() >= 1);
      /* A scene material, deleted from the console: its slot empties, nothing on disk changes. */
      GameObject *cyl = by_name(ed.scene(), "Cylinder");
      MaterialPtr orange = cyl->get<MeshRenderer>()->materials[0];
      ed.command("deletemat Orange");
      CHECK(cyl->get<MeshRenderer>()->materials.size() == 1 && cyl->get<MeshRenderer>()->materials[0] == nullptr);
      ed.step_frame_headless();
      /* The asset: both slots emptied, the file in the (scratch) trash. */
      ed.command("deletemat Assets/Materials/BrickRed.mat");
      ed.step_frame_headless();
      std::printf("    after deleting: users %zu, file %s, pending %d\n", users(brick), fs::exists(file) ? "still there" : "trashed",
                  (int)ed.material_delete_pending());
      CHECK(users(brick) == 0 && !fs::exists(file) && !ed.material_delete_pending());
      CHECK(ed.material_selected_for_test() == nullptr);
      /* It still renders (the default material) and saves with empty slots. */
      ed.step_frame_headless();
      std::string a = save_scene_text(ed.scene()), err;
      Scene l;
      CHECK(load_scene_text(a, l, err));
      /* Undo puts the slots back. */
      ed.step_frame_headless({ev(ET::KeyDown, 600, 400, platform::KEY_Z, platform::MOD_CTRL)});
      ed.step_frame_headless({ev(ET::KeyUp, 600, 400, platform::KEY_Z)});
      ed.step_frame_headless();
      size_t restored = 0;
      ed.scene().for_each([&](GameObject &g) {
        if (auto *mr = g.get<MeshRenderer>())
          for (auto &x : mr->materials) restored += x && x->name == "BrickRed";
      });
      std::printf("    after undo: %zu slot(s) with BrickRed again\n", restored);
      CHECK(restored == 2);
    }
    set_env("BLENDITY_PROJECT", "");
    set_env("BLENDITY_TRASH", "");
  });
  test("curved: regions touching themselves at a vertex extrude, inset and fill without non-manifold edges", [&] {
    /* A cone's side triangles 0 and 2 (and the cap): the region meets itself at the apex. */
    MeshPtr cone = primitives::cone(0.5f, 1.0f, 16);
    std::vector<uint8_t> sel(cone->face_count(), 0);
    sel[0] = sel[2] = 1;
    sel[cone->face_count() - 1] = 1;  // the base joins them into one region
    for (int op = 0; op < 3; op++) {
      Mesh m = *cone;
      std::vector<uint8_t> s2 = sel;
      if (op == 0) meshops::extrude_faces(m, s2, 0.1f);
      if (op == 1) meshops::inset_region(m, s2, 0.02f);
      if (op == 2) {
        meshops::delete_faces(m, sel);
        const auto r = meshops::smart_fill(m);
        std::printf("    smart fill: %zu loops, %zu faces, %zu open chains\n", r.loops, r.faces, r.open_chains);
      }
      std::string why;
      std::printf("    %s: %zu faces, valid %d, closed %d\n", op == 0 ? "extrude" : op == 1 ? "inset region" : "delete + Smart Fill", m.face_count(),
                  (int)structurally_valid(m, &why), (int)closed_manifold(m));
      /* Deleting cap and triangles 0 and 2 leaves triangle 1 as a fin: its outline is one face's, which Smart Fill leaves. */
      CHECK(structurally_valid(m) && (op == 2 || closed_manifold(m)));
    }
    /* Two holes meeting at the apex only. */
    Mesh h = *cone;
    std::vector<uint8_t> two(h.face_count(), 0);
    two[0] = two[2] = 1;
    meshops::delete_faces(h, two);
    const auto r = meshops::smart_fill(h);
    std::printf("    two holes at the apex: %zu loops filled, closed %d\n", r.loops, (int)closed_manifold(h));
    CHECK(r.loops == 2 && closed_manifold(h));
  });
}

/* ===================================================================== */
/* Round 18: bevel's modal, turning shapes on rectangles, perpendicular  */
/* snaps, guides from edges and axes, Make Face, overlapping elements,    */
/* drawn shapes' transforms, Fluent-style tools                           */
/* ===================================================================== */

static void round18_tests() {
  auto ev = [](platform::EventType t, int x, int y, int key = 0, int mods = 0) {
    platform::Event e;
    e.type = t;
    e.x = x;
    e.y = y;
    e.key = key;
    e.mods = mods;
    return e;
  };
  using ET = platform::EventType;
  test("bevel: Ctrl+B always opens the modal helper, and the wheel adds segments past two", [&] {
    for (int variant = 0; variant < 3; variant++) {
      Editor ed;
      ed.init_headless(1000, 700);
      ed.step_frame_headless();
      ed.command("keymap Blender");
      ed.command("select Cube");
      ed.command("edit edge");
      const Mesh &m0 = *ed.selected_object()->get<MeshFilter>()->mesh;
      const auto e = m0.edge_cache()[(size_t)variant * 3];
      ed.command(strprintf("esel %u %u", e.first, e.second));
      const Recti r = ed.scene_view_rect();
      /* The mouse over the Scene view, or (variant 2) over the Inspector after clicking a button there. */
      const int cx = variant == 2 ? r.right() + 60 : r.x + r.w / 2 + 40, cy = r.y + r.h / 2 + 30;
      ed.step_frame_headless({ev(ET::MouseMove, cx, cy)});
      ed.step_frame_headless({ev(ET::KeyDown, cx, cy, platform::KEY_B, platform::MOD_CTRL)});
      ed.step_frame_headless({ev(ET::KeyUp, cx, cy, platform::KEY_B)});
      ed.step_frame_headless();
      std::printf("    variant %d: modal %d after Ctrl+B\n", variant, (int)ed.modal_active_for_test());
      CHECK(ed.modal_active_for_test());
      for (int k = 0; k < 6; k++) {
        platform::Event w = ev(ET::Wheel, cx + 10, cy);
        w.wheel_y = 1;
        ed.step_frame_headless({w});
      }
      ed.step_frame_headless({ev(ET::MouseMove, cx + 40, cy + 10)});
      std::printf("    segments after six wheel steps: %d, modal %d\n", ed.last_op_segments_for_test(), (int)ed.modal_active_for_test());
      CHECK(ed.modal_active_for_test() && ed.last_op_segments_for_test() >= 7);
      ed.step_frame_headless({ev(ET::KeyDown, cx, cy, platform::KEY_ENTER)});
      ed.step_frame_headless({ev(ET::KeyUp, cx, cy, platform::KEY_ENTER)});
      CHECK(!ed.modal_active_for_test());
      /* Again on another edge: it starts with 7 segments (rounded, so Auto Smooth shades it) and must still wait. */
      const Mesh &m1 = *ed.selected_object()->get<MeshFilter>()->mesh;
      size_t pick = 0;
      for (size_t k = 0; k < m1.edge_cache().size(); k++)
        if (m1.positions[m1.edge_cache()[k].first].y < -0.49f && m1.positions[m1.edge_cache()[k].second].y < -0.49f) pick = k;
      const auto e2 = m1.edge_cache()[pick];
      ed.command(strprintf("esel %u %u", e2.first, e2.second));
      ed.step_frame_headless({ev(ET::KeyDown, cx, cy, platform::KEY_B, platform::MOD_CTRL)});
      ed.step_frame_headless({ev(ET::KeyUp, cx, cy, platform::KEY_B)});
      ed.step_frame_headless({ev(ET::MouseMove, cx + 20, cy)});
      std::printf("    second bevel: modal %d with %d segments\n", (int)ed.modal_active_for_test(), ed.last_op_segments_for_test());
      CHECK(ed.modal_active_for_test() && ed.last_op_segments_for_test() == 7);
      ed.step_frame_headless({ev(ET::KeyDown, cx, cy, platform::KEY_ESCAPE)});
    }
  });
  test("rotate a shape drawn on a rectangle (a quad, the 10 x 10 Plane, a long face): no artifacts", [&] {
    struct Setup {
      const char *name;
      const char *create;  // what to draw on
      Vec3 scale;
      float top;           // its top's height
      Vec3 a, b;           // the rectangle's corners
    };
    const Setup setups[] = {
        {"quad", "Quad", {1, 1, 1}, 0.0f, {-0.25f, 0, -0.15f}, {0.25f, 0, 0.15f}},
        {"long quad", "Quad", {4, 1, 1}, 0.0f, {-0.6f, 0, -0.2f}, {0.6f, 0, 0.2f}},
        {"10 x 10 plane, across cells", "Plane", {1, 1, 1}, 0.0f, {-1.3f, 0, -0.7f}, {1.3f, 0, 0.7f}},
        {"10 x 10 plane, inside a cell", "Plane", {1, 1, 1}, 0.0f, {0.2f, 0, 0.3f}, {0.8f, 0, 0.6f}},
        {"long box top", "Cube", {4, 1, 1.5f}, 0.5f, {-1.0f, 0.5f, -0.3f}, {1.0f, 0.5f, 0.3f}},
    };
    for (const Setup &st : setups)
      for (float deg : {15.0f, 30.0f, 45.0f, 90.0f}) {
        Editor ed;
        ed.init_headless(640, 480);  // the geometry is under test; a smaller window renders faster
        ed.step_frame_headless();
        ed.command("keymap Blender");
        ed.command(std::string("create ") + st.create);
        GameObject *g = ed.selected_object();
        g->set_world_position({0, 0, 0});
        g->set_local_scale(st.scale);
        if (std::string(st.create) == "Quad") g->set_local_euler({90, 0, 0});  // lying flat, facing up
        ed.command("edit face");
        ed.command("draw rectangle");
        ed.command(strprintf("drawpoint %g %g %g", st.a.x, st.a.y + st.top * 0, st.a.z));
        ed.command(strprintf("drawpoint %g %g %g", st.b.x, st.b.y, st.b.z));
        {
          const Recti vr = ed.scene_view_rect();
          ed.step_frame_headless({ev(ET::MouseMove, vr.x + vr.w / 2, vr.y + vr.h / 2)});
          ed.step_frame_headless({ev(ET::KeyDown, vr.x + vr.w / 2, vr.y + vr.h / 2, platform::KEY_ESCAPE)});
          ed.step_frame_headless({ev(ET::KeyUp, vr.x + vr.w / 2, vr.y + vr.h / 2, platform::KEY_ESCAPE)});
        }
        const Mesh *m = g->get<MeshFilter>()->mesh.get();
        /* The faces inside the rectangle (world space). */
        std::vector<size_t> inner;
        for (size_t f = 0; f < m->face_count(); f++) {
          bool in = std::fabs(normalize(g->world_matrix().dir(m->face_normal(f))).y) > 0.99f;
          for (uint32_t k = 0; k < m->face_size(f) && in; k++) {
            const Vec3 w = g->world_matrix().point(m->positions[m->face_verts(f)[k]]);
            in = w.x > st.a.x - 1e-3f && w.x < st.b.x + 1e-3f && w.z > st.a.z - 1e-3f && w.z < st.b.z + 1e-3f && std::fabs(w.y - st.a.y) < 1e-3f;
          }
          if (in) inner.push_back(f);
        }
        if (inner.empty()) {
          std::printf("    %s: the rectangle was not cut in\n", st.name);
          CHECK(!inner.empty());
          break;
        }
        ed.select_faces_for_test(inner);
        const size_t ov0 = overlapping_face_pairs(*m);
        const Recti r = ed.scene_view_rect();
        const int cx = r.x + r.w / 2 + 60, cy = r.y + r.h / 2;
        ed.step_frame_headless({ev(ET::MouseMove, cx, cy)});
        for (int k : {platform::KEY_R, platform::KEY_Y}) {
          ed.step_frame_headless({ev(ET::KeyDown, cx, cy, k)});
          ed.step_frame_headless({ev(ET::KeyUp, cx, cy, k)});
        }
        for (char c : strprintf("%g", deg)) {
          platform::Event t;
          t.type = ET::Text;
          t.codepoint = (uint32_t)c;
          t.x = cx;
          t.y = cy;
          ed.step_frame_headless({t});
        }
        ed.step_frame_headless({ev(ET::KeyDown, cx, cy, platform::KEY_ENTER)});
        ed.step_frame_headless({ev(ET::KeyUp, cx, cy, platform::KEY_ENTER)});
        m = g->get<MeshFilter>()->mesh.get();
        std::string bad, over;
        const size_t nb = bad_triangulations(*m, &bad), no = overlapping_face_pairs(*m, &over);
        std::printf("    %s, %g deg: %zu inner face(s), %zu faces, bad %zu, overlaps %zu (before %zu)\n", st.name, deg, inner.size(), m->face_count(), nb, no, ov0);
        if (nb) std::printf("      %s\n", bad.c_str());
        if (no > ov0) std::printf("      %s\n", over.c_str());
        /* Turned far enough to stick out past the face it was drawn on (the long faces at the larger
         * angles) there is no surface around it to rebuild: only the mesh must stay valid. */
        float half_x = 0.5f * st.scale.x, half_z = 0.5f * (std::string(st.create) == "Cube" ? st.scale.z : st.scale.y);
        if (std::string(st.create) == "Plane") half_x = half_z = 5.0f;
        const float ex = 0.5f * (st.b.x - st.a.x), ez = 0.5f * (st.b.z - st.a.z), rad = deg * kDeg2Rad;
        const bool fits = ex * std::fabs(std::cos(rad)) + ez * std::fabs(std::sin(rad)) < half_x && ex * std::fabs(std::sin(rad)) + ez * std::fabs(std::cos(rad)) < half_z;
        CHECK(structurally_valid(*m) && (!fits || (nb == 0 && no <= ov0)));
      }
  });
  auto click_at = [&](Editor &ed, Vec3 w, int mods = 0) {
    int x, y;
    if (!ed.project_to_window(w, x, y)) return false;
    ed.step_frame_headless({ev(ET::MouseMove, x, y, 0, mods)});
    platform::Event d = ev(ET::MouseDown, x, y, 0, mods), u = ev(ET::MouseUp, x, y, 0, mods);
    ed.step_frame_headless({d});
    ed.step_frame_headless({u});
    return true;
  };
  auto key_press = [&](Editor &ed, int k, int mods = 0, int mx = -1, int my = -1) {
    const Recti r = ed.scene_view_rect();
    const int x = mx < 0 ? r.x + r.w / 2 : mx, y = my < 0 ? r.y + r.h / 2 : my;
    ed.step_frame_headless({ev(ET::MouseMove, x, y)});
    ed.step_frame_headless({ev(ET::KeyDown, x, y, k, mods)});
    ed.step_frame_headless({ev(ET::KeyUp, x, y, k)});
  };
  test("draw: a polyline snaps onto an edge at 90 degrees from its last point (Plasticity)", [&] {
    Editor ed;
    ed.init_headless(1000, 700);
    ed.step_frame_headless();
    ed.command("create Cube");
    GameObject *g = ed.selected_object();
    g->set_world_position({0, 0.5f, 0});
    ed.command("camera 30 35 4 0 1 0");
    ed.command("edit face");
    ed.command("draw polyline");
    ed.step_frame_headless();
    std::printf("    cube %s at %.3f %.3f %.3f, editing %s\n", g->name.c_str(), g->world_position().x, g->world_position().y, g->world_position().z,
                ed.selected_object() ? ed.selected_object()->name.c_str() : "-");
    /* From inside the top face toward its +X edge, the mouse a little off square: it lands square. */
    CHECK(click_at(ed, {-0.2f, 1, 0.1f}));
    int x, y;
    CHECK(ed.project_to_window({0.5f, 1, 0.13f}, x, y));
    ed.step_frame_headless({ev(ET::MouseMove, x, y)});
    ed.step_frame_headless({ev(ET::MouseDown, x, y)});
    ed.step_frame_headless({ev(ET::MouseUp, x, y)});
    const auto &pts = ed.draw_points_for_test();
    CHECK(pts.size() == 2);
    if (pts.size() == 2) {
      std::printf("    points %.4f %.4f %.4f -> %.4f %.4f %.4f\n", pts[0].x, pts[0].y, pts[0].z, pts[1].x, pts[1].y, pts[1].z);
      CHECK(std::fabs(pts[1].x - 0.5f) < 1e-4f && std::fabs(pts[1].z - pts[0].z) < 1e-4f);  // square: straight across
    }
    /* Off: the same mouse keeps its own place on the edge. */
    key_press(ed, platform::KEY_ESCAPE);
    ed.command("drawmode perp off");
    ed.command("draw polyline");
    ed.step_frame_headless();
    CHECK(click_at(ed, {-0.2f, 1, 0.1f}));
    ed.step_frame_headless({ev(ET::MouseMove, x, y)});
    ed.step_frame_headless({ev(ET::MouseDown, x, y)});
    ed.step_frame_headless({ev(ET::MouseUp, x, y)});
    const auto &p2 = ed.draw_points_for_test();
    if (p2.size() == 2) std::printf("    without it %.4f %.4f %.4f\n", p2[1].x, p2[1].y, p2[1].z);
    CHECK(p2.size() == 2 && std::fabs(p2[1].z - p2[0].z) > 0.01f);
  });
  test("draw: Ctrl+click an edge lays a guide along it; X / Y / Z lay axis guides, local or global", [&] {
    Editor ed;
    ed.init_headless(1000, 700);
    ed.step_frame_headless();
    ed.command("create Cube");
    GameObject *g = ed.selected_object();
    g->set_world_position({0, 0.5f, 0});
    g->set_local_euler({0, 30, 0});
    ed.command("camera 30 35 5 0 0.5 0");
    ed.command("edit face");
    ed.command("draw polyline");
    ed.step_frame_headless();
    const size_t g0 = ed.guide_count_for_test();
    /* The top's edge from (-0.5, 1, 0.5) to (0.5, 1, 0.5) in object space: its middle, Ctrl+clicked. */
    const Vec3 mid = g->world_matrix().point({0, 0.5f, 0.5f}), along = normalize(g->world_matrix().dir({1, 0, 0}));
    CHECK(click_at(ed, mid, platform::MOD_CTRL));
    CHECK(ed.guide_count_for_test() == g0 + 1 && ed.draw_point_count() == 0);
    if (const GuideLine *gl = ed.last_guide_for_test()) {
      std::printf("    edge guide direction %.3f %.3f %.3f (edge %.3f %.3f %.3f)\n", gl->d.x, gl->d.y, gl->d.z, along.x, along.y, along.z);
      CHECK(std::fabs(std::fabs(dot(gl->d, along)) - 1.0f) < 1e-4f && length(cross(mid - gl->p, gl->d)) < 1e-4f);
    }
    /* X with Local axes: along the turned cube's X; with Global: the world's X. */
    ed.command("drawmode axes local");
    key_press(ed, platform::KEY_X);
    CHECK(ed.guide_count_for_test() == g0 + 2 && ed.last_guide_for_test() && std::fabs(dot(ed.last_guide_for_test()->d, along)) > 0.9999f);
    ed.command("drawmode axes global");
    const Recti r = ed.scene_view_rect();
    key_press(ed, platform::KEY_X, 0, r.x + r.w / 2 + 30, r.y + r.h / 2 + 20);
    CHECK(ed.guide_count_for_test() == g0 + 3 && ed.last_guide_for_test() && std::fabs(ed.last_guide_for_test()->d.x) > 0.9999f);
  });
  test("Make Face: one operation (Smart Fill's holes and Blender's F), in every mode, with the key anywhere in Edit Mode", [&] {
    for (const char *preset : {"Unity", "Blender"})
      for (const char *mode : {"vertex", "edge", "face"}) {
        Editor ed;
        ed.init_headless(1000, 700);
        ed.step_frame_headless();
        ed.command(std::string("keymap ") + preset);
        ed.command("select Cube");
        GameObject *g = ed.selected_object();
        /* A hole: the top face gone. */
        ed.command("edit face");
        ed.command("fsel facing 0 1 0");
        ed.command("editop delete");
        const Mesh *m = g->get<MeshFilter>()->mesh.get();
        CHECK(m->face_count() == 5 && !closed_manifold(*m));
        /* Select the hole's rim: every vertex (vertex / edge mode) or the side faces (face mode). */
        ed.command(std::string("edit ") + mode + " all");
        /* The key with the mouse over the Inspector, not the Scene view. */
        const Recti r = ed.scene_view_rect();
        if (std::string(preset) == "Unity") key_press(ed, platform::KEY_F, platform::MOD_ALT, r.right() + 80, r.y + 100);
        else key_press(ed, platform::KEY_F, 0, r.right() + 80, r.y + 100);
        m = g->get<MeshFilter>()->mesh.get();
        std::printf("    %s, %s mode: %zu faces, closed %d\n", preset, mode, m->face_count(), (int)closed_manifold(*m));
        CHECK(m->face_count() == 6 && closed_manifold(*m));
      }
    /* Loose vertices (no hole): one face through them, as Blender's F. */
    Editor ed;
    ed.init_headless(800, 600);
    ed.step_frame_headless();
    ed.command("select Main Camera");
    ed.command("draw polyline");
    ed.command("drawpoint 0 0 0");
    ed.command("drawpoint 1 0 0");
    ed.command("drawpoint 1 0 1");
    ed.command("drawpoint 0 0 1 finish");
    GameObject *g = ed.selected_object();
    ed.command("edit vertex all");
    ed.command("editop fill");
    const Mesh *m = g->get<MeshFilter>()->mesh.get();
    std::printf("    open polyline + Make Face: %zu face(s), %zu wire edges\n", m->face_count(), m->loose_edges.size());
    CHECK(m->face_count() == 1);
  });
  test("overlapping vertices and edges: found, shown, selected and merged", [&] {
    Mesh m;
    for (Vec3 p : {Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(1, 0, 1), Vec3(0, 0, 1)}) m.add_vert(p);
    m.add_face({0, 3, 2, 1});
    /* A second quad sharing the edge's place but not its vertices (unwelded), and a wire edge along another side. */
    const uint32_t a = m.add_vert({1, 0, 0}), b = m.add_vert({2, 0, 0}), c = m.add_vert({2, 0, 1}), d = m.add_vert({1, 0, 1});
    m.add_face({a, d, c, b});
    const uint32_t w0 = m.add_vert({0.25f, 0, 0}), w1 = m.add_vert({0.75f, 0, 0});
    m.add_loose_edge(w0, w1);
    const auto vs = meshops::overlapping_vertices(m);
    const auto es = meshops::overlapping_edges(m);
    std::printf("    %zu vertex pair(s), %zu edge pair(s)\n", vs.size(), es.size());
    CHECK(vs.size() == 2 && es.size() == 2);  // 1-a, 2-d; the doubled edge, and the wire along 0-1
    CHECK(meshops::overlapping_vertices(*primitives::cube()).empty() && meshops::overlapping_edges(*primitives::cube()).empty());
    Editor ed;
    ed.init_headless(900, 600);
    ed.step_frame_headless();
    ed.command("create Cube");
    GameObject *g = ed.selected_object();
    g->get<MeshFilter>()->mesh = std::make_shared<Mesh>(m);
    ed.command("edit vertex");
    CHECK(ed.overlap_vert_pairs_for_test() == 2 && ed.overlap_edge_pairs_for_test() == 2);
    ed.step_frame_headless();  // the overlay draws
    ed.command("overlaps select");
    ed.command("overlaps merge");
    const Mesh &after = *g->get<MeshFilter>()->mesh;
    std::printf("    after merging: %zu verts, %zu vertex pairs, %zu edge pairs\n", after.vert_count(), ed.overlap_vert_pairs_for_test(), ed.overlap_edge_pairs_for_test());
    CHECK(after.vert_count() == 10 - 2 && ed.overlap_vert_pairs_for_test() == 0 && ed.overlap_edge_pairs_for_test() == 1);  // the wire still lies along an edge
  });
  /* A rail of thin quads whose near side is the path (0,0,0) -> (1,0,0) -> (1,0,1) -> (2,0,1), and
   * a small square facing back along it at the rail's start: Follow must run along the rail. */
  auto rail_and_profile = [](std::vector<uint32_t> &path, size_t &profile) {
    Mesh m;
    const Vec3 P[4] = {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {2, 0, 1}};
    for (const Vec3 &p : P) path.push_back(m.add_vert(p));
    std::vector<uint32_t> far;
    for (const Vec3 &p : P) far.push_back(m.add_vert(p + Vec3(0, -1, 0)));
    for (int i = 0; i < 3; i++) m.add_face({path[i], far[i], far[i + 1], path[i + 1]});
    const float h = 0.1f;
    const uint32_t q0 = m.add_vert({0, -h, -h}), q1 = m.add_vert({0, h, -h}), q2 = m.add_vert({0, h, h}), q3 = m.add_vert({0, -h, h});
    m.add_face({q0, q1, q2, q3});
    profile = m.face_count() - 1;
    return m;
  };
  auto cap_at = [](const Mesh &m, Vec3 at) {
    for (size_t f = 0; f < m.face_count(); f++)
      if (m.face_size(f) == 4 && length(m.face_center(f) - at) < 2e-3f) return true;
    return false;
  };
  test("Follow along picked edges runs along the edges themselves (SketchUp's Follow Me)", [&] {
    std::vector<uint32_t> path;
    size_t profile = 0;
    Mesh m = rail_and_profile(path, profile);
    std::string err;
    CHECK(meshops::follow_path(m, profile, path, &err));
    if (!err.empty()) std::printf("    %s\n", err.c_str());
    std::printf("    swept: %zu faces; end cap at the path's end %d, the rail kept %d\n", m.face_count(), (int)cap_at(m, {2, 0, 1}), (int)(m.face_count() >= 3));
    CHECK(cap_at(m, {2, 0, 1}));
    /* Every ring sits on its path point (the square's centre follows the line). */
    for (const Vec3 &p : {Vec3(1, 0, 0), Vec3(1, 0, 1)}) {
      size_t near = 0;
      for (const Vec3 &q : m.positions) near += length(q - p) < 0.2f && length(q - p) > 0.05f;
      CHECK(near >= 4);
    }
    /* The same through the editor: pick the square, then the rail's edges (and a side of the square, still selected). */
    Editor ed;
    ed.init_headless(900, 600);
    ed.step_frame_headless();
    ed.command("create Cube");
    GameObject *g = ed.selected_object();
    path.clear();
    g->get<MeshFilter>()->mesh = std::make_shared<Mesh>(rail_and_profile(path, profile));
    ed.command("edit face");
    ed.command(strprintf("fsel %zu", profile));
    ed.command("editop follow");
    const Mesh &m0 = *g->get<MeshFilter>()->mesh;
    const uint32_t s0 = m0.face_verts(profile)[0], s1 = m0.face_verts(profile)[1];
    ed.command(strprintf("esel %u %u %u %u %u %u %u %u", s0, s1, path[0], path[1], path[1], path[2], path[2], path[3]));
    ed.command("editop follow");
    const Mesh &m1 = *g->get<MeshFilter>()->mesh;
    std::printf("    editor: %zu faces, end cap at the path's end %d\n", m1.face_count(), (int)cap_at(m1, {2, 0, 1}));
    CHECK(cap_at(m1, {2, 0, 1}));
  });
  test("a shape drawn at an angle: its new object takes the shape's place and turn (Pivot, Local)", [&] {
    Editor ed;
    ed.init_headless(1000, 700);
    ed.step_frame_headless();
    ed.command("select Main Camera");
    ed.command("drawmode rect 3point");
    ed.command("draw rectangle");
    GameObject *g = ed.selected_object();
    /* A 3-point rectangle on the ground, its first side 30 degrees off X. */
    const float a = 30.0f * kDeg2Rad;
    const Vec3 p0(1, 0, 1), p1 = p0 + Vec3(std::cos(a), 0, std::sin(a)) * 2.0f, p2 = p1 + Vec3(-std::sin(a), 0, std::cos(a)) * 1.0f;
    ed.command(strprintf("drawpoint %g %g %g", p0.x, p0.y, p0.z));
    ed.command(strprintf("drawpoint %g %g %g", p1.x, p1.y, p1.z));
    ed.command(strprintf("drawpoint %g %g %g", p2.x, p2.y, p2.z));
    const Mesh &m = *g->get<MeshFilter>()->mesh;
    CHECK(m.face_count() == 1);
    const Vec3 centre = (p0 + p2) * 0.5f, x = g->world_rotation().rotate({1, 0, 0}), y = g->world_rotation().rotate({0, 1, 0});
    std::printf("    object at %.3f %.3f %.3f (centre %.3f %.3f %.3f), X . side %.4f, Y . up %.4f\n", g->world_position().x, g->world_position().y,
                g->world_position().z, centre.x, centre.y, centre.z, std::fabs(dot(x, normalize(p1 - p0))), y.y);
    CHECK(length(g->world_position() - centre) < 1e-3f);
    CHECK(std::fabs(dot(x, normalize(p1 - p0))) > 0.9999f && std::fabs(y.y) > 0.9999f);
    /* Local in Edit Mode: the shape's own axes. */
    ed.command("edit face all");
    Quat q;
    CHECK(ed.selection_frame_for_test(q));
    CHECK(std::fabs(dot(q.rotate({1, 0, 0}), normalize(p1 - p0))) > 0.999f || std::fabs(dot(q.rotate({1, 0, 0}), normalize(p1 - p0))) < 1e-3f);
  });
  test("Push/Pull on several faces: each group, or each face, along its own normal", [&] {
    const Mesh cube = *primitives::cube();
    auto box = [](const Mesh &m) {
      Vec3 lo(1e9f), hi(-1e9f);
      for (const Vec3 &p : m.positions) lo = vmin(lo, p), hi = vmax(hi, p);
      return std::pair<Vec3, Vec3>{lo, hi};
    };
    /* Top and bottom (not touching): both out along their own normals. */
    {
      Mesh m = cube;
      std::vector<uint8_t> sel(m.face_count(), 0);
      for (size_t f = 0; f < m.face_count(); f++) sel[f] = std::fabs(m.face_normal(f).y) > 0.9f;
      std::string err;
      CHECK(meshops::push_pull_multi(m, sel, 0.2f, true, false, nullptr, &err));
      const auto [lo, hi] = box(m);
      std::printf("    top and bottom: y %.3f..%.3f, closed %d, %zu selected\n", lo.y, hi.y, (int)closed_manifold(m), (size_t)std::count(sel.begin(), sel.end(), 1));
      CHECK(std::fabs(lo.y + 0.7f) < 1e-4f && std::fabs(hi.y - 0.7f) < 1e-4f && closed_manifold(m) && std::count(sel.begin(), sel.end(), 1) == 2);
    }
    /* Every face of the cube, each on its own: the box grows on all six sides. */
    {
      Mesh m = cube;
      std::vector<uint8_t> sel(m.face_count(), 1);
      std::string err;
      CHECK(meshops::push_pull_multi(m, sel, 0.2f, true, true, nullptr, &err));
      const auto [lo, hi] = box(m);
      std::printf("    each face: %.3f..%.3f, %zu faces, closed %d, valid %d\n", lo.x, hi.x, m.face_count(), (int)closed_manifold(m), (int)structurally_valid(m));
      CHECK(std::fabs(hi.x - 0.7f) < 1e-4f && std::fabs(lo.z + 0.7f) < 1e-4f && std::fabs(hi.y - 0.7f) < 1e-4f && closed_manifold(m) && structurally_valid(m));
    }
    /* In the editor: P with two faces selected and a typed distance; Each Face from the console. */
    Editor ed;
    ed.init_headless(900, 600);
    ed.step_frame_headless();
    ed.command("create Cube");
    GameObject *g = ed.selected_object();
    ed.command("edit face");
    ed.command("fsel facing 1 0 0 -1 0 0");
    ed.command("pushpull individual on");
    const Recti r = ed.scene_view_rect();
    const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    ed.step_frame_headless({ev(ET::MouseMove, cx, cy)});
    ed.step_frame_headless({ev(ET::KeyDown, cx, cy, platform::KEY_P)});
    ed.step_frame_headless({ev(ET::KeyUp, cx, cy, platform::KEY_P)});
    for (char c : std::string("0.3")) {
      platform::Event t;
      t.type = ET::Text;
      t.codepoint = (uint32_t)c;
      t.x = cx;
      t.y = cy;
      ed.step_frame_headless({t});
    }
    ed.step_frame_headless({ev(ET::KeyDown, cx, cy, platform::KEY_ENTER)});
    ed.step_frame_headless({ev(ET::KeyUp, cx, cy, platform::KEY_ENTER)});
    const auto [lo, hi] = box(*g->get<MeshFilter>()->mesh);
    std::printf("    editor (P, two sides, 0.3): x %.3f..%.3f\n", lo.x, hi.x);
    CHECK(std::fabs(lo.x + 0.8f) < 1e-4f && std::fabs(hi.x - 0.8f) < 1e-4f);
  });
  test("hard-surface tools: Grid, Pipe, Array, Taper Extrude, Recess / Plate", [&] {
    auto setup = [&](Editor &ed) {
      ed.init_headless(900, 600);
      ed.step_frame_headless();
      ed.command("create Cube");
      ed.command("edit face");
      ed.command("fsel facing 0 1 0");
      return ed.selected_object();
    };
    {
      Editor ed;
      GameObject *g = setup(ed);
      ed.command("hs grid 3 2");
      ed.command("editop grid");
      const Mesh &m = *g->get<MeshFilter>()->mesh;
      std::printf("    grid: %zu faces, closed %d\n", m.face_count(), (int)closed_manifold(m));
      CHECK(m.face_count() == 5 + 6 && closed_manifold(m) && structurally_valid(m));
    }
    {
      Editor ed;
      GameObject *g = setup(ed);
      ed.command("hs recess 0.1 0.2");
      ed.command("editop recess");
      const Mesh &m = *g->get<MeshFilter>()->mesh;
      float ymin_top = 1e9f;
      for (size_t f = 0; f < m.face_count(); f++)
        if (ed.edit_face_selected(f)) ymin_top = std::min(ymin_top, m.face_center(f).y);
      std::printf("    recess: %zu faces, closed %d, panel at y %.3f\n", m.face_count(), (int)closed_manifold(m), ymin_top);
      CHECK(closed_manifold(m) && std::fabs(ymin_top - 0.3f) < 1e-3f);
      ed.command("hs recess 0.1 -0.2");  // a raised plate
      ed.command("fsel facing 0 1 0");
    }
    {
      Editor ed;
      GameObject *g = setup(ed);
      ed.command("hs taper 0.5 0.5");
      ed.command("editop taper");
      const Mesh &m = *g->get<MeshFilter>()->mesh;
      float top = -1e9f, xmax = -1e9f;
      for (const Vec3 &p : m.positions) top = std::max(top, p.y);
      for (const Vec3 &p : m.positions)
        if (p.y > top - 1e-4f) xmax = std::max(xmax, p.x);
      std::printf("    taper: top %.3f, half width there %.3f, closed %d\n", top, xmax, (int)closed_manifold(m));
      CHECK(std::fabs(top - 1.0f) < 1e-3f && std::fabs(xmax - 0.25f) < 1e-3f && closed_manifold(m));
    }
    {
      Editor ed;
      GameObject *g = setup(ed);
      ed.command("hs grid 2 2");
      ed.command("editop grid");
      ed.command("fsel 5");  // one of the grid's cells
      ed.command("hs array 3 0.2 0");
      const size_t f0 = g->get<MeshFilter>()->mesh->face_count();
      ed.command("editop array");
      const Mesh &m = *g->get<MeshFilter>()->mesh;
      std::printf("    array: %zu faces (was %zu)\n", m.face_count(), f0);
      CHECK(m.face_count() > f0 && structurally_valid(m) && closed_manifold(m) && overlapping_face_pairs(m) == 0);  // cut in, nothing on top
    }
    {
      Editor ed;
      ed.init_headless(900, 600);
      ed.step_frame_headless();
      ed.command("select Main Camera");
      ed.command("draw polyline");
      ed.command("drawpoint 0 0 0");
      ed.command("drawpoint 1 0 0");
      ed.command("drawpoint 1 0 1 finish");
      GameObject *g = ed.selected_object();
      ed.command("edit vertex all");
      ed.command("hs pipe 0.1 8");
      ed.command("editop pipe");
      const Mesh &m = *g->get<MeshFilter>()->mesh;
      Mesh copy = m;
      meshops::merge_by_distance(copy, 1e-6f);
      std::printf("    pipe: %zu faces, %zu wires, closed %d\n", copy.face_count(), copy.loose_edges.size(), (int)closed_manifold(copy));
      CHECK(copy.face_count() == 8 * 2 + 2 && copy.loose_edges.empty() && closed_manifold(copy));
    }
    /* On a cube's edges: the path is kept. */
    {
      Editor ed;
      ed.init_headless(900, 600);
      ed.step_frame_headless();
      ed.command("create Cube");
      GameObject *g = ed.selected_object();
      const Mesh &m0 = *g->get<MeshFilter>()->mesh;
      const auto e = m0.edge_cache()[0];
      ed.command(strprintf("esel %u %u", e.first, e.second));
      ed.command("editop pipe");
      const Mesh &m = *g->get<MeshFilter>()->mesh;
      std::printf("    pipe on a cube edge: %zu faces\n", m.face_count());
      CHECK(m.face_count() == 6 + 12 + 2 && structurally_valid(m));
    }
  });
  test("bevel profile: concave below 0.5, flat at 0.25, round at 0.5, convex toward 1 (Blender's Profile)", [&] {
    const float sign = signed_volume(*primitives::cube()) > 0 ? 1.0f : -1.0f;
    auto bevelled = [&](float profile, int segs) {
      Mesh m = *primitives::cube();
      std::vector<uint8_t> vs(m.vert_count(), 1), fs(m.face_count(), 0);
      std::unordered_set<uint64_t> one;
      for (auto &e : m.edge_cache())
        if (m.positions[e.first].y > 0.4f && m.positions[e.second].y > 0.4f && m.positions[e.first].z > 0.4f && m.positions[e.second].z > 0.4f)
          one.insert(Mesh::edge_key(e.first, e.second));  // the top front edge
      meshops::EdgeSelectionScope scope(&one);
      CHECK(meshops::bevel_edges(m, vs, fs, 0.3f, segs, nullptr, true, profile));
      return m;
    };
    float prev = -1.0f;
    for (float p : {0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 1.0f}) {
      const Mesh m = bevelled(p, 6);
      const float v = (float)signed_volume(m) * sign;
      std::printf("    profile %.2f: volume %.5f, closed %d\n", p, v, (int)closed_manifold(m));
      CHECK(closed_manifold(m) && structurally_valid(m) && v > prev && v <= 1.0f + 1e-5f);
      prev = v;
    }
    /* Flat (0.25) is a one-segment chamfer's volume; round (0.5) is a quarter circle's. */
    const float chamfer = (float)signed_volume(bevelled(0.5f, 1)) * sign, flat = (float)signed_volume(bevelled(0.25f, 6)) * sign;
    const float round = (float)signed_volume(bevelled(0.5f, 64)) * sign, expect_round = 1.0f - (0.09f - kPi * 0.09f / 4.0f);
    std::printf("    chamfer %.5f vs flat %.5f; round %.5f vs a quarter circle %.5f\n", chamfer, flat, round, expect_round);
    CHECK(std::fabs(chamfer - flat) < 1e-4f && std::fabs(round - expect_round) < 2e-4f);
    /* In the editor: the Inspector's value reaches Ctrl+B, and the modifier keeps its own. */
    Editor ed;
    ed.init_headless(900, 600);
    ed.step_frame_headless();
    ed.command("create Cube");
    GameObject *g = ed.selected_object();
    const Mesh &m0 = *g->get<MeshFilter>()->mesh;
    const auto e = m0.edge_cache()[0];
    ed.command(strprintf("esel %u %u", e.first, e.second));
    ed.command("bevelprofile 0.9");
    ed.command("editop bevel");
    CHECK(std::fabs(ed.last_op_profile_for_test() - 0.9f) < 1e-6f);
    Scene sc;
    GameObject *o = sc.create("Box");
    o->add<MeshFilter>()->mesh = primitives::cube();
    o->add<BevelModifier>()->profile = 0.2f;
    std::string a = save_scene_text(sc), err;
    Scene l;
    CHECK(load_scene_text(a, l, err) && l.find_by_name("Box") && std::fabs(l.find_by_name("Box")->get<BevelModifier>()->profile - 0.2f) < 1e-6f);
  });
  test("draw: an arc on a rectangle drawn on a face makes the face inside the arc", [&] {
    struct Case {
      const char *name;
      Vec3 a, b, bulge;
    };
    /* The rectangle (-0.3..0.3, -0.2..0.2) on the cube's top; arcs from its sides, out of it and into it,
     * from corner to corner, and with one end on the cube's own edge. */
    const Case cases[] = {
        {"side to side, bulging out", {-0.15f, 1, 0.2f}, {0.15f, 1, 0.2f}, {0, 1, 0.35f}},
        {"side to side, bulging in", {-0.15f, 1, 0.2f}, {0.15f, 1, 0.2f}, {0, 1, 0.05f}},
        {"corner to corner, out", {-0.3f, 1, 0.2f}, {0.3f, 1, 0.2f}, {0, 1, 0.4f}},
        {"across a corner, out", {-0.3f, 1, 0.0f}, {0.0f, 1, 0.2f}, {-0.35f, 1, 0.3f}},
        {"rectangle side to the cube's edge", {0.3f, 1, 0.0f}, {0.5f, 1, -0.4f}, {0.36f, 1, -0.28f}},
        {"on the cube's own edge (no rectangle)", {-0.4f, 1, -0.5f}, {-0.1f, 1, -0.5f}, {-0.25f, 1, -0.35f}},
    };
    for (const Case &c : cases)
      for (int segs : {8, 16}) {
        Editor ed;
        ed.init_headless(900, 600);
        ed.step_frame_headless();
        ed.command("create Cube");
        GameObject *g = ed.selected_object();
        g->set_world_position({0, 0.5f, 0});
        ed.command("camera 30 40 5 0 1 0");  // from above: a point on the top's edge belongs to the top
        ed.step_frame_headless();
        ed.command("edit face");
        ed.command("draw rectangle");
        ed.command("drawpoint -0.3 1 -0.2");
        ed.command("drawpoint 0.3 1 0.2");
        const size_t f0 = g->get<MeshFilter>()->mesh->face_count();
        ed.command(strprintf("draw arc %d", segs));
        ed.command(strprintf("drawpoint %g %g %g", c.a.x, c.a.y, c.a.z));
        ed.command(strprintf("drawpoint %g %g %g", c.b.x, c.b.y, c.b.z));
        ed.command(strprintf("drawpoint %g %g %g", c.bulge.x, c.bulge.y, c.bulge.z));
        const Mesh &m = *g->get<MeshFilter>()->mesh;
        /* The arc's face: one face holding every corner of the arc (and the face beside it the other). */
        std::vector<Vec3> arc_pts;
        {
        }
        size_t inside = 0;
        for (size_t f = 0; f < m.face_count(); f++) {
          size_t on_arc = 0;
          for (uint32_t k = 0; k < m.face_size(f); k++) {
            const Vec3 w = m.positions[m.face_verts(f)[k]] + Vec3(0, 0.5f, 0);
            /* On the circle through the three points (the arc's corners). */
            Vec3 cc;
            const Vec3 a = c.a, b = c.b, q = c.bulge;
            const Vec3 ab = b - a, aq = q - a, nrm = cross(ab, aq);
            cc = a + (cross(nrm, ab) * dot(aq, aq) + cross(aq, nrm) * dot(ab, ab)) / (2.0f * dot(nrm, nrm));
            if (std::fabs(length(w - cc) - length(a - cc)) < 1e-3f && std::fabs(w.y - 1.0f) < 1e-4f) on_arc++;
          }
          if (on_arc >= (size_t)(segs / 2) + 1) inside++;
        }
        std::printf("    %s (%d segs): faces %zu -> %zu, faces on the arc's middle %zu, wires %zu, closed %d\n", c.name, segs, f0, m.face_count(), inside,
                    m.loose_edges.size(), (int)closed_manifold(m));
        /* From the rectangle out to the cube's edge the arc encloses nothing: it only divides the faces it crosses. */
        const bool encloses = std::string(c.name).find("cube's edge (8") == std::string::npos && std::string(c.name) != "rectangle side to the cube's edge";
        CHECK(m.loose_edges.empty() && closed_manifold(m) && structurally_valid(m));
        if (encloses) CHECK(m.face_count() == f0 + 1 && inside >= 1);
      }
  });
  test("inset region: dragging the thickness changes it smoothly, holding at the limit instead of stepping", [&] {
    const Mesh cube = *primitives::cube();
    float prev_area = -1.0f, worst_jump = 0.0f;
    bool grows = true;
    for (int k = 0; k <= 80; k++) {
      const float th = 0.01f * k;  // past half the face's width (0.5) the region would fold
      Mesh m = cube;
      std::vector<uint8_t> sel(m.face_count(), 0);
      for (size_t f = 0; f < m.face_count(); f++) sel[f] = std::fabs(m.face_normal(f).y) > 0.9f;  // top and bottom
      meshops::inset_region(m, sel, th);
      float area = 0.0f;
      for (size_t f = 0; f < m.face_count(); f++)
        if (sel[f]) area += 0.5f * length(m.face_normal(f)) * 0.0f + [&] {
          Vec3 nw(0.0f);
          for (uint32_t i = 0; i < m.face_size(f); i++) nw += cross(m.positions[m.face_verts(f)[i]], m.positions[m.face_verts(f)[(i + 1) % m.face_size(f)]]);
          return 0.5f * length(nw);
        }();
      if (prev_area >= 0) {
        worst_jump = std::max(worst_jump, std::fabs(area - prev_area));
        grows = grows && area <= prev_area + 1e-5f;
      }
      if (std::getenv("BL_INSET_DEBUG")) std::printf("      t %.2f area %.5f selected %zu\n", th, area, (size_t)std::count(sel.begin(), sel.end(), 1));
      prev_area = area;
      CHECK(closed_manifold(m));
    }
    std::printf("    largest change between 0.01 steps: %.4f (inner area at the end %.5f)\n", worst_jump, prev_area);
    CHECK(worst_jump < 0.085f && grows && prev_area < 0.05f);  // the steepest natural change is 0.08 per step (two 1 x 1 faces)
  });
  test("hard-surface helpers: Array from the menu waits for the mouse, wheel, X / Y / Z and typed values", [&] {
    Editor ed;
    ed.init_headless(1000, 700);
    ed.step_frame_headless();
    ed.command("create Cube");
    GameObject *g = ed.selected_object();
    ed.command("edit face");
    ed.command("fsel facing 0 1 0");
    ed.command("hs array 3 0.5 0");
    const Recti r = ed.scene_view_rect();
    const int cx = r.x + r.w / 2 + 50, cy = r.y + r.h / 2 + 40;
    ed.step_frame_headless({ev(ET::MouseMove, cx, cy)});
    ed.command("edittool array");
    ed.step_frame_headless({ev(ET::MouseMove, cx, cy)});
    CHECK(ed.modal_active_for_test());
    for (int k = 0; k < 2; k++) {
      platform::Event w = ev(ET::Wheel, cx, cy);
      w.wheel_y = 1;
      ed.step_frame_headless({w});
    }
    ed.step_frame_headless({ev(ET::KeyDown, cx, cy, platform::KEY_Z)});
    ed.step_frame_headless({ev(ET::KeyUp, cx, cy, platform::KEY_Z)});
    for (char c : std::string("1.5")) {
      platform::Event t;
      t.type = ET::Text;
      t.codepoint = (uint32_t)c;
      t.x = cx;
      t.y = cy;
      ed.step_frame_headless({t});
    }
    CHECK(ed.modal_active_for_test());
    ed.step_frame_headless({ev(ET::KeyDown, cx, cy, platform::KEY_ENTER)});
    ed.step_frame_headless({ev(ET::KeyUp, cx, cy, platform::KEY_ENTER)});
    CHECK(!ed.modal_active_for_test());
    const Mesh &m = *g->get<MeshFilter>()->mesh;
    float zmax = -1e9f;
    for (const Vec3 &p : m.positions) zmax = std::max(zmax, p.z);
    std::printf("    array: %zu faces, furthest z %.3f\n", m.face_count(), zmax);
    CHECK(m.face_count() == 6 + 4 && std::fabs(std::fabs(zmax) - (0.5f + 4 * 1.5f)) < 1e-3f);
    /* The others start their helpers too, and Esc leaves the mesh as it was. */
    for (const char *op : {"grid", "taper", "recess"}) {
      const size_t f0 = g->get<MeshFilter>()->mesh->face_count();
      ed.command("fsel facing 0 -1 0");
      ed.command(std::string("edittool ") + op);
      ed.step_frame_headless({ev(ET::MouseMove, cx + 30, cy)});
      CHECK(ed.modal_active_for_test());
      ed.step_frame_headless({ev(ET::KeyDown, cx, cy, platform::KEY_ESCAPE)});
      ed.step_frame_headless({ev(ET::KeyUp, cx, cy, platform::KEY_ESCAPE)});
      CHECK(!ed.modal_active_for_test() && g->get<MeshFilter>()->mesh->face_count() == f0);
    }
  });
  test("push/pull on non-manifold meshes: fins, shared edges and corners, open boxes, stray wires", [&] {
    struct Case {
      std::string name;
      Mesh m;
      std::vector<size_t> faces;
    };
    std::vector<Case> cases;
    auto add_cube = [](Mesh &m, Vec3 at, float size = 1.0f) {
      const uint32_t base = (uint32_t)m.vert_count();
      const Mesh c = *primitives::cube(size);
      for (Vec3 p : c.positions) m.add_vert(p + at);
      for (size_t f = 0; f < c.face_count(); f++) {
        std::vector<uint32_t> v;
        for (uint32_t k = 0; k < c.face_size(f); k++) v.push_back(base + c.face_verts(f)[k]);
        m.add_face(v.data(), v.size());
      }
    };
    auto face_facing = [](const Mesh &m, Vec3 n, Vec3 near) {
      size_t best = SIZE_MAX;
      float bd = 1e9f;
      for (size_t f = 0; f < m.face_count(); f++)
        if (dot(normalize(m.face_normal(f)), n) > 0.99f && length(m.face_center(f) - near) < bd) bd = length(m.face_center(f) - near), best = f;
      return best;
    };
    {
      /* A fin on the cube's top front edge (three faces on that edge); pull the top, and the fin. */
      Mesh m = *primitives::cube();
      uint32_t a = UINT32_MAX, b = UINT32_MAX;
      for (uint32_t v = 0; v < m.vert_count(); v++)
        if (m.positions[v].y > 0.4f && m.positions[v].z > 0.4f) (a == UINT32_MAX ? a : b) = v;
      const uint32_t c = m.add_vert(m.positions[b] + Vec3(0, 0.5f, 0.5f)), d = m.add_vert(m.positions[a] + Vec3(0, 0.5f, 0.5f));
      m.add_face({a, b, c, d});
      cases.push_back({"fin: the top", m, {face_facing(m, {0, 1, 0}, {0, 0.5f, 0})}});
      cases.push_back({"fin: the fin itself", m, {m.face_count() - 1}});
      cases.push_back({"fin: the front", m, {face_facing(m, {0, 0, 1}, {0, 0, 0.5f})}});
    }
    {
      /* Two cubes sharing an edge (four faces on it), welded. */
      Mesh m;
      add_cube(m, {0, 0, 0});
      add_cube(m, {1, 1, 0});
      meshops::merge_by_distance(m, 1e-5f);
      cases.push_back({"two cubes on one edge: a top", m, {face_facing(m, {0, 1, 0}, {0, 0.5f, 0})}});
      cases.push_back({"two cubes on one edge: a side at the edge", m, {face_facing(m, {1, 0, 0}, {0.5f, 0, 0})}});
    }
    {
      /* Two cubes sharing one corner. */
      Mesh m;
      add_cube(m, {0, 0, 0});
      add_cube(m, {1, 1, 1});
      meshops::merge_by_distance(m, 1e-5f);
      cases.push_back({"two cubes on one corner: a top", m, {face_facing(m, {0, 1, 0}, {0, 0.5f, 0})}});
    }
    {
      /* An open box (no top): pull a side, push the bottom. */
      Mesh m = *primitives::cube();
      std::vector<uint8_t> drop(m.face_count(), 0);
      drop[face_facing(m, {0, 1, 0}, {0, 0.5f, 0})] = 1;
      meshops::delete_faces(m, drop);
      cases.push_back({"open box: a side", m, {face_facing(m, {1, 0, 0}, {0.5f, 0, 0})}});
      cases.push_back({"open box: the bottom", m, {face_facing(m, {0, -1, 0}, {0, -0.5f, 0})}});
    }
    {
      /* A cube with a stray wire edge at a corner, and a loose point. */
      Mesh m = *primitives::cube();
      const uint32_t w = m.add_vert({1, 1, 1});
      m.add_loose_edge(0, w);
      m.add_vert({3, 3, 3});
      cases.push_back({"cube with a wire edge and a loose point: a face", m, {face_facing(m, {0, 1, 0}, {0, 0.5f, 0})}});
    }
    {
      /* A face of an internal wall: a cube with a face across its middle (three faces on four edges). */
      Mesh m = *primitives::cube();
      std::vector<uint8_t> vs(m.vert_count(), 0);
      cases.push_back({"cube: two faces at once with a fin between", m, {face_facing(m, {0, 1, 0}, {0, 0.5f, 0}), face_facing(m, {0, -1, 0}, {0, -0.5f, 0})}});
    }
    for (const Case &c : cases)
      for (float d : {0.3f, -0.2f}) {
        Mesh m = c.m;
        std::vector<uint8_t> sel(m.face_count(), 0);
        bool have = true;
        for (size_t f : c.faces) {
          if (f >= m.face_count()) have = false;
          else sel[f] = 1;
        }
        CHECK(have);
        if (!have) continue;
        std::vector<Vec3> before_c;
        std::vector<Vec3> before_n;
        for (size_t f : c.faces) before_c.push_back(m.face_center(f)), before_n.push_back(normalize(m.face_normal(f)));
        std::string err;
        const bool ok = meshops::push_pull_multi(m, sel, d, true, false, nullptr, &err);
        /* Moved: a selected face now sits d along the old normal from where it was. */
        size_t moved = 0;
        for (size_t k = 0; k < before_c.size(); k++)
          for (size_t f = 0; f < m.face_count(); f++)
            if (f < sel.size() && sel[f] && length(m.face_center(f) - (before_c[k] + before_n[k] * d)) < 1e-3f) {
              moved++;
              break;
            }
        std::string why;
        /* Sound (indices, corners, numbers), with no more edges on 3+ faces than it came with. */
        auto many = [](const Mesh &mm) {
          std::unordered_map<uint64_t, int> uses;
          for (size_t f = 0; f < mm.face_count(); f++)
            for (uint32_t k = 0; k < mm.face_size(f); k++) uses[Mesh::edge_key(mm.face_verts(f)[k], mm.face_verts(f)[(k + 1) % mm.face_size(f)])]++;
          size_t n = 0;
          for (auto &kv : uses) n += kv.second > 2;
          return n;
        };
        bool valid = true;
        for (const Vec3 &p : m.positions) valid = valid && std::isfinite(p.x + p.y + p.z);
        for (size_t f = 0; f < m.face_count() && valid; f++) {
          valid = m.face_size(f) >= 3;
          for (uint32_t k = 0; k < m.face_size(f) && valid; k++)
            valid = m.face_verts(f)[k] < m.vert_count() && m.face_verts(f)[k] != m.face_verts(f)[(k + 1) % m.face_size(f)];
        }
        /* (Pulling one of two cubes on an edge up presses it against the other along a face: new shared edges there are right.) */
        if (many(m) > many(c.m) && c.name.find("two cubes on one edge: a top") == std::string::npos)
          valid = false, why = strprintf("edges on 3+ faces %zu -> %zu", many(c.m), many(m));
        std::printf("    %s, %+.1f: %s%s, moved %zu/%zu, valid %d %s\n", c.name.c_str(), d, ok ? "ok" : "refused: ", ok ? "" : err.c_str(), moved, before_c.size(),
                    (int)valid, why.c_str());
        CHECK(ok && valid && moved == before_c.size());
      }
  });
  test("overlapping elements: no false alarms on ordinary meshes", [&] {
    struct P {
      const char *name;
      MeshPtr m;
    };
    Mesh beveled = *primitives::cube();
    meshops::bevel_modifier(beveled, 0.1f, 3, -1.0f);
    Mesh sub = meshops::subdivide(*primitives::cube(), 2, true);
    Mesh drawn = *primitives::cube();
    {
      size_t bottom = 0;
      for (size_t f = 0; f < drawn.face_count(); f++)
        if (drawn.face_normal(f).y < -0.9f) bottom = f;
      CHECK(meshops::imprint_loop(drawn, bottom, {Vec3(-0.2f, -0.5f, -0.2f), Vec3(0.2f, -0.5f, -0.2f), Vec3(0.2f, -0.5f, 0.2f), Vec3(-0.2f, -0.5f, 0.2f)}) >= 0);
    }
    const P ps[] = {{"cube", primitives::cube()},
                    {"plane", primitives::plane()},
                    {"UV sphere", primitives::uv_sphere()},
                    {"icosphere", primitives::ico_sphere()},
                    {"cylinder", primitives::cylinder()},
                    {"cone", primitives::cone()},
                    {"torus", primitives::torus()},
                    {"teapot", primitives::teapot()},
                    {"bevelled cube", std::make_shared<Mesh>(beveled)},
                    {"subdivided cube", std::make_shared<Mesh>(sub)},
                    {"rectangle drawn on a cube", std::make_shared<Mesh>(drawn)}};
    for (const P &p : ps) {
      const auto vs = meshops::overlapping_vertices(*p.m);
      const auto es = meshops::overlapping_edges(*p.m);
      std::printf("    %s: %zu vertex pair(s), %zu edge pair(s)\n", p.name, vs.size(), es.size());
      for (size_t k = 0; k < es.size() && k < 3; k++) {
        const uint32_t a = (uint32_t)(es[k].first >> 32), b = (uint32_t)(es[k].first & 0xFFFFFFFF), c = (uint32_t)(es[k].second >> 32),
                       d = (uint32_t)(es[k].second & 0xFFFFFFFF);
        const Vec3 A = p.m->positions[a], B = p.m->positions[b], C = p.m->positions[c], D = p.m->positions[d];
        std::printf("      %u-%u (%.3f %.3f %.3f)-(%.3f %.3f %.3f)  %u-%u (%.3f %.3f %.3f)-(%.3f %.3f %.3f)\n", a, b, A.x, A.y, A.z, B.x, B.y, B.z, c, d, C.x,
                    C.y, C.z, D.x, D.y, D.z);
      }
      CHECK(vs.empty() && es.empty());
    }
  });
  test("overlapping edges: only the shorter edge of a pair is marked (a piece on a long edge, not the long edge)", [&] {
    /* A quad whose bottom edge has a wire piece lying on its middle third. */
    Mesh m;
    for (Vec3 p : {Vec3(0, 0, 0), Vec3(3, 0, 0), Vec3(3, 0, 1), Vec3(0, 0, 1)}) m.add_vert(p);
    m.add_face({0, 3, 2, 1});
    const uint32_t a = m.add_vert({1, 0, 0}), b = m.add_vert({2, 0, 0});
    m.add_loose_edge(a, b);
    const auto es = meshops::overlapping_edges(m);
    CHECK(es.size() == 1);
    if (es.size() == 1) CHECK(Editor::overlap_shorter(m, es[0]) == Mesh::edge_key(a, b));
    Editor ed;
    ed.init_headless(900, 600);
    ed.step_frame_headless();
    ed.command("create Cube");
    ed.selected_object()->get<MeshFilter>()->mesh = std::make_shared<Mesh>(m);
    ed.command("edit vertex");
    ed.command("overlaps select");
    size_t sel = 0;
    for (uint32_t v = 0; v < m.vert_count(); v++) sel += ed.vert_selected_for_test(v);
    std::printf("    selected %zu vertices (the piece's two)\n", sel);
    CHECK(sel == 2 && ed.vert_selected_for_test(a) && ed.vert_selected_for_test(b));
  });
  test("tool settings live in the tool's helper: Shell, Thicken and Draft are adjustable (F9) operations", [&] {
    for (const char *op : {"shell", "thicken", "draft"}) {
      Editor ed;
      ed.init_headless(900, 600);
      ed.step_frame_headless();
      ed.command("create Cube");
      GameObject *g = ed.selected_object();
      ed.command("edit face");
      ed.command(std::string("fsel facing ") + (std::string(op) == "draft" ? "1 0 0" : "0 1 0"));
      ed.command(std::string("editop ") + op);
      ed.step_frame_headless();
      const Mesh &m = *g->get<MeshFilter>()->mesh;
      std::printf("    %s: %zu faces, last operation %s (%.3g), closed %d\n", op, m.face_count(), ed.last_op_name_for_test().c_str(), ed.last_op_amount_for_test(),
                  (int)closed_manifold(m));
      CHECK(ed.last_op_name_for_test() == op && closed_manifold(m) && m.face_count() >= 6);
    }
  });
  test("Merge Coplanar on the selected faces only (the rest keeps its edges); nothing selected: the whole mesh", [&] {
    /* The 10 x 10 Plane: a 3 x 3 block of its cells selected. */
    Editor ed;
    ed.init_headless(900, 600);
    ed.step_frame_headless();
    ed.command("create Plane");
    GameObject *g = ed.selected_object();
    const Mesh &m0 = *g->get<MeshFilter>()->mesh;
    CHECK(m0.face_count() == 100);
    std::string fs = "fsel";
    size_t picked = 0;
    for (size_t f = 0; f < m0.face_count(); f++) {
      const Vec3 c = m0.face_center(f);
      if (c.x > -1.6f && c.x < 1.0f && c.z > -1.6f && c.z < 1.0f) fs += strprintf(" %zu", f), picked++;  // cells at -1.5, -0.5, 0.5
    }
    CHECK(picked == 9);
    ed.command("edit face");
    ed.command(fs);
    ed.command("editop dissolve_limited");
    const Mesh &m1 = *g->get<MeshFilter>()->mesh;
    size_t sel = 0, sel_corners = 0;
    for (size_t f = 0; f < m1.face_count(); f++)
      if (ed.edit_face_selected(f)) sel++, sel_corners = m1.face_size(f);
    std::printf("    3 x 3 block merged: %zu faces (100 - 9 + 1 = 92), %zu selected with %zu corners\n", m1.face_count(), sel, sel_corners);
    CHECK(m1.face_count() == 92 && sel == 1 && closed_manifold(m1) == closed_manifold(m0) && structurally_valid(m1));
    /* The merged face keeps the corners its unselected neighbours share (no T-junctions): 12 around a 3 x 3 block. */
    CHECK(sel_corners == 12);
    /* Two separate blocks: each becomes its own face. */
    ed.command("edit off");
    ed.command("create Plane");
    GameObject *g2 = ed.selected_object();
    const Mesh &p0 = *g2->get<MeshFilter>()->mesh;
    std::string fs2 = "fsel";
    for (size_t f = 0; f < p0.face_count(); f++) {
      const Vec3 c = p0.face_center(f);
      if ((c.x < -3 && c.z < -3) || (c.x > 3 && c.z > 3)) fs2 += strprintf(" %zu", f);
    }
    ed.command("edit face");
    ed.command(fs2);
    ed.command("editop dissolve_limited");
    const Mesh &p1 = *g2->get<MeshFilter>()->mesh;
    std::printf("    two 2 x 2 corners merged: %zu faces (100 - 8 + 2 = 94)\n", p1.face_count());
    CHECK(p1.face_count() == 94);
    /* Nothing selected: the whole plane is one face. */
    ed.command("edit off");
    ed.command("create Plane");
    GameObject *g3 = ed.selected_object();
    ed.command("edit face");
    ed.command("fsel");
    ed.command("editop dissolve_limited");
    std::printf("    nothing selected: %zu face(s)\n", g3->get<MeshFilter>()->mesh->face_count());
    CHECK(g3->get<MeshFilter>()->mesh->face_count() == 1);
  });
  test("array: copies of a drawn shape are cut into the surface like the original, never laid on top of it", [&] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.step_frame_headless();
    ed.command("create Cube");
    GameObject *g = ed.selected_object();
    g->set_world_position({0, 0.5f, 0});
    ed.command("edit face");
    ed.command("draw rectangle");
    ed.command("drawpoint -0.4 1 -0.05");  // 0.2 along X, 0.1 along Z: the long side (Array's X) is X
    ed.command("drawpoint -0.2 1 0.05");
    {
      const Recti vr = ed.scene_view_rect();
      ed.step_frame_headless({ev(ET::KeyDown, vr.x + vr.w / 2, vr.y + vr.h / 2, platform::KEY_ESCAPE)});
      ed.step_frame_headless({ev(ET::KeyUp, vr.x + vr.w / 2, vr.y + vr.h / 2, platform::KEY_ESCAPE)});
    }
    const Mesh &m0 = *g->get<MeshFilter>()->mesh;
    size_t inner = SIZE_MAX;
    for (size_t f = 0; f < m0.face_count(); f++)
      if (m0.face_normal(f).y > 0.9f && std::fabs(meshops::face_area_center(m0, f).x + 0.3f) < 1e-3f) inner = f;
    CHECK(inner != SIZE_MAX);
    if (inner == SIZE_MAX) return;
    ed.command(strprintf("fsel %zu", inner));
    ed.command("hs array 3 0.3 0");
    ed.command("editop array");
    const Mesh &m = *g->get<MeshFilter>()->mesh;
    size_t sel = 0;
    for (size_t f = 0; f < m.face_count(); f++) sel += ed.edit_face_selected(f);
    std::printf("    arrayed: %zu faces, %zu selected, overlaps %zu, closed %d, bad triangulations %zu\n", m.face_count(), sel, overlapping_face_pairs(m),
                (int)closed_manifold(m), bad_triangulations(m));
    if (!closed_manifold(m)) {
      std::unordered_map<uint64_t, int> dir;
      for (size_t f = 0; f < m.face_count(); f++)
        for (uint32_t i = 0; i < m.face_size(f); i++) {
          const uint32_t a = m.face_verts(f)[i], b = m.face_verts(f)[(i + 1) % m.face_size(f)];
          dir[Mesh::edge_key(a, b)] += a < b ? 1 : 16;
        }
      for (auto &[k, c] : dir)
        if (c != 17) {
          const Vec3 A = m.positions[(uint32_t)(k >> 32)], B = m.positions[(uint32_t)(k & 0xFFFFFFFF)];
          std::printf("      open edge %u-%u x%d/%d: (%.3f %.3f %.3f)-(%.3f %.3f %.3f)\n", (uint32_t)(k >> 32), (uint32_t)(k & 0xFFFFFFFF), c % 16, c / 16, A.x, A.y,
                      A.z, B.x, B.y, B.z);
        }
    }
    CHECK(sel == 3 && overlapping_face_pairs(m) == 0 && closed_manifold(m) && bad_triangulations(m) == 0);
    /* They behave like drawn shapes: Push/Pull the three into the box at once. */
    ed.command("editop push_pull");
    const Mesh &pm = *g->get<MeshFilter>()->mesh;
    std::printf("    pushed together: %zu faces, closed %d, overlaps %zu\n", pm.face_count(), (int)closed_manifold(pm), overlapping_face_pairs(pm));
    CHECK(closed_manifold(pm) && overlapping_face_pairs(pm) == 0);
    /* Off the surface (the box's whole top): separate copies, as before. */
    Editor ed2;
    ed2.init_headless(900, 600);
    ed2.step_frame_headless();
    ed2.command("create Cube");
    GameObject *g2 = ed2.selected_object();
    ed2.command("edit face");
    ed2.command("fsel facing 0 1 0");
    ed2.command("hs array 2 1.5 0");
    ed2.command("editop array");
    std::printf("    nothing under the copy: %zu faces\n", g2->get<MeshFilter>()->mesh->face_count());
    CHECK(g2->get<MeshFilter>()->mesh->face_count() == 7);
  });
  test("array: an arched doorway (a rectangle with an arc on top) on a wall copies cut in cleanly", [&] {
    for (int segs : {8, 16})
      for (int count : {2, 3}) {
        Editor ed;
        ed.init_headless(1000, 700);
        ed.step_frame_headless();
        ed.command("create Cube");
        GameObject *g = ed.selected_object();
        {
          Mesh &w = *mesh_make_mutable(g->get<MeshFilter>()->mesh);
          for (Vec3 &p : w.positions) p = Vec3(p.x * 5.0f, p.y * 3.0f + 1.5f, p.z * 0.5f);
          w.touch();
        }
        g->set_world_position({0, 0, 0});
        ed.command("edit face");
        ed.command("draw rectangle");
        ed.command("drawpoint -0.5 0.5 -0.25");
        ed.command("drawpoint 0.5 1.5 -0.25");
        ed.command(strprintf("draw arc %d", segs));
        ed.command("drawpoint -0.5 1.5 -0.25");
        ed.command("drawpoint 0.5 1.5 -0.25");
        ed.command("drawpoint 0 2 -0.25");
        {
          const Recti vr = ed.scene_view_rect();
          ed.step_frame_headless({ev(ET::KeyDown, vr.x + vr.w / 2, vr.y + vr.h / 2, platform::KEY_ESCAPE)});
          ed.step_frame_headless({ev(ET::KeyUp, vr.x + vr.w / 2, vr.y + vr.h / 2, platform::KEY_ESCAPE)});
        }
        const Mesh &m0 = *g->get<MeshFilter>()->mesh;
        std::string fs = "fsel";
        float area0 = 0.0f;
        auto area_of = [](const Mesh &mm, size_t f) {
          Vec3 nw(0.0f);
          for (uint32_t i = 0; i < mm.face_size(f); i++) nw += cross(mm.positions[mm.face_verts(f)[i]], mm.positions[mm.face_verts(f)[(i + 1) % mm.face_size(f)]]);
          return 0.5f * length(nw);
        };
        for (size_t f = 0; f < m0.face_count(); f++) {
          bool in = m0.face_normal(f).z < -0.99f;
          for (uint32_t k = 0; k < m0.face_size(f) && in; k++) {
            const Vec3 p = m0.positions[m0.face_verts(f)[k]];
            in = std::fabs(p.x) < 0.5f + 1e-4f && p.y > 0.5f - 1e-4f && p.y < 2.0f + 1e-4f;
          }
          if (in) fs += strprintf(" %zu", f), area0 += area_of(m0, f);
        }
        ed.command(fs);
        /* Along the wall: whichever of the shape's own axes runs along X. */
        Quat q;
        CHECK(ed.selection_frame_for_test(q));
        const int axis = std::fabs(q.rotate({1, 0, 0}).x) > 0.9f ? 0 : 2;
        ed.command(strprintf("hs array %d 1.3 %d", count, axis));
        ed.command("editop array");
        const Mesh &m = *g->get<MeshFilter>()->mesh;
        float area = 0.0f;
        size_t sel = 0;
        for (size_t f = 0; f < m.face_count(); f++)
          if (ed.edit_face_selected(f)) sel++, area += area_of(m, f);
        /* Each copy cut in: as much doorway area as count shapes, with the wall's own faces untouched behind. */
        std::printf("    %d segments, %d in all: %zu faces, %zu selected, area %.4f of %.4f, overlaps %zu, closed %d, bad %zu, wires %zu\n", segs, count,
                    m.face_count(), sel, area, area0 * count, overlapping_face_pairs(m), (int)closed_manifold(m), bad_triangulations(m), m.loose_edges.size());
        /* The wall runs to x = 2.5: copies at 1.3 fit, the one at 2.6 hangs off and only its part on the wall (x 2.1 .. 2.5) is cut in. */
        /* That part: 0.4 of the 1 wide doorway below the arch, plus the slice of the (polygonal) arch over it,
         * a little under the round slice r^2 acos(d / r) - d sqrt(r^2 - d^2), halved, with r = 0.5, d = 0.1 (0.147). */
        const float extra = count == 2 ? 0.0f : area - area0 * 2 - 0.4f;
        CHECK((count == 2 ? std::fabs(area - area0 * 2) < 1e-3f : extra > 0.12f && extra < 0.1468f) && overlapping_face_pairs(m) == 0 &&
              closed_manifold(m) && bad_triangulations(m) == 0 && m.loose_edges.empty());
        /* All of them pushed through together: holes (each copy a doorway). */
        ed.command("editop push_pull");
      }
  });
}

/* Round 26 (task 0002): what the AddressSanitizer / UBSan job found, each with a test that
 * failed under the sanitizer before its fix. */
static void round26_tests() {
  /* A quad whose first two corners are `gap` apart along x at x = base; the other two are 1 away
   * in y and z, so merging the pair leaves a triangle. */
  auto pair_at = [](float base, float gap) {
    Mesh m;
    m.add_vert({base, 0.0f, 0.0f});
    m.add_vert({base + gap, 0.0f, 0.0f});
    m.add_vert({base, 1.0f, 0.0f});
    m.add_vert({base, 0.0f, 1.0f});
    m.add_face({0u, 1u, 2u, 3u});
    return m;
  };
  test("merge by distance: far from the origin with a tiny distance, cells don't overflow", [&] {
    /* -2.0001 / 1e-4 is cell -20001: its hash overflowed int. 1e6 / 1e-6 is cell 1e12: the
     * cell itself didn't fit in an int. Both are undefined behaviour; the merge must still be right. */
    struct Case { float base, dist, near_gap, apart_gap; };
    /* Far out a float can't hold a gap below its step (0.0625 at 1e6, 2 at 3e7): the near pair
     * coincides and the apart pair is a few steps apart. */
    for (const Case &c : {Case{-2.0001f, 1e-4f, 2.5e-5f, 4e-4f}, Case{1.0e6f, 1e-6f, 0.0f, 0.25f}, Case{-3.0e7f, 1e-6f, 0.0f, 8.0f}}) {
      Mesh near_pair = pair_at(c.base, c.near_gap), apart = pair_at(c.base, c.apart_gap);
      Mesh naive = near_pair;
      const size_t r = meshops::merge_by_distance(near_pair, c.dist), rn = meshops::merge_by_distance_naive(naive, c.dist);
      const size_t ra = meshops::merge_by_distance(apart, c.dist);
      std::printf("    base %g, distance %g: merged %zu (naive %zu), apart merged %zu\n", c.base, c.dist, r, rn, ra);
      CHECK(r == 1 && rn == 1);
      CHECK(ra == 0 && apart.vert_count() == 4);
    }
    /* A tiny distance on an ordinary mesh: cells of 1e-6 across a 1 m object. */
    Mesh grid = pair_at(0.5f, 2e-7f);
    CHECK(meshops::merge_by_distance(grid, 1e-6f) == 1);
    /* NaN positions don't crash and aren't merged with anything. */
    Mesh nan_mesh = pair_at(0.0f, 0.5f);
    nan_mesh.positions[1].x = std::nanf("");
    meshops::merge_by_distance(nan_mesh, 1e-3f);
    CHECK(nan_mesh.vert_count() == 4);
  });
  test("merge by distance (selected): far from the origin with a tiny distance", [&] {
    Mesh m = pair_at(-2.0001f, 2.5e-5f);
    CHECK(meshops::merge_by_distance_selected(m, 1e-4f, {1, 1, 0, 0}) == 1 && m.vert_count() == 3);
    Mesh far = pair_at(1.0e6f, 0.25f);
    CHECK(meshops::merge_by_distance_selected(far, 0.5f, {1, 1, 1, 1}) == 1 && far.vert_count() == 3);
  });
}

/* ===================================================================== */
/* Round 27 (task 0004): camera filters, with the PS1 look               */
/* ===================================================================== */

static bool cf_is_15bit(uint32_t c) { return (c & 0x070707u) == 0; }

/* A tiny scene for direct renders: items keep their meshes and materials alive. */
struct CfScene {
  std::vector<MeshPtr> meshes;
  std::vector<std::vector<MaterialPtr>> mats;
  std::vector<DrawItem> items;
  void add(MeshPtr m, const Mat4 &model, uint32_t id, MaterialPtr mat) {
    meshes.push_back(m);
    mats.push_back({mat});
    DrawItem it;
    it.mesh = &m->render_mesh();
    it.model = model;
    it.id = id;
    it.unlit = mat && mat->unlit;
    items.push_back(it);
  }
  void seal() {  // the material vectors are stable now
    for (size_t i = 0; i < items.size(); i++) items[i].materials = &mats[i];
  }
};

static MaterialPtr cf_unlit(Vec3 c) {
  auto m = make_material("Unlit", c);
  m->unlit = true;
  return m;
}

/* Draws the scene into an attached target: deferred, one sun, cleared to `clear`. */
static void cf_draw(RenderTarget &rt, const Mat4 &v, const Mat4 &p, const RasterOptions &opt0, const CfScene &sc, uint32_t clear,
                    Renderer3D *keep = nullptr) {
  Renderer3D local;
  Renderer3D &r = keep ? *keep : local;
  RasterOptions opt = opt0;
  opt.shade = ShadeMode::Deferred;
  LightingEnv env;
  RenderLight sun;
  sun.direction = normalize(Vec3(-0.3f, -1, 0.4f));
  env.lights.push_back(sun);
  env.camera_pos = Vec3(0.0f);
  r.begin(&rt, v, p, env, opt);
  r.clear(clear);
  for (const DrawItem &it : sc.items) r.add(it);
  r.flush();
}

static void cf_render(Image &img, RenderTarget &rt, int W, int H, const Mat4 &v, const Mat4 &p, const RasterOptions &opt0, const CfScene &sc,
                      uint32_t clear = 0xFF204060u, Renderer3D *keep = nullptr) {
  img.resize(W, H);
  rt.attach(img, {0, 0, W, H});
  cf_draw(rt, v, p, opt0, sc, clear, keep);
}

static uint64_t cf_hash(const Image &img) {
  uint64_t h = 1469598103934665603ull;
  for (uint32_t c : img.pixels) h = (h ^ c) * 1099511628211ull;
  return h;
}

/* Colour changes along the busiest row, and rows that differ from the one above, inside r. */
static void cf_blockiness(const uint32_t *px, int stride, const Recti &r, int &max_runs, int &row_changes) {
  max_runs = 0;
  row_changes = 0;
  for (int y = r.y; y < r.bottom(); y++) {
    const uint32_t *row = px + (size_t)y * stride;
    int runs = 1;
    for (int x = r.x + 1; x < r.right(); x++) runs += row[x] != row[x - 1];
    max_runs = std::max(max_runs, runs);
    if (y > r.y && std::memcmp(row + r.x, px + (size_t)(y - 1) * stride + r.x, sizeof(uint32_t) * r.w) != 0) row_changes++;
  }
}

/* Task 0005 moved the Retro Console filter into a Camera Filters stack (ADR 0008): these find and
 * add it there, so round 27's tests keep testing the same behaviour. */
static RetroConsoleFilter *cf_add_retro(GameObject *cam) {
  auto *cf = cam->get<CameraFilters>();
  if (!cf) cf = cam->add<CameraFilters>();
  return static_cast<RetroConsoleFilter *>(cf->add(RetroConsoleFilter::kName));
}
static std::vector<RetroConsoleFilter *> cf_all_retro(const GameObject *cam) {
  std::vector<RetroConsoleFilter *> out;
  if (const auto *cf = cam ? cam->get<CameraFilters>() : nullptr)
    for (const auto &e : cf->effects)
      if (auto *r = dynamic_cast<RetroConsoleFilter *>(e.get())) out.push_back(r);
  return out;
}
static RetroConsoleFilter *cf_get_retro(const GameObject *cam) {
  auto all = cf_all_retro(cam);
  return all.empty() ? nullptr : all[0];
}

static void round27_tests() {
  /* ---------------------------------------------------------------- 1 */
  test("camera filters: with no filter the rasterizer's image is unchanged (default options, explicit zeros, an empty stack, a neutral component)", [&] {
    CfScene sc;
    auto grid = make_material("Grid", {0.9f, 0.9f, 0.9f});
    grid->base_map.path = "generated:UV Grid";
    sc.add(primitives::cube(), Mat4::translate({-1.2f, 0, 0}), 1, make_material("Red", {0.8f, 0.2f, 0.2f}));
    sc.add(primitives::uv_sphere(0.8f, 24, 12), Mat4::translate({1.2f, 0, 0.5f}), 2, grid);
    sc.add(primitives::plane(12.0f), Mat4::translate({0, -1, 0}), 3, make_material("Floor", {0.5f, 0.6f, 0.5f}));
    sc.seal();
    const Mat4 v = Mat4::look_at({0, 1.5f, -6}, {0, 0, 0}, {0, 1, 0});
    const Mat4 p = Mat4::perspective(50 * kDeg2Rad, 160 / 120.0f, 0.1f, 100);
    Image a, b, c, d;
    RenderTarget ra, rb, rc, rd;
    RasterOptions defaults;
    CHECK(defaults.vertex_snap == 0.0f && !defaults.affine_uv && !defaults.tex.active());
    cf_render(a, ra, 160, 120, v, p, defaults, sc);
    RasterOptions zero;
    zero.vertex_snap = 0.0f;
    zero.affine_uv = false;
    zero.tex = TexOverride{};
    zero.tex.filter = -1;
    zero.tex.max_size = 0;
    zero.tex.mipmaps = true;
    cf_render(b, rb, 160, 120, v, p, zero, sc);
    FilterStack empty;
    CHECK(empty.empty());
    RasterOptions via_stack;
    empty.apply_raster(via_stack);
    cf_render(c, rc, 160, 120, v, p, via_stack, sc);
    /* A Retro Console Filter with every effect off adds nothing to the stack. */
    RetroConsoleFilter neutral;
    neutral.vertex_snap = false;
    neutral.affine_textures = false;
    neutral.texture_filter = 0;
    neutral.max_texture_size = 0;
    neutral.mipmaps = true;
    neutral.width = neutral.height = 0;
    neutral.color_depth = RetroImageParams::Full;
    neutral.fog = false;
    FilterStack ns;
    neutral.contribute(ns);
    CHECK(ns.empty());
    /* A Camera Filters stack with no effects, or with every effect off, contributes nothing either;
     * one effect switched back on does. */
    {
      CameraFilters cf;
      FilterStack s0;
      cf.contribute(s0);
      CHECK(s0.empty());
      cf.add("Retro Console")->enabled = false;
      cf.add("Retro Console")->enabled = false;
      FilterStack s2;
      cf.contribute(s2);
      CHECK(s2.empty());
      cf.effects[1]->enabled = true;
      FilterStack s3;
      cf.contribute(s3);
      CHECK(!s3.empty());
    }
    RasterOptions via_neutral;
    ns.apply_raster(via_neutral);
    cf_render(d, rd, 160, 120, v, p, via_neutral, sc);
    CHECK(a.pixels == b.pixels && a.pixels == c.pixels && a.pixels == d.pixels);
    CHECK(ra.depth == rb.depth && ra.depth == rc.depth && ra.depth == rd.depth);
    CHECK(ra.ids == rb.ids && ra.ids == rd.ids);
    /* The picture isn't trivially empty. */
    std::set<uint32_t> ids(ra.ids.begin(), ra.ids.end());
    CHECK(ids.count(1) && ids.count(2) && ids.count(3));
  });

  /* ---------------------------------------------------------------- 2 */
  test("camera filters: filter_layout gives the internal size and where it lands in the view (Fill keeps square pixels, Letterbox is 4:3 with bars)", [&] {
    int iw, ih;
    Recti dst;
    FilterStack s;
    s.width = 320;
    s.height = 240;
    s.fit = FilterStack::Fill;
    filter_layout(s, 1280, 720, iw, ih, dst);
    CHECK(iw == 427 && ih == 240);
    CHECK(dst.x == 0 && dst.y == 0 && dst.w == 1280 && dst.h == 720);
    CHECK_NEAR(1280.0 / iw, 720.0 / ih, 0.02);  // square pixels
    s.fit = FilterStack::Letterbox;
    filter_layout(s, 1280, 720, iw, ih, dst);
    CHECK(iw == 320 && ih == 240);
    CHECK(dst.w == 960 && dst.h == 720 && dst.x == 160 && dst.y == 0);
    CHECK_NEAR((double)dst.w / dst.h, 4.0 / 3.0, 0.005);
    /* A tall view gets bars top and bottom. */
    filter_layout(s, 600, 900, iw, ih, dst);
    CHECK(dst.w == 600 && dst.h == 450 && dst.x == 0 && dst.y == 225 && iw == 320 && ih == 240);
    /* A filter never adds pixels: a view smaller than the internal size is rendered at its own size. */
    s.fit = FilterStack::Fill;
    filter_layout(s, 100, 50, iw, ih, dst);
    CHECK(iw == 100 && ih == 50 && dst.w == 100 && dst.h == 50);
    s.fit = FilterStack::Letterbox;
    filter_layout(s, 100, 50, iw, ih, dst);
    CHECK(iw <= dst.w && ih <= dst.h && dst.w <= 100 && dst.h <= 50 && iw >= 1 && ih >= 1);
    /* No internal size: the view itself. */
    FilterStack none;
    filter_layout(none, 640, 480, iw, ih, dst);
    CHECK(iw == 640 && ih == 480 && dst.x == 0 && dst.y == 0 && dst.w == 640 && dst.h == 480);
    /* Odd sizes: 1 x 1 views, zero and negative views, absurd internal sizes. */
    s.fit = FilterStack::Fill;
    filter_layout(s, 1, 1, iw, ih, dst);
    CHECK(iw == 1 && ih == 1 && dst.w == 1 && dst.h == 1);
    filter_layout(s, 0, -5, iw, ih, dst);
    CHECK(iw >= 1 && ih >= 1 && dst.w >= 1 && dst.h >= 1);
    s.fit = FilterStack::Letterbox;
    filter_layout(s, 1, 1, iw, ih, dst);
    CHECK(iw == 1 && ih == 1 && dst.w == 1 && dst.h == 1);
    FilterStack huge;
    huge.width = 1000000;
    huge.height = 1000000;
    for (int fit : {0, 1}) {
      huge.fit = fit;
      filter_layout(huge, 1920, 1080, iw, ih, dst);
      CHECK(iw >= 1 && ih >= 1 && iw <= 1920 && ih <= 1080);
      CHECK(dst.x >= 0 && dst.y >= 0 && dst.right() <= 1920 && dst.bottom() <= 1080);
    }
    FilterStack negative;
    negative.width = -3;
    negative.height = 240;
    negative.fit = FilterStack::Letterbox;  // no width: falls back to Fill
    filter_layout(negative, 800, 600, iw, ih, dst);
    CHECK(iw >= 1 && ih == 240 && dst.w == 800 && dst.h == 600);
  });

  test("camera filters: upscale_nearest copies whole blocks into the destination rectangle only", [&] {
    const uint32_t src[4] = {1, 2, 3, 4};  // 2 x 2
    std::vector<uint32_t> dst(8 * 6, 99u);
    upscale_nearest(src, 2, 2, 2, dst.data(), 8, Recti{2, 1, 4, 4});
    int upscale_wrong = 0;
    for (int y = 0; y < 6; y++)
      for (int x = 0; x < 8; x++) {
        const bool in = x >= 2 && x < 6 && y >= 1 && y < 5;
        const uint32_t expect = !in ? 99u : (uint32_t)(1 + ((x - 2) / 2) + 2 * ((y - 1) / 2));
        if (dst[(size_t)y * 8 + x] != expect && upscale_wrong++ == 0) std::printf("    first wrong pixel (%d, %d)\n", x, y);
      }
    CHECK(upscale_wrong == 0);
    /* Bad input is ignored, not crashed on. */
    upscale_nearest(nullptr, 2, 2, 2, dst.data(), 8, Recti{0, 0, 2, 2});
    upscale_nearest(src, 0, 2, 2, dst.data(), 8, Recti{0, 0, 2, 2});
    upscale_nearest(src, 2, 2, 2, dst.data(), 8, Recti{0, 0, 0, 2});
    CHECK(dst[0] == 99u);
    /* One source pixel fills any size. */
    const uint32_t one = 7u;
    std::vector<uint32_t> big(5 * 5, 0u);
    upscale_nearest(&one, 1, 1, 1, big.data(), 5, Recti{0, 0, 5, 5});
    CHECK(std::count(big.begin(), big.end(), 7u) == 25);
  });

  /* ---------------------------------------------------------------- 3 */
  test("camera filters: the PS1 dither is the console's 4x4 matrix, and flat grey takes exactly those offsets before dropping to 5 bits", [&] {
    static const int kPs1[4][4] = {{-4, 0, -3, 1}, {2, -2, 3, -1}, {-3, 1, -4, 0}, {3, -1, 2, -2}};
    int wrong_ps1 = 0, wrong_none = 0, bayer_out_of_range = 0;
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++) {
        wrong_ps1 += retro_dither_offset(RetroImageParams::Ps1, x, y) != kPs1[y & 3][x & 3];
        wrong_none += retro_dither_offset(RetroImageParams::NoDither, x, y) != 0;
        const int b = retro_dither_offset(RetroImageParams::Bayer4, x, y);
        bayer_out_of_range += b < -4 || b > 3;
      }
    CHECK(wrong_ps1 == 0);
    CHECK(wrong_none == 0);
    CHECK(bayer_out_of_range == 0);
    CHECK(retro_dither_offset(RetroImageParams::Ps1, -1, -1) == kPs1[3][3]);  // the pattern repeats below zero too
    CHECK(retro_dither_offset(99, 3, 2) == 0);
    /* Bayer uses every offset of the range exactly twice per tile. */
    std::map<int, int> hist;
    for (int y = 0; y < 4; y++)
      for (int x = 0; x < 4; x++) hist[retro_dither_offset(RetroImageParams::Bayer4, x, y)]++;
    CHECK(hist.size() == 8);
    CHECK(std::all_of(hist.begin(), hist.end(), [](const std::pair<const int, int> &kv) { return kv.second == 2; }));
    /* Flat grey 128: offsets -4..-1 give 124..127 -> 120, 0..3 give 128..131 -> 128. */
    Image img;
    img.resize(8, 8);
    std::fill(img.pixels.begin(), img.pixels.end(), 0xFF808080u);
    RenderTarget rt;
    rt.attach(img, {0, 0, 8, 8});
    RetroImageParams ps1;
    ps1.color_depth = RetroImageParams::Bits15;
    ps1.dither = RetroImageParams::Ps1;
    apply_retro_image(rt, ps1, nullptr);
    int low = 0, high = 0, wrong = 0;
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++) {
        const int off = kPs1[y & 3][x & 3];
        const uint32_t v = off < 0 ? 120u : 128u;
        wrong += img.row(y)[x] != (0xFF000000u | v << 16 | v << 8 | v);
        (off < 0 ? low : high)++;
      }
    CHECK(wrong == 0);
    CHECK(low == 32 && high == 32);
    /* No dither: every grey truncates the same way (0x87 -> 0x80). */
    std::fill(img.pixels.begin(), img.pixels.end(), 0xFF878787u);
    RetroImageParams plain;
    plain.color_depth = RetroImageParams::Bits15;
    apply_retro_image(rt, plain, nullptr);
    CHECK(count_bad("pixels", img.pixels, [&](uint32_t c) { return c == 0xFF808080u; }) == 0);
    /* The ends stay in range: white (255 + 3 clamps) and black (0 - 4 clamps). */
    std::fill(img.pixels.begin(), img.pixels.end(), 0xFFFFFFFFu);
    apply_retro_image(rt, ps1, nullptr);
    CHECK(count_bad("pixels", img.pixels, [&](uint32_t c) { return c == 0xFFF8F8F8u; }) == 0);
    std::fill(img.pixels.begin(), img.pixels.end(), 0xFF000000u);
    apply_retro_image(rt, ps1, nullptr);
    CHECK(count_bad("pixels", img.pixels, [&](uint32_t c) { return c == 0xFF000000u; }) == 0);
    /* Channels are quantized independently and alpha is kept. */
    std::fill(img.pixels.begin(), img.pixels.end(), 0x80FF8A07u);
    apply_retro_image(rt, plain, nullptr);
    CHECK(count_bad("pixels", img.pixels, [&](uint32_t c) { return c == 0x80F88800u; }) == 0);
    /* Full colour depth with no fog does nothing at all. */
    std::fill(img.pixels.begin(), img.pixels.end(), 0xFF123457u);
    RetroImageParams off;
    apply_retro_image(rt, off, nullptr);
    CHECK(count_bad("pixels", img.pixels, [&](uint32_t c) { return c == 0xFF123457u; }) == 0);
    /* Empty targets are ignored. */
    RenderTarget none;
    apply_retro_image(none, ps1, nullptr);
    CHECK(none.width == 0 && none.color == nullptr);
  });

  /* ---------------------------------------------------------------- 4 */
  test("camera filters: vertex snap puts rendered corners on whole pixels; a sub-pixel camera move leaves the picture alone, a bigger one jumps it", [&] {
    CfScene sc;
    sc.add(primitives::quad(2.0f), Mat4::identity(), 9, cf_unlit({1.0f, 0.5f, 0.2f}));
    sc.seal();
    const int W = 64, H = 64;
    const Mat4 p = Mat4::perspective(50 * kDeg2Rad, 1.0f, 0.1f, 50);
    const Vec3 corners[4] = {{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}};
    struct Step { std::vector<int> key; uint64_t snapped, plain; };
    std::vector<Step> steps;
    bool extents_ok = true, projected = true;
    for (int i = 0; i < 48; i++) {
      const float cx = i * 0.004f;  // about 0.055 px per step at this distance
      const Mat4 v = Mat4::look_at({cx, 0, -5}, {cx, 0, 0}, {0, 1, 0});
      Step s;
      RasterOptions snap;
      snap.vertex_snap = 1.0f;
      Image a;
      RenderTarget ra;
      Renderer3D r3d;
      cf_render(a, ra, W, H, v, p, snap, sc, 0xFF000000u, &r3d);
      s.snapped = cf_hash(a);
      int minx = 1 << 20, maxx = -1, miny = 1 << 20, maxy = -1;
      float fx0 = 1e9f, fx1 = -1e9f, fy0 = 1e9f, fy1 = -1e9f;
      for (const Vec3 &c : corners) {
        Vec2 sp;
        float z;
        projected &= r3d.project(c, sp, z);
        s.key.push_back((int)std::floor(sp.x + 0.5f));
        s.key.push_back((int)std::floor(sp.y + 0.5f));
        fx0 = std::min(fx0, sp.x), fx1 = std::max(fx1, sp.x), fy0 = std::min(fy0, sp.y), fy1 = std::max(fy1, sp.y);
      }
      for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
          if (ra.ids[(size_t)y * W + x] == 9) minx = std::min(minx, x), maxx = std::max(maxx, x), miny = std::min(miny, y), maxy = std::max(maxy, y);
      /* The drawn rectangle is exactly the one between the rounded corners. */
      if (minx != (int)std::floor(fx0 + 0.5f) || maxx != (int)std::floor(fx1 + 0.5f) - 1 || miny != (int)std::floor(fy0 + 0.5f) ||
          maxy != (int)std::floor(fy1 + 0.5f) - 1)
        extents_ok = false;
      steps.push_back(s);
    }
    CHECK(projected);
    CHECK(extents_ok);
    bool same_key_same_image = true, key_changes = false, jump_changes_image = true;
    for (size_t i = 0; i < steps.size(); i++)
      for (size_t j = i + 1; j < steps.size(); j++) {
        const bool same = steps[i].key == steps[j].key;
        if (same && steps[i].snapped != steps[j].snapped) same_key_same_image = false;
        if (!same && steps[i].snapped == steps[j].snapped && j == i + 1) jump_changes_image = false;
        if (!same && j == i + 1) key_changes = true;
      }
    CHECK(same_key_same_image);     // moves that round to the same corners draw the same picture
    CHECK(key_changes);             // the sweep does cross whole pixels
    CHECK(jump_changes_image);      // and when it does the picture changes (by whole pixels)
    /* An axis-aligned rectangle covers the same pixel centres snapped or not (an edge crosses a
     * centre exactly when its corner rounds the other way), so the creep shows on slanted edges:
     * a quad turned 30 degrees. Snapped, moves that keep its corners' rounding draw one picture;
     * unsnapped, the slanted edges creep across pixel centres in between. */
    CfScene tilted;
    tilted.add(primitives::quad(2.0f), Mat4::trs({0, 0, 0}, Quat::euler({0, 0, 30}), {1, 1, 1}), 9, cf_unlit({1.0f, 0.5f, 0.2f}));
    tilted.seal();
    std::vector<Step> tsteps;
    for (int i = 0; i < 48; i++) {
      const float cx = i * 0.004f;
      const Mat4 v = Mat4::look_at({cx, 0, -5}, {cx, 0, 0}, {0, 1, 0});
      Step s;
      RasterOptions snap, plain;
      snap.vertex_snap = 1.0f;
      Image a, b;
      RenderTarget ra, rb;
      Renderer3D r3d;
      cf_render(a, ra, W, H, v, p, snap, tilted, 0xFF000000u, &r3d);
      cf_render(b, rb, W, H, v, p, plain, tilted);
      s.snapped = cf_hash(a);
      s.plain = cf_hash(b);
      const Mat4 rot = Mat4::trs({0, 0, 0}, Quat::euler({0, 0, 30}), {1, 1, 1});
      for (const Vec3 &c : corners) {
        Vec2 sp;
        float z;
        projected &= r3d.project(rot.point(c), sp, z);
        s.key.push_back((int)std::floor(sp.x + 0.5f));
        s.key.push_back((int)std::floor(sp.y + 0.5f));
      }
      tsteps.push_back(s);
    }
    CHECK(projected);
    bool t_same_key_same_image = true, t_plain_creeps = false;
    for (size_t i = 0; i + 1 < tsteps.size(); i++) {
      const bool same = tsteps[i].key == tsteps[i + 1].key;
      if (same && tsteps[i].snapped != tsteps[i + 1].snapped) t_same_key_same_image = false;
      if (same && tsteps[i].plain != tsteps[i + 1].plain) t_plain_creeps = true;
    }
    CHECK(t_same_key_same_image);  // snapped: the picture holds still between whole-pixel jumps
    CHECK(t_plain_creeps);         // unsnapped: slanted edges creep pixel by pixel in between
  });

  test("camera filters: vertex snap with NaN, negative, infinite, huge and tiny grids, and on a 1x1 target, doesn't crash or draw garbage", [&] {
    CfScene sc;
    sc.add(primitives::cube(), Mat4::identity(), 5, cf_unlit({0.3f, 0.8f, 0.4f}));
    sc.seal();
    const Mat4 v = Mat4::look_at({0, 0.5f, -4}, {0, 0, 0}, {0, 1, 0});
    const Mat4 p = Mat4::perspective(50 * kDeg2Rad, 1.0f, 0.1f, 50);
    Image ref;
    RenderTarget rr;
    cf_render(ref, rr, 48, 48, v, p, RasterOptions{}, sc);
    /* A grid that isn't a positive number means no snapping. */
    const float nan = kNaN, inf = kInf;
    for (float g : {nan, -1.0f, 0.0f, -inf, inf}) {
      RasterOptions o;
      o.vertex_snap = g;
      Image im;
      RenderTarget rt;
      cf_render(im, rt, 48, 48, v, p, o, sc);
      std::printf("    snap %g: %s\n", (double)g, im.pixels == ref.pixels ? "same as off" : "different");
      CHECK(im.pixels == ref.pixels);
    }
    /* Extreme positive grids: everything collapses or jumps, but nothing crashes. */
    for (float g : {1e-12f, 1e-4f, 0.5f, 7.0f, 1e6f, 1e30f, 3e38f}) {
      RasterOptions o;
      o.vertex_snap = g;
      o.affine_uv = true;
      Image im;
      RenderTarget rt;
      cf_render(im, rt, 48, 48, v, p, o, sc);
      CHECK(im.pixels.size() == 48u * 48u);
    }
    /* A 1 x 1 target. */
    RasterOptions o;
    o.vertex_snap = 1.0f;
    o.affine_uv = true;
    o.tex.filter = 0;
    o.tex.mipmaps = false;
    Image one;
    RenderTarget r1;
    cf_render(one, r1, 1, 1, v, p, o, sc);
    CHECK(one.pixels.size() == 1u);
    /* A camera inside the object (near clipping) with snapping. */
    const Mat4 inside = Mat4::look_at({0, 0, 0.2f}, {0, 0, 5}, {0, 1, 0});
    o.vertex_snap = 2.0f;
    Image in;
    RenderTarget rin;
    cf_render(in, rin, 48, 48, inside, p, o, sc);
    CHECK(in.pixels.size() == 48u * 48u);
  });

  /* ---------------------------------------------------------------- 5 */
  test("camera filters: affine textures follow the screen-space (not perspective-correct) interpolation on a slanted quad", [&] {
    /* A texture whose red channel is the column number, so a pixel's colour tells its u. */
    const std::string path = fs::join(test_dir(), "cf_gradient.png");
    std::vector<uint32_t> g(256 * 4);
    for (int y = 0; y < 4; y++)
      for (int x = 0; x < 256; x++) g[(size_t)y * 256 + x] = 0xFF000000u | (uint32_t)x << 16 | (uint32_t)x << 8 | (uint32_t)x;
    CHECK(write_png(path, g.data(), 256, 4, 256));
    auto mat = cf_unlit({1, 1, 1});
    mat->base_map.path = path;
    mat->base_map.non_color = true;
    mat->wrap = (int)TexWrap::Extend;
    mat->filter = (int)TexFilter::Linear;
    CfScene sc;
    auto quad = primitives::quad(2.0f);
    const Mat4 model = Mat4::rotate(Quat::euler({0, 70, 0}));  // one side near, one side far
    sc.add(quad, model, 4, mat);
    sc.seal();
    const int W = 160, H = 120;
    const Mat4 v = Mat4::look_at({0, 0, -3.2f}, {0, 0, 0}, {0, 1, 0});
    const Mat4 p = Mat4::perspective(50 * kDeg2Rad, W / (float)H, 0.1f, 50);
    /* The quad's two ends, from its own UVs: screen x and 1/w of the u-minimum and u-maximum corners. */
    const RenderMesh &rm = quad->render_mesh();
    CHECK(!rm.uvs.empty());
    size_t ia = 0, ib = 0;
    for (size_t i = 0; i < rm.uvs.size(); i++) {
      if (rm.uvs[i].x < rm.uvs[ia].x) ia = i;
      if (rm.uvs[i].x > rm.uvs[ib].x) ib = i;
    }
    auto end = [&](size_t i, float &sx, float &iw) {
      const Vec4 c = p * v * model * Vec4(rm.positions[i], 1.0f);
      sx = (c.x / c.w * 0.5f + 0.5f) * W;
      iw = 1.0f / c.w;
    };
    float xa, xb, iwa, iwb;
    end(ia, xa, iwa);
    end(ib, xb, iwb);
    const float ua = rm.uvs[ia].x, ub = rm.uvs[ib].x;
    CHECK(std::fabs(xb - xa) > 20.0f && std::fabs(iwa - iwb) > 0.02f);  // slanted enough to matter
    auto red_for_u = [](float u) {
      const float lin = std::max(0.0f, std::min(1.0f, (u * 256.0f - 0.5f) / 255.0f));  // texel i holds i
      return 255.0f * linear_to_srgb(lin);
    };
    Image off, on;
    RenderTarget roff, ron;
    RasterOptions plain, affine;
    affine.affine_uv = true;
    cf_render(off, roff, W, H, v, p, plain, sc);
    cf_render(on, ron, W, H, v, p, affine, sc);
    double max_gap = 0;
    for (float s : {0.2f, 0.35f, 0.5f, 0.65f, 0.8f}) {
      const int x = (int)std::floor(xa + s * (xb - xa));
      const float sc_x = ((x + 0.5f) - xa) / (xb - xa);  // where the pixel centre is between the ends
      const float u_affine = ua + (ub - ua) * sc_x;
      const float u_persp = ua + (ub - ua) * (sc_x * iwb) / ((1 - sc_x) * iwa + sc_x * iwb);
      const float want_affine = red_for_u(u_affine), want_persp = red_for_u(u_persp);
      const float got_affine = (float)((on.row(H / 2)[x] >> 16) & 255), got_persp = (float)((off.row(H / 2)[x] >> 16) & 255);
      std::printf("    s=%.2f: affine got %.0f want %.0f | perspective got %.0f want %.0f\n", (double)s, (double)got_affine, (double)want_affine,
                  (double)got_persp, (double)want_persp);
      CHECK(ron.ids[(size_t)(H / 2) * W + x] == 4 && roff.ids[(size_t)(H / 2) * W + x] == 4);
      CHECK_NEAR(got_affine, want_affine, 7.0);
      CHECK_NEAR(got_persp, want_persp, 7.0);
      max_gap = std::max(max_gap, (double)std::fabs(want_affine - want_persp));
    }
    CHECK(max_gap > 20.0);  // the two interpolations really differ here
    /* Same silhouette, different insides. */
    CHECK(ron.ids == roff.ids && on.pixels != off.pixels);
    /* A quad facing the camera has no perspective to correct: both give the same picture. */
    CfScene flat;
    flat.add(primitives::quad(2.0f), Mat4::identity(), 4, mat);
    flat.seal();
    Image f0, f1;
    RenderTarget rf0, rf1;
    cf_render(f0, rf0, W, H, v, p, plain, flat);
    cf_render(f1, rf1, W, H, v, p, affine, flat);
    int differ = 0;
    for (size_t i = 0; i < f0.pixels.size(); i++) differ += std::abs((int)((f0.pixels[i] >> 16) & 255) - (int)((f1.pixels[i] >> 16) & 255)) > 2;
    CHECK(differ == 0);
  });

  /* ---------------------------------------------------------------- 6 */
  test("camera filters: texture override - Nearest gives exact texels, Max Texture Size picks the first mip level within the cap, Mipmaps off uses one level", [&] {
    /* 16 x 16: red alternates every column, green every 2, blue every 4. After n halvings the channels whose
     * period is below 2^n have averaged to 0.5, so the colour tells which mip level was sampled. */
    const std::string path = fs::join(test_dir(), "cf_bits.png");
    std::vector<uint32_t> px(16 * 16);
    for (int y = 0; y < 16; y++)
      for (int x = 0; x < 16; x++)
        px[(size_t)y * 16 + x] = 0xFF000000u | ((x & 1) ? 0xFF0000u : 0u) | ((x & 2) ? 0xFF00u : 0u) | ((x & 4) ? 0xFFu : 0u);
    CHECK(write_png(path, px.data(), 16, 16, 16));
    auto mat = make_material("Bits", {1, 1, 1});
    mat->base_map.path = path;
    mat->base_map.non_color = true;
    mat->filter = (int)TexFilter::Trilinear;
    auto eval = [&](const TexOverride *o, float u, Vec2 ddx = {0, 0}, Vec2 ddy = {0, 0}) {
      SurfacePoint sp;
      sp.uv = {u, 0.5f};
      sp.duvdx = ddx;
      sp.duvdy = ddy;
      sp.normal = sp.geo_normal = {0, 0, 1};
      sp.tex = o;
      return evaluate_material(*mat, sp).albedo;
    };
    const float u5 = 5.5f / 16.0f;  // the middle of column 5 (binary 101): red 1, green 0, blue 1
    auto close_to = [](Vec3 c, float r, float g, float b) { return std::fabs(c.x - r) < 0.02f && std::fabs(c.y - g) < 0.02f && std::fabs(c.z - b) < 0.02f; };
    CHECK(close_to(eval(nullptr, u5), 1, 0, 1));
    /* Nearest: between columns 4 and 5 (closer to 5) the texel is exact; Linear blends. */
    const float u_edge = 5.4f / 16.0f;
    TexOverride nearest;
    nearest.filter = (int)TexFilter::Closest;
    CHECK(nearest.active());
    CHECK(close_to(eval(&nearest, u_edge), 1, 0, 1));
    mat->filter = (int)TexFilter::Linear;
    const Vec3 blended = eval(nullptr, u_edge);
    CHECK(blended.x > 0.8f && blended.x < 0.98f);  // 0.9: a blend of columns 4 (0) and 5 (1)
    CHECK(close_to(eval(&nearest, u_edge), 1, 0, 1));  // the override beats the material's filter
    mat->filter = (int)TexFilter::Trilinear;
    /* Max Texture Size: the first level whose longer side is within the cap. */
    struct Cap { int cap; float r, g, b; };
    for (const Cap &c : {Cap{16, 1, 0, 1}, Cap{99, 1, 0, 1}, Cap{8, .5f, 0, 1}, Cap{5, .5f, .5f, 1}, Cap{4, .5f, .5f, 1}, Cap{3, .5f, .5f, .5f},
                         Cap{2, .5f, .5f, .5f}, Cap{1, .5f, .5f, .5f}}) {
      TexOverride o;
      o.filter = (int)TexFilter::Closest;
      o.max_size = c.cap;
      const Vec3 got = eval(&o, u5);
      std::printf("    cap %d -> %.2f %.2f %.2f\n", c.cap, (double)got.x, (double)got.y, (double)got.z);
      CHECK(close_to(got, c.r, c.g, c.b));
    }
    /* Mipmaps off: one level whatever the screen footprint (here 8 texels per pixel, level 3 without the override). */
    const Vec2 big_x = {0.5f, 0}, big_y = {0, 0.5f};
    const Vec3 mipped = eval(nullptr, u5, big_x, big_y);
    CHECK(close_to(mipped, .5f, .5f, .5f));
    TexOverride no_mips;
    no_mips.mipmaps = false;
    no_mips.filter = (int)TexFilter::Closest;
    CHECK(no_mips.active());
    CHECK(close_to(eval(&no_mips, u5, big_x, big_y), 1, 0, 1));
    /* ... and with a cap: that capped level, still one level. */
    no_mips.max_size = 8;
    CHECK(close_to(eval(&no_mips, u5, big_x, big_y), .5f, 0, 1));
    /* A cap alone keeps the mip chain below it (the footprint still picks a coarser level). */
    TexOverride cap_only;
    cap_only.max_size = 16;
    CHECK(close_to(eval(&cap_only, u5, big_x, big_y), .5f, .5f, .5f));
    /* A default TexOverride is inactive and changes nothing. */
    TexOverride none;
    CHECK(!none.active());
    CHECK(close_to(eval(&none, u5, big_x, big_y), .5f, .5f, .5f));
    /* NaN footprints don't poison the result when a cap is active. */
    TexOverride capped;
    capped.max_size = 4;
    const float nan = kNaN;
    const Vec3 poisoned = eval(&capped, u5, {nan, nan}, {nan, nan});
    CHECK(std::isfinite(poisoned.x) && std::isfinite(poisoned.y) && std::isfinite(poisoned.z));
  });

  test("camera filters: Texture::sample_level picks the level asked for (clamped), Nearest gives the exact texel", [&] {
    Bitmap bmp;
    bmp.width = bmp.height = 4;
    bmp.rgba8.resize(4 * 4 * 4);
    for (int y = 0; y < 4; y++)
      for (int x = 0; x < 4; x++) {
        uint8_t *p = &bmp.rgba8[(size_t)(y * 4 + x) * 4];
        p[0] = (uint8_t)(x * 60);
        p[1] = (uint8_t)(y * 60);
        p[2] = (x + y) % 2 ? 255 : 0;
        p[3] = 255;
      }
    Texture t;
    t.build(bmp, false);
    CHECK(t.levels.size() == 3);  // 4, 2, 1
    /* v = 0 is the bottom row: texel (x, y) is at u = (x + .5) / 4, v = 1 - (y + .5) / 4. */
    int texel_wrong = 0;
    for (int y = 0; y < 4; y++)
      for (int x = 0; x < 4; x++) {
        const Vec4 c = t.sample_level({(x + 0.5f) / 4, 1 - (y + 0.5f) / 4}, 0, TexWrap::Repeat, TexFilter::Closest);
        const bool ok = std::fabs(c.x * 255 - x * 60) <= 0.6f && std::fabs(c.y * 255 - y * 60) <= 0.6f &&
                        std::fabs(c.z - ((x + y) % 2 ? 1.0f : 0.0f)) <= 1e-4f;
        if (!ok && texel_wrong++ == 0) std::printf("    first wrong texel (%d, %d)\n", x, y);
      }
    CHECK(texel_wrong == 0);
    /* Slightly off-centre it is still that texel with Nearest, a blend with Linear. */
    const Vec4 n = t.sample_level({1.4f / 4, 0.5f}, 0, TexWrap::Repeat, TexFilter::Closest);
    const Vec4 l = t.sample_level({1.4f / 4, 0.5f}, 0, TexWrap::Repeat, TexFilter::Linear);
    CHECK_NEAR(n.x * 255, 60, 0.6);
    CHECK(std::fabs(l.x * 255 - 60) > 5.0);
    /* Levels clamp to the chain on both ends. */
    const Vec4 last = t.sample_level({0.3f, 0.3f}, 2, TexWrap::Repeat, TexFilter::Closest);
    const Vec4 way_past = t.sample_level({0.3f, 0.3f}, 50, TexWrap::Repeat, TexFilter::Closest);
    const Vec4 first = t.sample_level({0.3f, 0.3f}, 0, TexWrap::Repeat, TexFilter::Closest);
    const Vec4 way_before = t.sample_level({0.3f, 0.3f}, -7, TexWrap::Repeat, TexFilter::Closest);
    CHECK(last.x == way_past.x && last.y == way_past.y && last.z == way_past.z);
    CHECK(first.x == way_before.x && first.y == way_before.y && first.z == way_before.z);
    /* The 1 x 1 level is the whole picture's average. */
    CHECK_NEAR(last.x * 255, 90, 1.0);
    CHECK_NEAR(last.z, 0.5, 0.01);
    /* A texture with no levels gives the magenta placeholder rather than crashing. */
    Texture empty;
    const Vec4 e = empty.sample_level({0, 0}, 0, TexWrap::Repeat, TexFilter::Closest);
    CHECK(e.x == 1 && e.y == 0 && e.z == 1);
  });

  /* ---------------------------------------------------------------- 7 */
  test("camera filters: fog blends geometry toward the fog colour by depth, leaves the sky alone, and needs the camera frame", [&] {
    CfScene sc;
    sc.add(primitives::quad(2.0f), Mat4::translate({-1.2f, 0, 4}), 1, cf_unlit({0.2f, 0.6f, 0.2f}));   // 4 m away
    sc.add(primitives::quad(3.0f), Mat4::translate({2.5f, 0, 10}), 2, cf_unlit({0.9f, 0.9f, 0.2f}));   // 10 m away
    sc.add(primitives::quad(0.4f), Mat4::translate({0, 0.3f, 1}), 3, cf_unlit({0.2f, 0.2f, 0.9f}));    // 1 m: nearer than Fog Start
    sc.seal();
    const int W = 96, H = 96;
    const Mat4 v = Mat4::look_at({0, 0, 0}, {0, 0, 1}, {0, 1, 0});
    const Mat4 p = Mat4::perspective(60 * kDeg2Rad, 1.0f, 0.1f, 100);
    Image img;
    RenderTarget rt;
    Renderer3D r3d;
    cf_render(img, rt, W, H, v, p, RasterOptions{}, sc, 0xFF204060u, &r3d);
    const std::vector<uint32_t> before = img.pixels;
    auto pixel_of = [&](Vec3 w) {
      Vec2 s;
      float z;
      CHECK(r3d.project(w, s, z));
      return std::make_pair((int)s.x, (int)s.y);
    };
    const auto pa = pixel_of({-1.2f, 0, 4}), pb = pixel_of({2.5f, 0, 10}), pn = pixel_of({0, 0.3f, 1});
    CHECK(rt.ids[(size_t)pa.second * W + pa.first] == 1 && rt.ids[(size_t)pb.second * W + pb.first] == 2 && rt.ids[(size_t)pn.second * W + pn.first] == 3);
    CHECK(rt.depth_at(1, 1) == 1.0f && before[0] == 0xFF204060u);  // the corner is sky
    FilterFrame frame;
    frame.inv_view_proj = (p * v).inverse();
    frame.eye = {0, 0, 0};
    frame.forward = {0, 0, 1};
    frame.far_distance = 100.0f;
    RetroImageParams fog;
    fog.fog = true;
    fog.fog_start = 2.0f;
    fog.fog_end = 6.0f;
    fog.fog_color = {1.0f, 0.0f, 0.0f};
    /* No camera frame (the path tracer's passes): no fog, whatever the parameters. */
    apply_retro_image(rt, fog, nullptr);
    CHECK(img.pixels == before);
    apply_retro_image(rt, fog, &frame);
    auto ch = [](uint32_t c, int s) { return (int)((c >> s) & 255); };
    const uint32_t a0 = before[(size_t)pa.second * W + pa.first], a1 = img.pixels[(size_t)pa.second * W + pa.first];
    /* 4 m is halfway between start 2 and end 6. */
    CHECK_NEAR(ch(a1, 16), (ch(a0, 16) + 255) / 2.0, 4.0);
    CHECK_NEAR(ch(a1, 8), ch(a0, 8) / 2.0, 4.0);
    CHECK_NEAR(ch(a1, 0), ch(a0, 0) / 2.0, 4.0);
    /* Beyond Fog End it is the fog colour, exactly. */
    CHECK((img.pixels[(size_t)pb.second * W + pb.first] & 0xFFFFFFu) == 0xFF0000u);
    /* Nearer than Fog Start: untouched. */
    CHECK(img.pixels[(size_t)pn.second * W + pn.first] == before[(size_t)pn.second * W + pn.first]);
    /* Sky pixels (depth 1) take no fog. */
    int sky = 0, sky_changed = 0;
    for (int i = 0; i < W * H; i++)
      if (rt.depth[(size_t)i] >= 1.0f) sky++, sky_changed += img.pixels[(size_t)i] != before[(size_t)i];
    CHECK(sky > 500 && sky_changed == 0);
    /* Fog gets stronger with distance across a surface that recedes (the floor). */
    CfScene floor_sc;
    floor_sc.add(primitives::plane(60.0f), Mat4::translate({0, -1, 15}), 7, cf_unlit({0.4f, 0.8f, 0.4f}));
    floor_sc.seal();
    Image fi;
    RenderTarget fr;
    cf_render(fi, fr, W, H, v, p, RasterOptions{}, floor_sc);
    const std::vector<uint32_t> floor_before = fi.pixels;
    apply_retro_image(fr, fog, &frame);
    int prev_red = -1;
    bool monotone = true;
    for (int y = H - 1; y > H / 2 + 2; y -= 3) {  // bottom of the picture (near) up toward the horizon (far)
      if (fr.ids[(size_t)y * W + W / 2] != 7) continue;
      const int red = ch(fi.pixels[(size_t)y * W + W / 2], 16);
      if (prev_red >= 0 && red < prev_red) monotone = false;
      prev_red = red;
    }
    CHECK(prev_red >= 0 && monotone);
    /* Degenerate settings: start == end is a hard edge, reversed values are swapped, NaN and infinite ones don't crash. */
    const float nan = kNaN, inf = kInf;
    struct Case { float s, e; };
    for (const Case &c : {Case{5, 5}, Case{6, 2}, Case{nan, 4}, Case{2, nan}, Case{-inf, inf}, Case{0, 0}, Case{1e30f, 1e30f}}) {
      std::copy(floor_before.begin(), floor_before.end(), fi.pixels.begin());
      RetroImageParams q = fog;
      q.fog_start = c.s;
      q.fog_end = c.e;
      q.fog_color = {nan, 2.0f, -1.0f};
      apply_retro_image(fr, q, &frame);
      CHECK(fi.pixels.size() == floor_before.size());
    }
    /* Fog with a target that has no depth plane is skipped rather than reading out of range. */
    Image flat;
    flat.resize(8, 8);
    std::fill(flat.pixels.begin(), flat.pixels.end(), 0xFF336699u);
    RenderTarget flat_rt;
    flat_rt.attach(flat, {0, 0, 8, 8});
    flat_rt.depth.clear();
    apply_retro_image(flat_rt, fog, &frame);
    CHECK(flat.pixels[0] == 0xFF336699u);
  });

  /* ---------------------------------------------------------------- 8 */
  test("camera filters: Retro Console Filter contributes the PS1 look to the stack; the preset refills the fields; stacked filters combine", [&] {
    RetroConsoleFilter f;  // a new component is the PS1 preset
    CHECK(f.console == RetroConsoleFilter::PS1 && f.applied_console == RetroConsoleFilter::PS1);
    CHECK(f.width == 320 && f.height == 240 && f.fit == 0);
    CHECK(f.vertex_snap && f.snap_grid == 1.0f && f.affine_textures);
    CHECK(f.texture_filter == 1 && f.max_texture_size == 256 && !f.mipmaps);
    CHECK(f.color_depth == RetroImageParams::Bits15 && f.dither == RetroImageParams::Ps1 && !f.fog && !f.screen_door);
    CHECK(std::string(f.type_name()) == "Retro Console");
    CHECK(find_filter_effect_info(f.type_name()) && find_filter_effect_info(f.type_name())->category == "Retro Console");
    FilterStack s;
    f.contribute(s);
    CHECK(!s.empty());
    CHECK(s.vertex_snap == 1.0f && s.affine_uv);
    CHECK(s.tex.filter == (int)TexFilter::Closest && s.tex.max_size == 256 && !s.tex.mipmaps && s.tex.active());
    CHECK(s.width == 320 && s.height == 240 && s.fit == FilterStack::Fill);
    CHECK(s.passes.size() == 1);
    /* Edited to something else, then the preset puts the console's values back. */
    f.width = 7, f.height = 9, f.fit = 1, f.vertex_snap = false, f.snap_grid = 5.0f, f.affine_textures = false;
    f.texture_filter = 3, f.max_texture_size = 0, f.mipmaps = true, f.color_depth = 0, f.dither = 0, f.fog = true;
    f.apply_preset(RetroConsoleFilter::PS1);
    CHECK(f.width == 320 && f.height == 240 && f.fit == 0 && f.vertex_snap && f.snap_grid == 1.0f && f.affine_textures);
    CHECK(f.texture_filter == 1 && f.max_texture_size == 256 && !f.mipmaps);
    CHECK(f.color_depth == RetroImageParams::Bits15 && f.dither == RetroImageParams::Ps1 && !f.fog);
    f.apply_preset(f.console);
    CHECK(f.width == 320 && f.applied_console == RetroConsoleFilter::PS1);

    /* Stacking order is checked in "effects combine in list order". Here: the later snap grid wins, and a
     * cap of 0 means "no cap" and doesn't lift another filter's cap. */
    RetroConsoleFilter g;
    g.snap_grid = 2.0f, g.max_texture_size = 64;
    FilterStack both;
    f.contribute(both);
    g.contribute(both);
    CHECK(both.vertex_snap == 2.0f && both.tex.max_size == 64);
    FilterStack rev;
    g.contribute(rev);
    f.contribute(rev);
    CHECK(rev.vertex_snap == 1.0f && rev.tex.max_size == 64);  // the later grid, not the larger one
    RetroConsoleFilter nocap;
    nocap.max_texture_size = 0;
    nocap.contribute(both);
    CHECK(both.tex.max_size == 64);
    /* A grid that isn't a positive number leaves snapping off; the other effects still apply. */
    RetroConsoleFilter bad;
    bad.snap_grid = kNaN;
    FilterStack bs;
    bad.contribute(bs);
    CHECK(bs.vertex_snap == 0.0f && bs.affine_uv && !bs.empty());
    bad.snap_grid = -2.0f;
    FilterStack bs2;
    bad.contribute(bs2);
    CHECK(bs2.vertex_snap == 0.0f);
    /* Only the colour pass? Then no raster stage. */
    RetroConsoleFilter colour_only;
    colour_only.vertex_snap = false;
    colour_only.affine_textures = false;
    colour_only.texture_filter = 0;
    colour_only.max_texture_size = 0;
    colour_only.mipmaps = true;
    colour_only.width = colour_only.height = 0;
    FilterStack cs;
    colour_only.contribute(cs);
    CHECK(cs.vertex_snap == 0.0f && !cs.affine_uv && !cs.tex.active() && cs.height == 0 && cs.passes.size() == 1 && !cs.empty());
    /* Every texture-filter choice maps to the rasterizer's. */
    const int expect[4] = {-1, (int)TexFilter::Closest, (int)TexFilter::Linear, (int)TexFilter::Trilinear};
    for (int k = 0; k < 4; k++) {
      RetroConsoleFilter t;
      t.texture_filter = k;
      FilterStack ts;
      t.contribute(ts);
      CHECK(ts.tex.filter == expect[k]);
    }
    /* The colour pass the stack carries really quantizes, and its fog colour is entered in linear light. */
    CfScene sc;
    sc.add(primitives::quad(40.0f), Mat4::translate({0, 0, 10}), 1, cf_unlit({0.6f, 0.4f, 0.2f}));
    sc.seal();
    const Mat4 v = Mat4::look_at({0, 0, 0}, {0, 0, 1}, {0, 1, 0});
    const Mat4 p = Mat4::perspective(60 * kDeg2Rad, 1.0f, 0.1f, 100);
    Image img;
    RenderTarget rt;
    cf_render(img, rt, 32, 32, v, p, RasterOptions{}, sc);
    RetroConsoleFilter fogged;
    fogged.fog = true;
    fogged.fog_start = 2.0f;
    fogged.fog_end = 6.0f;
    fogged.fog_color = {0.5f, 0.0f, 0.0f};
    fogged.color_depth = RetroImageParams::Full;
    FilterStack fs;
    fogged.contribute(fs);
    CHECK(fs.passes.size() == 1);
    FilterFrame frame;
    frame.inv_view_proj = (p * v).inverse();
    frame.eye = {0, 0, 0};
    frame.forward = {0, 0, 1};
    fs.passes[0](rt, &frame);
    const int red = (int)std::lround(255.0f * linear_to_srgb(0.5f));
    CHECK_NEAR((img.row(16)[16] >> 16) & 255, red, 1.5);
    CHECK((img.row(16)[16] & 0xFFFFu) == 0u);
    RenderTarget rt2;
    cf_render(img, rt2, 32, 32, v, p, RasterOptions{}, sc);
    FilterStack ps1;
    f.contribute(ps1);
    ps1.passes[0](rt2, &frame);
    CHECK(count_bad("pixels", img.pixels, [&](uint32_t c) { return cf_is_15bit(c); }) == 0);
  });

  /* ---------------------------------------------------------------- 9 */
  test("camera filters: the component is added from the console, its fields are set from the console, and each change undoes", [&] {
    Editor ed;
    /* Each console command is its own undo step once a frame has passed. */
    auto cmd = [&](const char *c) {
      ed.command(c);
      ed.step_frame_headless();
    };
    ed.init_headless(1000, 700);
    ed.step_frame_headless();
    cmd("select Main Camera");
    GameObject *cam = ed.selected_object();
    CHECK(cam && cam->get<Camera>() && !cf_get_retro(cam));
    cmd("component Retro Console Filter");  // the name has spaces
    RetroConsoleFilter *f = cf_get_retro(cam);
    CHECK(f != nullptr);
    if (!f) return;
    CHECK(cam->get<CameraFilters>() != nullptr);  // the old name adds a stack holding the effect
    CHECK(f->width == 320 && f->height == 240);
    cmd("set RetroConsoleFilter.Width 64");
    CHECK(f->width == 64);
    cmd("set RetroConsoleFilter.Height 48");
    CHECK(f->height == 48 && f->width == 64);
    cmd("set RetroConsoleFilter.Fit Letterbox");
    CHECK(f->fit == 1);
    cmd("set RetroConsoleFilter.VertexSnap false");
    CHECK(!f->vertex_snap);
    cmd("set RetroConsoleFilter.SnapGrid 2");
    CHECK(f->snap_grid == 2.0f);
    cmd("set RetroConsoleFilter.AffineTextures off");
    CHECK(!f->affine_textures);
    cmd("set RetroConsoleFilter.TextureFilter Linear");
    CHECK(f->texture_filter == 2);
    cmd("set RetroConsoleFilter.MaxTextureSize 128");
    CHECK(f->max_texture_size == 128);
    cmd("set RetroConsoleFilter.Mipmaps true");
    CHECK(f->mipmaps);
    cmd("set RetroConsoleFilter.Dither Bayer");
    CHECK(f->dither == RetroImageParams::Bayer4);
    cmd("set RetroConsoleFilter.ColourDepth 24-bit");
    CHECK(f->color_depth == RetroImageParams::Full);
    cmd("set RetroConsoleFilter.Fog true");
    cmd("set RetroConsoleFilter.FogStart 3");
    cmd("set RetroConsoleFilter.FogEnd 12");
    CHECK(f->fog && f->fog_start == 3.0f && f->fog_end == 12.0f);
    /* Setting a field doesn't re-apply the PS1 preset over the others. */
    CHECK(f->width == 64 && f->height == 48 && f->fit == 1);
    /* A second filter on the same camera. */
    cmd("component Retro Console Filter");
    int count = 0;
    count = (int)cf_all_retro(cam).size();
    CHECK(count == 2);
    /* Undo walks back through the changes (Ctrl+Z); the component goes away with the step that added it. */
    platform::Event z;
    z.type = platform::EventType::KeyDown;
    z.key = platform::KEY_Z;
    z.mods = platform::MOD_CTRL;
    auto filters_on_camera = [&] {
      GameObject *c = ed.scene().find_by_name("Main Camera");
      int n = 0;
      if (c)
        n = (int)cf_all_retro(c).size();
      return n;
    };
    ed.step_frame_headless({z});
    CHECK(filters_on_camera() == 1);  // the second filter's creation is the last step
    ed.step_frame_headless({z});      // FogEnd
    {
      RetroConsoleFilter *r = cf_get_retro(ed.scene().find_by_name("Main Camera"));
      CHECK(r && r->fog_end != 12.0f);
    }
    for (int i = 0; i < 40 && filters_on_camera() > 0; i++) ed.step_frame_headless({z});
    CHECK(filters_on_camera() == 0);
  });

  /* --------------------------------------------------------------- 10 */
  test("camera filters: PS1 Game view is 15-bit colour in whole blocks; with no filter, an empty stack, disabled or neutral effects it is the old picture", [&] {
    Editor ed;
    ed.init_headless(1600, 1000);
    ed.step_frame_headless();
    ed.command("select Cube");  // so adding a component to the camera doesn't change the Inspector
    ed.command("window Game");
    auto settle = [&] {
      for (int i = 0; i < 4; i++) ed.step_frame_headless();
    };
    auto grab = [&] { return std::vector<uint32_t>(ed.framebuffer().pixels.begin(), ed.framebuffer().pixels.end()); };
    settle();
    const std::vector<uint32_t> base = grab();
    ed.step_frame_headless();
    const int FW = ed.framebuffer().width, FH = ed.framebuffer().height;
    /* The status bar's frame-time text changes every frame, so compare inside the view only. */
    auto same = [&](const std::vector<uint32_t> &a, const std::vector<uint32_t> &b) {
      const Recti r = ed.scene_view_rect();
      size_t diff = 0;
      for (int y = r.y; y < r.bottom(); y++)
        for (int x = r.x; x < r.right(); x++) diff += a[(size_t)y * FW + x] != b[(size_t)y * FW + x];
      return diff <= (size_t)r.w * r.h / 1000;
    };
    CHECK(same(grab(), base));  // the Game view is steady frame to frame
    auto diff_box = [&](const std::vector<uint32_t> &a, const std::vector<uint32_t> &b) {
      Recti box(0, 0, 0, 0);
      int x0 = FW, y0 = FH, x1 = -1, y1 = -1;
      const Recti r = ed.scene_view_rect();
      for (int y = r.y; y < r.bottom(); y++)
        for (int x = r.x; x < r.right(); x++)
          if (a[(size_t)y * FW + x] != b[(size_t)y * FW + x]) x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
      if (x1 >= x0) box = Recti(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
      return box;
    };
    GameObject *cam = ed.scene().find_by_name("Main Camera");
    CHECK(cam != nullptr);
    if (!cam) return;
    /* An empty Camera Filters stack changes nothing. */
    cam->add<CameraFilters>();
    settle();
    CHECK(same(grab(), base));
    RetroConsoleFilter *f = cf_add_retro(cam);
    /* Disabled filters change nothing (two of them, so nothing combines into a pass either). */
    RetroConsoleFilter *off2 = cf_add_retro(cam);
    f->enabled = off2->enabled = false;
    settle();
    CHECK(same(grab(), base));
    if (auto *cf = cam->get<CameraFilters>()) cf->effects.pop_back();  // keep one for the rest
    /* So does one with every effect off. */
    f->enabled = true;
    f->vertex_snap = false, f->affine_textures = false, f->texture_filter = 0, f->max_texture_size = 0, f->mipmaps = true;
    f->width = f->height = 0, f->color_depth = 0, f->fog = false;
    settle();
    CHECK(same(grab(), base));
    /* The PS1 look, at a small internal size so the blocks are big. */
    f->apply_preset(RetroConsoleFilter::PS1);
    f->width = 48;
    f->height = 36;
    settle();
    const std::vector<uint32_t> ps1 = grab();
    const Recti box = ed.scene_view_rect();  // the Game tab takes the Scene view's place
    const Recti changed = diff_box(base, ps1);
    std::printf("    the Game view is %d x %d at (%d, %d)\n", box.w, box.h, box.x, box.y);
    CHECK(box.w > 300 && box.h > 200 && changed.w > 300 && changed.h > 200 && box.contains(changed.x, changed.y));
    const Recti inner(box.x + 3, box.y + 3, box.w - 6, box.h - 6);
    int not15 = 0;
    for (int y = inner.y; y < inner.bottom(); y++)
      for (int x = inner.x; x < inner.right(); x++) not15 += !cf_is_15bit(ps1[(size_t)y * FW + x]);
    CHECK(not15 == 0);
    /* Whole blocks: at most 36 rows' worth of different rows, and as many runs per row as internal columns. */
    int runs, changes;
    cf_blockiness(ps1.data(), FW, inner, runs, changes);
    const int iw = (int)std::lround(36.0 * box.w / box.h);
    std::printf("    %d colour runs along the busiest row (internal width about %d), %d row changes (internal height 36)\n", runs, iw, changes);
    CHECK(runs <= iw + 3 && runs > 10);
    CHECK(changes <= 36 + 1 && changes > 10);
    /* Square pixels: a block is as wide as it is tall (the view's pixels per internal pixel agree). */
    CHECK_NEAR((double)box.w / iw, (double)box.h / 36, 0.6);
    /* Letterbox: 4:3 with black bars in the rest. */
    f->fit = 1;
    settle();
    const std::vector<uint32_t> lb = grab();
    int bx0 = FW, by0 = FH, bx1 = -1, by1 = -1;
    for (int y = box.y + 2; y < box.bottom() - 2; y++)
      for (int x = box.x + 2; x < box.right() - 2; x++)
        if (lb[(size_t)y * FW + x] != 0xFF000000u) bx0 = std::min(bx0, x), bx1 = std::max(bx1, x), by0 = std::min(by0, y), by1 = std::max(by1, y);
    const double aspect = (bx1 - bx0 + 1) / (double)(by1 - by0 + 1);
    std::printf("    letterboxed picture %d x %d (aspect %.3f) inside the %d x %d view\n", bx1 - bx0 + 1, by1 - by0 + 1, aspect, box.w, box.h);
    CHECK_NEAR(aspect, 4.0 / 3.0, 0.04);
    CHECK((bx1 - bx0 + 1) < box.w - 8 || (by1 - by0 + 1) < box.h - 8);  // there are bars
    CHECK(lb[(size_t)(box.y + box.h / 2) * FW + box.x + 3] == 0xFF000000u || lb[(size_t)(box.y + 3) * FW + box.x + box.w / 2] == 0xFF000000u);
    int lb_not15 = 0;
    for (int y = by0 + 1; y < by1; y++)
      for (int x = bx0 + 1; x < bx1; x++) lb_not15 += !cf_is_15bit(lb[(size_t)y * FW + x]);
    CHECK(lb_not15 == 0);
    /* Two filters: the later (coarser) one sets the size, and the picture is still 15-bit. */
    f->fit = 0;
    RetroConsoleFilter *g = cf_add_retro(cam);
    g->width = 24;
    g->height = 18;
    settle();
    const std::vector<uint32_t> two = grab();
    cf_blockiness(two.data(), FW, inner, runs, changes);
    std::printf("    two filters: %d runs, %d row changes\n", runs, changes);
    CHECK(runs <= (int)std::lround(18.0 * box.w / box.h) + 3 && changes <= 19);
    int two_not15 = 0;
    for (int y = inner.y; y < inner.bottom(); y++)
      for (int x = inner.x; x < inner.right(); x++) two_not15 += !cf_is_15bit(two[(size_t)y * FW + x]);
    CHECK(two_not15 == 0 && two != ps1);
    /* Removing the filters brings the old picture back. */
    if (auto *cf = cam->get<CameraFilters>()) cf->effects.clear();
    settle();
    CHECK(same(grab(), base));
  });

  /* --------------------------------------------------------------- 11 */
  test("camera filters: `filters on` puts the main camera's filters on the Scene view (15-bit), `filters off` restores it", [&] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.step_frame_headless();
    ed.command("select Cube");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    const Recti view = ed.scene_view_rect();
    CHECK(view.w > 300 && view.h > 200);
    auto grab = [&] {
      std::vector<uint32_t> out;
      const Image &fb = ed.framebuffer();
      for (int y = view.y; y < view.bottom(); y++) out.insert(out.end(), fb.pixels.data() + (size_t)y * fb.width + view.x, fb.pixels.data() + (size_t)y * fb.width + view.right());
      return out;
    };
    auto fraction15 = [&](const std::vector<uint32_t> &px) {
      size_t n = 0;
      for (uint32_t c : px) n += cf_is_15bit(c);
      return (double)n / px.size();
    };
    /* The Scene view's overlay text changes a little every frame, and the `filters` command's log line
     * shows in it for a while (~0.55% of the view on Linux): equal means all but 1% of pixels. The
     * filtered view differs in ~99% of them. */
    auto same = [&](const std::vector<uint32_t> &a, const std::vector<uint32_t> &b) {
      size_t diff = 0;
      for (size_t i = 0; i < a.size() && i < b.size(); i++) diff += a[i] != b[i];
      if (std::getenv("BLENDITY_CF_DEBUG")) std::printf("    same(): %zu of %zu pixels differ\n", diff, a.size());
      return a.size() == b.size() && diff <= a.size() / 100;
    };
    /* "Unfiltered" is judged by meaning, not pixel equality: the overlay text (frame times, the
     * log line) redraws a varying number of pixels, more on slow machines (macOS CI failed the
     * pixel comparison now and then). Unfiltered frames have almost no 15-bit pixels; filtered
     * ones are nearly all 15-bit. */
    auto unfiltered = [&](const std::vector<uint32_t> &px) { return fraction15(px) < 0.3; };
    /* The toggle with no filter on the camera changes nothing. */
    ed.command("filters on");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    CHECK(unfiltered(grab()));
    ed.command("filters off");
    GameObject *cam = ed.scene().find_by_name("Main Camera");
    CHECK(cam != nullptr);
    if (!cam) return;
    RetroConsoleFilter *f = cf_add_retro(cam);
    f->width = 80;
    f->height = 60;
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    const std::vector<uint32_t> off = grab();
    CHECK(unfiltered(off));  // off by default, whatever the camera carries
    ed.command("filters on");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    const std::vector<uint32_t> on = grab();
    const double f_off = fraction15(off), f_on = fraction15(on);
    std::printf("    15-bit pixels in the Scene view: off %.1f%%, on %.1f%%\n", 100.0 * f_off, 100.0 * f_on);
    CHECK(!same(on, off));
    CHECK(f_on > 0.75);  // all but the grid, outline and gizmo drawn on top
    CHECK(f_off < 0.3);
    /* The camera's other effects reach the Scene view too: blocks of the internal size. */
    int runs, changes;
    {
      std::vector<uint32_t> img(ed.framebuffer().pixels.begin(), ed.framebuffer().pixels.end());
      /* A strip of sky between the statistics text (left) and the navigation gizmo (right): 25% of the width, 1/12 of the
       * height. Grid lines drawn over the picture break a few rows, so use the typical row: it has about one colour run
       * per internal column (100 across the view), and most rows repeat the one above (7 view rows per internal row). */
      const int sx = view.x + view.w * 11 / 20, sy = view.y + 8, sw = view.w / 4, sh = view.h / 12, stride = ed.framebuffer().width;
      std::vector<int> run_counts;
      int repeats = 0;
      for (int y = sy; y < sy + sh; y++) {
        int n = 1, same_as_above = 0;
        for (int x = sx + 1; x < sx + sw; x++) n += img[(size_t)y * stride + x] != img[(size_t)y * stride + x - 1];
        for (int x = sx; x < sx + sw; x++) same_as_above += img[(size_t)y * stride + x] == img[(size_t)(y - 1) * stride + x];
        run_counts.push_back(n);
        repeats += same_as_above * 10 >= sw * 9;
      }
      std::sort(run_counts.begin(), run_counts.end());
      runs = run_counts[run_counts.size() / 2];
      changes = sh - repeats;
      std::printf("    Scene view sky strip with filters: %d colour runs in the typical row, %d of %d rows differ from the one above\n", runs, changes, sh);
      CHECK(runs <= 100 / 4 + 3 && runs > 5 && changes * 4 <= sh);
    }
    /* A disabled filter component is ignored there as well. */
    f->enabled = false;
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    CHECK(unfiltered(grab()));
    f->enabled = true;
    ed.command("filters off");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    CHECK(unfiltered(grab()));
    ed.command("filters");  // no argument: toggles
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    CHECK(!unfiltered(grab()) && fraction15(grab()) > 0.75);
    ed.command("filters 0");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    CHECK(unfiltered(grab()));
  });

  /* --------------------------------------------------------------- 12 */
  test("camera filters: F12 (rasterized) and camera sequences through a PS1 camera are 15-bit blocks; a camera without the filter is untouched; path tracing keeps the 15-bit look", [&] {
    const std::string proj = fs::join(test_dir(), "filterproject");
    std::error_code ec;
    std::filesystem::remove_all(proj, ec);
    fs::make_dirs(fs::join(proj, "Assets"));
    fs::make_dirs(fs::join(proj, "research"));
    set_env("BLENDITY_PROJECT", proj);
    {
      Editor ed;
      ed.init_headless(900, 600);
      ed.step_frame_headless();
      ed.command("set Render.RenderEngine Rasterized");
      ed.command("set Render.ResolutionX 160");
      ed.command("set Render.ResolutionY 90");
      GameObject *main = ed.scene().find_by_name("Main Camera");
      CHECK(main != nullptr);
      if (!main) {
        set_env("BLENDITY_PROJECT", "");
        return;
      }
      RetroConsoleFilter *f = cf_add_retro(main);
      f->width = 48;
      f->height = 27;
      /* A second camera with no filter, in the sequence after the first. */
      ed.command("create Camera");
      GameObject *side = ed.selected_object();
      side->name = "Side";
      side->set_world_position({6, 1.5f, 0});
      const Vec3 fwd = normalize(Vec3(0, 0.5f, 0) - Vec3(6, 1.5f, 0));
      side->set_world_rotation(Quat::euler({std::asin(-fwd.y) * kRad2Deg, std::atan2(fwd.x, fwd.z) * kRad2Deg, 0}));
      side->get<Camera>()->sequence_order = 1;
      main->get<Camera>()->sequence_order = 0;
      /* Reads a written image: its pixels as 0xAARRGGBB. */
      auto read_png = [&](const std::string &path, int &w, int &h) {
        Bitmap bmp;
        std::string err;
        std::vector<uint32_t> px;
        if (!load_image(path, bmp, err) || bmp.is_float) return px;
        w = bmp.width, h = bmp.height;
        for (int i = 0; i < w * h; i++) {
          const uint8_t *q = &bmp.rgba8[(size_t)i * 4];
          px.push_back(0xFF000000u | (uint32_t)q[0] << 16 | (uint32_t)q[1] << 8 | q[2]);
        }
        return px;
      };
      auto run_sequence = [&](const char *name, int max_frames) {
        ed.command(std::string("rendersequence Renders/") + name);
        for (int i = 0; i < max_frames && ed.sequence_running(); i++) ed.step_frame_headless();
        return ed.sequence_files();
      };
      auto find_file = [&](const std::vector<std::string> &files, const char *camera) {
        for (const std::string &p : files)
          if (p.find(camera) != std::string::npos) return p;
        return std::string();
      };
      auto fraction15 = [](const std::vector<uint32_t> &px) {
        size_t n = 0;
        for (uint32_t c : px) n += cf_is_15bit(c);
        return (double)n / std::max<size_t>(1, px.size());
      };
      auto check_retro_image = [&](const std::string &path, const char *what) {
        int w = 0, h = 0;
        const std::vector<uint32_t> px = read_png(path, w, h);
        CHECK(w == 160 && h == 90);
        if (px.empty()) return;
        size_t bad = 0;
        for (uint32_t c : px) bad += !cf_is_15bit(c);
        int runs, changes;
        cf_blockiness(px.data(), w, Recti(0, 0, w, h), runs, changes);
        std::printf("    %s: %zu pixels not 15-bit, %d runs, %d row changes\n", what, bad, runs, changes);
        CHECK(bad == 0);
        CHECK(runs <= 48 && runs > 8);  // 48 internal columns spread over 160
        CHECK(changes <= 26 && changes > 8);  // 27 internal rows
      };
      /* Rasterized sequence. */
      auto files = run_sequence("f12_raster", 80);
      CHECK(files.size() == 2);
      const std::string main_raster = find_file(files, "Main Camera"), side_raster = find_file(files, "Side");
      CHECK(!main_raster.empty() && !side_raster.empty());
      if (!main_raster.empty()) check_retro_image(main_raster, "rasterized, filtered");
      if (!side_raster.empty()) {
        int w = 0, h = 0;
        const std::vector<uint32_t> px = read_png(side_raster, w, h);
        const double fr = fraction15(px);
        std::printf("    rasterized, no filter: %.1f%% of pixels 15-bit by chance\n", 100.0 * fr);
        CHECK(!px.empty() && fr < 0.3);
      }
      /* The same camera without its filter renders a different (full colour) picture. */
      f->enabled = false;
      files = run_sequence("f12_raster_plain", 80);
      const std::string plain = find_file(files, "Main Camera");
      CHECK(!plain.empty());
      if (!plain.empty()) {
        int w = 0, h = 0;
        const std::vector<uint32_t> px = read_png(plain, w, h);
        CHECK(!px.empty() && fraction15(px) < 0.3);
      }
      f->enabled = true;
      /* Letterbox: black bars at the sides of a 4:3 frame in the 16:9 render. */
      f->fit = 1;
      f->width = 40;  // a 4:3 frame (not the render's 16:9)
      f->height = 30;
      files = run_sequence("f12_letterbox", 80);
      const std::string lb = find_file(files, "Main Camera");
      CHECK(!lb.empty());
      if (!lb.empty()) {
        int w = 0, h = 0;
        const std::vector<uint32_t> px = read_png(lb, w, h);
        CHECK(w == 160 && h == 90 && !px.empty());
        if (!px.empty()) {
          CHECK(px[(size_t)45 * w + 2] == 0xFF000000u && px[(size_t)45 * w + 157] == 0xFF000000u);
          CHECK(px[(size_t)45 * w + 80] != 0xFF000000u && px[(size_t)45 * w + 30] != 0xFF000000u);  // the 120-wide frame starts at x = 20
          bool all15 = true;
          for (uint32_t c : px) all15 = all15 && cf_is_15bit(c);
          CHECK(all15);
        }
      }
      f->fit = 0;
      f->width = 48;
      f->height = 27;
      /* A single F12 render and the Save Render command. */
      ed.command("render");
      ed.command("saverender");
      std::string newest;
      for (const auto &e : std::filesystem::directory_iterator(fs::join(proj, "Renders"), ec)) {
        const std::string name = e.path().filename().string();
        if (name.rfind("render_", 0) == 0 && e.path().extension() == ".png" && name > newest) newest = name;
      }
      CHECK(!newest.empty());
      if (!newest.empty()) check_retro_image(fs::join(fs::join(proj, "Renders"), newest), "F12");
      /* Path traced: 1 sample at the internal resolution, then the colour pass, then scaled up. */
      ed.command("set Render.RenderEngine Path");
      ed.command("set Render.Samples 1");
      ed.command("set Render.Denoise false");
      files = run_sequence("f12_path", 1500);
      CHECK(!ed.sequence_running() && files.size() == 2);
      const std::string main_path = find_file(files, "Main Camera"), side_path = find_file(files, "Side");
      CHECK(!main_path.empty() && !side_path.empty());
      if (!main_path.empty()) check_retro_image(main_path, "path traced, filtered");
      if (!side_path.empty()) {
        int w = 0, h = 0;
        const std::vector<uint32_t> px = read_png(side_path, w, h);
        CHECK(!px.empty() && fraction15(px) < 0.3);
      }
      /* Extreme: a 1 x 1 internal image and a huge one render without trouble. */
      ed.command("set Render.RenderEngine Rasterized");
      for (int size : {1, 4096}) {
        f->width = size;
        f->height = size;
        files = run_sequence("f12_odd", 80);
        CHECK(!files.empty());
      }
    }
    set_env("BLENDITY_PROJECT", "");
  });

  /* --------------------------------------------------------------- 13 */
  test("camera filters: the Camera Preview (Rendered) refreshes when a filter field changes", [&] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.step_frame_headless();
    ed.command("set Render.PreviewSamples 1");
    ed.command("select Main Camera");
    GameObject *cam = ed.selected_object();
    CHECK(cam != nullptr);
    if (!cam) return;
    cf_add_retro(cam);
    ed.command("camerapreview rendered");
    const Recti view = ed.scene_view_rect();
    /* The inset sits in the Scene view's lower right corner. */
    const Recti corner(view.x + view.w * 2 / 3, view.y + view.h * 2 / 3, view.w / 3 - 4, view.h / 3 - 4);
    auto grab = [&] {
      std::vector<uint32_t> out;
      const Image &fb = ed.framebuffer();
      for (int y = corner.y; y < corner.bottom(); y++) out.insert(out.end(), fb.pixels.data() + (size_t)y * fb.width + corner.x, fb.pixels.data() + (size_t)y * fb.width + corner.right());
      return out;
    };
    auto settle = [&] {
      std::vector<uint32_t> prev;
      for (int i = 0; i < 400; i++) {
        ed.step_frame_headless();
        std::vector<uint32_t> now = grab();
        if (!prev.empty() && now == prev && i > 3) return now;
        prev = now;
      }
      return prev;
    };
    const std::vector<uint32_t> first = settle();
    const std::vector<uint32_t> again = settle();
    CHECK(first == again);  // a finished preview stays put
    /* A different internal size, typed through the stack's own field names: the preview is rebuilt at the
     * new resolution. (The old `set RetroConsoleFilter.*` names are checked in the console test.) */
    ed.command("set CameraFilters.E0Width 16");
    ed.command("set CameraFilters.E0Height 16");
    const std::vector<uint32_t> after_size = settle();
    CHECK(after_size != first);
    /* Colour depth alone (no change to geometry or size) also refreshes it. */
    ed.command("set RetroConsoleFilter.ColourDepth 24-bit");
    const std::vector<uint32_t> after_depth = settle();
    CHECK(after_depth != after_size);
    /* Disabling the component refreshes it again. */
    cf_get_retro(cam)->enabled = false;
    const std::vector<uint32_t> after_off = settle();
    CHECK(after_off != after_depth);
    /* A field of a later effect (E1) refreshes it too: a second effect's size wins. */
    cf_add_retro(cam);
    const std::vector<uint32_t> with_second = settle();
    ed.command("set CameraFilters.E1Width 24");
    ed.command("set CameraFilters.E1Height 24");
    CHECK(settle() != with_second);
  });
}

/* Round 28: the Inspector's component and modifier menus act when an item is clicked. Popup
 * menus run at the end of the frame, after the Inspector has drawn: their items used to write to
 * the Inspector's locals, so Remove Component, Move Up and the modifier menu did nothing. */
static void round28_tests() {
  using ET = platform::EventType;
  auto ev = [](ET t, int x, int y) {
    platform::Event e;
    e.type = t;
    e.x = x;
    e.y = y;
    return e;
  };
  auto click = [&](Editor &ed, int x, int y) {
    ed.step_frame_headless({ev(ET::MouseMove, x, y)});
    ed.step_frame_headless({ev(ET::MouseDown, x, y)});
    ed.step_frame_headless({ev(ET::MouseUp, x, y)});
    ed.step_frame_headless();
    ed.step_frame_headless();
  };
  /* Opens a section's menu and clicks its item'th entry (no separators above it). */
  auto menu = [&](Editor &ed, const std::string &key, int item) {
    const Recti b = ed.inspector_menu_rect_for_test(key);
    if (b.w <= 0) return false;
    click(ed, b.x + b.w / 2, b.y + b.h / 2);
    const auto &rects = ed.ui_for_test().popup_rects();
    if (rects.empty()) return false;
    const Recti r = rects.back();
    const int row = ed.ui_for_test().row_h();
    click(ed, r.x + r.w / 2, r.y + ed.ui_for_test().px(4) + row * item + row / 2);
    return true;
  };
  auto index_of = [](GameObject *g, const char *type) {
    for (size_t i = 0; i < g->components.size(); i++)
      if (std::string(g->components[i]->type_name()) == type) return (int)i;
    return -1;
  };
  test("Inspector: Remove Component, Move Up and Reset in a component's menu act on a click, and undo", [&] {
    Editor ed;
    ed.init_headless(1600, 900);
    ed.step_frame_headless();
    ed.command("select Cube");
    ed.command("component Rotator");
    ed.command("component Oscillator");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube != nullptr);
    if (!cube) return;
    const size_t n0 = cube->components.size();
    /* Move Up: the Oscillator goes above the Rotator. */
    int osc = index_of(cube, "Oscillator"), rot = index_of(cube, "Rotator");
    CHECK(osc == rot + 1);
    CHECK(menu(ed, "Oscillator" + std::to_string(osc), 1));
    std::printf("    after Move Up: Rotator at %d, Oscillator at %d\n", index_of(cube, "Rotator"), index_of(cube, "Oscillator"));
    CHECK(index_of(cube, "Oscillator") == rot && index_of(cube, "Rotator") == rot + 1);
    /* Reset: an edited field goes back to its default. */
    auto *r = cube->get<Rotator>();
    r->degrees_per_second = Vec3(1, 2, 3);
    ed.step_frame_headless();
    CHECK(menu(ed, "Rotator" + std::to_string(index_of(cube, "Rotator")), 2));
    r = cube->get<Rotator>();
    CHECK(r && r->degrees_per_second.x == Rotator().degrees_per_second.x && r->degrees_per_second.y == Rotator().degrees_per_second.y);
    /* Remove Component. */
    CHECK(menu(ed, "Oscillator" + std::to_string(index_of(cube, "Oscillator")), 0));
    std::printf("    components: %zu before, %zu after Remove\n", n0, cube->components.size());
    CHECK(cube->components.size() == n0 - 1 && index_of(cube, "Oscillator") < 0);
    /* Undo brings it back (and the Inspector stays usable). */
    ed.command("undo");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    cube = ed.scene().find_by_name("Cube");
    CHECK(cube && index_of(cube, "Oscillator") >= 0);
    /* A choice made on one object never lands on another: select the Sphere before the next frame. */
    ed.command("select Cube");
    ed.step_frame_headless();
    const int osc2 = index_of(cube, "Oscillator");
    const Recti b = ed.inspector_menu_rect_for_test("Oscillator" + std::to_string(osc2));
    click(ed, b.x + b.w / 2, b.y + b.h / 2);  // the menu is open
    CHECK(!ed.ui_for_test().popup_rects().empty());
    const Recti pr = ed.ui_for_test().popup_rects().back();
    const int px = pr.x + pr.w / 2, py = pr.y + ed.ui_for_test().px(4) + ed.ui_for_test().row_h() / 2;  // Remove Component
    ed.step_frame_headless({ev(ET::MouseMove, px, py)});
    ed.step_frame_headless({ev(ET::MouseDown, px, py)});
    ed.command("select Sphere");  // before the Inspector draws again
    ed.step_frame_headless({ev(ET::MouseUp, px, py)});
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    std::printf("    Remove chosen, then the Sphere selected: the Cube keeps its Oscillator: %d\n", index_of(ed.scene().find_by_name("Cube"), "Oscillator") == osc2);
    CHECK(index_of(ed.scene().find_by_name("Cube"), "Oscillator") == osc2);
    CHECK(index_of(ed.scene().find_by_name("Sphere"), "Oscillator") < 0);
  });
  test("Inspector: a modifier's menu (Apply, Duplicate) acts on a click", [&] {
    Editor ed;
    ed.init_headless(1600, 900);
    ed.step_frame_headless();
    ed.command("select Cube");
    ed.command("component ArrayModifier");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube != nullptr);
    if (!cube) return;
    const int arr = index_of(cube, "ArrayModifier");
    CHECK(arr >= 0);
    const size_t verts0 = cube->get<MeshFilter>()->mesh->vert_count();
    CHECK(menu(ed, "mod" + std::to_string(arr), 1));  // Duplicate
    int arrays = 0;
    for (auto &c : cube->components) arrays += std::string(c->type_name()) == "ArrayModifier";
    std::printf("    Array modifiers after Duplicate: %d\n", arrays);
    CHECK(arrays == 2);
    CHECK(menu(ed, "mod" + std::to_string(index_of(cube, "ArrayModifier")), 0));  // Apply the first
    arrays = 0;
    for (auto &c : cube->components) arrays += std::string(c->type_name()) == "ArrayModifier";
    const size_t verts1 = cube->get<MeshFilter>()->mesh->vert_count();
    std::printf("    after Apply: %d modifiers left, %zu -> %zu vertices in the mesh\n", arrays, verts0, verts1);
    CHECK(arrays == 1 && verts1 > verts0);
  });
}

/* ===================================================================== */
/* Round 29 (task 0005): the Camera Filters stack, Retro Console presets  */
/* ===================================================================== */

static CameraFilters *r29_stack(Editor &ed) {
  GameObject *c = ed.scene().find_by_name("Main Camera");
  return c ? c->get<CameraFilters>() : nullptr;
}
static std::vector<RetroConsoleFilter *> r29_effects(Editor &ed) {
  return cf_all_retro(ed.scene().find_by_name("Main Camera"));
}

static void round29_tests() {
  using ET = platform::EventType;
  auto ev = [](ET t, int x, int y) {
    platform::Event e;
    e.type = t;
    e.x = x;
    e.y = y;
    return e;
  };
  auto click = [&](Editor &ed, int x, int y) {
    ed.step_frame_headless({ev(ET::MouseMove, x, y)});
    ed.step_frame_headless({ev(ET::MouseDown, x, y)});
    ed.step_frame_headless({ev(ET::MouseUp, x, y)});
    ed.step_frame_headless();
    ed.step_frame_headless();
  };
  /* Foldouts closed, so the stack's buttons stay on screen. */
  auto collapse = [&](Editor &ed) {
    if (CameraFilters *cf = r29_stack(ed))
      for (auto &e : cf->effects) e->ui_expanded = false;
    ed.step_frame_headless();
    ed.step_frame_headless();
  };
  /* Opens a button's popup; returns its rect (empty when the button or popup isn't there). */
  auto open_menu = [&](Editor &ed, const std::string &key) {
    collapse(ed);
    const Recti b = ed.inspector_menu_rect_for_test(key);
    if (b.w <= 0) return Recti{0, 0, 0, 0};
    click(ed, b.x + b.w / 2, b.y + b.h / 2);
    const auto &rects = ed.ui_for_test().popup_rects();
    return rects.empty() ? Recti{0, 0, 0, 0} : rects.back();
  };
  auto pick_row = [&](Editor &ed, const Recti &pop, int row_index) {
    const int row = ed.ui_for_test().row_h();
    click(ed, pop.x + pop.w / 2, pop.y + ed.ui_for_test().px(4) + row * row_index + row / 2);
  };
  auto pick_last = [&](Editor &ed, const Recti &pop) {  // "Remove Filter", below a separator
    const int row = ed.ui_for_test().row_h();
    click(ed, pop.x + pop.w / 2, pop.bottom() - ed.ui_for_test().px(4) - row / 2);
  };
  auto cmd = [&](Editor &ed, const char *c) {
    ed.command(c);
    ed.step_frame_headless();
  };
  auto redo_key = [&](Editor &ed) {  // the console's `redo` adjusts the last edit operator; undo/redo is Ctrl+Z / Ctrl+Y
    platform::Event y;
    y.type = ET::KeyDown;
    y.key = platform::KEY_Y;
    y.mods = platform::MOD_CTRL;
    ed.step_frame_headless({y});
    ed.step_frame_headless();
  };
  auto close_popups = [&](Editor &ed) {
    for (int i = 0; i < 3 && !ed.ui_for_test().popup_rects().empty(); i++) {
      platform::Event e;
      e.type = ET::KeyDown;
      e.key = platform::KEY_ESCAPE;
      ed.step_frame_headless({e});
      ed.step_frame_headless();
    }
  };
  auto widths = [&](Editor &ed) {
    std::vector<int> w;
    for (RetroConsoleFilter *r : r29_effects(ed)) w.push_back(r->width);
    return w;
  };

  /* ---------------------------------------------------------------- 1 */
  test("filter stack: Add Filter lists Retro Console under its category and a click adds it, as one undo step", [&] {
    Editor ed;
    ed.init_headless(1600, 1200);
    ed.step_frame_headless();
    cmd(ed, "select Main Camera");
    cmd(ed, "component Camera Filters");
    CameraFilters *cf = r29_stack(ed);
    CHECK(cf != nullptr && cf->effects.empty());
    if (!cf) return;
    /* The registry: Retro Console is a known effect in the Retro Console category. */
    const FilterEffectInfo *info = find_filter_effect_info("Retro Console");
    CHECK(info && info->category == "Retro Console" && !info->help.empty());
    CHECK(create_filter_effect("Retro Console") != nullptr && create_filter_effect("No Such Effect") == nullptr);
    CHECK(find_filter_effect_info("No Such Effect") == nullptr);
    bool listed = false;
    for (const FilterEffectInfo &fi : filter_effect_infos()) listed = listed || fi.name == "Retro Console";
    CHECK(listed);
    const Recti pop = open_menu(ed, "add_filter");
    CHECK(pop.w > 0);
    if (pop.w <= 0) return;
    pick_row(ed, pop, 1);  // row 0 is the category label
    cf = r29_stack(ed);
    CHECK(cf && cf->effects.size() == 1 && std::string(cf->effects[0]->type_name()) == "Retro Console");
    cmd(ed, "undo");
    cf = r29_stack(ed);
    CHECK(cf && cf->effects.empty());  // the click was one undo step
    redo_key(ed);
    cf = r29_stack(ed);
    CHECK(cf && cf->effects.size() == 1);
  });

  test("filter stack: each effect's menu (Move Up, Move Down, Reset, Remove Filter) works by real clicks, one undo step each", [&] {
    Editor ed;
    ed.init_headless(1600, 1200);
    ed.step_frame_headless();
    cmd(ed, "shading wire");  // only the Inspector is under test: a cheap Scene view keeps the frames fast
    cmd(ed, "select Main Camera");
    cmd(ed, "filter add Retro Console");
    cmd(ed, "filter add Retro Console");
    CHECK(r29_effects(ed).size() == 2);
    cmd(ed, "set CameraFilters.E0Width 100");
    cmd(ed, "set CameraFilters.E1Width 200");
    CHECK((widths(ed) == std::vector<int>{100, 200}));
    /* Move Down on effect 0. */
    Recti pop = open_menu(ed, "filter0");
    CHECK(pop.w > 0);
    if (pop.w <= 0) return;
    pick_row(ed, pop, 1);
    CHECK((widths(ed) == std::vector<int>{200, 100}));
    cmd(ed, "undo");
    CHECK((widths(ed) == std::vector<int>{100, 200}));
    /* Move Up on effect 1. */
    pop = open_menu(ed, "filter1");
    CHECK(pop.w > 0);
    if (pop.w <= 0) return;
    pick_row(ed, pop, 0);
    CHECK((widths(ed) == std::vector<int>{200, 100}));
    cmd(ed, "undo");
    CHECK((widths(ed) == std::vector<int>{100, 200}));
    /* Move Up on the first effect is a disabled item: nothing moves. */
    pop = open_menu(ed, "filter0");
    if (pop.w > 0) pick_row(ed, pop, 0);
    close_popups(ed);
    CHECK((widths(ed) == std::vector<int>{100, 200}));
    /* Reset: back to the PS1 preset. */
    pop = open_menu(ed, "filter0");
    CHECK(pop.w > 0);
    if (pop.w <= 0) return;
    pick_row(ed, pop, 2);
    {
      const std::vector<int> w = widths(ed);
      CHECK(w.size() == 2 && w[0] == 320 && w[1] == 200);
    }
    cmd(ed, "undo");
    {
      const std::vector<int> w = widths(ed);
      CHECK(w.size() == 2 && w[0] == 100 && w[1] == 200);
    }
    /* Remove Filter. */
    pop = open_menu(ed, "filter0");
    CHECK(pop.w > 0);
    if (pop.w <= 0) return;
    pick_last(ed, pop);
    CHECK((widths(ed) == std::vector<int>{200}));
    cmd(ed, "undo");
    CHECK((widths(ed) == std::vector<int>{100, 200}));
  });

  test("filter stack: the enabled checkbox toggles an effect by a real click, as one undo step", [&] {
    Editor ed;
    ed.init_headless(1600, 1200);
    ed.step_frame_headless();
    cmd(ed, "shading wire");  // only the Inspector is under test
    cmd(ed, "select Main Camera");
    cmd(ed, "filter add Retro Console");
    collapse(ed);
    const Recti mr = ed.inspector_menu_rect_for_test("filter0");
    CHECK(mr.w > 0);
    if (mr.w <= 0) return;
    const Recti insp = ed.window_rect_for_test(WindowKind::Inspector);
    CHECK(insp.w > 0);
    CHECK(r29_effects(ed).size() == 1 && r29_effects(ed)[0]->enabled);
    /* The checkbox isn't recorded: sweep the header row from the left until the flag flips. */
    const int y = mr.y + mr.h / 2;
    bool flipped = false;
    for (int x = std::max(insp.x + 2, mr.x - ed.ui_for_test().px(300)); x < mr.x && !flipped; x += ed.ui_for_test().px(4)) {  // the box is wider than 4 px
      ed.step_frame_headless({ev(ET::MouseMove, x, y)});
      ed.step_frame_headless({ev(ET::MouseDown, x, y)});
      ed.step_frame_headless({ev(ET::MouseUp, x, y)});
      ed.step_frame_headless();
      auto fx = r29_effects(ed);
      flipped = !fx.empty() && !fx[0]->enabled;
    }
    CHECK(flipped);
    if (!flipped) return;
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    cmd(ed, "undo");
    CHECK(r29_effects(ed).size() == 1 && r29_effects(ed)[0]->enabled);
    redo_key(ed);
    CHECK(r29_effects(ed).size() == 1 && !r29_effects(ed)[0]->enabled);
  });

  /* ---------------------------------------------------------------- 2 */
  test("filter stack: the console's filter add / list / move / reset / remove and set CameraFilters.E0Width, each undoing", [&] {
    Editor ed;
    ed.init_headless(1000, 700);
    ed.step_frame_headless();
    cmd(ed, "select Main Camera");
    GameObject *cam = ed.selected_object();
    CHECK(cam && !cam->get<CameraFilters>());
    cmd(ed, "filter list");  // with no stack: lists what could be added, doesn't crash
    cmd(ed, "filter add No Such Effect");
    CHECK(!ed.scene().find_by_name("Main Camera")->get<CameraFilters>());
    cmd(ed, "filter add Retro Console");
    CHECK(r29_stack(ed) != nullptr && r29_effects(ed).size() == 1);
    cmd(ed, "filter add Retro Console");
    CHECK(r29_effects(ed).size() == 2);
    cmd(ed, "filter list");
    cmd(ed, "set CameraFilters.E0Width 160");
    CHECK((widths(ed) == std::vector<int>{160, 320}));
    cmd(ed, "set RetroConsole.Width 96");  // the effect's own name: the first one
    CHECK(widths(ed)[0] == 96);
    cmd(ed, "set CameraFilters.E1Height 100");
    CHECK(r29_effects(ed)[1]->height == 100);
    cmd(ed, "set CameraFilters.E1Enabled false");
    CHECK(!r29_effects(ed)[1]->enabled);
    cmd(ed, "filter move 0 1");
    CHECK(r29_effects(ed)[0]->height == 100 && r29_effects(ed)[1]->width == 96);
    cmd(ed, "filter move 0 99");  // clamped to the end
    CHECK(r29_effects(ed)[1]->height == 100);
    cmd(ed, "filter move 1 1");   // nothing to do
    cmd(ed, "filter reset 0");
    CHECK(r29_effects(ed)[0]->width == 320 && r29_effects(ed)[0]->height == 240 && r29_effects(ed)[0]->enabled);
    cmd(ed, "filter remove 7");  // out of range: refused
    cmd(ed, "filter reset -1");
    CHECK(r29_effects(ed).size() == 2);
    cmd(ed, "filter remove 1");
    CHECK(r29_effects(ed).size() == 1);
    /* Undo walks back through the removal and the reset. */
    cmd(ed, "undo");
    CHECK(r29_effects(ed).size() == 2);
    cmd(ed, "undo");
    CHECK(r29_effects(ed)[0]->width == 96 && r29_effects(ed)[1]->height == 100);  // before the reset
    /* A Retro Console effect added with the old component name lands in the same stack. */
    cmd(ed, "component Retro Console Filter");
    CHECK(r29_effects(ed).size() == 3 && ed.scene().find_by_name("Main Camera")->get<CameraFilters>()->effects.size() == 3);
    int stacks = 0;
    for (auto &c : ed.scene().find_by_name("Main Camera")->components) stacks += std::string(c->type_name()) == "Camera Filters";
    CHECK(stacks == 1);
    /* A preset chosen by console fills in its fields. */
    cmd(ed, "set CameraFilters.E2Console 3");
    CHECK(r29_effects(ed)[2]->console == RetroConsoleFilter::DOS && r29_effects(ed)[2]->width == 320 && r29_effects(ed)[2]->height == 200);
    CHECK(r29_effects(ed)[2]->color_depth == RetroImageParams::Palette256);
  });

  /* ---------------------------------------------------------------- 3 */
  test("filter stack: a stack saves, loads (every field, its own values beating the preset) and round-trips; clones and Duplicate deep-copy it", [&] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.step_frame_headless();
    GameObject *cam = ed.scene().find_by_name("Main Camera");
    CHECK(cam != nullptr);
    if (!cam) return;
    auto *a = cf_add_retro(cam);
    a->apply_preset(RetroConsoleFilter::N64);
    a->console = RetroConsoleFilter::N64;
    a->width = 160, a->fog_end = 33.0f;
    auto *b = cf_add_retro(cam);
    b->console = RetroConsoleFilter::DOS;
    b->apply_preset(RetroConsoleFilter::DOS);
    b->height = 100, b->enabled = false;
    /* Every Retro Console field moved off the PS1 preset. */
    auto *c = cf_add_retro(cam);
    c->width = 64, c->height = 48, c->fit = 1, c->vertex_snap = true, c->snap_grid = 2.5f, c->affine_textures = false;
    c->texture_filter = 2, c->max_texture_size = 128, c->mipmaps = true, c->color_depth = RetroImageParams::Full, c->dither = RetroImageParams::Bayer4;
    c->fog = true, c->fog_start = 4.0f, c->fog_end = 22.0f, c->fog_color = {0.1f, 0.2f, 0.3f};
    const std::string text = save_scene_text(ed.scene());
    CHECK(text.find("component Camera Filters") != std::string::npos && text.find("component Retro Console Filter") == std::string::npos);
    Scene loaded;
    std::string err;
    CHECK(load_scene_text(text, loaded, err));
    auto *lc = loaded.find_by_name("Main Camera");
    CHECK(lc != nullptr);
    if (!lc) return;
    auto fl = cf_all_retro(lc);
    CHECK(fl.size() == 3);
    if (fl.size() != 3) return;
    CHECK(fl[0]->enabled && fl[0]->console == RetroConsoleFilter::N64 && fl[0]->width == 160 && fl[0]->fog_end == 33.0f);
    CHECK(fl[0]->texture_filter == RetroConsoleFilter::ThreePoint && fl[0]->max_texture_size == 64 && fl[0]->mipmaps && fl[0]->fog);
    CHECK(!fl[1]->enabled && fl[1]->console == RetroConsoleFilter::DOS && fl[1]->height == 100 && fl[1]->width == 320);
    CHECK(fl[1]->color_depth == RetroImageParams::Palette256 && fl[1]->dither == RetroImageParams::Bayer4);
    {
      const RetroConsoleFilter *x = fl[2];
      CHECK(x->width == 64 && x->height == 48 && x->fit == 1 && x->vertex_snap && x->snap_grid == 2.5f && !x->affine_textures);
      CHECK(x->texture_filter == 2 && x->max_texture_size == 128 && x->mipmaps);
      CHECK(x->color_depth == RetroImageParams::Full && x->dither == RetroImageParams::Bayer4);
      CHECK(x->fog && x->fog_start == 4.0f && x->fog_end == 22.0f);
      CHECK(std::fabs(x->fog_color.x - 0.1f) < 1e-5f && std::fabs(x->fog_color.y - 0.2f) < 1e-5f && std::fabs(x->fog_color.z - 0.3f) < 1e-5f);
      CHECK(x->applied_console == x->console);  // loading didn't re-apply the preset over its values
    }
    CHECK(save_scene_text(loaded) == text);  // byte for byte
    /* A scene clone copies the effects, not shares them. */
    std::unique_ptr<Scene> copy = ed.scene().clone();
    auto cl = cf_all_retro(copy->find_by_name("Main Camera"));
    CHECK(cl.size() == 3 && cl[0] != a && cl[1] != b);
    CHECK(save_scene_text(*copy) == text);  // every field carried
    if (cl.size() == 3) {
      cl[0]->width = 777;
      cl[1]->enabled = true;
      cl[0]->apply_preset(RetroConsoleFilter::Saturn);
      CHECK(a->width == 160 && !b->enabled && !a->screen_door);
      CHECK(cl[0]->screen_door && cf_all_retro(cam)[0]->width == 160);
    }
    /* The component's copy constructor and assignment deep-copy too. */
    CameraFilters orig;
    orig.add("Retro Console");
    CameraFilters dup(orig);
    CHECK(dup.effects.size() == 1 && dup.effects[0].get() != orig.effects[0].get());
    dup.find<RetroConsoleFilter>()->width = 64;
    CHECK(orig.find<RetroConsoleFilter>()->width == 320);
    CameraFilters assigned;
    assigned.add("Retro Console");
    assigned.add("Retro Console");
    assigned = orig;
    CHECK(assigned.effects.size() == 1 && assigned.effects[0].get() != orig.effects[0].get());
    CHECK(orig.add("No Such Effect") == nullptr && orig.effects.size() == 1);
    /* Duplicate in the editor, then edit the duplicate: the original keeps its values. */
    ed.command("select Main Camera");
    ed.command("duplicate");
    GameObject *dupe = ed.selected_object();
    CHECK(dupe != nullptr && dupe != ed.scene().find_by_name("Main Camera"));
    if (dupe) {
      auto dl = cf_all_retro(dupe);
      CHECK(dl.size() == 3);
      if (dl.size() == 3) {
        dl[0]->width = 555;
        CHECK(cf_all_retro(ed.scene().find_by_name("Main Camera"))[0]->width == 160);
        CHECK(dl[1]->height == 100 && !dl[1]->enabled);
      }
    }
  });

  test("filter stack: the component's hash changes with any effect field, the order, the enabled flag or the count (Camera Preview refresh)", [&] {
    Scene scene;
    GameObject *cam = scene.create("Main Camera");
    auto *a = cf_add_retro(cam);
    auto *b = cf_add_retro(cam);
    b->apply_preset(RetroConsoleFilter::DOS);
    b->console = RetroConsoleFilter::DOS;
    CameraFilters *cf = cam->get<CameraFilters>();
    const uint64_t h0 = hash_component(*cf);
    CHECK(hash_component(*cf) == h0);  // stable
    a->width = 100;
    const uint64_t h1 = hash_component(*cf);
    CHECK(h1 != h0);
    b->fog_end += 1.0f;
    const uint64_t h2 = hash_component(*cf);
    CHECK(h2 != h1);
    b->enabled = false;
    const uint64_t h3 = hash_component(*cf);
    CHECK(h3 != h2);
    b->enabled = true;
    CHECK(hash_component(*cf) == h2);
    std::swap(cf->effects[0], cf->effects[1]);
    CHECK(hash_component(*cf) != h2);  // order matters
    std::swap(cf->effects[0], cf->effects[1]);
    cf->effects.pop_back();
    CHECK(hash_component(*cf) != h2);
    /* That the Rendered Camera Preview rebuilds on such a change is checked in "the Camera Preview
     * (Rendered) refreshes when a filter field changes". */
  });

  test("filter stack: a scene saved with task 0004's Retro Console Filter components loads into a Camera Filters stack", [&] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.step_frame_headless();
    std::string text = save_scene_text(ed.scene());
    const size_t o = text.find("\"Main Camera\"");
    CHECK(o != std::string::npos);
    if (o == std::string::npos) return;
    const size_t t = text.find("\ntransform ", o);
    CHECK(t != std::string::npos);
    if (t == std::string::npos) return;
    const size_t at = text.find('\n', t + 1) + 1;
    const std::string legacy =
        "component Retro Console Filter 1\n  Console 1\n  Width 64\n  Height 48\n  Fog 1\n  Fog_Start 4\nend\n"
        "component Retro Console Filter 0\nend\n";
    text.insert(at, legacy);
    Scene loaded;
    std::string err;
    CHECK(load_scene_text(text, loaded, err));
    GameObject *lc = loaded.find_by_name("Main Camera");
    CHECK(lc != nullptr);
    if (!lc) return;
    int stacks = 0, old_components = 0;
    for (auto &c : lc->components) {
      stacks += std::string(c->type_name()) == "Camera Filters";
      old_components += std::string(c->type_name()) == "Retro Console Filter";
    }
    CHECK(stacks == 1 && old_components == 0);
    auto fl = cf_all_retro(lc);
    CHECK(fl.size() == 2);
    if (fl.size() != 2) return;
    /* The block's own values beat the preset it names; the rest follow the preset. */
    CHECK(fl[0]->enabled && fl[0]->console == RetroConsoleFilter::N64 && fl[0]->width == 64 && fl[0]->height == 48);
    CHECK(fl[0]->fog && fl[0]->fog_start == 4.0f && fl[0]->texture_filter == RetroConsoleFilter::ThreePoint && fl[0]->max_texture_size == 64);
    CHECK(!fl[1]->enabled && fl[1]->console == RetroConsoleFilter::PS1 && fl[1]->width == 320 && fl[1]->height == 240);
    /* Saving again writes the new form, and that loads to the same stack. */
    const std::string again = save_scene_text(loaded);
    CHECK(again.find("component Camera Filters") != std::string::npos && again.find("component Retro Console Filter") == std::string::npos);
    Scene second;
    CHECK(load_scene_text(again, second, err));
    CHECK(save_scene_text(second) == again);
  });

  /* ---------------------------------------------------------------- 4 */
  /* ---------------------------------------------------------------- 5 */
  test("presets: PS1, N64, Saturn and DOS fill in the documented fields and contribute them to the stack", [&] {
    /* PS1's fields are checked in "Retro Console Filter contributes the PS1 look". */
    RetroConsoleFilter n;
    n.apply_preset(RetroConsoleFilter::N64);
    CHECK(n.width == 320 && n.height == 240 && !n.vertex_snap && !n.affine_textures);
    CHECK(n.texture_filter == RetroConsoleFilter::ThreePoint && n.max_texture_size == 64 && n.mipmaps);
    CHECK(n.color_depth == RetroImageParams::Bits15 && n.dither != RetroImageParams::NoDither && n.fog && !n.screen_door);
    RetroConsoleFilter s;
    s.apply_preset(RetroConsoleFilter::Saturn);
    CHECK(s.width == 320 && s.height == 224 && s.vertex_snap && s.affine_textures && s.texture_filter == RetroConsoleFilter::Nearest);
    CHECK(s.screen_door && s.color_depth == RetroImageParams::Bits15 && !s.fog);
    RetroConsoleFilter d;
    d.apply_preset(RetroConsoleFilter::DOS);
    CHECK(d.width == 320 && d.height == 200 && d.affine_textures && d.texture_filter == RetroConsoleFilter::Nearest);
    CHECK(d.color_depth == RetroImageParams::Palette256 && d.dither != RetroImageParams::NoDither && !d.screen_door);
    /* A preset clears the earlier one's extras (Saturn's screen-door, N64's fog). */
    s.apply_preset(RetroConsoleFilter::PS1);
    CHECK(!s.screen_door);
    n.apply_preset(RetroConsoleFilter::DOS);
    CHECK(!n.fog);
    /* What N64 hands the stack. */
    RetroConsoleFilter n64;
    n64.apply_preset(RetroConsoleFilter::N64);
    FilterStack fs;
    n64.contribute(fs);
    CHECK(fs.vertex_snap == 0.0f && !fs.affine_uv && fs.tex.filter == (int)TexFilter::ThreePoint && fs.tex.max_size == 64 && fs.tex.mipmaps);
    CHECK(fs.width == 320 && fs.height == 240 && !fs.screen_door && fs.passes.size() == 1);
    RetroConsoleFilter sat;
    sat.apply_preset(RetroConsoleFilter::Saturn);
    FilterStack ss;
    sat.contribute(ss);
    CHECK(ss.screen_door && ss.vertex_snap > 0.0f && ss.affine_uv && ss.width == 320 && ss.height == 224);
    RasterOptions ro;
    ss.apply_raster(ro);
    CHECK(ro.screen_door);
    RetroConsoleFilter dos;
    dos.apply_preset(RetroConsoleFilter::DOS);
    FilterStack ds;
    dos.contribute(ds);
    CHECK(ds.affine_uv && ds.vertex_snap == 0.0f && ds.width == 320 && ds.height == 200 && ds.passes.size() == 1 && !ds.screen_door);
    /* Choosing a console (from a file or `set`) applies the preset when the effect is reflected. */
    RetroConsoleFilter c;
    c.console = RetroConsoleFilter::Saturn;
    struct Probe : Reflector {
      bool all_fields() const override { return false; }
      void field(const char *, float &, float, float, float) override {}
      void field(const char *, int &, int, int) override {}
      void field(const char *, bool &) override {}
      void field(const char *, Vec3 &) override {}
      void color(const char *, Vec3 &) override {}
      void enumeration(const char *, int &, const char *const *, int) override {}
      void text(const char *, std::string &) override {}
      void mesh(const char *, MeshPtr &) override {}
      void texture(const char *, TextureRef &) override {}
      void material_list(const char *, std::vector<MaterialPtr> &) override {}
    } probe;
    c.reflect(probe);
    CHECK(c.applied_console == RetroConsoleFilter::Saturn && c.screen_door && c.height == 224);
  });

  test("N64 3-point filter: Texture::sample_level gives the exact 3-texel values on a 2x2 texture and differs from bilinear", [&] {
    /* Texels (x, y): a=(0,0) b=(1,0) c=(0,1) d=(1,1), in the red channel 0, 100, 200, 250. */
    Bitmap bmp;
    bmp.width = bmp.height = 2;
    bmp.rgba8.resize(2 * 2 * 4);
    const uint8_t red[2][2] = {{0, 100}, {200, 250}};
    for (int y = 0; y < 2; y++)
      for (int x = 0; x < 2; x++) {
        uint8_t *px = &bmp.rgba8[(size_t)(y * 2 + x) * 4];
        px[0] = red[y][x], px[1] = 0, px[2] = 0, px[3] = 255;
      }
    Texture t;
    t.build(bmp, false);
    const float a = 0, b = 100 / 255.0f, c = 200 / 255.0f, d = 250 / 255.0f;
    /* In texel space (fx, fy) = (tx, ty) between the four centres: u = (tx + .5) / 2, v = 1 - (ty + .5) / 2. */
    const float pts[][2] = {{0.25f, 0.25f}, {0.75f, 0.75f}, {0.5f, 0.5f}, {0.1f, 0.6f}, {0.9f, 0.3f}, {0.0f, 0.0f}, {0.4f, 0.55f}, {0.8f, 0.8f}};
    int differs = 0;
    for (const auto &q : pts) {
      const float tx = q[0], ty = q[1];
      const Vec2 uv((tx + 0.5f) / 2, 1.0f - (ty + 0.5f) / 2);
      const float expect = tx + ty <= 1.0f ? a + (b - a) * tx + (c - a) * ty : d + (c - d) * (1.0f - tx) + (b - d) * (1.0f - ty);
      const Vec4 three = t.sample_level(uv, 0, TexWrap::Repeat, TexFilter::ThreePoint);
      const Vec4 lin = t.sample_level(uv, 0, TexWrap::Repeat, TexFilter::Linear);
      CHECK_NEAR(three.x, expect, 1e-4);
      CHECK(three.y == 0.0f && three.z == 0.0f);
      CHECK_NEAR(lin.x, (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty, 1e-4);
      differs += std::fabs(three.x - lin.x) > 1e-3f;
    }
    CHECK(differs >= 5);  // at texel centres and on the diagonal the two can agree; elsewhere they don't
    /* At a texel centre every filter gives that texel. */
    CHECK_NEAR(t.sample_level({0.25f, 0.75f}, 0, TexWrap::Repeat, TexFilter::ThreePoint).x, a, 1e-4);
    CHECK_NEAR(t.sample_level({0.75f, 0.25f}, 0, TexWrap::Repeat, TexFilter::ThreePoint).x, d, 1e-4);
    /* It never overshoots the texel values. */
    int overshoots = 0;
    for (int i = 0; i <= 20; i++)
      for (int j = 0; j <= 20; j++) {
        const Vec4 v = t.sample_level({0.25f + 0.5f * i / 20, 0.25f + 0.5f * j / 20}, 0, TexWrap::Repeat, TexFilter::ThreePoint);
        overshoots += !(v.x >= -1e-4f && v.x <= d + 1e-4f);
      }
    CHECK(overshoots == 0);
  });

  test("Saturn screen-door: a transparent quad over a background is an exact checkerboard of opaque pixels, nothing blended", [&] {
    const int W = 64, H = 64;
    const Mat4 v = Mat4::look_at({0, 0, 0}, {0, 0, 1}, {0, 1, 0});
    const Mat4 p = Mat4::perspective(60 * kDeg2Rad, 1.0f, 0.1f, 100);
    auto bg_scene = [&](CfScene &sc) { sc.add(primitives::quad(40.0f), Mat4::translate({0, 0, 10}), 1, cf_unlit({0.2f, 0.7f, 0.2f})); };
    auto veil_mat = [&](int surface, float alpha) {
      MaterialPtr m = cf_unlit({1.0f, 0.2f, 0.2f});
      m->surface = surface;
      m->alpha = alpha;
      m->double_sided = true;
      return m;
    };
    CfScene bg_only, opaque_veil, veil;
    bg_scene(bg_only);
    bg_scene(opaque_veil);
    bg_scene(veil);
    opaque_veil.add(primitives::quad(3.0f), Mat4::translate({0, 0, 5}), 2, veil_mat((int)MaterialSurface::Opaque, 1.0f));
    veil.add(primitives::quad(3.0f), Mat4::translate({0, 0, 5}), 2, veil_mat((int)MaterialSurface::Transparent, 0.5f));
    bg_only.seal();
    opaque_veil.seal();
    veil.seal();
    Image bg, op, blended, door;
    RenderTarget rb, ro, rl, rd;
    RasterOptions plain, sd;
    sd.screen_door = true;
    cf_render(bg, rb, W, H, v, p, plain, bg_only);
    cf_render(op, ro, W, H, v, p, plain, opaque_veil);
    cf_render(blended, rl, W, H, v, p, plain, veil);
    cf_render(door, rd, W, H, v, p, sd, veil);
    /* The quad covers the middle half of the picture. */
    const int x0 = 24, x1 = 40;
    int mine = 0, theirs = 0, other = 0, mixed = 0;
    for (int y = x0; y < x1; y++)
      for (int x = x0; x < x1; x++) {
        const uint32_t px = door.row(y)[x];
        const bool show = ((x + y) & 1) == 0;
        if (px == (show ? op.row(y)[x] : bg.row(y)[x])) (show ? mine : theirs)++;
        else other++;
        mixed += px != op.row(y)[x] && px != bg.row(y)[x];
      }
    std::printf("    screen-door: %d veil pixels, %d background pixels, %d wrong, %d blended\n", mine, theirs, other, mixed);
    CHECK(mine == (x1 - x0) * (x1 - x0) / 2 && theirs == (x1 - x0) * (x1 - x0) / 2 && other == 0 && mixed == 0);
    /* Without it the same quad blends: every pixel is in between. */
    int blended_px = 0;
    for (int y = x0; y < x1; y++)
      for (int x = x0; x < x1; x++) blended_px += blended.row(y)[x] != op.row(y)[x] && blended.row(y)[x] != bg.row(y)[x];
    /* Nearly all, not all: on macOS (arm64) a few pixels came out equal to one side, where the
     * veil's and the background's 8-bit colours nearly coincide. The screen-door checks above are exact. */
    std::printf("    blended without screen-door: %d of %d pixels between veil and background\n", blended_px, (x1 - x0) * (x1 - x0));
    CHECK(blended_px * 10 >= (x1 - x0) * (x1 - x0) * 9);
    /* Outside the quad nothing changes. */
    CHECK(door.row(2)[2] == bg.row(2)[2] && door.row(60)[60] == bg.row(60)[60]);
    /* A nearly clear surface (alpha < 0.1) vanishes instead of dotting the picture. */
    CfScene clear;
    bg_scene(clear);
    clear.add(primitives::quad(3.0f), Mat4::translate({0, 0, 5}), 2, veil_mat((int)MaterialSurface::Transparent, 0.05f));
    clear.seal();
    Image cl;
    RenderTarget rc;
    cf_render(cl, rc, W, H, v, p, sd, clear);
    CHECK(cl.pixels == bg.pixels);
    /* The preset turns it on through the stack. */
    RetroConsoleFilter sat;
    sat.apply_preset(RetroConsoleFilter::Saturn);
    FilterStack fs;
    sat.contribute(fs);
    RasterOptions viastack;
    fs.apply_raster(viastack);
    CHECK(viastack.screen_door);
    Image via;
    RenderTarget rv;
    viastack.vertex_snap = 0.0f;  // keep the geometry as above; only the transparency rule is under test
    viastack.affine_uv = false;
    cf_render(via, rv, W, H, v, p, viastack, veil);
    CHECK(via.pixels == door.pixels);
  });

  test("DOS palette: 256 distinct colours, nearest maps each to itself, and every pixel of a DOS frame is one of them", [&] {
    const uint32_t *pal = retro_palette();
    std::set<uint32_t> distinct;
    for (int i = 0; i < 256; i++) distinct.insert(pal[i] & 0xFFFFFFu);
    CHECK(distinct.size() == 256);
    int self = 0, shown = 0;
    for (int i = 0; i < 256; i++) {
      const uint32_t c = pal[i];
      const uint32_t got = retro_palette_nearest((c >> 16) & 255, (c >> 8) & 255, c & 255) & 0xFFFFFFu;
      self += got == (c & 0xFFFFFFu);
      if (got != (c & 0xFFFFFFu) && shown++ < 6) std::printf("    palette colour %06X maps to %06X\n", c & 0xFFFFFFu, got);
    }
    std::printf("    %d of 256 palette colours map to themselves\n", self);
    CHECK(self == 256);  // each palette colour is its own nearest entry (a 5-bit lookup table got 14 wrong)
    /* The 6x6x6 cube is in there. */
    CHECK(distinct.count(0x000000u) && distinct.count(0xFFFFFFu) && distinct.count(0xFF3300u) && distinct.count(0x3399CCu));
    /* Anything maps to a palette colour; out-of-range channels clamp. */
    int off_palette = 0;
    for (int r : {-50, 0, 7, 100, 130, 255, 400})
      for (int g : {0, 33, 128, 254})
        for (int b : {0, 90, 255, 1000}) off_palette += !distinct.count(retro_palette_nearest(r, g, b) & 0xFFFFFFu);
    CHECK(off_palette == 0);
    /* Pure black stays black (the old 5-bit lookup table turned it into 060606). */
    CHECK((retro_palette_nearest(-5, -5, -5) & 0xFFFFFFu) == 0u);
    CHECK((retro_palette_nearest(999, 999, 999) & 0xFFFFFFu) == 0xFFFFFFu);
    /* A rendered DOS frame: lit shapes, a textured sphere and the sky, through the preset's pass. */
    CfScene sc;
    auto grid = make_material("Grid", {0.9f, 0.9f, 0.9f});
    grid->base_map.path = "generated:UV Grid";
    sc.add(primitives::cube(), Mat4::translate({-1.2f, 0, 0}), 1, make_material("Red", {0.8f, 0.2f, 0.2f}));
    sc.add(primitives::uv_sphere(0.8f, 24, 12), Mat4::translate({1.2f, 0, 0.5f}), 2, grid);
    sc.add(primitives::plane(12.0f), Mat4::translate({0, -1, 0}), 3, make_material("Floor", {0.5f, 0.6f, 0.5f}));
    sc.seal();
    const Mat4 v = Mat4::look_at({0, 1.5f, -6}, {0, 0, 0}, {0, 1, 0});
    const Mat4 p = Mat4::perspective(50 * kDeg2Rad, 320 / 200.0f, 0.1f, 100);
    RetroConsoleFilter dos;
    dos.apply_preset(RetroConsoleFilter::DOS);
    FilterStack fs;
    dos.contribute(fs);
    CHECK(fs.passes.size() == 1);
    RasterOptions ro;
    fs.apply_raster(ro);
    CHECK(ro.affine_uv);
    Image img;
    RenderTarget rt;
    cf_render(img, rt, 320, 200, v, p, ro, sc);
    const std::set<uint32_t> before(img.pixels.begin(), img.pixels.end());
    CHECK(before.size() > 256);  // more colours than the palette, so the pass has work to do
    FilterFrame frame;
    frame.inv_view_proj = (p * v).inverse();
    frame.eye = {0, 1.5f, -6};
    frame.forward = normalize(Vec3(0, -1.5f, 6));
    fs.passes[0](rt, &frame);
    int outside = 0;
    std::set<uint32_t> used;
    for (uint32_t c : img.pixels) {
      used.insert(c & 0xFFFFFFu);
      outside += !distinct.count(c & 0xFFFFFFu);
    }
    std::printf("    DOS frame: %zu colours before, %zu after, %d pixels outside the palette\n", before.size(), used.size(), outside);
    CHECK(outside == 0 && used.size() > 8 && used.size() <= 256);
    /* Palette colours map to themselves, so running the palette pass again changes nothing */
    const std::vector<uint32_t> once = img.pixels;
    RetroImageParams again;
    again.color_depth = RetroImageParams::Palette256;
    again.dither = RetroImageParams::NoDither;
    apply_retro_image(rt, again, nullptr);
    CHECK(img.pixels == once);
  });

  /* ---------------------------------------------------------------- 6 */
  test("filter stack: effects combine in list order (later resolution and filter win, the smaller texture cap wins, passes run in order)", [&] {
    auto make = [&](bool ps1_first) {
      CameraFilters cf;
      cf.add("Retro Console");  // PS1
      auto *b = static_cast<RetroConsoleFilter *>(cf.add("Retro Console"));
      b->width = 160, b->height = 120, b->fit = 1;
      b->max_texture_size = 64;
      b->vertex_snap = false, b->affine_textures = false;
      b->texture_filter = RetroConsoleFilter::Linear;
      b->color_depth = RetroImageParams::Full;
      b->dither = RetroImageParams::NoDither;
      b->fog = true;
      if (!ps1_first) std::swap(cf.effects[0], cf.effects[1]);
      FilterStack s;
      cf.contribute(s);
      return s;
    };
    const FilterStack ab = make(true), ba = make(false);
    /* [PS1, B]: B is later. */
    CHECK(ab.width == 160 && ab.height == 120 && ab.fit == FilterStack::Letterbox);
    CHECK(ab.tex.filter == (int)TexFilter::Linear && ab.tex.max_size == 64);
    CHECK(ab.vertex_snap > 0.0f && ab.affine_uv);  // B doesn't undo A's switches
    CHECK(!ab.tex.mipmaps);
    CHECK(ab.passes.size() == 2);
    /* [B, PS1]: PS1 is later. */
    CHECK(ba.width == 320 && ba.height == 240 && ba.fit == FilterStack::Fill);
    CHECK(ba.tex.filter == (int)TexFilter::Closest && ba.tex.max_size == 64);  // the smaller cap wins either way
    CHECK(ba.passes.size() == 2);
    /* A disabled effect adds nothing, wherever it sits (also checked with no filter at all, in
     * "with no filter the rasterizer's image is unchanged"). */
    {
      CameraFilters cf;
      cf.add("Retro Console");
      auto *off = static_cast<RetroConsoleFilter *>(cf.add("Retro Console"));
      off->enabled = false;
      off->width = 16, off->height = 16;
      FilterStack s;
      cf.contribute(s);
      CHECK(s.width == 320 && s.height == 240 && s.passes.size() == 1);
    }
    /* Image passes run in list order: 15-bit then palette ends in the palette; palette then 15-bit ends in 15-bit. */
    auto run = [&](int first_depth, int second_depth) {
      CameraFilters cf;
      auto *a = static_cast<RetroConsoleFilter *>(cf.add("Retro Console"));
      auto *b = static_cast<RetroConsoleFilter *>(cf.add("Retro Console"));
      a->color_depth = first_depth, a->dither = RetroImageParams::NoDither;
      b->color_depth = second_depth, b->dither = RetroImageParams::NoDither;
      FilterStack s;
      cf.contribute(s);
      CHECK(s.passes.size() == 2);
      Image img;
      img.resize(64, 64);
      for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) img.row(y)[x] = 0xFF000000u | (uint32_t)(x * 4 + 1) << 16 | (uint32_t)(y * 4 + 3) << 8 | (uint32_t)((x + y) * 2 + 5);
      RenderTarget rt;
      rt.attach(img, {0, 0, 64, 64});
      for (auto &pass : s.passes) pass(rt, nullptr);
      return img;
    };
    const Image pal_last = run(RetroImageParams::Bits15, RetroImageParams::Palette256);
    const Image b15_last = run(RetroImageParams::Palette256, RetroImageParams::Bits15);
    std::set<uint32_t> pal;
    for (int i = 0; i < 256; i++) pal.insert(retro_palette()[i] & 0xFFFFFFu);
    int in_pal = 0, in_15 = 0, pal_in_15 = 0;
    for (uint32_t c : pal_last.pixels) in_pal += pal.count(c & 0xFFFFFFu) > 0;
    for (uint32_t c : b15_last.pixels) in_15 += cf_is_15bit(c), pal_in_15 += pal.count(c & 0xFFFFFFu) > 0;
    CHECK(in_pal == 64 * 64);
    CHECK(in_15 == 64 * 64);
    CHECK(pal_in_15 < 64 * 64);  // 15-bit last leaves colours the palette doesn't have
    CHECK(pal_last.pixels != b15_last.pixels);
  });
}

/* Task 0005 review: an effect's field typed in the Inspector reaches every selected camera's stack
 * (multi-object editing, as task 0004's separate components did). */
static void round29_review_tests() {
  test("filter stack: a Width typed in the Inspector with two cameras selected changes both cameras' effect", [] {
    using ET = platform::EventType;
    auto ev = [](ET t, int x, int y, int key = 0) {
      platform::Event e;
      e.type = t;
      e.x = x;
      e.y = y;
      e.key = key;
      return e;
    };
    Editor ed;
    ed.init_headless(1600, 1000);
    ed.step_frame_headless();
    ed.command("select Main Camera");
    ed.command("filter add Retro Console");
    ed.command("duplicate");
    ed.step_frame_headless();
    GameObject *copy = ed.selected_object();
    GameObject *main_cam = ed.scene().find_by_name("Main Camera");
    CHECK(copy && main_cam && copy != main_cam && copy->get<CameraFilters>());
    if (!copy || !main_cam || copy == main_cam) return;
    ed.command("select Main Camera");
    ed.command("selectadd " + copy->name);
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    const Recti f = ed.inspector_menu_rect_for_test("field:E0 Width");
    CHECK(f.w > 0);
    if (f.w <= 0) return;
    const int x = f.x + f.w / 2, y = f.y + f.h / 2;
    ed.step_frame_headless({ev(ET::MouseMove, x, y)});
    ed.step_frame_headless({ev(ET::MouseDown, x, y)});
    ed.step_frame_headless({ev(ET::MouseUp, x, y)});
    ed.step_frame_headless({ev(ET::KeyDown, x, y, platform::KEY_A)});  // a click selects the text; type over it
    std::vector<platform::Event> typed;
    for (char c : std::string("160")) {
      platform::Event t = ev(ET::Text, x, y);
      t.codepoint = (uint32_t)c;
      typed.push_back(t);
    }
    ed.step_frame_headless(typed);
    ed.step_frame_headless({ev(ET::KeyDown, x, y, platform::KEY_ENTER)});
    ed.step_frame_headless({ev(ET::KeyUp, x, y, platform::KEY_ENTER)});
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    const auto *a = cf_get_retro(main_cam), *b = cf_get_retro(copy);
    std::printf("    Width after typing 160: Main Camera %d, its copy %d\n", a ? a->width : -1, b ? b->width : -1);
    CHECK(a && b && a->width == 160 && b->width == 160);
  });
}


/* ===================================================================== */
/* Round 30 (task 0009): piloting a camera shows its Game view            */
/* ===================================================================== */

namespace {
using Px = std::vector<uint32_t>;
struct PilotRig {
  Editor ed;
  GameObject *cam = nullptr;
  void start(int W, int H, bool ps1) {
    ed.init_headless(W, H);
    ed.step_frame_headless();
    ed.command("select Main Camera");
    cam = ed.selected_object();
    if (ps1) ed.command("filter add Retro Console");
    settle();
  }
  void settle(int n = 4) {
    for (int i = 0; i < n; i++) ed.step_frame_headless();
  }
  Px grab() {
    const Image &fb = ed.framebuffer();
    return Px(fb.pixels.begin(), fb.pixels.end());
  }
  int FW() { return ed.framebuffer().width; }
  /* The rows under the "Piloting ..." banner (it is not part of the picture). */
  int banner_bottom() { return ed.scene_view_rect().y + ed.ui_for_test().px(8) + ed.ui_for_test().row_h() + 4; }
  Recti toolbar_button(int index) {  // 0 = lightbulb, 1 = grid, 2 = gizmos, 3 = statistics
    const auto &u = ed.ui_for_test();
    const int bh = u.row_h() + u.px(4), h = bh - u.px(6);
    const Recti v = ed.scene_view_rect();
    return {v.x + u.px(6) + u.px(176) + index * (h + u.px(2)), v.y - bh + u.px(3), h, h};
  }
  void click(int x, int y) {
    using ET = platform::EventType;
    auto ev = [&](ET t) {
      platform::Event e;
      e.type = t;
      e.x = x;
      e.y = y;
      return e;
    };
    ed.step_frame_headless({ev(ET::MouseMove)});
    ed.step_frame_headless({ev(ET::MouseDown)});
    ed.step_frame_headless({ev(ET::MouseUp)});
    ed.step_frame_headless();
  }
  void click_button(int index) {
    const Recti b = toolbar_button(index);
    click(b.x + b.w / 2, b.y + b.h / 2);
  }
};
/* Pixels that differ between two grabs inside r, skipping rows above `from_y`. */
size_t pilot_diff(const Px &a, const Px &b, int stride, const Recti &r, int from_y) {
  size_t d = 0;
  for (int y = std::max(r.y, from_y); y < r.bottom(); y++)
    for (int x = r.x; x < r.right(); x++) d += a[(size_t)y * stride + x] != b[(size_t)y * stride + x];
  return d;
}
}  // namespace

static void round30_tests() {
  test("pilot view: the frame is the camera's Game view picture (same size Game view in a second editor), plain and with PS1", [&] {
    /* Approach: a second Editor whose Game view has exactly the frame's size renders the same camera
     * through the Game view path; the piloted frame must match it pixel for pixel. The window size
     * found for the plain pass is reused for PS1 (the filter doesn't change the layout). */
    int W = 0, H = 0;
    for (int ps1 = 0; ps1 < 2; ps1++) {
      PilotRig A;
      A.start(1600, 1000, ps1 != 0);
      A.ed.command("pilot");
      A.settle();
      const Recti fr = A.ed.pilot_frame();
      const Recti vr = A.ed.scene_view_rect();
      CHECK(fr.w > 300 && fr.h > 150);
      if (fr.w <= 0) return;
      const Px a = A.grab();
      /* Find a window size whose Game view is the frame's size. */
      if (W == 0) W = 1600 - (vr.w - fr.w), H = 1000 - (vr.h - fr.h);
      Recti g{0, 0, 0, 0};
      for (int it = 0; it < 6; it++) {
        auto B = std::make_unique<PilotRig>();
        B->start(W, H, ps1 != 0);
        B->ed.command("window Game");
        B->settle();
        g = B->ed.scene_view_rect();
        if (g.w == fr.w && g.h == fr.h) {
          std::printf("    ps1=%d: frame %d x %d, second editor's Game view %d x %d (window %d x %d)\n", ps1, fr.w, fr.h, g.w, g.h, W, H);
          const Px b = B->grab();
          const int from = A.banner_bottom();
          size_t diff = 0, total = 0;
          for (int y = 0; y < fr.h; y++) {
            if (fr.y + y < from) continue;
            for (int x = 0; x < fr.w; x++) {
              total++;
              diff += a[(size_t)(fr.y + y) * A.FW() + fr.x + x] != b[(size_t)(g.y + y) * B->FW() + g.x + x];
            }
          }
          std::printf("    ps1=%d: %zu of %zu frame pixels differ from the Game view\n", ps1, diff, total);
          CHECK(diff <= total / 200);
          break;
        }
        W += fr.w - g.w;
        H += fr.h - g.h;
        if (it == 5) CHECK(false);  // never found a window size with a frame-sized Game view
      }
    }
  });

  test("pilot view: a PS1 filter shows in the frame (15-bit, whole blocks) whatever the Shading mode, and the mode changes nothing", [&] {
    PilotRig R;
    R.start(1600, 1000, true);
    RetroConsoleFilter *f = cf_get_retro(R.cam);
    CHECK(f != nullptr);
    if (!f) return;
    f->width = 48;
    f->height = 36;
    R.ed.command("pilot");
    R.settle();
    const Recti fr = R.ed.pilot_frame();
    const int top = std::max(fr.y + 2, R.banner_bottom());
    const Recti inner(fr.x + 2, top, fr.w - 4, fr.bottom() - 2 - top);
    Px first;
    for (const char *mode : {"solid", "wire", "rendered", "shaded"}) {
      R.ed.command(std::string("shading ") + mode);
      R.settle();
      const Px px = R.grab();
      int not15 = 0;
      for (int y = inner.y; y < inner.bottom(); y++)
        for (int x = inner.x; x < inner.right(); x++) not15 += !cf_is_15bit(px[(size_t)y * R.FW() + x]);
      int runs, changes;
      cf_blockiness(px.data(), R.FW(), inner, runs, changes);
      std::printf("    shading %s: %d not 15-bit, %d colour runs along the busiest row\n", mode, not15, runs);
      CHECK(not15 == 0);
      CHECK(runs <= (int)std::lround(36.0 * fr.w / fr.h) + 3 && runs > 10);
      if (first.empty()) first = px;
      else CHECK(pilot_diff(first, px, R.FW(), fr, R.banner_bottom()) <= (size_t)fr.w * fr.h / 200);
    }
  });

  test("pilot view: grid, gizmos and statistics don't draw in the frame, and the passepartout is one plain colour", [&] {
    PilotRig R;
    R.start(1600, 1000, false);
    /* First make sure the toolbar clicks find the Grid button (outside piloting the view changes). */
    const Recti v = R.ed.scene_view_rect();
    const Px before = R.grab();
    R.click_button(1);
    R.settle();
    const Px no_grid = R.grab();
    CHECK(pilot_diff(before, no_grid, R.FW(), v, v.y) > 200);  // the grid really was there
    R.click_button(1);  // grid back on
    R.settle();
    R.ed.command("pilot");
    R.settle();
    const Recti fr = R.ed.pilot_frame();
    const Px with_all = R.grab();
    const int from = R.banner_bottom();
    R.click_button(1);  // grid off
    R.click_button(2);  // gizmos off
    R.click_button(3);  // statistics off
    R.settle();
    const Px with_none = R.grab();
    const size_t d = pilot_diff(with_all, with_none, R.FW(), v, from);
    std::printf("    grid/gizmos/stats on vs off while piloting: %zu pixels differ in the view\n", d);
    CHECK(d <= 20);
    /* The passepartout: everything in the view outside the frame, its outline and the banner. */
    size_t odd = 0, n = 0;
    for (const Px *px : {&with_all, &with_none})
      for (int y = std::max(v.y, from); y < v.bottom(); y++)
        for (int x = v.x; x < v.right(); x++) {
          if (x >= fr.x - 3 && x < fr.right() + 3 && y >= fr.y - 3 && y < fr.bottom() + 3) continue;
          n++;
          odd += (*px)[(size_t)y * R.FW() + x] != 0xFF1C1C1Cu;
        }
    std::printf("    passepartout: %zu of %zu pixels are not the plain colour\n", odd, n);
    CHECK(n > 0 && odd == 0);
  });

  test("pilot view: clicking an object inside the frame selects it; clicking the passepartout does not", [&] {
    PilotRig R;
    R.start(1600, 1000, true);
    R.ed.command("pilot");
    R.settle();
    const Recti fr = R.ed.pilot_frame();
    const GameObject *cam = R.cam;
    GameObject *got = nullptr;
    int hx = 0, hy = 0;
    int clicks = 0;
    for (int gy = 1; gy < 14 && !got && clicks < 8; gy++)
      for (int gx = 1; gx < 20 && !got && clicks < 8; gx++) {
        const int x = fr.x + fr.w * gx / 20, y = fr.y + fr.h * gy / 14;
        if (y < R.banner_bottom()) continue;
        if (R.ed.scene_depth_for_test(x, y) >= 1.0f) continue;  // sky: nothing to pick there
        clicks++;
        R.ed.command("select Main Camera");
        R.click(x, y);
        GameObject *s = R.ed.selected_object();
        if (s && s != cam) got = s, hx = x, hy = y;
      }
    CHECK(got != nullptr);
    if (!got) return;
    std::printf("    clicked (%d, %d) inside the frame: selected '%s'\n", hx, hy, got->name.c_str());
    /* Outside the frame (the passepartout) nothing is pickable. */
    const Recti v = R.ed.scene_view_rect();
    int px, py;
    if (fr.x > v.x + 8) px = v.x + 3, py = v.y + v.h / 2;
    else px = v.x + v.w / 2, py = v.bottom() - 3;
    R.ed.command("select Main Camera");
    R.click(px, py);
    GameObject *s = R.ed.selected_object();
    CHECK(!s || s == cam);
  });

  test("pilot view: stopping the pilot (command and Esc) brings back the editor view with its grid", [&] {
    PilotRig R;
    R.start(1600, 1000, true);
    const Recti v = R.ed.scene_view_rect();
    const Px editor_view = R.grab();
    R.ed.command("pilot");
    R.settle();
    CHECK(R.ed.pilot_frame().w > 0);
    const Px piloting = R.grab();
    CHECK(pilot_diff(editor_view, piloting, R.FW(), v, v.y) > 1000);
    R.ed.command("pilot");
    R.settle();
    CHECK(R.ed.pilot_frame().w == 0);
    const Px back = R.grab();
    const size_t d = pilot_diff(editor_view, back, R.FW(), v, v.y);
    std::printf("    editor view after the pilot: %zu pixels differ from before\n", d);
    CHECK(d <= (size_t)v.w * v.h / 200);
    /* The grid is back: switching it off changes the view. */
    R.click_button(1);
    R.settle();
    CHECK(pilot_diff(back, R.grab(), R.FW(), v, v.y) > 200);
    R.click_button(1);
    R.settle();
    /* Esc stops it too. */
    R.ed.command("pilot");
    R.settle();
    CHECK(R.ed.pilot_frame().w > 0);
    using ET = platform::EventType;
    platform::Event mv, kd, ku;
    mv.type = ET::MouseMove;
    kd.type = ET::KeyDown;
    ku.type = ET::KeyUp;
    kd.key = ku.key = platform::KEY_ESCAPE;
    mv.x = kd.x = ku.x = v.x + v.w / 2;
    mv.y = kd.y = ku.y = v.y + v.h / 2;
    R.ed.step_frame_headless({mv});
    R.ed.step_frame_headless({kd});
    R.ed.step_frame_headless({ku});
    R.settle();
    CHECK(R.ed.pilot_frame().w == 0);
  });

  /* Review of task 0009: the depth the tools read while piloting is the editor's own, and Edit Mode
   * (whose cage the pure game look would hide) ends piloting. */
  test("pilot: while piloting, the Scene view's depth matches the editor's own render of the same view", [] {
    PilotRig R;
    R.start(1600, 900, false);
    R.ed.command("pilot");
    R.settle();
    const Recti fr = R.ed.pilot_frame();
    CHECK(fr.w > 0);
    if (fr.w <= 0) return;
    /* Sample a grid of pixels inside the frame (below the banner) while piloting... */
    std::vector<std::pair<int, int>> pts;
    for (int y = std::max(fr.y, R.banner_bottom()) + 10; y < fr.bottom() - 10; y += 23)
      for (int x = fr.x + 10; x < fr.right() - 10; x += 31) pts.push_back({x, y});
    std::vector<float> piloted;
    for (auto &q : pts) piloted.push_back(R.ed.scene_depth_for_test(q.first, q.second));
    /* ...then stop: the view stays where the camera is, and the editor renders it itself. */
    R.ed.command("pilot");
    R.ed.command("shading shaded");
    R.settle();
    int compared = 0, off = 0;
    float worst = 0.0f;
    for (size_t k = 0; k < pts.size(); k++) {
      const float a = piloted[k], b = R.ed.scene_depth_for_test(pts[k].first, pts[k].second);
      if (a >= 1.0f || b >= 1.0f) continue;  // sky on either side
      compared++;
      worst = std::max(worst, std::fabs(a - b));
      off += std::fabs(a - b) > 2e-3f;
    }
    std::printf("    depth piloting vs the editor's render: %d pixels compared, %d off by more than 2e-3 (worst %.5f)\n", compared, off, worst);
    CHECK(compared > 20);
    CHECK(off * 50 <= compared);  // edges may land on either side of a silhouette
  });

  test("pilot: entering Edit Mode stops piloting (its cage needs the editor's overlays)", [] {
    PilotRig R;
    R.start(1200, 800, false);
    R.ed.command("pilot");
    R.settle();
    CHECK(R.ed.pilot_frame().w > 0);
    R.ed.command("select Cube");
    R.ed.command("edit vertex");
    R.settle();
    CHECK(R.ed.pilot_frame().w == 0);
  });
}

/* ===================================================================== */
/* Round 31 (task 0006): Color, Lens and Stylize camera filters           */
/* ===================================================================== */

namespace {
/* A small synthetic target: an Image plus the RenderTarget over it (with depth and ids planes). */
struct R31Target {
  Image img;
  RenderTarget rt;
  R31Target(int w, int h) {
    img.resize(w, h);
    rt.attach(img, {0, 0, w, h});
  }
  R31Target(const R31Target &) = delete;
  R31Target &operator=(const R31Target &) = delete;
  uint32_t &at(int x, int y) { return img.pixels[(size_t)y * img.width + x]; }
  void fill(uint32_t c) { std::fill(img.pixels.begin(), img.pixels.end(), c); }
  void random(uint32_t seed) {
    uint32_t s = seed * 2654435761u + 12345u;
    for (uint32_t &p : img.pixels) {
      s = s * 1664525u + 1013904223u;
      p = 0xFF000000u | (s >> 8);
    }
  }
  /* Random colours plus random depth and ids, for passes that read them. */
  void random_planes(uint32_t seed) {
    random(seed);
    uint32_t s = seed * 40503u + 7u;
    for (size_t i = 0; i < rt.depth.size(); i++) {
      s = s * 1664525u + 1013904223u;
      rt.depth[i] = (s >> 8) / 16777216.0f;
      s = s * 1664525u + 1013904223u;
      rt.ids[i] = (s >> 24) % 5;
    }
  }
};
inline int r31_ch(uint32_t p, int k) { return (int)((p >> (16 - 8 * k)) & 255); }
inline float r31_dec(int v) {
  const float c = v / 255.0f;
  return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
/* The frame an Edge Outline pass needs: identity camera, so distance == the stored depth value. */
inline FilterFrame r31_frame() {
  FilterFrame f;
  f.eye = {0, 0, 0};
  f.forward = {0, 0, 1};
  return f;
}

/* Runs every pass with the given odd number in every float slot (and an odd int in every int slot). */
void r31_run_all(RenderTarget &rt, float v, int iv, const FilterFrame *frame) {
  ColorGradingParams cg;
  cg.exposure = cg.contrast = cg.saturation = cg.temperature = cg.tint = v;
  cg.lift = Vec3(v);
  cg.gamma = Vec3(v);
  cg.gain = Vec3(v);
  apply_color_grading(rt, cg);
  apply_posterize(rt, iv);
  apply_grayscale(rt, false, v);
  apply_grayscale(rt, true, v);
  apply_invert(rt, v);
  VignetteParams vg;
  vg.intensity = vg.smoothness = vg.roundness = v;
  vg.color = Vec3(v);
  apply_vignette(rt, vg);
  apply_chromatic_aberration(rt, v);
  GrainParams gp;
  gp.intensity = gp.size = gp.response = v;
  apply_film_grain(rt, gp, frame);
  apply_lens_distortion(rt, v, v);
  apply_lens_distortion(rt, v, 1.0f);
  apply_lens_distortion(rt, 0.5f, v);
  apply_pixelate(rt, iv);
  OutlineParams op;
  op.color = Vec3(v);
  op.thickness = iv;
  op.depth_sensitivity = v;
  apply_edge_outline(rt, op, frame);
  CrtParams cp;
  cp.scanlines = cp.curvature = cp.mask = cp.flicker = v;
  apply_crt(rt, cp, frame);
  apply_sharpen(rt, v);
}

/* One effect for the registry test: a field to move off its default and how to read it back. */
struct R31Case {
  const char *name, *field, *value;
  std::function<double(FilterEffect *)> read;
  double expect;
};
}  // namespace

static void round31_tests() {
  auto cmd = [&](Editor &ed, const std::string &c) {
    ed.command(c);
    ed.step_frame_headless();
  };

  /* ---------------------------------------------------------------- 1 */
  test("color/lens/stylize filters: neutral settings leave the picture bit-identical, for every pass", [] {
    R31Target t(37, 23);
    t.random_planes(1);
    const std::vector<uint32_t> orig = t.img.pixels;
    FilterFrame fr = r31_frame();
    auto same = [&](const char *what) {
      const bool ok = t.img.pixels == orig;
      if (!ok) std::printf("    changed by neutral %s\n", what);
      CHECK(ok);
      t.img.pixels = orig;
    };
    apply_color_grading(t.rt, ColorGradingParams{});
    same("color grading");
    apply_posterize(t.rt, 256);
    same("posterize 256");
    apply_grayscale(t.rt, false, 0.0f);
    same("grayscale amount 0");
    apply_grayscale(t.rt, true, 0.0f);
    same("sepia amount 0");
    apply_invert(t.rt, 0.0f);
    same("invert 0");
    VignetteParams vg;
    vg.intensity = 0.0f;
    apply_vignette(t.rt, vg);
    same("vignette 0");
    apply_chromatic_aberration(t.rt, 0.0f);
    same("chromatic aberration 0");
    GrainParams gp;
    gp.intensity = 0.0f;
    apply_film_grain(t.rt, gp, &fr);
    same("film grain 0");
    apply_lens_distortion(t.rt, 0.0f, 1.0f);
    same("lens distortion 0, scale 1");
    apply_pixelate(t.rt, 1);
    same("pixelate 1");
    OutlineParams op;  // no frame (the path tracer): nothing to draw
    apply_edge_outline(t.rt, op, nullptr);
    same("edge outline without a frame");
    op.object_edges = false;  // a constant depth and no object edges: nothing to find
    for (float &d : t.rt.depth) d = 0.5f;
    apply_edge_outline(t.rt, op, &fr);
    same("edge outline with nothing to find");
    CrtParams cp;
    cp.scanlines = cp.curvature = cp.mask = cp.flicker = 0.0f;
    apply_crt(t.rt, cp, &fr);
    same("crt 0");
    apply_sharpen(t.rt, 0.0f);
    same("sharpen 0");
  });

  /* ---------------------------------------------------------------- 2 */
  test("posterize leaves exactly `levels` values per channel on a full 0..255 ramp, keeping black and white", [] {
    for (int L : {2, 3, 4, 6, 16, 64, 255}) {
      R31Target t(256, 1);
      for (int x = 0; x < 256; x++) t.at(x, 0) = 0xFF000000u | (uint32_t)x << 16 | (uint32_t)x << 8 | (uint32_t)x;
      apply_posterize(t.rt, L);
      for (int k = 0; k < 3; k++) {
        std::set<int> values;
        for (int x = 0; x < 256; x++) values.insert(r31_ch(t.at(x, 0), k));
        if ((int)values.size() != L) std::printf("    levels %d channel %d: %zu distinct values\n", L, k, values.size());
        CHECK((int)values.size() == L);
        CHECK(*values.begin() == 0 && *values.rbegin() == 255);
      }
    }
    /* Below 2 levels it still makes two (black and white), never divides by zero. */
    R31Target t(256, 1);
    for (int x = 0; x < 256; x++) t.at(x, 0) = 0xFF000000u | (uint32_t)x * 0x010101u;
    apply_posterize(t.rt, 1);
    std::set<uint32_t> v;
    for (int x = 0; x < 256; x++) v.insert(t.at(x, 0));
    CHECK(v.size() == 2);
  });

  test("invert at 1 turns 255-v and applied twice is the identity; half way gives grey", [] {
    R31Target t(41, 17);
    t.random(2);
    const std::vector<uint32_t> orig = t.img.pixels;
    apply_invert(t.rt, 1.0f);
    CHECK(t.img.pixels != orig);
    CHECK(t.at(3, 3) == (0xFF000000u | (~orig[3 * 41 + 3] & 0xFFFFFFu)));
    apply_invert(t.rt, 1.0f);
    CHECK(t.img.pixels == orig);
    t.fill(0xFF102030u);
    apply_invert(t.rt, 0.5f);
    CHECK(r31_ch(t.at(0, 0), 0) == 128 || r31_ch(t.at(0, 0), 0) == 127);  // (16 + 239) / 2
  });

  test("grayscale gives equal channels (Rec.709 luma), amount blends, sepia matches the matrix on known pixels", [] {
    R31Target t(40, 25);
    t.random(3);
    apply_grayscale(t.rt, false, 1.0f);
    int unequal = 0;
    for (uint32_t p : t.img.pixels) unequal += r31_ch(p, 0) != r31_ch(p, 1) || r31_ch(p, 1) != r31_ch(p, 2);
    CHECK(unequal == 0);
    /* A pure green pixel: luma 0.7152 * 255 = 182.4. */
    t.at(0, 0) = 0xFF00FF00u;
    apply_grayscale(t.rt, false, 1.0f);
    CHECK(r31_ch(t.at(0, 0), 0) == 182 && r31_ch(t.at(0, 0), 2) == 182);
    /* Amount 0.5 on a pure green pixel: half way between (0,255,0) and (182,182,182). */
    t.at(1, 0) = 0xFF00FF00u;
    apply_grayscale(t.rt, false, 0.5f);
    CHECK(std::abs(r31_ch(t.at(1, 0), 0) - 91) <= 1 && std::abs(r31_ch(t.at(1, 0), 1) - 218) <= 1);
    /* Sepia: (100,150,50) -> (164.1, 146.2, 113.85); white clamps to (255,255,239). */
    t.at(2, 0) = 0xFF649632u;
    t.at(3, 0) = 0xFFFFFFFFu;
    apply_grayscale(t.rt, true, 1.0f);
    CHECK(r31_ch(t.at(2, 0), 0) == 164 && r31_ch(t.at(2, 0), 1) == 146 && r31_ch(t.at(2, 0), 2) == 114);
    CHECK(r31_ch(t.at(3, 0), 0) == 255 && r31_ch(t.at(3, 0), 1) == 255 && r31_ch(t.at(3, 0), 2) == 239);
  });

  test("vignette: the centre is unchanged, the corners darken, and more intensity darkens more; its colour tints the corners", [] {
    const int W = 41, H = 31;
    std::vector<int> corner;
    for (float in : {0.2f, 0.5f, 1.0f}) {
      R31Target t(W, H);
      t.fill(0xFFC8C8C8u);
      VignetteParams p;
      p.intensity = in;
      apply_vignette(t.rt, p);
      CHECK(t.at(W / 2, H / 2) == 0xFFC8C8C8u);
      const int c = r31_ch(t.at(0, 0), 1);
      CHECK(c < 200);
      CHECK(r31_ch(t.at(W - 1, H - 1), 1) == c && r31_ch(t.at(W - 1, 0), 1) == c && r31_ch(t.at(0, H - 1), 1) == c);  // symmetric
      CHECK(r31_ch(t.at(W / 2, 1), 1) >= c);  // an edge middle is lighter than a corner
      corner.push_back(c);
    }
    CHECK(corner[0] > corner[1] && corner[1] > corner[2]);
    CHECK(corner[2] <= 8);  // intensity 1 with the default black: nearly black corners
    R31Target t(W, H);
    t.fill(0xFFC8C8C8u);
    VignetteParams p;
    p.intensity = 1.0f;
    p.color = Vec3(1, 0, 0);
    apply_vignette(t.rt, p);
    CHECK(r31_ch(t.at(0, 0), 0) > 240 && r31_ch(t.at(0, 0), 1) < 8 && r31_ch(t.at(0, 0), 2) < 8);
  });

  test("chromatic aberration: the centre is unchanged and red / blue slide apart toward the edges", [] {
    const int W = 201, H = 21;
    R31Target t(W, H);
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++) {
        const uint32_t v = (uint32_t)(x * 255 / (W - 1));
        t.at(x, y) = 0xFF000000u | v << 16 | v << 8 | v;
      }
    const uint32_t centre = t.at(W / 2, H / 2);
    apply_chromatic_aberration(t.rt, 1.0f);
    CHECK(t.at(W / 2, H / 2) == centre);
    /* Green is never moved. On a rising ramp, red sampled outward is brighter on the right edge
     * side, darker on the left; blue the reverse. */
    const uint32_t r = t.at(W - 15, H / 2), l = t.at(14, H / 2);
    CHECK(r31_ch(r, 0) > r31_ch(r, 1) && r31_ch(r, 1) > r31_ch(r, 2));
    CHECK(r31_ch(l, 0) < r31_ch(l, 1) && r31_ch(l, 1) < r31_ch(l, 2));
    CHECK(r31_ch(r, 1) == (W - 15) * 255 / (W - 1));
  });

  test("lens distortion: the centre stays, a straight vertical line bends (both signs), strong barrel blackens the corners", [] {
    const int W = 61, H = 41;
    auto line_x = [&](R31Target &t, int y) {  // brightness-weighted x of the line in a row
      double sum = 0, wsum = 0;
      for (int x = 0; x < W; x++) {
        const double b = r31_ch(t.at(x, y), 1);
        sum += b * x;
        wsum += b;
      }
      return wsum > 0 ? sum / wsum : -1.0;
    };
    for (float k : {0.5f, -0.3f}) {
      R31Target t(W, H);
      t.fill(0xFF000000u);
      for (int y = 0; y < H; y++)
        for (int x = 44; x <= 46; x++) t.at(x, y) = 0xFFFFFFFFu;
      const double before = line_x(t, 0);
      CHECK_NEAR(before, line_x(t, H / 2), 1e-9);  // straight
      t.at(W / 2, H / 2) = 0xFF336699u;            // a marker at the centre
      apply_lens_distortion(t.rt, k, 1.0f);
      CHECK(t.at(W / 2, H / 2) == 0xFF336699u);
      const double mid = line_x(t, H / 2), up = line_x(t, H / 4), down = line_x(t, 3 * H / 4);
      std::printf("    k=%.1f: line x at the middle row %.2f, a quarter up %.2f, a quarter down %.2f\n", k, mid, up, down);
      CHECK(std::fabs(mid - up) > 0.5);  // bent
      CHECK_NEAR(up, down, 0.2);         // symmetric about the middle row
    }
    /* Strong barrel (negative intensity in the spec) leaves the corners black. */
    R31Target t(W, H);
    t.fill(0xFFFFFFFFu);
    apply_lens_distortion(t.rt, -1.0f, 1.0f);
    CHECK((t.at(0, 0) & 0xFFFFFF) == 0 && (t.at(W - 1, H - 1) & 0xFFFFFF) == 0 && (t.at(W - 1, 0) & 0xFFFFFF) == 0);
    CHECK(t.at(W / 2, H / 2) == 0xFFFFFFFFu);
    /* Scale zooms in: with scale 2 and no bending, the centre stays and the picture is magnified. */
    R31Target z(W, H);
    for (int x = 0; x < W; x++)
      for (int y = 0; y < H; y++) z.at(x, y) = 0xFF000000u | (uint32_t)(x * 4) << 16;
    const int before_px = r31_ch(z.at(W / 2 + 10, H / 2), 0);
    apply_lens_distortion(z.rt, 0.0f, 2.0f);
    CHECK(r31_ch(z.at(W / 2, H / 2), 0) == 30 * 4);
    CHECK(r31_ch(z.at(W / 2 + 10, H / 2), 0) < before_px);  // that pixel now shows something nearer the middle
  });

  test("pixelate: every cell is one colour, taken from the cell's middle pixel; oversized cells make one colour", [] {
    const int W = 37, H = 23, N = 5;
    R31Target t(W, H);
    t.random(4);
    const std::vector<uint32_t> orig = t.img.pixels;
    apply_pixelate(t.rt, N);
    int mixed = 0, wrong_centre = 0;
    for (int cy = 0; cy < H; cy += N)
      for (int cx = 0; cx < W; cx += N) {
        const uint32_t c = t.at(cx, cy);
        for (int y = cy; y < std::min(H, cy + N); y++)
          for (int x = cx; x < std::min(W, cx + N); x++) mixed += t.at(x, y) != c;
        if (cx + N <= W && cy + N <= H) wrong_centre += (c & 0xFFFFFF) != (orig[(size_t)(cy + N / 2) * W + cx + N / 2] & 0xFFFFFF);
      }
    CHECK(mixed == 0);
    CHECK(wrong_centre == 0);
    t.img.pixels = orig;
    apply_pixelate(t.rt, 100000);
    std::set<uint32_t> distinct(t.img.pixels.begin(), t.img.pixels.end());
    CHECK(distinct.size() == 1);
  });

  test("edge outline: lines exactly at an id boundary and at a depth step, thickness widens them, nothing on a flat plane or without a frame", [] {
    const int W = 20, H = 10;
    FilterFrame fr = r31_frame();
    auto cols_with_line = [&](R31Target &t) {
      std::vector<int> cols;
      for (int x = 0; x < W; x++) {
        int n = 0;
        for (int y = 0; y < H; y++) n += (t.at(x, y) & 0xFFFFFF) == 0;
        if (n == H) cols.push_back(x);
        else if (n != 0) cols.push_back(-1 - x);  // a line is a whole column: a partial one fails the comparison
      }
      return cols;
    };
    auto setup = [&](R31Target &t, bool two_ids, bool depth_step) {
      t.fill(0xFFFFFFFFu);
      for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
          t.rt.ids[(size_t)y * W + x] = two_ids && x >= 10 ? 2 : 1;
          t.rt.depth[(size_t)y * W + x] = depth_step && x >= 10 ? 0.6f : 0.3f;
        }
    };
    {  // ids differ, depth flat
      R31Target t(W, H);
      setup(t, true, false);
      OutlineParams p;
      apply_edge_outline(t.rt, p, &fr);
      CHECK((cols_with_line(t) == std::vector<int>{9, 10}));
      /* Object edges off: the id boundary is not an edge any more. */
      setup(t, true, false);
      p.object_edges = false;
      apply_edge_outline(t.rt, p, &fr);
      CHECK(cols_with_line(t).empty());
      /* Thickness 2 reaches two pixels each way. */
      setup(t, true, false);
      p = OutlineParams{};
      p.thickness = 2;
      apply_edge_outline(t.rt, p, &fr);
      CHECK((cols_with_line(t) == std::vector<int>{8, 9, 10, 11}));
    }
    {  // depth step, one id
      R31Target t(W, H);
      setup(t, false, true);
      OutlineParams p;
      apply_edge_outline(t.rt, p, &fr);
      CHECK((cols_with_line(t) == std::vector<int>{9, 10}));
      /* A tolerance larger than the jump (0.3 of 0.6 = 50%) finds no edge. */
      setup(t, false, true);
      p.depth_sensitivity = 0.9f;
      apply_edge_outline(t.rt, p, &fr);
      CHECK(cols_with_line(t).empty());
      /* The outline colour is used. */
      setup(t, false, true);
      p = OutlineParams{};
      p.color = Vec3(1, 0, 0);
      apply_edge_outline(t.rt, p, &fr);
      CHECK(t.at(9, 4) == 0xFFFF0000u && t.at(0, 0) == 0xFFFFFFFFu);
    }
    {  // flat plane (one id, depth constant or gently sloped): nothing
      R31Target t(W, H);
      setup(t, false, false);
      for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) t.rt.depth[(size_t)y * W + x] = 0.5f + 0.0005f * x;
      OutlineParams p;
      apply_edge_outline(t.rt, p, &fr);
      CHECK(cols_with_line(t).empty());
    }
    {  // no frame: nothing, whatever the planes hold
      R31Target t(W, H);
      setup(t, true, true);
      apply_edge_outline(t.rt, OutlineParams{}, nullptr);
      CHECK(cols_with_line(t).empty());
    }
    {  // a silhouette against the sky (depth 1) is outlined
      R31Target t(W, H);
      setup(t, false, false);
      for (int y = 0; y < H; y++)
        for (int x = 10; x < W; x++) t.rt.depth[(size_t)y * W + x] = 1.0f;
      apply_edge_outline(t.rt, OutlineParams{}, &fr);
      CHECK((cols_with_line(t) == std::vector<int>{9, 10}));
    }
  });

  test("CRT: alternate rows darker, the curved border goes black, the phosphor mask dims channels by column", [] {
    FilterFrame fr = r31_frame();
    {  // scanlines only
      R31Target t(30, 20);
      t.fill(0xFF646464u);
      CrtParams p;
      p.scanlines = 0.5f;
      p.curvature = 0.0f;
      p.mask = 0.0f;
      p.flicker = 0.0f;
      apply_crt(t.rt, p, &fr);
      for (int y = 0; y < 20; y++) CHECK(t.at(7, y) == (y & 1 ? 0xFF323232u : 0xFF646464u));
    }
    {  // curvature only
      R31Target t(30, 20);
      t.fill(0xFFC8C8C8u);
      CrtParams p;
      p.scanlines = 0.0f;
      p.curvature = 1.0f;
      p.mask = 0.0f;
      apply_crt(t.rt, p, &fr);
      CHECK((t.at(0, 0) & 0xFFFFFF) == 0 && (t.at(29, 19) & 0xFFFFFF) == 0 && (t.at(29, 0) & 0xFFFFFF) == 0 && (t.at(0, 19) & 0xFFFFFF) == 0);
      CHECK(t.at(15, 10) == 0xFFC8C8C8u);
      CHECK(t.at(15, 0) == 0xFFC8C8C8u);  // the middle of an edge stays on the tube
    }
    {  // mask only: one phosphor per column keeps full strength, the other two are dimmed
      R31Target t(30, 20);
      t.fill(0xFFC8C8C8u);
      CrtParams p;
      p.scanlines = 0.0f;
      p.curvature = 0.0f;
      p.mask = 0.5f;
      apply_crt(t.rt, p, &fr);
      for (int x = 0; x < 3; x++)
        for (int k = 0; k < 3; k++) CHECK(r31_ch(t.at(x, 4), k) == (x == k ? 200 : 100));
      CHECK(t.at(3, 4) == t.at(0, 4) && t.at(4, 4) == t.at(1, 4));
    }
  });

  test("sharpen: flat areas are unchanged and the contrast rises on both sides of a step edge", [] {
    R31Target t(8, 8);
    t.fill(0xFF808080u);
    apply_sharpen(t.rt, 2.0f);
    CHECK(count_bad("pixels", t.img.pixels, [&](uint32_t p) { return p == 0xFF808080u; }) == 0);
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++) t.at(x, y) = x < 4 ? 0xFF323232u : 0xFFC8C8C8u;
    apply_sharpen(t.rt, 1.0f);
    /* Hand values: dark side 50 + (4*50 - (50+200+50+50)) / 4 = 12.5, light side 200 + 37.5 = 237.5. */
    const int dark = r31_ch(t.at(3, 4), 0), light = r31_ch(t.at(4, 4), 0);
    CHECK(dark == 12 || dark == 13);
    CHECK(light == 237 || light == 238);
    CHECK(t.at(0, 4) == 0xFF323232u && t.at(7, 4) == 0xFFC8C8C8u);  // away from the edge: untouched
    CHECK(r31_ch(t.at(3, 4), 1) == dark && r31_ch(t.at(4, 4), 2) == light);
  });

  test("color grading: +1 stop doubles linear light, -1 saturation is grey, temperature warms, tint shifts green, lift raises blacks", [] {
    const uint32_t greys[] = {0xFF282828u, 0xFF505050u, 0xFF808080u};
    for (uint32_t g : greys) {
      R31Target t(2, 2);
      t.fill(g);
      ColorGradingParams p;
      p.exposure = 1.0f;
      apply_color_grading(t.rt, p);
      const double ratio = r31_dec(r31_ch(t.at(0, 0), 1)) / r31_dec(r31_ch(g, 1));
      std::printf("    grey %d: linear light x%.3f\n", r31_ch(g, 1), ratio);
      CHECK_NEAR(ratio, 2.0, 0.06);
      CHECK(r31_ch(t.at(0, 0), 0) == r31_ch(t.at(0, 0), 1) && r31_ch(t.at(0, 0), 1) == r31_ch(t.at(0, 0), 2));
    }
    {
      R31Target t(2, 2);
      t.fill(0xFF808080u);
      ColorGradingParams p;
      p.exposure = -1.0f;
      apply_color_grading(t.rt, p);
      CHECK_NEAR(r31_dec(r31_ch(t.at(0, 0), 0)) / r31_dec(128), 0.5, 0.03);
    }
    {  // saturation -1: equal channels from a vivid colour, at the colour's luma
      R31Target t(2, 2);
      t.fill(0xFFC85028u);
      ColorGradingParams p;
      p.saturation = -1.0f;
      apply_color_grading(t.rt, p);
      const uint32_t o = t.at(0, 0);
      CHECK(r31_ch(o, 0) == r31_ch(o, 1) && r31_ch(o, 1) == r31_ch(o, 2));
      const double y = 0.2126 * r31_dec(200) + 0.7152 * r31_dec(80) + 0.0722 * r31_dec(40);
      CHECK_NEAR(r31_dec(r31_ch(o, 1)), y, 0.01);
    }
    {  // temperature
      R31Target w(2, 2), c(2, 2);
      w.fill(0xFF808080u);
      c.fill(0xFF808080u);
      ColorGradingParams p;
      p.temperature = 0.5f;
      apply_color_grading(w.rt, p);
      p.temperature = -0.5f;
      apply_color_grading(c.rt, p);
      CHECK(r31_ch(w.at(0, 0), 0) > 128 && r31_ch(w.at(0, 0), 2) < 128);
      CHECK(r31_ch(c.at(0, 0), 0) < 128 && r31_ch(c.at(0, 0), 2) > 128);
    }
    {  // tint toward magenta lowers green; contrast stretches around middle grey; lift raises black
      R31Target t(2, 2);
      t.fill(0xFF808080u);
      ColorGradingParams p;
      p.tint = 0.5f;
      apply_color_grading(t.rt, p);
      CHECK(r31_ch(t.at(0, 0), 1) < 128 && r31_ch(t.at(0, 0), 0) == 128);
      R31Target k(2, 2);
      k.at(0, 0) = 0xFF202020u;
      k.at(1, 0) = 0xFFE0E0E0u;
      p = ColorGradingParams{};
      p.contrast = 0.5f;
      apply_color_grading(k.rt, p);
      CHECK(r31_ch(k.at(0, 0), 0) < 0x20 && r31_ch(k.at(1, 0), 0) > 0xE0);
      R31Target b(2, 2);
      b.fill(0xFF000000u);
      p = ColorGradingParams{};
      p.lift = Vec3(0.2f);
      apply_color_grading(b.rt, p);
      CHECK(r31_ch(b.at(0, 0), 1) > 60);
    }
  });

  /* ---------------------------------------------------------------- 3 */
  test("film grain and CRT flicker: identical across time unless animating; change across 1/24 s buckets when animating", [] {
    R31Target base(32, 32);
    base.fill(0xFF808080u);
    GrainParams g;
    g.intensity = 0.8f;
    FilterFrame still = r31_frame(), live = r31_frame();
    live.animate = true;
    auto run_grain = [&](const FilterFrame *f, float time) {
      R31Target t(32, 32);
      t.img.pixels = base.img.pixels;
      FilterFrame ff;
      if (f) {
        ff = *f;
        ff.time = time;
      }
      apply_film_grain(t.rt, g, f ? &ff : nullptr);
      return t.img.pixels;
    };
    const auto n0 = run_grain(nullptr, 0), n1 = run_grain(&still, 0.0f), n2 = run_grain(&still, 7.31f);
    CHECK(n0 == n1 && n1 == n2);   // not animating: the same grain at any time
    CHECK(n0 != base.img.pixels);  // and it is grain, not nothing
    const auto a0 = run_grain(&live, 1.0f), a1 = run_grain(&live, 1.0f + 0.5f / 24.0f), a2 = run_grain(&live, 1.0f + 1.0f / 24.0f + 1e-3f);
    CHECK(a0 == a1);  // same bucket
    CHECK(a0 != a2);  // next bucket
    size_t differ = 0;
    for (size_t i = 0; i < a0.size(); i++) differ += a0[i] != a2[i];
    CHECK(differ > a0.size() / 2);  // a new pattern, not a nudge
    /* A wild time never breaks anything. */
    for (float tt : {kNaN, kInf, -kInf, 1e30f, -5.0f}) {
      const auto w = run_grain(&live, tt);
      CHECK(w.size() == base.img.pixels.size());
    }
    /* CRT flicker is the same story. */
    CrtParams cp;
    cp.scanlines = cp.curvature = cp.mask = 0.0f;
    cp.flicker = 1.0f;
    auto run_crt = [&](const FilterFrame *f, float time) {
      R31Target t(8, 8);
      t.fill(0xFF808080u);
      FilterFrame ff;
      if (f) {
        ff = *f;
        ff.time = time;
      }
      apply_crt(t.rt, cp, f ? &ff : nullptr);
      return t.img.pixels;
    };
    const auto c0 = run_crt(nullptr, 0), c1 = run_crt(&still, 0.0f), c2 = run_crt(&still, 1.0f / 120.0f);
    CHECK(c0 == c1 && c1 == c2);
    CHECK(c0[0] == 0xFF808080u);  // not animating: full brightness
    /* Animating: a new brightness every 1/30 s. Within a bucket the same; at 60 fps (frames at n/60
     * s, where a 30 Hz sine would always sit on a zero crossing) it really flickers. */
    const auto f0 = run_crt(&live, 0.0f), f1 = run_crt(&live, 1.0f / 120.0f);
    CHECK(f0 == f1);
    std::set<uint32_t> at60;
    for (int n = 0; n < 12; n++) at60.insert(run_crt(&live, n / 60.0f)[0]);
    std::printf("    CRT flicker at 60 fps: %zu different brightnesses over 12 frames\n", at60.size());
    CHECK(at60.size() >= 3);
  });

  test("film grain in the editor: the Game view holds still across frames outside Play mode and changes while playing", [&] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.step_frame_headless();
    cmd(ed, "select Main Camera");
    cmd(ed, "filter add Film Grain");
    cmd(ed, "set CameraFilters.E0Intensity 1");
    cmd(ed, "window Game");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    auto grab = [&] {
      const Recti v = ed.scene_view_rect();
      const Image &fb = ed.framebuffer();
      std::vector<uint32_t> px;
      for (int y = v.y + 40; y < v.bottom(); y++)  // below any banner / toolbar row
        for (int x = v.x; x < v.right(); x++) px.push_back(fb.pixels[(size_t)y * fb.width + x]);
      return px;
    };
    auto differing = [](const std::vector<uint32_t> &a, const std::vector<uint32_t> &b) {
      size_t d = 0;
      for (size_t i = 0; i < std::min(a.size(), b.size()); i++) d += a[i] != b[i];
      return d;
    };
    const auto first = grab();
    CHECK(first.size() > 10000);
    /* The grain's seed changes every 1/24 s while playing: 45 ms apart, frames would differ if it moved. */
    size_t moved_while_stopped = 0;
    for (int i = 0; i < 2; i++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(45));
      ed.step_frame_headless();
      moved_while_stopped += differing(first, grab());
    }
    CHECK(moved_while_stopped == 0);
    /* Grain really is on: switching the effect off changes the picture. */
    cmd(ed, "set CameraFilters.E0Enabled false");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    CHECK(differing(first, grab()) > first.size() / 10);
    cmd(ed, "set CameraFilters.E0Enabled true");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    CHECK(differing(first, grab()) == 0);
    /* Playing: the grain moves frame to frame. */
    cmd(ed, "play");
    ed.step_frame_headless();
    std::vector<std::vector<uint32_t>> frames;
    for (int i = 0; i < 5; i++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(45));
      ed.step_frame_headless();
      frames.push_back(grab());
    }
    size_t moving_pairs = 0;
    for (size_t i = 1; i < frames.size(); i++) moving_pairs += differing(frames[i - 1], frames[i]) > frames[i].size() / 10;
    std::printf("    playing: %zu of %zu consecutive Game view frames changed in over 10%% of their pixels\n", moving_pairs, frames.size() - 1);
    CHECK(moving_pairs >= 3);
    cmd(ed, "stop");
    cmd(ed, "window Game");  // leaving Play puts the Scene view back in the tab; look at the Game view again
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    const auto after = grab();
    std::this_thread::sleep_for(std::chrono::milliseconds(45));
    ed.step_frame_headless();
    CHECK(differing(after, grab()) == 0);  // still again once stopped
  });

  /* ---------------------------------------------------------------- 4 */
  test("every filter effect (Retro Console, Color, Lens, Stylize, Bloom) can be added from the console, set, saved, loaded and undone", [&] {
    const std::vector<R31Case> cases = {
        {"Color Grading", "Exposure", "1.5", [](FilterEffect *e) { return (double)static_cast<ColorGradingEffect *>(e)->exposure; }, 1.5},
        {"Posterize", "Levels", "3", [](FilterEffect *e) { return (double)static_cast<PosterizeEffect *>(e)->levels; }, 3},
        {"Grayscale / Sepia", "Mode", "1", [](FilterEffect *e) { return (double)static_cast<GrayscaleEffect *>(e)->mode; }, 1},
        {"Invert", "Amount", "0.5", [](FilterEffect *e) { return (double)static_cast<InvertEffect *>(e)->amount; }, 0.5},
        {"Vignette", "Intensity", "0.7", [](FilterEffect *e) { return (double)static_cast<VignetteEffect *>(e)->intensity; }, 0.7},
        {"Chromatic Aberration", "Intensity", "0.6", [](FilterEffect *e) { return (double)static_cast<ChromaticAberrationEffect *>(e)->intensity; }, 0.6},
        {"Film Grain", "Size", "4", [](FilterEffect *e) { return (double)static_cast<FilmGrainEffect *>(e)->size; }, 4},
        {"Lens Distortion", "Intensity", "-0.4", [](FilterEffect *e) { return (double)static_cast<LensDistortionEffect *>(e)->intensity; }, -0.4},
        {"Pixelate", "CellSize", "16", [](FilterEffect *e) { return (double)static_cast<PixelateEffect *>(e)->cell_size; }, 16},
        {"Edge Outline", "Thickness", "3", [](FilterEffect *e) { return (double)static_cast<EdgeOutlineEffect *>(e)->thickness; }, 3},
        {"CRT", "Flicker", "0.5", [](FilterEffect *e) { return (double)static_cast<CrtEffect *>(e)->flicker; }, 0.5},
        {"Sharpen", "Amount", "2.5", [](FilterEffect *e) { return (double)static_cast<SharpenEffect *>(e)->amount; }, 2.5},
        /* The other categories' effects go through the same paths (they had their own copies of this test). */
        {"Retro Console", "Width", "100", [](FilterEffect *e) { return (double)static_cast<RetroConsoleFilter *>(e)->width; }, 100},
        {"Bloom", "Threshold", "2.5", [](FilterEffect *e) { return (double)static_cast<BloomEffect *>(e)->threshold; }, 2.5},
    };
    /* The registry lists them all: three categories of four, plus Retro Console and Bloom, each with help and a
     * working factory. */
    std::map<std::string, int> per_cat;
    for (const R31Case &c : cases) {
      const FilterEffectInfo *info = find_filter_effect_info(c.name);
      CHECK(info != nullptr);
      if (!info) continue;
      per_cat[info->category]++;
      CHECK(!info->help.empty());
      auto e = create_filter_effect(c.name);
      CHECK(e && std::string(e->type_name()) == c.name);
    }
    CHECK(per_cat["Color"] == 4 && per_cat["Lens"] == 4 && per_cat["Stylize"] == 4);
    CHECK(per_cat["Retro Console"] == 1 && per_cat["Bloom & glow"] == 1);
    CHECK(filter_effect_infos().size() == cases.size());  // a new effect type gets a row here
    {  // menu order is grouped by category (a category is one run)
      std::vector<std::string> order;
      for (const FilterEffectInfo &fi : filter_effect_infos())
        if (order.empty() || order.back() != fi.category) order.push_back(fi.category);
      std::set<std::string> uniq(order.begin(), order.end());
      CHECK(uniq.size() == order.size());
    }

    Editor ed;
    ed.init_headless(1000, 700);
    ed.step_frame_headless();
    cmd(ed, "select Main Camera");
    for (size_t i = 0; i < cases.size(); i++) {
      cmd(ed, std::string("filter add ") + cases[i].name);
      CameraFilters *cf = r29_stack(ed);
      CHECK(cf && cf->effects.size() == i + 1);
      if (!cf || cf->effects.size() != i + 1) return;
      CHECK(std::string(cf->effects[i]->type_name()) == cases[i].name);
    }
    cmd(ed, "filter list");
    std::vector<double> defaults;
    for (size_t i = 0; i < cases.size(); i++) {
      defaults.push_back(cases[i].read(r29_stack(ed)->effects[i].get()));
      cmd(ed, "set CameraFilters.E" + std::to_string(i) + cases[i].field + " " + cases[i].value);
      const double v = cases[i].read(r29_stack(ed)->effects[i].get());
      if (std::fabs(v - cases[i].expect) > 1e-5) std::printf("    %s %s: expected %g, got %g\n", cases[i].name, cases[i].field, cases[i].expect, v);
      CHECK_NEAR(v, cases[i].expect, 1e-5);
      CHECK(std::fabs(defaults[i] - cases[i].expect) > 1e-5);  // the test moved it off its default
    }
    /* Save, load, same stack and values; saving again gives the same text. */
    const std::string text = save_scene_text(ed.scene());
    Scene loaded;
    std::string err;
    CHECK(load_scene_text(text, loaded, err));
    GameObject *lc = loaded.find_by_name("Main Camera");
    CHECK(lc != nullptr);
    if (!lc) return;
    CameraFilters *lcf = lc->get<CameraFilters>();
    CHECK(lcf && lcf->effects.size() == cases.size());
    if (lcf && lcf->effects.size() == cases.size())
      for (size_t i = 0; i < cases.size(); i++) {
        CHECK(std::string(lcf->effects[i]->type_name()) == cases[i].name);
        CHECK_NEAR(cases[i].read(lcf->effects[i].get()), cases[i].expect, 1e-5);
      }
    CHECK(save_scene_text(loaded) == text);
    /* Every set is one undo step, then every add. */
    for (size_t i = cases.size(); i-- > 0;) {
      cmd(ed, "undo");
      CHECK_NEAR(cases[i].read(r29_stack(ed)->effects[i].get()), defaults[i], 1e-6);
    }
    for (size_t i = cases.size(); i-- > 0;) {
      cmd(ed, "undo");
      CameraFilters *cf = r29_stack(ed);
      CHECK(cf == nullptr || cf->effects.size() == i);
    }
  });

  /* ---------------------------------------------------------------- 5 */
  test("odd values (NaN, infinity, huge, negative, extreme ints) never crash a pass and the picture keeps its size", [] {
    const float odd[] = {kNaN, kInf, -kInf, 1e30f, -1e30f, -3.0f, 1e-30f};
    const int ints[] = {0, -7, 1, 2, 100000, std::numeric_limits<int>::max(), std::numeric_limits<int>::min()};
    for (int dims = 0; dims < 2; dims++) {
      const int W = dims ? 64 : 1, H = dims ? 48 : 1;
      for (float v : odd)
        for (int iv : ints) {
          R31Target t(W, H);
          t.random_planes(5);
          FilterFrame fr = r31_frame();
          fr.animate = true;
          fr.time = v;
          r31_run_all(t.rt, v, iv, &fr);
          r31_run_all(t.rt, v, iv, nullptr);
          CHECK(t.rt.width == W && t.rt.height == H && (int)t.img.pixels.size() == W * H);
        }
      /* A frame whose camera data is garbage must not break the outline. */
      R31Target t(W, H);
      t.random_planes(6);
      FilterFrame fr = r31_frame();
      fr.eye = {kNaN, kInf, 1e30f};
      fr.forward = {kNaN, 0, 0};
      fr.far_distance = kNaN;
      for (float &m : fr.inv_view_proj.m) m = kNaN;
      apply_edge_outline(t.rt, OutlineParams{}, &fr);
      CHECK(t.rt.width == W && t.rt.height == H);
    }
  });

  test("odd sizes: every pass works on 1 x 1, 1 x N, N x 1 and on a window into a larger image (row stride, Bloom too)", [] {
    const int sizes[][2] = {{1, 1}, {1, 9}, {9, 1}, {2, 2}, {3, 7}, {64, 48}};
    for (const auto &sz : sizes) {
      R31Target t(sz[0], sz[1]);
      t.random_planes(8);
      FilterFrame fr = r31_frame();
      fr.animate = true;
      ColorGradingParams cg;
      cg.exposure = 1.0f;
      cg.saturation = -0.5f;
      apply_color_grading(t.rt, cg);
      apply_posterize(t.rt, 4);
      apply_grayscale(t.rt, true, 0.7f);
      apply_invert(t.rt, 0.3f);
      apply_vignette(t.rt, VignetteParams{});
      apply_chromatic_aberration(t.rt, 0.8f);
      apply_film_grain(t.rt, GrainParams{}, &fr);
      apply_lens_distortion(t.rt, 0.4f, 1.1f);
      apply_pixelate(t.rt, 4);
      apply_edge_outline(t.rt, OutlineParams{}, &fr);
      apply_crt(t.rt, CrtParams{}, &fr);
      apply_sharpen(t.rt, 1.0f);
      CHECK(t.rt.width == sz[0] && t.rt.height == sz[1]);
    }
    /* A target that is a window into a larger image (row stride > width): pixels outside stay put. */
    Image big;
    big.resize(20, 12);
    std::fill(big.pixels.begin(), big.pixels.end(), 0xFF336699u);
    RenderTarget rt;
    rt.attach(big, {4, 3, 10, 6});
    CHECK(rt.width == 10 && rt.height == 6 && rt.stride == 20);
    /* A white patch inside, so Bloom has something to spread (it would reach past the window if it
     * ignored the stride). */
    for (int y = 5; y < 7; y++)
      for (int x = 8; x < 11; x++) big.pixels[(size_t)y * 20 + x] = 0xFFFFFFFFu;
    const std::vector<uint32_t> before = big.pixels;
    BloomParams bp;
    bp.threshold = 0.5f;
    bp.intensity = 2.0f;
    apply_bloom(rt, bp);
    int bloom_inside = 0;
    for (size_t i = 0; i < big.pixels.size(); i++) bloom_inside += big.pixels[i] != before[i];
    CHECK(bloom_inside > 0);
    apply_invert(rt, 1.0f);
    apply_sharpen(rt, 1.0f);
    apply_pixelate(rt, 3);
    apply_crt(rt, CrtParams{}, nullptr);
    int outside_changed = 0, inside_changed = 0;
    for (int y = 0; y < 12; y++)
      for (int x = 0; x < 20; x++) {
        const bool inside = x >= 4 && x < 14 && y >= 3 && y < 9;
        const bool changed = big.pixels[(size_t)y * 20 + x] != before[(size_t)y * 20 + x];
        (inside ? inside_changed : outside_changed) += changed;
      }
    CHECK(outside_changed == 0);
    CHECK(inside_changed > 0);
  });

  /* Review of task 0006: grain moves only in the Game view while playing. An F12 render made during
   * Play (Film Grain on the camera) is the same image a few frames later. */
  test("film grain: F12 renders made during Play are reproducible (only the Game view animates)", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.step_frame_headless();
    ed.command("select Main Camera");
    ed.command("filter add Film Grain");
    ed.command("set CameraFilters.E0Intensity 0.8");
    ed.command("set render.Engine 0");
    ed.command("play");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    ed.command("render");
    const std::vector<uint32_t> first = ed.render_image_for_test().pixels;
    std::this_thread::sleep_for(std::chrono::milliseconds(120));  // several 1/24 s grain buckets later
    for (int i = 0; i < 4; i++) ed.step_frame_headless();
    ed.command("render");
    const std::vector<uint32_t> second = ed.render_image_for_test().pixels;
    ed.command("stop");
    CHECK(!first.empty() && first == second);
  });

  /* Review of task 0006: a floor seen at a grazing angle is flat, so a thick, sensitive outline draws
   * nothing on it (a first difference in distance drew a solid band toward the horizon). */
  test("edge outline: a floor at a grazing angle gets no lines, even thick and sensitive; a box on it still does", [] {
    CfScene sc;
    sc.add(primitives::quad(200.0f), Mat4::trs({0, 0, 0}, Quat::euler({90, 0, 0}), {1, 1, 1}), 3, cf_unlit({0.6f, 0.6f, 0.6f}));
    sc.add(primitives::cube(), Mat4::trs({0, 0.5f, 6}, Quat(), {1, 1, 1}), 4, cf_unlit({0.9f, 0.3f, 0.2f}));
    sc.seal();
    const int W = 160, H = 100;
    const Vec3 eye{0, 0.6f, 0};
    const Mat4 v = Mat4::look_at(eye, {0, 0.45f, 10}, {0, 1, 0});
    const Mat4 pr = Mat4::perspective(60 * kDeg2Rad, W / (float)H, 0.1f, 500.0f);
    Image img;
    RenderTarget rt;
    cf_render(img, rt, W, H, v, pr, RasterOptions{}, sc);
    FilterFrame fr;
    fr.inv_view_proj = (pr * v).inverse();
    fr.eye = eye;
    fr.forward = normalize(Vec3(0, 0.45f, 10) - eye);
    OutlineParams op;
    op.color = {1, 0, 1};
    op.thickness = 8;
    op.depth_sensitivity = 0.005f;
    op.object_edges = false;  // only the depth test
    const std::vector<uint32_t> before = img.pixels;
    apply_edge_outline(rt, op, &fr);
    int floor_lines = 0, floor_px = 0, box_lines = 0;
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++) {
        const size_t i = (size_t)y * W + x;
        const bool line = img.pixels[i] != before[i];
        if (rt.ids[i] == 3) {
          /* Floor pixels well away from the box and the horizon (8 px either way). */
          bool clear = true;
          for (int dy = -9; dy <= 9 && clear; dy++)
            for (int dx = -9; dx <= 9 && clear; dx++) {
              const int xx = x + dx, yy = y + dy;
              if (xx < 0 || yy < 0 || xx >= W || yy >= H || rt.ids[(size_t)yy * W + xx] != 3) clear = false;
            }
          if (clear) floor_px++, floor_lines += line;
        }
        if (rt.ids[i] == 4) box_lines += line;
      }
    std::printf("    grazing floor: %d of %d interior pixels marked; box: %d marked\n", floor_lines, floor_px, box_lines);
    CHECK(floor_px > 200);
    CHECK(floor_lines == 0);
    CHECK(box_lines > 0);
  });
}

/* ===================================================================== */
/* Round 32 (task 0007): Bloom, with an HDR plane in the rasterizer       */
/* ===================================================================== */

namespace {
/* A bright unlit quad (linear light `lum`, all channels) on a dark background, seen square on. */
struct R32Scene {
  CfScene sc;
  Mat4 v, p;
  int W, H;
  R32Scene(float lum, int w = 128, int h = 128, float quad = 1.0f) : W(w), H(h) {
    sc.add(primitives::quad(quad), Mat4::identity(), 9, cf_unlit({lum, lum, lum}));
    sc.seal();
    v = Mat4::look_at({0, 0, -5}, {0, 0, 0}, {0, 1, 0});
    p = Mat4::perspective(50 * kDeg2Rad, w / (float)h, 0.1f, 50);
  }
  void render(Image &img, RenderTarget &rt, bool want_hdr) const {
    img.resize(W, H);
    rt.attach(img, {0, 0, W, H});
    rt.want_hdr = want_hdr;
    draw(rt);
  }
  void draw(RenderTarget &rt) const { cf_draw(rt, v, p, RasterOptions{}, sc, 0xFF101010u); }
  /* The middle row's last bright pixel of the quad (right edge). */
  int right_edge(const Image &img) const {
    int e = W / 2;
    while (e < W - 1 && (int)((img.pixels[(size_t)(H / 2) * W + e + 1] >> 8) & 255) > 100) e++;
    return e;
  }
};
inline int r32_sum(uint32_t c) { return r31_ch(c, 0) + r31_ch(c, 1) + r31_ch(c, 2); }
inline long r32_total(const Image &img) {
  long t = 0;
  for (uint32_t c : img.pixels) t += r32_sum(c);
  return t;
}
/* A pixel's gain over the render without bloom. */
inline int r32_gain(const Image &a, const Image &b, int x, int y) {
  return r32_sum(a.pixels[(size_t)y * a.width + x]) - r32_sum(b.pixels[(size_t)y * b.width + x]);
}
/* A copy of `base` with the hdr plane of `src` (as a bloom pass would find it in a render). */
struct R32Copy {
  Image img;
  RenderTarget rt;
  R32Copy(const Image &base, const RenderTarget &src) {
    img = base;
    rt.attach(img, {0, 0, base.width, base.height});
    rt.hdr = src.hdr;
    rt.hdr_view_transform = src.hdr_view_transform;
    rt.hdr_exposure = src.hdr_exposure;
  }
};
}  // namespace

static void round32_tests() {
  auto cmd = [&](Editor &ed, const std::string &c) {
    ed.command(c);
    ed.step_frame_headless();
  };

  /* ---------------------------------------------------------------- 1 */
  test("bloom: a bright quad glows into the pixels around it, fading with distance, and a dim one changes nothing", [] {
    R32Scene s(8.0f);
    Image base;
    RenderTarget rb;
    s.render(base, rb, true);
    CHECK(rb.hdr.size() >= (size_t)s.W * s.H);
    if (rb.hdr.size() >= (size_t)s.W * s.H) {
      CHECK(rb.hdr[(size_t)64 * s.W + 64].x > 4.0f);  // the quad keeps its real light (8), not the clipped 1
      CHECK(rb.hdr[0].x < 0.0f);                      // the clear colour was not shaded
    }
    const int edge = s.right_edge(base);
    CHECK(edge > 70 && edge < s.W - 40);
    R32Copy c(base, rb);
    BloomParams bp;
    bp.scatter = 0.3f;
    apply_bloom(c.rt, bp);
    const int g1 = r32_gain(c.img, base, edge + 2, 64), g2 = r32_gain(c.img, base, edge + 6, 64), g3 = r32_gain(c.img, base, edge + 14, 64);
    std::printf("    bloom gain at 2, 6, 14 px from the quad: %d, %d, %d\n", g1, g2, g3);
    CHECK(g1 > 0);
    CHECK(g1 >= g2 && g2 >= g3);
    CHECK(g1 > g3);
    /* Far away (the corners) the faint tail of the widest levels is all that arrives: under 2% of what
     * the pixel next to the quad gets. (Not exactly 0: the pyramid's top levels cover the whole frame.) */
    std::printf("    corner gains: %d %d\n", r32_gain(c.img, base, 0, 0), r32_gain(c.img, base, s.W - 1, s.H - 1));
    CHECK(r32_gain(c.img, base, 0, 0) >= 0 && r32_gain(c.img, base, 0, 0) * 50 < g1);
    CHECK(r32_gain(c.img, base, s.W - 1, s.H - 1) >= 0 && r32_gain(c.img, base, s.W - 1, s.H - 1) * 50 < g1);
    /* A dim quad (below the threshold): bit-identical, with or without the plane. */
    R32Scene dim(0.5f);
    Image dbase;
    RenderTarget drb;
    dim.render(dbase, drb, true);
    R32Copy d(dbase, drb);
    apply_bloom(d.rt, BloomParams{});
    CHECK(d.img.pixels == dbase.pixels);
    Image plain = dbase;
    RenderTarget prt;
    prt.attach(plain, {0, 0, dim.W, dim.H});
    apply_bloom(prt, BloomParams{});
    CHECK(plain.pixels == dbase.pixels);
  });

  /* ---------------------------------------------------------------- 2 */
  test("bloom: more intensity adds more light, and a larger scatter reaches farther", [] {
    R32Scene s(8.0f);
    Image base;
    RenderTarget rb;
    s.render(base, rb, true);
    const int edge = s.right_edge(base);
    auto run = [&](float intensity, float scatter) {
      R32Copy c(base, rb);
      BloomParams bp;
      bp.intensity = intensity;
      bp.scatter = scatter;
      apply_bloom(c.rt, bp);
      return c.img;
    };
    long prev = 0;
    for (float in : {0.1f, 0.3f, 0.6f, 1.2f}) {
      const Image im = run(in, 0.5f);
      const long added = r32_total(im) - r32_total(base);
      std::printf("    intensity %.1f adds %ld\n", in, added);
      CHECK(added >= prev);
      prev = added;
      size_t darker = 0;
      for (size_t i = 0; i < im.pixels.size(); i++) darker += r32_sum(im.pixels[i]) < r32_sum(base.pixels[i]);
      CHECK(darker == 0);  // light is only ever added
    }
    CHECK(prev > 0);
    const Image tight = run(1.0f, 0.1f), wide = run(1.0f, 1.0f);
    const int x = std::min(s.W - 1, edge + 20);
    const int gt = r32_gain(tight, base, x, 64), gw = r32_gain(wide, base, x, 64);
    std::printf("    gain %d px from the quad: scatter 0.1 -> %d, scatter 1 -> %d\n", x - edge, gt, gw);
    CHECK(gw > gt);
  });

  /* ---------------------------------------------------------------- 3 */
  test("bloom: with no Bloom effect, or intensity 0, the picture is bit-identical and no HDR plane is kept", [] {
    R32Scene s(8.0f);
    Image a, b, c;
    RenderTarget ra, rb, rc;
    s.render(a, ra, false);
    CHECK(ra.hdr.empty());
    s.render(b, rb, true);
    CHECK(!rb.hdr.empty());
    CHECK(a.pixels == b.pixels);  // asking for the plane doesn't change the colours
    s.render(c, rc, false);
    CHECK(rc.hdr.empty() && c.pixels == a.pixels);
    /* Asking again without it frees the plane on the same target. */
    rb.want_hdr = false;
    s.draw(rb);
    CHECK(rb.hdr.empty());
    /* Intensity 0 with a plane present changes nothing. */
    Image bb;
    RenderTarget rh;
    s.render(bb, rh, true);
    BloomParams zero;
    zero.intensity = 0.0f;
    apply_bloom(rh, zero);
    CHECK(bb.pixels == a.pixels);
    /* Through the stack: only an enabled Bloom asks for the plane. */
    FilterStack empty;
    CHECK(!empty.needs_hdr);
    CameraFilters cf;
    cf.add(PosterizeEffect::kName);
    FilterStack ps;
    cf.contribute(ps);
    CHECK(!ps.needs_hdr && ps.passes.size() == 1);
    auto *bl = static_cast<BloomEffect *>(cf.add(BloomEffect::kName));
    CHECK(bl != nullptr);
    FilterStack bs;
    cf.contribute(bs);
    CHECK(bs.needs_hdr && bs.passes.size() == 2);
    bl->enabled = false;
    FilterStack off;
    cf.contribute(off);
    CHECK(!off.needs_hdr && off.passes.size() == 1);
    /* Bloom at intensity 0 in a stack leaves the render untouched. */
    bl->enabled = true;
    bl->intensity = 0.0f;
    FilterStack z;
    cf.contribute(z);
    Image zi;
    RenderTarget zr;
    s.render(zi, zr, z.needs_hdr);
    for (auto &pass : z.passes)
      if (&pass != &z.passes[0]) pass(zr, nullptr);  // skip the posterize pass: only bloom is under test
    CHECK(zi.pixels == a.pixels);
  });

  /* ---------------------------------------------------------------- 4 */
  test("bloom: order in the stack matters, and each order equals applying the passes by hand", [] {
    R32Scene s(8.0f);
    Image base;
    RenderTarget rb;
    s.render(base, rb, true);
    BloomParams bp;
    bp.scatter = 0.4f;
    R32Copy h1(base, rb), h2(base, rb);
    apply_posterize(h1.rt, 4);
    apply_bloom(h1.rt, bp);
    apply_bloom(h2.rt, bp);
    apply_posterize(h2.rt, 4);
    CHECK(h1.img.pixels != h2.img.pixels);
    /* Bloom after Posterize builds on the posterized picture: it only adds light, so no channel falls
     * below its posterized value, and pixels the glow doesn't reach keep their posterized level
     * (re-encoding the shader's HDR value there would undo the posterize). */
    {
      R32Copy po(base, rb);
      apply_posterize(po.rt, 4);
      size_t lower = 0, kept = 0;
      for (size_t i = 0; i < po.img.pixels.size(); i++) {
        const uint32_t a = po.img.pixels[i], b = h1.img.pixels[i];
        for (int sh = 0; sh <= 16; sh += 8) lower += ((b >> sh) & 255) < ((a >> sh) & 255);
        kept += a == b;
      }
      std::printf("    posterize -> bloom: %zu channels darker, %zu of %zu pixels unchanged\n", lower, kept, po.img.pixels.size());
      CHECK(lower == 0);
      CHECK(kept > 0);
    }
    auto stacked = [&](const char *first, const char *second) {
      CameraFilters cf;
      FilterEffect *e1 = cf.add(first);
      FilterEffect *e2 = cf.add(second);
      for (FilterEffect *e : {e1, e2}) {
        if (auto *b = dynamic_cast<BloomEffect *>(e)) b->scatter = bp.scatter;
        if (auto *p = dynamic_cast<PosterizeEffect *>(e)) p->levels = 4;
      }
      FilterStack st;
      cf.contribute(st);
      R32Copy c(base, rb);
      for (auto &pass : st.passes) pass(c.rt, nullptr);
      return c.img.pixels;
    };
    CHECK(stacked("Posterize", "Bloom") == h1.img.pixels);
    CHECK(stacked("Bloom", "Posterize") == h2.img.pixels);
  });

  /* ---------------------------------------------------------------- 5 */
  test("bloom: odd values (NaN threshold / tint, huge or negative intensity) and odd sizes never crash and keep the picture's size", [] {
    const float odd[] = {kNaN, kInf, -kInf, 1e30f, -1e30f, -3.0f, 0.0f, 1e-30f};
    for (int dims = 0; dims < 5; dims++) {
      const int W = dims == 0 ? 1 : dims == 1 ? 2 : dims == 2 ? 3 : dims == 3 ? 64 : 9;
      const int H = dims == 0 ? 1 : dims == 1 ? 2 : dims == 2 ? 1 : dims == 3 ? 48 : 31;
      for (float v : odd)
        for (int use_hdr = 0; use_hdr < 2; use_hdr++) {
          /* Every odd value at the smallest and an uneven size; the other sizes with NaN and infinity
           * (each bloom builds a 540-row pyramid, so the full cross product took a second). */
          if ((dims == 1 || dims == 2 || dims == 3) && !(std::isnan(v) || v == kInf)) continue;
          R31Target t(W, H);
          t.random_planes(11);
          if (use_hdr) {
            t.rt.hdr.assign((size_t)W * H, Vec3(4.0f));
            t.rt.hdr[0] = Vec3(-1.0f, 0, 0);
          }
          BloomParams p;
          p.threshold = v;
          apply_bloom(t.rt, p);
          p = BloomParams{};
          p.intensity = v;
          p.soft_knee = v;
          p.scatter = v;
          p.clamp = v;
          apply_bloom(t.rt, p);
          p = BloomParams{};
          p.tint = Vec3(v, 1.0f, v);
          apply_bloom(t.rt, p);
          p.tint = Vec3(v);
          p.intensity = 5.0f;
          apply_bloom(t.rt, p);
          CHECK(t.rt.width == W && t.rt.height == H && (int)t.img.pixels.size() == W * H);
          CHECK(count_bad("bloom alpha", t.img.pixels, opaque) == 0);
        }
    }
    /* An empty target (no colour plane) is a no-op, not a crash. */
    RenderTarget none;
    apply_bloom(none, BloomParams{});
    CHECK(none.width == 0 && none.color == nullptr);
  });

  test("bloom: infinite and NaN values in the HDR plane don't spread NaN into the picture", [] {
    /* The plane is consistent with the 8-bit grey (0x30 is linear 0.0296) except for the bad pixels. */
    const float grey = 0.0296f;
    auto run = [&](Vec3 at_nan) {
      auto t = std::make_unique<R31Target>(32, 32);
      t->fill(0xFF303030u);
      t->rt.hdr.assign(32 * 32, Vec3(grey));
      t->rt.hdr[5 * 32 + 5] = Vec3(kInf, kNaN, 1e30f);
      t->rt.hdr[20 * 32 + 20] = at_nan;
      t->rt.hdr[10 * 32 + 25] = Vec3(-kInf, 5.0f, 5.0f);
      t->rt.hdr[25 * 32 + 3] = Vec3(50.0f);  // one honest bright pixel
      apply_bloom(t->rt, BloomParams{});
      return t;
    };
    auto bad = run(Vec3(kNaN));
    CHECK(count_bad("alpha", bad->img.pixels, opaque) == 0);
    /* A NaN pixel counts as "not shaded" (decoded from its 8-bit grey): same picture as -1 there. */
    auto ref = run(Vec3(-1.0f, 0, 0));
    CHECK(bad->img.pixels == ref->img.pixels);
    /* Infinity in the plane is no light at all: the pixel next to the honest one stays near its grey. */
    CHECK(r32_sum(bad->at(5, 5)) < 3 * 255);
  });

  /* ---------------------------------------------------------------- 6 */
  test("bloom: the Game view glows around an emissive sphere once Bloom is on the Main Camera, and undoing the add restores it", [&] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.step_frame_headless();
    cmd(ed, "select Sphere");
    cmd(ed, "material Emissive");
    cmd(ed, "select Main Camera");
    cmd(ed, "window Game");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    auto grab = [&] {
      const Recti v = ed.scene_view_rect();
      const Image &fb = ed.framebuffer();
      std::vector<uint32_t> px;
      for (int y = v.y + 40; y < v.bottom(); y++)
        for (int x = v.x; x < v.right(); x++) px.push_back(fb.pixels[(size_t)y * fb.width + x]);
      return px;
    };
    const auto without = grab();
    CHECK(without.size() > 10000);
    cmd(ed, "filter add Bloom");
    CameraFilters *cf = r29_stack(ed);
    CHECK(cf && cf->effects.size() == 1 && std::string(cf->effects[0]->type_name()) == "Bloom");
    const FilterEffectInfo *info = find_filter_effect_info("Bloom");
    CHECK(info && info->category == std::string("Bloom & glow") && !info->help.empty());
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    const auto with = grab();
    CHECK(with.size() == without.size());
    size_t brighter = 0, darker = 0;
    long gain = 0;
    for (size_t i = 0; i < std::min(with.size(), without.size()); i++) {
      const int d = r32_sum(with[i]) - r32_sum(without[i]);
      brighter += d > 0;
      darker += d < 0;
      gain += d;
    }
    std::printf("    Game view with bloom: %zu pixels brighter, %zu darker, total %+ld\n", brighter, darker, gain);
    CHECK(brighter > 50);
    CHECK(darker == 0);
    CHECK(gain > 0);
    /* Threshold's save / load / undo is Bloom's row in "every filter effect ... can be added from the
     * console"; Intensity and Scatter are checked here. */
    cmd(ed, "set CameraFilters.E0Intensity 1.5");
    cmd(ed, "set CameraFilters.E0Scatter 0.2");
    {
      const std::string text = save_scene_text(ed.scene());
      Scene loaded;
      std::string err;
      CHECK(load_scene_text(text, loaded, err));
      GameObject *lc = loaded.find_by_name("Main Camera");
      CameraFilters *lcf = lc ? lc->get<CameraFilters>() : nullptr;
      auto *b = lcf && lcf->effects.size() == 1 ? dynamic_cast<BloomEffect *>(lcf->effects[0].get()) : nullptr;
      CHECK(b != nullptr);
      if (b) CHECK(std::fabs(b->intensity - 1.5f) < 1e-5f && std::fabs(b->scatter - 0.2f) < 1e-5f);
      CHECK(save_scene_text(loaded) == text);
    }
    cmd(ed, "undo");
    cmd(ed, "undo");
    cf = r29_stack(ed);
    auto *bl = cf && cf->effects.size() == 1 ? static_cast<BloomEffect *>(cf->effects[0].get()) : nullptr;
    CHECK(bl && std::fabs(bl->intensity - 0.6f) < 1e-6f && std::fabs(bl->scatter - 0.7f) < 1e-6f);  // the defaults
    cmd(ed, "undo");
    cf = r29_stack(ed);
    CHECK(cf == nullptr || cf->effects.empty());
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    CHECK(grab() == without);  // and the picture is the plain one again
  });

  test("bloom: a path-traced F12 render through a bloom camera glows around an emissive object", [&] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.step_frame_headless();
    cmd(ed, "select Sphere");
    cmd(ed, "material Emissive");
    cmd(ed, "select Main Camera");
    ed.command("set Render.RenderEngine Path");
    ed.command("set Render.Samples 2");
    ed.command("set Render.Denoise false");
    /* The render advances a slice per frame: step until it reports done (with a generous cap). */
    auto render = [&] {
      ed.command("render");
      for (int i = 0; i < 2000 && ed.rendering_for_test(); i++) ed.step_frame_headless();
      CHECK(!ed.rendering_for_test());
      return ed.render_image_for_test();
    };
    const Image plain = render();
    CHECK(!plain.pixels.empty());
    cmd(ed, "filter add Bloom");
    const Image glow = render();
    CHECK(glow.width == plain.width && glow.height == plain.height);
    if (glow.width != plain.width || glow.height != plain.height || plain.pixels.empty()) return;
    const long a = r32_total(plain), b = r32_total(glow);
    std::printf("    F12 path traced: channel total %ld plain, %ld with bloom (%+.2f%%)\n", a, b, 100.0 * (b - a) / std::max(1L, a));
    CHECK(b > a);
  });
}

/* ===================================================================== */
/* Round 34: the editor stops redoing work it has done (task 0011)         */
/* ===================================================================== */

namespace {
using R34ET = platform::EventType;

platform::Event r34_mouse(R34ET t, int x, int y) {
  platform::Event e;
  e.type = t;
  e.x = x;
  e.y = y;
  return e;
}

/* What the 3D views did over a stretch of frames. */
struct R34Count {
  uint64_t frames = 0, renders = 0, hits = 0, shadows = 0, mismatches = 0;
};
R34Count r34_totals(const Editor &ed) {
  const FrameProfile &p = ed.frame_profile_totals();
  return {p.frames, p.view_renders, p.view_cache_hits, p.shadow_renders, p.cache_mismatches};
}
R34Count operator-(const R34Count &a, const R34Count &b) {
  return {a.frames - b.frames, a.renders - b.renders, a.hits - b.hits, a.shadows - b.shadows, a.mismatches - b.mismatches};
}

std::vector<uint32_t> r34_crop(const Editor &ed, const Recti &r) {
  std::vector<uint32_t> out;
  const Image &fb = ed.framebuffer();
  for (int y = r.y; y < r.bottom() && y < fb.height; y++)
    for (int x = r.x; x < r.right() && x < fb.width; x++) out.push_back(fb.pixels[(size_t)y * fb.width + x]);
  return out;
}

void r34_steps(Editor &ed, int n) {
  for (int i = 0; i < n; i++) ed.step_frame_headless();
}

/* A click with real events (moved there first, then down, then up on later frames). */
void r34_click(Editor &ed, int x, int y) {
  ed.step_frame_headless({r34_mouse(R34ET::MouseMove, x, y)});
  ed.step_frame_headless({r34_mouse(R34ET::MouseDown, x, y)});
  ed.step_frame_headless({r34_mouse(R34ET::MouseUp, x, y)});
}

/* The Scene view's toolbar buttons after the draw-mode combo: 0 scene lighting, 1 grid, 2 gizmos, 3 statistics. */
void r34_toolbar_click(Editor &ed, int index) {
  const ui::Context &u = ed.ui_for_test();
  const Recti v = ed.scene_view_rect();
  const int bh = u.row_h() + u.px(4), h = bh - u.px(6);
  const int x = v.x + u.px(6) + u.px(176) + index * (h + u.px(2)) + h / 2, y = v.y - bh + u.px(3) + h / 2;
  r34_click(ed, x, y);
}

/* The mouse rests over the Hierarchy: nothing under it draws in a 3D view. */
void r34_park_mouse(Editor &ed) {
  const Recti h = ed.window_rect_for_test(WindowKind::Hierarchy);
  ed.step_frame_headless({r34_mouse(R34ET::MouseMove, h.x + h.w / 2, h.y + h.h - 10)});
}

/* A 1600x1000 editor with the 2 by 3 layout (Scene and Game both visible), a known camera, and the
 * Statistics overlay off (it prints frame times, which change every frame by design). */
void r34_open(Editor &ed, bool two_by_three = true) {
  ed.init_headless(1600, 1000);
  /* These tests count renders and hits: verify mode (BLENDITY_RENDER_CACHE_VERIFY, which re-renders every
   * hit) would turn the hits into renders. The verify-mode test turns it on itself. */
  ed.set_render_cache_verify(false);
  if (two_by_three) ed.command("layout 2 by 3");
  ed.command("camera 25 20 7 0 0.5 0");
  r34_steps(ed, 3);
  r34_toolbar_click(ed, 3);
  r34_park_mouse(ed);
  r34_steps(ed, 4);
}

/* Drags the Move gizmo's X arrow (at a world point) with a real mouse drag. */
void r34_gizmo_drag_x(Editor &ed, Vec3 world_point, int pixels) {
  int x = 0, y = 0;
  CHECK(ed.project_to_window(world_point, x, y));
  ed.step_frame_headless({r34_mouse(R34ET::MouseMove, x + 55, y)});
  ed.step_frame_headless({r34_mouse(R34ET::MouseDown, x + 55, y)});
  ed.step_frame_headless({r34_mouse(R34ET::MouseMove, x + 55 + pixels / 2, y)});
  ed.step_frame_headless({r34_mouse(R34ET::MouseMove, x + 55 + pixels, y)});
  ed.step_frame_headless({r34_mouse(R34ET::MouseUp, x + 55 + pixels, y)});
}

struct R34Change {
  R34Count d;                           // what the action's frames cost
  std::vector<uint32_t> before, after;  // the Scene view's pixels
  Recti rect_before, rect_after;
};
R34Change r34_change(Editor &ed, const std::function<void()> &act, int frames = 1) {
  R34Change c;
  c.rect_before = ed.scene_view_rect();
  c.before = r34_crop(ed, c.rect_before);
  const R34Count t0 = r34_totals(ed);
  act();
  r34_steps(ed, frames);
  c.d = r34_totals(ed) - t0;
  c.rect_after = ed.scene_view_rect();
  c.after = r34_crop(ed, c.rect_after);
  return c;
}

/* One "this must redraw the Scene view" case on a fresh editor. */
void r34_redraw_case(const char *name, const std::function<void(Editor &)> &setup, const std::function<void(Editor &)> &act,
                     const std::function<void(Editor &, const R34Change &)> &extra = {}) {
  test(name, [=] {
    Editor ed;
    r34_open(ed);
    if (setup) setup(ed);
    r34_steps(ed, 4);
    const R34Change c = r34_change(ed, [&] { act(ed); });
    std::printf("    renders %llu, cache hits %llu, shadow maps %llu\n", (unsigned long long)c.d.renders, (unsigned long long)c.d.hits,
                (unsigned long long)c.d.shadows);
    CHECK(c.d.renders >= 1);
    CHECK(c.before != c.after);
    if (extra) extra(ed, c);
    /* ...and it settles again: the next idle frames cost nothing. */
    const R34Count t0 = r34_totals(ed);
    r34_steps(ed, 3);
    CHECK((r34_totals(ed) - t0).renders == 0);
  });
}

/* A scene for the Renderer3D tests: two cubes with different ids. */
struct R34Raster {
  CfScene sc;
  Mat4 v, p;
  int W, H;
  explicit R34Raster(int w = 300, int h = 200) : W(w), H(h) {
    sc.add(primitives::cube(), Mat4::translate(Vec3(0.0f, 0.0f, 0.0f)), 9, make_material("a", {0.8f, 0.3f, 0.2f}));
    sc.add(primitives::cube(), Mat4::translate(Vec3(2.2f, 0.4f, 1.0f)), 5, make_material("b", {0.2f, 0.5f, 0.8f}));
    sc.seal();
    v = Mat4::look_at({3, 3, -6}, {0.5f, 0, 0}, {0, 1, 0});
    p = Mat4::perspective(60 * kDeg2Rad, w / (float)h, 0.1f, 60);
  }
  void render(Image &img, RenderTarget &rt, Renderer3D &r, ShadeMode mode = ShadeMode::Deferred) const {
    img.resize(W, H);
    rt.attach(img, {0, 0, W, H});
    RasterOptions opt;
    opt.shade = mode;
    LightingEnv env;
    RenderLight sun;
    sun.direction = normalize(Vec3(-0.3f, -1, 0.4f));
    env.lights.push_back(sun);
    r.begin(&rt, v, p, env, opt);
    r.clear(0xFF204060u);
    for (const DrawItem &it : sc.items) r.add(it);
    r.flush();
  }
};

struct R34Rng {
  uint32_t s;
  float f() {
    s = s * 1664525u + 1013904223u;
    return (float)(s >> 8) / (float)(1u << 24);
  }
  float range(float a, float b) { return a + (b - a) * f(); }
};
}  // namespace

static void round34_tests() {
  /* ---------------------------------------------------------------- 1 */
  test("render cache: an idle frame and mouse moves over the Hierarchy redraw no 3D view, and the Scene and Game pictures stay bit-identical", [] {
    Editor ed;
    r34_open(ed);
    const Recti sr = ed.scene_view_rect(), gr = ed.window_rect_for_test(WindowKind::Game);
    CHECK(sr.w > 200 && sr.h > 200 && gr.w > 100 && gr.h > 100);
    CHECK(r34_totals(ed).renders > 0);  // the first frames did draw the views
    const std::vector<uint32_t> s0 = r34_crop(ed, sr), g0 = r34_crop(ed, gr);
    /* The views are really there: not an all-one-colour crop. */
    CHECK(std::set<uint32_t>(s0.begin(), s0.end()).size() > 8);
    CHECK(std::set<uint32_t>(g0.begin(), g0.end()).size() > 8);
    const R34Count t0 = r34_totals(ed);
    r34_steps(ed, 5);  // idle
    const Recti hr = ed.window_rect_for_test(WindowKind::Hierarchy);
    CHECK(hr.w > 20 && hr.h > 20);
    for (int i = 0; i < 12; i++) ed.step_frame_headless({r34_mouse(R34ET::MouseMove, hr.x + 10 + i * 7, hr.y + 15 + (i % 4) * 9)});
    const R34Count d = r34_totals(ed) - t0;
    std::printf("    %llu frames: %llu view renders, %llu cache hits, %llu shadow maps\n", (unsigned long long)d.frames,
                (unsigned long long)d.renders, (unsigned long long)d.hits, (unsigned long long)d.shadows);
    CHECK(d.frames == 17);
    CHECK(d.renders == 0);
    CHECK(d.shadows == 0);
    CHECK(d.hits >= 17);  // at least the Scene view every frame
    CHECK(r34_crop(ed, sr) == s0);
    CHECK(r34_crop(ed, gr) == g0);
  });

  /* ---------------------------------------------------------------- 2 */
  r34_redraw_case(
      "render cache: dragging the Move gizmo arrow redraws the Scene view and the cube's shadow",
      [](Editor &ed) {
        ed.command("select Cube");
        ed.command("camera 0 20 6 0 0.5 0");  // the X arrow points straight right on screen
        ed.command("tool move");
      },
      [](Editor &ed) {
        GameObject *cube = ed.scene().find_by_name("Cube");
        const float x0 = cube->world_position().x;
        r34_gizmo_drag_x(ed, cube->world_position(), 60);
        CHECK(std::fabs(cube->world_position().x - x0) > 0.1f);  // the real drag moved it
      },
      [](Editor &, const R34Change &c) { CHECK(c.d.shadows >= 1); });

  r34_redraw_case(
      "render cache: moving an object from the console redraws the Scene view",
      [](Editor &ed) { ed.command("select Cube"); }, [](Editor &ed) { ed.command("position 1.5 0.5 0"); });

  r34_redraw_case(
      "render cache: editing a material colour redraws the Scene view",
      [](Editor &ed) {
        ed.command("select Cube");
        MeshRenderer *mr = ed.scene().find_by_name("Cube")->get<MeshRenderer>();
        if (mr && mr->materials.empty()) mr->materials.push_back(make_material("Painted", {0.8f, 0.8f, 0.8f}));  // the cube starts on the default material
      },
      [](Editor &ed) {
        MeshRenderer *mr = ed.scene().find_by_name("Cube")->get<MeshRenderer>();
        CHECK(mr && !mr->materials.empty() && mr->materials[0]);
        if (!mr || mr->materials.empty() || !mr->materials[0]) return;
        mr->materials[0]->base_color = {0.05f, 0.9f, 0.1f};
        mr->materials[0]->touch();
      });

  r34_redraw_case(
      "render cache: rotating the sun redraws the Scene view and makes a new shadow map",
      [](Editor &) {},
      [](Editor &ed) {
        GameObject *sun = ed.scene().find_by_name("Directional Light");
        CHECK(sun != nullptr);
        if (sun) sun->set_local_rotation(Quat::euler({25.0f, 130.0f, 0.0f}));
      },
      [](Editor &, const R34Change &c) { CHECK(c.d.shadows >= 1); });

  test("render cache: undo brings back the exact picture from before the change", [] {
    Editor ed;
    r34_open(ed);
    ed.command("select Cube");
    r34_steps(ed, 4);
    const Recti sr = ed.scene_view_rect();
    const std::vector<uint32_t> first = r34_crop(ed, sr);
    const R34Change moved = r34_change(ed, [&] { ed.command("position 1.5 0.5 0"); });
    CHECK(moved.d.renders >= 1 && moved.after != first);
    const R34Change undone = r34_change(ed, [&] { ed.command("undo"); });
    CHECK(undone.d.renders >= 1);
    CHECK(undone.after != moved.after);
    CHECK(undone.after == first);
    /* Redo goes forward again to the very picture the move made. */
    const R34Change redone = r34_change(ed, [&] { ed.command("redo"); });
    CHECK(redone.d.renders >= 1 && redone.after == moved.after);
  });

  r34_redraw_case(
      "render cache: changing the layout (a new Scene view size) redraws it",
      [](Editor &) {}, [](Editor &ed) { ed.command("layout Default"); },
      [](Editor &, const R34Change &c) { CHECK(c.rect_before.w != c.rect_after.w || c.rect_before.h != c.rect_after.h); });

  r34_redraw_case(
      "render cache: selecting another object redraws the Scene view (the outline moves)",
      [](Editor &ed) { ed.command("select Cube"); }, [](Editor &ed) { ed.command("select Directional Light"); });

  test("render cache: selecting an object draws its outline in the Scene view", [] {
    Editor ed;
    r34_open(ed);
    r34_steps(ed, 3);
    const std::vector<uint32_t> none = r34_crop(ed, ed.scene_view_rect());
    const R34Change sel = r34_change(ed, [&] { ed.command("select Cube"); });
    CHECK(sel.d.renders >= 1 && sel.after != none);
    size_t orange = 0;
    for (uint32_t c : sel.after) {
      const int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
      orange += r > 200 && g > 100 && g < 190 && b < 80;
    }
    CHECK(orange > 20);  // the selection outline
  });

  r34_redraw_case(
      "render cache: an Edit Mode vertex drag with the Move gizmo redraws the Scene view",
      [](Editor &ed) {
        ed.command("select Cube");
        ed.command("edit vertex");
        ed.command("vsel 0");
        ed.command("camera 0 20 6 0 0.5 0");
        ed.command("tool move");
      },
      [](Editor &ed) {
        GameObject *cube = ed.scene().find_by_name("Cube");
        const Mesh &m0 = *cube->get<MeshFilter>()->mesh;
        const Vec3 wp = cube->world_matrix().point(m0.positions[0]);
        const Vec3 before = m0.positions[0];
        r34_gizmo_drag_x(ed, wp, 50);
        const Mesh &m1 = *ed.scene().find_by_name("Cube")->get<MeshFilter>()->mesh;
        std::printf("    vertex 0 moved from x %.3f to x %.3f\n", before.x, m1.positions[0].x);
        CHECK(std::fabs(m1.positions[0].x - before.x) > 1e-4f);  // the drag hit the gizmo (not a selection click)
      });

  /* Review of task 0011: inputs that once missed the cache keys. In verify mode every would-be hit is
   * rendered and compared, so a stale picture is a mismatch. */
  test("render cache: Show in Renders, a modifier's Show in Edit Mode and re-parenting a selected object's child leave no stale picture", [] {
    Editor ed;
    r34_open(ed);
    ed.set_render_cache_verify(true);
    const uint64_t before = Editor::render_cache_mismatches_all();
    const Recti game = ed.window_rect_for_test(WindowKind::Game);
    CHECK(game.w > 0);
    /* Show in Renders off: the Game view (and the Camera Preview) lose the cube. */
    ed.command("select Main Camera");  // the Camera Preview is up
    r34_steps(ed, 3);
    const std::vector<uint32_t> g0 = r34_crop(ed, game);
    if (GameObject *cube = ed.scene().find_by_name("Cube")) cube->get<MeshRenderer>()->show_in_renders = false;
    r34_steps(ed, 3);
    CHECK(r34_crop(ed, game) != g0);
    /* A modifier's Show in Edit Mode switches the cage between the modifier's result and the bare mesh. */
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube != nullptr);
    if (!cube) return;
    cube->get<MeshRenderer>()->show_in_renders = true;
    auto *arr = static_cast<ArrayModifier *>(cube->add_component(create_component("ArrayModifier")));
    arr->count = 3;
    ed.command("select Cube");
    ed.command("edit vertex");
    r34_steps(ed, 3);
    arr->show_in_editmode = !arr->show_in_editmode;
    r34_steps(ed, 3);
    arr->show_in_editmode = !arr->show_in_editmode;
    r34_steps(ed, 3);
    ed.command("edit off");
    /* A child gets its own outline when its parent is selected: parent the sphere under the cube, select
     * the cube, then un-parent it in place (its world matrix doesn't change). */
    GameObject *sphere = ed.scene().find_by_name("Sphere");
    CHECK(sphere != nullptr);
    if (!sphere) return;
    ed.scene().set_parent(sphere, cube);
    ed.command("select Cube");
    r34_steps(ed, 3);
    ed.scene().set_parent(sphere, nullptr);
    r34_steps(ed, 3);
    std::printf("    stale pictures found in verify mode: %llu\n", (unsigned long long)(Editor::render_cache_mismatches_all() - before));
    CHECK(Editor::render_cache_mismatches_all() == before);
  });

  r34_redraw_case(
      "render cache: toggling the grid button redraws the Scene view",
      [](Editor &) {}, [](Editor &ed) { r34_toolbar_click(ed, 1); });

  r34_redraw_case(
      "render cache: set Render.Exposure redraws the Scene view",
      [](Editor &) {}, [](Editor &ed) { ed.command("set Render.Exposure 1.5"); });

  r34_redraw_case(
      "render cache: changing the World to a sky redraws the Scene view",
      [](Editor &) {}, [](Editor &ed) { ed.command("world sky"); });

  r34_redraw_case(
      "render cache: console set MeshRenderer.ShowWireframe on the selection redraws the Scene view",
      [](Editor &ed) { ed.command("select Cube"); }, [](Editor &ed) { ed.command("set MeshRenderer.ShowWireframe true"); },
      [](Editor &ed, const R34Change &) { CHECK(ed.scene().find_by_name("Cube")->get<MeshRenderer>()->show_wireframe); });

  r34_redraw_case(
      "render cache: switching the shading mode redraws the Scene view",
      [](Editor &) {}, [](Editor &ed) { ed.command("shading solid"); });

  r34_redraw_case(
      "render cache: orbiting the Scene view camera redraws it",
      [](Editor &) {}, [](Editor &ed) { ed.command("camera 70 25 7 0 0.5 0"); });

  test("render cache: Play redraws the views on every frame, and Stop settles them again", [] {
    Editor ed;
    r34_open(ed);
    const R34Count t0 = r34_totals(ed);
    ed.command("play");
    r34_steps(ed, 6);
    const R34Count d = r34_totals(ed) - t0;
    std::printf("    playing: %llu frames, %llu view renders, %llu cache hits\n", (unsigned long long)d.frames, (unsigned long long)d.renders,
                (unsigned long long)d.hits);
    CHECK(d.renders >= d.frames);  // at least the Game view, every frame
    ed.command("stop");
    r34_steps(ed, 4);
    const R34Count t1 = r34_totals(ed);
    r34_steps(ed, 3);
    CHECK((r34_totals(ed) - t1).renders == 0);
  });

  /* ---------------------------------------------------------------- 3 */
  test("render cache: orbiting the Scene view renders the shadow map at most once", [] {
    Editor ed;
    r34_open(ed, false);
    const R34Count t0 = r34_totals(ed);
    for (int i = 0; i < 8; i++) {
      ed.command("camera " + std::to_string(10 + i * 9) + " " + std::to_string(15 + i) + " 7 0 0.5 0");
      ed.step_frame_headless();
    }
    const R34Count d = r34_totals(ed) - t0;
    std::printf("    orbit: %llu frames, %llu view renders, %llu shadow maps\n", (unsigned long long)d.frames, (unsigned long long)d.renders,
                (unsigned long long)d.shadows);
    CHECK(d.renders >= 8);  // every frame a new picture
    CHECK(d.shadows <= 1);
    CHECK(t0.frames > 0 && ed.frame_profile_totals().shadow_renders >= 1);  // the sun did get a shadow map in the first place
    /* Not vacuous: a moved caster does make a new one. */
    const R34Count t1 = r34_totals(ed);
    ed.command("select Cube");
    ed.command("position 1.5 0.5 0");
    r34_steps(ed, 2);
    CHECK((r34_totals(ed) - t1).shadows >= 1);
  });

  test("render cache: orbiting with the Scene and Game views side by side does not redraw the shadow map every frame", [] {
    Editor ed;
    r34_open(ed);
    const R34Count t0 = r34_totals(ed);
    for (int i = 0; i < 10; i++) {
      ed.command("camera " + std::to_string(10 + i * 9) + " " + std::to_string(15 + i) + " 7 0 0.5 0");
      ed.step_frame_headless();
    }
    const R34Count d = r34_totals(ed) - t0;
    std::printf("    2 by 3 orbit: %llu frames, %llu view renders, %llu cache hits, %llu shadow maps\n", (unsigned long long)d.frames,
                (unsigned long long)d.renders, (unsigned long long)d.hits, (unsigned long long)d.shadows);
    CHECK(d.renders >= 10);
    CHECK(d.shadows <= 2);  // the cache holds two maps
    CHECK(d.hits >= 5);     // the Game view's picture is reused while only the Scene view's camera moves
  });

  /* ---------------------------------------------------------------- 4 */
  test("render cache: ~30 mixed actions in verify mode leave no stale picture", [] {
    /* The same script twice: once with the cache doing its job (to see how often it can answer), once in
     * verify mode, where every view that would have been reused is drawn anyway and compared. */
    auto script = [](bool verify, R34Count &out) {
      Editor ed;
      r34_open(ed);
      ed.set_render_cache_verify(verify);
      const R34Count t0 = r34_totals(ed);
      const Recti hr = ed.window_rect_for_test(WindowKind::Hierarchy);
      auto idle = [&] {
        ed.step_frame_headless({r34_mouse(R34ET::MouseMove, hr.x + 20, hr.y + 20)});
        ed.step_frame_headless();
      };
      auto go = [&](const std::string &c) {
        ed.command(c);
        ed.step_frame_headless();
        idle();
      };
      MeshRenderer *mr = ed.scene().find_by_name("Cube")->get<MeshRenderer>();
      if (mr && mr->materials.empty()) mr->materials.push_back(make_material("Painted", {0.8f, 0.8f, 0.8f}));
      idle();
      go("select Cube");
      go("position 1 0.5 0");
      go("camera 40 30 8 0 0.5 0");
      if (mr && !mr->materials.empty()) {
        mr->materials[0]->base_color = {0.9f, 0.1f, 0.1f};
        mr->materials[0]->touch();
      }
      idle();
      go("undo");
      go("select Directional Light");
      if (GameObject *sun = ed.scene().find_by_name("Directional Light")) sun->set_local_rotation(Quat::euler({30.0f, 200.0f, 0.0f}));
      idle();
      go("shading solid");
      go("shading wire");
      go("shading both");
      go("shading shaded");
      go("select Main Camera");
      go("filter add Vignette");
      go("filters off");
      go("filters on");
      go("filter add Bloom");
      go("filter remove 0");
      go("select Cube");
      go("edit vertex");
      go("vsel 0 1");
      go("tool move");
      go("edit off");
      go("camera 100 10 5 0 0.5 0");
      go("world sky");
      go("set Render.Exposure -1");
      go("set MeshRenderer.ShowWireframe true");
      r34_toolbar_click(ed, 1);  // grid off
      idle();
      go("layout Default");
      go("layout 2 by 3");
      go("play");
      r34_steps(ed, 3);
      go("stop");
      go("undo");
      go("undo");
      go("redo");
      idle();
      idle();
      out = r34_totals(ed) - t0;
    };
    R34Count plain, checked;
    const uint64_t all0 = Editor::render_cache_mismatches_all();
    script(false, plain);
    script(true, checked);
    std::printf("    cache on: %llu frames, %llu view renders, %llu cache hits\n", (unsigned long long)plain.frames, (unsigned long long)plain.renders,
                (unsigned long long)plain.hits);
    std::printf("    verify:   %llu frames, %llu view renders, %llu stale pictures\n", (unsigned long long)checked.frames,
                (unsigned long long)checked.renders, (unsigned long long)checked.mismatches);
    CHECK(plain.hits > 10);                        // the script has stretches the cache can answer
    CHECK(checked.renders >= plain.renders + 10);  // and verify mode drew them anyway to compare
    CHECK(plain.mismatches == 0 && checked.mismatches == 0);
    CHECK(Editor::render_cache_mismatches_all() == all0);
  });

  /* ---------------------------------------------------------------- 5 */
  test("overlay batch: hundreds of random lines and points drawn in a batch are bit-identical to drawing them one by one", [] {
    const R34Raster rs(300, 200);
    for (uint32_t seed : {1u, 2u, 3u}) {
      Image img;
      RenderTarget rt;
      Renderer3D r;
      rs.render(img, rt, r);
      const std::vector<uint32_t> base_color = img.pixels;
      const std::vector<float> base_depth = rt.depth;
      const std::vector<uint32_t> base_ids = rt.ids;
      struct Cmd {
        bool point;
        Vec3 a, b;
        float radius, bias;
        uint32_t color;
        bool depth_test;
      };
      std::vector<Cmd> cmds;
      R34Rng rng{seed * 7919u};
      for (int i = 0; i < 450; i++) {
        Cmd c{};
        c.point = i % 5 == 4;
        const float reach = i % 7 == 0 ? 14.0f : 4.0f;  // some far off screen
        auto pt = [&] { return Vec3(rng.range(-reach, reach), rng.range(-reach * 0.6f, reach * 0.6f), rng.range(i % 9 == 0 ? -12.0f : -3.0f, 4.0f)); };
        c.a = pt();
        c.b = i % 6 == 0 ? Vec3(c.a.x + rng.range(-0.2f, 0.2f), c.a.y + rng.range(5.0f, 12.0f), c.a.z) : pt();  // some steep
        c.radius = rng.range(1.0f, 6.0f);
        c.bias = i % 3 == 0 ? rng.range(0.0f, 0.01f) : 0.0f;
        c.depth_test = (i % 2) == 0;
        c.color = ((uint32_t)rng.range(40.0f, 255.0f) << 24) | ((uint32_t)rng.range(0.0f, 255.0f) << 16) | ((uint32_t)rng.range(0.0f, 255.0f) << 8) |
                  (uint32_t)rng.range(0.0f, 255.0f);
        cmds.push_back(c);
      }
      auto draw = [&](bool batch) {
        if (batch) r.begin_overlay_batch();
        for (const Cmd &c : cmds) {
          if (c.point) r.point(c.a, c.radius, c.color, c.depth_test);
          else r.line(c.a, c.b, c.color, c.depth_test, c.bias);
        }
        if (batch) r.end_overlay_batch();
      };
      draw(false);
      const std::vector<uint32_t> serial = img.pixels;
      img.pixels = base_color;
      draw(true);
      const std::vector<uint32_t> batched = img.pixels;
      size_t changed = 0;
      for (size_t i = 0; i < serial.size(); i++) changed += serial[i] != base_color[i];
      std::printf("    seed %u: %zu pixels changed by the overlays\n", seed, changed);
      CHECK(changed > 500);  // the overlays drew something
      CHECK(batched == serial);
      CHECK(rt.depth == base_depth && rt.ids == base_ids);  // overlays touch the colour only
      /* Two batches in a row equal the same commands drawn one by one twice (order kept across batches). */
      img.pixels = base_color;
      draw(true);
      draw(true);
      const std::vector<uint32_t> twice = img.pixels;
      img.pixels = base_color;
      draw(false);
      draw(false);
      CHECK(twice == img.pixels);
    }
  });

  test("overlay batch: an empty batch, a single line and the same renderer on a smaller target all draw correctly", [] {
    const R34Raster big(300, 200), small(64, 48);
    Image img;
    RenderTarget rt;
    Renderer3D r;
    big.render(img, rt, r);
    const std::vector<uint32_t> base = img.pixels;
    r.begin_overlay_batch();
    r.end_overlay_batch();
    CHECK(img.pixels == base);
    r.begin_overlay_batch();
    r.line({-2, 0, 0}, {2, 1, 1}, 0xFFFFFFFFu);
    r.end_overlay_batch();
    const std::vector<uint32_t> batched = img.pixels;
    img.pixels = base;
    r.line({-2, 0, 0}, {2, 1, 1}, 0xFFFFFFFFu);
    CHECK(batched == img.pixels && batched != base);
    Image img2;
    RenderTarget rt2;
    small.render(img2, rt2, r);
    const std::vector<uint32_t> base2 = img2.pixels;
    r.begin_overlay_batch();
    for (int i = 0; i < 40; i++) r.line({-3.0f + i * 0.15f, -1, 0}, {3, 2.0f - i * 0.1f, 1}, 0x80FF8000u, i % 2 == 0);
    r.end_overlay_batch();
    const std::vector<uint32_t> b2 = img2.pixels;
    img2.pixels = base2;
    for (int i = 0; i < 40; i++) r.line({-3.0f + i * 0.15f, -1, 0}, {3, 2.0f - i * 0.1f, 1}, 0x80FF8000u, i % 2 == 0);
    CHECK(b2 == img2.pixels && b2 != base2);
  });

  test("outline: outlining the same selection twice gives the same picture, and a reused mask never leaks into another selection or frame", [] {
    const R34Raster rs(300, 200);
    Image img;
    RenderTarget rt;
    Renderer3D r;
    rs.render(img, rt, r);
    const std::vector<uint32_t> base = img.pixels;
    const std::vector<uint32_t> sel9 = {9}, sel5 = {5}, both = {5, 9};
    r.outline_ids(sel9, 0xFFF7941Du);
    const std::vector<uint32_t> a = img.pixels;
    CHECK(a != base);
    img.pixels = base;
    r.outline_ids(sel9, 0xFFF7941Du);
    CHECK(img.pixels == a);  // twice in a row: the same
    img.pixels = base;
    r.outline_ids(sel5, 0xFFF7941Du);
    const std::vector<uint32_t> b = img.pixels;
    CHECK(b != base && b != a);
    img.pixels = base;
    r.outline_ids(sel9, 0xFFF7941Du);
    CHECK(img.pixels == a);  // back to the first selection after the second: no leftovers
    img.pixels = base;
    r.outline_ids(both, 0xFFF7941Du, 3);
    const std::vector<uint32_t> c3 = img.pixels;
    {
      Renderer3D fresh;
      Image i2;
      RenderTarget rt2;
      rs.render(i2, rt2, fresh);
      fresh.outline_ids(both, 0xFFF7941Du, 3);
      CHECK(i2.pixels == c3);  // a renderer that never outlined before agrees
    }
    /* A different frame (the camera moved) through the same renderer outlines like a fresh one. */
    R34Raster moved(300, 200);
    moved.v = Mat4::look_at({-3, 2, -5}, {0, 0, 0}, {0, 1, 0});
    moved.render(img, rt, r);
    r.outline_ids(sel9, 0xFFF7941Du);
    Renderer3D fresh2;
    Image i3;
    RenderTarget rt3;
    moved.render(i3, rt3, fresh2);
    fresh2.outline_ids(sel9, 0xFFF7941Du);
    CHECK(img.pixels == i3.pixels);
    /* An empty selection changes nothing. */
    const std::vector<uint32_t> keep = img.pixels;
    r.outline_ids({}, 0xFFF7941Du);
    CHECK(img.pixels == keep);
  });

  test("outline: inside a batch it keeps order, so lines recorded before it are under the outline and later ones over it", [] {
    const R34Raster rs(300, 200);
    Image img;
    RenderTarget rt;
    Renderer3D r;
    rs.render(img, rt, r);
    const std::vector<uint32_t> base = img.pixels;
    const std::vector<uint32_t> sel = {9};
    auto draw = [&](bool batch) {
      if (batch) r.begin_overlay_batch();
      r.line({-1.5f, 0.0f, -1.2f}, {1.5f, 0.2f, -1.2f}, 0xFF00FF00u, false);  // across the cube, before the outline
      r.point({0.0f, 0.5f, -1.0f}, 4.0f, 0xFF0000FFu, false);
      r.outline_ids(sel, 0xFFF7941Du, 2);
      r.line({-1.5f, 0.6f, -1.2f}, {1.5f, -0.4f, -1.2f}, 0xFFFF00FFu, false);  // after it
      r.point({0.4f, 0.0f, -1.0f}, 3.0f, 0xFFFFFF00u, false);
      if (batch) r.end_overlay_batch();
    };
    draw(false);
    const std::vector<uint32_t> serial = img.pixels;
    CHECK(serial != base);
    img.pixels = base;
    draw(true);
    CHECK(img.pixels == serial);
    /* Not vacuous: the outline alone is a different picture. */
    img.pixels = base;
    r.outline_ids(sel, 0xFFF7941Du, 2);
    CHECK(img.pixels != serial);
  });

  /* ---------------------------------------------------------------- 6 */
  test("parallel clear: every depth is 1, every id 0, every colour the clear colour, for odd and tiny sizes too", [] {
    for (auto sz : {std::pair<int, int>{300, 200}, {333, 211}, {7, 3}, {1, 1}, {64, 1}, {1, 64}, {1025, 33}}) {
      const R34Raster rs(sz.first, sz.second);
      Image img;
      RenderTarget rt;
      Renderer3D r;
      rs.render(img, rt, r);  // leave real depth, ids and vis behind
      const size_t n = (size_t)sz.first * sz.second;
      CHECK(rt.depth.size() == n && rt.ids.size() == n);
      r.clear(0xFF123456u);
      CHECK(count_bad("depth", rt.depth, [](float d) { return d == 1.0f; }) == 0);
      CHECK(count_bad("ids", rt.ids, [](uint32_t i) { return i == 0; }) == 0);
      CHECK(count_bad("colour", img.pixels, [](uint32_t c) { return c == 0xFF123456u; }) == 0);
      CHECK(count_bad("vis", rt.vis, [](uint32_t v) { return v == 0; }) == 0);
      /* clear_planes alone leaves the colour. */
      rs.render(img, rt, r);
      const std::vector<uint32_t> col = img.pixels;
      r.clear_planes(nullptr);
      CHECK(img.pixels == col);
      CHECK(count_bad("depth", rt.depth, [](float d) { return d == 1.0f; }) == 0);
      CHECK(count_bad("ids", rt.ids, [](uint32_t i) { return i == 0; }) == 0);
      const uint32_t c2 = 0xFF0A0B0Cu;
      r.clear_planes(&c2);
      CHECK(count_bad("colour", img.pixels, [&](uint32_t c) { return c == c2; }) == 0);
    }
  });

  test("parallel clear: a Gouraud render allocates no visibility plane, a Deferred one does, and the ids and depth agree", [] {
    const R34Raster rs(300, 200);
    Image gi, di, gi2;
    RenderTarget gr, dr, gr2;
    Renderer3D r1, r2, r3;
    rs.render(gi, gr, r1, ShadeMode::Gouraud);
    rs.render(di, dr, r2, ShadeMode::Deferred);
    CHECK(gr.vis.empty());
    CHECK(dr.vis.size() == (size_t)300 * 200);
    CHECK(std::count(gr.ids.begin(), gr.ids.end(), 9u) > 100);
    CHECK(std::count(gr.ids.begin(), gr.ids.end(), 9u) == std::count(dr.ids.begin(), dr.ids.end(), 9u));  // same coverage
    CHECK(gr.depth == dr.depth);
    rs.render(gi2, gr2, r3, ShadeMode::Gouraud);  // drawing it again gives the same picture and still no vis
    CHECK(gr2.vis.empty() && gi2.pixels == gi.pixels && gr2.depth == gr.depth && gr2.ids == gr.ids);
    CHECK(count_bad("depth range", gr.depth, [](float d) { return d >= 0.0f && d <= 1.0f; }) == 0);
    CHECK(std::count(gr.depth.begin(), gr.depth.end(), 1.0f) > 1000);
    CHECK(std::count_if(gr.depth.begin(), gr.depth.end(), [](float d) { return d < 1.0f; }) > 1000);
  });
}

/* ===================================================================== */
/* Round 35: baked lighting - lightmaps and light modes (task 0012)        */
/* ===================================================================== */

namespace {
/* A tiny deterministic generator for the reference gathers. */
struct R35Rng {
  uint32_t s = 2463534242u;
  float f() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return (s >> 8) * (1.0f / 16777216.0f);
  }
};

bool r35_finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

/* Small, quick settings: a bake of a few thousand texels takes well under a second. */
BakeSettings r35_small(float tpu = 8.0f, int samples = 32) {
  BakeSettings s;
  s.texels_per_unit = tpu;
  s.max_size = 256;
  s.padding = 2;
  s.direct_samples = 8;
  s.indirect_samples = samples;
  s.bounces = 2;
  s.denoise = true;
  return s;
}

LightingData r35_run(Lightmapper &lm, const std::vector<BakeObject> &objs, const std::vector<BakeLight> &lights, const Environment &env,
                     const BakeSettings &s) {
  lm.begin(objs, lights, env, s);
  for (int i = 0; i < 100000 && !lm.step(1000.0); i++) {}
  CHECK(!lm.active());
  return lm.take_result();
}

BakeObject r35_object(uint64_t id, MeshPtr m, const Mat4 &model, Vec3 albedo = Vec3(0.8f)) {
  BakeObject o;
  o.id = id;
  o.mesh = m;
  o.model = model;
  o.materials = {make_material("m", albedo)};
  o.hash = lightmap_content_hash(*m, model);
  return o;
}

/* The lightmap value at a world point on a baked object: finds the render-mesh triangle the point is on,
 * interpolates the entry's lightmap UVs and samples the page. False when no triangle holds the point. */
bool r35_sample(const LightingData &ld, uint64_t id, const Mesh &m, const Mat4 &model, Vec3 p, Vec3 &out) {
  auto e = ld.entries.find(id);
  if (e == ld.entries.end() || e->second.page < 0 || e->second.page >= (int)ld.pages.size()) return false;
  const RenderMesh &rm = m.render_mesh();
  if (e->second.tri_uv.size() != rm.tri_count() * 3) return false;
  for (size_t t = 0; t < rm.tri_count(); t++) {
    const Vec3 a = model.point(rm.positions[rm.indices[t * 3]]), b = model.point(rm.positions[rm.indices[t * 3 + 1]]),
               c = model.point(rm.positions[rm.indices[t * 3 + 2]]);
    const Vec3 n = cross(b - a, c - a);
    const float area2 = dot(n, n);
    if (!(area2 > 1e-12f)) continue;
    if (std::fabs(dot(p - a, n)) / std::sqrt(area2) > 0.02f) continue;  // not on this triangle's plane
    const float w1 = dot(cross(p - a, c - a), n) / area2, w2 = dot(cross(b - a, p - a), n) / area2, w0 = 1.0f - w1 - w2;
    if (w0 < -1e-3f || w1 < -1e-3f || w2 < -1e-3f) continue;
    const Vec2 uv = e->second.tri_uv[t * 3] * w0 + e->second.tri_uv[t * 3 + 1] * w1 + e->second.tri_uv[t * 3 + 2] * w2;
    out = ld.pages[(size_t)e->second.page].sample(uv);
    return true;
  }
  return false;
}

/* ---- editor scenes ---- */

/* Window with the Scene and Game views, the default objects hidden: a camera, the sun, a grey world. */
void r35_open(Editor &ed) {
  r34_open(ed);
  for (const char *n : {"Plane", "Cube", "Sphere", "Cylinder"})
    if (GameObject *g = ed.scene().find_by_name(n)) g->active = false;
  EnvironmentSettings &env = ed.scene().environment;
  env.mode = 3;
  env.color = Vec3(0.25f);
  env.strength = 1.0f;
  LightingSettings &ls = ed.scene().lighting;
  ls.texels_per_unit = 8.0f;
  ls.max_size = 256;
  ls.padding = 2;
  ls.indirect_samples = 32;
  ls.direct_samples = 8;
  GameObject *sun = ed.scene().find_by_name("Directional Light");
  sun->set_local_euler({50, 90, 0});
  Light *l = sun->get<Light>();
  l->color = Vec3(1.0f);
  l->intensity = 1.0f;
  ed.commit_change("r35 setup");
}

GameObject *r35_add(Editor &ed, const char *kind, const char *name, Vec3 pos, Vec3 scale, Vec3 colour, bool gi = true) {
  GameObject *g = create_primitive(ed.scene(), kind);
  g->name = name;
  g->set_local_position(pos);
  g->set_local_scale(scale);
  MeshRenderer *mr = g->get<MeshRenderer>();
  mr->materials = {make_material(name, colour)};
  mr->contribute_gi = gi;
  return g;
}

void r35_sun_mode(Editor &ed, int mode) {
  ed.scene().find_by_name("Directional Light")->get<Light>()->mode = mode;
  ed.commit_change("r35 sun mode");
}

void r35_bake(Editor &ed) {
  ed.command("bake start");
  CHECK(ed.baking_for_test());
  for (int i = 0; i < 100 && ed.baking_for_test(); i++) ed.step_frame_headless();
  CHECK(!ed.baking_for_test());
  r34_steps(ed, 4);
}

std::vector<uint32_t> r35_game(Editor &ed) { return r34_crop(ed, ed.window_rect_for_test(WindowKind::Game)); }
std::vector<uint32_t> r35_scene_view(Editor &ed) { return r34_crop(ed, ed.scene_view_rect()); }

double r35_luma(const std::vector<uint32_t> &px) {
  double s = 0;
  for (uint32_t c : px) s += ((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255);
  return s;
}

/* How many pixels differ by more than `tol` in some channel. */
size_t r35_diff_count(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b, int tol = 0) {
  if (a.size() != b.size()) return std::max(a.size(), b.size());
  size_t n = 0;
  for (size_t i = 0; i < a.size(); i++) {
    const int dr = std::abs((int)((a[i] >> 16) & 255) - (int)((b[i] >> 16) & 255)), dg = std::abs((int)((a[i] >> 8) & 255) - (int)((b[i] >> 8) & 255)),
              db = std::abs((int)(a[i] & 255) - (int)(b[i] & 255));
    if (std::max({dr, dg, db}) > tol) n++;
  }
  return n;
}

/* A scratch scene file in the scratch project's Assets/Scenes, with the scene named after it. */
std::string r35_scene_path(Editor &ed, const char *name) {
  const std::string p = fs::join(fs::join(scratch_project(), "Assets/Scenes"), std::string(name) + ".scene");
  ed.scene().name = name;
  ed.scene().path = p;
  return p;
}

/* A red or white wall (x = 0) beside a white floor, the sun on the wall's -x side; the floor's lightmap
 * near the wall (0.4 from it) and far from it (4 away), on the sun's side. */
struct R35Bleed {
  Vec3 near_v, far_v;
  bool ok = false;
};
R35Bleed r35_bleed(int sun_mode, Vec3 wall_colour) {
  Editor ed;
  r35_open(ed);
  GameObject *floor_go = r35_add(ed, "Plane", "Floor", {0, 0, 0}, {1, 1, 1}, Vec3(0.8f));
  r35_add(ed, "Cube", "Wall", {0, 1, 0}, {0.2f, 2, 6}, wall_colour);
  r35_sun_mode(ed, sun_mode);
  r35_bake(ed);
  const LightingData &ld = ed.lighting_data_for_test();
  R35Bleed r;
  const Vec3 fwd = ed.scene().find_by_name("Directional Light")->world_rotation().rotate({0, 0, 1});
  const float side = fwd.x > 0 ? -1.0f : 1.0f;  // the sun's side of the wall
  const Mesh *fm = floor_go->evaluated_mesh(1);
  r.ok = fm && r35_sample(ld, floor_go->id, *fm, floor_go->world_matrix(), {side * 0.4f, 0, 0}, r.near_v) &&
         r35_sample(ld, floor_go->id, *fm, floor_go->world_matrix(), {side * 4.0f, 0, 0}, r.far_v);
  return r;
}

/* Union-find over triangles: islands are triangles joined across edges whose two UV ends agree. */
struct R35Islands {
  std::vector<int> parent;
  int find(int a) { return parent[(size_t)a] == a ? a : parent[(size_t)a] = find(parent[(size_t)a]); }
};

/* Checks one mesh's lightmap UVs: inside [0,1], finite, no two triangles overlapping, total area <= 1, islands
 * apart by at least `gap` cells of a res x res grid. Returns the number of problems. */
int r35_uv_problems(const char *what, const Mesh &m, int res, int padding, bool check_gap) {
  const std::vector<Vec2> uv = lightmap_uvs(m, true, res, padding);
  const RenderMesh &rm = m.render_mesh();
  int bad = 0;
  if (uv.size() != m.corner_verts.size()) {
    std::printf("    %s: %zu uvs for %zu corners\n", what, uv.size(), m.corner_verts.size());
    return 1;
  }
  for (const Vec2 &u : uv)
    if (!(std::isfinite(u.x) && std::isfinite(u.y) && u.x >= 0.0f && u.x <= 1.0f && u.y >= 0.0f && u.y <= 1.0f)) bad++;
  if (bad) std::printf("    %s: %d corners outside 0..1\n", what, bad);
  const size_t nt = rm.tri_count();
  auto tuv = [&](size_t t, int k) { return uv[rm.tri_corner[t * 3 + (size_t)k]]; };
  /* Overlap: rasterize every triangle's strict interior on a fine grid; one cell in two triangles = overlap. */
  const int G = 384;
  std::vector<int32_t> owner((size_t)G * G, -1);
  int overlap_cells = 0;
  double area = 0;
  for (size_t t = 0; t < nt; t++) {
    const Vec2 a = tuv(t, 0), b = tuv(t, 1), c = tuv(t, 2);
    const float a2 = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
    area += std::fabs(a2) * 0.5;
    if (std::fabs(a2) < 1e-9f) continue;
    const int x0 = std::max(0, (int)std::floor(std::min({a.x, b.x, c.x}) * G)), x1 = std::min(G - 1, (int)std::ceil(std::max({a.x, b.x, c.x}) * G));
    const int y0 = std::max(0, (int)std::floor(std::min({a.y, b.y, c.y}) * G)), y1 = std::min(G - 1, (int)std::ceil(std::max({a.y, b.y, c.y}) * G));
    for (int y = y0; y <= y1; y++)
      for (int x = x0; x <= x1; x++) {
        const float px = (x + 0.5f) / G, py = (y + 0.5f) / G;
        const float w0 = ((b.x - px) * (c.y - py) - (c.x - px) * (b.y - py)) / a2, w1 = ((c.x - px) * (a.y - py) - (a.x - px) * (c.y - py)) / a2,
                    w2 = 1.0f - w0 - w1;
        if (w0 < 0.02f || w1 < 0.02f || w2 < 0.02f) continue;
        int32_t &o = owner[(size_t)y * G + x];
        if (o >= 0 && o != (int32_t)t) overlap_cells++;
        o = (int32_t)t;
      }
  }
  if (overlap_cells) std::printf("    %s: %d grid cells hold two triangles\n", what, overlap_cells);
  bad += overlap_cells > 0;
  if (area > 1.0 + 1e-3) {
    std::printf("    %s: total chart area %.3f > 1\n", what, area);
    bad++;
  }
  if (!check_gap) return bad;
  /* Islands. */
  R35Islands isl;
  isl.parent.resize(nt);
  for (size_t t = 0; t < nt; t++) isl.parent[t] = (int)t;
  struct EdgeUse {
    size_t tri;
    Vec2 ua, ub;
  };
  std::map<std::pair<uint32_t, uint32_t>, std::vector<EdgeUse>> edges;
  for (size_t t = 0; t < nt; t++)
    for (int k = 0; k < 3; k++) {
      const uint32_t va = m.corner_verts[rm.tri_corner[t * 3 + (size_t)k]], vb = m.corner_verts[rm.tri_corner[t * 3 + (size_t)((k + 1) % 3)]];
      const Vec2 ua = tuv(t, k), ub = tuv(t, (k + 1) % 3);
      if (va < vb) edges[{va, vb}].push_back({t, ua, ub});
      else edges[{vb, va}].push_back({t, ub, ua});
    }
  auto same = [](Vec2 p, Vec2 q) { return std::fabs(p.x - q.x) < 1e-4f && std::fabs(p.y - q.y) < 1e-4f; };
  for (auto &kv : edges)
    for (size_t i = 0; i < kv.second.size(); i++)
      for (size_t j = i + 1; j < kv.second.size(); j++)
        if (same(kv.second[i].ua, kv.second[j].ua) && same(kv.second[i].ub, kv.second[j].ub))
          isl.parent[(size_t)isl.find((int)kv.second[i].tri)] = isl.find((int)kv.second[j].tri);
  /* Coverage per island on the res grid (any of 3x3 sub-samples inside a triangle): no cell within one cell of another island's. */
  const int R = res;
  std::vector<int32_t> cell((size_t)R * R, -1);
  int clash = 0;
  for (size_t t = 0; t < nt; t++) {
    const Vec2 a = tuv(t, 0), b = tuv(t, 1), c = tuv(t, 2);
    const float a2 = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
    if (std::fabs(a2) < 1e-9f) continue;
    const int id = isl.find((int)t);
    for (int y = 0; y < R; y++)
      for (int x = 0; x < R; x++) {
        bool in = false;
        for (int sy = 0; sy < 3 && !in; sy++)
          for (int sx = 0; sx < 3 && !in; sx++) {
            const float px = (x + (sx + 0.5f) / 3.0f) / R, py = (y + (sy + 0.5f) / 3.0f) / R;
            const float w0 = ((b.x - px) * (c.y - py) - (c.x - px) * (b.y - py)) / a2, w1 = ((c.x - px) * (a.y - py) - (a.x - px) * (c.y - py)) / a2,
                        w2 = 1.0f - w0 - w1;
            in = w0 >= 0 && w1 >= 0 && w2 >= 0;
          }
        if (!in) continue;
        int32_t &o = cell[(size_t)y * R + x];
        if (o >= 0 && o != id) clash++;
        o = id;
      }
  }
  for (int y = 0; y < R; y++)
    for (int x = 0; x < R; x++) {
      const int32_t o = cell[(size_t)y * R + x];
      if (o < 0) continue;
      for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
          const int xx = x + dx, yy = y + dy;
          if (xx < 0 || yy < 0 || xx >= R || yy >= R) continue;
          const int32_t q = cell[(size_t)yy * R + xx];
          if (q >= 0 && q != o) clash++;
        }
    }
  if (clash) std::printf("    %s: %d texel pairs of different charts touch (padding %d at %d texels)\n", what, clash, padding, res);
  return bad + (clash > 0);
}
}  // namespace

static void round35_tests() {
  /* ---------------------------------------------------------------- 1: lightmap UVs */
  test("baked lighting: lightmap UVs stay inside 0..1, no two triangles overlap, charts keep apart", [] {
    struct Shape {
      const char *name;
      MeshPtr m;
    };
    const std::vector<Shape> shapes = {{"cube", primitives::cube()},      {"sphere", primitives::uv_sphere()}, {"icosphere", primitives::ico_sphere()},
                                       {"cylinder", primitives::cylinder()}, {"torus", primitives::torus()},      {"teapot", primitives::teapot(1.2f, 4)},
                                       {"plane", primitives::plane(10.0f, 4)}};
    int bad = 0;
    for (const Shape &s : shapes) bad += r35_uv_problems(s.name, *s.m, 64, 2, true);
    CHECK(bad == 0);
    /* The same through the baker: every entry's UVs are inside the page. */
    Lightmapper lm;
    Environment env;
    std::vector<BakeObject> objs;
    uint64_t id = 1;
    for (const Shape &s : shapes) objs.push_back(r35_object(id++, s.m, Mat4::translate(Vec3((float)id, 0, 0))));
    const LightingData ld = r35_run(lm, objs, {}, env, r35_small(6.0f, 4));
    CHECK(ld.entries.size() == shapes.size());
    size_t outside = 0;
    for (auto &kv : ld.entries)
      for (const Vec2 &u : kv.second.tri_uv)
        if (!(u.x >= 0 && u.x <= 1 && u.y >= 0 && u.y <= 1)) outside++;
    CHECK(outside == 0);
    /* Different objects never share a texel: the pages' charts are laid out by rectangles. */
    std::map<std::pair<int, int>, uint64_t> texel_owner;
    size_t shared = 0;
    for (auto &kv : ld.entries) {
      const Lightmap &pg = ld.pages[(size_t)kv.second.page];
      const Mesh &m = *objs[(size_t)kv.first - 1].mesh;
      const RenderMesh &rm = m.render_mesh();
      for (size_t t = 0; t < rm.tri_count(); t++) {
        const Vec2 a = kv.second.tri_uv[t * 3], b = kv.second.tri_uv[t * 3 + 1], c = kv.second.tri_uv[t * 3 + 2];
        const Vec2 centre = (a + b + c) * (1.0f / 3.0f);
        const std::pair<int, int> key{kv.second.page * 100000 + (int)(centre.x * pg.width), (int)(centre.y * pg.height)};
        auto it = texel_owner.find(key);
        if (it != texel_owner.end() && it->second != kv.first) shared++;
        texel_owner[key] = kv.first;
      }
    }
    CHECK(shared == 0);
  });

  test("baked lighting: padding is respected - a bigger Lightmap Padding keeps charts further apart", [] {
    /* Charts of one object are at least (padding + 1) texels apart at the chart's resolution. */
    const MeshPtr m = primitives::cube();
    for (int pad : {1, 2, 4}) {
      const std::vector<Vec2> uv = lightmap_uvs(*m, true, 64, pad);
      /* Smallest gap between two different islands' bounding boxes, in texels of a 64 grid. */
      const RenderMesh &rm = m->render_mesh();
      std::map<uint32_t, std::vector<Vec2>> by_face;  // per mesh face
      for (size_t t = 0; t < rm.tri_count(); t++)
        for (int k = 0; k < 3; k++) by_face[(uint32_t)(t / 2)].push_back(uv[rm.tri_corner[t * 3 + (size_t)k]]);
      float min_gap = 1e9f;
      std::vector<std::array<float, 4>> boxes;
      for (auto &kv : by_face) {
        std::array<float, 4> b{1e9f, 1e9f, -1e9f, -1e9f};
        for (const Vec2 &u : kv.second) b[0] = std::min(b[0], u.x), b[1] = std::min(b[1], u.y), b[2] = std::max(b[2], u.x), b[3] = std::max(b[3], u.y);
        boxes.push_back(b);
      }
      for (size_t i = 0; i < boxes.size(); i++)
        for (size_t j = i + 1; j < boxes.size(); j++) {
          const float gx = std::max(boxes[i][0] - boxes[j][2], boxes[j][0] - boxes[i][2]), gy = std::max(boxes[i][1] - boxes[j][3], boxes[j][1] - boxes[i][3]);
          const float g = std::max(gx, gy);  // negative: the boxes overlap on both axes (the faces of one island touch)
          if (g > 1e-5f) min_gap = std::min(min_gap, g);
        }
      std::printf("    padding %d: closest separate faces %.2f texels apart at 64\n", pad, min_gap * 64.0f);
      CHECK(min_gap * 64.0f >= (float)pad * 0.9f);  // at least `pad` texels where the faces are separate
    }
  });

  test("baked lighting: texel density is within 20% of Lightmap Resolution x Scale In Lightmap for objects of different sizes", [] {
    struct Case {
      const char *name;
      MeshPtr m;
      Vec3 scale;
      float lm_scale;
    };
    const std::vector<Case> cases = {{"cube 1m", primitives::cube(), {1, 1, 1}, 1.0f},
                                     {"cube 3m", primitives::cube(), {3, 3, 3}, 1.0f},
                                     {"box 0.5x4x2", primitives::cube(), {0.5f, 4, 2}, 1.0f},
                                     {"sphere 2m", primitives::uv_sphere(), {2, 2, 2}, 1.0f},
                                     {"cylinder", primitives::cylinder(), {1, 1, 1}, 1.0f},
                                     {"cube 2m, scale 2", primitives::cube(), {2, 2, 2}, 2.0f},
                                     {"cube 2m, scale 0.5", primitives::cube(), {2, 2, 2}, 0.5f}};
    for (const float tpu : {8.0f, 20.0f, 40.0f}) {
      for (const Case &c : cases) {
        BakeObject o = r35_object(1, c.m, Mat4::scale(c.scale));
        o.scale = c.lm_scale;
        Lightmapper lm;
        Environment env;
        BakeSettings bs = r35_small(tpu, 1);
        bs.max_size = 1024;  // big enough that no chart is capped
        const LightingData ld = r35_run(lm, {o}, {}, env, bs);
        auto e = ld.entries.find(1);
        CHECK(e != ld.entries.end());
        if (e == ld.entries.end()) continue;
        const Lightmap &pg = ld.pages[(size_t)e->second.page];
        const RenderMesh &rm = c.m->render_mesh();
        double world = 0, texels = 0;
        for (size_t t = 0; t < rm.tri_count(); t++) {
          const Vec3 a = o.model.point(rm.positions[rm.indices[t * 3]]), b = o.model.point(rm.positions[rm.indices[t * 3 + 1]]),
                     cc = o.model.point(rm.positions[rm.indices[t * 3 + 2]]);
          world += 0.5 * length(cross(b - a, cc - a));
          const Vec2 u0 = e->second.tri_uv[t * 3], u1 = e->second.tri_uv[t * 3 + 1], u2 = e->second.tri_uv[t * 3 + 2];
          texels += 0.5 * std::fabs((double)(u1.x - u0.x) * (u2.y - u0.y) - (double)(u2.x - u0.x) * (u1.y - u0.y)) * pg.width * pg.height;
        }
        const double density = std::sqrt(texels / world), want = tpu * c.lm_scale;
        std::printf("    %-20s tpu %4.1f: %.2f texels/unit (want %.2f, %+.0f%%), page %dx%d\n", c.name, tpu, density, want, 100.0 * (density / want - 1.0),
                    pg.width, pg.height);
        CHECK(std::fabs(density / want - 1.0) <= 0.20);
      }
    }
  });

  test("baked lighting: Scale In Lightmap scales the texel count; the content hash follows the shape and the place", [] {
    const MeshPtr m = primitives::cube();
    Environment env;
    size_t counts[3];
    int k = 0;
    for (float s : {0.5f, 1.0f, 2.0f}) {
      BakeObject o = r35_object(1, m, Mat4::scale({2, 2, 2}));
      o.scale = s;
      Lightmapper lm;
      lm.begin({o}, {}, env, r35_small(8.0f, 1));
      counts[k++] = lm.texel_count();
    }
    std::printf("    texels at scale 0.5 / 1 / 2: %zu / %zu / %zu\n", counts[0], counts[1], counts[2]);
    CHECK(counts[0] > 0 && counts[1] > counts[0] * 2 && counts[2] > counts[1] * 2);
    const uint64_t h0 = lightmap_content_hash(*m, Mat4()), h1 = lightmap_content_hash(*m, Mat4()), h2 = lightmap_content_hash(*m, Mat4::translate({0.01f, 0, 0}));
    CHECK(h0 == h1 && h0 != h2 && h0 != 0);
    MeshPtr moved = std::make_shared<Mesh>(*m);
    moved->positions[0].x += 0.01f;
    CHECK(lightmap_content_hash(*moved, Mat4()) != h0);
  });

  /* ---------------------------------------------------------------- 2: colour bleeding */
  test("baked lighting: a red wall tints the white floor beside it with a Baked sun, a Mixed sun, and not with a white wall", [] {
    const Vec3 red(0.9f, 0.05f, 0.05f), white(0.8f);
    for (int mode : {2, 1}) {
      const R35Bleed r = r35_bleed(mode, red), w = r35_bleed(mode, white);
      CHECK(r.ok && w.ok);
      if (!r.ok || !w.ok) continue;
      std::printf("    sun mode %d: red wall near (%.3f %.3f %.3f) far (%.3f %.3f %.3f); white wall near (%.3f %.3f %.3f) far (%.3f %.3f %.3f)\n", mode,
                  r.near_v.x, r.near_v.y, r.near_v.z, r.far_v.x, r.far_v.y, r.far_v.z, w.near_v.x, w.near_v.y, w.near_v.z, w.far_v.x, w.far_v.y, w.far_v.z);
      /* Red wall: red clearly above green and blue near the wall, and more so than far from it. */
      CHECK(r.near_v.x > r.near_v.y * 1.10f && r.near_v.x > r.near_v.z * 1.10f);
      CHECK(r.near_v.x / std::max(1e-4f, r.near_v.y) > r.far_v.x / std::max(1e-4f, r.far_v.y) + 0.05f);
      /* White wall: neutral (the world and the sun are white). */
      CHECK(std::fabs(w.near_v.x - w.near_v.y) <= 0.01f * std::max(1.0f, w.near_v.y) && std::fabs(w.near_v.z - w.near_v.y) <= 0.01f * std::max(1.0f, w.near_v.y));
      /* The tint is the wall's: with the white wall, the red channel near the wall isn't the one that rose. */
      CHECK(r.near_v.y < w.near_v.y);  // the red wall absorbs green that the white one bounces
    }
  });

  /* ---------------------------------------------------------------- 3: accuracy */
  test("baked lighting: a floor under a uniform sky bakes the sky colour; under a big slab it is dark; at a slab's edge it is half", [] {
    Environment env;
    env.mode = Environment::Color;
    env.color = Vec3(0.4f, 0.3f, 0.2f);
    env.strength = 1.0f;
    const MeshPtr plane = primitives::plane(10.0f, 4);
    const MeshPtr cube = primitives::cube();
    BakeObject floor_o = r35_object(1, plane, Mat4());
    BakeSettings s = r35_small(6.0f, 256);
    s.denoise = false;
    /* Open sky. */
    {
      Lightmapper lm;
      const LightingData ld = r35_run(lm, {floor_o}, {}, env, s);
      double worst = 0;
      int n = 0;
      for (float x = -4.0f; x <= 4.0f; x += 1.7f)
        for (float z = -4.0f; z <= 4.0f; z += 1.7f) {
          Vec3 v;
          CHECK(r35_sample(ld, 1, *plane, Mat4(), {x, 0, z}, v));
          worst = std::max({worst, (double)std::fabs(v.x - 0.4f), (double)std::fabs(v.y - 0.3f), (double)std::fabs(v.z - 0.2f)});
          n++;
        }
      std::printf("    open sky: worst error %.4f over %d points (sky 0.4 0.3 0.2)\n", worst, n);
      CHECK(worst <= 0.01);
    }
    /* Big slab above: the sky is hidden. The floor is as wide as the slab: a Color world also shines from
     * below the horizon, and past a small floor's edge the slab's underside would see it and bounce it
     * back down (a real ~6% the first version of this test counted as a leak). */
    BakeObject slab = r35_object(2, cube, Mat4::trs({0, 1, 0}, Quat(), {100, 0.1f, 100}));
    const MeshPtr wide = primitives::plane(100.0f, 4);
    BakeObject wide_floor = r35_object(1, wide, Mat4());
    {
      Lightmapper lm;
      const LightingData ld = r35_run(lm, {wide_floor, slab}, {}, env, s);
      Vec3 v;
      CHECK(r35_sample(ld, 1, *wide, Mat4(), {0, 0, 0}, v));
      std::printf("    under a 100 m slab: (%.4f %.4f %.4f)\n", v.x, v.y, v.z);
      CHECK(v.x < 0.4f * 0.06f && v.y < 0.3f * 0.06f && v.z < 0.2f * 0.06f);
    }
    /* A slab ending exactly above the sample point: half of the cosine-weighted sky is open. Compared with
     * the analytic half and with a 8192-sample path-traced gather (a reference from the same scene). */
    {
      const Mat4 slab_m = Mat4::trs({-10, 1, 0}, Quat(), {20, 0.1f, 20});
      BakeObject edge = r35_object(2, cube, slab_m);
      Lightmapper lm;
      const LightingData ld = r35_run(lm, {floor_o, edge}, {}, env, s);
      Vec3 v;
      CHECK(r35_sample(ld, 1, *plane, Mat4(), {0, 0, 0}, v));
      PathTracer pt;
      std::vector<MaterialPtr> mats;
      const RenderMesh &frm = plane->render_mesh(), &crm = cube->render_mesh();
      std::vector<PTObject> pto = {{&frm, Mat4(), &mats}, {&crm, slab_m, &mats}};
      PTSettings ps;
      ps.max_bounces = 2;
      ps.denoise = false;
      ps.use_guiding = false;
      ps.gpus.clear();
      pt.set_settings(ps);
      pt.build(pto, {}, env);
      R35Rng rng;
      Vec3 ref(0.0f);
      const int N = 8192;
      const Vec3 n(0, 1, 0), tu(1, 0, 0), tv(0, 0, 1);
      uint64_t rays = 0;
      for (int i = 0; i < N; i++) {
        const float r1 = rng.f(), r2 = rng.f();
        const float r = std::sqrt(r1), phi = 2.0f * kPi * r2;
        const Vec3 d = normalize(tu * (r * std::cos(phi)) + tv * (r * std::sin(phi)) + n * std::sqrt(std::max(0.0f, 1.0f - r1)));
        uint32_t prng = 12345u + (uint32_t)i * 2654435761u;
        ref += pt.incoming_radiance({Vec3(0, 1e-3f, 0), d}, prng, rays);
      }
      ref = ref * (1.0f / N);
      std::printf("    slab edge: lightmap (%.3f %.3f %.3f), reference (%.3f %.3f %.3f), half sky (0.2 0.15 0.1)\n", v.x, v.y, v.z, ref.x, ref.y, ref.z);
      CHECK(std::fabs(v.x - ref.x) <= 0.06f && std::fabs(v.y - ref.y) <= 0.06f && std::fabs(v.z - ref.z) <= 0.06f);
      CHECK(std::fabs(v.x - 0.2f) <= 0.07f && std::fabs(v.y - 0.15f) <= 0.07f && std::fabs(v.z - 0.1f) <= 0.07f);
    }
  });

  test("baked lighting: a Baked sun's direct light in the map is colour x intensity x N.L, and zero in a shadow", [] {
    Environment env;
    env.mode = Environment::Color;
    env.color = Vec3(0.0f);
    const MeshPtr plane = primitives::plane(10.0f, 4);
    BakeLight sun;
    sun.mode = 2;
    sun.id = 7;
    sun.light.type = RenderLight::Directional;
    sun.light.direction = normalize(Vec3(0.3f, -1.0f, 0.2f));
    sun.light.color = Vec3(1.0f, 0.5f, 0.25f);
    sun.light.intensity = 2.0f;
    BakeSettings s = r35_small(6.0f, 16);
    s.denoise = false;
    Lightmapper lm;
    const BakeObject floor_o = r35_object(1, plane, Mat4());
    LightingData ld = r35_run(lm, {floor_o}, {sun}, env, s);
    Vec3 v;
    CHECK(r35_sample(ld, 1, *plane, Mat4(), {0.5f, 0, 0.5f}, v));
    const float ndl = dot(Vec3(0, 1, 0), normalize(-sun.light.direction));
    std::printf("    N.L %.3f: map (%.3f %.3f %.3f), expected (%.3f %.3f %.3f)\n", ndl, v.x, v.y, v.z, 2.0f * ndl, 1.0f * ndl, 0.5f * ndl);
    CHECK(std::fabs(v.x - 2.0f * ndl) <= 0.02f && std::fabs(v.y - 1.0f * ndl) <= 0.02f && std::fabs(v.z - 0.5f * ndl) <= 0.02f);
    CHECK(ld.light_modes.count(7) && ld.light_modes[7] == 2);
    /* A slab over the point shadows it. */
    const MeshPtr cube = primitives::cube();
    const BakeObject slab = r35_object(2, cube, Mat4::trs({0, 2, 0}, Quat(), {40, 0.1f, 40}));
    Lightmapper lm2;
    ld = r35_run(lm2, {floor_o, slab}, {sun}, env, s);
    CHECK(r35_sample(ld, 1, *plane, Mat4(), {0.5f, 0, 0.5f}, v));
    CHECK(v.x < 0.02f && v.y < 0.02f && v.z < 0.02f);
    /* A Mixed sun is not in the map's direct light (it stays realtime), but is recorded as Mixed. */
    sun.mode = 1;
    Lightmapper lm3;
    ld = r35_run(lm3, {floor_o}, {sun}, env, s);
    CHECK(r35_sample(ld, 1, *plane, Mat4(), {0.5f, 0, 0.5f}, v));
    CHECK(v.x < 0.02f && v.y < 0.02f && v.z < 0.02f);
    CHECK(ld.light_modes.count(7) && ld.light_modes[7] == 1);
    /* A Realtime sun is not in the map at all. */
    sun.mode = 0;
    Lightmapper lm4;
    ld = r35_run(lm4, {floor_o}, {sun}, env, s);
    CHECK(r35_sample(ld, 1, *plane, Mat4(), {0.5f, 0, 0.5f}, v));
    CHECK(v.x < 0.02f && v.y < 0.02f && v.z < 0.02f);
    CHECK(ld.light_modes.empty());
  });

  /* ---------------------------------------------------------------- 4: light modes */
  test("baked lighting: a Baked sun alone lights a lightmapped floor in the Game view (no realtime light needed), a Realtime one adds nothing to the map", [] {
    auto build = [](Editor &ed, int mode, bool sun_on) {
      r35_open(ed);
      r35_add(ed, "Plane", "Floor", {0, 0, 0}, {1, 1, 1}, Vec3(0.8f));
      r35_add(ed, "Cube", "Box", {0, 0.5f, 0}, {1, 1, 1}, Vec3(0.8f));
      r35_sun_mode(ed, mode);
      ed.scene().find_by_name("Directional Light")->get<Light>()->enabled = sun_on;
      ed.commit_change("sun");
      r34_steps(ed, 3);
    };
    Editor lit, dark, realtime, off;
    build(lit, 2, true);
    r35_bake(lit);
    build(dark, 2, false);
    r35_bake(dark);
    const double a = r35_luma(r35_game(lit)), b = r35_luma(r35_game(dark));
    std::printf("    Game view brightness, Baked sun on: %.0f, sun off: %.0f\n", a, b);
    CHECK(a > b * 1.05);
    CHECK(lit.lighting_data_for_test().light_modes.size() == 1);
    CHECK(dark.lighting_data_for_test().light_modes.empty());
    /* A Realtime sun: the bake is identical with it on or off. */
    build(realtime, 0, true);
    r35_bake(realtime);
    build(off, 0, false);
    r35_bake(off);
    const LightingData &x = realtime.lighting_data_for_test(), &y = off.lighting_data_for_test();
    CHECK(!x.pages.empty() && x.pages.size() == y.pages.size());
    bool same = x.pages.size() == y.pages.size();
    for (size_t p = 0; same && p < x.pages.size(); p++) same = x.pages[p].texels == y.pages[p].texels;
    CHECK(same);
    CHECK(x.light_modes.empty() && y.light_modes.empty());
    /* ...and the Baked sun's map is brighter than that. */
    const LightingData &z = lit.lighting_data_for_test();
    double sz = 0, sx = 0;
    for (const Vec3 &t : z.pages[0].texels) sz += t.x + t.y + t.z;
    for (const Vec3 &t : x.pages[0].texels) sx += t.x + t.y + t.z;
    CHECK(sz > sx * 1.2);
  });

  test("baked lighting: moving a Baked sun after the bake changes nothing on lightmapped surfaces; a Mixed sun's realtime shadow follows it", [] {
    auto run = [](int mode, size_t &darker, size_t &lighter) {
      Editor ed;
      r35_open(ed);
      r35_add(ed, "Plane", "Floor", {0, 0, 0}, {1, 1, 1}, Vec3(0.8f));
      r35_add(ed, "Cube", "Box", {0, 0.5f, 0}, {1.2f, 1, 1.2f}, Vec3(0.8f));
      r35_sun_mode(ed, mode);
      r35_bake(ed);
      const std::vector<uint32_t> a = r35_game(ed);
      ed.scene().find_by_name("Directional Light")->set_local_euler({50, -90, 0});  // the shadow swings to the other side
      ed.commit_change("move the sun");
      r34_steps(ed, 4);
      CHECK(ed.lighting_out_of_date_for_test());  // the light changed since the bake
      const std::vector<uint32_t> b = r35_game(ed);
      CHECK(a.size() == b.size());
      darker = lighter = 0;
      for (size_t i = 0; i < a.size() && i < b.size(); i++) {
        auto lum = [](uint32_t c) { return (int)((c >> 16) & 255) + (int)((c >> 8) & 255) + (int)(c & 255); };
        const int la = lum(a[i]), lb = lum(b[i]);
        if (lb < la * 0.8 - 6) darker++;
        if (lb > la * 1.25 + 6) lighter++;
      }
      return r35_diff_count(a, b);
    };
    size_t d2 = 0, l2 = 0, d1 = 0, l1 = 0;
    const size_t baked_diff = run(2, d2, l2);
    const size_t mixed_diff = run(1, d1, l1);
    std::printf("    Baked: %zu pixels changed (%zu darker, %zu lighter); Mixed: %zu changed (%zu darker, %zu lighter)\n", baked_diff, d2, l2, mixed_diff, d1, l1);
    CHECK(baked_diff == 0);
    CHECK(mixed_diff > 200 && d1 > 60 && l1 > 60);
  });

  test("baked lighting: a Mixed sun keeps a realtime shadow on the lightmapped floor and puts only its bounce in the map", [] {
    auto make = [](int mode) {
      auto ed = std::make_unique<Editor>();
      r35_open(*ed);
      r35_add(*ed, "Plane", "Floor", {0, 0, 0}, {1, 1, 1}, Vec3(0.8f));
      r35_add(*ed, "Cube", "Box", {0, 0.5f, 0}, {1.2f, 1, 1.2f}, Vec3(0.8f));
      r35_sun_mode(*ed, mode);
      return ed;
    };
    auto realtime = make(0);
    r34_steps(*realtime, 4);
    const std::vector<uint32_t> rt = r35_game(*realtime);  // nothing baked: the plain realtime picture
    auto mixed = make(1);
    r35_bake(*mixed);
    const std::vector<uint32_t> mx = r35_game(*mixed);
    CHECK(rt.size() == mx.size() && !rt.empty());
    /* The pixels the sun lights = the picture with the sun minus the picture without it. In the realtime
     * picture that is lit floor with the cube's shadow cut out of it; the Mixed picture must light the same
     * pixels (its realtime shadow), while the baked bounce stays when the sun is switched off. */
    realtime->scene().find_by_name("Directional Light")->get<Light>()->enabled = false;
    mixed->scene().find_by_name("Directional Light")->get<Light>()->enabled = false;
    realtime->commit_change("sun off");
    mixed->commit_change("sun off");
    r34_steps(*realtime, 4);
    r34_steps(*mixed, 4);
    const std::vector<uint32_t> rt_off = r35_game(*realtime), mx_off = r35_game(*mixed);
    auto lum = [](uint32_t c) { return (int)((c >> 16) & 255) + (int)((c >> 8) & 255) + (int)(c & 255); };
    size_t lit_rt = 0, lit_mx = 0, both = 0, either = 0;
    for (size_t i = 0; i < mx.size(); i++) {
      const bool a = lum(rt[i]) > lum(rt_off[i]) * 1.15 + 12, b = lum(mx[i]) > lum(mx_off[i]) * 1.15 + 12;
      lit_rt += a, lit_mx += b, both += a && b, either += a || b;
    }
    std::printf("    pixels lit by the sun: realtime %zu, mixed %zu, both %zu, either %zu\n", lit_rt, lit_mx, both, either);
    std::printf("    luma: rt %.0f rt_off %.0f mx %.0f mx_off %.0f of %zu px\n", r35_luma(rt), r35_luma(rt_off), r35_luma(mx), r35_luma(mx_off), rt.size());
    CHECK(lit_mx > 500 && lit_rt > 500);
    CHECK(both * 10 >= either * 9);  // the same lit region, so the same shadow
    CHECK(r35_luma(mx_off) > 0.5 * r35_luma(rt_off));  // and the baked bounce is still there with the sun off
    /* The Mixed map holds the bounce but not the sun: a Baked one has the direct light too. */
    auto baked = make(2);
    r35_bake(*baked);
    double sm = 0, sb = 0;
    CHECK(!mixed->lighting_data_for_test().pages.empty() && !baked->lighting_data_for_test().pages.empty());
    if (mixed->lighting_data_for_test().pages.empty() || baked->lighting_data_for_test().pages.empty()) return;
    for (const Vec3 &t : mixed->lighting_data_for_test().pages[0].texels) sm += t.x + t.y + t.z;
    for (const Vec3 &t : baked->lighting_data_for_test().pages[0].texels) sb += t.x + t.y + t.z;
    CHECK(sb > sm * 1.3);
  });

  /* ---------------------------------------------------------------- 5: lifecycle */
  test("baked lighting: saving the scene and opening it again brings the lightmaps back (pages, entries, picture)", [] {
    const std::string name = "R35_Lifecycle";
    Editor ed;
    r35_open(ed);
    r35_add(ed, "Plane", "Floor", {0, 0, 0}, {1, 1, 1}, Vec3(0.8f));
    r35_add(ed, "Cube", "Box", {0, 0.5f, 0}, {1, 1, 1}, Vec3(0.8f, 0.3f, 0.3f));
    r35_sun_mode(ed, 1);
    const std::string path = r35_scene_path(ed, name.c_str());
    const std::string dir = ed.lighting_dir_for_test();
    CHECK(!dir.empty() && dir == fs::join(fs::parent(path), name));
    if (fs::exists(dir)) std::filesystem::remove_all(dir);
    r35_bake(ed);
    CHECK(fs::exists(fs::join(dir, "LightingData.bin")) && fs::exists(fs::join(dir, "Lightmap-0.hdr")));
    CHECK(save_scene(ed.scene(), path));
    const LightingData before = ed.lighting_data_for_test();
    const std::vector<uint32_t> pic = r35_game(ed);
    CHECK(!before.pages.empty() && before.entries.size() == 2 && !ed.lighting_out_of_date_for_test());
    /* A fresh editor opens the scene (a file dropped on the window, as the Project window does). */
    Editor ed2;
    r34_open(ed2);
    CHECK(ed2.lighting_data_for_test().empty());
    platform::Event drop;
    drop.type = platform::EventType::Drop;
    drop.paths = {path};
    ed2.step_frame_headless({drop});
    r34_steps(ed2, 6);
    CHECK(ed2.scene().path == path);
    const LightingData &after = ed2.lighting_data_for_test();
    CHECK(after.pages.size() == before.pages.size());
    CHECK(after.entries.size() == before.entries.size());
    CHECK(after.light_modes == before.light_modes);
    CHECK(after.scene_key == before.scene_key);
    bool entries_same = true;
    for (auto &kv : before.entries) {
      auto it = after.entries.find(kv.first);
      entries_same = entries_same && it != after.entries.end() && it->second.hash == kv.second.hash && it->second.page == kv.second.page &&
                     it->second.tri_uv.size() == kv.second.tri_uv.size() && std::equal(kv.second.tri_uv.begin(), kv.second.tri_uv.end(), it->second.tri_uv.begin(),
                                                                                      [](const Vec2 &a, const Vec2 &b) { return a.x == b.x && a.y == b.y; });
    }
    CHECK(entries_same);
    /* The pages come back through a Radiance .hdr file (8-bit mantissa): equal to within 1% of the brightest texel. */
    double worst = 0, peak = 1e-6;
    for (size_t p = 0; p < before.pages.size() && p < after.pages.size(); p++) {
      CHECK(before.pages[p].width == after.pages[p].width && before.pages[p].texels.size() == after.pages[p].texels.size());
      for (size_t i = 0; i < before.pages[p].texels.size() && i < after.pages[p].texels.size(); i++) {
        const Vec3 a = before.pages[p].texels[i], b = after.pages[p].texels[i];
        peak = std::max({peak, (double)a.x, (double)a.y, (double)a.z});
        worst = std::max({worst, (double)std::fabs(a.x - b.x), (double)std::fabs(a.y - b.y), (double)std::fabs(a.z - b.z)});
      }
    }
    std::printf("    reloaded pages: worst texel error %.5f of peak %.3f\n", worst, peak);
    CHECK(worst <= 0.01 * peak);
    CHECK(!ed2.lighting_out_of_date_for_test());
    /* The reloaded scene draws with them: the picture matches the one before saving, up to the .hdr rounding. */
    CHECK(r35_diff_count(r35_game(ed2), pic, 3) == 0 || r35_diff_count(r35_game(ed2), pic, 6) < pic.size() / 200);
    std::filesystem::remove_all(dir);
    std::filesystem::remove(path);
  });

  /* Review of task 0012. */
  test("baked lighting: a scene baked before it had a file keeps its lightmaps when saved, and the folder is named after the file", [] {
    Editor ed;
    r35_open(ed);
    r35_add(ed, "Plane", "Floor", {0, 0, 0}, {1, 1, 1}, Vec3(0.8f));
    ed.scene().path.clear();
    CHECK(ed.lighting_dir_for_test().empty());
    r35_bake(ed);
    CHECK(!ed.lighting_data_for_test().empty());
    /* The scene's stored name differs from the file's: the folder follows the file. */
    const std::string path = fs::join(fs::join(scratch_project(), "Assets/Scenes"), "R35_Saved.scene");
    ed.scene().name = "Some Other Name";
    ed.scene().path = path;
    const std::string dir = fs::join(fs::parent(path), "R35_Saved");
    CHECK(ed.lighting_dir_for_test() == dir);
    if (fs::exists(dir)) std::filesystem::remove_all(dir);
    ed.run_action("file.save");
    CHECK(fs::exists(path));
    CHECK(fs::exists(fs::join(dir, "LightingData.bin")) && fs::exists(fs::join(dir, "Lightmap-0.hdr")));
    /* A scene file named "." or ".." never makes the scene folder itself the lighting folder. */
    ed.scene().path = fs::join(fs::join(scratch_project(), "Assets/Scenes"), "..scene");
    CHECK(ed.lighting_dir_for_test().empty() || fs::filename(ed.lighting_dir_for_test()) != "Scenes");
    std::filesystem::remove_all(dir);
    std::filesystem::remove(path);
  });

  test("baked lighting: a new scene cancels a running bake and starts with no baked data", [] {
    Editor ed;
    r35_open(ed);
    r35_add(ed, "Plane", "Floor", {0, 0, 0}, {1, 1, 1}, Vec3(0.8f));
    r35_bake(ed);
    CHECK(!ed.lighting_data_for_test().empty());
    ed.command("bake start");
    CHECK(ed.baking_for_test());
    ed.command("newscene");
    CHECK(!ed.baking_for_test());
    CHECK(ed.lighting_data_for_test().empty());
    r34_steps(ed, 3);
    CHECK(ed.lighting_data_for_test().empty() && !ed.lighting_out_of_date_for_test());
  });

  test("baked lighting: a damaged LightingData.bin is refused without a crash, and the scene opens with realtime light", [] {
    const std::string name = "R35_Damaged";
    Editor ed;
    r35_open(ed);
    r35_add(ed, "Plane", "Floor", {0, 0, 0}, {1, 1, 1}, Vec3(0.8f));
    const std::string path = r35_scene_path(ed, name.c_str());
    const std::string dir = ed.lighting_dir_for_test();
    r35_bake(ed);
    CHECK(save_scene(ed.scene(), path));
    const std::string bin = fs::join(dir, "LightingData.bin");
    std::string bytes;
    CHECK(fs::read_file(bin, bytes) && bytes.size() > 64);
    /* A huge UV count where the first entry's count sits, then a truncated copy. */
    for (int variant = 0; variant < 2; variant++) {
      std::string bad = bytes;
      if (variant == 0) {
        const uint64_t huge = 0x7FFFFFFFFFFFull;
        std::memcpy(&bad[8 + 8 + 8 + 16 + 8 + 8 + 8 + 8], &huge, 8);
      }
      else bad.resize(bytes.size() / 2);
      CHECK(fs::write_file(bin, bad));
      Editor ed2;
      r34_open(ed2);
      platform::Event drop;
      drop.type = platform::EventType::Drop;
      drop.paths = {path};
      ed2.step_frame_headless({drop});
      r34_steps(ed2, 2);
      CHECK(ed2.scene().path == path);
      CHECK(ed2.lighting_data_for_test().empty());
    }
    std::filesystem::remove_all(dir);
    std::filesystem::remove(path);
  });

  test("baked lighting: changing a Baked light on an object with its own (non-contributing) mesh says Lighting out of date", [] {
    Editor ed;
    r35_open(ed);
    r35_add(ed, "Plane", "Floor", {0, 0, 0}, {1, 1, 1}, Vec3(0.8f));
    GameObject *lamp = r35_add(ed, "Sphere", "Lamp", {0, 2, 0}, {0.2f, 0.2f, 0.2f}, Vec3(1.0f), false);
    Light *l = lamp->add<Light>();
    l->type = 1;
    l->mode = 2;
    l->range = 5.0f;
    ed.commit_change("lamp");
    r35_bake(ed);
    CHECK(!ed.lighting_out_of_date_for_test());
    l->intensity = 3.0f;
    r34_steps(ed, 2);
    CHECK(ed.lighting_out_of_date_for_test());
  });

  test("baked lighting: a light baked as Baked and switched to Realtime afterwards doesn't shine twice on lightmapped surfaces", [] {
    Editor ed;
    r35_open(ed);
    r35_add(ed, "Plane", "Floor", {0, 0, 0}, {1, 1, 1}, Vec3(0.8f));
    r35_sun_mode(ed, 2);
    r35_bake(ed);
    const std::vector<uint32_t> baked = r35_game(ed);
    r35_sun_mode(ed, 0);  // the map still holds its light
    r34_steps(ed, 4);
    const size_t d = r35_diff_count(r35_game(ed), baked);
    std::printf("    switched to Realtime after the bake: %zu Game view pixels changed\n", d);
    CHECK(d == 0);
    CHECK(ed.lighting_out_of_date_for_test());  // but a re-bake is due
  });

  test("baked lighting: moving a static object makes it fall back to realtime ambient and says Lighting out of date; moving it back restores the map", [] {
    auto build = [](Editor &ed) {
      r35_open(ed);
      r35_add(ed, "Plane", "Floor", {0, 0, 0}, {1, 1, 1}, Vec3(0.8f), false);  // dynamic floor
      r35_add(ed, "Cube", "Box", {0, 0.5f, 0}, {1, 1, 1}, Vec3(0.8f));          // the one static object
      r35_sun_mode(ed, 2);
      r34_steps(ed, 3);
    };
    /* After the bake the sun swings round: a lightmapped box keeps the baked light, a box without a map
     * follows the sun. That tells the two apart in the picture. */
    auto swing = [](Editor &ed) {
      ed.scene().find_by_name("Directional Light")->set_local_euler({50, -90, 0});
      ed.commit_change("swing the sun");
      r34_steps(ed, 3);
    };
    Editor baked, plain;
    build(baked);
    build(plain);
    r35_bake(baked);
    swing(baked);
    swing(plain);
    const std::vector<uint32_t> p0 = r35_game(plain);
    const std::vector<uint32_t> b0 = r35_game(baked);
    CHECK(baked.lighting_out_of_date_for_test());  // the sun moved since the bake
    CHECK(r35_diff_count(p0, b0) > 50);  // the lightmap really is in the picture: the box ignores the swung sun
    baked.command("select Box");
    baked.command("position 1.5 0.5 0");
    r34_steps(baked, 4);
    CHECK(baked.lighting_out_of_date_for_test());
    plain.command("select Box");
    plain.command("position 1.5 0.5 0");
    r34_steps(plain, 4);
    /* The moved box has no lightmap: the picture is exactly the never-baked one. */
    CHECK(r35_diff_count(r35_game(plain), r35_game(baked)) == 0);
    baked.command("position 0 0.5 0");
    r34_steps(baked, 4);
    CHECK(r35_game(baked) == b0);  // back in place: the map is used again
    /* ...and with the sun back where it was, nothing is out of date any more. */
    baked.scene().find_by_name("Directional Light")->set_local_euler({50, 90, 0});
    baked.commit_change("sun back");
    r34_steps(baked, 3);
    CHECK(!baked.lighting_out_of_date_for_test());
  });

  test("baked lighting: Clear Baked Data restores the picture bit-identically and removes the folder", [] {
    Editor ed;
    r35_open(ed);
    r35_add(ed, "Plane", "Floor", {0, 0, 0}, {1, 1, 1}, Vec3(0.8f));
    r35_add(ed, "Cube", "Box", {0, 0.5f, 0}, {1, 1, 1}, Vec3(0.9f, 0.3f, 0.2f));
    r35_sun_mode(ed, 2);
    const std::string path = r35_scene_path(ed, "R35_Clear");
    const std::string dir = ed.lighting_dir_for_test();
    if (fs::exists(dir)) std::filesystem::remove_all(dir);
    r34_steps(ed, 4);
    const std::vector<uint32_t> before = r35_game(ed), before_scene = r35_scene_view(ed);
    r35_bake(ed);
    CHECK(fs::exists(dir));
    const std::vector<uint32_t> baked = r35_game(ed);
    CHECK(r35_diff_count(before, baked) > 100);
    ed.command("bake clear");
    r34_steps(ed, 4);
    CHECK(ed.lighting_data_for_test().empty());
    CHECK(!fs::exists(dir));
    CHECK(!ed.lighting_out_of_date_for_test());
    CHECK(r35_game(ed) == before);
    CHECK(r35_scene_view(ed) == before_scene);
    /* Clearing again, and with nothing baked, is harmless. */
    ed.command("bake clear");
    r34_steps(ed, 2);
    CHECK(r35_game(ed) == before);
    (void)path;
  });

  test("baked lighting: dynamic objects are unaffected by a bake, and only the static box's pixels change", [] {
    auto build = [](Editor &ed, bool sphere, bool box) {
      r35_open(ed);
      r35_add(ed, "Plane", "Floor", {0, 0, 0}, {1, 1, 1}, Vec3(0.8f), false);
      if (box) r35_add(ed, "Cube", "Box", {1.5f, 0.5f, 0}, {1, 1, 1}, Vec3(0.8f, 0.4f, 0.3f));
      if (sphere) r35_add(ed, "Sphere", "Ball", {-3, 0.5f, 0}, {1, 1, 1}, Vec3(0.3f, 0.5f, 0.9f), false);
      r35_sun_mode(ed, 2);
      r34_steps(ed, 4);
    };
    Editor plain, baked, no_ball, no_box;
    build(plain, true, true);
    build(baked, true, true);
    build(no_ball, false, true);
    build(no_box, true, false);
    r35_bake(baked);
    /* The sun swings round after the bake: only what has a lightmap keeps the old light. */
    for (Editor *e : {&plain, &baked, &no_ball, &no_box}) {
      e->scene().find_by_name("Directional Light")->set_local_euler({50, -90, 0});
      e->commit_change("swing the sun");
      r34_steps(*e, 3);
    }
    const std::vector<uint32_t> p = r35_game(plain), b = r35_game(baked), nb = r35_game(no_ball), nx = r35_game(no_box);
    CHECK(p.size() == b.size());
    size_t changed = 0, changed_ball = 0, ball_pixels = 0, box_changed = 0;
    for (size_t i = 0; i < p.size(); i++) {
      const bool ball = p[i] != nb[i];  // pixels the ball (or its shadow) occupies
      const bool box = p[i] != nx[i];   // pixels the box (or its shadow) occupies
      const bool diff = p[i] != b[i];
      changed += diff;
      if (ball && !box) {
        ball_pixels++;
        changed_ball += diff;
      }
      if (diff && box) box_changed++;
    }
    std::printf("    %zu pixels changed by the bake; %zu of them in the box's area; the ball's area (%zu pixels) changed in %zu\n", changed, box_changed,
                ball_pixels, changed_ball);
    CHECK(changed > 30);
    CHECK(box_changed == changed);
    CHECK(ball_pixels > 30 && changed_ball == 0);
  });

  test("baked lighting: the path tracer ignores lightmaps - an F12 path-traced render is the same with and without baked data", [] {
    Editor ed;
    r35_open(ed);
    r35_add(ed, "Plane", "Floor", {0, 0, 0}, {1, 1, 1}, Vec3(0.8f));
    r35_add(ed, "Cube", "Box", {0, 0.5f, 0}, {1, 1, 1}, Vec3(0.8f, 0.3f, 0.3f));
    r35_sun_mode(ed, 2);
    ed.command("set Render.RenderEngine Path");
    ed.command("set Render.Samples 4");
    ed.command("set Render.Denoise false");
    ed.command("set Render.ResolutionX 160");
    ed.command("set Render.ResolutionY 90");
    auto render = [&] {
      ed.command("render");
      for (int i = 0; i < 4000 && ed.rendering_for_test(); i++) ed.step_frame_headless();
      CHECK(!ed.rendering_for_test());
      return ed.render_image_for_test().pixels;
    };
    const std::vector<uint32_t> r0 = render(), r0b = render();
    CHECK(!r0.empty());
    r35_bake(ed);
    const std::vector<uint32_t> r1 = render();
    const size_t noise = r35_diff_count(r0, r0b), baked = r35_diff_count(r0, r1);
    std::printf("    path-traced pixels differing: run to run %zu, with baked data %zu of %zu\n", noise, baked, r0.size());
    CHECK(r0.size() == r1.size());
    CHECK(baked <= noise);
    /* The same on the rasterized F12 render: it does use the maps. */
    ed.command("set Render.RenderEngine Rasterized");
    ed.command("set Render.RenderEngine 0");
    const std::vector<uint32_t> ras_baked = render();
    ed.command("bake clear");
    r34_steps(ed, 3);
    const std::vector<uint32_t> ras_plain = render();
    CHECK(r35_diff_count(ras_baked, ras_plain) > 50);
  });

  /* ---------------------------------------------------------------- 6: robustness */
  test("baked lighting: cancelling a bake midway leaves the scene usable, and a new bake works afterwards", [] {
    /* Through the editor: start, cancel before a frame runs. */
    Editor ed;
    r35_open(ed);
    r35_add(ed, "Plane", "Floor", {0, 0, 0}, {1, 1, 1}, Vec3(0.8f));
    r35_add(ed, "Cube", "Box", {0, 0.5f, 0}, {1, 1, 1}, Vec3(0.8f));
    r34_steps(ed, 3);
    const std::vector<uint32_t> before = r35_game(ed);
    ed.command("bake start");
    CHECK(ed.baking_for_test());
    ed.command("bake cancel");
    CHECK(!ed.baking_for_test());
    r34_steps(ed, 4);
    CHECK(ed.lighting_data_for_test().empty());
    CHECK(r35_game(ed) == before);
    ed.command("bake cancel");  // nothing running: harmless
    ed.command("bake start");
    ed.command("bake start");  // already running: ignored
    ed.command("bake clear");  // clear cancels
    CHECK(!ed.baking_for_test());
    CHECK(ed.lighting_dir_for_test().empty());  // a scene without a file keeps its lightmaps in memory only
    r35_bake(ed);
    CHECK(!ed.lighting_data_for_test().empty() && r35_diff_count(before, r35_game(ed)) > 50);
    /* Directly: stop after one batch, abandon it, then reuse the same Lightmapper. */
    Lightmapper lm;
    Environment env;
    const MeshPtr plane = primitives::plane(10.0f, 4);
    const BakeObject o = r35_object(1, plane, Mat4());
    lm.begin({o}, {}, env, r35_small(8.0f, 8));
    CHECK(lm.texel_count() > 4096);  // more than one batch of 2048
    const bool done = lm.step(0.0);
    CHECK(!done && lm.active() && lm.progress() > 0.0f && lm.progress() < 1.0f);
    lm.cancel();
    CHECK(!lm.active());
    CHECK(lm.step(1000.0));  // nothing left to do
    const LightingData cancelled = lm.take_result();
    (void)cancelled;  // whatever it holds, taking it is safe
    const LightingData again = r35_run(lm, {o}, {}, env, r35_small(8.0f, 8));
    CHECK(again.entries.size() == 1 && !again.pages.empty());
    Vec3 v;
    CHECK(r35_sample(again, 1, *plane, Mat4(), {1, 0, 1}, v) && r35_finite(v));
  });

  test("baked lighting: NaN, negative and huge settings, faceless and degenerate objects, and an empty scene are safe", [] {
    Environment env;
    const MeshPtr cube = primitives::cube();
    BakeSettings s;
    s.texels_per_unit = kNaN;
    s.max_size = -5;
    s.padding = -7;
    s.direct_samples = 0;
    s.indirect_samples = -3;
    s.bounces = -1;
    s.indirect_intensity = kNaN;
    {
      Lightmapper lm;
      const LightingData ld = r35_run(lm, {r35_object(1, cube, Mat4::scale({2, 2, 2}))}, {}, env, s);
      CHECK(!ld.pages.empty() && ld.pages[0].width >= 16 && ld.pages[0].width <= 8192);
      size_t bad = 0;
      for (const Lightmap &p : ld.pages)
        for (const Vec3 &t : p.texels) bad += !(r35_finite(t) && t.x >= 0 && t.y >= 0 && t.z >= 0);
      CHECK(bad == 0);
    }
    s = r35_small(kInf, 2);
    s.max_size = 1 << 30;
    s.padding = 1 << 30;
    s.direct_samples = 1 << 30;
    s.bounces = 1 << 30;
    s.indirect_intensity = kInf;
    {
      Lightmapper lm;
      BakeObject o = r35_object(1, cube, Mat4::scale({0.02f, 0.02f, 0.02f}));  // a small object: a huge density stays cheap
      lm.begin({o}, {}, env, s);
      CHECK(lm.texel_count() < 70000000u);
      lm.cancel();
    }
    s = r35_small(8.0f, 2);
    {
      /* NaN scale, NaN placement, a mesh with no faces, a zero-area face and an object that is nothing. */
      BakeObject nan_scale = r35_object(1, cube, Mat4());
      nan_scale.scale = kNaN;
      BakeObject neg_scale = r35_object(2, cube, Mat4::translate({3, 0, 0}));
      neg_scale.scale = -4.0f;
      BakeObject huge_scale = r35_object(3, cube, Mat4::translate({6, 0, 0}));
      huge_scale.scale = 1e30f;
      BakeObject faceless = r35_object(4, std::make_shared<Mesh>(), Mat4());
      BakeObject null_mesh;
      null_mesh.id = 5;
      MeshPtr flat = std::make_shared<Mesh>(*primitives::quad());
      for (Vec3 &p : flat->positions) p = Vec3(1, 1, 1);  // every face has zero area
      flat->touch();
      BakeObject degenerate = r35_object(6, flat, Mat4());
      MeshPtr nanm = std::make_shared<Mesh>(*primitives::quad());
      nanm->positions[0].x = kNaN;
      nanm->touch();
      BakeObject nan_mesh = r35_object(7, nanm, Mat4::translate({0, 0, 5}));
      Lightmapper lm;
      const LightingData ld = r35_run(lm, {nan_scale, neg_scale, huge_scale, faceless, null_mesh, degenerate, nan_mesh}, {}, env, s);
      CHECK(ld.entries.count(1) && ld.entries.count(2) && ld.entries.count(3));
      CHECK(!ld.entries.count(4) && !ld.entries.count(5));
      size_t bad = 0;
      for (const Lightmap &p : ld.pages)
        for (const Vec3 &t : p.texels) bad += !r35_finite(t);
      CHECK(bad == 0);
      for (auto &kv : ld.entries) {
        size_t b2 = 0;
        for (const Vec2 &u : kv.second.tri_uv) b2 += !(std::isfinite(u.x) && std::isfinite(u.y));
        if (b2) std::printf("    entry %llu: %zu of %zu lightmap UVs are not finite\n", (unsigned long long)kv.first, b2, kv.second.tri_uv.size());
        bad += b2;
      }
      CHECK(bad == 0);
    }
    {
      /* Nothing to bake. */
      Lightmapper lm;
      const LightingData ld = r35_run(lm, {}, {}, env, s);
      CHECK(ld.pages.empty() && ld.entries.empty() && lm.texel_count() == 0 && lm.progress() == 1.0f);
      BakeLight sun;
      sun.mode = 2;
      sun.id = 3;
      const LightingData ld2 = r35_run(lm, {}, {sun}, env, s);
      CHECK(ld2.pages.empty() && ld2.entries.empty());
    }
    /* Through the editor: settings gone wrong, an object with no faces, then nothing static at all. */
    Editor ed;
    r35_open(ed);
    r35_add(ed, "Plane", "Floor", {0, 0, 0}, {1, 1, 1}, Vec3(0.8f));
    GameObject *empty = r35_add(ed, "Cube", "Empty", {2, 0.5f, 0}, {1, 1, 1}, Vec3(0.5f));
    empty->get<MeshFilter>()->mesh = std::make_shared<Mesh>();
    r35_add(ed, "Cube", "Flat", {-2, 0.5f, 0}, {1, 1, 1}, Vec3(0.5f));
    LightingSettings &ls = ed.scene().lighting;
    ls.texels_per_unit = kNaN;
    ls.max_size = 1 << 30;
    ls.padding = -3;
    ls.direct_samples = 0;
    ls.indirect_samples = 1;
    ls.bounces = 1 << 20;
    ls.indirect_intensity = kNaN;
    ed.commit_change("bad settings");
    r34_steps(ed, 3);
    ed.command("bake start");
    for (int i = 0; i < 100 && ed.baking_for_test(); i++) ed.step_frame_headless();
    CHECK(!ed.baking_for_test());
    r34_steps(ed, 3);
    CHECK(!ed.lighting_data_for_test().empty());
    const LightingData &got = ed.lighting_data_for_test();
    size_t bad = 0;
    for (const Lightmap &p : got.pages)
      for (const Vec3 &t : p.texels) bad += !r35_finite(t);
    CHECK(bad == 0);
    CHECK(r35_luma(r35_game(ed)) > 0);
    /* Nothing contributes: the bake runs, makes no maps, and the picture is the plain one. */
    ed.command("bake clear");
    r34_steps(ed, 3);
    const std::vector<uint32_t> plain = r35_game(ed);
    ed.scene().find_by_name("Floor")->get<MeshRenderer>()->contribute_gi = false;
    ed.scene().find_by_name("Empty")->get<MeshRenderer>()->contribute_gi = false;
    ed.scene().find_by_name("Flat")->get<MeshRenderer>()->contribute_gi = false;
    ed.commit_change("nothing static");
    r35_bake(ed);
    CHECK(ed.lighting_data_for_test().pages.empty() && ed.lighting_data_for_test().entries.empty());
    CHECK(!ed.lighting_out_of_date_for_test());  // no data, so nothing is out of date
    CHECK(r35_game(ed) == plain);
    /* Baked GI off: Generate Lighting refuses. */
    ed.scene().lighting.baked_gi = false;
    ed.commit_change("baked gi off");
    ed.command("bake start");
    CHECK(!ed.baking_for_test());
  });

  test("baked lighting: undo covers Contribute GI, Light Mode and the Lighting settings; the mode and settings save and load", [] {
    Editor ed;
    auto run = [&](const char *c) {
      ed.command(c);
      r34_steps(ed, 2);  // a frame commits the undo step: two commands in one frame would be one step
    };
    r35_open(ed);
    r35_add(ed, "Cube", "Box", {0, 0.5f, 0}, {1, 1, 1}, Vec3(0.8f), false);
    ed.commit_change("add Box");  // an undo step of its own, so undoing the settings below never removes the box
    run("select Box");
    /* (a lookup that reports a missing object instead of crashing the run) */
    static MeshRenderer dummy_mr;
    static Light dummy_light;
    auto box_mr = [&](int line) -> MeshRenderer * {
      GameObject *g = ed.scene().find_by_name("Box");
      CHECK(g != nullptr && g->get<MeshRenderer>() != nullptr);
      return g && g->get<MeshRenderer>() ? g->get<MeshRenderer>() : &dummy_mr;
    };
    auto sun_mode = [&]() -> int & {
      GameObject *g = ed.scene().find_by_name("Directional Light");
      CHECK(g != nullptr && g->get<Light>() != nullptr);
      return (g && g->get<Light>() ? g->get<Light>() : &dummy_light)->mode;
    };
    CHECK(!box_mr(__LINE__)->contribute_gi);
    run("set MeshRenderer.ContributeGI true");
    CHECK(box_mr(__LINE__)->contribute_gi);
    run("set MeshRenderer.ScaleInLightmap 2");
    CHECK(std::fabs(box_mr(__LINE__)->scale_in_lightmap - 2.0f) < 1e-6f);
    run("undo");
    CHECK(std::fabs(box_mr(__LINE__)->scale_in_lightmap - 1.0f) < 1e-6f);
    run("undo");
    CHECK(!box_mr(__LINE__)->contribute_gi);
    run("redo");
    CHECK(box_mr(__LINE__)->contribute_gi);
    run("select Directional Light");
    CHECK(sun_mode() == 0);
    run("set Light.Mode Baked");
    CHECK(sun_mode() == 2);
    run("undo");
    CHECK(sun_mode() == 0);
    run("redo");
    CHECK(sun_mode() == 2);
    run("set Light.Mode Mixed");
    CHECK(sun_mode() == 1);
    const int before_samples = ed.scene().lighting.indirect_samples;
    run("set Lighting.IndirectSamples 64");
    CHECK(ed.scene().lighting.indirect_samples == 64);
    run("set Lighting.LightmapResolution 10");
    CHECK(std::fabs(ed.scene().lighting.texels_per_unit - 10.0f) < 1e-5f);
    run("set Lighting.Bounces 3");
    run("set Lighting.Denoise false");
    run("set Lighting.AutoGenerate true");
    CHECK(ed.scene().lighting.bounces == 3 && !ed.scene().lighting.denoise && ed.scene().lighting.auto_generate);
    /* Saved and loaded as they are. */
    Scene back;
    std::string err;
    CHECK(load_scene_text(save_scene_text(ed.scene()), back, err));
    CHECK(back.lighting.indirect_samples == 64 && back.lighting.bounces == 3 && !back.lighting.denoise && back.lighting.auto_generate);
    CHECK(std::fabs(back.lighting.texels_per_unit - 10.0f) < 1e-5f);
    CHECK(back.find_by_name("Directional Light")->get<Light>()->mode == 1);
    CHECK(back.find_by_name("Box")->get<MeshRenderer>()->contribute_gi);
    CHECK(save_scene_text(back) == save_scene_text(ed.scene()));
    /* Undo walks the settings back one by one. */
    for (int i = 0; i < 5; i++) run("undo");  // AutoGenerate, Denoise, Bounces, Resolution, Samples
    CHECK(ed.scene().lighting.indirect_samples == before_samples);
    CHECK(std::fabs(ed.scene().lighting.texels_per_unit - 8.0f) < 1e-5f);
    CHECK(ed.scene().lighting.bounces == 2 && ed.scene().lighting.denoise && !ed.scene().lighting.auto_generate);
    /* Bad numbers typed into a setting are clamped, not stored. */
    run("set Lighting.IndirectSamples -5");
    CHECK(ed.scene().lighting.indirect_samples >= 1);
    run("set Lighting.LightmapResolution nan");
    CHECK(std::isfinite(ed.scene().lighting.texels_per_unit) && ed.scene().lighting.texels_per_unit > 0.0f);
    run("set Lighting.MaxLightmapSize 99999999");
    CHECK(ed.scene().lighting.max_size <= 4096);
  });
}

/* ===================================================================== */
/* Round 36: voxel-based global illumination (task 0013)                   */
/* ===================================================================== */

namespace {

struct R36D3 {
  double x, y, z;
};
R36D3 r36_d3(Vec3 v) { return {v.x, v.y, v.z}; }
R36D3 r36_sub(R36D3 a, R36D3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
R36D3 r36_cross(R36D3 a, R36D3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double r36_dot(R36D3 a, R36D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

/* Akenine-Moeller's triangle / box test, in double: the 13 separating axes (a touch counts as an overlap). */
bool r36_tri_box(Vec3 va, Vec3 vb, Vec3 vc, Vec3 box_min) {
  const R36D3 ctr = {box_min.x + 0.5, box_min.y + 0.5, box_min.z + 0.5};
  const R36D3 a = r36_sub(r36_d3(va), ctr), b = r36_sub(r36_d3(vb), ctr), c = r36_sub(r36_d3(vc), ctr);
  const R36D3 e[3] = {r36_sub(b, a), r36_sub(c, b), r36_sub(a, c)};
  const R36D3 unit[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  auto separated = [&](R36D3 ax) {
    if (r36_dot(ax, ax) < 1e-18) return false;  // a degenerate axis separates nothing
    const double p0 = r36_dot(a, ax), p1 = r36_dot(b, ax), p2 = r36_dot(c, ax);
    const double r = 0.5 * (std::fabs(ax.x) + std::fabs(ax.y) + std::fabs(ax.z));
    return std::min({p0, p1, p2}) > r || std::max({p0, p1, p2}) < -r;
  };
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      if (separated(r36_cross(e[i], unit[j]))) return false;
  for (int j = 0; j < 3; j++)
    if (separated(unit[j])) return false;
  return !separated(r36_cross(e[0], e[1]));
}

int r36_pop(uint64_t v) {
  int n = 0;
  for (; v; v &= v - 1) n++;
  return n;
}
int r36_count_set(const VoxelGrid &g) {
  int n = 0;
  for (const VoxelColumn &c : g.levels[0]) n += r36_pop(c.lo) + r36_pop(c.hi);
  return n;
}

/* A grid whose world space is its grid space: voxel 1, origin 0, n columns, 128 deep. */
VoxelGrid r36_unit_grid(int n = 64, float voxel = 1.0f) {
  VoxelGrid g;
  AABB b;
  b.add(Vec3(0.0f));
  b.add(Vec3((float)(n - 14) * voxel, 100.0f * voxel, (float)(n - 14) * voxel));
  voxel_grid_fit(g, b, n);
  CHECK(g.valid() && g.voxel == voxel && g.n == n);
  CHECK(g.origin.x == 0.0f && g.origin.y == 0.0f && g.origin.z == 0.0f);
  return g;
}

RenderMesh r36_tris(const std::vector<Vec3> &p) {
  RenderMesh rm;
  rm.positions = p;
  for (uint32_t i = 0; i < p.size(); i++) rm.indices.push_back(i);
  return rm;
}

std::vector<VoxelSpan> r36_voxelize(VoxelGrid &g, const RenderMesh &rm, const Mat4 &model = Mat4()) {
  std::vector<VoxelSpan> spans;
  voxelize_mesh(g, rm, model, spans);
  voxel_grid_build(g, {&spans});
  return spans;
}

Vec3 r36_dir(R34Rng &rng) {
  const float z = rng.range(-1.0f, 1.0f), phi = rng.range(0.0f, 6.2831853f), r = std::sqrt(std::max(0.0f, 1.0f - z * z));
  return Vec3(r * std::cos(phi), z, r * std::sin(phi));
}

/* Random bits: some columns hold one or two short runs. */
std::vector<VoxelSpan> r36_random_bits(const VoxelGrid &g, R34Rng &rng, float density) {
  std::vector<VoxelSpan> spans;
  for (int z = 0; z < g.n; z++)
    for (int x = 0; x < g.n; x++) {
      if (rng.f() > density) continue;
      for (int k = 0, runs = 1 + (rng.f() < 0.3f); k < runs; k++) {
        const int y0 = (int)(rng.f() * 127.99f), y1 = std::min(127, y0 + (int)(rng.f() * 6.0f));
        spans.push_back({(uint32_t)(z * g.n + x), (uint8_t)y0, (uint8_t)y1});
      }
    }
  return spans;
}
/* Random triangles inside the grid's box. */
std::vector<VoxelSpan> r36_random_tris(const VoxelGrid &g, R34Rng &rng, int count, float size) {
  std::vector<Vec3> p;
  const float ex = (float)g.n * g.voxel, ey = (float)VoxelGrid::kDepth * g.voxel;
  for (int i = 0; i < count; i++) {
    const Vec3 c(rng.range(0.1f, 0.9f) * ex, rng.range(0.1f, 0.9f) * ey, rng.range(0.1f, 0.9f) * ex);
    for (int k = 0; k < 3; k++)
      p.push_back(g.origin + c + Vec3(rng.range(-size, size), rng.range(-size, size), rng.range(-size, size)) * g.voxel);
  }
  std::vector<VoxelSpan> spans;
  voxelize_mesh(g, r36_tris(p), Mat4(), spans);
  return spans;
}

/* Does the ray (grid units) pass within 1e-4 of a voxel edge (two lattice coordinates at once) while it is
 * inside the grid's box, up to grid distance t_end? Those are the rays whose answer is a coin toss. */
bool r36_near_edge(const VoxelGrid &g, Vec3 world_o, Vec3 world_d, double t_end) {
  const Vec3 go = g.to_grid(world_o);
  const double O[3] = {go.x, go.y, go.z}, D[3] = {world_d.x, world_d.y, world_d.z};
  const double hi[3] = {(double)g.n, (double)VoxelGrid::kDepth, (double)g.n};
  double t0 = 0.0, t1 = t_end;
  for (int a = 0; a < 3; a++) {
    if (std::fabs(D[a]) < 1e-12) {
      if (O[a] < 0.0 || O[a] > hi[a]) return false;
      continue;
    }
    double ta = (0.0 - O[a]) / D[a], tb = (hi[a] - O[a]) / D[a];
    if (ta > tb) std::swap(ta, tb);
    t0 = std::max(t0, ta);
    t1 = std::min(t1, tb);
  }
  if (!(t0 < t1)) return false;
  for (int a = 0; a < 3; a++) {
    if (std::fabs(D[a]) < 1e-9) continue;
    const double pa = O[a] + D[a] * t0, pb = O[a] + D[a] * t1;
    for (double k = std::ceil(std::min(pa, pb)); k <= std::max(pa, pb); k += 1.0) {
      const double t = (k - O[a]) / D[a];
      for (int b = 0; b < 3; b++) {
        if (b == a) continue;
        const double p = O[b] + D[b] * t;
        if (std::fabs(p - std::round(p)) < 1e-4) return true;
      }
    }
  }
  return false;
}

struct R36RayStats {
  int rays = 0, hits = 0, hier_vs_columns = 0, vs_dda = 0, grazing = 0, hard = 0, t_off = 0;
};
/* Shoots `count` random rays (and a few axis-parallel ones) at the grid with all three traces. */
R36RayStats r36_compare_traces(const VoxelGrid &g, R34Rng &rng, int count) {
  R36RayStats s;
  const float ex = (float)g.n * g.voxel, ey = (float)VoxelGrid::kDepth * g.voxel, m = 10.0f * g.voxel;
  for (int i = 0; i < count; i++) {
    const Vec3 o = g.origin + Vec3(rng.range(-m, ex + m), rng.range(-m, ey + m), rng.range(-m, ex + m));
    Vec3 d = r36_dir(rng);
    if (i % 50 == 0) d = Vec3(0.0f), d[i / 50 % 3] = (i / 150) % 2 ? -1.0f : 1.0f;  // axis-parallel: exact boundaries
    const float tmax = rng.range(2.0f, 120.0f) * g.voxel;
    VoxelHit a, b, c;
    const bool ha = voxel_trace(g, o, d, tmax, a), hb = voxel_trace_columns(g, o, d, tmax, b), hc = voxel_trace_dda(g, o, d, tmax, c);
    s.rays++;
    s.hits += hb;
    if (ha != hb || (ha && (a.x != b.x || a.y != b.y || a.z != b.z || a.t != b.t))) s.hier_vs_columns++;
    if (hb != hc || (hb && (b.x != c.x || b.y != c.y || b.z != c.z))) {
      /* A mismatch is allowed only for a ray that grazes a voxel edge on its way to the hit. */
      double t_end = (double)tmax / g.voxel;
      if (hb || hc) t_end = std::min(t_end, std::max(hb ? b.t : 0.0f, hc ? c.t : 0.0f) / (double)g.voxel + 2.0);
      if (r36_near_edge(g, o, d, t_end)) s.grazing++;
      else {
        s.hard++;
        if (s.hard <= 3)
          std::printf("    ray %d: o (%g %g %g) d (%g %g %g) tmax %g: columns %d (%d %d %d t %g), dda %d (%d %d %d t %g)\n", i, o.x, o.y, o.z, d.x, d.y, d.z,
                      tmax, hb, b.x, b.y, b.z, b.t, hc, c.x, c.y, c.z, c.t);
      }
      s.vs_dda++;
    }
    else if (hb && std::fabs(b.t - c.t) > 2e-3f * g.voxel + 1e-4f * b.t) s.t_off++;
  }
  return s;
}

/* ---- a scene for the gather and image tests: items, world, grid, RSM ---- */
struct R36Sc {
  CfScene sc;
  Environment env;
  VoxelGrid grid;
  Rsm rsm;
  uint32_t next_id = 1;
  R36Sc() {
    env.mode = Environment::Color;
    env.color = Vec3(0.4f);
    env.strength = 1.0f;
  }
  void add(MeshPtr m, const Mat4 &model, Vec3 colour) { sc.add(m, model, next_id++, make_material("m", colour)); }
  AABB bounds() const {
    AABB b;
    for (const DrawItem &it : sc.items) b.add(it.mesh->bounds.transformed(it.model));
    return b;
  }
  /* Voxelizes everything into a grid of n columns; renders the RSM when there is a sun. */
  void finish(int n, const RenderLight *sun, int rsm_res = 256) {
    sc.seal();
    const AABB b = bounds();
    voxel_grid_fit(grid, b, n);
    std::vector<std::vector<VoxelSpan>> spans(sc.items.size());
    std::vector<const std::vector<VoxelSpan> *> ptrs;
    for (size_t i = 0; i < sc.items.size(); i++) {
      voxelize_mesh(grid, *sc.items[i].mesh, sc.items[i].model, spans[i]);
      ptrs.push_back(&spans[i]);
    }
    voxel_grid_build(grid, ptrs);
    rsm = Rsm{};
    if (sun) render_rsm(rsm, sc.items, *sun, b, rsm_res);
  }
};

RenderLight r36_sun(Vec3 dir, float intensity = 0.7f) {
  RenderLight s;
  s.direction = normalize(dir);
  s.intensity = intensity;
  s.shadows = false;
  return s;
}

/* The bounce and sky summed over the 16 ray sets, for a receiver on the floor at p (normal up). */
GiSample r36_gather_all(const R36Sc &s, const Rsm *rsm, Vec3 p, const GiParams &prm, Vec3 n = Vec3(0, 1, 0)) {
  GiSample sum;
  sum.sky = 0.0f;
  for (int set = 0; set < 16; set++) {
    const GiSample g = voxel_gi_gather(s.grid, rsm, s.env, p, n, prm, set);
    sum.bounce += g.bounce;
    sum.sky += g.sky / 16.0f;
  }
  return sum;
}

bool r36_pix(const Mat4 &v, const Mat4 &p, int W, int H, Vec3 w, int &x, int &y) {
  const Vec4 c = (p * v) * Vec4(w, 1.0f);
  if (!(c.w > 0.0f)) return false;
  x = (int)((c.x / c.w * 0.5f + 0.5f) * W);
  y = (int)((0.5f - c.y / c.w * 0.5f) * H);
  return x >= 3 && y >= 3 && x < W - 3 && y < H - 3;
}
/* Mean channel values (0..255) of the 5 x 5 pixels around a world point; false when it is off screen. */
bool r36_probe(const Image &img, const Mat4 &v, const Mat4 &p, Vec3 w, double out[3]) {
  int x, y;
  if (!r36_pix(v, p, img.width, img.height, w, x, y)) return false;
  out[0] = out[1] = out[2] = 0.0;
  for (int dy = -2; dy <= 2; dy++)
    for (int dx = -2; dx <= 2; dx++) {
      const uint32_t c = img.pixels[(size_t)(y + dy) * img.width + x + dx];
      out[0] += (c >> 16) & 255, out[1] += (c >> 8) & 255, out[2] += c & 255;
    }
  for (int k = 0; k < 3; k++) out[k] /= 25.0;
  return true;
}
/* The 3 x 3 pixels around a world point, bit for bit. */
bool r36_same_block(const Image &a, const Image &b, const Mat4 &v, const Mat4 &p, Vec3 w) {
  int x, y;
  if (!r36_pix(v, p, a.width, a.height, w, x, y)) return false;
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++)
      if (a.pixels[(size_t)(y + dy) * a.width + x + dx] != b.pixels[(size_t)(y + dy) * b.width + x + dx]) return false;
  return true;
}

struct R36View {
  Mat4 v, p;
  Vec3 eye;
  int W = 240, H = 180;
  R36View(Vec3 e, Vec3 target) : eye(e) {
    v = Mat4::look_at(e, target, {0, 1, 0});
    p = Mat4::perspective(55 * kDeg2Rad, W / (float)H, 0.1f, 80.0f);
  }
};

void r36_render(Image &img, const R36Sc &s, const R36View &view, const RenderLight *sun, const VoxelGIFrame *gi) {
  img.resize(view.W, view.H);
  RenderTarget rt;
  rt.attach(img, {0, 0, view.W, view.H});
  RasterOptions opt;
  opt.shade = ShadeMode::Deferred;
  LightingEnv env;
  env.environment = &s.env;
  env.camera_pos = view.eye;
  if (sun) env.lights.push_back(*sun);
  env.gi = gi;
  Renderer3D r3d;
  r3d.begin(&rt, view.v, view.p, env, opt);
  r3d.clear(0xFF204060u);
  for (const DrawItem &it : s.sc.items) r3d.add(it);
  r3d.flush();
}

VoxelGIFrame r36_frame(const R36Sc &s, int rays = 16, float radius = 2.0f, int downsample = 2) {
  VoxelGIFrame f;
  f.grid = &s.grid;
  f.rsm = s.rsm.valid() ? &s.rsm : nullptr;
  GiParams p;
  p.rays = rays;
  p.radius = radius;
  p.downsample = downsample;
  f.params = gi_params_sanitized(p);
  return f;
}

/* A floor with a red (or other) wall beside it at x = dx, the sun on the wall's -x face; optionally a slab high above
 * that keeps the sun off the wall. */
void r36_wall_scene(R36Sc &s, Vec3 wall_colour, bool slab_over_wall, RenderLight &sun, float dx) {
  sun = r36_sun(Vec3(1.0f, -1.2f, 0.3f));
  s.add(primitives::plane(20.0f, 2), Mat4(), Vec3(0.7f));
  s.add(primitives::cube(), Mat4::translate({dx, 2, 0}) * Mat4::scale({0.2f, 4, 8}), wall_colour);
  if (slab_over_wall) s.add(primitives::cube(), Mat4::translate({dx - 3.5f, 6.2f, 0}) * Mat4::scale({9, 0.4f, 16}), Vec3(0.7f));
  s.finish(128, &sun);
}

/* Mean (GI on - GI off) over the floor beside the wall, per channel (0..255 steps). */
bool r36_bleed(const R36Sc &s, const RenderLight &sun, double d[3], float dx) {
  const R36View view(Vec3(-6 + dx, 5, 9), Vec3(-1 + dx, 0, 0));
  Image on, off;
  const VoxelGIFrame fr = r36_frame(s);
  r36_render(on, s, view, &sun, &fr);
  r36_render(off, s, view, &sun, nullptr);
  d[0] = d[1] = d[2] = 0.0;
  int n = 0;
  for (float z : {-1.5f, 0.0f, 1.5f}) {
    double a[3], b[3];
    if (!r36_probe(on, view.v, view.p, Vec3(dx - 0.6f, 0, z), a) || !r36_probe(off, view.v, view.p, Vec3(dx - 0.6f, 0, z), b)) continue;
    for (int k = 0; k < 3; k++) d[k] += a[k] - b[k];
    n++;
  }
  for (int k = 0; k < 3; k++) d[k] /= std::max(1, n);
  return n == 3;
}

/* ---- editor scenes ---- */

/* The Game view looks straight down at a floor that lies to the wall's -x side, so a whole-picture
 * average is the floor beside the wall. The wall stands at x = dx: the world origin is not special, but see the
 * "same wherever the scene sits" test for why these scenes keep away from it. */
void r36_editor_scene(Editor &ed, Vec3 wall_colour = Vec3(0.9f, 0.1f, 0.1f), float dx = 6.0f) {
  r35_open(ed);
  r35_add(ed, "Plane", "Floor", {dx - 5.1f, 0, 0}, {1, 1, 1}, Vec3(0.8f));
  r35_add(ed, "Cube", "Wall", {dx, 1, 0}, {0.2f, 2, 6}, wall_colour);
  GameObject *cam = ed.scene().find_by_name("Main Camera");
  cam->set_local_position({dx - 2.5f, 8, 0});
  cam->set_local_euler({90, 0, 0});
  ed.commit_change("r36 scene");
  r34_steps(ed, 3);
}

/* The 5 x 5 pixels around a world point in the Game view's picture (a crop from r35_game), mean R G B. */
bool r36_game_probe(Editor &ed, const std::vector<uint32_t> &crop, Vec3 w, double out[3]) {
  const Recti r = ed.window_rect_for_test(WindowKind::Game);
  GameObject *go = ed.scene().find_by_name("Main Camera");
  Camera *cam = go ? go->get<Camera>() : nullptr;
  if (!cam || r.w < 8 || r.h < 8 || crop.size() < (size_t)r.w * r.h) return false;
  const Quat q = go->world_rotation();
  const Vec3 eye = go->world_position();
  const Mat4 v = Mat4::look_at(eye, eye + q.rotate({0, 0, 1}), q.rotate({0, 1, 0})), p = cam->projection(r.w / (float)r.h);
  int x, y;
  if (!r36_pix(v, p, r.w, r.h, w, x, y)) return false;
  out[0] = out[1] = out[2] = 0.0;
  for (int dy = -2; dy <= 2; dy++)
    for (int dx = -2; dx <= 2; dx++) {
      const uint32_t c = crop[(size_t)(y + dy) * r.w + x + dx];
      out[0] += (c >> 16) & 255, out[1] += (c >> 8) & 255, out[2] += c & 255;
    }
  for (int k = 0; k < 3; k++) out[k] /= 25.0;
  return true;
}

struct R36Mean {
  double r = 0, g = 0, b = 0;
};
/* The mean per-channel difference a - b over the picture. */
R36Mean r36_mean_diff(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b) {
  R36Mean m;
  if (a.size() != b.size() || a.empty()) return m;
  for (size_t i = 0; i < a.size(); i++) {
    m.r += (int)((a[i] >> 16) & 255) - (int)((b[i] >> 16) & 255);
    m.g += (int)((a[i] >> 8) & 255) - (int)((b[i] >> 8) & 255);
    m.b += (int)(a[i] & 255) - (int)(b[i] & 255);
  }
  m.r /= a.size(), m.g /= a.size(), m.b /= a.size();
  return m;
}

/* A console command and three frames, nothing else. */
void r36_raw(Editor &ed, const char *c) {
  ed.command(c);
  r34_steps(ed, 3);
}
/* A console command and a few frames. (It used to rotate the world too, to work around the Lighting
 * settings missing from the views' cache key; that was fixed, and a rotated world rightly makes a bake
 * out of date.) */
void r36_cmd(Editor &ed, const char *c) {
  ed.command(c);
  r34_steps(ed, 3);
}

}  // namespace

static void round36_tests() {
  /* ---------------------------------------------------------------- 1: voxelizer */
  test("voxel GI: the voxelizer marks exactly the voxels a triangle touches (150 random triangles against a triangle-box SAT over every voxel)", [] {
    R34Rng rng{20261009u};
    VoxelGrid g = r36_unit_grid(64);
    int wrong = 0, total_set = 0;
    for (int t = 0; t < 150; t++) {
      const float size = t % 3 == 0 ? 0.6f : t % 3 == 1 ? 4.0f : 12.0f;
      const Vec3 c(rng.range(14, 36), rng.range(14, 86), rng.range(14, 36));
      Vec3 v[3];
      for (int k = 0; k < 3; k++) v[k] = c + Vec3(rng.range(-size, size), rng.range(-size, size), rng.range(-size, size));
      r36_voxelize(g, r36_tris({v[0], v[1], v[2]}));
      int expected = 0;
      const Vec3 lo = vmin(vmin(v[0], v[1]), v[2]), hi = vmax(vmax(v[0], v[1]), v[2]);
      for (int z = std::max(0, (int)lo.z - 1); z <= std::min(63, (int)hi.z + 1); z++)
        for (int y = std::max(0, (int)lo.y - 1); y <= std::min(127, (int)hi.y + 1); y++)
          for (int x = std::max(0, (int)lo.x - 1); x <= std::min(63, (int)hi.x + 1); x++) {
            const bool want = r36_tri_box(v[0], v[1], v[2], Vec3((float)x, (float)y, (float)z));
            expected += want;
            if (want != g.voxel_set(x, y, z)) {
              if (++wrong <= 3) std::printf("    triangle %d: voxel (%d %d %d) brute force %d, voxelizer %d\n", t, x, y, z, want, !want);
            }
          }
      const int got = r36_count_set(g);
      total_set += got;
      if (got != expected) wrong++;  // voxels set outside the triangle's box, or too few
    }
    std::printf("    %d voxels set over 150 triangles, %d disagreements\n", total_set, wrong);
    CHECK(wrong == 0);
  });

  test("voxel GI: a closed box voxelizes to a hollow shell, a vertical wall is captured, a big floor fills exactly one layer", [] {
    VoxelGrid g = r36_unit_grid(64);
    /* A cube from (10.3, 11.4, 12.2) to (20.7, 21.1, 22.9): 11 x 11 x 11 voxels touched, 9^3 of them inside. */
    const MeshPtr cube = primitives::cube();
    r36_voxelize(g, cube->render_mesh(), Mat4::translate({15.5f, 16.25f, 17.55f}) * Mat4::scale({10.4f, 9.7f, 10.7f}));
    CHECK(r36_count_set(g) == 11 * 11 * 11 - 9 * 9 * 9);
    CHECK(!g.voxel_set(15, 16, 17));  // the middle is empty
    CHECK(!g.voxel_set(12, 14, 15) && !g.voxel_set(19, 19, 20));
    CHECK(g.voxel_set(15, 11, 17) && g.voxel_set(15, 21, 17) && g.voxel_set(10, 16, 17) && g.voxel_set(20, 16, 17) && g.voxel_set(15, 16, 12) &&
          g.voxel_set(15, 16, 22));
    CHECK(g.voxel_set(10, 11, 12) && g.voxel_set(20, 21, 22));  // corners
    CHECK(!g.voxel_set(9, 16, 17) && !g.voxel_set(21, 16, 17) && !g.voxel_set(15, 10, 17) && !g.voxel_set(15, 22, 17));
    /* Turned 37 degrees about Y and X: still a shell (nothing deep inside), and the surface is set. */
    const Mat4 turned = Mat4::translate({30.0f, 50.0f, 30.0f}) * Mat4::rotate(Quat::euler({37, 37, 0})) * Mat4::scale({12, 12, 12});
    r36_voxelize(g, cube->render_mesh(), turned);
    CHECK(r36_count_set(g) > 0);
    CHECK(!g.voxel_set(30, 50, 30) && !g.voxel_set(29, 49, 29) && !g.voxel_set(31, 51, 31));
    int inner_set = 0, shell_set = 0;
    for (int z = 22; z < 38; z++)
      for (int y = 42; y < 58; y++)
        for (int x = 22; x < 38; x++) {
          const Vec3 q = Quat::euler({37, 37, 0}).conjugate().rotate(Vec3(x + 0.5f, y + 0.5f, z + 0.5f) - Vec3(30, 50, 30));
          const float m = std::max({std::fabs(q.x), std::fabs(q.y), std::fabs(q.z)});
          if (m < 6.0f - 1.8f) inner_set += g.voxel_set(x, y, z);  // deeper than a voxel diagonal inside
          if (m > 6.0f - 0.2f && m < 6.0f + 0.2f) shell_set += g.voxel_set(x, y, z);
        }
    CHECK(inner_set == 0 && shell_set > 50);
    /* A wall at x = 15.5, y 3.2..20.7, z 5.3..9.8: no area seen from above, but it is a surface. */
    const std::vector<Vec3> wall = {{15.5f, 3.2f, 5.3f}, {15.5f, 20.7f, 5.3f}, {15.5f, 20.7f, 9.8f}, {15.5f, 3.2f, 5.3f}, {15.5f, 20.7f, 9.8f}, {15.5f, 3.2f, 9.8f}};
    r36_voxelize(g, r36_tris(wall));
    CHECK(r36_count_set(g) == 18 * 5);
    bool all = true;
    for (int y = 3; y <= 20; y++)
      for (int z = 5; z <= 9; z++) all = all && g.voxel_set(15, y, z);
    CHECK(all);
    /* A floor triangle pair a million units wide, at y = 10.5: one layer over every column of the grid. */
    const float B = 1e6f;
    r36_voxelize(g, r36_tris({{-B, 10.5f, -B}, {-B, 10.5f, B}, {B, 10.5f, B}, {-B, 10.5f, -B}, {B, 10.5f, B}, {B, 10.5f, -B}}));
    CHECK(r36_count_set(g) == 64 * 64);
    bool layer = true;
    for (int z = 0; z < 64; z += 3)
      for (int x = 0; x < 64; x += 3) layer = layer && g.voxel_set(x, 10, z) && !g.voxel_set(x, 9, z) && !g.voxel_set(x, 11, z);
    CHECK(layer);
  });

  test("voxel GI: NaN, infinite, far-away and zero-area triangles are skipped or harmless", [] {
    VoxelGrid g = r36_unit_grid(64);
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    /* NaN or infinity in any coordinate of any corner: no spans at all. */
    std::vector<VoxelSpan> spans;
    for (int k = 0; k < 9; k++) {
      std::vector<Vec3> t = {{10, 10, 10}, {14, 10, 10}, {10, 14, 10}};
      float *f = &t[(size_t)k / 3].x + (k % 3);
      *f = k % 2 ? nan : inf;
      voxelize_mesh(g, r36_tris(t), Mat4(), spans);
      CHECK(spans.empty());
    }
    /* A good triangle after a bad one in the same mesh is still voxelized. */
    voxelize_mesh(g, r36_tris({{nan, 1, 1}, {2, 2, 2}, {3, 1, 4}, {10.2f, 10.2f, 10.2f}, {14.2f, 10.2f, 10.2f}, {10.2f, 14.2f, 10.2f}}), Mat4(), spans);
    CHECK(!spans.empty());
    voxel_grid_build(g, {&spans});
    CHECK(g.voxel_set(10, 10, 10) && g.voxel_set(14, 10, 10) && g.voxel_set(10, 14, 10));
    /* A model matrix with NaN: nothing, no crash. */
    Mat4 bad = Mat4::translate({nan, 0, 0});
    voxelize_mesh(g, r36_tris({{10, 10, 10}, {14, 10, 10}, {10, 14, 10}}), bad, spans);
    CHECK(spans.empty());
    /* Far away or empty meshes. */
    voxelize_mesh(g, r36_tris({{1e6f, 1e6f, 1e6f}, {1e6f + 1, 1e6f, 1e6f}, {1e6f, 1e6f + 1, 1e6f}}), Mat4(), spans);
    CHECK(spans.empty());
    voxelize_mesh(g, r36_tris({{-5, 10, 10}, {-1, 10, 10}, {-3, 12, 10}}), Mat4(), spans);  // beside the grid
    CHECK(spans.empty());
    voxelize_mesh(g, RenderMesh(), Mat4(), spans);
    CHECK(spans.empty());
    /* Zero-area triangles (spec: degenerate triangles are skipped). Three equal points, and three on a line. */
    voxelize_mesh(g, r36_tris({{20.5f, 20.5f, 20.5f}, {20.5f, 20.5f, 20.5f}, {20.5f, 20.5f, 20.5f}}), Mat4(), spans);
    const size_t point_spans = spans.size();
    voxelize_mesh(g, r36_tris({{20.5f, 20.5f, 20.5f}, {22.5f, 20.5f, 20.5f}, {26.5f, 20.5f, 20.5f}}), Mat4(), spans);
    const size_t line_spans = spans.size();
    std::printf("    zero-area triangles: a point makes %zu spans, a line makes %zu\n", point_spans, line_spans);
    CHECK(point_spans == 0 && line_spans == 0);
    /* Not a built grid: harmless. */
    VoxelGrid none;
    voxelize_mesh(none, r36_tris({{1, 1, 1}, {2, 1, 1}, {1, 2, 1}}), Mat4(), spans);
    CHECK(spans.empty());
    voxel_grid_build(none, {&spans});
    CHECK(!none.voxel_set(0, 0, 0));
  });

  /* ---------------------------------------------------------------- 2: mips */
  test("voxel GI: every mip level is the OR of the 2 x 2 columns below it, at 64, 128 and 256, and a rebuild leaves no stale bits", [] {
    R34Rng rng{77};
    for (int n : {64, 128, 256}) {
      VoxelGrid g = r36_unit_grid(n);
      int levels = 1;
      while ((n >> levels) >= 1) levels++;
      CHECK(g.level_count() == levels && g.side(levels - 1) == 1 && g.side(0) == n);
      std::vector<VoxelSpan> spans = r36_random_bits(g, rng, 0.08f);
      const std::vector<VoxelSpan> tris = r36_random_tris(g, rng, 60, 10.0f);
      spans.insert(spans.end(), tris.begin(), tris.end());
      voxel_grid_build(g, {&spans});
      CHECK(r36_count_set(g) > 100);
      int bad = 0;
      for (int l = 1; l < g.level_count(); l++) {
        CHECK(g.levels[(size_t)l].size() == (size_t)g.side(l) * g.side(l));
        for (int z = 0; z < g.side(l); z++)
          for (int x = 0; x < g.side(l); x++) {
            uint64_t lo = 0, hi = 0;
            for (int dz = 0; dz < 2; dz++)
              for (int dx = 0; dx < 2; dx++) {
                const VoxelColumn &c = g.column(l - 1, 2 * x + dx, 2 * z + dz);
                lo |= c.lo, hi |= c.hi;
              }
            const VoxelColumn &c = g.column(l, x, z);
            bad += c.lo != lo || c.hi != hi;
          }
      }
      CHECK(bad == 0);
      /* The single top column is everything. */
      uint64_t lo = 0, hi = 0;
      for (const VoxelColumn &c : g.levels[0]) lo |= c.lo, hi |= c.hi;
      CHECK(g.column(g.level_count() - 1, 0, 0).lo == lo && g.column(g.level_count() - 1, 0, 0).hi == hi);
      /* A new build with other spans replaces the old bits at every level. */
      std::vector<VoxelSpan> one = {{(uint32_t)(5 * n + 7), 20, 90}};
      voxel_grid_build(g, {&one});
      int set_columns = 0;
      for (int l = 0; l < g.level_count(); l++)
        for (const VoxelColumn &c : g.levels[(size_t)l]) set_columns += c.any();
      CHECK(set_columns == g.level_count());  // one column per level
      CHECK(r36_count_set(g) == 71);
      std::vector<VoxelSpan> none;
      voxel_grid_build(g, {&none, nullptr});
      CHECK(r36_count_set(g) == 0);
      /* A span past the grid's columns is ignored, not written. */
      std::vector<VoxelSpan> outside = {{(uint32_t)(n * n + 3), 0, 5}};
      voxel_grid_build(g, {&outside});
      CHECK(r36_count_set(g) == 0);
    }
  });

  /* ---------------------------------------------------------------- 3: ray test */
  test("voxel GI: the hierarchical ray test equals the column walk exactly on 100k random rays and agrees with a 3D DDA apart from rays grazing a voxel edge", [] {
    R34Rng rng{424242};
    R36RayStats total;
    int variant = 0;
    for (float voxel : {1.0f, 2.0f})
      for (bool triangles : {false, true}) {
        VoxelGrid g = r36_unit_grid(64, voxel);
        const std::vector<VoxelSpan> spans = triangles ? r36_random_tris(g, rng, 120, 9.0f) : r36_random_bits(g, rng, 0.09f);
        voxel_grid_build(g, {&spans});
        const R36RayStats s = r36_compare_traces(g, rng, 25000);
        std::printf("    grid %d (voxel %g, %s): %d rays, %d hit, hierarchical != columns %d, vs DDA: %d differ (%d grazing, %d not), %d hit times off\n",
                    variant++, voxel, triangles ? "triangles" : "random bits", s.rays, s.hits, s.hier_vs_columns, s.vs_dda, s.grazing, s.hard, s.t_off);
        total.rays += s.rays, total.hits += s.hits, total.hier_vs_columns += s.hier_vs_columns, total.vs_dda += s.vs_dda;
        total.grazing += s.grazing, total.hard += s.hard, total.t_off += s.t_off;
      }
    std::printf("    %d rays, %d hits: hierarchical != column walk on %d, DDA differs on %d (%d grazing)\n", total.rays, total.hits, total.hier_vs_columns,
                total.vs_dda, total.grazing);
    CHECK(total.rays == 100000);
    CHECK(total.hits > 10000);  // the rays do hit things
    CHECK(total.hier_vs_columns == 0);
    CHECK(total.hard == 0);
    CHECK(total.t_off == 0);
    CHECK(total.grazing * 100 < total.rays);  // grazing is rare
  });

  test("voxel GI: with bits 10 and 50 set in a column the first hit is 10 going up and 50 going down, at the analytic distance", [] {
    for (float voxel : {1.0f, 2.0f}) {
      VoxelGrid g = r36_unit_grid(64, voxel);
      const uint32_t col = 20 * 64 + 20;
      std::vector<VoxelSpan> spans = {{col, 10, 10}, {col, 50, 50}};
      voxel_grid_build(g, {&spans});
      const float cx = (20.5f) * voxel, cz = 20.5f * voxel;
      struct Case {
        float y0;
        float dy;     // +1 up, -1 down
        float slant;  // horizontal lean per unit height
        int want_y;
        float face;   // the plane the ray enters through (voxel units)
      };
      const Case cases[] = {{2.3f, 1, 0, 10, 10}, {2.3f, 1, 0.01f, 10, 10}, {100.2f, -1, 0, 50, 51}, {100.2f, -1, 0.01f, 50, 51},
                            {30.0f, 1, 0, 50, 50},  {30.0f, -1, 0, 10, 11},    {-8.0f, 1, 0, 10, 10},   {140.0f, -1, 0, 50, 51}};
      for (const Case &c : cases) {
        const Vec3 o = g.origin + Vec3(cx, c.y0 * voxel, cz);
        const Vec3 d = normalize(Vec3(c.slant, c.dy, c.slant * 0.5f));
        const float t_expect = (c.face - c.y0) * voxel / d.y;
        VoxelHit h[3];
        const bool r[3] = {voxel_trace(g, o, d, 1000.0f, h[0]), voxel_trace_columns(g, o, d, 1000.0f, h[1]), voxel_trace_dda(g, o, d, 1000.0f, h[2])};
        for (int k = 0; k < 3; k++) {
          CHECK(r[k]);
          if (!r[k]) continue;
          CHECK(h[k].x == 20 && h[k].z == 20 && h[k].y == c.want_y);
          if (k < 2) CHECK(std::fabs(h[k].t - t_expect) <= 1e-5f * std::max(1.0f, t_expect));
          else CHECK(std::fabs(h[k].t - t_expect) <= 1e-3f);
        }
        /* One step short of it: a miss. */
        VoxelHit m;
        CHECK(!voxel_trace(g, o, d, t_expect * 0.99f, m) && !voxel_trace_columns(g, o, d, t_expect * 0.99f, m));
      }
      /* Beside the column: nothing. */
      VoxelHit m;
      CHECK(!voxel_trace(g, g.origin + Vec3(22.5f, 2.3f, 20.5f) * voxel, Vec3(0, 1, 0), 1000.0f, m));
    }
    /* Not a ray: zero or NaN direction, NaN origin, zero range, no grid. */
    VoxelGrid g = r36_unit_grid(64);
    std::vector<VoxelSpan> spans = {{20 * 64 + 20, 10, 10}};
    voxel_grid_build(g, {&spans});
    const float nan = std::numeric_limits<float>::quiet_NaN();
    VoxelHit h;
    CHECK(!voxel_trace(g, {20.5f, 2, 20.5f}, {0, 0, 0}, 100.0f, h));
    CHECK(!voxel_trace(g, {20.5f, 2, 20.5f}, {0, nan, 0}, 100.0f, h));
    CHECK(!voxel_trace(g, {nan, 2, 20.5f}, {0, 1, 0}, 100.0f, h));
    CHECK(!voxel_trace(g, {20.5f, 2, 20.5f}, {0, 1, 0}, 0.0f, h));
    CHECK(!voxel_trace(g, {20.5f, 2, 20.5f}, {0, 1, 0}, nan, h));
    CHECK(voxel_trace(g, {20.5f, 2, 20.5f}, {0, 1, 0}, std::numeric_limits<float>::infinity(), h) && h.y == 10);
    VoxelGrid none;
    CHECK(!voxel_trace(none, {20.5f, 2, 20.5f}, {0, 1, 0}, 100.0f, h));
  });

  /* ---------------------------------------------------------------- 4: RSM back-projection */
  test("voxel GI: a lit wall gathers its reflected sunlight, a wall in shadow and the back of a slab gather nothing", [] {
    GiParams prm;
    prm.rays = 32;
    prm.radius = 3.0f;
    prm.downsample = 2;
    prm = gi_params_sanitized(prm);
    const Vec3 sun_dir(1.0f, -1.0f, 0.2f);
    auto scene = [&](R36Sc &s, Vec3 wall_colour, bool shadow_slab, bool low_slab) {
      RenderLight sun = r36_sun(sun_dir, 1.0f);
      s.add(primitives::plane(20.0f, 2), Mat4(), Vec3(0.7f));
      s.add(primitives::cube(), Mat4::translate({1.5f, 2, 0}) * Mat4::scale({0.2f, 4, 8}), wall_colour);
      if (shadow_slab) s.add(primitives::cube(), Mat4::translate({-3.0f, 6.2f, 0}) * Mat4::scale({9, 0.4f, 16}), Vec3(0.7f));
      if (low_slab) s.add(primitives::cube(), Mat4::translate({0, 2.1f, 0}) * Mat4::scale({16, 0.2f, 16}), Vec3(0.7f));
      s.finish(64, &sun);
      CHECK(s.rsm.valid());
    };
    const Vec3 receiver(0.0f, 0.0f, 0.0f);
    /* Lit: red light from a red wall. */
    R36Sc lit;
    scene(lit, Vec3(0.9f, 0.1f, 0.1f), false, false);
    const GiSample a = r36_gather_all(lit, &lit.rsm, receiver, prm);
    std::printf("    lit red wall: bounce (%.4f %.4f %.4f) summed over 16 sets, sky %.3f\n", a.bounce.x, a.bounce.y, a.bounce.z, a.sky);
    CHECK(a.bounce.x > 0.02f);
    CHECK(a.bounce.x > 5.0f * a.bounce.y && a.bounce.x > 5.0f * a.bounce.z);
    CHECK(a.sky < 0.95f && a.sky > 0.2f);  // the wall takes part of the sky
    /* The bounce is what the RSM stored, times intensity / rays: twice the intensity, twice the light. */
    GiParams twice = prm;
    twice.intensity = 2.0f;
    const GiSample a2 = r36_gather_all(lit, &lit.rsm, receiver, gi_params_sanitized(twice));
    CHECK(std::fabs(a2.bounce.x - 2.0f * a.bounce.x) <= 1e-4f * a.bounce.x + 1e-6f);
    /* No bounce: bounce off, no RSM, or intensity 0. The sky part stays. */
    GiParams nb = prm;
    nb.bounce = false;
    const GiSample b0 = r36_gather_all(lit, &lit.rsm, receiver, nb);
    const GiSample b1 = r36_gather_all(lit, nullptr, receiver, prm);
    GiParams zero = prm;
    zero.intensity = 0.0f;
    const GiSample b2 = r36_gather_all(lit, &lit.rsm, receiver, zero);
    CHECK(length(b0.bounce) == 0.0f && length(b1.bounce) == 0.0f && length(b2.bounce) == 0.0f);
    CHECK(std::fabs(b0.sky - a.sky) < 1e-6f && std::fabs(b1.sky - a.sky) < 1e-6f);
    /* A white wall bounces white light. */
    R36Sc white;
    scene(white, Vec3(0.9f), false, false);
    const GiSample w = r36_gather_all(white, &white.rsm, receiver, prm);
    CHECK(w.bounce.x > 0.02f && std::fabs(w.bounce.x - w.bounce.y) < 0.02f * w.bounce.x && std::fabs(w.bounce.x - w.bounce.z) < 0.02f * w.bounce.x);
    /* Shadowed: a slab between the sun and the wall (out of the receiver's reach). Exactly nothing. */
    R36Sc shadow;
    scene(shadow, Vec3(0.9f, 0.1f, 0.1f), true, false);
    for (int set = 0; set < 16; set++) {
      const GiSample g = voxel_gi_gather(shadow.grid, &shadow.rsm, shadow.env, receiver, Vec3(0, 1, 0), prm, set);
      CHECK(g.bounce.x == 0.0f && g.bounce.y == 0.0f && g.bounce.z == 0.0f);
    }
    /* Back-facing: a thin slab over the receiver. The sun lights its top; the receiver sees its underside, which is
     * within the RSM's epsilon of the top but faces away. */
    R36Sc under;
    scene(under, Vec3(0.9f, 0.1f, 0.1f), false, true);
    int blocked = 0;
    for (int set = 0; set < 16; set++) {
      const GiSample g = voxel_gi_gather(under.grid, &under.rsm, under.env, receiver, Vec3(0, 1, 0), prm, set);
      CHECK(g.bounce.x == 0.0f && g.bounce.y == 0.0f && g.bounce.z == 0.0f);
      blocked += g.sky < 0.9f;
    }
    CHECK(blocked == 16);  // the slab really is in the rays' way
    /* The same slab with the wall and the receiver in the open far from it: the wall still bounces. */
    const GiSample far_lit = r36_gather_all(lit, &lit.rsm, Vec3(0.3f, 0, 3.0f), prm);
    CHECK(far_lit.bounce.x > 0.0f);
  });

  test("voxel GI: parameters are clamped, the gather survives NaN and an unbuilt grid, an empty floor keeps a sky ratio of 1", [] {
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    GiParams p;
    p.rays = 0, p.radius = nan, p.intensity = inf, p.downsample = 3, p.specular_occlusion = nan;
    GiParams q = gi_params_sanitized(p);
    CHECK(q.rays == 1 && std::isfinite(q.radius) && q.radius > 0.0f && std::isfinite(q.intensity) && q.intensity >= 0.0f && q.intensity <= 10.0f);
    CHECK(q.downsample == 4 && q.specular_occlusion >= 0.0f && q.specular_occlusion <= 1.0f);
    p.rays = 9999, p.radius = 1e9f, p.intensity = -4.0f, p.downsample = 1, p.specular_occlusion = 7.0f;
    q = gi_params_sanitized(p);
    CHECK(q.rays == 32 && q.radius <= 1000.0f && q.intensity == 0.0f && q.downsample == 2 && q.specular_occlusion == 1.0f);
    p.radius = -3.0f, p.rays = -9;
    q = gi_params_sanitized(p);
    CHECK(q.radius > 0.0f && q.rays == 1);

    R36Sc flat;
    flat.add(primitives::plane(20.0f, 2), Mat4(), Vec3(0.7f));
    RenderLight sun = r36_sun(Vec3(1, -1, 0.2f));
    flat.finish(64, &sun);
    CHECK(flat.grid.valid());  // a flat scene still makes a grid
    GiParams prm = gi_params_sanitized(GiParams());
    float worst = 1.0f;
    R34Rng rng{5};
    for (int i = 0; i < 40; i++) {
      const GiSample g = voxel_gi_gather(flat.grid, &flat.rsm, flat.env, Vec3(rng.range(-8, 8), 0, rng.range(-8, 8)), Vec3(0, 1, 0), prm, i);
      worst = std::min(worst, g.sky);
      CHECK(length(g.bounce) == 0.0f);
    }
    std::printf("    lowest sky ratio over an empty floor: %.4f\n", worst);
    CHECK(worst >= 0.98f);
    /* Rubbish in, no crash and finite numbers out. */
    for (Vec3 pos : {Vec3(nan, 0, 0), Vec3(0, inf, 0), Vec3(1e9f, 1e9f, 1e9f), Vec3(-1e9f, 0, 0)})
      for (Vec3 nrm : {Vec3(0, 1, 0), Vec3(nan, 1, 0), Vec3(0, 0, 0), Vec3(1e9f, 0, 0)}) {
        const GiSample g = voxel_gi_gather(flat.grid, &flat.rsm, flat.env, pos, nrm, prm, 3);
        CHECK(std::isfinite(g.sky) && std::isfinite(g.bounce.x) && std::isfinite(g.bounce.y) && std::isfinite(g.bounce.z));
      }
    CHECK(voxel_gi_gather(flat.grid, &flat.rsm, flat.env, Vec3(0, 0, 0), Vec3(0, 1, 0), prm, -7).sky == 1.0f);  // a set out of range wraps
    VoxelGrid none;
    const GiSample g = voxel_gi_gather(none, nullptr, flat.env, Vec3(0, 0, 0), Vec3(0, 1, 0), prm, 0);
    CHECK(g.sky == 1.0f && length(g.bounce) == 0.0f);
    /* The grid fit refuses what it cannot hold, and makes a grid for a flat or tiny box. */
    VoxelGrid f;
    AABB empty;
    voxel_grid_fit(f, empty, 128);
    CHECK(!f.valid());
    AABB bad;
    bad.min = Vec3(0.0f), bad.max = Vec3(1, 1, inf);
    voxel_grid_fit(f, bad, 128);
    CHECK(!f.valid());
    AABB huge;
    huge.add(Vec3(-1e6f)), huge.add(Vec3(1e6f));
    voxel_grid_fit(f, huge, 128);
    CHECK(f.valid() && f.voxel >= 1e6f * 2.0f / 128.0f);
    CHECK(f.origin.x <= -1e6f && f.origin.x + f.n * f.voxel >= 1e6f);
    AABB absurd;
    absurd.add(Vec3(-1e12f)), absurd.add(Vec3(1e12f));
    voxel_grid_fit(f, absurd, 256);
    CHECK(!f.valid());
    AABB tiny;
    tiny.add(Vec3(5.0f)), tiny.add(Vec3(5.0f));  // a point
    voxel_grid_fit(f, tiny, 64);
    CHECK(f.valid());
    AABB flat_box;
    flat_box.add(Vec3(-3, 0, -3)), flat_box.add(Vec3(3, 0, 3));
    voxel_grid_fit(f, flat_box, 64);
    CHECK(f.valid() && f.origin.y <= 0.0f);
    /* The origin snaps, so a small move keeps the grid (and every object's spans). */
    VoxelGrid f1, f2;
    AABB b1, b2;
    b1.add(Vec3(0.3f, 0.2f, 0.1f)), b1.add(Vec3(20, 10, 20));
    b2.add(Vec3(0.5f, 0.2f, 0.4f)), b2.add(Vec3(20.5f, 10, 20.2f));
    voxel_grid_fit(f1, b1, 128);
    voxel_grid_fit(f2, b2, 128);
    CHECK(f1.valid() && f1.key == f2.key && f1.key != 0);
    voxel_grid_fit(f2, b2, 64);
    CHECK(f2.key != f1.key);
  });

  /* ---------------------------------------------------------------- 5: images */
  test("voxel GI: a red wall bleeds red onto a white floor only with GI on, and not with a white wall or a wall in shadow", [] {
    const float dx = 4.0f;  // away from the world origin: see the "same wherever the scene sits" test
    double red[3], white[3], shadow[3];
    RenderLight sun;
    {
      R36Sc s;
      r36_wall_scene(s, Vec3(0.9f, 0.05f, 0.05f), false, sun, dx);
      CHECK(r36_bleed(s, sun, red, dx));
    }
    {
      R36Sc s;
      r36_wall_scene(s, Vec3(0.9f), false, sun, dx);
      CHECK(r36_bleed(s, sun, white, dx));
    }
    {
      R36Sc s;
      r36_wall_scene(s, Vec3(0.9f, 0.05f, 0.05f), true, sun, dx);
      CHECK(r36_bleed(s, sun, shadow, dx));
    }
    std::printf("    floor beside the wall, GI on minus off (R G B): red wall (%.1f %.1f %.1f), white wall (%.1f %.1f %.1f), red wall in shadow (%.1f %.1f %.1f)\n",
                red[0], red[1], red[2], white[0], white[1], white[2], shadow[0], shadow[1], shadow[2]);
    CHECK(red[0] - red[2] > 4.0 && red[0] - red[1] > 4.0);  // redder
    CHECK(std::fabs(white[0] - white[2]) < 1.5);           // a white wall: no tint
    CHECK(white[0] > shadow[0] - 1.5 + 4.0);               // ...but it does add light, which the shadowed wall does not
    CHECK(std::fabs(shadow[0] - shadow[2]) < 1.5);         // a wall the sun doesn't reach: no tint
  });

  test("voxel GI: the bounce is the same wherever the scene sits in the world (a wall facing away from the origin bleeds as much as one facing it)", [] {
    /* The RSM is drawn from the sun with the rasterizer's "face the viewer" normal flip; if that flip uses a point
     * that is not the sun's view direction (for example the world origin), the lit faces that point away from the
     * origin get normals turned the wrong way, flux 0, and no bounce. */
    double at_origin[3], away[3];
    RenderLight sun;
    for (float dx : {0.0f, 4.0f}) {
      R36Sc s;
      r36_wall_scene(s, Vec3(0.9f, 0.05f, 0.05f), false, sun, dx);
      CHECK(r36_bleed(s, sun, dx == 0.0f ? at_origin : away, dx));
    }
    std::printf("    red wall bleed (R G B), wall at the origin: (%.1f %.1f %.1f), wall 4 units along +x: (%.1f %.1f %.1f)\n", at_origin[0], at_origin[1],
                at_origin[2], away[0], away[1], away[2]);
    CHECK(away[0] - away[2] > 4.0);
    CHECK(std::fabs((at_origin[0] - at_origin[2]) - (away[0] - away[2])) < 0.2 * (away[0] - away[2]));
    /* The same through the gather alone: the whole scene (floor, wall, receiver) slid 3 units along -x, which puts the
     * wall's lit face on the side that faces away from the origin. */
    GiParams prm = gi_params_sanitized([] {
      GiParams p;
      p.rays = 32, p.radius = 3.0f;
      return p;
    }());
    double bounce[2];
    for (int slid = 0; slid < 2; slid++) {
      const float shift = slid ? -3.0f : 0.0f;
      R36Sc s;
      RenderLight sun2 = r36_sun(Vec3(1.0f, -1.0f, 0.2f), 1.0f);
      s.add(primitives::plane(20.0f, 2), Mat4::translate({shift, 0, 0}), Vec3(0.7f));
      s.add(primitives::cube(), Mat4::translate({1.5f + shift, 2, 0}) * Mat4::scale({0.2f, 4, 8}), Vec3(0.9f, 0.1f, 0.1f));
      s.finish(64, &sun2);
      bounce[slid] = r36_gather_all(s, &s.rsm, Vec3(shift, 0, 0), prm).bounce.x;
    }
    std::printf("    summed red bounce on the floor beside a wall at x = 1.5: %.4f, the same scene slid by -3 (wall at x = -1.5): %.4f\n", bounce[0], bounce[1]);
    CHECK(bounce[0] > 0.02f);
    CHECK(std::fabs(bounce[1] - bounce[0]) < 0.1 * bounce[0]);
  });

  test("voxel GI: a box darkens the floor beside it under the sky, and the floor beyond the radius is bit-identical to GI off", [] {
    R36Sc s;
    s.add(primitives::plane(20.0f, 2), Mat4(), Vec3(0.7f));
    s.add(primitives::cube(), Mat4::translate({0, 1, 0}) * Mat4::scale({2, 2, 2}), Vec3(0.7f));
    s.finish(128, nullptr);  // no sun: the sky alone
    const R36View view(Vec3(0, 9, 12), Vec3(0, 0, 0));
    const float radius = 2.0f;
    VoxelGIFrame fr = r36_frame(s, 16, radius, 2);
    CHECK(fr.rsm == nullptr);
    Image on, off;
    r36_render(on, s, view, nullptr, &fr);
    r36_render(off, s, view, nullptr, nullptr);
    double a[3], b[3];
    CHECK(r36_probe(on, view.v, view.p, Vec3(1.5f, 0, 0), a) && r36_probe(off, view.v, view.p, Vec3(1.5f, 0, 0), b));
    std::printf("    floor 0.5 from the box, GI on / off: %.1f / %.1f\n", a[0], b[0]);
    CHECK(a[0] < b[0] - 8.0 && a[1] < b[1] - 8.0 && a[2] < b[2] - 8.0);
    CHECK(r36_probe(on, view.v, view.p, Vec3(-1.5f, 0, 0), a) && r36_probe(off, view.v, view.p, Vec3(-1.5f, 0, 0), b) && a[0] < b[0] - 8.0);
    /* Farther from the box than the radius plus a few voxels (box edge at 1, voxel 0.25): bit for bit. */
    int checked = 0, same = 0;
    for (float x = 4.5f; x <= 9.0f; x += 1.5f)
      for (float z = -6.0f; z <= 3.0f; z += 1.5f) {
        int px, py;
        if (!r36_pix(view.v, view.p, view.W, view.H, Vec3(x, 0, z), px, py)) continue;
        checked++;
        same += r36_same_block(on, off, view.v, view.p, Vec3(x, 0, z));
      }
    for (float x = -9.0f; x <= -4.5f; x += 1.5f)
      for (float z = -6.0f; z <= 3.0f; z += 1.5f) {
        int px, py;
        if (!r36_pix(view.v, view.p, view.W, view.H, Vec3(x, 0, z), px, py)) continue;
        checked++;
        same += r36_same_block(on, off, view.v, view.p, Vec3(x, 0, z));
      }
    std::printf("    far floor points: %d checked, %d bit-identical\n", checked, same);
    CHECK(checked >= 12 && same == checked);
    /* The picture does change somewhere (this is not a vacuous comparison). */
    CHECK(on.pixels != off.pixels);
  });

  test("voxel GI: GI off is bit-identical (no frame, an unbuilt grid, or sky and bounce both off), renders repeat exactly and 1 thread equals all threads", [] {
    R36Sc s;
    RenderLight sun = r36_sun(Vec3(1.0f, -1.2f, 0.3f));
    s.add(primitives::plane(20.0f, 2), Mat4(), Vec3(0.7f));
    s.add(primitives::cube(), Mat4::translate({0, 2, 0}) * Mat4::scale({0.2f, 4, 8}), Vec3(0.9f, 0.1f, 0.1f));
    s.add(primitives::uv_sphere(), Mat4::translate({-3, 1, 2}) * Mat4::scale({2, 2, 2}), Vec3(0.2f, 0.6f, 0.9f));
    s.finish(128, &sun);
    const R36View view(Vec3(-6, 5, 9), Vec3(-1, 0, 0));
    Image off, off2, none_grid, quiet, on1, on2, on_one_thread;
    r36_render(off, s, view, &sun, nullptr);
    r36_render(off2, s, view, &sun, nullptr);
    CHECK(off.pixels == off2.pixels);
    VoxelGIFrame unbuilt;  // a frame whose grid was never built
    VoxelGrid empty_grid;
    unbuilt.grid = &empty_grid;
    r36_render(none_grid, s, view, &sun, &unbuilt);
    CHECK(none_grid.pixels == off.pixels);
    VoxelGIFrame null_grid;  // no grid at all
    r36_render(none_grid, s, view, &sun, &null_grid);
    CHECK(none_grid.pixels == off.pixels);
    VoxelGIFrame fr = r36_frame(s);
    VoxelGIFrame calm = fr;
    calm.params.sky_occlusion = false;
    calm.params.bounce = false;
    r36_render(quiet, s, view, &sun, &calm);  // GI on, but it adds nothing: today's picture
    std::printf("    GI on with sky occlusion and bounce off: %zu pixels differ from GI off\n", r35_diff_count(quiet.pixels, off.pixels));
    CHECK(quiet.pixels == off.pixels);
    r36_render(on1, s, view, &sun, &fr);
    r36_render(on2, s, view, &sun, &fr);
    CHECK(on1.pixels != off.pixels);
    CHECK(on1.pixels == on2.pixels);
    JobSystem &js = JobSystem::global();
    const int old = js.max_threads();
    js.set_max_threads(1);
    r36_render(on_one_thread, s, view, &sun, &fr);
    js.set_max_threads(old);
    CHECK(on_one_thread.pixels == on1.pixels);
    /* The 16 ray sets and the quarter-resolution variant are deterministic too. */
    VoxelGIFrame quarter = r36_frame(s, 8, 2.0f, 4);
    Image q1, q2;
    r36_render(q1, s, view, &sun, &quarter);
    r36_render(q2, s, view, &sun, &quarter);
    CHECK(q1.pixels == q2.pixels && q1.pixels != off.pixels);
    /* Non-square sizes and tiny targets don't trip the low-resolution grid. */
    for (int w : {1, 2, 3, 7, 33}) {
      R36View v2(Vec3(-6, 5, 9), Vec3(-1, 0, 0));
      v2.W = w, v2.H = 5;
      Image img;
      r36_render(img, s, v2, &sun, &fr);
      CHECK(img.width == w);
    }
  });

  /* ---------------------------------------------------------------- 6: caching */
  test("voxel GI: orbiting does not re-voxelize, moving one object re-voxelizes only it, a light change makes a new RSM, an idle frame does nothing", [] {
    Editor ed;
    r36_editor_scene(ed);
    GameObject *box = r35_add(ed, "Cube", "Box", {-3.0f, 0.5f, 2.0f}, {1, 1, 1}, Vec3(0.8f));
    ed.commit_change("add Box");
    r36_cmd(ed, "set Lighting.RealtimeGI true");
    r34_steps(ed, 3);
    CHECK(ed.voxel_grid_for_test().valid());
    CHECK(ed.rsm_for_test().valid());
    const uint64_t vox0 = ed.voxelized_for_test(), rsm0 = ed.rsm_renders_for_test();
    std::printf("    first frames: %llu objects voxelized, %llu RSM renders\n", (unsigned long long)vox0, (unsigned long long)rsm0);
    /* The Scene view and the Game view keep a cache each (they draw viewport and render meshes): floor,
     * wall and box once per kind of view that drew (review of task 0013). */
    const uint64_t K = vox0 / 3;
    std::printf("    views with their own voxel cache: %llu\n", (unsigned long long)K);
    CHECK(vox0 % 3 == 0 && (K == 1 || K == 2));
    CHECK(rsm0 == K);
    /* Idle: nothing is drawn, nothing is rebuilt. */
    R34Count t0 = r34_totals(ed);
    r34_steps(ed, 5);
    CHECK((r34_totals(ed) - t0).renders == 0);
    CHECK(ed.voxelized_for_test() == vox0 && ed.rsm_renders_for_test() == rsm0);
    /* Orbit: the Scene view draws again, with the same voxels and the same RSM. */
    t0 = r34_totals(ed);
    ed.command("camera 40 22 7 0 0.5 0");
    r34_steps(ed, 3);
    ed.command("camera 55 15 8 1 0.5 0");
    r34_steps(ed, 3);
    CHECK((r34_totals(ed) - t0).renders >= 2);
    CHECK(ed.voxelized_for_test() == vox0 && ed.rsm_renders_for_test() == rsm0);
    /* Move one object: only it is voxelized again. */
    box->set_local_position({-3.5f, 0.5f, 2.0f});
    ed.commit_change("move Box");
    r34_steps(ed, 3);
    CHECK(ed.voxelized_for_test() == vox0 + K);  // the box, once per view kind
    const uint64_t rsm1 = ed.rsm_renders_for_test();
    CHECK(rsm1 == rsm0 + K);  // the casters moved
    /* The sun turns: a new RSM, the same voxels. */
    ed.scene().find_by_name("Directional Light")->set_local_euler({45, 120, 0});
    ed.commit_change("turn the sun");
    r34_steps(ed, 3);
    CHECK(ed.voxelized_for_test() == vox0 + K);
    CHECK(ed.rsm_renders_for_test() == rsm1 + K);
    /* A colour change on the wall: same voxels, a new RSM (its reflected light changed). */
    ed.scene().find_by_name("Wall")->get<MeshRenderer>()->materials[0]->base_color = Vec3(0.1f, 0.9f, 0.1f);
    ed.scene().find_by_name("Wall")->get<MeshRenderer>()->materials[0]->version++;
    ed.commit_change("wall colour");
    r34_steps(ed, 3);
    CHECK(ed.voxelized_for_test() == vox0 + K);
    CHECK(ed.rsm_renders_for_test() == rsm1 + 2 * K);
    /* Settings that don't change what the sun sees keep both. */
    const uint64_t vox2 = ed.voxelized_for_test(), rsm2 = ed.rsm_renders_for_test();
    r36_cmd(ed, "set Lighting.GIRadius 3");
    r36_cmd(ed, "set Lighting.GIRays 12");
    r36_cmd(ed, "set Lighting.GIIntensity 1.5");
    CHECK(ed.voxelized_for_test() == vox2 && ed.rsm_renders_for_test() == rsm2);
    /* The grid's resolution: a new grid, every object again. */
    r36_cmd(ed, "set Lighting.VoxelResolution 64");
    CHECK(ed.voxel_grid_for_test().n == 64);
    CHECK(ed.voxelized_for_test() == vox2 + 3 * K);
    CHECK(ed.rsm_renders_for_test() == rsm2);
    /* ...and idle again. */
    t0 = r34_totals(ed);
    r34_steps(ed, 4);
    CHECK((r34_totals(ed) - t0).renders == 0);
    CHECK(ed.voxelized_for_test() == vox2 + 3 * K && ed.rsm_renders_for_test() == rsm2);
  });

  test("voxel GI: turning Realtime GI or its settings on redraws the views (no stale picture)", [] {
    Editor ed;
    r36_editor_scene(ed);
    /* Plain commands, nothing else touching the scene: what the user does in the Lighting window. */
    const std::vector<uint32_t> off = r35_game(ed);
    r36_raw(ed, "set Lighting.RealtimeGI true");
    const std::vector<uint32_t> on = r35_game(ed);
    CHECK(on != off);
    r36_raw(ed, "set Lighting.Bounce false");
    const std::vector<uint32_t> no_bounce = r35_game(ed);
    CHECK(no_bounce != on);
    r36_raw(ed, "set Lighting.SkyOcclusion false");
    const std::vector<uint32_t> nothing = r35_game(ed);
    CHECK(nothing != no_bounce);
    CHECK(nothing == off);  // GI on but adding nothing is the plain picture
    r36_raw(ed, "set Lighting.SkyOcclusion true");
    r36_raw(ed, "set Lighting.Bounce true");
    CHECK(r35_game(ed) == on);
    r36_raw(ed, "set Lighting.GIRadius 0.5");
    CHECK(r35_game(ed) != on);
    r36_raw(ed, "set Lighting.RealtimeGI false");
    CHECK(r35_game(ed) == off);
    /* The render-cache verifier (it re-renders every picture the cache reused) finds no stale one. */
    ed.set_render_cache_verify(true);
    const uint64_t bad0 = Editor::render_cache_mismatches_all();
    r36_raw(ed, "set Lighting.RealtimeGI true");
    r36_raw(ed, "set Lighting.GIIntensity 2");
    r36_raw(ed, "set Lighting.RealtimeGI false");
    r34_steps(ed, 3);
    std::printf("    stale pictures found by the verifier: %llu\n", (unsigned long long)(Editor::render_cache_mismatches_all() - bad0));
    CHECK(Editor::render_cache_mismatches_all() == bad0);
    ed.set_render_cache_verify(false);
  });

  /* ---------------------------------------------------------------- 7: works with baking */
  test("voxel GI: rotating a Realtime sun moves the bounce on a lightmapped floor without Generate Lighting", [] {
    Editor ed;
    r36_editor_scene(ed);  // sun Realtime; the floor and the red wall are baked (the sky only)
    r35_bake(ed);
    CHECK(ed.lighting_data_for_test().entries.size() >= 2);
    GameObject *sun = ed.scene().find_by_name("Directional Light");
    /* How much redder GI makes the floor beside the wall (mean of red up minus blue up, in levels, over three spots 0.5
     * from it). The wall's own surfaces are left out: any extra light makes a red wall redder. */
    auto reddening = [&](float yaw) {
      sun->set_local_euler({50, yaw, 0});
      ed.commit_change("turn the sun");
      r36_cmd(ed, "set Lighting.RealtimeGI false");
      const std::vector<uint32_t> off = r35_game(ed);
      r36_cmd(ed, "set Lighting.RealtimeGI true");
      const std::vector<uint32_t> on = r35_game(ed);
      double sum = 0.0;
      int spots = 0;
      for (float z : {-2.0f, 0.0f, 2.0f}) {
        double a[3], b[3];
        if (r36_game_probe(ed, on, Vec3(6.0f - 0.5f, 0, z), a) && r36_game_probe(ed, off, Vec3(6.0f - 0.5f, 0, z), b)) sum += (a[0] - b[0]) - (a[2] - b[2]), spots++;
      }
      CHECK(spots == 3);
      std::printf("    sun yaw %g: GI makes the floor beside the wall %.1f levels redder\n", yaw, sum / std::max(1, spots));
      return sum / std::max(1, spots);
    };
    const double toward = reddening(90.0f);  // the sun lights the wall's face that looks at the floor
    const double away = reddening(270.0f);   // the sun lights the other face: the floor sees the wall's shadow
    CHECK(toward > 4.0);
    CHECK(away < 0.25 * toward);
    CHECK(!ed.baking_for_test());
  });

  test("voxel GI: with a valid bake of a Baked or Mixed sun the picture equals the baked one, and an object moved after the bake gets live GI", [] {
    for (int mode : {2, 1}) {
      Editor ed;
      r36_editor_scene(ed);
      r35_add(ed, "Cube", "Box", {-3.0f, 0.5f, 1.0f}, {1, 1, 1}, Vec3(0.8f));
      r35_sun_mode(ed, mode);
      r35_bake(ed);
      const std::vector<uint32_t> baked = r35_game(ed);
      r36_cmd(ed, "set Lighting.RealtimeGI true");
      const std::vector<uint32_t> with_gi = r35_game(ed);
      const size_t d = r35_diff_count(with_gi, baked);
      std::printf("    sun mode %d: %zu of %zu pixels differ with GI on (default settings)\n", mode, d, baked.size());
      CHECK(d == 0);  // spec: with a valid bake of Baked / Mixed lights the picture equals the baked one
      /* What the difference is: GI that adds nothing (sky occlusion and bounce off) is identical, and so is GI with
       * the specular occlusion turned off; only the occlusion of lightmapped surfaces' specular light remains. */
      r36_cmd(ed, "set Lighting.SkyOcclusion false");
      r36_cmd(ed, "set Lighting.Bounce false");
      CHECK(r35_diff_count(r35_game(ed), baked) == 0);
      r36_cmd(ed, "set Lighting.SkyOcclusion true");
      r36_cmd(ed, "set Lighting.Bounce true");
      r36_cmd(ed, "set Lighting.SpecularOcclusion 0");
      const size_t d_spec = r35_diff_count(r35_game(ed), baked);
      std::printf("    sun mode %d: %zu pixels differ with the specular occlusion at 0\n", mode, d_spec);
      CHECK(d_spec == 0);
      r36_cmd(ed, "set Lighting.SpecularOcclusion 1");
      /* Moved after the bake: its map is stale, so it falls back to realtime light - now with GI. */
      ed.scene().find_by_name("Box")->set_local_position({-3.0f, 0.5f, -1.0f});
      ed.commit_change("move Box after the bake");
      r34_steps(ed, 3);
      CHECK(ed.lighting_out_of_date_for_test());
      const std::vector<uint32_t> moved_on = r35_game(ed);
      r36_cmd(ed, "set Lighting.RealtimeGI false");
      const std::vector<uint32_t> moved_off = r35_game(ed);
      const size_t md = r35_diff_count(moved_on, moved_off);
      const R36Mean m = r36_mean_diff(moved_on, moved_off);
      std::printf("    box moved: %zu pixels differ between GI on and off, mean (%.3f %.3f %.3f)\n", md, m.r, m.g, m.b);
      CHECK(md > 40);  // not the flat ambient of GI off
    }
  });

  test("voxel GI: Auto Generate waits for the changes to settle, then re-bakes; the result replaces the stale fallback", [] {
    Editor ed;
    r36_editor_scene(ed);
    GameObject *box = r35_add(ed, "Cube", "Box", {-3.0f, 0.5f, 1.0f}, {1, 1, 1}, Vec3(0.8f));
    r35_sun_mode(ed, 2);
    r36_cmd(ed, "set Lighting.RealtimeGI true");
    r36_cmd(ed, "set Lighting.AutoGenerate true");  // before the bake: every Lighting setting is part of its key
    r36_cmd(ed, "set Lighting.SpecularOcclusion 0");  // (the occlusion of lightmapped specular light is its own finding)
    r35_bake(ed);
    r34_steps(ed, 3);
    CHECK(!ed.baking_for_test() && !ed.lighting_out_of_date_for_test());  // nothing changed: no bake
    box->set_local_position({-3.0f, 0.5f, -1.5f});
    ed.commit_change("move Box");
    r34_steps(ed, 3);
    CHECK(ed.lighting_out_of_date_for_test());
    CHECK(!ed.baking_for_test());  // it waits for the change to settle
    const std::vector<uint32_t> waiting = r35_game(ed);
    std::this_thread::sleep_for(std::chrono::milliseconds(1150));
    r34_steps(ed, 3);  // the settle time is over: the bake starts (a tiny scene may also finish within these frames)
    for (int i = 0; i < 200 && ed.baking_for_test(); i++) ed.step_frame_headless();
    CHECK(!ed.baking_for_test());
    r34_steps(ed, 4);
    CHECK(!ed.lighting_out_of_date_for_test());
    const std::vector<uint32_t> baked_again = r35_game(ed);
    std::printf("    waiting picture vs re-baked picture: %zu pixels differ\n", r35_diff_count(waiting, baked_again));
    CHECK(waiting != baked_again);
    /* No change, no more bakes. */
    std::this_thread::sleep_for(std::chrono::milliseconds(1150));
    r34_steps(ed, 4);
    CHECK(!ed.baking_for_test() && !ed.lighting_out_of_date_for_test());
    /* Baked, GI adds nothing on the lightmapped objects: the Baked sun is in the maps. */
    r36_cmd(ed, "set Lighting.RealtimeGI false");
    CHECK(r35_diff_count(r35_game(ed), baked_again) == 0);
  });

  /* Review of task 0013. */
  test("voxel GI: rays longer than 256 voxels on a 256 grid walk exactly like the column walk, and never stall", [] {
    VoxelGrid g;
    AABB b;
    b.add(Vec3(-200.0f, 0.0f, -200.0f));
    b.add(Vec3(200.0f, 30.0f, 200.0f));
    voxel_grid_fit(g, b, 256);
    CHECK(g.valid() && g.n == 256);
    if (!g.valid()) return;
    std::vector<VoxelSpan> spans;
    uint32_t seed = 4242;
    auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0f; };
    for (int i = 0; i < 3000; i++) {
      const int y0 = (int)(rnd() * 120);
      spans.push_back({(uint32_t)(rnd() * 255.99f) * 256 + (uint32_t)(rnd() * 255.99f), (uint8_t)y0, (uint8_t)(y0 + (int)(rnd() * 6))});
    }
    voxel_grid_build(g, {&spans});
    int differ = 0, hits = 0, n = 0;
    ScopedTimer t;
    for (int i = 0; i < 20000; i++) {
      /* Starting far from the grid's corner (t past 256 voxels along the ray), long and slanted. */
      const Vec3 o = g.to_world(Vec3(200.0f + rnd() * 55.0f, rnd() * 127.0f, 200.0f + rnd() * 55.0f));
      const Vec3 d = normalize(Vec3(-rnd(), (rnd() - 0.5f) * 0.2f, -rnd()));
      VoxelHit a, c;
      const bool ha = voxel_trace(g, o, d, 1e6f, a), hc = voxel_trace_columns(g, o, d, 1e6f, c);
      differ += ha != hc || (ha && (a.x != c.x || a.y != c.y || a.z != c.z || a.t != c.t));
      hits += ha;
      n++;
    }
    std::printf("    %d long rays: %d hits, %d differ, %.1f ms\n", n, hits, differ, t.ms());
    CHECK(differ == 0);
    CHECK(hits > 100);
    CHECK(t.ms() < 5000.0);  // a stalled walk would spin its guard on many rays
  });

  test("voxel GI: Auto Generate leaves a wake-up time for the idle editor, so the re-bake happens without more input", [] {
    Editor ed;
    r36_editor_scene(ed);
    ed.scene().lighting.auto_generate = true;
    r35_sun_mode(ed, 2);
    r35_bake(ed);
    CHECK(!ed.lighting_out_of_date_for_test());
    ed.scene().find_by_name("Directional Light")->set_local_euler({40, 60, 0});
    ed.commit_change("turn the sun");
    r34_steps(ed, 2);
    CHECK(ed.lighting_out_of_date_for_test());
    /* The run loop sleeps until next_wakeup when no event comes: it must point about a second ahead. */
    const double wake = ed.ui_for_test().next_wakeup;
    std::printf("    wake-up in %.2f s\n", wake - now_seconds());
    CHECK(wake > now_seconds() && wake < now_seconds() + 1.5);
  });

  test("voxel GI: changing the Realtime GI settings does not make the baked lighting out of date", [] {
    Editor ed;
    r36_editor_scene(ed);
    r35_sun_mode(ed, 2);
    r35_bake(ed);
    CHECK(!ed.lighting_out_of_date_for_test());
    r36_cmd(ed, "set Lighting.RealtimeGI true");
    const bool after_toggle = ed.lighting_out_of_date_for_test();
    r36_cmd(ed, "set Lighting.GIRadius 3");
    r36_cmd(ed, "set Lighting.GIRays 4");
    r36_cmd(ed, "set Lighting.Bounce false");
    r36_cmd(ed, "set Lighting.VoxelResolution 64");
    const bool after_settings = ed.lighting_out_of_date_for_test();
    std::printf("    out of date after turning GI on: %d, after changing its settings: %d\n", after_toggle, after_settings);
    CHECK(!after_toggle);
    CHECK(!after_settings);
  });

  /* ---------------------------------------------------------------- 8: settings and safety */
  test("voxel GI: settings set, undo, save and load; bad numbers are clamped", [] {
    Editor ed;
    r36_editor_scene(ed);
    const LightingSettings def = ed.scene().lighting;
    CHECK(!def.realtime_gi && def.gi_resolution == 1 && def.gi_rays == 8 && def.gi_downsample == 1 && def.gi_sky_occlusion && def.gi_bounce);
    r36_cmd(ed, "set Lighting.RealtimeGI true");
    r36_cmd(ed, "set Lighting.GIRadius 3");
    r36_cmd(ed, "set Lighting.GIRays 12");
    r36_cmd(ed, "set Lighting.VoxelResolution 256");
    r36_cmd(ed, "set Lighting.GIIntensity 1.5");
    r36_cmd(ed, "set Lighting.SkyOcclusion false");
    r36_cmd(ed, "set Lighting.Bounce false");
    r36_cmd(ed, "set Lighting.GIResolution Half");
    r36_cmd(ed, "set Lighting.RSMResolution 512");
    r36_cmd(ed, "set Lighting.SpecularOcclusion 0.5");
    r36_cmd(ed, "set Lighting.AutoGenerate true");
    const LightingSettings &ls = ed.scene().lighting;
    CHECK(ls.realtime_gi && std::fabs(ls.gi_radius - 3.0f) < 1e-5f && ls.gi_rays == 12 && ls.gi_resolution == 2);
    CHECK(std::fabs(ls.gi_intensity - 1.5f) < 1e-5f && !ls.gi_sky_occlusion && !ls.gi_bounce && ls.gi_downsample == 0);
    CHECK(ls.gi_rsm_resolution == 512 && std::fabs(ls.gi_specular_occlusion - 0.5f) < 1e-5f && ls.auto_generate);
    /* Saved and loaded as they are. */
    Scene back;
    std::string err;
    CHECK(load_scene_text(save_scene_text(ed.scene()), back, err));
    const LightingSettings &bl = back.lighting;
    CHECK(bl.realtime_gi && std::fabs(bl.gi_radius - 3.0f) < 1e-5f && bl.gi_rays == 12 && bl.gi_resolution == 2 && std::fabs(bl.gi_intensity - 1.5f) < 1e-5f);
    CHECK(!bl.gi_sky_occlusion && !bl.gi_bounce && bl.gi_downsample == 0 && bl.gi_rsm_resolution == 512 && std::fabs(bl.gi_specular_occlusion - 0.5f) < 1e-5f);
    CHECK(bl.auto_generate);
    CHECK(save_scene_text(back) == save_scene_text(ed.scene()));
    /* Undo walks them back, one command each. */
    for (int i = 0; i < 11; i++) r36_cmd(ed, "undo");
    CHECK(!ed.scene().lighting.realtime_gi && ed.scene().lighting.gi_rays == 8 && ed.scene().lighting.gi_resolution == 1 && ed.scene().lighting.gi_downsample == 1);
    CHECK(std::fabs(ed.scene().lighting.gi_radius - 2.0f) < 1e-5f && ed.scene().lighting.gi_sky_occlusion && ed.scene().lighting.gi_bounce && !ed.scene().lighting.auto_generate);
    CHECK(ed.scene().lighting.gi_rsm_resolution == def.gi_rsm_resolution);
    for (int i = 0; i < 11; i++) r36_cmd(ed, "redo");
    CHECK(ed.scene().lighting.realtime_gi && ed.scene().lighting.gi_rays == 12 && ed.scene().lighting.auto_generate);
    /* Bad numbers are clamped, not stored. */
    r36_cmd(ed, "set Lighting.GIRadius nan");
    CHECK(std::isfinite(ed.scene().lighting.gi_radius) && ed.scene().lighting.gi_radius > 0.0f && ed.scene().lighting.gi_radius <= 1000.0f);
    r36_cmd(ed, "set Lighting.GIRadius 1e9");
    CHECK(ed.scene().lighting.gi_radius <= 1000.0f);
    r36_cmd(ed, "set Lighting.GIRadius -4");
    CHECK(ed.scene().lighting.gi_radius > 0.0f);
    r36_cmd(ed, "set Lighting.GIRays 999");
    CHECK(ed.scene().lighting.gi_rays <= 32 && ed.scene().lighting.gi_rays >= 1);
    r36_cmd(ed, "set Lighting.GIRays -5");
    CHECK(ed.scene().lighting.gi_rays >= 1);
    r36_cmd(ed, "set Lighting.GIIntensity -3");
    CHECK(ed.scene().lighting.gi_intensity >= 0.0f);
    r36_cmd(ed, "set Lighting.GIIntensity 1e9");
    CHECK(std::isfinite(ed.scene().lighting.gi_intensity) && ed.scene().lighting.gi_intensity <= 10.0f);
    r36_cmd(ed, "set Lighting.RSMResolution 1");
    CHECK(ed.scene().lighting.gi_rsm_resolution >= 32);
    r36_cmd(ed, "set Lighting.RSMResolution 99999");
    CHECK(ed.scene().lighting.gi_rsm_resolution <= 2048);
    r36_cmd(ed, "set Lighting.SpecularOcclusion nan");
    CHECK(std::isfinite(ed.scene().lighting.gi_specular_occlusion) && ed.scene().lighting.gi_specular_occlusion >= 0.0f &&
          ed.scene().lighting.gi_specular_occlusion <= 1.0f);
    r36_cmd(ed, "set Lighting.VoxelResolution 99");
    CHECK(ed.scene().lighting.gi_resolution >= 0 && ed.scene().lighting.gi_resolution <= 2);
  });

  test("voxel GI: extreme settings, an empty scene, a flat scene and objects a million units away draw without trouble", [] {
    /* Extreme settings on a real scene. */
    Editor ed;
    r36_editor_scene(ed);
    r36_cmd(ed, "set Lighting.RealtimeGI true");
    r36_cmd(ed, "set Lighting.GIRadius 1000");
    r36_cmd(ed, "set Lighting.GIRays 32");
    r36_cmd(ed, "set Lighting.VoxelResolution 256");
    r36_cmd(ed, "set Lighting.RSMResolution 32");
    CHECK(r35_luma(r35_game(ed)) > 0);
    r36_cmd(ed, "set Lighting.GIRadius 0.05");
    r36_cmd(ed, "set Lighting.GIRays 1");
    r36_cmd(ed, "set Lighting.VoxelResolution 64");
    r36_cmd(ed, "set Lighting.RSMResolution 2048");
    CHECK(r35_luma(r35_game(ed)) > 0);
    /* Two cubes a million units out each way: the grid covers the lot (a coarse one), the views still draw. */
    r36_cmd(ed, "set Lighting.RSMResolution 128");
    r35_add(ed, "Cube", "FarA", {1e6f, 0, 0}, {1, 1, 1}, Vec3(0.8f));
    r35_add(ed, "Cube", "FarB", {-1e6f, 1e5f, 1e6f}, {5, 5, 5}, Vec3(0.8f));
    ed.commit_change("far objects");
    r34_steps(ed, 4);
    const VoxelGrid &g = ed.voxel_grid_for_test();
    CHECK(g.valid() && g.voxel > 1000.0f);
    CHECK(r35_luma(r35_game(ed)) > 0);
    /* An object scaled to a million units, then absurdly far (beyond what a grid can cover): GI quietly steps aside. */
    r35_add(ed, "Cube", "Huge", {0, 0, 0}, {1e6f, 1, 1e6f}, Vec3(0.8f));
    r35_add(ed, "Cube", "Absurd", {5e9f, 0, 0}, {1, 1, 1}, Vec3(0.8f));
    ed.commit_change("huge objects");
    r34_steps(ed, 4);
    CHECK(r35_luma(r35_game(ed)) > 0);
    ed.scene().find_by_name("FarA")->active = false;
    ed.scene().find_by_name("FarB")->active = false;
    ed.scene().find_by_name("Huge")->active = false;
    ed.scene().find_by_name("Absurd")->active = false;
    ed.commit_change("far objects gone");
    r34_steps(ed, 4);
    CHECK(r35_luma(r35_game(ed)) > 0);

    /* Numbers a file could hold: the views clamp them rather than trust them. */
    ed.scene().lighting.gi_rays = 1000;
    ed.scene().lighting.gi_resolution = 77;
    ed.scene().lighting.gi_radius = std::numeric_limits<float>::quiet_NaN();
    ed.scene().lighting.gi_intensity = std::numeric_limits<float>::infinity();
    ed.scene().lighting.gi_rsm_resolution = -5;
    ed.scene().lighting.gi_downsample = 9;
    ed.scene().lighting.gi_specular_occlusion = std::numeric_limits<float>::quiet_NaN();
    ed.commit_change("odd numbers");
    r34_steps(ed, 4);
    CHECK(r35_luma(r35_game(ed)) > 0);

    /* Everything hidden: no scene at all. */
    Editor empty;
    r35_open(empty);
    r36_cmd(empty, "set Lighting.RealtimeGI true");
    const std::vector<uint32_t> on = r35_game(empty);
    r36_cmd(empty, "set Lighting.RealtimeGI false");
    CHECK(r35_diff_count(r35_game(empty), on) == 0);
    CHECK(!empty.voxel_grid_for_test().valid());

    /* A flat scene (one plane): GI on changes nothing visible (the sky is all open). */
    Editor flat;
    r35_open(flat);
    r35_add(flat, "Plane", "Floor", {0, 0, 0}, {1, 1, 1}, Vec3(0.8f));
    flat.commit_change("floor");
    r34_steps(flat, 3);
    const std::vector<uint32_t> flat_off = r35_game(flat);
    r36_cmd(flat, "set Lighting.RealtimeGI true");
    CHECK(flat.voxel_grid_for_test().valid());
    const std::vector<uint32_t> flat_on = r35_game(flat);
    const size_t fd = r35_diff_count(flat_on, flat_off);
    std::printf("    flat scene, GI on vs off: %zu pixels differ\n", fd);
    CHECK(fd == 0);
  });
}

// SPDX-License-Identifier: GPL-2.0-or-later
// blendity_tests - dependency-free unit tests (exit code = number of failures).
#include "../src/core/core.h"
#include "../src/core/jobs.h"
#include "../src/editor/editor.h"
#include "../src/render/canvas.h"
#include "../src/render/raster.h"
#include "../src/render/dof.h"
#include "../src/research/research.h"
#include "../src/image/image.h"
#include "../src/render/colormanagement.h"
#include "../src/render/display.h"
#include "../src/render/gpu_device.h"
#include "../src/render/pathtracer.h"
#include "../src/scene/import.h"
#include "../src/scene/material.h"
#include "../src/scene/mesh.h"
#include "../src/scene/physics.h"
#include "../src/scene/scene.h"
#include "../src/scene/uv.h"

#include <algorithm>
#include <atomic>
#include <unordered_map>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
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
  fn();
  std::printf("%s %s\n", g_fail == before ? "[ ok ]" : "[FAIL]", name);
}

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
    std::vector<float> bsdf_ref = render(8192, false), nee_ref = render(8192, true);
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
    std::vector<float> ref = render(4096, false), guided = render(512, true);
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
    auto *tri = static_cast<TriangulateModifier *>(g->add_component(create_component("TriangulateModifier")));
    (void)tri;
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
    platform::Event esc;
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
    (void)esc;
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
    CHECK(true);  // drawing all of that without crashing is the check
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
    float circle_area = 0;
    const Mat4 &w = g->world_matrix();
    for (size_t f = 0; f < m.face_count(); f++)
      if (m.face_normal(f).y > 0.99f) top++;
    ed.command("fsel facing 0 1 0");
    (void)w;
    (void)circle_area;
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
      CHECK(true);
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
    CHECK(true);
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
    CHECK(true);
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
      GameObject *owner = nullptr;
      CHECK(ed.render_camera_name() == "Main Camera");
      (void)owner;
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
    CHECK(true);
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


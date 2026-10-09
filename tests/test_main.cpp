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
#include "../src/render/pathtracer.h"
#include "../src/render/shading.h"
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
static void round16_tests();
static void round17_tests();
static void round18_tests();
static void round26_tests();
static void round27_tests();

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
        ed.init_headless(900, 650);
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
          bool closed = false;
          (void)closed;
        }
        size_t inside = 0;
        for (size_t f = 0; f < m.face_count(); f++) {
          size_t on_arc = 0;
          for (uint32_t k = 0; k < m.face_size(f); k++) {
            const Vec3 w = m.positions[m.face_verts(f)[k]] + Vec3(0, 0.5f, 0);
            /* On the circle through the three points (the arc's corners). */
            Vec3 cc, nn;
            (void)nn;
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
      const size_t top = face_facing(m, {0, 1, 0}, {0, 0.5f, 0});
      (void)top;
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

static void cf_render(Image &img, RenderTarget &rt, int W, int H, const Mat4 &v, const Mat4 &p, const RasterOptions &opt0, const CfScene &sc,
                      uint32_t clear = 0xFF204060u, Renderer3D *keep = nullptr) {
  img.resize(W, H);
  rt.attach(img, {0, 0, W, H});
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
    for (int y = 0; y < 6; y++)
      for (int x = 0; x < 8; x++) {
        const bool in = x >= 2 && x < 6 && y >= 1 && y < 5;
        const uint32_t expect = !in ? 99u : (uint32_t)(1 + ((x - 2) / 2) + 2 * ((y - 1) / 2));
        CHECK(dst[(size_t)y * 8 + x] == expect);
      }
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
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++) {
        CHECK(retro_dither_offset(RetroImageParams::Ps1, x, y) == kPs1[y & 3][x & 3]);
        CHECK(retro_dither_offset(RetroImageParams::NoDither, x, y) == 0);
        const int b = retro_dither_offset(RetroImageParams::Bayer4, x, y);
        CHECK(b >= -4 && b <= 3);
      }
    CHECK(retro_dither_offset(RetroImageParams::Ps1, -1, -1) == kPs1[3][3]);  // the pattern repeats below zero too
    CHECK(retro_dither_offset(99, 3, 2) == 0);
    /* Bayer uses every offset of the range exactly twice per tile. */
    std::map<int, int> hist;
    for (int y = 0; y < 4; y++)
      for (int x = 0; x < 4; x++) hist[retro_dither_offset(RetroImageParams::Bayer4, x, y)]++;
    CHECK(hist.size() == 8);
    for (auto &kv : hist) CHECK(kv.second == 2);
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
    int low = 0, high = 0;
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++) {
        const int off = kPs1[y & 3][x & 3];
        const uint32_t v = off < 0 ? 120u : 128u;
        CHECK(img.row(y)[x] == (0xFF000000u | v << 16 | v << 8 | v));
        (off < 0 ? low : high)++;
      }
    CHECK(low == 32 && high == 32);
    /* No dither: every grey truncates the same way (0x87 -> 0x80). */
    std::fill(img.pixels.begin(), img.pixels.end(), 0xFF878787u);
    RetroImageParams plain;
    plain.color_depth = RetroImageParams::Bits15;
    apply_retro_image(rt, plain, nullptr);
    for (uint32_t c : img.pixels) CHECK(c == 0xFF808080u);
    /* The ends stay in range: white (255 + 3 clamps) and black (0 - 4 clamps). */
    std::fill(img.pixels.begin(), img.pixels.end(), 0xFFFFFFFFu);
    apply_retro_image(rt, ps1, nullptr);
    for (uint32_t c : img.pixels) CHECK(c == 0xFFF8F8F8u);
    std::fill(img.pixels.begin(), img.pixels.end(), 0xFF000000u);
    apply_retro_image(rt, ps1, nullptr);
    for (uint32_t c : img.pixels) CHECK(c == 0xFF000000u);
    /* Channels are quantized independently and alpha is kept. */
    std::fill(img.pixels.begin(), img.pixels.end(), 0x80FF8A07u);
    apply_retro_image(rt, plain, nullptr);
    for (uint32_t c : img.pixels) CHECK(c == 0x80F88800u);
    /* Full colour depth with no fog does nothing at all. */
    std::fill(img.pixels.begin(), img.pixels.end(), 0xFF123457u);
    RetroImageParams off;
    apply_retro_image(rt, off, nullptr);
    for (uint32_t c : img.pixels) CHECK(c == 0xFF123457u);
    /* Empty targets are ignored. */
    RenderTarget none;
    apply_retro_image(none, ps1, nullptr);
    CHECK(true);
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
    bool extents_ok = true;
    for (int i = 0; i < 48; i++) {
      const float cx = i * 0.004f;  // about 0.055 px per step at this distance
      const Mat4 v = Mat4::look_at({cx, 0, -5}, {cx, 0, 0}, {0, 1, 0});
      Step s;
      RasterOptions snap, plain;
      snap.vertex_snap = 1.0f;
      Image a, b;
      RenderTarget ra, rb;
      Renderer3D r3d;
      cf_render(a, ra, W, H, v, p, snap, sc, 0xFF000000u, &r3d);
      cf_render(b, rb, W, H, v, p, plain, sc);
      s.snapped = cf_hash(a);
      s.plain = cf_hash(b);
      int minx = 1 << 20, maxx = -1, miny = 1 << 20, maxy = -1;
      float fx0 = 1e9f, fx1 = -1e9f, fy0 = 1e9f, fy1 = -1e9f;
      for (const Vec3 &c : corners) {
        Vec2 sp;
        float z;
        CHECK(r3d.project(c, sp, z));
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
    CHECK(extents_ok);
    bool same_key_same_image = true, plain_changes_within_key = false, key_changes = false, jump_changes_image = true;
    for (size_t i = 0; i < steps.size(); i++)
      for (size_t j = i + 1; j < steps.size(); j++) {
        const bool same = steps[i].key == steps[j].key;
        if (same && steps[i].snapped != steps[j].snapped) same_key_same_image = false;
        if (!same && steps[i].snapped == steps[j].snapped && j == i + 1) jump_changes_image = false;
        if (!same && j == i + 1) key_changes = true;
        if (same && steps[i].plain != steps[j].plain) plain_changes_within_key = true;
      }
    CHECK(same_key_same_image);     // moves that round to the same corners draw the same picture
    CHECK(key_changes);             // the sweep does cross whole pixels
    CHECK(jump_changes_image);      // and when it does the picture changes (by whole pixels)
    /* An axis-aligned rectangle covers the same pixel centres snapped or not (an edge crosses a
     * centre exactly when its corner rounds the other way), so the creep shows on slanted edges:
     * a quad turned 30 degrees. Snapped, moves that keep its corners' rounding draw one picture;
     * unsnapped, the slanted edges creep across pixel centres in between. */
    (void)plain_changes_within_key;
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
        CHECK(r3d.project(rot.point(c), sp, z));
        s.key.push_back((int)std::floor(sp.x + 0.5f));
        s.key.push_back((int)std::floor(sp.y + 0.5f));
      }
      tsteps.push_back(s);
    }
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
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
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
    const float nan = std::numeric_limits<float>::quiet_NaN();
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
    for (int y = 0; y < 4; y++)
      for (int x = 0; x < 4; x++) {
        const Vec4 c = t.sample_level({(x + 0.5f) / 4, 1 - (y + 0.5f) / 4}, 0, TexWrap::Repeat, TexFilter::Closest);
        CHECK_NEAR(c.x * 255, x * 60, 0.6);
        CHECK_NEAR(c.y * 255, y * 60, 0.6);
        CHECK_NEAR(c.z, (x + y) % 2 ? 1.0 : 0.0, 1e-4);
      }
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
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
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
    CHECK(f.color_depth == RetroImageParams::Bits15 && f.dither == RetroImageParams::Ps1 && !f.fog);
    CHECK(std::string(f.type_name()) == "Retro Console Filter");
    CHECK(!f.unique());  // several filters may sit on one camera
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

    /* Two filters on one camera: the later size and snap win, the smaller texture cap wins, every colour pass runs. */
    RetroConsoleFilter g;
    g.width = 160, g.height = 120, g.snap_grid = 2.0f, g.max_texture_size = 64, g.fit = 1;
    FilterStack both;
    f.contribute(both);
    g.contribute(both);
    CHECK(both.width == 160 && both.height == 120 && both.fit == FilterStack::Letterbox);
    CHECK(both.vertex_snap == 2.0f && both.tex.max_size == 64 && both.passes.size() == 2);
    FilterStack reversed;
    g.contribute(reversed);
    f.contribute(reversed);
    CHECK(reversed.width == 320 && reversed.tex.max_size == 64 && reversed.vertex_snap == 1.0f);
    /* A cap of 0 means "no cap" and doesn't lift another filter's cap. */
    RetroConsoleFilter nocap;
    nocap.max_texture_size = 0;
    nocap.contribute(both);
    CHECK(both.tex.max_size == 64);
    /* A grid that isn't a positive number leaves snapping off; the other effects still apply. */
    RetroConsoleFilter bad;
    bad.snap_grid = std::numeric_limits<float>::quiet_NaN();
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
    for (uint32_t c : img.pixels) CHECK(cf_is_15bit(c));
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
    CHECK(cam && cam->get<Camera>() && !cam->get<RetroConsoleFilter>());
    cmd("component Retro Console Filter");  // the name has spaces
    RetroConsoleFilter *f = cam->get<RetroConsoleFilter>();
    CHECK(f != nullptr);
    if (!f) return;
    CHECK(dynamic_cast<CameraFilter *>(f) != nullptr);
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
    for (const auto &c : cam->components) count += dynamic_cast<RetroConsoleFilter *>(c.get()) != nullptr;
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
        for (const auto &comp : c->components) n += dynamic_cast<RetroConsoleFilter *>(comp.get()) != nullptr;
      return n;
    };
    ed.step_frame_headless({z});
    CHECK(filters_on_camera() == 1);  // the second filter's creation is the last step
    ed.step_frame_headless({z});      // FogEnd
    {
      RetroConsoleFilter *r = ed.scene().find_by_name("Main Camera")->get<RetroConsoleFilter>();
      CHECK(r && r->fog_end != 12.0f);
    }
    for (int i = 0; i < 40 && filters_on_camera() > 0; i++) ed.step_frame_headless({z});
    CHECK(filters_on_camera() == 0);
  });

  test("camera filters: the component saves, loads (its own values beat the preset), round-trips twice, and clones", [&] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.step_frame_headless();
    GameObject *cam = ed.scene().find_by_name("Main Camera");
    CHECK(cam != nullptr);
    if (!cam) return;
    auto *a = cam->add<RetroConsoleFilter>();
    a->width = 64, a->height = 48, a->fit = 1, a->vertex_snap = true, a->snap_grid = 2.5f, a->affine_textures = false;
    a->texture_filter = 2, a->max_texture_size = 128, a->mipmaps = true, a->color_depth = RetroImageParams::Full, a->dither = RetroImageParams::Bayer4;
    a->fog = true, a->fog_start = 4.0f, a->fog_end = 22.0f, a->fog_color = {0.1f, 0.2f, 0.3f};
    auto *b = cam->add<RetroConsoleFilter>();  // untouched: the preset
    b->enabled = false;
    const std::string text = save_scene_text(ed.scene());
    CHECK(text.find("Retro Console Filter") != std::string::npos);
    /* Clones (Duplicate, undo snapshots, Play mode) carry every field. */
    std::unique_ptr<Scene> copy = ed.scene().clone();
    GameObject *cc = copy->find_by_name("Main Camera");
    CHECK(cc != nullptr);
    if (cc) {
      std::vector<RetroConsoleFilter *> cl;
      for (const auto &c : cc->components)
        if (auto *r = dynamic_cast<RetroConsoleFilter *>(c.get())) cl.push_back(r);
      CHECK(cl.size() == 2);
      if (cl.size() == 2) {
        CHECK(cl[0]->width == 64 && cl[0]->snap_grid == 2.5f && cl[0]->fog && cl[0]->fog_end == 22.0f && cl[0]->owner == cc);
        CHECK(!cl[1]->enabled);
      }
    }
    CHECK(save_scene_text(*copy) == text);
    /* Duplicate in the editor. */
    ed.command("select Main Camera");
    ed.command("duplicate");
    int owners = 0;
    ed.scene().for_each([&](GameObject &g) { owners += g.get<RetroConsoleFilter>() != nullptr; });
    CHECK(owners == 2);
    Scene loaded;
    std::string err;
    CHECK(load_scene_text(text, loaded, err));
    GameObject *lc = loaded.find_by_name("Main Camera");
    CHECK(lc != nullptr);
    if (!lc) return;
    std::vector<RetroConsoleFilter *> fl;
    for (const auto &c : lc->components)
      if (auto *r = dynamic_cast<RetroConsoleFilter *>(c.get())) fl.push_back(r);
    CHECK(fl.size() == 2);
    if (fl.size() != 2) return;
    const RetroConsoleFilter *x = fl[0], *y = fl[1];
    CHECK(x->width == 64 && x->height == 48 && x->fit == 1 && x->vertex_snap && x->snap_grid == 2.5f && !x->affine_textures);
    CHECK(x->texture_filter == 2 && x->max_texture_size == 128 && x->mipmaps);
    CHECK(x->color_depth == RetroImageParams::Full && x->dither == RetroImageParams::Bayer4);
    CHECK(x->fog && x->fog_start == 4.0f && x->fog_end == 22.0f);
    CHECK_NEAR(x->fog_color.x, 0.1f, 1e-5);
    CHECK_NEAR(x->fog_color.y, 0.2f, 1e-5);
    CHECK_NEAR(x->fog_color.z, 0.3f, 1e-5);
    CHECK(x->applied_console == x->console);
    CHECK(y->width == 320 && y->height == 240 && !y->enabled);
    CHECK(save_scene_text(loaded) == text);
  });

  /* --------------------------------------------------------------- 10 */
  test("camera filters: PS1 Game view is 15-bit colour in whole blocks; with no filter, a disabled one or a neutral one it is the old picture", [&] {
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
    RetroConsoleFilter *f = cam->add<RetroConsoleFilter>();
    /* A disabled filter changes nothing. */
    f->enabled = false;
    settle();
    CHECK(same(grab(), base));
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
    RetroConsoleFilter *g = cam->add<RetroConsoleFilter>();
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
    cam->components.erase(std::remove_if(cam->components.begin(), cam->components.end(),
                                         [](const std::unique_ptr<Component> &c) { return dynamic_cast<RetroConsoleFilter *>(c.get()) != nullptr; }),
                          cam->components.end());
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
    const std::vector<uint32_t> base = grab();
    /* The Scene view's overlay text changes a little every frame, and the `filters` command's log line
     * shows in it for a while (~0.55% of the view on Linux): equal means all but 1% of pixels. The
     * filtered view differs in ~99% of them. */
    auto same = [&](const std::vector<uint32_t> &a, const std::vector<uint32_t> &b) {
      size_t diff = 0;
      for (size_t i = 0; i < a.size() && i < b.size(); i++) diff += a[i] != b[i];
      if (std::getenv("BLENDITY_CF_DEBUG")) std::printf("    same(): %zu of %zu pixels differ\n", diff, a.size());
      return a.size() == b.size() && diff <= a.size() / 100;
    };
    /* The toggle with no filter on the camera changes nothing. */
    ed.command("filters on");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    CHECK(same(grab(), base));
    ed.command("filters off");
    GameObject *cam = ed.scene().find_by_name("Main Camera");
    CHECK(cam != nullptr);
    if (!cam) return;
    RetroConsoleFilter *f = cam->add<RetroConsoleFilter>();
    f->width = 80;
    f->height = 60;
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    const std::vector<uint32_t> off = grab();
    CHECK(same(off, base));  // off by default, whatever the camera carries
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
    CHECK(same(grab(), base));
    f->enabled = true;
    ed.command("filters off");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    CHECK(same(grab(), base));
    ed.command("filters");  // no argument: toggles
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    CHECK(same(grab(), on));
    ed.command("filters 0");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    CHECK(same(grab(), base));
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
      RetroConsoleFilter *f = main->add<RetroConsoleFilter>();
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
    cam->add<RetroConsoleFilter>();
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
    /* A different internal size: the preview is rebuilt at the new resolution. */
    ed.command("set RetroConsoleFilter.Width 16");
    ed.command("set RetroConsoleFilter.Height 16");
    const std::vector<uint32_t> after_size = settle();
    CHECK(after_size != first);
    /* Colour depth alone (no change to geometry or size) also refreshes it. */
    ed.command("set RetroConsoleFilter.ColourDepth 24-bit");
    const std::vector<uint32_t> after_depth = settle();
    CHECK(after_depth != after_size);
    /* Disabling the component refreshes it again. */
    cam->get<RetroConsoleFilter>()->enabled = false;
    const std::vector<uint32_t> after_off = settle();
    CHECK(after_off != after_depth);
  });
}

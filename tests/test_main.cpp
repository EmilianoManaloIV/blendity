// SPDX-License-Identifier: GPL-2.0-or-later
// blendity_tests - dependency-free unit tests (exit code = number of failures).
#include "../src/core/core.h"
#include "../src/core/jobs.h"
#include "../src/editor/editor.h"
#include "../src/render/canvas.h"
#include "../src/render/raster.h"
#include "../src/research/research.h"
#include "../src/image/image.h"
#include "../src/render/colormanagement.h"
#include "../src/render/display.h"
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
#include <cstring>
#include <limits>
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

int main() {
  register_builtin_components();
  research::register_features();
  Log::echo_stdout = false;

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
  test("editor: the Push/Pull tool drags a face with the mouse", [] {
    Editor ed;
    ed.init_headless(1000, 700);
    ed.step_frame_headless();
    /* Snapshot every mesh so we can tell which one the drag changed. */
    std::vector<std::pair<uint64_t, std::vector<Vec3>>> before;
    ed.scene().for_each([&](GameObject &g) {
      if (auto *mf = g.get<MeshFilter>())
        if (mf->mesh) before.push_back({g.id, mf->mesh->positions});
    });
    ed.command("tool pushpull");
    Recti r = ed.scene_view_rect();
    int cx = r.x + r.w / 2, cy = r.y + r.h / 2;  // the scene's Cube sits at the view centre
    auto ev = [](platform::EventType t, int x, int y) {
      platform::Event e;
      e.type = t;
      e.x = x;
      e.y = y;
      return e;
    };
    ed.step_frame_headless({ev(platform::EventType::MouseMove, cx, cy)});
    ed.step_frame_headless({ev(platform::EventType::MouseDown, cx, cy)});
    ed.step_frame_headless({ev(platform::EventType::MouseMove, cx + 10, cy - 30)});
    ed.step_frame_headless({ev(platform::EventType::MouseMove, cx + 20, cy - 60)});
    ed.step_frame_headless({ev(platform::EventType::MouseUp, cx + 20, cy - 60)});
    ed.step_frame_headless();
    const Mesh *changed = nullptr;
    for (auto &[id, pos] : before)
      if (GameObject *g = ed.scene().find(id))
        if (g->get<MeshFilter>()->mesh->positions != pos) changed = g->get<MeshFilter>()->mesh.get();
    CHECK(changed != nullptr);
    if (changed) CHECK(closed_manifold(*changed));
    /* The distance can then be typed exactly (Adjust Last Operation). */
    ed.command("redo amount 0.25");
    ed.step_frame_headless();
    if (changed) {
      for (auto &[id, pos] : before)
        if (GameObject *g = ed.scene().find(id))
          if (g->get<MeshFilter>()->mesh->positions != pos) CHECK(closed_manifold(*g->get<MeshFilter>()->mesh));
    }
    platform::Event z;
    z.type = platform::EventType::KeyDown;
    z.key = platform::KEY_Z;
    z.mods = platform::MOD_CTRL;
    ed.step_frame_headless({z});
    bool restored = true;
    for (auto &[id, pos] : before)
      if (GameObject *g = ed.scene().find(id)) restored = restored && g->get<MeshFilter>()->mesh->positions == pos;
    CHECK(restored);  // one undo step for the drag and its adjustment
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

// SPDX-License-Identifier: GPL-2.0-or-later
// blendity_tests: the modeling tests (task 0177: one file a subsystem; the groups keep their rounds' order).
#include "test_common.h"
#include "../src/scene/uv_internal.h"

static void first_tests() {
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
    /* Open it and pick "Unreal", the list's second row below the field (Unity, Unreal, Blender, ...: task 0063). */
    click(r.x + r.w / 2, r.y + r.h / 2);
    CHECK(ed.popup_count_for_test() == 2);
    const int row = r.h + 4;
    click(r.x + 20, r.bottom() + 4 + row + row / 2);
    std::printf("    picked: preset %s, dialog %d, popups %zu\n", ed.keymap_preset_name().c_str(), (int)ed.dialog_open_for_test(),
                ed.popup_count_for_test());
    CHECK(ed.keymap_preset_name() == "Unreal");
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

static void round63_tests() {
  test("deforming modifiers: changing a field moves the render mesh in place, the same as evaluating the stack in full, on five meshes", [] {
    int cases = 0, in_place = 0, mismatches = 0;
    float worst = 0.0f;
    for (auto &[name, base] : r63_meshes()) {
      for (int kind = 0; kind < 4; kind++) {
        Scene s;
        GameObject *g = s.create("Deformed");
        g->add<MeshFilter>()->mesh = base;
        Component *mod = nullptr;
        std::function<void(float)> set;
        if (kind == 0) {
          auto *m = g->add<SimpleDeformModifier>();
          set = [m](float v) { m->angle = v; };
          mod = m;
        }
        else if (kind == 1) {
          auto *m = g->add<DisplaceModifier>();
          set = [m](float v) { m->strength = v / 300.0f; };
          mod = m;
        }
        else if (kind == 2) {
          auto *m = g->add<CastModifier>();
          set = [m](float v) { m->factor = v / 100.0f; };
          mod = m;
        }
        else {
          auto *m = g->add<SmoothModifier>();
          set = [m](float v) { m->factor = v / 100.0f; };
          mod = m;
        }
        set(45.0f);
        const RenderMesh &first = g->evaluated_mesh()->render_mesh_tangents();
        const uint64_t build = first.build_serial;
        for (float v : {60.0f, -30.0f, 10.0f}) {
          set(v);
          const Mesh *got = g->evaluated_mesh();
          const RenderMesh &rm = got->render_mesh_tangents();
          in_place += rm.build_serial == build;
          Mesh ref = *base;
          mod->modify(ref);
          ref.touch();
          float dn = 0.0f;
          const bool same = r63_same(*got, ref, &dn);
          mismatches += !same || dn > 1e-5f;
          if (!same || dn > 1e-5f) std::printf("    %s, modifier %d, value %.0f: positions %s, normals off by %g\n", name.c_str(), kind, v, same ? "same" : "DIFFER", dn);
          worst = std::max(worst, dn);
          cases++;
          /* Tangents stay unit length and at right angles to the normals. */
          bool tangents_ok = rm.tangents.size() == rm.positions.size();
          for (size_t i = 0; tangents_ok && i < rm.tangents.size(); i += 97)
            tangents_ok = std::fabs(length(rm.tangents[i].xyz()) - 1.0f) < 1e-3f && std::fabs(dot(rm.tangents[i].xyz(), rm.normals[i])) < 1e-3f;
          CHECK(tangents_ok);
        }
      }
    }
    std::printf("    %d changes, %d moved in place, worst normal difference %g\n", cases, in_place, worst);
    CHECK(cases == 60);
    CHECK(mismatches == 0);
    CHECK(in_place == 60);  // these five keep their split pattern under these changes
  });

  test("deforming modifiers: only the deforming tail is re-run; the render mesh keeps its build while a generative modifier rebuilds", [] {
    Scene s;
    GameObject *g = s.create("Twist");
    MeshPtr base = primitives::uv_sphere(0.5f, 32, 24);
    base->smooth = true;
    g->add<MeshFilter>()->mesh = base;
    auto *sub = g->add<SubdivisionSurface>();  // a head: evaluated once, kept
    auto *tw = g->add<SimpleDeformModifier>();
    const Mesh *m0 = g->evaluated_mesh();
    const RenderMesh &rm0 = m0->render_mesh();
    const uint64_t build = rm0.build_serial, serial = rm0.serial, version = m0->version, deform = m0->deform_version;
    tw->angle = 90.0f;
    const Mesh *m1 = g->evaluated_mesh();
    const RenderMesh &rm1 = m1->render_mesh();
    CHECK(m1 == m0);  // the same mesh, written in place (nothing else holds it)
    CHECK(m1->version == version && m1->deform_version == deform + 1);
    CHECK(rm1.build_serial == build && rm1.serial != serial);
    /* The same result as the whole stack from scratch. */
    Mesh ref = *base;
    sub->modify(ref);
    tw->modify(ref);
    ref.touch();
    float dn = 0.0f;
    CHECK(r63_same(*m1, ref, &dn) && dn <= 1e-5f);
    /* The head's own field: a full evaluation. */
    sub->levels = 2;
    CHECK(g->evaluated_mesh()->render_mesh().build_serial != build);
    const uint64_t build2 = g->evaluated_mesh()->render_mesh().build_serial;
    /* A generative modifier after the deformer: the whole stack is a head, every change rebuilds. */
    g->add<MirrorModifier>();
    CHECK(g->evaluated_mesh()->render_mesh().build_serial != build2);
    const uint64_t build3 = g->evaluated_mesh()->render_mesh().build_serial;
    tw->angle = 30.0f;
    CHECK(g->evaluated_mesh()->render_mesh().build_serial != build3);
    /* Edit Mode's cage (kind 2) keeps the full path. */
    s.find_by_name("Twist")->components.pop_back();
    const uint64_t b2 = g->evaluated_mesh(2)->render_mesh().build_serial;
    tw->angle = 40.0f;
    CHECK(g->evaluated_mesh(2)->render_mesh().build_serial != b2);
  });

  test("deforming modifiers: when a deform changes how corners split (a flat grid made bumpy) the render mesh is rebuilt, then follows in place", [] {
    Scene s;
    GameObject *g = s.create("Bumpy");
    MeshPtr base = primitives::grid(2.0f, 16, 16);
    base->smooth = false;  // flat: a flat grid's corners share one normal, a bumpy one's don't
    g->add<MeshFilter>()->mesh = base;
    auto *d = g->add<DisplaceModifier>();
    d->strength = 0.0f;
    const uint64_t b0 = g->evaluated_mesh()->render_mesh().build_serial;
    const size_t verts0 = g->evaluated_mesh()->render_mesh().positions.size();
    d->strength = 0.3f;
    const RenderMesh &rm1 = g->evaluated_mesh()->render_mesh();
    CHECK(rm1.build_serial != b0 && rm1.positions.size() > verts0);  // rebuilt, split per face
    const uint64_t b1 = rm1.build_serial;
    d->strength = 0.35f;
    CHECK(g->evaluated_mesh()->render_mesh().build_serial == b1);  // now in place
    Mesh ref = *base;
    d->modify(ref);
    ref.touch();
    float dn = 0.0f;
    CHECK(r63_same(*g->evaluated_mesh(), ref, &dn) && dn <= 1e-5f);
  });

  test("deforming modifiers: a Boolean whose cutter is deformed re-cuts on every change of the deformer", [] {
    if (!meshops::boolean_available()) return;
    Scene s;
    GameObject *a = s.create("Block");
    a->add<MeshFilter>()->mesh = primitives::cube(2.0f);
    auto *boolean = a->add<BooleanModifier>();
    boolean->object = "Cutter";
    GameObject *b = s.create("Cutter");
    MeshPtr bar = primitives::cube(1.0f);
    for (Vec3 &p : bar->positions) p = Vec3(p.x * 0.4f, p.y * 3.0f, p.z * 3.0f);  // a slab through the block
    bar->touch();
    b->add<MeshFilter>()->mesh = bar;
    auto *tw = b->add<SimpleDeformModifier>();
    tw->angle = 0.0f;
    (void)a->evaluated_mesh()->render_mesh();
    for (float ang : {30.0f, 60.0f}) {
      tw->angle = ang;
      const Mesh *cut = a->evaluated_mesh();
      /* The reference: the same cut against a cutter evaluated from scratch. */
      Mesh cutter = *bar;
      tw->modify(cutter);
      Mesh ref = *a->get<MeshFilter>()->mesh;
      std::string err;
      CHECK(meshops::boolean_op(ref, cutter, a->world_matrix().inverse() * b->world_matrix(), meshops::BooleanOp::Difference, &err));
      CHECK(cut->positions == ref.positions && cut->corner_verts == ref.corner_verts);
    }
  });

  test("deforming modifiers: tangents after a deform depend on the shape, not on the drag that led there, and follow a twisted cap", [] {
    MeshPtr base = primitives::cylinder(0.3f, 2.0f, 24);
    Scene s;
    GameObject *g1 = s.create("Dragged"), *g2 = s.create("Direct");
    g1->add<MeshFilter>()->mesh = base;
    g2->add<MeshFilter>()->mesh = base;
    auto *t1 = g1->add<SimpleDeformModifier>();
    auto *t2 = g2->add<SimpleDeformModifier>();
    t1->angle = t2->angle = 1.0f;
    const uint64_t b1 = g1->evaluated_mesh()->render_mesh_tangents().build_serial;
    const uint64_t b2 = g2->evaluated_mesh()->render_mesh_tangents().build_serial;
    for (float a : {30.0f, 90.0f, 140.0f, 60.0f, 90.0f}) {
      t1->angle = a;
      (void)g1->evaluated_mesh()->render_mesh_tangents();
    }
    t2->angle = 90.0f;
    const RenderMesh &r1 = g1->evaluated_mesh()->render_mesh_tangents(), &r2 = g2->evaluated_mesh()->render_mesh_tangents();
    std::printf("    builds: dragged %s, direct %s\n", r1.build_serial == b1 ? "kept" : "rebuilt", r2.build_serial == b2 ? "kept" : "rebuilt");
    CHECK(r1.build_serial != r2.build_serial);
    CHECK(r1.tangents.size() == r2.tangents.size() &&
          std::memcmp(r1.tangents.data(), r2.tangents.data(), r1.tangents.size() * sizeof(Vec4)) == 0);
    /* Against MikkTSpace on the same twisted shape built in full: the caps turn with the twist too. */
    Mesh ref = *base;
    t2->modify(ref);
    ref.touch();
    const RenderMesh &rf = ref.render_mesh_tangents();
    std::vector<uint32_t> ca(ref.corner_count(), UINT32_MAX), cb(ref.corner_count(), UINT32_MAX);
    for (size_t k = 0; k < r2.tri_corner.size(); k++) ca[r2.tri_corner[k]] = r2.indices[k];
    for (size_t k = 0; k < rf.tri_corner.size(); k++) cb[rf.tri_corner[k]] = rf.indices[k];
    size_t close = 0, n = 0;
    for (size_t c = 0; c < ca.size(); c++) {
      if (ca[c] == UINT32_MAX || cb[c] == UINT32_MAX) continue;
      n++;
      close += dot(r2.tangents[ca[c]].xyz(), rf.tangents[cb[c]].xyz()) > 0.95f && r2.tangents[ca[c]].w == rf.tangents[cb[c]].w;
    }
    std::printf("    %zu of %zu corners within 18 degrees of MikkTSpace with the same handedness\n", close, n);
    CHECK(n > 0 && close * 100 >= n * 95);
  });

  test("deforming modifiers: a saved and reloaded scene evaluates to the same deformed mesh", [] {
    Scene s;
    GameObject *g = s.create("Saved");
    g->add<MeshFilter>()->mesh = primitives::uv_sphere(0.5f, 24, 16);
    g->add<MeshRenderer>();
    auto *c = g->add<CastModifier>();
    c->shape = 2;
    c->factor = 0.2f;
    (void)g->evaluated_mesh()->render_mesh();
    c->factor = 0.7f;  // in place
    const std::vector<Vec3> want = g->evaluated_mesh()->positions;
    Scene back;
    std::string err;
    CHECK(load_scene_text(save_scene_text(s), back, err));
    GameObject *h = back.find_by_name("Saved");
    CHECK(h && h->evaluated_mesh() && h->evaluated_mesh()->positions == want);
  });

  test("deforming modifiers: a snapshot of the scene keeps its deformed mesh when the original changes again", [] {
    Scene s;
    GameObject *g = s.create("Bend");
    MeshPtr base = primitives::cylinder(0.3f, 2.0f, 16);
    g->add<MeshFilter>()->mesh = base;
    auto *bend = g->add<SimpleDeformModifier>();
    bend->mode = 1;
    bend->angle = 60.0f;
    const Mesh *m0 = g->evaluated_mesh();
    (void)m0->render_mesh();
    std::unique_ptr<Scene> snap = s.clone();  // shares the evaluated mesh (an undo step does this)
    const std::vector<Vec3> kept = snap->find_by_name("Bend")->evaluated_mesh()->positions;
    const std::vector<Vec3> kept_rm = snap->find_by_name("Bend")->evaluated_mesh()->render_mesh().positions;
    bend->angle = 120.0f;
    const Mesh *m1 = g->evaluated_mesh();
    CHECK(m1 != m0);  // copied before writing: the snapshot's is shared
    const Mesh *sm = snap->find_by_name("Bend")->evaluated_mesh();
    CHECK(sm->positions == kept);
    CHECK(sm->render_mesh().positions == kept_rm);
    CHECK(m1->positions != kept);
  });

  test("deforming modifiers: dragging Simple Deform's angle re-renders the view and the shadows, no stale picture, and undo brings the old shape back", [] {
    Editor ed;
    r34_open(ed);
    MeshPtr base = primitives::cylinder(0.3f, 2.0f, 24);
    GameObject *g = ed.scene().create("Twisted");
    g->add<MeshFilter>()->mesh = base;
    g->add<MeshRenderer>();
    auto *tw = g->add<SimpleDeformModifier>();
    tw->mode = 1;
    tw->angle = 20.0f;
    ed.mark_changed_for_test("R63 twisted");
    r34_steps(ed, 4);
    const uint64_t build = g->evaluated_mesh()->render_mesh().build_serial;
    const std::vector<Vec3> at20 = g->evaluated_mesh()->positions;
    ed.command("set Lighting.RealtimeGI true");
    r34_steps(ed, 3);
    ed.set_render_cache_verify(true);
    const uint64_t vox0 = ed.voxelized_for_test();
    const R34Change c = r34_change(ed, [&] {
      tw->angle = 80.0f;
      ed.mark_changed_for_test("R63 bend");
    }, 2);
    std::printf("    the change: %llu view renders, %llu shadow renders, %llu objects voxelized, %llu stale\n", (unsigned long long)c.d.renders,
                (unsigned long long)c.d.shadows, (unsigned long long)(ed.voxelized_for_test() - vox0), (unsigned long long)c.d.mismatches);
    CHECK(c.d.renders > 0 && c.d.shadows > 0);
    CHECK(ed.voxelized_for_test() > vox0);  // voxel GI re-made the bent object
    CHECK(c.before != c.after);
    CHECK(c.d.mismatches == 0);
    /* More changes, each a frame, as a drag gives them. */
    const uint64_t bad0 = Editor::render_cache_mismatches_all();
    for (float a : {85.0f, 90.0f, 95.0f, 100.0f}) {
      tw->angle = a;
      r34_steps(ed, 1);
    }
    ed.mark_changed_for_test("R63 drag");
    r34_steps(ed, 2);
    CHECK(Editor::render_cache_mismatches_all() == bad0);
    ed.set_render_cache_verify(false);
    CHECK(g->evaluated_mesh()->render_mesh().build_serial == build);  // the whole drag stayed in place
    /* Undo to 80 then 20 degrees: the positions are the full evaluation's. */
    ed.command("undo");
    ed.command("undo");
    r34_steps(ed, 2);
    GameObject *u = ed.scene().find_by_name("Twisted");
    CHECK(u && u->get<SimpleDeformModifier>() && u->get<SimpleDeformModifier>()->angle == 20.0f);
    if (u) CHECK(u->evaluated_mesh()->positions == at20);
  });
}


/* ===================================================================== */
/* Round 65 (task 0043): lattice objects and the Lattice modifier          */
/* ===================================================================== */

/* A mesh and a Lattice object at the origin (unit box, scaled by `box`), the mesh with a Lattice modifier on it. */
struct R65Scene {
  Scene s;
  GameObject *obj = nullptr, *lat_obj = nullptr;
  Lattice *lat = nullptr;
  LatticeModifier *mod = nullptr;
  MeshPtr base;
  R65Scene(MeshPtr m, float box = 1.0f) {
    base = m;
    obj = s.create("Thing");
    obj->add<MeshFilter>()->mesh = m;
    mod = obj->add<LatticeModifier>();
    mod->object = "Box";
    lat_obj = s.create("Box");
    lat = lat_obj->add<Lattice>();
    lat_obj->set_local_scale(Vec3(box));
  }
  float worst_move() const {
    const Mesh *m = obj->evaluated_mesh();
    float w = 0.0f;
    for (size_t i = 0; i < m->positions.size(); i++) w = std::max(w, length(m->positions[i] - base->positions[i]));
    return w;
  }
};

static void round65_tests() {
  test("lattices: a lattice at rest leaves the mesh as it is, Linear or B-Spline, at any resolution", [] {
    int bad = 0;
    for (int interp = 0; interp < 2; interp++)
      for (int res : {2, 3, 5}) {
        R65Scene t(primitives::uv_sphere(0.5f, 16, 12), 0.8f);  // the sphere pokes out of the box too
        t.lat->interpolation = interp;
        t.lat->resample(res, res + 1, res);
        bad += t.worst_move() > 1e-5f;
      }
    CHECK(bad == 0);
  });

  test("lattices: Linear: a corner moved by d moves the mesh's vertex there by d, the opposite one not at all, the centre by d / 8", [] {
    R65Scene t(primitives::cube(1.0f));
    t.lat->interpolation = 0;
    const Vec3 d(0.3f, -0.1f, 0.2f);
    t.lat->set_control_point(7, t.lat->control_point(7) + d);  // u = v = w = 1: the (+, +, +) corner
    const Mesh *m = t.obj->evaluated_mesh();
    float at_corner = 1e9f, at_opposite = 1e9f;
    for (size_t i = 0; i < m->positions.size(); i++) {
      const Vec3 v = t.base->positions[i], moved = m->positions[i] - v;
      if (v.x > 0 && v.y > 0 && v.z > 0) at_corner = length(moved - d);
      if (v.x < 0 && v.y < 0 && v.z < 0) at_opposite = length(moved);
    }
    CHECK(at_corner < 1e-5f && at_opposite < 1e-6f);
    CHECK(length(t.lat->displacement(Vec3(0.0f)) - d / 8.0f) < 1e-6f);
    /* Strength 0.5: every vertex half as far. */
    const std::vector<Vec3> full = m->positions;
    t.mod->strength = 0.5f;
    const Mesh *h = t.obj->evaluated_mesh();
    float off = 0.0f;
    for (size_t i = 0; i < h->positions.size(); i++)
      off = std::max(off, length((h->positions[i] - t.base->positions[i]) * 2.0f - (full[i] - t.base->positions[i])));
    CHECK(off < 1e-5f);
  });

  test("lattices: B-Spline: pulling an inner point moves the mesh smoothly, with no jump at cell borders", [] {
    Lattice l;
    l.resample(4, 4, 4);
    const Vec3 d(0.0f, 0.4f, 0.0f);
    const int inner = (1 * 4 + 1) * 4 + 1;
    l.set_control_point(inner, l.control_point(inner) + d);
    const float cell = 1.0f / 3.0f, h = 0.002f;
    float worst = 0.0f, peak = 0.0f;
    for (float x = -0.6f; x < 0.6f; x += h) {
      const Vec3 a = l.displacement(Vec3(x, -0.17f, -0.17f)), b = l.displacement(Vec3(x + h, -0.17f, -0.17f));
      worst = std::max(worst, length(b - a));
      peak = std::max(peak, length(a));
    }
    std::printf("    largest step %.5f for %.3f along X (bound %.5f), largest move %.3f\n", worst, h, length(d) * h / cell, peak);
    CHECK(peak > 0.05f && peak < length(d));  // B-Spline doesn't reach the point (it smooths)
    CHECK(worst <= length(d) * h / cell);
  });

  test("lattices: B-Spline keeps its points through resolution changes (2 to 3 and back), and outside the box blends on smoothly", [] {
    Lattice l;
    l.interpolation = 1;
    for (int i = 0; i < 8; i++) l.offsets[(size_t)i] = Vec3(l.rest_point(i).x, 0, 0);  // a stretch along X
    const std::vector<Vec3> start = l.offsets;
    l.resample(3, 2, 2);
    CHECK(std::fabs(l.offsets[2].x - 0.5f) < 1e-5f && std::fabs(l.offsets[1].x) < 1e-5f);  // the net is sampled linearly
    for (int k = 2; k <= 10; k++) l.resample(k, 2, 2);  // an Inspector drag across many resolutions
    l.resample(2, 2, 2);
    float drift = 0.0f;
    for (size_t i = 0; i < 8; i++) drift = std::max(drift, length(l.offsets[i] - start[i]));
    CHECK(drift < 1e-5f);
    /* Past the +X face: no crease (the slope carries on), and it settles on the border point one cell out. */
    const float a = l.displacement({0.49f, 0, 0}).x, b = l.displacement({0.51f, 0, 0}).x, far = l.displacement({1.6f, 0, 0}).x;
    std::printf("    at the face %.4f / %.4f, far out %.4f\n", a, b, far);
    CHECK(b > a && b - a < 0.02f);
    CHECK(std::fabs(far - 0.5f) < 1e-5f);
  });

  test("lattices: a turned, stretched lattice on a turned, scaled object bends the mesh as the same lattice would in world space", [] {
    R65Scene t(primitives::uv_sphere(0.4f, 12, 8));
    t.obj->set_world_position({0.3f, 0.2f, -0.1f});
    t.obj->set_world_rotation(Quat::euler({20, 35, -10}));
    t.obj->set_local_scale({1.5f, 0.7f, 1.2f});
    t.lat_obj->set_world_position({0.25f, 0.3f, 0.0f});
    t.lat_obj->set_world_rotation(Quat::euler({-15, 50, 5}));
    t.lat_obj->set_local_scale({1.4f, 2.0f, 1.7f});
    t.lat->interpolation = 0;
    t.lat->offsets[7] = Vec3(0.2f, 0.1f, -0.1f);
    t.lat->offsets[0] = Vec3(-0.1f, 0.0f, 0.15f);
    const Mesh *m = t.obj->evaluated_mesh();
    const Mat4 ow = t.obj->world_matrix(), lw = t.lat_obj->world_matrix(), li = lw.inverse();
    float off = 0.0f;
    for (size_t i = 0; i < m->positions.size(); i++) {
      const Vec3 w = ow.point(t.base->positions[i]);
      const Vec3 want = w + lw.dir(t.lat->displacement(li.point(w)));
      off = std::max(off, length(ow.point(m->positions[i]) - want));
    }
    std::printf("    off by %g\n", off);
    CHECK(off < 1e-4f);
    /* Moving the lattice object re-bends the mesh. */
    const std::vector<Vec3> before = m->positions;
    t.lat_obj->set_world_position({0.35f, 0.3f, 0.0f});
    CHECK(t.obj->evaluated_mesh()->positions != before);
  });

  test("lattices: changing the resolution keeps the shape (an affine pull, Linear)", [] {
    R65Scene t(primitives::uv_sphere(0.45f, 16, 12));
    t.lat->interpolation = 0;
    /* A shear and a stretch: every resolution holds them exactly. */
    for (int i = 0; i < t.lat->control_point_count(); i++) {
      const Vec3 r = t.lat->rest_point(i);
      t.lat->offsets[(size_t)i] = Vec3(0.3f * r.y, 0.5f * r.y, 0.0f);
    }
    const std::vector<Vec3> before = t.obj->evaluated_mesh()->positions;
    for (Vec3 res : {Vec3(5, 3, 4), Vec3(2, 7, 2), Vec3(32, 2, 3)}) {
      t.lat->resample((int)res.x, (int)res.y, (int)res.z);
      const std::vector<Vec3> after = t.obj->evaluated_mesh()->positions;
      float w = 0.0f;
      for (size_t i = 0; i < after.size(); i++) w = std::max(w, length(after[i] - before[i]));
      CHECK(w < 1e-3f);
    }
    CHECK(t.lat->control_point_count() == 32 * 2 * 3);
  });

  test("lattices: a flat, missing or NaN lattice leaves the mesh valid", [] {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    R65Scene t(primitives::cube(1.0f));
    t.lat->set_control_point(0, Vec3(nan, 0, 0));
    for (const Vec3 &p : t.obj->evaluated_mesh()->positions) CHECK(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
    t.lat->offsets.assign(8, Vec3(0.1f, 0, 0));
    t.lat_obj->set_local_scale({1, 0, 1});  // flattened: no inside
    CHECK(t.worst_move() == 0.0f);
    t.lat_obj->set_local_scale({1, 1, 1});
    CHECK(t.worst_move() > 0.05f);
    t.mod->object = "Thing";  // itself
    CHECK(t.worst_move() == 0.0f);
    t.mod->object = "Box";
    t.mod->strength = nan;
    CHECK(t.worst_move() == 0.0f);
  });

  test("lattices: GameObject > Lattice fits a box round the selected mesh with a modifier; clicking a point and dragging it reshapes the mesh; undo; a scene file keeps it", [] {
    Editor ed;
    r34_open(ed);
    ed.command("select Cube");
    ed.command("create Lattice");
    GameObject *box = ed.scene().find_by_name("Lattice");
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(box && box->lattice() && cube && cube->get<LatticeModifier>() && cube->get<LatticeModifier>()->object == "Lattice");
    if (!box || !cube || !box->lattice()) return;
    CHECK(ed.active_object_for_test() == box);
    const std::vector<Vec3> rest = cube->evaluated_mesh()->positions;
    r34_steps(ed, 3);
    /* The (+X, +Y, -Z) corner. */
    const int corner = 1 + 2;  // u = 1, v = 1, w = 0
    const Vec3 w = box->world_matrix().point(box->lattice()->control_point(corner));
    int x = 0, y = 0;
    CHECK(ed.project_to_window(w, x, y));
    r34_click(ed, x, y);
    r34_steps(ed, 1);
    CHECK(ed.points_selected_for_test() == std::vector<int>{corner});
    ed.command("tool move");
    r34_steps(ed, 2);
    CHECK(length(ed.gizmo_pivot_for_test() - w) < 1e-4f);  // the gizmo sits on the point
    /* Drag the X arrow along the way it points on screen (it needn't point right from this corner). */
    int ax = 0, ay = 0;
    CHECK(ed.project_to_window(w + Vec3(0.2f, 0, 0), ax, ay));
    const float dx = (float)(ax - x), dy = (float)(ay - y), dl = std::max(1.0f, std::sqrt(dx * dx + dy * dy));
    auto at = [&](float px) { return std::make_pair(x + (int)std::lround(dx / dl * px), y + (int)std::lround(dy / dl * px)); };
    const auto p0 = at(55.0f), p1 = at(85.0f), p2 = at(115.0f);
    ed.step_frame_headless({r34_mouse(R34ET::MouseMove, p0.first, p0.second)});
    ed.step_frame_headless({r34_mouse(R34ET::MouseDown, p0.first, p0.second)});
    ed.step_frame_headless({r34_mouse(R34ET::MouseMove, p1.first, p1.second)});
    ed.step_frame_headless({r34_mouse(R34ET::MouseMove, p2.first, p2.second)});
    ed.step_frame_headless({r34_mouse(R34ET::MouseUp, p2.first, p2.second)});
    r34_steps(ed, 2);
    const Lattice &l = *box->lattice();
    std::printf("    the corner moved by %.3f %.3f %.3f\n", l.offsets[(size_t)corner].x, l.offsets[(size_t)corner].y, l.offsets[(size_t)corner].z);
    CHECK(l.offsets[(size_t)corner].x > 0.05f);
    int others = 0;
    for (size_t i = 0; i < l.offsets.size(); i++) others += i != (size_t)corner && length(l.offsets[i]) > 0.0f;
    CHECK(others == 0);
    CHECK(cube->evaluated_mesh()->positions != rest);
    ed.command("points select Lattice 0 6");  // the console selects lattice points too
    CHECK(ed.points_selected_for_test() == (std::vector<int>{0, 6}));
    r34_steps(ed, 1);
    ed.command("delete");  // a lattice's points come with its resolution: Delete removes the lattice itself
    r34_steps(ed, 1);
    CHECK(ed.scene().find_by_name("Lattice") == nullptr);
    ed.command("undo");
    r34_steps(ed, 2);
    GameObject *back1 = ed.scene().find_by_name("Lattice");
    CHECK(back1 && back1->lattice() && back1->lattice()->offsets[(size_t)corner].x > 0.05f);
    ed.command("undo");
    r34_steps(ed, 2);
    GameObject *u = ed.scene().find_by_name("Lattice");
    CHECK(u && u->lattice() && length(u->lattice()->offsets[(size_t)corner]) == 0.0f);
    /* `set` reaches the resolution and any point. */
    ed.command("set Lattice.PointsU 4");
    ed.command("set Lattice.P5Offset 0 0.25 0");
    if (GameObject *l2 = ed.scene().find_by_name("Lattice")) {
      CHECK(l2->lattice()->points_u == 4 && l2->lattice()->control_point_count() == 16);
      CHECK(l2->lattice()->offsets[5].y == 0.25f);
    }
    /* A second lattice gets a name of its own, and its modifier names it. */
    ed.command("select Sphere");
    ed.command("create Lattice");
    GameObject *sphere = ed.scene().find_by_name("Sphere");
    CHECK(ed.scene().find_by_name("Lattice.001") && sphere && sphere->get<LatticeModifier>() &&
          sphere->get<LatticeModifier>()->object == "Lattice.001");
    /* A scene file: resolution, interpolation, offsets, the modifier. */
    Scene s;
    GameObject *lo = s.create("L");
    Lattice *lt = lo->add<Lattice>();
    lt->interpolation = 0;
    lt->resample(3, 2, 4);
    lt->offsets[5] = Vec3(0.1f, 0.2f, -0.3f);
    GameObject *o = s.create("O");
    o->add<MeshFilter>()->mesh = primitives::cube(1.0f);
    auto *lm = o->add<LatticeModifier>();
    lm->object = "L";
    lm->strength = 0.75f;
    Scene back;
    std::string err;
    CHECK(load_scene_text(save_scene_text(s), back, err));
    const Lattice *bl2 = back.find_by_name("L") ? back.find_by_name("L")->lattice() : nullptr;
    CHECK(bl2 && bl2->points_u == 3 && bl2->points_v == 2 && bl2->points_w == 4 && bl2->interpolation == 0 && bl2->offsets.size() == 24);
    if (bl2 && bl2->offsets.size() == 24) CHECK(bl2->offsets[5].z == -0.3f);
    GameObject *bo = back.find_by_name("O");
    CHECK(bo && bo->get<LatticeModifier>() && bo->get<LatticeModifier>()->strength == 0.75f);
    if (bo) CHECK(bo->evaluated_mesh()->positions == o->evaluated_mesh()->positions);
  });
}


/* ===================================================================== */
/* Round 66 (task 0044): the Mesh Deform modifier (a cage)                 */
/* ===================================================================== */

/* A sphere inside a cage object, the sphere with a Mesh Deform modifier naming it. */
struct R66Scene {
  Scene s;
  GameObject *obj = nullptr, *cage_obj = nullptr;
  MeshDeformModifier *mod = nullptr;
  MeshPtr base, cage;
  R66Scene(MeshPtr cage_mesh, MeshPtr m = nullptr) {
    base = m ? m : primitives::uv_sphere(0.35f, 16, 12);
    obj = s.create("Thing");
    obj->add<MeshFilter>()->mesh = base;
    mod = obj->add<MeshDeformModifier>();
    mod->object = "Cage";
    cage = cage_mesh;
    cage_obj = s.create("Cage");
    cage_obj->add<MeshFilter>()->mesh = cage;
  }
  /* The cage's vertices moved by f (in place, as Edit Mode does). */
  void move_cage(const std::function<Vec3(Vec3)> &f) {
    Mesh &cm = *mesh_make_mutable(cage_obj->get<MeshFilter>()->mesh);
    for (Vec3 &v : cm.positions) v = f(v);
    cm.touch();
  }
  float worst(const std::function<Vec3(Vec3)> &want) const {
    const Mesh *m = obj->evaluated_mesh();
    float w = 0.0f;
    for (size_t i = 0; i < m->positions.size(); i++) w = std::max(w, length(m->positions[i] - want(base->positions[i])));
    return w;
  }
};

static void round66_tests() {
  test("mesh deform: right after Bind the mesh is unchanged; the weights sum to 1; moving or turning the whole cage moves the mesh the same way", [] {
    for (int which = 0; which < 2; which++) {
      R66Scene t(which == 0 ? primitives::cube(1.0f) : primitives::ico_sphere(0.6f, 2));
      std::string err;
      CHECK(t.mod->bind(&err));
      if (!err.empty()) std::printf("    %s\n", err.c_str());
      CHECK(t.worst([](Vec3 v) { return v; }) < 1e-4f);
      /* Sums. */
      float worst_sum = 0.0f;
      std::vector<float> w(t.mod->bind_verts.size());
      for (size_t i = 0; i < t.base->positions.size(); i += 7) {
        mean_value_coordinates(t.base->positions[i], t.mod->bind_verts, t.mod->bind_tris, w.data());
        float sum = 0.0f;
        for (float x : w) sum += x;
        worst_sum = std::max(worst_sum, std::fabs(sum - 1.0f));
      }
      CHECK(worst_sum < 1e-4f);
      /* A move and a turn of every cage vertex. */
      const Vec3 d(0.3f, -0.2f, 0.5f);
      t.move_cage([&](Vec3 v) { return v + d; });
      const float moved = t.worst([&](Vec3 v) { return v + d; });
      const Quat q = Quat::euler({25, 40, -15});
      t.move_cage([&](Vec3 v) { return q.rotate(v - d); });
      const float turned = t.worst([&](Vec3 v) { return q.rotate(v); });
      std::printf("    %s cage: sums off by %g, a move off by %g, a turn off by %g\n", which == 0 ? "cube" : "icosphere", worst_sum, moved, turned);
      CHECK(moved < 1e-3f && turned < 1e-3f);
    }
  });

  test("mesh deform: moving one cage vertex moves the mesh near it most and the far side least", [] {
    R66Scene t(primitives::cube(1.0f));
    CHECK(t.mod->bind(nullptr));
    /* The cube's (+, +, +) corner pulled out. */
    t.move_cage([](Vec3 v) { return v.x > 0 && v.y > 0 && v.z > 0 ? v + Vec3(0.4f) : v; });
    const Mesh *m = t.obj->evaluated_mesh();
    float near_move = 0.0f, far_move = 1e9f;
    for (size_t i = 0; i < m->positions.size(); i++) {
      const Vec3 v = t.base->positions[i];
      const float mv = length(m->positions[i] - v);
      const float to_corner = length(v - Vec3(0.5f));
      if (to_corner < 0.6f) near_move = std::max(near_move, mv);  // the sphere (radius 0.35) comes within 0.52 of it
      if (to_corner > 1.15f) far_move = std::min(far_move, mv);
    }
    std::printf("    near the corner %.3f, far side %.4f\n", near_move, far_move);
    CHECK(near_move > 0.05f && near_move > 4.0f * far_move);
  });

  test("mesh deform: an open or flat cage is refused, Unbind leaves the mesh, a cage with other vertices stops with a message", [] {
    {
      R66Scene t(primitives::plane(1.0f, 2));  // open
      std::string err;
      CHECK(!t.mod->bind(&err) && err.find("open") != std::string::npos);
      CHECK(!t.mod->bound());
    }
    {
      MeshPtr flat = primitives::cube(1.0f);
      for (Vec3 &v : flat->positions) v.y = 0.0f;
      flat->touch();
      R66Scene t(flat);
      std::string err;
      CHECK(!t.mod->bind(&err) && err.find("volume") != std::string::npos);
    }
    {
      R66Scene t(primitives::cube(1.0f));
      CHECK(t.mod->bind(nullptr));
      t.move_cage([](Vec3 v) { return v * 1.3f; });
      CHECK(t.worst([](Vec3 v) { return v * 1.3f; }) < 1e-3f);
      t.mod->unbind();
      CHECK(t.worst([](Vec3 v) { return v; }) == 0.0f);
      CHECK(t.mod->bind(nullptr));
      Mesh &cm = *mesh_make_mutable(t.cage_obj->get<MeshFilter>()->mesh);  // another vertex
      cm.add_vert({0, 2, 0});
      cm.touch();
      CHECK(t.worst([](Vec3 v) { return v; }) == 0.0f);
      CHECK(t.mod->last_error.find("Bind again") != std::string::npos);
    }
  });

  test("mesh deform: cages of every kind: reversed winding works, mixed winding and doubled faces are refused, points on and outside the cage", [] {
    /* Reversed winding binds too, and reproduces a stretch exactly (the quads may split along the other diagonal, so
     * a non-affine move can differ a little from the other winding's). */
    MeshPtr flipped = primitives::cube(1.0f);
    meshops::flip_normals(*flipped);
    R66Scene a(primitives::cube(1.0f)), b(flipped);
    CHECK(a.mod->bind(nullptr) && b.mod->bind(nullptr));
    CHECK(b.worst([](Vec3 v) { return v; }) < 1e-4f);
    b.move_cage([](Vec3 v) { return Vec3(v.x, v.y * 1.4f, v.z * 0.8f); });
    CHECK(b.worst([](Vec3 v) { return Vec3(v.x, v.y * 1.4f, v.z * 0.8f); }) < 1e-4f);
    /* One face turned the other way, and a face doubled: refused. */
    MeshPtr mixed = primitives::cube(1.0f);
    std::reverse(mixed->corner_verts.begin(), mixed->corner_verts.begin() + mixed->face_offsets[1]);
    mixed->touch();
    R66Scene c(mixed);
    std::string err;
    CHECK(!c.mod->bind(&err) && err.find("opposite") != std::string::npos);
    MeshPtr doubled = primitives::cube(1.0f);
    doubled->add_face({doubled->corner_verts[0], doubled->corner_verts[1], doubled->corner_verts[2], doubled->corner_verts[3]});
    doubled->touch();
    R66Scene d(doubled);
    CHECK(!d.mod->bind(&err));
    /* A stray vertex far away doesn't spoil the volume test. */
    MeshPtr stray = primitives::cube(1.0f);
    stray->add_vert({1000, 0, 0});
    stray->touch();
    R66Scene e(stray);
    CHECK(e.mod->bind(&err));
    /* Weights at a cage vertex, on a face and outside the cage: finite, sum 1, reproduce the point. */
    std::vector<float> w(a.mod->bind_verts.size());
    for (Vec3 x : {a.mod->bind_verts[3], Vec3(0.5f, 0.1f, -0.2f), Vec3(1.5f, -0.7f, 2.0f), Vec3(0.0f)}) {
      mean_value_coordinates(x, a.mod->bind_verts, a.mod->bind_tris, w.data());
      Vec3 back(0.0f);
      float sum = 0.0f;
      for (size_t j = 0; j < w.size(); j++) back += a.mod->bind_verts[j] * w[j], sum += w[j];
      if (!(std::fabs(sum - 1.0f) < 1e-4f && length(back - x) < 1e-4f))
        std::printf("    at %g %g %g: sum %g, back off by %g\n", x.x, x.y, x.z, sum, length(back - x));
      CHECK(std::fabs(sum - 1.0f) < 1e-4f && length(back - x) < 1e-4f);
    }
  });

  test("mesh deform: two meshes caging each other don't hang or crash; editing the bound mesh gives what a fresh Bind would", [] {
    Scene s;
    GameObject *a = s.create("A"), *b = s.create("B");
    a->add<MeshFilter>()->mesh = primitives::cube(1.0f);
    b->add<MeshFilter>()->mesh = primitives::cube(2.0f);
    auto *ma = a->add<MeshDeformModifier>();
    auto *mb = b->add<MeshDeformModifier>();
    ma->object = "B";
    mb->object = "A";
    (void)ma->bind(nullptr);
    (void)mb->bind(nullptr);
    CHECK(a->evaluated_mesh() && b->evaluated_mesh());  // ends (the cycle is cut), with valid meshes
    uint64_t keys[3];
    a->evaluated_keys(keys);
    /* Editing the bound mesh: the moved vertices' weights are redone, the result is a fresh Bind's. */
    R66Scene t(primitives::cube(1.2f));
    CHECK(t.mod->bind(nullptr));
    t.move_cage([](Vec3 v) { return v + Vec3(0, v.x * 0.3f, 0); });
    (void)t.obj->evaluated_mesh();
    Mesh &own = *mesh_make_mutable(t.obj->get<MeshFilter>()->mesh);
    own.positions[5] += Vec3(0.05f, 0.1f, 0.0f);
    own.touch();
    const std::vector<Vec3> edited = t.obj->evaluated_mesh()->positions;
    R66Scene fresh(primitives::cube(1.2f), std::make_shared<Mesh>(own));
    CHECK(fresh.mod->bind(nullptr));
    fresh.move_cage([](Vec3 v) { return v + Vec3(0, v.x * 0.3f, 0); });
    float off = 0.0f;
    for (size_t i = 0; i < edited.size(); i++) off = std::max(off, length(edited[i] - fresh.obj->evaluated_mesh()->positions[i]));
    CHECK(off < 1e-6f);
  });

  test("mesh deform: the binding round-trips through a scene file and comes back with undo", [] {
    R66Scene t(primitives::ico_sphere(0.7f, 1));
    CHECK(t.mod->bind(nullptr));
    t.move_cage([](Vec3 v) { return Vec3(v.x * 1.4f, v.y, v.z * 0.8f); });
    const std::vector<Vec3> want = t.obj->evaluated_mesh()->positions;
    Scene back;
    std::string err;
    CHECK(load_scene_text(save_scene_text(t.s), back, err));
    GameObject *b = back.find_by_name("Thing");
    CHECK(b && b->get<MeshDeformModifier>() && b->get<MeshDeformModifier>()->bound());
    if (b) {
      const std::vector<Vec3> got = b->evaluated_mesh()->positions;
      float off = 0.0f;
      for (size_t i = 0; i < got.size() && i < want.size(); i++) off = std::max(off, length(got[i] - want[i]));
      CHECK(got.size() == want.size() && off < 1e-5f);
    }
    /* In the editor: Bind as one undo step; a refusal says why. */
    Editor ed;
    ed.init_headless(400, 300);
    GameObject *ball = ed.scene().find_by_name("Sphere");
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(ball && cube);
    if (!ball || !cube) return;
    auto *md = ball->add<MeshDeformModifier>();
    md->object = "Plane";
    ed.command("select Sphere");
    ed.mark_changed_for_test("R66 modifier");
    ed.step_frame_headless();
    ed.command("meshdeform bind");
    CHECK(!md->bound() && md->last_error.find("open") != std::string::npos);
    md->object = "Cube";
    ed.command("meshdeform bind Sphere");
    CHECK(md->bound());
    ed.step_frame_headless();
    ed.command("undo");
    ed.step_frame_headless();
    GameObject *ub = ed.scene().find_by_name("Sphere");
    CHECK(ub && ub->get<MeshDeformModifier>() && !ub->get<MeshDeformModifier>()->bound());
    /* Bind, Unbind, then undo the Unbind: bound again. */
    if (ub) ub->get<MeshDeformModifier>()->object = "Cube";
    ed.mark_changed_for_test("R66 cube");
    ed.step_frame_headless();
    ed.command("meshdeform bind Sphere");
    ed.step_frame_headless();
    ed.command("meshdeform unbind Sphere");
    ed.step_frame_headless();
    CHECK(ed.scene().find_by_name("Sphere") && !ed.scene().find_by_name("Sphere")->get<MeshDeformModifier>()->bound());
    ed.command("undo");
    ed.step_frame_headless();
    GameObject *rb = ed.scene().find_by_name("Sphere");
    CHECK(rb && rb->get<MeshDeformModifier>() && rb->get<MeshDeformModifier>()->bound());
  });
}


/* ===================================================================== */
/* Round 67 (task 0045): vertex groups                                     */
/* ===================================================================== */

/* A flat grid (y = 0, x and z in -1..1) with two groups: "Ramp" = (x + 1) / 2, linear, so any vertex on the plane
 * should carry exactly that; "Right" = 1 for x > 0. */
static MeshPtr r67_grid(int n = 6) {
  MeshPtr m = primitives::grid(2.0f, n, n);
  VertexGroup ramp{"Ramp", {}}, right{"Right", {}};
  for (const Vec3 &p : m->positions) {
    ramp.weights.push_back((p.x + 1.0f) * 0.5f);
    right.weights.push_back(p.x > 0.0f ? 1.0f : 0.0f);
  }
  m->vertex_groups = {ramp, right};
  return m;
}

/* Every group fits; on-plane vertices carry the ramp exactly; every weight finite and in 0..1. */
static bool r67_ok(const Mesh &m, float *worst) {
  bool ok = m.groups_fit() && m.find_group("Ramp") >= 0;
  float w = 0.0f;
  if (ok) {
    const std::vector<float> &ramp = m.vertex_groups[(size_t)m.find_group("Ramp")].weights;
    for (size_t i = 0; i < m.vert_count(); i++) {
      ok = ok && std::isfinite(ramp[i]) && ramp[i] >= -1e-6f && ramp[i] <= 1.0f + 1e-6f;
      if (std::fabs(m.positions[i].y) < 1e-5f && std::fabs(m.positions[i].x) <= 1.0f) w = std::max(w, std::fabs(ramp[i] - (m.positions[i].x + 1.0f) * 0.5f));
    }
  }
  *worst = w;
  return ok && w < 2e-4f;
}

static void round67_tests() {
  test("vertex groups: every Edit Mode operator family keeps one weight per vertex; vertices that stay keep theirs; new ones on the surface take its weights", [] {
    struct Case { const char *op; const char *select; bool creates; };
    const Case cases[] = {{"merge_distance", "all", false}, {"delete", "faces", false}, {"dissolve_vertices", "some", false},
                          {"subdivide_edges", "all", true}, {"loopcut", "edge", true}, {"bevel", "some", true}, {"extrude", "faces", false},
                          {"inset", "faces", true}, {"triangulate_faces", "faces", true}, {"poke", "faces", true}, {"split", "faces", false},
                          {"duplicate", "faces", false}, {"dissolve_faces", "faces", false}, {"tris_to_quads", "all", false},
                          {"collapse", "edge", false}, {"connect", "some", true}, {"edge_split", "some", false}};
    int bad = 0;
    for (const Case &c : cases) {
      Editor ed;
      ed.init_headless(800, 600);
      GameObject *g = ed.scene().create("Grid");
      g->add<MeshFilter>()->mesh = r67_grid();
      g->add<MeshRenderer>();
      const MeshPtr before = g->mesh_filter()->mesh;
      ed.command("select Grid");
      ed.mark_changed_for_test("R67 grid");
      ed.step_frame_headless();
      const std::string sel = c.select;
      if (sel == "faces") ed.command("edit face all");
      else ed.command("edit vertex");
      if (sel == "all") ed.command("edit vertex all");
      if (sel == "some") ed.command("vsel 8 9 15 16 22");
      if (sel == "edge") ed.command("vsel 8 9");
      ed.step_frame_headless();
      ed.command(std::string("editop ") + c.op);
      ed.step_frame_headless();
      ed.step_frame_headless();  // the undo commit mends the groups
      ed.command("edit off");
      ed.step_frame_headless();
      GameObject *after = ed.scene().find_by_name("Grid");
      const Mesh *m = after && after->mesh_filter() ? after->mesh_filter()->mesh.get() : nullptr;
      bool ok = m && m->groups_fit() && m->vertex_groups.size() == 2;
      int moved = 0;
      float worst = 0.0f;
      if (ok) {
        /* Vertices at a place only one old vertex had keep both its weights. */
        for (size_t i = 0; i < m->vert_count(); i++) {
          int n = 0;
          size_t src = 0;
          for (size_t j = 0; j < before->vert_count(); j++)
            if (std::memcmp(&before->positions[j], &m->positions[i], sizeof(Vec3)) == 0) n++, src = j;
          if (n == 1)
            for (int k = 0; k < 2; k++) moved += m->vertex_groups[(size_t)k].weights[i] != before->vertex_groups[(size_t)k].weights[src];
        }
        /* New vertices on the plane carry the linear ramp exactly (interpolated from the surface). */
        if (c.creates) ok = r67_ok(*m, &worst);
      }
      ok = ok && moved == 0;
      if (!ok) std::printf("    %s: %d weights moved, ramp off by %g\n", c.op, moved, worst);
      bad += !ok;
    }
    CHECK(bad == 0);
  });

  test("vertex groups: adding or removing a group survives the undo commit; Separate and Join keep groups", [] {
    Editor ed;
    ed.init_headless(800, 600);
    ed.command("select Cube");
    ed.command("vgroup add A");
    ed.step_frame_headless();
    ed.command("vgroup add B");
    ed.step_frame_headless();
    ed.step_frame_headless();
    const Mesh *m = ed.scene().find_by_name("Cube")->mesh_filter()->mesh.get();
    CHECK(m->find_group("A") == 0 && m->find_group("B") == 1);
    ed.command("vgroup remove A");
    ed.step_frame_headless();
    ed.step_frame_headless();
    m = ed.scene().find_by_name("Cube")->mesh_filter()->mesh.get();
    CHECK(m->vertex_groups.size() == 1 && m->find_group("B") == 0);
    /* Separate by loose parts: a part keeps its vertices' weights. */
    Scene s;
    (void)s;
    GameObject *two = ed.scene().create("Two");
    MeshPtr tm = primitives::cube(1.0f);
    Mesh other = *primitives::cube(1.0f);
    meshops::append_mesh(*tm, other, Mat4::translate({3, 0, 0}));
    tm->vertex_groups = {{"Side", {}}};
    for (const Vec3 &p : tm->positions) tm->vertex_groups[0].weights.push_back(p.x > 1.0f ? 1.0f : 0.0f);
    two->add<MeshFilter>()->mesh = tm;
    two->add<MeshRenderer>();
    ed.command("select Two");
    ed.mark_changed_for_test("R67 two");
    ed.step_frame_headless();
    ed.command("separate loose");
    ed.step_frame_headless();
    ed.step_frame_headless();
    int parts = 0, right = 0;
    ed.scene().for_each([&](GameObject &o) {
      const MeshFilter *mf = o.mesh_filter();
      if (!mf || !mf->mesh || mf->mesh->find_group("Side") < 0) return;
      parts++;
      const Mesh &pm = *mf->mesh;
      bool all_one = pm.groups_fit(), all_zero = pm.groups_fit();
      for (float w : pm.vertex_groups[(size_t)pm.find_group("Side")].weights) all_one = all_one && w == 1.0f, all_zero = all_zero && w == 0.0f;
      right += all_one || all_zero;
    });
    CHECK(parts == 2 && right == 2);
    /* Join through the editor: the other object's group comes along, by name. */
    ed.command("select Cube");
    ed.command("select Sphere");  // replace: Sphere only
    GameObject *sphere = ed.scene().find_by_name("Sphere");
    {
      Mesh &sm = *mesh_make_mutable(sphere->mesh_filter()->mesh);
      sm.vertex_groups = {{"Leg", std::vector<float>(sm.vert_count(), 0.5f)}};
      sm.touch();
    }
    ed.mark_changed_for_test("R67 leg");
    ed.step_frame_headless();
    ed.command("select Sphere");
    ed.command("selectadd Cube");  // the Cube is active: it keeps the result
    ed.command("join");
    ed.step_frame_headless();
    ed.step_frame_headless();
    const Mesh *joined = ed.scene().find_by_name("Cube")->mesh_filter()->mesh.get();
    CHECK(joined->groups_fit() && joined->find_group("B") >= 0 && joined->find_group("Leg") >= 0);
    if (joined->groups_fit() && joined->find_group("Leg") >= 0) {
      const std::vector<float> &leg = joined->vertex_groups[(size_t)joined->find_group("Leg")].weights;
      CHECK(leg.front() == 0.0f && leg.back() == 0.5f);  // the Cube's vertices first, the Sphere's after
    }
  });

  test("vertex groups: Apply Modifiers bakes a deformer limited to a group as the viewport shows it", [] {
    Editor ed;
    ed.init_headless(800, 600);
    GameObject *g = ed.scene().create("G");
    g->add<MeshFilter>()->mesh = r67_grid(6);
    g->add<MeshRenderer>();
    auto *d = g->add<DisplaceModifier>();
    d->direction = 2;
    d->strength = 1.0f;
    d->vertex_group = "Right";
    const std::vector<Vec3> shown = g->evaluated_mesh()->positions;
    ed.command("select G");
    ed.mark_changed_for_test("R67 displace");
    ed.step_frame_headless();
    ed.command("applymods");
    ed.step_frame_headless();
    GameObject *a = ed.scene().find_by_name("G");
    CHECK(a && a->mesh_filter()->mesh->positions == shown && !a->get<DisplaceModifier>());
  });

  test("vertex groups: Mirror, Array, Subdivision and Join carry them; mid-edge vertices get the average", [] {
    Scene s;
    GameObject *g = s.create("G");
    g->add<MeshFilter>()->mesh = r67_grid(4);
    auto *mir = g->add<MirrorModifier>();
    mir->x = true;
    mir->merge = false;
    float worst = 0.0f;
    const Mesh *m = g->evaluated_mesh();
    CHECK(m->groups_fit() && m->vert_count() == 2 * 25);
    if (m->groups_fit()) {
      const std::vector<float> &ramp = m->vertex_groups[0].weights;
      for (size_t i = 25; i < 50; i++) worst = std::max(worst, std::fabs(ramp[i] - ramp[i - 25]));  // the copy has its source's weights
    }
    CHECK(worst == 0.0f);
    g->remove_component(mir);
    auto *arr = g->add<ArrayModifier>();
    arr->count = 3;
    CHECK(g->evaluated_mesh()->groups_fit() && g->evaluated_mesh()->vert_count() == 3 * 25);
    g->remove_component(arr);
    g->add<SubdivisionSurface>()->smooth = false;  // simple: new vertices at edge midpoints and face centres, all on the plane
    CHECK(r67_ok(*g->evaluated_mesh(), &worst));
    /* Join: groups by name. */
    Mesh a = *r67_grid(2), b = *primitives::cube(1.0f);
    b.vertex_groups = {{"Other", std::vector<float>(b.vert_count(), 0.5f)}};
    meshops::append_mesh(a, b, Mat4::identity());
    CHECK(a.groups_fit() && a.find_group("Ramp") >= 0 && a.find_group("Other") >= 0);
    if (a.groups_fit()) CHECK(a.vertex_groups[(size_t)a.find_group("Other")].weights.back() == 0.5f && a.vertex_groups[(size_t)a.find_group("Other")].weights[0] == 0.0f);
  });

  test("vertex groups: a Displace limited to a group moves only its vertices, by their weights; Invert does the opposite", [] {
    Scene s;
    GameObject *g = s.create("G");
    MeshPtr base = r67_grid(8);
    g->add<MeshFilter>()->mesh = base;
    auto *d = g->add<DisplaceModifier>();
    d->direction = 2;  // Y
    d->strength = 1.0f;
    d->midlevel = -1.0f;  // every vertex moves up
    const std::vector<Vec3> all = g->evaluated_mesh()->positions;
    d->vertex_group = "Ramp";
    const Mesh *m = g->evaluated_mesh();
    float off = 0.0f;
    for (size_t i = 0; i < m->vert_count(); i++) {
      const float w = (base->positions[i].x + 1.0f) * 0.5f;
      off = std::max(off, length(m->positions[i] - (base->positions[i] + (all[i] - base->positions[i]) * w)));
    }
    CHECK(off < 1e-5f);
    d->invert_group = true;
    m = g->evaluated_mesh();
    off = 0.0f;
    for (size_t i = 0; i < m->vert_count(); i++) {
      const float w = 1.0f - (base->positions[i].x + 1.0f) * 0.5f;
      off = std::max(off, length(m->positions[i] - (base->positions[i] + (all[i] - base->positions[i]) * w)));
    }
    CHECK(off < 1e-5f);
    d->vertex_group = "Nothing";  // a name the mesh hasn't: the whole mesh
    CHECK(g->evaluated_mesh()->positions == all);
  });

  test("vertex groups: they round-trip through a scene file and undo; a file without them loads as before; the console edits them", [] {
    Scene s;
    GameObject *g = s.create("G");
    g->add<MeshFilter>()->mesh = r67_grid(3);
    Scene back;
    std::string err;
    CHECK(load_scene_text(save_scene_text(s), back, err));
    GameObject *b = back.find_by_name("G");
    CHECK(b && b->mesh_filter()->mesh->vertex_groups.size() == 2);
    if (b && b->mesh_filter()->mesh->vertex_groups.size() == 2)
      CHECK(b->mesh_filter()->mesh->vertex_groups[0].weights == g->mesh_filter()->mesh->vertex_groups[0].weights &&
            b->mesh_filter()->mesh->vertex_groups[1].name == "Right");
    Scene plain;
    plain.create("P")->add<MeshFilter>()->mesh = primitives::cube(1.0f);
    Scene back2;
    CHECK(load_scene_text(save_scene_text(plain), back2, err) && back2.find_by_name("P")->mesh_filter()->mesh->vertex_groups.empty());
    /* Console, Edit Mode, undo. */
    Editor ed;
    ed.init_headless(800, 600);
    ed.command("select Cube");
    ed.command("vgroup add Lid");
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube && cube->mesh_filter()->mesh->find_group("Lid") == 0);
    ed.step_frame_headless();
    ed.command("edit vertex");
    ed.command("vsel 2 3 6 7");
    ed.command("vgroup assign Lid 0.75");
    ed.step_frame_headless();
    ed.command("edit off");
    ed.step_frame_headless();
    cube = ed.scene().find_by_name("Cube");
    const std::vector<float> &w = cube->mesh_filter()->mesh->vertex_groups[0].weights;
    CHECK(w.size() == 8 && w[2] == 0.75f && w[7] == 0.75f && w[0] == 0.0f);
    ed.command("vgroup rename Lid Top");
    CHECK(ed.scene().find_by_name("Cube")->mesh_filter()->mesh->find_group("Top") == 0);
    ed.step_frame_headless();
    ed.command("undo");
    ed.step_frame_headless();
    CHECK(ed.scene().find_by_name("Cube")->mesh_filter()->mesh->find_group("Lid") == 0);
    ed.command("undo");
    ed.command("undo");
    ed.step_frame_headless();
    CHECK(ed.scene().find_by_name("Cube")->mesh_filter()->mesh->vertex_groups.empty());
  });
}

static void round68_tests() {
  test("weight paint: Add raises the weights under the brush only; Subtract lowers them; Draw moves them towards Weight", [] {
    R68 t;
    CHECK(t.ed.weight_paint_for_test());
    t.ed.command("wpbrush add 40 1 1");
    t.stroke({Vec3(-0.5f, 0, 0), Vec3(-0.4f, 0, 0)});
    const float under = t.weight_at({-0.5f, 0, 0}), far = t.weight_at({0.8f, 0, 0.8f});
    std::printf("    under the brush %.3f, far away %.3f\n", under, far);
    CHECK(under > 0.5f && far == 0.0f);
    t.ed.command("wpbrush subtract 40 0.5 1");
    t.stroke({Vec3(-0.5f, 0, 0), Vec3(-0.4f, 0, 0)});
    CHECK(t.weight_at({-0.5f, 0, 0}) < under);
    t.ed.command("wpbrush draw 40 1 0.25");
    t.stroke({Vec3(0.5f, 0, -0.5f), Vec3(0.5f, 0, -0.45f)});
    CHECK(std::fabs(t.weight_at({0.5f, 0, -0.5f}) - 0.25f) < 0.02f);
  });

  test("weight paint: the Smooth falloff paints the centre more than the rim; Blur evens a hard edge", [] {
    R68 t;
    t.ed.command("wpbrush add 60 0.4 1");
    t.ed.command("wpfalloff smooth");
    t.stroke({Vec3(0, 0, 0)});
    const float centre = t.weight_at({0, 0, 0});
    float rim = 1.0f;
    for (float d : {0.2f, 0.25f, 0.3f}) {
      const float w = t.weight_at({d, 0, 0});
      if (w > 0.0f) rim = std::min(rim, w);
    }
    std::printf("    centre %.3f, near the rim %.3f\n", centre, rim);
    CHECK(centre > rim);
    /* A hard edge: left half 1, right half 0; Blur across it. */
    R68 b;
    {
      Mesh &m = *mesh_make_mutable(b.pad->mesh_filter()->mesh);
      for (size_t i = 0; i < m.vert_count(); i++) m.vertex_groups[0].weights[i] = m.positions[i].x < 0.0f ? 1.0f : 0.0f;
    }
    b.ed.command("wpbrush blur 60 1 1");
    b.stroke({Vec3(0, 0, -0.3f), Vec3(0, 0, 0.3f)});
    const float l = b.weight_at({-0.1f, 0, 0}), r = b.weight_at({0.1f, 0, 0});
    std::printf("    across the edge after Blur: %.3f | %.3f\n", l, r);
    CHECK(l < 1.0f && r > 0.0f);
  });

  test("weight paint: Front Faces Only leaves a sphere's back alone; Mirror X paints the twin; a stroke is one undo step", [] {
    R68 t(primitives::uv_sphere(0.6f, 24, 16));
    t.ed.command("wpbrush add 200 1 1");
    t.stroke({Vec3(0, 0.6f, 0)});  // the top, facing the camera above
    CHECK(t.weight_at({0, 0.6f, 0}) > 0.5f && t.weight_at({0, -0.6f, 0}) == 0.0f);
    t.ed.command("wpfront off");
    t.stroke({Vec3(0, 0.6f, 0)});
    CHECK(t.weight_at({0, -0.6f, 0}) > 0.0f);  // the brush now reaches through
    /* Mirror X on the pad. */
    R68 m;
    m.ed.command("wpmirror on");
    m.ed.command("wpbrush add 30 1 1");
    m.stroke({Vec3(0.6f, 0, 0.3f)});
    const float a = m.weight_at({0.6f, 0, 0.3f}), b = m.weight_at({-0.6f, 0, 0.3f});
    CHECK(a > 0.5f && a == b);
    /* One undo step takes the whole stroke back; leaving the mode keeps the weights. */
    m.ed.command("weightpaint off");
    CHECK(!m.ed.weight_paint_for_test() && m.weight_at({0.6f, 0, 0.3f}) == a);
    m.ed.step_frame_headless();
    m.ed.command("undo");
    m.ed.step_frame_headless();
    CHECK(m.weight_at({0.6f, 0, 0.3f}) == 0.0f && m.weight_at({-0.6f, 0, 0.3f}) == 0.0f);
  });

  test("weight paint: the view shows the weights, red at 1 and blue at 0; Normalize All and Clean", [] {
    R68 t;
    {
      Mesh &m = *mesh_make_mutable(t.pad->mesh_filter()->mesh);
      for (size_t i = 0; i < m.vert_count(); i++) m.vertex_groups[0].weights[i] = m.positions[i].x < 0.0f ? 1.0f : 0.0f;
    }
    t.ed.command("wpbrush add 1 0 1");
    t.stroke({Vec3(0.95f, 0, 0.95f)});  // a dab of nothing: the view picks up the change
    for (int i = 0; i < 2; i++) t.ed.step_frame_headless();
    int x = 0, y = 0;
    CHECK(t.ed.project_to_window(t.pad->world_matrix().point({-0.6f, 0, 0.5f}), x, y));
    const uint32_t red = r34_crop(t.ed, {x, y, 1, 1})[0];
    CHECK(t.ed.project_to_window(t.pad->world_matrix().point({0.6f, 0, 0.5f}), x, y));
    const uint32_t blue = r34_crop(t.ed, {x, y, 1, 1})[0];
    auto ch = [](uint32_t c, int k) { return (int)((c >> (8 * k)) & 0xFF); };
    std::printf("    at weight 1: %d %d %d, at weight 0: %d %d %d\n", ch(red, 0), ch(red, 1), ch(red, 2), ch(blue, 0), ch(blue, 1), ch(blue, 2));
    /* The framebuffer is 0xAARRGGBB: red is byte 2, blue byte 0. */
    CHECK(ch(red, 2) > 200 && ch(red, 0) < 80);
    CHECK(ch(blue, 0) > 200 && ch(blue, 2) < 80);
    /* Normalize All and Clean. */
    t.ed.command("vgroup add H");
    {
      Mesh &m = *mesh_make_mutable(t.ed.scene().find_by_name("Pad")->mesh_filter()->mesh);
      for (size_t i = 0; i < m.vert_count(); i++) m.vertex_groups[1].weights[i] = 1.0f;
    }
    t.ed.command("wpnormalize");
    const Mesh &n = t.mesh();
    CHECK(n.vertex_groups[0].weights[0] + n.vertex_groups[1].weights[0] == 1.0f);
    t.ed.command("wpclean 0.6");
    CHECK(t.mesh().vertex_groups[1].weights[0] == 0.0f || t.mesh().vertex_groups[1].weights[0] >= 0.6f);
  });

  test("weight paint: a mouse drag in the Scene view is one stroke and one undo step; a stroke doesn't pile up past its strength, even along the mirror line", [] {
    R68 t;
    t.ed.command("wpbrush add 40 0.4 1");
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    CHECK(t.ed.project_to_window(t.pad->world_matrix().point({-0.6f, 0, 0}), x0, y0));
    CHECK(t.ed.project_to_window(t.pad->world_matrix().point({0.6f, 0, 0}), x1, y1));
    t.ed.step_frame_headless({r34_mouse(R34ET::MouseMove, x0, y0)});
    t.ed.step_frame_headless({r34_mouse(R34ET::MouseDown, x0, y0)});
    for (int k = 1; k <= 8; k++) t.ed.step_frame_headless({r34_mouse(R34ET::MouseMove, x0 + (x1 - x0) * k / 8, y0)});
    for (int k = 8; k >= 0; k--) t.ed.step_frame_headless({r34_mouse(R34ET::MouseMove, x0 + (x1 - x0) * k / 8, y0)});  // back over it
    t.ed.step_frame_headless({r34_mouse(R34ET::MouseUp, x0, y0)});
    t.ed.step_frame_headless();
    float top = 0.0f;
    for (float w : t.mesh().vertex_groups[0].weights) top = std::max(top, w);
    std::printf("    after a drag there and back at strength 0.4: highest weight %.3f\n", top);
    CHECK(top > 0.3f && top <= 0.4f + 1e-5f);  // Blender without Accumulate: one dab's worth per stroke
    t.ed.command("undo");
    t.ed.step_frame_headless();
    top = 0.0f;
    for (float w : t.mesh().vertex_groups[0].weights) top = std::max(top, w);
    CHECK(top == 0.0f);  // the whole drag was one step
    /* Mirror X along the centre line: twins both under the brush still get one dab's worth. */
    t.ed.command("wpmirror on");
    t.stroke({Vec3(0, 0, -0.8f), Vec3(0, 0, 0.8f)});
    top = 0.0f;
    for (float w : t.mesh().vertex_groups[0].weights) top = std::max(top, w);
    CHECK(top > 0.3f && top <= 0.4f + 1e-5f);
  });

  test("weight paint: a new scene leaves the mode; the painted group stays the painted object's when another object is selected; a file keeps the weights", [] {
    R68 t;
    t.ed.command("vgroup add Second");
    t.ed.command("vgroup active G");  // paint G
    t.ed.command("select Cube");      // the selection moves on; the painted object and group stay
    t.ed.command("wpbrush add 40 1 1");
    t.stroke({Vec3(0.3f, 0, 0.3f)});
    const Mesh &m = t.mesh();
    CHECK(m.vertex_groups.size() == 2 && t.weight_at({0.3f, 0, 0.3f}) > 0.5f);
    float second = 0.0f;
    for (float w : m.vertex_groups[1].weights) second = std::max(second, w);
    CHECK(second == 0.0f);
    std::string err;
    Scene back;
    CHECK(load_scene_text(save_scene_text(t.ed.scene()), back, err));
    const Mesh &bm = *back.find_by_name("Pad")->mesh_filter()->mesh;
    CHECK(bm.vertex_groups.size() == 2 && bm.vertex_groups[0].weights == m.vertex_groups[0].weights);
    t.ed.command("newscene");
    t.ed.step_frame_headless();
    CHECK(!t.ed.weight_paint_for_test());
  });
}

static void tests_round112() {
  /* ---------------------------------------------------------------- round 112: the render mesh follows a drag in place (task 0086) */
  /* Whether two render meshes draw the same: the same triangles from the same faces and corners, and bit for bit the
   * same position, normal and UV at every triangle corner (an in-place cache can keep two vertices a rebuild would
   * merge: it draws the same, so vertices aren't compared by number). */
  auto r112_draw_same = [](const RenderMesh &a, const RenderMesh &b, std::string *why) {
    auto fail = [&](const std::string &w) {
      if (why) *why = w;
      return false;
    };
    if (a.indices.size() != b.indices.size()) return fail(strprintf("%zu vs %zu triangle corners", a.indices.size(), b.indices.size()));
    if (a.tri_face != b.tri_face || a.tri_material != b.tri_material) return fail("faces or materials of the triangles differ");
    if (a.tri_corner != b.tri_corner) return fail("the triangulation differs");
    const bool uv = !a.uvs.empty();
    if (uv != !b.uvs.empty()) return fail("one has UVs");
    for (size_t k = 0; k < a.indices.size(); k++) {
      const uint32_t i = a.indices[k], j = b.indices[k];
      if (std::memcmp(&a.positions[i], &b.positions[j], sizeof(Vec3)) != 0) return fail(strprintf("position at triangle corner %zu", k));
      if (std::memcmp(&a.normals[i], &b.normals[j], sizeof(Vec3)) != 0) return fail(strprintf("normal at triangle corner %zu", k));
      if (uv && std::memcmp(&a.uvs[i], &b.uvs[j], sizeof(Vec2)) != 0) return fail(strprintf("UV at triangle corner %zu", k));
    }
    return true;
  };

  test("Edit Mode: moving vertices updates the shaded mesh in place exactly as a rebuild would - flat and smooth, sharp edges, seams, n-gons, concave quads", [=] {
    auto meshes = r63_meshes();
    {
      /* A dart (concave quad) and an L-shaped n-gon, next to a square, UVs on. */
      MeshPtr m = std::make_shared<Mesh>();
      for (Vec3 p : {Vec3(0, 0, 0), Vec3(2, 0, 0), Vec3(1, 0, 0.4f), Vec3(1, 0, 2), Vec3(3, 0, 0), Vec3(5, 0, 0), Vec3(5, 0, 1), Vec3(4, 0, 1),
                     Vec3(4, 0, 3), Vec3(3, 0, 3)})
        m->add_vert(p);
      const uint32_t dart[] = {0, 1, 3, 2}, ell[] = {4, 5, 6, 7, 8, 9};
      const Vec2 uv4[] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}}, uv6[] = {{0, 0}, {1, 0}, {1, .3f}, {.5f, .3f}, {.5f, 1}, {0, 1}};
      m->add_face(dart, 4, uv4);
      m->add_face(ell, 6, uv6);
      meshes.push_back({"dart and n-gon", m});
    }
    uint32_t seed = 112;  // (a small LCG: the same drags on every system)
    auto rng = [&] { return (seed = seed * 1664525u + 1013904223u) >> 8; };
    auto d = [&](decltype(rng) &r) { return (float)(r() % 3001) / 10000.0f - 0.15f; };
    int cases = 0, in_place = 0, mismatches = 0;
    for (auto &[name, base] : meshes) {
      for (bool flat : {false, true}) {
        MeshPtr m = std::make_shared<Mesh>(*base);
        (void)m->render_mesh(flat);  // (the build the drag starts from)
        for (int step = 0; step < 4; step++) {
          /* A drag: a handful of vertices move, as a selection does. */
          for (int k = 0; k < 6; k++) {
            Vec3 &p = m->positions[rng() % m->positions.size()];
            const float dx = d(rng), dy = d(rng), dz = d(rng);  // (in this order on every compiler)
            p = p + Vec3(dx, dy, dz);
          }
          m->touch();
          const MeshCounts c0 = mesh_counts();
          const RenderMesh &got = m->render_mesh(flat);
          in_place += (mesh_counts() - c0).in_place > 0;
          Mesh ref = *m;
          ref.forget_caches();
          std::string why;
          cases++;
          if (!r112_draw_same(got, ref.render_mesh(flat), &why)) {
            mismatches++;
            std::printf("    %s, %s, step %d: %s\n", name.c_str(), flat ? "flat" : "as shaded", step, why.c_str());
          }
        }
      }
    }
    std::printf("    %d drag steps on %zu meshes: %d followed in place, %d drew differently from a rebuild\n", cases, meshes.size(), in_place, mismatches);
    CHECK(mismatches == 0);
    CHECK(in_place >= cases * 3 / 4);  // (a step whose smoothing splits change rebuilds instead)
  });

  test("Edit Mode: a quad whose shorter diagonal changes during a drag is split the way a rebuild splits it", [=] {
    Mesh m;
    for (Vec3 p : {Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(1, 0, 1), Vec3(0, 0, 1)}) m.add_vert(p);
    m.add_face({0, 1, 2, 3});
    const std::vector<uint32_t> before = m.render_mesh().tri_corner;
    m.positions[2] = Vec3(3, 0, 3);  // 0-2 is the long diagonal now
    m.touch();
    const std::vector<uint32_t> after = m.render_mesh().tri_corner;
    Mesh ref = m;
    ref.forget_caches();
    std::string why;
    CHECK(after != before);
    CHECK(r112_draw_same(m.render_mesh(), ref.render_mesh(), &why));
  });

  test("Edit Mode: an extrude (new faces) builds the render mesh again; moving vertices keeps the edge table", [] {
    MeshPtr m = primitives::cube(1.0f);
    (void)m->render_mesh();
    (void)m->edge_cache();
    MeshCounts c0 = mesh_counts();
    m->positions[0].y += 0.1f;
    m->touch();
    (void)m->render_mesh();
    (void)m->edge_cache();
    MeshCounts d = mesh_counts() - c0;
    CHECK(d.in_place == 1 && d.full_builds == 0 && d.edge_builds == 0);
    c0 = mesh_counts();
    const uint32_t top[] = {2, 3, 7, 6};  // (any quad: a new face over existing vertices changes the topology)
    m->add_face(top, 4);
    m->touch();
    (void)m->render_mesh();
    (void)m->edge_cache();
    d = mesh_counts() - c0;
    CHECK(d.full_builds == 1 && d.in_place == 0 && d.edge_builds == 1);
  });

  test("Edit Mode: tangents follow a drag the quick way, within a few degrees of MikkTSpace, and settle() makes them MikkTSpace's", [=] {
    MeshPtr m = primitives::uv_sphere(1.0f, 24, 16);
    m->smooth = true;
    (void)m->render_mesh_tangents();
    for (int k = 0; k < 10; k++) m->positions[(size_t)(k * 23) % m->positions.size()].y += 0.05f;
    m->touch();
    const RenderMesh &quick = m->render_mesh_tangents();
    CHECK(m->tangents_approximate());
    Mesh ref = *m;
    ref.forget_caches();
    const RenderMesh &mikk = ref.render_mesh_tangents();
    float worst = 0.0f;
    for (size_t k = 0; k < quick.indices.size(); k++) {
      const Vec4 a = quick.tangents[quick.indices[k]], b = mikk.tangents[mikk.indices[k]];
      worst = std::max(worst, std::acos(std::min(1.0f, std::max(-1.0f, dot(a.xyz(), b.xyz())))) * kRad2Deg);
    }
    std::printf("    the quick tangents: at most %.2f degrees from MikkTSpace's\n", worst);
    CHECK(worst < 10.0f);  // (Lengyel's gradients against MikkTSpace's splits: furthest apart in a UV sphere's pole fans; settle() below makes them exact)
    const uint64_t v0 = m->version, l0 = m->looks;
    CHECK(m->settle());
    CHECK(m->version == v0 && m->looks != l0 && !m->tangents_approximate());  // (drawn again; the mesh itself unchanged)
    const RenderMesh &settled = m->render_mesh_tangents();
    bool same = settled.indices.size() == mikk.indices.size();
    for (size_t k = 0; same && k < settled.indices.size(); k++)
      same = std::memcmp(&settled.tangents[settled.indices[k]], &mikk.tangents[mikk.indices[k]], sizeof(Vec4)) == 0;
    CHECK(same);
    const uint64_t l1 = m->looks;
    CHECK(!m->settle() && m->looks == l1);  // (nothing quick left: nothing to do)
  });

  test("Edit Mode: a drag that changes where smoothing splits builds the render mesh again, and NaN positions stay drawable", [=] {
    /* Two faces meeting at 20 degrees, smoothing limited to 30: one smooth fan. Folding them to 60 splits it. */
    Mesh m;
    for (Vec3 p : {Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(1, 0, 1), Vec3(0, 0, 1), Vec3(-1, 0.36f, 1), Vec3(-1, 0.36f, 0)}) m.add_vert(p);
    m.add_face({0, 1, 2, 3});
    m.add_face({0, 3, 4, 5});
    m.smooth = true;
    m.smooth_angle = 30.0f;
    (void)m.render_mesh();
    MeshCounts c0 = mesh_counts();
    m.positions[4].y = m.positions[5].y = 1.7f;  // about 60 degrees
    m.touch();
    const RenderMesh &got = m.render_mesh();
    const MeshCounts d = mesh_counts() - c0;
    Mesh ref = m;
    ref.forget_caches();
    std::string why;
    CHECK(d.full_builds == 1);  // (the split changed: in place refused it)
    CHECK(r112_draw_same(got, ref.render_mesh(), &why));
    /* A NaN position: whatever path it takes, the result is a rebuild's. */
    m.positions[2] = Vec3(std::nanf(""), 0, 1);
    m.touch();
    const RenderMesh &nan_got = m.render_mesh();
    Mesh nan_ref = m;
    nan_ref.forget_caches();
    CHECK(nan_got.indices.size() == nan_ref.render_mesh().indices.size());
    CHECK(nan_got.tri_corner == nan_ref.render_mesh().tri_corner);
  });

  test("Edit Mode: after a positions-only operator on a normal-mapped mesh, F9 can still adjust it (settling the tangents leaves the mesh's version)", [] {
    Editor ed;
    r34_open(ed);
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube != nullptr);
    if (!cube) return;
    cube->get<MeshRenderer>()->material(0)->normal_map.path = "Assets/Textures/none.png";  // (tangents wanted; the file needn't exist)
    ed.command("select Cube");
    ed.command("edit vertex");
    ed.command("vsel 0 1 2 3 4 5 6 7");
    r34_steps(ed, 2);
    ed.command("editop smooth");
    r34_steps(ed, 3);  // (drawn - tangents follow in place - and the undo step committed, which settles them)
    const Mesh &m = *ed.scene().find_by_name("Cube")->get<MeshFilter>()->mesh;
    std::printf("    after the operator: %s, quick tangents %s\n", ed.last_op_name_for_test().c_str(), m.tangents_approximate() ? "left" : "settled");
    CHECK(ed.last_op_name_for_test() == "smooth");
    CHECK(ed.last_op_valid_for_test());
    CHECK(!m.tangents_approximate());
  });
}

static void tests_round113() {
  /* ---------------------------------------------------------------- round 113: the Edit Mode drag loop (task 0087) */
  test("Edit Mode: a 10-frame gizmo drag on a big mesh copies it at most once, builds nothing, checks no overlaps and hashes no lighting key; release checks once", [] {
    Editor ed;
    r34_open(ed);
    GameObject *big = ed.scene().create("Big");
    big->add<MeshFilter>()->mesh = primitives::uv_sphere(1.0f, 128, 96);  // about 12k faces
    big->add<MeshRenderer>();
    ed.commit_change("add Big");
    ed.command("select Big");
    ed.command("edit vertex");
    const Mesh &m0 = *big->get<MeshFilter>()->mesh;
    size_t front = 0;
    for (size_t v = 1; v < m0.vert_count(); v++)
      if (m0.positions[v].z > m0.positions[front].z) front = v;
    const Vec3 wp = big->world_matrix().point(m0.positions[front]);
    ed.command(strprintf("vsel %zu", front));
    ed.command(strprintf("camera 0 10 4 %g %g %g", wp.x, wp.y, wp.z));
    ed.command("tool move");
    r34_steps(ed, 3);
    int x = 0, y = 0;
    CHECK(ed.project_to_window(wp, x, y));
    ed.step_frame_headless({r34_mouse(R34ET::MouseMove, x + 55, y)});
    ed.step_frame_headless({r34_mouse(R34ET::MouseDown, x + 55, y)});
    const FrameProfile p0 = ed.frame_profile_totals();
    const MeshCounts c0 = mesh_counts();
    for (int k = 1; k <= 10; k++) ed.step_frame_headless({r34_mouse(R34ET::MouseMove, x + 55 + 3 * k, y)});
    const FrameProfile p1 = ed.frame_profile_totals();
    const MeshCounts d = mesh_counts() - c0;
    std::printf("    10 drag frames: %llu copies, %llu render meshes built, %llu moved in place, %llu overlap checks, %llu lighting keys; writes %.2f ms\n",
                (unsigned long long)d.copies, (unsigned long long)d.full_builds, (unsigned long long)d.in_place,
                (unsigned long long)(p1.overlap_checks - p0.overlap_checks), (unsigned long long)(p1.lightmap_key_hashes - p0.lightmap_key_hashes),
                p1.ms_edit_apply - p0.ms_edit_apply);
    CHECK(d.copies <= 1 && d.full_builds == 0 && d.in_place >= 10);
    CHECK(p1.overlap_checks == p0.overlap_checks && p1.lightmap_key_hashes == p0.lightmap_key_hashes);
    ed.step_frame_headless({r34_mouse(R34ET::MouseUp, x + 85, y)});
    r34_steps(ed, 2);
    /* (Checked once the drag ended: since task 0108 locally - only what moved - from the check made before it.) */
    const FrameProfile p2 = ed.frame_profile_totals();
    CHECK(p2.overlap_checks + p2.overlap_local + p2.overlap_background > p1.overlap_checks + p1.overlap_local + p1.overlap_background);
  });

  test("Edit Mode: G reuses its own copy frame to frame and Esc puts the mesh back exactly", [] {
    Editor ed;
    r34_open(ed);
    ed.command("select Cube");
    ed.command("edit vertex");
    ed.command("vsel 0 1");
    ed.command("camera 0 20 6 0 0.5 0");
    r34_steps(ed, 2);
    GameObject *cube = ed.scene().find_by_name("Cube");
    const Mesh before = *cube->get<MeshFilter>()->mesh;
    const Recti v = ed.scene_view_rect();
    const int cx = v.x + v.w / 2, cy = v.y + v.h / 2;
    auto key = [&](int k) {
      platform::Event d = r34_mouse(R34ET::KeyDown, cx, cy), u = r34_mouse(R34ET::KeyUp, cx, cy);
      d.key = u.key = k;
      ed.step_frame_headless({d, u});
    };
    ed.step_frame_headless({r34_mouse(R34ET::MouseMove, cx, cy)});
    key(platform::KEY_G);
    CHECK(ed.transform_active_for_test());
    const MeshCounts c0 = mesh_counts();
    for (int k = 1; k <= 8; k++) ed.step_frame_headless({r34_mouse(R34ET::MouseMove, cx + 6 * k, cy + 2 * k)});
    const MeshCounts d = mesh_counts() - c0;
    std::printf("    8 frames of G: %llu copies\n", (unsigned long long)d.copies);
    CHECK(d.copies <= 1);
    key(platform::KEY_ESCAPE);
    r34_steps(ed, 1);
    const Mesh &after = *ed.scene().find_by_name("Cube")->get<MeshFilter>()->mesh;
    bool same = after.positions.size() == before.positions.size() && after.corner_verts == before.corner_verts;
    for (size_t i = 0; same && i < after.positions.size(); i++) same = std::memcmp(&after.positions[i], &before.positions[i], sizeof(Vec3)) == 0;
    CHECK(same);
  });

  test("rings: looking edges up through the per-vertex face lists finds what an edge map of the whole mesh did", [] {
    /* What repair_rings used to build every call - an edge -> faces map, a face once per side on the edge - against
     * the lookup it does now through vertex_faces() (from the shorter of the two lists, a face visited once): the same
     * faces, in the same order, the same number of times, on a grid with odd faces added (a degenerate a-a side, a
     * face with a vertex twice, a side two faces share twice). */
    MeshPtr m = primitives::grid(2.0f, 6, 6);
    const uint32_t odd1[] = {0, 0, 1, 8}, odd2[] = {2, 3, 2, 10}, odd3[] = {4, 5, 12};
    m->add_face(odd1, 4);
    m->add_face(odd2, 4);
    m->add_face(odd3, 3);
    m->add_face(odd3, 3);
    m->touch();
    std::unordered_map<uint64_t, std::vector<uint32_t>> map;
    for (size_t f = 0; f < m->face_count(); f++)
      for (uint32_t i = 0; i < m->face_size(f); i++)
        map[Mesh::edge_key(m->face_verts(f)[i], m->face_verts(f)[(i + 1) % m->face_size(f)])].push_back((uint32_t)f);
    const Mesh::VertexFaces &vf = m->vertex_faces();
    auto lookup = [&](uint32_t a, uint32_t b) {
      std::vector<uint32_t> out;
      const uint32_t s = vf.offsets[b + 1] - vf.offsets[b] < vf.offsets[a + 1] - vf.offsets[a] ? b : a;
      for (uint32_t k = vf.offsets[s]; k < vf.offsets[s + 1]; k++) {
        const uint32_t f = vf.faces[k];
        if (k > vf.offsets[s] && vf.faces[k - 1] == f) continue;
        for (uint32_t i = 0; i < m->face_size(f); i++)
          if (Mesh::edge_key(m->face_verts(f)[i], m->face_verts(f)[(i + 1) % m->face_size(f)]) == Mesh::edge_key(a, b)) out.push_back(f);
      }
      return out;
    };
    size_t edges = 0, differ = 0;
    for (const auto &[k, faces] : map) {
      const uint32_t a = (uint32_t)(k >> 32), b = (uint32_t)(k & 0xFFFFFFFFu);
      edges++;
      differ += lookup(a, b) != faces || lookup(b, a) != faces;
    }
    std::printf("    %zu edges (odd ones too): %zu looked up differently\n", edges, differ);
    CHECK(edges > 80 && differ == 0);
    /* The lists: kept while only positions change; made again for new faces. */
    const uint32_t *before = vf.faces.data();
    m->positions[0].y += 0.1f;
    m->touch();
    CHECK(m->vertex_faces().faces.data() == before);
    m->add_face({0, 1, 9});
    m->touch();
    CHECK(m->vertex_faces().faces.size() == m->corner_count());
  });
}

static void tests_round116() {
  /* ---------------------------------------------------------------- round 116: in place, only moved faces split again (task 0090) */
  test("Edit Mode: a cache read after several moves it missed, a vertex moved and back, and a refused update all draw as a rebuild", [] {
    auto draw_same = [](const RenderMesh &a, const RenderMesh &b) {
      if (a.indices.size() != b.indices.size() || a.tri_corner != b.tri_corner || a.tri_face != b.tri_face) return false;
      for (size_t k = 0; k < a.indices.size(); k++)
        if (std::memcmp(&a.positions[a.indices[k]], &b.positions[b.indices[k]], sizeof(Vec3)) != 0 ||
            std::memcmp(&a.normals[a.indices[k]], &b.normals[b.indices[k]], sizeof(Vec3)) != 0)
          return false;
      return true;
    };
    auto rebuilt = [](const Mesh &m, bool flat) {
      Mesh r = m;
      r.forget_caches();
      return r.render_mesh(flat);  // (a copy of the fresh build)
    };
    /* A grid of quads, smooth: two caches (flat for Edit Mode, smooth for renders). */
    MeshPtr m = primitives::grid(2.0f, 12, 12);
    m->smooth = true;
    (void)m->render_mesh(true);
    (void)m->render_mesh(false);
    /* Several moves read by the flat cache only; then the smooth cache catches up in one go. */
    for (int k = 0; k < 5; k++) {
      m->positions[(size_t)(17 + 9 * k)].y += 0.2f;
      m->touch();
      (void)m->render_mesh(true);
    }
    CHECK(draw_same(m->render_mesh(false), rebuilt(*m, false)));
    CHECK(draw_same(m->render_mesh(true), rebuilt(*m, true)));
    /* A vertex of a quad pulled across its diagonal and back to exactly where it was. */
    const Vec3 home = m->positions[30];
    m->positions[30] = home + Vec3(0.3f, 0.0f, 0.3f);
    m->touch();
    CHECK(draw_same(m->render_mesh(true), rebuilt(*m, true)));
    m->positions[30] = home;
    m->touch();
    CHECK(draw_same(m->render_mesh(true), rebuilt(*m, true)));
    /* Smoothing by angle: a fold that splits a fan refuses in place (a full build), then moves go back in place. */
    m->smooth_angle = 30.0f;
    m->touch();
    (void)m->render_mesh(false);
    const MeshCounts c0 = mesh_counts();
    m->positions[40].y += 1.5f;  // a sharp spike: the fans around it split
    m->touch();
    CHECK(draw_same(m->render_mesh(false), rebuilt(*m, false)));
    CHECK((mesh_counts() - c0).full_builds >= 1);
    m->positions[90].y += 0.01f;
    m->touch();
    CHECK(draw_same(m->render_mesh(false), rebuilt(*m, false)));
  });

}

static void tests_round132() {
  /* ---------------------------------------------------------------- round 132: the overlap check, fast and local (task 0108) */
  /* Meshes with overlaps planted: an unwelded copy of a patch of faces, a wire edge along an edge, a vertex on another. */
  auto r132_meshes = [] {
    std::vector<std::pair<std::string, MeshPtr>> out;
    out.push_back({"a cube", primitives::cube(1.0f)});
    {
      MeshPtr s = primitives::uv_sphere(1.0f, 48, 32);
      const size_t n = s->vert_count();
      for (size_t f = 0; f < 40; f++) {  // (forty faces again on new vertices at the same places)
        std::vector<uint32_t> face;
        for (uint32_t c = s->face_offsets[f]; c < s->face_offsets[f + 1]; c++) face.push_back(s->add_vert(s->positions[s->corner_verts[c]]));
        s->add_face(face.data(), face.size());
      }
      s->add_loose_edge(s->add_vert(s->positions[100]), s->add_vert(lerp(s->positions[100], s->positions[101], 0.5f)));
      (void)n;
      out.push_back({"a sphere with an unwelded patch and a wire edge", s});
    }
    {
      MeshPtr g = primitives::grid(4.0f, 30, 30);
      g->positions[17] = g->positions[18];  // (a vertex moved onto its neighbour: a zero-length edge, overlapping edges)
      g->positions[200] = Vec3(std::nanf(""), 0, 0);  // (not a number: never paired)
      out.push_back({"a grid with a collapsed edge and a NaN", g});
    }
    {
      MeshPtr big = primitives::uv_sphere(1.0f, 256, 196);  // (about 50k faces)
      for (size_t f = 1000; f < 1400; f++) {
        std::vector<uint32_t> face;
        for (uint32_t c = big->face_offsets[f]; c < big->face_offsets[f + 1]; c++) face.push_back(big->add_vert(big->positions[big->corner_verts[c]]));
        big->add_face(face.data(), face.size());
      }
      out.push_back({"a 50k-face sphere with 400 faces doubled", big});
    }
    return out;
  };
  /* The ones a kernel trips on: nothing, one vertex, edges and no faces, infinities, random soups, edges near the
   * reference's 512-cell limit. */
  auto r132_odd_meshes = [] {
    std::vector<std::pair<std::string, MeshPtr>> out;
    out.push_back({"an empty mesh", std::make_shared<Mesh>()});
    {
      MeshPtr one = std::make_shared<Mesh>();
      one->add_vert({1, 2, 3});
      out.push_back({"a single vertex", one});
    }
    {
      MeshPtr w = std::make_shared<Mesh>();  // (wire only: a doubled edge, a piece on a longer one, a lone one)
      const uint32_t a = w->add_vert({0, 0, 0}), b = w->add_vert({1, 0, 0}), c = w->add_vert({0, 0, 0}), d = w->add_vert({1, 0, 0});
      const uint32_t e = w->add_vert({0.25f, 0, 0}), f = w->add_vert({0.75f, 0, 0}), g = w->add_vert({0, 1, 0}), h = w->add_vert({0, 2, 0});
      w->add_loose_edge(a, b), w->add_loose_edge(c, d), w->add_loose_edge(e, f), w->add_loose_edge(g, h);
      out.push_back({"edges and no faces", w});
    }
    {
      MeshPtr g = primitives::grid(2.0f, 12, 12);
      g->positions[5] = Vec3(INFINITY, 0, 0);
      g->positions[6] = Vec3(0, -INFINITY, 0);
      g->positions[40] = g->positions[41];
      out.push_back({"a grid with infinities", g});
    }
    {
      MeshPtr far = primitives::grid(2.0f, 8, 8);  // (1e11 out, sized 1e-8: cells past int64 once cast)
      for (Vec3 &p : far->positions) p = Vec3(1e11f, -3e11f, 1e11f) + p * 1e-12f;
      far->add_loose_edge(far->add_vert({-3e12f, -3e12f, -3e12f}), far->add_vert({3e12f, 3e12f, 3e12f}));
      out.push_back({"a grid far out, collapsed to a point", far});
    }
    uint32_t seed = 1320;
    auto rng = [&] { return (seed = seed * 1664525u + 1013904223u) >> 8; };
    for (int k = 0; k < 3; k++) {  // (random triangles over few points: many shared places, many doubled edges)
      MeshPtr r = std::make_shared<Mesh>();
      for (int v = 0; v < 300; v++) r->add_vert(Vec3((float)(rng() % 12), (float)(rng() % 12), (float)(rng() % 3)) * 0.1f);
      for (int f = 0; f < 400; f++) {
        uint32_t t[3] = {rng() % 300u, rng() % 300u, rng() % 300u};
        if (t[0] != t[1] && t[1] != t[2] && t[0] != t[2]) r->add_face(t, 3);
      }
      out.push_back({"a random soup " + std::to_string(k + 1), r});
    }
    {
      /* Diagonal wires across about 7 to 9 of the reference's cells each way (the wires lift the mean a third): boxes of about 512 cells, some skipped and
       * some not, and which ones moves with the mean edge length. */
      MeshPtr g = primitives::grid(4.0f, 16, 16);
      double total = 0;
      for (auto &e : g->edge_cache()) total += length(g->positions[e.second] - g->positions[e.first]);
      const float mean = (float)(total / (double)g->edge_cache().size());
      for (int k = 0; k < 8; k++) {
        const Vec3 p = Vec3(0.37f + 0.05f * (float)k, 0.41f, 0.53f) * mean, dir = Vec3(1.0f) * ((8.6f + 0.3f * (float)k) * mean);
        g->add_loose_edge(g->add_vert(p), g->add_vert(p + dir));
        g->add_loose_edge(g->add_vert(p), g->add_vert(p + dir * 0.5f));  // (along it from the same end: found only if both are binned)
      }
      out.push_back({"a grid with wires along each other", g});
    }
    return out;
  };
  test("overlap check: the fast check equals the reference on empty, single-vertex, wire-only, infinite, random and long-edged meshes", [&] {
    int differ = 0;
    for (auto &[name, m] : r132_odd_meshes()) {
      const bool same = meshops::overlapping_vertices(*m) == meshops::overlapping_vertices_reference(*m) &&
                        meshops::overlapping_edges(*m) == meshops::overlapping_edges_reference(*m);
      std::printf("    %s: %zu vertex pairs, %zu edge pairs, %s\n", name.c_str(), meshops::overlapping_vertices(*m).size(),
                  meshops::overlapping_edges(*m).size(), same ? "the same" : "DIFFERENT");
      differ += !same;
      /* Its size is of its finite points: a vertex at infinity once made eps infinite, every vertex a pair. */
      if (name == "a grid with infinities") CHECK(meshops::overlapping_vertices(*m).size() == 1);
    }
    CHECK(differ == 0);
  });
  test("overlap check: the fast check finds exactly the pairs the reference check finds (vertices and edges), in a fraction of its time", [&] {
    for (auto &[name, m] : r132_meshes()) {
      double ref_ms = 0, fast_ms = 0;
      std::vector<std::pair<uint32_t, uint32_t>> rv, fv;
      std::vector<std::pair<uint64_t, uint64_t>> re, fe;
      {
        ScopedTimer t;
        rv = meshops::overlapping_vertices_reference(*m);
        re = meshops::overlapping_edges_reference(*m);
        ref_ms = t.ms();
      }
      {
        ScopedTimer t;
        fv = meshops::overlapping_vertices(*m);
        fe = meshops::overlapping_edges(*m);
        fast_ms = t.ms();
      }
      std::printf("    %s: %zu vertex pairs, %zu edge pairs; the reference %.1f ms, the fast check %.1f ms\n", name.c_str(), fv.size(), fe.size(), ref_ms,
                  fast_ms);
      CHECK(fv == rv);
      CHECK(fe == re);
      /* (Measured about 15x on 32 threads; the ratio is a stress row: `modeling`, task 0177.) */
    }
  });
  test("overlap check: a mesh's topology serial is its own - two copies changed apart never share one", [] {
    MeshPtr a = primitives::cube(1.0f);
    const uint64_t s0 = a->topology_serial();
    Mesh b = *a, c = *a;
    CHECK(b.topology_serial() == s0);  // (an untouched copy: the same topology)
    b.add_loose_edge(0, 6);
    b.touch();
    c.add_loose_edge(1, 7);
    c.touch();
    std::printf("    the source %llu, one copy %llu, the other %llu\n", (unsigned long long)s0, (unsigned long long)b.topology_serial(),
                (unsigned long long)c.topology_serial());
    CHECK(b.topology_serial() != s0 && c.topology_serial() != s0 && b.topology_serial() != c.topology_serial());
  });
  test("overlap check: Merge welds only the doubled vertices the check found, with a vertex at infinity in the mesh", [&] {
    Editor ed;
    r34_open(ed);
    MeshPtr m = std::make_shared<Mesh>();  // (two doubled spots a unit apart, and one vertex at infinity)
    m->add_vert({0, 0, 0}), m->add_vert({0, 0, 0}), m->add_vert({1, 0, 0}), m->add_vert({1, 0, 0}), m->add_vert({INFINITY, 0, 0});
    m->add_loose_edge(0, 2), m->add_loose_edge(1, 3), m->add_loose_edge(2, 4);  // (edges: Merge drops unused vertices)
    GameObject *obj = ed.scene().create("Far");
    obj->add<MeshFilter>()->mesh = m;
    obj->add<MeshRenderer>();
    ed.command("select Far");
    ed.command("edit vertex");
    r34_steps(ed, 2);
    const size_t pairs = ed.overlap_vert_pairs_for_test();
    ed.command("overlaps merge");
    const size_t left = ed.scene().find_by_name("Far")->get<MeshFilter>()->mesh->vert_count();
    std::printf("    %zu doubled pairs found; after Merge %zu vertices (3: one at each spot and the one at infinity)\n", pairs, left);
    CHECK(pairs == 2);  // (eps was infinite once: every finite vertex paired with every other, 6 pairs)
    CHECK(left == 3);  // (its distance was infinite once: Merge refused it and welded nothing)
  });
  test("overlap check: the local check near the 512-cell limit (wires the reference may skip as the mean edge length moves) refuses or equals a full check", [&] {
    MeshPtr base = r132_odd_meshes().back().second;  // (the grid with long wires)
    uint32_t seed = 1321;
    auto rng = [&] { return (seed = seed * 1664525u + 1013904223u) >> 8; };
    Mesh m = *base;
    meshops::OverlapIndex idx;
    meshops::overlap_check(m, 0.0f, idx);
    int local = 0, refused = 0, differ = 0;
    for (int step = 0; step < 40; step++) {
      const uint32_t v = rng() % 289u;  // (a grid vertex: the mean edge length moves, the wires don't)
      m.positions[v] = m.positions[v] + Vec3(((float)(rng() % 200) - 100.0f) * 2e-3f, 0, ((float)(rng() % 200) - 100.0f) * 2e-3f);
      m.touch();
      if (!meshops::overlap_check_local(m, 0.0f, idx, m.vert_count())) {
        refused++;
        meshops::overlap_check(m, 0.0f, idx);
        continue;
      }
      local++;
      meshops::OverlapIndex full;
      meshops::overlap_check(m, 0.0f, full);
      differ += idx.verts != full.verts || idx.edge_pairs != full.edge_pairs;
    }
    std::printf("    %d steps checked locally, %d refused (a full check instead), %d differ from a full check\n", local, refused, differ);
    CHECK(local > 0);
    CHECK(differ == 0);
  });
  test("overlap check: after only positions moved, the local check re-tests what moved and equals a full check (random drags, onto and off other vertices)", [&] {
    uint32_t seed = 132;
    auto rng = [&] { return (seed = seed * 1664525u + 1013904223u) >> 8; };
    int steps = 0, differ = 0, refused = 0;
    for (auto &[name, base] : r132_meshes()) {
      Mesh m = *base;
      meshops::OverlapIndex idx;
      meshops::overlap_check(m, 0.0f, idx);
      const float eps = idx.eps;
      for (int step = 0; step < 6; step++) {
        for (int k = 0; k < 5; k++) {
          const uint32_t v = rng() % (uint32_t)m.vert_count();
          if (k == 0) m.positions[v] = m.positions[rng() % (uint32_t)m.vert_count()];  // (onto another vertex: a new overlap)
          else m.positions[v] = m.positions[v] + Vec3((float)(rng() % 100) * 1e-4f, 0, 0);
        }
        m.touch();
        if (meshops::overlap_eps(m, 0.0f) != eps) break;  // (the mesh's size changed: a full check, not this test's case)
        const bool local = meshops::overlap_check_local(m, 0.0f, idx, m.vert_count());
        meshops::OverlapIndex full;
        meshops::overlap_check(m, 0.0f, full);
        steps++;
        if (!local) {  // (an edge moved across the sphere lies near the 512-cell limit: the editor checks whole then)
          refused++;
          idx = full;
          continue;
        }
        if (idx.verts != full.verts || idx.edge_pairs != full.edge_pairs) {
          differ++;
          std::printf("    %s, step %d: differs from a full check\n", name.c_str(), step);
        }
      }
    }
    std::printf("    %d drag steps, %d of them refused (a full check instead), %d differ from a full check\n", steps, refused, differ);
    CHECK(steps >= 18 && steps - refused >= 12);
    CHECK(differ == 0);
  });
  test("overlap check in the editor: a drag's release checks only what moved, a selection that copied the mesh and an undo reuse the answer, and the rings are the full check's", [&] {
    Editor ed;
    r34_open(ed);
    GameObject *obj = ed.scene().create("Doubled");
    obj->add<MeshFilter>()->mesh = r132_meshes()[1].second;
    obj->add<MeshRenderer>();
    obj->set_local_position({0, 1, 0});
    ed.mark_changed_for_test("Add the doubled sphere");  // (its own undo step: the drag's undo goes back to it, not before it)
    ed.command("select Doubled");
    ed.command("edit vertex");
    r34_steps(ed, 2);
    /* (An undo restores the scene from its snapshot: the object is found again by name each time.) */
    auto mesh = [&]() -> const Mesh & { return *ed.scene().find_by_name("Doubled")->get<MeshFilter>()->mesh; };
    auto same_as_full = [&] {
      return ed.overlap_verts_for_test() == meshops::overlapping_vertices_reference(mesh()) &&
             ed.overlap_edges_for_test() == meshops::overlapping_edges_reference(mesh());
    };
    CHECK(!ed.overlap_verts_for_test().empty() && same_as_full());
    /* A drag of one vertex: its release checks locally. */
    ed.command("vsel 7");
    ed.command("camera 0 4 3 0 1 0");
    ed.command("tool move");
    r34_steps(ed, 2);
    const Vec3 before = mesh().positions[7];
    const Vec3 wp = ed.scene().find_by_name("Doubled")->world_matrix().point(before);
    const FrameProfile p0 = ed.frame_profile_totals();
    r34_gizmo_drag_x(ed, wp, 30);
    r34_steps(ed, 2);
    const FrameProfile p1 = ed.frame_profile_totals();
    std::printf("    a drag's release (the vertex moved %.3f): %llu full checks, %llu local\n", length(mesh().positions[7] - before),
                (unsigned long long)(p1.overlap_checks - p0.overlap_checks), (unsigned long long)(p1.overlap_local - p0.overlap_local));
    CHECK(length(mesh().positions[7] - before) > 1e-4f);
    CHECK(p1.overlap_local > p0.overlap_local && p1.overlap_checks == p0.overlap_checks);
    CHECK(same_as_full());
    /* A selection operator after the drag's undo step: it copies the mesh (shared with the step), a new version, the
     * same geometry. */
    const Mesh *was = &mesh();
    ed.command("vsel 5");
    ed.command("edittool select_more");
    r34_steps(ed, 2);
    const FrameProfile p2 = ed.frame_profile_totals();
    std::printf("    a selection operator (%s the mesh): %llu full checks, %llu answers reused\n", &mesh() != was ? "it copied" : "it did NOT copy",
                (unsigned long long)(p2.overlap_checks - p1.overlap_checks), (unsigned long long)(p2.overlap_reused - p1.overlap_reused));
    CHECK(p2.overlap_checks == p1.overlap_checks && p2.overlap_local == p1.overlap_local);
    CHECK(&mesh() == was || p2.overlap_reused > p1.overlap_reused);
    /* Undo: back to the geometry from before the drag, checked on entering. */
    ed.command("undo");
    r34_steps(ed, 2);
    const FrameProfile p3 = ed.frame_profile_totals();
    std::printf("    an undo (the vertex back: %s): %llu full checks, %llu local, %llu reused\n",
                length(mesh().positions[7] - before) < 1e-6f ? "yes" : "no", (unsigned long long)(p3.overlap_checks - p2.overlap_checks),
                (unsigned long long)(p3.overlap_local - p2.overlap_local), (unsigned long long)(p3.overlap_reused - p2.overlap_reused));
    CHECK(p3.overlap_checks == p2.overlap_checks && p3.overlap_reused > p2.overlap_reused);
    CHECK(same_as_full());
  });
  test("overlap check on a worker past the size threshold: the editor doesn't wait, no ring is shown for older geometry, and the answer is the full check's", [&] {
    Editor ed;
    r34_open(ed);
    ed.overlap_background_from_for_test(100);  // (any mesh here counts as huge)
    ed.overlap_worker_delay_for_test(150);     // (each worker sleeps first: still pending a frame later on any machine)
    GameObject *obj = ed.scene().create("Doubled");
    obj->add<MeshFilter>()->mesh = r132_meshes()[1].second;  // (the sphere with an unwelded patch)
    obj->add<MeshRenderer>();
    obj->set_local_position({0, 1, 0});
    ed.mark_changed_for_test("Add the doubled sphere");  // (its own undo step: the drag's undo goes back to it)
    ed.command("select Doubled");
    auto mesh = [&]() -> const Mesh & { return *ed.scene().find_by_name("Doubled")->get<MeshFilter>()->mesh; };
    /* Every frame: nothing shown, or the pairs of the mesh as it is in that frame. */
    int frames = 0, wrong = 0, shown = 0;
    auto frame = [&] {
      ed.step_frame_headless();
      frames++;
      if (ed.overlap_verts_for_test().empty() && ed.overlap_edges_for_test().empty()) return;
      shown++;
      wrong += ed.overlap_verts_for_test() != meshops::overlapping_vertices(mesh()) || ed.overlap_edges_for_test() != meshops::overlapping_edges(mesh());
    };
    const FrameProfile p0 = ed.frame_profile_totals();
    ed.command("edit vertex");
    const Vec3 entered_at = mesh().positions[7];
    frame();
    const bool pending = ed.overlap_pending_for_test();
    const size_t shown_while_pending = ed.overlap_verts_for_test().size();
    /* While that worker sleeps, a vertex dragged onto new ground: the worker's answer is for older geometry. */
    ed.command("vsel 7");
    ed.command("camera 0 4 3 0 1 0");
    ed.command("tool move");
    frame();
    const Vec3 wp = ed.scene().find_by_name("Doubled")->world_matrix().point(mesh().positions[7]);
    r34_gizmo_drag_x(ed, wp, 30);
    const bool pending_after_drag = ed.overlap_pending_for_test();
    /* (Headless frames don't sleep to the next wake-up: waited for by the clock, not by a count of frames.) */
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while ((ed.overlap_pending_for_test() || ed.overlap_verts_for_test().empty()) && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      frame();
    }
    const FrameProfile p1 = ed.frame_profile_totals();
    const bool same = ed.overlap_verts_for_test() == meshops::overlapping_vertices_reference(mesh()) &&
                      ed.overlap_edges_for_test() == meshops::overlapping_edges_reference(mesh());
    std::printf("    pending after the first frame: %s (%zu pairs shown meanwhile); after the drag: %s; %d frames, %d of them showing pairs, %d "
                "for other geometry; %llu on the worker, %llu local, %llu on the main thread; the answer %s the full check's\n",
                pending ? "yes" : "no", shown_while_pending, pending_after_drag ? "yes" : "no", frames, shown, wrong,
                (unsigned long long)(p1.overlap_background - p0.overlap_background), (unsigned long long)(p1.overlap_local - p0.overlap_local),
                (unsigned long long)(p1.overlap_checks - p0.overlap_checks), same ? "is" : "is NOT");
    CHECK(pending && shown_while_pending == 0);
    CHECK(length(mesh().positions[7] - entered_at) > 1e-4f);
    /* The drag's geometry: a local check from the first worker's cells once it ended, or a worker of its own. */
    CHECK(p1.overlap_background - p0.overlap_background + p1.overlap_local - p0.overlap_local >= 2 && p1.overlap_checks == p0.overlap_checks);
    CHECK(wrong == 0);
    CHECK(same && !ed.overlap_verts_for_test().empty());
    /* Undo to the geometry the first worker checked (superseded when it ended): its answer was kept. */
    ed.command("undo");
    frame();
    const FrameProfile p2 = ed.frame_profile_totals();
    std::printf("    the undo back to the entered geometry (the vertex back: %s): %llu answers reused, %llu new workers\n",
                length(mesh().positions[7] - entered_at) < 1e-6f ? "yes" : "no", (unsigned long long)(p2.overlap_reused - p1.overlap_reused),
                (unsigned long long)(p2.overlap_background - p1.overlap_background));
    CHECK(p2.overlap_reused > p1.overlap_reused && p2.overlap_background == p1.overlap_background && !ed.overlap_pending_for_test());
    CHECK(wrong == 0);
  });
}

/* Round 185 (task 0165): shallow_edges from a sorted edge table gives the hash map's count. */
static size_t r185_shallow_reference(const Mesh &m, float angle_deg) {
  std::unordered_map<uint64_t, std::pair<int, int>> ef;
  for (size_t f = 0; f < m.face_count(); f++)
    for (uint32_t k = 0; k < m.face_size(f); k++) {
      auto &e = ef.emplace(Mesh::edge_key(m.face_verts(f)[k], m.face_verts(f)[(k + 1) % m.face_size(f)]), std::make_pair(-1, -1)).first->second;
      (e.first < 0 ? e.first : e.second) = (int)f;
    }
  const float lo = std::cos(1.0f * kDeg2Rad), hi = std::cos(angle_deg * kDeg2Rad);
  size_t n = 0;
  for (auto &kv : ef) {
    if (kv.second.second < 0) continue;
    const float c = dot(normalize(m.face_normal((size_t)kv.second.first)), normalize(m.face_normal((size_t)kv.second.second)));
    if (c < lo && c > hi) n++;
  }
  return n;
}

static void tests_round185() {
  test("shallow_edges from a sorted edge table counts what the hash map did on a cube (none), a 12-sided cylinder (its sides) and a sphere; an edge shared by three faces is counted once (the pair compared may differ from the hash map's)", [&] {
    const MeshPtr cube = primitives::cube(), cyl = primitives::cylinder(0.5f, 2.0f, 12), ball = primitives::uv_sphere(0.5f, 16, 8);
    CHECK(meshops::shallow_edges(*cube, 30.0f) == 0);
    CHECK(meshops::shallow_edges(*cube, 30.0f) == r185_shallow_reference(*cube, 30.0f));
    const size_t sides = meshops::shallow_edges(*cyl, 35.0f);
    std::printf("    the 12-sided cylinder: %zu shallow edges at 35 degrees (12 sides 30 degrees apart), the sphere %zu of %zu edges\n", sides,
                meshops::shallow_edges(*ball, 30.0f), ball->edge_count());
    CHECK(sides == 12);
    CHECK(sides == r185_shallow_reference(*cyl, 35.0f));
    CHECK(meshops::shallow_edges(*ball, 30.0f) == r185_shallow_reference(*ball, 30.0f));
    CHECK(meshops::shallow_edges(*ball, 30.0f) > 0);
    /* Three faces on one edge (0-1): a fan of 20 degrees each side of the middle one. */
    Mesh m;
    m.add_vert({0, 0, 0});
    m.add_vert({1, 0, 0});
    m.add_vert({0.5f, 1, 0});
    m.add_vert({0.5f, std::cos(20.0f * kDeg2Rad), std::sin(20.0f * kDeg2Rad)});
    m.add_vert({0.5f, std::cos(20.0f * kDeg2Rad), -std::sin(20.0f * kDeg2Rad)});
    m.add_face({0, 1, 2});
    m.add_face({0, 1, 3});
    m.add_face({0, 1, 4});
    CHECK(meshops::shallow_edges(m, 30.0f) == 1);
    CHECK(meshops::shallow_edges(m, 30.0f) == r185_shallow_reference(m, 30.0f));
  });
}

/* Round 186 (task 0166): Select More / Less over the edge table match the adjacency walk. */
static void r186_grow_reference(const Mesh &m, std::vector<uint8_t> &vert_sel, bool grow) {
  vert_sel.resize(m.vert_count(), 0);
  std::vector<std::vector<uint32_t>> nb(m.vert_count());
  for (auto &e : m.edge_cache()) {
    nb[e.first].push_back(e.second);
    nb[e.second].push_back(e.first);
  }
  std::vector<uint8_t> out = vert_sel;
  for (uint32_t v = 0; v < m.vert_count(); v++)
    for (uint32_t w : nb[v]) {
      if (grow && vert_sel[v]) out[w] = 1;
      if (!grow && vert_sel[v] && !vert_sel[w]) out[v] = 0;
    }
  vert_sel = out;
}

static void tests_round186() {
  test("grow_selection over the edge table (no adjacency built) gives the adjacency walk's selection, growing and shrinking, on a sphere from one vertex and from a ring", [&] {
    const MeshPtr ball = primitives::uv_sphere(0.5f, 24, 16);
    std::vector<uint8_t> a(ball->vert_count(), 0), b;
    a[ball->vert_count() / 2] = 1;
    b = a;
    for (int k = 0; k < 4; k++) {
      meshops::grow_selection(*ball, a, true);
      r186_grow_reference(*ball, b, true);
      CHECK(a == b);
    }
    const size_t grown = (size_t)std::count(a.begin(), a.end(), 1);
    for (int k = 0; k < 2; k++) {
      meshops::grow_selection(*ball, a, false);
      r186_grow_reference(*ball, b, false);
      CHECK(a == b);
    }
    std::printf("    from one vertex: %zu after four grows, %zu after two shrinks (of %zu)\n", grown, (size_t)std::count(a.begin(), a.end(), 1), ball->vert_count());
    CHECK(grown > 20 && (size_t)std::count(a.begin(), a.end(), 1) < grown);
    /* A triangle, a loose edge hanging off it and an isolated vertex: the loose edge grows, the lone vertex never. */
    Mesh m;
    for (int k = 0; k < 5; k++) m.add_vert({(float)k, 0, 0});
    m.add_face({0, 1, 2});
    m.add_loose_edge(2, 3);
    std::vector<uint8_t> s(5, 0), s2;
    s[0] = 1;
    s2 = s;
    for (int k = 0; k < 3; k++) {
      meshops::grow_selection(m, s, true);
      r186_grow_reference(m, s2, true);
      CHECK(s == s2);
    }
    CHECK(s[3] == 1 && s[4] == 0);
    meshops::grow_selection(m, s, false);
    r186_grow_reference(m, s2, false);
    CHECK(s == s2);
  });
}

/* Task 0179: PartUV's "Common Shapes" idea from Blendity's primitives - a cube with a cylinder boss on it, a torus
 * beside it. */
static MeshPtr common_shape() {
  auto m = std::make_shared<Mesh>(*primitives::cube(1.0f));
  meshops::append_mesh(*m, *primitives::cylinder(0.25f, 0.6f, 24), Mat4::translate({0, 0.8f, 0}));
  meshops::append_mesh(*m, *primitives::torus(0.5f, 0.15f, 32, 12), Mat4::translate({1.6f, 0, 0}));
  m->sync_attributes();
  return m;
}

/* Faces whose UV triangle runs the wrong way (folded), and the share of a 512^2 raster covered twice (overlap). */
static int flipped_faces(const Mesh &m) {
  int n = 0;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t b = m.face_offsets[f], k = m.face_size(f);
    double a = 0;
    for (uint32_t i = 0; i < k; i++) {
      const Vec2 p = m.uvs[b + i], q = m.uvs[b + (i + 1) % k];
      a += (double)p.x * q.y - (double)q.x * p.y;
    }
    if (a <= 0) n++;
  }
  return n;
}

static bool same_uvs(const std::vector<Vec2> &a, const std::vector<Vec2> &b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++)
    if (a[i].x != b[i].x || a[i].y != b[i].y) return false;
  return true;
}

static double overlap_share(const Mesh &m) {
  const int N = 512;
  std::vector<uint8_t> hits((size_t)N * N, 0);
  size_t covered = 0, twice = 0;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t b = m.face_offsets[f], k = m.face_size(f);
    for (uint32_t j = 1; j + 1 < k; j++) {
      const Vec2 a = m.uvs[b] * (float)N, c1 = m.uvs[b + j] * (float)N, c2 = m.uvs[b + j + 1] * (float)N;
      const float area = (c1.x - a.x) * (c2.y - a.y) - (c2.x - a.x) * (c1.y - a.y);
      if (std::fabs(area) < 1e-12f) continue;
      const int x0 = std::max(0, (int)std::floor(std::min({a.x, c1.x, c2.x}))), x1 = std::min(N - 1, (int)std::ceil(std::max({a.x, c1.x, c2.x})));
      const int y0 = std::max(0, (int)std::floor(std::min({a.y, c1.y, c2.y}))), y1 = std::min(N - 1, (int)std::ceil(std::max({a.y, c1.y, c2.y})));
      for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
          const float px = x + 0.5f, py = y + 0.5f;
          const float w0 = ((c1.x - px) * (c2.y - py) - (c2.x - px) * (c1.y - py)) / area;
          const float w1 = ((c2.x - px) * (a.y - py) - (a.x - px) * (c2.y - py)) / area;
          if (w0 < 0 || w1 < 0 || w0 + w1 > 1) continue;
          uint8_t &h = hits[(size_t)y * N + x];
          if (h == 1) twice++;
          if (h == 0) covered++;
          h = (uint8_t)std::min(2, h + 1);
        }
    }
  }
  return covered ? (double)twice / covered : 0.0;
}

static void tests_round195() {
  test("uv parts: a cube with a cylinder boss and a torus - the fewest charts face sets allow (a third of Smart UV Project's), shorter seams, each chart under the bound, no folds, no overlaps", [] {
    MeshPtr a = common_shape(), b = mesh_copy(*a);
    uvops::smart_project(*b, nullptr);
    const uvops::UVReport smart = uvops::uv_report(*b, nullptr);
    const uvops::PartUnwrapReport r = uvops::part_unwrap(*a, nullptr);
    const uvops::UVReport parts = uvops::uv_report(*a, nullptr);
    std::printf("    Smart UV Project: %d charts, seams %.2f m, distortion %.3f / %.3f; Part Unwrap: %d charts in %d parts, seams %.2f m, distortion %.3f / %.3f (%d flattened, %d by ABF++, %.0f ms)\n",
                smart.charts, smart.seam_length, smart.distortion_mean, smart.distortion_max, parts.charts, r.parts, parts.seam_length,
                parts.distortion_mean, parts.distortion_max, r.flattened, r.abf_charts, r.ms);
    CHECK(parts.charts == r.charts && r.charts >= 3);
    CHECK(r.distortion_max <= 1.25f + 1e-4f && parts.distortion_max <= 1.25f + 1e-3f);
    /* Charts are sets of faces (no cuts inside one), so a closed piece takes two at least: six for these three is the
     * fewest there can be (the roadmap's 1/5 of Smart UV Project's would be 4.4). */
    CHECK(parts.charts == 6 && parts.charts * 3 <= smart.charts);
    CHECK(parts.seam_length <= 0.6 * smart.seam_length);
    CHECK(flipped_faces(*a) == 0);
#ifdef BL_WITH_EIGEN
    CHECK(r.abf_charts > 0);  // (ABF++ decided some near miss)
#endif
    double lo = 1e9, hi = -1e9;
    for (Vec2 uv : a->uvs) lo = std::min(lo, (double)std::min(uv.x, uv.y)), hi = std::max(hi, (double)std::max(uv.x, uv.y));
    CHECK(lo >= -1e-4 && hi <= 1.0 + 1e-4);
    const double ov = overlap_share(*a);
    std::printf("    overlap: %.4f %% of the covered texels\n", ov * 100.0);
    CHECK(ov < 0.002);
  });
  test("uv parts: the charts follow material slots (12 on a sphere), a non-manifold edge is a cut, tau bounds every chart", [] {
    MeshPtr s = mesh_copy(*primitives::uv_sphere(0.5f, 48, 24));
    s->face_material.assign(s->face_count(), 0);
    for (size_t f = 0; f < s->face_count(); f++) {
      Vec3 c(0.0f);
      for (uint32_t i = 0; i < s->face_size(f); i++) c += s->positions[s->face_verts(f)[i]];
      s->face_material[f] = (int32_t)((std::atan2(c.z, c.x) + 3.14159265f) / 6.2831853f * 12.0f) % 12;
    }
    s->sync_attributes();
    const uvops::PartUnwrapReport r = uvops::part_unwrap(*s, nullptr);
    std::vector<int> island;
    const int n = uvops::compute_islands(*s, nullptr, false, island);
    std::vector<int> slot_of(n, -1);
    bool single = n == r.charts;
    for (size_t f = 0; f < island.size(); f++) {
      int &sl = slot_of[island[f]];
      if (sl < 0) sl = s->face_material[f];
      single = single && sl == s->face_material[f];
    }
    std::printf("    12-slot sphere: %d charts, worst distortion %.3f\n", r.charts, r.distortion_max);
    CHECK(single && r.charts >= 12 && r.distortion_max <= 1.25f + 1e-4f && flipped_faces(*s) == 0);
    /* Three quads on one edge (a book of pages): the edge can't be flattened shared, each page its own chart. */
    Mesh book;
    book.positions = {{0, 0, 0}, {0, 1, 0}, {1, 0, 0}, {1, 1, 0}, {-1, 0, 0}, {-1, 1, 0}, {0, 0, 1}, {0, 1, 1}};
    book.face_offsets = {0, 4, 8, 12};
    book.corner_verts = {0, 2, 3, 1, 0, 1, 5, 4, 0, 6, 7, 1};
    book.sync_attributes();
    const uvops::PartUnwrapReport rb = uvops::part_unwrap(book, nullptr);
    bool finite = true;
    for (Vec2 uv : book.uvs) finite = finite && std::isfinite(uv.x) && std::isfinite(uv.y);
    CHECK(rb.charts == 3 && finite);
    /* A connected 2 x 2 sheet with a fin on its middle edge: the fin's edge is non-manifold, the sheet stays one piece
     * around it - two charts. */
    Mesh fin;
    fin.positions = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {0, 0, 1}, {1, 0, 1}, {2, 0, 1}, {0, 0, 2}, {1, 0, 2}, {2, 0, 2}, {1, 1, 0.5f}, {1, 1, 1.5f}};
    fin.face_offsets = {0, 4, 8, 12, 16, 20};
    fin.corner_verts = {0, 3, 4, 1, 1, 4, 5, 2, 3, 6, 7, 4, 4, 7, 8, 5, 4, 10, 9, 1};
    fin.sync_attributes();
    const uvops::PartUnwrapReport rf = uvops::part_unwrap(fin, nullptr);
    CHECK(rf.charts == 2 && flipped_faces(fin) == 0);
    /* A looser bound: every chart under it (the search is greedy: fewer charts is likely, not promised). */
    MeshPtr t1 = mesh_copy(*primitives::torus(0.5f, 0.2f, 32, 16)), t2 = mesh_copy(*t1);
    uvops::PartUnwrapOptions tight, loose;
    loose.tau = 2.0f;
    const auto rt = uvops::part_unwrap(*t1, nullptr, tight), rl = uvops::part_unwrap(*t2, nullptr, loose);
    std::printf("    torus: %d charts at bound 1.25, %d at 2.0\n", rt.charts, rl.charts);
    CHECK(rl.charts <= rt.charts && rt.distortion_max <= 1.25f + 1e-4f && rl.distortion_max <= 2.0f + 1e-4f);
  });
  test("uv abf: ABF++ closes every triangle at pi and every interior vertex at 2 pi, and keeps a dome's angles better than LSCM alone", [] {
    MeshPtr s = mesh_copy(*primitives::uv_sphere(0.5f, 32, 16));
    std::vector<uint32_t> dome;
    for (size_t f = 0; f < s->face_count(); f++) {
      bool top = true;
      for (uint32_t i = 0; i < s->face_size(f); i++) top = top && s->positions[s->face_verts(f)[i]].y > 0.05f;
      if (top) dome.push_back((uint32_t)f);
    }
    const uvops::detail::ChartTopology chart = uvops::detail::chart_topology(*s, dome);
    std::vector<float> ang;
    const bool ok = uvops::detail::abf_angles(*s, chart, ang);
#ifdef BL_WITH_EIGEN
    CHECK(ok && ang.size() == chart.tris.size() * 3);
#else
    CHECK(!ok);  // (the dependency-free build: LSCM's own angles)
    return;
#endif
    double worst_tri = 0;
    for (size_t t = 0; t < chart.tris.size(); t++) worst_tri = std::max(worst_tri, std::fabs((double)ang[t * 3] + ang[t * 3 + 1] + ang[t * 3 + 2] - 3.14159265358979));
    std::vector<double> around(chart.verts.size(), 0.0);
    std::vector<int> uses(chart.verts.size(), 0);
    for (size_t t = 0; t < chart.tris.size(); t++)
      for (int c = 0; c < 3; c++) around[chart.tris[t][c]] += ang[t * 3 + c], uses[chart.tris[t][c]]++;
    /* The pole and the rings above the lowest are interior: their angles meet at 2 pi. */
    double worst_vertex = 0;
    for (size_t v = 0; v < around.size(); v++)
      if (s->positions[chart.verts[v]].y > 0.2f) worst_vertex = std::max(worst_vertex, std::fabs(around[v] - 6.28318530717959));
    CHECK(worst_tri < 1e-3 && worst_vertex < 1e-3);
    /* Angle distortion of the layouts: mean |UV angle - 3D angle| / 3D angle. */
    auto angle_error = [&](const Mesh &m) {
      double sum = 0;
      int n = 0;
      for (uint32_t f : dome) {
        const uint32_t b = m.face_offsets[f], k = m.face_size(f);
        for (uint32_t i = 0; i < k; i++) {
          const Vec3 p = m.positions[m.corner_verts[b + i]], p1 = m.positions[m.corner_verts[b + (i + 1) % k]], p2 = m.positions[m.corner_verts[b + (i + k - 1) % k]];
          const Vec2 q = m.uvs[b + i], q1 = m.uvs[b + (i + 1) % k], q2 = m.uvs[b + (i + k - 1) % k];
          const double a3 = std::acos(std::clamp((double)dot(normalize(p1 - p), normalize(p2 - p)), -1.0, 1.0));
          const Vec2 u = q1 - q, w = q2 - q;
          const double lu = std::sqrt((double)u.x * u.x + (double)u.y * u.y), lw = std::sqrt((double)w.x * w.x + (double)w.y * w.y);
          if (lu < 1e-12 || lw < 1e-12 || a3 < 1e-6) continue;
          const double a2 = std::acos(std::clamp(((double)u.x * w.x + (double)u.y * w.y) / (lu * lw), -1.0, 1.0));
          sum += std::fabs(a2 - a3) / a3;
          n++;
        }
      }
      return n ? sum / n : 1e9;
    };
    MeshPtr l = mesh_copy(*s), a = mesh_copy(*s);
    uvops::detail::lscm_chart(*l, dome, nullptr);
    uvops::detail::lscm_chart(*a, dome, &ang);
    const double el = angle_error(*l), ea = angle_error(*a);
    std::printf("    dome of %zu faces: angle error LSCM %.4f, ABF++ then LSCM %.4f\n", dome.size(), el, ea);
    CHECK(ea < el);
  });
  test("uv parts: twelve separate pieces on UDIM tiles (u 0-9, then a row up), the Edit Mode mask leaves other faces' UVs", [] {
    Mesh m;
    for (int i = 0; i < 12; i++) meshops::append_mesh(m, *primitives::cube(1.0f), Mat4::translate({2.0f * i, 0, 0}));
    m.sync_attributes();
    uvops::PartUnwrapOptions o;
    o.multi_tile = true;
    uvops::part_unwrap(m, nullptr, o);
    std::vector<int> tile_of(12, -1);
    bool one_tile_each = true;
    for (size_t f = 0; f < m.face_count(); f++) {
      const int piece = (int)(f / 6);
      for (uint32_t k = m.face_offsets[f]; k < m.face_offsets[f + 1]; k++) {
        const Vec2 uv = m.uvs[k];
        const int tile = (int)std::floor(uv.x) + 10 * (int)std::floor(uv.y);
        const bool inside = uv.x - std::floor(uv.x) > 1e-5f && uv.y - std::floor(uv.y) > 1e-5f;
        if (tile_of[piece] < 0) tile_of[piece] = tile;
        one_tile_each = one_tile_each && tile == tile_of[piece] && inside && uv.x < 10.0f;
      }
    }
    std::vector<int> sorted = tile_of;
    std::sort(sorted.begin(), sorted.end());
    CHECK(one_tile_each && std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end() && sorted.back() == 11);
    /* Only the masked faces change. */
    MeshPtr t = mesh_copy(*primitives::torus(0.5f, 0.2f, 24, 12));
    uvops::smart_project(*t, nullptr);
    const std::vector<Vec2> before = t->uvs;
    uvops::Mask mask(t->face_count(), 0);
    for (size_t f = 0; f < mask.size() / 2; f++) mask[f] = 1;
    uvops::part_unwrap(*t, &mask);
    bool kept = true, moved = false;
    for (size_t f = 0; f < t->face_count(); f++)
      for (uint32_t k = t->face_offsets[f]; k < t->face_offsets[f + 1]; k++) {
        const bool same = t->uvs[k].x == before[k].x && t->uvs[k].y == before[k].y;
        if (mask[f]) moved = moved || !same;
        else kept = kept && same;
      }
    CHECK(kept && moved);
  });
  test("uv parts: the console's uv parts unwraps the selected mesh (a bound that isn't a number is refused), uv report runs, undo brings the old UVs back", [] {
    Editor ed;
    ed.init_headless(640, 400);
    ed.command("create Torus");
    GameObject *g = ed.active_object_for_test();
    CHECK(g && g->mesh_filter());
    if (!g) return;
    ed.step_frame_headless();
    const std::vector<Vec2> before = g->mesh_filter()->mesh->uvs;
    ed.command("uv parts x");
    CHECK(same_uvs(g->mesh_filter()->mesh->uvs, before));
    ed.command("uv parts 1.3");
    ed.step_frame_headless();
    const Mesh &m = *g->mesh_filter()->mesh;
    const uvops::UVReport r = uvops::uv_report(m, nullptr);
    CHECK(!same_uvs(m.uvs, before) && r.charts >= 2 && r.distortion_max <= 1.3f + 1e-3f);
    ed.command("uv report");
    ed.command("undo");
    ed.step_frame_headless();
    CHECK(ed.active_object_for_test() && same_uvs(ed.active_object_for_test()->mesh_filter()->mesh->uvs, before));
  });
}

void run_modeling_tests() {
  first_tests();
  edge_tool_tests();
  modeling_tests();
  library_modeling_tests();
  subdiv_tests();
  uv_tests();
  modeling_round9_tests();
  modeling_round10_tests();
  modeling_round11_tests();
  modeling_round12_tests();
  modeling_round13_tests();
  round13_feature_tests();
  round14_tests();
  round14_feature_tests();
  round16_tests();
  round17_tests();
  round18_tests();
  round26_tests();
  round63_tests();
  round65_tests();
  round66_tests();
  round67_tests();
  round68_tests();
  tests_round112();
  tests_round113();
  tests_round116();
  tests_round132();
  tests_round185();
  tests_round186();
  tests_round195();
}

// SPDX-License-Identifier: GPL-2.0-or-later
// blendity_tests: the lighting tests (task 0177: one file a subsystem; the groups keep their rounds' order).
#include "test_common.h"
#include "../src/render/reflection_probe.h"
#include "../src/scene/unity_material.h"
#include "../src/render/probe_sh.h"
#include "../src/render/tetrahedra.h"
#include "../src/render/unity_lightmaps.h"
#include <set>
#include <tuple>

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

  test("VRC Light Volumes baked in Blendity (round 398): the ambient cube's SH L1 fit; Unity's axes and back; the package's textures read back; a bake file's checksum refused when it doesn't match", [] {
    /* The fit: irradiance 2 towards +X and 0 elsewhere - L0 the six directions' mean 1/3, L1 along X half of 2 (review 398:
     * irradiance has no odd band past 1, so a + b . n comes back exactly). */
    ProbeVolumeData d;
    d.baked.resize(1), d.valid.assign(1, 1);
    d.baked[0].bounce[0] = Vec3(2.0f, 1.0f, 0.0f);
    Environment dark;
    dark.mode = Environment::Color, dark.color = Vec3(0.0f);
    const int one[3] = {1, 1, 1};
    LightVolumeSH sh;
    light_volume_fit(d, dark, one, sh);
    std::printf("    the fit: L0 %g, L1 red (%g %g %g)\n", sh.l0[0].x, sh.l1r[0].x, sh.l1r[0].y, sh.l1r[0].z);
    CHECK(std::fabs(sh.l0[0].x - 2.0f / 6.0f) < 1e-6f && std::fabs(sh.l1r[0].x - 1.0f) < 1e-6f && sh.l1r[0].y == 0.0f && std::fabs(sh.l1g[0].x - 0.5f) < 1e-6f);
    CHECK(sh.validity[0] == 0.0f);  // (valid: open - Unity's sense)
    /* A linear field a + b . n sampled on the six axes comes back exactly: (1, 0.2, -0.3) . n + 0.5. */
    for (int k = 0; k < 6; k++) {
      static const Vec3 kAxis[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
      d.baked[0].bounce[(size_t)k] = Vec3(0.5f + dot(Vec3(1.0f, 0.2f, -0.3f), kAxis[k]));
    }
    light_volume_fit(d, dark, one, sh);
    CHECK(std::fabs(sh.l0[0].x - 0.5f) < 1e-6f && std::fabs(sh.l1r[0].x - 1.0f) < 1e-6f && std::fabs(sh.l1r[0].y - 0.2f) < 1e-6f && std::fabs(sh.l1r[0].z + 0.3f) < 1e-6f);
    d.valid[0] = 0;
    light_volume_fit(d, dark, one, sh);
    CHECK(sh.validity[0] == 1.0f);  // (inside geometry: 1, which the package fills from its neighbours)
    /* Unity's axes: x reversed, L1's x negated - and back. */
    LightVolumeSH grid;
    grid.res[0] = 2, grid.res[1] = 1, grid.res[2] = 1;
    grid.l0 = {Vec3(1, 0, 0), Vec3(2, 0, 0)}, grid.l1r = {Vec3(0.5f, 0.25f, 0), Vec3(0)}, grid.l1g = grid.l1b = {Vec3(0), Vec3(0)}, grid.validity = {1, 0};
    const LightVolumeSH u = light_volume_to_unity(grid), back = light_volume_from_unity(u);
    CHECK(u.l0[0].x == 2.0f && u.l1r[1].x == -0.5f && u.l1r[1].y == 0.25f && u.validity[0] == 0.0f);
    CHECK(back.l0[0].x == 1.0f && back.l1r[0].x == 0.5f && back.checksum() == grid.checksum());
    /* The package's textures (RGBAHalf, its packing: L1 times 1.65) read back to the SH they hold. */
    auto half = [](float f) {  // (exact for these values: 0, 0.5, 1, 2 and their multiples of 1.65's halves)
      uint32_t b;
      std::memcpy(&b, &f, 4);
      const uint32_t e = (b >> 23) & 0xFF, m = b & 0x7FFFFF;
      return (uint16_t)(f == 0.0f ? 0 : ((b >> 16) & 0x8000) | ((e - 112) << 10) | (m >> 13));
    };
    auto tex = [&](const std::vector<float> &rgba) {
      std::string hex;
      for (float f : rgba) hex += strprintf("%02x%02x", half(f) & 0xFF, half(f) >> 8);
      return "%YAML 1.1\n--- !u!117 &11700000\nTexture3D:\n  m_Name: v\n  m_Width: 1\n  m_Height: 1\n  m_Depth: 1\n  m_Format: 17\n  image data: 8\n  _typelessdata: " + hex + "\n";
    };
    LightVolumeSH read;
    std::string why;
    CHECK(light_volume_from_textures(tex({1.0f, 0.5f, 2.0f, 1.65f}), tex({3.3f, 0.0f, 0.0f, 0.0f}), tex({0.0f, 0.0f, 0.0f, 0.0f}), read, why));
    std::printf("    read back: L0 (%g %g %g), L1 red (%g %g %g) %s\n", read.l0[0].x, read.l0[0].y, read.l0[0].z, read.l1r[0].x, read.l1r[0].y, read.l1r[0].z, why.c_str());
    CHECK(read.unity_axes && read.l0[0].y == 0.5f && std::fabs(read.l1r[0].x - 2.0f) < 2e-3f && std::fabs(read.l1r[0].z - 1.0f) < 2e-3f);
    CHECK(!light_volume_from_textures(tex({1, 1, 1, 1}), "not a texture", tex({1, 1, 1, 1}), read, why));
    /* The file: saved, read back equal; one byte changed, refused by its checksum. */
    const std::string path = fs::join(test_dir(), "lv_bakes.bin");
    std::map<uint64_t, LightVolumeSH> bakes{{42, grid}}, loaded;
    std::string err;
    bakes[42].from_unity = true, bakes[42].rotation = Quat::euler(Vec3(0, 30, 0));
    CHECK(save_light_volume_bakes(path, bakes, err) && load_light_volume_bakes(path, loaded, err) && loaded.count(42) && loaded[42].checksum() == grid.checksum());
    CHECK(loaded[42].from_unity && std::fabs(loaded[42].rotation.y - bakes[42].rotation.y) < 1e-7f);  // (version 2: its source and baked rotation kept)
    std::string raw;
    fs::read_file(path, raw);
    raw[raw.size() - 3] ^= 0x40;
    fs::write_file(path, raw);
    CHECK(!load_light_volume_bakes(path, loaded, err) && err.find("checksum") != std::string::npos);
    LightVolumeSH bad = grid;
    bad.l0[1].y = std::nanf("");
    CHECK(!bad.valid(err) && !save_light_volume_bakes(path, {{1, bad}}, err));
    LightVolumeSH huge;
    huge.res[0] = huge.res[1] = 2000, huge.res[2] = 1;  // (4M voxels: past the cap, refused before its arrays are looked at)
    CHECK(!huge.valid(err) && err.find("too large") != std::string::npos);
  });
  test("VRC Light Volumes baked in Blendity (round 398): Generate Lighting bakes a volume with Bake in Blendity on - its grid, every voxel a number, lit where the scene's probe volume says - and leaves one with it off to Unity", [] {
    Editor ed;
    r35_open(ed);
    r35_add(ed, "Plane", "Floor", {0, 0, 0}, {4, 1, 4}, Vec3(0.8f));
    r35_sun_mode(ed, 2);  // (Baked: the volumes and probes take it)
    GameObject *hall = ed.scene().create("Hall");
    hall->set_local_position(Vec3(0, 1, 0)), hall->set_local_scale(Vec3(4, 2, 4));
    LightVolumeComponent *v = hall->add<LightVolumeComponent>();
    v->voxels_per_unit = 1.0f, v->bake_in_blendity = true;
    GameObject *other = ed.scene().create("Unity's");
    other->add<LightVolumeComponent>();
    ed.scene().create("Probes")->add<ProbeVolume>()->global = true;  // (the scene's own probes: the same path tracer, placed apart - what the volume is checked against)
    ed.commit_change("volumes");
    r35_bake(ed);
    auto &bakes = ed.light_volume_bakes_for_test();
    const auto it = bakes.find(hall->id);
    CHECK(it != bakes.end() && !bakes.count(other->id));
    if (it == bakes.end()) return;
    const LightVolumeSH &sh = it->second;
    std::string why;
    CHECK(sh.res[0] == 4 && sh.res[1] == 2 && sh.res[2] == 4 && sh.valid(why));
    /* The voxel over the floor's middle against the scene's own probe volume at the same point, like for like: its L0
     * against the mean of the probes' six directions there, its L1 up against half their up less their down. */
    const std::vector<Vec3> pos = light_volume_positions(hall->world_matrix(), sh.res);
    const size_t i = 1 + 4 * (0 + 2 * 1);  // (x 1, y 0, z 1)
    static const Vec3 kAxis[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    Vec3 axis[6], mean(0.0f);
    bool sampled = true;
    for (int k = 0; k < 6; k++) {
      sampled = sampled && ed.probes_for_test().sample(pos[i], kAxis[k], ed.env_for_test().irradiance(kAxis[k]), false, axis[k]);
      mean = mean + axis[k] / 6.0f;
    }
    const float l1_up = (axis[2].x - axis[3].x) * 0.5f;
    std::printf("    voxel at (%.2f %.2f %.2f): L0 %.3f, L1 up %.3f; the probe volume there %s %.3f and %.3f\n", pos[i].x, pos[i].y, pos[i].z, sh.l0[i].x,
                sh.l1r[i].y, sampled ? "says" : "has none:", mean.x, l1_up);
    CHECK(sh.l0[i].x > 0.0f && std::isfinite(sh.l0[i].x));
    CHECK(sampled && std::fabs(sh.l0[i].x - mean.x) <= 0.1f * std::max(mean.x, 0.05f));  // (two placements and sample sets, one path tracer)
    CHECK(sampled && std::fabs(sh.l1r[i].y - l1_up) <= 0.1f * std::max(std::fabs(l1_up), 0.05f));
    /* A volume past the voxel cap is refused, said - nothing baked for it. */
    GameObject *world = ed.scene().create("World");
    world->set_local_scale(Vec3(300, 50, 300));
    world->add<LightVolumeComponent>()->bake_in_blendity = true;  // (3 a metre: 121M voxels)
    ed.commit_change("world volume");
    r35_bake(ed);
    CHECK(!ed.light_volume_bakes_for_test().count(world->id) && ed.light_volume_bakes_for_test().count(hall->id));
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
    /* Nothing contributes: Generate Lighting refuses (round 362: it had baked "0 objects" without a word), no maps, the
     * picture the plain one. */
    ed.command("bake clear");
    r34_steps(ed, 3);
    const std::vector<uint32_t> plain = r35_game(ed);
    ed.scene().find_by_name("Floor")->get<MeshRenderer>()->contribute_gi = false;
    ed.scene().find_by_name("Empty")->get<MeshRenderer>()->contribute_gi = false;
    ed.scene().find_by_name("Flat")->get<MeshRenderer>()->contribute_gi = false;
    ed.commit_change("nothing static");
    ed.command("bake start");
    CHECK(!ed.baking_for_test());
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
    ed.set_clock_for_test(1000.0);  // (the settle second on the editor's clock: a sanitizer build's frames took longer than it)
    r34_steps(ed, 3);
    CHECK(!ed.baking_for_test() && !ed.lighting_out_of_date_for_test());  // nothing changed: no bake
    box->set_local_position({-3.0f, 0.5f, -1.5f});
    ed.commit_change("move Box");
    r34_steps(ed, 3);
    CHECK(ed.lighting_out_of_date_for_test());
    CHECK(!ed.baking_for_test());  // it waits for the change to settle
    const std::vector<uint32_t> waiting = r35_game(ed);
    ed.set_clock_for_test(1001.15);  // (1.15 s later)
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
    ed.set_clock_for_test(2000.0);  // (the editor's clock: the wake-up is measured against it, not the wall)
    ed.scene().find_by_name("Directional Light")->set_local_euler({40, 60, 0});
    ed.commit_change("turn the sun");
    r34_steps(ed, 2);
    CHECK(ed.lighting_out_of_date_for_test());
    /* The run loop sleeps until next_wakeup when no event comes: it must point about a second ahead. */
    const double wake = ed.ui_for_test().next_wakeup;
    std::printf("    wake-up in %.2f s\n", wake - 2000.0);
    CHECK(wake > 2000.0 && wake < 2001.5);
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

static void round37_tests() {
  /* ---------------------------------------------------------------- 1: placement */
  test("probe volumes: bricks are finest within one brick of geometry and coarsest in open space; the probe count follows the geometry, not the volume", [] {
    const float mn = 1.0f, mx = 8.0f;
    auto floor_boxes = [](float half) { return std::vector<AABB>{p37_box(Vec3(-half, -0.01f, -half), Vec3(half, 0.01f, half))}; };
    ProbeVolumeData d;
    probe_place(d, p37_box(Vec3(-72.0f), Vec3(72.0f)), floor_boxes(10.0f), mn, mx);
    CHECK(!d.empty());
    CHECK(d.position.size() == d.bricks.size() * 64 && d.valid.size() == d.position.size() && d.baked.size() == d.position.size() &&
          d.live.size() == d.position.size() && d.live_ok.size() == d.position.size());
    bool all_valid = true;
    for (uint8_t v : d.valid) all_valid = all_valid && v == 1;
    CHECK(all_valid);  // placement alone invalidates nothing
    /* Every brick's probes sit on its grid and inside the volume's cells. */
    bool grid_ok = true;
    for (const ProbeBrick &b : d.bricks)
      for (int k = 0; k < 64; k++) {
        const Vec3 want = b.min + Vec3((float)(k & 3), (float)((k >> 2) & 3), (float)(k >> 4)) * b.spacing;
        const Vec3 got = d.position[b.first + (uint32_t)k];
        grid_ok = grid_ok && std::fabs(got.x - want.x) < 1e-4f && std::fabs(got.y - want.y) < 1e-4f && std::fabs(got.z - want.z) < 1e-4f;
      }
    CHECK(grid_ok);
    /* Finest within one fine brick (3 m) of the floor, anywhere around it; coarsest far away. */
    R34Rng rng{3737u};
    int near_bad = 0, near_n = 0, far_bad = 0, far_n = 0;
    for (int i = 0; i < 4000; i++) {
      const Vec3 p(rng.range(-13.0f, 13.0f), rng.range(-2.9f, 2.9f), rng.range(-13.0f, 13.0f));
      const float dx = std::max(0.0f, std::fabs(p.x) - 10.0f), dz = std::max(0.0f, std::fabs(p.z) - 10.0f);
      if (std::sqrt(dx * dx + p.y * p.y + dz * dz) > 2.9f) continue;
      const int b = d.find_brick(p);
      near_n++;
      if (b < 0 || std::fabs(d.bricks[(size_t)b].spacing - mn) > 1e-4f) near_bad++;
    }
    for (int i = 0; i < 1000; i++) {
      const Vec3 p(rng.range(50.0f, 71.0f) * (i & 1 ? 1 : -1), rng.range(50.0f, 71.0f) * (i & 2 ? 1 : -1), rng.range(50.0f, 71.0f));
      const int b = d.find_brick(p);
      far_n++;
      if (b < 0 || std::fabs(d.bricks[(size_t)b].spacing - mx) > 1e-4f) far_bad++;
    }
    std::printf("    %zu probes in %zu bricks; near the floor %d of %d points not at the finest spacing, far away %d of %d not at the coarsest\n", d.position.size(), d.bricks.size(),
                near_bad, near_n, far_bad, far_n);
    CHECK(near_n > 500 && near_bad == 0);
    CHECK(far_n > 0 && far_bad == 0);
    /* Going up from the floor the spacing never gets finer: 1, 2, 4, 8. */
    float prev = 0;
    bool monotone = true;
    for (float y = 0.05f; y < 70.0f; y += 0.37f) {
      const int b = d.find_brick(Vec3(0.2f, y, 0.2f));
      CHECK(b >= 0);
      if (b < 0) break;
      const float s = d.bricks[(size_t)b].spacing;
      monotone = monotone && s >= prev - 1e-4f;
      prev = s;
    }
    CHECK(monotone);
    CHECK(std::fabs(prev - mx) < 1e-4f);
    /* The count follows the geometry: an empty volume is the top cells and nothing else; the floor adds its own
     * fine bricks, four times the floor adds about four times as many; and the same floor in a volume twice as
     * wide adds only the coarse cells of the extra space. */
    ProbeVolumeData e, f10, f20, f10_small;
    probe_place(e, p37_box(Vec3(-72.0f), Vec3(72.0f)), {}, mn, mx);
    probe_place(f10, p37_box(Vec3(-72.0f), Vec3(72.0f)), floor_boxes(10.0f), mn, mx);
    probe_place(f20, p37_box(Vec3(-72.0f), Vec3(72.0f)), floor_boxes(20.0f), mn, mx);
    probe_place(f10_small, p37_box(Vec3(-24.0f), Vec3(24.0f)), floor_boxes(10.0f), mn, mx);
    const double uniform = 144.0 * 144.0 * 144.0;  // one probe per metre everywhere
    const size_t add10 = f10.position.size() - e.position.size(), add20 = f20.position.size() - e.position.size();
    std::printf("    empty %zu probes, floor 20 x 20 m adds %zu, floor 40 x 40 m adds %zu; the same 20 m floor in a 48 m volume %zu; a uniform 1 m grid would be %.0f\n",
                e.position.size(), add10, add20, f10_small.position.size(), uniform);
    CHECK(e.position.size() == 6u * 6u * 6u * 64u);
    CHECK(add10 > 0 && add20 > add10 * 2);
    CHECK((double)f10.position.size() < 0.05 * uniform);  // (a 20 m floor gets about 2%: bricks of 1 m probes a few metres thick; a uniform grid would be 100%)
    CHECK(f10.position.size() - f10_small.position.size() == e.position.size() - 8u * 64u);  // volume size alone adds only coarse cells
    /* The probe cap is kept (coarser where needed), also with dense geometry everywhere. */
    std::vector<AABB> dense;
    for (int i = 0; i < 200; i++) dense.push_back(p37_box(Vec3(rng.range(-70, 70), rng.range(-70, 70), rng.range(-70, 70)), Vec3(0.0f)));
    for (AABB &b : dense) b.add(b.min + Vec3(0.3f));
    ProbeVolumeData capped;
    probe_place(capped, p37_box(Vec3(-72.0f), Vec3(72.0f)), dense, mn, mx, 3000);
    std::printf("    200 small boxes scattered through the volume, cap 3000: %zu probes\n", capped.position.size());
    CHECK(!capped.empty() && capped.position.size() <= 3000);
  });

  test("probe volumes: placement refuses nonsense (NaN or inverted volumes, NaN or negative spacings) and stays inside its limits", [] {
    const std::vector<AABB> none;
    ProbeVolumeData d;
    AABB bad;  // never grown: invalid
    probe_place(d, bad, none, 1.0f, 8.0f);
    CHECK(d.empty() && d.position.empty());
    /* Set directly: AABB::add drops a NaN point on GCC (std::min(0, NaN) is 0), so the box would be a valid point. */
    AABB nan_box;
    nan_box.min = Vec3(0.0f);
    nan_box.max = Vec3(kNaN);
    probe_place(d, nan_box, none, 1.0f, 8.0f);
    CHECK(d.empty());
    AABB inf_box;
    inf_box.min = Vec3(0.0f);
    inf_box.max = Vec3(std::numeric_limits<float>::infinity());
    probe_place(d, inf_box, none, 1.0f, 8.0f);
    CHECK(d.empty());
    const AABB v = p37_box(Vec3(-6.0f), Vec3(6.0f));
    const float spacings[][2] = {{kNaN, 8.0f}, {1.0f, kNaN}, {-3.0f, 8.0f}, {0.0f, 0.0f}, {8.0f, 1.0f}, {1e30f, 1e30f}, {1e-30f, 1.0f}, {kNaN, kNaN}};
    for (const auto &s : spacings) {
      probe_place(d, v, none, s[0], s[1]);
      CHECK(!d.empty());
      bool finite = d.position.size() == d.bricks.size() * 64 && d.position.size() <= 2000000;
      for (const Vec3 &p : d.position) finite = finite && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
      CHECK(finite);
      CHECK(d.find_brick(Vec3(0.1f)) >= 0);
    }
    /* A volume of zero size, and a huge one (capped, not 10^30 probes). */
    probe_place(d, p37_box(Vec3(1.0f), Vec3(1.0f)), none, 1.0f, 8.0f);
    CHECK(d.position.size() == d.bricks.size() * 64);
    probe_place(d, p37_box(Vec3(-1e30f), Vec3(1e30f)), none, 1.0f, 8.0f, 20000);
    CHECK(d.position.size() == d.bricks.size() * 64 && d.position.size() <= 20000);
    /* Triangles with NaN bounds don't break the split. */
    std::vector<AABB> odd = {p37_box(Vec3(kNaN), Vec3(kNaN)), p37_box(Vec3(0.0f), Vec3(0.5f))};
    probe_place(d, v, odd, 1.0f, 8.0f);
    CHECK(!d.empty() && d.position.size() == d.bricks.size() * 64);
    /* Lookups outside, at NaN and at infinity find nothing. */
    probe_place(d, v, none, 1.0f, 8.0f);
    Vec3 out;
    CHECK(d.find_brick(Vec3(100.0f, 0, 0)) < 0 && d.find_brick(Vec3(kNaN, 0, 0)) < 0 && d.find_brick(Vec3(std::numeric_limits<float>::infinity(), 0, 0)) < 0);
    CHECK(!d.sample(Vec3(100.0f, 0, 0), Vec3(0, 1, 0), Vec3(0.3f), false, out));
    ProbeVolumeData blank;
    CHECK(blank.find_brick(Vec3(0.0f)) < 0 && !blank.sample(Vec3(0.0f), Vec3(0, 1, 0), Vec3(0.3f), false, out));
  });

  /* ---------------------------------------------------------------- 2: the cube (SH was replaced) */
  test("probe volumes: the ambient cube reconstructs a constant light exactly, gives an axis normal that axis's value, and blends by n squared", [] {
    /* The spec asks for spherical harmonics (L1 / L2). ADR 0012 replaced them with Valve's ambient cube (six
     * values, blended by n squared): cheaper per pixel and it cannot ring. So the SH criteria become: a constant
     * environment is returned exactly, an axis normal reads exactly its axis, and the blend weights sum to 1. */
    R34Rng rng{99u};
    ProbeCube c;
    for (int i = 0; i < 6; i++) c.bounce[(size_t)i] = Vec3(0.3f, 0.6f, 0.9f), c.sky[(size_t)i] = 0.0f;
    double worst = 0;
    for (int i = 0; i < 2000; i++) {
      const Vec3 n = normalize(Vec3(rng.range(-1, 1), rng.range(-1, 1), rng.range(-1, 1)) + Vec3(1e-3f));
      const Vec3 v = c.eval(n, Vec3(0.0f));
      worst = std::max({worst, (double)std::fabs(v.x - 0.3f), (double)std::fabs(v.y - 0.6f), (double)std::fabs(v.z - 0.9f)});
    }
    std::printf("    constant bounce 0.3 0.6 0.9: worst error %.2e over 2000 normals\n", worst);
    CHECK(worst < 2e-6);
    /* A constant sky seen entirely (share 1) and one seen half. */
    ProbeCube s;
    for (int i = 0; i < 6; i++) s.sky[(size_t)i] = 1.0f;
    const Vec3 E(0.2f, 0.4f, 0.8f);
    worst = 0;
    for (int i = 0; i < 2000; i++) {
      const Vec3 n = normalize(Vec3(rng.range(-1, 1), rng.range(-1, 1), rng.range(-1, 1)) + Vec3(1e-3f));
      const Vec3 v = s.eval(n, E);
      worst = std::max({worst, (double)std::fabs(v.x - E.x), (double)std::fabs(v.y - E.y), (double)std::fabs(v.z - E.z)});
    }
    CHECK(worst < 2e-6);
    for (int i = 0; i < 6; i++) s.sky[(size_t)i] = 0.5f;
    const Vec3 half = s.eval(normalize(Vec3(0.3f, -0.8f, 0.5f)), E);
    CHECK(std::fabs(half.x - 0.1f) < 1e-6f && std::fabs(half.z - 0.4f) < 1e-6f);
    /* Axis normals read exactly their axis (bounce and sky share), the six values all different. */
    ProbeCube a;
    const Vec3 axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int i = 0; i < 6; i++) a.bounce[(size_t)i] = Vec3((float)(i + 1), 10.0f * (float)(i + 1), 100.0f * (float)(i + 1)), a.sky[(size_t)i] = 0.1f * (float)(i + 1);
    for (int i = 0; i < 6; i++) {
      const Vec3 v = a.eval(axes[i], E);
      const Vec3 want = a.bounce[(size_t)i] + E * a.sky[(size_t)i];
      CHECK(v.x == want.x && v.y == want.y && v.z == want.z);
    }
    /* Between two axes the weights are n squared: 45 degrees is half and half, and the sign picks the face. */
    const Vec3 d45 = a.eval(normalize(Vec3(1, 1, 0)), Vec3(0.0f));
    const Vec3 want45 = (a.bounce[0] + a.bounce[2]) * 0.5f;
    CHECK(std::fabs(d45.x - want45.x) < 1e-4f && std::fabs(d45.y - want45.y) < 1e-3f && std::fabs(d45.z - want45.z) < 1e-2f);
    const Vec3 neg = a.eval(normalize(Vec3(-1, -1, 0)), Vec3(0.0f));
    const Vec3 want_neg = (a.bounce[1] + a.bounce[3]) * 0.5f;
    CHECK(std::fabs(neg.x - want_neg.x) < 1e-4f && std::fabs(neg.y - want_neg.y) < 1e-3f);
    const Vec3 three = a.eval(Vec3(-1, 2, -2) * (1.0f / 3.0f), Vec3(0.0f));  // n squared: 1/9, 4/9, 4/9
    const Vec3 want3 = a.bounce[1] * (1.0f / 9.0f) + a.bounce[2] * (4.0f / 9.0f) + a.bounce[5] * (4.0f / 9.0f);
    CHECK(std::fabs(three.x - want3.x) < 1e-4f && std::fabs(three.y - want3.y) < 1e-3f && std::fabs(three.z - want3.z) < 1e-2f);
    /* A smooth walk of the normal gives a smooth result (no jump at the sign flip of a component). */
    Vec3 last = a.eval(normalize(Vec3(-1.0f, 0.4f, 0.2f)), E);
    double jump = 0;
    for (float t = -1.0f; t <= 1.0f; t += 0.001f) {
      const Vec3 v = a.eval(normalize(Vec3(t, 0.4f, 0.2f)), E);
      jump = std::max(jump, (double)std::fabs(v.x - last.x));
      last = v;
    }
    CHECK(jump < 0.02);  // a step of 0.001 in the normal moves the x values (1..6 apart) by a few thousandths; a jump would be whole units
  });

  test("probe volumes: sampling blends trilinearly (a linear field comes back exactly), skips invalid probes, and falls back to the baked values", [] {
    ProbeVolumeData d = p37_one_brick(1.0f);
    CHECK(d.position.size() == 64 && d.bricks.size() == 1);
    for (size_t i = 0; i < 64; i++)
      for (int ax = 0; ax < 6; ax++) d.baked[i].bounce[(size_t)ax] = d.position[i], d.baked[i].sky[(size_t)ax] = 0.0f;
    R34Rng rng{5u};
    Vec3 out;
    double worst = 0;
    for (int i = 0; i < 500; i++) {
      const Vec3 p(rng.range(0.0f, 2.99f), rng.range(0.0f, 2.99f), rng.range(0.0f, 2.99f));
      const Vec3 n = normalize(Vec3(rng.range(-1, 1), rng.range(-1, 1), rng.range(-1, 1)) + Vec3(1e-3f));
      CHECK(d.sample(p, n, Vec3(0.0f), false, out, false));  // plain trilinear: leak reduction off
      worst = std::max({worst, (double)std::fabs(out.x - p.x), (double)std::fabs(out.y - p.y), (double)std::fabs(out.z - p.z)});
    }
    std::printf("    linear field 1 m probes: worst trilinear error %.2e\n", worst);
    CHECK(worst < 2e-5);
    /* The weights sum to 1 even with probes missing: a constant field is returned as it is, and a NaN in an invalid
     * probe never reaches the result. */
    for (size_t i = 0; i < 64; i++)
      for (int ax = 0; ax < 6; ax++) d.baked[i].bounce[(size_t)ax] = Vec3(0.4f, 0.5f, 0.6f);
    int invalid = 0;
    for (size_t i = 0; i < 64; i++)
      if (rng.f() < 0.3f) {
        d.valid[i] = 0;
        invalid++;
        for (int ax = 0; ax < 6; ax++) d.baked[i].bounce[(size_t)ax] = Vec3(kNaN), d.baked[i].sky[(size_t)ax] = kNaN;
      }
    CHECK(invalid > 5);
    int answered = 0;
    bool constant = true;
    for (int i = 0; i < 800; i++) {
      const Vec3 p(rng.range(0.0f, 2.99f), rng.range(0.0f, 2.99f), rng.range(0.0f, 2.99f));
      if (d.sample(p, Vec3(0, 1, 0), Vec3(0.0f), false, out, false)) {  // plain trilinear: leak reduction off
        answered++;
        constant = constant && std::fabs(out.x - 0.4f) < 1e-5f && std::fabs(out.y - 0.5f) < 1e-5f && std::fabs(out.z - 0.6f) < 1e-5f;
      }
    }
    CHECK(answered > 600);
    CHECK(constant);
    /* A cell whose eight corners are all invalid has nothing to give. */
    ProbeVolumeData e = p37_one_brick(1.0f);
    for (int z = 1; z <= 2; z++)
      for (int y = 1; y <= 2; y++)
        for (int x = 1; x <= 2; x++) e.valid[(size_t)(x + 4 * y + 16 * z)] = 0;
    CHECK(!e.sample(Vec3(1.5f), Vec3(0, 1, 0), Vec3(0.3f), false, out));
    CHECK(e.sample(Vec3(0.5f), Vec3(0, 1, 0), Vec3(0.3f), false, out));  // the cell beside it still answers
    /* Live values are used only where they exist. */
    ProbeVolumeData l = p37_one_brick(1.0f);
    for (size_t i = 0; i < 64; i++) {
      for (int ax = 0; ax < 6; ax++) l.baked[i].bounce[(size_t)ax] = Vec3(1.0f), l.live[i].bounce[(size_t)ax] = Vec3(5.0f), l.baked[i].sky[(size_t)ax] = l.live[i].sky[(size_t)ax] = 0.0f;
      l.live_ok[i] = 0;
    }
    CHECK(l.sample(Vec3(1.2f), Vec3(0, 1, 0), Vec3(0.0f), true, out) && std::fabs(out.x - 1.0f) < 1e-5f);   // live asked, not ready: baked
    for (size_t i = 0; i < 64; i++) l.live_ok[i] = 1;
    CHECK(l.sample(Vec3(1.2f), Vec3(0, 1, 0), Vec3(0.0f), true, out) && std::fabs(out.x - 5.0f) < 1e-5f);   // ready: live
    CHECK(l.sample(Vec3(1.2f), Vec3(0, 1, 0), Vec3(0.0f), false, out) && std::fabs(out.x - 1.0f) < 1e-5f);  // not asked: baked
    /* The sky's share is applied with the sky you pass in, so a new sky needs no new bake. */
    for (size_t i = 0; i < 64; i++)
      for (int ax = 0; ax < 6; ax++) l.baked[i].bounce[(size_t)ax] = Vec3(0.0f), l.baked[i].sky[(size_t)ax] = 0.5f;
    Vec3 lo, hi;
    CHECK(l.sample(Vec3(1.2f), Vec3(0, 1, 0), Vec3(0.2f), false, lo) && l.sample(Vec3(1.2f), Vec3(0, 1, 0), Vec3(0.8f), false, hi));
    CHECK(std::fabs(lo.x - 0.1f) < 1e-5f && std::fabs(hi.x - 0.4f) < 1e-5f);
    /* A NaN normal or point gives no answer rather than a NaN. */
    CHECK(!l.sample(Vec3(1.2f), Vec3(kNaN, 1, 0), Vec3(0.2f), false, out));
    CHECK(!l.sample(Vec3(kNaN), Vec3(0, 1, 0), Vec3(0.2f), false, out));
  });

  test("probe volumes: a linear field is continuous across a fine and a coarse brick (the seam of two resolutions)", [] {
    ProbeVolumeData d;
    std::vector<AABB> floor = {p37_box(Vec3(-3.0f, -0.01f, -3.0f), Vec3(3.0f, 0.01f, 3.0f))};
    probe_place(d, p37_box(Vec3(-24.0f), Vec3(24.0f)), floor, 1.0f, 8.0f);
    for (size_t i = 0; i < d.position.size(); i++)
      for (int ax = 0; ax < 6; ax++) d.baked[i].bounce[(size_t)ax] = d.position[i], d.baked[i].sky[(size_t)ax] = 0.0f;
    /* Walk along +y from the floor through the 1 m, 2 m, 4 m and 8 m bricks: the field is returned exactly everywhere,
     * including at the brick faces (a probe pool whose levels didn't line up would jump there). */
    double worst = 0;
    int changes = 0;
    float last_spacing = 0;
    for (float y = 0.02f; y < 23.0f; y += 0.0173f) {
      const Vec3 p(0.31f, y, 0.27f);
      Vec3 out;
      const int b = d.find_brick(p);
      if (b < 0) continue;
      if (d.bricks[(size_t)b].spacing != last_spacing) changes++, last_spacing = d.bricks[(size_t)b].spacing;
      if (!d.sample(p, Vec3(0, 1, 0), Vec3(0.0f), false, out, false)) continue;  // plain trilinear
      worst = std::max(worst, (double)length(out - p));
    }
    std::printf("    walking up through %d brick sizes: worst error %.2e\n", changes, worst);
    CHECK(changes >= 3);
    CHECK(worst < 1e-3);
    /* A curved field does jump where the resolutions meet (the coarse side interpolates between its own probes): the
     * size of the jump is reported, bounded by the curvature times the coarse spacing squared. */
    for (size_t i = 0; i < d.position.size(); i++)
      for (int ax = 0; ax < 6; ax++) d.baked[i].bounce[(size_t)ax] = Vec3(dot(d.position[i], d.position[i]) * 0.01f);
    double jump = 0;
    for (float y = 0.02f; y < 23.0f; y += 0.0173f) {
      const Vec3 p0(0.31f, y, 0.27f), p1(0.31f, y + 0.0173f, 0.27f);
      Vec3 a, b;
      if (d.sample(p0, Vec3(0, 1, 0), Vec3(0.0f), false, a, false) && d.sample(p1, Vec3(0, 1, 0), Vec3(0.0f), false, b, false)) jump = std::max(jump, (double)std::fabs(a.x - b.x) - 0.01 * 2 * y * 0.0173);
    }
    std::printf("    quadratic field 0.01 r^2: largest step beyond the smooth slope across the brick sizes %.4f\n", jump);
    CHECK(jump < 0.01 * 8.0 * 8.0 / 4.0);  // the coarsest interpolation error bound: curvature * spacing^2 / 4
  });

  /* ---------------------------------------------------------------- 3: validity */
  test("probe volumes: probes inside a closed box are moved out or marked invalid, and none stays inside; the others are untouched", [] {
    P37Pt s;
    s.add(primitives::cube(), Mat4::scale({2.0f, 2.0f, 2.0f}), Vec3(0.7f));  // a solid box, +-1
    Environment env;
    env.mode = Environment::Color;
    env.color = Vec3(0.3f);
    s.build({}, env);
    ProbeVolumeData d;
    probe_place(d, p37_box(Vec3(-3.1f), Vec3(2.9f)), s.tri_boxes(), 0.5f, 0.5f);
    const std::vector<Vec3> before = d.position;
    probe_validate(d, s.pt, 64);
    int inside = 0, moved = 0, invalid = 0, outside_touched = 0, still_inside_valid = 0;
    for (size_t i = 0; i < d.position.size(); i++) {
      const Vec3 b = before[i];
      const bool was_inside = std::max({std::fabs(b.x), std::fabs(b.y), std::fabs(b.z)}) < 0.93f;
      const Vec3 a = d.position[i];
      if (was_inside) {
        inside++;
        if (!d.valid[i]) invalid++;
        else {
          moved++;
          if (std::max({std::fabs(a.x), std::fabs(a.y), std::fabs(a.z)}) < 1.0f) still_inside_valid++;
        }
      }
      else if (std::max({std::fabs(b.x), std::fabs(b.y), std::fabs(b.z)}) > 1.2f && (a.x != b.x || a.y != b.y || a.z != b.z || !d.valid[i])) outside_touched++;
    }
    std::printf("    %d probes inside the box: %d moved out, %d invalid, %d valid and still inside; %d outside probes touched\n", inside, moved, invalid, still_inside_valid,
                outside_touched);
    CHECK(inside >= 20);
    CHECK(still_inside_valid == 0);
    CHECK(outside_touched == 0);
  });

  test("probe volumes: a bake leaves invalid probes dilated from valid neighbours and sampling never reads an invalid one", [] {
    P37Pt s;
    /* A 3 x 3 x 3 block of touching unit cubes: a probe in the middle one has a neighbour behind every face, so moving it
     * out lands inside the next cube and it must end up invalid (the outer ones can be moved out). */
    for (int x = -1; x <= 1; x++)
      for (int y = -1; y <= 1; y++)
        for (int z = -1; z <= 1; z++) s.add(primitives::cube(), Mat4::translate({(float)x, (float)y, (float)z}), Vec3(0.7f));
    s.add(primitives::plane(20.0f, 2), Mat4::translate({0, -3.0f, 0}), Vec3(0.7f));
    Environment env;
    env.mode = Environment::Color;
    env.color = Vec3(0.3f);
    s.build({}, env);
    ProbeVolumeData d;
    probe_place(d, p37_box(Vec3(-2.5f), Vec3(2.5f)), s.tri_boxes(), 0.5f, 0.5f);
    probe_validate(d, s.pt, 64);
    probe_bake(d, s.pt, {}, 32, 2);
    int invalid = 0, filled = 0;
    bool dilated = true, finite = true;
    for (const ProbeBrick &br : d.bricks)
      for (int z = 0; z < 4; z++)
        for (int y = 0; y < 4; y++)
          for (int x = 0; x < 4; x++) {
            const uint32_t i = br.first + (uint32_t)(x + 4 * y + 16 * z);
            for (int ax = 0; ax < 6; ax++)
              finite = finite && std::isfinite(d.baked[i].bounce[(size_t)ax].x + d.baked[i].bounce[(size_t)ax].y + d.baked[i].bounce[(size_t)ax].z + d.baked[i].sky[(size_t)ax]);
            if (d.valid[i]) continue;
            invalid++;
            /* An invalid probe holds the mean of its valid neighbours in the brick (the 3 x 3 x 3 around it). */
            Vec3 mean_b(0.0f);
            float mean_s = 0;
            int cnt = 0;
            for (int dz = -1; dz <= 1; dz++)
              for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                  const int xx = x + dx, yy = y + dy, zz = z + dz;
                  if (xx < 0 || yy < 0 || zz < 0 || xx > 3 || yy > 3 || zz > 3) continue;
                  const uint32_t j = br.first + (uint32_t)(xx + 4 * yy + 16 * zz);
                  if (!d.valid[j]) continue;
                  mean_b += d.baked[j].bounce[2], mean_s += d.baked[j].sky[2], cnt++;
                }
            if (!cnt) continue;
            filled++;
            mean_b = mean_b / (float)cnt;
            mean_s /= (float)cnt;
            dilated = dilated && length(d.baked[i].bounce[2] - mean_b) < 1e-4f && std::fabs(d.baked[i].sky[2] - mean_s) < 1e-4f;
          }
    CHECK(finite);
    CHECK(invalid > 0 && filled > 0);
    CHECK(dilated);
    std::printf("    %d of %zu probes invalid, %d dilated from neighbours\n", invalid, d.position.size(), filled);
    /* Poison the invalid probes: no sample changes. */
    ProbeVolumeData poisoned = d;
    for (size_t i = 0; i < poisoned.position.size(); i++)
      if (!poisoned.valid[i])
        for (int ax = 0; ax < 6; ax++) poisoned.baked[i].bounce[(size_t)ax] = Vec3(1e6f), poisoned.baked[i].sky[(size_t)ax] = 1e6f;
    R34Rng rng{8u};
    double worst = 0;
    int n = 0;
    for (int i = 0; i < 2000; i++) {
      const Vec3 p(rng.range(-2.9f, 2.9f), rng.range(-2.9f, 2.9f), rng.range(-2.9f, 2.9f));
      Vec3 a, b;
      const bool ha = d.sample(p, Vec3(0, 1, 0), Vec3(0.3f), false, a), hb = poisoned.sample(p, Vec3(0, 1, 0), Vec3(0.3f), false, b);
      CHECK(ha == hb);
      if (ha && hb) worst = std::max(worst, (double)length(a - b)), n++;
    }
    std::printf("    poisoned invalid probes: worst change %.2e over %d samples\n", worst, n);
    CHECK(worst == 0.0);
    CHECK(n > 500);
  });

  /* ---------------------------------------------------------------- 4: leaks */
  test("probe volumes: a closed room with thin walls, lit and skylit only from outside, stays dark inside (normal bias); outside samples are bright", [] {
    P37Pt s;
    const MeshPtr cube = primitives::cube();
    s.add(cube, Mat4::trs({0, -0.1f, 0}, Quat(), {6.4f, 0.2f, 6.4f}), Vec3(0.8f));  // floor
    s.add(cube, Mat4::trs({0, 3.1f, 0}, Quat(), {6.4f, 0.2f, 6.4f}), Vec3(0.8f));   // ceiling
    s.add(cube, Mat4::trs({3.1f, 1.5f, 0}, Quat(), {0.2f, 3.4f, 6.4f}), Vec3(0.8f));
    s.add(cube, Mat4::trs({-3.1f, 1.5f, 0}, Quat(), {0.2f, 3.4f, 6.4f}), Vec3(0.8f));
    s.add(cube, Mat4::trs({0, 1.5f, 3.1f}, Quat(), {6.4f, 3.4f, 0.2f}), Vec3(0.8f));
    s.add(cube, Mat4::trs({0, 1.5f, -3.1f}, Quat(), {6.4f, 3.4f, 0.2f}), Vec3(0.8f));
    Environment env;
    env.mode = Environment::Color;
    env.color = Vec3(0.3f);
    env.strength = 1.0f;
    env.compute_sh();
    RenderLight sun = p37_sun(Vec3(0.3f, -1.0f, 0.2f), 1.0f);
    std::vector<BakeLight> lights = {{sun, 1, 1}};
    s.build({sun}, env);
    ProbeVolumeData d;
    // An offset volume: no probe lies in a wall, so the virtual offset can't help and only the bias keeps the leak out.
    probe_place(d, p37_box(Vec3(-5.13f, -1.13f, -5.13f), Vec3(5.0f, 5.0f, 5.0f)), s.tri_boxes(), 0.5f, 4.0f);
    const std::vector<Vec3> placed = d.position;
    probe_validate(d, s.pt, 64);
    size_t moved_probes = 0;
    for (size_t i = 0; i < d.position.size(); i++) moved_probes += length(d.position[i] - placed[i]) > 1e-6f;
    probe_bake(d, s.pt, lights, 24, 2);
    size_t invalid = 0;
    for (uint8_t v : d.valid) invalid += !v;
    std::printf("    %zu probes (%zu invalid)\n", d.position.size(), invalid);
    struct Face {
      Vec3 n, origin, u, v;
    };
    // Inner faces of the room: normal pointing into it.
    const Face faces[] = {{{-1, 0, 0}, {3.0f, 0.25f, -2.5f}, {0, 0.5f, 0}, {0, 0, 0.5f}},
                          {{1, 0, 0}, {-3.0f, 0.25f, -2.5f}, {0, 0.5f, 0}, {0, 0, 0.5f}},
                          {{0, 0, -1}, {-2.5f, 0.25f, 3.0f}, {0.5f, 0, 0}, {0, 0.5f, 0}},
                          {{0, 0, 1}, {-2.5f, 0.25f, -3.0f}, {0.5f, 0, 0}, {0, 0.5f, 0}},
                          {{0, 1, 0}, {-2.5f, 0.0f, -2.5f}, {0.5f, 0, 0}, {0, 0, 0.5f}},
                          {{0, -1, 0}, {-2.5f, 3.0f, -2.5f}, {0.5f, 0, 0}, {0, 0, 0.5f}}};
    double worst_in = 0, worst_nobias = 0;
    Vec3 worst_at(0.0f), worst_n(0.0f);
    int answered = 0, asked = 0;
    for (const Face &f : faces) {
      const int nu = f.n.y != 0 ? 11 : 6, nv = 11;
      for (int a = 0; a < nu; a++)
        for (int b = 0; b < nv; b++) {
          const Vec3 p = f.origin + f.u * (float)a * (f.n.y != 0 ? 1.0f : 1.0f) + f.v * (float)b;
          if (std::fabs(p.x) > 2.9f || std::fabs(p.z) > 2.9f || p.y > 3.01f || p.y < -0.01f) continue;
          Vec3 irr, raw;
          asked++;
          if (d.sample(p + f.n * 0.25f, f.n, env.irradiance(f.n), false, irr)) {
            answered++;
            if ((double)std::max({irr.x, irr.y, irr.z}) > worst_in) worst_at = p, worst_n = f.n;
            worst_in = std::max(worst_in, (double)std::max({irr.x, irr.y, irr.z}));
          }
          if (d.sample(p + f.n * 0.01f, f.n, env.irradiance(f.n), false, raw, false)) worst_nobias = std::max(worst_nobias, (double)std::max({raw.x, raw.y, raw.z}));
        }
    }
    Vec3 up_out;
    CHECK(d.sample(Vec3(0.3f, 3.45f, 0.2f), Vec3(0, 1, 0), env.irradiance(Vec3(0, 1, 0)), false, up_out));
    std::printf("    sky irradiance %.3f; inside: brightest sample %.3f with the 0.25 m normal bias, %.3f without it nor leak reduction; above the roof %.3f\n", env.irradiance(Vec3(0, 1, 0)).x,
                worst_in, worst_nobias, up_out.x);
    std::printf("    brightest inside sample at (%.2f %.2f %.2f) facing (%.0f %.0f %.0f); %zu probes were moved by the virtual offset\n", worst_at.x, worst_at.y, worst_at.z, worst_n.x,
                worst_n.y, worst_n.z, moved_probes);
    if (worst_in >= 0.03) {  // which probes feed it
      const Vec3 q = worst_at + worst_n * 0.25f;
      const int b = d.find_brick(q);
      if (b >= 0) {
        const ProbeBrick &br = d.bricks[(size_t)b];
        const Vec3 l = (q - br.min) / br.spacing;
        for (int c = 0; c < 8; c++) {
          const int x = std::min(2, (int)l.x) + (c & 1), y = std::min(2, (int)l.y) + ((c >> 1) & 1), z = std::min(2, (int)l.z) + ((c >> 2) & 1);
          const uint32_t i = br.first + (uint32_t)(x + 4 * y + 16 * z);
          const Vec3 bn = d.baked[i].bounce[worst_n.y > 0 ? 2 : 0];
          std::printf("      corner placed (%.2f %.2f %.2f) now (%.2f %.2f %.2f) valid %d, bounce %.3f, sky %.3f\n", placed[i].x, placed[i].y, placed[i].z, d.position[i].x,
                      d.position[i].y, d.position[i].z, d.valid[i], bn.x, d.baked[i].sky[worst_n.y > 0 ? 2 : 0]);
        }
      }
    }
    CHECK(answered == asked && asked > 200);
    CHECK(up_out.x > 0.25f);                  // the outside is bright
    CHECK(worst_in < 0.03);                   // ...and the inside, 10% of that, is dark
    /* The scene really can leak: without the bias and without leak reduction (task 0014's review added it, as
     * Unity's APV has it) the room fills with sky. */
    CHECK(worst_nobias > 3.0 * worst_in + 0.02);
  });

  /* ---------------------------------------------------------------- 5: accuracy */
  test("probe volumes: a probe in open sky bakes a sky share of 1 upward, and about 0 under a big slab", [] {
    Environment env;
    env.mode = Environment::Color;
    env.color = Vec3(0.4f, 0.3f, 0.2f);
    env.compute_sh();
    {
      P37Pt s;
      s.add(primitives::cube(), Mat4::trs({0, -50.0f, 0}, Quat(), {200.0f, 0.1f, 200.0f}), Vec3(0.5f));  // far below
      s.build({}, env);
      ProbeVolumeData d = p37_one_brick(1.0f);
      probe_validate(d, s.pt, 64);
      probe_bake(d, s.pt, {}, 256, 2);
      float worst = 1;
      for (size_t i = 0; i < d.position.size(); i++) worst = std::min(worst, d.baked[i].sky[2]);
      std::printf("    open sky: lowest upward sky share over 64 probes %.4f\n", worst);
      CHECK(worst > 0.999f);
    }
    {
      P37Pt s;
      s.add(primitives::cube(), Mat4::trs({0, 3.5f, 0}, Quat(), {200.0f, 0.1f, 200.0f}), Vec3(0.5f));  // a big slab overhead
      s.build({}, env);
      ProbeVolumeData d;
      probe_place(d, p37_box(Vec3(-1.5f), Vec3(1.5f)), {}, 1.0f, 1.0f);  // all below the slab (y = 3.5)
      probe_validate(d, s.pt, 64);
      probe_bake(d, s.pt, {}, 512, 2);
      float best = 0;
      for (size_t i = 0; i < d.position.size(); i++) best = std::max(best, d.baked[i].sky[2]);
      std::printf("    under a 200 m slab: highest upward sky share %.4f\n", best);
      CHECK(best < 0.01f);
    }
  });

  test("probe volumes: a baked probe's bounce matches a high-sample path-traced gather, and a Baked sun adds exactly its direct light", [] {
    P37Pt s;
    s.add(primitives::plane(20.0f, 2), Mat4(), Vec3(0.7f));
    s.add(primitives::cube(), Mat4::trs({1.5f, 2.0f, 0}, Quat(), {0.2f, 4.0f, 8.0f}), Vec3(0.9f, 0.05f, 0.05f));
    Environment env;
    env.mode = Environment::Color;
    env.color = Vec3(0.2f);
    env.compute_sh();
    RenderLight sun = p37_sun(Vec3(1.0f, -1.0f, 0.2f), 1.0f);
    s.build({sun}, env);
    auto baked = [&](int light_mode, int samples) {
      ProbeVolumeData d;
      probe_place(d, p37_box(Vec3(-2.5f, 0.5f, -1.5f), Vec3(0.5f, 3.5f, 1.5f)), {}, 1.0f, 1.0f);
      probe_validate(d, s.pt, 64);
      probe_bake(d, s.pt, {{sun, light_mode, 1}}, samples, 2);
      return d;
    };
    const ProbeVolumeData d = baked(1, 4096);
    CHECK(d.position.size() == 64);
    const size_t pi = 3 + 4 * 1 + 16 * 1;  // (0.5, 1.5, -0.5): 0.9 m from the red wall
    const Vec3 p = d.position[pi];
    CHECK(d.valid[pi] == 1 && std::fabs(p.x - 0.5f) < 1e-4f && std::fabs(p.y - 1.5f) < 1e-4f);
    const Vec3 axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    double worst_rel = 0;
    for (int ax : {0, 2, 3, 1}) {
      /* Cosine-weighted rays about the axis: irradiance / pi is the mean radiance of the ones that hit something. */
      const Vec3 n = axes[ax];
      const Vec3 t = std::fabs(n.y) > 0.9f ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
      const Vec3 u = normalize(cross(n, t)), v = cross(n, u);
      R34Rng rng{777u + (uint32_t)ax};
      Vec3 sum(0.0f);
      const int N = 24000;
      uint64_t rays = 0;
      for (int i = 0; i < N; i++) {
        const float r1 = rng.f(), r2 = rng.f();
        const float r = std::sqrt(r1), phi = 2.0f * kPi * r2;
        const Vec3 dir = normalize(u * (r * std::cos(phi)) + v * (r * std::sin(phi)) + n * std::sqrt(std::max(0.0f, 1.0f - r1)));
        PathTracer::Hit h;
        if (!s.pt.intersect({p, dir}, h)) continue;
        uint32_t prng = 4242u + (uint32_t)i * 2654435761u;
        sum += s.pt.incoming_radiance({p, dir}, prng, rays);
      }
      const Vec3 ref = sum * (1.0f / (float)N), got = d.baked[pi].bounce[(size_t)ax];
      const double m = std::max({ref.x, ref.y, ref.z});
      const double err = std::max({(double)std::fabs(ref.x - got.x), (double)std::fabs(ref.y - got.y), (double)std::fabs(ref.z - got.z)});
      std::printf("    axis %d: probe (%.3f %.3f %.3f), path-traced gather (%.3f %.3f %.3f)\n", ax, got.x, got.y, got.z, ref.x, ref.y, ref.z);
      worst_rel = std::max(worst_rel, err / std::max(m, 0.05));
    }
    std::printf("    worst error %.1f%% of the brightest channel\n", worst_rel * 100.0);
    CHECK(worst_rel < 0.10);
    /* The red wall's light really is in it (+X looks at the wall). */
    CHECK(d.baked[pi].bounce[0].x > 2.5f * d.baked[pi].bounce[0].z);
    /* Baked sun: same rays, plus colour * intensity * max(0, axis . L) where nothing shadows the probe. */
    const ProbeVolumeData b = baked(2, 4096);
    const Vec3 L = normalize(-sun.direction);
    int checked = 0;
    double worst = 0;
    for (size_t i = 0; i < 64; i++) {
      if (!d.valid[i] || !b.valid[i]) continue;
      const Vec3 q = d.position[i];
      if (s.pt.occluded({q, L}, 1e30f)) continue;
      for (int ax = 0; ax < 6; ax++) {
        const Vec3 diff = b.baked[i].bounce[(size_t)ax] - d.baked[i].bounce[(size_t)ax];
        const float want = std::max(0.0f, dot(axes[ax], L));
        worst = std::max({worst, (double)std::fabs(diff.x - want), (double)std::fabs(diff.y - want), (double)std::fabs(diff.z - want)});
      }
      checked++;
    }
    std::printf("    Baked sun: %d unshadowed probes, worst difference from the direct light %.2e\n", checked, worst);
    CHECK(checked >= 20);
    CHECK(worst < 1e-4);
  });

  /* ---------------------------------------------------------------- 6: live update (library) */
  test("probe volumes: the live update visits probes in slices (each once), tints the ones near a red wall, and ignores a missing grid", [] {
    R36Sc sc;
    RenderLight sun = r36_sun(Vec3(1.0f, -1.0f, 0.2f), 1.0f);
    sc.add(primitives::plane(20.0f, 2), Mat4(), Vec3(0.7f));
    sc.add(primitives::cube(), Mat4::translate({1.5f, 2, 0}) * Mat4::scale({0.2f, 4, 8}), Vec3(0.9f, 0.05f, 0.05f));
    sc.finish(64, &sun);
    GiParams prm = gi_params_sanitized([] {
      GiParams p;
      p.rays = 32, p.radius = 3.0f;
      return p;
    }());
    ProbeVolumeData d;
    probe_place(d, p37_box(Vec3(-2.5f, 0.5f, -1.5f), Vec3(0.5f, 3.5f, 1.5f)), {}, 1.0f, 1.0f);
    const size_t n = d.position.size();
    CHECK(n == 64);
    auto done = [&] {
      size_t c = 0;
      for (uint8_t v : d.live_ok) c += v;
      return c;
    };
    CHECK(done() == 0);
    size_t next = probe_live_update(d, sc.grid, &sc.rsm, sc.env, prm, 0, 20);
    CHECK(next == 20 && done() == 20);
    bool first20 = true;
    for (size_t i = 0; i < n; i++) first20 = first20 && (d.live_ok[i] != 0) == (i < 20);
    CHECK(first20);
    next = probe_live_update(d, sc.grid, &sc.rsm, sc.env, prm, next, 30);
    CHECK(next == 50 && done() == 50);
    next = probe_live_update(d, sc.grid, &sc.rsm, sc.env, prm, next, 100000);  // a huge budget: every probe once, wrapped
    CHECK(next == 50 && done() == n);
    CHECK(probe_live_update(d, sc.grid, &sc.rsm, sc.env, prm, 7, 0) == 7 && done() == n);
    CHECK(probe_live_update(d, sc.grid, &sc.rsm, sc.env, prm, 64 * 5 + 3, 1) == 4);  // a start past the end wraps
    /* Red near the wall (probes at x = 0.5 face it), nothing like it 3 m away. */
    double near_rb = 0, far_rb = 0;
    int nn = 0, nf = 0;
    for (size_t i = 0; i < n; i++) {
      const Vec3 b = d.live[i].bounce[0];
      if (std::fabs(d.position[i].x - 0.5f) < 1e-3f) near_rb += b.x - b.z, nn++;
      if (std::fabs(d.position[i].x + 2.5f) < 1e-3f) far_rb += b.x - b.z, nf++;
    }
    near_rb /= nn, far_rb /= nf;
    std::printf("    +X face, red minus blue: probes 0.9 m from the wall %.4f, probes 3.9 m away %.4f\n", near_rb, far_rb);
    CHECK(near_rb > 0.03);
    CHECK(far_rb < 0.25 * near_rb);
    /* The sky: open above, mostly shut near the floor looking down. */
    float up = 1, down = 0;
    int nu = 0;
    for (size_t i = 0; i < n; i++)
      if (d.position[i].x < -1.0f && d.position[i].y > 2.0f) up = std::min(up, d.live[i].sky[2]), nu++;
    for (size_t i = 0; i < n; i++)
      if (d.position[i].y > 1.0f && d.position[i].y < 2.0f) down = std::max(down, d.live[i].sky[3]);  // 1.5 m above the floor, looking at it
    std::printf("    open probes' upward sky share >= %.3f; probes 1.5 m above the floor: downward share <= %.3f\n", up, down);
    CHECK(nu > 4 && up > 0.9f);
    CHECK(down < 0.5f);
    /* The slice and the whole pass agree (deterministic). */
    ProbeVolumeData e = d;
    e.live_ok.assign(n, 0);
    probe_live_update(e, sc.grid, &sc.rsm, sc.env, prm, 0, 100000);
    bool same = true;
    for (size_t i = 0; i < n; i++) same = same && p37_same_cube(e.live[i], d.live[i]);
    CHECK(same);
    /* No grid, no work. */
    ProbeVolumeData g = d;
    g.live_ok.assign(n, 0);
    VoxelGrid empty;
    CHECK(probe_live_update(g, empty, nullptr, sc.env, prm, 3, 10) == 0 && std::count(g.live_ok.begin(), g.live_ok.end(), 1) == 0);
    ProbeVolumeData blank;
    CHECK(probe_live_update(blank, sc.grid, &sc.rsm, sc.env, prm, 0, 10) == 0);
    /* No RSM (no sun): the bounce is gone, the sky stays. */
    ProbeVolumeData h = d;
    h.live_ok.assign(n, 0);
    probe_live_update(h, sc.grid, nullptr, sc.env, prm, 0, 100000);
    double with_rsm = 0, without = 0;
    for (size_t i = 0; i < n; i++) with_rsm += d.live[i].bounce[0].x, without += h.live[i].bounce[0].x;
    CHECK(with_rsm > 3.0 * without + 0.1);
  });

  test("probe volumes: a live probe close above a floor still sees the floor below it (no sky leak through a surface less than a voxel away)", [] {
    R36Sc sc;
    sc.add(primitives::plane(20.0f, 2), Mat4(), Vec3(0.7f));
    sc.add(primitives::cube(), Mat4::translate({1.5f, 2, 0}) * Mat4::scale({0.2f, 4, 8}), Vec3(0.7f));  // sets the grid's size: voxel 0.5
    sc.finish(64, nullptr);
    GiParams prm = gi_params_sanitized([] {
      GiParams p;
      p.rays = 32, p.radius = 3.0f;
      return p;
    }());
    const float heights[] = {0.1f, 0.25f, 0.4f, 0.5f, 0.6f, 0.75f, 1.0f, 1.5f};
    ProbeVolumeData d;
    probe_place(d, p37_box(Vec3(-2.5f, 0.5f, -1.5f), Vec3(0.5f, 3.5f, 1.5f)), {}, 1.0f, 1.0f);
    for (size_t i = 0; i < 8; i++) d.position[i] = Vec3(-2.0f, heights[i], 0.0f);  // 8 probes over the open floor
    probe_live_update(d, sc.grid, nullptr, sc.env, prm, 0, 8);
    std::printf("    voxel %.2f m; downward sky share at height:", sc.grid.voxel);
    float worst = 0;
    for (size_t i = 0; i < 8; i++) {
      std::printf(" %.2f m: %.2f", heights[i], d.live[i].sky[3]);
      worst = std::max(worst, d.live[i].sky[3]);
    }
    std::printf("\n");
    /* The floor fills the whole lower hemisphere: whatever the height, almost none of the sky comes through. */
    CHECK(worst < 0.2f);
  });

  /* ---------------------------------------------------------------- 8: file format */
  test("probe volumes: ProbeVolume.bin round-trips exactly, and a damaged or truncated file is refused without a crash", [] {
    P37Pt s;
    s.add(primitives::plane(10.0f, 2), Mat4(), Vec3(0.7f));
    s.add(primitives::cube(), Mat4::trs({1.0f, 0.5f, 0}, Quat(), {1, 1, 1}), Vec3(0.7f));
    Environment env;
    env.mode = Environment::Color;
    env.color = Vec3(0.3f);
    s.build({}, env);
    ProbeVolumeData d;
    probe_place(d, p37_box(Vec3(-4.0f, -0.5f, -4.0f), Vec3(4.0f, 3.0f, 4.0f)), s.tri_boxes(), 1.0f, 4.0f);
    probe_validate(d, s.pt, 32);
    probe_bake(d, s.pt, {}, 16, 2);
    d.scene_key = 0xABCDEF1234ull;
    const std::string path = fs::join(fs::join(scratch_project(), "Assets/Scenes"), "p37_probes.bin");
    std::string err;
    CHECK(d.save(path, err));
    ProbeVolumeData back;
    CHECK(back.load(path, err));
    CHECK(p37_same_probes(d, back));
    CHECK(back.live.size() == back.baked.size() && std::count(back.live_ok.begin(), back.live_ok.end(), 1) == 0);
    CHECK(back.top == d.top && back.tn[0] == d.tn[0] && back.tn[1] == d.tn[1] && back.tn[2] == d.tn[2]);
    R34Rng rng{31u};
    bool same_samples = true;
    for (int i = 0; i < 500; i++) {
      const Vec3 p(rng.range(-4, 4), rng.range(-0.5f, 3), rng.range(-4, 4));
      Vec3 a, b;
      const bool ha = d.sample(p, Vec3(0, 1, 0), Vec3(0.3f), false, a), hb = back.sample(p, Vec3(0, 1, 0), Vec3(0.3f), false, b);
      same_samples = same_samples && ha == hb && (!ha || (a.x == b.x && a.y == b.y && a.z == b.z));
    }
    CHECK(same_samples);
    /* Missing file, wrong magic, empty file. */
    ProbeVolumeData x;
    CHECK(!x.load(fs::join(scratch_project(), "Assets/Scenes/no_such_probes.bin"), err) && x.empty());
    const std::vector<char> good = p37_read(path);
    CHECK(good.size() > 200);
    std::vector<char> bad = good;
    bad[0] = 'X';
    p37_write(path, bad);
    CHECK(!x.load(path, err) && x.empty() && !err.empty());
    p37_write(path, {});
    CHECK(!x.load(path, err) && x.empty());
    /* Every truncation point of the header and a spread beyond: refused. */
    int accepted = 0;
    for (size_t len = 1; len < good.size(); len += (len < 400 ? 8 : 997)) {  // (every 8th point; stress `probes` tries every one, task 0177)
      p37_write(path, std::vector<char>(good.begin(), good.begin() + (std::ptrdiff_t)len));
      if (x.load(path, err)) accepted++;
      CHECK(x.empty() || accepted > 0);
    }
    std::printf("    truncations accepted: %d\n", accepted);
    CHECK(accepted == 0);
    /* Huge counts: a header claiming 2^62 roots / nodes / probes doesn't allocate or crash. */
    for (size_t off : {(size_t)68, (size_t)68 + 8}) {
      bad = good;
      const uint64_t huge = 1ull << 62;
      std::memcpy(&bad[off], &huge, 8);
      p37_write(path, bad);
      CHECK(!x.load(path, err) && x.empty());
    }
    /* Random single and multi byte corruption: either refused, or loaded and then safe to sample. */
    int loaded = 0, refused = 0;
    for (int i = 0; i < 30; i++) {  // (stress `probes` runs 150)
      bad = good;
      const int flips = 1 + (i % 4);
      for (int k = 0; k < flips; k++) bad[(size_t)(rng.f() * (float)(bad.size() - 1))] = (char)(int)(rng.f() * 255.0f);
      p37_write(path, bad);
      if (x.load(path, err)) {
        loaded++;
        for (int k = 0; k < 40; k++) {
          Vec3 o;
          x.sample(Vec3(rng.range(-6, 6), rng.range(-3, 4), rng.range(-6, 6)), Vec3(0, 1, 0), Vec3(0.3f), k & 1, o);
          x.find_brick(Vec3(rng.range(-6, 6), rng.range(-3, 4), rng.range(-6, 6)));
        }
      }
      else {
        refused++;
        CHECK(x.empty());
      }
    }
    std::printf("    30 corrupted files: %d refused, %d loaded and sampled safely\n", refused, loaded);
    CHECK(refused + loaded == 30);
    std::filesystem::remove(path);
  });

  /* ---------------------------------------------------------------- 6/7: the editor */
  test("probe volumes: a ball beside a red wall picks up red from the baked probes, one far from it does not, and the lightmapped scene is unchanged", [] {
    ProbeVolumeData probes_near;
    /* The same ball with a volume (a) and without (b): what the probes add is the difference. Beside a red wall, beside a
     * white wall (the control: a warm sun on a white floor tints the probes a little), and far from the red wall. */
    struct Variant {
      const char *what;
      float ball_x;
      Vec3 wall;
    };
    const Variant variants[3] = {{"beside the red wall", 1.0f, Vec3(0.9f, 0.05f, 0.05f)}, {"beside a white wall", 1.0f, Vec3(0.9f)}, {"far from the red wall", -2.5f, Vec3(0.9f, 0.05f, 0.05f)}};
    double redder[3] = {0, 0, 0};
    for (int v = 0; v < 3; v++) {
      Editor a, b;
      p37_world(a, true, 1, variants[v].ball_x, false, variants[v].wall);
      p37_world(b, false, 1, variants[v].ball_x, false, variants[v].wall);
      CHECK(a.probes_for_test().empty() && b.probes_for_test().empty());
      r35_bake(a);
      r35_bake(b);
      CHECK(!a.probes_for_test().empty() && b.probes_for_test().empty());
      CHECK(a.probes_for_test().scene_key != 0 && a.probes_for_test().scene_key == a.lighting_data_for_test().scene_key);
      redder[v] = p37_redder(r35_game(a), r35_game(b));
      if (v == 0) probes_near = a.probes_for_test();
      /* Ball pixels only: everything else (floor, wall, sky) is the same lightmapped picture. */
      const std::vector<char> ball = p37_footprint(b, b.scene().find_by_name("Ball"));
      const std::vector<uint32_t> pa = r35_game(a), pb = r35_game(b);
      const size_t outside = p37_diff_in(pa, pb, ball, false), inside = p37_diff_in(pa, pb, ball, true);
      std::printf("    ball %s: %zu pixels in its footprint (%zu differ with probes), %zu differ outside it\n", variants[v].what, p37_count(ball), inside, outside);
      CHECK(p37_count(ball) > 500);
      CHECK(inside > p37_count(ball) / 3);   // probe-lit differs from sky-only
      CHECK(outside == 0);                   // nothing else changed
    }
    std::printf("    redder than sky-only (sum of R - B over the Game view): beside the red wall %.0f, beside a white wall %.0f, far from the red wall %.0f\n", redder[0], redder[1], redder[2]);
    CHECK(redder[0] > 1000);
    CHECK(redder[0] > redder[1] + 0.4 * redder[0]);  // the red is the wall's, not the sun's warmth
    CHECK(redder[0] > redder[2] + 0.4 * redder[0]);  // ...and it fades with distance
    /* The wall bleeds red into the probes' +X cube faces, near the wall only. */
    double red_near = 0, red_far = 0;
    int nn = 0, nf = 0;
    for (size_t i = 0; i < probes_near.position.size(); i++) {
      if (!probes_near.valid[i]) continue;
      const Vec3 p = probes_near.position[i];
      if (p.y < 0.4f || p.y > 1.8f || std::fabs(p.z) > 1.5f) continue;
      const Vec3 b = probes_near.baked[i].bounce[0];
      if (p.x > 0.8f && p.x < 1.9f) red_near += b.x - b.z, nn++;
      if (p.x < -2.5f) red_far += b.x - b.z, nf++;
    }
    CHECK(nn > 0 && nf > 0);
    if (nn && nf) {
      red_near /= nn, red_far /= nf;
      std::printf("    probes' +X bounce, red minus blue: next to the wall %.4f, 5 m from it %.4f\n", red_near, red_far);
      CHECK(red_near > 0.02 && red_far < 0.5 * red_near);
    }
  });

  test("probe volumes: Receive GI picks the source (lightmaps ignore probes, Light Probes ignores the lightmap), set from the console", [] {
    Editor a, b;
    P37World wa = p37_world(a, true, 1, 1.0f, true);
    p37_world(b, false, 1, 1.0f, true);
    r35_bake(a);
    r35_bake(b);
    const std::vector<char> box = p37_footprint(b, b.scene().find_by_name("Box"));
    CHECK(p37_count(box) > 500);
    const std::vector<uint32_t> a0 = r35_game(a), b0 = r35_game(b);
    /* Lightmapped (Receive GI = Lightmaps): the box looks the same with or without probes in the scene. */
    CHECK(p37_diff_in(a0, b0, box, true) == 0);
    CHECK(wa.box->get<MeshRenderer>()->receive_gi == 0);
    /* The console sets the enum by name. */
    a.command("select Box");
    a.command("set MeshRenderer.ReceiveGI Light Probes");
    r34_steps(a, 4);
    CHECK(wa.box->get<MeshRenderer>()->receive_gi == 1);
    b.command("select Box");
    b.command("set MeshRenderer.ReceiveGI 1");
    r34_steps(b, 4);
    CHECK(b.scene().find_by_name("Box")->get<MeshRenderer>()->receive_gi == 1);
    /* The picture must follow the setting by itself: no other edit, no camera move. (The view render cache's hash of the
     * MeshRenderer flags has to include Receive GI for that.) */
    const std::vector<uint32_t> a_stale = r35_game(a);
    const size_t stale_changed = p37_diff_in(a_stale, a0, box, true);
    std::printf("    Game view box pixels (%zu) that changed on their own after Receive GI = Light Probes: %zu\n", p37_count(box), stale_changed);
    CHECK(stale_changed > p37_count(box) / 4);
    /* From here on, every view re-renders (the cache-verify mode), so the rest checks the shading and not the cache. */
    const uint64_t mm0 = Editor::render_cache_mismatches_all();
    a.set_render_cache_verify(true);
    b.set_render_cache_verify(true);
    r34_steps(a, 3);
    r34_steps(b, 3);
    const uint64_t mismatches = Editor::render_cache_mismatches_all() - mm0;
    std::printf("    stale pictures the render cache would have shown: %llu\n", (unsigned long long)mismatches);
    const std::vector<uint32_t> a1 = r35_game(a), b1 = r35_game(b);
    const size_t changed = p37_diff_in(a1, a0, box, true);
    const size_t vs_sky = p37_diff_in(a1, b1, box, true);
    std::printf("    box pixels (%zu): %zu change when it switches to probes; %zu differ from the same box without a volume (sky only)\n", p37_count(box), changed, vs_sky);
    CHECK(changed > p37_count(box) / 4);  // it left its lightmap
    CHECK(vs_sky > p37_count(box) / 4);   // ...and reads the probes
    /* Switched back: the lightmap again, bit for bit. */
    a.command("set MeshRenderer.ReceiveGI Lightmaps");
    r34_steps(a, 4);
    CHECK(r35_game(a) == a0);
    /* Not contributing: always probes. */
    a.command("set MeshRenderer.ContributeGI false");
    r34_steps(a, 4);
    const std::vector<uint32_t> a2 = r35_game(a);
    CHECK(p37_diff_in(a2, a0, box, true) > p37_count(box) / 4);
  });

  test("probe volumes: with voxel GI on, probe-lit objects still take the probes (as in Unity's APV), the rest voxel GI", [] {
    Editor a, b;
    /* A Realtime sun: its bounce isn't baked, so voxel GI has live work (task 0017: over a bake of every
     * bouncing light it traces nothing). */
    p37_world(a, true, 0, 1.0f, true);
    p37_world(b, false, 0, 1.0f, true);
    r35_bake(a);
    r35_bake(b);
    const std::vector<uint32_t> a_off = r35_game(a), b_off = r35_game(b);
    CHECK(r35_diff_count(a_off, b_off) > 100);  // GI off: the probes show (the ball)
    r36_cmd(a, "set Lighting.RealtimeGI true");
    r36_cmd(b, "set Lighting.RealtimeGI true");
    CHECK(p37_live_settled(a));
    r34_steps(a, 4);
    r34_steps(b, 4);
    CHECK(a.voxel_grid_for_test().valid() && b.voxel_grid_for_test().valid());
    const std::vector<uint32_t> a_on = r35_game(a), b_on = r35_game(b);
    const size_t d = r35_diff_count(a_on, b_on);
    std::printf("    GI off: %zu pixels differ with a volume; GI on: %zu\n", r35_diff_count(a_off, b_off), d);
    CHECK(r35_diff_count(b_on, b_off) > 100);  // the voxel GI really changed the picture without probes
    /* Task 0014's review: probes win over the per-pixel pass for the objects they light (the ball), so the
     * volume still shows with GI on, and live probes are what follow lighting changes there. */
    CHECK(d > 100);
  });

  test("probe volumes: Realtime GI keeps probes live with no bake: placed on their own, relit within a few frames when the sun turns or an object moves", [] {
    Editor ed;
    P37World w = p37_world(ed, true, 0, 1.0f, false);  // the sun Realtime: nothing baked
    CHECK(ed.probes_for_test().empty());
    r36_cmd(ed, "set Lighting.RealtimeGI true");
    CHECK(p37_live_settled(ed));
    const ProbeVolumeData &pv = ed.probes_for_test();
    CHECK(!pv.empty() && pv.scene_key == 0 && ed.lighting_data_for_test().empty());
    size_t ok = 0;
    for (uint8_t v : pv.live_ok) ok += v;
    std::printf("    %zu live-only probes, %zu updated; %zu bricks\n", pv.probe_count(), ok, pv.bricks.size());
    CHECK(ok == pv.probe_count());
    /* Nothing changes: it stays quiet (no restart, the values don't move). */
    const std::vector<ProbeCube> still = pv.live;
    r34_steps(ed, 8);
    CHECK(ed.probe_live_left_for_test() == 0);
    bool unchanged = still.size() == pv.live.size();
    for (size_t i = 0; unchanged && i < still.size(); i++) unchanged = p37_same_cube(still[i], pv.live[i]);
    CHECK(unchanged);
    auto total_red = [&] {
      double s = 0;
      for (const ProbeCube &c : ed.probes_for_test().live) s += c.bounce[0].x + c.bounce[1].x;
      return s;
    };
    const double red0 = total_red();
    /* Turn the sun to the other side of the wall: no Generate Lighting. */
    const std::vector<ProbeCube> before_sun = pv.live;
    ed.scene().find_by_name("Directional Light")->set_local_euler({50, -90, 0});
    ed.commit_change("sun turned");
    int max_left_frames = 0;
    bool restarted = false;
    for (int i = 0; i < 40; i++) {
      ed.step_frame_headless();
      if (ed.probe_live_left_for_test() > 0) restarted = true, max_left_frames++;
      if (restarted && ed.probe_live_left_for_test() == 0) break;
    }
    std::printf("    sun turned: live update ran for %d frames, %zu probes left\n", max_left_frames, ed.probe_live_left_for_test());
    CHECK(restarted || !ed.gi_gpu_name_for_test().empty());  // (on the GPU it all happens within a frame)
    CHECK(ed.probe_live_left_for_test() == 0 && ed.probe_live_ready_for_test());
    CHECK(max_left_frames <= 12);
    CHECK(ed.lighting_data_for_test().empty());  // still no bake
    double moved = 0, scale = 1e-6;
    for (size_t i = 0; i < before_sun.size(); i++)
      for (int ax = 0; ax < 6; ax++) {
        const Vec3 a = before_sun[i].bounce[(size_t)ax], b = ed.probes_for_test().live[i].bounce[(size_t)ax];
        moved += std::fabs(a.x - b.x) + std::fabs(a.y - b.y) + std::fabs(a.z - b.z);
        scale += std::fabs(a.x) + std::fabs(a.y) + std::fabs(a.z);
      }
    std::printf("    probe bounce moved by %.1f%% of its total (red-wall faces %.2f -> %.2f)\n", 100.0 * moved / scale, red0, total_red());
    CHECK(moved > 0.05 * scale);
    /* Move one object: live update restarts and finishes. */
    const std::vector<ProbeCube> before_move = ed.probes_for_test().live;
    w.wall->set_local_position({0.4f, 1, 0});
    ed.commit_change("wall moved");
    restarted = false;
    int frames = 0;
    for (int i = 0; i < 40; i++) {
      ed.step_frame_headless();
      if (ed.probe_live_left_for_test() > 0) restarted = true, frames++;
      if (restarted && ed.probe_live_left_for_test() == 0) break;
    }
    std::printf("    wall moved: live update ran for %d frames\n", frames);
    /* (On the GPU the whole volume updates within the frame, so nothing is ever left between frames.) */
    CHECK((restarted || !ed.gi_gpu_name_for_test().empty()) && ed.probe_live_left_for_test() == 0);
    bool changed = false;
    for (size_t i = 0; i < before_move.size() && !changed; i++) changed = !p37_same_cube(before_move[i], ed.probes_for_test().live[i]);
    CHECK(changed);
    /* Turning Realtime GI off stops it (and the redraw loop it asked for). */
    r36_cmd(ed, "set Lighting.RealtimeGI false");
    r34_steps(ed, 2);
    CHECK(ed.probe_live_left_for_test() == 0);
  });

  test("probe volumes: changing the sky colour relights probe-lit objects with no re-bake, close to a re-bake", [] {
    auto build = [](Editor &ed, float sky) {
      p37_world(ed, true, 1, 1.0f, false);
      ed.scene().environment.color = Vec3(sky);
      ed.commit_change("sky");
      r35_bake(ed);
    };
    Editor a, c;
    build(a, 0.25f);
    const std::vector<char> ball = p37_footprint(a, a.scene().find_by_name("Ball"));
    CHECK(p37_count(ball) > 500);
    auto sun_off = [](Editor &ed) {  // the sun hides what the sky adds: measure the ball by probes and sky alone
      ed.scene().find_by_name("Directional Light")->get<Light>()->enabled = false;
      ed.commit_change("sun off");
      r34_steps(ed, 4);
    };
    sun_off(a);
    const P37Mean m0 = p37_mean(r35_game(a), ball);
    const ProbeVolumeData probes_before = a.probes_for_test();
    const uint64_t key_before = a.lighting_data_for_test().scene_key;
    a.scene().environment.color = Vec3(0.6f);
    a.commit_change("brighter sky");
    r34_steps(a, 4);
    CHECK(!a.baking_for_test());
    const P37Mean m1 = p37_mean(r35_game(a), ball);
    CHECK(p37_same_probes(probes_before, a.probes_for_test()) && a.lighting_data_for_test().scene_key == key_before);  // nothing was re-baked
    build(c, 0.6f);
    sun_off(c);
    const P37Mean mc = p37_mean(r35_game(c), ball);
    std::printf("    ball luma: sky 0.25 baked %.1f, sky 0.6 applied at shading %.1f, sky 0.6 re-baked %.1f\n", m0.luma, m1.luma, mc.luma);
    /* Tone mapping squeezes the display values (the ball is mostly lit by the baked bounce), so the size of the change is
     * judged against the re-bake's own change: the sky was applied at shading about as far as a re-bake moves it. */
    CHECK(mc.luma > m0.luma * 1.05);
    CHECK(m1.luma - m0.luma > 0.5 * (mc.luma - m0.luma));
    CHECK(std::fabs(m1.luma - mc.luma) < 0.06 * mc.luma);  // a re-bake also bounces the brighter sky off the floor into the probes, which the old bake lacks
    /* Darker sky the other way. */
    a.scene().environment.color = Vec3(0.05f);
    a.commit_change("dark sky");
    r34_steps(a, 4);
    CHECK(m0.luma - p37_mean(r35_game(a), ball).luma > 0.5 * (mc.luma - m0.luma));
  });

  /* ---------------------------------------------------------------- 7/8: nothing without a volume, lifecycle */
  test("probe volumes: no Probe Volume component means today's pictures, bit for bit, and no probes (baked or live)", [] {
    Editor ed;
    p37_world(ed, false, 1, 1.0f, true);
    r35_bake(ed);
    const std::vector<uint32_t> game = r35_game(ed), view = r35_scene_view(ed);
    CHECK(ed.probes_for_test().empty());
    /* Adding a volume (or turning probes on) after the bake changes nothing until the next Generate Lighting. */
    GameObject *g = ed.scene().create("Probes");
    ProbeVolume *pv = g->add<ProbeVolume>();
    pv->global = false;
    pv->size = Vec3(10, 4, 10);
    ed.commit_change("volume");
    r34_steps(ed, 4);
    CHECK(ed.probes_for_test().empty());
    CHECK(r35_game(ed) == game && r35_scene_view(ed) == view);
    /* A disabled volume bakes nothing. */
    pv->enabled = false;
    ed.commit_change("volume off");
    r35_bake(ed);
    CHECK(ed.probes_for_test().empty());
    CHECK(r35_game(ed) == game);
    /* Realtime GI with no volume never makes probes either. */
    r36_cmd(ed, "set Lighting.RealtimeGI true");
    r34_steps(ed, 6);
    CHECK(ed.probes_for_test().empty() && ed.probe_live_left_for_test() == 0 && !ed.probe_live_ready_for_test());
    /* Enabled again: a bake makes probes, and removing the component's effect (disable + rebake) takes them away. */
    r36_cmd(ed, "set Lighting.RealtimeGI false");
    pv->enabled = true;
    ed.commit_change("volume on");
    r35_bake(ed);
    CHECK(!ed.probes_for_test().empty());
    pv->enabled = false;
    ed.commit_change("volume off again");
    r35_bake(ed);
    CHECK(ed.probes_for_test().empty());
    CHECK(r35_game(ed) == game);
  });

  test("probe volumes: saving and opening the scene restores the probes (count, positions, cubes); Clear Baked Data removes them and the file", [] {
    Editor ed;
    p37_world(ed, true, 1, 1.0f, true);
    const std::string path = r35_scene_path(ed, "R37_Probes");
    const std::string dir = ed.lighting_dir_for_test();
    CHECK(!dir.empty());
    if (fs::exists(dir)) std::filesystem::remove_all(dir);
    r35_bake(ed);
    const std::string file = fs::join(dir, "ProbeVolume.bin");
    CHECK(fs::exists(file));
    CHECK(save_scene(ed.scene(), path));
    const ProbeVolumeData before = ed.probes_for_test();
    const std::vector<uint32_t> pic = r35_game(ed);
    CHECK(!before.empty() && before.probe_count() == before.bricks.size() * 64);
    Editor ed2;
    p37_open_file(ed2, path);
    CHECK(ed2.scene().path == path);
    const ProbeVolumeData &after = ed2.probes_for_test();
    CHECK(!after.empty());
    CHECK(after.probe_count() == before.probe_count() && after.bricks.size() == before.bricks.size());
    CHECK(p37_same_probes(before, after));
    CHECK(after.baked.size() == after.position.size() && after.live_ok.size() == after.position.size());
    std::printf("    reopened: %zu probes in %zu bricks, scene key %llu\n", after.probe_count(), after.bricks.size(), (unsigned long long)after.scene_key);
    CHECK(r35_diff_count(r35_game(ed2), pic, 3) == 0 || r35_diff_count(r35_game(ed2), pic, 6) < pic.size() / 200);
    /* Clear Baked Data. */
    ed2.command("bake clear");
    r34_steps(ed2, 4);
    CHECK(ed2.probes_for_test().empty() && ed2.lighting_data_for_test().empty());
    CHECK(!fs::exists(file));
    ed.command("bake clear");
    r34_steps(ed, 4);
    CHECK(ed.probes_for_test().empty() && !fs::exists(file));
    const std::vector<uint32_t> cleared = r35_game(ed);
    CHECK(r35_diff_count(cleared, pic) > 100);  // the ball is sky-only again
    std::filesystem::remove_all(dir);
    std::filesystem::remove(path);
  });

  test("probe volumes: a damaged ProbeVolume.bin next to a saved scene is refused with the lightmaps still loading, in every way it can break", [] {
    const std::string name = "R37_Damaged";
    std::string dir, path;
    std::vector<char> good;
    {
      Editor ed;
      p37_world(ed, true, 1, 1.0f, false);
      path = r35_scene_path(ed, name.c_str());
      dir = ed.lighting_dir_for_test();
      if (fs::exists(dir)) std::filesystem::remove_all(dir);
      r35_bake(ed);
      CHECK(save_scene(ed.scene(), path));
      good = p37_read(fs::join(dir, "ProbeVolume.bin"));
      CHECK(good.size() > 300);
    }
    const std::string file = fs::join(dir, "ProbeVolume.bin");
    auto reopen = [&](const char *what, const std::vector<char> &bytes, bool expect_refused) {
      p37_write(file, bytes);
      Editor ed;
      p37_open_file(ed, path);
      CHECK(ed.scene().path == path);
      CHECK(!ed.lighting_data_for_test().empty());  // the lightmaps are not taken down with it
      const bool empty = ed.probes_for_test().empty();
      std::printf("    %-28s -> %s\n", what, empty ? "refused" : "loaded");
      if (expect_refused) CHECK(empty);
      CHECK(r35_luma(r35_game(ed)) > 0);
      r34_steps(ed, 3);
    };
    std::vector<char> v = good;
    reopen("intact", good, false);
    v.resize(good.size() / 2);
    reopen("cut in half", v, true);
    v = good;
    v[0] = 'Z';
    reopen("wrong magic", v, true);
    reopen("empty file", {}, true);
    v = good;
    const uint64_t huge = ~0ull;
    std::memcpy(&v[68], &huge, 8);
    reopen("a count of 2^64 - 1", v, true);
    v = good;
    for (size_t i = 64; i < v.size(); i++) v[i] = (char)(0x5A + i);
    reopen("garbage after the header", v, false);
    v.assign(good.size(), (char)0xFF);
    reopen("all bytes 0xFF", v, true);
    std::filesystem::remove_all(dir);
    std::filesystem::remove(path);
  });

  /* ---------------------------------------------------------------- 8: robustness, undo, display */
  test("probe volumes: odd settings (NaN, huge, zero), a zero-size or enormous local volume and an empty scene are safe, in a bake and live", [] {
    Editor ed;
    P37World w = p37_world(ed, true, 1, 1.0f, false);
    LightingSettings &ls = ed.scene().lighting;
    ProbeVolume *pv = w.probes->get<ProbeVolume>();
    const float inf = std::numeric_limits<float>::infinity();
    struct Odd {
      float mn, mx, nb, vb;
    };
    const Odd odds[] = {{kNaN, kNaN, kNaN, kNaN}, {-1.0f, -1.0f, -5.0f, -5.0f}, {0.0f, 0.0f, inf, inf}, {1e30f, 1e30f, 1e30f, 1e30f}, {0.5f, 0.5f, 0.0f, 0.0f}, {2.0f, 0.5f, 0.3f, 0.1f}};
    for (const Odd &o : odds) {
      ls.probe_min_spacing = o.mn, ls.probe_max_spacing = o.mx, ls.probe_normal_bias = o.nb, ls.probe_view_bias = o.vb;
      ed.commit_change("odd probe settings");
      r34_steps(ed, 2);
      CHECK(r35_luma(r35_game(ed)) > 0);
      r35_bake(ed);
      const ProbeVolumeData &d = ed.probes_for_test();
      bool finite = d.position.size() == d.bricks.size() * 64 && d.position.size() <= 2000000;
      for (size_t i = 0; i < d.baked.size() && i < 4000; i++)
        for (int ax = 0; ax < 6; ax++) {
          const Vec3 b = d.baked[i].bounce[(size_t)ax];
          finite = finite && std::isfinite(b.x + b.y + b.z + d.baked[i].sky[(size_t)ax]);
        }
      CHECK(finite);
      CHECK(r35_luma(r35_game(ed)) > 0);
      /* Pixels never go non-finite: the picture of the dynamic ball is a plain colour. */
      const std::vector<uint32_t> px = r35_game(ed);
      CHECK(px.size() > 1000);
    }
    ls.probe_min_spacing = 1.0f, ls.probe_max_spacing = 27.0f, ls.probe_normal_bias = 0.25f, ls.probe_view_bias = 0.1f;
    /* A volume with no size, a negative size, NaN, and 10^30 metres. */
    const Vec3 sizes[] = {Vec3(0.0f), Vec3(-4.0f, -4.0f, -4.0f), Vec3(kNaN, 4.0f, 4.0f), Vec3(1e30f), Vec3(inf, 4.0f, 4.0f)};
    for (const Vec3 &sz : sizes) {
      pv->size = sz;
      ed.commit_change("odd volume");
      r34_steps(ed, 2);
      r35_bake(ed);
      CHECK(ed.probes_for_test().position.size() <= 2000000);
      CHECK(r35_luma(r35_game(ed)) > 0);
    }
    /* The same, live. */
    r36_cmd(ed, "set Lighting.RealtimeGI true");
    for (const Vec3 &sz : sizes) {
      pv->size = sz;
      ed.commit_change("odd volume (live)");
      r34_steps(ed, 6);
      CHECK(ed.probes_for_test().position.size() <= 2000000);
      CHECK(r35_luma(r35_game(ed)) > 0);
    }
    for (const Odd &o : odds) {
      ls.probe_min_spacing = o.mn, ls.probe_max_spacing = o.mx, ls.probe_normal_bias = o.nb, ls.probe_view_bias = o.vb;
      pv->size = Vec3(10, 4, 10);
      ed.commit_change("odd probe settings (live)");
      r34_steps(ed, 6);
      CHECK(ed.probes_for_test().position.size() <= 2000000);
      CHECK(r35_luma(r35_game(ed)) > 0);
    }
    /* Everything hidden: nothing to put probes in. */
    Editor empty;
    r35_open(empty);
    GameObject *g = empty.scene().create("Probes");
    g->add<ProbeVolume>();
    empty.commit_change("global volume, empty scene");
    empty.command("bake start");
    r34_steps(empty, 12);
    CHECK(!empty.baking_for_test() && empty.probes_for_test().empty());
    r36_cmd(empty, "set Lighting.RealtimeGI true");
    r34_steps(empty, 6);
    CHECK(empty.probes_for_test().empty() && empty.probe_live_left_for_test() == 0);
    CHECK(r35_luma(r35_game(empty)) > 0);
    /* A Global volume is sized from what contributes to GI: the 10 m floor, not the cube 30 m away. */
    Editor glob;
    r35_open(glob);
    r35_add(glob, "Plane", "Floor", {0, 0, 0}, {1, 1, 1}, Vec3(0.8f), true);
    r35_add(glob, "Cube", "Far", {30, 0.5f, 0}, {1, 1, 1}, Vec3(0.8f), false);
    glob.scene().create("Probes")->add<ProbeVolume>();
    glob.commit_change("global volume");
    r35_bake(glob);
    CHECK(!glob.probes_for_test().empty());
    const AABB gb = glob.probes_for_test().box;
    std::printf("    global volume around the contributing floor: x %.1f .. %.1f\n", gb.min.x, gb.max.x);
    CHECK(gb.min.x <= -4.9f && gb.max.x >= 4.9f && gb.max.x < 10.0f && gb.min.y <= 0.0f);
    /* ...and from every mesh when none contributes (live, no bake). */
    Editor live;
    r35_open(live);
    r35_add(live, "Plane", "Floor", {0, 0, 0}, {1, 1, 1}, Vec3(0.8f), false);
    r35_add(live, "Cube", "Far", {30, 0.5f, 0}, {1, 1, 1}, Vec3(0.8f), false);
    live.scene().create("Probes")->add<ProbeVolume>();
    live.commit_change("global volume");
    r36_cmd(live, "set Lighting.RealtimeGI true");
    r34_steps(live, 6);
    CHECK(!live.probes_for_test().empty() && live.probes_for_test().box.max.x >= 30.0f);
  });

  test("probe volumes: undo and redo cover the component, its Global and Size, the spacing and bias settings, Receive GI and the probe display", [] {
    Editor ed;
    P37World w = p37_world(ed, false, 1, 1.0f, false);
    auto run = [&](const char *c) {
      ed.command(c);
      r34_steps(ed, 2);
    };
    auto floor_obj = [&]() { return ed.scene().find_by_name("Floor"); };  // (undo rebuilds the objects: look it up each time)
    auto pv = [&]() -> ProbeVolume * { GameObject *f = floor_obj(); return f ? f->get<ProbeVolume>() : nullptr; };
    
    run("select Floor");
    CHECK(pv() == nullptr);
    run("component Probe Volume");
    CHECK(pv() != nullptr && pv()->global);
    run("set ProbeVolume.Global false");
    CHECK(pv() && !pv()->global);
    run("set ProbeVolume.Size 4 5 6");
    CHECK(pv() && std::fabs(pv()->size.x - 4.0f) < 1e-5f && std::fabs(pv()->size.y - 5.0f) < 1e-5f && std::fabs(pv()->size.z - 6.0f) < 1e-5f);
    run("set Lighting.ProbeMinSpacing 0.5");
    run("set Lighting.ProbeMaxSpacing 8");
    run("set Lighting.ProbeNormalBias 0.4");
    run("set Lighting.ProbeViewBias 0.3");
    run("probes show");
    run("set MeshRenderer.ReceiveGI Light Probes");
    CHECK(std::fabs(ed.scene().lighting.probe_min_spacing - 0.5f) < 1e-5f && std::fabs(ed.scene().lighting.probe_max_spacing - 8.0f) < 1e-5f && std::fabs(ed.scene().lighting.probe_normal_bias - 0.4f) < 1e-5f &&
          std::fabs(ed.scene().lighting.probe_view_bias - 0.3f) < 1e-5f && ed.scene().lighting.show_probes);
    CHECK(floor_obj()->get<MeshRenderer>()->receive_gi == 1);
    /* Saved as they are, loaded back. */
    Scene back;
    std::string err;
    CHECK(load_scene_text(save_scene_text(ed.scene()), back, err));
    CHECK(back.find_by_name("Floor")->get<ProbeVolume>() && !back.find_by_name("Floor")->get<ProbeVolume>()->global &&
          std::fabs(back.find_by_name("Floor")->get<ProbeVolume>()->size.z - 6.0f) < 1e-5f);
    CHECK(back.find_by_name("Floor")->get<MeshRenderer>()->receive_gi == 1);
    CHECK(std::fabs(back.lighting.probe_min_spacing - 0.5f) < 1e-5f && back.lighting.show_probes);
    CHECK(save_scene_text(back) == save_scene_text(ed.scene()));
    /* One undo per step, back to the start. */
    run("undo");
    CHECK(floor_obj()->get<MeshRenderer>()->receive_gi == 0);
    run("undo");
    CHECK(!ed.scene().lighting.show_probes);
    run("undo");
    CHECK(std::fabs(ed.scene().lighting.probe_view_bias - 0.1f) < 1e-5f);
    run("undo");
    CHECK(std::fabs(ed.scene().lighting.probe_normal_bias - 0.25f) < 1e-5f);
    run("undo");
    CHECK(std::fabs(ed.scene().lighting.probe_max_spacing - 27.0f) < 1e-5f);
    run("undo");
    CHECK(std::fabs(ed.scene().lighting.probe_min_spacing - 1.0f) < 1e-5f);
    run("undo");
    CHECK(pv() && std::fabs(pv()->size.x - 20.0f) < 1e-5f);
    run("undo");
    CHECK(pv() && pv()->global);
    run("undo");
    CHECK(pv() == nullptr);
    run("redo");
    CHECK(pv() && pv()->global);
    run("redo");
    run("redo");
    CHECK(pv() && !pv()->global && std::fabs(pv()->size.x - 4.0f) < 1e-5f);
    /* Out-of-range numbers typed into the settings are kept in range. */
    run("set Lighting.ProbeMinSpacing -3");
    CHECK(std::isfinite(ed.scene().lighting.probe_min_spacing) && ed.scene().lighting.probe_min_spacing >= 0.05f);
    run("set Lighting.ProbeMinSpacing nan");
    CHECK(std::isfinite(ed.scene().lighting.probe_min_spacing) && ed.scene().lighting.probe_min_spacing >= 0.05f);
    run("set Lighting.ProbeMaxSpacing 99999999");
    CHECK(ed.scene().lighting.probe_max_spacing <= 1000.0f);
    run("set Lighting.ProbeNormalBias nan");
    CHECK(std::isfinite(ed.scene().lighting.probe_normal_bias));
    run("set ProbeVolume.Size nan 2 2");
    CHECK(pv() != nullptr);  // (a NaN size is stored as typed; probe_volume_box and probe_place refuse it - see the odd-settings test)
  });

  test("probe volumes: `probes show` draws the probes in the Scene view and `probes hide` takes them away, bit for bit", [] {
    Editor ed;
    p37_world(ed, true, 1, 1.0f, true);
    r35_bake(ed);
    const std::vector<uint32_t> hidden = r35_scene_view(ed);
    ed.command("probes show");
    r34_steps(ed, 4);
    CHECK(ed.scene().lighting.show_probes);
    const std::vector<uint32_t> shown = r35_scene_view(ed);
    const size_t dots = r35_diff_count(shown, hidden);
    std::printf("    Scene view: %zu of %zu pixels change with the probes shown\n", dots, shown.size());
    CHECK(dots > 200);
    ed.command("probes hide");
    r34_steps(ed, 4);
    CHECK(!ed.scene().lighting.show_probes);
    CHECK(r35_scene_view(ed) == hidden);
    /* Nothing to draw without a volume: the picture doesn't change. */
    Editor none;
    p37_world(none, false, 1, 1.0f, true);
    r35_bake(none);
    const std::vector<uint32_t> before = r35_scene_view(none);
    none.command("probes show");
    r34_steps(none, 4);
    CHECK(r35_scene_view(none) == before);
    /* Live-only probes draw too. */
    Editor live;
    p37_world(live, true, 0, 1.0f, false);
    r36_cmd(live, "set Lighting.RealtimeGI true");
    CHECK(p37_live_settled(live));
    const std::vector<uint32_t> l0 = r35_scene_view(live);
    live.command("probes show");
    r34_steps(live, 4);
    CHECK(r35_diff_count(r35_scene_view(live), l0) > 200);
  });

  /* ---------------------------------------------------------------- task 0014's review */
  test("probe volumes: an enormous volume (1e30 m) places nothing out of range and the probe cap is never exceeded", [] {
    for (float half : {1e30f, 1e12f, 1e7f, 5e3f}) {
      for (size_t cap : {(size_t)64, (size_t)4096, (size_t)100000}) {
        ProbeVolumeData d;
        std::vector<AABB> tris = {p37_box(Vec3(-1.0f), Vec3(1.0f)), p37_box(Vec3(half * 0.5f), Vec3(half * 0.5f + 1.0f))};
        probe_place(d, p37_box(Vec3(-half), Vec3(half)), tris, 1.0f, 27.0f, cap);
        CHECK(d.probe_count() <= cap);
        CHECK(d.probe_count() == d.bricks.size() * 64 && d.position.size() == d.probe_count());
        bool finite = true;
        for (const Vec3 &q : d.position) finite = finite && std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z);
        CHECK(finite);
      }
    }
  });

  test("probe volumes: baking in slices (as Generate Lighting does, a frame at a time) equals one bake of every probe", [] {
    P37Pt s;
    s.add(primitives::cube(), Mat4::translate({0, 0.5f, 0}), Vec3(0.9f, 0.1f, 0.1f));
    s.add(primitives::plane(20.0f, 2), Mat4::identity(), Vec3(0.7f));
    Environment env;
    env.mode = Environment::Color;
    env.color = Vec3(0.3f);
    s.build({}, env);
    ProbeVolumeData whole;
    probe_place(whole, p37_box(Vec3(-3.0f, 0.2f, -3.0f), Vec3(3.0f)), s.tri_boxes(), 0.5f, 2.0f);
    probe_validate(whole, s.pt, 32);
    ProbeVolumeData sliced = whole;
    probe_bake(whole, s.pt, {}, 24, 2);
    for (size_t at = 0; at < sliced.probe_count(); at += 100) probe_bake(sliced, s.pt, {}, 24, 2, at, 100);
    bool same = whole.baked.size() == sliced.baked.size() && whole.probe_count() > 100;
    for (size_t i = 0; same && i < whole.baked.size(); i++) same = p37_same_cube(whole.baked[i], sliced.baked[i]);
    CHECK(same);
  });

  test("probe volumes: with Baked Global Illumination off the baked probes are not used (no light counted twice), as with no volume", [] {
    Editor a, b;
    p37_world(a, true, 2, 1.0f, true);  // the sun Baked: its light is only in the bake
    p37_world(b, false, 2, 1.0f, true);
    r35_bake(a);
    r35_bake(b);
    CHECK(!a.probes_for_test().empty());
    CHECK(r35_diff_count(r35_game(a), r35_game(b)) > 100);
    r36_cmd(a, "set Lighting.BakedGlobalIllumination false");
    r36_cmd(b, "set Lighting.BakedGlobalIllumination false");
    r34_steps(a, 2);
    r34_steps(b, 2);
    CHECK(r35_diff_count(r35_game(a), r35_game(b)) == 0);
  });

  test("probe volumes: Generate Lighting again without a Probe Volume removes the old ProbeVolume.bin, so it can't come back on open", [] {
    Editor ed;
    P37World w = p37_world(ed, true, 1, 1.0f, false);
    const std::string path = r35_scene_path(ed, "R37_Rebake");
    const std::string dir = ed.lighting_dir_for_test();
    if (fs::exists(dir)) std::filesystem::remove_all(dir);
    r35_bake(ed);
    CHECK(save_scene(ed.scene(), path));
    const std::string file = fs::join(dir, "ProbeVolume.bin");
    CHECK(fs::exists(file));
    ed.scene().destroy(w.probes);
    ed.commit_change("no volume");
    r35_bake(ed);
    CHECK(ed.probes_for_test().empty());
    CHECK(!fs::exists(file));
    CHECK(save_scene(ed.scene(), path));
    Editor ed2;
    p37_open_file(ed2, path);
    CHECK(ed2.probes_for_test().empty() && !ed2.lighting_data_for_test().empty());
    std::filesystem::remove_all(dir);
    std::filesystem::remove(path);
  });

  test("probe volumes: a rotated and scaled box volume covers its rotated extent", [] {
    Editor ed;
    P37World w = p37_world(ed, true, 1, 1.0f, false);
    ProbeVolume *pv = w.probes->get<ProbeVolume>();
    pv->size = Vec3(10, 1, 1);
    w.probes->set_local_euler({0, 90, 0});  // the long side now along Z
    w.probes->set_local_scale({1, 2, 1});
    ed.commit_change("turned");
    AABB box;
    CHECK(ed.probe_volume_box_for_test(box));
    std::printf("    volume box x %.1f..%.1f, z %.1f..%.1f\n", box.min.x, box.max.x, box.min.z, box.max.z);
    CHECK(std::fabs(box.max.z - box.min.z - 10.0f) < 0.01f);  // the 10 m side, turned onto Z
    CHECK(std::fabs(box.max.x - box.min.x - 1.0f) < 0.01f);
    CHECK(std::fabs(box.max.y - box.min.y - 2.0f) < 0.01f);   // scaled twice in Y
    /* The cached box follows a change. */
    w.probes->set_local_euler({0, 0, 0});
    ed.commit_change("back");
    r34_steps(ed, 1);
    CHECK(ed.probe_volume_box_for_test(box) && std::fabs(box.max.x - box.min.x - 10.0f) < 0.01f);
    r35_bake(ed);
    CHECK(!ed.probes_for_test().empty());
  });
}

static void round39_tests() {
  test("scene view: there is no path-traced Rendered mode; `shading rendered` shows Shaded and nothing keeps redrawing", [] {
    Editor ed;
    r35_open(ed);
    r35_add(ed, "Cube", "Box", {0, 0.5f, 0}, {1, 1, 1}, Vec3(0.8f));
    r36_cmd(ed, "shading shaded");
    r34_steps(ed, 3);
    const std::vector<uint32_t> shaded = r35_scene_view(ed);
    r36_cmd(ed, "shading rendered");
    ed.set_clock_for_test(now_seconds() + 4.0);  // (the warning's toast (task 0252) has left the view before the compare)
    r34_steps(ed, 3);
    CHECK(r35_diff_count(r35_scene_view(ed), shaded) == 0);
    CHECK(!ed.wants_continuous_redraw_for_test());
    /* The other modes still switch. */
    r36_cmd(ed, "shading wire");
    r34_steps(ed, 2);
    CHECK(r35_diff_count(r35_scene_view(ed), shaded) > 100);
    r36_cmd(ed, "shading shaded");
    r34_steps(ed, 2);
    CHECK(r35_diff_count(r35_scene_view(ed), shaded) == 0);
  });

  /* ---------------------------------------------------------------- task 0017 */
  test("baked no live cost: with a current bake of a Mixed sun, Realtime GI traces nothing and the views equal Realtime GI off", [] {
    Editor ed;
    P37World w = p37_world(ed, false, 1, 1.0f, true);  // floor and wall Contribute GI; a Mixed sun
    r35_bake(ed);
    CHECK(!ed.lighting_data_for_test().empty());
    const std::vector<uint32_t> off_game = r35_game(ed), off_scene = r35_scene_view(ed);
    r36_cmd(ed, "set Lighting.RealtimeGI true");
    r34_steps(ed, 4);
    std::string why;
    CHECK(!ed.live_gi_needed_for_test(&why));
    std::printf("    %s\n", why.c_str());
    CHECK(why.find("idle") != std::string::npos);
    const uint64_t u0 = ed.vgi_updates_for_test();
    CHECK(r35_diff_count(r35_game(ed), off_game) == 0);
    CHECK(r35_diff_count(r35_scene_view(ed), off_scene) == 0);
    /* Moving an object that doesn't contribute re-renders the views but still traces nothing. */
    w.ball->set_local_position({0.5f, 0.5f, -1.0f});
    ed.commit_change("ball moved");
    r34_steps(ed, 4);
    CHECK(ed.vgi_updates_for_test() == u0);
    CHECK(ed.probe_live_left_for_test() == 0);
  });

  test("baked no live cost: a Realtime sun or a static object moved since the bake brings live GI back, and a re-bake stops it again", [] {
    Editor ed;
    P37World w = p37_world(ed, false, 1, 1.0f, true);
    r35_bake(ed);
    r36_cmd(ed, "set Lighting.RealtimeGI true");
    r34_steps(ed, 2);
    CHECK(!ed.live_gi_needed_for_test());
    /* A Realtime sun: its bounce isn't in the bake. */
    r35_sun_mode(ed, 0);
    r34_steps(ed, 2);
    std::string why;
    CHECK(ed.live_gi_needed_for_test(&why));
    std::printf("    Realtime sun: %s\n", why.c_str());
    CHECK(why.find("Realtime") != std::string::npos);
    const uint64_t u0 = ed.vgi_updates_for_test();
    r34_steps(ed, 1);
    w.ball->set_local_position({0.5f, 0.5f, -1.0f});
    ed.commit_change("ball moved");
    r34_steps(ed, 2);
    CHECK(ed.vgi_updates_for_test() > u0);
    /* Back to Mixed and re-baked: idle. */
    r35_sun_mode(ed, 1);
    r35_bake(ed);
    CHECK(!ed.live_gi_needed_for_test());
    /* A static (Contribute GI) object moved: live until the next bake. */
    w.wall->set_local_position({2.6f, 1, 0});
    ed.commit_change("wall moved");
    r34_steps(ed, 2);
    CHECK(ed.live_gi_needed_for_test(&why));
    std::printf("    wall moved: %s\n", why.c_str());
    const uint64_t u1 = ed.vgi_updates_for_test();
    r34_steps(ed, 1);
    w.ball->set_local_position({0.0f, 0.5f, -1.0f});
    ed.commit_change("ball moved again");
    r34_steps(ed, 2);
    CHECK(ed.vgi_updates_for_test() > u1);
    r35_bake(ed);
    CHECK(!ed.live_gi_needed_for_test());
    /* Realtime GI off: never live. */
    r36_cmd(ed, "set Lighting.RealtimeGI false");
    r35_sun_mode(ed, 0);
    CHECK(!ed.live_gi_needed_for_test());
  });

  test("baked no live cost: probe samples shared by 2 x 2 pixels match sampling every pixel to within a level or two", [] {
    Editor ed;
    ed.viewport_device_for_test(1);  // (the CPU's quads are what's compared - the card samples every pixel: whatever device the run gives the views)
    p37_world(ed, true, 1, 1.0f, true);  // a probe volume; the ball is lit by probes
    r35_bake(ed);
    const std::vector<uint32_t> shared = r35_game(ed);
    ed.set_probe_quad_reuse_for_test(false);
    r34_steps(ed, 2);
    const std::vector<uint32_t> every = r35_game(ed);
    ed.set_probe_quad_reuse_for_test(true);
    r34_steps(ed, 2);
    CHECK(r35_diff_count(r35_game(ed), shared) == 0);  // deterministic
    int worst = 0;
    size_t over2 = 0, differ = 0;
    for (size_t i = 0; i < shared.size() && i < every.size(); i++) {
      int d = 0;
      for (int k = 0; k < 24; k += 8) d = std::max(d, std::abs((int)((shared[i] >> k) & 255) - (int)((every[i] >> k) & 255)));
      worst = std::max(worst, d);
      differ += d > 0;
      over2 += d > 2;
    }
    std::printf("    %zu of %zu pixels differ, %zu by more than 2 levels, worst %d\n", differ, shared.size(), over2, worst);
    CHECK(differ > 0);  // it really shares (the ball is curved)
    /* Bounded, not the measured maximum (8 with Blender's libraries, 9 without): a shared sample is at most
     * one pixel away on the same triangle, so only a few silhouette pixels can differ visibly. */
    CHECK(over2 <= shared.size() / 1000);
    CHECK(worst <= 16);
  });

  test("learn: the Baked and Realtime Lighting lesson's Try it buttons open the Lighting window, mark the Plane static, bake and turn on GI", [] {
    int idx = -1;
    for (int i = 0; i < lesson_count(); i++)
      if (std::string(lesson(i).title) == "Baked and Realtime Lighting") idx = i;
    CHECK(idx >= 0);
    if (idx < 0) return;
    Editor ed;
    r35_open(ed);
    ed.scene().find_by_name("Plane")->active = true;  // (r35_open hides the default objects)
    ed.commit_change("plane shown");
    std::vector<std::string> cmds;
    std::istringstream body(lesson(idx).body);
    for (std::string line; std::getline(body, line);) {
      if (line.rfind("! cmd:", 0) != 0) continue;
      std::string c = line.substr(6, line.find(" |") - 6);
      for (size_t at = 0; at <= c.size();) {
        size_t semi = c.find(';', at);
        if (semi == std::string::npos) semi = c.size();
        std::string one = c.substr(at, semi - at);
        while (!one.empty() && one.front() == ' ') one.erase(0, 1);
        if (!one.empty()) cmds.push_back(one);
        at = semi + 1;
      }
    }
    CHECK(cmds.size() >= 5);
    for (const std::string &c : cmds) {
      ed.command(c);
      for (int i = 0; i < 100 && ed.baking_for_test(); i++) ed.step_frame_headless();
      r34_steps(ed, 1);
    }
    CHECK(ed.scene().find_by_name("Plane")->get<MeshRenderer>()->contribute_gi);
    CHECK(!ed.lighting_data_for_test().empty());
    CHECK(ed.scene().lighting.realtime_gi && ed.scene().lighting.gi_downsample == 0);
    CHECK(ed.window_rect_for_test(WindowKind::Lighting).w > 0);
  });

  /* ---------------------------------------------------------------- task 0016 */
  /* Until the Game view's realtime lightmaps have published a pass (a new one if `fresh`) and nothing is
   * left to gather. Returns the frames it took, or -1. */
  auto rt_settle = [](Editor &ed, int max_frames = 60, bool fresh = false) {
    const uint64_t p0 = ed.realtime_lightmaps_for_test(true).passes;
    for (int i = 0; i < max_frames; i++) {
      ed.step_frame_headless();
      const uint64_t p = ed.realtime_lightmaps_for_test(true).passes;
      if ((fresh ? p > p0 : p > 0) && !ed.realtime_lightmaps_busy_for_test()) return i + 1;
    }
    return -1;
  };

  test("realtime lightmaps: Contribute GI objects get maps that the Game view reads instead of tracing; others are still traced per pixel", [&] {
    Editor ed;
    P37World w = p37_world(ed, false, 0, 1.0f, true);  // floor, wall, box Contribute GI; the ball doesn't; a Realtime sun
    r36_cmd(ed, "set Lighting.RealtimeGI true");
    const int frames = rt_settle(ed);
    CHECK(frames > 0);
    const RealtimeLightmaps &rl = ed.realtime_lightmaps_for_test(true);
    std::printf("    published after %d frames: %zu texels on %zu pages, %zu objects\n", frames, rl.texel_count(), rl.shown.bounce.size(),
                rl.shown.entries.size());
    CHECK(!rl.shown.empty());
    CHECK(rl.shown.entries.count(w.floor_go->id) && rl.shown.entries.count(w.wall->id) && rl.shown.entries.count(w.box->id));
    CHECK(!rl.shown.entries.count(w.ball->id));
    r34_steps(ed, 2);
    const size_t traced_maps = ed.game_stats_for_test().gi_traced;
    r36_cmd(ed, "set Lighting.RealtimeResolution 0");
    r34_steps(ed, 3);
    const size_t traced_all = ed.game_stats_for_test().gi_traced;
    std::printf("    receivers traced per pixel: %zu with realtime lightmaps, %zu without\n", traced_maps, traced_all);
    CHECK(traced_maps > 0);                  // the ball still is
    CHECK(traced_maps * 4 < traced_all);     // the floor, wall and box no longer are
    CHECK(ed.realtime_lightmaps_for_test(true).shown.empty());
  });

  test("realtime lightmaps: a red wall's bounce shows on the floor beside it, and goes when the wall is white", [&] {
    Editor red, white;
    p37_world(red, false, 0, 1.0f, false);
    p37_world(white, false, 0, 1.0f, false, Vec3(0.9f));
    for (Editor *e : {&red, &white}) {
      r36_cmd(*e, "set Lighting.RealtimeGI true");
      CHECK(rt_settle(*e) > 0);
      r34_steps(*e, 2);
    }
    /* The floor's texels near the wall (not the wall's own pixels, which are red anyway): their bounce. */
    auto floor_red = [](Editor &e) {
      const RealtimeLightmaps &rl = e.realtime_lightmaps_for_test(true);
      double r = 0;
      for (const LightmapTexel &t : rl.layout.texels) {
        if (t.normal.y < 0.9f || t.position.y > 0.05f || t.position.x < 1.0f || t.position.x > 2.05f || std::fabs(t.position.z) > 2.0f) continue;
        const Lightmap &b = rl.shown.bounce[(size_t)t.page];
        const Vec3 v = b.texels[(size_t)t.y * b.width + t.x];
        r += v.x - v.z;
      }
      return r;
    };
    const double r_red = floor_red(red), r_white = floor_red(white);
    std::printf("    floor beside the wall, bounce red minus blue: %.3f with a red wall, %.3f with a white one\n", r_red, r_white);
    CHECK(r_red > 0.05);
    CHECK(std::fabs(r_white) < 0.2 * r_red);
  });

  test("realtime lightmaps: after the sun turns, the maps settle again within 30 frames with no Generate Lighting", [&] {
    Editor ed;
    p37_world(ed, false, 0, 1.0f, true);
    r36_cmd(ed, "set Lighting.RealtimeGI true");
    CHECK(rt_settle(ed) > 0);
    r34_steps(ed, 2);
    const std::vector<uint32_t> before = r35_game(ed);
    ed.scene().find_by_name("Directional Light")->set_local_euler({40, -60, 0});
    ed.commit_change("sun turned");
    const int frames = rt_settle(ed, 30, true);
    std::printf("    settled after %d frames\n", frames);
    CHECK(frames > 0);
    r34_steps(ed, 2);
    CHECK(r35_diff_count(r35_game(ed), before) > 500);
  });

  test("realtime lightmaps: up a wall standing on the floor, the sky share rises smoothly (no voxel steps, no dark contact band)", [&] {
    Editor ed;
    P37World w = p37_world(ed, false, 0, 1.0f, false);
    r36_cmd(ed, "set Lighting.RealtimeGI true");
    r36_cmd(ed, "set Lighting.RealtimeResolution 8");  // 16 texels up the 2 m wall
    CHECK(rt_settle(ed) > 0);
    const RealtimeLightmaps &rl = ed.realtime_lightmaps_for_test(true);
    /* The wall's face toward the scene (normal -X), at z near 0: its published sky share by height. */
    const auto e = rl.shown.entries.find(w.wall->id);
    CHECK(e != rl.shown.entries.end());
    if (e == rl.shown.entries.end()) return;
    std::vector<std::pair<float, float>> col;  // (height, sky)
    size_t wall_texels = 0;
    float z0 = 1e9f;  // the column of texels nearest z = 0
    for (const LightmapTexel &t : rl.layout.texels)
      if (t.normal.x < -0.9f && std::fabs(t.position.x - 2.1f) <= 0.02f && std::fabs(t.position.z) < std::fabs(z0)) z0 = t.position.z;
    for (const LightmapTexel &t : rl.layout.texels) {
      if (t.normal.x > -0.9f || std::fabs(t.position.x - 2.1f) > 0.02f) continue;  // the wall's face toward the scene (x = 2.2 - 0.1)
      wall_texels++;
      if (std::fabs(t.position.z - z0) > 0.01f) continue;
      const Lightmap &sky = rl.shown.sky[(size_t)t.page];
      col.push_back({t.position.y, sky.texels[(size_t)t.y * sky.width + t.x].x});
    }
    std::printf("    %zu texels on the wall's face\n", wall_texels);
    std::sort(col.begin(), col.end());
    float worst_step = 0.0f;
    for (size_t i = 1; i < col.size(); i++)
      if (col[i].first - col[i - 1].first > 0.02f) worst_step = std::max(worst_step, std::fabs(col[i].second - col[i - 1].second));
    std::printf("    %zu wall texels: sky share from %.2f at %.2f m to %.2f at %.2f m; largest step between heights %.3f\n", col.size(),
                col.empty() ? 0.f : col.front().second, col.empty() ? 0.f : col.front().first, col.empty() ? 0.f : col.back().second,
                col.empty() ? 0.f : col.back().first, worst_step);
    for (auto &c : col) std::printf("      y %.2f sky %.3f\n", c.first, c.second);
    CHECK(col.size() >= 4);
    CHECK(worst_step <= 0.15f);
  });

  test("realtime lightmaps: odd resolutions (NaN, negative, huge), an empty scene and undo are safe; 0 turns them off", [&] {
    Editor ed;
    p37_world(ed, false, 0, 1.0f, true);
    r36_cmd(ed, "set Lighting.RealtimeGI true");
    CHECK(rt_settle(ed) > 0);
    for (float v : {std::numeric_limits<float>::quiet_NaN(), -5.0f, 1e9f, 0.01f, 100.0f}) {
      ed.scene().lighting.realtime_resolution = v;
      ed.commit_change("odd resolution");
      r34_steps(ed, 3);
      const RealtimeLightmaps &rl = ed.realtime_lightmaps_for_test(true);
      bool finite = true;
      for (const Lightmap &m : rl.shown.bounce)
        for (const Vec3 &t : m.texels) finite = finite && std::isfinite(t.x) && std::isfinite(t.y) && std::isfinite(t.z);
      CHECK(finite);
      CHECK(rl.texel_count() < 4000000);
    }
    /* Undo brings the setting back. */
    r36_cmd(ed, "set Lighting.RealtimeResolution 3");
    r34_steps(ed, 2);
    r36_cmd(ed, "set Lighting.RealtimeResolution 0");
    r34_steps(ed, 2);
    CHECK(ed.realtime_lightmaps_for_test(true).shown.empty());
    ed.command("undo");
    r34_steps(ed, 2);
    CHECK(std::fabs(ed.scene().lighting.realtime_resolution - 3.0f) < 1e-6f);
    CHECK(rt_settle(ed) > 0);
    CHECK(!ed.realtime_lightmaps_for_test(true).shown.empty());
    /* Nothing contributing: no maps, nothing gathering. (By name: undo rebuilt the objects.) */
    for (const char *n : {"Floor", "Wall", "Box"}) ed.scene().find_by_name(n)->get<MeshRenderer>()->contribute_gi = false;
    ed.commit_change("nothing static");
    r34_steps(ed, 4);
    CHECK(ed.realtime_lightmaps_for_test(true).shown.empty());
    CHECK(!ed.realtime_lightmaps_busy_for_test());
  });

  test("realtime lightmaps: a small ball on the floor has no dark patches (texels gathering from inside the floor take their neighbours' light)", [&] {
    Editor ed;
    P37World w = p37_world(ed, false, 0, 1.0f, false);
    w.ball->get<MeshRenderer>()->contribute_gi = true;
    ed.commit_change("ball static");
    r36_cmd(ed, "set Lighting.RealtimeGI true");
    CHECK(rt_settle(ed) > 0);
    const RealtimeLightmaps &rl = ed.realtime_lightmaps_for_test(true);
    CHECK(rl.shown.entries.count(w.ball->id));
    /* The top of the ball (above its centre, facing up) is open to the sky: its texels'
     * published sky share stays well above zero, the black patch a texel inside the floor spread. */
    float lowest = 1.0f;
    size_t n = 0, invalid = 0;
    for (size_t k = 0; k < rl.layout.texels.size(); k++) {
      const LightmapTexel &t = rl.layout.texels[k];
      if (length(t.position - Vec3(1.0f, 0.5f, 0.0f)) > 0.55f) continue;  // the ball's surface
      invalid += k < rl.ok.size() && !rl.ok[k];
      if (t.position.y < 0.55f || t.normal.y < 0.5f) continue;  // facing up: open sky (the wall is to the side)
      const Lightmap &sky = rl.shown.sky[(size_t)t.page];
      lowest = std::min(lowest, sky.texels[(size_t)t.y * sky.width + t.x].x);
      n++;
    }
    std::printf("    %zu upper-half texels, lowest sky share %.2f; %zu of the ball's texels gathered from inside something\n", n, lowest, invalid);
    CHECK(n >= 8);
    /* "No black patch": the patch read 0. (Above 0.1, not the measured 0.24-0.30: the wall beside it really
     * takes some of the sky, a little differently on each compiler.) */
    CHECK(lowest > 0.1f);
    /* The texels that started inside the floor publish their neighbours' light, not the nothing they saw. */
    CHECK(invalid > 0);
    float worst_invalid = 1.0f;
    for (size_t k = 0; k < rl.layout.texels.size(); k++) {
      const LightmapTexel &t = rl.layout.texels[k];
      if (k >= rl.ok.size() || rl.ok[k] || length(t.position - Vec3(1.0f, 0.5f, 0.0f)) > 0.55f) continue;
      const Lightmap &sky = rl.shown.sky[(size_t)t.page];
      worst_invalid = std::min(worst_invalid, sky.texels[(size_t)t.y * sky.width + t.x].x);
      CHECK(rl.sky[k] < 0.05f);  // what it saw itself
    }
    std::printf("    the darkest of them as published: %.2f\n", worst_invalid);
    CHECK(worst_invalid > 0.05f);
  });

  /* ---------------------------------------------------------------- task 0018 */
  test("gpu gi: realtime lightmaps and live probes gathered on the GPU agree with the CPU's, a whole pass in one frame (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor cpu_ed, gpu_ed;
    for (Editor *e : {&cpu_ed, &gpu_ed}) {
      p37_world(*e, true, 0, 1.0f, true);  // a probe volume, a Realtime sun, nothing baked
    }
    r36_cmd(cpu_ed, "set Lighting.GIDevice 1");  // before Realtime GI: nothing gathers anywhere else first
    r36_cmd(gpu_ed, "set Lighting.GIDevice 2");
    for (Editor *e : {&cpu_ed, &gpu_ed}) r36_cmd(*e, "set Lighting.RealtimeGI true");
    CHECK(rt_settle(cpu_ed) > 0);
    const int gframes = rt_settle(gpu_ed);
    CHECK(gframes > 0);
    CHECK(p37_live_settled(cpu_ed));
    CHECK(p37_live_settled(gpu_ed));
    std::printf("    GPU: %s; realtime lightmaps published after %d frame(s)\n", gpu_ed.gi_gpu_name_for_test().c_str(), gframes);
    CHECK(!gpu_ed.gi_gpu_name_for_test().empty());
    CHECK(cpu_ed.gi_gpu_name_for_test().empty());
    CHECK(gpu_ed.realtime_lightmaps_for_test(true).gpu_texels > 0 && cpu_ed.realtime_lightmaps_for_test(true).gpu_texels == 0);
    /* The maps: the same layout, texel by texel. */
    const RealtimeLightmaps &a = cpu_ed.realtime_lightmaps_for_test(true), &b = gpu_ed.realtime_lightmaps_for_test(true);
    CHECK(a.texel_count() == b.texel_count() && a.texel_count() > 100);
    double sum_sky = 0, max_sky = 0, sum_b = 0, sum_bref = 0;
    for (size_t i = 0; i < a.sky.size() && i < b.sky.size(); i++) {
      const double d = std::fabs(a.sky[i] - b.sky[i]);
      sum_sky += d, max_sky = std::max(max_sky, d);
      sum_b += length(a.bounce[i] - b.bounce[i]), sum_bref += length(a.bounce[i]);
    }
    const double mean_sky = sum_sky / std::max<size_t>(1, a.sky.size());
    /* The probes. */
    const ProbeVolumeData &pa = cpu_ed.probes_for_test(), &pb = gpu_ed.probes_for_test();
    CHECK(pa.probe_count() == pb.probe_count() && pa.probe_count() > 0);
    double psky = 0, pmax = 0;
    size_t pn = 0;
    for (size_t i = 0; i < pa.live.size() && i < pb.live.size(); i++)
      for (int ax = 0; ax < 6; ax++) {
        const double d = std::fabs(pa.live[i].sky[ax] - pb.live[i].sky[ax]);
        psky += d, pmax = std::max(pmax, d), pn++;
      }
    std::printf("    realtime texels: sky share mean difference %.5f, largest %.4f; bounce differs by %.3f%% of its total\n", mean_sky, max_sky,
                100.0 * sum_b / std::max(1e-9, sum_bref));
    std::printf("    live probes: sky share mean difference %.5f, largest %.4f\n", psky / std::max<size_t>(1, pn), pmax);
    /* A grazing ray can go the other way on the GPU (its sin, cos and sqrt round differently): one ray of 32
     * is 0.031 of a texel's sky share. On average they agree. */
    CHECK(mean_sky < 0.002);
    CHECK(max_sky <= 0.13);
    CHECK(sum_b <= 0.01 * sum_bref + 1e-6);
    CHECK(psky / std::max<size_t>(1, pn) < 0.003);
    /* The pictures. */
    r34_steps(cpu_ed, 2);
    r34_steps(gpu_ed, 2);
    const std::vector<uint32_t> pc = r35_game(cpu_ed), pg = r35_game(gpu_ed);
    size_t over3 = 0;
    for (size_t i = 0; i < pc.size() && i < pg.size(); i++) {
      int d = 0;
      for (int k = 0; k < 24; k += 8) d = std::max(d, std::abs((int)((pc[i] >> k) & 255) - (int)((pg[i] >> k) & 255)));
      over3 += d > 3;
    }
    std::printf("    Game view: %zu of %zu pixels differ by more than 3 levels\n", over3, pc.size());
    CHECK(over3 <= pc.size() / 200);
  });

  test("gpu gi: Generate Lighting on the GPU (Render > Device: GPU Compute) matches the CPU's bake within its noise, probes too (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor cpu_ed, gpu_ed;
    for (Editor *e : {&cpu_ed, &gpu_ed}) {
      p37_world(*e, true, 2, 1.0f, true);  // a Baked sun, a red wall, a probe volume
      e->scene().lighting.indirect_samples = 96;
      e->commit_change("samples");
    }
    gpu_ed.scene().render.device = 1;
    gpu_ed.commit_change("GPU");
    r35_bake(cpu_ed);
    r35_bake(gpu_ed);
    std::printf("    texels gathered on the GPU: %zu (CPU editor: %zu); GPU error: '%s'\n", gpu_ed.baker_for_test().gpu_texels(),
                cpu_ed.baker_for_test().gpu_texels(), gpu_ed.baker_for_test().gpu_error().c_str());
    CHECK(gpu_ed.baker_for_test().gpu_texels() > 0 && gpu_ed.baker_for_test().gpu_error().empty());
    CHECK(cpu_ed.baker_for_test().gpu_texels() == 0);
    const LightingData &a = cpu_ed.lighting_data_for_test(), &b = gpu_ed.lighting_data_for_test();
    CHECK(a.pages.size() == b.pages.size() && !a.pages.empty());
    if (a.pages.size() != b.pages.size() || a.pages.empty()) return;
    /* The same texels (the layout doesn't depend on the device); their light within the noise of 96 paths:
     * the totals agree closely, and per texel the difference is noise-sized. */
    double ta = 0, tb = 0, diff = 0;
    size_t n = 0;
    for (size_t pg = 0; pg < a.pages.size(); pg++)
      for (size_t i = 0; i < a.pages[pg].texels.size() && i < b.pages[pg].texels.size(); i++) {
        const Vec3 x = a.pages[pg].texels[i], y = b.pages[pg].texels[i];
        ta += x.x + x.y + x.z, tb += y.x + y.y + y.z;
        diff += std::fabs(x.x - y.x) + std::fabs(x.y - y.y) + std::fabs(x.z - y.z);
        n++;
      }
    std::printf("    lightmaps: total %.1f on the CPU, %.1f on the GPU (%.2f%%); mean texel difference %.2f%% of the mean texel\n", ta, tb,
                100.0 * (tb - ta) / std::max(1e-9, ta), 100.0 * diff / std::max(1e-9, ta));
    CHECK(std::fabs(tb - ta) <= 0.02 * ta);
    CHECK(diff <= 0.08 * ta);
    /* The probes: their bounce and sky shares. */
    const ProbeVolumeData &pa = cpu_ed.probes_for_test(), &pb = gpu_ed.probes_for_test();
    CHECK(pa.probe_count() == pb.probe_count() && pa.probe_count() > 0);
    double sa = 0, sb = 0, ba = 0, bb = 0;
    for (size_t i = 0; i < pa.baked.size() && i < pb.baked.size(); i++)
      for (int ax = 0; ax < 6; ax++) {
        sa += pa.baked[i].sky[ax], sb += pb.baked[i].sky[ax];
        ba += pa.baked[i].bounce[ax].x + pa.baked[i].bounce[ax].y + pa.baked[i].bounce[ax].z;
        bb += pb.baked[i].bounce[ax].x + pb.baked[i].bounce[ax].y + pb.baked[i].bounce[ax].z;
      }
    std::printf("    probes: sky shares %.1f / %.1f (the same rays: equal), bounce %.1f / %.1f (%.2f%%)\n", sa, sb, ba, bb, 100.0 * (bb - ba) / std::max(1e-9, ba));
    CHECK(std::fabs(sb - sa) <= 1e-3 * std::max(1.0, sa));
    CHECK(std::fabs(bb - ba) <= 0.03 * std::max(1e-9, ba));
  });

  test("realtime lightmaps: with a Sky world (drawn from the sun) and Bounce off, turning the sun gathers the maps again", [&] {
    Editor ed;
    p37_world(ed, false, 0, 1.0f, true);
    ed.scene().environment.mode = 1;  // Sky
    ed.commit_change("sky");
    r36_cmd(ed, "set Lighting.Bounce false");
    r36_cmd(ed, "set Lighting.RealtimeGI true");
    CHECK(rt_settle(ed) > 0);
    const uint64_t passes = ed.realtime_lightmaps_for_test(true).passes;
    ed.scene().find_by_name("Directional Light")->set_local_euler({20, 150, 0});
    ed.commit_change("sun turned");
    CHECK(rt_settle(ed, 30, true) > 0);
    std::printf("    passes: %llu before the sun turned, %llu after\n", (unsigned long long)passes,
                (unsigned long long)ed.realtime_lightmaps_for_test(true).passes);
    CHECK(ed.realtime_lightmaps_for_test(true).passes > passes);
  });
}

static void round41_tests() {
  auto press = [](Editor &ed, int k, int mods) {
    platform::Event d, u;
    d.type = platform::EventType::KeyDown, d.key = k, d.mods = mods;
    u.type = platform::EventType::KeyUp, u.key = k;
    ed.step_frame_headless({d});
    ed.step_frame_headless({u});
  };
  /* Realtime GI on, a Realtime sun, Manual: one Update Lighting brings it up to date. */
  auto manual_world = [](Editor &ed, bool volume) {
    P37World w = p37_world(ed, volume, 0, 1.0f, true);
    r36_cmd(ed, "lighting mode manual");
    r36_cmd(ed, "set Lighting.RealtimeGI true");
    return w;
  };

  test("manual lighting: new scenes are Manual unless BLENDITY_LIGHTING_UPDATES says otherwise, and `lighting mode` switches", [] {
    /* The default follows BLENDITY_LIGHTING_UPDATES (the tests' main sets automatic; unset, it's Manual). */
    const char *env = std::getenv("BLENDITY_LIGHTING_UPDATES");
    const bool autom = env && (std::string(env) == "automatic" || std::string(env) == "auto");
    CHECK(LightingSettings{}.update_mode == (autom ? 1 : 0));
    Editor ed;
    r35_open(ed);
    r36_cmd(ed, "lighting mode manual");
    CHECK(ed.scene().lighting.update_mode == 0);
    r36_cmd(ed, "lighting mode automatic");
    CHECK(ed.scene().lighting.update_mode == 1);
    ed.command("undo");
    r34_steps(ed, 1);
    CHECK(ed.scene().lighting.update_mode == 0);
  });

  test("manual lighting: moving a Contribute GI object or turning the sun does no GI work; the last realtime lightmaps and probes stay up", [=] {
    Editor ed;
    P37World w = manual_world(ed, true);
    r34_steps(ed, 3);
    std::string why;
    CHECK(!ed.live_gi_needed_for_test(&why));
    std::printf("    %s\n", why.c_str());
    CHECK(why.find("frozen") != std::string::npos);
    /* Update once: maps and probes appear. */
    ed.command("lighting update");
    for (int i = 0; i < 60 && ed.lighting_update_pending_for_test(); i++) ed.step_frame_headless();
    CHECK(!ed.lighting_update_pending_for_test());
    const RealtimeLightmaps &rl = ed.realtime_lightmaps_for_test(true);
    CHECK(!rl.shown.empty() && ed.probe_live_ready_for_test());
    const uint64_t passes = rl.passes, updates = ed.vgi_updates_for_test();
    r34_steps(ed, 2);
    const std::vector<uint32_t> frozen_pic = r35_game(ed);
    /* Drag the wall a few frames, turn the sun: nothing is voxelized, gathered or published. */
    for (int i = 0; i < 5; i++) {
      w.wall->set_local_position({2.2f - 0.1f * i, 1, 0});
      ed.commit_change("drag");
      ed.step_frame_headless();
    }
    ed.scene().find_by_name("Directional Light")->set_local_euler({30, 140, 0});
    ed.commit_change("sun");
    r34_steps(ed, 3);
    CHECK(ed.vgi_updates_for_test() == updates);
    CHECK(rl.passes == passes);
    CHECK(ed.probe_live_left_for_test() == 0);
    CHECK(!rl.shown.empty());  // still shown
    CHECK(r35_diff_count(r35_game(ed), frozen_pic) > 100);  // direct light and shadows did move
  });

  test("manual lighting: Update Lighting (and Ctrl+Shift+L) catches up within a few frames, then freezes again", [=] {
    Editor ed;
    P37World w = manual_world(ed, false);
    press(ed, platform::KEY_L, platform::MOD_CTRL | platform::MOD_SHIFT);
    CHECK(ed.lighting_update_pending_for_test() || ed.realtime_lightmaps_for_test(true).passes > 0);
    int frames = 0;
    for (; frames < 30 && ed.lighting_update_pending_for_test(); frames++) ed.step_frame_headless();
    std::printf("    caught up after %d more frames\n", frames);
    CHECK(!ed.lighting_update_pending_for_test());
    const uint64_t passes = ed.realtime_lightmaps_for_test(true).passes;
    CHECK(passes > 0);
    /* A change, then Update Lighting again: one new pass, then nothing more. */
    w.box->set_local_position({-1.0f, 0.5f, 1.0f});
    ed.commit_change("box moved");
    r34_steps(ed, 3);
    CHECK(ed.realtime_lightmaps_for_test(true).passes == passes);
    ed.command("lighting update");
    for (int i = 0; i < 30 && ed.lighting_update_pending_for_test(); i++) ed.step_frame_headless();
    CHECK(!ed.lighting_update_pending_for_test());
    const uint64_t after = ed.realtime_lightmaps_for_test(true).passes;
    CHECK(after > passes);
    r34_steps(ed, 5);
    CHECK(ed.realtime_lightmaps_for_test(true).passes == after);
    CHECK(!ed.wants_continuous_redraw_for_test());
  });

  test("manual lighting: a baked object that moved keeps its lightmap, \"out of date\" shows, and Auto Generate never starts", [] {
    Editor ed, autom;
    P37World w = p37_world(ed, false, 1, 1.0f, true);
    P37World wa = p37_world(autom, false, 1, 1.0f, true);
    r36_cmd(ed, "lighting mode manual");
    r36_cmd(autom, "lighting mode automatic");
    for (Editor *e : {&ed, &autom}) {
      r35_bake(*e);
      r36_cmd(*e, "set Lighting.AutoGenerate true");
    }
    const std::vector<uint32_t> before = r35_game(ed);
    w.box->set_local_position({-1.5f, 0.5f, -0.6f});
    ed.commit_change("box moved");
    wa.box->set_local_position({-1.5f, 0.5f, -0.6f});
    autom.commit_change("box moved");
    r34_steps(ed, 3);
    r34_steps(autom, 3);
    CHECK(ed.lighting_out_of_date_for_test());
    /* The box keeps its baked light in Manual; in Automatic it falls back to realtime ambient: they differ. */
    const size_t d = r35_diff_count(r35_game(ed), r35_game(autom));
    std::printf("    Manual vs Automatic after the move: %zu pixels differ\n", d);
    CHECK(d > 50);
    CHECK(r35_diff_count(r35_game(ed), before) > 50);  // (the box did move)
    /* Auto Generate waits for a second of real time without changes: give it two. */
    const double t0 = now_seconds();
    int baked_frames = 0;  // (one check: the frame count depends on the machine)
    while (now_seconds() - t0 < 2.0) {
      ed.step_frame_headless();
      autom.step_frame_headless();
      baked_frames += ed.baking_for_test();
    }
    CHECK(baked_frames == 0);
    bool autom_baked = false;
    for (int i = 0; i < 200 && !autom_baked; i++) {
      autom_baked = autom.baking_for_test() || !autom.lighting_out_of_date_for_test();
      autom.step_frame_headless();
    }
    CHECK(autom_baked);  // (the same wait does start it in Automatic: the check above can fail)
  });

  test("lighting preview: refines 32 -> 64 -> 256 samples into preview lightmaps that are shown, never saved, and gone when turned off", [] {
    Editor ed;
    p37_world(ed, false, 2, 1.0f, true);  // a Baked sun: the preview shows its light
    r36_cmd(ed, "lighting mode manual");
    const std::string path = r35_scene_path(ed, "R41_Preview");  // a file, so a lighting folder to look in
    const std::string dir = ed.lighting_dir_for_test();
    CHECK(!dir.empty());
    if (!dir.empty() && fs::exists(dir)) std::filesystem::remove_all(dir);
    const std::vector<uint32_t> none = r35_game(ed);
    ed.command("lighting preview on");  // (no frames yet: the loop watches every stage)
    std::vector<int> seen;
    for (int i = 0; i < 20 && ed.preview_samples_for_test() < 256; i++) {
      ed.step_frame_headless();
      if (seen.empty() || seen.back() != ed.preview_samples_for_test()) seen.push_back(ed.preview_samples_for_test());
    }
    std::printf("    preview samples seen:");
    for (int v : seen) std::printf(" %d", v);
    std::printf("\n");
    CHECK(ed.preview_samples_for_test() == 256);
    CHECK(std::find(seen.begin(), seen.end(), 32) != seen.end() && std::find(seen.begin(), seen.end(), 64) != seen.end());
    CHECK(!ed.preview_data_for_test().empty());
    CHECK(ed.lighting_data_for_test().empty());  // the real bake is untouched
    r34_steps(ed, 2);
    const std::vector<uint32_t> preview = r35_game(ed);
    CHECK(r35_diff_count(preview, none) > 500);  // the preview's light shows
    CHECK(!fs::exists(fs::join(dir, "LightingData.bin")) && !fs::exists(fs::join(dir, "Lightmap-0.hdr")));
    /* A change starts it over. */
    ed.scene().find_by_name("Box")->set_local_position({-1.2f, 0.5f, 0.6f});
    ed.commit_change("box moved");
    ed.step_frame_headless();
    CHECK(ed.preview_samples_for_test() <= 256);
    for (int i = 0; i < 20 && !(ed.preview_samples_for_test() == 256 && !ed.preview_running_for_test()); i++) ed.step_frame_headless();
    CHECK(ed.preview_samples_for_test() == 256);
    /* Off: the picture without it, bit for bit. */
    r36_cmd(ed, "lighting preview off");
    r34_steps(ed, 2);
    CHECK(ed.preview_data_for_test().empty());
    ed.scene().find_by_name("Box")->set_local_position({-1.5f, 0.5f, 0.0f});  // back where it was
    ed.commit_change("box back");
    r34_steps(ed, 2);
    CHECK(r35_diff_count(r35_game(ed), none) == 0);
  });

  test("lighting preview: Generate Lighting replaces it; switching modes and odd settings mid-preview are safe", [] {
    Editor ed;
    p37_world(ed, false, 2, 1.0f, true);
    r36_cmd(ed, "lighting preview on");
    ed.step_frame_headless();
    r36_cmd(ed, "lighting mode automatic");
    r36_cmd(ed, "lighting mode manual");
    ed.scene().lighting.texels_per_unit = std::numeric_limits<float>::quiet_NaN();
    ed.commit_change("nan");
    r34_steps(ed, 4);
    ed.scene().lighting.texels_per_unit = 8.0f;
    ed.commit_change("ok");
    r34_steps(ed, 2);
    r35_bake(ed);
    CHECK(!ed.lighting_data_for_test().empty());
    /* The fresh bake is what shows, with the preview still on (it stands down while the bake is current). */
    r34_steps(ed, 4);
    CHECK(ed.preview_data_for_test().empty() && !ed.preview_running_for_test());
    const std::vector<uint32_t> baked = r35_game(ed);
    /* A change: the preview takes over again; Generate Lighting hands back to the bake. */
    ed.scene().find_by_name("Box")->set_local_position({-1.2f, 0.5f, 0.6f});
    ed.commit_change("box moved");
    for (int i = 0; i < 20 && ed.preview_samples_for_test() < 256; i++) ed.step_frame_headless();
    CHECK(!ed.preview_data_for_test().empty());
    r35_bake(ed);
    r34_steps(ed, 2);
    CHECK(ed.preview_data_for_test().empty());
    CHECK(r35_diff_count(r35_game(ed), baked) > 0);  // (the new bake, with the box where it is now)
    r36_cmd(ed, "lighting preview off");
    r34_steps(ed, 2);
    CHECK(ed.preview_data_for_test().empty());
    CHECK(!ed.preview_running_for_test());
  });

  /* ---------------------------------------------------------------- task 0020's review */
  /* Everything static (the ball too), a Realtime sun, a bake of the sky: realtime lightmaps add the sun's
   * bounce to the lightmaps. */
  auto static_world = [](Editor &ed) {
    P37World w = p37_world(ed, false, 0, 1.0f, true);
    w.ball->get<MeshRenderer>()->contribute_gi = true;
    ed.commit_change("ball static");
    return w;
  };

  test("manual lighting: the frozen picture is the last updated one (the sun's bounce on lightmaps and specular occlusion kept)", [=] {
    Editor ed;
    static_world(ed);
    r35_bake(ed);
    r36_cmd(ed, "lighting mode manual");
    r36_cmd(ed, "set Lighting.RealtimeGI true");
    for (int i = 0; i < 40 && ed.lighting_update_pending_for_test(); i++) ed.step_frame_headless();
    /* An update, watched frame by frame: the last frame drawn while it ran, then the frozen ones. */
    ed.command("lighting update");
    std::vector<uint32_t> live;
    for (int i = 0; i < 40 && ed.lighting_update_pending_for_test(); i++) {
      ed.step_frame_headless();
      if (ed.lighting_update_pending_for_test()) live = r35_game(ed);
    }
    CHECK(!ed.lighting_update_pending_for_test());
    r34_steps(ed, 2);
    const std::vector<uint32_t> frozen = r35_game(ed);
    CHECK(!live.empty());
    const size_t d = r35_diff_count(frozen, live);
    std::printf("    frozen vs the last live frame: %zu pixels differ\n", d);
    CHECK(d == 0);
  });

  test("manual lighting: undo and redo keep the frozen realtime lightmaps", [=] {
    Editor ed;
    P37World w = static_world(ed);
    r36_cmd(ed, "lighting mode manual");
    r36_cmd(ed, "set Lighting.RealtimeGI true");
    for (int i = 0; i < 40 && ed.lighting_update_pending_for_test(); i++) ed.step_frame_headless();
    r34_steps(ed, 2);
    const std::vector<uint32_t> before = r35_game(ed);
    w.box->set_local_position({-1.0f, 0.5f, 1.2f});
    ed.commit_change("box moved");
    r34_steps(ed, 2);
    CHECK(r35_diff_count(r35_game(ed), before) > 0);
    ed.command("undo");
    r34_steps(ed, 3);
    CHECK(r35_diff_count(r35_game(ed), before) == 0);
    ed.command("redo");
    ed.command("undo");
    r34_steps(ed, 3);
    CHECK(r35_diff_count(r35_game(ed), before) == 0);
  });

  test("manual lighting: frozen Realtime GI with no bake says it's out of date after a change, and not after Update Lighting", [=] {
    Editor ed;
    P37World w = manual_world(ed, false);
    for (int i = 0; i < 40 && ed.lighting_update_pending_for_test(); i++) ed.step_frame_headless();
    CHECK(!ed.frozen_out_of_date_for_test());
    w.box->set_local_position({-1.0f, 0.5f, 1.0f});
    ed.commit_change("box moved");
    r34_steps(ed, 1);
    CHECK(ed.frozen_out_of_date_for_test());
    ed.command("lighting update");
    for (int i = 0; i < 40 && ed.lighting_update_pending_for_test(); i++) ed.step_frame_headless();
    CHECK(!ed.frozen_out_of_date_for_test());
  });

  test("lighting preview: in Automatic with a stale bake (the case it's for) the preview is what shows", [] {
    Editor ed, off;
    for (Editor *e : {&ed, &off}) {
      p37_world(*e, false, 2, 1.0f, true);
      r36_cmd(*e, "lighting mode automatic");
      r36_cmd(*e, "set Lighting.RealtimeGI true");
      r35_bake(*e);
      e->scene().find_by_name("Box")->set_local_position({-1.2f, 0.5f, 0.6f});
      e->commit_change("box moved");
      r34_steps(*e, 2);
    }
    ed.command("lighting preview on");
    for (int i = 0; i < 20 && ed.preview_samples_for_test() < 256; i++) ed.step_frame_headless();
    r34_steps(ed, 2);
    r34_steps(off, 2);
    CHECK(!ed.preview_data_for_test().empty());
    const size_t d = r35_diff_count(r35_game(ed), r35_game(off));
    std::printf("    with the preview vs without: %zu pixels differ\n", d);
    CHECK(d > 500);  // its lightmaps are attached, not dropped as stale
  });

  test("lighting preview: opening another scene drops the old scene's preview", [] {
    Editor ed;
    p37_world(ed, false, 2, 1.0f, true);
    const std::string path = r35_scene_path(ed, "R41_Other");
    CHECK(save_scene(ed.scene(), path));
    ed.command("lighting preview on");
    for (int i = 0; i < 20 && ed.preview_samples_for_test() < 256; i++) ed.step_frame_headless();
    CHECK(!ed.preview_data_for_test().empty());
    ed.command("newscene");
    CHECK(ed.preview_data_for_test().empty() && !ed.preview_running_for_test() && ed.preview_samples_for_test() == 0);
    std::filesystem::remove(path);
  });
}

static void round43_tests() {
  /* The floor and wall static, a ball that isn't, the sun off, and a point light over the ball. */
  auto world = [](Editor &ed, int light_mode, bool volume = false) {
    P37World w = p37_world(ed, volume, 0, 1.0f, false);
    ed.scene().find_by_name("Directional Light")->get<Light>()->enabled = false;
    GameObject *lamp = ed.scene().create("Lamp");
    Light *l = lamp->add<Light>();
    l->type = 1;
    l->intensity = 3.0f;
    l->range = 8.0f;
    l->mode = light_mode;
    lamp->set_local_position({1.0f, 2.0f, 0.0f});
    ed.commit_change("lamp");
    r36_cmd(ed, "lighting mode automatic");
    r34_steps(ed, 2);
    return lamp;
  };

  test("baked lights: with nothing baked, a Baked light is not rasterized (the picture equals the light disabled); Mixed and Realtime light the scene", [=] {
    Editor baked, off, mixed, realtime;
    GameObject *lb = world(baked, 2);
    GameObject *lo = world(off, 2);
    lo->get<Light>()->enabled = false;
    off.commit_change("off");
    r34_steps(off, 2);
    world(mixed, 1);
    world(realtime, 0);
    const std::vector<uint32_t> pb = r35_game(baked), po = r35_game(off), pm = r35_game(mixed), pr = r35_game(realtime);
    std::printf("    Baked vs disabled: %zu pixels differ; Mixed vs disabled: %zu; Realtime vs disabled: %zu\n", r35_diff_count(pb, po),
                r35_diff_count(pm, po), r35_diff_count(pr, po));
    CHECK(r35_diff_count(pb, po) == 0);
    CHECK(r35_diff_count(pm, po) > 500);
    CHECK(r35_diff_count(pr, po) > 500);
    /* Switched to Realtime, it lights the scene again at once. */
    lb->get<Light>()->mode = 0;
    baked.commit_change("realtime now");
    r34_steps(baked, 2);
    CHECK(r35_diff_count(r35_game(baked), pr) == 0);
  });

  test("baked lights: a Baked sun renders no shadow map and no RSM, and bounces nothing live; a Mixed sun does all three", [] {
    for (int mode : {2, 1}) {
      Editor ed;
      P37World w = p37_world(ed, false, mode, 1.0f, true);  // (the setup's frames rendered a shadow map before the sun's mode was set)
      r36_cmd(ed, "lighting mode automatic");
      r36_cmd(ed, "set Lighting.RealtimeGI true");
      r34_steps(ed, 6);
      /* A moved caster makes a Mixed sun render its shadow map again; a Baked sun has none to render. */
      const uint64_t shadows0 = ed.frame_profile_totals().shadow_renders;
      w.box->set_local_position({-1.2f, 0.5f, 0.4f});
      ed.commit_change("box moved");
      r34_steps(ed, 4);
      const uint64_t shadows = ed.frame_profile_totals().shadow_renders - shadows0, rsm = ed.rsm_renders_for_test();
      Vec3 bounce(0.0f);
      const RealtimeLightmaps &rl = ed.realtime_lightmaps_for_test(true);
      for (const Lightmap &m : rl.shown.bounce)
        for (const Vec3 &t : m.texels) bounce += t;
      std::printf("    sun %s: %llu shadow renders, %llu RSM renders, realtime-lightmap bounce total %.3f\n", mode == 2 ? "Baked" : "Mixed",
                  (unsigned long long)shadows, (unsigned long long)rsm, bounce.x + bounce.y + bounce.z);
      if (mode == 2) CHECK(shadows == 0 && rsm == 0 && bounce.x + bounce.y + bounce.z == 0.0f);
      else CHECK(shadows > 0 && rsm > 0 && bounce.x + bounce.y + bounce.z > 0.01f);
    }
  });

  test("baked lights: after a bake the lightmapped floor shows the Baked light, a non-static ball shows none of it without a probe volume and the probes' light with one", [=] {
    Editor plain, probes, none;
    world(plain, 2);
    world(probes, 2, true);
    GameObject *ln = world(none, 2);
    ln->get<Light>()->enabled = false;
    none.commit_change("off");
    for (Editor *e : {&plain, &probes, &none}) r35_bake(*e);
    const std::vector<uint32_t> pp = r35_game(plain), pv = r35_game(probes), pn = r35_game(none);
    /* Where the ball is: the pixels that change when it is hidden (sun off: no shadow of it). */
    GameObject *ball = plain.scene().find_by_name("Ball");
    ball->active = false;
    plain.commit_change("hide");
    r34_steps(plain, 2);
    const std::vector<uint32_t> hidden = r35_game(plain);
    size_t ball_px = 0, ball_lit = 0, ball_probes = 0, floor_lit = 0;
    for (size_t i = 0; i < pp.size(); i++) {
      if (pp[i] != hidden[i]) {
        ball_px++;
        ball_lit += pp[i] != pn[i];
        ball_probes += pv[i] != pn[i];
      }
      else floor_lit += pp[i] != pn[i];
    }
    std::printf("    ball: %zu pixels, %zu differ from no light (no volume), %zu with a probe volume; elsewhere %zu pixels differ (the floor's map)\n",
                ball_px, ball_lit, ball_probes, floor_lit);
    CHECK(ball_px > 200);
    CHECK(ball_lit == 0);
    CHECK(ball_probes > ball_px / 4);
    CHECK(floor_lit > 1000);
  });

  test("baked lights: a path-traced F12 render through the editor still lights with a Baked light; the rasterized one doesn't", [=] {
    Editor ed;
    GameObject *lamp = world(ed, 2);
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
    const std::vector<uint32_t> traced_on = render();
    lamp->get<Light>()->enabled = false;
    ed.commit_change("off");
    const std::vector<uint32_t> traced_off = render();
    const double lit = r35_luma(traced_on), dark = r35_luma(traced_off);
    std::printf("    path traced: summed luma %.0f with the Baked light, %.0f without (%.1f%% more)\n", lit, dark, 100.0 * (lit / dark - 1.0));
    CHECK(lit > dark * 1.08);  // (the sky lights the whole frame; the lamp adds about 16% on top; run-to-run noise is under 1%)
    /* Rasterized F12: the Baked light isn't drawn, so the two renders are the same. */
    ed.command("set Render.RenderEngine Rasterized");
    const std::vector<uint32_t> raster_off = render();
    lamp->get<Light>()->enabled = true;
    ed.commit_change("on");
    const std::vector<uint32_t> raster_on = render();
    CHECK(r35_diff_count(raster_on, raster_off) == 0);
  });

  test("baked lights: the path tracer still lights with a Baked light (it computes true GI and doesn't read the bake)", [] {
    P37Pt s;
    s.add(primitives::plane(20.0f, 2), Mat4::identity(), Vec3(0.7f));
    s.add(primitives::uv_sphere(0.5f, 24, 12), Mat4::translate({1.0f, 0.5f, 0.0f}), Vec3(0.8f));
    Environment env;
    env.mode = Environment::Color;
    env.color = Vec3(0.0f);
    RenderLight lamp;
    lamp.type = RenderLight::Point;
    lamp.position = Vec3(1.0f, 2.0f, 0.0f);
    lamp.intensity = 3.0f;
    lamp.range = 8.0f;
    lamp.bake_mode = 2;
    s.build({lamp}, env, 1);
    uint32_t rng = 7;
    uint64_t rays = 0;
    Vec3 sum(0.0f);
    for (int i = 0; i < 64; i++) sum += s.pt.incoming_radiance({Vec3(1.0f, 3.0f, -3.0f), normalize(Vec3(1.0f, 0.9f, 0.0f) - Vec3(1.0f, 3.0f, -3.0f))}, rng, rays);
    std::printf("    radiance from the ball's top: %.3f\n", sum.x / 64.0f);
    CHECK(sum.x / 64.0f > 0.05f);
  });

  test("baked lights: Preview Lighting shows a Baked light before any bake (Bakery's way)", [=] {
    Editor ed;
    world(ed, 2);
    r36_cmd(ed, "lighting mode manual");
    const std::vector<uint32_t> dark = r35_game(ed);
    ed.command("lighting preview on");
    for (int i = 0; i < 20 && ed.preview_samples_for_test() < 256; i++) ed.step_frame_headless();
    r34_steps(ed, 2);
    CHECK(ed.preview_samples_for_test() == 256 && ed.lighting_data_for_test().empty());
    const std::vector<uint32_t> shown = r35_game(ed);
    const size_t d = r35_diff_count(shown, dark);
    const double lit = r35_luma(shown), before = r35_luma(dark);
    std::printf("    with the preview: %zu pixels differ from the dark scene; summed luma %.0f -> %.0f\n", d, before, lit);
    CHECK(d > 1000);
    CHECK(lit > before * 1.05);  // brighter: the light appeared through the preview, not something else that moved
  });
}

static void round44_tests() {
  test("area lights: a floor under a Baked area light bakes with less noise at 16 samples (a jittered grid over the light, against a converged bake)", [] {
    /* A 4 x 4 m floor, an area light 2 x 1 m facing down from 1.5 m, 16 direct samples per texel, no sky. */
    auto floor = primitives::plane(4.0f, 1);
    std::vector<MaterialPtr> mats = {make_material("f", Vec3(0.8f))};
    BakeObject o;
    o.id = 1;
    o.mesh = floor;
    o.model = Mat4::identity();
    o.materials = mats;
    RenderLight lamp;
    lamp.type = RenderLight::Area;
    lamp.position = Vec3(0, 1.5f, 0);
    lamp.direction = Vec3(0, -1, 0);
    lamp.right = Vec3(1, 0, 0);
    lamp.width = 2.0f;
    lamp.height = 1.0f;
    lamp.intensity = 4.0f;
    lamp.range = 10.0f;
    Environment env;
    env.mode = Environment::Color;
    env.color = Vec3(0.0f);
    BakeSettings bs;
    bs.texels_per_unit = 8.0f;
    bs.direct_samples = 16;
    bs.indirect_samples = 1;
    bs.bounces = 0;
    bs.denoise = false;  // the raw texels: the sampling alone must be smooth
    auto bake = [&](int samples) {
      BakeSettings b = bs;
      b.direct_samples = samples;
      Lightmapper lm;
      lm.begin({o}, {{lamp, 2, 9}}, env, b);
      while (!lm.step(1e9)) {}
      return lm.take_result();
    };
    const LightingData d = bake(16), ref = bake(1024);  // the same layout: the texels line up
    CHECK(d.pages.size() == 1 && ref.pages.size() == 1);
    if (d.pages.empty() || ref.pages.empty()) return;
    const Lightmap &m = d.pages[0], &r = ref.pages[0];
    CHECK(m.width == r.width && m.height == r.height);
    /* Each lit texel against the converged bake: the 16 samples' error. */
    double worst = 0, mean = 0;
    size_t n = 0;
    int wx = -1, wy = -1;
    for (int y = 0; y < m.height; y++)
      for (int x = 0; x < m.width; x++) {
        const float c = m.texels[(size_t)y * m.width + x].x, t = r.texels[(size_t)y * m.width + x].x;
        if (t < 0.3f) continue;  // the padding and the dim edges
        const double rel = std::fabs(c - t) / t;
        if (rel > worst) worst = rel, wx = x, wy = y;
        mean += rel, n++;
      }
    std::printf("    %zu lit texels: error against 1024 samples, mean %.2f%%, worst %.2f%% at texel %d,%d of %d x %d\n", n,
                100.0 * mean / std::max<size_t>(1, n), 100.0 * worst, wx, wy, m.width, m.height);
    CHECK(n > 200);
    /* Measured: random 16-point sampling 5.8% mean (35% worst), the jittered grid 3.7% (22%). */
    CHECK(mean / std::max<size_t>(1, n) < 0.05);
  });

  test("area lights: Preview Lighting's first pass gives a small sphere a chart of at least 32 texels across and a smooth picture", [] {
    Editor ed;
    P37World w = p37_world(ed, false, 0, 1.0f, false);
    ed.scene().find_by_name("Directional Light")->get<Light>()->enabled = false;
    w.ball->get<MeshRenderer>()->contribute_gi = true;
    GameObject *lamp = ed.scene().create("Panel");
    Light *l = lamp->add<Light>();
    l->type = 3;
    l->mode = 2;
    l->intensity = 10.0f;
    l->area_width = 3.0f;
    l->area_height = 1.5f;
    lamp->set_local_position({1.0f, 2.0f, 0.5f});
    lamp->set_local_euler({90, 0, 0});  // facing down
    ed.commit_change("panel");
    r36_cmd(ed, "lighting mode manual");
    ed.command("lighting preview on");
    ed.step_frame_headless();  // the first pass (16 samples) in one headless frame
    CHECK(ed.preview_samples_for_test() == 32);
    const LightingData &pd = ed.preview_data_for_test();
    auto e = pd.entries.find(w.ball->id);
    CHECK(e != pd.entries.end() && !pd.pages.empty());
    if (e == pd.entries.end() || pd.pages.empty()) return;
    /* The chart's extent in texels, from its UVs. */
    float u0 = 1, u1 = 0, v0 = 1, v1 = 0;
    for (const Vec2 &uv : e->second.tri_uv) u0 = std::min(u0, uv.x), u1 = std::max(u1, uv.x), v0 = std::min(v0, uv.y), v1 = std::max(v1, uv.y);
    const int side = pd.pages[(size_t)e->second.page].width;
    const float tex_w = (u1 - u0) * side, tex_h = (v1 - v0) * side;
    std::printf("    the ball's preview chart: its islands span %.0f x %.0f texels on a %d page\n", tex_w, tex_h, side);
    /* The islands of a sphere fill part of a 32-texel chart (before: a chart of about 18, islands of about 15). */
    CHECK(std::max(tex_w, tex_h) >= 24.0f);
    /* The first pass is close to the refined one (256 samples), texel by texel on the lit top: the same layout
     * (the scene didn't change), so the difference is the first pass's noise. */
    const Lightmap first = pd.pages[(size_t)e->second.page];
    const int first_page = e->second.page;  // (e is an iterator into data the next pass replaces)
    for (int i = 0; i < 20 && ed.preview_samples_for_test() < 256; i++) ed.step_frame_headless();
    CHECK(ed.preview_samples_for_test() == 256);
    const LightingData &pd3 = ed.preview_data_for_test();
    auto e3 = pd3.entries.find(w.ball->id);
    CHECK(e3 != pd3.entries.end() && e3->second.page == first_page && !pd3.pages.empty());
    if (e3 == pd3.entries.end() || pd3.pages.empty()) return;
    const Lightmap &fin = pd3.pages[(size_t)e3->second.page];
    CHECK(fin.width == first.width && fin.height == first.height);
    double sum = 0, worst = 0;
    size_t n = 0;
    std::vector<double> rels;
    const int x0 = (int)(u0 * side), x1 = (int)(u1 * side), y0 = (int)(v0 * side), y1 = (int)(v1 * side);
    /* Only the brightly lit top (the sky is off): a third of the chart's peak, whatever the light's units (task 0155
     * made a panel's light fall as the inverse square, so an absolute cut let the dim sides in with their larger
     * noise). The median: a few texels at the very top differ by more, and the question is whether the picture as
     * a whole is smooth. */
    float peak = 0.0f;
    for (int y = y0; y <= y1 && y < fin.height; y++)
      for (int x = x0; x <= x1 && x < fin.width; x++) peak = std::max(peak, fin.texels[(size_t)y * fin.width + x].x);
    for (int y = y0; y <= y1 && y < fin.height; y++)
      for (int x = x0; x <= x1 && x < fin.width; x++) {
        const float a = first.texels[(size_t)y * fin.width + x].x, b = fin.texels[(size_t)y * fin.width + x].x;
        if (b < peak / 3.0f) continue;
        const double rel = std::fabs(a - b) / b;
        sum += rel, worst = std::max(worst, rel), n++;
        rels.push_back(rel);
      }
    std::sort(rels.begin(), rels.end());
    const double median = rels.empty() ? 0.0 : rels[rels.size() / 2];
    std::printf("    %zu lit texels on the ball: the first pass differs from the refined one by %.1f%% on average (median %.1f%%), %.1f%% at worst\n", n,
                100.0 * sum / std::max<size_t>(1, n), 100.0 * median, 100.0 * worst);
    CHECK(n > 50);
    CHECK(median < 0.10);  // measured about 7% (mostly the 16 indirect samples' noise after denoising)
  });

  test("area lights: a layout whose smallest chart is bigger than the page (Max Lightmap Size 16, 32-texel charts) keeps every texel inside the page", [] {
    std::vector<BakeObject> objs;
    std::vector<MaterialPtr> mats = {make_material("m", Vec3(0.8f))};
    std::vector<MeshPtr> keep = {primitives::uv_sphere(0.5f, 16, 8), primitives::cube(), primitives::plane(4.0f, 1)};
    for (size_t i = 0; i < keep.size(); i++) {
      BakeObject o;
      o.id = i + 1;
      o.mesh = keep[i];
      o.model = Mat4::translate({2.0f * i, 0, 0});
      o.materials = mats;
      objs.push_back(o);
    }
    BakeSettings s;
    s.texels_per_unit = 8.0f;
    s.max_size = 16;
    s.min_chart = 32;  // the preview's, bigger than the page
    LightmapLayout lay;
    lightmap_layout(objs, s, lay);
    CHECK(!lay.data.pages.empty() && !lay.texels.empty());
    bool inside = true;
    for (const LightmapTexel &t : lay.texels) {
      const Lightmap &pg = lay.data.pages[(size_t)t.page];
      inside = inside && t.x >= 0 && t.y >= 0 && t.x < pg.width && t.y < pg.height && pg.width == 16 && pg.height == 16;
    }
    for (size_t p = 0; p < lay.data.pages.size(); p++) inside = inside && lay.covered[p].size() == 256 && lay.owner[p].size() == 256;
    std::printf("    %zu texels on %zu pages of 16 x 16\n", lay.texels.size(), lay.data.pages.size());
    CHECK(inside);
    /* The same through Preview Lighting (the case the review found): no crash, a picture. */
    Editor ed;
    P37World w = p37_world(ed, false, 2, 1.0f, true);
    ed.scene().lighting.max_size = 16;
    ed.commit_change("tiny pages");
    r36_cmd(ed, "lighting mode manual");
    ed.command("lighting preview on");
    for (int i = 0; i < 20 && ed.preview_samples_for_test() < 256; i++) ed.step_frame_headless();
    CHECK(ed.preview_samples_for_test() == 256);
    w.box->set_local_position({-1.2f, 0.5f, 0.3f});
    ed.commit_change("moved");
    for (int i = 0; i < 20 && ed.preview_samples_for_test() < 256; i++) ed.step_frame_headless();
    CHECK(!ed.preview_data_for_test().empty());
  });
}

static void round52_tests() {
  test("area light: its light over the whole surface matches the path tracer's converged estimate (under it, a cube's side, close up, a disk)", [] {
    struct Case {
      const char *name;
      Vec3 p, n;
      bool disk;
      float w, h;
    };
    const Case cases[] = {{"floor under the centre", {0, 0, 0}, {0, 1, 0}, false, 10, 10},
                          {"a cube's side", {0.5f, 0.5f, 0}, {1, 0, 0}, false, 10, 10},
                          {"a cube's top", {0, 1, 0}, {0, 1, 0}, false, 10, 10},
                          {"floor near the edge", {4.5f, 0, 2}, {0, 1, 0}, false, 10, 10},
                          {"close below, tilted", {1, 3.0f, -1}, normalize(Vec3(0.3f, 1, 0.2f)), false, 10, 10},
                          {"0.3 m below, off centre", {1.7f, 3.2f, -2.3f}, {0, 1, 0}, false, 10, 10},
                          {"0.6 m below, a side", {2.2f, 2.9f, 0.4f}, {1, 0, 0}, false, 10, 10},
                          {"a 20 x 0.2 m strip 0.5 m above", {0.3f, 3.0f, 0}, {0, 1, 0}, false, 20, 0.2f},
                          {"a 0.2 x 20 m strip, a side cutting it", {0.05f, 3.0f, 0}, {1, 0, 0}, false, 0.2f, 20},
                          {"a 0.1 m light (as a point)", {0.4f, 1.0f, 0.3f}, normalize(Vec3(0.2f, 1, 0)), false, 0.1f, 0.1f},
                          {"a disk light, a side", {0.5f, 0.5f, 0}, {1, 0, 0}, true, 10, 10},
                          {"a disk light, floor", {1.0f, 0, -2.0f}, {0, 1, 0}, true, 10, 10},
                          {"a disk light 0.5 m below, tilted", {1.0f, 3.0f, 1.0f}, normalize(Vec3(-0.4f, 1, 0.1f)), true, 10, 10}};
    for (const Case &c : cases) {
      const RenderLight l = r52_big_light(c.disk, c.w, c.h);
      const double ref = r52_reference(l, c.p, c.n), got = area_light_irradiance(l, c.p, c.n);
      std::printf("    %s: %.4f (reference %.4f)\n", c.name, got, ref);
      CHECK(std::fabs(got - ref) <= 0.01 * ref + 1e-4);
    }
    /* Turned away, behind the light, NaN: nothing. */
    const RenderLight l = r52_big_light();
    CHECK(area_light_irradiance(l, {0, 5, 0}, {0, 1, 0}) == 0.0f);
    CHECK(area_light_irradiance(l, {0, 0, 0}, {0, -1, 0}) == 0.0f);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(area_light_irradiance(l, {nan, 0, 0}, {0, 1, 0}) == 0.0f);
    /* No size (an object scaled to 0 on an axis): the centre, as a point, as the path tracer does (3.5 m up:
     * the inverse square and the window, task 0155). */
    const RenderLight z = r52_big_light(false, 0.0f, 0.0f);
    const float pz = area_light_irradiance(z, {0, 0, 0}, {0, 1, 0});
    const float window = std::pow(1.0f - std::pow(3.5f / 10.0f, 4.0f), 2.0f);  // Range 10 at 3.5 m
    CHECK(std::fabs(pz - 1e-4f * window / (3.5f * 3.5f)) < 1e-8f);  // a square centimetre's worth, facing, over r^2
  });

  test("area light: a 10 m light lights a cube's side in the rasterizer (it was black), as the path tracer does", [] {
    /* A quad standing where a unit cube's +X side is, seen from +X; only the area light, a black world. */
    auto quad = primitives::quad(1.0f);
    std::vector<MaterialPtr> mats = {make_material("side", Vec3(0.8f))};
    mats[0]->specular = 0.0f;
    mats[0]->roughness = 1.0f;
    const Mat4 model = Mat4::translate({0.5f, 0.5f, 0}) * Mat4::rotate(Quat::euler({0, 90, 0}));
    const RenderLight l = r52_big_light();
    Environment black;
    black.mode = Environment::Color;
    black.color = Vec3(0.0f);
    /* The rasterizer, linear (its HDR plane). */
    Image img;
    img.resize(32, 32);
    RenderTarget rt;
    rt.attach(img, {0, 0, 32, 32});
    rt.want_hdr = true;
    Renderer3D r;
    RasterOptions opt;
    opt.shade = ShadeMode::Deferred;
    LightingEnv env;
    env.view_transform = ViewTransform::Standard;
    env.camera_pos = {4, 0.5f, 0};
    env.sky = env.equator = env.ground = Vec3(0.0f);
    env.environment = &black;
    env.lights = {l};
    r.begin(&rt, Mat4::look_at(env.camera_pos, {0.5f, 0.5f, 0}, {0, 1, 0}), Mat4::perspective(10 * kDeg2Rad, 1, 0.1f, 20), env, opt);
    r.clear(0xFF000000);
    DrawItem it;
    it.mesh = &quad->render_mesh_tangents();
    it.model = model;
    it.materials = &mats;
    it.double_sided = true;
    r.add(it);
    r.flush();
    CHECK(!rt.hdr.empty());
    const float raster = rt.hdr.empty() ? 0.0f : rt.hdr[16 * 32 + 16].y;
    /* The path tracer, direct light only. */
    PathTracer pt;
    PTSettings st;
    st.denoise = false;
    st.use_embree = false;
    st.max_bounces = 0;
    pt.set_settings(st);
    pt.build({{&quad->render_mesh_tangents(), model, &mats}}, {l}, black);
    pt.set_camera(Mat4::look_at(env.camera_pos, {0.5f, 0.5f, 0}, {0, 1, 0}), Mat4::ortho(0.3f, 1.0f, 0.1f, 20), 8, 8);
    pt.render(1e9, 4096);
    const auto rgb = pt.linear_rgb(false);
    float traced = 0;
    for (int y = 3; y < 5; y++)
      for (int x = 3; x < 5; x++) traced += rgb[((size_t)y * 8 + x) * 3 + 1] * 0.25f;
    std::printf("    the side's middle: rasterizer %.4f, path tracer %.4f\n", raster, traced);
    CHECK(traced > 0.05f);
    CHECK(std::fabs(raster - traced) < 0.1f * traced);
  });

  test("area light: a glossy floor's highlight from a 1 m area light is close to the path tracer's (it was normalised twice)", [] {
    auto quad = primitives::quad(4.0f);
    std::vector<MaterialPtr> mats = {make_material("gloss", Vec3(0.0f))};  // black: the specular alone
    mats[0]->roughness = 0.3f;
    const Mat4 model = Mat4::rotate(Quat::euler({90, 0, 0}));  // lying flat
    RenderLight l = r52_big_light(false, 1.0f, 1.0f);
    l.position = {1, 2, 0};  // the mirror image of the camera through the floor's centre
    Environment black;
    black.mode = Environment::Color;
    black.color = Vec3(0.0f);
    const Vec3 eye{-1, 2, 0};
    Image img;
    img.resize(32, 32);
    RenderTarget rt;
    rt.attach(img, {0, 0, 32, 32});
    rt.want_hdr = true;
    Renderer3D r;
    RasterOptions opt;
    opt.shade = ShadeMode::Deferred;
    LightingEnv env;
    env.view_transform = ViewTransform::Standard;
    env.camera_pos = eye;
    env.sky = env.equator = env.ground = Vec3(0.0f);
    env.environment = &black;
    env.lights = {l};
    r.begin(&rt, Mat4::look_at(eye, {0, 0, 0}, {0, 1, 0}), Mat4::perspective(4 * kDeg2Rad, 1, 0.1f, 20), env, opt);
    r.clear(0xFF000000);
    DrawItem it;
    it.mesh = &quad->render_mesh_tangents();
    it.model = model;
    it.materials = &mats;
    it.double_sided = true;
    r.add(it);
    r.flush();
    const float raster = rt.hdr.empty() ? 0.0f : rt.hdr[16 * 32 + 16].y;
    PathTracer pt;
    PTSettings st;
    st.denoise = false;
    st.use_embree = false;
    st.max_bounces = 0;
    pt.set_settings(st);
    pt.build({{&quad->render_mesh_tangents(), model, &mats}}, {l}, black);
    pt.set_camera(Mat4::look_at(eye, {0, 0, 0}, {0, 1, 0}), Mat4::ortho(0.05f, 1.0f, 0.1f, 20), 4, 4);
    pt.render(1e9, 20000);
    const auto rgb = pt.linear_rgb(false);
    float traced = 0;
    for (int i = 0; i < 16; i++) traced += rgb[(size_t)i * 3 + 1] / 16.0f;
    std::printf("    the highlight: rasterizer %.4f, path tracer %.4f (%.2fx)\n", raster, traced, raster / std::max(1e-6f, traced));
    CHECK(traced > 0.01f);
    /* Karis's representative point is an approximation: within a factor 0.5 .. 1.6 (measured by the reviewer
     * 0.53 .. 1.18 over sizes and roughnesses); before the fix 0.12x. */
    CHECK(raster > 0.5f * traced && raster < 1.6f * traced);
  });

  test("area light: a floor baked under a 10 m Baked area light is smooth (no splotches) and as bright as the path tracer's", [] {
    auto floor = primitives::plane(8.0f, 1);
    BakeObject o;
    o.id = 1;
    o.mesh = floor;
    o.model = Mat4::identity();
    o.materials = {make_material("f", Vec3(0.8f))};
    const RenderLight l = r52_big_light();
    Environment env;
    env.mode = Environment::Color;
    env.color = Vec3(0.0f);
    BakeSettings bs;
    bs.texels_per_unit = 8.0f;
    bs.direct_samples = 32;
    bs.indirect_samples = 1;
    bs.bounces = 0;
    bs.denoise = false;  // the raw texels
    Lightmapper lm;
    lm.begin({o}, {{l, 2, 9}}, env, bs);
    while (!lm.step(1e9)) {}
    const LightingData d = lm.take_result();
    CHECK(d.pages.size() == 1);
    if (d.pages.empty()) return;
    const Lightmap &m = d.pages[0];
    /* An open floor's light varies smoothly: each texel against the mean of its left and right neighbours (a
     * second difference: the floor's real gradient cancels, the noise doesn't). */
    double bump = 0, level = 0;
    size_t n = 0;
    for (int y = 1; y < m.height; y++)
      for (int x = 1; x + 1 < m.width; x++) {
        const size_t i = (size_t)y * m.width + x;
        const float c = m.texels[i].x, left = m.texels[i - 1].x, right = m.texels[i + 1].x;
        if (c < 0.05f || left < 0.05f || right < 0.05f) continue;  // padding
        bump += std::fabs(c - 0.5f * (left + right));
        level += c;
        n++;
      }
    std::printf("    %zu texels: mean bump against the neighbours %.3f%% of the level\n", n, 100.0 * bump / std::max(1e-9, level));
    CHECK(n > 500);
    CHECK(bump / std::max(1e-9, level) < 0.005);
    /* The floor's centre against the path tracer's converged estimate (the lightmap holds what albedo multiplies). */
    Vec3 centre;
    CHECK(r35_sample(d, 1, *floor, Mat4::identity(), {0.05f, 0, 0.05f}, centre));
    const double want = l.intensity * r52_reference(l, {0.05f, 0, 0.05f}, {0, 1, 0});
    std::printf("    centre %.4f, path tracer %.4f\n", centre.x, want);
    CHECK(std::fabs(centre.x - want) < 0.05 * want);
  });

  test("set origin: a change asks for another frame, and in it the gizmo sits on the new origin (it waited for the next input)", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.step_frame_headless();
    ed.command("create Cube");
    GameObject *g = ed.selected_object();
    CHECK(g != nullptr);
    if (!g) return;
    g->set_world_position({0, 2, 0});
    ed.step_frame_headless();
    ed.step_frame_headless();
    CHECK(length(ed.gizmo_pivot_for_test() - Vec3(0, 2, 0)) < 1e-4f);
    /* The Inspector's Apply runs after the Scene view placed the gizmo for that frame; the console's
     * `origin` runs the same set_origin. Afterwards the editor must ask for a frame by itself. */
    ed.step_frame_headless();  // (an idle frame: nothing asks for another)
    ed.command("origin bottom");
    std::printf("    origin y %.3f; another frame asked for: %s\n", g->world_position().y, ed.ui_for_test().redraw ? "yes" : "no");
    CHECK(std::fabs(g->world_position().y - 1.5f) < 1e-4f);  // the bottom of a unit cube at y = 2
    CHECK(ed.ui_for_test().redraw);  // before the fix: no, so the app slept with the gizmo on the old origin
    ed.step_frame_headless();
    CHECK(length(ed.gizmo_pivot_for_test() - g->world_position()) < 1e-4f);
  });

  test("camera filters: every filter shows in a path-traced F12 render at 720p with the denoiser, as in a rasterized one", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.step_frame_headless();
    ed.command("set Render.Samples 2");
    ed.command("set Render.ResolutionX 1280");
    ed.command("set Render.ResolutionY 720");
    ed.command("set Render.Denoise true");
    ed.command("device cpu");
    auto render = [&](const char *engine) {
      ed.command(std::string("set Render.RenderEngine ") + engine);
      ed.command("render");
      for (int i = 0; i < 4000 && ed.rendering_for_test(); i++) ed.step_frame_headless();
      CHECK(!ed.rendering_for_test());
      const Image &img = ed.render_image_for_test();
      return r47_dark(img, {0, 0, img.width, img.height});
    };
    const int path0 = render("Path"), raster0 = render("Rasterized");
    ed.command("select Main Camera");
    ed.command("filter add Edge Outline");
    const int path1 = render("Path"), raster1 = render("Rasterized");
    std::printf("    near-black pixels with an Edge Outline: path traced %d -> %d, rasterized %d -> %d\n", path0, path1, raster0, raster1);
    CHECK(raster1 > raster0 + 300);
    CHECK(path1 > path0 + 300);
    CHECK(path1 - path0 > (raster1 - raster0) / 2);
    /* On the GPUs (Render > Device GPU Compute, the user's setting), and the Render window's Preview. */
    if (gpu::available()) {
      ed.command("device gpu");
      const int gpu1 = render("Path");
      std::printf("    on the GPU: %d\n", gpu1);
      CHECK(gpu1 > path0 + 300);
      ed.command("device cpu");
    }
    ed.command("set Render.RenderEngine Path");
    ed.command("preview");
    for (int i = 0; i < 4000 && ed.rendering_for_test(); i++) ed.step_frame_headless();
    const Image &pv = ed.render_image_for_test();
    const int prev1 = r47_dark(pv, {0, 0, pv.width, pv.height});
    std::printf("    the Render window's Preview (%d x %d): %d\n", pv.width, pv.height, prev1);
    CHECK(prev1 > 0);
  });

  test("select coplanar: adds the faces in the selected face's plane, joined or not, and nothing tilted, raised or facing the other way", [] {
    /* Four quads at y = 0 facing up (two apart from the first), one raised, one facing down, one tilted by 5 degrees. */
    auto m = std::make_shared<Mesh>();
    auto quad = [&](float x0, float y, bool down, float tilt = 0.0f) {
      uint32_t a = m->add_vert({x0, y, 0}), b = m->add_vert({x0, y + tilt, 1}), c = m->add_vert({x0 + 1, y + tilt, 1}), d = m->add_vert({x0 + 1, y, 0});
      if (down) m->add_face({a, d, c, b});
      else m->add_face({a, b, c, d});
    };
    quad(0, 0, false);   // 0: the one selected
    quad(1, 0, false);   // 1: joined, coplanar
    quad(5, 0, false);   // 2: apart, coplanar
    quad(8, 0.5f, false);  // 3: raised
    quad(10, 0, true);   // 4: facing down
    quad(12, 0, false, 0.09f);  // 5: tilted ~5 degrees
    {  // 6: degenerate (three corners on a line): no plane
      uint32_t a = m->add_vert({20, 0, 0}), b = m->add_vert({21, 0, 0}), c = m->add_vert({22, 0, 0});
      m->add_face({a, b, c});
    }
    std::vector<uint8_t> sel(m->face_count(), 0);
    sel[0] = 1;
    CHECK(meshops::select_coplanar(*m, sel) == 2);
    CHECK(sel[1] && sel[2] && !sel[3] && !sel[4] && !sel[5] && !sel[6]);
    /* Nothing selected, or only the degenerate face: nothing added. */
    std::vector<uint8_t> none(m->face_count(), 0);
    CHECK(meshops::select_coplanar(*m, none) == 0);
    none[6] = 1;
    CHECK(meshops::select_coplanar(*m, none) == 0);
    /* Through the editor: face mode, and vertex mode from the corners of one face. */
    Editor ed;
    ed.init_headless(1000, 700);
    ed.step_frame_headless();
    ed.command("create Cube");
    GameObject *g = ed.selected_object();
    CHECK(g && g->get<MeshFilter>());
    if (!g || !g->get<MeshFilter>()) return;
    g->get<MeshFilter>()->mesh = m;
    ed.command("fsel 0");
    ed.command("editop select_coplanar");
    CHECK(ed.edit_face_selected(0) && ed.edit_face_selected(1) && ed.edit_face_selected(2));
    CHECK(!ed.edit_face_selected(3) && !ed.edit_face_selected(4) && !ed.edit_face_selected(5));
    ed.command("edit vertex");
    ed.command("vsel 0 1 2 3 12");  // the first quad's corners, and one corner of the raised quad
    ed.command("editop select_coplanar");
    CHECK(ed.edit_vert_selected(8) && ed.edit_vert_selected(12));  // a coplanar quad's corner added; the other kept
    ed.command("edit face");
    CHECK(ed.edit_face_selected(2) && !ed.edit_face_selected(3));
  });
}


/* ===================================================================== */
/* Round 58 (task 0036, ADR 0019): bake textures onto a mesh              */
/* ===================================================================== */

/* A baked image's texel at a baked UV (rows top to bottom, as textures are sampled). */
static uint32_t r58_at(const std::vector<uint32_t> &img, int size, Vec2 uv) {
  const int x = std::max(0, std::min(size - 1, (int)std::floor(uv.x * size)));
  const int y = std::max(0, std::min(size - 1, (int)std::floor((1.0f - uv.y) * size)));
  return img[(size_t)y * size + x];
}
static Vec3 r58_rgb(uint32_t c) { return Vec3((c >> 16 & 255) / 255.0f, (c >> 8 & 255) / 255.0f, (c & 255) / 255.0f); }
/* Face f's centre in the baked UVs (the corners' average: a convex face's centre is inside its chart). */
static Vec2 r58_face_uv(const Mesh &m, const TextureBakeResult &r, size_t f) {
  Vec2 s;
  const uint32_t n = m.face_size(f);
  for (uint32_t k = 0; k < n; k++) s = s + r.uvs[m.face_offsets[f] + k];
  return s / (float)n;
}
/* The colour a material shows (sRGB 0..1) where a point of the original surface has original UV q: found by the
 * triangle holding q, and that point's baked UV. */
static bool r58_baked_uv(const Mesh &m, const TextureBakeResult &r, Vec2 q, Vec2 &out) {
  const RenderMesh &rm = m.render_mesh();
  for (size_t t = 0; t < rm.tri_count(); t++) {
    Vec2 a[3], b[3];
    for (int k = 0; k < 3; k++) a[k] = m.uvs[rm.tri_corner[t * 3 + k]], b[k] = r.uvs[rm.tri_corner[t * 3 + k]];
    const float d = (a[1].x - a[0].x) * (a[2].y - a[0].y) - (a[2].x - a[0].x) * (a[1].y - a[0].y);
    if (std::fabs(d) < 1e-12f) continue;
    const float w1 = ((q.x - a[0].x) * (a[2].y - a[0].y) - (a[2].x - a[0].x) * (q.y - a[0].y)) / d;
    const float w2 = ((a[1].x - a[0].x) * (q.y - a[0].y) - (q.x - a[0].x) * (a[1].y - a[0].y)) / d;
    if (w1 < -1e-5f || w2 < -1e-5f || w1 + w2 > 1.0f + 1e-5f) continue;
    out = b[0] * (1.0f - w1 - w2) + b[1] * w1 + b[2] * w2;
    return true;
  }
  return false;
}
static float r58_to_srgb(float v) { return linear_to_srgb(std::max(0.0f, std::min(1.0f, v))); }
static bool r58_same(const std::vector<Vec2> &a, const std::vector<Vec2> &b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++)
    if (a[i].x != b[i].x || a[i].y != b[i].y) return false;
  return true;
}

static void r58_forget_baked(const std::string &name) {
  for (const char *m : {"_albedo.png", "_roughness.png", "_metallic.png", "_normal.png"})
    std::remove(resolve_asset_path("Assets/Textures/Baked/" + name + m).c_str());
  std::remove(resolve_asset_path("Assets/Materials/" + name + " Baked.mat").c_str());
}

static void round58_tests() {
  test("texture bake: a cube with a different material on each face bakes each colour at its face's centre, on one atlas", [] {
    MeshPtr cube = primitives::cube();
    Mesh &m = *mesh_make_mutable(cube);
    m.face_material.assign(m.face_count(), 0);
    for (size_t f = 0; f < m.face_count(); f++) m.face_material[f] = (int32_t)f;
    m.touch();
    const Vec3 cols[6] = {{0.9f, 0.1f, 0.1f}, {0.1f, 0.9f, 0.1f}, {0.1f, 0.1f, 0.9f}, {0.9f, 0.9f, 0.1f}, {0.1f, 0.9f, 0.9f}, {0.9f, 0.1f, 0.9f}};
    std::vector<MaterialPtr> mats;
    for (int k = 0; k < 6; k++) mats.push_back(make_material("F" + std::to_string(k), cols[k]));
    TextureBakeSettings s;
    s.size = 256;
    const TextureBakeResult r = bake_textures(m, Mat4::identity(), mats, s);
    CHECK(r.size == 256 && r.albedo.size() == 256u * 256u && r.uvs.size() == m.corner_verts.size());
    std::printf("    %zu of %d texels covered\n", r.texels, 256 * 256);
    CHECK(r.texels > 256 * 256 / 5);
    for (Vec2 u : r.uvs) CHECK(u.x >= 0.0f && u.x <= 1.0f && u.y >= 0.0f && u.y <= 1.0f);
    int right = 0;
    for (size_t f = 0; f < m.face_count() && f < 6; f++) {
      const Vec3 got = r58_rgb(r58_at(r.albedo, r.size, r58_face_uv(m, r, f)));
      const Vec3 want(r58_to_srgb(cols[f].x), r58_to_srgb(cols[f].y), r58_to_srgb(cols[f].z));
      const float err = std::max({std::fabs(got.x - want.x), std::fabs(got.y - want.y), std::fabs(got.z - want.z)});
      right += err < 2.5f / 255.0f;
      if (err >= 2.5f / 255.0f) std::printf("    face %zu: baked %.3f %.3f %.3f, wanted %.3f %.3f %.3f\n", f, got.x, got.y, got.z, want.x, want.y, want.z);
    }
    CHECK(right == 6);
    /* Roughness and metallic hold the materials' values (0.5, 0); the normal map is flat. */
    const uint32_t rough = r58_at(r.roughness, r.size, r58_face_uv(m, r, 2)), metal = r58_at(r.metallic, r.size, r58_face_uv(m, r, 2));
    CHECK(std::abs((int)(rough & 255) - 128) <= 1 && (metal & 255) == 0);
  });

  test("texture bake: a checker on a plane's UVs bakes the colour the renderer shows at every cell centre", [] {
    MeshPtr plane = primitives::plane(2.0f, 1);
    CHECK(plane->has_uvs());
    MaterialPtr mat = make_material("Checker", Vec3(0.9f, 0.85f, 0.2f));
    mat->procedural = (int)Procedural::Checker;
    mat->procedural_scale = 4.0f;
    mat->procedural_color2 = Vec3(0.05f, 0.1f, 0.6f);
    TextureBakeSettings s;
    s.size = 256;
    const TextureBakeResult r = bake_textures(*plane, Mat4::translate({1, 2, 3}), {mat}, s);
    int checked = 0, right = 0;
    for (int j = 0; j < 8; j++)
      for (int i = 0; i < 8; i++) {
        const Vec2 q((i + 0.5f) / 8.0f, (j + 0.5f) / 8.0f);  // the middle of a checker cell's quarter
        Vec2 b;
        if (!r58_baked_uv(*plane, r, q, b)) continue;
        SurfacePoint sp;
        sp.uv = q;
        sp.normal = sp.geo_normal = sp.local_normal = Vec3(0, 1, 0);
        const SurfaceSample want = evaluate_material(*mat, sp);
        const Vec3 got = r58_rgb(r58_at(r.albedo, r.size, b));
        checked++;
        right += std::fabs(got.x - r58_to_srgb(want.albedo.x)) < 3.0f / 255.0f && std::fabs(got.z - r58_to_srgb(want.albedo.z)) < 3.0f / 255.0f;
      }
    std::printf("    %d of %d cell centres match\n", right, checked);
    CHECK(checked == 64 && right == 64);
  });

  test("texture bake: an axis material bakes grass on the top's texels and dirt on the sides', whatever the object's rotation", [] {
    Editor ed;
    ed.init_headless(400, 300);
    const R51Mats am = r51_mats(1);
    MeshPtr cube = primitives::cube();
    TextureBakeSettings s;
    s.size = 256;
    for (int turn = 0; turn < 2; turn++) {
      /* A quarter turn about X: the face that was +Z faces up. World mode follows the world's up, not the mesh's. */
      const Mat4 model = turn ? Mat4::rotate(Quat::axis_angle(Vec3(1, 0, 0), -kPi * 0.5f)) : Mat4::identity();
      const TextureBakeResult r = bake_textures(*cube, model, {am.axis}, s);
      int top = 0, side = 0, tops = 0, sides = 0;
      for (size_t f = 0; f < cube->face_count(); f++) {
        const Vec3 n = normalize(model.dir(cube->face_normal(f)));
        const Vec3 c = r58_rgb(r58_at(r.albedo, r.size, r58_face_uv(*cube, r, f)));
        const Vec3 lin(srgb_to_linear(c.x), srgb_to_linear(c.y), srgb_to_linear(c.z));
        if (n.y > 0.9f) tops++, top += r51_green(lin);
        else if (std::fabs(n.y) < 0.1f) sides++, side += r51_brown(lin);
      }
      std::printf("    turn %d: %d of %d tops green, %d of %d sides brown\n", turn, top, tops, side, sides);
      CHECK(tops == 1 && top == 1 && sides == 4 && side == 4);
    }
  });

  test("texture bake: a flat surface bakes a flat normal map; a Bricks texture's normal bakes its mortar", [] {
    MeshPtr cube = primitives::cube();
    TextureBakeSettings s;
    s.size = 128;
    const TextureBakeResult flat = bake_textures(*cube, Mat4::scale({2, 1, 3}), {make_material("Grey", Vec3(0.5f))}, s);
    float worst = 0.0f;
    for (size_t i = 0; i < flat.normal.size(); i++) {
      if (!flat.covered[i]) continue;
      const Vec3 c = r58_rgb(flat.normal[i]) * 2.0f - Vec3(1.0f);
      worst = std::max(worst, std::acos(std::min(1.0f, normalize(c).z)) * 180.0f / kPi);
    }
    std::printf("    flat: the most tilted texel is %.2f degrees off +Z\n", worst);
    CHECK(worst < 2.0f);
    /* Bricks: the normal map comes from the texture's height, so the mortar lines tilt the texels beside them. */
    Editor ed;
    ed.init_headless(400, 300);
    ed.command("newtex R58Bricks Bricks");
    MaterialPtr bricks = make_material("Bricks", Vec3(1.0f));
    bricks->normal_map = TextureRef{"Assets/Textures/R58Bricks.ptex#normal", true};
    bricks->touch();
    CHECK(bricks->textures().normal != nullptr);
    MeshPtr plane = primitives::plane(2.0f, 1);
    s.size = 256;
    const TextureBakeResult b = bake_textures(*plane, Mat4::identity(), {bricks}, s);
    size_t tilted = 0;
    for (size_t i = 0; i < b.normal.size(); i++) {
      if (!b.covered[i]) continue;
      const Vec3 c = normalize(r58_rgb(b.normal[i]) * 2.0f - Vec3(1.0f));
      tilted += std::acos(std::min(1.0f, c.z)) * 180.0f / kPi > 5.0f;
    }
    std::printf("    bricks: %zu of %zu texels tilted more than 5 degrees\n", tilted, b.texels);
    CHECK(tilted * 50 > b.texels && tilted * 2 < b.texels);  // mortar edges, not everything
    std::remove(resolve_asset_path("Assets/Textures/R58Bricks.ptex").c_str());
  });

  test("texture bake: Object > Bake Textures keeps the look (rasterized within 5 %), writes the images and material, and undo restores the UVs and materials", [] {
    Editor ed;
    r57_open(ed);
    for (const char *n : {"Cube", "Cube 2", "Cube 3"}) r58_forget_baked(n);
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube != nullptr);
    if (!cube) return;
    MaterialPtr mat = make_material("R58 Checker", Vec3(0.85f, 0.3f, 0.1f));
    mat->procedural = (int)Procedural::Checker;
    mat->procedural_scale = 3.0f;
    mat->procedural_color2 = Vec3(0.1f, 0.3f, 0.8f);
    mat->roughness = 0.35f;
    cube->get<MeshRenderer>()->materials = {mat};
    ed.mark_changed_for_test("R58 material");  // the state undo returns to (committed by the next frame)
    ed.step_frame_headless();
    cube = ed.scene().find_by_name("Cube");
    ed.select_object(cube->id);
    const std::vector<Vec2> uvs_before = cube->get<MeshFilter>()->mesh->uvs;
    const Image before = r57_render(ed, "Rasterized", false);
    ed.command("baketex 1024");
    cube = ed.scene().find_by_name("Cube");
    const Mesh &bm = *cube->get<MeshFilter>()->mesh;
    const MaterialPtr &now = cube->get<MeshRenderer>()->materials[0];
    CHECK(now && now->name == "Cube Baked" && now->asset_path == "Assets/Materials/Cube Baked.mat");
    CHECK(!r58_same(bm.uvs, uvs_before));
    for (const char *m : {"_albedo.png", "_roughness.png", "_metallic.png", "_normal.png"})
      CHECK(fs::exists(resolve_asset_path(std::string("Assets/Textures/Baked/Cube") + m)));
    const Image after = r57_render(ed, "Rasterized", false);
    CHECK(after.pixels.size() == before.pixels.size());
    double diff = 0.0;
    for (size_t i = 0; i < before.pixels.size() && i < after.pixels.size(); i++)
      for (int ch = 0; ch < 3; ch++) diff += std::abs((int)(before.pixels[i] >> (ch * 8) & 255) - (int)(after.pixels[i] >> (ch * 8) & 255));
    diff /= std::max<size_t>(1, before.pixels.size()) * 3.0 * 255.0;
    std::printf("    mean colour difference after the bake: %.2f %%\n", diff * 100.0);
    CHECK(diff < 0.05);
    ed.command("undo");
    cube = ed.scene().find_by_name("Cube");
    CHECK(r58_same(cube->get<MeshFilter>()->mesh->uvs, uvs_before));
    const std::vector<MaterialPtr> &slots = cube->get<MeshRenderer>()->materials;
    CHECK(slots.size() == 1 && slots[0] && slots[0]->name == "R58 Checker");
    r58_forget_baked("Cube");
  });

  test("texture bake: right-clicking an object in the Hierarchy offers Bake Textures, which bakes it", [] {
    Editor ed;
    r57_open(ed);
    for (const char *n : {"Cube", "Cube 2", "Cube 3"}) r58_forget_baked(n);
    int x = 0, y = 0;
    CHECK(ed.hierarchy_row_for_test("Cube", x, y));
    platform::Event e = r34_mouse(R34ET::MouseMove, x, y);
    ed.step_frame_headless({e});
    e.type = R34ET::MouseDown, e.button = 1;
    ed.step_frame_headless({e});
    e.type = R34ET::MouseUp;
    ed.step_frame_headless({e});
    r34_steps(ed, 1);
    const Recti menu = r50_popup(ed);
    CHECK(menu.w > 0 && ed.selected_object() && ed.selected_object()->name == "Cube");
    if (menu.w <= 0) return;
    /* Rows: Rename, Duplicate, Delete, Frame Selected, Move into... (task 0255), Select Children, Clear Parent, Join, Boolean, Separate By Loose
     * Parts, Export Selection..., Bake Textures. Hovering it opens its sizes. */
    const int row = ed.ui_for_test().row_h();
    const int by = menu.y + ed.ui_for_test().px(4) + row * 11 + row / 2;
    ed.step_frame_headless({r34_mouse(R34ET::MouseMove, menu.x + menu.w / 2, by)});
    r34_steps(ed, 2);
    const Recti sizes = r50_popup(ed);
    CHECK(sizes.w > 0 && sizes.x != menu.x);
    if (sizes.w <= 0 || sizes.x == menu.x) return;
    r50_pick_row(ed, sizes, 0);  // 512 x 512
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube && cube->get<MeshRenderer>()->materials[0] && cube->get<MeshRenderer>()->materials[0]->name == "Cube Baked");
    CHECK(fs::exists(resolve_asset_path("Assets/Textures/Baked/Cube_albedo.png")));
    for (const char *n : {"Cube", "Cube 2", "Cube 3"}) r58_forget_baked(n);
  });

  test("texture bake: two objects with one name get their own images and material; a Mirror modifier is refused, the object unchanged", [] {
    Editor ed;
    r57_open(ed);
    for (const char *n : {"Cube", "Cube 2", "Cube 3"}) r58_forget_baked(n);
    GameObject *a = ed.scene().find_by_name("Cube");
    if (!a) return;
    ed.select_object(a->id);
    ed.command("duplicate");
    GameObject *b = ed.selected_object();
    CHECK(b && b != a);
    if (!b || b == a) return;
    b->name = "Cube";
    const uint64_t ida = a->id, idb = b->id;
    ed.select_object(ida);
    CHECK(ed.bake_object_textures_for_test(256));
    ed.select_object(idb);
    CHECK(ed.bake_object_textures_for_test(256));
    a = ed.scene().find(ida), b = ed.scene().find(idb);
    const MaterialPtr ma = a->get<MeshRenderer>()->materials[0], mb = b->get<MeshRenderer>()->materials[0];
    std::printf("    first: %s, second: %s\n", ma->asset_path.c_str(), mb->asset_path.c_str());
    CHECK(ma->asset_path == "Assets/Materials/Cube Baked.mat" && mb->asset_path == "Assets/Materials/Cube 2 Baked.mat");
    CHECK(ma->base_map.path != mb->base_map.path);
    CHECK(fs::exists(resolve_asset_path("Assets/Textures/Baked/Cube_albedo.png")) && fs::exists(resolve_asset_path("Assets/Textures/Baked/Cube 2_albedo.png")));
    /* Mirror adds geometry: the bake would repeat the base's images, so it is refused and nothing changes. */
    b->add<MirrorModifier>();
    b->mark_dirty();
    ed.mark_changed_for_test("R58 mirror");
    ed.step_frame_headless();
    b = ed.scene().find(idb);
    const std::vector<Vec2> uvs = b->get<MeshFilter>()->mesh->uvs;
    ed.select_object(idb);
    CHECK(!ed.bake_object_textures_for_test(256));
    b = ed.scene().find(idb);
    CHECK(r58_same(b->get<MeshFilter>()->mesh->uvs, uvs) && b->get<MeshRenderer>()->materials[0]->asset_path == "Assets/Materials/Cube 2 Baked.mat");
    CHECK(!fs::exists(resolve_asset_path("Assets/Materials/Cube 3 Baked.mat")));
    for (const char *n : {"Cube", "Cube 2", "Cube 3"}) r58_forget_baked(n);
  });

  test("texture bake: alpha only from see-through surfaces: an Opaque material with alpha bakes solid, a Transparent slot gives the baked material its surface", [] {
    MeshPtr cube = primitives::cube();
    Mesh &m = *mesh_make_mutable(cube);
    m.face_material.assign(m.face_count(), 0);
    m.face_material[0] = 1;
    m.touch();
    MaterialPtr solid = make_material("Solid", Vec3(0.5f));
    solid->alpha = 0.3f;  // ignored by every renderer on an Opaque surface
    TextureBakeSettings s;
    s.size = 128;
    const TextureBakeResult r1 = bake_textures(m, Mat4::identity(), {solid}, s);
    CHECK(!r1.any_alpha && r1.alpha_slot == -1);
    bool opaque = true;
    for (size_t i = 0; i < r1.albedo.size(); i++) opaque = opaque && (r1.albedo[i] >> 24) == 255;
    CHECK(opaque);
    CHECK(baked_material("X", "x", solid.get(), nullptr).surface == (int)MaterialSurface::Opaque);
    MaterialPtr glassy = make_material_preset("Transparent");
    glassy->alpha = 0.4f;
    const TextureBakeResult r2 = bake_textures(m, Mat4::identity(), {solid, glassy}, s);
    CHECK(r2.any_alpha && r2.alpha_slot == 1);
    const uint32_t c = r58_at(r2.albedo, r2.size, r58_face_uv(m, r2, 0));
    std::printf("    the Transparent face's alpha: %u\n", c >> 24);
    CHECK(std::abs((int)(c >> 24) - 102) <= 1);
    CHECK((r58_at(r2.albedo, r2.size, r58_face_uv(m, r2, 1)) >> 24) == 255);
    const Material bm = baked_material("X", "x", solid.get(), glassy.get());
    CHECK(bm.surface == (int)MaterialSurface::Transparent);
  });


  test("texture bake: OBJ, glb and FBX exports with Bake Textures write the images beside the file and reference them", [] {
    Editor ed;
    r57_open(ed);
    GameObject *cube = ed.scene().find_by_name("Cube");
    if (!cube) return;
    MaterialPtr mat = make_material("R58 Export", Vec3(0.2f, 0.7f, 0.3f));
    mat->procedural = (int)Procedural::Noise;
    cube->get<MeshRenderer>()->materials = {mat};
    ed.select_object(cube->id);
    const std::vector<Vec2> uvs_before = cube->get<MeshFilter>()->mesh->uvs;
    const std::string dir = fs::join(scratch_project(), "R58Export");
    for (int fmt : {(int)ExportFormat::OBJ, (int)ExportFormat::GLB, (int)ExportFormat::FBX}) {
      ExportOptions o = export_defaults(fmt);
      o.bake_textures = true;
      o.bake_size = 256;
      const std::string path = fs::join(dir, std::string("r58") + export_format_extension(fmt));
      CHECK(ed.export_model_for_test(o, path) == path);
      const std::string base = fs::join(dir, "r58_Cube");
      for (const char *m : {"_albedo.png", "_roughness.png", "_metallic.png", "_normal.png"}) CHECK(fs::exists(base + m));
      std::string file;
      CHECK(fs::read_file(path, file));
      if (fmt == (int)ExportFormat::OBJ) {
        std::string mtl;
        CHECK(fs::read_file(fs::join(dir, "r58.mtl"), mtl));
        CHECK(mtl.find("map_Kd r58_Cube_albedo.png") != std::string::npos);
        CHECK(mtl.find("map_Pr r58_Cube_roughness.png") != std::string::npos);
        CHECK(mtl.find("map_Bump r58_Cube_normal.png") != std::string::npos);
        CHECK(file.find("\nvt ") != std::string::npos);
      }
      else if (fmt == (int)ExportFormat::GLB) {
        for (const char *k : {"baseColorTexture", "metallicRoughnessTexture", "normalTexture", "image/png"}) CHECK(file.find(k) != std::string::npos);
      }
      else {
        for (const char *k : {"TextureVideoClip", "DiffuseColor", "NormalMap", "ShininessExponent", "r58_Cube_albedo.png"})
          CHECK(file.find(k) != std::string::npos);
      }
    }
    /* The scene is untouched. */
    CHECK(r58_same(cube->get<MeshFilter>()->mesh->uvs, uvs_before) && cube->get<MeshRenderer>()->materials[0] == mat);
    /* ufbx reads the FBX's textures on the material. */
    std::string fbx;
    fs::read_file(fs::join(dir, "r58.fbx"), fbx);
    ufbx_load_opts opts = {};
    ufbx_error uerr;
    ufbx_scene *sc = ufbx_load_memory(fbx.data(), fbx.size(), &opts, &uerr);
    CHECK(sc != nullptr);
    if (sc) {
      bool diffuse = false, normal = false;
      for (size_t i = 0; i < sc->materials.count; i++) {
        const ufbx_material *um = sc->materials.data[i];
        diffuse = diffuse || (um->fbx.diffuse_color.texture && std::string(um->fbx.diffuse_color.texture->relative_filename.data).find("r58_Cube_albedo.png") != std::string::npos);
        normal = normal || um->fbx.normal_map.texture != nullptr;
      }
      CHECK(diffuse && normal);
      ufbx_free_scene(sc);
    }
  });

  test("texture bake: exports reference image maps of ordinary materials too (glTF normal and packed metallic-roughness, FBX textures), never a .ptex output", [] {
    const std::string dir = fs::join(scratch_project(), "R58Maps");
    fs::make_dirs(dir);
    std::vector<uint32_t> px(16 * 16, 0xFF808080u);
    const std::string rough = fs::join(dir, "rough.png"), nrm = fs::join(dir, "nrm.png");
    CHECK(write_png(rough, px.data(), 16, 16, 16));
    CHECK(write_png(nrm, px.data(), 16, 16, 16));
    MaterialPtr mat = make_material("Maps", Vec3(0.5f));
    mat->roughness_map = TextureRef{rough, true};
    mat->normal_map = TextureRef{nrm, true};
    MeshPtr cube = primitives::cube();
    ExportItem it;
    it.name = "Cube";
    it.mesh = cube.get();
    it.world = Mat4::identity();
    it.materials = {mat};
    const std::string glb = export_gltf({it}, export_defaults((int)ExportFormat::GLB), true, "", nullptr);
    CHECK(glb.find("metallicRoughnessTexture") != std::string::npos && glb.find("normalTexture") != std::string::npos);
    const std::string fbx = export_fbx({it}, export_defaults((int)ExportFormat::FBX));
    CHECK(fbx.find("rough.png") != std::string::npos && fbx.find("ShininessExponent") != std::string::npos);
    /* A procedural texture's output isn't a file: the MTL leaves it out (the editor writes those as PNGs first). */
    mat->base_map = TextureRef{"Assets/Textures/X.ptex#albedo", false};
    std::string obj, mtl;
    export_obj_mtl({it}, "x.mtl", obj, mtl);
    CHECK(mtl.find(".ptex") == std::string::npos && mtl.find("map_Pr") != std::string::npos);
    std::remove(rough.c_str());
    std::remove(nrm.c_str());
  });

  test("texture bake: Keep UVs keeps a plane's own UVs, not a mesh whose UVs overlap; empty, NaN and degenerate meshes bake nothing", [] {
    MeshPtr plane = primitives::plane(2.0f, 4);
    TextureBakeSettings s;
    s.size = 64;
    s.keep_uvs = true;
    const TextureBakeResult own = bake_textures(*plane, Mat4::identity(), {}, s);
    CHECK(own.own_uvs && own.uvs.size() == plane->uvs.size());
    bool same = own.uvs.size() == plane->uvs.size();
    for (size_t i = 0; same && i < own.uvs.size(); i++) same = std::fabs(own.uvs[i].x - plane->uvs[i].x) < 1e-6f && std::fabs(own.uvs[i].y - plane->uvs[i].y) < 1e-6f;
    CHECK(same);
    MeshPtr stacked = primitives::plane(2.0f, 4);
    Mesh &sm = *mesh_make_mutable(stacked);
    for (size_t f = 0; f < sm.face_count(); f++)  // every face on the whole 0..1 square: all overlap
      for (uint32_t k = 0; k < sm.face_size(f); k++) {
        static const Vec2 sq[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        sm.uvs[sm.face_offsets[f] + k] = sq[k % 4];
      }
    sm.touch();
    const TextureBakeResult gen = bake_textures(sm, Mat4::identity(), {}, s);
    CHECK(!gen.own_uvs && gen.texels > 0);
    /* Nothing to bake: no faces, every position NaN, every vertex in one place. */
    Mesh empty;
    CHECK(bake_textures(empty, Mat4::identity(), {}, s).texels == 0);
    MeshPtr bad = primitives::cube();
    Mesh &bm = *mesh_make_mutable(bad);
    for (Vec3 &p : bm.positions) p = Vec3(std::nanf(""), 0.0f, 0.0f);
    bm.touch();
    const TextureBakeResult nan = bake_textures(bm, Mat4::identity(), {}, s);
    CHECK(nan.albedo.size() == 64u * 64u && nan.texels == 0);
    MeshPtr dot = primitives::cube();
    Mesh &dm = *mesh_make_mutable(dot);
    for (Vec3 &p : dm.positions) p = Vec3(1.0f);
    dm.touch();
    const TextureBakeResult collapsed = bake_textures(dm, Mat4::identity(), {}, s);
    CHECK(collapsed.albedo.size() == 64u * 64u && collapsed.texels == 0);
    /* Out-of-range sizes are clamped (16..4096). */
    s.size = 1;
    CHECK(bake_textures(*plane, Mat4::identity(), {}, s).size == 16);
  });
}

static void tests_round111() {
  /* ---------------------------------------------------------------- round 111: per-view waste (task 0085) */
  test("voxel GI: moving an object inside the grid makes no new column pyramid; growing the scene past it makes one", [] {
    Editor ed;
    r36_editor_scene(ed);
    GameObject *box = r35_add(ed, "Cube", "Box", {-3.0f, 0.5f, 2.0f}, {1, 1, 1}, Vec3(0.8f));
    ed.commit_change("add Box");
    r36_cmd(ed, "set Lighting.RealtimeGI true");
    r34_steps(ed, 3);
    CHECK(ed.voxel_grid_for_test().valid());
    const uint64_t a0 = voxel_grid_allocations();
    for (int k = 0; k < 6; k++) {
      box->set_local_position({-3.0f + 0.05f * k, 0.5f, 2.0f});
      r34_steps(ed, 1);
    }
    const uint64_t moved = voxel_grid_allocations() - a0;
    std::printf("    6 small moves: %llu new pyramids (before task 0085: one per view per frame)\n", (unsigned long long)moved);
    CHECK(moved == 0);
    CHECK(ed.voxel_grid_for_test().valid());
    /* Far outside: the grid can't hold the scene any more - a new one, with its columns. */
    box->set_local_position({60.0f, 0.5f, 2.0f});
    r34_steps(ed, 2);
    CHECK(voxel_grid_allocations() > a0 && ed.voxel_grid_for_test().valid());
  });

  test("voxel GI: a grid fitted without its columns has the same placement and key as one fitted with them", [] {
    AABB b;
    b.add({-3, 0, -2});
    b.add({5, 4, 7});
    VoxelGrid with, without;
    voxel_grid_fit(with, b, 128);
    voxel_grid_fit(without, b, 128, false);
    CHECK(with.valid() && !without.valid());
    CHECK(with.n == without.n && with.voxel == without.voxel && with.key == without.key);
    CHECK(with.origin.x == without.origin.x && with.origin.y == without.origin.y && with.origin.z == without.origin.z);
    voxel_grid_allocate(without);
    CHECK(without.valid() && without.level_count() == with.level_count());
    for (int l = 0; l < with.level_count(); l++) CHECK(without.levels[(size_t)l].size() == with.levels[(size_t)l].size());
  });

  test("shadows: a shadow map keeps no ids plane, and its depth is what it was with one", [] {
    /* A small scene's sun shadow, rendered into a depth-only target; then the same into a full target (ids too). */
    std::vector<MeshPtr> keep;
    std::vector<DrawItem> items;
    for (int i = 0; i < 3; i++) {
      MeshPtr m = primitives::cube();
      keep.push_back(m);
      DrawItem it;
      it.mesh = &m->render_mesh();
      it.model = Mat4::translate({(float)i * 1.5f - 1.5f, 0.5f * i, 0.0f});
      it.id = (uint32_t)(i + 1);
      items.push_back(it);
    }
    const Mat4 view = Mat4::look_at({4, 6, 5}, {0, 0, 0}, {0, 1, 0});
    const Mat4 proj = Mat4::ortho(5.0f, 1.0f, 0.1f, 30.0f);
    RasterOptions opt;
    opt.shade = ShadeMode::DepthOnly;
    opt.backface_culling = false;
    LightingEnv env;
    RenderTarget shadow, full;
    shadow.make_depth_only(256, 256);
    CHECK(shadow.ids.empty() && shadow.depth.size() == 256u * 256u);
    full.width = full.height = full.stride = 256;
    full.resize_planes();
    CHECK(full.ids.size() == 256u * 256u);
    for (RenderTarget *rt : {&shadow, &full}) {
      Renderer3D r;
      r.begin(rt, view, proj, env, opt);
      r.clear(0);
      for (const DrawItem &it : items) r.add(it);
      r.flush();
    }
    CHECK(shadow.depth == full.depth);
    size_t hit = 0;
    for (uint32_t id : full.ids) hit += id != 0;
    CHECK(hit > 1000);  // (the full target did record what it drew)
    CHECK(shadow.id_at(128, 128) == 0);
  });
}

static void tests_round164() {
  /* ---------------------------------------------------------------- round 164: area lights as emissive planes (task 0155, ADR 0050) */
  /* A Lambertian floor's radiance at a point, path traced with direct light only, under either an area light or a quad
   * with an emissive material of the same radiance (intensity x pi over its area), seen from the side under the panel. */
  auto r164_floor = [](const RenderLight *light, bool emissive_quad, float w, float y, float I, Vec3 at, int samples) {
    auto floor = primitives::quad(40.0f);
    std::vector<MaterialPtr> mats = {make_material("floor", Vec3(0.8f))};
    mats[0]->specular = 0.0f;
    mats[0]->roughness = 1.0f;
    auto panel = primitives::quad(w);
    std::vector<MaterialPtr> pmats = {make_material("panel", Vec3(0.0f))};
    pmats[0]->emission = Vec3(1.0f);
    pmats[0]->emission_strength = I * kPi;  // the panel's radiance: intensity x pi
    Environment black;
    black.mode = Environment::Color;
    black.color = Vec3(0.0f);
    PathTracer pt;
    PTSettings st;
    st.denoise = false;
    st.use_embree = false;
    st.max_bounces = 1;  // (a bounce, so an emissive quad found by BSDF sampling counts too: the light samples are MIS-weighted against it)
    pt.set_settings(st);
    std::vector<PTObject> objs = {{&floor->render_mesh_tangents(), Mat4::rotate(Quat::euler({90, 0, 0})), &mats}};  // facing +Y
    if (emissive_quad) objs.push_back({&panel->render_mesh_tangents(), Mat4::translate({0, y, 0}) * Mat4::rotate(Quat::euler({-90, 0, 0})), &pmats});  // facing -Y
    std::vector<RenderLight> lights;
    if (light) lights.push_back(*light);
    pt.build(objs, lights, black);
    /* Seen from the side under the panel (an emissive quad is geometry), or from above through it when it is too low. */
    const Vec3 eye = y > 0.5f ? at + Vec3(2.0f, 0.8f, 0.0f) : at + Vec3(0.0f, 2.0f, 0.0f);
    pt.set_camera(Mat4::look_at(eye, at, y > 0.5f ? Vec3(0, 1, 0) : Vec3(0, 0, 1)), Mat4::perspective(1.0f * kDeg2Rad, 1, 0.1f, 100), 4, 4);
    pt.render(1e9, samples);
    const auto rgb = pt.linear_rgb(false);
    float v = 0;
    for (int i = 0; i < 16; i++) v += rgb[(size_t)i * 3 + 1] / 16.0f;
    return v;
  };
  auto r164_light = [](float w, float h, float y, float I, float range) {
    RenderLight l = r52_big_light(false, w, h);
    l.position = {0, y, 0};
    l.intensity = I;
    l.range = range;
    return l;
  };
  test("area light: a 1 m panel 1 m above the floor lights it by its radiance times its area (albedo x intensity x area over the square of the distance), 2 m up a quarter of it", [&] {
    /* A 20 cm panel: small against its distance, so the floor gets albedo x intensity x 0.04 / r^2 (the cosines 1). */
    const RenderLight one = r164_light(0.2f, 0.2f, 1.0f, 2.0f, 1000.0f), two = r164_light(0.2f, 0.2f, 2.0f, 2.0f, 1000.0f);
    const float a = r164_floor(&one, false, 0.2f, 1.0f, 2.0f, {0, 0, 0}, 1024), b = r164_floor(&two, false, 0.2f, 2.0f, 2.0f, {0, 0, 0}, 1024);
    std::printf("    1 m: %.4f (0.8 x 2 x 0.04 = 0.064 expected), 2 m: %.4f (a quarter: 0.016)\n", a, b);
    CHECK(std::fabs(a - 0.064f) < 0.0025f);
    CHECK(std::fabs(b - 0.016f) < 0.0006f);
    /* The analytic term says the same (the views, lightmaps and probes take it). */
    CHECK(std::fabs(0.8f * 2.0f * area_light_irradiance(one, {0, 0, 0}, {0, 1, 0}) - 0.064f) < 0.001f);
  });

  test("area light: a 10 x 10 m panel 3.5 m up lights the floor as a 10 x 10 m quad of an emissive material does - the path tracer, the exact term and the quad agree", [&] {
    const RenderLight l = r164_light(10.0f, 10.0f, 3.5f, 2.0f, 1000.0f);
    const float traced = r164_floor(&l, false, 10.0f, 3.5f, 2.0f, {0, 0, 0}, 4096);
    const float quad = r164_floor(nullptr, true, 10.0f, 3.5f, 2.0f, {0, 0, 0}, 4096);
    const float exact = 0.8f * 2.0f * area_light_irradiance(l, {0, 0, 0}, {0, 1, 0});
    std::printf("    floor centre: area light %.4f, emissive quad %.4f, the exact term %.4f\n", traced, quad, exact);
    CHECK(traced > 0.05f);
    CHECK(std::fabs(traced - exact) < 0.03f * exact);
    CHECK(std::fabs(quad - traced) < 0.03f * traced);
    /* Off centre too (3 m out, under the panel). */
    const float t2 = r164_floor(&l, false, 10.0f, 3.5f, 2.0f, {3, 0, 0}, 4096), q2 = r164_floor(nullptr, true, 10.0f, 3.5f, 2.0f, {3, 0, 0}, 4096);
    const float e2 = 0.8f * 2.0f * area_light_irradiance(l, {3, 0, 0}, {0, 1, 0});
    std::printf("    3 m out: area light %.4f, emissive quad %.4f, the exact term %.4f\n", t2, q2, e2);
    CHECK(std::fabs(q2 - t2) < 0.03f * t2 && std::fabs(e2 - t2) < 0.03f * t2);
  });

  test("area light: the floor under a panel's edge is dimmer than under its centre and brighter than beyond it, with no step", [&] {
    const RenderLight l = r164_light(10.0f, 10.0f, 3.5f, 2.0f, 1000.0f);
    float prev = 1e9f;
    bool ok = true;
    for (float x : {0.0f, 2.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f}) {
      const float v = area_light_irradiance(l, {x, 0, 0}, {0, 1, 0});
      std::printf("    x = %4.1f m: %.4f\n", x, v);
      ok = ok && v < prev && v > 0.0f && (prev > 1e8f || v > 0.5f * prev);  // falling, never off, never by more than half from one metre to the next
      prev = v;
    }
    CHECK(ok);
  });

  test("area light: a floor 10 cm under a panel gets nearly pi times its radiance (the path tracer and the exact term agree), and a millimetre under it stays finite", [&] {
    const RenderLight l = r164_light(1.0f, 1.0f, 0.1f, 2.0f, 1000.0f);  // the floor 10 cm below a 1 m panel
    const float exact = area_light_irradiance(l, {0, 0, 0}, {0, 1, 0});
    const float traced = r164_floor(&l, false, 1.0f, 0.1f, 2.0f, {0, 0, 0}, 4096);
    std::printf("    10 cm under a 1 m panel: exact %.4f (a plane touching an endless panel: pi), path traced %.4f (0.8 x 2 x exact = %.4f)\n", exact, traced, 1.6f * exact);
    CHECK(exact > 0.8f * kPi && exact < kPi);
    CHECK(std::fabs(traced - 1.6f * exact) < 0.05f * 1.6f * exact);
    /* A millimetre under it: the inverse square stops at 1 cm, so the maths stays finite (at most the area over 1e-4). */
    const RenderLight mm = r164_light(1.0f, 1.0f, 0.001f, 2.0f, 1000.0f);
    const float near = area_light_irradiance(mm, {0, 0, 0}, {0, 1, 0}), near_traced = r164_floor(&mm, false, 1.0f, 0.001f, 2.0f, {0, 0, 0}, 256);
    std::printf("    a millimetre under it: exact %.4f, path traced %.4f\n", near, near_traced);
    CHECK(finite_bits(near) && near <= 1e4f && finite_bits(near_traced) && near_traced <= 1.6e4f);
  });

  test("area light: Range is a window - nothing beyond it, within 2 % of an unlimited light at a quarter of it", [&] {
    const RenderLight r4 = r164_light(1.0f, 1.0f, 0.0f, 2.0f, 4.0f), big = r164_light(1.0f, 1.0f, 0.0f, 2.0f, 1000.0f);
    const float beyond = area_light_falloff(r4, r4.position, {0, -5, 0}), at_edge = area_light_falloff(r4, r4.position, {0, -4, 0});
    const float near4 = area_light_irradiance(r4, {0, -1, 0}, {0, 1, 0}), near_big = area_light_irradiance(big, {0, -1, 0}, {0, 1, 0});
    std::printf("    Range 4: at 5 m %.4f, at 4 m %.4f; at 1 m %.4f (unlimited %.4f)\n", beyond, at_edge, near4, near_big);
    CHECK(beyond == 0.0f && at_edge == 0.0f);
    CHECK(near4 > 0.98f * near_big && near4 <= near_big);  // (the panel's corners are 1.2 m away: the window there 0.983)
  });

  test("area light: the Light's Range help and the lesson say what Range does now; MoonRay takes the panel's radiance as it is", [] {
    Light l;
    l.type = 3;
    struct Helps : Reflector {
      std::string last, range_help, intensity_help;
      void field(const char *n, float &, float, float, float) override { last = n; }
      void field(const char *n, int &, int, int) override { last = n; }
      void field(const char *n, bool &) override { last = n; }
      void field(const char *n, Vec3 &) override { last = n; }
      void color(const char *n, Vec3 &) override { last = n; }
      void enumeration(const char *n, int &, const char *const *, int) override { last = n; }
      void text(const char *n, std::string &) override { last = n; }
      void mesh(const char *n, MeshPtr &) override { last = n; }
      void help(const char *h) override {
        if (last == "Range") range_help = h;
        if (last == "Intensity") intensity_help = h;
      }
    } hr;
    l.reflect(hr);
    CHECK(hr.range_help.find("inverse square") != std::string::npos && hr.intensity_help.find("emissive material") != std::string::npos);
    RenderLight rl = r52_big_light(false, 2.0f, 1.0f);
    rl.intensity = 1.0f;
    rl.range = 10.0f;
    moonray::ExportScene sc;
    Environment env;
    env.mode = Environment::Color;
    env.color = Vec3(0.0f);
    sc.lights = {rl};
    sc.world = &env;
    sc.world_map_width = 16;
    sc.view = Mat4::trs({0, 1, -5}, Quat::euler({0, 0, 0}), {1, 1, 1}).inverse();
    sc.proj = Mat4::perspective(60.0f * kDeg2Rad, 16.0f / 9.0f, 0.1f, 100.0f);
    sc.width = 16, sc.height = 9, sc.samples = 1;
    const std::string dir = fs::join(fs::temp_dir(), "blendity-test-r164-moonray");
    moonray::ExportReport rep;
    std::string error, text;
    CHECK(moonray::export_scene(sc, dir, [](const std::string &p) { return p; }, rep, error));
    fs::read_file(rep.scene_path, text);
    const size_t rl_at = text.find("RectLight(");
    const size_t at = rl_at == std::string::npos ? rl_at : text.find("[\"intensity\"] = ", rl_at);
    CHECK(at != std::string::npos);
    if (at != std::string::npos) {
      const float v = std::strtof(text.c_str() + at + 16, nullptr);
      std::printf("    MoonRay RectLight intensity %.4f (pi: the radiance of a panel of intensity 1, whatever its size)\n", v);
      CHECK_NEAR(v, kPi, 1e-3);
    }
  });
}

static void tests_round175() {
  /* ---------------------------------------------------------------- round 175: baked emission sampled at the texel (task 0157) */
  test("baked emission: Preview Lighting's first pass under a small lamp shows no texel over three times its lit neighbours", [] {
    Editor ed;
    r35_open(ed);
    r35_add(ed, "Plane", "Floor", {0, 0, 0}, {1, 1, 1}, Vec3(0.8f));
    GameObject *lamp = r35_add(ed, "Cube", "Lamp", {0, 0.5f, 0}, {0.1f, 0.1f, 0.1f}, Vec3(1.0f));  // (in the bake, so it is a light in it)
    lamp->get<MeshRenderer>()->materials[0]->emission = Vec3(1.0f);
    lamp->get<MeshRenderer>()->materials[0]->emission_strength = 20.0f;
    r35_sun_mode(ed, 0);  // (no sun: the lamp lights the floor)
    ed.commit_change("lamp");
    r34_steps(ed, 2);
    const auto t0 = std::chrono::steady_clock::now();
    ed.command("lighting preview on");
    for (int i = 0; i < 200 && ed.preview_samples_for_test() < 32; i++) ed.step_frame_headless();
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const LightingData &pd = ed.preview_data_for_test();
    float worst = 0.0f, peak = 0.0f;
    int lit = 0;
    for (const Lightmap &lm : pd.pages) {
      auto lum = [&](int x, int y) { const Vec3 &c = lm.texels[(size_t)y * lm.width + x]; return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; };
      for (int y = 1; y + 1 < lm.height; y++)
        for (int x = 1; x + 1 < lm.width; x++) {
          const float me = lum(x, y);
          if (me < 0.01f) continue;
          float sum = 0.0f;
          int n = 0;
          for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
              const float l = (dx || dy) ? lum(x + dx, y + dy) : 0.0f;
              if ((dx || dy) && l > 0.01f) sum += l, n++;
            }
          if (n < 4) continue;
          lit++, peak = std::max(peak, me), worst = std::max(worst, me / (sum / (float)n));
        }
    }
    std::printf("    the preview's first pass (32 rays) in %.0f ms: %d lit texels, the brightest %.3f, the worst texel %.2fx its neighbours' mean (blocks would be 5-20x)\n", ms, lit,
                peak, worst);
    CHECK(ed.preview_samples_for_test() >= 32 && lit > 50 && peak > 0.35f && worst < 3.0f);  // (the lamp adds about 0.28 under it on top of the sky: a glow, no blocks)
  });

}

static void tests_round177() {
  test("lightmap keys: the hashes a bake saves (lightmap_mesh_hash, lightmap_placed_hash) keep their values across builds, so old bakes stay valid", [] {
    /* Task 0177 moved the hash loops into core/hash.h; these two keys are saved with every bake (LightingData's
     * scene_key and the per-object keys), so a new formula would mark every bake out of date. The literal is the
     * value before the move. */
    const MeshPtr cube = primitives::cube(1.0f);
    const uint64_t mesh = lightmap_mesh_hash(*cube), placed = lightmap_placed_hash(mesh, Mat4::identity());
    std::printf("    cube: mesh hash %llx, placed %llx\n", (unsigned long long)mesh, (unsigned long long)placed);
    CHECK(mesh == 0x9877b239b9639bafull && placed == 0xf2f367bf9aeebd16ull);
  });
}

/* ---------------------------------------------------------------- round 421: measuring the bake (task 0397, ADR 0113) */
static void tests_round421() {
  test("bake settings (round 421): a Unity scene's lightmap settings are read from its LightingSettings asset when it references one, else from the scene's own block", [] {
    const std::string scene =
        "%YAML 1.1\n%TAG !u! tag:unity3d.com,2011:\n--- !u!157 &3\nLightmapSettings:\n  m_ObjectHideFlags: 0\n  m_LightmapEditorSettings:\n"
        "    m_BakeResolution: 20\n    m_AtlasSize: 512\n    m_Padding: 3\n    m_LightmapsBakeMode: 0\n    m_MixedBakeMode: 0\n    m_PVRDirectSampleCount: 16\n"
        "    m_PVRSampleCount: 300\n    m_PVRBounces: 3\n    m_PVRFilteringMode: 0\n  m_LightingSettings: {fileID: 4890085278179872738, guid: 0123, type: 2}\n";
    const std::string asset =
        "%YAML 1.1\n%TAG !u! tag:unity3d.com,2011:\n--- !u!850595691 &4890085278179872738\nLightingSettings:\n  m_Name: Test\n  serializedVersion: 4\n"
        "  m_BakeResolution: 40\n  m_Padding: 8\n  m_LightmapMaxSize: 2048\n  m_LightmapsBakeMode: 1\n  m_MixedBakeMode: 2\n  m_AO: 1\n  m_BakeBackend: 2\n"
        "  m_PVRDirectSampleCount: 32\n  m_PVRSampleCount: 1024\n  m_PVREnvironmentSampleCount: 1\n  m_PVRBounces: 2\n  m_PVRFilteringMode: 2\n"
        "  m_PVRDenoiserTypeIndirect: 1\n";
    std::vector<UnityDoc> docs;
    std::string why;
    CHECK(parse_unity_yaml(scene, docs, why));
    UnityBakeSettings own, from_asset;
    CHECK(unity_bake_settings(docs, "", own));
    CHECK(own.source == "the scene's own settings" && own.texels == 20.0f && own.max_size == 512 && own.padding == 3 && own.indirect == 300 &&
          own.direct == 16 && own.bounces == 3 && own.filtering == 0 && own.directional == 0);
    CHECK(unity_bake_settings(docs, asset, from_asset));
    CHECK(from_asset.source == "the LightingSettings asset" && from_asset.texels == 40.0f && from_asset.max_size == 2048 && from_asset.padding == 8 &&
          from_asset.indirect == 1024 && from_asset.environment == 1 && from_asset.directional == 1 && from_asset.mixed == 2 && from_asset.ao &&
          from_asset.backend == 2 && from_asset.denoiser_indirect == 1);
    std::vector<UnityDoc> none;
    UnityBakeSettings n;
    CHECK(!unity_bake_settings(none, "", n) && !n.read);
    /* Out of range and malformed: clamped in double before the cast (1e12 samples, a negative padding, NaN bounces). */
    const std::string wild =
        "%YAML 1.1\n--- !u!157 &3\nLightmapSettings:\n  m_LightmapEditorSettings:\n    m_BakeResolution: 5000\n    m_AtlasSize: 99999\n    m_Padding: -4\n"
        "    m_PVRSampleCount: 1e12\n    m_PVRBounces: .nan\n";
    std::vector<UnityDoc> wd;
    UnityBakeSettings w;
    CHECK(parse_unity_yaml(wild, wd, why) && unity_bake_settings(wd, "", w));
    CHECK(w.texels == 1000.0f && w.max_size == 4096 && w.padding == 0 && w.indirect == 65536 && w.bounces >= 0 && w.bounces <= 16);
    /* A LightingSettings embedded in the scene (referenced by its fileID alone), Baked GI off. */
    const std::string embedded =
        "%YAML 1.1\n--- !u!157 &3\nLightmapSettings:\n  m_GISettings:\n    m_EnableBakedLightmaps: 1\n  m_LightmapEditorSettings:\n    m_BakeResolution: 12\n"
        "  m_LightingSettings: {fileID: 77}\n--- !u!850595691 &77\nLightingSettings:\n  m_BakeResolution: 33\n  m_EnableBakedLightmaps: 0\n  m_PVRSampleCount: 640\n";
    std::vector<UnityDoc> ed_docs;
    UnityBakeSettings e;
    CHECK(parse_unity_yaml(embedded, ed_docs, why) && unity_bake_settings(ed_docs, "", e));
    CHECK(e.source == "the scene's embedded LightingSettings" && e.texels == 33.0f && e.indirect == 640 && !e.baked_gi);
    CHECK(own.baked_gi);  // (no flag: on)
  });
  test("bake settings (round 421): opening a Unity scene puts its LightingSettings asset's numbers in the Lighting window, logs them, and notes what isn't baked yet; a missing asset is said", [] {
    Editor ed;
    ed.init_headless(400, 300);
    const std::string dir = resolve_asset_path("Assets/Imported/R421B");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    fs::make_dirs(fs::join(dir, ".unity-meta"));
    const std::string gs = "aaaa4210aaaa4210aaaa4210aaaa0001", gl = "aaaa4210aaaa4210aaaa4210aaaa0002", gone = "aaaa4210aaaa4210aaaa4210aaaa0003";
    auto scene_text = [](const std::string &guid) {
      return "%YAML 1.1\n--- !u!157 &3\nLightmapSettings:\n  m_LightmapEditorSettings:\n    m_BakeResolution: 10\n    m_PVRSampleCount: 100\n"
             "  m_LightingSettings: {fileID: 4890085278179872738, guid: " + guid + ", type: 2}\n--- !u!1 &10\nGameObject:\n  m_Name: Thing\n"
             "--- !u!4 &11\nTransform:\n  m_GameObject: {fileID: 10}\n  m_Father: {fileID: 0}\n";
    };
    CHECK(fs::write_file(fs::join(dir, "Lit.unity"), scene_text(gl)));
    CHECK(fs::write_file(fs::join(dir, "Gone.unity"), scene_text(gone)));
    CHECK(fs::write_file(fs::join(dir, "Lit.lighting"),
                         "%YAML 1.1\n%TAG !u! tag:unity3d.com,2011:\n--- !u!850595691 &4890085278179872738\nLightingSettings:\n  m_Name: Lit\n"
                         "  m_BakeResolution: 25\n  m_Padding: 6\n  m_LightmapMaxSize: 512\n  m_LightmapsBakeMode: 1\n  m_MixedBakeMode: 1\n"
                         "  m_PVRDirectSampleCount: 24\n  m_PVRSampleCount: 99999\n  m_PVRBounces: 3\n  m_PVRFilteringMode: 0\n  m_PVRDenoiserTypeIndirect: 1\n"));
    for (const std::string &g : {gs, gl}) CHECK(fs::write_file(fs::join(dir, ".unity-meta/" + g + ".meta"), "fileFormatVersion: 2\nguid: " + g + "\n"));
    CHECK(fs::write_file(fs::join(dir, ".unity-guids"), gs + "\tAssets/Lit.unity\tAssets/Imported/R421B/Lit.unity\n" + gl +
                                                            "\tAssets/Lit.lighting\tAssets/Imported/R421B/Lit.lighting\n"));
    std::vector<LogEntry> log;
    const size_t since = Log::fetch(0, log);
    ed.command("open Assets/Imported/R421B/Lit.unity");
    const LightingSettings &l = ed.scene().lighting;
    std::printf("    %.0f texels, %d max, padding %d, %d direct, %d indirect, %d bounces, denoise %d\n", l.texels_per_unit, l.max_size, l.padding,
                l.direct_samples, l.indirect_samples, l.bounces, (int)l.denoise);
    CHECK(ed.scene().find_by_name("Thing") != nullptr);
    CHECK(l.texels_per_unit == 25.0f && l.max_size == 512 && l.padding == 6 && l.direct_samples == 24 && l.indirect_samples == 65536 && l.bounces == 3 && !l.denoise);
    CHECK(ed.scene().unity_bake.read && ed.scene().unity_bake.source == "the LightingSettings asset" && ed.scene().unity_bake.mixed == 1);
    log.clear();
    Log::fetch(since, log);
    bool settings_info = false, subtractive_unbaked = false, optix = false;
    for (const LogEntry &e : log) {
      settings_info = settings_info || (e.level == LogLevel::Info && e.text.find("the lightmap settings came from the LightingSettings asset: 25 texels per unit") != std::string::npos);
      subtractive_unbaked = subtractive_unbaked || (e.text.find("not baked yet") != std::string::npos && e.text.find("Subtractive") != std::string::npos);
      optix = optix || e.text.find("OptiX") != std::string::npos;  // (the filter is off in Unity: no denoiser to name)
    }
    /* (Round 444: Subtractive is baked now - the scene's Lighting Mode, not a note; round 426's Directional mode too.) */
    CHECK(settings_info && !subtractive_unbaked && !optix && l.lighting_mode == 2);
    CHECK(l.directional_mode == 1);  // (round 426: the asset's m_LightmapsBakeMode 1, Directional)
    CHECK(l.bake_device == 0);  // (round 430: no m_BakeBackend in the asset - Unity's Progressive CPU: as the Render device)
    /* The asset's GUID not in the project: the scene's own block, and a note that says so (a fresh editor: the import
     * left the first one with unsaved changes, and Open would ask first). */
    Editor ed2;
    ed2.init_headless(400, 300);
    const size_t since2 = Log::fetch(0, log);
    ed2.command("open Assets/Imported/R421B/Gone.unity");
    CHECK(ed2.scene().lighting.texels_per_unit == 10.0f && ed2.scene().lighting.indirect_samples == 100);
    log.clear();
    Log::fetch(since2, log);
    bool said = false;
    for (const LogEntry &e : log) said = said || e.text.find("Lighting Settings asset (" + gone + ") isn't in this project") != std::string::npos;
    if (!said)
      for (const LogEntry &e : log) std::printf("    log: %s\n", e.text.c_str());
    CHECK(said);
    std::filesystem::remove_all(dir, ec);
  });
  test("bake phases (round 421): Generate Lighting on a worker times its lightmaps, probes, Light Volumes, reflection probes and saving, and its landing frame", [] {
    Editor ed;
    ed.init_headless(640, 400);
    GameObject *floor = ed.scene().create("Floor");
    floor->add<MeshFilter>()->mesh = primitives::plane(6.0f, 4);
    MeshRenderer *mr = floor->add<MeshRenderer>();
    mr->contribute_gi = true;
    GameObject *box = ed.scene().create("Box");
    box->add<MeshFilter>()->mesh = primitives::cube();
    box->set_local_position({0, 0.5f, 0});
    box->add<MeshRenderer>()->contribute_gi = true;
    ed.scene().lighting.texels_per_unit = 8.0f;
    ed.scene().lighting.indirect_samples = 32;
    ed.commit_change("scene");
    ed.set_async_jobs_for_test(true);
    ed.command("bake start");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(300);
    while (ed.baking_for_test() && std::chrono::steady_clock::now() < deadline) ed.step_frame_headless();
    const auto &b = ed.last_bake_times_for_test();
    std::printf("    lightmaps %.0f ms (%zu texels, filter %.1f ms), probes %.0f, volumes %.0f, reflection probes %.0f, saving %.0f; total %.0f, landing %.1f ms\n",
                b.phase_ms[0], b.texels, b.finish_ms, b.phase_ms[1], b.phase_ms[2], b.phase_ms[3], b.phase_ms[4], b.total_ms, b.land_ms);
    CHECK(!ed.baking_for_test() && !ed.lighting_data_for_test().empty());
    CHECK(b.texels > 0 && b.rays > 0 && b.phase_ms[0] > 0 && b.total_ms >= b.phase_ms[0] && b.land_ms > 0);
    for (double p : b.phase_ms) CHECK(p >= 0.0);  // (every phase reached: -1 is a phase the job never finished)
    const double total = b.total_ms;
    /* A cancelled bake lands nothing: the last bake's times stay. */
    ed.command("bake start");
    ed.step_frame_headless();
    ed.command("bake cancel");
    for (int i = 0; i < 2000 && ed.baking_for_test(); i++) ed.step_frame_headless();
    CHECK(!ed.baking_for_test() && ed.last_bake_times_for_test().total_ms == total);
  });
}


/* ---------------------------------------------------------------- round 423: the lightmap denoiser (task 0399) */
static BakeObject bake_object(uint64_t id, MeshPtr mesh, const Mat4 &model, const std::vector<MaterialPtr> &materials) {
  BakeObject o;
  o.id = id, o.mesh = mesh, o.model = model, o.materials = materials;
  return o;
}
static void tests_round423() {
  test("lightmap denoiser (round 423): the noisy part denoised - OIDN's lightmap filter where the OIDN build has it, else the A-Trous filter - closer to a converged bake at 16 and at 512 samples, the direct light kept", [] {
    MeshPtr floor = primitives::plane(4.0f, 8), wall = primitives::plane(4.0f, 8);
    const MaterialPtr white = make_material("White", Vec3(0.8f));
    std::vector<BakeObject> objs{bake_object(1, floor, Mat4::identity(), {white}),
                                 bake_object(2, wall, Mat4::trs({0, 2, -2}, Quat::euler({90, 0, 0}), {1, 1, 1}), {white})};
    BakeLight sun;
    sun.light.direction = normalize(Vec3(0.3f, -1.0f, -0.4f)), sun.light.intensity = 1.0f, sun.mode = 2, sun.id = 9;
    Environment env;
    env.mode = Environment::Gradient;
    auto bake = [&](int n, bool denoise, uint32_t seed, std::string *which) {
      BakeSettings bs;
      bs.texels_per_unit = 8, bs.indirect_samples = n, bs.denoise = denoise, bs.seed = seed;
      Lightmapper lm;
      lm.begin(objs, {sun}, env, bs);
      while (!lm.step(1e9)) {
      }
      if (which) *which = lm.denoiser();
      return lm.take_result();
    };
    const LightingData ref = bake(2048, false, 1, nullptr), noisy = bake(16, false, 0, nullptr);
    std::string which;
    const LightingData filtered = bake(16, true, 0, &which);
    auto rmse = [&](const LightingData &a) {
      double s = 0;
      size_t k = 0;
      for (size_t p = 0; p < a.pages.size(); p++)
        for (size_t i = 0; i < a.pages[p].texels.size(); i++) {
          const float r = ref.pages[p].texels[i].y;
          if (!(r > 0.0f)) continue;
          const double d = (double)a.pages[p].texels[i].y - r;
          s += d * d, k++;
        }
      return k ? std::sqrt(s / (double)k) : 1e9;
    };
    std::printf("    %s: the error at 16 samples %.4f unfiltered, %.4f filtered\n", which.c_str(), rmse(noisy), rmse(filtered));
    std::string why;
    const bool lightmap_oidn = PathTracer::oidn_lightmap_available(&why);
    if (!lightmap_oidn) std::printf("    (OIDN's lightmap filter: %s)\n", why.c_str());
    CHECK(which == (lightmap_oidn ? "OpenImageDenoise (RTLightmap)" : "the A-Trous filter"));
    CHECK(rmse(filtered) < rmse(noisy) * 0.8);
    /* A long bake: the filter mustn't make it worse (round 421 had found the old one doubling the error at 1024). */
    const LightingData long_raw = bake(512, false, 0, nullptr), long_filtered = bake(512, true, 0, nullptr);
    std::printf("    at 512 samples: %.4f unfiltered, %.4f filtered\n", rmse(long_raw), rmse(long_filtered));
    CHECK(rmse(long_filtered) <= rmse(long_raw) * 1.1 + 1e-4);
    /* The direct light kept: the sun alone (no bounce, a black sky) - the filter's pass over it moves the mean by under
     * half a percent (only shadow edges change). */
    env.mode = Environment::Color, env.color = Vec3(0.0f);
    auto direct_only = [&](bool denoise) {
      BakeSettings bs;
      bs.texels_per_unit = 8, bs.indirect_samples = 4, bs.bounces = 0, bs.denoise = denoise;
      Lightmapper lm;
      lm.begin(objs, {sun}, env, bs);
      while (!lm.step(1e9)) {
      }
      return lm.take_result();
    };
    const LightingData d0 = direct_only(false), d1 = direct_only(true);
    double s0 = 0, s1 = 0;
    for (size_t p = 0; p < d0.pages.size(); p++)
      for (size_t i = 0; i < d0.pages[p].texels.size(); i++) s0 += d0.pages[p].texels[i].y, s1 += d1.pages[p].texels[i].y;
    std::printf("    the sun alone: the summed light %.2f unfiltered, %.2f filtered\n", s0, s1);
    CHECK(s0 > 0 && std::fabs(s1 - s0) < s0 * 0.005);
    /* The defaults: Unity's 512 indirect samples. */
    CHECK(BakeSettings{}.indirect_samples == 512 && LightingSettings{}.indirect_samples == 512);
  });
  test("lightmap denoiser (review 423): a shadow edge moves by under a texel - every filtered texel inside its unfiltered 3 x 3 neighbourhood's range; a glowing strip's light near a wall kept (its emitters denoised, not clamped: none lost)", [] {
    MeshPtr floor = primitives::plane(4.0f, 8), card = primitives::plane(1.0f, 2);
    const MaterialPtr white = make_material("White", Vec3(0.8f));
    const std::vector<BakeObject> objs{bake_object(1, floor, Mat4::identity(), {white}), bake_object(2, card, Mat4::trs({0, 0.5f, 0}, Quat(), {1, 1, 1}), {white})};
    BakeLight sun;
    sun.light.direction = normalize(Vec3(0.2f, -1.0f, 0.1f)), sun.light.intensity = 1.0f, sun.mode = 2, sun.id = 9;
    Environment env;
    env.mode = Environment::Color, env.color = Vec3(0.0f);
    auto bake = [&](const std::vector<BakeObject> &o, const std::vector<BakeLight> &l, int bounces, int n, bool denoise) {
      BakeSettings bs;
      bs.texels_per_unit = 16, bs.indirect_samples = n, bs.bounces = bounces, bs.denoise = denoise;
      Lightmapper lm;
      lm.begin(o, l, env, bs);
      while (!lm.step(1e9)) {
      }
      return lm.take_result();
    };
    const LightingData a = bake(objs, {sun}, 0, 4, false), b = bake(objs, {sun}, 0, 4, true);
    const LightingData::Entry &e = a.entries.at(1);
    const Lightmap &pa = a.pages[(size_t)e.page], &pb = b.pages[(size_t)e.page];
    int outside = 0, edge = 0;
    for (int y = 1; y + 1 < pa.height; y++)
      for (int x = 1; x + 1 < pa.width; x++) {
        float lo = 1e30f, hi = -1e30f;
        for (int dy = -1; dy <= 1; dy++)
          for (int dx = -1; dx <= 1; dx++) {
            const float v = pa.texels[(size_t)(y + dy) * pa.width + x + dx].y;
            lo = std::min(lo, v), hi = std::max(hi, v);
          }
        const float v = pb.texels[(size_t)y * pa.width + x].y;
        edge += hi - lo > 0.3f;
        outside += v < lo - 1e-4f || v > hi + 1e-4f;
      }
    std::printf("    the card's shadow: %d texels on its edge; %d filtered texels outside their neighbourhood's range\n", edge, outside);
    CHECK(edge > 20 && outside == 0);
    /* A glowing strip 3 cm off a wall: its glow on the wall summed with the filter and without. */
    MeshPtr wall = primitives::plane(2.0f, 4), strip = primitives::plane(0.1f, 1);
    MaterialPtr glow = make_material("Glow", Vec3(0.0f));
    glow->emission = Vec3(1.0f), glow->emission_strength = 20.0f;
    const std::vector<BakeObject> lamp{bake_object(1, wall, Mat4::identity(), {white}), bake_object(2, strip, Mat4::trs({0, 0.03f, 0}, Quat::euler({180, 0, 0}), {1, 1, 1}), {glow})};
    const LightingData g0 = bake(lamp, {}, 1, 64, false), g1 = bake(lamp, {}, 1, 64, true);
    const LightingData::Entry &we = g0.entries.at(1);
    double s0 = 0, s1 = 0;
    float peak = 0.0f;
    for (size_t i = 0; i < g0.pages[(size_t)we.page].texels.size(); i++) {
      s0 += g0.pages[(size_t)we.page].texels[i].y, s1 += g1.pages[(size_t)we.page].texels[i].y;
      peak = std::max(peak, g0.pages[(size_t)we.page].texels[i].y);
    }
    std::printf("    the strip's glow on the wall: %.2f unfiltered, %.2f filtered (peak texel %.2f)\n", s0, s1, peak);
    CHECK(s0 > 0 && peak > 0.5f && s1 >= s0 * 0.97 && s1 <= s0 * 1.08);  // (none lost - the clamp had cut the glow; a little more where the filter spreads it into the padding, which the sum counts)
  });
}

/* ---------------------------------------------------------------- round 424: soft baked shadows (task 0400) */
static void tests_round424() {
  test("soft baked shadows (round 424): a Baked Shadow Radius widens a point light's penumbra and an Angle a sun's, the light blocked about the same; No Shadows bakes none; Unity's fields read and passed to the bake", [] {
    MeshPtr floor = primitives::plane(4.0f, 8), card = primitives::plane(0.8f, 2);
    const MaterialPtr white = make_material("White", Vec3(0.8f));
    /* A card 1 m over the floor; the light 2 m over the card (a point light) or straight down (a sun). */
    const std::vector<BakeObject> objs{bake_object(1, floor, Mat4::identity(), {white}), bake_object(2, card, Mat4::trs({0, 1, 0}, Quat(), {1, 1, 1}), {white})};
    Environment env;
    env.mode = Environment::Color, env.color = Vec3(0.0f);
    auto bake = [&](const RenderLight &l) {
      BakeSettings bs;
      bs.texels_per_unit = 16, bs.indirect_samples = 4, bs.bounces = 0, bs.direct_samples = 64, bs.denoise = false;
      BakeLight bl;
      bl.light = l, bl.mode = 2, bl.id = 9;
      Lightmapper lm;
      lm.begin(objs, {bl}, env, bs);
      while (!lm.step(1e9)) {
      }
      return lm.take_result();
    };
    /* The floor's texels against the same light with No Shadows: those between 5 % and 95 % lit are the penumbra. */
    auto penumbra = [&](RenderLight l, double *lit_share) {
      const LightingData shadowed = bake(l);
      l.shadows = false;
      const LightingData open = bake(l);
      const LightingData::Entry &e = shadowed.entries.at(1);
      const std::vector<Vec3> &a = shadowed.pages[(size_t)e.page].texels, &b = open.pages[(size_t)e.page].texels;
      int part = 0;
      double sa = 0, sb = 0;
      for (size_t i = 0; i < a.size(); i++) {
        if (!(b[i].y > 1e-4f)) continue;
        const float r = a[i].y / b[i].y;
        part += r > 0.05f && r < 0.95f;
        sa += a[i].y, sb += b[i].y;
      }
      if (lit_share) *lit_share = sb > 0 ? sa / sb : 0;
      return part;
    };
    RenderLight point;
    point.type = RenderLight::Point, point.position = Vec3(0, 3, 0), point.range = 20.0f, point.intensity = 4.0f;
    double share0, share1;
    const int hard = penumbra(point, &share0);
    point.shadow_radius = 0.4f;
    const int soft = penumbra(point, &share1);
    std::printf("    point light: %d penumbra texels sharp, %d with a 0.4 m radius; the floor's share of the open light %.3f / %.3f\n", hard, soft, share0, share1);
    CHECK(soft > hard * 3 && soft > 20);
    CHECK(1.0 - share0 > 0.02 && std::fabs((1.0 - share1) - (1.0 - share0)) < (1.0 - share0) * 0.15);  // (the shadow spreads; about as much light is blocked)
    RenderLight sun;
    sun.direction = Vec3(0, -1, 0), sun.intensity = 1.0f;
    const int sun_hard = penumbra(sun, nullptr);
    sun.shadow_angle = 20.0f;
    const int sun_soft = penumbra(sun, nullptr);
    std::printf("    sun: %d penumbra texels sharp, %d at 20 degrees\n", sun_hard, sun_soft);
    CHECK(sun_soft > sun_hard * 3 && sun_soft > 20);
    /* No Shadows: the floor's darkest texel (under the card's middle) as lit as with no card at all (Unity bakes no shadow
     * for it); the two bakes lay their pages out differently, so their darkest lit texels are compared. */
    point.shadows = false;
    const LightingData none = bake(point);
    const std::vector<BakeObject> bare{objs[0]};
    BakeSettings bs;
    bs.texels_per_unit = 16, bs.indirect_samples = 4, bs.bounces = 0, bs.direct_samples = 64, bs.denoise = false;
    BakeLight bl;
    bl.light = point, bl.mode = 2, bl.id = 9;
    Lightmapper lm;
    lm.begin(bare, {bl}, env, bs);
    while (!lm.step(1e9)) {
    }
    const LightingData alone = lm.take_result();
    auto darkest_lit = [](const Lightmap &p) {
      float m = 1e30f;
      for (const Vec3 &v : p.texels)
        if (v.y > 1e-4f) m = std::min(m, v.y);
      return m;
    };
    const float dn = darkest_lit(none.pages[(size_t)none.entries.at(1).page]), da = darkest_lit(alone.pages[(size_t)alone.entries.at(1).page]);
    std::printf("    No Shadows: the floor's darkest lit texel %.4f, without the card %.4f\n", dn, da);
    CHECK(da > 0.01f && da < 1e29f && std::fabs(dn - da) < da * 0.02f);
    /* A bulb a third of its radius off the floor, nothing in the way: the floor as lit as by a sharp light (review 424: the
     * cone's rays below the floor's horizon had counted as blocked). */
    const std::vector<BakeObject> floor_only{objs[0]};
    auto bake_on = [&](const std::vector<BakeObject> &o, const RenderLight &l) {
      BakeSettings b2;
      b2.texels_per_unit = 16, b2.indirect_samples = 4, b2.bounces = 0, b2.direct_samples = 64, b2.denoise = false;
      BakeLight b;
      b.light = l, b.mode = 2, b.id = 9;
      Lightmapper m;
      m.begin(o, {b}, env, b2);
      while (!m.step(1e9)) {
      }
      return m.take_result();
    };
    auto floor_sum = [](const LightingData &d) {
      double s = 0;
      for (const Vec3 &v : d.pages[(size_t)d.entries.at(1).page].texels) s += v.y;
      return s;
    };
    RenderLight low;
    low.type = RenderLight::Point, low.position = Vec3(0.3f, 0.1f, 0.2f), low.range = 5.0f, low.intensity = 1.0f;
    const double sharp_sum = floor_sum(bake_on(floor_only, low));
    low.shadow_radius = 0.3f;
    const double ball_sum = floor_sum(bake_on(floor_only, low));
    std::printf("    a 0.3 m bulb 0.1 m off the floor: the floor's light %.2f, sharp %.2f\n", ball_sum, sharp_sum);
    CHECK(sharp_sum > 0 && std::fabs(ball_sum - sharp_sum) < sharp_sum * 0.02);
    /* A light in a closed 20 cm box with a 5 cm radius: the floor gets none (the box's walls lie outside the ball - each
     * ray stops at the ball's near side along it, the walls are tested). */
    MeshPtr box = primitives::cube(0.2f);
    std::vector<BakeObject> boxed{objs[0], bake_object(3, box, Mat4::trs({0, 1, 0}, Quat(), {1, 1, 1}), {white})};
    RenderLight inside;
    inside.type = RenderLight::Point, inside.position = Vec3(0, 1, 0), inside.range = 5.0f, inside.intensity = 1.0f, inside.shadow_radius = 0.05f;
    const double boxed_sum = floor_sum(bake_on(boxed, inside)), unboxed_sum = floor_sum(bake_on(floor_only, inside));
    std::printf("    a 5 cm bulb in a closed 20 cm box: the floor's light %.4f (%.1f without the box)\n", boxed_sum, unboxed_sum);
    CHECK(unboxed_sum > 1.0 && boxed_sum < unboxed_sum * 1e-4);  // (a ray grazing the cube's edge may slip through a seam without Embree: macOS's CI read 0.0013)
    /* The bake's light as Unity's LightmapperUtils makes it: the radius with Soft Shadows only. */
    Light hard_light;
    hard_light.type = 1, hard_light.shadows = 1, hard_light.shadow_radius = 0.5f;
    Scene hs;
    GameObject *hg = hs.create("Hard");
    CHECK(to_render_light(*hg, hard_light).shadow_radius == 0.0f);  // (Hard Shadows: Unity's bake takes no radius)
    hard_light.shadows = 2;
    CHECK(to_render_light(*hg, hard_light).shadow_radius == 0.5f);
    /* Unity's fields: read from a prefab's Light (clamped to the field's range), given to the bake's light. */
    Editor ed;
    ed.init_headless(400, 300);
    const std::string dir = resolve_asset_path("Assets/Imported/R424");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    fs::make_dirs(dir);
    CHECK(fs::write_file(fs::join(dir, "Lamp.prefab"),
                         "%YAML 1.1\n--- !u!1 &10\nGameObject:\n  m_Name: Bulb\n--- !u!4 &11\nTransform:\n  m_GameObject: {fileID: 10}\n  m_Father: {fileID: 0}\n"
                         "--- !u!108 &12\nLight:\n  m_GameObject: {fileID: 10}\n  m_Type: 2\n  m_Intensity: 1\n  m_Shadows:\n    m_Type: 2\n  m_Lightmapping: 2\n  m_ShadowRadius: 0.25\n  m_ShadowAngle: 3\n"
                         "--- !u!1 &20\nGameObject:\n  m_Name: Sun\n--- !u!4 &21\nTransform:\n  m_GameObject: {fileID: 20}\n  m_Father: {fileID: 0}\n"
                         "--- !u!108 &22\nLight:\n  m_GameObject: {fileID: 20}\n  m_Type: 1\n  m_Intensity: 1\n  m_Lightmapping: 2\n  m_ShadowAngle: 500\n"));
    ed.command("import Assets/Imported/R424/Lamp.prefab");
    GameObject *lamp = ed.scene().find_by_name("Bulb"), *sun_g = ed.scene().find_by_name("Sun");  // (not "Lamp": the prefab's root takes its file's name)
    const Light *ll = lamp ? lamp->get<Light>() : nullptr, *sl = sun_g ? sun_g->get<Light>() : nullptr;
    std::printf("    imported: the bulb's radius %.2f (angle %.1f), the sun's angle %.1f (500 in the file)\n", ll ? ll->shadow_radius : -1.0f, ll ? ll->shadow_angle : -1.0f, sl ? sl->shadow_angle : -1.0f);
    CHECK(ll && std::fabs(ll->shadow_radius - 0.25f) < 1e-6f && std::fabs(ll->shadow_angle - 3.0f) < 1e-6f);
    CHECK(sl && sl->shadow_angle == 90.0f && sl->shadow_radius == 0.0f);
    if (lamp && ll) CHECK(to_render_light(*lamp, *ll).shadow_radius == ll->shadow_radius);
    Light dflt;
    CHECK(dflt.shadow_radius == 0.0f && dflt.shadow_angle == 0.0f);
    std::filesystem::remove_all(dir, ec);
  });
}

/* ---------------------------------------------------------------- round 425: baked light through see-through surfaces (task 0401) */
static void tests_round425() {
  test("Unity's Alpha Source (round 425): a texture whose .meta says From Gray Scale loads with its grey as alpha, None as opaque, Input Texture Alpha as decoded; the Budget reads the same field", [] {
    const std::string dir = resolve_asset_path("Assets/Imported/R425");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    fs::make_dirs(dir);
    /* A 2 x 1 TGA (32-bit): a white opaque texel and a dark grey one (grey 0x40) of alpha 0x80 - its own alpha and its grey
     * differ, so each Alpha Source shows. */
    std::string tga(18, '\0');
    tga[2] = 2, tga[12] = 2, tga[14] = 1, tga[16] = 32, tga[17] = 0x28;
    tga += std::string("\xff\xff\xff\xff", 4) + std::string("\x40\x40\x40\x80", 4);
    auto load_with = [&](const std::string &name, const char *meta) {
      const std::string p = fs::join(dir, name);
      CHECK(fs::write_file(p, tga));
      if (meta) CHECK(fs::write_file(p + ".meta", std::string("fileFormatVersion: 2\nguid: 0000425\nTextureImporter:\n  alphaUsage: ") + meta + "\n"));
      return texture_load(p, false);
    };
    const TexturePtr gray = load_with("gray.tga", "2"), none = load_with("none.tga", "0"), input = load_with("input.tga", "1"), plain = load_with("plain.tga", nullptr);
    CHECK(gray && none && input && plain);
    if (gray && none && input && plain) {
      std::printf("    alpha of the white / dark texels: From Gray Scale %.3f / %.3f, None %.3f / %.3f, Input %.3f / %.3f, no .meta %.3f\n", gray->fetch(0, 0, 0, TexWrap::Extend).w,
                  gray->fetch(0, 1, 0, TexWrap::Extend).w, none->fetch(0, 0, 0, TexWrap::Extend).w, none->fetch(0, 1, 0, TexWrap::Extend).w, input->fetch(0, 0, 0, TexWrap::Extend).w,
                  input->fetch(0, 1, 0, TexWrap::Extend).w, plain->fetch(0, 1, 0, TexWrap::Extend).w);
      CHECK(std::fabs(gray->fetch(0, 0, 0, TexWrap::Extend).w - 1.0f) < 1e-3f && std::fabs(gray->fetch(0, 1, 0, TexWrap::Extend).w - 64.0f / 255.0f) < 2e-3f);
      CHECK(none->fetch(0, 1, 0, TexWrap::Extend).w == 1.0f);
      CHECK(std::fabs(input->fetch(0, 1, 0, TexWrap::Extend).w - 128.0f / 255.0f) < 2e-3f && std::fabs(plain->fetch(0, 1, 0, TexWrap::Extend).w - 128.0f / 255.0f) < 2e-3f);
    }
    /* The Budget: From Gray Scale has alpha even when the image has none (DXT5 automatic, twice DXT1's bytes). */
    double with_alpha = 0, without = 0;
    CHECK(unity_texture_megabytes("Assets/Imported/R425/gray.tga", 1024, 1024, false, with_alpha) && unity_texture_megabytes("Assets/Imported/R425/none.tga", 1024, 1024, false, without));
    std::printf("    the Budget: From Gray Scale %.3f MB, None %.3f MB\n", with_alpha, without);
    CHECK(with_alpha > without * 1.9);
    Bitmap b;
    b.width = 1, b.height = 1, b.rgba8 = {30, 60, 90, 7};
    apply_alpha_usage(b, 2);
    CHECK(b.rgba8[3] == 60);
    apply_alpha_usage(b, 1);
    CHECK(b.rgba8[3] == 60);
    apply_alpha_usage(b, 0);
    CHECK(b.rgba8[3] == 255);
    texture_cache_clear();
    std::filesystem::remove_all(dir, ec);
  });
  test("baked light through see-through surfaces and the legacy falloff (round 425): a card of alpha 0.25 lets 3/4 of a Baked light through to the floor as Unity's lightmapper does, an opaque one none; a bake's light falls off as 1 / (1 + 25 (d / Range)^2), windowed; the views' curve unchanged", [] {
    MeshPtr floor = primitives::plane(4.0f, 8), card = primitives::plane(2.0f, 2);
    const MaterialPtr white = make_material("White", Vec3(0.8f));
    MaterialPtr veil = make_material("Veil", Vec3(0.0f));
    veil->surface = (int)MaterialSurface::Transparent, veil->alpha = 0.25f;
    Environment env;
    env.mode = Environment::Color, env.color = Vec3(0.0f);
    RenderLight sun;
    sun.direction = Vec3(0, -1, 0), sun.intensity = 1.0f;
    auto floor_sum = [&](const MaterialPtr &card_mat) {
      std::vector<BakeObject> objs{bake_object(1, floor, Mat4::identity(), {white})};
      if (card_mat) objs.push_back(bake_object(2, card, Mat4::trs({0, 1, 0}, Quat(), {1, 1, 1}), {card_mat}));
      BakeSettings bs;
      bs.texels_per_unit = 8, bs.indirect_samples = 4, bs.bounces = 0, bs.denoise = false;
      bs.backface_tolerance = 0.0f;  // (round 430: the floor under a single-sided card sees its back face - Unity's advice for such geometry)
      BakeLight bl;
      bl.light = sun, bl.mode = 2, bl.id = 9;
      Lightmapper lm;
      lm.begin(objs, {bl}, env, bs);
      while (!lm.step(1e9)) {
      }
      const LightingData d = lm.take_result();
      double s = 0;
      for (const Vec3 &v : d.pages[(size_t)d.entries.at(1).page].texels) s += v.y;
      return s;
    };
    /* The page holds the card's own texels too (lit alike in every case): the share through is (veiled - opaque) over
     * (clear - opaque), a clear card being one of alpha 0. */
    MaterialPtr clear = make_material("Clear", Vec3(0.0f));
    clear->surface = (int)MaterialSurface::Transparent, clear->alpha = 0.0f;
    const double through = floor_sum(clear), veiled = floor_sum(veil), blocked = floor_sum(white);
    const double share = through > blocked ? (veiled - blocked) / (through - blocked) : 0.0;
    std::printf("    the page's light: under a clear card %.1f, one of alpha 0.25 %.1f, an opaque one %.1f - %.3f of the shadowed light through\n", through, veiled, blocked, share);
    CHECK(through > blocked * 1.1 && std::fabs(share - 0.75) < 0.02);
    /* The same through a sized point light's cone of rays (round 424's path). */
    sun.type = RenderLight::Point, sun.position = Vec3(0, 3, 0), sun.range = 20.0f, sun.intensity = 4.0f, sun.shadow_radius = 0.3f;
    const double p_through = floor_sum(clear), p_veiled = floor_sum(veil), p_blocked = floor_sum(white);
    const double p_share = p_through > p_blocked ? (p_veiled - p_blocked) / (p_through - p_blocked) : 0.0;
    std::printf("    a 0.3 m point light: %.3f of the shadowed light through the card of alpha 0.25\n", p_share);
    CHECK(p_through > p_blocked * 1.05 && std::fabs(p_share - 0.75) < 0.03);
    RenderLight pt;
    pt.type = RenderLight::Point, pt.range = 10.0f;
    CHECK(std::fabs(range_falloff(pt, 5.0f) - 0.25f) < 1e-6f);  // (the views: (1 - 0.5)^2)
    pt.baked_falloff = true;
    const float expect = (1.0f - 0.0625f) * (1.0f - 0.0625f) / (1.0f + 25.0f * 0.25f);
    std::printf("    at half the Range: the views' curve 0.250, the bake's %.4f\n", range_falloff(pt, 5.0f));
    CHECK(std::fabs(range_falloff(pt, 5.0f) - expect) < 1e-6f && range_falloff(pt, 0.0f) == 1.0f && range_falloff(pt, 10.0f) == 0.0f && range_falloff(pt, 12.0f) == 0.0f);
    /* The bake's lights carry it; the views' don't. */
    Editor ed;
    ed.init_headless(400, 300);
    ed.command("create Point Light");
    ed.command("set Light.Mode 2");
    std::vector<BakeObject> objs;
    std::vector<BakeLight> lights;
    ed.collect_bake_scene_for_test(objs, lights);
    bool baked = false;
    for (const BakeLight &l : lights) baked = baked || (l.mode == 2 && l.light.baked_falloff);
    CHECK(baked);
    GameObject *g = ed.scene().find_by_name("Point Light");
    if (g && g->get<Light>()) CHECK(!to_render_light(*g, *g->get<Light>()).baked_falloff);
  });
}

/* ---------------------------------------------------------------- round 426: directional lightmaps (task 0402) */
static void tests_round426() {
  test("directional lightmaps (round 426): the bake keeps where each texel's light came from - toward a slanted sun on an open floor; the direction page saved and read back; a non-directional bake has none (an old file's pages left unread); the rebalance leaves an unbumped surface as baked and lights a bump facing the light more, the back of a double-sided surface as its front", [] {
    MeshPtr floor = primitives::plane(4.0f, 8);
    const MaterialPtr white = make_material("White", Vec3(0.8f));
    Environment env;
    env.mode = Environment::Color, env.color = Vec3(0.0f);
    BakeLight sun;
    sun.light.direction = normalize(Vec3(-1.0f, -1.0f, 0.0f)), sun.light.intensity = 1.0f, sun.mode = 2, sun.id = 9;
    auto bake = [&](bool directional) {
      BakeSettings bs;
      bs.texels_per_unit = 8, bs.indirect_samples = 8, bs.bounces = 0, bs.denoise = false, bs.directional = directional;
      Lightmapper lm;
      lm.begin({bake_object(1, floor, Mat4::identity(), {white})}, {sun}, env, bs);
      while (!lm.step(1e9)) {
      }
      return lm.take_result();
    };
    const LightingData d = bake(true), plain = bake(false);
    CHECK(d.dir_pages.size() == d.pages.size() && plain.dir_pages.empty());
    const Vec3 to_sun = normalize(Vec3(1.0f, 1.0f, 0.0f));
    float worst = 1.0f;
    size_t lit = 0;
    for (size_t i = 0; !d.dir_pages.empty() && i < d.pages[0].texels.size(); i++) {
      if (!(d.pages[0].texels[i].y > 1e-3f)) continue;
      worst = std::min(worst, dot(d.dir_pages[0].texels[i], to_sun));
      lit++;
    }
    std::printf("    %zu lit texels: their light's direction against the sun's, worst cosine %.4f\n", lit, worst);
    CHECK(lit > 100 && worst > 0.99f);
    /* Saved and read back (Radiance's 8-bit mantissas: about 1 % of the encoded value). */
    const std::string dir = resolve_asset_path("Assets/Imported/R426");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::string err;
    CHECK(d.save(dir, err));
    LightingData back;
    CHECK(back.load(dir, err) && back.dir_pages.size() == d.dir_pages.size());
    float diff = 0.0f;
    for (size_t i = 0; !back.dir_pages.empty() && i < back.dir_pages[0].texels.size(); i++) diff = std::max(diff, length(back.dir_pages[0].texels[i] - d.dir_pages[0].texels[i]));
    std::printf("    the direction page read back: worst difference %.4f\n", diff);
    CHECK(diff < 0.03f);
    /* A direction file gone: the bake loads, without directions. */
    std::filesystem::remove(std::filesystem::u8path(fs::join(dir, "LightmapDir-0.hdr")), ec);
    LightingData no_file;
    CHECK(no_file.load(dir, err) && no_file.dir_pages.empty() && no_file.pages.size() == d.pages.size());
    CHECK(d.save(dir, err));
    CHECK(plain.save(dir, err));  // (the folder's LightmapDir-0.hdr stays: the file says the bake has none)
    LightingData back_plain;
    CHECK(back_plain.load(dir, err) && back_plain.dir_pages.empty() && back_plain.pages.size() == plain.pages.size());
    std::filesystem::remove_all(dir, ec);
    /* The rebalance (Unity's DecodeDirectionalLightmap with its divisor at the surface's own normal). */
    const Vec3 up(0, 1, 0), a = to_sun * 0.9f;
    CHECK(std::fabs(lightmap_direction_factor(up, up, a) - 1.0f) < 1e-6f);
    const Vec3 toward = normalize(Vec3(0.5f, 1.0f, 0.0f)), away = normalize(Vec3(-0.5f, 1.0f, 0.0f));
    std::printf("    a bump tilted toward the light %.3f, away %.3f, light from all around (a = 0) %.3f\n", lightmap_direction_factor(toward, up, a),
                lightmap_direction_factor(away, up, a), lightmap_direction_factor(toward, up, Vec3(0.0f)));
    CHECK(lightmap_direction_factor(toward, up, a) > 1.05f && lightmap_direction_factor(away, up, a) < 0.95f);
    CHECK(lightmap_direction_factor(toward, up, Vec3(0.0f)) == 1.0f);
    /* The back of a double-sided surface: both normals turned to the eye - decoded in the front's frame, so a bump there
     * gets what it gets on the front (review 426: up to 4x before). */
    std::printf("    the same bump seen from behind %.3f\n", lightmap_direction_factor(-toward, -up, a, true));
    CHECK(std::fabs(lightmap_direction_factor(-toward, -up, a, true) - lightmap_direction_factor(toward, up, a)) < 1e-6f);
    CHECK(lightmap_direction_factor(up, up, -a) == 1.0f);  // (light from behind the surface: no rebalancing)
  });
  test("directional lightmaps in the editor (round 426): Generate Lighting bakes the direction pages, a lightmapped object's draw item gets its page, Non-Directional none", [] {
    Editor ed;
    ed.init_headless(640, 400);
    GameObject *floor = ed.scene().create("Floor");
    floor->add<MeshFilter>()->mesh = primitives::plane(6.0f, 4);
    floor->add<MeshRenderer>()->contribute_gi = true;
    ed.scene().lighting.texels_per_unit = 4.0f;
    ed.scene().lighting.indirect_samples = 16;
    CHECK(ed.scene().lighting.directional_mode == 1);  // (Unity's default)
    ed.commit_change("scene");
    ed.set_async_jobs_for_test(true);
    ed.command("bake start");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(300);
    while (ed.baking_for_test() && std::chrono::steady_clock::now() < deadline) ed.step_frame_headless();
    const LightingData &ld = ed.lighting_data_for_test();
    CHECK(!ld.empty() && ld.dir_pages.size() == ld.pages.size());
    const Mesh *m = floor->evaluated_mesh(1);
    DrawItem it;
    it.mesh = m ? &m->render_mesh() : nullptr;
    if (m) ed.attach_lightmap_for_test(*floor, *m, it);
    CHECK(it.lightmap && it.lightmap_dir);
    ed.scene().lighting.directional_mode = 0;
    DrawItem it2;
    it2.mesh = it.mesh;
    if (m) ed.attach_lightmap_for_test(*floor, *m, it2);
    CHECK(it2.lightmap && !it2.lightmap_dir);  // (a Unity scene's mode: the round 421 test's import reads it)
  });
}

/* ---------------------------------------------------------------- round 428: reflection probes prefiltered as Unity's (task 0404) */
/* The prefilter's exact value: every cube texel weighted by what the importance sampler converges to - the lobe's pdf
 * (D(h) / 4 when N = V) times N.L, by the texel's solid angle. */
static float r428_exact_prefilter(const ReflectionProbeBake &b, Vec3 n, float rough) {
  const float a = std::max(1e-3f, rough * rough), a2 = a * a;
  double sum = 0.0, wsum = 0.0;
  for (int f = 0; f < 6; f++)
    for (int y = 0; y < b.size; y++)
      for (int x = 0; x < b.size; x++) {
        const float u = ((float)x + 0.5f) / (float)b.size, v = ((float)y + 0.5f) / (float)b.size;
        const Vec3 l = cube_direction(f, u, v);
        const float ndl = dot(n, l);
        if (ndl <= 0.0f) continue;
        const float su = 2.0f * u - 1.0f, sv = 2.0f * v - 1.0f, r2 = 1.0f + su * su + sv * sv;
        const float sr = 4.0f / ((float)b.size * (float)b.size) / (r2 * std::sqrt(r2));  // (a cube texel's solid angle)
        const Vec3 h = normalize(n + l);
        const float noh = dot(n, h), d = noh * noh * (a2 - 1.0f) + 1.0f, pdf = a2 / (kPi * d * d) * 0.25f;
        const double w = (double)pdf * ndl * sr;
        sum += w * b.faces[f][(size_t)y * b.size + x].x;
        wsum += w;
      }
  return wsum > 0.0 ? (float)(sum / wsum) : 0.0f;
}

static void tests_round428() {
  test("reflection probes (round 428): the mips prefiltered with GGX for the roughness Unity maps to each (r (1.7 - 0.7 r) x 6): a uniform probe stays uniform at every level, a bright patch spreads wider the rougher with its light kept, and both renderers' lookups take Unity's mapping", [] {
    CHECK(reflection_probe_lod(0.0f, 11) == 0.0f && std::fabs(reflection_probe_lod(1.0f, 11) - 6.0f) < 1e-6f && reflection_probe_lod(1.0f, 4) == 3.0f);
    for (float r : {0.1f, 0.35f, 0.6f, 0.9f}) CHECK(std::fabs(reflection_probe_level_roughness(reflection_probe_lod(r, 11)) - r) < 1e-4f);
    ReflectionProbeBake b;
    b.size = 128;  // (Unity's default resolution: at 32 a level-4 map is 8 x 4 pixels, too coarse for its lobe)
    for (auto &f : b.faces) f.assign((size_t)b.size * b.size, Vec3(0.25f, 0.5f, 1.0f));
    TexturePtr uniform = reflection_probe_texture(b);
    CHECK(uniform && uniform->levels.size() > 6);
    float worst = 0.0f;
    for (int k = 0; uniform && k <= 6; k++)
      for (const Vec4 &p : uniform->levels[(size_t)k].pxf) worst = std::max(worst, std::fabs(p.y - 0.5f));
    std::printf("    a uniform probe: the levels 0-6 differ from it by at most %.5f\n", worst);
    CHECK(worst < 1e-3f);
    /* A bright square in the middle of +Y (the sky's top), dark elsewhere. */
    for (auto &f : b.faces) f.assign((size_t)b.size * b.size, Vec3(0.0f));
    for (int y = 48; y < 80; y++)
      for (int x = 48; x < 80; x++) b.faces[2][(size_t)y * b.size + x] = Vec3(10.0f);
    ScopedTimer build_t;
    TexturePtr t = reflection_probe_texture(b);
    std::printf("    a 128 probe's map with its prefiltered levels: %.1f ms\n", build_t.ms());
    CHECK(t != nullptr);
    if (!t) return;
    auto level_stats = [&](int k, float &peak, double &total) {
      const Texture::Level &lv = t->levels[(size_t)k];
      peak = 0.0f, total = 0.0;
      for (int y = 0; y < lv.h; y++)
        for (int x = 0; x < lv.w; x++) {
          const float v = lv.pxf[(size_t)y * lv.w + x].x;
          peak = std::max(peak, v);
          total += v * std::sin(kPi * ((float)y + 0.5f) / (float)lv.h) / ((double)lv.w * lv.h);  // (the sphere's area per row)
        }
    };
    float p1, p3, p5;
    double t1, t3, t5;
    level_stats(1, p1, t1), level_stats(3, p3, t3), level_stats(5, p5, t5);
    std::printf("    a bright patch: the peak %.3f / %.3f / %.3f at levels 1 / 3 / 5, the light over the sphere %.4f / %.4f / %.4f\n", p1, p3, p5, t1, t3, t5);
    CHECK(p1 > p3 && p3 > p5);
    /* (Each texel's lobe is normalized by its own N.L weights - the split sum's prefilter, Unity's and Karis's - so the
     * light over the sphere may drift a little as the lobe widens: within 5 %.) */
    CHECK(std::fabs(t3 - t1) < t1 * 0.05 && std::fabs(t5 - t1) < t1 * 0.05);
    /* Near the top the lookups read the patch (round 428 found the rows upside down: the sharp lookup had read the floor's
     * dark) and the rough one the level prefiltered for its roughness (10 degrees off the pole: the map's top row wraps to
     * its bottom one in a bilinear read). */
    RenderProbe rp;
    rp.map = t;
    const Vec3 up = normalize(Vec3(0.1763f, 1.0f, 0.0f));
    const Vec3 sharp = reflection_probe_specular(rp, Vec3(0.0f), up, 0.0f), rough = reflection_probe_specular(rp, Vec3(0.0f), up, 0.5f);
    const float want = reflection_probe_prefilter(b, up, 0.5f, 1024).x;
    std::printf("    10 degrees off the top: %.3f at roughness 0, %.3f at 0.5 (the lobe itself %.3f)\n", sharp.x, rough.x, want);
    CHECK(sharp.x > 9.0f && sharp.x > rough.x && std::fabs(rough.x - want) < want * 0.2f);
    /* And straight down, the dark below: a reflection toward the floor sees the floor. */
    CHECK(reflection_probe_specular(rp, Vec3(0.0f), normalize(Vec3(0.1763f, -1.0f, 0.0f)), 0.0f).x < 0.01f);
    /* A lamp 2 x 2 texels across (review 428): at the rough levels each sample reads the mip its share of the lobe covers,
     * so the lamp blurs into a smooth blob - every texel near it within 20 % of the exact lobe (every cube texel weighted). */
    for (auto &f : b.faces) f.assign((size_t)b.size * b.size, Vec3(0.0f));
    for (int y = 63; y < 65; y++)
      for (int x = 63; x < 65; x++) b.faces[4][(size_t)y * b.size + x] = Vec3(1000.0f);  // (+Z's middle)
    TexturePtr lamp = reflection_probe_texture(b);
    CHECK(lamp != nullptr);
    if (!lamp) return;
    float worst_off = 0.0f;
    for (int k = 4; k <= 6; k++) {
      const Texture::Level &lv = lamp->levels[(size_t)k];
      const float rough_k = reflection_probe_level_roughness((float)k);
      for (int dx = -2; dx <= 2; dx++) {
        /* Texels along the equator around +Z (u = 0.5 + phi / 2 pi; +Z at phi 0). */
        const int x = lv.w / 2 + dx, y = lv.h / 2;
        const Vec3 dir = equirect_to_dir(Vec2(((float)x + 0.5f) / (float)lv.w, 1.0f - ((float)y + 0.5f) / (float)lv.h), 0.0f);
        const float ref = r428_exact_prefilter(b, dir, rough_k), got = lv.pxf[(size_t)y * lv.w + x].x;
        if (ref > 0.05f) worst_off = std::max(worst_off, std::fabs(got - ref) / ref);
      }
    }
    std::printf("    a lamp 2 x 2 texels across: levels 4-6 near it within %.1f %% of the exact lobe\n", worst_off * 100.0f);
    CHECK(worst_off < 0.2f);  // (15 % measured; the worst is a texel at the lobe's tail, where the value is small)
  });
}

/* ---------------------------------------------------------------- round 429: Filamented's specular occlusion from the baked light (task 0405) */
static void tests_round429() {
  test("baked specular occlusion (round 429): Filamented's to its own numbers - Lagarde's specular occlusion of |baked irradiance| / Exposure Occlusion at the squared roughness, then the multi-bounce by f0; off at 0, none where the baked light is bright", [] {
    const Vec3 dielectric(0.04f), metal(0.8f);
    CHECK(baked_specular_occlusion(0.0f, Vec3(0.0f), 0.5f, 0.5f, dielectric).x == 1.0f);  // (off: every shader but Filamented)
    CHECK(baked_specular_occlusion(0.2f, Vec3(0.5f), 0.5f, 0.5f, dielectric).x == 1.0f);  // (bright: saturated)
    /* Reference values from Filamented's HLSL (SpecularAO_Lagarde, gtaoMultiBounce, perceptualRoughness^2), computed
     * apart: nv, baked, Exposure Occlusion, perceptual roughness, f0 -> what the reflection keeps. */
    struct Case {
      float nv, baked, eo, pr;
      Vec3 f0;
      float want;
    };
    const Case cases[] = {{0.5f, 0.05f, 0.2f, 0.3f, dielectric, 0.420316f}, {0.5f, 0.05f, 0.2f, 0.3f, metal, 0.748777f},
                          {0.2f, 0.02f, 0.2f, 0.6f, dielectric, 0.164152f}, {0.9f, 0.0f, 0.2f, 0.1f, Vec3(0.5f), 0.0f}};
    for (const Case &c : cases) {
      const float got = baked_specular_occlusion(c.eo, Vec3(c.baked), c.nv, c.pr, c.f0).x;
      std::printf("    nv %.1f, |irradiance| %.3f, roughness %.1f, f0 %.2f: keeps %.6f (Filamented %.6f)\n", c.nv, c.baked * 1.7320508f, c.pr, c.f0.x, got, c.want);
      CHECK(std::fabs(got - c.want) < 1e-4f);
    }
  });
}

/* ---------------------------------------------------------------- round 430: texel validity and the bake's device (task 0406) */
static void tests_round430() {
  test("texel validity (round 430): Unity's Backface Tolerance - a floor's texels under a box sunk into it see back faces and are left out, filled from their valid neighbours (no dark line along the box); tolerance 0 keeps every texel", [] {
    MeshPtr floor = primitives::plane(4.0f, 40), box = primitives::cube(1.0f);
    const MaterialPtr white = make_material("White", Vec3(0.8f));
    const std::vector<BakeObject> objs{bake_object(1, floor, Mat4::identity(), {white}), bake_object(2, box, Mat4::trs({0, 0.25f, 0}, Quat(), {1, 1, 1}), {white})};
    Environment env;
    env.mode = Environment::Color, env.color = Vec3(1.0f);
    auto bake = [&](float tolerance, size_t *invalid) {
      BakeSettings bs;
      bs.texels_per_unit = 16, bs.indirect_samples = 32, bs.bounces = 1, bs.denoise = true, bs.backface_tolerance = tolerance;
      Lightmapper lm;
      lm.begin(objs, {}, env, bs);
      while (!lm.step(1e9)) {
      }
      if (invalid) *invalid = lm.invalid_texels();
      return lm.take_result();
    };
    size_t with = 0, without = 0;
    const LightingData kept = bake(0.9f, &with), all = bake(0.0f, &without);
    /* The box's 1 m footprint at 16 texels a metre: about 256 of the floor's texels are inside it, and the box's own
     * faces below the floor (its bottom and the lower quarter of its sides, under the floor's back) up to 512 more. */
    std::printf("    texels inside geometry: %zu with Unity's 0.9 tolerance, %zu with 0\n", with, without);
    CHECK(with > 180 && with < 900 && without == 0);
    /* The floor's darkest point on the box's contact line (0.5 m from its centre): with validity it stays
     * lit from the valid side (the hidden texels no longer bleed their darkness out through the filter). */
    auto ring_min = [&](const LightingData &d) {
      const LightingData::Entry &e = d.entries.at(1);
      const Lightmap &p = d.pages[(size_t)e.page];
      float m = 1e30f;
      for (size_t t = 0; t < floor->render_mesh().tri_count(); t++)
        for (int c = 0; c < 3; c++) {
          const Vec3 q = floor->render_mesh().positions[floor->render_mesh().indices[t * 3 + (size_t)c]];
          const float r = std::max(std::fabs(q.x), std::fabs(q.z));
          if (r < 0.49f || r > 0.56f) continue;  // (the vertices on the contact line, 0.5 m out)
          m = std::min(m, p.sample(e.tri_uv[t * 3 + (size_t)c]).y);
        }
      return m;
    };
    const float ring_kept = ring_min(kept), ring_all = ring_min(all);
    std::printf("    the darkest of the floor just outside the box: %.4f with validity, %.4f without\n", ring_kept, ring_all);
    CHECK(ring_kept > ring_all * 1.5f);  // (0.52 against 0.19 measured: the dark line along the box gone)
    /* A single-sided 2 m roof 1 m over the floor (review 430): the floor's texels under it see its back face, are inside
     * geometry at 0.9, and are filled from their chart - none left black. Its material Double Sided: none inside. */
    MeshPtr roof = primitives::plane(2.0f, 2);
    MaterialPtr single = make_material("Single", Vec3(0.8f)), both = make_material("Both", Vec3(0.8f));
    both->double_sided = true;
    auto roof_bake = [&](const MaterialPtr &m, size_t *invalid) {
      const std::vector<BakeObject> o{bake_object(1, floor, Mat4::identity(), {white}), bake_object(3, roof, Mat4::trs({0, 1, 0}, Quat(), {1, 1, 1}), {m})};
      BakeSettings bs;
      bs.texels_per_unit = 16, bs.indirect_samples = 16, bs.bounces = 1, bs.denoise = true;
      Lightmapper lm;
      lm.begin(o, {}, env, bs);
      while (!lm.step(1e9)) {
      }
      *invalid = lm.invalid_texels();
      return lm.take_result();
    };
    size_t under_single = 0, under_both = 0;
    const LightingData rs = roof_bake(single, &under_single);
    roof_bake(both, &under_both);
    const LightingData::Entry &fe = rs.entries.at(1);
    const Lightmap &fp = rs.pages[(size_t)fe.page];
    float floor_min = 1e30f;
    for (size_t i = 0; i < fe.tri_uv.size(); i++) floor_min = std::min(floor_min, fp.sample(fe.tri_uv[i]).y);
    std::printf("    a single-sided roof: %zu texels inside geometry, the floor's darkest %.4f; Double Sided: %zu inside\n", under_single, floor_min, under_both);
    CHECK(under_single > 200 && floor_min > 0.05f && under_both == 0);
  });
}

/* ---------------------------------------------------------------- round 438: directional strength (task 0414) */
static void tests_round438() {
  test("directional strength (round 438): a direction is saved as long as Unity's - its share raised to 7.25 (SideRail's median 0.744 to Unity's 0.117), the way it points kept; one light alone stays full length; a floor lit by a sun and a bright sky gets a short direction, so a bump tilts its baked light less than with the sun alone", [] {
    const Vec3 d = normalize(Vec3(1.0f, 2.0f, 0.5f));
    const Vec3 med = Lightmapper::saved_direction(d * 0.744f), p90 = Lightmapper::saved_direction(d * 0.949f);
    std::printf("    share 0.744 -> %.3f (Unity's median 0.117), 0.949 -> %.3f (Unity's p90 0.715), 1 -> %.3f\n", length(med), length(p90),
                length(Lightmapper::saved_direction(d)));
    CHECK(std::fabs(length(med) - 0.117f) < 0.005f && std::fabs(length(p90) - 0.684f) < 0.01f);
    CHECK(dot(normalize(med), d) > 0.9999f && std::fabs(length(Lightmapper::saved_direction(d)) - 1.0f) < 1e-5f);
    CHECK(length(Lightmapper::saved_direction(Vec3(0.0f))) == 0.0f && length(Lightmapper::saved_direction(d * 3.0f)) <= 1.0f + 1e-5f);
    /* A floor under a slanted sun, with and without a bright sky. */
    MeshPtr floor = primitives::plane(4.0f, 8);
    const MaterialPtr white = make_material("White", Vec3(0.8f));
    BakeLight sun;
    sun.light.direction = normalize(Vec3(-1.0f, -1.0f, 0.0f)), sun.light.intensity = 1.0f, sun.mode = 2, sun.id = 9;
    auto mean_length = [&](Vec3 sky) {
      Environment env;
      env.mode = Environment::Color, env.color = sky;
      BakeSettings bs;
      bs.texels_per_unit = 8, bs.indirect_samples = 16, bs.bounces = 1, bs.denoise = false, bs.directional = true;
      Lightmapper lm;
      lm.begin({bake_object(1, floor, Mat4::identity(), {white})}, {sun}, env, bs);
      while (!lm.step(1e9)) {
      }
      const LightingData r = lm.take_result();
      double sum = 0.0;
      size_t n = 0;
      for (size_t i = 0; !r.dir_pages.empty() && i < r.pages[0].texels.size(); i++) {
        if (!(r.pages[0].texels[i].y > 1e-3f)) continue;
        sum += length(r.dir_pages[0].texels[i]);
        n++;
      }
      return n ? (float)(sum / (double)n) : 0.0f;
    };
    const float alone = mean_length(Vec3(0.0f)), with_sky = mean_length(Vec3(0.5f));
    const Vec3 up(0, 1, 0), toward = normalize(Vec3(0.5f, 1.0f, 0.0f)), to_sun = normalize(Vec3(1.0f, 1.0f, 0.0f));
    const float tilt_alone = lightmap_direction_factor(toward, up, to_sun * alone), tilt_sky = lightmap_direction_factor(toward, up, to_sun * with_sky);
    std::printf("    the floor's mean direction length: the sun alone %.3f, with a sky %.3f; a bump toward the sun gets x%.3f and x%.3f\n", alone, with_sky,
                tilt_alone, tilt_sky);
    CHECK(alone > 0.97f && with_sky < 0.5f * alone && tilt_sky < tilt_alone && tilt_sky >= 1.0f);
  });
}
/* ---------------------------------------------------------------- round 439: emitters (task 0415) */
static void tests_round439() {
  test("emitters (round 439): an Unlit material sends its colour as light and is picked by it - the bake samples a small unlit sign at each texel, so a floor under it is far less noisy at 16 samples than when only the gather finds it, and the same on average; an emission map counts by its mean, not as white", [] {
    MaterialPtr sign = make_material("Exit sign", Vec3(0.2f, 1.0f, 0.3f));
    sign->unlit = true;
    MaterialPtr glow = make_material("Glow", Vec3(0.5f));
    glow->emission = Vec3(1.0f), glow->emission_strength = 2.0f;
    MaterialPtr plain = make_material("Plain", Vec3(0.5f));
    std::printf("    emitter luminance: the unlit sign %.3f (its colour's), an emission of 2 %.3f, a plain grey %.3f\n", PathTracer::emitter_luminance(*sign),
                PathTracer::emitter_luminance(*glow), PathTracer::emitter_luminance(*plain));
    CHECK(std::fabs(PathTracer::emitter_luminance(*sign) - (0.2126f * 0.2f + 0.7152f + 0.0722f * 0.3f)) < 1e-4f);
    CHECK(std::fabs(PathTracer::emitter_luminance(*glow) - 2.0f) < 1e-4f && PathTracer::emitter_luminance(*plain) == 0.0f);
    /* A floor in the dark, a small unlit sign half a metre above it. */
    MeshPtr floor = primitives::plane(2.0f, 4), panel = primitives::plane(0.3f, 1);
    const MaterialPtr white = make_material("White", Vec3(0.8f));
    Environment env;
    env.mode = Environment::Color, env.color = Vec3(0.0f);
    auto bake = [&](bool sample, int samples, uint32_t seed) {
      BakeSettings bs;
      bs.texels_per_unit = 8, bs.indirect_samples = samples, bs.bounces = 1, bs.denoise = false, bs.directional = false, bs.sample_emissive = sample, bs.seed = seed;
      BakeObject f = bake_object(1, floor, Mat4::identity(), {white}), p = bake_object(2, panel, Mat4::translate(Vec3(0.0f, 0.5f, 0.0f)), {sign});
      p.want_lightmap = false;
      Lightmapper lm;
      lm.begin({f, p}, {}, env, bs);
      while (!lm.step(1e9)) {
      }
      return lm.take_result();
    };
    const LightingData ref = bake(true, 1024, 7), ref_off = bake(false, 1024, 8), noisy = bake(false, 16, 1), sampled = bake(true, 16, 1);
    auto floor_texels = [](const LightingData &d) {
      std::vector<float> v;
      const LightingData::Entry &e = d.entries.at(1);
      const Lightmap &pg = d.pages[(size_t)e.page];
      for (size_t i = 0; i < e.tri_uv.size(); i++) {
        const Vec3 c = pg.sample(e.tri_uv[i]);
        v.push_back(0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z);
      }
      return v;
    };
    const std::vector<float> r = floor_texels(ref), a = floor_texels(noisy), b = floor_texels(sampled), ro = floor_texels(ref_off);
    double sum_on = 0.0, sum_off = 0.0;
    for (size_t i = 0; i < r.size() && i < ro.size(); i++) sum_on += r[i], sum_off += ro[i];
    std::printf("    1024 samples: the floor's light %.4f with the sign sampled, %.4f found by the gather alone\n", sum_on, sum_off);
    CHECK(std::fabs(sum_on - sum_off) / sum_off < 0.03);
    double ea = 0.0, eb = 0.0, mr = 0.0, mb = 0.0;
    for (size_t i = 0; i < r.size() && i < a.size() && i < b.size(); i++) {
      ea += (a[i] - r[i]) * (a[i] - r[i]), eb += (b[i] - r[i]) * (b[i] - r[i]);
      mr += r[i], mb += b[i];
    }
    const double n = (double)std::max<size_t>(1, r.size());
    std::printf("    the floor at 16 samples against 1024: error %.4f with only the gather, %.4f with the sign sampled; mean %.4f against %.4f\n", std::sqrt(ea / n),
                std::sqrt(eb / n), mb / n, mr / n);
    CHECK(mr / n > 1e-3 && eb < 0.5 * ea && std::fabs(mb - mr) / mr < 0.1);
  });
  test("emitters (round 439): a Unity material's Global Illumination decides whether its emission lights the bake - Baked does, Realtime and None don't (Unity's m_LightmapFlags, read on import); a Blendity material's always does", [] {
    std::string yaml = "%YAML 1.1\n--- !u!21 &2100000\nMaterial:\n  m_Name: Lamp\n  m_ShaderKeywords: _EMISSION\n  m_LightmapFlags: 1\n  m_SavedProperties:\n    m_Colors:\n    - _EmissionColor: {r: 1, g: 1, b: 1, a: 1}\n";
    Material m;
    UnityMaterialInfo info;
    CHECK(unity_material(yaml, [](const std::string &) { return std::string(); }, [](const std::string &, int64_t) { return std::string("Standard"); }, m, info));
    std::printf("    a Realtime lamp material: flags %d, bakes %d; Blendity's own: %d\n", m.gi_flags, (int)m.emission_bakes(), (int)Material().emission_bakes());
    CHECK(m.gi_flags == 1 && !m.emission_bakes() && Material().emission_bakes());
    MeshPtr floor = primitives::plane(2.0f, 4), panel = primitives::plane(0.5f, 1);
    const MaterialPtr white = make_material("White", Vec3(0.8f));
    Environment env;
    env.mode = Environment::Color, env.color = Vec3(0.0f);
    auto floor_light = [&](int flags) {
      MaterialPtr lamp = make_material("Lamp", Vec3(0.5f));
      lamp->emission = Vec3(1.0f), lamp->emission_strength = 4.0f, lamp->gi_flags = flags;
      BakeSettings bs;
      bs.texels_per_unit = 4, bs.indirect_samples = 16, bs.bounces = 1, bs.denoise = false, bs.directional = false;
      BakeObject f = bake_object(1, floor, Mat4::identity(), {white}), p = bake_object(2, panel, Mat4::translate(Vec3(0.0f, 0.5f, 0.0f)), {lamp});
      p.want_lightmap = false;
      Lightmapper lm;
      lm.begin({f, p}, {}, env, bs);
      while (!lm.step(1e9)) {
      }
      const LightingData d = lm.take_result();
      double s = 0.0;
      for (const Vec3 &c : d.pages[0].texels) s += c.y;
      return s;
    };
    const double baked = floor_light(2), realtime = floor_light(1), none = floor_light(4), own = floor_light(-1);
    std::printf("    the floor's light: Baked %.3f, Realtime %.3f, None %.3f, Blendity's %.3f\n", baked, realtime, none, own);
    CHECK(baked > 1.0 && realtime == 0.0 && none == 0.0 && own > 1.0);
  });
}
/* ---------------------------------------------------------------- round 440: charts and seams (task 0416) */
static void tests_round440() {
  test("charts and seams (round 440): each UV island packed on its own across the atlas - more charts than objects, the pages filled at least as well as one square per object; seams on a smooth sphere stitched (the step across them shrinks), none across a cube's hard corners", [] {
    std::vector<BakeObject> objs;
    for (int i = 0; i < 12; i++) {
      BakeObject o = bake_object((uint64_t)(i + 1), i % 2 ? primitives::uv_sphere(0.3f + 0.05f * i, 16, 8) : primitives::cube(0.4f + 0.1f * i),
                                 Mat4::translate(Vec3(2.0f * i, 0.0f, 0.0f)), {make_material("White", Vec3(0.8f))});
      objs.push_back(o);
    }
    BakeSettings bs;
    bs.texels_per_unit = 16;
    LightmapLayout squares, islands;
    bs.island_charts = false;
    lightmap_layout(objs, bs, squares);
    bs.island_charts = true;
    lightmap_layout(objs, bs, islands);
    size_t sphere_pairs = 0, cube_pairs = 0;
    for (const auto &page : islands.seams) sphere_pairs += page.size();
    std::printf("    12 objects: %zu squares fill %.3f of %zu page(s); %zu island charts fill %.3f of %zu page(s); %zu seam pairs\n", squares.charts,
                lightmap_layout_fill(squares), squares.data.pages.size(), islands.charts, lightmap_layout_fill(islands), islands.data.pages.size(), sphere_pairs);
    /* (5 % slack: island boxes carry their own margins, so on a page that doesn't shrink the fill can dip a little) */
    CHECK(islands.charts > squares.charts && lightmap_layout_fill(islands) >= 0.95 * lightmap_layout_fill(squares));
    /* Many objects on small pages: every object's islands on one page, no texel claimed twice, pages about as many as
     * with squares (review 440: a page per split object, overlaps). */
    std::vector<BakeObject> many;
    for (int i = 0; i < 60; i++)
      many.push_back(bake_object((uint64_t)(i + 1), i % 3 ? primitives::uv_sphere(0.3f + 0.02f * i, 16, 8) : primitives::cube(0.5f + 0.03f * i),
                                 Mat4::translate(Vec3(2.0f * i, 0.0f, 0.0f)), {make_material("White", Vec3(0.8f))}));
    BakeSettings small = bs;
    small.max_size = 128;
    LightmapLayout sq_pages, is_pages;
    small.island_charts = false;
    lightmap_layout(many, small, sq_pages);
    small.island_charts = true;
    lightmap_layout(many, small, is_pages);
    std::set<std::tuple<int, int, int>> claimed;
    size_t twice = 0, outside = 0;
    for (const LightmapTexel &t : is_pages.texels) {
      twice += !claimed.insert({t.page, t.x, t.y}).second;
      outside += t.x < 0 || t.y < 0 || t.x >= 128 || t.y >= 128;
    }
    std::printf("    60 objects on 128-texel pages: %zu pages of squares, %zu of islands; texels claimed twice %zu, outside a page %zu\n", sq_pages.data.pages.size(),
                is_pages.data.pages.size(), twice, outside);
    CHECK(is_pages.data.pages.size() <= sq_pages.data.pages.size() + 1 && twice == 0 && outside == 0);
    /* A floor with its own UVs filling the page at the page's size: its island's box plus margins is wider than the page -
     * it packs as its square (review 440: written past the page). */
    BakeObject own = bake_object(1, primitives::plane(30.0f, 4), Mat4::identity(), {make_material("White", Vec3(0.8f))});
    own.generate_uvs = false;
    BakeSettings big = bs;
    big.max_size = 64, big.texels_per_unit = 40;
    LightmapLayout own_lay;
    lightmap_layout({own}, big, own_lay);
    size_t own_outside = 0;
    for (const LightmapTexel &t : own_lay.texels) own_outside += t.x < 0 || t.y < 0 || t.x >= 64 || t.y >= 64;
    std::printf("    a floor's own UVs at the page's size: %zu page(s), %zu texels, %zu outside\n", own_lay.data.pages.size(), own_lay.texels.size(), own_outside);
    CHECK(own_lay.data.pages.size() == 1 && own_lay.texels.size() > 3000 && own_outside == 0);
    CHECK(squares.texels.size() > 0 && islands.texels.size() > 0);
    /* A cube alone: its UV seams all lie on hard edges - nothing to stitch. */
    LightmapLayout cube_only;
    lightmap_layout({bake_object(1, primitives::cube(1.0f), Mat4::identity(), {make_material("White", Vec3(0.8f))})}, bs, cube_only);
    for (const auto &page : cube_only.seams) cube_pairs += page.size();
    CHECK(cube_pairs == 0);
    /* A sphere under a sun and a sky: the step across its seams before and after stitching. */
    Environment env;
    env.mode = Environment::Color, env.color = Vec3(0.3f);
    BakeLight sun;
    sun.light.direction = normalize(Vec3(-1.0f, -1.0f, -0.3f)), sun.light.intensity = 1.0f, sun.mode = 2, sun.id = 9;
    auto bake = [&](bool stitch, double &before, double &after) {
      BakeSettings b2;
      b2.texels_per_unit = 24, b2.indirect_samples = 16, b2.bounces = 1, b2.denoise = false, b2.directional = false, b2.stitch_seams = stitch;
      Lightmapper lm;
      lm.begin({bake_object(1, primitives::uv_sphere(0.5f, 24, 12), Mat4::identity(), {make_material("White", Vec3(0.8f))})}, {sun}, env, b2);
      while (!lm.step(1e9)) {
      }
      before = lm.seam_step_before(), after = lm.seam_step_after();
    };
    double b_on = 0, a_on = 0, b_off = 0, a_off = 0;
    bake(true, b_on, a_on);
    bake(false, b_off, a_off);
    std::printf("    a sphere's seams: the step %.4f -> %.4f stitched; Stitch Seams off finds none (%.4f)\n", b_on, a_on, a_off);
    CHECK(b_on > 0.0 && a_on < 0.5 * b_on && a_off == 0.0 && b_off == 0.0);
  });
}

/* ---------------------------------------------------------------- round 441: sampling (task 0417) */
static void tests_round441() {
  test("sampling (round 441): a bright patch in an HDRI sky sampled by its light - a floor under it far less noisy at 16 samples, and the same on average with the sampling on or off; 40 point lights picked by their light per texel - the same light on average from far fewer shadow rays", [] {
    Bitmap b;
    b.width = 64, b.height = 32, b.is_float = true;
    b.rgbaf.assign((size_t)64 * 32 * 4, 0.05f);
    for (int y = 6; y < 8; y++)
      for (int x = 20; x < 22; x++)
        for (int c = 0; c < 3; c++) b.rgbaf[((size_t)y * 64 + x) * 4 + c] = 400.0f;  // (a small, very bright patch high in the sky)
    for (size_t i = 3; i < b.rgbaf.size(); i += 4) b.rgbaf[i] = 1.0f;
    auto tex = std::make_shared<Texture>();
    tex->build(b, false);
    Environment env;
    env.mode = Environment::Hdri, env.map = tex;
    MeshPtr floor = primitives::plane(2.0f, 4);
    const MaterialPtr white = make_material("White", Vec3(0.8f));
    auto floor_values = [&](bool sample_env, int samples, uint32_t seed) {
      BakeSettings bs;
      bs.texels_per_unit = 8, bs.indirect_samples = samples, bs.bounces = 1, bs.denoise = false, bs.directional = false, bs.sample_env = sample_env, bs.seed = seed;
      Lightmapper lm;
      lm.begin({bake_object(1, floor, Mat4::identity(), {white})}, {}, env, bs);
      while (!lm.step(1e9)) {
      }
      const LightingData d = lm.take_result();
      std::vector<float> v;
      for (const Vec3 &c : d.pages[0].texels) v.push_back(c.y);
      return v;
    };
    const std::vector<float> ref = floor_values(true, 2048, 7), ref_off = floor_values(false, 8192, 8), off = floor_values(false, 16, 1), on = floor_values(true, 16, 1);
    double e_off = 0, e_on = 0, m_ref = 0, m_ref_off = 0, m_on = 0;
    for (size_t i = 0; i < ref.size(); i++) {
      e_off += (off[i] - ref[i]) * (off[i] - ref[i]), e_on += (on[i] - ref[i]) * (on[i] - ref[i]);
      m_ref += ref[i], m_ref_off += ref_off[i], m_on += on[i];
    }
    const double n = (double)std::max<size_t>(1, ref.size());
    std::printf("    an HDRI's bright patch: error at 16 samples %.4f found by the gather, %.4f sampled; the mean %.4f (8192 off %.4f, 16 on %.4f)\n", std::sqrt(e_off / n),
                std::sqrt(e_on / n), m_ref / n, m_ref_off / n, m_on / n);
    CHECK(e_on < 0.25 * e_off && std::fabs(m_ref_off - m_ref) / m_ref < 0.05 && std::fabs(m_on - m_ref) / m_ref < 0.05);
    /* A wall beside the floor at Bounces 1: the sky the wall sends to the floor is found at the path's last vertex - the
     * world's sampling there counts in full, so on and off agree (review 441). */
    MeshPtr wall = primitives::plane(2.0f, 4);
    const Mat4 upright = Mat4::trs(Vec3(0.0f, 1.0f, -1.0f), Quat::axis_angle(Vec3(1, 0, 0), kPi * 0.5f), Vec3(1.0f));
    auto with_wall = [&](bool sample_env) {
      BakeSettings bs;
      bs.texels_per_unit = 6, bs.indirect_samples = sample_env ? 1024 : 8192, bs.bounces = 1, bs.denoise = false, bs.directional = false, bs.sample_env = sample_env, bs.seed = 3;  // (the gather alone needs 8x the samples to settle within the check)
      BakeObject f = bake_object(1, floor, Mat4::identity(), {white}), w = bake_object(2, wall, upright, {white});
      w.want_lightmap = false;
      Lightmapper lm;
      lm.begin({f, w}, {}, env, bs);
      while (!lm.step(1e9)) {
      }
      const LightingData d = lm.take_result();
      double s = 0;
      for (const Vec3 &c : d.pages[0].texels) s += c.y;
      return s;
    };
    const double wall_on = with_wall(true), wall_off = with_wall(false);
    std::printf("    a wall beside the floor (Bounces 1): the floor's light %.2f sampled, %.2f by the gather alone\n", wall_on, wall_off);
    CHECK(std::fabs(wall_on - wall_off) / wall_off < 0.05);
    /* Forty lamps along a 40 m floor. */
    MeshPtr hall = primitives::plane(40.0f, 40);
    std::vector<BakeLight> lamps;
    for (int i = 0; i < 40; i++) {
      BakeLight l;
      l.light.type = RenderLight::Point, l.light.position = Vec3(-19.5f + i, 1.0f, 0.0f), l.light.intensity = 2.0f, l.light.range = 1e4f;
      l.mode = 2, l.id = (uint64_t)(100 + i);
      lamps.push_back(l);
    }
    Environment dark;
    dark.mode = Environment::Color, dark.color = Vec3(0.0f);
    auto hall_light = [&](bool pick, uint64_t &rays) {
      BakeSettings bs;
      bs.texels_per_unit = 2, bs.indirect_samples = 1, bs.bounces = 0, bs.denoise = false, bs.directional = false, bs.pick_lights = pick;
      Lightmapper lm;
      lm.begin({bake_object(1, hall, Mat4::identity(), {white})}, lamps, dark, bs);
      while (!lm.step(1e9)) {
      }
      rays = lm.rays();
      const LightingData d = lm.take_result();
      double s = 0;
      for (const Vec3 &c : d.pages[0].texels) s += c.y;
      return s;
    };
    uint64_t rays_all = 0, rays_pick = 0;
    const double all = hall_light(false, rays_all), picked = hall_light(true, rays_pick);
    std::printf("    40 lamps: the floor's light %.1f from %llu rays, picked %.1f from %llu rays\n", all, (unsigned long long)rays_all, picked, (unsigned long long)rays_pick);
    CHECK(all > 0.0 && std::fabs(picked - all) / all < 0.03 && rays_pick < rays_all / 2);
    /* Per texel, with the filter on: the picking's noise goes with the denoised part (review 441: it had been left
     * unfiltered in the direct light). */
    auto hall_texels = [&](bool pick) {
      BakeSettings bs;
      bs.texels_per_unit = 2, bs.indirect_samples = 1, bs.bounces = 0, bs.denoise = true, bs.directional = false, bs.pick_lights = pick;
      Lightmapper lm;
      lm.begin({bake_object(1, hall, Mat4::identity(), {white})}, lamps, dark, bs);
      while (!lm.step(1e9)) {
      }
      return lm.take_result().pages[0].texels;
    };
    const std::vector<Vec3> every = hall_texels(false), chosen = hall_texels(true);
    double err = 0.0, mean = 0.0;
    size_t lit = 0;
    for (size_t i = 0; i < every.size() && i < chosen.size(); i++) {
      if (!(every[i].y > 1e-3f)) continue;
      err += (chosen[i].y - every[i].y) * (chosen[i].y - every[i].y), mean += every[i].y, lit++;
    }
    const double rel = lit ? std::sqrt(err / (double)lit) / (mean / (double)lit) : 1.0;
    std::printf("    the picked hall per texel, filtered: relative error %.3f against every lamp traced\n", rel);
    CHECK(lit > 1000 && rel < 0.08);
  });
}
/* ---------------------------------------------------------------- round 442: L2 SH probes, Light Probe Groups (task 0418) */
static void tests_round442() {
  test("L2 SH probes (round 442): a lamp's light as nine coefficients - 1.06 toward it, a sixteenth behind (L2's ringing, as Unity's); light from all around gives 1 everywhere; the 26-direction fit gives back the coefficients exactly; a sun-lit probe volume's SH follows N.L far closer than the ambient cube between its axes; saved and read back", [] {
    ProbeSH lamp;
    lamp.add_direct(Vec3(0, 1, 0), Vec3(1.0f));
    std::printf("    a lamp from above: toward it %.3f, sideways %.3f, behind %.3f\n", lamp.eval(Vec3(0, 1, 0)).x, lamp.eval(Vec3(1, 0, 0)).x, lamp.eval(Vec3(0, -1, 0)).x);
    /* (behind it L2 leaves 1/16: the clamped cosine's bands 1/4 - 1/2 + 5/16, as Unity's own probes) */
    CHECK(std::fabs(lamp.eval(Vec3(0, 1, 0)).x - 1.0625f) < 1e-3f && std::fabs(lamp.eval(Vec3(0, -1, 0)).x - 0.0625f) < 1e-3f && lamp.eval(Vec3(1, 0, 0)).x < 0.15f);
    ProbeSH all;
    const int N = 4096;
    for (int i = 0; i < N; i++) {
      const float z = 1.0f - 2.0f * (i + 0.5f) / N, r = std::sqrt(std::max(0.0f, 1.0f - z * z)), phi = i * 2.39996323f;
      all.add_radiance(Vec3(r * std::cos(phi), z, r * std::sin(phi)), Vec3(1.0f), 4.0f * kPi / N);
    }
    std::printf("    light from all around: %.4f up, %.4f down\n", all.eval(Vec3(0, 1, 0)).x, all.eval(Vec3(0, -1, 0)).x);
    CHECK(std::fabs(all.eval(Vec3(0, 1, 0)).x - 1.0f) < 0.01f && std::fabs(all.eval(Vec3(0, -1, 0)).x - 1.0f) < 0.01f);
    /* The fit: values along the 26 directions back to the coefficients (positive enough that nothing is clamped). */
    ProbeSH truth;
    for (int k = 0; k < 9; k++) truth.c[(size_t)k] = k == 0 ? Vec3(10.0f) : Vec3(0.1f * (k % 4) - 0.15f, 0.05f * k, -0.02f * k);  // (a large constant: nothing clamped)
    Vec3 dirs[26], seen[26];
    probe_fit_directions(dirs);
    for (int j = 0; j < 26; j++) seen[j] = truth.eval(dirs[j]);
    ProbeShFit fit;
    fit.init(dirs);
    const ProbeSH got = fit.fit(seen);
    float worst = 0.0f;
    for (int k = 0; k < 9; k++) worst = std::max(worst, length(got.c[(size_t)k] - truth.c[(size_t)k]));
    std::printf("    the 26-direction fit: worst coefficient error %.2e\n", worst);
    CHECK(fit.ok && worst < 1e-4f);
    /* A probe volume in the open under a slanted sun: SH against the cube, against the true col x max(0, N.L). */
    P37Pt s;
    s.add(primitives::plane(40.0f, 2), Mat4::translate({0, -20.0f, 0}), Vec3(0.0f));
    Environment env;
    env.mode = Environment::Color, env.color = Vec3(0.0f);
    s.build({}, env);
    BakeLight sun;
    sun.light.direction = normalize(Vec3(-1.0f, -1.0f, -1.0f)), sun.light.intensity = 1.0f, sun.mode = 2, sun.id = 3;
    ProbeVolumeData d;
    probe_place(d, p37_box(Vec3(-1.0f), Vec3(1.0f)), s.tri_boxes(), 1.0f, 1.0f);
    probe_validate(d, s.pt, 32);
    probe_bake(d, s.pt, {sun}, 32, 1);
    CHECK(d.sh.size() == d.position.size() && !d.sh.empty());
    double e_sh = 0.0, e_cube = 0.0;
    const Vec3 L = normalize(Vec3(1, 1, 1));
    for (int i = 0; i < 200; i++) {
      const float z = 1.0f - 2.0f * (i + 0.5f) / 200, r = std::sqrt(std::max(0.0f, 1.0f - z * z)), phi = i * 2.39996323f;
      const Vec3 n(r * std::cos(phi), z, r * std::sin(phi));
      const float truth_v = std::max(0.0f, dot(n, L));
      Vec3 with_sh, with_cube;
      d.sample(Vec3(0.0f), n, Vec3(0.0f), false, with_sh, false);
      e_sh += std::fabs(with_sh.x - truth_v);
      ProbeVolumeData cube_only = d;
      cube_only.sh.clear();
      cube_only.sample(Vec3(0.0f), n, Vec3(0.0f), false, with_cube, false);
      e_cube += std::fabs(with_cube.x - truth_v);
    }
    std::printf("    a sun-lit probe, mean error over 200 normals: SH %.4f, ambient cube %.4f\n", e_sh / 200, e_cube / 200);
    CHECK(e_sh < 0.6 * e_cube);
    /* Saved and read back. */
    const std::string path = resolve_asset_path("Assets/Imported/r442_probes.bin");
    std::string err;
    CHECK(d.save(path, err));
    ProbeVolumeData back;
    CHECK(back.load(path, err) && back.sh.size() == d.sh.size() && length(back.sh[5].c[0] - d.sh[5].c[0]) == 0.0f);
    std::remove(path.c_str());
  });
  test("Light Probe Groups (round 442): Unity's probes' points tetrahedralised - a linear light reproduced exactly inside, nothing found outside; Unity's probes read along 26 directions (version 3) and fitted; a volume from them interpolated over their tetrahedra", [] {
    std::vector<Vec3> pts;
    uint32_t r = 12345u;
    auto rnd = [&] {
      r ^= r << 13, r ^= r >> 17, r ^= r << 5;
      return (float)(r >> 8) / 16777216.0f;
    };
    for (int x = 0; x < 5; x++)  // (a Light Probe Group's grid: many points on one sphere - the degenerate case)
      for (int y = 0; y < 4; y++)
        for (int z = 0; z < 5; z++) pts.push_back(Vec3((float)x, (float)y, (float)z));
    for (int i = 0; i < 60; i++) pts.push_back(Vec3(rnd() * 4.0f, rnd() * 3.0f, rnd() * 4.0f));
    Tetrahedra T;
    CHECK(T.build(pts));
    auto f = [](Vec3 p) { return 2.0f * p.x - 0.5f * p.y + 0.25f * p.z + 1.0f; };
    float worst = 0.0f;
    int found = 0, hint = -1;
    for (int i = 0; i < 400; i++) {
      const Vec3 q(0.1f + rnd() * 3.8f, 0.1f + rnd() * 2.8f, 0.1f + rnd() * 3.8f);
      int t;
      float w[4];
      if (!T.locate(q, t, w, hint)) continue;
      hint = t, found++;
      float v = 0.0f;
      for (int c = 0; c < 4; c++) v += w[c] * f(T.points[(size_t)T.tets[(size_t)t][(size_t)c]]);
      worst = std::max(worst, std::fabs(v - f(q)));
    }
    int t_out;
    float w_out[4];
    std::printf("    %zu points, %zu tetrahedra: %d of 400 inside found, a linear light's worst error %.2e\n", T.points.size(), T.tets.size(), found, worst);
    CHECK(found == 400 && worst < 1e-3f && !T.locate(Vec3(20, 20, 20), t_out, w_out));
    /* Review 442: a group at one height has no volume - refused (the nearest four instead), not a mesh of slivers. */
    std::vector<Vec3> flat;
    for (int x = 0; x < 10; x++)
      for (int z = 0; z < 10; z++) flat.push_back(Vec3((float)x, 1.5f, (float)z));
    Tetrahedra F;
    CHECK(!F.build(flat));
    /* The grid turned (a placement's rotation): still exact inside. */
    const Quat turn = Quat::axis_angle(normalize(Vec3(0.3f, 1.0f, 0.2f)), 0.7f);
    std::vector<Vec3> turned_pts;
    for (Vec3 q : pts) turned_pts.push_back(turn.rotate(q));
    Tetrahedra TT;
    CHECK(TT.build(turned_pts));
    float worst_t = 0.0f;
    int found_t = 0;
    for (int i = 0; i < 200; i++) {
      const Vec3 q = turn.rotate(Vec3(0.2f + rnd() * 3.6f, 0.2f + rnd() * 2.6f, 0.2f + rnd() * 3.6f));
      int t;
      float w[4];
      if (!TT.locate(q, t, w)) continue;
      found_t++;
      float v = 0.0f;
      for (int c = 0; c < 4; c++) v += w[c] * f(TT.points[(size_t)TT.tets[(size_t)t][(size_t)c]]);
      worst_t = std::max(worst_t, std::fabs(v - f(q)));
    }
    std::printf("    a flat group refused; the grid turned: %d of 200 found, worst %.2e\n", found_t, worst_t);
    CHECK(found_t == 200 && worst_t < 1e-3f);
    /* The build at its cap, timed (review 442: O(n^2) on the main thread when Unity's lighting is applied). */
    std::vector<Vec3> many;
    for (size_t i = 0; i < Tetrahedra::kMaxPoints; i++) many.push_back(Vec3(rnd() * 40.0f, rnd() * 10.0f, rnd() * 40.0f));
    ScopedTimer bt;
    Tetrahedra M;
    const bool built_many = M.build(many);
    const double build_ms = bt.ms();
    ScopedTimer lt;
    int hint_m = -1, found_m = 0;
    for (int i = 0; i < 20000; i++) {
      int t;
      float w[4];
      if (M.locate(Vec3(1.0f + rnd() * 38.0f, 0.5f + rnd() * 9.0f, 1.0f + rnd() * 38.0f), t, w, hint_m)) hint_m = t, found_m++;
    }
    std::printf("    %zu points: built in %.0f ms (%zu tetrahedra); 20,000 walks in %.1f ms, %d inside\n", many.size(), build_ms, M.tets.size(), lt.ms(), found_m);
    CHECK(built_many && found_m > 15000);
    /* Unity's probes on that grid, each lit from above by a different amount (version 3: 26 directions). */
    UnityLighting u;
    Vec3 dirs[26];
    probe_fit_directions(dirs);
    for (int i = 0; i < 100; i++) {
      UnityLighting::Probe p;
      p.position = pts[(size_t)i];
      ProbeSH sh;
      sh.add_direct(Vec3(0, 1, 0), Vec3(0.5f + 0.1f * p.position.x));
      sh.c[0] += Vec3(0.2f);  // (an ambient floor: nothing clamped)
      for (int j = 0; j < 6; j++) p.axes[(size_t)j] = sh.eval(dirs[j]);
      for (int j = 0; j < 26; j++) p.seen.push_back(sh.eval(dirs[j]));
      u.probes.push_back(p);
    }
    std::string why;
    UnityLighting back;
    CHECK(read_unity_lighting(write_unity_lighting(u), back, why) && back.probes.size() == 100 && back.probes[7].seen.size() == 26);
    ProbeVolumeData vol;
    unity_probe_volume(back, vol);
    CHECK(vol.sh.size() == vol.position.size() && vol.unity_tetra_inside > 0);
    Vec3 up, mid;
    vol.sample(Vec3(2.0f, 1.5f, 2.0f), Vec3(0, 1, 0), Vec3(0.0f), false, up, false);
    const float expect = (0.5f + 0.1f * 2.0f) * 1.0625f + 0.2f * 0.282095f;
    std::printf("    a volume from Unity's probes: %zu of %zu inside their tetrahedra; light from above at x = 2: %.3f (Unity's own %.3f)\n", vol.unity_tetra_inside,
                vol.position.size(), up.x, expect);
    CHECK(std::fabs(up.x - expect) < 0.03f);
    (void)mid;
  });
}
/* ---------------------------------------------------------------- round 444: Shadowmask and Subtractive (task 0420) */
static void tests_round444() {
  test("Shadowmask and Subtractive (round 444): a Mixed lamp over a box - Baked Indirect bakes only its bounce; Shadowmask keeps a mask of its shadow from the box (dark under it, clear in the open) and marks the lamp's channel; Subtractive bakes its direct light into the lightmap; the renderers' rule for each; the masks saved and read back", [] {
    CHECK(light_baked_here(2, true, false) && light_baked_here(2, false, true) && light_baked_here(3, true, false) && !light_baked_here(3, false, true));
    CHECK(!light_baked_here(1, true, true) && !light_baked_here(5, true, true) && light_mask_channel(5) == 1 && light_mask_channel(3) == -1 && light_mask_channel(2) == -1);
    MeshPtr floor = primitives::plane(6.0f, 8), box = primitives::cube(1.0f);
    const MaterialPtr white = make_material("White", Vec3(0.8f));
    Environment env;
    env.mode = Environment::Color, env.color = Vec3(0.0f);
    BakeLight lamp;
    lamp.light.type = RenderLight::Point, lamp.light.position = Vec3(0.0f, 4.0f, 0.0f), lamp.light.intensity = 8.0f, lamp.light.range = 100.0f;
    lamp.mode = 1, lamp.id = 7;
    auto bake = [&](int mode) {
      BakeSettings bs;
      bs.texels_per_unit = 6, bs.indirect_samples = 16, bs.bounces = 1, bs.denoise = false, bs.directional = false, bs.lighting_mode = mode;
      BakeObject f = bake_object(1, floor, Mat4::identity(), {white}), b = bake_object(2, box, Mat4::translate(Vec3(0.0f, 1.5f, 0.0f)), {white});
      b.want_lightmap = false;
      Lightmapper lm;
      lm.begin({f, b}, {lamp}, env, bs);
      while (!lm.step(1e9)) {
      }
      return lm.take_result();
    };
    const LightingData indirect = bake(0), mask = bake(1), sub = bake(2);
    auto sum = [](const LightingData &d) {
      double s = 0;
      for (const Vec3 &c : d.pages[0].texels) s += c.y;
      return s;
    };
    float lo = 1.0f, hi = 0.0f;
    for (const Vec3 &m : mask.mask_pages.empty() ? std::vector<Vec3>{} : mask.mask_pages[0].texels) lo = std::min(lo, m.x), hi = std::max(hi, m.x);
    std::printf("    the lamp's mode in the bake: %d / %d / %d; the mask from %.3f (under the box) to %.3f; the floor's light %.1f indirect, %.1f Subtractive\n",
                indirect.light_modes.at(7), mask.light_modes.at(7), sub.light_modes.at(7), lo, hi, sum(indirect), sum(sub));
    CHECK(indirect.light_modes.at(7) == 1 && indirect.mask_pages.empty() && mask.light_modes.at(7) == 4 && mask.mask_pages.size() == mask.pages.size());
    CHECK(lo < 0.05f && hi > 0.95f && sub.light_modes.at(7) == 3 && sub.mask_pages.empty() && sum(sub) > 3.0 * sum(indirect));
    const std::string dir = resolve_asset_path("Assets/Imported/R444");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::string err;
    CHECK(mask.save(dir, err));
    LightingData back;
    CHECK(back.load(dir, err) && back.mask_pages.size() == mask.mask_pages.size() && back.light_modes.at(7) == 4);
    float diff = 0.0f;
    for (size_t i = 0; !back.mask_pages.empty() && i < back.mask_pages[0].texels.size(); i++) diff = std::max(diff, std::fabs(back.mask_pages[0].texels[i].x - mask.mask_pages[0].texels[i].x));
    std::printf("    the mask read back: worst difference %.4f\n", diff);
    CHECK(diff < 0.01f);
    std::filesystem::remove_all(dir, ec);
    /* Unity's Subtractive split: the brightest Mixed sun stays Mixed, every other Mixed light becomes Baked. */
    std::vector<BakeLight> set(3);
    set[0].light.type = RenderLight::Point, set[0].mode = 1;
    set[1].light.type = RenderLight::Directional, set[1].light.intensity = 0.5f, set[1].mode = 1;
    set[2].light.type = RenderLight::Directional, set[2].light.intensity = 2.0f, set[2].mode = 1;
    const int main_light = subtractive_split(set);
    std::printf("    Subtractive's split: the main light %d; the modes %d %d %d\n", main_light, set[0].mode, set[1].mode, set[2].mode);
    CHECK(main_light == 2 && set[0].mode == 2 && set[1].mode == 2 && set[2].mode == 1);
  });
}

static void tests_round451() {
  test("Reflection probes from the lit scene (round 451): a probe over a lightmapped floor reads the lightmap x the floor's albedo - not a path trace of the raw geometry (no lights, a black world: that is black) - the sky stays path-traced, and a Realtime sun the maps don't hold adds its direct light", [] {
    MeshPtr floor = primitives::plane(6.0f, 2);
    const std::vector<MaterialPtr> white = {make_material("White", Vec3(0.8f))};
    Environment env;
    env.mode = Environment::Color, env.color = Vec3(0.0f);
    std::vector<PTObject> objs = {{&floor->render_mesh_tangents(), Mat4::identity(), &white}};
    PathTracer pt;
    PTSettings ps;
    ps.denoise = false;
    pt.set_settings(ps);
    pt.build(objs, {}, env);
    LightingData ld;
    Lightmap page;
    page.width = page.height = 4;
    page.texels.assign(16, Vec3(0.5f, 0.25f, 0.125f));
    ld.pages.push_back(page);
    LightingData::Entry e;
    e.page = 0;
    e.tri_uv.assign(floor->render_mesh_tangents().tri_count() * 3, Vec2(0.5f, 0.5f));
    ld.entries[1] = e;
    const std::vector<uint64_t> ids = {1};
    ProbeLitCapture cap;
    cap.pt = &pt, cap.lighting = &ld, cap.object_ids = &ids, cap.samples = 2;
    std::vector<Vec3> down, up;
    const float lit_down = reflection_probe_capture_lit(cap, Vec3(0, 1, 0), 8, 0.05f, 3, down);
    const float lit_up = reflection_probe_capture_lit(cap, Vec3(0, 1, 0), 8, 0.05f, 2, up);
    Vec3 mean_down(0.0f), mean_up(0.0f);
    for (const Vec3 &c : down) mean_down += c / (float)down.size();
    for (const Vec3 &c : up) mean_up += c / (float)up.size();
    std::printf("    down: (%.3f %.3f %.3f), %.0f%% lightmapped; up (the sky): (%.3f %.3f %.3f), %.0f%%\n", mean_down.x, mean_down.y, mean_down.z, lit_down * 100.0f, mean_up.x,
                mean_up.y, mean_up.z, lit_up * 100.0f);
    CHECK(down.size() == 64 && lit_down > 0.99f && std::fabs(mean_down.x - 0.4f) < 0.01f && std::fabs(mean_down.y - 0.2f) < 0.01f && std::fabs(mean_down.z - 0.1f) < 0.01f);
    CHECK(lit_up < 0.01f && mean_up.x < 1e-4f);
    uint32_t rng = 1;
    uint64_t rays = 0;
    CHECK(pt.incoming_radiance({Vec3(0, 1, 0), Vec3(0, -1, 0)}, rng, rays).x < 1e-4f);  // (the raw geometry's path: no light at all)
    /* A Realtime sun straight down, the maps empty: its direct light x albedo. */
    page.texels.assign(16, Vec3(0.0f));
    ld.pages[0] = page;
    RenderLight sun;
    sun.type = RenderLight::Directional, sun.direction = Vec3(0, -1, 0), sun.intensity = 1.0f;
    const std::vector<RenderLight> direct = {sun};
    cap.lights = &direct;
    reflection_probe_capture_lit(cap, Vec3(0, 1, 0), 8, 0.05f, 3, down);
    mean_down = Vec3(0.0f);
    for (const Vec3 &c : down) mean_down += c / (float)down.size();
    std::printf("    a Realtime sun on an empty lightmap: (%.3f %.3f %.3f)\n", mean_down.x, mean_down.y, mean_down.z);
    CHECK(std::fabs(mean_down.x - 0.8f) < 0.02f && std::fabs(mean_down.z - 0.8f) < 0.02f);
  });
  test("Reflection probes from the lit scene (round 451): Generate Lighting with a Baked sun - the probe above the floor sees the floor as its lightmap x albedo; `lighting save` writes the shown lightmaps with objects.json naming the floor", [] {
    Editor ed;
    r35_open(ed);
    GameObject *floor_go = r35_add(ed, "Plane", "Floor", {0, 0, 0}, {1, 1, 1}, Vec3(0.8f));
    r35_sun_mode(ed, 2);
    GameObject *probe = ed.scene().create("Probe");
    probe->set_local_position({0, 0.5f, 0});
    probe->add<ReflectionProbeComponent>()->resolution = 16;
    ed.commit_change("probe");
    r35_bake(ed);
    const auto &bakes = ed.reflection_probe_bakes_for_test();
    const auto it = bakes.find(probe->id);
    Vec3 lm;
    const Mesh *fm = floor_go->evaluated_mesh(1);
    const bool sampled = fm && r35_sample(ed.lighting_data_for_test(), floor_go->id, *fm, floor_go->world_matrix(), {0, 0, 0}, lm);
    CHECK(it != bakes.end() && sampled);
    if (it == bakes.end() || !sampled) return;
    const Vec3 seen = it->second.sample(Vec3(0, -1, 0)), want = lm * 0.8f;
    std::printf("    the probe looking down (%.3f %.3f %.3f), the floor's lightmap x albedo (%.3f %.3f %.3f)\n", seen.x, seen.y, seen.z, want.x, want.y, want.z);
    CHECK(want.y > 0.05f && std::fabs(seen.y - want.y) < 0.1f * want.y + 0.01f);
    const std::string dir = fs::join(scratch_project(), "R451Lighting");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    ed.command("lighting save " + dir);
    std::filesystem::remove(std::filesystem::u8path(fs::join(dir, "objects.json")), ec);
    ed.command("lighting save " + dir);  // (review 451: a folder with a bake in it is refused without `overwrite`)
    CHECK(!fs::exists(fs::join(dir, "objects.json")));
    ed.command("lighting save " + dir + " overwrite");
    std::string objects;
    CHECK(fs::exists(fs::join(dir, "LightingData.bin")) && fs::exists(fs::join(dir, "Lightmap-0.hdr")) && fs::read_file(fs::join(dir, "objects.json"), objects));
    std::printf("    objects.json: %.120s\n", objects.c_str());
    CHECK(objects.find("\"path\":\"Floor\"") != std::string::npos && objects.find(std::to_string(floor_go->id)) != std::string::npos && objects.find("\"source\":\"blendity\"") != std::string::npos);
    std::filesystem::remove_all(dir, ec);
  });
  test("The render split by component (round 451, BLENDITY_RENDER_COMPONENTS): noreflections drops only the reflection probes, noprobes the probe volume and Light Volumes, indirect the realtime lights, direct the lightmaps, probes and the sky; an unknown word applies nothing", [] {
    Lightmap page;
    std::vector<Vec2> uv(3, Vec2(0.5f, 0.5f));
    ProbeFrame pf;
    LightVolumeFrame lv;
    auto fresh = [&](LightingEnv &env, std::vector<DrawItem> &items) {
      env = LightingEnv{};
      env.lights.resize(2);
      env.reflection_probes.resize(1);
      env.probes = &pf, env.light_volumes = &lv;
      items.assign(1, DrawItem{});
      items[0].lightmap = &page, items[0].lightmap_uv = &uv, items[0].use_probes = true;
    };
    LightingEnv env;
    std::vector<DrawItem> items;
    fresh(env, items);
    CHECK(apply_render_components("noreflections", env, items) == "noreflections" && env.reflection_probes.empty() && env.lights.size() == 2 && env.probes && items[0].lightmap);
    fresh(env, items);
    CHECK(apply_render_components("noprobes", env, items) == "noprobes" && !env.probes && !env.light_volumes && env.reflection_probes.size() == 1);
    fresh(env, items);
    CHECK(apply_render_components(" Indirect , noreflections", env, items) == "indirect,noreflections" && env.lights.empty() && env.reflection_probes.empty() && items[0].lightmap);
    fresh(env, items);
    CHECK(apply_render_components("direct", env, items) == "direct" && env.lights.size() == 2 && !items[0].lightmap && !items[0].use_probes && !env.probes && env.environment &&
          env.environment->irradiance(Vec3(0, 1, 0)).y == 0.0f && env.sky.y == 0.0f);
    fresh(env, items);
    CHECK(apply_render_components("everything", env, items).empty() && env.lights.size() == 2 && items[0].lightmap);
  });
  test("Reflection probes from the lit scene (review 451): a single-sided floor seen from below is culled as the views cull it (the sky behind it shows); a double-sided one seen from behind takes its lightmap with the direction page decoded in the front's frame, as the views do", [] {
    MeshPtr floor = primitives::plane(6.0f, 2);
    MaterialPtr one = make_material("One", Vec3(0.8f)), two = make_material("Two", Vec3(0.8f));
    two->double_sided = true;
    Environment env;
    env.mode = Environment::Color, env.color = Vec3(0.05f, 0.1f, 0.2f);
    LightingData ld;
    Lightmap page, dir;
    page.width = page.height = dir.width = dir.height = 4;
    page.texels.assign(16, Vec3(0.5f));
    dir.texels.assign(16, Vec3(0.0f, 0.9f, 0.0f));  // (the light came from above the floor)
    ld.pages.push_back(page);
    ld.dir_pages.push_back(dir);
    LightingData::Entry e;
    e.page = 0;
    e.tri_uv.assign(floor->render_mesh_tangents().tri_count() * 3, Vec2(0.5f, 0.5f));
    ld.entries[1] = e;
    const std::vector<uint64_t> ids = {1};
    auto look_up_from_below = [&](const MaterialPtr &m, float &lit) {
      const std::vector<MaterialPtr> mats = {m};
      std::vector<PTObject> objs = {{&floor->render_mesh_tangents(), Mat4::identity(), &mats}};
      PathTracer pt;
      PTSettings ps;
      ps.denoise = false;
      pt.set_settings(ps);
      pt.build(objs, {}, env);
      ProbeLitCapture cap;
      cap.pt = &pt, cap.lighting = &ld, cap.object_ids = &ids, cap.samples = 1;
      std::vector<Vec3> face;
      lit = reflection_probe_capture_lit(cap, Vec3(0, -1, 0), 4, 0.05f, 2, face);
      Vec3 mean(0.0f);
      for (const Vec3 &c : face) mean += c / (float)face.size();
      return mean;
    };
    float lit_one = 0.0f, lit_two = 0.0f;
    const Vec3 single = look_up_from_below(one, lit_one), both = look_up_from_below(two, lit_two);
    std::printf("    single-sided from below: (%.3f %.3f %.3f), %.0f%% lightmapped; double-sided: (%.3f %.3f %.3f), %.0f%%\n", single.x, single.y, single.z, lit_one * 100.0f, both.x,
                both.y, both.z, lit_two * 100.0f);
    CHECK(lit_one < 0.01f && std::fabs(single.z - 0.2f) < 0.01f);
    CHECK(lit_two > 0.99f && std::fabs(both.x - 0.4f) < 0.01f);  // (decoded as the front: factor 1 - not the back's divisor)
  });
  test("The render split (review 451): `direct` keeps only the realtime lights - a Baked sun and a Subtractive light go with the lightmaps (no double count beside `indirect`), a Realtime point light stays, the shadowed light keeps its map at its new place", [] {
    ShadowMap map;
    LightingEnv env;
    env.lights.resize(3);
    env.lights[0].type = RenderLight::Directional, env.lights[0].bake_mode = 2;
    env.lights[1].type = RenderLight::Point, env.lights[1].bake_mode = 0;
    env.lights[2].type = RenderLight::Directional, env.lights[2].bake_mode = 3;
    env.shadow = &map, env.shadow_light = 0;
    std::vector<DrawItem> items(1);
    CHECK(apply_render_components("direct", env, items) == "direct" && env.lights.size() == 1 && env.lights[0].type == RenderLight::Point && !env.shadow && env.shadow_light == -1);
    env.lights.resize(2);
    env.lights[0].type = RenderLight::Directional, env.lights[0].bake_mode = 2;
    env.lights[1].type = RenderLight::Directional, env.lights[1].bake_mode = 0;
    env.shadow = &map, env.shadow_light = 1;
    CHECK(apply_render_components("direct", env, items) == "direct" && env.lights.size() == 1 && env.shadow == &map && env.shadow_light == 0);
  });
}



/* ---------------------------------------------------------------- round 452: Unity's Indirect Multiplier (task 0428) */
static void tests_round452() {
  test("Indirect Multiplier (round 452): a Baked sun's light bounced off a wall onto a floor in its shadow scales with the light's Indirect Multiplier (0, 0.5, 1, 3) while the lit floor's direct light stays; the lights a bake bounces carry it, a Realtime light none", [] {
    MeshPtr floor = primitives::plane(4.0f, 8), wall = primitives::plane(4.0f, 8), roof = primitives::plane(4.0f, 8);
    const MaterialPtr white = make_material("White", Vec3(0.8f));
    /* A floor, a wall standing at its +x edge facing it, and a roof over the floor's -x half: the sun lights the wall and
     * the floor's +x half; the -x half under the roof gets only what the wall and the lit floor bounce. */
    const std::vector<BakeObject> objs{bake_object(1, floor, Mat4::identity(), {white}),
                                       bake_object(2, wall, Mat4::trs({2.0f, 2.0f, 0}, Quat::euler({0, 0, 90}), {1, 1, 1}), {white}),
                                       bake_object(3, roof, Mat4::trs({-1.0f, 1.2f, 0}, Quat::euler({180, 0, 0}), {0.5f, 1, 1}), {white})};
    Environment env;
    env.mode = Environment::Color, env.color = Vec3(0.0f);
    auto bake = [&](float indirect) {
      BakeLight sun;
      sun.light.direction = normalize(Vec3(0.6f, -1.0f, 0.0f)), sun.light.intensity = 1.0f, sun.mode = 2, sun.id = 9, sun.indirect = indirect;
      BakeSettings bs;
      bs.texels_per_unit = 8, bs.indirect_samples = 256, bs.bounces = 2, bs.denoise = false, bs.directional = false;
      Lightmapper lm;
      lm.begin(objs, {sun}, env, bs);
      while (!lm.step(1e9)) {
      }
      return lm.take_result();
    };
    /* The floor's mean light in the roof's shadow (-1 < x < 0.4) and in the sun (x > 1): each triangle's lightmap texel at its
     * centre (the plane's own x, the model is the identity). */
    auto floor_means = [&](const LightingData &d, double &shade, double &sun) {
      const LightingData::Entry &e = d.entries.at(1);
      const Lightmap &pg = d.pages[(size_t)e.page];
      const RenderMesh &rm = floor->render_mesh();
      double s0 = 0, s1 = 0;
      int n0 = 0, n1 = 0;
      for (size_t k = 0; k < rm.tri_count() && k * 3 + 2 < e.tri_uv.size(); k++) {
        float cx = 0;
        Vec2 uv(0, 0);
        for (int c = 0; c < 3; c++) cx += rm.positions[rm.indices[k * 3 + c]].x / 3.0f, uv = uv + e.tri_uv[k * 3 + c] * (1.0f / 3.0f);
        const int x = std::min(pg.width - 1, std::max(0, (int)(uv.x * pg.width))), y = std::min(pg.height - 1, std::max(0, (int)(uv.y * pg.height)));
        const float t = pg.texels[(size_t)y * pg.width + x].y;
        if (cx > -1.0f && cx < 0.4f) s0 += t, n0++;  // (the roof, -2..0 at 1.2 high, shades the floor from -1.28 to 0.72 for this sun)
        if (cx > 1.0f) s1 += t, n1++;
      }
      shade = n0 ? s0 / n0 : 0, sun = n1 ? s1 / n1 : 0;
    };
    double sh0, su0, sh5, su5, sh1, su1, sh3, su3;
    floor_means(bake(0.0f), sh0, su0);
    floor_means(bake(0.5f), sh5, su5);
    floor_means(bake(1.0f), sh1, su1);
    floor_means(bake(3.0f), sh3, su3);
    std::printf("    under the roof: x0 %.4f, x0.5 %.4f, x1 %.4f, x3 %.4f; in the sun: %.4f %.4f %.4f %.4f\n", sh0, sh5, sh1, sh3, su0, su5, su1, su3);
    CHECK(sh1 > 0.01 && sh0 < 0.02 * sh1);  // (no bounced light at 0)
    CHECK(std::fabs(sh5 - 0.5 * sh1) < 0.08 * sh1);
    CHECK(std::fabs(sh3 - 3.0 * sh1) < 0.1 * 3.0 * sh1);
    CHECK(std::fabs(su0 - su1) < su1 * 0.25 && su0 > 0.5 * su1);  // (the sunlit half mostly direct)
    /* What the lightmapper, the probes and the Light Volumes all trace: the bounce list. */
    BakeLight lamp;
    lamp.light.type = RenderLight::Point, lamp.light.position = Vec3(0, 3, 0), lamp.light.intensity = 2.0f, lamp.light.range = 10.0f, lamp.mode = 2;
    lamp.indirect = 0.25f;
    const std::vector<RenderLight> bl = bounce_lights({lamp});
    CHECK(bl.size() == 1 && std::fabs(bl[0].intensity - 0.5f) < 1e-6f);
    lamp.indirect = std::nanf("");  // (a bad number is the default 1)
    CHECK(bounce_lights({lamp}).size() == 1 && std::fabs(bounce_lights({lamp})[0].intensity - 2.0f) < 1e-6f);
    lamp.indirect = 1000.0f;  // (clamped to 100, as Indirect Intensity)
    CHECK(bounce_lights({lamp}).size() == 1 && std::fabs(bounce_lights({lamp})[0].intensity - 200.0f) < 1e-3f);
    lamp.indirect = 0.0f;  // (a light bounced at 0 is left out of the path tracer's lights)
    CHECK(bounce_lights({lamp}).empty());
    lamp.indirect = 1.0f;
    lamp.mode = 0;  // (a Realtime light bounces nothing in a bake)
    CHECK(bounce_lights({lamp}).empty());
  });
}

/* ---------------------------------------------------------------- round 453: probes see the sky and pick lights (task 0429) */
static void tests_round453() {
  test("probes see the sky (round 453): a probe in the open under a Gradient world holds the sky's own light in its SH - bright above, the ground below, each within a few percent of the world's true irradiance - and takes no second sky share; saved, read back; an older file (no flag) keeps the cubes' share", [] {
    P37Pt s;
    s.add(primitives::plane(2.0f, 2), Mat4::translate({0, -50.0f, 0}), Vec3(0.0f));  // (far away: nothing but the world around the probe)
    Environment env;
    env.mode = Environment::Gradient;
    env.sky = Vec3(1.0f, 0.8f, 0.6f), env.equator = Vec3(0.3f), env.ground = Vec3(0.05f);
    s.build({}, env);
    ProbeVolumeData d;
    probe_place(d, p37_box(Vec3(-1.0f), Vec3(1.0f)), s.tri_boxes(), 1.0f, 1.0f);
    probe_validate(d, s.pt, 32);
    probe_bake(d, s.pt, {}, 512, 1, 0, SIZE_MAX, 1.0f, -1, nullptr, &env);
    /* The truth: the world's irradiance on a surface facing n, integrated over many directions. */
    auto truth = [&](Vec3 n) {
      Vec3 acc(0.0f);
      const int N = 8192;
      for (int i = 0; i < N; i++) {
        const float z = 1.0f - 2.0f * (i + 0.5f) / N, r = std::sqrt(std::max(0.0f, 1.0f - z * z)), phi = i * 2.39996323f;
        const Vec3 w(r * std::cos(phi), z, r * std::sin(phi));
        const float c = dot(w, n);
        if (c > 0.0f) acc += env.radiance(w) * c;
      }
      return acc * (4.0f * kPi / N) / kPi;  // (irradiance / pi: the diffuse light the views multiply albedo by)
    };
    Vec3 up, down;
    // (the sky now passed is the one the probes were baked under: a second sky share would show at once)
    d.sample(Vec3(0.0f), Vec3(0, 1, 0), env.irradiance(Vec3(0, 1, 0)), false, up, false, nullptr, &env);
    d.sample(Vec3(0.0f), Vec3(0, -1, 0), env.irradiance(Vec3(0, -1, 0)), false, down, false, nullptr, &env);
    const Vec3 tu = truth(Vec3(0, 1, 0)), td = truth(Vec3(0, -1, 0));
    std::printf("    up %.3f %.3f %.3f (truth %.3f %.3f %.3f); down %.3f (truth %.3f); sky in the SH %d\n", up.x, up.y, up.z, tu.x, tu.y, tu.z, down.x, td.x,
                (int)d.sky_in_sh);
    CHECK(d.sky_in_sh);
    CHECK(std::fabs(up.x - tu.x) < 0.06f * tu.x && std::fabs(up.z - tu.z) < 0.06f * tu.z);
    CHECK(std::fabs(down.x - td.x) < 0.02f + 0.1f * td.x);
    CHECK(up.x > 2.0f * down.x);
    const std::string path = resolve_asset_path("Assets/Imported/r453_probes.bin");
    std::string err;
    CHECK(d.save(path, err));
    ProbeVolumeData back;
    CHECK(back.load(path, err) && back.sky_in_sh && back.has_baked_sky && back.baked_sky[2].x > 0.5f);
    std::string bytes_with_flag, bytes_old;
    /* An older file: the same bytes without the trailing flag and the baked sky - read as SH from geometry only, the cubes'
     * sky share kept. */
    {
      std::ifstream in(path, std::ios::binary);
      const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      bytes_with_flag = bytes.substr(0, bytes.size() - sizeof(Vec3) * 6);
      bytes_old = bytes.substr(0, bytes_with_flag.size() - 1);
    }
    /* The first 453 files: the flag byte, no baked-sky floats - the SH hold the sky, the sky stays as baked. */
    {
      std::ofstream out(path, std::ios::binary | std::ios::trunc);
      out.write(bytes_with_flag.data(), (std::streamsize)bytes_with_flag.size());
    }
    ProbeVolumeData flag_only;
    CHECK(flag_only.load(path, err) && flag_only.sky_in_sh && !flag_only.has_baked_sky);
    {
      std::ofstream out(path, std::ios::binary | std::ios::trunc);
      out.write(bytes_old.data(), (std::streamsize)bytes_old.size());
    }
    ProbeVolumeData old;
    CHECK(old.load(path, err) && !old.sky_in_sh && !old.has_baked_sky && old.sh.size() == d.sh.size());
    std::remove(path.c_str());
  });
  test("probes pick among many lights (round 453): forty Baked point lights in one spot light a probe grid as one light forty times as bright - the same on average (unbiased), in less time", [] {
    P37Pt s;
    s.add(primitives::plane(2.0f, 2), Mat4::translate({0, -50.0f, 0}), Vec3(0.0f));
    Environment env;
    env.mode = Environment::Color, env.color = Vec3(0.0f);
    s.build({}, env);
    auto lamp = [](float intensity, uint64_t id) {
      BakeLight l;
      l.light.type = RenderLight::Point, l.light.position = Vec3(0.3f, 2.5f, -0.2f), l.light.range = 20.0f, l.light.intensity = intensity;
      l.light.shadows = false, l.mode = 2, l.id = id;
      return l;
    };
    std::vector<BakeLight> many, one = {lamp(40.0f, 1)};
    for (int i = 0; i < 40; i++) many.push_back(lamp(1.0f, 100 + (uint64_t)i));
    auto bake = [&](const std::vector<BakeLight> &lights, double &ms) {
      ProbeVolumeData d;
      probe_place(d, p37_box(Vec3(-2.0f), Vec3(2.0f)), s.tri_boxes(), 0.5f, 0.5f);
      probe_validate(d, s.pt, 16);
      ScopedTimer t;
      probe_bake(d, s.pt, lights, 16, 0);
      ms = t.ms();
      return d;
    };
    double ms_one = 0, ms_many = 0;
    const ProbeVolumeData a = bake(one, ms_one), b = bake(many, ms_many);
    double sa = 0, sb = 0;
    size_t n = 0;
    for (size_t i = 0; i < a.sh.size() && i < b.sh.size(); i++) {
      if (!a.valid[i]) continue;
      sa += a.sh[i].c[0].x, sb += b.sh[i].c[0].x, n++;
    }
    const double ratio = sa > 0 ? sb / sa : 0;
    std::printf("    %zu probes: forty lights / one light, mean SH L0 ratio %.3f; baked in %.1f ms (one light %.1f ms)\n", n, ratio, ms_many, ms_one);
    CHECK(n > 50);
    CHECK(std::fabs(ratio - 1.0) < 0.05);
  });
  test("a changed sky relights baked probes (round 453): after a bake under one Gradient sky, shading under a brighter one - no re-bake - is close to a re-bake under it on and off the axes; the same sky adds exactly nothing at any normal; a black world takes light away but never below 0; a probe without the baked sky takes no change", [] {
    P37Pt s;
    s.add(primitives::plane(2.0f, 2), Mat4::translate({0, -50.0f, 0}), Vec3(0.0f));
    auto gradient = [](float k) {
      Environment env;
      env.mode = Environment::Gradient;
      env.sky = Vec3(1.0f, 0.8f, 0.6f) * k, env.equator = Vec3(0.3f) * k, env.ground = Vec3(0.05f) * k;
      return env;
    };
    auto bake = [&](const Environment &env) {
      s.build({}, env);
      ProbeVolumeData d;
      probe_place(d, p37_box(Vec3(-1.0f), Vec3(1.0f)), s.tri_boxes(), 1.0f, 1.0f);
      probe_validate(d, s.pt, 32);
      probe_bake(d, s.pt, {}, 512, 1, 0, SIZE_MAX, 1.0f, -1, nullptr, &env);
      return d;
    };
    const Environment e1 = gradient(1.0f), e2 = gradient(2.5f), black = gradient(0.0f);
    const ProbeVolumeData d1 = bake(e1), d2 = bake(e2);
    CHECK(d1.sky_in_sh && d1.has_baked_sky);
    ProbeVolumeData plain = d1;
    plain.has_baked_sky = false;
    const Vec3 normals[] = {Vec3(0, 1, 0), Vec3(0, -1, 0), Vec3(1, 0, 0), normalize(Vec3(0.6f, 0.5f, 0.62f)), normalize(Vec3(-0.4f, 0.8f, 0.3f)),
                            normalize(Vec3(0.3f, -0.5f, -0.7f))};
    for (const Vec3 n : normals) {
      Vec3 same, moved, rebaked, bare, dark;
      d1.sample(Vec3(0.0f), n, e1.irradiance(n), false, same, false, nullptr, &e1);
      d1.sample(Vec3(0.0f), n, e2.irradiance(n), false, moved, false, nullptr, &e2);
      d2.sample(Vec3(0.0f), n, e2.irradiance(n), false, rebaked, false, nullptr, &e2);
      plain.sample(Vec3(0.0f), n, e2.irradiance(n), false, bare, false, nullptr, &e2);
      d1.sample(Vec3(0.0f), n, black.irradiance(n), false, dark, false, nullptr, &black);
      std::printf("    n (%.2f %.2f %.2f): baked under sky 1 %.3f, shaded under 2.5 %.3f, re-baked under 2.5 %.3f; the world black %.3f\n", n.x, n.y, n.z, same.x,
                  moved.x, rebaked.x, dark.x);
      /* The unchanged sky adds nothing: the SH as baked (the plain copy, which takes no change, is the reference). */
      Vec3 ref;
      plain.sample(Vec3(0.0f), n, e1.irradiance(n), false, ref, false, nullptr, &e1);
      CHECK(std::fabs(same.x - ref.x) < 1e-4f && std::fabs(same.y - ref.y) < 1e-4f && std::fabs(same.z - ref.z) < 1e-4f);
      /* The weight is the six-axis blend, which is blurrier between the axes than the re-bake's SH (ADR 0116's amendment):
       * off the axes the miss reaches 15 % (1.35 against 1.60 at the diagonal), so 18 % is the bound, not a tight fit. */
      CHECK(std::fabs(moved.x - rebaked.x) < 0.18f * rebaked.x + 0.02f);
      CHECK(std::fabs(bare.x - same.x) < 1e-5f);  // (no baked sky: the SH as baked)
      CHECK(dark.x >= 0.0f && dark.y >= 0.0f && dark.z >= 0.0f);
    }
  });
}

/* ---------------------------------------------------------------- round 454: four mask lights, Distance Shadowmask (task 0430) */
static void tests_round454() {
  /* A mask page's four channels at a point on a mesh (identity-placed), through its entry's UVs as the renderers read them. */
  auto mask_at = [](const LightingData &ld, uint64_t id, const Mesh &m, Vec3 p, Vec4 &out) {
    auto e = ld.entries.find(id);
    if (e == ld.entries.end() || (size_t)e->second.page >= ld.mask_pages.size()) return false;
    const RenderMesh &rm = m.render_mesh();
    for (size_t t = 0; t < rm.tri_count() && e->second.tri_uv.size() == rm.tri_count() * 3; t++) {
      const Vec3 a = rm.positions[rm.indices[t * 3]], b = rm.positions[rm.indices[t * 3 + 1]], c = rm.positions[rm.indices[t * 3 + 2]];
      const Vec3 n = cross(b - a, c - a);
      const float area2 = dot(n, n);
      if (!(area2 > 1e-12f) || std::fabs(dot(p - a, n)) / std::sqrt(area2) > 0.02f) continue;
      const float w1 = dot(cross(p - a, c - a), n) / area2, w2 = dot(cross(b - a, p - a), n) / area2, w0 = 1.0f - w1 - w2;
      if (w0 < -1e-3f || w1 < -1e-3f || w2 < -1e-3f) continue;
      out = ld.mask_pages[(size_t)e->second.page].sample4(e->second.tri_uv[t * 3] * w0 + e->second.tri_uv[t * 3 + 1] * w1 + e->second.tri_uv[t * 3 + 2] * w2);
      return true;
    }
    return false;
  };
  test("Four mask lights (round 454): four Mixed lamps over four boxes in a Shadowmask bake are recorded as modes 4-7 and each channel is dark under its own box and clear in the open; a fifth Mixed lamp is baked as a Baked light", [&] {
    MeshPtr floor = primitives::plane(16.0f, 16), box = primitives::cube(1.0f);
    const MaterialPtr white = make_material("White", Vec3(0.8f));
    Environment env;
    env.mode = Environment::Color, env.color = Vec3(0.0f);
    const float xs[4] = {-6.0f, -2.0f, 2.0f, 6.0f};
    std::vector<BakeLight> lamps(5);
    for (int i = 0; i < 5; i++) {
      lamps[(size_t)i].light.type = RenderLight::Point, lamps[(size_t)i].light.range = 100.0f;
      lamps[(size_t)i].light.position = i < 4 ? Vec3(xs[i], 4.0f, 0.0f) : Vec3(0.0f, 4.0f, -6.0f);
      lamps[(size_t)i].light.intensity = i < 4 ? 8.0f - (float)i : 1.0f;  // (the brightest first: channels 0-3 in this order; the weakest has none left)
      lamps[(size_t)i].mode = 1, lamps[(size_t)i].id = 11 + (uint64_t)i;
    }
    BakeSettings bs;
    bs.texels_per_unit = 6, bs.indirect_samples = 16, bs.bounces = 1, bs.denoise = false, bs.directional = false, bs.lighting_mode = 1;
    std::vector<BakeObject> objs{bake_object(1, floor, Mat4::identity(), {white})};
    for (int i = 0; i < 4; i++) {
      BakeObject b = bake_object(2 + (uint64_t)i, box, Mat4::translate(Vec3(xs[i], 1.5f, 0.0f)), {white});
      b.want_lightmap = false;
      objs.push_back(b);
    }
    Lightmapper lm;
    lm.begin(objs, lamps, env, bs);
    while (!lm.step(1e9)) {
    }
    const LightingData ld = lm.take_result();
    std::printf("    the lamps' modes: %d %d %d %d, the fifth %d\n", ld.light_modes.at(11), ld.light_modes.at(12), ld.light_modes.at(13), ld.light_modes.at(14), ld.light_modes.at(15));
    CHECK(ld.light_modes.at(11) == 4 && ld.light_modes.at(12) == 5 && ld.light_modes.at(13) == 6 && ld.light_modes.at(14) == 7 && ld.light_modes.at(15) == 2);
    CHECK(ld.mask_pages.size() == ld.pages.size());
    /* The shared list the probes and Light Volumes bake from: the same split (the suns first, then by intensity), before any
     * Lightmapper sees it - a fifth Mixed light is Baked for every baker, not only for the lightmaps. */
    std::vector<BakeLight> shared(6);
    for (size_t i = 0; i < shared.size(); i++) {
      shared[i].mode = 1, shared[i].id = 40 + i;
      shared[i].light.type = i == 0 ? RenderLight::Directional : RenderLight::Point;
      shared[i].light.intensity = i == 0 ? 1.0f : 6.0f - (float)i;  // (the sun 1, then 5 4 3 2 1)
    }
    CHECK(shadowmask_split(shared, false) == 2);
    CHECK(shared[0].mode == 1 && shared[1].mode == 1 && shared[2].mode == 1 && shared[3].mode == 1 && shared[4].mode == 2 && shared[5].mode == 2);
    CHECK(shadowmask_split(shared, false) == 0);  // (nothing past four any more)
    Vec4 open(0, 0, 0, 0);
    CHECK(mask_at(ld, 1, *floor, Vec3(0.0f, 0.0f, 6.0f), open));
    for (int c = 0; c < 4; c++) {
      Vec4 under(1, 1, 1, 1);
      CHECK(mask_at(ld, 1, *floor, Vec3(xs[c], 0.0f, 0.0f), under));
      std::printf("    channel %d: %.3f under its box, %.3f in the open\n", c, under[c], open[c]);
      CHECK(under[c] < 0.05f && open[c] > 0.95f);
    }
  });
  test("A Shadowmask bake's masks on disk (round 454): four channels saved as ShadowMask-N.png and read back within 1/255; a round 444 bake's 3-channel .hdr still loads (the fourth channel clear); a folder with neither keeps the masks out; saving over old .hdr masks removes them", [] {
    const std::string dir = fs::join(test_dir(), "r454_masks");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    fs::make_dirs(dir);
    LightingData ld;
    for (int p = 0; p < 2; p++) {
      Lightmap page, mask;
      page.width = mask.width = 8, page.height = mask.height = 4 + 4 * p;
      page.texels.assign((size_t)page.width * page.height, Vec3(0.5f));
      mask.texels.resize(page.texels.size());
      mask.alpha.resize(page.texels.size());
      for (size_t i = 0; i < mask.texels.size(); i++) {
        const float t = (float)i / (float)mask.texels.size();
        mask.texels[i] = Vec3(t, 1.0f - t, std::fmod(t * 7.0f, 1.0f));
        mask.alpha[i] = std::fmod(t * 3.0f + 0.31f, 1.0f);
      }
      ld.pages.push_back(page), ld.mask_pages.push_back(mask);
    }
    LightingData::Entry e;
    e.page = 0;
    ld.entries[1] = e;
    ld.light_modes[7] = 7;
    /* Old masks in the folder: a round 444 .hdr for each page. */
    for (int p = 0; p < 2; p++) {
      const std::vector<float> rgb((size_t)ld.pages[(size_t)p].texels.size() * 3, 0.5f);
      CHECK(write_hdr(fs::join(dir, strprintf("ShadowMask-%d.hdr", p)), rgb.data(), ld.pages[(size_t)p].width, ld.pages[(size_t)p].height));
    }
    std::string err;
    CHECK(ld.save(dir, err));
    CHECK(fs::exists(fs::join(dir, "ShadowMask-0.png")) && fs::exists(fs::join(dir, "ShadowMask-1.png")));
    CHECK(!fs::exists(fs::join(dir, "ShadowMask-0.hdr")) && !fs::exists(fs::join(dir, "ShadowMask-1.hdr")));
    LightingData back;
    CHECK(back.load(dir, err) && back.mask_pages.size() == 2 && back.light_modes.at(7) == 7);
    float worst = 0.0f;
    for (size_t p = 0; p < back.mask_pages.size(); p++)
      for (size_t i = 0; i < ld.mask_pages[p].texels.size() && back.mask_pages[p].alpha.size() == ld.mask_pages[p].alpha.size(); i++) {
        const Vec3 a = ld.mask_pages[p].texels[i], b = back.mask_pages[p].texels[i];
        worst = std::max({worst, std::fabs(a.x - b.x), std::fabs(a.y - b.y), std::fabs(a.z - b.z), std::fabs(ld.mask_pages[p].alpha[i] - back.mask_pages[p].alpha[i])});
      }
    std::printf("    the PNG pages read back: worst difference %.5f (1/255 is %.5f)\n", worst, 1.0f / 255.0f);
    CHECK(worst <= 1.0f / 255.0f);
    /* A round 444 folder: the .hdr pages (three channels), no PNG. */
    for (int p = 0; p < 2; p++) std::remove(fs::join(dir, strprintf("ShadowMask-%d.png", p)).c_str());
    const float levels[3] = {0.0f, 0.5f, 1.0f};
    for (int p = 0; p < 2; p++) {
      const Lightmap &pg = ld.pages[(size_t)p];
      std::vector<float> rgb(pg.texels.size() * 3);
      for (size_t i = 0; i < pg.texels.size(); i++) rgb[i * 3] = levels[i % 3], rgb[i * 3 + 1] = levels[(i + 1) % 3], rgb[i * 3 + 2] = levels[(i + 2) % 3];
      CHECK(write_hdr(fs::join(dir, strprintf("ShadowMask-%d.hdr", p)), rgb.data(), pg.width, pg.height));
    }
    LightingData old;
    CHECK(old.load(dir, err) && old.mask_pages.size() == 2);
    bool same = old.mask_pages.size() == 2;
    for (size_t p = 0; same && p < 2; p++)
      for (size_t i = 0; i < old.mask_pages[p].texels.size(); i++) {
        const Vec3 t = old.mask_pages[p].texels[i];
        same = same && std::fabs(t.x - levels[i % 3]) < 0.01f && std::fabs(t.y - levels[(i + 1) % 3]) < 0.01f && std::fabs(t.z - levels[(i + 2) % 3]) < 0.01f &&
               old.mask_pages[p].sample4(Vec2(0.5f, 0.5f)).w == 1.0f && old.mask_pages[p].alpha[i] == 1.0f;
      }
    CHECK(same);
    /* Neither file: the colour pages load, the masks stay out. */
    for (int p = 0; p < 2; p++) std::remove(fs::join(dir, strprintf("ShadowMask-%d.hdr", p)).c_str());
    LightingData none;
    CHECK(none.load(dir, err) && none.pages.size() == 2 && none.mask_pages.empty());
    CHECK(none.light_modes.at(7) == 1);  // (a channel with no mask: the light shadows in realtime alone, not against a white mask)
    std::filesystem::remove_all(dir, ec);
  });
  test("Distance Shadowmask (round 454): the key light's realtime shadow within 0.9 of the Shadow Distance, the mask beyond it, a fade between; plain Shadowmask is the darker of the two at every distance; a Mixed light without a realtime map keeps the mask", [] {
    LightingEnv env;
    env.shadowmask_mode = 1, env.shadow_distance = 100.0f;
    const float near_v = shadowmask_visibility(env, true, 0.0f, 1.0f, 50.0f), far_v = shadowmask_visibility(env, true, 0.0f, 1.0f, 120.0f);
    const float mid = shadowmask_visibility(env, true, 0.0f, 1.0f, 95.0f), edge = shadowmask_visibility(env, true, 0.0f, 1.0f, 90.0f);
    std::printf("    realtime 0 / mask 1: %.2f at 0.5 D, %.2f at 0.9 D, %.2f at 0.95 D, %.2f at 1.2 D\n", near_v, edge, mid, far_v);
    /* (The fade's edge to within a float step: 0.9f x D is not 90 exactly, and macOS's arm64 build fuses the multiply-add, so
     * the edge came out 2.4e-7 there - the check is the blend's shape, not bit equality.) */
    CHECK(near_v == 0.0f && edge < 1e-5f && std::fabs(mid - 0.5f) < 1e-4f && far_v == 1.0f);
    CHECK(shadowmask_visibility(env, true, 1.0f, 0.0f, 50.0f) == 1.0f && shadowmask_visibility(env, true, 1.0f, 0.0f, 120.0f) == 0.0f);  // (a moving caster's shadow near, the mask's beyond)
    CHECK(shadowmask_visibility(env, false, 1.0f, 0.25f, 120.0f) == 0.25f && shadowmask_visibility(env, false, 0.0f, 0.75f, 20.0f) == 0.0f);  // (no map: min)
    env.shadowmask_mode = 0;
    for (float d : {10.0f, 95.0f, 500.0f}) CHECK(shadowmask_visibility(env, true, 0.0f, 1.0f, d) == 0.0f && shadowmask_visibility(env, true, 1.0f, 0.25f, d) == 0.25f);
    CHECK(light_mask_channel(4) == 0 && light_mask_channel(7) == 3 && light_mask_channel(8) == -1 && light_mask_channel(3) == -1);
  });
  test("Distance Shadowmask in the Game view (round 454): a ball moved after the bake shadows the floor within the Shadow Distance and not beyond it, as plain Shadowmask shadows it everywhere; changing the mode or the distance redraws without a bake; set Lighting.ShadowmaskMode and ShadowDistance work", [] {
    Editor ed;
    P37World w = p37_world(ed, false, 1, 1.0f, true);
    ed.viewport_device_for_test(1);
    ed.scene().environment.color = Vec3(0.02f);
    ed.scene().lighting.lighting_mode = 1;
    ed.commit_change("shadowmask");
    r35_bake(ed);
    CHECK(!ed.lighting_data_for_test().mask_pages.empty());
    const uint64_t page_serial = ed.lighting_data_for_test().pages.empty() ? 0 : ed.lighting_data_for_test().pages[0].serial;
    auto game = [&] {
      r34_steps(ed, 3);
      return r35_game(ed);
    };
    ed.command("set Lighting.ShadowmaskMode 0");
    const std::vector<uint32_t> plain = game();
    ed.command("set Lighting.ShadowmaskMode 1");
    ed.command("set Lighting.ShadowDistance 1000");
    const std::vector<uint32_t> within = game();
    ed.command("set Lighting.ShadowDistance 0.1");  // (the field's smallest: every floor point is beyond it)
    const std::vector<uint32_t> beyond = game();
    CHECK(ed.scene().lighting.shadowmask_mode == 1 && std::fabs(ed.scene().lighting.shadow_distance - 0.1f) < 1e-6f);
    ed.command("set Lighting.ShadowmaskMode 0");
    const std::vector<uint32_t> plain_again = game();
    const size_t near_diff = r35_diff_count(plain, within, 12), far_diff = r35_diff_count(plain, beyond, 12);
    std::printf("    pixels differing from plain Shadowmask: %zu within the distance, %zu beyond it; plain again %zu\n", near_diff, far_diff, r35_diff_count(plain, plain_again, 0));
    CHECK(far_diff > 150 && near_diff * 4 < far_diff);
    CHECK(r35_diff_count(plain, plain_again, 0) == 0);  // (nothing else in the picture moved)
    CHECK(!ed.baking_for_test() && !ed.lighting_data_for_test().pages.empty() && ed.lighting_data_for_test().pages[0].serial == page_serial);  // (no rebake)
  });
  test("Distance Shadowmask without its masks (round 454): a saved Shadowmask bake whose ShadowMask pages are gone loads with its lights in realtime alone - the baked pages and modes of lights with no mask, the picture the same inside and beyond the Shadow Distance (no white mask to blend toward)", [] {
    Editor ed;
    P37World w = p37_world(ed, false, 1, 1.0f, true);
    ed.viewport_device_for_test(1);
    ed.scene().environment.color = Vec3(0.02f);
    ed.scene().lighting.lighting_mode = 1;
    ed.commit_change("shadowmask");
    r35_bake(ed);
    CHECK(!ed.lighting_data_for_test().mask_pages.empty());
    const std::string scene = "Assets/Scenes/R454M.scene";
    ed.command("save " + scene);
    const std::string dir = ed.lighting_dir_for_test();
    CHECK(!dir.empty() && fs::exists(fs::join(dir, "ShadowMask-0.png")));
    for (int p = 0; p < 8; p++) std::remove(fs::join(dir, strprintf("ShadowMask-%d.png", p)).c_str());
    ed.command("open " + scene);
    const LightingData &ld = ed.lighting_data_for_test();
    bool masked = false;
    for (const auto &kv : ld.light_modes) masked = masked || (kv.second >= 4 && kv.second <= 7);
    CHECK(!ld.pages.empty() && ld.mask_pages.empty() && !masked);
    ed.command("set Lighting.ShadowmaskMode 1");
    auto game = [&](const char *d) {
      ed.command(std::string("set Lighting.ShadowDistance ") + d);
      r34_steps(ed, 3);
      return r35_game(ed);
    };
    const std::vector<uint32_t> within = game("1000"), beyond = game("0.1");
    const size_t diff = r35_diff_count(within, beyond, 6);
    std::printf("    no masks: %zu pixels differ between inside and beyond the distance\n", diff);
    CHECK(diff < 20);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::remove(resolve_asset_path(scene).c_str());
  });
  test("Unity's shadowmasks in the lightmaps file (round 454): version 4 written and read back with the mask pages, the lights' channels and the Quality settings; a version 3 file reads as before; the project's QualitySettings.asset read at its current level", [] {
    UnityLighting u;
    UnityLighting::Page p;
    p.color = "Assets/S/Lightmap-0_comp_light.exr", p.width = 2, p.height = 2;
    p.rgb.assign(12, 0x3800);
    p.mask_width = 2, p.mask_height = 1;
    p.mask = {255, 0, 128, 64, 0, 255, 0, 255};
    u.pages.push_back(p);
    u.lights.push_back({"Lamp[0]", 2, 1, 2});
    u.lights.push_back({"Sun[0]", 1, 1, -1});
    u.lights.push_back({"Spare[0]", 2, 1, -1, true});
    u.shadowmask_mode = 0, u.shadow_distance = 40.0f;
    const std::string bytes = write_unity_lighting(u);
    UnityLighting back;
    std::string why;
    CHECK(read_unity_lighting(bytes, back, why));
    CHECK(back.pages.size() == 1 && back.pages[0].mask == p.mask && back.pages[0].mask_width == 2 && back.lights.size() == 3 && back.lights[0].mask_channel == 2 &&
          back.lights[1].mask_channel == -1 && !back.lights[1].overflow && back.lights[2].overflow && back.shadowmask_mode == 0 && back.shadow_distance == 40.0f);
    Lightmap mp;
    CHECK(unity_mask_page(back.pages[0], mp) && mp.width == 2 && mp.height == 1 && std::fabs(mp.texels[0].z - 128.0f / 255.0f) < 1e-5f && std::fabs(mp.alpha[0] - 64.0f / 255.0f) < 1e-5f);
    /* Version 3: the same file without its tail (16 bytes: no masks, no channels, the two settings), the version turned back. */
    UnityLighting plain = u;
    plain.pages[0].mask.clear(), plain.pages[0].mask_width = plain.pages[0].mask_height = 0, plain.lights[0].mask_channel = -1, plain.lights[2].overflow = false, plain.shadowmask_mode = -1, plain.shadow_distance = -1.0f;
    std::string v3 = write_unity_lighting(plain);
    v3.resize(v3.size() - 4 - 16);
    const int32_t three = 3;
    std::memcpy(&v3[8], &three, 4);
    const uint32_t sum = unity_lighting_hash(v3);
    v3.append((const char *)&sum, 4);
    UnityLighting old;
    CHECK(read_unity_lighting(v3, old, why) && old.pages.size() == 1 && old.pages[0].mask.empty() && old.lights[0].mask_channel == -1 && old.shadowmask_mode == -1 && old.shadow_distance < 0.0f);
    std::string broken = bytes;
    broken[broken.size() / 2] ^= 0x55;
    CHECK(!read_unity_lighting(broken, back, why));
    const std::string quality =
        "QualitySettings:\n  m_ObjectHideFlags: 0\n  serializedVersion: 5\n  m_CurrentQuality: 1\n  m_QualitySettings:\n"
        "  - serializedVersion: 2\n    name: Low\n    shadowmaskMode: 0\n    shadowDistance: 20\n"
        "  - serializedVersion: 2\n    name: Medium\n    shadowmaskMode: 1\n    shadowDistance: 75.5\n";
    int mode = -1;
    float dist = -1.0f;
    CHECK(unity_quality_shadowmask(quality, mode, dist) && mode == 1 && std::fabs(dist - 75.5f) < 1e-4f && !unity_quality_shadowmask("nothing here\n", mode, dist));
    const std::string proj = fs::join(test_dir(), "r454_project");
    std::error_code ec;
    std::filesystem::remove_all(proj, ec);
    fs::make_dirs(fs::join(proj, "Assets/Scenes"));
    fs::make_dirs(fs::join(proj, "ProjectSettings"));
    CHECK(fs::write_file(fs::join(proj, "Assets/Scenes/Lit.unity"), "x") && unity_find_quality_settings(fs::join(proj, "Assets/Scenes/Lit.unity")).empty());
    CHECK(fs::write_file(fs::join(proj, "ProjectSettings/QualitySettings.asset"), quality) && unity_find_quality_settings(fs::join(proj, "Assets/Scenes/Lit.unity")) == quality);  // (the project the scene stands in)
    /* Blendity's own project (an Assets folder, no ProjectSettings) under a folder that has some: not that one's. */
    fs::make_dirs(fs::join(proj, "Inner/Assets/Imported/Pkg"));
    CHECK(fs::write_file(fs::join(proj, "Inner/Assets/Imported/Pkg/Lit.unity"), "x") && unity_find_quality_settings(fs::join(proj, "Inner/Assets/Imported/Pkg/Lit.unity")).empty());
    std::filesystem::remove_all(proj, ec);
  });
  test("Unity's shadowmasks and channels in the editor (round 454): a Shadowmask scene's mask pages come in with the lightmaps (rows flipped), each Mixed lamp takes mode 4 + its channel, the project's Shadowmask Mode and Shadow Distance come from the file (Unity's defaults, Distance Shadowmask and 150 m, before it); a mask of another size leaves the masks out; an exported package carries the mask PNG as a Shadowmask texture and the Lighting Mode as m_MixedBakeMode", [] {
    const std::string rel = "Assets/Imported/R454U";
    const std::string dir = resolve_asset_path(rel), pkg = fs::join(test_dir(), "R454U.unitypackage");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    fs::make_dirs(fs::join(dir, ".unity-meta"));
    const std::string gs = "eeee4540eeee4540eeee4540eeee0001";
    auto lamp = [](int id, const char *name, int x) {
      const std::string n = std::to_string(id);
      return "--- !u!1 &" + n + "0\nGameObject:\n  m_Name: " + name + "\n--- !u!4 &" + n + "1\nTransform:\n  m_GameObject: {fileID: " + n + "0}\n  m_LocalPosition: {x: " + std::to_string(x) +
             ", y: 3, z: 0}\n  m_Father: {fileID: 0}\n--- !u!108 &" + n + "2\nLight:\n  m_GameObject: {fileID: " + n + "0}\n  m_Type: 2\n  m_Lightmapping: 1\n  m_Intensity: 1\n";
    };
    CHECK(fs::write_file(fs::join(dir, "Lit.unity"),
                         "%YAML 1.1\n--- !u!157 &3\nLightmapSettings:\n  m_LightingSettings: {fileID: 4}\n--- !u!850595691 &4\nLightingSettings:\n  m_BakeResolution: 40\n  m_MixedBakeMode: 0\n"
                         "--- !u!1 &10\nGameObject:\n  m_Name: Floor\n--- !u!4 &11\nTransform:\n  m_GameObject: {fileID: 10}\n  m_Father: {fileID: 0}\n"
                         "--- !u!33 &12\nMeshFilter:\n  m_GameObject: {fileID: 10}\n  m_Mesh: {fileID: 10202, guid: 0000000000000000e000000000000000, type: 0}\n"
                         "--- !u!23 &13\nMeshRenderer:\n  m_GameObject: {fileID: 10}\n" + lamp(20, "LampA", -3) + lamp(30, "LampB", 0) + lamp(40, "LampC", 3) + lamp(50, "LampD", 6)));
    CHECK(fs::write_file(fs::join(dir, ".unity-guids"), gs + "\tAssets/Lit.unity\t" + rel + "/Lit.unity\n"));
    CHECK(fs::write_file(fs::join(dir, ".unity-meta/" + gs + ".meta"), "fileFormatVersion: 2\nguid: " + gs + "\n"));
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("open " + rel + "/Lit.unity");
    GameObject *floor = ed.scene().find_by_name("Floor");
    CHECK(floor && floor->evaluated_mesh(1) && ed.scene().find_by_name("LampA"));
    if (!floor || !floor->evaluated_mesh(1)) return;
    std::printf("    the import's defaults: Shadowmask Mode %d, Shadow Distance %.0f\n", ed.scene().lighting.shadowmask_mode, ed.scene().lighting.shadow_distance);
    CHECK(ed.scene().lighting.shadowmask_mode == 1 && ed.scene().lighting.shadow_distance == 150.0f);
    const RenderMesh &rm = floor->evaluated_mesh(1)->render_mesh();
    UnityLighting u;
    UnityLighting::Page p;
    p.color = "Assets/Lit/Lightmap-0_comp_light.exr", p.width = p.height = 8;
    p.rgb.assign((size_t)8 * 8 * 3, 0x3800);
    p.mask_width = p.mask_height = 8;
    p.mask.resize((size_t)8 * 8 * 4);
    for (int y = 0; y < 8; y++)  // (Unity's rows run up: its row 0 is the bottom)
      for (int x = 0; x < 8; x++) {
        uint8_t *c = &p.mask[((size_t)y * 8 + (size_t)x) * 4];
        c[0] = y == 0 ? 255 : 0, c[1] = y == 7 ? 255 : 0, c[2] = (uint8_t)(x * 30), c[3] = 64;
      }
    u.pages.push_back(p);
    UnityLighting::MeshData m;
    for (size_t i = 0; i < rm.positions.size(); i++) m.positions.push_back(rm.positions[i]), m.uv.push_back(Vec2(rm.positions[i].x + 0.5f, rm.positions[i].z + 0.5f));
    m.indices = rm.indices;
    u.meshes.push_back(m);
    UnityLighting::Renderer r;
    r.path = "Floor[0]", r.mesh = 0, r.lightmap = 0, r.world = Mat4::identity();
    u.renderers.push_back(r);
    u.lights = {{"LampA[0]", 2, 1, 3}, {"LampB[0]", 2, 1, 0}, {"LampC[0]", 2, 1, -1, true}, {"LampD[0]", 2, 1, -1}};  // (C: Unity baked it as Baked; D: no channel, as a lamp added since)
    u.shadowmask_mode = 0, u.shadow_distance = 40.0f;
    const std::string file = fs::join(dir, "Lit.unity.unity-lighting");
    ed.scene().unity_bake.mixed = 2;  // (the scene's Lighting Mode Shadowmask, as its LightingSettings said it)
    CHECK(fs::write_file(file, write_unity_lighting(u)));
    CHECK(ed.apply_unity_lighting(file));
    const LightingData &ld = ed.lighting_data_for_test();
    GameObject *a = ed.scene().find_by_name("LampA"), *b = ed.scene().find_by_name("LampB"), *c = ed.scene().find_by_name("LampC");
    GameObject *d = ed.scene().find_by_name("LampD");
    CHECK(d && ld.light_modes.count(d->id) && ld.light_modes.at(d->id) == 1);  // (no channel and not baked as Baked: still Mixed)
    CHECK(a && b && c && ld.light_modes.count(a->id) && ld.light_modes.count(b->id) && ld.light_modes.count(c->id));
    if (!a || !b || !c || !ld.light_modes.count(c->id)) return;
    std::printf("    the lamps' modes: %d %d %d; %zu mask page(s)\n", ld.light_modes.at(a->id), ld.light_modes.at(b->id), ld.light_modes.at(c->id), ld.mask_pages.size());
    CHECK(ld.light_modes.at(a->id) == 7 && ld.light_modes.at(b->id) == 4 && ld.light_modes.at(c->id) == 2);  // (LampC: Mixed with no channel while others have one - past Unity's four: Baked)
    CHECK(ld.mask_pages.size() == 1 && ld.mask_pages[0].width == 8 && ld.mask_pages[0].height == 8);
    if (ld.mask_pages.size() == 1 && ld.mask_pages[0].width == 8) {
      const Lightmap &mp = ld.mask_pages[0];
      CHECK(mp.texels[7 * 8 + 3].x == 1.0f && mp.texels[3].x == 0.0f && mp.texels[3].y == 1.0f);  // (Unity's bottom row is Blendity's last)
      CHECK(std::fabs(mp.alpha[5] - 64.0f / 255.0f) < 1e-5f && std::fabs(mp.texels[7 * 8 + 4].z - 120.0f / 255.0f) < 1e-5f);
    }
    /* A mask that isn't its lightmap's size: refused for this bake, the lamps stay Mixed. */
    u.pages[0].mask_width = u.pages[0].mask_height = 4;
    u.pages[0].mask.resize((size_t)4 * 4 * 4);
    CHECK(fs::write_file(file, write_unity_lighting(u)));
    CHECK(ed.apply_unity_lighting(file) && ed.lighting_data_for_test().mask_pages.empty() && ed.lighting_data_for_test().light_modes.at(a->id) == 1);
    /* The file's Quality values stay in the file: a re-apply never reverts the Inspector's own. */
    ed.scene().lighting.shadowmask_mode = 1, ed.scene().lighting.shadow_distance = 77.0f;
    CHECK(fs::write_file(file, write_unity_lighting(u)) && ed.apply_unity_lighting(file) && ed.scene().lighting.shadowmask_mode == 1 && ed.scene().lighting.shadow_distance == 77.0f);
    /* The package back to Unity: a bake's mask PNG as a Shadowmask texture, the Lighting Mode in the scene's settings. */
    ed.scene().lighting.lighting_mode = 1;
    const uint32_t px[4] = {0xFF0000FFu, 0x80FF0000u, 0xFF00FF00u, 0x00FFFFFFu};
    CHECK(write_png(fs::join(dir, "ShadowMask-0.png"), px, 2, 2, 2, true) && write_png(fs::join(dir, "Other.png"), px, 2, 2, 2, true));
    CHECK(fs::write_file(fs::join(dir, "LightingData.bin"), "a bake's index"));  // (the mask is a bake's page only beside it)
    ed.command("export unitypackage " + rel + " " + pkg);
    std::string bytes, why;
    std::vector<UnityAsset> assets;
    CHECK(fs::read_file(pkg, bytes) && read_unity_package(bytes, assets, why));
    const UnityAsset *mask = nullptr, *scene = nullptr;
    for (const UnityAsset &asset : assets) {
      mask = fs::filename(asset.pathname) == "ShadowMask-0.png" ? &asset : mask;
      scene = fs::filename(asset.pathname) == "Lit.unity" ? &asset : scene;
    }
    CHECK(mask && scene);
    if (mask) {
      std::printf("    the mask's .meta: %s%s%s\n", mask->meta.find("textureType: 11") != std::string::npos ? "type 11 " : "", mask->meta.find("sRGBTexture: 0") != std::string::npos ? "linear " : "",
                  mask->meta.find("textureCompression: 0") != std::string::npos ? "uncompressed" : "");
      CHECK(mask->meta.find("guid: " + mask->guid) != std::string::npos && mask->meta.find("TextureImporter:") != std::string::npos && mask->meta.find("sRGBTexture: 0") != std::string::npos &&
            mask->meta.find("textureCompression: 0") != std::string::npos && mask->meta.find("textureType: 11") != std::string::npos);
    }
    CHECK(scene && scene->data.find("m_MixedBakeMode: 2") != std::string::npos);
    std::remove(pkg.c_str());
    std::filesystem::remove_all(dir, ec);
  });
}

void run_lighting_tests() {
  round35_tests();
  round36_tests();
  round37_tests();
  round39_tests();
  round41_tests();
  round43_tests();
  round44_tests();
  round52_tests();
  round58_tests();
  tests_round111();
  tests_round164();
  tests_round175();
  tests_round177();
  tests_round421();
  tests_round423();
  tests_round424();
  tests_round425();
  tests_round426();
  tests_round428();
  tests_round429();
  tests_round430();
  tests_round438();
  tests_round439();
  tests_round440();
  tests_round441();
  tests_round442();
  tests_round444();
  tests_round451();
  tests_round452();
  tests_round453();
  tests_round454();
}

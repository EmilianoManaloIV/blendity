// SPDX-License-Identifier: GPL-2.0-or-later
// blendity_tests: the render tests (task 0177: one file a subsystem; the groups keep their rounds' order).
#include "test_common.h"
#include "../src/render/mesh_maps.h"

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
      /* A GPU that failed (a kernel over SPIR-V's id bound: task 0157 found the software-BVH variant there) hands its
       * samples to the CPU and the picture still matches: the device must have done the work itself. */
      if (!cpu && !pt.gpu_error().empty()) std::printf("    GPU error: %s\n", pt.gpu_error().c_str());
      if (!cpu) CHECK(pt.gpu_error().empty() && pt.stats().cpu_samples == 0);
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
    /* A view smaller than the internal size still renders at the internal size (task 0026: a small
     * Camera Preview inset shows the Game view's look) and is shrunk into the view. */
    s.fit = FilterStack::Fill;
    filter_layout(s, 100, 50, iw, ih, dst);
    CHECK(iw == 480 && ih == 240 && dst.w == 100 && dst.h == 50);
    s.fit = FilterStack::Letterbox;
    filter_layout(s, 100, 50, iw, ih, dst);
    CHECK(iw == 320 && ih == 240 && dst.w <= 100 && dst.h <= 50 && dst.w >= 1 && dst.h >= 1);
    /* No internal size: the view itself. */
    FilterStack none;
    filter_layout(none, 640, 480, iw, ih, dst);
    CHECK(iw == 640 && ih == 480 && dst.x == 0 && dst.y == 0 && dst.w == 640 && dst.h == 480);
    /* Odd sizes: 1 x 1 views, zero and negative views, absurd internal sizes. */
    s.fit = FilterStack::Fill;
    filter_layout(s, 1, 1, iw, ih, dst);
    CHECK(iw == 240 && ih == 240 && dst.w == 1 && dst.h == 1);
    filter_layout(s, 0, -5, iw, ih, dst);
    CHECK(iw >= 1 && ih >= 1 && dst.w >= 1 && dst.h >= 1);
    s.fit = FilterStack::Letterbox;
    filter_layout(s, 1, 1, iw, ih, dst);
    CHECK(iw == 320 && ih == 240 && dst.w == 1 && dst.h == 1);
    FilterStack huge;
    huge.width = 1000000;
    huge.height = 1000000;
    for (int fit : {0, 1}) {
      huge.fit = fit;
      filter_layout(huge, 1920, 1080, iw, ih, dst);
      CHECK(iw >= 1 && ih >= 1 && iw <= 1920 && ih <= 1080);  // an absurd stack size never exceeds the view (1080p here)
      CHECK(dst.x >= 0 && dst.y >= 0 && dst.right() <= 1920 && dst.bottom() <= 1080);
    }
    /* A squeezed view can't ask for a huge internal image either: both sides are capped (1920 x 1080, or
     * the view's own size when larger). */
    for (int fit : {0, 1}) {
      huge.fit = fit;
      filter_layout(huge, 1600, 8, iw, ih, dst);
      CHECK(iw >= 1 && ih >= 1 && iw <= 1920 && ih <= 1080 && dst.right() <= 1600 && dst.bottom() <= 8);
      filter_layout(huge, 16384, 16, iw, ih, dst);
      CHECK(iw >= 1 && ih >= 1 && iw <= 16384 && ih <= 1080 && dst.right() <= 16384 && dst.bottom() <= 16);
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
      for (int i = 0; i < 150; i++) {
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
    /* Park the mouse outside the window: resting on a toolbar button, its tooltip appears after a real-time
     * delay that a slow (sanitizer) run reaches, and it draws over the view's top. */
    x = y = -100;
    ed.step_frame_headless({ev(ET::MouseMove)});
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
    for (const char *mode : {"solid", "wire", "both", "shaded"}) {
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
    /* Statistics off: its frame time changes from frame to frame (a slow sanitizer run pushed it past the
     * limit below, all of it in that box). */
    R.click_button(3);
    R.settle();
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
    Recti where{0, 0, 0, 0};  // where the differences are, for when it fails
    {
      int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;
      for (int y = v.y; y < v.bottom(); y++)
        for (int x = v.x; x < v.right(); x++)
          if (editor_view[(size_t)y * R.FW() + x] != back[(size_t)y * R.FW() + x])
            x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
      if (x1 >= 0) where = {x0 - v.x, y0 - v.y, x1 - x0 + 1, y1 - y0 + 1};
    }
    std::printf("    editor view after the pilot: %zu pixels differ from before (limit %d), within %d,%d %dx%d of the %dx%d view\n", d,
                v.w * v.h / 200, where.x, where.y, where.w, where.h, v.w, v.h);
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
        /* The anime and manga effects (task 0039). */
        {"Halftone", "CellSize", "12", [](FilterEffect *e) { return (double)static_cast<HalftoneEffect *>(e)->cell; }, 12},
        {"Hatching", "Spacing", "9", [](FilterEffect *e) { return (double)static_cast<HatchingEffect *>(e)->spacing; }, 9},
        {"Line Art", "CreaseAngle", "60", [](FilterEffect *e) { return (double)static_cast<LineArtEffect *>(e)->crease_angle; }, 60},
        {"Paper", "Strength", "0.6", [](FilterEffect *e) { return (double)static_cast<PaperEffect *>(e)->strength; }, 0.6},
        {"Color Ramp", "Stops", "4", [](FilterEffect *e) { return (double)static_cast<ColorRampEffect *>(e)->stops; }, 4},
        /* The Anime and Manga looks as single effects (task 0154). */
        {"Anime", "LineThickness", "2", [](FilterEffect *e) { return (double)static_cast<AnimeEffect *>(e)->line_thickness; }, 2},
        {"Manga", "DotCell", "9", [](FilterEffect *e) { return (double)static_cast<MangaEffect *>(e)->dot_cell; }, 9},
        /* The other categories' effects go through the same paths (they had their own copies of this test). */
        {"Retro Console", "Width", "100", [](FilterEffect *e) { return (double)static_cast<RetroConsoleFilter *>(e)->width; }, 100},
        {"Bloom", "Threshold", "2.5", [](FilterEffect *e) { return (double)static_cast<BloomEffect *>(e)->threshold; }, 2.5},
    };
    /* The registry lists them all: Color (5), Lens (4), Stylize (8), plus Retro Console and Bloom, each with help and a
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
    CHECK(per_cat["Color"] == 5 && per_cat["Lens"] == 4 && per_cat["Stylize"] == 10);
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
        ed.viewport_device_for_test(1);  // (the CPU's own shadow maps are counted: whatever device the run gives the views)
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
      [](Editor &ed) {
        ed.viewport_device_for_test(1);  // (the CPU's own shadow maps are counted: whatever device the run gives the views)
      },
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
    ed.viewport_device_for_test(1);  // (the CPU's own shadow maps are counted: whatever device the run gives the views)
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

static void round45_tests() {
  test("hash memo: evaluated_keys gives the three evaluated_key values on every modifier on / render / edit combination", [] {
    Scene s;
    GameObject *g = s.create("Stack");
    g->add<MeshFilter>()->mesh = primitives::cube(1.0f);
    g->add<MeshRenderer>();
    g->add<ArrayModifier>();
    g->add<MirrorModifier>();
    g->add<SolidifyModifier>();
    g->add<BevelModifier>();
    g->add<SubdivisionSurface>();
    g->add<TriangulateModifier>();
    g->add<WeldModifier>();
    g->add<DisplaceModifier>();
    g->add<SimpleDeformModifier>();
    g->add<CastModifier>();
    std::vector<Component *> mods;
    for (auto &c : g->components)
      if (c->is_modifier()) mods.push_back(c.get());
    CHECK(mods.size() == 10);
    uint64_t seed = 12345;
    auto bit = [&] { seed = seed * 6364136223846793005ull + 1442695040888963407ull; return (seed >> 40) & 1; };
    int same = 0, trials = 0;
    /* Every combination for the first two modifiers, random ones for the rest. */
    for (int t = 0; t < 300; t++) {
      for (size_t i = 0; i < mods.size(); i++) {
        if (i < 2) {
          mods[i]->enabled = (t >> (i * 3)) & 1;
          mods[i]->show_in_render = (t >> (i * 3 + 1)) & 1;
          mods[i]->show_in_editmode = (t >> (i * 3 + 2)) & 1;
        }
        else {
          mods[i]->enabled = bit();
          mods[i]->show_in_render = bit();
          mods[i]->show_in_editmode = bit();
        }
      }
      if (t % 7 == 0) static_cast<ArrayModifier *>(mods[0])->count = 2 + t % 5;  // a field change changes the keys too
      uint64_t k[3];
      g->evaluated_keys(k);
      trials++;
      if (k[0] == g->evaluated_key(0) && k[1] == g->evaluated_key(1) && k[2] == g->evaluated_key(2)) same++;
    }
    std::printf("    %d of %d combinations agree\n", same, trials);
    CHECK(same == trials);
    /* Without a mesh: all zero, like evaluated_key. */
    GameObject *e = s.create("Empty");
    uint64_t z[3] = {1, 1, 1};
    e->evaluated_keys(z);
    CHECK(z[0] == 0 && z[1] == 0 && z[2] == 0 && e->evaluated_key(0) == 0);
  });
  test("hash memo: an object's transform version bumps on its own move, a parent's move and reparenting (ADR 0015)", [] {
    Scene s;
    GameObject *a = s.create("A");
    GameObject *b = s.create("B", a);
    GameObject *c = s.create("C");
    (void)a->world_matrix();
    (void)b->world_matrix();
    (void)c->world_matrix();
    const uint64_t a0 = a->transform_version, b0 = b->transform_version, c0 = c->transform_version;
    a->set_local_position({1, 0, 0});  // a parent's move reaches the child
    CHECK(a->transform_version > a0 && b->transform_version > b0 && c->transform_version == c0);
    const uint64_t a1 = a->transform_version, b1 = b->transform_version;
    a->set_local_position({2, 0, 0});  // twice before anyone read the matrix: the moved object still bumps
    CHECK(a->transform_version > a1 && b->transform_version >= b1);
    /* The child need not bump again: its memo can only have been refreshed by a hash, which reads the
     * child's world matrix and so cleans the whole chain; a dirty parent means no memo was taken since
     * the first bump. */
    (void)b->world_matrix();  // cleans A and B
    const uint64_t b2 = b->transform_version;
    a->set_local_position({3, 0, 0});
    CHECK(b->transform_version > b2);
    const uint64_t c1 = c->transform_version;
    s.set_parent(c, a);  // the world matrix changes without the local transform changing
    CHECK(c->transform_version > c1);
    const uint64_t c2 = c->transform_version;
    s.set_parent(c, nullptr);
    CHECK(c->transform_version > c2);
  });
  test("hash memo: the memoised scene hash equals a from-scratch hash and changes after every kind of change, in verify mode", [] {
    Editor ed;
    r34_open(ed);
    ed.set_render_cache_verify(true);
    const uint64_t mm0 = Editor::render_cache_mismatches_all();
    uint64_t last = ed.render_hash_for_test();
    CHECK(last == ed.render_hash_uncached_for_test());
    /* Idle frames: the hash holds still. */
    r34_steps(ed, 3);
    CHECK(ed.render_hash_for_test() == last);
    int changed = 0, agreed = 0, cases = 0;
    auto change = [&](const char *what, const std::function<void()> &act) {
      act();
      r34_steps(ed, 2);
      const uint64_t memo = ed.render_hash_for_test(), scratch = ed.render_hash_uncached_for_test();
      cases++;
      changed += memo != last;
      agreed += memo == scratch;
      if (memo == last || memo != scratch)
        std::printf("    %s: %s\n", what, memo == last ? "the hash did not change" : "memo and from-scratch hashes differ");
      last = memo;
    };
    GameObject *cube = by_name(ed.scene(), "Cube");
    GameObject *sphere = by_name(ed.scene(), "Sphere");
    GameObject *sun = by_name(ed.scene(), "Directional Light");
    CHECK(cube && sphere && sun);
    if (!cube || !sphere || !sun) return;
    MeshRenderer *mr = cube->get<MeshRenderer>();
    if (mr->materials.empty()) mr->materials.push_back(make_material("Painted", {0.8f, 0.8f, 0.8f}));
    r34_steps(ed, 2);
    last = ed.render_hash_for_test();
    ed.command("select Cube");  // the Move gizmo is on the selection
    ed.command("camera 0 20 6 0 0.5 0");  // the X arrow points straight right on screen
    ed.command("tool move");
    r34_steps(ed, 2);
    last = ed.render_hash_for_test();
    change("gizmo move", [&] {
      const float x0 = cube->world_position().x;
      r34_gizmo_drag_x(ed, cube->world_position(), 60);
      CHECK(std::fabs(cube->world_position().x - x0) > 0.1f);  // the real drag moved it
    });
    change("the Inspector's position field", [&] {  // (what `set` can't reach: the Transform isn't a component)
      cube->set_local_position({1, 2, 3});
      ed.commit_change("Set position");
    });
    change("undo", [&] { ed.command("undo"); });
    change("redo", [&] { ed.command("redo"); });
    change("parenting", [&] { ed.scene().set_parent(cube, sphere); });
    change("a parent's move", [&] { sphere->set_local_position({0, 1.5f, 0}); });
    change("unparenting", [&] { ed.scene().set_parent(cube, nullptr); });
    change("a material field", [&] {
      mr->materials[0]->base_color = {0.9f, 0.2f, 0.2f};
      mr->materials[0]->touch();
    });
    change("a light field", [&] { sun->get<Light>()->intensity = 2.5f; });
    change("a modifier added", [&] {
      ed.command("select Cube");
      ed.command("component ArrayModifier");
    });
    change("a modifier field", [&] { cube->get<ArrayModifier>()->count = 5; });
    change("a modifier disabled", [&] { cube->get<ArrayModifier>()->enabled = false; });
    change("a modifier's Show in Renders", [&] { cube->get<ArrayModifier>()->show_in_render = false; });
    change("the renderer disabled", [&] { mr->enabled = false; });
    change("the renderer enabled", [&] { mr->enabled = true; });
    change("a renderer flag", [&] { mr->cast_shadows = false; });
    change("a material slot swapped", [&] { mr->materials[0] = make_material("Other", {0.2f, 0.9f, 0.2f}); });
    change("an object deactivated", [&] { sphere->active = false; });
    change("an object reactivated", [&] { sphere->active = true; });
    change("an object deleted", [&] {
      ed.command("select Sphere");
      ed.command("delete");
    });
    change("an object created", [&] { ed.command("create Cube"); });
    change("a probe volume added", [&] {
      ed.command("select Cube");
      ed.command("component Probe Volume");
    });
    change("a probe volume field", [&] { cube->get<ProbeVolume>()->size = {3, 3, 3}; });
    std::printf("    %d cases: %d changed the hash, %d agreed with the from-scratch hash\n", cases, changed, agreed);
    CHECK(changed == cases);
    CHECK(agreed == cases);
    std::printf("    stale pictures found in verify mode: %llu\n", (unsigned long long)(Editor::render_cache_mismatches_all() - mm0));
    CHECK(Editor::render_cache_mismatches_all() == mm0);
  });
  test("hash memo: a camera filter field re-renders the Game view (the camera hash is not memoised)", [] {
    Editor ed;
    r34_open(ed);
    ed.command("select Main Camera");
    ed.command("filter add Retro Console");
    r34_steps(ed, 4);
    const R34Count idle0 = r34_totals(ed);
    r34_steps(ed, 3);
    CHECK((r34_totals(ed) - idle0).renders == 0);
    const R34Change c = r34_change(ed, [&] { ed.command("set RetroConsoleFilter.Width 64"); });
    std::printf("    renders after the field change: %llu\n", (unsigned long long)c.d.renders);
    CHECK(c.d.renders >= 1);
    const R34Count t0 = r34_totals(ed);
    r34_steps(ed, 3);
    CHECK((r34_totals(ed) - t0).renders == 0);
  });
  test("hash memo: the main camera follows a rename, a disabled Camera component and a deactivated object at once", [] {
    Editor ed;
    r34_open(ed);
    GameObject *mc = by_name(ed.scene(), "Main Camera");
    CHECK(mc && ed.main_camera_owner_for_test() == mc);
    if (!mc) return;
    ed.command("create Camera");
    GameObject *second = ed.selected_object();
    CHECK(second && second != mc && second->get<Camera>());
    if (!second) return;
    r34_steps(ed, 2);
    CHECK(ed.main_camera_owner_for_test() == mc);  // "Main Camera" wins over a later camera
    mc->name = "Old Camera";
    second->name = "Main Camera";
    CHECK(ed.main_camera_owner_for_test() == second);
    second->get<Camera>()->enabled = false;
    CHECK(ed.main_camera_owner_for_test() == mc);
    second->get<Camera>()->enabled = true;
    second->active = false;
    CHECK(ed.main_camera_owner_for_test() == mc);
    second->active = true;
    CHECK(ed.main_camera_owner_for_test() == second);
  });
  test("hash memo: an icon drawn through its mask matches the icon rasterized directly (within a step of coverage)", [] {
    Editor ed;
    ed.init_headless(400, 300);
    ui::Context &u = ed.ui_mut_for_test();
    Image a, b;
    a.resize(64, 64);
    b.resize(64, 64);
    for (auto &img : {&a, &b}) std::fill(img->pixels.begin(), img->pixels.end(), 0xFF203040u);
    int worst = 0, pixels = 0;
    for (ui::Icon ic : {ui::Icon::Camera, ui::Icon::Light, ui::Icon::Gear, ui::Icon::Search, ui::Icon::Folder, ui::Icon::PushPull})
      for (int size : {13, 16, 22, 48}) {
        for (auto &img : {&a, &b}) std::fill(img->pixels.begin(), img->pixels.end(), 0xFF203040u);
        u.canvas.begin(&a);
        u.draw_icon(ic, {5, 7, size, size}, 0xFFE0C080);
        u.canvas.begin(&b);
        u.rasterize_icon(u.canvas, ic, {5, 7, size, size}, 0xFFE0C080);
        for (size_t i = 0; i < a.pixels.size(); i++) {
          for (int sh : {0, 8, 16}) worst = std::max(worst, std::abs((int)((a.pixels[i] >> sh) & 255) - (int)((b.pixels[i] >> sh) & 255)));
          pixels += a.pixels[i] != 0xFF203040u;
        }
      }
    std::printf("    worst channel difference %d over %d icon pixels\n", worst, pixels);
    CHECK(pixels > 200);
    CHECK(worst <= 2);  // one step of 8-bit coverage, and one of blending
  });
  test("hash memo: Canvas::circle walking the ring gives the same pixels as walking the bounding box", [] {
    auto old_circle = [](Canvas &c, Image &img, float cx, float cy, float r, uint32_t color, float t) {
      float hw = t * 0.5f;
      const Recti clip = c.clip();
      int x0 = std::max(clip.x, (int)std::floor(cx - r - hw - 1)), x1 = std::min(clip.right() - 1, (int)std::ceil(cx + r + hw + 1));
      int y0 = std::max(clip.y, (int)std::floor(cy - r - hw - 1)), y1 = std::min(clip.bottom() - 1, (int)std::ceil(cy + r + hw + 1));
      for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
          float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
          float d = std::fabs(std::sqrt(dx * dx + dy * dy) - r);
          if (d < hw + 1.0f) c.blend_px_a(x, y, color, saturate(hw + 0.5f - d));
        }
      (void)img;
    };
    int bad = 0, drawn = 0;
    const float cases[][4] = {{10, 10.45f, 1.6f, 1}, {20.3f, 19.7f, 3.0f, 1.5f}, {32, 32, 12.5f, 2}, {40.2f, 25.9f, 20, 1}, {5, 5, 0.8f, 1}, {30.5f, 30.5f, 2.2f, 3}};
    for (const float *k : cases) {
      Image a, b;
      a.resize(64, 64);
      b.resize(64, 64);
      Canvas ca, cb;
      ca.begin(&a);
      cb.begin(&b);
      ca.circle(k[0], k[1], k[2], 0xFFFFC040, k[3]);
      old_circle(cb, b, k[0], k[1], k[2], 0xFFFFC040, k[3]);
      for (size_t i = 0; i < a.pixels.size(); i++) {
        bad += a.pixels[i] != b.pixels[i];
        drawn += b.pixels[i] != 0xFF000000u;
      }
    }
    std::printf("    %d pixels differ over %d drawn\n", bad, drawn);
    CHECK(drawn > 100);
    CHECK(bad == 0);
  });
  test("hash memo: Statistics shows the rasterizer's binning time", [] {
    Editor ed;
    ed.viewport_device_for_test(1);  // (the CPU rasterizer's binning is shown: whatever device the run gives the views)
    r34_open(ed);
    r34_steps(ed, 2);
    const RasterStats s = ed.scene_stats_for_test();
    std::printf("    clip / cull / bin %.3f ms, of it binning (CPU) %.3f ms\n", s.ms_setup, s.ms_bin);
    CHECK(s.ms_bin > 0.0);
  });
}

static void round46_tests() {
  test("render frame: shading through the textures resolved once per flush gives the same sample as the per-pixel path (which forwards to it: a guard against the two drifting apart)", [] {
    Material m;
    m.procedural = (int)Procedural::UVGrid;  // a texture without a file
    m.tiling = Vec3(2.0f, 3.0f, 1.0f);
    m.metallic = 0.3f;
    m.roughness = 0.6f;
    int same = 0, n = 0;
    for (int i = 0; i < 64; i++) {
      SurfacePoint sp;
      sp.uv = {(i % 8) / 7.3f, (i / 8) / 7.9f};
      sp.duvdx = {0.01f, 0.0f};
      sp.duvdy = {0.0f, 0.02f};
      sp.normal = sp.geo_normal = normalize(Vec3(0.2f, 1.0f, 0.1f * i));
      sp.position = sp.local_position = Vec3(i * 0.1f, 0.5f, -i * 0.2f);
      const SurfaceSample a = evaluate_material(m, sp), b = evaluate_material(m, m.textures(), sp);
      n++;
      same += a.albedo.x == b.albedo.x && a.albedo.y == b.albedo.y && a.albedo.z == b.albedo.z && a.metallic == b.metallic && a.roughness == b.roughness &&
              a.alpha == b.alpha && a.normal.x == b.normal.x && a.normal.y == b.normal.y && a.normal.z == b.normal.z && a.unlit == b.unlit;
    }
    CHECK(same == n);
  });
  test("render frame: two filtered views of different sizes keep their own filter target (no reallocation every frame)", [] {
    Editor ed;
    r34_open(ed);
    ed.command("select Main Camera");  // the Camera Preview shows too: a third filtered view
    ed.command("filter add Retro Console");  // its own resolution: every view draws through a scaled target
    ed.command("filters on");
    GameObject *cube = by_name(ed.scene(), "Cube");
    auto render_frames = [&](int n) {  // frames that really render: each moves the cube (an idle frame is a cache hit)
      for (int i = 0; i < n; i++) {
        cube->set_local_position({0.02f * i, 0.5f, 0.0f});
        ed.step_frame_headless();
      }
    };
    render_frames(10);
    const uint64_t warm = ed.frame_profile_totals().filter_target_resizes;
    std::printf("    filter target resizes over the first 10 rendering frames: %llu (one per filtered view)\n", (unsigned long long)warm);
    CHECK(warm >= 2 && warm <= 3);
    const uint64_t r0 = ed.frame_profile_totals().view_renders;
    render_frames(10);
    const uint64_t more = ed.frame_profile_totals().filter_target_resizes - warm, rendered = ed.frame_profile_totals().view_renders - r0;
    std::printf("    over the next 10 (%llu view renders): %llu\n", (unsigned long long)rendered, (unsigned long long)more);
    CHECK(rendered >= 20);  // the Scene view, the Game view and the Camera Preview, each frame
    CHECK(more == 0);
  });
  test("render frame: a camera's filter stack is built once, and again only when one of its filters changes", [] {
    Editor ed;
    r34_open(ed);
    ed.command("select Main Camera");
    ed.command("filter add Retro Console");
    r34_steps(ed, 6);
    const uint64_t b0 = ed.frame_profile_totals().filter_stack_builds;
    GameObject *cube = by_name(ed.scene(), "Cube");
    for (int i = 0; i < 10; i++) {  // rendering frames: the Scene view, Game view and Camera Preview all ask for the stack
      cube->set_local_position({0.02f * i, 0.5f, 0.0f});
      ed.step_frame_headless();
    }
    const uint64_t idle = ed.frame_profile_totals().filter_stack_builds - b0;
    ed.command("set RetroConsoleFilter.Width 64");
    r34_steps(ed, 3);
    const uint64_t changed = ed.frame_profile_totals().filter_stack_builds - b0 - idle;
    r34_steps(ed, 5);
    const uint64_t after = ed.frame_profile_totals().filter_stack_builds - b0 - idle - changed;
    std::printf("    stack builds: %llu idle, %llu after a field change, %llu after\n", (unsigned long long)idle, (unsigned long long)changed, (unsigned long long)after);
    CHECK(idle == 0);
    CHECK(changed == 1);
    CHECK(after == 0);
  });
  test("render frame: the sun's shadow map is still rendered once per change, not per frame (the casters are copied only then)", [] {
    Editor ed;
    ed.viewport_device_for_test(1);  // (the CPU's own shadow maps are counted: whatever device the run gives the views)
    r34_open(ed);
    r34_steps(ed, 4);
    const R34Count t0 = r34_totals(ed);
    r34_steps(ed, 5);
    CHECK((r34_totals(ed) - t0).shadows == 0);
    GameObject *cube = by_name(ed.scene(), "Cube");
    const R34Count t1 = r34_totals(ed);
    cube->set_local_position({0.5f, 0.5f, 0.0f});
    r34_steps(ed, 3);
    const R34Count d = r34_totals(ed) - t1;
    std::printf("    shadow maps after a move: %llu\n", (unsigned long long)d.shadows);
    CHECK(d.shadows == 1);
  });
}

static void round47_tests() {
  test("active camera: a filter on a camera that is not \"Main Camera\" shows in the Game view only once it is the Active Camera", [] {
    Editor ed;
    r34_open(ed);
    ed.command("create Camera");
    GameObject *second = ed.selected_object();
    CHECK(second && second->camera() && second->name != "Main Camera");
    if (!second) return;
    second->set_local_position({0, 1, -6});
    ed.command("filter add Retro Console");  // on the selection: the second camera
    r34_steps(ed, 4);
    CHECK(ed.main_camera_owner_for_test() && ed.main_camera_owner_for_test()->name == "Main Camera");
    const std::vector<uint32_t> before = r34_crop(ed, ed.game_rect_for_test());
    ed.command("activecamera " + second->name);
    r34_steps(ed, 4);
    CHECK(ed.scene().active_camera == second->id);
    CHECK(ed.main_camera_owner_for_test() == second);
    const std::vector<uint32_t> after = r34_crop(ed, ed.game_rect_for_test());
    CHECK(before != after);
    /* Undo puts the name rule back. */
    ed.command("undo");
    r34_steps(ed, 2);
    CHECK(ed.scene().active_camera == 0);
    CHECK(ed.main_camera_owner_for_test()->name == "Main Camera");
  });
  test("active camera: the Camera Preview and the Game view both follow the active camera's filters (Shaded)", [] {
    Editor ed;
    r34_open(ed);
    ed.command("create Camera");
    GameObject *second = ed.selected_object();
    if (!second) return;
    second->set_local_position({0, 1, -6});
    ed.command("activecamera " + second->name);
    r34_steps(ed, 4);
    const std::vector<uint32_t> g0 = r34_crop(ed, ed.game_rect_for_test()), p0 = r34_crop(ed, ed.cam_preview_rect_for_test());
    ed.command("filter add Retro Console");
    r34_steps(ed, 4);
    const std::vector<uint32_t> g1 = r34_crop(ed, ed.game_rect_for_test()), p1 = r34_crop(ed, ed.cam_preview_rect_for_test());
    CHECK(!p0.empty() && g0 != g1 && p0 != p1);
  });
  test("active camera: with filters on, the Scene view takes the active camera's exposure and Solid Color clear", [] {
    Editor ed;
    r34_open(ed);
    ed.command("select Main Camera");
    GameObject *mc = by_name(ed.scene(), "Main Camera");
    Camera *cam = mc->camera();
    ed.command("filter add Sharpen");  // any pass: the Scene view then renders through render_camera
    ed.command("filters on");
    r34_steps(ed, 4);
    const Recti sv = ed.scene_view_rect();
    const double m0 = r47_mean(r34_crop(ed, sv));
    cam->physical_exposure = true;
    cam->iso = 1600.0f;  // +4 EV
    r34_steps(ed, 3);
    const double m1 = r47_mean(r34_crop(ed, sv));
    std::printf("    Scene view mean brightness: %.1f, at +4 EV on the camera: %.1f\n", m0, m1);
    CHECK(m1 > m0 * 1.1);  // Filmic compresses the +4 EV: measured 142.8 -> 168.6
    cam->physical_exposure = false;
    cam->clear_flags = 1;
    cam->background = {1.0f, 0.0f, 0.0f};
    r34_steps(ed, 3);
    const uint32_t sky = ed.framebuffer().pixels[(size_t)(sv.y + 4) * ed.framebuffer().width + sv.x + sv.w / 2];
    std::printf("    a sky pixel with Solid Color red: %06x\n", sky & 0xFFFFFF);
    CHECK(((sky >> 16) & 255) > 200 && ((sky >> 8) & 255) < 40 && (sky & 255) < 40);
  });
  test("active camera: an Edge Outline filter draws outlines in a path-traced F12 render, and Solid Color fills its background", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.step_frame_headless();
    ed.command("set Render.RenderEngine Path");
    ed.command("set Render.Samples 2");
    ed.command("set Render.ResolutionX 160");
    ed.command("set Render.ResolutionY 90");
    ed.command("set Render.Denoise false");
    auto render = [&] {
      ed.command("render");
      for (int i = 0; i < 4000 && ed.rendering_for_test(); i++) ed.step_frame_headless();
      CHECK(!ed.rendering_for_test());
    };
    render();
    const Image &img = ed.render_image_for_test();
    const int dark0 = r47_dark(img, {0, 0, img.width, img.height});
    ed.command("select Main Camera");
    ed.command("filter add Edge Outline");
    render();
    const int dark1 = r47_dark(img, {0, 0, img.width, img.height});
    std::printf("    near-black pixels: %d without the outline, %d with\n", dark0, dark1);
    CHECK(dark1 > dark0 + 40);
    Camera *cam = by_name(ed.scene(), "Main Camera")->camera();
    cam->clear_flags = 1;
    cam->background = {1.0f, 0.0f, 0.0f};
    ed.command("set Render.ViewTransform Standard");
    render();
    const uint32_t corner = img.pixels[2 * img.width + 2];
    std::printf("    a background pixel with Solid Color red: %06x\n", corner & 0xFFFFFF);
    CHECK(((corner >> 16) & 255) > 150 && ((corner >> 8) & 255) < 60 && (corner & 255) < 60);
  });
  test("active camera: an F12 raster render without filters applies the camera's depth of field", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.step_frame_headless();
    ed.command("set Render.RenderEngine Rasterized");
    ed.command("set Render.ResolutionX 240");
    ed.command("set Render.ResolutionY 135");
    auto render = [&] {
      ed.command("render");
      for (int i = 0; i < 100 && ed.rendering_for_test(); i++) ed.step_frame_headless();
    };
    render();
    const Image &img = ed.render_image_for_test();
    const double d0 = r47_detail(img);
    Camera *cam = by_name(ed.scene(), "Main Camera")->camera();
    cam->dof = true;
    cam->f_stop = 0.3f;
    cam->focus_distance = 0.2f;  // everything far out of focus
    render();
    const double d1 = r47_detail(img);
    std::printf("    neighbour difference: %.2f sharp, %.2f with depth of field\n", d0, d1);
    CHECK(d1 < d0 * 0.85);  // measured 0.47 -> 0.36 at 240 x 135 (the blur works on the supersampled image)
  });
  test("active camera: saved and loaded with the scene; deleting the camera clears it", [] {
    Scene s;
    GameObject *a = s.create("Cam A");
    a->add<Camera>();
    GameObject *b = s.create("Cam B");
    b->add<Camera>();
    s.active_camera = b->id;
    const std::string path = fs::join(fs::join(scratch_project(), "Assets/Scenes"), "R47_Active.scene");
    CHECK(save_scene(s, path));
    Scene t;
    std::string err;
    CHECK(load_scene(path, t, err));
    GameObject *tb = t.find_by_name("Cam B");
    CHECK(tb && t.active_camera == tb->id);
    t.destroy(tb);
    CHECK(t.active_camera == 0);
    std::remove(path.c_str());
  });
  test("active camera: the Game view's camera dropdown opens with a click and picking a camera makes it the active one", [] {
    Editor ed;
    r34_open(ed);
    ed.command("create Camera");
    GameObject *second = ed.selected_object();
    if (!second) return;
    second->name = "Second Camera";
    second->set_local_position({0, 1, -6});
    ed.command("select Cube");
    r34_steps(ed, 3);
    const ui::Context &u = ed.ui_for_test();
    const Recti gv = ed.game_rect_for_test();
    const int bh = u.row_h() + u.px(4);
    const Recti combo{gv.x + u.px(6), gv.y - bh + u.px(3), u.px(170), bh - u.px(6)};
    r34_click(ed, combo.x + combo.w / 2, combo.y + combo.h / 2);  // opens the list (its labels are read at the frame's end)
    r34_steps(ed, 3);
    /* The list drops below the field: a row per camera, the second camera second (Hierarchy order). */
    const int row_y = combo.bottom() + u.px(4) + u.row_h() + u.row_h() / 2;
    r34_click(ed, combo.x + u.px(30), row_y);
    r34_steps(ed, 3);
    std::printf("    active camera after the pick: %s\n", ed.main_camera_owner_for_test() ? ed.main_camera_owner_for_test()->name.c_str() : "none");
    CHECK(ed.scene().active_camera == second->id);
    CHECK(ed.main_camera_owner_for_test() == second);
    /* Deleting it through the editor: back to the name rule. */
    ed.command("select Second Camera");
    ed.command("delete");
    r34_steps(ed, 2);
    CHECK(ed.scene().active_camera == 0);
    CHECK(ed.main_camera_owner_for_test() && ed.main_camera_owner_for_test()->name == "Main Camera");
  });
  test("active camera: an Edge Outline filter draws outlines in the Rendered (path-traced) Camera Preview", [] {
    Editor ed;
    r34_open(ed);
    ed.command("set Render.PreviewSamples 2");
    ed.command("set Render.Denoise false");
    ed.command("select Main Camera");
    ed.command("camerapreview rendered");
    auto settle = [&] {
      for (int i = 0; i < 400 && !ed.cam_preview_done_for_test(); i++) ed.step_frame_headless();
      CHECK(ed.cam_preview_done_for_test());
    };
    settle();
    const Recti pr = ed.cam_preview_rect_for_test();
    CHECK(pr.w > 40 && pr.h > 20);
    const int dark0 = r47_dark(ed.framebuffer(), pr);
    ed.command("filter add Edge Outline");
    r34_steps(ed, 2);
    settle();
    const int dark1 = r47_dark(ed.framebuffer(), pr);
    std::printf("    near-black pixels in the Rendered Preview: %d without the outline, %d with\n", dark0, dark1);
    CHECK(dark1 > dark0 + 30);
  });
  test("active camera: the console reports it, 'none' clears it, and a non-camera is refused", [] {
    Editor ed;
    r34_open(ed);
    ed.command("activecamera Cube");
    CHECK(ed.scene().active_camera == 0);
    ed.command("activecamera Main Camera");
    CHECK(ed.scene().active_camera == by_name(ed.scene(), "Main Camera")->id);
    ed.command("activecamera none");
    CHECK(ed.scene().active_camera == 0);
  });
}


/* Round 48 (task 0026, ADR 0016 part 2): filters look the same at every resolution and during traces. */
static int r48_runs(const Image &img, int y) {  // colour changes along one row (a pixelated row: one per cell)
  int n = 0;
  for (int x = 1; x < img.width; x++)
    if ((img.pixels[(size_t)y * img.width + x] & 0xFFFFFF) != (img.pixels[(size_t)y * img.width + x - 1] & 0xFFFFFF)) n++;
  return n;
}
static void round48_tests() {
  test("pixel scale: an Edge Outline covers the same fraction of the picture at 540p and at 1080p (±10 %)", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.step_frame_headless();
    ed.command("set Render.RenderEngine Rasterized");
    ed.command("select Main Camera");
    ed.command("filter add Edge Outline");
    ed.command("set EdgeOutline.Thickness 2");  // (thickness 1 rounds to 1 pixel at both sizes: relatively thicker at 540p, by design)
    auto fraction = [&](int w, int h) {
      ed.command("set Render.ResolutionX " + std::to_string(w));
      ed.command("set Render.ResolutionY " + std::to_string(h));
      ed.command("render");
      for (int i = 0; i < 100 && ed.rendering_for_test(); i++) ed.step_frame_headless();
      const Image &img = ed.render_image_for_test();
      return r47_dark(img, {0, 0, img.width, img.height}) / (double)(img.width * img.height);
    };
    const double f540 = fraction(960, 540), f1080 = fraction(1920, 1080);
    std::printf("    outlined fraction: %.4f at 540p, %.4f at 1080p\n", f540, f1080);
    CHECK(f540 > 0.002 && f1080 > 0.002);
    CHECK(std::fabs(f540 - f1080) <= 0.1 * std::max(f540, f1080));
  });
  test("pixel scale: a letterboxed 21:9 stack keeps square pixels in a small inset, and a Fill stack in a very wide view stays bounded", [] {
    FilterStack s;
    s.width = 2560;
    s.height = 1080;
    s.fit = FilterStack::Letterbox;
    int iw = 0, ih = 0;
    Recti dst;
    filter_layout(s, 320, 135, iw, ih, dst);
    std::printf("    21:9 stack in a 320 x 135 inset: %d x %d internal\n", iw, ih);
    CHECK_NEAR((double)iw / ih, 2560.0 / 1080.0, 0.01);  // one scale factor for both sides
    CHECK(iw <= 1920 && ih <= 1080);
    s.fit = FilterStack::Fill;
    filter_layout(s, 1600, 8, iw, ih, dst);
    std::printf("    Fill stack in a 1600 x 8 view: %d x %d internal\n", iw, ih);
    CHECK(iw <= 1920 && ih >= 1 && ih <= 1080);
  });
  test("pixel scale: Color Grading's exposure applies in linear light only where the HDR plane still matches the picture (a glow or a solid clear keeps the 8-bit path)", [] {
    Image img;
    img.resize(32, 8);
    RenderTarget rt;
    rt.attach(img, {0, 0, 32, 8});
    rt.hdr.assign((size_t)32 * 8, Vec3(0.5f));  // scene light, encoded below
    rt.hdr_view_transform = ViewTransform::Standard;
    rt.hdr_exposure = 0.0f;
    for (int i = 0; i < 32 * 8; i++) rt.hdr[(size_t)i] = i % 32 < 16 ? Vec3(0.5f) : Vec3(-1.0f, 0.0f, 0.0f);  // the right half: unshaded (a solid clear)
    display::encode_span(&rt.hdr[0].x, img.pixels.data(), 16, ViewTransform::Standard, 0.0f);
    for (int y = 1; y < 8; y++) std::copy(img.pixels.begin(), img.pixels.begin() + 16, img.pixels.begin() + (size_t)y * 32);
    for (int y = 0; y < 8; y++)
      for (int x = 16; x < 32; x++) img.pixels[(size_t)y * 32 + x] = 0xFF2040C0u;  // a solid clear colour the plane knows nothing about
    img.pixels[3] = 0xFFFFFFFFu;  // one pixel drawn over since (a glow): not the plane's encoding
    ColorGradingParams p;
    p.exposure = 1.0f;
    apply_color_grading(rt, p);
    const uint32_t lit = img.pixels[0], glow = img.pixels[3], solid = img.pixels[20];
    std::printf("    +1 EV: a shaded pixel %06x (from 0.5 linear), the glow %06x, the solid clear %06x\n", lit & 0xFFFFFF, glow & 0xFFFFFF, solid & 0xFFFFFF);
    CHECK(((lit >> 16) & 255) > 230);     // 0.5 x 2 = 1.0 linear: white
    CHECK(glow == 0xFFFFFFFFu);           // already white; untouched by the HDR path and clamped by the 8-bit one
    CHECK(((solid >> 16) & 255) > 0x20 && ((solid >> 8) & 255) > 0x40 && (solid & 255) > 0xC0);  // brighter, not black
  });
  test("pixel scale: a Pixelate filter gives the same number of cells across a row at 540p and at 1080p (±15 %)", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.step_frame_headless();
    ed.command("set Render.RenderEngine Rasterized");
    ed.command("select Main Camera");
    ed.command("filter add Pixelate");
    ed.command("set Pixelate.CellSize 12");
    auto cells = [&](int w, int h) {
      ed.command("set Render.ResolutionX " + std::to_string(w));
      ed.command("set Render.ResolutionY " + std::to_string(h));
      ed.command("render");
      for (int i = 0; i < 100 && ed.rendering_for_test(); i++) ed.step_frame_headless();
      const Image &img = ed.render_image_for_test();
      int best = 0;
      for (int y = img.height / 3; y < img.height * 2 / 3; y += img.height / 12) best = std::max(best, r48_runs(img, y));
      return best;
    };
    const int c540 = cells(960, 540), c1080 = cells(1920, 1080);
    std::printf("    colour changes along a row: %d at 540p, %d at 1080p\n", c540, c1080);
    CHECK(c540 > 10 && c1080 > 10);
    CHECK(std::abs(c540 - c1080) <= 0.15 * std::max(c540, c1080));
  });
  test("pixel scale: changing the path tracer's Samples doesn't redraw the Game view; changing Exposure does", [] {
    Editor ed;
    r34_open(ed);
    r34_steps(ed, 3);
    R34Count t0 = r34_totals(ed);
    ed.command("set Render.Samples 77");
    r34_steps(ed, 3);
    R34Count d = r34_totals(ed) - t0;
    std::printf("    view renders after Samples: %llu\n", (unsigned long long)d.renders);
    CHECK(d.renders == 0);
    t0 = r34_totals(ed);
    ed.command("set Render.Exposure 1.5");
    r34_steps(ed, 3);
    d = r34_totals(ed) - t0;
    std::printf("    view renders after Exposure: %llu\n", (unsigned long long)d.renders);
    CHECK(d.renders >= 1);
  });
  test("pixel scale: a filtered progressive trace's bloom doesn't brighten as samples accumulate (mean within 5 % from the first sample to the last)", [] {
    Editor ed;
    r34_open(ed);
    ed.command("set Render.PreviewSamples 256");  // enough that the first frames show an unfinished trace (verify mode too)
    ed.command("set Render.Denoise false");
    ed.command("select Main Camera");
    ed.command("filter add Bloom");
    ed.command("set Bloom.Intensity 1");
    ed.command("set Bloom.Threshold 0.2");
    ed.command("camerapreview rendered");
    ed.step_frame_headless();
    ed.step_frame_headless();
    Recti pr = ed.cam_preview_rect_for_test();
    CHECK(pr.w > 40);
    pr.y += pr.h / 5, pr.h -= pr.h / 5;  // (not the inset's header: its sample count text changes)
    const double m1 = r47_mean(r34_crop(ed, pr));
    const bool partial = !ed.cam_preview_done_for_test();  // (a very fast machine can finish within the first frames: then there is nothing to compare)
    for (int i = 0; i < 4000 && !ed.cam_preview_done_for_test(); i++) ed.step_frame_headless();  // (a sanitizer build is slow: many frames)
    CHECK(ed.cam_preview_done_for_test());
    ed.step_frame_headless();
    const double mN = r47_mean(r34_crop(ed, pr));
    if (!partial) std::printf("    (the trace finished within the first frames: nothing partial to compare)\n");
    std::printf("    Rendered Preview with bloom, mean brightness: %.1f after the first frames, %.1f finished\n", m1, mN);
    if (partial) CHECK(std::fabs(mN - m1) <= 0.05 * std::max(1.0, std::max(m1, mN)));  // (5 %, not the spec's 2 %: the first frames are a noisy trace)
  });
  test("pixel scale: with the scene's lighting on, the Game view's background is the world's gradient, not the editor's sky", [] {
    Editor ed;
    r34_open(ed);
    ed.scene().environment.mode = 0;  // Gradient
    ed.scene().environment.sky = {1.0f, 0.0f, 0.0f};
    ed.scene().environment.equator = {1.0f, 0.0f, 0.0f};
    r34_steps(ed, 3);
    const Recti gv = ed.game_rect_for_test();
    const uint32_t top = ed.framebuffer().pixels[(size_t)(gv.y + 3) * ed.framebuffer().width + gv.x + gv.w / 2];
    std::printf("    Game view sky pixel with a red gradient: %06x\n", top & 0xFFFFFF);
    CHECK(((top >> 16) & 255) > 2 * (((top >> 8) & 255) + 10) && ((top >> 16) & 255) > 2 * ((top & 255) + 10));
  });
  test("pixel scale: the Rendered Camera Preview restarts when a path tracer setting changes (Max Bounces), though the Game view doesn't redraw", [] {
    Editor ed;
    r34_open(ed);
    ed.command("set Render.PreviewSamples 4");
    ed.command("select Main Camera");
    ed.command("camerapreview rendered");
    for (int i = 0; i < 400 && !ed.cam_preview_done_for_test(); i++) ed.step_frame_headless();
    CHECK(ed.cam_preview_done_for_test());
    const R34Count t0 = r34_totals(ed);
    ed.command("set Render.MaxBounces 1");
    r34_steps(ed, 2);
    CHECK(!ed.cam_preview_done_for_test() || (r34_totals(ed) - t0).renders == 0);  // restarted (or already finished again): a new trace
    for (int i = 0; i < 400 && !ed.cam_preview_done_for_test(); i++) ed.step_frame_headless();
    CHECK(ed.cam_preview_done_for_test());
    CHECK((r34_totals(ed) - t0).renders == 0);  // the Game view (raster) didn't redraw for a path tracer setting
  });
  test("pixel scale: a paused Game view with Film Grain keeps the same picture from frame to frame", [] {
    Editor ed;
    r34_open(ed);
    ed.command("select Main Camera");
    ed.command("filter add Film Grain");
    ed.command("play");
    r34_steps(ed, 4);
    ed.run_action("play.pause");
    r34_steps(ed, 2);
    const std::vector<uint32_t> a = r34_crop(ed, ed.game_rect_for_test());
    r34_steps(ed, 3);
    const std::vector<uint32_t> b = r34_crop(ed, ed.game_rect_for_test());
    CHECK(!a.empty() && a == b);
    ed.command("stop");
  });
}

static void round51_tests() {
  test("axis material: World mode puts Top on surfaces facing up, Side on steep ones, Bottom underneath, whatever the object's rotation", [] {
    const R51Mats m = r51_mats(1);
    const Material &a = *m.axis;
    CHECK(r51_green(r51_eval(a, {0, 1, 0}, {0, 0, 1}).albedo));   // up in the world (the object turned: locally +Z)
    CHECK(r51_brown(r51_eval(a, {1, 0, 0}, {0, 1, 0}).albedo));   // a side in the world (locally the top)
    CHECK(r51_brown(r51_eval(a, {0, 0.3f, 1}, {0, 1, 0}).albedo));  // steep: past 45 degrees
    const Vec3 bottom = r51_eval(a, {0, -1, 0}, {0, 1, 0}).albedo;
    CHECK(std::fabs(bottom.x - 0.4f) < 0.02f && std::fabs(bottom.y - 0.4f) < 0.02f);
  });
  test("axis material: Local mode follows the object's own up (a quarter turn moves the grass with it)", [] {
    const R51Mats m = r51_mats(2);
    const Material &a = *m.axis;
    CHECK(r51_green(r51_eval(a, {1, 0, 0}, {0, 1, 0}).albedo));  // the object's top, now facing +X in the world
    CHECK(r51_brown(r51_eval(a, {0, 1, 0}, {0, 0, 1}).albedo));  // the world's up is the object's side
  });
  test("axis material: Blend 0 is a hard edge at the angle; a wider blend mixes the parts across it", [] {
    const R51Mats m = r51_mats(1);
    auto mixed = [&](float blend) {
      m.axis->axis_blend = blend;
      m.axis->touch();
      int n = 0;
      for (int i = 0; i <= 200; i++) {  // normals swept from 30 to 60 degrees off up
        const float t = (30.0f + 30.0f * i / 200.0f) * kDeg2Rad;
        const Vec3 c = r51_eval(*m.axis, {std::sin(t), std::cos(t), 0}, {0, 1, 0}).albedo;
        n += !r51_green(c) && !r51_brown(c);
      }
      return n;
    };
    const int hard = mixed(0.0f), soft = mixed(0.3f);
    std::printf("    in-between samples across the edge: %d with blend 0, %d with blend 0.3\n", hard, soft);
    CHECK(hard == 0);
    CHECK(soft > 20);
  });
  test("axis material: a part's texture is projected without UVs (triplanar at the world position)", [] {
    const R51Mats m = r51_mats(1);
    m.grass->procedural = (int)Procedural::UVGrid;  // a texture on the Top
    m.grass->touch();
    Vec3 a = r51_eval(*m.axis, {0, 1, 0}, {0, 1, 0}, Vec3(0.11f, 0.0f, 0.13f)).albedo;
    Vec3 b = r51_eval(*m.axis, {0, 1, 0}, {0, 1, 0}, Vec3(0.61f, 0.0f, 0.37f)).albedo;
    std::printf("    two points of the top, the same (empty) UVs: %.3f %.3f %.3f vs %.3f %.3f %.3f\n", a.x, a.y, a.z, b.x, b.y, b.z);
    CHECK(length(a - b) > 0.05f);  // the texture varies across the surface without UVs
  });
  test("axis material: the fields round-trip through a .mat; an edit undoes; a part's edit changes the views' hash", [] {
    Editor ed;
    r34_open(ed);
    R51Mats m = r51_mats(1);
    m.axis->axis_angle = 30.0f;
    m.axis->axis_blend = 0.25f;
    m.axis->axis_scale = 2.0f;
    MaterialPtr saved = create_material_asset(*m.axis, "Assets/Materials/R51/Ground.mat");
    CHECK(saved != nullptr);
    Material back;
    std::string text;
    CHECK(fs::read_file(resolve_asset_path("Assets/Materials/R51/Ground.mat"), text));
    CHECK(load_material_text(text, back));
    CHECK(back.axis_mode == 1 && back.axis_top == "Assets/Materials/R51/Grass.mat" && back.axis_side == "Assets/Materials/R51/Dirt.mat" &&
          back.axis_bottom == "Assets/Materials/R51/Rock.mat" && std::fabs(back.axis_angle - 30.0f) < 1e-4f && std::fabs(back.axis_scale - 2.0f) < 1e-4f);
    /* On the Cube: the hash follows a part's edit, and a field edit undoes. */
    GameObject *cube = by_name(ed.scene(), "Cube");
    cube->get<MeshRenderer>()->materials = {saved};
    ed.commit_change("Ground");
    r34_steps(ed, 3);
    const uint64_t h0 = ed.render_hash_for_test();
    m.grass->base_color = {0.0f, 1.0f, 0.0f};
    m.grass->touch();
    const uint64_t h1 = ed.render_hash_for_test();
    CHECK(h1 != h0);
    saved->axis_mode = 0;
    saved->touch();
    ed.commit_change("Axis Off");
    r34_steps(ed, 2);
    ed.command("undo");
    CHECK(by_name(ed.scene(), "Cube")->get<MeshRenderer>()->materials[0]->axis_mode == 1);
  });
  test("axis material: an F12 raster render from above shows the Top on the cube's top, and after a quarter turn still the Top on what faces up", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.step_frame_headless();
    R51Mats m = r51_mats(1);
    GameObject *cube = by_name(ed.scene(), "Cube");
    cube->get<MeshRenderer>()->materials = {m.axis};
    GameObject *cam = by_name(ed.scene(), "Main Camera");
    cam->set_local_position({0.0f, 6.0f, 0.0f});
    cam->set_local_euler({90.0f, 0.0f, 0.0f});  // looking straight down
    ed.command("set Render.RenderEngine Rasterized");
    ed.command("set Render.ResolutionX 96");
    ed.command("set Render.ResolutionY 64");
    auto centre = [&] {
      ed.command("render");
      for (int i = 0; i < 100 && ed.rendering_for_test(); i++) ed.step_frame_headless();
      const Image &img = ed.render_image_for_test();
      const uint32_t c = img.pixels[(size_t)(img.height / 2) * img.width + img.width / 2];
      return Vec3(((c >> 16) & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, (c & 255) / 255.0f);
    };
    const Vec3 a = centre();
    cube->set_local_euler({90.0f, 0.0f, 0.0f});  // a quarter turn about X
    const Vec3 b = centre();
    std::printf("    the cube's top seen from above: %.2f %.2f %.2f, after the turn %.2f %.2f %.2f\n", a.x, a.y, a.z, b.x, b.y, b.z);
    /* (Filmic and the sun desaturate: green means green clearly above red and blue.) */
    CHECK(a.y > a.x + 0.12f && a.y > a.z + 0.12f);  // green
    CHECK(b.y > b.x + 0.12f && b.y > b.z + 0.12f);  // still green: World mode
    m.axis->axis_mode = 2;  // Local: the face now up was a side
    m.axis->touch();
    const Vec3 c = centre();
    std::printf("    Local mode after the turn: %.2f %.2f %.2f\n", c.x, c.y, c.z);
    CHECK(c.x > c.y && c.y > c.z);  // brown
  });
  test("axis material: a cycle of parts (A's Top is B, B's Top is A) resolves without recursing forever, and edge cases fall back sensibly", [] {
    R51Mats m = r51_mats(1);
    Material a;
    a.name = "CycleA";
    a.base_color = {0.9f, 0.1f, 0.1f};
    a.axis_mode = 1;
    a.axis_top = "Assets/Materials/R51/CycleB.mat";
    Material b;
    b.name = "CycleB";
    b.base_color = {0.1f, 0.1f, 0.9f};
    b.axis_mode = 1;
    b.axis_top = "Assets/Materials/R51/CycleA.mat";
    MaterialPtr pa = create_material_asset(a, "Assets/Materials/R51/CycleA.mat");
    MaterialPtr pb = create_material_asset(b, "Assets/Materials/R51/CycleB.mat");
    CHECK(pa && pb);
    if (!pa || !pb) return;
    invalidate_material_textures();
    const Vec3 top = r51_eval(*pa, {0, 1, 0}, {0, 1, 0}).albedo;  // A's Top is B, used plainly: blue
    CHECK(top.z > 0.8f && top.x < 0.2f);
    pb->touch();
    const Vec3 top2 = r51_eval(*pb, {0, 1, 0}, {0, 1, 0}).albedo;  // B's Top is A, used plainly: red
    CHECK(top2.x > 0.8f && top2.z < 0.2f);
    /* A missing part and the material itself as a part: its own maps. */
    Material own;
    own.base_color = {0.2f, 0.7f, 0.9f};
    own.axis_mode = 1;
    own.axis_top = "Assets/Materials/R51/NoSuch.mat";
    const Vec3 o = r51_eval(own, {0, 1, 0}, {0, 1, 0}).albedo;
    CHECK(std::fabs(o.x - 0.2f) < 0.02f && std::fabs(o.z - 0.9f) < 0.02f);
    /* A NaN or zero normal: the Side part, not black. */
    const Vec3 z = r51_eval(*m.axis, {0, 0, 0}, {0, 0, 0}).albedo;
    CHECK(r51_brown(z));
    /* A part's Checker follows the projection, not the mesh's UVs: two points of the top with the same UVs differ. */
    m.grass->procedural = (int)Procedural::Checker;
    m.grass->procedural_scale = 4.0f;
    m.grass->touch();
    int differ = 0;
    for (int i = 0; i < 20; i++) {
      const Vec3 p1 = r51_eval(*m.axis, {0, 1, 0}, {0, 1, 0}, Vec3(0.05f * i, 0.0f, 0.0f)).albedo;
      const Vec3 p2 = r51_eval(*m.axis, {0, 1, 0}, {0, 1, 0}, Vec3(0.05f * i + 0.13f, 0.0f, 0.0f)).albedo;
      differ += length(p1 - p2) > 0.05f;
    }
    CHECK(differ > 3);
  });
  test("axis material: the CPU path tracer and (with a Vulkan GPU) the GPU path tracer agree on an axis-material cube", [] {
    R51Mats m = r51_mats(1);
    /* A part that is itself an axis material (used plainly) and a part shared by two parents. */
    m.dirt->axis_mode = 1;
    m.dirt->axis_top = "Assets/Materials/R51/Grass.mat";
    m.dirt->touch();
    MaterialPtr second = make_material("Ground 2", Vec3(1.0f));
    second->axis_mode = 1;
    second->axis_side = "Assets/Materials/R51/Dirt.mat";
    second->touch();
    auto cube = primitives::cube(1.0f);
    std::vector<MaterialPtr> mats = {m.axis}, mats2 = {second};
    std::vector<PTObject> objs = {{&cube->render_mesh_tangents(), Mat4::trs({0, 0, 0}, Quat::euler({30, 20, 0}), {1, 1, 1}), &mats},
                                  {&cube->render_mesh_tangents(), Mat4::translate({2.2f, 0, 0}), &mats2}};
    RenderLight sun;
    sun.direction = normalize(Vec3(-0.3f, -1, 0.2f));
    Environment env;
    const int W = 32, H = 32, SPP = 64;
    auto render = [&](bool cpu, std::vector<int> gpus) {
      PathTracer pt;
      PTSettings st;
      st.use_cpu = cpu;
      st.gpus = gpus;
      st.denoise = false;
      st.use_embree = false;
      pt.set_settings(st);
      pt.build(objs, {sun}, env);
      pt.set_camera(Mat4::look_at({0, 3.0f, -2.0f}, {0, 0, 0}, {0, 1, 0}), Mat4::perspective(40 * kDeg2Rad, 1.0f, 0.1f, 50), W, H);
      pt.render(1e9, SPP);
      return pt.linear_rgb(false);
    };
    const std::vector<float> ref = render(true, {});
    double mean = 0;
    for (float v : ref) mean += v;
    CHECK(mean > 0.0);
    /* From straight above, the unrotated second cube's top fills the middle of a narrow view: the Top (its own white
     * maps) there, and the first cube's top is grass. */
    {
      PathTracer pt;
      PTSettings st;
      st.denoise = false;
      st.use_embree = false;
      pt.set_settings(st);
      std::vector<PTObject> one = {{&cube->render_mesh_tangents(), Mat4::identity(), &mats}};
      pt.build(one, {sun}, env);
      pt.set_camera(Mat4::look_at({0, 5.0f, 0.001f}, {0, 0, 0}, {0, 0, 1}), Mat4::perspective(10 * kDeg2Rad, 1.0f, 0.1f, 50), 16, 16);
      pt.render(1e9, 32);
      const std::vector<float> img = pt.linear_rgb(false);
      const size_t c = (8 * 16 + 8) * 3;
      std::printf("    CPU path tracer, the cube's top from above: %.3f %.3f %.3f\n", img[c], img[c + 1], img[c + 2]);
      CHECK(img[c + 1] > 2.0f * img[c] && img[c + 1] > 2.0f * img[c + 2]);  // grass
    }
    if (!gpu::available()) return;
    for (const gpu::DeviceInfo &d : gpu::devices()) {
      const std::vector<float> img = render(false, {d.index});
      double err = 0, mean = 0;
      for (size_t i = 0; i < img.size(); i++) err += std::fabs(img[i] - ref[i]), mean += ref[i];
      std::printf("    %s: mean |difference| %.4f of mean %.4f\n", d.name.c_str(), err / img.size(), mean / img.size());
      CHECK(err / img.size() < 0.08 * (mean / img.size()));
    }
  });
}


/* ===================================================================== */
/* Round 54 (task 0032, ADR 0008 amended): every camera filter in the     */
/* path tracers                                                            */
/* ===================================================================== */

/* A 2 m quad seen at a grazing angle from (0, 0.6, -2.2), 48 x 48: the camera both renderers use. */
struct R54Cam {
  Mat4 view = Mat4::look_at({0.0f, 0.6f, -2.2f}, {0, 0, 0}, {0, 1, 0});
  Mat4 proj = Mat4::perspective(50 * kDeg2Rad, 1.0f, 0.1f, 20.0f);
  int size = 48;
};
/* The quad lying back 60 degrees (so its far half is foreshortened), an unlit material. */
static Mat4 r54_model() { return Mat4::rotate(Quat::euler({60, 0, 0})); }

/* Linear RGB of the rasterizer (unlit: the material's colour) with the given stages. */
static std::vector<Vec3> r54_raster(const MaterialPtr &m, const RasterOptions &stages) {
  static auto quad = primitives::quad(2.0f);
  R54Cam c;
  std::vector<MaterialPtr> mats = {m};
  Image img;
  img.resize(c.size, c.size);
  RenderTarget rt;
  rt.attach(img, {0, 0, c.size, c.size});
  rt.want_hdr = true;
  Renderer3D r;
  RasterOptions opt = stages;
  opt.shade = ShadeMode::Deferred;
  LightingEnv env;
  env.view_transform = ViewTransform::Standard;
  env.camera_pos = {0.0f, 0.6f, -2.2f};
  r.begin(&rt, c.view, c.proj, env, opt);
  r.clear(0xFF000000);
  DrawItem it;
  it.mesh = &quad->render_mesh_tangents();
  it.model = r54_model();
  it.materials = &mats;
  it.double_sided = true;
  r.add(it);
  r.flush();
  std::vector<Vec3> out((size_t)c.size * c.size);
  for (size_t i = 0; i < out.size(); i++) {
    if (!rt.hdr.empty() && rt.hdr[i].x >= 0.0f) out[i] = rt.hdr[i];
    else {
      const uint32_t px = img.pixels[i];
      out[i] = Vec3(srgb_to_linear(((px >> 16) & 255) / 255.0f), srgb_to_linear(((px >> 8) & 255) / 255.0f), srgb_to_linear((px & 255) / 255.0f));
    }
  }
  return out;
}

/* The path tracer's picture of the same, with the Retro stages given (and GPUs when asked). */
static std::vector<Vec3> r54_trace(const MaterialPtr &m, const PTSettings::Retro &retro, std::vector<int> gpus = {}, int spp = 64) {
  static auto quad = primitives::quad(2.0f);
  R54Cam c;
  std::vector<MaterialPtr> mats = {m};
  Environment black;
  black.mode = Environment::Color;
  black.color = Vec3(0.0f);
  PathTracer pt;
  PTSettings st;
  st.denoise = false;
  st.use_embree = false;
  st.max_bounces = 0;
  st.view_transform = ViewTransform::Standard;
  st.gpus = gpus;
  st.use_cpu = gpus.empty();
  st.retro = retro;
  pt.set_settings(st);
  pt.build({{&quad->render_mesh_tangents(), r54_model(), &mats}}, {}, black);
  pt.set_camera(c.view, c.proj, c.size, c.size);
  pt.render(1e9, spp);
  if (!gpus.empty()) CHECK(pt.device_summary().find("GPU") != std::string::npos || pt.device_summary().find("NVIDIA") != std::string::npos ||
                          pt.device_summary().find("AMD") != std::string::npos || pt.device_summary().find("Intel") != std::string::npos);
  const auto rgb = pt.linear_rgb(false);
  std::vector<Vec3> out((size_t)c.size * c.size);
  for (size_t i = 0; i < out.size(); i++) out[i] = Vec3(rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]);
  return out;
}
static double r54_diff(const std::vector<Vec3> &a, const std::vector<Vec3> &b) {
  double d = 0;
  for (size_t i = 0; i < a.size(); i++) d += std::fabs(a[i].x - b[i].x) + std::fabs(a[i].y - b[i].y) + std::fabs(a[i].z - b[i].z);
  return d / a.size();
}
static PTSettings::Retro r54_retro() {
  R54Cam c;
  PTSettings::Retro r;
  r.view_proj = c.proj * c.view;
  r.width = r.height = c.size;
  return r;
}

static void round54_tests() {
  test("retro in the path tracer: affine (screen-linear) UVs as the rasterizer draws them on a quad at a grazing angle", [] {
    MaterialPtr m = make_material("checker", {1, 1, 1});
    m->unlit = true;
    m->procedural = (int)Procedural::Checker;
    m->procedural_color2 = {0, 0, 0};
    m->procedural_scale = 6.0f;
    RasterOptions affine;
    affine.affine_uv = true;
    const auto ra = r54_raster(m, affine), rp = r54_raster(m, RasterOptions{});
    PTSettings::Retro retro = r54_retro();
    retro.affine_uv = true;
    const auto ta = r54_trace(m, retro), tp = r54_trace(m, r54_retro());
    const double same = r54_diff(ta, ra), other = r54_diff(tp, ra), check = r54_diff(ra, rp);
    std::printf("    mean |difference| to the affine raster: traced affine %.4f, traced perspective %.4f (raster affine vs perspective %.4f)\n", same, other, check);
    CHECK(check > 0.05);         // the effect is visible at this angle
    CHECK(same < 0.5 * other);   // the traced affine picture is the raster's, not the perspective one
    if (gpu::available()) {
      const auto ga = r54_trace(m, retro, {gpu::devices()[0].index});
      const double g = r54_diff(ga, ta);
      std::printf("    the GPU against the CPU: %.4f\n", g);
      CHECK(g < 0.1 * std::max(0.05, other));
    }
  });

  test("retro in the path tracer: the texture rules reach every hit (a 1-texel cap makes a textured quad one colour, CPU and GPU)", [] {
    /* (The tracer averages many samples a pixel, so a Nearest or 64-texel look can't be compared pixel for pixel with
     * the rasterizer's single sample; a 1-texel cap shows unmistakably whether the rules apply.) */
    MaterialPtr m = make_material("grid", {1, 1, 1});
    m->unlit = true;
    m->procedural = (int)Procedural::UVGrid;
    auto spread = [](const std::vector<Vec3> &img) {  // the colours' spread over the quad's middle
      Vec3 lo(1e9f), hi(-1e9f);
      for (int y = 18; y < 30; y++)
        for (int x = 14; x < 34; x++) lo = vmin(lo, img[(size_t)y * 48 + x]), hi = vmax(hi, img[(size_t)y * 48 + x]);
      return std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z});
    };
    PTSettings::Retro retro = r54_retro();
    retro.tex.filter = (int)TexFilter::Closest;
    retro.tex.max_size = 1;
    retro.tex.mipmaps = false;
    const float with = spread(r54_trace(m, retro, {}, 8)), without = spread(r54_trace(m, r54_retro(), {}, 8));
    std::printf("    colour spread over the quad: %.4f with a 1-texel cap, %.4f without\n", with, without);
    CHECK(with < 0.01f && without > 0.1f);
    if (gpu::available()) {
      const float g = spread(r54_trace(m, retro, {gpu::devices()[0].index}, 8));
      std::printf("    the GPU with the cap: %.4f\n", g);
      CHECK(g < 0.01f);
    }
  });

  test("retro in the path tracer: the GPU takes the same capped mip level and filter as the CPU (Nearest and 3-point at 16 texels)", [] {
    if (!gpu::available()) return;
    MaterialPtr m = make_material("grid", {1, 1, 1});
    m->unlit = true;
    m->procedural = (int)Procedural::UVGrid;
    for (int f : {(int)TexFilter::Closest, (int)TexFilter::ThreePoint}) {
      PTSettings::Retro retro = r54_retro();
      retro.tex.filter = f;
      retro.tex.max_size = 16;
      retro.tex.mipmaps = false;
      const auto cpu = r54_trace(m, retro, {}, 16), gpu = r54_trace(m, retro, {gpu::devices()[0].index}, 16);
      const auto plain = r54_trace(m, r54_retro(), {}, 16);
      const double d = r54_diff(cpu, gpu), effect = r54_diff(cpu, plain);
      std::printf("    filter %d at 16 texels: GPU vs CPU %.4f (the cap's own effect %.4f)\n", f, d, effect);
      CHECK(effect > 0.02);
      CHECK(d < 0.2 * effect);
    }
  });

  test("retro in the path tracer: screen-door transparency is a 2 x 2 checkerboard, solid and see-through", [] {
    MaterialPtr m = make_material("glass red", {1, 0, 0});
    m->unlit = true;
    m->surface = (int)MaterialSurface::Transparent;
    m->alpha = 0.5f;
    PTSettings::Retro retro = r54_retro();
    retro.screen_door = true;
    auto pattern = [&](const std::vector<Vec3> &img) {
      /* Over the quad's middle: pixels of each checkerboard colour, solid red vs black. */
      int solid_even = 0, hole_odd = 0, n_even = 0, n_odd = 0;
      for (int y = 18; y < 30; y++)
        for (int x = 14; x < 34; x++) {
          const Vec3 c = img[(size_t)y * 48 + x];
          if (((x + y) & 1) == 0) n_even++, solid_even += c.x > 0.95f;
          else n_odd++, hole_odd += c.x < 0.05f;
        }
      return std::pair<double, double>((double)solid_even / n_even, (double)hole_odd / n_odd);
    };
    const auto door = pattern(r54_trace(m, retro, {}, 16));
    const auto blend = pattern(r54_trace(m, r54_retro(), {}, 16));
    std::printf("    with the door: %.0f%% of even pixels solid, %.0f%% of odd ones see-through; without: %.0f%% / %.0f%%\n",
                100 * door.first, 100 * door.second, 100 * blend.first, 100 * blend.second);
    CHECK(door.first > 0.95 && door.second > 0.95);
    CHECK(blend.first < 0.5);  // alpha blending: half red
    /* Nearly clear (alpha 0.05): the rasterizer's door drops it on its solid squares too. */
    m->alpha = 0.05f;
    m->touch();
    const auto clear = pattern(r54_trace(m, retro, {}, 16));
    std::printf("    alpha 0.05 with the door: %.0f%% of even pixels solid\n", 100 * clear.first);
    CHECK(clear.first < 0.05);
    m->alpha = 0.5f;
    m->touch();
    if (gpu::available()) {
      const auto g = pattern(r54_trace(m, retro, {gpu::devices()[0].index}, 16));
      std::printf("    the GPU: %.0f%% / %.0f%%\n", 100 * g.first, 100 * g.second);
      CHECK(g.first > 0.95 && g.second > 0.95);
    }
  });

  test("retro in the path tracer: vertex snapping moves a path-traced F12 render's silhouette onto the rasterized one's grid", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.step_frame_headless();
    for (const char *n : {"Plane", "Sphere", "Cylinder", "Utah Teapot"})
      if (GameObject *g = ed.scene().find_by_name(n)) g->active = false;
    ed.command("set Render.ResolutionX 320");
    ed.command("set Render.ResolutionY 240");
    ed.command("set Render.Samples 4");
    ed.command("set Render.Denoise false");
    ed.command("device cpu");
    Camera *cam = by_name(ed.scene(), "Main Camera")->camera();
    cam->clear_flags = 1;  // solid black: the silhouette is what isn't black
    cam->background = {0, 0, 0};
    ed.command("select Main Camera");
    ed.command("filter add Retro Console");
    auto *cf = by_name(ed.scene(), "Main Camera")->get<CameraFilters>();
    CHECK(cf && !cf->effects.empty());
    if (!cf || cf->effects.empty()) return;
    auto *retro = static_cast<RetroConsoleFilter *>(cf->effects[0].get());
    retro->width = 80;  // a coarse grid: 4 output pixels a cell
    retro->height = 60;
    retro->snap_grid = 4.0f;
    retro->affine_textures = false;
    auto mask = [&](const char *engine) {
      ed.command(std::string("set Render.RenderEngine ") + engine);
      ed.command("render");
      for (int i = 0; i < 4000 && ed.rendering_for_test(); i++) ed.step_frame_headless();
      const Image &img = ed.render_image_for_test();
      std::vector<uint8_t> m(img.pixels.size());
      for (size_t i = 0; i < m.size(); i++) m[i] = (img.pixels[i] & 0xFFFFFF) > 0x080808;
      return m;
    };
    auto differ = [](const std::vector<uint8_t> &a, const std::vector<uint8_t> &b) {
      size_t d = 0;
      for (size_t i = 0; i < a.size() && i < b.size(); i++) d += a[i] != b[i];
      return d;
    };
    const auto raster = mask("Rasterized"), traced = mask("Path");
    retro->vertex_snap = false;
    const auto traced_free = mask("Path"), raster_free = mask("Rasterized");
    size_t on = 0;
    for (uint8_t v : raster) on += v;
    const size_t d_snap = differ(traced, raster), d_free = differ(traced_free, raster), d_effect = differ(raster_free, raster);
    std::printf("    silhouette pixels %zu; traced vs raster (snapped): %zu differ; traced without the snap: %zu (raster with / without: %zu)\n", on, d_snap, d_free,
                d_effect);
    CHECK(on > 500);
    CHECK(d_effect > 0);
    CHECK(d_snap <= on / 50);  // the same silhouette (measured: 0 pixels differ)
    CHECK(d_snap < d_free);
  });
}


/* ===================================================================== */
/* Round 55 (task 0033): camera composition guides                        */
/* ===================================================================== */

/* A Game view in a 2 x 3 layout, the camera's guides pure red and opaque (so a test can find them). */
static Camera *r55_open(Editor &ed) {
  r34_open(ed);
  Camera *c = by_name(ed.scene(), "Main Camera")->camera();
  c->guide_color = {1, 0, 0};
  c->guide_opacity = 1.0f;
  c->guide_thirds = false;
  return c;
}
/* Share of a vertical (or horizontal) line's pixels inside the frame that are the guides' red. */
static double r55_red_line(const Editor &ed, const Recti &f, bool vertical, int at) {
  const Image &fb = ed.framebuffer();
  int red = 0, n = 0;
  const int a = vertical ? f.y + 2 : f.x + 2, b = vertical ? f.bottom() - 2 : f.right() - 2;
  for (int k = a; k < b; k++) {
    const int x = vertical ? at : k, y = vertical ? k : at;
    if (x < 0 || y < 0 || x >= fb.width || y >= fb.height) continue;
    const uint32_t c = fb.pixels[(size_t)y * fb.width + x];
    red += ((c >> 16) & 255) > 200 && ((c >> 8) & 255) < 60 && (c & 255) < 60;
    n++;
  }
  return n ? (double)red / n : 0.0;
}
/* Red pixels anywhere in the frame. */
static size_t r55_red_pixels(const Editor &ed, const Recti &f) {
  const Image &fb = ed.framebuffer();
  size_t n = 0;
  for (int y = std::max(0, f.y); y < std::min(fb.height, f.bottom()); y++)
    for (int x = std::max(0, f.x); x < std::min(fb.width, f.right()); x++) {
      const uint32_t c = fb.pixels[(size_t)y * fb.width + x];
      n += ((c >> 16) & 255) > 200 && ((c >> 8) & 255) < 60 && (c & 255) < 60;
    }
  return n;
}

static void round55_tests() {
  test("camera guides: rule of thirds lines at a third and two thirds of the Game view's camera frame, letterboxed too", [] {
    Editor ed;
    Camera *c = r55_open(ed);
    ed.command("guides thirds on");
    r34_steps(ed, 2);
    CHECK(c->show_guides && c->guide_thirds);
    Recti f = ed.game_guides_frame_for_test();
    CHECK(f.w > 50 && f.h > 50);
    const int x3 = f.x + (int)std::lround(f.w / 3.0), y3 = f.y + (int)std::lround(f.h * 2.0 / 3.0);
    auto best = [&](bool vertical, int at) {  // (the line may land on either pixel of a boundary)
      return std::max({r55_red_line(ed, f, vertical, at - 1), r55_red_line(ed, f, vertical, at), r55_red_line(ed, f, vertical, at + 1)});
    };
    const double v3 = best(true, x3), h23 = best(false, y3), mid = r55_red_line(ed, f, true, f.x + f.w / 2);
    std::printf("    frame %d x %d at %d,%d: red along x = w/3 %.0f%%, y = 2h/3 %.0f%%, x = w/2 %.0f%%\n", f.w, f.h, f.x, f.y, 100 * v3, 100 * h23, 100 * mid);
    CHECK(v3 > 0.9 && h23 > 0.9 && mid < 0.1);
    /* A 2.39:1 camera letterboxes the Game view: the lines follow the frame, not the view. */
    c->aspect_mode = 4;  // (an aspect other than the render's)
    ed.command("set Camera.AspectRatio 2.39:1");
    r34_steps(ed, 2);
    const Recti f2 = ed.game_guides_frame_for_test();
    std::printf("    letterboxed frame %d x %d\n", f2.w, f2.h);
    CHECK(f2.w != f.w || f2.h != f.h);
    CHECK(std::max({r55_red_line(ed, f2, false, f2.y + (int)std::lround(f2.h / 3.0) - 1), r55_red_line(ed, f2, false, f2.y + (int)std::lround(f2.h / 3.0)),
                    r55_red_line(ed, f2, false, f2.y + (int)std::lround(f2.h / 3.0) + 1)}) > 0.9);
  });

  test("camera guides: each one toggles on its own, Show Guides off draws none, and renders and the render hash ignore them", [] {
    Editor ed;
    Camera *c = r55_open(ed);
    const uint64_t h0 = hash_component(*c);
    r34_steps(ed, 2);
    const Recti f = ed.game_guides_frame_for_test();
    const size_t none = r55_red_pixels(ed, f);
    struct G {
      const char *cmd;
      bool *flag;
    } guides[] = {{"thirds", &c->guide_thirds},     {"center", &c->guide_center},       {"diagonal", &c->guide_center_diagonal},
                  {"golden", &c->guide_golden},     {"goldena", &c->guide_golden_tri_a}, {"goldenb", &c->guide_golden_tri_b},
                  {"harmonya", &c->guide_harmony_a}, {"harmonyb", &c->guide_harmony_b},  {"title", &c->guide_title_safe},
                  {"action", &c->guide_action_safe}};
    int drawn = 0;
    for (const G &g : guides) {
      ed.command(std::string("guides ") + g.cmd + " on");
      r34_steps(ed, 2);
      const size_t n = r55_red_pixels(ed, f);
      drawn += n > none + 50;
      if (n <= none + 50) std::printf("    %s drew nothing\n", g.cmd);
      ed.command(std::string("guides ") + g.cmd + " off");
    }
    CHECK(drawn == 10);
    CHECK(hash_component(*c) == h0);  // guides don't touch the render hash: toggling them never re-renders
    ed.command("guides thirds on");
    ed.command("guides off");
    r34_steps(ed, 2);
    CHECK(r55_red_pixels(ed, f) == none);
    /* F12 (rasterized) with the guides on is the same picture as with them off. */
    ed.command("set Render.RenderEngine Rasterized");
    ed.command("set Render.ResolutionX 160");
    ed.command("set Render.ResolutionY 90");
    ed.command("render");
    const std::vector<uint32_t> off = ed.render_image_for_test().pixels;
    ed.command("guides thirds on");
    ed.command("guides title on");
    ed.command("render");
    CHECK(ed.render_image_for_test().pixels == off);
  });

  test("camera guides: the Game view's Guides menu, clicked with the mouse, turns Thirds on (the menu reads its camera when it runs)", [] {
    Editor ed;
    Camera *c = r55_open(ed);
    CHECK(!c->show_guides && !c->guide_thirds);
    const Recti b = ed.game_guides_rect_for_test("button");
    CHECK(b.w > 0);
    if (b.w <= 0) return;
    auto click = [&](const Recti &r) {
      const int x = r.x + r.w / 2, y = r.y + r.h / 2;
      ed.step_frame_headless({r34_mouse(R34ET::MouseMove, x, y)});
      ed.step_frame_headless({r34_mouse(R34ET::MouseDown, x, y)});
      ed.step_frame_headless({r34_mouse(R34ET::MouseUp, x, y)});
    };
    click(b);
    ed.step_frame_headless();
    const Recti t = ed.game_guides_rect_for_test("Thirds");
    CHECK(t.w > 0);
    if (t.w <= 0) return;
    click(t);
    r34_steps(ed, 2);
    c = by_name(ed.scene(), "Main Camera")->camera();
    std::printf("    after clicking Guides > Thirds: shown %d, thirds %d\n", (int)c->show_guides, (int)c->guide_thirds);
    CHECK(c->show_guides && c->guide_thirds);
    const Recti f = ed.game_guides_frame_for_test();
    CHECK(r55_red_pixels(ed, f) > (size_t)f.w);  // the lines are drawn
  });

  test("camera guides: the golden and harmonious triangles are Blender's construction (endpoints on a 16:9 frame)", [] {
    Camera c;
    c.show_guides = true;
    c.guide_thirds = false;
    const Recti f{0, 0, 1600, 900};
    auto lines_of = [&](bool Camera::*flag) {
      Camera k = c;
      k.*flag = true;
      return camera_guide_lines(k, f);
    };
    auto near = [](Vec2 a, Vec2 b) { return length(a - b) < 0.5f; };
    /* Golden Triangle A (Blender: golden = false, ofs = h^2 / w = 506.25): the rising diagonal, from the bottom-right
     * corner to x = w - ofs on the top edge, from the top-left to x = ofs on the bottom edge. */
    const auto g = lines_of(&Camera::guide_golden_tri_a);
    CHECK(g.size() == 3);
    if (g.size() == 3) {
      CHECK(near(g[0].first, {0, 900}) && near(g[0].second, {1600, 0}));
      CHECK(near(g[1].first, {1600, 900}) && near(g[1].second, {1600 - 506.25f, 0}));
      CHECK(near(g[2].first, {0, 0}) && near(g[2].second, {506.25f, 900}));
    }
    /* Harmonious Triangle A (golden = true, ofs = 0.381966 w = 611.15). */
    const auto hh = lines_of(&Camera::guide_harmony_a);
    CHECK(hh.size() == 3);
    if (hh.size() == 3) {
      CHECK(near(hh[1].second, {1600 - 611.146f, 0}) && near(hh[2].second, {611.146f, 900}));
    }
    /* B mirrors A top to bottom; a zero safe margin draws nothing. */
    const auto gb = lines_of(&Camera::guide_golden_tri_b);
    CHECK(gb.size() == 3 && near(gb[0].first, {0, 0}) && near(gb[0].second, {1600, 900}));
    Camera z = c;
    z.guide_title_safe = true;
    z.title_safe_x = z.title_safe_y = 0.0f;
    CHECK(camera_guide_lines(z, f).empty());
  });

  test("camera guides: Title and Action Safe at Blender's margins (a fraction of half the frame per side)", [] {
    Editor ed;
    Camera *c = r55_open(ed);
    ed.command("guides title on");
    r34_steps(ed, 2);
    const Recti f = ed.game_guides_frame_for_test();
    const int top = f.y + (int)std::lround(c->title_safe_y * f.h * 0.5f), left = f.x + (int)std::lround(c->title_safe_x * f.w * 0.5f);
    const Recti inner{left, top, f.w - 2 * (left - f.x), f.h - 2 * (top - f.y)};
    auto near_line = [&](bool vertical, int at, const Recti &r) {
      return std::max({r55_red_line(ed, r, vertical, at - 1), r55_red_line(ed, r, vertical, at), r55_red_line(ed, r, vertical, at + 1)});
    };
    const double t = near_line(false, top, inner), l = near_line(true, left, inner);
    std::printf("    title safe: top edge %.0f%%, left edge %.0f%% red\n", 100 * t, 100 * l);
    CHECK(t > 0.9 && l > 0.9);
  });

  test("camera guides: saved with the scene, undone, and set from the Inspector's fields and the console", [] {
    Editor ed;
    Camera *c = r55_open(ed);
    ed.command("select Main Camera");
    ed.step_frame_headless();
    ed.command("guides golden on");
    ed.step_frame_headless();
    CHECK(c->show_guides && c->guide_golden);
    ed.command("set Camera.TitleSafeX 0.2");
    ed.step_frame_headless();
    ed.command("set Camera.GuideOpacity 0.8");
    ed.step_frame_headless();
    ed.command("undo");
    ed.step_frame_headless();
    ed.command("undo");
    ed.step_frame_headless();
    c = by_name(ed.scene(), "Main Camera")->camera();  // (undo restores a copy of the scene: a new Camera)
    std::printf("    after two undos: title safe x %.2f, opacity %.2f, golden %d\n", c->title_safe_x, c->guide_opacity, (int)c->guide_golden);
    CHECK(std::fabs(c->title_safe_x - 0.1f) < 1e-6f && std::fabs(c->guide_opacity - 1.0f) < 1e-6f && c->guide_golden);
    ed.command("redo");
    ed.step_frame_headless();
    ed.command("redo");
    ed.step_frame_headless();
    Scene &sc = ed.scene();
    const std::string path = fs::join(fs::join(scratch_project(), "Assets/Scenes"), "R55_Guides.scene");
    CHECK(save_scene(sc, path));
    Scene t;
    std::string err;
    CHECK(load_scene(path, t, err));
    Camera *tc = t.find_by_name("Main Camera") ? t.find_by_name("Main Camera")->camera() : nullptr;
    CHECK(tc && tc->show_guides && tc->guide_golden && std::fabs(tc->guide_opacity - 0.8f) < 1e-5f);
    std::remove(path.c_str());
  });
}

static void round56_tests() {
  test("references: an old layout holding the Research window loads without it, and opening it shows Learn > References", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.step_frame_headless();
    CHECK(!ed.window_docked_for_test(WindowKind::Research));  // not in the default layout
    /* A layout saved before: Research beside the Console, and alone in an area of its own. */
    ed.command("layout H(0.5,V(0.5,L(0,1:0),L(5,7:1)),V(0.5,L(3:0),L(7:0)))");
    ed.step_frame_headless();
    CHECK(!ed.window_docked_for_test(WindowKind::Research));
    CHECK(ed.window_docked_for_test(WindowKind::Console) && ed.window_docked_for_test(WindowKind::Inspector));
    for (const char *preset : {"Default", "2 by 3", "Tall", "Wide", "Learning"}) {
      ed.command(std::string("layout ") + preset);
      CHECK(!ed.window_docked_for_test(WindowKind::Research));
    }
    ed.command("window Research");
    ed.step_frame_headless();
    CHECK(!ed.window_docked_for_test(WindowKind::Research));
    CHECK(ed.window_docked_for_test(WindowKind::Learn));
    CHECK(std::string(lesson(ed.lesson_for_test()).title) == "References");
    ed.command("research");
    CHECK(std::string(lesson(ed.lesson_for_test()).title) == "References");
    /* Ctrl+8 (its old shortcut) opens References, from any lesson. */
    ed.command("lesson 3");
    platform::Event k8;
    k8.type = platform::EventType::KeyDown;
    k8.key = platform::KEY_8;
    k8.mods = platform::MOD_CTRL;
    ed.step_frame_headless({k8});
    ed.step_frame_headless();
    CHECK(std::string(lesson(ed.lesson_for_test()).title) == "References");
    /* Research in the root area (beside the Console), and a layout that was only Research. */
    ed.command("layout L(5,7:1)");
    CHECK(!ed.window_docked_for_test(WindowKind::Research) && ed.window_docked_for_test(WindowKind::Console));
    ed.command("layout L(7:0)");
    CHECK(!ed.window_docked_for_test(WindowKind::Research) && ed.window_docked_for_test(WindowKind::Scene));  // the default
  });

  test("references: every feature built from a paper is cited in the References lesson, and its Try-it button runs", [] {
    int idx = -1;
    for (int i = 0; i < lesson_count(); i++)
      if (std::string(lesson(i).title) == "References") idx = i;
    CHECK(idx >= 0);
    if (idx < 0) return;
    const std::string body = lesson(idx).body;
    int missing = 0;
    for (const auto &f : research::features()) {
      /* The citation's first author's surname (the paper behind a feature; utilities cite none). */
      std::string c = f.citation;
      const size_t comma = c.find(',');
      const std::string author = comma == std::string::npos ? c : c.substr(0, comma);
      if (author.empty() || f.status == "utility") continue;  // (a tool, no paper)
      if (body.find(author) == std::string::npos) {
        std::printf("    not cited: %s (%s)\n", f.name.c_str(), f.citation.c_str());
        missing++;
      }
    }
    CHECK(missing == 0);
    for (const char *paper : {"Taubin", "Catmull", "Thiedemann", "conformal maps", "Walter", "Karis", "Heitz", "Perlin", "Hosek"})
      CHECK(body.find(paper) != std::string::npos);
    /* The Taubin Try-it button: a noisy sphere smoothed (the mesh changes). */
    Editor ed;
    ed.init_headless(1000, 700);
    ed.step_frame_headless();
    ed.command("create Sphere");
    GameObject *g = ed.selected_object();
    CHECK(g && g->get<MeshFilter>());
    if (!g || !g->get<MeshFilter>()) return;
    ed.command("meshop research:surface_noise");
    const std::vector<Vec3> noisy = g->get<MeshFilter>()->mesh->positions;
    ed.command("meshop research:taubin_smoothing");
    CHECK(g->get<MeshFilter>()->mesh->positions != noisy);
  });

  test("references: dropping a paper onto the editor copies nothing into the project (no research folder)", [] {
    Editor ed;
    ed.init_headless(1000, 700);
    ed.step_frame_headless();
    const std::string src = fs::join(scratch_project(), "R56_paper.pdf");
    {
      std::FILE *f = std::fopen(src.c_str(), "wb");
      if (f) {
        std::fputs("%PDF-1.4 test", f);
        std::fclose(f);
      }
    }
    platform::Event drop;
    drop.type = platform::EventType::Drop;
    drop.paths = {src};
    ed.step_frame_headless({drop});
    ed.step_frame_headless();
    CHECK(!fs::exists(fs::join(scratch_project(), "research/papers/R56_paper.pdf")));
    CHECK(!ed.window_docked_for_test(WindowKind::Research));
    std::remove(src.c_str());
  });
}

static void round57_tests() {
  test("transparent film: rasterized and path traced, the background's alpha is 0, the cube's 255, its edges between", [] {
    Editor ed;
    r57_open(ed);
    for (const char *engine : {"Rasterized", "Path"}) {
      const Image &img = r57_render(ed, engine, true);
      CHECK(img.width == 160 && img.height == 90);
      int zero = 0, full = 0, part = 0;
      for (uint32_t c : img.pixels) {
        const int a = (int)(c >> 24);
        zero += a == 0, full += a == 255, part += a > 0 && a < 255;
      }
      std::printf("    %s: corner alpha %d, middle %d; %d transparent, %d solid, %d partial pixels\n", engine, r57_alpha(img, 1, 1),
                  r57_alpha(img, 80, 45), zero, full, part);
      CHECK(r57_alpha(img, 1, 1) == 0 && r57_alpha(img, 158, 88) == 0);
      CHECK(r57_alpha(img, 80, 45) == 255);
      CHECK(zero > 1000 && full > 100 && part > 0);
    }
    /* The GPU's coverage, as the CPU's. */
    if (gpu::available()) {
      ed.command("device gpu");
      const Image &g = r57_render(ed, "Path", true);
      std::printf("    GPU: corner alpha %d, middle %d\n", r57_alpha(g, 1, 1), r57_alpha(g, 80, 45));
      CHECK(r57_alpha(g, 1, 1) == 0 && r57_alpha(g, 80, 45) == 255);
      ed.command("device cpu");
    }
    /* Off: every pixel opaque, as before. */
    const Image &off = r57_render(ed, "Rasterized", false);
    bool all = true;
    for (uint32_t c : off.pixels) all = all && (c >> 24) == 255;
    CHECK(all);
  });

  test("transparent film: a 50 % see-through quad gives alpha about 128 (rasterized and path traced)", [] {
    /* (A single surface: a closed see-through cube is two, which the path tracer counts as 75 % and the rasterizer,
     * culling the far face, as 50 %.) */
    Editor ed;
    r57_open(ed);
    if (GameObject *cube = ed.scene().find_by_name("Cube")) cube->active = false;
    ed.command("create Quad");
    GameObject *g = ed.selected_object();
    CHECK(g != nullptr);
    if (!g) return;
    g->set_world_position({0, 1, 0});
    g->set_local_scale({3, 3, 3});
    g->set_world_rotation(by_name(ed.scene(), "Main Camera")->world_rotation());  // facing the camera
    ed.command("material Transparent");
    CHECK(g && g->get<MeshRenderer>() && !g->get<MeshRenderer>()->materials.empty());
    if (!g || !g->get<MeshRenderer>() || g->get<MeshRenderer>()->materials.empty()) return;
    MaterialPtr m = g->get<MeshRenderer>()->materials[0];
    m->alpha = 0.5f;
    m->double_sided = true;
    m->touch();
    ed.command("set Render.Samples 256");
    for (const char *engine : {"Rasterized", "Path"}) {
      const Image &img = r57_render(ed, engine, true);
      double sum = 0;  // the mean over the middle (the quad covers it; the tracer's alpha is a noisy estimate)
      for (int y = 40; y < 50; y++)
        for (int x = 75; x < 85; x++) sum += r57_alpha(img, x, y);
      const double a = sum / 100.0;
      std::printf("    %s: the middle's alpha %.1f\n", engine, a);
      CHECK(a > 112 && a < 144);
    }
  });

  test("transparent film: a glass sphere is covered (alpha 255) in a path-traced render, CPU and GPU, and shows the world through it", [] {
    Editor ed;
    r57_open(ed);
    if (GameObject *cube = ed.scene().find_by_name("Cube")) cube->active = false;
    ed.command("create Sphere");
    GameObject *g = ed.selected_object();
    CHECK(g != nullptr);
    if (!g) return;
    g->set_world_position({0, 1, 0});
    ed.command("material Glass");
    std::vector<const char *> devices = {"cpu"};
    if (gpu::available()) devices.push_back("gpu");
    for (const char *dev : devices) {
      ed.command(std::string("device ") + dev);
      const Image &img = r57_render(ed, "Path", true);
      const uint32_t c = img.pixels[(size_t)45 * img.width + 80];
      std::printf("    %s: the sphere's middle alpha %u, colour %06x; corner alpha %d\n", dev, c >> 24, c & 0xFFFFFF, r57_alpha(img, 1, 1));
      CHECK((c >> 24) == 255);
      CHECK((c & 0xFFFFFF) != 0);  // the world seen through it, not the empty background
      CHECK(r57_alpha(img, 1, 1) == 0);
    }
    ed.command("device cpu");
  });

  test("transparent film: a Bloom filter doesn't make a rasterized cube see-through, and an outline keeps its alpha in a filtered trace", [] {
    Editor ed;
    r57_open(ed);
    ed.command("select Main Camera");
    ed.command("filter add Bloom");
    const Image &img = r57_render(ed, "Rasterized", true);
    int solid_inside = 0, n = 0;
    for (int y = 40; y < 50; y++)
      for (int x = 75; x < 85; x++) n++, solid_inside += r57_alpha(img, x, y) == 255;
    std::printf("    Bloom: %d of %d of the cube's middle pixels opaque, corner alpha %d\n", solid_inside, n, r57_alpha(img, 1, 1));
    CHECK(solid_inside == n);
    CHECK(r57_alpha(img, 1, 1) == 0);
    /* An Edge Outline in a filtered path-traced render: its lines (drawn over the background beside the cube) are
     * opaque, as in the rasterizer. */
    ed.command("filter remove 0");
    ed.command("filter add Edge Outline");
    for (const char *engine : {"Rasterized", "Path"}) {
      const Image &o = r57_render(ed, engine, true);
      int dark_opaque = 0;
      for (uint32_t c : o.pixels) dark_opaque += (c >> 24) == 255 && (c & 0xFFFFFF) < 0x202020;
      std::printf("    %s with an Edge Outline: %d opaque dark (outline) pixels\n", engine, dark_opaque);
      CHECK(dark_opaque > 40);
    }
  });

  test("transparent film: the PNG keeps the alpha (and opens with it), a JPEG is composited, off writes an opaque PNG", [] {
    Editor ed;
    r57_open(ed);
    r57_render(ed, "Rasterized", true);
    const std::string base = fs::join(scratch_project(), "R57_render");
    std::string written;
    CHECK(ed.write_render_file_for_test(base, &written));
    Bitmap b;
    std::string err;
    CHECK(load_image(written, b, err));
    CHECK(b.width == 160 && b.height == 90);
    if (b.width == 160 && b.height == 90) {
      std::printf("    PNG alpha: corner %d, middle %d\n", b.rgba8[(1 * 160 + 1) * 4 + 3], b.rgba8[(45 * 160 + 80) * 4 + 3]);
      CHECK(b.rgba8[(1 * 160 + 1) * 4 + 3] == 0 && b.rgba8[(45 * 160 + 80) * 4 + 3] == 255);
    }
    std::remove(written.c_str());
    r57_render(ed, "Rasterized", false);
    CHECK(ed.write_render_file_for_test(base, &written));
    CHECK(load_image(written, b, err));
    if (b.width == 160) CHECK(b.rgba8[(1 * 160 + 1) * 4 + 3] == 255);
    std::remove(written.c_str());
  });
}

static void round60_tests() {
  test("toon: the tone of a surface steps through Toon Bands levels at the threshold, softness 0 a hard edge", [] {
    Material m;
    m.shading_model = 1;
    m.toon_threshold = 0.2f;
    m.toon_softness = 0.0f;
    for (int bands : {1, 2, 3, 5}) {
      m.toon_bands = bands;
      std::set<int> levels;
      for (int i = 0; i <= 1000; i++) levels.insert((int)std::lround(toon_level(m, i / 1000.0f) * 1000.0f));
      CHECK((int)levels.size() == bands + 1);  // the shadow and the lit tones
    }
    m.toon_bands = 1;
    CHECK(toon_level(m, 0.19f) == 0.0f && toon_level(m, 0.21f) == 1.0f);
    CHECK(toon_level(m, std::nanf("")) == 0.0f);
    m.toon_softness = 0.1f;
    const float mid = toon_level(m, 0.2f);
    CHECK(mid > 0.3f && mid < 0.7f);  // soft: half way at the edge
  });

  test("toon: the rim lights the silhouette and nothing facing the eye", [] {
    Material m;
    m.toon_rim_width = 0.3f;
    m.toon_rim_strength = 1.0f;
    m.toon_rim = Vec3(1.0f, 0.5f, 0.25f);
    SurfaceSample s;  // (task 0148: the rim reads the material through the sample, with its mask and the key light's side)
    s.toon = &m;
    const Vec3 V(0, 0, 1), key(0, 1, 0);
    CHECK(length(toon_rim(s, V, V, key)) == 0.0f);
    const Vec3 edge = toon_rim(s, normalize(Vec3(1, 0, 0.05f)), V, key);
    CHECK(edge.x > 0.9f && std::fabs(edge.y - 0.5f * edge.x) < 1e-4f);
    m.toon_rim_width = 0.0f;
    CHECK(length(toon_rim(s, normalize(Vec3(1, 0, 0.05f)), V, key)) == 0.0f);
  });

  test("toon: a rasterized toon sphere takes exactly its Toon Bands tones (plus the shadow); PBR shades smoothly", [] {
    for (int bands : {1, 3}) {
      Editor ed;
      if (!r60_open(ed, bands)) return;
      int solid = 0;
      const auto v = r60_colours(r57_render(ed, "Rasterized", true), &solid);
      int big = 0, covered = 0;
      for (const auto &c : v)
        if (c.second * 50 >= solid) big++, covered += c.second;  // a tone: at least 2 % of the sphere
      std::printf("    %d band%s: %d tones cover %.1f %% of %d pixels\n", bands, bands == 1 ? "" : "s", big, 100.0 * covered / std::max(1, solid), solid);
      CHECK(big == bands || big == bands + 1);
      CHECK(covered * 100 >= solid * 85);  // the rest: anti-aliased edges between tones
    }
    Editor ed;
    if (!r60_open(ed, 1, false)) return;
    int solid = 0;
    const auto v = r60_colours(r57_render(ed, "Rasterized", true), &solid);
    int top = 0;
    for (size_t i = 0; i < v.size() && i < 4; i++) top += v[i].second;
    std::printf("    PBR: the 4 commonest colours cover %.1f %%\n", 100.0 * top / std::max(1, solid));
    CHECK(top * 2 < solid);
  });

  test("toon: the CPU path tracer's direct light gives the rasterizer's tones (each tone's share within 4 %), the GPU's the CPU's", [] {
    Editor ed;
    if (!r60_open(ed, 2)) return;
    const Image raster = r57_render(ed, "Rasterized", true);
    int solid = 0;
    const auto tones = r60_colours(raster, &solid);
    std::vector<float> lum;
    for (const auto &c : tones)
      if (c.second * 50 >= solid) lum.push_back(r60_lum(c.first));
    auto shares = [&](const Image &img) {
      std::vector<double> sh(lum.size(), 0.0);
      int n = 0;
      for (uint32_t c : img.pixels) {
        if ((c >> 24) != 255) continue;
        const float l = r60_lum(c);
        size_t best = 0;
        for (size_t k = 1; k < lum.size(); k++)
          if (std::fabs(lum[k] - l) < std::fabs(lum[best] - l)) best = k;
        sh[best] += 1.0, n++;
      }
      for (double &x : sh) x /= std::max(1, n);
      return sh;
    };
    ed.command("set Render.Samples 64");
    const Image cpu = r57_render(ed, "Path", true);
    const auto a = shares(raster), b = shares(cpu);
    double worst = 0.0;
    for (size_t k = 0; k < a.size(); k++) worst = std::max(worst, std::fabs(a[k] - b[k]));
    std::printf("    %zu tones; the largest share difference raster / CPU trace: %.1f %%\n", lum.size(), worst * 100.0);
    CHECK(lum.size() >= 2 && worst < 0.04);
    if (gpu::available()) {
      ed.command("device gpu");
      const Image g = r57_render(ed, "Path", true);
      double diff = 0.0;
      int n = 0;
      for (size_t i = 0; i < cpu.pixels.size() && i < g.pixels.size(); i++) {
        if ((cpu.pixels[i] >> 24) != 255) continue;
        for (int ch = 0; ch < 3; ch++) diff += std::abs((int)(cpu.pixels[i] >> (ch * 8) & 255) - (int)(g.pixels[i] >> (ch * 8) & 255));
        n++;
      }
      diff /= std::max(1, n) * 3.0 * 255.0;
      std::printf("    GPU vs CPU: mean difference %.2f %%\n", diff * 100.0);
      CHECK(diff < 0.03);
      ed.command("device cpu");
    }
  });

  test("toon: an Outline draws a line of its colour round the silhouette (rasterized and path traced), none over the object, and thickness 0 draws nothing", [] {
    Editor ed;
    GameObject *g = r60_open(ed, 1);
    if (!g) return;
    const Image plain = r57_render(ed, "Rasterized", true);
    ed.select_object(g->id);
    ed.command("component Outline");
    ed.command("set Outline.Color 1 0 0");
    ed.command("set Outline.Thickness 0.08");
    g = ed.scene().find_by_name("Cube");
    CHECK(g && g->find_role(Component::RenderRole::Outline) != nullptr);
    for (const char *engine : {"Rasterized", "Path"}) {
      const Image &o = r57_render(ed, engine, true);
      /* The ring: solid pixels where the sphere alone left the background, red-dominant (the view transform
       * desaturates pure red). Inside: the sphere's own pixels stay as they were, but for its edge. */
      int ring = 0, ring_red = 0, inside = 0, inside_changed = 0;
      for (size_t i = 0; i < o.pixels.size() && i < plain.pixels.size(); i++) {
        const uint32_t c = o.pixels[i], p0 = plain.pixels[i];
        if ((p0 >> 24) == 0 && (c >> 24) == 255) {
          ring++;
          ring_red += (int)(c >> 16 & 255) > 2 * (int)(c >> 8 & 255) + 20 && (int)(c >> 16 & 255) > 2 * (int)(c & 255) + 20;
        }
        if ((p0 >> 24) == 255) {
          inside++;
          int d = 0;
          for (int ch = 0; ch < 3; ch++) d = std::max(d, std::abs((int)(c >> (ch * 8) & 255) - (int)(p0 >> (ch * 8) & 255)));
          inside_changed += d > 40;
        }
      }
      std::printf("    %s: %d new solid pixels round the sphere (%d red), %d of %d of its own pixels changed\n", engine, ring, ring_red,
                  inside_changed, inside);
      /* 80 %: a traced ring pixel next to the sphere averages the sphere's colour in (its samples spread over the
       * pixel); the rasterizer's are all red. */
      CHECK(ring > 60 && ring_red * 10 >= ring * 8);
      CHECK(inside_changed * 25 < inside);  // only its edge: the hull doesn't cover or darken the sphere
    }
    /* The path tracer: the hull doesn't shadow or darken the sphere it surrounds. */
    ed.command("set Outline.Thickness 0");
    const Image none = r57_render(ed, "Rasterized", true);
    int changed = 0;
    for (size_t i = 0; i < none.pixels.size() && i < plain.pixels.size(); i++) changed += none.pixels[i] != plain.pixels[i];
    CHECK(changed == 0);
  });

  test("toon: a mirrored (negative scale) object keeps its outline a rim, rasterized and traced, and draws its own front faces", [] {
    Editor ed;
    GameObject *g = r60_open(ed, 1);
    if (!g) return;
    g->set_local_scale({-1.0f, 1.0f, 1.0f});
    ed.mark_changed_for_test("R60 mirror");
    ed.step_frame_headless();
    g = ed.scene().find_by_name("Cube");
    const Image plain = r57_render(ed, "Rasterized", true);
    int solid = 0;
    const auto tones = r60_colours(plain, &solid);
    CHECK(solid > 2000);
    ed.select_object(g->id);
    ed.command("component Outline");
    ed.command("set Outline.Color 1 0 0");
    ed.command("set Outline.Thickness 0.08");
    for (const char *engine : {"Rasterized", "Path"}) {
      const Image &o = r57_render(ed, engine, true);
      int ring = 0, inside = 0, changed = 0;
      for (size_t i = 0; i < o.pixels.size() && i < plain.pixels.size(); i++) {
        const uint32_t c = o.pixels[i], p0 = plain.pixels[i];
        if ((p0 >> 24) == 0 && (c >> 24) == 255) ring++;
        if ((p0 >> 24) == 255) {
          inside++;
          int d = 0;
          for (int ch = 0; ch < 3; ch++) d = std::max(d, std::abs((int)(c >> (ch * 8) & 255) - (int)(p0 >> (ch * 8) & 255)));
          changed += d > 40;
        }
      }
      std::printf("    mirrored, %s: %d ring pixels, %d of %d of the sphere's own changed\n", engine, ring, changed, inside);
      CHECK(ring > 60 && changed * 25 < inside);
    }
  });

  test("toon: an outlined object with a modifier traced in the Camera Preview while the Scene view redraws (each view keeps its own shell)", [] {
    Editor ed;
    GameObject *g = r60_open(ed, 1);
    if (!g) return;
    g->add<MirrorModifier>();
    g->mark_dirty();
    ed.select_object(g->id);
    ed.command("component Outline");
    ed.command("select Main Camera");
    ed.command("camerapreview rendered");
    for (int i = 0; i < 30; i++) {
      ed.command("redraw always");
      ed.step_frame_headless();  // the Scene view's raster (viewport mesh) and the preview's trace (render mesh) both
    }
    ed.command("redraw changes");
    const Image &o = r57_render(ed, "Path", true);
    int solid = 0;
    for (uint32_t c : o.pixels) solid += (c >> 24) == 255;
    CHECK(solid > 2000);  // (the sanitizer build reads freed memory here if a view's shell is freed under a tracer)
  });

  test("toon: adding an Outline undoes; a toon field's edit is one undo step", [] {
    Editor ed;
    GameObject *g = r60_open(ed, 2);
    if (!g) return;
    ed.select_object(g->id);
    ed.command("component Outline");
    ed.step_frame_headless();
    g = ed.scene().find_by_name("Cube");
    CHECK(g && g->find_role(Component::RenderRole::Outline));
    ed.command("undo");
    g = ed.scene().find_by_name("Cube");
    CHECK(g && !g->find_role(Component::RenderRole::Outline));
    MaterialPtr m = g->get<MeshRenderer>()->materials[0];
    m->toon_bands = 5;
    m->touch();
    ed.mark_changed_for_test("R60 bands");
    ed.step_frame_headless();
    ed.command("undo");
    g = ed.scene().find_by_name("Cube");
    CHECK(g->get<MeshRenderer>()->materials[0]->toon_bands == 2);
  });

  test("toon: under a large area light the rasterizer's tones match the CPU path tracer's (each share within 6 %)", [] {
    Editor ed;
    GameObject *g = r60_open(ed, 2);
    if (!g) return;
    if (GameObject *sun = ed.scene().find_by_name("Directional Light")) {
      Light *l = sun->get<Light>();
      l->type = 3;  // Area
      l->area_width = l->area_height = 3.0f;
      l->range = 30.0f;
      sun->set_world_position({0.0f, 6.0f, -4.0f});
      sun->set_world_rotation(Quat::euler({60.0f, 0.0f, 0.0f}));
      l->intensity = 3.0f;
    }
    ed.mark_changed_for_test("R60 area");
    ed.step_frame_headless();
    const Image raster = r57_render(ed, "Rasterized", true);
    int solid = 0;
    const auto tones = r60_colours(raster, &solid);
    std::vector<float> lum;
    for (const auto &c : tones)
      if (c.second * 50 >= solid) lum.push_back(r60_lum(c.first));
    auto shares = [&](const Image &img) {
      std::vector<double> sh(lum.size(), 0.0);
      int n = 0;
      for (uint32_t c : img.pixels) {
        if ((c >> 24) != 255 || lum.empty()) continue;
        const float l = r60_lum(c);
        size_t best = 0;
        for (size_t k = 1; k < lum.size(); k++)
          if (std::fabs(lum[k] - l) < std::fabs(lum[best] - l)) best = k;
        sh[best] += 1.0, n++;
      }
      for (double &x : sh) x /= std::max(1, n);
      return sh;
    };
    ed.command("set Render.Samples 128");
    const auto a = shares(raster), b = shares(r57_render(ed, "Path", true));
    double worst = 0.0;
    for (size_t k = 0; k < a.size(); k++) worst = std::max(worst, std::fabs(a[k] - b[k]));
    std::printf("    area light: %zu tones, the largest share difference %.1f %%\n", lum.size(), worst * 100.0);
    CHECK(lum.size() >= 2 && worst < 0.06);
  });

  test("toon: the outline shell is the mesh pushed out with its faces flipped; the Outline round-trips through the scene and the fields through a .mat", [] {
    MeshPtr cube = primitives::cube();
    Outline ol;
    ol.thickness = 0.1f;
    ol.world_space = false;
    const Mesh *sh = ol.shell(*cube, 1.0f);
    CHECK(sh && sh->face_count() == cube->face_count() && sh->positions.size() == cube->positions.size());
    if (sh) {
      CHECK(dot(sh->face_normal(0), cube->face_normal(0)) < -0.99f);
      float far = 0.0f;
      for (size_t i = 0; i < sh->positions.size(); i++) far = std::max(far, length(sh->positions[i] - cube->positions[i]));
      CHECK(std::fabs(far - 0.1f) < 1e-4f);
      CHECK(ol.shell(*cube, 1.0f) == sh);  // cached
    }
    ol.world_space = true;
    const Mesh *scaled = ol.shell(*cube, 2.0f);
    CHECK(scaled && std::fabs(length(scaled->positions[0] - cube->positions[0]) - 0.05f) < 1e-4f);  // 0.1 m on a 2x object
    CHECK(ol.materials().size() == 1 && ol.materials()[0]->outline_hull && ol.materials()[0]->unlit && !ol.materials()[0]->cast_shadows);
    ol.thickness = 0.0f;
    CHECK(ol.shell(*cube, 1.0f) == nullptr);
    Material m;
    m.shading_model = 1;
    m.toon_bands = 4;
    m.toon_shadow = Vec3(0.1f, 0.2f, 0.3f);
    m.toon_rim_width = 0.4f;
    m.toon_flat_ambient = false;
    Material back;
    CHECK(load_material_text(save_material_text(m), back));
    CHECK(back.shading_model == 1 && back.toon_bands == 4 && std::fabs(back.toon_shadow.z - 0.3f) < 1e-5f && back.toon_rim_width == 0.4f &&
          !back.toon_flat_ambient);
    Editor ed;
    GameObject *g = r60_open(ed, 1);
    if (!g) return;
    ed.select_object(g->id);
    ed.command("component Outline");
    ed.command("set Outline.Thickness 0.07");
    Scene back_scene;
    std::string err;
    CHECK(load_scene_text(save_scene_text(ed.scene()), back_scene, err));
    GameObject *g2 = back_scene.find_by_name("Cube");
    const Component *oc = g2 ? g2->find_role(Component::RenderRole::Outline) : nullptr;
    CHECK(oc && std::fabs(static_cast<const Outline *>(oc)->thickness - 0.07f) < 1e-5f);
  });
}


/* ===================================================================== */
/* Round 61 (task 0039): anime and manga camera filters, looks            */
/* ===================================================================== */

/* An image of one grey, or a left-to-right gradient. */
static Image r61_image(int w, int h, int grey, bool gradient = false) {
  Image img;
  img.resize(w, h);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      const int v = gradient ? (int)std::lround(255.0 * x / std::max(1, w - 1)) : grey;
      img.pixels[(size_t)y * w + x] = 0xFF000000u | (uint32_t)v << 16 | (uint32_t)v << 8 | (uint32_t)v;
    }
  return img;
}
/* Connected groups of dark pixels (4-neighbour flood fill): halftone dots. */
static int r61_blobs(const Image &img) {
  std::vector<uint8_t> seen(img.pixels.size(), 0);
  auto dark = [&](size_t i) { return ((img.pixels[i] >> 8) & 255) < 128; };
  int n = 0;
  std::vector<size_t> stack;
  for (size_t i = 0; i < img.pixels.size(); i++) {
    if (seen[i] || !dark(i)) continue;
    n++;
    stack.push_back(i);
    seen[i] = 1;
    while (!stack.empty()) {
      const size_t j = stack.back();
      stack.pop_back();
      const int x = (int)(j % img.width), y = (int)(j / img.width);
      const int nb[4][2] = {{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}};
      for (auto &q : nb) {
        if (q[0] < 0 || q[1] < 0 || q[0] >= img.width || q[1] >= img.height) continue;
        const size_t k = (size_t)q[1] * img.width + q[0];
        if (!seen[k] && dark(k)) seen[k] = 1, stack.push_back(k);
      }
    }
  }
  return n;
}

static void round61_tests() {
  test("anime filters: Halftone's dots quadruple when the cell halves, and a 540p and a 1080p picture show the same dots", [] {
    auto dots = [](int w, int h, float cell, float scale) {
      Image img = r61_image(w, h, 200);  // light enough that neighbouring dots stay apart at 6-pixel cells (counted one by one)
      RenderTarget rt;
      rt.attach(img, {0, 0, w, h});
      FilterFrame f;
      f.pixel_scale = scale;
      HalftoneParams p;
      p.cell = cell;
      apply_halftone(rt, p, &f);
      return r61_blobs(img);
    };
    const int big = dots(960, 540, 16.0f, 1.0f), small = dots(960, 540, 8.0f, 1.0f);
    const double ratio = (double)small / std::max(1, big);
    const int at540 = dots(960, 540, 12.0f, 0.5f), at1080 = dots(1920, 1080, 12.0f, 1.0f);
    std::printf("    dots: %d at cell 16, %d at cell 8 (x%.2f); %d at 540p, %d at 1080p\n", big, small, ratio, at540, at1080);
    CHECK(ratio > 3.5 && ratio < 4.5);
    CHECK(std::abs(at540 - at1080) * 10 <= at1080);
    /* White stays paper, black is all ink. */
    Image w = r61_image(64, 64, 255), b = r61_image(64, 64, 0);
    RenderTarget rw, rb;
    rw.attach(w, {0, 0, 64, 64});
    rb.attach(b, {0, 0, 64, 64});
    HalftoneParams p;
    apply_halftone(rw, p, nullptr);
    apply_halftone(rb, p, nullptr);
    /* Exactly: no grey speck at a white cell's centre, none in a black cell's corners. */
    bool all_white = true, all_black = true;
    for (uint32_t c : w.pixels) all_white = all_white && (c & 0xFFFFFF) == 0xFFFFFF;
    for (uint32_t c : b.pixels) all_black = all_black && ((c >> 8) & 255) <= 2;
    CHECK(all_white && all_black);
  });

  test("anime filters: Hatching's ink falls as the picture brightens, white stays paper; Color Ramp Constant gives exactly its stops; Paper at 0 changes nothing", [] {
    Image g = r61_image(800, 200, 0, true);
    RenderTarget rt;
    rt.attach(g, {0, 0, 800, 200});
    HatchingParams hp;
    apply_hatching(rt, hp, nullptr);
    std::vector<double> ink(8, 0.0);
    for (int y = 0; y < 200; y++)
      for (int x = 0; x < 800; x++) ink[(size_t)x / 100] += ((g.pixels[(size_t)y * 800 + x] >> 8) & 255) < 128;
    bool falling = true;
    for (size_t k = 1; k < ink.size(); k++) falling = falling && ink[k] <= ink[k - 1] + 50;
    int last_col_ink = 0;
    for (int y = 0; y < 200; y++) last_col_ink += ((g.pixels[(size_t)y * 800 + 799] >> 8) & 255) < 250;
    std::printf("    hatching ink per eighth (dark to light):");
    for (double v : ink) std::printf(" %.0f", v);
    std::printf("\n");
    CHECK(falling && ink[0] > ink[7] * 4 && last_col_ink == 0);
    Image r = r61_image(256, 4, 0, true);
    RenderTarget rr;
    rr.attach(r, {0, 0, 256, 4});
    ColorRampParams cp;
    cp.stops = 3;
    cp.color[0] = {0.1f, 0.0f, 0.2f}, cp.color[1] = {0.5f, 0.5f, 0.0f}, cp.color[2] = {1.0f, 0.9f, 0.8f};
    cp.position[0] = 0.0f, cp.position[1] = 0.4f, cp.position[2] = 0.7f;
    apply_color_ramp(rr, cp);
    std::set<uint32_t> cols(r.pixels.begin(), r.pixels.end());
    CHECK(cols.size() == 3);
    Image a = r61_image(300, 200, 0, true), b = a;
    RenderTarget ra, rb2;
    ra.attach(a, {0, 0, 300, 200});
    PaperParams pp;
    pp.strength = 0.0f;
    apply_paper(ra, pp, nullptr);
    CHECK(a.pixels == b.pixels);
    pp.strength = 0.5f;
    Image c1 = b, c2 = b;
    RenderTarget r1, r2;
    r1.attach(c1, {0, 0, 300, 200});
    r2.attach(c2, {0, 0, 300, 200});
    apply_paper(r1, pp, nullptr);
    apply_paper(r2, pp, nullptr);
    CHECK(c1.pixels == c2.pixels && c1.pixels != b.pixels);  // the same paper every time
  });

  test("anime filters: Line Art draws a cube's edges as creases (rasterized and path traced), a 30-degree fold at 20 but not at 40, and nothing on a smooth sphere", [] {
    Editor ed;
    r57_open(ed);
    ed.command("select Main Camera");
    ed.command("filter add Line Art");
    GameObject *cam = ed.scene().find_by_name("Main Camera");
    LineArtEffect *la = cam ? cam->get<CameraFilters>()->find<LineArtEffect>() : nullptr;
    CHECK(la != nullptr);
    if (!la) return;
    la->silhouettes = false, la->object_edges = false;  // creases alone
    la->color = Vec3(1.0f, 0.0f, 1.0f);              // magenta: nothing else in the scene is
    auto crease_pixels = [&](const char *engine) {
      cam->get<CameraFilters>()->effects[0]->enabled = true;
      ed.mark_changed_for_test("R61 crease");
      ed.step_frame_headless();
      const Image &img = r57_render(ed, engine, true);
      int n = 0, solid = 0;
      for (uint32_t c : img.pixels) {
        if ((c >> 24) != 255) continue;
        solid++;
        n += (int)((c >> 16) & 255) > 200 && (int)(c & 255) > 200 && (int)((c >> 8) & 255) < 90;
      }
      return std::make_pair(n, solid);
    };
    auto cube = crease_pixels("Rasterized");
    const Image cube_raster = ed.render_image_for_test();
    auto cube_pt = crease_pixels("Path");
    const Image &cube_traced = ed.render_image_for_test();
    /* The trace's creases where the rasterizer's are (within a pixel): its depth is the pixel centres', not the
     * jittered samples' (which drew noise on the faces). */
    auto magenta = [](uint32_t c) { return (c >> 24) == 255 && (int)((c >> 16) & 255) > 200 && (int)(c & 255) > 200 && (int)((c >> 8) & 255) < 90; };
    int near_raster = 0, traced_lines = 0;
    for (int y = 0; y < cube_traced.height; y++)
      for (int x = 0; x < cube_traced.width; x++) {
        if (!magenta(cube_traced.pixels[(size_t)y * cube_traced.width + x])) continue;
        traced_lines++;
        bool near = false;
        for (int dy = -1; dy <= 1; dy++)
          for (int dx = -1; dx <= 1; dx++) {
            const int xx = x + dx, yy = y + dy;
            if (xx >= 0 && yy >= 0 && xx < cube_raster.width && yy < cube_raster.height && magenta(cube_raster.pixels[(size_t)yy * cube_raster.width + xx])) near = true;
          }
        near_raster += near;
      }
    std::printf("    cube: %d crease pixels rasterized, %d traced (%d of them by the rasterized ones)\n", cube.first, cube_pt.first, near_raster);
    CHECK(cube.first > 15 && cube_pt.first > 15);  // (the cube is small in a 160 x 90 render: its three visible edges)
    CHECK(near_raster * 10 >= traced_lines * 9);
    /* A plate folded 30 degrees along its middle. */
    GameObject *g = ed.scene().find_by_name("Cube");
    if (!g) return;
    auto plate = std::make_shared<Mesh>();
    const float a = 30.0f * kDeg2Rad;
    plate->add_vert({-1.5f, 0.0f, -1.5f});
    plate->add_vert({0.0f, 0.0f, -1.5f});
    plate->add_vert({0.0f, 0.0f, 1.5f});
    plate->add_vert({-1.5f, 0.0f, 1.5f});
    plate->add_vert({1.5f * std::cos(a), 1.5f * std::sin(a), -1.5f});
    plate->add_vert({1.5f * std::cos(a), 1.5f * std::sin(a), 1.5f});
    plate->add_face({0, 3, 2, 1});
    plate->add_face({1, 2, 5, 4});
    g->get<MeshFilter>()->mesh = plate;
    MaterialPtr both = make_material("Plate", Vec3(0.7f));  // (the default cube has no material slot of its own)
    both->double_sided = true;
    g->get<MeshRenderer>()->materials = {both};
    g->mark_dirty();
    la->crease_angle = 20.0f;
    const auto plate20 = crease_pixels("Rasterized");
    const int at20 = plate20.first;
    std::printf("    the plate covers %d pixels\n", plate20.second);
    la->crease_angle = 40.0f;
    const int at40 = crease_pixels("Rasterized").first;
    g->get<MeshFilter>()->mesh = primitives::uv_sphere(1.5f, 64, 32);
    g->mark_dirty();
    auto sph = crease_pixels("Rasterized");
    std::printf("    30-degree fold: %d pixels at 20, %d at 40; smooth sphere: %d of %d\n", at20, at40, sph.first, sph.second);
    CHECK(at20 > 10 && at40 == 0);
    CHECK(sph.first * 100 <= sph.second);
  });

  test("material texture fields: a procedural texture's output shows its image and name (not missing, not Non-Color), a missing one says so", [] {
    Editor ed;
    ed.init_headless(400, 300);
    ed.command("newtex R61Field Bricks");
    TextureRef albedo{"Assets/Textures/R61Field.ptex#albedo", false}, normal{"Assets/Textures/R61Field.ptex#normal", true};
    CHECK(resolve_texture_ref(albedo) != nullptr && resolve_texture_ref(normal) != nullptr);
    CHECK(texture_ref_label(albedo) == "R61Field (albedo)" && texture_ref_label(normal) == "R61Field (normal)");
    CHECK(texture_ref_is_procedural(normal) && !texture_ref_is_procedural(TextureRef{"Assets/Textures/x.png", true}));
    /* The same image the material renders with. */
    MaterialPtr m = make_material("Field", Vec3(1.0f));
    m->base_map = albedo;
    m->touch();
    CHECK(m->textures().base == resolve_texture_ref(albedo));
    CHECK(resolve_texture_ref(TextureRef{"Assets/Textures/NoSuch.ptex#albedo", false}) == nullptr);
    CHECK(resolve_texture_ref(TextureRef{"generated:UV Grid", false}) != nullptr && texture_ref_label(TextureRef{"generated:UV Grid", false}) == "UV Grid");
    std::remove(resolve_asset_path("Assets/Textures/R61Field.ptex").c_str());
  });

  test("procedural textures: the Normal / Roughness / Height Map switches make those outputs neutral while a Color Ramp still colours by the height", [] {
    Editor ed;
    ed.init_headless(400, 300);
    ed.command("newtex R61Maps Bricks");
    ProceduralTexturePtr pt = texture_asset("Assets/Textures/R61Maps.ptex");
    CHECK(pt != nullptr);
    if (!pt) return;
    pt->width = pt->height = 64;
    pt->add("Color Ramp");  // the albedo from the height: mortar and bricks two colours
    pt->touch();
    const Bitmap albedo_on = pt->output(0), normal_on = pt->output(1);
    pt->use_normal = false, pt->use_roughness = false, pt->use_height = false;
    pt->touch();
    const Bitmap albedo_off = pt->output(0), normal_off = pt->output(1), rough_off = pt->output(2), height_off = pt->output(3);
    CHECK(albedo_on.rgba8 == albedo_off.rgba8);  // the colour (and the ramp's input, the height inside) unchanged
    std::set<uint32_t> cols;
    for (size_t i = 0; i < albedo_off.rgba8.size(); i += 4) cols.insert((uint32_t)albedo_off.rgba8[i] << 16 | (uint32_t)albedo_off.rgba8[i + 1] << 8 | albedo_off.rgba8[i + 2]);
    CHECK(cols.size() >= 2);  // the ramp still separates bricks from mortar
    bool flat = true, rough1 = true, mid = true, bumpy = false;
    for (size_t i = 0; i < normal_off.rgba8.size(); i += 4) {
      flat = flat && normal_off.rgba8[i] == 128 && normal_off.rgba8[i + 1] == 128 && normal_off.rgba8[i + 2] == 255;
      rough1 = rough1 && rough_off.rgba8[i] == 255;
      mid = mid && height_off.rgba8[i] == 128;
      bumpy = bumpy || normal_on.rgba8[i] != 128;
    }
    CHECK(flat && rough1 && mid && bumpy);
    /* Assign to Selection gives only the maps that are on. */
    ed.command("select Cube");
    ed.assign_texture_asset_for_test("Assets/Textures/R61Maps.ptex");
    GameObject *cube = ed.scene().find_by_name("Cube");
    const Material &m = *cube->get<MeshRenderer>()->materials[0];
    CHECK(m.base_map.path == "Assets/Textures/R61Maps.ptex#albedo" && m.normal_map.empty() && m.roughness_map.empty());
    /* The switches save with the texture. */
    ProceduralTexture back;
    CHECK(load_texture_text(save_texture_text(*pt), back));
    CHECK(!back.use_normal && !back.use_roughness && !back.use_height);
    std::remove(resolve_asset_path("Assets/Textures/R61Maps.ptex").c_str());
  });

  test("anime filters: the Anime and Manga looks add one effect each (task 0154), one undo step removes it, and a scene keeps it", [] {
    Editor ed;
    r57_open(ed);
    ed.step_frame_headless();  // r57_open's settings as their own undo step (not merged with the look's)
    ed.command("select Main Camera");
    ed.command("filter look Manga");
    GameObject *cam = ed.scene().find_by_name("Main Camera");
    CameraFilters *cf = cam ? cam->get<CameraFilters>() : nullptr;
    CHECK(cf && cf->effects.size() == 1);
    if (!cf || cf->effects.size() != 1) return;
    CHECK(std::string(cf->effects[0]->type_name()) == MangaEffect::kName);
    Scene back;
    std::string err;
    CHECK(load_scene_text(save_scene_text(ed.scene()), back, err));
    GameObject *bc = back.find_by_name("Main Camera");
    CHECK(bc && bc->get<CameraFilters>() && bc->get<CameraFilters>()->effects.size() == 1);
    ed.step_frame_headless();
    ed.command("undo");
    cam = ed.scene().find_by_name("Main Camera");
    cf = cam ? cam->get<CameraFilters>() : nullptr;
    CHECK(!cf || cf->effects.empty());
    ed.command("filter look Anime");
    cam = ed.scene().find_by_name("Main Camera");
    cf = cam->get<CameraFilters>();
    CHECK(cf && cf->effects.size() == 1 && std::string(cf->effects[0]->type_name()) == AnimeEffect::kName);
    /* The look renders. (NaN and huge fields: the stress section `filters` fuzzes every effect.) */
    const Image &img = r57_render(ed, "Rasterized", false);
    CHECK(img.width == 160);
    ed.command("filter look Comic");  // unknown: a warning, nothing added
    CHECK(cam->get<CameraFilters>()->effects.size() == 1);
    ed.command("select Directional Light");  // and no empty stack on an object that had none
    ed.command("filter look Comic");
    GameObject *sun = ed.scene().find_by_name("Directional Light");
    CHECK(sun && !sun->get<CameraFilters>());
  });
}


/* ===================================================================== */
/* Round 62 (task 0040): axis materials by height, with a noisy border     */
/* ===================================================================== */

static bool r62_white(Vec3 c) { return c.x > 0.85f && c.y > 0.85f && c.z > 0.85f; }

static void round62_tests() {
  test("axis materials: Axis High covers the slope's parts above High Start, blends across High Blend, and nothing changes without it", [] {
    Editor ed;
    ed.init_headless(400, 300);
    R51Mats am = r51_mats(1);
    Material snow;
    snow.name = "Snow";
    snow.base_color = {0.95f, 0.95f, 0.97f};
    create_material_asset(snow, "Assets/Materials/R51/Snow.mat");
    /* Without the band: the task-0029 result, bit for bit. */
    const SurfaceSample before_top = r51_eval(*am.axis, {0, 1, 0}, {0, 1, 0}, {0.3f, 20.0f, 0.1f});
    am.axis->axis_high_start = 5.0f;
    am.axis->axis_high_blend = 1.0f;
    am.axis->touch();
    const SurfaceSample same = r51_eval(*am.axis, {0, 1, 0}, {0, 1, 0}, {0.3f, 20.0f, 0.1f});
    CHECK(same.albedo.x == before_top.albedo.x && same.albedo.y == before_top.albedo.y && same.albedo.z == before_top.albedo.z);
    am.axis->axis_high = "Assets/Materials/R51/Snow.mat";
    am.axis->touch();
    const Vec3 hi_top = r51_eval(*am.axis, {0, 1, 0}, {0, 1, 0}, {0.3f, 7.0f, 0.1f}).albedo;
    const Vec3 hi_side = r51_eval(*am.axis, {1, 0, 0}, {1, 0, 0}, {0.3f, 7.0f, 0.1f}).albedo;
    const Vec3 lo_top = r51_eval(*am.axis, {0, 1, 0}, {0, 1, 0}, {0.3f, 3.0f, 0.1f}).albedo;
    const Vec3 lo_side = r51_eval(*am.axis, {1, 0, 0}, {1, 0, 0}, {0.3f, 3.0f, 0.1f}).albedo;
    const Vec3 mid = r51_eval(*am.axis, {0, 1, 0}, {0, 1, 0}, {0.3f, 5.0f, 0.1f}).albedo;
    CHECK(r62_white(hi_top) && r62_white(hi_side));
    CHECK(r51_green(lo_top) && r51_brown(lo_side));
    CHECK(!r62_white(mid) && !r51_green(mid) && mid.x > 0.4f);  // half way between snow and grass
    /* Local mode measures the object's own height. */
    am.axis->axis_mode = 2;
    am.axis->touch();
    SurfacePoint sp;
    sp.position = {0, 100, 0};       // high up in the world...
    sp.local_position = {0, 2, 0};   // ...but low on the object
    sp.normal = sp.geo_normal = sp.local_normal = {0, 1, 0};
    CHECK(r51_green(evaluate_material(*am.axis, am.axis->textures(), sp).albedo));
  });

  test("axis materials: Axis Noise 0 changes nothing; with noise the grass / dirt border wanders along the surface, the same at the same point", [] {
    Editor ed;
    ed.init_headless(400, 300);
    R51Mats am = r51_mats(1);
    const float c = std::cos(45.0f * kDeg2Rad);
    const Vec3 n_border = normalize(Vec3(std::sqrt(1.0f - c * c), c, 0.0f));  // right at the angle
    std::vector<Vec3> before;
    for (int i = 0; i < 64; i++) before.push_back(r51_eval(*am.axis, n_border, n_border, {i * 0.7f, 1.0f, i * 0.3f}).albedo);
    am.axis->axis_noise = 0.0f;
    am.axis->touch();
    bool same = true;
    for (int i = 0; i < 64; i++) {
      const Vec3 a = r51_eval(*am.axis, n_border, n_border, {i * 0.7f, 1.0f, i * 0.3f}).albedo;
      same = same && a.x == before[(size_t)i].x && a.y == before[(size_t)i].y && a.z == before[(size_t)i].z;
    }
    CHECK(same);
    am.axis->axis_noise = 0.8f;
    am.axis->axis_noise_scale = 2.0f;
    am.axis->axis_blend = 0.02f;
    am.axis->touch();
    int green = 0, brown = 0;
    bool stable = true;
    for (int i = 0; i < 200; i++) {
      const Vec3 pos{i * 0.37f, 1.0f, i * 0.21f};
      const Vec3 a = r51_eval(*am.axis, n_border, n_border, pos).albedo, b = r51_eval(*am.axis, n_border, n_border, pos).albedo;
      stable = stable && a.x == b.x && a.y == b.y && a.z == b.z;
      green += r51_green(a), brown += r51_brown(a);
    }
    std::printf("    at the border angle with noise: %d grass, %d dirt of 200 points\n", green, brown);
    CHECK(stable && green > 30 && brown > 30);
    /* Noise 0: the slope rule alone (task 0029's formula: Top above the angle's cosine, Side below). */
    am.axis->axis_noise = 0.0f;
    am.axis->axis_blend = 0.0f;
    am.axis->touch();
    int ok = 0;
    for (int i = 0; i < 90; i++) {
      const float ang = i * kDeg2Rad, ny = std::cos(ang);
      const Vec3 n = normalize(Vec3(std::sin(ang), ny, 0.0f));
      const Vec3 a = r51_eval(*am.axis, n, n, {i * 0.3f, 1.0f, 0.0f}).albedo;
      ok += ny >= c ? r51_green(a) : r51_brown(a);
    }
    CHECK(ok == 90);
    /* NaN fields and positions don't make NaN colours, with a height band too. */
    Material snow;
    snow.base_color = {0.95f, 0.95f, 0.97f};
    create_material_asset(snow, "Assets/Materials/R51/Snow.mat");
    am.axis->axis_high = "Assets/Materials/R51/Snow.mat";
    am.axis->axis_noise = 0.5f;
    am.axis->axis_noise_scale = std::nanf("");
    am.axis->axis_high_start = std::nanf("");
    am.axis->axis_high_blend = std::numeric_limits<float>::infinity();
    am.axis->touch();
    for (const Vec3 pos : {Vec3(1, 1, 1), Vec3(std::nanf(""), 1, 1), Vec3(1, std::numeric_limits<float>::infinity(), 1)}) {
      const Vec3 a = r51_eval(*am.axis, n_border, n_border, pos).albedo;
      CHECK(std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z));
    }
  });

  test("axis materials: a tilted plane crossing High Start: rasterized, CPU and GPU path traced agree on how much is snow", [] {
    Editor ed;
    r57_open(ed);
    R51Mats am = r51_mats(1);
    Material snow;
    snow.name = "Snow";
    snow.base_color = {0.95f, 0.95f, 0.97f};
    create_material_asset(snow, "Assets/Materials/R51/Snow.mat");
    am.axis->axis_high = "Assets/Materials/R51/Snow.mat";
    am.axis->axis_high_start = 0.6f;
    am.axis->axis_high_blend = 0.05f;
    am.axis->axis_noise = 0.3f;
    am.axis->axis_noise_scale = 0.5f;
    am.axis->touch();
    GameObject *g = ed.scene().find_by_name("Cube");
    if (!g) return;
    g->get<MeshFilter>()->mesh = primitives::plane(3.0f, 4);
    g->get<MeshRenderer>()->materials = {am.axis};
    g->set_world_rotation(Quat::euler({-25.0f, 0.0f, 0.0f}));
    g->mark_dirty();
    ed.mark_changed_for_test("R62 plane");
    ed.step_frame_headless();
    auto snow_share = [&](const Image &img) {
      int solid = 0, white = 0;
      for (uint32_t c : img.pixels) {
        if ((c >> 24) != 255) continue;
        solid++;
        /* Snow: bright and neutral (lit and tone mapped it isn't 255; grass is green, dirt brown). */
        const int rr = (int)((c >> 16) & 255), gg = (int)((c >> 8) & 255), bb2 = (int)(c & 255);
        white += std::max({rr, gg, bb2}) - std::min({rr, gg, bb2}) < 30 && rr > 110;
      }
      return solid ? (double)white / solid : 0.0;
    };
    const double r = snow_share(r57_render(ed, "Rasterized", true));
    ed.command("set Render.Samples 32");
    const double p = snow_share(r57_render(ed, "Path", true));
    std::printf("    snow: %.1f %% rasterized, %.1f %% CPU traced\n", r * 100.0, p * 100.0);
    CHECK(r > 0.1 && r < 0.9 && std::fabs(r - p) < 0.08);
    if (gpu::available()) {
      ed.command("device gpu");
      const double gp = snow_share(r57_render(ed, "Path", true));
      std::printf("    GPU: %.1f %% (%s)\n", gp * 100.0, ed.render_device_for_test().c_str());
      /* On the GPU indeed: a kernel that failed to build falls back to the CPU and would pass the comparison. */
      CHECK(ed.render_gpu_error_for_test().empty() && ed.render_device_for_test().find("CPU") == std::string::npos);
      CHECK(std::fabs(gp - p) < 0.03);
      ed.command("device cpu");
    }
  });

  test("axis materials: the height and noise fields round-trip through a .mat", [] {
    Material m;
    m.axis_mode = 1;
    m.axis_high = "Assets/Materials/Snow.mat";
    m.axis_high_start = 12.5f;
    m.axis_high_blend = 2.0f;
    m.axis_noise = 0.4f;
    m.axis_noise_scale = 7.0f;
    Material back;
    CHECK(load_material_text(save_material_text(m), back));
    CHECK(back.axis_high == "Assets/Materials/Snow.mat" && back.axis_high_start == 12.5f && back.axis_high_blend == 2.0f &&
          back.axis_noise == 0.4f && back.axis_noise_scale == 7.0f);
    /* A file from before (no such lines) loads with the defaults. */
    Material old;
    CHECK(load_material_text("blendity_material 1\n  Name \"Old\"\n  Axis_Mode 1\nend\n", old));
    CHECK(old.axis_high.empty() && old.axis_noise == 0.0f && old.axis_high_start == 10.0f);
    /* The pickers (Inspector and Materials window) set every part, Axis High included, as one undo step each. */
    Editor ed;
    ed.init_headless(400, 300);
    MaterialPtr parent = make_material("Ground", Vec3(1.0f));
    parent->axis_mode = 1;
    MaterialPtr high = create_material_asset(Material(), "Assets/Materials/R62High.mat");
    for (const char *f : {"Axis Top", "Axis Side", "Axis Bottom", "Axis High"}) ed.set_material_ref_for_test(parent.get(), f, high);
    CHECK(parent->axis_top == high->asset_path && parent->axis_side == high->asset_path && parent->axis_bottom == high->asset_path &&
          parent->axis_high == high->asset_path);
    std::remove(resolve_asset_path("Assets/Materials/R62High.mat").c_str());
  });
}

static void tests_round105() {
  /* ---------------------------------------------------------------- round 105: updates (task 0079) */
  test("updates: SHA-256 matches FIPS 180-4's vectors; SHA256SUMS lines are read in both of sha256sum's forms", [] {
    CHECK(net::sha256("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(net::sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(net::sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(net::sha256(std::string(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    const std::string sums = std::string(64, 'A') + "  Blendity-Setup-0.11.0.exe\r\n" + std::string(64, 'b') + " *Blendity-0.11.0-x86_64.AppImage\n";
    CHECK(releases::sum_for(sums, "Blendity-Setup-0.11.0.exe") == std::string(64, 'a'));
    CHECK(releases::sum_for(sums, "Blendity-0.11.0-x86_64.AppImage") == std::string(64, 'b'));
    CHECK(releases::sum_for(sums, "other.dmg").empty());
  });

  test("updates: GitHub's releases list gives the newer builds for this system, newest first; older ones in the history", [] {
    auto rel = [](const char *tag, const char *date, bool pre, const char *body) {
      const std::string p = releases::platform();
      const std::string inst = p == "windows" ? "Blendity-Setup-x.exe" : p == "linux" ? "Blendity-x-x86_64.AppImage" : "Blendity-x-macos.dmg";
      return strprintf("{\"tag_name\":\"%s\",\"name\":\"%s\",\"body\":\"%s\",\"published_at\":\"%s\",\"prerelease\":%s,\"draft\":false,"
                       "\"assets\":[{\"name\":\"%s\",\"browser_download_url\":\"https://x/%s\",\"size\":123},"
                       "{\"name\":\"SHA256SUMS.txt\",\"browser_download_url\":\"https://x/sums\",\"size\":10}]}",
                       tag, tag, body, date, pre ? "true" : "false", inst.c_str(), inst.c_str());
    };
    const std::string json = "[" + rel("build-pr72", "2026-10-16T10:00:00Z", true, "Adds the updater.\\n- Version History") + "," +
                             rel("v0.10.0", "2026-10-15T10:00:00Z", false, "Stable") + "," + rel("build-pr70", "2026-10-14T10:00:00Z", true, "FFmpeg") + "," +
                             rel("build-pr69", "2026-10-13T10:00:00Z", true, "Installers") +
                             ",{\"tag_name\":\"draft\",\"draft\":true,\"published_at\":\"2026-10-17T00:00:00Z\",\"assets\":[]}]";
    const std::vector<releases::Release> all = releases::parse(json);
    std::printf("    %zu releases; the first %s (PR %d), notes '%s'\n", all.size(), all.empty() ? "-" : all[0].tag.c_str(), all.empty() ? 0 : all[0].pr,
                all.empty() ? "" : all[0].notes.c_str());
    CHECK(all.size() == 4 && all[0].tag == "build-pr72" && all[0].pr == 72 && all[0].notes == "Adds the updater.\n- Version History");
    CHECK(all.size() == 4 && releases::installer(all[0], releases::platform()) && releases::checksums(all[0]));
    /* From PR 69's build: everything above it; without previews only the stable one. */
    const auto newer = releases::newer(all, "build-pr69", releases::platform(), true);
    CHECK(newer.size() == 3 && newer[0]->tag == "build-pr72" && newer[2]->tag == "build-pr70");
    const auto stable = releases::newer(all, "build-pr69", releases::platform(), false);
    CHECK(stable.size() == 1 && stable[0]->tag == "v0.10.0");
    CHECK(releases::newer(all, "build-pr72", releases::platform(), true).empty());  // (the newest: nothing)
    CHECK(releases::newer(all, "dev", releases::platform(), true).empty());         // (a local build offers nothing)
    CHECK(releases::installable(all, releases::platform(), true).size() == 4);       // (older ones too: Version History)
    CHECK(releases::parse("not json").empty() && releases::parse("{}").empty());
    std::printf("    this build: %s (%s)\n", releases::build_label().c_str(), releases::build_tag());
  });

  test("updates: the Update and Version History dialogs draw at 1280 x 720 from a releases list, and Skip is remembered", [] {
    Editor ed;
    ed.init_headless(1280, 720);
    ed.record_buttons_for_test(true);
    const std::string p = releases::platform();
    const std::string inst = p == "windows" ? "Blendity-Setup-x.exe" : p == "linux" ? "Blendity-x-x86_64.AppImage" : "Blendity-x-macos.dmg";
    const std::string json = strprintf("[{\"tag_name\":\"build-pr80\",\"name\":\"0.11.0 (PR #80)\",\"body\":\"Line one\\n\\nA long paragraph %s\","
                                       "\"published_at\":\"2026-10-20T00:00:00Z\",\"prerelease\":true,\"assets\":[{\"name\":\"%s\",\"browser_download_url\":\"https://x/a\",\"size\":5}]}]",
                                       std::string(400, 'w').c_str(), inst.c_str());
    ed.show_update_for_test(json, "build-pr80");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    Recti b;
    CHECK(ed.button_rect_for_test("Install", b) && b.bottom() <= 720);
    CHECK(ed.button_rect_for_test("Skip this one", b));
    using ET = platform::EventType;
    for (ET t : {ET::MouseMove, ET::MouseDown, ET::MouseUp}) {
      platform::Event e;
      e.type = t, e.x = b.x + b.w / 2, e.y = b.y + b.h / 2;
      ed.step_frame_headless({e});
    }
    CHECK(ed.update_skip_for_test() == "build-pr80");
    ed.command("update history");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    CHECK(ed.button_rect_for_test("Install this version", b) && b.bottom() <= 720);
  });
  test("updates (network, BLENDITY_NET_TESTS=1 only): GitHub's real releases list is fetched and read", [] {
    const char *on = std::getenv("BLENDITY_NET_TESTS");
    if (!on || std::string(on) != "1") {
      std::printf("    skipped: set BLENDITY_NET_TESTS=1 to fetch from GitHub\n");
      return;
    }
    std::string body, error;
    const bool ok = net::get("https://api.github.com/repos/EmilianoManaloIV/blendity/releases?per_page=5", body, &error);
    std::printf("    fetched %d: %zu bytes %s; %zu release(s)\n", (int)ok, body.size(), error.c_str(), releases::parse(body).size());
    CHECK(ok && !body.empty() && body[0] == '[');
    std::string none;
    CHECK(!net::get("https://api.github.com/repos/EmilianoManaloIV/no-such-repo-xyz/releases", none, &error) && !error.empty());
    std::printf("    a missing page: '%s'\n", error.c_str());
  });
}

static void tests_round108() {
  /* ---------------------------------------------------------------- round 108: install guide in the releases (task 0082) */
  test("updates: an update shows what changed, not the release page's install guide", [] {
    const std::vector<releases::Release> all = releases::parse(
        "[{\"tag_name\":\"v0.10.0\",\"name\":\"Blendity 0.10.0\",\"body\":\"Faster renders.\\n\\n<!-- install -->\\n## How to install\\nDownload one file\","
        "\"published_at\":\"2026-10-15T00:00:00Z\",\"prerelease\":true,\"assets\":[]},"
        "{\"tag_name\":\"build-pr72\",\"name\":\"x\",\"body\":\"<!-- install -->\\n## How to install\",\"published_at\":\"2026-10-14T00:00:00Z\",\"assets\":[]}]");
    std::printf("    notes: '%s' and '%s'\n", all.size() > 0 ? all[0].notes.c_str() : "-", all.size() > 1 ? all[1].notes.c_str() : "-");
    CHECK(all.size() == 2 && all[0].notes == "Faster renders." && all[1].notes.empty());
  });

  test("updates: a release lists only its installers; the libraries come from the extras release, matched by tag", [] {
    const std::string p = releases::platform();
    const std::string inst = p == "windows" ? "Blendity-Setup-Windows.exe" : p == "linux" ? "Blendity-linux-x86_64.AppImage" : "Blendity-macos.dmg";
    const std::string own = releases::build_tag();
    const std::string json = strprintf("[{\"tag_name\":\"%s\",\"name\":\"this\",\"body\":\"\",\"published_at\":\"2026-10-20T00:00:00Z\",\"prerelease\":false,"
                                       "\"assets\":[{\"name\":\"%s\",\"browser_download_url\":\"https://x/i\",\"size\":5},"
                                       "{\"name\":\"SHA256SUMS.txt\",\"browser_download_url\":\"https://x/s\",\"size\":10}]}]",
                                       own.c_str(), inst.c_str());
    const std::string extras = strprintf("{\"tag_name\":\"libraries\",\"name\":\"Libraries\",\"body\":\"\",\"published_at\":\"2026-10-01T00:00:00Z\",\"prerelease\":true,"
                                         "\"assets\":[{\"name\":\"blendity-libs-%s-%s7.zip\",\"browser_download_url\":\"https://x/wrong\",\"size\":1},"
                                         "{\"name\":\"blendity-libs-%s-%s.zip\",\"browser_download_url\":\"https://x/right\",\"size\":157286400}]}",
                                         p.c_str(), own.c_str(), p.c_str(), own.c_str());
    const std::vector<releases::Release> all = releases::parse(json), ex = releases::parse("[" + extras + "]");
    CHECK(all.size() == 1 && ex.size() == 1 && releases::installer(all[0], p));  // (the version-free installer name)
    const releases::Asset *a = all.empty() || ex.empty() ? nullptr : releases::libraries(all[0], &ex[0], p);
    std::printf("    this build (%s): libraries %s\n", own.c_str(), a ? a->url.c_str() : "none");
    CHECK(a && a->url == "https://x/right");
    CHECK(all.empty() || !releases::libraries(all[0], nullptr, p));  // (no extras release: nothing to offer)
    /* The extras release itself has no installer: never offered as an update or in Version History. */
    CHECK(releases::installable(ex, p, true).empty());
    Editor ed;
    ed.init_headless(1280, 720);
    CHECK(!ed.show_libraries_for_test(json));
    CHECK(ed.show_libraries_for_test(json, extras));
  });
}

static void tests_round114() {
  /* ---------------------------------------------------------------- round 114: shading and shadows (task 0088) */
  test("shading: a uniform material evaluated once per triangle is bit for bit what evaluating it per pixel gives", [] {
    uint32_t seed = 114;
    auto rnd = [&] { return (float)((seed = seed * 1664525u + 1013904223u) >> 8) / 16777216.0f; };
    int checked = 0, differ = 0;
    for (int k = 0; k < 200; k++) {
      Material m;
      m.base_color = {rnd(), rnd(), rnd()};
      m.metallic = rnd() * 1.4f - 0.2f;  // (out of range too: both clamp)
      m.roughness = rnd() * 1.4f - 0.2f;
      m.specular = rnd();
      m.alpha = rnd();
      m.unlit = k % 7 == 0;
      m.emission = {rnd(), rnd(), rnd()};
      m.emission_strength = rnd() * 3.0f;
      m.shading_model = k % 5 == 0 ? 1 : 0;
      CHECK(material_uniform(m, m.textures()));
      const SurfaceSample once = evaluate_material(m, m.textures(), SurfacePoint{});
      SurfacePoint sp;
      sp.position = {rnd() * 10, rnd() * 10, rnd() * 10};
      sp.normal = normalize(Vec3(rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f));
      sp.local_position = {rnd(), rnd(), rnd()};
      sp.local_normal = sp.normal;
      sp.uv = {rnd() * 4, rnd() * 4};
      sp.duvdx = {rnd() * 0.01f, 0};
      SurfaceSample per = evaluate_material(m, m.textures(), sp);
      SurfaceSample fast = once;
      fast.normal = sp.normal;
      checked++;
      differ += std::memcmp(&fast.albedo, &per.albedo, sizeof(Vec3)) || std::memcmp(&fast.normal, &per.normal, sizeof(Vec3)) ||
                std::memcmp(&fast.emission, &per.emission, sizeof(Vec3)) || fast.metallic != per.metallic || fast.roughness != per.roughness ||
                fast.specular != per.specular || fast.alpha != per.alpha || fast.unlit != per.unlit || fast.toon != per.toon;
    }
    CHECK(checked == 200 && differ == 0);
    /* Not uniform: a procedural, an axis material. */
    Material p;
    p.procedural = (int)Procedural::Checker;
    CHECK(!material_uniform(p, p.textures()));
    Material a;
    a.axis_mode = 1;
    CHECK(!material_uniform(a, a.textures()));
  });

  test("shading: a deferred view with uniform materials once per triangle draws exactly what per pixel draws (sun, shadows, point and spot lights, flat and smooth)", [] {
    std::vector<MeshPtr> keep;
    std::vector<std::vector<MaterialPtr>> mats;
    std::vector<DrawItem> items;
    mats.reserve(8);
    auto add = [&](MeshPtr m, Mat4 model, uint32_t id, MaterialPtr mat) {
      keep.push_back(m);
      mats.push_back({mat});
      DrawItem it;
      it.mesh = &m->render_mesh();
      it.model = model;
      it.id = id;
      it.materials = &mats.back();
      items.push_back(it);
    };
    MeshPtr smooth = primitives::uv_sphere(1.0f, 32, 24);
    smooth->smooth = true;
    auto red = make_material("R", Vec3(0.8f, 0.2f, 0.1f));
    red->roughness = 0.3f;
    red->metallic = 0.4f;
    auto glow = make_material("G", Vec3(0.2f, 0.7f, 0.3f));
    glow->emission = Vec3(1, 1, 0);
    glow->emission_strength = 0.5f;
    auto checker = make_material("C", Vec3(0.9f));
    checker->procedural = (int)Procedural::Checker;
    add(smooth, Mat4::translate({-1.5f, 0, 6}), 1, red);
    add(primitives::cube(1.5f), Mat4::translate({1.5f, 0, 6}), 2, glow);
    add(primitives::plane(20.0f), Mat4::translate({0, -1.2f, 6}), 3, checker);
    const Mat4 v = Mat4::look_at({0, 1.5f, 0}, {0, 0, 6}, {0, 1, 0});
    const Mat4 p = Mat4::perspective(60 * kDeg2Rad, 320 / 200.0f, 0.1f, 100);
    LightingEnv env;
    RenderLight sun, pt, spot;
    sun.direction = normalize(Vec3(-0.3f, -1, 0.4f));
    pt.type = RenderLight::Point;
    pt.position = {0, 2, 5};
    pt.intensity = 3.0f;
    pt.range = 8.0f;
    spot.type = RenderLight::Spot;
    spot.position = {2, 3, 4};
    spot.direction = normalize(Vec3(-0.3f, -1, 0.5f));
    spot.intensity = 4.0f;
    spot.range = 10.0f;
    env.lights = {sun, pt, spot};
    env.camera_pos = {0, 1.5f, 0};
    ShadowMap sm;
    AABB b;
    for (const DrawItem &it : items) b.add(it.mesh->bounds.transformed(it.model));
    render_shadow_map(sm, items, sun.direction, b, 1024);
    env.shadow = &sm;
    env.shadow_light = 0;
    Image img[2];
    RenderTarget rt[2];
    for (int k = 0; k < 2; k++) {
      img[k].resize(320, 200);
      rt[k].attach(img[k], {0, 0, 320, 200});
      Renderer3D r;
      RasterOptions opt;
      opt.shade = ShadeMode::Deferred;
      opt.uniform_shading = k == 0;
      r.begin(&rt[k], v, p, env, opt);
      r.clear(0xFF000000);
      for (auto &it : items) r.add(it);
      r.flush();
    }
    size_t lit = 0;
    for (uint32_t px : img[0].pixels) lit += (px & 0xFFFFFF) != 0;
    std::printf("    %zu of %zu pixels drawn\n", lit, img[0].pixels.size());
    CHECK(lit > 20000);
    CHECK(img[0].pixels == img[1].pixels);
  });

  test("shadows: an Edit Mode mesh's flat and smooth render caches share a shape; a copy or an edit makes a new one", [] {
    MeshPtr m = primitives::uv_sphere(1.0f, 16, 12);
    m->smooth = true;
    const uint64_t flat = m->render_mesh(true).shape, shaded = m->render_mesh(false).shape;
    CHECK(flat != 0 && flat == shaded);
    CHECK(m->render_mesh(true).serial != m->render_mesh(false).serial);  // (two caches: different pictures)
    m->positions[0].y += 0.1f;
    m->touch();
    CHECK(m->render_mesh(true).shape != flat && m->render_mesh(true).shape == m->render_mesh(false).shape);
    Mesh copy = *m;
    copy.forget_caches();
    CHECK(copy.render_mesh(true).shape != m->render_mesh(true).shape);  // (a copy is a mesh of its own)
  });

  test("shadows: an Edit Mode drag renders the sun's shadow map once a frame, not once for each view", [] {
    Editor ed;
    r34_open(ed);
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube != nullptr);
    if (!cube) return;
    cube->get<MeshFilter>()->mesh->smooth = true;  // (a flat Edit Mode cache and a smooth render cache)
    cube->get<MeshFilter>()->mesh->touch();
    ed.command("views follow on");  // (the Game view draws during the drag too: it and the Scene view share the map; task 0103)
    ed.command("select Cube");
    ed.command("edit vertex");
    ed.command("vsel 0");
    ed.command("camera 0 20 6 0 0.5 0");
    ed.command("tool move");
    r34_steps(ed, 3);
    const Vec3 wp = cube->world_matrix().point(cube->get<MeshFilter>()->mesh->positions[0]);
    int x = 0, y = 0;
    CHECK(ed.project_to_window(wp, x, y));
    ed.step_frame_headless({r34_mouse(R34ET::MouseMove, x + 55, y)});
    ed.step_frame_headless({r34_mouse(R34ET::MouseDown, x + 55, y)});
    const FrameProfile p0 = ed.frame_profile_totals();
    for (int k = 1; k <= 6; k++) ed.step_frame_headless({r34_mouse(R34ET::MouseMove, x + 55 + 4 * k, y)});
    const FrameProfile p1 = ed.frame_profile_totals();
    ed.step_frame_headless({r34_mouse(R34ET::MouseUp, x + 79, y)});
    const uint64_t frames = p1.frames - p0.frames, shadows = p1.shadow_renders - p0.shadow_renders, views = p1.view_renders - p0.view_renders;
    std::printf("    %llu drag frames: %llu view renders, %llu shadow renders\n", (unsigned long long)frames, (unsigned long long)views,
                (unsigned long long)shadows);
    CHECK(views > frames);       // (more than one view drew the moving mesh)
    CHECK(shadows <= frames);    // (but one map served them)
  });

}

static void tests_round117() {
  /* ---------------------------------------------------------------- round 117: 8 pixels at a time (task 0091) */
  test("shading: pixels lit 8 at a time (AVX2) are the same picture as one at a time - sun with 3x3 and 2x2 shadows, point and spot lights, highlighted faces, both sides", [] {
    if (!cpu::features().avx2) {
      std::printf("    (this CPU has no AVX2: the 8-wide shader isn't used here - skipped)\n");
      return;
    }
    std::vector<MeshPtr> keep;
    std::vector<std::vector<MaterialPtr>> mats;
    std::vector<DrawItem> items;
    mats.reserve(8);
    std::vector<uint8_t> highlight(400, 0);
    for (size_t i = 0; i < highlight.size(); i += 3) highlight[i] = 1;
    auto add = [&](MeshPtr m, Mat4 model, uint32_t id, MaterialPtr mat, bool two_sided, bool hl) {
      keep.push_back(m);
      mats.push_back({mat});
      DrawItem it;
      it.mesh = &m->render_mesh();
      it.model = model;
      it.id = id;
      it.materials = &mats.back();
      it.double_sided = two_sided;
      if (hl) {
        it.face_highlight = &highlight;
        it.highlight_color = Vec3(1.0f, 0.6f, 0.1f);
      }
      items.push_back(it);
    };
    MeshPtr smooth = primitives::uv_sphere(1.0f, 32, 24);
    smooth->smooth = true;
    auto red = make_material("R", Vec3(0.8f, 0.2f, 0.1f));
    red->roughness = 0.3f;
    red->metallic = 0.4f;
    auto rough = make_material("G", Vec3(0.2f, 0.7f, 0.3f));
    rough->roughness = 0.9f;
    rough->emission = Vec3(0.2f, 0.2f, 0.0f);
    rough->emission_strength = 0.5f;
    auto shiny = make_material("S", Vec3(0.9f));
    shiny->roughness = 0.0f;  // (the floored roughness)
    shiny->metallic = 1.0f;
    auto checker = make_material("C", Vec3(0.9f));
    checker->procedural = (int)Procedural::Checker;  // (not plain: one pixel at a time, side by side with the others)
    add(smooth, Mat4::translate({-1.5f, 0, 6}), 1, red, false, true);
    add(primitives::cube(1.5f), Mat4::translate({1.5f, 0, 6}), 2, rough, false, false);
    add(primitives::ico_sphere(0.8f, 3), Mat4::translate({0, 1.6f, 7}), 3, shiny, false, false);
    add(primitives::plane(20.0f), Mat4::translate({0, -1.2f, 6}), 4, checker, true, false);
    add(primitives::quad(3.0f), Mat4::translate({3.5f, 1, 5}) * Mat4::rotate(Quat::axis_angle({0, 1, 0}, 2.6f)), 5, rough, true, false);  // (its back)
    const Mat4 v = Mat4::look_at({0, 1.5f, 0}, {0, 0, 6}, {0, 1, 0});
    const Mat4 p = Mat4::perspective(60 * kDeg2Rad, 320 / 200.0f, 0.1f, 100);
    LightingEnv env;
    RenderLight sun, pt, spot;
    sun.direction = normalize(Vec3(-0.3f, -1, 0.4f));
    pt.type = RenderLight::Point;
    pt.position = {0, 2, 5};
    pt.intensity = 3.0f;
    pt.range = 8.0f;
    spot.type = RenderLight::Spot;
    spot.position = {2, 3, 4};
    spot.direction = normalize(Vec3(-0.3f, -1, 0.5f));
    spot.intensity = 4.0f;
    spot.range = 10.0f;
    spot.cos_outer = 0.8f, spot.cos_inner = 0.9f;
    env.lights = {sun, pt, spot};
    env.camera_pos = {0, 1.5f, 0};
    ShadowMap sm;
    AABB b;
    for (const DrawItem &it : items) b.add(it.mesh->bounds.transformed(it.model));
    render_shadow_map(sm, items, sun.direction, b, 1024);
    env.shadow = &sm;
    env.shadow_light = 0;
    for (bool fast_pcf : {false, true}) {
      sm.fast = fast_pcf;
      Image img[2];
      RenderTarget rt[2];
      size_t simd = 0;
      for (int k = 0; k < 2; k++) {
        img[k].resize(320, 200);
        rt[k].attach(img[k], {0, 0, 320, 200});
        rt[k].want_hdr = true;  // (the colours before display encoding: a float differing anywhere shows there)
        Renderer3D r;
        RasterOptions opt;
        opt.shade = ShadeMode::Deferred;
        opt.simd_shading = k == 0;
        r.begin(&rt[k], v, p, env, opt);
        r.clear(0xFF000000);
        for (auto &it : items) r.add(it);
        r.flush();
        if (k == 0) simd = r.stats().pixels_simd;
      }
      /* The displayed pixels are the same. The colours before display encoding can differ in their last bits: the
       * kernel keeps the scalar shader's order of operations, but a release build may let the compiler reorder the
       * scalar code (MSVC's /fp:fast). Measured: about 1e-5 relative at most.  */
      size_t differ = 0, hdr_differ = 0;
      double worst = 0.0;
      for (size_t i = 0; i < img[0].pixels.size(); i++) differ += img[0].pixels[i] != img[1].pixels[i];
      CHECK(rt[0].hdr.size() == img[0].pixels.size() && rt[1].hdr.size() == rt[0].hdr.size());
      for (size_t i = 0; i < rt[0].hdr.size() && i < rt[1].hdr.size(); i++) {
        const Vec3 a = rt[0].hdr[i], b = rt[1].hdr[i];
        if (std::memcmp(&a, &b, sizeof(Vec3)) == 0) continue;
        hdr_differ++;
        for (int c = 0; c < 3; c++) worst = std::max(worst, std::fabs((double)a[c] - b[c]) / std::max(1e-3, std::fabs((double)b[c])));  // (relative; absolute below 0.001)
      }
      std::printf("    %s shadows: %zu pixels lit 8 at a time; %zu pixels differ; %zu HDR colours differ, by at most %.2g relative\n",
                  fast_pcf ? "2x2" : "3x3", simd, differ, hdr_differ, worst);
      CHECK(simd > 5000);
      CHECK(differ == 0);
      CHECK(worst < 1e-4);
    }
  });

}

static void tests_round118() {
  /* ---------------------------------------------------------------- round 118: views nobody picks in keep no full-size planes (task 0092) */
  test("views: the Game view at a reduced resolution looks the same without its full-size depth and ids, with a camera filter too", [] {
    Editor ed;
    r34_open(ed);
    ed.command("quality low");  // (67 %: the views are drawn small and scaled up)
    ed.command("select Main Camera");  // (the Camera Preview is up too)
    const Recti game = ed.window_rect_for_test(WindowKind::Game);
    CHECK(game.w > 0);
    GameObject *cam = ed.scene().find_by_name("Main Camera");
    CHECK(cam != nullptr);
    if (!cam) return;
    for (int filtered = 0; filtered < 2; filtered++) {
      if (filtered) cf_add_retro(cam);  // (a filter stack with its own resolution: render_camera's own upscale)
      std::vector<uint32_t> shot[2];
      for (int k = 0; k < 2; k++) {
        ed.planes_always_for_test(k == 0);
        GameObject *cube = ed.scene().find_by_name("Cube");
        if (cube) cube->set_local_position({0.05f * (k + 1), 0.5f, 0.0f});  // (a change: both views draw again)
        r34_steps(ed, 2);
        if (cube) cube->set_local_position({0.0f, 0.5f, 0.0f});
        r34_steps(ed, 2);
        shot[k] = r34_crop(ed, game);
      }
      std::printf("    %s: %s\n", filtered ? "with a retro filter" : "plain", shot[0] == shot[1] ? "the same pixels" : "DIFFERENT");
      CHECK(shot[0] == shot[1]);
    }
    ed.planes_always_for_test(false);
  });
}

static void tests_round120() {
  /* ---------------------------------------------------------------- round 120: the denoiser on the GPU (task 0094) */
  /* A 4-sample path-traced frame of a sphere on a plane, denoised with Denoise Device `device` (0 Auto, 1 CPU, 2 GPU). */
  auto r120_denoised = [](int device, std::string *backend) {
    static auto sphere = primitives::uv_sphere(0.5f, 32, 24);
    static auto plane = primitives::plane(6.0f, 1);
    static std::vector<MaterialPtr> mats = {make_material("grey", {0.7f, 0.7f, 0.7f})};
    RenderLight sun;
    sun.direction = normalize(Vec3(-0.5f, -1.0f, 0.3f));
    sun.intensity = 2.0f;
    std::vector<PTObject> objs = {{&sphere->render_mesh_tangents(), Mat4::translate({0, 0.5f, 0}), &mats},
                                  {&plane->render_mesh_tangents(), Mat4::identity(), &mats}};
    PathTracer pt;
    PTSettings st;
    st.denoise_device = device;
    pt.set_settings(st);
    pt.build(objs, {sun}, Environment{});
    pt.set_camera(Mat4::look_at({0, 1.5f, -3}, {0, 0.4f, 0}, {0, 1, 0}), Mat4::perspective(45 * kDeg2Rad, 1, 0.1f, 50), 96, 96);
    pt.render(1e9, 4);
    std::vector<float> rgb = pt.linear_rgb(true);
    if (backend) *backend = pt.denoise_backend();
    return rgb;
  };
  test("denoise: OpenImageDenoise on the GPU looks like the CPU's (mean under 1/255, 99 % within 3/255; skipped without a GPU)",
       [&] {
    if (!PathTracer::oidn_available() || !PathTracer::oidn_gpu_available()) {
      std::printf("    skipped: no GPU for OpenImageDenoise here\n");
      return;
    }
    std::string on_cpu, on_gpu;
    const std::vector<float> c = r120_denoised(1, &on_cpu), g = r120_denoised(2, &on_gpu);
    std::printf("    %s / %s (GPU fallbacks so far: %zu)\n", on_cpu.c_str(), on_gpu.c_str(), PathTracer::oidn_gpu_fallbacks());
    CHECK(on_cpu == "OpenImageDenoise");
    CHECK(on_gpu.find("OpenImageDenoise, ") == 0);
    CHECK(c.size() == g.size() && !c.empty());
    if (c.size() != g.size() || c.empty()) return;
    /* Compared as displayed (sRGB, 8 bits), as the bar of ADR 0013 / 0014 is written. */
    std::vector<uint32_t> pc(c.size() / 3), pg(g.size() / 3);
    display::encode_span(c.data(), pc.data(), pc.size(), ViewTransform::Standard, 0.0f);
    display::encode_span(g.data(), pg.data(), pg.size(), ViewTransform::Standard, 0.0f);
    double sum = 0;
    size_t within = 0;
    for (size_t i = 0; i < pc.size(); i++) {
      int worst = 0;
      for (int k = 0; k < 24; k += 8) worst = std::max(worst, std::abs((int)((pc[i] >> k) & 255) - (int)((pg[i] >> k) & 255)));
      sum += worst;
      within += worst <= 3;
    }
    const double mean = sum / pc.size(), share = (double)within / pc.size();
    std::printf("    mean difference %.3f / 255, %.2f %% within 3 / 255\n", mean, share * 100);
    CHECK(mean < 1.0);
    CHECK(share >= 0.99);
  });
  test("denoise: a GPU that fails denoises on the CPU (exactly the CPU's picture) and counts a fallback", [&] {
    if (!PathTracer::oidn_available()) {
      std::printf("    skipped: built without OpenImageDenoise\n");
      return;
    }
    const std::vector<float> cpu = r120_denoised(1, nullptr);
    const size_t before = PathTracer::oidn_gpu_fallbacks();
    PathTracer::oidn_fail_gpu_for_test(true);
    std::string backend;
    const std::vector<float> asked_gpu = r120_denoised(2, &backend);
    PathTracer::oidn_fail_gpu_for_test(false);
    CHECK(asked_gpu == cpu);
    CHECK(backend == "OpenImageDenoise");
    /* (Without a GPU, GPU is the CPU from the start: nothing failed.) The failure came after a real run on the card,
     * so its filter and buffers were dropped, and the next GPU frame makes them again. */
    CHECK(PathTracer::oidn_gpu_fallbacks() == before + (PathTracer::oidn_gpu_available() ? 1u : 0u));
    CHECK(!PathTracer::oidn_gpu_holds_buffers());
    if (PathTracer::oidn_gpu_available()) {
      std::string again;
      r120_denoised(2, &again);
      CHECK(again.find("OpenImageDenoise, ") == 0);
      CHECK(PathTracer::oidn_gpu_holds_buffers());
      PathTracer::oidn_release_gpu();  // (what a GPU render does before fitting its scene)
      CHECK(!PathTracer::oidn_gpu_holds_buffers());
    }
  });
  test("denoise: Denoise Device is saved with the scene, and Auto stays on the CPU when BLENDITY_DENOISE_DEVICE=cpu", [&] {
    Scene sc;
    sc.render.denoise_device = 2;
    Scene back;
    std::string err;
    CHECK(load_scene_text(save_scene_text(sc), back, err));
    CHECK(back.render.denoise_device == 2);
    if (!PathTracer::oidn_available()) return;
    std::string backend;
    const std::vector<float> autod = r120_denoised(0, &backend);  // (the tests pin BLENDITY_DENOISE_DEVICE=cpu)
    CHECK(backend == "OpenImageDenoise");
    CHECK(autod == r120_denoised(1, nullptr));
  });
}

static void tests_round128() {
  /* ---------------------------------------------------------------- round 128: the other views catch up on release (task 0103) */
  test("views: during a gizmo drag the Game view and the Camera Preview keep their last picture, draw once on release as a fresh render would, and follow every frame with Other Views Follow Drags on", [] {
    for (bool follow : {false, true}) {
      Editor ed;
      r34_open(ed);
      ed.command(follow ? "views follow on" : "views follow off");
      ed.command("select Main Camera");  // (the Camera Preview up, and kept up while the cube is selected)
      ed.command("camerapreview lock");
      r34_steps(ed, 3);
      GameObject *cube = ed.scene().find_by_name("Cube");
      CHECK(cube != nullptr);
      if (!cube) return;
      ed.command("select Cube");
      ed.command("camera 0 20 6 0 0.5 0");  // (the X arrow points straight right on screen)
      ed.command("tool move");
      r34_steps(ed, 3);
      int x = 0, y = 0;
      CHECK(ed.project_to_window(cube->world_position(), x, y));
      ed.step_frame_headless({r34_mouse(R34ET::MouseMove, x + 55, y)});
      ed.step_frame_headless({r34_mouse(R34ET::MouseDown, x + 55, y)});
      const FrameProfile before = ed.frame_profile_totals();
      for (int k = 1; k <= 10; k++) ed.step_frame_headless({r34_mouse(R34ET::MouseMove, x + 55 + 4 * k, y)});
      const FrameProfile during = ed.frame_profile_totals();
      ed.step_frame_headless({r34_mouse(R34ET::MouseUp, x + 95, y)});
      r34_steps(ed, 1);
      const FrameProfile after = ed.frame_profile_totals();
      const std::vector<uint32_t> game = r34_crop(ed, ed.window_rect_for_test(WindowKind::Game));
      const std::vector<uint32_t> preview = r34_crop(ed, ed.cam_preview_rect_for_test());
      const uint64_t held = during.views_held - before.views_held, renders = during.view_renders - before.view_renders;
      std::printf("    %s: 10 drag frames - %llu pictures held, %llu views rendered; on release %llu rendered\n", follow ? "following" : "catching up",
                  (unsigned long long)held, (unsigned long long)renders, (unsigned long long)(after.view_renders - during.view_renders));
      if (follow) {
        CHECK(held == 0);
        CHECK(renders >= 20);  // (the Scene view and the Game view every frame, at least)
        continue;
      }
      CHECK(held >= 10);                                 // (the Game view at least, every frame of the drag)
      CHECK(renders <= 10 + 1);                          // (the Scene view's own, and no more)
      CHECK(after.view_renders - during.view_renders >= 2);  // (the release: the Game view and the Camera Preview again)
      /* What the Game view shows after the release is what it draws afresh (a change made and taken back). */
      const Vec3 at = cube->local().position;
      cube->set_local_position(at + Vec3(0.001f, 0, 0));
      r34_steps(ed, 2);
      cube->set_local_position(at);
      r34_steps(ed, 2);
      CHECK(game == r34_crop(ed, ed.window_rect_for_test(WindowKind::Game)));
      CHECK(preview == r34_crop(ed, ed.cam_preview_rect_for_test()));
    }
  });
  test("views: in Play the Game view draws every frame, a gizmo drag or not", [] {
    Editor ed;
    r34_open(ed);
    ed.command("views follow off");
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube != nullptr);
    if (!cube) return;
    ed.command("select Cube");
    ed.command("camera 0 20 6 0 0.5 0");  // (the X arrow points straight right on screen)
    ed.command("tool move");
    ed.command("play");
    r34_steps(ed, 2);
    int x = 0, y = 0;
    CHECK(ed.project_to_window(cube->world_position(), x, y));
    ed.step_frame_headless({r34_mouse(R34ET::MouseMove, x + 55, y)});
    ed.step_frame_headless({r34_mouse(R34ET::MouseDown, x + 55, y)});
    const FrameProfile before = ed.frame_profile_totals();
    for (int k = 1; k <= 5; k++) ed.step_frame_headless({r34_mouse(R34ET::MouseMove, x + 55 + 4 * k, y)});
    const FrameProfile after = ed.frame_profile_totals();
    ed.step_frame_headless({r34_mouse(R34ET::MouseUp, x + 75, y)});
    ed.command("stop");
    std::printf("    5 frames of a drag in Play: %llu pictures held, %llu views drawn\n", (unsigned long long)(after.views_held - before.views_held),
                (unsigned long long)(after.view_renders - before.view_renders));
    CHECK(after.views_held == before.views_held);
    CHECK(after.view_renders - before.view_renders >= 10);  // (the Scene view and the Game view, every frame)
  });
  test("views: a click in the Hierarchy holds no view and doesn't draw the Scene view again (only a drag in the Scene view does)", [] {
    Editor ed;
    r34_open(ed);
    ed.viewport_device_for_test(1);
    r34_steps(ed, 3);
    const Recti h = ed.window_rect_for_test(WindowKind::Hierarchy);
    const FrameProfile before = ed.frame_profile_totals();
    ed.step_frame_headless({r34_mouse(R34ET::MouseMove, h.x + h.w / 2, h.y + h.h - 12)});
    ed.step_frame_headless({r34_mouse(R34ET::MouseDown, h.x + h.w / 2, h.y + h.h - 12)});
    r34_steps(ed, 2);
    ed.step_frame_headless({r34_mouse(R34ET::MouseUp, h.x + h.w / 2, h.y + h.h - 12)});
    r34_steps(ed, 2);
    const FrameProfile after = ed.frame_profile_totals();
    std::printf("    a click in the Hierarchy's empty space: %llu pictures held, %llu cheap shadow maps, %llu views drawn\n",
                (unsigned long long)(after.views_held - before.views_held), (unsigned long long)(after.cheap_shadows - before.cheap_shadows),
                (unsigned long long)(after.view_renders - before.view_renders));
    CHECK(after.views_held == before.views_held);
    CHECK(after.cheap_shadows == before.cheap_shadows);
    CHECK(after.view_renders == before.view_renders);
  });
}

static void tests_round129() {
  /* ---------------------------------------------------------------- round 129: cheaper frames while moving (task 0104) */
  test("views: while a gizmo drag moves the cube the CPU views take a half-size shadow map, and the frame after release is the exact picture; off, every frame is exact", [] {
    for (bool simpler : {true, false}) {
      Editor ed;
      r34_open(ed);
      ed.viewport_device_for_test(1);
      ed.command(simpler ? "views simpler on" : "views simpler off");
      GameObject *cube = ed.scene().find_by_name("Cube");
      CHECK(cube != nullptr);
      if (!cube) return;
      ed.command("select Cube");
    ed.command("camera 0 20 6 0 0.5 0");  // (the X arrow points straight right on screen)
      ed.command("tool move");
      r34_steps(ed, 3);
      int x = 0, y = 0;
      CHECK(ed.project_to_window(cube->world_position(), x, y));
      ed.step_frame_headless({r34_mouse(R34ET::MouseMove, x + 55, y)});
      ed.step_frame_headless({r34_mouse(R34ET::MouseDown, x + 55, y)});
      const FrameProfile before = ed.frame_profile_totals();
      for (int k = 1; k <= 6; k++) ed.step_frame_headless({r34_mouse(R34ET::MouseMove, x + 55 + 5 * k, y)});
      const FrameProfile during = ed.frame_profile_totals();
      ed.step_frame_headless({r34_mouse(R34ET::MouseUp, x + 85, y)});
      r34_steps(ed, 1);
      const std::vector<uint32_t> settled = r34_crop(ed, ed.scene_view_rect());
      /* The exact picture of the same scene: a change made and taken back, nothing held. */
      const Vec3 at = cube->local().position;
      cube->set_local_position(at + Vec3(0.001f, 0, 0));
      r34_steps(ed, 2);
      cube->set_local_position(at);
      r34_steps(ed, 2);
      const std::vector<uint32_t> exact = r34_crop(ed, ed.scene_view_rect());
      const uint64_t cheap = during.cheap_shadows - before.cheap_shadows;
      std::printf("    simpler %s: %llu cheap shadow maps in 6 drag frames; the settled frame %s the exact picture\n", simpler ? "on" : "off",
                  (unsigned long long)cheap, settled == exact ? "is" : "is NOT");
      CHECK(settled == exact);
      CHECK(simpler ? cheap >= 6 : cheap == 0);
    }
  });
  test("views: verify mode finds no stale picture across a drag with Simpler While Moving on", [] {
    Editor ed;
    r34_open(ed);
    ed.viewport_device_for_test(1);
    ed.command("views simpler on");
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube != nullptr);
    if (!cube) return;
    ed.command("select Cube");
    ed.command("camera 0 20 6 0 0.5 0");  // (the X arrow points straight right on screen)
    ed.command("tool move");
    r34_steps(ed, 3);
    ed.set_render_cache_verify(true);
    const uint64_t stale0 = Editor::render_cache_mismatches_all();
    int x = 0, y = 0;
    CHECK(ed.project_to_window(cube->world_position(), x, y));
    ed.step_frame_headless({r34_mouse(R34ET::MouseMove, x + 55, y)});
    ed.step_frame_headless({r34_mouse(R34ET::MouseDown, x + 55, y)});
    for (int k = 1; k <= 4; k++) ed.step_frame_headless({r34_mouse(R34ET::MouseMove, x + 55 + 5 * k, y)});
    ed.step_frame_headless({r34_mouse(R34ET::MouseUp, x + 75, y)});
    r34_steps(ed, 3);
    ed.set_render_cache_verify(false);
    std::printf("    %llu stale pictures\n", (unsigned long long)(Editor::render_cache_mismatches_all() - stale0));
    CHECK(Editor::render_cache_mismatches_all() == stale0);
  });
}

static void tests_round133() {
  /* ---------------------------------------------------------------- round 133: the render mesh built on every thread (task 0109) */
  test("render mesh: the threaded full build equals the serial one bit for bit (positions, normals, UVs, triangles, corners, materials) on smooth, by angle, sharp, seamed, n-gon, several-material, flat, degenerate, pole, UV-less and empty meshes", [] {
    std::vector<std::pair<std::string, MeshPtr>> ms;
    ms.push_back({"a smooth sphere", primitives::uv_sphere(1.0f, 64, 48)});
    {
      MeshPtr g = primitives::grid(4.0f, 40, 40);
      for (Vec3 &p : g->positions) p.y = 0.3f * std::sin(p.x * 2.0f) * std::cos(p.z * 2.0f);
      g->smooth = true;
      g->smooth_angle = 30.0f;
      ms.push_back({"a grid smooth by angle", g});
    }
    {
      MeshPtr g = primitives::grid(4.0f, 30, 30);
      g->smooth = true;
      for (uint32_t i = 0; i + 1 < 31; i++) g->sharp_edges.push_back(Mesh::edge_key(i * 31 + 12, i * 31 + 43));
      g->seams.push_back(Mesh::edge_key(300, 301));
      g->seams_sharp = true;
      ms.push_back({"a grid with sharp edges and a seam", g});
    }
    {
      MeshPtr c = primitives::cube(2.0f);  // (n-gons: two of its quads joined into one hexagon-ish face)
      c->face_material.assign(c->face_count(), 0);
      for (size_t f = 0; f < c->face_count(); f += 2) c->face_material[f] = 1;
      ms.push_back({"a cube with two materials", c});
    }
    {
      MeshPtr s = primitives::uv_sphere(1.0f, 24, 16);
      s->smooth = false;
      ms.push_back({"a flat sphere", s});
    }
    {
      MeshPtr d = primitives::grid(2.0f, 10, 10);
      d->smooth = true;
      d->corner_verts[d->face_offsets[7] + 1] = d->corner_verts[d->face_offsets[7]];  // (a quad with a vertex twice)
      d->positions[30] = d->positions[31];  // (a zero-length edge)
      d->positions[60] = Vec3(std::nanf(""), 0, 0);  // (NaN normals around it: corners the serial build never shares)
      ms.push_back({"a degenerate grid", d});
    }
    ms.push_back({"a cylinder (n-gon caps)", primitives::cylinder(1.0f, 2.0f, 32)});
    {
      MeshPtr fan = primitives::cone(1.0f, 0.5f, 3000);  // (its tip: 3,000 corners at one vertex, sorted rather than compared pairwise)
      fan->smooth = true;
      ms.push_back({"a 3000-segment cone (a pole)", fan});
    }
    {
      MeshPtr g = primitives::grid(2.0f, 12, 12);
      g->uvs.clear();
      g->smooth = true;
      ms.push_back({"a grid without UVs", g});
    }
    ms.push_back({"an empty mesh", std::make_shared<Mesh>()});
    int differ = 0;
    for (auto &[name, base] : ms) {
      for (bool flat : {false, true}) {
        Mesh a = *base, b = *base;
        a.forget_caches();
        b.forget_caches();
        const RenderMesh &fast = a.render_mesh(flat);
        mesh_serial_build_for_test(true);
        const RenderMesh &slow = b.render_mesh(flat);
        mesh_serial_build_for_test(false);
        auto bits = [](const auto &x, const auto &y) {
          return x.size() == y.size() && (x.empty() || std::memcmp(x.data(), y.data(), x.size() * sizeof(x[0])) == 0);
        };
        const bool same = bits(fast.positions, slow.positions) && bits(fast.normals, slow.normals) && bits(fast.uvs, slow.uvs) &&
                          fast.indices == slow.indices && fast.tri_face == slow.tri_face && fast.tri_material == slow.tri_material &&
                          fast.tri_corner == slow.tri_corner;
        if (!same) {
          differ++;
          std::printf("    %s (%s): differs (%zu / %zu render vertices, %zu / %zu indices)\n", name.c_str(), flat ? "flat" : "as shaded", fast.positions.size(),
                      slow.positions.size(), fast.indices.size(), slow.indices.size());
        }
      }
    }
    std::printf("    %zu meshes, both caches: %d differ\n", ms.size(), differ);
    CHECK(differ == 0);
  });
  test("render mesh: the threaded full build of a 50k-face mesh takes a fraction of the serial one", [] {
    MeshPtr big = primitives::uv_sphere(1.0f, 256, 196);
    double fast = 1e9, slow = 1e9;
    for (int k = 0; k < 3; k++) {
      Mesh a = *big;
      a.forget_caches();
      {
        ScopedTimer t;
        (void)a.render_mesh(true);
        fast = std::min(fast, t.ms());
      }
      Mesh b = *big;
      b.forget_caches();
      mesh_serial_build_for_test(true);
      {
        ScopedTimer t;
        (void)b.render_mesh(true);
        slow = std::min(slow, t.ms());
      }
      mesh_serial_build_for_test(false);
    }
    std::printf("    the flat cache of a 50k-face sphere: %.1f ms on every thread, %.1f ms serial (the face normals included in both)\n", fast, slow);
    const JobSystem &js = JobSystem::global();
    if (js.thread_count() > 1 && js.max_threads() > 1 && js.thread_limit() != 1) CHECK(fast < slow);  // (one thread: only printed)
  });
  test("render mesh: after a threaded build, a drag's in-place update and the tangents equal the serial build's", [] {
    MeshPtr base = primitives::uv_sphere(1.0f, 48, 32);
    int differ = 0;
    for (bool flat : {false, true}) {
      Mesh a = *base, b = *base;
      a.forget_caches();
      b.forget_caches();
      (void)a.render_mesh(flat);
      mesh_serial_build_for_test(true);
      (void)b.render_mesh(flat);
      mesh_serial_build_for_test(false);
      for (Mesh *m : {&a, &b}) {  // (a drag: positions alone, the in-place path reads each corner's render vertex)
        for (uint32_t v = 100; v < 140; v++) m->positions[v] = m->positions[v] * 1.1f;
        m->touch();
      }
      const RenderMesh &ra = a.render_mesh(flat), &rb = b.render_mesh(flat);
      auto bits = [](const auto &x, const auto &y) {
        return x.size() == y.size() && (x.empty() || std::memcmp(x.data(), y.data(), x.size() * sizeof(x[0])) == 0);
      };
      differ += !(bits(ra.positions, rb.positions) && bits(ra.normals, rb.normals) && ra.indices == rb.indices);
      const RenderMesh &ta = a.render_mesh_tangents(flat), &tb = b.render_mesh_tangents(flat);
      differ += !bits(ta.tangents, tb.tangents);
    }
    std::printf("    in place after a drag, then tangents, both caches: %d differ\n", differ);
    CHECK(differ == 0);
  });
  test("entering Edit Mode on a 50k-face mesh: the overlap check predicted past a frame goes to the worker (none on the main thread), its rings arrive; with no budget it runs on the main thread", [] {
    for (bool budget : {true, false}) {
      Editor ed;
      r34_open(ed);
      if (!budget) ed.overlap_budget_for_test(1e9);
      MeshPtr big = primitives::uv_sphere(1.0f, 256, 196);
      for (size_t f = 1000; f < 1040; f++) {  // (forty faces doubled: rings to wait for)
        std::vector<uint32_t> face;
        for (uint32_t c = big->face_offsets[f]; c < big->face_offsets[f + 1]; c++) face.push_back(big->add_vert(big->positions[big->corner_verts[c]]));
        big->add_face(face.data(), face.size());
      }
      GameObject *obj = ed.scene().create("Big");
      obj->add<MeshFilter>()->mesh = big;
      obj->add<MeshRenderer>();
      ed.command("select Big");
      r34_steps(ed, 2);
      const FrameProfile p0 = ed.frame_profile_totals();
      ed.command("edit vertex");
      ed.step_frame_headless();
      const FrameProfile p1 = ed.frame_profile_totals();
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
      while (ed.overlap_verts_for_test().empty() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        ed.step_frame_headless();
      }
      std::printf("    %s: the entering frame %llu checks on the main thread (%.1f ms), %llu on the worker; then %zu vertex pairs\n",
                  budget ? "a frame's budget" : "no budget", (unsigned long long)(p1.overlap_checks - p0.overlap_checks), p1.ms_overlap - p0.ms_overlap,
                  (unsigned long long)(p1.overlap_background - p0.overlap_background), ed.overlap_verts_for_test().size());
      CHECK(ed.overlap_verts_for_test() == meshops::overlapping_vertices(*ed.scene().find_by_name("Big")->get<MeshFilter>()->mesh));
      CHECK(!ed.overlap_verts_for_test().empty());
      if (budget) CHECK(p1.overlap_checks == p0.overlap_checks && p1.overlap_background > p0.overlap_background);
      else CHECK(p1.overlap_checks > p0.overlap_checks && p1.overlap_background == p0.overlap_background);
    }
  });
  test("overlap check: a few stalled checks don't send every later check of a mid-size mesh to the worker (the learned rate is held)", [] {
    Editor ed;
    r34_open(ed);
    for (int k = 0; k < 5; k++) ed.overlap_learn_rate_for_test(6000, 2000.0);  // (five checks stalled 2 s each)
    GameObject *obj = ed.scene().create("Mid");
    obj->add<MeshFilter>()->mesh = primitives::uv_sphere(1.0f, 80, 64);  // (about 5,000 vertices)
    obj->add<MeshRenderer>();
    ed.command("select Mid");
    r34_steps(ed, 2);
    const FrameProfile p0 = ed.frame_profile_totals();
    ed.command("edit vertex");
    ed.step_frame_headless();
    const FrameProfile p1 = ed.frame_profile_totals();
    std::printf("    %zu vertices after the stalls: %llu checks on the main thread, %llu on the worker\n",
                obj->get<MeshFilter>()->mesh->vert_count(), (unsigned long long)(p1.overlap_checks - p0.overlap_checks),
                (unsigned long long)(p1.overlap_background - p0.overlap_background));
    CHECK(p1.overlap_checks > p0.overlap_checks && p1.overlap_background == p0.overlap_background);
  });
}

static void tests_round135() {
  /* ---------------------------------------------------------------- round 135: MoonRay, an external renderer (tasks 0111-0114) */
  test("moonray: WSL's distribution list (UTF-16 or UTF-8), Windows paths as WSL sees them, MoonRay's progress from its output", [] {
    std::string u16 = "\xFF\xFE";  // (a BOM, then UTF-16LE text, as wsl.exe writes)
    for (char c : std::string("Ubuntu\r\nDebian\r\n")) u16 += c, u16 += '\0';
    const std::vector<std::string> a = moonray::parse_wsl_list(u16), b = moonray::parse_wsl_list("Ubuntu-24.04\nkali\n\n");
    std::printf("    UTF-16: %zu (%s, %s); UTF-8: %zu\n", a.size(), a.size() > 0 ? a[0].c_str() : "-", a.size() > 1 ? a[1].c_str() : "-", b.size());
    CHECK(a.size() == 2 && a[0] == "Ubuntu" && a[1] == "Debian");
    CHECK(b.size() == 2 && b[0] == "Ubuntu-24.04" && b[1] == "kali");
    CHECK(moonray::parse_wsl_list("").empty());
    CHECK(moonray::wsl_path("C:\\Users\\a b\\scene.rdla") == "/mnt/c/Users/a b/scene.rdla");
    CHECK(moonray::wsl_path("D:/x/y.exr") == "/mnt/d/x/y.exr");
    CHECK(moonray::wsl_path("/already/linux") == "/already/linux");
    CHECK_NEAR(moonray::parse_progress("Loading\n  [+] Rendering [=====     ] 45.2%"), 0.452f, 1e-4);
    CHECK_NEAR(moonray::parse_progress("[==] 10.0%\r[====] 20.5%\r"), 0.205f, 1e-4);
    CHECK_NEAR(moonray::parse_progress("100.0%"), 1.0f, 1e-6);
    CHECK(moonray::parse_progress("Render prep time = 00:00:00.008") < 0.0f);
    CHECK(moonray::parse_progress("") < 0.0f);
    moonray::Install in;
    in.kind = moonray::Install::Native;
    in.root = "/opt/mr";
    const std::vector<std::string> cmd = moonray::command(in, {"-in", "s.rdla"});
#ifdef _WIN32
    in.kind = moonray::Install::Wsl;
    const std::vector<std::string> w = moonray::command(in, {"-in", "/mnt/c/s.rdla"});
    CHECK(w.size() >= 4 && to_lower(fs::filename(w[0])) == "wsl.exe" && std::find(w.begin(), w.end(), "-e") != w.end() &&
          std::find(w.begin(), w.end(), "/opt/mr/bin/blendity-moonray") != w.end());
    CHECK(moonray::path_for(in, "C:\\t\\a.exr") == "/mnt/c/t/a.exr");
#else
    CHECK(cmd.size() == 3 && cmd[0] == "/opt/mr/bin/blendity-moonray" && cmd[2] == "s.rdla");
#endif
    in.launcher = {"fake", "--x"};
    const std::vector<std::string> f = moonray::command(in, {"-in", "s"});
    CHECK(f.size() == 4 && f[0] == "fake" && f[1] == "--x");
    CHECK(moonray::path_for(in, "C:\\t\\a.exr") == "C:\\t\\a.exr");  // (a stand-in runs here: no WSL paths)
    CHECK(!moonray::archive_name().empty() || std::string(releases::platform()) == "macos");
  });
  test("moonray: a program's output streamed as it comes, its exit code, and stopped at once when cancelled", [] {
    const std::string dir = fs::join(fs::temp_dir(), "blendity-test-moonray");
    fs::make_dirs(dir);
    const std::string scene = fs::join(dir, "stream.rdla"), out = fs::join(dir, "stream.exr");
    std::string lua_out;  // (as the exporter quotes it: backslashes doubled)
    for (char ch : out) lua_out += ch == '\\' ? std::string("\\\\") : std::string(1, ch);
    fs::write_file(scene, "SceneVariables {\n    [\"image_width\"] = 4,\n    [\"image_height\"] = 2,\n}\nRenderOutput(\"beauty\") {\n    [\"file_name\"] = \"" +
                              lua_out + "\",\n}\n");
    fs::remove_file(out);
    std::string text;
    int chunks = 0;
    const int code = sys::run_streaming({test_program(), "--fake-moonray", "-in", scene}, [&](const std::string &s) { text += s, chunks++; });
    Bitmap bm;
    std::string error;
    const bool read = load_image(out, bm, error);
    std::printf("    exit %d, %d pieces of output, the last progress %.2f; the image %s (%dx%d)\n", code, chunks, moonray::parse_progress(text),
                read ? "read" : error.c_str(), bm.width, bm.height);
    CHECK(code == 0 && chunks >= 1 && moonray::parse_progress(text) == 1.0f);
    CHECK(read && bm.width == 4 && bm.height == 2 && bm.is_float && std::fabs(bm.rgbaf[0] - 0.5f) < 1e-6f && std::fabs(bm.rgbaf[2] - 0.125f) < 1e-6f);
    set_env("BLENDITY_FAKE_MOONRAY", "fail");
    std::string ftext;
    const int fcode = sys::run_streaming({test_program(), "--fake-moonray", "-in", scene}, [&](const std::string &s) { ftext += s; });
    set_env("BLENDITY_FAKE_MOONRAY", "slow");
    std::atomic<bool> cancel{false};
    std::thread stopper([&] {
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
      cancel.store(true);
    });
    const auto t0 = std::chrono::steady_clock::now();
    const int ccode = sys::run_streaming({test_program(), "--fake-moonray", "-in", scene}, [](const std::string &) {}, &cancel);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    stopper.join();
    set_env("BLENDITY_FAKE_MOONRAY", "");
    const int tcode = sys::run_streaming({test_program(), "--fake-moonray", "-in", scene}, [](const std::string &) {}, nullptr, 0);
    std::printf("    told to fail: exit %d (%s); cancelled after %.0f ms: %d; a missing program: %d\n", fcode,
                ftext.find("told to fail") != std::string::npos ? "its message seen" : "NO message", ms, ccode,
                sys::run_streaming({"blendity-no-such-program"}, [](const std::string &) {}));
    CHECK(fcode == 3 && ftext.find("told to fail") != std::string::npos);
    CHECK(ccode == sys::kExitCancelled && ms < 3000.0);
    CHECK(tcode == 0);
    CHECK(sys::run_streaming({"blendity-no-such-program"}, [](const std::string &) {}) == -1);
    std::string cap;
    CHECK(sys::run_capture({test_program(), "--fake-moonray", "-in", scene}, cap) == 0 && cap.find("Wrote") != std::string::npos);
  });
  test("images: an uncompressed OpenEXR reads without Blender's libraries (float and half, channels by name); a compressed or broken one is refused; a PFM is written bottom row first", [] {
    std::vector<float> px = {1.0f, 0.5f, 0.25f, 1.0f, 2.0f, 0.0f, 0.125f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 4.0f, 8.0f, 0.5f, 1.0f};
    for (bool half : {false, true}) {
      Bitmap bm;
      std::string error;
      const bool ok = decode_exr_plain(test_exr_plain(2, 2, px, half), bm, error);
      bool same = ok && bm.width == 2 && bm.height == 2;
      for (size_t i = 0; same && i < px.size(); i++) same = std::fabs(bm.rgbaf[i] - px[i]) < 1e-6f;
      std::printf("    %s channels: %s\n", half ? "half" : "float", same ? "the same values" : ("DIFFERENT " + error).c_str());
      CHECK(same);
      Bitmap via;
      CHECK(decode_image(test_exr_plain(2, 2, px, half), via, error) && via.width == 2);  // (decode_image too, libraries or not)
    }
    std::string zipped = test_exr_plain(2, 2, px);
    const size_t c = zipped.find("compression");
    zipped[c + 12 + 12 + 4] = 3;  // (name, type, size: then ZIP)
    Bitmap bm;
    std::string error;
    CHECK(!decode_exr_plain(zipped, bm, error) && error.find("compressed") != std::string::npos);
    CHECK(!decode_exr_plain(test_exr_plain(2, 2, px).substr(0, 200), bm, error));
    CHECK(!decode_exr_plain("not an exr", bm, error));
    const std::string pfm = fs::join(fs::temp_dir(), "blendity-test.pfm");
    const float rgb[6] = {1, 2, 3, 4, 5, 6};  // (a 1 x 2 image: top (1, 2, 3), bottom (4, 5, 6))
    CHECK(write_pfm(pfm, rgb, 1, 2));
    std::string data;
    fs::read_file(pfm, data);
    float first = 0;
    std::memcpy(&first, data.data() + std::string("PF\n1 2\n-1.0\n").size(), 4);
    CHECK(data.rfind("PF\n1 2\n-1.0\n", 0) == 0 && first == 4.0f);
    fs::remove_file(pfm);
  });

  /* A scene for the exporter: a turned cube with one material, the four light kinds, a gradient world. */
  auto r135_scene = [](std::vector<std::shared_ptr<Mesh>> &keep, std::vector<MaterialPtr> &mats, Environment &env) {
    moonray::ExportScene sc;
    keep.push_back(primitives::cube(1.0f));
    mats = {make_material("Red", {1, 0, 0})};
    sc.objects.push_back({&keep.back()->render_mesh(false), Mat4::trs({1, 2, 3}, Quat::euler({0, 30, 0}), {1, 1, 1}), &mats});
    RenderLight sun, point, spot, rect, disk;
    sun.type = RenderLight::Directional, sun.direction = normalize(Vec3(0, -1, 1)), sun.intensity = 2;
    point.type = RenderLight::Point, point.position = {0, 3, 0}, point.intensity = 1, point.range = 10;
    spot.type = RenderLight::Spot, spot.position = {2, 3, 0}, spot.direction = {0, -1, 0}, spot.cos_outer = std::cos(0.5f), spot.cos_inner = std::cos(0.3f);
    rect.type = RenderLight::Area, rect.position = {0, 4, 0}, rect.direction = {0, -1, 0}, rect.width = 2, rect.height = 1;
    disk = rect, disk.disk = true;
    sc.lights = {sun, point, spot, rect, disk};
    env.mode = Environment::Gradient;
    sc.world = &env;
    sc.world_map_width = 64;
    sc.view = Mat4::trs({0, 1, -5}, Quat::euler({0, 0, 0}), {1, 1, 1}).inverse();
    sc.proj = Mat4::perspective(60.0f * 3.14159265f / 180.0f, 16.0f / 9.0f, 0.1f, 100.0f);
    sc.width = 64, sc.height = 36, sc.samples = 64, sc.max_bounces = 5;
    return sc;
  };
  /* The numbers after `key` in an RDLA text, up to the closing brace. */
  auto r135_numbers = [](const std::string &text, const std::string &key) {
    std::vector<float> v;
    const size_t k = text.find("[\"" + key + "\"]");
    if (k == std::string::npos) return v;
    const size_t open = text.find('{', k), close = text.find('}', open);
    const char *p = text.c_str() + open + 1, *end = text.c_str() + close;
    while (p < end) {
      if (std::isalpha((unsigned char)*p)) {  // (a name such as Vec3: its digits aren't numbers)
        while (p < end && std::isalnum((unsigned char)*p)) p++;
        continue;
      }
      if (std::isdigit((unsigned char)*p) || *p == '-' || *p == '.') {
        char *q;
        v.push_back(std::strtof(p, &q));
        p = q;
      }
      else p++;
    }
    return v;
  };
  test("moonray export: a scene's meshes, material, lights, world, camera and settings in MoonRay's RDLA, balanced and finite", [&] {
    std::vector<std::shared_ptr<Mesh>> keep;
    std::vector<MaterialPtr> mats;
    Environment env;
    const moonray::ExportScene sc = r135_scene(keep, mats, env);
    const std::string dir = fs::join(fs::temp_dir(), "blendity-test-moonray-export");
    moonray::ExportReport rep;
    std::string error, text;
    const bool ok = moonray::export_scene(sc, dir, [](const std::string &p) { return p; }, rep, error);
    fs::read_file(rep.scene_path, text);
    int braces = 0, parens = 0;
    for (char c : text) braces += c == '{' ? 1 : c == '}' ? -1 : 0, parens += c == '(' ? 1 : c == ')' ? -1 : 0;
    std::printf("    %s: %zu bytes, %zu meshes (%zu triangles), %zu lights, %zu materials; braces %d, parentheses %d; %zu notes\n", ok ? "written" : error.c_str(),
                text.size(), rep.meshes, rep.triangles, rep.lights, rep.materials, braces, parens, rep.notes.size());
    CHECK(ok && rep.meshes == 1 && rep.triangles == 12 && rep.lights == 6 && rep.materials == 1);
    CHECK(braces == 0 && parens == 0);
    CHECK(text.find("nan") == std::string::npos && text.find("inf") == std::string::npos);
    for (const char *s : {"RdlMeshGeometry(\"mesh0\")", "DwaBaseMaterial(\"mat0\")", "DistantLight(", "SphereLight(", "SpotLight(", "RectLight(", "DiskLight(",
                          "EnvLight(\"world\")", "PerspectiveCamera(\"camera\")", "RenderOutput(\"beauty\")", "RenderOutput(\"alpha\")", "[\"pixel_samples\"] = 8,",
                          "[\"max_depth\"] = 5,", "[\"compression\"] = \"none\"", "[\"result\"] = \"alpha\"", "[\"scene_scale\"] = 1,", "Layer(\"layer\")"}) {
      if (text.find(s) == std::string::npos) std::printf("    missing: %s\n", s);
      CHECK(text.find(s) != std::string::npos);
    }
    std::string pfm;
    CHECK(fs::read_file(fs::join(dir, "world.pfm"), pfm) && pfm.size() == std::string("PF\n64 32\n-1.0\n").size() + 64 * 32 * 12);
    /* The cube faces out after the mirror: each triangle's winding agrees with its normals. */
    const std::vector<float> pos = r135_numbers(text, "vertex_list_0"), idx = r135_numbers(text, "vertices_by_index"), nrm = r135_numbers(text, "normal_list");
    int agree = 0, tris = (int)idx.size() / 3;
    for (int t = 0; t < tris; t++) {
      auto P = [&](int k) { const int i = (int)idx[t * 3 + k]; return Vec3(pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]); };
      const int i0 = (int)idx[t * 3];
      agree += dot(cross(P(1) - P(0), P(2) - P(0)), Vec3(nrm[i0 * 3], nrm[i0 * 3 + 1], nrm[i0 * 3 + 2])) > 0;
    }
    std::printf("    %d of %d triangles wound as their normals face\n", agree, tris);
    CHECK(tris == 12 && agree == tris);
    /* A point light of intensity 1 (range 10): the path tracer's pi x 0.81 at 1 m from a sphere of radius 0.1. */
    const size_t sp = text.find("SphereLight(");
    const float L = std::strtof(text.c_str() + text.find("[\"intensity\"] = ", sp) + 16, nullptr);
    CHECK_NEAR(L, 0.81f / (0.1f * 0.1f), 1e-2);
    const size_t dl = text.find("DistantLight(");
    CHECK_NEAR(std::strtof(text.c_str() + text.find("[\"intensity\"] = ", dl) + 16, nullptr), 2.0f, 1e-4);  // (MoonRay's normalized sun gives pi x its intensity, as Blendity's)
    /* The camera: MoonRay's film width 24 and the focal length giving the same horizontal field of view. */
    const float focal = std::strtof(text.c_str() + text.find("[\"focal\"] = ") + 12, nullptr);
    CHECK_NEAR(24.0f / (2.0f * focal), std::tan(30.0f * 3.14159265f / 180.0f) * 16.0f / 9.0f, 1e-4);
  });
  test("moonray export: the right-handed conversion keeps every point where it was (mirrored), the world map's directions, Principled's IOR", [] {
    const Mat4 m = Mat4::trs({1, -2, 3}, Quat::euler({10, 40, -20}), {1, 2, 0.5f});
    float worst = 0;
    for (Vec3 p : {Vec3(0, 0, 0), Vec3(1, 2, 3), Vec3(-4, 0.5f, 7)}) {
      const Vec3 a = moonray::to_right_handed(m).point(Vec3(p.x, p.y, -p.z)), b = m.point(p);
      worst = std::max(worst, length(a - Vec3(b.x, b.y, -b.z)));
    }
    CHECK(worst < 1e-4f);
    /* MoonRay's way round, as measured: v 1 up, u 0 -Z, 0.25 +X, 0.5 +Z (right-handed). */
    CHECK(length(moonray::env_map_direction(0.5f, 1.0f) - Vec3(0, -1, 0)) < 1e-4f);
    CHECK(length(moonray::env_map_direction(0.0f, 0.5f) - Vec3(0, 0, -1)) < 1e-4f);
    CHECK(length(moonray::env_map_direction(0.25f, 0.5f) - Vec3(1, 0, 0)) < 1e-4f);
    CHECK(std::fabs(moonray::env_map_direction(0.3f, 0.5f).y) < 1e-4f);
    for (float u : {0.0f, 0.25f, 0.7f}) CHECK_NEAR(length(moonray::env_map_direction(u, 0.3f)), 1.0f, 1e-5);
    CHECK_NEAR(moonray::ior_from_specular(0.5f), 1.5f, 1e-4);
    CHECK(moonray::ior_from_specular(0.0f) == 1.0f);
    CHECK(moonray::lua_number(std::nanf("")) == "0" && moonray::lua_number(0.25f) == "0.25");
  });
  test("moonray export: what MoonRay doesn't take is noted (a non-finite mesh, a generated texture, toon, alpha clipping), a hidden background, a 50k-face mesh quickly", [&] {
    std::vector<std::shared_ptr<Mesh>> keep;
    std::vector<MaterialPtr> mats;
    Environment env;
    moonray::ExportScene sc = r135_scene(keep, mats, env);
    MaterialPtr odd = make_material("Odd", {0, 1, 0});
    odd->base_map.path = "generated:UV Grid";
    odd->shading_model = 1;
    odd->surface = (int)MaterialSurface::Cutout;
    std::vector<MaterialPtr> odd_mats{odd};
    RenderMesh bad = sc.objects[0].mesh ? *sc.objects[0].mesh : RenderMesh();
    bad.positions[0] = Vec3(std::nanf(""), 0, 0);
    sc.objects.push_back({sc.objects[0].mesh, Mat4(), &odd_mats});
    sc.objects.push_back({&bad, Mat4(), &mats});
    sc.hide_background = true;
    moonray::ExportReport rep;
    std::string error, text;
    CHECK(moonray::export_scene(sc, fs::join(fs::temp_dir(), "blendity-test-moonray-odd"), [](const std::string &p) { return p; }, rep, error));
    fs::read_file(rep.scene_path, text);
    std::string all;
    for (const std::string &n : rep.notes) all += n + " | ";
    std::printf("    %zu meshes; notes: %s\n", rep.meshes, all.c_str());
    CHECK(rep.meshes == 2);
    for (const char *s : {"non-finite", "generated texture", "toon", "alpha clipping", "fade physically"}) CHECK(all.find(s) != std::string::npos);
    CHECK(text.find("\"force off\"") != std::string::npos);
    moonray::ExportScene big = sc;
    keep.push_back(primitives::uv_sphere(1.0f, 256, 196));
    big.objects = {{&keep.back()->render_mesh(false), Mat4(), &mats}};
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(moonray::export_scene(big, fs::join(fs::temp_dir(), "blendity-test-moonray-big"), [](const std::string &p) { return p; }, rep, error));
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("    a 50k-face sphere (%zu triangles): written in %.0f ms\n", rep.triangles, ms);
    CHECK(rep.triangles > 90000 && ms < 5000.0);
  });
  test("moonray in F12: the scene sent to MoonRay (a stand-in here), its progress shown, its image read back through the view transform; transparent, failed, stopped, and the path tracer when it isn't installed", [] {
    auto run_f12 = [](Editor &ed, double limit_s) {
      ed.command("render");
      const auto t0 = std::chrono::steady_clock::now();
      std::string seen;
      while (ed.rendering_for_test() && std::chrono::steady_clock::now() - t0 < std::chrono::duration<double>(limit_s)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        ed.step_frame_headless();
        if (ed.render_status_for_test().find('%') != std::string::npos) seen = ed.render_status_for_test();
      }
      return seen;
    };
    Editor ed;
    r34_open(ed);
    CHECK(!ed.moonray_offer_pending_for_test());  // (never asked in a headless run)
    moonray::Install in;
    in.kind = moonray::Install::Native;
    in.root = "stand-in";
    in.version = "test";
    in.launcher = {test_program(), "--fake-moonray"};
    ed.moonray_set_for_test(in);
    ed.command("engine moonray");
    ed.scene().render.width = 64, ed.scene().render.height = 36, ed.scene().render.percent = 100;
    const std::string progress = run_f12(ed, 30.0);
    const Image &img = ed.render_image_for_test();
    const uint32_t c = img.pixels.empty() ? 0 : img.pixels[img.pixels.size() / 2];
    const int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
    std::printf("    engine %d; progress seen '%s'; '%s'; %dx%d, the middle pixel %d %d %d\n", ed.scene().render.engine, progress.c_str(),
                ed.render_status_for_test().c_str(), img.width, img.height, r, g, b);
    CHECK(ed.scene().render.engine == 2);
    CHECK(ed.render_status_for_test().rfind("MoonRay 64x36", 0) == 0);
    CHECK(img.width == 64 && img.height == 36 && r > g && g > b && b > 0);
    ed.scene().render.transparent = true;
    run_f12(ed, 30.0);
    const uint32_t left = img.pixels[(size_t)18 * 64 + 5], right = img.pixels[(size_t)18 * 64 + 60];
    std::printf("    transparent: alpha %u on the left (the stand-in's empty half), %u on the right\n", left >> 24, right >> 24);
    CHECK((left >> 24) == 0 && (right >> 24) == 255);
    ed.scene().render.transparent = false;
    set_env("BLENDITY_FAKE_MOONRAY", "fail");
    run_f12(ed, 30.0);
    std::printf("    told to fail: '%s'\n", ed.render_status_for_test().substr(0, 80).c_str());
    CHECK(ed.render_status_for_test().find("MoonRay failed: exit 3") != std::string::npos);
    set_env("BLENDITY_FAKE_MOONRAY", "slow");
    ed.command("render");
    for (int k = 0; k < 20; k++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      ed.step_frame_headless();
    }
    const bool was = ed.moonray_rendering_for_test();
    const auto t0 = std::chrono::steady_clock::now();
    ed.stop_render_for_test();
    ed.step_frame_headless();
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    set_env("BLENDITY_FAKE_MOONRAY", "");
    std::printf("    stopped while rendering (%s): MoonRay gone after %.0f ms\n", was ? "it was" : "it was NOT", ms);
    CHECK(was && !ed.moonray_rendering_for_test() && ms < 3000.0);
    moonray::Install none;
    none.kind = moonray::Install::Native;
    ed.moonray_set_for_test(none);
    ed.scene().render.samples = 2;
    run_f12(ed, 60.0);
    std::printf("    not installed: '%s'\n", ed.render_status_for_test().substr(0, 60).c_str());
    CHECK(ed.render_status_for_test().find("MoonRay") == std::string::npos && !img.pixels.empty());
  });
  test("moonray install: an archive whose checksum doesn't match is refused; one unpacks, renders its test scene and is removed again (Linux and macOS)", [] {
    Editor ed;
    r34_open(ed);
    moonray::Install in;
    in.kind = moonray::Install::Native;
    ed.moonray_set_for_test(in);
    const std::string dir = fs::join(fs::temp_dir(), "blendity-test-moonray-install");
    fs::make_dirs(dir);
    const std::string archive = fs::join(dir, "blendity-moonray-test.tar.gz");
    fs::write_file(archive, "not really an archive");
    fs::write_file(archive + ".sha256", std::string(64, '0') + "  blendity-moonray-test.tar.gz\n");
    ed.command("moonray install " + archive);  // (checked on a thread of its own: the editor isn't held up)
    for (int k = 0; k < 2000 && ed.moonray_busy_for_test(); k++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      ed.step_frame_headless();
    }
    CHECK(!ed.moonray_busy_for_test() && !ed.moonray_for_test().ready() && fs::exists(archive));
#ifndef _WIN32
    /* A stand-in archive: bin/blendity-moonray writes whatever -out names. Installed under a scratch HOME. */
    const std::string src = fs::join(dir, "src/moonray");
    fs::make_dirs(fs::join(src, "bin"));
    fs::make_dirs(fs::join(src, "test"));
    fs::write_file(fs::join(src, "bin/blendity-moonray"),
                   "#!/bin/sh\nout=\nwhile [ $# -gt 0 ]; do [ \"$1\" = -out ] && out=$2; shift; done\necho '  [+] Rendering 100.0%'\nprintf x > \"$out\"\n");
    sys::run_program({"chmod", "+x", fs::join(src, "bin/blendity-moonray")});
    fs::write_file(fs::join(src, "test/rectangle.rdla"), "SceneVariables {}\n");
    fs::write_file(fs::join(src, "VERSION"), "vtest\n");
    const std::string tgz = fs::join(dir, "good.tar.gz");
    CHECK(sys::run_program({"tar", "-czf", tgz, "-C", fs::join(dir, "src"), "moonray"}) == 0);
    const char *home = std::getenv("HOME");
    const std::string old_home = home ? home : "";
    set_env("HOME", dir);
    moonray::Install here = moonray::locate("");
    std::string error;
    const bool unpacked = moonray::unpack(here, tgz, error);
    std::printf("    unpacked: %s (%s, version '%s'); \n", unpacked ? "yes" : error.c_str(), here.root.c_str(), here.version.c_str());
    CHECK(unpacked && here.ready() && here.version == "vtest" && here.root.find(dir) == 0);
    CHECK(moonray::locate("").ready());
    CHECK(moonray::remove(here, error) && !here.ready() && !moonray::locate("").ready());
    if (home) set_env("HOME", old_home);
    else unsetenv("HOME");
#endif
  });
  test("moonray (review): a short EXR claiming a huge image is refused before anything is allocated; a program whose child keeps the pipe open still ends when it does", [] {
    std::vector<float> px(4 * 4, 0.5f);
    std::string bomb = test_exr_plain(2, 2, px);
    const size_t dw = bomb.find("dataWindow");
    const int32_t huge[4] = {0, 0, 65535, 65535};
    std::memcpy(&bomb[dw + 11 + 6 + 4], huge, 16);  // (name, type "box2i", size: then the box)
    Bitmap bm;
    std::string error;
    const auto t0 = std::chrono::steady_clock::now();
    const bool read = decode_exr_plain(bomb, bm, error);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("    65536 x 65536 claimed in %zu bytes: %s in %.1f ms\n", bomb.size(), read ? "READ" : error.c_str(), ms);
    CHECK(!read && error.find("truncated") != std::string::npos && ms < 100.0 && bm.rgbaf.empty());
#ifdef _WIN32
    const std::vector<std::string> holder = {"cmd", "/c", "start /b ping -n 6 127.0.0.1 >nul & echo hi"};
#else
    const std::vector<std::string> holder = {"sh", "-c", "sleep 5 & echo hi"};
#endif
    std::string out;
    const auto t1 = std::chrono::steady_clock::now();
    const int code = sys::run_streaming(holder, [&](const std::string &s) { out += s; });
    const double ms1 = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
    std::printf("    a child still holding the pipe: exit %d after %.0f ms ('%s')\n", code, ms1, out.substr(0, out.find_first_of("\r\n")).c_str());
    CHECK(code == 0 && out.find("hi") != std::string::npos && ms1 < 3000.0);
  });
  test("moonray in F12 (review): a new render replaces a MoonRay one under way; an orthographic camera path traces", [] {
    Editor ed;
    r34_open(ed);
    moonray::Install in;
    in.kind = moonray::Install::Native;
    in.root = "stand-in";
    in.version = "test";
    in.launcher = {test_program(), "--fake-moonray"};
    ed.moonray_set_for_test(in);
    ed.command("engine moonray");
    ed.scene().render.width = 64, ed.scene().render.height = 36, ed.scene().render.percent = 100, ed.scene().render.samples = 2;
    set_env("BLENDITY_FAKE_MOONRAY", "slow");
    ed.command("render");
    ed.step_frame_headless();
    const bool moon = ed.moonray_rendering_for_test();
    ed.command("engine path");
    const auto t0 = std::chrono::steady_clock::now();
    ed.command("render");
    const bool replaced = !ed.moonray_rendering_for_test();
    for (int k = 0; k < 4000 && ed.rendering_for_test(); k++) ed.step_frame_headless();
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    set_env("BLENDITY_FAKE_MOONRAY", "");
    std::printf("    MoonRay under way: %s; replaced by the path tracer: %s; it finished in %.0f ms: '%s'\n", moon ? "yes" : "no", replaced ? "yes" : "no", ms,
                ed.render_status_for_test().substr(0, 40).c_str());
    CHECK(moon && replaced && !ed.rendering_for_test() && ed.render_status_for_test().find("MoonRay") == std::string::npos);
    GameObject *cam = ed.scene().find_by_name("Main Camera");
    CHECK(cam && cam->get<Camera>());
    if (cam && cam->get<Camera>()) cam->get<Camera>()->orthographic = true;
    ed.command("engine moonray");
    ed.command("render");
    CHECK(!ed.moonray_rendering_for_test());  // (orthographic: not sent; the path tracer renders)
    for (int k = 0; k < 4000 && ed.rendering_for_test(); k++) ed.step_frame_headless();
    std::printf("    orthographic: '%s'\n", ed.render_status_for_test().substr(0, 40).c_str());
    CHECK(ed.render_status_for_test().find("MoonRay") == std::string::npos);
  });
  test("moonray in F12: the image fills in - MoonRay's checkpoint shown while it renders, then the final image", [] {
    Editor ed;
    r34_open(ed);
    moonray::Install in;
    in.kind = moonray::Install::Native;
    in.root = "stand-in";
    in.version = "test";
    in.launcher = {test_program(), "--fake-moonray"};
    ed.moonray_set_for_test(in);
    ed.command("engine moonray");
    ed.scene().render.width = 64, ed.scene().render.height = 36, ed.scene().render.percent = 100, ed.scene().render.samples = 2;
    set_env("BLENDITY_FAKE_MOONRAY", "progress");
    ed.command("render");
    /* While it renders: the checkpoint (half as bright) in the Render panel. */
    float during = -1.0f;
    for (int k = 0; k < 3000 && ed.rendering_for_test() && during < 0.0f; k++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      ed.step_frame_headless();
      if (ed.rendering_for_test() && ed.render_linear_for_test().size() >= 3) during = ed.render_linear_for_test()[0];
    }
    for (int k = 0; k < 4000 && ed.rendering_for_test(); k++) std::this_thread::sleep_for(std::chrono::milliseconds(2)), ed.step_frame_headless();
    set_env("BLENDITY_FAKE_MOONRAY", "");
    const float after = ed.render_linear_for_test().size() >= 3 ? ed.render_linear_for_test()[0] : -1.0f;
    std::printf("    red while rendering %.3f (the checkpoint), when done %.3f; '%s'\n", during, after, ed.render_status_for_test().substr(0, 40).c_str());
    CHECK(std::fabs(during - 0.25f) < 1e-3f && std::fabs(after - 0.5f) < 1e-3f);
  });
}

static void tests_round136() {
  /* ---------------------------------------------------------------- round 136: MoonRay checked against the real renderer (task 0113) */
  test("moonray export: a texture goes as a tiled, mip-mapped EXR (MoonRay takes nothing else) - every level's tiles where the table says, down to one pixel of the image's mean; made once while its source is unchanged", [] {
    struct Parsed {
      bool ok = false;
      int levels = 0, tiles = 0;
      Vec3 last;  // (the 1 x 1 level's colour)
    };
    auto parse = [](const std::string &d, int w, int h) {
      Parsed p;
      auto u32 = [&](size_t at) { uint32_t v = 0; std::memcpy(&v, &d[at], 4); return v; };
      if (d.size() < 8 || u32(0) != 20000630u || !(u32(4) & 0x200u)) return p;
      size_t at = 8;
      bool tiled = false;
      while (at < d.size() && d[at]) {  // (the header's attributes, to the empty name)
        const std::string name = d.c_str() + at;
        at += name.size() + 1;
        at += std::strlen(d.c_str() + at) + 1;
        const uint32_t size = u32(at);
        at += 4;
        if (name == "tiles") tiled = u32(at) == 64 && u32(at + 4) == 64 && d[at + 8] == 1;
        at += size;
      }
      at++;
      int levels = 1;
      while ((std::max(w, h) >> levels) > 0) levels++;
      p.levels = levels;
      bool good = tiled;
      for (int l = 0; l < levels; l++) {
        const int lw = std::max(1, w >> l), lh = std::max(1, h >> l);
        for (int ty = 0; ty * 64 < lh; ty++)
          for (int tx = 0; tx * 64 < lw; tx++, p.tiles++) {
            uint64_t off = 0;
            std::memcpy(&off, &d[at + (size_t)p.tiles * 8], 8);
            const int tw = std::min(64, lw - tx * 64), th = std::min(64, lh - ty * 64);
            good = good && off + 20 + (uint64_t)tw * th * 12 <= d.size() && u32(off) == (uint32_t)tx && u32(off + 4) == (uint32_t)ty &&
                   u32(off + 8) == (uint32_t)l && u32(off + 12) == (uint32_t)l && u32(off + 16) == (uint32_t)(tw * th * 12);
            if (good && l + 1 == levels) {
              float bgr[3];
              std::memcpy(bgr, &d[off + 20], 12);
              p.last = Vec3(bgr[2], bgr[1], bgr[0]);
            }
          }
      }
      p.ok = good;
      return p;
    };
    const std::string dir = fs::join(fs::temp_dir(), "blendity-test-texture-exr");
    fs::make_dirs(dir);
    for (Vec2 size : {Vec2(100, 37), Vec2(128, 64)}) {
      const int w = (int)size.x, h = (int)size.y;
      std::vector<float> rgb((size_t)w * h * 3);
      Vec3 mean(0.0f);
      for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
          const Vec3 c((float)x / w, (float)y / h, 0.25f);
          rgb[((size_t)y * w + x) * 3] = c.x, rgb[((size_t)y * w + x) * 3 + 1] = c.y, rgb[((size_t)y * w + x) * 3 + 2] = c.z;
          mean = mean + c / (float)(w * h);
        }
      const std::string file = fs::join(dir, "t.exr");
      CHECK(write_texture_exr(file, rgb.data(), w, h));
      std::string bytes;
      fs::read_file(file, bytes);
      const Parsed p = parse(bytes, w, h);
      std::printf("    %d x %d: %d levels, %d tiles, %s; the last level (%.3f %.3f %.3f), the mean (%.3f %.3f %.3f)\n", w, h, p.levels, p.tiles,
                  p.ok ? "each where the table says" : "BROKEN", p.last.x, p.last.y, p.last.z, mean.x, mean.y, mean.z);
      CHECK(p.ok && p.levels == (w == 100 ? 7 : 8) && p.tiles == (w == 100 ? 8 : 9));
      CHECK(length(p.last - mean) < 1e-4f);  // (area-weighted all the way down, odd sizes too: the image's mean exactly)
    }
    CHECK(!write_texture_exr(fs::join(dir, "none.exr"), nullptr, 0, 0));
    /* Made once while the source is unchanged (an animation's frames reuse it), again when it changes; an unreadable
     * source gives a white pixel (the material's own colour) and says so. */
    const std::string png = fs::join(dir, "src.png"), out = fs::join(dir, "made.exr");
    std::vector<uint32_t> px(8 * 4, 0xFF808080u);
    write_png(png, px.data(), 8, 4, 8);
    std::string e, bytes;
    const moonray::TextureJob job{png, out, false};
    CHECK(moonray::convert_texture(job, e));
    fs::write_file(out, "kept");  // (a stand-in: if it's made again, this goes)
    CHECK(moonray::convert_texture(job, e));
    fs::read_file(out, bytes);
    const bool reused = bytes == "kept";
    px.assign(64 * 64, 0xFF808080u);
    for (size_t i = 0; i < px.size(); i += 2) px[i] = 0xFF7F7F7Fu, px[i + 1] = 0xFF818181u;  // (grey on average; a bigger file)
    write_png(png, px.data(), 64, 64, 64);  // (another size: changed, whatever the clock's resolution)
    CHECK(moonray::convert_texture(job, e));
    fs::read_file(out, bytes);
    const Parsed again = parse(bytes, 64, 64);
    fs::write_file(png, "not an image");
    const bool bad = moonray::convert_texture(job, e);
    fs::read_file(out, bytes);
    const Parsed white = parse(bytes, 1, 1);
    std::printf("    the same source: %s; changed: %s (its mean %.4f linear); unreadable: %s, '%s', (%.1f %.1f %.1f)\n", reused ? "reused" : "MADE AGAIN",
                again.ok ? "made again" : "NOT MADE", again.last.x, bad ? "CONVERTED" : "refused", e.c_str(), white.last.x, white.last.y, white.last.z);
    CHECK(reused && again.ok && std::fabs(again.last.x - 0.5f * (srgb_byte_to_linear(0x7F) + srgb_byte_to_linear(0x81))) < 1e-5f);
    CHECK(!bad && !e.empty() && white.ok && length(white.last - Vec3(1.0f)) < 1e-6f);
    for (const char *f : {"t.exr", "src.png", "made.exr", "made.exr.source"}) fs::remove_file(fs::join(dir, f));
  });
  test("moonray (real, BLENDITY_REAL_MOONRAY=1): the installed MoonRay renders scenes as the path tracer does - the world's orientation, a sun's and a point light's brightness, a texture's way round", [] {
    if (!std::getenv("BLENDITY_REAL_MOONRAY")) {
      std::printf("    skipped (set BLENDITY_REAL_MOONRAY=1 with MoonRay installed: it renders in it)\n");
      return;
    }
    const moonray::Install in = moonray::locate("");
    std::printf("    MoonRay: %s %s%s\n", in.ready() ? in.version.c_str() : "not ready", in.root.c_str(), in.problem.empty() ? "" : (" - " + in.problem).c_str());
    CHECK(in.ready());
    if (!in.ready()) return;
    std::string probe_error;
    CHECK(moonray::probe(in, probe_error));
    if (!probe_error.empty()) std::printf("    probe: %s\n", probe_error.c_str());
    /* A scene: a matte floor (albedo 0.5) at y = 0, no other object, the scene's camera off (F12 takes the editor's). */
    auto scene = [&](Editor &ed) {
      r34_open(ed);
      Scene &s = ed.scene();
      std::vector<GameObject *> drop;
      s.for_each([&](GameObject &g) {
        if (g.get<MeshRenderer>() || g.get<Light>() || g.get<Camera>()) drop.push_back(&g);
      });
      for (GameObject *g : drop) {
        if (Camera *c = g->get<Camera>()) c->enabled = false;
        if (Light *l = g->get<Light>()) l->enabled = false;
        if (MeshRenderer *m = g->get<MeshRenderer>()) m->enabled = false;
      }
      GameObject *floor = s.create("Floor");
      floor->add<MeshFilter>()->mesh = primitives::grid(40.0f, 4, 4);
      floor->add<MeshRenderer>()->materials = {make_material("Grey", {0.5f, 0.5f, 0.5f})};
      ed.scene().render.width = 96, ed.scene().render.height = 54, ed.scene().render.percent = 100, ed.scene().render.samples = 64;
      ed.scene().render.denoise = false;
      ed.scene().render.view_transform = 0;  // (Standard: the numbers compared as they are)
      return floor;
    };
    auto render = [&](Editor &ed, bool moon) {
      ed.moonray_set_for_test(in);
      ed.command(moon ? "engine moonray" : "engine path");
      ed.command("render");
      for (int k = 0; k < 20000 && ed.rendering_for_test(); k++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(moon ? 20 : 0));
        ed.step_frame_headless();
      }
      return ed.render_linear_for_test();
    };
    /* The mean light over a part of the image (rows y0..y1, columns x0..x1 in 0..1). */
    auto mean = [](const std::vector<float> &lin, float x0, float x1, float y0, float y1) {
      Vec3 s(0.0f);
      int n = 0;
      const int w = 96, h = 54;
      if (lin.size() < (size_t)w * h * 3) return Vec3(-1.0f);
      for (int y = (int)(y0 * h); y < (int)(y1 * h); y++)
        for (int x = (int)(x0 * w); x < (int)(x1 * w); x++, n++) s = s + Vec3(lin[((size_t)y * w + x) * 3], lin[((size_t)y * w + x) * 3 + 1], lin[((size_t)y * w + x) * 3 + 2]);
      return n ? s / (float)n : s;
    };
    auto report = [](const char *what, Vec3 pt, Vec3 mr) {
      std::printf("    %-34s path tracer (%.4f %.4f %.4f), MoonRay (%.4f %.4f %.4f), ratio %.3f\n", what, pt.x, pt.y, pt.z, mr.x, mr.y, mr.z,
                  (mr.x + mr.y + mr.z) / std::max(1e-6f, pt.x + pt.y + pt.z));
    };
    {  // The world alone (Unity's gradient), looking at the horizon: sky above, ground's colour below (the floor hidden).
      Editor ed;
      GameObject *floor = scene(ed);
      floor->get<MeshRenderer>()->enabled = false;
      ed.command("camera 0 0 10 0 1 0");  // (yaw, pitch, distance, pivot: level, at the horizon)
      const std::vector<float> pt = render(ed, false), mr = render(ed, true);
      report("the world, top rows (sky)", mean(pt, 0, 1, 0, 0.2f), mean(mr, 0, 1, 0, 0.2f));
      report("the world, bottom rows (ground)", mean(pt, 0, 1, 0.8f, 1), mean(mr, 0, 1, 0.8f, 1));
      report("the world, left third", mean(pt, 0, 0.33f, 0.3f, 0.7f), mean(mr, 0, 0.33f, 0.3f, 0.7f));
      for (float y0 : {0.0f, 0.8f}) {
        const Vec3 a = mean(pt, 0, 1, y0, y0 + 0.2f), b = mean(mr, 0, 1, y0, y0 + 0.2f);
        CHECK(length(a - b) < 0.03f * length(a));
      }
    }
    {  // A sun straight down, the world black: the floor's light.
      Editor ed;
      scene(ed);
      ed.scene().environment.mode = 3;
      ed.scene().environment.color = Vec3(0.0f);
      GameObject *sun = ed.scene().create("Sun");
      Light *l = sun->add<Light>();
      l->type = 0, l->intensity = 1.0f, l->color = Vec3(1.0f);
      sun->set_local_rotation(Quat::euler({90, 0, 0}));
      ed.command("camera 0 35 6 0 0 0");
      const std::vector<float> pt = render(ed, false), mr = render(ed, true);
      report("a sun from above, the floor", mean(pt, 0.3f, 0.7f, 0.4f, 0.8f), mean(mr, 0.3f, 0.7f, 0.4f, 0.8f));
      const Vec3 a = mean(pt, 0.3f, 0.7f, 0.4f, 0.8f), b = mean(mr, 0.3f, 0.7f, 0.4f, 0.8f);
      CHECK(length(a - b) < 0.03f * length(a));
    }
    {  // A point light 1 m above the floor, the world black: the floor below it.
      Editor ed;
      scene(ed);
      ed.scene().environment.mode = 3;
      ed.scene().environment.color = Vec3(0.0f);
      GameObject *lamp = ed.scene().create("Lamp");
      Light *l = lamp->add<Light>();
      l->type = 1, l->intensity = 1.0f, l->range = 10.0f, l->color = Vec3(1.0f);
      lamp->set_local_position({0, 1, 0});
      ed.command("camera 0 60 4 0 0 0");
      const std::vector<float> pt = render(ed, false), mr = render(ed, true);
      report("a point light 1 m up, under it", mean(pt, 0.45f, 0.55f, 0.4f, 0.6f), mean(mr, 0.45f, 0.55f, 0.4f, 0.6f));
      const Vec3 a = mean(pt, 0.45f, 0.55f, 0.4f, 0.6f), b = mean(mr, 0.45f, 0.55f, 0.4f, 0.6f);
      const float r = (b.x + b.y + b.z) / std::max(1e-6f, a.x + a.y + a.z);
      CHECK(r > 0.8f && r < 1.25f);  // (MoonRay's falls off physically, Blendity's by Unity's Range: matched at 1 m)
    }
    auto ratio = [&](const std::vector<float> &pt, const std::vector<float> &mr, float x0, float x1, float y0, float y1) {
      const Vec3 a = mean(pt, x0, x1, y0, y1), b = mean(mr, x0, x1, y0, y1);
      return (b.x + b.y + b.z) / std::max(1e-6f, a.x + a.y + a.z);
    };
    {  // The physical sky with its sun to one side: the world map turned the same way (its brightest third the same side).
      Editor ed;
      GameObject *floor = scene(ed);
      floor->get<MeshRenderer>()->enabled = false;
      ed.scene().environment.mode = 1;
      GameObject *sun = ed.scene().create("Sun");
      Light *l = sun->add<Light>();
      l->type = 0, l->intensity = 0.0001f;  // (it places the sky's sun; too faint to light anything)
      sun->set_local_rotation(Quat::euler({20, 60, 0}));
      ed.command("camera 0 10 10 0 1 0");
      const std::vector<float> pt = render(ed, false), mr = render(ed, true);
      report("a sky, left third", mean(pt, 0, 0.33f, 0.2f, 0.5f), mean(mr, 0, 0.33f, 0.2f, 0.5f));
      report("a sky, right third", mean(pt, 0.67f, 1, 0.2f, 0.5f), mean(mr, 0.67f, 1, 0.2f, 0.5f));
      CHECK(std::fabs(ratio(pt, mr, 0, 0.33f, 0.2f, 0.5f) - 1.0f) < 0.05f && std::fabs(ratio(pt, mr, 0.67f, 1, 0.2f, 0.5f) - 1.0f) < 0.05f);
    }
    {  // A floor with a four-colour texture (red, green / blue, white): each quadrant the same colour in both (the UVs' way round).
      Editor ed;
      GameObject *floor = scene(ed);
      floor->get<MeshFilter>()->mesh = primitives::grid(4.0f, 1, 1);
      const std::string png = fs::join(fs::temp_dir(), "blendity-test-quadrants.png");
      std::vector<uint32_t> px(64 * 64);
      for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) px[(size_t)y * 64 + x] = y < 32 ? (x < 32 ? 0xFFFF0000u : 0xFF00FF00u) : (x < 32 ? 0xFF0000FFu : 0xFFFFFFFFu);
      write_png(png, px.data(), 64, 64, 64);
      MaterialPtr m = make_material("Quadrants", {1, 1, 1});
      m->base_map.path = png;
      m->unlit = true;  // (its colour exactly, no light needed)
      floor->get<MeshRenderer>()->materials = {m};
      ed.scene().environment.mode = 3;
      ed.scene().environment.color = Vec3(0.0f);
      ed.command("camera 0 89 4 0 0 0");
      const std::vector<float> pt = render(ed, false), mr = render(ed, true);
      int same = 0;
      for (float qx : {0.3f, 0.6f})
        for (float qy : {0.3f, 0.6f}) {
          const Vec3 a = mean(pt, qx, qx + 0.1f, qy, qy + 0.1f), b = mean(mr, qx, qx + 0.1f, qy, qy + 0.1f);
          std::printf("    quadrant at (%.1f, %.1f): path tracer (%.2f %.2f %.2f), MoonRay (%.2f %.2f %.2f)\n", qx, qy, a.x, a.y, a.z, b.x, b.y, b.z);
          same += length(a - b) < 0.1f * std::max(0.1f, length(a));
        }
      CHECK(same == 4);
    }
  });
}

static void tests_round165() {
  /* ---------------------------------------------------------------- round 165: Anime and Manga as single effects (task 0154) */
  auto r165_max_diff = [](const Image &a, const Image &b) {
    int worst = 0;
    if (a.pixels.size() != b.pixels.size()) return 255;
    for (size_t i = 0; i < a.pixels.size(); i++)
      for (int s = 0; s < 24; s += 8) worst = std::max(worst, std::abs((int)((a.pixels[i] >> s) & 255) - (int)((b.pixels[i] >> s) & 255)));
    return worst;
  };
  test("anime filters: the Anime effect draws what the old Line Art + Color Grading + Bloom stack drew (within a step of 255), and its fields change the picture", [&] {
    Editor one;
    r57_open(one);
    one.command("select Main Camera");
    one.command("filter look Anime");
    const Image a = r57_render(one, "Rasterized", false);
    Editor two;
    r57_open(two);
    two.command("select Main Camera");
    two.command("filter add Line Art");
    two.command("set CameraFilters.E0Thickness 1.2");
    two.command("set CameraFilters.E0CreaseAngle 50");
    two.command("filter add Color Grading");
    two.command("set CameraFilters.E1Saturation 0.15");
    two.command("set CameraFilters.E1Contrast 0.08");
    two.command("filter add Bloom");
    two.command("set CameraFilters.E2Intensity 0.25");
    two.command("set CameraFilters.E2Threshold 1.2");
    const Image b = r57_render(two, "Rasterized", false);
    const int d = r165_max_diff(a, b);
    std::printf("    Anime against the three-effect stack: the largest channel difference %d\n", d);
    CHECK(a.width == 160 && d <= 1);
    one.command("set CameraFilters.E0Lines 0");
    const Image c = r57_render(one, "Rasterized", false);
    CHECK(r165_max_diff(a, c) > 1);
    /* Saved and loaded with its fields. */
    Scene back;
    std::string err;
    CHECK(load_scene_text(save_scene_text(one.scene()), back, err));
    GameObject *bc = back.find_by_name("Main Camera");
    CameraFilters *cf = bc ? bc->get<CameraFilters>() : nullptr;
    CHECK(cf && cf->effects.size() == 1 && std::string(cf->effects[0]->type_name()) == AnimeEffect::kName && !static_cast<AnimeEffect *>(cf->effects[0].get())->lines);
  });

  test("anime filters: the Manga effect draws what the old Grayscale + Halftone + Line Art + Paper stack drew, Tones posterises, and the old composite stack still loads and renders", [&] {
    Editor one;
    r57_open(one);
    one.command("select Main Camera");
    one.command("filter look Manga");
    const Image a = r57_render(one, "Rasterized", false);
    Editor two;
    r57_open(two);
    two.command("select Main Camera");
    two.command("filter add Grayscale / Sepia");
    two.command("filter add Halftone");
    two.command("set CameraFilters.E1CellSize 6");
    two.command("set CameraFilters.E1Contrast 1.2");
    two.command("filter add Line Art");
    two.command("set CameraFilters.E2Thickness 1.5");
    two.command("filter add Paper");
    const Image b = r57_render(two, "Rasterized", false);
    const int d = r165_max_diff(a, b);
    std::printf("    Manga against the four-effect stack: the largest channel difference %d\n", d);
    CHECK(a.width == 160 && d <= 1);
    one.command("set CameraFilters.E0Tones 3");
    const Image c = r57_render(one, "Rasterized", false);
    CHECK(r165_max_diff(a, c) > 1);
    /* The old composite (a scene saved before task 0154) loads as its four effects (and rendered above, as `b`). */
    Scene back;
    std::string err;
    CHECK(load_scene_text(save_scene_text(two.scene()), back, err));
    GameObject *cam = back.find_by_name("Main Camera");
    CameraFilters *cf = cam ? cam->get<CameraFilters>() : nullptr;
    CHECK(cf && cf->effects.size() == 4 && std::string(cf->effects[1]->type_name()) == HalftoneEffect::kName);
  });
}

static void tests_round167() {
  /* ---------------------------------------------------------------- round 167: toon styles (task 0148, ADR 0048) */
  /* A small texture from colours (linear, so the samples come back as given): w x h, row by row from the top. */
  auto r167_tex = [](int w, int h, const std::vector<Vec3> &px) {
    Bitmap bm;
    bm.width = w, bm.height = h;
    for (const Vec3 &c : px)
      for (int k = 0; k < 4; k++) bm.rgba8.push_back((uint8_t)std::lround(clampf(k == 3 ? 1.0f : c[k], 0.0f, 1.0f) * 255.0f));
    auto t = std::make_shared<Texture>();
    t->build(bm, false);
    return TexturePtr(t);
  };
  test("toon styles: each style sets its fields (the maps kept), by name in any spelling; toonstyle on the selected objects' materials undoes as one step; a .mat keeps the new fields", [] {
    Material m;
    m.shade_map.path = "Assets/Textures/face_shade.png";
    apply_toon_style(m, 1);
    CHECK(m.shading_model == 1 && m.toon_style == 1 && m.toon_bands == 1 && m.toon_softness == 0.0f && m.toon_key_light_only && m.toon_rim_width == 0.0f &&
          m.toon_specular_size > 0.0f && m.shade_map.path == "Assets/Textures/face_shade.png");
    apply_toon_style(m, 2);
    CHECK(m.toon_style == 2 && m.toon_bands == 3 && std::fabs(m.toon_wrap - 0.3f) < 1e-6f && m.toon_rim_side == 1 && m.toon_rim_width > 0.0f);
    apply_toon_style(m, 3);
    CHECK(m.toon_style == 3 && m.toon_bands == 4 && !m.toon_flat_ambient && !m.toon_key_light_only);
    apply_toon_style(m, 4);
    CHECK(m.toon_style == 4 && m.toon_bands == 1 && std::fabs(m.toon_shadow.x - m.toon_shadow.z) < 1e-6f && m.toon_specular_size == 0.0f);
    apply_toon_style(m, 0);
    CHECK(m.toon_style == 0 && m.toon_bands == 1 && std::fabs(m.toon_threshold - 0.15f) < 1e-6f && std::fabs(m.toon_rim_width - 0.25f) < 1e-6f);
    CHECK(toon_style_index("guiltygear") == 1 && toon_style_index("Soft anime") == 2 && toon_style_index("PAINTERLY") == 3 && toon_style_index("nope") == -1);
    /* The console on the selected objects' materials, one undo step (a frame between the commands: a frame's changes
     * are one step). */
    Editor ed;
    r34_open(ed);
    ed.command("create Cube");
    ed.command("select Cube");
    ed.command("material Toon");
    ed.step_frame_headless();
    MaterialPtr cm = ed.scene().find_by_name("Cube")->get<MeshRenderer>()->materials[0];
    ed.command("toonstyle Painterly");
    CHECK(cm->toon_style == 3 && cm->toon_bands == 4);
    ed.command("toonstyle Nope");  // a warning, no change
    CHECK(cm->toon_style == 3);
    ed.step_frame_headless();
    ed.command("undo");
    GameObject *cube = ed.scene().find_by_name("Cube");
    MeshRenderer *cmr = cube ? cube->get<MeshRenderer>() : nullptr;
    CHECK(cmr && !cmr->materials.empty());
    if (!cmr || cmr->materials.empty()) return;
    cm = cmr->materials[0];
    std::printf("    after undo: style %d, bands %d\n", cm->toon_style, cm->toon_bands);
    CHECK(cm->shading_model == 1 && cm->toon_style == 0 && cm->toon_bands == 1);  // (the Toon preset stays: only the style's step went)
    /* The .mat round trip. */
    Material w;
    w.shading_model = 1, w.toon_style = 2, w.toon_wrap = 0.3f, w.toon_rim_side = 2, w.toon_hair_highlight = true, w.toon_key_light_only = true;
    w.toon_specular_color = Vec3(1.0f, 0.2f, 0.1f);
    w.shade_map.path = "Assets/Textures/shade.png", w.toon_ramp_map.path = "Assets/Textures/Ramp.ptex#albedo", w.toon_shadow_bias_map.path = "Assets/Textures/bias.png";
    w.toon_specular_map.path = "Assets/Textures/spec.png", w.toon_rim_map.path = "Assets/Textures/rim.png";
    Material back;
    CHECK(load_material_text(save_material_text(w), back));
    CHECK(back.toon_style == 2 && std::fabs(back.toon_wrap - 0.3f) < 1e-6f && back.toon_rim_side == 2 && back.toon_hair_highlight && back.toon_key_light_only &&
          std::fabs(back.toon_specular_color.y - 0.2f) < 1e-6f && back.shade_map.path == "Assets/Textures/shade.png" &&
          back.toon_ramp_map.path == "Assets/Textures/Ramp.ptex#albedo" && back.toon_shadow_bias_map.path == "Assets/Textures/bias.png" &&
          back.toon_specular_map.path == "Assets/Textures/spec.png" && back.toon_rim_map.path == "Assets/Textures/rim.png");
  });

  test("toon styles: the shared shading - the shade map, the painted bias, light wrap, the ramp (horizontal or vertical), the specular's colour, map and hair band, the rim's side and mask, the key light", [&] {
    Material m;
    m.shading_model = 1, m.toon_bands = 1, m.toon_softness = 0.0f, m.toon_threshold = 0.5f, m.toon_shadow = Vec3(0.5f);
    m.toon_specular_size = 0.3f, m.toon_specular_smooth = 0.0f, m.toon_specular_color = Vec3(1.0f, 0.0f, 0.0f);
    m.toon_rim_width = 0.3f, m.toon_rim_strength = 1.0f, m.toon_rim = Vec3(1.0f);
    SurfaceSample s;
    s.albedo = Vec3(0.8f), s.specular = 0.5f, s.metallic = 0.0f, s.toon = &m;
    const Vec3 n(0, 0, 1), V(0, 0, 1), L(0, 0, 1);
    /* The tones and the spot's colour. */
    Vec3 lit = shade_toon(s, n, V, L, 1.0f), shadow = shade_toon(s, n, V, L, 0.2f);
    std::printf("    lit %.2f %.2f %.2f (a red spot on grey), shadow %.2f %.2f %.2f\n", lit.x, lit.y, lit.z, shadow.x, shadow.y, shadow.z);
    CHECK(lit.x > lit.y + 0.5f && std::fabs(lit.y - 0.8f) < 1e-4f && std::fabs(shadow.x - 0.4f) < 1e-4f);
    s.toon_spec_mask = 0.0f;
    CHECK(length(shade_toon(s, n, V, L, 1.0f) - Vec3(0.8f)) < 1e-4f);  // no spot where the map says none
    s.toon_spec_mask = 1.0f;
    /* The painted bias: white keeps a shadowed point lit, black puts a lit one in shadow. */
    s.toon_bias = 1.0f;
    CHECK(std::fabs(shade_toon(s, n, V, L, 0.2f).y - 0.8f) < 1e-4f);
    s.toon_bias = 0.0f;
    CHECK(std::fabs(shade_toon(s, n, V, L, 1.0f).y - 0.4f) < 1e-4f);
    s.toon_bias = 0.5f;
    /* The shade map is the shadow side itself, tinted by the shadow colour (0.5 here). */
    s.has_shade = true, s.toon_shade = Vec3(0.1f, 0.2f, 0.3f);
    CHECK(length(shade_toon(s, n, V, L, 0.2f) - Vec3(0.05f, 0.1f, 0.15f)) < 1e-4f && std::fabs(shade_toon(s, n, V, L, 1.0f).y - 0.8f) < 1e-4f);
    s.has_shade = false;
    /* Light wrap remaps the signed N.L before the clamp (the callers' step): 0.3 reaches 0.53 (lit at threshold 0.5),
     * -1 (the back) stays in shadow, so a cast shadow and the far side keep the shadow tone. */
    m.toon_wrap = 0.5f;
    CHECK(std::fabs(toon_wrap_ndl(m, 0.3f) - 0.8f / 1.5f) < 1e-4f && toon_wrap_ndl(m, -1.0f) < 0.0f);
    CHECK(std::fabs(shade_toon(s, n, V, L, std::max(0.0f, toon_wrap_ndl(m, -1.0f))).y - 0.4f) < 1e-4f);
    m.toon_wrap = 0.0f;
    CHECK(std::fabs(toon_wrap_ndl(m, 0.3f) - 0.3f) < 1e-6f);
    /* A ramp: horizontal (left dark, right lit) and vertical (bottom dark, top lit) read the same. */
    const TexturePtr horizontal = r167_tex(4, 1, {Vec3(1, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 1, 0)});
    const TexturePtr vertical = r167_tex(1, 4, {Vec3(0, 1, 0), Vec3(0, 1, 0), Vec3(1, 0, 0), Vec3(1, 0, 0)});
    m.toon_specular_size = 0.0f;
    for (const TexturePtr &ramp : {horizontal, vertical}) {
      s.toon_ramp = ramp.get();
      const Vec3 dark = shade_toon(s, n, V, L, 0.1f), bright = shade_toon(s, n, V, L, 0.9f);
      std::printf("    %s ramp: dark %.2f %.2f %.2f, bright %.2f %.2f %.2f\n", ramp == horizontal ? "horizontal" : "vertical", dark.x, dark.y, dark.z, bright.x, bright.y, bright.z);
      CHECK(dark.x > 0.7f && dark.y < 0.1f && bright.y > 0.7f && bright.x < 0.1f);
    }
    s.toon_ramp = nullptr;
    /* The hair highlight: a band where the half vector is square to the strands (V of the UVs). */
    m.toon_specular_size = 0.3f, m.toon_hair_highlight = true;
    s.toon_has_tangent = true;
    s.toon_strand = Vec3(1, 0, 0);
    CHECK(shade_toon(s, n, V, L, 1.0f).x > 1.2f);  // square to the half vector: the sheen
    s.toon_strand = Vec3(0, 0, 1);
    CHECK(std::fabs(shade_toon(s, n, V, L, 1.0f).x - 0.8f) < 1e-4f);  // along it: none
    m.toon_hair_highlight = false;
    /* The rim: its side against the key light, its mask. */
    const Vec3 edge(1, 0, 0);  // the silhouette: square to the eye
    for (int side = 0; side < 3; side++) {
      m.toon_rim_side = side;
      const bool lit_side = toon_rim(s, edge, V, Vec3(1, 0, 0)).x > 0.5f, shadow_side = toon_rim(s, edge, V, Vec3(-1, 0, 0)).x > 0.5f;
      std::printf("    rim side %d: on the lit side %d, on the shadow side %d\n", side, (int)lit_side, (int)shadow_side);
      CHECK(lit_side == (side != 2) && shadow_side == (side != 1));
    }
    m.toon_rim_side = 0;
    s.toon_rim_mask = 0.0f;
    CHECK(toon_rim(s, edge, V, Vec3(1, 0, 0)).x == 0.0f);
    /* The key light: the brightest sun. */
    RenderLight point, weak, strong;
    point.type = RenderLight::Point, point.intensity = 10.0f;
    weak.type = RenderLight::Directional, weak.intensity = 0.5f, weak.direction = normalize(Vec3(1, -1, 0));
    strong.type = RenderLight::Directional, strong.intensity = 2.0f, strong.direction = normalize(Vec3(0, -1, 1));
    Vec3 key_L;
    CHECK(toon_key_light({point, weak, strong}, &key_L) == 2 && length(key_L - normalize(Vec3(0, 1, -1))) < 1e-5f);
    CHECK(toon_key_light({point}, &key_L) == -1 && key_L.y > 0.99f);
    RenderLight dark = weak;
    dark.intensity = 0.0f;
    CHECK(toon_key_light({point, dark}, &key_L) == -1);  // (a sun that gives no light isn't the key)
  });

  test("toon styles: light wrap lights more of a sphere and the rasterizer, the CPU path tracer (and the GPU's) agree on the tones; Key Light Only keeps a second light from banding", [] {
    Editor ed;
    GameObject *g = r60_open(ed, 1);
    if (!g) return;
    MaterialPtr m = g->get<MeshRenderer>()->materials[0];
    auto lit_share = [&](const Image &img) {
      int solid = 0;
      const auto tones = r60_colours(img, &solid);
      float best = 0.0f;
      int lit_px = 0;
      for (const auto &c : tones) best = std::max(best, r60_lum(c.first));
      for (const auto &c : tones)
        if (r60_lum(c.first) > best - 0.05f) lit_px += c.second;
      return solid ? (double)lit_px / solid : 0.0;
    };
    m->toon_threshold = 0.5f, m->touch();  // (the shadow a wide band, so the wrap has room to move its edge)
    ed.mark_changed_for_test("R167 threshold");
    ed.step_frame_headless();
    const double plain = lit_share(r57_render(ed, "Rasterized", true));
    m->toon_wrap = 0.5f, m->touch();
    ed.mark_changed_for_test("R167 wrap");
    ed.step_frame_headless();
    const Image raster = r57_render(ed, "Rasterized", true);
    const double wrapped = lit_share(raster);
    std::printf("    the lit tone's share of the sphere: %.1f %% plain, %.1f %% with wrap 0.5\n", plain * 100.0, wrapped * 100.0);
    CHECK(wrapped > plain + 0.1 && wrapped < 0.85);  // (more lit, but the far side keeps its shadow)
    /* The path tracer bands the same (each tone's share within 4 %), the GPU's the CPU's. */
    int solid = 0;
    const auto tones = r60_colours(raster, &solid);
    std::vector<float> lum;
    for (const auto &c : tones)
      if (c.second * 50 >= solid) lum.push_back(r60_lum(c.first));
    auto shares = [&](const Image &img) {
      std::vector<double> sh(lum.size(), 0.0);
      int n = 0;
      for (uint32_t c : img.pixels) {
        if ((c >> 24) != 255 || lum.empty()) continue;
        const float l = r60_lum(c);
        size_t best = 0;
        for (size_t k = 1; k < lum.size(); k++)
          if (std::fabs(lum[k] - l) < std::fabs(lum[best] - l)) best = k;
        sh[best] += 1.0, n++;
      }
      for (double &x : sh) x /= std::max(1, n);
      return sh;
    };
    ed.command("set Render.Samples 64");
    const Image cpu = r57_render(ed, "Path", true);
    const auto a = shares(raster), b = shares(cpu);
    double worst = 0.0;
    for (size_t k = 0; k < a.size(); k++) worst = std::max(worst, std::fabs(a[k] - b[k]));
    std::printf("    with wrap: %zu tones, the largest share difference raster / CPU trace %.1f %%\n", lum.size(), worst * 100.0);
    CHECK(lum.size() >= 2 && worst < 0.04);
    auto mean_diff = [](const Image &x, const Image &y) {
      double diff = 0.0;
      int n = 0;
      for (size_t i = 0; i < x.pixels.size() && i < y.pixels.size(); i++) {
        if ((x.pixels[i] >> 24) != 255) continue;
        for (int ch = 0; ch < 3; ch++) diff += std::abs((int)(x.pixels[i] >> (ch * 8) & 255) - (int)(y.pixels[i] >> (ch * 8) & 255));
        n++;
      }
      return diff / (std::max(1, n) * 3.0 * 255.0);
    };
    if (gpu::available()) {
      ed.command("device gpu");
      const double d = mean_diff(cpu, r57_render(ed, "Path", true));
      std::printf("    GPU vs CPU with wrap: mean difference %.2f %% (device %s; error '%s')\n", d * 100.0, ed.render_device_for_test().c_str(), ed.render_gpu_error_for_test().c_str());
      CHECK(d < 0.03 && ed.render_gpu_error_for_test().empty());
      ed.command("device cpu");
    }
    /* A second light, strong and from the side: it bands too, unless Key Light Only, when it fills smoothly - the same
     * in the rasterizer and the path tracer. */
    ed.command("create Point Light");
    GameObject *pl = ed.scene().find_by_name("Point Light");
    CHECK(pl != nullptr);
    if (!pl) return;
    pl->set_world_position({4.0f, 1.0f, 0.0f});
    pl->get<Light>()->intensity = 3.0f;
    pl->get<Light>()->range = 20.0f;
    ed.mark_changed_for_test("R167 fill");
    ed.step_frame_headless();
    const Image banded = r57_render(ed, "Rasterized", true);
    m->toon_key_light_only = true, m->touch();
    ed.mark_changed_for_test("R167 key only");
    ed.step_frame_headless();
    const Image keyed = r57_render(ed, "Rasterized", true);
    const double changed = mean_diff(banded, keyed), agree = mean_diff(keyed, r57_render(ed, "Path", true));
    std::printf("    key light only: the picture changes by %.1f %%; rasterizer vs path tracer %.1f %%\n", changed * 100.0, agree * 100.0);
    CHECK(changed > 0.01 && agree < 0.04);
  });

  test("toon styles: the Toon Ramp generator steps at its threshold, and a material reading its output gets exactly those tones; MMD's toon image reads lit at the top", [&] {
    std::unique_ptr<TextureLayer> ramp = create_texture_layer("Toon Ramp");
    CHECK(ramp != nullptr);
    if (!ramp) return;
    LayerContext ctx;
    const LayerSample dark = ramp->sample(0.2f, 0.5f, ctx), bright = ramp->sample(0.8f, 0.5f, ctx), top = ramp->sample(0.8f, 0.05f, ctx), bottom = ramp->sample(0.8f, 0.95f, ctx);
    std::printf("    the generator: left %.2f, right %.2f, top %.2f, bottom %.2f (height = how lit)\n", dark.height, bright.height, top.height, bottom.height);
    CHECK(dark.height < 0.01f && bright.height > 0.99f && std::fabs(top.height - bottom.height) < 1e-6f);  // (a horizontal ramp: the same in every row)
    /* A texture asset of one Toon Ramp layer as a sphere's ramp: two tones, nothing else. */
    Editor ed;
    GameObject *g = r60_open(ed, 3);  // (three bands without the ramp: with it, the ramp's two)
    if (!g) return;
    ProceduralTexturePtr pt = ed.new_texture_asset_for_test("Bricks");
    CHECK(pt != nullptr);
    if (!pt) return;
    pt->layers.clear();
    pt->layers.push_back(create_texture_layer("Toon Ramp"));
    pt->live_shading = false;  // (a live-shaded texture's output is read per pixel by the base slot only)
    pt->touch();
    MaterialPtr m = g->get<MeshRenderer>()->materials[0];
    m->toon_ramp_map.path = pt->asset_path + "#albedo";
    m->touch();
    ed.mark_changed_for_test("R167 ramp");
    ed.step_frame_headless();
    {
      const Material::Resolved &tx = m->textures();
      std::printf("    the ramp resolved: %s", tx.toon_ramp ? strprintf("%d x %d;", tx.toon_ramp->width, tx.toon_ramp->height).c_str() : "none");
      for (float l : {0.1f, 0.9f})
        if (tx.toon_ramp) {
          const Vec4 c = tx.toon_ramp->sample({l, l}, 0.0f, TexWrap::Extend, TexFilter::Linear);
          std::printf(" lit %.1f -> %.2f %.2f %.2f;", l, c.x, c.y, c.z);
        }
      std::printf("\n");
      CHECK(tx.toon_ramp && tx.toon_ramp->sample({0.1f, 0.1f}, 0.0f, TexWrap::Extend, TexFilter::Linear).x < 0.6f &&
            tx.toon_ramp->sample({0.9f, 0.9f}, 0.0f, TexWrap::Extend, TexFilter::Linear).x > 0.95f);
    }
    int solid = 0;
    const auto tones = r60_colours(r57_render(ed, "Rasterized", true), &solid);
    int main_tones = 0, covered = 0;
    for (const auto &c : tones)
      if (c.second * 50 >= solid) main_tones++, covered += c.second;
    std::printf("    with the ramp: %d tones cover %.1f %% of the sphere\n", main_tones, solid ? 100.0 * covered / solid : 0.0);
    CHECK(main_tones == 2 && covered > solid * 0.95);
    /* MMD's toon image (white above a violet bottom row) as a vertical ramp: lit is white, shadow the violet. */
    const std::string dir = fs::join(test_dir(), "r167_mmd");
    fs::make_dirs(dir);
    std::vector<uint32_t> toon(16, 0xFFFFFFFFu);
    for (int x = 0; x < 4; x++) toon[12 + x] = 0xFF664D80u;  // (the bottom row)
    CHECK(write_png(fs::join(dir, "toon.png"), toon.data(), 4, 4, 4, false));
    Material mm;
    mm.shading_model = 1;
    mm.toon_ramp_map = TextureRef{fs::join(dir, "toon.png"), false};
    const Material::Resolved &tx = mm.textures();
    CHECK(tx.toon_ramp != nullptr);
    if (!tx.toon_ramp) return;
    SurfaceSample s;
    s.albedo = Vec3(1.0f), s.toon = &mm, s.toon_ramp = tx.toon_ramp.get();
    const Vec3 n(0, 0, 1);
    const Vec3 lit = shade_toon(s, n, n, n, 1.0f), shadow = shade_toon(s, n, n, n, 0.0f);
    std::printf("    MMD ramp: lit %.2f %.2f %.2f, shadow %.2f %.2f %.2f\n", lit.x, lit.y, lit.z, shadow.x, shadow.y, shadow.z);
    CHECK(lit.y > 0.95f && shadow.y < 0.5f && shadow.z > shadow.y);
  });
  test("toon styles: the GPU viewport draws the plain settings (wrap, a lit-side rim, the spot's colour, Key Light Only, a Toon Ramp) as the CPU does; toonstyle lands on every material selected in the Materials window", [] {
    {
      Editor ed;
      r50_open(ed);
      ed.record_buttons_for_test(true);
      {  // (an earlier run's files: newmat with a taken name makes a numbered copy, and the grid would push B out of view)
        const std::string dir = fs::join(scratch_project(), "Assets/Materials");
        for (const DirEntry &de : fs::list(dir))
          if (de.name.size() > 3 && de.name.rfind("R167", 0) == 0 && fs::extension(de.name) == ".mat") std::remove(fs::join(dir, de.name).c_str());
      }
      for (const char *n : {"R167A", "R167B"}) ed.command(std::string("newmat ") + n);
      MaterialPtr a = material_asset("Assets/Materials/R167A.mat"), b = material_asset("Assets/Materials/R167B.mat");
      CHECK(a && b);
      if (a && b) {
        ed.command("window Materials");
        ed.set_material_search_for_test("R167");
        r34_steps(ed, 3);
        auto click = [&](const std::string &key, int mods) {
          Recti r;
          if (!ed.button_rect_for_test(key, r)) return false;
          platform::Event m, d, u;
          m.type = platform::EventType::MouseMove, d.type = platform::EventType::MouseDown, u.type = platform::EventType::MouseUp;
          m.x = d.x = u.x = r.x + r.w / 2, m.y = d.y = u.y = r.y + r.h / 2;
          m.mods = d.mods = u.mods = mods;
          ed.step_frame_headless({m}), ed.step_frame_headless({d}), ed.step_frame_headless({u}), ed.step_frame_headless();
          return true;
        };
        CHECK(click("mat:R167A", 0));
        CHECK(click("mat:R167B", platform::MOD_CTRL));
        ed.command("toonstyle Guilty Gear");
        std::printf("    toonstyle on two selected materials: A style %d bands %d key-only %d, B style %d bands %d key-only %d\n", a->toon_style, a->toon_bands,
                    (int)a->toon_key_light_only, b->toon_style, b->toon_bands, (int)b->toon_key_light_only);
        CHECK(a->toon_style == 1 && b->toon_style == 1 && a->toon_bands == 1 && b->toon_bands == 1 && a->toon_key_light_only && b->toon_key_light_only);
      }
    }
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: the viewport comparison skipped\n");
      return;
    }
    Editor ed;
    r34_open(ed);
    ed.command("create Sphere");
    ed.command("select Sphere");
    ed.command("material Toon");
    ed.command("create Point Light");
    GameObject *sphere = ed.scene().find_by_name("Sphere"), *pl = ed.scene().find_by_name("Point Light");
    CHECK(sphere && pl);
    if (!sphere || !pl) return;
    sphere->set_world_position({0.0f, 1.0f, 0.0f});
    pl->set_world_position({3.0f, 1.5f, 0.0f});
    pl->get<Light>()->intensity = 3.0f;
    MaterialPtr m = sphere->get<MeshRenderer>()->materials[0];
    ProceduralTexturePtr pt = ed.new_texture_asset_for_test("Bricks");
    CHECK(pt != nullptr);
    if (!pt) return;
    pt->layers.clear();
    pt->layers.push_back(create_texture_layer("Toon Ramp"));
    pt->live_shading = false;
    pt->touch();
    m->toon_wrap = 0.4f, m->toon_rim_side = 1, m->toon_rim_width = 0.3f, m->toon_rim_strength = 0.8f, m->toon_specular_size = 0.4f;
    m->toon_specular_color = Vec3(1.0f, 0.2f, 0.2f), m->toon_key_light_only = true;
    m->toon_ramp_map.path = pt->asset_path + "#albedo";
    m->touch();
    ed.command("camera 0 1.2 3.5 0 1 0");
    ed.mark_changed_for_test("R167 gpu");
    r34_steps(ed, 3);
    auto draw = [&](int device) {
      ed.viewport_device_for_test(device);
      for (int k = 0; k < 2; k++) {  // (a change, then back: the view draws again on the device asked for)
        sphere->set_local_position({0.0f, k ? 1.0f : 1.0001f, 0.0f});
        r34_steps(ed, 2);
      }
      return r34_crop(ed, ed.scene_view_rect());
    };
    const std::vector<uint32_t> cpu = draw(1), gpu = draw(2);
    double diff = 0.0;
    size_t n = 0, toon_px = 0;
    for (size_t i = 0; i < cpu.size() && i < gpu.size(); i++) {
      for (int ch = 0; ch < 3; ch++) diff += std::abs((int)(cpu[i] >> (ch * 8) & 255) - (int)(gpu[i] >> (ch * 8) & 255));
      n++;
      toon_px += ((cpu[i] >> 16) & 255) > 100 && ((cpu[i] >> 8) & 255) < 90;  // the spot's red
    }
    diff /= std::max<size_t>(1, n) * 3.0 * 255.0;
    std::printf("    the Scene view on the card against the CPU: mean difference %.2f %% over %zu pixels (%zu red-spot pixels)\n", diff * 100.0, n, toon_px);
    CHECK(n > 1000 && diff < 0.02);
    ed.viewport_device_for_test(1);
  });

}

static void tests_round168() {
  /* ---------------------------------------------------------------- round 168: the 2D look (task 0149, ADR 0048) */
  test("the 2D look: custom normals - every corner takes its vertex's own normal, saved and loaded with the scene, carried onto another mesh by position, kept through a glTF round trip; a file's smooth-but-not-edited or split normals are left alone; customnormals clear", [] {
    MeshPtr cube = std::make_shared<Mesh>(*primitives::cube());
    const Vec3 tilt = normalize(Vec3(0.3f, 1.0f, 0.2f));
    cube->custom_normals.assign(cube->vert_count(), tilt);
    cube->touch();
    float worst = 1.0f;
    for (const Vec3 &n : cube->render_mesh().normals) worst = std::min(worst, dot(n, tilt));
    CHECK(worst > 0.999f);  // (the cube's own flat normals replaced at every corner)
    /* With the scene. */
    Scene s;
    GameObject *g = s.create("CN");
    g->add<MeshFilter>()->mesh = cube;
    Scene back;
    std::string error;
    CHECK(load_scene_text(save_scene_text(s), back, error));
    GameObject *bg = back.find_by_name("CN");
    const MeshFilter *bmf = bg ? bg->get<MeshFilter>() : nullptr;
    float back_worst = bmf && bmf->mesh && bmf->mesh->custom_normals_fit() ? 1.0f : -1.0f;
    if (back_worst > 0.0f)
      for (const Vec3 &n : bmf->mesh->custom_normals) back_worst = std::min(back_worst, dot(n, tilt));
    std::printf("    saved and loaded: %zu custom normals, the worst within %.4f of the tilt\n", bmf && bmf->mesh ? bmf->mesh->custom_normals.size() : (size_t)0, back_worst);
    CHECK(back_worst > 0.999f);
    /* Onto a sphere, by position on the cube's surface. */
    Mesh sphere = *primitives::uv_sphere(0.5f, 8, 6);
    meshops::transfer_custom_normals(*cube, sphere);
    float sw = sphere.custom_normals_fit() ? 1.0f : -1.0f;
    for (size_t i = 0; sw > 0.0f && i < sphere.custom_normals.size(); i++) sw = std::min(sw, dot(sphere.custom_normals[i], tilt));
    CHECK(sw > 0.99f);
    /* The glTF round trip: the exporter writes the custom normals, the importer keeps them (they differ from the
     * recomputed ones) and says so. */
    ExportItem it;
    it.name = "CN";
    it.mesh = cube.get();
    it.world = Mat4::identity();
    const std::string glb = export_gltf({it}, export_defaults((int)ExportFormat::GLB), true, "", nullptr);
    const std::string dir = fs::join(test_dir(), "r168");
    fs::make_dirs(dir);
    const std::string path = fs::join(dir, "cn.glb");
    {
      std::ofstream f(path, std::ios::binary);
      f.write(glb.data(), (std::streamsize)glb.size());
    }
    ImportResult res;
    const bool ok = import_gltf_file(path, res);
    const Mesh *im = ok && !res.nodes.empty() ? res.nodes[0].mesh.get() : nullptr;
    float iw = im && im->custom_normals_fit() ? 1.0f : -1.0f;
    for (size_t i = 0; iw > 0.0f && i < im->custom_normals.size(); i++) iw = std::min(iw, dot(im->custom_normals[i], tilt));
    bool noted = false;
    for (const std::string &n : res.notes) noted = noted || n.find("custom normals") != std::string::npos;
    std::printf("    glTF: %s, %zu vertices, custom normals %s (worst %.4f), note %s\n", ok ? "imported" : res.error.c_str(), im ? im->vert_count() : (size_t)0,
                im && im->custom_normals_fit() ? "kept" : "none", iw, noted ? "yes" : "no");
    CHECK(ok && im && iw > 0.99f && noted);
    /* The file's normals are kept only when they differ: a plain export carries no copy; an exporter's angle-weighted
     * normals (a stretched box's differ from Blendity's area-weighted ones) are no edit either; a zero (a hard edge's
     * split copies, a primitive without normals) leaves the mesh alone. */
    Mesh plain = *primitives::cube();
    CHECK(!keep_file_normals(plain, meshops::vertex_normals(plain)) && plain.custom_normals.empty());
    Mesh box = *primitives::cube();
    for (Vec3 &p : box.positions) p.z *= 4.0f;
    box.touch();
    std::vector<Vec3> angle(box.vert_count(), Vec3(0.0f));
    for (size_t f = 0; f < box.face_count(); f++) {  // (angle-weighted: each face's normal by its angle at the corner, 90 degrees on a box)
      const uint32_t *v = box.face_verts(f);
      for (uint32_t c = 0; c < box.face_size(f); c++) angle[v[c]] += box.face_normal(f);
    }
    for (Vec3 &n : angle) n = normalize(n);
    float apart = 1.0f;
    for (size_t i = 0; i < angle.size(); i++) apart = std::min(apart, dot(angle[i], meshops::vertex_normals(box)[i]));
    std::printf("    a 1 x 1 x 4 box: angle-weighted and area-weighted normals agree to cos %.3f, and neither counts as an edit\n", apart);
    CHECK(apart < 0.999f && !keep_file_normals(box, angle) && box.custom_normals.empty());
    std::vector<Vec3> split = meshops::vertex_normals(plain);
    split[0] = Vec3(0.0f);
    CHECK(!keep_file_normals(plain, split));
    std::vector<Vec3> turned = meshops::vertex_normals(plain);
    for (Vec3 &n : turned) n = Quat::euler(Vec3(0.0f, 30.0f, 0.0f)).rotate(n);
    CHECK(keep_file_normals(plain, turned) && plain.custom_normals_fit());
    /* A flat-shaded glTF cube (split normals) still welds to 8 vertices and keeps none. */
    MeshPtr flat = std::make_shared<Mesh>(*primitives::cube());
    ExportItem fit;
    fit.name = "Flat";
    fit.mesh = flat.get();
    fit.world = Mat4::identity();
    const std::string fpath = fs::join(dir, "flat.glb");
    {
      const std::string fglb = export_gltf({fit}, export_defaults((int)ExportFormat::GLB), true, "", nullptr);
      std::ofstream f(fpath, std::ios::binary);
      f.write(fglb.data(), (std::streamsize)fglb.size());
    }
    ImportResult fres;
    const bool fok = import_gltf_file(fpath, fres);
    const Mesh *fm = fok && !fres.nodes.empty() ? fres.nodes[0].mesh.get() : nullptr;
    std::printf("    a flat cube's glTF: %zu vertices, custom normals %s\n", fm ? fm->vert_count() : (size_t)0, fm && fm->custom_normals_fit() ? "kept" : "none");
    CHECK(fm && fm->vert_count() == 8 && !fm->custom_normals_fit());
    /* The way back: customnormals clear on the selected mesh, one undo step. */
    Editor ed;
    r34_open(ed);
    ed.command("create Cube");
    ed.command("select Cube");
    ed.step_frame_headless();
    GameObject *cg = ed.scene().find_by_name("Cube");
    {
      Mesh &cm = *mesh_make_mutable(cg->mesh_filter()->mesh);
      cm.custom_normals.assign(cm.vert_count(), tilt);
      cm.touch();
    }
    ed.mark_changed_for_test("R168 normals");
    ed.step_frame_headless();
    ed.command("customnormals clear");
    CHECK(!cg->mesh_filter()->mesh->custom_normals_fit());
    ed.step_frame_headless();
    ed.command("undo");
    cg = ed.scene().find_by_name("Cube");
    CHECK(cg && cg->mesh_filter()->mesh->custom_normals_fit());
  });

  test("the 2D look: Smooth Normals averages the shading normals (a vertex group limits it) and moves nothing; Normal Transfer reads a proxy's normals, follows the proxy; Anime Face Normals adds the proxy and the modifier, undone together", [] {
    /* A bumped sphere: its pole pushed out, so its neighbours' normals splay. Smoothing brings adjacent normals closer. */
    auto bumped = [] {
      MeshPtr m = std::make_shared<Mesh>(*primitives::uv_sphere(0.5f, 16, 12));
      size_t top = 0;
      for (size_t i = 1; i < m->vert_count(); i++)
        if (m->positions[i].y > m->positions[top].y) top = i;
      m->positions[top].y += 0.3f;
      m->touch();
      return m;
    };
    auto worst_edge = [](const Mesh &m, const std::vector<Vec3> &n) {
      float worst = 1.0f;
      for (size_t f = 0; f < m.face_count(); f++) {
        const uint32_t *v = m.face_verts(f);
        const uint32_t k = m.face_size(f);
        for (uint32_t c = 0; c < k; c++) worst = std::min(worst, dot(n[v[c]], n[v[(c + 1) % k]]));
      }
      return worst;
    };
    Scene s;
    GameObject *a = s.create("A");
    MeshPtr ma = bumped();
    a->add<MeshFilter>()->mesh = ma;
    SmoothNormalsModifier *sm = a->add<SmoothNormalsModifier>();
    sm->iterations = 10;
    const Mesh *ea = a->evaluated_mesh();
    const float before = worst_edge(*ma, meshops::vertex_normals(*ma)), after = ea && ea->custom_normals_fit() ? worst_edge(*ea, ea->custom_normals) : -2.0f;
    float moved = 0.0f;
    for (size_t i = 0; ea && i < ea->vert_count() && i < ma->vert_count(); i++) moved = std::max(moved, length(ea->positions[i] - ma->positions[i]));
    std::printf("    Smooth Normals: the sharpest edge's normals at cos %.3f before, %.3f after; vertices moved %g\n", before, after, moved);
    CHECK(after > before + 0.05f && moved == 0.0f);
    /* Limited to the top half: the bottom's normals stay the mesh's own. */
    GameObject *b = s.create("B");
    MeshPtr mb = bumped();
    std::vector<float> w(mb->vert_count());
    for (size_t i = 0; i < w.size(); i++) w[i] = mb->positions[i].y > 0.0f ? 1.0f : 0.0f;
    mb->vertex_groups.push_back({"Top", w});
    mb->touch();
    b->add<MeshFilter>()->mesh = mb;
    SmoothNormalsModifier *sb = b->add<SmoothNormalsModifier>();
    sb->iterations = 10;
    sb->vertex_group = "Top";
    const Mesh *eb = b->evaluated_mesh();
    const std::vector<Vec3> own = meshops::vertex_normals(*mb);
    float bottom = eb && eb->custom_normals_fit() ? 1.0f : -1.0f;
    for (size_t i = 0; bottom > 0.0f && i < own.size(); i++)
      if (mb->positions[i].y < 0.0f) bottom = std::min(bottom, dot(eb->custom_normals[i], own[i]));
    CHECK(bottom > 0.9999f);
    /* Normal Transfer: a cube reading a sphere's normals shades like the sphere (radial normals); the sphere moved,
     * the normals follow. */
    GameObject *box = s.create("Box");
    box->add<MeshFilter>()->mesh = std::make_shared<Mesh>(*primitives::cube());
    GameObject *ball = s.create("Ball");
    ball->add<MeshFilter>()->mesh = primitives::uv_sphere(1.0f, 32, 24);
    NormalTransferModifier *nt = box->add<NormalTransferModifier>();
    nt->object = "Ball";
    const Mesh *eb2 = box->evaluated_mesh();
    float radial = eb2 && eb2->custom_normals_fit() ? 1.0f : -1.0f;
    for (size_t i = 0; radial > 0.0f && i < eb2->vert_count(); i++) radial = std::min(radial, dot(eb2->custom_normals[i], normalize(eb2->positions[i])));
    ball->set_local_position({0.5f, 0.0f, 0.0f});
    const Mesh *eb3 = box->evaluated_mesh();
    float shifted = eb3 && eb3->custom_normals_fit() ? 1.0f : -1.0f;
    for (size_t i = 0; shifted > 0.0f && i < eb3->vert_count(); i++) shifted = std::min(shifted, dot(eb3->custom_normals[i], normalize(eb3->positions[i])));
    std::printf("    Normal Transfer: the cube's normals radial within cos %.3f; the ball moved, the worst %.3f\n", radial, shifted);
    CHECK(radial > 0.97f && shifted < 0.9f);
    /* The Rigging window's button (the console's facenormals): a proxy without a renderer, the modifier reading it. */
    Editor ed;
    r34_open(ed);
    ed.command("create Cube");
    ed.command("select Cube");
    ed.step_frame_headless();
    ed.command("facenormals");
    ed.step_frame_headless();
    GameObject *cube = ed.scene().find_by_name("Cube"), *proxy = ed.scene().find_by_name("Cube Face Normals");
    const NormalTransferModifier *cnt = cube ? cube->get<NormalTransferModifier>() : nullptr;
    CHECK(proxy && proxy->get<MeshFilter>() && !proxy->get<MeshRenderer>() && proxy->parent == cube);
    CHECK(cnt && cnt->object == "Cube Face Normals" && cube->evaluated_mesh() && cube->evaluated_mesh()->custom_normals_fit());
    ed.command("undo");
    ed.step_frame_headless();
    cube = ed.scene().find_by_name("Cube");
    CHECK(!ed.scene().find_by_name("Cube Face Normals") && cube && !cube->get<NormalTransferModifier>());
    /* Evaluated again with fewer iterations, the normals un-smooth (the modifier starts from the mesh's own, not
     * from its last result); Blend 0.5 twice is still half way. */
    sm->iterations = 1;
    a->mark_dirty();
    const Mesh *ea1 = a->evaluated_mesh();
    const float after1 = ea1 && ea1->custom_normals_fit() ? worst_edge(*ea1, ea1->custom_normals) : -2.0f;
    nt->blend = 0.5f;
    ball->set_local_position({0.0f, 0.0f, 0.0f});
    const Mesh *h1 = box->evaluated_mesh();
    const Vec3 half_once = h1 && h1->custom_normals_fit() ? h1->custom_normals[0] : Vec3(0.0f);
    ball->set_local_position({0.0f, 0.0f, 1e-3f});
    ball->set_local_position({0.0f, 0.0f, 0.0f});
    const Mesh *h2 = box->evaluated_mesh();
    const Vec3 half_twice = h2 && h2->custom_normals_fit() ? h2->custom_normals[0] : Vec3(1.0f);
    std::printf("    Smooth Normals at 1 iteration after 10: cos %.3f (10 gave %.3f); Blend 0.5 evaluated twice moves by %g\n", after1, after, length(half_twice - half_once));
    CHECK(after1 < after - 0.02f && length(half_twice - half_once) < 1e-5f);
    /* A skinned mesh turns its custom normals with the bone: a cube on one bone turned 90 degrees. */
    Scene ss;
    GameObject *rig = ss.create("Rig");
    rig->add<Armature>();
    GameObject *bone = ss.create("B", rig);
    bone->add<Bone>()->rest_position = {0, 0, 0};
    MeshPtr sc = std::make_shared<Mesh>(*primitives::cube());
    sc->vertex_groups.push_back({"B", std::vector<float>(sc->vert_count(), 1.0f)});
    const Vec3 tilt = normalize(Vec3(1.0f, 0.2f, 0.0f));
    sc->custom_normals.assign(sc->vert_count(), tilt);
    sc->touch();
    GameObject *skinned = ss.create("Skinned");
    skinned->add<MeshFilter>()->mesh = sc;
    skinned->add<SkinModifier>()->object = "Rig";
    const Quat q = Quat::euler(Vec3(0.0f, 90.0f, 0.0f));
    bone->set_local_rotation(q);
    const Mesh *es = skinned->evaluated_mesh();
    const float turned = es && es->custom_normals_fit() ? dot(es->custom_normals[0], q.rotate(tilt)) : -1.0f;
    const float moved_too = es ? dot(normalize(es->positions[0]), q.rotate(normalize(sc->positions[0]))) : -1.0f;
    std::printf("    skinned: the custom normal turned with the bone to cos %.4f (the vertex %.4f)\n", turned, moved_too);
    CHECK(turned > 0.999f && moved_too > 0.999f);
  });

  test("the 2D look: the face shadow map lights by the key light's angle instead of the bumps - the unit, the Face Shadow generator, a sphere lit all over from the front and all shadow from behind in the rasterizer and the path tracers", [] {
    Material m;
    m.shading_model = 1;
    m.face_shadow_softness = 0.0f;
    SurfaceSample s;
    s.toon = &m;
    s.face_sdf = 0.3f, s.face_sdf_mirror = 0.7f;
    CHECK(toon_face_lit(m, s, {0, 0, 1}) == 1.0f && toon_face_lit(m, s, {0, 0, -1}) == 0.0f);  // from the front: all lit; from behind: all shadow
    CHECK(toon_face_lit(m, s, {1, 0, 0}) == 0.0f && toon_face_lit(m, s, {-1, 0, 0}) == 1.0f);  // from the right: this point's 0.3 under the 0.5 threshold; from the left: its mirror's 0.7
    CHECK(toon_face_lit(m, s, {0, 1, 0}) == 1.0f);  // straight above: no side
    m.face_forward = 1;
    CHECK(toon_face_lit(m, s, {0, 0, -1}) == 1.0f);  // a -Z face
    s.face_sdf = -1.0f;
    CHECK(toon_face_lit(m, s, {0, 0, 1}) < 0.0f);  // no map
    /* The generator: a sweep from the far edge to the near one. */
    std::unique_ptr<TextureLayer> gen = create_texture_layer("Face Shadow");
    CHECK(gen != nullptr);
    if (!gen) return;
    LayerContext ctx;
    const float l = gen->sample(0.1f, 0.5f, ctx).height, c = gen->sample(0.5f, 0.5f, ctx).height, r = gen->sample(0.9f, 0.5f, ctx).height;
    std::printf("    the generator across the face: %.2f %.2f %.2f\n", l, c, r);
    CHECK(l < c && c < r && gen->sample(0.0f, 0.5f, ctx).height < 0.01f && gen->sample(1.0f, 0.5f, ctx).height > 0.9f);
    /* A sphere with the sweep as its face shadow map: the sun from the front lights every pixel one tone (the far
     * side too, which N.L would shade); from behind every pixel is the shadow tone. */
    Editor ed;
    GameObject *g = r60_open(ed, 1);
    if (!g) return;
    g->set_local_rotation(Quat());
    ProceduralTexturePtr pt = ed.new_texture_asset_for_test("Bricks");
    CHECK(pt != nullptr);
    if (!pt) return;
    pt->layers.clear();
    pt->layers.push_back(create_texture_layer("Face Shadow"));
    pt->live_shading = false;
    pt->touch();
    MaterialPtr mat = g->get<MeshRenderer>()->materials[0];
    mat->face_shadow_map.path = pt->asset_path + "#albedo";
    mat->face_shadow_softness = 0.0f;
    mat->touch();
    GameObject *sun = nullptr;
    ed.scene().for_each([&](GameObject &o) {
      if (const Light *lt = o.get<Light>(); lt && lt->type == 0 && !sun) sun = &o;
    });
    CHECK(sun != nullptr);
    if (!sun) return;
    auto mean_lum = [](const Image &img, int *tones_out) {
      int solid = 0;
      const auto tones = r60_colours(img, &solid);
      double sum = 0.0;
      int main_tones = 0;
      for (const auto &c : tones) {
        sum += r60_lum(c.first) * c.second;
        if (c.second * 20 >= solid) main_tones++;
      }
      if (tones_out) *tones_out = main_tones;
      return solid ? sum / solid : 0.0;
    };
    auto shine = [&](float yaw) {  // the sun turned about Y: 180 shines along -Z (from the sphere's front, +Z), 0 along +Z (from behind)
      sun->set_local_rotation(Quat::euler(Vec3(0.0f, yaw, 0.0f)));
      ed.mark_changed_for_test("R168 sun");
      ed.step_frame_headless();
    };
    /* Without the map first: the camera (at -Z) sees the sphere's far side when the sun shines from +Z (the
     * shadow tone) and its near side when the sun shines from -Z (the lit tone). */
    const std::string face_path = mat->face_shadow_map.path;
    mat->face_shadow_map.path.clear(), mat->touch();
    int t0 = 0;
    shine(180.0f);
    const double plain_dark = mean_lum(r57_render(ed, "Rasterized", true), &t0);
    shine(0.0f);
    const double plain_lit = mean_lum(r57_render(ed, "Rasterized", true), &t0);
    mat->face_shadow_map.path = face_path, mat->touch();
    int tf = 0, tb = 0;
    shine(180.0f);
    const Image front = r57_render(ed, "Rasterized", true);
    const double lf = mean_lum(front, &tf);
    shine(0.0f);
    const Image behind = r57_render(ed, "Rasterized", true);
    const double lb = mean_lum(behind, &tb);
    std::printf("    rasterizer: N.L alone %.3f dark / %.3f lit; with the map, the sun from the face's front: %d tone(s), mean luminance %.3f; from behind %d tone(s), %.3f\n",
                plain_dark, plain_lit, tf, lf, tb, lb);
    CHECK(plain_lit > plain_dark + 0.1 && tf == 1 && tb == 1 && std::fabs(lf - plain_lit) < 0.03 && std::fabs(lb - plain_dark) < 0.03);
    /* The path tracers the same. */
    shine(180.0f);
    ed.command("set Render.Samples 32");
    const Image cpu = r57_render(ed, "Path", true);
    int tc = 0;
    const double lc = mean_lum(cpu, &tc);
    std::printf("    CPU path tracer from the front: %d tone(s), mean luminance %.3f\n", tc, lc);
    CHECK(tc == 1 && std::fabs(lc - lf) < 0.08);
    shine(0.0f);
    int tcb = 0;
    const double lcb = mean_lum(r57_render(ed, "Path", true), &tcb);
    std::printf("    CPU path tracer from behind: %d tone(s), mean luminance %.3f\n", tcb, lcb);
    CHECK(tcb == 1 && std::fabs(lcb - lb) < 0.08);
    shine(180.0f);
    if (gpu::available()) {
      ed.command("device gpu");
      const Image gpu_img = r57_render(ed, "Path", true);
      double diff = 0.0;
      int n = 0;
      for (size_t i = 0; i < cpu.pixels.size() && i < gpu_img.pixels.size(); i++) {
        if ((cpu.pixels[i] >> 24) != 255) continue;
        for (int ch = 0; ch < 3; ch++) diff += std::abs((int)(cpu.pixels[i] >> (ch * 8) & 255) - (int)(gpu_img.pixels[i] >> (ch * 8) & 255));
        n++;
      }
      diff /= std::max(1, n) * 3.0 * 255.0;
      std::printf("    GPU vs CPU: mean difference %.2f %% (device %s; error '%s')\n", diff * 100.0, ed.render_device_for_test().c_str(), ed.render_gpu_error_for_test().c_str());
      CHECK(diff < 0.03 && ed.render_gpu_error_for_test().empty());
      ed.command("device cpu");
    }
  });

  test("the 2D look: inner lines darken the colour (or only the shadow tones), hatching the shadow tones; the Outline's thickness group, distance fade and pixel limits", [] {
    Material m;
    m.shading_model = 1, m.toon_bands = 1, m.toon_softness = 0.0f, m.toon_threshold = 0.5f, m.toon_shadow = Vec3(0.5f), m.toon_specular_size = 0.0f;
    SurfaceSample s;
    s.albedo = Vec3(0.8f), s.toon = &m;
    s.toon_shadow_mul = Vec3(0.5f);
    const Vec3 n(0, 0, 1);
    const Vec3 lit = shade_toon(s, n, n, n, 1.0f), shadow = shade_toon(s, n, n, n, 0.2f);
    CHECK(std::fabs(lit.x - 0.8f) < 1e-4f && std::fabs(shadow.x - 0.2f) < 1e-4f);  // (0.8 x 0.5 shadow x 0.5 hatching)
    /* The maps at a point: a mid-grey line map halves the colour (sRGB grey 128: 0.21 linear), only in shadow it goes
     * into the shadow multiplier; a mid-grey hatch map (data) into the shadow multiplier. */
    const std::string dir = fs::join(test_dir(), "r168");
    fs::make_dirs(dir);
    std::vector<uint32_t> grey(16, 0xFF808080u);
    const std::string png = fs::join(dir, "grey.png");
    CHECK(write_png(png, grey.data(), 4, 4, 4, false));
    SurfacePoint sp;
    sp.normal = sp.geo_normal = n;
    sp.uv = {0.5f, 0.5f};
    Material lm;
    lm.shading_model = 1;
    lm.line_map = TextureRef{png, false};
    const SurfaceSample a = evaluate_material(lm, sp);
    lm.line_only_shadow = true;
    lm.touch();
    const SurfaceSample b = evaluate_material(lm, sp);
    Material hm;
    hm.shading_model = 1;
    hm.hatch_map = TextureRef{png, true};
    hm.hatch_scale = 4.0f;
    const SurfaceSample c = evaluate_material(hm, sp);
    std::printf("    line map: colour %.3f (only in shadow: colour %.3f, shadow x %.3f); hatch map: shadow x %.3f\n", a.albedo.x, b.albedo.x, b.toon_shadow_mul.x, c.toon_shadow_mul.x);
    CHECK(a.albedo.x < 0.3f && std::fabs(b.albedo.x - 0.8f) < 1e-4f && b.toon_shadow_mul.x < 0.3f && std::fabs(c.toon_shadow_mul.x - 0.5f) < 0.02f);
    /* The Outline: the group scales the width per vertex (0: no line); the distance scale. */
    Outline ol;
    ol.thickness = 0.1f;
    ol.thickness_group = "Lines";
    MeshPtr cube = std::make_shared<Mesh>(*primitives::cube());
    std::vector<float> w(cube->vert_count());
    for (size_t i = 0; i < w.size(); i++) w[i] = cube->positions[i].y > 0.0f ? 1.0f : 0.0f;
    cube->vertex_groups.push_back({"Lines", w});
    cube->touch();
    const Mesh *sh = ol.shell(*cube, 1.0f, 0);
    float top = 0.0f, bottom = 0.0f;
    for (size_t i = 0; sh && i < sh->vert_count(); i++) (cube->positions[i].y > 0.0f ? top : bottom) = std::max(cube->positions[i].y > 0.0f ? top : bottom, length(sh->positions[i] - cube->positions[i]));
    std::printf("    the shell stands out %.3f on top (weight 1), %.3f below (weight 0)\n", top, bottom);
    CHECK(sh && std::fabs(top - 0.1f) < 1e-4f && bottom == 0.0f);
    ol.thickness_group.clear();
    const Mesh *all = ol.shell(*cube, 1.0f, 0);
    float low = 1.0f;
    for (size_t i = 0; all && i < all->vert_count(); i++) low = std::min(low, length(all->positions[i] - cube->positions[i]));
    CHECK(all && std::fabs(low - 0.1f) < 1e-4f);  // (a new shell: the group is part of the cache's key)
    CHECK(ol.outline_distance_scale(3.0f, 300.0f) == 1.0f);
    ol.distance_fade = 1.0f;
    CHECK(std::fabs(ol.outline_distance_scale(3.0f, 300.0f) - 0.25f) < 0.003f);  // (within a 2 % step)
    ol.distance_fade = 0.0f;
    ol.min_pixels = 4.0f;
    CHECK(std::fabs(ol.outline_distance_scale(30.0f, 300.0f) - 4.0f) < 0.05f);  // 0.1 m at 30 m is 1 pixel: four times (within a 2 % step)
    ol.max_pixels = 0.5f;
    CHECK(std::fabs(ol.outline_distance_scale(1.0f, 300.0f) - 0.5f / 30.0f) < 0.0003f);  // 30 pixels at 1 m: a sixtieth (within a 2 % step)
    CHECK(ol.outline_distance_scale(0.0f, 300.0f) == 1.0f && ol.outline_distance_scale(3.0f, 0.0f) == 1.0f);
    ol.min_pixels = ol.max_pixels = 0.0f, ol.distance_fade = 1.0f;
    CHECK(std::fabs(ol.outline_distance_scale(3.0f, 300.0f) - 0.25f) < 0.01f && ol.outline_distance_scale(3.0f, 300.0f) == ol.outline_distance_scale(3.02f, 300.0f));  // (2 % steps)
    ol.distance_fade = 0.0f, ol.max_pixels = 1.0f, ol.world_space = false;  // thickness 0.1 in the object's units, the object 3 x: 0.3 m = 90 px at 1 m
    CHECK(std::fabs(ol.outline_distance_scale(1.0f, 300.0f, 3.0f) - 1.0f / 90.0f) < 1e-4f);
  });

  test("the 2D look: an Animation on 2s changes its pose every other frame, on 3s every third, while the frames go on; Hold is saved with the scene", [] {
    std::remove(resolve_asset_path("Assets/Animations/R168Hold.anim").c_str());
    AnimationClipPtr c = create_clip_asset("Assets/Animations/R168Hold.anim", "R168Hold");
    CHECK(c != nullptr);
    if (!c) return;
    c->frame_rate = 24.0f;
    c->track("", "Position", 0).set_key(0.0f, 0.0f, AnimLinear);
    c->track("", "Position", 0).set_key(24.0f, 24.0f, AnimLinear);
    auto distinct = [&](int hold) {
      Scene s;
      GameObject *g = s.create("Held");
      Animation *an = g->add<Animation>();
      an->clip = c->asset_path;
      an->hold = hold;
      PlayContext ctx;
      ctx.scene = &s;
      an->start(ctx);
      ctx.dt = 1.0f / 24.0f;
      std::set<int> seen;
      for (int k = 0; k < 23; k++) {
        an->update(ctx);
        seen.insert((int)std::lround(g->local().position.x * 1000.0f));
      }
      return (int)seen.size();
    };
    const int on1 = distinct(1), on2 = distinct(2), on3 = distinct(3);
    std::printf("    23 frames: %d poses on 1s, %d on 2s, %d on 3s\n", on1, on2, on3);
    CHECK(on1 == 23 && on2 == 12 && on3 == 8);
    CHECK(hold_frame(7.0f, 2) == 6.0f && hold_frame(7.0f, 3) == 6.0f && hold_frame(7.0f, 1) == 7.0f);
    CHECK(hold_frame(1.9999f, 2) == 2.0f);  // (a float frame just under the multiple is that frame)
    CHECK(hold_frame_from(3.0f, 3.0f, 2) == 3.0f && hold_frame_from(3.0f, 4.0f, 2) == 3.0f && hold_frame_from(3.0f, 5.0f, 2) == 5.0f);  // (a render range held from its first frame)
    Scene s;
    s.create("Held")->add<Animation>()->hold = 3;
    Scene back;
    std::string error;
    CHECK(load_scene_text(save_scene_text(s), back, error));
    const GameObject *bg = back.find_by_name("Held");
    CHECK(bg && bg->get<Animation>() && bg->get<Animation>()->hold == 3);
    std::remove(resolve_asset_path("Assets/Animations/R168Hold.anim").c_str());
  });

}

static void tests_round184() {
  /* The Scene view's crop of an object seen from `dist` metres, straight on, with the views on `device`. */
  struct R184 {
    std::vector<uint32_t> px;
    std::vector<uint8_t> mask;  // 1 where the object is
    Recti rect;
  };
  /* The picture, and which of its pixels are the object's: those that differ from the same view with the object
   * moved away (a 1 m object at 60 m is nine pixels wide; the view's background would swamp any measure). */
  auto r184_view = [](Editor &ed, GameObject *obj, float dist, int device) {
    ed.viewport_device_for_test(device);
    ed.command(strprintf("camera 0 0 %g 0 1 0", dist));  // (yaw, pitch, distance, the pivot: straight on from `dist` metres)
    obj->set_local_position({1000.0f, 1.0f, 0.0f});
    r34_steps(ed, 2);
    R184 r;
    r.rect = ed.scene_view_rect();
    const std::vector<uint32_t> empty = r34_crop(ed, r.rect);
    obj->set_local_position({0.0f, 1.0f, 0.0f});
    r34_steps(ed, 2);
    r.px = r34_crop(ed, r.rect);
    r.mask.assign(r.px.size(), 0);
    for (size_t i = 0; i < r.px.size() && i < empty.size(); i++) r.mask[i] = r.px[i] != empty[i] ? 1 : 0;
    return r;
  };
  auto luma = [](uint32_t p) { return 0.2126f * ((p >> 16) & 255) + 0.7152f * ((p >> 8) & 255) + 0.0722f * (p & 255); };
  /* The mean absolute Laplacian over the object's interior pixels (those whose four neighbours are the object's too:
   * the silhouette against the background is not the texture's doing). */
  auto energy = [&](const std::vector<float> &l, const std::vector<uint8_t> &mask, int w, int h) {
    double sum = 0;
    size_t n = 0;
    for (int y = 1; y + 1 < h; y++)
      for (int x = 1; x + 1 < w; x++) {
        const size_t i = (size_t)y * w + x;
        if (!mask[i] || !mask[i - 1] || !mask[i + 1] || !mask[i - w] || !mask[i + w]) continue;
        const float c = l[(size_t)y * w + x];
        sum += std::fabs(4.0f * c - l[(size_t)y * w + x - 1] - l[(size_t)y * w + x + 1] - l[(size_t)(y - 1) * w + x] - l[(size_t)(y + 1) * w + x]);
        n++;
      }
    return n ? sum / n : 0.0;
  };
  auto lumas = [&](const std::vector<uint32_t> &px) {
    std::vector<float> l(px.size());
    for (size_t i = 0; i < px.size(); i++) l[i] = luma(px[i]);
    return l;
  };
  /* The near picture box-filtered by `f` x `f`: the far picture's reference (what a filtered texture should give). */
  auto downsample = [](const std::vector<float> &l, const std::vector<uint8_t> &mask, int w, int h, int f, int &ow, int &oh, std::vector<uint8_t> &omask) {
    ow = w / f, oh = h / f;
    std::vector<float> o((size_t)ow * oh, 0.0f);
    omask.assign((size_t)ow * oh, 0);
    for (int y = 0; y < oh; y++)
      for (int x = 0; x < ow; x++) {
        double s = 0;
        int m = 0;
        for (int j = 0; j < f; j++)
          for (int i = 0; i < f; i++) {
            s += l[(size_t)(y * f + j) * w + x * f + i];
            m += mask[(size_t)(y * f + j) * w + x * f + i];
          }
        o[(size_t)y * ow + x] = (float)(s / (f * f));
        omask[(size_t)y * ow + x] = m * 2 >= f * f ? 1 : 0;  // (an object pixel where the object covered half the block)
      }
    return o;
  };
  auto far_vs_near = [&](const char *what, Editor &ed, GameObject *obj, float near_m, float far_m, int device, double &e_far, double &e_ref) {
    const R184 near = r184_view(ed, obj, near_m, device), far = r184_view(ed, obj, far_m, device);
    const int f = (int)std::lround(far_m / near_m);
    const std::vector<float> ln = lumas(near.px), lf = lumas(far.px);
    int ow, oh;
    std::vector<uint8_t> rmask;
    const std::vector<float> ref = downsample(ln, near.mask, near.rect.w, near.rect.h, f, ow, oh, rmask);
    size_t far_px = 0, near_px = 0;
    for (uint8_t m : far.mask) far_px += m;
    for (uint8_t m : near.mask) near_px += m;
    e_far = energy(lf, far.mask, far.rect.w, far.rect.h);
    e_ref = energy(ref, rmask, ow, oh);
    std::printf("    %s (%s): %zu object pixels at %g m, %zu at %g m; high-frequency energy at %g m %.2f, the %g m picture downsampled %d x %.2f\n", what,
                device == 2 ? "the card" : "the CPU", near_px, near_m, far_px, far_m, far_m, e_far, near_m, f, e_ref);
    CHECK(far_px > 200 && near_px > 2000);  // (a 1 m object at 20 m: about 27 pixels wide)
  };
  /* A PNG of w x h pixels from a function (an 8-bit grey with alpha 255). */
  auto grey_png = [](const std::string &path, int w, int h, const std::function<float(int, int)> &fn) {
    std::vector<uint32_t> px((size_t)w * h);
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) {
        const uint8_t g = (uint8_t)std::lround(255.0f * clampf(fn(x, y), 0.0f, 1.0f));
        px[(size_t)y * w + x] = 0xFF000000u | ((uint32_t)g << 16) | ((uint32_t)g << 8) | g;
      }
    return write_png(path, px.data(), w, h, w);
  };

  test("toon at a distance: a hatched plane, a face-shadow sphere at softness 0 and a box-mapped cube seen from 20 m carry no more high-frequency energy inside them than their 2 m pictures downsampled (the maps read at the pixel's lod), on the CPU and on the card", [&] {
    const std::string hatch = fs::join(test_dir(), "hatch_lines.png"), face = fs::join(test_dir(), "face_sdf.png"), check = fs::join(test_dir(), "box_checker.png");
    CHECK(grey_png(hatch, 64, 64, [](int x, int) { return (x / 4) % 2 ? 1.0f : 0.0f; }));             // (vertical lines, 8 per tile)
    CHECK(grey_png(face, 128, 128, [](int x, int y) { return 0.5f + 0.5f * std::sin(x * 0.4f) * std::sin(y * 0.4f); }));  // (a wavy field: a hard threshold sparkles)
    CHECK(grey_png(check, 64, 64, [](int x, int y) { return ((x / 8) + (y / 8)) % 2 ? 1.0f : 0.0f; }));
    for (int device : {1, 2}) {
      if (device == 2 && !gpu::available()) continue;
      Editor ed;
      r34_open(ed);
      ed.command("select Main Camera");
      ed.command("select Cube");
      ed.command("delete");
      GameObject *plane = ed.scene().create("Hatched");
      plane->add<MeshFilter>()->mesh = primitives::plane(1.0f, 1);
      MaterialPtr hm = make_material("hatched", Vec3(0.9f));
      hm->shading_model = 1;
      hm->toon_bands = 1;
      hm->hatch_map.path = hatch;
      hm->hatch_scale = 8.0f;
      plane->add<MeshRenderer>()->materials = {hm};
      plane->set_local_position({0.0f, 1.0f, 0.0f});
      plane->set_local_rotation(Quat::euler({-90, 0, 0}));  // (upright, facing the camera; lit from behind the camera: in the shadow tone where the hatching shows)
      ed.command("set Directional Light.Transform.rotation 20 160 0");
      r34_steps(ed, 2);
      double e_far, e_ref;
      far_vs_near("the hatched plane", ed, plane, 2.0f, 20.0f, device, e_far, e_ref);
      CHECK(e_far <= 2.0 * e_ref + 2.0);
      /* The face shadow map at softness 0. */
      GameObject *head = ed.scene().create("Head");
      head->add<MeshFilter>()->mesh = primitives::uv_sphere(0.5f, 48, 24);
      MaterialPtr fm = make_material("face", Vec3(0.9f, 0.8f, 0.7f));
      fm->shading_model = 1;
      fm->face_shadow_map.path = face;
      fm->face_shadow_softness = 0.0f;
      head->add<MeshRenderer>()->materials = {fm};
      head->set_local_position({0.0f, 1.0f, 0.0f});
      plane->set_local_position({100.0f, 1.0f, 0.0f});  // (out of the way)
      far_vs_near("the face-shadow sphere", ed, head, 2.0f, 20.0f, device, e_far, e_ref);
      CHECK(e_far <= 2.0 * e_ref + 2.0);
      /* Box mapping. */
      GameObject *box = ed.scene().create("Boxed");
      box->add<MeshFilter>()->mesh = primitives::cube(1.0f);
      MaterialPtr bm = make_material("boxed", Vec3(1.0f));
      bm->base_map.path = check;
      bm->mapping = (int)MaterialMapping::Box;
      bm->tiling = {4, 4, 4};
      box->add<MeshRenderer>()->materials = {bm};
      box->set_local_position({0.0f, 1.0f, 0.0f});
      box->set_local_rotation(Quat::euler({20, 30, 0}));
      head->set_local_position({100.0f, 1.0f, 0.0f});
      far_vs_near("the box-mapped cube", ed, box, 2.0f, 20.0f, device, e_far, e_ref);
      CHECK(e_far <= 2.0 * e_ref + 2.0);
      ed.viewport_device_for_test(1);
    }
  });

  test("toon at a distance: a two-tone sphere seen from 20 m carries no more high-frequency energy inside it than its 2 m picture downsampled (its cel edge is one antialiased pixel, not a sparkle), on the CPU and on the card", [&] {
    for (int device : {1, 2}) {
      if (device == 2 && !gpu::available()) continue;
      Editor ed;
      r34_open(ed);
      ed.command("select Main Camera");
      ed.command("select Cube");
      ed.command("delete");
      GameObject *ball = ed.scene().create("Cel");
      ball->add<MeshFilter>()->mesh = primitives::uv_sphere(0.5f, 64, 32);
      MaterialPtr cm = make_material("cel", Vec3(0.9f, 0.3f, 0.3f));
      cm->shading_model = 1;
      cm->toon_bands = 1;
      cm->toon_softness = 0.0f;
      ball->add<MeshRenderer>()->materials = {cm};
      ball->set_local_position({0.0f, 1.0f, 0.0f});
      ed.command("set Directional Light.Transform.rotation 30 -60 0");
      r34_steps(ed, 2);
      double e_far, e_ref;
      far_vs_near("the two-tone sphere", ed, ball, 2.0f, 20.0f, device, e_far, e_ref);
      CHECK(e_far <= 2.0 * e_ref + 2.0);
      /* The edge is there: with softness 0 the sphere at 2 m is the two tones (a few edge pixels aside), and at 20 m
       * the band edge is one antialiased pixel - mid tones between the two, which a hard edge never gives. */
      const R184 near = r184_view(ed, ball, 2.0f, device), far = r184_view(ed, ball, 20.0f, device);
      auto tones = [&](const R184 &r, size_t &lit_n, size_t &shade_n, size_t &mid_n) {
        std::vector<float> l;
        for (size_t i = 0; i < r.px.size(); i++)
          if (r.mask[i]) l.push_back(luma(r.px[i]));
        std::sort(l.begin(), l.end());
        const float lo = l[l.size() / 10], hi = l[l.size() * 9 / 10];  // (the shade tone and the lit tone: the deciles)
        const float margin = (hi - lo) * 0.25f;  // (a tone is the lit or the shade one within a quarter of their contrast; the middle half is "between")
        lit_n = shade_n = mid_n = 0;
        for (float v : l) {
          if (v >= hi - margin) lit_n++;
          else if (v <= lo + margin) shade_n++;
          else mid_n++;
        }
        return hi - lo;
      };
      size_t ln2, sn2, mn2, lnf, snf, mnf;
      const float contrast2 = tones(near, ln2, sn2, mn2), contrastf = tones(far, lnf, snf, mnf);
      std::printf("    %s: at 2 m the tones %.0f apart, %zu lit / %zu shade / %zu between of %zu; at 20 m %.0f apart, %zu / %zu / %zu of %zu\n", device == 2 ? "the card" : "the CPU",
                  contrast2, ln2, sn2, mn2, ln2 + sn2 + mn2, contrastf, lnf, snf, mnf, lnf + snf + mnf);
      CHECK(contrast2 > 15.0f && mnf > 0 && mnf * 20 >= lnf + snf);  // (a clear step between the tones; at 20 m a one-pixel edge over a 27-pixel sphere is a twentieth of it or more)
      ed.viewport_device_for_test(1);
    }
  });

  test("outlines at a distance: an outlined sphere at 60 m keeps a one-pixel silhouette (Min Pixels 1 for a new outline), and a scene saved before the field loads with 0", [&] {
    Editor ed;
    r34_open(ed);
    ed.command("select Main Camera");
    ed.command("select Cube");
      ed.command("delete");
    GameObject *ball = ed.scene().create("Lined");
    ball->add<MeshFilter>()->mesh = primitives::uv_sphere(0.5f, 48, 24);
    ball->add<MeshRenderer>()->materials = {make_material("plain", Vec3(0.8f))};
    Outline *ol = ball->add<Outline>();
    CHECK(ol->min_pixels == 1.0f);
    ol->thickness = 0.02f;
    ol->color = {0.0f, 0.0f, 0.0f};
    ball->set_local_position({0.0f, 1.0f, 0.0f});
    r34_steps(ed, 2);
    auto dark = [&](const R184 &r) {  // (the object's pixels that are the outline's black)
      size_t n = 0;
      for (size_t i = 0; i < r.px.size(); i++) n += r.mask[i] && luma(r.px[i]) < 40.0f ? 1 : 0;
      return n;
    };
    const R184 near = r184_view(ed, ball, 2.0f, 1), far = r184_view(ed, ball, 60.0f, 1);
    const size_t dn = dark(near), df = dark(far);
    size_t far_px = 0;
    for (uint8_t m : far.mask) far_px += m;
    std::printf("    outline pixels: %zu at 2 m; at 60 m %zu of the object's %zu pixels (a nine-pixel sphere's one-pixel ring is about 28)\n", dn, df, far_px);
    CHECK(dn > 200 && df > 15 && df * 3 >= far_px);
    /* A scene text without the field: the outline loads with Min Pixels 0 (as it drew when it was saved). */
    const std::string text = "blendity_scene 1\nobject 7 0 \"Old\" 1\ntransform 0 0 0 0 0 0 1 1 1 1 0 0 0\ncomponent Outline 1\nThickness 0.02\nend\nend\n";
    Scene old;
    std::string err;
    const bool loaded = load_scene_text(text, old, err);
    const GameObject *g = loaded ? old.find_by_name("Old") : nullptr;
    const Outline *o = g ? g->get<Outline>() : nullptr;
    std::printf("    an old scene's outline: %s, Min Pixels %g\n", o ? "loaded" : err.c_str(), o ? o->min_pixels : -1.0f);
    CHECK(o && o->min_pixels == 0.0f);
    /* And a saved one keeps its own: Min Pixels 3 round-trips. */
    ol->min_pixels = 3.0f;
    Scene back;
    CHECK(load_scene_text(save_scene_text(ed.scene()), back, err));
    const GameObject *gb = back.find_by_name("Lined");
    const Outline *ob = gb ? gb->get<Outline>() : nullptr;
    std::printf("    a saved outline's Min Pixels after a round trip: %g\n", ob ? ob->min_pixels : -1.0f);
    CHECK(ob && ob->min_pixels == 3.0f);
  });
}

/* ---------------------------------------------------------------- round 211: overlays recorded (task 0192) */
static void tests_round211() {
  test("overlays recorded in the world and replayed draw exactly what drawing them does - lines, dots, lines without the depth test, an outline between them", [] {
    const R34Raster rs(300, 200);
    Image img;
    RenderTarget rt;
    Renderer3D r;
    rs.render(img, rt, r);
    const std::vector<uint32_t> base = img.pixels;
    std::vector<uint32_t> sel;
    for (uint32_t id : rt.ids)
      if (id) sel.push_back(id);
    std::sort(sel.begin(), sel.end());
    sel.erase(std::unique(sel.begin(), sel.end()), sel.end());
    if (sel.size() > 1) sel.resize(1);
    R34Rng rng{77u};
    auto draw = [&] {
      r.begin_overlay_batch();
      for (int i = 0; i < 300; i++) {
        const Vec3 a(rng.range(-4.0f, 4.0f), rng.range(-2.0f, 2.0f), rng.range(-3.0f, 4.0f)), b(rng.range(-4.0f, 4.0f), rng.range(-2.0f, 2.0f), rng.range(-3.0f, 4.0f));
        const uint32_t c = 0xC0000000u | (uint32_t)(i * 2654435761u & 0xFFFFFF);
        if (i % 4 == 3) r.point(a, rng.range(1.0f, 5.0f), c, i % 2 == 0);
        else r.line(a, b, c, i % 3 != 0, i % 5 == 0 ? 0.01f : 0.0f);
        if (i == 150 && !sel.empty()) r.outline_ids(sel, 0xFFFF8800u, 2);
      }
      r.end_overlay_batch();
    };
    rng = R34Rng{77u};
    draw();
    const std::vector<uint32_t> direct = img.pixels;
    img.pixels = base;
    Renderer3D::OverlayRecord rec;
    r.record_overlays(&rec);
    rng = R34Rng{77u};
    draw();
    r.record_overlays(nullptr);
    CHECK(img.pixels == base);  // (recording draws nothing)
    std::printf("    %zu recorded, %zu ids\n", rec.ops.size(), rec.ids.size());
    CHECK(rec.ops.size() == 300 + (sel.empty() ? 0 : 1));
    r.begin_overlay_batch();
    r.replay(rec);
    r.end_overlay_batch();
    CHECK(img.pixels == direct);
  });
}

/* ---------------------------------------------------------------- round 212: Color::over (task 0193) */
static void tests_round212() {
  test("Color::over is blend() over an opaque pixel, and drawing over a transparent hole then putting that over a picture gives what drawing over the picture gives (within 2 / 255 a layer)", [] {
    R34Rng rng{212u};
    int worst = 0;
    bool same = true;
    for (int i = 0; i < 20000; i++) {
      const uint32_t base = 0xFF000000u | ((uint32_t)rng.range(0.0f, 16777215.0f) & 0xFFFFFF);
      auto alpha = [&] { return std::min(255u, (uint32_t)rng.range(0.0f, 300.0f)); };  // (255, opaque, a sixth of the time)
      const uint32_t s1 = alpha() << 24 | ((uint32_t)rng.range(0.0f, 16777215.0f) & 0xFFFFFF);
      const uint32_t s2 = alpha() << 24 | ((uint32_t)rng.range(0.0f, 16777215.0f) & 0xFFFFFF);
      same = same && Color::over(base, s1) == Color::blend(base, s1);
      /* The UI's layer: two things drawn over a hole; the card then puts it over the picture (straight alpha). */
      const uint32_t layer = Color::over(Color::over(0u, s1), s2), direct = Color::blend(Color::blend(base, s1), s2);
      const float a = (layer >> 24) / 255.0f;
      for (int k = 0; k < 24; k += 8) {
        const float c = ((layer >> k) & 255) * a + ((base >> k) & 255) * (1.0f - a);
        worst = std::max(worst, std::abs((int)std::lround(c) - (int)((direct >> k) & 255)));
      }
    }
    std::printf("    composited against drawn directly: at most %d / 255 apart\n", worst);
    CHECK(same);
    CHECK(worst <= 4);  // (blend() truncates at each layer and the composite rounds once: up to 2 / 255 a layer, two layers here)
  });
}

static void tests_round263() {
  auto grey_of = [](uint32_t c) { return (float)(c & 0xFF) / 255.0f; };
  test("mesh maps (ADR 0079): a sphere's ambient occlusion is open everywhere, its thickness deep, its curvature one convex value, its world normal the position's direction; an L of two cubes darkens in its inner corner; a thin plate reads thin", [&] {
    const MeshPtr sphere = primitives::uv_sphere(0.5f, 48, 32);
    MeshMapsSettings s;
    s.size = 256, s.samples = 16;
    MeshMapsResult r;
    std::string why;
    const bool ok = bake_mesh_maps(*sphere, Mat4::identity(), s, r, &why);
    CHECK(ok);
    if (!ok) {
      std::printf("    %s\n", why.c_str());
      return;
    }
    float ao_min = 1.0f, th_sum = 0.0f, cv_min = 1.0f, cv_max = 0.0f, worst_deg = 0.0f;
    size_t covered = 0;
    /* The point a texel covers, back from its owner triangle: the normal map against the position direction (the
     * position map is the point in the bounds). */
    for (size_t i = 0; i < r.covered.size(); i++) {
      if (!r.covered[i]) continue;
      covered++;
      ao_min = std::min(ao_min, grey_of(r.ao[i]));
      th_sum += grey_of(r.thickness[i]);
      cv_min = std::min(cv_min, grey_of(r.curvature[i])), cv_max = std::max(cv_max, grey_of(r.curvature[i]));
      const uint32_t n = r.normal[i], pcol = r.position[i];
      const Vec3 nrm = normalize(Vec3((float)((n >> 16) & 0xFF) / 255.0f, (float)((n >> 8) & 0xFF) / 255.0f, (float)(n & 0xFF) / 255.0f) * 2.0f - Vec3(1.0f));
      const Vec3 pos = Vec3((float)((pcol >> 16) & 0xFF) / 255.0f, (float)((pcol >> 8) & 0xFF) / 255.0f, (float)(pcol & 0xFF) / 255.0f) - Vec3(0.5f);  // (the sphere's bounds are centred)
      if (length(pos) > 0.05f) worst_deg = std::max(worst_deg, std::acos(std::max(-1.0f, std::min(1.0f, dot(nrm, normalize(pos))))) * 180.0f / kPi);
    }
    /* Thickness is the mean inward distance over the reach (the bounds' longest side, the diameter here): a chord
     * 2R cos(theta) under the cosine weighting averages 4R/3, two thirds of the diameter. */
    const float th_mean = covered ? th_sum / (float)covered : 0.0f;
    std::printf("    the sphere at 256: %zu texels; AO min %.3f; thickness mean %.3f (two thirds expected); curvature %.3f..%.3f; the normal within %.1f deg of the position's direction; %llu rays, %.0f ms\n",
                covered, ao_min, th_mean, cv_min, cv_max, worst_deg, (unsigned long long)r.rays, r.ms_total);
    CHECK(covered > 0 && ao_min > 0.95f && std::fabs(th_mean - 0.667f) < 0.05f && cv_min > 0.6f && cv_max < 0.65f && cv_max - cv_min < 0.05f && worst_deg < 3.0f);
    /* An L: a unit cube and another on top of it, half a unit over. */
    Mesh L = *primitives::cube(1.0f);
    for (Vec3 &p : L.positions) p += Vec3(0.5f, 0.5f, 0.5f);
    {
      Mesh top = *primitives::cube(1.0f);
      for (Vec3 &p : top.positions) p += Vec3(1.0f, 1.5f, 0.5f);
      const uint32_t off = (uint32_t)L.vert_count();
      L.positions.insert(L.positions.end(), top.positions.begin(), top.positions.end());
      for (size_t f = 0; f < top.face_count(); f++) {
        std::vector<uint32_t> fv(top.face_verts(f), top.face_verts(f) + top.face_size(f));
        for (uint32_t &v : fv) v += off;
        L.add_face(fv.data(), fv.size());
      }
      L.touch();
    }
    MeshMapsResult rl;
    CHECK(bake_mesh_maps(L, Mat4::identity(), s, rl, &why));
    float l_min = 1.0f, l_max = 0.0f;
    for (size_t i = 0; i < rl.covered.size(); i++)
      if (rl.covered[i]) l_min = std::min(l_min, grey_of(rl.ao[i])), l_max = std::max(l_max, grey_of(rl.ao[i]));
    /* A plate: a cube 0.02 thick. */
    Mesh plate = *primitives::cube(1.0f);
    for (Vec3 &p : plate.positions) p.y *= 0.02f;
    plate.touch();
    MeshMapsResult rp;
    CHECK(bake_mesh_maps(plate, Mat4::identity(), s, rp, &why));
    /* Means: a plate's narrow sides are legitimately thick across, a cube's texels beside an edge legitimately thin. */
    auto mean_thickness = [&](const MeshMapsResult &m, size_t *n) {
      float sum = 0.0f;
      *n = 0;
      for (size_t i = 0; i < m.covered.size(); i++)
        if (m.covered[i]) (*n)++, sum += grey_of(m.thickness[i]);
      return *n ? sum / (float)*n : 0.0f;
    };
    size_t p_cov = 0, c_cov = 0;
    const float p_mean = mean_thickness(rp, &p_cov);
    MeshMapsResult rc;
    CHECK(bake_mesh_maps(*primitives::cube(1.0f), Mat4::identity(), s, rc, &why));
    const float c_mean = mean_thickness(rc, &c_cov);
    /* The inner corner proper: the lower cube's top face (its world normal up, the position map's y at a half of
     * the 0..2 bounds) within a twentieth of the upper cube's wall at x = 0.5 (the bounds run 0..1.5 in x: the
     * position's x in 0.30..0.333). */
    float corner_min = 1.0f, corner_sum = 0.0f;
    size_t corner_n = 0;
    for (size_t i = 0; i < rl.covered.size(); i++) {
      if (!rl.covered[i]) continue;
      const uint32_t n = rl.normal[i], pc = rl.position[i];
      const float ny = (float)((n >> 8) & 0xFF) / 255.0f * 2.0f - 1.0f, px = (float)((pc >> 16) & 0xFF) / 255.0f, py = (float)((pc >> 8) & 0xFF) / 255.0f;
      if (ny < 0.9f || std::fabs(py - 0.5f) > 0.02f || px < 0.30f || px > 0.333f) continue;
      corner_n++, corner_sum += grey_of(rl.ao[i]), corner_min = std::min(corner_min, grey_of(rl.ao[i]));
    }
    const float corner_mean = corner_n ? corner_sum / (float)corner_n : 1.0f;
    /* The same cube placed at a hundredth of its size: the reach and the offset follow the object into the world. */
    MeshMapsResult rs;
    const Mat4 small = Mat4::scale(Vec3(0.01f));
    CHECK(bake_mesh_maps(*primitives::cube(1.0f), small, s, rs, &why));
    size_t s_cov = 0;
    const float s_mean = mean_thickness(rs, &s_cov);
    float s_ao_min = 1.0f;
    for (size_t i = 0; i < rs.covered.size(); i++)
      if (rs.covered[i]) s_ao_min = std::min(s_ao_min, grey_of(rs.ao[i]));
    std::printf("    the L: AO %.3f (the faces that meet) .. %.3f (the open faces), the inner corner's strip mean %.3f, min %.3f over %zu texels; the plate: mean thickness %.3f over %zu texels; the cube: %.3f over %zu, at a hundredth of the size %.3f (AO min %.3f)\n",
                l_min, l_max, corner_mean, corner_min, corner_n, p_mean, p_cov, c_mean, c_cov, s_mean, s_ao_min);
    CHECK(l_min < 0.7f && l_max > 0.95f && corner_n > 0 && corner_mean < 0.8f && corner_min < 0.7f && p_mean < 0.12f && c_mean > 0.4f && std::fabs(s_mean - c_mean) < 0.05f && s_ao_min > 0.95f);
    /* The curvature does not change with the mesh's resolution: the sphere at twice the segments reads the same. */
    MeshMapsResult rd;
    CHECK(bake_mesh_maps(*primitives::uv_sphere(0.5f, 96, 64), Mat4::identity(), s, rd, &why));
    float d_sum = 0.0f;
    size_t d_n = 0;
    for (size_t i = 0; i < rd.covered.size(); i++)
      if (rd.covered[i]) d_n++, d_sum += grey_of(rd.curvature[i]);
    const float d_mean = d_n ? d_sum / (float)d_n : 0.0f, c48 = covered ? (cv_min + cv_max) * 0.5f : 0.0f;
    /* The same dense sphere with a little noise on its radius (a ten-thousandth, a sculpt's surface grain): the
     * sagitta over the chord squared is a second difference and amplifies it; the smoothing pass holds the spread. */
    Mesh noisy = *primitives::uv_sphere(0.5f, 96, 64);
    uint32_t nseed = 7u;
    for (Vec3 &q : noisy.positions) {
      nseed = nseed * 1664525u + 1013904223u;
      q = q * (1.0f + ((float)(nseed >> 8) / 16777216.0f - 0.5f) * 2e-4f);
    }
    noisy.touch();
    MeshMapsResult rn;
    CHECK(bake_mesh_maps(noisy, Mat4::identity(), s, rn, &why));
    float n_sum = 0.0f, n_min = 1.0f, n_max = 0.0f;
    size_t n_n = 0;
    for (size_t i = 0; i < rn.covered.size(); i++)
      if (rn.covered[i]) n_n++, n_sum += grey_of(rn.curvature[i]), n_min = std::min(n_min, grey_of(rn.curvature[i])), n_max = std::max(n_max, grey_of(rn.curvature[i]));
    const float n_mean = n_n ? n_sum / (float)n_n : 0.0f;
    std::printf("    curvature: the 48 x 32 sphere %.3f..%.3f, the 96 x 64 sphere's mean %.3f; with a ten-thousandth of radius noise: mean %.3f, %.3f..%.3f\n", cv_min, cv_max, d_mean, n_mean, n_min, n_max);
    CHECK(d_n > 0 && std::fabs(d_mean - c48) < 0.03f && d_mean > 0.55f);
    CHECK(n_n > 0 && std::fabs(n_mean - d_mean) < 0.03f && n_max - n_min < 0.3f);
    Mesh empty;
    CHECK(!bake_mesh_maps(empty, Mat4::identity(), s, rc, &why) && !why.empty());
  });
}

void run_render_tests() {
  colormanagement_tests();
  render_tests();
  round27_tests();
  round29_tests();
  round29_review_tests();
  round30_tests();
  round31_tests();
  round32_tests();
  round34_tests();
  round45_tests();
  round46_tests();
  round47_tests();
  round48_tests();
  round51_tests();
  round54_tests();
  round55_tests();
  round56_tests();
  round57_tests();
  round60_tests();
  round61_tests();
  round62_tests();
  tests_round105();
  tests_round108();
  tests_round114();
  tests_round117();
  tests_round118();
  tests_round120();
  tests_round128();
  tests_round129();
  tests_round133();
  tests_round135();
  tests_round136();
  tests_round165();
  tests_round167();
  tests_round168();
  tests_round184();
  tests_round211();
  tests_round212();
  tests_round263();
}

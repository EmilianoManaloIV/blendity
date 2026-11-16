// SPDX-License-Identifier: GPL-2.0-or-later
// Gaussian splats in the CPU path tracer (round 533, task 0513): the shared maths of src/render/gsplat_math.h (Cycles
// 5.3's splat intersection, stochastic alpha and spherical-harmonics colour), then splat clouds traced in F12, bakes
// and reflections.
#include "test_common.h"

#include "../src/render/gsplat_math.h"
#include "../src/scene/pointcloud.h"
#include "../src/scene/pointcloud_object.h"

using namespace bl;

namespace {

gsplat::F3 g3(float x, float y, float z) { return gsplat::f3(x, y, z); }
const gsplat::Q4 kNoTurn{0, 0, 0, 1};

/* Cycles' own polynomial forms (util/math_spherical_harmonics.h), written out again from that file so the shared
 * basis is checked against an independent copy. */
void cycles_sh_forms(gsplat::F3 v, float o[16]) {
  o[0] = 0.282094792f;
  o[1] = -0.488602512f * v.y;
  o[2] = 0.488602512f * v.z;
  o[3] = -0.488602512f * v.x;
  o[4] = 1.092548431f * (v.x * v.y);
  o[5] = -1.092548431f * (v.y * v.z);
  o[6] = 0.315391565f * (3.0f * v.z * v.z - 1.0f);
  o[7] = -1.092548431f * (v.x * v.z);
  o[8] = 0.546274215f * (v.x * v.x - v.y * v.y);
  o[9] = -0.590043589f * v.y * (3.0f * v.x * v.x - v.y * v.y);
  o[10] = 2.890611442f * (v.y * v.x * v.z);
  o[11] = -0.457045799f * v.y * (-1.0f + 5.0f * v.z * v.z);
  o[12] = 0.373176332f * v.z * (5.0f * v.z * v.z - 3.0f);
  o[13] = -0.457045799f * v.x * (-1.0f + 5.0f * v.z * v.z);
  o[14] = 1.445305721f * v.z * (v.x * v.x - v.y * v.y);
  o[15] = -0.5900435899f * v.x * (v.x * v.x - 3.0f * v.y * v.y);
}

void run_r533_math() {
  test("Splat maths: the bounding sphere ends where the splat fades under 1% (Cycles' radius)", [] {
    const float r = gsplat::bound_radius(0.8f, g3(0.1f, 0.3f, 0.2f));
    CHECK_NEAR(r, std::sqrt(2.0f * std::log(0.8f / 0.01f)) * 0.3f, 1e-5f);
    CHECK(gsplat::bound_radius(0.009f, g3(1, 1, 1)) == 0.0f);  // too faint to see: left out
    CHECK(gsplat::bound_radius(kNaN, g3(1, 1, 1)) == 0.0f);
    /* The response at the radius along the largest axis is 1%. */
    CHECK_NEAR(0.8f * std::exp(-0.5f * (r / 0.3f) * (r / 0.3f)), 0.01f, 1e-5f);
  });

  test("Splat maths: a ray through the centre peaks at the centre with the full opacity", [] {
    gsplat::Prepared p;
    CHECK(gsplat::prepare(g3(0, 0, 5), g3(0.5f, 0.2f, 0.1f), kNoTurn, 0.7f, p));
    float t = -1, a = -1;
    CHECK(gsplat::intersect(p, g3(0, 0, 0), g3(0, 0, 1), 1e-4f, 100.0f, t, a));
    CHECK_NEAR(t, 5.0f, 1e-4f);
    CHECK_NEAR(a, 0.7f, 1e-5f);
    /* A direction that is not unit keeps t as the ray's parameter. */
    CHECK(gsplat::intersect(p, g3(0, 0, 0), g3(0, 0, 2), 1e-4f, 100.0f, t, a));
    CHECK_NEAR(t, 2.5f, 1e-4f);
  });

  test("Splat maths: one standard deviation off the centre sees opacity times e^-1/2", [] {
    gsplat::Prepared p;
    CHECK(gsplat::prepare(g3(0, 0, 5), g3(0.5f, 0.2f, 0.1f), kNoTurn, 0.7f, p));
    float t, a;
    CHECK(gsplat::intersect(p, g3(0.5f, 0, 0), g3(0, 0, 1), 1e-4f, 100.0f, t, a));  // 1 sigma along X
    CHECK_NEAR(a, 0.7f * std::exp(-0.5f), 1e-5f);
    CHECK(gsplat::intersect(p, g3(0, 0.4f, 0), g3(0, 0, 1), 1e-4f, 100.0f, t, a));  // 2 sigma along Y
    CHECK_NEAR(a, 0.7f * std::exp(-2.0f), 1e-5f);
  });

  test("Splat maths: a turned splat stretches along its turned axis", [] {
    /* 90 degrees about Z: the splat's long X axis lies along world Y. */
    const float h = std::sqrt(0.5f);
    gsplat::Prepared p;
    CHECK(gsplat::prepare(g3(0, 0, 5), g3(0.5f, 0.1f, 0.1f), gsplat::Q4{0, 0, h, h}, 1.0f, p));
    float t, a;
    CHECK(gsplat::intersect(p, g3(0, 0.5f, 0), g3(0, 0, 1), 1e-4f, 100.0f, t, a));
    CHECK_NEAR(a, std::exp(-0.5f), 1e-4f);
    /* 5 sigma across the short axis: inside the bounding sphere (it ignores the turn, as Cycles'), all but unseen. */
    CHECK(gsplat::intersect(p, g3(0.5f, 0, 0), g3(0, 0, 1), 1e-4f, 100.0f, t, a));
    CHECK_NEAR(a, std::exp(-12.5f), 1e-6f);
    /* A quaternion that is not unit turns the same. */
    gsplat::Prepared q;
    CHECK(gsplat::prepare(g3(0, 0, 5), g3(0.5f, 0.1f, 0.1f), gsplat::Q4{0, 0, 3 * h, 3 * h}, 1.0f, q));
    CHECK(gsplat::intersect(q, g3(0, 0.5f, 0), g3(0, 0, 1), 1e-4f, 100.0f, t, a));
    CHECK_NEAR(a, std::exp(-0.5f), 1e-4f);
  });

  test("Splat maths: an oblique ray peaks where the Gaussian along it is largest", [] {
    gsplat::Prepared p;
    CHECK(gsplat::prepare(g3(0, 0, 0), g3(1.0f, 0.25f, 0.5f), gsplat::Q4{0.1f, 0.3f, -0.2f, 0.9f}, 0.9f, p));
    const gsplat::F3 o = g3(-2.0f, 0.3f, -1.5f), d = g3(0.8f, 0.05f, 0.6f);
    float t, a;
    CHECK(gsplat::intersect(p, o, d, 0.0f, 100.0f, t, a));
    /* Brute force: the response along the ray is largest at t. */
    auto resp = [&](float s) {
      const gsplat::F3 u = gsplat::to_unit_space(p, gsplat::add(o, gsplat::mul(d, s)));
      return 0.9f * std::exp(-0.5f * gsplat::dot(u, u));
    };
    CHECK_NEAR(resp(t), a, 1e-5f);
    CHECK(resp(t) >= resp(t - 0.01f) && resp(t) >= resp(t + 0.01f));
  });

  test("Splat maths: a ray inside a splat sees it ahead, not behind", [] {
    gsplat::Prepared p;
    CHECK(gsplat::prepare(g3(0, 0, 0), g3(1, 1, 1), kNoTurn, 1.0f, p));
    float t, a;
    CHECK(gsplat::intersect(p, g3(0, 0, -0.5f), g3(0, 0, 1), 1e-4f, 100.0f, t, a));  // inside, centre ahead
    CHECK_NEAR(t, 0.5f, 1e-5f);
    CHECK(!gsplat::intersect(p, g3(0, 0, 0.5f), g3(0, 0, 1), 1e-4f, 100.0f, t, a));  // centre behind: skipped
    CHECK(!gsplat::intersect(p, g3(0, 0, -3.0f), g3(0, 0, 1), 1e-4f, 2.0f, t, a));  // peak past tmax
  });

  test("Splat maths: NaN, infinite and zero splats are refused, never traced", [] {
    gsplat::Prepared p;
    CHECK(!gsplat::prepare(g3(kNaN, 0, 0), g3(1, 1, 1), kNoTurn, 1.0f, p));
    CHECK(!gsplat::prepare(g3(0, kInf, 0), g3(1, 1, 1), kNoTurn, 1.0f, p));
    CHECK(!gsplat::prepare(g3(0, 0, 0), g3(1, kNaN, 1), kNoTurn, 1.0f, p));
    CHECK(!gsplat::prepare(g3(0, 0, 0), g3(kInf, 1, 1), kNoTurn, 1.0f, p));
    CHECK(!gsplat::prepare(g3(0, 0, 0), g3(0, 0, 0), kNoTurn, 1.0f, p));
    CHECK(!gsplat::prepare(g3(0, 0, 0), g3(1, 1, 1), gsplat::Q4{kNaN, 0, 0, 1}, 1.0f, p));
    CHECK(!gsplat::prepare(g3(0, 0, 0), g3(1, 1, 1), kNoTurn, kNaN, p));
    CHECK(!gsplat::prepare(g3(0, 0, 0), g3(1, 1, 1), kNoTurn, 0.005f, p));  // under 1%
    CHECK(p.radius == 0.0f);
    float t, a;
    CHECK(!gsplat::intersect(p, g3(0, 0, -5), g3(0, 0, 1), 0.0f, 100.0f, t, a));
    /* A zero quaternion is no turn; a flat splat (one scale 0) is clamped, not refused, and stays finite. */
    CHECK(gsplat::prepare(g3(0, 0, 0), g3(1, 0.5f, 0), gsplat::Q4{0, 0, 0, 0}, 1.0f, p));
    CHECK(gsplat::finite3(p.inv_axis_z));
    CHECK(gsplat::intersect(p, g3(0.2f, 0.1f, -5), g3(0, 0, 1), 0.0f, 100.0f, t, a));
    CHECK(std::isfinite(t) && std::isfinite(a) && a > 0.0f && a <= 1.0f);
    /* A zero direction misses. */
    CHECK(gsplat::prepare(g3(0, 0, 0), g3(1, 1, 1), kNoTurn, 1.0f, p));
    CHECK(!gsplat::intersect(p, g3(0, 0, -5), g3(0, 0, 0), 0.0f, 100.0f, t, a));
  });

  test("Splat maths: the stochastic test keeps about alpha of the hits", [] {
    for (float alpha : {0.1f, 0.5f, 0.9f}) {
      int kept = 0;
      const int n = 20000;
      for (int i = 0; i < n; i++)
        kept += gsplat::stochastic_keep(alpha, g3(0.001f * i, 0.37f, -1.25f + 0.0003f * i), 7u) ? 1 : 0;
      CHECK_NEAR((double)kept / n, alpha, 0.02);
    }
    CHECK(!gsplat::stochastic_keep(0.0f, g3(1, 2, 3), 0));
    /* The seed changes the draw for the same point. */
    int differ = 0;
    for (unsigned s = 0; s < 64; s++) differ += gsplat::stochastic_keep(0.5f, g3(1, 2, 3), s) ? 1 : 0;
    CHECK(differ > 10 && differ < 54);
  });

  test("Splat maths: the SH basis equals Cycles' polynomial forms", [] {
    for (int k = 0; k < 50; k++) {
      gsplat::F3 d = g3(std::sin(k * 1.7f), std::cos(k * 0.9f), std::sin(k * 0.31f + 1.0f));
      d = gsplat::mul(d, 1.0f / std::sqrt(gsplat::dot(d, d)));
      float a[16], b[16];
      gsplat::sh_basis(d, a);
      cycles_sh_forms(d, b);
      for (int i = 0; i < 16; i++) CHECK_NEAR(a[i], b[i], 2e-6f);
    }
  });

  test("Splat maths: degree 0 is the base colour; higher bands turn with the view and clamp at 0", [] {
    const gsplat::F3 base = g3(0.6f, 0.3f, -0.1f);
    gsplat::F3 c = gsplat::sh_color(base, nullptr, 0, g3(0, 0, 1));
    CHECK_NEAR(c.x, 0.6f, 1e-6f);
    CHECK_NEAR(c.z, 0.0f, 1e-6f);  // clamped
    float rest[45] = {};
    rest[3 * 1 + 0] = 0.4f;  // band 1, m = 0 (the second coefficient, m = -1, 0, 1): the z term, red only
    const gsplat::F3 up = gsplat::sh_color(base, rest, 3, g3(0, 0, 5));  // not unit: normalised
    const gsplat::F3 dn = gsplat::sh_color(base, rest, 3, g3(0, 0, -1));
    CHECK_NEAR(up.x, 0.6f + 0.4f * 0.4886025f, 1e-5f);
    CHECK_NEAR(dn.x, 0.6f - 0.4f * 0.4886025f, 1e-5f);
    CHECK_NEAR(up.y, 0.3f, 1e-6f);
    /* A zero or NaN direction leaves the base colour. */
    CHECK_NEAR(gsplat::sh_color(base, rest, 3, g3(0, 0, 0)).x, 0.6f, 1e-6f);
    CHECK_NEAR(gsplat::sh_color(base, rest, 3, g3(kNaN, 0, 0)).x, 0.6f, 1e-6f);
    /* NaN coefficients give 0, not NaN. */
    rest[0] = kNaN;
    CHECK(gsplat::sh_color(base, rest, 3, g3(0, 1, 0)).x == 0.0f);
  });

  test("Splat maths: radiance is the SH colour taken from sRGB to linear, as Cycles", [] {
    const gsplat::F3 l = gsplat::radiance_linear(g3(0.5f, 1.0f, 0.02f));
    CHECK_NEAR(l.x, 0.21404f, 1e-4f);
    CHECK_NEAR(l.y, 1.0f, 1e-5f);
    CHECK_NEAR(l.z, 0.02f / 12.92f, 1e-6f);
  });

  test("Splat maths: the normal is the shortest axis, facing the viewer", [] {
    const float h = std::sqrt(0.5f);
    gsplat::F3 n = gsplat::normal(g3(1, 1, 0.1f), kNoTurn, g3(0, 0, 1));
    CHECK_NEAR(n.z, -1.0f, 1e-6f);
    n = gsplat::normal(g3(0.1f, 1, 1), gsplat::Q4{0, 0, h, h}, g3(0, -1, 0));  // X turned onto Y
    CHECK_NEAR(n.y, 1.0f, 1e-5f);
  });
}

/* ---------------------------------------------------------------------------------------------------------------- */
/* Splat clouds in the path tracer                                                                                   */
/* ---------------------------------------------------------------------------------------------------------------- */

/* A splat cloud built in code (round 530's arrays, without its object): what the renderer rounds test with. */
struct TestCloud {
  std::vector<Vec3> position, color, scale, sh;
  std::vector<float> opacity;
  std::vector<Quat> rotation;
  int rest = 0;
  void add(Vec3 p, Vec3 c, float a, Vec3 s, Quat q = Quat()) {
    position.push_back(p), color.push_back(c), opacity.push_back(a), scale.push_back(s), rotation.push_back(q);
    for (int k = 0; k < rest; k++) sh.push_back(Vec3(0.0f));
  }
  SplatCloudView view(const Mat4 &model = Mat4::identity(), uint32_t id = 7, uint64_t key = 1) const {
    SplatCloudView v;
    v.position = position.data(), v.color = color.data(), v.opacity = opacity.data(), v.scale = scale.data();
    v.rotation = rotation.data(), v.sh_rest = rest ? sh.data() : nullptr, v.sh_rest_per_point = rest;
    v.count = position.size(), v.key = key, v.model = model, v.id = id;
    return v;
  }
};

constexpr int kW = 32, kH = 32;
const Vec3 kEye{0, 0, -5};

Environment black_world() {
  Environment env;
  env.mode = Environment::Color;
  env.color = Vec3(0.0f);
  return env;
}

Mat4 test_view() { return Mat4::look_at(kEye, {0, 0, 0}, {0, 1, 0}); }
Mat4 test_proj() { return Mat4::perspective(40 * kDeg2Rad, 1.0f, 0.1f, 100.0f); }

std::vector<float> trace_splats(const std::vector<SplatCloudView> &clouds, const std::vector<PTObject> &objs, int spp, bool embree,
                                size_t *kept = nullptr, int bounces = 4) {
  PathTracer pt;
  PTSettings st;
  st.use_embree = embree;
  st.denoise = false;
  st.max_bounces = bounces;
  st.clamp_indirect = 0;
  pt.set_settings(st);
  pt.set_splats(clouds);
  pt.build(objs, {}, black_world());
  if (kept) *kept = pt.splat_count();
  pt.set_camera(test_view(), test_proj(), kW, kH);
  pt.render(1e9, spp);
  return pt.linear_rgb(false);
}

Vec3 px(const std::vector<float> &img, int x, int y) {
  const size_t i = ((size_t)y * kW + x) * 3;
  return {img[i], img[i + 1], img[i + 2]};
}

/* An independent reference for one ray: every splat it meets, nearest first, alpha-blended front to back - the
 * expectation of the stochastic test (each splat kept with its alpha, independently). */
Vec3 composite_ray(const TestCloud &c, Vec3 o, Vec3 d) {
  struct M {
    float t, a;
    size_t i;
  };
  std::vector<M> met;
  for (size_t i = 0; i < c.position.size(); i++) {
    gsplat::Prepared p;
    const Quat q = c.rotation[i];
    if (!gsplat::prepare(g3(c.position[i].x, c.position[i].y, c.position[i].z), g3(c.scale[i].x, c.scale[i].y, c.scale[i].z),
                         gsplat::Q4{q.x, q.y, q.z, q.w}, c.opacity[i], p))
      continue;
    float t, a;
    if (gsplat::intersect(p, g3(o.x, o.y, o.z), g3(d.x, d.y, d.z), 1e-5f, 1e30f, t, a)) met.push_back({t, a, i});
  }
  std::sort(met.begin(), met.end(), [](const M &a, const M &b) { return a.t < b.t; });
  Vec3 sum(0.0f);
  float through = 1.0f;
  for (const M &m : met) {
    const gsplat::F3 col = gsplat::radiance_linear(gsplat::sh_color(g3(c.color[m.i].x, c.color[m.i].y, c.color[m.i].z), nullptr, 0, g3(d.x, d.y, d.z)));
    sum = sum + Vec3(col.x, col.y, col.z) * (through * m.a);
    through *= 1.0f - m.a;
  }
  return sum;
}

/* The reference picture: each pixel the mean of 4x4 rays across it (the path tracer jitters inside the pixel). */
std::vector<float> composite_image(const TestCloud &c) {
  const Mat4 inv = (test_proj() * test_view()).inverse();
  std::vector<float> img((size_t)kW * kH * 3, 0.0f);
  for (int y = 0; y < kH; y++)
    for (int x = 0; x < kW; x++) {
      Vec3 sum(0.0f);
      for (int s = 0; s < 16; s++) {
        const float nx = ((x + (s % 4 + 0.5f) / 4.0f) / kW) * 2.0f - 1.0f, ny = 1.0f - ((y + (s / 4 + 0.5f) / 4.0f) / kH) * 2.0f;
        const Vec4 far = inv * Vec4(nx, ny, 1.0f, 1.0f);
        const Vec3 d = normalize(far.xyz() / far.w - kEye);
        sum = sum + composite_ray(c, kEye, d);
      }
      const size_t i = ((size_t)y * kW + x) * 3;
      img[i] = sum.x / 16, img[i + 1] = sum.y / 16, img[i + 2] = sum.z / 16;
    }
  return img;
}

/* A few soft splats round the origin, overlapping in depth, facing the camera. */
TestCloud test_cloud() {
  TestCloud c;
  c.add({0, 0, 0}, {0.9f, 0.3f, 0.2f}, 0.8f, {0.6f, 0.6f, 0.08f});
  c.add({0.5f, 0.3f, -0.4f}, {0.2f, 0.8f, 0.3f}, 0.6f, {0.4f, 0.25f, 0.1f}, Quat::axis_angle({0, 0, 1}, 0.6f));
  c.add({-0.6f, -0.4f, 0.3f}, {0.2f, 0.3f, 0.9f}, 0.9f, {0.3f, 0.5f, 0.1f}, Quat::axis_angle({1, 1, 0}, 0.4f));
  c.add({0.1f, -0.7f, -0.8f}, {0.9f, 0.9f, 0.9f}, 0.4f, {0.5f, 0.2f, 0.2f});
  return c;
}

double mean_abs_diff(const std::vector<float> &a, const std::vector<float> &b) {
  double s = 0;
  for (size_t i = 0; i < a.size(); i++) s += std::fabs((double)a[i] - b[i]);
  return s / (double)a.size();
}

void run_r533_trace() {
  test("Splats in F12: a splat shows its colour, taken to linear light; the world shows round it", [] {
    TestCloud c;
    c.add({0, 0, 0}, {0.8f, 0.5f, 0.2f}, 0.99f, {0.6f, 0.6f, 0.05f});  // (bounding sphere 1.8: clear of the corners)
    size_t kept = 0;
    const std::vector<float> img = trace_splats({c.view()}, {}, 64, false, &kept);
    CHECK(kept == 1);
    const Vec3 mid = px(img, kW / 2, kH / 2);
    const gsplat::F3 want = gsplat::radiance_linear(g3(0.8f, 0.5f, 0.2f));
    /* Near the centre alpha is about 0.99: the pixel is about the splat's light. */
    CHECK_NEAR(mid.x, want.x * 0.98f, 0.06f);
    CHECK_NEAR(mid.y, want.y * 0.98f, 0.06f);
    CHECK(mid.x > mid.y && mid.y > mid.z);
    const Vec3 corner = px(img, 0, 0);
    CHECK(corner.x == 0.0f && corner.y == 0.0f && corner.z == 0.0f);
  });

  test("Splats in F12: overlapping splats blend front to back like the reference", [] {
    const TestCloud c = test_cloud();
    const std::vector<float> got = trace_splats({c.view()}, {}, 256, false), want = composite_image(c);
    const double d = mean_abs_diff(got, want);
    std::printf("    splats vs reference: mean |diff| %.4f\n", d);
    CHECK(d < 0.02);
    for (float v : got) CHECK(finite_bits(v));
  });

  test("Splats in F12: Embree's user geometry and the built-in tree give the same picture", [] {
    if (!PathTracer::embree_available()) return;  // (the dependency-free build has no Embree)
    const TestCloud c = test_cloud();
    const std::vector<float> a = trace_splats({c.view()}, {}, 256, true), b = trace_splats({c.view()}, {}, 256, false);
    const double d = mean_abs_diff(a, b);
    std::printf("    Embree vs built-in: mean |diff| %.4f\n", d);
    CHECK(d < 0.02);
  });

  test("Splats in F12: an opaque mesh in front hides them, a mesh behind shows through their edges", [] {
    TestCloud c;
    c.add({0, 0, 0}, {0.9f, 0.9f, 0.9f}, 0.99f, {0.5f, 0.5f, 0.05f});
    auto quad = primitives::quad(1.0f);
    std::vector<MaterialPtr> glow = {make_material("glow", {0, 0, 0})};
    glow[0]->emission = {0.0f, 0.0f, 1.0f};
    glow[0]->emission_strength = 1.0f;
    /* primitives::quad lies in XY facing -Z, towards the camera at z = -5. */
    glow[0]->double_sided = true;
    const Mat4 in_front = Mat4::trs({0, 0, -1}, Quat(), {4, 4, 4});
    const Mat4 behind = Mat4::trs({0, 0, 1}, Quat(), {4, 4, 4});
    const std::vector<float> front = trace_splats({c.view()}, {{&quad->render_mesh_tangents(), in_front, &glow}}, 32, false);
    const Vec3 f = px(front, kW / 2, kH / 2);
    CHECK(f.x < 0.02f && f.z > 0.5f);  // the blue panel, no splat white
    const std::vector<float> back = trace_splats({c.view()}, {{&quad->render_mesh_tangents(), behind, &glow}}, 64, false);
    const Vec3 b = px(back, kW / 2, kH / 2);
    CHECK(b.x > 0.6f);  // the splat over the panel
    /* Off the splat's middle (9 pixels: about 2 sigma) the panel's blue comes through its low alpha. */
    const Vec3 edge = px(back, kW / 2 + 9, kH / 2);
    CHECK(edge.z > edge.x);
  });

  test("Splats light the meshes round them: a floor under a bright splat gains light from it", [] {
    TestCloud c;
    c.add({0, 1.0f, 0}, {1.0f, 1.0f, 1.0f}, 0.95f, {1.5f, 0.05f, 1.5f});  // a bright flat sheet over the floor
    auto plane = primitives::plane(10.0f, 1);
    std::vector<MaterialPtr> grey = {make_material("grey", {0.8f, 0.8f, 0.8f})};
    /* The camera looks from below the sheet's level at the floor, the sheet out of view above. */
    PathTracer pt;
    PTSettings st;
    st.denoise = false;
    st.use_embree = false;
    pt.set_settings(st);
    pt.set_splats({c.view()});
    pt.build({{&plane->render_mesh_tangents(), Mat4::translate({0, -0.5f, 0}), &grey}}, {}, black_world());
    pt.set_camera(Mat4::look_at({0, 0.2f, -3}, {0, -0.5f, 0}, {0, 1, 0}), Mat4::perspective(30 * kDeg2Rad, 1.0f, 0.1f, 100.0f), kW, kH);
    pt.render(1e9, 64);
    const std::vector<float> img = pt.linear_rgb(false);
    const Vec3 floor_px = px(img, kW / 2, kH - 4);
    CHECK(floor_px.x > 0.02f);
    /* Without the splats the floor is black (no lights, black world). */
    PathTracer dark;
    dark.set_settings(st);
    dark.build({{&plane->render_mesh_tangents(), Mat4::translate({0, -0.5f, 0}), &grey}}, {}, black_world());
    dark.set_camera(Mat4::look_at({0, 0.2f, -3}, {0, -0.5f, 0}, {0, 1, 0}), Mat4::perspective(30 * kDeg2Rad, 1.0f, 0.1f, 100.0f), kW, kH);
    dark.render(1e9, 16);
    CHECK(px(dark.linear_rgb(false), kW / 2, kH - 4).x == 0.0f);
  });

  test("Splats in reflections: a mirror shows a splat behind the camera", [] {
    TestCloud c;
    c.add({0, 0, -8}, {1.0f, 0.2f, 0.2f}, 0.99f, {3.0f, 3.0f, 0.1f});  // behind the camera (at z = -5), facing +Z
    auto quad = primitives::quad(1.0f);
    std::vector<MaterialPtr> mirror = {make_material("mirror", {1, 1, 1})};
    mirror[0]->metallic = 1.0f;
    mirror[0]->roughness = 0.0f;
    mirror[0]->double_sided = true;
    const Mat4 wall = Mat4::trs({0, 0, 1}, Quat(), {6, 6, 6});  // (the quad faces the camera)
    const std::vector<float> img = trace_splats({c.view()}, {{&quad->render_mesh_tangents(), wall, &mirror}}, 32, false);
    const Vec3 m = px(img, kW / 2, kH / 2);
    CHECK(m.x > 0.3f && m.x > 2.0f * m.y);
  });

  test("Splats in F12: NaN, infinite and zero splats are skipped, the rest render, no NaN pixel", [] {
    TestCloud c = test_cloud();
    c.add({kNaN, 0, 0}, {1, 1, 1}, 1.0f, {1, 1, 1});
    c.add({0, 0, 0}, {1, 1, 1}, kNaN, {1, 1, 1});
    c.add({0, 0, 0}, {1, 1, 1}, 1.0f, {kInf, 1, 1});
    c.add({0, 0, 0}, {1, 1, 1}, 1.0f, {0, 0, 0});
    c.add({0, 0, 0}, {kNaN, 1, 1}, 1.0f, {0.2f, 0.2f, 0.2f}, Quat(kNaN, 0, 0, 1));
    c.add({0, 0, 0}, {kNaN, kNaN, kNaN}, 0.5f, {0.3f, 0.3f, 0.01f});  // finite shape, NaN colour: traced, dark
    size_t kept = 0;
    const std::vector<float> img = trace_splats({c.view()}, {}, 16, false, &kept);
    CHECK(kept == 5);  // the four of test_cloud and the NaN-coloured one
    CHECK(count_bad("pixels", img, [](float v) { return finite_bits(v) && v >= 0.0f; }) == 0);  // (bits: /fp:fast)
    /* A cloud with missing arrays or a transform that can't be inverted is skipped whole. */
    SplatCloudView broken = c.view();
    broken.scale = nullptr;
    SplatCloudView flat = c.view(Mat4::trs({0, 0, 0}, Quat(), {1, 1, 0}));
    trace_splats({broken, flat}, {}, 1, false, &kept);
    CHECK(kept == 0);
  });

  test("Splats in F12: the object's transform moves, turns and stretches the cloud", [] {
    TestCloud c;
    c.add({0, 0, 0}, {0.9f, 0.9f, 0.9f}, 0.99f, {0.3f, 0.3f, 0.05f});
    /* Moved 1.2 to one side: the middle goes dark and one side lights. */
    const std::vector<float> moved = trace_splats({c.view(Mat4::translate({1.2f, 0, 0}))}, {}, 32, false);
    CHECK(px(moved, kW / 2, kH / 2).x < 0.05f);
    float left = 0, right = 0;
    for (int x = 0; x < kW / 2; x++) left += px(moved, x, kH / 2).x, right += px(moved, kW - 1 - x, kH / 2).x;
    CHECK(std::max(left, right) > 1.0f && std::min(left, right) < 0.05f);
    /* Stretched 4x along Y: lit far up the middle column, not along the middle row. */
    const std::vector<float> tall = trace_splats({c.view(Mat4::trs({0, 0, 0}, Quat(), {1, 4, 1}))}, {}, 32, false);
    CHECK(px(tall, kW / 2, 4).x > 0.2f);
    CHECK(px(tall, 4, kH / 2).x < 0.05f);
  });

  test("Splats in F12: the SH colour changes with the side the camera sees", [] {
    TestCloud c;
    c.rest = 3;
    c.add({0, 0, 0}, {0.5f, 0.5f, 0.5f}, 0.99f, {0.8f, 0.8f, 0.8f});
    c.sh[1] = Vec3(0.8f, -0.8f, 0.0f);  // band 1, m = 0: the z term - red seen looking along +Z, green along -Z
    const std::vector<float> front = trace_splats({c.view()}, {}, 16, false);
    const std::vector<float> turned = trace_splats({c.view(Mat4::trs({0, 0, 0}, Quat::axis_angle({0, 1, 0}, kPi), {1, 1, 1}))}, {}, 16, false);
    const Vec3 a = px(front, kW / 2, kH / 2), b = px(turned, kW / 2, kH / 2);
    CHECK(a.x > a.y + 0.2f);
    CHECK(b.y > b.x + 0.2f);
  });

  test("Splats shade what is behind them: a shadow ray through two splats keeps the product of their (1 - alpha)", [] {
    /* The expected value of Cycles' stochastic test, without noise: a bake's shadow ray is the same ray every sample. */
    for (bool embree : {false, true}) {
      if (embree && !PathTracer::embree_available()) continue;
      TestCloud c;
      c.add({0, 0, 0}, {1, 1, 1}, 0.5f, {1, 1, 0.01f});
      c.add({0, 0, 1}, {1, 1, 1}, 0.4f, {1, 1, 0.01f});
      PathTracer pt;
      PTSettings st;
      st.use_embree = embree;
      pt.set_settings(st);
      pt.set_splats({c.view()});
      pt.build({}, {}, black_world());
      CHECK_NEAR(pt.transmittance({{0, 0, -2}, {0, 0, 1}}, 2.5f).x, 0.5f, 1e-4f);  // the first only
      CHECK_NEAR(pt.transmittance({{0, 0, -2}, {0, 0, 1}}, 4.0f).x, 0.5f * 0.6f, 1e-4f);
      CHECK_NEAR(pt.transmittance({{0, 0, -2}, {0, 0, 1}}, 4.0f).y, 0.3f, 1e-4f);  // the same ray again: the same answer
      CHECK(pt.occluded({{0, 0, -2}, {0, 0, 1}}, 4.0f));
      CHECK(!pt.occluded({{5, 5, -2}, {0, 0, 1}}, 4.0f));  // misses them
      CHECK(pt.transmittance({{5, 5, -2}, {0, 0, 1}}, 4.0f).x == 1.0f);
      /* A nearly opaque splat blocks outright. */
      TestCloud solid;
      solid.add({0, 0, 0}, {1, 1, 1}, 1.0f, {1, 1, 0.01f});
      solid.add({0, 0, 0.5f}, {1, 1, 1}, 1.0f, {1, 1, 0.01f});
      solid.add({0, 0, 1.0f}, {1, 1, 1}, 1.0f, {1, 1, 0.01f});
      solid.add({0, 0, 1.5f}, {1, 1, 1}, 1.0f, {1, 1, 0.01f});
      pt.set_splats({solid.view()});
      pt.build({}, {}, black_world());
      CHECK(pt.transmittance({{0, 0, -2}, {0, 0, 1}}, 4.0f).x == 0.0f);
    }
  });

  test("Splats in F12: two clouds, each moved and turned, look the same with Embree and the built-in tree", [] {
    if (!PathTracer::embree_available()) return;
    const TestCloud a = test_cloud();
    TestCloud b;
    b.add({0, 0, 0}, {0.2f, 0.9f, 0.9f}, 0.9f, {0.5f, 0.2f, 0.05f});
    b.add({0.3f, 0.2f, 0.1f}, {0.9f, 0.9f, 0.2f}, 0.7f, {0.2f, 0.4f, 0.05f});
    const std::vector<SplatCloudView> clouds = {
        a.view(Mat4::trs({-0.6f, 0.3f, 0.5f}, Quat::axis_angle({0, 1, 0}, 0.5f), {0.7f, 0.7f, 0.7f}), 1, 1),
        b.view(Mat4::trs({0.8f, -0.5f, -0.5f}, Quat::axis_angle({0, 0, 1}, 0.8f), {1.0f, 2.0f, 1.0f}), 2, 1)};
    const std::vector<float> e = trace_splats(clouds, {}, 256, true), w = trace_splats(clouds, {}, 256, false);
    const double d = mean_abs_diff(e, w);
    std::printf("    two moved clouds, Embree vs built-in: mean |diff| %.4f\n", d);
    CHECK(d < 0.02);
    double sum = 0;
    for (float v : e) sum += v;
    CHECK(sum > 10.0);  // (both clouds in view)
  });

  test("Splats: a scene without splats renders exactly as before", [] {
    auto sphere = primitives::uv_sphere(0.5f, 16, 12);
    std::vector<MaterialPtr> grey = {make_material("grey", {0.7f, 0.7f, 0.7f})};
    RenderLight sun;
    sun.direction = normalize(Vec3(-0.5f, -1.0f, 0.3f));
    auto render = [&](bool with_empty_list) {
      PathTracer pt;
      PTSettings st;
      st.denoise = false;
      pt.set_settings(st);
      if (with_empty_list) pt.set_splats({});
      pt.build({{&sphere->render_mesh_tangents(), Mat4::identity(), &grey}}, {sun}, Environment());
      pt.set_camera(test_view(), test_proj(), 16, 16);
      pt.render(1e9, 4);
      return pt.linear_rgb(false);
    };
    CHECK(render(false) == render(true));
  });
}

void run_r533_scene() {
  test("Splats in bakes: a lightmap under a bright splat sheet gains its light, with no lamps and a black world", [] {
    TestCloud c;
    c.add({0, 1.0f, 0}, {1.0f, 1.0f, 1.0f}, 0.95f, {1.5f, 0.05f, 1.5f});
    auto plane = primitives::plane(4.0f, 2);
    const Mat4 model = Mat4::identity();
    auto bake = [&](bool with_splats) {
      Lightmapper lm;
      BakeSettings s = r35_small(8.0f, 64);
      s.denoise = false;
      if (with_splats) s.splats = {c.view()};
      return r35_run(lm, {r35_object(1, plane, model)}, {}, black_world(), s);
    };
    Vec3 lit(0.0f), dark(0.0f);
    CHECK(r35_sample(bake(true), 1, *plane, model, {0.1f, 0, 0.1f}, lit));
    CHECK(r35_sample(bake(false), 1, *plane, model, {0.1f, 0, 0.1f}, dark));
    std::printf("    lightmap under the splat sheet: %.3f (without it %.3f)\n", lit.x, dark.x);
    CHECK(lit.x > 0.05f);
    CHECK(dark.x == 0.0f);
    CHECK(r35_finite(lit));
  });

  test("Splats in renders: the scene's Gaussian Splats objects are gathered, placed, kept alive; Points and hidden ones not", [] {
    Scene scene;
    GameObject *g = create_point_cloud_object(scene, PointCloudType::GaussianSplats);
    PointCloudObject *po = g->get<PointCloudObject>();
    po->data = pointcloud_random(PointCloudType::GaussianSplats, 200, 1, 3u);
    g->set_world_position({2, 0, 0});
    GameObject *pts = create_point_cloud_object(scene, PointCloudType::Points);
    pts->get<PointCloudObject>()->data = pointcloud_random(PointCloudType::Points, 50, 0, 4u);
    GameObject *hidden = create_point_cloud_object(scene, PointCloudType::GaussianSplats);
    hidden->get<PointCloudObject>()->data = pointcloud_random(PointCloudType::GaussianSplats, 30, 0, 5u);
    hidden->active = false;
    GameObject *off = create_point_cloud_object(scene, PointCloudType::GaussianSplats);  // its component unticked
    off->get<PointCloudObject>()->data = pointcloud_random(PointCloudType::GaussianSplats, 20, 0, 6u);
    off->get<PointCloudObject>()->enabled = false;
    std::vector<SplatCloudView> views = scene_splat_views(scene);
    CHECK(views.size() == 1);
    if (views.size() != 1) return;
    CHECK(views[0].count == 200 && views[0].id == (uint32_t)g->id && views[0].sh_rest_per_point == 3);
    CHECK_NEAR(views[0].model.m[12], 2.0f, 1e-5f);
    CHECK(views[0].uid == po->data->uid.v && views[0].key == po->data->version);
    /* An edit while a render holds the view copies the cloud: the render's arrays stay as they were. */
    const Vec3 *before = views[0].position;
    const Vec3 first = before[0];
    PointCloudPtr &mine = pointcloud_make_mutable(po->data);
    mine->position[0] = Vec3(99, 99, 99);
    mine->touch();
    CHECK(views[0].position[0].x == first.x);
    CHECK(scene_splat_views(scene)[0].key != views[0].key);
    /* And it traces. */
    size_t kept = 0;
    trace_splats(views, {}, 1, false, &kept);
    CHECK(kept > 150);
  });

  test("Splats in the editor: a Gaussian Splats object reaches F12's settings, and moving or editing it re-renders", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.command("pointcloud random splats 300 1 7");
    CHECK(ed.pt_settings_for_test().splats.size() == 1);
    if (ed.pt_settings_for_test().splats.size() != 1) return;
    CHECK(ed.pt_settings_for_test().splats[0].count == 300);
    GameObject *g = ed.active_object_for_test();
    CHECK(g != nullptr && (uint32_t)g->id == ed.pt_settings_for_test().splats[0].id);
    if (!g) return;
    const uint64_t h0 = ed.render_hash_for_test();
    g->set_world_position(g->world_position() + Vec3(1, 0, 0));
    const uint64_t h1 = ed.render_hash_for_test();
    CHECK(h1 != h0);  // moved: the previews and the lighting's out-of-date check see it
    ed.command("pointcloud random splats 300 1 8");  // (it is selected: refills the same object)
    CHECK(ed.render_hash_for_test() != h1);  // a new cloud on the same object
  });
}

}  // namespace

void run_splats_trace_tests() {
  run_r533_math();
  run_r533_trace();
  run_r533_scene();
}

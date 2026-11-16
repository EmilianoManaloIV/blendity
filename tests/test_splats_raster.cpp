// SPDX-License-Identifier: GPL-2.0-or-later
// Round 532 (task 0512): Gaussian splats drawn by the rasterizer and the Scene view. The test splats are built in code
// (round 531's importers are not needed).
#include "test_common.h"

#include "../src/render/gsplat_raster.h"
#include "../src/scene/pointcloud.h"

namespace {

using namespace bl;

/* One splat: centre p, standard deviations s, turn q, linear colour c, opacity a. */
PointCloudPtr one_splat(Vec3 p, Vec3 s, Quat q, Vec3 c, float a) {
  auto pc = std::make_shared<PointCloud>();
  pc->type = PointCloudType::GaussianSplats;
  pc->resize(1);
  pc->position[0] = p, pc->scale[0] = s, pc->rotation[0] = q, pc->color[0] = c, pc->opacity[0] = a;
  return pc;
}

struct SplatShot {
  int W = 200, H = 200;
  Mat4 v = Mat4::look_at({0, 0, -5}, {0, 0, 0}, {0, 1, 0});
  Mat4 p = Mat4::perspective(60 * kDeg2Rad, 1.0f, 0.1f, 100.0f);
  Image img;
  RenderTarget rt;
  Renderer3D r;
  SplatPass pass;
  /* Draws `draws` over a black background (and `meshes` first, if any). */
  void draw(const std::vector<SplatDraw> &draws, const std::vector<DrawItem> &meshes = {}) {
    img.resize(W, H);
    rt.attach(img, {0, 0, W, H});
    RasterOptions opt;
    opt.shade = ShadeMode::Gouraud;
    LightingEnv env;
    r.begin(&rt, v, p, env, opt);
    r.clear(0xFF000000u);
    for (const DrawItem &it : meshes) r.add(it);
    SplatView sv{v, p, W, H, ViewTransform::Standard, 0.0f};
    pass.prepare(draws, sv, false);
    r.set_splats(&pass);
    r.flush();
  }
  uint32_t px(int x, int y) const { return img.pixels[(size_t)y * W + x]; }
  uint32_t id(int x, int y) const { return rt.ids[(size_t)y * W + x]; }
};

int red(uint32_t c) { return (c >> 16) & 255; }
int green(uint32_t c) { return (c >> 8) & 255; }

}  // namespace

static void splat_raster_round532_tests() {
  test("splats: a round splat draws as its Gaussian, one sigma at exp(-1/2) of its opacity", [] {
    SplatShot s;
    auto pc = one_splat({0, 0, 0}, {0.1f, 0.1f, 0.1f}, Quat(), {1, 0, 0}, 0.8f);
    s.draw({SplatDraw{pc, Mat4::identity(), 7, false, 0}});
    /* The camera is 5 m away with a 60 degree view: sigma in pixels = 0.1 x (H / 2) / tan(30) / 5. */
    const float focal = 0.5f * s.H / std::tan(30 * kDeg2Rad), sig = 0.1f * focal / 5.0f;
    const SplatScreen &sp = s.pass.screen().at(0);
    CHECK(std::fabs(sp.x - 100.0f) < 0.01f && std::fabs(sp.y - 100.0f) < 0.01f);
    const float a1 = gsplat_alpha_at(sp, sp.x + sig, sp.y);
    CHECK(std::fabs(a1 - 0.8f * std::exp(-0.5f)) < 2.0f / 255.0f);
    CHECK(std::fabs(gsplat_alpha_at(sp, sp.x, sp.y + sig) - a1) < 2.0f / 255.0f);
    CHECK(std::abs(red(s.px(100, 100)) - (int)std::lround(sp.r * 0.8f * 255.0f)) <= 2);
    CHECK(s.id(100, 100) == 7);
    CHECK(s.id(100 + (int)(3.0f * sig), 100) == 0);  // alpha under 0.1: not picked there
  });

  test("splats: a splat behind a mesh is hidden by it, one in front covers it", [] {
    SplatShot s;
    CfScene sc;
    sc.add(primitives::cube(), Mat4::translate(Vec3(0.0f, 0.0f, 0.0f)) * Mat4::scale(Vec3(0.3f)), 9, make_material("a", {0.0f, 0.0f, 1.0f}));
    sc.seal();
    auto behind = one_splat({0, 0, 2}, {0.5f, 0.5f, 0.5f}, Quat(), {1, 0, 0}, 1.0f);
    s.draw({SplatDraw{behind, Mat4::identity(), 7, false, 0}}, sc.items);
    CHECK(s.id(100, 100) == 9);
    CHECK(s.id(100 + 40, 100) == 7);  // beside the cube, the splat shows
    auto front = one_splat({0, 0, -1}, {0.5f, 0.5f, 0.5f}, Quat(), {1, 0, 0}, 1.0f);
    s.draw({SplatDraw{front, Mat4::identity(), 7, false, 0}}, sc.items);
    CHECK(s.id(100, 100) == 7);
  });

  test("splats: overlapping splats blend back to front whatever their stored order", [] {
    SplatShot s;
    auto pc = std::make_shared<PointCloud>();
    pc->type = PointCloudType::GaussianSplats;
    pc->resize(2);
    for (int i = 0; i < 2; i++) pc->scale[i] = Vec3(0.3f), pc->opacity[i] = 0.9f;
    pc->position[0] = {0, 0, -1}, pc->color[0] = {1, 0, 0};  // near: red
    pc->position[1] = {0, 0, 1}, pc->color[1] = {0, 1, 0};   // far: green
    s.draw({SplatDraw{pc, Mat4::identity(), 7, false, 0}});
    const uint32_t a = s.px(100, 100);
    CHECK(red(a) > green(a));
    std::swap(pc->position[0], pc->position[1]);
    pc->touch();
    s.draw({SplatDraw{pc, Mat4::identity(), 7, false, 0}});
    const uint32_t b = s.px(100, 100);
    CHECK(green(b) > red(b));
  });

  test("splats: NaN, zero-scale, huge and transparent splats and an empty cloud draw nothing and crash nothing", [] {
    SplatShot s;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    auto pc = std::make_shared<PointCloud>();
    pc->type = PointCloudType::GaussianSplats;
    pc->resize(5);
    pc->position[0] = {nan, 0, 0};
    pc->scale[1] = Vec3(0.0f);
    pc->scale[2] = Vec3(1e30f);
    pc->opacity[3] = 0.0f;
    pc->scale[4] = Vec3(nan);
    for (int i = 0; i < 5; i++) pc->opacity[i] = i == 3 ? 0.0f : 1.0f;
    s.draw({SplatDraw{pc, Mat4::identity(), 7, false, 0}, SplatDraw{std::make_shared<PointCloud>(), Mat4::identity(), 8, false, 0}});
    CHECK(s.pass.stats().submitted == 5);
  });

  test("splats: the sort orders a million splats back to front (prints its time)", [] {
    auto pc = pointcloud_random(PointCloudType::GaussianSplats, 1000000, 0, 532);
    CHECK(pc != nullptr);
    const Mat4 mvp = Mat4::perspective(60 * kDeg2Rad, 1.0f, 0.1f, 100.0f) * Mat4::look_at({0, 0, -5}, {0, 0, 0}, {0, 1, 0});
    std::vector<uint32_t> ord;
    ScopedTimer t;
    gsplat_sort_back_to_front(*pc, mvp, ord);
    std::printf("    1M splat sort: %.1f ms\n", t.ms());
    CHECK(ord.size() == pc->count());
    bool sorted = true;
    float prev = 1e30f;
    for (uint32_t i : ord) {
      const Vec4 c = mvp * Vec4(pc->position[i], 1.0f);
      const float z = c.z / c.w;
      if (z > prev + 1e-6f) sorted = false;
      prev = z;
    }
    CHECK(sorted);
  });

  test("splats: a big cloud's frame does not wait for its worker sort", [] {
    auto pc = pointcloud_random(PointCloudType::GaussianSplats, SplatSorter::kInlineMax + 1000, 0, 7);
    SplatSorter sorter;
    SplatDraw d{pc, Mat4::identity(), 1, false, 1};
    const Mat4 p = Mat4::perspective(60 * kDeg2Rad, 1.0f, 0.1f, 100.0f);
    auto first = sorter.order(d, p * Mat4::look_at({0, 0, -5}, {0, 0, 0}, {0, 1, 0}));
    CHECK(first && !sorter.last_was_async());  // a cloud's first frame sorts in place
    sorter.hold_worker_for_test(true);
    auto second = sorter.order(d, p * Mat4::look_at({0.5f, 0, -5}, {0, 0, 0}, {0, 1, 0}));
    CHECK(sorter.last_was_async() && second == first);  // the last order, at once
    CHECK(sorter.busy());
    sorter.hold_worker_for_test(false);
    sorter.wait();
    CHECK(!sorter.busy());
  });
}

void run_splats_raster_tests() { splat_raster_round532_tests(); }

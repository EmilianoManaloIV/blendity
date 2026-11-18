// SPDX-License-Identifier: GPL-2.0-or-later
// Round 532 (task 0512): Gaussian splats drawn by the rasterizer and the Scene view. The test splats are built in code
// (round 531's importers are not needed).
#include "test_common.h"

#include <cctype>
#include <chrono>
#include <thread>

#include "../src/editor/scene_splats.h"
#include "../src/render/gpu_device.h"
#include "../src/render/gpu_raster.h"
#include "../src/render/gsplat_raster.h"
#include "../src/scene/pointcloud.h"
#include "../src/scene/pointcloud_object.h"

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
    /* Over black, the pixel (its centre at 100.5, 100.5) is the colour times the alpha there. */
    CHECK(std::abs(red(s.px(100, 100)) - (int)std::lround(sp.r * gsplat_alpha_at(sp, 100.5f, 100.5f) * 255.0f)) <= 2);
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
    /* Beside the cube (5 px across at 5 m), well inside the splat (sigma 12 px at 7 m: alpha 0.27 at 20 px). */
    CHECK(s.id(100 + 20, 100) == 7);
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

/* ===================================================================== */
/* Round 532, the test engineer's pass: the rest of the acceptance criteria */
/* ===================================================================== */
namespace {

/* The display byte of a linear value through the Standard view transform (the sRGB curve, clamped). */
int r532_srgb8(float l) {
  l = l < 0.0f ? 0.0f : l > 1.0f ? 1.0f : l;
  const float e = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
  return (int)std::lround(e * 255.0f);
}

/* The extent (width, height in pixels) of the pixels whose red is over `min_red`. */
void r532_extent(const SplatShot &s, int min_red, int &w, int &h) {
  int x0 = 1 << 30, x1 = -1, y0 = 1 << 30, y1 = -1;
  for (int y = 0; y < s.H; y++)
    for (int x = 0; x < s.W; x++)
      if (red(s.px(x, y)) > min_red) x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
  w = x1 < 0 ? 0 : x1 - x0 + 1;
  h = y1 < 0 ? 0 : y1 - y0 + 1;
}

GameObject *r532_cloud(Editor &ed) {
  GameObject *found = nullptr;
  ed.scene().for_each([&](GameObject &o) {
    if (!found && o.get<PointCloudObject>()) found = &o;
  });
  return found;
}

/* A 1600 x 1000 editor whose scene is only the cloud made by the console command `create` (its data replaced by `pc`):
 * the Scene view on the CPU, orbiting round the origin. Null when the object was not made. */
GameObject *r532_scene(Editor &ed, const char *create, PointCloudPtr pc, bool keep_camera = false) {
  r34_open(ed);
  ed.viewport_device_for_test(1);
  ed.command(create);
  ed.step_frame_headless();
  GameObject *g = r532_cloud(ed);
  if (!g) return nullptr;
  std::vector<GameObject *> others;
  ed.scene().for_each([&](GameObject &o) {
    if (&o != g && !o.parent && !(keep_camera && o.name == "Main Camera")) others.push_back(&o);
  });
  ed.scene().destroy(others);
  g->get<PointCloudObject>()->data = pc;
  ed.mark_changed_for_test("r532 setup");
  ed.command("camera 25 20 7 0 0 0");
  r34_steps(ed, 4);
  return g;
}

bool r532_orange(uint32_t c) {
  return std::abs(red(c) - 247) < 24 && std::abs(green(c) - 148) < 24 && std::abs((int)(c & 255) - 29) < 24;
}

/* A big opaque green splat at the origin (green: the selection outline's orange is not confused with it). */
PointCloudPtr r532_big_splat() { return one_splat({0, 0, 0}, {0.5f, 0.5f, 0.5f}, Quat(), {0, 1, 0}, 1.0f); }

/* Contiguous run of pixels satisfying `is` through (px, py), along x (dx = 1) or y, both ways. */
int r532_run(const std::vector<uint32_t> &crop, int w, int h, int px, int py, bool horizontal, const std::function<bool(uint32_t)> &is) {
  auto at = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && is(crop[(size_t)y * w + x]); };
  int n = 0;
  for (int k = 0; at(horizontal ? px + k : px, horizontal ? py : py + k); k++) n++;
  for (int k = 1; at(horizontal ? px - k : px, horizontal ? py : py - k); k++) n++;
  return n;
}

}  // namespace

static void splat_raster_round532_test_pass() {
  test("splats: a splat stretched along X and turned 90 degrees about the view axis draws taller than wide", [] {
    SplatShot s;
    auto flat = one_splat({0, 0, 0}, {0.5f, 0.08f, 0.08f}, Quat(), {1, 0, 0}, 1.0f);
    s.draw({SplatDraw{flat, Mat4::identity(), 7, false, 0}});
    int w0 = 0, h0 = 0;
    r532_extent(s, 40, w0, h0);
    std::printf("    unturned: %d x %d px\n", w0, h0);
    CHECK(w0 > 3 * h0 && h0 > 0);  // (the control: stretched along X it is wide)
    auto turned = one_splat({0, 0, 0}, {0.5f, 0.08f, 0.08f}, Quat::euler({0, 0, 90}), {1, 0, 0}, 1.0f);
    s.draw({SplatDraw{turned, Mat4::identity(), 7, false, 0}});
    int w1 = 0, h1 = 0;
    r532_extent(s, 40, w1, h1);
    std::printf("    turned 90 degrees: %d x %d px\n", w1, h1);
    CHECK(h1 > 3 * w1 && w1 > 0);
    CHECK(std::abs(w0 - h1) <= 3 && std::abs(h0 - w1) <= 3);  // (the same ellipse, quarter turned)
  });

  test("splats: a degree 1 splat looks different from +X and from -X, each the colour gsplat_radiance gives along that view", [] {
    auto pc = one_splat({0, 0, 0}, {0.6f, 0.6f, 0.6f}, Quat(), {0.4f, 0.4f, 0.4f}, 1.0f);
    pc->set_sh_degree(1);
    CHECK(pc->sh_rest.size() == 3);
    pc->sh_rest[2] = {0.5f, -0.3f, 0.0f};  // band 1, m = +1 (the x term)
    int seen[2] = {0, 0};
    for (int side = 0; side < 2; side++) {
      const float ex = side == 0 ? 5.0f : -5.0f;
      SplatShot s;
      s.v = Mat4::look_at({ex, 0, 0}, {0, 0, 0}, {0, 1, 0});
      s.draw({SplatDraw{pc, Mat4::identity(), 7, false, 0}});
      const Vec3 lin = gsplat_radiance(*pc, 0, {side == 0 ? -1.0f : 1.0f, 0, 0});  // (from the camera towards the splat)
      const SplatScreen &sp = s.pass.screen().at(0);
      const int want[3] = {r532_srgb8(lin.x), r532_srgb8(lin.y), r532_srgb8(lin.z)};
      CHECK(std::abs(sp.r * 255.0f - want[0]) <= 2.0f && std::abs(sp.g * 255.0f - want[1]) <= 2.0f && std::abs(sp.b * 255.0f - want[2]) <= 2.0f);
      const uint32_t c = s.px(100, 100);
      const float a = gsplat_alpha_at(sp, 100.5f, 100.5f);
      std::printf("    from %cX: pixel %d %d %d, radiance through the view transform %d %d %d x alpha %.3f\n", side == 0 ? '+' : '-', red(c), green(c),
                  (int)(c & 255), want[0], want[1], want[2], a);
      CHECK(std::abs(red(c) - (int)std::lround(want[0] * a)) <= 2);
      CHECK(std::abs(green(c) - (int)std::lround(want[1] * a)) <= 2);
      seen[side] = red(c);
    }
    CHECK(std::abs(seen[0] - seen[1]) > 15);
  });

  test("splats: a mirrored model matrix (negative scale) draws the splat where, and as wide as, the mirror image of it", [] {
    /* A turned, stretched splat at x = +1 under diag(-1, 1, 1) is the splat at x = -1 turned the other way. */
    auto obj = one_splat({1, 0, 0}, {0.5f, 0.1f, 0.1f}, Quat::euler({0, 0, 30}), {1, 0, 0}, 0.9f);
    auto ref = one_splat({-1, 0, 0}, {0.5f, 0.1f, 0.1f}, Quat::euler({0, 0, -30}), {1, 0, 0}, 0.9f);
    SplatShot a, b;
    a.draw({SplatDraw{obj, Mat4::scale({-1, 1, 1}), 8, false, 0}});
    b.draw({SplatDraw{ref, Mat4::identity(), 7, false, 0}});
    CHECK(a.pass.screen().size() == 1 && b.pass.screen().size() == 1);
    if (a.pass.screen().empty() || b.pass.screen().empty()) return;
    const SplatScreen &sa = a.pass.screen()[0], &sb = b.pass.screen()[0];
    const Vec4 c = a.p * a.v * Vec4(-1, 0, 0, 1);
    const float ex = (c.x / c.w * 0.5f + 0.5f) * a.W, ey = (0.5f - c.y / c.w * 0.5f) * a.H;
    std::printf("    mirrored centre %.2f %.2f, mirror image's %.2f %.2f, expected %.2f %.2f\n", sa.x, sa.y, sb.x, sb.y, ex, ey);
    CHECK(std::fabs(sa.x - ex) < 0.6f && std::fabs(sa.y - ey) < 0.6f);
    CHECK(std::fabs(sa.x - sb.x) < 0.05f && std::fabs(sa.y - sb.y) < 0.05f);
    float worst = 0.0f;
    for (float dy = -50; dy <= 50; dy += 5)
      for (float dx = -50; dx <= 50; dx += 5)
        worst = std::max(worst, std::fabs(gsplat_alpha_at(sa, sa.x + dx, sa.y + dy) - gsplat_alpha_at(sb, sb.x + dx, sb.y + dy)));
    std::printf("    worst alpha difference over the ellipse: %.4f\n", worst);
    CHECK(worst < 2.0f / 255.0f);
    CHECK(a.id((int)sa.x, (int)sa.y) == 8);
    /* nothing is drawn where the unmirrored splat would be */
    const Vec4 o = a.p * a.v * Vec4(1, 0, 0, 1);
    CHECK(a.id((int)((o.x / o.w * 0.5f + 0.5f) * a.W), (int)((0.5f - o.y / o.w * 0.5f) * a.H)) == 0);
  });

  test("splats: an orthographic camera draws the splat at its projected place, its sigma the world size over the view's height", [] {
    SplatShot s;
    s.p = Mat4::ortho(3.0f, 1.0f, 0.1f, 100.0f);
    auto pc = one_splat({1.0f, 0.5f, 0}, {0.3f, 0.3f, 0.3f}, Quat(), {1, 0, 0}, 0.8f);
    s.draw({SplatDraw{pc, Mat4::identity(), 7, false, 0}});
    CHECK(s.pass.screen().size() == 1);
    if (s.pass.screen().empty()) return;
    const SplatScreen &sp = s.pass.screen()[0];
    const Vec4 c = s.p * s.v * Vec4(1.0f, 0.5f, 0, 1);
    const float ex = (c.x / c.w * 0.5f + 0.5f) * s.W, ey = (0.5f - c.y / c.w * 0.5f) * s.H;
    std::printf("    ortho centre %.2f %.2f, expected %.2f %.2f\n", sp.x, sp.y, ex, ey);
    CHECK(std::fabs(sp.x - ex) < 0.6f && std::fabs(sp.y - ey) < 0.6f);
    const float sig = 0.3f * (s.H * 0.5f) / 3.0f;  // 10 px
    CHECK(std::fabs(gsplat_alpha_at(sp, sp.x + sig, sp.y) - 0.8f * std::exp(-0.5f)) < 2.0f / 255.0f);
    CHECK(std::fabs(gsplat_alpha_at(sp, sp.x, sp.y + sig) - 0.8f * std::exp(-0.5f)) < 2.0f / 255.0f);
    CHECK(s.id((int)sp.x, (int)sp.y) == 7);
    CHECK(red(s.px((int)sp.x, (int)sp.y)) > 150);
  });

  test("splats: a Points cloud draws round dots whose size follows point_size", [] {
    auto pc = std::make_shared<PointCloud>();
    pc->type = PointCloudType::Points;
    pc->resize(1);
    pc->position[0] = {0, 0, 0}, pc->color[0] = {1, 0, 0};
    SplatShot s;
    int run[3] = {0, 0, 0};
    const float sizes[3] = {0.1f, 0.2f, 0.4f};
    for (int k = 0; k < 3; k++) {
      SplatDraw d{pc, Mat4::identity(), 7, false, 0};
      d.point_size = sizes[k];
      s.draw({d});
      const std::vector<uint32_t> px(s.img.pixels.begin(), s.img.pixels.end());
      const auto is_red = [](uint32_t c) { return red(c) > 180 && green(c) < 80; };
      run[k] = std::min(r532_run(px, s.W, s.H, 100, 100, true, is_red), r532_run(px, s.W, s.H, 100, 100, false, is_red));
      std::printf("    point_size %.2f: %d px across, id at its centre %u\n", sizes[k], run[k], s.id(100, 100));
      CHECK(s.id(100, 100) == 7);
    }
    CHECK(run[0] >= 3);
    CHECK(run[1] > run[0] && run[2] > run[1]);
    CHECK(run[2] >= (int)(1.6f * run[1]));
  });

  test("splats: a Gaussian Splats object made by the console is picked by a click on it, not by a click on empty space", [] {
    Editor ed;
    GameObject *g = r532_scene(ed, "create Gaussian Splats", r532_big_splat());
    CHECK(g != nullptr);
    if (!g) return;
    ed.clear_selection_for_test();
    r34_steps(ed, 3);
    CHECK(ed.selection_size_for_test() == 0);
    int cx = 0, cy = 0;
    CHECK(ed.project_to_window(g->world_matrix().point({0, 0, 0}), cx, cy));
    const Recti v = ed.scene_view_rect();
    CHECK(cx > v.x && cx < v.right() && cy > v.y && cy < v.bottom());
    r34_click(ed, v.x + 40, v.y + v.h / 2);  // empty space
    r34_steps(ed, 2);
    CHECK(ed.selection_size_for_test() == 0);
    r34_click(ed, cx, cy);
    r34_steps(ed, 2);
    CHECK(ed.selection_size_for_test() == 1 && ed.active_object_for_test() == g);
    r34_click(ed, v.x + 40, v.y + v.h / 2);  // empty again: deselects
    r34_steps(ed, 2);
    CHECK(ed.selection_size_for_test() == 0);
  });

  test("splats: a selected splat cloud gets the orange outline round its visible splats", [] {
    Editor ed;
    GameObject *g = r532_scene(ed, "create Gaussian Splats", r532_big_splat());
    CHECK(g != nullptr);
    if (!g) return;
    ed.clear_selection_for_test();
    r34_steps(ed, 3);
    const Recti v = ed.scene_view_rect();
    const std::vector<uint32_t> before = r34_crop(ed, v);
    int cx = 0, cy = 0;
    CHECK(ed.project_to_window(g->world_matrix().point({0, 0, 0}), cx, cy));
    r34_click(ed, cx, cy);
    r34_steps(ed, 3);
    CHECK(ed.selection_size_for_test() == 1);
    const std::vector<uint32_t> after = r34_crop(ed, v);
    CHECK(before.size() == after.size());
    int x0 = 1 << 30, x1 = -1, y0 = 1 << 30, y1 = -1;
    size_t n = 0;
    for (size_t i = 0; i < after.size() && i < before.size(); i++) {
      if (!r532_orange(after[i]) || r532_orange(before[i])) continue;
      const int x = (int)(i % (size_t)v.w), y = (int)(i / (size_t)v.w);
      x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y), n++;
    }
    std::printf("    %zu new orange pixels, box %d..%d x %d..%d, splat centre %d %d\n", n, x0, x1, y0, y1, cx - v.x, cy - v.y);
    CHECK(n >= 40);
    CHECK(x0 < cx - v.x - 20 && x1 > cx - v.x + 20 && y0 < cy - v.y - 20 && y1 > cy - v.y + 20);  // (a ring round the centre)
    /* and not on the splat's own centre */
    CHECK(!r532_orange(after[(size_t)(cy - v.y) * v.w + (size_t)(cx - v.x)]));
  });

  test("splats: editing a cloud or moving it changes the render hash and the Scene view redraws", [] {
    Editor ed;
    GameObject *g = r532_scene(ed, "create Gaussian Splats", r532_big_splat());
    CHECK(g != nullptr);
    if (!g) return;
    const Recti v = ed.scene_view_rect();
    const uint64_t h0 = ed.render_hash_for_test();
    const std::vector<uint32_t> p0 = r34_crop(ed, v);
    r34_steps(ed, 3);
    CHECK(ed.render_hash_for_test() == h0);  // (at rest it is stable)
    CHECK(r34_crop(ed, v) == p0);
    ed.command("pointcloud random splats 400 0 3 1.5");  // the selected cloud, refilled
    r34_steps(ed, 3);
    const uint64_t h1 = ed.render_hash_for_test();
    const std::vector<uint32_t> p1 = r34_crop(ed, v);
    CHECK(h1 != h0);
    CHECK(p1 != p0);
    g->set_local_position({1.0f, 0.5f, 0.0f});
    r34_steps(ed, 3);
    const uint64_t h2 = ed.render_hash_for_test();
    CHECK(h2 != h1);
    CHECK(r34_crop(ed, v) != p1);
    /* a second, different refill with another seed differs again */
    ed.command("pointcloud random splats 400 0 4 1.5");
    r34_steps(ed, 3);
    CHECK(ed.render_hash_for_test() != h2);
  });

  test("splats: F12's rasterized render shows the splats", [] {
    Editor ed;
    ed.init_headless(640, 360);
    ed.command("create Gaussian Splats");
    ed.step_frame_headless();
    GameObject *g = r532_cloud(ed);
    CHECK(g != nullptr);
    if (!g) return;
    GameObject *cam = ed.scene().find_by_name("Main Camera");
    CHECK(cam != nullptr);
    if (!cam) return;
    std::vector<GameObject *> others;
    ed.scene().for_each([&](GameObject &o) {
      if (&o != g && &o != cam && !o.parent) others.push_back(&o);
    });
    ed.scene().destroy(others);
    cam->set_local_position({0, 0, -5});
    cam->set_local_rotation(Quat());
    g->get<PointCloudObject>()->data = one_splat({0, 0, 0}, {0.6f, 0.6f, 0.6f}, Quat(), {1, 0, 0}, 1.0f);
    ed.scene().render.engine = 0, ed.scene().render.width = 320, ed.scene().render.height = 180, ed.scene().render.raster_aa = 1;
    ed.mark_changed_for_test("r532 f12");
    ed.step_frame_headless();
    ed.command("render");
    const Image with = ed.render_image_for_test();
    CHECK(with.width == 320 && with.height == 180);
    if (with.width != 320) return;
    const uint32_t c = with.pixels[(size_t)90 * 320 + 160];
    g->active = false;
    ed.mark_changed_for_test("r532 f12 off");
    ed.step_frame_headless();
    ed.command("render");
    const uint32_t bg = ed.render_image_for_test().pixels[(size_t)90 * 320 + 160];
    std::printf("    F12 centre pixel with the splat %d %d %d, without %d %d %d\n", red(c), green(c), (int)(c & 255), red(bg), green(bg), (int)(bg & 255));
    CHECK(red(c) > 150 && red(c) - green(c) > 60);
    CHECK(c != bg);
  });

  test("splats: `splat stats` prints the last frame's counts", [] {
    Editor ed;
    GameObject *g = r532_scene(ed, "create Gaussian Splats", r532_big_splat());
    CHECK(g != nullptr);
    if (!g) return;
    r34_steps(ed, 2);
    std::vector<LogEntry> tmp;
    const size_t mark = Log::fetch((size_t)1 << 40, tmp);
    ed.command("splat stats");
    std::vector<LogEntry> v;
    Log::fetch(mark, v);
    std::string text;
    bool warned = false;
    for (const LogEntry &e : v) {
      if (e.level != LogLevel::Info) warned = true;
      if (e.text.rfind("splats:", 0) == 0) text = e.text;
    }
    std::printf("    %s\n", text.c_str());
    CHECK(!warned && !text.empty());
    size_t submitted = 0, culled = 0, drawn = 0;
    CHECK(std::sscanf(text.c_str(), "splats: %zu submitted, %zu culled, %zu drawn", &submitted, &culled, &drawn) == 3);
    CHECK(submitted >= 1 && drawn >= 1 && culled + drawn <= submitted);
    CHECK(text.find("project") != std::string::npos && text.find("sort") != std::string::npos && text.find("draw") != std::string::npos);
    CHECK(text.find(" ms") != std::string::npos);
    /* a word it doesn't know warns (the usage) */
    const size_t mark2 = Log::fetch((size_t)1 << 40, tmp);
    ed.command("splat bogus");
    std::vector<LogEntry> w;
    Log::fetch(mark2, w);
    bool warns = false;
    for (const LogEntry &e : w) warns = warns || e.level != LogLevel::Info;
    CHECK(warns);
  });

  test("splats: the card draws a small splat scene as the CPU does, the same ids at the probes (skipped without a card)", [] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    std::string err;
    std::unique_ptr<gpu::Raster> g = gpu::Raster::create(0, &err);
    if (!g || !g->can_draw_splats()) {
      std::printf("    skipped: %s\n", g ? "this card draws no splats" : err.c_str());
      return;
    }
    auto a = std::make_shared<PointCloud>(), b = std::make_shared<PointCloud>();
    a->type = b->type = PointCloudType::GaussianSplats;
    a->resize(2), b->resize(1);
    a->position[0] = {-0.6f, 0.2f, 0}, a->scale[0] = {0.45f, 0.12f, 0.12f}, a->rotation[0] = Quat::euler({0, 0, 30}), a->color[0] = {1, 0.1f, 0.1f}, a->opacity[0] = 0.9f;
    a->position[1] = {0.5f, -0.2f, 1}, a->scale[1] = Vec3(0.3f), a->color[1] = {0.1f, 1, 0.1f}, a->opacity[1] = 0.8f;
    b->position[0] = {0, 0.4f, -1}, b->scale[0] = {0.2f, 0.4f, 0.2f}, b->color[0] = {0.2f, 0.2f, 1}, b->opacity[0] = 0.7f;
    SplatShot s;
    const std::vector<SplatDraw> draws = {SplatDraw{a, Mat4::identity(), 7, false, 1}, SplatDraw{b, Mat4::identity(), 8, false, 2}};
    s.draw(draws);
    CHECK(s.pass.screen().size() == 3);
    gpu::RasterFrame fr;
    fr.width = s.W, fr.height = s.H, fr.shadow_size = 0, fr.gouraud = true, fr.background = 0, fr.clear = 0xFF000000u;
    const Mat4 vp = s.p * s.v;
    std::memcpy(fr.view_proj, vp.m, 64);
    std::memcpy(fr.sun_view_proj, vp.m, 64);
    fr.eye[0] = 0, fr.eye[1] = 0, fr.eye[2] = -5;
    /* Round 557: the card projects the splats itself - its own pass and static set, the CPU's picture above as the reference. */
    SplatPass card_pass;
    CardSplats keep;
    const SplatView sv{s.v, s.p, s.W, s.H, ViewTransform::Standard, 0.0f};
    CHECK(scene_splats_to_card(draws, card_pass, sv, keep, *g, fr, false) == nullptr);
    CHECK(fr.splat_order.size() == 3 && fr.splat_ids.size() == 2 && fr.splat_clouds.size() == 2 && fr.splat_src && fr.splat_src->size() == 3);
    g->set_instances({});
    std::vector<uint32_t> col((size_t)s.W * s.H), ids((size_t)s.W * s.H);
    CHECK(g->render_read(fr, col.data(), nullptr, ids.data(), &err));
    const gpu::RasterMatch m = gpu::raster_match(s.img.pixels.data(), s.rt.ids.data(), col.data(), ids.data(), s.W, s.H);
    double sum = 0;
    for (size_t i = 0; i < col.size(); i++) {
      int worst = 0;
      for (int k = 0; k < 24; k += 8) worst = std::max(worst, std::abs((int)((s.img.pixels[i] >> k) & 255) - (int)((col[i] >> k) & 255)));
      sum += worst;
    }
    const double whole = sum / (double)col.size();
    std::printf("    scored %zu px: mean %.3f / 255, %.1f %% within 3, ids same %.1f %%; whole picture mean %.3f\n", m.pixels, m.mean, m.within3 * 100,
                m.ids_same * 100, whole);
    CHECK(m.pixels > 500);
    CHECK(m.mean < 3.0);
    CHECK(whole < 3.0);
    for (size_t i = 0; i < 3; i++) {
      const SplatScreen &sp = s.pass.screen()[i];
      const size_t at = (size_t)(int)sp.y * s.W + (size_t)(int)sp.x;
      if (ids[at] != s.rt.ids[at]) std::printf("    probe %zu at %d %d: card id %u, CPU id %u\n", i, (int)sp.x, (int)sp.y, ids[at], s.rt.ids[at]);
      CHECK(ids[at] == s.rt.ids[at] && s.rt.ids[at] != 0);
    }
    CHECK(ids[(size_t)5 * s.W + 5] == 0 && s.rt.ids[(size_t)5 * s.W + 5] == 0);
  });
}

/* ===================================================================== */
/* Round 532, the review's findings                                      */
/* ===================================================================== */
static void splat_raster_round532_review() {
  test("splats: two views of one big cloud keep their own back-to-front orders (no sort ping-pong between them)", [] {
    auto pc = pointcloud_random(PointCloudType::GaussianSplats, SplatSorter::kInlineMax + 5000, 0, 11, 1.5f);
    CHECK(pc != nullptr);
    if (!pc) return;
    const std::vector<SplatDraw> draws = {SplatDraw{pc, Mat4::identity(), 7, false, 1}};
    const Mat4 p = Mat4::perspective(60 * kDeg2Rad, 1.0f, 0.1f, 100.0f);
    const SplatView a{Mat4::look_at({0, 0, -5}, {0, 0, 0}, {0, 1, 0}), p, 200, 200, ViewTransform::Standard, 0.0f};
    const SplatView b{Mat4::look_at({5, 1, 0}, {0, 0, 0}, {0, 1, 0}), p, 200, 200, ViewTransform::Standard, 0.0f};
    Renderer3D ra, rb;
    /* Each view's first frame sorts in place; after that, drawing the two in turn (as the editor's windows do) must
     * reuse each one's own order - no worker started, nothing sorted again. */
    ra.splat_pass().prepare(draws, a, true);
    rb.splat_pass().prepare(draws, b, true);
    int async_frames = 0;
    for (int k = 0; k < 4; k++) {
      ra.splat_pass().prepare(draws, a, true);
      async_frames += ra.splat_pass().stats().sort_async;
      rb.splat_pass().prepare(draws, b, true);
      async_frames += rb.splat_pass().stats().sort_async;
    }
    CHECK(async_frames == 0);
    CHECK(gsplat_sorts_running() == 0);
    /* The editor swaps each window's renderer into its views by value: the pass goes with it. */
    SplatPass *pa = &ra.splat_pass(), *pb = &rb.splat_pass();
    std::swap(ra, rb);
    CHECK(&ra.splat_pass() == pb && &rb.splat_pass() == pa);
  });

  test("splats: when a worker's order lands the Scene view draws it, and the scene hash saw the change", [] {
    Editor ed;
    GameObject *g = r532_scene(ed, "create Gaussian Splats", pointcloud_random(PointCloudType::GaussianSplats, SplatSorter::kInlineMax + 20000, 0, 5, 1.5f));
    CHECK(g != nullptr);
    if (!g) return;
    const uint64_t misses0 = ed.scene_hash_misses_for_test();
    const uint64_t before = ed.render_hash_for_test(), gen0 = gsplat_sort_generation();
    ed.command("camera 60 20 7 0 0 0");  // a new camera: the big cloud's order goes to a worker
    ed.step_frame_headless();
    /* (The worker may land inside that frame already - a 220,000-splat sort is a few ms - so the hash is compared with
     * the one before the camera moved, which no camera move changes.) */
    /* The worker lands (a few hundred ms at most); the editor keeps drawing frames while one runs. */
    ScopedTimer t;
    while (gsplat_sorts_running() > 0 && t.ms() < 20000.0) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(gsplat_sorts_running() == 0);
    r34_steps(ed, 2);
    CHECK(gsplat_sort_generation() > gen0);         // (a worker's order landed)
    CHECK(ed.render_hash_for_test() != before);  // (and the scene hash has it: the view draws it)
    CHECK(ed.scene_hash_misses_for_test() == misses0);  // (and not one the edit counter missed: ADR 0115)
    std::printf("    hash misses %llu -> %llu\n", (unsigned long long)misses0, (unsigned long long)ed.scene_hash_misses_for_test());
  });

  test("splats: a camera inside huge splats stays within the frame's pixel budget, leaving the biggest out", [] {
    auto pc = pointcloud_random(PointCloudType::GaussianSplats, 20000, 0, 3, 0.5f);
    CHECK(pc != nullptr);
    if (!pc) return;
    for (Vec3 &s : pc->scale) s = Vec3(5.0f);  // every splat far bigger than the screen: each box at the 1024 px cap
    pc->touch();
    SplatShot s;
    s.W = 1600, s.H = 900;
    s.p = Mat4::perspective(60 * kDeg2Rad, 1600.0f / 900.0f, 0.1f, 100.0f);
    ScopedTimer t;
    s.draw({SplatDraw{pc, Mat4::identity(), 7, false, 0}});
    std::printf("    %zu submitted, %zu drawn, %zu left out over the budget; %.1f ms\n", s.pass.stats().submitted, s.pass.stats().drawn,
                s.pass.stats().dropped, t.ms());
    CHECK(s.pass.stats().dropped > 0);
    CHECK(s.pass.stats().drawn > 0);  // (the nearest still draw: the camera is not left blind)
    double area = 0.0;
    for (const SplatScreen &sp : s.pass.screen()) {
      const double w = std::min<double>(s.W, sp.x + sp.rx) - std::max<double>(0.0, sp.x - sp.rx);
      const double h = std::min<double>(s.H, sp.y + sp.ry) - std::max<double>(0.0, sp.y - sp.ry);
      area += w > 0.0 && h > 0.0 ? (w + 1.0) * (h + 1.0) : 1.0;
    }
    CHECK(area <= kSplatPixelBudget);
  });

  test("splats: NaN, zero-scale and transparent splats leave the picture and the id plane untouched", [] {
    SplatShot s;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    auto pc = std::make_shared<PointCloud>();
    pc->type = PointCloudType::GaussianSplats;
    pc->resize(4);
    pc->position[0] = {nan, 0, 0};
    pc->scale[1] = Vec3(0.0f);
    pc->opacity[2] = 0.0f;
    pc->scale[3] = Vec3(nan);
    pc->opacity[0] = pc->opacity[1] = pc->opacity[3] = 1.0f;
    s.draw({});
    const std::vector<uint32_t> before = s.img.pixels, ids_before = s.rt.ids;
    s.draw({SplatDraw{pc, Mat4::identity(), 7, false, 0}});
    CHECK(s.pass.stats().drawn == 0);
    CHECK(s.img.pixels == before);
    CHECK(s.rt.ids == ids_before);
  });
}

/* ===================================================================== */
/* Round 557 (task 0539): the card projects the splats itself            */
/* ===================================================================== */
namespace {

/* The card, or null with a note when this machine has none (or a card without the splat pass). */
std::unique_ptr<gpu::Raster> open_splat_card() {
  if (!gpu::available()) {
    std::printf("    no Vulkan GPU: skipped\n");
    return nullptr;
  }
  std::string err;
  std::unique_ptr<gpu::Raster> g = gpu::Raster::create(0, &err);
  if (!g || !g->can_draw_splats()) {
    std::printf("    skipped: %s\n", g ? "this card draws no splats" : err.c_str());
    return nullptr;
  }
  g->set_instances({});
  return g;
}

/* A card frame over black for `view` (the same camera as SplatShot's), the exposure and view transform as given. */
gpu::RasterFrame splat_card_frame(const SplatShot &s, int view_transform = 0, float exposure = 0.0f) {
  gpu::RasterFrame fr;
  fr.width = s.W, fr.height = s.H, fr.shadow_size = 0, fr.gouraud = true, fr.background = 0, fr.clear = 0xFF000000u;
  fr.view_transform = view_transform, fr.exposure = exposure;
  const Mat4 vp = s.p * s.v;
  std::memcpy(fr.view_proj, vp.m, 64);
  std::memcpy(fr.sun_view_proj, vp.m, 64);
  fr.eye[0] = 0, fr.eye[1] = 0, fr.eye[2] = -5;
  return fr;
}

/* The mean worst-channel difference over the whole picture, of 255. */
double splat_picture_diff(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b) {
  double sum = 0;
  for (size_t i = 0; i < a.size(); i++) {
    int worst = 0;
    for (int k = 0; k < 24; k += 8) worst = std::max(worst, std::abs((int)((a[i] >> k) & 255) - (int)((b[i] >> k) & 255)));
    sum += worst;
  }
  return sum / (double)a.size();
}

}  // namespace

static void splat_raster_round557_tests() {
  test("splats: a degree-3 cloud through the Filmic view transform and an exposure matches the CPU on the card (skipped without a card)", [] {
    std::unique_ptr<gpu::Raster> g = open_splat_card();
    if (!g) return;
    auto pc = pointcloud_random(PointCloudType::GaussianSplats, 400, 3, 21, 1.2f);
    CHECK(pc != nullptr);
    if (!pc) return;
    const std::vector<SplatDraw> draws = {SplatDraw{pc, Mat4::identity(), 7, false, 1}};
    for (int vt = 0; vt <= 2; vt++) {
      SplatShot s;
      const SplatView sv{s.v, s.p, s.W, s.H, (ViewTransform)vt, 0.5f};
      s.img.resize(s.W, s.H);
      s.rt.attach(s.img, {0, 0, s.W, s.H});
      RasterOptions opt;
      opt.shade = ShadeMode::Gouraud;
      LightingEnv env;
      s.r.begin(&s.rt, s.v, s.p, env, opt);
      s.r.clear(0xFF000000u);
      s.pass.prepare(draws, sv, false);
      s.r.set_splats(&s.pass);
      s.r.flush();
      gpu::RasterFrame fr = splat_card_frame(s, vt, 0.5f);
      SplatPass card_pass;
      CardSplats keep;
      CHECK(scene_splats_to_card(draws, card_pass, sv, keep, *g, fr, false) == nullptr);
      std::vector<uint32_t> col((size_t)s.W * s.H), ids((size_t)s.W * s.H);
      std::string err;
      CHECK(g->render_read(fr, col.data(), nullptr, ids.data(), &err));
      const double diff = splat_picture_diff(s.img.pixels, col);
      std::printf("    view transform %d: whole picture mean %.3f / 255\n", vt, diff);
      CHECK(diff < 3.0);
    }
  });

  test("splats: the card keeps a cloud between frames - a second frame sends no splats, an edit or a new cloud sends them again (skipped without a card)", [] {
    std::unique_ptr<gpu::Raster> g = open_splat_card();
    if (!g) return;
    auto a = pointcloud_random(PointCloudType::GaussianSplats, 300, 1, 5, 1.0f);
    CHECK(a != nullptr);
    if (!a) return;
    SplatShot s;
    SplatPass pass;
    CardSplats keep;
    std::vector<uint32_t> col((size_t)s.W * s.H), ids((size_t)s.W * s.H);
    std::string err;
    auto frame = [&](const PointCloudPtr &cloud, const Mat4 &view) {
      const std::vector<SplatDraw> draws = {SplatDraw{cloud, Mat4::identity(), 7, false, 1}};
      gpu::RasterFrame fr = splat_card_frame(s);
      const SplatView sv{view, s.p, s.W, s.H, ViewTransform::Standard, 0.0f};
      CHECK(scene_splats_to_card(draws, pass, sv, keep, *g, fr, false) == nullptr);
      const bool sent = fr.splat_src != nullptr;
      CHECK(g->render_read(fr, col.data(), nullptr, ids.data(), &err));
      return sent;
    };
    const uint64_t before = g->splat_uploads();
    CHECK(frame(a, s.v));
    CHECK(g->splat_uploads() == before + 1 && g->splat_set_key_held() != 0);
    const uint64_t key = g->splat_set_key_held();
    /* The same cloud from another side: only the order and the clouds' values go. */
    CHECK(!frame(a, Mat4::look_at({4, 1, -2}, {0, 0, 0}, {0, 1, 0})));
    CHECK(!frame(a, s.v));
    CHECK(g->splat_uploads() == before + 1 && keep.src.empty() && keep.sh.empty());
    /* An edit (a new version) sends it again. */
    a->opacity[0] = 0.5f;
    a->touch();
    CHECK(frame(a, s.v));
    CHECK(g->splat_uploads() == before + 2 && g->splat_set_key_held() != key);
    CHECK(!frame(a, s.v));
    /* So does another cloud (a new uid) - even a copy with the same numbers. */
    auto copy = std::make_shared<PointCloud>(*a);
    CHECK(frame(copy, s.v));
    CHECK(g->splat_uploads() == before + 3);
    /* Nothing left to draw: the card's copy goes. */
    g->release_splats();
    CHECK(g->splat_set_key_held() == 0);
  });

  test("splats: a set of 256 clouds is refused with a reason and nothing filled, 255 go (skipped without a card; the 512 MB budget is a stress row's size)", [] {
    auto one = one_splat({0, 0, 0}, Vec3(0.1f), Quat(), {1, 1, 1}, 1.0f);
    std::vector<SplatDraw> draws;
    for (int i = 0; i < 256; i++) draws.push_back(SplatDraw{one, Mat4::identity(), (uint32_t)(100 + i), false, (uint64_t)(100 + i)});
    SplatShot s;
    SplatPass pass;
    CardSplats keep;
    gpu::RasterFrame fr;
    std::unique_ptr<gpu::Raster> g = open_splat_card();  // (the function asks the card for the key it holds)
    if (!g) return;
    const SplatView sv{s.v, s.p, s.W, s.H, ViewTransform::Standard, 0.0f};
    const char *why = scene_splats_to_card(draws, pass, sv, keep, *g, fr, false);
    CHECK(why != nullptr && std::string(why).find("255") != std::string::npos);
    CHECK(fr.splat_order.empty() && fr.splat_src == nullptr);
    draws.resize(255);
    CHECK(scene_splats_to_card(draws, pass, sv, keep, *g, fr, false) == nullptr);
    CHECK(fr.splat_order.size() == 255 && fr.splat_ids.size() == 255);
  });

  test("splats: a second card opened after the first closed makes its pipelines from the cache file; a damaged file is ignored (skipped without a card)", [] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    /* In a scratch cache folder, as tests/test_gpu.cpp does: never the user's (restored on every way out). */
    struct EnvGuard {
      std::string keep;
      EnvGuard() {
        const char *old_env = std::getenv("BLENDITY_CACHE_DIR");
        keep = old_env ? old_env : "";
      }
      ~EnvGuard() { set_env("BLENDITY_CACHE_DIR", keep); }
    } guard;
    set_env("BLENDITY_CACHE_DIR", fs::join(test_dir(), "raster_cache"));
    const std::string path = gpu::raster_pipeline_cache_file(0);
    CHECK(!path.empty() && path.find("raster_cache") != std::string::npos);
    std::remove(path.c_str());
    std::string err;
    {
      std::unique_ptr<gpu::Raster> first = gpu::Raster::create(0, &err);
      CHECK(first != nullptr);
      if (!first) return;
      CHECK(!first->pipelines_from_cache());
    }
    CHECK(fs::exists(path));
    {
      std::unique_ptr<gpu::Raster> second = gpu::Raster::create(0, &err);
      CHECK(second != nullptr && second->pipelines_from_cache());
    }
    std::string good;
    CHECK(fs::read_file(path, good) && good.size() > 48);
    /* Garbage in the file: ignored, the card opens and makes its pipelines itself. */
    CHECK(fs::write_file(path, std::string(4096, 'x')));
    {
      std::unique_ptr<gpu::Raster> third = gpu::Raster::create(0, &err);
      CHECK(third != nullptr && !third->pipelines_from_cache());
    }
    /* A cache from another device (the header's deviceID changed, the trailer valid): ignored. */
    std::string blob;
    CHECK(gpu::pipeline_cache_load(0, path, blob) && blob.size() > 32);  // (the third open wrote a good one)
    if (blob.size() > 32) {
      blob[12] = (char)(blob[12] ^ 0x5A);
      gpu::pipeline_cache_store(path, blob);
      std::unique_ptr<gpu::Raster> fourth = gpu::Raster::create(0, &err);
      CHECK(fourth != nullptr && !fourth->pipelines_from_cache());
    }
    std::remove(path.c_str());
  });

  test("splats: a Points cloud's dots on the card match the CPU's (skipped without a card)", [] {
    std::unique_ptr<gpu::Raster> g = open_splat_card();
    if (!g) return;
    auto pc = pointcloud_random(PointCloudType::Points, 25, 0, 3, 1.0f);
    CHECK(pc != nullptr);
    if (!pc) return;
    const std::vector<SplatDraw> draws = {SplatDraw{pc, Mat4::identity(), 7, false, 1, 0.3f}};
    SplatShot s;
    s.draw(draws);
    gpu::RasterFrame fr = splat_card_frame(s);
    SplatPass card_pass;
    CardSplats keep;
    const SplatView sv{s.v, s.p, s.W, s.H, ViewTransform::Standard, 0.0f};
    CHECK(scene_splats_to_card(draws, card_pass, sv, keep, *g, fr, false) == nullptr);
    std::vector<uint32_t> col((size_t)s.W * s.H), ids((size_t)s.W * s.H);
    std::string err;
    CHECK(g->render_read(fr, col.data(), nullptr, ids.data(), &err));
    const gpu::RasterMatch m = gpu::raster_match(s.img.pixels.data(), s.rt.ids.data(), col.data(), ids.data(), s.W, s.H);
    std::printf("    Points: scored %zu px, mean %.3f / 255, ids same %.1f %%\n", m.pixels, m.mean, m.ids_same * 100);
    CHECK(m.pixels > 100 && m.mean < 3.0 && m.ids_same > 0.9);
  });

  test("splats: a camera inside a dense cloud is refused for the card with a reason, the same cloud from 10 m is not (skipped without a card)", [] {
    std::unique_ptr<gpu::Raster> g = open_splat_card();
    if (!g) return;
    auto pc = pointcloud_random(PointCloudType::GaussianSplats, 50000, 0, 9, 1.0f);
    CHECK(pc != nullptr);
    if (!pc) return;
    const std::vector<SplatDraw> draws = {SplatDraw{pc, Mat4::identity(), 7, false, 1}};
    SplatShot s;
    SplatPass pass;
    CardSplats keep;
    gpu::RasterFrame fr;
    const Mat4 inside = Mat4::look_at({0, 0, 0}, {0, 0, 1}, {0, 1, 0}), from_afar = Mat4::look_at({0, 0, -10}, {0, 0, 0}, {0, 1, 0});
    const char *why = scene_splats_to_card(draws, pass, SplatView{inside, s.p, s.W, s.H, ViewTransform::Standard, 0.0f}, keep, *g, fr, false);
    CHECK(why != nullptr && std::string(why).find("too close to the camera") != std::string::npos);
    CHECK(fr.splat_order.empty() && fr.splat_src == nullptr);
    CHECK(scene_splats_to_card(draws, pass, SplatView{from_afar, s.p, s.W, s.H, ViewTransform::Standard, 0.0f}, keep, *g, fr, false) == nullptr);
    CHECK(fr.splat_order.size() == 50000);
  });

  test("splats: a set over the card's largest storage buffer is refused with a reason and nothing is filled (skipped without a card)", [] {
    std::unique_ptr<gpu::Raster> g = open_splat_card();
    if (!g) return;
    auto pc = pointcloud_random(PointCloudType::GaussianSplats, 100, 3, 4, 1.0f);
    CHECK(pc != nullptr);
    if (!pc) return;
    const std::vector<SplatDraw> draws = {SplatDraw{pc, Mat4::identity(), 7, false, 1}};
    SplatShot s;
    SplatPass pass;
    CardSplats keep;
    gpu::RasterFrame fr;
    const SplatView sv{s.v, s.p, s.W, s.H, ViewTransform::Standard, 0.0f};
    CHECK(g->max_storage_buffer_bytes() > 0);
    scene_splats_card_limit_for_test(4096);  // (100 splats are 6400 bytes)
    const char *why = scene_splats_to_card(draws, pass, sv, keep, *g, fr, false);
    scene_splats_card_limit_for_test(0);
    CHECK(why != nullptr && std::string(why).find("storage buffer") != std::string::npos);
    CHECK(fr.splat_order.empty() && fr.splat_src == nullptr && keep.src.empty());
    CHECK(scene_splats_to_card(draws, pass, sv, keep, *g, fr, false) == nullptr && fr.splat_src != nullptr);
  });
}

static void splat_raster_round557_cache_tests() {
  test("splats: the tests keep the cards' caches out of the user's folder (the rasterizer's pipeline cache included)", [] {
    const char *dir = std::getenv("BLENDITY_CACHE_DIR");
    CHECK(dir != nullptr && *dir);
    /* (Round 557b: compared as paths, not as text - a folder given with backslashes is the one fs::join writes with
     * forward slashes; on Windows the case doesn't matter either.) */
    auto norm = [](std::string p) {
      for (char &c : p) {
        if (c == '\\') c = '/';
#ifdef _WIN32
        c = (char)std::tolower((unsigned char)c);
#endif
      }
      while (p.size() > 1 && p.back() == '/') p.pop_back();
      return p;
    };
    const std::string user = norm(fs::join(fs::join(fs::home_dir(), ".blendity"), "cache"));
    const std::string own = dir ? norm(dir) : std::string();
    CHECK(own != user);
    /* (Not its name: a run that sets BLENDITY_CACHE_DIR itself - as the scratch-folder rule asks - is as right as the
     * runner's per-build default. What matters is that the cache is the folder in effect and never the user's.) */
    if (!gpu::available() || gpu::devices().empty()) {
      std::printf("    no Vulkan GPU: the file's path not checked\n");
      return;
    }
    const std::string file = gpu::raster_pipeline_cache_file(gpu::devices()[0].index);
    std::printf("    the rasterizer's pipeline cache: %s\n", file.c_str());
    CHECK(!file.empty() && norm(file).rfind(own + "/", 0) == 0);
    CHECK(norm(file).rfind(user + "/", 0) != 0);
  });
}

/* ---------------------------------------------------------------------------------------------------------------- */
/* Round 566 (task 0548): a million splats on the CPU within the frame budget - the projection in the cloud's own order,  */
/* colours by the span, the binning in parallel, the tile loop's written-out exp. The picture must be what it was.        */
/* ---------------------------------------------------------------------------------------------------------------- */

namespace {

/* The screen list a cloud must give: gsplat_project_one for each splat in the sorter's order, the culled left out. */
std::vector<SplatScreen> r566_reference_list(SplatPass &pass, const SplatDraw &d, const SplatView &sv) {
  const SplatDrawView dv = gsplat_draw_view(d, sv);
  const auto ord = pass.sorter(d.key).order(d, dv.mvp, false);  // (the order prepare() just drew with: the same matrix)
  std::vector<SplatScreen> out;
  if (!ord) return out;
  for (uint32_t i : *ord) {
    SplatScreen s;
    if (!gsplat_project_one(*d.cloud, i, dv.mvp, sv.width, sv.height, dv.cam_local, dv.ortho_dir, sv.view_transform, sv.exposure, s, dv.dot_px)) continue;
    s.id = d.id;
    out.push_back(s);
  }
  return out;
}

/* Bit for bit, but the display colour within `colour_steps` of 1/255: the span encoder's wide kernels give a curve
 * (Filmic, ACES) one step of FMA rounding against its one-pixel path (test_render's "all kernels agree"); Standard is
 * exact on every kernel, so 0 there. */
bool r566_same_splat(const SplatScreen &a, const SplatScreen &b, int colour_steps) {
  const float fa[] = {a.x, a.y, a.z, a.e0x, a.e0y, a.inv0, a.inv1, a.rx, a.ry, a.opacity};
  const float fb[] = {b.x, b.y, b.z, b.e0x, b.e0y, b.inv0, b.inv1, b.rx, b.ry, b.opacity};
  const float tol = (colour_steps + 0.01f) / 255.0f;
  const bool colour = colour_steps == 0 ? (a.r == b.r && a.g == b.g && a.b == b.b)
                                        : (std::fabs(a.r - b.r) <= tol && std::fabs(a.g - b.g) <= tol && std::fabs(a.b - b.b) <= tol);
  return std::memcmp(fa, fb, sizeof(fa)) == 0 && colour && a.id == b.id && a.visible == b.visible && a.dot == b.dot;
}

size_t r566_differing(const std::vector<SplatScreen> &a, const std::vector<SplatScreen> &b, int colour_steps = 0) {
  if (a.size() != b.size()) return std::max(a.size(), b.size());
  size_t bad = 0;
  for (size_t k = 0; k < a.size(); k++) bad += !r566_same_splat(a[k], b[k], colour_steps);
  return bad;
}

/* A plain blend of a screen list over black, as draw_cpu's comment describes it: each pixel front to back with
 * gsplat_alpha_at, no more splats once what shows through is under 1/1024. `ids` gets the front-most splat's id at
 * kSplatPickAlpha; `unsure` marks a pixel where a splat's alpha is within 1e-5 of that edge (the id may go either way
 * with the last bit of exp). */
void r566_reference_blend(const std::vector<SplatScreen> &list, int W, int H, std::vector<float> &rgb, std::vector<uint32_t> &ids, std::vector<uint8_t> &unsure) {
  rgb.assign((size_t)W * H * 3, 0.0f), ids.assign((size_t)W * H, 0u), unsure.assign((size_t)W * H, 0);
  std::vector<float> trans((size_t)W * H, 1.0f);
  std::vector<uint8_t> picked((size_t)W * H, 0);
  for (size_t k = list.size(); k-- > 0;) {
    const SplatScreen &s = list[k];
    const int x0 = std::max(0, (int)std::floor(s.x - s.rx)), x1 = std::min(W - 1, (int)std::floor(s.x + s.rx));
    const int y0 = std::max(0, (int)std::floor(s.y - s.ry)), y1 = std::min(H - 1, (int)std::floor(s.y + s.ry));
    for (int y = y0; y <= y1; y++)
      for (int x = x0; x <= x1; x++) {
        const size_t l = (size_t)y * W + x;
        if (trans[l] < 1.0f / 1024.0f) continue;
        float a;
        if (s.dot) a = (x + 0.5f - s.x) * (x + 0.5f - s.x) + (y + 0.5f - s.y) * (y + 0.5f - s.y) <= s.rx * s.rx ? 1.0f : 0.0f;
        else a = gsplat_alpha_at(s, x + 0.5f, y + 0.5f);
        if (a <= 0.0f) continue;
        const float w = trans[l] * a;
        rgb[l * 3] += s.r * w, rgb[l * 3 + 1] += s.g * w, rgb[l * 3 + 2] += s.b * w;
        trans[l] -= w;
        if (picked[l]) continue;
        if (std::fabs(a - kSplatPickAlpha) < 1e-5f) unsure[l] = 1;
        if (a >= kSplatPickAlpha) ids[l] = s.id, picked[l] = 1;
      }
  }
}

/* Two clouds over each other: 20,000 small splats (several a pixel in the middle: the front-to-back stop works) and 300
 * large ones whose boxes cross the 32 px tiles. */
std::vector<SplatDraw> r566_dense_draws() {
  PointCloudPtr small = pointcloud_random(PointCloudType::GaussianSplats, 20000, 3, 566);
  PointCloudPtr large = pointcloud_random(PointCloudType::GaussianSplats, 300, 1, 567);
  return {SplatDraw{small, Mat4::identity(), 7, false, 0}, SplatDraw{large, Mat4::translate(Vec3(0.2f, 0.1f, 0.0f)), 8, false, 0}};
}

}  // namespace

static void splat_raster_round566_tests() {
  test("splats: a cloud's screen list is each splat as gsplat_project_one gives it, in back-to-front order, the culled left out", [] {
    struct Case {
      const char *name;
      PointCloudType type;
      int degree;
      bool ortho;
      ViewTransform vt;
      float exposure;
      Vec3 eye;
    };
    /* (The eye at 2.2 m sees the cloud's cube past the view's edges: a part of it is culled.) */
    const Case cases[] = {{"degree 3, perspective, Standard", PointCloudType::GaussianSplats, 3, false, ViewTransform::Standard, 0.0f, {0, 0, -5}},
                          {"degree 3, close, Filmic +0.5", PointCloudType::GaussianSplats, 3, false, ViewTransform::Filmic, 0.5f, {0.4f, 0.2f, -2.2f}},
                          {"degree 1, orthographic", PointCloudType::GaussianSplats, 1, true, ViewTransform::Standard, 0.0f, {0, 0, -5}},
                          {"Points, close", PointCloudType::Points, 0, false, ViewTransform::Standard, 0.0f, {0.4f, 0.2f, -2.2f}}};
    for (const Case &c : cases) {
      PointCloudPtr pc = pointcloud_random(c.type, 30000, c.degree, 566);
      /* Some that must be left out whatever the view: transparent, and a NaN centre. */
      pc->position[11] = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
      if (pc->is_splats()) pc->opacity[12] = 0.0f;
      SplatView sv;
      sv.view = Mat4::look_at(c.eye, {0, 0, 0}, {0, 1, 0});
      sv.proj = c.ortho ? Mat4::ortho(1.5f, 320.0f / 240.0f, 0.1f, 100.0f) : Mat4::perspective(60 * kDeg2Rad, 320.0f / 240.0f, 0.1f, 100.0f);
      sv.width = 320, sv.height = 240, sv.view_transform = c.vt, sv.exposure = c.exposure;
      const SplatDraw d{pc, Mat4::translate(Vec3(0.1f, 0.0f, 0.0f)), 5, false, 0, 0.03f};
      SplatPass pass;
      pass.prepare({d}, sv, false);
      const std::vector<SplatScreen> want = r566_reference_list(pass, d, sv);
      const size_t bad = r566_differing(pass.screen(), want, c.vt == ViewTransform::Standard ? 0 : 1);
      std::printf("    %s: %zu of %zu drawn, %zu differ\n", c.name, pass.screen().size(), pc->count(), bad);
      CHECK(!want.empty() && want.size() < pc->count());
      CHECK(pass.screen().size() == want.size());
      CHECK(bad == 0);
      CHECK(pass.stats().culled == pc->count() - want.size());
      /* Back to front - by the sorter's depth, which is the projection's to the last bits only (the same z / w from
       * another expression: GCC put neighbours a rounding apart the other way round; the list is the sorter's order
       * exactly, checked above). */
      bool ordered = true;
      for (size_t k = 1; k < pass.screen().size(); k++) ordered = ordered && pass.screen()[k - 1].z >= pass.screen()[k].z - 1e-5f;
      CHECK(ordered);
    }
  });

  test("splats: a cloud wholly behind the camera or off the screen leaves an empty list and the picture as it was", [] {
    SplatShot s;
    PointCloudPtr pc = pointcloud_random(PointCloudType::GaussianSplats, 9000, 3, 566);
    s.draw({SplatDraw{pc, Mat4::translate(Vec3(0, 0, -20.0f)), 7, false, 0}});  // behind the eye at z = -5
    CHECK(s.pass.screen().empty() && s.pass.stats().culled == 9000);
    s.draw({SplatDraw{pc, Mat4::translate(Vec3(40.0f, 0, 0)), 7, false, 0}});  // far to the side
    CHECK(s.pass.screen().empty() && s.pass.stats().culled == 9000);
    bool black = true;
    for (uint32_t p : s.img.pixels) black = black && (p & 0xFFFFFFu) == 0;
    CHECK(black);
    /* And then the same pass draws a visible cloud (its kept buffers hold the last frame's sizes). */
    s.draw({SplatDraw{pc, Mat4::identity(), 7, false, 0}});
    CHECK(s.pass.screen().size() == 9000 && s.id(100, 100) == 7);
  });

  test("splats: one pass drawing frame after frame - other clouds, ids, views, a culled part, a trimmed frame - gives each frame its own list, nothing left from the last", [] {
    /* (Review 566: the pass keeps last frame's list as this frame's room; every splat of every frame must be this
     * frame's own, whatever the list held before.) */
    SplatPass pass;
    PointCloudPtr points = pointcloud_random(PointCloudType::Points, 12000, 0, 566);
    PointCloudPtr splats = pointcloud_random(PointCloudType::GaussianSplats, 12000, 3, 567);
    PointCloudPtr larger = pointcloud_random(PointCloudType::GaussianSplats, 15000, 1, 568);
    PointCloudPtr smaller = pointcloud_random(PointCloudType::GaussianSplats, 5000, 2, 569);
    PointCloudPtr huge = pointcloud_random(PointCloudType::GaussianSplats, 12000, 0, 570);
    for (Vec3 &sc : huge->scale) sc = Vec3(4.0f);  // (as the budget test: every box at the 1024 px cap)
    SplatView far_view;
    far_view.view = Mat4::look_at({0, 0, -5}, {0, 0, 0}, {0, 1, 0});
    far_view.proj = Mat4::perspective(60 * kDeg2Rad, 320.0f / 240.0f, 0.1f, 100.0f);
    far_view.width = 320, far_view.height = 240;
    SplatView close_view = far_view;
    close_view.view = Mat4::look_at({0.4f, 0.2f, -2.2f}, {0, 0, 0}, {0, 1, 0});
    close_view.view_transform = ViewTransform::Filmic, close_view.exposure = 0.5f;
    SplatView small_view = far_view;
    small_view.width = 200, small_view.height = 120;
    small_view.proj = Mat4::perspective(60 * kDeg2Rad, 200.0f / 120.0f, 0.1f, 100.0f);
    struct Frame {
      const char *name;
      std::vector<SplatDraw> draws;
      SplatView view;
      bool some_culled, trimmed;
    };
    const SplatDraw d_points{points, Mat4::identity(), 5, false, 0, 0.03f}, d_splats{splats, Mat4::identity(), 6, false, 0};
    const SplatDraw d_larger{larger, Mat4::translate(Vec3(0.3f, 0, 0)), 7, false, 0}, d_smaller{smaller, Mat4::identity(), 8, false, 0};
    const SplatDraw d_huge{huge, Mat4::identity(), 9, false, 0};
    const Frame frames[] = {{"Points, all on screen, id 5", {d_points}, far_view, false, false},
                            {"splats of the same count, id 6, close (a part culled), Filmic", {d_splats}, close_view, true, false},
                            {"the same splats, all on screen again", {d_splats}, far_view, false, false},
                            {"huge splats: trimmed to the budget", {d_huge}, far_view, false, true},
                            {"after the trimmed frame, the Points again", {d_points}, far_view, false, false},
                            {"a smaller cloud", {d_smaller}, far_view, false, false},
                            {"a larger cloud, another picture size", {d_larger}, small_view, false, false},
                            {"the larger cloud, close (a part culled)", {d_larger}, close_view, true, false},
                            {"the Points after a culled frame", {d_points}, far_view, false, false}};
    for (const Frame &f : frames) {
      pass.prepare(f.draws, f.view, false);
      const SplatDraw &d = f.draws[0];
      const std::vector<SplatScreen> want = r566_reference_list(pass, d, f.view);
      const int steps = f.view.view_transform == ViewTransform::Standard ? 0 : 1;
      std::printf("    %s: %zu drawn of %zu, %zu culled, %zu left out over the budget\n", f.name, pass.screen().size(), d.cloud->count(),
                  pass.stats().culled, pass.stats().dropped);
      CHECK(pass.stats().culled == d.cloud->count() - want.size());
      CHECK((pass.stats().culled > 0) == f.some_culled);
      CHECK((pass.stats().dropped > 0) == f.trimmed);
      if (f.trimmed) {
        /* What the budget kept is a part of the frame's own list, in its order. */
        size_t at = 0, found = 0;
        for (const SplatScreen &s : pass.screen()) {
          while (at < want.size() && !r566_same_splat(s, want[at], steps)) at++;
          found += at < want.size();
        }
        CHECK(!pass.screen().empty() && found == pass.screen().size());
        continue;
      }
      CHECK(r566_differing(pass.screen(), want, steps) == 0);
    }
    /* Two clouds merged, then one of them alone: the single list is again its own. */
    pass.prepare({d_splats, d_larger}, far_view, false);
    CHECK(pass.screen().size() == splats->count() + larger->count());
    size_t of6 = 0, of7 = 0;
    for (const SplatScreen &s : pass.screen()) of6 += s.id == 6, of7 += s.id == 7;
    CHECK(of6 == splats->count() && of7 == larger->count());
    pass.prepare({d_smaller}, far_view, false);
    CHECK(r566_differing(pass.screen(), r566_reference_list(pass, d_smaller, far_view)) == 0);
  });

  test("splats: a dense cloud's picture is the plain front-to-back blend of its screen list within 1/255, the ids the same", [] {
    SplatShot s;
    s.W = 240, s.H = 200;
    s.p = Mat4::perspective(60 * kDeg2Rad, 240.0f / 200.0f, 0.1f, 100.0f);
    s.draw(r566_dense_draws());
    std::vector<float> rgb;
    std::vector<uint32_t> ids;
    std::vector<uint8_t> unsure;
    r566_reference_blend(s.pass.screen(), s.W, s.H, rgb, ids, unsure);
    int worst = 0;
    size_t lit = 0, id_bad = 0, id_7 = 0, id_8 = 0, skipped = 0;
    for (int y = 0; y < s.H; y++)
      for (int x = 0; x < s.W; x++) {
        const size_t l = (size_t)y * s.W + x;
        const uint32_t c = s.px(x, y);
        const int got[3] = {red(c), green(c), (int)(c & 255)};
        for (int k = 0; k < 3; k++) worst = std::max(worst, std::abs(got[k] - (int)std::lround(std::min(1.0f, rgb[l * 3 + k]) * 255.0f)));
        lit += (c & 0xFFFFFFu) != 0;
        id_7 += ids[l] == 7, id_8 += ids[l] == 8;
        if (unsure[l]) skipped++;
        else id_bad += s.id(x, y) != ids[l];
      }
    std::printf("    %zu splats, %zu lit pixels, worst channel difference %d/255, ids differing %zu (%zu on the pick edge not compared)\n",
                s.pass.screen().size(), lit, worst, id_bad, skipped);
    CHECK(lit > 5000 && id_7 > 100 && id_8 > 100);  // both clouds are seen and picked
    CHECK(worst <= 1);
    CHECK(id_bad == 0);
    CHECK(skipped < 50);
  });

  test("splats: the picture and the ids of a dense cloud are the same on one thread as on all of them", [] {
    JobSystem &js = JobSystem::global();
    const int before = js.max_threads();
    const std::vector<SplatDraw> draws = r566_dense_draws();
    SplatShot all, one;
    all.W = one.W = 240, all.H = one.H = 200;
    all.draw(draws);
    js.set_max_threads(1);
    one.draw(draws);
    js.set_max_threads(before);
    CHECK(all.pass.screen().size() > 20000);
    CHECK(r566_differing(all.pass.screen(), one.pass.screen()) == 0);
    CHECK(all.img.pixels == one.img.pixels);
    CHECK(all.rt.ids == one.rt.ids);
  });

  test("splats: a splat's projected colour is gsplat_radiance through the view transform within a display step - dark, near 1 and SH overshoot", [] {
    /* (The projection reads the sRGB curve from a table; gsplat_radiance is the curve itself.) */
    PointCloudPtr pc = pointcloud_random(PointCloudType::GaussianSplats, 20000, 3, 566);
    /* Base colours across the whole curve: under the 0.04045 knee, round it, near 1, and over 1 (which Filmic keeps
     * apart); the SH bands move each a little further. */
    for (size_t i = 0; i < pc->count(); i++) {
      const float t = (float)(i % 1000) / 999.0f;
      pc->color[i] = i % 4 == 0 ? Vec3(0.09f * t) : i % 4 == 1 ? Vec3(0.9f + 0.2f * t) : i % 4 == 2 ? Vec3(1.0f + 3.0f * t) : Vec3(t, 1.0f - t, 0.5f * t);
    }
    const float inf = std::numeric_limits<float>::infinity();
    pc->color[5] = {inf, 0.5f, -inf};  // (finite in the arrays is the loader's promise; a NaN sum must not crash either)
    pc->sh_rest[6 * 15] = {3e38f, 0, 0}, pc->sh_rest[6 * 15 + 1] = {3e38f, 0, 0}, pc->sh_rest[6 * 15 + 2] = {-3e38f, 0, 0};
    const Mat4 v = Mat4::look_at({0, 0, -5}, {0, 0, 0}, {0, 1, 0}), p = Mat4::perspective(60 * kDeg2Rad, 1.0f, 0.1f, 100.0f);
    const Vec3 cam(0, 0, -5);
    for (ViewTransform vt : {ViewTransform::Standard, ViewTransform::Filmic}) {
      int worst = 0;
      size_t seen = 0;
      for (size_t i = 0; i < pc->count(); i++) {
        SplatScreen s;
        if (!gsplat_project_one(*pc, i, p * v, 200, 200, cam, Vec3(0.0f), vt, 0.0f, s)) continue;
        seen++;
        const uint32_t want = to_display_pixel(gsplat_radiance(*pc, i, pc->position[i] - cam), vt, 0.0f);
        const int got[3] = {(int)std::lround(s.r * 255.0f), (int)std::lround(s.g * 255.0f), (int)std::lround(s.b * 255.0f)};
        const int wanted[3] = {(int)((want >> 16) & 255), (int)((want >> 8) & 255), (int)(want & 255)};
        for (int k = 0; k < 3; k++) worst = std::max(worst, std::abs(got[k] - wanted[k]));
      }
      std::printf("    %s: %zu splats, worst channel difference %d/255\n", vt == ViewTransform::Standard ? "Standard" : "Filmic", seen, worst);
      CHECK(seen > 19000);
      CHECK(worst <= 1);
    }
  });

  test("splats: a cloud whose opacity or SH array is short draws what it can and crashes nothing", [] {
    SplatShot s;
    PointCloudPtr pc = pointcloud_random(PointCloudType::GaussianSplats, 9000, 3, 566);
    pc->sh_rest.resize(pc->sh_rest.size() / 2);  // the second half has no bands: its base colour alone
    pc->opacity.resize(6000);                    // the last 3,000 have no opacity: not drawn
    const SplatDraw d{pc, Mat4::identity(), 7, false, 0};
    s.draw({d});
    CHECK(s.pass.screen().size() == 6000);
    SplatView sv{s.v, s.p, s.W, s.H, ViewTransform::Standard, 0.0f};
    CHECK(r566_differing(s.pass.screen(), r566_reference_list(s.pass, d, sv)) == 0);
    CHECK(s.id(100, 100) == 7);
  });

  test("splats: the tile loop's exp is std::exp to a millionth over a billboard's range, and 0 far past it", [] {
    double worst = 0.0;
    for (int k = 0; k <= 8 * 256; k++) {
      const float q = (float)k / 256.0f;
      const double want = std::exp(-(double)q);
      worst = std::max(worst, std::fabs((double)gsplat_exp_neg(q) - want) / want);
    }
    /* Past the billboard (the loop never asks): looser - a compiler that joins ln 2's two parts loses a little with n. */
    double worst_far = 0.0;
    for (float q = 8.0f; q < 79.0f; q += 0.37f) {
      const double want = std::exp(-(double)q);
      worst_far = std::max(worst_far, std::fabs((double)gsplat_exp_neg(q) - want) / want);
    }
    std::printf("    worst relative difference %.3g to q = 8, %.3g to q = 79\n", worst, worst_far);
    CHECK(worst < 1e-6);
    CHECK(worst_far < 1e-4);
    CHECK(gsplat_exp_neg(0.0f) == 1.0f);
    CHECK(gsplat_exp_neg(80.0f) == 0.0f && gsplat_exp_neg(1e30f) == 0.0f);
    /* A splat's alpha through it is gsplat_alpha_at's. */
    SplatScreen sp;
    sp.x = 10.3f, sp.y = 7.6f, sp.e0x = 0.8f, sp.e0y = 0.6f, sp.inv0 = 1.0f / 3.0f, sp.inv1 = 1.0f / 1.5f, sp.opacity = 0.9f;
    for (float dx = -5.0f; dx <= 5.0f; dx += 0.5f) {
      const float u0 = (dx * sp.e0x) * sp.inv0, u1 = (-dx * sp.e0y) * sp.inv1;
      if (std::fabs(u0) > 2.0f || std::fabs(u1) > 2.0f) continue;
      CHECK(std::fabs(sp.opacity * gsplat_exp_neg(u0 * u0 + u1 * u1) - gsplat_alpha_at(sp, sp.x + dx, sp.y)) < 1e-5f);
    }
  });
}

void run_splats_raster_tests() {
  splat_raster_round532_tests();
  splat_raster_round532_test_pass();
  splat_raster_round532_review();
  splat_raster_round557_cache_tests();
  splat_raster_round557_tests();
  splat_raster_round566_tests();
}

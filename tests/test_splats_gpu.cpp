// SPDX-License-Identifier: GPL-2.0-or-later
// blendity_tests: Gaussian splats in the card path tracer (round 534, task 0514) - group `splats_gpu`.
#include "test_common.h"
#include "../src/render/gsplat_embree.h"
#include "../src/render/gsplat_gpu.h"
#include "../src/render/gsplat_math.h"
#include "../src/scene/pointcloud.h"

#include <cstring>
#include <set>

namespace {

/* A flattened scene with `objects` mesh-less instances, as build_gpu_scene leaves it before the splats. */
gpu::Scene empty_card_scene(size_t objects) {
  gpu::Scene s;
  s.instances.resize(objects);
  if (objects) std::memset(s.instances.data(), 0, objects * sizeof(gpu::GInstance));
  s.object_blas.assign(objects, UINT32_MAX);
  return s;
}

/* A cloud as the path tracer hands it over (gsplat_embree.h). */
SplatCloudView view_of(const PointCloud &pc, uint32_t id = 7, const Mat4 &model = Mat4()) {
  SplatCloudView v;
  v.position = pc.position.data(), v.color = pc.color.data(), v.opacity = pc.opacity.data();
  v.scale = pc.scale.data(), v.rotation = pc.rotation.data(), v.sh_rest = pc.sh_rest.data();
  v.sh_rest_per_point = pc.sh_rest_per_point();
  v.count = pc.is_splats() ? pc.count() : 0;
  v.key = pc.version, v.model = model, v.id = id;
  return v;
}

uint32_t sh_first_of(const gpu::GSplat &g) {
  uint32_t u;
  std::memcpy(&u, &g.ay[3], 4);
  return u;
}

/* Every splat index the cloud's BVH reaches from its root, each leaf's splats inside its node's box. */
bool walk_cloud(const gpu::Scene &s, const gpu::GInstance &gi, std::vector<uint32_t> &seen) {
  std::vector<uint32_t> stack{0};
  while (!stack.empty()) {
    const gpu::GNode &n = s.nodes[gi.off[0] + stack.back()];
    stack.pop_back();
    if (n.count == 0) {
      stack.push_back(n.left), stack.push_back(n.left + 1);
      continue;
    }
    for (uint32_t i = n.left; i < n.left + n.count; i++) {
      const gpu::GSplat &g = s.splats[gi.off[1] + i];
      for (int k = 0; k < 3; k++)
        if (g.c[k] - g.ax[3] < n.bmin[k] - 1e-5f || g.c[k] + g.ax[3] > n.bmax[k] + 1e-5f) return false;
      seen.push_back(i);
    }
  }
  return true;
}


/* A splat scene on one device - 0 the CPU, 1 GPU Compute with ray queries, 2 GPU Compute walking Blendity's BVH,
 * 3 OptiX: the cloud over a grey plane under a low sun (its shadow on the plane), a blue-grey sky. Linear RGB, 3
 * floats a pixel; `device_out` what traced it. */
std::vector<float> splat_scene_render(int device, const PointCloudPtr &pc, int spp, const Mat4 &cloud_model, std::string *device_out) {
  auto plane = primitives::plane(10.0f, 1);
  std::vector<MaterialPtr> mats = {make_material("Grey", Vec3(0.6f))};
  std::vector<PTObject> objs = {{&plane->render_mesh_tangents(), Mat4::translate({0, -0.9f, 0}), &mats}};
  Environment env;
  env.mode = Environment::Color;
  env.color = {0.2f, 0.25f, 0.3f};
  RenderLight sun;
  sun.direction = normalize(Vec3(-0.5f, -1.0f, 0.3f));
  set_env("BLENDITY_GPU_BACKEND", device == 3 ? "optix" : "vulkan");
  PathTracer pt;
  PTSettings st;
  st.use_cpu = device == 0;
  if (device != 0) st.gpus = {0};
  st.gpu_hardware_rt = device != 2;
  st.max_bounces = 4;
  st.denoise = false;
  st.use_guiding = false;
  st.splats = {splat_view_of(pc, cloud_model, 9)};
  pt.set_settings(st);
  pt.build(objs, {sun}, env);
  pt.set_camera(Mat4::look_at({0, 0.7f, -3.4f}, {0, -0.2f, 0}, {0, 1, 0}), Mat4::perspective(45 * kDeg2Rad, 1.0f, 0.1f, 50.0f), 64, 64);
  pt.render(1e9, spp);
  set_env("BLENDITY_GPU_BACKEND", "vulkan");
  if (device_out) *device_out = pt.gpu_error().empty() ? pt.device_summary() : pt.gpu_error();
  return pt.linear_rgb(false);
}

/* Two pictures apart: their means, and the worst 4 x 4 block's mean difference (single pixels are noise here). */
struct ImageDiff { double mean_a = 0, mean_b = 0, worst = 0; };
ImageDiff image_diff(const std::vector<float> &a, const std::vector<float> &b) {
  ImageDiff d;
  const size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; i++) d.mean_a += a[i], d.mean_b += b[i];
  if (n) d.mean_a /= (double)n, d.mean_b /= (double)n;
  for (int by = 0; by < 64; by += 4)
    for (int bx = 0; bx < 64; bx += 4) {
      double sum = 0;
      for (int y = by; y < by + 4; y++)
        for (int x = bx; x < bx + 4; x++)
          for (int c = 0; c < 3; c++) {
            const size_t i = ((size_t)y * 64 + x) * 3 + c;
            if (i < n) sum += (double)a[i] - b[i];
          }
      d.worst = std::max(d.worst, std::fabs(sum) / 48.0);
    }
  return d;
}

/* The card devices this machine has for the match tests (none: skipped). */
std::vector<int> card_devices() {
  std::vector<int> out;
  if (!gpu::available() || gpu::devices().empty()) return out;
  if (gpu::devices()[0].hardware_rt) out.push_back(1);
  out.push_back(2);
  if (gpu::optix_compiled_in() && gpu::optix_device_of(0) >= 0) out.push_back(3);
  return out;
}

/* The card's picture of `pc` placed by `model` against the CPU's. */
void check_card_matches_cpu(const PointCloudPtr &pc, const Mat4 &model) {
  const char *names[] = {"the CPU", "GPU Compute, ray queries", "GPU Compute, BVH walk", "OptiX"};
  std::string dev;
  const std::vector<float> cpu = splat_scene_render(0, pc, 256, model, &dev);
  CHECK(cpu.size() == 64 * 64 * 3);
  /* (Two empty pictures would match too: the splats must be in it - a spread of brightness.) */
  float lo = 1e30f, hi = -1e30f;
  for (size_t i = 0; i + 2 < cpu.size(); i += 3) {
    const float l = 0.2126f * cpu[i] + 0.7152f * cpu[i + 1] + 0.0722f * cpu[i + 2];
    lo = std::min(lo, l), hi = std::max(hi, l);
  }
  CHECK(hi - lo > 0.2f);
  for (int d : card_devices()) {
    const std::vector<float> card = splat_scene_render(d, pc, 256, model, &dev);
    const ImageDiff diff = image_diff(cpu, card);
    std::printf("    %s (%s): mean %.4f against the CPU's %.4f, worst 4x4 block apart by %.4f\n", names[d], dev.c_str(), diff.mean_b, diff.mean_a,
                diff.worst);
    CHECK(card.size() == cpu.size());
    CHECK(std::fabs(diff.mean_b - diff.mean_a) < 0.02 * diff.mean_a);
    CHECK(diff.worst < 0.12);
    if (d == 3) CHECK(dev.find("OptiX") != std::string::npos);
  }
}
}  // namespace

static void round534_tests() {
  test("card splats: three splat clouds go onto the card once each - every splat reachable through its cloud's BVH, the object flagged with its splats, SH and box structure", [] {
    gpu::Scene s = empty_card_scene(1);  // (one mesh object first: the clouds come after it)
    const PointCloudPtr clouds[3] = {pointcloud_random(PointCloudType::GaussianSplats, 100, 0, 1),
                                     pointcloud_random(PointCloudType::GaussianSplats, 517, 3, 2),
                                     pointcloud_random(PointCloudType::GaussianSplats, 1, 1, 3)};
    const uint32_t objects[3] = {1, 2, 3};
    size_t total = 0, sh = 0;
    for (int c = 0; c < 3; c++) {
      gpu::SplatAppend st;
      std::string why;
      CHECK(gpu::gsplat_gpu_append(s, view_of(*clouds[c], 10 + c), &st, &why));
      CHECK(st.splats + st.dropped == clouds[c]->count());
      CHECK(st.bytes > 0);
      total += st.splats;
      sh += st.splats * (size_t)clouds[c]->sh_rest_per_point() * 3;
    }
    CHECK(s.splats.size() == total);
    CHECK(s.splat_sh.size() == sh);
    CHECK(s.splat_aabbs.size() == total * 6);
    CHECK(s.blas.size() == 3);
    CHECK(s.instances.size() == 4 && s.object_blas.size() == 4);
    CHECK(s.object_blas[0] == UINT32_MAX);  // (the mesh-less object stays out)
    CHECK((s.instances[0].info[3] & gpu::kInstSplats) == 0);
    for (int c = 0; c < 3; c++) {
      const gpu::GInstance &gi = s.instances[objects[c]];
      const gpu::GBlas &gb = s.blas[s.object_blas[objects[c]]];
      CHECK(gi.info[3] == gpu::kInstSplats && gi.info[0] == 10u + c);
      CHECK(gi.off[3] == (uint32_t)clouds[c]->sh_rest_per_point());
      CHECK(gb.kind == gpu::kBlasSplats && gb.idx_off == gi.off[1]);
      std::vector<uint32_t> seen;
      CHECK(walk_cloud(s, gi, seen));
      std::set<uint32_t> once(seen.begin(), seen.end());
      CHECK(seen.size() == gb.tri_count && once.size() == gb.tri_count && *once.rbegin() == gb.tri_count - 1);
      /* The instance's bounds hold every splat's sphere; each splat's SH run starts where the last ended. */
      for (uint32_t i = 0; i < gb.tri_count; i++) {
        const gpu::GSplat &g = s.splats[gi.off[1] + i];
        for (int k = 0; k < 3; k++) CHECK(g.c[k] - g.ax[3] >= gi.bmin[k] - 1e-5f && g.c[k] + g.ax[3] <= gi.bmax[k] + 1e-5f);
        CHECK(sh_first_of(g) == i * (uint32_t)clouds[c]->sh_rest_per_point() * 3);
      }
    }
  });

  test("card splats: the card's splats are the CPU path tracer's - the same prepared centre, axes, opacity and radius (gsplat_math.h), the base colour and SH coefficients", [] {
    gpu::Scene s = empty_card_scene(0);
    const PointCloudPtr pc = pointcloud_random(PointCloudType::GaussianSplats, 300, 2, 7);
    CHECK(gpu::gsplat_gpu_append(s, view_of(*pc)));
    const size_t rest = (size_t)pc->sh_rest_per_point();
    size_t matched = 0;
    for (size_t i = 0; i < pc->count(); i++) {
      gsplat::Prepared p;
      const Quat q = pc->rotation[i];
      if (!gsplat::prepare(gsplat::f3(pc->position[i].x, pc->position[i].y, pc->position[i].z),
                           gsplat::f3(pc->scale[i].x, pc->scale[i].y, pc->scale[i].z), gsplat::Q4{q.x, q.y, q.z, q.w}, pc->opacity[i], p))
        continue;
      for (const gpu::GSplat &g : s.splats) {
        if (g.c[0] != p.center.x || g.c[1] != p.center.y || g.c[2] != p.center.z) continue;
        matched++;
        CHECK(g.c[3] == p.opacity && g.ax[3] == p.radius);
        CHECK(g.ax[0] == p.inv_axis_x.x && g.ay[1] == p.inv_axis_y.y && g.az[2] == p.inv_axis_z.z);
        CHECK(g.col[0] == pc->color[i].x && g.col[2] == pc->color[i].z);
        const float *sh = s.splat_sh.data() + s.instances[0].off[2] + sh_first_of(g);
        CHECK(sh[0] == pc->sh_rest[i * rest].x && sh[3 * rest - 1] == pc->sh_rest[i * rest + rest - 1].z);
        break;
      }
    }
    CHECK(matched == s.splats.size() && matched > 250);
  });

  test("card splats: an empty cloud and a cloud too faint to see add nothing to the card; a cloud with missing arrays or a transform that can't be inverted is refused and the scene is left as it was", [] {
    gpu::Scene s = empty_card_scene(2);
    gpu::SplatAppend st;
    CHECK(gpu::gsplat_gpu_append(s, view_of(*pointcloud_random(PointCloudType::Points, 50, 0, 1)), &st) && st.splats == 0);
    PointCloud empty;
    empty.set_type(PointCloudType::GaussianSplats);
    CHECK(gpu::gsplat_gpu_append(s, view_of(empty), &st) && st.splats == 0);
    PointCloudPtr faint = pointcloud_random(PointCloudType::GaussianSplats, 40, 1, 2);
    for (float &o : faint->opacity) o = 0.005f;  // (under gsplat_math.h's 1%: never seen)
    CHECK(gpu::gsplat_gpu_append(s, view_of(*faint), &st) && st.splats == 0 && st.dropped == 40);
    CHECK(s.splats.empty() && s.nodes.empty() && s.blas.empty() && s.instances.size() == 2);
    PointCloudPtr ok = pointcloud_random(PointCloudType::GaussianSplats, 10, 1, 3);
    SplatCloudView missing = view_of(*ok);
    missing.opacity = nullptr;
    std::string why;
    CHECK(!gpu::gsplat_gpu_append(s, missing, &st, &why) && !why.empty());
    Mat4 flat;
    flat.m[0] = flat.m[5] = flat.m[10] = 0.0f;  // (a scale of 0: no inverse)
    why.clear();
    CHECK(!gpu::gsplat_gpu_append(s, view_of(*ok, 1, flat), &st, &why) && !why.empty());
    CHECK(s.splats.empty() && s.blas.empty() && s.instances.size() == 2 && s.object_blas.size() == 2);
  });

  test("card splats: NaN, infinite and zero-size splats never reach the card; the rest of the cloud does, with finite boxes", [] {
    gpu::Scene s = empty_card_scene(0);
    PointCloudPtr pc = pointcloud_random(PointCloudType::GaussianSplats, 20, 1, 4);
    const float nan = std::nanf(""), inf = std::numeric_limits<float>::infinity();
    pc->position[0].x = nan;
    pc->scale[1] = Vec3(0.0f);
    pc->scale[2].y = inf;
    pc->opacity[3] = nan;
    pc->rotation[4] = Quat(nan, 0, 0, 1);
    gpu::SplatAppend st;
    CHECK(gpu::gsplat_gpu_append(s, view_of(*pc), &st));
    CHECK(st.dropped == 5 && st.splats == 15);
    for (float f : s.splat_aabbs) CHECK(std::isfinite(f));
    for (const gpu::GSplat &g : s.splats) CHECK(std::isfinite(g.c[0]) && std::isfinite(g.ax[3]) && g.ax[3] > 0.0f);
  });

  test("card splats: a million splats of degree 3 fit the card's buffers (80 B a splat, 180 B of SH, 24 B a box) and their BVH builds (timing printed, not checked)", [] {
    gpu::Scene s = empty_card_scene(0);
    ScopedTimer make;
    const PointCloudPtr pc = pointcloud_random(PointCloudType::GaussianSplats, 1000000, 3, 11, 10.0f);
    const double make_ms = make.ms();
    CHECK(pc != nullptr);
    if (!pc) return;
    ScopedTimer flat;
    gpu::SplatAppend st;
    CHECK(gpu::gsplat_gpu_append(s, view_of(*pc), &st));
    const double flat_ms = flat.ms();
    CHECK(st.splats + st.dropped == 1000000 && st.splats > 900000);
    CHECK(s.splats.size() == st.splats && s.splat_sh.size() == st.splats * 45 && s.splat_aabbs.size() == st.splats * 6);
    CHECK(s.nodes.size() < st.splats);  // (leaves of up to 4)
    CHECK(gpu::gsplat_gpu_bytes(view_of(*pc)) >= st.bytes);
    /* The deepest leaf stays within the kernel's 32-entry traversal stack. */
    int deepest = 0;
    std::vector<std::pair<uint32_t, int>> stack{{0, 1}};
    while (!stack.empty()) {
      const auto [n, d] = stack.back();
      stack.pop_back();
      deepest = std::max(deepest, d);
      if (s.nodes[n].count == 0) stack.push_back({s.nodes[n].left, d + 1}), stack.push_back({s.nodes[n].left + 1, d + 1});
    }
    CHECK(deepest < 31);
    std::printf("    1M splats: cloud %.0f ms, onto the card's buffers %.0f ms (%.0f MB, BVH depth %d)\n", make_ms, flat_ms, st.bytes / 1048576.0, deepest);
  });

  test("card splats: a cloud of 2 000 splats (SH degree 3) over a sunlit plane renders on the card - with ray queries, walking the BVH, and through OptiX where the card has it - as the CPU path tracer renders it: its colours, its see-through edges and its shadow on the plane (skipped without a card)", [] {
    if (card_devices().empty()) {
      std::printf("    (no GPU here: skipped)\n");
      return;
    }
    check_card_matches_cpu(pointcloud_random(PointCloudType::GaussianSplats, 2000, 3, 21, 0.6f), Mat4());
  });

  test("card splats: a cloud moved, turned, stretched and mirrored renders on the card as on the CPU - the splats and their view-dependent colour follow the object (skipped without a card)", [] {
    if (card_devices().empty()) {
      std::printf("    (no GPU here: skipped)\n");
      return;
    }
    const Mat4 model = Mat4::trs({0.3f, 0.1f, 0.2f}, Quat::axis_angle({0, 1, 0}, 0.7f), {-1.2f, 0.8f, 1.0f});
    check_card_matches_cpu(pointcloud_random(PointCloudType::GaussianSplats, 2000, 3, 33, 0.6f), model);
  });

  test("card splats: the path tracer's kernel with the splats compiles for cards with and without ray queries (skipped without Vulkan's shader compiler)", [] {
    if (!gpu::compiled_in()) {
      std::printf("    (no Vulkan in this build: skipped)\n");
      return;
    }
    std::string err;
    CHECK(gpu::compile_kernels(false, &err));
    if (!err.empty()) std::printf("    %s\n", err.c_str());
    err.clear();
    CHECK(gpu::compile_kernels(true, &err));
    if (!err.empty()) std::printf("    %s\n", err.c_str());
  });
}

void run_splats_gpu_tests() {
  round534_tests();
}

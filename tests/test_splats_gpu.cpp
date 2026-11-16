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

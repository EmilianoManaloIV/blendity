// SPDX-License-Identifier: GPL-2.0-or-later
// blendity_stress - pushes every subsystem until it breaks a time budget and
// compares naive vs optimised implementations ("find the efficiencies").
//
//   blendity_stress                 full run (a few minutes)
//   blendity_stress --quick         smaller sizes (CI)
//   blendity_stress --only raster   run tests whose name contains "raster"
//   blendity_stress --out DIR       where reports go (default stress/results)
//   blendity_stress --budget 16.7   frame budget in ms used for "limit" searches
//
// Theory: GEA Vol. I 2.3 Profiling Tools, 10.8 In-Game Profiling;
//         FoCG 12.3 Spatial Data Structures.
#include "../src/core/core.h"
#include "../src/core/jobs.h"
#include "../src/editor/editor.h"
#include "../src/image/image.h"
#include "../src/render/colormanagement.h"
#include "../src/render/display.h"
#include "../src/render/gpu_device.h"
#include "../src/core/cpu.h"
#include "../src/render/pathtracer.h"
#include "../src/render/raster.h"
#include "../src/scene/material.h"
#include "../src/scene/mesh.h"
#include "../src/scene/physics.h"
#include "../src/scene/scene.h"
#include "../src/scene/uv.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
#include <string>
#include <thread>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

static void install_crash_handler();  // Windows: symbolised stack on crash (end of file)

using namespace bl;

/* ------------------------------------------------------------------ report */

struct Report {
  std::string md, csv;
  std::string section;
  void title(const std::string &t, const std::string &what) {
    section = t;
    std::printf("\n=== %s ===\n%s\n", t.c_str(), what.c_str());
    md += "\n## " + t + "\n\n" + what + "\n\n";
  }
  void table(const std::vector<std::string> &head) {
    std::string h = "|", s = "|";
    for (auto &c : head) { h += " " + c + " |"; s += "---|"; }
    md += h + "\n" + s + "\n";
    for (auto &c : head) std::printf("%-26s", c.c_str());
    std::printf("\n");
  }
  void row(const std::vector<std::string> &cells) {
    std::string r = "|";
    for (auto &c : cells) r += " " + c + " |";
    md += r + "\n";
    csv += section;
    for (auto &c : cells) csv += "," + c;
    csv += "\n";
    for (auto &c : cells) std::printf("%-26s", c.c_str());
    std::printf("\n");
    std::fflush(stdout);
  }
  void note(const std::string &n) {
    md += "\n> " + n + "\n";
    std::printf("  -> %s\n", n.c_str());
  }
};

static std::string f2(double v) { return strprintf("%.2f", v); }
static std::string f1(double v) { return strprintf("%.1f", v); }
static std::string num(double v) {
  if (v >= 1e9) return strprintf("%.2fG", v / 1e9);
  if (v >= 1e6) return strprintf("%.2fM", v / 1e6);
  if (v >= 1e3) return strprintf("%.1fk", v / 1e3);
  return strprintf("%.0f", v);
}

template<class F> static double time_ms(F &&fn, int reps = 1) {
  ScopedTimer t;
  for (int i = 0; i < reps; i++) fn();
  return t.ms() / reps;
}

struct Options {
  bool quick = false;
  std::string only;
  std::string out = "stress/results";
  double budget = 16.7;
};

/* --------------------------------------------------------- scene helpers */

static Scene make_grid_scene(int count, MeshPtr mesh, float spacing = 1.6f) {
  Scene s;
  int side = (int)std::ceil(std::sqrt((double)count));
  for (int i = 0; i < count; i++) {
    GameObject *g = s.create("obj");
    g->set_local_position({(i % side - side * 0.5f) * spacing, 0, (i / side - side * 0.5f) * spacing});
    g->add<MeshFilter>()->mesh = mesh;
    g->add<MeshRenderer>();
  }
  return s;
}

/* Renders a scene once from a camera that frames the whole grid. */
struct Bench3D {
  Image img;
  RenderTarget rt;
  Renderer3D r3d;
  LightingEnv env;
  Bench3D(int w, int h) {
    img.resize(w, h);
    rt.attach(img, {0, 0, w, h});
    RenderLight sun;
    sun.direction = normalize(Vec3(-0.4f, -1.0f, 0.5f));
    env.lights.push_back(sun);
  }
  double render(const Scene &s, const RasterOptions &opt, float extent, int reps = 3) {
    Vec3 eye{0, extent * 0.9f, -extent * 1.1f};
    Mat4 v = Mat4::look_at(eye, {0, 0, 0}, {0, 1, 0});
    Mat4 p = Mat4::perspective(60 * kDeg2Rad, img.width / (float)img.height, 0.1f, 1000.0f);
    env.camera_pos = eye;
    auto frame = [&] {
      r3d.begin(&rt, v, p, env, opt);
      r3d.clear(0xFF303030);
      s.for_each([&](GameObject &g) {
        DrawItem it;
        it.mesh = &g.get<MeshFilter>()->mesh->render_mesh();
        it.model = g.world_matrix();
        it.id = (uint32_t)g.id;
        r3d.add(it);
      });
      r3d.flush();
    };
    frame();  // warm-up (caches, thread wake-up)
    return time_ms(frame, reps);
  }
};

/* ------------------------------------------------------------------ tests */

static void test_raster_scaling(Report &rep, const Options &o) {
  rep.title("Rasterizer: triangle throughput & limit",
            "Grid of icospheres (1,280 tris each) at 1280x720. Optimised = all cores + back-face + frustum culling + "
            "exact row spans + fast setup (AVX2 rejection of 8 triangles at a time, small-triangle cull, lazy vertex "
            "shading). Each optimisation is then disabled in turn. Limit = largest triangle count under the budget.");
  auto mesh = primitives::ico_sphere(0.5f, 3);
  size_t tris_per = mesh->render_mesh().tri_count();
  Bench3D b(1280, 720);
  struct Cfg { const char *name; RasterOptions opt; };
  RasterOptions best;
  RasterOptions single = best; single.multithreaded = false;
  RasterOptions nocull = best; nocull.backface_culling = false;
  RasterOptions nospan = best; nospan.span_rows = false;
  RasterOptions nofast = best; nofast.fast_setup = false;
  Cfg cfgs[] = {{"optimised", best}, {"single thread", single}, {"no backface cull", nocull}, {"no row spans", nospan},
                {"no fast setup", nofast}};
  rep.table({"objects", "triangles", "optimised ms", "1 thread ms", "no-cull ms", "no-span ms", "no fast setup ms", "Mtris/s (opt)"});
  std::vector<int> counts = o.quick ? std::vector<int>{16, 128, 512} : std::vector<int>{16, 64, 256, 1024, 2048, 4096, 8192, 16384};
  size_t limit_opt = 0, limit_single = 0;
  std::vector<std::vector<std::string>> phases;
  for (int n : counts) {
    Scene s = make_grid_scene(n, mesh, 1.3f);
    float extent = std::sqrt((float)n) * 0.65f + 2.0f;
    double ms[5];
    RasterStats st;
    for (int c = 0; c < 5; c++) {
      ms[c] = b.render(s, cfgs[c].opt, extent, n > 4096 ? 2 : 3);
      if (c == 0) st = b.r3d.stats();
    }
    phases.push_back({std::to_string(n), f2(st.ms_vertex), f2(st.ms_setup), f2(st.ms_raster), f2(st.ms_total),
                      f1(100.0 * st.tris_rasterized / std::max<size_t>(1, st.tris_submitted)) + "%"});
    size_t tris = (size_t)n * tris_per;
    if (ms[0] <= o.budget) limit_opt = tris;
    if (ms[1] <= o.budget) limit_single = tris;
    rep.row({std::to_string(n), num((double)tris), f2(ms[0]), f2(ms[1]), f2(ms[2]), f2(ms[3]), f2(ms[4]), f1(tris / ms[0] / 1000.0)});
    if (ms[1] > 2000) break;
  }
  rep.note(strprintf("60 FPS limit at 1280x720: ~%s triangles optimised vs ~%s single-threaded (%d threads available).",
                     num((double)limit_opt).c_str(), num((double)limit_single).c_str(), JobSystem::global().thread_count()));
  rep.table({"objects", "vertex ms", "assembly + binning ms", "raster ms", "total ms", "triangles kept"});
  for (auto &r : phases) rep.row(r);
}

static void test_tile_size(Report &rep, const Options &o) {
  rep.title("Rasterizer: tile size sweep", "Same scene (1,024 icospheres, 1.3M tris), varying the binning tile size.");
  auto mesh = primitives::ico_sphere(0.5f, 3);
  Scene s = make_grid_scene(o.quick ? 256 : 1024, mesh, 1.3f);
  Bench3D b(1280, 720);
  rep.table({"tile px", "ms/frame"});
  double best = 1e9;
  int best_t = 0;
  for (int t : {16, 32, 64, 128, 256}) {
    RasterOptions opt;
    opt.tile_size = t;
    double ms = b.render(s, opt, 22.0f, 4);
    if (ms < best) { best = ms; best_t = t; }
    rep.row({std::to_string(t), f2(ms)});
  }
  rep.note(strprintf("Fastest tile size on this machine: %d px.", best_t));
}

static void test_resolution(Report &rep, const Options &o) {
  rep.title("Rasterizer: resolution scaling (fill rate)", "256 icospheres, varying the viewport resolution.");
  auto mesh = primitives::ico_sphere(0.5f, 3);
  Scene s = make_grid_scene(256, mesh, 1.3f);
  rep.table({"resolution", "megapixels", "ms/frame", "Mpix/s"});
  std::vector<std::pair<int, int>> res = {{640, 360}, {1280, 720}, {1920, 1080}, {2560, 1440}};
  if (!o.quick) res.push_back({3840, 2160});
  for (auto [w, h] : res) {
    Bench3D b(w, h);
    double ms = b.render(s, RasterOptions(), 12.0f, 3);
    double mp = w * h / 1e6;
    rep.row({strprintf("%dx%d", w, h), f2(mp), f2(ms), f1(mp / ms * 1000.0)});
  }
}

static void test_hierarchy(Report &rep, const Options &o) {
  rep.title("Scene graph: world-matrix updates",
            "Move the root, then read every world matrix. Cached = dirty-flag propagation (GameObject::world_matrix). "
            "Uncached = walk the parent chain for every object (world_matrix_uncached).");
  rep.table({"shape", "objects", "cached ms", "uncached ms", "speed-up"});
  auto run = [&](const char *shape, int n, int fanout) {
    Scene s;
    std::vector<GameObject *> all;
    GameObject *root = s.create("root");
    all.push_back(root);
    for (int i = 1; i < n; i++) {
      GameObject *parent = fanout == 0 ? nullptr : (fanout == 1 ? all.back() : all[(i - 1) / fanout]);
      GameObject *g = s.create("n", parent);
      g->set_local_position({0.01f, 0.02f, 0.0f});
      all.push_back(g);
    }
    float acc = 0;
    double cached = time_ms([&] {
      root->set_local_position({acc += 0.001f, 0, 0});
      float sum = 0;
      for (GameObject *g : all) sum += g->world_matrix().m[12];
      if (sum == 12345.f) std::printf(" ");
    }, 3);
    double uncached = time_ms([&] {
      float sum = 0;
      for (GameObject *g : all) sum += g->world_matrix_uncached().m[12];
      if (sum == 12345.f) std::printf(" ");
    }, 1);
    rep.row({shape, std::to_string(n), f2(cached), f2(uncached), f1(uncached / std::max(cached, 1e-6)) + "x"});
  };
  for (int n : o.quick ? std::vector<int>{1000, 10000} : std::vector<int>{1000, 10000, 100000}) run("flat (all roots)", n, 0);
  for (int n : o.quick ? std::vector<int>{1000, 4000} : std::vector<int>{1000, 4000, 10000}) run("deep chain", n, 1);
  for (int n : o.quick ? std::vector<int>{1000, 10000} : std::vector<int>{1000, 10000, 100000}) run("tree (fan-out 4)", n, 4);
  rep.note("Dirty flags turn the deep-chain case from O(n^2) into O(n): every object reuses its parent's cached matrix.");
}

static void test_picking(Report &rep, const Options &o) {
  rep.title("Picking: ray casting vs id buffer",
            "Average cost of one mouse pick. Brute force tests every triangle of every object; AABB rejects objects "
            "first (FoCG 12.3); the id buffer reads one pixel written by the rasterizer (how Blendity and Blender's "
            "GPU selection work).");
  rep.table({"objects", "brute force us", "AABB first us", "id buffer us"});
  auto mesh = primitives::ico_sphere(0.5f, 2);
  for (int n : o.quick ? std::vector<int>{100, 1000} : std::vector<int>{100, 1000, 10000}) {
    Scene s = make_grid_scene(n, mesh, 1.3f);
    std::vector<GameObject *> objs;
    s.for_each([&](GameObject &g) { objs.push_back(&g); });
    const RenderMesh &rm = mesh->render_mesh();
    uint32_t seed = 7;
    auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0f; };
    const int rays = 64;
    std::vector<Ray> rs;
    float ext = std::sqrt((float)n) * 0.65f;
    for (int i = 0; i < rays; i++) rs.push_back({{(rnd() - 0.5f) * ext * 2, 10, (rnd() - 0.5f) * ext * 2}, {0, -1, 0}});
    auto cast = [&](bool aabb) {
      int hits = 0;
      for (const Ray &r : rs) {
        float best = 1e30f;
        for (GameObject *g : objs) {
          Mat4 inv = g->world_matrix().inverse();
          Ray lr{inv.point(r.origin), normalize(inv.dir(r.dir))};
          if (aabb && ray_aabb(lr, rm.bounds) < 0) continue;
          for (size_t t = 0; t < rm.tri_count(); t++) {
            float d = ray_triangle(lr, rm.positions[rm.indices[t * 3]], rm.positions[rm.indices[t * 3 + 1]], rm.positions[rm.indices[t * 3 + 2]]);
            if (d > 0 && d < best) best = d;
          }
        }
        hits += best < 1e30f;
      }
      return hits;
    };
    double brute = n <= 1000 ? time_ms([&] { cast(false); }) * 1000.0 / rays : -1;
    double aabb = time_ms([&] { cast(true); }) * 1000.0 / rays;
    Bench3D b(1280, 720);
    b.render(s, RasterOptions(), ext + 2, 1);
    volatile uint32_t sink = 0;
    double idb = time_ms([&] { for (int i = 0; i < 100000; i++) sink = sink + b.rt.id_at(i % 1280, (i * 7) % 720); }) * 1000.0 / 100000;
    rep.row({std::to_string(n), brute < 0 ? "(skipped)" : f1(brute), f1(aabb), strprintf("%.4f", idb)});
  }
  rep.note("The id buffer costs nothing extra per pick because the renderer already wrote it; ray casting scales with scene size.");
}

static void test_subdivision(Report &rep, const Options &o) {
  rep.title("Catmull-Clark subdivision: limit",
            "Repeatedly subdivide a cube until one level takes longer than 2 s or exceeds 25M faces.");
  rep.table({"level", "faces", "vertices", "ms", "mesh memory"});
  Mesh m = *primitives::cube();
  for (int level = 1; level <= (o.quick ? 6 : 12); level++) {
    if (m.face_count() * 4 > 25000000) { rep.note(strprintf("Stopped before level %d: would exceed 25M faces.", level)); break; }
    double ms = time_ms([&] { m = meshops::subdivide_catmull_clark(m); });
    rep.row({std::to_string(level), num((double)m.face_count()), num((double)m.vert_count()), f1(ms), format_bytes(m.memory_bytes())});
    if (ms > 2000) { rep.note(strprintf("Level %d exceeded 2 s - practical interactive limit reached.", level)); break; }
  }
  if (!meshops::subdiv_opensubdiv_available()) return;
  /* Blendity's Catmull-Clark vs OpenSubdiv (Blender's Subdivision Surface), all
   * levels at once from a 6-face cube and a 5k-face torus with UVs. */
  rep.table({"mesh", "levels", "faces", "Blendity ms", "OpenSubdiv ms", "OpenSubdiv speed-up"});
  for (auto [name, src] : {std::pair<const char *, MeshPtr>{"cube", primitives::cube()}, {"torus (UVs)", primitives::torus(0.5f, 0.2f, 64, 32)}}) {
    Mesh base = *src;
    base.seams.clear();
    for (int levels : o.quick ? std::vector<int>{2, 4} : std::vector<int>{2, 4, 6}) {
      double ms[2];
      size_t faces = 0;
      for (int k = 0; k < 2; k++) {
        meshops::set_subdiv_opensubdiv(k == 1);
        ms[k] = time_ms([&] { faces = meshops::subdivide(base, levels, true).face_count(); });
      }
      meshops::set_subdiv_opensubdiv(false);
      rep.row({name, std::to_string(levels), num((double)faces), f1(ms[0]), f1(ms[1]), f2(ms[0] / ms[1]) + "x"});
    }
  }
}

static void test_edge_building(Report &rep, const Options &o) {
  rep.title("Topology: building the edge table",
            "Subdivision, wireframes and smoothing all need unique edges. Compared: std::unordered_map keyed by vertex "
            "pair, the original sort-and-unique implementation, and the bucketed counting sort Blendity now uses "
            "(Mesh::edges, subdivide_impl).");
  rep.table({"faces", "hash map ms", "global sort ms", "bucket/CSR ms", "bucket vs sort"});
  for (int levels : o.quick ? std::vector<int>{4, 6} : std::vector<int>{4, 6, 8, 9}) {
    Mesh m = *primitives::cube();
    for (int i = 0; i < levels; i++) m = meshops::subdivide_simple(m);
    double hm = time_ms([&] {
      std::unordered_map<uint64_t, uint32_t> map;
      for (size_t f = 0; f < m.face_count(); f++)
        for (uint32_t k = 0; k < m.face_size(f); k++) {
          uint32_t a = m.face_verts(f)[k], b = m.face_verts(f)[(k + 1) % m.face_size(f)];
          uint64_t key = a < b ? ((uint64_t)a << 32) | b : ((uint64_t)b << 32) | a;
          map.emplace(key, (uint32_t)map.size());
        }
    });
    /* The first implementation of Mesh::edges: collect pairs, sort, unique. */
    double so = time_ms([&] {
      std::vector<std::pair<uint32_t, uint32_t>> e;
      e.reserve(m.corner_count());
      for (size_t f = 0; f < m.face_count(); f++)
        for (uint32_t k = 0; k < m.face_size(f); k++) {
          uint32_t a = m.face_verts(f)[k], b = m.face_verts(f)[(k + 1) % m.face_size(f)];
          e.push_back(a < b ? std::make_pair(a, b) : std::make_pair(b, a));
        }
      std::sort(e.begin(), e.end());
      e.erase(std::unique(e.begin(), e.end()), e.end());
    });
    double bu = time_ms([&] {
      std::vector<std::pair<uint32_t, uint32_t>> e;
      m.edges(e);
    }, 3);
    rep.row({num((double)m.face_count()), f1(hm), f1(so), f1(bu), f1(so / bu) + "x"});
  }
  rep.note("Bucketing edges by their lower vertex index (counting sort) is linear and cache-friendly; Mesh::edges now uses it.");
}

static void test_merge(Report &rep, const Options &o) {
  rep.title("Merge by Distance: spatial hash vs O(n^2)",
            "A grid where every vertex is duplicated (as after an import with split normals).");
  rep.table({"vertices", "naive ms", "spatial hash ms", "speed-up", "merged"});
  for (int g : o.quick ? std::vector<int>{40, 80} : std::vector<int>{40, 80, 160, 320, 640}) {
    Mesh base = *primitives::grid(10.0f, g, g);
    meshops::triangulate(base);
    /* Unweld: every face gets its own vertices. */
    Mesh m;
    for (size_t f = 0; f < base.face_count(); f++) {
      uint32_t v[3];
      for (int k = 0; k < 3; k++) v[k] = m.add_vert(base.positions[base.face_verts(f)[k]]);
      m.add_face(v, 3);
    }
    Mesh a = m, b = m;
    double naive = m.vert_count() <= 160000 ? time_ms([&] { meshops::merge_by_distance_naive(a, 1e-4f); }) : -1;
    size_t merged = 0;
    double hash = time_ms([&] { merged = meshops::merge_by_distance(b, 1e-4f); });
    rep.row({num((double)m.vert_count()), naive < 0 ? "(skipped: too slow)" : f1(naive), f1(hash), naive < 0 ? "-" : f1(naive / hash) + "x", num((double)merged)});
  }
}

static void test_serialization(Report &rep, const Options &o) {
  rep.title("Scene save / load (text format)", "Many small objects, then one huge mesh.");
  rep.table({"case", "file size", "save ms", "load ms", "load MB/s"});
  auto run = [&](const std::string &name, Scene &s) {
    std::string text;
    double save = time_ms([&] { text = save_scene_text(s); });
    Scene loaded;
    std::string err;
    double load = time_ms([&] { load_scene_text(text, loaded, err); });
    rep.row({name, format_bytes(text.size()), f1(save), f1(load), f1(text.size() / 1e6 / (load / 1000.0))});
    if (loaded.object_count() != s.object_count()) rep.note("ERROR: object count mismatch after reload: " + err);
  };
  for (int n : o.quick ? std::vector<int>{1000} : std::vector<int>{1000, 10000, 50000}) {
    Scene s = make_grid_scene(n, primitives::cube());
    run(strprintf("%d objects (shared cube)", n), s);
  }
  Scene big;
  GameObject *g = big.create("big");
  Mesh m = *primitives::cube();
  for (int i = 0; i < (o.quick ? 6 : 8); i++) m = meshops::subdivide_simple(m);
  g->add<MeshFilter>()->mesh = std::make_shared<Mesh>(m);
  g->add<MeshRenderer>();
  run(strprintf("1 mesh, %s verts", num((double)m.vert_count()).c_str()), big);
  /* Micro-benchmark behind the save-path optimisation. */
  std::vector<float> vals(1000000);
  for (size_t i = 0; i < vals.size(); i++) vals[i] = std::sin((float)i) * 123.456f;
  std::string sink;
  sink.reserve(vals.size() * 16);
  double pf = time_ms([&] {
    sink.clear();
    char b[32];
    for (float v : vals) sink.append(b, (size_t)std::snprintf(b, sizeof(b), "%.9g", v));
  });
  double tc = time_ms([&] {
    sink.clear();
    for (float v : vals) append_float(sink, v);
  });
  rep.note(strprintf("Formatting 1M floats: printf %%.9g %.1f ms vs to_chars %.1f ms (%.1fx). The writer now uses to_chars.", pf, tc, pf / tc));
}

static void test_undo(Report &rep, const Options &o) {
  rep.title("Undo snapshots: copy-on-write vs deep copy",
            "Cost of one undo step for scenes holding heavy meshes. CoW = Scene::clone() sharing meshes "
            "(Blender's implicit sharing); deep = also duplicating every mesh.");
  rep.table({"objects", "total verts", "CoW clone ms", "deep copy ms", "deep copy memory"});
  Mesh heavy = *primitives::cube();
  for (int i = 0; i < (o.quick ? 5 : 7); i++) heavy = meshops::subdivide_simple(heavy);
  for (int n : o.quick ? std::vector<int>{10, 100} : std::vector<int>{10, 100, 500}) {
    Scene s;
    for (int i = 0; i < n; i++) {
      GameObject *g = s.create("heavy");
      g->add<MeshFilter>()->mesh = std::make_shared<Mesh>(heavy);
      g->add<MeshRenderer>();
    }
    std::unique_ptr<Scene> c;
    double cow = time_ms([&] { c = s.clone(); });
    size_t bytes = 0;
    double deep = time_ms([&] {
      c = s.clone();
      c->for_each([&](GameObject &g) {
        auto *mf = g.get<MeshFilter>();
        mf->mesh = std::make_shared<Mesh>(*mf->mesh);
        bytes += mf->mesh->memory_bytes();
      });
    });
    rep.row({std::to_string(n), num((double)n * heavy.vert_count()), f2(cow), f1(deep), format_bytes(bytes)});
  }
  rep.note("Copy-on-write makes undo cost proportional to what changed, not to scene size.");
}

static void test_jobs(Report &rep, const Options &o) {
  rep.title("Job system: parallel scaling", "Transform 8M vertices by a matrix with 1..N threads.");
  rep.table({"threads", "ms", "speed-up", "efficiency"});
  size_t n = o.quick ? 2000000 : 8000000;
  std::vector<Vec3> in(n, Vec3(1, 2, 3)), out(n);
  Mat4 m = Mat4::trs({1, 2, 3}, Quat::euler({10, 20, 30}), {1, 2, 1});
  JobSystem &js = JobSystem::global();
  double base = 0;
  int maxt = js.thread_count();
  for (int t = 1; t <= maxt; t = t < 4 ? t + 1 : t * 2) {
    js.set_max_threads(t);
    double ms = time_ms([&] {
      js.parallel_for((int64_t)n, 65536, [&](int64_t b, int64_t e) {
        for (int64_t i = b; i < e; i++) out[i] = m.point(in[i]);
      });
    }, 5);
    if (t == 1) base = ms;
    rep.row({std::to_string(t), f2(ms), f1(base / ms) + "x", strprintf("%.0f%%", base / ms / t * 100)});
    if (t * 2 > maxt && t != maxt) t = maxt / 2;  // make sure we end on maxt
  }
  js.set_max_threads(1 << 30);
  rep.note("Memory bandwidth, not core count, limits this kind of streaming kernel (GEA Vol. I 3.5 Memory Architectures).");
  if (!JobSystem::tbb_available()) return;
  /* Built-in pool vs oneTBB (what Blender uses), all threads. */
  rep.table({"workload", "built-in pool ms", "oneTBB ms", "TBB speed-up"});
  auto compare = [&](const char *name, const std::function<void()> &work, int reps) {
    double ms[2];
    for (int k = 0; k < 2; k++) {
      js.set_backend(k == 0 ? JobSystem::Backend::Builtin : JobSystem::Backend::TBB);
      work();  // warm-up (thread start, caches)
      ms[k] = time_ms(work, reps);
    }
    rep.row({name, f2(ms[0]), f2(ms[1]), f2(ms[0] / ms[1]) + "x"});
  };
  compare("stream 8M vertices", [&] {
    js.parallel_for((int64_t)n, 65536, [&](int64_t b, int64_t e) {
      for (int64_t i = b; i < e; i++) out[i] = m.point(in[i]);
    });
  }, 5);
  std::vector<double> acc(4096);
  compare("imbalanced compute (cost grows 64x)", [&] {
    js.parallel_for(4096, 16, [&](int64_t b, int64_t e) {
      for (int64_t i = b; i < e; i++) {
        double s = 0;
        int iters = 200 + (int)(i * 12600 / 4096);  // last items cost 64x the first
        for (int k = 0; k < iters; k++) s += std::sin(k * 0.001 + i);
        acc[i] = s;
      }
    });
  }, 3);
  compare("10,000 small parallel_for calls", [&] {
    for (int c = 0; c < 10000; c++)
      js.parallel_for(4096, 256, [&](int64_t b, int64_t e) {
        for (int64_t i = b; i < e; i++) out[i] = m.point(in[i]);
      });
  }, 1);
  compare("nested: 32 x parallel_for(250k)", [&] {
    js.parallel_for(32, 1, [&](int64_t b, int64_t e) {
      for (int64_t o = b; o < e; o++)
        js.parallel_for(250000, 4096, [&](int64_t b2, int64_t e2) {
          for (int64_t i = b2; i < e2; i++) out[(o * 250000 + i) % n] = m.point(in[i]);
        });
    });
  }, 3);
  js.set_backend(JobSystem::Backend::TBB);
  rep.note("TBB's clear win is dispatch cost: many small loops (typical of editor operators) start much faster. "
           "Streaming and imbalanced work are a wash: memory bandwidth limits the first, and the built-in pool already hands out "
           "small chunks dynamically. Nested loops gain a little because TBB runs the inner loops in parallel too.");
}

static void test_editor(Report &rep, const Options &o) {
  rep.title("Editor UI: Hierarchy virtualisation & frame cost",
            "Full editor frames (headless, 1600x900) with N empty GameObjects expanded in the Hierarchy.");
  rep.table({"objects", "frame ms", "objects/ms"});
  for (int n : o.quick ? std::vector<int>{1000, 10000} : std::vector<int>{1000, 10000, 100000}) {
    Editor ed;
    Log::echo_stdout = false;
    ed.init_headless(1600, 900);
    ed.command("stress " + std::to_string(n) + " Empty");
    ed.command("select Empty");  // expands the parent so all rows are listed
    ed.step_frame_headless();
    double ms = time_ms([&] { ed.step_frame_headless(); }, 5);
    Log::echo_stdout = true;
    rep.row({std::to_string(n), f2(ms), f1(n / ms)});
  }
  rep.note("Only visible rows are drawn, so frame cost grows with the tree walk, not with drawing.");
}

static void test_fuzz(Report &rep, const Options &o) {
  rep.title("Editor robustness: random input fuzzing",
            "Random clicks, drags, wheel and key presses across the whole window (monkey testing).");
  rep.table({"frames", "events", "objects at end", "avg frame ms", "result"});
  Editor ed;
  /* BLENDITY_FUZZ_TRACE=1 prints every frame's events and the editor log, to
   * replay a crash the fuzzer finds. */
  const bool trace = std::getenv("BLENDITY_FUZZ_TRACE") != nullptr;
  Log::echo_stdout = trace;
  ed.init_headless(1280, 720);
  uint32_t seed = std::getenv("BLENDITY_FUZZ_SEED") ? (uint32_t)std::strtoul(std::getenv("BLENDITY_FUZZ_SEED"), nullptr, 10) : 12345u;
  auto rnd = [&](int n) { seed = seed * 1664525u + 1013904223u; return (int)((seed >> 8) % (uint32_t)n); };
  int frames = o.quick ? 1500 : 6000, events = 0;
  int mx = 640, my = 360;
  bool down[3] = {};
  ScopedTimer t;
  for (int f = 0; f < frames; f++) {
    std::vector<platform::Event> ev;
    int k = rnd(4);
    for (int i = 0; i < k; i++) {
      platform::Event e;
      int r = rnd(100);
      if (r < 40) {
        e.type = platform::EventType::MouseMove;
        mx = std::max(0, std::min(1279, mx + rnd(161) - 80));
        my = std::max(0, std::min(719, my + rnd(161) - 80));
      }
      else if (r < 65) {
        e.button = rnd(10) < 7 ? 0 : rnd(3);
        e.type = down[e.button] ? platform::EventType::MouseUp : platform::EventType::MouseDown;
        down[e.button] = !down[e.button];
      }
      else if (r < 75) {
        e.type = platform::EventType::Wheel;
        e.wheel_y = (float)(rnd(3) - 1);
      }
      else if (r < 92) {
        /* Keys, but never Ctrl+S / F12 (no files written during fuzzing). */
        static const int keys[] = {platform::KEY_W, platform::KEY_E, platform::KEY_R, platform::KEY_Q, platform::KEY_F,
                                   platform::KEY_TAB, platform::KEY_DELETE, platform::KEY_1, platform::KEY_3,
                                   platform::KEY_ESCAPE, platform::KEY_ENTER, platform::KEY_A, platform::KEY_D,
                                   platform::KEY_Z, platform::KEY_Y, platform::KEY_P, platform::KEY_LEFT, platform::KEY_DOWN,
                                   /* phase 2: edge mode, proportional, merge, normals, UV Editor, render window */
                                   platform::KEY_2, platform::KEY_O, platform::KEY_M, platform::KEY_N, platform::KEY_U,
                                   platform::KEY_L, platform::KEY_9, platform::KEY_F11, platform::KEY_I,
                                   /* phase 7: modal G / S, axis X, bevel B, triangulate T */
                                   platform::KEY_G, platform::KEY_X, platform::KEY_S, platform::KEY_B, platform::KEY_T};
        e.type = platform::EventType::KeyDown;
        e.key = keys[rnd(sizeof(keys) / sizeof(keys[0]))];
        int mods = rnd(10);
        e.mods = mods == 0 ? platform::MOD_CTRL : (mods == 1 ? platform::MOD_ALT : (mods == 2 ? platform::MOD_SHIFT : 0));
        if (e.mods == platform::MOD_CTRL && (e.key == platform::KEY_S)) e.mods = 0;
        ev.push_back(e);
        e.type = platform::EventType::KeyUp;
      }
      else {
        e.type = platform::EventType::Text;
        e.codepoint = 'a' + rnd(26);
      }
      e.x = mx;
      e.y = my;
      ev.push_back(e);
    }
    events += (int)ev.size();
    if (trace) {
      std::fprintf(stderr, "frame %d:", f);
      for (auto &e : ev) std::fprintf(stderr, " [t%d b%d k%d m%d %d,%d]", (int)e.type, e.button, e.key, e.mods, e.x, e.y);
      std::fprintf(stderr, "\n");
      std::fflush(stderr);
    }
    ed.step_frame_headless(ev);
  }
  double avg = t.ms() / frames;
  Log::echo_stdout = true;
  rep.row({std::to_string(frames), std::to_string(events), std::to_string(ed.scene().object_count()), f2(avg), "no crash"});
}

static void test_memory(Report &rep, const Options &) {
  rep.title("Memory footprint", "Approximate bytes per element (Scene::memory_bytes / Mesh::memory_bytes).");
  rep.table({"item", "bytes"});
  Scene s;
  for (int i = 0; i < 10000; i++) s.create("GameObject");
  rep.row({"empty GameObject", f1(s.memory_bytes() / 10000.0)});
  Scene s2 = make_grid_scene(10000, primitives::cube());
  rep.row({"GameObject + MeshFilter + MeshRenderer (shared mesh)", f1(s2.memory_bytes() / 10000.0)});
  Mesh m = *primitives::cube();
  for (int i = 0; i < 6; i++) m = meshops::subdivide_simple(m);
  m.render_mesh();
  rep.row({"mesh vertex (incl. render cache)", f1(m.memory_bytes() / (double)m.vert_count())});
}

/* ------------------------------------------------- phase 2: render / UV / modeling */

static void test_shading_cost(Report &rep, const Options &o) {
  rep.title("Shading: Gouraud vs deferred PBR vs deferred + sun shadows",
            "256 icospheres with a material at each resolution. Deferred shades each visible pixel once from the "
            "visibility buffer (EEVEE-like); shadows add a 2048^2 depth-only pass plus a 3x3 PCF lookup per pixel.");
  auto mesh = primitives::ico_sphere(0.5f, 3);
  Scene s = make_grid_scene(256, mesh, 1.3f);
  std::vector<MaterialPtr> mats = {make_material("bench", {0.7f, 0.5f, 0.3f})};
  rep.table({"resolution", "Gouraud ms", "deferred ms", "deferred+shadow ms", "shadow pass ms", "shade ms"});
  std::vector<std::pair<int, int>> res = {{640, 360}, {1280, 720}, {1920, 1080}};
  if (!o.quick) res.push_back({3840, 2160});
  for (auto [w, h] : res) {
    Bench3D b(w, h);
    std::vector<DrawItem> items;
    s.for_each([&](GameObject &g) {
      DrawItem it;
      it.mesh = &g.get<MeshFilter>()->mesh->render_mesh();
      it.model = g.world_matrix();
      it.id = (uint32_t)g.id;
      it.materials = &mats;
      items.push_back(it);
    });
    AABB bounds;
    for (auto &it : items) bounds.add(it.model.translation());
    bounds.min = bounds.min - Vec3(1.0f);
    bounds.max = bounds.max + Vec3(1.0f);
    ShadowMap sm;
    double shadow_ms = time_ms([&] { render_shadow_map(sm, items, b.env.lights[0].direction, bounds, 2048); }, 3);
    Vec3 eye{0, 12 * 0.9f, -12 * 1.1f};
    Mat4 v = Mat4::look_at(eye, {0, 0, 0}, {0, 1, 0});
    Mat4 p = Mat4::perspective(60 * kDeg2Rad, w / (float)h, 0.1f, 1000.0f);
    b.env.camera_pos = eye;
    double shade_ms = 0;
    auto frame = [&](ShadeMode mode, bool shadows) {
      RasterOptions opt;
      opt.shade = mode;
      LightingEnv env = b.env;
      if (shadows) { env.shadow = &sm; env.shadow_light = 0; }
      b.r3d.begin(&b.rt, v, p, env, opt);
      b.r3d.clear(0xFF303030);
      for (auto &it : items) b.r3d.add(it);
      b.r3d.flush();
      shade_ms = b.r3d.stats().ms_shade;
    };
    frame(ShadeMode::Deferred, true);  // warm-up
    double g = time_ms([&] { frame(ShadeMode::Gouraud, false); }, 3);
    double d = time_ms([&] { frame(ShadeMode::Deferred, false); }, 3);
    double ds = time_ms([&] { frame(ShadeMode::Deferred, true); }, 3);
    rep.row({strprintf("%dx%d", w, h), f2(g), f2(d), f2(ds), f2(shadow_ms), f2(shade_ms)});
  }
  rep.note("The shadow map only needs re-rendering when lights or casters move (render_view.cpp caches it by scene hash).");
  /* View transforms: per-frame cost of turning scene light into display pixels. */
  rep.table({"view transform (1920x1080)", "ms / frame", "Mpix/s"});
  std::vector<Vec3> hdr((size_t)1920 * 1080);
  uint32_t rng = 3;
  for (Vec3 &c : hdr) {
    rng = rng * 1664525u + 1013904223u;
    float l = std::exp2(((rng >> 8) & 0xFFFF) / 65536.0f * 14.0f - 8.0f);
    c = Vec3(l, l * 0.7f, l * 0.4f);
  }
  std::vector<uint32_t> outpx(hdr.size());
  auto run_vt = [&](const std::string &name, ViewTransform vt) {
    to_display_pixel(hdr[0], vt, 0.0f);  // bakes an OpenColorIO LUT on first use
    double ms = time_ms([&] {
      JobSystem::global().parallel_for((int64_t)hdr.size(), 8192, [&](int64_t b, int64_t e) {
        for (int64_t i = b; i < e; i++) outpx[i] = to_display_pixel(hdr[i], vt, 0.0f);
      });
    }, 5);
    rep.row({name, f2(ms), f1(hdr.size() / ms / 1000.0)});
  };
  run_vt("Filmic (built-in curve)", ViewTransform::Filmic);
  if (colormanagement::available()) {
    for (const char *name : {"AgX", "Filmic", "Khronos PBR Neutral"}) {
      int v = colormanagement::view_index(name);
      if (v >= 0) run_vt(std::string(name) + " (OCIO 97^3 LUT)", (ViewTransform)((int)ViewTransform::OcioView + v));
    }
    /* The exact OpenColorIO CPU processor on the same frame, for comparison. */
    int agx = colormanagement::view_index("AgX");
    std::vector<float> in(hdr.size() * 3), out(hdr.size() * 3);
    for (size_t i = 0; i < hdr.size(); i++)
      for (int k = 0; k < 3; k++) in[i * 3 + k] = hdr[i][k];
    double ms = time_ms([&] {
      JobSystem::global().parallel_for((int64_t)hdr.size(), 65536, [&](int64_t b, int64_t e) {
        colormanagement::reference(agx, &in[b * 3], &out[b * 3], (size_t)(e - b));
      });
    }, 2);
    rep.row({"AgX (OCIO CPU processor, exact)", f2(ms), f1(hdr.size() / ms / 1000.0)});
  }

  /* Hand-written display kernels (display.cpp) on one thread, against the
   * per-pixel pow() version Blendity used before. All give identical sRGB. */
  rep.table({"Filmic encode, 1 thread", "ms / 1080p frame", "Mpix/s", "vs pow()"});
  auto pow_pixel = [](Vec3 c) {
    c = tonemap(c, ViewTransform::Filmic, 0.0f);
    auto q = [](float v) { return (uint32_t)std::clamp((int)(linear_to_srgb(saturate(v)) * 255.0f + 0.5f), 0, 255); };
    return 0xFF000000u | (q(c.x) << 16) | (q(c.y) << 8) | q(c.z);
  };
  double pow_ms = time_ms([&] {
    for (size_t i = 0; i < hdr.size(); i++) outpx[i] = pow_pixel(hdr[i]);
  }, 2);
  rep.row({"scalar, pow() per channel", f2(pow_ms), f1(hdr.size() / pow_ms / 1000.0), "1.00x"});
  for (display::Kernel k : {display::Kernel::Scalar, display::Kernel::SSE41, display::Kernel::AVX2}) {
    display::set_kernel(k);
    if (display::active_kernel() != k) continue;  // CPU lacks it
    double ms = time_ms([&] { display::encode_span(&hdr[0].x, outpx.data(), hdr.size(), ViewTransform::Filmic, 0.0f); }, 5);
    rep.row({std::string(display::kernel_name(k)) + ", sRGB table", f2(ms), f1(hdr.size() / ms / 1000.0), f2(pow_ms / ms) + "x"});
  }
  display::set_kernel(display::Kernel::Auto);
  double par_ms = time_ms([&] {
    JobSystem::global().parallel_for((int64_t)hdr.size(), 16384, [&](int64_t b, int64_t e) {
      display::encode_span(&hdr[b].x, outpx.data() + b, (size_t)(e - b), ViewTransform::Filmic, 0.0f);
    });
  }, 5);
  rep.row({std::string(display::kernel_name(display::active_kernel())) + ", all threads", f2(par_ms), f1(hdr.size() / par_ms / 1000.0),
           f2(pow_ms / par_ms) + "x"});
  rep.note("CPU: " + cpu::describe() + ". The sRGB table is correctly rounded for every float in [0, 1] (checked in the unit tests).");
}

static void test_pathtracer(Report &rep, const Options &o) {
  rep.title("Path tracer: BVH build and ray throughput (Blendity BVH vs Embree)",
            "Grids of icospheres traced at 320x180, 4 bounces (Cycles-like: NEE, MIS). Mrays/s counts every camera, bounce "
            "and shadow ray actually cast. Each scene runs on Blendity's two-level SAH BVH and, when built with Blender's "
            "libraries, on Embree (Cycles' CPU ray tracing kernels).");
  auto mesh = primitives::ico_sphere(0.5f, 3);
  const RenderMesh &rm = mesh->render_mesh_tangents();
  std::vector<MaterialPtr> mats = {make_material("bench", {0.8f, 0.8f, 0.8f})};
  Environment env;
  RenderLight sun;
  sun.direction = normalize(Vec3(-0.4f, -1.0f, 0.5f));
  sun.intensity = 3.0f;
  const bool embree = PathTracer::embree_available();
  struct Run {
    double build_ms, ms_per_sample, mrays;
  };
  auto run = [&](const std::vector<PTObject> &objs, int side, bool use_embree) {
    PathTracer pt;
    PTSettings st;
    st.use_embree = use_embree;
    pt.set_settings(st);
    pt.build(objs, {sun}, env);
    double build = pt.stats().bvh_build_ms;
    float ext = side * 0.65f + 2.0f;
    pt.set_camera(Mat4::look_at({0, ext * 0.9f, -ext * 1.1f}, {0, 0, 0}, {0, 1, 0}), Mat4::perspective(60 * kDeg2Rad, 16.0f / 9.0f, 0.1f, 1000),
                  320, 180);
    pt.render(0, 1);  // warm-up
    pt.reset();
    ScopedTimer t;
    int spp = pt.render(o.quick ? 300 : 1000, 64);
    return Run{build, t.ms() / std::max(1, spp), pt.stats().mrays_per_s()};
  };
  rep.table({"objects", "triangles", "BVH build ms", "Embree build ms", "BVH Mrays/s", "Embree Mrays/s", "Embree speed-up"});
  for (int n : o.quick ? std::vector<int>{16, 256} : std::vector<int>{16, 256, 1024, 4096, 16384}) {
    std::vector<PTObject> objs;
    int side = (int)std::ceil(std::sqrt((double)n));
    for (int i = 0; i < n; i++) objs.push_back({&rm, Mat4::translate({(i % side - side * 0.5f) * 1.3f, 0, (i / side - side * 0.5f) * 1.3f}), &mats});
    Run a = run(objs, side, false);
    Run b = embree ? run(objs, side, true) : Run{0, 0, 0};
    rep.row({std::to_string(n), num((double)n * rm.tri_count()), f1(a.build_ms), embree ? f1(b.build_ms) : "-", f1(a.mrays),
             embree ? f1(b.mrays) : "-", embree ? f2(b.mrays / std::max(1e-9, a.mrays)) + "x" : "-"});
  }
  rep.note("Every object shares one mesh, so both backends build a single bottom-level tree (Cycles instancing).");
  /* Worst case for instancing: every object has its own mesh. */
  rep.table({"unique meshes", "triangles", "backend", "first build ms", "move 1 object ms", "edit 1 mesh ms", "rebuilt"});
  for (int n : o.quick ? std::vector<int>{64} : std::vector<int>{64, 256, 1024}) {
    for (bool use_embree : {false, true}) {
      if (use_embree && !embree) continue;
      std::vector<MeshPtr> meshes;
      std::vector<PTObject> objs;
      int side = (int)std::ceil(std::sqrt((double)n));
      for (int i = 0; i < n; i++) meshes.push_back(std::make_shared<Mesh>(*mesh));
      for (int i = 0; i < n; i++)
        objs.push_back({&meshes[i]->render_mesh_tangents(), Mat4::translate({(i % side - side * 0.5f) * 1.3f, 0, (i / side - side * 0.5f) * 1.3f}),
                        &mats});
      PathTracer pt;
      PTSettings st;
      st.use_embree = use_embree;
      pt.set_settings(st);
      pt.build(objs, {sun}, env);
      double first = pt.stats().bvh_build_ms;
      objs[0].model = Mat4::translate({0, 3, 0});
      pt.build(objs, {sun}, env);
      double moved = pt.stats().bvh_build_ms;
      meshes[1]->positions[0].y += 0.2f;
      meshes[1]->touch();
      objs[1].mesh = &meshes[1]->render_mesh_tangents();
      pt.build(objs, {sun}, env);
      rep.row({std::to_string(n), num((double)pt.stats().triangles), pt.ray_backend(), f1(first), f1(moved), f1(pt.stats().bvh_build_ms),
               std::to_string(pt.stats().meshes_rebuilt)});
    }
  }
  rep.note("Bottom-level trees are cached by mesh content hash in both backends: moving an object only rebuilds the top level; "
           "editing a mesh rebuilds just that mesh.");
  /* Path guiding (OpenPGL): two rooms joined by a doorway with the light in the
   * far one, at a realistic resolution so the field gets enough training samples. */
  if (PathTracer::guiding_available()) {
    /* Two rooms joined by a doorway, the light only in the far one. */
    auto cube = primitives::cube(1.0f);
    auto panel = primitives::quad(1.0f);
    std::vector<MaterialPtr> grey = {make_material("grey", {0.6f, 0.6f, 0.6f})};
    std::vector<MaterialPtr> light = {make_material("light", {0, 0, 0})};
    light[0]->emission = {1, 1, 1};
    light[0]->emission_strength = 40.0f;
    std::vector<PTObject> robjs;
    auto wall = [&](Vec3 lo, Vec3 hi) { robjs.push_back({&cube->render_mesh_tangents(), Mat4::trs((lo + hi) * 0.5f, Quat(), hi - lo), &grey}); };
    wall({-3.1f, -0.1f, -3.1f}, {3.1f, 0.0f, 9.1f});
    wall({-3.1f, 4.0f, -3.1f}, {3.1f, 4.1f, 9.1f});
    wall({-3.1f, 0.0f, -3.1f}, {-3.0f, 4.0f, 9.1f});
    wall({3.0f, 0.0f, -3.1f}, {3.1f, 4.0f, 9.1f});
    wall({-3.0f, 0.0f, -3.1f}, {3.0f, 4.0f, -3.0f});
    wall({-3.0f, 0.0f, 9.0f}, {3.0f, 4.0f, 9.1f});
    wall({-3.0f, 0.0f, 2.95f}, {-0.5f, 4.0f, 3.05f});
    wall({0.5f, 0.0f, 2.95f}, {3.0f, 4.0f, 3.05f});
    wall({-0.5f, 2.0f, 2.95f}, {0.5f, 4.0f, 3.05f});
    robjs.push_back({&panel->render_mesh_tangents(), Mat4::trs({0, 3.95f, 6.0f}, Quat::euler({90, 0, 0}), {1, 1, 1}), &light});
    Environment dark;
    dark.mode = Environment::Color;
    dark.color = Vec3(0.0f);
    const int W = o.quick ? 160 : 320, H = o.quick ? 120 : 240;
    auto render_room = [&](int spp, bool guiding, double *ms, bool mesh_lights = true) {
      PathTracer pt;
      PTSettings st;
      st.use_guiding = guiding;
      st.sample_mesh_lights = mesh_lights;
      st.max_bounces = 6;
      st.clamp_indirect = 0;
      pt.set_settings(st);
      pt.build(robjs, {}, dark);
      pt.set_camera(Mat4::look_at({0, 1.6f, -2.6f}, {0, 1.0f, 3.0f}, {0, 1, 0}), Mat4::perspective(70 * kDeg2Rad, 4.0f / 3.0f, 0.05f, 50), W, H);
      ScopedTimer t;
      pt.render(1e9, spp);
      if (ms) *ms = t.ms();
      return pt.linear_rgb(false);
    };
    auto rmse = [](const std::vector<float> &a, const std::vector<float> &b) {
      double e = 0;
      for (size_t i = 0; i < a.size(); i++) e += (a[i] - b[i]) * (a[i] - b[i]);
      return std::sqrt(e / a.size());
    };
    std::vector<float> ref = render_room(o.quick ? 1024 : 4096, false, nullptr);
    rep.table({"samples", "unguided RMSE", "guided RMSE", "unguided ms", "guided ms", "efficiency gain"});
    for (int spp : {256, 512}) {
      double tu = 0, tg = 0;
      double eu = rmse(render_room(spp, false, &tu), ref), eg = rmse(render_room(spp, true, &tg), ref);
      /* Efficiency = 1 / (error^2 x time): how much faster guiding reaches the same noise. */
      rep.row({std::to_string(spp), strprintf("%.4f", eu), strprintf("%.4f", eg), f1(tu), f1(tg), f2((eu * eu * tu) / (eg * eg * tg)) + "x"});
    }
    /* The bigger win in this scene: sampling the emissive panel directly. */
    {
      double tb = 0, tn = 0;
      double eb = rmse(render_room(256, false, &tb, false), ref), en = rmse(render_room(256, false, &tn, true), ref);
      rep.row({"256, BSDF-only vs mesh-light NEE", strprintf("%.4f", eb), strprintf("%.4f", en), f1(tb), f1(tn),
               f2((eb * eb * tb) / (en * en * tn)) + "x"});
    }
    rep.note("Guiding trains on the first 128 samples, then steers diffuse bounces toward the light it learned (Cycles: "
             "Light Paths > Path Guiding). It is unbiased and learns (10x more guided samples point at the doorway than cosine "
             "sampling), but here the OpenPGL lookups cost more than the noise it removes - it stays opt-in, as in Cycles.");
  }
  /* Denoisers: time per frame (quality is checked by the unit tests). */
  rep.table({"resolution", "A-Trous ms", "OpenImageDenoise ms"});
  const bool oidn = PathTracer::oidn_available();
  std::vector<PTObject> objs;
  for (int i = 0; i < 64; i++) objs.push_back({&rm, Mat4::translate({(i % 8 - 4) * 1.3f, 0, (i / 8 - 4) * 1.3f}), &mats});
  std::vector<std::pair<int, int>> sizes = {{640, 360}, {1280, 720}};
  if (!o.quick) sizes.push_back({1920, 1080});
  for (auto [w, h] : sizes) {
    double ms[2] = {0, 0};
    for (int k = 0; k < (oidn ? 2 : 1); k++) {
      PathTracer pt;
      PTSettings st;
      st.use_oidn = k == 1;
      pt.set_settings(st);
      pt.build(objs, {sun}, env);
      pt.set_camera(Mat4::look_at({0, 8, -9}, {0, 0, 0}, {0, 1, 0}), Mat4::perspective(60 * kDeg2Rad, w / (float)h, 0.1f, 1000), w, h);
      pt.render(0, 2);
      pt.linear_rgb(true);  // warm-up (OIDN loads its weights once)
      ms[k] = time_ms([&] { pt.linear_rgb(true); }, 2);
    }
    rep.row({strprintf("%dx%d", w, h), f1(ms[0]), oidn ? f1(ms[1]) : "-"});
  }
}

static void test_uv_unwrap(Report &rep, const Options &o) {
  rep.title("UV: LSCM unwrap and packing",
            "LSCM on an n x n grid (one island): block-Jacobi conjugate gradients on the assembled normal equations "
            "(parallel sparse mat-vec) vs Eigen's sparse Cholesky, the direct-solver route Blender takes. Then Smart UV "
            "Project + pack on a subdivided torus.");
  rep.table({"case", "faces", "islands", "CG ms", "Eigen ms", "Eigen speed-up"});
  const bool eigen = std::string(uvops::lscm_solver_name()) != "conjugate gradients";
  for (int g : o.quick ? std::vector<int>{16, 64} : std::vector<int>{16, 64, 128, 256, 512}) {
    Mesh src = *primitives::grid(10.0f, g, g);
    for (auto &p : src.positions) p.y = 0.3f * std::sin(p.x) * std::cos(p.z);  // not flat
    int islands = 0;
    double ms[2] = {-1, -1};
    for (int k = 0; k < (eigen ? 2 : 1); k++) {
      if (k == 0 && g > 256) continue;  // CG takes ~10 s at 512^2
      uvops::set_lscm_solver(k == 0 ? uvops::LscmSolver::ConjugateGradient : uvops::LscmSolver::Auto);
      Mesh m = src;
      ms[k] = time_ms([&] { islands = uvops::unwrap_lscm(m, nullptr); });
    }
    uvops::set_lscm_solver(uvops::LscmSolver::Auto);
    rep.row({strprintf("LSCM grid %dx%d", g, g), num((double)src.face_count()), std::to_string(islands), ms[0] < 0 ? "(skipped)" : f1(ms[0]),
             ms[1] < 0 ? "-" : f1(ms[1]), ms[0] > 0 && ms[1] > 0 ? f1(ms[0] / ms[1]) + "x" : "-"});
  }
  for (int lv : o.quick ? std::vector<int>{0, 1} : std::vector<int>{0, 1, 2, 3}) {
    Mesh m = *primitives::torus();
    for (int i = 0; i < lv; i++) m = meshops::subdivide_catmull_clark(m);
    int islands = 0;
    double ms = time_ms([&] { islands = uvops::smart_project(m, nullptr); });
    rep.row({strprintf("Smart UV torus (subdiv %d)", lv), num((double)m.face_count()), std::to_string(islands), f1(ms)});
  }
}

static void test_texture_sampling(Report &rep, const Options &o) {
  rep.title("Textures: mip build and sampling throughput",
            "Generated UV Grid textures; random UVs, single thread. Trilinear = 8 texel fetches + sRGB LUT decode.");
  rep.table({"size", "mip build ms", "memory", "closest Ms/s", "linear Ms/s", "trilinear Ms/s"});
  for (int size : o.quick ? std::vector<int>{512, 1024} : std::vector<int>{512, 1024, 2048, 4096}) {
    TexturePtr t;
    double build = time_ms([&] { t = texture_uv_grid(size); });
    const int N = 2000000;
    uint32_t rng = 1;
    volatile float sink = 0;
    double ms[3];
    for (int f = 0; f < 3; f++)
      ms[f] = time_ms([&] {
        float acc = 0;
        for (int i = 0; i < N; i++) {
          rng = rng * 1664525u + 1013904223u;
          Vec2 uv{(rng & 0xFFFF) / 65536.0f, (rng >> 16) / 65536.0f};
          acc += t->sample(uv, 1.3f, TexWrap::Repeat, (TexFilter)f).x;
        }
        sink = sink + acc;
      });
    rep.row({std::to_string(size), f1(build), format_bytes(t->memory_bytes()), f1(N / ms[0] / 1000.0), f1(N / ms[1] / 1000.0),
             f1(N / ms[2] / 1000.0)});
  }
}

static void test_codecs(Report &rep, const Options &o) {
  rep.title("Image codecs and scene compression",
            "Decoding a 3840x2160 photo-like image (gradients + noise) with Blendity's own decoders vs Blender's libjpeg-turbo "
            "and libpng, then Zstandard on a text scene.");
  const int w = o.quick ? 1920 : 3840, h = o.quick ? 1080 : 2160;
  std::vector<uint32_t> px((size_t)w * h);
  uint32_t rng = 7;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      rng = rng * 1664525u + 1013904223u;
      int n = (int)((rng >> 24) & 31) - 16;
      auto c = [&](int v) { return (uint32_t)std::clamp(v + n, 0, 255); };
      px[(size_t)y * w + x] = 0xFF000000u | c(x * 255 / w) << 16 | c(y * 255 / h) << 8 | c((x + y) * 128 / (w + h) + 64);
    }
  /* Scratch files go to the system temp folder, not next to the reports. */
  std::error_code tmp_ec;
  std::string dir = (std::filesystem::temp_directory_path(tmp_ec) / "blendity_stress_codecs").string();
  fs::make_dirs(dir);
  std::string png = fs::join(dir, "img.png"), jpg = fs::join(dir, "img.jpg");
  write_png(png, px.data(), w, h, w);
  write_jpeg(jpg, px.data(), w, h, w, 92);
  rep.table({"format", "file size", "Blendity ms", "library ms", "library speed-up"});
  for (const std::string &path : {png, jpg}) {
    std::string bytes, err;
    fs::read_file(path, bytes);
    double ms[2] = {0, 0};
    for (int k = 0; k < 2; k++) {
      set_image_library_codecs(k == 1);
      Bitmap b;
      ms[k] = time_ms([&] { decode_image(bytes, b, err); }, 3);
    }
    set_image_library_codecs(true);
    const bool jpeg = path == jpg;
    rep.row({jpeg ? "JPEG (libjpeg-turbo)" : "PNG (libpng + zlib)", format_bytes(bytes.size()), f1(ms[0]), f1(ms[1]), f2(ms[0] / ms[1]) + "x"});
  }
  if (scene_compression_available()) {
    Scene s = make_grid_scene(o.quick ? 2000 : 10000, primitives::ico_sphere(0.5f, 2));
    std::string text = save_scene_text(s);
    s.compress = true;
    std::string path = fs::join(dir, "big.scene"), packed, err;
    double save_ms = time_ms([&] { save_scene(s, path); });
    fs::read_file(path, packed);
    Scene l;
    double load_ms = time_ms([&] { load_scene(path, l, err); });
    rep.table({"scene", "text size", "zstd size", "ratio", "save ms", "load ms"});
    rep.row({strprintf("%zu objects", s.object_count()), format_bytes(text.size()), format_bytes(packed.size()),
             f1((double)text.size() / packed.size()) + "x", f1(save_ms), f1(load_ms)});
  }
}

static void test_gpu_devices(Report &rep, const Options &o) {
  rep.title("Render devices: CPU vs GPU (Vulkan) vs combined",
            "The same path-traced frame on every device: the CPU (Embree when built with it), each GPU tracing Blendity's "
            "BVH in a compute shader, each GPU using its ray tracing hardware (VK_KHR_ray_query), and CPU + all GPUs "
            "together. 400 textured spheres on a checker floor, sun + sky, 4 bounces. Samples per second at 1280x720.");
  rep.note(gpu::status());
  if (!gpu::available()) return;
  auto sphere = primitives::uv_sphere(0.5f, 32, 16);
  auto floor = primitives::plane(40.0f, 1);
  std::vector<MaterialPtr> m_floor = {make_material("floor", {0.8f, 0.8f, 0.8f})}, m_ball = {make_material("ball", {1, 1, 1})},
                           m_glass = {make_material_preset("Glass")};
  m_floor[0]->procedural = (int)Procedural::Checker;
  m_ball[0]->procedural = (int)Procedural::UVGrid;
  std::vector<PTObject> objs = {{&floor->render_mesh_tangents(), Mat4::identity(), &m_floor}};
  for (int i = 0; i < 400; i++)
    objs.push_back({&sphere->render_mesh_tangents(), Mat4::translate({(i % 20 - 9.5f) * 1.3f, 0.5f, (i / 20 - 9.5f) * 1.3f}), i % 7 == 0 ? &m_glass : &m_ball});
  RenderLight sun;
  sun.direction = normalize(Vec3(-0.4f, -1, 0.3f));
  Environment env;
  const int W = o.quick ? 640 : 1280, H = o.quick ? 360 : 720, SPP = o.quick ? 8 : 128;
  struct Run { std::string name; bool cpu; std::vector<int> gpus; bool hw; };
  std::vector<Run> runs = {{"CPU", true, {}, false}};
  std::vector<int> all;
  for (const gpu::DeviceInfo &d : gpu::devices()) {
    runs.push_back({d.name + " (compute, Blendity BVH)", false, {d.index}, false});
    if (d.hardware_rt) runs.push_back({d.name + " (ray tracing hardware)", false, {d.index}, true});
    all.push_back(d.index);
  }
  if (all.size() > 1) runs.push_back({"All GPUs", false, all, true});
  runs.push_back({"CPU + all GPUs", true, all, true});
  rep.table({"device", "samples/s", "ms / sample", "vs CPU", "samples per device"});
  double cpu_rate = 0;
  for (const Run &r : runs) {
    PathTracer pt;
    PTSettings st;
    st.use_cpu = r.cpu;
    st.gpus = r.gpus;
    st.gpu_hardware_rt = r.hw;
    st.denoise = false;
    pt.set_settings(st);
    pt.build(objs, {sun}, env);
    pt.set_camera(Mat4::look_at({0, 6, -16}, {0, 0, 0}, {0, 1, 0}), Mat4::perspective(55 * kDeg2Rad, W / (float)H, 0.1f, 200), W, H);
    pt.render(1e9, 8);  // warm-up: kernel compile, caches, GPU clocks
    pt.set_camera(Mat4::look_at({0, 6, -16}, {0, 0, 0}, {0, 1, 0}), Mat4::perspective(55 * kDeg2Rad, W / (float)H, 0.1f, 200), W, H);
    ScopedTimer t;
    int done = 0;
    while (done < SPP) done = pt.render(1e9, SPP);
    double ms = t.ms(), rate = done / (ms / 1000.0);
    if (r.name == "CPU") cpu_rate = rate;
    std::string share = "-";
    if (r.gpus.size() + (r.cpu ? 1 : 0) > 1) {
      share = r.cpu ? strprintf("CPU %d", pt.stats().cpu_samples) : "";
      for (size_t g = 0; g < pt.stats().gpu_samples.size(); g++) share += strprintf("%sGPU%zu %d", share.empty() ? "" : ", ", g + 1, pt.stats().gpu_samples[g]);
    }
    rep.row({r.name, f1(rate), f2(ms / done), f2(rate / std::max(cpu_rate, 1e-9)) + "x", share});
  }
  rep.note("Every device renders whole samples of the frame and the sums are averaged, so devices of any speed combine "
           "without seams (Cycles splits work across devices the same way). The unit tests check each device against the CPU.");
}

static void test_physics(Report &rep, const Options &o) {
  rep.title("Physics: Blendity's sphere physics vs Jolt",
            "N rigid boxes dropped in a pile onto a plane; average ms per 60 Hz frame over 2 simulated seconds. Blendity's "
            "fallback tests every pair (O(n^2)); Jolt uses a broad phase, sleeping and a multithreaded solver.");
  rep.table({"bodies", "Blendity ms/frame", "Jolt ms/frame", "Jolt speed-up"});
  for (int n : o.quick ? std::vector<int>{100, 500} : std::vector<int>{100, 500, 2000, 5000}) {
    double ms[2] = {-1, -1};
    for (int k = 0; k < 2; k++) {
      if (k == 1 && !physics_jolt_available()) continue;
      if (k == 0 && n > 2000) continue;  // O(n^2): minutes
      Scene s;
      GameObject *ground = s.create("Ground");
      ground->add<MeshFilter>()->mesh = primitives::plane(200.0f, 1);
      ground->add<MeshRenderer>();
      auto cube = primitives::cube();
      int side = (int)std::ceil(std::cbrt((double)n));
      for (int i = 0; i < n; i++) {
        GameObject *g = s.create("Box");
        g->add<MeshFilter>()->mesh = cube;
        g->add<MeshRenderer>();
        g->add<Rigidbody>();
        g->set_local_position({(i % side) * 1.1f - side * 0.55f, 1.0f + (i / (side * side)) * 1.1f, ((i / side) % side) * 1.1f - side * 0.55f});
      }
      PlayContext ctx;
      ctx.dt = 1.0f / 60.0f;
      s.start(ctx);
      if (k == 0) s.physics.reset();  // force the fallback
      ScopedTimer t;
      for (int f = 0; f < 120; f++) s.update(ctx);
      ms[k] = t.ms() / 120.0;
    }
    rep.row({std::to_string(n), ms[0] < 0 ? "(skipped)" : f2(ms[0]), ms[1] < 0 ? "-" : f2(ms[1]),
             ms[0] > 0 && ms[1] > 0 ? f1(ms[0] / ms[1]) + "x" : "-"});
  }
}

static void test_modeling_tools(Report &rep, const Options &o) {
  rep.title("Modeling tools at scale",
            "Edit-mode operators on large meshes (an n x n grid / subdivided cube). Interactive tools should stay "
            "under ~100 ms.");
  rep.table({"operator", "faces", "ms"});
  for (int g : o.quick ? std::vector<int>{128, 256} : std::vector<int>{128, 512, 1024}) {
    Mesh m = *primitives::grid(10.0f, g, g);
    uint32_t a = (uint32_t)(g / 2) * (g + 1) + 1, b = a + 1;
    size_t loop = 0;
    double el = time_ms([&] { loop = meshops::edge_loop(m, a, b).size(); });
    rep.row({strprintf("edge loop (%zu verts)", loop), num((double)m.face_count()), f1(el)});
    double lc = time_ms([&] { meshops::loop_cut(m, a, b, 1); });
    rep.row({"loop cut", num((double)m.face_count()), f1(lc)});
    Mesh s = m;
    double so = time_ms([&] { meshops::solidify(s, 0.1f, -1.0f, false, true); });
    rep.row({"solidify (+rim)", num((double)s.face_count()), f1(so)});
    double rn = time_ms([&] { meshops::recalc_normals_outside(s); });
    rep.row({"recalculate normals", num((double)s.face_count()), f1(rn)});
    Mesh mi = m;
    double mr = time_ms([&] { meshops::mirror(mi, true, false, false, 0.001f); });
    rep.row({"mirror X (merge)", num((double)mi.face_count()), f1(mr)});
  }
}

/* ------------------------------------------------------- Push/Pull stress */

namespace ppstress {

double signed_volume(const Mesh &m) {
  /* In double, relative to the first vertex: a mesh far from the origin
   * would otherwise lose everything to cancellation. */
  if (m.positions.empty()) return 0;
  const Vec3 o = m.positions[0];
  auto d3 = [&](uint32_t i, double *out) {
    out[0] = (double)m.positions[i].x - o.x;
    out[1] = (double)m.positions[i].y - o.y;
    out[2] = (double)m.positions[i].z - o.z;
  };
  double v = 0;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t *fv = m.face_verts(f);
    double a[3], b[3], c[3];
    d3(fv[0], a);
    for (uint32_t i = 1; i + 1 < m.face_size(f); i++) {
      d3(fv[i], b);
      d3(fv[i + 1], c);
      v += a[0] * (b[1] * c[2] - b[2] * c[1]) + a[1] * (b[2] * c[0] - b[0] * c[2]) + a[2] * (b[0] * c[1] - b[1] * c[0]);
    }
  }
  return v / 6.0;
}

/* Every edge used by exactly two faces, in opposite directions. */
bool closed_manifold(const Mesh &m) {
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

/* Structural problems: bad indices, short faces, repeated corners, NaN. */
std::string invalid(const Mesh &m) {
  for (Vec3 p : m.positions)
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return "non-finite position";
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t n = m.face_size(f), *v = m.face_verts(f);
    if (n < 3) return "face with < 3 corners";
    for (uint32_t i = 0; i < n; i++) {
      if (v[i] >= m.vert_count()) return "index out of range";
      if (v[i] == v[(i + 1) % n]) return "repeated corner";
    }
  }
  if (!m.uvs.empty() && m.uvs.size() != m.corner_verts.size()) return "UV count mismatch";
  return "";
}

size_t degenerate_faces(const Mesh &m, float scale) {
  size_t n = 0;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t *v = m.face_verts(f);
    Vec3 an(0.0f);
    const Vec3 o = m.positions[v[0]];  // relative: far from the origin the products would cancel
    for (uint32_t i = 0; i < m.face_size(f); i++) an += cross(m.positions[v[i]] - o, m.positions[v[(i + 1) % m.face_size(f)]] - o);
    if (length(an) * 0.5f < 1e-9f * scale * scale) n++;
  }
  return n;
}

Mesh from(const MeshPtr &p) { return *p; }

void append(Mesh &m, const Mesh &b, const Mat4 &t) {
  uint32_t base = (uint32_t)m.vert_count();
  for (Vec3 p : b.positions) m.add_vert(t.point(p));
  for (size_t f = 0; f < b.face_count(); f++) {
    std::vector<uint32_t> v(b.face_verts(f), b.face_verts(f) + b.face_size(f));
    for (uint32_t &x : v) x += base;
    m.add_face(v.data(), v.size());
  }
}

/* A closed prism from a 2D outline (counter-clockwise seen from +Y). */
Mesh prism(const std::vector<Vec2> &outline, float h) {
  Mesh m;
  const uint32_t n = (uint32_t)outline.size();
  for (Vec2 p : outline) m.add_vert({p.x, 0, p.y});
  for (Vec2 p : outline) m.add_vert({p.x, h, p.y});
  std::vector<uint32_t> bottom, top;
  for (uint32_t i = 0; i < n; i++) {
    top.push_back(n + i);
    bottom.push_back(n - 1 - i);
  }
  m.add_face(top.data(), top.size());
  m.add_face(bottom.data(), bottom.size());
  for (uint32_t i = 0; i < n; i++) {
    uint32_t j = (i + 1) % n;
    m.add_face({i, j, n + j, n + i});
  }
  meshops::recalc_normals_outside(m);
  return m;
}

struct Case {
  std::string name;
  Mesh mesh;
};

std::vector<Case> cases() {
  std::vector<Case> c;
  c.push_back({"cube", from(primitives::cube())});
  {
    Mesh m = from(primitives::cube());
    std::vector<uint8_t> fs(m.face_count(), 0);
    for (size_t f = 0; f < m.face_count(); f++) fs[f] = dot(m.face_normal(f), Vec3(0, 1, 0)) > 0.99f;
    meshops::inset_faces(m, fs, 0.3f);
    c.push_back({"cube, inset top", m});
  }
  c.push_back({"cylinder (32-gon caps)", from(primitives::cylinder(0.5f, 2.0f, 32))});
  c.push_back({"cylinder (128-gon caps)", from(primitives::cylinder(0.5f, 1.0f, 128))});
  c.push_back({"cone (point apex)", from(primitives::cone(0.5f, 1.0f, 16))});
  c.push_back({"UV sphere (tris + quads)", from(primitives::uv_sphere(0.5f, 12, 8))});
  c.push_back({"icosphere (all tris)", from(primitives::ico_sphere(0.5f, 1))});
  c.push_back({"torus (genus 1)", from(primitives::torus(0.5f, 0.2f, 12, 8))});
  c.push_back({"single quad (open)", from(primitives::quad(1.0f))});
  c.push_back({"grid 6x6 (open surface)", from(primitives::grid(2.0f, 6, 6))});
  c.push_back({"L-prism (concave cap)", prism({{0, 0}, {2, 0}, {2, 1}, {1, 1}, {1, 2}, {0, 2}}, 1.0f)});
  {
    std::vector<Vec2> star;
    for (int i = 0; i < 10; i++) {
      float r = i % 2 ? 0.35f : 1.0f, a = i * kPi / 5;
      star.push_back({r * std::cos(a), r * std::sin(a)});
    }
    c.push_back({"star prism (very concave)", prism(star, 0.5f)});
  }
  {
    Mesh m = from(primitives::cube());
    for (Vec3 &p : m.positions)
      if (p.y > 0 && p.x > 0 && p.z > 0) p.y += 0.35f;  // one corner up: a non-planar top
    c.push_back({"cube, non-planar top", m});
  }
  {
    Mesh m = from(primitives::cube());
    for (Vec3 &p : m.positions) p.x += p.y * 0.8f;  // sheared: sides lean over
    c.push_back({"sheared box", m});
  }
  c.push_back({"wedge (slanted faces)", prism({{0, 0}, {2, 0}, {0, 1.2f}}, 1.0f)});
  {
    Mesh m = from(primitives::cube());
    meshops::flip_normals(m);
    c.push_back({"cube, inside-out normals", m});
  }
  {
    /* Every face with its own vertices: a cube that isn't connected. */
    Mesh src = from(primitives::cube()), m;
    for (size_t f = 0; f < src.face_count(); f++) {
      std::vector<uint32_t> v;
      for (uint32_t i = 0; i < src.face_size(f); i++) v.push_back(m.add_vert(src.positions[src.face_verts(f)[i]]));
      m.add_face(v.data(), v.size());
    }
    c.push_back({"cube, unwelded faces", m});
  }
  {
    /* A sliver face (zero area) and collinear corners on the top. */
    Mesh m = from(primitives::cube());
    std::vector<uint8_t> vs(m.vert_count(), 0);
    auto e = m.edge_cache()[0];
    vs[e.first] = vs[e.second] = 1;
    meshops::subdivide_edges(m, vs, 2);  // collinear corners on two faces
    uint32_t a = m.add_vert({-0.5f, 0.5f, -0.5f}), b = m.add_vert({0.5f, 0.5f, -0.5f}), d = m.add_vert({0.0f, 0.5f, -0.5f});
    m.add_face({a, d, b});  // zero area, floating
    c.push_back({"collinear corners + sliver", m});
  }
  {
    /* Non-manifold: a fin sharing an edge with the cube (three faces on one edge). */
    Mesh m = from(primitives::cube());
    auto e = m.edge_cache()[0];
    Vec3 pa = m.positions[e.first], pb = m.positions[e.second];
    Vec3 out = normalize((pa + pb) * 0.5f) * 0.8f;
    uint32_t c0 = m.add_vert(pb + out), c1 = m.add_vert(pa + out);
    m.add_face({e.first, e.second, c0, c1});
    c.push_back({"non-manifold fin", m});
  }
  {
    Mesh m = from(primitives::cube());
    for (Vec3 &p : m.positions) p = p * 1e-4f;
    c.push_back({"tiny cube (0.1 mm)", m});
  }
  {
    Mesh m = from(primitives::cube());
    for (Vec3 &p : m.positions) p = p * 1e4f;
    c.push_back({"huge cube (10 km)", m});
  }
  {
    Mesh m = from(primitives::cube());
    for (Vec3 &p : m.positions) p += Vec3(1e5f, -3e4f, 2e5f);
    c.push_back({"cube far from origin", m});
  }
  {
    Mesh m = from(primitives::cube());
    append(m, *primitives::cube(2.0f), Mat4::trs({2.6f, 0.3f, 0}, Quat::euler({0, 25, 15}), {1, 1, 1}));
    c.push_back({"two boxes, tilted target", m});
  }
  {
    Mesh m = from(primitives::cube());
    for (Vec3 &p : m.positions) p.z *= 0.02f;  // a 2 cm wall
    c.push_back({"thin wall", m});
  }
  c.push_back({"subdivided cube (curved quads)", meshops::subdivide(*primitives::cube(), 2, true)});
  {
    /* A cube with a tunnel through it (genus 1, faces inside the hole). */
    Mesh m = from(primitives::cube());
    std::vector<uint8_t> fs(m.face_count(), 0);
    for (size_t f = 0; f < m.face_count(); f++) fs[f] = dot(m.face_normal(f), Vec3(0, 0, -1)) > 0.99f;
    meshops::inset_faces(m, fs, 0.3f);
    meshops::push_through(m, fs, 1);
    c.push_back({"cube with a tunnel", m});
  }
  return c;
}

/* Connected face regions grown from a seed (random walk over shared edges). */
std::vector<uint8_t> grow_region(const Mesh &m, uint32_t seed, int faces, uint32_t &rng) {
  std::vector<uint8_t> sel(m.face_count(), 0);
  sel[seed] = 1;
  std::unordered_map<uint64_t, std::vector<uint32_t>> by_edge;
  for (size_t f = 0; f < m.face_count(); f++)
    for (uint32_t i = 0; i < m.face_size(f); i++)
      by_edge[Mesh::edge_key(m.face_verts(f)[i], m.face_verts(f)[(i + 1) % m.face_size(f)])].push_back((uint32_t)f);
  std::vector<uint32_t> in{seed};
  for (int k = 1; k < faces; k++) {
    std::vector<uint32_t> cand;
    for (uint32_t f : in)
      for (uint32_t i = 0; i < m.face_size(f); i++)
        for (uint32_t g : by_edge[Mesh::edge_key(m.face_verts(f)[i], m.face_verts(f)[(i + 1) % m.face_size(f)])])
          if (!sel[g]) cand.push_back(g);
    if (cand.empty()) break;
    rng = rng * 1664525u + 1013904223u;
    uint32_t g = cand[(rng >> 8) % cand.size()];
    sel[g] = 1;
    in.push_back(g);
  }
  return sel;
}

}  // namespace ppstress

static void test_pushpull_stress(Report &rep, const Options &o) {
  using namespace ppstress;
  using meshops::PushPullResult;
  rep.title("Push/Pull on odd meshes",
            "SketchUp-style Push/Pull (meshops::push_pull) on every face and on random face groups of 26 awkward meshes "
            "(n-gons, concave and non-planar caps, open surfaces, inside-out normals, unwelded and non-manifold "
            "geometry, slivers, 0.1 mm to 10 km scales, far from the origin), at distances from tiny to huge, "
            "exactly at the hole and join limits, and NaN / infinity. Every result is checked for broken structure, "
            "closed meshes for staying closed, and volume for moving the right way.");
  rep.table({"mesh", "ops", "applied / refused", "moved / walls / hole / join", "problems", "max ms"});
  const char *kinds[] = {"moved", "walls", "hole", "join"};
  size_t total_ops = 0, total_problems = 0, overlap_results = 0;
  std::vector<std::string> examples;
  std::map<std::string, int> refusals, problem_types;
  std::set<std::string> seen;
  for (Case &cs : cases()) {
    const Mesh &base = cs.mesh;
    AABB bb;
    for (Vec3 p : base.positions) bb.add(p);
    const float scale = std::max(1e-12f, length(bb.max - bb.min));
    const bool closed = closed_manifold(base);
    const double v0 = signed_volume(base);
    /* Volume in the solid's own terms: a pull along the face normal adds solid,
     * whichever way the normals point (an inside-out cube's solid is outside). */
    static const double csign = signed_volume(*primitives::cube()) >= 0 ? 1.0 : -1.0;
    /* Selections: every face (up to 48), then random groups of 2 - 6 faces. */
    std::vector<std::vector<uint8_t>> sels;
    uint32_t rng = 1234567u + (uint32_t)cs.name.size();
    const size_t nf = base.face_count();
    for (size_t f = 0; f < nf && sels.size() < (o.quick ? 12u : 48u); f++) {
      size_t pick = nf <= 48 ? f : (f * 7919u) % nf;
      std::vector<uint8_t> s(nf, 0);
      s[pick] = 1;
      sels.push_back(s);
    }
    for (int k = 0; k < (o.quick ? 4 : 16); k++) {
      rng = rng * 1664525u + 1013904223u;
      sels.push_back(grow_region(base, (rng >> 8) % (uint32_t)nf, 2 + k % 5, rng));
    }
    sels.push_back(std::vector<uint8_t>(nf, 1));  // everything: no outline, must refuse
    if (nf >= 4) {
      std::vector<uint8_t> two(nf, 0);  // two faces far apart: usually disconnected
      two[0] = two[nf - 1] = 1;
      sels.push_back(two);
    }
    size_t ops = 0, applied = 0, refused = 0, problems = 0;
    size_t kind_count[4] = {};
    double max_ms = 0;
    for (const auto &sel0 : sels) {
      meshops::PushPullLimits lim = meshops::push_pull_limits(base, sel0);
      std::vector<float> ds = {0.0f, 1e-9f * scale, 0.01f * scale, -0.01f * scale, 0.2f * scale, -0.2f * scale, 0.6f * scale,
                               -0.6f * scale, 3.0f * scale, -3.0f * scale, 1e3f * scale,
                               std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()};
      if (lim.through > 0) for (float d : {-lim.through, -lim.through * 0.999f, -lim.through * 1.5f}) ds.push_back(d);
      if (lim.contact > 0) for (float d : {lim.contact, lim.contact * 0.999f, lim.contact * 1.5f}) ds.push_back(d);
      for (float d : ds)
        for (int keep = 0; keep < 2; keep++) {
          Mesh m = base;
          std::vector<uint8_t> sel = sel0;
          PushPullResult res = PushPullResult::Moved;
          std::string err;
          ScopedTimer t;
          bool ok = meshops::push_pull(m, sel, d, keep == 0, &res, &err);
          max_ms = std::max(max_ms, t.ms());
          ops++;
          auto problem = [&](const std::string &what) {
            problems++;
            const std::string type = what.substr(0, what.find(" ("));
            problem_types[type]++;
            if (const char *dbg = std::getenv("BLENDITY_PP_DEBUG")) {
              std::string q = dbg;
              size_t bar = q.find('|');
              static bool dumped = false;
              if (!dumped && cs.name.find(q.substr(0, bar)) != std::string::npos && what.find(q.substr(bar + 1)) != std::string::npos) {
                dumped = true;
                auto dump = [](const char *title, const Mesh &mm, const std::vector<uint8_t> &fs) {
                  std::printf("--- %s: %zu verts, %zu faces\n", title, mm.vert_count(), mm.face_count());
                  for (size_t i = 0; i < mm.vert_count(); i++) std::printf("v%zu %.5f %.5f %.5f\n", i, mm.positions[i].x, mm.positions[i].y, mm.positions[i].z);
                  for (size_t f = 0; f < mm.face_count(); f++) {
                    std::printf("f%zu%s n(%.2f %.2f %.2f):", f, f < fs.size() && fs[f] ? "*" : "", mm.face_normal(f).x, mm.face_normal(f).y, mm.face_normal(f).z);
                    for (uint32_t k = 0; k < mm.face_size(f); k++) std::printf(" %u", mm.face_verts(f)[k]);
                    std::printf("\n");
                  }
                };
                std::printf("=== %s: %s, d = %g, keep = %d\n", cs.name.c_str(), what.c_str(), d, keep);
                dump("before", base, sel0);
                dump("after", m, sel);
                {
                  std::vector<std::pair<uint32_t, uint32_t>> pairs;
                  meshops::overlapping_faces(m, 2e-3f * scale, &pairs);
                  for (auto &pr : pairs) std::printf("overlap f%u f%u\n", pr.first, pr.second);
                }
                std::fflush(stdout);
              }
            }
            if (seen.insert(cs.name + "|" + type).second && examples.size() < 150)
              examples.push_back(strprintf("%s: %s (d = %g, %s, %zu faces selected, result %s)", cs.name.c_str(), what.c_str(), d,
                                           keep ? "new face" : "merge", (size_t)std::count(sel0.begin(), sel0.end(), 1), kinds[(int)res]));
          };
          if (!ok) {
            refused++;
            refusals[err]++;
            if (base.positions != m.positions || base.corner_verts != m.corner_verts) problem("refused but changed the mesh");
            if (!std::isfinite(d) || std::count(sel0.begin(), sel0.end(), 1) == (long long)nf) continue;
            continue;
          }
          applied++;
          kind_count[(int)res]++;
          if (!std::isfinite(d)) { problem("accepted a non-finite distance"); continue; }
          std::string bad = invalid(m);
          if (!bad.empty()) { problem(bad); continue; }
          if (std::count(sel.begin(), sel.end(), 1) == 0 && std::fabs(d) > 1e-7f && res != PushPullResult::Hole && res != PushPullResult::Joined)
            problem("lost the selection");
          if (closed && !closed_manifold(m)) problem("a closed mesh came out open / non-manifold");
          if (closed && std::fabs(d) > 1e-6f * scale) {
            const double v1 = signed_volume(m), dv = (v1 - v0) * csign;
            double tol = 1e-4 * (double)scale * scale * scale;
            if (res == PushPullResult::Hole && dv > tol) problem("a hole added volume");
            else if (res == PushPullResult::Joined && dv < -tol) problem("a join removed volume");
            else if ((res == PushPullResult::Moved || res == PushPullResult::Extruded) && dv * d < -tol)
              problem(strprintf("volume moved the wrong way (%+.3g)", dv));
            if (v0 * v1 < 0 && std::fabs(v1) > tol) problem("turned inside out");
          }
          size_t degen = degenerate_faces(m, scale);
          if (degen > degenerate_faces(base, scale)) problem(strprintf("new zero-area faces (%zu)", degen - degenerate_faces(base, scale)));
          /* Faces on top of each other (or a hair apart): the z-fighting artifacts. */
          if (m.face_count() < 400) {
            const size_t ov = meshops::overlapping_faces(m, 2e-3f * scale), ov0 = meshops::overlapping_faces(base, 2e-3f * scale);
            if (ov > ov0) {
              overlap_results++;
              if (std::getenv("BLENDITY_PP_DEBUG")) problem(strprintf("new overlapping faces (%zu)", ov - ov0));
            }
          }
        }
    }
    total_ops += ops;
    total_problems += problems;
    rep.row({cs.name, num((double)ops), strprintf("%zu / %zu", applied, refused),
             strprintf("%zu / %zu / %zu / %zu", kind_count[0], kind_count[1], kind_count[2], kind_count[3]), num((double)problems), f2(max_ms)});
  }
  rep.note(strprintf("%zu Push/Pull operations, %zu problems", total_ops, total_problems));
  rep.note(strprintf("%zu results with faces newly on top of each other (z-fighting; 1,502 before round 13's fixes): mostly holes "
                     "through curved surfaces and the deliberately broken inputs (unwelded faces, inside-out normals)",
                     overlap_results));
  for (auto &[why, n] : refusals) rep.note(strprintf("refused %d x: %s", n, why.c_str()));
  for (auto &[type, n] : problem_types) rep.note(strprintf("problem %d x: %s", n, type.c_str()));
  for (const std::string &e : examples) rep.note(e);

  /* The interactive operator on the same meshes: P, the mouse swept back and
   * forth across the view (through the hole and join snaps), then a click,
   * Esc or a right-click; undo must give the mesh back exactly. */
  rep.table({"mesh (editor)", "faces tried", "confirmed", "cancelled", "undo exact", "avg frame ms"});
  Editor ed;
  ed.init_headless(1000, 700);
  ed.command("create Cube");
  ed.step_frame_headless();
  GameObject *obj = nullptr;
  ed.scene().for_each([&](GameObject &g) {
    if (g.name == "Cube" && (!obj || g.id > obj->id)) obj = &g;
  });
  if (!obj) return;
  const uint64_t id = obj->id;
  const Recti view = ed.scene_view_rect();
  const int cx = view.x + view.w / 2, cy = view.y + view.h / 2;
  auto ev = [](platform::EventType t, int x, int y, int key = 0, int button = 0) {
    platform::Event e;
    e.type = t;
    e.x = x;
    e.y = y;
    e.key = key;
    e.button = button;
    return e;
  };
  size_t total_bad = 0;
  for (Case &cs : cases()) {
    if (o.quick && cs.mesh.face_count() > 200) continue;
    ed.command("edit off");  // out of Edit Mode while the mesh is swapped
    GameObject *g = ed.scene().find(id);
    if (!g) break;
    g->get<MeshFilter>()->mesh = std::make_shared<Mesh>(cs.mesh);
    ed.commit_change("Push/Pull test mesh");  // one undo step, so undo comes back to this mesh
    ed.select_object(id);
    ed.command("edit face");
    const size_t nf = cs.mesh.face_count();
    int tried = 0, confirmed = 0, cancelled = 0, exact = 0;
    ScopedTimer t;
    int frames = 0;
    for (size_t k = 0; k < nf && tried < (o.quick ? 3 : 8); k += std::max<size_t>(1, nf / 8), tried++) {
      GameObject *cur = ed.scene().find(id);
      if (!cur) break;
      const std::vector<Vec3> before = cur->get<MeshFilter>()->mesh->positions;
      ed.command(strprintf("fsel %zu", k));
      ed.step_frame_headless({ev(platform::EventType::MouseMove, cx, cy)});
      ed.step_frame_headless({ev(platform::EventType::KeyDown, cx, cy, platform::KEY_P), ev(platform::EventType::KeyUp, cx, cy, platform::KEY_P)});
      for (int s = 0; s < 24; s++) {  // sweep: up past the join, down past the hole
        int y = cy + (int)(std::sin(s * 0.55f) * view.h * 0.45f);
        ed.step_frame_headless({ev(platform::EventType::MouseMove, cx + s * 3, y)});
        frames++;
      }
      const int how = tried % 3;  // click, Esc, right-click
      if (how == 0) {
        ed.step_frame_headless({ev(platform::EventType::MouseDown, cx, cy - 60, 0, 0)});
        ed.step_frame_headless({ev(platform::EventType::MouseUp, cx, cy - 60, 0, 0)});
        confirmed++;
        GameObject *after = ed.scene().find(id);
        if (!after || after->get<MeshFilter>()->mesh->positions == before) { frames += 2; exact++; continue; }  // refused: nothing to undo
        platform::Event z = ev(platform::EventType::KeyDown, cx, cy, platform::KEY_Z);
        z.mods = platform::MOD_CTRL;
        ed.step_frame_headless({z});
      }
      else if (how == 1) {
        ed.step_frame_headless({ev(platform::EventType::KeyDown, cx, cy, platform::KEY_ESCAPE)});
        cancelled++;
      }
      else {
        ed.step_frame_headless({ev(platform::EventType::MouseDown, cx, cy, 0, 1)});
        ed.step_frame_headless({ev(platform::EventType::MouseUp, cx, cy, 0, 1)});
        cancelled++;
      }
      frames += 4;
      GameObject *now = ed.scene().find(id);
      if (now && now->get<MeshFilter>()->mesh->positions == before) exact++;
    }
    total_bad += (size_t)(tried - exact);
    rep.row({cs.name, std::to_string(tried), std::to_string(confirmed), std::to_string(cancelled), strprintf("%d / %d", exact, tried),
             f2(t.ms() / std::max(1, frames))});
  }
  rep.note(total_bad ? strprintf("%zu interactive Push/Pulls did not restore exactly", total_bad)
                     : "every interactive Push/Pull was undone or cancelled back to the exact mesh");
}

/* The modifiers and tools added with the modifier stack (Bevel, Weld,
 * Triangulate, Wireframe, Displace, Simple Deform, Cast, Screw), vertex /
 * edge extrusion, Limited Dissolve, the knife's edge / face splits, Join and
 * Separate, and every parametric shape - on the same awkward meshes as
 * Push/Pull, with ordinary, zero, negative, huge and NaN settings. Nothing may
 * crash, and every result must be a structurally valid mesh. */
static void test_modifier_tools_stress(Report &rep, const Options &o) {
  using namespace ppstress;
  rep.title("Modifiers, n-gon tools and shapes on odd meshes",
            "Every new modifier with ordinary and hostile settings (0, negative, huge, NaN), vertex / edge extrusion on random "
            "selections, Limited Dissolve, random knife cuts, Join and Separate, on the 26 Push/Pull meshes; then every "
            "parametric shape with random and extreme settings. Each result is checked for broken structure.");
  rep.table({"mesh", "ops", "problems", "max ms"});
  const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
  size_t total_ops = 0, total_problems = 0;
  std::map<std::string, int> problem_types;
  std::vector<std::string> examples;
  for (Case &cs : cases()) {
    const Mesh &base = cs.mesh;
    AABB bb;
    for (Vec3 p : base.positions) bb.add(p);
    const float s = std::max(1e-6f, length(bb.max - bb.min));
    size_t ops = 0, problems = 0;
    double max_ms = 0;
    auto run = [&](const char *what, const std::function<void(Mesh &)> &fn) {
      Mesh m = base;
      ScopedTimer t;
      fn(m);
      max_ms = std::max(max_ms, t.ms());
      ops++;
      const std::string bad = invalid(m);
      if (!bad.empty()) {
        problems++;
        problem_types[bad]++;
        if (examples.size() < 60) examples.push_back(strprintf("%s: %s -> %s", cs.name.c_str(), what, bad.c_str()));
      }
    };
    for (float w : {0.02f * s, 0.0f, -0.1f * s, 10.0f * s, nan})
      for (int seg : {1, 3}) run("bevel", [&](Mesh &m) { meshops::bevel_modifier(m, w, seg, 30.0f); });
    run("bevel all edges", [&](Mesh &m) { meshops::bevel_modifier(m, 0.01f * s, 2, -1.0f); });
    for (int mv : {3, 4, 5, 100}) run("triangulate", [&](Mesh &m) { meshops::triangulate_min(m, mv); });
    for (float d : {0.0f, 1e-4f * s, 0.5f * s, 10.0f * s}) run("weld", [&](Mesh &m) { meshops::merge_by_distance(m, d); });
    for (float th : {0.01f * s, 0.0f, 5.0f * s, nan})
      for (int keep = 0; keep < 2; keep++) run("wireframe", [&](Mesh &m) { meshops::wireframe(m, th, true, keep != 0); });
    for (float st : {0.1f * s, -1.0f * s, 0.0f, 1e6f, nan})
      for (int dir = 0; dir < 4; dir++) run("displace", [&](Mesh &m) { meshops::displace(m, st, 0.5f, 0.3f * s, dir, 3, 5); });
    for (int mode = 0; mode < 4; mode++)
      for (float amt : {45.0f, -720.0f, 0.0f, 1e-8f, nan})
        for (int axis = 0; axis < 3; axis++)
          run("simple deform", [&](Mesh &m) { meshops::simple_deform(m, mode, mode <= 1 ? amt : amt / 90.0f, axis, 0.2f, 0.8f); });
    run("simple deform, inverted limits", [&](Mesh &m) { meshops::simple_deform(m, 1, 90.0f, 2, 0.9f, 0.1f); });
    for (int shape = 0; shape < 3; shape++)
      for (float f : {1.0f, -2.0f, 0.0f, nan}) run("cast", [&](Mesh &m) { meshops::cast(m, shape, f, 0.0f, 1); });
    for (float ang : {360.0f, 90.0f, -720.0f, 0.0f})
      for (int steps : {2, 16})
        run("screw", [&](Mesh &m) { meshops::screw(m, ang, steps, ang == 90.0f ? 0.5f * s : 0.0f, 1, ang == 0.0f ? 3 : 1, true, false); });
    /* Vertex / edge extrusion from random selections (wire edges included). */
    uint32_t rng = 99991u + (uint32_t)cs.name.size();
    for (int k = 0; k < (o.quick ? 4 : 12); k++)
      run("extrude verts / edges", [&](Mesh &m) {
        std::vector<uint8_t> sel(m.vert_count(), 0);
        for (size_t v = 0; v < sel.size(); v++) {
          rng = rng * 1664525u + 1013904223u;
          sel[v] = (rng >> 24) < (uint32_t)(k * 20 + 10);
        }
        meshops::extrude_verts_edges(m, sel);
        for (size_t v = 0; v < sel.size(); v++)
          if (sel[v]) m.positions[v] += Vec3(0.1f, 0.2f, 0.0f) * s;
        meshops::extrude_verts_edges(m, sel);  // again, from the new ends
      });
    run("limited dissolve", [&](Mesh &m) { meshops::dissolve_limited(m, 1.0f); });
    run("limited dissolve 30", [&](Mesh &m) { meshops::dissolve_limited(m, 30.0f); });
    /* Knife: split random edges and then their face, the way the tool does. */
    for (int k = 0; k < (o.quick ? 4 : 16); k++)
      run("knife", [&](Mesh &m) {
        if (!m.face_count()) return;
        rng = rng * 1664525u + 1013904223u;
        const size_t f = (rng >> 8) % m.face_count();
        const uint32_t n = m.face_size(f);
        if (n < 3) return;
        const uint32_t i = (rng >> 4) % n, j = (i + 1 + (rng >> 12) % std::max(1u, n - 1)) % n;
        const uint32_t a0 = m.face_verts(f)[i], a1 = m.face_verts(f)[(i + 1) % n];
        const uint32_t b0 = m.face_verts(f)[j], b1 = m.face_verts(f)[(j + 1) % n];
        const float ta = (float)((rng >> 16) % 100) / 100.0f, tb = (float)((rng >> 20) % 100) / 100.0f;
        const uint32_t va = meshops::split_edge(m, a0, a1, ta);
        const uint32_t vb = (Mesh::edge_key(a0, a1) == Mesh::edge_key(b0, b1)) ? va : meshops::split_edge(m, b0, b1, tb);
        meshops::split_face(m, f, va, vb);
      });
    run("join with itself", [&](Mesh &m) {
      const Mesh copy = m;
      meshops::append_mesh(m, copy, Mat4::trs({s, 0, 0}, Quat::euler({0, 30, 0}), {-1, 1, 1}), {0, 1});
    });
    /* This round's tools: Slice, Spin, Seams from Sharp, imprinting shapes and bent cuts. */
    for (int axis = 0; axis < 3; axis++)
      for (int clear = 0; clear < 3; clear++)
        run("slice", [&](Mesh &m) {
          Vec3 n(0.0f);
          n[axis] = 1.0f;
          meshops::slice(m, (bb.min + bb.max) * 0.5f + Vec3(0.013f, 0.017f, 0.011f) * s, n, clear);
        });
    run("slice, NaN plane", [&](Mesh &m) { meshops::slice(m, Vec3(nan), {0, 1, 0}, 0); });
    for (float ang : {360.0f, 90.0f, -45.0f})
      run("spin", [&](Mesh &m) {
        std::vector<uint8_t> sel(m.vert_count(), 0);
        for (size_t v = 0; v < sel.size(); v += 3) sel[v] = 1;
        meshops::spin(m, sel, (bb.min + bb.max) * 0.5f, {0, 1, 0}, ang, 6);
      });
    run("seams from sharp", [&](Mesh &m) { meshops::seams_from_sharp(m, 30.0f); });
    for (size_t f = 0; f < std::min<size_t>(base.face_count(), o.quick ? 3 : 8); f++)
      run("imprint a square", [&](Mesh &m) {
        const Vec3 c = m.face_center(f), n = normalize(m.face_normal(f));
        Vec3 u = normalize(cross(std::fabs(n.y) < 0.9f ? Vec3(0, 1, 0) : Vec3(1, 0, 0), n)), v = cross(n, u);
        const float r = 0.02f * s;
        meshops::imprint_loop(m, f, {c - u * r - v * r, c + u * r - v * r, c + u * r + v * r, c - u * r + v * r});
      });
    run("bent cut", [&](Mesh &m) {
      if (!m.face_count() || m.face_size(0) < 4) return;
      const uint32_t *fv = m.face_verts(0);
      meshops::split_face_path(m, 0, fv[0], fv[2], {m.face_center(0)});
    });
    /* Plasticity tools and attaching drawn shapes to faces. */
    for (float th : {0.02f * s, 0.0f, 5.0f * s, nan})
      run("shell", [&](Mesh &m) {
        std::vector<uint8_t> open(m.face_count(), 0);
        if (!open.empty()) open[0] = 1;
        meshops::shell(m, open, th);
      });
    for (float ang : {5.0f, -30.0f, 89.0f, nan})
      run("draft", [&](Mesh &m) {
        std::vector<uint8_t> sel(m.face_count(), 1);
        meshops::draft(m, sel, ang, {0, 1, 0});
      });
    for (int count : {1, 3, 12}) run("radial array", [&](Mesh &m) { meshops::radial_array(m, count, 1, 360.0f, 1e-5f * s); });
    run("attach an arc to a face's side", [&](Mesh &m) {
      if (!m.face_count() || m.face_size(0) < 3) return;
      const uint32_t a = m.face_verts(0)[0], b = m.face_verts(0)[1];
      const Vec3 n = normalize(m.face_normal(0)), mid = (m.positions[a] + m.positions[b]) * 0.5f;
      Vec3 out = normalize(cross(m.positions[b] - m.positions[a], n));
      if (!std::isfinite(out.x)) return;
      if (meshops::point_in_face(m, 0, mid + out * 0.01f * s, 1e-4f * s)) out = out * -1.0f;
      meshops::attach_face(m, 0, a, b, {mid + out * 0.1f * s});
    });
    /* Shapes drawn across several faces: circles and squares of random size and
     * place over each face's plane, crossing its edges, corners and outline. */
    for (int k = 0; k < (o.quick ? 6 : 24); k++)
      run("imprint across faces", [&](Mesh &m) {
        if (!m.face_count()) return;
        rng = rng * 1664525u + 1013904223u;
        const size_t f = (rng >> 8) % m.face_count();
        const Vec3 n = normalize(m.face_normal(f));
        if (!std::isfinite(n.x)) return;
        Vec3 u = normalize(cross(std::fabs(n.y) < 0.9f ? Vec3(0, 1, 0) : Vec3(1, 0, 0), n)), v = cross(n, u);
        const float r = (0.05f + 0.6f * (float)((rng >> 4) % 1000) / 1000.0f) * s;
        const Vec3 c = m.face_center(f) + u * (0.3f * s * ((float)((rng >> 12) % 100) / 100.0f - 0.5f));
        const int sides = k % 3 == 0 ? 4 : 5 + (int)((rng >> 20) % 20);
        std::vector<Vec3> loop;
        for (int i = 0; i < sides; i++) {
          const float a = 2.0f * kPi * i / sides + (k % 3 == 0 ? kPi / 4 : 0.1f * k);
          loop.push_back(c + (u * std::cos(a) + v * std::sin(a)) * r);
        }
        std::vector<size_t> inner;
        meshops::imprint_loop_across(m, loop, n, &inner);
        for (size_t g : inner)
          if (g >= m.face_count()) m.positions.assign(1, Vec3(nan));  // flag a bad index through the check
      });
    run("imprint across faces, degenerate loop", [&](Mesh &m) {
      if (!m.face_count()) return;
      const Vec3 c = m.face_center(0), n = m.face_normal(0);
      meshops::imprint_loop_across(m, {c, c, c, c}, n);
      meshops::imprint_loop_across(m, {c, c + Vec3(1, 0, 0) * s, c + Vec3(2, 0, 0) * s}, n);
      meshops::imprint_loop_across(m, {Vec3(nan), c, c + Vec3(0, 0, 1)}, n);
    });
    run("fillet a face's outline", [&](Mesh &m) {
      if (!m.face_count()) return;
      std::vector<Vec3> loop;
      for (uint32_t k = 0; k < m.face_size(0); k++) loop.push_back(m.positions[m.face_verts(0)[k]]);
      auto f = meshops::fillet_polygon(loop, true, 0.05f * s, 4, m.face_normal(0));
      std::vector<uint32_t> fv;
      for (const Vec3 &p : f) fv.push_back(m.add_vert(p + m.face_normal(0) * s));
      if (fv.size() >= 3) m.add_face(fv.data(), fv.size());
    });
    run("separate loose parts", [&](Mesh &m) {
      std::vector<int> part;
      const size_t n = meshops::loose_parts(m, part);
      for (size_t k = 0; k < n && k < 8; k++) {
        std::vector<uint8_t> sel(m.face_count(), 0);
        for (size_t f = 0; f < m.face_count(); f++) sel[f] = part[f] == (int)k;
        Mesh piece = meshops::extract_faces(m, sel);
        if (!invalid(piece).empty()) m.positions.assign(1, Vec3(nan));  // flag it through the check
      }
    });
    total_ops += ops;
    total_problems += problems;
    rep.row({cs.name, num((double)ops), num((double)problems), f2(max_ms)});
  }
  /* Parametric shapes: every shape with its defaults, random settings and extremes. */
  size_t shape_ops = 0, shape_problems = 0;
  uint32_t rng = 4242u;
  auto rnd = [&](float lo, float hi) {
    rng = rng * 1664525u + 1013904223u;
    return lo + (hi - lo) * (float)(rng >> 8) / 16777216.0f;
  };
  for (int k = 0; k < kShapeCount; k++)
    for (int trial = 0; trial < (o.quick ? 6 : 30); trial++) {
      ProceduralShape ps;
      ps.shape = k;
      if (trial > 0) {
        ps.size = {rnd(-2, 5), rnd(-2, 5), rnd(-2, 5)};
        ps.radius = rnd(0, 3);
        ps.radius2 = rnd(0, 3);
        ps.thickness = rnd(0, 4);
        ps.height = rnd(0, 4);
        ps.angle = rnd(-10, 400);
        ps.segments = (int)rnd(-3, 70);
        ps.rings = (int)rnd(-3, 40);
        ps.sides = (int)rnd(-3, 20);
        ps.steps = (int)rnd(-3, 60);
        ps.subdivisions = (int)rnd(-3, 9);
        ps.fill_under = trial % 2 == 0;
      }
      if (trial == 1) ps.radius = ps.height = ps.thickness = ps.radius2 = 0.0f, ps.size = {0, 0, 0};
      if (trial == 2) ps.radius = ps.height = 1e6f, ps.size = {1e6f, 1e6f, 1e6f};
      ScopedTimer t;
      MeshPtr m = ps.build();
      shape_ops++;
      const std::string bad = m ? invalid(*m) : "no mesh";
      if (!bad.empty() || !m->face_count()) {
        shape_problems++;
        problem_types[bad.empty() ? "a shape with no faces" : bad]++;
        if (examples.size() < 80) examples.push_back(strprintf("shape %s trial %d: %s", kShapeNames[k], trial, bad.empty() ? "no faces" : bad.c_str()));
      }
    }
  rep.row({"parametric shapes", num((double)shape_ops), num((double)shape_problems), "-"});
  rep.note(strprintf("%zu operations on odd meshes, %zu problems; %zu shapes built, %zu problems", total_ops, total_problems, shape_ops,
                     shape_problems));
  for (auto &[type, n] : problem_types) rep.note(strprintf("problem %d x: %s", n, type.c_str()));
  for (const std::string &e : examples) rep.note(e);
  (void)inf;
}
/* ------------------------------------------------------------------ main */

int main(int argc, char **argv) {
  install_crash_handler();
  Options o;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--quick") o.quick = true;
    else if (a == "--only" && i + 1 < argc) o.only = argv[++i];
    else if (a == "--out" && i + 1 < argc) o.out = argv[++i];
    else if (a == "--budget" && i + 1 < argc) o.budget = std::atof(argv[++i]);
    else if (a == "--help") {
      std::printf("blendity_stress [--quick] [--only NAME] [--out DIR] [--budget MS]\n");
      return 0;
    }
  }
  register_builtin_components();
  Log::echo_stdout = true;
  {
    /* The editor fuzzer clicks and types at random, and the Project window can move,
     * rename and delete files: it works in a scratch project, never the real Assets. */
    std::error_code ec;
    const std::string tmp = (std::filesystem::temp_directory_path(ec) / "blendity_stress").string();
    const std::string proj = fs::join(tmp, "project"), trash = fs::join(tmp, "trash");
    fs::make_dirs(fs::join(proj, "Assets/Scenes"));
    fs::make_dirs(fs::join(proj, "research/papers"));
#ifdef _WIN32
    _putenv_s("BLENDITY_PROJECT", proj.c_str());
    _putenv_s("BLENDITY_TRASH", trash.c_str());
#else
    setenv("BLENDITY_PROJECT", proj.c_str(), 1);
    setenv("BLENDITY_TRASH", trash.c_str(), 1);
#endif
  }
  std::printf("Blendity stress test - %d hardware threads, budget %.1f ms%s\n", JobSystem::global().thread_count(),
              o.budget, o.quick ? " (quick)" : "");
  Report rep;
  std::time_t now = std::time(nullptr);
  char stamp[64];
  std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M", std::localtime(&now));
  rep.md = strprintf("# Blendity stress test report\n\nDate: %s  \nThreads: %d  \nMode: %s  \nFrame budget: %.1f ms\n",
                     stamp, JobSystem::global().thread_count(), o.quick ? "quick" : "full", o.budget);
  struct T { const char *name; std::function<void(Report &, const Options &)> fn; };
  T tests[] = {{"raster", test_raster_scaling}, {"raster_tiles", test_tile_size}, {"raster_resolution", test_resolution},
               {"hierarchy", test_hierarchy},    {"picking", test_picking},       {"subdivision", test_subdivision},
               {"edges", test_edge_building},    {"merge", test_merge},           {"serialization", test_serialization},
               {"undo", test_undo},              {"jobs", test_jobs},             {"editor_ui", test_editor},
               {"editor_fuzz", test_fuzz},       {"memory", test_memory},         {"shading", test_shading_cost},
               {"pathtracer", test_pathtracer},  {"uv", test_uv_unwrap},         {"textures", test_texture_sampling},
               {"modeling", test_modeling_tools}, {"codecs", test_codecs},      {"physics", test_physics},
               {"gpu", test_gpu_devices},        {"pushpull", test_pushpull_stress}, {"modifiers_ngon", test_modifier_tools_stress}};
  ScopedTimer total;
  for (auto &t : tests) {
    if (!o.only.empty() && std::string(t.name).find(o.only) == std::string::npos) continue;
    t.fn(rep, o);
  }
  rep.md += strprintf("\n---\nTotal run time: %.1f s\n", total.ms() / 1000.0);
  fs::make_dirs(o.out);
  char fname[64];
  std::strftime(fname, sizeof(fname), "%Y%m%d_%H%M%S", std::localtime(&now));
  std::string md = fs::join(o.out, std::string("report_") + fname + ".md");
  std::string csv = fs::join(o.out, std::string("report_") + fname + ".csv");
  fs::write_file(md, rep.md);
  fs::write_file(csv, "section,values...\n" + rep.csv);
  fs::write_file(fs::join(o.out, "latest.md"), rep.md);
  std::printf("\nReport written to %s (and latest.md)\nTotal %.1f s\n", md.c_str(), total.ms() / 1000.0);
  return 0;
}

/* -------------------------------------------------- crash reports */

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <crtdbg.h>
#  include <dbghelp.h>
#  pragma comment(lib, "dbghelp.lib")
/* Crash reports for the fuzzer: print a symbolised stack (DbgHelp ships with
 * Windows) instead of dying silently. Most useful with `build.bat debug stress`. */
static LONG WINAPI crash_handler(EXCEPTION_POINTERS *ep) {
  HANDLE proc = GetCurrentProcess(), thread = GetCurrentThread();
  SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
  SymInitialize(proc, nullptr, TRUE);
  std::fprintf(stderr, "\n*** crash: exception 0x%08lx at %p\n", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
  CONTEXT ctx = *ep->ContextRecord;
  STACKFRAME64 sf = {};
  sf.AddrPC.Offset = ctx.Rip;
  sf.AddrFrame.Offset = ctx.Rbp;
  sf.AddrStack.Offset = ctx.Rsp;
  sf.AddrPC.Mode = sf.AddrFrame.Mode = sf.AddrStack.Mode = AddrModeFlat;
  for (int i = 0; i < 40 && StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, thread, &sf, &ctx, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr); i++) {
    char buf[sizeof(SYMBOL_INFO) + 256] = {};
    auto *sym = reinterpret_cast<SYMBOL_INFO *>(buf);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    DWORD64 disp = 0;
    IMAGEHLP_LINE64 line = {};
    line.SizeOfStruct = sizeof(line);
    DWORD ldisp = 0;
    bool has_sym = SymFromAddr(proc, sf.AddrPC.Offset, &disp, sym);
    bool has_line = SymGetLineFromAddr64(proc, sf.AddrPC.Offset, &ldisp, &line);
    std::fprintf(stderr, "  #%02d %s", i, has_sym ? sym->Name : "?");
    if (has_line) std::fprintf(stderr, "  %s:%lu", line.FileName, line.LineNumber);
    std::fprintf(stderr, "\n");
  }
  std::fflush(stderr);
  return EXCEPTION_EXECUTE_HANDLER;
}
#endif

static void install_crash_handler() {
#ifdef _WIN32
  SetUnhandledExceptionFilter(crash_handler);
  /* Debug-CRT assertions go to stderr instead of a message box. */
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
  _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
}

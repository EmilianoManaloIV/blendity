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
#include "../src/render/pathtracer.h"
#include "../src/render/raster.h"
#include "../src/scene/material.h"
#include "../src/scene/mesh.h"
#include "../src/scene/scene.h"
#include "../src/scene/uv.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
#include <string>
#include <thread>
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
            "exact row spans. Each optimisation is then disabled in turn. Limit = largest triangle count under the budget.");
  auto mesh = primitives::ico_sphere(0.5f, 3);
  size_t tris_per = mesh->render_mesh().tri_count();
  Bench3D b(1280, 720);
  struct Cfg { const char *name; RasterOptions opt; };
  RasterOptions best;
  RasterOptions single = best; single.multithreaded = false;
  RasterOptions nocull = best; nocull.backface_culling = false;
  RasterOptions nospan = best; nospan.span_rows = false;
  Cfg cfgs[] = {{"optimised", best}, {"single thread", single}, {"no backface cull", nocull}, {"no row spans", nospan}};
  rep.table({"objects", "triangles", "optimised ms", "1 thread ms", "no-cull ms", "no-span ms", "Mtris/s (opt)"});
  std::vector<int> counts = o.quick ? std::vector<int>{16, 128, 512} : std::vector<int>{16, 64, 256, 1024, 2048, 4096, 8192, 16384};
  size_t limit_opt = 0, limit_single = 0;
  for (int n : counts) {
    Scene s = make_grid_scene(n, mesh, 1.3f);
    float extent = std::sqrt((float)n) * 0.65f + 2.0f;
    double ms[4];
    for (int c = 0; c < 4; c++) ms[c] = b.render(s, cfgs[c].opt, extent, n > 4096 ? 2 : 3);
    size_t tris = (size_t)n * tris_per;
    if (ms[0] <= o.budget) limit_opt = tris;
    if (ms[1] <= o.budget) limit_single = tris;
    rep.row({std::to_string(n), num((double)tris), f2(ms[0]), f2(ms[1]), f2(ms[2]), f2(ms[3]), f1(tris / ms[0] / 1000.0)});
    if (ms[1] > 2000) break;
  }
  rep.note(strprintf("60 FPS limit at 1280x720: ~%s triangles optimised vs ~%s single-threaded (%d threads available).",
                     num((double)limit_opt).c_str(), num((double)limit_single).c_str(), JobSystem::global().thread_count()));
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
                                   platform::KEY_L, platform::KEY_9, platform::KEY_F11, platform::KEY_I};
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
}

static void test_pathtracer(Report &rep, const Options &o) {
  rep.title("Path tracer: BVH build and ray throughput",
            "Grids of icospheres traced at 320x180, 1 sample per pass, 4 bounces (Cycles-like: binned-SAH BVH, NEE, MIS). "
            "Mrays/s counts every camera, bounce and shadow ray.");
  auto mesh = primitives::ico_sphere(0.5f, 3);
  const RenderMesh &rm = mesh->render_mesh_tangents();
  std::vector<MaterialPtr> mats = {make_material("bench", {0.8f, 0.8f, 0.8f})};
  rep.table({"objects", "triangles", "BVH build ms", "BVH nodes", "ms / sample", "Mrays/s"});
  Environment env;
  RenderLight sun;
  sun.direction = normalize(Vec3(-0.4f, -1.0f, 0.5f));
  sun.intensity = 3.0f;
  for (int n : o.quick ? std::vector<int>{16, 256} : std::vector<int>{16, 256, 1024, 4096, 16384}) {
    std::vector<PTObject> objs;
    int side = (int)std::ceil(std::sqrt((double)n));
    for (int i = 0; i < n; i++) objs.push_back({&rm, Mat4::translate({(i % side - side * 0.5f) * 1.3f, 0, (i / side - side * 0.5f) * 1.3f}), &mats});
    PathTracer pt;
    pt.build(objs, {sun}, env);
    float ext = side * 0.65f + 2.0f;
    pt.set_camera(Mat4::look_at({0, ext * 0.9f, -ext * 1.1f}, {0, 0, 0}, {0, 1, 0}), Mat4::perspective(60 * kDeg2Rad, 16.0f / 9.0f, 0.1f, 1000), 320, 180);
    pt.render(0, 1);  // warm-up
    pt.reset();
    ScopedTimer t;
    int spp = pt.render(o.quick ? 300 : 1000, 64);
    double ms = t.ms();
    rep.row({std::to_string(n), num((double)pt.stats().triangles), f1(pt.stats().bvh_build_ms), num((double)pt.stats().bvh_nodes),
             f1(ms / std::max(1, spp)), f1(pt.stats().mrays_per_s())});
  }
  rep.note("Ray cost grows ~logarithmically with triangle count thanks to the BVH. All objects share one mesh here, so the "
           "two-level BVH builds a single bottom-level tree (Cycles instancing).");
  /* Worst case for instancing: every object has its own mesh. */
  rep.table({"unique meshes", "triangles", "first build ms", "move 1 object ms", "edit 1 mesh ms", "rebuilt"});
  for (int n : o.quick ? std::vector<int>{64} : std::vector<int>{64, 256, 1024}) {
    std::vector<MeshPtr> meshes;
    std::vector<PTObject> objs;
    int side = (int)std::ceil(std::sqrt((double)n));
    for (int i = 0; i < n; i++) meshes.push_back(std::make_shared<Mesh>(*mesh));
    for (int i = 0; i < n; i++)
      objs.push_back({&meshes[i]->render_mesh_tangents(), Mat4::translate({(i % side - side * 0.5f) * 1.3f, 0, (i / side - side * 0.5f) * 1.3f}), &mats});
    PathTracer pt;
    pt.build(objs, {sun}, env);
    double first = pt.stats().bvh_build_ms;
    objs[0].model = Mat4::translate({0, 3, 0});
    pt.build(objs, {sun}, env);
    double moved = pt.stats().bvh_build_ms;
    meshes[1]->positions[0].y += 0.2f;
    meshes[1]->touch();
    objs[1].mesh = &meshes[1]->render_mesh_tangents();
    pt.build(objs, {sun}, env);
    rep.row({std::to_string(n), num((double)pt.stats().triangles), f1(first), f1(moved), f1(pt.stats().bvh_build_ms),
             std::to_string(pt.stats().meshes_rebuilt)});
  }
  rep.note("Bottom-level BVHs are cached by mesh content hash: moving an object only rebuilds the small top level; "
           "editing a mesh rebuilds just that mesh. The first version rebuilt one flat BVH over all triangles on any change "
           "(1.85 s at 1.3M triangles).");
}

static void test_uv_unwrap(Report &rep, const Options &o) {
  rep.title("UV: LSCM unwrap and packing",
            "LSCM (block-Jacobi conjugate gradients on the assembled normal equations, parallel sparse mat-vec) on an "
            "n x n grid - one island - and Smart UV Project + pack on a subdivided torus. Blender uses a sparse direct "
            "solver; CG iterations grow with the island's diameter, so very large single islands stay the slow case.");
  rep.table({"case", "faces", "islands", "ms"});
  for (int g : o.quick ? std::vector<int>{16, 64} : std::vector<int>{16, 64, 128, 256}) {
    Mesh m = *primitives::grid(10.0f, g, g);
    for (auto &p : m.positions) p.y = 0.3f * std::sin(p.x) * std::cos(p.z);  // not flat
    int islands = 0;
    double ms = time_ms([&] { islands = uvops::unwrap_lscm(m, nullptr); });
    rep.row({strprintf("LSCM grid %dx%d", g, g), num((double)m.face_count()), std::to_string(islands), f1(ms)});
    if (ms > 5000) break;
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
               {"modeling", test_modeling_tools}};
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

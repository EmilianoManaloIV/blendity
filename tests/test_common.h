// SPDX-License-Identifier: GPL-2.0-or-later
// blendity_tests: what every test file shares (task 0177 split tests/test_main.cpp into tests/test_<group>.cpp).
#pragma once
#include "../src/scene/curve.h"
#include "../src/core/core.h"
#include "../src/core/jobs.h"
#include "../src/core/cpu.h"
#include "../src/editor/editor.h"
#include "../src/editor/learn.h"
#include "../src/render/camera_filter.h"
#include "../src/render/canvas.h"
#include "../src/render/raster.h"
#include "../src/render/dof.h"
#include "../src/render/video_encode.h"
#include "../src/core/net.h"
#include "../src/core/releases.h"
#include "../src/core/launch.h"
#include "../src/research/research.h"
#include "../src/image/image.h"
#include "../src/render/colormanagement.h"
#include "../src/render/display.h"
#include "../src/render/gpu_device.h"
#include "../src/render/gpu_raster.h"
#include "../src/render/optix_device.h"
#include "../src/render/lightmapper.h"
#include "../src/render/pathtracer.h"
#include "../src/render/shading.h"
#include "../src/render/texture_bake.h"
#include "../extern/ufbx/ufbx.h"
#include "../src/scene/import.h"
#include "../src/scene/material.h"
#include "../src/scene/skin_weights.h"
#include "../src/scene/animation.h"
#include "../src/scene/mesh.h"
#include "../src/scene/physics.h"
#include "../src/scene/autophysics.h"
#include "../src/core/qp.h"
#include "../src/scene/spring_bones.h"
#include "../src/core/text_encoding.h"
#include "../src/scene/ragdoll.h"
#include "../src/scene/autopose.h"
#include "../src/scene/gait.h"
#include "../src/scene/footsteps.h"
#include "../src/scene/locomotion.h"
#include "../src/scene/inbetween.h"
#include "../src/scene/scene.h"
#include "../src/texture/procedural_texture.h"
#include "../src/scene/uv.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <fstream>
#include <iterator>
#include <thread>
#include <unordered_map>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <unordered_set>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace bl;

extern int g_fail, g_checks;  // (test_main.cpp)
#define CHECK(cond)                                                              \
  do {                                                                           \
    g_checks++;                                                                  \
    if (!(cond)) {                                                               \
      g_fail++;                                                                  \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);              \
    }                                                                            \
  } while (0)
#define CHECK_NEAR(a, b, eps) CHECK(std::fabs((double)(a) - (double)(b)) <= (eps))

inline void test(const char *name, const std::function<void()> &fn) {
  /* BLENDITY_TEST_FILTER=text runs only the tests whose name contains it. */
  static const char *filter = std::getenv("BLENDITY_TEST_FILTER");
  if (filter && !std::strstr(name, filter)) return;
  int before = g_fail;
  ScopedTimer t;
  fn();
  std::printf("%s %s (%.0f ms)\n", g_fail == before ? "[ ok ]" : "[FAIL]", name, t.ms());
  std::fflush(stdout);  // (a crash in the next test still shows this one)
}

/* ------------------------------------------------------- shared helpers */

inline const float kNaN = std::numeric_limits<float>::quiet_NaN();
inline const float kInf = std::numeric_limits<float>::infinity();

/* How many elements fail `ok`; prints the first one, so one CHECK(count_bad(...) == 0) over a whole
 * picture or table reports as much as a CHECK per element did, without counting thousands of checks. */
template <class V, class F>
inline size_t count_bad(const char *what, const V &v, F ok) {
  size_t bad = 0, first = 0;
  for (size_t i = 0; i < v.size(); i++)
    if (!ok(v[i]) && bad++ == 0) first = i;
  if (bad) std::printf("    %s: %zu of %zu bad, first at index %zu\n", what, bad, v.size(), first);
  return bad;
}
inline bool opaque(uint32_t c) { return (c >> 24) == 0xFF; }

/* Scratch files go to the OS temp folder, not next to the shipped executables. */
inline std::string test_dir() {
  static std::string dir = [] {
    std::error_code ec;
    std::string d = (std::filesystem::temp_directory_path(ec) / "blendity_tests").string();
    if (ec) d = fs::join(fs::executable_dir(), "test_output");
    fs::make_dirs(d);
    return d;
  }();
  return dir;
}

/* Every Editor in the tests works in a scratch project (and a scratch trash), never
 * the real Assets folder: the Project window can now move, rename and delete files.
 * Setting BLENDITY_PROJECT to "" returns to this default. */
extern std::string g_asset_root_at_start;  // the asset root main() set before any test (task 0081)

inline std::string scratch_project() {
  const std::string p = fs::join(test_dir(), "default_project");
  fs::make_dirs(fs::join(p, "Assets/Scenes"));
  fs::make_dirs(fs::join(p, "research/papers"));
  return p;
}

inline void set_env(const char *k, const std::string &v_in) {
  std::string v = v_in;
  if (v.empty() && std::string(k) == "BLENDITY_PROJECT") v = scratch_project();
  if (v.empty() && std::string(k) == "BLENDITY_TRASH") v = fs::join(test_dir(), "trash");
#ifdef _WIN32
  _putenv_s(k, v.c_str());
#else
  setenv(k, v.c_str(), 1);
#endif
}

inline int euler_characteristic(const Mesh &m) { return (int)m.vert_count() - (int)m.edge_count() + (int)m.face_count(); }

inline bool normals_outward(const Mesh &m, Vec3 c = Vec3(0.0f)) {
  for (size_t f = 0; f < m.face_count(); f++)
    if (dot(m.face_normal(f), m.face_center(f) - c) <= 0) return false;
  return true;
}

/* Every edge used by exactly two faces, in opposite directions. */
inline bool closed_manifold(const Mesh &m) {
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

inline float uv_area_total(const Mesh &m) {
  float a = 0;
  for (size_t f = 0; f < m.face_count(); f++) {
    uint32_t b = m.face_offsets[f], n = m.face_size(f);
    float s = 0;
    for (uint32_t i = 0; i < n; i++) {
      Vec2 p = m.uvs[b + i], q = m.uvs[b + (i + 1) % n];
      s += p.x * q.y - q.x * p.y;
    }
    a += std::fabs(0.5f * s);
  }
  return a;
}

inline std::vector<uint8_t> select_verts(const Mesh &m, std::initializer_list<uint32_t> vs) {
  std::vector<uint8_t> s(m.vert_count(), 0);
  for (uint32_t v : vs) s[v] = 1;
  return s;
}


/* Tests (round 135, tasks 0112-0114): an uncompressed scanline OpenEXR, float channels A B G R (EXR's order), as
 * MoonRay writes them for Blendity. */
inline std::string test_exr_plain(int w, int h, const std::vector<float> &rgba, bool half = false) {
  std::string o("\x76\x2F\x31\x01", 4);
  auto i32 = [&](int32_t v) { o.append((const char *)&v, 4); };
  auto attr = [&](const std::string &name, const std::string &type, const std::string &value) {
    o += name + '\0' + type + '\0';
    i32((int32_t)value.size());
    o += value;
  };
  i32(2);
  std::string ch;
  for (const char *c : {"A", "B", "G", "R"}) {
    ch += std::string(c) + '\0';
    const int32_t t = half ? 1 : 2, one = 1;
    ch.append((const char *)&t, 4);
    ch += std::string(4, '\0');
    ch.append((const char *)&one, 4);
    ch.append((const char *)&one, 4);
  }
  ch += '\0';
  attr("channels", "chlist", ch);
  attr("compression", "compression", std::string(1, '\0'));
  const int32_t box[4] = {0, 0, w - 1, h - 1};
  attr("dataWindow", "box2i", std::string((const char *)box, 16));
  attr("displayWindow", "box2i", std::string((const char *)box, 16));
  attr("lineOrder", "lineOrder", std::string(1, '\0'));
  o += '\0';
  const size_t row = (size_t)w * 4 * (half ? 2 : 4);
  uint64_t at = o.size() + (size_t)h * 8;
  for (int y = 0; y < h; y++, at += 8 + row) o.append((const char *)&at, 8);
  for (int y = 0; y < h; y++) {
    i32(y);
    i32((int32_t)row);
    for (int c : {3, 2, 1, 0})
      for (int x = 0; x < w; x++) {
        const float v = rgba[((size_t)y * w + x) * 4 + c];
        if (!half) {
          o.append((const char *)&v, 4);
          continue;
        }
        uint32_t b;  // (exact for the test's values: no rounding needed)
        std::memcpy(&b, &v, 4);
        const uint16_t hv = v == 0.0f ? 0 : (uint16_t)(((b >> 16) & 0x8000) | ((((b >> 23) & 0xFF) - 112) << 10) | ((b >> 13) & 0x3FF));
        o.append((const char *)&hv, 2);
      }
  }
  return o;
}


/* This program, for tests that run it as another program (the fake MoonRay). */
inline std::string test_program() {
#ifdef _WIN32
  return fs::join(fs::executable_dir(), "blendity_tests.exe");
#else
  return fs::join(fs::executable_dir(), "blendity_tests");
#endif
}

/* ---------------------------------------------- helpers several groups share (task 0177) */

/* ===================================================================== */

/* Signed volume (divergence theorem over fan triangles): > 0 when faces point outward. */
inline float signed_volume(const Mesh &m) {
  double v = 0;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t *fv = m.face_verts(f);
    for (uint32_t i = 1; i + 1 < m.face_size(f); i++)
      v += dot(m.positions[fv[0]], cross(m.positions[fv[i]], m.positions[fv[i + 1]]));
  }
  return (float)(v / 6.0);
}

inline size_t face_facing(const Mesh &m, Vec3 n) {
  for (size_t f = 0; f < m.face_count(); f++)
    if (dot(m.face_normal(f), n) > 0.99f) return f;
  return SIZE_MAX;
}

/* True when a ray along d through p passes through the mesh without hitting it. */
inline bool ray_misses(const Mesh &m, Vec3 p, Vec3 d) {
  const RenderMesh &rm = m.render_mesh();
  Ray r{p, normalize(d)};
  for (size_t t = 0; t < rm.tri_count(); t++)
    if (ray_triangle(r, rm.positions[rm.indices[t * 3]], rm.positions[rm.indices[t * 3 + 1]], rm.positions[rm.indices[t * 3 + 2]]) > 0) return false;
  return true;
}


/* ===================================================================== */
/* Join / Boolean / Separate, vertex & edge extrusion, origins, modifiers, */
/* parametric shapes, n-gon editing                                        */
/* ===================================================================== */

inline GameObject *by_name(Scene &s, const char *n) { return s.find_by_name(n); }


/* ===================================================================== */
/* Drawing on rotated objects and drawing edge cases, Plasticity tools,     */
/* surface / vertex snapping, camera piloting, the new windows              */
/* ===================================================================== */

inline bool structurally_valid(const Mesh &m, std::string *why = nullptr) {
  for (Vec3 p : m.positions)
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
      if (why) *why = "non-finite position";
      return false;
    }
  std::unordered_map<uint64_t, int> uses;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t n = m.face_size(f), *v = m.face_verts(f);
    if (n < 3) {
      if (why) *why = "face with < 3 corners";
      return false;
    }
    for (uint32_t i = 0; i < n; i++) {
      if (v[i] >= m.vert_count() || v[i] == v[(i + 1) % n]) {
        if (why) *why = "bad corner";
        return false;
      }
      if (++uses[Mesh::edge_key(v[i], v[(i + 1) % n])] > 2) {
        if (why) *why = "an edge on three faces";
        return false;
      }
    }
  }
  if (!m.uvs.empty() && m.uvs.size() != m.corner_verts.size()) {
    if (why) *why = "UV count";
    return false;
  }
  return true;
}


/* ===================================================================== */
/* Round 13: Push/Pull sequences without overlapping faces, Delete Loose, */
/* region inset, empty number fields, face centres, depth of field        */
/* ===================================================================== */

inline size_t overlapping_face_pairs(const Mesh &m, std::string *first = nullptr) {
  std::vector<std::pair<uint32_t, uint32_t>> pairs;
  const size_t n = meshops::overlapping_faces(m, 0.0f, &pairs);
  if (first && n) *first = strprintf("faces %u and %u", pairs[0].first, pairs[0].second);
  return n;
}


/* ===================================================================== */
/* Round 14: archways pushed through, drawing over existing edges,       */
/* Smart Fill, auto smooth, camera sequences                             */
/* ===================================================================== */

/* Faces whose triangles don't add up to the polygon (overlapping or missing
 * triangles: the "face artifacts" of a bad triangulation), or point the wrong way. */
inline size_t bad_triangulations(const Mesh &m, std::string *first = nullptr) {
  const RenderMesh &rm = m.render_mesh(true);
  std::vector<double> tri_area(m.face_count(), 0.0);
  std::vector<int> flipped(m.face_count(), 0);
  for (size_t t = 0; t < rm.tri_count(); t++) {
    const uint32_t f = rm.tri_face[t];
    const Vec3 a = rm.positions[rm.indices[t * 3]], b = rm.positions[rm.indices[t * 3 + 1]], c = rm.positions[rm.indices[t * 3 + 2]];
    const Vec3 cr = cross(b - a, c - a);
    tri_area[f] += 0.5 * length(cr);
    if (dot(cr, m.face_normal(f)) < -1e-9f * length(m.face_normal(f))) flipped[f]++;
  }
  size_t bad = 0;
  for (size_t f = 0; f < m.face_count(); f++) {
    Vec3 nw(0.0f);
    for (uint32_t k = 0; k < m.face_size(f); k++) nw += cross(m.positions[m.face_verts(f)[k]], m.positions[m.face_verts(f)[(k + 1) % m.face_size(f)]]);
    const double area = 0.5 * length(nw);
    if (std::fabs(tri_area[f] - area) > 1e-4 * std::max(1.0, area) || flipped[f]) {
      if (first && !bad) *first = strprintf("face %zu (%u corners): polygon %.5f, triangles %.5f, flipped %d", f, m.face_size(f), area, tri_area[f], flipped[f]);
      bad++;
    }
  }
  return bad;
}


/* ===================================================================== */
/* Round 27 (task 0004): camera filters, with the PS1 look               */
/* ===================================================================== */

inline bool cf_is_15bit(uint32_t c) { return (c & 0x070707u) == 0; }

/* A tiny scene for direct renders: items keep their meshes and materials alive. */
struct CfScene {
  std::vector<MeshPtr> meshes;
  std::vector<std::vector<MaterialPtr>> mats;
  std::vector<DrawItem> items;
  void add(MeshPtr m, const Mat4 &model, uint32_t id, MaterialPtr mat) {
    meshes.push_back(m);
    mats.push_back({mat});
    DrawItem it;
    it.mesh = &m->render_mesh();
    it.model = model;
    it.id = id;
    it.unlit = mat && mat->unlit;
    items.push_back(it);
  }
  void seal() {  // the material vectors are stable now
    for (size_t i = 0; i < items.size(); i++) items[i].materials = &mats[i];
  }
};

inline MaterialPtr cf_unlit(Vec3 c) {
  auto m = make_material("Unlit", c);
  m->unlit = true;
  return m;
}

/* Draws the scene into an attached target: deferred, one sun, cleared to `clear`. */
inline void cf_draw(RenderTarget &rt, const Mat4 &v, const Mat4 &p, const RasterOptions &opt0, const CfScene &sc, uint32_t clear,
                    Renderer3D *keep = nullptr) {
  Renderer3D local;
  Renderer3D &r = keep ? *keep : local;
  RasterOptions opt = opt0;
  opt.shade = ShadeMode::Deferred;
  LightingEnv env;
  RenderLight sun;
  sun.direction = normalize(Vec3(-0.3f, -1, 0.4f));
  env.lights.push_back(sun);
  env.camera_pos = Vec3(0.0f);
  r.begin(&rt, v, p, env, opt);
  r.clear(clear);
  for (const DrawItem &it : sc.items) r.add(it);
  r.flush();
}

inline void cf_render(Image &img, RenderTarget &rt, int W, int H, const Mat4 &v, const Mat4 &p, const RasterOptions &opt0, const CfScene &sc,
                      uint32_t clear = 0xFF204060u, Renderer3D *keep = nullptr) {
  img.resize(W, H);
  rt.attach(img, {0, 0, W, H});
  cf_draw(rt, v, p, opt0, sc, clear, keep);
}

inline uint64_t cf_hash(const Image &img) {
  uint64_t h = 1469598103934665603ull;
  for (uint32_t c : img.pixels) h = (h ^ c) * 1099511628211ull;
  return h;
}

/* Colour changes along the busiest row, and rows that differ from the one above, inside r. */
inline void cf_blockiness(const uint32_t *px, int stride, const Recti &r, int &max_runs, int &row_changes) {
  max_runs = 0;
  row_changes = 0;
  for (int y = r.y; y < r.bottom(); y++) {
    const uint32_t *row = px + (size_t)y * stride;
    int runs = 1;
    for (int x = r.x + 1; x < r.right(); x++) runs += row[x] != row[x - 1];
    max_runs = std::max(max_runs, runs);
    if (y > r.y && std::memcmp(row + r.x, px + (size_t)(y - 1) * stride + r.x, sizeof(uint32_t) * r.w) != 0) row_changes++;
  }
}

/* Task 0005 moved the Retro Console filter into a Camera Filters stack (ADR 0008): these find and
 * add it there, so round 27's tests keep testing the same behaviour. */
inline RetroConsoleFilter *cf_add_retro(GameObject *cam) {
  auto *cf = cam->get<CameraFilters>();
  if (!cf) cf = cam->add<CameraFilters>();
  return static_cast<RetroConsoleFilter *>(cf->add(RetroConsoleFilter::kName));
}
inline std::vector<RetroConsoleFilter *> cf_all_retro(const GameObject *cam) {
  std::vector<RetroConsoleFilter *> out;
  if (const auto *cf = cam ? cam->get<CameraFilters>() : nullptr)
    for (const auto &e : cf->effects)
      if (auto *r = dynamic_cast<RetroConsoleFilter *>(e.get())) out.push_back(r);
  return out;
}
inline RetroConsoleFilter *cf_get_retro(const GameObject *cam) {
  auto all = cf_all_retro(cam);
  return all.empty() ? nullptr : all[0];
}


/* ===================================================================== */
/* Round 29 (task 0005): the Camera Filters stack, Retro Console presets  */
/* ===================================================================== */

inline CameraFilters *r29_stack(Editor &ed) {
  GameObject *c = ed.scene().find_by_name("Main Camera");
  return c ? c->get<CameraFilters>() : nullptr;
}
inline std::vector<RetroConsoleFilter *> r29_effects(Editor &ed) {
  return cf_all_retro(ed.scene().find_by_name("Main Camera"));
}


/* ===================================================================== */
/* Round 31 (task 0006): Color, Lens and Stylize camera filters           */
/* ===================================================================== */

/* A small synthetic target: an Image plus the RenderTarget over it (with depth and ids planes). */
struct R31Target {
  Image img;
  RenderTarget rt;
  R31Target(int w, int h) {
    img.resize(w, h);
    rt.attach(img, {0, 0, w, h});
  }
  R31Target(const R31Target &) = delete;
  R31Target &operator=(const R31Target &) = delete;
  uint32_t &at(int x, int y) { return img.pixels[(size_t)y * img.width + x]; }
  void fill(uint32_t c) { std::fill(img.pixels.begin(), img.pixels.end(), c); }
  void random(uint32_t seed) {
    uint32_t s = seed * 2654435761u + 12345u;
    for (uint32_t &p : img.pixels) {
      s = s * 1664525u + 1013904223u;
      p = 0xFF000000u | (s >> 8);
    }
  }
  /* Random colours plus random depth and ids, for passes that read them. */
  void random_planes(uint32_t seed) {
    random(seed);
    uint32_t s = seed * 40503u + 7u;
    for (size_t i = 0; i < rt.depth.size(); i++) {
      s = s * 1664525u + 1013904223u;
      rt.depth[i] = (s >> 8) / 16777216.0f;
      s = s * 1664525u + 1013904223u;
      rt.ids[i] = (s >> 24) % 5;
    }
  }
};
inline int r31_ch(uint32_t p, int k) { return (int)((p >> (16 - 8 * k)) & 255); }
inline float r31_dec(int v) {
  const float c = v / 255.0f;
  return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
/* The frame an Edge Outline pass needs: identity camera, so distance == the stored depth value. */
inline FilterFrame r31_frame() {
  FilterFrame f;
  f.eye = {0, 0, 0};
  f.forward = {0, 0, 1};
  return f;
}

/* Runs every pass with the given odd number in every float slot (and an odd int in every int slot). */
inline void r31_run_all(RenderTarget &rt, float v, int iv, const FilterFrame *frame) {
  ColorGradingParams cg;
  cg.exposure = cg.contrast = cg.saturation = cg.temperature = cg.tint = v;
  cg.lift = Vec3(v);
  cg.gamma = Vec3(v);
  cg.gain = Vec3(v);
  apply_color_grading(rt, cg);
  apply_posterize(rt, iv);
  apply_grayscale(rt, false, v);
  apply_grayscale(rt, true, v);
  apply_invert(rt, v);
  VignetteParams vg;
  vg.intensity = vg.smoothness = vg.roundness = v;
  vg.color = Vec3(v);
  apply_vignette(rt, vg);
  apply_chromatic_aberration(rt, v);
  GrainParams gp;
  gp.intensity = gp.size = gp.response = v;
  apply_film_grain(rt, gp, frame);
  apply_lens_distortion(rt, v, v);
  apply_lens_distortion(rt, v, 1.0f);
  apply_lens_distortion(rt, 0.5f, v);
  apply_pixelate(rt, iv);
  OutlineParams op;
  op.color = Vec3(v);
  op.thickness = iv;
  op.depth_sensitivity = v;
  apply_edge_outline(rt, op, frame);
  CrtParams cp;
  cp.scanlines = cp.curvature = cp.mask = cp.flicker = v;
  apply_crt(rt, cp, frame);
  apply_sharpen(rt, v);
}

/* One effect for the registry test: a field to move off its default and how to read it back. */
struct R31Case {
  const char *name, *field, *value;
  std::function<double(FilterEffect *)> read;
  double expect;
};


/* ===================================================================== */
/* Round 34: the editor stops redoing work it has done (task 0011)         */
/* ===================================================================== */

using R34ET = platform::EventType;

inline platform::Event r34_mouse(R34ET t, int x, int y) {
  platform::Event e;
  e.type = t;
  e.x = x;
  e.y = y;
  return e;
}

/* What the 3D views did over a stretch of frames. */
struct R34Count {
  uint64_t frames = 0, renders = 0, hits = 0, shadows = 0, mismatches = 0;
};
inline R34Count r34_totals(const Editor &ed) {
  const FrameProfile &p = ed.frame_profile_totals();
  return {p.frames, p.view_renders, p.view_cache_hits, p.shadow_renders, p.cache_mismatches};
}
inline R34Count operator-(const R34Count &a, const R34Count &b) {
  return {a.frames - b.frames, a.renders - b.renders, a.hits - b.hits, a.shadows - b.shadows, a.mismatches - b.mismatches};
}

inline std::vector<uint32_t> r34_crop(const Editor &ed, const Recti &r) {
  std::vector<uint32_t> out;
  const Image &fb = ed.framebuffer();
  for (int y = r.y; y < r.bottom() && y < fb.height; y++)
    for (int x = r.x; x < r.right() && x < fb.width; x++) out.push_back(fb.pixels[(size_t)y * fb.width + x]);
  return out;
}

inline void r34_steps(Editor &ed, int n) {
  for (int i = 0; i < n; i++) ed.step_frame_headless();
}

/* A click with real events (moved there first, then down, then up on later frames). */
inline void r34_click(Editor &ed, int x, int y) {
  ed.step_frame_headless({r34_mouse(R34ET::MouseMove, x, y)});
  ed.step_frame_headless({r34_mouse(R34ET::MouseDown, x, y)});
  ed.step_frame_headless({r34_mouse(R34ET::MouseUp, x, y)});
}

/* The Scene view's toolbar buttons after the draw-mode combo: 0 scene lighting, 1 grid, 2 gizmos, 3 statistics. */
inline void r34_toolbar_click(Editor &ed, int index) {
  const ui::Context &u = ed.ui_for_test();
  const Recti v = ed.scene_view_rect();
  const int bh = u.row_h() + u.px(4), h = bh - u.px(6);
  const int x = v.x + u.px(6) + u.px(176) + index * (h + u.px(2)) + h / 2, y = v.y - bh + u.px(3) + h / 2;
  r34_click(ed, x, y);
}

/* The mouse rests over the Hierarchy: nothing under it draws in a 3D view. */
inline void r34_park_mouse(Editor &ed) {
  const Recti h = ed.window_rect_for_test(WindowKind::Hierarchy);
  ed.step_frame_headless({r34_mouse(R34ET::MouseMove, h.x + h.w / 2, h.y + h.h - 10)});
}

/* A 1600x1000 editor with the 2 by 3 layout (Scene and Game both visible), a known camera, and the
 * Statistics overlay off (it prints frame times, which change every frame by design). */
inline void r34_open(Editor &ed, bool two_by_three = true) {
  ed.init_headless(1600, 1000);
  /* These tests count renders and hits: verify mode (BLENDITY_RENDER_CACHE_VERIFY, which re-renders every
   * hit) would turn the hits into renders. The verify-mode test turns it on itself. */
  ed.set_render_cache_verify(false);
  if (two_by_three) ed.command("layout 2 by 3");
  ed.command("camera 25 20 7 0 0.5 0");
  r34_steps(ed, 3);
  r34_toolbar_click(ed, 3);
  r34_park_mouse(ed);
  r34_steps(ed, 4);
}

/* Drags the Move gizmo's X arrow (at a world point) with a real mouse drag. */
inline void r34_gizmo_drag_x(Editor &ed, Vec3 world_point, int pixels) {
  int x = 0, y = 0;
  CHECK(ed.project_to_window(world_point, x, y));
  ed.step_frame_headless({r34_mouse(R34ET::MouseMove, x + 55, y)});
  ed.step_frame_headless({r34_mouse(R34ET::MouseDown, x + 55, y)});
  ed.step_frame_headless({r34_mouse(R34ET::MouseMove, x + 55 + pixels / 2, y)});
  ed.step_frame_headless({r34_mouse(R34ET::MouseMove, x + 55 + pixels, y)});
  ed.step_frame_headless({r34_mouse(R34ET::MouseUp, x + 55 + pixels, y)});
}

struct R34Change {
  R34Count d;                           // what the action's frames cost
  std::vector<uint32_t> before, after;  // the Scene view's pixels
  Recti rect_before, rect_after;
};
inline R34Change r34_change(Editor &ed, const std::function<void()> &act, int frames = 1) {
  R34Change c;
  c.rect_before = ed.scene_view_rect();
  c.before = r34_crop(ed, c.rect_before);
  const R34Count t0 = r34_totals(ed);
  act();
  r34_steps(ed, frames);
  c.d = r34_totals(ed) - t0;
  c.rect_after = ed.scene_view_rect();
  c.after = r34_crop(ed, c.rect_after);
  return c;
}

/* One "this must redraw the Scene view" case on a fresh editor. */
inline void r34_redraw_case(const char *name, const std::function<void(Editor &)> &setup, const std::function<void(Editor &)> &act,
                     const std::function<void(Editor &, const R34Change &)> &extra = {}) {
  test(name, [=] {
    Editor ed;
    r34_open(ed);
    if (setup) setup(ed);
    r34_steps(ed, 4);
    const R34Change c = r34_change(ed, [&] { act(ed); });
    std::printf("    renders %llu, cache hits %llu, shadow maps %llu\n", (unsigned long long)c.d.renders, (unsigned long long)c.d.hits,
                (unsigned long long)c.d.shadows);
    CHECK(c.d.renders >= 1);
    CHECK(c.before != c.after);
    if (extra) extra(ed, c);
    /* ...and it settles again: the next idle frames cost nothing. */
    const R34Count t0 = r34_totals(ed);
    r34_steps(ed, 3);
    CHECK((r34_totals(ed) - t0).renders == 0);
  });
}

/* A scene for the Renderer3D tests: two cubes with different ids. */
struct R34Raster {
  CfScene sc;
  Mat4 v, p;
  int W, H;
  explicit R34Raster(int w = 300, int h = 200) : W(w), H(h) {
    sc.add(primitives::cube(), Mat4::translate(Vec3(0.0f, 0.0f, 0.0f)), 9, make_material("a", {0.8f, 0.3f, 0.2f}));
    sc.add(primitives::cube(), Mat4::translate(Vec3(2.2f, 0.4f, 1.0f)), 5, make_material("b", {0.2f, 0.5f, 0.8f}));
    sc.seal();
    v = Mat4::look_at({3, 3, -6}, {0.5f, 0, 0}, {0, 1, 0});
    p = Mat4::perspective(60 * kDeg2Rad, w / (float)h, 0.1f, 60);
  }
  void render(Image &img, RenderTarget &rt, Renderer3D &r, ShadeMode mode = ShadeMode::Deferred) const {
    img.resize(W, H);
    rt.attach(img, {0, 0, W, H});
    RasterOptions opt;
    opt.shade = mode;
    LightingEnv env;
    RenderLight sun;
    sun.direction = normalize(Vec3(-0.3f, -1, 0.4f));
    env.lights.push_back(sun);
    r.begin(&rt, v, p, env, opt);
    r.clear(0xFF204060u);
    for (const DrawItem &it : sc.items) r.add(it);
    r.flush();
  }
};

struct R34Rng {
  uint32_t s;
  float f() {
    s = s * 1664525u + 1013904223u;
    return (float)(s >> 8) / (float)(1u << 24);
  }
  float range(float a, float b) { return a + (b - a) * f(); }
};


/* ===================================================================== */
/* Round 35: baked lighting - lightmaps and light modes (task 0012)        */
/* ===================================================================== */

/* A tiny deterministic generator for the reference gathers. */
struct R35Rng {
  uint32_t s = 2463534242u;
  float f() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return (s >> 8) * (1.0f / 16777216.0f);
  }
};

inline bool r35_finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

/* Small, quick settings: a bake of a few thousand texels takes well under a second. */
inline BakeSettings r35_small(float tpu = 8.0f, int samples = 32) {
  BakeSettings s;
  s.texels_per_unit = tpu;
  s.max_size = 256;
  s.padding = 2;
  s.direct_samples = 8;
  s.indirect_samples = samples;
  s.bounces = 2;
  s.denoise = true;
  return s;
}

inline LightingData r35_run(Lightmapper &lm, const std::vector<BakeObject> &objs, const std::vector<BakeLight> &lights, const Environment &env,
                     const BakeSettings &s) {
  lm.begin(objs, lights, env, s);
  for (int i = 0; i < 100000 && !lm.step(1000.0); i++) {}
  CHECK(!lm.active());
  return lm.take_result();
}

inline BakeObject r35_object(uint64_t id, MeshPtr m, const Mat4 &model, Vec3 albedo = Vec3(0.8f)) {
  BakeObject o;
  o.id = id;
  o.mesh = m;
  o.model = model;
  o.materials = {make_material("m", albedo)};
  o.hash = lightmap_content_hash(*m, model);
  return o;
}

/* The lightmap value at a world point on a baked object: finds the render-mesh triangle the point is on,
 * interpolates the entry's lightmap UVs and samples the page. False when no triangle holds the point. */
inline bool r35_sample(const LightingData &ld, uint64_t id, const Mesh &m, const Mat4 &model, Vec3 p, Vec3 &out) {
  auto e = ld.entries.find(id);
  if (e == ld.entries.end() || e->second.page < 0 || e->second.page >= (int)ld.pages.size()) return false;
  const RenderMesh &rm = m.render_mesh();
  if (e->second.tri_uv.size() != rm.tri_count() * 3) return false;
  for (size_t t = 0; t < rm.tri_count(); t++) {
    const Vec3 a = model.point(rm.positions[rm.indices[t * 3]]), b = model.point(rm.positions[rm.indices[t * 3 + 1]]),
               c = model.point(rm.positions[rm.indices[t * 3 + 2]]);
    const Vec3 n = cross(b - a, c - a);
    const float area2 = dot(n, n);
    if (!(area2 > 1e-12f)) continue;
    if (std::fabs(dot(p - a, n)) / std::sqrt(area2) > 0.02f) continue;  // not on this triangle's plane
    const float w1 = dot(cross(p - a, c - a), n) / area2, w2 = dot(cross(b - a, p - a), n) / area2, w0 = 1.0f - w1 - w2;
    if (w0 < -1e-3f || w1 < -1e-3f || w2 < -1e-3f) continue;
    const Vec2 uv = e->second.tri_uv[t * 3] * w0 + e->second.tri_uv[t * 3 + 1] * w1 + e->second.tri_uv[t * 3 + 2] * w2;
    out = ld.pages[(size_t)e->second.page].sample(uv);
    return true;
  }
  return false;
}

/* ---- editor scenes ---- */

/* Window with the Scene and Game views, the default objects hidden: a camera, the sun, a grey world. */
inline void r35_open(Editor &ed) {
  r34_open(ed);
  for (const char *n : {"Plane", "Cube", "Sphere", "Cylinder"})
    if (GameObject *g = ed.scene().find_by_name(n)) g->active = false;
  EnvironmentSettings &env = ed.scene().environment;
  env.mode = 3;
  env.color = Vec3(0.25f);
  env.strength = 1.0f;
  LightingSettings &ls = ed.scene().lighting;
  ls.texels_per_unit = 8.0f;
  ls.max_size = 256;
  ls.padding = 2;
  ls.indirect_samples = 32;
  ls.direct_samples = 8;
  GameObject *sun = ed.scene().find_by_name("Directional Light");
  sun->set_local_euler({50, 90, 0});
  Light *l = sun->get<Light>();
  l->color = Vec3(1.0f);
  l->intensity = 1.0f;
  ed.commit_change("r35 setup");
}

inline GameObject *r35_add(Editor &ed, const char *kind, const char *name, Vec3 pos, Vec3 scale, Vec3 colour, bool gi = true) {
  GameObject *g = create_primitive(ed.scene(), kind);
  g->name = name;
  g->set_local_position(pos);
  g->set_local_scale(scale);
  MeshRenderer *mr = g->get<MeshRenderer>();
  mr->materials = {make_material(name, colour)};
  mr->contribute_gi = gi;
  return g;
}

inline void r35_sun_mode(Editor &ed, int mode) {
  ed.scene().find_by_name("Directional Light")->get<Light>()->mode = mode;
  ed.commit_change("r35 sun mode");
}

inline void r35_bake(Editor &ed) {
  ed.command("bake start");
  CHECK(ed.baking_for_test());
  for (int i = 0; i < 100 && ed.baking_for_test(); i++) ed.step_frame_headless();
  CHECK(!ed.baking_for_test());
  r34_steps(ed, 4);
}

inline std::vector<uint32_t> r35_game(Editor &ed) { return r34_crop(ed, ed.window_rect_for_test(WindowKind::Game)); }
inline std::vector<uint32_t> r35_scene_view(Editor &ed) { return r34_crop(ed, ed.scene_view_rect()); }

inline double r35_luma(const std::vector<uint32_t> &px) {
  double s = 0;
  for (uint32_t c : px) s += ((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255);
  return s;
}

/* How many pixels differ by more than `tol` in some channel. */
inline size_t r35_diff_count(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b, int tol = 0) {
  if (a.size() != b.size()) return std::max(a.size(), b.size());
  size_t n = 0;
  for (size_t i = 0; i < a.size(); i++) {
    const int dr = std::abs((int)((a[i] >> 16) & 255) - (int)((b[i] >> 16) & 255)), dg = std::abs((int)((a[i] >> 8) & 255) - (int)((b[i] >> 8) & 255)),
              db = std::abs((int)(a[i] & 255) - (int)(b[i] & 255));
    if (std::max({dr, dg, db}) > tol) n++;
  }
  return n;
}

/* A scratch scene file in the scratch project's Assets/Scenes, with the scene named after it. */
inline std::string r35_scene_path(Editor &ed, const char *name) {
  const std::string p = fs::join(fs::join(scratch_project(), "Assets/Scenes"), std::string(name) + ".scene");
  ed.scene().name = name;
  ed.scene().path = p;
  return p;
}

/* A red or white wall (x = 0) beside a white floor, the sun on the wall's -x side; the floor's lightmap
 * near the wall (0.4 from it) and far from it (4 away), on the sun's side. */
struct R35Bleed {
  Vec3 near_v, far_v;
  bool ok = false;
};
inline R35Bleed r35_bleed(int sun_mode, Vec3 wall_colour) {
  Editor ed;
  r35_open(ed);
  GameObject *floor_go = r35_add(ed, "Plane", "Floor", {0, 0, 0}, {1, 1, 1}, Vec3(0.8f));
  r35_add(ed, "Cube", "Wall", {0, 1, 0}, {0.2f, 2, 6}, wall_colour);
  r35_sun_mode(ed, sun_mode);
  r35_bake(ed);
  const LightingData &ld = ed.lighting_data_for_test();
  R35Bleed r;
  const Vec3 fwd = ed.scene().find_by_name("Directional Light")->world_rotation().rotate({0, 0, 1});
  const float side = fwd.x > 0 ? -1.0f : 1.0f;  // the sun's side of the wall
  const Mesh *fm = floor_go->evaluated_mesh(1);
  r.ok = fm && r35_sample(ld, floor_go->id, *fm, floor_go->world_matrix(), {side * 0.4f, 0, 0}, r.near_v) &&
         r35_sample(ld, floor_go->id, *fm, floor_go->world_matrix(), {side * 4.0f, 0, 0}, r.far_v);
  return r;
}

/* Union-find over triangles: islands are triangles joined across edges whose two UV ends agree. */
struct R35Islands {
  std::vector<int> parent;
  int find(int a) { return parent[(size_t)a] == a ? a : parent[(size_t)a] = find(parent[(size_t)a]); }
};

/* Checks one mesh's lightmap UVs: inside [0,1], finite, no two triangles overlapping, total area <= 1, islands
 * apart by at least `gap` cells of a res x res grid. Returns the number of problems. */
inline int r35_uv_problems(const char *what, const Mesh &m, int res, int padding, bool check_gap) {
  const std::vector<Vec2> uv = lightmap_uvs(m, true, res, padding);
  const RenderMesh &rm = m.render_mesh();
  int bad = 0;
  if (uv.size() != m.corner_verts.size()) {
    std::printf("    %s: %zu uvs for %zu corners\n", what, uv.size(), m.corner_verts.size());
    return 1;
  }
  for (const Vec2 &u : uv)
    if (!(std::isfinite(u.x) && std::isfinite(u.y) && u.x >= 0.0f && u.x <= 1.0f && u.y >= 0.0f && u.y <= 1.0f)) bad++;
  if (bad) std::printf("    %s: %d corners outside 0..1\n", what, bad);
  const size_t nt = rm.tri_count();
  auto tuv = [&](size_t t, int k) { return uv[rm.tri_corner[t * 3 + (size_t)k]]; };
  /* Overlap: rasterize every triangle's strict interior on a fine grid; one cell in two triangles = overlap. */
  const int G = 384;
  std::vector<int32_t> owner((size_t)G * G, -1);
  int overlap_cells = 0;
  double area = 0;
  for (size_t t = 0; t < nt; t++) {
    const Vec2 a = tuv(t, 0), b = tuv(t, 1), c = tuv(t, 2);
    const float a2 = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
    area += std::fabs(a2) * 0.5;
    if (std::fabs(a2) < 1e-9f) continue;
    const int x0 = std::max(0, (int)std::floor(std::min({a.x, b.x, c.x}) * G)), x1 = std::min(G - 1, (int)std::ceil(std::max({a.x, b.x, c.x}) * G));
    const int y0 = std::max(0, (int)std::floor(std::min({a.y, b.y, c.y}) * G)), y1 = std::min(G - 1, (int)std::ceil(std::max({a.y, b.y, c.y}) * G));
    for (int y = y0; y <= y1; y++)
      for (int x = x0; x <= x1; x++) {
        const float px = (x + 0.5f) / G, py = (y + 0.5f) / G;
        const float w0 = ((b.x - px) * (c.y - py) - (c.x - px) * (b.y - py)) / a2, w1 = ((c.x - px) * (a.y - py) - (a.x - px) * (c.y - py)) / a2,
                    w2 = 1.0f - w0 - w1;
        if (w0 < 0.02f || w1 < 0.02f || w2 < 0.02f) continue;
        int32_t &o = owner[(size_t)y * G + x];
        if (o >= 0 && o != (int32_t)t) overlap_cells++;
        o = (int32_t)t;
      }
  }
  if (overlap_cells) std::printf("    %s: %d grid cells hold two triangles\n", what, overlap_cells);
  bad += overlap_cells > 0;
  if (area > 1.0 + 1e-3) {
    std::printf("    %s: total chart area %.3f > 1\n", what, area);
    bad++;
  }
  if (!check_gap) return bad;
  /* Islands. */
  R35Islands isl;
  isl.parent.resize(nt);
  for (size_t t = 0; t < nt; t++) isl.parent[t] = (int)t;
  struct EdgeUse {
    size_t tri;
    Vec2 ua, ub;
  };
  std::map<std::pair<uint32_t, uint32_t>, std::vector<EdgeUse>> edges;
  for (size_t t = 0; t < nt; t++)
    for (int k = 0; k < 3; k++) {
      const uint32_t va = m.corner_verts[rm.tri_corner[t * 3 + (size_t)k]], vb = m.corner_verts[rm.tri_corner[t * 3 + (size_t)((k + 1) % 3)]];
      const Vec2 ua = tuv(t, k), ub = tuv(t, (k + 1) % 3);
      if (va < vb) edges[{va, vb}].push_back({t, ua, ub});
      else edges[{vb, va}].push_back({t, ub, ua});
    }
  auto same = [](Vec2 p, Vec2 q) { return std::fabs(p.x - q.x) < 1e-4f && std::fabs(p.y - q.y) < 1e-4f; };
  for (auto &kv : edges)
    for (size_t i = 0; i < kv.second.size(); i++)
      for (size_t j = i + 1; j < kv.second.size(); j++)
        if (same(kv.second[i].ua, kv.second[j].ua) && same(kv.second[i].ub, kv.second[j].ub))
          isl.parent[(size_t)isl.find((int)kv.second[i].tri)] = isl.find((int)kv.second[j].tri);
  /* Coverage per island on the res grid (any of 3x3 sub-samples inside a triangle): no cell within one cell of another island's. */
  const int R = res;
  std::vector<int32_t> cell((size_t)R * R, -1);
  int clash = 0;
  for (size_t t = 0; t < nt; t++) {
    const Vec2 a = tuv(t, 0), b = tuv(t, 1), c = tuv(t, 2);
    const float a2 = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
    if (std::fabs(a2) < 1e-9f) continue;
    const int id = isl.find((int)t);
    for (int y = 0; y < R; y++)
      for (int x = 0; x < R; x++) {
        bool in = false;
        for (int sy = 0; sy < 3 && !in; sy++)
          for (int sx = 0; sx < 3 && !in; sx++) {
            const float px = (x + (sx + 0.5f) / 3.0f) / R, py = (y + (sy + 0.5f) / 3.0f) / R;
            const float w0 = ((b.x - px) * (c.y - py) - (c.x - px) * (b.y - py)) / a2, w1 = ((c.x - px) * (a.y - py) - (a.x - px) * (c.y - py)) / a2,
                        w2 = 1.0f - w0 - w1;
            in = w0 >= 0 && w1 >= 0 && w2 >= 0;
          }
        if (!in) continue;
        int32_t &o = cell[(size_t)y * R + x];
        if (o >= 0 && o != id) clash++;
        o = id;
      }
  }
  for (int y = 0; y < R; y++)
    for (int x = 0; x < R; x++) {
      const int32_t o = cell[(size_t)y * R + x];
      if (o < 0) continue;
      for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
          const int xx = x + dx, yy = y + dy;
          if (xx < 0 || yy < 0 || xx >= R || yy >= R) continue;
          const int32_t q = cell[(size_t)yy * R + xx];
          if (q >= 0 && q != o) clash++;
        }
    }
  if (clash) std::printf("    %s: %d texel pairs of different charts touch (padding %d at %d texels)\n", what, clash, padding, res);
  return bad + (clash > 0);
}


/* ===================================================================== */
/* Round 36: voxel-based global illumination (task 0013)                   */
/* ===================================================================== */


struct R36D3 {
  double x, y, z;
};
inline R36D3 r36_d3(Vec3 v) { return {v.x, v.y, v.z}; }
inline R36D3 r36_sub(R36D3 a, R36D3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline R36D3 r36_cross(R36D3 a, R36D3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double r36_dot(R36D3 a, R36D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

/* Akenine-Moeller's triangle / box test, in double: the 13 separating axes (a touch counts as an overlap). */
inline bool r36_tri_box(Vec3 va, Vec3 vb, Vec3 vc, Vec3 box_min) {
  const R36D3 ctr = {box_min.x + 0.5, box_min.y + 0.5, box_min.z + 0.5};
  const R36D3 a = r36_sub(r36_d3(va), ctr), b = r36_sub(r36_d3(vb), ctr), c = r36_sub(r36_d3(vc), ctr);
  const R36D3 e[3] = {r36_sub(b, a), r36_sub(c, b), r36_sub(a, c)};
  const R36D3 unit[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  auto separated = [&](R36D3 ax) {
    if (r36_dot(ax, ax) < 1e-18) return false;  // a degenerate axis separates nothing
    const double p0 = r36_dot(a, ax), p1 = r36_dot(b, ax), p2 = r36_dot(c, ax);
    const double r = 0.5 * (std::fabs(ax.x) + std::fabs(ax.y) + std::fabs(ax.z));
    return std::min({p0, p1, p2}) > r || std::max({p0, p1, p2}) < -r;
  };
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      if (separated(r36_cross(e[i], unit[j]))) return false;
  for (int j = 0; j < 3; j++)
    if (separated(unit[j])) return false;
  return !separated(r36_cross(e[0], e[1]));
}

inline int r36_pop(uint64_t v) {
  int n = 0;
  for (; v; v &= v - 1) n++;
  return n;
}
inline int r36_count_set(const VoxelGrid &g) {
  int n = 0;
  for (const VoxelColumn &c : g.levels[0]) n += r36_pop(c.lo) + r36_pop(c.hi);
  return n;
}

/* A grid whose world space is its grid space: voxel 1, origin 0, n columns, 128 deep. */
inline VoxelGrid r36_unit_grid(int n = 64, float voxel = 1.0f) {
  VoxelGrid g;
  AABB b;
  b.add(Vec3(0.0f));
  b.add(Vec3((float)(n - 14) * voxel, 100.0f * voxel, (float)(n - 14) * voxel));
  voxel_grid_fit(g, b, n);
  CHECK(g.valid() && g.voxel == voxel && g.n == n);
  CHECK(g.origin.x == 0.0f && g.origin.y == 0.0f && g.origin.z == 0.0f);
  return g;
}

inline RenderMesh r36_tris(const std::vector<Vec3> &p) {
  RenderMesh rm;
  rm.positions = p;
  for (uint32_t i = 0; i < p.size(); i++) rm.indices.push_back(i);
  return rm;
}

inline std::vector<VoxelSpan> r36_voxelize(VoxelGrid &g, const RenderMesh &rm, const Mat4 &model = Mat4()) {
  std::vector<VoxelSpan> spans;
  voxelize_mesh(g, rm, model, spans);
  voxel_grid_build(g, {&spans});
  return spans;
}

inline Vec3 r36_dir(R34Rng &rng) {
  const float z = rng.range(-1.0f, 1.0f), phi = rng.range(0.0f, 6.2831853f), r = std::sqrt(std::max(0.0f, 1.0f - z * z));
  return Vec3(r * std::cos(phi), z, r * std::sin(phi));
}

/* Random bits: some columns hold one or two short runs. */
inline std::vector<VoxelSpan> r36_random_bits(const VoxelGrid &g, R34Rng &rng, float density) {
  std::vector<VoxelSpan> spans;
  for (int z = 0; z < g.n; z++)
    for (int x = 0; x < g.n; x++) {
      if (rng.f() > density) continue;
      for (int k = 0, runs = 1 + (rng.f() < 0.3f); k < runs; k++) {
        const int y0 = (int)(rng.f() * 127.99f), y1 = std::min(127, y0 + (int)(rng.f() * 6.0f));
        spans.push_back({(uint32_t)(z * g.n + x), (uint8_t)y0, (uint8_t)y1});
      }
    }
  return spans;
}
/* Random triangles inside the grid's box. */
inline std::vector<VoxelSpan> r36_random_tris(const VoxelGrid &g, R34Rng &rng, int count, float size) {
  std::vector<Vec3> p;
  const float ex = (float)g.n * g.voxel, ey = (float)VoxelGrid::kDepth * g.voxel;
  for (int i = 0; i < count; i++) {
    const Vec3 c(rng.range(0.1f, 0.9f) * ex, rng.range(0.1f, 0.9f) * ey, rng.range(0.1f, 0.9f) * ex);
    for (int k = 0; k < 3; k++)
      p.push_back(g.origin + c + Vec3(rng.range(-size, size), rng.range(-size, size), rng.range(-size, size)) * g.voxel);
  }
  std::vector<VoxelSpan> spans;
  voxelize_mesh(g, r36_tris(p), Mat4(), spans);
  return spans;
}

/* Does the ray (grid units) pass within 1e-4 of a voxel edge (two lattice coordinates at once) while it is
 * inside the grid's box, up to grid distance t_end? Those are the rays whose answer is a coin toss. */
inline bool r36_near_edge(const VoxelGrid &g, Vec3 world_o, Vec3 world_d, double t_end) {
  const Vec3 go = g.to_grid(world_o);
  const double O[3] = {go.x, go.y, go.z}, D[3] = {world_d.x, world_d.y, world_d.z};
  const double hi[3] = {(double)g.n, (double)VoxelGrid::kDepth, (double)g.n};
  double t0 = 0.0, t1 = t_end;
  for (int a = 0; a < 3; a++) {
    if (std::fabs(D[a]) < 1e-12) {
      if (O[a] < 0.0 || O[a] > hi[a]) return false;
      continue;
    }
    double ta = (0.0 - O[a]) / D[a], tb = (hi[a] - O[a]) / D[a];
    if (ta > tb) std::swap(ta, tb);
    t0 = std::max(t0, ta);
    t1 = std::min(t1, tb);
  }
  if (!(t0 < t1)) return false;
  for (int a = 0; a < 3; a++) {
    if (std::fabs(D[a]) < 1e-9) continue;
    const double pa = O[a] + D[a] * t0, pb = O[a] + D[a] * t1;
    for (double k = std::ceil(std::min(pa, pb)); k <= std::max(pa, pb); k += 1.0) {
      const double t = (k - O[a]) / D[a];
      for (int b = 0; b < 3; b++) {
        if (b == a) continue;
        const double p = O[b] + D[b] * t;
        if (std::fabs(p - std::round(p)) < 1e-4) return true;
      }
    }
  }
  return false;
}

struct R36RayStats {
  int rays = 0, hits = 0, hier_vs_columns = 0, vs_dda = 0, grazing = 0, hard = 0, t_off = 0;
};
/* Shoots `count` random rays (and a few axis-parallel ones) at the grid with all three traces. */
inline R36RayStats r36_compare_traces(const VoxelGrid &g, R34Rng &rng, int count) {
  R36RayStats s;
  const float ex = (float)g.n * g.voxel, ey = (float)VoxelGrid::kDepth * g.voxel, m = 10.0f * g.voxel;
  for (int i = 0; i < count; i++) {
    const Vec3 o = g.origin + Vec3(rng.range(-m, ex + m), rng.range(-m, ey + m), rng.range(-m, ex + m));
    Vec3 d = r36_dir(rng);
    if (i % 50 == 0) d = Vec3(0.0f), d[i / 50 % 3] = (i / 150) % 2 ? -1.0f : 1.0f;  // axis-parallel: exact boundaries
    const float tmax = rng.range(2.0f, 120.0f) * g.voxel;
    VoxelHit a, b, c;
    const bool ha = voxel_trace(g, o, d, tmax, a), hb = voxel_trace_columns(g, o, d, tmax, b), hc = voxel_trace_dda(g, o, d, tmax, c);
    s.rays++;
    s.hits += hb;
    if (ha != hb || (ha && (a.x != b.x || a.y != b.y || a.z != b.z || a.t != b.t))) s.hier_vs_columns++;
    if (hb != hc || (hb && (b.x != c.x || b.y != c.y || b.z != c.z))) {
      /* A mismatch is allowed only for a ray that grazes a voxel edge on its way to the hit. */
      double t_end = (double)tmax / g.voxel;
      if (hb || hc) t_end = std::min(t_end, std::max(hb ? b.t : 0.0f, hc ? c.t : 0.0f) / (double)g.voxel + 2.0);
      if (r36_near_edge(g, o, d, t_end)) s.grazing++;
      else {
        s.hard++;
        if (s.hard <= 3)
          std::printf("    ray %d: o (%g %g %g) d (%g %g %g) tmax %g: columns %d (%d %d %d t %g), dda %d (%d %d %d t %g)\n", i, o.x, o.y, o.z, d.x, d.y, d.z,
                      tmax, hb, b.x, b.y, b.z, b.t, hc, c.x, c.y, c.z, c.t);
      }
      s.vs_dda++;
    }
    else if (hb && std::fabs(b.t - c.t) > 2e-3f * g.voxel + 1e-4f * b.t) s.t_off++;
  }
  return s;
}

/* ---- a scene for the gather and image tests: items, world, grid, RSM ---- */
struct R36Sc {
  CfScene sc;
  Environment env;
  VoxelGrid grid;
  Rsm rsm;
  uint32_t next_id = 1;
  R36Sc() {
    env.mode = Environment::Color;
    env.color = Vec3(0.4f);
    env.strength = 1.0f;
  }
  void add(MeshPtr m, const Mat4 &model, Vec3 colour) { sc.add(m, model, next_id++, make_material("m", colour)); }
  AABB bounds() const {
    AABB b;
    for (const DrawItem &it : sc.items) b.add(it.mesh->bounds.transformed(it.model));
    return b;
  }
  /* Voxelizes everything into a grid of n columns; renders the RSM when there is a sun. */
  void finish(int n, const RenderLight *sun, int rsm_res = 256) {
    sc.seal();
    const AABB b = bounds();
    voxel_grid_fit(grid, b, n);
    std::vector<std::vector<VoxelSpan>> spans(sc.items.size());
    std::vector<const std::vector<VoxelSpan> *> ptrs;
    for (size_t i = 0; i < sc.items.size(); i++) {
      voxelize_mesh(grid, *sc.items[i].mesh, sc.items[i].model, spans[i]);
      ptrs.push_back(&spans[i]);
    }
    voxel_grid_build(grid, ptrs);
    rsm = Rsm{};
    if (sun) render_rsm(rsm, sc.items, *sun, b, rsm_res);
  }
};

inline RenderLight r36_sun(Vec3 dir, float intensity = 0.7f) {
  RenderLight s;
  s.direction = normalize(dir);
  s.intensity = intensity;
  s.shadows = false;
  return s;
}

/* The bounce and sky summed over the 16 ray sets, for a receiver on the floor at p (normal up). */
inline GiSample r36_gather_all(const R36Sc &s, const Rsm *rsm, Vec3 p, const GiParams &prm, Vec3 n = Vec3(0, 1, 0)) {
  GiSample sum;
  sum.sky = 0.0f;
  for (int set = 0; set < 16; set++) {
    const GiSample g = voxel_gi_gather(s.grid, rsm, s.env, p, n, prm, set);
    sum.bounce += g.bounce;
    sum.sky += g.sky / 16.0f;
  }
  return sum;
}

inline bool r36_pix(const Mat4 &v, const Mat4 &p, int W, int H, Vec3 w, int &x, int &y) {
  const Vec4 c = (p * v) * Vec4(w, 1.0f);
  if (!(c.w > 0.0f)) return false;
  x = (int)((c.x / c.w * 0.5f + 0.5f) * W);
  y = (int)((0.5f - c.y / c.w * 0.5f) * H);
  return x >= 3 && y >= 3 && x < W - 3 && y < H - 3;
}
/* Mean channel values (0..255) of the 5 x 5 pixels around a world point; false when it is off screen. */
inline bool r36_probe(const Image &img, const Mat4 &v, const Mat4 &p, Vec3 w, double out[3]) {
  int x, y;
  if (!r36_pix(v, p, img.width, img.height, w, x, y)) return false;
  out[0] = out[1] = out[2] = 0.0;
  for (int dy = -2; dy <= 2; dy++)
    for (int dx = -2; dx <= 2; dx++) {
      const uint32_t c = img.pixels[(size_t)(y + dy) * img.width + x + dx];
      out[0] += (c >> 16) & 255, out[1] += (c >> 8) & 255, out[2] += c & 255;
    }
  for (int k = 0; k < 3; k++) out[k] /= 25.0;
  return true;
}
/* The 3 x 3 pixels around a world point, bit for bit. */
inline bool r36_same_block(const Image &a, const Image &b, const Mat4 &v, const Mat4 &p, Vec3 w) {
  int x, y;
  if (!r36_pix(v, p, a.width, a.height, w, x, y)) return false;
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++)
      if (a.pixels[(size_t)(y + dy) * a.width + x + dx] != b.pixels[(size_t)(y + dy) * b.width + x + dx]) return false;
  return true;
}

struct R36View {
  Mat4 v, p;
  Vec3 eye;
  int W = 240, H = 180;
  R36View(Vec3 e, Vec3 target) : eye(e) {
    v = Mat4::look_at(e, target, {0, 1, 0});
    p = Mat4::perspective(55 * kDeg2Rad, W / (float)H, 0.1f, 80.0f);
  }
};

inline void r36_render(Image &img, const R36Sc &s, const R36View &view, const RenderLight *sun, const VoxelGIFrame *gi) {
  img.resize(view.W, view.H);
  RenderTarget rt;
  rt.attach(img, {0, 0, view.W, view.H});
  RasterOptions opt;
  opt.shade = ShadeMode::Deferred;
  LightingEnv env;
  env.environment = &s.env;
  env.camera_pos = view.eye;
  if (sun) env.lights.push_back(*sun);
  env.gi = gi;
  Renderer3D r3d;
  r3d.begin(&rt, view.v, view.p, env, opt);
  r3d.clear(0xFF204060u);
  for (const DrawItem &it : s.sc.items) r3d.add(it);
  r3d.flush();
}

inline VoxelGIFrame r36_frame(const R36Sc &s, int rays = 16, float radius = 2.0f, int downsample = 2) {
  VoxelGIFrame f;
  f.grid = &s.grid;
  f.rsm = s.rsm.valid() ? &s.rsm : nullptr;
  GiParams p;
  p.rays = rays;
  p.radius = radius;
  p.downsample = downsample;
  f.params = gi_params_sanitized(p);
  return f;
}

/* A floor with a red (or other) wall beside it at x = dx, the sun on the wall's -x face; optionally a slab high above
 * that keeps the sun off the wall. */
inline void r36_wall_scene(R36Sc &s, Vec3 wall_colour, bool slab_over_wall, RenderLight &sun, float dx) {
  sun = r36_sun(Vec3(1.0f, -1.2f, 0.3f));
  s.add(primitives::plane(20.0f, 2), Mat4(), Vec3(0.7f));
  s.add(primitives::cube(), Mat4::translate({dx, 2, 0}) * Mat4::scale({0.2f, 4, 8}), wall_colour);
  if (slab_over_wall) s.add(primitives::cube(), Mat4::translate({dx - 3.5f, 6.2f, 0}) * Mat4::scale({9, 0.4f, 16}), Vec3(0.7f));
  s.finish(128, &sun);
}

/* Mean (GI on - GI off) over the floor beside the wall, per channel (0..255 steps). */
inline bool r36_bleed(const R36Sc &s, const RenderLight &sun, double d[3], float dx) {
  const R36View view(Vec3(-6 + dx, 5, 9), Vec3(-1 + dx, 0, 0));
  Image on, off;
  const VoxelGIFrame fr = r36_frame(s);
  r36_render(on, s, view, &sun, &fr);
  r36_render(off, s, view, &sun, nullptr);
  d[0] = d[1] = d[2] = 0.0;
  int n = 0;
  for (float z : {-1.5f, 0.0f, 1.5f}) {
    double a[3], b[3];
    if (!r36_probe(on, view.v, view.p, Vec3(dx - 0.6f, 0, z), a) || !r36_probe(off, view.v, view.p, Vec3(dx - 0.6f, 0, z), b)) continue;
    for (int k = 0; k < 3; k++) d[k] += a[k] - b[k];
    n++;
  }
  for (int k = 0; k < 3; k++) d[k] /= std::max(1, n);
  return n == 3;
}

/* ---- editor scenes ---- */

/* The Game view looks straight down at a floor that lies to the wall's -x side, so a whole-picture
 * average is the floor beside the wall. The wall stands at x = dx: the world origin is not special, but see the
 * "same wherever the scene sits" test for why these scenes keep away from it. */
inline void r36_editor_scene(Editor &ed, Vec3 wall_colour = Vec3(0.9f, 0.1f, 0.1f), float dx = 6.0f) {
  r35_open(ed);
  r35_add(ed, "Plane", "Floor", {dx - 5.1f, 0, 0}, {1, 1, 1}, Vec3(0.8f));
  r35_add(ed, "Cube", "Wall", {dx, 1, 0}, {0.2f, 2, 6}, wall_colour);
  GameObject *cam = ed.scene().find_by_name("Main Camera");
  cam->set_local_position({dx - 2.5f, 8, 0});
  cam->set_local_euler({90, 0, 0});
  ed.commit_change("r36 scene");
  r34_steps(ed, 3);
}

/* The 5 x 5 pixels around a world point in the Game view's picture (a crop from r35_game), mean R G B. */
inline bool r36_game_probe(Editor &ed, const std::vector<uint32_t> &crop, Vec3 w, double out[3]) {
  const Recti r = ed.window_rect_for_test(WindowKind::Game);
  GameObject *go = ed.scene().find_by_name("Main Camera");
  Camera *cam = go ? go->get<Camera>() : nullptr;
  if (!cam || r.w < 8 || r.h < 8 || crop.size() < (size_t)r.w * r.h) return false;
  const Quat q = go->world_rotation();
  const Vec3 eye = go->world_position();
  const Mat4 v = Mat4::look_at(eye, eye + q.rotate({0, 0, 1}), q.rotate({0, 1, 0})), p = cam->projection(r.w / (float)r.h);
  int x, y;
  if (!r36_pix(v, p, r.w, r.h, w, x, y)) return false;
  out[0] = out[1] = out[2] = 0.0;
  for (int dy = -2; dy <= 2; dy++)
    for (int dx = -2; dx <= 2; dx++) {
      const uint32_t c = crop[(size_t)(y + dy) * r.w + x + dx];
      out[0] += (c >> 16) & 255, out[1] += (c >> 8) & 255, out[2] += c & 255;
    }
  for (int k = 0; k < 3; k++) out[k] /= 25.0;
  return true;
}

struct R36Mean {
  double r = 0, g = 0, b = 0;
};
/* The mean per-channel difference a - b over the picture. */
inline R36Mean r36_mean_diff(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b) {
  R36Mean m;
  if (a.size() != b.size() || a.empty()) return m;
  for (size_t i = 0; i < a.size(); i++) {
    m.r += (int)((a[i] >> 16) & 255) - (int)((b[i] >> 16) & 255);
    m.g += (int)((a[i] >> 8) & 255) - (int)((b[i] >> 8) & 255);
    m.b += (int)(a[i] & 255) - (int)(b[i] & 255);
  }
  m.r /= a.size(), m.g /= a.size(), m.b /= a.size();
  return m;
}

/* A console command and three frames, nothing else. */
inline void r36_raw(Editor &ed, const char *c) {
  ed.command(c);
  r34_steps(ed, 3);
}
/* A console command and a few frames. (It used to rotate the world too, to work around the Lighting
 * settings missing from the views' cache key; that was fixed, and a rotated world rightly makes a bake
 * out of date.) */
inline void r36_cmd(Editor &ed, const char *c) {
  ed.command(c);
  r34_steps(ed, 3);
}



/* ===================================================================== */
/* Round 37: probe volumes (task 0014)                                     */
/* ===================================================================== */


/* A path tracer over a few meshes; the meshes and materials live as long as it does. */
struct P37Pt {
  std::vector<MeshPtr> meshes;
  std::deque<std::vector<MaterialPtr>> mats;
  std::vector<PTObject> objs;
  PathTracer pt;
  void add(MeshPtr m, const Mat4 &model, Vec3 colour) {
    meshes.push_back(m);
    mats.push_back({make_material("m", colour)});
    objs.push_back({&m->render_mesh_tangents(), model, &mats.back()});
  }
  void build(const std::vector<RenderLight> &lights, const Environment &env, int bounces = 2) {
    PTSettings ps;
    ps.max_bounces = bounces;
    ps.denoise = false;
    ps.use_guiding = false;
    ps.gpus.clear();
    pt.set_settings(ps);
    pt.build(objs, lights, env);
  }
  std::vector<AABB> tri_boxes() const {
    std::vector<AABB> out;
    for (const PTObject &o : objs)
      for (size_t k = 0; k < o.mesh->tri_count(); k++) {
        AABB b;
        for (int c = 0; c < 3; c++) b.add(o.model.point(o.mesh->positions[o.mesh->indices[k * 3 + c]]));
        out.push_back(b);
      }
    return out;
  }
};

inline AABB p37_box(Vec3 a, Vec3 b) {
  AABB r;
  r.add(a);
  r.add(b);
  return r;
}

inline RenderLight p37_sun(Vec3 travel, float intensity = 1.0f) {
  RenderLight s;
  s.direction = normalize(travel);
  s.intensity = intensity;
  return s;
}

inline bool p37_same_cube(const ProbeCube &a, const ProbeCube &b) {
  for (int i = 0; i < 6; i++)
    if (a.bounce[i].x != b.bounce[i].x || a.bounce[i].y != b.bounce[i].y || a.bounce[i].z != b.bounce[i].z || a.sky[i] != b.sky[i]) return false;
  return true;
}

inline bool p37_same_probes(const ProbeVolumeData &a, const ProbeVolumeData &b) {
  if (a.position.size() != b.position.size() || a.bricks.size() != b.bricks.size() || a.nodes.size() != b.nodes.size() || a.roots != b.roots ||
      a.valid != b.valid || a.scene_key != b.scene_key)
    return false;
  for (size_t i = 0; i < a.position.size(); i++)
    if (a.position[i].x != b.position[i].x || a.position[i].y != b.position[i].y || a.position[i].z != b.position[i].z) return false;
  for (size_t i = 0; i < a.baked.size(); i++)
    if (!p37_same_cube(a.baked[i], b.baked[i])) return false;
  for (size_t i = 0; i < a.bricks.size(); i++)
    if (a.bricks[i].min.x != b.bricks[i].min.x || a.bricks[i].min.y != b.bricks[i].min.y || a.bricks[i].min.z != b.bricks[i].min.z ||
        a.bricks[i].spacing != b.bricks[i].spacing || a.bricks[i].first != b.bricks[i].first)
      return false;
  return true;
}

/* One brick of 4 x 4 x 4 probes, `spacing` apart, from the origin (probe i at its grid position). */
inline ProbeVolumeData p37_one_brick(float spacing = 1.0f) {
  ProbeVolumeData d;
  probe_place(d, p37_box(Vec3(0.0f), Vec3(3.0f * spacing)), {}, spacing, spacing);
  return d;
}

/* ---- editor scenes ---- */

struct P37World {
  GameObject *floor_go = nullptr, *wall = nullptr, *ball = nullptr, *box = nullptr, *probes = nullptr;
};

/* A floor and a red wall (x = 2.2) that contribute to the lightmaps, a dynamic ball beside the wall, an
 * optional lightmapped box, the sun in `sun_mode`, and (optionally) a local probe volume over all of it. */
inline P37World p37_world(Editor &ed, bool volume, int sun_mode = 1, float ball_x = 1.0f, bool with_box = false, Vec3 wall_colour = Vec3(0.9f, 0.05f, 0.05f)) {
  r35_open(ed);
  P37World w;
  w.floor_go = r35_add(ed, "Plane", "Floor", {0, 0, 0}, {1, 1, 1}, Vec3(0.8f));
  w.wall = r35_add(ed, "Cube", "Wall", {2.2f, 1, 0}, {0.2f, 2, 6}, wall_colour);
  w.ball = r35_add(ed, "Sphere", "Ball", {ball_x, 0.5f, 0}, {1, 1, 1}, Vec3(0.8f), false);
  if (with_box) w.box = r35_add(ed, "Cube", "Box", {-1.5f, 0.5f, 0}, {1, 1, 1}, Vec3(0.8f));
  r35_sun_mode(ed, sun_mode);
  if (volume) {
    w.probes = ed.scene().create("Probes");
    ProbeVolume *pv = w.probes->add<ProbeVolume>();
    pv->global = false;
    pv->size = Vec3(10, 4, 10);
    w.probes->set_local_position({0, 1.5f, 0});
    ed.commit_change("probe volume");
  }
  r34_steps(ed, 2);
  return w;
}

template <class F>
inline int p37_until(Editor &ed, int max_frames, F pred) {
  for (int i = 0; i < max_frames; i++) {
    if (pred()) return i;
    ed.step_frame_headless();
  }
  return pred() ? max_frames : -1;
}

/* Realtime GI on and the live probes caught up. */
inline bool p37_live_settled(Editor &ed, int max_frames = 80) {
  return p37_until(ed, max_frames, [&] { return ed.probe_live_ready_for_test() && ed.probe_live_left_for_test() == 0; }) >= 0;
}

/* Sum over all pixels of (A - B) in red minus blue: how much redder A is than B. */
inline double p37_redder(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b) {
  double s = 0;
  for (size_t i = 0; i < a.size() && i < b.size(); i++)
    s += (int)((a[i] >> 16) & 255) - (int)((b[i] >> 16) & 255) - ((int)(a[i] & 255) - (int)(b[i] & 255));
  return s;
}

/* Where an object is in the Game view: the pixels that change when it is hidden, with the sun off (so no
 * shadow of it counts). */
inline std::vector<char> p37_footprint(Editor &ed, GameObject *g) {
  Light *sun = ed.scene().find_by_name("Directional Light")->get<Light>();
  sun->enabled = false;
  const Vec3 sky = ed.scene().environment.color;
  std::vector<char> m;
  /* Twice, against two sky colours: a ball lit by a grey sky can match a grey background by chance. */
  for (const Vec3 bg : {sky, Vec3(0.95f)}) {
    ed.scene().environment.color = bg;
    ed.commit_change("footprint");
    r34_steps(ed, 3);
    const std::vector<uint32_t> with = r35_game(ed);
    g->active = false;
    ed.commit_change("hide");
    r34_steps(ed, 3);
    const std::vector<uint32_t> without = r35_game(ed);
    g->active = true;
    ed.commit_change("show");
    r34_steps(ed, 1);
    if (m.empty()) m.assign(with.size(), 0);
    for (size_t i = 0; i < with.size() && i < without.size(); i++) {
      const int d = std::max({std::abs((int)((with[i] >> 16) & 255) - (int)((without[i] >> 16) & 255)), std::abs((int)((with[i] >> 8) & 255) - (int)((without[i] >> 8) & 255)),
                              std::abs((int)(with[i] & 255) - (int)(without[i] & 255))});
      if (d > 2) m[i] = 1;
    }
  }
  ed.scene().environment.color = sky;
  sun->enabled = true;
  ed.commit_change("footprint done");
  r34_steps(ed, 3);
  return m;
}

inline size_t p37_count(const std::vector<char> &m) {
  size_t n = 0;
  for (char c : m) n += c != 0;
  return n;
}

/* Mean luma (sum of channels / 3) and red-minus-blue over the masked pixels. */
struct P37Mean {
  double luma = 0, red_minus_blue = 0;
};
inline P37Mean p37_mean(const std::vector<uint32_t> &px, const std::vector<char> &mask) {
  P37Mean r;
  size_t n = 0;
  for (size_t i = 0; i < px.size() && i < mask.size(); i++) {
    if (!mask[i]) continue;
    const int R = (px[i] >> 16) & 255, G = (px[i] >> 8) & 255, B = px[i] & 255;
    r.luma += (R + G + B) / 3.0;
    r.red_minus_blue += R - B;
    n++;
  }
  if (n) r.luma /= n, r.red_minus_blue /= n;
  return r;
}

/* Pixels that differ (by more than tol) inside / outside a mask. */
inline size_t p37_diff_in(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b, const std::vector<char> &mask, bool inside, int tol = 0) {
  size_t n = 0;
  for (size_t i = 0; i < a.size() && i < b.size() && i < mask.size(); i++) {
    if ((mask[i] != 0) != inside) continue;
    const int dr = std::abs((int)((a[i] >> 16) & 255) - (int)((b[i] >> 16) & 255)), dg = std::abs((int)((a[i] >> 8) & 255) - (int)((b[i] >> 8) & 255)),
              db = std::abs((int)(a[i] & 255) - (int)(b[i] & 255));
    if (std::max({dr, dg, db}) > tol) n++;
  }
  return n;
}

inline std::vector<char> p37_read(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  return std::vector<char>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
inline void p37_write(const std::string &path, const std::vector<char> &bytes) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  f.write(bytes.data(), (std::streamsize)bytes.size());
}

/* A fresh editor opens a scene file as a file dropped on the window (what the Project window does). */
inline void p37_open_file(Editor &ed, const std::string &path) {
  r34_open(ed);
  platform::Event drop;
  drop.type = platform::EventType::Drop;
  drop.paths = {path};
  ed.step_frame_headless({drop});
  r34_steps(ed, 6);
}



/* Round 47 (task 0025, ADR 0016): the Active Camera, and filters wherever a camera's picture shows. */
inline int r47_dark(const Image &img, const Recti &r) {  // pixels near black (an outline's colour)
  int n = 0;
  for (int y = r.y; y < r.bottom() && y < img.height; y++)
    for (int x = r.x; x < r.right() && x < img.width; x++) {
      const uint32_t c = img.pixels[(size_t)y * img.width + x];
      if (((c >> 16) & 255) < 24 && ((c >> 8) & 255) < 24 && (c & 255) < 24) n++;
    }
  return n;
}
inline double r47_mean(const std::vector<uint32_t> &px) {
  double s = 0;
  for (uint32_t c : px) s += ((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255);
  return px.empty() ? 0 : s / (3.0 * px.size());
}
inline double r47_detail(const Image &img) {  // high-frequency energy: mean |difference| between horizontal neighbours
  double s = 0;
  size_t n = 0;
  for (int y = 0; y < img.height; y++)
    for (int x = 1; x < img.width; x++) {
      const uint32_t a = img.pixels[(size_t)y * img.width + x], b = img.pixels[(size_t)y * img.width + x - 1];
      s += std::abs((int)((a >> 8) & 255) - (int)((b >> 8) & 255));
      n++;
    }
  return n ? s / n : 0;
}

/* Round 49 (task 0027, ADR 0017): procedural texture assets. */
/* Sets one of a texture's fields by its reflected name (a layer's as "L0 Rows"), from text: what `settex` does. */
struct R49Set : Reflector {
  std::string key, value;
  bool done = false;
  bool all_fields() const override { return true; }
  bool is(const char *n) const { return !done && key == n; }
  void field(const char *n, float &v, float, float, float) override { if (is(n)) v = (float)std::atof(value.c_str()), done = true; }
  void field(const char *n, int &v, int, int) override { if (is(n)) v = std::atoi(value.c_str()), done = true; }
  void field(const char *n, bool &v) override { if (is(n)) v = value != "0" && value != "false", done = true; }
  void field(const char *n, Vec3 &v) override {
    if (!is(n)) return;
    std::sscanf(value.c_str(), "%f %f %f", &v.x, &v.y, &v.z);
    done = true;
  }
  void color(const char *n, Vec3 &v) override { field(n, v); }
  void enumeration(const char *n, int &v, const char *const *, int) override { field(n, v, 0, 0); }
  void text(const char *n, std::string &v) override { if (is(n)) v = value, done = true; }
  void mesh(const char *, MeshPtr &) override {}
};
inline bool r49_set(ProceduralTexture &t, const char *key, const char *value) {
  R49Set r;
  r.key = key;
  r.value = value;
  t.reflect(r);
  if (r.done) t.touch();
  return r.done;
}
inline ProceduralTexture r49_tex(const char *layer, int size = 64) {
  ProceduralTexture t;
  t.name = layer;
  t.width = t.height = size;
  t.add(layer);
  return t;
}
/* The biggest step between neighbouring texels inside the tile, and across its seam (left / right, top / bottom). */
inline void r49_steps(const TexPlanes &p, double &interior, double &seam) {
  interior = seam = 0;
  auto d = [&](size_t a, size_t b) {
    const Vec3 c = p.albedo[a] - p.albedo[b];
    return std::max({std::fabs(p.height[a] - p.height[b]), std::fabs(c.x), std::fabs(c.y), std::fabs(c.z)});
  };
  for (int y = 0; y < p.h; y++)
    for (int x = 0; x < p.w; x++) {
      const size_t i = (size_t)y * p.w + x;
      if (x + 1 < p.w) interior = std::max(interior, (double)d(i, i + 1));
      if (y + 1 < p.h) interior = std::max(interior, (double)d(i, i + p.w));
      if (x == 0) seam = std::max(seam, (double)d(i, i + p.w - 1));
      if (y == 0) seam = std::max(seam, (double)d(i, i + (size_t)(p.h - 1) * p.w));
    }
}

/* Round 50 (task 0028): the Textures window. */
inline Recti r50_popup(Editor &ed) {
  const auto &rects = ed.ui_for_test().popup_rects();
  return rects.empty() ? Recti{0, 0, 0, 0} : rects.back();
}
inline void r50_pick_row(Editor &ed, const Recti &pop, int row_index) {
  const int row = ed.ui_for_test().row_h();
  r34_click(ed, pop.x + pop.w / 2, pop.y + ed.ui_for_test().px(4) + row * row_index + row / 2);
  r34_steps(ed, 2);
}
inline void r50_open(Editor &ed) {
  /* Earlier runs' textures out of the scratch project (the grid would scroll them out of view). */
  const std::string dir = fs::join(scratch_project(), "Assets/Textures");
  for (const DirEntry &de : fs::list(dir))
    if (fs::extension(de.name) == ".ptex") std::remove(fs::join(dir, de.name).c_str());
  ed.init_headless(1600, 1200);
  ed.step_frame_headless();
  ed.set_render_cache_verify(false);
  ed.command("shading wire");  // the window is under test: a cheap Scene view keeps the frames fast
  ed.command("window Textures");
  r34_steps(ed, 2);
  /* The right half of the Scene view's area: tall enough for the stack, the Scene view still beside it (where it
   * opens by itself, beside the Materials window, the short bottom area scrolls the stack out of view). */
  ed.dock_split_for_test(WindowKind::Textures, 2);
  r34_steps(ed, 3);
}

/* Round 51 (task 0029, ADR 0018): axis materials. */
struct R51Mats {
  MaterialPtr grass, dirt, rock, axis;
};
/* Grass (green), Dirt (brown) and Rock (grey) as material assets, and an axis material using them. */
inline R51Mats r51_mats(int mode = 1) {
  R51Mats m;
  Material g;
  g.name = "Grass";
  g.base_color = {0.1f, 0.8f, 0.1f};
  Material d;
  d.name = "Dirt";
  d.base_color = {0.5f, 0.3f, 0.1f};
  Material r;
  r.name = "Rock";
  r.base_color = {0.4f, 0.4f, 0.4f};
  m.grass = create_material_asset(g, "Assets/Materials/R51/Grass.mat");
  m.dirt = create_material_asset(d, "Assets/Materials/R51/Dirt.mat");
  m.rock = create_material_asset(r, "Assets/Materials/R51/Rock.mat");
  m.axis = make_material("Ground", Vec3(1.0f));
  m.axis->axis_mode = mode;
  m.axis->axis_top = "Assets/Materials/R51/Grass.mat";
  m.axis->axis_side = "Assets/Materials/R51/Dirt.mat";
  m.axis->axis_bottom = "Assets/Materials/R51/Rock.mat";
  m.axis->touch();
  return m;
}
inline SurfaceSample r51_eval(const Material &m, Vec3 world_n, Vec3 local_n, Vec3 pos = Vec3(0.3f, 0.2f, 0.1f)) {
  SurfacePoint sp;
  sp.position = sp.local_position = pos;
  sp.normal = sp.geo_normal = normalize(world_n);
  sp.local_normal = normalize(local_n);
  return evaluate_material(m, m.textures(), sp);
}
inline bool r51_green(Vec3 c) { return c.y > 0.6f && c.x < 0.3f; }
inline bool r51_brown(Vec3 c) { return c.x > 0.4f && c.y > 0.2f && c.y < 0.4f && c.z < 0.2f; }

/* ===================================================================== */
/* Round 52 (task 0030): large area lights, the gizmo after Set Origin,   */
/* Select Coplanar                                                         */
/* ===================================================================== */

/* A 10 x 10 m area light 3.5 m up, facing down (the user's report). */
inline RenderLight r52_big_light(bool disk = false, float w = 10.0f, float h = 10.0f) {
  RenderLight l;
  l.type = RenderLight::Area;
  l.position = {0, 3.5f, 0};
  l.direction = {0, -1, 0};
  l.right = {1, 0, 0};
  l.width = w;
  l.height = h;
  l.disk = disk;
  l.intensity = 2.0f;
  l.range = 10.0f;
  return l;
}

/* The path tracer's estimate without shadows, converged: the integral of max(0, n.L) x area_light_falloff (the
 * facing, the inverse square, the Range window: task 0155) over the light - its area times the mean over uniform
 * random points. */
inline double r52_reference(const RenderLight &l, Vec3 p, Vec3 n, int samples = 1000000) {
  uint32_t s = 12345;
  auto rnd = [&] {
    s = s * 1664525u + 1013904223u;
    return (s >> 8) / 16777216.0f;
  };
  double sum = 0;
  for (int i = 0; i < samples; i++) {
    float a = rnd() - 0.5f, b = rnd() - 0.5f;
    if (l.disk) {
      const float rr = 0.5f * std::sqrt(rnd()), ph = 2 * kPi * rnd();
      a = rr * std::cos(ph);
      b = rr * std::sin(ph);
    }
    const Vec3 up = cross(l.direction, l.right);
    const Vec3 q = l.position + l.right * (a * l.width) + up * (b * l.height);
    const Vec3 L = normalize(q - p);
    const float ndl = dot(n, L);
    if (ndl > 0) sum += ndl * area_light_falloff(l, q, p);
  }
  return sum / samples * area_light_area(l);
}


/* ===================================================================== */
/* Round 57 (task 0035): transparent backgrounds in renders               */
/* ===================================================================== */

/* The default scene's Cube alone (no floor), rendered at 160 x 90 with the engine given. */
inline const Image &r57_render(Editor &ed, const char *engine, bool transparent) {
  ed.command(std::string("set Render.RenderEngine ") + engine);
  ed.command(std::string("set Render.Transparent ") + (transparent ? "true" : "false"));
  ed.command("render");
  for (int i = 0; i < 4000 && ed.rendering_for_test(); i++) ed.step_frame_headless();
  return ed.render_image_for_test();
}
inline void r57_open(Editor &ed) {
  ed.init_headless(900, 600);
  ed.step_frame_headless();
  for (const char *n : {"Plane", "Sphere", "Cylinder", "Utah Teapot"})
    if (GameObject *g = ed.scene().find_by_name(n)) g->active = false;
  ed.command("set Render.ResolutionX 160");
  ed.command("set Render.ResolutionY 90");
  ed.command("set Render.Samples 16");
  ed.command("set Render.Denoise false");
  ed.command("device cpu");
}
inline int r57_alpha(const Image &img, int x, int y) { return (int)(img.pixels[(size_t)y * img.width + x] >> 24); }


/* ===================================================================== */
/* Round 60 (task 0038, ADR 0020): toon shading and outlines              */
/* ===================================================================== */

/* The round 57 scene with the Cube's mesh a sphere, a black world (no ambient: only the sun shades it) and a Toon
 * material with `bands` tones, no rim, no highlight. Returns the sphere's object. */
inline GameObject *r60_open(Editor &ed, int bands, bool toon = true) {
  r57_open(ed);
  ed.scene().environment.mode = 3;
  ed.scene().environment.color = Vec3(0.0f);
  GameObject *g = ed.scene().find_by_name("Cube");
  if (!g) return nullptr;
  g->get<MeshFilter>()->mesh = primitives::uv_sphere(2.5f, 96, 48);  // big in the frame: anti-aliased edges are few of its pixels
  MaterialPtr m = make_material_preset(toon ? "Toon" : "Solid");
  m->base_color = Vec3(0.9f, 0.6f, 0.5f);
  m->toon_bands = bands;
  m->toon_softness = 0.0f;
  m->toon_rim_width = 0.0f;
  m->toon_specular_size = 0.0f;
  g->get<MeshRenderer>()->materials = {m};
  g->mark_dirty();
  ed.mark_changed_for_test("R60 setup");
  ed.step_frame_headless();
  return ed.scene().find_by_name("Cube");
}
/* The solid pixels' colours (a transparent render: alpha 255 is the object), most common first, with their counts. */
inline std::vector<std::pair<uint32_t, int>> r60_colours(const Image &img, int *solid = nullptr) {
  std::map<uint32_t, int> h;
  int n = 0;
  for (uint32_t c : img.pixels)
    if ((c >> 24) == 255) h[c & 0xFFFFFF]++, n++;
  std::vector<std::pair<uint32_t, int>> v(h.begin(), h.end());
  std::sort(v.begin(), v.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
  if (solid) *solid = n;
  return v;
}
inline float r60_lum(uint32_t c) { return ((c >> 16 & 255) * 0.2126f + (c >> 8 & 255) * 0.7152f + (c & 255) * 0.0722f) / 255.0f; }


/* ===================================================================== */
/* Round 63 (task 0041): deforming modifiers move positions in place       */
/* ===================================================================== */

/* got (a deformed, in-place mesh) against ref (the same stack evaluated in full): the positions bit for
 * bit, and per mesh corner the render vertex's position bit for bit and normal within max_dn. */
inline bool r63_same(const Mesh &got, const Mesh &ref, float *max_dn) {
  if (got.positions.size() != ref.positions.size() || got.corner_count() != ref.corner_count()) return false;
  bool ok = std::memcmp(got.positions.data(), ref.positions.data(), got.positions.size() * sizeof(Vec3)) == 0;
  const RenderMesh &a = got.render_mesh(), &b = ref.render_mesh();
  std::vector<uint32_t> ca(got.corner_count(), UINT32_MAX), cb(ref.corner_count(), UINT32_MAX);
  for (size_t k = 0; k < a.tri_corner.size(); k++) ca[a.tri_corner[k]] = a.indices[k];
  for (size_t k = 0; k < b.tri_corner.size(); k++) cb[b.tri_corner[k]] = b.indices[k];
  float dn = 0.0f;
  for (size_t c = 0; c < ca.size(); c++) {
    if (ca[c] == UINT32_MAX || cb[c] == UINT32_MAX) {
      ok = ok && ca[c] == cb[c];
      continue;
    }
    ok = ok && std::memcmp(&a.positions[ca[c]], &b.positions[cb[c]], sizeof(Vec3)) == 0;
    dn = std::max(dn, length(a.normals[ca[c]] - b.normals[cb[c]]));
  }
  *max_dn = dn;
  return ok;
}

/* The five meshes of the spec: a cube, a UV sphere, a 50k-face grid, sharp edges, UV seams. */
inline std::vector<std::pair<std::string, MeshPtr>> r63_meshes() {
  std::vector<std::pair<std::string, MeshPtr>> out;
  out.push_back({"cube", primitives::cube(1.0f)});
  MeshPtr sphere = primitives::uv_sphere(0.5f, 24, 16);
  sphere->smooth = true;
  out.push_back({"UV sphere", sphere});
  MeshPtr grid = primitives::grid(4.0f, 224, 224);  // 50,176 faces
  grid->smooth = true;
  out.push_back({"50k-face grid", grid});
  MeshPtr sharp = primitives::cylinder(0.5f, 2.0f, 24);
  meshops::shade_auto_smooth(*sharp, 30.0f);  // smooth sides, hard caps (sharp edges)
  out.push_back({"sharp edges", sharp});
  MeshPtr seams = primitives::torus(0.5f, 0.2f, 32, 16);
  seams->smooth = true;
  seams->seams_sharp = true;
  std::vector<std::pair<uint32_t, uint32_t>> edges;
  seams->edges(edges);
  for (size_t i = 0; i < edges.size(); i += 7) seams->set_seam(edges[i].first, edges[i].second, true);
  out.push_back({"UV seams", seams});
  return out;
}


/* ===================================================================== */
/* Round 68 (task 0046): Weight Paint mode                                 */
/* ===================================================================== */

/* An editor looking down at a flat 2 x 2 pad (20 x 20 quads) high above the default scene, in Weight Paint with a
 * group "G" at 0 everywhere. */
struct R68 {
  Editor ed;
  GameObject *pad = nullptr;
  explicit R68(MeshPtr mesh = nullptr) {
    ed.init_headless(1000, 800);
    pad = ed.scene().create("Pad");
    MeshPtr m = mesh ? mesh : primitives::grid(2.0f, 20, 20);
    m->vertex_groups = {{"G", std::vector<float>(m->vert_count(), 0.0f)}};
    pad->add<MeshFilter>()->mesh = m;
    pad->add<MeshRenderer>();
    pad->set_world_position({0, 20, 0});
    ed.command("select Pad");
    ed.command("camera 0 89 3.5 0 20 0");
    ed.mark_changed_for_test("R68 pad");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    ed.command("weightpaint on");
    ed.step_frame_headless();
  }
  const Mesh &mesh() { return *ed.scene().find_by_name("Pad")->mesh_filter()->mesh; }
  float weight_at(Vec3 local) {  // the weight of the vertex nearest a local point
    const Mesh &m = mesh();
    size_t best = 0;
    for (size_t i = 0; i < m.vert_count(); i++)
      if (length(m.positions[i] - local) < length(m.positions[best] - local)) best = i;
    return m.vertex_groups[0].weights[best];
  }
  /* A stroke through local points (projected into the window). */
  void stroke(std::initializer_list<Vec3> pts) {
    std::string cmd = "wpstroke";
    for (Vec3 p : pts) {
      int x = 0, y = 0;
      CHECK(ed.project_to_window(pad->world_matrix().point(p), x, y));
      cmd += " " + std::to_string(x) + " " + std::to_string(y);
    }
    ed.command(cmd);
    ed.step_frame_headless();
  }
};


/* ===================================================================== */
/* Round 69 (task 0047): skeletons and skinning                            */
/* ===================================================================== */

/* A tube up +Y from 0 to 2 (radius 0.2, 21 rings of 16), an armature "Rig" with Lower (0..1) and Upper (1..2), the
 * tube skinned: Lower below 0.9, Upper above 1.1, a blend between. */
struct R69Rig {
  Scene s;
  GameObject *rig = nullptr, *lower = nullptr, *upper = nullptr, *tube = nullptr;
  MeshPtr base;
  R69Rig() {
    rig = s.create("Rig");
    rig->add<Armature>();
    lower = s.create("Lower", rig);
    lower->add<Bone>();
    upper = s.create("Upper", lower);
    upper->set_local_position({0, 1, 0});
    Bone *ub = upper->add<Bone>();
    ub->rest_position = {0, 1, 0};
    base = std::make_shared<Mesh>();
    const int rings = 21, seg = 16;
    for (int r = 0; r < rings; r++)
      for (int k = 0; k < seg; k++) {
        const float a = k * 2.0f * kPi / seg;
        base->add_vert({0.2f * std::cos(a), r * 0.1f, 0.2f * std::sin(a)});
      }
    for (int r = 0; r + 1 < rings; r++)
      for (int k = 0; k < seg; k++) {
        const uint32_t a = (uint32_t)(r * seg + k), b = (uint32_t)(r * seg + (k + 1) % seg);
        base->add_face({a, a + seg, b + seg, b});
      }
    VertexGroup lo{"Lower", {}}, up{"Upper", {}};
    for (const Vec3 &p : base->positions) {
      const float t = std::max(0.0f, std::min(1.0f, (p.y - 0.9f) / 0.2f));
      lo.weights.push_back(1.0f - t);
      up.weights.push_back(t);
    }
    base->vertex_groups = {lo, up};
    base->touch();
    tube = s.create("Tube");
    tube->add<MeshFilter>()->mesh = base;
    tube->add<SkinModifier>()->object = "Rig";
  }
  /* The ring at height y: its mean distance from the bone axis (Y). */
  float ring_radius(float y) {
    const Mesh *m = tube->evaluated_mesh();
    float sum = 0.0f;
    int n = 0;
    for (size_t i = 0; i < base->vert_count(); i++)
      if (std::fabs(base->positions[i].y - y) < 1e-4f) sum += length(Vec3(m->positions[i].x, 0, m->positions[i].z)), n++;
    return n ? sum / n : 0.0f;
  }
};


/* An open tube from a to b (rings x segments, normals out), appended to m: the test bodies of round 81. */
inline void r81_tube(Mesh &m, Vec3 a, Vec3 b, float radius, int rings, int segs) {
  const Vec3 axis = normalize(b - a);
  const Vec3 side = std::fabs(axis.y) < 0.9f ? normalize(cross(axis, Vec3(0, 1, 0))) : normalize(cross(axis, Vec3(1, 0, 0)));
  const Vec3 up = cross(side, axis);
  const uint32_t base = (uint32_t)m.vert_count();
  for (int r = 0; r <= rings; r++)
    for (int s = 0; s < segs; s++) {
      const float t = (float)r / (float)rings, ang = 6.2831853f * (float)s / (float)segs;
      m.add_vert(a + (b - a) * t + (side * std::cos(ang) + up * std::sin(ang)) * radius);
    }
  for (int r = 0; r < rings; r++)
    for (int s = 0; s < segs; s++) {
      const uint32_t i0 = base + (uint32_t)(r * segs + s), i1 = base + (uint32_t)(r * segs + (s + 1) % segs);
      m.add_face({i0, i1, i1 + (uint32_t)segs, i0 + (uint32_t)segs});
    }
}

/* Each vertex's weights over the groups: in [0, 1] and summing to 1 (the worst error). */
inline float r81_partition_error(const Mesh &m) {
  float worst = 0.0f;
  for (size_t v = 0; v < m.vert_count(); v++) {
    float sum = 0.0f;
    for (const VertexGroup &g : m.vertex_groups) {
      const float w = g.weights[v];
      if (!(w >= 0.0f && w <= 1.0f)) worst = std::max(worst, 1.0f);
      sum += w;
    }
    worst = std::max(worst, std::fabs(sum - 1.0f));
  }
  return worst;
}

inline float r81_weight(const Mesh &m, const char *group, size_t v) {
  const int gi = m.find_group(group);
  return gi < 0 ? 0.0f : m.vertex_groups[(size_t)gi].weights[v];
}

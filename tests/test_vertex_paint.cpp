// SPDX-License-Identifier: GPL-2.0-or-later
// Colour attributes on Mesh and the Vertex Paint mode (task 0525, round 545, ADR 0152). Every stroke is a real mouse drag
// through step_frame_headless; only the console tests use the console's own forms.
#include "test_common.h"

#include <cmath>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>

#include "../src/core/json.h"
#include "../src/scene/dyntopo.h"

#include <chrono>

using namespace bl;

namespace {

using Dom = ColorAttribute::Domain;

/* ------------------------------------------------------------------ colours as a function of position */

/* A colour that depends on a vertex's place alone (x and z, so any point of a flat pad on y = 0 has it): carrying a layer
 * by position over a flat pad must give exactly this at every vertex, however the operator made the vertex. */
Vec4 vpt_f(Vec3 p) { return Vec4((p.x + 1.0f) * 0.5f, (p.z + 1.0f) * 0.5f, 0.25f, 1.0f); }

bool vpt_near(Vec4 a, Vec4 b, float eps) {
  return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps && std::fabs(a.z - b.z) <= eps && std::fabs(a.w - b.w) <= eps;
}

bool vpt_same(const std::vector<Vec4> &a, const std::vector<Vec4> &b) { return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(Vec4)) == 0); }
bool vpt_same2(const std::vector<Vec2> &a, const std::vector<Vec2> &b) { return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(Vec2)) == 0); }

bool vpt_finite4(Vec4 c) { return std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z) && std::isfinite(c.w); }

/* Adds a layer, every element vpt_f of its vertex (Corner: the blue channel also names the face, so two faces sharing a
 * vertex differ there); it becomes the active and render layer when none is. */
int vpt_add_layer(Mesh &m, const std::string &name, Dom dom) {
  ColorAttribute c = m.make_color(name, dom, Vec4(1, 1, 1, 1));
  if (dom == Dom::Point) {
    for (size_t v = 0; v < m.vert_count(); v++) c.rgba[v] = vpt_f(m.positions[v]);
  }
  else {
    for (size_t f = 0; f < m.face_count(); f++)
      for (uint32_t k = m.face_offsets[f]; k < m.face_offsets[f + 1]; k++) {
        Vec4 col = vpt_f(m.positions[m.corner_verts[k]]);
        col.z = (float)(f + 1) / (float)(m.face_count() + 1);
        c.rgba[k] = col;
      }
  }
  m.color_attributes.push_back(std::move(c));
  const int idx = (int)m.color_attributes.size() - 1;
  if (m.active_color < 0) m.active_color = idx;
  if (m.render_color < 0) m.render_color = idx;
  return idx;
}

/* Every layer fits its domain, active / render are in range, every index in the mesh is in range. */
bool vpt_valid(const Mesh &m) {
  for (uint32_t v : m.corner_verts)
    if (v >= m.vert_count()) return false;
  for (const ColorAttribute &c : m.color_attributes)
    if (!m.color_fits(c)) return false;
  const int n = (int)m.color_attributes.size();
  return m.active_color >= -1 && m.active_color < n && m.render_color >= -1 && m.render_color < n;
}

const ColorAttribute *vpt_find(const Mesh &m, const char *name) {
  const int i = m.find_color(name);
  return i >= 0 ? &m.color_attributes[(size_t)i] : nullptr;
}

/* ------------------------------------------------------------------ the editor on a pad, in Vertex Paint */

/* An editor looking down at a flat 2 x 2 pad (20 x 20 quads) high above the default scene, in Vertex Paint (the mode adds
 * a white Point layer "Color"). */
struct VPad {
  Editor ed;
  GameObject *pad = nullptr;
  explicit VPad(MeshPtr mesh = nullptr, bool corner = false, bool on = true) {
    ed.init_headless(1000, 800);
    pad = ed.scene().create("Pad");
    pad->add<MeshFilter>()->mesh = mesh ? mesh : primitives::grid(2.0f, 20, 20);
    pad->add<MeshRenderer>();
    pad->set_world_position({0, 20, 0});
    ed.command("select Pad");
    ed.command("camera 0 89 3.5 0 20 0");
    ed.mark_changed_for_test("VP pad");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    if (corner) ed.command("colorattr add Corner corner");
    if (on) ed.command("vpaint on");
    ed.step_frame_headless();
  }
  const Mesh &mesh() { return *ed.scene().find_by_name("Pad")->mesh_filter()->mesh; }
  const ColorAttribute *layer() {
    const Mesh &m = mesh();
    return m.active_color >= 0 && m.active_color < (int)m.color_attributes.size() ? &m.color_attributes[(size_t)m.active_color] : nullptr;
  }
  size_t nearest(Vec3 local) {
    const Mesh &m = mesh();
    size_t best = 0;
    for (size_t i = 0; i < m.vert_count(); i++)
      if (length(m.positions[i] - local) < length(m.positions[best] - local)) best = i;
    return best;
  }
  /* The colour of the Point layer's vertex nearest a local point. */
  Vec4 at(Vec3 local) {
    const ColorAttribute *c = layer();
    const size_t v = nearest(local);
    return c && v < c->rgba.size() ? c->rgba[v] : Vec4(-1, -1, -1, -1);
  }
  /* Rewrites the active layer's elements (position of the vertex -> colour). */
  template <class F> void paint_layer(F fn) {
    Mesh &m = *mesh_make_mutable(pad->mesh_filter()->mesh);
    ColorAttribute &c = m.color_attributes[(size_t)m.active_color];
    for (size_t e = 0; e < c.rgba.size(); e++) {
      const size_t v = c.domain == Dom::Point ? e : m.corner_verts[e];
      c.rgba[e] = fn(m.positions[v]);
    }
    m.touch();
    ed.step_frame_headless();
  }
  float ppu() {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    CHECK(ed.project_to_window(pad->world_matrix().point({0, 0, 0}), x0, y0));
    CHECK(ed.project_to_window(pad->world_matrix().point({1, 0, 0}), x1, y1));
    return (float)std::abs(x1 - x0);
  }
  int px(float units) { return std::max(2, (int)(units * ppu())); }
  /* One mouse drag through local points: move, press, move along, release (a single point is a click). */
  void drag(const std::vector<Vec3> &pts, int mods = 0) {
    std::vector<std::pair<int, int>> p;
    for (Vec3 q : pts) {
      int x = 0, y = 0;
      CHECK(ed.project_to_window(pad->world_matrix().point(q), x, y));
      p.push_back({x, y});
    }
    auto ev = [&](R34ET ty, int x, int y) {
      platform::Event e = r34_mouse(ty, x, y);
      e.mods = mods;
      return e;
    };
    ed.step_frame_headless({ev(R34ET::MouseMove, p[0].first, p[0].second)});
    ed.step_frame_headless({ev(R34ET::MouseDown, p[0].first, p[0].second)});
    for (size_t i = 1; i < p.size(); i++)
      for (int k = 1; k <= 6; k++)
        ed.step_frame_headless({ev(R34ET::MouseMove, p[i - 1].first + (p[i].first - p[i - 1].first) * k / 6,
                                   p[i - 1].second + (p[i].second - p[i - 1].second) * k / 6)});
    ed.step_frame_headless({ev(R34ET::MouseUp, p.back().first, p.back().second)});
    ed.step_frame_headless();
  }
  /* A flat brush of `units` radius: constant falloff, Front Faces Only off. */
  void flat(float units, const char *tool = "draw", float strength = 1.0f) {
    ed.command("vpset falloff constant");
    ed.command("vpset front off");
    ed.command(std::string("vpbrush ") + tool + " " + std::to_string(px(units)) + " " + std::to_string(strength));
  }
  /* How much red-on-white paint each vertex has (Mix of red over white: the green channel falls by the amount). */
  std::vector<float> amounts() {
    std::vector<float> a;
    const ColorAttribute *c = layer();
    if (c)
      for (const Vec4 &v : c->rgba) a.push_back(1.0f - v.y);
    return a;
  }
};

float vpt_max(const std::vector<float> &w, size_t b = 0, size_t e = SIZE_MAX) {
  float m = 0.0f;
  for (size_t i = b; i < std::min(e, w.size()); i++) m = std::max(m, w[i]);
  return m;
}

/* Blender's IMB_blend formulas for one channel (c: the layer, p: the paint, a: its share). */
float vpt_blender(int mode, float c, float p, float a) {
  switch (mode) {
    case 1: return c + p * a;                                          // Add (Blender's float blend: not clamped to 1 - an HDR layer)
    case 2: return std::max(c - p * a, 0.0f);                          // Subtract
    case 3: return c * (1.0f - a) + c * p * a;                         // Multiply
    case 4: return p > c ? c * (1.0f - a) + p * a : c;                 // Lighten
    case 5: return p < c ? c * (1.0f - a) + p * a : c;                 // Darken
    default: return c * (1.0f - a) + p * a;                            // Mix
  }
}

/* ------------------------------------------------------------------ Edit Mode and meshes without a brush */

void vpt_setup(Editor &ed, MeshPtr m) {
  ed.init_headless(1000, 800);
  GameObject *g = ed.scene().create("Grid");
  g->add<MeshFilter>()->mesh = std::move(m);
  g->add<MeshRenderer>();
  ed.command("select Grid");
  ed.mark_changed_for_test("VP grid");
  ed.step_frame_headless();
}

/* One Edit Mode operator on the "Grid" (the way the vertex-group tests of round 67 run one), the undo commit then. */
void vpt_op(Editor &ed, const char *op, const std::string &sel) {
  if (sel == "subset") ed.command("fsel 5 6 7 12");  // (some faces, not all: Split needs a boundary)
  else if (sel == "faces") ed.command("edit face all");
  else ed.command("edit vertex");
  if (sel == "all") ed.command("edit vertex all");
  if (sel == "some") ed.command("vsel 8 9 15 16 22");
  if (sel == "edge") ed.command("vsel 8 9");
  ed.step_frame_headless();
  ed.command(std::string("editop ") + op);
  ed.step_frame_headless();
  ed.step_frame_headless();
  ed.command("edit off");
  ed.step_frame_headless();
}

const Mesh &vpt_grid(Editor &ed) { return *ed.scene().find_by_name("Grid")->mesh_filter()->mesh; }

MeshPtr vpt_flat(bool point, bool corner, int n = 6) {
  MeshPtr m = primitives::grid(2.0f, n, n);
  if (point) vpt_add_layer(*m, "P", Dom::Point);
  if (corner) vpt_add_layer(*m, "Seam", Dom::Corner);
  return m;
}

/* The log's new warnings and infos since `since`, as one string. */
std::string vpt_log_since(size_t since) {
  std::vector<LogEntry> e;
  Log::fetch(since, e);
  std::string s;
  for (const LogEntry &x : e) s += x.text + "\n";
  return s;
}

size_t vpt_log_mark() {
  std::vector<LogEntry> e;
  return Log::fetch(0, e);
}

/* The scene as text and back (one object "M"). */
bool vpt_roundtrip(MeshPtr m, Scene &loaded, std::string &err, std::string *text_out = nullptr) {
  Scene s;
  GameObject *g = s.create("M");
  g->add<MeshFilter>()->mesh = std::move(m);
  const std::string text = save_scene_text(s);
  if (text_out) *text_out = text;
  return load_scene_text(text, loaded, err);
}

const Mesh *vpt_loaded(Scene &s) {
  GameObject *g = s.find_by_name("M");
  return g && g->mesh_filter() ? g->mesh_filter()->mesh.get() : nullptr;
}

/* A .glb's JSON chunk and the render layer's colours as the file holds them. */
struct VptGlb {
  Json j;
  bool has_colour = false;
  size_t positions = 0;
  std::vector<Vec3> pos;
  std::vector<Vec4> col;
};

VptGlb vpt_glb(const Mesh &m, bool outline = false) {
  (void)outline;
  ExportItem it;
  it.name = "M";
  it.mesh = &m;
  it.world = Mat4::identity();
  const std::string glb = export_gltf({it}, export_defaults((int)ExportFormat::GLB), true, "", nullptr);
  VptGlb r;
  auto u32 = [&](size_t at) {
    uint32_t v = 0;
    if (at + 4 <= glb.size()) std::memcpy(&v, glb.data() + at, 4);
    return v;
  };
  if (glb.size() < 20 || u32(0) != 0x46546C67u || u32(16) != 0x4E4F534Au || 20 + (size_t)u32(12) > glb.size()) return r;
  JsonParser p{glb.data() + 20, glb.data() + 20 + u32(12)};
  r.j = p.value();
  if (!p.ok) return r;
  const size_t bin = 20 + (size_t)u32(12) + 8;
  const Json &attrs = r.j["meshes"][0]["primitives"][0]["attributes"];
  auto read = [&](const Json &idx, int comps, std::vector<float> &out, size_t &count) {
    const Json &acc = r.j["accessors"][(size_t)idx.number()];
    const Json &view = r.j["bufferViews"][(size_t)acc["bufferView"].number()];
    const size_t at = bin + (size_t)view["byteOffset"].number(0) + (size_t)acc["byteOffset"].number(0);
    count = (size_t)acc["count"].number();
    out.resize(count * (size_t)comps);
    if (at + out.size() * 4 <= glb.size()) std::memcpy(out.data(), glb.data() + at, out.size() * 4);
  };
  std::vector<float> f;
  size_t n = 0;
  if (attrs["POSITION"].type == Json::Num) {
    read(attrs["POSITION"], 3, f, n);
    r.positions = n;
    for (size_t i = 0; i < n; i++) r.pos.push_back(Vec3(f[i * 3], f[i * 3 + 1], f[i * 3 + 2]));
  }
  r.has_colour = attrs["COLOR_0"].type == Json::Num;
  if (r.has_colour) {
    read(attrs["COLOR_0"], 4, f, n);
    for (size_t i = 0; i < n; i++) r.col.push_back(Vec4(f[i * 4], f[i * 4 + 1], f[i * 4 + 2], f[i * 4 + 3]));
  }
  return r;
}

std::string vpt_ply(const Mesh &m) {
  ExportItem it;
  it.name = "M";
  it.mesh = &m;
  it.world = Mat4::identity();
  ExportOptions o = export_defaults((int)ExportFormat::PLY);
  o.ascii = true;
  return export_ply({it}, o);
}

/* ------------------------------------------------------------------ the mesh builders of round 539's fixtures */

/* Two 5 by 5 grids in one mesh, 0.1 apart and mirror images across x = 0 (the first has the vertices [0, na)). */
MeshPtr vpt_two_islands(size_t &na) {
  auto m = std::make_shared<Mesh>();
  for (int side = 0; side < 2; side++) {
    MeshPtr g = primitives::grid(1.0f, 5, 5);
    const uint32_t base = (uint32_t)m->vert_count();
    const float cx = side == 0 ? -0.55f : 0.55f;
    for (const Vec3 &p : g->positions) m->add_vert({p.x + cx, p.y, p.z});
    for (size_t f = 0; f < g->face_count(); f++) {
      std::vector<uint32_t> vs(g->face_verts(f), g->face_verts(f) + g->face_size(f));
      for (uint32_t &v : vs) v += base;
      m->add_face(vs.data(), vs.size());
    }
    if (side == 0) na = m->vert_count();
  }
  return m;
}

/* A groove along z = -0.5 (floor 0.15 down) and a ridge along z = 0.5 (top 0.15 up) in a 2 by 2 pad. */
MeshPtr vpt_groove_ridge() {
  MeshPtr m = primitives::grid(2.0f, 20, 20);
  for (int x = 0; x <= 20; x++) {
    m->positions[(size_t)(5 * 21 + x)].y = -0.15f;
    m->positions[(size_t)(15 * 21 + x)].y = 0.15f;
  }
  return m;
}

/* A top (6 by 6, y = 0) with a vertical wall down its +x edge, as separate vertices (a box's top and side). */
MeshPtr vpt_box_top_and_side(size_t &ntop) {
  auto m = std::make_shared<Mesh>();
  MeshPtr g = primitives::grid(1.0f, 6, 6);
  for (const Vec3 &p : g->positions) m->add_vert(p);
  for (size_t f = 0; f < g->face_count(); f++) m->add_face(std::vector<uint32_t>(g->face_verts(f), g->face_verts(f) + g->face_size(f)).data(), g->face_size(f));
  ntop = m->vert_count();
  const int nz = 6, ny = 4;
  for (int r = 0; r <= ny; r++)
    for (int c = 0; c <= nz; c++) m->add_vert({0.5f, -0.5f * (float)r / (float)ny, -0.5f + (float)c / (float)nz});
  for (int r = 0; r < ny; r++)
    for (int c = 0; c < nz; c++) {
      const uint32_t a = (uint32_t)ntop + (uint32_t)(r * (nz + 1) + c), b = a + 1, cc = a + (uint32_t)(nz + 1) + 1, d = a + (uint32_t)(nz + 1);
      m->add_face({a, b, cc, d});
    }
  return m;
}

/* Two unit quads sharing the edge x = 1 (vertices 1 and 4), on y = 0. */
MeshPtr vpt_two_quads() {
  auto m = std::make_shared<Mesh>();
  for (int z = 0; z < 2; z++)
    for (int x = 0; x < 3; x++) m->add_vert({(float)x, 0.0f, (float)z});
  m->add_face({0, 1, 4, 3});
  m->add_face({1, 2, 5, 4});
  return m;
}

/* ================================================================ round 545b (task 0533): a material's Vertex Color input and
 * importers that read colours. Helpers first, then the tests. */

/* A scratch folder for the files these tests write (inside the scratch project, never Assets/). */
std::string vci_dir() {
  const std::string d = fs::join(scratch_project(), "vci");
  fs::make_dirs(d);
  return d;
}

void vci_write(const std::string &path, const std::string &bytes) {
  std::ofstream f(path, std::ios::binary);
  f.write(bytes.data(), (std::streamsize)bytes.size());
}

template <class T> void vci_put(std::string &s, T v) { s.append(reinterpret_cast<const char *>(&v), sizeof(T)); }

long vci_q(float v) { return std::lround(v * 1000.0f); }
bool vci_less(const Vec4 &a, const Vec4 &b) {
  if (vci_q(a.x) != vci_q(b.x)) return vci_q(a.x) < vci_q(b.x);
  if (vci_q(a.y) != vci_q(b.y)) return vci_q(a.y) < vci_q(b.y);
  if (vci_q(a.z) != vci_q(b.z)) return vci_q(a.z) < vci_q(b.z);
  return vci_q(a.w) < vci_q(b.w);
}

/* The worst channel difference between two colour sets, as sets (an importer may reorder vertices); 1e9 when the sizes differ. */
float vci_set_error(std::vector<Vec4> got, std::vector<Vec4> want) {
  if (got.size() != want.size()) return 1e9f;
  std::sort(got.begin(), got.end(), vci_less);
  std::sort(want.begin(), want.end(), vci_less);
  float worst = 0.0f;
  for (size_t i = 0; i < got.size(); i++)
    worst = std::max(worst, std::max(std::max(std::fabs(got[i].x - want[i].x), std::fabs(got[i].y - want[i].y)),
                                     std::max(std::fabs(got[i].z - want[i].z), std::fabs(got[i].w - want[i].w))));
  return worst;
}

Vec4 vci_encode(Vec4 c) { return Vec4(linear_to_srgb(c.x), linear_to_srgb(c.y), linear_to_srgb(c.z), c.w); }
std::vector<Vec4> vci_encode(std::vector<Vec4> v) {
  for (Vec4 &c : v) c = vci_encode(c);
  return v;
}

const Mesh *vci_first_mesh(const ImportResult &r) {
  for (const ImportedNode &n : r.nodes)
    if (n.mesh) return n.mesh.get();
  return nullptr;
}

/* A grid whose left half (x < 0) is red and right half blue - per vertex (Point) or per face (Corner, by the face's centre).
 * `alpha_left` is the red half's alpha. */
MeshPtr vci_pad(bool corner, float alpha_left = 1.0f, int n = 8) {
  MeshPtr m = primitives::grid(2.0f, n, n);
  ColorAttribute c = m->make_color("Col", corner ? Dom::Corner : Dom::Point, Vec4(1, 1, 1, 1));
  auto col = [&](float x) { return x < 0.0f ? Vec4(1, 0, 0, alpha_left) : Vec4(0, 0, 1, 1); };
  if (!corner) {
    for (size_t v = 0; v < m->vert_count(); v++) c.rgba[v] = col(m->positions[v].x);
  }
  else {
    for (size_t f = 0; f < m->face_count(); f++) {
      float cx = 0.0f;
      for (uint32_t k = m->face_offsets[f]; k < m->face_offsets[f + 1]; k++) cx += m->positions[m->corner_verts[k]].x;
      cx /= (float)(m->face_offsets[f + 1] - m->face_offsets[f]);
      for (uint32_t k = m->face_offsets[f]; k < m->face_offsets[f + 1]; k++) c.rgba[k] = col(cx);
    }
  }
  m->color_attributes.push_back(std::move(c));
  m->active_color = m->render_color = 0;
  return m;
}

MaterialPtr vci_mat(bool on, bool cutout = false) {
  MaterialPtr m = make_material("VC", Vec3(1, 1, 1));
  m->vertex_color = on;
  m->specular = 0.0f;
  m->roughness = 1.0f;
  if (cutout) m->surface = (int)MaterialSurface::Cutout, m->alpha_clip = 0.5f;
  return m;
}

/* The Shaded view of one pad (the "Pad" of VPad) with a material. */
struct VciView {
  VPad t;
  VciView(MeshPtr mesh, MaterialPtr mat) : t(mesh, false, false) {
    t.pad->get<MeshRenderer>()->materials = {mat};
    t.ed.command("shading shaded");
    t.ed.mark_changed_for_test("VCI view");
    for (int i = 0; i < 4; i++) t.ed.step_frame_headless();
  }
  /* The pixel over a local point of the pad (x, z on y = 0). */
  std::array<int, 3> px(float x, float z) {
    int sx = 0, sy = 0;
    CHECK(t.ed.project_to_window(t.pad->world_matrix().point(Vec3(x, 0, z)), sx, sy));
    const Image &fb = t.ed.main_fb_for_test();
    if (sx < 0 || sy < 0 || sx >= fb.width || sy >= fb.height) return {0, 0, 0};
    const uint32_t p = fb.pixels[(size_t)sy * (size_t)fb.width + (size_t)sx];
    return {(int)((p >> 16) & 255), (int)((p >> 8) & 255), (int)(p & 255)};
  }
  void frames(int n = 3) {
    for (int i = 0; i < n; i++) t.ed.step_frame_headless();
  }
};

bool vci_red(const std::array<int, 3> &p) { return p[0] > 50 && p[1] * 10 < p[0] * 6 && p[2] * 10 < p[0] * 6; }
bool vci_blue(const std::array<int, 3> &p) { return p[2] > 50 && p[0] * 10 < p[2] * 6 && p[1] * 10 < p[2] * 6; }
bool vci_greenish(const std::array<int, 3> &p) { return p[1] > 50 && p[0] * 10 < p[1] * 6 && p[2] * 10 < p[1] * 6; }
bool vci_grey(const std::array<int, 3> &p) { return p[0] > 50 && std::abs(p[0] - p[1]) < 45 && std::abs(p[0] - p[2]) < 45; }
bool vci_same_px(const std::array<int, 3> &a, const std::array<int, 3> &b, int tol = 3) {
  return std::abs(a[0] - b[0]) <= tol && std::abs(a[1] - b[1]) <= tol && std::abs(a[2] - b[2]) <= tol;
}

/* The CPU path tracer on `mesh` seen from above through a uniform environment `env`: linear RGB per pixel (3 floats), the
 * pad's left / right halves at columns 7 / 25 of 32 (the camera's up is +z, so world -x is the right of the picture). */
const int kVciW = 32;
std::vector<float> vci_pt(const MeshPtr &mesh, const MaterialPtr &mat, bool give_colours, float env = 0.5f) {
  const RenderMesh &rm = mesh->render_mesh_tangents();
  std::vector<Vec4> cols;
  const bool have = render_vertex_colors_into(*mesh, rm, cols);
  std::vector<MaterialPtr> mats{mat};
  PTObject o;
  o.mesh = &rm;
  o.model = Mat4::identity();
  o.materials = &mats;
  o.colors = give_colours && have ? &cols : nullptr;
  Environment e;
  e.mode = Environment::Color;
  e.color = {env, env, env};
  PathTracer pt;
  pt.build({o}, {}, e);
  PTSettings st;
  st.max_bounces = 4;
  st.clamp_indirect = 0;
  st.denoise = false;
  pt.set_settings(st);
  pt.set_camera(Mat4::look_at({0, 3, 0}, {0, 0, 0}, {0, 0, 1}), Mat4::perspective(40 * kDeg2Rad, 1, 0.1f, 10), kVciW, kVciW);
  pt.render(1e9, 32);
  return pt.linear_rgb(false);
}

Vec3 vci_pix(const std::vector<float> &rgb, int x, int y = kVciW / 2) {
  const size_t i = ((size_t)y * kVciW + (size_t)x) * 3;
  return i + 2 < rgb.size() ? Vec3(rgb[i], rgb[i + 1], rgb[i + 2]) : Vec3(-1, -1, -1);
}
bool vci_pt_red(Vec3 c, float lit) { return c.x > 0.6f * lit && c.y < 0.2f * lit && c.z < 0.2f * lit; }
bool vci_pt_blue(Vec3 c, float lit) { return c.z > 0.6f * lit && c.x < 0.2f * lit && c.y < 0.2f * lit; }
bool vci_pt_white(Vec3 c, float lit) { return c.x > 0.8f * lit && c.y > 0.8f * lit && c.z > 0.8f * lit; }
bool vci_finite_all(const std::vector<float> &v) {
  for (float f : v)
    if (!std::isfinite(f)) return false;
  return !v.empty();
}

/* ---- file fixtures */

/* The four colours every importer test uses: bytes (sRGB for PLY / FBX) and their linear floats. */
const int kVciBytes[4][4] = {{255, 0, 0, 255}, {0, 0, 255, 255}, {0, 255, 0, 128}, {64, 128, 192, 255}};
const float kVciFloats[4][4] = {{1, 0, 0, 1}, {0, 0, 1, 1}, {0, 1, 0, 0.5f}, {0.25f, 0.5f, 0.75f, 1}};

/* A 4-vertex, 2-triangle PLY with colours: binary or ASCII, uchar or float channels, with or without alpha. */
std::string vci_ply(bool binary, bool uchar, bool alpha, int count = 4, const char *only = nullptr) {
  std::string s = "ply\nformat ";
  s += binary ? "binary_little_endian" : "ascii";
  s += " 1.0\nelement vertex " + std::to_string(count) + "\nproperty float x\nproperty float y\nproperty float z\n";
  const char *ty = uchar ? "uchar" : "float";
  const char *names[4] = {"red", "green", "blue", "alpha"};
  const int nch = alpha ? 4 : 3;
  for (int k = 0; k < nch; k++)
    if (!only || std::strstr(only, names[k])) s += std::string("property ") + ty + " " + names[k] + "\n";
  s += "element face 2\nproperty list uchar int vertex_indices\nend_header\n";
  const float pos[4][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
  for (int v = 0; v < 4 && v < count; v++) {
    if (binary) {
      for (int k = 0; k < 3; k++) vci_put<float>(s, pos[v][k]);
    }
    else s += std::to_string(pos[v][0]) + " " + std::to_string(pos[v][1]) + " " + std::to_string(pos[v][2]);
    for (int k = 0; k < nch; k++) {
      if (only && !std::strstr(only, names[k])) continue;
      if (binary) {
        if (uchar) vci_put<uint8_t>(s, (uint8_t)kVciBytes[v][k]);
        else vci_put<float>(s, kVciFloats[v][k]);
      }
      else s += " " + (uchar ? std::to_string(kVciBytes[v][k]) : std::to_string(kVciFloats[v][k]));
    }
    if (!binary) s += "\n";
  }
  const int tris[2][3] = {{0, 1, 2}, {0, 2, 3}};
  for (int f = 0; f < 2; f++) {
    if (binary) {
      vci_put<uint8_t>(s, 3);
      for (int k = 0; k < 3; k++) vci_put<int32_t>(s, tris[f][k]);
    }
    else s += "3 " + std::to_string(tris[f][0]) + " " + std::to_string(tris[f][1]) + " " + std::to_string(tris[f][2]) + "\n";
  }
  return s;
}

std::vector<Vec4> vci_ply_expected(bool uchar, bool alpha) {
  std::vector<Vec4> w;
  for (int v = 0; v < 4; v++) {
    if (uchar)
      w.push_back(Vec4(srgb_to_linear(kVciBytes[v][0] / 255.0f), srgb_to_linear(kVciBytes[v][1] / 255.0f), srgb_to_linear(kVciBytes[v][2] / 255.0f),
                       alpha ? kVciBytes[v][3] / 255.0f : 1.0f));
    else w.push_back(Vec4(kVciFloats[v][0], kVciFloats[v][1], kVciFloats[v][2], alpha ? kVciFloats[v][3] : 1.0f));
  }
  return w;
}

/* A glTF (JSON + .bin beside it): `pos` / `idx` as given, COLOR_0 of component type `ct` (5126 float, 5121 u8, 5123 u16) with
 * `comps` (3 or 4) values per element in `colour` (`ccount` elements; empty: no COLOR_0). */
std::string vci_gltf_file(const std::string &name, const std::vector<Vec3> &pos, const std::vector<uint16_t> &idx, int ct, int comps, bool normalized,
                          const std::string &colour, size_t ccount) {
  std::string bin;
  for (const Vec3 &p : pos) {
    vci_put<float>(bin, p.x);
    vci_put<float>(bin, p.y);
    vci_put<float>(bin, p.z);
  }
  const size_t ioff = bin.size();
  for (uint16_t i : idx) vci_put<uint16_t>(bin, i);
  while (bin.size() % 4) bin.push_back('\0');
  const size_t coff = bin.size();
  bin += colour;
  const std::string dir = vci_dir();
  vci_write(fs::join(dir, name + ".bin"), bin);
  const bool has_col = !colour.empty();
  std::string j = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],"meshes":[{"primitives":[{"attributes":{"POSITION":0)";
  if (has_col) j += R"(,"COLOR_0":2)";
  j += R"(},"indices":1}]}],"buffers":[{"uri":")" + name + R"(.bin","byteLength":)" + std::to_string(bin.size()) + "}],";
  j += R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":)" + std::to_string(ioff) + R"(},{"buffer":0,"byteOffset":)" + std::to_string(ioff) +
       R"(,"byteLength":)" + std::to_string(idx.size() * 2) + "}";
  if (has_col) j += R"(,{"buffer":0,"byteOffset":)" + std::to_string(coff) + R"(,"byteLength":)" + std::to_string(colour.size()) + "}";
  j += R"(],"accessors":[{"bufferView":0,"componentType":5126,"count":)" + std::to_string(pos.size()) + R"(,"type":"VEC3","min":[0,0,0],"max":[1,1,1]},)";
  j += R"({"bufferView":1,"componentType":5123,"count":)" + std::to_string(idx.size()) + R"(,"type":"SCALAR"})";
  if (has_col)
    j += std::string(R"(,{"bufferView":2,"componentType":)") + std::to_string(ct) + (normalized ? R"(,"normalized":true)" : "") + R"(,"count":)" +
         std::to_string(ccount) + R"(,"type":")" + (comps == 4 ? "VEC4" : "VEC3") + R"("})";
  j += "]}";
  const std::string path = fs::join(dir, name + ".gltf");
  vci_write(path, j);
  return path;
}

const std::vector<Vec3> kVciQuadPos = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
const std::vector<uint16_t> kVciQuadIdx = {0, 1, 2, 0, 2, 3};

/* The four colours as glTF bytes of one component type: float (VEC3 / VEC4), normalized u8 (VEC4), normalized u16 (VEC3 / VEC4);
 * `want` is what an importer must give back (linear, nothing decoded). */
std::string vci_gltf_colours(int ct, int comps, std::vector<Vec4> &want) {
  std::string s;
  want.clear();
  for (int v = 0; v < 4; v++) {
    Vec4 w(kVciFloats[v][0], kVciFloats[v][1], kVciFloats[v][2], comps == 4 ? kVciFloats[v][3] : 1.0f);
    for (int k = 0; k < comps; k++) {
      const float f = kVciFloats[v][k];
      if (ct == 5126) vci_put<float>(s, f);
      else if (ct == 5121) {
        const uint8_t b = (uint8_t)std::lround(f * 255.0f);
        vci_put<uint8_t>(s, b);
        (k == 0 ? w.x : k == 1 ? w.y : k == 2 ? w.z : w.w) = b / 255.0f;
      }
      else {
        const uint16_t b = (uint16_t)std::lround(f * 65535.0f);
        vci_put<uint16_t>(s, b);
        (k == 0 ? w.x : k == 1 ? w.y : k == 2 ? w.z : w.w) = b / 65535.0f;
      }
    }
    want.push_back(w);
  }
  return s;
}

/* An ASCII FBX with one quad and a per-corner colour set (sRGB doubles, as FBX keeps them). */
std::string vci_fbx() {
  std::string s =
      "; FBX 7.4.0 project file\nFBXHeaderExtension:  {\n\tFBXHeaderVersion: 1003\n\tFBXVersion: 7400\n}\nObjects:  {\n"
      "\tGeometry: 100, \"Geometry::Quad\", \"Mesh\" {\n\t\tVertices: *12 {\n\t\t\ta: 0,0,0,1,0,0,1,1,0,0,1,0\n\t\t}\n"
      "\t\tPolygonVertexIndex: *4 {\n\t\t\ta: 0,1,2,-4\n\t\t}\n\t\tGeometryVersion: 124\n"
      "\t\tLayerElementColor: 0 {\n\t\t\tVersion: 101\n\t\t\tName: \"Col\"\n\t\t\tMappingInformationType: \"ByPolygonVertex\"\n"
      "\t\t\tReferenceInformationType: \"Direct\"\n\t\t\tColors: *16 {\n\t\t\t\ta: ";
  for (int v = 0; v < 4; v++)
    for (int k = 0; k < 4; k++) {
      char b[32];
      std::snprintf(b, sizeof b, "%s%.6f", (v || k) ? "," : "", kVciBytes[v][k] / 255.0);
      s += b;
    }
  s +=
      "\n\t\t\t}\n\t\t}\n\t\tLayer: 0 {\n\t\t\tVersion: 100\n\t\t\tLayerElement:  {\n\t\t\t\tType: \"LayerElementColor\"\n\t\t\t\tTypedIndex: 0\n\t\t\t}\n\t\t}\n"
      "\t}\n\tModel: 200, \"Model::Quad\", \"Mesh\" {\n\t\tVersion: 232\n\t}\n}\nConnections:  {\n\tC: \"OO\",100,200\n\tC: \"OO\",200,0\n}\n";
  return s;
}

}  // namespace

static void vertex_colour_input_tests() {
  /* ---------------------------------------------------------------- criterion 1: the colours show */
  test("vertex colour input: a white material with Vertex Color on shows a red / blue Point layer red and blue in the Shaded view; off, white", [] {
    VciView on(vci_pad(false), vci_mat(true));
    const auto l = on.px(-0.6f, 0.6f), r = on.px(0.6f, 0.6f);
    std::printf("    on: left %d %d %d, right %d %d %d\n", l[0], l[1], l[2], r[0], r[1], r[2]);
    CHECK(vci_red(l) && vci_blue(r));
    VciView off(vci_pad(false), vci_mat(false));
    const auto ol = off.px(-0.6f, 0.6f), orr = off.px(0.6f, 0.6f);
    std::printf("    off: left %d %d %d, right %d %d %d\n", ol[0], ol[1], ol[2], orr[0], orr[1], orr[2]);
    CHECK(vci_grey(ol) && vci_grey(orr) && vci_same_px(ol, orr, 12));
  });

  test("vertex colour input: the path tracer (F12) renders the same red / blue pad with Vertex Color on, white with it off or without the colours handed over", [] {
    MeshPtr pad = vci_pad(false);
    const std::vector<float> on = vci_pt(pad, vci_mat(true), true);
    const Vec3 a = vci_pix(on, 7), b = vci_pix(on, 25);
    std::printf("    on: col 7 %.3f %.3f %.3f, col 25 %.3f %.3f %.3f\n", a.x, a.y, a.z, b.x, b.y, b.z);
    CHECK((vci_pt_red(a, 0.5f) && vci_pt_blue(b, 0.5f)) || (vci_pt_blue(a, 0.5f) && vci_pt_red(b, 0.5f)));
    const std::vector<float> off = vci_pt(pad, vci_mat(false), true);
    const Vec3 c = vci_pix(off, 7), d = vci_pix(off, 25);
    std::printf("    flag off: %.3f %.3f %.3f | %.3f %.3f %.3f\n", c.x, c.y, c.z, d.x, d.y, d.z);
    CHECK(vci_pt_white(c, 0.5f) && vci_pt_white(d, 0.5f));
    const std::vector<float> none = vci_pt(pad, vci_mat(true), false);
    const Vec3 e = vci_pix(none, 7), f = vci_pix(none, 25);
    std::printf("    flag on, no colours in the PTObject: %.3f %.3f %.3f | %.3f %.3f %.3f\n", e.x, e.y, e.z, f.x, f.y, f.z);
    CHECK(vci_pt_white(e, 0.5f) && vci_pt_white(f, 0.5f));
  });

  test("vertex colour input: alpha under the cutout discards in the Shaded view and the path tracer (a Cutout material)", [] {
    /* Shaded: the red half has alpha 0; its pixel is the background (the same pixel with the pad switched off). */
    VciView on(vci_pad(false, 0.0f), vci_mat(true, true));
    const auto kept = on.px(0.6f, 0.6f), gone = on.px(-0.6f, 0.6f);
    on.t.pad->active = false;
    on.t.ed.mark_changed_for_test("VCI hide");
    on.frames(4);
    const auto bg = on.px(-0.6f, 0.6f);
    std::printf("    cutout alpha 0 half %d %d %d vs background %d %d %d; kept half %d %d %d\n", gone[0], gone[1], gone[2], bg[0], bg[1], bg[2], kept[0], kept[1],
                kept[2]);
    CHECK(vci_same_px(gone, bg, 4) && vci_blue(kept) && !vci_same_px(kept, bg, 20));
    /* Path tracer in a white world: the alpha-0 half shows the world (white), the blue half is blue; with the flag off both are white. */
    MeshPtr pad = vci_pad(false, 0.0f);
    const std::vector<float> pt = vci_pt(pad, vci_mat(true, true), true, 1.0f);
    const Vec3 a = vci_pix(pt, 7), b = vci_pix(pt, 25);
    std::printf("    PT cutout: col 7 %.3f %.3f %.3f, col 25 %.3f %.3f %.3f\n", a.x, a.y, a.z, b.x, b.y, b.z);
    CHECK((vci_pt_white(a, 1.0f) && vci_pt_blue(b, 1.0f)) || (vci_pt_blue(a, 1.0f) && vci_pt_white(b, 1.0f)));
    const std::vector<float> off = vci_pt(pad, vci_mat(false, true), true, 1.0f);
    CHECK(vci_pt_white(vci_pix(off, 7), 1.0f) && vci_pt_white(vci_pix(off, 25), 1.0f));
  });

  /* ---------------------------------------------------------------- criterion 2: a Corner layer's faces */
  test("vertex colour input: a Corner layer's two halves render their own colours in the Shaded view and the path tracer, away from the seam", [] {
    VciView v(vci_pad(true), vci_mat(true));
    const auto l = v.px(-0.6f, 0.6f), r = v.px(0.6f, 0.6f);
    std::printf("    Shaded: left %d %d %d, right %d %d %d\n", l[0], l[1], l[2], r[0], r[1], r[2]);
    CHECK(vci_red(l) && vci_blue(r));
    MeshPtr pad = vci_pad(true);
    const std::vector<float> pt = vci_pt(pad, vci_mat(true), true);
    const Vec3 a = vci_pix(pt, 7), b = vci_pix(pt, 25);
    std::printf("    PT: col 7 %.3f %.3f %.3f, col 25 %.3f %.3f %.3f\n", a.x, a.y, a.z, b.x, b.y, b.z);
    CHECK((vci_pt_red(a, 0.5f) && vci_pt_blue(b, 0.5f)) || (vci_pt_blue(a, 0.5f) && vci_pt_red(b, 0.5f)));
  });

  test("vertex colour input: a cube's six Corner-layer face colours give each render vertex its own face's colour; a seam inside a render vertex is the mean", [] {
    MeshPtr cube = primitives::cube();
    Mesh &m = *mesh_make_mutable(cube);
    ColorAttribute c = m.make_color("Faces", Dom::Corner, Vec4(1, 1, 1, 1));
    for (size_t f = 0; f < m.face_count(); f++)
      for (uint32_t k = m.face_offsets[f]; k < m.face_offsets[f + 1]; k++) c.rgba[k] = Vec4((float)(f + 1) / 8.0f, (float)(6 - f) / 8.0f, 0.25f, 1.0f);
    m.color_attributes.push_back(std::move(c));
    m.active_color = m.render_color = 0;
    m.touch();
    const RenderMesh &rm = m.render_mesh();
    const std::vector<Vec4> *cols = m.render_vertex_colors(rm);
    CHECK(cols && cols->size() == rm.positions.size());
    int wrong = 0, checked = 0;
    for (size_t k = 0; cols && k < rm.indices.size() && k < rm.tri_corner.size(); k++) {
      const uint32_t cn = rm.tri_corner[k];
      const Vec4 want = m.color_attributes[0].rgba[cn];
      const Vec4 got = (*cols)[rm.indices[k]];
      checked++;
      wrong += !vpt_near(got, want, 1e-5f);
    }
    std::printf("    cube: %zu render vertices, %d of %d triangle corners differ from their face's colour\n", rm.positions.size(), wrong, checked);
    CHECK(checked > 0 && wrong == 0);
    /* Two quads sharing a render vertex (flat, planar): the shared vertices get the mean of the two faces' colours. */
    MeshPtr two = vpt_two_quads();
    Mesh &t = *mesh_make_mutable(two);
    ColorAttribute tc = t.make_color("C", Dom::Corner, Vec4(1, 1, 1, 1));
    for (size_t f = 0; f < t.face_count(); f++)
      for (uint32_t k = t.face_offsets[f]; k < t.face_offsets[f + 1]; k++) tc.rgba[k] = f == 0 ? Vec4(1, 0, 0, 1) : Vec4(0, 0, 1, 1);
    t.color_attributes.push_back(std::move(tc));
    t.active_color = t.render_color = 0;
    t.touch();
    std::vector<Vec4> out;
    CHECK(render_vertex_colors_into(t, t.render_mesh(), out));
    int purple = 0, red = 0, blue = 0;
    for (const Vec4 &o : out) {
      purple += vpt_near(o, Vec4(0.5f, 0, 0.5f, 1), 1e-5f);
      red += vpt_near(o, Vec4(1, 0, 0, 1), 1e-5f);
      blue += vpt_near(o, Vec4(0, 0, 1, 1), 1e-5f);
    }
    std::printf("    two quads: %zu render vertices: %d red, %d blue, %d mean\n", out.size(), red, blue, purple);
    for (const Vec4 &o : out) std::printf("      render vertex colour %.3f %.3f %.3f %.3f\n", o.x, o.y, o.z, o.w);
    CHECK(out.size() == 6 && red == 2 && blue == 2 && purple == 2);
  });

  /* ---------------------------------------------------------------- criterion 3: no render mesh rebuild */
  test("vertex colour input: colorattr fill shows on the next frame and the render mesh is not rebuilt", [] {
    VciView v(vci_pad(false), vci_mat(true));
    CHECK(vci_red(v.px(-0.6f, 0.6f)));
    v.frames(3);
    const MeshCounts c0 = mesh_counts();
    v.t.ed.command("colorattr fill 0 1 0 1");
    v.t.ed.step_frame_headless();
    const MeshCounts d = mesh_counts() - c0;
    const auto l = v.px(-0.6f, 0.6f), r = v.px(0.6f, 0.6f);
    std::printf("    after fill: left %d %d %d, right %d %d %d; render meshes built whole %llu, moved in place %llu\n", l[0], l[1], l[2], r[0], r[1], r[2],
                (unsigned long long)d.full_builds, (unsigned long long)d.in_place);
    CHECK(vci_greenish(l) && vci_greenish(r));
    CHECK(d.full_builds == 0);  // (round 545b review: the in-place move of the render mesh is counted and printed above, not forbidden: only a whole rebuild is what the spec rules out)
  });

  test("vertex colour input: a Vertex Paint stroke shows on the next frame on a Vertex Color material and rebuilds no render mesh", [] {
    VPad t;  // Vertex Paint on, the layer white
    t.pad->get<MeshRenderer>()->materials = {vci_mat(true)};
    t.ed.command("shading shaded");
    t.ed.mark_changed_for_test("VCI stroke");
    for (int i = 0; i < 4; i++) t.ed.step_frame_headless();
    auto px = [&](float x, float z) {
      int sx = 0, sy = 0;
      CHECK(t.ed.project_to_window(t.pad->world_matrix().point(Vec3(x, 0, z)), sx, sy));
      const Image &fb = t.ed.main_fb_for_test();
      const uint32_t p = fb.pixels[(size_t)sy * (size_t)fb.width + (size_t)sx];
      return std::array<int, 3>{(int)((p >> 16) & 255), (int)((p >> 8) & 255), (int)(p & 255)};
    };
    const auto before = px(-0.5f, 0.5f);
    t.flat(0.25f, "draw", 1.0f);
    const MeshCounts c0 = mesh_counts();
    t.drag({Vec3(-0.5f, 0, 0.5f)});
    const MeshCounts d = mesh_counts() - c0;
    const auto after = px(-0.5f, 0.5f);
    std::printf("    before %d %d %d, after %d %d %d; render meshes built whole during the stroke: %llu\n", before[0], before[1], before[2], after[0], after[1],
                after[2], (unsigned long long)d.full_builds);
    CHECK(vci_grey(before) && vci_red(after) && d.full_builds == 0);
  });

  /* ---------------------------------------------------------------- criterion 4: the bake */
  test("vertex colour input: a texture bake's albedo carries the vertex colours (and stays white with the flag off)", [] {
    auto sample = [](const Mesh &m, const TextureBakeResult &r, size_t f) {
      Vec2 uv(0, 0);
      const float n = (float)(m.face_offsets[f + 1] - m.face_offsets[f]);
      for (uint32_t k = m.face_offsets[f]; k < m.face_offsets[f + 1]; k++) uv = uv + r.uvs[k] * (1.0f / n);
      const int x = std::max(0, std::min(r.size - 1, (int)(uv.x * (float)r.size))), y = std::max(0, std::min(r.size - 1, (int)((1.0f - uv.y) * (float)r.size)));
      return r.albedo[(size_t)y * (size_t)r.size + (size_t)x];
    };
    for (bool on : {true, false}) {
      MeshPtr pad = vci_pad(false);
      TextureBakeSettings s;
      s.size = 128;
      const TextureBakeResult r = bake_textures(*pad, Mat4::identity(), {vci_mat(on)}, s);
      int red = 0, blue = 0, white = 0, left = 0, right = 0;
      for (size_t f = 0; f < pad->face_count() && r.albedo.size() == (size_t)r.size * (size_t)r.size; f++) {
        float cx = 0.0f;
        for (uint32_t k = pad->face_offsets[f]; k < pad->face_offsets[f + 1]; k++) cx += pad->positions[pad->corner_verts[k]].x;
        cx /= (float)(pad->face_offsets[f + 1] - pad->face_offsets[f]);
        if (std::fabs(cx) < 0.3f) continue;
        const uint32_t p = sample(*pad, r, f);
        const int pr = (int)((p >> 16) & 255), pg = (int)((p >> 8) & 255), pb = (int)(p & 255);
        (cx < 0 ? left : right)++;
        red += pr > 245 && pg < 10 && pb < 10;
        blue += pb > 245 && pr < 10 && pg < 10;
        white += pr > 245 && pg > 245 && pb > 245;
      }
      std::printf("    flag %s: %d of %d left faces red, %d of %d right faces blue, %d white\n", on ? "on" : "off", red, left, blue, right, white);
      CHECK(r.size == 128 && left >= 10 && right >= 10);
      if (on) CHECK(red == left && blue == right);
      else CHECK(white == left + right);
    }
  });

  /* ---------------------------------------------------------------- criterion 5: the importers */
  test("vertex colour input: a PLY with uchar or float colours, ASCII or binary, imports them as the Col layer (bytes sRGB-decoded, alpha and floats as stored)", [] {
    int n = 0;
    for (bool binary : {false, true})
      for (bool uchar : {true, false})
        for (bool alpha : {true, false}) {
          const std::string path = fs::join(vci_dir(), "c" + std::to_string(n++) + ".ply");
          vci_write(path, vci_ply(binary, uchar, alpha));
          ImportResult res;
          const bool ok = import_ply_file(path, res);
          const Mesh *m = ok ? vci_first_mesh(res) : nullptr;
          const ColorAttribute *c = m ? vpt_find(*m, "Col") : nullptr;
          const float err = c ? vci_set_error(c->rgba, vci_ply_expected(uchar, alpha)) : 1e9f;
          std::printf("    %s %s%s: %s, worst colour error %.2g\n", binary ? "binary" : "ASCII", uchar ? "uchar" : "float", alpha ? "+alpha" : "", ok ? "imported" : res.error.c_str(),
                      err);
          CHECK(ok && m && c && c->domain == Dom::Point && m->color_fits(*c) && m->active_color == m->find_color("Col") && m->render_color == m->active_color);
          CHECK(err < 1e-4f);
        }
    /* A PLY without colours gets no layer. */
    const std::string path = fs::join(vci_dir(), "plain.ply");
    vci_write(path, vci_ply(false, true, false, 4, "none"));
    ImportResult res;
    CHECK(import_ply_file(path, res) && vci_first_mesh(res) && vci_first_mesh(res)->color_attributes.empty());
  });

  test("vertex colour input: glTF COLOR_0 as VEC3 or VEC4 float, normalized u8 and u16 imports as the Color layer, linear, as stored", [] {
    struct V {
      const char *name;
      int ct, comps;
      bool norm;
    };
    for (const V &v : {V{"f3", 5126, 3, false}, V{"f4", 5126, 4, false}, V{"u8", 5121, 4, true}, V{"u16_3", 5123, 3, true}, V{"u16_4", 5123, 4, true}}) {
      std::vector<Vec4> want;
      const std::string bytes = vci_gltf_colours(v.ct, v.comps, want);
      const std::string path = vci_gltf_file(std::string("col_") + v.name, kVciQuadPos, kVciQuadIdx, v.ct, v.comps, v.norm, bytes, 4);
      ImportResult res;
      const bool ok = import_gltf_file(path, res);
      const Mesh *m = ok ? vci_first_mesh(res) : nullptr;
      const ColorAttribute *c = m ? vpt_find(*m, "Color") : nullptr;
      const float err = c ? vci_set_error(c->rgba, want) : 1e9f;
      std::printf("    %s: %s, %zu colours, worst error %.2g\n", v.name, ok ? "imported" : res.error.c_str(), c ? c->rgba.size() : (size_t)0, err);
      CHECK(ok && m && c && c->domain == Dom::Point && m->color_fits(*c) && m->render_color == m->find_color("Color") && m->active_color == m->render_color);
      CHECK(err < 1e-5f);
    }
    ImportResult res;
    CHECK(import_gltf_file(vci_gltf_file("col_none", kVciQuadPos, kVciQuadIdx, 5126, 4, false, "", 0), res) && vci_first_mesh(res) &&
          vci_first_mesh(res)->color_attributes.empty());
  });

  test("vertex colour input: an FBX's first colour set imports as the Corner layer Col, sRGB-decoded (skipped when the importer doesn't read the hand-written file)", [] {
    const std::string path = fs::join(vci_dir(), "col.fbx");
    vci_write(path, vci_fbx());
    ImportResult res;
    const bool ok = import_fbx_file(path, res);
    const Mesh *m = ok ? vci_first_mesh(res) : nullptr;
    if (!m) {
      std::printf("    the hand-written ASCII FBX did not import (%s): fixture not accepted, FBX colours untested\n", res.error.c_str());
      return;
    }
    const ColorAttribute *c = vpt_find(*m, "Col");
    std::vector<Vec4> want = vci_ply_expected(true, true);  // the same four colours: sRGB -> linear, alpha as is
    const float err = c ? vci_set_error(c->rgba, want) : 1e9f;
    std::printf("    FBX: %zu corners, layer %s, worst error %.2g\n", m->corner_count(), c ? "Col" : "missing", err);
    CHECK(c && c->domain == Dom::Corner && m->color_fits(*c) && m->render_color == m->find_color("Col") && err < 1e-3f);
  });

  test("vertex colour input: colours round trip through Blendity's glTF (exactly) and PLY (within one byte step) exporters", [] {
    MeshPtr quads = vpt_two_quads();
    Mesh &m = *mesh_make_mutable(quads);
    const std::vector<Vec4> cols = {Vec4(0.1f, 0.2f, 0.3f, 1), Vec4(0.9f, 0.5f, 0.1f, 0.8f), Vec4(0, 0, 1, 1), Vec4(0.5f, 0.5f, 0.5f, 0.25f), Vec4(1, 1, 0, 1), Vec4(0.3f, 0.6f, 0.9f, 0.5f)};
    ColorAttribute c = m.make_color("Color", Dom::Point, Vec4(1, 1, 1, 1));
    c.rgba = cols;
    m.color_attributes.push_back(std::move(c));
    m.active_color = m.render_color = 0;
    m.touch();
    ExportItem it;
    it.name = "M";
    it.mesh = &m;
    it.world = Mat4::identity();
    {
      const std::string glb = export_gltf({it}, export_defaults((int)ExportFormat::GLB), true, "", nullptr);
      const std::string path = fs::join(vci_dir(), "round.glb");
      vci_write(path, glb);
      ImportResult res;
      const bool ok = import_gltf_file(path, res);
      const Mesh *im = ok ? vci_first_mesh(res) : nullptr;
      const ColorAttribute *ic = im ? vpt_find(*im, "Color") : nullptr;
      const float err = ic ? vci_set_error(ic->rgba, cols) : 1e9f;
      std::printf("    glTF: %s, %zu colours, worst error %.3g\n", ok ? "imported" : res.error.c_str(), ic ? ic->rgba.size() : (size_t)0, err);
      CHECK(ic && err <= 1.0f / 255.0f);
    }
    for (bool ascii : {true, false}) {
      ExportOptions o = export_defaults((int)ExportFormat::PLY);
      o.ascii = ascii;
      const std::string ply = export_ply({it}, o);
      const std::string path = fs::join(vci_dir(), ascii ? "round_a.ply" : "round_b.ply");
      vci_write(path, ply);
      ImportResult res;
      const bool ok = import_ply_file(path, res);
      const Mesh *im = ok ? vci_first_mesh(res) : nullptr;
      const ColorAttribute *ic = im ? vpt_find(*im, "Col") : nullptr;
      const float err = ic ? vci_set_error(vci_encode(ic->rgba), vci_encode(cols)) : 1e9f;  // compared as stored: sRGB bytes
      std::printf("    PLY %s: %s, %zu colours, worst error (sRGB) %.3g (one step %.3g)\n", ascii ? "ASCII" : "binary", ok ? "imported" : res.error.c_str(),
                  ic ? ic->rgba.size() : (size_t)0, err, 1.0f / 255.0f);
      CHECK(ic && err <= 1.0f / 255.0f + 1e-3f);
    }
  });

  /* ---------------------------------------------------------------- criterion 6: weld_exact */
  test("vertex colour input: a PLY with doubled positions welds the vertices of one colour and keeps apart those of two colours", [] {
    /* Triangles (A B C) and (A' C' D): A = A' (red), C and C' coincide but are yellow and cyan -> 5 vertices, not 4 or 6. */
    std::string s = "ply\nformat ascii 1.0\nelement vertex 6\nproperty float x\nproperty float y\nproperty float z\nproperty uchar red\nproperty uchar green\nproperty uchar blue\n"
                    "element face 2\nproperty list uchar int vertex_indices\nend_header\n"
                    "0 0 0 255 0 0\n1 0 0 0 255 0\n1 1 0 255 255 0\n0 0 0 255 0 0\n1 1 0 0 255 255\n0 1 0 0 0 255\n3 0 1 2\n3 3 4 5\n";
    const std::string path = fs::join(vci_dir(), "weld.ply");
    vci_write(path, s);
    ImportResult res;
    const bool ok = import_ply_file(path, res);
    const Mesh *m = ok ? vci_first_mesh(res) : nullptr;
    const ColorAttribute *c = m ? vpt_find(*m, "Col") : nullptr;
    std::vector<Vec4> want;
    for (int k = 0; k < 6; k++) {
      const int rgb[6][3] = {{255, 0, 0}, {0, 255, 0}, {255, 255, 0}, {255, 0, 0}, {0, 255, 255}, {0, 0, 255}};
      if (k == 3) continue;  // the welded copy
      want.push_back(Vec4(srgb_to_linear(rgb[k][0] / 255.0f), srgb_to_linear(rgb[k][1] / 255.0f), srgb_to_linear(rgb[k][2] / 255.0f), 1.0f));
    }
    std::printf("    PLY: %zu vertices (want 5), layer %s, error %.2g\n", m ? m->vert_count() : (size_t)0, c ? "kept" : "missing", c ? vci_set_error(c->rgba, want) : -1.0f);
    CHECK(m && c && m->vert_count() == 5 && m->color_fits(*c) && vci_set_error(c->rgba, want) < 1e-4f && vpt_valid(*m));
    /* The same through glTF (COLOR_0 per vertex, six vertices). */
    const std::vector<Vec3> pos = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 0, 0}, {1, 1, 0}, {0, 1, 0}};
    std::string bytes;
    for (int k = 0; k < 6; k++) {
      const float rgb[6][3] = {{1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}, {0, 1, 1}, {0, 0, 1}};
      for (int j = 0; j < 3; j++) vci_put<float>(bytes, rgb[k][j]);
    }
    ImportResult gr;
    const bool gok = import_gltf_file(vci_gltf_file("weld", pos, {0, 1, 2, 3, 4, 5}, 5126, 3, false, bytes, 6), gr);
    const Mesh *gm = gok ? vci_first_mesh(gr) : nullptr;
    const ColorAttribute *gc = gm ? vpt_find(*gm, "Color") : nullptr;
    std::printf("    glTF: %zu vertices (want 5)\n", gm ? gm->vert_count() : (size_t)0);
    CHECK(gm && gc && gm->vert_count() == 5 && gm->color_fits(*gc) && vpt_valid(*gm));
    /* weld_exact on a mesh directly: the layer renumbers with the vertices; active / render keep their layer by name. */
    Mesh d;
    for (const Vec3 &p : pos) d.add_vert(p);
    d.add_face({0, 1, 2});
    d.add_face({3, 4, 5});
    ColorAttribute other = d.make_color("Other", Dom::Point, Vec4(1, 1, 1, 1));
    ColorAttribute dc = d.make_color("Second", Dom::Point, Vec4(1, 1, 1, 1));
    const Vec4 cc[6] = {Vec4(1, 0, 0, 1), Vec4(0, 1, 0, 1), Vec4(1, 1, 0, 1), Vec4(1, 0, 0, 1), Vec4(0, 1, 1, 1), Vec4(0, 0, 1, 1)};
    for (int k = 0; k < 6; k++) dc.rgba[(size_t)k] = cc[k];  // ("Other" stays white: it welds both pairs)
    d.color_attributes.push_back(std::move(other));
    d.color_attributes.push_back(std::move(dc));
    d.active_color = 1, d.render_color = 1;
    weld_exact(d);
    std::printf("    weld_exact: %zu vertices, layers %zu, render layer %d\n", d.vert_count(), d.color_attributes.size(), d.render_color);
    CHECK(d.vert_count() == 5 && d.color_attributes.size() == 2 && d.color_fits(d.color_attributes[0]) && d.color_fits(d.color_attributes[1]) &&
          d.render_color >= 0 && d.color_attributes[(size_t)d.render_color].name == "Second" && d.active_color == d.render_color);
  });

  /* ---------------------------------------------------------------- criterion 7: robustness */
  test("vertex colour input: NaN, infinite and out-of-range colours render finite in the Shaded view and the path tracer", [] {
    MeshPtr pad = vci_pad(false);
    Mesh &m = *mesh_make_mutable(pad);
    ColorAttribute &c = m.color_attributes[0];
    const float nan = std::nanf(""), inf = std::numeric_limits<float>::infinity();
    c.rgba[3] = Vec4(nan, 0.5f, 0.5f, 1);
    c.rgba[10] = Vec4(0.5f, inf, nan, nan);
    c.rgba[20] = Vec4(-5, 1e6f, 2, -1);
    c.rgba[30] = Vec4(1e6f, 1e6f, 1e6f, 7);
    c.rgba[40] = Vec4(-inf, 0, 0, inf);
    m.touch();
    std::vector<Vec4> out;
    CHECK(render_vertex_colors_into(m, m.render_mesh(), out));
    bool finite = !out.empty();
    for (const Vec4 &o : out) finite = finite && vpt_finite4(o);
    std::printf("    render colours finite: %d (%zu render vertices)\n", (int)finite, out.size());
    CHECK(finite);
    for (bool cut : {false, true}) {
      const std::vector<float> pt = vci_pt(pad, vci_mat(true, cut), true);
      std::printf("    path tracer%s: %zu values, all finite %d\n", cut ? " (Cutout)" : "", pt.size(), (int)vci_finite_all(pt));
      CHECK(vci_finite_all(pt));
    }
    VciView v(pad, vci_mat(true));  // (a frame renders without a crash; the clean sides stay red and blue)
    CHECK(vci_red(v.px(-0.6f, 0.6f)) && vci_blue(v.px(0.6f, 0.6f)));
    VciView cutv(pad, vci_mat(true, true));
    cutv.frames(2);
    CHECK(cutv.t.ed.main_fb_for_test().pixels.size() > 0);
  });

  test("vertex colour input: a colour layer that no longer fits, and a mesh without colours, render white under a Vertex Color material", [] {
    MeshPtr short_layer = vci_pad(false);
    Mesh &m = *mesh_make_mutable(short_layer);
    m.color_attributes[0].rgba.resize(m.vert_count() - 5);  // a short array
    m.touch();
    std::vector<Vec4> out;
    CHECK(!render_vertex_colors_into(m, m.render_mesh(), out) && out.empty());
    MeshPtr bare = primitives::grid(2.0f, 8, 8);
    for (const MeshPtr &mesh : {short_layer, bare}) {
      const std::vector<float> pt = vci_pt(mesh, vci_mat(true), true);
      const Vec3 a = vci_pix(pt, 7), b = vci_pix(pt, 25);
      std::printf("    PT: %.3f %.3f %.3f | %.3f %.3f %.3f\n", a.x, a.y, a.z, b.x, b.y, b.z);
      CHECK(vci_pt_white(a, 0.5f) && vci_pt_white(b, 0.5f));
      VciView on(mesh, vci_mat(true)), off(mesh, vci_mat(false));
      const auto p = on.px(-0.6f, 0.6f), q = off.px(-0.6f, 0.6f);
      std::printf("    Shaded flag on %d %d %d, flag off %d %d %d\n", p[0], p[1], p[2], q[0], q[1], q[2]);
      CHECK(vci_grey(p) && vci_same_px(p, q, 3));
    }
    /* No render layer chosen (the colours turned off): white as well. */
    MeshPtr off_layer = vci_pad(false);
    mesh_make_mutable(off_layer)->render_color = -1;
    const std::vector<float> pt = vci_pt(off_layer, vci_mat(true), true);
    CHECK(vci_pt_white(vci_pix(pt, 7), 0.5f) && vci_pt_white(vci_pix(pt, 25), 0.5f));
  });

  test("vertex colour input: PLY and glTF colours that are NaN, out of range, short or half there import cleanly (or refuse), never a broken layer", [] {
    auto check_import = [](const char *what, bool ok, const ImportResult &res, bool expect_layer) {
      const Mesh *m = ok ? vci_first_mesh(res) : nullptr;
      std::printf("    %s: %s%s\n", what, ok ? "imported" : "refused: ", ok ? "" : res.error.c_str());
      if (!ok) {
        CHECK(!res.error.empty());
        return;
      }
      CHECK(m && m->vert_count() > 0 && vpt_valid(*m));
      for (const ColorAttribute &c : m ? m->color_attributes : std::vector<ColorAttribute>()) {
        bool good = true;
        for (const Vec4 &v : c.rgba) good = good && vpt_finite4(v) && v.x >= 0 && v.y >= 0 && v.z >= 0 && v.w >= 0 && v.w <= 1.0f;
        CHECK(good);
      }
      if (expect_layer) CHECK(m && !m->color_attributes.empty());
    };
    /* ASCII floats: nan, inf, negative, huge, alpha above 1. */
    {
      const std::string path = fs::join(vci_dir(), "wild.ply");
      vci_write(path, "ply\nformat ascii 1.0\nelement vertex 4\nproperty float x\nproperty float y\nproperty float z\nproperty float red\nproperty float green\nproperty float blue\nproperty float alpha\n"
                      "element face 2\nproperty list uchar int vertex_indices\nend_header\n"
                      "0 0 0 nan 0.5 0.5 1\n1 0 0 inf -inf 0 nan\n1 1 0 -3 1e30 2 9\n0 1 0 0.5 0.5 0.5 -1\n3 0 1 2\n3 0 2 3\n");
      ImportResult res;
      check_import("PLY NaN / inf / out of range", import_ply_file(path, res), res, false);
    }
    /* A colour property list that stops short: the last vertex line has only three of the seven values. */
    {
      const std::string path = fs::join(vci_dir(), "short.ply");
      vci_write(path, "ply\nformat ascii 1.0\nelement vertex 4\nproperty float x\nproperty float y\nproperty float z\nproperty uchar red\nproperty uchar green\nproperty uchar blue\n"
                      "element face 2\nproperty list uchar int vertex_indices\nend_header\n0 0 0 255 0 0\n1 0 0 0 255 0\n1 1 0 0 0 255\n0 1 0 255\n3 0 1 2\n3 0 2 3\n");
      ImportResult res;
      check_import("PLY last vertex short of values", import_ply_file(path, res), res, false);
    }
    /* A binary file cut off inside the colour bytes. */
    {
      std::string full = vci_ply(true, true, true);
      const std::string path = fs::join(vci_dir(), "cut.ply");
      vci_write(path, full.substr(0, full.size() - 30));
      ImportResult res;
      check_import("PLY binary cut short", import_ply_file(path, res), res, false);
    }
    /* Only red and green (no blue): no colour layer, the mesh still imports. */
    {
      const std::string path = fs::join(vci_dir(), "rg.ply");
      vci_write(path, vci_ply(false, true, false, 4, "red green"));
      ImportResult res;
      const bool ok = import_ply_file(path, res);
      check_import("PLY with red and green only", ok, res, false);
      CHECK(ok && vci_first_mesh(res) && vci_first_mesh(res)->color_attributes.empty());
    }
    /* glTF: COLOR_0 with fewer elements than POSITION. */
    {
      std::vector<Vec4> want;
      const std::string bytes = vci_gltf_colours(5126, 4, want).substr(0, 2 * 16);
      ImportResult res;
      check_import("glTF COLOR_0 with 2 of 4 elements", import_gltf_file(vci_gltf_file("short", kVciQuadPos, kVciQuadIdx, 5126, 4, false, bytes, 2), res), res, false);
    }
    /* glTF: NaN and huge floats. */
    {
      std::string bytes;
      const float nan = std::nanf("");
      for (int v = 0; v < 4; v++)
        for (int k = 0; k < 4; k++) vci_put<float>(bytes, (v + k) % 3 == 0 ? nan : (v + k) % 3 == 1 ? 1e30f : -4.0f);
      ImportResult res;
      check_import("glTF COLOR_0 NaN / huge / negative", import_gltf_file(vci_gltf_file("wild", kVciQuadPos, kVciQuadIdx, 5126, 4, false, bytes, 4), res), res, true);
    }
  });

  test("vertex colour input: the flat (Edit Mode) and the shaded render meshes keep their own colours - asking for one leaves the other's alone", [] {
    /* The review of round 545b: one cache for both variants refilled the vector a render job was reading. A smooth sphere
     * has fewer shaded render vertices than flat ones, so the two lists differ in size. */
    MeshPtr sphere = primitives::uv_sphere(0.5f, 12, 8);
    Mesh &m = *sphere;
    m.smooth = true;
    ColorAttribute c = m.make_color("Color", ColorAttribute::Domain::Point, Vec4(1, 1, 1, 1));
    for (size_t v = 0; v < m.vert_count(); v++) c.rgba[v] = Vec4(m.positions[v].y + 0.5f, 0.2f, 0.3f, 1.0f);
    m.color_attributes.push_back(std::move(c));
    m.active_color = m.render_color = 0;
    m.touch();
    const RenderMesh &shaded = m.render_mesh(false), &flat = m.render_mesh(true);
    const std::vector<Vec4> *a = m.render_vertex_colors(shaded);
    CHECK(a != nullptr);
    if (!a) return;
    const std::vector<Vec4> before = *a;
    const std::vector<Vec4> *b = m.render_vertex_colors(flat);
    std::printf("    shaded %zu render vertices, flat %zu; the shaded list kept: %d, at its own address: %d\n", shaded.positions.size(), flat.positions.size(),
                (int)(a->size() == before.size() && std::memcmp(a->data(), before.data(), before.size() * sizeof(Vec4)) == 0), (int)(a != b));
    CHECK(b != nullptr && a != b && a->size() == shaded.positions.size() && b->size() == flat.positions.size());
    CHECK(std::memcmp(a->data(), before.data(), before.size() * sizeof(Vec4)) == 0);
    CHECK(m.render_vertex_colors(shaded) == a);  // (a hit: the same list, not refilled)
  });
}

/* ================================================================ round 545b2 (task 0537): what goes to the card, pinned.
 * The card's own pictures are in test_gpu.cpp (group gpu); this one needs no card. */
static void vertex_colour_card_packing_tests() {
  test("vertex colour on the card (task 0537): the path tracer's vertex list carries the colour in the spare lanes - r p.w, g n.w, b uv.z, a uv.w; white without colours, with a layer that doesn't fit, and a NaN or HDR value clamped", [] {
    MeshPtr pad = vci_pad(false);
    const RenderMesh &rm = pad->render_mesh_tangents();
    std::vector<Vec4> cols;
    CHECK(render_vertex_colors_into(*pad, rm, cols) && cols.size() == rm.positions.size());
    std::vector<MaterialPtr> mats{vci_mat(true)};
    Environment e;
    e.mode = Environment::Color;
    e.color = {0.5f, 0.5f, 0.5f};
    /* The scene the card is sent, built with `c` as the object's colours. */
    auto sent = [&](const std::vector<Vec4> *c) {
      PTObject o;
      o.mesh = &rm;
      o.model = Mat4::identity();
      o.materials = &mats;
      o.colors = c;
      PathTracer pt;
      pt.build({o}, {}, e);
      gpu::Scene s;
      CHECK(pt.build_gpu_scene_fit(s, 0, nullptr, false));
      return s.verts;
    };
    const std::vector<gpu::GVertex> good = sent(&cols);
    CHECK(good.size() == rm.positions.size());
    size_t wrong = 0, moved = 0;
    for (size_t v = 0; v < good.size() && v < cols.size(); v++) {
      const gpu::GVertex &g = good[v];
      wrong += g.p[3] != cols[v].x || g.n[3] != cols[v].y || g.uv[2] != cols[v].z || g.uv[3] != cols[v].w;
      moved += g.p[0] != rm.positions[v].x || g.p[1] != rm.positions[v].y || g.p[2] != rm.positions[v].z || g.n[0] != rm.normals[v].x ||
               g.n[1] != rm.normals[v].y || g.n[2] != rm.normals[v].z || (v < rm.uvs.size() && (g.uv[0] != rm.uvs[v].x || g.uv[1] != rm.uvs[v].y));
    }
    std::printf("    %zu vertices: %zu with the wrong colour lanes, %zu with a position / normal / uv changed\n", good.size(), wrong, moved);
    CHECK(wrong == 0 && moved == 0);
    bool both = false, red = false, blue = false;  // (the pad really has both colours in the lanes)
    for (const gpu::GVertex &g : good) red = red || (g.p[3] == 1 && g.uv[2] == 0), blue = blue || (g.p[3] == 0 && g.uv[2] == 1);
    both = red && blue;
    CHECK(both);
    /* Without colours, and with a list of the wrong size: every vertex white. */
    std::vector<Vec4> short_list(cols.begin(), cols.end() - 1);
    for (const std::vector<gpu::GVertex> &v : {sent(nullptr), sent(&short_list)}) {
      size_t not_white = 0;
      for (const gpu::GVertex &g : v) not_white += g.p[3] != 1 || g.n[3] != 1 || g.uv[2] != 1 || g.uv[3] != 1;
      CHECK(v.size() == good.size() && not_white == 0);
    }
    /* NaN (-> white lane) and out-of-range values (clamped to 0..1). */
    std::vector<Vec4> odd = cols;
    odd[0] = Vec4(std::numeric_limits<float>::quiet_NaN(), 2.0f, -1.0f, 0.5f);
    const std::vector<gpu::GVertex> o = sent(&odd);
    std::printf("    NaN / 2 / -1 / 0.5 in the lanes: %g %g %g %g\n", o[0].p[3], o[0].n[3], o[0].uv[2], o[0].uv[3]);
    CHECK(o[0].p[3] == 1.0f && o[0].n[3] == 1.0f && o[0].uv[2] == 0.0f && o[0].uv[3] == 0.5f);
    CHECK(std::isfinite(o[0].p[3] + o[0].n[3] + o[0].uv[2] + o[0].uv[3]));
  });
}

void run_vertex_paint_tests() {
  /* ================================================================ 1. the operator table */

  test("colour attribute: a Point layer is exact through copy-on-write, a Mesh copy and undo; editing the copy leaves the original", [] {
    MeshPtr a = vpt_flat(true, false);
    const std::vector<Vec4> before = a->color_attributes[0].rgba;
    MeshPtr b = a;  // shared: mesh_make_mutable must copy
    Mesh &w = *mesh_make_mutable(b);
    CHECK(b.get() != a.get());
    CHECK(w.color_attributes.size() == 1 && vpt_same(w.color_attributes[0].rgba, before) && w.active_color == 0 && w.render_color == 0);
    w.color_attributes[0].rgba[3] = Vec4(0, 0, 0, 0);
    w.touch();
    const Mesh c = *b;  // the copy constructor keeps the layer, its domain and name
    std::printf("    original layer %zu colours, edited copy differs at 3: %d, original intact: %d\n", a->color_attributes[0].rgba.size(),
                (int)(c.color_attributes[0].rgba[3].x == 0.0f), (int)(vpt_same(a->color_attributes[0].rgba, before)));
    CHECK(vpt_same(a->color_attributes[0].rgba, before));
    CHECK(c.color_attributes.size() == 1 && c.color_attributes[0].name == "P" && c.color_attributes[0].rgba[3].x == 0.0f && c.color_fits(c.color_attributes[0]));
    /* A stroke then Undo: the layer goes back to before the stroke. */
    VPad t;
    t.flat(0.3f, "draw", 1.0f);
    t.drag({Vec3(0, 0, 0)});
    CHECK(t.at({0, 0, 0}).y < 0.1f);
    t.ed.command("undo");
    t.ed.step_frame_headless();
    std::printf("    after Undo of a stroke: %.3f %.3f %.3f\n", t.at({0, 0, 0}).x, t.at({0, 0, 0}).y, t.at({0, 0, 0}).z);
    CHECK(vpt_near(t.at({0, 0, 0}), Vec4(1, 1, 1, 1), 1e-6f));
  });

  test("colour attribute: Mirror and Array modifiers carry a Point layer exactly, the copies with their source's colours", [] {
    Scene s;
    GameObject *g = s.create("G");
    MeshPtr base = primitives::grid(2.0f, 4, 4);
    for (Vec3 &p : base->positions) p.x += 1.0f;  // x in [0, 2]: the mirror copy lands at [-2, 0]
    for (size_t v = 0; v < base->vert_count(); v++) base->positions[v] = base->positions[v];
    ColorAttribute c = base->make_color("Tint", Dom::Point, Vec4(1, 1, 1, 1));
    for (size_t v = 0; v < c.rgba.size(); v++) c.rgba[v] = Vec4(base->positions[v].x * 0.5f, base->positions[v].z * 0.5f + 0.5f, 0.1f, 1.0f);
    base->color_attributes.push_back(c);
    base->active_color = base->render_color = 0;
    g->add<MeshFilter>()->mesh = base;
    auto *mir = g->add<MirrorModifier>();
    mir->x = true;
    mir->merge = false;
    const Mesh *m = g->evaluated_mesh();
    CHECK(m && m->vert_count() == 2 * base->vert_count());
    float worst = 0.0f;
    bool fits = m && m->color_attributes.size() == 1 && m->color_fits(m->color_attributes[0]);
    CHECK(fits);
    if (fits)
      for (size_t i = 0; i < m->vert_count(); i++) {
        const Vec3 p = m->positions[i];
        const Vec4 want(std::fabs(p.x) * 0.5f, p.z * 0.5f + 0.5f, 0.1f, 1.0f);
        const Vec4 got = m->color_attributes[0].rgba[i];
        worst = std::max(worst, std::max(std::fabs(got.x - want.x), std::fabs(got.y - want.y)));
      }
    std::printf("    Mirror X: %zu vertices, worst colour error %g\n", m ? m->vert_count() : 0, worst);
    CHECK(worst < 1e-6f);
    g->remove_component(mir);
    auto *arr = g->add<ArrayModifier>();
    arr->count = 3;
    m = g->evaluated_mesh();
    const size_t n = base->vert_count();
    CHECK(m && m->vert_count() == 3 * n);
    fits = m && m->color_attributes.size() == 1 && m->color_fits(m->color_attributes[0]);
    CHECK(fits);
    float diff = 0.0f, shift = 0.0f;
    if (fits)
      for (size_t k = 1; k < 3; k++)
        for (size_t i = 0; i < n; i++) {
          const Vec4 a = m->color_attributes[0].rgba[i], b = m->color_attributes[0].rgba[i + k * n];
          diff = std::max(diff, std::fabs(a.x - b.x) + std::fabs(a.y - b.y) + std::fabs(a.z - b.z));
          shift = std::max(shift, std::fabs(m->positions[i + k * n].x - m->positions[i].x - 2.0f * (float)k));  // (the copies are where the order says)
        }
    std::printf("    Array x3: %zu vertices, copies' worst colour difference %g (copy offset check %g)\n", m ? m->vert_count() : 0, diff, shift);
    CHECK(diff == 0.0f && shift < 1e-4f);
  });

  test("colour attribute: Delete and Dissolve keep every surviving vertex's colour exactly, a Point layer follows the renumbering", [] {
    for (const char *op : {"delete", "dissolve_vertices"}) {
      Editor ed;
      vpt_setup(ed, vpt_flat(true, false));
      const size_t nv = vpt_grid(ed).vert_count();
      vpt_op(ed, op, "some");
      const Mesh &m = vpt_grid(ed);
      float worst = 0.0f;
      const bool ok = vpt_valid(m) && m.color_attributes.size() == 1 && m.color_attributes[0].name == "P";
      if (ok)
        for (size_t i = 0; i < m.vert_count(); i++) {
          const Vec4 got = m.color_attributes[0].rgba[i], want = vpt_f(m.positions[i]);
          worst = std::max(worst, std::max(std::fabs(got.x - want.x), std::fabs(got.y - want.y)));
        }
      std::printf("    %s: %zu -> %zu vertices, layer kept %d, worst colour error %g\n", op, nv, m.vert_count(), (int)ok, worst);
      CHECK(ok && m.vert_count() < nv && worst < 1e-6f);
    }
  });

  test("colour attribute: Join matches layers by name (a side without one takes white), Separate hands each part its own vertices' colours", [] {
    Editor ed;
    ed.init_headless(800, 600);
    GameObject *a = ed.scene().create("A");
    GameObject *b = ed.scene().create("B");
    MeshPtr am = primitives::cube(1.0f), bm = primitives::cube(1.0f);
    const size_t na = am->vert_count();
    am->color_attributes.push_back(am->make_color("Shared", Dom::Point, Vec4(1, 0, 0, 1)));
    am->color_attributes.push_back(am->make_color("OnlyA", Dom::Point, Vec4(0, 0, 1, 1)));
    bm->color_attributes.push_back(bm->make_color("Shared", Dom::Point, Vec4(0, 1, 0, 1)));
    bm->color_attributes.push_back(bm->make_color("OnlyB", Dom::Point, Vec4(0, 1, 1, 1)));
    am->active_color = am->render_color = 0;
    bm->active_color = bm->render_color = 0;
    a->add<MeshFilter>()->mesh = am;
    a->add<MeshRenderer>();
    b->add<MeshFilter>()->mesh = bm;
    b->add<MeshRenderer>();
    b->set_world_position({3, 0, 0});
    ed.mark_changed_for_test("VP join");
    ed.step_frame_headless();
    ed.command("select B");
    ed.command("selectadd A");  // A is active: it keeps the result
    const size_t log0 = vpt_log_mark();
    ed.command("join");
    ed.step_frame_headless();
    ed.step_frame_headless();
    const Mesh *j = ed.scene().find_by_name("A")->mesh_filter()->mesh.get();
    CHECK(j && vpt_valid(*j) && j->vert_count() == na + bm->vert_count());
    /* Every layer was joined: nothing is reported dropped (the review of round 545 found a Join that said so falsely). */
    const std::string said = vpt_log_since(log0);
    std::printf("    a dropped-layer warning after the Join: %d\n", (int)(said.find("dropped") != std::string::npos));
    CHECK(said.find("dropped") == std::string::npos);
    const ColorAttribute *sh = j ? vpt_find(*j, "Shared") : nullptr, *oa = j ? vpt_find(*j, "OnlyA") : nullptr, *ob = j ? vpt_find(*j, "OnlyB") : nullptr;
    CHECK(sh && oa && ob);
    if (sh && oa && ob) {
      std::printf("    joined: Shared %.0f%.0f%.0f / %.0f%.0f%.0f, OnlyA after A white %d, OnlyB before B white %d\n", sh->rgba.front().x, sh->rgba.front().y,
                  sh->rgba.front().z, sh->rgba.back().x, sh->rgba.back().y, sh->rgba.back().z, (int)vpt_near(oa->rgba.back(), Vec4(1, 1, 1, 1), 0),
                  (int)vpt_near(ob->rgba.front(), Vec4(1, 1, 1, 1), 0));
      CHECK(vpt_near(sh->rgba.front(), Vec4(1, 0, 0, 1), 0) && vpt_near(sh->rgba.back(), Vec4(0, 1, 0, 1), 0));  // the first mesh's vertices first
      CHECK(vpt_near(oa->rgba.front(), Vec4(0, 0, 1, 1), 0) && vpt_near(oa->rgba.back(), Vec4(1, 1, 1, 1), 0));
      CHECK(vpt_near(ob->rgba.front(), Vec4(1, 1, 1, 1), 0) && vpt_near(ob->rgba.back(), Vec4(0, 1, 1, 1), 0));
    }
    /* Separate by loose parts: each part keeps its own vertices' colours (the joined cubes are two parts). */
    ed.command("separate loose");
    ed.step_frame_headless();
    ed.step_frame_headless();
    int parts = 0, right = 0;
    ed.scene().for_each([&](GameObject &o) {
      const MeshFilter *mf = o.mesh_filter();
      if (!mf || !mf->mesh || mf->mesh->find_color("Shared") < 0) return;
      parts++;
      const Mesh &pm = *mf->mesh;
      const ColorAttribute &c = *vpt_find(pm, "Shared");
      bool red = true, green = true;
      for (const Vec4 &v : c.rgba) red = red && vpt_near(v, Vec4(1, 0, 0, 1), 0), green = green && vpt_near(v, Vec4(0, 1, 0, 1), 0);
      right += vpt_valid(pm) && (red != green) && pm.active_color >= 0;
    });
    std::printf("    separated into %d parts, %d with exactly one source's colour and a valid layer\n", parts, right);
    CHECK(parts == 2 && right == 2);
  });

  test("colour attribute: Subdivision and Bevel modifiers interpolate a Point layer by position (new vertices on the surface carry the surface's colour)", [] {
    Scene s;
    GameObject *g = s.create("Pad");
    MeshPtr base = vpt_flat(true, false, 4);
    g->add<MeshFilter>()->mesh = base;
    auto *sub = g->add<SubdivisionSurface>();
    sub->levels = 2;
    const Mesh *m = g->evaluated_mesh();
    bool fits = m && m->color_attributes.size() == 1 && m->color_fits(m->color_attributes[0]) && m->vert_count() > base->vert_count();
    CHECK(fits);
    float worst = 0.0f;
    if (fits)
      for (size_t i = 0; i < m->vert_count(); i++) {
        const Vec4 got = m->color_attributes[0].rgba[i], want = vpt_f(m->positions[i]);
        worst = std::max(worst, std::max(std::fabs(got.x - want.x), std::fabs(got.y - want.y)));
      }
    std::printf("    Subdivision x2 on a flat pad: %zu -> %zu vertices, worst colour error %g\n", base->vert_count(), m ? m->vert_count() : 0, worst);
    CHECK(worst < 2e-3f);
    /* Bevel on a cube whose colour is its position (a linear function: exact on every face). */
    Scene s2;
    GameObject *c = s2.create("Cube");
    MeshPtr cube = primitives::cube(1.0f);
    ColorAttribute ca = cube->make_color("Pos", Dom::Point, Vec4(1, 1, 1, 1));
    for (size_t v = 0; v < ca.rgba.size(); v++) ca.rgba[v] = Vec4(cube->positions[v].x + 0.5f, cube->positions[v].y + 0.5f, cube->positions[v].z + 0.5f, 1.0f);
    cube->color_attributes.push_back(ca);
    cube->active_color = cube->render_color = 0;
    c->add<MeshFilter>()->mesh = cube;
    auto *bev = c->add<BevelModifier>();
    bev->width = 0.1f;
    bev->segments = 1;
    bev->limit_method = 0;
    const Mesh *bm = c->evaluated_mesh();
    fits = bm && bm->color_attributes.size() == 1 && bm->color_fits(bm->color_attributes[0]) && bm->vert_count() > cube->vert_count();
    CHECK(fits);
    worst = 0.0f;
    if (fits)
      for (size_t i = 0; i < bm->vert_count(); i++) {
        const Vec4 got = bm->color_attributes[0].rgba[i];
        const Vec3 p = bm->positions[i];
        worst = std::max(worst, std::max(std::fabs(got.x - (p.x + 0.5f)), std::max(std::fabs(got.y - (p.y + 0.5f)), std::fabs(got.z - (p.z + 0.5f)))));
      }
    std::printf("    Bevel on a cube: %zu -> %zu vertices, worst colour error %g\n", cube->vert_count(), bm ? bm->vert_count() : 0, worst);
    CHECK(worst < 0.02f);
  });

  test("colour attribute: Extrude and Loop Cut keep a Point layer, the new vertices interpolated by position", [] {
    struct Case { const char *op; const char *select; };
    for (const Case &c : {Case{"extrude", "faces"}, Case{"loopcut", "edge"}, Case{"subdivide_edges", "all"}, Case{"inset", "faces"}}) {
      Editor ed;
      vpt_setup(ed, vpt_flat(true, false));
      const size_t nv = vpt_grid(ed).vert_count();
      vpt_op(ed, c.op, c.select);
      const Mesh &m = vpt_grid(ed);
      float worst = 0.0f;
      const bool ok = vpt_valid(m) && m.color_attributes.size() == 1;
      if (ok)
        for (size_t i = 0; i < m.vert_count(); i++) {
          const Vec4 got = m.color_attributes[0].rgba[i], want = vpt_f(m.positions[i]);
          worst = std::max(worst, std::max(std::fabs(got.x - want.x), std::fabs(got.y - want.y)));
        }
      std::printf("    %s: %zu -> %zu vertices, layer kept %d, worst colour error %g\n", c.op, nv, m.vert_count(), (int)ok, worst);
      CHECK(ok && worst < 2e-3f);
    }
  });

  test("colour attribute: Flip Normals keeps a Corner layer on its corners, and the UVs come through Flip exactly as before", [] {
    auto make = [](bool colours) {
      MeshPtr m = primitives::grid(2.0f, 4, 4);
      m->uvs.resize(m->corner_verts.size());
      for (size_t k = 0; k < m->uvs.size(); k++) m->uvs[k] = Vec2((float)k * 0.01f, (float)k * 0.02f + 0.5f);
      if (colours) vpt_add_layer(*m, "Seam", Dom::Corner);
      return m;
    };
    const MeshPtr plain = make(false), coloured = make(true);
    const std::vector<Vec2> uv0 = plain->uvs;
    const Mesh orig = *coloured;
    /* The reference: what Flip does to the UVs of a mesh with no colour layer. */
    MeshPtr ref = std::make_shared<Mesh>(*plain);
    meshops::flip_normals(*ref);
    MeshPtr both = std::make_shared<Mesh>(*coloured);
    meshops::flip_normals(*both);
    bool uv_same = ref->uvs.size() == both->uvs.size(), uv_reversed = true;
    for (size_t i = 0; uv_same && i < ref->uvs.size(); i++) uv_same = std::memcmp(&ref->uvs[i], &both->uvs[i], sizeof(Vec2)) == 0;
    for (size_t f = 0; f < plain->face_count(); f++) {  // each face's corners turned around, as before this round
      const uint32_t a = plain->face_offsets[f], n = plain->face_offsets[f + 1] - a;
      for (uint32_t i = 0; i < n; i++) uv_reversed = uv_reversed && std::memcmp(&ref->uvs[a + i], &uv0[a + n - 1 - i], sizeof(Vec2)) == 0;
    }
    std::printf("    UVs after Flip: with colour layer == without %d, each face's corners reversed %d\n", (int)uv_same, (int)uv_reversed);
    CHECK(uv_same && uv_reversed);
    /* The Corner layer: each (face, vertex) keeps its colour; the layer still fits the flipped corners. */
    CHECK(both->color_attributes.size() == 1 && both->color_fits(both->color_attributes[0]));
    size_t moved = 0, checked = 0;
    for (size_t f = 0; f < orig.face_count() && both->color_attributes.size() == 1; f++)
      for (uint32_t k = orig.face_offsets[f]; k < orig.face_offsets[f + 1]; k++)
        for (uint32_t k2 = both->face_offsets[f]; k2 < both->face_offsets[f + 1]; k2++)
          if (both->corner_verts[k2] == orig.corner_verts[k]) {
            checked++;
            moved += !vpt_near(both->color_attributes[0].rgba[k2], orig.color_attributes[0].rgba[k], 0);
          }
    std::printf("    Flip Normals: %zu corners checked, %zu changed colour\n", checked, moved);
    CHECK(checked == orig.corner_count() && moved == 0);
    /* Flipping a subset (Edit Mode's Flip) does the same for its faces. */
    MeshPtr part = std::make_shared<Mesh>(*coloured);
    std::vector<uint8_t> sel(part->face_count(), 0);
    sel[0] = sel[5] = 1;
    meshops::flip_faces(*part, sel);
    bool same = part->color_attributes.size() == 1 && part->color_fits(part->color_attributes[0]);
    for (size_t f = 0; same && f < orig.face_count(); f++)
      for (uint32_t k = orig.face_offsets[f]; k < orig.face_offsets[f + 1]; k++)
        for (uint32_t k2 = part->face_offsets[f]; k2 < part->face_offsets[f + 1]; k2++)
          if (part->corner_verts[k2] == orig.corner_verts[k]) same = same && vpt_near(part->color_attributes[0].rgba[k2], orig.color_attributes[0].rgba[k], 0);
    CHECK(same);
    /* And as the editor's Normals > Flip does it (undo commit included). */
    Editor ed;
    vpt_setup(ed, std::make_shared<Mesh>(*coloured));
    ed.command("meshop flip");
    ed.step_frame_headless();
    ed.step_frame_headless();
    const Mesh &fm = vpt_grid(ed);
    std::printf("    editor Flip: Corner layer kept %d, UVs kept %d\n", (int)(fm.color_attributes.size() == 1), (int)(fm.uvs.size() == ref->uvs.size()));
    CHECK(vpt_valid(fm) && fm.color_attributes.size() == 1 && vpt_same2(fm.uvs, ref->uvs));
  });

  test("colour attribute: an edit that rebuilds the corners drops a Corner layer whole (a Point layer stays), names it in the log and the Inspector, and OK dismisses it", [] {
    Editor ed;
    vpt_setup(ed, vpt_flat(true, true));
    ed.record_buttons_for_test(true);
    const size_t mark = vpt_log_mark();
    vpt_op(ed, "extrude", "faces");
    const Mesh &m = vpt_grid(ed);
    const std::string log = vpt_log_since(mark);
    std::printf("    after Extrude: layers %zu (Seam %d, P %d), log mentions Seam %d\n", m.color_attributes.size(), (int)(vpt_find(m, "Seam") != nullptr),
                (int)(vpt_find(m, "P") != nullptr), (int)(log.find("Seam") != std::string::npos));
    CHECK(vpt_valid(m) && !vpt_find(m, "Seam") && vpt_find(m, "P"));
    CHECK(log.find("Seam") != std::string::npos);
    bool note = false;
    Recti r;
    for (int i = 0; i < 4 && !note; i++) {
      ed.step_frame_headless();
      note = ed.button_rect_for_test("vp:DroppedOK", r);
    }
    std::printf("    Inspector shows the dropped-layer note: %d\n", (int)note);
    CHECK(note);
    if (note)
      for (R34ET ty : {R34ET::MouseMove, R34ET::MouseDown, R34ET::MouseUp}) ed.step_frame_headless({r34_mouse(ty, r.x + r.w / 2, r.y + r.h / 2)});
    bool gone = false;
    for (int i = 0; i < 4; i++) {
      ed.step_frame_headless();
      gone = !ed.button_rect_for_test("vp:DroppedOK", r);
    }
    CHECK(gone);
    /* Undo brings the layer back (the snapshot before the edit has it). */
    ed.command("undo");
    ed.step_frame_headless();
    const Mesh &back = vpt_grid(ed);
    std::printf("    after Undo: Seam back %d\n", (int)(vpt_find(back, "Seam") != nullptr));
    CHECK(vpt_valid(back) && vpt_find(back, "Seam") && vpt_find(back, "P"));
  });

  test("colour attribute: Loop Cut, Dissolve, Delete and a modifier never leave a Corner layer on the wrong corners; a Corner layer that survives belongs to unchanged corners", [] {
    struct Case { const char *op; const char *select; bool must_drop; };
    const Case cases[] = {{"loopcut", "edge", true}, {"dissolve_vertices", "some", true}, {"delete", "some", true}, {"extrude", "faces", true},
                          {"split", "subset", true}, {"subdivide_edges", "all", true}, {"inset", "faces", true}, {"poke", "faces", true},
                          {"triangulate_faces", "faces", true}, {"edge_split", "some", false}, {"merge_distance", "all", false}, {"bevel", "some", false},
                          {"duplicate", "faces", false}, {"dissolve_faces", "faces", false}, {"tris_to_quads", "all", false}, {"collapse", "edge", false},
                          {"connect", "some", false}};
    int bad = 0;
    for (const Case &c : cases) {
      Editor ed;
      MeshPtr base = vpt_flat(true, true);
      vpt_setup(ed, base);
      const std::vector<uint32_t> corners0 = base->corner_verts;
      vpt_op(ed, c.op, c.select);
      const Mesh &m = vpt_grid(ed);
      const ColorAttribute *seam = vpt_find(m, "Seam");
      bool ok = vpt_valid(m) && vpt_find(m, "P") && vpt_find(m, "P")->rgba.size() == m.vert_count();
      if (seam) ok = ok && m.corner_verts == corners0;  // it may only stay on the very corners it was made for
      if (c.must_drop) ok = ok && !seam;
      if (!ok) std::printf("    %s: BAD (Corner layer kept %d, valid %d)\n", c.op, (int)(seam != nullptr), (int)vpt_valid(m));
      bad += !ok;
    }
    std::printf("    %zu operators: %d bad\n", sizeof(cases) / sizeof(cases[0]), bad);
    CHECK(bad == 0);
    /* The Subdivision modifier builds a new mesh: its result carries no Corner layer (and a Point one by position). */
    Scene s;
    GameObject *g = s.create("G");
    g->add<MeshFilter>()->mesh = vpt_flat(true, true, 4);
    g->add<SubdivisionSurface>()->levels = 1;
    const Mesh *e = g->evaluated_mesh();
    CHECK(e && vpt_valid(*e) && !vpt_find(*e, "Seam") && vpt_find(*e, "P"));
    std::printf("    Subdivision modifier: Corner layer gone %d, Point layer kept %d\n", (int)(e && !vpt_find(*e, "Seam")), (int)(e && vpt_find(*e, "P") != nullptr));
  });

  test("colour attribute: an edit that keeps the corner count but rebuilds the corners (Split) drops the Corner layer, never misplacing it", [] {
    Editor ed;
    MeshPtr base = vpt_flat(false, true, 4);
    const size_t corners = base->corner_count();
    const std::vector<uint32_t> c0 = base->corner_verts;
    vpt_setup(ed, base);
    vpt_op(ed, "split", "subset");
    const Mesh &m = vpt_grid(ed);
    std::printf("    Split: corners %zu -> %zu, corner list changed %d, Corner layer kept %d\n", corners, m.corner_count(), (int)(m.corner_verts != c0),
                (int)(m.color_attributes.size() > 0));
    CHECK(m.corner_count() == corners);  // (the setup: the same count)
    CHECK(m.corner_verts != c0);
    CHECK(vpt_valid(m) && m.color_attributes.empty());
    /* The rule itself: two faces swap their corners (same count, other vertices). */
    Mesh q = *primitives::grid(2.0f, 2, 2);
    vpt_add_layer(q, "Seam", Dom::Corner);
    CHECK(q.color_fits(q.color_attributes[0]));
    for (uint32_t i = 0; i < 4; i++) std::swap(q.corner_verts[i], q.corner_verts[4 + i]);
    CHECK(q.corner_verts.size() == q.color_attributes[0].rgba.size());
    CHECK(!q.color_fits(q.color_attributes[0]));
    q.sync_attributes();
    std::printf("    swapped faces: layers left %zu, dropped names %zu\n", q.color_attributes.size(), q.dropped_colors.size());
    CHECK(q.color_attributes.empty() && q.dropped_colors.size() == 1 && q.dropped_colors[0] == "'Seam' (per corner)" && q.active_color == -1 && q.render_color == -1);
  });

  test("colour attribute: a Point layer is never the wrong size after any Edit Mode operator, with a Corner layer beside it", [] {
    const char *ops[][2] = {{"merge_distance", "all"}, {"delete", "faces"}, {"dissolve_vertices", "some"}, {"subdivide_edges", "all"}, {"loopcut", "edge"},
                            {"bevel", "some"}, {"extrude", "faces"}, {"inset", "faces"}, {"triangulate_faces", "faces"}, {"poke", "faces"},
                            {"split", "faces"}, {"duplicate", "faces"}, {"dissolve_faces", "faces"}, {"tris_to_quads", "all"}, {"collapse", "edge"},
                            {"connect", "some"}, {"edge_split", "some"}, {"delete_loose", "all"}};
    int bad = 0;
    for (auto &o : ops) {
      Editor ed;
      vpt_setup(ed, vpt_flat(true, true));
      vpt_op(ed, o[0], o[1]);
      const Mesh &m = vpt_grid(ed);
      const ColorAttribute *p = vpt_find(m, "P");
      bool ok = vpt_valid(m);
      if (std::string(o[0]) != "delete") ok = ok && p;  // (Delete of every face leaves nothing to paint: the layer may be empty, still valid)
      if (p) for (const Vec4 &v : p->rgba) ok = ok && vpt_finite4(v);
      if (!ok) std::printf("    %s: BAD\n", o[0]);
      bad += !ok;
    }
    CHECK(bad == 0);
  });

  /* ================================================================ 2. save and load */

  test("colour attribute: save and load keep every layer, its domain, colours, name, active and render choice; text and binary meshes", [] {
    for (int big = 0; big < 2; big++) {
      MeshPtr m = big ? primitives::grid(2.0f, 64, 64) : primitives::grid(2.0f, 4, 4);
      vpt_add_layer(*m, "Paint Layer", Dom::Point);
      vpt_add_layer(*m, "Seam \"quoted\"", Dom::Corner);
      vpt_add_layer(*m, "Third", Dom::Point);
      m->active_color = 1;
      m->render_color = 2;
      Scene l;
      std::string err, text;
      const bool ok = vpt_roundtrip(m, l, err, &text);
      const Mesh *r = vpt_loaded(l);
      std::printf("    %s mesh (%zu verts, %zu corners): loaded %d, layers %zu, active %d, render %d\n", big ? "binary" : "text", m->vert_count(), m->corner_count(),
                  (int)ok, r ? r->color_attributes.size() : 0, r ? r->active_color : -9, r ? r->render_color : -9);
      CHECK(ok && r && vpt_valid(*r) && r->color_attributes.size() == 3);
      if (!ok || !r || r->color_attributes.size() != 3) continue;
      CHECK(r->active_color == 1 && r->render_color == 2);
      for (size_t i = 0; i < 3; i++) {
        const ColorAttribute &a = m->color_attributes[i], &b = r->color_attributes[i];
        bool same = a.name == b.name && a.domain == b.domain && a.rgba.size() == b.rgba.size();
        for (size_t k = 0; same && k < a.rgba.size(); k++) same = vpt_near(a.rgba[k], b.rgba[k], 1e-6f);
        if (!same) std::printf("    layer %zu '%s' differs after the round trip\n", i, a.name.c_str());
        CHECK(same);
      }
      CHECK(r->color_attributes[1].corners == r->corner_verts);
    }
  });

  test("colour attribute: an old file loads with none; a damaged ca line is refused without a crash or a wrong-size layer", [] {
    /* A mesh with no layers writes none, and loads with none. */
    Scene l;
    std::string err, text;
    CHECK(vpt_roundtrip(primitives::grid(2.0f, 3, 3), l, err, &text));
    const Mesh *r = vpt_loaded(l);
    CHECK(r && r->color_attributes.empty() && r->active_color == -1 && r->render_color == -1);
    CHECK(text.find("\nca ") == std::string::npos && text.find("\ncactive") == std::string::npos);
    /* A file written with layers, the layer lines cut out (what an older build wrote): the mesh loads, with none. */
    MeshPtr m = primitives::grid(2.0f, 3, 3);
    vpt_add_layer(*m, "A", Dom::Point);
    vpt_add_layer(*m, "B", Dom::Corner);
    Scene s;
    s.create("M")->add<MeshFilter>()->mesh = m;
    const std::string full = save_scene_text(s);
    auto lines_of = [](const std::string &t) {
      std::vector<std::string> v;
      size_t at = 0;
      while (at < t.size()) {
        size_t e = t.find('\n', at);
        if (e == std::string::npos) e = t.size();
        v.push_back(t.substr(at, e - at));
        at = e + 1;
      }
      return v;
    };
    auto join = [](const std::vector<std::string> &v) {
      std::string o;
      for (const std::string &x : v) o += x + "\n";
      return o;
    };
    std::vector<std::string> ls = lines_of(full), old, cut, nanl, bad_active, junk;
    for (const std::string &x : ls) {
      const bool ca = x.rfind("ca ", 0) == 0, active = x.rfind("cactive ", 0) == 0;
      if (!ca && !active) old.push_back(x);
      if (!ca && !active) cut.push_back(x), nanl.push_back(x), bad_active.push_back(x), junk.push_back(x);
      if (ca) {
        std::string shorter = x.substr(0, x.rfind(' ', x.rfind(' ') - 1));  // two numbers fewer: one element short
        cut.push_back(shorter);
        std::string with_nan = x;
        const size_t sp = with_nan.find(' ', with_nan.find(' ', 3) + 1);  // after the domain letter
        if (sp != std::string::npos) with_nan.replace(sp + 1, 4, "nan");
        nanl.push_back(with_nan);
        junk.push_back(x.substr(0, x.find(' ', x.find(' ', 3) + 1)) + " abc def");
      }
      if (active) cut.push_back(x), nanl.push_back(x), junk.push_back("cactive 99 -7"), bad_active.push_back("cactive 99 -7");
    }
    struct V { const char *name; std::vector<std::string> *lines; };
    for (const V &v : {V{"old file (no layer lines)", &old}, V{"one element short", &cut}, V{"NaN colour", &nanl}, V{"non-numeric colours", &junk}, V{"active out of range", &bad_active}}) {
      Scene l2;
      std::string e2;
      const bool ok = load_scene_text(join(*v.lines), l2, e2);
      const Mesh *rm = vpt_loaded(l2);
      bool good = !ok || (rm && vpt_valid(*rm));
      if (ok && rm)
        for (const ColorAttribute &c : rm->color_attributes)
          for (const Vec4 &x : c.rgba) good = good && vpt_finite4(x);
      std::printf("    %s: loaded %d, layers %zu, valid %d\n", v.name, (int)ok, rm ? rm->color_attributes.size() : 0, (int)good);
      CHECK(good);
      if (v.lines == &old) CHECK(ok && rm && rm->color_attributes.empty() && rm->active_color == -1);
      if (v.lines == &cut) CHECK(ok && rm && !vpt_find(*rm, "A"));  // the short Point layer is refused, not padded
    }
  });

  /* ================================================================ 3. Draw */

  test("vertex paint: Draw with each blend matches Blender's formulas on a known colour, and paints only under the brush", [] {
    const Vec4 start(0.4f, 0.5f, 0.6f, 0.8f), paint(0.2f, 0.7f, 0.9f, 0.2f);
    for (int mode = 0; mode < 6; mode++) {
      VPad t;
      t.paint_layer([&](Vec3) { return start; });
      t.flat(0.3f, "draw", 0.5f);
      t.ed.command(std::string("vpblend ") + kVertexPaintBlends[mode]);
      t.ed.command("vpcolor 0.2 0.7 0.9 0.2");
      CHECK(t.ed.vp_brush_for_test().blend == mode);
      t.drag({Vec3(0, 0, 0)});
      const Vec4 got = t.at({0, 0, 0});
      const Vec4 want(vpt_blender(mode, start.x, paint.x, 0.5f), vpt_blender(mode, start.y, paint.y, 0.5f), vpt_blender(mode, start.z, paint.z, 0.5f),
                      mode == 0 ? start.w + (paint.w - start.w) * 0.5f : start.w);
      const Vec4 far = t.at({0.9f, 0, 0.9f});
      std::printf("    %-9s got %.3f %.3f %.3f %.3f, Blender %.3f %.3f %.3f %.3f; far vertex untouched %d\n", kVertexPaintBlends[mode], got.x, got.y, got.z, got.w,
                  want.x, want.y, want.z, want.w, (int)(far.x == start.x && far.y == start.y && far.z == start.z && far.w == start.w));
      CHECK(vpt_near(got, want, 0.01f));
      CHECK(far.x == start.x && far.y == start.y && far.z == start.z && far.w == start.w);
    }
  });

  test("vertex paint: a stroke changes a vertex at most one dab's worth however often it crosses it; a second stroke adds its own", [] {
    VPad t;
    t.paint_layer([](Vec3) { return Vec4(0.4f, 0.5f, 0.6f, 1.0f); });
    t.flat(0.3f, "draw", 0.5f);
    t.ed.command("vpcolor 1 1 1 1");
    t.drag({Vec3(-0.3f, 0, 0), Vec3(0.3f, 0, 0), Vec3(-0.3f, 0, 0), Vec3(0.3f, 0, 0), Vec3(0, 0, 0)});  // five passes over the vertex
    const Vec4 one = t.at({0, 0, 0});
    t.drag({Vec3(0, 0, 0)});
    const Vec4 two = t.at({0, 0, 0});
    std::printf("    red channel: start 0.400, one stroke (many dabs) %.3f (one dab: %.3f), second stroke %.3f (%.3f)\n", one.x, 0.4f + 0.6f * 0.5f, two.x,
                0.4f + 0.6f * 0.75f);
    CHECK(std::fabs(one.x - (0.4f + 0.6f * 0.5f)) < 0.01f);
    CHECK(std::fabs(two.x - (0.4f + 0.6f * 0.75f)) < 0.01f);
  });

  test("vertex paint: Front Faces Only leaves a sphere's back alone and Mirror X paints the twin", [] {
    VPad s(primitives::uv_sphere(0.6f, 24, 16));
    s.ed.command("vpbrush draw 200 1");
    s.ed.command("vpset falloff constant");
    s.ed.command("vpset front on");
    s.drag({Vec3(0, 0.6f, 0)});
    const float top = 1.0f - s.at({0, 0.6f, 0}).y, bottom = 1.0f - s.at({0, -0.6f, 0}).y;
    s.ed.command("vpset front off");
    s.drag({Vec3(0, 0.6f, 0)});
    const float through = 1.0f - s.at({0, -0.6f, 0}).y;
    std::printf("    front only: top %.2f, bottom %.2f; front off: bottom %.2f\n", top, bottom, through);
    CHECK(top > 0.5f && bottom == 0.0f && through > 0.0f);
    VPad m;
    m.ed.command("vpset mirror on");
    m.ed.command("vpset falloff constant");
    m.ed.command("vpbrush draw 30 1");
    m.drag({Vec3(0.6f, 0, 0.3f)});
    const Vec4 a = m.at({0.6f, 0, 0.3f}), b = m.at({-0.6f, 0, 0.3f});
    std::printf("    Mirror X: green at the stroke %.3f, at the twin %.3f\n", a.y, b.y);
    CHECK(a.y < 0.5f && a.y == b.y);
  });

  /* ================================================================ 4. Blur, Average, Smear */

  test("vertex paint: Blur evens out a hard colour edge, dab after dab", [] {
    VPad t;
    t.paint_layer([](Vec3 p) { return p.x < 0.0f ? Vec4(1, 0, 0, 1) : Vec4(0, 0, 1, 1); });
    auto variance = [&] {
      double mean = 0, var = 0;
      const ColorAttribute *c = t.layer();
      for (const Vec4 &v : c->rgba) mean += v.x;
      mean /= (double)c->rgba.size();
      for (const Vec4 &v : c->rgba) var += (v.x - mean) * (v.x - mean);
      return (float)(var / (double)c->rgba.size());
    };
    const float v0 = variance(), gap0 = t.at({-0.1f, 0, 0}).x - t.at({0.1f, 0, 0}).x;
    t.flat(0.4f, "blur", 0.5f);
    t.drag({Vec3(0, 0, -0.5f), Vec3(0, 0, 0.5f), Vec3(0, 0, -0.5f)});
    const float v1 = variance(), gap1 = t.at({-0.1f, 0, 0}).x - t.at({0.1f, 0, 0}).x;
    std::printf("    red variance %.4f -> %.4f, red gap across the edge %.3f -> %.3f\n", v0, v1, gap0, gap1);
    CHECK(gap0 > 0.99f && gap1 < gap0 - 0.1f && gap1 >= 0.0f);
    CHECK(v1 < v0);
    CHECK(vpt_near(t.at({0.9f, 0, 0.9f}), Vec4(0, 0, 1, 1), 0));  // far from the brush: unchanged
  });

  test("vertex paint: Average pulls every vertex under the brush to the area's mean colour and leaves the rest", [] {
    VPad t;
    t.paint_layer([](Vec3 p) { return p.x < 0.0f ? Vec4(1, 0, 0, 1) : Vec4(0, 0, 1, 1); });
    t.flat(0.4f, "average", 1.0f);
    t.drag({Vec3(0, 0, 0)});
    const Vec4 l = t.at({-0.1f, 0, 0}), r = t.at({0.1f, 0, 0});
    std::printf("    left of the edge %.3f %.3f %.3f, right %.3f %.3f %.3f (mean is about half red, half blue)\n", l.x, l.y, l.z, r.x, r.y, r.z);
    CHECK(vpt_near(l, r, 0.02f));
    CHECK(l.x > 0.3f && l.x < 0.7f && l.z > 0.3f && l.z < 0.7f);
    CHECK(vpt_near(t.at({-0.9f, 0, 0}), Vec4(1, 0, 0, 1), 0) && vpt_near(t.at({0.9f, 0, 0}), Vec4(0, 0, 1, 1), 0));
  });

  test("vertex paint: Smear drags colour along the stroke, from where it began", [] {
    VPad t;
    t.paint_layer([](Vec3 p) { return p.x < 0.0f ? Vec4(1, 0, 0, 1) : Vec4(1, 1, 1, 1); });
    t.flat(0.25f, "smear", 1.0f);
    const float before = t.at({0.3f, 0, 0}).y;
    t.drag({Vec3(-0.4f, 0, 0), Vec3(0.5f, 0, 0)});
    const Vec4 ahead = t.at({0.3f, 0, 0}), off = t.at({0.3f, 0, 0.9f});
    std::printf("    green at (0.3, 0, 0): %.3f -> %.3f (red dragged in); off the stroke %.3f\n", before, ahead.y, off.y);
    CHECK(before == 1.0f && ahead.y < 0.5f);
    CHECK(vpt_near(off, Vec4(1, 1, 1, 1), 0));
    /* The other way round leaves a different result: smear follows the stroke's direction. */
    VPad u;
    u.paint_layer([](Vec3 p) { return p.x < 0.0f ? Vec4(1, 0, 0, 1) : Vec4(1, 1, 1, 1); });
    u.flat(0.25f, "smear", 1.0f);
    u.drag({Vec3(0.5f, 0, 0), Vec3(-0.4f, 0, 0)});
    const Vec4 back = u.at({0.3f, 0, 0});
    std::printf("    stroked the other way, green at (0.3, 0, 0) stays %.3f\n", back.y);
    CHECK(back.y > ahead.y + 0.3f);
  });

  /* ================================================================ 5. automasking */

  test("vertex paint automasking: Topology paints only the island the stroke starts on; with Mirror X the mirrored island too", [] {
    size_t na = 0;
    MeshPtr m = vpt_two_islands(na);
    auto run = [&](bool topology, bool mirror) {
      VPad t(m);
      t.flat(0.3f);
      if (topology) t.ed.command("vpautomask topology on");
      if (mirror) t.ed.command("vpset mirror on");
      t.drag({Vec3(-0.15f, 0, 0)});
      const std::vector<float> w = t.amounts();
      return std::pair<float, float>(vpt_max(w, 0, na), vpt_max(w, na));
    };
    const auto off = run(false, false), on = run(true, false), mir = run(true, true);
    std::printf("    islands (A, B) paint max: no automask %.2f/%.2f, Topology %.2f/%.2f, Topology + Mirror X %.2f/%.2f\n", off.first, off.second, on.first, on.second,
                mir.first, mir.second);
    CHECK(off.first > 0.5f && off.second > 0.5f);
    CHECK(on.first > 0.5f && on.second == 0.0f);
    CHECK(mir.first > 0.5f && mir.second > 0.5f);
  });

  test("vertex paint automasking: Mesh Boundary leaves the open edge unpainted and Propagation Steps 3 grades the rings", [] {
    VPad t;
    t.flat(0.4f);
    t.ed.command("vpautomask boundary on");
    t.drag({Vec3(0.5f, 0, 0), Vec3(1.2f, 0, 0)});
    auto amt = [&](VPad &p, float x) { return 1.0f - p.at({x, 0, 0}).y; };
    const float edge = amt(t, 1.0f), inside = amt(t, 0.6f);
    std::printf("    boundary on, steps 1: edge %.3f, inside %.3f\n", edge, inside);
    CHECK(edge == 0.0f && inside > 0.9f);
    VPad u;
    u.flat(0.4f);
    u.ed.command("vpautomask boundary on");
    u.ed.command("vpautomask boundarysteps 3");
    u.drag({Vec3(0.5f, 0, 0), Vec3(1.2f, 0, 0)});
    const float r0 = amt(u, 1.0f), r1 = amt(u, 0.9f), r2 = amt(u, 0.8f), r3 = amt(u, 0.7f);
    std::printf("    steps 3: rings 0..3 = %.3f %.3f %.3f %.3f\n", r0, r1, r2, r3);
    CHECK(r0 == 0.0f && r0 < r1 && r1 < r2 && r2 < r3 && r3 > 0.9f);
  });

  test("vertex paint automasking: Cavity paints the groove's floor more than the ridge's top, Inverted the other way round", [] {
    MeshPtr m = vpt_groove_ridge();
    auto run = [&](const char *kind) {
      VPad t(m);
      t.flat(0.25f);
      t.ed.command(std::string("vpautomask ") + kind + " on");
      t.ed.command("vpautomask cavityfactor 1");
      t.drag({Vec3(0, 0, -0.6f), Vec3(0, 0, 0.6f)});
      return std::pair<float, float>(1.0f - t.at({0, -0.15f, -0.5f}).y, 1.0f - t.at({0, 0.15f, 0.5f}).y);
    };
    const auto cav = run("cavity"), inv = run("inverted");
    std::printf("    groove floor / ridge top: Cavity %.3f / %.3f, Inverted %.3f / %.3f\n", cav.first, cav.second, inv.first, inv.second);
    CHECK(cav.first > cav.second + 0.05f);
    CHECK(inv.second > inv.first + 0.05f);
  });

  test("vertex paint automasking: View Normal at 30 degrees paints the top of a sphere seen from above, not its edge-on sides", [] {
    MeshPtr s = primitives::uv_sphere(0.5f, 24, 16);
    auto run = [&](bool view) {
      VPad t(s);
      t.flat(0.7f);
      if (view) {
        t.ed.command("vpautomask view on");
        t.ed.command("vpautomask viewlimit 30");
      }
      t.drag({Vec3(0, 0, 0)});
      float top = 0.0f, side = 0.0f;
      const Mesh &mm = t.mesh();
      const std::vector<float> w = t.amounts();
      for (size_t i = 0; i < mm.vert_count(); i++) {
        const float ny = mm.positions[i].y / length(mm.positions[i]);
        if (ny > 0.9f) top = std::max(top, w[i]);
        if (ny < 0.2f) side = std::max(side, w[i]);
      }
      return std::pair<float, float>(top, side);
    };
    const auto off = run(false), on = run(true);
    std::printf("    top / sides: no automask %.3f / %.3f, View Normal 30 deg %.3f / %.3f\n", off.first, off.second, on.first, on.second);
    CHECK(off.second > 0.3f);
    CHECK(on.first > 0.9f);
    CHECK(on.second == 0.0f);
  });

  test("vertex paint automasking: Area Normal paints the top of a box a stroke started on, not the side it was dragged over", [] {
    size_t ntop = 0;
    MeshPtr m = vpt_box_top_and_side(ntop);
    auto run = [&](bool area) {
      VPad t(m);
      t.flat(0.35f);
      if (area) t.ed.command("vpautomask area on");
      t.drag({Vec3(-0.2f, 0, 0), Vec3(0.5f, -0.25f, 0)});
      const std::vector<float> w = t.amounts();
      return std::pair<float, float>(vpt_max(w, 0, ntop), vpt_max(w, ntop));
    };
    const auto off = run(false), on = run(true);
    std::printf("    top / side: no automask %.3f / %.3f, Area Normal %.3f / %.3f\n", off.first, off.second, on.first, on.second);
    CHECK(off.second > 0.3f);
    CHECK(on.first > 0.9f);
    CHECK(on.second == 0.0f);
  });

  /* ================================================================ 6. Corner layers */

  test("vertex paint: a stroke paints every corner of a vertex together; a Corner layer keeps two faces that share an edge in their own colours through save, load and glTF", [] {
    VPad t(nullptr, true);
    CHECK(t.layer() && t.layer()->domain == Dom::Corner && t.mesh().color_fits(*t.layer()));
    t.flat(0.3f);
    t.drag({Vec3(0, 0, 0)});
    /* Every corner of a vertex has the same colour. */
    const Mesh &m = t.mesh();
    const ColorAttribute &c = *t.layer();
    std::vector<Vec4> first(m.vert_count(), Vec4(-1, -1, -1, -1));
    size_t split = 0, painted = 0;
    for (size_t k = 0; k < m.corner_verts.size(); k++) {
      Vec4 &f = first[m.corner_verts[k]];
      if (f.x < 0) f = c.rgba[k];
      else if (!vpt_near(f, c.rgba[k], 0)) split++;
      painted += c.rgba[k].y < 0.9f;
    }
    std::printf("    Corner layer: %zu corners painted, %zu vertices whose corners differ\n", painted, split);
    CHECK(painted > 4 && split == 0);
    /* Two faces on one edge keep their own colours. */
    MeshPtr q = vpt_two_quads();
    ColorAttribute ca = q->make_color("Faces", Dom::Corner, Vec4(1, 0, 0, 1));
    for (uint32_t k = q->face_offsets[1]; k < q->face_offsets[2]; k++) ca.rgba[k] = Vec4(0, 0, 1, 1);
    q->color_attributes.push_back(ca);
    q->active_color = q->render_color = 0;
    Scene l;
    std::string err;
    CHECK(vpt_roundtrip(q, l, err));
    const Mesh *r = vpt_loaded(l);
    bool kept = r && vpt_valid(*r) && r->color_attributes.size() == 1 && r->color_attributes[0].domain == Dom::Corner;
    CHECK(kept);
    if (kept)
      for (size_t f = 0; f < 2; f++)
        for (uint32_t k = r->face_offsets[f]; k < r->face_offsets[f + 1]; k++)
          kept = kept && vpt_near(r->color_attributes[0].rgba[k], f == 0 ? Vec4(1, 0, 0, 1) : Vec4(0, 0, 1, 1), 0);
    std::printf("    two faces after save and load keep red / blue: %d\n", (int)kept);
    CHECK(kept);
    /* glTF: the shared vertices (1 and 4) are written twice, once per colour; a Point layer would share them. */
    const VptGlb g = vpt_glb(*q);
    std::printf("    glTF vertices with a Corner layer: %zu (6 in the mesh)\n", g.positions);
    CHECK(g.has_colour && g.positions == 8 && g.col.size() == 8);
    int reds = 0, blues = 0;
    for (size_t i = 0; i < g.pos.size() && i < g.col.size(); i++)
      if (std::fabs(std::fabs(g.pos[i].x) - 1.0f) < 1e-5f) {  // |x| = 1: the shared edge (glTF is right-handed: the export mirrors x)
        reds += vpt_near(g.col[i], Vec4(1, 0, 0, 1), 0);
        blues += vpt_near(g.col[i], Vec4(0, 0, 1, 1), 0);
      }
    std::printf("    on the shared edge: %d red copies, %d blue copies\n", reds, blues);
    CHECK(reds == 2 && blues == 2);
    MeshPtr pq = vpt_two_quads();
    vpt_add_layer(*pq, "Point", Dom::Point);
    CHECK(vpt_glb(*pq).positions == 6);
  });

  /* ================================================================ 7. the Scene view */

  test("vertex paint: the Scene view shows the painted colours, and leaving the mode restores the shading", [] {
    VPad t;
    const Shading before = Shading::Shaded;
    t.paint_layer([](Vec3 p) { return p.x < 0.0f ? Vec4(1, 0, 0, 1) : Vec4(0, 0, 1, 1); });
    for (int i = 0; i < 3; i++) t.ed.step_frame_headless();
    auto px = [&](Vec3 local) {
      int x = 0, y = 0;
      CHECK(t.ed.project_to_window(t.pad->world_matrix().point(local), x, y));
      return r34_crop(t.ed, {x, y, 1, 1})[0];
    };
    auto ch = [](uint32_t c, int k) { return (int)((c >> (8 * k)) & 0xFF); };  // 0xAARRGGBB: red is byte 2, blue byte 0
    const uint32_t red = px({-0.6f, 0, 0.5f}), blue = px({0.6f, 0, 0.5f});
    std::printf("    left (red layer) pixel %d %d %d, right (blue layer) pixel %d %d %d\n", ch(red, 2), ch(red, 1), ch(red, 0), ch(blue, 2), ch(blue, 1), ch(blue, 0));
    CHECK(ch(red, 2) > ch(red, 0) + 40 && ch(red, 2) > ch(red, 1) + 40);
    CHECK(ch(blue, 0) > ch(blue, 2) + 40 && ch(blue, 0) > ch(blue, 1) + 40);
    t.ed.command("vpaint off");
    for (int i = 0; i < 3; i++) t.ed.step_frame_headless();
    const uint32_t l2 = px({-0.6f, 0, 0.5f}), r2 = px({0.6f, 0, 0.5f});
    std::printf("    after leaving: left %d %d %d, right %d %d %d, shading restored %d\n", ch(l2, 2), ch(l2, 1), ch(l2, 0), ch(r2, 2), ch(r2, 1), ch(r2, 0),
                (int)(t.ed.shading_for_test() == before));
    CHECK(t.ed.shading_for_test() == before);
    CHECK(std::abs(ch(l2, 2) - ch(l2, 0)) < 30 && std::abs(ch(r2, 2) - ch(r2, 0)) < 30);
    CHECK(!t.ed.vertex_paint_for_test());
  });

  /* ================================================================ 8. export */

  test("vertex paint: glTF writes COLOR_0 from the render layer only; none without one, with an unfitting one, or with the layer chosen away", [] {
    MeshPtr m = primitives::grid(2.0f, 3, 3);
    m->color_attributes.push_back(m->make_color("Active", Dom::Point, Vec4(0, 1, 0, 1)));
    m->color_attributes.push_back(m->make_color("Render", Dom::Point, Vec4(1, 0, 0, 0.5f)));
    m->active_color = 0;
    m->render_color = 1;
    const VptGlb g = vpt_glb(*m);
    bool all_render = !g.col.empty();
    for (const Vec4 &c : g.col) all_render = all_render && vpt_near(c, Vec4(1, 0, 0, 0.5f), 0);
    std::printf("    render layer exported: COLOR_0 %d, %zu vertices, all the Render layer's red %d\n", (int)g.has_colour, g.col.size(), (int)all_render);
    CHECK(g.has_colour && g.col.size() == g.positions && all_render);
    Mesh none = *m;
    none.render_color = -1;
    CHECK(!vpt_glb(none).has_colour);
    Mesh bare = *primitives::grid(2.0f, 3, 3);
    CHECK(!vpt_glb(bare).has_colour);
    Mesh wrong = *m;
    wrong.color_attributes[1].rgba.resize(3);
    CHECK(!vpt_glb(wrong).has_colour);
    std::printf("    no render layer %d, no layers %d, wrong-size layer %d: COLOR_0 absent in each\n", (int)!vpt_glb(none).has_colour, (int)!vpt_glb(bare).has_colour,
                (int)!vpt_glb(wrong).has_colour);
  });

  test("vertex paint: PLY writes red / green / blue / alpha bytes (sRGB, as Blender's exporter); a mesh with none writes none", [] {
    MeshPtr m = primitives::grid(2.0f, 2, 2);
    m->color_attributes.push_back(m->make_color("C", Dom::Point, Vec4(1.0f, 0.0f, 0.25f, 0.4f)));
    m->active_color = m->render_color = 0;
    const std::string ply = vpt_ply(*m);
    CHECK(ply.find("property uchar red") != std::string::npos && ply.find("property uchar green") != std::string::npos);
    CHECK(ply.find("property uchar blue") != std::string::npos && ply.find("property uchar alpha") != std::string::npos);
    const size_t body = ply.find("end_header\n");
    CHECK(body != std::string::npos);
    int rows = 0, right = 0;
    if (body != std::string::npos) {
      size_t at = body + 11;
      for (size_t v = 0; v < m->vert_count(); v++) {
        const size_t e = ply.find('\n', at);
        std::vector<int> tok;
        const std::string line = ply.substr(at, e - at);
        size_t p = 0;
        std::vector<std::string> words;
        while (p < line.size()) {
          const size_t sp = line.find(' ', p);
          words.push_back(line.substr(p, sp == std::string::npos ? std::string::npos : sp - p));
          if (sp == std::string::npos) break;
          p = sp + 1;
        }
        rows++;
        if (words.size() >= 4) {
          const size_t n = words.size();
          const int r = std::atoi(words[n - 4].c_str()), gg = std::atoi(words[n - 3].c_str()), b = std::atoi(words[n - 2].c_str()), a = std::atoi(words[n - 1].c_str());
          right += r == 255 && gg == 0 && std::abs(b - 137) <= 1 && a == 102;
          if (v == 0) std::printf("    first vertex's colour bytes: %d %d %d %d (linear 1, 0, 0.25, 0.4 -> 255 0 137 102)\n", r, gg, b, a);
        }
        at = e + 1;
      }
    }
    CHECK(rows == (int)m->vert_count() && right == rows);
    const std::string bare = vpt_ply(*primitives::grid(2.0f, 2, 2));
    CHECK(bare.find("uchar red") == std::string::npos && bare.find("property uchar alpha") == std::string::npos);
    Mesh nonrender = *m;
    nonrender.render_color = -1;
    CHECK(vpt_ply(nonrender).find("uchar red") == std::string::npos);
  });

  /* ================================================================ 9. robustness */

  test("vertex paint: odd meshes (open, non-manifold, loose vertex, NaN positions, empty, wrong-size layer) leave a valid mesh with every tool, one undo step a stroke", [] {
    struct Case { const char *name; MeshPtr mesh; bool wrong_layer = false; };
    std::vector<Case> cases;
    cases.push_back({"closed sphere", primitives::uv_sphere(0.5f, 16, 12)});
    {
      auto m = std::make_shared<Mesh>();
      m->add_vert({0, 0, -0.5f});
      m->add_vert({0, 0, 0.5f});
      m->add_vert({0.5f, 0, 0});
      m->add_vert({-0.5f, 0, 0});
      m->add_vert({0, 0.5f, 0});
      m->add_face({0, 1, 2});
      m->add_face({1, 0, 3});
      m->add_face({0, 1, 4});
      cases.push_back({"non-manifold edge (3 faces)", m});
    }
    {
      MeshPtr m = primitives::grid(2.0f, 6, 6);
      m->add_vert({0.02f, 0, 0});
      cases.push_back({"loose vertex", m});
    }
    cases.push_back({"open strip", primitives::grid(2.0f, 12, 1)});
    {
      MeshPtr m = primitives::grid(2.0f, 8, 8);
      m->positions[40].x = std::nanf("");
      m->positions[41].y = std::nanf("");
      m->positions[13] = Vec3(INFINITY, 0, 0);
      cases.push_back({"NaN / infinite positions", m});
    }
    cases.push_back({"empty mesh", std::make_shared<Mesh>()});
    cases.push_back({"wrong-size layer", primitives::grid(2.0f, 8, 8), true});
    for (Case &c : cases)
      for (const char *tool : {"draw", "blur", "average", "smear"}) {
        VPad t(c.mesh ? std::make_shared<Mesh>(*c.mesh) : nullptr, false, false);
        if (c.wrong_layer) {
          Mesh &w = *mesh_make_mutable(t.pad->mesh_filter()->mesh);
          w.color_attributes.push_back(w.make_color("Short", Dom::Point, Vec4(0.5f, 0.5f, 0.5f, 1)));
          w.color_attributes[0].rgba.resize(5);
          w.active_color = w.render_color = 0;
          w.touch();
        }
        t.ed.command("vpaint on");
        t.ed.step_frame_headless();
        t.flat(0.4f, tool, 0.8f);
        t.ed.command("vpautomask topology on");
        t.ed.command("vpautomask boundary on");
        t.ed.command("vpautomask cavity on");
        t.ed.command("vpautomask view on");
        t.ed.command("vpautomask area on");
        t.ed.command("vpset mirror on");
        t.drag({Vec3(-0.4f, 0, 0), Vec3(0.4f, 0, 0)});
        t.drag({Vec3(0.02f, 0, -0.4f), Vec3(0.02f, 0, 0.4f)}, platform::MOD_CTRL);
        const Mesh &m = t.mesh();
        bool ok = vpt_valid(m);
        for (const ColorAttribute &l : m.color_attributes)
          for (const Vec4 &v : l.rgba) ok = ok && vpt_finite4(v) && v.x >= 0 && v.x <= 1 && v.y >= 0 && v.y <= 1 && v.z >= 0 && v.z <= 1 && v.w >= 0 && v.w <= 1;
        if (!ok) std::printf("    NOT VALID: %s with %s\n", c.name, tool);
        CHECK(ok);
      }
    std::printf("    %zu odd meshes x 4 tools, every automask on with Mirror X: every layer fits, every colour finite in 0..1\n", cases.size());
    /* The wrong-size layer on its own: entering the mode mends it with a fresh one, the old is reported dropped. */
    VPad w(primitives::grid(2.0f, 4, 4), false, false);
    {
      Mesh &m = *mesh_make_mutable(w.pad->mesh_filter()->mesh);
      m.color_attributes.push_back(m.make_color("Short", Dom::Point, Vec4(0, 0, 0, 1)));
      m.color_attributes[0].rgba.resize(3);
      m.active_color = m.render_color = 0;
      m.touch();
    }
    w.ed.command("vpaint on");
    w.ed.step_frame_headless();
    CHECK(vpt_valid(w.mesh()) && w.ed.vertex_paint_for_test() && w.layer() && w.layer()->rgba.size() == w.mesh().vert_count());
  });

  test("vertex paint: NaN colours in a layer are repainted finite, saved without NaN and exported without NaN", [] {
    VPad t;
    {
      Mesh &m = *mesh_make_mutable(t.pad->mesh_filter()->mesh);
      ColorAttribute &c = m.color_attributes[(size_t)m.active_color];
      c.rgba[10] = Vec4(std::nanf(""), 0.5f, 0.5f, 1.0f);
      c.rgba[11] = Vec4(0.2f, INFINITY, -INFINITY, std::nanf(""));
      m.touch();
    }
    t.ed.step_frame_headless();
    t.flat(0.5f, "blur", 1.0f);
    t.drag({Vec3(-0.4f, 0, 0), Vec3(0.4f, 0, 0)});
    t.flat(0.5f, "draw", 1.0f);
    t.drag({Vec3(-0.9f, 0, -0.9f), Vec3(0.9f, 0, -0.9f)});
    bool finite = true;
    for (const Vec4 &v : t.layer()->rgba) finite = finite && vpt_finite4(v);
    std::printf("    layer still holds a non-finite colour after strokes: %d\n", (int)!finite);
    /* Colours a stroke never touched may still be NaN in the layer: what leaves the program must not be. */
    Scene l;
    std::string err;
    CHECK(vpt_roundtrip(std::make_shared<Mesh>(t.mesh()), l, err));
    const Mesh *r = vpt_loaded(l);
    bool saved_ok = r && vpt_valid(*r);
    if (r)
      for (const ColorAttribute &c : r->color_attributes)
        for (const Vec4 &v : c.rgba) saved_ok = saved_ok && vpt_finite4(v);
    CHECK(saved_ok);
    const VptGlb g = vpt_glb(t.mesh());
    bool glb_ok = g.has_colour;
    for (const Vec4 &v : g.col) glb_ok = glb_ok && vpt_finite4(v);
    std::printf("    after save and load every colour finite: %d; glTF COLOR_0 finite: %d\n", (int)saved_ok, (int)glb_ok);
    CHECK(glb_ok);
  });

  test("vertex paint: a stroke is one undo step; two strokes undo one at a time; a stroke that changes nothing makes no step", [] {
    VPad t;
    t.flat(0.25f);
    t.drag({Vec3(-0.5f, 0, 0), Vec3(-0.2f, 0, 0), Vec3(-0.5f, 0, 0.2f)});  // many dabs, one stroke
    t.drag({Vec3(0.5f, 0, 0), Vec3(0.2f, 0, 0)});
    const bool a = t.at({-0.5f, 0, 0}).y < 0.1f, b = t.at({0.5f, 0, 0}).y < 0.1f;
    CHECK(a && b);
    t.ed.command("undo");
    t.ed.step_frame_headless();
    const Vec4 a1 = t.at({-0.5f, 0, 0}), b1 = t.at({0.5f, 0, 0});
    std::printf("    after one Undo: first stroke's green %.3f (kept), second's %.3f (gone)\n", a1.y, b1.y);
    CHECK(a1.y < 0.1f && vpt_near(b1, Vec4(1, 1, 1, 1), 0));
    t.ed.command("undo");
    t.ed.step_frame_headless();
    CHECK(vpt_near(t.at({-0.5f, 0, 0}), Vec4(1, 1, 1, 1), 0) && vpt_near(t.at({-0.5f, 0, 0.2f}), Vec4(1, 1, 1, 1), 0));
    /* A stroke with strength 0 changes nothing: nothing to undo (the next Undo takes the one before it). */
    t.ed.command("vpbrush draw 40 0");
    t.drag({Vec3(0, 0, 0.5f)});
    t.ed.command("redo");
    t.ed.step_frame_headless();
    CHECK(vpt_valid(t.mesh()));
  });

  /* ================================================================ 10. the console, the Inspector, the modes */

  test("vertex paint: vpaint on / off / toggle, the Inspector button, Esc, and one mode at a time", [] {
    VPad t(nullptr, false, false);
    CHECK(!t.ed.vertex_paint_for_test());
    t.ed.command("vpaint on");
    CHECK(t.ed.vertex_paint_for_test());
    CHECK(t.mesh().color_attributes.size() == 1 && t.mesh().color_attributes[0].name == "Color" && t.mesh().active_color == 0 && t.mesh().render_color == 0);
    CHECK(t.mesh().color_attributes[0].domain == Dom::Point && vpt_near(t.at({0, 0, 0}), Vec4(1, 1, 1, 1), 0));
    t.ed.command("vpaint off");
    CHECK(!t.ed.vertex_paint_for_test());
    t.ed.command("vpaint");  // toggles
    CHECK(t.ed.vertex_paint_for_test());
    t.ed.command("vpaint toggle");
    CHECK(!t.ed.vertex_paint_for_test());
    /* Entering again keeps the layer it has (no second one). */
    t.ed.command("vpaint on");
    CHECK(t.mesh().color_attributes.size() == 1);
    /* The Inspector's button toggles it. */
    t.ed.command("vpaint off");
    t.ed.record_buttons_for_test(true);
    Recti r;
    bool found = false;
    for (int i = 0; i < 4 && !found; i++) {
      t.ed.step_frame_headless();
      found = t.ed.button_rect_for_test("vp:Toggle", r);
    }
    CHECK(found);
    auto click = [&] {
      for (R34ET ty : {R34ET::MouseMove, R34ET::MouseDown, R34ET::MouseUp}) t.ed.step_frame_headless({r34_mouse(ty, r.x + r.w / 2, r.y + r.h / 2)});
      t.ed.step_frame_headless();
    };
    if (found) {
      click();
      const bool on = t.ed.vertex_paint_for_test();
      t.ed.step_frame_headless();
      t.ed.button_rect_for_test("vp:Toggle", r);
      click();
      const bool off = !t.ed.vertex_paint_for_test();
      std::printf("    Inspector button: on %d, off again %d\n", (int)on, (int)off);
      CHECK(on && off);
    }
    t.ed.record_buttons_for_test(false);
    /* Esc over the Scene view leaves the mode. */
    t.ed.command("vpaint on");
    int x = 0, y = 0;
    CHECK(t.ed.project_to_window(t.pad->world_matrix().point({0.5f, 0, 0.5f}), x, y));
    t.ed.step_frame_headless({r34_mouse(R34ET::MouseMove, x, y)});
    platform::Event esc = r34_mouse(R34ET::KeyDown, x, y);
    esc.key = platform::KEY_ESCAPE;
    t.ed.step_frame_headless({esc});
    t.ed.step_frame_headless();
    std::printf("    Esc over the Scene view: Vertex Paint on %d\n", (int)t.ed.vertex_paint_for_test());
    CHECK(!t.ed.vertex_paint_for_test());
    /* One mode at a time, both ways. */
    t.ed.command("vpaint on");
    t.ed.command("weightpaint on");
    t.ed.step_frame_headless();
    t.ed.step_frame_headless();
    CHECK(t.ed.weight_paint_for_test() && !t.ed.vertex_paint_for_test());
    t.ed.command("vpaint on");
    t.ed.step_frame_headless();
    CHECK(t.ed.vertex_paint_for_test() && !t.ed.weight_paint_for_test());
    t.ed.command("edit vertex");
    t.ed.step_frame_headless();
    t.ed.step_frame_headless();
    CHECK(!t.ed.vertex_paint_for_test());
    t.ed.command("edit off");
    /* Nothing selected: refused. */
    Editor e2;
    e2.init_headless(800, 600);
    e2.command("select Nothing");
    e2.command("vpaint on");
    CHECK(!e2.vertex_paint_for_test());
  });

  test("vertex paint: vpbrush, vpblend, vpcolor and vpset set the brush, clamp numbers, and refuse bad input without a change", [] {
    VPad t;
    VertexBrush &b = t.ed.vp_brush_for_test();
    CHECK(std::string(kVertexPaintTools[0]) == "Draw" && std::string(kVertexPaintTools[1]) == "Blur" && std::string(kVertexPaintTools[2]) == "Average" &&
          std::string(kVertexPaintTools[3]) == "Smear");
    const char *blends[6] = {"Mix", "Add", "Subtract", "Multiply", "Lighten", "Darken"};
    for (int i = 0; i < 6; i++) CHECK(std::string(kVertexPaintBlends[i]) == blends[i]);
    t.ed.command("vpbrush smear 33 0.25");
    CHECK(b.tool == 3 && b.radius == 33.0f && std::fabs(b.strength - 0.25f) < 1e-6f);
    t.ed.command("vpbrush blur");
    CHECK(b.tool == 1 && b.radius == 33.0f);
    t.ed.command("vpbrush average");
    CHECK(b.tool == 2);
    t.ed.command("vpbrush DRAW 5000 9");
    CHECK(b.tool == 0 && b.radius == 1000.0f && b.strength == 1.0f);
    t.ed.command("vpbrush draw -5 -1");
    CHECK(b.radius == 1.0f && b.strength == 0.0f);
    t.ed.command("vpbrush draw 40 0.5");
    t.ed.command("vpbrush draw abc nan");
    CHECK(b.radius == 40.0f && b.strength == 0.5f);
    t.ed.command("vpbrush bogus");
    CHECK(b.tool == 0);
    for (int i = 0; i < 6; i++) {
      t.ed.command(std::string("vpblend ") + blends[i]);
      CHECK(b.blend == i);
    }
    t.ed.command("vpblend subtract");
    CHECK(b.blend == 2);
    t.ed.command("vpblend bogus");
    CHECK(b.blend == 2);
    t.ed.command("vpcolor 0.2 0.3 0.4 0.5");
    CHECK(vpt_near(Vec4(b.color.x, b.color.y, b.color.z, b.alpha), Vec4(0.2f, 0.3f, 0.4f, 0.5f), 1e-6f));
    t.ed.command("vpcolor 2 -1 0.5");
    CHECK(vpt_near(Vec4(b.color.x, b.color.y, b.color.z, b.alpha), Vec4(1.0f, 0.0f, 0.5f, 0.5f), 1e-6f));  // clamped; alpha kept
    t.ed.command("vpcolor a b c");
    t.ed.command("vpcolor 0.1 0.1");
    t.ed.command("vpcolor nan 0 0");
    CHECK(vpt_near(Vec4(b.color.x, b.color.y, b.color.z, b.alpha), Vec4(1.0f, 0.0f, 0.5f, 0.5f), 1e-6f));
    t.ed.command("vpset falloff linear");
    CHECK(b.falloff == 4);
    t.ed.command("vpset falloff sharp");
    CHECK(b.falloff == 3);
    t.ed.command("vpset falloff bogus");
    CHECK(b.falloff == 3);
    t.ed.command("vpset mirror on");
    CHECK(b.mirror_x);
    t.ed.command("vpset mirror off");
    CHECK(!b.mirror_x);
    t.ed.command("vpset front off");
    CHECK(!b.front_only);
    t.ed.command("vpset front on");
    CHECK(b.front_only);
    t.ed.command("vpset nonsense");
    CHECK(b.front_only && !b.mirror_x && b.falloff == 3);
  });

  test("vertex paint: the vpautomask console switches options, takes numbers, and refuses bad input without a change", [] {
    VPad t;
    VertexBrush &b = t.ed.vp_brush_for_test();
    CHECK(!b.am_topology && !b.am_boundary && b.am_cavity == 0 && !b.am_view_normal && !b.am_area_normal);
    t.ed.command("vpautomask topology");
    CHECK(b.am_topology);
    t.ed.command("vpautomask topology off");
    CHECK(!b.am_topology);
    t.ed.command("vpautomask boundary on");
    t.ed.command("vpautomask cavity on");
    CHECK(b.am_boundary && b.am_cavity == 1);
    t.ed.command("vpautomask inverted on");
    CHECK(b.am_cavity == 2);
    t.ed.command("vpautomask inverted off");
    CHECK(b.am_cavity == 1);
    t.ed.command("vpautomask view on");
    t.ed.command("vpautomask area on");
    t.ed.command("vpautomask topology on");
    CHECK(b.am_view_normal && b.am_area_normal && b.am_topology);
    t.ed.command("vpautomask none");
    CHECK(!b.am_topology && !b.am_boundary && b.am_cavity == 0 && !b.am_view_normal && !b.am_area_normal);
    t.ed.command("vpautomask boundarysteps 3");
    t.ed.command("vpautomask cavityfactor 1.5");
    t.ed.command("vpautomask cavityblur 4");
    t.ed.command("vpautomask viewlimit 45");
    t.ed.command("vpautomask arealimit 30");
    CHECK(b.am_boundary_steps == 3 && b.am_cavity_blur == 4 && std::fabs(b.am_cavity_factor - 1.5f) < 1e-5f);
    CHECK(std::fabs(b.am_view_limit - 45.0f) < 1e-5f && std::fabs(b.am_area_limit - 30.0f) < 1e-5f);
    const VertexBrush before = b;
    t.ed.command("vpautomask bogus on");
    t.ed.command("vpautomask");
    t.ed.command("vpautomask boundarysteps abc");
    t.ed.command("vpautomask viewlimit inf");
    CHECK(b.am_boundary_steps == before.am_boundary_steps && b.am_view_limit == before.am_view_limit && b.am_cavity == before.am_cavity);
    /* Weight Paint's brush is a separate one: the Vertex Paint options don't touch it. */
    CHECK(!t.ed.wp_brush_for_test().am_boundary);
  });

  test("vertex paint: the console vpstroke paints like a mouse drag, is one undo step, and refuses with the mode off", [] {
    VPad t;
    t.flat(0.3f);
    std::string cmd = "vpstroke";
    for (Vec3 p : {Vec3(-0.3f, 0, 0), Vec3(0.3f, 0, 0)}) {
      int x = 0, y = 0;
      CHECK(t.ed.project_to_window(t.pad->world_matrix().point(p), x, y));
      cmd += " " + std::to_string(x) + " " + std::to_string(y);
    }
    t.ed.command(cmd);
    t.ed.step_frame_headless();
    const float by_console = t.at({0, 0, 0}).y;
    t.ed.command("undo");
    t.ed.step_frame_headless();
    CHECK(vpt_near(t.at({0, 0, 0}), Vec4(1, 1, 1, 1), 0));
    /* An Undo restores the same objects: painting goes on, as Weight Paint does (review of round 545's tests). */
    std::printf("    Vertex Paint still on after Undo: %d\n", (int)t.ed.vertex_paint_for_test());
    CHECK(t.ed.vertex_paint_for_test());
    t.ed.command("vpaint on");
    t.ed.step_frame_headless();
    t.drag({Vec3(-0.3f, 0, 0), Vec3(0.3f, 0, 0)});
    const float by_mouse = t.at({0, 0, 0}).y;
    std::printf("    green at the centre: console stroke %.3f, mouse drag %.3f\n", by_console, by_mouse);
    CHECK(by_console < 0.1f && std::fabs(by_console - by_mouse) < 0.02f);
    t.ed.command("vpaint off");
    const Vec4 off = t.at({0, 0, 0});
    t.ed.command(cmd);
    CHECK(vpt_near(t.at({0, 0, 0}), off, 0));
    t.ed.command("vpaint on");
    t.ed.command("vpstroke");  // no points
    t.ed.command("vpstroke 5");
    CHECK(vpt_valid(t.mesh()));
  });

  test("colour attribute: the colorattr console adds, removes, renames, selects and fills layers, one undo step each, refusing bad input", [] {
    VPad t;
    auto names = [&] {
      std::string s;
      for (const ColorAttribute &c : t.mesh().color_attributes) s += c.name + (c.domain == Dom::Corner ? "(c) " : "(p) ");
      return s;
    };
    CHECK(t.mesh().color_attributes.size() == 1);
    t.ed.command("colorattr add Foo");
    CHECK(t.mesh().color_attributes.size() == 2 && t.mesh().active_color == 1 && t.mesh().color_attributes[1].domain == Dom::Point);
    t.ed.command("colorattr add Bar corner");
    const Mesh &m = t.mesh();
    CHECK(m.color_attributes.size() == 3 && m.color_attributes[2].domain == Dom::Corner && m.color_fits(m.color_attributes[2]) && m.active_color == 2);
    t.ed.command("colorattr add Foo");  // the name is taken
    CHECK(t.mesh().color_attributes.size() == 3);
    t.ed.command("colorattr add");
    CHECK(t.mesh().color_attributes.size() == 3);
    t.ed.command("colorattr active Color");
    CHECK(t.mesh().active_color == 0);
    t.ed.command("colorattr render Bar");
    CHECK(t.mesh().render_color == 2);
    t.ed.command("colorattr active Nothing");
    CHECK(t.mesh().active_color == 0);
    t.ed.command("colorattr rename Foo Baz");
    CHECK(t.mesh().find_color("Baz") == 1 && t.mesh().find_color("Foo") == -1);
    t.ed.command("colorattr rename Baz Bar");  // taken
    CHECK(t.mesh().find_color("Baz") == 1);
    t.ed.command("colorattr active Baz");
    t.ed.command("colorattr fill 0.1 0.2 0.3 0.4");
    bool filled = true;
    for (const Vec4 &v : t.mesh().color_attributes[1].rgba) filled = filled && vpt_near(v, Vec4(0.1f, 0.2f, 0.3f, 0.4f), 1e-6f);
    CHECK(filled && vpt_near(t.mesh().color_attributes[0].rgba[0], Vec4(1, 1, 1, 1), 0));  // only the active layer
    /* A float layer keeps HDR colours (the review of round 545: Blender's float layers aren't clamped to 1); a negative
     * channel goes to 0, alpha stays 0..1. (Was: 5 clamped to 1.) */
    t.ed.command("colorattr fill 5 -1 0.5");
    CHECK(vpt_near(t.mesh().color_attributes[1].rgba[0], Vec4(5, 0, 0.5f, 1), 1e-6f));
    t.ed.command("colorattr fill 0.1 abc 0.3");
    t.ed.command("colorattr fill");
    CHECK(vpt_near(t.mesh().color_attributes[1].rgba[0], Vec4(5, 0, 0.5f, 1), 1e-6f));
    std::printf("    layers now: %s (active %d, render %d)\n", names().c_str(), t.mesh().active_color, t.mesh().render_color);
    t.ed.command("colorattr remove Baz");  // the active one: active falls back to the first
    CHECK(t.mesh().color_attributes.size() == 2 && t.mesh().find_color("Baz") == -1 && t.mesh().active_color == 0 && t.mesh().render_color == 1);
    t.ed.command("colorattr remove Nothing");
    CHECK(t.mesh().color_attributes.size() == 2);
    t.ed.command("colorattr remove Bar");  // the render one
    CHECK(t.mesh().color_attributes.size() == 1 && t.mesh().render_color == 0 && vpt_valid(t.mesh()));
    t.ed.command("colorattr bogus x");
    CHECK(t.mesh().color_attributes.size() == 1);
    /* Each is an undo step. */
    t.ed.command("colorattr add Undoable");
    t.ed.step_frame_headless();
    CHECK(t.mesh().color_attributes.size() == 2);
    t.ed.command("undo");
    t.ed.step_frame_headless();
    std::printf("    after Undo of `colorattr add`: %zu layer(s)\n", t.mesh().color_attributes.size());
    CHECK(t.mesh().color_attributes.size() == 1 && vpt_valid(t.mesh()));
  });

  test("colour attribute: Sculpt Dyntopo carries a Point layer onto its new vertices and through its collapses, exactly for colours that follow position; costs nothing without a layer", [] {
    /* A dense pad refined to a coarser and a finer detail: splits put new vertices at edge midpoints (the mean of the
     * ends' colours), collapses move a vertex to the midpoint (the mean too), the stroke's end compacts the vertices.
     * Colours linear in position stay exactly that function of each vertex's place. The time is printed, not checked
     * (ADR 0056): the hub asked to see that a mesh without layers pays nothing for the hook. */
    auto run = [](bool layer, float detail, double &ms, Mesh &out) {
      Mesh m = *primitives::grid(2.0f, 40, 40);
      if (layer) {
        ColorAttribute c = m.make_color("Color", Dom::Point, Vec4(1, 1, 1, 1));
        for (size_t v = 0; v < m.vert_count(); v++) c.rgba[v] = vpt_f(m.positions[v]);
        m.color_attributes.push_back(std::move(c));
        m.active_color = m.render_color = 0;
      }
      std::vector<uint32_t> region(m.vert_count()), added;
      for (uint32_t v = 0; v < (uint32_t)m.vert_count(); v++) region[v] = v;
      const auto t0 = std::chrono::steady_clock::now();
      meshops::Dyntopo d(m);
      d.refine(region, Vec3(0, 0, 0), 2.0f, detail, added);
      d.finish();
      ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      out = std::move(m);
    };
    for (float detail : {0.02f, 0.2f}) {  // finer (splits) and coarser (collapses: under 0.4 x detail = 0.08) than the pad's 0.05 edges
      double with_ms = 0, without_ms = 0;
      Mesh with, without;
      run(false, detail, without_ms, without);
      run(true, detail, with_ms, with);
      float worst = 0.0f;
      bool fits = !with.color_attributes.empty() && with.color_fits(with.color_attributes[0]);
      for (size_t v = 0; fits && v < with.vert_count(); v++) {
        const Vec4 c = with.color_attributes[0].rgba[v], e = vpt_f(with.positions[v]);
        worst = std::max(worst, std::max(std::fabs(c.x - e.x), std::fabs(c.y - e.y)));
      }
      std::printf("    Dyntopo at detail %.2f: %zu -> %zu vertices; layer fits %d, worst colour error %.2g; %.2f ms without a layer, %.2f ms with\n",
                  detail, (size_t)41 * 41, with.vert_count(), (int)fits, worst, without_ms, with_ms);
      CHECK(fits && with.vert_count() != (size_t)41 * 41 && worst < 1e-4f);
      CHECK(without.color_attributes.empty() && without.vert_count() == with.vert_count());  // the same refinement either way
    }
  });


  vertex_colour_input_tests();
  vertex_colour_card_packing_tests();
}

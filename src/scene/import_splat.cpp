/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */
// Gaussian splat and point cloud import (round 531, task 0511). Ported, not copied, from Blender 5.3:
//  - io/ply/importer/ply_import.cc (is_data_gaussian_splat) and ply_import_gsplat.cc: which PLY is a splat file, the
//    f_dc / opacity / scale / rot / f_rest properties, f_rest channel-major, the activation functions
//    (io/common/IO_gsplat.hh: sigmoid opacity, exp scale);
//  - io/spz/intern/spz_header.*, spz_read_common.*, spz_read_gzip.cc, spz_read_ngsp.cc: the SPZ headers and their
//    checks, 24-bit fixed-point positions, 8-bit alpha / colour (0.15 scale) / log scale, the v2 and v3 rotations, the
//    8-bit SH, the v4 table of zstd streams;
// Read, not copied: the glTF add-on's (Apache-2.0) KHR_gaussian_splatting attribute names, and Niantic's SPZ
// format notes (MIT) for version 1's float16 positions, which Blender's reader leaves out.
// Blender turns each format into its Z-up space with a fixed table per SH band; here one general map per band
// (splat_sh_band_matrix) carries the coefficients through any orthogonal change of axes, Blendity's mirror included.
#include "import_splat.h"

#include "../core/core.h"
#include "../core/jobs.h"
#include "import.h"
#include "unity_package.h"  // gunzip (over image.cpp's inflate)

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <new>

#ifdef BL_WITH_ZSTD
#  include <zstd.h>
#endif

namespace bl {

namespace {

/* ------------------------------------------------------------------ axes */

/* PLY as Blender writes it: Z up, right-handed - the mesh importer's from_z_up, (x, y, z) -> (-x, z, -y). */
constexpr float kFromZUp[3][3] = {{-1, 0, 0}, {0, 0, 1}, {0, -1, 0}};
/* Y up, right-handed (SPZ's RUB, glTF): X mirrored, as the glTF meshes are. */
constexpr float kMirrorX[3][3] = {{-1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

Vec3 mul(const float m[3][3], Vec3 v) {
  return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z, m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z, m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
}

void quat_to_matrix(Quat q, float r[3][3]) {
  const float x = q.x, y = q.y, z = q.z, w = q.w;
  r[0][0] = 1 - 2 * (y * y + z * z), r[0][1] = 2 * (x * y - w * z), r[0][2] = 2 * (x * z + w * y);
  r[1][0] = 2 * (x * y + w * z), r[1][1] = 1 - 2 * (x * x + z * z), r[1][2] = 2 * (y * z - w * x);
  r[2][0] = 2 * (x * z - w * y), r[2][1] = 2 * (y * z + w * x), r[2][2] = 1 - 2 * (x * x + y * y);
}

/* A proper rotation matrix to a unit quaternion (Shepperd's method). */
Quat matrix_to_quat(const float r[3][3]) {
  const float tr = r[0][0] + r[1][1] + r[2][2];
  Quat q;
  if (tr > 0.0f) {
    const float s = std::sqrt(tr + 1.0f) * 2.0f;
    q = {(r[2][1] - r[1][2]) / s, (r[0][2] - r[2][0]) / s, (r[1][0] - r[0][1]) / s, 0.25f * s};
  }
  else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
    const float s = std::sqrt(1.0f + r[0][0] - r[1][1] - r[2][2]) * 2.0f;
    q = {0.25f * s, (r[0][1] + r[1][0]) / s, (r[0][2] + r[2][0]) / s, (r[2][1] - r[1][2]) / s};
  }
  else if (r[1][1] > r[2][2]) {
    const float s = std::sqrt(1.0f + r[1][1] - r[0][0] - r[2][2]) * 2.0f;
    q = {(r[0][1] + r[1][0]) / s, 0.25f * s, (r[1][2] + r[2][1]) / s, (r[0][2] - r[2][0]) / s};
  }
  else {
    const float s = std::sqrt(1.0f + r[2][2] - r[0][0] - r[1][1]) * 2.0f;
    q = {(r[0][2] + r[2][0]) / s, (r[1][2] + r[2][1]) / s, 0.25f * s, (r[1][0] - r[0][1]) / s};
  }
  const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  return l > 0.0f && std::isfinite(l) ? Quat(q.x / l, q.y / l, q.z / l, q.w / l) : Quat();
}

float det3(const float m[3][3]) {
  return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

/* -------------------------------------------------------------- decoding */

/* Round 567 (task 0549): a point is decoded from its own bytes into its own entries of the cloud's arrays, so the
 * readers' loops run on every core, this many points a piece. (From the editor a file is read on the import's
 * worker, round 413: the pieces then run on the background pool, never the main thread's.) */
constexpr int64_t kSplatGrain = 4096;

float sigmoid(float v) { return std::max(0.0f, std::min(1.0f, 1.0f / (1.0f + std::exp(-v)))); }
/* sRGB to linear, its ends exact (a byte 255 is 1.0, not one float step under it). */
float ply_srgb_to_linear(float c) {
  if (c <= 0.0f) return 0.0f;
  if (c >= 1.0f) return 1.0f;
  return c <= 0.04045f ? c / 12.92f : (float)std::pow((c + 0.055) / 1.055, 2.4);
}
/* An SPZ / PLY DC term to the base colour (ADR 0147: 0.5 + C0 * f_dc). */
Vec3 dc_to_color(float r, float g, float b) { return {0.5f + kSplatShC0 * r, 0.5f + kSplatShC0 * g, 0.5f + kSplatShC0 * b}; }

float half_to_float(uint16_t h) {
  const uint32_t sign = (uint32_t)(h & 0x8000u) << 16, expo = (h >> 10) & 0x1Fu, mant = h & 0x3FFu;
  uint32_t bits;
  if (expo == 0) {
    if (mant == 0) bits = sign;
    else {  // (subnormal: normalise)
      int e = -1;
      uint32_t m = mant;
      do {
        e++;
        m <<= 1;
      } while (!(m & 0x400u));
      bits = sign | ((uint32_t)(127 - 15 - e) << 23) | ((m & 0x3FFu) << 13);
    }
  }
  else if (expo == 31) bits = sign | 0x7F800000u | (mant << 13);
  else bits = sign | ((expo + 127 - 15) << 23) | (mant << 13);
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}

/* The SH of a file: `per_channel` coefficients a channel, of which the cloud keeps the bands its degree has. A note
 * when bands are dropped. */
int keep_degree(int per_channel, SplatReadReport &report, const char *format) {
  if (per_channel <= 0) return 0;
  int degree = splat_sh_degree_for_rest(per_channel);
  if (per_channel >= 24) {
    report.notes.push_back(strprintf("%s: spherical harmonics of degree 4 - Blendity keeps bands 1 to 3", format));
    return kSplatMaxShDegree;
  }
  if (splat_sh_rest_count(degree) != per_channel)
    report.notes.push_back(strprintf("%s: an incomplete spherical-harmonics band - importing %d of %d coefficients", format, splat_sh_rest_count(degree), per_channel));
  return degree;
}

PointCloudPtr new_cloud(PointCloudType type, int degree, size_t n, SplatReadReport &report) {
  auto pc = std::make_shared<PointCloud>();
  pc->type = type;
  pc->sh_degree = type == PointCloudType::GaussianSplats ? degree : 0;
  if (!pc->resize(n)) {
    report.error = n > kPointCloudMaxPoints ? "too many points" : "not enough memory for the points";
    return nullptr;
  }
  return pc;
}

/* Validation as every reader ends: a note for the dropped points; null when nothing usable remains. */
PointCloudPtr finish(PointCloudPtr pc, SplatReadReport &report, const char *format) {
  std::string why;
  const long long dropped = pointcloud_validate(*pc, &why);
  if (dropped < 0) {
    report.error = strprintf("%s: %s", format, why.c_str());
    return nullptr;
  }
  if (dropped > 0) report.notes.push_back(strprintf("%s: %lld points with no position (NaN) left out", format, dropped));
  pc->touch();
  return pc;
}

/* How every reader ends (round 567: each step timed for the stress rows): the file's axes to Blendity's, then finish. */
PointCloudPtr axes_and_finish(PointCloudPtr pc, const float m[3][3], SplatReadReport &report, const char *format) {
  const double t0 = now_seconds();
  pointcloud_change_axes(*pc, m);
  const double t1 = now_seconds();
  pc = finish(std::move(pc), report, format);
  report.times.axes_ms = (t1 - t0) * 1000.0, report.times.validate_ms = (now_seconds() - t1) * 1000.0;
  return pc;
}

/* ------------------------------------------------------------------- PLY */

enum class PlyTy : uint8_t { I8, U8, I16, U16, I32, U32, F32, F64 };
size_t ply_size(PlyTy t) { return t == PlyTy::I8 || t == PlyTy::U8 ? 1 : t == PlyTy::I16 || t == PlyTy::U16 ? 2 : t == PlyTy::F64 ? 8 : 4; }
bool ply_type(const std::string &t, PlyTy &out) {
  if (t == "char" || t == "int8") out = PlyTy::I8;
  else if (t == "uchar" || t == "uint8") out = PlyTy::U8;
  else if (t == "short" || t == "int16") out = PlyTy::I16;
  else if (t == "ushort" || t == "uint16") out = PlyTy::U16;
  else if (t == "int" || t == "int32") out = PlyTy::I32;
  else if (t == "uint" || t == "uint32") out = PlyTy::U32;
  else if (t == "float" || t == "float32") out = PlyTy::F32;
  else if (t == "double" || t == "float64") out = PlyTy::F64;
  else return false;
  return true;
}

struct PlyProp {
  std::string name;
  PlyTy ty = PlyTy::F32;
  bool list = false;
};
struct PlyElem {
  std::string name;
  uint64_t count = 0;
  std::vector<PlyProp> props;
  size_t stride = 0;  // (scalar-only elements)
  bool lists = false;
};
struct PlyHeader {
  int format = -1;  // 0 ascii, 1 little endian, 2 big endian
  std::vector<PlyElem> elems;
  size_t body = 0;
  std::string error;
};

PlyHeader parse_ply_header(const std::string &data) {
  PlyHeader h;
  const size_t end = data.find("end_header");
  if (data.compare(0, 3, "ply") != 0 || end == std::string::npos || end > (1u << 20)) {
    h.error = "not a PLY file";
    return h;
  }
  const size_t nl = data.find('\n', end);
  h.body = nl == std::string::npos ? data.size() : nl + 1;
  size_t at = 0;
  while (at < end) {
    size_t e = data.find('\n', at);
    if (e == std::string::npos || e > end) e = end;
    const std::vector<std::string> t = split_ws(data.substr(at, e - at));
    at = e + 1;
    if (t.empty()) continue;
    if (t[0] == "format" && t.size() > 1) h.format = t[1] == "ascii" ? 0 : t[1] == "binary_little_endian" ? 1 : t[1] == "binary_big_endian" ? 2 : -1;
    else if (t[0] == "element" && t.size() > 2) {
      PlyElem el;
      el.name = t[1];
      const double c = std::strtod(t[2].c_str(), nullptr);
      if (!(c >= 0.0) || c > 1e15) {
        h.error = "an element count that is not a count";
        return h;
      }
      el.count = (uint64_t)c;
      h.elems.push_back(el);
    }
    else if (t[0] == "property" && !h.elems.empty()) {
      PlyProp p;
      PlyElem &el = h.elems.back();
      if (t.size() > 4 && t[1] == "list") {
        p.list = true, el.lists = true, p.name = t[4];
        if (!ply_type(t[3], p.ty)) p.ty = PlyTy::F32;
      }
      else if (t.size() > 2) {
        p.name = t[2];
        if (!ply_type(t[1], p.ty)) {
          h.error = "a property of an unknown type (" + t[1] + ")";
          return h;
        }
        el.stride += ply_size(p.ty);
      }
      el.props.push_back(p);
    }
  }
  if (h.format < 0) h.error = "an unknown format";
  return h;
}

int find_prop(const PlyElem &el, const char *name) {
  for (size_t k = 0; k < el.props.size(); k++)
    if (!el.props[k].list && el.props[k].name == name) return (int)k;
  return -1;
}

const PlyElem *ply_vertex(const PlyHeader &h) {
  for (const PlyElem &el : h.elems)
    if (el.name == "vertex") return &el;
  return nullptr;
}

double ply_scalar(const char *q, PlyTy t, bool swap) {
  unsigned char b[8];
  const size_t n = ply_size(t);
  std::memcpy(b, q, n);
  if (swap) std::reverse(b, b + n);
  switch (t) {
    case PlyTy::I8: return (int8_t)b[0];
    case PlyTy::U8: return b[0];
    case PlyTy::I16: { int16_t v; std::memcpy(&v, b, 2); return v; }
    case PlyTy::U16: { uint16_t v; std::memcpy(&v, b, 2); return v; }
    case PlyTy::I32: { int32_t v; std::memcpy(&v, b, 4); return v; }
    case PlyTy::U32: { uint32_t v; std::memcpy(&v, b, 4); return v; }
    case PlyTy::F64: { double v; std::memcpy(&v, b, 8); return v; }
    default: { float v; std::memcpy(&v, b, 4); return v; }
  }
}

/* What a vertex's properties mean for the cloud, by index (-1: absent). */
struct PlyRoles {
  int x = -1, y = -1, z = -1, dc[3] = {-1, -1, -1}, opacity = -1, scale[3] = {-1, -1, -1}, rot[4] = {-1, -1, -1, -1}, rgb[3] = {-1, -1, -1};
  std::vector<int> rest;  // f_rest_0.. in order
  bool splats = false;
};

PlyRoles ply_roles(const PlyElem &v) {
  PlyRoles r;
  r.x = find_prop(v, "x"), r.y = find_prop(v, "y"), r.z = find_prop(v, "z");
  const char *dc[3] = {"f_dc_0", "f_dc_1", "f_dc_2"}, *sc[3] = {"scale_0", "scale_1", "scale_2"}, *rt[4] = {"rot_0", "rot_1", "rot_2", "rot_3"};
  for (int i = 0; i < 3; i++) r.dc[i] = find_prop(v, dc[i]), r.scale[i] = find_prop(v, sc[i]);
  for (int i = 0; i < 4; i++) r.rot[i] = find_prop(v, rt[i]);
  r.opacity = find_prop(v, "opacity");
  r.splats = r.opacity >= 0;
  for (int i = 0; i < 3; i++) r.splats = r.splats && r.dc[i] >= 0 && r.scale[i] >= 0;
  for (int i = 0; i < 4; i++) r.splats = r.splats && r.rot[i] >= 0;
  for (int i = 0; i < 73; i++) {  // (Blender reads up to 72; a 73rd says the degree is above 4)
    const int k = find_prop(v, ("f_rest_" + std::to_string(i)).c_str());
    if (k < 0) break;
    r.rest.push_back(k);
  }
  const char *rgb[3] = {"red", "green", "blue"}, *drgb[3] = {"diffuse_red", "diffuse_green", "diffuse_blue"};
  for (int i = 0; i < 3; i++) r.rgb[i] = find_prop(v, rgb[i]) >= 0 ? find_prop(v, rgb[i]) : find_prop(v, drgb[i]);
  return r;
}

/* A colour property's scale to 0..1 (integers from their range, read as sRGB; floats as they are). */
float ply_color(PlyTy t, double val) {
  if (t == PlyTy::U8) return ply_srgb_to_linear((float)val / 255.0f);
  if (t == PlyTy::U16) return ply_srgb_to_linear((float)val / 65535.0f);
  return (float)val;
}

/* What the vertex loop needs of the file, the same for every way its numbers are read. */
struct PlyTarget {
  PointCloud *pc = nullptr;
  const PlyElem *v = nullptr;
  const PlyRoles *roles = nullptr;
  size_t keep = 0, per_channel = 0;
};

/* One vertex into the cloud; `val(k)` is its property k (a float or a double: the cloud keeps floats). */
template<class Val> void ply_store_point(const PlyTarget &t, size_t i, const Val &val) {
  PointCloud &c = *t.pc;
  const PlyRoles &roles = *t.roles;
  c.position[i] = Vec3((float)val(roles.x), (float)val(roles.y), (float)val(roles.z));
  if (!roles.splats) {
    if (roles.rgb[0] < 0 || roles.rgb[1] < 0 || roles.rgb[2] < 0) return;
    auto channel = [&](int k) { return ply_color(t.v->props[(size_t)k].ty, (double)val(k)); };
    c.color[i] = Vec3(channel(roles.rgb[0]), channel(roles.rgb[1]), channel(roles.rgb[2]));
    return;
  }
  c.color[i] = dc_to_color((float)val(roles.dc[0]), (float)val(roles.dc[1]), (float)val(roles.dc[2]));
  c.opacity[i] = sigmoid((float)val(roles.opacity));
  c.scale[i] = Vec3(std::exp((float)val(roles.scale[0])), std::exp((float)val(roles.scale[1])), std::exp((float)val(roles.scale[2])));
  /* rot_0..3 are w, x, y, z (Blender's math::Quaternion order); validation normalises. */
  c.rotation[i] = Quat((float)val(roles.rot[1]), (float)val(roles.rot[2]), (float)val(roles.rot[3]), (float)val(roles.rot[0]));
  const int *rest = roles.rest.data();
  Vec3 *sh = c.sh_rest.data() + i * t.keep;
  for (size_t k = 0; k < t.keep; k++) sh[k] = Vec3((float)val(rest[k]), (float)val(rest[k + t.per_channel]), (float)val(rest[k + 2 * t.per_channel]));
}

/* Binary records whose every property is a little-endian float - what a 3DGS trainer writes: each value straight
 * from the record (round 567: before, every one went through a double and a switch on its type). */
void ply_read_floats(const PlyTarget &t, const char *records, size_t n) {
  const size_t stride = t.v->stride;
  JobSystem::global().parallel_for((int64_t)n, kSplatGrain, [&](int64_t b, int64_t e) {
    for (int64_t i = b; i < e; i++) {
      const char *rec = records + (size_t)i * stride;
      ply_store_point(t, (size_t)i, [rec](int k) {
        float f;
        std::memcpy(&f, rec + 4 * (size_t)k, 4);
        return f;
      });
    }
  });
}

/* Binary records of any types, either byte order. */
void ply_read_binary(const PlyTarget &t, const char *records, size_t n, bool swap) {
  const std::vector<PlyProp> &props = t.v->props;
  std::vector<size_t> off(props.size(), 0);
  for (size_t k = 1; k < props.size(); k++) off[k] = off[k - 1] + ply_size(props[k - 1].ty);
  const size_t stride = t.v->stride;
  JobSystem::global().parallel_for((int64_t)n, kSplatGrain, [&](int64_t b, int64_t e) {
    for (int64_t i = b; i < e; i++) {
      const char *rec = records + (size_t)i * stride;
      ply_store_point(t, (size_t)i, [&](int k) { return ply_scalar(rec + off[(size_t)k], props[(size_t)k].ty, swap); });
    }
  });
}

/* One line of text into `vals`, a number a property; false when the line has fewer. */
bool ply_parse_line(const char *q, const char *qe, std::vector<double> &vals) {
  bool whole = true;
  for (double &val : vals) {
    while (q < qe && std::isspace((unsigned char)*q)) q++;
    const std::string tok(q, (size_t)(std::find_if(q, qe, [](char c) { return std::isspace((unsigned char)c) != 0; }) - q));
    val = tok.empty() ? std::nan("") : std::strtod(tok.c_str(), nullptr);
    whole = whole && !tok.empty();
    q += tok.size();
  }
  return whole;
}

/* Text records, a line each. False when some line had fewer numbers than properties. */
bool ply_read_text(const PlyTarget &t, const std::vector<std::pair<const char *, const char *>> &lines, size_t n) {
  std::atomic<bool> short_line{false};
  const size_t nprops = t.v->props.size();
  JobSystem::global().parallel_for((int64_t)n, kSplatGrain / 4, [&](int64_t b, int64_t e) {
    std::vector<double> vals(nprops);
    for (int64_t i = b; i < e; i++) {
      if (!ply_parse_line(lines[(size_t)i].first, lines[(size_t)i].second, vals)) short_line.store(true, std::memory_order_relaxed);
      ply_store_point(t, (size_t)i, [&](int k) { return vals[(size_t)k]; });
    }
  });
  return !short_line.load();
}

}  // namespace

bool ply_is_point_cloud(const std::string &data) {
  const PlyHeader h = parse_ply_header(data);
  if (!h.error.empty()) return false;
  const PlyElem *v = ply_vertex(h);
  if (!v || v->lists || find_prop(*v, "x") < 0 || find_prop(*v, "y") < 0 || find_prop(*v, "z") < 0) return false;
  for (const PlyElem &el : h.elems)
    if ((el.name == "face" || el.name == "edge" || el.name == "tristrips") && el.count > 0) return false;
  return true;
}

PointCloudPtr read_ply_point_cloud(const std::string &data, SplatReadReport &report) {
  const double started = now_seconds();
  const PlyHeader h = parse_ply_header(data);
  if (!h.error.empty()) {
    report.error = "PLY: " + h.error;
    return nullptr;
  }
  const PlyElem *v = ply_vertex(h);
  if (!v || v->lists) {
    report.error = "PLY: no vertex element of plain numbers";
    return nullptr;
  }
  const PlyRoles roles = ply_roles(*v);
  if (roles.x < 0 || roles.y < 0 || roles.z < 0) {
    report.error = "PLY: the vertices have no x, y, z";
    return nullptr;
  }
  if (v->count > kPointCloudMaxPoints) {
    report.error = "PLY: too many points";
    return nullptr;
  }
  if (v->count == 0) {
    report.error = "PLY: no points";
    return nullptr;
  }
  /* Where the vertex records start, and the check that they are all there - before anything the size of the count is
   * allocated. (Elements before the vertices are skipped by their stride; one with lists can't be.) */
  const char *p = data.data() + h.body, *e = data.data() + data.size();
  std::vector<std::pair<const char *, const char *>> lines;  // (ASCII: the vertex records' lines)
  if (h.format != 0) {
    for (const PlyElem &el : h.elems) {
      if (&el == v) break;
      if (el.count == 0) continue;
      if (el.lists || el.count > (uint64_t)(e - p) / std::max<size_t>(1, el.stride)) {
        report.error = "PLY: an element before the vertices can't be skipped";
        return nullptr;
      }
      p += el.count * el.stride;
    }
    if (v->stride == 0 || v->count > (uint64_t)(e - p) / v->stride) {
      report.error = "PLY: the file ends before its points do";
      return nullptr;
    }
  }
  else {
    uint64_t skip = 0;
    for (const PlyElem &el : h.elems) {
      if (&el == v) break;
      skip += el.count;
    }
    for (const char *q = p; q < e && lines.size() < skip + v->count;) {
      const char *nl = (const char *)std::memchr(q, '\n', (size_t)(e - q));
      const char *le = nl ? nl : e;
      const char *a = q;
      while (a < le && std::isspace((unsigned char)*a)) a++;
      if (a < le) lines.push_back({a, le});
      q = nl ? nl + 1 : e;
    }
    if (lines.size() < skip + v->count) {
      report.error = "PLY: the file ends before its points do";
      return nullptr;
    }
    lines.erase(lines.begin(), lines.begin() + (ptrdiff_t)skip);
  }

  /* The SH as the file has them: f_rest channel-major (all R, then G, then B). */
  int per_channel = 0, degree = 0;
  if (roles.splats && !roles.rest.empty()) {
    if (roles.rest.size() > 72) report.notes.push_back("PLY: spherical harmonics above degree 4 - not imported");
    else if (roles.rest.size() % 3) report.notes.push_back(strprintf("PLY: %zu f_rest properties (not a multiple of 3) - spherical harmonics not imported", roles.rest.size()));
    else {
      per_channel = (int)roles.rest.size() / 3;
      degree = keep_degree(per_channel, report, "PLY");
    }
  }
  const size_t n = (size_t)v->count;
  PointCloudPtr pc = new_cloud(roles.splats ? PointCloudType::GaussianSplats : PointCloudType::Points, degree, n, report);
  if (!pc) {
    report.error = "PLY: " + report.error;
    return nullptr;
  }
  pc->name = "PLY";
  const double decoding = now_seconds();
  report.times.setup_ms = (decoding - started) * 1000.0;
  PlyTarget target;
  target.pc = pc.get(), target.v = v, target.roles = &roles;
  target.keep = (size_t)splat_sh_rest_count(degree), target.per_channel = (size_t)per_channel;
  bool all_float = h.format == 1;
  for (const PlyProp &prop : v->props) all_float = all_float && prop.ty == PlyTy::F32;
  if (all_float) ply_read_floats(target, p, n);
  else if (h.format != 0) ply_read_binary(target, p, n, h.format == 2);
  else if (!ply_read_text(target, lines, n)) report.notes.push_back("PLY: some lines have fewer numbers than properties");
  report.times.decode_ms = (now_seconds() - decoding) * 1000.0;
  return axes_and_finish(pc, kFromZUp, report, "PLY");
}

/* ------------------------------------------------------------------- SPZ */

namespace {

constexpr uint32_t kSpzMagic = 0x5053474e;  // "NGSP"
constexpr float kSpzColorScale = 0.15f;

struct SpzLayout {
  uint32_t version = 0, n = 0;
  int sh_degree = 0, fractional_bits = 0, flags = 0;
  size_t pos_bytes = 9, rot_bytes = 4, sh_coeffs = 0;  // (per point)
};

/* Blender's check_common_header_for_errors, plus the cloud's own cap. */
bool spz_check(const SpzLayout &L, uint32_t magic, uint32_t min_v, uint32_t max_v, SplatReadReport &report) {
  if (magic != kSpzMagic) report.error = strprintf("SPZ: unexpected header magic 0x%X", magic);
  else if (L.version < min_v || L.version > max_v) report.error = strprintf("SPZ: unsupported version %u", L.version);
  else if (L.n > kPointCloudMaxPoints || L.n > (uint32_t)INT32_MAX) report.error = "SPZ: too many points";
  else if (L.sh_degree > 4) report.error = strprintf("SPZ: unsupported SH degree %d", L.sh_degree);
  else if (L.fractional_bits >= 24) report.error = strprintf("SPZ: unsupported number of position fractional bits %d", L.fractional_bits);
  else if (L.n == 0) report.error = "SPZ: no points";
  return report.error.empty();
}

/* The six attribute blocks of a cloud, one after another (v1-v3: in the one gzip stream; v4: one stream each). */
struct SpzBlocks {
  const uint8_t *pos = nullptr, *alpha = nullptr, *color = nullptr, *scale = nullptr, *rot = nullptr, *sh = nullptr;
};

Vec3 spz_position(const SpzLayout &L, const uint8_t *pos, size_t i, float fixed) {
  if (L.version == 1) {  // (float16 positions: Niantic's reference)
    const uint8_t *q = pos + i * 6;
    float c[3];
    for (int k = 0; k < 3; k++) c[k] = half_to_float((uint16_t)(q[2 * k] | (q[2 * k + 1] << 8)));
    return Vec3(c[0], c[1], c[2]);
  }
  const uint8_t *q = pos + i * 9;
  int32_t c[3];
  for (int k = 0; k < 3; k++) {
    c[k] = (int32_t)(q[3 * k] | (q[3 * k + 1] << 8) | (q[3 * k + 2] << 16));
    if (c[k] & 0x800000) c[k] |= (int32_t)0xFF000000;
  }
  return Vec3((float)c[0] * fixed, (float)c[1] * fixed, (float)c[2] * fixed);
}

Quat spz_rotation(const SpzLayout &L, const uint8_t *rot, size_t i) {
  if (L.version < 3) {
    const uint8_t *r = rot + i * 3;
    const Vec3 xyz = Vec3((float)r[0], (float)r[1], (float)r[2]) / 127.5f - Vec3(1.0f);
    return Quat(xyz.x, xyz.y, xyz.z, std::sqrt(std::max(0.0f, 1.0f - dot(xyz, xyz))));
  }
  const uint8_t *r = rot + i * 4;
  uint32_t packed = (uint32_t)r[0] | ((uint32_t)r[1] << 8) | ((uint32_t)r[2] << 16) | ((uint32_t)r[3] << 24);
  constexpr uint32_t kMask = (1u << 9) - 1;
  const int largest = (int)(packed >> 30);
  float q[4] = {0, 0, 0, 0};  // x, y, z, w
  float sum = 0.0f;
  for (int k = 3; k >= 0; k--) {
    if (k == largest) continue;
    const float mag = (float)(packed & kMask) / (float)kMask, sign = ((packed >> 9) & 1u) ? -1.0f : 1.0f;
    q[k] = 0.70710678118654752f * mag * sign;
    sum += q[k] * q[k];
    packed >>= 10;
  }
  q[largest] = std::sqrt(std::max(0.0f, 1.0f - sum));
  return Quat(q[0], q[1], q[2], q[3]);
}

PointCloudPtr spz_decode(const SpzLayout &L, const SpzBlocks &b, SplatReadReport &report) {
  const double started = now_seconds();
  const int degree = keep_degree((int)L.sh_coeffs, report, "SPZ");
  PointCloudPtr pc = new_cloud(PointCloudType::GaussianSplats, degree, L.n, report);
  if (!pc) {
    report.error = "SPZ: " + report.error;
    return nullptr;
  }
  pc->name = "SPZ";
  const double decoding = now_seconds();
  report.times.setup_ms = (decoding - started) * 1000.0;
  const size_t keep = (size_t)splat_sh_rest_count(degree);
  const float fixed = 1.0f / (float)(1 << L.fractional_bits);
  PointCloud &c = *pc;
  JobSystem::global().parallel_for((int64_t)L.n, kSplatGrain, [&](int64_t begin, int64_t end) {
    for (size_t i = (size_t)begin; i < (size_t)end; i++) {
      c.position[i] = spz_position(L, b.pos, i, fixed);
      c.opacity[i] = (float)b.alpha[i] / 255.0f;  // (the stored sigmoid: Blender's read_alphas)
      const uint8_t *col = b.color + i * 3;
      auto dc = [](uint8_t v) { return ((float)v / 255.0f - 0.5f) / kSpzColorScale; };
      c.color[i] = dc_to_color(dc(col[0]), dc(col[1]), dc(col[2]));
      const uint8_t *s = b.scale + i * 3;
      c.scale[i] = Vec3(std::exp((float)s[0] / 16.0f - 10.0f), std::exp((float)s[1] / 16.0f - 10.0f), std::exp((float)s[2] / 16.0f - 10.0f));
      c.rotation[i] = spz_rotation(L, b.rot, i);
      /* SH: a point's coefficients one after another, each R G B, as (b - 128) / 128. */
      const uint8_t *h = b.sh + i * L.sh_coeffs * 3;
      Vec3 *sh = c.sh_rest.data() + i * keep;
      for (size_t k = 0; k < keep; k++) sh[k] = Vec3(((float)h[3 * k] - 128.0f) / 128.0f, ((float)h[3 * k + 1] - 128.0f) / 128.0f, ((float)h[3 * k + 2] - 128.0f) / 128.0f);
    }
  });
  if (L.flags & 1) report.notes.push_back("SPZ: trained with anti-aliasing, which Blendity draws as an ordinary splat (as Blender does)");
  if (L.flags & 2) report.notes.push_back("SPZ: the file's extensions are not read (as Blender)");
  report.times.decode_ms = (now_seconds() - decoding) * 1000.0;
  return axes_and_finish(pc, kMirrorX, report, "SPZ");
}

uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
uint64_t le64(const uint8_t *p) { return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32); }

void spz_sizes(SpzLayout &L) {
  L.pos_bytes = L.version == 1 ? 6 : 9;
  L.rot_bytes = L.version >= 3 ? 4 : 3;
  L.sh_coeffs = (size_t)(L.sh_degree == 0 ? 0 : L.sh_degree == 1 ? 3 : L.sh_degree == 2 ? 8 : L.sh_degree == 3 ? 15 : 24);
}

PointCloudPtr spz_gzip(const std::string &bytes, SplatReadReport &report) {
  const double started = now_seconds();
  std::vector<uint8_t> raw;
  std::string why;
  /* Deflate packs at most about 1032:1; a bigger promise is a damaged or hostile file. */
  const size_t limit = (size_t)std::min<uint64_t>((uint64_t)3 << 30, (uint64_t)bytes.size() * 1100 + 4096);
  if (!gunzip(bytes, raw, why, limit)) {
    report.error = "SPZ: the gzip stream can't be read (" + why + ")";
    return nullptr;
  }
  report.times.unpack_ms = (now_seconds() - started) * 1000.0;
  if (raw.size() < 16) {
    report.error = "SPZ: the header is cut short";
    return nullptr;
  }
  const uint8_t *h = raw.data();
  SpzLayout L;
  L.version = le32(h + 4), L.n = le32(h + 8), L.sh_degree = h[12], L.fractional_bits = h[13], L.flags = h[14];
  if (!spz_check(L, le32(h), 1, 3, report)) return nullptr;
  spz_sizes(L);
  const uint64_t per = L.pos_bytes + 1 + 3 + 3 + L.rot_bytes + 3 * L.sh_coeffs;
  if ((uint64_t)(raw.size() - 16) / per < L.n) {
    report.error = "SPZ: the data ends before its points do";
    return nullptr;
  }
  SpzBlocks b;
  b.pos = h + 16;
  b.alpha = b.pos + (size_t)L.n * L.pos_bytes;
  b.color = b.alpha + L.n;
  b.scale = b.color + (size_t)L.n * 3;
  b.rot = b.scale + (size_t)L.n * 3;
  b.sh = b.rot + (size_t)L.n * L.rot_bytes;
  return spz_decode(L, b, report);
}

PointCloudPtr spz_ngsp(const std::string &bytes, SplatReadReport &report) {
  if (bytes.size() < 32) {
    report.error = "SPZ: the header is cut short";
    return nullptr;
  }
  const uint8_t *h = (const uint8_t *)bytes.data();
  SpzLayout L;
  L.version = le32(h + 4), L.n = le32(h + 8), L.sh_degree = h[12], L.fractional_bits = h[13], L.flags = h[14];
  const int streams = h[15];
  const uint32_t toc = le32(h + 16);
  if (!spz_check(L, le32(h), 4, 4, report)) return nullptr;
  if (toc < 32) report.error = "SPZ: the table of streams overlaps the header";
  else if (streams < 6) report.error = "SPZ: fewer than the six streams";
  else if ((uint64_t)toc + (uint64_t)streams * 16 > bytes.size()) report.error = "SPZ: the table of streams is cut short";
  if (!report.error.empty()) return nullptr;
#ifndef BL_WITH_ZSTD
  report.error = "SPZ: version 4 files are compressed with Zstandard, which this build of Blendity (without Blender's libraries) can't read";
  return nullptr;
#else
  spz_sizes(L);
  const uint64_t want[6] = {(uint64_t)L.n * L.pos_bytes, L.n, (uint64_t)L.n * 3, (uint64_t)L.n * 3, (uint64_t)L.n * L.rot_bytes, (uint64_t)L.n * 3 * L.sh_coeffs};
  std::vector<std::string> plain(6);
  const double started = now_seconds();
  uint64_t at = (uint64_t)toc + (uint64_t)streams * 16;
  for (int s = 0; s < streams; s++) {
    const uint64_t csize = le64(h + toc + 16 * (size_t)s), usize = le64(h + toc + 16 * (size_t)s + 8);
    if (csize > bytes.size() || at > bytes.size() - csize) {
      report.error = "SPZ: a stream runs past the end of the file";
      return nullptr;
    }
    if (s < 6) {
      if (usize != want[s]) {
        report.error = strprintf("SPZ: stream %d holds %llu bytes, %llu expected for %u points", s, (unsigned long long)usize, (unsigned long long)want[s], L.n);
        return nullptr;
      }
      /* Before anything the size of the header's count is allocated (review 531): a zstd frame can't grow past about
       * 32K:1 (its RLE blocks), and the frame must say the same size the table does. */
      const unsigned long long framed = csize ? ZSTD_getFrameContentSize(bytes.data() + at, (size_t)csize) : 0;
      if (usize && (usize > csize * 32768ull + 4096ull || framed != usize)) {
        report.error = strprintf("SPZ: stream %d's size doesn't match its compressed data", s);
        return nullptr;
      }
      try {
        plain[(size_t)s].resize((size_t)usize);
      }
      catch (const std::bad_alloc &) {
        report.error = "SPZ: not enough memory for the points";
        return nullptr;
      }
      const size_t got = usize ? ZSTD_decompress(&plain[(size_t)s][0], (size_t)usize, bytes.data() + at, (size_t)csize) : 0;
      if (usize && (ZSTD_isError(got) || got != usize)) {
        report.error = strprintf("SPZ: stream %d can't be decompressed", s);
        return nullptr;
      }
    }
    at += csize;
  }
  report.times.unpack_ms = (now_seconds() - started) * 1000.0;
  SpzBlocks b;
  b.pos = (const uint8_t *)plain[0].data(), b.alpha = (const uint8_t *)plain[1].data(), b.color = (const uint8_t *)plain[2].data();
  b.scale = (const uint8_t *)plain[3].data(), b.rot = (const uint8_t *)plain[4].data(), b.sh = (const uint8_t *)plain[5].data();
  return spz_decode(L, b, report);
#endif
}

}  // namespace

PointCloudPtr read_spz(const std::string &bytes, SplatReadReport &report) {
  if (bytes.size() >= 4 && le32((const uint8_t *)bytes.data()) == kSpzMagic) return spz_ngsp(bytes, report);
  if (bytes.size() >= 2 && (uint8_t)bytes[0] == 0x1f && (uint8_t)bytes[1] == 0x8b) return spz_gzip(bytes, report);
  report.error = "SPZ: not an SPZ file (neither gzip nor NGSP)";
  return nullptr;
}

/* ------------------------------------------------------------------ glTF */

PointCloudPtr gltf_point_cloud(const std::map<std::string, std::vector<float>> &attrs, const std::map<std::string, int> &components, bool splats,
                               SplatReadReport &report) {
  auto find = [&](const std::string &name, int comps) -> const std::vector<float> * {
    auto it = attrs.find(name);
    auto ct = components.find(name);
    if (it == attrs.end() || ct == components.end() || ct->second != comps) return nullptr;
    return &it->second;
  };
  const std::vector<float> *pos = find("POSITION", 3);
  if (!pos || pos->empty()) {
    report.error = "glTF: a point primitive with no POSITION";
    return nullptr;
  }
  const size_t n = pos->size() / 3;
  auto fits = [&](const std::vector<float> *a, size_t comps) { return a && a->size() == n * comps; };
  const std::string pre = "KHR_gaussian_splatting:";
  const std::vector<float> *scale = find(pre + "SCALE", 3), *rot = find(pre + "ROTATION", 4), *opacity = find(pre + "OPACITY", 1),
                           *dc = find(pre + "SH_DEGREE_0_COEF_0", 3);
  const std::vector<float> *col3 = find("COLOR_0", 3), *col4 = find("COLOR_0", 4);
  if (splats && !(fits(scale, 3) && fits(rot, 4) && fits(opacity, 1))) {
    report.notes.push_back("glTF: a Gaussian-splat primitive without its scale, rotation or opacity - imported as points");
    splats = false;
  }
  /* The SH bands the file has, complete: SH_DEGREE_l_COEF_k for k = 0..2l. */
  int degree = 0;
  if (splats)
    for (int l = 1; l <= 4; l++) {
      bool all = true;
      for (int k = 0; k <= 2 * l && all; k++) all = fits(find(strprintf("%sSH_DEGREE_%d_COEF_%d", pre.c_str(), l, k), 3), 3);
      if (!all) break;
      degree = l;
    }
  if (degree > kSplatMaxShDegree) {
    report.notes.push_back("glTF: spherical harmonics of degree 4 - Blendity keeps bands 1 to 3");
    degree = kSplatMaxShDegree;
  }
  PointCloudPtr pc = new_cloud(splats ? PointCloudType::GaussianSplats : PointCloudType::Points, degree, n, report);
  if (!pc) {
    report.error = "glTF: " + report.error;
    return nullptr;
  }
  pc->name = "glTF";
  const size_t keep = (size_t)splat_sh_rest_count(degree);
  std::vector<const std::vector<float> *> bands;
  for (int l = 1; l <= degree; l++)
    for (int k = 0; k <= 2 * l; k++) bands.push_back(find(strprintf("%sSH_DEGREE_%d_COEF_%d", pre.c_str(), l, k), 3));
  for (size_t i = 0; i < n; i++) {
    pc->position[i] = Vec3((*pos)[3 * i], (*pos)[3 * i + 1], (*pos)[3 * i + 2]);
    if (splats && fits(dc, 3)) pc->color[i] = dc_to_color((*dc)[3 * i], (*dc)[3 * i + 1], (*dc)[3 * i + 2]);
    else if (fits(col3, 3)) pc->color[i] = Vec3((*col3)[3 * i], (*col3)[3 * i + 1], (*col3)[3 * i + 2]);
    else if (fits(col4, 4)) pc->color[i] = Vec3((*col4)[4 * i], (*col4)[4 * i + 1], (*col4)[4 * i + 2]);
    if (!splats) continue;
    pc->scale[i] = Vec3((*scale)[3 * i], (*scale)[3 * i + 1], (*scale)[3 * i + 2]);
    pc->rotation[i] = Quat((*rot)[4 * i], (*rot)[4 * i + 1], (*rot)[4 * i + 2], (*rot)[4 * i + 3]);  // (glTF: x, y, z, w)
    pc->opacity[i] = (*opacity)[i];
    for (size_t k = 0; k < keep; k++) pc->sh_rest[i * keep + k] = Vec3((*bands[k])[3 * i], (*bands[k])[3 * i + 1], (*bands[k])[3 * i + 2]);
  }
  pointcloud_change_axes(*pc, kMirrorX);
  return finish(pc, report, "glTF");
}

/* ------------------------------------------------------------ axis change */

void splat_sh_band_matrix(int band, const float m[3][3], double *out) {
  /* f_new(d) = f_old(m^T d) for every direction d: sampled at 32 directions spread over the sphere, the band's
   * coefficient map is the least-squares solution of A T = B, A the basis at d, B the basis at m^T d. Exact (to
   * rounding) because a band is closed under rotations and reflections. */
  const int n = 2 * band + 1, first = band * band;
  constexpr int kDirs = 32;
  double A[kDirs][7], B[kDirs][7];
  for (int k = 0; k < kDirs; k++) {
    const double zz = 1.0 - (2.0 * k + 1.0) / kDirs, r = std::sqrt(std::max(0.0, 1.0 - zz * zz)), phi = k * 2.399963229728653;
    const Vec3 d((float)(r * std::cos(phi)), (float)(r * std::sin(phi)), (float)zz);
    const Vec3 dt(m[0][0] * d.x + m[1][0] * d.y + m[2][0] * d.z, m[0][1] * d.x + m[1][1] * d.y + m[2][1] * d.z, m[0][2] * d.x + m[1][2] * d.y + m[2][2] * d.z);
    float ya[16], yb[16];
    splat_sh_basis(d, ya);
    splat_sh_basis(dt, yb);
    for (int c = 0; c < n; c++) A[k][c] = ya[first + c], B[k][c] = yb[first + c];
  }
  /* Normal equations (A^T A) T = A^T B, solved by Gauss-Jordan with partial pivoting. */
  double M[7][14] = {};
  for (int r = 0; r < n; r++)
    for (int c = 0; c < n; c++) {
      double ata = 0.0, atb = 0.0;
      for (int k = 0; k < kDirs; k++) ata += A[k][r] * A[k][c], atb += A[k][r] * B[k][c];
      M[r][c] = ata, M[r][n + c] = atb;
    }
  for (int c = 0; c < n; c++) {
    int piv = c;
    for (int r = c + 1; r < n; r++)
      if (std::fabs(M[r][c]) > std::fabs(M[piv][c])) piv = r;
    for (int k = 0; k < 2 * n; k++) std::swap(M[c][k], M[piv][k]);
    const double d = M[c][c];
    for (int k = 0; k < 2 * n; k++) M[c][k] /= d;
    for (int r = 0; r < n; r++) {
      if (r == c || M[r][c] == 0.0) continue;
      const double f = M[r][c];
      for (int k = 0; k < 2 * n; k++) M[r][k] -= f * M[c][k];
    }
  }
  /* X = (A^T A)^-1 A^T B solves A X = B: column j of X is Y_j(m^T d) written in the Y_i(d), X[i][j] its part of
   * Y_i. f_new(d) = sum_j c_old[j] Y_j(m^T d) = sum_i (sum_j X[i][j] c_old[j]) Y_i(d), so c_new = X c_old. */
  for (int i = 0; i < n; i++)
    for (int j = 0; j < n; j++) {
      const double t = M[i][n + j];
      out[i * n + j] = std::fabs(t) < 1e-9 ? 0.0 : t;
    }
}

namespace {

/* A splat's turn under the change of axes `m` (`mirror`: its determinant is -1). */
Quat turn_changed_axes(Quat q, const float m[3][3], bool mirror) {
  /* Unit first (review 531): a 3DGS trainer saves its rotations unnormalised, and the matrix formula below holds only
   * for a unit quaternion (|q|^2 = k gives (1 - k) I + k R, not R). A zero or non-finite one is no turn. */
  const float l2 = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
  if (!std::isfinite(l2) || l2 < 1e-20f) q = Quat();
  else {
    const float inv = 1.0f / std::sqrt(l2);
    q = {q.x * inv, q.y * inv, q.z * inv, q.w * inv};
  }
  /* R' = M R D, D flipping the third axis when M mirrors: R' is a rotation again, and the ellipsoid R S S R^T
   * maps to M R S S R^T M^T because D S S D = S S. */
  float r[3][3], out[3][3];
  quat_to_matrix(q, r);
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) {
      float s = 0.0f;
      for (int k = 0; k < 3; k++) s += m[i][k] * r[k][j];
      out[i][j] = j == 2 && mirror ? -s : s;
    }
  return matrix_to_quat(out);
}

/* One point's SH coefficients (`c`, sh_rest's order) through the bands' maps `T`. */
void sh_changed_axes(Vec3 *c, int degree, const double T[3][49]) {
  Vec3 old[15];
  std::copy(c, c + splat_sh_rest_count(degree), old);
  for (int l = 1; l <= degree; l++) {
    const int n = 2 * l + 1, first = l * l - 1;  // (sh_rest has no constant term: band l starts at l^2 - 1)
    for (int a = 0; a < n; a++) {
      double x = 0, y = 0, z = 0;
      for (int b = 0; b < n; b++) {
        const double t = T[l - 1][a * n + b];
        x += t * old[first + b].x, y += t * old[first + b].y, z += t * old[first + b].z;
      }
      c[first + a] = Vec3((float)x, (float)y, (float)z);
    }
  }
}

}  // namespace

void pointcloud_change_axes(PointCloud &pc, const float m[3][3]) {
  const bool mirror = det3(m) < 0.0f;
  const int degree = pc.is_splats() ? std::max(0, std::min(kSplatMaxShDegree, pc.sh_degree)) : 0;
  const size_t n = pc.count(), rest = (size_t)splat_sh_rest_count(degree);
  const bool turns = pc.is_splats() && pc.rotation.size() == n, bands = degree > 0 && pc.sh_rest.size() == n * rest;
  double T[3][49];
  for (int l = 1; l <= degree; l++) splat_sh_band_matrix(l, m, T[l - 1]);
  if (pc.is_splats() && !turns)  // (a cloud not yet validated, its turns another length than its points: all of them, as before)
    for (Quat &q : pc.rotation) q = turn_changed_axes(q, m, mirror);
  /* Round 567: a point's position, turn and coefficients are its own - every core takes a piece of the cloud. */
  JobSystem::global().parallel_for((int64_t)n, kSplatGrain, [&](int64_t begin, int64_t end) {
    for (size_t i = (size_t)begin; i < (size_t)end; i++) {
      pc.position[i] = mul(m, pc.position[i]);
      if (turns) pc.rotation[i] = turn_changed_axes(pc.rotation[i], m, mirror);
      if (bands) sh_changed_axes(pc.sh_rest.data() + i * rest, degree, T);
    }
  });
}

}  // namespace bl

/* ------------------------------------------------------------ file entry */

namespace bl {

/* A cloud as the import's one node, named for the file; the reader's notes go to the import's. */
static bool cloud_node(PointCloudPtr pc, const std::string &path, SplatReadReport &report, ImportResult &out) {
  for (std::string &n : report.notes) out.notes.push_back(std::move(n));
  if (!pc) {
    out.error = report.error.empty() ? "the point cloud could not be read" : report.error;
    return false;
  }
  ImportedNode node;
  node.name = fs::stem(path);
  pc->name = node.name;
  node.points = pc;
  out.summary = strprintf("%zu %s", pc->count(), pc->is_splats() ? strprintf("Gaussian splats, SH degree %d", pc->sh_degree).c_str() : "points");
  out.nodes.push_back(std::move(node));
  return true;
}

bool import_ply_point_cloud(const std::string &path, const std::string &data, ImportResult &out) {
  if (out.source.empty()) out.source = path;
  SplatReadReport report;
  PointCloudPtr pc = read_ply_point_cloud(data, report);
  return cloud_node(pc, path, report, out);
}

bool import_spz_file(const std::string &path, ImportResult &out) {
  if (out.source.empty()) out.source = path;
  std::string bytes;
  if (!fs::read_file(path, bytes)) {
    out.error = "cannot read " + path;
    return false;
  }
  SplatReadReport report;
  PointCloudPtr pc = read_spz(bytes, report);
  return cloud_node(pc, path, report, out);
}

}  // namespace bl

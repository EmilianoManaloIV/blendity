// SPDX-License-Identifier: GPL-2.0-or-later
// Stress: the Gaussian splat and point cloud readers (round 531, task 0511, ADR 0147). Included by stress_main.cpp;
// `--only splats` (`--quick` for a shorter pass). Two parts:
//  1. a fuzz of the three readers - the PLY reader, the SPZ reader (v1-v3 gzip and v4 NGSP) and the glTF point
//     reader (the attribute function, and whole .glb files through import_model). Valid files from the builders below
//     (copied from tests/test_splats.cpp: test helpers are not linked into the stress program) are damaged over a few
//     thousand seeds: bits flipped, bytes set, the file cut short, chunks spliced, inserted and erased, header numbers
//     set to 0 / -1 / 2^31 / 2^28 + 1 / NaN / infinity, a PLY's counts and property types replaced, a glTF's counts and
//     offsets replaced. Every result must be a clean refusal (a non-empty error) or a cloud that is consistent() with
//     every value finite. Counts go in a table; any problem is listed with its seed.
//  2. a measurement row for import speed (100k splats: PLY binary and ASCII, SPZ v3). Times are printed, never judged
//     (ADR 0056).
// Nothing here touches the user's files: the one file it writes (a .glb for import_model) goes in the temp folder.
#include "../src/scene/import_splat.h"
#include "../src/scene/pointcloud.h"
#include "../src/scene/unity_package.h"

#include <cctype>
#include <cfloat>

namespace splatstress {

struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x1234567ull) {}
  uint64_t next() {
    s += 0x9E3779B97F4A7C15ull;
    uint64_t z = s;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }
  uint32_t below(uint32_t n) { return n ? (uint32_t)(next() % n) : 0; }
  float unit() { return (float)(next() >> 40) / 16777216.0f; }
};

/* ------------------------------------------------------------------ builders */

static void put32(std::string &s, uint32_t v) {
  for (int k = 0; k < 4; k++) s += (char)((v >> (8 * k)) & 0xFF);
}
static void put64(std::string &s, uint64_t v) {
  put32(s, (uint32_t)v);
  put32(s, (uint32_t)(v >> 32));
}
static float sv(int i, int k) { return std::sin(1.7f * (float)(i * 97 + k) + 0.3f); }

/* format 0 ascii, 1 little endian, 2 big endian. */
static void put_float(std::string &s, float x, int format) {
  if (format == 0) {
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.9g ", x);
    s += buf;
    return;
  }
  unsigned char b[4];
  std::memcpy(b, &x, 4);
  if (format == 2) {
    std::swap(b[0], b[3]);
    std::swap(b[1], b[2]);
  }
  s.append((const char *)b, 4);
}

static const char *ply_format_name(int format) { return format == 0 ? "ascii" : format == 1 ? "binary_little_endian" : "binary_big_endian"; }

/* A 3DGS PLY with normals and `per_channel` f_rest coefficients a channel. */
static std::string make_ply(int format, int n, int per_channel) {
  std::string s = std::string("ply\nformat ") + ply_format_name(format) + " 1.0\ncomment made by the stress section\nelement vertex " + std::to_string(n) + "\n";
  std::vector<std::string> props = {"x", "y", "z", "nx", "ny", "nz", "f_dc_0", "f_dc_1", "f_dc_2"};
  for (int k = 0; k < 3 * per_channel; k++) props.push_back("f_rest_" + std::to_string(k));
  for (const char *p : {"opacity", "scale_0", "scale_1", "scale_2", "rot_0", "rot_1", "rot_2", "rot_3"}) props.push_back(p);
  for (const std::string &p : props) s += "property float " + p + "\n";
  s += "end_header\n";
  for (int i = 0; i < n; i++) {
    std::vector<float> v;
    for (int k = 0; k < 3; k++) v.push_back(2.0f * sv(i, k));
    for (int k = 0; k < 3; k++) v.push_back(0.0f);
    for (int k = 0; k < 3; k++) v.push_back(sv(i, 10 + k));
    for (int j = 0; j < 3 * per_channel; j++) v.push_back(0.2f * sv(i, 100 + j));
    v.push_back(2.0f * sv(i, 30));
    for (int k = 0; k < 3; k++) v.push_back(-3.0f + sv(i, 20 + k));
    for (int k = 0; k < 4; k++) v.push_back(sv(i, 40 + k) + (k == 0 ? 1.5f : 0.0f));
    for (float x : v) put_float(s, x, format);
    if (format == 0) s += "\n";
  }
  return s;
}

/* A plain point PLY: x y z floats, red green blue bytes, an empty face element. */
static std::string make_points_ply(int format, int n) {
  std::string s = std::string("ply\nformat ") + ply_format_name(format) +
                  " 1.0\nelement vertex " + std::to_string(n) +
                  "\nproperty float x\nproperty float y\nproperty float z\nproperty uchar red\nproperty uchar green\nproperty uchar blue\n"
                  "element face 0\nproperty list uchar int vertex_indices\nend_header\n";
  for (int i = 0; i < n; i++) {
    for (int k = 0; k < 3; k++) put_float(s, 3.0f * sv(i, k), format);
    for (int k = 0; k < 3; k++) {
      const unsigned c = (unsigned)((i * 53 + k * 91) & 0xFF);
      if (format == 0) s += std::to_string(c) + " ";
      else s += (char)c;
    }
    if (format == 0) s += "\n";
  }
  return s;
}

/* A zstd frame of raw (stored) blocks: valid without the library, so the v4 fuzz needs no zstd to build its files. */
static std::string zstd_raw_frame(const std::string &d) {
  std::string f;
  put32(f, 0xFD2FB528u);
  f += (char)0xA0;  // single segment, 4-byte content size
  put32(f, (uint32_t)d.size());
  size_t at = 0;
  do {
    const size_t take = std::min<size_t>(65536, d.size() - at);
    const bool last = at + take == d.size();
    const uint32_t bh = (last ? 1u : 0u) | ((uint32_t)take << 3);
    f += (char)(bh & 0xFF), f += (char)((bh >> 8) & 0xFF), f += (char)((bh >> 16) & 0xFF);
    f.append(d, at, take);
    at += take;
  } while (at < d.size());
  return f;
}

/* An SPZ file's bytes: v1-v3 as the one stream's contents (to gzip), v4 as the whole NGSP container. */
static std::string make_spz(uint32_t version, uint32_t n, int degree, int fractional = 12) {
  const int coeffs = degree == 0 ? 0 : degree == 1 ? 3 : degree == 2 ? 8 : degree == 3 ? 15 : 24;
  std::string pos, alpha, color, scale, rot, sh;
  auto byte = [](uint32_t i, int k) { return (char)((i * 131u + (uint32_t)k * 71u + 17u) & 0xFF); };
  for (uint32_t i = 0; i < n; i++) {
    for (int k = 0; k < (version == 1 ? 6 : 9); k++) pos += (version == 1 && (k % 2)) ? (char)0x3C : byte(i, k);
    alpha += byte(i, 20);
    for (int k = 0; k < 3; k++) color += byte(i, 30 + k), scale += (char)(100 + (uint8_t)byte(i, 40 + k) % 60);
    if (version >= 3) put32(rot, (3u << 30) | ((i * 37u % 300u) << 20) | ((i * 11u % 400u) << 10) | (i * 5u % 500u));
    else
      for (int k = 0; k < 3; k++) rot += (char)(110 + (uint8_t)byte(i, 50 + k) % 40);
    for (int k = 0; k < 3 * coeffs; k++) sh += byte(i, 60 + k);
  }
  std::string head;
  put32(head, 0x5053474e);
  put32(head, version);
  put32(head, n);
  head += (char)degree, head += (char)fractional, head += (char)0;
  if (version != 4) return (head += (char)0) + pos + alpha + color + scale + rot + sh;
  head += (char)6;
  put32(head, 32);
  head += std::string(12, '\0');
  const std::string *blocks[6] = {&pos, &alpha, &color, &scale, &rot, &sh};
  std::string toc, data;
  for (const std::string *b : blocks) {
    const std::string f = zstd_raw_frame(*b);
    put64(toc, f.size());
    put64(toc, b->size());
    data += f;
  }
  return head + toc + data;
}

struct GlbParts {
  std::string json, bin;
};
/* One POINTS primitive with KHR_gaussian_splatting's attributes (degree 1). */
static GlbParts make_glb_parts(int n) {
  GlbParts g;
  std::vector<std::pair<std::string, int>> attrs = {{"POSITION", 3}, {"KHR_gaussian_splatting:SCALE", 3}, {"KHR_gaussian_splatting:ROTATION", 4},
                                                     {"KHR_gaussian_splatting:OPACITY", 1}, {"KHR_gaussian_splatting:SH_DEGREE_0_COEF_0", 3}};
  for (int k = 0; k < 3; k++) attrs.push_back({"KHR_gaussian_splatting:SH_DEGREE_1_COEF_" + std::to_string(k), 3});
  std::string views, acc, attr_json;
  for (size_t a = 0; a < attrs.size(); a++) {
    const size_t at = g.bin.size();
    for (int i = 0; i < n; i++)
      for (int c = 0; c < attrs[a].second; c++) {
        float v = sv(i, (int)a * 7 + c);
        if (a == 1) v = std::exp(-3.0f + v);
        if (a == 3) v = 0.5f + 0.4f * v;
        g.bin.append((const char *)&v, 4);
      }
    const char *type = attrs[a].second == 1 ? "SCALAR" : attrs[a].second == 3 ? "VEC3" : "VEC4";
    views += strprintf("%s{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu}", a ? "," : "", at, g.bin.size() - at);
    acc += strprintf("%s{\"bufferView\":%zu,\"componentType\":5126,\"count\":%d,\"type\":\"%s\"}", a ? "," : "", a, n, type);
    attr_json += strprintf("%s\"%s\":%zu", a ? "," : "", attrs[a].first.c_str(), a);
  }
  g.json = strprintf("{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":%zu}],\"bufferViews\":[%s],\"accessors\":[%s],"
                     "\"meshes\":[{\"name\":\"Splats\",\"primitives\":[{\"mode\":0,\"attributes\":{%s},\"extensions\":{\"KHR_gaussian_splatting\":{}}}]}],"
                     "\"nodes\":[{\"name\":\"SplatNode\",\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}],\"scene\":0,"
                     "\"extensionsUsed\":[\"KHR_gaussian_splatting\"]}",
                     g.bin.size(), views.c_str(), acc.c_str(), attr_json.c_str());
  return g;
}
static std::string pack_glb(std::string json, std::string bin) {
  while (json.size() % 4) json += ' ';
  while (bin.size() % 4) bin += '\0';
  std::string glb = "glTF";
  put32(glb, 2);
  put32(glb, (uint32_t)(12 + 8 + json.size() + 8 + bin.size()));
  put32(glb, (uint32_t)json.size());
  put32(glb, 0x4E4F534Au);
  glb += json;
  put32(glb, (uint32_t)bin.size());
  put32(glb, 0x004E4942u);
  glb += bin;
  return glb;
}

/* ------------------------------------------------------------------ damage */

static const uint32_t kSpecial32[] = {0u, 1u, 2u, 3u, 4u, 0x7Fu, 0x80u, 0xFFu, 0xFFFFu, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu,
                                      0x10000000u, 0x10000001u, 0x7FC00000u, 0x7F800000u, 0xFF800000u, 0xFFFFFFFEu, 0x1000000u};

static void write32_at(std::string &b, size_t at, uint32_t v) {
  for (int k = 0; k < 4 && at + (size_t)k < b.size(); k++) b[at + (size_t)k] = (char)((v >> (8 * k)) & 0xFF);
}

static void mutate(std::string &b, Rng &r) {
  const int rounds = 1 + (int)r.below(3);
  for (int round = 0; round < rounds; round++) {
    const uint32_t kind = r.below(9);
    if (b.empty() && kind != 4) continue;
    const size_t head = std::min<size_t>(b.size(), 64);
    switch (kind) {
      case 0:
        for (uint32_t k = 0, n = 1 + r.below(4); k < n; k++) b[r.below((uint32_t)b.size())] ^= (char)(1u << r.below(8));
        break;
      case 1: b[r.below((uint32_t)head)] = (char)r.below(256); break;
      case 2: b.resize(r.below((uint32_t)b.size() + 1)); break;
      case 3: {  // splice: a chunk copied over another place
        const size_t len = std::min<size_t>(b.size(), 1 + r.below(64)), from = r.below((uint32_t)(b.size() - len + 1)), to = r.below((uint32_t)(b.size() - len + 1));
        const std::string chunk = b.substr(from, len);
        b.replace(to, len, chunk);
        break;
      }
      case 4: {  // insert random bytes
        std::string chunk(1 + r.below(32), '\0');
        for (char &c : chunk) c = (char)r.below(256);
        b.insert(r.below((uint32_t)b.size() + 1), chunk);
        break;
      }
      case 5: {  // erase a chunk
        const size_t len = std::min<size_t>(b.size(), 1 + r.below(64));
        b.erase(r.below((uint32_t)(b.size() - len + 1)), len);
        break;
      }
      case 6: write32_at(b, r.below((uint32_t)head), kSpecial32[r.below((uint32_t)(sizeof kSpecial32 / sizeof kSpecial32[0]))]); break;
      case 7: write32_at(b, r.below((uint32_t)b.size()), kSpecial32[r.below((uint32_t)(sizeof kSpecial32 / sizeof kSpecial32[0]))]); break;
      default: {  // a 64-bit number where v4's table of streams sits
        const size_t at = 16 + r.below(160);
        write32_at(b, at, kSpecial32[r.below((uint32_t)(sizeof kSpecial32 / sizeof kSpecial32[0]))]);
        write32_at(b, at + 4, r.below(2) ? 0u : 0xFFFFFFFFu);
        break;
      }
    }
  }
}

static void replace_token(std::string &b, size_t at, const std::string &with) {
  size_t e = at;
  while (e < b.size() && !std::isspace((unsigned char)b[e])) e++;
  b.replace(at, e - at, with);
}

/* A PLY's header numbers and words: the vertex count, a property's type, the format. */
static void mutate_ply_text(std::string &b, Rng &r) {
  static const char *numbers[] = {"0", "1", "-1", "4294967295", "268435456", "268435457", "1e300", "nan", "inf", "99999999999999999999", "3.5", "", "0x10", "2147483648"};
  static const char *types[] = {"float", "double", "uchar", "char", "short", "ushort", "int", "uint", "quux", "list"};
  static const char *formats[] = {"ascii", "binary_little_endian", "binary_big_endian", "bogus"};
  const size_t count = b.find("element vertex ");
  if (count != std::string::npos && r.below(2)) replace_token(b, count + 15, numbers[r.below((uint32_t)(sizeof numbers / sizeof numbers[0]))]);
  const size_t prop = b.find("property ", r.below((uint32_t)std::min<size_t>(b.size(), 600) + 1));
  if (prop != std::string::npos && prop + 9 < b.size() && r.below(2)) replace_token(b, prop + 9, types[r.below((uint32_t)(sizeof types / sizeof types[0]))]);
  const size_t fmt = b.find("format ");
  if (fmt != std::string::npos && fmt + 7 < b.size() && r.below(4) == 0) replace_token(b, fmt + 7, formats[r.below(4)]);
}

/* A glTF JSON's numbers: a count, a length, an offset, a view or accessor index, a mode, replaced. */
static void mutate_json_numbers(std::string &j, Rng &r) {
  static const char *keys[] = {"\"count\":", "\"byteLength\":", "\"byteOffset\":", "\"bufferView\":", "\"mode\":", "\"componentType\":", "\"buffer\":"};
  static const char *numbers[] = {"0", "1", "-1", "4294967295", "99999999999", "1e300", "12", "268435457", "5126", "5123", "2147483648", "0.5"};
  for (uint32_t k = 0, n = 1 + r.below(3); k < n; k++) {
    const char *key = keys[r.below((uint32_t)(sizeof keys / sizeof keys[0]))];
    size_t at = j.find(key);
    for (uint32_t skip = r.below(8); skip > 0 && at != std::string::npos; skip--) {
      const size_t nx = j.find(key, at + 1);
      if (nx == std::string::npos) break;
      at = nx;
    }
    if (at == std::string::npos) continue;
    at += std::strlen(key);
    size_t e = at;
    while (e < j.size() && (std::isdigit((unsigned char)j[e]) || j[e] == '.' || j[e] == '-' || j[e] == '+' || j[e] == 'e' || j[e] == 'E')) e++;
    j.replace(at, e - at, numbers[r.below((uint32_t)(sizeof numbers / sizeof numbers[0]))]);
  }
}

/* ------------------------------------------------------------------ verdicts */

static bool finite3(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

/* "" when the outcome is acceptable: a refusal with a reason, or a consistent cloud of finite values. */
static std::string verdict(const PointCloudPtr &pc, const std::string &error) {
  if (!pc) return error.empty() ? "refused without a reason" : "";
  if (!pc->consistent()) return "the cloud is not consistent()";
  if (pc->count() > kPointCloudMaxPoints) return "more points than the cap";
  for (const Vec3 &v : pc->position)
    if (!finite3(v)) return "a non-finite position";
  for (const Vec3 &v : pc->color)
    if (!finite3(v)) return "a non-finite colour";
  for (const Vec3 &v : pc->scale)
    if (!finite3(v)) return "a non-finite scale";
  for (const Vec3 &v : pc->sh_rest)
    if (!finite3(v)) return "a non-finite SH coefficient";
  for (float o : pc->opacity)
    if (!std::isfinite(o)) return "a non-finite opacity";
  for (const Quat &q : pc->rotation)
    if (!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w)) return "a non-finite rotation";
  return "";
}

struct Tally {
  int cases = 0, refused = 0, clouds = 0, empty = 0, bad = 0;
  double ms = 0.0;
  std::vector<std::string> first;
};

static void record(Tally &t, uint32_t seed, const PointCloudPtr &pc, const std::string &error, const std::string &thrown, double ms) {
  t.cases++;
  t.ms += ms;
  std::string why = thrown.empty() ? verdict(pc, error) : thrown;
  if (!pc) t.refused++;
  else {
    t.clouds++;
    if (pc->count() == 0) t.empty++;
  }
  if (why.empty()) return;
  t.bad++;
  if (t.first.size() < 5) t.first.push_back(strprintf("seed %u: %s", seed, why.c_str()));
}

template<class F> static void run_case(Tally &t, uint32_t seed, F &&fn) {
  PointCloudPtr pc;
  std::string error, thrown;
  ScopedTimer tm;
  try {
    fn(pc, error);
  }
  catch (const std::exception &e) {
    thrown = std::string("threw: ") + e.what();
  }
  catch (...) {
    thrown = "threw";
  }
  record(t, seed, pc, error, thrown, tm.ms());
}

static void row_for(Report &rep, const char *name, const Tally &t) {
  rep.row({name, std::to_string(t.cases), std::to_string(t.refused), std::to_string(t.clouds), std::to_string(t.empty), std::to_string(t.bad), f1(t.ms)});
}

/* A glTF attribute map of random shape: names present or missing, components right or not, sizes matching or not,
 * values with NaN / infinity / huge ones. */
static void fuzz_gltf_attributes(Rng &r, std::map<std::string, std::vector<float>> &attrs, std::map<std::string, int> &comps) {
  static const float specials[] = {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                                   1e30f, -1e30f, 0.0f, 1e-30f, FLT_MAX, -0.0f};
  const uint32_t n = r.below(40);
  std::vector<std::pair<std::string, int>> names = {{"POSITION", 3}, {"KHR_gaussian_splatting:SCALE", 3}, {"KHR_gaussian_splatting:ROTATION", 4},
                                                     {"KHR_gaussian_splatting:OPACITY", 1}, {"KHR_gaussian_splatting:SH_DEGREE_0_COEF_0", 3},
                                                     {"COLOR_0", r.below(2) ? 3 : 4}};
  for (int l = 1; l <= 4; l++)
    for (int k = 0; k <= 2 * l; k++) names.push_back({strprintf("KHR_gaussian_splatting:SH_DEGREE_%d_COEF_%d", l, k), 3});
  const uint32_t bands_present = r.below(5);  // (how many SH bands this case gives)
  for (const auto &nm : names) {
    const bool is_band = nm.first.find("SH_DEGREE_") != std::string::npos && nm.first.find("SH_DEGREE_0") == std::string::npos;
    if (is_band) {
      const int l = nm.first[nm.first.find("SH_DEGREE_") + 10] - '0';
      if ((uint32_t)l > bands_present) continue;
    }
    if (r.below(100) >= 90) continue;  // missing
    const int c = r.below(10) == 0 ? 1 + (int)r.below(5) : nm.second;
    size_t size = (size_t)n * (size_t)nm.second;
    if (r.below(100) >= 90) size = r.below((uint32_t)size + 4);
    std::vector<float> v(size);
    for (float &x : v) x = r.below(100) < 3 ? specials[r.below((uint32_t)(sizeof specials / sizeof specials[0]))] : 4.0f * r.unit() - 2.0f;
    attrs[nm.first] = std::move(v);
    comps[nm.first] = c;
  }
}

/* ------------------------------------------------------------------ the section */

struct Base {
  std::string bytes;
  bool gz = false;
};

static void measure_import(Report &rep) {
  rep.table({"reader", "splats", "file bytes", "read ms", "M splats / s"});
  const int n = 100000;
  struct Case {
    const char *name;
    std::string bytes;
    int kind;  // 0 PLY, 1 SPZ
  };
  std::vector<Case> cases;
  cases.push_back({"PLY binary, degree 3", make_ply(1, n, 15), 0});
  cases.push_back({"PLY ASCII, degree 3", make_ply(0, n, 15), 0});
  cases.push_back({"SPZ v3 gzip, degree 3", gzip(make_spz(3, (uint32_t)n, 3)), 1});
  cases.push_back({"PLY points, binary", make_points_ply(1, n), 0});
  for (const Case &c : cases) {
    size_t got = 0;
    const double ms = time_ms([&] {
      SplatReadReport r;
      PointCloudPtr pc = c.kind == 0 ? read_ply_point_cloud(c.bytes, r) : read_spz(c.bytes, r);
      got = pc ? pc->count() : 0;
    });
    rep.row({c.name, std::to_string(got), num((double)c.bytes.size()), f1(ms), f2((double)got / std::max(ms, 1e-6) / 1000.0)});
  }
  rep.note("Import speed is a measurement (ADR 0056): printed, never asserted. SPZ's time includes the gzip inflate.");
}

static void test_splats(Report &rep, const Options &o) {
  rep.title("splats", "The Gaussian splat / point cloud readers (PLY, SPZ v1-v4, glTF) on damaged files: every result is a clean refusal with a reason or a "
                      "consistent cloud of finite values.");
  const int seeds = o.quick ? 500 : 4000;
  const int glb_seeds = o.quick ? 100 : 500;

  /* The valid files the damage starts from. */
  std::vector<Base> ply_bases, spz_bases, v4_bases;
  for (int format = 0; format < 3; format++) {
    ply_bases.push_back({make_ply(format, 20, 15), false});
    ply_bases.push_back({make_ply(format, 12, 0), false});
    ply_bases.push_back({make_points_ply(format, 15), false});
  }
  for (uint32_t version = 1; version <= 3; version++)
    for (int degree : {0, 1, 3}) spz_bases.push_back({make_spz(version, 20, degree), true});
  for (int degree : {0, 2}) v4_bases.push_back({make_spz(4, 20, degree), false});

  /* Sanity: the undamaged files read (v4 only with Blender's libraries: without them its refusal is the right answer). */
  int base_ok = 0, base_total = 0;
  for (const Base &b : ply_bases) {
    SplatReadReport r;
    base_total++, base_ok += read_ply_point_cloud(b.bytes, r) != nullptr;
  }
  for (const Base &b : spz_bases) {
    SplatReadReport r;
    base_total++, base_ok += read_spz(gzip(b.bytes), r) != nullptr;
  }
  int v4_ok = 0;
  for (const Base &b : v4_bases) {
    SplatReadReport r;
    v4_ok += read_spz(b.bytes, r) != nullptr;
  }
  rep.note(strprintf("Undamaged starting files that read: %d of %d (PLY and SPZ v1-v3); SPZ v4: %d of %zu (%s).", base_ok, base_total, v4_ok, v4_bases.size(),
#ifdef BL_WITH_ZSTD
                     "with Zstandard: all expected"
#else
                     "no Zstandard in this build: a clean refusal is expected"
#endif
                     ));

  rep.table({"reader", "cases", "clean refusals", "clouds", "of them empty", "PROBLEMS", "ms"});

  Tally ply, spz, v4, gltf, glb;
  for (int s = 0; s < seeds; s++) {
    Rng r((uint64_t)s + 1000);
    std::string b = ply_bases[r.below((uint32_t)ply_bases.size())].bytes;
    const bool text = r.below(2) == 0;
    if (text) mutate_ply_text(b, r);
    if (!text || r.below(2)) mutate(b, r);
    run_case(ply, (uint32_t)s, [&](PointCloudPtr &pc, std::string &error) {
      SplatReadReport rr;
      (void)ply_is_point_cloud(b);
      pc = read_ply_point_cloud(b, rr);
      error = rr.error;
    });
  }
  row_for(rep, "PLY", ply);

  for (int s = 0; s < seeds; s++) {
    Rng r((uint64_t)s + 2000);
    std::string b = spz_bases[r.below((uint32_t)spz_bases.size())].bytes;
    if (r.below(10) < 7) {  // damage the stream's contents, then gzip it
      mutate(b, r);
      b = gzip(b);
    }
    else {  // damage the gzip file itself
      b = gzip(b);
      mutate(b, r);
    }
    run_case(spz, (uint32_t)s, [&](PointCloudPtr &pc, std::string &error) {
      SplatReadReport rr;
      pc = read_spz(b, rr);
      error = rr.error;
    });
  }
  row_for(rep, "SPZ v1-v3 (gzip)", spz);

  for (int s = 0; s < seeds; s++) {
    Rng r((uint64_t)s + 3000);
    std::string b = v4_bases[r.below((uint32_t)v4_bases.size())].bytes;
    mutate(b, r);
    run_case(v4, (uint32_t)s, [&](PointCloudPtr &pc, std::string &error) {
      SplatReadReport rr;
      pc = read_spz(b, rr);
      error = rr.error;
    });
  }
  row_for(rep, "SPZ v4 (NGSP)", v4);

  for (int s = 0; s < seeds; s++) {
    Rng r((uint64_t)s + 4000);
    std::map<std::string, std::vector<float>> attrs;
    std::map<std::string, int> comps;
    fuzz_gltf_attributes(r, attrs, comps);
    const bool splats = r.below(4) != 0;
    run_case(gltf, (uint32_t)s, [&](PointCloudPtr &pc, std::string &error) {
      SplatReadReport rr;
      pc = gltf_point_cloud(attrs, comps, splats, rr);
      error = rr.error;
    });
  }
  row_for(rep, "glTF attributes", gltf);

  /* Whole .glb files through import_model: the JSON's numbers and the buffer damaged, then the packed file. */
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "blendity_stress";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  const std::string path = (dir / "splats_fuzz.glb").string();
  for (int s = 0; s < glb_seeds; s++) {
    Rng r((uint64_t)s + 5000);
    GlbParts g = make_glb_parts(8);
    if (r.below(3)) mutate_json_numbers(g.json, r);
    if (r.below(2)) mutate(g.bin, r);
    std::string packed = pack_glb(g.json, g.bin);
    if (r.below(3) == 0) mutate(packed, r);
    fs::write_file(path, packed);
    ScopedTimer tm;
    std::string thrown, why;
    bool refused = false, any_cloud = false;
    try {
      ImportResult out;
      const bool ok = import_model(path, out);
      refused = !ok;
      if (!ok && out.error.empty()) why = "refused without a reason";
      for (const ImportedNode &nd : out.nodes) {
        if (!nd.points) continue;
        any_cloud = true;
        if (why.empty()) why = verdict(nd.points, "");
      }
    }
    catch (const std::exception &e) {
      why = std::string("threw: ") + e.what();
    }
    catch (...) {
      why = "threw";
    }
    glb.cases++, glb.ms += tm.ms();
    glb.refused += refused, glb.clouds += any_cloud;
    if (!why.empty()) {
      glb.bad++;
      if (glb.first.size() < 5) glb.first.push_back(strprintf("seed %d: %s", s, why.c_str()));
    }
  }
  std::filesystem::remove(path, ec);
  row_for(rep, "glTF .glb (import_model)", glb);

  int problems = 0;
  for (const Tally *t : {&ply, &spz, &v4, &gltf, &glb}) {
    problems += t->bad;
    for (const std::string &line : t->first) rep.note("PROBLEM: " + line + (t == &ply ? " (PLY)" : t == &spz ? " (SPZ gzip)" : t == &v4 ? " (SPZ v4)" : t == &gltf ? " (glTF attributes)" : " (glb)"));
  }
  rep.note(strprintf("Splat readers on damaged files: %d cases, %d problems (a problem is a crash, a refusal with no reason, or a cloud that is not consistent() "
                     "or holds a non-finite value).",
                     ply.cases + spz.cases + v4.cases + gltf.cases + glb.cases, problems));

  measure_import(rep);
}

}  // namespace splatstress

static void test_splats(Report &rep, const Options &o) { splatstress::test_splats(rep, o); }

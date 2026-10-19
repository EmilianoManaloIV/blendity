// SPDX-License-Identifier: GPL-2.0-or-later
// MikuMikuDance models (task 0136, ADR 0047): PMX 2.0 / 2.1 and PMD, read by our own code (no library), into the
// import's nodes, a skinned mesh with its vertex morphs as shape keys, toon materials with an outline, bones turned to
// point at their tails, IK on the legs with a pole at each knee, and the physics' dynamic bones as Spring Bones.
//
// MMD's space is left-handed and Y-up like Blendity's, in units of 8 cm, with the model facing -Z: a point comes in
// turned half a turn about Y and scaled, (x, y, z) -> (-x, y, -z) * 0.08 - a rotation, so faces keep their order.
// Formats: the PMX 2.0 / 2.1 specification (PMX仕様) that ships with PMXEditor, and the PMD layout as MMD and
// mmd_tools read it.
#include "import.h"

#include "../core/core.h"
#include "../core/text_encoding.h"
#include "../image/image.h"
#include "mmd_binary.h"
#include "spring_bones.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>

namespace bl {

namespace {

using Reader = MmdReader;

constexpr float kMmdScale = 0.08f;          // metres in MMD's unit
constexpr float kFarthest = 1e6f;           // MMD units: a position past this is a damaged file
constexpr size_t kMaxBones = 65535;         // (PMD's limit; real PMX models have a few thousand at most)
constexpr int kMaxDepth = 512;              // bones nested deeper: a damaged file (and recursion elsewhere stays shallow)
constexpr size_t kKeyBudget = 512u << 20;   // bytes of shape keys (they are dense: one offset a vertex each)
constexpr size_t kMaxKeys = 4096;           // and at most this many (real models have a few hundred)

Vec3 mmd_point(Vec3 p) { return Vec3(-p.x, p.y, -p.z) * kMmdScale; }
Vec3 mmd_offset(Vec3 d) { return Vec3(-d.x, d.y, -d.z) * kMmdScale; }
bool near_origin(Vec3 p) { return std::fabs(p.x) <= kFarthest && std::fabs(p.y) <= kFarthest && std::fabs(p.z) <= kFarthest; }

struct MmdVertex {
  Vec3 p, n;
  Vec2 uv;
  int bone[4] = {-1, -1, -1, -1};
  float w[4] = {0, 0, 0, 0};
};
struct MmdMaterial {
  std::string name;
  Vec4 diffuse{1, 1, 1, 1};
  Vec3 ambient;
  uint8_t flags = 0;
  Vec4 edge_color{0, 0, 0, 1};
  float edge_size = 0.0f;
  std::string texture, toon;  // file names relative to the model ("" none)
  int shared_toon = -1;       // MMD's own toon01..10.bmp (-1: none)
  bool sphere = false;        // a sphere map (not carried over)
  size_t corners = 0;         // its run of the index list
};
struct MmdIKLink {
  int bone = -1;
  bool limited = false;
  Vec3 lo, hi;
};
struct MmdBone {
  std::string name;
  Vec3 head;
  int parent = -1;
  int tail_bone = -1;
  Vec3 tail_offset;
  bool ik = false;
  int ik_target = -1, ik_loops = 0;
  std::vector<MmdIKLink> links;
  int inherit_from = -1;  // a bone whose turn (or move) this one copies, times the ratio
  float inherit_ratio = 0.0f;
  bool inherit_rotation = false, inherits = false;
};
struct MmdMorph {
  std::string name;
  int type = 1;  // 0 group, 1 vertex (PMX's numbering)
  std::vector<std::pair<int, Vec3>> offsets;   // vertex: index, offset
  std::vector<std::pair<int, float>> members;  // group: morph, weight
};
struct MmdRigid {
  int bone = -1;
  int shape = 0;  // sphere, box, capsule
  Vec3 size;
  int mode = 0;   // 0 follows its bone, 1 physics, 2 physics with the bone's position
};
struct MmdModel {
  std::string name;
  std::vector<MmdVertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<MmdMaterial> materials;
  std::vector<MmdBone> bones;
  std::vector<MmdMorph> morphs;
  std::vector<MmdRigid> rigids;
  int skipped_morphs[11] = {};  // by PMX morph type: bone, UV, material, flip, impulse (not shape keys)
  int sdef = 0, qdef = 0;       // vertices blended spherically or by dual quaternions in MMD (linear here)
  size_t joints = 0;            // MMD's physics joints (the springs stand in)
};

/* Positions within reach (a flipped exponent byte gives 1e30): a damaged file is refused rather than imported huge. */
bool positions_sane(const MmdModel &m, std::string &error) {
  for (const MmdVertex &v : m.vertices)
    if (!near_origin(v.p)) return error = "a vertex a million units away: a damaged file", false;
  for (const MmdBone &b : m.bones)
    if (!near_origin(b.head) || !near_origin(b.tail_offset)) return error = "a bone a million units away: a damaged file", false;
  if (m.bones.size() > kMaxBones) return error = strprintf("%zu bones (at most %zu)", m.bones.size(), kMaxBones), false;
  return true;
}

/* ---- PMX ---- */

void read_pmx_vertex(Reader &r, MmdVertex &v, MmdModel &m, int extra_uvs, int bsize, std::string &error) {
  v.p = r.v3(), v.n = r.v3();
  const float u = r.f(), t = r.f();
  v.uv = {u, t};
  r.skip(16 * (size_t)extra_uvs);
  const uint8_t type = r.get<uint8_t>();
  if (type == 0) v.bone[0] = r.index(bsize), v.w[0] = 1.0f;
  else if (type == 1 || type == 3) {  // BDEF2, SDEF (read as BDEF2: its spherical blend is a refinement)
    v.bone[0] = r.index(bsize), v.bone[1] = r.index(bsize);
    v.w[0] = std::max(0.0f, std::min(1.0f, r.f())), v.w[1] = 1.0f - v.w[0];
    if (type == 3) r.skip(36), m.sdef++;
  }
  else if (type == 2 || type == 4) {  // BDEF4, QDEF (dual quaternion: read as linear)
    for (int k = 0; k < 4; k++) v.bone[k] = r.index(bsize);
    for (int k = 0; k < 4; k++) v.w[k] = std::max(0.0f, std::min(1.0f, r.f()));
    m.qdef += type == 4;
  }
  else if (error.empty()) error = "PMX vertex weight type " + std::to_string(type), r.bad = true;
  r.f();  // (edge scale)
}

void read_pmx_bone(Reader &r, MmdBone &b, int bsize) {
  b.head = r.v3();
  b.parent = r.index(bsize);
  r.get<int32_t>();  // (its deform layer)
  const uint16_t flags = r.get<uint16_t>();
  if (flags & 0x0001) b.tail_bone = r.index(bsize);
  else b.tail_offset = r.v3();
  if (flags & 0x0300) {  // (its turn (0x100) or move (0x200) copied from another bone, times a ratio)
    b.inherits = true, b.inherit_rotation = flags & 0x0100;
    b.inherit_from = r.index(bsize), b.inherit_ratio = r.f();
  }
  if (flags & 0x0400) r.v3();          // (a fixed axis)
  if (flags & 0x0800) r.v3(), r.v3();  // (local axes)
  if (flags & 0x2000) r.get<int32_t>();  // (an external parent's key)
  if (!(flags & 0x0020)) return;
  b.ik = true;
  b.ik_target = r.index(bsize);
  b.ik_loops = r.get<int32_t>();
  r.f();  // (the angle limit per loop)
  b.links.resize(r.count(r.get<int32_t>(), (size_t)bsize + 1));
  for (MmdIKLink &l : b.links) {
    l.bone = r.index(bsize);
    l.limited = r.get<uint8_t>() != 0;
    if (l.limited) l.lo = r.v3(), l.hi = r.v3();
  }
}

/* A morph's records: groups and vertex offsets kept, the rest (bone, UV, material, flip, impulse) skipped. */
bool read_pmx_morph(Reader &r, MmdMorph &mo, MmdModel &m, const uint8_t g[8], std::string &error) {
  r.get<uint8_t>();  // (its panel)
  mo.type = r.get<uint8_t>();
  const int32_t n = r.get<int32_t>();
  static const size_t record[11] = {4, 12, 28, 16, 16, 16, 16, 16, 113, 4, 25};
  if (mo.type > 10) return error = "PMX morph type " + std::to_string(mo.type), false;
  const int vsize = g[2], msize = g[4], bsize = g[5], morph_size = g[6], rsize = g[7];
  const size_t idx = mo.type == 0 || mo.type == 9 ? (size_t)morph_size : mo.type == 2 ? (size_t)bsize : mo.type == 8 ? (size_t)msize
                                                                                                        : mo.type == 10 ? (size_t)rsize : (size_t)vsize;
  const size_t count = r.count(n, idx + record[mo.type]);
  if (mo.type == 0)
    for (size_t k = 0; k < count; k++) {
      const int member = r.index(morph_size);
      mo.members.push_back({member, r.f()});
    }
  else if (mo.type == 1)
    for (size_t k = 0; k < count; k++) {
      const int v = r.vertex(vsize);
      mo.offsets.push_back({v, r.v3()});
    }
  else {
    m.skipped_morphs[mo.type]++;
    r.skip(count * (idx + record[mo.type]));
  }
  return true;
}

bool read_pmx(const std::string &data, MmdModel &m, std::string &error) {
  Reader r(data);
  if (r.bytes(4) != std::string("PMX ", 4)) return error = "not a PMX file", false;
  const float version = r.f();
  const size_t nglobals = r.get<uint8_t>();
  uint8_t g[8] = {0, 0, 4, 4, 4, 4, 4, 4};
  for (size_t i = 0; i < nglobals; i++) {
    const uint8_t v = r.get<uint8_t>();
    if (i < 8) g[i] = v;
  }
  if (version < 2.0f || version > 2.1f + 1e-4f) return error = strprintf("PMX version %.1f", version), false;
  if (g[0] > 1 || g[1] > 4) return error = strprintf("PMX header: encoding %d, %d extra UVs", g[0], g[1]), false;
  for (int i = 2; i < 8; i++)
    if (g[i] != 1 && g[i] != 2 && g[i] != 4) return error = "PMX header: an index size of " + std::to_string(g[i]), false;
  const bool utf8 = g[0] == 1;
  const int vsize = g[2], tsize = g[3], bsize = g[5], morph_size = g[6];
  auto text = [&]() {
    const int32_t n = r.get<int32_t>();
    const std::string s = r.bytes(r.count(n, 1));
    return utf8 ? s : utf16le_to_utf8(s);
  };
  m.name = text();
  text(), text(), text();  // (the English name, the comments)
  m.vertices.resize(r.count(r.get<int32_t>(), 33));
  for (MmdVertex &v : m.vertices) read_pmx_vertex(r, v, m, g[1], bsize, error);
  if (!error.empty()) return false;
  m.indices.resize(r.count(r.get<int32_t>(), (size_t)vsize));
  for (uint32_t &i : m.indices) i = (uint32_t)r.vertex(vsize);
  std::vector<std::string> textures(r.count(r.get<int32_t>(), 4));
  for (std::string &t : textures) t = text();
  auto texture = [&](int i) { return i >= 0 && (size_t)i < textures.size() ? textures[(size_t)i] : std::string(); };
  m.materials.resize(r.count(r.get<int32_t>(), 60));
  for (MmdMaterial &mt : m.materials) {
    mt.name = text();
    text();
    mt.diffuse = r.v4();
    r.v3(), r.f();  // (specular colour and power: MMD's highlight, left to the toon shading)
    mt.ambient = r.v3();
    mt.flags = r.get<uint8_t>();
    mt.edge_color = r.v4(), mt.edge_size = r.f();
    mt.texture = texture(r.index(tsize));
    mt.sphere = r.index(tsize) >= 0;
    mt.sphere = r.get<uint8_t>() != 0 && mt.sphere;  // (its mode: 0 off)
    if (r.get<uint8_t>() == 0) mt.toon = texture(r.index(tsize));
    else mt.shared_toon = r.get<uint8_t>();
    text();
    mt.corners = r.count(r.get<int32_t>(), 0);
  }
  m.bones.resize(r.count(r.get<int32_t>(), 20));
  for (MmdBone &b : m.bones) {
    b.name = text();
    text();
    read_pmx_bone(r, b, bsize);
  }
  m.morphs.resize(r.count(r.get<int32_t>(), 14));
  for (MmdMorph &mo : m.morphs) {
    mo.name = text();
    text();
    if (!read_pmx_morph(r, mo, m, g, error)) return false;
  }
  /* Display frames (skipped), then the rigid bodies and the joints (counted). */
  const size_t frames = r.count(r.get<int32_t>(), 9);
  for (size_t i = 0; i < frames && !r.bad; i++) {
    text(), text();
    r.get<uint8_t>();
    const size_t n = r.count(r.get<int32_t>(), 2);
    for (size_t k = 0; k < n; k++) (void)(r.get<uint8_t>() == 0 ? r.index(bsize) : r.index(morph_size));
  }
  m.rigids.resize(r.count(r.get<int32_t>(), 70));
  for (MmdRigid &rb : m.rigids) {
    text(), text();
    rb.bone = r.index(bsize);
    r.get<uint8_t>(), r.get<uint16_t>();  // (its collision group and mask)
    rb.shape = r.get<uint8_t>();
    rb.size = r.v3();
    r.v3(), r.v3();                    // (position, rotation)
    r.f(), r.f(), r.f(), r.f(), r.f();  // (mass, damping, restitution, friction)
    rb.mode = r.get<uint8_t>();
  }
  if (r.at < data.size()) m.joints = r.count(r.get<int32_t>(), 0);
  if (r.bad && m.rigids.empty() && m.morphs.empty() && m.bones.empty()) return error = "the PMX file ends early", false;
  return positions_sane(m, error);  // (a file cut short after its bones still gives the model)
}

/* ---- PMD ---- */

void read_pmd_materials(Reader &r, MmdModel &m, std::vector<int> &toon_of) {
  m.materials.resize(r.count(r.get<uint32_t>(), 70));
  toon_of.assign(m.materials.size(), -1);
  for (size_t i = 0; i < m.materials.size(); i++) {
    MmdMaterial &mt = m.materials[i];
    mt.name = strprintf("Material %zu", i + 1);
    const Vec3 rgb = r.v3();
    mt.diffuse = Vec4(rgb, r.f());
    r.f(), r.v3();  // (specular power and colour)
    mt.ambient = r.v3();
    const uint8_t toon = r.get<uint8_t>();
    toon_of[i] = toon == 0xFF ? -1 : toon;
    mt.flags = r.get<uint8_t>() ? 0x10 : 0;  // (its edge)
    if (mt.diffuse.w < 0.999f) mt.flags |= 0x01;  // (MMD draws a see-through PMD material's back faces)
    mt.edge_size = 1.0f;
    mt.corners = r.get<uint32_t>();
    const std::string file = sjis_to_utf8(r.bytes(20));
    const size_t star = file.find('*');
    mt.texture = file.substr(0, star);  // ("face.bmp*face.sph": the sphere map after the star)
    mt.sphere = star != std::string::npos;
    const std::string ext = to_lower(fs::extension(mt.texture));
    if (ext == ".sph" || ext == ".spa") mt.texture.clear(), mt.sphere = true;
  }
}

void read_pmd_bones(Reader &r, MmdModel &m) {
  m.bones.resize(r.count(r.get<uint16_t>(), 39));
  for (MmdBone &b : m.bones) {
    b.name = sjis_to_utf8(r.bytes(20));
    const uint16_t parent = r.get<uint16_t>(), tail = r.get<uint16_t>();
    b.parent = parent == 0xFFFF ? -1 : parent;
    b.tail_bone = tail == 0xFFFF || tail == 0 ? -1 : tail;
    const uint8_t type = r.get<uint8_t>();
    b.inherits = type == 5 || type == 9;  // (turned by another bone: twist and eye helpers)
    r.get<uint16_t>();                    // (the IK bone it follows, or the bone it turns with)
    b.head = r.v3();
  }
  const size_t iks = r.count(r.get<uint16_t>(), 11);
  for (size_t i = 0; i < iks; i++) {
    const uint16_t ik = r.get<uint16_t>(), target = r.get<uint16_t>();
    const size_t links = r.get<uint8_t>();
    const int loops = r.get<uint16_t>();
    r.f();
    MmdBone dummy, &b = ik < m.bones.size() ? m.bones[ik] : dummy;
    b.ik = true, b.ik_target = target, b.ik_loops = loops;
    for (size_t k = 0; k < links; k++) b.links.push_back({(int)r.get<uint16_t>(), false, {}, {}});
  }
}

/* Morphs: the base's own vertex list, then each morph's offsets by place in that list. The count read. */
size_t read_pmd_morphs(Reader &r, MmdModel &m) {
  const size_t morphs = r.count(r.get<uint16_t>(), 25);
  std::vector<int> base;
  for (size_t i = 0; i < morphs && !r.bad; i++) {
    MmdMorph mo;
    mo.name = sjis_to_utf8(r.bytes(20));
    const size_t n = r.count(r.get<uint32_t>(), 16);
    const uint8_t type = r.get<uint8_t>();
    for (size_t k = 0; k < n; k++) {
      const uint32_t v = r.get<uint32_t>();
      const Vec3 d = r.v3();
      if (type == 0) base.push_back((int)v);
      else if (v < base.size()) mo.offsets.push_back({base[v], d});
    }
    if (type != 0) m.morphs.push_back(std::move(mo));
  }
  return morphs;
}

bool read_pmd(const std::string &data, MmdModel &m, std::string &error) {
  Reader r(data);
  if (r.bytes(3) != "Pmd") return error = "not a PMD file", false;
  r.f();
  m.name = sjis_to_utf8(r.bytes(20));
  r.skip(256);
  m.vertices.resize(r.count(r.get<uint32_t>(), 38));
  for (MmdVertex &v : m.vertices) {
    v.p = r.v3(), v.n = r.v3();
    const float u = r.f(), t = r.f();
    v.uv = {u, t};
    v.bone[0] = r.get<uint16_t>(), v.bone[1] = r.get<uint16_t>();
    v.w[0] = std::min(100, (int)r.get<uint8_t>()) / 100.0f, v.w[1] = 1.0f - v.w[0];
    r.get<uint8_t>();  // (no edge)
  }
  m.indices.resize(r.count(r.get<uint32_t>(), 2));
  for (uint32_t &i : m.indices) i = r.get<uint16_t>();
  std::vector<int> toon_of;
  read_pmd_materials(r, m, toon_of);
  read_pmd_bones(r, m);
  const size_t morphs = read_pmd_morphs(r, m);
  /* The display lists, the English names, then the toon textures and the rigid bodies (each part optional). */
  r.skip(2 * (size_t)r.get<uint8_t>());
  const size_t frame_names = r.get<uint8_t>();
  r.skip(50 * frame_names);
  r.skip(3 * r.count(r.get<uint32_t>(), 3));
  if (r.at < data.size() && r.get<uint8_t>() == 1)
    r.skip(20 + 256 + 20 * m.bones.size() + 20 * (morphs ? morphs - 1 : 0) + 50 * frame_names);
  std::string toons[10];
  for (std::string &t : toons) t = sjis_to_utf8(r.bytes(100));
  for (size_t i = 0; i < m.materials.size(); i++) {
    if (toon_of[i] < 0 || toon_of[i] >= 10) continue;
    if (!r.bad) m.materials[i].toon = toons[toon_of[i]];
    else m.materials[i].shared_toon = toon_of[i];  // (no toon list: MMD's own ramps by number)
  }
  m.rigids.resize(r.count(r.get<uint32_t>(), 83));
  for (MmdRigid &rb : m.rigids) {
    r.skip(20);
    const uint16_t bone = r.get<uint16_t>();
    rb.bone = bone == 0xFFFF ? -1 : bone;
    r.get<uint8_t>(), r.get<uint16_t>();
    rb.shape = r.get<uint8_t>();
    rb.size = r.v3();
    r.skip(24 + 20);
    rb.mode = r.get<uint8_t>();
  }
  if (r.at < data.size()) m.joints = r.count(r.get<uint32_t>(), 0);
  if (r.bad && m.vertices.empty()) return error = "the PMD file ends early", false;
  return positions_sane(m, error);
}

/* ---- Names ---- */

/* Full-width letters and digits (ＩＫ, ０) as ASCII: MMD's names use both. */
std::string halfwidth(const std::string &s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    const uint8_t a = (uint8_t)s[i];
    if (a == 0xEF && i + 2 < s.size()) {
      const uint8_t b = (uint8_t)s[i + 1], c = (uint8_t)s[i + 2];
      const int cp = ((a & 0x0F) << 12) | ((b & 0x3F) << 6) | (c & 0x3F);
      if (cp >= 0xFF01 && cp <= 0xFF5E) {
        o += (char)(cp - 0xFF01 + 0x21);
        i += 2;
        continue;
      }
    }
    o += s[i];
  }
  return o;
}

bool ascii(const std::string &s) {
  return std::all_of(s.begin(), s.end(), [](char c) { return (uint8_t)c < 0x80; });
}

}  // namespace

std::string mmd_bone_name(const std::string &japanese) {
  static const std::map<std::string, std::string> body = {
      {"全ての親", "Root"}, {"センター", "Center"}, {"グルーブ", "Groove"}, {"腰", "Waist"}, {"下半身", "Hips"}, {"上半身", "Spine"},
      {"上半身2", "Chest"}, {"上半身3", "UpperChest"}, {"首", "Neck"}, {"頭", "Head"}, {"両目", "Eyes"}};
  static const std::map<std::string, std::string> sided = {
      {"目", "Eye"}, {"肩", "Shoulder"}, {"肩P", "ShoulderP"}, {"肩C", "ShoulderC"}, {"腕", "Arm"}, {"腕捩", "TwistUpper"},
      {"ひじ", "ForeArm"}, {"手捩", "TwistLower"}, {"手首", "Hand"}, {"足", "UpLeg"}, {"ひざ", "Leg"}, {"足首", "Foot"},
      {"つま先", "ToeBase"}, {"足IK", "FootIK"}, {"つま先IK", "ToeIK"}, {"足IK親", "FootIKParent"},
      {"親指0", "HandThumb1"}, {"親指1", "HandThumb2"}, {"親指2", "HandThumb3"}, {"人指1", "HandIndex1"}, {"人指2", "HandIndex2"},
      {"人指3", "HandIndex3"}, {"中指1", "HandMiddle1"}, {"中指2", "HandMiddle2"}, {"中指3", "HandMiddle3"}, {"薬指1", "HandRing1"},
      {"薬指2", "HandRing2"}, {"薬指3", "HandRing3"}, {"小指1", "HandPinky1"}, {"小指2", "HandPinky2"}, {"小指3", "HandPinky3"}};
  const std::string n = halfwidth(japanese);
  if (const auto it = body.find(n); it != body.end()) return it->second;
  for (const auto &[side, word] : {std::pair<const char *, const char *>{"左", "Left"}, {"右", "Right"}}) {
    const size_t l = std::strlen(side);
    if (n.compare(0, l, side) != 0) continue;
    if (const auto it = sided.find(n.substr(l)); it != sided.end()) return word + it->second;
  }
  return japanese;  // (the model's own bones - hair, skirt, accessories - keep their names)
}

int mmd_spring_label(const std::string &name) {
  /* English names by whole words (spring::secondary_label: "ponytail" is hair, not a tail); Japanese by the word in
   * the name (Japanese has no spaces to split at). */
  if (ascii(name)) {
    const int label = spring::secondary_label(name);
    return label >= 0 ? label : 0;
  }
  auto has = [&](std::initializer_list<const char *> ws) {
    return std::any_of(ws.begin(), ws.end(), [&](const char *w) { return name.find(w) != std::string::npos; });
  };
  if (has({"耳"})) return 1;
  if (has({"尻尾", "しっぽ", "尾"})) return 2;
  if (has({"スカート", "裾", "袖", "リボン", "ネクタイ", "服", "コート", "マント", "スカーフ", "フリル"})) return 3;
  return 0;  // (hair: what most MMD physics is)
}

namespace {

/* ---- Into the import ---- */

/* What one import reads once: images' headers and toon ramps, by path. */
struct ImageMemo {
  std::map<std::string, bool> alpha;
  std::map<std::string, std::pair<bool, Vec3>> shadow;
};

/* Whether an image file has an alpha channel, from its first bytes (PNG's colour type, a 32-bit TGA or BMP). */
bool image_has_alpha(const std::string &path, ImageMemo &memo) {
  const auto it = memo.alpha.find(path);
  if (it != memo.alpha.end()) return it->second;
  std::string h;
  bool alpha = false;
  if (fs::read_prefix(path, 32, h) && h.size() >= 29) {
    if (h.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0) alpha = h[25] == 4 || h[25] == 6;
    else if (to_lower(fs::extension(path)) == ".tga") alpha = (uint8_t)h[16] == 32;
    else if (h.compare(0, 2, "BM") == 0) alpha = (uint8_t)h[28] == 32;
  }
  return memo.alpha[path] = alpha;
}

/* A toon ramp's shadow: MMD's ramps run from the lit colour at the top to the shadow's at the bottom. */
bool toon_shadow_of(const std::string &path, Vec3 &shadow, ImageMemo &memo) {
  const auto it = memo.shadow.find(path);
  if (it != memo.shadow.end()) return shadow = it->second.second, it->second.first;
  Bitmap bm;
  std::string e;
  bool ok = fs::exists(path) && load_image(path, bm, e) && bm.width > 0 && bm.height > 0;
  if (ok) {
    const size_t i = ((size_t)(bm.height - 1) * bm.width + bm.width / 2) * 4;
    shadow = bm.is_float ? Vec3(bm.rgbaf[i], bm.rgbaf[i + 1], bm.rgbaf[i + 2])
                         : Vec3(bm.rgba8[i] / 255.0f, bm.rgba8[i + 1] / 255.0f, bm.rgba8[i + 2] / 255.0f);
  }
  memo.shadow[path] = {ok, shadow};
  return ok;
}

std::string slashes(std::string p) {
  for (char &c : p)
    if (c == '\\') c = '/';
  return p;
}

bool absolute_path(const std::string &p) { return !p.empty() && (p[0] == '/' || (p.size() > 1 && p[1] == ':')); }

/* The toon ramp a material names: its own file beside the model, or MMD's toonNN.bmp if the model brought a copy. */
std::string toon_file(const MmdMaterial &mt, const std::string &dir) {
  std::string t = slashes(mt.toon);
  if (t.empty() && mt.shared_toon >= 0 && mt.shared_toon < 10) t = strprintf("toon%02d.bmp", mt.shared_toon + 1);
  if (t.empty()) return {};
  if (absolute_path(t)) return t;
  for (const std::string &d : {dir, fs::join(dir, "toon"), fs::join(dir, "Toon")})
    if (fs::exists(fs::join(d, t))) return fs::join(d, t);
  return {};
}

MaterialPtr make_mmd_material(const MmdMaterial &mt, const std::string &dir, ImportResult &out, ImageMemo &memo, int &missing_toons) {
  auto clamp01 = [](float v) { return std::max(0.0f, std::min(1.0f, v)); };
  MaterialPtr m = make_material(mt.name.empty() ? "Material" : mt.name, Vec3(1.0f));
  /* MMD lights a material as its ambient plus its diffuse times the light: lit, that sum. */
  m->base_color = Vec3(clamp01(mt.diffuse.x + mt.ambient.x), clamp01(mt.diffuse.y + mt.ambient.y), clamp01(mt.diffuse.z + mt.ambient.z));
  m->alpha = clamp01(mt.diffuse.w);
  m->shading_model = 1;  // Toon
  m->roughness = 0.6f;
  m->double_sided = mt.flags & 0x01;
  if (m->alpha < 0.999f) m->surface = (int)MaterialSurface::Transparent;
  if (!mt.texture.empty()) {
    const std::string t = slashes(mt.texture);
    m->base_map = TextureRef{find_texture(out, absolute_path(t) ? t : fs::join(dir, t)), false};
    if (std::find(out.textures.begin(), out.textures.end(), m->base_map.path) == out.textures.end()) out.textures.push_back(m->base_map.path);
    if (m->surface == (int)MaterialSurface::Opaque && image_has_alpha(m->base_map.path, memo)) m->surface = (int)MaterialSurface::Cutout;
  }
  Vec3 shadow;
  const std::string toon = toon_file(mt, dir);
  if (!toon.empty() && toon_shadow_of(toon, shadow, memo)) m->toon_shadow = shadow;
  else if (!mt.toon.empty() || mt.shared_toon >= 0) missing_toons++;
  return m;
}

/* A bone's world rotation at rest: +Y along it to its tail (Blender's convention), X kept near the world's X (along
 * an arm, near the world's forward), so a knee bends about X as in MMD. */
Quat along(Vec3 dir) {
  const Vec3 Y = normalize(dir);
  const Vec3 ref = std::fabs(Y.x) < 0.9f ? Vec3(1, 0, 0) : Vec3(0, 0, Y.x > 0.0f ? 1.0f : -1.0f);
  const Vec3 X = normalize(ref - Y * dot(ref, Y));
  return quat_from_axes(X, Y, cross(X, Y));
}

/* The bones' parents with any loop broken (a damaged file), and each one's depth: one pass, no recursion. */
bool bone_order(const MmdModel &mm, std::vector<int> &parent, std::vector<int> &depth, std::string &error) {
  const size_t nb = mm.bones.size();
  parent.assign(nb, -1), depth.assign(nb, -1);
  for (size_t i = 0; i < nb; i++) {
    const int p = mm.bones[i].parent;
    parent[i] = p >= 0 && (size_t)p < nb && (size_t)p != i ? p : -1;
  }
  std::vector<uint8_t> state(nb, 0);  // 0 not seen, 1 on the path being walked, 2 done
  std::vector<int> path;
  for (size_t i = 0; i < nb; i++) {
    path.clear();
    int c = (int)i;
    while (c >= 0 && state[(size_t)c] == 0) {
      state[(size_t)c] = 1;
      path.push_back(c);
      int p = parent[(size_t)c];
      if (p >= 0 && state[(size_t)p] == 1) parent[(size_t)c] = p = -1;  // (a loop: broken here)
      c = p;
    }
    int d = c >= 0 ? depth[(size_t)c] : -1;
    for (auto it = path.rbegin(); it != path.rend(); ++it) depth[(size_t)*it] = ++d, state[(size_t)*it] = 2;
    if (d > kMaxDepth) return error = strprintf("bones nested %d deep (at most %d): a damaged file", d, kMaxDepth), false;
  }
  return true;
}

/* Bones that only copy another's whole turn from the same place (足D, ひざD, 足首D: MMD's "D bones", which the skin
 * is weighted to): their weights go to the bone they copy, so posing, IK and walking the leg move the skin. The bone a
 * vertex's weight lands on, for each bone. */
std::vector<int> weight_owner(const MmdModel &mm, int &moved) {
  const size_t nb = mm.bones.size();
  std::vector<int> owner(nb);
  for (size_t i = 0; i < nb; i++) owner[i] = (int)i;
  auto copies = [&](size_t i) {
    const MmdBone &b = mm.bones[i];
    const int s = b.inherit_from;
    return b.inherit_rotation && s >= 0 && (size_t)s < nb && (size_t)s != i && std::fabs(b.inherit_ratio - 1.0f) < 1e-3f &&
           length(b.head - mm.bones[(size_t)s].head) < 1e-3f;
  };
  for (size_t i = 0; i < nb; i++) {
    size_t c = i;
    for (int steps = 0; steps < 8 && copies(c); steps++) c = (size_t)mm.bones[c].inherit_from;
    owner[i] = (int)c;
    moved += c != i;
  }
  return owner;
}

/* Unique names within the import (PMX allows two bones of one name, or none): the skin finds bones by name. */
void unique_names(std::vector<std::string> &names) {
  std::set<std::string> seen;
  for (size_t i = 0; i < names.size(); i++) {
    if (names[i].empty()) names[i] = strprintf("Bone %zu", i);
    std::string n = names[i];
    for (int k = 1; seen.count(n); k++) n = strprintf("%s.%03d", names[i].c_str(), k);
    names[i] = n;
    seen.insert(n);
  }
}

struct Built {
  size_t first = 0;  // the bones' first node
  std::vector<int> parent;
  std::vector<Vec3> head;
  std::vector<Quat> world;
  std::vector<float> len;
};

/* Bones: each a node, its rest turned to point at its tail; placed parents first (by depth), no recursion. */
bool build_bones(const MmdModel &mm, ImportResult &out, Built &b, std::string &error) {
  const size_t nb = mm.bones.size();
  std::vector<int> depth;
  if (!bone_order(mm, b.parent, depth, error)) return false;
  b.head.resize(nb), b.world.resize(nb), b.len.assign(nb, 0.0f);
  std::vector<Vec3> tail(nb);
  for (size_t i = 0; i < nb; i++) b.head[i] = mmd_point(mm.bones[i].head);
  for (size_t i = 0; i < nb; i++) {
    const MmdBone &bn = mm.bones[i];
    tail[i] = bn.tail_bone >= 0 && (size_t)bn.tail_bone < nb ? b.head[(size_t)bn.tail_bone] : b.head[i] + mmd_offset(bn.tail_offset);
  }
  for (const MmdBone &bn : mm.bones)  // (an IK chain's first link points at what it reaches: its tip is the effector)
    if (bn.ik && !bn.links.empty() && bn.links[0].bone >= 0 && (size_t)bn.links[0].bone < nb && bn.ik_target >= 0 && (size_t)bn.ik_target < nb)
      tail[(size_t)bn.links[0].bone] = b.head[(size_t)bn.ik_target];
  std::vector<size_t> order(nb);
  for (size_t i = 0; i < nb; i++) order[i] = i;
  std::stable_sort(order.begin(), order.end(), [&](size_t x, size_t y) { return depth[x] < depth[y]; });
  for (size_t i : order) {
    const Vec3 d = tail[i] - b.head[i];
    const float l = length(d);
    b.len[i] = std::isfinite(l) ? l : 0.0f;
    b.world[i] = b.len[i] > 1e-5f ? along(d) : b.parent[i] >= 0 ? b.world[(size_t)b.parent[i]] : Quat();
  }
  std::vector<std::string> names(nb);
  for (size_t i = 0; i < nb; i++) names[i] = mmd_bone_name(mm.bones[i].name);
  unique_names(names);
  b.first = out.nodes.size();
  for (size_t i = 0; i < nb; i++) {
    ImportedNode n;
    n.name = names[i];
    n.is_bone = true;
    n.parent = b.parent[i] >= 0 ? (int)(b.first + (size_t)b.parent[i]) : -1;
    const Quat pr = b.parent[i] >= 0 ? b.world[(size_t)b.parent[i]] : Quat();
    const Vec3 ph = b.parent[i] >= 0 ? b.head[(size_t)b.parent[i]] : Vec3(0.0f);
    n.position = pr.conjugate().rotate(b.head[i] - ph);
    n.rotation = normalize(pr.conjugate() * b.world[i]);
    n.length = b.len[i] > 1e-4f ? b.len[i] : 0.05f;
    out.nodes.push_back(std::move(n));
  }
  return true;
}

/* The skin's weights as groups named after the bones - only the bones that carry vertices. */
void build_groups(const MmdModel &mm, const ImportResult &out, const Built &b, const std::vector<int> &owner, Mesh &m) {
  const size_t nb = mm.bones.size(), nv = mm.vertices.size();
  std::vector<int> group(nb, -1);
  for (size_t i = 0; i < nv; i++)
    for (int k = 0; k < 4; k++) {
      const int bone = mm.vertices[i].bone[k];
      if (bone < 0 || (size_t)bone >= nb || !(mm.vertices[i].w[k] > 0.0f)) continue;
      const size_t o = (size_t)owner[(size_t)bone];
      if (group[o] < 0) group[o] = (int)m.vertex_groups.size(), m.vertex_groups.push_back({out.nodes[b.first + o].name, std::vector<float>(nv, 0.0f)});
      m.vertex_groups[(size_t)group[o]].weights[i] += mm.vertices[i].w[k];
    }
  for (VertexGroup &g : m.vertex_groups)
    for (float &w : g.weights) w = std::min(1.0f, w);
}

/* Faces: the file's order, unless its own normals say it is the other way round (a file written mirrored). */
void build_faces(const MmdModel &mm, Mesh &m) {
  const size_t nv = mm.vertices.size();
  int agree = 0;
  for (size_t f = 0; f + 2 < mm.indices.size(); f += 3) {
    const uint32_t a = mm.indices[f], b = mm.indices[f + 1], c = mm.indices[f + 2];
    if (a >= nv || b >= nv || c >= nv) continue;
    const Vec3 n = cross(m.positions[b] - m.positions[a], m.positions[c] - m.positions[a]);
    const Vec3 fn = mmd_offset(mm.vertices[a].n + mm.vertices[b].n + mm.vertices[c].n);
    agree += dot(n, fn) > 0.0f ? 1 : dot(n, fn) < 0.0f ? -1 : 0;
  }
  const bool flip = agree < 0;
  size_t corner = 0;
  for (size_t mi = 0; mi < mm.materials.size(); mi++) {
    const size_t end = std::min(mm.indices.size(), corner + mm.materials[mi].corners);
    for (; corner + 3 <= end; corner += 3) {
      uint32_t v[3] = {mm.indices[corner], mm.indices[corner + 1], mm.indices[corner + 2]};
      if (v[0] >= nv || v[1] >= nv || v[2] >= nv || v[0] == v[1] || v[1] == v[2] || v[0] == v[2]) continue;
      if (flip) std::swap(v[1], v[2]);
      Vec2 uv[3];
      for (int k = 0; k < 3; k++) uv[k] = Vec2(mm.vertices[v[k]].uv.x, 1.0f - mm.vertices[v[k]].uv.y);  // (MMD's V runs down)
      m.add_face(v, 3, uv, (int)mi);
    }
    corner = std::max(corner, end);
  }
}

/* Vertex morphs as shape keys; a group morph as the sum of its members' (one level: groups of groups are rare). Keys
 * are dense (an offset for every vertex): past the budget the rest are left out, and said so. */
void build_shape_keys(const MmdModel &mm, Mesh &m, ImportResult &out) {
  const size_t nv = mm.vertices.size();
  const size_t most = std::min(kMaxKeys, std::max<size_t>(1, kKeyBudget / (sizeof(Vec3) * std::max<size_t>(1, nv))));
  size_t over = 0;
  std::vector<int> key_of(mm.morphs.size(), -1);
  for (size_t i = 0; i < mm.morphs.size(); i++) {
    const MmdMorph &mo = mm.morphs[i];
    if (mo.type != 1 || mo.name.empty() || m.find_shape(mo.name) >= 0) continue;
    const bool any = std::any_of(mo.offsets.begin(), mo.offsets.end(), [&](const std::pair<int, Vec3> &o) {
      return o.first >= 0 && (size_t)o.first < nv && length_sq(o.second) > 0.0f;
    });
    if (!any) continue;
    if (m.shape_keys.size() >= most) {
      over++;
      continue;
    }
    ShapeKey k{mo.name, std::vector<Vec3>(nv, Vec3(0.0f))};
    for (const auto &[v, d] : mo.offsets)
      if (v >= 0 && (size_t)v < nv) k.delta[(size_t)v] = mmd_offset(d);
    key_of[i] = (int)m.shape_keys.size();
    m.shape_keys.push_back(std::move(k));
  }
  for (const MmdMorph &mo : mm.morphs) {
    if (mo.type != 0 || mo.name.empty() || m.find_shape(mo.name) >= 0) continue;
    std::vector<std::pair<int, float>> members;
    for (const auto &[member, w] : mo.members)
      if (member >= 0 && (size_t)member < key_of.size() && key_of[(size_t)member] >= 0 && w != 0.0f) members.push_back({key_of[(size_t)member], w});
    if (members.empty()) continue;
    if (m.shape_keys.size() >= most) {
      over++;
      continue;
    }
    ShapeKey k{mo.name, std::vector<Vec3>(nv, Vec3(0.0f))};
    for (const auto &[key, w] : members) {
      const std::vector<Vec3> &src = m.shape_keys[(size_t)key].delta;
      for (size_t v = 0; v < nv; v++) k.delta[v] = k.delta[v] + src[v] * w;
    }
    m.shape_keys.push_back(std::move(k));
  }
  if (over) out.notes.push_back(strprintf("%zu morph(s) left out: at most %zu shape keys, within %zu MB (%zu vertices)", over, kMaxKeys, kKeyBudget >> 20, nv));
}

/* IK: on the chain's first link (its tip is the effector), as long as the chain; a knee's pole in front of it. */
void build_ik(const MmdModel &mm, const Built &b, ImportResult &out) {
  const size_t nb = mm.bones.size();
  for (size_t i = 0; i < nb; i++) {
    const MmdBone &bn = mm.bones[i];
    if (!bn.ik || bn.links.empty() || bn.links[0].bone < 0 || (size_t)bn.links[0].bone >= nb) continue;
    const size_t link = (size_t)bn.links[0].bone;
    ImportedIK ik;
    ik.node = (int)(b.first + link);
    ik.target = out.nodes[b.first + i].name;
    ik.chain_length = (int)std::min<size_t>(bn.links.size(), 8);
    ik.iterations = std::max(1, std::min(bn.ik_loops, 64));
    const std::string &knee = out.nodes[(size_t)ik.node].name;
    if (bn.links.size() == 2 && (knee == "LeftLeg" || knee == "RightLeg" || mm.bones[link].name.find("ひざ") != std::string::npos)) {
      ImportedNode pole;  // (a knee bends forward: MMD's model faces -Z, ours +Z)
      pole.name = knee + " Pole";
      pole.parent = (int)(b.first + i);
      const Vec3 at = b.head[link] + Vec3(0.0f, 0.0f, std::max(0.3f, b.len[link]));
      pole.position = b.world[i].conjugate().rotate(at - b.head[i]);
      ik.pole = pole.name;
      out.nodes.push_back(std::move(pole));
    }
    out.iks.push_back(std::move(ik));
  }
}

/* Physics: a bone whose rigid body MMD simulates is part of a spring chain. A chain starts where its parent is driven
 * by the animation, and again below a fork (a chain runs one child at a time: spring::chain_bones). */
void build_springs(const MmdModel &mm, const Built &b, ImportResult &out) {
  const size_t nb = mm.bones.size();
  std::vector<char> dynamic(nb, 0);
  std::vector<float> radius(nb, 0.0f);
  std::vector<int> children(nb, 0);
  for (size_t i = 0; i < nb; i++)
    if (b.parent[i] >= 0) children[(size_t)b.parent[i]]++;
  for (const MmdRigid &rb : mm.rigids) {
    if (rb.mode == 0 || rb.bone < 0 || (size_t)rb.bone >= nb) continue;
    dynamic[(size_t)rb.bone] = 1;
    const float size = rb.shape == 1 ? std::min(rb.size.x, rb.size.z) : rb.size.x;
    radius[(size_t)rb.bone] = std::isfinite(size) ? std::fabs(size) * kMmdScale : 0.0f;
  }
  for (size_t i = 0; i < nb; i++) {
    const int p = b.parent[i];
    if (!dynamic[i] || (p >= 0 && dynamic[(size_t)p] && children[(size_t)p] < 2)) continue;
    out.springs.push_back({(int)(b.first + i), mmd_spring_label(mm.bones[i].name), std::max(0.005f, std::min(radius[i], 0.2f))});
  }
}

/* What MMD does that this import doesn't, for the Console. */
void report(const MmdModel &mm, int moved, int missing_toons, ImportResult &out) {
  const int *s = mm.skipped_morphs;
  const int bone_uv_material = s[2] + s[3] + s[4] + s[5] + s[6] + s[7] + s[8];
  if (bone_uv_material || s[9] || s[10])
    out.notes.push_back(strprintf("%d bone, UV or material morph(s) and %d flip or impulse morph(s) not imported (vertex and group morphs are shape keys)",
                                  bone_uv_material, s[9] + s[10]));
  int follow = 0, spheres = 0;
  for (const MmdBone &b : mm.bones) follow += b.inherits;
  if (follow - moved > 0)
    out.notes.push_back(strprintf("%d bone(s) copy part of another's turn in MMD (twist and eye helpers): here they turn on their own", follow - moved));
  if (moved) out.notes.push_back(strprintf("%d bone(s) that copy another's whole turn (D bones): their weights moved onto that bone", moved));
  if (mm.sdef || mm.qdef) out.notes.push_back(strprintf("%d SDEF and %d QDEF vertices blended linearly", mm.sdef, mm.qdef));
  for (const MmdMaterial &m : mm.materials) spheres += m.sphere;
  if (spheres) out.notes.push_back(strprintf("%d sphere map(s) not used (MMD's fake reflections)", spheres));
  if (missing_toons) out.notes.push_back(strprintf("%d material(s) name a toon ramp not found beside the model (MMD's own toon01-10): the default shadow", missing_toons));
  if (mm.joints) out.notes.push_back(strprintf("%zu physics joint(s) not imported (the spring chains stand in)", mm.joints));
}

bool build(MmdModel &mm, const std::string &path, ImportResult &out) {
  const std::string dir = fs::parent(path);
  const std::string model = mm.name.empty() ? fs::stem(path) : mm.name;
  Built b;
  if (!build_bones(mm, out, b, out.error)) return false;
  int moved = 0;
  const std::vector<int> owner = weight_owner(mm, moved);
  MeshPtr m = std::make_shared<Mesh>();
  m->name = model;
  std::vector<Vec3> normals;
  for (const MmdVertex &v : mm.vertices) m->add_vert(mmd_point(v.p)), normals.push_back(normalize(mmd_offset(v.n)));
  build_groups(mm, out, b, owner, *m);
  build_faces(mm, *m);
  build_shape_keys(mm, *m, out);
  weld_exact(*m, &normals);  // (MMD splits vertices at UV seams and at hard edges: the seams welded, the edges kept)
  m->smooth = true;
  m->smooth_angle = 180.0f;  // (the hard edges are the split vertices)
  m->sync_attributes();
  m->touch();
  ImportedNode mesh_node;
  mesh_node.name = model;
  mesh_node.mesh = m;
  ImageMemo memo;
  int missing_toons = 0;
  for (const MmdMaterial &mt : mm.materials) {
    mesh_node.materials.push_back(make_mmd_material(mt, dir, out, memo, missing_toons));
    out.materials.push_back(mesh_node.materials.back());
  }
  const int mesh_index = (int)out.nodes.size();
  out.nodes.push_back(std::move(mesh_node));
  if (!mm.bones.empty() && !m->vertex_groups.empty()) {
    ImportedSkin skin;
    skin.mesh_node = mesh_index;
    for (size_t k = 0; k < mm.bones.size(); k++) skin.joints.push_back((int)(b.first + k));
    out.skins.push_back(std::move(skin));
  }
  /* The outline: MMD draws an edge per material; Blendity's is per object, from the widest edge the model asks for. */
  float edge = 0.0f;
  Vec3 edge_color(0.0f);
  for (const MmdMaterial &mt : mm.materials)
    if ((mt.flags & 0x10) && mt.edge_size > edge) edge = mt.edge_size, edge_color = mt.edge_color.xyz();
  if (edge > 0.0f) out.outlines.push_back({mesh_index, std::min(0.05f, 0.004f * edge), edge_color});
  build_ik(mm, b, out);
  build_springs(mm, b, out);
  report(mm, moved, missing_toons, out);
  out.summary = strprintf("%zu vertices, %zu faces, %zu bones, %zu shape keys, %zu material(s), %zu IK, %zu spring chain(s)", m->vert_count(),
                          m->face_count(), mm.bones.size(), m->shape_keys.size(), mm.materials.size(), out.iks.size(), out.springs.size());
  return true;
}

}  // namespace

bool import_pmx_file(const std::string &path, ImportResult &out) {
  std::string data;
  if (!fs::read_file(path, data)) return out.error = "cannot read " + path, false;
  if (out.source.empty()) out.source = path;
  MmdModel mm;
  const bool pmd = to_lower(fs::extension(path)) == ".pmd";
  if (!(pmd ? read_pmd(data, mm, out.error) : read_pmx(data, mm, out.error))) return false;
  if (mm.vertices.empty() || mm.indices.empty()) return out.error = "the model has no mesh", false;
  return build(mm, path, out);
}

}  // namespace bl

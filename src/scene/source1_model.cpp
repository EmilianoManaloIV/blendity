// SPDX-License-Identifier: GPL-2.0-or-later
// Source SDK 2013 studio.h and optimize.h define the classic packed model records.
#include "source_content.h"

#include "import.h"
#include "source1_material.h"
#include "source1_model_binary.h"
#include "../core/core.h"

#include <array>
#include <unordered_map>
#include <unordered_set>

namespace bl {
namespace {

using source1::Bytes;
constexpr float kInch = 0.0254f;
constexpr size_t kVvdVertex = 48;
constexpr size_t kMdlModel = 148;
constexpr size_t kMdlMesh = 116;
constexpr uint32_t kStaticProp = 0x10;
constexpr size_t kMaxVertices = 1000000;
constexpr size_t kMaxTriangles = 2000000;
constexpr size_t kMaxWeightValues = 32 * 1024 * 1024;

Vec3 axis_convert(Vec3 v) { return {-v.y, v.z, v.x}; }

Vec3 vec3(const Bytes &b, size_t at) { return {b.f32(at), b.f32(at + 4), b.f32(at + 8)}; }

bool finite(Vec3 v) { return finite_bits(v.x) && finite_bits(v.y) && finite_bits(v.z); }

Mat4 matrix_convert(const Mat4 &m) {
  Mat4 out;
  // C R C^-1 and C t * inch, where C maps Source forward, left and up to +Z, -X and +Y.
  const Vec3 cols[4]{axis_convert(-m.column(1)), axis_convert(m.column(2)), axis_convert(m.column(0)), axis_convert(m.translation()) * kInch};
  for (int c = 0; c < 4; ++c) {
    out.at(0, c) = cols[c].x;
    out.at(1, c) = cols[c].y;
    out.at(2, c) = cols[c].z;
  }
  return out;
}

bool read_joint_bind(const Bytes &mdl, size_t at, Mat4 &bind) {
  Mat4 pose_to_bone;
  for (int row = 0; row < 3; ++row)
    for (int col = 0; col < 4; ++col) {
      const float value = mdl.f32(at + size_t(row * 4 + col) * 4);
      if (!finite_bits(value)) return false;
      pose_to_bone.at(row, col) = value;
    }
  const float det = dot(cross(pose_to_bone.column(0), pose_to_bone.column(1)), pose_to_bone.column(2));
  if (!finite_bits(det) || det < 1e-5f) return false;
  bind = matrix_convert(pose_to_bone.inverse());
  for (float f : bind.m) if (!finite_bits(f)) return false;
  return true;
}

struct Joint {
  int parent = -1;
  Mat4 source_default;  // original joint-to-parent pose in converted model coordinates
  Mat4 pose, bind;
};

bool solve_pose(size_t index, std::vector<Joint> &joints, std::vector<uint8_t> &state) {
  if (state[index]) return state[index] == 2;
  state[index] = 1;
  Joint &joint = joints[index];
  joint.pose = joint.source_default;
  if (joint.parent >= 0) {
    const size_t parent = (size_t)joint.parent;
    if (!solve_pose(parent, joints, state)) return false;
    joint.pose = joints[parent].pose * joint.pose;
  }
  if (!finite(joint.pose.translation())) return false;
  state[index] = 2;
  return true;
}

using Limb = std::array<size_t, 3>;

bool find_limbs(const ImportResult &out, const char *prefix, bool valvebiped, std::array<Limb, 4> &limbs) {
  std::unordered_map<std::string, size_t> by_name;
  for (size_t i = 0; i < out.nodes.size(); ++i) by_name.emplace(to_lower(out.nodes[i].name), i);
  static const char *const valve_segments[2][3] = {{"upperarm", "forearm", "hand"}, {"thigh", "calf", "foot"}};
  static const char *const bip_segments[2][3] = {{"upperarm", "lowerarm", "hand"}, {"hip", "knee", "foot"}};
  const auto *segments = valvebiped ? valve_segments : bip_segments;
  const std::string namespace_prefix = prefix;
  if (!by_name.count(namespace_prefix + "pelvis") ||
      (!by_name.count(namespace_prefix + "head") && !by_name.count(namespace_prefix + "head1"))) return false;
  for (size_t side = 0; side < 2; ++side)
    for (size_t type = 0; type < 2; ++type) {
      const char *s = side == 0 ? "l" : "r";
      Limb &limb = limbs[side * 2 + type];
      for (size_t i = 0; i < limb.size(); ++i) {
        const std::string name = valvebiped ? std::string(prefix) + s + '_' + segments[type][i]
                                           : std::string(prefix) + segments[type][i] + '_' + s;
        const auto joint = by_name.find(name);
        if (joint == by_name.end()) return false;
        limb[i] = joint->second;
        if (i && out.nodes[limb[i]].parent != (int)limb[i - 1]) return false;
      }
    }
  return true;
}

Quat joint_rotation(const Mat4 &m) {
  return quat_from_axes(normalize(m.column(0)), normalize(m.column(1)), normalize(m.column(2)));
}

// Native IK reaches each joint's +Y tip. Rebase the eight limb axes for named
// humanoids only. Post-multiplying bind and pose by the same local rotation
// cancels in pose * inverse(bind), leaving the skin and joint origins unchanged.
void aim_limbs(ImportResult &out, std::vector<Joint> &joints) {
  std::array<Limb, 4> limbs;
  if (!find_limbs(out, "valvebiped.bip01_", true, limbs) && !find_limbs(out, "bip_", false, limbs)) return;
  for (const Limb &limb : limbs)
    for (size_t i = 0; i < 2; ++i) {
      Joint &joint = joints[limb[i]];
      const Vec3 delta = joints[limb[i + 1]].bind.translation() - joint.bind.translation();
      const float len = length(delta);
      if (!(len > 1e-5f) || !finite_bits(len)) continue;
      const Vec3 y = delta / len;
      Vec3 x = normalize(joint.bind.column(0));
      if (std::fabs(dot(x, y)) > 0.95f) x = normalize(joint.bind.column(2));
      const Vec3 z = normalize(cross(x, y));
      if (length(z) < 1e-5f) continue;
      const Quat aligned = quat_from_axes(cross(y, z), y, z);
      const Mat4 rebase = Mat4::rotate(joint_rotation(joint.bind).conjugate() * aligned);
      joint.pose = joint.pose * rebase;
      joint.bind = joint.bind * rebase;
      out.nodes[limb[i]].length = len;
    }
}

bool read_bones(const Bytes &mdl, ImportResult &out, std::string &error) {
  size_t start, count;
  if (!mdl.table(0, 156, 160, 216, start, count) || count > 256) return false;
  if (mdl.u32(152) & kStaticProp) return true;
  std::vector<Joint> joints(count);
  out.nodes.resize(count);
  std::unordered_set<std::string> names;
  for (size_t i = 0; i < count; ++i) {
    const size_t at = start + i * 216;
    ImportedNode &node = out.nodes[i];
    if (!mdl.u32(at)) return false;
    if (const char *reason = mdl.string_at(at, mdl.u32(at), node.name)) {
      error = "MDL bone name " + std::to_string(i) + ' ' + reason;
      return false;
    }
    if (node.name.empty()) return false;
    const std::string original = node.name;
    for (size_t n = 2; !names.insert(node.name).second; ++n) node.name = original + '.' + std::to_string(n);
    const int parent = mdl.i32(at + 4);
    if (parent < -1 || parent >= (int)count || parent == (int)i) return false;
    const Vec3 p = vec3(mdl, at + 32), qv = vec3(mdl, at + 44);
    const float qw = mdl.f32(at + 56), qlen2 = dot(qv, qv) + qw * qw;
    if (!finite(p) || !finite_bits(qlen2) || qlen2 < 1e-8f) return false;
    Joint &joint = joints[i];
    joint.parent = node.parent = parent;
    joint.source_default = matrix_convert(Mat4::trs(p, normalize(Quat(qv.x, qv.y, qv.z, qw)), Vec3(1.0f)));
    if (!read_joint_bind(mdl, at + 96, joint.bind)) return false;
    node.is_bone = node.has_bind = true;
  }
  std::vector<uint8_t> state(count);
  for (size_t i = 0; i < count; ++i) if (!solve_pose(i, joints, state)) return false;
  aim_limbs(out, joints);
  for (size_t i = 0; i < count; ++i) {
    ImportedNode &node = out.nodes[i];
    const Joint &joint = joints[i];
    const Mat4 local = joint.parent >= 0 ? joints[(size_t)joint.parent].pose.inverse() * joint.pose : joint.pose;
    node.position = local.translation();
    node.rotation = joint_rotation(local);
    node.bind_world = joint.bind;
  }
  return true;
}

struct Vvd {
  const Bytes &file;
  size_t vertex_data = 0, count = 0;
  std::vector<uint32_t> order;
  size_t vertex(size_t index) const { return vertex_data + (order.empty() ? index : order[index]) * kVvdVertex; }
};

std::string read_vvd(Vvd &vvd, uint32_t checksum) {
  const Bytes &b = vvd.file;
  if (!b.fits(0, 64) || !b.magic("IDSV")) return "invalid VVD header";
  if (b.u32(4) != 4) return strprintf("unsupported VVD version %u", b.u32(4));
  if (b.u32(8) != checksum) return "VVD checksum differs from MDL";
  vvd.count = b.u32(16);
  vvd.vertex_data = b.u32(56);
  const size_t lods = b.u32(12);
  if (lods < 1 || lods > 8 || vvd.count > kMaxVertices || vvd.vertex_data < 64 ||
      !b.fits(vvd.vertex_data, vvd.count, kVvdVertex)) return "invalid VVD vertex table";
  size_t start, count;
  if (!b.table(0, 48, 52, 12, start, count) || count > kMaxVertices ||
      (count && (start < 64 || start + count * 12 > vvd.vertex_data))) return "invalid VVD fixup table";
  if (!count) return {};
  vvd.order.reserve(vvd.count);
  std::vector<bool> seen(vvd.count);
  // At root LOD 0 every fixup participates, in the order the records are stored.
  for (size_t i = 0; i < count; ++i) {
    const size_t at = start + i * 12;
    const size_t source = b.u32(at + 4), vertices = b.u32(at + 8);
    if (b.u32(at) >= lods || source > vvd.count || vertices > vvd.count - source || vertices > vvd.count - vvd.order.size()) return "invalid VVD fixup range";
    for (size_t j = source; j < source + vertices; ++j) {
      if (seen[j]) return "overlapping VVD fixup ranges";
      seen[j] = true;
      vvd.order.push_back((uint32_t)j);
    }
  }
  return vvd.order.size() == vvd.count ? std::string() : "VVD fixups do not cover LOD 0";
}

struct Geometry {
  struct Face { uint32_t vertices[3]; uint32_t skin; };
  std::vector<Face> faces;
  size_t triangles_seen = 0;
};

struct StripGroup {
  const Bytes &vtx;
  size_t verts, vert_count, indices, index_count;
  size_t model_first, mesh_count, material;
  bool extended;

  bool read_strip(size_t at, Geometry &geo) const {
    const size_t count = vtx.u32(at), first = vtx.u32(at + 4);
    const size_t vertex_count = vtx.u32(at + 8), vertex_first = vtx.u32(at + 12);
    if (first > index_count || count > index_count - first || vertex_first > vert_count || vertex_count > vert_count - vertex_first) return false;
    const unsigned flags = vtx.u8(at + 18);
    if (flags == 1) {
      if (count % 3) return false;
      for (size_t j = first; j < first + count; j += 3)
        if (!triangle(j, j + 1, j + 2, geo)) return false;
      return true;
    }
    if (flags == 2 && !extended) {
      for (size_t j = 2; j < count; ++j) {
        const size_t start = first + j - 2;
        if (!triangle(start + (j % 2), start + (1 - j % 2), start + 2, geo)) return false;
      }
      return true;
    }
    return count == 0;
  }

  bool triangle(size_t a, size_t b, size_t c, Geometry &geo) const {
    if (geo.triangles_seen++ == kMaxTriangles) return false;
    Geometry::Face face{{}, (uint32_t)material};
    size_t j = 0;
    for (size_t i : {a, b, c}) {
      const size_t vertex = vtx.u16(indices + i * 2);
      if (vertex >= vert_count) return false;
      const size_t mesh_vertex = vtx.u16(verts + vertex * 9 + 4);
      if (mesh_vertex >= mesh_count) return false;
      face.vertices[j++] = (uint32_t)(model_first + mesh_vertex);
    }
    if (face.vertices[0] == face.vertices[1] || face.vertices[1] == face.vertices[2] || face.vertices[2] == face.vertices[0]) return true;
    // VTX winding is clockwise in Source's right-handed frame. Axis conversion
    // reflects it, so retaining index order gives Blendity front-facing normals.
    geo.faces.push_back(face);
    return true;
  }
};

struct TopologyReader {
  const Bytes &mdl, &vtx;
  size_t vertex_count;
  bool extended;

  bool read_mesh(size_t mesh, size_t model_first, size_t model_count, size_t vtx_mesh, Geometry &geo) const {
    const size_t group_stride = extended ? 33 : 25, strip_stride = extended ? 35 : 27;
    const size_t mesh_count = mdl.u32(mesh + 8), mesh_first = mdl.u32(mesh + 12);
    if (mesh_first > model_count || mesh_count > model_count - mesh_first) return false;
    size_t groups, group_count;
    if (!vtx.table(vtx_mesh, 0, 4, group_stride, groups, group_count) || group_count > 32) return false;
    for (size_t i = 0; i < group_count; ++i) {
      const size_t group = groups + i * group_stride;
      StripGroup tables{vtx, 0, 0, 0, 0, model_first + mesh_first, mesh_count, mdl.u32(mesh), extended};
      size_t strips, strip_count;
      if (!vtx.table(group, 0, 4, 9, tables.verts, tables.vert_count) ||
          !vtx.table(group, 8, 12, 2, tables.indices, tables.index_count) ||
          !vtx.table(group, 16, 20, strip_stride, strips, strip_count)) return false;
      // The following flags are optional semantics; reserved header bits indicate the wrong stride.
      if (vtx.u8(group + 24) & 0xf0) return false;
      if (extended) {
        size_t split, split_count;
        if (!vtx.table(group, 25, 29, 2, split, split_count)) return false;
      }
      if (tables.vert_count > 65536 || tables.index_count > kMaxTriangles * 3 || strip_count > tables.index_count + 1 ||
          (tables.index_count && !strip_count)) return false;
      for (size_t s = 0; s < strip_count; ++s)
        if (!tables.read_strip(strips + s * strip_stride, geo)) return false;
    }
    return true;
  }

  bool read_model(size_t model, size_t vtx_model, Geometry &geo) const {
    size_t meshes, mesh_count, lod, lod_count;
    if (!mdl.table(model, 72, 76, kMdlMesh, meshes, mesh_count) ||
        !vtx.table(vtx_model, 0, 4, 12, lod, lod_count) || lod_count > 8) return false;
    if (!mesh_count) return true;  // blank default bodygroup variant
    if (!lod_count) return false;
    const size_t model_count = mdl.u32(model + 80), model_bytes = mdl.u32(model + 84);
    const size_t model_first = model_bytes / kVvdVertex;
    if (model_bytes % kVvdVertex || model_first > vertex_count || model_count > vertex_count - model_first) return false;
    size_t vtx_meshes, vtx_count;
    if (!vtx.table(lod, 0, 4, 9, vtx_meshes, vtx_count) || mesh_count != vtx_count) return false;
    for (size_t i = 0; i < mesh_count; ++i)
      if (!read_mesh(meshes + i * kMdlMesh, model_first, model_count, vtx_meshes + i * 9, geo)) return false;
    return true;
  }

  bool read(Geometry &geo) const {
    size_t body, body_count, vtx_body, vtx_count;
    if (!mdl.table(0, 232, 236, 16, body, body_count) ||
        !vtx.table(0, 28, 32, 8, vtx_body, vtx_count) || body_count != vtx_count || body_count > 256) return false;
    for (size_t i = 0; i < body_count; ++i) {
      size_t model, model_count, vtx_model, vtx_model_count;
      if (!mdl.table(body + i * 16, 4, 12, kMdlModel, model, model_count) ||
          !vtx.table(vtx_body + i * 8, 0, 4, 8, vtx_model, vtx_model_count) || model_count != vtx_model_count) return false;
      if (model_count && !read_model(model, vtx_model, geo)) return false;  // body value zero selects variant zero
    }
    return !geo.faces.empty();
  }
};

std::string as_virtual_path(std::string name) {
  for (char &c : name) if (c == '\\') c = '/';
  while (!name.empty() && name.back() == '/') name.pop_back();
  return name;
}

struct MaterialBinding {
  std::string path;
  std::string identity;
};

MaterialBinding find_vmt(const SourceContent &content, const std::vector<std::string> &dirs, std::string name,
                         const std::string &model_path) {
  name = source1::canonical_virtual_path(as_virtual_path(std::move(name)));
  const bool rooted = name.rfind("materials/", 0) == 0;
  if (rooted) name.erase(0, 10);
  if (fs::extension(name) != ".vmt") name += ".vmt";
  const std::string direct = "materials/" + name;
  const auto applies_prefix = [&](const std::string &dir) { return !dir.empty() && name.rfind(dir + '/', 0) != 0; };
  if (rooted || std::none_of(dirs.begin(), dirs.end(), applies_prefix)) return {direct, "source1:vmt:" + direct};

  // A relative reference can resolve under a later $cdmaterials directory or directly under materials.
  // The binding is stable across mounts and independent of which candidate is available today.
  const std::string identity = "source1:mdl:" + model_path + ":vmt:" + name;
  std::string error;
  SourceEntry entry;
  const auto candidate = [&](const std::string &dir) { return applies_prefix(dir) ? "materials/" + dir + '/' + name : direct; };
  bool tried_direct = false;
  for (const auto &dir : dirs) {
    const std::string path = candidate(dir);
    if (path == direct) {
      if (tried_direct) continue;
      tried_direct = true;
    }
    if (content.locate(path, entry, error)) return {entry.virtual_path, identity};
  }
  if (!tried_direct && content.locate(direct, entry, error)) return {entry.virtual_path, identity};
  return {candidate(dirs.front()), identity};
}

struct Materials {
  std::vector<std::string> names, directories;
  std::vector<uint16_t> skin;

  bool read(const Bytes &mdl, std::string &error) {
    size_t start, count;
    if (!mdl.table(0, 204, 208, 64, start, count) || count > 4096) return false;
    names.resize(count);
    for (size_t i = 0; i < count; ++i) {
      const size_t at = start + i * 64;
      if (!mdl.u32(at)) return false;
      if (const char *reason = mdl.string_at(at, mdl.u32(at), names[i])) {
        error = "MDL material name " + std::to_string(i) + ' ' + reason;
        return false;
      }
      if (names[i].empty()) return false;
    }
    if (!mdl.table(0, 212, 216, 4, start, count) || count > 256) return false;
    directories.resize(count);
    for (size_t i = 0; i < count; ++i) {
      if (const char *reason = mdl.string_at(0, mdl.u32(start + i * 4), directories[i])) {
        error = "MDL material directory " + std::to_string(i) + ' ' + reason;
        return false;
      }
      auto &dir = directories[i];
      dir = source1::canonical_virtual_path(as_virtual_path(std::move(dir)));
      if (dir == "materials") dir.clear();
      else if (dir.rfind("materials/", 0) == 0) dir.erase(0, 10);
    }
    const size_t families = mdl.u32(224);
    if (!mdl.table(0, 220, 228, 2, start, count) || count > 4096) return false;
    if (!families || !count) return true;
    if (!mdl.fits(start, families, count * 2)) return false;
    skin.resize(count);
    for (size_t i = 0; i < count; ++i) {
      skin[i] = mdl.u16(start + i * 2);
      if (skin[i] >= names.size()) return false;
    }
    return true;
  }

  bool slot(uint32_t skin_ref, uint32_t &texture) const {
    if (skin.empty()) { texture = skin_ref; return names.empty() ? skin_ref == 0 : texture < names.size(); }
    if (skin_ref >= skin.size()) return false;
    texture = skin[skin_ref];
    return true;
  }
};

struct Weight {
  std::array<uint8_t, 3> joints{};
  std::array<float, 3> values{};
};

bool read_vertex(const Vvd &vvd, size_t index, size_t bones, Mesh &mesh, std::vector<Vec2> &uv,
                 std::vector<Weight> &weights, std::vector<bool> &used_joints) {
  const Bytes &b = vvd.file;
  const size_t at = vvd.vertex(index);
  const Vec3 position = axis_convert(vec3(b, at + 16)) * kInch;
  const Vec3 normal = axis_convert(vec3(b, at + 28));
  const Vec2 tex(b.f32(at + 40), 1.0f - b.f32(at + 44));
  if (!finite(position) || !finite(normal) || !finite_bits(tex.x) || !finite_bits(tex.y)) return false;
  mesh.positions.push_back(position);
  mesh.custom_normals.push_back(normalize(normal));
  uv.push_back(tex);
  if (!bones) return true;
  Weight weight;
  const size_t count = b.u8(at + 15);
  if (count > 3) return false;
  for (size_t i = 0; i < count; ++i) {
    const uint8_t joint = b.u8(at + 12 + i);
    const float value = b.f32(at + i * 4);
    if (!finite_bits(value) || value < 0.0f || value > 1.0f) return false;
    if (!value) continue;
    if (joint >= bones) return false;
    weight.joints[i] = joint;
    weight.values[i] = value;
    used_joints[joint] = true;
  }
  weights.push_back(weight);
  return true;
}

bool attach_weights(const std::vector<Weight> &weights, const std::vector<bool> &used, const ImportResult &out, Mesh &mesh) {
  size_t groups = 0;
  for (bool u : used) groups += u;
  if (!groups) return true;
  if (weights.size() > kMaxWeightValues / groups) return false;
  std::vector<size_t> map(used.size());
  for (size_t j = 0; j < used.size(); ++j) {
    if (!used[j]) continue;
    map[j] = mesh.vertex_groups.size();
    mesh.vertex_groups.push_back({out.nodes[j].name, std::vector<float>(weights.size())});
  }
  for (size_t v = 0; v < weights.size(); ++v)
    for (size_t i = 0; i < 3; ++i)
      if (const float w = weights[v].values[i]; w > 0.0f) mesh.vertex_groups[map[weights[v].joints[i]]].weights[v] += w;
  return true;
}

std::string build_mesh(const SourceContent &content, const Geometry &geo, const Vvd &vvd, const Materials &materials,
                       const std::string &name, ImportResult &out) {
  ImportedNode node;
  node.name = name;
  node.mesh = std::make_shared<Mesh>();
  Mesh &mesh = *node.mesh;
  mesh.name = name;
  const size_t bones = out.nodes.size();
  std::vector<bool> used(bones);
  std::vector<Vec2> uv;
  std::vector<Weight> weights;
  std::vector<uint32_t> vertices(vvd.count, UINT32_MAX);
  std::unordered_map<uint32_t, int> slots;
  std::vector<uint32_t> ordered_textures;
  for (const auto &face : geo.faces) {
    uint32_t texture;
    if (!materials.slot(face.skin, texture)) return "invalid MDL default skin material index";
    const auto [slot, added] = slots.emplace(texture, (int)slots.size());
    if (added) ordered_textures.push_back(texture);
    uint32_t face_verts[3];
    Vec2 face_uv[3];
    for (size_t i = 0; i < 3; ++i) {
      const size_t original = face.vertices[i];
      uint32_t &v = vertices[original];
      if (v == UINT32_MAX) {
        v = (uint32_t)mesh.positions.size();
        if (!read_vertex(vvd, original, bones, mesh, uv, weights, used)) return "invalid VVD vertex or bone weight";
      }
      face_verts[i] = v;
      face_uv[i] = uv[v];
    }
    mesh.add_face(face_verts, 3, face_uv, slot->second);
  }
  if (!attach_weights(weights, used, out, mesh)) return "Source model exceeds native skin weight budget";
  if (std::any_of(mesh.custom_normals.begin(), mesh.custom_normals.end(), [](Vec3 n) { return length(n) < 1e-6f; })) mesh.custom_normals.clear();
  mesh.smooth = true;
  mesh.sync_attributes();
  mesh.touch();
  std::unordered_map<std::string, MaterialPtr> by_binding;
  for (uint32_t index : ordered_textures) {
    if (materials.names.empty()) {
      node.materials.push_back(make_material(name, Vec3(0.8f)));
      out.materials.push_back(node.materials.back());
      out.source_material_ids.emplace(node.materials.back().get(), "source1:mdl:" + out.source + ":default");
      continue;
    }
    const auto [path, identity] = find_vmt(content, materials.directories, materials.names[index], out.source);
    const auto [it, added] = by_binding.try_emplace(identity);
    if (added) {
      it->second = import_source1_material(content, path, out);
      out.source_material_ids.insert_or_assign(it->second.get(), identity);
      out.materials.push_back(it->second);
    }
    node.materials.push_back(it->second);
  }
  if (!mesh.vertex_groups.empty()) {
    ImportedSkin skin;
    skin.mesh_node = (int)bones;
    for (size_t i = 0; i < bones; ++i) skin.joints.push_back((int)i);
    out.skins.push_back(std::move(skin));
  }
  out.summary = strprintf("%zu vertices, %zu faces, %zu bones, %zu material(s), base LOD and default skin/bodygroups",
                          mesh.vert_count(), mesh.face_count(), bones, out.materials.size());
  out.nodes.push_back(std::move(node));
  return {};
}

bool fail(ImportResult &out, const std::string &path, const std::string &reason) {
  out.error = path + ": " + reason;
  return false;
}

}  // namespace

bool import_source1_model(const SourceContent &content, const std::string &virtual_path, ImportResult &out) {
  out = ImportResult{};
  if (const char *reason = source1::name_error(virtual_path))
    return fail(out, source1::escaped_name(virtual_path), std::string("MDL virtual path ") + reason);
  std::vector<uint8_t> mdl_data, vvd_data, vtx_data;
  if (!content.read(virtual_path, mdl_data, out.error)) return false;
  Bytes mdl{mdl_data};
  if (!mdl.fits(0, 240) || !mdl.magic("IDST")) return fail(out, virtual_path, "invalid MDL header");
  const uint32_t version = mdl.u32(4), checksum = mdl.u32(8);
  if (version != 48 && version != 49) return fail(out, virtual_path, strprintf("unsupported MDL version %u (expected 48 or 49)", version));
  const size_t declared = mdl.u32(76);
  if (declared < 240 || !mdl.fits(0, declared)) return fail(out, virtual_path, "truncated MDL");
  mdl.size = declared;
  const std::string base = virtual_path.substr(0, virtual_path.size() - fs::extension(virtual_path).size());
  const std::string vvd_path = base + ".vvd";
  if (!content.read(vvd_path, vvd_data, out.error)) return false;
  std::string vtx_path;
  std::string error;
  SourceEntry entry;
  for (const char *suffix : {".dx90.vtx", ".vtx", ".sw.vtx", ".dx80.vtx"}) {
    const std::string candidate = base + suffix;
    if (!content.locate(candidate, entry, error)) continue;
    vtx_path = candidate;
    break;
  }
  if (vtx_path.empty()) return fail(out, base + ".dx90.vtx", "missing VTX companion (also checked .vtx, .sw.vtx and .dx80.vtx)");
  if (!content.read(vtx_path, vtx_data, out.error)) return false;
  const Bytes vvd_bytes{vvd_data}, vtx{vtx_data};
  Vvd vvd{vvd_bytes, 0, 0, {}};
  if (const std::string invalid = read_vvd(vvd, checksum); !invalid.empty()) return fail(out, vvd_path, invalid);
  if (!vtx.fits(0, 36)) return fail(out, vtx_path, "truncated VTX header");
  if (vtx.u32(0) != 7) return fail(out, vtx_path, strprintf("unsupported VTX version %u", vtx.u32(0)));
  if (vtx.u32(16) != checksum) return fail(out, vtx_path, "VTX checksum differs from MDL");
  if (!vtx.u32(20) || vtx.u32(20) > 8) return fail(out, vtx_path, "invalid VTX LOD count");
  Geometry geometry;
  if (!TopologyReader{mdl, vtx, vvd.count, false}.read(geometry)) {
    geometry.faces.clear();
    geometry.triangles_seen = 0;
    if (version != 49 || !TopologyReader{mdl, vtx, vvd.count, true}.read(geometry)) return fail(out, vtx_path, "invalid or unsupported MDL/VTX default geometry tables");
  }
  ImportResult result;
  result.source = source1::canonical_virtual_path(virtual_path);
  error.clear();
  if (!read_bones(mdl, result, error)) return fail(out, virtual_path, error.empty() ? "invalid MDL skeleton or bind matrix" : error);
  Materials materials;
  if (!materials.read(mdl, error)) return fail(out, virtual_path, error.empty() ? "invalid MDL material table" : error);
  if (const std::string invalid = build_mesh(content, geometry, vvd, materials, fs::stem(virtual_path), result); !invalid.empty())
    return fail(out, virtual_path, invalid);
  out = std::move(result);
  return true;
}

}  // namespace bl

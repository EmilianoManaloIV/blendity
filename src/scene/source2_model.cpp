// SPDX-License-Identifier: GPL-2.0-or-later
#include "source2_model.h"

#include "import.h"
#include "source2_material.h"
#include "source2_resource.h"
#include "source_content.h"
#include "../core/core.h"
#include "../deps/meshopt.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#ifdef BL_WITH_ZSTD
#  include <zstd.h>
#endif

namespace bl {
namespace {
using namespace source2;

void expect(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

Bytes bytes_at(Bytes bytes, size_t offset, size_t count) {
  expect(offset <= bytes.size() && count <= bytes.size() - offset, "truncated model mesh buffer");
  return bytes.substr(offset, count);
}

template<class T> T from(Bytes bytes, size_t offset) {
  T value;
  std::memcpy(&value, bytes_at(bytes, offset, sizeof(T)).data(), sizeof(T));
  return value;
}

int index(const Json &value, int fallback = -1) {
  double n = value.number(fallback);
  expect(finite_bits(n) && n >= INT32_MIN && n <= INT32_MAX && std::floor(n) == n, "invalid model integer");
  return int(n);
}

uint64_t visibility_mask(const Json &value, uint64_t fallback) {
  if (value.type == Json::Null) return fallback;
  uint64_t bits = 0;
  expect(kv3_integer_bits(value, bits), "invalid model visibility mask");
  return bits;
}

size_t count(const Json &value, size_t max, int fallback = -1) {
  int n = index(value, fallback);
  expect(n >= 0 && size_t(n) <= max, "invalid model count or offset");
  return size_t(n);
}

float scalar(const Json &value, float fallback = 0) {
  double n = value.number(fallback);
  expect(finite_bits(n) && std::abs(n) < 1e10, "invalid model transform");
  return float(n);
}

bool flag(const Json &value) { return value.type == Json::Bool ? value.b : value.number() == 1; }

Json read_block(Bytes bytes, const char *tag) {
  Json result;
  std::string error;
  if (!read_kv3(bytes, result, error)) throw std::runtime_error(std::string(tag) + ": " + error);
  return result;
}

void check_version(const Resource &resource) {
  if (resource.version > 1)
    throw std::runtime_error("unsupported Source 2 model or mesh resource version " + std::to_string(resource.version));
}

Bytes block_at(const Resource &r, int i, std::string_view first, std::string_view second = {}) {
  expect(i >= 0 && size_t(i) < r.blocks.size(), "invalid model resource block reference");
  const auto &block = r.blocks[size_t(i)];
  expect(block.first == first || block.first == second, "unexpected model resource block tag");
  return block.second;
}

struct Attribute {
  std::string name;
  size_t semantic_index = 0, format = 0, offset = 0;
};

std::string semantic(std::string name) {
  auto terminator = name.find('\0');
  if (terminator != std::string::npos) name.resize(terminator);
  for (char &c : name) c = char(std::toupper(static_cast<unsigned char>(c)));
  return name;
}

struct Buffer {
  size_t count = 0, stride = 0;
  std::vector<Attribute> attributes;
  std::vector<uint8_t> decoded;
  Bytes packed;
  bool meshopt = false, sequence = false, zstd = false;
  Bytes data() const { return decoded.empty() ? packed : Bytes(decoded.data(), decoded.size()); }
};

void decode(Buffer &b, bool vertex) {
  constexpr size_t limit = 256 * 1024 * 1024;
  expect(b.stride && b.stride <= 4096 && b.count <= limit / b.stride, "model mesh buffer is too large");
  const size_t size = b.count * b.stride;
  Bytes input = b.packed;
  std::vector<uint8_t> unpacked;
  if (b.zstd) {
#ifdef BL_WITH_ZSTD
    unpacked.resize(size);
    size_t written = ZSTD_decompress(unpacked.data(), unpacked.size(), input.data(), input.size());
    expect(!ZSTD_isError(written) && written <= size, "invalid Zstandard model mesh buffer");
    unpacked.resize(written);
    input = {unpacked.data(), unpacked.size()};
#else
    throw std::runtime_error("Zstandard model mesh buffer requires Blender's libraries");
#endif
  }
  if (!b.meshopt) {
    expect(input.size() == size, "wrong model mesh buffer size");
    b.decoded = std::move(unpacked);
    return;
  }
#ifdef BL_WITH_MESHOPT
  expect(vertex ? b.stride % 4 == 0 && b.stride <= 256 : b.stride == 2 || b.stride == 4,
         "unsupported meshoptimizer element size");
  expect(vertex || b.sequence || b.count % 3 == 0, "invalid meshoptimizer triangle index count");
  b.decoded.resize(size);
  int result = 0;
  if (vertex) result = meshopt_decodeVertexBuffer(b.decoded.data(), b.count, b.stride, input.data(), input.size());
  else if (b.sequence) result = meshopt_decodeIndexSequence(b.decoded.data(), b.count, b.stride, input.data(), input.size());
  else result = meshopt_decodeIndexBuffer(b.decoded.data(), b.count, b.stride, input.data(), input.size());
  expect(result == 0, "unsupported or invalid meshoptimizer model mesh buffer");
#else
  (void)vertex;
  throw std::runtime_error("meshoptimizer model mesh buffer requires Blender's libraries");
#endif
}

struct Geometry {
  std::vector<Buffer> vertices, indices;
};

Buffer binary_buffer(Bytes bytes, size_t loc) {
  bytes_at(bytes, loc, 24);
  Buffer b;
  b.count = from<uint32_t>(bytes, loc);
  uint32_t flags = from<uint32_t>(bytes, loc + 4);
  b.stride = flags & 0x3FFFFFF;
  expect(b.stride > 0, "invalid model mesh buffer stride");
  size_t field_count = from<uint32_t>(bytes, loc + 12);
  size_t field_start = loc + 8 + from<uint32_t>(bytes, loc + 8);
  expect(field_count <= 256, "too many model vertex attributes");
  for (size_t i = 0; i < field_count; ++i) {
    Bytes field = bytes_at(bytes, field_start + i * 56, 56);
    std::string name(reinterpret_cast<const char *>(field.data()), 32);
    b.attributes.push_back({semantic(std::move(name)), from<uint32_t>(field, 32), from<uint32_t>(field, 36),
                            from<uint32_t>(field, 40)});
  }
  b.packed = bytes_at(bytes, loc + 16 + from<uint32_t>(bytes, loc + 16), from<uint32_t>(bytes, loc + 20));
  uint64_t expected = b.count * uint64_t(b.stride);
  if (b.packed.size() < expected) {
    b.meshopt = !(flags & 0x4000000);
    b.zstd = (flags & 0x8000000) != 0;
  }
  else b.packed = b.packed.substr(0, size_t(expected));
  return b;
}

std::vector<Buffer> binary_buffers(Bytes bytes, size_t hdr, bool vertex) {
  size_t offset = hdr + from<uint32_t>(bytes, hdr);
  size_t count = from<uint32_t>(bytes, hdr + 4);
  expect(count <= 4096, "too many model mesh buffers");
  std::vector<Buffer> out;
  for (size_t i = 0; i < count; ++i) {
    Buffer b = binary_buffer(bytes, offset + i * 24);
    decode(b, vertex);
    out.push_back(std::move(b));
  }
  return out;
}

std::vector<Buffer> described_buffers(const Resource &r, const Json &list, bool vertex) {
  expect(list.size() <= 4096, "too many model mesh buffers");
  std::vector<Buffer> out;
  for (const Json &item : list.arr) {
    Buffer b;
    b.count = count(item["m_nElementCount"], INT32_MAX);
    b.stride = count(item["m_nElementSizeInBytes"], 4096);
    const Json &raw = item["m_pData"];
    if (raw.type == Json::Str) {
      b.packed = {reinterpret_cast<const uint8_t *>(raw.str.data()), raw.str.size()};
      b.meshopt = b.packed.size() != b.count * uint64_t(b.stride);
    }
    else {
      int i = index(item["m_nBlockIndex"]);
      if (b.count) b.packed = block_at(r, i, vertex ? "MVTX" : "MIDX");
      b.meshopt = flag(item["m_bMeshoptCompressed"]);
      b.sequence = flag(item["m_bMeshoptIndexSequence"]);
      b.zstd = flag(item["m_bCompressedZSTD"]);
      expect(!flag(item["m_bCompressedZ3"]), "unsupported Z3 model mesh buffer");
    }
    for (const Json &attr : item["m_inputLayoutFields"].arr)
      b.attributes.push_back({semantic(attr["m_pSemanticName"].str), count(attr["m_nSemanticIndex"], 256, 0),
                              count(attr["m_Format"], 65535), count(attr["m_nOffset"], b.stride)});
    if (b.count) decode(b, vertex);
    out.push_back(std::move(b));
  }
  return out;
}

Geometry geometry(const Resource &r, const Json &metadata, Bytes buffers) {
  if (!buffers.empty()) return {binary_buffers(buffers, 0, true), binary_buffers(buffers, 8, false)};
  return {described_buffers(r, metadata["m_vertexBuffers"], true),
          described_buffers(r, metadata["m_indexBuffers"], false)};
}

void bind_pose(ImportResult &result, std::vector<uint8_t> &state, size_t i, size_t depth = 0) {
  if (state[i] == 2) return;
  expect(state[i] == 0 && depth < 512, "cyclic or excessively deep model skeleton");
  state[i] = 1;
  auto &node = result.nodes[i];
  Mat4 local = Mat4::trs(node.position, node.rotation, node.scale);
  if (node.parent >= 0) {
    bind_pose(result, state, size_t(node.parent), depth + 1);
    node.bind_world = result.nodes[size_t(node.parent)].bind_world * local;
  }
  else node.bind_world = local;
  node.has_bind = true;
  state[i] = 2;
}

void skeleton(const Json &model, ImportResult &result) {
  const Json &s = model["m_modelSkeleton"];
  const Json &names = s["m_boneName"];
  const Json &parents = s["m_nParent"];
  const Json &positions = s["m_bonePosParent"];
  const Json &rotations = s["m_boneRotParent"];
  expect(names.size() <= 4096 && names.size() == parents.size() && names.size() == positions.size() &&
             names.size() == rotations.size(), "incomplete model skeleton");
  std::unordered_set<std::string> names_seen;
  for (size_t i = 0; i < names.size(); ++i) {
    ImportedNode node;
    node.name = names[i].str;
    expect(!node.name.empty() && names_seen.insert(node.name).second, "empty or duplicate model bone name");
    node.is_bone = true;
    node.parent = index(parents[i]);
    expect(node.parent >= -1 && node.parent < int(names.size()), "invalid model bone parent");
    const Json &p = positions[i], &q = rotations[i];
    expect(p.size() == 3 && q.size() == 4, "invalid model bone transform");
    node.position = position({scalar(p[0]), scalar(p[1]), scalar(p[2])});
    node.rotation = normalize(rotation({scalar(q[0]), scalar(q[1]), scalar(q[2]), scalar(q[3])}));
    float scale = scalar(s["m_boneScaleParent"][i], 1);
    expect(std::abs(scale) > 1e-6f, "singular model bone scale");
    node.scale = {scale, scale, scale};
    result.nodes.push_back(std::move(node));
  }
  std::vector<uint8_t> state(names.size());
  for (size_t i = 0; i < names.size(); ++i) bind_pose(result, state, i);
}

struct Channel {
  const Buffer *buffer = nullptr;
  size_t format = 0, offset = 0, first = 0;

  Bytes at(size_t i) const {
    expect(first <= buffer->count && i < buffer->count - first, "model vertex stream index is out of bounds");
    return bytes_at(buffer->data(), (first + i) * buffer->stride + offset, buffer->stride - offset);
  }
};

Channel channel(const Geometry &g, const Json &draw, std::string_view name) {
  for (const Json &binding : draw["m_vertexBuffers"].arr) {
    size_t n = count(binding["m_hBuffer"], g.vertices.size());
    expect(n < g.vertices.size(), "missing model vertex buffer");
    const Buffer &buffer = g.vertices[n];
    if (!buffer.count) continue;
    size_t offset = count(binding["m_nBindOffsetBytes"], buffer.data().size(), 0);
    expect(offset % buffer.stride == 0, "unaligned model vertex buffer binding");
    for (const Attribute &a : buffer.attributes) {
      if (a.semantic_index || a.name != name) continue;
      expect(a.offset < buffer.stride, "model vertex attribute exceeds its stride");
      return {&buffer, a.format, a.offset, offset / buffer.stride};
    }
  }
  return {};
}

float half(uint16_t bits) {
  int exp = (bits >> 10) & 31;
  int frac = bits & 1023;
  float value = 0;
  if (exp == 0) value = std::ldexp(float(frac), -24);
  else if (exp == 31) value = std::numeric_limits<float>::infinity();
  else value = std::ldexp(float(frac + 1024), exp - 25);
  return bits & 0x8000 ? -value : value;
}

Vec2 uv(const Channel &c, size_t vertex) {
  if (!c.buffer) return {};
  Bytes data = c.at(vertex);
  Vec2 v;
  switch (c.format) {
    case 2: case 16: v = {from<float>(data, 0), from<float>(data, 4)}; break;
    case 10: case 34: v = {half(from<uint16_t>(data, 0)), half(from<uint16_t>(data, 2))}; break;
    case 11: case 35: v = {from<uint16_t>(data, 0) / 65535.0f, from<uint16_t>(data, 2) / 65535.0f}; break;
    case 13: case 37: v = {from<int16_t>(data, 0) / 32767.0f, from<int16_t>(data, 2) / 32767.0f}; break;
    default: throw std::runtime_error("unsupported model TEXCOORD format " + std::to_string(c.format));
  }
  expect(finite_bits(v.x) && finite_bits(v.y), "invalid model UV");
  return {v.x, 1.0f - v.y};
}

Vec3 vec3(Bytes data) { return {from<float>(data, 0), from<float>(data, 4), from<float>(data, 8)}; }

bool finite_vector(Vec3 v) { return finite_bits(v.x) && finite_bits(v.y) && finite_bits(v.z); }

Vec3 normal(const Channel &c, size_t vertex) {
  if (!c.buffer) return {};
  Bytes data = c.at(vertex);
  Vec3 n;
  if (c.format == 6) n = vec3(data);
  else if (c.format == 42) {
    uint32_t bits = from<uint32_t>(data, 0);
    n.x = float((bits >> 12) & 1023) * (2.0f / 1023.0f) - 1;
    n.y = float(bits >> 22) * (2.0f / 1023.0f) - 1;
    n.z = 1 - std::abs(n.x) - std::abs(n.y);
    float t = std::max(-n.z, 0.0f);
    n.x -= std::copysign(t, n.x);
    n.y -= std::copysign(t, n.y);
  }
  else if (c.format == 28) {
    // Alyx stores the Z sign in the first byte's high half, then folds X and Y into 7-bit magnitudes with signs.
    int a = from<uint8_t>(data, 0), b = from<uint8_t>(data, 1);
    bool below = a < 128;
    a = a < 128 ? 127 - a : a - 128;
    b = b < 128 ? 127 - b : b - 128;
    n.x = float(a < 64 ? 63 - a : a - 64) / 63;
    n.y = float(b < 64 ? 63 - b : b - 64) / 63;
    n.z = (1 - n.x - n.y) * (below ? -1 : 1);
    if (a < 64) n.x = -n.x;
    if (b < 64) n.y = -n.y;
  }
  else throw std::runtime_error("unsupported model NORMAL format " + std::to_string(c.format));
  return finite_vector(n) ? normalize(direction(n)) : Vec3();
}

struct Remap {
  const Json *entries = nullptr;
  size_t begin = 0, end = 0;
};

Remap remapping(const Json &model, size_t mesh) {
  const Json &starts = model["m_remappingTableStarts"];
  const Json &entries = model["m_remappingTable"];
  if (mesh >= starts.size()) return {};
  size_t begin = count(starts[mesh], entries.size());
  size_t end = mesh + 1 < starts.size() ? count(starts[mesh + 1], entries.size()) : entries.size();
  expect(begin <= end, "invalid model joint remapping range");
  return {&entries, begin, end};
}

struct Influences {
  std::array<uint16_t, 8> joints{};
  std::array<float, 8> weights{};
  size_t count = 0;
};

Influences influences(const Channel &joint, const Channel &weight, size_t vertex, const Remap &remap, size_t bones,
                      size_t authored_weights) {
  Influences out;
  if (!joint.buffer || !bones || !authored_weights) return out;
  Bytes j = joint.at(vertex);
  size_t count = 4, width = 1, joint_slots = 4;
  switch (joint.format) {
    case 30: break;
    case 4: count = joint_slots = 8; width = 2; break;
    case 12: count = joint_slots = 8; break;
    case 14: width = 2; break;
    case 38: joint_slots = 2; width = 2; break;
    default: throw std::runtime_error("unsupported model BLENDINDICES format " + std::to_string(joint.format));
  }
  Bytes w = weight.buffer ? weight.at(vertex) : Bytes();
  double total = 0;
  for (size_t i = 0; i < count; ++i) {
    float n = i < authored_weights ? 1.0f : 0.0f;
    if (weight.buffer) {
      switch (weight.format) {
        case 2: n = from<float>(w, i * 4); break;
        case 11: n = count == 8 ? from<uint8_t>(w, i) / 255.0f : from<uint16_t>(w, i * 2) / 65535.0f; break;
        case 35: n = i < 2 ? from<uint16_t>(w, i * 2) / 65535.0f : 0; break;
        case 28: case 30: n = from<uint8_t>(w, i) / 255.0f; break;
        default: throw std::runtime_error("unsupported model BLENDWEIGHT format " + std::to_string(weight.format));
      }
    }
    if (!finite_bits(n) || n <= 0) continue;
    size_t slot = std::min(i, joint_slots - 1);
    int bone = width == 1 ? from<uint8_t>(j, slot) : from<uint16_t>(j, slot * 2);
    if (remap.entries) {
      expect(size_t(bone) < remap.end - remap.begin, "weighted joint is outside its model remapping table");
      bone = index((*remap.entries)[remap.begin + size_t(bone)]);
    }
    expect(bone >= 0 && size_t(bone) < bones, "weighted joint is outside the model skeleton");
    out.joints[out.count] = uint16_t(bone);
    out.weights[out.count++] = n;
    total += n;
  }
  if (total > 0)
    for (size_t i = 0; i < out.count; ++i) out.weights[i] = float(out.weights[i] / total);
  return out;
}

struct DrawStreams {
  Channel position, uv, normal, joint, weight;
  Bytes indices;
  size_t index_stride = 0, base = 0, max_vertex = 0, bone_weights = 1;
};

DrawStreams draw_streams(const Geometry &g, const Json &draw) {
  DrawStreams s;
  const Json &primitive = draw["m_nPrimitiveType"];
  expect(primitive.str == "RENDER_PRIM_TRIANGLES" || primitive.number(-1) == 5, "unsupported model primitive type");
  s.position = channel(g, draw, "POSITION");
  expect(s.position.buffer && (s.position.format == 6 || s.position.format == 2), "missing or unsupported model POSITION stream");
  s.uv = channel(g, draw, "TEXCOORD");
  s.normal = channel(g, draw, "NORMAL");
  s.joint = channel(g, draw, "BLENDINDICES");
  s.weight = channel(g, draw, "BLENDWEIGHT");
  if (!s.weight.buffer) s.weight = channel(g, draw, "BLENDWEIGHTS");
  const Json &binding = draw["m_indexBuffer"];
  size_t k = count(binding["m_hBuffer"], g.indices.size());
  expect(k < g.indices.size(), "missing model index buffer");
  const Buffer &indices = g.indices[k];
  s.index_stride = indices.stride;
  expect(s.index_stride == 2 || s.index_stride == 4, "unsupported model index stride");
  size_t offset = count(binding["m_nBindOffsetBytes"], indices.data().size(), 0);
  size_t start = count(draw["m_nStartIndex"], indices.count);
  size_t amount = count(draw["m_nIndexCount"], indices.count);
  expect(amount % 3 == 0, "model triangle index count is not a multiple of three");
  s.indices = bytes_at(indices.data(), offset + start * s.index_stride, amount * s.index_stride);
  const size_t vertex_count = s.position.buffer->count - s.position.first;
  s.base = count(draw["m_nBaseVertex"], vertex_count, 0);
  s.max_vertex = count(draw["m_nVertexCount"], vertex_count - s.base, int(vertex_count - s.base));
  return s;
}

struct MeshData {
  MeshPtr mesh = std::make_shared<Mesh>();
  std::vector<Vec3> normals;
  std::vector<Influences> weights;
  std::vector<MaterialPtr> materials;
};

uint32_t vertex(MeshData &mesh, const DrawStreams &s, const Remap &remap, size_t bones, size_t i) {
  size_t source = s.base + i;
  Vec3 point = vec3(s.position.at(source));
  expect(finite_vector(point) && std::abs(point.x) < 1e10f && std::abs(point.y) < 1e10f && std::abs(point.z) < 1e10f,
         "invalid model vertex position");
  size_t vertex_index = mesh.mesh->positions.size();
  expect(vertex_index < 8000000, "model mesh has too many vertices");
  mesh.mesh->positions.push_back(position(point));
  mesh.normals.push_back(normal(s.normal, source));
  mesh.weights.push_back(influences(s.joint, s.weight, source, remap, bones, s.bone_weights));
  return uint32_t(vertex_index);
}

void append_draw(MeshData &mesh, const DrawStreams &s, const Remap &remap, size_t bones, int material) {
  std::vector<uint32_t> mapped(s.max_vertex, UINT32_MAX);
  for (size_t i = 0; i < s.indices.size(); i += s.index_stride * 3) {
    size_t source[3];
    for (size_t j = 0; j < 3; ++j) {
      size_t at = i + (2 - j) * s.index_stride;  // the reflected basis reverses triangle winding
      source[j] = s.index_stride == 2 ? from<uint16_t>(s.indices, at) : from<uint32_t>(s.indices, at);
      expect(source[j] < mapped.size(), "model triangle index is outside its vertex range");
    }
    if (source[0] == source[1] || source[1] == source[2] || source[2] == source[0]) continue;
    uint32_t corners[3];
    Vec2 uvs[3];
    for (size_t j = 0; j < 3; ++j) {
      size_t original = source[j];
      auto &mapped_vertex = mapped[original];
      if (mapped_vertex == UINT32_MAX) mapped_vertex = vertex(mesh, s, remap, bones, original);
      corners[j] = mapped_vertex;
      uvs[j] = uv(s.uv, s.base + original);
    }
    mesh.mesh->add_face(corners, 3, s.uv.buffer ? uvs : nullptr, material);
  }
}

std::vector<int> bind_weights(MeshData &mesh, const ImportResult &result, size_t bones) {
  std::vector<int> groups(bones, -1);
  std::vector<int> joints;
  for (size_t i = 0; i < mesh.weights.size(); ++i) {
    const Influences &w = mesh.weights[i];
    for (size_t j = 0; j < w.count; ++j) {
      size_t bone = w.joints[j];
      int &group = groups[bone];
      if (group < 0) {
        expect(joints.size() < 16000000 / mesh.weights.size(), "model skin has too many vertex weights");
        group = int(joints.size());
        joints.push_back(int(bone));
        mesh.mesh->vertex_groups.push_back({result.nodes[bone].name, std::vector<float>(mesh.weights.size())});
      }
      mesh.mesh->vertex_groups[size_t(group)].weights[i] += w.weights[j];
    }
  }
  return joints;
}

struct ModelImport {
  const SourceContent &content;
  ImportResult &result;
  int selected_draw;
  int next_draw = 0;
  size_t bones = 0;
  std::unordered_map<std::string, MaterialPtr> materials;

  int material_slot(MeshData &mesh, const Json &draw) {
    std::string path = draw["m_material"].str;
    if (path.empty()) path = draw["m_pMaterial"].str;
    MaterialPtr mat = default_material();
    if (!path.empty()) {
      path = compiled_path(path);
      auto slot = materials.find(path);
      if (slot == materials.end()) slot = materials.emplace(path, read_material(content, path, result)).first;
      mat = slot->second;
    }
    auto existing = std::find(mesh.materials.begin(), mesh.materials.end(), mat);
    if (existing != mesh.materials.end()) return int(existing - mesh.materials.begin());
    mesh.materials.push_back(mat);
    return int(mesh.materials.size()) - 1;
  }

  void add_mesh(const std::string &name, const Json &data, const Geometry &g, const Remap &remap) {
    MeshData mesh;
    const size_t blend_count = count(data["m_skeleton"]["m_nBoneWeightCount"], 8, 1);
    for (const Json &object : data["m_sceneObjects"].arr) {
      for (const Json &draw : object["m_drawCalls"].arr) {
        int at = next_draw++;
        if (selected_draw >= 0 && selected_draw != at) continue;
        if (draw["m_nIndexCount"].number() == 0) continue;
        DrawStreams streams = draw_streams(g, draw);
        streams.bone_weights = blend_count;
        append_draw(mesh, streams, remap, bones, material_slot(mesh, draw));
      }
    }
    if (!mesh.mesh->face_count()) return;
    mesh.mesh->name = name;
    mesh.mesh->smooth = true;
    keep_file_normals(*mesh.mesh, mesh.normals);
    auto joints = bind_weights(mesh, result, bones);
    ImportedNode node;
    node.name = name;
    node.mesh = std::move(mesh.mesh);
    node.materials = std::move(mesh.materials);
    if (!joints.empty()) result.skins.push_back({int(result.nodes.size()), std::move(joints)});
    result.nodes.push_back(std::move(node));
  }

  void standalone(const Resource &r, const std::string &name, const Remap &remap = {}) {
    check_version(r);
    Bytes data = r.block("DATA");
    if (data.empty()) data = r.block("MDAT");
    Json mesh = read_block(data, "mesh DATA");
    Bytes buffers = r.block("VBIB");
    if (buffers.empty()) buffers = r.block("MBUF");
    add_mesh(name, mesh, geometry(r, mesh, buffers), remap);
  }

  void model(const Resource &r, const std::string &path) {
    check_version(r);
    Json model = read_block(r.block("DATA"), "model DATA");
    skeleton(model, result);
    bones = result.nodes.size();
    const uint64_t defaults = visibility_mask(model["m_nDefaultMeshGroupMask"], 0);
    auto visible = [&](size_t mesh) {
      const uint64_t lod_mask = visibility_mask(model["m_refLODGroupMasks"][mesh], 1);
      const uint64_t mesh_groups = visibility_mask(model["m_refMeshGroupMasks"][mesh], 0);
      return (lod_mask & 1) && (!defaults || !mesh_groups || (mesh_groups & defaults));
    };
    if (!r.block("CTRL").empty()) {
      Json control = read_block(r.block("CTRL"), "model CTRL");
      for (const Json &entry : control["embedded_meshes"].arr) {
        bool has_vbib = entry["vbib_block"].type != Json::Null;
        size_t mesh = count(entry[has_vbib ? "mesh_index" : "m_nMeshIndex"], 4096);
        if (!visible(mesh)) continue;
        Json data = read_block(block_at(r, index(entry[has_vbib ? "data_block" : "m_nDataBlock"]), "MDAT"), "MDAT");
        Bytes buffers;
        if (has_vbib) buffers = block_at(r, index(entry["vbib_block"]), "MBUF", "VBIB");
        const std::string &label = entry[has_vbib ? "name" : "m_Name"].str;
        add_mesh(label.empty() ? fs::stem(path) : label, data, geometry(r, entry, buffers), remapping(model, mesh));
      }
    }
    const Json &referenced = model["m_refMeshes"];
    for (size_t i = 0; i < referenced.size(); ++i) {
      if (!visible(i) || referenced[i].str.empty()) continue;
      std::string mesh_path = compiled_path(referenced[i].str);
      std::string error;
      std::vector<uint8_t> bytes;
      Resource mesh;
      if (!content.read(mesh_path, bytes, error) || !read_resource(bytes, mesh, error)) {
        result.notes.push_back(mesh_path + ": " + error);
        continue;
      }
      try {
        standalone(mesh, fs::stem(mesh_path), remapping(model, i));
      }
      catch (const std::runtime_error &e) {
        throw std::runtime_error(mesh_path + ": " + e.what());
      }
    }
  }
};

}  // namespace

bool import_source2_renderable(const SourceContent &content, const std::string &virtual_path, int draw_call, ImportResult &out) {
  ImportResult result;
  result.source = virtual_path;
  std::vector<uint8_t> bytes;
  Resource resource;
  if (!content.read(virtual_path, bytes, out.error) || !read_resource(bytes, resource, out.error)) return false;
  try {
    std::string ext = fs::extension(virtual_path);
    for (char &c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
    ModelImport importer{content, result, draw_call, 0, 0, {}};
    if (ext == ".vmdl_c") importer.model(resource, virtual_path);
    else if (ext == ".vmesh_c") importer.standalone(resource, fs::stem(virtual_path));
    else throw std::runtime_error("unsupported compiled Source 2 model or mesh extension");
    if (result.nodes.size() == importer.bones) {
      std::string reason = result.notes.empty() ? "" : " (" + result.notes.front() + ")";
      throw std::runtime_error(draw_call < 0 ? "no supported Source 2 triangles" + reason
                                             : "renderable draw call is absent or empty" + reason);
    }
    result.summary = std::to_string(result.nodes.size() - importer.bones) + " Source 2 meshes, " +
                     std::to_string(importer.bones) + " bones";
    out = std::move(result);
    return true;
  }
  catch (const std::runtime_error &e) {
    out.error = virtual_path + ": " + e.what();
    return false;
  }
}

bool import_source2_model(const SourceContent &content, const std::string &virtual_path, ImportResult &out) {
  return import_source2_renderable(content, virtual_path, -1, out);
}

}  // namespace bl

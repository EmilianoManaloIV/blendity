// SPDX-License-Identifier: GPL-2.0-or-later
#include "source1_map.h"

#include "../core/core.h"
#include "import.h"
#include "source_content.h"
#include "source1_material.h"
#include "../../extern/fast_float/fast_float.h"

#include <array>
#include <charconv>
#include <cstring>
#include <string_view>
#include <unordered_map>

namespace bl {
namespace {

// Record sizes and lump IDs from Valve's bspfile.h. Read each field explicitly to avoid alignment and host-endian assumptions.
constexpr size_t kHeaderSize = 1036;
constexpr size_t kMaxCorners = 8000000;
enum LumpId {
  Entities = 0, Planes = 1, TexData = 2, Vertices = 3, TexInfo = 6, Faces = 7, Edges = 12, SurfEdges = 13,
  Models = 14, DispInfo = 26, DispVertices = 33, Game = 35, Primitives = 37, PrimVertices = 38, PrimIndices = 39,
  Pak = 40, TexNames = 43, TexNameOffsets = 44, DispTriangles = 48, FacesHdr = 58
};

struct Bytes {
  const uint8_t *data = nullptr;
  size_t size = 0;

  bool contains(size_t at, size_t count) const { return at <= size && count <= size - at; }
  bool records(size_t at, size_t count, size_t stride) const { return at <= size && count <= (size - at) / stride; }
  uint16_t u16(size_t at) const { return uint16_t(data[at]) | (uint16_t(data[at + 1]) << 8); }
  uint32_t u32(size_t at) const { return uint32_t(u16(at)) | (uint32_t(u16(at + 2)) << 16); }
  float number(size_t at) const {
    uint32_t bits = u32(at);
    float result;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
  }
  Vec3 vec(size_t at) const { return {number(at), number(at + 4), number(at + 8)}; }
  std::string terminated(size_t at, size_t limit) const {
    if (!contains(at, limit) || !limit) return {};
    const auto *end = static_cast<const uint8_t *>(std::memchr(data + at, 0, limit));
    return end ? std::string(reinterpret_cast<const char *>(data + at), size_t(end - data - at)) : std::string();
  }
};

struct Bsp {
  Bytes file;
  std::array<Bytes, 64> lumps;
  std::array<uint32_t, 64> offsets{};

  bool lump(LumpId id, size_t stride, uint32_t version, std::string &error) {
    const size_t at = 8 + size_t(id) * 16;
    const uint32_t start = file.u32(at);
    const uint32_t length = file.u32(at + 4);
    if (!length) return true;
    const std::string tag = "BSP lump " + std::to_string(id);
    if (start < kHeaderSize || !file.contains(start, length) || length % stride)
      return error = tag + " has an invalid range or record size", false;
    if (file.u32(at + 12) != 0)
      return error = tag + " uses unsupported lump compression", false;
    if (file.u32(at + 8) > version)
      return error = tag + " has unsupported version " + std::to_string(file.u32(at + 8)), false;
    offsets[id] = start;
    lumps[id] = {file.data + start, length};
    return true;
  }

  bool open(const std::vector<uint8_t> &data, std::string &error) {
    file = {data.data(), data.size()};
    if (!file.contains(0, kHeaderSize) || std::memcmp(file.data, "VBSP", 4) != 0)
      return error = "Invalid classic Source BSP header", false;
    const uint32_t version = file.u32(4);
    if (version < 19 || version > 21)
      return error = "Unsupported classic Source BSP version " + std::to_string(version), false;
    return lump(Entities, 1, 0, error) && lump(Planes, 20, 0, error) && lump(TexData, 32, 0, error) &&
           lump(Vertices, 12, 0, error) && lump(TexInfo, 72, 0, error) && lump(Faces, 56, 1, error) &&
           lump(Edges, 4, 0, error) && lump(SurfEdges, 4, 0, error) && lump(Models, 48, 0, error) &&
           lump(DispInfo, 176, 0, error) && lump(DispVertices, 20, 0, error) &&
           lump(TexNames, 1, 0, error) && lump(TexNameOffsets, 4, 0, error) && lump(DispTriangles, 2, 0, error) &&
           lump(Game, 1, 0, error) && lump(Primitives, 10, 0, error) && lump(PrimVertices, 12, 0, error) &&
           lump(PrimIndices, 2, 0, error) && lump(Pak, 1, 0, error) &&
           (lumps[Faces].size || lump(FacesHdr, 56, 1, error));
  }

  Bytes faces() const { return lumps[Faces].size ? lumps[Faces] : lumps[FacesHdr]; }
};

bool reasonable(Vec3 v) {
  constexpr float limit = 1.0e8f;
  return finite_bits(v.x) && finite_bits(v.y) && finite_bits(v.z) &&
         std::abs(v.x) < limit && std::abs(v.y) < limit && std::abs(v.z) < limit;
}

Vec3 source_axes(Vec3 p) { return {-p.y, p.z, p.x}; }
Vec3 source_position(Vec3 p) { return source_axes(p) * 0.0254f; }

Quat source_angles(Vec3 angles) {
  // Source QAngle is yaw about Z, then local pitch about Y and roll about X.
  const Vec3 a = angles * kDeg2Rad;
  const Quat source = Quat::axis_angle({0, 0, 1}, a.y) * Quat::axis_angle({0, 1, 0}, a.x) *
                      Quat::axis_angle({1, 0, 0}, a.z);
  return quat_from_axes(source_axes(source.rotate({0, -1, 0})), source_axes(source.rotate({0, 0, 1})),
                        source_axes(source.rotate({1, 0, 0})));
}

char ascii_lower(char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; }

std::string virtual_name(std::string path) {
  for (char &c : path) c = c == '\\' ? '/' : ascii_lower(c);
  return path;
}

struct FaceTexture {
  std::string material;
  Vec3 u, v;
  float u_shift = 0, v_shift = 0;
  int width = 1, height = 1;
  bool hidden = false;

  Vec2 uv(Vec3 p) const {
    return {(dot(u, p) + u_shift) / float(width), 1.0f - (dot(v, p) + v_shift) / float(height)};
  }
};

bool face_texture(const Bsp &bsp, uint16_t index, FaceTexture &tex) {
  if (index == 0xffff) {
    tex.hidden = true;
    return true;
  }
  const Bytes infos = bsp.lumps[TexInfo], data = bsp.lumps[TexData];
  if (index >= infos.size / 72) return false;
  const size_t ti = size_t(index) * 72;
  // Valve bspflags.h: sky and entities' non-rendered tool surfaces have no native draw geometry.
  constexpr uint32_t sky_or_nodraw = 0x2 | 0x4 | 0x80 | 0x200;
  if (infos.u32(ti + 64) & sky_or_nodraw) {
    tex.hidden = true;
    return true;
  }
  const uint32_t td = infos.u32(ti + 68);
  if (td >= data.size / 32) return false;
  const size_t at = size_t(td) * 32;
  const uint32_t width = data.u32(at + 16), height = data.u32(at + 20);
  if (!width || !height || width > 65536 || height > 65536) return false;
  const Bytes offsets = bsp.lumps[TexNameOffsets], names = bsp.lumps[TexNames];
  const uint32_t name_id = data.u32(at + 12);
  if (name_id >= offsets.size / 4) return false;
  const uint32_t start = offsets.u32(size_t(name_id) * 4);
  if (start >= names.size) return false;
  std::string name = virtual_name(names.terminated(start, std::min<size_t>(names.size - start, 512)));
  if (name.empty()) return false;
  tex.material = "materials/" + name + ".vmt";
  tex.u = infos.vec(ti);
  tex.v = infos.vec(ti + 16);
  tex.u_shift = infos.number(ti + 12);
  tex.v_shift = infos.number(ti + 28);
  tex.width = int(width);
  tex.height = int(height);
  return reasonable(tex.u) && reasonable(tex.v) && finite_bits(tex.u_shift) && finite_bits(tex.v_shift) &&
         std::abs(tex.u_shift) < 1.0e8f && std::abs(tex.v_shift) < 1.0e8f;
}

bool face_points(const Bsp &bsp, size_t first, size_t count, std::vector<uint32_t> &ids, std::vector<Vec3> &points) {
  const Bytes surfedges = bsp.lumps[SurfEdges], edges = bsp.lumps[Edges], vertices = bsp.lumps[Vertices];
  if (count < 3 || count > 4096 || !surfedges.records(first * 4, count, 4)) return false;
  ids.resize(count);
  points.resize(count);
  uint32_t next = 0, initial = 0;
  for (size_t i = 0; i < count; ++i) {
    const uint32_t index = surfedges.u32((first + i) * 4);
    const bool reverse = (index & 0x80000000u) != 0;
    const uint32_t edge = reverse ? 0u - index : index;
    if (edge >= edges.size / 4) return false;
    const size_t at = size_t(edge) * 4;
    const uint32_t v = edges.u16(at + (reverse ? 2 : 0));
    if (v >= vertices.size / 12 || (i && v != next)) return false;
    if (!i) initial = v;
    next = edges.u16(at + (reverse ? 0 : 2));
    if (v == next) return false;
    ids[i] = v;
    points[i] = vertices.vec(size_t(v) * 12);
    if (!reasonable(points[i])) return false;
  }
  return next == initial;
}

using Materials = std::unordered_map<std::string, MaterialPtr>;

struct MeshBuild {
  MeshPtr mesh = std::make_shared<Mesh>();
  std::unordered_map<uint32_t, uint32_t> vertex_ids;
  std::unordered_map<std::string, int> slots;
  std::vector<std::string> materials;
  size_t degenerate_faces = 0;

  int slot(const std::string &path) {
    const auto result = slots.try_emplace(path, int(materials.size()));
    if (result.second) materials.push_back(path);
    return result.first->second;
  }

  uint32_t vertex(uint32_t id, Vec3 point) {
    auto v = vertex_ids.try_emplace(id, 0);
    if (v.second) v.first->second = mesh->add_vert(source_position(point));
    return v.first->second;
  }

  void polygon(const FaceTexture &tex, const std::vector<uint32_t> &ids, const std::vector<Vec3> &points, bool reverse) {
    std::vector<uint32_t> verts(ids.size());
    std::vector<Vec2> uvs(ids.size());
    for (size_t i = 0; i < ids.size(); ++i) {
      const size_t j = reverse ? ids.size() - 1 - i : i;
      verts[i] = vertex(ids[j], points[j]);
      uvs[i] = tex.uv(points[j]);
    }
    mesh->add_face(verts.data(), verts.size(), uvs.data(), slot(tex.material));
  }
};

float winding_facing(const std::vector<Vec3> &points, Vec3 normal) {
  Vec3 area;
  for (size_t i = 2; i < points.size(); ++i)
    area += cross(source_position(points[i - 1] - points[0]), source_position(points[i] - points[0]));
  return dot(area, normal);
}

void append_brush_winding(const FaceTexture &tex, const std::vector<uint32_t> &ids, const std::vector<Vec3> &points,
                          Vec3 normal, MeshBuild &build) {
  std::vector<uint32_t> unique = ids;
  std::sort(unique.begin(), unique.end());
  const float facing = winding_facing(points, normal);
  if (std::adjacent_find(unique.begin(), unique.end()) == unique.end()) {
    if (std::abs(facing) >= 1.0e-10f) build.polygon(tex, ids, points, facing < 0);
    else ++build.degenerate_faces;
    return;
  }

  // VBSP's T-junction fix can leave an edge traversed in both directions. Split the closed walk
  // on repeated vertices. A retraced spur forms a zero-area cycle and leaves the surface loop intact.
  const size_t before = build.mesh->face_count();
  std::unordered_map<uint32_t, size_t> on_path;
  std::vector<uint32_t> path;
  std::vector<Vec3> path_points;
  for (size_t i = 0; i <= ids.size(); ++i) {
    const size_t j = i == ids.size() ? 0 : i;
    const auto insertion = on_path.try_emplace(ids[j], path.size());
    if (insertion.second) {
      path.push_back(ids[j]);
      path_points.push_back(points[j]);
      continue;
    }
    const size_t start = insertion.first->second;
    if (path.size() - start >= 3) {
      const std::vector<Vec3> cycle(path_points.begin() + start, path_points.end());
      if (std::abs(winding_facing(cycle, normal)) >= 1.0e-10f)
        build.polygon(tex, {path.begin() + start, path.end()}, cycle, facing < 0);
    }
    for (size_t k = start + 1; k < path.size(); ++k) on_path.erase(path[k]);
    path.resize(start + 1);
    path_points.resize(start + 1);
  }
  if (build.mesh->face_count() == before) ++build.degenerate_faces;
}

struct BSPPrimitive {
  bool strip = false;
  size_t first_index = 0, index_count = 0, first_vertex = 0, vertex_count = 0;
};

bool read_primitive(const Bsp &bsp, size_t at, BSPPrimitive &prim) {
  const Bytes data = bsp.lumps[Primitives];
  if (data.data[at] > 1) return false;
  prim = {data.data[at] == 1, data.u16(at + 2), data.u16(at + 4), data.u16(at + 6), data.u16(at + 8)};
  return prim.index_count >= 3 && (prim.strip || prim.index_count % 3 == 0) &&
         bsp.lumps[PrimIndices].records(prim.first_index * 2, prim.index_count, 2) &&
         (!prim.vertex_count || bsp.lumps[PrimVertices].records(prim.first_vertex * 12, prim.vertex_count, 12));
}

bool primitive_triangle(const Bsp &bsp, const BSPPrimitive &prim, size_t first, bool reverse, const FaceTexture &tex,
                        const std::vector<uint32_t> &winding_ids, const std::vector<Vec3> &winding, MeshBuild &build) {
  const Bytes indices = bsp.lumps[PrimIndices], positions = bsp.lumps[PrimVertices];
  std::array<uint32_t, 3> ids;
  std::array<Vec3, 3> points;
  for (size_t i = 0; i < 3; ++i) {
    const size_t j = reverse ? 2 - i : i;
    const uint16_t index = indices.u16((prim.first_index + first + j) * 2);
    if (prim.vertex_count) {
      // Subdivided faces use absolute indices into the primitive vertex lump. They live in a separate ID space.
      if (index < prim.first_vertex || index - prim.first_vertex >= prim.vertex_count) return false;
      ids[i] = 65536u + index;
      points[i] = positions.vec(size_t(index) * 12);
      if (!reasonable(points[i])) return false;
    }
    else {
      if (index >= winding.size()) return false;
      ids[i] = winding_ids[index];
      points[i] = winding[index];
    }
  }
  const Vec3 a = source_position(points[1] - points[0]), b = source_position(points[2] - points[0]);
  if (length_sq(cross(a, b)) <= 1.0e-20f) return true;  // strip connectors and collapsed T-junction triangles
  if (build.mesh->corner_count() > kMaxCorners - 3) return false;
  const uint32_t verts[] = {build.vertex(ids[0], points[0]), build.vertex(ids[1], points[1]), build.vertex(ids[2], points[2])};
  const Vec2 uv[] = {tex.uv(points[0]), tex.uv(points[1]), tex.uv(points[2])};
  build.mesh->add_face(verts, 3, uv, build.slot(tex.material));
  return true;
}

bool append_primitives(const Bsp &bsp, size_t first, size_t count, const FaceTexture &tex,
                       const std::vector<uint32_t> &ids, const std::vector<Vec3> &points, bool reverse, MeshBuild &build) {
  if (!bsp.lumps[Primitives].records(first * 10, count, 10)) return false;
  const size_t before = build.mesh->face_count();
  size_t index_budget = kMaxCorners;
  for (size_t i = 0; i < count; ++i) {
    BSPPrimitive prim;
    if (!read_primitive(bsp, (first + i) * 10, prim) || prim.index_count > index_budget) return false;
    index_budget -= prim.index_count;
    const size_t step = prim.strip ? 1 : 3;
    for (size_t n = 0; n + 2 < prim.index_count; n += step) {
      const bool flip = reverse != (prim.strip && (n & 1));
      if (!primitive_triangle(bsp, prim, n, flip, tex, ids, points, build)) return false;
    }
  }
  if (build.mesh->face_count() == before) ++build.degenerate_faces;
  return true;
}

void displacement_triangle(Mesh &mesh, const std::vector<uint32_t> &vertices, const std::vector<Vec2> &uvs,
                           std::array<size_t, 3> idx, bool reverse, int material) {
  if (reverse) std::swap(idx[1], idx[2]);
  const uint32_t ids[] = {vertices[idx[0]], vertices[idx[1]], vertices[idx[2]]};
  const Vec2 tex[] = {uvs[idx[0]], uvs[idx[1]], uvs[idx[2]]};
  const Vec3 a = mesh.positions[ids[1]] - mesh.positions[ids[0]];
  const Vec3 b = mesh.positions[ids[2]] - mesh.positions[ids[0]];
  if (length_sq(cross(a, b)) <= 1.0e-20f) return;
  mesh.add_face(ids, 3, tex, material);
  mesh.set_face_smooth(mesh.face_count() - 1, true);
}

bool displacement(const Bsp &bsp, uint16_t index, size_t face_index, std::vector<Vec3> points,
                  const FaceTexture &tex, bool reverse, MeshBuild &build) {
  const Bytes info = bsp.lumps[DispInfo], verts = bsp.lumps[DispVertices], tris = bsp.lumps[DispTriangles];
  if (points.size() != 4 || index >= info.size / 176) return false;
  const size_t at = size_t(index) * 176;
  const uint32_t power = info.u32(at + 20);
  if (power < 2 || power > 4 || info.u16(at + 36) != face_index) return false;
  const size_t side = (size_t(1) << power) + 1, cells = side - 1;
  const uint32_t first = info.u32(at + 12), first_tri = info.u32(at + 16);
  if (first > verts.size / 20 || side * side > verts.size / 20 - first ||
      first_tri > tris.size / 2 || 2 * cells * cells > tris.size / 2 - first_tri) return false;
  const Vec3 start = info.vec(at);
  if (!reasonable(start)) return false;
  const auto nearest = std::min_element(points.begin(), points.end(), [start](Vec3 a, Vec3 b) {
    return length_sq(a - start) < length_sq(b - start);
  });
  std::rotate(points.begin(), nearest, points.end());
  std::vector<uint32_t> vertices;
  std::vector<Vec2> uvs;
  for (size_t i = 0; i < side * side; ++i) {
    const float row = float(i / side) / float(cells), col = float(i % side) / float(cells);
    const Vec3 flat = lerp(lerp(points[0], points[1], row), lerp(points[3], points[2], row), col);
    const size_t offset = (size_t(first) + i) * 20;
    const Vec3 position = flat + verts.vec(offset) * verts.number(offset + 12);
    if (!reasonable(position)) return false;
    vertices.push_back(build.mesh->add_vert(source_position(position)));
    uvs.push_back(tex.uv(flat));
  }

  const int slot = build.slot(tex.material);
  for (size_t row = 0; row < cells; ++row) {
    for (size_t col = 0; col < cells; ++col) {
      const size_t a = row * side + col, b = a + side, c = b + 1, d = a + 1;
      const size_t tag = first_tri + 2 * (row * cells + col);
      // Valve builddisp.cpp tessellates the full grid with alternating diagonals. The REMOVE bit marks a hole.
      if (!(tris.u16(tag * 2) & 0x20))
        displacement_triangle(*build.mesh, vertices, uvs, {a, b, (a & 1) ? d : c}, reverse, slot);
      if (!(tris.u16((tag + 1) * 2) & 0x20))
        displacement_triangle(*build.mesh, vertices, uvs, {(a & 1) ? d : a, (a & 1) ? b : c, (a & 1) ? c : d}, reverse, slot);
    }
  }
  return true;
}

bool append_face(const Bsp &bsp, size_t index, MeshBuild &build) {
  const Bytes faces = bsp.faces(), planes = bsp.lumps[Planes];
  const size_t at = index * 56;
  FaceTexture tex;
  if (!face_texture(bsp, faces.u16(at + 10), tex)) return false;
  if (tex.hidden) return true;
  const size_t plane = faces.u16(at);
  if (plane >= planes.size / 20) return false;
  // VBSP writes the oriented plane index. The side byte also marks odd indices.
  const Vec3 normal = source_axes(planes.vec(plane * 20));
  if (!reasonable(normal) || length_sq(normal) < 0.1f) return false;
  const size_t first = faces.u32(at + 4), count = faces.u16(at + 8);
  if (first > bsp.lumps[SurfEdges].size / 4) return false;
  std::vector<uint32_t> ids;
  std::vector<Vec3> points;
  if (!face_points(bsp, first, count, ids, points)) return false;
  const uint16_t disp = faces.u16(at + 12);
  if (disp == 0xffff) {
    const size_t primitive_count = faces.u16(at + 48) & 0x7fff;  // high bit disables dynamic shadows
    if (primitive_count)
      return append_primitives(bsp, faces.u16(at + 50), primitive_count, tex, ids, points,
                               winding_facing(points, normal) < 0, build);
    append_brush_winding(tex, ids, points, normal, build);
    return true;
  }
  const float facing = winding_facing(points, normal);
  if (std::abs(facing) < 1.0e-10f) return false;
  return displacement(bsp, disp, index, std::move(points), tex, facing < 0, build);
}

bool build_brushes(const Bsp &bsp, std::vector<MeshBuild> &brushes, ImportResult &out) {
  const Bytes models = bsp.lumps[Models], faces = bsp.faces();
  if (!models.size || models.size / 48 > 4096 || faces.size / 56 > 65536)
    return out.error = "BSP has no world model or exceeds the classic model or face limits", false;
  brushes.resize(models.size / 48);
  std::vector<uint8_t> used(faces.size / 56, 0);
  size_t corners = 0;
  for (size_t model = 0; model < brushes.size(); ++model) {
    const size_t at = model * 48;
    const uint32_t first = models.u32(at + 40), count = models.u32(at + 44);
    if (first > used.size() || count > used.size() - first)
      return out.error = "BSP brush model " + std::to_string(model) + " has an invalid face range", false;
    MeshBuild &build = brushes[model];
    build.mesh->name = model ? "Brush " + std::to_string(model) : "World";
    for (size_t i = first; i < size_t(first) + count; ++i) {
      if (used[i] || !append_face(bsp, i, build))
        return out.error = "BSP face " + std::to_string(i) + " has invalid geometry or indices", false;
      used[i] = 1;
      if (build.mesh->corner_count() > kMaxCorners - corners)
        return out.error = "BSP geometry exceeds the import size limit", false;
    }
    corners += build.mesh->corner_count();
  }
  // Some compilers put displacement base faces after the model face runs.
  for (size_t i = 0; i < used.size(); ++i) {
    if (used[i] || faces.u16(i * 56 + 12) == 0xffff) continue;
    const size_t before = brushes[0].mesh->corner_count();
    if (!append_face(bsp, i, brushes[0]))
      return out.error = "BSP displacement face " + std::to_string(i) + " has invalid geometry or indices", false;
    corners += brushes[0].mesh->corner_count() - before;
    if (corners > kMaxCorners) return out.error = "BSP geometry exceeds the import size limit", false;
  }
  size_t degenerate = 0;
  for (const MeshBuild &brush : brushes) degenerate += brush.degenerate_faces;
  if (degenerate) out.notes.push_back("Skipped " + std::to_string(degenerate) + " degenerate BSP brush faces");
  return true;
}

struct BrushPlacement {
  size_t model = 0;
  std::string name;
  Vec3 origin, angles;
};

constexpr char kEntityWhitespace[] = " \t\r\n\f\v";

std::string_view trimmed_left(std::string_view text) {
  const size_t start = text.find_first_not_of(kEntityWhitespace);
  return start == std::string_view::npos ? std::string_view() : text.substr(start);
}

bool quoted(std::string_view &text, std::string_view &value) {
  text = trimmed_left(text);
  if (text.empty() || text.front() != '"') return false;
  const size_t end = text.find('"', 1);
  if (end == std::string_view::npos) return false;
  value = text.substr(1, end - 1);
  text.remove_prefix(end + 1);
  return true;
}

bool entity_vec(std::string_view text, Vec3 &value) {
  // Source fills any omitted trailing components with zero.
  value = {};
  text = trimmed_left(text);
  for (int i = 0; i < 3 && !text.empty(); ++i) {
    const std::string_view token = text.substr(0, text.find_first_of(kEntityWhitespace));
    const char *begin = token.data(), *end = begin + token.size();
    if (*begin == '+') {
      ++begin;
      if (begin == end || *begin == '-') return false;
    }
    const auto number = fast_float::from_chars(begin, end, value[i]);
    if (number.ec != std::errc() || number.ptr != end) return false;
    text = trimmed_left(text.substr(token.size()));
  }
  return text.empty() && reasonable(value);
}

bool entity_key(std::string_view key, std::string_view lowercase) {
  return std::equal(key.begin(), key.end(), lowercase.begin(), lowercase.end(),
                    [](char a, char b) { return ascii_lower(a) == b; });
}

bool brush_entity(std::string_view &text, size_t model_count, std::vector<BrushPlacement> &placements) {
  std::string_view model, name, classname, origin, angles;
  while (!(text = trimmed_left(text)).empty() && text.front() != '}') {
    std::string_view key, value;
    if (!quoted(text, key) || !quoted(text, value)) return false;
    if (entity_key(key, "model")) model = value;
    else if (entity_key(key, "targetname")) name = value;
    else if (entity_key(key, "classname")) classname = value;
    else if (entity_key(key, "origin")) origin = value;
    else if (entity_key(key, "angles")) angles = value;
  }
  if (text.empty()) return false;
  text.remove_prefix(1);
  if (model.empty() || model.front() != '*') return true;
  BrushPlacement placement;
  const char *end = model.data() + model.size();
  const auto number = std::from_chars(model.data() + 1, end, placement.model);
  if (number.ec != std::errc() || number.ptr != end || placement.model == 0 || placement.model >= model_count ||
      !entity_vec(origin, placement.origin) || !entity_vec(angles, placement.angles)) return false;
  placement.name = std::string(name.empty() ? classname : name);
  placements.push_back(std::move(placement));
  return placements.size() <= 32768;
}

bool read_brush_placements(Bytes lump, size_t model_count, std::vector<BrushPlacement> &placements) {
  if (!lump.size) return true;
  std::string_view text(reinterpret_cast<const char *>(lump.data), lump.size);
  text = trimmed_left(text.substr(0, text.find('\0')));
  while (!text.empty()) {
    if (text.front() != '{') return false;
    text.remove_prefix(1);
    if (!brush_entity(text, model_count, placements)) return false;
    text = trimmed_left(text);
  }
  return true;
}

struct StaticProp {
  std::string path;
  Vec3 origin, angles;
  uint32_t skin = 0;
};

bool prop_table(Bytes data, size_t &at, uint32_t &count, size_t stride) {
  if (!data.contains(at, 4)) return false;
  count = data.u32(at);
  at += 4;
  return data.records(at, count, stride);
}

bool read_static_props(Bytes data, uint16_t version, std::vector<StaticProp> &props, ImportResult &out) {
  size_t at = 0;
  uint32_t names, leaves, entries;
  if (!prop_table(data, at, names, 128) || names > 65536) return false;
  const size_t names_at = at;
  at += size_t(names) * 128;
  if (!prop_table(data, at, leaves, 2)) return false;
  at += size_t(leaves) * 2;
  if (!prop_table(data, at, entries, 1) || entries > 32768) return false;
  if (!entries) return at == data.size;
  const size_t stride = (data.size - at) / entries;
  if (data.size - at != stride * entries) return false;
  // Branches extend the trailing lighting/fade fields. They keep the transform and model index at the start.
  constexpr size_t strides[] = {56, 60, 64, 68, 68, 72, 72};
  if (stride != strides[version - 4] && !(version == 10 && stride == 76)) {
    out.notes.push_back("BSP static props version " + std::to_string(version) + " has unsupported record size " +
                        std::to_string(stride));
    return true;
  }
  std::vector<std::string> dictionary(names);
  for (size_t i = 0; i < names; ++i)
    dictionary[i] = virtual_name(data.terminated(names_at + i * 128, 128));
  props.reserve(entries);
  for (size_t i = 0; i < entries; ++i, at += stride) {
    const uint16_t model = data.u16(at + 24);
    if (model >= dictionary.size() || dictionary[model].empty()) return false;
    StaticProp p{dictionary[model], data.vec(at), data.vec(at + 12), data.u32(at + 32)};
    if (!reasonable(p.origin) || !reasonable(p.angles)) return false;
    props.push_back(std::move(p));
  }
  return true;
}

bool read_game_lump(const Bsp &bsp, std::vector<StaticProp> &props, ImportResult &out) {
  const Bytes game = bsp.lumps[Game];
  if (!game.size) return true;
  if (!game.contains(0, 4) || !game.records(4, game.u32(0), 16)) return false;
  for (size_t i = 0; i < game.u32(0); ++i) {
    const size_t at = 4 + i * 16;
    if (game.u32(at) != 0x73707270u) continue;  // 'sprp' in Valve's gamebspfile.h
    const uint16_t flags = game.u16(at + 4), version = game.u16(at + 6);
    if (version < 4 || version > 10 || flags != 0) {
      out.notes.push_back("BSP static props use unsupported version or compression (sprp " + std::to_string(version) + ")");
      return true;
    }
    const size_t start = game.u32(at + 8), size = game.u32(at + 12);
    const size_t game_start = bsp.offsets[Game], directory_end = 4 + size_t(game.u32(0)) * 16;
    if (start < game_start + directory_end || !game.contains(start - game_start, size)) return false;
    return read_static_props({bsp.file.data + start, size}, version, props, out);
  }
  return true;
}

MaterialPtr resolve_material(const SourceContent &content, const std::string &path, Materials &cache, ImportResult &out) {
  auto found = cache.try_emplace(path);
  if (!found.second) return found.first->second;
  MaterialPtr material = import_source1_material(content, path, out);
  if (!material) {
    out.notes.push_back("Missing Source material " + path);
    material = make_material(path, {1.0f, 0.0f, 1.0f});
    out.source_material_ids.emplace(material.get(), path + "#missing");
  }
  out.materials.push_back(material);
  found.first->second = material;
  return material;
}

void append_brush(const SourceContent &content, const MeshBuild &build, const std::string &name, Vec3 origin, Vec3 angles,
                  Materials &cache, ImportResult &out) {
  if (!build.mesh->face_count()) return;
  ImportedNode node;
  node.name = name.empty() ? build.mesh->name : name;
  node.mesh = build.mesh;
  node.position = source_position(origin);
  node.rotation = source_angles(angles);
  for (const std::string &path : build.materials)
    node.materials.push_back(resolve_material(content, path, cache, out));
  out.nodes.push_back(std::move(node));
}

void append_brushes(const SourceContent &content, const Bsp &bsp, const std::vector<MeshBuild> &brushes,
                    const std::vector<BrushPlacement> &placements, ImportResult &out) {
  Materials materials;
  append_brush(content, brushes.front(), {}, {}, {}, materials, out);
  std::vector<uint8_t> placed(brushes.size(), 0);
  for (const BrushPlacement &p : placements) {
    append_brush(content, brushes[p.model], p.name, p.origin, p.angles, materials, out);
    placed[p.model] = 1;
  }
  for (size_t i = 1; i < brushes.size(); ++i) {
    const Vec3 origin = bsp.lumps[Models].vec(i * 48 + 24);
    if (!placed[i] && reasonable(origin)) append_brush(content, brushes[i], {}, origin, {}, materials, out);
  }
}

void prop_assets(const ImportResult &model, ImportResult &out) {
  out.materials.insert(out.materials.end(), model.materials.begin(), model.materials.end());
  out.textures.insert(out.textures.end(), model.textures.begin(), model.textures.end());
  out.source_material_ids.insert(model.source_material_ids.begin(), model.source_material_ids.end());
  out.source_texture_ids.insert(model.source_texture_ids.begin(), model.source_texture_ids.end());
  out.written.insert(out.written.end(), model.written.begin(), model.written.end());
  out.notes.insert(out.notes.end(), model.notes.begin(), model.notes.end());
}

void append_prop_nodes(const ImportResult &model, int root, ImportResult &out) {
  const int offset = int(out.nodes.size());
  for (ImportedNode node : model.nodes) {
    node.parent = node.parent < 0 ? root : node.parent + offset;
    node.is_bone = false;
    out.nodes.push_back(std::move(node));
  }
}

bool append_props(const SourceContent &content, const std::vector<StaticProp> &props, ImportResult &out) {
  std::unordered_map<std::string, ImportResult> cache;
  std::unordered_map<std::string, uint32_t> skins;
  MeshPtr missing_mesh;
  MaterialPtr missing_material;
  for (const StaticProp &p : props) {
    if (p.skin && skins.try_emplace(p.path, p.skin).second)
      out.notes.push_back("BSP prop " + p.path + " requests skin " + std::to_string(p.skin) + "; using the default skin");
    auto insertion = cache.try_emplace(p.path);
    ImportResult &model = insertion.first->second;
    if (insertion.second) {
      if (import_source1_model(content, p.path, model)) prop_assets(model, out);
      else {
        out.notes.push_back("Could not import BSP prop " + p.path + ": " + model.error);
        model.nodes.clear();
      }
    }
    if (model.nodes.size() >= 250000 - out.nodes.size())
      return out.error = "BSP instances exceed the import size limit", false;
    const int root = int(out.nodes.size());
    ImportedNode node;
    node.name = fs::stem(p.path);
    node.position = source_position(p.origin);
    node.rotation = source_angles(p.angles);
    if (!model.nodes.empty()) {
      out.nodes.push_back(std::move(node));
      append_prop_nodes(model, root, out);
      continue;
    }
    node.name += " (missing prop)";
    if (!missing_mesh) {
      missing_mesh = primitives::cube(0.3f);
      missing_material = make_material("Missing Source prop", {1.0f, 0.0f, 1.0f});
      out.source_material_ids.emplace(missing_material.get(), virtual_name(out.source) + "#missing-static-prop");
      out.materials.push_back(missing_material);
    }
    node.mesh = missing_mesh;
    node.materials = {missing_material};
    out.nodes.push_back(std::move(node));
  }
  return true;
}

}  // namespace

bool import_source1_map(const SourceContent &content, const std::string &virtual_path, ImportResult &out) {
  ImportResult result;
  result.source = virtual_path;
  std::vector<uint8_t> bytes;
  Bsp bsp;
  if (!content.read(virtual_path, bytes, result.error) || !bsp.open(bytes, result.error)) {
    out = std::move(result);
    return false;
  }
  std::vector<MeshBuild> brushes;
  std::vector<BrushPlacement> placements;
  std::vector<StaticProp> props;
  if (!build_brushes(bsp, brushes, result)) {
    out = std::move(result);
    return false;
  }
  if (!read_brush_placements(bsp.lumps[Entities], brushes.size(), placements))
    result.error = "BSP brush entity data is malformed";
  else if (!read_game_lump(bsp, props, result))
    result.error = "BSP static prop data is malformed";
  if (!result.error.empty()) {
    out = std::move(result);
    return false;
  }

  SourceContent mounted = content;
  const Bytes pak = bsp.lumps[Pak];
  if (pak.size && !mounted.mount_embedded_zip(virtual_path, {pak.data, pak.data + pak.size}, result.error)) {
    result.error = "Could not mount embedded BSP pakfile: " + result.error;
    out = std::move(result);
    return false;
  }
  append_brushes(mounted, bsp, brushes, placements, result);
  if (!append_props(mounted, props, result)) {
    out = {};
    out.error = std::move(result.error);
    return false;
  }
  if (result.nodes.empty()) result.error = "BSP has no visible world, brush models or static props";
  result.summary = std::to_string(result.nodes.size()) + " map objects, " + std::to_string(props.size()) + " static props";
  out = std::move(result);
  return out.error.empty();
}

}  // namespace bl

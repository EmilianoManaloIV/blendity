// SPDX-License-Identifier: GPL-2.0-or-later
#include "test_common.h"
#include "../src/core/json.h"
#include "../src/scene/source_content.h"
#include "../src/scene/unity_package.h"

namespace {

template<class T> void put(std::string &bytes, T value) {
  bytes.append(reinterpret_cast<const char *>(&value), sizeof(value));
}

void zstring(std::string &bytes, const std::string &value) {
  bytes += value;
  bytes += '\0';
}

// Original packed fixtures. KV3 v1 uses separate value lanes followed by strings and type codes.
struct Kv3Fixture {
  std::vector<std::string> strings;
  std::string words, doubles, types;

  void string(const std::string &value) {
    auto found = std::find(strings.begin(), strings.end(), value);
    put(words, uint32_t(found - strings.begin()));
    if (found == strings.end()) strings.push_back(value);
  }

  void value(const Json &data, const std::string *name = nullptr) {
    const uint8_t codes[] = {1, uint8_t(data.b ? 13 : 14), 5, 6, 8, 9};
    put(types, codes[data.type]);
    if (name) string(*name);
    switch (data.type) {
      case Json::Num: put(doubles, data.num); break;
      case Json::Str: string(data.str); break;
      case Json::Arr:
        put(words, uint32_t(data.arr.size()));
        for (const Json &item : data.arr) value(item);
        break;
      case Json::Obj:
        put(words, uint32_t(data.obj.size()));
        for (const auto &entry : data.obj) value(entry.second, &entry.first);
        break;
      default: break;
    }
  }

  std::string build(const std::string &json) {
    JsonParser parser{json.data(), json.data() + json.size()};
    const Json root = parser.value();
    CHECK(parser.ok);
    value(root);
    std::string data;
    put(data, uint32_t(strings.size()));
    data += words;
    data.resize((data.size() + 7) & ~size_t(7));
    data += doubles;
    for (const auto &entry : strings) zstring(data, entry);
    data += types;
    put(data, uint32_t(0xFFEEDD00));

    std::string header;
    put(header, uint32_t(0x4B563301));
    // Valve's generic format GUID 7412167c-06e9-4698-aff2-e63eb59037e7.
    header.append("\x7c\x16\x12\x74\xe9\x06\x98\x46\xaf\xf2\xe6\x3e\xb5\x90\x37\xe7", 16);
    put(header, uint32_t(0)); // Uncompressed.
    put(header, uint32_t(0));
    put(header, uint32_t(1 + words.size() / 4));
    put(header, uint32_t(doubles.size() / 8));
    put(header, uint32_t(data.size()));
    return header + data;
  }
};

std::string resource(const std::vector<std::pair<std::string, std::string>> &blocks, uint16_t version = 1) {
  const size_t table_end = 16 + 12 * blocks.size();
  std::string header, data;
  put(header, uint32_t(0));
  put(header, uint16_t(12));
  put(header, version);
  put(header, uint32_t(8));
  put(header, uint32_t(blocks.size()));
  for (const auto &block : blocks) {
    header += block.first;
    put(header, uint32_t(table_end + data.size() - header.size()));
    put(header, uint32_t(block.second.size()));
    data += block.second;
  }
  const uint32_t size = uint32_t(header.size() + data.size());
  std::memcpy(header.data(), &size, sizeof(size));
  return header + data;
}

std::string reference(const std::string &path) {
  std::string rerl;
  put(rerl, uint32_t(8)); // First reference, at the end of the header.
  put(rerl, uint32_t(1));
  put(rerl, uint64_t(1));
  put(rerl, uint64_t(8)); // Name immediately follows its relative offset.
  zstring(rerl, path);
  return rerl;
}

std::string vpk(const std::map<std::string, std::string> &files) {
  std::map<std::string, std::map<std::string, std::map<std::string, std::string>>> entries;
  for (const auto &file : files) {
    const size_t slash = file.first.rfind('/'), dot = file.first.rfind('.');
    entries[file.first.substr(dot + 1)][file.first.substr(0, slash)][file.first.substr(slash + 1, dot - slash - 1)] = file.second;
  }
  std::string tree, body;
  for (const auto &extension : entries) {
    zstring(tree, extension.first);
    for (const auto &folder : extension.second) {
      zstring(tree, folder.first);
      for (const auto &file : folder.second) {
        zstring(tree, file.first);
        put(tree, crc32(file.second));
        put(tree, uint16_t(0));
        put(tree, uint16_t(0x7FFF)); // Directory archive contains the bytes itself.
        put(tree, uint32_t(body.size()));
        put(tree, uint32_t(file.second.size()));
        put(tree, uint16_t(0xFFFF));
        body += file.second;
      }
      tree += '\0';
    }
    tree += '\0';
  }
  tree += '\0';
  std::string header;
  put(header, uint32_t(0x55AA1234));
  put(header, uint32_t(1));
  put(header, uint32_t(tree.size()));
  return header + tree + body;
}

struct Vertex {
  Vec3 position;
  Vec2 uv;
};

std::string mesh_buffers(const std::vector<Vertex> &vertices, const std::vector<uint16_t> &indices) {
  const uint32_t vertex_bytes = uint32_t(vertices.size()) * 20;
  std::string vbib;
  const uint32_t descriptors[] = {
    16, 1, 32, 1,  // Vertex and index table locations and counts.
    uint32_t(vertices.size()), 20, 40, 2, 144, vertex_bytes,
    uint32_t(indices.size()), 2, 0, 0, 120 + vertex_bytes, uint32_t(indices.size()) * 2
  };
  for (uint32_t word : descriptors)
    put(vbib, word);
  auto attribute = [&](const std::string &name, uint32_t format, uint32_t offset) {
    vbib += name;
    vbib.append(32 - name.size(), '\0');
    for (uint32_t word : {0u, format, offset, 0u, 0u, 0u}) put(vbib, word);
  };
  attribute("POSITION", 6, 0);  // DXGI_FORMAT_R32G32B32_FLOAT.
  attribute("TEXCOORD", 16, 12); // DXGI_FORMAT_R32G32_FLOAT.
  for (const Vertex &vertex : vertices)
    for (float value : {vertex.position.x, vertex.position.y, vertex.position.z, vertex.uv.x, vertex.uv.y})
      put(vbib, value);
  for (uint16_t index : indices) put(vbib, index);
  return vbib;
}

std::string draw(const char *material, int base, int vertices, int start, int indices) {
  return "{\"m_material\":\"materials/fixture/" + std::string(material) + ".vmat\"," +
      "\"m_nBaseVertex\":" + std::to_string(base) + ",\"m_nVertexCount\":" + std::to_string(vertices) +
      ",\"m_nStartIndex\":" + std::to_string(start) + ",\"m_nIndexCount\":" + std::to_string(indices) +
      R"(,"m_nPrimitiveType":5,"m_indexBuffer":{"m_hBuffer":0,"m_nBindOffsetBytes":0},
      "m_vertexBuffers":[{"m_hBuffer":0,"m_nBindOffsetBytes":0}]})";
}

std::string render_mesh(const std::string &draws, const std::vector<Vertex> &vertices,
                        const std::vector<uint16_t> &indices) {
  const auto data = Kv3Fixture{}.build("{\"m_sceneObjects\":[{\"m_drawCalls\":[" + draws + "]}]}");
  return resource({{"DATA", data}, {"VBIB", mesh_buffers(vertices, indices)}});
}

std::string floor_mesh() {
  return render_mesh(draw("floor", 0, 3, 0, 3),
                     {{{0, 0, 0}, {0, 0}}, {{100, 0, 0}, {0.75f, 0.25f}}, {{0, -50, 20}, {0.25f, 0.5f}}},
                     {0, 1, 2});
}

std::string prop_mesh(bool aggregate) {
  const std::vector<Vertex> blade = {
    {{0, 0, 0}, {0, 0}}, {{50, 0, 0}, {0.5f, 0.25f}}, {{0, -25, 10}, {0.25f, 0.75f}}
  };
  if (!aggregate) return render_mesh(draw("blade", 0, 3, 0, 3), blade, {0, 1, 2});

  // Draw 0 is a rectangular base. Draw 1 uses different vertices, indices, UVs and VMAT.
  std::vector<Vertex> vertices = {
    {{0, 0, -10}, {0, 0}}, {{20, 0, -10}, {1, 0}}, {{20, -10, -10}, {1, 1}}, {{0, -10, -10}, {0, 1}}
  };
  vertices.insert(vertices.end(), blade.begin(), blade.end());
  return render_mesh(draw("base", 0, 4, 0, 6) + "," + draw("blade", 4, 3, 6, 3),
                     vertices, {0, 1, 2, 0, 2, 3, 0, 1, 2});
}

std::string material(const char *name, const char *tint, const char *parameters = "") {
  const auto data = Kv3Fixture{}.build("{\"m_materialName\":\"materials/fixture/" + std::string(name) +
      R"(.vmat","m_shaderName":"csgo_complex.vfx","m_vectorParams":[{"m_name":"g_vColorTint","m_value":)" +
      tint + "}]" + parameters + "}");
  return resource({{"DATA", data}});
}

std::string rgba_texture(const std::array<uint8_t, 8> &pixels) {
  std::string data;
  put(data, uint16_t(1)); // Version.
  put(data, uint16_t(0)); // Flags.
  data.append(16, '\0'); // Reflectivity.
  for (uint16_t dimension : {2, 1, 1}) put(data, dimension);
  put(data, uint8_t(4));  // RGBA8888.
  put(data, uint8_t(1));  // One mip.
  data.append(12, '\0'); // Picmip resolution, extra-data offset and count.
  return resource({{"DATA", data}}) + std::string(reinterpret_cast<const char *>(pixels.data()), pixels.size());
}

constexpr char map_entry[] = "maps/picked.vmap_c";
constexpr char world_entry[] = "maps/layout/world.vwrld_c";
constexpr char node_entry[] = "maps/layout/worldnodes/n0.vwnod_c";

std::string regular_node(bool aggregate, bool material_overrides = false) {
  const std::string floor = R"({"m_renderable":"maps/layout/floor.vmesh","m_vTransform":[[1,0,0,-10],[0,1,0,-20],[0,0,1,5]]})";
  const std::string missing = R"({"m_renderableModel":"models/fixture/missing.vmdl","m_vTransform":[[1,0,0,160],[0,1,0,20],[0,0,1,30]]})";
  const std::string first = R"([[1,0,0,25],[0,1,0,10],[0,0,1,0]])";
  const std::string turned = R"([[0,-3,0,100],[2,0,0,-50],[0,0,4,25]])";
  const std::string model = "\"m_renderableModel\":\"models/fixture/prop.vmdl\"";
  const std::string skin = material_overrides ? R"(,"m_skin":"weathered")" : "";
  const std::string tint = material_overrides ? R"(,"m_vTintColor":[0.25,0.5,0.75,0.5])" : "";
  const std::string regular_props = ",{" + model + skin + ",\"m_vTransform\":" + first +
      "},{" + model + tint + ",\"m_vTransform\":" + turned + "}";
  // The untransformed fragment between the two instances consumes no transform from the array.
  const std::string aggregate_props = ",\"m_aggregateSceneObjects\":[{" + model +
      R"(,"m_aggregateMeshes":[{"m_nDrawCallIndex":1,"m_bHasTransform":true},{"m_nDrawCallIndex":0,"m_bHasTransform":false},
      {"m_nDrawCallIndex":1,"m_bHasTransform":true}],"m_fragmentTransforms":[)" +
      first + "," + turned + "]}]";
  const std::string overrides = material_overrides ?
      R"(,"m_materialOverrides":[{"m_nSceneObjectIndex":2,"m_nSubSceneObject":0,"m_nDrawCallIndex":0,
      "m_pMaterial":"materials/fixture/base.vmat"},{"m_nSceneObjectIndex":3,"m_nSubSceneObject":0,
      "m_nDrawCallIndex":4294967295,"m_pMaterial":"materials/fixture/painted.vmat"}])" : "";
  return "{\"m_sceneObjects\":[" + floor + "," + missing +
      (aggregate ? "]" + aggregate_props : regular_props + "]") + overrides + "}";
}

struct ResourceVersions {
  uint16_t map = 1, world = 1, node = 1;
};

struct PropTextures {
  std::array<uint8_t, 8> blade{50, 160, 30, 255, 80, 175, 15, 64};
  std::array<uint8_t, 8> painted{240, 210, 100, 255, 255, 180, 40, 255};
};

std::string packaged_map(bool aggregate, const std::string &node, ResourceVersions versions = {},
                         const std::string &world_properties = {}, const PropTextures &textures = {}) {
  const auto map_data = resource({{"RERL", reference(aggregate ? "maps/layout/world.vrman" : "maps/layout/world.vwrld")},
                                  {"DATA", {}}}, versions.map);
  const auto world_data = Kv3Fixture{}.build(
      R"({"m_worldNodes":[{"m_worldNodePrefix":"maps\\layout\\worldnodes\\n0"}])" + world_properties + "}");
  const auto node_data = Kv3Fixture{}.build(node);
  const auto prop_data = Kv3Fixture{}.build(R"({"m_refMeshes":["models/fixture/prop.vmesh"],"m_refLODGroupMasks":[1]})");
  return vpk({{map_entry, map_data}, {world_entry, resource({{"DATA", world_data}}, versions.world)},
              {node_entry, resource({{"DATA", node_data}}, versions.node)},
              {"maps/layout/floor.vmesh_c", floor_mesh()}, {"models/fixture/prop.vmesh_c", prop_mesh(aggregate)},
              {"models/fixture/prop.vmdl_c", resource({{"DATA", prop_data}})},
              {"materials/fixture/floor.vmat_c", material("floor", "[1,0,0,1]",
                R"(,"m_textureParams":[{"m_name":"g_tColor","m_pValue":"materials/fixture/floor_color.vtex"}])")},
              {"materials/fixture/blade.vmat_c", material("blade", "[0.4,0.8,0.6,1]",
                R"(,"m_intParams":[{"m_name":"F_ALPHA_TEST","m_nValue":1}],
                "m_floatParams":[{"m_name":"g_flAlphaTestReference","m_flValue":0.375},{"m_name":"g_flOpacityScale","m_flValue":0.8}],
                "m_textureParams":[{"m_name":"g_tColor","m_pValue":"materials/fixture/blade/color.vtex"},
                {"m_name":"g_tRoughness","m_pValue":"materials/fixture/missing_roughness.vtex"}])")},
              {"materials/fixture/base.vmat_c", material("base", "[0,0,1,1]",
                R"(,"m_textureParams":[{"m_name":"g_tColor","m_pValue":"materials/fixture/base/color.vtex"}])")},
              {"materials/fixture/painted.vmat_c", material("painted", "[1,1,0,1]",
                R"(,"m_intParams":[{"m_name":"F_ALPHA_TEST","m_nValue":1}],
                "m_floatParams":[{"m_name":"g_flAlphaTestReference","m_flValue":0.625},{"m_name":"g_flOpacityScale","m_flValue":0.8}],
                "m_textureParams":[{"m_name":"g_tColor","m_pValue":"materials/fixture/painted_color.vtex"}])")},
              {"materials/fixture/floor_color.vtex_c", rgba_texture({240, 80, 25, 255, 30, 45, 90, 255})},
              {"materials/fixture/painted_color.vtex_c", rgba_texture(textures.painted)},
              // The distinct aggregate draw materials use color textures with the same basename.
              {"materials/fixture/blade/color.vtex_c", rgba_texture(textures.blade)},
              {"materials/fixture/base/color.vtex_c", rgba_texture({35, 50, 210, 255, 160, 30, 140, 255})}});
}

bool node_world(const ImportResult &result, int index, Mat4 &world) {
  size_t depth = 0;
  for (int i = index; i != -1; i = result.nodes[i].parent) {
    if (i < 0 || size_t(i) >= result.nodes.size() || ++depth > result.nodes.size()) return false;
    const ImportedNode &node = result.nodes[i];
    world = Mat4::trs(node.position, node.rotation, node.scale) * world;
  }
  return true;
}

struct Placement {
  const ImportedNode *node;
  Mat4 world;
};

const Placement *placement_at(const std::vector<Placement> &placements, Vec3 point) {
  auto found = std::find_if(placements.begin(), placements.end(), [&](const Placement &placement) {
    return length(placement.world.translation() - point) < 1e-4f;
  });
  return found == placements.end() ? nullptr : &*found;
}

bool has_corner(const Placement &placement, Vec3 point, Vec2 uv) {
  const Mesh &mesh = *placement.node->mesh;
  if (!mesh.has_uvs()) return false;
  for (size_t i = 0; i < mesh.corner_count(); ++i) {
    uint32_t vertex = mesh.corner_verts[i];
    if (vertex >= mesh.positions.size()) return false;
    if (length(placement.world.point(mesh.positions[vertex]) - point) < 1e-4f &&
        std::fabs(mesh.uvs[i].x - uv.x) < 1e-4f && std::fabs(mesh.uvs[i].y - uv.y) < 1e-4f) return true;
  }
  return false;
}

const MaterialPtr *face_material(const Placement &placement, size_t face) {
  const ImportedNode &node = *placement.node;
  const int slot = node.mesh->material_of(face);
  return slot >= 0 && size_t(slot) < node.materials.size() && node.materials[slot] ? &node.materials[slot] : nullptr;
}

bool material_color(const ImportResult &result, const Placement &placement, Vec3 color) {
  const ImportedNode &node = *placement.node;
  if (!node.mesh->face_count()) return false;
  for (size_t f = 0; f < node.mesh->face_count(); ++f) {
    const MaterialPtr *mat = face_material(placement, f);
    if (!mat || !(length((*mat)->base_color - color) < 1e-4f) ||
        std::find(result.materials.begin(), result.materials.end(), *mat) == result.materials.end()) return false;
  }
  return true;
}

void check_base_map(const ImportResult &result, const Placement &placement, const std::vector<uint8_t> &pixels,
                    size_t face = 0) {
  const MaterialPtr *mat = face_material(placement, face);
  CHECK(mat);
  if (!mat) return;
  const std::string &path = (*mat)->base_map.path;
  CHECK(std::find(result.textures.begin(), result.textures.end(), path) != result.textures.end());
  Bitmap image;
  std::string error;
  CHECK(load_image(path, image, error));
  CHECK(image.width == 2 && image.height == 1 && image.rgba8 == pixels);
}

void check_cutout(const Placement &placement, size_t face, float alpha, float threshold) {
  const MaterialPtr *mat = face_material(placement, face);
  CHECK(mat);
  if (!mat) return;
  CHECK((*mat)->surface == int(MaterialSurface::Cutout));
  CHECK_NEAR((*mat)->alpha, alpha, 1e-4f);
  CHECK_NEAR((*mat)->alpha_clip, threshold, 1e-4f);
}

bool has_note(const ImportResult &result, const char *path, const char *detail = "") {
  return std::any_of(result.notes.begin(), result.notes.end(), [&](const std::string &note) {
    return note.find(path) != std::string::npos && note.find(detail) != std::string::npos;
  });
}

bool read_fixture(const char *name, const std::string &fixture, ImportResult &result) {
  const std::string path = fs::join(test_dir(), std::string(name) + "_source2_map.vpk");
  CHECK(fs::write_file(path, fixture));
  SourceContent content;
  std::string error;
  CHECK(content.mount(path, error));
  return import_source2_map(content, map_entry, result);
}

std::vector<Placement> import_fixture(const char *name, const std::string &fixture, ImportResult &result) {
  const bool imported = read_fixture(name, fixture, result);
  if (!imported) std::printf("  Source 2 map error: %s\n", result.error.c_str());
  CHECK(imported);
  std::vector<Placement> renderables;
  for (int i = 0; i < (int)result.nodes.size(); ++i) {
    const ImportedNode &node = result.nodes[i];
    if (!node.mesh) continue;
    Mat4 world;
    const bool valid = node_world(result, i, world);
    CHECK(valid);
    if (valid) renderables.push_back({&node, world});
  }
  return renderables;
}

void check_floor(const ImportResult &result, const Placement &floor) {
  CHECK(floor.node->mesh->face_count() == 1);
  CHECK(floor.node->mesh->corner_count() == 3);
  // Hammer units are inches. Positions below are in Blendity's left-handed Y-up metre space.
  CHECK(has_corner(floor, {0.508f, 0.127f, -0.254f}, {0, 1}));
  CHECK(has_corner(floor, {0.508f, 0.127f, 2.286f}, {0.75f, 0.75f}));
  CHECK(has_corner(floor, {1.778f, 0.635f, -0.254f}, {0.25f, 0.5f}));
  CHECK(material_color(result, floor, {1, 0, 0}));
  check_base_map(result, floor, {240, 80, 25, 255, 30, 45, 90, 255});
}

void check_prop_pair(const ImportResult &result, const Placement &first, const Placement &turned) {
  CHECK(first.node->mesh == turned.node->mesh);
  CHECK(first.node->mesh->face_count() == 1);
  CHECK(first.node->mesh->corner_count() == 3);
  CHECK(has_corner(first, {-0.254f, 0, 0.635f}, {0, 1}));
  CHECK(has_corner(first, {-0.254f, 0, 1.905f}, {0.5f, 0.75f}));
  CHECK(has_corner(first, {0.381f, 0.254f, 0.635f}, {0.25f, 0.25f}));
  CHECK(has_corner(turned, {1.27f, 0.635f, 2.54f}, {0, 1}));
  CHECK(has_corner(turned, {-1.27f, 0.635f, 2.54f}, {0.5f, 0.75f}));
  CHECK(has_corner(turned, {1.27f, 1.651f, 4.445f}, {0.25f, 0.25f}));
  CHECK(length(turned.world.dir({0, 0, 1}) - Vec3{-2, 0, 0}) < 1e-4f);
  CHECK(length(turned.world.dir({1, 0, 0}) - Vec3{0, 0, 3}) < 1e-4f);
  CHECK(length(turned.world.dir({0, 1, 0}) - Vec3{0, 4, 0}) < 1e-4f);
  CHECK(material_color(result, first, {0.4f, 0.8f, 0.6f}));
  CHECK(material_color(result, turned, {0.4f, 0.8f, 0.6f}));
  check_base_map(result, first, {50, 160, 30, 255, 80, 175, 15, 64});
  check_base_map(result, turned, {50, 160, 30, 255, 80, 175, 15, 64});
  check_cutout(first, 0, 0.8f, 0.375f);
  check_cutout(turned, 0, 0.8f, 0.375f);
}

void check_map(const char *name, bool aggregate) {
  ImportResult result;
  const auto renderables = import_fixture(name, packaged_map(aggregate, regular_node(aggregate)), result);
  CHECK(renderables.size() == (aggregate ? 5 : 4));
  const Placement *floor = placement_at(renderables, {0.508f, 0.127f, -0.254f});
  const Placement *missing = placement_at(renderables, {-0.508f, 0.762f, 4.064f});
  const Placement *first = placement_at(renderables, {-0.254f, 0, 0.635f});
  const Placement *turned = placement_at(renderables, {1.27f, 0.635f, 2.54f});
  CHECK(floor);
  CHECK(missing);
  CHECK(first);
  CHECK(turned);
  if (floor) check_floor(result, *floor);
  if (missing) {
    CHECK(missing->node->name == "Missing: missing" && !missing->node->materials.empty());
    CHECK(missing->node->mesh->face_count() == 6);
    CHECK(missing->node->mesh->positions.size() == 8);
  }
  CHECK(has_note(result, "models/fixture/missing.vmdl_c", "placeholder"));
  CHECK(has_note(result, "materials/fixture/missing_roughness.vtex_c"));
  if (first && turned) check_prop_pair(result, *first, *turned);
  if (!aggregate) return;

  const Placement *base = placement_at(renderables, {0, 0, 0});
  CHECK(base);
  if (!base) return;
  if (first) CHECK(base->node->mesh != first->node->mesh);
  CHECK(base->node->mesh->face_count() == 2);
  CHECK(base->node->mesh->corner_count() == 6);
  CHECK(has_corner(*base, {0, -0.254f, 0}, {0, 1}));
  CHECK(has_corner(*base, {0, -0.254f, 0.508f}, {1, 1}));
  CHECK(has_corner(*base, {0.254f, -0.254f, 0.508f}, {1, 0}));
  CHECK(has_corner(*base, {0.254f, -0.254f, 0}, {0, 0}));
  CHECK(material_color(result, *base, {0, 0, 1}));
  check_base_map(result, *base, {35, 50, 210, 255, 160, 30, 140, 255});
}

void check_scene_tint(const ImportResult &result, const Placement &placement, Vec3 blade_color,
                      float blue, float alpha) {
  const Mesh &mesh = *placement.node->mesh;
  CHECK(mesh.face_count() == 3);
  CHECK(mesh.material_of(0) == mesh.material_of(1));
  CHECK(mesh.material_of(0) != mesh.material_of(2));
  const MaterialPtr *base = face_material(placement, 0);
  const MaterialPtr *blade = face_material(placement, 2);
  CHECK(base);
  CHECK(blade);
  if (!base || !blade) return;
  CHECK(length((*base)->base_color - Vec3{0, 0, blue}) < 1e-4f);
  CHECK(length((*blade)->base_color - blade_color) < 1e-4f);
  CHECK_NEAR((*base)->alpha, alpha, 1e-4f);
  CHECK(std::find(result.materials.begin(), result.materials.end(), *base) != result.materials.end());
  CHECK(std::find(result.materials.begin(), result.materials.end(), *blade) != result.materials.end());
  check_cutout(placement, 2, 0.8f * alpha, 0.375f);
  check_base_map(result, placement, {35, 50, 210, 255, 160, 30, 140, 255}, 0);
  check_base_map(result, placement, {50, 160, 30, 255, 80, 175, 15, 64}, 2);
}

void check_scene_tints() {
  // A two-draw prop checks that tint changes the surface without collapsing its material slots.
  const char *node = R"({"m_sceneObjects":[
    {"m_renderableModel":"models/fixture/prop.vmdl","m_vTintColor":[0.5,0.25,0.75,0.5],
     "m_vTransform":[[1,0,0,0],[0,1,0,0],[0,0,1,0]]},
    {"m_renderableModel":"models/fixture/prop.vmdl","m_vTintColor":[0.02,0.75,0.5,1],
     "m_vTransform":[[1,0,0,10],[0,1,0,0],[0,0,1,0]]},
    {"m_renderableModel":"models/fixture/prop.vmdl","m_vTintColor":[0.9,0.9,0.9,0],
     "m_vTransform":[[1,0,0,20],[0,1,0,0],[0,0,1,0]]}]})";
  ImportResult result;
  const auto renderables = import_fixture("scene_tints", packaged_map(true, node), result);
  CHECK(renderables.size() == 3);
  const Placement *first = placement_at(renderables, {0, 0, 0});
  const Placement *second = placement_at(renderables, {0, 0, 0.254f});
  const Placement *neutral = placement_at(renderables, {0, 0, 0.508f});
  CHECK(first);
  CHECK(second);
  CHECK(neutral);
  if (!first || !second || !neutral) return;
  CHECK(first->node->mesh == second->node->mesh);
  CHECK(second->node->mesh == neutral->node->mesh);
  // Apply the standard sRGB transfer function to the placement RGB before multiplying the linear VMAT color.
  // The second placement's red channel is below the sRGB transfer function's 0.04045 breakpoint.
  check_scene_tint(result, *first, {0.085616456f, 0.040700871f, 0.313512932f}, 0.522521554f, 0.5f);
  check_scene_tint(result, *second, {0.000619195f, 0.418017243f, 0.128424684f}, 0.214041140f, 1.0f);
  check_scene_tint(result, *neutral, {0.4f, 0.8f, 0.6f}, 1.0f, 1.0f);
}

void check_aggregate_lods() {
  const char *node = R"({"m_sceneObjects":[],"m_aggregateSceneObjects":[
    {"m_renderableModel":"models/fixture/prop.vmdl","m_lodSetups":[{}],
     "m_aggregateMeshes":[
       {"m_nDrawCallIndex":1,"m_bHasTransform":true,"m_nLODSetupIndex":0,"m_nLODGroupMask":2},
       {"m_nDrawCallIndex":1,"m_bHasTransform":true,"m_nLODSetupIndex":0,"m_nLODGroupMask":1,"m_vTintColor":[64,128,255]},
       {"m_nDrawCallIndex":0,"m_bHasTransform":false,"m_nLODSetupIndex":0,"m_nLODGroupMask":0},
       {"m_nDrawCallIndex":0,"m_bHasTransform":true,"m_nLODSetupIndex":-1,"m_nLODGroupMask":2}],
     "m_fragmentTransforms":[
       [[1,0,0,-800],[0,1,0,0],[0,0,1,0]],
       [[1,0,0,10],[0,1,0,0],[0,0,1,0]],
       [[1,0,0,20],[0,1,0,0],[0,0,1,0]]]}]})";
  ImportResult result;
  const auto renderables = import_fixture("aggregate_lods", packaged_map(true, node), result);
  CHECK(renderables.size() == 3);
  CHECK(!placement_at(renderables, {0, 0, -20.32f}));
  const Placement *lod0 = placement_at(renderables, {0, 0, 0.254f});
  const Placement *zero_mask = placement_at(renderables, {0, 0, 0});
  const Placement *ungoverned = placement_at(renderables, {0, 0, 0.508f});
  CHECK(lod0);
  CHECK(zero_mask);
  CHECK(ungoverned);
  if (!lod0 || !zero_mask || !ungoverned) return;

  CHECK(lod0->node->mesh->face_count() == 1);
  CHECK(zero_mask->node->mesh->face_count() == 2);
  CHECK(zero_mask->node->mesh == ungoverned->node->mesh);
  // The transfer function receives the normalized 64/255, 128/255 and 255/255 byte channels.
  CHECK(material_color(result, *lod0, {0.020507783f, 0.172688400f, 0.6f}));
  CHECK(material_color(result, *zero_mask, {0, 0, 1}));
  CHECK(material_color(result, *ungoverned, {0, 0, 1}));
  check_cutout(*lod0, 0, 0.8f, 0.375f);
  check_base_map(result, *lod0, {50, 160, 30, 255, 80, 175, 15, 64});
}

std::string identity_node(bool reordered) {
  auto prop = [](int x, const char *tint) {
    return R"({"m_renderableModel":"models/fixture/prop.vmdl","m_vTintColor":)" + std::string(tint) +
        R"(,"m_vTransform":[[1,0,0,)" + std::to_string(x) + "],[0,1,0,0],[0,0,1,0]]}";
  };
  std::vector<std::string> objects = {
    R"({"m_renderable":"maps/layout/floor.vmesh","m_vTransform":[[1,0,0,0],[0,1,0,0],[0,0,1,0]]})",
    prop(10, "[1,1,1,1]"), prop(20, "[0.5,1,1,1]"), prop(30, "[0.5,1,1,0.5]"),
    prop(40, "[1,1,1,1]"), prop(50, "[0.5,1,1,1]")
  };
  if (reordered) std::reverse(objects.begin(), objects.end());
  std::string node = "{\"m_sceneObjects\":[" + objects[0];
  for (size_t i = 1; i < objects.size(); ++i) node += "," + objects[i];
  const std::string material = reordered ? "materials/fixture/painted.vmat_c" : "Materials/Fixture/Painted.VMAT";
  auto override_for = [&](int index) {
    return "{\"m_nSceneObjectIndex\":" + std::to_string(index) +
        ",\"m_nDrawCallIndex\":4294967295,\"m_pMaterial\":\"" + material + "\"}";
  };
  return node + "],\"m_materialOverrides\":[" + override_for(reordered ? 1 : 4) + "," +
      override_for(reordered ? 0 : 5) + "]}";
}

struct PlacementSourceIds {
  std::string material, texture;
};

PlacementSourceIds placement_ids(const ImportResult &result, const std::vector<Placement> &renderables, float z,
                                 const std::string &cache, const std::array<uint8_t, 8> &pixels) {
  const Placement *placement = placement_at(renderables, {0, 0, z});
  CHECK(placement);
  if (!placement) return {};
  const MaterialPtr *material = face_material(*placement, 0);
  CHECK(material);
  if (!material) return {};
  const std::string &file = (*material)->base_map.path;
  CHECK(fs::normalize(file).find(fs::normalize(cache) + "/") == 0);
  check_base_map(result, *placement, std::vector<uint8_t>(pixels.begin(), pixels.end()));
  const auto material_id = result.source_material_ids.find(material->get());
  const auto texture_id = result.source_texture_ids.find(file);
  CHECK(material_id != result.source_material_ids.end());
  CHECK(texture_id != result.source_texture_ids.end());
  return {material_id == result.source_material_ids.end() ? "" : material_id->second,
          texture_id == result.source_texture_ids.end() ? "" : texture_id->second};
}

void check_stable_source_ids() {
  struct RestoreCache {
    std::string previous;
    RestoreCache() {
      if (const char *path = std::getenv("BLENDITY_CACHE_DIR")) previous = path;
    }
    ~RestoreCache() { set_env("BLENDITY_CACHE_DIR", previous); }
  } restore;
  const PropTextures first_colors;
  const PropTextures second_colors{{30, 2, 190, 255, 45, 6, 200, 64}, {37, 57, 97, 255, 46, 66, 106, 255}};
  const std::array<uint8_t, 8> floor_color{240, 80, 25, 255, 30, 45, 90, 255};
  const std::string first_cache = fs::join(test_dir(), "map_identity_first_cache");
  const std::string second_cache = fs::join(test_dir(), "map_identity_second_cache");
  ImportResult first, second;
  set_env("BLENDITY_CACHE_DIR", first_cache);
  const auto before = import_fixture("map_identity_first", packaged_map(false, identity_node(false)), first);
  set_env("BLENDITY_CACHE_DIR", second_cache);
  const auto after = import_fixture("map_identity_second",
      packaged_map(false, identity_node(true), {}, {}, second_colors), second);
  CHECK(before.size() == 6);
  CHECK(after.size() == 6);

  const char *floor_material = "materials/fixture/floor.vmat_c";
  const char *blade_material = "materials/fixture/blade.vmat_c";
  const char *painted_material = "materials/fixture/painted.vmat_c";
  const char *floor_texture = "materials/fixture/floor_color.vtex_c";
  const char *blade_texture = "materials/fixture/blade/color.vtex_c";
  const char *painted_texture = "materials/fixture/painted_color.vtex_c";
  const struct {
    float z;
    const char *material, *texture;
    const std::array<uint8_t, 8> &before, &after;
  } placements[] = {
    {0.f, floor_material, floor_texture, floor_color, floor_color},
    {0.254f, blade_material, blade_texture, first_colors.blade, second_colors.blade},
    {0.508f, blade_material, blade_texture, first_colors.blade, second_colors.blade},
    {0.762f, blade_material, blade_texture, first_colors.blade, second_colors.blade},
    {1.016f, painted_material, painted_texture, first_colors.painted, second_colors.painted},
    {1.270f, painted_material, painted_texture, first_colors.painted, second_colors.painted},
  };
  std::set<std::string> material_ids;
  std::map<std::string, std::string> texture_ids;
  for (const auto &placement : placements) {
    const auto a = placement_ids(first, before, placement.z, first_cache, placement.before);
    const auto b = placement_ids(second, after, placement.z, second_cache, placement.after);
    CHECK(a.material.find(placement.material) != std::string::npos);
    CHECK(a.texture.find(placement.texture) != std::string::npos);
    CHECK(a.material == b.material);
    CHECK(a.texture == b.texture);
    CHECK(material_ids.insert(a.material).second);
    // All tints of a source keep the texture's identity, even though they have distinct material identities.
    CHECK(texture_ids.try_emplace(placement.texture, a.texture).first->second == a.texture);
  }
}

}  // namespace

void test_source2_map() {
  test("Alyx-style packed world places shared rotated props and marks a missing prop", [] { check_map("alyx", false); });
  test("CS2-style packed world selects each aggregate draw and shares its repeated rotated prop", [] { check_map("cs2", true); });
  test("Source 2 map tints the whole-model replacement on one prop and reports unsupported overrides", [] {
    ImportResult result;
    const auto renderables = import_fixture("placement_materials", packaged_map(false, regular_node(false, true)), result);
    CHECK(renderables.size() == 4);
    const Placement *original = placement_at(renderables, {-0.254f, 0, 0.635f});
    const Placement *painted = placement_at(renderables, {1.27f, 0.635f, 2.54f});
    CHECK(original);
    CHECK(painted);
    CHECK(has_note(result, "models/fixture/prop.vmdl", "weathered"));
    CHECK(has_note(result, "materials/fixture/base.vmat", "unsupported per-draw"));
    if (!original || !painted) return;
    CHECK(original->node->mesh == painted->node->mesh);
    CHECK(material_color(result, *original, {0.4f, 0.8f, 0.6f}));
    CHECK(material_color(result, *painted, {0.050876088f, 0.214041140f, 0}));
    check_cutout(*original, 0, 0.8f, 0.375f);
    check_cutout(*painted, 0, 0.4f, 0.625f);
    check_base_map(result, *original, {50, 160, 30, 255, 80, 175, 15, 64});
    check_base_map(result, *painted, {240, 210, 100, 255, 255, 180, 40, 255});
  });
  test("Source 2 scene prop tints keep both native draw materials and share a mesh", check_scene_tints);
  test("CS2 aggregate keeps LOD zero and uses byte tint and the correct placement transforms", check_aggregate_lods);
  test("Source 2 map reports omitted baked lighting textures by path", [] {
    ImportResult result;
    const auto map = packaged_map(false, regular_node(false), {},
        R"(,"m_worldLightingInfo":{"m_lightMaps":["maps/layout/lightmaps/irradiance.vtex","maps/layout/lightmaps/direction.vtex_c"]})");
    const auto renderables = import_fixture("omitted_lighting", map, result);
    CHECK(renderables.size() == 4);
    CHECK(has_note(result, "maps/layout/lightmaps/irradiance.vtex_c", "lightmap omitted"));
    CHECK(has_note(result, "maps/layout/lightmaps/direction.vtex_c", "lightmap omitted"));
  });
  test("Source 2 map reports the unsupported version of each compiled resource", [] {
    const auto node = regular_node(false);
    const struct { const char *name, *path, *diagnostic; ResourceVersions versions; } cases[] = {
      {"unsupported_map", map_entry, "version 71", {71, 1, 1}},
      {"unsupported_world", world_entry, "version 72", {1, 72, 1}},
      {"unsupported_node", node_entry, "version 73", {1, 1, 73}},
    };
    for (const auto &fixture : cases) {
      ImportResult result;
      CHECK(!read_fixture(fixture.name, packaged_map(false, node, fixture.versions), result));
      CHECK(result.error.find(fixture.path) != std::string::npos);
      CHECK(result.error.find(fixture.diagnostic) != std::string::npos);
    }
  });
  test("Source 2 map keeps source identities across cache moves and packed map changes", check_stable_source_ids);
}

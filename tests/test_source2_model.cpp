// SPDX-License-Identifier: GPL-2.0-or-later
#include "test_common.h"
#include "../src/scene/source2_model.h"
#include "../src/scene/source2_material.h"
#include "../src/scene/source2_resource.h"
#include "../src/scene/source_content.h"

namespace {

template<class T> void put(std::string &bytes, T value) {
  bytes.append(reinterpret_cast<const char *>(&value), sizeof(T));
}

template<class T> void set(std::string &bytes, size_t offset, T value) {
  std::memcpy(&bytes[offset], &value, sizeof(T));
}

struct Kv3Fixture {
  std::string one, four, eight, types, strings;
  std::vector<std::string> string_table;
  // These placeholders let a JSON-authored fixture write exact KV3 UINT64 wire values.
  std::vector<std::pair<std::string, uint64_t>> unsigned_values;
  uint32_t objects = 0;
  std::string object_lengths;

  void intern(const std::string &s) {
    auto it = std::find(string_table.begin(), string_table.end(), s);
    put(four, uint32_t(it - string_table.begin()));
    if (it == string_table.end()) {
      string_table.push_back(s);
      strings.append(s);
      strings.push_back('\0');
    }
  }

  void add(const Json &value, bool v5) {
    switch (value.type) {
      case Json::Null: types.push_back(1); break;
      case Json::Bool: types.push_back(value.b ? 13 : 14); break;
      case Json::Num: types.push_back(5); put(eight, value.num); break;
      case Json::Str: {
        auto integer = std::find_if(unsigned_values.begin(), unsigned_values.end(), [&](const auto &entry) {
          return entry.first == value.str;
        });
        if (integer == unsigned_values.end()) {
          types.push_back(6);
          intern(value.str);
        }
        else {
          types.push_back(4);
          put(eight, integer->second);
        }
        break;
      }
      case Json::Arr:
        types.push_back(8);
        put(four, uint32_t(value.arr.size()));
        for (const auto &item : value.arr) add(item, v5);
        break;
      case Json::Obj:
        types.push_back(9);
        ++objects;
        put(v5 ? object_lengths : four, uint32_t(value.obj.size()));
        for (const auto &item : value.obj) {
          intern(item.first);
          add(item.second, v5);
        }
        break;
    }
  }

  static void align(std::string &bytes, size_t a) { bytes.resize((bytes.size() + a - 1) & ~(a - 1)); }

  static std::string lz4_literal(const std::string &plain) {
    std::string out(1, char(std::min<size_t>(plain.size(), 15) << 4));
    if (plain.size() >= 15) {
      size_t remaining = plain.size() - 15;
      out.append(remaining / 255, char(255));
      out.push_back(char(remaining % 255));
    }
    out += plain;
    return out;
  }

  std::string encode(std::string_view json, bool v5) {
    JsonParser parser{json.data(), json.data() + json.size()};
    Json root = parser.value();
    CHECK(parser.ok);
    add(root, v5);
    std::string out;
    put(out, uint32_t(v5 ? 0x4b563305 : 0x4b563301));
    out.resize(20);  // unused format GUID in this fixture
    put(out, uint32_t(v5 ? 1 : 0));
    std::string one_count;
    put(one_count, uint32_t(string_table.size()));
    if (!v5) {
      std::string plain = one_count + four;
      align(plain, 8);
      plain += eight + strings + types;
      put(plain, uint32_t(0xffeedd00));
      put(out, uint32_t(0));
      put(out, uint32_t(1 + four.size() / 4));
      put(out, uint32_t(eight.size() / 8));
      put(out, uint32_t(plain.size()));
      return out + plain;
    }
    std::string aux = strings;
    align(aux, 4);
    aux += one_count;
    std::string values = object_lengths + four;
    if (!eight.empty()) {
      align(values, 8);
      values += eight;
    }
    values += types;
    put(values, uint32_t(0xffeedd00));
    const std::string pack_aux = lz4_literal(aux), pack_values = lz4_literal(values);
    put(out, uint16_t(0));
    put(out, uint16_t(16384));
    put(out, uint32_t(strings.size()));
    put(out, uint32_t(1));
    put(out, uint32_t(0));
    put(out, uint32_t(types.size()));
    put(out, objects);
    put(out, uint32_t(aux.size() + values.size()));
    put(out, uint32_t(pack_aux.size() + pack_values.size()));
    out.resize(out.size() + 16);  // no blobs, two-byte lane or frame lengths
    put(out, uint32_t(aux.size()));
    put(out, uint32_t(pack_aux.size()));
    put(out, uint32_t(values.size()));
    put(out, uint32_t(pack_values.size()));
    put(out, uint32_t(0));
    put(out, uint32_t(0));
    put(out, uint32_t(four.size() / 4));
    put(out, uint32_t(eight.size() / 8));
    put(out, uint32_t(0));
    put(out, objects);
    out.resize(out.size() + 8);
    return out + pack_aux + pack_values;
  }
};

std::string kv3(std::string_view json, bool v5 = false) {
  return Kv3Fixture().encode(json, v5);
}

std::string resource(std::initializer_list<std::pair<std::string, std::string>> blocks, const std::string &tail = {}) {
  std::string out;
  put(out, uint32_t(0));
  put(out, uint16_t(12));
  put(out, uint16_t(1));
  put(out, uint32_t(8));
  put(out, uint32_t(blocks.size()));
  out.resize(16 + blocks.size() * 12);
  size_t row = 16;
  for (const auto &block : blocks) {
    out.replace(row, 4, block.first);
    set(out, row + 4, uint32_t(out.size() - (row + 4)));
    set(out, row + 8, uint32_t(block.second.size()));
    out += block.second;
    row += 12;
  }
  set(out, 0, uint32_t(out.size()));
  return out + tail;
}

std::string fixture_vbib() {
  const size_t vertices = 16, indices = vertices + 24, attributes = indices + 24, data = attributes + 4 * 56;
  std::string vbib(data, '\0');
  set(vbib, 0, uint32_t(vertices));
  set(vbib, 4, uint32_t(1));
  set(vbib, 8, uint32_t(indices - 8));
  set(vbib, 12, uint32_t(1));
  set(vbib, vertices, uint32_t(3));
  set(vbib, vertices + 4, uint32_t(28));
  set(vbib, vertices + 8, uint32_t(attributes - vertices - 8));
  set(vbib, vertices + 12, uint32_t(4));
  set(vbib, vertices + 16, uint32_t(data - vertices - 16));
  set(vbib, vertices + 20, uint32_t(84));
  set(vbib, indices, uint32_t(3));
  set(vbib, indices + 4, uint32_t(2));
  set(vbib, indices + 16, uint32_t(data + 84 - indices - 16));
  set(vbib, indices + 20, uint32_t(6));
  auto attr = [&](size_t i, const char *name, uint32_t format, uint32_t offset) {
    size_t p = attributes + i * 56;
    vbib.replace(p, std::strlen(name), name);
    set(vbib, p + 36, format);
    set(vbib, p + 40, offset);
  };
  attr(0, "POSITION", 6, 0);
  attr(1, "TEXCOORD", 16, 12);
  attr(2, "BLENDINDICES", 30, 20);
  attr(3, "BLENDWEIGHT", 28, 24);
  for (int i = 0; i < 3; ++i) {
    put(vbib, i == 1 ? 10.0f : 0.0f);
    put(vbib, i == 2 ? 10.0f : 0.0f);
    put(vbib, 0.0f);
    put(vbib, i == 1 ? 1.0f : 0.0f);
    put(vbib, i == 2 ? 1.0f : 0.0f);
    put(vbib, uint32_t(0x00000100));
    put(vbib, i == 2 ? uint32_t(0xff) : uint32_t(0x807f));
  }
  for (uint16_t i = 0; i < 3; ++i) put(vbib, i);
  return vbib;
}

std::string draws(bool two) {
  std::string base = R"({"m_nPrimitiveType":5,"m_nBaseVertex":0,"m_nStartIndex":0,"m_nVertexCount":3,"m_nIndexCount":3,"m_vertexBuffers":[{"m_hBuffer":0}],"m_indexBuffer":{"m_hBuffer":0},"m_material":"materials/a.vmat"})";
  std::string both = base;
  if (two) {
    base.replace(base.find("a.vmat"), 6, "b.vmat");
    both += "," + base;
  }
  return R"({"m_skeleton":{"m_nBoneWeightCount":2},"m_sceneObjects":[{"m_drawCalls":[)" + both + "]}]}";
}

std::string mat(const char *folder, bool cutout, bool channels = false) {
  std::string data = R"({"m_intParams":[{"m_name":"F_ALPHA_TEST","m_nValue":)";
  data += cutout ? "1" : "0";
  data += "}";
  if (channels) data += R"(,{"m_name":"F_SELF_ILLUM","m_nValue":1})";
  data += R"(],"m_floatParams":[{"m_name":"g_flAlphaTestReference","m_flValue":0.42}],"m_textureParams":[{"m_name":"g_tColor","m_pValue":")";
  data += std::string(folder) + R"(/color.vtex"})";
  if (channels) {
    for (const char *name : {"g_tNormal", "g_tRoughness", "g_tMetalness", "g_tSelfIllumMask"})
      data += std::string(R"(,{"m_name":")") + name + R"(","m_pValue":")" + folder + R"(/channels.vtex"})";
  }
  data += "]}";
  return resource({{"DATA", kv3(data, true)}});
}

std::string vtex(unsigned format, uint8_t red, const std::string &compiler_algorithm = {}) {
  std::string head(40, '\0');
  set(head, 0, uint16_t(1));
  set(head, 20, uint16_t(4));
  set(head, 22, uint16_t(4));
  set(head, 24, uint16_t(1));
  set(head, 26, uint8_t(format));
  set(head, 27, uint8_t(1));
  std::string pixels;
  if (format == 1) {
    // BC1 three-color mode. The first texel is transparent and the rest use endpoint zero.
    put(pixels, uint16_t(0x07e0));
    put(pixels, uint16_t(0xf800));
    put(pixels, uint32_t(3));
  }
  else if (format == 20) {
    // BC7 mode 6, identical endpoints (200, 100, 50, 30) and index zero.
    pixels.resize(16);
    size_t offset = 0;
    auto bits = [&](int n, unsigned value) {
      for (int i = 0; i < n; ++i, ++offset)
        pixels[offset / 8] |= char(((value >> i) & 1) << (offset % 8));
    };
    bits(7, 64);
    for (unsigned endpoint : {100u, 50u, 25u, 15u}) {
      bits(7, endpoint);
      bits(7, endpoint);
    }
  }
  else {
    for (int i = 0; i < 16; ++i) {
      put(pixels, red);
      put(pixels, uint8_t(17));
      put(pixels, uint8_t(33));
      put(pixels, uint8_t(i ? 255 : 0));
    }
  }
  if (format == 20) {
    // A compiled VTEX's mip size pointer can point beyond its declared 12-byte descriptor.
    head.resize(68);
    set(head, 32, uint32_t(8));
    set(head, 36, uint32_t(1));
    set(head, 40, uint32_t(4));
    set(head, 44, uint32_t(8));
    set(head, 48, uint32_t(12));
    set(head, 52, uint32_t(0));
    set(head, 56, uint32_t(8));
    set(head, 60, uint32_t(1));
    set(head, 64, uint32_t(pixels.size()));
  }
  if (!compiler_algorithm.empty()) return resource({{"REDI", compiler_algorithm}, {"DATA", head}}, pixels);
  return resource({{"DATA", head}}, pixels);
}

std::string model(bool modern, bool two_materials = false) {
  std::string control = R"({"embedded_meshes":[{"name":"body","mesh_index":0,"data_block":1,"vbib_block":2}]})";
  std::string data = R"({"m_modelSkeleton":{"m_boneName":["root","elbow"],"m_nParent":[-1,0],"m_bonePosParent":[[0,0,0],[0,0,10]],"m_boneRotParent":[[0,0,0,1],[0,0,0,1]]},"m_remappingTableStarts":[0],"m_remappingTable":[1,0]})";
  std::string buffers = fixture_vbib();
  if (!modern)
    return resource({{"CTRL", kv3(control)}, {"MDAT", kv3(draws(two_materials))}, {"MBUF", std::move(buffers)}, {"DATA", kv3(data)}});
  control = R"({"embedded_meshes":[{"m_Name":"body","m_nMeshIndex":0,"m_nDataBlock":1,"m_vertexBuffers":[{"m_nBlockIndex":2,"m_nElementCount":3,"m_nElementSizeInBytes":28,"m_inputLayoutFields":[{"m_pSemanticName":"POSITION","m_Format":6,"m_nOffset":0},{"m_pSemanticName":"TEXCOORD","m_Format":16,"m_nOffset":12},{"m_pSemanticName":"BLENDINDICES","m_Format":30,"m_nOffset":20},{"m_pSemanticName":"BLENDWEIGHT","m_Format":28,"m_nOffset":24}]}],"m_indexBuffers":[{"m_nBlockIndex":3,"m_nElementCount":3,"m_nElementSizeInBytes":2}]}]})";
  return resource({{"CTRL", kv3(control, true)}, {"MDAT", kv3(draws(two_materials), true)}, {"MVTX", buffers.substr(buffers.size() - 90, 84)},
                   {"MIDX", buffers.substr(buffers.size() - 6)}, {"DATA", kv3(data, true)}});
}

void write_fixture(const std::string &root, const std::string &path, const std::string &data) {
  std::string abs = fs::join(root, path);
  fs::make_dirs(fs::parent(abs));
  CHECK(fs::write_file(abs, data));
}

struct ScratchSource {
  std::string root;
  std::string prior_cache;
  ScratchSource(const std::string &name) : root(fs::join(test_dir(), name)) {
    if (const char *override_dir = std::getenv("BLENDITY_CACHE_DIR")) prior_cache = override_dir;
    set_env("BLENDITY_CACHE_DIR", fs::join(root, "cache"));
  }
  ~ScratchSource() { set_env("BLENDITY_CACHE_DIR", prior_cache); }
};

}  // namespace

void test_source2_model() {
  test("Source 2 character: compiled v1 and v5 models keep bind transforms, weighted joints, cutout and UVs", [] {
    ScratchSource scratch("source2-character");
    write_fixture(scratch.root, "models/older.vmdl_c", model(false));
    write_fixture(scratch.root, "models/newer.vmdl_c", model(true));
    write_fixture(scratch.root, "materials/a.vmat_c", mat("textures/a", true, true));
    write_fixture(scratch.root, "textures/a/color.vtex_c", vtex(1, 0));
    write_fixture(scratch.root, "textures/a/channels.vtex_c", vtex(4, 80));
    SourceContent content;
    std::string error;
    CHECK(content.mount(scratch.root, error));
    for (const char *path : {"models/older.vmdl_c", "models/newer.vmdl_c"}) {
      ImportResult result;
      const bool ok = import_source2_model(content, path, result);
      if (!ok) std::printf("%s\n", result.error.c_str());
      CHECK(ok && result.notes.empty() && result.nodes.size() == 3 && result.skins.size() == 1);
      if (!ok || result.nodes.size() != 3 || result.skins.empty()) continue;
      CHECK(result.nodes[1].name == "elbow" && result.nodes[1].parent == 0 && result.nodes[1].has_bind);
      CHECK_NEAR(result.nodes[1].bind_world.point({}).y, 0.254f, 1e-5);
      const Mesh &mesh = *result.nodes[2].mesh;
      CHECK(mesh.face_count() == 1 && mesh.vert_count() == 3 && mesh.uvs.size() == 3);
      if (mesh.vert_count() != 3 || mesh.uvs.size() != 3) continue;
      CHECK_NEAR(mesh.positions[0].x, -0.254f, 1e-5);
      CHECK_NEAR(mesh.positions[1].z, 0.254f, 1e-5);
      // Read the UV through each face corner so a permutation of the positions or UVs cannot pass independently.
      for (size_t corner = 0; corner < 3; ++corner) {
        const Vec3 position = mesh.positions[mesh.corner_verts[corner]];
        CHECK_NEAR(mesh.uvs[corner].x, position.z / 0.254f, 1e-5);
        CHECK_NEAR(mesh.uvs[corner].y, 1.0f + position.x / 0.254f, 1e-5);
      }
      CHECK(mesh.vertex_groups.size() == 2);
      if (mesh.vertex_groups.size() == 2) {
        CHECK(mesh.vertex_groups[0].name == "elbow");
        CHECK_NEAR(mesh.vertex_groups[0].weights[0], 1.0f, 1e-5);
        CHECK_NEAR(mesh.vertex_groups[0].weights[1], 127.0f / 255.0f, 1e-5);
      }
      CHECK(result.nodes[2].materials.size() == 1);
      if (result.nodes[2].materials.size() != 1) continue;
      const auto &material = result.nodes[2].materials[0];
      CHECK(material->surface == int(MaterialSurface::Cutout));
      CHECK_NEAR(material->alpha_clip, 0.42f, 1e-5);
      CHECK(material->metallic == 1 && material->roughness == 1 && material->emission_strength == 1);
      for (const TextureRef *ref : {&material->normal_map, &material->metallic_map, &material->roughness_map, &material->emission_map})
        CHECK(!ref->empty() && ref->non_color && fs::exists(ref->path));
      Bitmap bitmap;
      CHECK(load_image(material->base_map.path, bitmap, error));
      CHECK(bitmap.width == 4 && bitmap.height == 4 && bitmap.rgba8.size() == 64);
      if (bitmap.rgba8.size() == 64) {
        CHECK(bitmap.rgba8[3] == 0);
        CHECK(bitmap.rgba8[5] == 255 && bitmap.rgba8[7] == 255);
      }
      Scene scene;
      std::vector<GameObject *> made;
      CHECK(instantiate_import(scene, result, "Model", nullptr, &made) != nullptr);
      CHECK(made.size() == 3 && made[1] && made[2]);
      if (made.size() != 3 || !made[1] || !made[2]) continue;
      auto rest = made[2]->evaluated_mesh();
      CHECK(rest && rest->positions.size() == 3);
      if (!rest || rest->positions.size() != 3) continue;
      const auto before = rest->positions;
      for (size_t vertex = 0; vertex < before.size(); ++vertex)
        CHECK_NEAR(length(before[vertex] - mesh.positions[vertex]), 0, 1e-5);
      Vec3 moved = made[1]->local().position;
      moved.y += 0.1f;
      made[1]->set_local_position(moved);
      auto bent = made[2]->evaluated_mesh();
      CHECK(bent && bent->positions.size() == 3);
      if (bent && bent->positions.size() == 3) {
        CHECK_NEAR(length(bent->positions[0] - before[0] - Vec3(0, 0.1f, 0)), 0, 1e-5);
        CHECK_NEAR(length(bent->positions[1] - before[1] - Vec3(0, 0.1f * 127 / 255, 0)), 0, 1e-5);
        CHECK_NEAR(length(bent->positions[2] - before[2] - Vec3(0, 0.1f * 127 / 255, 0)), 0, 1e-5);
      }
    }
  });

  test("Source 2 prop: face material slots and standalone draw calls distinguish texture paths sharing a basename", [] {
    ScratchSource scratch("source2-prop");
    write_fixture(scratch.root, "prop.vmesh_c", resource({{"DATA", kv3(draws(true))}, {"VBIB", fixture_vbib()}}));
    write_fixture(scratch.root, "materials/a.vmat_c", mat("textures/a", true));
    write_fixture(scratch.root, "materials/b.vmat_c", mat("textures/b", false));
    write_fixture(scratch.root, "textures/a/color.vtex_c", vtex(4, 80));
    write_fixture(scratch.root, "textures/b/color.vtex_c", vtex(20, 0));
    SourceContent content;
    std::string error;
    CHECK(content.mount(scratch.root, error));
    ImportResult all, first, second;
    CHECK(import_source2_renderable(content, "prop.vmesh_c", -1, all));
    CHECK(import_source2_renderable(content, "prop.vmesh_c", 0, first));
    CHECK(import_source2_renderable(content, "prop.vmesh_c", 1, second));
    CHECK(all.nodes.size() == 1 && all.skins.empty());
    if (all.nodes.size() != 1 || first.nodes.size() != 1 || second.nodes.size() != 1) return;
    CHECK(all.nodes[0].mesh->face_material == std::vector<int32_t>({0, 1}));
    CHECK(all.nodes[0].mesh->vertex_groups.empty() && !all.nodes[0].is_bone);
    CHECK(first.nodes[0].mesh->face_count() == 1 && second.nodes[0].mesh->face_count() == 1);
    CHECK(all.nodes[0].materials.size() == 2 && !first.nodes[0].materials.empty() && !second.nodes[0].materials.empty());
    if (first.nodes[0].materials.empty() || second.nodes[0].materials.empty()) return;
    const std::string a = first.nodes[0].materials[0]->base_map.path;
    const std::string b = second.nodes[0].materials[0]->base_map.path;
    CHECK(!a.empty() && !b.empty() && a != b);
    Bitmap left, right;
    CHECK(load_image(a, left, error) && load_image(b, right, error));
    CHECK(left.rgba8.size() == 64 && right.rgba8.size() == 64);
    if (left.rgba8.size() == 64 && right.rgba8.size() == 64) {
      CHECK(left.rgba8[0] == 80 && left.rgba8[3] == 0);
      CHECK(right.rgba8[0] == 200 && right.rgba8[1] == 100 && right.rgba8[2] == 50 && right.rgba8[3] == 30);
    }
    ImportResult absent;
    CHECK(!import_source2_renderable(content, "prop.vmesh_c", 2, absent));
    CHECK(!absent.error.empty() && absent.nodes.empty());
  });

  test("Source 2 character: two face slots retain distinct editable materials and texture channels", [] {
    ScratchSource scratch("source2-character-materials");
    const std::string assets = fs::join(scratch.root, "assets");
    const std::string dependencies = fs::join(scratch.root, "dependencies");
    write_fixture(assets, "models/two_materials.vmdl_c", model(true, true));
    write_fixture(dependencies, "materials/a.vmat_c", resource({{"DATA", kv3(R"({
      "m_shaderName":"csgo_character.vfx",
      "m_intParams":[{"m_name":"F_ALPHA_TEST","m_nValue":1},{"m_name":"F_SELF_ILLUM","m_nValue":1}],
      "m_floatParams":[{"m_name":"g_flSelfIllumScale","m_flValue":0.25},{"m_name":"g_flOpacityScale","m_flValue":0.8}],
      "m_vectorParams":[{"m_name":"g_vColorTint","m_value":[0.3,0.6,0.9,1]},
                        {"m_name":"g_vTexCoordScale","m_value":[2,3,1,1]},
                        {"m_name":"g_vTexCoordCenter","m_value":[0.25,0.25,0,0]},
                        {"m_name":"g_vTexCoordOffset","m_value":[0.125,0.25,0,0]}],
      "m_textureParams":[{"m_name":"g_tColor","m_pValue":"textures/color.vtex"},
                         {"m_name":"g_tNormal","m_pValue":"textures/normal.vtex"},
                         {"m_name":"g_tMetalness","m_pValue":"textures/metal.vtex"},
                         {"m_name":"g_tRoughness","m_pValue":"textures/rough.vtex"},
                         {"m_name":"g_tSelfIllumMask","m_pValue":"textures/emission.vtex"}]})", true)}}));
    write_fixture(dependencies, "materials/b.vmat_c", mat("textures/b", false));
    write_fixture(dependencies, "textures/color.vtex_c", vtex(4, 34));
    write_fixture(dependencies, "textures/normal.vtex_c", vtex(4, 56));
    write_fixture(dependencies, "textures/metal.vtex_c", vtex(4, 78));
    write_fixture(dependencies, "textures/rough.vtex_c", vtex(4, 90));
    write_fixture(dependencies, "textures/emission.vtex_c", vtex(4, 123));
    write_fixture(dependencies, "textures/b/color.vtex_c", vtex(4, 145));
    SourceContent content;
    std::string error;
    CHECK(content.mount(assets, error));
    CHECK(content.mount(dependencies, error));
    ImportResult result;
    CHECK(import_source2_model(content, "models/two_materials.vmdl_c", result));
    CHECK(result.notes.empty() && result.nodes.size() == 3);
    if (result.nodes.size() != 3) return;
    const auto &node = result.nodes[2];
    CHECK(node.mesh && node.mesh->face_material == std::vector<int32_t>({0, 1}));
    CHECK(node.materials.size() == 2 && result.textures.size() == 6);
    if (node.materials.size() != 2) return;
    const auto &material = node.materials[0];
    CHECK(material->surface == int(MaterialSurface::Cutout));
    CHECK_NEAR(material->alpha, 0.8, 1e-5);
    CHECK_NEAR(material->emission_strength, 0.25, 1e-5);
    CHECK_NEAR(length(material->base_color - Vec3(0.3f, 0.6f, 0.9f)), 0, 1e-5);
    CHECK_NEAR(length(material->tiling - Vec3(2, 3, 1)), 0, 1e-5);
    CHECK_NEAR(length(material->offset - Vec3(-0.125f, -1.75f, 0)), 0, 1e-5);

    auto check_pixels = [&](const TextureRef &ref, bool data, int red, int green, int alpha, int blue = 33) {
      CHECK(!ref.empty() && ref.non_color == data);
      Bitmap image;
      CHECK(load_image(ref.path, image, error));
      CHECK(image.width == 4 && image.height == 4 && image.rgba8.size() == 64);
      if (image.rgba8.size() != 64) return;
      CHECK(image.rgba8[0] == red && image.rgba8[1] == green && image.rgba8[2] == blue);
      // Base color uses alpha for cutout. The native PBR data channels sample RGB.
      if (!data) CHECK(image.rgba8[3] == alpha);
    };
    check_pixels(material->base_map, false, 34, 17, 0);
    check_pixels(material->normal_map, true, 56, 238, 255);
    // csgo_character samples green. The converted scalar map makes that channel available as native red.
    check_pixels(material->metallic_map, true, 17, 17, 255, 17);
    check_pixels(material->roughness_map, true, 90, 17, 0);
    check_pixels(material->emission_map, true, 123, 17, 0);
    check_pixels(node.materials[1]->base_map, false, 145, 17, 0);

    Scene scene;
    std::vector<GameObject *> made;
    CHECK(instantiate_import(scene, result, "Model", nullptr, &made) != nullptr);
    CHECK(made.size() == 3 && made[2] && made[2]->mesh_renderer());
    if (made.size() != 3 || !made[2] || !made[2]->mesh_renderer()) return;
    auto *renderer = made[2]->mesh_renderer();
    CHECK(renderer->materials.size() == 2 && renderer->material(0) != renderer->material(1));
    renderer->main_material().roughness = 0.37f;
    CHECK_NEAR(renderer->material(0)->roughness, 0.37, 1e-5);
    CHECK_NEAR(renderer->material(1)->roughness, 0.5, 1e-5);
  });

  test("Source 2 prop: compiled models honor full-width default and per-mesh visibility masks", [] {
    ScratchSource scratch("source2-mesh-group-masks");
    const uint64_t high = uint64_t(1) << 63;
    Kv3Fixture all, selective;
    all.unsigned_values = {{"all bits", UINT64_MAX}};
    selective.unsigned_values = {{"high bit", high}, {"low bit", 1}, {"hide", high >> 1}, {"high and low", high | 1}};
    const std::string mesh = resource({{"DATA", kv3(draws(false))}, {"VBIB", fixture_vbib()}});
    for (const char *name : {"all", "high", "low", "hidden"})
      write_fixture(scratch.root, std::string("models/") + name + ".vmesh_c", mesh);
    write_fixture(scratch.root, "materials/a.vmat_c", resource({{"DATA", kv3("{}")}}));
    write_fixture(scratch.root, "models/all.vmdl_c", resource({{"DATA", all.encode(R"({
      "m_refMeshes":["models/all.vmesh"],"m_refMeshGroupMasks":["all bits"],"m_nDefaultMeshGroupMask":"all bits"})", true)}}));
    write_fixture(scratch.root, "models/selective.vmdl_c", resource({{"DATA", selective.encode(R"({
      "m_refMeshes":["models/high.vmesh","models/low.vmesh","models/hidden.vmesh"],
      "m_refMeshGroupMasks":["high bit","low bit","hide"],"m_nDefaultMeshGroupMask":"high and low"})", true)}}));
    SourceContent content;
    std::string error;
    CHECK(content.mount(scratch.root, error));
    ImportResult a, b;
    const bool all_imported = import_source2_model(content, "models/all.vmdl_c", a);
    if (!all_imported) std::printf("%s\n", a.error.c_str());
    CHECK(all_imported && a.notes.empty() && a.nodes.size() == 1 && a.nodes[0].mesh);
    const bool selective_imported = import_source2_model(content, "models/selective.vmdl_c", b);
    if (!selective_imported) std::printf("%s\n", b.error.c_str());
    CHECK(selective_imported && b.notes.empty() && b.nodes.size() == 2);
    if (b.nodes.size() == 2)
      CHECK(b.nodes[0].name == "high" && b.nodes[1].name == "low" && b.skins.empty());
  });

  test("Source 2 reimport: material and texture identities follow virtual paths through reorder and replacement", [] {
    auto read = [](const ScratchSource &scratch, bool updated, ImportResult &result) {
      std::string mesh = draws(true);
      mesh.replace(mesh.find("materials/a.vmat"), 16, updated ? "materials/b/shared.vmat_c" : "Materials/A/Shared.VMAT");
      mesh.replace(mesh.find("materials/b.vmat"), 16, updated ? "materials/a/shared.vmat" : "materials/b/shared.vmat");
      write_fixture(scratch.root, "prop.vmesh_c", resource({{"DATA", kv3(mesh)}, {"VBIB", fixture_vbib()}}));
      const char *mat_a = R"({"m_intParams":[{"m_name":"F_METALNESS_TEXTURE","m_nValue":1},{"m_name":"F_SELF_ILLUM","m_nValue":1}],
        "m_textureParams":[{"m_name":"g_tColor","m_pValue":"Textures/A/Same.VTEX"},{"m_name":"g_tSelfIllumMask","m_pValue":"textures/a/same.vtex_c"},
        {"m_name":"g_tNormal","m_pValue":"textures/common/normal.vtex"}]})";
      const char *mat_b = R"({"m_shaderName":"csgo_complex.vfx","m_textureParams":[{"m_name":"g_tColor","m_pValue":"textures/b/same.vtex"},
        {"m_name":"g_tNormal","m_pValue":"textures/common/normal.vtex"}]})";
      write_fixture(scratch.root, "materials/a/shared.vmat_c", resource({{"DATA", kv3(mat_a, true)}}));
      write_fixture(scratch.root, "materials/b/shared.vmat_c", resource({{"DATA", kv3(mat_b, true)}}));
      write_fixture(scratch.root, "textures/a/same.vtex_c", vtex(4, updated ? 160 : 80));
      write_fixture(scratch.root, "textures/b/same.vtex_c", vtex(4, 80));
      write_fixture(scratch.root, "textures/common/normal.vtex_c", vtex(4, 91, "HemiOctIsoRoughness_RG_B"));
      SourceContent content;
      std::string error;
      return content.mount(scratch.root, error) && import_source2_renderable(content, "prop.vmesh_c", -1, result);
    };
    auto material_id = [](const ImportResult &result, const Material *material) {
      auto found = result.source_material_ids.find(material);
      return found == result.source_material_ids.end() ? std::string() : found->second;
    };
    auto texture_id = [](const ImportResult &result, const TextureRef &ref) {
      auto found = result.source_texture_ids.find(ref.path);
      return found == result.source_texture_ids.end() ? std::string() : found->second;
    };
    ImportResult before, after;
    {
      ScratchSource scratch("source2-identity-before");
      CHECK(read(scratch, false, before));
    }
    {
      ScratchSource scratch("source2-identity-after");
      CHECK(read(scratch, true, after));
    }
    CHECK(before.nodes.size() == 1 && after.nodes.size() == 1);
    if (before.nodes.size() != 1 || after.nodes.size() != 1) return;
    const auto &old_slots = before.nodes[0].materials;
    const auto &new_slots = after.nodes[0].materials;
    CHECK(old_slots.size() == 2 && new_slots.size() == 2);
    if (old_slots.size() != 2 || new_slots.size() != 2) return;
    CHECK(old_slots[0]->name == old_slots[1]->name && new_slots[0]->name == new_slots[1]->name);
    const std::string a = "materials/a/shared.vmat_c", b = "materials/b/shared.vmat_c";
    CHECK(material_id(before, old_slots[0].get()) == a && material_id(before, old_slots[1].get()) == b);
    CHECK(material_id(after, new_slots[0].get()) == b && material_id(after, new_slots[1].get()) == a);
    CHECK(before.nodes[0].mesh->face_material == std::vector<int32_t>({0, 1}));
    CHECK(after.nodes[0].mesh->face_material == std::vector<int32_t>({0, 1}));
    CHECK(old_slots[0]->base_map.path != old_slots[1]->base_map.path);
    CHECK(old_slots[0]->base_map.path != new_slots[1]->base_map.path);
    CHECK(old_slots[1]->base_map.path != new_slots[0]->base_map.path);
    for (const ImportResult *r : {&before, &after}) {
      const auto &slots = r->nodes[0].materials;
      bool swapped = r == &after;
      const auto &left = *slots[swapped ? 1 : 0], &right = *slots[swapped ? 0 : 1];
      CHECK(texture_id(*r, left.base_map) == "textures/a/same.vtex_c#base-color");
      CHECK(texture_id(*r, right.base_map) == "textures/b/same.vtex_c#base-color");
      CHECK(texture_id(*r, left.emission_map) == "textures/a/same.vtex_c#data");
      CHECK(texture_id(*r, left.metallic_map) == "textures/a/same.vtex_c#metalness-alpha");
      CHECK(texture_id(*r, left.normal_map) == "textures/common/normal.vtex_c#normal");
      CHECK(texture_id(*r, right.normal_map) == "textures/common/normal.vtex_c#normal-flip-y");
      CHECK(texture_id(*r, left.roughness_map) == "textures/common/normal.vtex_c#roughness-blue");
      CHECK(left.roughness_map.path == right.roughness_map.path);
      CHECK(r->source_texture_ids.size() == 7 && r->textures.size() == 7);
    }
    auto check_red = [](const TextureRef &ref, uint8_t expected) {
      Bitmap bitmap;
      std::string error;
      CHECK(load_image(ref.path, bitmap, error));
      CHECK(!bitmap.rgba8.empty() && bitmap.rgba8[0] == expected);
    };
    check_red(old_slots[0]->base_map, 80);
    check_red(new_slots[1]->base_map, 160);
  });

  test("Source 2 materials: character metalness uses green and R8 keeps only red", [] {
    ScratchSource scratch("source2-material-channels");
    const char *character = R"({"m_shaderName":"csgo_character.vfx","m_intParams":[{"m_name":"F_ANISOTROPIC_GLOSS","m_nValue":1}],
      "m_textureParams":[{"m_name":"g_tMetalness","m_pValue":"textures/metal.vtex"},{"m_name":"g_tColor","m_pValue":"textures/red.vtex"},
      {"m_name":"g_tAnisoGloss","m_pValue":"textures/directional.vtex"}]})";
    const char *other = R"({"m_textureParams":[{"m_name":"g_tMetalness","m_pValue":"textures/metal.vtex"},
      {"m_name":"g_tColor","m_pValue":"textures/intensity.vtex"}]})";
    write_fixture(scratch.root, "materials/character.vmat_c", resource({{"DATA", kv3(character)}}));
    write_fixture(scratch.root, "materials/other.vmat_c", resource({{"DATA", kv3(other)}}));
    write_fixture(scratch.root, "textures/metal.vtex_c", vtex(4, 80));
    auto single_channel = [](uint8_t format) {
      std::string header(40, '\0');
      set(header, 0, uint16_t(1));
      set(header, 20, uint16_t(4));
      set(header, 22, uint16_t(4));
      set(header, 24, uint16_t(1));
      set(header, 26, format);
      set(header, 27, uint8_t(1));
      return resource({{"DATA", header}}, std::string(16, char(91)));
    };
    write_fixture(scratch.root, "textures/red.vtex_c", single_channel(33));
    write_fixture(scratch.root, "textures/intensity.vtex_c", single_channel(3));
    SourceContent content;
    std::string error;
    CHECK(content.mount(scratch.root, error));
    ImportResult result;
    result.source = "material_challenges.vmdl_c";
    auto material = source2::read_material(content, "materials/character.vmat_c", result);
    auto scalar = source2::read_material(content, "materials/other.vmat_c", result);
    auto pixel = [&](const TextureRef &ref, std::array<uint8_t, 4> expected) {
      Bitmap bitmap;
      CHECK(load_image(ref.path, bitmap, error));
      CHECK(bitmap.rgba8.size() >= 4 && std::equal(expected.begin(), expected.end(), bitmap.rgba8.begin()));
    };
    pixel(material->metallic_map, {17, 17, 17, 255});
    pixel(scalar->metallic_map, {80, 17, 33, 255});
    pixel(material->base_map, {91, 0, 0, 255});
    pixel(scalar->base_map, {91, 91, 91, 255});
    auto metal = result.source_texture_ids.find(material->metallic_map.path);
    CHECK(metal != result.source_texture_ids.end() && metal->second == "textures/metal.vtex_c#metalness-green");
    CHECK(result.notes.size() == 1 && result.notes[0].find("F_ANISOTROPIC_GLOSS") != std::string::npos &&
          result.notes[0].find("textures/directional.vtex_c") != std::string::npos);
  });

  test("Source 2 materials: each shader reads its authored alpha, texture, or scalar metalness", [] {
    ScratchSource scratch("source2-missing-retro-metalness");
    const struct Sample {
      const char *name, *shader;
      bool texture_mode, retro, translucent, separate_map, uses_alpha, reports_missing;
    } samples[] = {
        {"alyx-simple", "vr_simple.vfx", true, false, false, false, true, false},
        {"alyx-foliage", "vr_xen_foliage.vfx", true, false, true, false, true, false},
        {"cs2-simple", "csgo_simple.vfx", true, false, false, false, true, false},
        {"generic", "workshop_custom.vfx", true, false, false, false, true, false},
        {"alyx-complex", "vr_complex.vfx", true, false, false, true, true, false},
        {"alyx-retro", "vr_complex.vfx", true, true, false, true, false, true},
        {"alyx-retro-no-reference", "vr_complex.vfx", true, true, false, false, false, false},
        {"cs2-character", "csgo_character.vfx", true, false, false, false, false, false},
        {"alyx-simple-scalar", "vr_simple.vfx", false, false, false, true, false, false},
    };
    for (const Sample &sample : samples) {
      JsonWriter material;
      material.obj().key("m_shaderName").str(sample.shader).key("m_intParams").arr();
      for (auto feature : {std::make_pair("F_METALNESS_TEXTURE", sample.texture_mode),
                           std::make_pair("F_RETRO_REFLECTIVE", sample.retro),
                           std::make_pair("F_TRANSLUCENT", sample.translucent)})
        material.obj().key("m_name").str(feature.first).key("m_nValue").num(double(feature.second)).end();
      material.end().key("m_floatParams").arr().obj().key("m_name").str("g_flMetalness").key("m_flValue").num(0.31).end().end();
      material.key("m_textureParams").arr().obj().key("m_name").str("g_tColor").key("m_pValue").str("textures/body.vtex").end();
      if (sample.separate_map)
        material.obj().key("m_name").str("g_tMetalness").key("m_pValue").str("textures/missing.vtex").end();
      material.end().end();
      write_fixture(scratch.root, std::string("materials/") + sample.name + ".vmat_c", resource({{"DATA", kv3(material.out)}}));
    }
    write_fixture(scratch.root, "textures/body.vtex_c", vtex(4, 80));
    SourceContent content;
    std::string error;
    CHECK(content.mount(scratch.root, error));
    for (const Sample &sample : samples) {
      ImportResult result;
      result.source = std::string("models/") + sample.name + ".vmdl_c";
      auto imported = source2::read_material(content, std::string("materials/") + sample.name + ".vmat_c", result);
      Bitmap bitmap;
      CHECK(load_image(imported->base_map.path, bitmap, error));
      CHECK(bitmap.rgba8.size() == 64 && bitmap.rgba8[3] == 0 && bitmap.rgba8[7] == 255);
      CHECK(imported->metallic_map.empty() != sample.uses_alpha);
      CHECK_NEAR(imported->metallic, sample.uses_alpha ? 1.0 : 0.31, 1e-5);
      CHECK(result.textures.size() == (sample.uses_alpha ? 2 : 1));
      bool reports_missing = std::any_of(result.notes.begin(), result.notes.end(), [](const std::string &note) {
        return note.find("textures/missing.vtex_c") != std::string::npos;
      });
      CHECK(reports_missing == sample.reports_missing);
      if (sample.uses_alpha) {
        Bitmap metallic;
        CHECK(load_image(imported->metallic_map.path, metallic, error));
        CHECK(metallic.rgba8.size() == 64 && metallic.rgba8[0] == 0 && metallic.rgba8[4] == 255);
      }
    }
  });

  test("Source 2 textures: validate embedded PNG and JPEG before staging", [] {
    ScratchSource scratch("source2-embedded-image-validation");
    uint32_t pixels[16];
    std::fill(std::begin(pixels), std::end(pixels), uint32_t(0xff663322));
    std::string png = encode_png(pixels, 4, 4, 4, true), jpeg = encode_jpeg(pixels, 4, 4, 4);
    std::string bad_crc = png, oversized = png, truncated_jpeg = jpeg;
    CHECK(!bad_crc.empty() && oversized.size() >= 24 && truncated_jpeg.size() >= 2);
    if (bad_crc.empty() || oversized.size() < 24 || truncated_jpeg.size() < 2) return;
    bad_crc.back() ^= 1;
    oversized[16] = 0x7f;
    truncated_jpeg.resize(truncated_jpeg.size() - 2);
    struct Sample { const char *name; uint8_t format; int width; std::string bytes; bool valid; };
    const Sample samples[] = {{"png", 16, 4, png, true}, {"jpeg", 17, 4, jpeg, true}, {"empty", 16, 4, {}, false},
                              {"crc", 16, 4, bad_crc, false}, {"size", 16, 5, png, false},
                              {"huge", 16, 4, oversized, false}, {"truncated", 15, 4, truncated_jpeg, false}};
    for (const Sample &sample : samples) {
      std::string header(40, '\0');
      set(header, 0, uint16_t(1));
      set(header, 20, uint16_t(sample.width));
      set(header, 22, uint16_t(4));
      set(header, 24, uint16_t(1));
      set(header, 26, sample.format);
      set(header, 27, uint8_t(1));
      const std::string stem = sample.name;
      write_fixture(scratch.root, stem + ".vtex_c", resource({{"DATA", header}}, sample.bytes));
      std::string shader = R"({"m_textureParams":[{"m_name":"g_tColor","m_pValue":")" + stem + R"(.vtex"}]})";
      write_fixture(scratch.root, stem + ".vmat_c", resource({{"DATA", kv3(shader)}}));
    }
    SourceContent content;
    std::string error;
    CHECK(content.mount(scratch.root, error));
    for (const Sample &sample : samples) {
      const std::string stem = sample.name;
      ImportResult result;
      result.source = stem + ".vmdl_c";
      auto material = source2::read_material(content, stem + ".vmat_c", result);
      CHECK(!material->base_map.empty() == sample.valid);
      CHECK(result.source_texture_ids.size() == size_t(sample.valid));
      if (!sample.valid) {
        CHECK(result.textures.empty());
        CHECK(result.notes.size() == 1 && result.notes[0].find(stem + ".vtex_c") != std::string::npos);
      }
    }
  });

  test("Source 2 import: repeated KV3 strings and mesh streams exhaust aggregate budgets before staging", [] {
    const std::string repeated(4096, 'x');
    for (bool keys : {false, true}) {
      std::string json = keys ? "{" : "[";
      for (int i = 0; i < 200; ++i) {
        if (i) json += ',';
        json += '"' + repeated + '"';
        if (keys) json += ":0";
      }
      json += keys ? '}' : ']';
      auto bytes = kv3(json, true);
      Json decoded;
      std::string error;
      CHECK(!source2::read_kv3({reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size()}, decoded, error));
      CHECK(decoded.type == Json::Null && error.find("expansion exceeds its budget") != std::string::npos);
    }
    ScratchSource scratch("source2-aggregate-budgets");
    const char *streams = R"({"m_vertexBuffers":[{"m_nElementCount":16000000,"m_nElementSizeInBytes":12,"m_nBlockIndex":1,"m_bMeshoptCompressed":true},
      {"m_nElementCount":16000000,"m_nElementSizeInBytes":12,"m_nBlockIndex":1,"m_bMeshoptCompressed":true}],"m_indexBuffers":[]})";
    write_fixture(scratch.root, "buffers.vmesh_c", resource({{"DATA", kv3(streams)}, {"MVTX", "x"}}));
    const std::string draw = R"({"m_nPrimitiveType":5,"m_nVertexCount":3,"m_nStartIndex":0,"m_nIndexCount":6000,
      "m_vertexBuffers":[{"m_hBuffer":0}],"m_indexBuffer":{"m_hBuffer":0}})";
    std::string scene = R"({"m_sceneObjects":[{"m_drawCalls":[)";
    for (int i = 0; i < 1001; ++i) { if (i) scene += ','; scene += draw; }
    scene += "]}]}";
    std::string buffers = fixture_vbib();
    set(buffers, 40, uint32_t(6000));
    set(buffers, 60, uint32_t(12000));
    buffers.append(12000 - 6, '\0');
    write_fixture(scratch.root, "draws.vmesh_c", resource({{"DATA", kv3(scene)}, {"VBIB", buffers}}));
    SourceContent content;
    std::string error;
    CHECK(content.mount(scratch.root, error));
    ImportResult result;
    CHECK(!import_source2_renderable(content, "buffers.vmesh_c", -1, result));
    CHECK(result.nodes.empty() && result.error.find("decoded mesh buffer budget") != std::string::npos);
    CHECK(!import_source2_renderable(content, "draws.vmesh_c", -1, result));
    CHECK(result.nodes.empty() && result.error.find("output geometry budget") != std::string::npos);
  });

  test("Source 2 import: missing or unsupported dependencies name their path and newer resource versions are refused", [] {
    ScratchSource scratch("source2-diagnostics");
    write_fixture(scratch.root, "prop.vmesh_c", resource({{"DATA", kv3(draws(false))}, {"VBIB", fixture_vbib()}}));
    std::string unsupported = R"({"m_intParams":[{"m_name":"F_PARALLAX","m_nValue":1}]})";
    std::string alternate = draws(false);
    alternate.replace(alternate.find("a.vmat"), 6, "b.vmat");
    write_fixture(scratch.root, "prop_shader.vmesh_c", resource({{"DATA", kv3(alternate)}, {"VBIB", fixture_vbib()}}));
    write_fixture(scratch.root, "materials/b.vmat_c", resource({{"DATA", kv3(unsupported, true)}}));
    std::string bad = kv3("{}");
    set(bad, 0, uint32_t(0x4b563306));
    write_fixture(scratch.root, "bad.vmdl_c", resource({{"DATA", bad}}));
    std::string wrong_header = model(false), wrong_model = wrong_header;
    set(wrong_header, 4, uint16_t(13));
    set(wrong_model, 6, uint16_t(99));
    write_fixture(scratch.root, "bad_header.vmdl_c", wrong_header);
    write_fixture(scratch.root, "bad_model.vmdl_c", wrong_model);
    auto textured_draw = draws(false);
    textured_draw.replace(textured_draw.find("a.vmat"), 6, "c.vmat");
    write_fixture(scratch.root, "prop_texture.vmesh_c", resource({{"DATA", kv3(textured_draw)}, {"VBIB", fixture_vbib()}}));
    write_fixture(scratch.root, "materials/c.vmat_c", resource({{"DATA", kv3(R"({"m_textureParams":[
      {"m_name":"g_tColor","m_pValue":"textures/missing.vtex"},{"m_name":"g_tNormal","m_pValue":"textures/bad.vtex"}]})")}}));
    write_fixture(scratch.root, "textures/bad.vtex_c", vtex(255, 0));
    SourceContent content;
    std::string error;
    CHECK(content.mount(scratch.root, error));
    ImportResult good, rejected;
    CHECK(import_source2_renderable(content, "prop.vmesh_c", -1, good));
    CHECK(good.notes.size() == 1 && good.notes[0].find("materials/a.vmat_c") != std::string::npos);
    auto missing_material = good.materials.empty() ? good.source_material_ids.end() : good.source_material_ids.find(good.materials[0].get());
    CHECK(missing_material != good.source_material_ids.end() && missing_material->second == "materials/a.vmat_c");
    ImportResult substituted;
    CHECK(import_source2_renderable(content, "prop_shader.vmesh_c", -1, substituted));
    CHECK(substituted.notes.size() == 1 && substituted.notes[0].find("materials/b.vmat_c: unsupported") != std::string::npos &&
          substituted.notes[0].find("F_PARALLAX") != std::string::npos);
    CHECK(!import_source2_model(content, "bad.vmdl_c", rejected));
    CHECK(rejected.nodes.empty() && rejected.error.find("version 6") != std::string::npos);
    ImportResult header, model_version, textures;
    CHECK(!import_source2_model(content, "bad_header.vmdl_c", header));
    CHECK(header.nodes.empty() && header.error.find("header version 13") != std::string::npos);
    CHECK(!import_source2_model(content, "bad_model.vmdl_c", model_version));
    CHECK(model_version.nodes.empty() && model_version.error.find("version 99") != std::string::npos);
    CHECK(import_source2_renderable(content, "prop_texture.vmesh_c", -1, textures));
    CHECK(textures.nodes.size() == 1 && textures.textures.empty());
    CHECK(textures.notes.size() == 2);
    if (textures.notes.size() == 2) {
      CHECK(textures.notes[0].find("textures/missing.vtex_c") != std::string::npos);
      CHECK(textures.notes[1].find("textures/bad.vtex_c: unsupported") != std::string::npos);
    }
  });
}

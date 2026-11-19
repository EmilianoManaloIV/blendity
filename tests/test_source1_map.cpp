// SPDX-License-Identifier: GPL-2.0-or-later
#include "test_common.h"
#include "../src/scene/source1_map.h"
#include "../src/scene/source_content.h"

namespace {

// Synthetic BSP records assembled here from the Valve SDK layout. No game assets are used.
void put32(std::string &bytes, size_t at, uint32_t value) {
  for (size_t i = 0; i < 4; ++i) bytes[at + i] = char(value >> (i * 8));
}

uint32_t lump_start(const std::string &bytes, size_t id) {
  uint32_t offset = 0;
  for (size_t i = 0; i < 4; ++i) offset |= uint32_t(uint8_t(bytes[8 + id * 16 + i])) << (8 * i);
  return offset;
}

void word(std::string &bytes, uint32_t value) {
  const size_t at = bytes.size();
  bytes.resize(at + 4);
  put32(bytes, at, value);
}

void half(std::string &bytes, uint16_t value) {
  bytes.push_back(char(value));
  bytes.push_back(char(value >> 8));
}

void real(std::string &bytes, float value) {
  uint32_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  word(bytes, bits);
}

void point(std::string &bytes, Vec3 p) {
  real(bytes, p.x);
  real(bytes, p.y);
  real(bytes, p.z);
}

void lump(std::string &file, size_t id, const std::string &bytes) {
  const size_t entry = 8 + id * 16;
  put32(file, entry, uint32_t(file.size()));
  put32(file, entry + 4, uint32_t(bytes.size()));
  file += bytes;
}

void face(std::string &faces, int edge, uint16_t material, uint16_t disp, uint16_t plane = 0, uint16_t corners = 4) {
  const size_t start = faces.size();
  half(faces, plane);
  faces.push_back(char(plane & 1));
  faces.push_back(1);
  word(faces, edge);
  half(faces, corners);
  half(faces, material);
  half(faces, disp);
  faces.resize(start + 56);
}

enum class FloorWinding { Plain, Retraced, Indexed, ExplicitStrip };

void floor_primitives(std::string &bsp, std::string &faces, FloorWinding winding) {
  put32(faces, 48, 0x8001);  // one primitive, first ID 0, dynamic shadows disabled
  std::string primitive, indices;
  const bool strip = winding == FloorWinding::ExplicitStrip;
  half(primitive, strip ? 1 : 0);  // byte type and padding
  half(primitive, 0);  // first index
  half(primitive, strip ? 4 : 6);
  half(primitive, strip ? 2 : 0);  // first explicit vertex
  half(primitive, strip ? 4 : 0);  // zero explicit vertices means the indices refer to the face winding
  if (strip) {
    std::string vertices;
    for (const Vec3 p : {Vec3(250, 0, 0), Vec3(300, 0, 0),   // other records in the shared primitive vertex lump
                         Vec3(-80, 0, 0), Vec3(-80, 32, 0), Vec3(-16, 0, 48), Vec3(-16, 32, 48)})
      point(vertices, p);
    for (uint16_t absolute_index = 2; absolute_index < 6; ++absolute_index) half(indices, absolute_index);
    lump(bsp, 38, vertices);
  } else {
    for (const uint16_t winding_index : std::array<uint16_t, 6>{0, 3, 5, 3, 4, 5}) half(indices, winding_index);
  }
  lump(bsp, 37, primitive);
  lump(bsp, 39, indices);
}

std::string bsp_fixture(FloorWinding floor = FloorWinding::Plain) {
  std::string bsp(1036, '\0');
  bsp.replace(0, 4, "VBSP");
  put32(bsp, 4, 20);
  lump(bsp, 0, R"entities({
"classname" "worldspawn"
}
{
"classname" "func_brush"
"targetname" "Platform"
"model" "*1"
"origin" "100 20 30"
"angles" "90 0 0"
}
{
"classname" "func_brush"
"targetname" "Turned Platform"
"model" "*1"
"origin" "200 40 60"
"angles" "0 90 90"
}
)entities");

  std::string planes;
  point(planes, {-0.6f, 0, 0.8f});
  real(planes, 48);
  word(planes, 5);  // non-axial, dominant Z
  point(planes, {0.6f, 0, -0.8f});
  real(planes, -48);
  word(planes, 5);
  point(planes, {0, 0, 1});
  real(planes, 0);
  word(planes, 2);  // axial Z
  point(planes, {0, 0, -1});
  real(planes, 0);
  word(planes, 2);
  lump(bsp, 1, planes);

  std::string texdata(3 * 32, '\0'), texinfo;
  // All surfaces share the same axes, with distinct shifts and dimensions.
  for (uint32_t i = 0; i < 3; ++i) {
    put32(texdata, i * 32 + 12, i);
    put32(texdata, i * 32 + 16, i == 2 ? 128 : 64);
    put32(texdata, i * 32 + 20, 32);
    point(texinfo, {1, 0, 0});
    real(texinfo, float(i * 8));
    point(texinfo, {0, 1, 0});
    real(texinfo, float(i * 4));
    texinfo.resize(i * 72 + 68);
    word(texinfo, i);
  }
  lump(bsp, 2, texdata);
  lump(bsp, 6, texinfo);

  std::string verts, edges(4, '\0'), surfedges, faces;
  // Clockwise Source quads, with each second edge stored in the opposite direction.
  for (int f = 0; f < 3; ++f) {
    const float x = f == 0 ? -80.0f : 0.0f;
    const float height = f == 0 ? 48.0f : 0.0f;
    for (Vec3 p : {Vec3(x, 0, 0), Vec3(x, 32, 0), Vec3(x + 64, 32, height), Vec3(x + 64, 0, height)})
      point(verts, p);
    for (int i = 0; i < 4; ++i) {
      const uint16_t a = uint16_t(f * 4 + i), b = uint16_t(f * 4 + (i + 1) % 4);
      half(edges, (i & 1) ? b : a);
      half(edges, (i & 1) ? a : b);
      const uint32_t edge = uint32_t(1 + f * 4 + i);
      word(surfedges, (i & 1) ? 0u - edge : edge);
    }
  }
  if (floor == FloorWinding::Plain) {
    face(faces, 0, 0, 0xffff);
  } else {
    // Six winding corners A-spur-A-B-C-D. This backtrack lies in the face plane outside the quad.
    point(verts, {-112, 0, -24});
    half(edges, 0);
    half(edges, 12);
    for (const int edge : {13, -13, 1, -2, 3, -4}) word(surfedges, uint32_t(edge));
    face(faces, 12, 0, 0xffff, 0, 6);
  }
  if (floor == FloorWinding::Indexed || floor == FloorWinding::ExplicitStrip) floor_primitives(bsp, faces, floor);
  face(faces, 4, 1, 0xffff, 3);  // shared brush entity on the odd (already oriented) plane
  face(faces, 8, 2, 0, 2);    // displacement emitted outside the model runs
  lump(bsp, 3, verts);
  lump(bsp, 7, faces);
  lump(bsp, 12, edges);
  lump(bsp, 13, surfedges);
  std::string models(96, '\0');
  put32(models, 44, 1);
  put32(models, 88, 1);
  put32(models, 92, 1);
  lump(bsp, 14, models);

  std::string dispinfo, dispverts, disptris;
  point(dispinfo, {64, 0, 0});  // grid begins at the fourth point of the face
  dispinfo.resize(176);
  put32(dispinfo, 20, 2);
  put32(dispinfo, 36, 2);
  for (int i = 0; i < 25; ++i) {
    point(dispverts, {0, 0, 1});
    real(dispverts, i == 6 ? 16.0f : 0.0f);
    real(dispverts, 0.0f);
  }
  for (int i = 0; i < 32; ++i) half(disptris, i == 31 ? 32 : 1);
  lump(bsp, 26, dispinfo);
  lump(bsp, 33, dispverts);
  std::string names;
  std::string name_strings;
  for (const char *name : {"fixture/floor", "fixture/platform", "fixture/terrain"}) {
    word(names, uint32_t(name_strings.size()));
    name_strings += name;
    name_strings.push_back('\0');
  }
  lump(bsp, 43, name_strings);
  lump(bsp, 44, names);
  lump(bsp, 48, disptris);
  return bsp;
}

bool write_map_fixture(const std::string &mount, const std::string &bsp) {
  const std::string mats = fs::join(mount, "materials/fixture");
  return fs::make_dirs(mats) && fs::make_dirs(fs::join(mount, "maps")) &&
         fs::write_file(fs::join(mats, "floor.vmt"), R"("LightmappedGeneric" { "$color" "[0.4 0.2 0.1]" })") &&
         fs::write_file(fs::join(mats, "terrain.vmt"), R"("LightmappedGeneric" { "$color" "[0.2 0.4 0.8]" })") &&
         fs::write_file(fs::join(mats, "platform.vmt"), R"("LightmappedGeneric" { "$color" "[0.9 0.3 0.2]" })") &&
         fs::write_file(fs::join(mount, "maps/fixture.bsp"), bsp);
}

std::string packed_floor() {
  const std::string name = "materials/fixture/floor.vmt";
  const std::string material = R"("LightmappedGeneric" { "$color" "[0.1 0.8 0.3]" })";
  std::string attributes;
  half(attributes, 20);  // ZIP 2.0, stored member
  attributes.resize(10);
  word(attributes, 0x3436ba8e);  // CRC-32 of the authored VMT, from Python zlib.crc32
  word(attributes, uint32_t(material.size()));
  word(attributes, uint32_t(material.size()));
  half(attributes, uint16_t(name.size()));
  half(attributes, 0);

  std::string archive, directory;
  word(archive, 0x04034b50);
  archive += attributes + name + material;
  word(directory, 0x02014b50);
  half(directory, 20);
  directory += attributes;
  directory.resize(directory.size() + 14);
  directory += name;
  const uint32_t directory_start = uint32_t(archive.size());
  archive += directory;
  word(archive, 0x06054b50);
  word(archive, 0);
  half(archive, 1);
  half(archive, 1);
  word(archive, uint32_t(directory.size()));
  word(archive, directory_start);
  half(archive, 0);
  return archive;
}

void add_missing_prop(std::string &bsp) {
  std::string records;
  word(records, 1);  // model dictionary
  records += "models/fixture/absent.mdl";
  records.resize(4 + 128);
  word(records, 0);  // leaf count
  word(records, 1);  // placement count
  const size_t placement = records.size();
  point(records, {120, -12, 48});
  point(records, {0, 180, 0});
  records.resize(placement + 56);  // v4, dictionary index 0

  std::string game;
  word(game, 1);
  word(game, 0x73707270);  // sprp
  half(game, 0);
  half(game, 4);
  word(game, uint32_t(bsp.size() + 20));
  word(game, uint32_t(records.size()));
  lump(bsp, 35, game + records);
}

void check_vector(Vec3 got, Vec3 expected) {
  CHECK_NEAR(got.x, expected.x, 1e-5);
  CHECK_NEAR(got.y, expected.y, 1e-5);
  CHECK_NEAR(got.z, expected.z, 1e-5);
}

// Each row pairs an independently authored native vertex (in inches) with its UV.
struct Corner {
  Vec3 position;
  Vec2 uv;
};

const std::array<Corner, 4> floor_corners = {{{{0, 0, -80}, {-1.25f, 1}}, {{-32, 0, -80}, {-1.25f, 0}},
                                             {{-32, 48, -16}, {-0.25f, 0}}, {{0, 48, -16}, {-0.25f, 1}}}};

void check_quad(const Mesh &mesh, const std::array<Corner, 4> &expected, Vec3 normal) {
  CHECK(mesh.face_count() >= 1);
  CHECK(mesh.has_uvs());
  if (!mesh.face_count() || !mesh.has_uvs()) return;
  CHECK(mesh.face_size(0) == 4);
  CHECK(mesh.material_of(0) == 0);
  check_vector(mesh.face_normal(0), normal);
  for (const Corner &want : expected) {
    bool found = false;
    for (size_t i = 0; i < mesh.face_size(0); ++i) {
      const uint32_t index = mesh.corner_verts[i];
      if (index >= mesh.positions.size() || length_sq(mesh.positions[index] - want.position * 0.0254f) > 1e-10f)
        continue;
      CHECK_NEAR(mesh.uvs[i].x, want.uv.x, 1e-5);
      CHECK_NEAR(mesh.uvs[i].y, want.uv.y, 1e-5);
      found = true;
    }
    CHECK(found);
  }
}

void check_world(const Mesh &mesh) {
  CHECK(mesh.face_count() == 32);
  CHECK(mesh.corner_count() == 97);
  CHECK(mesh.positions.size() == 29);
  check_quad(mesh, floor_corners, {0, 0.8f, -0.6f});
  if (!mesh.has_uvs()) return;
  int raised = 0, neighboring_triangles = 0, removed_triangles = 0;
  for (size_t f = 1; f < mesh.face_count(); ++f) {
    CHECK(mesh.face_size(f) == 3);
    CHECK(mesh.material_of(f) == 1);
    CHECK(mesh.face_normal(f).y > 0.4f);
    unsigned last_cell = 0;
    for (size_t i = mesh.face_offsets[f]; i < mesh.face_offsets[f + 1]; ++i) {
      const size_t vertex = mesh.corner_verts[i];
      CHECK(vertex < mesh.positions.size());
      if (vertex >= mesh.positions.size()) return;
      const Vec3 inches = mesh.positions[vertex] / 0.0254f;
      CHECK_NEAR(inches.x / 8, std::round(inches.x / 8), 1e-5);
      CHECK_NEAR(inches.z / 16, std::round(inches.z / 16), 1e-5);
      CHECK_NEAR(mesh.uvs[i].x, (inches.z + 16) / 128, 1e-5);
      CHECK_NEAR(mesh.uvs[i].y, 1 + (inches.x - 8) / 32, 1e-5);
      if (inches.y > 1) {
        check_vector(mesh.positions[vertex], Vec3(-8, 16, 48) * 0.0254f);
        ++raised;
      }
      // The last cell's A-B-C triangle remains. A-C-D has the authored REMOVE bit.
      if (length_sq(inches - Vec3(-24, 0, 16)) < 1e-8f) last_cell |= 1;
      if (length_sq(inches - Vec3(-24, 0, 0)) < 1e-8f) last_cell |= 2;
      if (length_sq(inches - Vec3(-32, 0, 0)) < 1e-8f) last_cell |= 4;
      if (length_sq(inches - Vec3(-32, 0, 16)) < 1e-8f) last_cell |= 8;
    }
    neighboring_triangles += last_cell == 7;
    removed_triangles += last_cell == 13;
  }
  CHECK(raised > 0);
  CHECK(neighboring_triangles == 1);
  CHECK(removed_triangles == 0);
}

void check_brush(const Mesh &mesh) {
  CHECK(mesh.face_count() == 1);
  CHECK(mesh.positions.size() == 4);
  check_quad(mesh, {{{{0, 0, 0}, {0.125f, 0.875f}}, {{-32, 0, 0}, {0.125f, -0.125f}},
                     {{-32, 0, 64}, {1.125f, -0.125f}}, {{0, 0, 64}, {1.125f, 0.875f}}}}, {0, -1, 0});
}

void check_placements(const Mat4 &first, const Mat4 &second) {
  check_vector(first.translation(), {-0.508f, 0.762f, 2.54f});
  check_vector(second.translation(), {-1.016f, 1.524f, 5.08f});
  check_vector(first.column(0), {1, 0, 0});
  check_vector(first.column(1), {0, 0, 1});
  check_vector(first.column(2), {0, -1, 0});
  check_vector(second.column(0), {0, -1, 0});
  check_vector(second.column(1), {0, 0, 1});
  check_vector(second.column(2), {-1, 0, 0});
}

void check_map_materials(const std::vector<MaterialPtr> &world, const std::vector<MaterialPtr> &brush) {
  CHECK(world.size() == 2);
  CHECK(brush.size() == 1);
  if (world.size() == 2) {
    check_vector(world[0]->base_color, {0.4f, 0.2f, 0.1f});
    check_vector(world[1]->base_color, {0.2f, 0.4f, 0.8f});
  }
  if (brush.size() == 1) check_vector(brush[0]->base_color, {0.9f, 0.3f, 0.2f});
}

void check_named_brush(const MeshPtr &mesh, const std::vector<MaterialPtr> &materials, const Mat4 &transform,
                       const std::array<Vec3, 4> &columns) {
  CHECK(mesh != nullptr);
  if (mesh) check_brush(*mesh);
  CHECK(materials.size() == 1 && materials.front());
  if (materials.size() == 1 && materials.front()) check_vector(materials.front()->base_color, {0.9f, 0.3f, 0.2f});
  for (int i = 0; i < 4; ++i) check_vector(transform.column(i), columns[i]);
}

void check_retraced_floor(const std::string &path, FloorWinding winding) {
  CHECK(write_map_fixture(path, bsp_fixture(winding)));
  SourceContent content;
  std::string error;
  CHECK(content.mount(path, error));
  ImportResult result;
  CHECK(import_source1_map(content, "maps/fixture.bsp", result));
  CHECK(result.nodes.size() == 3);
  if (result.nodes.size() != 3) return;

  const ImportedNode &world = result.nodes.front();
  CHECK(world.materials.size() == 2);
  if (world.materials.size() != 2) return;
  CHECK(world.mesh && world.mesh->has_uvs());
  if (!world.mesh || !world.mesh->has_uvs()) return;
  const Mesh &mesh = *world.mesh;
  check_vector(world.materials[0]->base_color, {0.4f, 0.2f, 0.1f});

  const Vec3 normal(0, 0.8f, -0.6f);
  const bool expect_triangles = winding != FloorWinding::Retraced;
  float area = 0;
  std::vector<unsigned> face_corners;
  for (size_t f = 0; f < mesh.face_count(); ++f) {
    if (mesh.material_of(f) != 0) continue;
    check_vector(mesh.face_normal(f), normal);
    if (expect_triangles) CHECK(mesh.face_size(f) == 3);
    unsigned mask = 0;
    const size_t start = mesh.face_offsets[f], end = mesh.face_offsets[f + 1];
    const Vec3 origin = mesh.positions[mesh.corner_verts[start]];
    for (size_t c = start; c < end; ++c) {
      const Vec3 p = mesh.positions[mesh.corner_verts[c]];
      const auto expect = std::find_if(floor_corners.begin(), floor_corners.end(), [p](const Corner &corner) {
        return length_sq(p - corner.position * 0.0254f) <= 1e-10f;
      });
      CHECK(expect != floor_corners.end());
      if (expect != floor_corners.end()) {
        const unsigned bit = 1u << (expect - floor_corners.begin());
        CHECK(!(mask & bit));
        mask |= bit;
        CHECK_NEAR(mesh.uvs[c].x, expect->uv.x, 1e-5);
        CHECK_NEAR(mesh.uvs[c].y, expect->uv.y, 1e-5);
      }
      if (c > start + 1)
        area += 0.5f * dot(cross(mesh.positions[mesh.corner_verts[c - 1]] - origin, p - origin), normal);
    }
    CHECK(mask != 0);
    face_corners.push_back(mask);
  }
  CHECK_NEAR(area, 1.6516096f, 1e-5);  // 80 by 32 Source inches along the slope, converted to m²
  unsigned all = 0;
  for (const unsigned mask : face_corners) all |= mask;
  CHECK(all == 0b1111);
  if (expect_triangles) {
    std::sort(face_corners.begin(), face_corners.end());
    CHECK((face_corners == std::vector<unsigned>{0b1011, 0b1110}));  // A-B-D and B-C-D
  }
}

}  // namespace

void test_source1_map() {
  test("Source BSP displacement and shared brush placements reopen after moving the mount", [] {
    const std::string path = fs::join(test_dir(), "source1-map-placement");
    CHECK(write_map_fixture(path, bsp_fixture()));
    SourceContent content;
    std::string error;
    CHECK(content.mount(path, error));
    ImportResult result;
    CHECK(import_source1_map(content, "maps/fixture.bsp", result));
    CHECK(result.nodes.size() == 3);
    if (result.nodes.size() != 3) return;
    CHECK(result.notes.empty());
    CHECK(result.nodes[1].mesh == result.nodes[2].mesh);
    CHECK(result.nodes[0].mesh && result.nodes[1].mesh);
    if (!result.nodes[0].mesh || !result.nodes[1].mesh) return;
    check_world(*result.nodes[0].mesh);
    check_brush(*result.nodes[1].mesh);
    check_map_materials(result.nodes[0].materials, result.nodes[1].materials);
    check_placements(Mat4::translate(result.nodes[1].position) * Mat4::rotate(result.nodes[1].rotation),
                     Mat4::translate(result.nodes[2].position) * Mat4::rotate(result.nodes[2].rotation));

    Scene scene;
    GameObject *map = instantiate_import(scene, result, "Map", nullptr);
    CHECK(map != nullptr);
    if (!map) return;
    const auto id = map->id;
    const std::string saved = fs::join(test_dir(), "source1-map-reopen.scene");
    CHECK(save_scene(scene, saved));
    CHECK(fs::move(path, path + "-offline"));
    CHECK(!fs::exists(path));
    Scene reopened;
    CHECK(load_scene(saved, reopened, error));
    GameObject *root = reopened.find(id);
    CHECK(root && root->children.size() == 3);
    if (!root || root->children.size() != 3) return;
    const GameObject *world = root->children[0], *first = root->children[1], *second = root->children[2];
    CHECK(first->name == "Platform");
    CHECK(second->name == "Turned Platform");
    check_placements(first->world_matrix(), second->world_matrix());
    const MeshFilter *wf = world->get<MeshFilter>(), *bf = first->get<MeshFilter>(), *sf = second->get<MeshFilter>();
    CHECK(wf && wf->mesh);
    CHECK(bf && bf->mesh && sf && sf->mesh == bf->mesh);
    if (wf && wf->mesh) check_world(*wf->mesh);
    if (bf && bf->mesh) check_brush(*bf->mesh);
    const MeshRenderer *wr = world->get<MeshRenderer>(), *br = second->get<MeshRenderer>();
    CHECK(wr && br);
    if (wr && br) check_map_materials(wr->materials, br->materials);
  });

  test("Source BSP keeps named brush placements with mixed-case keys and shortened transforms after reopen", [] {
    std::string bsp = bsp_fixture();
    lump(bsp, 0, R"entities({
"ClAsSnAmE" "func_brush"
"TaRgEtNaMe" "Short Pitch"
"MoDeL" "*1"
"oRiGiN" "+100"
"aNgLeS" "+90 "
}
{
"CLASSNAME" "func_rotating"
"MODEL" "*1"
"ORIGIN" "200 -40"
"ANGLES" "0 -90"
}
)entities");
    const std::string path = fs::join(test_dir(), "source1-map-short-transforms");
    CHECK(write_map_fixture(path, bsp));
    SourceContent content;
    std::string error;
    CHECK(content.mount(path, error));
    ImportResult result;
    CHECK(import_source1_map(content, "maps/fixture.bsp", result));
    CHECK(result.nodes.size() == 3);
    if (result.nodes.size() != 3) return;

    struct ExpectedPlacement {
      const char *name;
      std::array<Vec3, 4> columns;
    };
    const ExpectedPlacement expected[] = {
        {"Short Pitch", {{{1, 0, 0}, {0, 0, 1}, {0, -1, 0}, {0, 0, 2.54f}}}},
        {"func_rotating", {{{0, 0, -1}, {0, 1, 0}, {1, 0, 0}, {1.016f, 0, 5.08f}}}},
    };
    for (const auto &placement : expected) {
      const auto node = std::find_if(result.nodes.begin(), result.nodes.end(), [&](const ImportedNode &item) {
        return item.name == placement.name;
      });
      CHECK(node != result.nodes.end());
      if (node == result.nodes.end()) continue;
      check_named_brush(node->mesh, node->materials, Mat4::translate(node->position) * Mat4::rotate(node->rotation),
                        placement.columns);
    }

    Scene scene;
    CHECK(instantiate_import(scene, result, "Short Transforms", nullptr));
    const std::string saved = fs::join(test_dir(), "source1-map-short-transforms.scene");
    CHECK(save_scene(scene, saved));
    CHECK(fs::move(path, path + "-offline"));
    CHECK(!fs::exists(path));
    Scene reopened;
    CHECK(load_scene(saved, reopened, error));
    for (const auto &placement : expected) {
      const GameObject *brush = reopened.find_by_name(placement.name);
      CHECK(brush != nullptr);
      if (!brush) continue;
      const MeshFilter *filter = brush->get<MeshFilter>();
      const MeshRenderer *renderer = brush->get<MeshRenderer>();
      CHECK(filter && renderer);
      if (filter && renderer) check_named_brush(filter->mesh, renderer->materials, brush->world_matrix(), placement.columns);
    }
  });

  test("Source BSP rejects invalid or extra brush transform components before emitting scene geometry", [] {
    const std::array<std::string, 2> invalid = {R"("ORIGIN" "12 bad" "ANGLES" "90")",
                                                R"("ORIGIN" "12" "ANGLES" "0 90 0 1")"};
    const std::string path = fs::join(test_dir(), "source1-map-broken-transform");
    for (const auto &transform : invalid) {
      std::string bsp = bsp_fixture();
      lump(bsp, 0, R"({ "MoDeL" "*1" )" + transform + " }");
      CHECK(write_map_fixture(path, bsp));
      SourceContent content;
      std::string error;
      CHECK(content.mount(path, error));
      ImportResult result;
      CHECK(!import_source1_map(content, "maps/fixture.bsp", result));
      CHECK(result.error.find("brush entity") != std::string::npos);
      CHECK(result.nodes.empty());
    }
  });

  test("Source BSP prefers its embedded material and marks a missing static prop at its authored position", [] {
    std::string bsp = bsp_fixture();
    lump(bsp, 40, packed_floor());
    add_missing_prop(bsp);
    const std::string path = fs::join(test_dir(), "source1-map-embedded");
    CHECK(write_map_fixture(path, bsp));
    SourceContent content;
    std::string error;
    CHECK(content.mount(path, error));
    ImportResult result;
    CHECK(import_source1_map(content, "maps/fixture.bsp", result));
    CHECK(result.nodes.size() == 4);
    if (result.nodes.size() != 4) return;
    CHECK(result.nodes[0].materials.size() == 2);
    if (result.nodes[0].materials.size() == 2) {
      check_vector(result.nodes[0].materials[0]->base_color, {0.1f, 0.8f, 0.3f});
      check_vector(result.nodes[0].materials[1]->base_color, {0.2f, 0.4f, 0.8f});
    }
    const ImportedNode &marker = result.nodes.back();
    CHECK(marker.mesh && marker.mesh->face_count() > 0);
    CHECK(marker.materials.size() == 1);
    if (marker.materials.size() == 1) check_vector(marker.materials[0]->base_color, {1, 0, 1});
    check_vector(marker.position, {0.3048f, 1.2192f, 3.048f});
    check_vector(marker.rotation.rotate({0, 0, 1}), {0, 0, -1});
    CHECK(std::any_of(result.notes.begin(), result.notes.end(), [](const std::string &note) {
      return note.find("models/fixture/absent.mdl") != std::string::npos;
    }));
  });

  test("Source BSP preserves the floor area of a brush winding with a retraced edge", [] {
    check_retraced_floor(fs::join(test_dir(), "source1-map-retraced-floor"), FloorWinding::Retraced);
  });

  test("Source BSP uses indexed triangles on a brush winding with a retraced edge", [] {
    check_retraced_floor(fs::join(test_dir(), "source1-map-indexed-floor"), FloorWinding::Indexed);
  });

  test("Source BSP keeps the winding and UVs of an explicit vertex strip with absolute indices", [] {
    check_retraced_floor(fs::join(test_dir(), "source1-map-explicit-strip"), FloorWinding::ExplicitStrip);
  });

  test("Source BSP rejects an explicit primitive outside its vertex span", [] {
    const std::string path = fs::join(test_dir(), "source1-map-broken-strip");
    const std::string name = "maps/fixture.bsp";
    for (bool bad_span : {false, true}) {
      std::string bsp = bsp_fixture(FloorWinding::ExplicitStrip);
      if (bad_span) {
        bsp[lump_start(bsp, 37) + 8] = 5;  // declared vertices [2, 7) exceed the six-vertex lump
      } else {
        bsp[lump_start(bsp, 39) + 6] = 1;  // second triangle uses a vertex outside [2, 6)
      }
      CHECK(write_map_fixture(path, bsp));
      SourceContent content;
      std::string error;
      CHECK(content.mount(path, error));
      ImportResult result;
      CHECK(!import_source1_map(content, name, result));
      CHECK(result.error.find("BSP face 0") != std::string::npos);
      CHECK(result.nodes.empty());
    }
  });

  test("Source BSP reports the invalid face instead of returning partial geometry", [] {
    const std::string path = fs::join(test_dir(), "source1-map-broken");
    CHECK(fs::make_dirs(fs::join(path, "maps")));
    const std::array<const char *, 5> expected = {"header", "version 99", "invalid range", "BSP face 0", "BSP face 0"};
    for (size_t i = 0; i < expected.size(); ++i) {
      std::string bsp = bsp_fixture();
      switch (i) {
        case 0: bsp.resize(20); break;
        case 1: put32(bsp, 4, 99); break;
        case 2: put32(bsp, 8 + 7 * 16, uint32_t(bsp.size() - 1)); break;
        case 3: put32(bsp, lump_start(bsp, 13), 0x80000000u); break;
        case 4: put32(bsp, lump_start(bsp, 3), 0x7fc00000u); break;  // NaN vertex coordinate
      }
      const std::string name = "maps/broken-" + std::to_string(i) + ".bsp";
      CHECK(fs::write_file(fs::join(path, name), bsp));
      SourceContent content;
      std::string error;
      CHECK(content.mount(path, error));
      ImportResult result;
      CHECK(!import_source1_map(content, name, result));
      CHECK(result.error.find(expected[i]) != std::string::npos);
      CHECK(result.nodes.empty());
    }
  });
}

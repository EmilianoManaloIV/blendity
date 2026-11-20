// SPDX-License-Identifier: GPL-2.0-or-later
#include "test_common.h"
#include "../src/scene/source_content.h"
#include "../src/scene/source1_material.h"

namespace {

// Generated here from documented disk layouts. No compiled title content is checked into the repository.
struct Disk {
  std::string bytes;
  explicit Disk(size_t n) : bytes(n, '\0') {}
  void u8(size_t at, unsigned x) { bytes[at] = char(x & 255); }
  void u16(size_t at, unsigned x) { u8(at, x); u8(at + 1, x >> 8); }
  void u32(size_t at, uint32_t x) { u16(at, x); u16(at + 2, x >> 16); }
  void f32(size_t at, float x) {
    uint32_t bits;
    std::memcpy(&bits, &x, sizeof(bits));
    u32(at, bits);
  }
  void point(size_t at, Vec3 p) { f32(at, p.x); f32(at + 4, p.y); f32(at + 8, p.z); }
  size_t append(size_t length) {
    const size_t at = bytes.size();
    bytes.resize(at + length, '\0');
    return at;
  }
  void string_ref(size_t at, size_t owner, const std::string &s) {
    const size_t string_at = append(s.size() + 1);
    u32(at, (uint32_t)(string_at - owner));
    bytes.replace(string_at, s.size(), s);
  }
};

constexpr uint32_t kChecksum = 0x96f47a15;
constexpr char kModelPath[] = "models/dummy.mdl";
constexpr char kValve[] = "ValveBiped.Bip01_";

struct JointFixture { const char *name; int parent; Vec3 global; };
const JointFixture kJoints[] = {
    {"Pelvis", -1, {0, 0, 36}}, {"Head1", 0, {0, 0, 63}},
    {"L_UpperArm", 0, {0, 7, 56}}, {"L_Forearm", 2, {-2, 16, 55}}, {"L_Hand", 3, {0, 24, 55}},
    {"R_UpperArm", 0, {0, -7, 56}}, {"R_Forearm", 5, {-2, -16, 55}}, {"R_Hand", 6, {0, -24, 55}},
    {"L_Thigh", 0, {0, 4, 36}}, {"L_Calf", 8, {3, 4, 20}}, {"L_Foot", 9, {0, 4, 6}},
    {"R_Thigh", 0, {0, -4, 36}}, {"R_Calf", 11, {3, -4, 20}}, {"R_Foot", 12, {0, -4, 6}},
};

std::string model_bytes(int version, bool prop, uint32_t selected_vertices = 4, bool machine_joints = false,
                        const std::string &material = "shirt",
                        const std::vector<std::string> &material_dirs = {"models\\dummy\\"},
                        const std::string &other_material = {}) {
  Disk mdl(408);
  mdl.bytes.replace(0, 4, "IDST");
  mdl.u32(4, version);
  mdl.u32(8, kChecksum);
  mdl.u32(152, prop ? 0x10 : 0);
  const size_t bones = mdl.append(std::size(kJoints) * 216);
  mdl.u32(156, (uint32_t)std::size(kJoints));
  mdl.u32(160, (uint32_t)bones);
  for (size_t i = 0; i < std::size(kJoints); ++i) {
    const auto &j = kJoints[i];
    const size_t at = bones + i * 216;
    const std::string name = machine_joints ? (i == 3 ? "Flap" : "Link" + std::to_string(i)) : std::string(kValve) + j.name;
    mdl.string_ref(at, at, name);
    mdl.u32(at + 4, (uint32_t)j.parent);
    mdl.point(at + 32, j.parent >= 0 ? j.global - kJoints[j.parent].global : j.global);
    mdl.f32(at + 56, 1);
    // Authored bone +X is not directed down any limb. poseToBone is the inverse bind world matrix.
    mdl.f32(at + 96, 1);
    mdl.f32(at + 116, 1);
    mdl.f32(at + 136, 1);
    mdl.f32(at + 108, -j.global.x);
    mdl.f32(at + 124, -j.global.y);
    mdl.f32(at + 140, -j.global.z);
  }
  const size_t textures = mdl.append(2 * 64);
  mdl.u32(204, 2);
  mdl.u32(208, (uint32_t)textures);
  mdl.string_ref(textures, textures, other_material.empty() ? "unused" : other_material);
  mdl.string_ref(textures + 64, textures + 64, material);
  const size_t cd = mdl.append(material_dirs.size() * 4);
  mdl.u32(212, (uint32_t)material_dirs.size());
  mdl.u32(216, (uint32_t)cd);
  for (size_t i = 0; i < material_dirs.size(); ++i) mdl.string_ref(cd + i * 4, 0, material_dirs[i]);
  const size_t skins = mdl.append(8);
  mdl.u32(220, 2);
  mdl.u32(224, 2);
  mdl.u32(228, (uint32_t)skins);
  mdl.u16(skins, 1);
  mdl.u16(skins + 6, 1);
  const size_t primary_meshes = other_material.empty() ? 1 : 2;
  const size_t body = mdl.append(16), models = mdl.append(2 * 148), meshes = mdl.append((primary_meshes + 1) * 116);
  mdl.u32(232, 1);
  mdl.u32(236, (uint32_t)body);
  mdl.u32(body + 4, 2);
  mdl.u32(body + 8, 1);
  mdl.u32(body + 12, (uint32_t)(models - body));
  for (size_t i = 0; i < 2; ++i) {
    const size_t model = models + i * 148, mesh = meshes + i * primary_meshes * 116;
    mdl.u32(model + 72, i == 0 ? (uint32_t)primary_meshes : 1);
    mdl.u32(model + 76, (uint32_t)(mesh - model));
    mdl.u32(model + 80, i == 0 ? selected_vertices : 3);
    mdl.u32(model + 84, i == 0 ? 0 : selected_vertices * 48);
    mdl.u32(mesh, (uint32_t)i);
    mdl.u32(mesh + 8, i == 0 ? selected_vertices : 3);
  }
  if (primary_meshes == 2) {
    mdl.u32(meshes + 116, 1);
    mdl.u32(meshes + 116 + 8, selected_vertices);
  }
  mdl.u32(76, (uint32_t)mdl.bytes.size());
  return mdl.bytes;
}

std::string vertex_bytes() {
  Disk vvd(100 + 7 * 48);
  vvd.bytes.replace(0, 4, "IDSV");
  vvd.u32(4, 4);
  vvd.u32(8, kChecksum);
  vvd.u32(12, 2);
  vvd.u32(16, 7);
  vvd.u32(20, 2);
  vvd.u32(48, 3);
  vvd.u32(52, 64);
  vvd.u32(56, 100);
  // VVD on-disk order is 2,3,0,1,4,5,6. The first two ranges must swap at LOD 0.
  vvd.u32(68, 2); vvd.u32(72, 2);
  vvd.u32(76, 1); vvd.u32(84, 2);
  vvd.u32(92, 4); vvd.u32(96, 3);
  const Vec3 positions[]{{6, 24, 8}, {6, -24, 8}, {6, -24, 64}, {6, 24, 64}, {320, 20, 7}, {320, -20, 7}, {320, -20, 55}};
  const Vec2 uvs[]{{0, 1}, {1, 1}, {1, 0}, {0, 0}, {0, 1}, {1, 1}, {1, 0}};
  const int joint[] = {0, 0, 0, 3, 0, 0, 0};
  for (size_t i = 0; i < 7; ++i) {
    const size_t at = 100 + (i < 4 ? (i + 2) % 4 : i) * 48;
    vvd.f32(at, i == 2 ? 0.75f : 1.0f);
    vvd.u8(at + 12, joint[i]);
    if (i == 2) { vvd.f32(at + 4, 0.25f); vvd.u8(at + 13, 3); }
    vvd.u8(at + 15, i == 2 ? 2 : 1);
    vvd.point(at + 16, positions[i]);
    vvd.point(at + 28, {1, 0, 0});
    vvd.f32(at + 40, uvs[i].x);
    vvd.f32(at + 44, uvs[i].y);
  }
  return vvd.bytes;
}

std::string topology_bytes(bool extended, uint32_t vertices = 4, const std::vector<uint16_t> &triangles = {}, bool two_slots = false) {
  Disk vtx(36 + 8 + 16);
  vtx.u32(0, 7);
  vtx.u32(16, kChecksum);
  vtx.u32(20, 1);
  vtx.u32(28, 1);
  vtx.u32(32, 36);
  vtx.u32(36, 2);
  vtx.u32(40, 8);
  const size_t group_stride = extended ? 33 : 25;
  for (size_t model = 0; model < 2; ++model) {
    const size_t mesh_count = model == 0 && two_slots ? 2 : 1;
    const size_t header = 44 + model * 8, lod = vtx.append(12), meshes = vtx.append(mesh_count * 9);
    vtx.u32(header, 1);
    vtx.u32(header + 4, (uint32_t)(lod - header));
    vtx.u32(lod, (uint32_t)mesh_count);
    vtx.u32(lod + 4, (uint32_t)(meshes - lod));
    for (size_t m = 0; m < mesh_count; ++m) {
      const size_t mesh = meshes + m * 9;
      const bool custom = model == 0 && !triangles.empty();
      const size_t group_count = model == 0 && !custom && !two_slots ? 2 : 1, groups = vtx.append(group_count * group_stride);
      vtx.u32(mesh, (uint32_t)group_count);
      vtx.u32(mesh + 4, (uint32_t)(groups - mesh));
      for (size_t i = 0; i < group_count; ++i) {
        const size_t at = groups + i * group_stride;
        const uint32_t points = custom ? vertices : 3, count = custom ? (uint32_t)triangles.size() : 3;
        const size_t vert = vtx.append(size_t(points) * 9), indices = vtx.append(size_t(count) * 2), strip = vtx.append(extended ? 35 : 27);
        vtx.u32(at, points); vtx.u32(at + 4, (uint32_t)(vert - at));
        vtx.u32(at + 8, count); vtx.u32(at + 12, (uint32_t)(indices - at));
        vtx.u32(at + 16, 1); vtx.u32(at + 20, (uint32_t)(strip - at));
        vtx.u8(at + 24, 2);
        vtx.u32(strip, count); vtx.u32(strip + 8, points); vtx.u8(strip + 18, 1);
        // Clockwise in Source coordinates as seen from +X. The second strip maps its own indices.
        const unsigned id[2][3]{{0, 1, 2}, {0, 2, 3}};
        for (uint32_t v = 0; v < points; ++v) vtx.u16(vert + 9 * v + 4, custom ? v : id[m + i][v]);
        for (uint32_t v = 0; v < count; ++v) vtx.u16(indices + 2 * v, custom ? triangles[v] : v);
      }
    }
  }
  return vtx.bytes;
}

std::string color_vtf() {
  Disk file(88 + 4 + 16);
  file.bytes.replace(0, 4, "VTF\0", 4);
  file.u32(4, 7);
  file.u32(8, 3);
  file.u32(12, 88);
  file.u16(16, 2); file.u16(18, 2);
  file.u32(20, 0xc);  // clamp both axes
  file.u16(24, 1); file.u16(63, 1);
  file.u8(56, 2); file.u32(57, UINT32_MAX);
  file.u32(68, 1);
  file.u32(80, 0x30); file.u32(84, 88);
  // The 1x1 mip is black. In the largest mip: top-left red, top-right transparent
  // green, bottom-left blue, bottom-right yellow. This catches both flip and mip selection.
  file.u32(92, 0xff0000ff);
  file.u32(96, 0x0000ff00);
  file.u32(100, 0xffff0000);
  file.u32(104, 0xff00ffff);
  return file.bytes;
}

std::string normal_vtf() {
  Disk file(80 + 16);
  file.bytes.replace(0, 4, "VTF\0", 4);
  file.u32(4, 7);
  file.u32(8, 2);
  file.u32(12, 80);
  file.u16(16, 4); file.u16(18, 4);
  file.u16(24, 1); file.u16(63, 1);
  file.u32(52, 15);  // BC3/DXT5, solid cyan colour, alpha endpoint 200
  file.u8(56, 1); file.u32(57, UINT32_MAX);
  file.u8(80, 200); file.u8(81, 30);
  file.u16(88, 0x07ff); file.u16(90, 0x07ff);
  return file.bytes;
}

std::string fixture(const char *sub, bool v49 = true, bool prop = false, bool vvd = true, bool vtx = true) {
  const std::string root = fs::join(test_dir(), sub);
  CHECK(fs::make_dirs(fs::join(root, "models")));
  CHECK(fs::make_dirs(fs::join(root, "materials/models/dummy")));
  CHECK(fs::write_file(fs::join(root, kModelPath), model_bytes(v49 ? 49 : 48, prop)));
  if (vvd) CHECK(fs::write_file(fs::join(root, "models/dummy.vvd"), vertex_bytes()));
  if (vtx) CHECK(fs::write_file(fs::join(root, "models/dummy.dx90.vtx"), topology_bytes(v49)));
  CHECK(fs::write_file(fs::join(root, "materials/models/dummy/shirt.vmt"),
      "Patch { include \"materials/models/dummy/generic.vmt\" replace { \"$basetexture\" models/dummy/asym } insert { $alphatest 1 $alphatestreference 0.35 } }"));
  CHECK(fs::write_file(fs::join(root, "materials/models/dummy/generic.vmt"),
      "UnlitGeneric { // two source textures\n $basetexture missing $bumpmap \"models\\dummy\\tangent\" }"));
  CHECK(fs::write_file(fs::join(root, "materials/models/dummy/asym.vtf"), color_vtf()));
  CHECK(fs::write_file(fs::join(root, "materials/models/dummy/tangent.vtf"), normal_vtf()));
  return root;
}

std::string humanoid_fixture() {
  const std::string root = fixture("source1-marker");
  std::vector<Vec3> points;
  std::vector<uint16_t> triangles;
  auto append_box = [&](Vec3 lo, Vec3 hi) {
    const auto base = (uint16_t)points.size();
    for (Vec3 p : {Vec3(lo.x, lo.y, lo.z), Vec3(hi.x, lo.y, lo.z), Vec3(hi.x, hi.y, lo.z), Vec3(lo.x, hi.y, lo.z),
                   Vec3(lo.x, lo.y, hi.z), Vec3(hi.x, lo.y, hi.z), Vec3(hi.x, hi.y, hi.z), Vec3(lo.x, hi.y, hi.z)})
      points.emplace_back(p.z, -p.x, p.y);  // fixture is drawn in inches with Y up, then written in Source axes
    for (uint16_t i : {0, 3, 2, 0, 2, 1, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
                       3, 7, 6, 3, 6, 2, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5}) triangles.push_back(base + i);
  };
  append_box({-7, 32, 0}, {7, 57, 12});   // trunk
  append_box({-4, 56, 2}, {4, 66, 11});   // head
  append_box({-25, 51, 3}, {-6, 57, 9});  // arms
  append_box({6, 51, 3}, {25, 57, 9});
  append_box({-7, 4, 1}, {-1, 35, 10});   // legs and feet
  append_box({1, 4, 1}, {7, 35, 10});
  append_box({-7, 0, 1}, {-1, 6, 13});
  append_box({1, 0, 1}, {7, 6, 13});
  const auto nv = (uint32_t)points.size();
  points.insert(points.end(), {{300, 0, 0}, {300, 1, 0}, {300, 0, 1}});  // alternate group
  Disk vvd(64 + points.size() * 48);
  vvd.bytes.replace(0, 4, "IDSV");
  vvd.u32(4, 4); vvd.u32(8, kChecksum); vvd.u32(12, 1);
  vvd.u32(16, (uint32_t)points.size()); vvd.u32(56, 64);
  for (size_t i = 0; i < points.size(); ++i) {
    const size_t at = 64 + i * 48;
    vvd.f32(at, 1.0f); vvd.u8(at + 15, 1);
    vvd.point(at + 16, points[i]);
  }
  CHECK(fs::write_file(fs::join(root, kModelPath), model_bytes(49, false, nv)));
  CHECK(fs::write_file(fs::join(root, "models/dummy.vvd"), vvd.bytes));
  CHECK(fs::write_file(fs::join(root, "models/dummy.dx90.vtx"), topology_bytes(true, nv, triangles)));
  return root;
}

bool load_fixture(const std::string &root, SourceContent &content, ImportResult &out) {
  std::string error;
  if (content.mount(root, error) && import_source1_model(content, kModelPath, out)) return true;
  std::printf("    classic fixture: %s %s\n", error.c_str(), out.error.c_str());
  return false;
}

bool has_note(const ImportResult &out, const std::string &text) {
  return std::any_of(out.notes.begin(), out.notes.end(), [&](const auto &note) { return note.find(text) != std::string::npos; });
}

void check_picture(const ImportedNode &node) {
  constexpr int size = 128;
  Image image;
  image.resize(size, size);
  RenderTarget target;
  target.attach(image, {0, 0, size, size});
  DrawItem draw;
  draw.mesh = &node.mesh->render_mesh();
  draw.materials = &node.materials;
  draw.id = 15;
  node.materials[0]->filter = (int)TexFilter::Closest;
  const Mat4 ortho = Mat4::ortho(0.8f, 1.0f, 0.1f, 4.0f);
  Renderer3D renderer;
  RasterOptions options;
  options.shade = ShadeMode::Deferred;
  options.multithreaded = false;
  LightingEnv environment;
  environment.view_transform = ViewTransform::Standard;
  environment.camera_pos = {0, 0.9f, 2};
  renderer.begin(&target, Mat4::look_at(environment.camera_pos, {0, 0.9f, 0}, {0, 1, 0}), ortho, environment, options);
  constexpr uint32_t background = 0xff252525;
  renderer.clear(background);
  renderer.add(draw);
  renderer.flush();
  auto pixel = [&](Vec3 p, uint32_t id) {
    Vec2 screen;
    float depth;
    const bool visible = renderer.project(p, screen, depth);
    CHECK(visible);
    if (!visible) return 0u;
    const int x = (int)screen.x, y = (int)screen.y;
    if (x < 0 || x >= size || y < 0 || y >= size) return 0u;
    CHECK(target.id_at(x, y) == id);
    return image.pixels[(size_t)y * size + x];
  };
  const uint32_t top_left = pixel({-0.32f, 1.35f, 0.1524f}, draw.id);
  const uint32_t top_right = pixel({0.32f, 1.35f, 0.1524f}, 0);
  const uint32_t bottom_left = pixel({-0.32f, 0.45f, 0.1524f}, draw.id);
  const uint32_t bottom_right = pixel({0.32f, 0.45f, 0.1524f}, draw.id);
  CHECK((top_left >> 16 & 255) > 180 && (top_left & 255) < 32);
  CHECK(top_right == background);
  CHECK((bottom_left & 255) > 180 && (bottom_left >> 16 & 255) < 32);
  CHECK((bottom_right >> 16 & 255) > 180 && (bottom_right >> 8 & 255) > 180 && (bottom_right & 255) < 32);
  CHECK(pixel({0.72f, 0.45f, 0.1524f}, 0) == background);

  environment.camera_pos = {0, 0.9f, -2};
  renderer.begin(&target, Mat4::look_at(environment.camera_pos, {0, 0.9f, 0}, {0, 1, 0}), ortho, environment, options);
  renderer.clear(background);
  renderer.add(draw);
  renderer.flush();
  CHECK(pixel({-0.32f, 0.45f, 0.1524f}, 0) == background);
}

}  // namespace

void test_source1_model() {
  const char *prior_cache = std::getenv("BLENDITY_CACHE_DIR");
  const bool own_cache = !prior_cache || !*prior_cache;
  if (own_cache) set_env("BLENDITY_CACHE_DIR", fs::join(test_dir(), "source1-cache"));
  test("classic Source model: default body and skin render with the authored winding, UVs, metre scale and cutout", [] {
    SourceContent content;
    ImportResult result;
    const bool ok = load_fixture(fixture("source1-surface"), content, result);
    CHECK(ok);
    if (!ok || result.nodes.empty()) return;
    const ImportedNode &node = result.nodes.back();
    CHECK(node.mesh && node.materials.size() == 1 && result.materials.size() == 1 && result.textures.size() == 2);
    if (!node.mesh || node.materials.size() != 1) return;
    const Mesh &mesh = *node.mesh;
    CHECK(mesh.face_count() == 2 && mesh.vert_count() == 4);
    if (mesh.face_count() != 2 || mesh.vert_count() != 4) return;
    CHECK_NEAR(mesh.positions[0].x, -0.6096f, 1e-6f);
    CHECK_NEAR(mesh.positions[2].y, 1.6256f, 1e-6f);
    CHECK_NEAR(mesh.positions[3].z, 0.1524f, 1e-6f);
    CHECK(dot(cross(mesh.positions[1] - mesh.positions[0], mesh.positions[2] - mesh.positions[0]), mesh.custom_normals[0]) > 0);
    CHECK(node.materials[0]->name == "shirt" && node.materials[0]->surface == (int)MaterialSurface::Cutout);
    CHECK_NEAR(node.materials[0]->alpha_clip, 0.35f, 1e-6f);
    CHECK(node.materials[0]->normal_map.non_color);
    Bitmap normal;
    std::string error;
    CHECK(load_image(node.materials[0]->normal_map.path, normal, error));
    CHECK(normal.rgba8.size() == 64 && normal.rgba8[0] == 0 && normal.rgba8[1] == 0 && normal.rgba8[2] == 255 && normal.rgba8[3] == 200);
    check_picture(node);
  });

  test("classic Source humanoid: weighted manual pose, undo and reopen, and Auto IK reaches both hands and feet", [] {
    SourceContent content;
    ImportResult result;
    const bool ok = load_fixture(fixture("source1-rig", false), content, result);
    CHECK(ok);
    if (!ok) return;
    Editor ed;
    ed.init_headless(800, 600);
    std::vector<GameObject *> made;
    GameObject *root = instantiate_import(ed.scene(), result, "FixtureRig", nullptr, &made);
    CHECK(root && root->get<Armature>() && made.size() == 15);
    if (!root || made.size() != 15) return;
    GameObject *mesh = made.back();
    GameObject *forearm = made[3];
    CHECK(mesh && mesh->get<SkinModifier>() && forearm && forearm->get<Bone>());
    if (!mesh || !forearm || !mesh->get<SkinModifier>()) return;
    ed.mark_changed_for_test("Import Source fixture");
    ed.step_frame_headless();
    const std::vector<Vec3> rest = mesh->evaluated_mesh()->positions;
    const auto &base = mesh->mesh_filter()->mesh->positions;
    CHECK(rest.size() == base.size() && rest.size() == 4);
    if (rest.size() != 4 || base.size() != 4) return;
    for (size_t i = 0; i < rest.size(); ++i) CHECK_NEAR(length(rest[i] - base[i]), 0, 3e-6f);
    const Vec3 pivot = forearm->world_position();
    const Quat turn = Quat::axis_angle({0, 1, 0}, 0.6f);
    forearm->set_world_rotation(turn * forearm->world_rotation());
    ed.mark_changed_for_test("Source fixture manual pose");
    ed.step_frame_headless();
    const std::vector<Vec3> posed = mesh->evaluated_mesh()->positions;
    for (size_t i = 0; i < posed.size(); ++i) {
      const float weight = i == 3 ? 1.0f : i == 2 ? 0.25f : 0.0f;
      const Vec3 moved = pivot + turn.rotate(rest[i] - pivot);
      CHECK_NEAR(length(posed[i] - lerp(rest[i], moved, weight)), 0, 3e-5f);
    }
    const std::string path = fs::join(test_dir(), "source1-posed.scene");
    Scene reopened;
    std::string error;
    CHECK(save_scene(ed.scene(), path) && load_scene(path, reopened, error));
    GameObject *reopened_mesh = reopened.find_by_name("dummy");
    CHECK(reopened_mesh && reopened_mesh->get<SkinModifier>());
    if (reopened_mesh) {
      const auto &reopened_vertices = reopened_mesh->evaluated_mesh()->positions;
      CHECK(reopened_vertices.size() == posed.size());
      for (size_t i = 0; i < reopened_vertices.size() && i < posed.size(); ++i) CHECK_NEAR(length(reopened_vertices[i] - posed[i]), 0, 2e-5f);
    }
    ed.command("undo");
    ed.step_frame_headless();
    mesh = ed.scene().find_by_name("dummy");
    CHECK(mesh && mesh->evaluated_mesh()->positions.size() == rest.size());
    if (!mesh || mesh->evaluated_mesh()->positions.size() != rest.size()) return;
    for (size_t i = 0; i < rest.size(); ++i) CHECK_NEAR(length(mesh->evaluated_mesh()->positions[i] - rest[i]), 0, 3e-6f);
    ed.command("rig autoik FixtureRig");
    const char *end[][3]{{"L_Forearm", "L_Hand", "LeftHandIK"}, {"R_Forearm", "R_Hand", "RightHandIK"},
                         {"L_Calf", "L_Foot", "LeftFootIK"}, {"R_Calf", "R_Foot", "RightFootIK"}};
    for (const auto &chain : end) {
      GameObject *lower = ed.scene().find_by_name(std::string(kValve) + chain[0]);
      GameObject *tip = ed.scene().find_by_name(std::string(kValve) + chain[1]);
      GameObject *target = ed.scene().find_by_name(chain[2]);
      CHECK(lower && lower->get<IKConstraint>() && tip && target);
      if (!lower || !tip || !target || !lower->parent) continue;
      CHECK_NEAR(length(tip->world_position() - target->world_position()), 0, 5e-5f);
      const Vec3 inward = normalize(lower->parent->world_position() - target->world_position());
      target->set_world_position(target->world_position() + inward * 0.04f + Vec3(0, 0, 0.02f));
      solve_constraints(ed.scene());
      CHECK_NEAR(length(tip->world_position() - target->world_position()), 0, 1e-4f);
    }
  });

  test("classic Source mesh: a fresh unskinned copy can use marker-based humanoid Auto-Rig", [] {
    SourceContent content;
    ImportResult result;
    const bool ok = load_fixture(humanoid_fixture(), content, result);
    CHECK(ok);
    if (!ok || result.nodes.empty() || !result.nodes.back().mesh) return;
    Editor ed;
    ed.init_headless(800, 600);
    GameObject *mesh = ed.scene().create("FreshCopy");
    mesh->add<MeshFilter>()->mesh = std::make_shared<Mesh>(*result.nodes.back().mesh);
    mesh->mesh_filter()->mesh->vertex_groups.clear();
    mesh->add<MeshRenderer>();
    ed.mark_changed_for_test("Unskinned Source model");
    ed.step_frame_headless();
    ed.command("autorig humanoid FreshCopy");
    for (const char *m : {"autorig mark Chin 0 1.46 0.1524", "autorig mark Groin 0 0.9 0.1524", "autorig mark LeftWrist -0.56 1.34 0.1524",
                          "autorig mark LeftElbow -0.33 1.34 0.14", "autorig mark LeftKnee -0.12 0.52 0.17"}) ed.command(m);
    ed.command("autorig build");
    ed.step_frame_headless();
    mesh = ed.scene().find_by_name("FreshCopy");
    const GameObject *forearm = ed.scene().find_by_name("LeftLowerArm");
    CHECK(mesh && mesh->get<SkinModifier>() && forearm && forearm->get<IKConstraint>());
    if (!mesh || !mesh->get<SkinModifier>()) return;
    const auto &groups = mesh->mesh_filter()->mesh->vertex_groups;
    CHECK(!groups.empty());
    for (size_t v = 0; v < mesh->mesh_filter()->mesh->vert_count(); ++v) {
      float sum = 0;
      for (const auto &group : groups) sum += group.weights[v];
      CHECK_NEAR(sum, 1, 1e-4f);
    }
    GameObject *target = ed.scene().find_by_name("LeftHandIK");
    CHECK(target);
    if (target) {
      const std::vector<Vec3> before = mesh->evaluated_mesh()->positions;
      target->set_world_position(target->world_position() + Vec3(0.04f, 0.08f, 0));
      solve_constraints(ed.scene());
      const auto &posed = mesh->evaluated_mesh()->positions;
      float left_hand_moved = 0;
      for (size_t i = 0; i < before.size() && i < posed.size(); ++i)
        if (before[i].x < -0.55f) left_hand_moved = std::max(left_hand_moved, length(posed[i] - before[i]));
      CHECK(left_hand_moved > 0.035f);
    }
    ed.command("undo");
    ed.step_frame_headless();
    mesh = ed.scene().find_by_name("FreshCopy");
    CHECK(mesh && !mesh->get<SkinModifier>() && mesh->mesh_filter()->mesh->vertex_groups.empty());
  });

  test("classic Source model: a static prop stays unskinned and missing companions name their virtual path", [] {
    SourceContent content;
    ImportResult result;
    const bool ok = load_fixture(fixture("source1-prop", false, true), content, result);
    CHECK(ok && result.nodes.size() == 1 && result.skins.empty());
    if (ok && !result.nodes.empty()) CHECK(!result.nodes.front().is_bone && result.nodes.front().mesh->vertex_groups.empty());
    SourceContent no_vvd, no_vtx;
    std::string error;
    CHECK(no_vvd.mount(fixture("source1-missing-vvd", false, false, false), error));
    CHECK(!import_source1_model(no_vvd, kModelPath, result) && result.nodes.empty());
    CHECK(result.error.find("models/dummy.vvd") != std::string::npos);
    CHECK(no_vtx.mount(fixture("source1-missing-vtx", false, false, true, false), error));
    CHECK(!import_source1_model(no_vtx, kModelPath, result) && result.nodes.empty());
    CHECK(result.error.find("models/dummy.dx90.vtx") != std::string::npos);
  });

  test("classic Source model: a nonhumanoid retains its named joints, bind pose and weighted motion", [] {
    const std::string dir = fixture("source1-machine", false);
    CHECK(fs::write_file(fs::join(dir, kModelPath), model_bytes(48, false, 4, true)));
    SourceContent content;
    ImportResult result;
    const bool ok = load_fixture(dir, content, result);
    CHECK(ok && result.skins.size() == 1);
    if (!ok || result.skins.size() != 1) return;
    Scene scene;
    CHECK(instantiate_import(scene, result, "Machine", nullptr));
    GameObject *flap = scene.find_by_name("Flap");
    GameObject *mesh = scene.find_by_name("dummy");
    CHECK(flap && flap->parent && flap->parent->name == "Link2" && flap->get<Bone>());
    CHECK(mesh && mesh->get<SkinModifier>());
    if (!flap || !mesh || !mesh->get<SkinModifier>()) return;
    CHECK_NEAR(length(flap->world_position() - Vec3(-0.4064f, 1.3970f, -0.0508f)), 0, 2e-6f);
    // The Source joint basis converts to identity. A nonhumanoid keeps that basis when it is posed.
    CHECK_NEAR(length(flap->world_matrix().dir({0, 1, 0}) - Vec3(0, 1, 0)), 0, 2e-6f);
    const auto rest = mesh->evaluated_mesh()->positions;
    CHECK(rest.size() == 4);
    if (rest.size() != 4) return;
    const auto &base = mesh->mesh_filter()->mesh->positions;
    const Vec3 displacement{0.1f, -0.04f, 0.06f};
    flap->set_local_position(flap->local().position + displacement);
    const auto &posed = mesh->evaluated_mesh()->positions;
    CHECK(posed.size() == 4);
    if (posed.size() != 4) return;
    const float weights[]{0, 0, 0.25f, 1};
    for (size_t i = 0; i < 4; ++i) {
      CHECK_NEAR(length(rest[i] - base[i]), 0, 2e-6f);
      CHECK_NEAR(length(posed[i] - rest[i] - displacement * weights[i]), 0, 2e-6f);
    }
  });

  test("classic Source model: companion files from a different model identify their virtual path", [] {
    const char *companions[]{"models/dummy.vvd", "models/dummy.dx90.vtx"};
    for (size_t i = 0; i < std::size(companions); ++i) {
      const std::string dir = fixture(i == 0 ? "source1-mismatched-vvd" : "source1-mismatched-vtx");
      std::string bytes = i == 0 ? vertex_bytes() : topology_bytes(true);
      bytes[i == 0 ? 8 : 16] ^= 1;
      CHECK(fs::write_file(fs::join(dir, companions[i]), bytes));
      SourceContent content;
      ImportResult result;
      std::string error;
      CHECK(content.mount(dir, error));
      CHECK(!import_source1_model(content, kModelPath, result) && result.nodes.empty());
      CHECK(result.error.find(companions[i]) != std::string::npos);
      CHECK(result.error.find("checksum") != std::string::npos);
    }
  });

  test("classic Source materials: a missing base map and unsupported normal map identify both virtual paths", [] {
    const std::string dir = fixture("source1-material-diagnostics");
    CHECK(fs::write_file(fs::join(dir, "materials/models/dummy/shirt.vmt"),
          "VertexLitGeneric { $basetexture models/dummy/absent $bumpmap models/dummy/unsupported }"));
    Disk unsupported(80);
    unsupported.bytes.replace(0, 4, "VTF\0", 4);
    unsupported.u32(4, 7); unsupported.u32(8, 2); unsupported.u32(12, 80);
    unsupported.u16(16, 1); unsupported.u16(18, 1); unsupported.u16(24, 1);
    unsupported.u32(52, 1234); unsupported.u8(56, 1);
    CHECK(fs::write_file(fs::join(dir, "materials/models/dummy/unsupported.vtf"), unsupported.bytes));
    SourceContent content;
    ImportResult result;
    CHECK(load_fixture(dir, content, result));
    CHECK(result.materials.size() == 1 && result.textures.empty());
    bool missing = false, format = false;
    for (const std::string &note : result.notes) {
      missing |= note.find("materials/models/dummy/absent.vtf") != std::string::npos;
      format |= note.find("materials/models/dummy/unsupported.vtf: unsupported VTF pixel format 1234") != std::string::npos;
    }
    CHECK(missing && format);
  });
  test("classic Source names: malformed encoding and overlong MDL strings report readable paths", [] {
    const std::string legacy = std::string("shirt-") + char(0xe9);
    for (bool oversized : {false, true}) {
      const std::string dir = fixture(oversized ? "source1-overlong-name" : "source1-legacy-mdl");
      CHECK(fs::write_file(fs::join(dir, kModelPath), model_bytes(49, false, 4, false, oversized ? std::string(1025, 'x') : legacy)));
      SourceContent content;
      ImportResult result;
      std::string error;
      CHECK(content.mount(dir, error));
      CHECK(!import_source1_model(content, kModelPath, result) && result.nodes.empty());
      CHECK(result.error.find(oversized ? "models/dummy.mdl: MDL material name 1 exceeds 1024 bytes"
                                        : "models/dummy.mdl: MDL material name 1 contains invalid UTF-8") != std::string::npos);
    }
    SourceContent empty;
    ImportResult result;
    CHECK(!import_source1_model(empty, "models/" + legacy + ".mdl", result));
    CHECK(result.error.find("models/shirt-\\xE9.mdl: MDL virtual path contains invalid UTF-8") != std::string::npos);
    CHECK(import_source1_material(empty, "materials/" + legacy + ".vmt", result));
    CHECK(has_note(result, "VMT virtual path 'materials/shirt-\\xE9.vmt' contains invalid UTF-8"));
  });

  test("classic Source materials: valid UTF-8 resolves while malformed texture and Patch paths are reported", [] {
    const std::string dir = fixture("source1-vmt-encoding");
    const std::string valid = "materials/models/dummy/color-\xc3\xa9.vtf";
    CHECK(fs::write_file(fs::join(dir, valid), color_vtf()));
    CHECK(fs::write_file(fs::join(dir, "materials/models/dummy/shirt.vmt"),
          "VertexLitGeneric { $basetexture " + valid + " $bumpmap models/dummy/legacy-" + char(0xe9) + " }"));
    CHECK(fs::write_file(fs::join(dir, "materials/models/dummy/bad-patch.vmt"),
          std::string("Patch { include materials/models/dummy/legacy-") + char(0xe9) + ".vmt }"));
    SourceContent content;
    ImportResult result;
    const bool ok = load_fixture(dir, content, result);
    CHECK(ok);
    if (!ok || result.materials.empty()) return;
    CHECK(!result.materials[0]->base_map.empty() && result.materials[0]->normal_map.empty());
    CHECK(has_note(result, "shirt.vmt: $bumpmap 'models/dummy/legacy-\\xE9' contains invalid UTF-8"));
    CHECK(import_source1_material(content, "materials/models/dummy/bad-patch.vmt", result));
    CHECK(has_note(result, "bad-patch.vmt: Patch include 'materials/models/dummy/legacy-\\xE9.vmt' contains invalid UTF-8"));
  });

  test("classic Source materials: SSBump is reported and explicit unsupported samplers name their sources", [] {
    for (bool in_vtf : {false, true}) {
      const std::string dir = fixture(in_vtf ? "source1-ssbump-flag" : "source1-ssbump-vmt");
      const std::string vmt = "LightmappedGeneric { $bumpmap models/dummy/tangent $envmap " +
          std::string(in_vtf ? "env_cubemap" : "reflect/room") + " $blendmodulatetexture wall/mask $phongwarptexture skin/ramp " +
          (in_vtf ? "}" : "$ssbump 1 }");
      CHECK(fs::write_file(fs::join(dir, "materials/models/dummy/shirt.vmt"), vmt));
      if (in_vtf) {
        Disk flagged(0);
        flagged.bytes = normal_vtf();
        flagged.u32(20, 0x08000000);
        CHECK(fs::write_file(fs::join(dir, "materials/models/dummy/tangent.vtf"), flagged.bytes));
      }
      SourceContent content;
      ImportResult result;
      CHECK(load_fixture(dir, content, result));
      CHECK(result.materials.size() == 1 && result.textures.empty());
      if (result.materials.size() != 1) continue;
      CHECK(result.materials[0]->normal_map.empty());
      CHECK(has_note(result, in_vtf ? "materials/models/dummy/tangent.vtf: unsupported VTF SSBump"
                                    : "unsupported $ssbump directional texture materials/models/dummy/tangent.vtf"));
      CHECK(has_note(result, in_vtf ? "unsupported runtime $envmap 'env_cubemap'" : "unsupported $envmap texture materials/reflect/room.vtf"));
      CHECK(!has_note(result, "env_cubemap.vtf"));
      CHECK(has_note(result, "unsupported $blendmodulatetexture texture materials/wall/mask.vtf"));
      CHECK(has_note(result, "unsupported $phongwarptexture texture materials/skin/ramp.vtf"));
    }
  });
  test("classic Source reimport: similar material names and identical textures keep their own stable identities", [] {
    const std::string dir = fixture("source1-stable-identities");
    const std::string first_dir = "materials/models/dummy/first", second_dir = "materials/models/dummy/second";
    const std::string first_vmt = first_dir + "/shirt.vmt", second_vmt = second_dir + "/shirt.vmt";
    const std::string first_vtf = first_dir + "/paint.vtf", second_vtf = second_dir + "/paint.vtf";
    for (const std::string &sub : {first_dir, second_dir}) {
      CHECK(fs::make_dirs(fs::join(dir, sub)));
      CHECK(fs::write_file(fs::join(dir, sub + "/shirt.vmt"), "VertexLitGeneric { $basetexture " + sub + "/paint }"));
      CHECK(fs::write_file(fs::join(dir, sub + "/paint.vtf"), color_vtf()));
    }
    CHECK(fs::write_file(fs::join(dir, first_vmt),
          "VertexLitGeneric { $basetexture " + first_vtf + " $bumpmap " + first_vtf + " }"));
    const char *cache_env = std::getenv("BLENDITY_CACHE_DIR");
    const std::string prior = cache_env ? cache_env : "";
    auto read = [&](const char *cache, const std::string &a, const std::string &b) {
      set_env("BLENDITY_CACHE_DIR", fs::join(test_dir(), cache));
      SourceContent mounted;
      std::string error;
      ImportResult out;
      out.source = kModelPath;
      CHECK(mounted.mount(dir, error));
      out.materials.push_back(import_source1_material(mounted, a, out));
      out.materials.push_back(import_source1_material(mounted, b, out));
      set_env("BLENDITY_CACHE_DIR", prior);
      return out;
    };
    const auto initial = read("identity-cache-a", first_vmt, second_vmt);
    const auto reordered = read("identity-cache-a", second_vmt, first_vmt);
    std::string changed = color_vtf();
    changed[92] ^= 1;  // edit the largest mip's first pixel
    CHECK(fs::write_file(fs::join(dir, first_vtf), changed));
    CHECK(fs::write_file(fs::join(dir, first_vmt),
          "VertexLitGeneric { $basetexture " + first_vtf + " $bumpmap " + first_vtf + " $alpha 0.75 }"));
    const auto edited = read("identity-cache-a", first_vmt, second_vmt);
    const auto moved = read("identity-cache-b", "Materials\\Models\\Dummy\\SECOND\\shirt.vmt", first_vmt);

    auto identity = [](const auto &table, const auto &lookup) {
      const auto it = table.find(lookup);
      return it != table.end() ? it->second : std::string();
    };
    auto material_id = [&](const ImportResult &result, size_t index) {
      return identity(result.source_material_ids, result.materials[index].get());
    };
    auto texture_id = [&](const ImportResult &result, size_t index, bool normal = false) {
      const Material &mat = *result.materials[index];
      return identity(result.source_texture_ids, normal ? mat.normal_map.path : mat.base_map.path);
    };
    CHECK(initial.materials[0]->name == initial.materials[1]->name);
    const std::string a = initial.materials[0]->base_map.path, b = initial.materials[1]->base_map.path;
    CHECK(!a.empty() && !b.empty() && a != b && fs::filename(a) == fs::filename(b));
    std::string pixels_a, pixels_b;
    CHECK(fs::read_file(a, pixels_a) && fs::read_file(b, pixels_b) && pixels_a == pixels_b);
    CHECK(initial.source_material_ids.size() == 2 && initial.source_texture_ids.size() == 3);
    CHECK(material_id(initial, 0) == "source1:vmt:" + first_vmt && material_id(initial, 1) == "source1:vmt:" + second_vmt);
    CHECK(texture_id(initial, 0) == "source1:vtf:" + first_vtf + ":color");
    CHECK(texture_id(initial, 1) == "source1:vtf:" + second_vtf + ":color");
    CHECK(texture_id(initial, 0, true) == "source1:vtf:" + first_vtf + ":normal");
    CHECK(material_id(reordered, 1) == material_id(initial, 0) && material_id(reordered, 0) == material_id(initial, 1));
    CHECK(texture_id(reordered, 1) == texture_id(initial, 0) && texture_id(reordered, 0) == texture_id(initial, 1));
    CHECK(material_id(edited, 0) == material_id(initial, 0) && texture_id(edited, 0) == texture_id(initial, 0));
    CHECK(edited.materials[0]->base_map.path != a);
    CHECK(material_id(moved, 0) == material_id(initial, 1) && material_id(moved, 1) == material_id(initial, 0));
    CHECK(texture_id(moved, 1) == texture_id(initial, 0) && texture_id(moved, 0) == texture_id(initial, 1));
    CHECK(moved.materials[1]->base_map.path != edited.materials[0]->base_map.path);
    Bitmap original_pixels, edited_pixels, moved_pixels;
    std::string error;
    const bool decoded = load_image(a, original_pixels, error) &&
                         load_image(edited.materials[0]->base_map.path, edited_pixels, error) &&
                         load_image(moved.materials[1]->base_map.path, moved_pixels, error);
    CHECK(decoded && original_pixels.rgba8.size() == 16 && edited_pixels.rgba8.size() == 16);
    if (decoded && original_pixels.rgba8.size() == 16 && edited_pixels.rgba8.size() == 16) {
      CHECK(original_pixels.rgba8[0] == 255 && edited_pixels.rgba8[0] == 254);
      CHECK(edited_pixels.rgba8 == moved_pixels.rgba8);
    }
  });

  test("classic Source reimport: fallback materials have identities before their textures resolve", [] {
    const std::string dir = fixture("source1-fallback-identities");
    SourceContent mounted;
    ImportResult missing;
    std::string error;
    CHECK(mounted.mount(dir, error));
    const std::string unresolved = "materials/models/dummy/missing.vmt";
    const auto fallback = import_source1_material(mounted, unresolved, missing);
    const auto before = missing.source_material_ids.find(fallback.get());
    CHECK(before != missing.source_material_ids.end());
    CHECK(fs::write_file(fs::join(dir, unresolved), "UnlitGeneric {}"));
    SourceContent updated;
    CHECK(updated.mount(dir, error));
    ImportResult supplied;
    const auto now = import_source1_material(updated, unresolved, supplied);
    const auto after = supplied.source_material_ids.find(now.get());
    CHECK(after != supplied.source_material_ids.end());
    if (before != missing.source_material_ids.end() && after != supplied.source_material_ids.end())
      CHECK(before->second == after->second && after->second == "source1:vmt:" + unresolved);
    CHECK(has_note(missing, unresolved) && supplied.notes.empty());

    Disk bare(0);
    bare.bytes = model_bytes(49, true);
    bare.u32(204, 0);  // textures
    bare.u32(212, 0);  // search folders
    bare.u32(220, 0);  // skin refs
    bare.u32(224, 0);  // skin families
    CHECK(fs::write_file(fs::join(dir, kModelPath), bare.bytes));
    SourceContent model_content;
    ImportResult model;
    const bool ok = load_fixture(dir, model_content, model);
    CHECK(ok && model.materials.size() == 1);
    if (ok && model.materials.size() == 1) {
      const auto identity = model.source_material_ids.find(model.materials.front().get());
      CHECK(identity != model.source_material_ids.end());
      if (identity != model.source_material_ids.end()) CHECK(identity->second == "source1:mdl:models/dummy.mdl:default");
    }
  });

  test("classic Source reimport: a model surface keeps its material identity when a new mount supplies it", [] {
    const std::string dir = fixture("source1-model-fallback");
    const std::string supplemental = fs::join(test_dir(), "source1-model-supplemental");
    const std::string material_path = "materials/models/dummy/panel/paint.vmt";
    CHECK(fs::write_file(fs::join(dir, kModelPath), model_bytes(49, false, 4, false, "models/dummy/panel/paint")));
    CHECK(fs::make_dirs(fs::join(supplemental, fs::parent(material_path))));
    CHECK(fs::write_file(fs::join(supplemental, material_path),
                         "UnlitGeneric { $basetexture models/dummy/panel/paint }"));
    CHECK(fs::write_file(fs::join(supplemental, "materials/models/dummy/panel/paint.vtf"), color_vtf()));
    SourceContent mounted;
    ImportResult missing, resolved;
    std::string error;
    const bool before = mounted.mount(dir, error) && import_source1_model(mounted, kModelPath, missing);
    const bool after = mounted.mount(supplemental, error) && import_source1_model(mounted, "MODELS\\DUMMY.MDL", resolved);
    CHECK(before && after && missing.materials.size() == 1 && resolved.materials.size() == 1);
    if (missing.materials.size() != 1 || resolved.materials.size() != 1) return;
    CHECK(has_note(missing, material_path) && resolved.notes.empty());
    const auto fallback = missing.source_material_ids.find(missing.materials[0].get());
    const auto supplied = resolved.source_material_ids.find(resolved.materials[0].get());
    CHECK(fallback != missing.source_material_ids.end() && supplied != resolved.source_material_ids.end());
    if (fallback != missing.source_material_ids.end() && supplied != resolved.source_material_ids.end())
      CHECK(fallback->second == "source1:vmt:" + material_path && supplied->second == fallback->second);
    CHECK(missing.materials[0]->base_map.empty() && !resolved.materials[0]->base_map.empty());
    CHECK(!resolved.nodes.empty() && resolved.nodes.back().materials == resolved.materials);
  });

  test("classic Source reimport: a relative MDL binding preserves an edited material through second-directory and direct fallback", [] {
    const std::string dir = fixture("source1-search-fallback");
    const std::string second_root = fs::join(test_dir(), "source1-search-second");
    const std::string direct_root = fs::join(test_dir(), "source1-search-direct");
    const std::string first_path = "materials/models/first/panel/paint.vmt";
    const std::string second_path = "materials/models/second/panel/paint.vmt";
    const std::string direct_path = "materials/panel/paint.vmt";
    CHECK(fs::write_file(fs::join(dir, kModelPath),
                         model_bytes(49, false, 4, false, "Panel\\Paint", {"models/first", "models/second"},
                                     "materials/models/dummy/shirt")));
    CHECK(fs::write_file(fs::join(dir, "models/dummy.dx90.vtx"), topology_bytes(true, 4, {}, true)));
    CHECK(fs::make_dirs(fs::join(second_root, fs::parent(second_path))));
    CHECK(fs::make_dirs(fs::join(direct_root, fs::parent(direct_path))));
    CHECK(fs::write_file(fs::join(second_root, second_path),
                         "UnlitGeneric { $color \"[0.25 0.5 0.75]\" $basetexture models/dummy/asym }"));
    CHECK(fs::write_file(fs::join(direct_root, direct_path),
                         "UnlitGeneric { $color \"[0.75 0.5 0.25]\" $basetexture models/dummy/asym }"));

    const char *current_project = std::getenv("BLENDITY_PROJECT");
    struct RestoreProject {
      std::string env, root;
      ~RestoreProject() {
        clear_material_assets();
        clear_texture_assets();
        set_env("BLENDITY_PROJECT", env);
        set_asset_root(root);
      }
    } restore{current_project ? current_project : "", asset_root()};
    const std::string project = fs::join(test_dir(), "source1-search-project");
    set_env("BLENDITY_PROJECT", project);
    Editor editor;
    editor.init_headless(600, 400);

    SourceContent initial_mount;
    ImportResult initial;
    const bool ok = load_fixture(dir, initial_mount, initial);
    CHECK(ok && initial.materials.size() == 2);
    if (!ok || initial.materials.size() != 2) return;
    SourceEntry entry;
    std::string error;
    if (!initial_mount.locate(kModelPath, entry, error)) { CHECK(false); return; }
    const auto id = initial.source_material_ids.find(initial.materials.front().get());
    CHECK(id != initial.source_material_ids.end());
    if (id == initial.source_material_ids.end()) return;
    const std::string binding = id->second;
    CHECK(binding == "source1:mdl:models/dummy.mdl:vmt:panel/paint.vmt");
    CHECK(has_note(initial, first_path));
    CHECK(editor.source_import_files_for_test(entry, initial, true, error));
    const std::string asset = initial.materials.front()->asset_path;
    const std::string textured_asset = initial.materials[1]->asset_path;
    const std::string texture = initial.materials[1]->base_map.path;
    CHECK(!asset.empty() && !textured_asset.empty() && asset != textured_asset);
    CHECK(texture.rfind("Assets/Textures/", 0) == 0 && fs::exists(editor.resolve_asset_path_for_test(texture)));
    if (asset.empty() || textured_asset.empty()) return;
    const std::string file = editor.resolve_asset_path_for_test(asset);
    initial.materials.front()->roughness = 0.375f;
    const std::string edited = save_material_text(*initial.materials.front());
    CHECK(fs::write_file(file, edited));
    const std::string assets = fs::join(project, "Assets");
    auto asset_files = [&](const char *extension) {
      auto files = fs::find_files(assets, extension);
      std::sort(files.begin(), files.end());
      return files;
    };
    const auto mat_files = asset_files(".mat"), png_files = asset_files(".png");
    CHECK(mat_files.size() == 2 && png_files.size() == 2);

    const std::string supplemental[]{second_root, direct_root};
    const float red[]{0.25f, 0.75f};
    for (size_t i = 0; i < 2; ++i) {
      SourceContent content = initial_mount;
      ImportResult retried;
      const bool resolved = content.mount(supplemental[i], error) && import_source1_model(content, kModelPath, retried);
      CHECK(resolved && retried.materials.size() == 2 && retried.notes.empty());
      if (!resolved || retried.materials.size() != 2) continue;
      CHECK_NEAR(retried.materials.front()->base_color.x, red[i], 1e-6f);
      CHECK(!retried.materials.front()->base_map.empty() &&
            retried.materials.front()->base_map.path == retried.materials[1]->base_map.path);
      const auto found = retried.source_material_ids.find(retried.materials.front().get());
      CHECK(found != retried.source_material_ids.end() && found->second == binding);
      clear_material_assets();  // Load the saved edit from disk on both reimports.
      CHECK(editor.source_import_files_for_test(entry, retried, true, error));
      CHECK(retried.materials.front()->asset_path == asset);
      CHECK_NEAR(retried.materials.front()->roughness, 0.375f, 1e-6f);
      CHECK(retried.materials[1]->asset_path == textured_asset && retried.materials[1]->base_map.path == texture);
      std::string bytes;
      CHECK(fs::read_file(file, bytes) && bytes == edited);
      CHECK(asset_files(".mat") == mat_files && asset_files(".png") == png_files);
    }
  });

  test("classic Source reimport: different invalid VMT references get distinct bounded fallback identities", [] {
    SourceContent empty;
    const std::string common = "materials/models/dummy/legacy";
    const std::string first = common + char(0xe9) + ".vmt", second = common + char(0xe8) + ".vmt";
    ImportResult result;
    result.source = kModelPath;
    const auto a = import_source1_material(empty, first, result);
    const auto b = import_source1_material(empty, second, result);
    const auto ai = result.source_material_ids.find(a.get()), bi = result.source_material_ids.find(b.get());
    CHECK(a && b && a != b && ai != result.source_material_ids.end() && bi != result.source_material_ids.end());
    if (ai == result.source_material_ids.end() || bi == result.source_material_ids.end()) return;
    CHECK(ai->second != bi->second);
    CHECK(ai->second.size() < 4096 && bi->second.size() < 4096);
    CHECK(has_note(result, "legacy\\xE9.vmt") && has_note(result, "legacy\\xE8.vmt"));
    ImportResult reordered;
    reordered.source = "MODELS\\DUMMY.MDL";
    const auto reverse_b = import_source1_material(empty, second, reordered);
    const auto reverse_a = import_source1_material(empty, first, reordered);
    CHECK(reordered.source_material_ids[reverse_a.get()] == ai->second);
    CHECK(reordered.source_material_ids[reverse_b.get()] == bi->second);
  });

  test("classic Source reimport: two logical surface bindings stay distinct when they load the same VMT", [] {
    const std::string dir = fixture("source1-aliased-vmt");
    CHECK(fs::write_file(fs::join(dir, kModelPath),
                         model_bytes(49, false, 4, false, "shirt", {"models/dummy"}, "models/dummy/shirt")));
    CHECK(fs::write_file(fs::join(dir, "models/dummy.dx90.vtx"), topology_bytes(true, 4, {}, true)));
    SourceContent content;
    ImportResult result;
    const bool ok = load_fixture(dir, content, result);
    CHECK(ok && result.materials.size() == 2 && result.source_material_ids.size() == 2);
    if (!ok || result.materials.size() != 2 || result.nodes.empty() || !result.nodes.back().mesh) return;
    const auto &a = result.materials[0], &b = result.materials[1];
    CHECK(a && b && a != b && a->name == b->name);
    if (!a || !b) return;
    CHECK(a->base_map.path == b->base_map.path && !a->base_map.empty());
    CHECK(result.source_material_ids[a.get()] == "source1:mdl:models/dummy.mdl:vmt:shirt.vmt");
    CHECK(result.source_material_ids[b.get()] == "source1:vmt:materials/models/dummy/shirt.vmt");
    CHECK(result.nodes.back().mesh->face_material == std::vector<int32_t>({0, 1}));
    const float second_roughness = b->roughness;
    a->roughness = 0.175f;
    CHECK(b->roughness == second_roughness);
  });
  test("classic Source materials: an earlier search directory wins over a later matching prefix", [] {
    const std::string root = fixture("source1-ordered-material");
    const std::string first_root = fs::join(test_dir(), "source1-ordered-first");
    const std::string first_path = "materials/models/first/panel/paint.vmt";
    const std::string direct_path = "materials/panel/paint.vmt";
    CHECK(fs::write_file(fs::join(root, kModelPath),
                         model_bytes(49, false, 4, false, "panel/paint", {"models/first", "panel"}, direct_path)));
    CHECK(fs::write_file(fs::join(root, "models/dummy.dx90.vtx"), topology_bytes(true, 4, {}, true)));
    CHECK(fs::make_dirs(fs::join(first_root, fs::parent(first_path))));
    CHECK(fs::make_dirs(fs::join(root, fs::parent(direct_path))));
    CHECK(fs::write_file(fs::join(first_root, first_path), "UnlitGeneric { $color \"[0.25 0.5 0.75]\" }"));
    CHECK(fs::write_file(fs::join(root, direct_path), "UnlitGeneric { $color \"[0.75 0.5 0.25]\" }"));

    for (bool first_present : {true, false}) {
      SourceContent mounted;
      std::string error;
      CHECK(mounted.mount(root, error));
      if (first_present) CHECK(mounted.mount(first_root, error));
      ImportResult result;
      const bool ok = import_source1_model(mounted, kModelPath, result);
      CHECK(ok && result.materials.size() == 2 && result.notes.empty());
      if (!ok || result.materials.size() != 2) continue;
      const auto &relative = result.materials[0], &rooted = result.materials[1];
      CHECK(relative && rooted && relative != rooted);
      if (!relative || !rooted) continue;
      CHECK_NEAR(relative->base_color.x, first_present ? 0.25f : 0.75f, 1e-6f);
      CHECK_NEAR(rooted->base_color.x, 0.75f, 1e-6f);
      CHECK(result.source_material_ids[relative.get()] == "source1:mdl:models/dummy.mdl:vmt:panel/paint.vmt");
      CHECK(result.source_material_ids[rooted.get()] == "source1:vmt:" + direct_path);
    }
  });
  if (own_cache) clear_env("BLENDITY_CACHE_DIR");
}

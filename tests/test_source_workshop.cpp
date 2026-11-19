// SPDX-License-Identifier: GPL-2.0-or-later
#include "test_common.h"

#include "../src/scene/source_content.h"

#include <iomanip>
#include <locale>
#include <numeric>
#include <optional>
#include <sstream>

namespace {

const std::string fixture_root = "tests/fixtures/source_workshop/layout";
const std::string classic_item = "steamapps/common/SourceFilmmaker/game/workshop";
const std::string cs2_game = "steamapps/common/Counter-Strike Global Offensive/game/csgo/pak01_dir.vpk";

struct Station {
  const char *test;
  const char *name;
  std::string item, dependency;
  const char *actor, *world;
  std::array<const char *, 4> lower_bones;
};

const std::array<Station, 3> stations{{
    {"Workshop SFM: loose item, ValveBiped controls, textured scene and detached reopen", "sfm", classic_item, {},
     "models/workshop_fixture/mannequin.mdl", "maps/workshop_fixture.bsp",
     {"ValveBiped.Bip01_L_Forearm", "ValveBiped.Bip01_R_Forearm", "ValveBiped.Bip01_L_Calf", "ValveBiped.Bip01_R_Calf"}},
    {"Workshop Alyx: packaged item, Valve limb names with helpers, textured scene and detached reopen", "alyx",
     "steamapps/workshop/content/546560/1/1.vpk", {},
     "models/workshop_fixture/mannequin.vmdl_c", "maps/workshop_fixture.vmap_c",
     {"arm_lower_L", "arm_lower_R", "leg_lower_L", "leg_lower_R"}},
    {"Workshop CS2: packaged item, mounted dependencies, agent controls, textured scene and detached reopen", "cs2",
     "steamapps/workshop/content/730/1", cs2_game,
     "models/workshop_fixture/mannequin.vmdl_c", "maps/workshop_fixture.vmap_c",
     {"arm_lower_L", "arm_lower_R", "leg_lower_L", "leg_lower_R"}},
}};

const std::array<const char *, 4> controls{{"LeftHandIK", "RightHandIK", "LeftFootIK", "RightFootIK"}};

struct WorkshopProject {
  std::string scratch;
  std::string old_project = std::getenv("BLENDITY_PROJECT") ? std::getenv("BLENDITY_PROJECT") : "";
  std::string old_cache = std::getenv("BLENDITY_CACHE_DIR") ? std::getenv("BLENDITY_CACHE_DIR") : "";
  std::string old_prefs = std::getenv("BLENDITY_PREFS_DIR") ? std::getenv("BLENDITY_PREFS_DIR") : "";
  std::string old_asset_root = asset_root();

  explicit WorkshopProject(const std::string &name) : scratch(fs::join(test_dir(), "workshop-" + name)) {
    const std::string project = fs::join(scratch, "Project");
    fs::make_dirs(fs::join(project, "Assets/Scenes"));
    set_env("BLENDITY_PROJECT", project);
    set_env("BLENDITY_CACHE_DIR", fs::join(scratch, "ImportCache"));
    set_env("BLENDITY_PREFS_DIR", fs::join(scratch, "ImportCache/Preferences"));
  }

  ~WorkshopProject() {
    texture_cache_clear();
    clear_texture_assets();
    clear_material_assets();
    set_asset_root(old_asset_root);
    set_env("BLENDITY_PROJECT", old_project);
    set_env("BLENDITY_CACHE_DIR", old_cache);
    set_env("BLENDITY_PREFS_DIR", old_prefs);
  }

  std::string source(const std::string &path) const {
    const std::string root = fs::join(scratch, "Installed");
    return path.empty() ? root : fs::join(root, path);
  }

  bool copy_source() const {
    std::error_code error;
    std::filesystem::copy(std::filesystem::u8path(fixture_root), std::filesystem::u8path(source("")),
                          std::filesystem::copy_options::recursive, error);
    if (error) std::printf("    fixture copy: %s\n", error.message().c_str());
    return !error;
  }

  bool detach(const std::string &folder) const {
    const std::string from = fs::join(scratch, folder);
    std::error_code error;
    std::filesystem::rename(std::filesystem::u8path(from),
                            std::filesystem::u8path(fs::join(scratch, folder + "-detached")), error);
    if (error) std::printf("    detach %s: %s\n", folder.c_str(), error.message().c_str());
    return !error && !fs::exists(from);
  }
};

void start_empty(Editor &ed) {
  ed.init_headless(1000, 740);
  ed.scene().clear();
  ed.mark_changed_for_test("Empty scratch scene");
  ed.record_buttons_for_test(true);
  ed.step_frame_headless();
}

bool click(Editor &ed, const std::string &label) {
  Recti r;
  if (!ed.button_rect_for_test(label, r) || r.w <= 0 || r.h <= 0) {
    std::printf("    Workshop control missing: %s\n", label.c_str());
    return false;
  }
  platform::Event move, down, up;
  move.type = platform::EventType::MouseMove;
  down.type = platform::EventType::MouseDown;
  up.type = platform::EventType::MouseUp;
  move.x = down.x = up.x = r.x + r.w / 2;
  move.y = down.y = up.y = r.y + r.h / 2;
  ed.step_frame_headless({move});
  ed.step_frame_headless({down});
  ed.step_frame_headless({up});
  ed.step_frame_headless();
  return true;
}

bool path_field(Editor &ed, const char *field, const std::string &path) {
  if (!click(ed, field)) return false;
  std::vector<platform::Event> text;
  for (const char32_t c : std::filesystem::u8path(path).u32string()) {
    platform::Event event;
    event.type = platform::EventType::Text;
    event.codepoint = uint32_t(c);
    text.push_back(event);
  }
  ed.step_frame_headless(text);
  return true;
}

bool import_item(Editor &ed, const std::string &item, const std::string &entry, const std::string &dependency = {}) {
  ed.command("import source");
  ed.step_frame_headless();
  if (!path_field(ed, "Source item path", item) || !click(ed, "Use Path")) return false;
  if (!dependency.empty() && (!path_field(ed, "Source root path", dependency) || !click(ed, "Add Root"))) return false;
  if (!click(ed, entry) || !click(ed, "Import")) return false;
  for (int i = 0; i < 3; ++i) ed.step_frame_headless();
  Recti with_notes;
  if (ed.button_rect_for_test("Import With Notes", with_notes) && with_notes.w > 0) {
    if (!click(ed, "Import With Notes")) return false;
    ed.step_frame_headless();
  }
  return true;
}

GameObject *actor_mesh(Scene &s) {
  GameObject *actor = nullptr;
  s.for_each([&](GameObject &g) {
    if (g.get<SkinModifier>() && g.mesh_filter() && g.mesh_filter()->mesh) actor = &g;
  });
  return actor;
}

bool has_texture(const GameObject &g) {
  const MeshRenderer *r = g.get<MeshRenderer>();
  if (!r) return false;
  const Material &material = *r->material(0);
  return material.asset_path.rfind("Assets/", 0) == 0 && fs::exists(resolve_asset_path(material.asset_path)) &&
         material.base_map.path.rfind("Assets/", 0) == 0 && fs::exists(resolve_asset_path(material.base_map.path));
}

void check_material_slots(const GameObject &g) {
  const MeshRenderer *renderer = g.mesh_renderer();
  const Mesh &mesh = *g.mesh_filter()->mesh;
  CHECK(renderer && renderer->materials.size() == 1);
  CHECK(mesh.face_count() > 0);
  bool slots_ok = true;
  for (size_t face = 0; face < mesh.face_count(); ++face) slots_ok &= mesh.material_of(face) == 0;
  CHECK(slots_ok);
}

void check_texel(const TextureRef &texture, const std::array<uint8_t, 4> &rgba, size_t pixel = 0) {
  CHECK(texture.path.rfind("Assets/", 0) == 0);
  Bitmap image;
  std::string error;
  const bool loaded = load_image(resolve_asset_path(texture.path), image, error);
  CHECK(loaded && !image.is_float && image.width == 4 && image.height == 4 && image.rgba8.size() == 64);
  if (image.rgba8.size() == 64)
    CHECK(std::equal(rgba.begin(), rgba.end(), image.rgba8.begin() + pixel * 4));
}

void check_actor_material(const GameObject &actor, bool modern) {
  CHECK(has_texture(actor));
  check_material_slots(actor);
  const MeshRenderer *renderer = actor.mesh_renderer();
  if (!renderer) return;
  const Material &m = *renderer->material(0);
  CHECK(m.surface == (int)MaterialSurface::Cutout);
  CHECK_NEAR(m.alpha_clip, .5f, 1e-5f);
  check_texel(m.base_map, {188, 42, 28, 0});
  check_texel(m.base_map, {244, 165, 43, 255}, 2);
  CHECK(m.normal_map.non_color);
  check_texel(m.normal_map, {128, uint8_t(modern ? 110 : 145), 251, 255});
  if (!modern) return;
  CHECK(m.roughness_map.non_color && m.metallic_map.non_color);
  check_texel(m.roughness_map, {105, 105, 105, 255});
  check_texel(m.metallic_map, {61, 61, 61, 255});
  check_texel(m.emission_map, {41, 25, 12, 255});
}

bool check_face_uv_at(const Mesh &mesh, Vec3 position, Vec2 uv, int axis) {
  if (mesh.uvs.size() != mesh.corner_count()) return false;
  for (size_t face = 0; face < mesh.face_count(); ++face) {
    bool on_plane = true, matches = false;
    for (size_t c = mesh.face_offsets[face]; c < mesh.face_offsets[face + 1]; ++c) {
      const Vec3 &p = mesh.positions[mesh.corner_verts[c]];
      on_plane &= std::fabs(p[axis] - position[axis]) < 1e-5f;
      matches |= length(p - position) < 1e-5f &&
                 std::fabs(mesh.uvs[c].x - uv.x) + std::fabs(mesh.uvs[c].y - uv.y) < 1e-5f;
    }
    if (on_plane && matches) return true;
  }
  return false;
}

void check_material_errors() {
  WorkshopProject sandbox("material-errors");
  const bool copied = sandbox.copy_source();
  CHECK(copied);
  if (!copied) return;
  const std::string root = sandbox.source(classic_item);
  const std::string material = fs::join(root, "materials/workshop_fixture/enamel.vmt");
  const std::string companion = fs::join(root, "models/workshop_fixture/mannequin.vvd");
  const std::string entry = stations[0].actor;
  const std::string vmt = "\"VertexLitGeneric\" { \"$basetexture\" \"workshop_fixture/missing\" \"$detail\" \"workshop_fixture/enamel\" }\n";
  CHECK(fs::write_file(material, vmt));
  SourceContent content;
  std::string error;
  CHECK(content.mount(root, error));
  ImportResult result;
  CHECK(import_source_entry(content, entry, result));
  const auto note_mentions = [&](const std::string &keyword) {
    return std::any_of(result.notes.begin(), result.notes.end(), [&](const std::string &note) {
      return note.find(keyword) != std::string::npos;
    });
  };
  CHECK(note_mentions("materials/workshop_fixture/missing.vtf"));
  CHECK(note_mentions("materials/workshop_fixture/enamel.vmt: unsupported $detail texture materials/workshop_fixture/enamel.vtf"));
  CHECK(fs::remove_file(companion));
  result = {};
  CHECK(!import_source_entry(content, entry, result));
  CHECK(result.error.find("models/workshop_fixture/mannequin.vvd") != std::string::npos);
}

void check_nested_packages() {
  WorkshopProject sandbox("nested");
  const bool copied = sandbox.copy_source();
  CHECK(copied);
  if (!copied) return;
  SourceContent content;
  std::string error;
  const bool mounted = content.mount(sandbox.source("steamapps/workshop/content/730/2"), error);
  CHECK(mounted);
  if (!mounted) return;
  const auto named_warning = [&](const std::string &path, const std::string &reason) {
    const auto &warnings = content.mount_warnings();
    return std::any_of(warnings.begin(), warnings.end(), [&](const std::string &warning) {
      return warning.find(path) != std::string::npos && warning.find(reason) != std::string::npos;
    });
  };
  CHECK(named_warning("maps/corrupt.vpk", "Invalid VPK directory"));
  CHECK(named_warning("maps/future.vpk", "Unsupported VPK version 99"));
  size_t map_count = 0;
  for (const SourceEntry &entry : content.entries()) {
    if (entry.virtual_path != "maps/workshop_fixture.vmap_c") continue;
    map_count++;
    ImportResult result;
    const bool ok = import_source_entry(content, entry, result);
    CHECK(ok);
    if (!ok) continue;
    float span = 0;
    for (const ImportedNode &node : result.nodes) {
      if (!node.mesh) continue;
      const AABB b = node.mesh->bounds();
      span = std::max(span, b.max.x - b.min.x);
    }
    const bool first = entry.provider.find("maps/first.vpk") != std::string::npos;
    CHECK(first || entry.provider.find("maps/second.vpk") != std::string::npos);
    CHECK(first ? span > 4.0f : span < .5f);
  }
  CHECK(map_count == 2);
}

std::vector<Vec3> pose_vertices(const GameObject &g) {
  const Mesh *m = g.evaluated_mesh();
  return m ? m->positions : std::vector<Vec3>{};
}

std::optional<float> largest_pose_delta(const std::vector<Vec3> &before, const std::vector<Vec3> &after) {
  if (before.empty() || before.size() != after.size()) return std::nullopt;
  float largest = 0;
  for (size_t i = 0; i < before.size(); ++i) {
    const float change = length(before[i] - after[i]);
    if (!finite_bits(change)) return std::nullopt;
    largest = std::max(largest, change);
  }
  return largest;
}

bool pose_moved(const std::vector<Vec3> &before, const std::vector<Vec3> &after, float minimum) {
  const auto change = largest_pose_delta(before, after);
  return change && *change > minimum;
}

bool pose_matches(const std::vector<Vec3> &before, const std::vector<Vec3> &after) {
  const auto change = largest_pose_delta(before, after);
  return change && *change < 1e-4f;
}

bool write_pose_reference(const std::string &path, const std::vector<Vec3> &vertices) {
  if (vertices.empty()) return false;
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::setprecision(std::numeric_limits<float>::max_digits10);
  for (const Vec3 &v : vertices) out << v.x << ' ' << v.y << ' ' << v.z << '\n';
  return fs::write_file(path, out.str());
}

std::vector<Vec3> read_pose_reference(const std::string &path) {
  std::string text;
  if (!fs::read_file(path, text)) return {};
  std::istringstream in(text);
  in.imbue(std::locale::classic());
  std::vector<Vec3> vertices;
  Vec3 v;
  while (in >> std::ws && !in.eof()) {
    if (!(in >> v.x >> v.y >> v.z)) return {};
    vertices.push_back(v);
  }
  return vertices;
}

bool pose_limb(Editor &ed, size_t i, const char *lower_name) {
  GameObject *target = ed.scene().find_by_name(controls[i]);
  GameObject *lower = ed.scene().find_by_name(lower_name);
  GameObject *actor = actor_mesh(ed.scene());
  if (!target || !lower || !actor) return false;
  const IKConstraint *ik = lower->get<IKConstraint>();
  CHECK(ik && ik->target == target->name && ik->chain_length == 2);
  const uint64_t actor_id = actor->id;
  const Mesh &rest = *actor->mesh_filter()->mesh;
  const std::vector<Vec3> before = pose_vertices(*actor);
  const Vec3 p = target->local().position + Vec3(0, i < 2 ? -.11f : .11f, .07f);
  ed.select_for_test(target->id, false);
  ed.command(strprintf("set Transform.Position %.8f %.8f %.8f", p.x, p.y, p.z));
  ed.step_frame_headless();
  const std::vector<Vec3> after = pose_vertices(*actor);
  if (before.size() != rest.vert_count() || after.size() != rest.vert_count()) return false;
  float weighted = 0, torso = 0;
  size_t weighted_vertices = 0, torso_vertices = 0;
  const int group = rest.find_group(lower_name);
  const bool weights_ok = group >= 0 && rest.groups_fit();
  CHECK(weights_ok);
  for (size_t v = 0; v < before.size(); ++v) {
    const float d = length(before[v] - after[v]);
    if (!finite_bits(d)) return false;
    if (weights_ok && rest.vertex_groups[(size_t)group].weights[v] > .8f) {
      weighted = std::max(weighted, d);
      weighted_vertices++;
    }
    const Vec3 &at = rest.positions[v];
    if (std::fabs(at.x) < .12f && at.y > 1.47f) {
      torso = std::max(torso, d);
      torso_vertices++;
    }
  }
  CHECK(weighted_vertices > 0 && torso_vertices > 0);
  CHECK(weighted > .025f && torso < 1e-4f);
  ed.command("undo");
  actor = ed.scene().find(actor_id);
  CHECK(actor && pose_matches(before, pose_vertices(*actor)));
  ed.command("redo");
  actor = ed.scene().find(actor_id);
  CHECK(actor && pose_matches(after, pose_vertices(*actor)));
  return true;
}

struct WorldMesh {
  uint64_t id;
  Vec3 center;
  Vec3 forward;
  MeshPtr mesh;
};

std::vector<WorldMesh> world_meshes(Scene &s, bool modern, uint64_t *floor_id = nullptr) {
  std::vector<WorldMesh> props;
  size_t floors = 0;
  std::string floor_texture, prop_texture;
  s.for_each([&](GameObject &g) {
    if (g.get<SkinModifier>() || !g.mesh_filter() || !g.mesh_filter()->mesh) return;
    const AABB b = g.world_bounds();
    if (!b.valid()) return;
    const bool floor = b.max.x - b.min.x > 4.0f;
    CHECK(has_texture(g));
    check_material_slots(g);
    const MeshRenderer *renderer = g.mesh_renderer();
    if (!renderer) return;
    const Material &material = *renderer->material(0);
    const Mesh &mesh = *g.mesh_filter()->mesh;
    if (floor) {
      floor_texture = material.base_map.path;
      check_texel(material.base_map, {25, 86, 121, 255});
      // The BSP projects UVs from the floor axes. The Source 2 slab stores its UVs per corner.
      CHECK(check_face_uv_at(mesh, Vec3(-90, -1, -80) * .0254f, modern ? Vec2(0, 1) : Vec2(-1, -.125f), 1));
      CHECK(check_face_uv_at(mesh, Vec3(90, -1, 90) * .0254f, modern ? Vec2(1, 0) : Vec2(1.125f, 2.125f), 1));
      if (floor_id) *floor_id = g.id;
      floors++;
      CHECK_NEAR(b.max.y, -.0254f, 1e-4f);
      return;
    }
    if (!prop_texture.empty()) CHECK(material.base_map.path == prop_texture);
    prop_texture = material.base_map.path;
    check_texel(material.base_map, {29, 151, 54, 255});
    CHECK(check_face_uv_at(mesh, Vec3(-9, 0, -7) * .0254f, {0, 1}, 0));
    CHECK(check_face_uv_at(mesh, Vec3(-9, 14, 7) * .0254f, {1, 0}, 0));
    props.push_back({g.id, b.center(), normalize(g.world_matrix().dir(Vec3(0, 0, 1))), g.mesh_filter()->mesh});
  });
  CHECK(floors == 1);
  CHECK(floor_texture != prop_texture);
  std::sort(props.begin(), props.end(), [](const WorldMesh &a, const WorldMesh &b) { return a.center.x < b.center.x; });
  CHECK(props.size() == 2);
  if (props.size() == 2) {
    CHECK(length(props[0].center - Vec3(-45, 7, 15) * .0254f) < .015f);
    CHECK(length(props[1].center - Vec3(42, 7, 25) * .0254f) < .015f);
    const float yaw = 41 * kDeg2Rad;
    CHECK(length(props[0].forward - Vec3(0, 0, 1)) < 1e-4f);
    CHECK(length(props[1].forward - Vec3(-std::sin(yaw), 0, std::cos(yaw))) < 1e-4f);
  }
  return props;
}

void setup_camera(Scene &s) {
  GameObject *cam = s.create("Workshop Camera");
  cam->add<Camera>();
  const Vec3 eye(2.4f, 2.55f, 4.5f);
  cam->set_world_position(eye);
  cam->set_world_rotation(transform_from_matrix(Mat4::look_at(eye, Vec3(0, .75f, .1f), Vec3(0, 1, 0)).inverse()).rotation);
  GameObject *lamp = s.create("Workshop Key");
  lamp->add<Light>()->intensity = 2.4f;
  lamp->set_local_euler({47, 205, 0});
  s.render.engine = 0;
  s.render.width = 192;
  s.render.height = 128;
}

std::vector<uint32_t> capture(Editor &ed) {
  ed.command("device cpu");
  ed.command("render wait");
  return ed.render_image_for_test().pixels;
}

void check_visibility(Editor &ed, GameObject *g, const std::vector<uint32_t> &full, size_t minimum) {
  MeshRenderer *r = g ? g->mesh_renderer() : nullptr;
  CHECK(r && r->show_in_renders);
  if (!r) return;
  r->show_in_renders = false;
  const std::vector<uint32_t> partial = capture(ed);
  r->show_in_renders = true;
  CHECK(partial.size() == full.size());
  const auto different = [](uint32_t a, uint32_t b) { return a != b; };
  if (partial.size() == full.size())
    CHECK(std::inner_product(full.begin(), full.end(), partial.begin(), size_t(0), std::plus<size_t>{}, different) >= minimum);
}

void check_detached_picture(const std::vector<uint32_t> &before, const std::vector<uint32_t> &after) {
  CHECK(before.size() == 192u * 128u && after.size() == before.size());
  if (after.size() != before.size() || before.empty()) return;
  double total = 0;
  for (size_t j = 0; j < after.size(); ++j)
    for (int shift : {0, 8, 16})
      total += std::abs(int((before[j] >> shift) & 255) - int((after[j] >> shift) & 255));
  CHECK(total / (3.0 * before.size()) < .5);
}

void check_manual_pose(Editor &ed, const char *joint_name) {
  GameObject *joint = ed.scene().find_by_name(joint_name);
  GameObject *actor = actor_mesh(ed.scene());
  CHECK(joint && actor);
  if (!joint || !actor) return;
  const uint64_t actor_id = actor->id;
  const std::vector<Vec3> rest = pose_vertices(*actor);
  ed.select_for_test(joint->id, false);
  ed.command("rotate 0 0 30");
  CHECK(pose_moved(rest, pose_vertices(*actor), .05f));
  ed.command("undo");
  actor = ed.scene().find(actor_id);
  CHECK(actor && pose_matches(rest, pose_vertices(*actor)));
}

void check_material_reimport(Editor &ed, const std::string &item, const Station &station, const std::string &dependency) {
  GameObject *actor = actor_mesh(ed.scene());
  if (!actor || !actor->mesh_renderer()) return;
  ed.select_for_test(actor->id, false);
  ed.command("matset Roughness .42");
  const MaterialPtr edited = actor->mesh_renderer()->material(0);
  CHECK_NEAR(edited->roughness, .42f, 1e-5f);
  save_dirty_material_assets();
  CHECK(import_item(ed, item, station.actor, dependency));
  size_t users = 0;
  ed.scene().for_each([&](GameObject &g) {
    const MeshRenderer *r = g.mesh_renderer();
    if (!g.get<SkinModifier>() || !r) return;
    const MaterialPtr &m = r->material(0);
    CHECK(m->asset_path == edited->asset_path && m->base_map.path == edited->base_map.path);
    CHECK_NEAR(m->roughness, .42f, 1e-5f);
    users++;
  });
  CHECK(users == 2);
  ed.command("undo");
}

void check_marker_rig(const Mesh &geometry) {
  Editor marker;
  start_empty(marker);
  GameObject *mesh = marker.scene().create("Unskinned mannequin");
  mesh->add<MeshFilter>()->mesh = std::make_shared<Mesh>(geometry);
  mesh->mesh_filter()->mesh->vertex_groups.clear();
  mesh->add<MeshRenderer>();
  const uint64_t mesh_id = mesh->id;
  marker.mark_changed_for_test("Unskinned Workshop mesh");
  marker.step_frame_headless();
  marker.select_for_test(mesh_id, false);
  marker.command("autorig humanoid");
  const std::array<const char *, 5> markers{{
      "Chin 0 1.50 0", "Groin 0 .91 0", "LeftWrist -.73 1.38 -.02",
      "LeftElbow -.48 1.37 -.045", "LeftKnee -.127 .508 .05",
  }};
  for (const char *position : markers) marker.command(std::string("autorig mark ") + position);
  CHECK(marker.autorig_next_for_test().empty());
  marker.command("autorig build");
  marker.step_frame_headless();
  mesh = marker.scene().find(mesh_id);
  CHECK(mesh && mesh->get<SkinModifier>() && !mesh->mesh_filter()->mesh->vertex_groups.empty());
  for (const char *control : controls) CHECK(marker.scene().find_by_name(control));
  marker.command("undo");
  mesh = marker.scene().find(mesh_id);
  CHECK(mesh && !mesh->get<SkinModifier>() && mesh->mesh_filter()->mesh->vertex_groups.empty());
  for (const char *control : controls) CHECK(!marker.scene().find_by_name(control));
  CHECK(mesh && pose_matches(geometry.positions, pose_vertices(*mesh)));
  marker.command("redo");
  mesh = marker.scene().find(mesh_id);
  const std::array<const char *, 4> lower_bones{{"LeftLowerArm", "RightLowerArm", "LeftLowerLeg", "RightLowerLeg"}};
  const std::array<const char *, 4> end_bones{{"LeftHand", "RightHand", "LeftFoot", "RightFoot"}};
  for (size_t i = 0; i < controls.size(); ++i) {
    GameObject *target = marker.scene().find_by_name(controls[i]);
    CHECK(mesh && mesh->get<SkinModifier>() && target);
    if (!mesh || !mesh->get<SkinModifier>() || !target) return;
    const Mesh &bound = *mesh->mesh_filter()->mesh;
    const int lower = bound.find_group(lower_bones[i]), end = bound.find_group(end_bones[i]);
    const bool groups_ok = lower >= 0 && end >= 0 && bound.groups_fit();
    CHECK(groups_ok);
    if (!groups_ok) return;

    const std::vector<Vec3> before = pose_vertices(*mesh);
    const Vec3 at = target->local().position + Vec3(0, i < 2 ? -.13f : .13f, .07f);
    marker.select_for_test(target->id, false);
    marker.command(strprintf("set Transform.Position %.8f %.8f %.8f", at.x, at.y, at.z));
    marker.step_frame_headless();
    const std::vector<Vec3> after = pose_vertices(*mesh);
    const bool counts_match = before.size() == bound.vert_count() && after.size() == before.size();
    CHECK(counts_match);
    if (!counts_match) return;

    float movement = 0;
    for (size_t v = 0; v < before.size(); ++v) {
      const float weight = bound.vertex_groups[size_t(lower)].weights[v] + bound.vertex_groups[size_t(end)].weights[v];
      if (weight > .5f) movement = std::max(movement, length(after[v] - before[v]));
    }
    if (movement <= .025f) std::printf("    Marker target %s moved its weighted vertices %.6f m\n", controls[i], movement);
    CHECK(movement > .025f && pose_moved(before, after, .025f));
    marker.command("undo");
    mesh = marker.scene().find(mesh_id);
    CHECK(mesh && pose_matches(before, pose_vertices(*mesh)));
    marker.command("redo");
    mesh = marker.scene().find(mesh_id);
    CHECK(mesh && pose_matches(after, pose_vertices(*mesh)));
  }
  if (!mesh) return;
  const std::vector<Vec3> posed = pose_vertices(*mesh);
  marker.command("save WorkshopMarker");
  const std::string path = marker.scene().path;
  CHECK(!path.empty() && marker.open_scene_for_test(path));
  mesh = marker.scene().find(mesh_id);
  CHECK(mesh && mesh->get<SkinModifier>() && pose_matches(posed, pose_vertices(*mesh)));
  for (const char *control : controls) CHECK(marker.scene().find_by_name(control));
}

std::vector<uint32_t> reference_pixels(const std::string &path) {
  Bitmap image;
  std::string error;
  if (!load_image(path, image, error) || image.is_float || image.rgba8.size() % 4 != 0) return {};
  std::vector<uint32_t> result;
  for (size_t i = 0; i < image.rgba8.size(); i += 4)
    result.push_back((uint32_t(image.rgba8[i + 3]) << 24) | (uint32_t(image.rgba8[i]) << 16) |
                     (uint32_t(image.rgba8[i + 1]) << 8) | uint32_t(image.rgba8[i + 2]));
  return result;
}

void cold_reopen(const WorkshopProject &sandbox, const Station &station) {
  CHECK(!fs::exists(sandbox.source("")) && !fs::exists(fs::join(sandbox.scratch, "ImportCache")));
  set_env("BLENDITY_PREFS_DIR", fs::join(sandbox.scratch, "ReopenedPreferences"));
  Editor reopened;
  reopened.init_headless(1000, 740);
  const std::string path = fs::join(sandbox.scratch, "Project/Assets/Scenes/Workshop.scene");
  CHECK(reopened.open_scene_for_test(path));
  const bool modern = fs::extension(station.actor) == ".vmdl_c";
  GameObject *actor = actor_mesh(reopened.scene());
  CHECK(actor && has_texture(*actor));
  if (actor) {
    CHECK(pose_matches(read_pose_reference(fs::join(sandbox.scratch, "posed-vertices.txt")), pose_vertices(*actor)));
    check_actor_material(*actor, modern);
  }
  for (const char *name : controls) CHECK(reopened.scene().find_by_name(name));
  uint64_t floor = 0;
  world_meshes(reopened.scene(), modern, &floor);
  const std::vector<uint32_t> picture = capture(reopened);
  check_detached_picture(reference_pixels(fs::join(sandbox.scratch, "before.png")), picture);
  check_visibility(reopened, actor, picture, 25);
  check_visibility(reopened, reopened.scene().find(floor), picture, 100);
}

void run_title(const Station &station) {
  WorkshopProject sandbox(station.name);
  const bool modern = fs::extension(station.actor) == ".vmdl_c";
  const char *cold = std::getenv("BLENDITY_WORKSHOP_COLD_REOPEN");
  if (cold && std::string(cold) == "1") {
    cold_reopen(sandbox, station);
    return;
  }
  const bool ready = sandbox.copy_source();
  CHECK(ready);
  if (!ready) return;
  std::vector<Vec3> posed;
  std::vector<uint32_t> picture;
  MeshPtr geometry;
  std::string path;
  {
    Editor ed;
    start_empty(ed);
    const std::string item = sandbox.source(station.item);
    const std::string dependency = station.dependency.empty() ? "" : sandbox.source(station.dependency);
    CHECK(import_item(ed, item, station.actor, dependency));
    GameObject *actor = actor_mesh(ed.scene());
    CHECK(actor && has_texture(*actor));
    if (!actor) return;
    const uint64_t actor_id = actor->id;
    const Mesh &rest = *actor->mesh_filter()->mesh;
    const std::vector<Vec3> bind = pose_vertices(*actor);
    CHECK(pose_matches(rest.positions, bind));
    CHECK_NEAR(rest.bounds().max.y, 67 * .0254f, 1e-4f);
    CHECK_NEAR(rest.bounds().min.z, -5 * .0254f, 1e-4f);
    CHECK(check_face_uv_at(rest, Vec3(-8, 36, -4) * .0254f, {0, 1}, 0));
    CHECK(check_face_uv_at(rest, Vec3(-8, 58, 4) * .0254f, {1, 0}, 0));
    check_actor_material(*actor, modern);
    check_manual_pose(ed, station.lower_bones[0]);
    check_material_reimport(ed, item, station, dependency);
    actor = ed.scene().find(actor_id);
    CHECK(actor && actor->get<SkinModifier>());
    if (!actor || !actor->get<SkinModifier>()) return;
    GameObject *root = skin_armature_of(*actor, actor->get<SkinModifier>()->object);
    CHECK(root && root->get<Armature>());
    if (!root) return;
    ed.select_for_test(root->id, false);
    ed.command("rig autoik");
    for (const char *name : controls) CHECK(ed.scene().find_by_name(name));
    ed.command("undo");
    for (const char *name : controls) CHECK(!ed.scene().find_by_name(name));
    ed.command("redo");
    for (size_t i = 0; i < controls.size(); ++i) CHECK(pose_limb(ed, i, station.lower_bones[i]));
    actor = ed.scene().find(actor_id);
    if (!actor) return;
    posed = pose_vertices(*actor);
    CHECK(pose_moved(bind, posed, .06f));
    CHECK(write_pose_reference(fs::join(sandbox.scratch, "posed-vertices.txt"), posed));
    geometry = actor->mesh_filter()->mesh;
    CHECK(import_item(ed, item, station.world, dependency));
    uint64_t floor = 0;
    const std::vector<WorldMesh> props = world_meshes(ed.scene(), modern, &floor);
    if (props.size() == 2) {
      CHECK(props[0].mesh == props[1].mesh);
      const std::string unchanged = save_scene_text(ed.scene());
      ed.select_for_test(props[0].id, false);
      ed.command("rig autoik");
      CHECK(save_scene_text(ed.scene()) == unchanged);
    }
    setup_camera(ed.scene());
    picture = capture(ed);
    CHECK(!picture.empty());
    if (picture.size() == 192u * 128u)
      CHECK(write_png(fs::join(sandbox.scratch, "before.png"), picture.data(), 192, 128, 192));
    check_visibility(ed, actor, picture, 25);
    check_visibility(ed, ed.scene().find(floor), picture, 100);
    ed.command("save Workshop");
    path = ed.scene().path;
    CHECK(!path.empty() && fs::exists(path));
  }
  const bool source_detached = sandbox.detach("Installed");
  const bool cache_detached = sandbox.detach("ImportCache");
  CHECK(source_detached && cache_detached);
  if (!source_detached || !cache_detached || path.empty()) return;
  texture_cache_clear();
  clear_material_assets();
  clear_texture_assets();
  set_env("BLENDITY_PREFS_DIR", fs::join(sandbox.scratch, "ReopenedPreferences"));
  Editor reopened;
  reopened.init_headless(1000, 740);
  CHECK(reopened.open_scene_for_test(path));
  GameObject *actor = actor_mesh(reopened.scene());
  CHECK(actor && has_texture(*actor));
  if (actor) {
    CHECK(pose_matches(pose_vertices(*actor), posed));
    check_actor_material(*actor, modern);
  }
  for (const char *name : controls) CHECK(reopened.scene().find_by_name(name));
  world_meshes(reopened.scene(), modern);
  check_detached_picture(picture, capture(reopened));
  if (geometry) check_marker_rig(*geometry);
}

void scout_names() {
  const char *cold = std::getenv("BLENDITY_WORKSHOP_COLD_REOPEN");
  if (cold && std::string(cold) == "1") return;
  WorkshopProject sandbox("scout");
  Editor ed;
  start_empty(ed);
  CHECK(import_item(ed, fs::join(fixture_root, classic_item), "models/workshop_fixture/mannequin_scout_names.mdl"));
  GameObject *actor = actor_mesh(ed.scene());
  CHECK(actor);
  if (!actor) return;
  GameObject *root = skin_armature_of(*actor, actor->get<SkinModifier>()->object);
  CHECK(root);
  if (!root) return;
  ed.select_for_test(root->id, false);
  ed.command("rig autoik");
  const std::array<const char *, 4> lower{{"bip_lowerArm_L", "bip_lowerArm_R", "bip_knee_L", "bip_knee_R"}};
  for (size_t i = 0; i < lower.size(); ++i) CHECK(pose_limb(ed, i, lower[i]));
}

void prop_armature_names() {
  WorkshopProject sandbox("alarm-prop");
  Editor ed;
  start_empty(ed);
  Scene &scene = ed.scene();
  GameObject *root = scene.create("Alarm Rig");
  root->add<Armature>();
  GameObject *upper = scene.create("Alarm_Upper_L", root);
  upper->add<Bone>()->set_rest(upper->local());
  GameObject *lower = scene.create("Alarm_Lower_L", upper);
  lower->set_local_position({0, 1, 0});
  lower->add<Bone>()->set_rest(lower->local());
  ed.select_for_test(root->id, false);
  ed.mark_changed_for_test("Alarm prop armature");
  ed.step_frame_headless();

  const std::string before = save_scene_text(scene);
  ed.command("rig autoik");
  CHECK(!lower->get<IKConstraint>());
  CHECK(!scene.find_by_name("LeftHandIK") && !scene.find_by_name("LeftElbowPole"));
  CHECK(save_scene_text(scene) == before);
}

}  // namespace

void test_source_workshop() {
  for (const Station &station : stations)
    test(station.test, [&] { run_title(station); });
  test("Workshop SFM: Scout's side-specific hip and lower arm names drive all four limbs", scout_names);
  test("Workshop Auto IK: an alarm prop's two-bone armature keeps its existing controls", prop_armature_names);
  test("Workshop: missing companions and unsupported classic shader features name the source path", check_material_errors);
  test("Workshop CS2: duplicate nested map entries stay isolated and invalid children name their package", check_nested_packages);
}

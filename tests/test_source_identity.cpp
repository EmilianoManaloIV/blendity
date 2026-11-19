// SPDX-License-Identifier: GPL-2.0-or-later
#include "test_common.h"
#include "../src/core/hash.h"

namespace {

namespace disk = std::filesystem;
using Pixel = std::array<uint8_t, 4>;
using Samples = std::array<Pixel, 2>;

const char *const map_entry = "maps/workshop_fixture.vmap_c";
const char *const scene_path = "Assets/Scenes/WorkshopIdentity.scene";
const Samples original{{{40, 80, 120, 255}, {84, 124, 164, 255}}};
const Samples user_edited_floor{{{197, 36, 155, 255}, {223, 56, 185, 255}}};
const std::array<Samples, 2> updated{{
    {{{16, 102, 154, 255}, {25, 117, 168, 255}}},
    {{{160, 66, 27, 255}, {179, 87, 43, 255}}},
}};
constexpr std::array<float, 2> edited_roughness{{.37f, .73f}};

const Samples &project_samples(size_t slot) { return slot == 0 ? user_edited_floor : original; }

struct AssetPaths {
  std::string material, texture;
  bool operator==(const AssetPaths &other) const { return material == other.material && texture == other.texture; }
};

struct Placement {
  uint64_t id;
  size_t region;
  AssetPaths assets;
};

std::string env(const char *name) {
  const char *value = std::getenv(name);
  return value ? value : "";
}

class ProjectScope {
  std::string old_project_ = env("BLENDITY_PROJECT");
  std::string old_asset_root_ = asset_root();

 public:
  explicit ProjectScope(const std::string &path) { set_env("BLENDITY_PROJECT", path); }
  ~ProjectScope() {
    texture_cache_clear();
    clear_material_assets();
    clear_texture_assets();
    set_asset_root(old_asset_root_);
    set_env("BLENDITY_PROJECT", old_project_);
  }
};

bool click(Editor &editor, const std::string &label) {
  Recti rect;
  if (!editor.button_rect_for_test(label, rect) || rect.w <= 0 || rect.h <= 0) {
    std::printf("    Picker control missing: %s\n", label.c_str());
    return false;
  }
  platform::Event pointer;
  pointer.x = rect.x + rect.w / 2;
  pointer.y = rect.y + rect.h / 2;
  for (const auto event : {platform::EventType::MouseMove, platform::EventType::MouseDown, platform::EventType::MouseUp}) {
    pointer.type = event;
    editor.step_frame_headless({pointer});
  }
  editor.step_frame_headless();
  return true;
}

bool import_map(Editor &editor, const std::string &path) {
  editor.command("import source");
  editor.step_frame_headless();
  if (!click(editor, "Source item path")) return false;
  std::vector<platform::Event> chars;
  for (char32_t point : disk::u8path(path).u32string()) {
    platform::Event event;
    event.type = platform::EventType::Text;
    event.codepoint = uint32_t(point);
    chars.push_back(event);
  }
  editor.step_frame_headless(chars);
  if (!click(editor, "Use Path") || !click(editor, map_entry) || !click(editor, "Import")) return false;
  for (int i = 0; i < 4; ++i) editor.step_frame_headless();
  Recti rect;
  if (editor.button_rect_for_test("Import With Notes", rect) && rect.w > 0) {
    std::printf("    The identity fixture import produced diagnostics.\n");
    return false;
  }
  return true;
}

void expect_samples(const std::string &relative, const Samples &samples) {
  Bitmap image;
  std::string error;
  const bool ok = load_image(resolve_asset_path(relative), image, error);
  if (!ok) std::printf("    Could not decode %s: %s\n", relative.c_str(), error.c_str());
  CHECK(ok && image.width == 4 && image.height == 4 && !image.is_float && image.rgba8.size() == 64);
  if (!ok || image.rgba8.size() != 64) return;
  for (size_t i = 0; i < samples.size(); ++i) {
    const auto pixel = image.rgba8.begin() + i * 8;
    if (!std::equal(samples[i].begin(), samples[i].end(), pixel))
      std::printf("    %s x=%zu y=0: got RGBA (%u, %u, %u, %u), expected (%u, %u, %u, %u)\n",
                  relative.c_str(), i * 2, unsigned(pixel[0]), unsigned(pixel[1]), unsigned(pixel[2]), unsigned(pixel[3]),
                  unsigned(samples[i][0]), unsigned(samples[i][1]), unsigned(samples[i][2]), unsigned(samples[i][3]));
    CHECK(std::equal(samples[i].begin(), samples[i].end(), pixel));
  }
}

void expect_live_samples(Scene &scene, uint64_t object_id, const AssetPaths &assets, const Samples &samples) {
  const GameObject *object = scene.find(object_id);
  const MeshRenderer *renderer = object ? object->mesh_renderer() : nullptr;
  const MaterialPtr material = renderer && renderer->materials.size() == 1 ? renderer->materials[0] : nullptr;
  CHECK(material && material->asset_path == assets.material && material->base_map.path == assets.texture);
  if (!material) return;

  // The renderers resolve and retain these mip levels through Material::textures().
  const TexturePtr texture = material->textures().base;
  const bool image_ok = texture && !texture->is_float && texture->width == 4 && texture->height == 4 &&
                        !texture->levels.empty() && texture->levels[0].px8.size() == 16;
  CHECK(image_ok);
  if (!image_ok) return;
  for (size_t i = 0; i < samples.size(); ++i) {
    const Pixel &pixel = samples[i];
    const uint32_t expected = uint32_t(pixel[0]) | uint32_t(pixel[1]) << 8 |
                              uint32_t(pixel[2]) << 16 | uint32_t(pixel[3]) << 24;
    const uint32_t actual = texture->levels[0].px8[i * 2];
    if (actual != expected)
      std::printf("    Live renderer texture %s x=%zu y=0: got 0x%08x, expected 0x%08x\n",
                  assets.texture.c_str(), i * 2, unsigned(actual), unsigned(expected));
    CHECK(actual == expected);
  }
}

std::vector<Placement> inspect_scene(Scene &scene, size_t imports, bool new_pixels, bool edited,
                                     const std::array<AssetPaths, 2> *expected = nullptr) {
  std::vector<Placement> result;
  std::array<size_t, 3> counts{};
  scene.for_each([&](GameObject &object) {
    const MeshFilter *filter = object.mesh_filter();
    if (!filter || !filter->mesh) return;
    const AABB bounds = object.world_bounds();
    CHECK(bounds.valid());
    if (!bounds.valid()) return;
    const bool floor = bounds.max.x - bounds.min.x > 4;
    const size_t region = floor ? 0 : (bounds.center().x < 0 ? 1 : 2);
    if (!floor) CHECK_NEAR(bounds.center().x, (region == 1 ? -45 : 42) * .0254f, .015f);
    else CHECK_NEAR(bounds.max.y, -.0254f, .0001f);
    counts[region]++;

    const MeshRenderer *renderer = object.mesh_renderer();
    CHECK(renderer && renderer->materials.size() == 1 && renderer->materials[0]);
    if (!renderer || renderer->materials.size() != 1 || !renderer->materials[0]) return;
    const Mesh &mesh = *filter->mesh;
    CHECK(mesh.face_count() > 0);
    bool slot_zero = true;
    for (size_t i = 0; i < mesh.face_count(); ++i) slot_zero &= mesh.material_of(i) == 0;
    CHECK(slot_zero);

    const Material &material = *renderer->materials[0];
    const AssetPaths refs{material.asset_path, material.base_map.path};
    CHECK(refs.material.rfind("Assets/Materials/", 0) == 0 && fs::exists(resolve_asset_path(refs.material)));
    CHECK(refs.texture.rfind("Assets/Textures/", 0) == 0 && fs::exists(resolve_asset_path(refs.texture)));
    const size_t slot = floor ? 0 : 1;
    if (expected && !(refs == (*expected)[slot])) {
      std::printf("    %s: got %s and %s, expected %s and %s\n", object.name.c_str(), refs.material.c_str(),
                  refs.texture.c_str(), (*expected)[slot].material.c_str(), (*expected)[slot].texture.c_str());
    }
    if (expected) CHECK(refs == (*expected)[slot]);
    if (edited) CHECK_NEAR(material.roughness, edited_roughness[slot], .00001f);
    expect_samples(refs.texture, new_pixels ? updated[slot] : (edited ? project_samples(slot) : original));
    result.push_back({object.id, region, refs});
  });
  const std::array<size_t, 3> expected_counts{{imports, imports, imports}};
  CHECK(counts == expected_counts);
  CHECK(result.size() == 3 * imports);
  std::sort(result.begin(), result.end(), [](const Placement &a, const Placement &b) { return a.region < b.region; });
  return result;
}

bool relocation_record(const std::string &project, const std::string &path) {
  const std::string directory = "Assets/Materials/workshop_fixture/.source-assets";
  if (fs::parent(path) != directory) return false;
  const std::string name = fs::filename(path);
  if (name.size() != 18 || (name[0] != 'p' && name[0] != 'f') || name[1] != '_') return false;

  std::string record;
  if (!fs::read_file(fs::join(project, path), record)) return false;
  const size_t newline = record.find('\n');
  if (newline == std::string::npos || record.substr(newline + 1) != map_entry) return false;
  const std::string key = record.substr(0, newline);
  if (name != strprintf("%c_%016llx", name[0], (unsigned long long)fnv1a(key.data(), key.size()))) return false;
  const size_t separator = key.find('\t');
  if (name[0] == 'p' && separator != std::string::npos) return false;
  // Both pinned fixtures carry identical bytes for the selected .vmap_c entry.
  if (name[0] == 'f' && (separator == std::string::npos || key.substr(separator + 1) != "package:7c0d530f:81"))
    return false;

  const std::string provider = fs::normalize(key.substr(0, separator));
  const std::string root = fs::parent(project);
  for (const char *relative : {"Installed-A/WorkshopIdentity.vpk", "Installed-B/WorkshopIdentity.vpk", "Installed-C/MovedCopy.vpk"})
    if (provider == fs::normalize(fs::join(root, relative))) return true;
  return false;
}

std::string asset_inventory(const std::string &project, const std::array<AssetPaths, 2> &assets) {
  std::vector<std::string> inventory;
  std::set<std::string> mat_files, tex_files;
  const disk::path root = disk::u8path(project);
  std::error_code ec;
  for (disk::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
    const auto path = it->path().lexically_relative(root).generic_u8string();
    const auto kind = it->symlink_status(ec);
    if (ec) break;
    const bool folder = disk::is_directory(kind);
    if (disk::is_regular_file(kind) && relocation_record(project, path)) continue;
    inventory.push_back(path + (folder ? "/" : ""));
    CHECK(path.rfind(".blendity-source-import-", 0) != 0);
    if (!folder && path.rfind("Assets/Materials/", 0) == 0 && fs::extension(path) == ".mat") mat_files.insert(path);
    if (!folder && path.rfind("Assets/Textures/", 0) == 0 && image_extension_supported(fs::extension(path))) tex_files.insert(path);
  }
  CHECK(!ec);
  const std::set<std::string> expected_mats{assets[0].material, assets[1].material};
  const std::set<std::string> expected_tex{assets[0].texture, assets[1].texture};
  CHECK(mat_files.size() == 2 && mat_files == expected_mats);
  CHECK(tex_files.size() == 2 && tex_files == expected_tex);
  for (size_t slot = 0; slot < assets.size(); ++slot) {
    std::string text;
    Material stored;
    const auto &entry = assets[slot];
    const bool decoded = fs::read_file(fs::join(project, entry.material), text) && load_material_text(text, stored);
    if (!decoded) std::printf("    Could not read native material %s\n", entry.material.c_str());
    CHECK(decoded);
    if (!decoded) continue;
    CHECK(stored.base_map.path == entry.texture);
    CHECK_NEAR(stored.roughness, edited_roughness[slot], .00001f);
  }
  std::sort(inventory.begin(), inventory.end());
  std::string text;
  for (const auto &path : inventory) text += path + "\n";
  return text;
}

void check_inventory(const std::string &project, const std::array<AssetPaths, 2> &assets, const std::string &expected) {
  const std::string actual = asset_inventory(project, assets);
  if (actual != expected) {
    auto paths_in = [](const std::string &inventory) {
      std::istringstream input(inventory);
      std::set<std::string> paths;
      for (std::string path; std::getline(input, path);) paths.insert(std::move(path));
      return paths;
    };
    const auto before = paths_in(expected), after = paths_in(actual);
    for (const std::string &path : after)
      if (!before.count(path)) std::printf("    Added project path: %s\n", path.c_str());
    for (const std::string &path : before)
      if (!after.count(path)) std::printf("    Removed project path: %s\n", path.c_str());
  }
  CHECK(actual == expected);
}

bool read_baseline(const std::string &path, std::array<AssetPaths, 2> &assets, std::string &inventory) {
  std::string record;
  if (!fs::read_file(path, record)) return false;
  std::istringstream stream(record);
  for (AssetPaths &entry : assets)
    if (!std::getline(stream, entry.material) || !std::getline(stream, entry.texture)) return false;
  inventory.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
  return !inventory.empty();
}

std::string absolute_scene(const std::string &project) { return fs::join(project, scene_path); }

void edit_project_png(const std::string &path) {
  std::array<uint32_t, 16> pixels{};
  for (size_t i = 0; i < pixels.size(); ++i) {
    const Pixel &rgba = user_edited_floor[i % 4 / 2];
    pixels[i] = uint32_t(rgba[3]) << 24 | uint32_t(rgba[0]) << 16 | uint32_t(rgba[1]) << 8 | uint32_t(rgba[2]);
  }
  CHECK(write_png(resolve_asset_path(path), pixels.data(), 4, 4, 4, true));
}

std::string png_reference(const std::string &baseline, size_t slot) {
  return baseline + (slot == 0 ? ".floor.png" : ".prop.png");
}

void check_png_bytes(const std::string &baseline, const std::array<AssetPaths, 2> &assets) {
  for (size_t slot = 0; slot < assets.size(); ++slot) {
    std::string expected, actual;
    CHECK(fs::read_file(png_reference(baseline, slot), expected) && !expected.empty());
    CHECK(fs::read_file(resolve_asset_path(assets[slot].texture), actual));
    if (actual != expected)
      std::printf("    Project PNG bytes changed at %s\n", assets[slot].texture.c_str());
    CHECK(!actual.empty() && actual == expected);
  }
}

void save(Editor &editor, const std::string &project) {
  editor.command("save WorkshopIdentity");
  editor.step_frame_headless();
  CHECK(fs::normalize(editor.scene().path) == fs::normalize(absolute_scene(project)));
  CHECK(fs::exists(absolute_scene(project)));
}

void seed(Editor &editor, const std::string &project, const std::string &baseline, const std::string &item) {
  editor.scene().clear();
  editor.mark_changed_for_test("Empty identity scratch scene");
  editor.step_frame_headless();
  const bool imported = import_map(editor, item);
  CHECK(imported);
  if (!imported) return;
  const auto initial = inspect_scene(editor.scene(), 1, false, false);
  if (initial.size() != 3) return;
  CHECK(initial[0].region == 0 && initial[1].region == 1 && initial[2].region == 2);
  const std::array<AssetPaths, 2> assets{{initial[0].assets, initial[1].assets}};
  CHECK(initial[2].assets == assets[1]);
  CHECK(assets[0].material != assets[1].material && assets[0].texture != assets[1].texture);
  std::string floor_bytes, prop_bytes;
  CHECK(fs::read_file(resolve_asset_path(assets[0].texture), floor_bytes));
  CHECK(fs::read_file(resolve_asset_path(assets[1].texture), prop_bytes));
  CHECK(!floor_bytes.empty() && floor_bytes == prop_bytes);

  for (size_t slot = 0; slot < assets.size(); ++slot) {
    editor.select_for_test(initial[slot].id, false);
    editor.command(slot == 0 ? "matset Roughness .37" : "matset Roughness .73");
    editor.step_frame_headless();
  }
  save_dirty_material_assets();
  // Simulate a user's own image edit at the existing project path, keeping the prop PNG as imported.
  edit_project_png(assets[0].texture);
  inspect_scene(editor.scene(), 1, false, true, &assets);
  save(editor, project);
  std::string edited_bytes, untouched_bytes;
  CHECK(fs::read_file(resolve_asset_path(assets[0].texture), edited_bytes));
  CHECK(fs::read_file(resolve_asset_path(assets[1].texture), untouched_bytes));
  CHECK(!edited_bytes.empty() && edited_bytes != floor_bytes && untouched_bytes == prop_bytes);
  CHECK(fs::write_file(png_reference(baseline, 0), edited_bytes));
  CHECK(fs::write_file(png_reference(baseline, 1), untouched_bytes));
  std::string record;
  for (const AssetPaths &pair : assets) record += pair.material + "\n" + pair.texture + "\n";
  CHECK(fs::write_file(baseline, record + asset_inventory(project, assets)));
}

void recheck(Editor &editor, const std::string &phase, const std::string &project,
             const std::string &baseline, const std::string &item, bool refresh_textures) {
  std::array<AssetPaths, 2> assets;
  std::string inventory;
  const bool reopened = read_baseline(baseline, assets, inventory) && editor.open_scene_for_test(absolute_scene(project));
  CHECK(reopened);
  if (!reopened) return;

  const size_t prior_count = phase == "offline-v2" ? 3 : (phase == "repeat-v2" ? 2 : 1);
  const bool prior_v2 = refresh_textures && (phase == "offline-v2" || phase == "repeat-v2");
  const auto prior = inspect_scene(editor.scene(), prior_count, prior_v2, true, &assets);
  check_inventory(project, assets, inventory);
  if (!prior_v2) check_png_bytes(baseline, assets);

  std::array<uint64_t, 2> live_ids{};
  for (size_t slot = 0; slot < assets.size(); ++slot) {
    const auto found = std::find_if(prior.begin(), prior.end(), [&](const Placement &p) { return p.region == slot; });
    CHECK(found != prior.end());
    if (found == prior.end()) return;
    live_ids[slot] = found->id;
    expect_live_samples(editor.scene(), found->id, assets[slot], prior_v2 ? updated[slot] : project_samples(slot));
  }
  if (item.empty()) return;
  const bool imported = import_map(editor, item);
  CHECK(imported);
  if (!imported) return;
  for (size_t slot = 0; slot < assets.size(); ++slot)
    expect_live_samples(editor.scene(), live_ids[slot], assets[slot], refresh_textures ? updated[slot] : project_samples(slot));
  const auto all = inspect_scene(editor.scene(), prior_count + 1, refresh_textures, true, &assets);
  for (const Placement &old : prior) {
    CHECK(std::any_of(all.begin(), all.end(), [&](const Placement &now) {
      return old.id == now.id && old.region == now.region && old.assets == now.assets;
    }));
  }
  check_inventory(project, assets, inventory);
  if (!refresh_textures) check_png_bytes(baseline, assets);
  save(editor, project);
  check_inventory(project, assets, inventory);
  if (!refresh_textures) check_png_bytes(baseline, assets);
}

}  // namespace

void test_source_identity() {
  const std::string phase = env("BLENDITY_IDENTITY_PHASE");
  if (phase.empty()) {
    std::printf("    Workshop identity harness requires the multi-process runner.\n");
    return;
  }
  test("Workshop reimport preserves project identity, material edits and correct textures", [&] {
    const std::string project = env("BLENDITY_IDENTITY_PROJECT");
    const std::string baseline = env("BLENDITY_IDENTITY_BASELINE");
    const std::string item = env("BLENDITY_IDENTITY_ITEM");
    const std::string policy = env("BLENDITY_IDENTITY_TEXTURE_POLICY");
    const bool preserve = policy.empty() || policy == "preserve";
    CHECK(preserve || policy == "refresh");
    if (!preserve && policy != "refresh") return;
    std::printf("    Texture policy: %s\n", preserve ? "preserve" : "refresh");
    const bool imports = phase == "seed" || phase == "reimport-v2" || phase == "repeat-v2";
    const bool phase_ok = imports || phase == "offline-v1" || phase == "offline-v2";
    const bool usable = phase_ok && !project.empty() && !baseline.empty() && imports == !item.empty();
    CHECK(usable);
    if (!usable) return;
    ProjectScope scope(project);
    Editor editor;
    editor.init_headless(1000, 740);
    editor.record_buttons_for_test(true);
    if (phase == "seed") seed(editor, project, baseline, item);
    else recheck(editor, phase, project, baseline, item, !preserve);
  });
}

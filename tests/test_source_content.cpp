// SPDX-License-Identifier: GPL-2.0-or-later
#include "test_common.h"
#include "../src/scene/source_content.h"

namespace {

using Bytes = std::vector<uint8_t>;

// Scratch fixtures use original text payloads. The format references are recorded in task 0583.
struct LittleEndian : Bytes {
  void word(uint16_t value) {
    push_back(uint8_t(value));
    push_back(uint8_t(value >> 8));
  }
  void dword(uint32_t value) {
    word(uint16_t(value));
    word(uint16_t(value >> 16));
  }
  void append(std::string_view text) { insert(end(), text.begin(), text.end()); }
  void append(const Bytes &data) { insert(end(), data.begin(), data.end()); }
  void zstring(std::string_view text) {
    append(text);
    push_back(0);
  }
};

std::string write_fixture(const std::string &root, const std::string &relative, std::string_view data) {
  const std::string path = fs::join(root, relative);
  fs::make_dirs(fs::parent(path));
  CHECK(fs::write_file(path, std::string(data)));
  return path;
}

std::string write_fixture(const std::string &root, const std::string &relative, const Bytes &data) {
  return write_fixture(root, relative, std::string_view(reinterpret_cast<const char *>(data.data()), data.size()));
}

std::string provider(const std::string &path) {
  std::error_code ec;
  const auto normalized = std::filesystem::canonical(std::filesystem::u8path(path), ec);
  CHECK(!ec);
  return normalized.generic_u8string();
}

void expect_resource(const SourceContent &source, const std::string &query, const std::string &virtual_path,
                     const std::string &origin, std::string_view payload) {
  SourceEntry entry;
  Bytes bytes;
  std::string error;
  CHECK(source.locate(query, entry, error));
  CHECK(error.empty());
  CHECK(entry.virtual_path == virtual_path);
  CHECK(entry.provider == origin);
  CHECK(source.read(query, bytes, error));
  CHECK(error.empty());
  CHECK(Bytes(payload.begin(), payload.end()) == bytes);
}

// Directory records use the ValvePython VPK layout. The fixed CRCs were generated with Python zlib.crc32.
void vpk_file(LittleEndian &tree, std::string_view name, uint16_t archive, uint32_t offset, uint32_t length,
              std::string_view preload, uint32_t crc) {
  tree.zstring(name);
  tree.dword(crc);
  tree.word(uint16_t(preload.size()));
  tree.word(archive);
  tree.dword(offset);
  tree.dword(length);
  tree.word(0xffff);
  tree.append(preload);
}

void vpk_record(LittleEndian &tree, std::string_view extension, std::string_view directory, std::string_view name,
                uint16_t archive, uint32_t offset, uint32_t length, std::string_view preload, uint32_t crc) {
  tree.zstring(extension);
  tree.zstring(directory);
  vpk_file(tree, name, archive, offset, length, preload, crc);
  tree.push_back(0);  // end of filenames
  tree.push_back(0);  // end of directories
}

Bytes vpk_package(LittleEndian tree, std::string_view inline_data, uint32_t version) {
  tree.push_back(0);  // end of extensions
  LittleEndian package;
  package.dword(0x55aa1234);
  package.dword(version);
  package.dword(uint32_t(tree.size()));
  if (version == 2) {
    package.dword(uint32_t(inline_data.size()));
    package.dword(0);  // no archive MD5 section
    package.dword(0);  // no other MD5 section
    package.dword(0);  // no signature section
  }
  package.append(tree);
  package.append(inline_data);
  return package;
}

Bytes fixture_vpk(bool multipart) {
  LittleEndian tree;
  vpk_record(tree, "mdl", "models", "fixture", 0x7fff, 2, 4, "dir:", 0x42fd91b9);
  if (multipart) vpk_record(tree, "bsp", "maps", "fixture", 2, 3, 3, "part:", 0x5d718801);
  return vpk_package(std::move(tree), "__mesh", multipart ? 2 : 1);
}

uint32_t fixture_crc(std::string_view data) {
  // The reflected CRC-32 used by the archive formats, independent of the production reader.
  uint32_t crc = 0xffffffff;
  for (const unsigned char byte : data) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
  }
  return ~crc;
}

std::string binary_string(const Bytes &bytes) {
  return std::string(bytes.begin(), bytes.end());
}

void put_vpk_file(LittleEndian &tree, std::string &chunk, std::string_view name, std::string_view bytes,
                  size_t preload_size, uint16_t archive) {
  vpk_file(tree, name, archive, uint32_t(chunk.size()), uint32_t(bytes.size() - preload_size),
           bytes.substr(0, preload_size), fixture_crc(bytes));
  chunk += bytes.substr(preload_size);
}

std::string fixture_nested(const std::string &root) {
  LittleEndian inner;
  vpk_record(inner, "bsp", "maps", "fixture", 5, 2, 3, "part:", 0x5d718801);
  const std::string child = binary_string(vpk_package(std::move(inner), "", 2));
  LittleEndian sibling;
  vpk_record(sibling, "bsp", "maps", "fixture", 0x7fff, 0, 0, "different map", fixture_crc("different map"));
  vpk_record(sibling, "vmt", "materials", "sibling-only", 0x7fff, 0, 0, "sibling data", fixture_crc("sibling data"));
  const std::string second = binary_string(vpk_package(std::move(sibling), "", 1));
  LittleEndian broken;
  broken.dword(0x55aa1234);
  broken.dword(3);
  broken.dword(1);
  const std::string unsupported = binary_string(broken);
  std::string chunk = "seek past this";
  LittleEndian outer;
  outer.zstring("vpk");
  outer.zstring("maps");
  // Both reads cross the outer member's preload boundary. The inner map also has its own preload.
  put_vpk_file(outer, chunk, "compiled_dir", child, 9, 3);
  put_vpk_file(outer, chunk, "compiled_005", "__map", 3, 3);
  put_vpk_file(outer, chunk, "separate", second, 0, 3);
  put_vpk_file(outer, chunk, "unsupported", unsupported, unsupported.size(), 3);
  outer.push_back(0);
  outer.push_back(0);
  vpk_record(outer, "mdl", "models", "outer", 0x7fff, 0, 0, "outer model", fixture_crc("outer model"));
  vpk_record(outer, "vmt", "materials", "outer", 0x7fff, 0, 0, "outer material", fixture_crc("outer material"));
  write_fixture(root, "workshop_003.vpk", chunk);
  return write_fixture(root, "workshop_dir.vpk", vpk_package(std::move(outer), "", 2));
}

Bytes fixture_deep() {
  LittleEndian tree;
  vpk_record(tree, "mdl", "models", "observable", 0x7fff, 0, 0, "model", fixture_crc("model"));
  Bytes current = vpk_package(std::move(tree), "", 1);
  for (int level = 0; level < 8; ++level) {
    const std::string inner = binary_string(current);
    LittleEndian parent;
    vpk_record(parent, "vpk", " ", "child", 0x7fff, 0, uint32_t(inner.size()), "", fixture_crc(inner));
    vpk_record(parent, "mdl", "models", "observable", 0x7fff, 0, 0, "model", fixture_crc("model"));
    current = vpk_package(std::move(parent), inner, 1);
  }
  return current;
}

// ZIP records follow PKWARE APPNOTE 6.3.10 §4.3. The raw DEFLATE bytes were generated with Python zlib (wbits=-15).
void zip_member(LittleEndian &zip, LittleEndian &central, std::string_view name, const Bytes &encoded,
                uint16_t method, uint32_t decoded_size, uint32_t crc) {
  const uint32_t start = uint32_t(zip.size());
  auto attributes = [&](LittleEndian &out) {
    out.word(20);  // version needed to extract
    out.word(0);   // flags
    out.word(method);
    out.word(0);   // time
    out.word(0x21);  // 1980-01-01
    out.dword(crc);
    out.dword(uint32_t(encoded.size()));
    out.dword(decoded_size);
    out.word(uint16_t(name.size()));
    out.word(0);  // no extra field
  };
  zip.dword(0x04034b50);
  attributes(zip);
  zip.append(name);
  zip.append(encoded);
  central.dword(0x02014b50);
  central.word(20);  // version made by
  attributes(central);
  central.word(0);  // no member comment
  central.word(0);  // starting disk
  central.word(0);  // internal attributes
  central.dword(0);  // external attributes
  central.dword(start);
  central.append(name);
}

Bytes fixture_zip() {
  LittleEndian zip, central;
  constexpr std::string_view model = "embedded-model";
  zip_member(zip, central, "Models/Shared.MDL", Bytes(model.begin(), model.end()), 0, uint32_t(model.size()), 0xe104cbe9);
  const Bytes compressed{0x4b, 0xcd, 0x4d, 0x4a, 0x4d, 0x49, 0x49, 0x4d, 0xd1, 0x2d, 0x49, 0xad, 0x48, 0xcd, 0x29, 0x06, 0x00};
  zip_member(zip, central, "materials/override.vtf", compressed, 8, 15, 0xb69f32c9);
  const uint32_t directory = uint32_t(zip.size());
  zip.append(central);
  zip.dword(0x06054b50);
  zip.word(0);  // disk number
  zip.word(0);  // directory disk
  zip.word(2);  // members on this disk
  zip.word(2);  // total members
  zip.dword(uint32_t(central.size()));
  zip.dword(directory);
  zip.word(0);  // no archive comment
  return zip;
}

bool picker_control(Editor &editor, const std::string &label, Recti &rect) {
  if (editor.button_rect_for_test(label, rect) && !rect.empty()) return true;
  std::printf("    Source picker control '%s' is missing\n", label.c_str());
  CHECK(false);
  return false;
}

bool picker_click(Editor &editor, const std::string &label) {
  Recti rect;
  if (!picker_control(editor, label, rect)) return false;
  r34_click(editor, rect.x + rect.w / 2, rect.y + rect.h / 2);
  return true;
}

bool picker_enter_path(Editor &editor, const char *field, const std::string &path, const char *button) {
  if (!picker_click(editor, field)) return false;
  std::vector<platform::Event> text;
  const char *cursor = path.data(), *end = cursor + path.size();
  while (cursor != end) {
    platform::Event event;
    event.type = platform::EventType::Text;
    event.codepoint = utf8_next(cursor, end);
    text.push_back(event);
  }
  editor.step_frame_headless(text);
  if (!picker_click(editor, button)) return false;
  editor.step_frame_headless();  // the headless picker collects its completed worker at the next frame
  return true;
}

bool picker_tip_contains(Editor &editor, const std::string &fragment) {
  const auto &tips = editor.ui_for_test().tooltip_texts;
  return std::any_of(tips.begin(), tips.end(), [&](const auto &tip) { return tip.second.find(fragment) != std::string::npos; });
}

struct SourceScratchProject {
  std::string previous, previous_root, work, project;

  explicit SourceScratchProject(const std::string &root)
      : previous_root(asset_root()), work(fs::join(root, "snapshots " + std::to_string(test_run_stamp()))),
        project(fs::join(work, "Project")) {
    const char *active = std::getenv("BLENDITY_PROJECT");
    if (active) previous = active;
    set_env("BLENDITY_PROJECT", project);
  }

  ~SourceScratchProject() {
    clear_material_assets();
    clear_texture_assets();
    set_env("BLENDITY_PROJECT", previous);
    set_asset_root(previous_root);
  }
};

void write_color(const std::string &path, uint32_t argb) {
  fs::make_dirs(fs::parent(path));
  CHECK(write_png(path, &argb, 1, 1, 1));
}

void expect_color(const std::string &relative, uint32_t argb) {
  Bitmap pixels;
  std::string error;
  CHECK(load_image(resolve_asset_path(relative), pixels, error));
  CHECK(pixels.width == 1 && pixels.height == 1);
  const Bytes expected{uint8_t(argb >> 16), uint8_t(argb >> 8), uint8_t(argb), uint8_t(argb >> 24)};
  CHECK(pixels.rgba8 == expected);
}

using NativeFiles = std::map<std::string, std::string>;

NativeFiles native_files(const std::string &project) {
  NativeFiles actual;
  const auto assets = std::filesystem::u8path(fs::join(project, "Assets"));
  for (const auto &entry : std::filesystem::recursive_directory_iterator(assets)) {
    if (entry.path().extension() != ".mat" && entry.path().extension() != ".png") continue;
    const std::string relative = "Assets/" + entry.path().lexically_relative(assets).generic_u8string();
    CHECK(fs::read_file(entry.path().u8string(), actual[relative]));
  }
  return actual;
}

const std::array<std::string, 2> source_texture_ids{"materials/east/paint.vtf", "materials/west/paint.vtf"};
const std::array<std::string, 2> source_material_ids{"materials/east/paint.vmt", "materials/west/paint.vmt"};

ImportResult decoded_pair(const std::array<std::string, 2> &textures, bool reverse_materials, bool reverse_textures,
                         bool extra_materials) {
  ImportResult result;
  result.textures.assign(textures.begin(), textures.end());
  for (size_t i = 0; i != textures.size(); ++i) result.source_texture_ids.emplace(textures[i], source_texture_ids[i]);
  if (reverse_textures) std::reverse(result.textures.begin(), result.textures.end());
  ImportedNode &node = result.nodes.emplace_back();
  node.name = "Painted faces";
  node.mesh = std::make_shared<Mesh>();
  for (const Vec3 point : {Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(1, 1, 0), Vec3(0, 1, 0),
                           Vec3(2, 0, 0), Vec3(3, 0, 0), Vec3(3, 1, 0), Vec3(2, 1, 0)}) node.mesh->add_vert(point);
  const uint32_t faces[][3] = {{0, 1, 2}, {0, 2, 3}, {4, 5, 6}, {4, 6, 7}};
  const size_t count = extra_materials ? 4 : 2;
  for (size_t i = 0; i < count; ++i) {
    node.mesh->add_face(faces[i], 3, nullptr, int(i));
    const bool overlay = i >= 2;
    const size_t side = i % 2;
    MaterialPtr material = make_material(overlay ? "Overlay" : "Paint", Vec3(1));
    material->base_map = TextureRef{textures[side], false};
    result.source_material_ids.emplace(material.get(), source_material_ids[side] + (overlay ? "#overlay" : ""));
    result.materials.push_back(material);
    node.materials.push_back(material);
  }
  if (reverse_materials) std::reverse(result.materials.begin(), result.materials.end());
  return result;
}

bool publish(Editor &editor, const SourceEntry &entry, ImportResult &result) {
  std::string error;
  const bool ok = editor.source_import_files_for_test(entry, result, true, error);
  if (!ok) std::printf("    Source snapshot publication failed: %s\n", error.c_str());
  CHECK(ok);
  return ok;
}

void decoded_material(ImportResult &result, const std::string &name, const std::string &id, const std::string &texture) {
  MaterialPtr material = make_material(name, Vec3(1));
  material->base_map = TextureRef{texture, false};
  result.materials.push_back(material);
  result.source_material_ids.emplace(material.get(), "materials/" + id + ".vmt");
  result.source_texture_ids.emplace(texture, "materials/" + id + ".vtf");
}

const std::string relocation_path = "models/relocation.mdl";

std::string write_relocation_source(const std::string &item, bool vpk, std::string_view model) {
  if (!vpk) return write_fixture(item, relocation_path, model);
  LittleEndian tree;
  vpk_record(tree, "mdl", "models", "relocation", 0x7fff, 0, 0, model, fixture_crc(model));
  return write_fixture(item, "item_dir.vpk", vpk_package(std::move(tree), "", 2));
}

SourceEntry select_relocation_source(const std::string &file, std::string_view model) {
  SourceContent source;
  SourceEntry entry;
  Bytes content;
  std::string error;
  CHECK(source.mount(file, error));
  CHECK(source.locate(relocation_path, entry, error));
  CHECK(source.read(relocation_path, content, error) && content == Bytes(model.begin(), model.end()));
  entry.fingerprint = source.entry_fingerprint(entry);
  CHECK(!entry.fingerprint.empty());
  return entry;
}

const MeshRenderer *instantiate_painted_faces(Scene &scene, const ImportResult &result) {
  const GameObject *object = instantiate_import(scene, result, "Painted faces", nullptr);
  const MeshRenderer *renderer = object ? object->get<MeshRenderer>() : nullptr;
  const MeshFilter *filter = object ? object->get<MeshFilter>() : nullptr;
  const size_t expected = result.nodes.front().materials.size();
  CHECK(filter && filter->mesh && filter->mesh->face_count() == expected);
  if (!filter || !filter->mesh || filter->mesh->face_count() != expected) return nullptr;
  for (size_t face = 0; face < expected; ++face) CHECK(filter->mesh->material_of(face) == int(face));
  CHECK(renderer && renderer->materials.size() == expected);
  if (!renderer || renderer->materials.size() != expected) return nullptr;
  return renderer;
}

}  // namespace

void test_source_content() {
  const std::string root = fs::join(test_dir(), "source_content");

  test("Source content: a two-model item lists both paths and a loose model can use its sibling materials", [&] {
    const std::string item = fs::join(root, "two models");
    const std::string first = write_fixture(item, "Models/Actors/Alpha.MDL", "actor A");
    const std::string second = write_fixture(item, "models/actors/beta.vmdl_c", "actor B");
    const std::string texture = write_fixture(item, "materials/actors/skin.vmt", "skin material");
    write_fixture(item, "notes.txt", "not a model");
    SourceContent content;
    std::string error, selected;
    CHECK(content.mount(item, error, selected));
    CHECK(error.empty() && selected.empty());
    const auto entries = content.entries();
    CHECK(entries.size() == 2);
    if (entries.size() != 2) return;
    CHECK(entries[0].virtual_path == "models/actors/alpha.mdl" && entries[0].provider == provider(first));
    CHECK(entries[1].virtual_path == "models/actors/beta.vmdl_c" && entries[1].provider == provider(second));
    SourceContent loose;
    CHECK(loose.mount(first, error, selected));
    CHECK(selected == "models/actors/alpha.mdl");
    expect_resource(loose, selected, selected, provider(first), "actor A");
    expect_resource(loose, "Materials\\Actors\\Skin.VMT", "materials/actors/skin.vmt", provider(texture), "skin material");
  });

  test("Source content: reversing two roots changes which file provides a duplicate path", [&] {
    const std::string a = fs::join(root, "ordered first");
    const std::string b = fs::join(root, "ordered second");
    const std::string file_a = write_fixture(a, "models/shared.mdl", "disk-a");
    const std::string file_b = write_fixture(b, "Models/SHARED.MDL", "disk-b");
    const std::string path = "models/shared.mdl";
    std::string error;
    SourceContent forward, reverse;
    CHECK(forward.mount(a, error));
    CHECK(forward.mount(b, error));
    CHECK(reverse.mount(b, error));
    CHECK(reverse.mount(a, error));
    expect_resource(forward, path, path, provider(file_a), "disk-a");
    expect_resource(reverse, path, path, provider(file_b), "disk-b");
    const auto first = forward.entries(), second = reverse.entries();
    CHECK(first.size() == 1 && first[0].virtual_path == path && first[0].provider == provider(file_a));
    CHECK(second.size() == 1 && second[0].virtual_path == path && second[0].provider == provider(file_b));
  });

  test("Source content: a map's embedded ZIP wins over mounted roots and can decode compressed textures", [&] {
    const std::string item = fs::join(root, "map item");
    const std::string selected = write_fixture(item, "models/shared.mdl", "disk-a");
    const std::string other = fs::join(root, "map game");
    write_fixture(other, "models/shared.mdl", "disk-b");
    const std::string fallback = write_fixture(other, "materials/loose.vtf", "loose");
    std::string error;
    SourceContent shared;
    CHECK(shared.mount(item, error));
    CHECK(shared.mount(other, error));
    SourceContent map = shared;
    const std::string pak = "maps/fixture.bsp embedded pak";
    CHECK(map.mount_embedded_zip(pak, fixture_zip(), error));
    expect_resource(map, "Models/SHARED.MDL", "models/shared.mdl", pak, "embedded-model");
    expect_resource(map, "materials/override.vtf", "materials/override.vtf", pak, "embedded-texels");
    expect_resource(map, "materials/loose.vtf", "materials/loose.vtf", provider(fallback), "loose");
    expect_resource(shared, "models/shared.mdl", "models/shared.mdl", provider(selected), "disk-a");
    const auto entries = map.entries();
    CHECK(entries.size() == 1 && entries[0].provider == pak);
  });

  test("Source content: VPK directory and numbered archive files supply entries with their preload bytes", [&] {
    const std::string item = fs::join(root, "packaged item");
    const std::string local = write_fixture(item, "local.vpk", fixture_vpk(false));
    const std::string directory = write_fixture(item, "split_dir.vpk", fixture_vpk(true));
    const std::string chunk = write_fixture(item, "split_002.vpk", "___map");
    const std::string model = "models/fixture.mdl", map = "maps/fixture.bsp";
    std::string error;
    SourceContent standalone, multipart;
    CHECK(standalone.mount(local, error));
    expect_resource(standalone, model, model, provider(local), "dir:mesh");
    CHECK(multipart.mount(chunk, error));
    const auto entries = multipart.entries();
    CHECK(entries.size() == 2 && entries[0].virtual_path == map && entries[1].virtual_path == model);
    expect_resource(multipart, model, model, provider(directory), "dir:mesh");
    expect_resource(multipart, "MAPS\\Fixture.BSP", map, provider(directory), "part:map");
    std::error_code ec;
    CHECK(std::filesystem::remove(std::filesystem::u8path(chunk), ec) && !ec);
    Bytes bytes{1};
    CHECK(!multipart.read(map, bytes, error));
    CHECK(bytes.empty() && error.find(map) != std::string::npos && error.find("split_dir.vpk") != std::string::npos);
  });

  test("Source content: a numbered VPK starting with an inner signature still uses its matching directory", [&] {
    const std::string item = fs::join(root, "signature chunk");
    LittleEndian inner;
    constexpr std::string_view map_bytes = "nested map";
    vpk_record(inner, "bsp", "maps", "signature", 0x7fff, 0, 0, map_bytes, fixture_crc(map_bytes));
    const std::string child = binary_string(vpk_package(std::move(inner), "", 1));
    const std::string chunk = write_fixture(item, "outer_003.vpk", child);
    LittleEndian outer;
    vpk_record(outer, "vpk", "maps", "compiled", 3, 0, uint32_t(child.size()), "", fixture_crc(child));
    vpk_record(outer, "vmt", "materials", "outside", 0x7fff, 0, 0, "outer-only", fixture_crc("outer-only"));
    const std::string directory = write_fixture(item, "outer_dir.vpk", vpk_package(std::move(outer), "", 2));
    const std::string outer_provider = provider(directory);
    const std::string child_provider = outer_provider + " > maps/compiled.vpk";
    const std::string map = "maps/signature.bsp", material = "materials/outside.vmt";
    for (const std::string &selection : {item, chunk}) {
      SourceContent source, scoped;
      std::string error;
      CHECK(source.mount(selection, error));
      CHECK(error.empty() && source.mount_warnings().empty());
      const auto entries = source.entries();
      CHECK(entries.size() == 1);
      if (entries.size() != 1) continue;
      CHECK(entries.front().virtual_path == map && entries.front().provider == child_provider);
      CHECK(source.for_entry(entries.front(), scoped, error));
      expect_resource(scoped, map, map, child_provider, map_bytes);
      expect_resource(scoped, material, material, outer_provider, "outer-only");
    }
  });

  test("Source content: an unreadable physical VPK leaves loose files and healthy sibling packages available", [&] {
    const std::string item = fs::join(root, "mixed packages");
    const std::string bad = write_fixture(item, "broken_dir.vpk", "truncated");
    const std::string good = write_fixture(item, "valid_dir.vpk", fixture_vpk(false));
    const std::string loose = write_fixture(item, "models/loose.mdl", "loose model");
    SourceContent source;
    std::string error;
    CHECK(source.mount(item, error));
    CHECK(error.empty());
    CHECK(source.entries().size() == 2);
    const auto &warnings = source.mount_warnings();
    CHECK(warnings.size() == 1 && warnings.front().find("broken_dir.vpk") != std::string::npos &&
          warnings.front().find("Invalid VPK") != std::string::npos);
    expect_resource(source, "models/loose.mdl", "models/loose.mdl", provider(loose), "loose model");
    expect_resource(source, "models/fixture.mdl", "models/fixture.mdl", provider(good), "dir:mesh");
    CHECK(!source.mount(bad, error));
    CHECK(error.find("broken_dir.vpk") != std::string::npos);
    CHECK(source.entries().size() == 2);
    expect_resource(source, "models/fixture.mdl", "models/fixture.mdl", provider(good), "dir:mesh");
  });

  test("Source content: nested multipart maps use their own package and can resolve outer and added dependencies", [&] {
    const std::string item = fs::join(root, "nested item");
    const std::string outer = fixture_nested(item);
    const std::string outer_provider = provider(outer);
    const std::string first = outer_provider + " > maps/compiled_dir.vpk";
    const std::string second = outer_provider + " > maps/separate.vpk";
    const std::string map = "maps/fixture.bsp", material = "materials/outer.vmt", missing = "materials/scout.vmt";
    const std::string extra = fs::join(root, "nested extra root");
    const std::string extra_texture = write_fixture(extra, missing, "scout color");
    write_fixture(extra, material, "lower priority material");
    SourceContent content;
    std::string error;
    CHECK(content.mount(outer, error));
    CHECK(error.empty());
    const auto matches = content.entries();
    CHECK(matches.size() == 3);
    if (matches.size() != 3) return;
    CHECK(matches[0].virtual_path == map && matches[0].provider == first);
    CHECK(matches[1].virtual_path == map && matches[1].provider == second);
    CHECK(matches[2].virtual_path == "models/outer.mdl" && matches[2].provider == outer_provider);
    const auto &warnings = content.mount_warnings();
    CHECK(warnings.size() == 1);
    CHECK(!warnings.empty() && warnings.front().find("maps/unsupported.vpk") != std::string::npos &&
          warnings.front().find("Unsupported VPK version 3") != std::string::npos);
    ImportResult unqualified;
    CHECK(!import_source_entry(content, map, unqualified));
    CHECK(unqualified.error.find(map) != std::string::npos && unqualified.error.find("Several") != std::string::npos);

    SourceContent first_scope, second_scope;
    CHECK(content.for_entry(matches[0], first_scope, error));
    CHECK(content.for_entry(matches[1], second_scope, error));
    expect_resource(first_scope, map, map, first, "part:map");
    expect_resource(second_scope, map, map, second, "different map");
    expect_resource(first_scope, material, material, outer_provider, "outer material");
    expect_resource(second_scope, material, material, outer_provider, "outer material");
    Bytes bytes;
    CHECK(!first_scope.read("materials/sibling-only.vmt", bytes, error));
    CHECK(error.find("materials/sibling-only.vmt") != std::string::npos);
    CHECK(!first_scope.read(missing, bytes, error));
    CHECK(error.find("Missing") != std::string::npos && error.find(missing) != std::string::npos);
    CHECK(content.mount(extra, error));
    CHECK(content.for_entry(matches[0], first_scope, error));
    expect_resource(first_scope, map, map, first, "part:map");
    expect_resource(first_scope, material, material, outer_provider, "outer material");
    expect_resource(first_scope, missing, missing, provider(extra_texture), "scout color");

    // The index survives, but the map payload must still come from the physical multipart file.
    CHECK(fs::remove_file(fs::join(item, "workshop_003.vpk")));
    SourceEntry map_entry;
    CHECK(first_scope.locate(map, map_entry, error) && map_entry.provider == first);
    CHECK(!first_scope.read(map, bytes, error));
    CHECK(bytes.empty() && error.find(map) != std::string::npos && error.find(first) != std::string::npos);
    expect_resource(first_scope, material, material, outer_provider, "outer material");
  });

  test("Source content: nested indexing stops at a bounded depth and keeps valid outer entries", [&] {
    const std::string path = write_fixture(root, "deep.vpk", fixture_deep());
    SourceContent content;
    std::string error;
    CHECK(content.mount(path, error));
    const auto entries = content.entries();
    CHECK(entries.size() == 5);  // outer plus the four documented nested levels
    for (const auto &entry : entries) {
      SourceContent scoped;
      CHECK(content.for_entry(entry, scoped, error));
      expect_resource(scoped, "models/observable.mdl", "models/observable.mdl", entry.provider, "model");
    }
    const auto &warnings = content.mount_warnings();
    CHECK(warnings.size() == 1);
    CHECK(!warnings.empty() && warnings.front().find("depth limit") != std::string::npos &&
          warnings.front().find("child.vpk") != std::string::npos);
  });

  test("Source content: a missing dependency is named and an additional content root resolves it", [&] {
    const std::string item = fs::join(root, "requires game");
    write_fixture(item, "models/fixture.mdl", "model");
    const std::string game = fs::join(root, "game content");
    const std::string name = "materials/characters/scout.vmt";
    const std::string dependency = write_fixture(game, name, "scout color");
    SourceContent content;
    std::string error;
    CHECK(content.mount(item, error));
    SourceEntry entry;
    CHECK(!content.locate(name, entry, error));
    CHECK(error.find("Missing") != std::string::npos && error.find(name) != std::string::npos);
    CHECK(entry.virtual_path.empty() && entry.provider.empty());
    Bytes bytes{1};
    CHECK(!content.read(name, bytes, error));
    CHECK(error.find("Missing") != std::string::npos && error.find(name) != std::string::npos && bytes.empty());
    CHECK(content.mount(game, error));
    expect_resource(content, name, name, provider(dependency), "scout color");
    CHECK(!content.read("materials/../models/fixture.mdl", bytes, error));
    CHECK(bytes.empty());
  });

  test("Source picker: same-named nested maps are individually selectable and a skipped package is named", [&] {
    const std::string outer = fixture_nested(fs::join(root, "nested picker item"));
    const std::string origin = provider(outer);
    const std::string path = "source:entry maps/fixture.bsp @ " + origin;
    const std::string first = path + " > maps/compiled_dir.vpk";
    const std::string second = path + " > maps/separate.vpk";
    Editor editor;
    editor.init_headless(1200, 720);
    editor.record_buttons_for_test(true);
    editor.command("import source");
    editor.step_frame_headless();
    if (!picker_enter_path(editor, "Source item path", outer, "Use Path")) return;
    CHECK(picker_tip_contains(editor, "maps/unsupported.vpk"));
    Recti first_row, second_row;
    if (!picker_control(editor, first, first_row) || !picker_control(editor, second, second_row)) return;
    CHECK(first_row.y != second_row.y);
    auto selected = [&](const std::string &label) {
      const auto &tips = editor.ui_for_test().tooltip_texts;
      return std::count_if(tips.begin(), tips.end(), [&](const auto &tip) { return tip.second == label; }) == 2;
    };
    if (!picker_click(editor, first)) return;
    editor.step_frame_headless();
    CHECK(selected(origin + " > maps/compiled_dir.vpk"));
    CHECK(!selected(origin + " > maps/separate.vpk"));
    if (!picker_click(editor, second)) return;
    editor.step_frame_headless();
    CHECK(!selected(origin + " > maps/compiled_dir.vpk"));
    CHECK(selected(origin + " > maps/separate.vpk"));
    if (!picker_click(editor, "Cancel")) return;
    editor.step_frame_headless();
    CHECK(!editor.dialog_open_for_test());
  });

  test("Source picker: two model paths appear and cancelling leaves the active scene and selection intact", [&] {
    const std::string item = fs::join(root, "picker item");
    write_fixture(item, "models/alpha.mdl", "actor A");
    write_fixture(item, "models/beta.vmdl_c", "actor B");
    const std::string game = fs::join(root, "picker extra root");
    write_fixture(game, "materials/actors/skin.vmt", "skin material");
    Editor editor;
    editor.init_headless(1200, 720);
    editor.command("create cube");
    GameObject *active = editor.active_object_for_test();
    CHECK(active != nullptr);
    if (!active) return;
    const auto selected_id = active->id;
    const std::string before = save_scene_text(editor.scene());
    const bool dirty = editor.scene_dirty_for_test();
    editor.record_buttons_for_test(true);
    editor.command("import source");
    editor.step_frame_headless();
    CHECK(editor.dialog_open_for_test());
    if (!picker_enter_path(editor, "Source item path", item, "Use Path")) return;
    Recti rect;
    picker_control(editor, "models/alpha.mdl", rect);
    picker_control(editor, "models/beta.vmdl_c", rect);
    if (!picker_click(editor, "models/beta.vmdl_c")) return;
    if (!picker_enter_path(editor, "Source root path", game, "Add Root")) return;
    CHECK(picker_tip_contains(editor, game));
    if (!picker_click(editor, "Cancel")) return;
    editor.step_frame_headless();
    CHECK(!editor.dialog_open_for_test());
    editor.command("import source");
    editor.step_frame_headless();
    CHECK(editor.dialog_open_for_test());
    CHECK(!editor.button_rect_for_test("models/beta.vmdl_c", rect));
    platform::Event press, release;
    press.type = platform::EventType::KeyDown;
    release.type = platform::EventType::KeyUp;
    press.key = release.key = platform::KEY_ESCAPE;
    editor.step_frame_headless({press});
    editor.step_frame_headless({release});
    editor.step_frame_headless();
    CHECK(!editor.dialog_open_for_test());
    CHECK(save_scene_text(editor.scene()) == before);
    CHECK(editor.active_object_for_test() && editor.active_object_for_test()->id == selected_id);
    CHECK(editor.scene_dirty_for_test() == dirty);
  });

  test("Source content: converted texture bytes go to the scratch import cache", [&] {
    const char *key = "BLENDITY_CACHE_DIR";
    const char *saved = std::getenv(key);
    const bool had_override = saved != nullptr;
    const std::string previous = saved ? saved : "";
    const std::string cache = fs::join(root, "isolated cache");
    set_env(key, cache);
    ImportResult result;
    result.source = fs::join(root, "cache origin/models/example.mdl");
    const std::string texels = "original synthetic texture bytes";
    const std::string generated = save_embedded("paint.png", texels.data(), texels.size(), result);
    CHECK(fs::normalize(generated).rfind(fs::normalize(fs::join(cache, "import")) + "/", 0) == 0);
    std::string actual;
    CHECK(fs::read_file(generated, actual) && actual == texels);
    if (had_override) set_env(key, previous);
    else clear_env(key);
  });

  test("Source import: discard and failed publication allow a fresh retry, then material and texture edits survive reimport and offline reopen", [&] {
    SourceScratchProject scratch(root);
    const auto &work = scratch.work, &project = scratch.project;
    const std::string material_dir = fs::join(project, "Assets/Materials");
    const std::string texture = fs::join(work, "cache/paint.png");
    fs::make_dirs(fs::parent(texture));
    const std::string original = write_fixture(work, "item/models/snapshot.mdl", "fixture entry");
    const SourceEntry entry{"models/snapshot.mdl", provider(original)};
    Editor editor;
    editor.init_headless(800, 600);

    auto decoded = [&] {
      ImportResult data;
      data.source = original;
      data.nodes.emplace_back();
      MaterialPtr material = make_material("Paint", Vec3(1.0f));
      material->base_map = TextureRef{texture, false};
      data.nodes.back().materials.push_back(material);  // a reader can attach a material directly to a mesh node
      return data;
    };
    const std::string rel_mat = "Assets/Materials/snapshot/Paint.mat";
    const std::string rel_tex = "Assets/Textures/snapshot/paint.png";
    const std::string material_path = fs::join(project, rel_mat);
    const std::string texture_path = fs::join(project, rel_tex);
    std::string error;

    write_color(texture, 0xff813718);
    ImportResult discarded = decoded();
    CHECK(editor.source_import_files_for_test(entry, discarded, false, error));
    CHECK(error.empty() && !fs::exists(material_path) && !fs::exists(texture_path));

    // Textures can be published before materials. A regular file blocks material folder creation so rollback must
    // remove the already published texture too, without touching the obstruction.
    CHECK(fs::write_file(material_dir, "obstruct material publication"));
    ImportResult failed = decoded();
    CHECK(!editor.source_import_files_for_test(entry, failed, true, error));
    CHECK(error.find("material") != std::string::npos && !fs::exists(texture_path));
    std::string bytes;
    CHECK(fs::read_file(material_dir, bytes) && bytes == "obstruct material publication");
    CHECK(fs::remove_file(material_dir));

    write_color(texture, 0xff3e6b12);
    std::string fresh;
    CHECK(fs::read_file(texture, fresh));
    ImportResult retried = decoded();
    const bool published = editor.source_import_files_for_test(entry, retried, true, error);
    if (!published) std::printf("    Source snapshot retry failed: %s\n", error.c_str());
    CHECK(published);
    CHECK(fs::read_file(texture_path, bytes) && bytes == fresh);
    const MaterialPtr material = material_asset(rel_mat);
    CHECK(material && material->base_map.path == rel_tex);
    if (!material) return;

    material->roughness = 0.375f;
    const std::string edited_material = save_material_text(*material);
    CHECK(fs::write_file(material_path, edited_material));
    write_color(texture_path, 0xff1749b4);
    std::string edited_texture;
    CHECK(fs::read_file(texture_path, edited_texture) && edited_texture != fresh);
    write_color(texture, 0xff090807);
    ImportResult reimported = decoded();
    CHECK(editor.source_import_files_for_test(entry, reimported, true, error));
    CHECK(fs::read_file(texture_path, bytes) && bytes == edited_texture);
    CHECK(fs::read_file(material_path, bytes) && bytes == edited_material);

    editor.command("create Cube");
    GameObject *object = editor.active_object_for_test();
    MeshRenderer *renderer = object ? object->get<MeshRenderer>() : nullptr;
    CHECK(renderer != nullptr);
    if (!renderer) return;
    renderer->materials = {material};
    const std::string scene = fs::join(project, "Assets/Scenes/Snapshot.scene");
    CHECK(save_scene(editor.scene(), scene));
    CHECK(fs::remove_file(texture) && fs::remove_file(original));

    clear_material_assets();
    clear_texture_assets();
    Scene reopened;
    CHECK(load_scene(scene, reopened, error));
    const GameObject *cube = reopened.find_by_name(object->name);
    const MeshRenderer *loaded = cube ? cube->get<MeshRenderer>() : nullptr;
    const MaterialPtr persisted = loaded && !loaded->materials.empty() ? loaded->materials[0] : nullptr;
    CHECK(persisted && persisted->asset_path == rel_mat);
    if (!persisted) return;
    CHECK_NEAR(persisted->roughness, 0.375f, 1e-6);
    CHECK(persisted->base_map.path == rel_tex);
    Bitmap texels;
    CHECK(load_image(resolve_asset_path(persisted->base_map.path), texels, error));
    CHECK(texels.width == 1 && texels.height == 1);
    CHECK(fs::read_file(resolve_asset_path(persisted->base_map.path), bytes) && bytes == edited_texture);
  });

  test("Source reimport: matching basenames keep distinct face assignments and edited textures across moved roots", [&] {
    SourceScratchProject scratch(root);
    const std::string path = "models/painted.mdl";
    const std::string scene_file = fs::join(scratch.project, "Assets/Scenes/Painted.scene");
    constexpr std::array<uint32_t, 2> original_colors{0xffa41132, 0xff1e8cb4};
    constexpr std::array<uint32_t, 2> edited_colors{0xffa831a0, original_colors[1]};
    constexpr std::array<float, 2> roughness{0.125f, 0.875f};
    std::array<std::string, 2> material_paths, texture_paths, overlay_paths;
    NativeFiles stable_files;
    for (int pass = 0; pass < 3; ++pass) {
      const std::string new_root = fs::join(scratch.work, "location " + std::to_string(pass));
      const std::string mdl = write_fixture(new_root, "item/" + path, "original model");
      const std::string basename = pass == 2 ? "paint-new.png" : "paint.png";
      const std::array<std::string, 2> textures{fs::join(new_root, "cache/east/" + basename), fs::join(new_root, "cache/west/" + basename)};
      write_color(textures[0], pass == 0 ? original_colors[0] : 0xff201000 + pass);
      write_color(textures[1], pass == 0 ? original_colors[1] : 0xff009020 + pass);
      Editor editor;
      editor.init_headless(800, 600);
      ImportResult decoded = decoded_pair(textures, pass == 1, pass != 0, pass != 0);
      if (!publish(editor, {path, provider(mdl)}, decoded)) return;
      Scene current;
      const MeshRenderer *renderer = instantiate_painted_faces(current, decoded);
      if (!renderer) return;
      CHECK(renderer->materials.size() == (pass == 0 ? 2 : 4));
      for (size_t side = 0; side != 2; ++side) {
        const MaterialPtr &material = renderer->materials[side];
        CHECK(material != nullptr);
        if (!material) return;
        if (pass == 0) {
          material_paths[side] = material->asset_path;
          texture_paths[side] = material->base_map.path;
          expect_color(texture_paths[side], original_colors[side]);
          material->roughness = roughness[side];
          CHECK(fs::write_file(resolve_asset_path(material_paths[side]), save_material_text(*material)));
        }
        else {
          CHECK(material->asset_path == material_paths[side] && material->base_map.path == texture_paths[side]);
          CHECK_NEAR(material->roughness, roughness[side], 1e-6f);
          expect_color(material->base_map.path, edited_colors[side]);
          if (renderer->materials.size() != 4) return;
          const MaterialPtr &overlay = renderer->materials[side + 2];
          CHECK(overlay && overlay->base_map.path == texture_paths[side]);
          if (!overlay) return;
          expect_color(overlay->base_map.path, edited_colors[side]);
          if (pass == 1) overlay_paths[side] = overlay->asset_path;
          else CHECK(overlay->asset_path == overlay_paths[side]);
        }
      }
      if (pass == 0) {
        CHECK(material_paths[0] != material_paths[1] && texture_paths[0] != texture_paths[1]);
        write_color(resolve_asset_path(texture_paths[0]), edited_colors[0]);
        CHECK(save_scene(current, scene_file));
      }
      else CHECK(overlay_paths[0] != overlay_paths[1]);

      const NativeFiles files = native_files(scratch.project);
      CHECK(files.size() == (pass == 0 ? 4 : 6));
      for (size_t side = 0; side != 2; ++side) {
        CHECK(files.count(material_paths[side]) == 1 && files.count(texture_paths[side]) == 1);
        if (pass != 0) CHECK(files.count(overlay_paths[side]) == 1);
      }
      for (const auto &[relative, bytes] : stable_files) {
        const auto saved = files.find(relative);
        CHECK(saved != files.end() && saved->second == bytes);
      }
      stable_files = files;
      CHECK(fs::remove_file(mdl));
      for (const auto &texture : textures) CHECK(fs::remove_file(texture));
    }
    clear_material_assets();
    clear_texture_assets();
    Scene reopened;
    std::string error;
    CHECK(load_scene(scene_file, reopened, error));
    const GameObject *object = reopened.find_by_name("Painted faces");
    const MeshRenderer *renderer = object ? object->get<MeshRenderer>() : nullptr;
    CHECK(renderer && renderer->materials.size() == 2);
    if (!renderer || renderer->materials.size() != 2) return;
    for (size_t side = 0; side != 2; ++side) {
      const MaterialPtr &material = renderer->materials[side];
      CHECK(material && material->asset_path == material_paths[side] && material->base_map.path == texture_paths[side]);
      if (material) expect_color(material->base_map.path, edited_colors[side]);
    }
  });

  test("Source reimport: legacy assets migrate only when their old filenames identify one assignment", [&] {
    for (const char *ambiguity : {"", "material", "texture"}) {
      SourceScratchProject scratch(root);
      const std::string map = "models/legacy.mdl";
      const std::string original = write_fixture(scratch.work, "item/" + map, "legacy model");
      const SourceEntry entry{map, provider(original)};
      const std::string texture = fs::join(scratch.work, "cache/paint.png");
      write_color(texture, 0xff4480f3);
      const std::string mat = "Assets/Materials/legacy/Paint.mat", tex = "Assets/Textures/legacy/paint.png";
      Material edited = *make_material("Paint", Vec3(1));
      edited.roughness = 0.625f;
      edited.base_map = TextureRef{tex, false};
      write_fixture(scratch.project, mat, save_material_text(edited));
      write_color(fs::join(scratch.project, tex), 0xff0257ae);
      write_fixture(scratch.project, "Assets/Materials/legacy/.import-source",
                    to_lower(fs::normalize(entry.provider + "/" + entry.virtual_path)) + "\n");
      if (std::string_view(ambiguity) == "material")
        write_fixture(scratch.project, "Assets/Materials/legacy/Paint 2.mat", save_material_text(edited));
      if (std::string_view(ambiguity) == "texture")
        write_color(fs::join(scratch.project, "Assets/Textures/legacy/paint 2.png"), 0xffac182f);
      const NativeFiles before = native_files(scratch.project);
      for (int launch = 0; launch < 2; ++launch) {
        Editor editor;
        editor.init_headless(800, 600);
        ImportResult decoded;
        MaterialPtr material = make_material("Paint", Vec3(1));
        material->base_map = TextureRef{texture, false};
        decoded.materials.push_back(material);
        decoded.source_material_ids.emplace(material.get(), source_material_ids[0]);
        decoded.source_texture_ids.emplace(texture, source_texture_ids[0]);
        std::string error;
        const bool ok = editor.source_import_files_for_test(entry, decoded, true, error);
        if (*ambiguity) {
          CHECK(!ok);
          const std::string filename = std::string_view(ambiguity) == "material" ? "Paint.mat" : "paint.png";
          CHECK(error.find("legacy Source assets") != std::string::npos && error.find(filename) != std::string::npos);
        }
        else {
          CHECK(ok && error.empty());
          const MaterialPtr adopted = decoded.materials.front();
          CHECK(adopted->asset_path == mat && adopted->base_map.path == tex);
          CHECK_NEAR(adopted->roughness, 0.625f, 1e-6f);
          expect_color(adopted->base_map.path, 0xff0257ae);
        }
        CHECK(native_files(scratch.project) == before);
      }
    }
  });

  test("Source reimport: separate Steam items keep separate edits and a moved item keeps its own", [&] {
    for (const bool vpk : {false, true}) {
      SourceScratchProject scratch(root);
      const std::string workshop = "/steamapps/workshop/content/";
      const std::string first_id = "730/700001";
      // One fixture varies the app and the other varies the item. Either change must separate the snapshots.
      const std::string second_id = vpk ? "546560/700001" : "730/700002";
      const std::array<std::string, 3> installations{
          "Library A" + workshop + first_id, "Library B" + workshop + second_id, "Library C" + workshop + second_id};
      constexpr std::array<uint32_t, 3> incoming{0xff3e7580, 0xff91b7ab, 0xff77cac1};
      constexpr std::array<uint32_t, 2> edited{0xff2c1eb3, 0xff970dd8};
      std::array<std::string, 2> materials, textures;
      std::string first_fingerprint;
      NativeFiles prior;
      for (size_t pass = 0; pass < installations.size(); ++pass) {
        const std::string_view model = pass == 2 ? "model version B" : "model version A";
        const std::string file = write_relocation_source(fs::join(scratch.work, installations[pass]), vpk, model);
        const SourceEntry entry = select_relocation_source(file, model);
        if (pass == 0) first_fingerprint = entry.fingerprint;
        if (pass == 1) CHECK(entry.fingerprint == first_fingerprint);
        if (pass == 2) CHECK(entry.fingerprint != first_fingerprint);
        const std::string cache = fs::join(scratch.work, "cache " + std::to_string(pass) + "/paint.png");
        write_color(cache, incoming[pass]);
        Editor editor;
        editor.init_headless(800, 600);
        ImportResult result;
        decoded_material(result, "Paint", "paint", cache);
        if (!publish(editor, entry, result)) return;
        const MaterialPtr material = result.materials.front();
        CHECK(material && !material->asset_path.empty() && !material->base_map.path.empty());
        if (!material || material->asset_path.empty() || material->base_map.path.empty()) return;
        if (pass < 2) {
          materials[pass] = material->asset_path;
          textures[pass] = material->base_map.path;
          expect_color(textures[pass], incoming[pass]);
          if (pass == 1) CHECK(materials[0] != materials[1] && textures[0] != textures[1]);
          material->roughness = pass == 0 ? 0.25f : 0.75f;
          CHECK(fs::write_file(resolve_asset_path(materials[pass]), save_material_text(*material)));
          write_color(resolve_asset_path(textures[pass]), edited[pass]);
        }
        else {
          CHECK(material->asset_path == materials[1] && material->base_map.path == textures[1]);
          CHECK_NEAR(material->roughness, 0.75f, 1e-6f);
          expect_color(material->base_map.path, edited[1]);
        }
        const NativeFiles files = native_files(scratch.project);
        CHECK(files.size() == (pass == 0 ? 2 : 4));
        for (const auto &[path, bytes] : prior) {
          const auto original = files.find(path);
          CHECK(original != files.end() && original->second == bytes);
        }
        prior = files;
        CHECK(fs::remove_file(file));
        CHECK(fs::remove_file(cache));
      }
    }
  });

  test("Source reimport: changed non-Workshop entry bytes get their own snapshot after moving", [&] {
    SourceScratchProject scratch(root);
    std::string old_material, old_texture, old_fingerprint;
    NativeFiles prior;
    for (int pass = 0; pass < 2; ++pass) {
      const std::string install = fs::join(scratch.work, pass == 0 ? "Ordinary original" : "Ordinary replacement");
      const std::string_view model = pass == 0 ? "first model" : "other model";
      const std::string file = write_relocation_source(install, false, model);
      const SourceEntry entry = select_relocation_source(file, model);
      const std::string texture = fs::join(install, "cache/paint.png");
      const uint32_t color = pass == 0 ? 0xff71503a : 0xffe6a221;
      write_color(texture, color);
      Editor editor;
      editor.init_headless(800, 600);
      ImportResult result;
      decoded_material(result, "Paint", "paint", texture);
      if (!publish(editor, entry, result)) return;
      const MaterialPtr material = result.materials.front();
      CHECK(material && !material->asset_path.empty() && !material->base_map.path.empty());
      if (!material || material->asset_path.empty() || material->base_map.path.empty()) return;
      expect_color(material->base_map.path, color);
      if (pass == 0) {
        old_material = material->asset_path;
        old_texture = material->base_map.path;
        old_fingerprint = entry.fingerprint;
        material->roughness = 0.625f;
        CHECK(fs::write_file(resolve_asset_path(old_material), save_material_text(*material)));
        write_color(resolve_asset_path(old_texture), 0xffb69cc3);
        prior = native_files(scratch.project);
        CHECK(prior.size() == 2);
        CHECK(fs::remove_file(file));
        CHECK(fs::remove_file(texture));
      }
      else {
        CHECK(entry.fingerprint != old_fingerprint);
        CHECK(material->asset_path != old_material && material->base_map.path != old_texture);
        const NativeFiles files = native_files(scratch.project);
        CHECK(files.size() == 4);
        for (const auto &[path, bytes] : prior) {
          const auto original = files.find(path);
          CHECK(original != files.end() && original->second == bytes);
        }
      }
    }
  });

  test("Source reimport: dormant legacy files retain their assignments and newer unrelated files stay separate", [&] {
    SourceScratchProject scratch(root);
    const std::string path = "models/dormant_parts.mdl";
    const std::string original = write_fixture(scratch.work, "item/" + path, "legacy model");
    const SourceEntry entry{path, provider(original)};
    const std::string materials = "Assets/Materials/dormant_parts/", textures = "Assets/Textures/dormant_parts/";
    const auto write_native_pair = [&](const std::string &name, uint32_t pixel, float roughness) {
      write_color(fs::join(scratch.project, textures + to_lower(name) + ".png"), pixel);
      Material material = *make_material(name, Vec3(1));
      material.base_map = TextureRef{textures + to_lower(name) + ".png", false};
      material.roughness = roughness;
      write_fixture(scratch.project, materials + name + ".mat", save_material_text(material));
    };
    write_native_pair("Active", 0xff102080, 0.25f);
    write_native_pair("Dormant", 0xff703130, 0.75f);
    write_fixture(scratch.project, materials + ".import-source",
                  to_lower(fs::normalize(entry.provider + "/" + entry.virtual_path)) + "\n");
    NativeFiles prior = native_files(scratch.project);
    CHECK(prior.size() == 4);
    std::string new_material, new_texture;
    for (int launch = 0; launch < 3; ++launch) {
      const std::string cache = fs::join(scratch.work, "cache " + std::to_string(launch));
      const std::string active = fs::join(cache, "active.png"), dormant = fs::join(cache, "dormant.png"),
                        added = fs::join(cache, "new.png");
      write_color(active, 0xff657e3b);
      ImportResult result;
      decoded_material(result, "Active", "active", active);
      if (launch != 0) {
        write_color(dormant, 0xffec6670);
        write_color(added, 0xff27af76);
        decoded_material(result, "Dormant", "dormant", dormant);
        decoded_material(result, "New", "new", added);
      }
      Editor editor;
      editor.init_headless(800, 600);
      if (!publish(editor, entry, result)) return;
      const auto active_mat = result.materials[0];
      CHECK(active_mat && active_mat->asset_path == materials + "Active.mat" && active_mat->base_map.path == textures + "active.png");
      if (active_mat) expect_color(active_mat->base_map.path, 0xff102080);
      if (launch != 0) {
        const auto dormant_mat = result.materials[1], new_mat = result.materials[2];
        CHECK(dormant_mat && dormant_mat->asset_path == materials + "Dormant.mat" && dormant_mat->base_map.path == textures + "dormant.png");
        CHECK(new_mat && new_mat->asset_path != materials + "New.mat" && new_mat->base_map.path != textures + "new.png");
        if (!dormant_mat || !new_mat) return;
        CHECK_NEAR(dormant_mat->roughness, 0.75f, 1e-6f);
        expect_color(dormant_mat->base_map.path, 0xff703130);
        expect_color(new_mat->base_map.path, 0xff27af76);
        if (launch == 1) {
          new_material = new_mat->asset_path;
          new_texture = new_mat->base_map.path;
        }
        else CHECK(new_mat->asset_path == new_material && new_mat->base_map.path == new_texture);
      }
      NativeFiles files = native_files(scratch.project);
      CHECK(files.size() == (launch == 0 ? 4 : 8));
      for (const auto &[relative, bytes] : prior) {
        const auto saved = files.find(relative);
        CHECK(saved != files.end() && saved->second == bytes);
      }
      if (launch == 0) {
        // An unrelated pair appears after the first (partial) migration recorded the original inventory.
        write_native_pair("New", 0xff00bb26, 0.875f);
        files = native_files(scratch.project);
        CHECK(files.size() == 6);
      }
      prior = std::move(files);
    }
  });
}

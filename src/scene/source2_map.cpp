// SPDX-License-Identifier: GPL-2.0-or-later
#include "source_content.h"
#include "source2_material.h"
#include "source2_model.h"
#include "source2_resource.h"
#include "import.h"
#include "../core/core.h"
#include "../image/image.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace bl {
namespace {

using source2::Resource;
using Tint = std::array<float, 4>;
constexpr Tint untinted{1.f, 1.f, 1.f, 1.f};

bool ends_with(const std::string &path, const std::string &suffix) {
  return path.size() >= suffix.size() && path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string virtual_name(std::string path) {
  for (char &ch : path) {
    if (ch == '\\') ch = '/';
    else if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
  }
  return path;
}

std::string basename(const std::string &path) {
  const size_t slash = path.find_last_of('/');
  const size_t start = slash == std::string::npos ? 0 : slash + 1;
  const size_t dot = path.find_last_of('.');
  return path.substr(start, dot >= start && dot != std::string::npos ? dot - start : std::string::npos);
}

std::string map_world_path(const std::string &map, const std::vector<std::pair<uint64_t, std::string>> &references) {
  std::string manifest;
  for (const auto &ref : references) {
    const std::string path = virtual_name(ref.second);
    if (ends_with(path, ".vwrld_c")) return path;
    if (ends_with(path, ".vwrld")) return path + "_c";
    if (ends_with(path, "/world.vrman") || ends_with(path, "/world.vrman_c")) manifest = path;
  }
  if (!manifest.empty()) return manifest.substr(0, manifest.find_last_of('/') + 1) + "world.vwrld_c";
  return map.substr(0, map.size() - sizeof(".vmap_c") + 1) + "/world.vwrld_c";
}

bool read_map_resource(const SourceContent &content, const std::string &path, std::vector<uint8_t> &bytes,
                       Resource &resource, std::string &error) {
  if (!content.read(path, bytes, error) || !source2::read_resource(bytes, resource, error)) return false;
  if (resource.version == 1) return true;
  error = "unsupported Source 2 map resource version " + std::to_string(resource.version);
  return false;
}

bool read_data(const SourceContent &content, const std::string &path, Json &data, std::string &error) {
  std::vector<uint8_t> bytes;
  Resource resource;
  if (!read_map_resource(content, path, bytes, resource, error)) return false;
  if (!source2::read_kv3(resource.block("DATA"), data, error)) return false;
  if (data.type == Json::Obj) return true;
  error = "Source 2 world DATA is not an object";
  return false;
}

bool matrix_value(const Json &value, float &out) {
  if (value.type != Json::Num || !std::isfinite(value.num) || std::fabs(value.num) > 1e12) return false;
  out = (float)value.num;
  return true;
}

// Source 2 stores a matrix3x4_t as three rows, with the origins in the fourth column.
bool placement_transform(const Json &data, Transform &out) {
  Mat4 source;
  if (data.type != Json::Arr) return false;
  const bool rows = data.size() == 3 || data.size() == 4;
  if (!rows && data.size() != 12 && data.size() != 16) return false;
  const size_t row_count = rows ? data.size() : data.size() / 4;
  for (size_t row = 0; row < row_count; ++row) {
    if (rows && data[row].size() != 4) return false;
    for (size_t col = 0; col < 4; ++col) {
      const Json &entry = rows ? data[row][col] : data[row * 4 + col];
      if (!matrix_value(entry, source.at((int)row, (int)col))) return false;
    }
  }
  if (source.at(3, 0) != 0 || source.at(3, 1) != 0 || source.at(3, 2) != 0 || source.at(3, 3) != 1) return false;

  // Conjugate the orientation by the same basis used for the mesh. Only translation changes units.
  Mat4 native;
  const Vec3 source_axes[] = {{0, -1, 0}, {0, 0, 1}, {1, 0, 0}};
  Vec3 unit_columns[3];
  for (int col = 0; col < 3; ++col) {
    Vec3 v = source2::direction(source.dir(source_axes[col]));
    const float scale = length(v);
    if (scale <= 1e-12f) return false;
    unit_columns[col] = v / scale;
    for (int row = 0; row < 3; ++row) native.at(row, col) = v[row];
  }
  if (std::fabs(dot(unit_columns[0], unit_columns[1])) > 1e-4f ||
      std::fabs(dot(unit_columns[0], unit_columns[2])) > 1e-4f ||
      std::fabs(dot(unit_columns[1], unit_columns[2])) > 1e-4f) return false;
  const Vec3 position = source2::position(source.translation());
  for (int row = 0; row < 3; ++row) native.at(row, 3) = position[row];
  out = transform_from_matrix(native);
  return true;
}

bool read_tint(const Json &data, bool aggregate, Tint &out) {
  if (data.type == Json::Null) return true;
  if (data.size() != 4 && !(aggregate && data.size() == 3)) return false;
  const double range = aggregate ? 255.0 : 1.0;
  Tint color = untinted;
  for (size_t i = 0; i < data.size(); ++i) {
    const double value = data[i].number(-1);
    if (!(value >= 0 && value <= range)) return false;
    color[i] = value == 0 ? 0.f : float(value / range);
  }
  // A zero alpha is the compiler's unset tint for scene objects.
  if (aggregate || color[3] != 0.f) out = color;
  return true;
}

bool aggregate_lod0(const Json &fragment, size_t setup_count) {
  const double setup = fragment["m_nLODSetupIndex"].number(-1);
  if (!(setup >= 0 && setup < setup_count && setup == std::floor(setup))) return true;
  const double mask = fragment["m_nLODGroupMask"].number();
  if (!(mask > 0 && mask <= std::numeric_limits<uint32_t>::max() && mask == std::floor(mask))) return true;
  return (uint32_t(mask) & 1u) != 0;
}

class MapImport {
 public:
  MapImport(const SourceContent &content, ImportResult &result) : content_(content), result_(result) {}

  bool read_world(const std::string &path) {
    Json world;
    if (!read_data(content_, path, world, result_.error)) {
      result_.error = path + ": " + result_.error;
      return false;
    }
    const Json &nodes = world["m_worldNodes"];
    if (nodes.type != Json::Arr || nodes.arr.empty()) {
      result_.error = path + ": no compiled world nodes";
      return false;
    }
    for (const Json &lightmap : world["m_worldLightingInfo"]["m_lightMaps"].arr)
      if (!lightmap.str.empty()) note(path + ": baked lightmap omitted: " + source2::compiled_path(lightmap.str));
    std::unordered_set<std::string> visited;
    for (const Json &entry : nodes.arr) {
      std::string name = virtual_name(entry["m_worldNodePrefix"].str);
      if (name.empty()) {
        note(path + ": world node without a resource path");
        continue;
      }
      if (ends_with(name, ".vwnod")) name += "_c";
      else if (!ends_with(name, ".vwnod_c")) name += ".vwnod_c";
      if (visited.insert(name).second) read_node(name);
      if (limit_) break;
    }
    if (limit_) result_.error = path + ": too many Source 2 map objects";
    else if (!rendered_)
      result_.error = node_error_.empty() ? path + ": no supported world render geometry could be imported" : node_error_;
    if (!result_.error.empty()) return false;
    result_.summary = "Source 2 map: " + std::to_string(rendered_) + " renderable placements";
    if (missing_) result_.summary += ", " + std::to_string(missing_) + " placeholders";
    return true;
  }

 private:
  const SourceContent &content_;
  ImportResult &result_;
  std::map<std::pair<std::string, int>, ImportResult> renderables_;
  std::unordered_map<std::string, MaterialPtr> overrides_;
  std::unordered_map<const Material *, std::map<Tint, MaterialPtr>> tinted_;
  std::unordered_set<std::string> notes_, textures_;
  std::unordered_set<const Material *> materials_;
  MeshPtr placeholder_mesh_;
  MaterialPtr placeholder_material_;
  std::string node_error_;
  size_t rendered_ = 0, missing_ = 0;
  bool limit_ = false;

  void note(const std::string &message) {
    if (notes_.insert(message).second) result_.notes.push_back(message);
  }

  Tint placement_tint(const Json &data, bool aggregate, const std::string &node_path, const std::string &renderable_path) {
    Tint tint = untinted;
    if (!read_tint(data, aggregate, tint)) note(node_path + ": malformed placement tint for " + renderable_path);
    return tint;
  }

  void read_node(const std::string &path) {
    Json node;
    std::string error;
    if (!read_data(content_, path, node, error)) {
      error = path + ": " + error;
      if (node_error_.empty()) node_error_ = error;
      note(error);
      return;
    }
    const Json &objects = node["m_sceneObjects"];
    const Json &aggregates = node["m_aggregateSceneObjects"];
    std::unordered_map<size_t, std::vector<const Json *>> overrides;
    for (const Json &entry : node["m_materialOverrides"].arr) {
      const auto &material = entry["m_pMaterial"].str;
      if (material.empty()) continue;
      const double index = entry["m_nSceneObjectIndex"].number(-1);
      if (!(index >= 0 && index < objects.size() && index == std::floor(index))) {
        note(path + ": invalid material override scene object for " + material);
        continue;
      }
      overrides[size_t(index)].push_back(&entry);
    }
    for (size_t i = 0; i < objects.size(); ++i) {
      if (limit_) return;
      const auto found = overrides.find(i);
      read_object(objects[i], path, found == overrides.end() ? nullptr : &found->second);
    }
    for (const Json &aggregate : aggregates.arr) {
      if (limit_) return;
      read_aggregate(aggregate, path);
    }
    if (objects.type != Json::Arr && aggregates.type != Json::Arr) note(path + ": no scene object table");
    if (node["m_clutterSceneObjects"].size() != 0) note(path + ": compiled clutter instances are unsupported");
  }

  MaterialPtr placement_material(const std::string &node_path, const std::string &model,
                                 const std::vector<const Json *> *overrides) {
    if (!overrides) return {};
    // The first whole-model override supersedes all individual draw overrides.
    for (const Json *entry : *overrides) {
      if ((*entry)["m_nDrawCallIndex"].number(-1) != std::numeric_limits<uint32_t>::max()) continue;
      const std::string path = source2::compiled_path((*entry)["m_pMaterial"].str);
      auto [found, inserted] = overrides_.try_emplace(path);
      if (inserted) {
        const size_t first_texture = result_.textures.size();
        // Decode into the map result so the override and its textures keep their source IDs.
        found->second = source2::read_material(content_, path, result_);
        textures_.insert(result_.textures.begin() + first_texture, result_.textures.end());
      }
      return found->second;
    }
    for (const Json *entry : *overrides) {
      note(node_path + ": unsupported per-draw material override " + (*entry)["m_pMaterial"].str + " for " + model);
    }
    return {};
  }

  void read_object(const Json &object, const std::string &node_path, const std::vector<const Json *> *overrides) {
    const std::string &model = object["m_renderableModel"].str;
    const std::string &mesh = object["m_renderable"].str;
    if (model.empty() && mesh.empty()) return;
    const std::string &skin = object["m_skin"].str;
    if (!skin.empty() && virtual_name(skin) != "default")
      note(node_path + ": unsupported material skin '" + skin + "' for " + (model.empty() ? mesh : model));
    Transform placement;
    if (!placement_transform(object["m_vTransform"], placement)) {
      note(node_path + ": invalid or unsupported transform for " + (model.empty() ? mesh : model));
      return;
    }
    const Tint tint = placement_tint(object["m_vTintColor"], false, node_path, model.empty() ? mesh : model);
    if (!model.empty()) place(model, -1, placement, tint, placement_material(node_path, model, overrides));
    else if (overrides) {
      for (const Json *entry : *overrides)
        note(node_path + ": unsupported standalone mesh material override " + (*entry)["m_pMaterial"].str + " for " + mesh);
    }
    if (!mesh.empty() && !limit_) place(mesh, -1, placement, tint);
  }

  void read_aggregate(const Json &aggregate, const std::string &node_path) {
    const std::string &model = aggregate["m_renderableModel"].str;
    if (model.empty()) return;
    const Json &fragments = aggregate["m_aggregateMeshes"];
    const Json &transforms = aggregate["m_fragmentTransforms"];
    if (fragments.size() == 0 || fragments[0]["m_nDrawCallIndex"].type == Json::Null) {
      if (transforms.size() == 0) place(model, -1, {});  // Older aggregates are baked in world space.
      else note(node_path + ": unsupported aggregate for " + model);
      return;
    }
    const size_t setup_count = aggregate["m_lodSetups"].size();
    size_t transform_index = 0;
    for (const Json &fragment : fragments.arr) {
      if (limit_) return;
      const Json &has_transform = fragment["m_bHasTransform"];
      const Json *transform = nullptr;
      if (has_transform.b || has_transform.number() != 0) transform = &transforms[transform_index++];
      if (!aggregate_lod0(fragment, setup_count)) continue;
      const double draw = fragment["m_nDrawCallIndex"].number(-1);
      Transform placement;
      if (transform && !placement_transform(*transform, placement)) {
        note(node_path + ": invalid or unsupported aggregate transform for " + model);
        continue;
      }
      if (!(draw >= 0 && draw <= 1000000 && draw == std::floor(draw))) {
        note(node_path + ": invalid aggregate draw call for " + model);
        continue;
      }
      place(model, (int)draw, placement, placement_tint(fragment["m_vTintColor"], true, node_path, model));
    }
    if (transform_index < transforms.size()) note(node_path + ": unused aggregate transforms for " + model);
  }

  ImportResult &renderable(const std::string &path, int draw) {
    auto [entry, inserted] = renderables_.try_emplace({path, draw});
    ImportResult &asset = entry->second;
    if (!inserted) return asset;
    const bool supported = ends_with(path, ".vmdl_c") || ends_with(path, ".vmesh_c");
    const bool loaded = supported && import_source2_renderable(content_, path, draw, asset);
    for (const std::string &message : asset.notes) note(message);
    if (!loaded || std::none_of(asset.nodes.begin(), asset.nodes.end(), [](const ImportedNode &node) { return bool(node.mesh); })) {
      const std::string error = asset.error;
      asset.nodes.clear();
      note(path + (draw < 0 ? "" : " (draw call " + std::to_string(draw) + ")") + ": " +
           (error.empty() ? "no supported render mesh" : error) + ". Using a placeholder");
      return asset;
    }
    for (const MaterialPtr &mat : asset.materials)
      if (mat && materials_.insert(mat.get()).second) result_.materials.push_back(mat);
    for (const std::string &texture : asset.textures)
      if (textures_.insert(texture).second) result_.textures.push_back(texture);
    result_.source_material_ids.insert(asset.source_material_ids.begin(), asset.source_material_ids.end());
    result_.source_texture_ids.insert(asset.source_texture_ids.begin(), asset.source_texture_ids.end());
    return asset;
  }

  MaterialPtr tinted_material(const MaterialPtr &material, const Tint &tint) {
    const MaterialPtr &base = material ? material : default_material();
    auto [found, inserted] = tinted_[base.get()].try_emplace(tint);
    if (!inserted) return found->second;

    auto variant = std::make_shared<Material>(*base);
    // Preserve exact tint bits in the source identity so nearby colors remain separate on reimport.
    static_assert(sizeof(float) == sizeof(uint32_t));
    uint32_t bits[4];
    std::memcpy(bits, tint.data(), sizeof(bits));
    const std::string tint_bits = strprintf("%08x%08x%08x%08x", unsigned(bits[0]), unsigned(bits[1]), unsigned(bits[2]), unsigned(bits[3]));
    variant->name += " Tint " + tint_bits;
    const auto base_id = result_.source_material_ids.find(base.get());
    if (base_id != result_.source_material_ids.end())
      result_.source_material_ids.emplace(variant.get(), base_id->second + "#tint=" + tint_bits);
    else if (base == default_material())
      result_.source_material_ids.emplace(variant.get(), "source2/generated/default-material#tint=" + tint_bits);
    variant->asset_path.clear();
    const Vec3 linear_tint{srgb_to_linear(tint[0]), srgb_to_linear(tint[1]), srgb_to_linear(tint[2])};
    variant->base_color = variant->base_color * linear_tint;
    variant->alpha *= tint[3];
    found->second = variant;
    result_.materials.push_back(variant);
    return variant;
  }

  void set_placement_materials(ImportedNode &node, const Tint &tint, const MaterialPtr &override_material) {
    if (!node.mesh || (!override_material && tint == untinted)) return;
    if (node.materials.empty()) node.materials.push_back(default_material());
    for (MaterialPtr &material : node.materials) {
      if (override_material) material = override_material;
      if (tint != untinted) material = tinted_material(material, tint);
    }
  }

  void place(const std::string &name, int draw, const Transform &placement, const Tint &tint = untinted,
             const MaterialPtr &override_material = {}) {
    std::string path = virtual_name(name);
    if (ends_with(path, ".vmdl") || ends_with(path, ".vmesh")) path += "_c";
    const ImportResult &asset = renderable(path, draw);
    constexpr size_t max_nodes = 500000;
    if (asset.nodes.size() >= max_nodes - result_.nodes.size()) {
      limit_ = true;
      return;
    }

    ImportedNode node;
    node.name = basename(path);
    node.position = placement.position;
    node.rotation = placement.rotation;
    node.scale = placement.scale;
    if (asset.nodes.empty()) {
      place_placeholder(std::move(node));
      return;
    }

    const int parent = (int)result_.nodes.size();
    result_.nodes.push_back(std::move(node));
    for (const ImportedNode &source : asset.nodes) {
      ImportedNode child = source;
      child.parent = source.parent < 0 ? parent : parent + 1 + source.parent;
      child.is_bone = false;
      child.has_bind = false;
      set_placement_materials(child, tint, override_material);
      result_.nodes.push_back(std::move(child));
    }
    ++rendered_;
  }

  void place_placeholder(ImportedNode node) {
    if (!placeholder_mesh_) {
      placeholder_mesh_ = primitives::cube(0.25f);
      placeholder_material_ = std::make_shared<Material>();
      placeholder_material_->name = "Missing Source 2 renderable";
      placeholder_material_->base_color = {1.0f, 0.03f, 0.45f};
      result_.materials.push_back(placeholder_material_);
      result_.source_material_ids.emplace(placeholder_material_.get(), "source2/generated/missing-renderable");
    }
    node.name = "Missing: " + node.name;
    node.mesh = placeholder_mesh_;
    node.materials = {placeholder_material_};
    result_.nodes.push_back(std::move(node));
    ++missing_;
  }
};

}  // namespace

bool import_source2_map(const SourceContent &content, const std::string &virtual_path, ImportResult &out) {
  const std::string path = virtual_name(virtual_path);
  std::string world_path = path;
  ImportResult imported;
  imported.source = virtual_path;
  if (ends_with(path, ".vmap_c")) {
    std::vector<uint8_t> bytes;
    Resource resource;
    std::vector<std::pair<uint64_t, std::string>> references;
    if (!read_map_resource(content, path, bytes, resource, imported.error) ||
        !source2::read_references(resource, references, imported.error)) {
      out.error = path + ": " + imported.error;
      return false;
    }
    world_path = map_world_path(path, references);
  }
  else if (!ends_with(path, ".vwrld_c")) {
    out.error = path + ": expected a compiled Source 2 map";
    return false;
  }
  MapImport map(content, imported);
  if (!map.read_world(world_path)) {
    out.error = std::move(imported.error);
    out.notes = std::move(imported.notes);
    return false;
  }
  out = std::move(imported);
  return true;
}

}  // namespace bl

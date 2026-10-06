// SPDX-License-Identifier: GPL-2.0-or-later
// Asset workflow (Unity: drop files into Assets / onto objects;
// Blender: File > Import, Image Texture node "Open", Material slots > Assign).
#include "editor.h"

#include "../core/core.h"
#include "../scene/import.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace bl {

const std::vector<std::string> &Editor::image_assets() {
  if (ui_.time - image_assets_time_ < 3.0) return image_assets_;
  image_assets_time_ = ui_.time;
  image_assets_.clear();
  std::function<void(const std::string &, int)> scan = [&](const std::string &dir, int depth) {
    if (depth > 6) return;
    for (auto &e : fs::list(dir)) {
      std::string p = fs::join(dir, e.name);
      if (e.is_dir) scan(p, depth + 1);
      else if (image_extension_supported(fs::extension(e.name))) image_assets_.push_back(make_asset_relative(p));
    }
  };
  scan(assets_dir_, 0);
  std::sort(image_assets_.begin(), image_assets_.end());
  return image_assets_;
}

std::vector<MaterialPtr> Editor::scene_materials() {
  std::vector<MaterialPtr> out;
  std::unordered_set<const Material *> seen;
  scene_->for_each_ordered([&](GameObject &g, int) {
    if (auto *mr = g.get<MeshRenderer>())
      for (auto &m : mr->materials)
        if (m && seen.insert(m.get()).second) out.push_back(m);
  });
  return out;
}

void Editor::import_model_file(const std::string &path, bool copy_into_assets) {
  ScopedTimer t;
  ImportResult r;
  if (!import_model(path, r)) {
    Log::error("Import failed (%s): %s", fs::filename(path).c_str(), r.error.c_str());
    return;
  }
  /* Unity copies imported files into Assets: model -> Assets/Meshes,
   * referenced textures -> Assets/Textures, and materials point at the copies. */
  std::unordered_map<std::string, std::string> tex_map;
  if (copy_into_assets) {
    std::string mesh_dir = fs::join(assets_dir_, "Meshes"), tex_dir = fs::join(assets_dir_, "Textures");
    fs::make_dirs(mesh_dir);
    std::string dst = fs::join(mesh_dir, fs::filename(path));
    if (fs::normalize(dst) != fs::normalize(path)) fs::copy_file(path, dst);
    std::string ext = fs::extension(path);
    if (ext == ".obj") {
      /* Bring the .mtl along too. */
      std::string mtl = fs::join(fs::parent(path), fs::stem(path) + ".mtl");
      if (fs::exists(mtl)) fs::copy_file(mtl, fs::join(mesh_dir, fs::filename(mtl)));
    }
    for (const std::string &tp : r.textures) {
      if (!fs::exists(tp) || tex_map.count(tp)) continue;
      fs::make_dirs(tex_dir);
      std::string td = fs::join(tex_dir, fs::filename(tp));
      if (fs::normalize(td) != fs::normalize(tp)) fs::copy_file(tp, td);
      tex_map[tp] = make_asset_relative(td);
    }
  }
  auto fix = [&](TextureRef &ref) {
    if (ref.empty()) return;
    auto it = tex_map.find(ref.path);
    ref.path = it != tex_map.end() ? it->second : make_asset_relative(ref.path);
  };
  size_t missing = 0;
  for (auto &m : r.materials) {
    for (TextureRef *ref : {&m->base_map, &m->metallic_map, &m->roughness_map, &m->normal_map, &m->emission_map}) {
      if (!ref->empty() && !fs::exists(resolve_asset_path(ref->path)) && !fs::exists(ref->path)) missing++;
      fix(*ref);
    }
  }
  GameObject *root = instantiate_import(*scene_, r, fs::stem(path), nullptr);
  if (root) {
    root->set_world_position(cam_.pivot);
    select(root->id);
  }
  mark_changed("Import " + fs::filename(path));
  project_listed_ = -100;
  image_assets_time_ = -100;
  invalidate_material_textures();
  Log::info("Imported %s: %s in %.0f ms", fs::filename(path).c_str(), r.summary.c_str(), t.ms());
  if (missing) Log::warn("%zu texture reference(s) could not be found next to the model", missing);
}

void Editor::assign_texture_to_selection(const std::string &asset_path) {
  int n = 0;
  for (GameObject *g : selected_objects(false)) {
    auto *mr = g->get<MeshRenderer>();
    if (!mr) continue;
    /* Unity: dropping a texture on an object creates a material for it. */
    std::string mname = starts_with(asset_path, "generated:") ? asset_path.substr(10) : fs::stem(asset_path);
    if (mr->materials.empty() || !mr->materials[0]) mr->materials = {make_material(mname, Vec3(1.0f))};
    Material &m = *mr->materials[0];
    m.base_map = TextureRef{asset_path, false};
    m.base_color = Vec3(1.0f);
    m.procedural = 0;
    m.touch();
    n++;
  }
  if (n) {
    invalidate_material_textures();
    mark_changed("Assign Texture");
    Log::info("Assigned %s as Base Map on %d object(s)", asset_path.c_str(), n);
  }
}

void Editor::assign_material_to_faces(int slot) {
  GameObject *g = edit_object();
  if (!g) return;
  Mesh &m = *mesh_make_mutable(*edit_mesh_ptr());
  if (m.face_material.empty()) m.face_material.assign(m.face_count(), 0);
  m.sync_attributes();
  int n = 0;
  for (size_t f = 0; f < m.face_count() && f < face_sel_.size(); f++)
    if (face_sel_[f]) {
      m.face_material[f] = slot;
      n++;
    }
  if (!n) {
    Log::warn("Select faces first (press 3 for face mode)");
    return;
  }
  auto *mr = g->get<MeshRenderer>();
  if (mr) {
    /* Grow the Materials array so the slot exists (new slots get a fresh material). */
    static const Vec3 palette[] = {{0.8f, 0.8f, 0.8f}, {0.85f, 0.35f, 0.25f}, {0.3f, 0.6f, 0.9f}, {0.4f, 0.75f, 0.35f}, {0.9f, 0.75f, 0.3f}};
    while ((int)mr->materials.size() <= slot) {
      size_t i = mr->materials.size();
      mr->materials.push_back(make_material(strprintf("Material.%03zu", i), palette[i % 5]));
    }
  }
  m.touch();
  mark_changed("Assign Material");
  Log::info("Assigned material slot %d to %d face(s)", slot, n);
}

}  // namespace bl

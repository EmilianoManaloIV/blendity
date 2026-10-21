// SPDX-License-Identifier: GPL-2.0-or-later
// Asset workflow (Unity: drop files into Assets / onto objects;
// Blender: File > Import, Image Texture node "Open", Material slots > Assign).
#include "editor.h"

#include "../core/core.h"
#include "../scene/animation.h"
#include "../scene/import.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace bl {

namespace {
/* A file name from a material or model name: letters, digits, spaces, - _ . ( ) kept, anything else _. */
std::string safe_file_name(const std::string &name) {
  std::string s;
  for (char c : name) s += std::isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_' || c == '.' || c == '(' || c == ')' || (unsigned char)c >= 0x80 ? c : '_';
  while (!s.empty() && (s.back() == '.' || s.back() == ' ')) s.pop_back();
  return s.empty() ? std::string("Material") : s;
}

/* A clip's file name: nothing Windows refuses (Blender's stacks are "Armature|ArmatureAction"). */
std::string clip_file_name(std::string n) {
  for (char &ch : n)
    if (std::strchr("/\\:*?\"<>|", ch)) ch = '_';
  while (!n.empty() && (n.front() == '.' || n.front() == ' ')) n.erase(n.begin());
  while (!n.empty() && (n.back() == '.' || n.back() == ' ')) n.pop_back();
  return n.empty() ? std::string("Clip") : n;
}

bool same_file_content(const std::string &a, const std::string &b) {
  std::string x, y;
  return fs::read_file(a, x) && fs::read_file(b, y) && x == y;
}

/* `dir/name`, or `dir/name 2.ext`... when another file of that name is there already. */
std::string free_name(const std::string &dir, const std::string &file, const std::vector<std::string> &taken) {
  std::string p = fs::join(dir, file);
  for (int k = 2; fs::exists(p) || std::find(taken.begin(), taken.end(), p) != taken.end(); k++)
    p = fs::join(dir, fs::stem(file) + " " + std::to_string(k) + fs::extension(file));
  return p;
}
}  // namespace

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
  if (motion_extension_supported(fs::extension(path))) return import_motion_file(path);  // (a motion: onto a character)
  ScopedTimer t;
  ImportResult r;
  if (!import_model(path, r)) {
    Log::error("Import failed (%s): %s", fs::filename(path).c_str(), r.error.c_str());
    return;
  }
  /* Unity copies imported files into Assets: the model into Assets/Meshes (when it comes from outside the project, or
   * dropped), its textures into Assets/Textures/<model> (any not in the project yet: embedded ones, ones beside a model
   * elsewhere), and its materials into Assets/Materials/<model>, pointing at the copies (task 0134). */
  std::unordered_map<std::string, std::string> tex_map;
  std::vector<std::string> sources{path};
  if (copy_into_assets || !inside_assets(path)) sources.push_back(copy_model_into_assets(path));
  const bool again = import_folder_reused(sources, fs::stem(path));
  const std::string model = import_folder(sources, fs::stem(path));
  copy_import_textures(r, model, again, tex_map);
  auto fix = [&](TextureRef &ref) {
    if (ref.empty()) return;
    auto it = tex_map.find(ref.path);
    ref.path = it != tex_map.end() ? it->second : make_asset_relative(ref.path);
  };
  size_t missing = 0;
  for (auto &m : r.materials) {
    for (TextureRef *ref : {&m->base_map, &m->metallic_map, &m->roughness_map, &m->normal_map, &m->emission_map, &m->toon_ramp_map, &m->shade_map,
                            &m->toon_shadow_bias_map, &m->toon_specular_map, &m->toon_rim_map, &m->face_shadow_map, &m->line_map, &m->hatch_map}) {
      if (!ref->empty() && !fs::exists(resolve_asset_path(ref->path)) && !fs::exists(ref->path)) missing++;
      fix(*ref);
    }
  }
  const size_t made = import_material_assets(r, model);
  std::vector<GameObject *> objects;
  GameObject *root = instantiate_import(*scene_, r, fs::stem(path), nullptr, &objects);
  if (root) {
    root->set_world_position(cam_.pivot);
    select(root->id);
  }
  /* Its animations as clips (task 0139), the first playing on the root. The tracks name the nodes; the objects made
   * may differ (an armature renamed for uniqueness): each path is rewritten onto its object. */
  std::vector<std::string> clip_paths;
  std::unordered_map<std::string, int> node_of_path;
  for (size_t i = 0; i < r.nodes.size(); i++) node_of_path.emplace(import_node_path(r, (int)i), (int)i);
  for (size_t i = 0; root && i < r.clips.size(); i++) {
    AnimationClip clip = r.clips[i];
    for (AnimTrack &t : clip.tracks) {
      const auto it = node_of_path.find(t.path);
      if (it != node_of_path.end() && objects[(size_t)it->second]) t.path = anim_path(*root, *objects[(size_t)it->second]);
      else if (it != node_of_path.end()) t.keys.clear();  // (a folded end bone's track: nothing to move)
    }
    /* Tracks with no keys dropped: the folded nodes' (cleared above) and any a reader left empty. */
    clip.tracks.erase(std::remove_if(clip.tracks.begin(), clip.tracks.end(), [](const AnimTrack &t) { return t.keys.empty(); }), clip.tracks.end());
    const std::string rel = save_imported_clip(*root, clip, model, i == 0);
    if (!rel.empty()) clip_paths.push_back(rel);
    else Log::warn("Import: the clip '%s' couldn't be written under Assets/Animations/%s", clip.name.c_str(), model.c_str());
  }
  mark_changed("Import " + fs::filename(path));
  project_listed_ = -100;
  image_assets_time_ = -100;
  invalidate_material_textures();
  Log::info("Imported %s: %s in %.0f ms", fs::filename(path).c_str(), r.summary.c_str(), t.ms());
  if (made) Log::info("Materials: %zu in Assets/Materials/%s (an edited one is kept when the model is imported again)", made, model.c_str());
  for (const std::string &c : clip_paths) Log::info("Clip: %s%s", c.c_str(), c == clip_paths.front() ? " (playing on the root: Window > Animation)" : "");
  for (const std::string &n : r.notes) Log::info("Import: %s", n.c_str());
  if (missing) Log::warn("%zu texture reference(s) could not be found next to the model", missing);
}

/* The character a motion goes onto: the one the selection is in (its topmost object), else the scene's only one. */
GameObject *Editor::motion_target() {
  auto has_rig = [](GameObject &top) {
    bool found = false;
    std::function<void(GameObject &)> walk = [&](GameObject &g) {
      found = found || g.get<Armature>() || g.get<Bone>();
      for (GameObject *c : g.children)
        if (!found) walk(*c);
    };
    walk(top);
    return found;
  };
  GameObject *top = active_object();
  while (top && top->parent) top = top->parent;
  if (top && has_rig(*top)) return top;
  GameObject *only = nullptr;
  int count = 0;
  for (GameObject *g : scene_->roots)
    if (has_rig(*g)) only = g, count++;
  return count == 1 ? only : nullptr;
}

/* A motion file (task 0137: MMD's .vmd) onto a character: Assets/Animations/<character>/<motion>.anim, played by the
 * character's Animation component (Unity's way), shown in the Animation window. */
void Editor::import_motion_file(const std::string &path) {
  if (playing_) return Log::warn("Import Motion: stop Play first");
  GameObject *root = motion_target();
  if (!root) return Log::warn("Import %s: select the character it moves (an object with bones)", fs::filename(path).c_str());
  AnimationClip clip;
  std::vector<std::string> notes;
  std::string error;
  if (!import_vmd_file(path, *root, clip, notes, error)) return Log::error("Import failed (%s): %s", fs::filename(path).c_str(), error.c_str());
  const std::string rel = save_imported_clip(*root, clip, root->name, true);
  if (rel.empty()) return Log::warn("Import Motion: couldn't write the clip for %s", fs::filename(path).c_str());
  const std::string copy = fs::join(fs::parent(resolve_asset_path(rel)), fs::filename(path));  // (the motion kept beside its clip, as models are)
  if (fs::normalize(copy) != fs::normalize(path)) fs::copy_file(path, copy);
  mark_changed("Import Motion");
  Log::info("Imported %s onto '%s': %s", fs::filename(path).c_str(), root->name.c_str(), rel.c_str());
  for (const std::string &n : notes) Log::info("Import: %s", n.c_str());
}

std::string Editor::save_imported_clip(GameObject &root, const AnimationClip &clip, const std::string &folder, bool play) {
  const std::string dir = "Assets/Animations/" + clip_file_name(folder), base = clip_file_name(clip.name);
  std::string rel = dir + "/" + base + ".anim";
  for (int k = 2; fs::exists(resolve_asset_path(rel)); k++) rel = dir + "/" + base + " " + std::to_string(k) + ".anim";
  fs::make_dirs(fs::parent(resolve_asset_path(rel)));
  AnimationClipPtr c = create_clip_asset(rel, clip.name);
  if (!c) return "";
  const std::string asset_path = c->asset_path;
  *c = clip;
  c->asset_path = asset_path;
  c->touch();
  save_dirty_clip_assets();
  anim_clip_paths_listed_ = -100;
  invalidate_project_listing();
  if (!play) return rel;
  Animation *an = root.get<Animation>();
  if (!an) an = root.add<Animation>();
  an->clip = rel;
  anim_.root = root.id;
  return rel;
}

/* shape list | add <name> [amount] | set <name> <weight> | remove <name> (task 0135): the selected object's shape keys.
 * add makes a key pushing every vertex out along its normal by `amount` (0.1 m): an inflate morph to animate. */
void Editor::shape_command(const std::vector<std::string> &t) {
  GameObject *g = active_object();
  MeshFilter *mf = g ? g->mesh_filter() : nullptr;
  if (!mf || !mf->mesh) return Log::warn("shape: select an object with a mesh");
  const std::string sub = t.size() > 1 ? to_lower(t[1]) : "list";
  MeshRenderer *mr = g->get<MeshRenderer>();
  if (sub == "list") {
    if (mf->mesh->shape_keys.empty()) return Log::info("%s has no shape keys (shape add <name>)", g->name.c_str());
    for (const ShapeKey &k : mf->mesh->shape_keys)
      Log::info("  %-24s %.3f%s", k.name.c_str(), mr ? mr->shape_weight(k.name) : 0.0f, k.delta.size() == mf->mesh->vert_count() ? "" : " (the mesh was edited since: off)");
    return;
  }
  if (t.size() < 3) return Log::warn("shape list | add <name> [amount] | set <name> <weight> | remove <name>");
  const std::string name = t[2];
  if (sub == "set") {
    if (!mr) mr = g->add<MeshRenderer>();
    mr->set_shape_weight(name, t.size() > 3 ? (float)std::atof(t[3].c_str()) : 1.0f);
    return mark_changed("Shape Key Weight");
  }
  Mesh &m = *mesh_make_mutable(mf->mesh);
  if (sub == "remove") {
    const int i = m.find_shape(name);
    if (i < 0) return Log::warn("shape: no key '%s'", name.c_str());
    m.shape_keys.erase(m.shape_keys.begin() + i);
    m.touch();
    return mark_changed("Remove Shape Key");
  }
  if (sub != "add") return Log::warn("shape list | add <name> [amount] | set <name> <weight> | remove <name>");
  if (m.find_shape(name) >= 0) return Log::warn("shape: '%s' is there already", name.c_str());
  const float amount = t.size() > 3 ? (float)std::atof(t[3].c_str()) : 0.1f;
  const std::vector<Vec3> n = meshops::vertex_normals(m);
  ShapeKey k{name, std::vector<Vec3>(m.vert_count(), Vec3(0.0f))};
  for (size_t i = 0; i < k.delta.size() && i < n.size(); i++) k.delta[i] = n[i] * amount;
  m.shape_keys.push_back(std::move(k));
  m.touch();
  if (!mr) g->add<MeshRenderer>();
  mark_changed("Add Shape Key");
  Log::info("Shape key '%s' on %s: its weight in the Inspector (MeshRenderer > BlendShape/%s), animatable", name.c_str(), g->name.c_str(), name.c_str());
}

/* A file in the project's Assets already. */
bool Editor::inside_assets(const std::string &path) const {
  const std::string a = to_lower(fs::normalize(assets_dir_)), p = to_lower(fs::normalize(path));
  return p.size() > a.size() && p.compare(0, a.size(), a) == 0 && (p[a.size()] == '/' || p[a.size()] == '\\');
}

/* The model file into Assets/Meshes, with its .mtl (OBJ) or .bin (glTF) beside it; where it went. A different model
 * of the same name there already (every Sketchfab download is scene.gltf) puts this one in a folder of its own,
 * Meshes/<name> 2/, so the file still finds its .bin by the name it gives it. */
std::string Editor::copy_model_into_assets(const std::string &path) {
  std::string mesh_dir = fs::join(assets_dir_, "Meshes");
  std::string dst = fs::join(mesh_dir, fs::filename(path));
  for (int k = 2; fs::exists(dst) && fs::normalize(dst) != fs::normalize(path) && !same_file_content(dst, path); k++) {
    mesh_dir = fs::join(fs::join(assets_dir_, "Meshes"), fs::stem(path) + " " + std::to_string(k));
    dst = fs::join(mesh_dir, fs::filename(path));
  }
  fs::make_dirs(mesh_dir);
  if (fs::normalize(dst) != fs::normalize(path)) fs::copy_file(path, dst);
  const std::string ext = fs::extension(path);
  if (ext != ".obj" && ext != ".gltf") return dst;
  const std::string side = fs::join(fs::parent(path), fs::stem(path) + (ext == ".obj" ? ".mtl" : ".bin"));
  if (fs::exists(side)) fs::copy_file(side, fs::join(mesh_dir, fs::filename(side)));
  return dst;
}

/* The folder name under Assets/Materials and Assets/Textures for a model's import: its name, or "<name> 2"... when
 * that folder came from another model of the same name. Each folder lists the files it was imported from
 * (.import-source), so importing the same model again reuses its folder - and the .mat files edited since. */
std::string Editor::import_folder(const std::vector<std::string> &sources, const std::string &stem) {
  const std::string base = safe_file_name(stem);
  std::string name = base;
  for (int k = 2; k < 1000; k++) {
    const std::string dir = fs::join(fs::join(assets_dir_, "Materials"), name);
    if (!fs::exists(dir) || import_folder_matches(dir, sources)) break;
    name = base + " " + std::to_string(k);
  }
  const std::string dir = fs::join(fs::join(assets_dir_, "Materials"), name), marker = fs::join(dir, ".import-source");
  std::string text;
  fs::read_file(marker, text);
  for (const std::string &s : sources)
    if (!import_folder_matches(dir, {s})) text += to_lower(fs::normalize(s)) + "\n";
  fs::make_dirs(dir);
  fs::write_file(marker, text);
  return name;
}

bool Editor::import_folder_matches(const std::string &dir, const std::vector<std::string> &sources) const {
  std::string text;
  if (!fs::read_file(fs::join(dir, ".import-source"), text)) return false;
  for (const std::string &s : sources)
    if (("\n" + text).find("\n" + to_lower(fs::normalize(s)) + "\n") != std::string::npos) return true;
  return false;
}

/* Whether this model was imported before (its folder is there): its textures are kept as they are then. */
bool Editor::import_folder_reused(const std::vector<std::string> &sources, const std::string &stem) const {
  const std::string base = safe_file_name(stem);
  for (int k = 1; k < 1000; k++) {
    const std::string dir = fs::join(fs::join(assets_dir_, "Materials"), k == 1 ? base : base + " " + std::to_string(k));
    if (!fs::exists(dir)) return false;
    if (import_folder_matches(dir, sources)) return true;
  }
  return false;
}

/* Textures outside the project into Assets/Textures/<folder>/: two of one name get two files ("body 2.png"); a texture
 * from an earlier import of this model is kept (it may have been edited, as the .mat files are). */
void Editor::copy_import_textures(const ImportResult &r, const std::string &folder, bool again, std::unordered_map<std::string, std::string> &tex_map) {
  const std::string tex_dir = fs::join(fs::join(assets_dir_, "Textures"), folder);
  std::vector<std::string> taken;
  for (const std::string &tp : r.textures) {
    if (!fs::exists(tp) || tex_map.count(tp) || inside_assets(tp)) continue;
    fs::make_dirs(tex_dir);
    const std::string same = fs::join(tex_dir, fs::filename(tp));
    const bool keep = again && fs::exists(same) && std::find(taken.begin(), taken.end(), same) == taken.end();
    const std::string td = keep || (fs::exists(same) && same_file_content(same, tp)) ? same : free_name(tex_dir, fs::filename(tp), taken);
    if (td != same || !fs::exists(td)) fs::copy_file(tp, td);
    taken.push_back(td);
    tex_map[tp] = make_asset_relative(td);
  }
}

/* Unity's Extract Materials, on import (task 0134): every material of the model as Assets/Materials/<model>/<name>.mat,
 * the model's renderers using those assets. A .mat already there (an earlier import, maybe edited since) is used as it
 * is, never overwritten. The count made or reused. */
size_t Editor::import_material_assets(ImportResult &r, const std::string &model) {
  std::unordered_map<const Material *, MaterialPtr> asset_of;
  std::unordered_set<std::string> names;
  for (const MaterialPtr &m : r.materials) {
    if (!m || asset_of.count(m.get())) continue;
    std::string base = safe_file_name(m->name.empty() ? model : m->name), name = base;
    for (int k = 2; names.count(to_lower(name)); k++) name = base + " " + std::to_string(k);  // (two of one name)
    names.insert(to_lower(name));
    const std::string rel = "Assets/Materials/" + safe_file_name(model) + "/" + name + ".mat";
    MaterialPtr a = fs::exists(resolve_asset_path(rel)) ? material_asset(rel) : nullptr;
    if (!a) {
      fs::make_dirs(fs::parent(resolve_asset_path(rel)));
      a = create_material_asset(*m, rel);
    }
    if (a) asset_of[m.get()] = a;
  }
  auto swap = [&](MaterialPtr &m) {
    const auto it = m ? asset_of.find(m.get()) : asset_of.end();
    if (it != asset_of.end()) m = it->second;
  };
  for (ImportedNode &n : r.nodes)
    for (MaterialPtr &m : n.materials) swap(m);
  for (MaterialPtr &m : r.materials) swap(m);
  return asset_of.size();
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

// SPDX-License-Identifier: GPL-2.0-or-later
// Import a Unity prefab or scene (round 370, task 0347): a .prefab or .unity file from an unpacked package (or a Unity
// project's Assets copied in) made into objects under one root - its meshes found by GUID through the packages'
// .unity-guids and the .meta files, a model's submesh by the fileID its .meta names; nested prefabs, lights,
// cameras, colliders; scripts and the rest kept as Engine Components. One Undo step.
#include "editor.h"

#include "../scene/import.h"
#include "../scene/material.h"
#include "../scene/unity_material.h"
#include "../scene/unity_scene.h"
#include "../core/core.h"

#include <algorithm>
#include <cstdlib>
#include <set>

namespace bl {

namespace {

using GuidIndex = UnityGuidIndex;

/* A package folder's .unity-guids lines, with the .meta texts kept in its .unity-meta folder. */
void read_package_guids(const std::string &dir, const std::string &project_root, GuidIndex &out) {
  std::string text;
  if (!fs::read_file(fs::join(dir, ".unity-guids"), text)) return;
  for (size_t at = 0; at < text.size();) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) end = text.size();
    const std::string line = text.substr(at, end - at);
    at = end + 1;
    const size_t t1 = line.find('\t'), t2 = t1 == std::string::npos ? t1 : line.find('\t', t1 + 1);
    if (t2 == std::string::npos) continue;
    UnityGuidEntry &e = out[line.substr(0, t1)];
    e.path = fs::join(project_root, line.substr(t2 + 1));
    fs::read_file(fs::join(fs::join(dir, ".unity-meta"), line.substr(0, t1) + ".meta"), e.meta);
  }
}

}  // namespace

/* Every GUID the project knows: the unpacked packages' and any .meta file beside an asset (a Unity project's Assets
 * copied in). */
GuidIndex unity_guid_index(const std::string &assets_dir, const std::string &project_root) {
  GuidIndex out;
  std::function<void(const std::string &, int)> walk = [&](const std::string &dir, int depth) {
    if (depth > 16) return;
    read_package_guids(dir, project_root, out);
    for (const DirEntry &de : fs::list(dir)) {
      const std::string p = fs::join(dir, de.name);
      if (de.is_dir) {
        walk(p, depth + 1);
        continue;
      }
      if (fs::extension(de.name) != ".meta") continue;
      std::string meta;
      fs::read_file(p, meta);
      const std::string guid = meta_guid(meta);
      if (!guid.empty() && !out.count(guid)) out[guid] = {p.substr(0, p.size() - 5), meta};
    }
  };
  walk(assets_dir, 0);
  return out;
}

std::shared_ptr<const UnityGuidIndex> unity_guid_index_cached(const std::string &assets_dir, const std::string &project_root, bool fresh) {
  static std::shared_ptr<const UnityGuidIndex> kept;
  static std::string kept_root;
  static double kept_at = -1e9;
  if (!fresh && kept && kept_root == assets_dir && now_seconds() - kept_at < 5.0) return kept;
  kept = std::make_shared<const UnityGuidIndex>(unity_guid_index(assets_dir, project_root));
  kept_root = assets_dir, kept_at = now_seconds();
  return kept;
}

std::string unity_shader_name(const UnityGuidIndex &index, const std::string &guid, int64_t file_id) {
  if (guid == "0000000000000000f000000000000000") return file_id == 45 ? "Standard (Specular setup)" : file_id == 46 ? "Standard" : "Unlit";
  if (guid == "933532a4fcc9baf4fa0491de14d08ed7") return "Lit";      // (URP's Lit)
  if (guid == "8d2bb70cbf9db8d4da26e15b26e74248") return "SimpleLit";
  if (guid == "650dd9526735d5b46b79224bc6e94025") return "Unlit";    // (URP's Unlit)
  const auto it = index.find(guid);
  if (it == index.end()) return std::string();
  /* The name the shader declares (Shader "Hidden/lilToonOutline"): its file's name is often a code ("lts_o"). */
  std::string head;
  fs::read_prefix(it->second.path, 4096, head);
  const size_t at = head.find("Shader \""), end = at == std::string::npos ? at : head.find('"', at + 8);
  return end == std::string::npos ? fs::stem(it->second.path) : head.substr(at + 8, end - at - 8);
}

bool load_unity_material(const std::string &text, const std::string &path, Material &m) {
  if (text.find("--- !u!21 ") == std::string::npos) return false;  // (a Material document: anything else isn't one - no index walked for it)
  const std::string assets = resolve_asset_path("Assets");
  const std::shared_ptr<const UnityGuidIndex> index = unity_guid_index_cached(assets, fs::parent(assets));
  UnityMaterialInfo info;
  const bool ok = unity_material(text, [&](const std::string &guid) {
        const auto it = index->find(guid);
        return it == index->end() ? std::string() : make_asset_relative(it->second.path);
      }, [&](const std::string &guid, int64_t fid) { return unity_shader_name(*index, guid, fid); }, m, info);
  if (!ok) return false;
  m.import_outline = info.outline_width, m.import_outline_color = info.outline_color;
  for (const std::string &n : info.notes) Log::info("Unity material %s: %s", path.c_str(), n.c_str());
  if (!info.known) Log::warn("Unity material %s: its shader (%s) isn't one Blendity reads - its main texture and colour came in", path.c_str(), info.shader.c_str());
  return true;
}

namespace {

/* The mesh name a model's .meta gives a fileID (Unity 2019+: internalIDToNameTable; before: fileIDToRecycleName). */
std::string meta_mesh_name(const YamlNode &root, int64_t file_id) {
  const YamlNode *importer = nullptr;  // (ModelImporter, or any importer that names its sub-assets)
  for (const auto &[key, v] : root.map) importer = key.find("Importer") != std::string::npos ? &v : importer;
  if (!importer) return "";
  const std::string fid = std::to_string(file_id);
  if (const YamlNode *recycle = importer->get("fileIDToRecycleName"))
    for (const auto &[k, name] : recycle->map)
      if (k == fid) return name.value;
  const YamlNode *table = importer->get("internalIDToNameTable");
  for (const YamlNode &e : table ? table->seq : std::vector<YamlNode>()) {
    const YamlNode *first = e.get("first");
    bool match = false;
    for (const auto &[cls, id] : first ? first->map : std::vector<std::pair<std::string, YamlNode>>()) match = match || id.value == fid;
    if (match) return e.str("second");
  }
  return "";
}

/* What one import reads once (review 370: a prefab used a hundred times was read a hundred times). */
struct ImportCache {
  std::map<std::string, ImportResult> models;          // (by path)
  std::map<std::string, YamlNode> metas;               // (by GUID: a model's .meta, parsed)
  std::map<std::string, std::vector<UnityDoc>> prefabs; // (by GUID; empty: not one)
  int first_mesh_fallbacks = 0;
};

const ImportResult &model_of(ImportCache &c, const std::string &path) {
  auto m = c.models.find(path);
  if (m != c.models.end()) return m->second;
  m = c.models.emplace(path, ImportResult{}).first;
  if (!import_model(path, m->second)) Log::warn("Import Unity: %s - %s", fs::filename(path).c_str(), m->second.error.c_str());
  return m->second;
}

/* A model's part for a MeshFilter: the one its .meta names for the fileID, else the node or mesh named as the object
 * (Unity names a model's objects after its nodes; 2019+ .meta files often list nothing), else the first - counted. */
const ImportedNode *pick_part(const ImportResult &r, const std::string &name, const std::string &object, int &fallbacks) {
  const ImportedNode *first = nullptr, *by_meta = nullptr, *by_object = nullptr;
  for (const ImportedNode &n : r.nodes) {
    if (!n.mesh) continue;
    first = first ? first : &n;
    by_meta = by_meta ? by_meta : !name.empty() && (n.mesh->name == name || n.name == name) ? &n : nullptr;
    by_object = by_object ? by_object : n.name == object || n.mesh->name == object ? &n : nullptr;
  }
  if (by_meta || by_object) return by_meta ? by_meta : by_object;
  fallbacks += first != nullptr;
  return first;
}

MeshPtr builtin_mesh(int64_t file_id) {
  const char *kind = unity_builtin_mesh(file_id);
  if (!kind) return nullptr;
  const std::string k = kind;
  if (k == "Cube") return primitives::cube();
  if (k == "Sphere") return primitives::uv_sphere();
  if (k == "Cylinder") return primitives::cylinder();
  if (k == "Capsule") return primitives::capsule();
  if (k == "Plane") return primitives::plane();
  return primitives::quad();
}

}  // namespace

/* import <file>.prefab|.unity: its objects under one root named for the file, selected; one Undo step. */
bool Editor::import_unity_scene(const std::string &path) {
  std::string text, why;
  std::vector<UnityDoc> docs;
  if (!fs::read_file(path, text)) return Log::warn("Import Unity %s: can't read %s", fs::extension(path).c_str(), path.c_str()), false;
  if (!parse_unity_yaml(text, docs, why)) return Log::warn("Import Unity %s: %s - %s", fs::extension(path).c_str(), fs::filename(path).c_str(), why.c_str()), false;
  const std::shared_ptr<const GuidIndex> kept = unity_guid_index_cached(assets_dir_, project_root_, true);
  const GuidIndex &index = *kept;
  ImportCache cache;
  auto model_path = [&](const std::string &guid) {
    const auto it = index.find(guid);
    return it != index.end() && model_extension_supported(fs::extension(it->second.path)) ? it->second.path : std::string();
  };
  UnityImport ctx;
  ctx.source = make_asset_relative(path);  // (round 372: each object's UnityOrigin)
  std::replace(ctx.source.begin(), ctx.source.end(), '\\', '/');
  ctx.path_of = [&](const std::string &guid) {
    const auto it = index.find(guid);
    std::string p = it == index.end() ? std::string() : make_asset_relative(it->second.path);
    std::replace(p.begin(), p.end(), '\\', '/');
    return p;
  };
  ctx.mesh = [&](const std::string &guid, int64_t fid, const std::string &object, std::vector<MaterialPtr> &mats) -> MeshPtr {
    if (guid == kUnityBuiltinGuid) return builtin_mesh(fid);
    const std::string p = model_path(guid);
    if (p.empty()) return nullptr;
    auto meta = cache.metas.find(guid);
    if (meta == cache.metas.end()) meta = cache.metas.emplace(guid, parse_yaml(index.at(guid).meta)).first;
    const ImportedNode *pick = pick_part(model_of(cache, p), meta_mesh_name(meta->second, fid), object, cache.first_mesh_fallbacks);
    if (pick) mats = pick->materials;
    return pick ? pick->mesh : nullptr;
  };
  ctx.script_name = [&](const std::string &guid) {  // (a .cs file's class is its name; a DLL's many classes aren't)
    const auto it = index.find(guid);
    return it == index.end() || fs::extension(it->second.path) != ".cs" ? std::string() : fs::stem(it->second.path);
  };
  ctx.prefab = [&](const std::string &guid, std::vector<UnityDoc> &out) {
    auto c = cache.prefabs.find(guid);
    if (c == cache.prefabs.end()) {
      const auto it = index.find(guid);
      std::string t, w;
      std::vector<UnityDoc> docs;
      if (it != index.end() && fs::extension(it->second.path) == ".prefab" && fs::read_file(it->second.path, t)) parse_unity_yaml(t, docs, w);
      c = cache.prefabs.emplace(guid, std::move(docs)).first;
    }
    out = c->second;
    return !out.empty();
  };
  ctx.material = [&](const std::string &guid) -> MaterialPtr {  // (round 371: a Unity .mat, converted as it loads)
    const auto it = index.find(guid);
    return it == index.end() || fs::extension(it->second.path) != ".mat" ? nullptr : material_asset(make_asset_relative(it->second.path));
  };
  ctx.model = [&](const std::string &guid, Scene &scene) -> GameObject * {
    const std::string p = model_path(guid);
    return p.empty() ? nullptr : objects_from_model(model_of(cache, p), fs::stem(p), scene);
  };
  commit_undo();
  std::set<uint64_t> before;
  scene_->for_each([&](GameObject &g) { before.insert(g.id); });
  GameObject *root = scene_->create(fs::stem(path));
  root->set_world_position(cam_.pivot);
  build_unity_objects(docs, *scene_, root, ctx);
  std::vector<GameObject *> strays;  // (review 370: a variant's base, a parent loop - anything left at the top goes under the root)
  scene_->for_each([&](GameObject &g) {
    if (!g.parent && &g != root && !before.count(g.id)) strays.push_back(&g);
  });
  for (GameObject *g : strays) scene_->set_parent(g, root, -1, false);
  if (ctx.objects == 0 && strays.empty() && root->children.empty()) {  // (nothing in it: no object, no Undo step)
    scene_->destroy(root);
    return Log::warn("Import Unity: %s has no objects", fs::filename(path).c_str()), false;
  }
  expanded_.insert(root->id);
  select(root->id);
  mark_changed("Import " + fs::filename(path));
  if (cache.first_mesh_fallbacks) ctx.notes.push_back(std::to_string(cache.first_mesh_fallbacks) + " mesh part(s) not named by their model's .meta or object: the model's first mesh used");
  if (ctx.dropped) ctx.notes.push_back(std::to_string(ctx.dropped) + " component(s) on objects that weren't found (a nested prefab not in the project) were left out");
  if (ctx.materials || ctx.outlines || ctx.springs)
    Log::info("Import Unity: %d Unity material(s) converted, %d outline(s), %d PhysBone setting(s) kept as spring bones", ctx.materials, ctx.outlines, ctx.springs);
  for (const auto &[n, count] : count_notes(ctx.notes)) Log::warn("Import Unity: %s%s", n.c_str(), count > 1 ? strprintf(" (x%d)", count).c_str() : "");
  Log::info("Import Unity: %s - %d object(s), %d mesh(es)%s, %d light(s), %d camera(s), %d collider(s), %d nested prefab(s), %d engine "
            "component(s) kept as they are (scripts, VRChat's)%s", fs::filename(path).c_str(), ctx.objects, ctx.meshes,
            ctx.missing_meshes ? strprintf(" (%d not found)", ctx.missing_meshes).c_str() : "", ctx.lights, ctx.cameras, ctx.colliders, ctx.prefabs, ctx.engine,
            ctx.overrides_kept ? strprintf(", %d prefab override(s) Blendity doesn't hold left to Unity", ctx.overrides_kept).c_str() : "");
  return true;
}

}  // namespace bl

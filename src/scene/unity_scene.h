// SPDX-License-Identifier: GPL-2.0-or-later
// Unity prefabs and scenes into Blendity's objects (round 370, task 0347): GameObjects with their Transforms (Unity
// and Blendity share Y-up, left-handed, metres: nothing flips), MeshFilter / MeshRenderer / SkinnedMeshRenderer meshes,
// Lights, Cameras, Box / Sphere / Capsule / Mesh Colliders, nested prefabs with their overrides - and every other
// component (scripts, Udon, VRChat's) an EngineComponent with its YAML kept as it was.
#pragma once

#include "scene.h"
#include "unity_yaml.h"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace bl {

/* Unity's built-in meshes (Library/unity default resources), by the fileID a MeshFilter names them with. */
const char *unity_builtin_mesh(int64_t file_id);
constexpr const char *kUnityBuiltinGuid = "0000000000000000e000000000000000";

/* What the builder asks for and what it counts. */
struct UnityImport {
  /* A mesh by its asset's GUID and the fileID within it (an FBX's submesh), for the object of that name (Unity names
   * a model's GameObjects after its nodes: the part when the .meta names none); `materials` its model's own. */
  std::function<MeshPtr(const std::string &guid, int64_t file_id, const std::string &object, std::vector<MaterialPtr> &materials)> mesh;
  /* A script's class by its GUID ("" when unknown, or not a .cs file: a DLL holds many). */
  std::function<std::string(const std::string &guid)> script_name;
  /* A prefab's documents by its GUID (false when it isn't in the project, or isn't a YAML prefab). */
  std::function<bool(const std::string &guid, std::vector<UnityDoc> &docs)> prefab;
  /* A model placed as a prefab (an FBX dragged into a scene: a VRChat avatar's usual root): its nodes made as objects
   * under one, returned; nullptr when the GUID isn't a model. */
  std::function<GameObject *(const std::string &guid, Scene &scene)> model;
  /* A material by its GUID (round 371: a Unity .mat converted; nullptr when not in the project). */
  std::function<MaterialPtr(const std::string &guid)> material;
  int depth = 0;
  /* Shared down the nesting (review 370): the prefabs being built (one instancing itself is refused) and the objects
   * made so far against a cap. */
  std::shared_ptr<std::set<std::string>> building;
  std::shared_ptr<int> total;
  int max_objects = 200000;
  int objects = 0, meshes = 0, missing_meshes = 0, lights = 0, cameras = 0, colliders = 0, engine = 0, prefabs = 0, overrides_kept = 0;
  int dropped = 0;  // (components whose object couldn't be found)
  int materials = 0, outlines = 0, springs = 0;  // (round 371)
  std::vector<std::string> notes;
};

/* Every GUID a project knows (round 371: shared by the scene import and the material loader): the unpacked packages'
 * .unity-guids with their kept .meta texts, and any .meta beside an asset. */
struct UnityGuidEntry {
  std::string path, meta;  // (absolute; the .meta's text)
};
using UnityGuidIndex = std::map<std::string, UnityGuidEntry>;
UnityGuidIndex unity_guid_index(const std::string &assets_dir, const std::string &project_root);
/* The project's index, kept a few seconds (a material loads a texture's path through it; many load at once). */
/* `fresh`: read again now (an import: files may have just arrived). */
std::shared_ptr<const UnityGuidIndex> unity_guid_index_cached(const std::string &assets_dir, const std::string &project_root, bool fresh = false);
/* A Unity shader's name by its GUID and fileID: the built-in Standard, URP's Lit and Unlit, else its file's name. */
std::string unity_shader_name(const UnityGuidIndex &index, const std::string &guid, int64_t file_id);
/* The material loader for Unity .mat files (set_foreign_material_loader): its textures and shader through the
 * project's index. */
bool load_unity_material(const std::string &text, const std::string &path, Material &m);

/* The documents' objects made in `scene` under `parent`, by their GameObject's and Transform's fileIDs. */
std::map<int64_t, GameObject *> build_unity_objects(const std::vector<UnityDoc> &docs, Scene &scene, GameObject *parent, UnityImport &ctx);

}  // namespace bl

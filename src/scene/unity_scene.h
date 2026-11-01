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
  int depth = 0;
  /* Shared down the nesting (review 370): the prefabs being built (one instancing itself is refused) and the objects
   * made so far against a cap. */
  std::shared_ptr<std::set<std::string>> building;
  std::shared_ptr<int> total;
  int max_objects = 200000;
  int objects = 0, meshes = 0, missing_meshes = 0, lights = 0, cameras = 0, colliders = 0, engine = 0, prefabs = 0, overrides_kept = 0;
  int dropped = 0;  // (components whose object couldn't be found)
  std::vector<std::string> notes;
};

/* The documents' objects made in `scene` under `parent`, by their GameObject's and Transform's fileIDs. */
std::map<int64_t, GameObject *> build_unity_objects(const std::vector<UnityDoc> &docs, Scene &scene, GameObject *parent, UnityImport &ctx);

}  // namespace bl

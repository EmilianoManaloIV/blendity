// SPDX-License-Identifier: GPL-2.0-or-later
// Model import (Unity: drop a model into Assets; Blender: File > Import).
//   .obj + .mtl  - own parser (blender/source/blender/io/wavefront_obj)
//   .glb .gltf .stl .ply .usda - import_formats.cpp
//   .fbx         - ufbx, the FBX loader Blender itself uses (blender/extern/ufbx,
//                  blender/source/blender/io/fbx)
// Everything is converted to Unity's left-handed Y-up metre space by
// mirroring X and reversing face winding, the same thing Unity's importer does.
#pragma once

#include "material.h"
#include "mesh.h"
#include "scene.h"

#include <string>
#include <vector>

namespace bl {

struct ImportedNode {
  std::string name;
  int parent = -1;  // index into nodes, -1 = root
  Vec3 position;
  Quat rotation;
  Vec3 scale{1, 1, 1};
  MeshPtr mesh;
  std::vector<MaterialPtr> materials;
  bool is_bone = false;  // a joint of a skin (task 0047): becomes a Bone, its local transform its rest
  /* The joint's bind pose (task 0056), when the file has one: where it was when the skin was bound, in the import's
   * own space and Blendity's axes. It becomes the Bone's rest; the saved transform stays as the pose. */
  bool has_bind = false;
  Mat4 bind_world;
};

/* A file's right-handed matrix in Blendity's axes: the X mirror on both sides (S M S, S = diag(-1, 1, 1, 1)). */
inline Mat4 mirror_x(const Mat4 &m) {
  Mat4 r = m;
  for (int c = 0; c < 4; c++)
    for (int k = 0; k < 4; k++)
      if ((c == 0) != (k == 0)) r.at(k, c) = -r.at(k, c);
  return r;
}

/* A skinned mesh (task 0047): its node and its joints' nodes. The mesh's vertex groups are named after the joints. */
struct ImportedSkin {
  int mesh_node = -1;
  std::vector<int> joints;
};

struct ImportResult {
  std::vector<ImportedNode> nodes;
  std::vector<MaterialPtr> materials;
  std::vector<std::string> textures;  // absolute paths referenced by materials
  std::vector<ImportedSkin> skins;
  std::string error;
  std::string summary;
};

bool import_model(const std::string &path, ImportResult &out);
bool import_obj_file(const std::string &path, ImportResult &out);
bool import_fbx_file(const std::string &path, ImportResult &out);
bool import_gltf_file(const std::string &path, ImportResult &out);  // import_formats.cpp
bool import_stl_file(const std::string &path, ImportResult &out);   // Z up, like Blender writes it
bool import_ply_file(const std::string &path, ImportResult &out);   // Z up, like Blender writes it
bool import_usda_file(const std::string &path, ImportResult &out);  // text USD; the stage says its up axis
bool model_extension_supported(const std::string &ext);

/* Creates GameObjects for an import result under `parent` (may be null). */
GameObject *instantiate_import(Scene &scene, const ImportResult &r, const std::string &root_name, GameObject *parent);

}  // namespace bl

// SPDX-License-Identifier: GPL-2.0-or-later
// Model import (Unity: drop a model into Assets; Blender: File > Import).
//   .obj + .mtl  - own parser (blender/source/blender/io/wavefront_obj)
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
};

struct ImportResult {
  std::vector<ImportedNode> nodes;
  std::vector<MaterialPtr> materials;
  std::vector<std::string> textures;  // absolute paths referenced by materials
  std::string error;
  std::string summary;
};

bool import_model(const std::string &path, ImportResult &out);
bool import_obj_file(const std::string &path, ImportResult &out);
bool import_fbx_file(const std::string &path, ImportResult &out);
bool model_extension_supported(const std::string &ext);

/* Creates GameObjects for an import result under `parent` (may be null). */
GameObject *instantiate_import(Scene &scene, const ImportResult &r, const std::string &root_name, GameObject *parent);

}  // namespace bl

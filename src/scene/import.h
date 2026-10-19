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

#include <map>
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
  std::vector<std::pair<std::string, float>> shape_weights;  // the mesh's shape keys' weights as the file left them (task 0135)
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
  std::string source;               // the model file (its folder is where textures are looked for)
  std::vector<std::string> notes;   // what the import did or couldn't (textures found by name, maps split, ...)
  /* Work kept for the rest of the import (task 0134): the texture folders' files listed once, each channel split once,
   * the images written to the cache (two embedded "diffuse.png" of different content get two names). */
  bool listed = false;
  std::vector<std::string> listing;
  std::map<std::string, std::string> derived;
  std::vector<std::string> written;
};

bool import_model(const std::string &path, ImportResult &out);
bool import_obj_file(const std::string &path, ImportResult &out);
bool import_fbx_file(const std::string &path, ImportResult &out);
bool import_gltf_file(const std::string &path, ImportResult &out);  // import_formats.cpp
bool import_stl_file(const std::string &path, ImportResult &out);   // Z up, like Blender writes it
bool import_ply_file(const std::string &path, ImportResult &out);   // Z up, like Blender writes it
bool import_usda_file(const std::string &path, ImportResult &out);  // text USD; the stage says its up axis
bool model_extension_supported(const std::string &ext);

/* ---- Materials made on import (task 0134, import_materials.cpp) ---- */
/* A texture a file links: the link, beside the model, in its .fbm, textures or images folder, by name in any case. */
std::string find_texture(ImportResult &r, const std::string &file);
/* An image inside the model file (FBX content, glTF buffer) written to the import cache; its path ("" if none). */
std::string save_embedded(const std::string &name, const void *data, size_t size, ImportResult &r);
/* One channel of an image as a grey image of its own (inverted: a gloss map into roughness), in the import cache. */
std::string derive_channel(const std::string &src, int channel, bool invert, const std::string &tag, ImportResult &r);
std::string import_cache_dir(const ImportResult &r);
/* After any import: a material for every mesh without one, and texture sets beside the model matched by name
 * (Body_BaseColor, T_Body_N, ...) onto the materials the file left without textures. */
void complete_materials(ImportResult &r);

/* Creates GameObjects for an import result under `parent` (may be null). */
GameObject *instantiate_import(Scene &scene, const ImportResult &r, const std::string &root_name, GameObject *parent);

}  // namespace bl

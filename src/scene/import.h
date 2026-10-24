// SPDX-License-Identifier: GPL-2.0-or-later
// Model import (Unity: drop a model into Assets; Blender: File > Import).
//   .obj + .mtl  - own parser (blender/source/blender/io/wavefront_obj)
//   .glb .gltf .stl .ply .usda - import_formats.cpp
//   .fbx         - ufbx, the FBX loader Blender itself uses (blender/extern/ufbx,
//                  blender/source/blender/io/fbx)
//   .pmx .pmd    - MikuMikuDance models, own reader (import_mmd.cpp, task 0136)
//   .vrm         - VRM characters: glTF plus the VRM extension (import_vrm.cpp, task 0138)
//   .bvh         - motion capture: a skeleton and its clip (import_bvh.cpp, task 0140)
//   .dae         - Collada, own XML reader (import_dae.cpp, core/xml.h, task 0141)
// Everything is converted to Unity's left-handed Y-up metre space by
// mirroring X and reversing face winding, the same thing Unity's importer does.
#pragma once

#include "animation.h"
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
  float length = 0.0f;  // a bone's length when the file says it (MMD: to its tail); 0: to its first child
  bool folded = false;  // an end bone folded into its parent's tail (task 0144): no object is made for it
};

/* Rig parts beyond the bones (task 0136), on nodes by index. IK on a chain's end bone (its tip reaches the target);
 * a spring chain's first bone; an object's toon outline. */
struct ImportedIK {
  int node = -1;
  std::string target, pole;  // node names
  int chain_length = 2, iterations = 20;
};
struct ImportedSpring {
  int node = -1;
  int label = 0;  // spring::kLabels
  float radius = 0.02f;
  float stiffness = -1.0f, damping = -1.0f, gravity = -1.0f;  // (below 0: the label's defaults)
};
struct ImportedOutline {
  int node = -1;
  float thickness = 0.005f;
  Vec3 color;
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
  std::vector<ImportedIK> iks;
  std::vector<ImportedSpring> springs;
  std::vector<ImportedOutline> outlines;
  std::vector<AnimationClip> clips;  // the file's animations, paths relative to the import's root (task 0139)
  std::string error;
  std::string summary;
  std::string source;               // the model file (its folder is where textures are looked for)
  std::vector<std::string> notes;   // what the import did or couldn't (textures found by name, maps split, ...)
  /* Work kept for the rest of the import (task 0134): the texture folders' files listed once, each channel split once,
   * the images written to the cache (two embedded "diffuse.png" of different content get two names). */
  bool listed = false;
  std::vector<std::string> listing;
  std::vector<int> listing_distance;  // each image's distance from the model's folder (task 0143)
  std::vector<char> listing_beside;   // ...and whether it lies in a folder beside the model's, not its own tree
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
bool import_pmx_file(const std::string &path, ImportResult &out);   // .pmx and .pmd (import_mmd.cpp)
/* The file's own vertex normals (task 0149): kept as the mesh's custom normals when they differ from the recomputed
 * ones (an artist's edited normals: an anime face's); a plain export that stored the smooth normals carries no copy.
 * True when kept. (import_formats.cpp) */
bool keep_file_normals(Mesh &m, const std::vector<Vec3> &file_normals);
bool import_bvh_file(const std::string &path, ImportResult &out);   // the joints as bones, the motion as a clip (import_bvh.cpp)
bool import_dae_file(const std::string &path, ImportResult &out);   // Collada: geometry, materials, skins, nodes, animations (import_dae.cpp)
/* VRM (task 0138, import_vrm.cpp): what the extension adds to a glTF. Called by the glTF reader with its node map
 * (glTF node index -> import node), before the skins (the humanoid's bones named for the rig tools) and after them
 * (expressions as shape keys, spring chains, MToon as Toon with outlines, a 0.x model turned to face +Z). */
struct Json;
bool vrm_present(const Json &doc);
void vrm_name_bones(const Json &doc, const std::vector<int> &node_index, ImportResult &out);
void vrm_complete(const Json &doc, const std::vector<int> &node_index, const std::vector<MeshPtr> &meshes, const std::vector<MaterialPtr> &materials,
                  const std::vector<std::string> &image_paths, ImportResult &out);  // image_paths: the glTF's images as files (task 0148: MToon's shade texture)
/* MMD's standard bone names in the words Blendity's rig tools read ("左ひざ" -> "LeftLeg"); any other name as it is.
 * Motions (.vmd) name bones in Japanese: the same mapping finds them. */
std::string mmd_bone_name(const std::string &japanese);
int mmd_spring_label(const std::string &name);  // hair, ear, tail, cloth or soft tissue from a physics bone's name
/* Welds vertices at identical positions with the same weights and shape keys (STL, glTF and MMD split them), and the
 * same normal when `normals` (one a vertex) is given: a hard edge stays split. */
void weld_exact(Mesh &m, const std::vector<Vec3> *normals = nullptr);
bool model_extension_supported(const std::string &ext);

/* ---- Motions onto a character (task 0137) ---- */
bool motion_extension_supported(const std::string &ext);  // .vmd
/* A MikuMikuDance motion's keys for the character under `root` (bones found by MMD's names, as mmd_bone_name maps
 * them), into `clip`: bone turns and moves, morphs as shape-key weights, the legs' IK switched off and on. `notes`:
 * what it couldn't place. */
bool import_vmd_file(const std::string &path, GameObject &root, AnimationClip &clip, std::vector<std::string> &notes, std::string &error);

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

/* End bones folded into their parents (task 0144): a weightless leaf named "<bone>_end" / "_End" / "_END" / ".end" /
 * " End" / " end" (Blender's FBX leaf bones, Mixamo's HeadTop_End, BVH's end sites, VRoid's spring tails): the
 * parent's length reaches it and no object is made for it. The count. */
size_t fold_end_bones(ImportResult &r);
/* Creates GameObjects for an import result under `parent` (may be null). `made`: the object of each node (null for a
 * folded end bone). Bones get their lengths: to their first child, else in metres (a tenth of a metre, or half the
 * parent's) whatever the armature's scale. */
GameObject *instantiate_import(Scene &scene, const ImportResult &r, const std::string &root_name, GameObject *parent, std::vector<GameObject *> *made = nullptr);
/* A node's path below the import's root, as anim_path names it (a single node is the root itself: ""). The clips'
 * tracks are made with these; the editor rewrites them onto the objects made (an armature renamed for uniqueness). */
std::string import_node_path(const ImportResult &r, int node);

}  // namespace bl

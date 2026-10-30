// SPDX-License-Identifier: GPL-2.0-or-later
// Model export in the formats Blender 5.2 itself exports meshes to
// (File > Export):
//   Wavefront OBJ + MTL   blender/source/blender/io/wavefront_obj/exporter
//   FBX (ASCII 7.4)       scripts/addons_core/io_scene_fbx/export_fbx_bin.py
//   glTF 2.0 .glb/.gltf   scripts/addons_core/io_scene_gltf2
//   STL (binary / ASCII)  blender/source/blender/io/stl/exporter
//   PLY (binary / ASCII)  blender/source/blender/io/ply/exporter
//   USD (.usda, text)     blender/source/blender/io/usd/intern/usd_writer_mesh.cc
// (Blender's other exporters, Alembic and grease-pencil SVG / PDF, are for
// animation caches and 2D strokes.) Our space is Unity's left-handed Y-up, so
// X is mirrored and polygon winding reversed - the conversion Unity applies
// when importing. Z-up files (Blender's own STL / PLY / USD default) are
// additionally turned so Y becomes Z, exactly like Blender's axis conversion
// (forward -Z, up Y  ->  forward Y, up Z).
#pragma once

#include "material.h"
#include "mesh.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace bl {

struct Reflector;

/* Task 0268: a skinned mesh's skeleton, weights and clip, for glTF (the other formats write the mesh as posed). Joints
 * are listed parents first, each local to its parent joint, the first ones to `base`; Blendity's space (the writer
 * mirrors and scales them as it does the vertices). */
struct ExportSkin {
  struct Joint {
    std::string name;
    int parent = -1;
    Vec3 t;
    Quat r;
    Vec3 s{1, 1, 1};
  };
  std::string base_name = "Skeleton";
  Mat4 base;  // the skeleton's parent's world matrix
  std::vector<Joint> joints;
  std::vector<std::array<uint16_t, 4>> vjoints;  // per mesh vertex: its four strongest joints
  std::vector<std::array<float, 4>> vweights;     // ...their weights, summing to 1 (all 0: no joint)
  struct Anim {
    std::string name;
    float fps = 30.0f;
    int frames = 0;
    std::vector<std::vector<Vec3>> t, s;  // [joint][frame], local
    std::vector<std::vector<Quat>> r;
  };
  std::vector<Anim> anims;
};

struct ExportItem {
  std::string name;
  const Mesh *mesh = nullptr;
  Mat4 world;                         // baked into the vertices
  std::vector<MaterialPtr> materials; // slot i -> material
  std::shared_ptr<const ExportSkin> skin;  // (task 0268: glTF writes the skeleton, the weights and the clip; the mesh at rest)
};

enum class ExportFormat { OBJ = 0, FBX, GLB, GLTF, STL, PLY, USD, Count };

/* Blender's export options (the side panel of its File > Export dialogs). */
struct ExportOptions {
  int format = 0;               // ExportFormat
  bool selection_only = true;   // Blender: Limit to > Selected Only
  bool apply_modifiers = true;  // Blender: Apply Modifiers
  float scale = 1.0f;           // Blender: Scale
  int up_axis = 0;              // 0 Y up, 1 Z up (OBJ, STL, PLY, USD; glTF and FBX are always Y up)
  bool triangulate = false;     // Blender: Triangulated Mesh / Triangulate Faces
  bool normals = true;          // Blender: Normals
  bool uvs = true;              // Blender: UV Coordinates
  bool materials = true;        // Blender: Materials (glTF: with their base colour textures)
  bool skins = true;            // glTF: skinned meshes with their armature, weights and clip (task 0268; Blender: Armature, Skinning, Animation)
  bool ascii = false;           // STL / PLY: ASCII instead of binary
  /* Bake Textures (task 0036, OBJ / FBX / glTF): each object's look baked on a copy into one set of images beside
   * the file (albedo, roughness, metallic, normal) on new UVs; the copies' materials name them. */
  bool bake_textures = false;
  bool embed_textures = false;  // FBX (binary): the images inside the file (Blender: Path Mode Copy + Embed Textures)
  int bake_size = 1024;         // texels per side
  std::string texture_dir;      // (not reflected) the exported file's folder: textures in it are written by name
  /* (not reflected; task 0172, Live Link) glTF: each vertex's index in the mesh as an attribute _VERT (a float),
   * so a client can move the file's split vertices from the mesh's positions alone. */
  bool vertex_ids = false;
  void reflect(Reflector &r);
};

const char *export_format_name(int format);       // "Wavefront (.obj)"
const char *export_format_extension(int format);  // ".obj"
int export_format_from_extension(const std::string &ext);  // -1 if unknown
/* Defaults per format, as Blender's dialogs have them (STL / PLY / USD are Z up). */
ExportOptions export_defaults(int format);

/* Writes `path` (plus a .mtl, .bin or textures next to it when the format has them). */
bool export_file(const std::string &path, const std::vector<ExportItem> &items, const ExportOptions &o, std::string *error = nullptr);

/* In-memory writers (tests use these directly). */
void export_obj_mtl(const std::vector<ExportItem> &items, const std::string &mtl_file, std::string &obj, std::string &mtl,
                    const ExportOptions &o = ExportOptions());
std::string export_fbx(const std::vector<ExportItem> &items, const ExportOptions &o = ExportOptions());
std::string export_stl(const std::vector<ExportItem> &items, const ExportOptions &o);
std::string export_ply(const std::vector<ExportItem> &items, const ExportOptions &o);
std::string export_usda(const std::vector<ExportItem> &items, const ExportOptions &o);
/* glTF: binary .glb, or the .gltf JSON with its buffer in bin_out (written as bin_name). */
std::string export_gltf(const std::vector<ExportItem> &items, const ExportOptions &o, bool binary, const std::string &bin_name,
                        std::string *bin_out, std::vector<std::pair<std::string, std::string>> *copy_textures = nullptr);
/* A material as glTF writes it (one object on one line, no textures: Live Link sends their paths beside it), with
 * the BLENDITY_toon extension on a Toon material (every field by its Inspector name). Each primitive glTF writes
 * names its material slot in extras.slot (an empty slot has no primitive). */
std::string gltf_material_json(const Material &m);

}  // namespace bl

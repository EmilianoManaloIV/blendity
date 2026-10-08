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

#include <string>
#include <vector>

namespace bl {

struct Reflector;

struct ExportItem {
  std::string name;
  const Mesh *mesh = nullptr;
  Mat4 world;                         // baked into the vertices
  std::vector<MaterialPtr> materials; // slot i -> material
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
  bool ascii = false;           // STL / PLY: ASCII instead of binary
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

}  // namespace bl

// SPDX-License-Identifier: GPL-2.0-or-later
// Model export: Wavefront OBJ + MTL and ASCII FBX 7.4 (Blender:
// io/wavefront_obj/exporter, io_scene_fbx/export_fbx_bin.py; Unity: the FBX
// Exporter package). Both formats are right-handed and Y-up, so X is mirrored
// and polygon winding reversed - the conversion Unity applies when importing.
#pragma once

#include "material.h"
#include "mesh.h"

#include <string>
#include <vector>

namespace bl {

struct ExportItem {
  std::string name;
  const Mesh *mesh = nullptr;
  Mat4 world;                         // baked into the vertices
  std::vector<MaterialPtr> materials; // slot i -> material
};

/* OBJ text and its MTL (mtl_file is the name the OBJ's mtllib line refers to). */
void export_obj_mtl(const std::vector<ExportItem> &items, const std::string &mtl_file, std::string &obj, std::string &mtl);
/* ASCII FBX 7.4: geometry, per-corner normals and UVs, per-face materials, one Model per item. */
std::string export_fbx(const std::vector<ExportItem> &items);

}  // namespace bl

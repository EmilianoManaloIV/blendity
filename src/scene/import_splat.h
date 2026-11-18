// SPDX-License-Identifier: GPL-2.0-or-later
// Gaussian splat and point cloud import (round 531, task 0511, ADR 0147): PLY (a 3DGS file auto-detected, a plain
// point file as Points), SPZ v1-v4, and glTF's KHR_gaussian_splatting attributes. Each reader returns a cloud in
// Blendity's axes, run through pointcloud_validate, or null with a reason. Ported, not copied, from Blender 5.3's
// io/ply/importer/ply_import_gsplat.cc, io/spz/ and the glTF add-on's gsplat import (see import_splat.cpp).
#pragma once

#include "pointcloud.h"

#include <map>
#include <string>
#include <vector>

namespace bl {

struct ImportResult;

/* Where a read's time went (round 567: the `splats` stress section's rows; a few clock reads a file). */
struct SplatReadTimes {
  double unpack_ms = 0.0;    // SPZ: the gzip / zstd streams inflated
  double setup_ms = 0.0;     // the header read, a text file's lines found, the cloud's arrays allocated
  double decode_ms = 0.0;    // the file's bytes to the cloud's arrays
  double axes_ms = 0.0;      // pointcloud_change_axes
  double validate_ms = 0.0;  // pointcloud_validate
};
/* What a reader leaves besides the cloud: why it refused (`error`), and what it dropped or guessed (`notes`). */
struct SplatReadReport {
  std::string error;
  std::vector<std::string> notes;
  SplatReadTimes times;
};

/* True when `data` is a PLY whose geometry is points only: a vertex element with x, y, z, and no face or edge
 * element with records (Blender's is_data_gaussian_splat, widened to plain points). The mesh importer hands such a
 * file here instead of refusing it. */
bool ply_is_point_cloud(const std::string &data);
/* A face-less PLY as a cloud: Gaussian Splats when the vertices carry f_dc_0..2, opacity, scale_0..2 and rot_0..3
 * (Blender's test), else Points (colours from red / green / blue, or diffuse_*). PLY is Z up, as Blender writes it. */
PointCloudPtr read_ply_point_cloud(const std::string &data, SplatReadReport &report);

/* An SPZ file: v1-v3 one gzip stream (unity_package's gunzip over image.cpp's inflate), v4 (NGSP) zstd streams, read
 * only with Blender's libraries (BL_WITH_ZSTD); without them a v4 file is refused with a message. SPZ is Y up,
 * right-handed (RUB). */
PointCloudPtr read_spz(const std::string &bytes, SplatReadReport &report);

/* glTF's KHR_gaussian_splatting (or a plain POINTS primitive): the attributes as the glTF reader decoded them,
 * floats per element, by name ("POSITION", "COLOR_0", "KHR_gaussian_splatting:SCALE", ...), already in the file's
 * own right-handed Y-up space. `components` holds each one's floats per element (3 for VEC3). */
PointCloudPtr gltf_point_cloud(const std::map<std::string, std::vector<float>> &attrs, const std::map<std::string, int> &components, bool splats,
                               SplatReadReport &report);

/* An orthogonal change of axes (det +1 or -1) applied to the whole cloud: positions, the splats' turns (a mirror
 * kept a proper rotation: the ellipsoid is symmetric) and their SH coefficients (each band by the exact linear map
 * of the axes' change). `m` row-major: new = m * old. */
void pointcloud_change_axes(PointCloud &pc, const float m[3][3]);
/* The SH coefficients' map for one band (1..3) under `m`: out[(2l+1)^2], c_new = out * c_old. */
void splat_sh_band_matrix(int band, const float m[3][3], double *out);

/* The importer's entry points: the cloud as the import's one node (ImportedNode::points), named for the file. */
bool import_spz_file(const std::string &path, ImportResult &out);
/* A PLY whose geometry is points only (ply_is_point_cloud), its bytes already read. */
bool import_ply_point_cloud(const std::string &path, const std::string &data, ImportResult &out);

}  // namespace bl

// SPDX-License-Identifier: GPL-2.0-or-later
// VRC Light Volumes baked in Blendity (round 398, task 0375): a Light Volume's voxel grid baked as probes by the path
// tracer (probe_bake), each probe's ambient cube fitted to the SH L1 the package stores (L0 + L1 . n: Unity's
// SphericalHarmonicsL2 in its shader-constant form, irradiance / pi - what Blendity's probes hold), checked, saved
// beside the scene's lightmaps, and sent to Unity in the package's own order (Unity's axes: X mirrored). Unity's
// bake read back from the package's Texture3D assets the same way.
#pragma once

#include "../core/math.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace bl {

struct Environment;
struct ProbeVolumeData;

/* One volume's bake: per voxel (x fastest, then y, then z) L0 (RGB) and L1 per colour (its x, y, z), and its validity
 * in Unity's sense - the share of back faces it sees: 0 open, 1 inside geometry (review 398: the package takes a voxel
 * under its Dilation Backface Bias as valid and fills the others from them). */
struct LightVolumeSH {
  static constexpr size_t kMaxVoxels = 2000000;  // (review 398: the scene's probe volume's cap - a world-sized grid refused, said)
  int res[3] = {0, 0, 0};
  std::vector<Vec3> l0, l1r, l1g, l1b;
  std::vector<float> validity;
  bool unity_axes = false;  // (true: in Unity's axes and order - mirrored in X - as sent)
  bool from_unity = false;  // (review 398: Unity's bake read back - shown, never sent back as Blendity's)
  Quat rotation;            // (the volume's world rotation when baked: its L1 is in world axes - the package's InvBakedRotation)
  size_t count() const { return (size_t)res[0] * (size_t)res[1] * (size_t)res[2]; }
  /* Sizes agree with res, every value finite: the checks before anything is written or sent. */
  bool valid(std::string &why) const;
  /* CRC-32 of the floats as sent (l0, l1r, l1g, l1b, validity in that order, little-endian). */
  uint32_t checksum() const;
  /* The floats as sent: count x 12 then count x 1. */
  std::string bytes() const;
  static bool from_bytes(const std::string &b, const int res[3], LightVolumeSH &out, std::string &why);
};

/* The voxel centres of a volume in world space: its object's unit cube, (i + 0.5) / res - 0.5 on each axis. */
std::vector<Vec3> light_volume_positions(const Mat4 &world, const int res[3]);

/* SH L1 from each probe's ambient cube (the sky's light added from `env` along each axis). Each cube direction is the
 * irradiance there, which has no odd band past 1: L1 along an axis is exactly half its positive less its negative, L0
 * the six directions' mean (exact to band 2) - review 398: 3/8, the fit of the cube's own n^2 blend, was 25% weak. */
void light_volume_fit(const ProbeVolumeData &d, const Environment &env, const int res[3], LightVolumeSH &out);

/* Blendity's axes to Unity's and back (X mirrored: voxel columns reversed in x, L1's x negated). */
LightVolumeSH light_volume_to_unity(const LightVolumeSH &sh);
LightVolumeSH light_volume_from_unity(const LightVolumeSH &sh);

/* A scene's bakes, by object id: one file beside its lightmaps ("BLVS", version 2, then per volume its id, grid,
 * whether it is Unity's, its baked rotation, checksum and floats). A volume whose checksum doesn't match its floats is
 * refused on load, and so is version 1 (its validity and L1 were the review's bugs: bake again). */
bool save_light_volume_bakes(const std::string &path, const std::map<uint64_t, LightVolumeSH> &bakes, std::string &error);
bool load_light_volume_bakes(const std::string &path, std::map<uint64_t, LightVolumeSH> &bakes, std::string &error);

/* Unity's bake read back: the package's three Texture3D assets (Unity's YAML: m_Width, m_Height, m_Depth, m_Format
 * RGBAHalf - TextureFormat 17 or GraphicsFormat 48 - image data as hex) unpacked to SH in Unity's axes (its L1 stored
 * times 1.65). False when they aren't three RGBAHalf textures of one size. */
bool light_volume_from_textures(const std::string &tex0, const std::string &tex1, const std::string &tex2, LightVolumeSH &out, std::string &why);

}  // namespace bl

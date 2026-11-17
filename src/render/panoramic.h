// SPDX-FileCopyrightText: 2021-2022 Blender Authors
// SPDX-FileCopyrightText: 2026 Emiliano Manalo and contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Panoramic camera projections (round 538, task 0518). Ported, not copied, from Blender's EEVEE
// (source/blender/draw/engines/eevee/shaders/eevee_camera_lib.bsl.hh, eevee_camera.cc: the cube faces a projection needs)
// which adapted them from Cycles' intern/cycles/kernel/camera/projection.h.
//
// The maths below is written in Blender's camera space (-Z forward, Y up, X right; the image's uv has v up, as in
// Blender) and converted once, in blender_to_camera(), to Blendity's (+Z forward): the public functions take and give
// Blendity camera-space directions and Blendity's image uv (u right, v DOWN, the row order of an Image).
#pragma once

#include "../core/math.h"
#include "raster.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace bl {

enum PanoramicType : int {
  kPanoOff = 0,
  kPanoEquirectangular = 1,
  kPanoFisheyeEquidistant = 2,
  kPanoFisheyeEquisolid = 3,
  kPanoCentralCylindrical = 4,
  kPanoMirrorBall = 5,
  kPanoTypeCount = 6,
};

/* A camera's panoramic settings in radians and millimetres, with Blender's defaults. */
struct PanoramicParams {
  int type = kPanoOff;
  float lat_min = -1.57079632679f, lat_max = 1.57079632679f;  // Equirectangular
  float lon_min = -3.14159265359f, lon_max = 3.14159265359f;  // Equirectangular and Central Cylindrical
  float fisheye_fov = 3.14159265359f;                         // the fisheyes' field of view
  float fisheye_lens = 10.5f;                                 // Equisolid: focal length, mm
  float sensor_w = 36.0f, sensor_h = 24.0f;                   // Equisolid: the sensor as the output's shape fits it, mm
  float cyl_v_min = -1.0f, cyl_v_max = 1.0f;                  // Central Cylindrical: the height range
  /* Bad numbers (NaN, a minimum above its maximum, a field of view of 0 or 400 degrees) put back into a range the
   * projections can use: swapped, clamped, or the default when nothing sensible is left. */
  void sanitize();
  bool operator==(const PanoramicParams &o) const;
};

/* The names the Inspector and the console use, in type order (kPanoTypeCount - 1 entries, from type 1). */
const char *panoramic_type_name(int type);
int panoramic_type_from_name(const std::string &name);  // 0 for "off" / unknown, a type for a name or a unique prefix

/* The direction image point (u, v) looks along, in camera space, unit length; (0, 0, 0) where the projection shows
 * nothing ("outside": a fisheye's corners). u, v in [0, 1], v down. */
Vec3 panoramic_direction(const PanoramicParams &p, float u, float v);

/* Where a camera-space direction lands in the image; false when it lands outside the picture (or the projection can't
 * show it). u, v as above. Used for the round trip and for the lookup table. */
bool panoramic_uv(const PanoramicParams &p, Vec3 dir, float &u, float &v);

/* The cube faces a projection needs, as Blender's panoramic_view_mask_get says: bit i is face i of panoramic_face(). */
uint32_t panoramic_face_mask(const PanoramicParams &p);

/* A cube face's look direction and up, in camera space (Blendity: +Z is the camera's forward). Faces: 0 +X, 1 -X, 2 +Y,
 * 3 -Y, 4 +Z (front), 5 -Z (back). */
void panoramic_face(int face, Vec3 &forward, Vec3 &up);

/* Each face is drawn 90 degrees widened by this much (Blender's 5% overscan) so a bilinear lookup at its edge stays inside. */
constexpr float kPanoramicOverscan = 1.05f;

/* Each output pixel's cube face and place on it (face pixels, centres at +0.5), built once per settings and output size. */
struct PanoramicMap {
  uint64_t key = 0;
  int width = 0, height = 0, side = 0;
  uint32_t mask = 0;
  std::vector<uint32_t> pixels[6];  // per face: the output pixels it fills (y * width + x)
  std::vector<float> fx, fy;        // per output pixel: its place on its face, face pixels
};
uint64_t panoramic_map_key(const PanoramicParams &p, int w, int h, int side);
void panoramic_build_map(const PanoramicParams &p, int w, int h, int side, PanoramicMap &out);

/* The face target's side for an output: its larger side / 2, in [64, 2048]. */
int panoramic_face_side(int w, int h);

/* The face picture's colours into the output pixels that look at that face (bilinear). */
void panoramic_resample_face(const PanoramicMap &map, int face, const uint32_t *face_pixels, uint32_t *out, int out_stride);

/* What a view keeps between frames: the one square face picture and its target (reused, so the GPU path has one slot)
 * and the few lookup tables recently used (several views of different sizes may show a panoramic camera). */
struct PanoramicState {
  Image img;
  RenderTarget rt;
  std::vector<PanoramicMap> maps;  // newest last, at most 4
  const PanoramicMap &map_for(const PanoramicParams &p, int w, int h, int side);
};

/* Cube faces drawn through `draw` (face, view matrix, projection; the face target is st.rt) and each resampled into
 * `out` as it is done; what no face covers is opaque black. `cam`: the camera's axes in the world. */
struct PanoramicCameraFrame {
  Vec3 eye, right, up, forward;
  float near_clip = 0.3f, far_clip = 1000.0f;
};
using PanoramicFaceDraw = std::function<void(int face, const Mat4 &view, const Mat4 &proj, RenderTarget &face_rt)>;
void panoramic_render(const PanoramicParams &p, int w, int h, const PanoramicCameraFrame &cam, PanoramicState &st, uint32_t *out, int out_stride,
                      const PanoramicFaceDraw &draw);

/* A camera ray for the path tracer: the world direction through the pixel at (px, py) (fractions of the image, y down),
 * or false where the projection shows nothing. */
bool panoramic_ray(const PanoramicParams &p, float px, float py, Vec3 right, Vec3 up, Vec3 forward, Vec3 &dir);

}  // namespace bl

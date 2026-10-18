// SPDX-License-Identifier: GPL-2.0-or-later
// The scene in MoonRay's scene language, RDLA (Lua), from what F12's path tracer is given (task 0113, ADR 0041):
// the draw items (meshes with modifiers applied, their materials), the lights, the world and the camera.
//
//   Blendity (Unity, left-handed, Y up)   MoonRay (right-handed, Y up)
//   ----------------------------------    ---------------------------------------------------------------
//   a mesh, its local transform           RdlMeshGeometry (z mirrored, winding reversed), node_xform S.M.S
//   Material (Principled-like)            DwaBaseMaterial; image maps through ImageMap / ImageNormalMap
//   sun / point / spot / area light       DistantLight / SphereLight / SpotLight / RectLight or DiskLight
//   the world (gradient, sky, HDRI)       EnvLight with an equirectangular map baked to a .pfm
//   camera                                PerspectiveCamera (focal and film from the field of view, DOF)
//   samples, bounces, size                SceneVariables; RenderOutputs beauty + alpha, uncompressed float EXR
#pragma once
#include "../core/math.h"
#include "../scene/material.h"
#include "pathtracer.h"
#include <functional>
#include <string>
#include <vector>

namespace bl {
struct RenderMesh;
struct Environment;

namespace moonray {

struct ExportObject {
  const RenderMesh *mesh = nullptr;
  Mat4 model;
  const std::vector<MaterialPtr> *materials = nullptr;
};

struct ExportScene {
  std::vector<ExportObject> objects;
  std::vector<RenderLight> lights;
  const Environment *world = nullptr;
  Mat4 view, proj;  // the camera, as F12 renders it (left-handed view, the projection gives the field of view)
  PTLens lens;      // depth of field
  int width = 640, height = 360;
  int samples = 64;      // per pixel (MoonRay's pixel_samples is their square root)
  int max_bounces = 4;
  float point_radius = 0.1f;   // the path tracer's (PTSettings): sphere lights this size
  float sun_angle_deg = 0.526f;
  bool hide_background = false;  // a solid colour or Transparent film: the world isn't seen by the camera (alpha out)
  int world_map_width = 1024;    // the baked world's size (height half of it)
};

/* What was written, and what MoonRay doesn't take as Blendity draws it (shown once, not silently dropped). */
struct ExportReport {
  std::string scene_path, image_path;  // ours (scene.rdla, frame.exr)
  size_t meshes = 0, triangles = 0, lights = 0, materials = 0, textures = 0;
  std::vector<std::string> notes;
};

/* A file of ours as MoonRay sees it (moonray::path_for: WSL's /mnt/c/... on Windows). */
using PathMap = std::function<std::string(const std::string &)>;

/* Writes scene.rdla (and world.pfm) into `dir`; the image will be dir/frame.exr. */
bool export_scene(const ExportScene &sc, const std::string &dir, const PathMap &path, ExportReport &report, std::string &error);

/* The pieces, for the tests. */
Mat4 to_right_handed(const Mat4 &m);  // S.M.S, S = diag(1, 1, -1)
/* A direction of MoonRay's world (right-handed) for a point of its equirectangular environment map (u, v in 0..1,
 * v = 0 the top row) - the convention the baked world map is written in. */
Vec3 env_map_direction(float u, float v);
/* Index of refraction from Principled's Specular (IOR Level: 0.5 -> 1.5). */
float ior_from_specular(float specular);
/* Lua's own text for a number (finite; %.9g). */
std::string lua_number(float v);

}  // namespace moonray
}  // namespace bl

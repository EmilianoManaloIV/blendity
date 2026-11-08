// SPDX-License-Identifier: GPL-2.0-or-later
// Unity's baked lighting in Blendity (round 400, task 0377, ADR 0098): a Unity scene's lightmaps, which renderer takes
// which rect of which page through which UVs, and its baked light probes - as Unity itself reports them (Blendity can't
// read LightingData.asset, Unity's own binary): an editor script run by the installed Unity (editor/unity_lighting_ui)
// writes `<scene>.unity-lighting` beside the scene in Blendity's project, and this turns it into Blendity's own baked
// lighting: lightmap pages, each object's triangle UVs into them, the lights' modes, and a probe volume.
//
// The file (little-endian; a string is an int32 byte count and UTF-8; FNV-1a 32 of everything before it last):
//   "BLULIGHT", int32 version (1), int32 encoding (-1: not known), int32 lightmaps mode,
//   string LightingData (Unity path), uint32 its bytes' FNV-1a 32 (which bake of Unity's this was read from)
//   int32 pages; each: string colour (Unity path), string direction (Unity path or ""), int32 width, int32 height,
//                      half rgb[width x height], bottom row first (the image as Unity reads it, re-imported plain: Unity
//                      writes its lightmaps PIZ-compressed, which Blendity without Blender's libraries can't read),
//                      int32 direction width, int32 direction height, uint8 rgba[...] (0 x 0: not directional)
//   int32 meshes; each: string key, int32 n, float3 positions[n], int32 which UV (1: uv2, 0: uv), float2 uv[n],
//                       int32 indices, int32 index[]
//   int32 renderers; each: string path ("Root[0]/Child[2]": names with their place among same-named siblings),
//                          int64 targetObjectId, int64 targetPrefabId, int32 mesh, int32 lightmap,
//                          float4 scale-offset (x, y scale; z, w offset), float16 local-to-world (column-major)
//   int32 lights; each: string path, int32 type (Unity's LightType), int32 bake (Unity's LightmapBakeType)
//   int32 probes; each: float3 position, float3 x 6: the probe as Unity's shading reads it along +X -X +Y -Y +Z -Z
//   (version 2: the reflection probes; version 3, last: int32 probes again (0 or the same count), each float3 x 26 - the
//   probe along probe_fit_directions' 26 directions, fitted to L2 SH on reading)
// Positions and directions are Unity's, which an imported Unity scene's are in Blendity too (round 395: its objects where
// Unity has them); a UV's v runs up (Blendity's lightmaps run down).
#pragma once

#include "lightmapper.h"
#include "probe_volume.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace bl {

class Scene;
class GameObject;
struct Bitmap;

struct UnityLighting {
  struct Page {
    std::string color, dir_path;  // (Unity paths: Assets/...)
    int width = 0, height = 0;
    std::vector<uint16_t> rgb;  // (half floats, 3 a texel, bottom row first)
    int dir_width = 0, dir_height = 0;
    std::vector<uint8_t> dir;  // (Unity's directional lightmap: rgba a texel, bottom row first)
  };
  struct MeshData {
    std::string key;
    std::vector<Vec3> positions;
    std::vector<Vec2> uv;
    bool uv2 = true;  // (false: the mesh's first UVs - Unity lightmaps a mesh without a second set by them)
    std::vector<uint32_t> indices;
  };
  struct Renderer {
    std::string path;
    int64_t object_id = 0, prefab_id = 0;
    int mesh = -1, lightmap = -1;
    float scale_offset[4] = {1, 1, 0, 0};
    Mat4 world = Mat4::identity();
  };
  struct LightInfo {
    std::string path;
    int type = 0, bake = 4;  // (Unity: Spot 0, Directional 1, Point 2, Rectangle 3; Mixed 1, Baked 2, Realtime 4)
  };
  struct Probe {
    Vec3 position;
    std::array<Vec3, 6> axes{};
    /* Round 442 (version 3): Unity's shading along probe_fit_directions' 26 directions (empty from older files). */
    std::vector<Vec3> seen;
  };
  /* Round 401 (version 2): a ReflectionProbe's baked cubemap - six faces +X -X +Y -Y +Z -Z, size x size half RGB each,
   * top row first as Cubemap.GetPixels gives them (checked against Unity: review 401); size 0 when it has no bake. */
  struct ReflectionProbe {
    std::string path;
    Vec3 position, box_size, box_offset;
    bool projection = false;
    float intensity = 1.0f;
    int importance = 1, size = 0;
    std::vector<uint16_t> faces[6];
  };
  int lightmaps_mode = 0;
  std::string lighting_data;          // (the LightingData.asset it was read from: Unity path)
  uint32_t lighting_data_hash = 0;    // (its bytes', FNV-1a 32: a bake of Unity's since makes the file stale)
  std::vector<Page> pages;
  std::vector<MeshData> meshes;
  std::vector<Renderer> renderers;
  std::vector<LightInfo> lights;
  std::vector<Probe> probes;
  std::vector<ReflectionProbe> reflection_probes;  // (round 401)
};

bool read_unity_lighting(const std::string &bytes, UnityLighting &out, std::string &why);
std::string write_unity_lighting(const UnityLighting &u);  // (the same format: tests, and nothing else writes it)

/* A half float's value (round 401: the reflection probes' faces are read where they are applied). */
float unity_half_to_float(uint16_t h);

/* FNV-1a 32 of a file's bytes (the file's lighting_data_hash for the LightingData.asset it names). */
uint32_t unity_lighting_hash(const std::string &bytes);

/* An object's path as the file names it, from `under` (nullptr: the scene's top): "Root[0]/Child[2]". */
std::string unity_lighting_path(const GameObject &g, const GameObject *under);

/* Which of Blendity's objects each renderer is, and its triangles' lightmap UVs: each of the object's render triangles
 * matched to Unity's by its corners in world space (within a millimetre), the corner's UV taken
 * with the renderer's scale and offset and turned to Blendity's v. A triangle with no match takes its nearest
 * Unity triangle's; an object with under half matched is left out (not the same mesh). `out`: object id -> (page,
 * three UVs a render triangle in render order). */
struct UnityLightingMatch {
  int renderers = 0, matched = 0, not_found = 0, not_same = 0;
  size_t triangles = 0, triangles_matched = 0;
};
UnityLightingMatch unity_lightmap_uvs(Scene &scene, const GameObject *under, const UnityLighting &u,
                                      std::map<uint64_t, std::pair<int, std::vector<Vec2>>> &out);
/* Unity's world into Blendity's: an imported scene's objects stand under its root, wherever that was put (review 400). */
Mat4 unity_lighting_placement(const GameObject *under);

/* Unity's lightmap texel to Blendity's: both hold what albedo multiplies (Unity's shaders: albedo x DecodeLightmap; Blendity:
 * irradiance / pi) - checked against Unity's render of SideRail (spec 0377). */
constexpr float kUnityLightmapScale = 1.0f;

/* A page as Blendity's texels: linear, row 0 the image's top (Blendity's lightmap v runs down). */
bool unity_lightmap_page(const UnityLighting::Page &page, float scale, Lightmap &out);

/* Unity's Directional lightmaps (its default) shade by the surface's normal against the light's dominant direction:
 * colour x (dot(n, dir - 0.5) + 0.5) / max(1e-4, dir.w), per pixel, as UnityCG's DecodeDirectionalLightmap. A lightmap texel
 * is one place on one surface, so the factor goes into the page once: each object's render triangles drawn into it
 * through their UVs, each texel by its smooth normal there; the texels round them (the bilinear filter's) take their
 * neighbours'. Returns the texels changed. */
size_t unity_lightmap_directions(Scene &scene, const UnityLighting &u, LightingData &ld, const Mat4 &placement = Mat4::identity());

/* Unity's baked probes as Blendity's probe volume: probes placed over theirs (finer near them); round 442: each from the
 * tetrahedron of Unity's probes around it (their Light Probe Groups), outside them from its nearest
 * four of Unity's (by inverse squared distance). Empty when there are none. */
void unity_probe_volume(const UnityLighting &u, ProbeVolumeData &out, const Mat4 &placement = Mat4::identity());
/* Round 442: Unity's probes as L2 SH in Blendity's axes (empty when the file predates version 3). */
std::vector<ProbeSH> unity_probe_sh(const UnityLighting &u, const Mat4 &placement = Mat4::identity());

}  // namespace bl

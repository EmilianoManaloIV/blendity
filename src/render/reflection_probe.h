// Reflection probes (round 401, task 0378, ADR 0099): a probe's baked cubemap, its file beside the lightmaps, the
// equirectangular texture the renderers sample it through, and Unity's box projection and probe blending - so a
// Unity scene's metal and glass reflect the room Unity baked, not Blendity's sky.
#pragma once

#include "../core/math.h"
#include "../image/image.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace bl {

/* A Reflection Probe's baked cubemap: six faces +X -X +Y -Y +Z -Z, size x size, linear RGB, row 0 the top. The faces lie
 * as OpenGL's (and Unity's Cubemap's): +X right -Z up +Y, -X right +Z up +Y, +Y right +X up -Z, -Y right +X up +Z,
 * +Z right +X up +Y, -Z right -X up +Y. From Unity's bake (read through the lightmap fetch) or Blendity's (Generate
 * Lighting's last phase, the path tracer). */
struct ReflectionProbeBake {
  static constexpr int kMaxSize = 2048;
  int size = 0;
  std::vector<Vec3> faces[6];
  bool from_unity = false;
  bool valid(std::string &why) const;
  Vec3 sample(Vec3 dir) const;  // bilinear, on the face the direction leaves through
};

/* A face's pixel's direction (u across, v down, 0..1) and the inverse. */
Vec3 cube_direction(int face, float u, float v);
void cube_face_uv(Vec3 dir, int &face, float &u, float &v);

/* The faces as one equirectangular texture with mips (4 x size wide, 2 x size high, at most 1024 x 512): what the
 * renderers sample, the level per roughness as the environment map's. */
TexturePtr reflection_probe_texture(const ReflectionProbeBake &b);

/* The file beside the lightmaps (ReflectionProbes.bin): each probe object's id, its faces and a checksum. */
bool save_reflection_probe_bakes(const std::string &path, const std::map<uint64_t, ReflectionProbeBake> &bakes, std::string &error);
bool load_reflection_probe_bakes(const std::string &path, std::map<uint64_t, ReflectionProbeBake> &bakes, std::string &error);

/* A probe as the renderers see it: its box in world space (axis-aligned, as Unity's), its settings, its map. */
struct RenderProbe {
  Vec3 position, box_min, box_max;
  bool projection = false;
  float intensity = 1.0f, blend = 1.0f;
  int importance = 1;
  TexturePtr map;
};

/* Unity's BoxProjectedCubemapDirection: the reflection from `at` carried to the box's wall, seen from the probe. */
Vec3 box_projected_direction(const RenderProbe &p, Vec3 at, Vec3 R);
/* The probes lighting `at`, as Unity blends them: of the boxes holding the point, the most important (then the smallest)
 * first - its weight 1 but within Blend Distance of a wall - the second filling what it leaves, the environment the
 * rest. How many (0, 1 or 2), their indices and weights. */
int reflection_probe_weights(const std::vector<RenderProbe> &probes, Vec3 at, int idx[2], float w[2]);
float reflection_probe_weight(const RenderProbe &p, Vec3 at);  // (1 inside the box, 0 Blend Distance outside it)
/* One probe's reflection at `at` along R, blurred by the roughness. */
Vec3 reflection_probe_specular(const RenderProbe &p, Vec3 at, Vec3 R, float roughness);

}  // namespace bl

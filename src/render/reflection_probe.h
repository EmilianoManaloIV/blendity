// Reflection probes (round 401, task 0378, ADR 0099): a probe's baked cubemap, its file beside the lightmaps, the
// equirectangular texture the renderers sample it through, and Unity's box projection and probe blending - so a
// Unity scene's metal and glass reflect the room Unity baked, not Blendity's sky.
#pragma once

#include "../core/math.h"
#include "../image/image.h"

#include <array>
#include <atomic>
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
 * renderers sample. Round 428: its first kProbeLodSteps mips prefiltered as Unity's reflection probes are - each the
 * GGX lobe of the roughness that maps to it (reflection_probe_lod) - instead of box-filtered. */
TexturePtr reflection_probe_texture(const ReflectionProbeBake &b);

/* Unity's mip for a perceptual roughness (UnityStandardConfig's perceptualRoughnessToMipmapLevel: r (1.7 - 0.7 r) x
 * UNITY_SPECCUBE_LOD_STEPS, 6), capped at the texture's last level; and its inverse, the roughness a mip holds. */
constexpr int kProbeLodSteps = 6;
float reflection_probe_lod(float roughness, int levels);
float reflection_probe_level_roughness(float level);
/* The GGX-prefiltered radiance around `n` for `roughness` (N = V = R, as Unity's and Karis's split sum): `samples`
 * importance samples of the lobe weighted by N.L. With `mips` (the cube's own box chain) each sample reads the mip
 * its share of the lobe covers - filtered importance sampling (Krivanek and Colbert 2008), so a
 * lamp a few texels wide blurs into the lobe instead of being hit by a few samples (review 428); without, the cube's
 * texels (the tests' reference, with many samples). The texture's levels; exposed for the tests. Levels past
 * kProbeLodSteps stay box-filtered: no lookup reaches them. */
struct ProbeCubeMips;
Vec3 reflection_probe_prefilter(const ReflectionProbeBake &b, Vec3 n, float roughness, int samples, const ProbeCubeMips *mips = nullptr);

/* The cube's faces halved again and again (a box filter per face): the source the prefilter's samples read by lod - on
 * the cube, whose texels are nearly the same solid angle everywhere (review 428: box mips of the equirectangular map
 * over-weight the poles' tiny texels, a bright patch overhead gained a third of its light). */
struct ProbeCubeMips {
  std::vector<int> sizes;
  std::vector<std::array<std::vector<Vec3>, 6>> levels;
  explicit ProbeCubeMips(const ReflectionProbeBake &b);
  Vec3 sample(Vec3 dir, float lod) const;
  float texel_sr() const;  // a level-0 texel's mean solid angle
};

/* The file beside the lightmaps (ReflectionProbes.bin): each probe object's id, its faces and a checksum. */
bool save_reflection_probe_bakes(const std::string &path, const std::map<uint64_t, ReflectionProbeBake> &bakes, std::string &error);
bool load_reflection_probe_bakes(const std::string &path, std::map<uint64_t, ReflectionProbeBake> &bakes, std::string &error);

/* Round 451 (task 0427): a probe's face captured from the finished, lightmapped scene, as Unity bakes its reflection
 * probes (it renders the lit scene, it doesn't path-trace raw geometry): a static surface with a lightmap shows its
 * lightmap x albedo, plus the direct light of the lights the lightmaps don't hold (Realtime, Mixed, Shadowmask), plus its
 * emission; a surface without one (a moving object) and the sky are path-traced as before. `object_ids` is the GameObject
 * id of each object `pt` was built with, in its order; `lights` the lights to add directly. */
class PathTracer;
struct LightingData;
struct RenderLight;
struct ProbeLitCapture {
  const PathTracer *pt = nullptr;
  const LightingData *lighting = nullptr;
  const std::vector<uint64_t> *object_ids = nullptr;
  const std::vector<RenderLight> *lights = nullptr;
  int samples = 4;  // rays a pixel (jittered)
  const std::atomic<bool> *cancel = nullptr;  // (set: checked every row - the face's rest left black)
};
/* One face (0..5, as ReflectionProbeBake's) size x size, the near clip `near_clip` from `eye`; returns the share of the
 * face's rays that met a lightmapped surface. */
float reflection_probe_capture_lit(const ProbeLitCapture &c, Vec3 eye, int size, float near_clip, int face, std::vector<Vec3> &out);

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

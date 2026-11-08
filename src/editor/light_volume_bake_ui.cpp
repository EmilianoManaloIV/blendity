// SPDX-License-Identifier: GPL-2.0-or-later
// VRC Light Volumes baked in Blendity (round 398, task 0375): Generate Lighting's last phase. After its lightmaps and
// probes, each Light Volume with Bake in Blendity on bakes its voxel grid as probes - the same path tracer, the
// Mixed and Baked lights - in slices of the frame's budget (its validity checked in slices too), then fitted to the
// package's SH L1 (render/light_volume_bake). The results are kept by object id, saved beside the lightmaps with a
// checksum each, and Live Link sends them to Unity, where the bridge has the package save them as its own bake.
#include "editor.h"

#include "../render/light_volume_bake.h"
#include "../render/pathtracer.h"
#include "../render/probe_volume.h"
#include "../core/core.h"

namespace bl {

namespace {

constexpr const char *kBakesFile = "LightVolumes.bin";
constexpr size_t kSlice = 256;           // (probes baked a step on the CPU: a volume's grid is small beside a probe volume's)
constexpr size_t kValidateSlice = 4096;  // (probes checked for geometry a step)

/* Validity as the package wants it (review 398): a voxel that the probe check would move out of geometry, or gives up
 * on, is inside it - marked so (the package fills it from its neighbours) and left at its grid centre (moved, it had
 * taken light from behind a thin wall). */
void validate_slice(ProbeVolumeData &d, const PathTracer &pt, size_t from, size_t count) {
  ProbeVolumeData part;
  part.position.assign(d.position.begin() + (std::ptrdiff_t)from, d.position.begin() + (std::ptrdiff_t)(from + count));
  part.valid.assign(count, 1);
  probe_validate(part, pt, 64);
  for (size_t k = 0; k < count; k++) d.valid[from + k] = part.valid[k] && part.position[k] == d.position[from + k] ? 1 : 0;
}

}  // namespace

/* Round 412 (ADR 0106): the volumes gathered on the main thread when Generate Lighting starts - every enabled one with
 * Bake in Blendity on, its grid placed - for the worker; a grid past LightVolumeSH::kMaxVoxels is refused, said
 * (review 398). The ids of all of them: Blendity's bakes of volumes no longer baked here go when the bake lands. */
void Editor::bake_gather_volumes(BakeJob &j) {
  scene_->for_each([&](GameObject &g) {
    const LightVolumeComponent *v = g.get<LightVolumeComponent>();
    if (!v || !v->enabled || !v->bake_in_blendity || !g.active_in_hierarchy()) return;
    j.volume_ids.insert(g.id);
    BakeJob::Volume vol;
    vol.id = g.id;
    vol.name = g.name;
    light_volume_resolution(g, *v, vol.res);
    const size_t voxels = (size_t)vol.res[0] * (size_t)vol.res[1] * (size_t)vol.res[2];
    if (voxels > LightVolumeSH::kMaxVoxels) {
      Log::warn("Light Volume %s: %d x %d x %d voxels is more than Blendity bakes (%zu) - lower its Voxels Per Unit or split it", g.name.c_str(), vol.res[0],
                vol.res[1], vol.res[2], LightVolumeSH::kMaxVoxels);
      return;
    }
    vol.positions = light_volume_positions(g.world_matrix(), vol.res);
    vol.rotation = g.world_rotation();
    j.volumes.push_back(std::move(vol));
  });
}

/* The worker: each volume's grid as probes - checked for geometry, baked, fitted to its SH - from the bake's meshes and
 * lights; a cancel ends it between slices (the volumes finished land anyway). */
void Editor::bake_volumes_on_worker(BakeJob &j) {
  if (j.volumes.empty()) return;
  ScopedTimer t;
  std::vector<PTObject> objs;
  for (const BakeObject &o : j.objects) objs.push_back({&o.mesh->render_mesh_tangents(), o.model, &o.materials});
  std::vector<RenderLight> lights;
  for (const BakeLight &l : j.lights)
    if (l.mode != 0) lights.push_back(l.light);
  PathTracer pt;
  PTSettings ps;
  ps.max_bounces = std::max(0, j.bounces - 1);
  ps.denoise = false, ps.use_guiding = false;
  pt.set_settings(ps);
  pt.build(objs, lights, j.env);
  const int psamples = std::max(24, std::min(4096, j.indirect_samples / 4));
  int kept = 0;
  for (size_t at = 0; at < j.volumes.size() && !j.cancel.load(std::memory_order_relaxed); at++) {
    const BakeJob::Volume &v = j.volumes[at];
    ProbeVolumeData d;
    d.position = v.positions;
    d.valid.assign(d.position.size(), 1);
    d.baked.resize(d.position.size());
    const size_t n = d.probe_count();
    for (size_t from = 0; from < n && !j.cancel.load(std::memory_order_relaxed); from += kValidateSlice) validate_slice(d, pt, from, std::min(kValidateSlice, n - from));
    for (size_t next = 0; next < n && !j.cancel.load(std::memory_order_relaxed); next += kSlice) {
      probe_bake(d, pt, j.lights, psamples, j.bounces, next, std::min(kSlice, n - next), j.indirect_intensity);
      j.progress.store((float)(next + std::min(kSlice, n - next)) / (float)std::max<size_t>(1, n));
      std::lock_guard<std::mutex> lock(j.mutex);
      j.status = strprintf("Baking Light Volumes %zu of %zu  (%.1f s)", at + 1, j.volumes.size(), t.ms() / 1000.0);
    }
    if (j.cancel.load()) break;
    LightVolumeSH sh;
    light_volume_fit(d, j.env, v.res, sh);
    sh.rotation = v.rotation;
    std::string why;
    if (!sh.valid(why)) {
      Log::warn("Light Volume bake of %s refused (%s): kept the last one", v.name.c_str(), why.c_str());
      continue;
    }
    j.volume_bakes[v.id] = std::move(sh);
    kept++;
  }
  Log::info("Light Volumes: %d baked in Blendity", kept);
}

/* Round 401 / 412: the Reflection Probes Blendity bakes - every enabled one in Baked or Realtime mode (Custom keeps its
 * own cubemap) - gathered on the main thread with their places; the worker renders their six faces each. */
void Editor::bake_gather_reflection_probes(BakeJob &j) {
  scene_->for_each([&](GameObject &g) {
    const ReflectionProbeComponent *c = g.get<ReflectionProbeComponent>();
    if (!c || !c->enabled || !g.active_in_hierarchy() || c->mode == 2) return;
    BakeJob::Probe p;
    p.id = g.id;
    p.name = g.name;
    p.eye = g.world_position();
    p.size = std::max(16, std::min(512, c->resolution));
    p.near_clip = std::max(0.01f, c->near_clip);
    p.far_clip = std::max(1.0f, c->far_clip);
    j.probes.push_back(p);
  });
  j.probe_samples = std::max(16, std::min(1024, scene_->lighting.indirect_samples / 8));
}

void Editor::bake_reflection_probes_on_worker(BakeJob &j) {
  if (j.probes.empty()) return;
  ScopedTimer t;
  std::vector<PTObject> objs;
  for (const BakeObject &o : j.objects) objs.push_back({&o.mesh->render_mesh_tangents(), o.model, &o.materials});
  std::vector<RenderLight> lights;
  for (const BakeLight &l : j.lights) lights.push_back(l.light);
  PathTracer pt;
  PTSettings ps;
  ps.max_bounces = std::max(1, j.bounces);
  ps.denoise = false;
  ps.use_guiding = false;
  pt.set_settings(ps);
  pt.build(objs, lights, j.env);
  static const Vec3 kFwd[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  static const Vec3 kUp[6] = {{0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}};
  for (size_t at = 0; at < j.probes.size() && !j.cancel.load(std::memory_order_relaxed); at++) {
    const BakeJob::Probe &p = j.probes[at];
    ReflectionProbeBake bake;
    bake.size = p.size;
    for (int face = 0; face < 6 && !j.cancel.load(std::memory_order_relaxed); face++) {
      pt.set_camera(Mat4::look_at(p.eye, p.eye + kFwd[face], kUp[face]), Mat4::perspective(kPi * 0.5f, 1.0f, p.near_clip, p.far_clip), p.size, p.size);
      pt.reset();
      while (pt.samples() < j.probe_samples && !j.cancel.load(std::memory_order_relaxed)) pt.render(200.0, j.probe_samples);
      {
        std::lock_guard<std::mutex> lock(j.mutex);
        j.status = strprintf("Baking reflection probe %zu of %zu, face %d  (%.1f s)", at + 1, j.probes.size(), face + 1, t.ms() / 1000.0);
      }
      j.progress.store(((float)at + (face + 1) / 6.0f) / (float)j.probes.size());
      if (j.cancel.load()) break;
      const std::vector<float> rgb = pt.linear_rgb(false);
      std::vector<Vec3> &out = bake.faces[face];
      out.assign((size_t)p.size * p.size, Vec3(0.0f));
      for (size_t i = 0; i < out.size() && i * 3 + 2 < rgb.size(); i++) {
        const Vec3 v(rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]);
        out[i] = std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) ? vmax(v, Vec3(0.0f)) : Vec3(0.0f);
      }
    }
    if (j.cancel.load()) break;
    bake.from_unity = false;
    j.probe_bakes[p.id] = std::move(bake);
    j.probe_maps[p.id] = reflection_probe_texture(j.probe_bakes[p.id]);  // (round 428: the GGX levels here, not in the landing frame - 16 ms a 128 probe)
  }
  Log::info("Reflection probes: %zu baked", j.probe_bakes.size());
}

void Editor::load_light_volume_bakes(const std::string &dir) {
  const std::string path = fs::join(dir, kBakesFile);
  std::string err;
  if (fs::exists(path) && !bl::load_light_volume_bakes(path, light_volume_bakes_, err))
    Log::warn("Light Volume bakes not loaded: %s", err.c_str()), light_volume_bakes_.clear();
  livelink_light_volume_bakes_changed();
  light_volume_bakes_dirty_ = false;  // (as on disk)
}

/* Round 401: the reflection probes' bakes, beside the Light Volumes'. */
void Editor::load_reflection_probe_bakes(const std::string &dir) {
  const std::string path = fs::join(dir, "ReflectionProbes.bin");
  std::string err;
  reflection_probe_bakes_.clear();
  if (fs::exists(path) && !bl::load_reflection_probe_bakes(path, reflection_probe_bakes_, err))
    Log::warn("Reflection probe bakes not loaded: %s", err.c_str()), reflection_probe_bakes_.clear();
  reflection_probe_bakes_changed();
  reflection_probe_bakes_dirty_ = false;  // (as on disk)
}

void Editor::save_reflection_probe_bakes(const std::string &dir) {
  const std::string path = fs::join(dir, "ReflectionProbes.bin");
  std::string err;
  if (reflection_probe_bakes_.empty()) {
    if (fs::exists(path)) std::remove(path.c_str());
    return;
  }
  fs::make_dirs(dir);
  if (!bl::save_reflection_probe_bakes(path, reflection_probe_bakes_, err)) Log::error("Could not save the reflection probe bakes: %s", err.c_str());
}

void Editor::save_light_volume_bakes(const std::string &dir) {
  const std::string path = fs::join(dir, kBakesFile);
  std::string err;
  if (light_volume_bakes_.empty()) {
    if (fs::exists(path)) std::remove(path.c_str());  // (an older bake's must not come back)
    return;
  }
  fs::make_dirs(dir);
  if (!bl::save_light_volume_bakes(path, light_volume_bakes_, err)) Log::error("Could not save the Light Volume bakes: %s", err.c_str());
}

}  // namespace bl

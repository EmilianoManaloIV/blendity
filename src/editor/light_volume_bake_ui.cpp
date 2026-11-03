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

/* The run begun: the volumes to bake, the path tracer from Generate Lighting's scene; Blendity's bakes of volumes no
 * longer baked here dropped (review 398: a stale one would have gone to Unity when the toggle came back). False with
 * nothing to bake. */
bool Editor::light_volume_bake_begin() {
  lv_bake_.started = true;
  std::set<uint64_t> baking;
  scene_->for_each([&](GameObject &g) {
    const LightVolumeComponent *v = g.get<LightVolumeComponent>();
    if (v && v->enabled && v->bake_in_blendity && g.active_in_hierarchy()) lv_bake_.ids.push_back(g.id), baking.insert(g.id);
  });
  const size_t before = light_volume_bakes_.size();
  for (auto it = light_volume_bakes_.begin(); it != light_volume_bakes_.end();)
    it = !it->second.from_unity && !baking.count(it->first) ? light_volume_bakes_.erase(it) : std::next(it);
  if (light_volume_bakes_.size() != before) livelink_light_volume_bakes_changed();  // (re-check 398: the file drops them too)
  if (lv_bake_.ids.empty()) return false;
  std::vector<PTObject> objs;
  for (const BakeObject &o : bake_objects_) objs.push_back({&o.mesh->render_mesh_tangents(), o.model, &o.materials});
  std::vector<RenderLight> lights;
  for (const BakeLight &l : bake_lights_)
    if (l.mode != 0) lights.push_back(l.light);
  lv_bake_.pt = std::make_unique<PathTracer>();
  PTSettings ps;
  ps.max_bounces = std::max(0, scene_->lighting.bounces - 1);
  ps.denoise = false, ps.use_guiding = false;
  lv_bake_.pt->set_settings(ps);
  lv_bake_.pt->build(objs, lights, env_);
  lv_bake_.at = 0;
  return light_volume_bake_next();
}

/* One step of the volumes' bake; false once there is nothing (more) to bake. */
bool Editor::light_volume_bake_step(double budget) {
  if (!lv_bake_.started && !light_volume_bake_begin()) return false;
  if (!lv_bake_.probes) return false;
  ScopedTimer t;
  const LightingSettings &ls = scene_->lighting;
  const int psamples = std::max(24, std::min(4096, ls.indirect_samples / 4));
  do {
    ProbeVolumeData &d = *lv_bake_.probes;
    const size_t n = d.probe_count();
    if (lv_bake_.validated < n) {
      const size_t slice = std::min(kValidateSlice, n - lv_bake_.validated);
      validate_slice(d, *lv_bake_.pt, lv_bake_.validated, slice);
      lv_bake_.validated += slice;
      continue;
    }
    const size_t slice = std::min(kSlice, n - lv_bake_.next);
    probe_bake(d, *lv_bake_.pt, bake_lights_, psamples, ls.bounces, lv_bake_.next, slice, ls.indirect_intensity);
    lv_bake_.next += slice;
    if (lv_bake_.next < n) continue;
    light_volume_bake_keep();
    lv_bake_.at++;
    if (!light_volume_bake_next()) break;
  } while (t.ms() < budget);
  bake_status_ = strprintf("Baking Light Volumes %zu of %zu  (%.1f s)", std::min(lv_bake_.at + 1, lv_bake_.ids.size()), lv_bake_.ids.size(), now_seconds() - bake_start_);
  if (lv_bake_.probes) return true;
  Log::info("Light Volumes: %d baked in Blendity", lv_bake_.kept);
  lv_bake_ = LightVolumeBakeRun{};
  lv_bake_.started = true;  // (this run's done: Generate Lighting finishes)
  return false;
}

/* The volume just baked, fitted and kept - Live Link sends it at once (review 398: a cancel after it kept it unsent). */
void Editor::light_volume_bake_keep() {
  GameObject *g = scene_->find(lv_bake_.ids[lv_bake_.at]);
  if (!g) return;  // (deleted while it baked)
  LightVolumeSH sh;
  light_volume_fit(*lv_bake_.probes, env_, lv_bake_.res, sh);
  sh.rotation = g->world_rotation();
  std::string why;
  if (!sh.valid(why)) return Log::warn("Light Volume bake of %s refused (%s): kept the last one", g->name.c_str(), why.c_str());
  light_volume_bakes_[g->id] = std::move(sh);
  lv_bake_.kept++;
  livelink_light_volume_bakes_changed();
}

/* The next volume's probes placed (validated and baked in the steps); false when none is left. A grid past
 * LightVolumeSH::kMaxVoxels is refused, said (review 398: a world-sized box had no cap). */
bool Editor::light_volume_bake_next() {
  lv_bake_.probes.reset();
  lv_bake_.next = lv_bake_.validated = 0;
  for (; lv_bake_.at < lv_bake_.ids.size(); lv_bake_.at++) {
    GameObject *g = scene_->find(lv_bake_.ids[lv_bake_.at]);
    const LightVolumeComponent *v = g ? g->get<LightVolumeComponent>() : nullptr;
    if (!v) continue;  // (deleted since the bake began)
    light_volume_resolution(*g, *v, lv_bake_.res);
    const size_t voxels = (size_t)lv_bake_.res[0] * (size_t)lv_bake_.res[1] * (size_t)lv_bake_.res[2];
    if (voxels > LightVolumeSH::kMaxVoxels) {
      Log::warn("Light Volume %s: %d x %d x %d voxels is more than Blendity bakes (%zu) - lower its Voxels Per Unit or split it", g->name.c_str(),
                lv_bake_.res[0], lv_bake_.res[1], lv_bake_.res[2], LightVolumeSH::kMaxVoxels);
      continue;
    }
    auto d = std::make_unique<ProbeVolumeData>();
    d->position = light_volume_positions(g->world_matrix(), lv_bake_.res);
    d->valid.assign(d->position.size(), 1);
    d->baked.resize(d->position.size());
    lv_bake_.probes = std::move(d);
    return true;
  }
  return false;
}

void Editor::load_light_volume_bakes(const std::string &dir) {
  const std::string path = fs::join(dir, kBakesFile);
  std::string err;
  if (fs::exists(path) && !bl::load_light_volume_bakes(path, light_volume_bakes_, err))
    Log::warn("Light Volume bakes not loaded: %s", err.c_str()), light_volume_bakes_.clear();
  livelink_light_volume_bakes_changed();
  light_volume_bakes_dirty_ = false;  // (as on disk)
}

/* Round 401: the Reflection Probes Blendity bakes - every enabled one in Baked or Realtime mode (Custom keeps its own
 * cubemap). The tracer is built once from the bake's meshes and lights, as the probe volume's is. */
bool Editor::reflection_probe_bake_begin() {
  rp_bake_ = ReflectionProbeBakeRun{};
  scene_->for_each([&](GameObject &g) {
    const ReflectionProbeComponent *c = g.get<ReflectionProbeComponent>();
    if (c && c->enabled && g.active_in_hierarchy() && c->mode != 2) rp_bake_.ids.push_back(g.id);
  });
  if (rp_bake_.ids.empty()) return false;
  std::vector<PTObject> objs;
  for (const BakeObject &o : bake_objects_) objs.push_back({&o.mesh->render_mesh_tangents(), o.model, &o.materials});
  std::vector<RenderLight> lights;
  for (const BakeLight &l : bake_lights_) lights.push_back(l.light);
  rp_bake_.pt = std::make_unique<PathTracer>();
  PTSettings ps;
  ps.max_bounces = std::max(1, scene_->lighting.bounces);
  ps.denoise = false;
  ps.use_guiding = false;
  rp_bake_.pt->set_settings(ps);
  rp_bake_.pt->build(objs, lights, env_);
  rp_bake_.samples = std::max(16, std::min(1024, scene_->lighting.indirect_samples / 8));
  rp_bake_.begun = true;
  return true;
}

/* A slice of the probes' faces: the current face traced until its samples are in, then kept; the next face, the next
 * probe; every bake kept and the file marked changed when the last is done. */
bool Editor::reflection_probe_bake_step(double budget) {
  if (!rp_bake_.begun && !reflection_probe_bake_begin()) return false;
  ScopedTimer t;
  while (rp_bake_.at < rp_bake_.ids.size()) {
    GameObject *g = scene_->find(rp_bake_.ids[rp_bake_.at]);
    const ReflectionProbeComponent *c = g ? g->get<ReflectionProbeComponent>() : nullptr;
    if (!c) { rp_bake_.at++, rp_bake_.face = 0; continue; }
    const int size = std::max(16, std::min(512, c->resolution));
    if (rp_bake_.face == 0 && rp_bake_.current.size != size) rp_bake_.current = ReflectionProbeBake{}, rp_bake_.current.size = size;
    static const Vec3 kFwd[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    static const Vec3 kUp[6] = {{0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}};
    const Vec3 eye = g->world_position();
    if (rp_bake_.pt->samples() == 0 || rp_bake_.pt->width() != size) {
      rp_bake_.pt->set_camera(Mat4::look_at(eye, eye + kFwd[rp_bake_.face], kUp[rp_bake_.face]), Mat4::perspective(kPi * 0.5f, 1.0f, std::max(0.01f, c->near_clip), std::max(1.0f, c->far_clip)), size, size);
      rp_bake_.pt->reset();
    }
    rp_bake_.pt->render(std::max(1.0, budget - t.ms()), rp_bake_.samples);
    bake_status_ = strprintf("Baking reflection probe %zu of %zu, face %d  (%.1f s)", rp_bake_.at + 1, rp_bake_.ids.size(), rp_bake_.face + 1, now_seconds() - bake_start_);
    if (rp_bake_.pt->samples() < rp_bake_.samples) return true;
    const std::vector<float> rgb = rp_bake_.pt->linear_rgb(false);
    std::vector<Vec3> &face = rp_bake_.current.faces[rp_bake_.face];
    face.assign((size_t)size * size, Vec3(0.0f));
    for (size_t i = 0; i < face.size() && i * 3 + 2 < rgb.size(); i++) {
      const Vec3 v(rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]);
      face[i] = std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) ? vmax(v, Vec3(0.0f)) : Vec3(0.0f);
    }
    rp_bake_.pt->reset();
    rp_bake_.face++;
    if (rp_bake_.face < 6) {
      if (t.ms() >= budget) return true;
      continue;
    }
    rp_bake_.current.from_unity = false;
    reflection_probe_bakes_[g->id] = rp_bake_.current;
    rp_bake_.current = ReflectionProbeBake{};
    rp_bake_.at++, rp_bake_.face = 0;
    if (t.ms() >= budget) return true;
  }
  const size_t baked = rp_bake_.ids.size();
  rp_bake_ = ReflectionProbeBakeRun{};
  reflection_probe_bakes_changed();
  Log::info("Reflection probes: %zu baked", baked);
  return false;
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

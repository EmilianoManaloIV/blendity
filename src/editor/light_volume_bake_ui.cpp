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
#include "../render/reflection_probe.h"
#include "../core/core.h"
#include "../core/json.h"

#include <filesystem>

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

/* A face as before round 451 (and without lightmaps): the path tracer's camera at the probe, raw geometry path-traced. */
void path_traced_face(PathTracer &pt, Vec3 eye, int size, float near_clip, float far_clip, int face, int samples, const std::atomic<bool> &cancel,
                      std::vector<Vec3> &out) {
  static const Vec3 kFwd[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  static const Vec3 kUp[6] = {{0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}};
  pt.set_camera(Mat4::look_at(eye, eye + kFwd[face], kUp[face]), Mat4::perspective(kPi * 0.5f, 1.0f, near_clip, far_clip), size, size);
  pt.reset();
  while (pt.samples() < samples && !cancel.load(std::memory_order_relaxed)) pt.render(200.0, samples);
  const std::vector<float> rgb = pt.linear_rgb(false);
  out.assign((size_t)size * size, Vec3(0.0f));
  for (size_t i = 0; i < out.size() && i * 3 + 2 < rgb.size(); i++) {
    const Vec3 v(rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]);
    out[i] = std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) ? vmax(v, Vec3(0.0f)) : Vec3(0.0f);
  }
}

/* Round 451: the lights the lightmaps don't hold - Realtime ones, Mixed ones (Baked Indirect bakes only their bounce),
 * Shadowmask ones - which Unity's capture of the lit scene shines directly; Baked and Subtractive ones are in the maps. */
std::vector<RenderLight> lights_not_in_lightmaps(const std::vector<BakeLight> &lights, const LightingData &ld) {
  std::vector<RenderLight> out;
  for (const BakeLight &l : lights) {
    const auto m = ld.light_modes.find(l.id);
    const bool in_maps = m != ld.light_modes.end() && (m->second == 2 || m->second == 3);
    if (in_maps) continue;
    out.push_back(l.light);
    out.back().baked_falloff = false;  // (review 451: shone in realtime, it falls off as the views draw it, not as baked)
  }
  return out;
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

/* Round 451 (task 0427): with the lightmaps just baked, the faces are captured from the lit scene as Unity captures
 * its probes - static surfaces from their lightmaps plus the lights the maps don't hold (reflection_probe_capture_lit) -
 * so a probe shows the room the views show; without lightmaps, path-traced as before. */
void Editor::bake_reflection_probes_on_worker(BakeJob &j) {
  if (j.probes.empty()) return;
  ScopedTimer t;
  std::vector<PTObject> objs;
  std::vector<uint64_t> ids;
  for (const BakeObject &o : j.objects) objs.push_back({&o.mesh->render_mesh_tangents(), o.model, &o.materials}), ids.push_back(o.id);
  std::vector<RenderLight> lights;
  for (const BakeLight &l : j.lights) lights.push_back(l.light);
  PathTracer pt;
  PTSettings ps;
  ps.max_bounces = std::max(1, j.bounces);
  ps.denoise = false;
  ps.use_guiding = false;
  pt.set_settings(ps);
  pt.build(objs, lights, j.env);
  const bool from_lightmaps = !j.lighting.empty();
  const std::vector<RenderLight> direct = lights_not_in_lightmaps(j.lights, j.lighting);
  ProbeLitCapture cap;
  cap.pt = &pt, cap.lighting = &j.lighting, cap.object_ids = &ids, cap.lights = &direct, cap.cancel = &j.cancel;
  cap.samples = std::max(4, std::min(64, j.probe_samples / 4));  // (a lightmap lookup a ray, not a path: a few jittered rays a texel smooth the edges)
  double lit_share = 0.0;
  int lit_faces = 0;
  for (size_t at = 0; at < j.probes.size() && !j.cancel.load(std::memory_order_relaxed); at++) {
    const BakeJob::Probe &p = j.probes[at];
    ReflectionProbeBake bake;
    bake.size = p.size;
    for (int face = 0; face < 6 && !j.cancel.load(std::memory_order_relaxed); face++) {
      if (from_lightmaps) lit_share += reflection_probe_capture_lit(cap, p.eye, p.size, p.near_clip, face, bake.faces[face]), lit_faces++;
      else path_traced_face(pt, p.eye, p.size, p.near_clip, p.far_clip, face, j.probe_samples, j.cancel, bake.faces[face]);
      {
        std::lock_guard<std::mutex> lock(j.mutex);
        j.status = strprintf("Baking reflection probe %zu of %zu, face %d  (%.1f s)", at + 1, j.probes.size(), face + 1, t.ms() / 1000.0);
      }
      j.progress.store(((float)at + (face + 1) / 6.0f) / (float)j.probes.size());
    }
    if (j.cancel.load()) break;
    bake.from_unity = false;
    j.probe_bakes[p.id] = std::move(bake);
    j.probe_maps[p.id] = reflection_probe_texture(j.probe_bakes[p.id]);  // (round 428: the GGX levels here, not in the landing frame - 16 ms a 128 probe)
  }
  if (from_lightmaps)
    Log::info("Reflection probes: %zu baked from the lightmapped scene (%.0f%% of their rays on lightmapped surfaces, %zu lights added directly)", j.probe_bakes.size(),
              100.0 * lit_share / std::max(1, lit_faces), direct.size());
  else Log::info("Reflection probes: %zu baked (path-traced: no lightmaps)", j.probe_bakes.size());
}

/* Round 451 (task 0427): `lighting save <dir>` - the shown lightmaps as a bake's folder plus objects.json (each entry's
 * object by id and hierarchy path), for tools/unity_compare.py's page compare. */
std::string trim_overwrite_word(const std::string &s) {
  const size_t end = s.find_last_not_of(" \t");
  const size_t cut = end == std::string::npos ? std::string::npos : s.find_last_of(" \t", end);
  if (cut == std::string::npos) return std::string();
  const size_t keep = s.find_last_not_of(" \t", cut);
  return keep == std::string::npos ? std::string() : s.substr(0, keep + 1);
}

void Editor::save_shown_lighting(const std::string &folder, bool overwrite) {
  if (folder.empty()) return Log::warn("lighting save: give a folder (a relative one is the project's)");
  const std::filesystem::path p = std::filesystem::u8path(folder);
  const std::string dir = p.is_absolute() ? folder : fs::join(project_root_, folder);  // (review 451: not the process's working folder)
  const LightingData &ld = shown_lighting();
  if (ld.empty()) return refuse("lighting save: no lightmaps are shown (Generate Lighting, or `unity lightmaps`, first)", "", "");
  if (fs::exists(fs::join(dir, "LightingData.bin")) && !overwrite)
    return refuse("lighting save: " + dir + " holds lightmaps already - add `overwrite` to replace them", "", "");
  std::string err;
  if (!ld.save(dir, err)) return Log::error("lighting save: %s", err.c_str());
  JsonWriter w;
  w.obj().key("source").str(lighting_from_unity_ ? "unity" : "blendity").key("objects").arr();
  for (const auto &[id, e] : ld.entries) {
    const GameObject *g = scene_->find(id);
    std::string path = g ? g->name : std::string();
    for (const GameObject *p = g ? g->parent : nullptr; p; p = p->parent) path = p->name + "/" + path;
    w.obj().key("id").str(std::to_string(id)).key("path").str(path).key("page").integer(e.page).key("tris").integer((int64_t)(e.tri_uv.size() / 3)).end();
  }
  w.end().end();
  if (!fs::write_file(fs::join(dir, "objects.json"), w.out)) return Log::error("lighting save: could not write objects.json in %s", dir.c_str());
  Log::info("lighting save: %zu page(s), %zu object(s) (%s's lightmaps) in %s", ld.pages.size(), ld.entries.size(), lighting_from_unity_ ? "Unity" : "Blendity", dir.c_str());
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

/* Round 443 (task 0419): the baked volumes as the views sample them - each enabled, active Light Volume that has a bake
 * of its grid, its box from the object's transform (the unit cube about it), the Smooth Blending in the cube's units,
 * Color x Intensity, the baked L1's turn if the object turned since the bake; the smallest box first, as the package
 * orders them. The bakes' copies are shared (made once per bake: the views and a render's worker hold them without
 * copying). Null with the setting off, or when no volume has a bake. */
const LightVolumeFrame *Editor::light_volume_frame() {
  if (!scene_->lighting.light_volumes_in_views || light_volume_bakes_.empty()) return nullptr;
  if (lv_shared_gen_ != lv_bake_gen_) {
    lv_shared_.clear();
    for (const auto &[id, sh] : light_volume_bakes_)
      lv_shared_[id] = std::make_shared<const LightVolumeSH>(sh.unity_axes ? light_volume_from_unity(sh) : sh);  // (Unity's own bake read back: its X mirrored)
    lv_shared_gen_ = lv_bake_gen_;
  }
  lv_frame_.volumes.clear();
  std::vector<float> size;
  for (const auto &[id, sh] : lv_shared_) {
    const GameObject *g = scene_->find(id);
    const LightVolumeComponent *v = g ? g->get<LightVolumeComponent>() : nullptr;
    if (!v || !v->enabled || !g->active_in_hierarchy()) continue;
    int res[3];
    light_volume_resolution(*g, *v, res);
    if (sh->res[0] != res[0] || sh->res[1] != res[1] || sh->res[2] != res[2]) continue;  // (its grid changed since: stale until baked again)
    const Mat4 &w = g->world_matrix();
    const float scale[3] = {length(w.column(0)), length(w.column(1)), length(w.column(2))};
    if (!(scale[0] > 1e-6f && scale[1] > 1e-6f && scale[2] > 1e-6f)) continue;
    LightVolumeInstance in;
    in.sh = sh;
    in.inv_world = w.inverse();
    for (int a = 0; a < 3; a++) in.edge[a] = 1.0f / std::max(v->smooth_blending / scale[a], 1e-6f);
    const Vec3 tint = v->color * std::max(v->intensity, 0.0f);
    in.tint = finite_bits(tint.x) && finite_bits(tint.y) && finite_bits(tint.z) ? tint : Vec3(1.0f);  // (review 443: a NaN or infinite colour is no light)
    in.additive = v->additive;
    const LightVolumeSH &baked = light_volume_bakes_.at(id);
    if (!baked.from_unity) in.turn = Mat4::rotate(normalize(baked.rotation * g->world_rotation().conjugate()));  // (the L1 is in world axes as baked: turned with the box)
    lv_frame_.volumes.push_back(std::move(in));
    size.push_back(scale[0] * scale[1] * scale[2]);
  }
  std::vector<size_t> order(size.size());
  for (size_t i = 0; i < order.size(); i++) order[i] = i;
  std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return size[a] < size[b]; });
  std::vector<LightVolumeInstance> sorted;
  for (size_t i : order) sorted.push_back(std::move(lv_frame_.volumes[i]));
  if (sorted.size() > (size_t)LightVolumeComponent::kMaxVolumes) sorted.resize((size_t)LightVolumeComponent::kMaxVolumes);  // (the smallest 32: as the card's shader and the package)
  lv_frame_.volumes = std::move(sorted);
  return lv_frame_.empty() ? nullptr : &lv_frame_;
}

}  // namespace bl

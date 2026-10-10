// SPDX-License-Identifier: GPL-2.0-or-later
// Baked lighting in the editor (task 0012, ADR 0010): Generate Lighting, the lightmaps on disk next
// to the scene, and which drawn objects get theirs (Unity: the Lighting window's Generate Lighting,
// Lighting Data Asset, "Lighting out of date").
#include "editor.h"

#include "../core/core.h"
#include "../render/pathtracer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace bl {

/* Unity keeps a scene's lighting in a folder named after the scene file, next to it. Only a plain name
 * makes a folder: never the scene's own folder or anything above it. */
std::string Editor::lighting_dir() const {
  if (!scene_ || scene_->path.empty()) return "";
  const std::string name = fs::stem(scene_->path);
  if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\:") != std::string::npos) return "";
  return fs::join(fs::parent(scene_->path), name);
}

/* lightmap_content_hash() of a drawn mesh at g's place. The mesh part is remembered by its render
 * mesh's serial (new for every rebuild, never reused), so a drag only rehashes the matrix. */
uint64_t Editor::lightmap_hash_of(const GameObject &g, const Mesh &m) {
  const uint64_t serial = m.render_mesh().serial;
  uint64_t mh = 0;
  auto it = serial ? lm_mesh_hash_.find(serial) : lm_mesh_hash_.end();
  if (it != lm_mesh_hash_.end()) mh = it->second;
  else {
    mh = lightmap_mesh_hash(m);
    if (lm_mesh_hash_.size() > 8192) lm_mesh_hash_.clear();  // old meshes' serials never come back
    if (serial) lm_mesh_hash_[serial] = mh;
  }
  return lightmap_placed_hash(mh, g.world_matrix());
}

/* What the baked lighting depends on: the contributing objects (shape, place, material colours),
 * the Baked and Mixed lights, the world and the lighting settings. Different from the bake's = out of date. */
uint64_t Editor::lightmap_scene_key() {
  uint64_t h = 1469598103934665603ull;
  auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull, h ^= h >> 29; };
  auto mixf = [&](const float *p, size_t n) {
    for (size_t i = 0; i < n; i++) {
      uint32_t b;
      std::memcpy(&b, p + i, 4);
      mix(b);
    }
  };
  scene_->for_each([&](GameObject &g) {
    if (!g.active_in_hierarchy()) return;
    const auto *mr = g.get<MeshRenderer>();
    const Mesh *m = mr && mr->enabled && mr->contribute_gi && mr->show_in_renders ? g.evaluated_mesh(1) : nullptr;
    if (m) {
      mix(g.id);
      mix(lightmap_hash_of(g, *m));
      mixf(&mr->scale_in_lightmap, 1);
      mix(mr->generate_lightmap_uvs);
      for (const MaterialPtr &mp : mr->materials)
        if (mp) {
          mixf(&mp->base_color.x, 3);
          mixf(&mp->emission.x, 3);
          mixf(&mp->emission_strength, 1);
          mixf(&mp->metallic, 1);
        }
    }
    if (auto *l = g.get<Light>()) {
      if (!l->enabled || l->mode == 0) return;
      mix(g.id);
      mix(hash_component(*l));
      mixf(g.world_matrix().m, 16);
    }
  });
  mix(hash_reflect([this](Reflector &r) { scene_->environment.reflect(r); }));
  /* The settings a bake depends on (not Realtime GI's or Auto Generate). */
  const LightingSettings &ls = scene_->lighting;
  mix(ls.baked_gi);
  mix((uint64_t)ls.lighting_mode);
  mixf(&ls.texels_per_unit, 1);
  mix((uint64_t)ls.max_size);
  mix((uint64_t)ls.padding);
  mix((uint64_t)ls.direct_samples);
  mix((uint64_t)ls.indirect_samples);
  mix((uint64_t)ls.bounces);
  mix(ls.denoise);
  mixf(&ls.indirect_intensity, 1);
  return h ? h : 1;
}

/* Only recomputed when something the views draw or the lighting settings changed (the status bar asks
 * every frame). */
bool Editor::lighting_out_of_date() {
  if (lighting_data_.empty()) return false;
  const uint64_t k = scene_render_hash() ^ hash_reflect([this](Reflector &r) { scene_->lighting.reflect(r); }) * 31 ^ lighting_gen_ * 131;
  if (k != ood_key_) {
    ood_key_ = k;
    ood_ = lighting_data_.scene_key != lightmap_scene_key();
  }
  return ood_;
}

/* Gives a drawn item its lightmap when the object was baked as it is now. */
void Editor::attach_lightmap(const GameObject &g, const Mesh &m, DrawItem &it) {
  if (lighting_data_.empty() || !scene_->lighting.baked_gi) return;
  auto e = lighting_data_.entries.find(g.id);
  if (e == lighting_data_.entries.end() || e->second.page < 0 || e->second.page >= (int)lighting_data_.pages.size()) return;
  if (e->second.tri_uv.size() != it.mesh->tri_count() * 3) return;
  if (e->second.hash != lightmap_hash_of(g, m)) return;  // changed since the bake: realtime ambient
  if (lm_stale_live_) return;  // lights or world changed since the bake: live GI until the next one
  it.lightmap = &lighting_data_.pages[(size_t)e->second.page];
  it.lightmap_uv = &e->second.tri_uv;
}

void Editor::load_lighting_data() {
  lighting_data_ = LightingData{};
  probes_ = ProbeVolumeData{};
  probe_live_ready_ = false;
  probe_live_left_ = 0;
  probe_live_key_ = probe_layout_key_ = 0;
  lighting_gen_++;
  lighting_saved_dir_.clear();
  lm_mesh_hash_.clear();
  ood_key_ = 0;
  const std::string dir = lighting_dir();
  if (dir.empty() || !fs::exists(fs::join(dir, "LightingData.bin"))) return;
  std::string err;
  if (!lighting_data_.load(dir, err)) Log::warn("Baked lighting not loaded: %s", err.c_str());
  else lighting_saved_dir_ = dir;
  const std::string pv = fs::join(dir, "ProbeVolume.bin");
  if (fs::exists(pv) && !probes_.load(pv, err)) Log::warn("Probe volume not loaded: %s", err.c_str());
  if (!probes_.empty() && (lighting_saved_dir_.empty() || probes_.scene_key != lighting_data_.scene_key)) {
    probes_ = ProbeVolumeData{};  // not from the same bake as the lightmaps (or those didn't load)
    Log::warn("Probe volume not loaded: it doesn't belong to this bake");
  }
  if (!lighting_data_.empty())
    Log::info("Baked lighting: %zu lightmap%s, %zu objects", lighting_data_.pages.size(), lighting_data_.pages.size() == 1 ? "" : "s",
              lighting_data_.entries.size());
}

/* Writes the baked data next to the scene file if it isn't there yet (baked before the scene had a
 * file, or the scene was saved under another name). */
void Editor::save_lighting_data() {
  if (lighting_data_.empty() && (probes_.empty() || probes_.scene_key == 0)) return;
  const std::string dir = lighting_dir();
  if (dir.empty() || dir == lighting_saved_dir_) return;
  std::string err;
  const std::string pv = fs::join(dir, "ProbeVolume.bin");
  const bool baked_probes = !probes_.empty() && probes_.scene_key != 0;
  if (!baked_probes && fs::exists(pv)) std::remove(pv.c_str());  // an older bake's probes must not come back
  if (lighting_data_.save(dir, err) && (!baked_probes || probes_.save(pv, err))) {
    lighting_saved_dir_ = dir;
    project_listed_ = -100;
    Log::info("Saved the baked lighting to %s", dir.c_str());
  }
  else Log::error("Could not save the baked lighting: %s", err.c_str());
}

void Editor::bake_start() {
  if (baking_) return;
  if (playing_) {
    Log::warn("Generate Lighting: not while playing.");
    return;
  }
  const LightingSettings &ls = scene_->lighting;
  if (!ls.baked_gi) {
    Log::warn("Generate Lighting: Baked Global Illumination is off (Lighting window).");
    return;
  }
  std::vector<BakeObject> objects;
  std::vector<BakeLight> lights;
  scene_->for_each([&](GameObject &g) {
    if (!g.active_in_hierarchy()) return;
    if (auto *mr = g.get<MeshRenderer>())
      if (mr->enabled && mr->contribute_gi && mr->show_in_renders)
        if (const Mesh *m = g.evaluated_mesh(1)) {
          BakeObject o;
          o.id = g.id;
          o.mesh = std::make_shared<Mesh>(*m);  // the bake's own copy: the scene may change while it runs
          o.model = g.world_matrix();
          o.materials = mr->materials;
          o.scale = mr->scale_in_lightmap;
          o.generate_uvs = mr->generate_lightmap_uvs;
          o.want_lightmap = mr->receive_gi == 0;  // Light Probes: in the scene the light bounces off, no chart
          o.hash = lightmap_hash_of(g, *m);
          objects.push_back(std::move(o));
        }
    if (auto *l = g.get<Light>())
      if (l->enabled) lights.push_back({to_render_light(g, *l), l->mode, g.id});
  });
  update_environment();
  bake_objects_ = objects;  // the probes are baked from the same scene when the lightmaps are done
  bake_lights_ = lights;
  BakeSettings s;
  s.texels_per_unit = ls.texels_per_unit;
  s.max_size = ls.max_size;
  s.padding = ls.padding;
  s.direct_samples = ls.direct_samples;
  s.indirect_samples = ls.indirect_samples;
  s.bounces = ls.bounces;
  s.denoise = ls.denoise;
  s.indirect_intensity = ls.indirect_intensity;
  bake_key_ = lightmap_scene_key();
  bake_start_ = now_seconds();
  baker_.begin(objects, lights, env_, s);
  baking_ = true;
  bake_status_ = strprintf("Baking %zu objects, %zu texels", objects.size(), baker_.texel_count());
  Log::info("Generate Lighting: %zu objects, %zu texels", objects.size(), baker_.texel_count());
}

void Editor::bake_cancel() {
  probe_phase_ = false;
  probe_pt_.reset();
  bake_objects_.clear();
  bake_lights_.clear();
  if (!baking_) {
    baker_.cancel();  // nothing left behind either way
    return;
  }
  baker_.cancel();
  baking_ = false;
  bake_status_ = "Baking cancelled";
  Log::info("Generate Lighting cancelled");
}

void Editor::bake_clear() {
  if (baking_) bake_cancel();
  lighting_data_ = LightingData{};
  probes_ = ProbeVolumeData{};
  probe_live_ready_ = false;
  probe_live_left_ = 0;
  probe_live_key_ = probe_layout_key_ = 0;
  lighting_gen_++;
  const std::string dir = lighting_dir();
  if (!dir.empty() && fs::exists(dir)) {
    if (!delete_project_entry(dir)) Log::warn("Clear Baked Data: %s was left on disk (outside the project's Assets)", dir.c_str());
  }
  lighting_saved_dir_.clear();
  project_listed_ = -100;
  bake_status_ = "Baked data cleared";
}

/* A slice of the bake each frame, like the path-traced render; saves when done. */
void Editor::step_bake() {
  if (!baking_) return;
  const double budget = headless_ ? 1e9 : 40.0;
  if (!probe_phase_) {
    const bool done = baker_.step(budget);
    bake_status_ = strprintf("Baking lighting %.0f%%  (%.1f s)", baker_.progress() * 100.0, now_seconds() - bake_start_);
    if (!done) return;
    lighting_data_ = baker_.take_result();
    lighting_data_.scene_key = bake_key_;
    probe_phase_ = bake_probes_begin();  // then the probes, in slices like the texels
  }
  if (probe_phase_) {
    ScopedTimer t;
    const LightingSettings &ls = scene_->lighting;
    const size_t n = probes_.probe_count();
    do {
      const size_t slice = std::min<size_t>(n - probe_bake_next_, 1024);
      probe_bake(probes_, *probe_pt_, bake_lights_, std::max(24, std::min(4096, ls.indirect_samples / 4)), ls.bounces, probe_bake_next_, slice,
                 ls.indirect_intensity);
      probe_bake_next_ += slice;
    } while (probe_bake_next_ < n && t.ms() < budget);
    bake_status_ = strprintf("Baking probes %.0f%%  (%.1f s)", 100.0 * probe_bake_next_ / std::max<size_t>(1, n), now_seconds() - bake_start_);
    if (probe_bake_next_ < n) return;
    probes_.scene_key = bake_key_;  // (the last slice dilated the invalid probes)
    probe_phase_ = false;
    probe_pt_.reset();
    size_t valid = 0;
    for (uint8_t v : probes_.valid) valid += v;
    Log::info("Probe volume: %zu probes (%zu inside geometry), %zu bricks", n, n - valid, probes_.bricks.size());
  }
  baking_ = false;
  bake_objects_.clear();  // the bake's copies of the meshes
  bake_lights_.clear();
  lighting_gen_++;
  const double secs = now_seconds() - bake_start_;
  bake_status_ = strprintf("Baked %zu lightmap%s in %.1f s", lighting_data_.pages.size(), lighting_data_.pages.size() == 1 ? "" : "s", secs);
  lighting_saved_dir_.clear();
  Log::info("Generate Lighting: done in %.1f s", secs);
  if (lighting_dir().empty()) Log::info("Save the scene to keep its lightmaps: they are written next to the scene file.");
  else save_lighting_data();
}

/* ------------------------------------------------------- probe volumes (task 0014) */

/* The region the Probe Volume components ask for: Global ones around everything that contributes to GI
 * (or every mesh if nothing does), local ones a box of their size. False without any. */
bool Editor::probe_volume_box(AABB &out) {
  uint32_t spacing_bits;  // (by its bits: the setting can be NaN)
  std::memcpy(&spacing_bits, &scene_->lighting.probe_min_spacing, 4);
  const uint64_t key = scene_render_hash() ^ (uint64_t)spacing_bits * 7919;
  if (key == probe_box_key_) {
    out = probe_box_;
    return probe_box_ok_;
  }
  probe_box_key_ = key;
  out = AABB{};
  bool any = false, global = false;
  scene_->for_each([&](GameObject &g) {
    if (!g.active_in_hierarchy()) return;
    auto *pv = g.get<ProbeVolume>();
    if (!pv || !pv->enabled) return;
    any = true;
    if (pv->global) global = true;
    else {
      /* The box in the object's space (its rotation and scale apply), as an axis-aligned world box. */
      const Vec3 h = Vec3(std::fabs(pv->size.x), std::fabs(pv->size.y), std::fabs(pv->size.z)) * 0.5f;
      const Mat4 &w = g.world_matrix();
      for (int k = 0; k < 8; k++) out.add(w.point(Vec3(k & 1 ? h.x : -h.x, k & 2 ? h.y : -h.y, k & 4 ? h.z : -h.z)));
    }
  });
  if (!any) {
    probe_box_ok_ = false;
    return false;
  }
  if (global) {
    AABB all, gi;
    scene_->for_each([&](GameObject &g) {
      if (!g.active_in_hierarchy()) return;
      auto *mr = g.get<MeshRenderer>();
      if (!mr || !mr->enabled || !g.evaluated_mesh(1)) return;
      const AABB b = g.world_bounds();
      all.add(b.min), all.add(b.max);
      if (mr->contribute_gi) gi.add(b.min), gi.add(b.max);
    });
    const AABB &use = gi.valid() ? gi : all;
    if (use.valid()) {
      /* One and a half spacings: the probe lattice then falls between the surfaces at the bounds (a floor
       * at the bottom), not on them, where a probe would see through the surface it sits in. */
      const float pad = 1.5f * std::max(0.05f, scene_->lighting.probe_min_spacing);
      out.add(use.min - Vec3(pad));
      out.add(use.max + Vec3(pad));
    }
  }
  probe_box_ = out;
  probe_box_ok_ = out.valid() && finite_bits(out.min.x) && finite_bits(out.max.x) && finite_bits(out.min.y) && finite_bits(out.max.y) &&
                  finite_bits(out.min.z) && finite_bits(out.max.z);
  return probe_box_ok_;
}

/* Generate Lighting's probe phase: places the probes from the scene the lightmaps were baked from, checks
 * which are inside geometry, and builds the tracer; step_bake() then bakes them in slices. False without a
 * Probe Volume. */
bool Editor::bake_probes_begin() {
  probes_ = ProbeVolumeData{};
  probe_live_ready_ = false;
  probe_live_left_ = 0;
  probe_live_key_ = probe_layout_key_ = 0;
  probe_bake_next_ = 0;
  probe_box_key_ = 0;
  AABB vol;
  if (!probe_volume_box(vol)) return false;
  const LightingSettings &ls = scene_->lighting;
  std::vector<AABB> tris;
  std::vector<PTObject> objs;
  for (const BakeObject &o : bake_objects_) {
    const RenderMesh &rm = o.mesh->render_mesh_tangents();
    for (size_t k = 0; k < rm.tri_count(); k++) {
      AABB b;
      for (int c = 0; c < 3; c++) b.add(o.model.point(rm.positions[rm.indices[k * 3 + c]]));
      tris.push_back(b);
    }
    objs.push_back({&rm, o.model, &o.materials});
  }
  probe_place(probes_, vol, tris, ls.probe_min_spacing, ls.probe_max_spacing);
  if (probes_.empty()) return false;
  probe_pt_ = std::make_unique<PathTracer>();
  PTSettings ps;
  ps.max_bounces = std::max(0, ls.bounces - 1);
  ps.denoise = false;
  ps.use_guiding = false;
  probe_pt_->set_settings(ps);
  std::vector<RenderLight> lights;
  for (const BakeLight &l : bake_lights_)
    if (l.mode != 0) lights.push_back(l.light);
  probe_pt_->build(objs, lights, env_);
  probe_validate(probes_, *probe_pt_, 64);
  return true;
}

/* Realtime GI keeps the probes live: once per frame, a slice of them gathers again through the voxel grid
 * whenever the grid, the sun or the world changed. Without a bake the probes are placed here (around the
 * objects' bounds) and are live only. */
void Editor::update_probes_live() {
  const LightingSettings &ls = scene_->lighting;
  const VoxelGIState &vg = vgi_[1].grid.valid() ? vgi_[1] : vgi_[0];
  if (!ls.realtime_gi || !vg.grid.valid()) {
    probe_live_left_ = 0;
    return;
  }
  if (probes_.empty() || probes_.scene_key == 0) {  // live-only probes: placed around the objects
    AABB vol;
    if (!probe_volume_box(vol)) {
      probes_ = ProbeVolumeData{};
      return;
    }
    uint64_t key = 1469598103934665603ull;
    const float f[8] = {vol.min.x, vol.min.y, vol.min.z, vol.max.x, vol.max.y, vol.max.z, ls.probe_min_spacing, ls.probe_max_spacing};
    for (float v : f) {
      uint32_t b;
      std::memcpy(&b, &v, 4);
      key = (key ^ b) * 1099511628211ull;
    }
    if (key != probe_layout_key_) {
      std::vector<AABB> boxes;
      scene_->for_each([&](GameObject &g) {
        if (g.active_in_hierarchy() && g.get<MeshRenderer>() && g.evaluated_mesh(1)) boxes.push_back(g.world_bounds());
      });
      probe_place(probes_, vol, boxes, ls.probe_min_spacing, ls.probe_max_spacing, 200000);
      probe_layout_key_ = key;
      probe_live_key_ = 0;
      probe_live_ready_ = false;
    }
  }
  if (probes_.empty()) return;
  uint64_t key = 1469598103934665603ull;
  for (const VoxelGIState &st : vgi_) key = (key ^ (st.grid.key + st.assembled * 31 + st.rsm_key * 131)) * 1099511628211ull;
  key ^= hash_reflect([this](Reflector &r) { scene_->environment.reflect(r); }) * 1031;
  const float gp[6] = {ls.gi_radius, ls.gi_intensity, (float)ls.gi_rays, (float)ls.gi_resolution, (float)ls.gi_bounce, (float)ls.gi_sky_occlusion};
  for (float v : gp) {
    uint32_t b;
    std::memcpy(&b, &v, 4);
    key = (key ^ b) * 1099511628211ull;
  }
  if (key != probe_live_key_) {
    probe_live_key_ = key;
    probe_live_left_ = probes_.probe_count();
  }
  if (probe_live_left_ == 0) return;
  const size_t budget = std::min<size_t>(probe_live_left_, 4096);
  probe_cursor_ = probe_live_update(probes_, vg.grid, vg.rsm.valid() ? &vg.rsm : nullptr, env_, vg.frame.params, probe_cursor_, budget);
  probe_live_left_ -= budget;
  probe_live_gen_++;
  if (probe_live_left_ == 0) probe_live_ready_ = true;
  ui_.redraw = true;
}

/* Auto Generate (Unity's): once the lighting is out of date and has stopped changing for a second,
 * bake again. Until then realtime GI (if on) keeps the picture right. */
void Editor::auto_generate_step() {
  const LightingSettings &ls = scene_->lighting;
  if (!ls.auto_generate || !ls.baked_gi || baking_ || playing_ || lighting_data_.empty()) {
    settle_since_ = 0;
    return;
  }
  if (!lighting_out_of_date()) {
    settle_since_ = 0;
    return;
  }
  const double now = now_seconds();
  if (settle_since_ == 0 || ood_key_ != settle_key_) {  // still changing: wait for it to settle
    settle_key_ = ood_key_;
    settle_since_ = now;
  }
  if (now - settle_since_ >= 1.0) {
    settle_since_ = 0;
    Log::info("Auto Generate: the lighting changed, baking again");
    bake_start();
  }
  else ui_.next_wakeup = ui_.next_wakeup > 0 ? std::min(ui_.next_wakeup, settle_since_ + 1.0) : settle_since_ + 1.0;
}

}  // namespace bl

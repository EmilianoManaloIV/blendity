// SPDX-License-Identifier: GPL-2.0-or-later
// Baked lighting in the editor (task 0012, ADR 0010): Generate Lighting, the lightmaps on disk next
// to the scene, and which drawn objects get theirs (Unity: the Lighting window's Generate Lighting,
// Lighting Data Asset, "Lighting out of date").
#include "editor.h"

#include "../core/core.h"

#include <algorithm>
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
  mix(hash_reflect([this](Reflector &r) { scene_->lighting.reflect(r); }));
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
  it.lightmap = &lighting_data_.pages[(size_t)e->second.page];
  it.lightmap_uv = &e->second.tri_uv;
}

void Editor::load_lighting_data() {
  lighting_data_ = LightingData{};
  lighting_gen_++;
  lighting_saved_dir_.clear();
  lm_mesh_hash_.clear();
  ood_key_ = 0;
  const std::string dir = lighting_dir();
  if (dir.empty() || !fs::exists(fs::join(dir, "LightingData.bin"))) return;
  std::string err;
  if (!lighting_data_.load(dir, err)) Log::warn("Baked lighting not loaded: %s", err.c_str());
  else lighting_saved_dir_ = dir;
  if (!lighting_data_.empty())
    Log::info("Baked lighting: %zu lightmap%s, %zu objects", lighting_data_.pages.size(), lighting_data_.pages.size() == 1 ? "" : "s",
              lighting_data_.entries.size());
}

/* Writes the baked data next to the scene file if it isn't there yet (baked before the scene had a
 * file, or the scene was saved under another name). */
void Editor::save_lighting_data() {
  if (lighting_data_.empty()) return;
  const std::string dir = lighting_dir();
  if (dir.empty() || dir == lighting_saved_dir_) return;
  std::string err;
  if (lighting_data_.save(dir, err)) {
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
          o.hash = lightmap_hash_of(g, *m);
          objects.push_back(std::move(o));
        }
    if (auto *l = g.get<Light>())
      if (l->enabled) lights.push_back({to_render_light(g, *l), l->mode, g.id});
  });
  update_environment();
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
  const bool done = baker_.step(headless_ ? 1e9 : 40.0);
  bake_status_ = strprintf("Baking lighting %.0f%%  (%.1f s)", baker_.progress() * 100.0, now_seconds() - bake_start_);
  if (!done) return;
  baking_ = false;
  lighting_data_ = baker_.take_result();
  lighting_data_.scene_key = bake_key_;
  lighting_gen_++;
  const double secs = now_seconds() - bake_start_;
  bake_status_ = strprintf("Baked %zu lightmap%s in %.1f s", lighting_data_.pages.size(), lighting_data_.pages.size() == 1 ? "" : "s", secs);
  lighting_saved_dir_.clear();
  Log::info("Generate Lighting: done in %.1f s", secs);
  if (lighting_dir().empty()) Log::info("Save the scene to keep its lightmaps: they are written next to the scene file.");
  else save_lighting_data();
}

}  // namespace bl

// SPDX-License-Identifier: GPL-2.0-or-later
// Baked lighting in the editor (task 0012, ADR 0010): Generate Lighting, the lightmaps on disk next
// to the scene, and which drawn objects get theirs (Unity: the Lighting window's Generate Lighting,
// Lighting Data Asset, "Lighting out of date").
#include "editor.h"

#include "../core/core.h"
#include "../core/jobs.h"
#include "../render/gpu_device.h"
#include "../render/pathtracer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>

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
uint64_t Editor::mesh_hash_of(const Mesh &m) {
  const uint64_t serial = m.render_mesh().serial;
  auto it = serial ? lm_mesh_hash_.find(serial) : lm_mesh_hash_.end();
  if (it != lm_mesh_hash_.end()) return it->second;
  const uint64_t mh = lightmap_mesh_hash(m);
  if (lm_mesh_hash_.size() > 8192) lm_mesh_hash_.clear();  // old meshes' serials never come back
  if (serial) lm_mesh_hash_[serial] = mh;
  return mh;
}

uint64_t Editor::lightmap_hash_of(const GameObject &g, const Mesh &m) { return lightmap_placed_hash(mesh_hash_of(m), g.world_matrix()); }

/* What the baked lighting depends on: the contributing objects (shape, place, material colours),
 * the Baked and Mixed lights, the world and the lighting settings. Different from the bake's = out of date. */
uint64_t Editor::lightmap_scene_key() {
  uint64_t h = kFnvBasisLegacy;  // (saved with every bake: a new value would mark them all out of date)
  auto mix = [&](uint64_t v) { h = hash_mix(h, v); };
  auto mixf = [&](const float *p, size_t n) {
    for (size_t i = 0; i < n; i++) h = mix_float(h, p[i]);
  };
  scene_->for_each([&](GameObject &g) {
    if (!g.active_in_hierarchy()) return;
    const auto *mr = g.mesh_renderer();
    const Mesh *m = mr && mr->enabled && mr->contribute_gi && mr->show_in_renders && !blockout_uses_as_piece(g) ? g.evaluated_mesh(1) : nullptr;
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
          mix((uint64_t)(int64_t)mp->gi_flags);  // (round 439: Unity's Global Illumination decides whether the emission bakes)
          mixf(&mp->metallic, 1);
        }
    }
    if (auto *l = g.light()) {
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
  /* Round 455: Light Probe Groups' probes in the world, only when there are some - a scene without a group keeps the key
   * its saved bakes hold. */
  const std::vector<Vec3> group_points = probe_group_points();
  if (!group_points.empty()) {
    mix(group_points.size());
    mixf(&group_points[0].x, group_points.size() * 3);
  }
  return h ? h : 1;
}

/* Only recomputed when something the views draw or the lighting settings changed (the status bar asks
 * every frame). */
/* Round 426: Directional Mode asks for directions the shown bake doesn't have (one from before round 426, or baked
 * Non-Directional): out of date, so Auto Generate and Preview bake again. Unity's lightmaps excepted (theirs are folded
 * in). Kept out of lightmap_scene_key, whose value every saved bake holds. */
bool Editor::wants_directions() const {
  return scene_->lighting.directional_mode != 0 && !lighting_data_.empty() && lighting_data_.dir_pages.empty() && !lighting_from_unity_;
}

/* Round 455 (ADR 0119): the probes of every enabled Light Probe Group in an active object, in the world, in the scene's order. */
std::vector<Vec3> Editor::probe_group_points() {
  std::vector<Vec3> out;
  scene_->for_each([&](GameObject &g) {
    const LightProbeGroupComponent *pg = g.active_in_hierarchy() ? g.probe_group() : nullptr;
    if (!pg || !pg->enabled) return;
    const Mat4 &w = g.world_matrix();
    for (const Vec3 &p : pg->positions) {
      const Vec3 q = w.point(p);
      if (finite_bits(q.x) && finite_bits(q.y) && finite_bits(q.z)) out.push_back(q);
    }
  });
  return out;
}

/* Round 455 (ADR 0119): the window the baked probes' SH are evaluated under, from Remove Ringing and its width. Unity's own
 * probes (a volume made from its lighting file) are shown as Unity baked them: it applied its Remove Ringing then. */
void Editor::update_probe_window() {
  const LightingSettings &ls = scene_->lighting;
  const float w = finite_bits(ls.probe_dering_width) ? std::max(3.0f, std::min(16.0f, ls.probe_dering_width)) : 4.0f;
  probe_window_ = lighting_from_unity_ ? std::array<float, 3>{1.0f, 1.0f, 1.0f} : window_factors(ls.probe_dering, w);
}

bool Editor::lighting_out_of_date() {
  if (lighting_data_.empty()) return false;
  const uint64_t k = scene_render_hash() ^ hash_reflect([this](Reflector &r) { scene_->lighting.reflect(r); }) * 31 ^ lighting_gen_ * 131;
  /* During a drag, a change is taken as out of date without the exact check (the baked scene's key hashes every
   * mesh, every frame): Live GI and the status bar follow as they did; the check itself runs when it ends. */
  if (interacting() && k != ood_key_) return true;
  if (k != ood_key_) {
    ProfileScope prof{prof_.ms_lighting_key, {}};
    prof_.lightmap_key_hashes++;
    ood_key_ = k;
    ood_ = lighting_data_.scene_key != lightmap_scene_key() || wants_directions();
  }
  return ood_;
}

bool Editor::live_gi_needed(std::string *why) {
  const LightingSettings &ls = scene_->lighting;
  auto say = [&](const char *s) {
    if (why) *why = s;
  };
  if (!ls.realtime_gi) {
    say("Live GI: off (Realtime GI is off)");
    return false;
  }
  if (lighting_manual() && !lighting_update_requested_) {
    say(("Live GI: frozen (Lighting Updates: Manual; Update Lighting or " + key("lighting.update") + " refreshes it)").c_str());
    return false;
  }
  if (!ls.baked_gi || lighting_data_.empty()) {
    say("Live GI: running (nothing baked)");
    return true;
  }
  /* Voxel GI bounces the first directional light the rasterizer draws (as the shadow map; a Baked one isn't
   * drawn, task 0021): live if the bake doesn't hold it. */
  int sun_mode = -1;
  scene_->for_each([&](GameObject &g) {
    if (sun_mode >= 0 || !g.active_in_hierarchy()) return;
    if (const Light *l = g.get<Light>())
      if (l->enabled && l->type == 0 && l->mode != 2) sun_mode = l->mode;
  });
  if (sun_mode == 0) {
    say("Live GI: running (the Directional Light is Realtime, so its bounce isn't baked)");
    return true;
  }
  if (lighting_out_of_date()) {
    say("Live GI: running until the next bake (the scene changed since it)");
    return true;
  }
  say("Live GI: idle (the bake covers the scene: nothing is traced per frame)");
  return false;
}

/* Gives a drawn item its lightmap when the object was baked as it is now. */
void Editor::attach_lightmap(const GameObject &g, const Mesh &m, DrawItem &it) {
  const LightingData &ld = shown_lighting();  // the preview's while there is one (task 0020)
  if (ld.empty() || !scene_->lighting.baked_gi) return;
  auto e = ld.entries.find(g.id);
  if (e == ld.entries.end() || e->second.page < 0 || e->second.page >= (int)ld.pages.size()) return;
  if (e->second.tri_uv.size() != it.mesh->tri_count() * 3) return;
  /* Changed since the bake: realtime ambient - except in Manual, where the last lighting stays up (a moved
   * object keeps its map, lit where it was) until you update. */
  if (e->second.hash != lightmap_hash_of(g, m)) {
    if (!lighting_manual()) return;
    auto mb = lm_mesh_baked_.find(g.id);
    if (mb == lm_mesh_baked_.end() || mb->second != mesh_hash_of(m)) return;  // its mesh changed: probes or the sky
  }
  if (lm_stale_live_) return;  // lights or world changed since the bake: live GI until the next one
  it.lightmap = &ld.pages[(size_t)e->second.page];
  it.lightmap_uv = &e->second.tri_uv;
  if (scene_->lighting.directional_mode != 0 && (size_t)e->second.page < ld.dir_pages.size()) it.lightmap_dir = &ld.dir_pages[(size_t)e->second.page];  // (round 426)
  if ((size_t)e->second.page < ld.mask_pages.size()) it.shadowmask = &ld.mask_pages[(size_t)e->second.page];  // (round 444)
}

void Editor::load_lighting_data() {
  render_job_wait("the scene's lighting is replaced");  // (round 412: a render on a worker reads the lightmaps and probes)
  scene_ident_++;  // a new or opened scene: nothing published for the last one shows
  if (preview_running_) preview_baker_.cancel();
  preview_running_ = false;
  preview_data_ = LightingData{};
  preview_key_ = preview_view_key_ = preview_scene_key_ = 0;
  preview_stage_ = preview_samples_ = 0;
  lighting_update_requested_ = false;
  last_realtime_gi_ = false;  // Realtime GI on in the scene: one update when it's drawn (task 0020)
  frozen_key_ = 0;
  lm_mesh_baked_.clear();
  lighting_data_ = LightingData{};
  light_volume_bakes_.clear();  // (round 398: the scene's, loaded below)
  reflection_probe_bakes_.clear(), reflection_probe_maps_.clear(), reflection_probe_bakes_dirty_ = false, reflection_probe_gen_++;  // (review 401: never the last scene's)
  light_volume_bakes_dirty_ = false;
  probes_ = ProbeVolumeData{};
  probe_live_ready_ = false;
  probe_live_left_ = 0;
  probe_live_key_ = probe_layout_key_ = 0;
  lighting_gen_++;
  lighting_saved_dir_.clear();
  lm_mesh_hash_.clear();
  ood_key_ = 0;
  const std::string dir = lighting_dir();
  if (!dir.empty()) load_light_volume_bakes(dir);  // (round 398: their own file - review 398: also with no lightmaps)
  if (!dir.empty()) load_reflection_probe_bakes(dir);  // (round 401)
  if (dir.empty() || !fs::exists(fs::join(dir, "LightingData.bin"))) return;
  std::string err;
  if (!lighting_data_.load(dir, err)) Log::warn("Baked lighting not loaded: %s", err.c_str());
  else {
    lighting_saved_dir_ = dir;
    /* The meshes it was baked from: the scene's as they are (where they still sit where they were baked). */
    scene_->for_each([&](GameObject &g) {
      auto e = lighting_data_.entries.find(g.id);
      const Mesh *m = e != lighting_data_.entries.end() ? g.evaluated_mesh(1) : nullptr;
      if (m && e->second.hash == lightmap_hash_of(g, *m)) lm_mesh_baked_[g.id] = mesh_hash_of(*m);
    });
  }
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
  if (!lighting_dir().empty() && light_volume_bakes_dirty_) save_light_volume_bakes(lighting_dir()), light_volume_bakes_dirty_ = false;  // (review 398: on its own)
  if (!lighting_dir().empty() && reflection_probe_bakes_dirty_) save_reflection_probe_bakes(lighting_dir()), reflection_probe_bakes_dirty_ = false;  // (round 401)
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

/* What Generate Lighting (and its preview) bakes: the Contribute GI objects, copied (the scene may change
 * while it runs), and the lights with their modes. */
void Editor::collect_bake_scene(std::vector<BakeObject> &objects, std::vector<BakeLight> &lights) {
  eval_jobs_wait();  // (round 413: the workers' finished results taken first)
  blockout_jobs_wait();  // (round 477: a Blockout bakes the mesh its pieces make now)
  const GameObject::EvaluateNowScope evaluate_now;  // (the stacks' exact results, not what a worker is still making)
  objects.clear();
  lights.clear();
  /* The parts that contribute and their distinct meshes (round 412: parts sharing a mesh share its copy). */
  struct Part {
    GameObject *g;
    MeshRenderer *mr;
    size_t mesh;
  };
  struct MeshWork {
    std::shared_ptr<const Mesh> m;  // (held: a later object's evaluation can't free it under the gather - review 422)
    std::shared_ptr<const Mesh> copy;
    uint64_t hash = 0;
  };
  std::vector<Part> parts;
  std::vector<MeshWork> meshes;
  std::unordered_map<const Mesh *, size_t> mesh_index;
  ScopedTimer pass_t;
  scene_->for_each([&](GameObject &g) {
    if (!g.active_in_hierarchy()) return;
    if (auto *l = g.get<Light>(); l && l->enabled) {
      lights.push_back({to_render_light(g, *l, unity_lights_gamma()), l->mode, g.id});  // (re-check 401: the bakes as the views)
      lights.back().indirect = l->bounce_intensity;  // (round 452: Unity's Indirect Multiplier)
      lights.back().light.baked_falloff = l->mode != 0;  // (round 425: Unity's built-in pipeline bakes with the legacy falloff)
    }
    auto *mr = g.get<MeshRenderer>();
    if (!mr || !mr->enabled || !mr->contribute_gi || !mr->show_in_renders || blockout_uses_as_piece(g)) return;  // (round 480: a Blockout's mesh piece is not baked)
    const Mesh *raw = g.evaluated_mesh(1);
    if (!raw) return;
    std::shared_ptr<const Mesh> m = g.evaluated_mesh_shared(1);
    if (!m) m = std::shared_ptr<const Mesh>(std::shared_ptr<const Mesh>(), raw);  // (not held, as before round 422: never left out)
    const auto [it, fresh] = mesh_index.emplace(m.get(), meshes.size());
    if (fresh) meshes.push_back({std::move(m)});
    parts.push_back({&g, mr, it->second});
  });
  /* Round 422: each distinct mesh's work on every core - its render mesh warmed (mesh_hash_of keys its cache by the
   * render mesh's serial: building them one after another had been SideRail's 6 s starting frame), its lightmap hash
   * and the bake's own copy (without the caches - the worker builds its own; the scene may change while it runs). */
  const double eval_ms = pass_t.ms();
  JobSystem::global().parallel_for((int64_t)meshes.size(), 2, [&](int64_t a, int64_t b) {
    for (int64_t i = a; i < b; i++) {
      MeshWork &w = meshes[(size_t)i];
      const uint64_t serial = w.m->render_mesh().serial;
      const auto known = serial ? lm_mesh_hash_.find(serial) : lm_mesh_hash_.end();  // (only read here: filled below)
      w.hash = known != lm_mesh_hash_.end() ? known->second : lightmap_mesh_hash(*w.m);
      w.copy = mesh_copy_bare(*w.m);
    }
  });
  if (lm_mesh_hash_.size() > 8192) lm_mesh_hash_.clear();  // (as mesh_hash_of: old meshes' serials never come back)
  for (const MeshWork &w : meshes)
    if (const uint64_t serial = w.m->render_mesh().serial) lm_mesh_hash_[serial] = w.hash;
  const double mesh_ms = pass_t.ms() - eval_ms;
  std::vector<MaterialPtr> used;
  for (const Part &p : parts) used.insert(used.end(), p.mr->materials.begin(), p.mr->materials.end());
  prefetch_material_textures(used);  // (round 422: their files decoded on every core; freezing them finds them cached)
  std::unordered_map<const Material *, MaterialPtr> copies;
  for (const Part &p : parts) {
    const MeshWork &w = meshes[p.mesh];
    BakeObject o;
    o.id = p.g->id;
    o.mesh = w.copy;
    o.model = p.g->world_matrix();
    for (const MaterialPtr &mp : p.mr->materials) {  // (round 412: frozen copies - the worker never reads the scene's own)
      MaterialPtr &c = copies[mp.get()];
      if (!c) c = snapshot_material(mp);
      o.materials.push_back(c);
    }
    o.scale = p.mr->scale_in_lightmap;
    o.generate_uvs = p.mr->generate_lightmap_uvs;
    o.want_lightmap = p.mr->receive_gi == 0;  // Light Probes: in the scene the light bounces off, no chart
    o.hash = lightmap_placed_hash(w.hash, o.model);  // (lightmap_hash_of's value, from the hash made above)
    objects.push_back(std::move(o));
  }
  Log::info("Generate Lighting: %zu parts, %zu meshes - evaluated in %.0f ms, warmed, hashed and copied in %.0f ms, %zu materials frozen in %.0f ms",
            parts.size(), meshes.size(), eval_ms, mesh_ms, copies.size(), pass_t.ms() - eval_ms - mesh_ms);
}

/* Task 0018: on the ticked GPUs when Render > Device is GPU Compute (as F12 renders); empty: the CPU. Round 487:
 * every ticked one - Generate Lighting's gather shares its texels between them. */
std::vector<int> Editor::choose_bake_gpus() {
  const int want = scene_->lighting.bake_device;  // (round 430: Unity's Lightmapper)
  const bool on_gpu = want == 2 || (want == 0 && scene_->render.device >= 1);
  if (!on_gpu || !gpu::available()) return {};
  std::vector<int> g = enabled_gpus();
  if (bake_one_card_ && g.size() > 1) g.resize(1);  // (`lighting cards one`)
  return g;
}

/* lighting cards [all|one] (round 487): whether the next Generate Lighting's gather runs on every ticked card (all,
 * the normal bake) or on the first one only (one - what bakes did before round 487), for measuring what the other
 * cards save. The lightmaps are the same either way, so it is not saved and not in the scene's key. Alone it says
 * the setting. */
void Editor::lighting_cards_command(const std::string &word) {
  const char *now = bake_one_card_ ? "one" : "all";
  if (!word.empty() && word != "all" && word != "one")
    return Log::warn("lighting cards: say all or one, not '%s' - it stays %s", word.c_str(), now);
  if (!word.empty()) bake_one_card_ = word == "one";
  if (bake_one_card_)
    return Log::info("lighting cards: one (the next Generate Lighting gathers on the first ticked card only; `lighting cards all` restores the normal bake)");
  Log::info("lighting cards: all (Generate Lighting's gather on every ticked card - the normal bake)");
}

/* The first of them: what works on one card (Preview Lighting, which steps on the main thread; the probes); -1 the CPU. */
int Editor::choose_bake_gpu() {
  const std::vector<int> g = choose_bake_gpus();
  return g.empty() ? -1 : g[0];
}

/* lighting pick [on|off|<rays>] (round 484): how the next Generate Lighting treats the Baked point and spot lights when
 * there are more than eight - about <rays> of them kept a texel by chance (on: 8, the bake's own number), or every one
 * at every texel (off). For measuring what the picking costs in noise and saves in time: both are unbiased, so it is
 * not saved and not in the scene's key. Alone it says the setting. Preview Lighting builds its own settings and the
 * light probes pick on their own: both keep 8. */
void Editor::lighting_pick_command(const std::string &word) {
  const float normal = BakeSettings().pick_rays;
  auto say = [&] {
    if (bake_pick_rays_ <= 0.0f)
      return Log::info("lighting pick: off (the next Generate Lighting sends every Baked point and spot light's shadow rays at every texel; Preview Lighting keeps 8)");
    Log::info("lighting pick: about %g lights a texel%s", bake_pick_rays_,
              bake_pick_rays_ == normal ? " (the normal bake)" : " (the next Generate Lighting; Preview Lighting keeps 8; `lighting pick on` restores the normal 8)");
  };
  if (word.empty()) return say();
  if (word == "on") return bake_pick_rays_ = normal, say();
  if (word == "off") return bake_pick_rays_ = 0.0f, say();
  /* Digits and at most one point, from 1 to 4096: strtof alone would read "8x" as 8 and "-3" as a number. */
  const bool number = word.size() <= 8 && word.find_first_not_of("0123456789.") == std::string::npos && std::count(word.begin(), word.end(), '.') <= 1 &&
                      word.find_first_of("0123456789") != std::string::npos;
  const float v = number ? std::strtof(word.c_str(), nullptr) : 0.0f;
  if (!number || !(v >= 1.0f) || v > 4096.0f)
    return Log::warn("lighting pick: say on, off or a number of lights from 1 to 4096 - it stays %s", bake_pick_rays_ > 0.0f ? strprintf("%g", bake_pick_rays_).c_str() : "off");
  bake_pick_rays_ = v;
  say();
}

/* lighting variancescale [auto|on|off] (round 486, task 0462): how the next Generate Lighting's filter reads the noise
 * the bake measured in the gathered light. Round 465 scaled it for Sobol samples (by 4 over the square root of the
 * samples); auto, the bake's own, keeps that scale only as far as the texel's samples carry it (a texel lit by a few
 * rare bright samples gets none: what specks are made of); on is round 465's scale everywhere (the bake of rounds
 * 465 to 485), off none at all - both for comparisons. The round's other measured candidates answer too (clamp, snr,
 * step). The gathered light is the same whatever the word, so it is not saved and not in the scene's key. Alone it
 * says the setting. */
namespace {
/* `lighting variancescale`'s words and what each sets BakeSettings::variance_scale to (-1: the bake's own). */
const struct {
  const char *name;
  int mode;
} kVarianceScaleWords[] = {{"auto", -1}, {"on", 0}, {"clamp", 1}, {"off", 2}, {"snr", 3}, {"step", 4}};

const char *variance_scale_name(int mode) {
  for (const auto &w : kVarianceScaleWords)
    if (w.mode == mode) return w.name;
  return "auto";
}
}  // namespace

void Editor::lighting_variancescale_command(const std::string &word) {
  auto say = [&] {
    Log::info("lighting variancescale: %s%s", variance_scale_name(bake_variance_scale_),
              bake_variance_scale_ < 0 ? " (the normal bake)" : " (the next Generate Lighting; Preview Lighting keeps auto; `lighting variancescale auto` restores the normal bake)");
  };
  if (word.empty()) return say();
  for (const auto &w : kVarianceScaleWords)
    if (word == w.name) return bake_variance_scale_ = w.mode, say();
  Log::warn("lighting variancescale: say auto, on or off - it stays %s", variance_scale_name(bake_variance_scale_));
}

void Editor::bake_start() {
  if (baking_) return;
  if (unity_lighting_job_) return refuse("Generate Lighting: not while Unity reads its lightmaps for this scene (they'd land in the middle of the bake)", "", "");  // (review 400)
  if (playing_) {
    refuse("Generate Lighting: not while playing.", "stop", "stop Play");
    return;
  }
  const LightingSettings &ls = scene_->lighting;
  if (!ls.baked_gi) {
    refuse("Generate Lighting: Baked Global Illumination is off (Lighting window).", "window Lighting", "Open Lighting");
    return;
  }
  if (bake_job_) {  // (a cancelled bake still finishing its slice: waited for, its finished probes kept)
    std::unique_ptr<BakeJob> old = std::move(bake_job_);
    old->cancel.store(true);
    if (old->thread.joinable()) old->thread.join();
    bake_job_land(*old);
  }
  /* Round 412 (ADR 0106): the scene as the bake reads it - the meshes copied, the lights, the world, the probes' box,
   * the Light Volumes' grids and the Reflection Probes' places - gathered here; the worker bakes it all. */
  auto job = std::make_unique<BakeJob>();
  ScopedTimer start_t;
  double part_ms[5] = {0, 0, 0, 0, 0};  // (round 422: the starting frame's parts - collect, environment, GPU, keys and boxes, volumes and probes)
  double part_at = 0;
  auto part = [&](int k) {
    const double now = start_t.ms();
    part_ms[k] = now - part_at, part_at = now;
  };
  collect_bake_scene(job->objects, job->lights);
  if (scene_->lighting.lighting_mode == 2) subtractive_split(job->lights);  // (round 444: Unity's Subtractive split)
  if (scene_->lighting.lighting_mode == 1) shadowmask_split(job->lights);  // (round 454: a fifth Mixed light is Baked for every baker)
  part(0);
  /* Round 362 (the comparison's finding 10): nothing marked had baked "0 objects" without a word of why. */
  if (job->objects.empty()) return bake_refuse_empty();
  if (preview_running_) {  // the real bake replaces the preview
    preview_baker_.cancel();
    preview_running_ = false;
  }
  update_environment();
  job->env = env_;
  part(1);
  BakeSettings &s = job->settings;
  s.texels_per_unit = ls.texels_per_unit;
  s.max_size = ls.max_size;
  s.padding = ls.padding;
  s.direct_samples = ls.direct_samples;
  s.indirect_samples = ls.indirect_samples;
  s.bounces = ls.bounces;
  s.denoise = ls.denoise;
  s.indirect_intensity = ls.indirect_intensity;
  s.directional = ls.directional_mode != 0;  // (round 426)
  s.lighting_mode = ls.lighting_mode;  // (round 444)
  s.backface_tolerance = finite_bits(ls.backface_tolerance) ? std::max(0.0f, std::min(1.0f, ls.backface_tolerance)) : 0.9f;  // (round 430)
  s.sobol = bake_sobol_;  // (round 465: unbiased either way, so not in the scene key)
  s.denoiser = bake_variance_denoiser_ ? 1 : 0;  // (round 466: filtering happens at the end - not in the scene key)
  s.light_tree = bake_light_tree_;  // (round 464: unbiased either way, so not in the scene key)
  s.seed = bake_seed_;  // (round 463: the texels, the card's gather and the probes all draw from it)
  if (s.seed) Log::info("Generate Lighting: seed %u (`lighting seed 0` for the normal bake)", s.seed);
  const float normal_pick = BakeSettings().pick_rays;
  s.pick_lights = bake_pick_rays_ > 0.0f;  // (round 484: unbiased either way, so not in the scene key)
  s.pick_rays = s.pick_lights ? bake_pick_rays_ : normal_pick;
  job->pick_rays = bake_pick_rays_;
  if (bake_pick_rays_ != normal_pick)
    Log::info("Generate Lighting: light picking %s (`lighting pick on` for the normal bake)",
              s.pick_lights ? strprintf("at about %g lights a texel", bake_pick_rays_).c_str() : "off - every Baked point and spot light at every texel");
  if (bake_variance_scale_ >= 0) {  // (round 486: what the filter reads - the gathered light is the same either way, so not in the scene key)
    s.variance_scale = bake_variance_scale_;
    Log::info("Generate Lighting: the gather's measured noise read as `lighting variancescale %s` (`lighting variancescale auto` for the normal bake)",
              variance_scale_name(bake_variance_scale_));
  }
  s.gpu_devices = choose_bake_gpus();  // (round 487: the gather on every ticked card)
  s.gpu_batch_by_speed = true;  // (the bake has its own thread: batches as large as keep the cards working)
  bake_gpu_ = s.gpu_devices.empty() ? -1 : s.gpu_devices[0];  // (the first of them: the probes' and the reflection probes' card)
  s.gpu_device = bake_gpu_;
  job->gpu = bake_gpu_;
  part(2);
  job->key = bake_key_ = lightmap_scene_key();
  job->probe_box_ok = probe_volume_box(job->probe_box);
  job->group_points = probe_group_points();  // (round 455, ADR 0119: hand-placed probes replace the grid)
  if (!job->group_points.empty() && job->probe_box_ok) Log::info("Generate Lighting: Light Probe Groups bake the probes; the Probe Volume component is ignored");
  job->probe_min_spacing = ls.probe_min_spacing, job->probe_max_spacing = ls.probe_max_spacing;
  job->indirect_samples = ls.indirect_samples, job->bounces = ls.bounces, job->indirect_intensity = ls.indirect_intensity;
  part(3);
  bake_gather_volumes(*job);
  bake_gather_reflection_probes(*job);
  part(4);
  last_bake_start_ms_ = start_t.ms();
  Log::info("Generate Lighting: started in %.0f ms (the scene gathered %.0f, the world %.0f, the device %.0f, keys %.0f, volumes and probes %.0f)",
            last_bake_start_ms_, part_ms[0], part_ms[1], part_ms[2], part_ms[3], part_ms[4]);
  job->save_dir = lighting_dir();  // (the worker writes the lightmaps and probes beside it when it is done: not the landing frame's work)
  job->scene_ident = scene_ident_;
  bake_start_ = now_seconds();
  BakeJob *raw = job.get();
  bake_job_ = std::move(job);
  baking_ = true;
  bake_status_ = "Baking: laying out the lightmaps...";
  auto run = [this, raw] {
    JobSystem::global().run_background([this, raw] { bake_job_run(*raw); });
    raw->done.store(true, std::memory_order_release);
  };
  if (!jobs_async()) {  // (a headless editor that didn't ask for workers: the whole bake now, landed by this frame's step_bake)
    run();
    return;
  }
  raw->thread = std::thread(run);
  Log::info("Generate Lighting: %zu objects on a worker (the tray's Cancel stops it)", raw->objects.size());
}

/* The worker: the lightmaps (the layout, the texels, the filter and dilation), the probe volume (placed, checked for
 * geometry, baked in slices), the Light Volumes and the Reflection Probes, in that order; its status and progress
 * published for the tray; a cancel ends it at the next slice. */
void Editor::bake_job_run(BakeJob &j) {
  ScopedTimer whole;
  double phase_start = 0;
  auto phase_done = [&](int k) {  // (round 421: each phase's time, for the log, the comparison tool and the tests)
    const double now = whole.ms();
    j.phase_ms[k] = now - phase_start;
    phase_start = now;
  };
  auto say = [&](const std::string &text) {
    std::lock_guard<std::mutex> lock(j.mutex);
    j.status = text;
  };
  baker_.begin(j.objects, j.lights, j.env, j.settings);
  Log::info("Generate Lighting: %zu objects, %zu texels", j.objects.size(), baker_.texel_count());
  while (!j.cancel.load(std::memory_order_relaxed)) {
    const bool done = baker_.step(200.0);
    j.progress.store(baker_.progress());
    say(strprintf("Baking lighting %.0f%%  (%.1f s)", baker_.progress() * 100.0, whole.ms() / 1000.0));
    if (done) break;
  }
  if (j.cancel.load()) {
    baker_.cancel();
    return;
  }
  j.lighting = baker_.take_result();
  j.lighting.scene_key = j.key;
  j.lightmap_finish_ms = baker_.finish_ms(), j.lightmap_texels = baker_.texel_count(), j.lightmap_rays = baker_.rays(), j.lightmap_gpu_texels = baker_.gpu_texels();
  j.lightmap_gpu_cards = baker_.gpu_cards();  // (round 487)
  j.lightmap_pick_texels = baker_.pick_texels(), j.lightmap_chance_kept = baker_.chance_kept(), j.lightmap_chance_dropped = baker_.chance_dropped();  // (round 484)
  j.lightmap_denoiser = baker_.denoiser();
  j.mesh_hashes.assign(j.objects.size(), 0);
  JobSystem::global().parallel_for((int64_t)j.objects.size(), 4, [&](int64_t a, int64_t b) {
    for (int64_t i = a; i < b; i++) j.mesh_hashes[(size_t)i] = lightmap_mesh_hash(*j.objects[(size_t)i].mesh);
  });
  phase_done(0);
  /* The probe volume (task 0014): placed from the scene the lightmaps were baked from, checked for geometry, baked. */
  j.phase.store(1);
  j.progress.store(-1.0f);
  const bool grouped = !j.group_points.empty();
  if (j.probe_box_ok || grouped) {
    std::vector<AABB> tris;
    std::vector<PTObject> objs;
    for (const BakeObject &o : j.objects) {
      const RenderMesh &rm = o.mesh->render_mesh_tangents();
      for (size_t k = 0; k < rm.tri_count(); k++) {
        AABB b;
        for (int c = 0; c < 3; c++) b.add(o.model.point(rm.positions[rm.indices[k * 3 + c]]));
        tris.push_back(b);
      }
      objs.push_back({&rm, o.model, &o.materials});
    }
    /* Round 455: with Light Probe Groups the probes are theirs, where they were put (Unity moves none); baked as a list,
     * then a grid over their tetrahedra (probe_volume_from_points) is what the views read. */
    ProbeVolumeData placed;
    ProbeVolumeData &pv = grouped ? placed : j.probe_volume;
    if (grouped) {
      pv.position = j.group_points;
      pv.valid.assign(pv.position.size(), 1);
      pv.baked.assign(pv.position.size(), ProbeCube{});
    }
    else probe_place(pv, j.probe_box, tris, j.probe_min_spacing, j.probe_max_spacing);
    if (pv.probe_count() > 0) {
      PathTracer pt;
      PTSettings ps;
      ps.max_bounces = std::max(0, j.bounces - 1);
      ps.denoise = false;
      ps.use_guiding = false;
      pt.set_settings(ps);
      const std::vector<RenderLight> lights = bounce_lights(j.lights);  // (round 452: at their Indirect Multipliers)
      pt.build(objs, lights, j.env);
      say("Baking probes: checking for geometry...");
      if (!grouped) probe_validate(pv, pt, 64);
      const size_t n = pv.probe_count();
      const int psamples = std::max(24, std::min(4096, j.indirect_samples / 4));
      int gpu = j.gpu;
      size_t next = 0;
      while (next < n && !j.cancel.load(std::memory_order_relaxed)) {
        /* On a GPU about 8M rays a slice (their points go up at once: memory), on the CPU 1,024 probes. */
        const size_t slice = std::min<size_t>(n - next, gpu >= 0 ? std::max<size_t>(1, ((size_t)8 << 20) / (size_t)psamples) : 1024);
        std::string gerr;
        probe_bake(pv, pt, j.lights, psamples, j.bounces, next, slice, j.indirect_intensity, gpu, &gerr, &j.env, j.settings.seed);
        if (!gerr.empty()) {
          Log::warn("Baking probes on the GPU stopped (%s): the CPU bakes the rest", gerr.c_str());
          gpu = -1;
        }
        next += slice;
        j.progress.store((float)next / (float)std::max<size_t>(1, n));
        say(strprintf("Baking probes %.0f%%  (%.1f s)", 100.0 * next / std::max<size_t>(1, n), whole.ms() / 1000.0));
      }
      if (j.cancel.load()) return;
      if (grouped) {
        probe_volume_from_points(pv.position, pv.sh, pv.baked, j.probe_volume);
        j.probe_volume.sky_in_sh = pv.sky_in_sh, j.probe_volume.has_baked_sky = pv.has_baked_sky, j.probe_volume.baked_sky = pv.baked_sky;
      }
      j.probe_volume.scene_key = j.key;  // (the last slice dilated the invalid probes)
      for (uint8_t v : j.probe_volume.valid) j.probes_valid += v;
      const size_t total = j.probe_volume.probe_count();
      if (grouped) Log::info("Probe volume: %zu placed probes baked, %zu grid probes over their tetrahedra, %zu bricks", n, total, j.probe_volume.bricks.size());
      else Log::info("Probe volume: %zu probes (%zu inside geometry), %zu bricks", n, n - j.probes_valid, j.probe_volume.bricks.size());
    }
  }
  phase_done(1);
  j.phase.store(2);
  j.progress.store(-1.0f);
  bake_volumes_on_worker(j);  // (round 398: the Light Volumes Blendity bakes)
  if (j.cancel.load()) return;
  phase_done(2);
  j.phase.store(3);
  bake_reflection_probes_on_worker(j);  // (round 401: the Reflection Probes, their six faces each)
  phase_done(3);
  if (!j.cancel.load() && !j.save_dir.empty()) {
    /* Written into a folder beside the scene's (<name>.baking) and moved over when the bake lands, so a save on the
     * main thread meanwhile never writes the same files, and a Save As meanwhile leaves nothing in the old folder. */
    say("Saving the lightmaps...");
    std::string err;
    j.saved_tmp = j.save_dir + ".baking";
    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::u8path(j.saved_tmp), ec);
    fs::make_dirs(j.saved_tmp);
    const bool baked_probes = !j.probe_volume.empty() && j.probe_volume.scene_key != 0;
    j.saved = j.lighting.save(j.saved_tmp, err) && (!baked_probes || j.probe_volume.save(fs::join(j.saved_tmp, "ProbeVolume.bin"), err));
    if (!j.saved) Log::error("Could not save the baked lighting: %s", err.c_str());
  }
  phase_done(4);
  j.ms = phase_start;  // (the last phase's end: the phases add up to the whole by construction)
  j.phase.store(4);
}

void Editor::bake_cancel(bool discard) {
  if (bake_job_) {  // (the worker stops at its next slice; step_bake drops what it had, keeping the volumes and reflection probes it finished - unless discarded)
    bake_job_->cancel.store(true);
    if (discard) bake_job_->discard = true;
  }
  if (!baking_) {
    if (!bake_job_) baker_.cancel();  // nothing left behind either way
    return;
  }
  baking_ = false;
  bake_status_ = "Baking cancelled";
  Log::info("Generate Lighting cancelled");
}

void Editor::bake_clear() {
  bake_cancel(true);  // (a job still finishing lands nothing: the data is being cleared)
  render_job_wait("Clear Baked Data");  // (round 412: a render on a worker reads the lightmaps and probes about to go)
  light_volume_bakes_.clear();  // (review 398: the volumes' bakes go with the rest)
  livelink_light_volume_bakes_changed();
  reflection_probe_bakes_.clear(), reflection_probe_bakes_changed();  // (round 401)
  lighting_data_ = LightingData{};
  lighting_from_unity_ = false;  // (round 400)
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

/* Each frame: the worker's status; when it is done, its results land as one change (a headless editor that didn't ask
 * for workers waits for it here, so the bake lands in the frame that started it, as before round 412). */
void Editor::step_bake() {
  if (!bake_job_) {
    baking_ = false;
    return;
  }
  BakeJob *j = bake_job_.get();
  if (!jobs_async() && !j->done.load(std::memory_order_acquire) && j->thread.joinable()) j->thread.join();
  {
    std::lock_guard<std::mutex> lock(j->mutex);
    if (!j->status.empty() && baking_) bake_status_ = j->status;
  }
  if (!j->done.load(std::memory_order_acquire)) return;
  if (render_job_holds_lighting()) return;  // (lands next frame: an F12 render on a worker reads the lightmaps it would replace)
  std::unique_ptr<BakeJob> job = std::move(bake_job_);
  if (job->thread.joinable()) job->thread.join();
  bake_job_land(*job);
}

/* The results into the scene: the lightmaps and the probe volume (not when cancelled), the Light Volumes and the
 * Reflection Probes it finished (kept even then - review 401), the stale volume bakes dropped (review 398); saved. */
void Editor::bake_job_land(BakeJob &j) {
  ScopedTimer land_t;  // (round 421: the frame that lands a bake, timed - with its phases, said once it has landed)
  struct LandTimer {
    Editor &ed;
    BakeJob &j;
    ScopedTimer &t;
    ~LandTimer() {
      if (j.cancel.load() || j.discard || j.scene_ident != ed.scene_ident_) return;  // (nothing landed: the last times stay)
      BakeTimes &b = ed.last_bake_times_;
      for (int k = 0; k < 5; k++) b.phase_ms[k] = j.phase_ms[k];
      b.finish_ms = j.lightmap_finish_ms, b.land_ms = t.ms(), b.total_ms = j.ms;
      b.texels = j.lightmap_texels, b.gpu_texels = j.lightmap_gpu_texels, b.rays = j.lightmap_rays;
      b.gpu_cards = j.lightmap_gpu_cards;
      if (!b.gpu_cards.empty())  // (round 487: which cards gathered, and how the texels were shared between them)
        Log::info("Generate Lighting: the gather ran on %s", b.gpu_cards.c_str());
      Log::info("Generate Lighting phases: lightmaps %.1f s (%zu texels, %.0f Mrays, %zu on the GPU; %s and dilation %.0f ms), probes %.1f s, "
                "Light Volumes %.1f s, reflection probes %.1f s, saving %.1f s; the landing frame %.0f ms",
                b.phase_ms[0] / 1000.0, b.texels, b.rays / 1e6, b.gpu_texels, j.lightmap_denoiser.empty() ? "no filter" : j.lightmap_denoiser.c_str(), b.finish_ms,
                b.phase_ms[1] / 1000.0, b.phase_ms[2] / 1000.0, b.phase_ms[3] / 1000.0, b.phase_ms[4] / 1000.0, b.land_ms);
      /* Round 484: what the light picking did, for `lighting pick`'s comparisons (light_pick.h). */
      b.pick_texels = j.lightmap_pick_texels, b.chance_kept = j.lightmap_chance_kept, b.chance_dropped = j.lightmap_chance_dropped;
      if (b.pick_texels)
        Log::info("Generate Lighting: light picking at about %g lights a texel - %.2f kept by chance and %.2f dropped a texel, over %llu texels", j.pick_rays,
                  (double)b.chance_kept / (double)b.pick_texels, (double)b.chance_dropped / (double)b.pick_texels, (unsigned long long)b.pick_texels);
    }
  } land_timer{*this, j, land_t};
  const bool cancelled = j.cancel.load();
  std::error_code ec;
  if (j.discard || j.scene_ident != scene_ident_) {  // (Clear Baked Data, or another scene since: nothing of it lands - review 412)
    if (!j.saved_tmp.empty()) std::filesystem::remove_all(std::filesystem::u8path(j.saved_tmp), ec);
    baking_ = false;
    ui_.redraw = true;
    return;
  }
  if (!j.probe_bakes.empty()) {
    for (auto &kv : j.probe_bakes) reflection_probe_bakes_[kv.first] = std::move(kv.second);
    land_reflection_probe_maps(j.probe_maps);
  }
  bool volumes_changed = !j.volume_bakes.empty();
  for (auto &kv : j.volume_bakes) light_volume_bakes_[kv.first] = std::move(kv.second);
  if (!cancelled)
    for (auto it = light_volume_bakes_.begin(); it != light_volume_bakes_.end();) {
      if (!it->second.from_unity && !j.volume_ids.count(it->first)) {
        it = light_volume_bakes_.erase(it);
        volumes_changed = true;
      }
      else ++it;
    }
  if (volumes_changed) livelink_light_volume_bakes_changed();
  if (cancelled) {
    baking_ = false;
    ui_.redraw = true;
    return;
  }
  lighting_data_ = std::move(j.lighting);
  lighting_from_unity_ = false;  // (round 400: Blendity's own bake in Unity's place)
  lm_mesh_baked_.clear();
  for (size_t i = 0; i < j.objects.size(); i++)
    lm_mesh_baked_[j.objects[i].id] = i < j.mesh_hashes.size() ? j.mesh_hashes[i] : lightmap_mesh_hash(*j.objects[i].mesh);
  probes_ = std::move(j.probe_volume);
  probe_live_ready_ = false;
  probe_live_left_ = 0;
  probe_live_key_ = probe_layout_key_ = 0;
  probe_box_key_ = 0;
  baking_ = false;
  lighting_gen_++;
  const double secs = j.ms / 1000.0;
  bake_status_ = strprintf("Baked %zu lightmap%s in %.1f s", lighting_data_.pages.size(), lighting_data_.pages.size() == 1 ? "" : "s", secs);
  /* (round 487: which cards gathered, and their shares - said only when more than one did: a bake on one card reads as it always did) */
  if (j.lightmap_gpu_cards.find(", ") != std::string::npos) bake_status_ += " - the gather on " + j.lightmap_gpu_cards;
  lighting_saved_dir_.clear();
  Log::info("Generate Lighting: done in %.1f s", secs);
  if (j.saved && j.save_dir == lighting_dir()) {  // (the worker's files moved over: the scene's folder is still the one it was)
    fs::make_dirs(j.save_dir);
    bool moved = true;
    for (const DirEntry &de : fs::list(j.saved_tmp)) {
      const std::string to = fs::join(j.save_dir, de.name);
      std::filesystem::remove(std::filesystem::u8path(to), ec);
      std::filesystem::rename(std::filesystem::u8path(fs::join(j.saved_tmp, de.name)), std::filesystem::u8path(to), ec);
      if (ec) moved = false;
    }
    if (probes_.empty() || probes_.scene_key == 0) std::filesystem::remove(std::filesystem::u8path(fs::join(j.save_dir, "ProbeVolume.bin")), ec);  // an older bake's probes must not come back
    std::filesystem::remove_all(std::filesystem::u8path(j.saved_tmp), ec);
    if (moved) {
      lighting_saved_dir_ = j.save_dir;
      project_listed_ = -100;
      Log::info("Saved the baked lighting to %s", j.save_dir.c_str());
    }
  }
  else if (!j.saved_tmp.empty()) std::filesystem::remove_all(std::filesystem::u8path(j.saved_tmp), ec);  // (the scene moved meanwhile: save_lighting_data writes them where it is now)
  if (lighting_dir().empty()) Log::info("Save the scene to keep its lightmaps: they are written next to the scene file.");
  else save_lighting_data();  // (the volumes' and probes' files; the lightmaps again only if the scene moved meanwhile)
  ui_.redraw = true;
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
      if (!mr || !mr->enabled || blockout_uses_as_piece(g) || !g.evaluated_mesh(1)) return;
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

/* Realtime GI keeps the probes live: once per frame, a slice of them gathers again through the voxel grid
 * whenever the grid, the sun or the world changed. Without a bake the probes are placed here (around the
 * objects' bounds) and are live only. */
void Editor::update_probes_live() {
  if (render_job_holds_lighting()) return;  // (round 412: an F12 render on a worker reads the probes: left as they are until it is done)
  const LightingSettings &ls = scene_->lighting;
  const VoxelGIState &vg = vgi_[1].grid.valid() ? vgi_[1] : vgi_[0];
  if (!live_gi_needed() || !vg.grid.valid()) {
    probe_live_left_ = 0;
    return;
  }
  if (probes_.empty() || probes_.scene_key == 0) {  // live-only probes: placed around the objects
    AABB vol;
    if (!probe_volume_box(vol)) {
      probes_ = ProbeVolumeData{};
      return;
    }
    uint64_t key = kFnvBasis;
    const float f[8] = {vol.min.x, vol.min.y, vol.min.z, vol.max.x, vol.max.y, vol.max.z, ls.probe_min_spacing, ls.probe_max_spacing};
    for (float v : f) {
      uint32_t b;
      std::memcpy(&b, &v, 4);
      key = (key ^ b) * kFnvPrime;
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
  uint64_t key = kFnvBasis;
  for (const VoxelGIState &st : vgi_) key = (key ^ (st.grid.key + st.assembled * 31 + st.rsm_key * 131)) * kFnvPrime;
  key ^= (hash_reflect([this](Reflector &r) { scene_->environment.reflect(r); }) ^ env_key_ * 31) * 1031;  // (a Sky follows the sun)
  const float gp[7] = {ls.gi_radius, ls.gi_intensity, (float)ls.gi_rays, (float)ls.gi_resolution, (float)ls.gi_bounce, (float)ls.gi_sky_occlusion,
                       (float)ls.gi_device};
  for (float v : gp) {
    uint32_t b;
    std::memcpy(&b, &v, 4);
    key = (key ^ b) * kFnvPrime;
  }
  if (key != probe_live_key_) {
    probe_live_key_ = key;
    probe_live_left_ = probes_.probe_count();
  }
  if (probe_live_left_ == 0) return;
  /* On the GPU the whole volume in one frame; on the CPU 4,096 probes a frame. */
  VoxelGiGpu *gpu = gi_gpu(vgi_[&vg - vgi_]);
  const size_t budget = gpu ? probe_live_left_ : std::min<size_t>(probe_live_left_, 4096);
  std::string gerr;
  probe_cursor_ = probe_live_update(probes_, vg.grid, vg.rsm.valid() ? &vg.rsm : nullptr, env_, vg.frame.params, probe_cursor_, budget, gpu, &gerr);
  if (!gerr.empty()) {
    Log::warn("Live probes on the GPU stopped (%s): gathering on the CPU", gerr.c_str());
    gi_gpu_failed_ = true;
  }
  probe_live_left_ -= budget;
  probe_live_gen_++;
  if (probe_live_left_ == 0) probe_live_ready_ = true;
  ui_.redraw = true;
}

/* Task 0018: GI Device Auto uses the GPU when there is one (BLENDITY_GI_DEVICE=cpu keeps Auto on the CPU:
 * the unit tests, so this machine and CI's agree); GPU without one, or after an error, is the CPU. */
VoxelGiGpu *Editor::gi_gpu(VoxelGIState &st) {
  const int dev = scene_->lighting.gi_device;
  if (dev != gi_gpu_device_seen_) {  // choosing a device again tries the GPU again after an error
    gi_gpu_device_seen_ = dev;
    gi_gpu_failed_ = false;
  }
  if (dev == 1 || gi_gpu_failed_) return nullptr;
  if (dev == 0) {
    const char *e = std::getenv("BLENDITY_GI_DEVICE");
    if (e && to_lower(e) == "cpu") return nullptr;
  }
  if (!gpu::available() || gpu::devices().empty()) return nullptr;
  std::string err;
  if (!st.gpu) {
    st.gpu = std::make_unique<VoxelGiGpu>();
    if (!st.gpu->init(gpu::devices()[0].index, &err)) {
      Log::warn("Realtime GI can't use the GPU (%s): gathering on the CPU", err.c_str());
      gi_gpu_failed_ = true;
      st.gpu.reset();
      return nullptr;
    }
    gi_gpu_name_ = gpu::devices()[0].name;
  }
  /* env_key_: update_environment()'s key, which covers the sun when the world is a Sky (it's drawn from it). */
  const uint64_t env_hash = (hash_reflect([this](Reflector &r) { scene_->environment.reflect(r); }) ^ env_key_ * 31) | 1;
  if (!st.gpu->set_grid(st.grid, (st.grid.key * 31 + st.assembled) | 1, &err) ||
      !st.gpu->set_rsm(st.rsm.valid() ? &st.rsm : nullptr, st.rsm.valid() ? (st.rsm_key | 1) : 2, &err) || !st.gpu->set_environment(env_, env_hash, &err)) {
    Log::warn("Realtime GI on the GPU stopped (%s): gathering on the CPU", err.c_str());
    gi_gpu_failed_ = true;
    return nullptr;
  }
  return st.gpu.get();
}

/* Realtime lightmaps (task 0016): once per frame, each kind of view's texels gather again through its voxel
 * grid, a slice at a time, whenever the grid, the sun, the world or the GI settings changed. A finished
 * pass is published and the views redraw with it. */
void Editor::update_realtime_lightmaps() {
  rtlm_busy_ = false;
  if (!live_gi_needed()) return;
  const LightingSettings &ls = scene_->lighting;
  const uint64_t env_hash = hash_reflect([this](Reflector &r) { scene_->environment.reflect(r); }) ^ env_key_ * 31;  // (a Sky follows the sun)
  for (VoxelGIState &st : vgi_) {
    if (!st.grid.valid() || st.rtlm.layout_key == 0) continue;
    uint64_t key = (st.grid.key + st.assembled * 31 + st.rsm_key * 131) * kFnvPrime ^ env_hash * 1031 ^ (uint64_t)ls.gi_device << 60;  // (a new device gathers again)
    const float gp[6] = {ls.gi_radius, ls.gi_intensity, (float)ls.gi_rays, (float)ls.gi_resolution, (float)ls.gi_bounce, (float)ls.gi_sky_occlusion};
    for (float v : gp) {
      uint32_t b;
      std::memcpy(&b, &v, 4);
      key = (key ^ b) * kFnvPrime;
    }
    VoxelGiGpu *gpu = st.rtlm.pass_done && key == st.rtlm.gather_key ? nullptr : gi_gpu(st);
    std::string gerr;
    if (realtime_lightmap_update(st.rtlm, st.grid, st.rsm.valid() ? &st.rsm : nullptr, env_, st.frame.params, key, 16384, gpu, &gerr)) ui_.redraw = true;
    if (!gerr.empty()) {
      Log::warn("Realtime GI on the GPU stopped (%s): gathering on the CPU", gerr.c_str());
      gi_gpu_failed_ = true;
    }
    rtlm_busy_ = rtlm_busy_ || !st.rtlm.pass_done;
  }
}

/* Auto Generate (Unity's): once the lighting is out of date and has stopped changing for a second,
 * bake again. Until then realtime GI (if on) keeps the picture right. */
void Editor::auto_generate_step() {
  const LightingSettings &ls = scene_->lighting;
  if (!ls.auto_generate || !ls.baked_gi || baking_ || playing_ || lighting_data_.empty() || lighting_manual()) {
    settle_since_ = 0;
    return;
  }
  if (!lighting_out_of_date()) {
    settle_since_ = 0;
    return;
  }
  const double now = clock_now();  // (the test clock when a test sets one: a slow build's frames no longer pass the second)
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

/* Task 0020: Update Lighting. Realtime GI runs once - the grid, the RSM, the realtime lightmaps and the live
 * probes - and freezes again when they have all caught up (a frame on a GPU, a few on the CPU). */
void Editor::lighting_update() {
  if (!scene_->lighting.realtime_gi) {
    refuse("Update Lighting: Realtime GI is off (Lighting window); Generate Lighting bakes.", "set Lighting.RealtimeGI true", "Turn Realtime GI on");
    return;
  }
  lighting_update_requested_ = true;
  lighting_update_frames_ = 0;
  lighting_gen_++;  // the views render again, building this frame's grid and RSM
  ui_.redraw = true;
}

/* What frozen Realtime GI depends on: every drawn object's place and shape, the lights, the world and the
 * GI settings. */
uint64_t Editor::frozen_lighting_key() {
  uint64_t h = kFnvBasis;
  auto mix = [&](const void *p, size_t n) { h = fnv1a(p, n, h); };
  scene_->for_each([&](GameObject &g) {
    if (!g.active_in_hierarchy()) return;
    const auto *mr = g.get<MeshRenderer>();
    const Light *l = g.get<Light>();
    if (mr && blockout_uses_as_piece(g)) mr = nullptr;  // (round 480: a Blockout's mesh piece is no part of the baked scene)
    if (!(mr && mr->enabled) && !(l && l->enabled)) return;
    mix(&g.id, 8);
    mix(g.world_matrix().m, sizeof(float) * 16);
    if (mr && mr->enabled) {
      const uint64_t k = g.evaluated_key(1);
      mix(&k, 8);
      mix(&mr->contribute_gi, 1);
    }
    if (l && l->enabled) {
      const uint64_t lh = hash_component(*const_cast<Light *>(l));
      mix(&lh, 8);
    }
  });
  const uint64_t w = hash_reflect([this](Reflector &r) { scene_->environment.reflect(r); });
  mix(&w, 8);
  const LightingSettings &ls = scene_->lighting;
  const float gp[7] = {ls.gi_radius, ls.gi_intensity, (float)ls.gi_rays, (float)ls.gi_resolution, (float)ls.gi_bounce, (float)ls.gi_sky_occlusion,
                       ls.realtime_resolution};
  mix(gp, sizeof(gp));
  return h ? h : 1;
}

/* Manual with Realtime GI: the scene changed since the last Update Lighting (the status bar says so). */
bool Editor::frozen_out_of_date() {
  if (!scene_->lighting.realtime_gi || !lighting_manual() || lighting_update_requested_ || frozen_key_ == 0) return false;
  return frozen_lighting_key() != frozen_key_;
}

void Editor::lighting_update_step() {
  const bool rgi = scene_->lighting.realtime_gi;
  if (rgi && !last_realtime_gi_ && lighting_manual() && !lighting_update_requested_) lighting_update();  // turned on, or a scene opened
  last_realtime_gi_ = rgi;
  if (!lighting_update_requested_) return;
  lighting_update_frames_++;
  const bool caught_up = lighting_update_frames_ >= 2 && !rtlm_busy_ && probe_live_left_ == 0;
  if (caught_up || lighting_update_frames_ > 600 || !scene_->lighting.realtime_gi) {
    lighting_update_requested_ = false;
    frozen_key_ = frozen_lighting_key();
    lighting_gen_++;  // drawn once more from the frozen results
  }
  ui_.redraw = true;
}

/* Task 0020: Preview Lighting, like Bakery's Realtime Preview. Half a second after the scene stops changing,
 * a rough bake (half the resolution, 32, then 64, then 256 samples) runs a slice a frame and is
 * shown instead of the real lightmaps; any change starts over. Never saved. */
void Editor::preview_step() {
  if (render_job_holds_lighting()) return;  // (round 412: an F12 render on a worker reads the preview's pages: nothing replaces them until it is done)
  const LightingSettings &ls = scene_->lighting;
  if (!ls.preview || baking_ || playing_ || !ls.baked_gi) {
    if (preview_running_) preview_baker_.cancel();
    preview_running_ = false;
    if (!preview_data_.empty()) {
      preview_data_ = LightingData{};
      lighting_gen_++;
    }
    preview_key_ = 0;
    preview_stage_ = preview_samples_ = 0;
    return;
  }
  /* (lightmap_scene_key() walks every object: only when something the views draw or a setting changed.) */
  const uint64_t vk = scene_render_hash() ^ hash_reflect([this](Reflector &r) { scene_->lighting.reflect(r); }) * 31;
  if (vk != preview_view_key_ || preview_scene_key_ == 0) {
    preview_view_key_ = vk;
    preview_scene_key_ = lightmap_scene_key();
  }
  const uint64_t key = preview_scene_key_;
  const double now = now_seconds();
  if (!lighting_data_.empty() && lighting_data_.scene_key == key && !wants_directions()) {
    /* The real bake is current (Generate Lighting just ran, or nothing changed since): it's shown. */
    if (preview_running_) preview_baker_.cancel();
    preview_running_ = false;
    if (!preview_data_.empty()) {
      preview_data_ = LightingData{};
      lighting_gen_++;
    }
    preview_key_ = key;
    preview_stage_ = 3;
    preview_samples_ = 0;
    return;
  }
  if (key != preview_key_) {  // changed: wait for it to settle, then start over
    if (preview_running_) preview_baker_.cancel();
    preview_running_ = false;
    preview_key_ = key;
    preview_settle_ = now;
    preview_stage_ = 0;
    preview_samples_ = 0;
  }
  const bool real_time = !headless_ || preview_like_window_;
  if (real_time && now - preview_settle_ < 0.5) {
    ui_.next_wakeup = ui_.next_wakeup > 0 ? std::min(ui_.next_wakeup, preview_settle_ + 0.5) : preview_settle_ + 0.5;
    return;
  }
  static const int kSamples[3] = {32, 64, 256};  // (task 0157: 16 showed a small emitter as blocks)
  if (!preview_running_) {
    if (preview_stage_ >= 3) return;  // refined
    std::vector<BakeObject> objects;
    std::vector<BakeLight> lights;
    collect_bake_scene(objects, lights);
    if (scene_->lighting.lighting_mode == 2) subtractive_split(lights);  // (round 444)
    if (scene_->lighting.lighting_mode == 1) shadowmask_split(lights, false);  // (round 454: logged once, by the bake)
    update_environment();
    BakeSettings s;
    s.texels_per_unit = std::max(2.0f, (finite_bits(ls.texels_per_unit) ? ls.texels_per_unit : 40.0f) * 0.5f);  // (half the density: a quarter showed blocks, task 0157)
    s.min_chart = 32;  // a small object still gets a few hundred texels, or it looks blocky at a quarter of the resolution
    s.max_size = std::min(ls.max_size, 1024);
    s.padding = ls.padding;
    s.direct_samples = std::max(32, std::min(ls.direct_samples, 64));  // area lights: shadow rays are cheap, and 32 stratified is smooth
    s.indirect_samples = kSamples[preview_stage_];
    s.bounces = ls.bounces;
    s.denoise = true;
    s.indirect_intensity = ls.indirect_intensity;
    s.directional = ls.directional_mode != 0;  // (round 426)
    s.lighting_mode = ls.lighting_mode;  // (round 444)
    s.gpu_device = choose_bake_gpu();
    s.use_oidn = false;  // (round 423: the preview finishes on the main thread - OIDN's filter there would be a stall)
    s.backface_tolerance = 0.0f;  // (round 430: its validity rays would be on the main thread too)
    preview_baker_.begin(objects, lights, env_, s);
    preview_running_ = true;
  }
  if (preview_baker_.step(real_time ? 8.0 : 1e9)) {
    preview_data_ = preview_baker_.take_result();
    preview_data_.scene_key = key;
    scene_->for_each([&](GameObject &g) {  // the meshes it lit (Manual keeps a moved object's map)
      if (preview_data_.entries.count(g.id))
        if (const Mesh *m = g.evaluated_mesh(1)) lm_mesh_baked_[g.id] = mesh_hash_of(*m);
    });
    preview_samples_ = kSamples[preview_stage_];
    preview_stage_++;
    preview_running_ = false;
    lighting_gen_++;
  }
  ui_.redraw = true;
}

}  // namespace bl

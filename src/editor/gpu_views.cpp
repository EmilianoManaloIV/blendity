// SPDX-License-Identifier: GPL-2.0-or-later
/* Task 0096 (ADR 0036): the views' deferred 3D on the GPU, read back into the canvas.
 *
 * render_deferred asks gpu_view_may_draw before its shadow map (a view the GPU draws renders its own; the CPU only
 * fits the frame), then gpu_view_draw. It draws on the GPU when Viewport Device allows it and everything the view shows
 * is supported. Otherwise it returns false and the CPU draws, as before, with the reason kept for Preferences. The CPU
 * renderer stays the reference: the unit tests pin Auto to it (BLENDITY_VIEWPORT_DEVICE=cpu), and the GPU's own tests
 * compare the two. */
#include "editor.h"

#include "../core/jobs.h"
#include "../render/colormanagement.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace bl {

namespace {

/* The material a triangle's slot shows, as Renderer3D::setup_tri picks it. */
const Material *slot_material(const DrawItem &it, uint32_t slot) {
  if (!it.materials || it.materials->empty()) return default_material().get();
  const MaterialPtr &mp = (*it.materials)[std::min<size_t>(slot, it.materials->size() - 1)];
  return mp ? mp.get() : default_material().get();
}

/* The material slots a mesh's triangles use, ascending (one, 0, for a mesh without them). */
std::vector<uint32_t> mesh_slots(const RenderMesh &rm) {
  const uint16_t first = rm.tri_material.empty() ? 0 : rm.tri_material[0];
  if (std::all_of(rm.tri_material.begin(), rm.tri_material.end(), [&](uint16_t s) { return s == first; })) return {first};  // (most meshes)
  std::vector<uint8_t> seen;
  for (uint16_t s : rm.tri_material) {
    if (s >= seen.size()) seen.resize((size_t)s + 1, 0);
    seen[s] = 1;
  }
  std::vector<uint32_t> out;
  for (size_t s = 0; s < seen.size(); s++)
    if (seen[s]) out.push_back((uint32_t)s);
  if (out.empty()) out.push_back(0);
  return out;
}

/* Why a material can't be drawn on the GPU yet, or null. Textures, procedurals and Box / Generated mapping are drawn
 * there since task 0101 (ADR 0038). */
const char *material_unsupported(const Material &m) {
  if (!m.opaque()) return "a see-through material";
  if (m.axis_mode != 0) return "an axis material";
  const Material::Resolved &tx = m.textures();
  for (int k = 0; k < 5; k++)
    if (tx.live[k]) return "a live procedural texture";
  return nullptr;
}

/* Why an object can't be drawn on the GPU yet, or null. */
const char *item_unsupported(const DrawItem &it, const LightingEnv &env) {
  if (!it.mesh) return nullptr;
  if (it.face_highlight) return "Edit Mode's face highlight";
  if (it.vertex_colors) return "vertex colours (Weight Paint)";
  if (it.lightmap || it.rt_bounce || (it.use_probes && env.probes)) return "lightmaps, probes or realtime GI";
  for (uint32_t slot : mesh_slots(*it.mesh))
    if (const char *why = material_unsupported(*slot_material(it, slot))) return why;
  return nullptr;
}

/* The mesh's indices for the card: as they are with one material; with several, its triangles grouped by slot (each
 * group one part, drawn with its own material), in their order within each. */
std::vector<uint32_t> card_indices(const RenderMesh &rm, std::vector<std::array<uint32_t, 3>> &parts) {
  parts.clear();
  const std::vector<uint32_t> slots = mesh_slots(rm);
  if (slots.size() < 2 || rm.tri_material.size() * 3 != rm.indices.size()) return rm.indices;
  std::vector<uint32_t> out;
  out.reserve(rm.indices.size());
  for (uint32_t s : slots) {
    const uint32_t first = (uint32_t)out.size();
    for (size_t t = 0; t < rm.tri_material.size(); t++)
      if (rm.tri_material[t] == s) out.insert(out.end(), rm.indices.begin() + (ptrdiff_t)t * 3, rm.indices.begin() + (ptrdiff_t)t * 3 + 3);
    parts.push_back({s, first, (uint32_t)out.size() - first});
  }
  return out;
}

/* Whether an object casts into the shadow map: as update_shadow_map decides it (any slot that casts, or none set). */
bool casts_shadows(const DrawItem &it) {
  if (!it.materials || it.materials->empty()) return true;
  for (const MaterialPtr &m : *it.materials)
    if (!m || m->cast_shadows) return true;
  return false;
}

/* A placement that mirrors (negative determinant) turns its faces inside out: the CPU flips its culling test for it. */
bool mirrored(const Mat4 &m) {
  const float det = m.at(0, 0) * (m.at(1, 1) * m.at(2, 2) - m.at(1, 2) * m.at(2, 1)) -
                    m.at(0, 1) * (m.at(1, 0) * m.at(2, 2) - m.at(1, 2) * m.at(2, 0)) +
                    m.at(0, 2) * (m.at(1, 0) * m.at(2, 1) - m.at(1, 1) * m.at(2, 0));
  return det < 0.0f;
}

gpu::RasterLight raster_light(const RenderLight &l) {
  gpu::RasterLight g;
  g.type = (int)l.type;
  g.position[0] = l.position.x, g.position[1] = l.position.y, g.position[2] = l.position.z;
  g.direction[0] = l.direction.x, g.direction[1] = l.direction.y, g.direction[2] = l.direction.z;
  g.color[0] = l.color.x, g.color[1] = l.color.y, g.color[2] = l.color.z;
  g.intensity = l.intensity;
  g.range = l.range;
  g.spot_cos_outer = l.cos_outer;
  g.spot_cos_inner = l.cos_inner;
  return g;
}

void set3(float *d, Vec3 v) { d[0] = v.x, d[1] = v.y, d[2] = v.z; }

/* A material that varies over the surface (task 0101, ADR 0038): its own values and its maps, which the card
 * evaluates per pixel as evaluate_surface does. `tex` gives a texture's id on the card. */
void raster_textured(gpu::RasterInstance &in, const Material &m, const RenderMesh &rm, const std::function<int(const TexturePtr &)> &tex) {
  const Material::Resolved &tx = m.textures();
  in.textured = true;
  set3(in.albedo_rough, m.base_color);
  in.albedo_rough[3] = m.roughness;
  in.metal_spec[0] = m.metallic;
  in.metal_spec[1] = m.specular;
  set3(in.emission, m.emission * m.emission_strength);
  set3(in.tiling, m.tiling);
  set3(in.offset, m.offset);
  set3(in.color2, m.procedural_color2);
  in.normal_strength = m.normal_strength;
  in.procedural_scale = m.procedural_scale;
  const bool proc = m.procedural == (int)Procedural::Checker || m.procedural == (int)Procedural::Noise;
  in.procedural = proc ? m.procedural : 0;
  const TexturePtr *maps[5] = {&tx.base, &tx.metallic, &tx.roughness, &tx.normal, &tx.emission};
  for (int k = 0; k < 5; k++) in.tex[k] = *maps[k] && !(k == 0 && proc) ? tex(*maps[k]) : -1;
  in.mapping = m.mapping;
  in.wrap = m.wrap;
  in.filter = m.filter;
  if (rm.bounds.valid()) {
    set3(in.bounds_min, rm.bounds.min);
    set3(in.bounds_max, rm.bounds.max);
  }
  in.has_uv = !rm.uvs.empty();
  in.has_tangent = !rm.tangents.empty();
}

/* A toon material's settings (ADR 0020), as the lighting pass's shade_toon / toon_rim read them (task 0102). */
void raster_toon(gpu::RasterInstance &in, const Material &m) {
  /* (A NaN setting: clampf's result on the CPU, where GLSL's clamp of a NaN is undefined.) */
  auto clean = [](float v, float lo, float hi) { return clampf(v, lo, hi); };
  in.toon_bands = m.toon_bands;
  in.toon_softness = clean(m.toon_softness, 0.0f, 0.5f);
  in.toon_threshold = clean(m.toon_threshold, 0.0f, 0.95f);
  in.toon_specular_size = m.toon_specular_size;
  in.toon_specular_smooth = m.toon_specular_smooth;
  set3(in.toon_shadow, m.toon_shadow);
  set3(in.toon_rim, m.toon_rim);
  in.toon_rim_width = m.toon_rim_width;
  in.toon_rim_strength = m.toon_rim_strength;
  in.toon_flat_ambient = m.toon_flat_ambient;
}

/* An object (one material's part of it) as the GPU draws it: a plain material sampled once (the same everywhere but
 * for the normal), else evaluated per pixel; the flags as the CPU decides them. */
gpu::RasterInstance raster_instance(const DrawItem &it, int mesh, const Material &m, const std::function<int(const TexturePtr &)> &tex) {
  gpu::RasterInstance in;
  std::memcpy(in.model, it.model.m, 64);
  const SurfaceSample s = evaluate_material(m, m.textures(), SurfacePoint{});
  set3(in.albedo_rough, s.albedo);
  in.albedo_rough[3] = s.roughness;
  in.metal_spec[0] = s.metallic;
  in.metal_spec[1] = s.specular;
  set3(in.emission, s.emission);
  if (!material_uniform(m, m.textures())) raster_textured(in, m, *it.mesh, tex);
  if (m.shading_model == 1) raster_toon(in, m);
  in.flags = 0;
  if (s.unlit) in.flags |= gpu::RasterInstance::Unlit;
  if (it.receive_shadows) in.flags |= gpu::RasterInstance::ReceiveShadows;
  if (casts_shadows(it)) in.flags |= gpu::RasterInstance::CastShadows;
  if (it.double_sided) in.flags |= gpu::RasterInstance::DoubleSided;  // (per object, as the CPU culls: its first material's)
  if (mirrored(it.model)) in.flags |= gpu::RasterInstance::Mirrored;
  if (it.always_cull) in.flags |= gpu::RasterInstance::AlwaysCull;
  if (m.shading_model == 1) in.flags |= gpu::RasterInstance::Toon;
  in.mesh = (uint32_t)mesh;
  in.id = it.id;
  return in;
}

gpu::RasterVertex raster_vertex(const RenderMesh &rm, size_t i) {
  const Vec3 p = rm.positions[i], n = i < rm.normals.size() ? rm.normals[i] : Vec3(0, 1, 0);
  gpu::RasterVertex v{{p.x, p.y, p.z}, {n.x, n.y, n.z}};
  if (i < rm.uvs.size()) v.uv[0] = rm.uvs[i].x, v.uv[1] = rm.uvs[i].y;
  if (i < rm.tangents.size()) v.t[0] = rm.tangents[i].x, v.t[1] = rm.tangents[i].y, v.t[2] = rm.tangents[i].z, v.t[3] = rm.tangents[i].w;
  return v;
}

std::vector<gpu::RasterVertex> raster_vertices(const RenderMesh &rm) {
  std::vector<gpu::RasterVertex> v(rm.positions.size());
  for (size_t i = 0; i < v.size(); i++) v[i] = raster_vertex(rm, i);
  return v;
}

/* The frame's world, background and view transform, as render_deferred and light_finish take them. Without a world
 * (the editor's own lighting) the CPU lights with a Gradient of the LightingEnv's own sky, equator and ground at
 * strength 1 (Renderer3D::flush's fallback_env), so the GPU does too. */
void raster_world(gpu::RasterFrame &fr, const LightingEnv &env, const Camera *cam, bool scene_lights) {
  const Environment *e = env.environment;
  fr.env_mode = e && e->mode == Environment::Color ? 1 : 0;
  fr.env_strength = e ? e->strength : 1.0f;
  set3(fr.sky, e ? e->sky : env.sky);
  set3(fr.equator, e ? e->equator : env.equator);
  set3(fr.ground, e ? e->ground : env.ground);
  if (e) set3(fr.env_color, e->color);
  fr.view_transform = (int)env.view_transform;
  fr.exposure = env.exposure;
  if (cam && cam->clear_flags == 1) {
    fr.background = 0;
    fr.clear = to_display_pixel(cam->background, ViewTransform::Standard, 0.0f);
    return;
  }
  if (!scene_lights) {  // the editor's own sky (render_deferred's clear_sky)
    fr.background = 1;
    set3(fr.bg_sky, {0.33f, 0.47f, 0.68f});
    set3(fr.bg_horizon, {0.70f, 0.74f, 0.79f});
    set3(fr.bg_ground, {0.27f, 0.26f, 0.25f});
    return;
  }
  fr.background = 2;
}

/* Task 0102: a Sky or HDRI world - its map on the card (as a texture, ADR 0038), its rotation and irradiance. */
void raster_world_map(gpu::RasterFrame &fr, const LightingEnv &env, const std::function<int(const TexturePtr &)> &tex) {
  const Environment *e = env.environment;
  if (!e || (e->mode != Environment::Sky && e->mode != Environment::Hdri)) return;
  fr.env_mode = 2;
  fr.env_rotation = e->rotation;
  fr.env_map = e->map ? tex(e->map) : -1;  // (no map yet: the gradient, as Environment::radiance)
  fr.sh_valid = e->sh_valid;
  for (int k = 0; k < 9; k++) set3(fr.sh[k], e->sh[k]);
}

/* An OpenColorIO view: its table goes to the GPU (once per view), which applies it as the CPU does. False when the
 * view has no table (OpenColorIO isn't there). The table is read as packed floats: Vec3 is three floats, nothing else. */
bool raster_view_lut(gpu::Raster &g, gpu::RasterFrame &fr, ViewTransform vt, std::string &err) {
  if ((int)vt < (int)ViewTransform::OcioView) return true;
  const int view = (int)vt - (int)ViewTransform::OcioView;
  const std::vector<Vec3> *lut = nullptr;
  int size = 0;
  float lo = 0, hi = 1;
  if (!colormanagement::view_lut(view, lut, size, lo, hi) || lut->empty()) {
    err = "an OpenColorIO view without its table";
    return false;
  }
  static_assert(sizeof(Vec3) == 3 * sizeof(float), "the LUT is read as packed floats");
  fr.view_transform = 3;
  return g.set_view_lut((uint64_t)view, &(*lut)[0].x, size, lo, hi, &err);
}

/* What the meshes on the card may take before the least recently drawn leave: a quarter of the card, at most 1 GB. */
uint64_t gpu_mesh_budget(const gpu::DeviceInfo &d) { return std::min<uint64_t>(1ull << 30, (d.memory_mb ? d.memory_mb : 2048) << 18); }

}  // namespace

void Editor::set_viewport_device(int d) {
  viewport_device_ = std::max(0, std::min(2, d));
  gpu_view_tried_ = false;  // (choosing again tries the GPU again after an error)
  gpu_view_error_.clear();
  gpu_view_reason_.clear();
  for (std::string &r : gpu_slot_reason_) r.clear();
}

bool Editor::gpu_view_wanted() const {
  if (viewport_device_ == 1) return false;
  if (viewport_device_ == 2) return true;
  const char *e = std::getenv("BLENDITY_VIEWPORT_DEVICE");
  return !(e && to_lower(e) == "cpu");
}

/* The GPU view, on the first Vulkan GPU. The editor opens it on a worker (the device, its pipelines and shaders take
 * a while the first time) and draws on the CPU meanwhile; headless runs open it at once. Null when there is none yet,
 * none at all, or it failed. */
gpu::Raster *Editor::gpu_view() {
  if (gpu_view_) return gpu_view_.get();
  if (gpu_view_opening_.valid()) {
    if (gpu_view_opening_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return nullptr;
    gpu_view_ = gpu_view_opening_.get();
    if (!gpu_view_) Log::warn("The views can't draw on the GPU (%s): drawing on the CPU", gpu_view_error_.c_str());
    return gpu_view_.get();
  }
  if (gpu_view_tried_) return nullptr;
  gpu_view_tried_ = true;
  if (!gpu::available() || gpu::devices().empty()) {
    gpu_view_error_ = "no Vulkan GPU";
    return nullptr;
  }
  const int index = gpu::devices()[0].index;
  auto open = [this, index]() {
    ScopedTimer t;
    std::string err;
    std::unique_ptr<gpu::Raster> r = gpu::Raster::create(index, &err);
    gpu_view_open_ms_ = t.ms();
    if (!r) gpu_view_error_ = err.empty() ? "the GPU couldn't be opened" : err;
    else Log::info("The views draw on the GPU (%s), opened in %.0f ms", r->info().name.c_str(), gpu_view_open_ms_);
    return r;
  };
  if (headless_) {
    gpu_view_ = open();
    return gpu_view_.get();
  }
  gpu_view_opening_ = std::async(std::launch::async, open);
  return nullptr;
}

std::string Editor::gpu_view_status() const {
  if (!gpu_view_wanted()) return "CPU (Viewport Device)";
  if (!gpu_view_) return gpu_view_opening_.valid() ? "CPU while the GPU opens" : gpu_view_tried_ ? "CPU: " + gpu_view_error_ : "GPU (not opened yet)";
  std::string s = "GPU: " + gpu_view_->info().name;
  std::string why;
  for (const std::string &r : gpu_slot_reason_)
    if (!r.empty() && why.find(r) == std::string::npos) why += (why.empty() ? "" : "; ") + r;
  if (!why.empty()) s += " (a view on the CPU: " + why + ")";
  if (shading_ == Shading::Solid || shading_ == Shading::Wireframe) s += " (the Scene view's Solid / Wireframe shading draws on the CPU)";
  return s;
}

const char *Editor::gpu_view_unsupported(const RenderTarget &rt, const LightingEnv &env, const std::vector<DrawItem> &items,
                                         const FilterStack *filters) const {
  if (filters && !filters->empty()) return "a camera filter";
  if (render_clear_override_on_) return "a transparent render";
  if (rt.want_hdr) return "a picture kept in linear light";
  if (env.gi || env.probes) return "realtime GI or probes";
  for (const RenderLight &l : env.lights)
    if (l.type == RenderLight::Area) return "an area light";
  if (env.lights.size() > 16) return "more than 16 lights";
  for (const DrawItem &it : items)
    if (const char *why = item_unsupported(it, env)) return why;
  return nullptr;
}

/* Before the shadow map: whether the GPU will draw this view as far as is known then (GI and probes are decided
 * after; gpu_view_draw checks again). The reason is kept per view for Preferences. */
bool Editor::gpu_view_may_draw(const RenderTarget &rt, const LightingEnv &env, const std::vector<DrawItem> &items, const FilterStack *filters) {
  if (gpu_view_off_ || !gpu_view_wanted() || rt.width <= 0 || rt.height <= 0 || !rt.color || !gpu_view()) return false;
  const int slot = gpu_slot(rt);
  const char *why = gpu_view_unsupported(rt, env, items, filters);
  gpu_slot_reason_[slot] = why ? why : "";
  if (!why) return true;
  gpu_view_reason_ = why;
  prof_.gpu_fallbacks++;
  return false;
}

/* A mesh on the card: kept while its serial stays; a new one of the same size (an Edit Mode drag moves it in place,
 * splitting quads again) updates it, another size replaces it. */
int Editor::gpu_mesh(const RenderMesh &rm, std::string &err) {
  GpuMesh &e = gpu_meshes_[&rm];
  e.used = gpu_calls_;
  if (e.id >= 0 && rm.serial != 0 && e.serial == rm.serial) return e.id;
  if (gpu_mesh_moved(e, rm, err)) return e.id;
  err.clear();  // (a refused partial update falls back to the whole mesh)
  const std::vector<gpu::RasterVertex> v = raster_vertices(rm);
  const std::vector<uint32_t> indices = card_indices(rm, e.parts);
  prof_.gpu_upload_bytes += (uint64_t)v.size() * sizeof(gpu::RasterVertex) + (uint64_t)rm.indices.size() * 4;
  if (e.id >= 0 && e.vertices == rm.positions.size() && e.indices == rm.indices.size() && gpu_view_->update_mesh(e.id, v, &indices, &err)) {
    prof_.gpu_mesh_updates++;
    e.serial = rm.serial;
    return e.id;
  }
  if (e.id >= 0) {
    gpu_view_->remove_mesh(e.id);
    gpu_mesh_bytes_ -= e.bytes;
  }
  e.id = gpu_view_->add_mesh(v, indices, &err);
  e.serial = rm.serial;
  e.vertices = (uint32_t)rm.positions.size();
  e.indices = (uint32_t)rm.indices.size();
  e.bytes = e.id >= 0 ? (uint64_t)v.size() * sizeof(gpu::RasterVertex) + (uint64_t)rm.indices.size() * 4 : 0;
  gpu_mesh_bytes_ += e.bytes;
  prof_.gpu_mesh_uploads++;
  return e.id;
}

/* This view on the CPU after all (what it shows can't go to the card), the card kept: the reason for Preferences. */
bool Editor::gpu_view_refuse(int slot, const std::string &why) {
  gpu_slot_reason_[slot] = gpu_view_reason_ = why;
  prof_.gpu_fallbacks++;
  return false;
}

/* Task 0102: a Sky world's map is made anew whenever its sun changes. The one before leaves the card at once rather
 * than after 256 calls unused (rotating the sun would otherwise hold dozens of float maps there). */
void Editor::gpu_forget_old_world(const LightingEnv &env) {
  const Environment *e = env.environment;
  const Texture *now = e && (e->mode == Environment::Sky || e->mode == Environment::Hdri) ? e->map.get() : nullptr;
  if (now == gpu_world_tex_) return;
  auto old = gpu_world_tex_ ? gpu_textures_.find(gpu_world_tex_) : gpu_textures_.end();
  if (old != gpu_textures_.end()) {
    if (old->second.id >= 0) gpu_view_->remove_texture(old->second.id);
    gpu_textures_.erase(old);
  }
  gpu_world_tex_ = now;
}

/* Task 0101: a texture on the card, sent once while it is drawn (its mip chain as the CPU built it). -1 and `err`
 * when the card can't hold it. */
int Editor::gpu_texture(const TexturePtr &t, std::string &err) {
  if (!t) return -1;
  auto found = gpu_textures_.find(t.get());
  if (found != gpu_textures_.end()) {
    found->second.used = gpu_calls_;
    if (found->second.id < 0 && err.empty()) err = found->second.refused;
    return found->second.id;
  }
  std::string why;
  const int id = gpu_view_->add_texture(*t, &why);
  /* Refused (too large): remembered, so it isn't copied and tried again every frame; forgotten with the others. */
  gpu_textures_[t.get()] = {id, t, gpu_calls_, id < 0 ? (why.empty() ? "a texture the card can't hold" : why) : std::string()};
  if (id < 0) {
    if (err.empty()) err = gpu_textures_[t.get()].refused;
    return -1;
  }
  prof_.gpu_texture_uploads++;
  return id;
}

/* Task 0099: the card's copy is the render mesh this one moved from in place: only what the move changed goes up. */
bool Editor::gpu_mesh_moved(GpuMesh &e, const RenderMesh &rm, std::string &err) {
  if (e.id < 0 || rm.moved_from == 0 || e.serial != rm.moved_from || e.vertices != rm.positions.size() || e.indices != rm.indices.size()) return false;
  if (!e.parts.empty()) return false;  // (its triangles are grouped by material on the card: the index ranges don't hold)
  std::vector<gpu::RasterVertex> v;  // (just the moved ranges' vertices, one range after another)
  for (const auto &r : rm.moved_vertices)
    for (uint32_t i = r.first; i < r.first + r.second && i < rm.positions.size(); i++) v.push_back(raster_vertex(rm, i));
  if (!gpu_view_->update_mesh_ranges(e.id, v, rm.moved_vertices, rm.indices, rm.moved_indices, &err)) return false;
  uint64_t bytes = 0;
  for (const auto &r : rm.moved_vertices) bytes += (uint64_t)r.second * sizeof(gpu::RasterVertex);
  for (const auto &r : rm.moved_indices) bytes += (uint64_t)r.second * 4;
  prof_.gpu_upload_bytes += bytes;
  prof_.gpu_partial_bytes += bytes;
  prof_.gpu_mesh_partial++;
  prof_.gpu_mesh_updates++;
  e.serial = rm.serial;
  return true;
}

/* Meshes not drawn for a while leave the card, and the least recently drawn when they take more than the budget (a
 * mesh made anew every frame - a time-driven modifier in Play - would otherwise pile up). Never one drawn in this
 * call. */
void Editor::gpu_forget_meshes() {
  for (auto it = gpu_textures_.begin(); it != gpu_textures_.end();) {  // (textures not drawn for a while: let go)
    if (gpu_calls_ - it->second.used <= 256) {
      ++it;
      continue;
    }
    if (it->second.id >= 0) gpu_view_->remove_texture(it->second.id);
    it = gpu_textures_.erase(it);
  }
  const uint64_t budget = gpu_mesh_budget(gpu_view_->info());
  if (gpu_calls_ % 64 && gpu_mesh_bytes_ <= budget) return;
  std::vector<std::pair<uint64_t, const RenderMesh *>> by_age;
  for (const auto &kv : gpu_meshes_)
    if (kv.second.used != gpu_calls_) by_age.push_back({kv.second.used, kv.first});
  std::sort(by_age.begin(), by_age.end());
  for (const auto &a : by_age) {
    if (gpu_calls_ - a.first <= 256 && gpu_mesh_bytes_ <= budget) break;
    GpuMesh &e = gpu_meshes_[a.second];
    if (e.id >= 0) gpu_view_->remove_mesh(e.id);
    gpu_mesh_bytes_ -= e.bytes;
    gpu_meshes_.erase(a.second);
  }
}

/* The view's slot of targets: the one it had, else the least recently used. */
int Editor::gpu_slot(const RenderTarget &rt) {
  int best = 0;
  for (int i = 0; i < gpu::Raster::kSlots; i++) {
    if (gpu_slot_rt_[i] == &rt) best = i;
    else if (gpu_slot_rt_[best] != &rt && gpu_slot_used_[i] < gpu_slot_used_[best]) best = i;
  }
  gpu_slot_rt_[best] = &rt;
  gpu_slot_used_[best] = gpu_calls_ + 1;
  return best;
}

bool Editor::gpu_view_draw(RenderTarget &rt, const Mat4 &v, const Mat4 &p, Vec3 eye, const LightingEnv &env, const std::vector<DrawItem> &items,
                           const Camera *cam, bool scene_lights, const FilterStack *filters, const RasterOptions &opt) {
  gpu::Raster *g = gpu_view_.get();
  if (!g) return false;
  const int slot = gpu_slot(rt);
  if (const char *why = gpu_view_unsupported(rt, env, items, filters)) {  // (GI and probes, decided after the first check)
    gpu_slot_reason_[slot] = gpu_view_reason_ = why;
    prof_.gpu_fallbacks++;
    return false;
  }
  ProfileScope ps{prof_.ms_gpu};
  gpu_calls_++;
  std::string err;
  std::vector<gpu::RasterInstance> inst;
  inst.reserve(items.size());
  bool ok = true;
  std::string tex_err;  // (a texture the card can't hold: this view on the CPU, the card kept)
  auto tex = [&](const TexturePtr &t) { return gpu_texture(t, tex_err); };
  for (const DrawItem &it : items) {
    if (!it.mesh || it.mesh->indices.empty()) continue;
    const int mesh = gpu_mesh(*it.mesh, err);
    ok = ok && mesh >= 0;
    if (mesh < 0) continue;
    const GpuMesh &e = gpu_meshes_[it.mesh];
    if (e.parts.empty()) {
      inst.push_back(raster_instance(it, mesh, *slot_material(it, mesh_slots(*it.mesh)[0]), tex));
      continue;
    }
    for (const auto &part : e.parts) {  // (several materials: one instance per material's triangles)
      gpu::RasterInstance in = raster_instance(it, mesh, *slot_material(it, part[0]), tex);
      in.first_index = part[1];
      in.index_count = part[2];
      inst.push_back(in);
    }
  }
  if (ok && !tex_err.empty()) return gpu_view_refuse(slot, tex_err);
  gpu::RasterFrame fr;
  fr.slot = slot;
  fr.width = rt.width;
  fr.height = rt.height;
  const Mat4 vp = p * v;
  std::memcpy(fr.view_proj, vp.m, 64);
  set3(fr.eye, eye);
  fr.shadow_size = env.shadow && env.shadow_light >= 0 ? env.shadow->size : 0;
  if (fr.shadow_size) {
    std::memcpy(fr.sun_view_proj, env.shadow->view_proj.m, 64);
    fr.shadow_light = env.shadow_light;
    fr.shadow_bias = env.shadow->bias;
    fr.shadow_fast = env.shadow->fast;
  }
  fr.cull_backfaces = opt.backface_culling;
  for (const RenderLight &l : env.lights) fr.lights.push_back(raster_light(l));
  raster_world(fr, env, cam, scene_lights);
  raster_world_map(fr, env, tex);
  gpu_forget_old_world(env);
  if (ok && !tex_err.empty()) return gpu_view_refuse(slot, tex_err);  // (the world's map too large for the card)
  ok = ok && raster_view_lut(*g, fr, env.view_transform, err);
  std::string cage_err;  // (a cage the card refuses is drawn by the CPU, as before)
  const bool cage = scene_full_call_ && edit_mode_ && ok && gpu_cage(*g, fr, cage_err);
  if (!cage_err.empty() && !cage_err_logged_) {
    Log::warn("The GPU view can't take the Edit Mode cage (%s): the CPU draws it", cage_err.c_str());
    cage_err_logged_ = true;
  }
  g->set_instances(inst);
  /* The picture, and the planes later work reads: depth for picking and overlays, and for a camera's depth of field
   * when it has one; ids for picking (not in a view nobody picks in: task 0092). The colour comes straight into the
   * canvas when its rows are contiguous there, else through a buffer of the view's own size. */
  const bool contiguous = rt.stride == rt.width;
  if (!contiguous) gpu_color_.resize((size_t)rt.width * rt.height);
  uint32_t *color = contiguous ? rt.color : gpu_color_.data();
  const bool dof = cam && cam->dof && !cam->orthographic;
  const bool want_depth = !rt.depth.empty() && (planes_wanted_ || dof), want_ids = !rt.ids.empty() && planes_wanted_;
  ok = ok && g->render(fr, &err) && g->read_back(slot, color, want_depth ? rt.depth.data() : nullptr, want_ids ? rt.ids.data() : nullptr, &err);
  gpu_forget_meshes();
  if (!ok) {
    /* Any error: this frame on the CPU, and the GPU set aside until Viewport Device is chosen again. */
    Log::warn("The GPU view failed (%s): drawing on the CPU", err.c_str());
    gpu_view_error_ = err;
    gpu_slot_reason_[slot] = gpu_view_reason_ = "a GPU error";
    gpu_view_drop();
    prof_.gpu_fallbacks++;
    return false;
  }
  for (int y = 0; !contiguous && y < rt.height; y++)
    std::memcpy(rt.color + (size_t)y * rt.stride, &gpu_color_[(size_t)y * rt.width], (size_t)rt.width * 4);
  gpu_slot_reason_[slot].clear();
  gpu_view_reason_.clear();
  prof_.gpu_views++;
  if (scene_gpu_call_) scene_on_gpu_ = true;
  cage_on_gpu_ = cage_on_gpu_ || cage;
  prof_.gpu_cage_frames += cage ? 1 : 0;
  return true;
}

/* The card set aside (an error): what was kept there is forgotten with it, so a card opened again - perhaps at the
 * same address - gets everything again. */
void Editor::gpu_view_drop() {
  gpu_meshes_.clear();
  gpu_textures_.clear();
  gpu_world_tex_ = nullptr;
  gpu_mesh_bytes_ = 0;
  gpu_cage_v_.clear();
  gpu_cage_e_.clear();
  gpu_cage_owner_ = nullptr;
  gpu_cage_edges_key_ = 0;
  scene_on_gpu_ = false;
  gpu_view_.reset();
}

/* The elements of `now` that differ from `before`, as (first, count) runs, runs closer than 64 joined. False when the
 * sizes differ (everything goes). */
template <class T>
static bool changed_runs(const std::vector<T> &now, const std::vector<T> &before, std::vector<std::pair<uint32_t, uint32_t>> &out) {
  out.clear();
  if (now.size() != before.size()) return false;
  for (uint32_t i = 0; i < (uint32_t)now.size(); i++) {
    if (std::memcmp(&now[i], &before[i], sizeof(T)) == 0) continue;
    if (!out.empty() && i <= out.back().first + out.back().second + 64) out.back().second = i + 1 - out.back().first;
    else out.push_back({i, 1});
  }
  return true;
}

/* What the cage's edge colours depend on (cage_edge_color), hashed as scene_view_key hashes the selection. */
uint64_t Editor::cage_edges_key(const Mesh &m) const {
  uint64_t h = 1469598103934665603ull;
  auto mix = [&](const void *p, size_t n) {
    const uint8_t *b = (const uint8_t *)p;
    for (size_t i = 0; i < n; i += 8) {
      uint64_t v = 0;
      std::memcpy(&v, b + i, std::min<size_t>(8, n - i));
      h = (h ^ v) * 1099511628211ull;
      h ^= h >> 29;
    }
  };
  const uint64_t head[5] = {m.uid.v, m.topology_serial(), (uint64_t)elem_, ngon_mode_ ? m.version : 0, ngon_mode_ ? m.deform_version : 0};
  mix(head, sizeof(head));
  mix(vert_sel_.data(), vert_sel_.size());
  uint64_t es = edge_sel_.size();  // order-free: the set's iteration order isn't stable
  for (uint64_t k : edge_sel_) es += (k * 0x9E3779B97F4A7C15ull) ^ (k >> 31);
  mix(&es, 8);
  return h;
}

/* Task 0100: the Edit Mode cage for the card - the edges and vertex dots render_overlays would draw, their colours by
 * its rules - sent as only what changed since the last frame (a drag moves a few vertices; a click recolours a few). */
bool Editor::gpu_cage(gpu::Raster &g, gpu::RasterFrame &fr, std::string &err) {
  GameObject *obj = edit_object();
  if (!obj || !edit_mesh_ptr() || !*edit_mesh_ptr() || !g.can_draw_cage()) return false;
  const Mesh &m = **edit_mesh_ptr();
  const Mat4 &w = obj->world_matrix();
  vert_sel_.resize(m.vert_count(), 0);
  if (ngon_mode_) ngon_cache(m);
  const bool dots = elem_ == EditElement::Vertex;
  std::vector<gpu::CageVertex> v(m.vert_count());
  JobSystem::global().parallel_for((int64_t)v.size(), 4096, [&](int64_t a, int64_t b) {
    for (int64_t i = a; i < b; i++) {
      const Vec3 p = w.point(m.positions[(size_t)i]);
      v[(size_t)i] = {{p.x, p.y, p.z}, dots ? cage_dot_color((size_t)i) : 0u};
    }
  });
  if (gpu_cage_owner_ != &g) {  // (a card opened again holds nothing yet)
    gpu_cage_v_.clear();
    gpu_cage_e_.clear();
    gpu_cage_owner_ = &g;
  }
  /* The edges' colours follow the selection, the topology (its seams), the mode, and in N-gon mode the shape (which
   * edges lie inside a flat region): worked out again only when one of them changed. */
  const uint64_t key = cage_edges_key(m);
  const bool edges_same = key == gpu_cage_edges_key_ && gpu_cage_e_.size() == m.edge_cache().size();
  std::vector<gpu::CageEdge> e;
  if (!edges_same) {
    const auto &edges = m.edge_cache();
    e.resize(edges.size());
    JobSystem::global().parallel_for((int64_t)e.size(), 4096, [&](int64_t a, int64_t b) {
      for (int64_t i = a; i < b; i++) {
        const auto &ed = edges[(size_t)i];
        e[(size_t)i] = {ed.first, ed.second, cage_edge_color(m, ed.first, ed.second), 0};
      }
    });
  }
  std::vector<std::pair<uint32_t, uint32_t>> vr, er;
  const bool vpart = changed_runs(v, gpu_cage_v_, vr), epart = edges_same || changed_runs(e, gpu_cage_e_, er);
  uint64_t bytes = 0;
  for (const auto &r : vr) bytes += (uint64_t)r.second * sizeof(gpu::CageVertex);
  for (const auto &r : er) bytes += (uint64_t)r.second * sizeof(gpu::CageEdge);
  bytes += vpart ? 0 : v.size() * sizeof(gpu::CageVertex);
  bytes += epart ? 0 : e.size() * sizeof(gpu::CageEdge);
  if (!g.set_cage_vertices(v, vpart ? &vr : nullptr, &err) || (!edges_same && !g.set_cage_edges(e, epart ? &er : nullptr, &err))) {
    gpu_cage_owner_ = nullptr;  // (what the card holds is unknown: everything again next time)
    gpu_cage_edges_key_ = 0;
    return false;
  }
  prof_.gpu_cage_bytes += bytes;
  gpu_cage_v_ = std::move(v);
  if (!edges_same) gpu_cage_e_ = std::move(e);
  gpu_cage_edges_key_ = key;
  fr.cage = true;
  fr.cage_bias = kCageBias;
  fr.cage_point_radius = dots ? cage_dot_radius() : 0.0f;
  return true;
}

}  // namespace bl

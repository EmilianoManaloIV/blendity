// SPDX-License-Identifier: GPL-2.0-or-later
/* Task 0096 (ADR 0036): the views' deferred 3D on the GPU, read back into the canvas.
 *
 * render_deferred asks gpu_view_may_draw before its shadow map (a view the GPU draws renders its own; the CPU only
 * fits the frame), then gpu_view_draw. It draws on the GPU when Viewport Device allows it and everything the view shows
 * is supported. Otherwise it returns false and the CPU draws, as before, with the reason kept for Preferences. The CPU
 * renderer stays the reference: the unit tests pin Auto to it (BLENDITY_VIEWPORT_DEVICE=cpu), and the GPU's own tests
 * compare the two. */
#include "editor.h"

#include "../render/colormanagement.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace bl {

namespace {

/* The plain material a mesh shows (its one slot), or null when the mesh has several. */
const Material *only_material(const DrawItem &it) {
  const RenderMesh &rm = *it.mesh;
  const uint16_t slot = rm.tri_material.empty() ? 0 : rm.tri_material[0];
  for (uint16_t s : rm.tri_material)
    if (s != slot) return nullptr;
  if (!it.materials || it.materials->empty()) return default_material().get();
  const MaterialPtr &mp = (*it.materials)[std::min<size_t>(slot, it.materials->size() - 1)];
  return mp ? mp.get() : default_material().get();
}

/* Why a material can't be drawn on the GPU yet, or null. */
const char *material_unsupported(const Material &m) {
  if (!m.opaque()) return "a see-through material";
  if (m.shading_model == 1) return "a toon material";
  if (!material_uniform(m, m.textures())) return "a material with textures, a procedural or axis mapping";
  return nullptr;
}

/* Why an object can't be drawn on the GPU yet, or null. */
const char *item_unsupported(const DrawItem &it, const LightingEnv &env) {
  if (!it.mesh) return nullptr;
  if (it.face_highlight) return "Edit Mode's face highlight";
  if (it.vertex_colors) return "vertex colours (Weight Paint)";
  if (it.always_cull) return "a toon outline";
  if (it.lightmap || it.rt_bounce || (it.use_probes && env.probes)) return "lightmaps, probes or realtime GI";
  const Material *m = only_material(it);
  if (!m) return "several materials on one mesh";
  return material_unsupported(*m);
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

/* An object as the GPU draws it: the material sampled once (a plain one is the same everywhere but for the normal),
 * the flags as the CPU decides them. */
gpu::RasterInstance raster_instance(const DrawItem &it, int mesh) {
  gpu::RasterInstance in;
  std::memcpy(in.model, it.model.m, 64);
  const Material &m = *only_material(it);
  const SurfaceSample s = evaluate_material(m, m.textures(), SurfacePoint{});
  set3(in.albedo_rough, s.albedo);
  in.albedo_rough[3] = s.roughness;
  in.metal_spec[0] = s.metallic;
  in.metal_spec[1] = s.specular;
  set3(in.emission, s.emission);
  in.flags = 0;
  if (s.unlit) in.flags |= gpu::RasterInstance::Unlit;
  if (it.receive_shadows) in.flags |= gpu::RasterInstance::ReceiveShadows;
  if (casts_shadows(it)) in.flags |= gpu::RasterInstance::CastShadows;
  if (it.double_sided || m.double_sided) in.flags |= gpu::RasterInstance::DoubleSided;
  if (mirrored(it.model)) in.flags |= gpu::RasterInstance::Mirrored;
  in.mesh = (uint32_t)mesh;
  in.id = it.id;
  return in;
}

std::vector<gpu::RasterVertex> raster_vertices(const RenderMesh &rm) {
  std::vector<gpu::RasterVertex> v(rm.positions.size());
  for (size_t i = 0; i < v.size(); i++) {
    const Vec3 p = rm.positions[i], n = i < rm.normals.size() ? rm.normals[i] : Vec3(0, 1, 0);
    v[i] = {{p.x, p.y, p.z}, {n.x, n.y, n.z}};
  }
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
  if (env.environment && (env.environment->mode == Environment::Sky || env.environment->mode == Environment::Hdri)) return "a Sky or HDRI world";
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
  const std::vector<gpu::RasterVertex> v = raster_vertices(rm);
  if (e.id >= 0 && e.vertices == rm.positions.size() && e.indices == rm.indices.size() && gpu_view_->update_mesh(e.id, v, &rm.indices, &err)) {
    prof_.gpu_mesh_updates++;
    e.serial = rm.serial;
    return e.id;
  }
  if (e.id >= 0) {
    gpu_view_->remove_mesh(e.id);
    gpu_mesh_bytes_ -= e.bytes;
  }
  e.id = gpu_view_->add_mesh(v, rm.indices, &err);
  e.serial = rm.serial;
  e.vertices = (uint32_t)rm.positions.size();
  e.indices = (uint32_t)rm.indices.size();
  e.bytes = e.id >= 0 ? (uint64_t)v.size() * sizeof(gpu::RasterVertex) + (uint64_t)rm.indices.size() * 4 : 0;
  gpu_mesh_bytes_ += e.bytes;
  prof_.gpu_mesh_uploads++;
  return e.id;
}

/* Meshes not drawn for a while leave the card, and the least recently drawn when they take more than the budget (a
 * mesh made anew every frame - a time-driven modifier in Play - would otherwise pile up). Never one drawn in this
 * call. */
void Editor::gpu_forget_meshes() {
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
  for (const DrawItem &it : items) {
    if (!it.mesh || it.mesh->indices.empty()) continue;
    const int mesh = gpu_mesh(*it.mesh, err);
    ok = ok && mesh >= 0;
    if (mesh >= 0) inst.push_back(raster_instance(it, mesh));
  }
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
  ok = ok && raster_view_lut(*g, fr, env.view_transform, err);
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
    gpu_meshes_.clear();
    gpu_mesh_bytes_ = 0;
    gpu_view_.reset();
    prof_.gpu_fallbacks++;
    return false;
  }
  for (int y = 0; !contiguous && y < rt.height; y++)
    std::memcpy(rt.color + (size_t)y * rt.stride, &gpu_color_[(size_t)y * rt.width], (size_t)rt.width * 4);
  gpu_slot_reason_[slot].clear();
  gpu_view_reason_.clear();
  prof_.gpu_views++;
  return true;
}

}  // namespace bl

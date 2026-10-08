// SPDX-License-Identifier: GPL-2.0-or-later
// Rendering glue between the scene and the two render engines:
//   * Shaded viewport / Game view: deferred PBR rasterizer + sun shadow map
//     (Blender: EEVEE "Material Preview"/"Rendered"; Unity: Scene view "Shaded")
//   * Rendered viewport + final render: progressive path tracer (Blender: Cycles)
//   * World environment: gradient / Hosek-Wilkie sky / HDRI / colour
#include "editor.h"

#include "../core/core.h"
#include "../core/jobs.h"
#include "../render/sky.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>

namespace bl {

/* ===================================================================== */
/* Scene -> render data                                                   */
/* ===================================================================== */

static bool needs_tangents(const std::vector<MaterialPtr> &mats) {
  for (auto &m : mats)
    if (m && !m->normal_map.empty()) return true;
  return false;
}

std::vector<DrawItem> Editor::collect_items(bool game, bool want_tangents) {
  std::vector<DrawItem> items;
  scene_->for_each([&](GameObject &g) {
    if (!g.active_in_hierarchy()) return;
    auto *mr = g.get<MeshRenderer>();
    auto *mf = g.get<MeshFilter>();
    if (!mr || !mr->enabled || !mf || !mf->mesh) return;
    if (game ? !mr->show_in_renders : mr->display_as != 0) return;  // wire / bounds: drawn as overlays
    bool editing = !game && edit_mode_ && g.id == edit_obj_;
    /* Edit Mode shows the modifiers set to Show in Edit Mode over the cage; renders use Show in Renders. */
    const Mesh *m = g.evaluated_mesh(editing ? 2 : game ? 1 : 0);
    if (editing && m != mf->mesh.get()) editing = false;  // a modifier result: shade it normally
    if (!m) return;
    DrawItem it;
    it.mesh = want_tangents && needs_tangents(mr->materials) ? &m->render_mesh_tangents(editing) : &m->render_mesh(editing);
    it.model = g.world_matrix();
    const Material &mat0 = *mr->material(0);
    it.albedo = mat0.base_color;
    it.specular = mat0.specular * 0.5f;
    it.unlit = mat0.unlit;
    it.double_sided = editing;
    for (auto &mp : mr->materials)
      if (mp && mp->double_sided) it.double_sided = true;
    it.materials = &mr->materials;
    it.receive_shadows = mr->receive_shadows;
    it.id = (uint32_t)g.id;
    if (editing && elem_ == EditElement::Face) it.face_highlight = &face_sel_;
    items.push_back(it);
  });
  return items;
}

/* A Light component as the renderers see it (all four types, temperature applied). */
RenderLight to_render_light(const GameObject &g, const Light &l) {
  RenderLight rl;
  rl.type = l.type == 1 ? RenderLight::Point : l.type == 2 ? RenderLight::Spot : l.type == 3 ? RenderLight::Area : RenderLight::Directional;
  const Quat q = g.world_rotation();
  rl.direction = normalize(q.rotate({0, 0, 1}));
  rl.right = normalize(q.rotate({1, 0, 0}));
  rl.position = g.world_position();
  rl.color = l.final_color();
  rl.intensity = l.intensity;
  rl.range = l.range;
  const float outer = std::max(1.0f, std::min(179.0f, l.spot_angle)) * 0.5f;
  const float inner = std::min(outer, std::max(0.0f, l.inner_spot_angle) * 0.5f);
  rl.cos_outer = std::cos(outer * kDeg2Rad);
  rl.cos_inner = std::cos(inner * kDeg2Rad);
  rl.disk = l.area_shape == 1;
  /* The object's scale stretches an area light, as in Blender. */
  const Mat4 &w = g.world_matrix();
  rl.width = l.area_width * length(w.dir({1, 0, 0}));
  rl.height = l.area_height * length(w.dir({0, 1, 0}));
  return rl;
}

LightingEnv Editor::make_lighting(Vec3 eye, bool use_scene_lights) {
  LightingEnv env;
  const EnvironmentSettings &es = scene_->environment;
  env.sky = es.sky;
  env.equator = es.equator;
  env.ground = es.ground;
  env.camera_pos = eye;
  env.view_transform = view_transform_from_setting(scene_->render.view_transform);
  env.exposure = scene_->render.exposure;
  if (!use_scene_lights) {
    /* Blender "Studio" style headlight so unlit scenes stay readable. */
    RenderLight head;
    head.direction = normalize(cam_.forward() + cam_.up() * -0.3f + cam_.right() * 0.2f);
    head.intensity = 0.85f;
    env.lights.push_back(head);
    env.sky = env.equator = env.ground = Vec3(0.35f);
    return env;
  }
  scene_->for_each([&](GameObject &g) {
    if (!g.active_in_hierarchy()) return;
    auto *l = g.get<Light>();
    if (!l || !l->enabled) return;
    env.lights.push_back(to_render_light(g, *l));
  });
  update_environment();
  env.environment = &env_;
  return env;
}

void Editor::update_environment() {
  const EnvironmentSettings &es = scene_->environment;
  Vec3 sun{0.35f, 0.75f, -0.55f};
  scene_->for_each([&](GameObject &g) {
    if (auto *l = g.get<Light>())
      if (l->enabled && l->type == 0 && g.active_in_hierarchy()) sun = -normalize(g.world_rotation().rotate({0, 0, 1}));
  });
  /* Cache key: everything the baked environment depends on. */
  uint64_t key = 1469598103934665603ull;
  auto mix = [&](const void *p, size_t n) {
    for (size_t i = 0; i < n; i++) { key ^= ((const uint8_t *)p)[i]; key *= 1099511628211ull; }
  };
  mix(&es.mode, 4);
  mix(&es.turbidity, 4);
  mix(&es.ground_albedo, 4);
  mix(&es.rotation, 4);
  mix(es.hdri.path.data(), es.hdri.path.size());
  if (es.mode == 1) mix(&sun, sizeof(sun));
  env_.sky = es.sky;
  env_.equator = es.equator;
  env_.ground = es.ground;
  env_.color = es.color;
  env_.strength = es.strength;
  env_.rotation = es.rotation;
  env_.mode = (Environment::Mode)std::max(0, std::min(3, es.mode));
  if (key == env_key_) return;
  env_key_ = key;
  env_.map = nullptr;
  if (es.mode == 1) {
    env_.map = generate_sky_texture(sun, es.turbidity, es.ground_albedo, 512);
    env_.rotation = 0.0f;  // the sky is baked in world space
  }
  else if (es.mode == 2 && !es.hdri.empty()) {
    std::string err;
    env_.map = texture_load(resolve_asset_path(es.hdri.path), false, &err);
    if (!env_.map) Log::warn("World HDRI: %s", err.c_str());
  }
  if ((es.mode == 1 || es.mode == 2) && !env_.map) env_.mode = Environment::Gradient;
  env_.compute_sh();
}

uint64_t Editor::scene_render_hash() {
  uint64_t h = 1469598103934665603ull;
  auto mix = [&](const void *p, size_t n) {
    for (size_t i = 0; i < n; i++) { h ^= ((const uint8_t *)p)[i]; h *= 1099511628211ull; }
  };
  scene_->for_each([&](GameObject &g) {
    if (!g.active_in_hierarchy()) return;
    auto *mr = g.get<MeshRenderer>();
    auto *l = g.get<Light>();
    if (!mr && !l) return;
    mix(&g.id, 8);
    mix(g.world_matrix().m, sizeof(float) * 16);
    if (mr && mr->enabled) {
      const Mesh *m = g.evaluated_mesh(1);  // renders: Show in Renders
      mix(&m, sizeof(m));
      const Mesh *mv = g.evaluated_mesh(0);  // the Rendered viewport: the viewport toggles
      mix(&mv, sizeof(mv));
      if (mv) mix(&mv->version, 8);
      if (m) mix(&m->version, 8);
      for (auto &mp : mr->materials) {
        const void *p = mp.get();
        mix(&p, sizeof(p));
        if (mp) mix(&mp->version, 8);
      }
    }
    if (l && l->enabled) {
      mix(&l->type, 4);
      mix(&l->color, sizeof(Vec3));
      mix(&l->intensity, 4);
      mix(&l->range, 4);
    }
  });
  const EnvironmentSettings &es = scene_->environment;
  mix(&es.mode, 4);
  mix(&es.strength, 4);
  mix(&es.rotation, 4);
  mix(&es.turbidity, 4);
  mix(&es.color, sizeof(Vec3));
  mix(&es.sky, sizeof(Vec3) * 3);
  mix(es.hdri.path.data(), es.hdri.path.size());
  mix(&edit_mode_, 1);
  mix(&edit_obj_, 8);
  mix(&scene_lighting_, 1);
  return h;
}

bool Editor::camera_for_render(Mat4 &view, Mat4 &proj, float aspect) {
  GameObject *owner = nullptr;
  Camera *cam = main_camera(*scene_, &owner);
  if (!cam) {
    view = cam_.view();
    proj = cam_.proj(aspect);
    return false;
  }
  Quat q = owner->world_rotation();
  Vec3 eye = owner->world_position();
  view = Mat4::look_at(eye, eye + q.rotate({0, 0, 1}), q.rotate({0, 1, 0}));
  proj = cam->projection(aspect);
  return true;
}

/* ===================================================================== */
/* Rasterized (EEVEE-like)                                                */
/* ===================================================================== */

void Editor::update_shadow_map(const std::vector<DrawItem> &items, const LightingEnv &env) {
  int li = -1;
  for (size_t i = 0; i < env.lights.size(); i++)
    if (env.lights[i].type == RenderLight::Directional) { li = (int)i; break; }
  if (li < 0) {
    shadow_.size = 0;
    return;
  }
  std::vector<DrawItem> casters;
  AABB bounds;
  for (const DrawItem &it : items) {
    bool casts = false;
    if (!it.materials || it.materials->empty()) casts = true;
    else
      for (auto &m : *it.materials)
        if (!m || m->cast_shadows) casts = true;
    if (!casts) continue;
    casters.push_back(it);
    bounds.add(it.mesh->bounds.transformed(it.model));
  }
  render_shadow_map(shadow_, casters, env.lights[li].direction, bounds, std::max(256, scene_->render.shadow_resolution));
}

void Editor::render_deferred(Renderer3D &r3d, RenderTarget &rt, const Mat4 &v, const Mat4 &p, Vec3 eye, bool game,
                             bool scene_lights, const Camera *cam) {
  LightingEnv env = make_lighting(eye, scene_lights);
  if (cam) env.exposure += cam->exposure_stops();  // ISO, shutter and f-stop
  std::vector<DrawItem> items = collect_items(game, true);
  if (scene_lights && scene_->render.shadows) {
    update_shadow_map(items, env);
    if (shadow_.valid()) {
      for (size_t i = 0; i < env.lights.size(); i++)
        if (env.lights[i].type == RenderLight::Directional) { env.shadow_light = (int)i; break; }
      env.shadow = &shadow_;
    }
  }
  RasterOptions opt = raster_opt_;
  opt.shade = ShadeMode::Deferred;
  r3d.begin(&rt, v, p, env, opt);
  Mat4 inv = (p * v).inverse();
  if (cam && cam->clear_flags == 1) r3d.clear(to_display_pixel(cam->background, ViewTransform::Standard, 0.0f));
  else if (!scene_lights || env_.mode == Environment::Gradient)
    r3d.clear_sky(inv, {0.33f, 0.47f, 0.68f}, {0.70f, 0.74f, 0.79f}, {0.27f, 0.26f, 0.25f});
  else r3d.clear_environment(inv, env_, env.view_transform, env.exposure);
  for (const DrawItem &it : items) r3d.add(it);
  r3d.flush();
}

/* ===================================================================== */
/* Path traced (Cycles-like)                                              */
/* ===================================================================== */

std::vector<int> Editor::enabled_gpus() const {
  std::vector<int> out;
  for (const gpu::DeviceInfo &d : gpu::devices())
    if (!render_devices_off_.count(d.name)) out.push_back(d.index);
  return out;
}

PTSettings Editor::make_pt_settings(const RenderSettings &rs) const {
  PTSettings s;
  /* GPU Compute: the ticked GPUs (and the CPU too when asked). With no GPU
   * available it quietly stays on the CPU. */
  if (rs.device == 1 && gpu::available()) {
    s.gpus = enabled_gpus();
    s.use_cpu = rs.gpu_with_cpu || s.gpus.empty();
    s.gpu_hardware_rt = rs.hardware_rt;
  }
  s.max_bounces = rs.max_bounces;
  s.clamp_indirect = rs.clamp_indirect;
  s.denoise = rs.denoise;
  s.use_oidn = rs.denoiser == 0;
  s.use_embree = rs.use_embree;
  s.use_guiding = rs.path_guiding;
  s.view_transform = view_transform_from_setting(rs.view_transform);
  s.exposure = rs.exposure;
  return s;
}

static void build_pt(PathTracer &pt, Editor &, const std::vector<DrawItem> &items, const LightingEnv &env,
                     const Environment &world) {
  std::vector<PTObject> objs;
  for (const DrawItem &it : items) objs.push_back({it.mesh, it.model, it.materials});
  pt.build(objs, env.lights, world);
}

void Editor::render_pathtraced_view(const Recti &view) {
  /* Depth / id pre-pass so picking, outlines, grid and gizmos keep working. */
  float aspect = view.w / (float)std::max(1, view.h);
  Mat4 v = cam_.view(), p = cam_.proj(aspect);
  std::vector<DrawItem> items = collect_items(false, true);
  {
    RasterOptions opt = raster_opt_;
    opt.shade = ShadeMode::DepthOnly;
    LightingEnv none;
    scene_r3d_.begin(&scene_rt_, v, p, none, opt);
    scene_r3d_.clear(0xFF000000);
    for (const DrawItem &it : items) scene_r3d_.add(it);
    scene_r3d_.flush();
  }
  /* Half resolution on the CPU; GPUs are fast enough for the full view. */
  const int div = scene_->render.device == 1 && gpu::available() && !enabled_gpus().empty() ? 1 : 2;
  int pw = std::max(1, view.w / div), ph = std::max(1, view.h / div);
  uint64_t hash = scene_render_hash();
  bool rebuilt = false;
  const RenderSettings &rs = scene_->render;
  const int target = std::max(1, rs.viewport_samples);
  PTSettings ps = make_pt_settings(rs);
  /* While samples accumulate the viewport uses the fast A-Trous filter; the
   * slower OpenImageDenoise runs once when the view converges (~70 ms at
   * half resolution, too slow for every frame). */
  ps.use_oidn = ps.use_oidn && vp_pt_.samples() + 1 >= target;
  vp_pt_.set_settings(ps);
  const bool want_embree = rs.use_embree && PathTracer::embree_available();
  uint64_t device_key = (uint64_t)rs.device * 7 + (rs.gpu_with_cpu ? 3 : 0) + (rs.hardware_rt ? 11 : 0);
  for (int g : ps.gpus) device_key = device_key * 131 + (uint64_t)g + 1;
  if (hash != vp_pt_hash_ || (std::strcmp(vp_pt_.ray_backend(), "Embree") == 0) != want_embree || vp_pt_guiding_ != rs.path_guiding ||
      device_key != vp_pt_device_key_) {
    vp_pt_guiding_ = rs.path_guiding;
    vp_pt_device_key_ = device_key;
    LightingEnv env = make_lighting(cam_.position(), scene_lighting_);
    Environment world = env_;
    if (!scene_lighting_) {
      world = Environment();
      world.sky = world.equator = world.ground = Vec3(0.35f);
    }
    build_pt(vp_pt_, *this, items, env, world);
    vp_pt_hash_ = hash;
    rebuilt = true;
  }
  bool cam_changed = std::memcmp(v.m, vp_pt_view_.m, sizeof(v.m)) || std::memcmp(p.m, vp_pt_proj_.m, sizeof(p.m));
  if (rebuilt || cam_changed || vp_pt_.width() != pw || vp_pt_.height() != ph) {
    vp_pt_.set_camera(v, p, pw, ph);
    vp_pt_view_ = v;
    vp_pt_proj_ = p;
    vp_pt_shown_ = 0;
  }
  vp_pt_.render(18.0, target);
  if (vp_pt_img_.width != pw || vp_pt_img_.height != ph) {
    vp_pt_img_.resize(pw, ph);
    vp_pt_shown_ = 0;
  }
  /* Resolve (denoise + tone map) only when something it depends on changed:
   * once converged, the viewport costs just the upscale below. */
  uint64_t key = 1469598103934665603ull;
  for (uint64_t part : {(uint64_t)vp_pt_.samples(), (uint64_t)rs.denoise, (uint64_t)ps.use_oidn, (uint64_t)rs.view_transform,
                        (uint64_t)std::llround(rs.exposure * 1000.0f)})
    key = (key ^ part) * 1099511628211ull;
  if (key != vp_pt_shown_) {
    vp_pt_.resolve(vp_pt_img_.pixels.data(), pw, rs.denoise);
    vp_pt_shown_ = key;
  }
  /* Upscale into the viewport (bilinear), rows in parallel. */
  JobSystem::global().parallel_for(view.h, 32, [&](int64_t yb, int64_t ye) {
    for (int64_t y = yb; y < ye; y++) {
      uint32_t *row = scene_rt_.color + (size_t)y * scene_rt_.stride;
      float fy = std::min((float)ph - 1.001f, std::max(0.0f, (y + 0.5f) / div - 0.5f));
      int y0 = (int)fy;
      float ty = fy - y0;
      const uint32_t *r0 = vp_pt_img_.row(y0), *r1 = vp_pt_img_.row(std::min(ph - 1, y0 + 1));
      for (int x = 0; x < view.w; x++) {
        float fx = std::min((float)pw - 1.001f, std::max(0.0f, (x + 0.5f) / div - 0.5f));
        int x0 = (int)fx;
        float tx = fx - x0;
        int x1 = std::min(pw - 1, x0 + 1);
        auto ch = [&](int s) {
          float a = ((r0[x0] >> s) & 255) * (1 - tx) + ((r0[x1] >> s) & 255) * tx;
          float b = ((r1[x0] >> s) & 255) * (1 - tx) + ((r1[x1] >> s) & 255) * tx;
          return (uint32_t)(a * (1 - ty) + b * ty + 0.5f);
        };
        row[x] = 0xFF000000u | (ch(16) << 16) | (ch(8) << 8) | ch(0);
      }
    }
  });
  scene_stats_ = scene_r3d_.stats();
}

/* ===================================================================== */
/* Final render (F12)                                                     */
/* ===================================================================== */

/* The camera's thin lens for the path tracer (Cycles: camera_sample_aperture). */
static PTLens camera_lens(const Camera *cam, float aspect) {
  PTLens l;
  if (!cam) return l;
  l.radius = cam->aperture_radius(aspect);
  l.focus_distance = cam->focus_distance;
  l.blades = cam->blades >= 3 ? cam->blades : 0;
  l.rotation = cam->blade_rotation * kDeg2Rad;
  return l;
}

void Editor::start_final_render(bool preview, bool open_window) {
  const RenderSettings &rs = scene_->render;
  const int pct = preview ? std::max(1, rs.percent * rs.preview_percent / 100) : rs.percent;
  int w = std::max(16, rs.width * pct / 100), h = std::max(16, rs.height * pct / 100);
  render_preview_ = preview;
  live_preview_hash_ = live_preview_hash();
  live_preview_time_ = now_seconds();
  GameObject *owner = nullptr;
  Camera *cam = main_camera(*scene_, &owner);
  /* A camera with its own aspect ratio keeps the width and sets the height. */
  if (cam) h = std::max(16, (int)std::lround(w / cam->image_aspect(w / (float)h)));
  Mat4 v, p;
  camera_for_render(v, p, w / (float)h);
  Vec3 eye = owner ? owner->world_position() : cam_.position();
  render_img_.resize(w, h);
  render_linear_.clear();
  render_start_ = now_seconds();
  render_has_result_ = true;
  if (open_window) dock_open(WindowKind::Render);
  if (rs.engine == 0) {
    /* Rasterized: supersample then box-filter down (SSAA). */
    int aa = preview ? 1 : std::max(1, std::min(4, rs.raster_aa));
    Image big;
    big.resize(w * aa, h * aa);
    RenderTarget rt;
    rt.attach(big, {0, 0, big.width, big.height});
    Renderer3D r3d;
    render_deferred(r3d, rt, v, p, eye, true, true, cam);
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) {
        uint32_t r = 0, g = 0, b = 0;
        for (int sy = 0; sy < aa; sy++)
          for (int sx = 0; sx < aa; sx++) {
            uint32_t c = big.pixels[(size_t)(y * aa + sy) * big.width + x * aa + sx];
            r += (c >> 16) & 255;
            g += (c >> 8) & 255;
            b += c & 255;
          }
        int n = aa * aa;
        render_img_.pixels[(size_t)y * w + x] = 0xFF000000u | ((r / n) << 16) | ((g / n) << 8) | (b / n);
      }
    render_time_ = now_seconds() - render_start_;
    rendering_ = false;
    render_status_ = strprintf("%s %dx%d, %dx SSAA, %.0f ms (%zu tris)", preview ? "Preview (rasterized)" : "Rasterized", w, h, aa,
                               render_time_ * 1000.0, r3d.stats().tris_submitted);
    if (!preview) Log::info("Render finished: %s", render_status_.c_str());
    return;
  }
  LightingEnv env = make_lighting(eye, true);
  std::vector<DrawItem> items = collect_items(true, true);
  PTSettings ps = make_pt_settings(rs);
  if (cam) ps.exposure += cam->exposure_stops();
  final_pt_.set_settings(ps);  // before build(): it picks the ray backend
  build_pt(final_pt_, *this, items, env, env_);
  final_pt_.set_camera(v, p, w, h, camera_lens(cam, w / (float)h));
  rendering_ = true;
  render_status_ = strprintf("%s %dx%d: %s scene of %zu tris built in %.0f ms", preview ? "Preview" : "Path tracing", w, h, final_pt_.device_summary().c_str(),
                             final_pt_.stats().triangles,
                             final_pt_.stats().bvh_build_ms);
  if (!preview) Log::info("%s", render_status_.c_str());
}

void Editor::step_final_render() {
  if (!rendering_) return;
  const RenderSettings &rs = scene_->render;
  int target = std::max(1, render_preview_ ? rs.preview_samples : rs.samples);
  int done = final_pt_.render(40.0, target);
  render_time_ = now_seconds() - render_start_;
  bool finished = done >= target;
  if (finished) {
    /* Denoise once and keep the linear result for HDR / EXR saving. */
    render_linear_ = final_pt_.linear_rgb(rs.denoise);
    final_pt_.resolve_rgb(render_linear_, render_img_.pixels.data(), render_img_.width);
  }
  else {
    final_pt_.resolve(render_img_.pixels.data(), render_img_.width, false);
  }
  /* Samples per second, and how they were shared when several devices render. */
  std::string split;
  if (!final_pt_.stats().gpu_samples.empty()) {
    split = strprintf("  (CPU %d", final_pt_.stats().cpu_samples);
    for (size_t g = 0; g < final_pt_.stats().gpu_samples.size(); g++) split += strprintf(", GPU%zu %d", g + 1, final_pt_.stats().gpu_samples[g]);
    split += ")";
  }
  render_status_ = strprintf("%s: sample %d / %d%s  |  %.1f s  |  %.1f samples/s  |  %s%s", render_preview_ ? "Preview" : "Path tracing", done,
                             target, split.c_str(), render_time_, done / std::max(1e-3, render_time_), final_pt_.device_summary().c_str(),
                             finished && rs.denoise ? strprintf("  |  denoised (%s)", final_pt_.denoise_backend()).c_str() : "");
  if (finished) {
    rendering_ = false;
    if (!render_preview_) Log::info("Render finished: %d samples in %.1f s (%s)", done, render_time_, final_pt_.device_summary().c_str());
  }
}

void Editor::save_render() {
  if (!render_has_result_ || render_img_.width == 0) {
    Log::warn("Nothing rendered yet - press Render Image (F12) first");
    return;
  }
  if (render_preview_) Log::warn("Saving a preview (%d%% resolution) - press Render for the full image", scene_->render.preview_percent);
  std::string dir = fs::join(project_root_, "Renders");
  fs::make_dirs(dir);
  std::time_t t = std::time(nullptr);
  char buf[64];
  std::strftime(buf, sizeof(buf), "render_%Y%m%d_%H%M%S", std::localtime(&t));
  int fmt = scene_->render.file_format;  // 0 PNG, 1 JPEG, 2 Radiance HDR, 3 OpenEXR
  if (fmt == 3 && !exr_available()) {
    Log::warn("This build has no OpenEXR (needs Blender's libraries); saving Radiance HDR instead");
    fmt = 2;
  }
  if (fmt >= 2 && render_linear_.empty()) {
    Log::warn("Float formats need the path-traced engine (linear light); saving PNG instead");
    fmt = 0;
  }
  static const char *exts[] = {".png", ".jpg", ".hdr", ".exr"};
  std::string path = fs::join(dir, std::string(buf) + exts[fmt]);
  /* Ask where (Blender: Image > Save As); the file type follows the name or the filter. */
  std::vector<platform::FileFilter> filters = {{"PNG", {".png"}}, {"JPEG", {".jpg", ".jpeg"}}, {"Radiance HDR", {".hdr"}}};
  if (exr_available()) filters.push_back({"OpenEXR", {".exr"}});
  int filter = std::min(fmt, (int)filters.size() - 1);
  std::string chosen;
  switch (pick_save_path("Save Render", path, filters, chosen, &filter)) {
    case PathPick::Cancelled: return;
    case PathPick::Unavailable: break;
    case PathPick::Chosen: {
      path = chosen;
      const std::string ext = fs::extension(path);
      int by_name = ext == ".png" ? 0 : (ext == ".jpg" || ext == ".jpeg") ? 1 : ext == ".hdr" ? 2 : ext == ".exr" && exr_available() ? 3 : -1;
      fmt = by_name >= 0 ? by_name : filter;
      if (fmt >= 2 && render_linear_.empty()) {
        Log::warn("Float formats need the path-traced engine (linear light); saving PNG instead");
        fmt = 0;
        path = fs::join(fs::parent(path), fs::stem(path) + ".png");
      }
      break;
    }
  }
  const int w = render_img_.width, h = render_img_.height;
  bool ok = fmt == 0   ? write_png(path, render_img_.pixels.data(), w, h, w)
            : fmt == 1 ? write_jpeg(path, render_img_.pixels.data(), w, h, w, scene_->render.jpeg_quality)
            : fmt == 2 ? write_hdr(path, render_linear_.data(), w, h)
                       : write_exr(path, render_linear_.data(), w, h, true);
  if (ok) Log::info("Saved render: %s", path.c_str());
  else Log::error("Could not write %s", path.c_str());
  project_listed_ = -100;
}

/* What a render through one camera depends on: geometry, lights, materials,
 * the camera and every render / world setting. */
uint64_t Editor::camera_render_hash(const GameObject *owner, Camera *cam) {
  uint64_t h = scene_render_hash();
  if (owner && cam) {
    Mat4 w = owner->world_matrix();
    uint64_t c = hash_component(*cam);
    h ^= c + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    for (float f : w.m) {
      uint32_t b;
      std::memcpy(&b, &f, 4);
      h = (h ^ b) * 1099511628211ull;
    }
  }
  h ^= hash_reflect([this](Reflector &r) { scene_->render.reflect(r); }) * 31;
  h ^= hash_reflect([this](Reflector &r) { scene_->environment.reflect(r); }) * 131;
  return h;
}

uint64_t Editor::live_preview_hash() {
  GameObject *owner = nullptr;
  Camera *cam = main_camera(*scene_, &owner);
  return camera_render_hash(owner, cam);
}

/* Unity's Camera Preview: selecting a camera shows what it sees in a corner
 * of the Scene view. Shaded is the rasterizer (cheap); Rendered runs the
 * render engine at the inset's size, progressively, like a small render
 * preview (path traced with the camera's depth of field and exposure). */
void Editor::draw_camera_preview(const Recti &view) {
  /* Locked: the same camera stays up whatever is selected, in Edit Mode too,
   * so the shot can be watched while the mesh is changed. */
  GameObject *g = cam_preview_lock_ ? scene_->find(cam_preview_lock_) : active_object();
  if (cam_preview_lock_ && (!g || !g->get<Camera>())) {
    cam_preview_lock_ = 0;
    g = active_object();
  }
  Camera *cam = g ? g->get<Camera>() : nullptr;
  cam_preview_rect_ = Recti{};
  if (!cam || !cam->enabled || (edit_mode_ && !cam_preview_lock_) || (g && g->id == pilot_cam_)) {  // piloting: the view is the preview
    cam_preview_pt_hash_ = 0;
    return;
  }
  auto &u = ui_;
  const RenderSettings &rs = scene_->render;
  const float aspect = cam->image_aspect(rs.width / (float)std::max(1, rs.height));
  int w = std::max(u.px(120), std::min(u.px(360), view.w / 4)), h = (int)(w / aspect);
  if (h > view.h / 3) {
    h = view.h / 3;
    w = (int)(h * aspect);
  }
  if (w < 32 || h < 24) return;
  Quat q = g->world_rotation();
  Vec3 eye = g->world_position();
  Mat4 v = Mat4::look_at(eye, eye + q.rotate({0, 0, 1}), q.rotate({0, 1, 0}));
  Mat4 p = cam->projection(aspect);
  const bool traced = cam_preview_rendered_;  // Rendered is always the path tracer, whatever the final engine
  std::string status;
  if (traced) {
    /* Rebuild when the scene, this camera or the inset size changes; then add
     * a few milliseconds of samples every frame until the preview count. */
    uint64_t hash = camera_render_hash(g, cam) ^ ((uint64_t)w << 40) ^ ((uint64_t)h << 20) ^ g->id;
    if (hash != cam_preview_pt_hash_) {
      cam_preview_pt_hash_ = hash;
      LightingEnv env = make_lighting(eye, true);
      std::vector<DrawItem> items = collect_items(true, true);
      PTSettings ps = make_pt_settings(rs);
      ps.exposure += cam->exposure_stops();
      cam_preview_pt_.set_settings(ps);
      build_pt(cam_preview_pt_, *this, items, env, env_);
      cam_preview_pt_.set_camera(v, p, w, h, camera_lens(cam, aspect));
      cam_preview_done_ = false;
    }
    if (cam_preview_img_.width != w || cam_preview_img_.height != h) cam_preview_img_.resize(w, h);  // keeps a finished preview
    const int target = std::max(1, rs.preview_samples);
    if (!cam_preview_done_) {
      int done = cam_preview_pt_.render(12.0, target);
      if (done >= target) {
        std::vector<float> lin = cam_preview_pt_.linear_rgb(rs.denoise);
        cam_preview_pt_.resolve_rgb(lin, cam_preview_img_.pixels.data(), w);
        cam_preview_done_ = true;
      }
      else {
        cam_preview_pt_.resolve(cam_preview_img_.pixels.data(), w, false);
        u.redraw = true;
      }
    }
    status = strprintf("  %d / %d", std::min(cam_preview_pt_.samples(), target), target);
  }
  else {
    cam_preview_pt_hash_ = 0;
    cam_preview_img_.resize(w, h);
    cam_preview_rt_.attach(cam_preview_img_, {0, 0, w, h});
    render_deferred(cam_preview_r3d_, cam_preview_rt_, v, p, eye, true, true, cam);
  }
  Recti box{view.right() - w - u.px(12), view.bottom() - h - u.px(12) - u.row_h(), w, h};
  cam_preview_rect_ = {box.x - u.px(4), box.y - u.row_h() - u.px(4), w + u.px(8), h + u.row_h() + u.px(8)};  // clicks here stay off the scene
  u.canvas.fill_rect({box.x - u.px(4), box.y - u.row_h() - u.px(4), w + u.px(8), h + u.row_h() + u.px(8)}, Color::hex(0x222222, 230));
  /* Header: the camera's name, then a Shaded / Rendered switch. */
  const char *mode = cam_preview_rendered_ ? "Rendered" : "Shaded";
  int bw = u.font.text_width("Rendered") + u.px(12), lw = u.font.text_width("Locked") + u.px(12);
  Recti mb{box.right() - bw, box.y - u.row_h() - u.px(2), bw, u.row_h()};
  Recti lb{mb.x - lw - u.px(4), mb.y, lw, mb.h};
  u.label({box.x, box.y - u.row_h(), w - bw - lw - u.px(8), u.row_h()}, g->name + status, u.theme.text);
  if (u.button(lb, cam_preview_lock_ ? "Locked" : "Lock", cam_preview_lock_ != 0)) cam_preview_lock_ = cam_preview_lock_ ? 0 : g->id;
  u.tooltip("Lock: keep this camera's preview up while you select other objects and edit meshes\n"
            "(Edit Mode too), so you can see the shot change as you model. Click again to unlock.");
  if (u.button(mb, mode, cam_preview_rendered_)) {
    cam_preview_rendered_ = !cam_preview_rendered_;
    cam_preview_pt_hash_ = 0;
  }
  u.tooltip("Click to switch. Shaded: the fast rasterized view.\nRendered: a small path-traced render preview through this camera\n"
            "(depth of field, exposure, Preview Samples; denoised when done).");
  Image *fb = u.canvas.target();
  Recti dst = box.intersect(view);
  for (int y = dst.y; y < dst.bottom(); y++)
    std::memcpy(fb->row(y) + dst.x, cam_preview_img_.row(y - box.y) + (dst.x - box.x), sizeof(uint32_t) * dst.w);
  u.canvas.rect_outline(box, Color::hex(0x101010));
}

}  // namespace bl

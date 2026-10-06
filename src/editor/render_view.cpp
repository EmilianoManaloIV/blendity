// SPDX-License-Identifier: GPL-2.0-or-later
// Rendering glue between the scene and the two render engines:
//   * Shaded viewport / Game view: deferred PBR rasterizer + sun shadow map
//     (Blender: EEVEE "Material Preview"/"Rendered"; Unity: Scene view "Shaded")
//   * Rendered viewport + final render: progressive path tracer (Blender: Cycles)
//   * World environment: gradient / Hosek-Wilkie sky / HDRI / colour
#include "editor.h"

#include "../core/core.h"
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
    bool editing = !game && edit_mode_ && g.id == edit_obj_;
    const Mesh *m = editing ? mf->mesh.get() : g.evaluated_mesh();
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

LightingEnv Editor::make_lighting(Vec3 eye, bool use_scene_lights) {
  LightingEnv env;
  const EnvironmentSettings &es = scene_->environment;
  env.sky = es.sky;
  env.equator = es.equator;
  env.ground = es.ground;
  env.camera_pos = eye;
  env.view_transform = (ViewTransform)scene_->render.view_transform;
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
    RenderLight rl;
    rl.type = l->type == 1 ? RenderLight::Point : RenderLight::Directional;
    rl.direction = normalize(g.world_rotation().rotate({0, 0, 1}));
    rl.position = g.world_position();
    rl.color = l->color;
    rl.intensity = l->intensity;
    rl.range = l->range;
    env.lights.push_back(rl);
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
      const Mesh *m = g.evaluated_mesh();
      mix(&m, sizeof(m));
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
  proj = cam->orthographic ? Mat4::ortho(cam->ortho_size, aspect, cam->near_clip, cam->far_clip)
                           : Mat4::perspective(cam->fov * kDeg2Rad, aspect, cam->near_clip, cam->far_clip);
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

static PTSettings pt_settings(const RenderSettings &rs) {
  PTSettings s;
  s.max_bounces = rs.max_bounces;
  s.clamp_indirect = rs.clamp_indirect;
  s.denoise = rs.denoise;
  s.view_transform = (ViewTransform)rs.view_transform;
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
  const int div = 2;
  int pw = std::max(1, view.w / div), ph = std::max(1, view.h / div);
  uint64_t hash = scene_render_hash();
  bool rebuilt = false;
  if (hash != vp_pt_hash_) {
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
  vp_pt_.set_settings(pt_settings(scene_->render));
  bool cam_changed = std::memcmp(v.m, vp_pt_view_.m, sizeof(v.m)) || std::memcmp(p.m, vp_pt_proj_.m, sizeof(p.m));
  if (rebuilt || cam_changed || vp_pt_.width() != pw || vp_pt_.height() != ph) {
    vp_pt_.set_camera(v, p, pw, ph);
    vp_pt_view_ = v;
    vp_pt_proj_ = p;
  }
  int target = std::max(1, scene_->render.viewport_samples);
  vp_pt_.render(18.0, target);
  if (vp_pt_img_.width != pw || vp_pt_img_.height != ph) vp_pt_img_.resize(pw, ph);
  vp_pt_.resolve(vp_pt_img_.pixels.data(), pw, scene_->render.denoise);
  /* Upscale into the viewport (bilinear). */
  for (int y = 0; y < view.h; y++) {
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
  scene_stats_ = scene_r3d_.stats();
}

/* ===================================================================== */
/* Final render (F12)                                                     */
/* ===================================================================== */

void Editor::start_final_render() {
  const RenderSettings &rs = scene_->render;
  int w = std::max(16, rs.width * rs.percent / 100), h = std::max(16, rs.height * rs.percent / 100);
  Mat4 v, p;
  camera_for_render(v, p, w / (float)h);
  GameObject *owner = nullptr;
  Camera *cam = main_camera(*scene_, &owner);
  Vec3 eye = owner ? owner->world_position() : cam_.position();
  render_img_.resize(w, h);
  render_linear_.clear();
  render_start_ = now_seconds();
  render_has_result_ = true;
  dock_open(WindowKind::Render);
  if (rs.engine == 0) {
    /* Rasterized: supersample then box-filter down (SSAA). */
    int aa = std::max(1, std::min(4, rs.raster_aa));
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
    render_status_ = strprintf("Rasterized %dx%d, %dx SSAA, %.0f ms (%zu tris)", w, h, aa, render_time_ * 1000.0, r3d.stats().tris_submitted);
    Log::info("Render finished: %s", render_status_.c_str());
    return;
  }
  LightingEnv env = make_lighting(eye, true);
  std::vector<DrawItem> items = collect_items(true, true);
  build_pt(final_pt_, *this, items, env, env_);
  final_pt_.set_settings(pt_settings(rs));
  final_pt_.set_camera(v, p, w, h);
  rendering_ = true;
  render_status_ = strprintf("Path tracing %dx%d: BVH %zu tris in %.0f ms", w, h, final_pt_.stats().triangles, final_pt_.stats().bvh_build_ms);
  Log::info("%s", render_status_.c_str());
}

void Editor::step_final_render() {
  if (!rendering_) return;
  const RenderSettings &rs = scene_->render;
  int target = std::max(1, rs.samples);
  int done = final_pt_.render(40.0, target);
  render_time_ = now_seconds() - render_start_;
  bool finished = done >= target;
  final_pt_.resolve(render_img_.pixels.data(), render_img_.width, finished && rs.denoise);
  render_status_ = strprintf("Path tracing: sample %d / %d  |  %.1f s  |  %.1f Mrays/s%s", done, target, render_time_,
                             final_pt_.stats().mrays_per_s(), finished ? (rs.denoise ? "  |  denoised" : "") : "");
  if (finished) {
    rendering_ = false;
    render_linear_ = final_pt_.linear_rgb(rs.denoise);
    Log::info("Render finished: %d samples in %.1f s", done, render_time_);
  }
}

void Editor::save_render() {
  if (!render_has_result_ || render_img_.width == 0) {
    Log::warn("Nothing rendered yet - press Render Image (F12) first");
    return;
  }
  std::string dir = fs::join(project_root_, "Renders");
  fs::make_dirs(dir);
  std::time_t t = std::time(nullptr);
  char buf[64];
  std::strftime(buf, sizeof(buf), "render_%Y%m%d_%H%M%S", std::localtime(&t));
  int fmt = scene_->render.file_format;
  if (fmt == 2 && render_linear_.empty()) {
    Log::warn("Radiance HDR needs the path-traced engine (linear light); saving PNG instead");
    fmt = 0;
  }
  std::string path = fs::join(dir, std::string(buf) + (fmt == 0 ? ".png" : fmt == 1 ? ".jpg" : ".hdr"));
  bool ok = fmt == 0   ? write_png(path, render_img_.pixels.data(), render_img_.width, render_img_.height, render_img_.width)
            : fmt == 1 ? write_jpeg(path, render_img_.pixels.data(), render_img_.width, render_img_.height, render_img_.width, scene_->render.jpeg_quality)
                       : write_hdr(path, render_linear_.data(), render_img_.width, render_img_.height);
  if (ok) Log::info("Saved render: %s", path.c_str());
  else Log::error("Could not write %s", path.c_str());
  project_listed_ = -100;
}

}  // namespace bl

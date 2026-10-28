// SPDX-License-Identifier: GPL-2.0-or-later
// Paint Mode (ADR 0081, task 0245): the brush paints a surface's Paint layer by projection in the Scene view - the
// triangles under the brush's disc, each one's texels placed by barycentrics, projected and depth-tested against the
// view's planes - a stroke building up by flow to its opacity over the tiles as they were, one undo step of the 64 x 64
// tiles it touched; fills by polygroup, island, material or object; the material's albedo texture written during the
// stroke so the views follow the dabs.
#include "editor.h"
#include "../core/core.h"
#include "../render/display.h"
#include "../texture/paint_layer.h"
#include "../texture/surface.h"
#include "../texture/smart_material.h"

namespace bl {
bool surface_stack_writes_emission(const ProceduralTexture &pt);  // (surface_ui.cpp)
}

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace bl {

namespace {

constexpr int kPaintTile = 64;

inline float paint_falloff(float t, float hardness) {  // (t: 0 at the centre, 1 at the edge; hardness: how far the full weight reaches)
  const float h = std::max(0.0f, std::min(0.99f, hardness));
  if (t <= h) return 1.0f;
  const float s = (t - h) / (1.0f - h);
  return 1.0f - s * s * (3.0f - 2.0f * s);
}

inline uint32_t pack_srgb(Vec3 c) {
  auto ch = [](float v) { return (uint32_t)display::linear_to_srgb8(std::max(0.0f, std::min(1.0f, v))); };
  return 0xFF000000u | ch(c.x) << 16 | ch(c.y) << 8 | ch(c.z);
}
inline uint32_t to_rgba8(uint32_t argb) { return 0xFF000000u | ((argb >> 16) & 0xFF) | (argb & 0xFF00u) | (argb & 0xFF) << 16; }  // (Texture::Level packs R low)
inline uint8_t lerp8(uint8_t a, float b, float k) { return (uint8_t)std::lround(a + (b * 255.0f - a) * k); }
inline uint32_t lerp_argb(uint32_t a, uint32_t b, float k) {
  uint32_t out = 0xFF000000u;
  for (int s = 0; s < 24; s += 8) out |= (uint32_t)std::lround(((a >> s) & 0xFF) + (((b >> s) & 0xFF) - (float)((a >> s) & 0xFF)) * k) << s;
  return out;
}

/* The island flood for a fill: the triangles joined to `face`'s through corners at the same UV. */
void island_triangles(const RenderMesh &rm, const Mesh &mesh, uint32_t face, std::vector<uint8_t> &take) {
  std::map<std::pair<int64_t, int64_t>, std::vector<uint32_t>> at;
  auto key = [&](uint32_t c) { return std::make_pair((int64_t)std::llround(mesh.uvs[c].x * 1e5), (int64_t)std::llround(mesh.uvs[c].y * 1e5)); };
  for (size_t t = 0; t < rm.tri_count(); t++)
    for (int k = 0; k < 3; k++) at[key(rm.tri_corner[t * 3 + k])].push_back((uint32_t)t);
  std::vector<uint32_t> stack;
  for (size_t t = 0; t < rm.tri_count(); t++)
    if (rm.tri_face[t] == face) stack.push_back((uint32_t)t), take[t] = 1;
  while (!stack.empty()) {
    const uint32_t t = stack.back();
    stack.pop_back();
    for (int k = 0; k < 3; k++)
      for (uint32_t o : at[key(rm.tri_corner[t * 3 + k])])
        if (!take[o]) take[o] = 1, stack.push_back(o);
  }
}

}  // namespace

/* ===================================================================== */
/* The mode                                                               */
/* ===================================================================== */

bool Editor::set_paint(bool on) {
  if (on && paint_ && active_ == paint_obj_ && scene_.get() == paint_scene_) return true;
  if (paint_) {
    paint_abort_stroke();
    paint_ = false, paint_obj_ = 0, paint_scene_ = nullptr, paint_layer_ = -1;
    paint_surface_.reset();
    if (!on) Log::info("Paint off");
  }
  if (!on) return true;
  GameObject *g = active_object();
  MeshFilter *mf = g ? g->mesh_filter() : nullptr;
  if (!mf || !mf->mesh) {
    Log::warn("Paint: select a mesh object first");
    return false;
  }
  if (playing_) {
    Log::warn("Paint: stop Play first");
    return false;
  }
  std::string why;
  ProceduralTexturePtr pt = surface_of(g, &why);
  if (!pt) {
    Log::warn("Paint: %s", why.c_str());
    return false;
  }
  if (mf->mesh->uvs.size() != mf->mesh->corner_verts.size()) {
    Log::warn("Paint: %s has no UVs (surface new lays them out)", g->name.c_str());
    return false;
  }
  const Mesh *ev = g->evaluated_mesh();
  if (ev && ev != mf->mesh.get() && (ev->vert_count() != mf->mesh->vert_count() || ev->face_count() != mf->mesh->face_count())) {
    Log::warn("Paint: %s's modifiers add or remove geometry; apply them first (the brush projects onto the base mesh)", g->name.c_str());
    return false;
  }
  if (edit_mode_) exit_edit_mode();
  if (weight_paint_) set_weight_paint(false);
  if (sculpt_) set_sculpt(false);
  if (sketch_.active) sketch_end();
  commit_undo();
  paint_ = true, paint_obj_ = g->id, paint_scene_ = scene_.get(), paint_layer_ = -1;
  paint_surface_ = pt;
  Log::info("Paint on '%s': drag over it; the pen's eraser end erases; Esc leaves", g->name.c_str());
  ui_.redraw = true;
  return true;
}

/* The Paint layer the brush writes: the chosen one, else the top Paint layer, else a new one on top. */
PaintLayer *Editor::paint_layer(bool make) {
  if (!paint_surface_) return nullptr;
  ProceduralTexture &pt = *paint_surface_;
  auto as_paint = [&](int i) { return i >= 0 && i < (int)pt.layers.size() && std::string(pt.layers[(size_t)i]->type_name()) == "Paint" ? static_cast<PaintLayer *>(pt.layers[(size_t)i].get()) : nullptr; };
  if (PaintLayer *l = as_paint(paint_layer_)) return l;
  for (int i = (int)pt.layers.size() - 1; i >= 0; i--)
    if (PaintLayer *l = as_paint(i)) {
      paint_layer_ = i;
      return l;
    }
  if (!make) return nullptr;
  note_texture_edit(pt);
  pt.add("Paint");
  paint_layer_ = (int)pt.layers.size() - 1;
  PaintLayer *l = as_paint(paint_layer_);
  if (l) {  // (a base no files sit under: another layer's PNGs must not come back as this one's paint)
    const std::string stem = "Assets/Textures/" + pt.name + "_L" + std::to_string(paint_layer_);
    l->base = stem;
    for (int k = 2; fs::exists(resolve_asset_path(l->base + "_color.png")) || paint_images_by_base_.count(l->base); k++) l->base = stem + "_" + std::to_string(k);
  }
  mark_changed("Add Paint Layer");
  commit_undo();  // (the layer's step stands before the stroke's)
  return l;
}

/* The layer's images, held by the editor by their file base as well: a texture rebuilt from text (an undo, a redo)
 * takes them back instead of reloading a PNG that may hold strokes the history has not. */
PaintImages &Editor::paint_layer_images(PaintLayer &l, int size) {
  if (!l.images || l.images->empty()) {
    auto kept = paint_images_by_base_.find(l.base);
    if (kept != paint_images_by_base_.end() && kept->second && !kept->second->empty()) l.images = kept->second;
  }
  PaintImages &im = l.ensure_images(size);
  if (!l.base.empty()) paint_images_by_base_[l.base] = l.images;
  return im;
}

void Editor::paint_reattach_images(ProceduralTexture &pt) {
  for (auto &l : pt.layers) {
    if (std::string(l->type_name()) != "Paint") continue;
    PaintLayer &pl = static_cast<PaintLayer &>(*l);
    auto kept = paint_images_by_base_.find(pl.base);
    if (kept != paint_images_by_base_.end()) pl.images = kept->second;
  }
}

/* The object and depth under a Scene view pixel, for the painted object alone (sculpt_hit's walk of the depth plane). */
bool Editor::paint_hit(const Recti &view, float x, float y, Vec3 &world, float &dist) {
  const uint64_t keep = sculpt_obj_;
  sculpt_obj_ = paint_obj_;
  const bool ok = sculpt_hit(view, x, y, world, dist);
  sculpt_obj_ = keep;
  return ok;
}

/* The painted object's face under a Scene view pixel (the pixel's ray against its render mesh). */
uint32_t Editor::paint_face_under(float x, float y) {
  GameObject *g = scene_->find(paint_obj_);
  MeshFilter *mf = g ? g->mesh_filter() : nullptr;
  if (!mf || !mf->mesh) return UINT32_MAX;
  const Mat4 inv = g->world_matrix().inverse();
  const Ray ray = scene_r3d_.screen_ray(x, y);
  const Ray lr{inv.point(ray.origin), normalize(inv.dir(ray.dir))};
  const RenderMesh &rm = mf->mesh->render_mesh();
  float best = 1e30f;
  uint32_t face = UINT32_MAX;
  for (size_t t = 0; t < rm.tri_count() && t < rm.tri_face.size(); t++) {
    const float d = ray_triangle(lr, rm.positions[rm.indices[t * 3]], rm.positions[rm.indices[t * 3 + 1]], rm.positions[rm.indices[t * 3 + 2]]);
    if (d > 0.0f && d < best) best = d, face = rm.tri_face[t];
  }
  return face;
}

/* The brush's alpha image at a point of the disc (-1..1 across it): its luminance. */
float Editor::paint_alpha_at(Vec2 uv) const {
  const Bitmap &a = paint_alpha_;
  if (a.width <= 0 || a.height <= 0) return 1.0f;
  if (uv.x < -1.0f || uv.x > 1.0f || uv.y < -1.0f || uv.y > 1.0f) return 0.0f;
  const int x = std::max(0, std::min(a.width - 1, (int)((uv.x * 0.5f + 0.5f) * a.width))), y = std::max(0, std::min(a.height - 1, (int)((uv.y * 0.5f + 0.5f) * a.height)));
  const uint8_t *px = &a.rgba8[((size_t)y * a.width + x) * 4];
  return (px[0] + px[1] + px[2]) / (3.0f * 255.0f);
}

/* ===================================================================== */
/* Strokes                                                                */
/* ===================================================================== */

/* A stroke's start: the layer's images, the delta, the per-texel stroke tables (the weight built so far, the dab that
 * last wrote the texel, what this stroke wrote - for the dilation). */
bool Editor::paint_begin_stroke_images() {
  PaintLayer *l = paint_layer(true);
  if (!l) return false;
  PaintImages &im = paint_layer_images(*l, std::max(paint_surface_->width, paint_surface_->height));
  paint_images_ = l->images;
  paint_delta_ = std::make_shared<PaintDelta>();
  paint_delta_->asset = paint_surface_->asset_path, paint_delta_->layer = paint_layer_, paint_delta_->stamp_before = l->stamp;
  paint_tiles_x_ = (im.w + kPaintTile - 1) / kPaintTile, paint_tiles_y_ = (im.h + kPaintTile - 1) / kPaintTile;
  paint_tile_slot_.assign((size_t)paint_tiles_x_ * paint_tiles_y_, 0);
  paint_stroke_alpha_.assign(im.size(), 0);
  paint_stroke_dab_.assign(im.size(), 0);
  paint_written_.assign(im.size(), 0);
  paint_dab_serial_ = 0;
  paint_rows_y0_ = im.h, paint_rows_y1_ = 0;
  paint_dab_rows_y0_ = im.h, paint_dab_rows_y1_ = 0;
  paint_view_tex_ = procedural_texture_output(*paint_surface_, 0);  // (the material's albedo: written as the stroke goes)
  paint_stroking_ = true, paint_dabbed_ = false;
  return true;
}

bool Editor::paint_begin_stroke(const Recti &view, float x, float y) {
  Vec3 hit;
  float dist;
  if (!paint_hit(view, x, y, hit, dist)) return false;
  if (!paint_begin_stroke_images()) return false;
  paint_lazy_px_ = paint_last_px_ = {x, y};
  return true;
}

void Editor::paint_stroke_to(const Recti &view, float x, float y, bool first) {
  const float pen = std::isfinite(ui_.in.pressure) ? ui_.in.pressure : 1.0f;
  Vec2 to(x, y);
  if (paint_brush_.lazy > 0.0f && !first) {
    const Vec2 d = to - paint_lazy_px_;
    const float l = length(d);
    if (l > paint_brush_.lazy) paint_lazy_px_ += d * (1.0f - paint_brush_.lazy / l);
    to = paint_lazy_px_;
  }
  if (first) {
    paint_dab(view, to.x, to.y, pen);
    paint_last_px_ = to;
    return;
  }
  const float step = std::max(1.0f, paint_brush_.radius * ui_.scale * std::max(0.05f, paint_brush_.spacing));
  const Vec2 d = to - paint_last_px_;
  const float len = length(d);
  if (len < step) return;
  const int k = std::min(64, std::max(1, (int)(len / step)));
  for (int i = 1; i <= k && paint_stroking_; i++) {
    const Vec2 at = paint_last_px_ + d * (i / (float)k);
    paint_dab(view, at.x, at.y, pen);
  }
  paint_last_px_ = to;
}

/* A tile's pixels kept before the stroke's first write to it (the undo's "before"); its slot + 1 returned. */
uint32_t Editor::paint_touch_tile(int tx, int ty) {
  const size_t slot = (size_t)ty * paint_tiles_x_ + tx;
  if (paint_tile_slot_[slot]) return paint_tile_slot_[slot];
  const PaintImages &im = *paint_images_;
  PaintDelta::Tile t;
  t.x0 = tx * kPaintTile, t.y0 = ty * kPaintTile;
  t.w = std::min(kPaintTile, im.w - t.x0), t.h = std::min(kPaintTile, im.h - t.y0);
  const size_t n = (size_t)t.w * t.h;
  t.before.color.resize(n), t.before.emit.resize(n), t.before.rough.resize(n), t.before.metal.resize(n), t.before.height.resize(n), t.before.cover.resize(n), t.before.channels.resize(n);
  for (int y = 0; y < t.h; y++) {
    const size_t src = (size_t)(t.y0 + y) * im.w + t.x0, dst = (size_t)y * t.w;
    std::memcpy(&t.before.color[dst], &im.color[src], t.w * sizeof(uint32_t));
    std::memcpy(&t.before.emit[dst], &im.emit[src], t.w * sizeof(uint32_t));
    std::memcpy(&t.before.rough[dst], &im.rough[src], t.w), std::memcpy(&t.before.metal[dst], &im.metal[src], t.w);
    std::memcpy(&t.before.height[dst], &im.height[src], t.w), std::memcpy(&t.before.cover[dst], &im.cover[src], t.w), std::memcpy(&t.before.channels[dst], &im.channels[src], t.w);
  }
  paint_delta_->tiles.push_back(std::move(t));
  paint_tile_slot_[slot] = (uint32_t)paint_delta_->tiles.size();
  return paint_tile_slot_[slot];
}

/* One texel written by a dab with weight k (the falloff, the alpha, the pressure): the stroke's weight at the texel
 * builds by Flow up to its Opacity and the brush's channels are composited by it against the texel as it was before the
 * stroke (Substance's and Blender's model: overlapping dabs don't pile up past the opacity); a dab writes a texel once.
 * An erase takes the coverage down the same way. */
void Editor::paint_write_texel(size_t i, float k) {
  PaintImages &im = *paint_images_;
  if (paint_stroke_dab_[i] == paint_dab_serial_) return;  // (two triangles of one dab share the texel)
  paint_stroke_dab_[i] = paint_dab_serial_;
  const int x = (int)(i % im.w), y = (int)(i / im.w);
  const uint32_t slot = paint_touch_tile(x / kPaintTile, y / kPaintTile);
  const PaintDelta::Tile &tile = paint_delta_->tiles[slot - 1];
  const size_t b = (size_t)(y - tile.y0) * tile.w + (x - tile.x0);  // (the texel before the stroke)
  paint_rows_y0_ = std::min(paint_rows_y0_, y), paint_rows_y1_ = std::max(paint_rows_y1_, y + 1);
  paint_dab_rows_y0_ = std::min(paint_dab_rows_y0_, y), paint_dab_rows_y1_ = std::max(paint_dab_rows_y1_, y + 1);
  const PaintBrush &br = paint_brush_;
  const float opacity = std::max(0.0f, std::min(1.0f, br.opacity)), flow = std::max(0.0f, std::min(1.0f, br.flow));
  const float a = std::min(opacity, paint_stroke_alpha_[i] / 255.0f + flow * k);  // (the stroke's weight here, so far)
  paint_stroke_alpha_[i] = (uint8_t)std::lround(a * 255.0f);
  paint_written_[i] = 1;
  if (paint_erase_) {
    im.cover[i] = (uint8_t)std::lround(tile.before.cover[b] * (1.0f - a));
    if (!im.cover[i]) im.channels[i] = 0;
    return;
  }
  /* Composited against the texel before the stroke: a texel that held no paint takes the brush's values straight. */
  const bool fresh = tile.before.cover[b] == 0;
  const uint8_t was_ch = tile.before.channels[b];
  auto chan = [&](PaintChannel c) { return (br.channels & paint_channel_bit(c)) != 0; };
  auto had = [&](PaintChannel c) { return !fresh && (was_ch & paint_channel_bit(c)); };
  if (chan(PaintColor)) im.color[i] = had(PaintColor) ? lerp_argb(tile.before.color[b], pack_srgb(br.color), a) : pack_srgb(br.color);
  if (chan(PaintRough)) im.rough[i] = had(PaintRough) ? lerp8(tile.before.rough[b], br.roughness, a) : (uint8_t)std::lround(br.roughness * 255.0f);
  if (chan(PaintMetal)) im.metal[i] = had(PaintMetal) ? lerp8(tile.before.metal[b], br.metallic, a) : (uint8_t)std::lround(br.metallic * 255.0f);
  if (chan(PaintHeight)) im.height[i] = had(PaintHeight) ? lerp8(tile.before.height[b], br.height, a) : (uint8_t)std::lround(br.height * 255.0f);
  if (chan(PaintEmit)) im.emit[i] = had(PaintEmit) ? lerp_argb(tile.before.emit[b], pack_srgb(br.emission), a) : pack_srgb(br.emission);
  im.cover[i] = std::max(tile.before.cover[b], (uint8_t)std::lround(a * 255.0f));
  im.channels[i] = (uint8_t)(was_ch | (br.channels & ((1 << PaintChannels) - 1)));
  /* The view follows the dab: the material's albedo texture takes the colour here (the stack lands when the stroke ends). */
  if (paint_view_tex_ && !paint_view_tex_->levels.empty() && paint_view_tex_->levels[0].w == im.w && paint_view_tex_->levels[0].h == im.h && chan(PaintColor))
    const_cast<Texture &>(*paint_view_tex_).levels[0].px8[i] = to_rgba8(im.color[i]);  // (the cache's own image, written in place on purpose: the preview of the stroke)
}

/* The texels of a triangle inside the brush's disc at `centre` (screen) of `radius` px, written with the falloff;
 * each texel's world point by barycentrics, projected and kept when its depth is within the triangle's own depth slope
 * (two texels' worth) of the depth plane there - the far side and anything behind are not painted. `all` paints the
 * whole triangle (a fill). */
void Editor::paint_triangle(const RenderMesh &rm, const Mesh &mesh, size_t tri, const Mat4 &W, Vec2 centre, float radius, float k_max, bool all) {
  PaintImages &im = *paint_images_;
  const uint32_t *ix = &rm.indices[tri * 3], *cn = &rm.tri_corner[tri * 3];
  if (cn[0] >= mesh.uvs.size() || cn[1] >= mesh.uvs.size() || cn[2] >= mesh.uvs.size()) return;
  const Vec3 p0 = W.point(rm.positions[ix[0]]), p1 = W.point(rm.positions[ix[1]]), p2 = W.point(rm.positions[ix[2]]);
  Vec2 s0, s1, s2;
  float d0 = 0.0f, d1, d2;
  float slope = 0.0f;
  if (!all) {
    if (!scene_r3d_.project(p0, s0, d0) || !scene_r3d_.project(p1, s1, d1) || !scene_r3d_.project(p2, s2, d2)) return;  // (a corner behind the camera: left alone)
    const float x0 = std::min({s0.x, s1.x, s2.x}), x1 = std::max({s0.x, s1.x, s2.x}), y0 = std::min({s0.y, s1.y, s2.y}), y1 = std::max({s0.y, s1.y, s2.y});
    if (x1 < centre.x - radius || x0 > centre.x + radius || y1 < centre.y - radius || y0 > centre.y + radius) return;
    /* The depth tolerance: the triangle's depth range over its screen footprint, two texels of it. */
    const float foot = std::max(1.0f, std::max(x1 - x0, y1 - y0));
    slope = (std::max({d0, d1, d2}) - std::min({d0, d1, d2})) / foot;
  }
  const float tol = slope * 2.0f + std::fabs(d0) * 1e-4f + 1e-6f;
  /* The UV triangle in texels (v down the image as the layout's rows run); the slack a quarter texel, whatever the
   * triangle's size (no gap and no spill between neighbours). */
  const Vec2 t0 = Vec2(mesh.uvs[cn[0]].x * im.w, (1.0f - mesh.uvs[cn[0]].y) * im.h), t1 = Vec2(mesh.uvs[cn[1]].x * im.w, (1.0f - mesh.uvs[cn[1]].y) * im.h),
             t2 = Vec2(mesh.uvs[cn[2]].x * im.w, (1.0f - mesh.uvs[cn[2]].y) * im.h);
  const float area = (t1.x - t0.x) * (t2.y - t0.y) - (t2.x - t0.x) * (t1.y - t0.y);
  if (!(std::fabs(area) > 1e-9f)) return;
  const float extent = std::max(1.0f, std::max(std::max({t0.x, t1.x, t2.x}) - std::min({t0.x, t1.x, t2.x}), std::max({t0.y, t1.y, t2.y}) - std::min({t0.y, t1.y, t2.y})));
  const float slack = 0.25f / extent;
  const int bx0 = std::max(0, (int)std::floor(std::min({t0.x, t1.x, t2.x}))), bx1 = std::min(im.w - 1, (int)std::ceil(std::max({t0.x, t1.x, t2.x})));
  const int by0 = std::max(0, (int)std::floor(std::min({t0.y, t1.y, t2.y}))), by1 = std::min(im.h - 1, (int)std::ceil(std::max({t0.y, t1.y, t2.y})));
  const float inv = 1.0f / area, r2 = radius * radius;
  for (int y = by0; y <= by1; y++)
    for (int x = bx0; x <= bx1; x++) {
      const float cx = x + 0.5f, cy = y + 0.5f;
      const float b1 = ((cx - t0.x) * (t2.y - t0.y) - (t2.x - t0.x) * (cy - t0.y)) * inv;
      const float b2 = ((t1.x - t0.x) * (cy - t0.y) - (cx - t0.x) * (t1.y - t0.y)) * inv;
      const float b0 = 1.0f - b1 - b2;
      if (b0 < -slack || b1 < -slack || b2 < -slack) continue;
      float k = k_max;
      if (!all) {
        const Vec3 wp = p0 * b0 + p1 * b1 + p2 * b2;
        Vec2 s;
        float d;
        if (!scene_r3d_.project(wp, s, d)) continue;
        const float dist2 = (s.x - centre.x) * (s.x - centre.x) + (s.y - centre.y) * (s.y - centre.y);
        if (dist2 > r2) continue;
        k *= paint_falloff(std::sqrt(dist2) / radius, paint_brush_.hardness) * paint_alpha_at({(s.x - centre.x) / radius, (s.y - centre.y) / radius}) * paint_stencil_at(s);
        const int sx = (int)s.x, sy = (int)s.y;
        if (sx < 0 || sy < 0 || sx >= scene_rt_.width || sy >= scene_rt_.height) continue;  // (off the view: not seen, not painted)
        if (scene_rt_.id_at(sx, sy) != (uint32_t)paint_obj_) continue;                      // (another object in front, or the background)
        const float plane = scene_rt_.depth_at(sx, sy);                                       // (behind the surface the view shows - the far side - whichever way the depth runs)
        if (paint_depth_grows_ ? d > plane + tol : d < plane - tol) continue;
      }
      if (k <= 0.002f) continue;
      paint_write_texel((size_t)y * im.w + x, k);
    }
}

/* The symmetry instances: the hit point mirrored across the object's planes, each a disc of its own on screen. */
std::vector<Vec3> Editor::paint_mirrors(Vec3 hit_local) const {
  std::vector<Vec3> out{hit_local};
  auto mirror = [&](int axis) {
    const size_t n = out.size();
    for (size_t i = 0; i < n; i++) {
      Vec3 m = out[i];
      (axis == 0 ? m.x : axis == 1 ? m.y : m.z) *= -1.0f;
      out.push_back(m);
    }
  };
  if (paint_brush_.sym_x) mirror(0);
  if (paint_brush_.sym_y) mirror(1);
  if (paint_brush_.sym_z) mirror(2);
  return out;
}

void Editor::paint_dab(const Recti &view, float x, float y, float pressure) {
  GameObject *g = scene_->find(paint_obj_);
  MeshFilter *mf = g ? g->mesh_filter() : nullptr;
  if (!mf || !mf->mesh || !paint_images_ || !paint_surface_) {
    paint_abort_stroke();
    return;
  }
  Vec3 hit;
  float dist;
  if (!paint_hit(view, x, y, hit, dist)) return;
  pressure = std::isfinite(pressure) ? std::max(0.0f, std::min(1.0f, pressure)) : 1.0f;
  const float weight = paint_brush_.pressure == 1 ? pressure : 1.0f;
  const float radius = std::max(1.0f, paint_brush_.radius * ui_.scale * (paint_brush_.pressure == 2 ? std::max(0.1f, pressure) : 1.0f));
  paint_erase_ = paint_brush_.erase || ui_.in.eraser;
  paint_dab_serial_++;
  paint_dab_rows_y0_ = paint_images_->h, paint_dab_rows_y1_ = 0;
  const Mat4 &W = g->world_matrix();
  const Mesh &mesh = *mf->mesh;
  const RenderMesh &rm = mesh.render_mesh();
  if (rm.tri_corner.size() != rm.indices.size()) return;
  {  // (which way the view's depth runs: the plane under the cursor against a nearer point on its ray)
    const Ray ray = scene_r3d_.screen_ray(x, y);
    Vec2 s;
    float near_d = 0.0f;
    paint_depth_grows_ = !scene_r3d_.project(ray.origin + ray.dir * (dist * 0.5f), s, near_d) || near_d < scene_rt_.depth_at((int)x, (int)y);
  }
  for (const Vec3 &local : paint_mirrors(W.inverse().point(hit))) {
    Vec2 centre;
    float cd;
    if (!scene_r3d_.project(W.point(local), centre, cd)) continue;
    for (size_t tri = 0; tri < rm.tri_count(); tri++) paint_triangle(rm, mesh, tri, W, centre, radius, weight, false);
  }
  paint_dabbed_ = true;
  if (paint_dab_rows_y1_ > paint_dab_rows_y0_) gpu_texture_rows_changed(paint_view_tex_, paint_dab_rows_y0_, paint_dab_rows_y1_);  // (this dab's rows only)
  ui_.redraw = true;
}

/* The stroke's end: the painted texels dilated two texels into their islands' unpainted neighbours (a painting stroke;
 * the tiles the dilation reaches join the step), the tiles' after taken, one undo step holding the tiles, the layer's
 * stamp bumped so the surface re-evaluates on its worker. */
void Editor::paint_end_stroke() {
  if (!paint_stroking_) return;
  paint_stroking_ = false, paint_2d_stroking_ = false;
  if (!paint_dabbed_ || !paint_images_ || !paint_delta_ || paint_delta_->tiles.empty()) {
    paint_delta_.reset();
    return;
  }
  PaintImages &im = *paint_images_;
  if (!paint_erase_) {
    const std::vector<float> *allowed = paint_surface_ && paint_surface_->maps && paint_surface_->maps->w == im.w && paint_surface_->maps->h == im.h ? &paint_surface_->maps->cover : nullptr;
    paint_dilate(im, paint_written_, allowed, 2, [&](size_t i) {
      const int x = (int)(i % im.w), y = (int)(i / im.w);
      paint_touch_tile(x / kPaintTile, y / kPaintTile);
      paint_rows_y0_ = std::min(paint_rows_y0_, y), paint_rows_y1_ = std::max(paint_rows_y1_, y + 1);
    });
  }
  size_t bytes = 0;
  for (PaintDelta::Tile &t : paint_delta_->tiles) {
    t.after = t.before;
    for (int y = 0; y < t.h; y++) {
      const size_t src = (size_t)(t.y0 + y) * im.w + t.x0, dst = (size_t)y * t.w;
      std::memcpy(&t.after.color[dst], &im.color[src], t.w * sizeof(uint32_t));
      std::memcpy(&t.after.emit[dst], &im.emit[src], t.w * sizeof(uint32_t));
      std::memcpy(&t.after.rough[dst], &im.rough[src], t.w), std::memcpy(&t.after.metal[dst], &im.metal[src], t.w);
      std::memcpy(&t.after.height[dst], &im.height[src], t.w), std::memcpy(&t.after.cover[dst], &im.cover[src], t.w), std::memcpy(&t.after.channels[dst], &im.channels[src], t.w);
    }
    bytes += 2 * (size_t)t.w * t.h * 13;
  }
  im.dirty = true;
  PaintLayer *l = paint_layer(false);
  if (l) {  // (the stamp travels in the tile step, not in a texture-text snapshot: one step a stroke)
    l->stamp++;
    paint_delta_->stamp_after = l->stamp;
    paint_surface_->touch();
  }
  commit_undo();  // (a change made during the stroke is its own step, before this one)
  UndoState s;
  s.paint = std::move(paint_delta_);
  s.label = paint_erase_ ? "Erase" : "Paint";
  s.bytes = bytes;
  paint_last_step_bytes_ = bytes;
  undo_.push_back(std::move(s));
  trim_undo();
  redo_.clear();
  scene_dirty_ = true;
  if (paint_rows_y1_ > paint_rows_y0_) gpu_texture_rows_changed(paint_view_tex_, paint_rows_y0_, paint_rows_y1_);
  paint_stroke_alpha_.clear(), paint_stroke_dab_.clear(), paint_written_.clear();
  ui_.redraw = true;
}

void Editor::paint_abort_stroke() {
  if (paint_stroking_ && paint_delta_ && paint_images_) {  // (what the stroke wrote goes back)
    for (const PaintDelta::Tile &t : paint_delta_->tiles) paint_tile_write(*paint_images_, t, true);
  }
  paint_stroking_ = false;
  paint_delta_.reset();
  paint_stroke_alpha_.clear(), paint_stroke_dab_.clear(), paint_written_.clear();
}

/* A tile's pixels (before or after) written into the images. */
void Editor::paint_tile_write(PaintImages &im, const PaintDelta::Tile &t, bool before) {
  const PaintDelta::Pixels &px = before ? t.before : t.after;
  if (px.color.size() != (size_t)t.w * t.h || t.x0 >= im.w || t.y0 >= im.h) return;  // (images of another size: the tile does not fit)
  const int w = std::min(t.w, im.w - t.x0);
  for (int y = 0; y < t.h && t.y0 + y < im.h; y++) {
    const size_t dst = (size_t)(t.y0 + y) * im.w + t.x0, src = (size_t)y * t.w;
    std::memcpy(&im.color[dst], &px.color[src], w * sizeof(uint32_t));
    std::memcpy(&im.emit[dst], &px.emit[src], w * sizeof(uint32_t));
    std::memcpy(&im.rough[dst], &px.rough[src], w), std::memcpy(&im.metal[dst], &px.metal[src], w);
    std::memcpy(&im.height[dst], &px.height[src], w), std::memcpy(&im.cover[dst], &px.cover[src], w);
    if (px.channels.size() == px.color.size()) std::memcpy(&im.channels[dst], &px.channels[src], w);
  }
}

/* Undo / redo of a paint step: the tiles back in place, the layer's stamp with them (the surface re-evaluates). */
void Editor::apply_paint_delta(UndoState s, std::vector<UndoState> &into, const char *verb, bool undo) {
  ProceduralTexturePtr pt = texture_asset(s.paint->asset);
  PaintLayer *l = pt && s.paint->layer >= 0 && s.paint->layer < (int)pt->layers.size() && std::string(pt->layers[(size_t)s.paint->layer]->type_name()) == "Paint"
                      ? static_cast<PaintLayer *>(pt->layers[(size_t)s.paint->layer].get())
                      : nullptr;
  if (!l) {
    Log::warn("%s %s: the painted layer is gone (the step is kept)", verb, s.label.c_str());
    into.push_back(std::move(s));
    return;
  }
  PaintImages &im = paint_layer_images(*l, std::max(pt->width, pt->height));
  for (const PaintDelta::Tile &t : s.paint->tiles) paint_tile_write(im, t, undo);
  im.dirty = true;
  l->stamp = undo ? s.paint->stamp_before : s.paint->stamp_after;
  pt->touch();
  invalidate_material_textures();
  scene_dirty_ = true;
  Log::info("%s %s", verb, s.label.c_str());
  into.push_back(std::move(s));
  ui_.redraw = true;
}

/* ===================================================================== */
/* Fills                                                                  */
/* ===================================================================== */

/* paint fill group|island|material|object [x y]: the brush's channels over a set of triangles chosen by the mesh at the
 * Scene view pixel (the pointer's, or the given one). */
bool Editor::paint_fill(const std::string &what, float x, float y) {
  paint_last_fill_ = -1;
  GameObject *g = scene_->find(paint_obj_);
  MeshFilter *mf = g ? g->mesh_filter() : nullptr;
  if (!paint_ || !mf || !mf->mesh || !paint_surface_) {
    Log::warn("paint fill: Paint Mode is off (paint on)");
    return false;
  }
  const Mesh &mesh = *mf->mesh;
  const RenderMesh &rm = mesh.render_mesh();
  if (rm.tri_corner.size() != rm.indices.size() || rm.tri_face.size() != rm.tri_count()) {
    paint_last_fill_ = -2;
    return false;
  }
  std::vector<uint8_t> take(rm.tri_count(), 0);
  if (what == "object") std::fill(take.begin(), take.end(), 1);
  else {
    const uint32_t face = paint_face_under(x, y);
    if (face == UINT32_MAX) {
      Log::warn("paint fill %s: nothing of %s at (%g, %g)", what.c_str(), g->name.c_str(), x, y);
      return false;
    }
    if (what == "group") {
      const int32_t grp = mesh.group_of(face);
      for (size_t t = 0; t < rm.tri_count(); t++) take[t] = mesh.group_of(rm.tri_face[t]) == grp;
    }
    else if (what == "material") {
      const int slot = mesh.material_of(face);
      for (size_t t = 0; t < rm.tri_count(); t++) take[t] = mesh.material_of(rm.tri_face[t]) == slot;
    }
    else if (what == "island") island_triangles(rm, mesh, face, take);
    else return false;
  }
  if (!paint_begin_stroke_images()) {
    paint_last_fill_ = -3;
    return false;
  }
  const Mat4 &W = g->world_matrix();
  paint_erase_ = paint_brush_.erase;
  paint_dab_serial_++;
  size_t n = 0;
  for (size_t t = 0; t < rm.tri_count(); t++)
    if (take[t]) paint_triangle(rm, mesh, t, W, Vec2(), 0.0f, 1.0f, true), n++;
  paint_dabbed_ = n > 0;
  paint_last_fill_ = (int)n;
  paint_end_stroke();
  Log::info("paint fill %s: %zu triangle(s)", what.c_str(), n);
  return n > 0;
}

/* ===================================================================== */
/* 2D painting and stencils (task 0246)                                   */
/* ===================================================================== */

/* A dab on the UV square: the texels within the brush's radius (in texels) of (u, v), no projection and no depth test -
 * every triangle sharing a texel is painted alike, as a 2D view paints. */
bool Editor::paint_dab_uv(float u, float v, float pressure) {
  if (!paint_ || !paint_surface_) return false;
  const bool own = !paint_stroking_;
  if (own && !paint_begin_stroke_images()) return false;
  PaintImages &im = *paint_images_;
  if (!std::isfinite(u) || !std::isfinite(v)) return false;
  u -= std::floor(u), v -= std::floor(v);  // (the UV square, wrapped)
  pressure = std::isfinite(pressure) ? std::max(0.0f, std::min(1.0f, pressure)) : 1.0f;
  const float weight = paint_brush_.pressure == 1 ? pressure : 1.0f;
  const float radius = std::min((float)std::max(im.w, im.h), std::max(1.0f, paint_brush_.radius * (paint_brush_.pressure == 2 ? std::max(0.1f, pressure) : 1.0f)));
  if (own) paint_erase_ = paint_brush_.erase || ui_.in.eraser;
  paint_dab_serial_++;
  const float cx = u * im.w, cy = (1.0f - v) * im.h;
  const int x0 = std::max(0, (int)std::floor(cx - radius)), x1 = std::min(im.w - 1, (int)std::ceil(cx + radius));
  const int y0 = std::max(0, (int)std::floor(cy - radius)), y1 = std::min(im.h - 1, (int)std::ceil(cy + radius));
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) {
      const float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = std::sqrt(dx * dx + dy * dy);
      if (d > radius) continue;
      const float k = weight * paint_falloff(d / radius, paint_brush_.hardness) * paint_alpha_at({dx / radius, dy / radius});
      if (k > 0.002f) paint_write_texel((size_t)y * im.w + x, k);
    }
  paint_dabbed_ = true;
  if (own) paint_end_stroke();
  ui_.redraw = true;
  return true;
}

/* An alpha or stencil image: a float image (EXR, HDR) converted to 8 bits, so every reader indexes rgba8. */
bool Editor::paint_load_brush_image(const std::string &path, Bitmap &out, const char *what) {
  std::string err;
  Bitmap bm;
  if (!load_image(resolve_asset_path(path), bm, err)) {
    Log::warn("%s: %s", what, err.c_str());
    return false;
  }
  if (bm.is_float || bm.rgba8.size() != (size_t)bm.width * bm.height * 4) {
    bm.rgba8.assign((size_t)bm.width * bm.height * 4, 255);
    for (size_t i = 0; i < (size_t)bm.width * bm.height * 4 && i < bm.rgbaf.size(); i++)
      bm.rgba8[i] = (uint8_t)std::lround(std::max(0.0f, std::min(1.0f, std::isfinite(bm.rgbaf[i]) ? bm.rgbaf[i] : 0.0f)) * 255.0f);
    bm.rgbaf.clear(), bm.is_float = false;
  }
  out = std::move(bm);
  return true;
}

/* The stencil's rect in the Scene view: the image's aspect kept, fit to the view's shorter side, scaled about the view's
 * top left and moved by the offset. */
static Recti paint_stencil_rect(const Bitmap &st, const Recti &view, Vec2 offset, float scale) {
  const float fit = std::min((float)view.w / (float)st.width, (float)view.h / (float)st.height) * std::max(0.05f, scale);
  return {view.x + (int)std::lround(offset.x), view.y + (int)std::lround(offset.y), std::max(1, (int)std::lround(st.width * fit)), std::max(1, (int)std::lround(st.height * fit))};
}

/* The stencil's luminance at a Scene view pixel (from the view's top left); 1 without one, 0 outside the image. */
float Editor::paint_stencil_at(Vec2 screen) const {
  const Bitmap &st = paint_stencil_;
  if (st.width <= 0 || st.height <= 0 || scene_rect_.w <= 0 || scene_rect_.h <= 0) return 1.0f;
  if (!std::isfinite(screen.x) || !std::isfinite(screen.y)) return 0.0f;
  const Recti r = paint_stencil_rect(st, Recti{0, 0, scene_rect_.w, scene_rect_.h}, paint_stencil_offset_, paint_stencil_scale_);
  const float fx = (screen.x - r.x) / (float)r.w, fy = (screen.y - r.y) / (float)r.h;
  if (!(fx >= 0.0f && fx < 1.0f && fy >= 0.0f && fy < 1.0f)) return 0.0f;  // (outside the stencil: nothing gets through)
  const int x = std::min(st.width - 1, std::max(0, (int)(fx * st.width))), y = std::min(st.height - 1, std::max(0, (int)(fy * st.height)));
  const uint8_t *px = &st.rgba8[((size_t)y * st.width + x) * 4];
  return (px[0] + px[1] + px[2]) / (3.0f * 255.0f);
}

/* The stencil over the Scene view, translucent, so offset and scale are placed by eye (the pixels under its dark parts
 * are what the brush leaves alone). */
void Editor::paint_stencil_draw(const Recti &view) {
  const Bitmap &st = paint_stencil_;
  if (!paint_ || st.width <= 0 || st.height <= 0) return;
  Image *img = ui_.canvas.target();
  if (!img) return;
  const Recti r = paint_stencil_rect(st, view, paint_stencil_offset_, paint_stencil_scale_);
  const Recti c = r.intersect(view).intersect(ui_.canvas.clip());
  for (int y = c.y; y < c.bottom(); y++) {
    const int sy = std::min(st.height - 1, (int)((int64_t)(y - r.y) * st.height / r.h));
    uint32_t *row = img->row(y);
    for (int x = c.x; x < c.right(); x++) {
      const int sx = std::min(st.width - 1, (int)((int64_t)(x - r.x) * st.width / r.w));
      const uint8_t *px = &st.rgba8[((size_t)sy * st.width + sx) * 4];
      const int lum = (px[0] + px[1] + px[2]) / 3;
      const uint32_t d = row[x];
      /* The dark parts tint the view towards the warning colour by 40 %; the light parts leave it. */
      const int k = (255 - lum) * 102 / 255;
      const uint32_t w = ui_.theme.warning;
      auto mix = [&](int ch, int wv) { return (uint32_t)((ch * (255 - k) + wv * k) / 255); };
      row[x] = 0xFF000000u | mix((d >> 16) & 255, (w >> 16) & 255) << 16 | mix((d >> 8) & 255, (w >> 8) & 255) << 8 | mix(d & 255, w & 255);
    }
  }
  ui_.canvas.rect_outline(r, ui_.theme.warning);
}

/* The Textures window's canvas (the preview pane showing `tiles` x `tiles` copies of the texture): a press over it begins
 * a 2D stroke on the shown surface, dabs follow the pointer by the brush's spacing (in texels, as the brush's radius),
 * and the release ends the stroke as one step. The pane's pixel maps to the UV of the tile under it. */
void Editor::paint_canvas_update(const Recti &pane, int tiles) {
  auto &in = ui_.in;
  if (!paint_ || !paint_2d_ || !texture_selected_ || texture_selected_ != paint_surface_) {
    paint_canvas_end();
    return;
  }
  const float px = (float)(in.mx - pane.x) / (float)std::max(1, pane.w) * std::max(1, tiles), py = (float)(in.my - pane.y) / (float)std::max(1, pane.h) * std::max(1, tiles);
  const Vec2 uv(px - std::floor(px), 1.0f - (py - std::floor(py)));
  const float pressure = std::isfinite(in.pressure) ? in.pressure : 1.0f;
  if (!paint_2d_stroking_) {
    if (!(ui_.hovered(pane) && in.pressed[0] && !ui_.any_active())) {
      paint_canvas_why_ = strprintf("hovered %d pressed %d active %d at %d,%d pane %d,%d %dx%d", (int)ui_.hovered(pane), (int)in.pressed[0], (int)ui_.any_active(), in.mx, in.my, pane.x, pane.y, pane.w, pane.h);
      return;
    }
    if (!paint_begin_stroke_images()) {
      paint_canvas_why_ = "no images";
      return;
    }
    paint_canvas_why_.clear();
    ui_.consume_click();
    paint_stroking_ = true, paint_dabbed_ = false, paint_2d_stroking_ = true;
    paint_erase_ = paint_brush_.erase || in.eraser;
    paint_dab_uv(uv.x, uv.y, pressure);
    paint_2d_last_ = uv;
    return;
  }
  if (!in.down[0]) {
    paint_canvas_end();
    return;
  }
  const PaintImages *im = paint_images_.get();
  const float texels = im ? (float)std::max(im->w, im->h) : 1024.0f;
  const float step = std::max(1.0f, paint_brush_.radius * std::max(0.05f, paint_brush_.spacing)) / texels;  // (in UV units)
  Vec2 d = uv - paint_2d_last_;
  if (std::fabs(d.x) > 0.5f) d.x -= std::copysign(1.0f, d.x);  // (the shortest way across the tile's wrap)
  if (std::fabs(d.y) > 0.5f) d.y -= std::copysign(1.0f, d.y);
  const float len = length(d);
  if (len < step) return;
  const int k = std::min(64, std::max(1, (int)(len / step)));
  for (int i = 1; i <= k; i++) {
    const Vec2 at = paint_2d_last_ + d * (i / (float)k);
    paint_dab_uv(at.x, at.y, pressure);
  }
  paint_2d_last_ = uv;
}

void Editor::paint_canvas_end() {
  if (!paint_2d_stroking_) return;
  paint_2d_stroking_ = false;
  paint_end_stroke();
}

/* ===================================================================== */
/* The view, the frame, the console                                       */
/* ===================================================================== */

bool Editor::paint_update(const Recti &view) {
  if (!paint_) return false;
  auto &in = ui_.in;
  if (scene_.get() != paint_scene_ || !scene_->find(paint_obj_)) {
    set_paint(false);
    return false;
  }
  const float x = (float)(in.mx - view.x), y = (float)(in.my - view.y);
  if (!paint_stroking_) {
    if (scene_hovered_ && in.key_pressed[platform::KEY_ESCAPE] && !ui_.wants_keyboard()) {
      set_paint(false);
      in.key_pressed[platform::KEY_ESCAPE] = false;
      return true;
    }
    if (!(scene_hovered_ && in.pressed[0] && !ui_.any_active() && !qm_.open)) return false;
    if (!paint_begin_stroke(view, x, y)) return false;  // (over nothing: the click is the view's)
    ui_.consume_click();
    paint_stroke_to(view, x, y, true);
    return true;
  }
  if (in.down[0]) paint_stroke_to(view, x, y, false);
  else paint_end_stroke();
  return true;
}

void Editor::paint_draw(const Recti &view) {
  paint_stencil_draw(view);
  if (!paint_ || !scene_hovered_) return;
  auto &u = ui_;
  const float r = paint_brush_.radius * u.scale;
  const Vec2 c((float)u.in.mx, (float)u.in.my);
  if (c.x < view.x || c.y < view.y || c.x > view.right() || c.y > view.bottom()) return;
  u.canvas.circle(c.x, c.y, r, paint_erase_ || u.in.eraser ? u.theme.warning : u.theme.accent);
  u.canvas.circle(c.x, c.y, std::max(1.0f, r * std::max(0.0f, std::min(0.99f, paint_brush_.hardness))), u.theme.text_dim);
}

/* paint on|off | color r g b | roughness f | metallic f | height f | emission r g b | channels <list> | size <px> | hardness <0-1> |
 * opacity <0-1> | flow <0-1> | spacing <0-1> | alpha <image>|off | symmetry x|y|z|none | lazy <px> | erase on|off | pressure off|opacity|size |
 * dab <x> <y> [pressure] | fill group|island|material|object [x y] | layer <i> */
void Editor::paint_command(const std::vector<std::string> &t) {
  auto arg = [&](size_t i, const std::string &d) { return t.size() > i ? to_lower(t[i]) : d; };
  auto num = [&](size_t i, float d) { return t.size() > i ? (float)std::atof(t[i].c_str()) : d; };
  const std::string v = arg(1, "on");
  if (v == "on" || v == "off") {
    set_paint(v == "on");
    return;
  }
  PaintBrush &b = paint_brush_;
  if (v == "color") b.color = Vec3(num(2, 0.8f), num(3, 0.1f), num(4, 0.1f));
  else if (v == "roughness") b.roughness = std::max(0.0f, std::min(1.0f, num(2, 0.5f)));
  else if (v == "metallic") b.metallic = std::max(0.0f, std::min(1.0f, num(2, 0.0f)));
  else if (v == "height") b.height = std::max(0.0f, std::min(1.0f, num(2, 0.5f)));
  else if (v == "emission") b.emission = Vec3(num(2, 0.0f), num(3, 0.0f), num(4, 0.0f));
  else if (v == "channels") {  // paint channels color,rough,metal,height,emit | all
    int ch = 0;
    for (size_t k = 2; k < t.size(); k++) {
      const std::string s = to_lower(t[k]);
      if (s == "all") ch = (1 << PaintChannels) - 1;
      size_t a = 0;
      while (a < s.size()) {
        size_t bb = s.find(',', a);
        if (bb == std::string::npos) bb = s.size();
        const std::string w = s.substr(a, bb - a);
        if (w == "color") ch |= paint_channel_bit(PaintColor);
        else if (w == "rough" || w == "roughness") ch |= paint_channel_bit(PaintRough);
        else if (w == "metal" || w == "metallic") ch |= paint_channel_bit(PaintMetal);
        else if (w == "height") ch |= paint_channel_bit(PaintHeight);
        else if (w == "emit" || w == "emission") ch |= paint_channel_bit(PaintEmit);
        a = bb + 1;
      }
    }
    if (ch) b.channels = ch;
    else Log::warn("paint channels color,rough,metal,height,emit | all");
  }
  else if (v == "size") b.radius = std::max(1.0f, std::min(500.0f, num(2, 20.0f)));
  else if (v == "hardness") b.hardness = std::max(0.0f, std::min(1.0f, num(2, 0.5f)));
  else if (v == "opacity") b.opacity = std::max(0.0f, std::min(1.0f, num(2, 1.0f)));
  else if (v == "flow") b.flow = std::max(0.0f, std::min(1.0f, num(2, 1.0f)));
  else if (v == "spacing") b.spacing = std::max(0.05f, std::min(2.0f, num(2, 0.25f)));
  else if (v == "lazy") b.lazy = std::max(0.0f, num(2, 0.0f));
  else if (v == "erase") b.erase = arg(2, "on") == "on";
  else if (v == "pressure") b.pressure = arg(2, "opacity") == "size" ? 2 : arg(2, "opacity") == "off" ? 0 : 1;
  else if (v == "symmetry") {
    b.sym_x = b.sym_y = b.sym_z = false;
    for (size_t k = 2; k < t.size(); k++) {
      const std::string s = to_lower(t[k]);
      if (s == "x") b.sym_x = true;
      if (s == "y") b.sym_y = true;
      if (s == "z") b.sym_z = true;
    }
  }
  else if (v == "alpha") {
    if (arg(2, "off") == "off") paint_alpha_ = Bitmap();
    else {
      std::string err;
      paint_load_brush_image(t[2], paint_alpha_, "paint alpha");
    }
  }
  else if (v == "layer") paint_layer_ = (int)num(2, -1.0f);
  else if (v == "2d") {  // paint 2d on|off | dab u v [pressure]: the Textures window's canvas (task 0246)
    const std::string w = arg(2, "on");
    if (w == "dab") {
      if (!paint_dab_uv(num(3, 0.5f), num(4, 0.5f), num(5, 1.0f))) Log::warn("paint 2d dab: Paint Mode is off (paint on)");
    }
    else paint_2d_ = w == "on" || (w == "toggle" && !paint_2d_), Log::info("Paint: the Textures window's canvas %s the brush (2D painting)", paint_2d_ ? "takes" : "no longer takes");
  }
  else if (v == "stencil") {  // paint stencil <image>|off | offset x y | scale s (task 0246)
    const std::string w = arg(2, "off");
    auto finite = [&](float f, float d) { return std::isfinite(f) ? f : d; };
    if (w == "off") paint_stencil_ = Bitmap();
    else if (w == "offset") paint_stencil_offset_ = Vec2(std::max(-1e5f, std::min(1e5f, finite(num(3, 0.0f), 0.0f))), std::max(-1e5f, std::min(1e5f, finite(num(4, 0.0f), 0.0f))));
    else if (w == "scale") paint_stencil_scale_ = std::max(0.05f, std::min(20.0f, finite(num(3, 1.0f), 1.0f)));
    else {
      std::string path;
      for (size_t k = 2; k < t.size(); k++) path += (k > 2 ? " " : "") + t[k];
      if (paint_load_brush_image(path, paint_stencil_, "paint stencil")) paint_stencil_offset_ = Vec2(), paint_stencil_scale_ = 1.0f;
    }
  }
  else if (v == "dab") {
    if (!paint_) {
      Log::warn("paint dab: Paint Mode is off (paint on)");
      return;
    }
    const Recti view = scene_rect_;
    const float x = num(2, 0.0f), y = num(3, 0.0f);
    ensure_scene_planes();
    if (!paint_begin_stroke(view, x, y)) {
      Log::warn("paint dab: nothing of the painted object at (%g, %g)", x, y);
      return;
    }
    paint_dab(view, x, y, num(4, 1.0f));
    paint_end_stroke();
  }
  else if (v == "fill") {
    ensure_scene_planes();
    const float x = t.size() > 4 ? num(3, 0.0f) : (float)(ui_.in.mx - scene_rect_.x), y = t.size() > 4 ? num(4, 0.0f) : (float)(ui_.in.my - scene_rect_.y);
    paint_fill(arg(2, "object"), x, y);
  }
  else Log::warn("paint on|off | color r g b | roughness f | metallic f | height f | emission r g b | channels <list> | size | hardness | opacity | flow | spacing | alpha | symmetry | lazy | erase | pressure | dab x y [p] | fill group|island|material|object [x y] | layer <i> | 2d on|off|dab u v | stencil <image>|off|offset x y|scale s");
}

/* ===================================================================== */
/* Smart materials and texture sets (task 0246)                           */
/* ===================================================================== */

/* smat apply <built-in name | Assets/Materials/Smart/<name>.smat | name>: a copy of the group on top of the active
 * object's surface; its Mesh Map effects read that surface's maps. */
bool Editor::smat_apply(const std::string &name_or_path, ProceduralTexturePtr onto) {
  std::string why;
  ProceduralTexturePtr pt = onto ? onto : surface_of(active_object(), &why);
  if (pt && !pt->surface) pt = nullptr, why = pt ? "" : "the texture shown is not a surface (surface new makes one on an object)";
  if (!pt) {
    Log::warn("smat apply: %s", why.c_str());
    return false;
  }
  /* A saved file first - a built-in written out and edited wins over the code's copy - then the built-in. */
  std::string path = name_or_path;
  if (path.find('/') == std::string::npos && path.find('\\') == std::string::npos) path = "Assets/Materials/Smart/" + path + ".smat";
  std::string text;
  std::unique_ptr<TextureLayer> grp;
  if (fs::read_file(resolve_asset_path(path), text)) {
    grp = load_smart_material_text(text, &why);
    if (!grp) {
      Log::warn("smat apply: %s", why.c_str());
      return false;
    }
    const std::string refusal = smart_material_refusal(*grp);
    if (!refusal.empty()) {
      Log::warn("smat apply: %s", refusal.c_str());
      return false;
    }
    grp->name = fs::stem(path);
  }
  else grp = smart_material_builtin(name_or_path);
  if (!grp) {
    Log::warn("smat apply: no built-in and no file called '%s' (smat list)", name_or_path.c_str());
    return false;
  }
  note_texture_edit(*pt);
  pt->layers.push_back(std::move(grp));
  pt->touch();
  mark_changed("Apply Smart Material");
  Log::info("Surface %s: %s applied on top (%zu layers)%s", pt->name.c_str(), pt->layers.back()->name.c_str(), pt->layers.size(), pt->maps ? "" : " - no mesh maps: its generators read flat grey (surface maps)");
  return true;
}

/* smat save <layer> <name> | apply <name> | list | builtin <name> */
void Editor::smat_command(const std::vector<std::string> &t) {
  auto arg = [&](size_t i, const std::string &d) { return t.size() > i ? t[i] : d; };
  auto rest = [&](size_t from) {
    std::string s;
    for (size_t k = from; k < t.size(); k++) s += (k > from ? " " : "") + t[k];
    return s;
  };
  const std::string sub = to_lower(arg(1, "list"));
  if (sub == "list") {
    Log::info("Smart materials, built in: %s", [&] {
      std::string s;
      for (const std::string &n : smart_material_builtins()) s += (s.empty() ? "" : ", ") + n;
      return s;
    }().c_str());
    size_t n = 0;
    for (const DirEntry &e : fs::list(resolve_asset_path("Assets/Materials/Smart")))
      if (!e.is_dir && fs::extension(e.name) == ".smat") Log::info("  %s", e.name.c_str()), n++;
    if (!n) Log::info("  (no saved ones: smat save <layer> <name>)");
    return;
  }
  if (sub == "apply") {
    smat_apply(rest(2));
    return;
  }
  if (sub == "builtin" || sub == "save") {
    std::unique_ptr<TextureLayer> grp;
    std::string name;
    if (sub == "builtin") {
      name = rest(2);
      grp = smart_material_builtin(name);
      if (!grp) {
        Log::warn("smat builtin: Worn Steel | Painted Metal | Rubber | Wood | Plastic | Concrete | Gold");
        return;
      }
    }
    else {
      GameObject *g = active_object();
      std::string why;
      ProceduralTexturePtr pt = surface_of(g, &why);
      const int i = std::atoi(arg(2, "-1").c_str());
      if (!pt || i < 0 || i >= (int)pt->layers.size()) {
        Log::warn("smat save <layer index> <name>: %s", pt ? "no such layer" : why.c_str());
        return;
      }
      const std::string refusal = smart_material_refusal(*pt->layers[(size_t)i]);
      if (!refusal.empty()) {
        Log::warn("smat save: %s", refusal.c_str());
        return;
      }
      name = rest(3);
      grp = pt->layers[(size_t)i]->clone();
      if (name.empty()) name = grp->name.empty() ? "Smart Material" : grp->name;
    }
    std::string file = name;
    for (char &ch : file)
      if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|' || ch == '#' || (unsigned char)ch < 32) ch = '_';
    while (!file.empty() && (file.back() == ' ' || file.back() == '.')) file.pop_back();
    static const char *reserved[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "LPT1", "LPT2", "LPT3"};
    for (const char *r : reserved)
      if (to_lower(file) == to_lower(r)) file += "_";
    if (file.empty()) {
      Log::warn("smat: a name is needed");
      return;
    }
    grp->name = file;
    const std::string rel = "Assets/Materials/Smart/" + file + ".smat";
    fs::make_dirs(resolve_asset_path("Assets/Materials/Smart"));
    if (!fs::write_file(resolve_asset_path(rel), save_smart_material_text(*grp))) {
      Log::warn("smat: could not write %s", rel.c_str());
      return;
    }
    project_listed_ = -100;
    Log::info("Smart material %s written (smat apply %s)", rel.c_str(), file.c_str());
    return;
  }
  Log::warn("smat save <layer> <name> | apply <name> | list | builtin <name>");
}

/* export textures <packing> <dir> [--assign]: the active object's surface as a texture set. */
void Editor::export_textures_command(const std::vector<std::string> &t) {
  auto arg = [&](size_t i, const std::string &d) { return t.size() > i ? t[i] : d; };
  GameObject *g = active_object();
  std::string why;
  ProceduralTexturePtr pt = surface_of(g, &why);
  if (!pt) {
    Log::warn("export textures: %s", why.c_str());
    return;
  }
  const std::string packing = to_lower(arg(2, "gltf"));
  std::string dir = arg(3, "Exports/Textures");
  const bool assign = std::find(t.begin(), t.end(), "--assign") != t.end();
  if (dir == "--assign") dir = "Exports/Textures";
  std::vector<std::string> files;
  if (!export_texture_set(*pt, packing, resolve_asset_path(dir), files, &why)) {
    Log::warn("export textures: %s", why.c_str());
    return;
  }
  Log::info("export textures %s: %zu file(s) under %s (a snapshot: export again after more strokes)%s", packing.c_str(), files.size(), dir.c_str(), assign ? "; the material now reads the files (the Textures window's Assign puts the live surface back)" : "");
  if (!assign) return;
  MeshRenderer *mr = g->get<MeshRenderer>();
  if (!mr || mr->materials.empty() || !mr->materials[0]) {
    Log::warn("export textures --assign: %s has no material to point at the files", g->name.c_str());
    return;
  }
  /* The engine's maps one a channel (`separate`) beside the packing's files: the material reads roughness and metallic
   * from their own images, as it did from the surface's channels, and emission only when the stack writes it. */
  std::vector<std::string> sep;
  if (!export_texture_set(*pt, "separate", resolve_asset_path(dir), sep, &why)) {
    Log::warn("export textures --assign: %s", why.c_str());
    return;
  }
  const std::vector<std::string> maps = texture_packing_maps("separate");
  auto file_of = [&](const char *map) {
    for (size_t k = 0; k < maps.size() && k < sep.size(); k++)
      if (maps[k] == map) return fs::join(dir, fs::filename(sep[k]));
    return std::string();
  };
  Material &m = *mr->materials[0];
  m.base_map = TextureRef{file_of("Albedo"), false};
  m.normal_map = TextureRef{file_of("Normal"), true};
  m.roughness_map = TextureRef{file_of("Roughness"), true};
  m.metallic_map = TextureRef{file_of("Metallic"), true};
  m.metallic = 1.0f, m.roughness = 1.0f;  // (the maps multiply the values)
  m.emission_map = surface_stack_writes_emission(*pt) ? TextureRef{file_of("Emission"), false} : TextureRef{};
  m.touch();
  mark_changed("Assign Exported Textures");
}

/* The Paint layers' images written to their PNGs when they changed (called where the project and its assets are saved). */
size_t Editor::paint_flush_images() {
  size_t n = 0;
  for (const auto &entry : loaded_texture_assets()) {
    ProceduralTexturePtr t = entry.second;
    if (!t) continue;
    for (auto &l : t->layers) {
      if (std::string(l->type_name()) != "Paint") continue;
      PaintLayer &pl = static_cast<PaintLayer &>(*l);
      if (!pl.images || !pl.images->dirty) continue;
      if (pl.base.empty()) pl.base = "Assets/Textures/" + t->name + "_paint";
      std::string err;
      if (paint_images_save(*pl.images, resolve_asset_path(pl.base), &err)) pl.images->dirty = false, n++;
      else Log::warn("Paint: %s", err.c_str());
    }
  }
  if (n) project_listed_ = -100;
  return n;
}

}  // namespace bl

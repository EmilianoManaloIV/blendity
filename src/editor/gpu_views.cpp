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
#include "../render/display.h"
#include "../render/probe_volume.h"
#include "../render/voxel_gi.h"

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
  /* (A cutout draws on the card since task 0163, Transparent and Glass since task 0195 - but not toon ones, whose
   * lighting reads the neighbouring pixels' normals in the g-buffer.) */
  if (!m.opaque() && m.surface != (int)MaterialSurface::Cutout && m.shading_model == 1) return "a see-through toon material";
  if (m.volume()) return "a Volume (fog) material";  // (round 331)
  if (m.shading_model == 0 && m.principled_extras()) return "a material with Subsurface, Sheen, Coat or Anisotropic";  // (round 330: the card's g-buffer has no room for them yet)
  const Material::Resolved &tx = m.textures();
  /* (A live procedural texture draws on the card since task 0196, except seen close up: gpu_view_draw's live_close_up.)
   * Axis materials draw on the card since task 0195 - not toon ones (the toon maps aren't read through the parts), nor
   * parts with live procedurals. */
  if (m.axis_mode != 0 && m.shading_model == 1) return "a toon axis material";
  for (int k = 0; m.axis_mode != 0 && k < 4; k++)
    for (int j = 0; tx.axis[k] && j < 5; j++)
      if ((tx.axis_tx[k] ? *tx.axis_tx[k] : tx.axis[k]->textures()).live[j]) return "a live procedural texture";
  /* (Toon materials' painted maps and the hair highlight draw on the card since task 0196: its toon pass.) */
  return nullptr;
}

/* Why an object can't be drawn on the GPU yet, or null. */
const char *item_unsupported(const DrawItem &it, const LightingEnv &env) {
  if (!it.mesh) return nullptr;
  if (it.flattened) return "a Camera Flatten (the card has no room for its true shape's shading)";  // (round 334)
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
  g.right[0] = l.right.x, g.right[1] = l.right.y, g.right[2] = l.right.z;  // (task 0163: an area light's panel)
  g.width = l.width;
  g.height = l.height;
  g.disk = l.disk;
  g.bake_mode = l.bake_mode;  // (task 0197)
  return g;
}

/* Task 0197: the probe volume packed for the card (gpu::Raster::set_probe_volume, read by kProbeFns): a header of
 * three vec4, then the octree's nodes (two each), the bricks (two each), the probes (thirteen each: position and
 * flags, the baked cube's six axes, the live one's), the roots four to a vec4. */
std::vector<float> probe_pack(const ProbeVolumeData &d) {
  const size_t nn = d.nodes.size(), nb = d.bricks.size(), np = d.position.size(), nr = d.roots.size();
  const size_t node_at = 3, brick_at = node_at + nn * 2, probe_at = brick_at + nb * 2, root_at = probe_at + np * 13;
  std::vector<float> o((root_at + (nr + 3) / 4) * 4, 0.0f);
  auto put = [&](size_t v, float a, float b, float c, float e) { o[v * 4] = a, o[v * 4 + 1] = b, o[v * 4 + 2] = c, o[v * 4 + 3] = e; };
  auto bits = [](int64_t i) {  // (an index as its int32's bits: exact past 2^24, read with floatBitsToInt)
    const int32_t v = (int32_t)i;
    float f;
    std::memcpy(&f, &v, 4);
    return f;
  };
  put(0, d.origin.x, d.origin.y, d.origin.z, d.top);
  put(1, bits(d.tn[0]), bits(d.tn[1]), bits(d.tn[2]), bits((int64_t)node_at));
  put(2, bits((int64_t)brick_at), bits((int64_t)probe_at), bits((int64_t)root_at), 0.0f);
  for (size_t i = 0; i < nn; i++) {
    const ProbeVolumeData::Node &n = d.nodes[i];
    put(node_at + i * 2, n.b.min.x, n.b.min.y, n.b.min.z, bits(n.child));
    put(node_at + i * 2 + 1, n.b.max.x, n.b.max.y, n.b.max.z, bits(n.brick));
  }
  for (size_t i = 0; i < nb; i++) {
    const ProbeBrick &b = d.bricks[i];
    put(brick_at + i * 2, b.min.x, b.min.y, b.min.z, b.spacing);
    put(brick_at + i * 2 + 1, bits(b.first), 0.0f, 0.0f, 0.0f);
  }
  for (size_t i = 0; i < np; i++) {
    const size_t v = probe_at + i * 13;
    const bool valid = i < d.valid.size() && d.valid[i], live_ok = i < d.live_ok.size() && d.live_ok[i];
    put(v, d.position[i].x, d.position[i].y, d.position[i].z, bits((valid ? 1 : 0) + (live_ok ? 2 : 0)));
    const ProbeCube &baked = i < d.baked.size() ? d.baked[i] : ProbeCube{};
    const ProbeCube &live = live_ok && i < d.live.size() ? d.live[i] : baked;
    for (int ax = 0; ax < 6; ax++) {
      put(v + 1 + ax, baked.bounce[(size_t)ax].x, baked.bounce[(size_t)ax].y, baked.bounce[(size_t)ax].z, baked.sky[(size_t)ax]);
      put(v + 7 + ax, live.bounce[(size_t)ax].x, live.bounce[(size_t)ax].y, live.bounce[(size_t)ax].z, live.sky[(size_t)ax]);
    }
  }
  for (size_t i = 0; i < nr; i++) o[root_at * 4 + i] = bits(d.roots[i]);
  return o;
}

void set3(float *d, Vec3 v) { d[0] = v.x, d[1] = v.y, d[2] = v.z; }

/* A material that varies over the surface (task 0101, ADR 0038): its own values and its maps, which the card
 * evaluates per pixel as evaluate_surface does. `tex` gives a texture's id on the card. */
void raster_textured(gpu::RasterInstance &in, const Material &m, const RenderMesh &rm, const std::function<int(const TexturePtr &)> &tex,
                     const Material::Resolved *resolved = nullptr) {
  const Material::Resolved &tx = resolved ? *resolved : m.textures();
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

/* Task 0195: an axis material (ADR 0018) for the card - its settings as the path tracer's GMaterial takes them, and its
 * parts as instances of their own (`parts`, each with the slot it fills; a part that is itself an axis material is used
 * plainly, as on the CPU). The caller places the parts and fills in their indices. */
void raster_axis(gpu::RasterInstance &in, const Material &m, const RenderMesh &rm, const std::function<int(const TexturePtr &)> &tex,
                 std::vector<std::pair<gpu::RasterInstance, int>> &parts) {
  in.flags |= gpu::RasterInstance::Axis;
  in.axis[0] = std::cos(clampf(m.axis_angle, 0.0f, 90.0f) * kDeg2Rad);
  in.axis[1] = clampf(m.axis_blend, 0.0f, 1.0f) * 0.5f;
  in.axis[2] = 1.0f / std::max(0.01f, m.axis_scale);
  in.axis[3] = (float)m.axis_mode;
  in.axis2[0] = finite_bits(m.axis_noise) ? clampf(m.axis_noise, 0.0f, 1.0f) : 0.0f;
  in.axis2[1] = 1.0f / (finite_bits(m.axis_noise_scale) ? std::max(0.01f, m.axis_noise_scale) : 4.0f);
  in.axis2[2] = finite_bits(m.axis_high_start) ? m.axis_high_start : 0.0f;
  in.axis2[3] = finite_bits(m.axis_high_blend) ? std::max(0.0f, m.axis_high_blend) : 1.0f;
  const Material::Resolved &tx = m.textures();
  for (int k = 0; k < 4; k++) {
    in.axis_parts[k] = -1.0f;
    if (!tx.axis[k]) continue;
    gpu::RasterInstance part = in;
    part.flags = gpu::RasterInstance::AxisPart;
    raster_textured(part, *tx.axis[k], rm, tex, tx.axis_tx[k]);
    part.metal_spec[3] = tx.axis[k]->alpha;
    parts.push_back({part, k});
  }
}

/* Task 0196: a toon material's painted maps and its hair highlight, for the card's toon pass (evaluate_surface's toon
 * maps; the settings as it cleans them). True: it has some (the instance's ToonMaps flag). */
bool raster_toon_maps(gpu::RasterInstance &in, const Material &m, const std::function<int(const TexturePtr &)> &tex) {
  const Material::Resolved &tx = m.textures();
  const TexturePtr *maps[7] = {&tx.shade, &tx.toon_bias, &tx.toon_specular, &tx.toon_rim_mask, &tx.face_shadow, &tx.line, &tx.hatch};
  bool any = m.toon_hair_highlight;
  for (int k = 0; k < 7; k++) {
    in.toon_maps[k] = *maps[k] ? tex(*maps[k]) : -1;
    any = any || *maps[k];
  }
  if (!any) return false;
  in.face_forward = m.face_forward;
  in.hatch_scale = finite_bits(m.hatch_scale) && m.hatch_scale > 0.0f ? m.hatch_scale : 1.0f;
  in.face_softness = clampf(finite_bits(m.face_shadow_softness) ? m.face_shadow_softness : 0.0f, 0.0f, 0.5f);
  in.line_only_shadow = m.line_only_shadow;
  in.hair = m.toon_hair_highlight;
  return true;
}

/* Task 0196: the most a mesh's UVs are stretched - the largest world length one UV unit spans along any direction, over
 * its triangles (the largest singular value of each triangle's position-from-UV map), in its own space. Triangles with
 * no UV area are left out: the CPU sees no UV change across them, and reads the image there. */
float uv_stretch(const RenderMesh &rm) {
  if (rm.uvs.size() < rm.positions.size()) return 0.0f;
  float most = 0.0f;
  for (size_t t = 0; t + 2 < rm.indices.size(); t += 3) {
    const uint32_t a = rm.indices[t], b = rm.indices[t + 1], c = rm.indices[t + 2];
    const Vec3 e1 = rm.positions[b] - rm.positions[a], e2 = rm.positions[c] - rm.positions[a];
    const Vec2 d1 = rm.uvs[b] - rm.uvs[a], d2 = rm.uvs[c] - rm.uvs[a];
    const float det = d1.x * d2.y - d2.x * d1.y;
    if (d1.x == 0.0f && d1.y == 0.0f && d2.x == 0.0f && d2.y == 0.0f) continue;  // (one UV for the triangle: no change, the image)
    if (!(std::fabs(det) > 1e-12f)) {  // (UVs on a line: the largest world length an edge's UV length spans)
      const Vec3 e3 = rm.positions[c] - rm.positions[b];
      const Vec2 d3 = rm.uvs[c] - rm.uvs[b];
      for (const auto &[e, d] : {std::pair<Vec3, Vec2>{e1, d1}, {e2, d2}, {e3, d3}}) {
        const float dl = std::sqrt(d.x * d.x + d.y * d.y);
        if (dl > 0.0f) most = std::max(most, length(e) / dl);
      }
      continue;
    }
    /* J = [e1 e2] [d1 d2]^-1: its columns dP/du and dP/dv; the largest singular value from J^T J. */
    const Vec3 ju = (e1 * d2.y - e2 * d1.y) / det, jv = (e2 * d1.x - e1 * d2.x) / det;
    const float p = dot(ju, ju), q = dot(ju, jv), r = dot(jv, jv);
    const float s = 0.5f * (p + r) + std::sqrt(std::max(0.0f, 0.25f * (p - r) * (p - r) + q * q));
    if (finite_bits(s)) most = std::max(most, std::sqrt(s));
  }
  return most;
}

/* Task 0196: the largest stretch of the linear map with columns a, b, c (its largest singular value), by power
 * iteration on M^T M, rounded up a little: an upper bound in practice. */
float largest_stretch(Vec3 a, Vec3 b, Vec3 c) {
  Vec3 x(1.0f, 0.7f, 0.4f);
  float s = 0.0f;
  for (int i = 0; i < 24; i++) {
    const Vec3 mx = a * x.x + b * x.y + c * x.z;  // M x
    const Vec3 y(dot(a, mx), dot(b, mx), dot(c, mx));  // M^T M x
    const float len = length(y);
    if (!(len > 1e-20f) || !finite_bits(len)) break;
    s = len / std::max(length(x), 1e-20f);
    x = y / len;
  }
  return std::sqrt(std::max(s, 0.0f)) * 1.01f;
}

/* Task 0196: whether the CPU would evaluate one of this material's live procedural textures point by point anywhere
 * on the item (evaluate_surface's live_weight above 0: one of its texels larger than a pixel), judged at the item's
 * nearest point to the eye and, for UVs, their most stretched triangle; then the view draws on the CPU. `px1`: a
 * pixel's world size one metre away (or everywhere, orthographic: `ortho`). Conservative: where it says no, the CPU
 * reads the same image the card does. */
bool live_close_up(const DrawItem &it, const Material &m, float stretch, float near_d, float px1, bool ortho) {
  const Material::Resolved &tx = m.textures();
  if (m.wrap != (int)TexWrap::Repeat || m.filter == (int)TexFilter::Closest) return false;  // (the CPU's live_ok: the image always)
  const TexturePtr *images[5] = {&tx.base, &tx.metallic, &tx.roughness, &tx.normal, &tx.emission};
  const Vec3 sx = it.model.dir(Vec3(1, 0, 0)), sy = it.model.dir(Vec3(0, 1, 0)), sz = it.model.dir(Vec3(0, 0, 1));
  const float scale = std::max(largest_stretch(sx, sy, sz), 1e-6f);  // (sheared too)
  const float f = (ortho ? 1.0f : near_d) * px1;  // the smallest world footprint of a pixel on it
  const bool box = m.mapping == (int)MaterialMapping::Box, uv = m.mapping == (int)MaterialMapping::UV;
  for (int k = 0; k < 5; k++) {
    if (!tx.live[k]) continue;
    const TexturePtr &t = *images[k];
    if (!t) return true;  // (no image: always evaluated)
    const float texels = (float)std::max(t->width, t->height);
    float per_px;
    if (uv) {
      if (!(stretch > 0.0f)) continue;  // (no UV changes: the image)
      const float tile = std::min(std::fabs(m.tiling.x), std::fabs(m.tiling.y));
      per_px = f / (stretch * scale) * tile * texels / std::sqrt(2.0f);  // (the largest derivative component is at least this)
    }
    else {
      float size = 1.0f;
      if (!box && it.mesh->bounds.valid()) {
        const Vec3 ext = it.mesh->bounds.max - it.mesh->bounds.min;
        size = std::max(1e-6f, std::min({std::max(ext.x, 1e-6f), std::max(ext.y, 1e-6f), std::max(ext.z, 1e-6f)}));
      }
      const float tile = std::max({std::fabs(m.tiling.x), std::fabs(m.tiling.y), std::fabs(m.tiling.z)});
      per_px = f / scale * tile * texels / size;
    }
    if (!(per_px >= 1.0f)) return true;
  }
  return false;
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
  set3(in.toon_specular_color, m.toon_specular_color);
  in.toon_wrap = clean(m.toon_wrap, 0.0f, 1.0f);
  in.toon_rim_side = m.toon_rim_side;
  in.toon_key_light_only = m.toon_key_light_only;
}

/* Task 0195: an instance as Solid shading draws it (the CPU raster's Gouraud mode): the object's one colour and
 * highlight strength, unlit or not, lit per vertex; a cutout keeps its material, shaded per pixel as the see-through
 * pass does. */
void solid_instance(gpu::RasterInstance &in, const DrawItem &it) {
  in.metal_spec[1] = it.specular;
  in.flags &= ~(uint32_t)(gpu::RasterInstance::Toon | gpu::RasterInstance::ToonMaps);
  if (in.metal_spec[2] >= 0.0f || in.surface) return;
  set3(in.albedo_rough, it.albedo);
  in.textured = false;
  in.flags &= ~(uint32_t)gpu::RasterInstance::Unlit;
  if (it.unlit) in.flags |= gpu::RasterInstance::Unlit;
}

/* Weight Paint's colours for the card: one a vertex, the object's colour past the end (as shade_vertex reads them). */
uint32_t solid_vertex_colors(const DrawItem &it, std::vector<float> &out) {
  const uint32_t first = (uint32_t)(out.size() / 4) + 1;
  const std::vector<Vec3> &c = *it.vertex_colors;
  for (size_t v = 0; v < it.mesh->positions.size(); v++) {
    const Vec3 x = v < c.size() ? c[v] : it.albedo;
    out.insert(out.end(), {x.x, x.y, x.z, 1.0f});
  }
  return first;
}

/* Task 0195: one see-through triangle as Renderer3D::draw_see_through keeps it - clipped against the near plane, its
 * pieces culled by their winding on screen unless two-sided - with the first kept piece's mean depth, its sort key.
 * False: none kept. (The card draws the whole triangle: its own clipper cuts it the same.) */
bool see_through_key(const Vec4 (&v)[3], bool cull, float facing, float &key) {
  Vec4 pv[4];
  int np = 0;
  for (int k = 0; k < 3; k++) {
    const int k2 = (k + 1) % 3;
    const bool in0 = v[k].z >= 0, in1 = v[k2].z >= 0;
    if (in0) pv[np++] = v[k];
    if (in0 != in1) pv[np++] = lerp(v[k], v[k2], v[k].z / (v[k].z - v[k2].z));
  }
  float sx[4], sy[4], sz[4];
  for (int k = 0; k < np; k++) {
    const float iw = 1.0f / pv[k].w;
    sx[k] = pv[k].x * iw, sy[k] = -pv[k].y * iw, sz[k] = pv[k].z * iw;  // (y down, as the CPU's screen; the scale cancels in the sign)
  }
  for (int k = 1; k + 1 < np; k++) {
    const float area = (sx[k] - sx[0]) * (sy[k + 1] - sy[0]) - (sx[k + 1] - sx[0]) * (sy[k] - sy[0]);
    if (area == 0.0f || (area * facing < 0.0f && cull)) continue;
    key = (sz[0] + sz[k] + sz[k + 1]) / 3.0f;
    return true;
  }
  return false;
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
  in.metal_spec[2] = m.surface == (int)MaterialSurface::Cutout ? m.alpha_clip : -1.0f;  // (task 0163: the card punches the holes)
  in.metal_spec[3] = m.alpha;
  /* (Task 0195: see-through - Transparent, else Glass, as draw_see_through takes any other value.) */
  if (!m.opaque() && m.surface != (int)MaterialSurface::Cutout) in.surface = m.surface == (int)MaterialSurface::Transparent ? 2 : 3, in.ior = m.ior;
  set3(in.emission, s.emission);
  if (!material_uniform(m, m.textures())) raster_textured(in, m, *it.mesh, tex);
  bool toon_maps = false;
  if (m.shading_model == 1) {
    raster_toon(in, m);
    toon_maps = raster_toon_maps(in, m, tex);
    const TexturePtr &ramp = m.textures().toon_ramp;
    in.toon_ramp = ramp ? tex(ramp) : -1;  // (the ramp needs no UVs: the lighting pass reads it by how lit)
  }
  in.flags = 0;
  if (s.unlit) in.flags |= gpu::RasterInstance::Unlit;
  if (it.receive_shadows) in.flags |= gpu::RasterInstance::ReceiveShadows;
  if (casts_shadows(it)) in.flags |= gpu::RasterInstance::CastShadows;
  if (it.double_sided) in.flags |= gpu::RasterInstance::DoubleSided;  // (per object, as the CPU culls: its first material's)
  if (mirrored(it.model)) in.flags |= gpu::RasterInstance::Mirrored;
  if (it.always_cull) in.flags |= gpu::RasterInstance::AlwaysCull;
  if (m.shading_model == 1) in.flags |= gpu::RasterInstance::Toon;
  if (toon_maps) in.flags |= gpu::RasterInstance::ToonMaps;  // (task 0196)
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

/* Task 0195: the see-through triangles back to front (a stable sort: ties keep the items' and triangles' order, as
 * the CPU's), three corners each - the mesh's vertices, the instance, the face highlight. */
template <class SeeTri>
void see_through_to_frame(std::vector<SeeTri> &see, std::vector<gpu::SeeCorner> &out) {
  std::stable_sort(see.begin(), see.end(), [](const SeeTri &a, const SeeTri &b) { return a.key > b.key; });
  out.resize(see.size() * 3);
  for (size_t i = 0; i < see.size(); i++) {
    const DrawItem &it = *see[i].it;
    const RenderMesh &rm = *it.mesh;
    const uint32_t prim = see[i].prim;
    const bool hl = it.face_highlight && prim < rm.tri_face.size() && rm.tri_face[prim] < it.face_highlight->size() && (*it.face_highlight)[rm.tri_face[prim]];
    for (int q = 0; q < 3; q++) {
      const gpu::RasterVertex rv = raster_vertex(rm, rm.indices[(size_t)prim * 3 + q]);
      gpu::SeeCorner &c = out[i * 3 + q];
      std::memcpy(c.p, rv.p, 12);
      std::memcpy(c.n, rv.n, 12);
      std::memcpy(c.uv, rv.uv, 8);
      std::memcpy(c.t, rv.t, 16);
      c.inst = see[i].inst;
      c.highlight = hl ? 1.0f : 0.0f;
      c.pad[0] = c.pad[1] = 0.0f;
    }
  }
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
  if (e) {  // (task 0248: the stylized sky and the fog)
    set3(fr.sky_disc, e->sun_dir), fr.sky_disc[3] = e->disc_cos;
    set3(fr.sky_disc_color, e->disc_color), fr.sky_disc_color[3] = e->disc_soft;
    set3(fr.sky_band, e->band_color), fr.sky_band[3] = e->band_width;
    fr.fog[0] = e->fog_start, fr.fog[1] = e->fog_end, fr.fog[2] = e->fog ? 1.0f : 0.0f;
    set3(fr.fog_color, e->fog_color), set3(fr.fog_far, e->fog_far);
  }
  else fr.sky_disc[3] = 2.0f, fr.sky_band[3] = 0.0f, fr.fog[2] = 0.0f;
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
  d = std::max(0, std::min(2, d));
  if (d != viewport_device_) scene_cache_.key = game_cache_.key = 0;  // (task 0199: pictures drawn by the other device drawn again)
  viewport_device_ = d;
  card_retry();  // (task 0193)
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
    if (!gpu_view_) notice_cpu_fallback();
    return gpu_view_.get();
  }
  if (gpu_view_tried_) return nullptr;
  gpu_view_tried_ = true;
  if (!gpu::available() || gpu::devices().empty()) {
    gpu_view_error_ = "no Vulkan GPU";
    notice_cpu_fallback();
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

/* Task 0199: the first time Auto finds no card for the views (none, or it couldn't open), said once - every later
 * start keeps quiet, as the CPU is then simply how this machine draws (Preferences says why). */
bool Editor::notice_cpu_fallback() {
  if (viewport_device_ != 0 || cpu_fallback_noticed_ || (headless_ && !notice_in_tests_) || windowed_check_) return false;
  Log::warn("The views draw on the CPU: %s. That works everywhere, more slowly; Preferences > Viewport Device says why.", gpu_view_error_.c_str());
  cpu_fallback_noticed_ = true;
  save_prefs();
  return true;
}

/* Task 0199: a window's drawing and showing, for Preferences: its views on the card or the CPU, and whether the card
 * or the system shows the window (with why, when the card can't). */
std::string Editor::window_device_status(bool card_present, int card_win, const std::string &card_reason) const {
  const bool on_gpu = gpu_view_wanted() && gpu_view_;  // (the card stays open after choosing CPU: it isn't drawing them)
  std::string s = on_gpu ? "views on the GPU (" + gpu_view_->info().name + ")" : "views on the CPU";
  if (card_win >= 0 && (card_present || card_reason.empty())) return s + ", shown by the card";
  return s + ", shown by the system" + (card_reason.empty() ? std::string() : ": " + card_reason);
}

std::string Editor::gpu_view_status() const {
  if (!gpu_view_wanted()) return "CPU (Viewport Device)";
  if (!gpu_view_) return gpu_view_opening_.valid() ? "CPU while the GPU opens" : gpu_view_tried_ ? "CPU: " + gpu_view_error_ : "GPU (not opened yet)";
  std::string s = "GPU: " + gpu_view_->info().name;
  std::string why;
  for (const std::string &r : gpu_slot_reason_)
    if (!r.empty() && why.find(r) == std::string::npos) why += (why.empty() ? "" : "; ") + r;
  if (!why.empty()) s += " (a view on the CPU: " + why + ")";
  return s;
}

const char *Editor::gpu_view_unsupported(const RenderTarget &rt, const LightingEnv &env, const std::vector<DrawItem> &items,
                                         const FilterStack *filters) const {
  /* (Since task 0197 a camera filter's passes run over the card's picture, read back; its rasterizer stage - snapped
   * vertices, affine UVs, texture rules, a screen door - changes how the triangles are drawn, which the card doesn't.) */
  if (filters && (filters->vertex_snap > 0.0f || filters->affine_uv || filters->tex.active() || filters->screen_door))
    return "a camera filter's rasterizer stage (snapped vertices, affine UVs, texture rules or a screen door)";
  if (rt.want_hdr && !(gpu_view_ && gpu_view_->can_draw_hdr())) return "a picture kept in linear light";
  /* (Lightmaps, realtime lightmaps and probes draw on the card since task 0197; realtime GI's per-pixel gather - the
   * receivers the CPU traces from its own picture - for objects without realtime lightmaps doesn't.) */
  if (env.gi)
    for (const DrawItem &it : items)
      if (it.mesh && !(it.rt_bounce && it.rt_sky && it.rt_uv)) return "realtime GI's per-pixel light (Receive GI: Lightmaps draws on the card)";
  const bool highlight = gpu_view_ && gpu_view_->can_highlight_faces();
  for (const DrawItem &it : items) {
    if (it.face_highlight && !highlight) return "Edit Mode's face highlight (the card has no geometry shaders)";
    if (const char *why = item_unsupported(it, env)) return why;
  }
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
  /* Round 337: a mesh first drawn without a normal map went up with zero tangents; its tangents are added to the same
   * render mesh (the serial kept) when a normal map comes, so they are part of the key - the card's zero tangents
   * normalized to NaN and drew the object black (a texture's Physical Size moved a cube from the CPU to the card). */
  const bool tangents = !rm.tangents.empty();
  if (e.id >= 0 && rm.serial != 0 && e.serial == rm.serial && e.tangents == tangents) return e.id;
  if (e.tangents == tangents && gpu_mesh_moved(e, rm, err)) return e.id;
  err.clear();  // (a refused partial update falls back to the whole mesh)
  const std::vector<gpu::RasterVertex> v = raster_vertices(rm);
  const std::vector<uint32_t> indices = card_indices(rm, e.parts);
  prof_.gpu_upload_bytes += (uint64_t)v.size() * sizeof(gpu::RasterVertex) + (uint64_t)rm.indices.size() * 4;
  if (e.id >= 0 && e.vertices == rm.positions.size() && e.indices == rm.indices.size() && gpu_view_->update_mesh(e.id, v, &indices, &err)) {
    prof_.gpu_mesh_updates++;
    e.serial = rm.serial;
    e.tangents = tangents;
    return e.id;
  }
  if (e.id >= 0) {
    gpu_view_->remove_mesh(e.id);
    gpu_mesh_bytes_ -= e.bytes;
  }
  e.id = gpu_view_->add_mesh(v, indices, &err);
  e.serial = rm.serial;
  e.tangents = tangents;
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

/* Task 0245: a texture painted in place - its rows [y0, y1) go up when the card holds it (nothing otherwise: it is
 * uploaded whole when first drawn). */
void Editor::gpu_texture_rows_changed(const TexturePtr &t, int y0, int y1) {
  if (!t || !gpu_view_) return;
  auto found = gpu_textures_.find(t.get());
  if (found == gpu_textures_.end() || found->second.id < 0) return;
  std::string err;
  if (!gpu_view_->update_texture_rows(found->second.id, *t, y0, y1, &err)) {
    gpu_view_->remove_texture(found->second.id);  // (its slot freed; the whole texture goes up again at the next frame)
    gpu_textures_.erase(found);
    if (!err.empty()) Log::warn("GPU view: %s", err.c_str());
  }
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
  auto mine = [&](int i) { return gpu_slot_rt_[i] == &rt && gpu_slot_win_[i] == active_window_; };  // (task 0232: per window)
  int best = 0;
  for (int i = 0; i < gpu::Raster::kSlots; i++) {
    if (mine(i)) best = i;
    else if (!mine(best) && gpu_slot_used_[i] < gpu_slot_used_[best]) best = i;
  }
  if (best == scene_planes_slot_ && !mine(best)) scene_planes_slot_ = -1;  // (task 0204: another view takes the slot)
  if (!mine(best))  // (task 0232: another window's Scene view had planes waiting in it)
    for (auto &w : windows_)
      if (w->view && w->view->scene_planes_slot == best) w->view->scene_planes_slot = -1;
  gpu_slot_rt_[best] = &rt;
  gpu_slot_win_[best] = active_window_;
  gpu_slot_used_[best] = gpu_calls_ + 1;
  return best;
}

/* Task 0197: a lightmap page on the card - a float texture kept by the page's serial (a page is published whole: a
 * new serial when it changes); a page let go is dropped with the textures nothing uses. -1: refused (why in `err`). */
int Editor::gpu_lightmap(const Lightmap &lm, std::string &err) {
  auto found = gpu_lightmaps_.find(&lm);
  if (found != gpu_lightmaps_.end() && found->second.first != lm.serial) {  // (replaced: the old copy let go)
    gpu_forget_texture(found->second.second.get());
    gpu_lightmaps_.erase(found);
    found = gpu_lightmaps_.end();
  }
  if (gpu_lightmaps_.size() > 256) prune_gpu_lightmaps();
  if (found == gpu_lightmaps_.end()) {
    auto t = std::make_shared<Texture>();
    t->srgb = false, t->is_float = true, t->width = lm.width, t->height = lm.height;
    Texture::Level l;
    l.w = lm.width, l.h = lm.height;
    l.pxf.resize(lm.texels.size());
    for (size_t i = 0; i < lm.texels.size(); i++) l.pxf[i] = Vec4(lm.texels[i], 1.0f);
    t->levels.push_back(std::move(l));
    gpu_lightmaps_[&lm] = {lm.serial, t};
    found = gpu_lightmaps_.find(&lm);
  }
  return gpu_texture(found->second.second, err);
}

/* Task 0197: a texture's copy on the card let go now (a lightmap page replaced). */
void Editor::gpu_forget_texture(const Texture *t) {
  auto e = gpu_textures_.find(t);
  if (e == gpu_textures_.end()) return;
  if (e->second.id >= 0 && gpu_view_) gpu_view_->remove_texture(e->second.id);
  gpu_textures_.erase(e);
}

/* Task 0197: the pages whose card copy was already let go (not drawn for a while: gpu_forget_meshes) forgotten here too. */
void Editor::prune_gpu_lightmaps() {
  for (auto it = gpu_lightmaps_.begin(); it != gpu_lightmaps_.end();)
    it = gpu_textures_.count(it->second.second.get()) ? std::next(it) : gpu_lightmaps_.erase(it);
}

/* Task 0197: the probe volume to the card when it changed (another bake, more probes, a live update). */
void Editor::gpu_probe_volume(const ProbeVolumeData &d) {
  const uint64_t key = ((((uint64_t)(uintptr_t)&d * 31 ^ d.scene_key) * 1099511628211ull ^ d.probe_count()) * 1099511628211ull ^ probe_live_gen_) *
                           1099511628211ull ^ lighting_gen_;
  if (key == gpu_probe_key_) return;
  gpu_probe_key_ = key;
  gpu_view_->set_probe_volume(probe_pack(d));
}

/* Task 0196: why an item's live procedural textures need the CPU in this view (seen close up), or null. */
const char *Editor::live_close_up_of(const DrawItem &it, GpuMesh &e, const Mat4 &vp, const Mat4 &p, Vec3 eye, int height) {
  if (!it.materials) return nullptr;
  bool live = false;
  for (const MaterialPtr &m : *it.materials)
    for (int k = 0; m && k < 5; k++) live = live || m->textures().live[k];
  if (!live) return nullptr;
  if (e.stretch_serial != it.mesh->serial || e.uv_stretch < 0.0f) {
    e.uv_stretch = uv_stretch(*it.mesh);
    e.stretch_serial = it.mesh->serial;
  }
  const AABB box = it.mesh->bounds.transformed(it.model);
  const Vec3 c(clampf(eye.x, box.min.x, box.max.x), clampf(eye.y, box.min.y, box.max.y), clampf(eye.z, box.min.z, box.max.z));
  const bool ortho = std::fabs(p.at(3, 3) - 1.0f) < 1e-6f;
  /* A pixel's world size one metre away at the frame's centre, shrunk to the object's farthest place from it on screen
   * (by 1 + tan^2 x + tan^2 y there: off the axis a pixel covers less; its box's corners, within the frame). */
  const float tx = 1.0f / std::fabs(p.at(0, 0)), ty = 1.0f / std::fabs(p.at(1, 1));
  float ox = 0.0f, oy = 0.0f;
  for (int k = 0; k < 8 && !ortho; k++) {
    const Vec3 q((k & 1) ? box.max.x : box.min.x, (k & 2) ? box.max.y : box.min.y, (k & 4) ? box.max.z : box.min.z);
    const Vec4 cl = vp * Vec4(q, 1.0f);
    if (!(cl.w > 1e-6f)) {  // (behind the eye: anywhere in the frame)
      ox = tx, oy = ty;
      break;
    }
    ox = std::max(ox, std::min(tx, std::fabs(cl.x / cl.w) * tx));
    oy = std::max(oy, std::min(ty, std::fabs(cl.y / cl.w) * ty));
  }
  const float px1 = 2.0f / (std::fabs(p.at(1, 1)) * (float)std::max(height, 1)) / (1.0f + ox * ox + oy * oy);
  for (const MaterialPtr &m : *it.materials)
    if (m && live_close_up(it, *m, e.uv_stretch, length(c - eye), px1, ortho)) return "a live procedural texture seen close up";
  return nullptr;
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
  std::vector<uint32_t> hl_bits;  // task 0166: Edit Mode's face highlight, a bit a triangle of the edited mesh
  const bool gouraud = opt.shade == ShadeMode::Gouraud;  // task 0195: Solid shading
  std::vector<float> vcols;
  struct SeeTri {  // task 0195: a see-through triangle, its sort key, its instance
    float key;
    uint32_t inst, prim;
    const DrawItem *it;
  };
  std::vector<SeeTri> see;
  std::vector<std::pair<gpu::RasterInstance, int>> axis_parts;  // task 0195: axis materials' parts, and their owners
  std::vector<size_t> axis_owner;
  const Mat4 vp = p * v;
  auto gather_see_through = [&](const DrawItem &it, uint32_t first_inst, const GpuMesh &e) {
    const RenderMesh &rm = *it.mesh;
    const Mat4 mvp = vp * it.model;
    const bool cull = !((it.double_sided || !opt.backface_culling) && !it.always_cull);
    const float facing = mirrored(it.model) ? -1.0f : 1.0f;
    for (uint32_t prim = 0; prim < (uint32_t)rm.tri_count(); prim++) {
      const uint32_t slot = rm.tri_material.size() == rm.tri_count() ? rm.tri_material[prim] : 0;
      const Material &m = *slot_material(it, slot);
      if (m.opaque() || m.surface == (int)MaterialSurface::Cutout) continue;
      const uint32_t *tri = &rm.indices[(size_t)prim * 3];
      const Vec4 c[3] = {mvp * Vec4(rm.positions[tri[0]], 1.0f), mvp * Vec4(rm.positions[tri[1]], 1.0f), mvp * Vec4(rm.positions[tri[2]], 1.0f)};
      float key;
      if (!see_through_key(c, cull, facing, key)) continue;
      uint32_t inst_at = first_inst;
      for (size_t k = 0; k < e.parts.size(); k++)
        if (e.parts[k][0] == slot) inst_at = first_inst + (uint32_t)k;
      see.push_back({key, inst_at, prim, &it});
    }
  };
  auto highlight_base = [&](const DrawItem &it, const GpuMesh &e) {
    /* One bit per triangle in the card's order: the mesh's own with one material, grouped by slot (card_indices)
     * with several. */
    const RenderMesh &rm = *it.mesh;
    const std::vector<uint8_t> &fh = *it.face_highlight;
    const uint32_t base = (uint32_t)hl_bits.size() * 32;
    hl_bits.resize(hl_bits.size() + (rm.tri_count() + 31) / 32, 0);
    uint32_t pos = base;
    auto set_bit = [&](size_t t) {
      const uint32_t f = rm.tri_face[t];
      if (f < fh.size() && fh[f]) hl_bits[pos / 32] |= 1u << (pos & 31);
      pos++;
    };
    if (e.parts.empty() || rm.tri_material.size() != rm.tri_count()) {
      for (size_t t = 0; t < rm.tri_count() && t < rm.tri_face.size(); t++) set_bit(t);
      return base;
    }
    for (const auto &part : e.parts)
      for (size_t t = 0; t < rm.tri_count() && t < rm.tri_face.size(); t++)
        if (rm.tri_material[t] == part[0]) set_bit(t);
    return base;
  };
  bool ok = true;
  std::string tex_err;  // (a texture the card can't hold: this view on the CPU, the card kept)
  auto tex = [&](const TexturePtr &t) { return gpu_texture(t, tex_err); };
  auto axis_of = [&](size_t i, const DrawItem &it, const Material &m) {
    if (m.axis_mode == 0 || !inst[i].textured) return;
    raster_axis(inst[i], m, *it.mesh, tex, axis_parts);
    axis_owner.resize(axis_parts.size(), i);
  };
  std::vector<float> corners;  // task 0197: the baked-lit triangles' corners
  const VoxelGIFrame *gi = env.gi && env.gi->grid && env.gi->grid->valid() ? env.gi : nullptr;  // (as Renderer3D::flush takes it)
  const bool bounce_onto_lm = gi ? gi->bounce_not_baked : env.rt_bounce_not_baked;
  auto baked_of = [&](const DrawItem &it, const GpuMesh &e, size_t first_inst) {
    const RenderMesh &rm = *it.mesh;
    const bool lm = it.lightmap && it.lightmap_uv && it.lightmap_uv->size() >= rm.tri_count() * 3;
    const bool rt = it.rt_bounce && it.rt_sky && it.rt_uv && it.rt_uv->size() >= rm.tri_count() * 3;
    const bool probes = !lm && !rt && it.use_probes && env.probes && env.probes->data && !env.probes->data->empty();
    if (!lm && !rt && !probes) return;
    const int32_t lm_tex = lm ? gpu_lightmap(*it.lightmap, tex_err) : -1;
    const int32_t rb = rt ? gpu_lightmap(*it.rt_bounce, tex_err) : -1, rs = rt ? gpu_lightmap(*it.rt_sky, tex_err) : -1;
    const int32_t base = lm || rt ? (int32_t)(corners.size() / 24) : -1;
    if (lm || rt) {  // (the card's order: the mesh's, or grouped by material slot as card_indices groups them)
      auto put = [&](size_t t) {
        for (int k = 0; k < 3; k++) {
          const Vec3 p = rm.positions[rm.indices[t * 3 + (size_t)k]];
          const Vec2 a = lm ? (*it.lightmap_uv)[t * 3 + (size_t)k] : Vec2(0.0f, 0.0f), b = rt ? (*it.rt_uv)[t * 3 + (size_t)k] : Vec2(0.0f, 0.0f);
          corners.insert(corners.end(), {p.x, p.y, p.z, a.x, a.y, b.x, b.y, 0.0f});
        }
      };
      const bool grouped = !e.parts.empty() && rm.tri_material.size() == rm.tri_count();
      for (size_t t = 0; !grouped && t < rm.tri_count(); t++) put(t);
      for (size_t p = 0; grouped && p < e.parts.size(); p++)
        for (size_t t = 0; t < rm.tri_count(); t++)
          if (rm.tri_material[t] == e.parts[p][0]) put(t);
    }
    for (size_t k = first_inst; k < inst.size(); k++) {
      gpu::RasterInstance &in = inst[k];
      in.baked = true;
      in.lightmap = lm_tex, in.rt_bounce = rb, in.rt_sky = rs;
      in.corner_base = base < 0 ? -1 : base + (int32_t)(in.first_index / 3);
      in.baked_flags = (probes ? 1u : 0u) | (lm && rt && bounce_onto_lm ? 2u : 0u);
    }
  };
  for (const DrawItem &it : items) {
    if (!it.mesh || it.mesh->indices.empty()) continue;
    const int mesh = gpu_mesh(*it.mesh, err);
    ok = ok && mesh >= 0;
    if (mesh < 0) continue;
    GpuMesh &e = gpu_meshes_[it.mesh];
    if (const char *why = live_close_up_of(it, e, vp, p, eye, rt.height)) return gpu_view_refuse(slot, why);
    const uint32_t hl = it.face_highlight && !it.mesh->tri_face.empty() ? highlight_base(it, e) + 1 : 0;
    const uint32_t vc = gouraud && it.vertex_colors ? solid_vertex_colors(it, vcols) : 0;
    const uint32_t first_inst = (uint32_t)inst.size();
    if (e.parts.empty()) {
      inst.push_back(raster_instance(it, mesh, *slot_material(it, mesh_slots(*it.mesh)[0]), tex));
      inst.back().highlight = hl;
      inst.back().vertex_colors = vc;
      if (gouraud) solid_instance(inst.back(), it);
      axis_of(inst.size() - 1, it, *slot_material(it, mesh_slots(*it.mesh)[0]));  // (after Solid's: no parts for its one colour)
      if (!gouraud) baked_of(it, e, first_inst);  // (task 0197; Solid lights by its own formula)
      if (inst.back().surface) gather_see_through(it, first_inst, e);
      continue;
    }
    for (const auto &part : e.parts) {  // (several materials: one instance per material's triangles)
      gpu::RasterInstance in = raster_instance(it, mesh, *slot_material(it, part[0]), tex);
      in.first_index = part[1];
      in.index_count = part[2];
      in.highlight = hl ? hl + part[1] / 3 : 0;  // (gl_PrimitiveID counts from the draw's first triangle)
      in.vertex_colors = vc;
      if (gouraud) solid_instance(in, it);
      inst.push_back(in);
      axis_of(inst.size() - 1, it, *slot_material(it, part[0]));
    }
    for (size_t k = first_inst; k < inst.size(); k++)
      if (inst[k].surface) {
        gather_see_through(it, first_inst, e);
        break;
      }
    if (!gouraud) baked_of(it, e, first_inst);  // (task 0197; Solid lights by its own formula)
  }
  for (size_t k = 0; k < axis_parts.size(); k++) {  // (the parts after every drawn instance: no index moves)
    inst[axis_owner[k]].axis_parts[axis_parts[k].second] = (float)inst.size();
    inst.push_back(axis_parts[k].first);
  }
  if (ok && !tex_err.empty()) return gpu_view_refuse(slot, tex_err);
  if (!see.empty() && !g->can_draw_see_through()) return gpu_view_refuse(slot, "see-through surfaces (the card has no dual-source blending)");
  if (!see.empty() && opt.screen_door) return gpu_view_refuse(slot, "see-through surfaces drawn as a screen door");
  const bool want_hdr = rt.want_hdr && opt.shade == ShadeMode::Deferred;  // (task 0197: a camera's bloom reads it)
  if (want_hdr && !see.empty()) return gpu_view_refuse(slot, "a picture kept in linear light with see-through surfaces");
  const bool toon_maps = std::any_of(inst.begin(), inst.end(), [](const gpu::RasterInstance &in) { return (in.flags & gpu::RasterInstance::ToonMaps) != 0; });
  const bool baked = std::any_of(inst.begin(), inst.end(), [](const gpu::RasterInstance &in) { return in.baked; });
  if (baked && env.probes && env.probes->data && env.probes->data->probe_count() > kCardMaxProbes)
    return gpu_view_refuse(slot, "a probe volume of more than 262,144 probes");
  if (baked && !g->can_draw_baked()) return gpu_view_refuse(slot, "lightmaps or probes (the card has no geometry shaders)");
  if (toon_maps && !g->can_draw_toon_maps()) return gpu_view_refuse(slot, "toon maps (the card's toon pass couldn't be made)");
  gpu::RasterFrame fr;
  see_through_to_frame(see, fr.see_through);
  fr.corners = std::move(corners);  // (task 0197: the baked light's settings)
  if (env.probes) {
    fr.probe_normal_bias = env.probes->normal_bias, fr.probe_view_bias = env.probes->view_bias;
    fr.probe_leak = env.probes->leak_reduction, fr.probe_live = env.probes->use_live;
    if (env.probes->data) gpu_probe_volume(*env.probes->data);
  }
  fr.gi_specular_occlusion = env.gi ? env.gi->params.specular_occlusion : env.rt_specular_occlusion;
  fr.want_hdr = want_hdr;
  fr.highlight_bits = std::move(hl_bits);
  fr.slot = slot;
  fr.width = rt.width;
  fr.height = rt.height;
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
  if (render_clear_override_on_) fr.background = 0, fr.clear = render_clear_override_;  // (a transparent render's black / white pass: task 0195)
  if (gouraud) {  // (shade()'s ambient: the LightingEnv's own colours, whatever the world)
    fr.gouraud = true;
    fr.vertex_colors = std::move(vcols);
    set3(fr.sky, env.sky);
    set3(fr.equator, env.equator);
    set3(fr.ground, env.ground);
  }
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
  /* Task 0192: the Scene view's overlays with its picture, when the card draws at the view's full size. */
  const bool overlays = scene_full_call_ && scene_overlays_recorded_ && g->can_draw_overlays() && !overlays_off_gpu_for_test_;
  if (overlays) overlays_to_frame(fr);
  g->set_instances(inst);
  /* The picture, and the planes later work reads: depth for picking and overlays, and for a camera's depth of field
   * when it has one; ids for picking (not in a view nobody picks in: task 0092). The colour comes straight into the
   * canvas when its rows are contiguous there, else through a buffer of the view's own size. */
  const bool contiguous = rt.stride == rt.width;
  if (!contiguous) gpu_color_.resize((size_t)rt.width * rt.height);
  const bool dof = cam && cam->dof && !cam->orthographic;
  int card_x = 0, card_y = 0;
  /* (Task 0193: the picture stays on the card - unless the CPU works on it afterwards: a camera's depth of field.) */
  const bool filtered = filters && !filters->passes.empty();  // (task 0197: the CPU's passes work on the picture afterwards)
  const bool to_card = card_present_ && !dof && !filtered && card_view_at(rt, card_x, card_y);
  uint32_t *color = to_card ? nullptr : contiguous ? rt.color : gpu_color_.data();
  /* Task 0204: the Scene view's depth and ids stay on the card when the card draws its picture and overlays: a click, a
   * box select, the knife or a material drop asks for them (ensure_scene_planes), not every frame. Not in verify mode
   * (it compares the planes every frame), nor when the CPU draws the overlays (they test its depth). */
  if (scene_full_call_) scene_planes_slot_ = -1;
  const bool defer_planes = scene_full_call_ && &rt == &scene_rt_ && to_card && overlays && (!edit_mode_ || cage) && !dof && !filtered &&
                            !cache_verify_ && !gpu_split_readback_for_test_;
  const bool want_depth = !rt.depth.empty() && !defer_planes && (planes_wanted_ || dof || filtered),
             want_ids = !rt.ids.empty() && !defer_planes && (planes_wanted_ || filtered);
  float *depth = want_depth ? rt.depth.data() : nullptr;
  uint32_t *ids = want_ids ? rt.ids.data() : nullptr;
  /* Task 0160 (ADR 0051): the frame and its copies out in one submission, one wait (the split path kept for the test
   * that checks the picture is the same). Task 0204: with nothing to copy back the frame is left running on the card
   * while the editor goes on (the present, or the next use of the card, waits for it). */
  const bool nothing_back = !color && !depth && !ids && !want_hdr;
  if (ok) {
    if (gpu_split_readback_for_test_) ok = g->render(fr, &err) && g->read_back(slot, color, depth, ids, &err);
    else if (nothing_back) ok = g->render_async(fr, &err);
    else ok = g->render_read(fr, color, depth, ids, &err);
  }
  if (ok && defer_planes) scene_planes_slot_ = slot, scene_planes_w_ = rt.width, scene_planes_h_ = rt.height;
  if (ok) {  // (task 0204: the card's times of the frames left running that finished meanwhile)
    const gpu::RasterTimes at = g->take_async_times();
    prof_.ms_gpu_shadow += at.shadow, prof_.ms_gpu_gbuffer += at.gbuffer, prof_.ms_gpu_lighting += at.lighting;
  }
  if (ok) {
    const gpu::RasterTimes t = g->times();  // (task 0159: the card's own time in each pass, for the Profiler)
    prof_.ms_gpu_shadow += t.shadow, prof_.ms_gpu_gbuffer += t.gbuffer, prof_.ms_gpu_lighting += t.lighting;
    prof_.ms_gpu_readback += std::max(0.0, t.submit_wall - t.total);  // (the submission's wall time beyond the passes: the copies out and the fence wait; without timestamps the whole submission)
  }
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
  gpu_slot_gen_[slot]++;
  if (to_card) {  // (transparent where its picture shows: what is drawn over it here keeps its coverage, Color::over)
    for (int y = 0; y < rt.height; y++) std::fill(rt.color + (size_t)y * rt.stride, rt.color + (size_t)y * rt.stride + rt.width, 0u);
    card_views_.push_back({slot, card_x, card_y});
  }
  for (int y = 0; !to_card && !contiguous && y < rt.height; y++)
    std::memcpy(rt.color + (size_t)y * rt.stride, &gpu_color_[(size_t)y * rt.width], (size_t)rt.width * 4);
  if (!read_linear_light(rt, slot, want_hdr, env)) return false;  // (task 0197: after the colour - it encodes it again)
  gpu_slot_reason_[slot].clear();
  gpu_view_reason_.clear();
  prof_.gpu_views++;
  if (scene_gpu_call_) scene_on_gpu_ = true;
  cage_on_gpu_ = cage_on_gpu_ || cage;
  prof_.gpu_cage_frames += cage ? 1 : 0;
  overlays_on_gpu_ = overlays_on_gpu_ || overlays;
  prof_.gpu_overlay_frames += overlays ? 1 : 0;
  return true;
}

/* Task 0204: the Scene view's depth and ids, left on the card by its last frame, copied into scene_rt_ before the CPU
 * reads them (picking, box select, the knife, a material drop). Once a frame at most: they stay until the next frame. */
void Editor::ensure_scene_planes() {
  if (scene_planes_slot_ < 0) return;
  const int slot = scene_planes_slot_;
  scene_planes_slot_ = -1;
  RenderTarget &rt = scene_rt_;
  /* (Still the Scene view's slot, at the size it drew: else the target's own planes are newer than the card's.) */
  if (gpu_slot_rt_[slot] != &rt || gpu_slot_win_[slot] != active_window_ || scene_planes_w_ != rt.width || scene_planes_h_ != rt.height) return;
  std::string err;
  if (gpu_view_ && rt.depth.size() == (size_t)rt.width * rt.height &&
      gpu_view_->read_back(slot, nullptr, rt.depth.data(), rt.ids.size() == rt.depth.size() ? rt.ids.data() : nullptr, &err))
    return;
  /* (No card, or it failed: nothing is picked rather than a stale pick.) */
  std::fill(rt.depth.begin(), rt.depth.end(), 1.0f);
  std::fill(rt.ids.begin(), rt.ids.end(), 0u);
  if (!err.empty()) Log::warn("The Scene view's depth couldn't come back from the GPU: %s", err.c_str());
}

/* Task 0197: the picture in linear light into the target, as Renderer3D's begin() and deferred pass fill it ("none",
 * x -1, where the card's pass kept none), or the target's let go when not wanted (as Renderer3D clears it). */
bool Editor::read_linear_light(RenderTarget &rt, int slot, bool want, const LightingEnv &env) {
  if (!want) {
    rt.hdr.clear();
    return true;
  }
  const size_t px = (size_t)rt.width * rt.height;
  gpu_hdr_scratch_.resize(px * 4);
  std::string err;
  if (!gpu_view_->read_hdr(slot, gpu_hdr_scratch_.data(), &err)) {  // (any error: as gpu_view_draw's own - the card set aside)
    Log::warn("The GPU view couldn't read its linear light back (%s): drawing on the CPU", err.c_str());
    gpu_view_error_ = err;
    gpu_slot_reason_[slot] = gpu_view_reason_ = "a GPU error";
    gpu_view_drop();
    prof_.gpu_fallbacks++;
    return false;
  }
  rt.hdr.resize(px);
  rt.hdr_view_transform = env.view_transform;
  rt.hdr_exposure = env.exposure;
  /* Where nothing was lit: "none", or 0 in a transparent render's black and white passes (render_view fills 0 there:
   * no light for bloom in either). Where lit: the colour as the CPU encodes its own linear light, bit for bit, so the
   * passes that check a pixel still holds its linear light (colour grading's exposure, bloom) find it does. */
  const Vec3 none = render_clear_override_on_ ? Vec3(0.0f) : Vec3(-1.0f, 0.0f, 0.0f);
  const float *f = gpu_hdr_scratch_.data();
  JobSystem::global().parallel_for(rt.height, 16, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++) {
      Vec3 *h = rt.hdr.data() + (size_t)y * rt.width;
      uint32_t *c = rt.color + (size_t)y * rt.stride;
      const float *row = f + (size_t)y * rt.width * 4;
      int run = -1;  // (the first of the current run of lit pixels)
      for (int x = 0; x <= rt.width; x++) {
        const bool lit = x < rt.width && row[x * 4] >= 0.0f;
        if (lit) h[x] = Vec3(row[x * 4], row[x * 4 + 1], row[x * 4 + 2]);
        else if (x < rt.width) h[x] = none;
        if (lit && run < 0) run = x;
        if (lit || run < 0) continue;
        display::encode_span(&h[run].x, c + run, (size_t)(x - run), env.view_transform, env.exposure);
        run = -1;
      }
    }
  });
  return true;
}

/* Task 0193: whether the card presents the window drawing now (it is open, wanted, and its swapchain attached - made
 * here the first time), and its views for this frame forgotten. */
void Editor::begin_card_frame() {
  card_views_.clear();
  card_present_ = false;
  if (card_present_for_test_) {
    card_present_ = gpu_view_wanted() && card_present_pref_ && gpu_view() != nullptr;
    return;
  }
  /* (Verify mode compares each cached picture with a fresh one: a hole the card fills would always compare equal.) */
  const bool want = gpu_view_wanted() && card_present_pref_ && !headless_ && window_ && !card_off_ && !cache_verify_;
  if (!want) return card_detach();
  gpu::Raster *g = gpu_view();
  if (g && (card_win_ >= 0 || card_attach(*g))) card_present_ = true;
}

/* The card's swapchain on this window (false, and the system shows it from now on, when the card can't). */
bool Editor::card_attach(gpu::Raster &g) {
  std::string err;
  card_win_ = g.attach_window(platform::native_display(window_), platform::native_handle(window_), &err);
  present_all_ = true;
  if (card_win_ >= 0) {
    Log::info("The card presents this window (%s)", g.info().name.c_str());
    return true;
  }
  card_off_ = true;
  card_reason_ = err.empty() ? "the card can't present this window" : err;
  Log::info("This window is shown by the system: %s", card_reason_.c_str());
  return false;
}

/* This window no longer presented by the card (the setting, the device): its swapchain let go, so the system's present
 * reaches the window, all of it the next time. */
void Editor::card_detach() {
  if (card_win_ < 0) return;
  if (gpu_view_) gpu_view_->detach_window(card_win_);
  card_win_ = -1;
  present_all_ = true;
  scene_cache_.key = game_cache_.key = 0;  // (pictures that were the card's)
}

/* The card may present the windows again (Viewport Device or `viewport present card` chosen again). */
void Editor::card_retry() {
  card_off_ = false, card_reason_.clear();
  for (auto &w : windows_) w->card_off = false, w->card_reason.clear();
}

/* Where a view drawing into `rt` sits in the window, when rt is a full-size part of the window's canvas (a scaled or
 * separate target is read back as before). */
bool Editor::card_view_at(const RenderTarget &rt, int &x, int &y) const {
  if (!rt.color || fb_.pixels.empty() || rt.stride != fb_.width) return false;
  const uint32_t *base = fb_.pixels.data();
  if (rt.color < base || rt.color >= base + fb_.pixels.size()) return false;
  const size_t off = (size_t)(rt.color - base);
  x = (int)(off % (size_t)fb_.width), y = (int)(off / (size_t)fb_.width);
  return x + rt.width <= fb_.width && y + rt.height <= fb_.height;
}

/* Presenting through the card failed: the system shows this window from now on, every view drawn again for it. */
void Editor::card_present_failed(const std::string &why) {
  Log::warn("The card can't present this window (%s): the system shows it", why.c_str());
  card_off_ = true;
  card_reason_ = why;
  if (gpu_view_ && card_win_ >= 0) gpu_view_->detach_window(card_win_);
  card_win_ = -1;
  scene_cache_.key = game_cache_.key = 0;  // (their pictures were the card's)
  present_all_ = true;
  ui_.redraw = true;
}

/* The card set aside or made again: no window is attached to it any more (each attaches again when it draws). */
void Editor::card_forget_windows() {
  card_win_ = -1, card_present_ = false;
  card_views_.clear(), card_views_shown_.clear();
  for (auto &w : windows_) w->card_win = -1, w->card_present = false, w->card_views.clear(), w->card_views_shown.clear();
  scene_cache_.key = game_cache_.key = 0;
  present_all_ = true;
}

/* The window as shown: the card's frame when it shows views, else the canvas (screenshots). */
std::vector<uint32_t> Editor::window_pixels() {
  if (card_views_shown_.empty() || !gpu_view_) return fb_.pixels;
  std::vector<uint32_t> out(fb_.pixels.size());
  std::string err;
  if (gpu_view_->compose_read(fb_.pixels.data(), fb_.width, fb_.height, card_views_shown_, out.data(), &err)) return out;
  Log::warn("The window's picture couldn't be read from the card (%s)", err.c_str());
  return fb_.pixels;
}

bool Editor::card_frame_for_test(std::vector<uint32_t> &out) {
  if (!gpu_view_) return false;
  out.resize(fb_.pixels.size());
  std::string err;
  return gpu_view_->compose_read(fb_.pixels.data(), fb_.width, fb_.height, card_views_, out.data(), &err);
}

/* The card set aside (an error): what was kept there is forgotten with it, so a card opened again - perhaps at the
 * same address - gets everything again. */
void Editor::gpu_view_drop() {
  scene_planes_slot_ = -1;  // (task 0204: the card's planes go with it)
  card_forget_windows();  // (task 0193)
  gpu_meshes_.clear();
  gpu_textures_.clear();
  gpu_world_tex_ = nullptr;
  gpu_mesh_bytes_ = 0;
  gpu_cage_v_.clear();
  gpu_cage_e_.clear();
  gpu_cage_owner_ = nullptr;
  gpu_cage_edges_key_ = 0;
  gpu_lightmaps_.clear();  // (task 0197: a card opened again gets the pages and the probe volume again)
  gpu_probe_key_ = 0;
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
  uint64_t h = kFnvBasis;
  auto mix = [&](const void *p, size_t n) { h = mix_bytes(h, p, n); };
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
  const bool dots = elem_ == EditElement::Vertex, face_dots = elem_ == EditElement::Face;
  if (gpu_cage_owner_ != &g) {  // (a card opened again holds nothing yet)
    gpu_cage_v_.clear();
    gpu_cage_e_.clear();
    gpu_cage_verts_key_ = 0;
    gpu_cage_owner_ = &g;
  }
  /* Task 0205: the vertices again only when what they are worked out from changed (a frame that only redraws - an orbit,
   * the views drawn again - sends nothing and builds nothing: on 500k faces that was 3 ms a frame). */
  uint64_t vkey = kFnvBasis;
  {
    const uint64_t head[6] = {m.uid.v, m.version, m.deform_version, (uint64_t)elem_, (uint64_t)m.vert_count(), (uint64_t)ngon_mode_};
    vkey = mix_bytes(vkey, head, sizeof(head));
    vkey = mix_bytes(vkey, w.m, sizeof(w.m));
    if (dots) vkey = mix_bytes(vkey, vert_sel_.data(), vert_sel_.size());
    if (face_dots) vkey = mix_bytes(vkey, face_sel_.data(), face_sel_.size());
  }
  const bool verts_same = vkey == gpu_cage_verts_key_ && gpu_cage_v_.size() >= m.vert_count();
  std::vector<gpu::CageVertex> v;
  if (!verts_same) {
    /* Task 0205: Face mode's dots go after the mesh's vertices, kept on the card with them (they were worked out and
     * sent again every frame: on 500k faces, 10 ms of it). */
    std::vector<Vec3> fpos;
    std::vector<uint32_t> fcol;
    if (face_dots) edit_face_dots(m, w, fpos, fcol);
    prof_.gpu_cage_vert_builds++;
    v.resize(m.vert_count() + fpos.size());
    JobSystem::global().parallel_for((int64_t)m.vert_count(), 4096, [&](int64_t a, int64_t b) {
      for (int64_t i = a; i < b; i++) {
        const Vec3 p = w.point(m.positions[(size_t)i]);
        v[(size_t)i] = {{p.x, p.y, p.z}, dots ? cage_dot_color((size_t)i) : 0u};
      }
    });
    for (size_t i = 0; i < fpos.size(); i++) v[m.vert_count() + i] = {{fpos[i].x, fpos[i].y, fpos[i].z}, fcol[i]};
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
  const bool vpart = verts_same || changed_runs(v, gpu_cage_v_, vr), epart = edges_same || changed_runs(e, gpu_cage_e_, er);
  uint64_t bytes = 0;
  for (const auto &r : vr) bytes += (uint64_t)r.second * sizeof(gpu::CageVertex);
  for (const auto &r : er) bytes += (uint64_t)r.second * sizeof(gpu::CageEdge);
  bytes += vpart ? 0 : v.size() * sizeof(gpu::CageVertex);
  bytes += epart ? 0 : e.size() * sizeof(gpu::CageEdge);
  if ((!verts_same && !g.set_cage_vertices(v, vpart ? &vr : nullptr, &err)) || (!edges_same && !g.set_cage_edges(e, epart ? &er : nullptr, &err))) {
    gpu_cage_owner_ = nullptr;  // (what the card holds is unknown: everything again next time)
    gpu_cage_edges_key_ = gpu_cage_verts_key_ = 0;
    return false;
  }
  prof_.gpu_cage_bytes += bytes;
  if (!verts_same) gpu_cage_v_ = std::move(v);
  gpu_cage_verts_key_ = vkey;
  if (!edges_same) gpu_cage_e_ = std::move(e);
  gpu_cage_edges_key_ = key;
  if (face_dots) prof_.gpu_face_dot_frames++;  // task 0192: Face mode's dots over the cage (task 0205: the cage's vertices after the mesh's)
  fr.cage = true;
  fr.cage_bias = kCageBias;
  fr.cage_point_radius = dots ? cage_dot_radius() : face_dots ? ui_.px(ngon_mode_ ? 2.5f : 2.0f) : 0.0f;  // (edit_face_dots' radii, at this frame's scale)
  fr.cage_point_first = face_dots ? (uint32_t)m.vert_count() : 0u;
  return true;
}

}  // namespace bl

// SPDX-License-Identifier: GPL-2.0-or-later
// Camera filters (ADR 0007): what a camera's filter components ask of a render.
// - The raster stage: settings for the rasterizer while it draws (vertex snap, affine UVs,
//   texture rules).
// - The internal resolution: the camera renders smaller and is scaled up nearest-neighbour.
// - The image passes: functions over the finished 8-bit target and its depth (fog, colour depth,
//   dither), in component order.
// The retro looks follow the consoles' documented behaviour: the PS1 GPU drew at integer vertex
// positions with affine texture mapping, 15-bit colour and a fixed 4x4 dither (psx-spx, "GPU
// Rendering"; "Dither Matrix").
#pragma once

#include "raster.h"

#include <functional>
#include <vector>

namespace bl {

/* What a pass may read beside the colour: the camera, to turn depth into distance. Null for
 * the path tracer, which keeps no depth buffer (passes that need depth skip themselves). */
struct FilterFrame {
  Mat4 inv_view_proj;
  Vec3 eye, forward{0, 0, 1};
  float far_distance = 1000.0f;
  /* Animated effects (grain, flicker) move only when `animate` (the editor sets it in Play mode),
   * so editor views and renders are reproducible. */
  float time = 0.0f;
  bool animate = false;
};

using FilterPass = std::function<void(RenderTarget &, const FilterFrame *)>;

struct FilterStack {
  /* Raster stage. */
  float vertex_snap = 0.0f;
  bool affine_uv = false;
  TexOverride tex;
  bool screen_door = false;
  /* Internal resolution: 0 = the view's own. Fill keeps `height` rows and gives the width the
   * view's aspect (square pixels); Letterbox renders width x height and fits it, with bars. */
  enum Fit { Fill = 0, Letterbox = 1 };
  int width = 0, height = 0, fit = Fill;
  std::vector<FilterPass> passes;

  bool empty() const { return vertex_snap <= 0.0f && !affine_uv && !tex.active() && !screen_door && height <= 0 && passes.empty(); }
  void apply_raster(RasterOptions &o) const {
    o.vertex_snap = vertex_snap;
    o.affine_uv = affine_uv;
    o.tex = tex;
    o.screen_door = screen_door;
  }
};

/* Where an internal image of the stack's resolution goes in a dw x dh view, and its size.
 * Never larger than the view (a filter can't add pixels); at least 1 x 1. */
void filter_layout(const FilterStack &s, int dw, int dh, int &iw, int &ih, Recti &dst);

/* Nearest-neighbour scaling of src (sw x sh, row stride sstride) into dst's rectangle r. */
void upscale_nearest(const uint32_t *src, int sw, int sh, int sstride, uint32_t *dst, int dstride, const Recti &r);

/* The retro colour pass. */
struct RetroImageParams {
  enum Depth { Full = 0, Bits15 = 1, Palette256 = 2 };
  enum Dither { NoDither = 0, Ps1 = 1, Bayer4 = 2 };
  int color_depth = Full;
  int dither = NoDither;
  bool fog = false;
  float fog_start = 10.0f, fog_end = 50.0f;  // metres along the view
  Vec3 fog_color{0.5f, 0.5f, 0.5f};          // display colour, 0..1
};
/* The dither offset added to each 8-bit channel at (x, y) before truncation: -4..+3. */
int retro_dither_offset(int dither, int x, int y);
/* The 256-colour palette (0xFF RRGGBB): a 6x6x6 colour cube (0, 51 ... 255 per channel, the
 * "web-safe" layout VGA games often loaded) and 40 greys between its levels. */
const uint32_t *retro_palette();
/* The palette entry nearest an 8-bit colour, exactly (each palette colour maps to itself). */
uint32_t retro_palette_nearest(int r, int g, int b);
/* Fog (needs frame), then dither and either truncate to 5 bits per channel or map to the
 * 256-colour palette. Pixels with nothing drawn (depth 1, the sky) take no fog. */
void apply_retro_image(RenderTarget &rt, const RetroImageParams &p, const FilterFrame *frame);

/* ---- Color, Lens and Stylize passes (task 0006). All clamp their settings (NaN / infinite act as
 * neutral) and work at any size. ---- */
struct ColorGradingParams {
  float exposure = 0.0f;    // stops
  float contrast = 0.0f;    // -1..1, around middle grey (0.18)
  float saturation = 0.0f;  // -1 (grey) .. 1
  float temperature = 0.0f, tint = 0.0f;  // -1..1: warmer / cooler, magenta / green
  Vec3 lift{0, 0, 0}, gamma{1, 1, 1}, gain{1, 1, 1};
};
void apply_color_grading(RenderTarget &rt, const ColorGradingParams &p);
void apply_posterize(RenderTarget &rt, int levels);
void apply_grayscale(RenderTarget &rt, bool sepia, float amount);
void apply_invert(RenderTarget &rt, float amount);
struct VignetteParams {
  float intensity = 0.4f, smoothness = 0.5f, roundness = 1.0f;
  Vec3 color{0, 0, 0};  // display colour
};
void apply_vignette(RenderTarget &rt, const VignetteParams &p);
void apply_chromatic_aberration(RenderTarget &rt, float intensity);
struct GrainParams {
  float intensity = 0.3f, size = 1.0f, response = 0.8f;
};
void apply_film_grain(RenderTarget &rt, const GrainParams &p, const FilterFrame *frame);
void apply_lens_distortion(RenderTarget &rt, float intensity, float scale);
void apply_pixelate(RenderTarget &rt, int cell);
struct OutlineParams {
  Vec3 color{0, 0, 0};     // display colour
  int thickness = 1;       // pixels
  float depth_sensitivity = 0.05f;  // relative change in distance that counts as an edge
  bool object_edges = true;          // where one object meets another (ids)
};
void apply_edge_outline(RenderTarget &rt, const OutlineParams &p, const FilterFrame *frame);
struct CrtParams {
  float scanlines = 0.5f, curvature = 0.2f, mask = 0.3f, flicker = 0.0f;
};
void apply_crt(RenderTarget &rt, const CrtParams &p, const FilterFrame *frame);
void apply_sharpen(RenderTarget &rt, float amount);

}  // namespace bl

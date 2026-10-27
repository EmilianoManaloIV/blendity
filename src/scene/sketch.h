// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// A sketch drawn over the Scene view (task 0223, ADR 0063): layers of pen strokes on a canvas, and the geometry that
// corrects a stroke toward what it meant - a line, an arc, an ellipse, a smooth curve, split at its corners - the way
// SketchBook's predictive stroke and Igarashi's Pegasus (1997) do. Plain data and maths: the editor draws and
// drives it (editor/sketch_tool.cpp), scene_io saves it.
//
// Coordinates: a stroke's points are in its canvas's own frame. The picture-plane canvas (the only kind in task 0223)
// measures in view heights: y runs 0..1 down the view, x 0..aspect across it, so a drawing keeps its place when the
// window changes. The fitting below works in pixels; the editor converts.

#include "../core/math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bl {

struct SketchPoint {
  float x = 0.0f, y = 0.0f;
  float w = 1.0f;  // the pen's pressure, 0..1 (1 for a mouse)
};

enum class StrokeKind { Freehand, Line, Arc, Ellipse, Curve };

/* A canvas (task 0224): the picture plane (id 0, kind 0: the stroke's frame is the view's), or a plane in the scene
 * (kind 1: the frame is metres along u and v from the origin). */
struct SketchCanvas {
  int id = 0;
  int kind = 0;
  std::string name = "View";
  Vec3 origin, u{1, 0, 0}, v{0, 0, 1};
};

struct SketchStroke {
  int layer = 0;
  int canvas = 0;  // (SketchCanvas::id; 0 the picture plane)
  uint32_t color = 0xFF202020;
  float width = 2.0f;  // pixels at the UI's 1x
  StrokeKind kind = StrokeKind::Freehand;
  bool closed = false;
  std::vector<SketchPoint> pts;  // what is drawn: the corrected stroke
  std::vector<SketchPoint> raw;  // as the pen drew it (empty when pts is it), kept for another correction later
  /* The fit's parameters, in the canvas's frame (an arc, an ellipse): the centre, the radii, the first axis's angle,
   * where the arc starts and how far it sweeps (radians, counter-clockwise on a y-down canvas is negative). */
  Vec2 center;
  float rx = 0.0f, ry = 0.0f, angle = 0.0f, start = 0.0f, sweep = 0.0f;
};

struct SketchLayer {
  std::string name = "Layer";
  float opacity = 1.0f;
  bool visible = true;
  int correct = 2;  // 0 none, 1 light, 2 full
};

/* The scene's sketch: its layers and strokes, and the perspective guide - saved with the scene, since its vanishing
 * points are the camera's directions (task 0224). */
struct Sketch {
  std::vector<SketchLayer> layers;
  std::vector<SketchStroke> strokes;
  int active_layer = 0;
  std::vector<SketchCanvas> canvases;  // (the planes in the scene; the picture plane isn't listed)
  int active_canvas = 0;
  int perspective = 0;  // 0 off, 1-3 point
  /* The guide follows the camera and the camera the guide (task 0224): its points are the world axes' vanishing
   * points - X, Z and (3-point) Y, each the end with the camera in front of it. */
  bool follow_camera = true;
  Vec2 vp[3] = {{-0.4f, 0.5f}, {2.0f, 0.5f}, {0.8f, 3.0f}};  // (view heights; 2 and 3 are the second and third points)
  bool vp_placed = false;  // (the editor places them on the view when first shown, unless a file or a command did)
  bool empty() const {
    const bool plain = layers.empty() || (layers.size() == 1 && layers[0].visible && layers[0].opacity == 1.0f && layers[0].correct == 2 && layers[0].name == "Layer");
    return strokes.empty() && plain && perspective == 0 && active_layer == 0 && canvases.empty() && active_canvas == 0 && follow_camera;
  }
  SketchLayer &layer_at(int i);
  const SketchCanvas *canvas_at(int id) const;
  SketchCanvas *canvas_at(int id);
};

namespace sketch {

/* One piece of a corrected stroke. */
struct Fit {
  StrokeKind kind = StrokeKind::Freehand;
  std::vector<Vec2> pts;  // the piece drawn (a line: its two ends; an arc or ellipse: sampled; a curve: smoothed)
  bool closed = false;
  Vec2 center;
  float rx = 0.0f, ry = 0.0f, angle = 0.0f, start = 0.0f, sweep = 0.0f;
  float error = 0.0f;  // the fit's largest distance from the drawn points
};

/* The polyline sampled every `spacing` along its length (the first and last points kept). */
std::vector<Vec2> resample(const std::vector<Vec2> &pts, float spacing);
/* Where a resampled polyline turns sharply: indices of its corners, in order (never the ends). `k` samples each side
 * give the directions compared; a turn over `turn_deg` that is the largest within k is a corner. */
std::vector<size_t> corners(const std::vector<Vec2> &pts, int k, float turn_deg);
/* Fits within `tol` (the largest distance of a point from the fit). False when the points don't fit. */
bool fit_line(const std::vector<Vec2> &pts, float tol, Fit &out);
bool fit_arc(const std::vector<Vec2> &pts, float tol, bool closed, Fit &out);
bool fit_ellipse(const std::vector<Vec2> &pts, float tol, Fit &out);  // (a closed stroke)
/* The stroke as a smooth curve: Gaussian smoothing of the resampled polyline, the ends kept. */
Fit fit_curve(const std::vector<Vec2> &pts, float spacing);
/* The whole correction: level 0 gives the stroke back as it was (lightly smoothed), 1 light, 2 full. The pieces come
 * in the stroke's order and meet at its corners. A scribble (too many corners, pieces too short to mean anything)
 * stays one freehand piece. Pixels in, pixels out. */
std::vector<Fit> correct(const std::vector<Vec2> &pts, int level);
/* An arc or ellipse sampled for drawing: `n` points from `start` through `sweep`. */
std::vector<Vec2> sample_arc(Vec2 center, float rx, float ry, float angle, float start, float sweep, int n);

/* Guides. */
Vec2 snap_to_line(Vec2 p, Vec2 a, Vec2 b);  // the nearest point of the line through a and b
Vec2 snap_to_ellipse(Vec2 p, Vec2 center, float rx, float ry, float angle);  // the nearest point on the ellipse
Vec2 snap_to_polyline(Vec2 p, const std::vector<Vec2> &pts);
Vec2 mirror(Vec2 p, Vec2 a, Vec2 dir);  // across the line through a along dir
/* The vanishing point among `n` whose line through `from` best matches the direction toward `to`; -1 when none is
 * within `max_deg`. */
int nearest_vanishing(Vec2 from, Vec2 to, const Vec2 *vps, int n, float max_deg);
/* The distance from p to the polyline. */
float distance_to_polyline(Vec2 p, const std::vector<Vec2> &pts);
/* A French curve's template, as a unit polyline (centred, about 1 unit across): 0 a gentle S, 1 a C, 2 a tightening
 * spiral. */
std::vector<Vec2> french_curve(int which);

}  // namespace sketch
}  // namespace bl

// SPDX-License-Identifier: GPL-2.0-or-later
// What Weight Paint and Vertex Paint share (task 0525; automasking from task 0519): the per-mesh helpers a stroke needs
// (normals, neighbours, the X-mirror map), a stroke's automask, and the automask options both brushes carry.
#pragma once
#include "../core/math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bl {

struct Mesh;

extern const char *const kWeightCavityModes[3];  // Off, Cavity, Inverted (task 0519)

/* Blender 5.3's mesh automasking options and defaults (task 0519). Limits in degrees; falloffs as fractions of pi.
 * A base of WeightBrush and VertexBrush, so both keep the same field names. */
struct PaintAutomaskFields {
  bool am_topology = false, am_boundary = false, am_view_normal = false, am_area_normal = false;
  int am_cavity = 0;  // kWeightCavityModes: Off, Cavity, Inverted
  int am_boundary_steps = 1, am_cavity_blur = 2;
  float am_cavity_factor = 0.5f;
  float am_view_limit = 90.0f, am_view_falloff = 0.25f, am_area_limit = 20.0f, am_area_falloff = 0.25f;
  bool am_any() const { return am_topology || am_boundary || am_cavity != 0 || am_view_normal || am_area_normal; }
};

/* `<prefix>automask <option> [on|off]` and its numbers (task 0519): false, with a warning, on bad input. */
bool paint_automask_command(PaintAutomaskFields &b, const std::vector<std::string> &t, const char *mode_name);

/* The proportional-editing falloff curves (kWeightFalloffs): 1 at the centre, 0 at the radius. */
float paint_falloff(int kind, float t);

/* The per-mesh helpers a stroke needs, kept while the mesh is the same version (task 0046, shared since task 0525). */
struct PaintMeshHelpers {
  const Mesh *mesh = nullptr;  // what these were made for (nullptr: make them again)
  uint64_t version = ~0ull;
  bool mirror_done = false;
  std::vector<Vec3> normals;               // object space, per vertex
  std::vector<uint32_t> nb_off, nb;        // the neighbours, CSR
  std::vector<uint32_t> mirror;            // each vertex's X-mirror twin (UINT32_MAX: none)
  void prepare(const Mesh &m, bool mirror_x);
  void forget() { mesh = nullptr; }
};

/* One dab's inputs (Editor::paint_dab_amounts): where, how big and how strong, in the Scene view's own pixels. */
struct PaintDab {
  const Mesh *mesh = nullptr;
  const Mat4 *world = nullptr;
  float x = 0, y = 0, radius = 1, strength = 0;
  int falloff = 0;  // kWeightFalloffs
  bool front_only = true, mirror_x = false;
  const PaintAutomaskFields *automask = nullptr;
  uint64_t obj = 0;  // the painted object's id (the view's id plane: where the stroke starts)
};

/* A stroke's automask (task 0519), made at its first dab that reaches a vertex. */
struct PaintAutomask {
  bool started = false;
  std::vector<uint8_t> island;  // Topology: the island the stroke started on
  std::vector<float> cavity;    // per vertex, -1 until worked out (lazily, from the dab's parallel loop)
  Vec3 area_normal, view_dir;   // world space
  std::vector<int> edge_dist;   // Mesh Boundary: edge steps from an open edge, -1 beyond
  int edge_steps = 1;
  /* From the start vertex and the area normal the editor picked (world space). */
  void begin(const PaintAutomaskFields &b, const PaintMeshHelpers &h, const Mesh &m, size_t start, Vec3 area, Vec3 start_normal, Vec3 view_dir,
             bool mirror_x);
  /* Vertex v's factor, 0..1 (the options multiplied). Writes only v's own cavity slot: safe from a parallel loop. */
  float factor(const PaintAutomaskFields &b, const PaintMeshHelpers &h, const Mesh &m, const Mat4 &N, size_t v);
};

}  // namespace bl

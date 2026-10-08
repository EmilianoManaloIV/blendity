// SPDX-License-Identifier: GPL-2.0-or-later
// Polygon mesh using Blender's own memory layout (blender/source/blender/
// makesdna/DNA_mesh_types.h): a flat `positions` array, `face_offsets`
// (face i spans corners [face_offsets[i], face_offsets[i+1])) and
// `corner_verts`. N-gons are first-class, like Blender; triangulation only
// happens for rendering, like Unity's MeshFilter would require.
// Theory: Fundamentals of Computer Graphics ch. 12.1 "Triangle Meshes".
#pragma once

#include "../core/math.h"
#include "../render/raster.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace bl {

struct Mesh {
  std::string name = "Mesh";
  std::vector<Vec3> positions;
  std::vector<uint32_t> face_offsets{0};  // size faces + 1
  std::vector<uint32_t> corner_verts;
  bool smooth = false;  // Blender's "Shade Smooth" / Unity's smoothing angle
  /* Blender 4.1+ "Smooth by Angle": with smooth shading, edges sharper than
   * this (degrees) stay hard. 180 = fully smooth. */
  float smooth_angle = 180.0f;

  /* --- attributes (Blender stores these as named attributes on the mesh) --- */
  /* Corner-domain UV map ("UVMap"); empty = mesh has no UVs. Unity: Mesh.uv. */
  std::vector<Vec2> uvs;
  /* Face-domain material slot index ("material_index"); empty = all slot 0.
   * Unity: one sub-mesh per material. */
  std::vector<int32_t> face_material;
  /* UV seams as sorted edge keys (lo << 32 | hi) - Blender's ".uv_seam" edge attribute. */
  std::vector<uint64_t> seams;
  /* Face-domain smooth shading (Blender stores the inverse, "sharp_face"); empty =
   * every face follows `smooth`. Shade Smooth / Flat on selected faces fills it. */
  std::vector<uint8_t> face_smooth;
  /* Hard edges as sorted edge keys (Blender: Mark Sharp, "sharp_edge"): smooth
   * shading stops at them. */
  std::vector<uint64_t> sharp_edges;
  /* UV seams also act as hard edges (a Unity-style habit; Blender keeps them apart). */
  bool seams_sharp = false;

  size_t vert_count() const { return positions.size(); }
  size_t face_count() const { return face_offsets.size() - 1; }
  size_t corner_count() const { return corner_verts.size(); }
  uint32_t face_size(size_t f) const { return face_offsets[f + 1] - face_offsets[f]; }
  const uint32_t *face_verts(size_t f) const { return corner_verts.data() + face_offsets[f]; }
  bool has_uvs() const { return !uvs.empty() && uvs.size() == corner_verts.size(); }
  int material_of(size_t f) const { return f < face_material.size() ? face_material[f] : 0; }
  int material_count() const;  // highest slot index + 1
  static uint64_t edge_key(uint32_t a, uint32_t b) { return a < b ? ((uint64_t)a << 32) | b : ((uint64_t)b << 32) | a; }
  bool is_seam(uint32_t a, uint32_t b) const;
  void set_seam(uint32_t a, uint32_t b, bool on);
  bool is_sharp(uint32_t a, uint32_t b) const;
  void set_sharp(uint32_t a, uint32_t b, bool on);
  bool smooth_of(size_t f) const { return f < face_smooth.size() ? face_smooth[f] != 0 : smooth; }
  bool any_smooth() const;  // any face shaded smooth
  void set_face_smooth(size_t f, bool on);  // fills face_smooth from `smooth` the first time

  void clear();
  uint32_t add_vert(Vec3 p) { positions.push_back(p); return (uint32_t)positions.size() - 1; }
  void add_face(std::initializer_list<uint32_t> verts);
  /* uv may be null (zeros are added when the mesh has a UV map); mat < 0 = slot 0. */
  void add_face(const uint32_t *verts, size_t n, const Vec2 *uv = nullptr, int mat = -1);
  /* Keeps attribute arrays the right size after topology edits. */
  void sync_attributes();

  Vec3 face_normal(size_t f) const;  // Newell's method, robust for n-gons
  Vec3 face_center(size_t f) const;
  AABB bounds() const;
  size_t edge_count() const;
  /* Unique undirected edges (v0 < v1). */
  void edges(std::vector<std::pair<uint32_t, uint32_t>> &out) const;
  size_t memory_bytes() const;

  /* Triangulated, render-ready cache. Rebuilt lazily when `version` changes.
   * Vertices are split where normals or UVs differ (like a Unity mesh). */
  const RenderMesh &render_mesh(bool force_flat = false) const;
  /* Same, plus MikkTSpace tangents for normal mapping (blender/intern/mikktspace). */
  const RenderMesh &render_mesh_tangents(bool force_flat = false) const;
  /* Unique edges, cached per version (wireframe & edit-mode overlays). */
  const std::vector<std::pair<uint32_t, uint32_t>> &edge_cache() const;
  void touch() { version++; }
  uint64_t version = 1;

 private:
  mutable std::vector<std::pair<uint32_t, uint32_t>> edges_;
  mutable uint64_t edges_version_ = 0;
  /* One cache per variant (0 = as shaded, 1 = forced flat for Edit Mode) so a
   * renderer holding pointers to one never sees it rebuilt as the other. */
  mutable RenderMesh cache_[2];
  mutable uint64_t cache_version_[2] = {0, 0};
  mutable bool cache_tangents_[2] = {false, false};
};

using MeshPtr = std::shared_ptr<Mesh>;

/* Copy-on-write: returns a uniquely owned mesh, cloning if shared. This is
 * Blender's implicit sharing (blender/source/blender/blenlib/BLI_implicit_sharing.hh)
 * and is what makes undo snapshots cheap. */
MeshPtr &mesh_make_mutable(MeshPtr &m);

/* ------------------------------------------------------------- Primitives */
/* Equivalent to blender/source/blender/bmesh/operators/bmo_primitive.cc and
 * Unity's GameObject > 3D Object menu. All are sized like Unity primitives. */
namespace primitives {
MeshPtr cube(float size = 1.0f);
MeshPtr plane(float size = 10.0f, int subdivisions = 10);
MeshPtr quad(float size = 1.0f);
MeshPtr uv_sphere(float radius = 0.5f, int segments = 24, int rings = 16);
MeshPtr ico_sphere(float radius = 0.5f, int subdivisions = 2);
MeshPtr cylinder(float radius = 0.5f, float height = 2.0f, int segments = 24);
MeshPtr cone(float radius = 0.5f, float height = 1.0f, int segments = 24);
MeshPtr torus(float major = 0.5f, float minor = 0.2f, int major_seg = 32, int minor_seg = 16);
MeshPtr grid(float size, int nx, int nz);
}  // namespace primitives

/* --------------------------------------------------------------- Operators */
/* The "Edit Mode" toolset. Each mirrors a Blender operator. */
namespace meshops {
/* An explicit edge selection for the edge tools. Normally an edge counts as
 * selected when both its vertices are, so two opposite sides of a quad would
 * select all four. In edge select mode Blender keeps edges themselves; while
 * a scope is alive, every tool that reads edges from vert_sel uses this set
 * (Mesh::edge_key values) instead. */
struct EdgeSelectionScope {
  explicit EdgeSelectionScope(const std::unordered_set<uint64_t> *edges);
  ~EdgeSelectionScope();
  EdgeSelectionScope(const EdgeSelectionScope &) = delete;
  EdgeSelectionScope &operator=(const EdgeSelectionScope &) = delete;
  const std::unordered_set<uint64_t> *previous;
};
bool edge_selected(uint32_t a, uint32_t b, const std::vector<uint8_t> &vert_sel);
/* Catmull-Clark subdivision (Blender: Subdivision Surface modifier,
 * blender/source/blender/blenkernel/intern/subdiv_mesh.cc via OpenSubdiv). */
Mesh subdivide_catmull_clark(const Mesh &in);
/* Midpoint subdivision without smoothing (Blender: Subdivide, simple). */
Mesh subdivide_simple(const Mesh &in);
/* Several levels at once, configured like Blender's Subdivision Surface
 * modifier. Blendity's parallel Catmull-Clark by default; OpenSubdiv (when
 * built with Blender's libraries) on request, for meshes without UV seams. */
Mesh subdivide(const Mesh &in, int levels, bool smooth);
void set_subdiv_opensubdiv(bool on);  // stress tests compare both
bool subdiv_opensubdiv_available();
/* Fan/ear triangulation (blender/source/blender/bmesh/operators/bmo_triangulate.cc). */
void triangulate(Mesh &m);
/* One face's triangles as local corner indices (3 per triangle). */
void triangulate_face_local(const Mesh &m, size_t f, std::vector<uint32_t> &local);
void flip_normals(Mesh &m);
/* Merge vertices closer than `dist` using a spatial hash
 * (blender/source/blender/bmesh/operators/bmo_removedoubles.cc). Returns removed count. */
size_t merge_by_distance(Mesh &m, float dist);
/* Same result, O(n^2) reference implementation (used by stress tests). */
size_t merge_by_distance_naive(Mesh &m, float dist);
/* Region extrude of selected faces along their averaged normal
 * (blender/source/blender/bmesh/operators/bmo_extrude.cc). Selection is
 * updated to the new cap faces. */
void extrude_faces(Mesh &m, std::vector<uint8_t> &face_sel, float distance);
/* Individual inset of selected faces (Blender: Inset Faces, individual). */
void inset_faces(Mesh &m, std::vector<uint8_t> &face_sel, float amount);
/* Removes selected faces and any vertices left unused. */
void delete_faces(Mesh &m, const std::vector<uint8_t> &face_sel);
/* Removes vertices not referenced by faces; returns the old->new remap. */
std::vector<uint32_t> remove_loose_verts(Mesh &m);
/* Uniform Laplacian smoothing (blender/source/blender/modifiers/intern/MOD_smooth.cc). */
void smooth_laplacian(Mesh &m, float factor, int iterations, const std::vector<uint8_t> *vert_mask = nullptr);
/* Moves vertices along their normals by random amounts (for stress tests / noise). */
void randomize(Mesh &m, float amount, uint32_t seed);
std::vector<Vec3> vertex_normals(const Mesh &m);
/* Vertex adjacency in CSR form (offsets size n+1). */
void vertex_neighbors(const Mesh &m, std::vector<uint32_t> &offsets, std::vector<uint32_t> &neighbors);

/* --- edit-mode topology tools (blender/source/blender/bmesh/tools, editors/mesh) --- */
/* Vertices of the edge loop through edge (a, b): continues straight through
 * valence-4 vertices and along boundaries (Blender: Select Edge Loop, Alt+click). */
std::vector<uint32_t> edge_loop(const Mesh &m, uint32_t a, uint32_t b);
/* Loop Cut (Ctrl+R): splits the quad ring crossing edge (a, b) with `cuts`
 * parallel edge loops. factor slides a single cut along the edge from a.
 * Faces at the ends of the ring receive the new vertices (no T-junctions).
 * Returns the new vertices (Blender selects them). */
std::vector<uint32_t> loop_cut(Mesh &m, uint32_t a, uint32_t b, int cuts = 1, float factor = 0.5f);
/* Fill (F): one face from the selected vertices - a boundary loop/chain in
 * order, otherwise sorted around their best-fit plane. */
bool fill(Mesh &m, const std::vector<uint8_t> &vert_sel);
/* Merge > At Center (M): selected vertices weld into one at their centroid.
 * Renumbers vertices; returns how many were merged away. */
size_t merge_at_center(Mesh &m, const std::vector<uint8_t> &vert_sel);
/* Recalculate Outside (Shift+N): consistent winding, then outward. */
void recalc_normals_outside(Mesh &m);

/* --- edge and face tools (mesh_tools.cpp). Selections are updated to the result. --- */
/* Bevel the selected edges (both ends selected, two faces each). width is
 * measured along the neighbouring edges; segments > 1 rounds the profile.
 * Blender: Ctrl+B (bmesh_bevel.cc). */
bool bevel_edges(Mesh &m, std::vector<uint8_t> &vert_sel, std::vector<uint8_t> &face_sel, float width, int segments,
                 std::string *error = nullptr, bool clamp_overlap = true);  // Blender: Clamp Overlap
/* Joins two selected face regions (removed) or two open edge loops with a
 * tube. Loops may differ in vertex count and face any direction: the second
 * loop is rotated onto the first's plane to match vertices, and segments > 1
 * curve the tube along the faces' normals (smooth scales the bend; path 0
 * auto, 1 outside as a handle, 2 inside as a tunnel). Regions
 * touching face to face are fused instead. Blender: Bridge Edge Loops. */
bool bridge(Mesh &m, std::vector<uint8_t> &vert_sel, std::vector<uint8_t> &face_sel, int segments, int twist, float smooth, int path = 0,
            std::string *error = nullptr);
/* Selected regions lying on another face (e.g. extruded onto it) merge into
 * it: the face gets a hole the shape of the region, or the region shrinks to
 * a ring around a smaller face, or both are welded when they match. */
bool fuse_contacts(Mesh &m, std::vector<uint8_t> &face_sel);
/* Cuts a hole through the object along the selected region's normal: the
 * outline is projected onto the face it exits through (at any angle), cut
 * out of it, and joined to the opening by a tube. Several exit faces use the
 * Boolean solver when available. */
bool push_through(Mesh &m, std::vector<uint8_t> &face_sel, int segments, std::string *error = nullptr);
/* SketchUp's Push/Pull on one face region, by `distance` along its normal
 * (negative pushes in):
 *  - sides whose neighbouring face lies in the moved wall's plane stretch
 *    that face instead of adding a wall, so a box top just moves and a
 *    face at the edge of a surface makes a clean notch or step;
 *  - pushed to the far side of the object: a hole, cut into the exit face
 *    at whatever angle it has (push_through);
 *  - pulled onto a face in front: every vertex lands on that face (tilted
 *    or not) and the two are joined (fuse_contacts).
 * merge_coplanar = false always adds walls (SketchUp's Ctrl: keep the face). */
enum class PushPullResult { Moved, Extruded, Hole, Joined };
struct PushPullLimits {
  float through = -1.0f;  // push distance that reaches the far side (-1: none)
  float contact = -1.0f;  // pull distance that reaches a face in front (-1: none)
  uint32_t contact_face = UINT32_MAX;
  float behind = -1.0f;   // nearest surface behind the region (pushes stop there unless they make a hole)
  float ahead = -1.0f;    // nearest surface in front (pulls stop there unless they join it)
  float behind_sweep = -1.0f;  // the same, from geometry inside the swept outline only
};
PushPullLimits push_pull_limits(const Mesh &m, const std::vector<uint8_t> &face_sel);
bool push_pull(Mesh &m, std::vector<uint8_t> &face_sel, float distance, bool merge_coplanar = true,
               PushPullResult *result = nullptr, std::string *error = nullptr);
size_t subdivide_edges(Mesh &m, std::vector<uint8_t> &vert_sel, int cuts);  // Blender: Subdivide (edges only)
size_t dissolve_edges(Mesh &m, std::vector<uint8_t> &vert_sel);             // Blender: Dissolve Edges
size_t connect_vertices(Mesh &m, std::vector<uint8_t> &vert_sel);           // Blender: J (Connect Vertex Path)
size_t collapse_edges(Mesh &m, std::vector<uint8_t> &vert_sel);             // Blender: Collapse
/* More Blender operators (mesh_tools2.cpp). Selections follow the result. */
size_t poke_faces(Mesh &m, std::vector<uint8_t> &face_sel, float offset = 0.0f);       // Face > Poke Faces
size_t triangulate_faces(Mesh &m, std::vector<uint8_t> &face_sel);                     // Ctrl+T on a selection
size_t tris_to_quads(Mesh &m, std::vector<uint8_t> &face_sel, float max_angle_deg = 40.0f);  // Alt+J
size_t flip_faces(Mesh &m, const std::vector<uint8_t> &face_sel);                      // Normals > Flip (selected)
size_t duplicate_faces(Mesh &m, std::vector<uint8_t> &face_sel);                       // Shift+D: the copy is selected
size_t split_faces(Mesh &m, std::vector<uint8_t> &face_sel);                           // Y: detach from the rest
size_t dissolve_faces(Mesh &m, std::vector<uint8_t> &face_sel);                        // each region -> one n-gon
size_t dissolve_vertices(Mesh &m, std::vector<uint8_t> &vert_sel);                     // Dissolve Vertices
size_t extrude_individual(Mesh &m, std::vector<uint8_t> &face_sel, float distance);   // Extrude Individual Faces
void shrink_fatten(Mesh &m, const std::vector<uint8_t> &vert_sel, float distance);    // Alt+S
void to_sphere(Mesh &m, const std::vector<uint8_t> &vert_sel, float factor);          // Shift+Alt+S
void randomize(Mesh &m, const std::vector<uint8_t> &vert_sel, float amount, uint32_t seed = 1);
size_t edge_split(Mesh &m, std::vector<uint8_t> &vert_sel);                            // Edge Split (selected edges)
void select_linked(const Mesh &m, std::vector<uint8_t> &vert_sel);                     // Ctrl+L
void grow_selection(const Mesh &m, std::vector<uint8_t> &vert_sel, bool grow);         // Ctrl+Numpad +/-
void select_non_manifold(const Mesh &m, std::vector<uint8_t> &vert_sel);
std::vector<std::pair<uint32_t, uint32_t>> edge_ring_edges(const Mesh &m, uint32_t a, uint32_t b);  // Select Edge Ring
/* Blender's Set Origin reference points, in mesh space. Volume falls back to
 * Surface for open meshes (no enclosed volume), Surface to Median for meshes
 * without area. */
enum class OriginPoint { BoundsCenter, Median, SurfaceCenter, VolumeCenter, BoundsBottom };
Vec3 origin_point(const Mesh &m, OriginPoint mode);
void translate(Mesh &m, Vec3 offset);
/* Boolean (Blender: Boolean modifier, Manifold solver; needs Blender's
 * libraries). b is placed into a's object space by b_to_a. Both must be
 * closed manifolds; on failure a is unchanged and error explains why. */
/* Decimate (Blender: Decimate modifier, Collapse) via meshoptimizer: keeps
 * ratio of the triangles. Returns false (mesh unchanged) without it. */
bool decimate_available();
bool decimate(Mesh &m, float ratio);
enum class BooleanOp { Difference = 0, Union = 1, Intersect = 2 };
bool boolean_available();
bool boolean_op(Mesh &a, const Mesh &b, const Mat4 &b_to_a, BooleanOp op, std::string *error = nullptr);

/* Generative modifiers (blender/source/blender/modifiers/intern/MOD_mirror.cc,
 * MOD_array.cc, MOD_solidify_extrude.cc), in object space. */
void mirror(Mesh &m, bool x, bool y, bool z, float merge_dist);
void make_array(Mesh &m, int count, Vec3 relative_offset, Vec3 constant_offset, float merge_dist);
void solidify(Mesh &m, float thickness, float offset, bool even, bool rim);
}  // namespace meshops

}  // namespace bl

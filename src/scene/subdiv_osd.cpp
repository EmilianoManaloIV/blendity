// SPDX-License-Identifier: GPL-2.0-or-later
// Subdivision through OpenSubdiv's Far API, configured like Blender's
// Subdivision Surface modifier (blender/source/blender/blenkernel/intern/
// subdiv.cc, intern/opensubdiv): Catmull-Clark ("Catmark") or bilinear,
// boundary smooth "All" (VTX_BOUNDARY_EDGE_ONLY) and UV smooth "Keep
// Boundaries" (FVAR_LINEAR_BOUNDARIES).
#ifndef _USE_MATH_DEFINES
#  define _USE_MATH_DEFINES  // OpenSubdiv's headers use M_PI (Blender defines this globally)
#endif
#include <cmath>

#include "mesh.h"

#include <memory>
#include <unordered_map>

#ifdef BL_WITH_OPENSUBDIV
#  include <opensubdiv/far/primvarRefiner.h>
#  include <opensubdiv/far/topologyDescriptor.h>
#endif

namespace bl::meshops {

/* Off by default: the stress test measured OpenSubdiv's single-threaded
 * uniform refinement at ~3x the time of Blendity's parallel Catmull-Clark for
 * identical output. It stays available for parity checks (and creases later). */
static bool g_use_opensubdiv = false;
void set_subdiv_opensubdiv(bool on) { g_use_opensubdiv = on; }
bool subdiv_opensubdiv_available() {
#ifdef BL_WITH_OPENSUBDIV
  return true;
#else
  return false;
#endif
}

#ifdef BL_WITH_OPENSUBDIV
namespace {
using namespace OpenSubdiv;

/* Primvar types in the form PrimvarRefiner expects. */
struct OsdVertex {
  float p[3];
  void Clear() { p[0] = p[1] = p[2] = 0.0f; }
  void AddWithWeight(const OsdVertex &s, float w) {
    p[0] += w * s.p[0];
    p[1] += w * s.p[1];
    p[2] += w * s.p[2];
  }
};
struct OsdUV {
  float uv[2];
  void Clear() { uv[0] = uv[1] = 0.0f; }
  void AddWithWeight(const OsdUV &s, float w) {
    uv[0] += w * s.uv[0];
    uv[1] += w * s.uv[1];
  }
};
struct OsdMaterial {
  int index;
  void Clear() { index = 0; }
  void AddWithWeight(const OsdMaterial &s, float) { index = s.index; }  // child faces inherit
};

bool refine(const Mesh &in, int levels, bool smooth, Mesh &out) {
  using Descriptor = Far::TopologyDescriptor;
  const int nv = (int)in.vert_count(), nf = (int)in.face_count();
  std::vector<int> nverts(nf), indices(in.corner_verts.begin(), in.corner_verts.end());
  for (int f = 0; f < nf; f++) nverts[f] = (int)in.face_size(f);
  Descriptor desc;
  desc.numVertices = nv;
  desc.numFaces = nf;
  desc.numVertsPerFace = nverts.data();
  desc.vertIndicesPerFace = indices.data();
  /* Face-varying UVs: OpenSubdiv wants unique values plus per-corner indices.
   * Corners of one vertex with the same UV share a value (welded). */
  const bool has_uv = in.has_uvs();
  std::vector<int> uv_index;
  std::vector<OsdUV> uv_values;
  Descriptor::FVarChannel channel;
  if (has_uv) {
    uv_index.resize(in.corner_count());
    std::unordered_map<uint64_t, std::vector<int>> by_vertex;
    by_vertex.reserve(in.vert_count());
    for (size_t c = 0; c < in.corner_count(); c++) {
      Vec2 t = in.uvs[c];
      auto &cands = by_vertex[in.corner_verts[c]];
      int found = -1;
      for (int k : cands)
        if (uv_values[k].uv[0] == t.x && uv_values[k].uv[1] == t.y) { found = k; break; }
      if (found < 0) {
        found = (int)uv_values.size();
        uv_values.push_back({{t.x, t.y}});
        cands.push_back(found);
      }
      uv_index[c] = found;
    }
    channel.numValues = (int)uv_values.size();
    channel.valueIndices = uv_index.data();
    desc.numFVarChannels = 1;
    desc.fvarChannels = &channel;
  }
  Sdc::Options opts;
  opts.SetVtxBoundaryInterpolation(Sdc::Options::VTX_BOUNDARY_EDGE_ONLY);
  opts.SetFVarLinearInterpolation(Sdc::Options::FVAR_LINEAR_BOUNDARIES);
  using Factory = Far::TopologyRefinerFactory<Descriptor>;
  std::unique_ptr<Far::TopologyRefiner> refiner(
      Factory::Create(desc, Factory::Options(smooth ? Sdc::SCHEME_CATMARK : Sdc::SCHEME_BILINEAR, opts)));
  if (!refiner) return false;
  Far::TopologyRefiner::UniformOptions uniform(levels);
  uniform.fullTopologyInLastLevel = true;
  refiner->RefineUniform(uniform);
  Far::PrimvarRefiner primvar(*refiner);

  /* Positions through every level. */
  std::vector<OsdVertex> vbuf(refiner->GetNumVerticesTotal());
  for (int v = 0; v < nv; v++) vbuf[v] = {{in.positions[v].x, in.positions[v].y, in.positions[v].z}};
  OsdVertex *vsrc = vbuf.data();
  for (int l = 1; l <= levels; l++) {
    OsdVertex *vdst = vsrc + refiner->GetLevel(l - 1).GetNumVertices();
    primvar.Interpolate(l, vsrc, vdst);
    vsrc = vdst;
  }
  /* UVs (face-varying) and material indices (face-uniform). */
  std::vector<OsdUV> ubuf;
  OsdUV *usrc = nullptr;
  if (has_uv) {
    ubuf.resize(refiner->GetNumFVarValuesTotal(0));
    std::copy(uv_values.begin(), uv_values.end(), ubuf.begin());
    usrc = ubuf.data();
    for (int l = 1; l <= levels; l++) {
      OsdUV *udst = usrc + refiner->GetLevel(l - 1).GetNumFVarValues(0);
      primvar.InterpolateFaceVarying(l, usrc, udst, 0);
      usrc = udst;
    }
  }
  const bool has_mat = !in.face_material.empty();
  std::vector<OsdMaterial> mbuf;
  OsdMaterial *msrc = nullptr;
  if (has_mat) {
    mbuf.resize(refiner->GetNumFacesTotal());
    for (int f = 0; f < nf; f++) mbuf[f].index = in.material_of(f);
    msrc = mbuf.data();
    for (int l = 1; l <= levels; l++) {
      OsdMaterial *mdst = msrc + refiner->GetLevel(l - 1).GetNumFaces();
      primvar.InterpolateFaceUniform(l, msrc, mdst);
      msrc = mdst;
    }
  }

  const Far::TopologyLevel &last = refiner->GetLevel(levels);
  out.clear();
  out.name = in.name;
  out.smooth = smooth ? true : in.smooth;
  out.smooth_angle = in.smooth_angle;
  out.positions.resize(last.GetNumVertices());
  for (int v = 0; v < last.GetNumVertices(); v++) out.positions[v] = {vsrc[v].p[0], vsrc[v].p[1], vsrc[v].p[2]};
  /* Write the arrays directly (one face at a time through add_face was a
   * measurable part of the cost at millions of faces). */
  const int nfaces = last.GetNumFaces();
  out.face_offsets.resize((size_t)nfaces + 1);
  out.face_offsets[0] = 0;
  for (int f = 0; f < nfaces; f++) out.face_offsets[f + 1] = out.face_offsets[f] + (uint32_t)last.GetFaceVertices(f).size();
  out.corner_verts.resize(out.face_offsets[nfaces]);
  if (has_uv) out.uvs.resize(out.corner_verts.size());
  if (has_mat) out.face_material.resize(nfaces);
  for (int f = 0; f < nfaces; f++) {
    Far::ConstIndexArray verts = last.GetFaceVertices(f);
    uint32_t b = out.face_offsets[f];
    for (int k = 0; k < verts.size(); k++) out.corner_verts[b + k] = (uint32_t)verts[k];
    if (has_uv) {
      Far::ConstIndexArray vals = last.GetFaceFVarValues(f, 0);
      for (int k = 0; k < vals.size(); k++) out.uvs[b + k] = {usrc[vals[k]].uv[0], usrc[vals[k]].uv[1]};
    }
    if (has_mat) out.face_material[f] = msrc[f].index;
  }
  out.touch();
  return true;
}
}  // namespace
#endif

Mesh subdivide(const Mesh &in, int levels, bool smooth) {
  if (levels <= 0) return in;
#ifdef BL_WITH_OPENSUBDIV
  /* UV seams are edge data OpenSubdiv doesn't carry; meshes that have them
   * use Blendity's subdivision, which splits seams along. */
  if (g_use_opensubdiv && in.seams.empty() && in.face_count() > 0) {
    Mesh out;
    if (refine(in, levels, smooth, out)) return out;
  }
#endif
  Mesh m = in;
  for (int i = 0; i < levels; i++) m = smooth ? subdivide_catmull_clark(m) : subdivide_simple(m);
  return m;
}

}  // namespace bl::meshops

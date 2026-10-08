// SPDX-License-Identifier: GPL-2.0-or-later
#include "export.h"

#include "../core/core.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace bl {

namespace {

/* Short round-trip float text (like the scene writer). */
std::string num(double v) {
  if (std::fabs(v) < 1e-12) return "0";
  std::string s = strprintf("%.7g", v);
  return s;
}

/* Per-corner normals in world space: the vertex normal on smooth faces, the
 * face normal on flat ones. */
std::vector<Vec3> corner_normals(const Mesh &m, const Mat4 &world) {
  const std::vector<Vec3> vn = meshops::vertex_normals(m);
  const Mat4 nm = world.inverse().transposed();
  std::vector<Vec3> out(m.corner_count());
  for (size_t f = 0; f < m.face_count(); f++) {
    const Vec3 fn = m.face_normal(f);
    for (uint32_t c = m.face_offsets[f]; c < m.face_offsets[f + 1]; c++) {
      const Vec3 n = m.smooth_of(f) ? vn[m.corner_verts[c]] : fn;
      out[c] = normalize(nm.dir(n));
    }
  }
  return out;
}

/* Our left-handed Y-up to the formats' right-handed Y-up. */
Vec3 rh(Vec3 v) { return {-v.x, v.y, v.z}; }


}  // namespace

void export_obj_mtl(const std::vector<ExportItem> &items, const std::string &mtl_file, std::string &obj, std::string &mtl) {
  obj = "# Exported by Blendity (left-handed Y-up mirrored to OBJ's right-handed Y-up)\n";
  obj += "mtllib " + mtl_file + "\n";
  mtl = "# Blendity materials\n";
  std::unordered_map<const Material *, std::string> written;
  std::unordered_map<std::string, int> used_names;
  auto mtl_name = [&](const MaterialPtr &mp) {
    if (!mp) return std::string("Default");
    auto it = written.find(mp.get());
    if (it != written.end()) return it->second;
    std::string name = mp->name.empty() ? "Material" : mp->name;
    for (char &ch : name)
      if (ch == ' ') ch = '_';
    if (used_names[name]++) name += "_" + std::to_string(used_names[name] - 1);
    written[mp.get()] = name;
    /* Phong terms the way Blender's OBJ exporter maps Principled BSDF. */
    const Material &m = *mp;
    const float ns = (1.0f - m.roughness) * (1.0f - m.roughness) * 1000.0f;
    mtl += "\nnewmtl " + name + "\n";
    mtl += "Ka 1 1 1\n";
    mtl += "Kd " + num(m.base_color.x) + " " + num(m.base_color.y) + " " + num(m.base_color.z) + "\n";
    mtl += "Ks " + num(m.specular) + " " + num(m.specular) + " " + num(m.specular) + "\n";
    const Vec3 ke = m.emission * m.emission_strength;
    mtl += "Ke " + num(ke.x) + " " + num(ke.y) + " " + num(ke.z) + "\n";
    mtl += "Ns " + num(ns) + "\n";
    mtl += "Ni " + num(m.ior) + "\n";
    mtl += "d " + num(m.alpha) + "\n";
    mtl += "illum " + std::string(m.alpha < 1.0f ? "4" : m.metallic > 0.5f ? "3" : "2") + "\n";
    mtl += "Pm " + num(m.metallic) + "\nPr " + num(m.roughness) + "\n";  // PBR extension (Blender reads these)
    auto map = [&](const char *key, const TextureRef &t) {
      if (!t.empty()) mtl += std::string(key) + " " + resolve_asset_path(t.path) + "\n";
    };
    map("map_Kd", m.base_map);
    map("map_Pm", m.metallic_map);
    map("map_Pr", m.roughness_map);
    map("map_Bump", m.normal_map);
    map("map_Ke", m.emission_map);
    return name;
  };
  bool wrote_default = false;
  size_t vbase = 1, tbase = 1, nbase = 1;
  for (const ExportItem &it : items) {
    if (!it.mesh) continue;
    const Mesh &m = *it.mesh;
    std::string oname = it.name.empty() ? m.name : it.name;
    for (char &ch : oname)
      if (ch == ' ') ch = '_';
    obj += "o " + oname + "\n";
    for (const Vec3 &p : m.positions) {
      const Vec3 w = rh(it.world.point(p));
      obj += "v " + num(w.x) + " " + num(w.y) + " " + num(w.z) + "\n";
    }
    const bool uv = m.has_uvs();
    if (uv)
      for (const Vec2 &t : m.uvs) obj += "vt " + num(t.x) + " " + num(t.y) + "\n";
    const std::vector<Vec3> cn = corner_normals(m, it.world);
    for (const Vec3 &n : cn) {
      const Vec3 r = rh(n);
      obj += "vn " + num(r.x) + " " + num(r.y) + " " + num(r.z) + "\n";
    }
    int cur_mat = -2, cur_smooth = -1;
    for (size_t f = 0; f < m.face_count(); f++) {
      const int slot = m.material_of(f);
      if (slot != cur_mat) {
        cur_mat = slot;
        const MaterialPtr mp = slot < (int)it.materials.size() ? it.materials[(size_t)slot] : nullptr;
        if (!mp && !wrote_default) {
          mtl += "\nnewmtl Default\nKd 0.8 0.8 0.8\nd 1\nillum 2\n";
          wrote_default = true;
        }
        obj += "usemtl " + mtl_name(mp) + "\n";
      }
      const int s = m.smooth_of(f) ? 1 : 0;
      if (s != cur_smooth) {
        cur_smooth = s;
        obj += s ? "s 1\n" : "s off\n";
      }
      obj += "f";
      /* Mirroring X flips handedness: reverse the corners to keep the faces facing out. */
      for (int k = (int)m.face_size(f) - 1; k >= 0; k--) {
        const uint32_t c = m.face_offsets[f] + (uint32_t)k;
        obj += " " + std::to_string(vbase + m.corner_verts[c]) + "/" + (uv ? std::to_string(tbase + c) : std::string()) + "/" +
               std::to_string(nbase + c);
      }
      obj += "\n";
    }
    vbase += m.vert_count();
    if (uv) tbase += m.uvs.size();
    nbase += cn.size();
  }
}

std::string export_fbx(const std::vector<ExportItem> &items) {
  std::string s;
  s.reserve(1 << 16);
  int64_t next_id = 1000000;
  auto array = [&](const char *name, const std::vector<std::string> &vals, int indent) {
    std::string pad(indent, '\t');
    s += pad + name + ": *" + std::to_string(vals.size()) + " {\n" + pad + "\ta: ";
    for (size_t i = 0; i < vals.size(); i++) {
      if (i) s += ",";
      if (i && i % 24 == 0) s += "\n" + pad + "\t";
      s += vals[i];
    }
    s += "\n" + pad + "}\n";
  };
  /* Materials, each written once. */
  struct Mat { int64_t id; MaterialPtr m; std::string name; };
  std::vector<Mat> mats;
  std::unordered_map<const Material *, int64_t> mat_id;
  std::vector<int64_t> geo_ids, model_ids;
  std::vector<std::vector<int64_t>> item_mats(items.size());
  for (size_t i = 0; i < items.size(); i++) {
    const ExportItem &it = items[i];
    if (!it.mesh) continue;
    int slots = std::max(1, std::max(it.mesh->material_count(), (int)it.materials.size()));
    for (int k = 0; k < slots; k++) {
      MaterialPtr mp = k < (int)it.materials.size() ? it.materials[(size_t)k] : nullptr;
      const Material *key = mp.get();
      auto f = mat_id.find(key);
      if (f == mat_id.end()) {
        const int64_t id = next_id++;
        f = mat_id.emplace(key, id).first;
        mats.push_back({id, mp, mp ? mp->name : std::string("Default")});
      }
      item_mats[i].push_back(f->second);
    }
  }
  size_t models = 0;
  for (const ExportItem &it : items)
    if (it.mesh) models++;
  s += "; FBX 7.4.0 project file\n; Exported by Blendity\n\n";
  s += "FBXHeaderExtension:  {\n\tFBXHeaderVersion: 1003\n\tFBXVersion: 7400\n\tCreator: \"Blendity\"\n}\n";
  s += "GlobalSettings:  {\n\tVersion: 1000\n\tProperties70:  {\n";
  s += "\t\tP: \"UpAxis\", \"int\", \"Integer\", \"\",1\n\t\tP: \"UpAxisSign\", \"int\", \"Integer\", \"\",1\n";
  s += "\t\tP: \"FrontAxis\", \"int\", \"Integer\", \"\",2\n\t\tP: \"FrontAxisSign\", \"int\", \"Integer\", \"\",1\n";
  s += "\t\tP: \"CoordAxis\", \"int\", \"Integer\", \"\",0\n\t\tP: \"CoordAxisSign\", \"int\", \"Integer\", \"\",1\n";
  s += "\t\tP: \"UnitScaleFactor\", \"double\", \"Number\", \"\",100\n";  // 1 unit = 1 m, as Unity and Blender expect
  s += "\t}\n}\n";
  s += "Definitions:  {\n\tVersion: 100\n\tCount: " + std::to_string(models * 2 + mats.size()) + "\n";
  s += "\tObjectType: \"Model\" {\n\t\tCount: " + std::to_string(models) + "\n\t}\n";
  s += "\tObjectType: \"Geometry\" {\n\t\tCount: " + std::to_string(models) + "\n\t}\n";
  s += "\tObjectType: \"Material\" {\n\t\tCount: " + std::to_string(mats.size()) + "\n\t}\n}\n";
  s += "Objects:  {\n";
  for (size_t i = 0; i < items.size(); i++) {
    const ExportItem &it = items[i];
    if (!it.mesh) continue;
    const Mesh &m = *it.mesh;
    const int64_t gid = next_id++, mid = next_id++;
    geo_ids.push_back(gid);
    model_ids.push_back(mid);
    std::string name = it.name.empty() ? m.name : it.name;
    for (char &ch : name)
      if (ch == '"') ch = '\'';
    s += "\tGeometry: " + std::to_string(gid) + ", \"Geometry::" + name + "\", \"Mesh\" {\n";
    std::vector<std::string> v, idx, nrm, uvs, uvidx, matidx;
    for (const Vec3 &p : m.positions) {
      const Vec3 w = rh(it.world.point(p));
      v.push_back(num(w.x));
      v.push_back(num(w.y));
      v.push_back(num(w.z));
    }
    const std::vector<Vec3> cn = corner_normals(m, it.world);
    const bool uv = m.has_uvs();
    for (size_t f = 0; f < m.face_count(); f++) {
      const uint32_t n = m.face_size(f);
      for (int k = (int)n - 1; k >= 0; k--) {  // reversed: mirroring X flips the winding
        const uint32_t c = m.face_offsets[f] + (uint32_t)k;
        const int64_t vi = m.corner_verts[c];
        idx.push_back(std::to_string(k == 0 ? -vi - 1 : vi));  // the last corner of a polygon is stored as -(i + 1)
        const Vec3 r = rh(cn[c]);
        nrm.push_back(num(r.x));
        nrm.push_back(num(r.y));
        nrm.push_back(num(r.z));
        if (uv) uvidx.push_back(std::to_string(c));
      }
      matidx.push_back(std::to_string(std::max(0, std::min((int)item_mats[i].size() - 1, m.material_of(f)))));
    }
    if (uv)
      for (const Vec2 &t : m.uvs) {
        uvs.push_back(num(t.x));
        uvs.push_back(num(t.y));
      }
    array("Vertices", v, 2);
    array("PolygonVertexIndex", idx, 2);
    s += "\t\tGeometryVersion: 124\n";
    s += "\t\tLayerElementNormal: 0 {\n\t\t\tVersion: 102\n\t\t\tName: \"\"\n\t\t\tMappingInformationType: \"ByPolygonVertex\"\n"
         "\t\t\tReferenceInformationType: \"Direct\"\n";
    array("Normals", nrm, 3);
    s += "\t\t}\n";
    if (uv) {
      s += "\t\tLayerElementUV: 0 {\n\t\t\tVersion: 101\n\t\t\tName: \"UVMap\"\n\t\t\tMappingInformationType: \"ByPolygonVertex\"\n"
           "\t\t\tReferenceInformationType: \"IndexToDirect\"\n";
      array("UV", uvs, 3);
      array("UVIndex", uvidx, 3);
      s += "\t\t}\n";
    }
    s += "\t\tLayerElementMaterial: 0 {\n\t\t\tVersion: 101\n\t\t\tName: \"\"\n\t\t\tMappingInformationType: \"ByPolygon\"\n"
         "\t\t\tReferenceInformationType: \"IndexToDirect\"\n";
    array("Materials", matidx, 3);
    s += "\t\t}\n";
    s += "\t\tLayer: 0 {\n\t\t\tVersion: 100\n";
    s += "\t\t\tLayerElement:  {\n\t\t\t\tType: \"LayerElementNormal\"\n\t\t\t\tTypedIndex: 0\n\t\t\t}\n";
    if (uv) s += "\t\t\tLayerElement:  {\n\t\t\t\tType: \"LayerElementUV\"\n\t\t\t\tTypedIndex: 0\n\t\t\t}\n";
    s += "\t\t\tLayerElement:  {\n\t\t\t\tType: \"LayerElementMaterial\"\n\t\t\t\tTypedIndex: 0\n\t\t\t}\n";
    s += "\t\t}\n\t}\n";
    s += "\tModel: " + std::to_string(mid) + ", \"Model::" + name + "\", \"Mesh\" {\n\t\tVersion: 232\n\t\tProperties70:  {\n";
    s += "\t\t\tP: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\",0,0,0\n";
    s += "\t\t\tP: \"Lcl Rotation\", \"Lcl Rotation\", \"\", \"A\",0,0,0\n";
    s += "\t\t\tP: \"Lcl Scaling\", \"Lcl Scaling\", \"\", \"A\",1,1,1\n";
    s += "\t\t}\n\t\tShading: T\n\t\tCulling: \"CullingOff\"\n\t}\n";
  }
  for (const Mat &mt : mats) {
    const Vec3 c = mt.m ? mt.m->base_color : Vec3(0.8f);
    const float a = mt.m ? mt.m->alpha : 1.0f;
    const Vec3 e = mt.m ? mt.m->emission * mt.m->emission_strength : Vec3(0.0f);
    std::string name = mt.name;
    for (char &ch : name)
      if (ch == '"') ch = '\'';
    s += "\tMaterial: " + std::to_string(mt.id) + ", \"Material::" + name + "\", \"\" {\n\t\tVersion: 102\n\t\tShadingModel: \"phong\"\n"
         "\t\tMultiLayer: 0\n\t\tProperties70:  {\n";
    s += "\t\t\tP: \"DiffuseColor\", \"Color\", \"\", \"A\"," + num(c.x) + "," + num(c.y) + "," + num(c.z) + "\n";
    s += "\t\t\tP: \"EmissiveColor\", \"Color\", \"\", \"A\"," + num(e.x) + "," + num(e.y) + "," + num(e.z) + "\n";
    s += "\t\t\tP: \"Opacity\", \"double\", \"Number\", \"\"," + num(a) + "\n";
    s += "\t\t\tP: \"TransparencyFactor\", \"double\", \"Number\", \"\"," + num(1.0f - a) + "\n";
    s += "\t\t}\n\t}\n";
  }
  s += "}\nConnections:  {\n";
  size_t k = 0;
  for (size_t i = 0; i < items.size(); i++) {
    if (!items[i].mesh) continue;
    s += "\tC: \"OO\"," + std::to_string(model_ids[k]) + ",0\n";
    s += "\tC: \"OO\"," + std::to_string(geo_ids[k]) + "," + std::to_string(model_ids[k]) + "\n";
    for (int64_t mid : item_mats[i]) s += "\tC: \"OO\"," + std::to_string(mid) + "," + std::to_string(model_ids[k]) + "\n";
    k++;
  }
  s += "}\n";
  return s;
}

}  // namespace bl

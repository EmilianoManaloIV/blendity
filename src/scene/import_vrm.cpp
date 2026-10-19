// SPDX-License-Identifier: GPL-2.0-or-later
// VRM characters (task 0138, ADR 0047): a glTF with the VRM extension (0.x: "VRM"; 1.0: "VRMC_vrm" and friends), as
// VRoid Studio and UniVRM write them. The glTF reader brings the mesh, skin and morph targets; this adds what the
// extension says: the humanoid's bones named for the rig tools, its expressions as shape keys, its spring bones, its
// MToon materials as Toon with an outline, and its facing (a 0.x model faces -Z in glTF, a 1.0 model +Z).
#include "import.h"

#include "../core/core.h"
#include "../core/json.h"
#include "../image/image.h"
#include "spring_bones.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace bl {

namespace {

const Json &vrm0(const Json &doc) { return doc["extensions"]["VRM"]; }
const Json &vrm1(const Json &doc) { return doc["extensions"]["VRMC_vrm"]; }

int node_of(const std::vector<int> &node_index, const Json &j) {
  const int n = j.integer();
  return n >= 0 && (size_t)n < node_index.size() ? node_index[(size_t)n] : -1;
}

/* A humanoid bone's name in the words Blendity's rig tools read (Mixamo's): "leftUpperLeg" -> "LeftUpLeg". */
std::string humanoid_name(const std::string &vrm, bool vrm1) {
  static const std::map<std::string, std::string> body = {
      {"hips", "Hips"}, {"spine", "Spine"}, {"chest", "Chest"}, {"upperChest", "UpperChest"}, {"neck", "Neck"}, {"head", "Head"}, {"jaw", "Jaw"},
      {"leftEye", "LeftEye"}, {"rightEye", "RightEye"}};
  static const std::map<std::string, std::string> sided = {
      {"Shoulder", "Shoulder"}, {"UpperArm", "Arm"}, {"LowerArm", "ForeArm"}, {"Hand", "Hand"}, {"UpperLeg", "UpLeg"}, {"LowerLeg", "Leg"},
      {"Foot", "Foot"}, {"Toes", "ToeBase"}};
  static const char *const fingers[5][2] = {{"Thumb", "Thumb"}, {"Index", "Index"}, {"Middle", "Middle"}, {"Ring", "Ring"}, {"Little", "Pinky"}};
  if (const auto it = body.find(vrm); it != body.end()) return it->second;
  for (const char *side : {"left", "right"}) {
    const size_t l = std::strlen(side);
    if (vrm.compare(0, l, side) != 0) continue;
    const std::string rest = vrm.substr(l), word = side[0] == 'l' ? "Left" : "Right";
    if (const auto it = sided.find(rest); it != sided.end()) return word + it->second;
    for (const auto &f : fingers) {
      if (rest.compare(0, std::strlen(f[0]), f[0]) != 0) continue;
      const std::string joint = rest.substr(std::strlen(f[0]));
      /* 1.0 names the thumb's joints Metacarpal, Proximal, Distal; 0.x Proximal, Intermediate, Distal. */
      const int n = joint == "Metacarpal" ? 1 : joint == "Proximal" ? (vrm1 && f[0][0] == 'T' ? 2 : 1) : joint == "Intermediate" ? 2 : joint == "Distal" ? 3 : 0;
      if (n) return word + "Hand" + f[1] + std::to_string(n);
    }
  }
  return {};
}

/* The nodes whose mesh is glTF mesh `mi`. */
std::vector<int> nodes_with_mesh(const std::vector<MeshPtr> &meshes, int mi, const ImportResult &out) {
  std::vector<int> r;
  if (mi < 0 || (size_t)mi >= meshes.size()) return r;
  for (size_t i = 0; i < out.nodes.size(); i++)
    if (out.nodes[i].mesh == meshes[(size_t)mi]) r.push_back((int)i);
  return r;
}

/* An expression as a shape key on a mesh: the weighted sum of the morph targets it binds there (weights clamped: a
 * damaged file's 1e9). A morph target of the same name already is the expression when the group is just that target
 * whole; otherwise the expression's key is named "<name>.expr" beside it. */
void bind_expression(Mesh &m, std::string name, const std::vector<std::pair<int, float>> &binds) {
  if (name.empty()) return;
  const int same = m.find_shape(name);
  if (same >= 0 && binds.size() == 1 && binds[0].first == same && std::fabs(binds[0].second - 1.0f) < 1e-3f) return;
  if (same >= 0) name += ".expr";
  if (m.find_shape(name) >= 0) return;
  ShapeKey k{name, std::vector<Vec3>(m.vert_count(), Vec3(0.0f))};
  bool any = false;
  for (const auto &[index, w_in] : binds) {
    const float w = std::max(-1.0f, std::min(1.0f, w_in));
    if (index < 0 || (size_t)index >= m.shape_keys.size() || m.shape_keys[(size_t)index].delta.size() != m.vert_count() || w == 0.0f) continue;
    const std::vector<Vec3> &d = m.shape_keys[(size_t)index].delta;
    for (size_t v = 0; v < k.delta.size(); v++) k.delta[v] = k.delta[v] + d[v] * w;
    any = true;
  }
  if (any) m.shape_keys.push_back(std::move(k));
}

void expressions0(const Json &vrm, const std::vector<MeshPtr> &meshes, ImportResult &out, int &count) {
  for (const Json &g : vrm["blendShapeMaster"]["blendShapeGroups"].arr) {
    std::string name = g["presetName"].str;
    if (name.empty() || name == "unknown") name = g["name"].str;
    std::map<int, std::vector<std::pair<int, float>>> per_node;  // (a group can bind several meshes)
    for (const Json &b : g["binds"].arr)
      for (int n : nodes_with_mesh(meshes, b["mesh"].integer(), out)) per_node[n].push_back({b["index"].integer(), (float)b["weight"].number() / 100.0f});
    for (auto &[n, binds] : per_node) bind_expression(*out.nodes[(size_t)n].mesh, name, binds);
    count += !per_node.empty();
  }
}

void expressions1(const Json &vrm, const std::vector<int> &node_index, ImportResult &out, int &count) {
  for (const char *kind : {"preset", "custom"})
    for (const auto &[name, e] : vrm["expressions"][kind].obj) {
      std::map<int, std::vector<std::pair<int, float>>> per_node;
      for (const Json &b : e["morphTargetBinds"].arr) {
        const int n = node_of(node_index, b["node"]);
        if (n >= 0 && out.nodes[(size_t)n].mesh) per_node[n].push_back({b["index"].integer(), (float)b["weight"].number(1.0)});
      }
      for (auto &[n, binds] : per_node) bind_expression(*out.nodes[(size_t)n].mesh, name, binds);
      count += !per_node.empty();
    }
}

/* A chain's settings from VRM's: UniVRM's stiffness slider runs 0..4 (a pull back per step), Blendity's 0..1, so a
 * quarter; drag 0..1 is the damping; gravityPower is a tail offset per step in UniVRM, near "times g" for chains kept
 * at their lengths, taken as such; the hit radius in metres. The node and its run below are made bones (VRM asks only
 * for nodes: an accessory's chain may not be a skin joint). */
ImportedSpring spring_from(int node, const std::string &name, float stiffness, float drag, float gravity, float radius, ImportResult &out) {
  ImportedSpring s;
  s.node = node;
  const int label = spring::secondary_label(name);
  s.label = label >= 0 ? label : 0;
  s.stiffness = std::max(0.0f, std::min(1.0f, stiffness / 4.0f));
  s.damping = std::max(0.0f, std::min(1.0f, drag));
  s.gravity = std::max(0.0f, std::min(4.0f, gravity));
  s.radius = std::max(0.005f, std::min(0.2f, radius));
  std::vector<int> stack{node};
  while (!stack.empty()) {
    const int n = stack.back();
    stack.pop_back();
    out.nodes[(size_t)n].is_bone = true;
    for (size_t i = 0; i < out.nodes.size(); i++)
      if (out.nodes[i].parent == n && !out.nodes[i].mesh) stack.push_back((int)i);
  }
  return s;
}

void springs0(const Json &vrm, const std::vector<int> &node_index, ImportResult &out) {
  for (const Json &g : vrm["secondaryAnimation"]["boneGroups"].arr)
    for (const Json &b : g["bones"].arr) {
      const int n = node_of(node_index, b);
      if (n < 0) continue;
      const std::string name = g["comment"].str.empty() ? out.nodes[(size_t)n].name : g["comment"].str;
      out.springs.push_back(spring_from(n, name, (float)g["stiffiness"].number(1.0), (float)g["dragForce"].number(0.4), (float)g["gravityPower"].number(0.0),
                                        (float)g["hitRadius"].number(0.02), out));
    }
}

/* 1.0: a spring's first joint's settings for the whole run (the joints below are followed by the hierarchy). */
void springs1(const Json &doc, const std::vector<int> &node_index, ImportResult &out) {
  for (const Json &s : doc["extensions"]["VRMC_springBone"]["springs"].arr) {
    const Json &first = s["joints"][0];
    const int n = node_of(node_index, first["node"]);
    if (n < 0) continue;
    const std::string name = s["name"].str.empty() ? out.nodes[(size_t)n].name : s["name"].str;
    out.springs.push_back(spring_from(n, name, (float)first["stiffness"].number(1.0), (float)first["dragForce"].number(0.5), (float)first["gravityPower"].number(0.0),
                                      (float)first["hitRadius"].number(0.02), out));
  }
}

Vec3 rgb(const Json &j, Vec3 def) {
  return j.size() >= 3 ? Vec3((float)j[0].number(), (float)j[1].number(), (float)j[2].number()) : def;
}
/* 0.x writes Unity's material colours as they are, in sRGB; glTF's factors (and 1.0's) are linear. */
Vec3 srgb(Vec3 c) { return Vec3(srgb_to_linear(std::max(0.0f, std::min(1.0f, c.x))), srgb_to_linear(std::max(0.0f, std::min(1.0f, c.y))), srgb_to_linear(std::max(0.0f, std::min(1.0f, c.z)))); }

/* MToon's shade colour is the colour of the shadow side; Blendity's toon shadow is a ratio of the lit colour. MToon's
 * edge is a step of half width (1 - toony) centred where N.L = -shift; Blendity's is centred at the threshold with
 * `softness` each side. */
void toon_from(Material &m, Vec3 shade, float toony, float shift) {
  m.shading_model = 1;
  Vec3 ratio;
  for (int k = 0; k < 3; k++) ratio[k] = m.base_color[k] > 1e-3f ? std::max(0.0f, std::min(1.0f, shade[k] / m.base_color[k])) : 1.0f;
  m.toon_shadow = ratio;
  m.toon_softness = std::max(0.005f, std::min(0.5f, 1.0f - toony));
  m.toon_threshold = std::max(0.0f, std::min(0.9f, -shift));
}

struct OutlineSpec {
  float width = 0.0f;  // metres (0: none)
  Vec3 color;
};

/* 0.x: a materialProperties entry per glTF material, with MToon's shader and its named properties. */
void materials0(const Json &vrm, const std::vector<MaterialPtr> &mats, std::vector<OutlineSpec> &outlines, int &toon) {
  const Json &props = vrm["materialProperties"];
  for (size_t i = 0; i < mats.size() && i < props.size(); i++) {
    const Json &p = props[i];
    Material &m = *mats[i];
    const Json &f = p["floatProperties"], &v = p["vectorProperties"];
    const std::string shader = p["shader"].str;
    if (shader.find("Unlit") != std::string::npos) m.unlit = true;
    if (shader != "VRM/MToon") continue;
    toon++;
    toon_from(m, srgb(rgb(v["_ShadeColor"], Vec3(0.97f, 0.81f, 0.86f))), (float)f["_ShadeToony"].number(0.9), (float)f["_ShadeShift"].number(0.0));  // (the specs' defaults)
    const int blend = (int)f["_BlendMode"].number(0);
    if (blend == 1) m.surface = (int)MaterialSurface::Cutout, m.alpha_clip = (float)f["_Cutoff"].number(0.5);
    else if (blend >= 2) m.surface = (int)MaterialSurface::Transparent;
    if ((int)f["_CullMode"].number(2) == 0) m.double_sided = true;
    const int mode = (int)f["_OutlineWidthMode"].number(0);  // 0 none, 1 world, 2 screen
    if (mode == 0) continue;
    outlines[i].width = mode == 1 ? (float)f["_OutlineWidth"].number(0.0) * 0.01f : 0.003f;  // (world: units of a centimetre)
    outlines[i].color = srgb(rgb(v["_OutlineColor"], Vec3(0.0f)));
  }
}

/* 1.0: each material's VRMC_materials_mtoon extension. */
void materials1(const Json &doc, const std::vector<MaterialPtr> &mats, std::vector<OutlineSpec> &outlines, int &toon) {
  const Json &jm = doc["materials"];
  for (size_t i = 0; i < mats.size() && i < jm.size(); i++) {
    Material &m = *mats[i];
    const Json &ext = jm[i]["extensions"];
    if (ext["KHR_materials_unlit"].type == Json::Obj) m.unlit = true;
    const Json &t = ext["VRMC_materials_mtoon"];
    if (t.type != Json::Obj) continue;
    toon++;
    toon_from(m, rgb(t["shadeColorFactor"], Vec3(0.0f)), (float)t["shadingToonyFactor"].number(0.9), (float)t["shadingShiftFactor"].number(0.0));
    const std::string mode = t["outlineWidthMode"].str;
    if (mode.empty() || mode == "none") continue;
    outlines[i].width = mode == "worldCoordinates" ? (float)t["outlineWidthFactor"].number(0.0) : 0.003f;
    outlines[i].color = rgb(t["outlineColorFactor"], Vec3(0.0f));
  }
}

/* Each mesh's outline: the widest its materials ask for (Blendity's outline is per object). */
void outlines_onto_nodes(const std::vector<MaterialPtr> &mats, const std::vector<OutlineSpec> &outlines, ImportResult &out) {
  for (size_t n = 0; n < out.nodes.size(); n++) {
    const ImportedNode &node = out.nodes[n];
    if (!node.mesh) continue;
    OutlineSpec best;
    for (const MaterialPtr &m : node.materials) {
      const auto it = std::find(mats.begin(), mats.end(), m);
      if (it == mats.end()) continue;
      const OutlineSpec &o = outlines[(size_t)(it - mats.begin())];
      if (o.width > best.width) best = o;
    }
    if (best.width > 0.0f) out.outlines.push_back({(int)n, std::min(0.05f, best.width), best.color});
  }
}

/* A 0.x model faces -Z in glTF (UniVRM's export): turned half a turn about Y to face +Z like every character. */
void face_forward(ImportResult &out) {
  const Quat half = Quat::axis_angle({0, 1, 0}, kPi);
  const Mat4 turn = Mat4::rotate(half);
  for (ImportedNode &n : out.nodes) {
    if (n.parent < 0) n.position = half.rotate(n.position), n.rotation = normalize(half * n.rotation);
    if (n.has_bind) n.bind_world = turn * n.bind_world;
  }
}

void meta_note(const Json &meta, bool vrm1, ImportResult &out) {
  const std::string title = vrm1 ? meta["name"].str : meta["title"].str, author = vrm1 ? meta["authors"][0].str : meta["author"].str;
  const std::string license = vrm1 ? meta["licenseUrl"].str : meta["licenseName"].str;
  if (title.empty() && author.empty()) return;
  out.notes.push_back("VRM: \"" + title + "\"" + (author.empty() ? "" : " by " + author) + (license.empty() ? "" : ", license " + license) +
                      " (the model's own terms apply to its use)");
}

}  // namespace

bool vrm_present(const Json &doc) { return vrm0(doc).type == Json::Obj || vrm1(doc).type == Json::Obj; }

void vrm_name_bones(const Json &doc, const std::vector<int> &node_index, ImportResult &out) {
  const bool v1 = vrm1(doc).type == Json::Obj;
  std::map<int, std::string> names;
  if (v1)
    for (const auto &[bone, j] : vrm1(doc)["humanoid"]["humanBones"].obj) names[node_of(node_index, j["node"])] = humanoid_name(bone, true);
  else
    for (const Json &b : vrm0(doc)["humanoid"]["humanBones"].arr) names[node_of(node_index, b["node"])] = humanoid_name(b["bone"].str, false);
  /* A name is taken only by a node that isn't itself renamed (a node called "Head" that is the humanoid's neck). */
  for (const auto &[n, name] : names) {
    if (n < 0 || name.empty()) continue;
    bool taken = false;
    for (size_t i = 0; i < out.nodes.size(); i++) taken = taken || ((int)i != n && out.nodes[i].name == name && !names.count((int)i));
    if (!taken) out.nodes[(size_t)n].name = name;
  }
}

void vrm_complete(const Json &doc, const std::vector<int> &node_index, const std::vector<MeshPtr> &meshes, const std::vector<MaterialPtr> &mats, ImportResult &out) {
  const bool v1 = vrm1(doc).type == Json::Obj;
  int expressions = 0, toon = 0;
  if (v1) expressions1(vrm1(doc), node_index, out, expressions);
  else expressions0(vrm0(doc), meshes, out, expressions);
  if (v1) springs1(doc, node_index, out);
  else springs0(vrm0(doc), node_index, out);
  std::vector<OutlineSpec> outlines(mats.size());
  if (v1) materials1(doc, mats, outlines, toon);
  else materials0(vrm0(doc), mats, outlines, toon);
  outlines_onto_nodes(mats, outlines, out);
  if (!v1) face_forward(out);
  meta_note(v1 ? vrm1(doc)["meta"] : vrm0(doc)["meta"], v1, out);
  out.notes.push_back(strprintf("VRM %s: %d expression(s) as shape keys, %zu spring chain(s) (a 1.0 chain follows the bones below its first joint), %d MToon material(s)%s",
                                v1 ? "1.0" : "0.x", expressions, out.springs.size(), toon, toon ? " (shade textures and rim light not carried over)" : ""));
}

}  // namespace bl

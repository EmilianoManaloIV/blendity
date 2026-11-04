// SPDX-License-Identifier: GPL-2.0-or-later
#include "unity_anim.h"
#include "scene.h"
#include "unity_yaml.h"
#include "../core/core.h"
#include "../core/math.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <sstream>

namespace bl {

namespace {

const char *kBlendShapePrefix = "MeshRenderer.BlendShape/";
const char *kBlendShapeAttr = "blendShape.";
constexpr int kClassGameObject = 1, kClassTransform = 4, kClassSkinnedMeshRenderer = 137, kClassAnimationClip = 74, kClassAnimator = 95;
/* Unity's serialized tangentMode: the left mode in bits 1-3, the right in bits 5-7, bit 0 "broken"; the modes Free 0,
 * Auto 1, Linear 2, Constant 3, ClampedAuto 4. */
enum UnityTangent { kFree = 0, kAuto = 1, kLinear = 2, kConstant = 3, kClampedAuto = 4 };
constexpr double kInf = std::numeric_limits<double>::infinity();

int pack_modes(int left, int right, bool broken) { return (left << 1) | (right << 5) | (broken ? 1 : 0); }
int left_mode(int packed) { return (packed >> 1) & 7; }
int right_mode(int packed) { return (packed >> 5) & 7; }

std::string num(double v) {
  if (std::isinf(v)) return v > 0 ? "Infinity" : "-Infinity";
  if (std::isnan(v)) return "0";
  char b[64];
  std::snprintf(b, sizeof b, "%.9g", v);
  return b;
}

/* A YAML scalar as Unity quotes them: single quotes when the text would read as something else. */
std::string yaml_scalar(const std::string &s) {
  const bool risky = s.empty() || s.front() == ' ' || s.back() == ' ' || s.find_first_of(":#[]{}&*!|>%@'\"`,?") != std::string::npos || s == "~" || s == "null";
  if (!risky) return s;
  std::string out = "'";
  for (char c : s) out += c == '\'' ? "''" : std::string(1, c);
  return out + "'";
}

float frame_of(double time, float rate) { return (float)std::round(time * rate * 1000.0) / 1000.0f; }

double slope_of(const YamlNode *n, const char *comp) {
  if (!n) return 0.0;
  if (n->kind == YamlNode::Scalar) return std::strtod(n->value.c_str(), nullptr);  // (strtod reads "Infinity")
  const YamlNode *c = n->get(comp);
  return c ? std::strtod(c->value.c_str(), nullptr) : 0.0;
}

/* ---- reading */

struct ReadKey {  // (one Unity key of one component, before the spans' modes are settled)
  float frame = 0.0f;
  double value = 0.0, in_slope = 0.0, out_slope = 0.0;
  int mode = pack_modes(kClampedAuto, kClampedAuto, false);
};

std::vector<ReadKey> read_keys(const YamlNode &curve, const char *comp, float rate) {
  std::vector<ReadKey> out;
  const YamlNode *keys = curve.get("m_Curve");
  if (!keys || keys->kind != YamlNode::Seq) return out;
  for (const YamlNode &k : keys->seq) {
    ReadKey r;
    r.frame = frame_of(k.num("time", 0.0), rate);
    const YamlNode *v = k.get("value");
    r.value = !v ? 0.0 : v->kind == YamlNode::Scalar ? std::strtod(v->value.c_str(), nullptr) : v->num(comp, 0.0);
    r.in_slope = slope_of(k.get("inSlope"), comp), r.out_slope = slope_of(k.get("outSlope"), comp);
    r.mode = (int)k.num("tangentMode", pack_modes(kClampedAuto, kClampedAuto, false));
    out.push_back(r);
  }
  std::sort(out.begin(), out.end(), [](const ReadKey &a, const ReadKey &b) { return a.frame < b.frame; });
  return out;
}

/* The span k -> k+1 as Blendity's key k: constant when either end says so (Unity steps when key k's out slope or key
 * k+1's in slope is infinite), linear when both ends are linear, free with the slopes when broken or free, else auto. */
void settle_span(const std::vector<ReadKey> &ks, size_t i, float rate, float scale, AnimKey &out) {
  const ReadKey &k = ks[i];
  const ReadKey *next = i + 1 < ks.size() ? &ks[i + 1] : nullptr;
  const bool constant = std::isinf(k.out_slope) || right_mode(k.mode) == kConstant || (next && (std::isinf(next->in_slope) || left_mode(next->mode) == kConstant));
  if (constant) return (void)(out.interp = AnimConstant);
  const bool linear = right_mode(k.mode) == kLinear && (!next || left_mode(next->mode) == kLinear);
  if (linear) return (void)(out.interp = AnimLinear);
  const bool free = (k.mode & 1) || left_mode(k.mode) == kFree || right_mode(k.mode) == kFree;
  if (!free) return (void)(out.tangent = TanAuto);
  out.tangent = TanBroken;
  out.in_slope = std::isfinite(k.in_slope) ? (float)(k.in_slope * scale / rate) : 0.0f;
  out.out_slope = std::isfinite(k.out_slope) ? (float)(k.out_slope * scale / rate) : 0.0f;
}

void keys_onto_track(const std::vector<ReadKey> &ks, float rate, float scale, AnimTrack &t) {
  for (size_t i = 0; i < ks.size(); i++) {
    AnimKey key;
    key.frame = ks[i].frame, key.value = (float)(ks[i].value * scale);
    settle_span(ks, i, rate, scale, key);
    t.put_key(key);
  }
}

/* The per-axis editor curves (Unity's own layout for Euler-interpolated clips): attribute m_LocalPosition.x and the
 * like on the Transform, localEulerAnglesRaw.x in m_EulerEditorCurves. The properties they cover, by path, so the
 * runtime vector curves don't add keys over them. */
void read_editor_curves(const YamlNode &body, const char *list, float rate, AnimationClip &out, std::set<std::string> &covered) {
  const YamlNode *curves = body.get(list);
  if (!curves || curves->kind != YamlNode::Seq) return;
  static const std::pair<const char *, const char *> kNames[] = {{"m_LocalPosition.", "Position"}, {"localEulerAnglesRaw.", "Rotation"}, {"m_LocalScale.", "Scale"}, {"m_LocalEulerAngles.", "Rotation"}};
  for (const YamlNode &c : curves->seq) {
    const std::string path = c.str("path"), attr = c.str("attribute");
    const YamlNode *curve = c.get("curve");
    if (!curve) continue;
    for (const auto &[prefix, property] : kNames) {
      if (attr.rfind(prefix, 0) != 0 || attr.size() != std::strlen(prefix) + 1) continue;
      const int axis = attr.back() == 'x' ? 0 : attr.back() == 'y' ? 1 : attr.back() == 'z' ? 2 : -1;
      if (axis < 0) continue;
      const std::vector<ReadKey> ks = read_keys(*curve, "x", rate);
      if (ks.empty()) continue;  // (Unity's empty m_LocalEulerAngles hints: not a curve)
      keys_onto_track(ks, rate, 1.0f, out.track(path, property, axis));
      covered.insert(path + "|" + property);
    }
  }
}

void read_vector_curves(const YamlNode &body, const char *list, const char *property, float rate, AnimationClip &out, const std::set<std::string> &covered) {
  const YamlNode *curves = body.get(list);
  if (!curves || curves->kind != YamlNode::Seq) return;
  static const char *const comps[3] = {"x", "y", "z"};
  for (const YamlNode &c : curves->seq) {
    const std::string path = c.str("path");
    const YamlNode *curve = c.get("curve");
    if (!curve || covered.count(path + "|" + property)) continue;
    for (int a = 0; a < 3; a++) keys_onto_track(read_keys(*curve, comps[a], rate), rate, 1.0f, out.track(path, property, a));
  }
}

/* Quaternion rotation curves onto Euler tracks for paths with no Euler curve: each key's quaternion as Unity's Euler
 * angles, the turn nearest the key before (a quaternion has no winding); the keys auto. */
void read_rotation_curves(const YamlNode &body, float rate, AnimationClip &out, const std::set<std::string> &covered) {
  const YamlNode *curves = body.get("m_RotationCurves");
  if (!curves || curves->kind != YamlNode::Seq) return;
  for (const YamlNode &c : curves->seq) {
    const std::string path = c.str("path");
    const YamlNode *curve = c.get("curve");
    const YamlNode *keys = curve ? curve->get("m_Curve") : nullptr;
    if (!keys || keys->kind != YamlNode::Seq || covered.count(path + "|Rotation")) continue;
    Vec3 prev(0.0f);
    bool first = true;
    for (const YamlNode &k : keys->seq) {
      const YamlNode *v = k.get("value");
      if (!v) continue;
      Vec3 e = normalize(Quat((float)v->num("x"), (float)v->num("y"), (float)v->num("z"), (float)v->num("w", 1.0))).to_euler();
      for (int a = 0; a < 3 && !first; a++) e[a] -= 360.0f * std::round((e[a] - prev[a]) / 360.0f);
      prev = e, first = false;
      const float frame = frame_of(k.num("time", 0.0), rate);
      for (int a = 0; a < 3; a++) {
        AnimKey key;
        key.frame = frame, key.value = e[a];
        out.track(path, "Rotation", a).put_key(key);
      }
    }
  }
}

void read_float_curves(const YamlNode &body, float rate, AnimationClip &out, std::map<std::string, int> &not_carried) {
  const YamlNode *curves = body.get("m_FloatCurves");
  if (!curves || curves->kind != YamlNode::Seq) return;
  for (const YamlNode &c : curves->seq) {
    const std::string path = c.str("path"), attr = c.str("attribute");
    const YamlNode *curve = c.get("curve");
    if (!curve) continue;
    if (attr.rfind(kBlendShapeAttr, 0) == 0) {
      keys_onto_track(read_keys(*curve, "x", rate), rate, 0.01f, out.track(path, std::string(kBlendShapePrefix) + attr.substr(std::strlen(kBlendShapeAttr)), 0));
      continue;
    }
    if (attr == "m_IsActive") {
      keys_onto_track(read_keys(*curve, "x", rate), rate, 1.0f, out.track(path, "Active", 0));
      continue;
    }
    const int class_id = (int)c.num("classID", 0);
    const std::string kind = class_id == kClassAnimator ? "Humanoid muscle curve(s)" : attr.rfind("material.", 0) == 0 ? "material value curve(s)" : "script or component value curve(s)";
    not_carried[kind]++;
  }
}

/* ---- writing */

struct AnimCurveOut {  // (one Unity curve: the frames, a value, in and out slope and packed mode per frame and component)
  std::string path, attribute;
  int class_id = 0, comps = 1;
  std::vector<float> frames;
  std::vector<std::vector<double>> values, in_slopes, out_slopes;
  std::vector<int> modes;
};

std::vector<float> union_frames(const std::vector<const AnimTrack *> &ts) {
  std::vector<float> fs;
  for (const AnimTrack *t : ts)
    if (t)
      for (const AnimKey &k : t->keys) fs.push_back(k.frame);
  std::sort(fs.begin(), fs.end());
  fs.erase(std::unique(fs.begin(), fs.end(), [](float a, float b) { return std::fabs(a - b) < 0.01f; }), fs.end());
  return fs;
}

/* Unity's two sides of Blendity's key i of `t`: the left from the span before (constant: an infinite in slope; linear:
 * the secant), the right from the key's own span; a free key broken with its slopes. */
struct KeySides {
  double in = 0.0, out = 0.0;
  int left = kClampedAuto, right = kClampedAuto;
  bool broken = false;
};

double secant(const AnimTrack &t, size_t a, size_t b, float rate) {
  const float df = t.keys[b].frame - t.keys[a].frame;
  return df > 1e-6f ? (double)(t.keys[b].value - t.keys[a].value) / df * rate : 0.0;
}

KeySides key_sides(const AnimTrack &t, size_t i, float rate, float scale) {
  KeySides s;
  const AnimKey &k = t.keys[i];
  const bool has_next = i + 1 < t.keys.size(), has_prev = i > 0;
  s.in = t.in_tangent(i) * rate * scale, s.out = t.out_tangent(i) * rate * scale;
  if (k.tangent == TanBroken || k.tangent == TanFree || k.tangent == TanFlat) s.left = s.right = kFree, s.broken = k.tangent == TanBroken;
  if (has_next && k.interp == AnimLinear) s.right = kLinear, s.out = secant(t, i, i + 1, rate) * scale, s.broken = true;
  if (has_next && k.interp == AnimConstant) s.right = kConstant, s.out = kInf, s.broken = true;
  if (has_prev && t.keys[i - 1].interp == AnimLinear) s.left = kLinear, s.in = secant(t, i - 1, i, rate) * scale, s.broken = true;
  if (has_prev && t.keys[i - 1].interp == AnimConstant) s.left = kConstant, s.in = kInf, s.broken = true;
  return s;
}

/* The curve's slope at a frame between its keys, by a finite difference: for an axis sampled at another's key. */
double sampled_slope(const AnimTrack &t, float frame, float rate) {
  const float h = 0.05f;
  return (double)(t.sample(frame + h) - t.sample(frame - h)) / (2.0f * h) * rate;
}

/* A vector property's three axis tracks as one runtime curve at the union of their frames. */
AnimCurveOut vector_curve(const std::string &path, const std::vector<const AnimTrack *> &ts, const Vec3 &fill, float rate) {
  AnimCurveOut c;
  c.path = path, c.comps = 3;
  c.frames = union_frames(ts);
  for (float f : c.frames) {
    std::vector<double> v(3, 0.0), in(3, 0.0), out(3, 0.0);
    int left = kClampedAuto, right = kClampedAuto;
    bool broken = false, any_key = false;
    for (int a = 0; a < 3; a++) {
      const AnimTrack *t = ts[(size_t)a];
      if (!t) {
        v[(size_t)a] = fill[a];
        continue;
      }
      const int ki = t->key_at(f);
      if (ki < 0) {  // (no key of its own here: its curve's value and slope, free)
        v[(size_t)a] = t->sample(f), in[(size_t)a] = out[(size_t)a] = sampled_slope(*t, f, rate), broken = true;
        continue;
      }
      const KeySides s = key_sides(*t, (size_t)ki, rate, 1.0f);
      v[(size_t)a] = t->keys[(size_t)ki].value, in[(size_t)a] = s.in, out[(size_t)a] = s.out;
      if (!any_key) left = s.left, right = s.right, any_key = true;
      broken = broken || s.broken;
    }
    if (broken && !any_key) left = right = kFree;
    c.values.push_back(v), c.in_slopes.push_back(in), c.out_slopes.push_back(out), c.modes.push_back(pack_modes(left, right, broken));
  }
  return c;
}

/* One track as a float curve (a blend shape in 0-100, Active, or an axis's editor curve). */
AnimCurveOut float_curve(const std::string &path, const std::string &attribute, int class_id, const AnimTrack &t, float scale, float rate) {
  AnimCurveOut c;
  c.path = path, c.attribute = attribute, c.class_id = class_id, c.comps = 1;
  for (size_t i = 0; i < t.keys.size(); i++) {
    const KeySides s = key_sides(t, i, rate, scale);
    c.frames.push_back(t.keys[i].frame);
    c.values.push_back({(double)t.keys[i].value * scale}), c.in_slopes.push_back({s.in}), c.out_slopes.push_back({s.out});
    c.modes.push_back(pack_modes(s.left, s.right, s.broken));
  }
  return c;
}

void write_vec(std::ostringstream &os, const char *indent, const char *name, const std::vector<double> &v, int comps) {
  os << indent << "  " << name << ": ";
  if (comps == 1) return (void)(os << num(v[0]) << "\n");
  os << "{x: " << num(v[0]) << ", y: " << num(v[1]) << ", z: " << num(v[2]) << "}\n";
}

void write_keys(std::ostringstream &os, const AnimCurveOut &c, float rate, const char *indent) {
  os << indent << "m_Curve:" << (c.frames.empty() ? " []\n" : "\n");
  const std::vector<double> weights((size_t)c.comps, 0.333333343);
  for (size_t f = 0; f < c.frames.size(); f++) {
    os << indent << "- serializedVersion: 3\n" << indent << "  time: " << num(c.frames[f] / rate) << "\n";
    write_vec(os, indent, "value", c.values[f], c.comps);
    write_vec(os, indent, "inSlope", c.in_slopes[f], c.comps);
    write_vec(os, indent, "outSlope", c.out_slopes[f], c.comps);
    os << indent << "  tangentMode: " << c.modes[f] << "\n" << indent << "  weightedMode: 0\n";
    write_vec(os, indent, "inWeight", weights, c.comps);
    write_vec(os, indent, "outWeight", weights, c.comps);
  }
}

void write_curve_list(std::ostringstream &os, const char *name, const std::vector<AnimCurveOut> &curves, float rate, bool with_attribute) {
  os << "  " << name << ":" << (curves.empty() ? " []\n" : "\n");
  for (const AnimCurveOut &c : curves) {
    os << "  - curve:\n      serializedVersion: 2\n";
    write_keys(os, c, rate, "      ");
    os << "      m_PreInfinity: 2\n      m_PostInfinity: 2\n      m_RotationOrder: 4\n";
    if (with_attribute) os << "    attribute: " << yaml_scalar(c.attribute) << "\n";
    os << "    path: " << yaml_scalar(c.path) << "\n";
    if (with_attribute) os << "    classID: " << c.class_id << "\n    script: {fileID: 0}\n    flags: 0\n";
  }
}

/* The object's current value for an axis without a track (the clip's root and the path below it). */
Vec3 fill_value(const GameObject *root, const std::string &path, const std::string &property, std::vector<std::string> *notes) {
  Vec3 v = property == "Scale" ? Vec3(1.0f) : Vec3(0.0f);
  GameObject *g = root ? anim_target(*const_cast<GameObject *>(root), path) : nullptr;
  for (int a = 0; a < 3; a++) {
    float x = v[a];
    if (g && read_anim_value(*g, property, a, x)) v[a] = x;
  }
  if (!g && notes && property != "Scale") notes->push_back((path.empty() ? std::string("(root)") : path) + " " + property + ": an axis without keys written as 0 (no object to read it from)");
  return v;
}

struct Lists {
  std::vector<AnimCurveOut> pos, eul, scl, flt, editor, euler_editor;
  int dropped = 0;
};

void vector_lists(const std::string &path, const std::string &prop, const std::vector<const AnimTrack *> &ts, const GameObject *root, float rate, std::vector<std::string> *notes, Lists &l) {
  const bool any_missing = !ts[0] || !ts[1] || !ts[2];
  const Vec3 fill = any_missing ? fill_value(root, path, prop, notes) : Vec3(0.0f);
  std::vector<AnimCurveOut> &runtime = prop == "Position" ? l.pos : prop == "Rotation" ? l.eul : l.scl;
  runtime.push_back(vector_curve(path, ts, fill, rate));
  const char *attr = prop == "Position" ? "m_LocalPosition." : prop == "Rotation" ? "localEulerAnglesRaw." : "m_LocalScale.";
  for (int a = 0; a < 3; a++) {  // (all three in m_EditorCurves, localEulerAnglesRaw.* included: where Unity's own files keep them)
    if (!ts[(size_t)a]) continue;
    l.editor.push_back(float_curve(path, std::string(attr) + "xyz"[a], kClassTransform, *ts[(size_t)a], 1.0f, rate));
  }
}

void float_lists(const std::string &path, const std::string &prop, const AnimTrack *t, float rate, Lists &l) {
  if (!t) return;
  const bool shape = prop != "Active";
  const std::string attr = shape ? std::string(kBlendShapeAttr) + prop.substr(std::strlen(kBlendShapePrefix)) : std::string("m_IsActive");
  l.flt.push_back(float_curve(path, attr, shape ? kClassSkinnedMeshRenderer : kClassGameObject, *t, shape ? 100.0f : 1.0f, rate));
}

}  // namespace

bool is_unity_anim_text(const std::string &text) { return text.rfind("%YAML", 0) == 0 || text.find("--- !u!74 ") != std::string::npos; }

bool unity_anim_to_clip(const std::string &text, AnimationClip &out, std::vector<std::string> *notes, std::string *why, bool *lossy) {
  std::vector<UnityDoc> docs;
  std::string parse_why;
  if (!parse_unity_yaml(text, docs, parse_why)) {
    if (why) *why = parse_why;
    return false;
  }
  const UnityDoc *clip = nullptr;
  for (const UnityDoc &d : docs)
    if (d.class_id == kClassAnimationClip || d.type == "AnimationClip") clip = &d;
  if (!clip) {
    if (why) *why = "no AnimationClip document in the file";
    return false;
  }
  const YamlNode &b = clip->body;
  out.tracks.clear(), out.contacts.clear(), out.ragdoll.clear();
  out.name = b.str("m_Name", out.name);
  float rate = (float)b.num("m_SampleRate", 60.0);
  if (!(rate > 0.0f) || !std::isfinite(rate)) rate = 60.0f;
  out.frame_rate = rate;
  const YamlNode *settings = b.get("m_AnimationClipSettings");
  out.loop = settings && settings->num("m_LoopTime", 0.0) != 0.0;
  std::set<std::string> covered;
  read_editor_curves(b, "m_EditorCurves", rate, out, covered);
  read_editor_curves(b, "m_EulerEditorCurves", rate, out, covered);
  read_vector_curves(b, "m_PositionCurves", "Position", rate, out, covered);
  read_vector_curves(b, "m_EulerCurves", "Rotation", rate, out, covered);
  read_vector_curves(b, "m_ScaleCurves", "Scale", rate, out, covered);
  if (const YamlNode *e = b.get("m_EulerCurves"); e && e->kind == YamlNode::Seq)
    for (const YamlNode &c : e->seq) covered.insert(c.str("path") + "|Rotation");
  read_rotation_curves(b, rate, out, covered);
  std::map<std::string, int> not_carried;
  read_float_curves(b, rate, out, not_carried);
  auto seq_size = [&](const char *key) {
    const YamlNode *n = b.get(key);
    return n && n->kind == YamlNode::Seq ? n->seq.size() : (size_t)0;
  };
  if (seq_size("m_PPtrCurves")) not_carried["object reference curve(s) (sprites, materials)"] += (int)seq_size("m_PPtrCurves");
  if (seq_size("m_Events")) not_carried["animation event(s)"] += (int)seq_size("m_Events");
  if (b.num("m_Legacy", 0.0) != 0.0) not_carried["legacy flag (the Animation component's clips)"] = 1;
  if (lossy) *lossy = !not_carried.empty();
  if (notes)
    for (const auto &[kind, n] : not_carried) notes->push_back(std::to_string(n) + " " + kind + " not carried");
  const double stop = settings ? settings->num("m_StopTime", 0.0) : 0.0;
  float last = 0.0f;
  for (const AnimTrack &t : out.tracks)
    for (const AnimKey &k : t.keys) last = std::max(last, k.frame);
  out.length = std::max(stop > 0.0 ? (float)(stop * rate) : 0.0f, last);
  if (out.length <= 0.0f) out.length = 1.0f;
  out.unity_yaml = true;
  return true;
}

std::string clip_to_unity_anim(const AnimationClip &c, const GameObject *root, std::vector<std::string> *notes) {
  const float rate = c.frame_rate > 0.0f ? c.frame_rate : 60.0f;
  std::map<std::string, std::map<std::string, std::vector<const AnimTrack *>>> by_path;
  std::vector<std::string> path_order;
  for (const AnimTrack &t : c.tracks) {
    if (!by_path.count(t.path)) path_order.push_back(t.path);
    std::vector<const AnimTrack *> &ts = by_path[t.path][t.property];
    if (ts.size() < 3) ts.resize(3, nullptr);
    if (t.axis >= 0 && t.axis < 3) ts[(size_t)t.axis] = &t;
  }
  Lists l;
  for (const std::string &path : path_order)
    for (const auto &[prop, ts] : by_path[path]) {
      if (is_transform_property(prop)) vector_lists(path, prop, ts, root, rate, notes, l);
      else if (prop.rfind(kBlendShapePrefix, 0) == 0 || prop == "Active") float_lists(path, prop, ts[0], rate, l);
      else l.dropped++;
    }
  if (l.dropped && notes) notes->push_back(std::to_string(l.dropped) + " track(s) of other properties (lights, materials, components) left out: Unity's clip has no field for them");
  std::ostringstream os;
  os << "%YAML 1.1\n%TAG !u! tag:unity3d.com,2011:\n--- !u!74 &7400000\nAnimationClip:\n  m_ObjectHideFlags: 0\n  m_CorrespondingSourceObject: {fileID: 0}\n"
        "  m_PrefabInstance: {fileID: 0}\n  m_PrefabAsset: {fileID: 0}\n  m_Name: " << yaml_scalar(c.name) << "\n  serializedVersion: 6\n  m_Legacy: 0\n  m_Compressed: 0\n  m_UseHighQualityCurve: 1\n"
        "  m_RotationCurves: []\n  m_CompressedRotationCurves: []\n";
  write_curve_list(os, "m_EulerCurves", l.eul, rate, false);
  write_curve_list(os, "m_PositionCurves", l.pos, rate, false);
  write_curve_list(os, "m_ScaleCurves", l.scl, rate, false);
  write_curve_list(os, "m_FloatCurves", l.flt, rate, true);
  os << "  m_PPtrCurves: []\n  m_SampleRate: " << num(rate) << "\n  m_WrapMode: 0\n  m_Bounds:\n    m_Center: {x: 0, y: 0, z: 0}\n    m_Extent: {x: 0, y: 0, z: 0}\n"
        "  m_ClipBindingConstant:\n    genericBindings: []\n    pptrCurveMapping: []\n  m_AnimationClipSettings:\n    serializedVersion: 2\n    m_AdditiveReferencePoseClip: {fileID: 0}\n"
        "    m_AdditiveReferencePoseTime: 0\n    m_StartTime: 0\n    m_StopTime: " << num(std::max(c.length, c.end_frame()) / rate) << "\n    m_OrientationOffsetY: 0\n    m_Level: 0\n    m_CycleOffset: 0\n"
        "    m_HasAdditiveReferencePose: 0\n    m_LoopTime: " << (c.loop ? 1 : 0) << "\n    m_LoopBlend: 0\n    m_LoopBlendOrientation: 0\n    m_LoopBlendPositionY: 0\n    m_LoopBlendPositionXZ: 0\n"
        "    m_KeepOriginalOrientation: 0\n    m_KeepOriginalPositionY: 1\n    m_KeepOriginalPositionXZ: 0\n    m_HeightFromFeet: 0\n    m_Mirror: 0\n";
  std::vector<AnimCurveOut> editor = l.editor;
  editor.insert(editor.end(), l.flt.begin(), l.flt.end());
  write_curve_list(os, "m_EditorCurves", editor, rate, true);
  write_curve_list(os, "m_EulerEditorCurves", l.euler_editor, rate, true);
  os << "  m_HasGenericRootTransform: 0\n  m_HasMotionFloatCurves: 0\n  m_Events: []\n";
  return os.str();
}

std::string unity_package_clip_data(const std::string &data, const std::string &pathname, bool *converted, int *dropped, int *zeroed) {
  if (converted) *converted = false;
  if (fs::extension(pathname) != ".anim" || data.rfind("blendity_anim", 0) != 0) return data;
  AnimationClip clip;
  if (!load_clip_text(data, clip)) return data;
  clip.name = fs::stem(pathname);
  std::vector<std::string> notes;
  const std::string out = clip_to_unity_anim(clip, nullptr, &notes);
  if (converted) *converted = true;
  if (zeroed)
    for (const std::string &n : notes) *zeroed += n.find("written as 0") != std::string::npos;
  if (dropped)
    for (const AnimTrack &t : clip.tracks) *dropped += !is_transform_property(t.property) && t.property.rfind(kBlendShapePrefix, 0) != 0 && t.property != "Active";
  return out;
}

std::string unity_anim_meta(const std::string &guid) {
  return "fileFormatVersion: 2\nguid: " + guid + "\nNativeFormatImporter:\n  externalObjects: {}\n  mainObjectFileID: 7400000\n  userData: \n  assetBundleName: \n  assetBundleVariant: \n";
}

}  // namespace bl

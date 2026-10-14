// SPDX-License-Identifier: GPL-2.0-or-later
// Keyframe animation (task 0059, ADR 0029). The Auto interpolation is Unity's ClampedAuto: a cubic Hermite through the
// keys with each key's tangent the average of its two neighbours' slopes, flat at a local peak or dip, and limited so
// no span overshoots its two keys (Fritsch-Carlson monotone limits).
#include "animation.h"

#include "material.h"
#include "sequence.h"

#include "../core/core.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <unordered_map>

namespace bl {

/* ---------------------------------------------------------------- tracks */

namespace {

/* Key i's tangent (value per frame) for the Auto curve: the average of the slopes on its two sides, 0 at an extreme,
 * limited to three times either side's slope so the spans stay between their keys. */
float auto_tangent(const std::vector<AnimKey> &k, size_t i) {
  if (k.size() < 2) return 0.0f;
  if (i == 0 || i + 1 == k.size()) return 0.0f;  // (the ends: flat, as Unity's clamped tangents)
  const float dl = k[i].frame - k[i - 1].frame, dr = k[i + 1].frame - k[i].frame;
  const float sl = dl > 0.0f ? (k[i].value - k[i - 1].value) / dl : 0.0f, sr = dr > 0.0f ? (k[i + 1].value - k[i].value) / dr : 0.0f;
  if (sl * sr <= 0.0f) return 0.0f;  // a peak or a dip
  const float t = 0.5f * (sl + sr);
  const float limit = 3.0f * std::min(std::fabs(sl), std::fabs(sr));
  return std::max(-limit, std::min(limit, t));
}

float hermite(float p0, float m0, float p1, float m1, float u, float span) {
  const float u2 = u * u, u3 = u2 * u;
  return (2 * u3 - 3 * u2 + 1) * p0 + (u3 - 2 * u2 + u) * span * m0 + (-2 * u3 + 3 * u2) * p1 + (u3 - u2) * span * m1;
}

}  // namespace

float AnimTrack::sample(float frame) const {
  if (keys.empty()) return 0.0f;
  if (frame <= keys.front().frame) return keys.front().value;
  if (frame >= keys.back().frame) return keys.back().value;
  const auto it = std::upper_bound(keys.begin(), keys.end(), frame, [](float f, const AnimKey &k) { return f < k.frame; });
  const size_t i = (size_t)(it - keys.begin()) - 1;
  const AnimKey &a = keys[i], &b = keys[i + 1];
  const float span = b.frame - a.frame;
  if (!(span > 0.0f) || a.interp == AnimConstant) return a.value;
  const float u = (frame - a.frame) / span;
  if (a.interp == AnimLinear) return a.value + (b.value - a.value) * u;
  return hermite(a.value, out_tangent(i), b.value, in_tangent(i + 1), u, span);
}

float AnimTrack::in_tangent(size_t i) const {
  if (i >= keys.size()) return 0.0f;
  switch (keys[i].tangent) {
    case TanFlat: return 0.0f;
    case TanFree:
    case TanBroken: return keys[i].in_slope;
    default: return auto_tangent(keys, i);
  }
}

float AnimTrack::out_tangent(size_t i) const {
  if (i >= keys.size()) return 0.0f;
  switch (keys[i].tangent) {
    case TanFlat: return 0.0f;
    case TanFree: return keys[i].in_slope;  // (one slope both sides)
    case TanBroken: return keys[i].out_slope;
    default: return auto_tangent(keys, i);
  }
}

size_t AnimTrack::put_key(const AnimKey &k) {
  const size_t i = set_key(k.frame, k.value, k.interp, false);
  if (i == SIZE_MAX) return i;
  keys[i].tangent = k.tangent, keys[i].in_slope = k.in_slope, keys[i].out_slope = k.out_slope;
  return i;
}

int AnimTrack::key_at(float frame) const {
  for (size_t i = 0; i < keys.size(); i++)
    if (std::fabs(keys[i].frame - frame) < 0.01f) return (int)i;
  return -1;
}

size_t AnimTrack::set_key(float frame, float value, int interp, bool keep_interp) {
  if (!std::isfinite(frame) || !std::isfinite(value)) return SIZE_MAX;
  const int at = key_at(frame);
  if (at >= 0) {
    keys[(size_t)at].value = value;
    if (!keep_interp) keys[(size_t)at].interp = interp;
    return (size_t)at;
  }
  const auto it = std::upper_bound(keys.begin(), keys.end(), frame, [](float f, const AnimKey &k) { return f < k.frame; });
  AnimKey k;
  k.frame = frame, k.value = value, k.interp = interp;
  const auto at_new = keys.insert(it, k);  // (before begin(): insert may move the keys, and the order of a - b is unspecified)
  return (size_t)(at_new - keys.begin());
}

std::string AnimTrack::label() const {
  static const char *kAxes[] = {"x", "y", "z", "w"};
  const std::string who = path.empty() ? "(root)" : path.substr(path.rfind('/') == std::string::npos ? 0 : path.rfind('/') + 1);
  return who + " : " + property + "." + kAxes[std::max(0, std::min(3, axis))];
}

float AnimationClip::end_frame() const {
  float e = length;
  for (const AnimTrack &t : tracks)
    if (!t.keys.empty()) e = std::max(e, t.keys.back().frame);
  return e;
}

AnimTrack *AnimationClip::find(const std::string &path, const std::string &property, int axis) {
  for (AnimTrack &t : tracks)
    if (t.path == path && t.property == property && t.axis == axis) return &t;
  return nullptr;
}

AnimTrack &AnimationClip::track(const std::string &path, const std::string &property, int axis) {
  if (AnimTrack *t = find(path, property, axis)) return *t;
  tracks.push_back({path, property, axis, {}});
  return tracks.back();
}

/* ---------------------------------------------------------------- files */

/* blendity_anim 1 / name / rate / length / loop, then for each track a "track<TAB><axis><TAB><property><TAB><path>"
 * line (tabs: properties and object names may hold spaces, "Light.Spot Angle") and its "key <frame> <value> <interp>"
 * lines. */
std::string save_clip_text(const AnimationClip &c) {
  std::ostringstream os;
  os.precision(9);
  os << "blendity_anim 1\nname " << c.name << "\nrate " << c.frame_rate << "\nlength " << c.length << "\nloop " << (c.loop ? 1 : 0) << "\n";
  for (const AnimTrack &t : c.tracks) {
    os << "track\t" << t.axis << "\t" << t.property << "\t" << t.path << "\n";
    for (const AnimKey &k : t.keys) {
      os << "key " << k.frame << " " << k.value << " " << k.interp;
      if (k.tangent != TanAuto) os << " " << k.tangent << " " << k.in_slope << " " << k.out_slope;  // (task 0061)
      os << "\n";
    }
  }
  return os.str();
}

namespace {

/* "track<TAB>axis<TAB>property<TAB>path": a new track of `c` (nullptr for a line that isn't one). */
AnimTrack *read_track(const std::string &line, AnimationClip &c) {
  std::vector<std::string> f;
  size_t s = 0;
  for (size_t e = line.find('\t'); ; e = line.find('\t', s)) {
    f.push_back(line.substr(s, e == std::string::npos ? std::string::npos : e - s));
    if (e == std::string::npos) break;
    s = e + 1;
  }
  if (f.size() < 3 || f[1].empty() || f[2].empty()) return nullptr;
  AnimTrack t;
  t.axis = std::max(0, std::min(2, std::atoi(f[1].c_str())));
  t.property = f[2];
  t.path = f.size() > 3 ? f[3] : std::string();
  c.tracks.push_back(std::move(t));
  return &c.tracks.back();
}

}  // namespace

bool load_clip_text(const std::string &text, AnimationClip &c) {
  std::istringstream is(text);
  std::string line;
  if (!std::getline(is, line) || line.rfind("blendity_anim", 0) != 0) return false;
  c.tracks.clear();
  AnimTrack *cur = nullptr;
  while (std::getline(is, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::istringstream ls(line);
    std::string word;
    ls >> word;
    if (word == "name") std::getline(ls >> std::ws, c.name);
    else if (word == "rate") ls >> c.frame_rate;
    else if (word == "length") ls >> c.length;
    else if (word == "loop") {
      int l = 1;
      ls >> l;
      c.loop = l != 0;
    }
    else if (word == "track") {
      cur = read_track(line, c);
    }
    else if (word == "key" && cur) {
      AnimKey k;
      if (!(ls >> k.frame >> k.value >> k.interp)) continue;  // (a bad number: no key)
      const size_t i = cur->set_key(k.frame, k.value, std::max(0, std::min(2, k.interp)));
      int tan = TanAuto;
      float in = 0.0f, out = 0.0f;
      if (i == SIZE_MAX || !(ls >> tan >> in >> out) || !std::isfinite(in) || !std::isfinite(out)) continue;  // (no tangents: Clamped Auto)
      const float limit = 1e6f;  // (a slope steeper than this draws as a vertical line anyway)
      cur->keys[i].tangent = std::max(0, std::min(3, tan));
      cur->keys[i].in_slope = std::max(-limit, std::min(limit, in)), cur->keys[i].out_slope = std::max(-limit, std::min(limit, out));
    }
  }
  if (!(c.frame_rate > 0.0f) || !std::isfinite(c.frame_rate)) c.frame_rate = 60.0f;
  if (!(c.length >= 0.0f) || !std::isfinite(c.length)) c.length = 60.0f;
  c.touch();
  return true;
}

namespace {

struct ClipEntry {
  AnimationClipPtr clip;
  uint64_t saved_version = 0;
};
std::unordered_map<std::string, ClipEntry> &clip_library() {
  static std::unordered_map<std::string, ClipEntry> lib;
  return lib;
}
std::string clip_key(std::string p) {
  std::replace(p.begin(), p.end(), '\\', '/');
  return p;
}

}  // namespace

AnimationClipPtr clip_asset(const std::string &path_in) {
  if (path_in.empty()) return nullptr;
  const std::string path = clip_key(path_in);
  auto &lib = clip_library();
  if (auto it = lib.find(path); it != lib.end()) return it->second.clip;
  /* A clip that isn't there: looked for again after a second, not on every call of every frame. */
  static std::unordered_map<std::string, double> missing;
  if (auto m = missing.find(path); m != missing.end() && now_seconds() - m->second < 1.0) return nullptr;
  std::string text;
  if (!fs::read_file(resolve_asset_path(path), text)) {
    missing[path] = now_seconds();
    return nullptr;
  }
  missing.erase(path);
  auto c = std::make_shared<AnimationClip>();
  if (!load_clip_text(text, *c)) return nullptr;
  c->asset_path = path;
  c->name = fs::stem(path);
  lib[path] = {c, c->version};
  return c;
}

AnimationClipPtr create_clip_asset(const std::string &path_in, const std::string &name) {
  const std::string path = clip_key(path_in);
  auto c = std::make_shared<AnimationClip>();
  c->name = name.empty() ? fs::stem(path) : name;
  c->asset_path = path;
  const std::string file = resolve_asset_path(path);
  fs::make_dirs(fs::parent(file));
  if (!fs::write_file(file, save_clip_text(*c))) return nullptr;
  clip_library()[path] = {c, c->version};
  return c;
}

size_t save_dirty_clip_assets() {
  size_t n = 0;
  for (auto &kv : clip_library()) {
    ClipEntry &e = kv.second;
    if (!e.clip || e.clip->version == e.saved_version) continue;
    if (!fs::write_file(resolve_asset_path(kv.first), save_clip_text(*e.clip))) continue;
    e.saved_version = e.clip->version;
    n++;
  }
  return n;
}

size_t retarget_clip_assets(const std::string &from_in, const std::string &to_in) {
  const std::string from = clip_key(from_in), to = clip_key(to_in);
  auto &lib = clip_library();
  std::vector<std::pair<std::string, ClipEntry>> moved;
  for (auto it = lib.begin(); it != lib.end();) {
    const bool under = it->first == from || (it->first.size() > from.size() && it->first.compare(0, from.size(), from) == 0 && it->first[from.size()] == '/');
    if (!under) {
      ++it;
      continue;
    }
    moved.push_back({to + it->first.substr(from.size()), it->second});
    it = lib.erase(it);
  }
  for (auto &kv : moved) {
    if (kv.second.clip) kv.second.clip->asset_path = kv.first, kv.second.clip->name = fs::stem(kv.first);
    lib[kv.first] = kv.second;
  }
  return moved.size();
}

size_t forget_clip_assets(const std::string &path_in) {
  const std::string path = clip_key(path_in);
  auto &lib = clip_library();
  size_t n = 0;
  for (auto it = lib.begin(); it != lib.end();) {
    const bool under = it->first == path || (it->first.size() > path.size() && it->first.compare(0, path.size(), path) == 0 && it->first[path.size()] == '/');
    if (!under) {
      ++it;
      continue;
    }
    it = lib.erase(it);
    n++;
  }
  return n;
}

void clear_clip_assets() { clip_library().clear(); }

/* ---------------------------------------------------------------- binding */

bool is_transform_property(const std::string &p) { return p == "Position" || p == "Rotation" || p == "Scale"; }

namespace {
GameObject *child_named(GameObject *g, const std::string &name) {
  const auto it = std::find_if(g->children.begin(), g->children.end(), [&](const GameObject *c) { return c->name == name; });
  return it == g->children.end() ? nullptr : *it;
}
}  // namespace

GameObject *anim_target(GameObject &root, const std::string &path) {
  GameObject *g = &root;
  size_t s = 0;
  while (g && s < path.size()) {
    const size_t e = path.find('/', s);
    g = child_named(g, path.substr(s, e == std::string::npos ? std::string::npos : e - s));
    if (e == std::string::npos) break;
    s = e + 1;
  }
  return g;
}

std::string anim_path(const GameObject &root, const GameObject &g) {
  std::string p;
  for (const GameObject *x = &g; x && x != &root; x = x->parent) p = p.empty() ? x->name : x->name + "/" + p;
  return p;
}

namespace {

/* One number of a component field, by "Field" name and axis: read or written through the reflection (as `set`). */
struct FieldAccess : Reflector {
  std::string want;
  int axis = 0;
  bool write = false, found = false;
  float value = 0.0f;
  bool hit(const char *n) { return !found && want == n; }
  template<class T> void take(T &v) {
    if (write) v = (T)value;
    else value = (float)v;
    found = true;
  }
  void field(const char *n, float &v, float, float, float) override {
    if (hit(n)) take(v);
  }
  void field(const char *n, int &v, int, int) override {
    if (hit(n)) {
      if (write) v = (int)std::lround(value);
      else value = (float)v;
      found = true;
    }
  }
  void field(const char *n, bool &v) override {
    if (!hit(n)) return;
    if (write) v = value >= 0.5f;
    else value = v ? 1.0f : 0.0f;
    found = true;
  }
  void field(const char *n, Vec3 &v) override {
    if (hit(n)) take(v[std::max(0, std::min(2, axis))]);
  }
  void color(const char *n, Vec3 &v) override { field(n, v); }
  void enumeration(const char *n, int &v, const char *const *, int count) override {
    if (!hit(n)) return;
    if (write) v = std::max(0, std::min(count - 1, (int)std::lround(value)));
    else value = (float)v;
    found = true;
  }
  void text(const char *, std::string &) override {}
  void mesh(const char *, MeshPtr &) override {}
};

bool component_value(GameObject &g, const std::string &property, int axis, bool write, float &value) {
  const size_t dot = property.find('.');
  if (dot == std::string::npos) return false;
  const std::string type = property.substr(0, dot), field = property.substr(dot + 1);
  for (auto &c : g.components) {
    if (type != c->type_name()) continue;
    FieldAccess fa;
    fa.want = field, fa.axis = axis, fa.write = write, fa.value = value;
    c->reflect(fa);
    if (!fa.found) return false;
    value = fa.value;
    return true;
  }
  return false;
}

}  // namespace

bool read_anim_value(GameObject &g, const std::string &property, int axis, float &out) {
  const Transform &t = g.local();
  const int a = std::max(0, std::min(2, axis));
  if (property == "Position") return out = t.position[a], true;
  if (property == "Rotation") return out = t.euler_hint[a], true;
  if (property == "Scale") return out = t.scale[a], true;
  return component_value(g, property, axis, false, out);
}

bool write_anim_value(GameObject &g, const std::string &property, int axis, float value) {
  if (!std::isfinite(value)) return false;
  Transform t = g.local();
  const int a = std::max(0, std::min(2, axis));
  if (property == "Position") t.position[a] = value;
  else if (property == "Scale") t.scale[a] = value;
  else if (property == "Rotation") {
    t.euler_hint[a] = value;
    t.rotation = Quat::euler(t.euler_hint);
  }
  else return component_value(g, property, axis, true, value);
  g.set_local(t);
  return true;
}

void apply_clip(const AnimationClip &clip, GameObject &root, float frame) {
  /* Each object found once and its transform set once, however many of its numbers are animated. */
  std::unordered_map<std::string, GameObject *> found;
  std::unordered_map<GameObject *, Transform> poses;
  for (const AnimTrack &t : clip.tracks) {
    if (t.keys.empty()) continue;
    auto it = found.find(t.path);
    if (it == found.end()) it = found.emplace(t.path, anim_target(root, t.path)).first;
    GameObject *g = it->second;
    if (!g) continue;  // (renamed or gone: the window shows the track as missing)
    const float v = t.sample(frame);
    if (!std::isfinite(v)) continue;
    if (!is_transform_property(t.property)) {
      write_anim_value(*g, t.property, t.axis, v);
      continue;
    }
    auto p = poses.find(g);
    if (p == poses.end()) p = poses.emplace(g, g->local()).first;
    const int a = std::max(0, std::min(2, t.axis));
    if (t.property == "Position") p->second.position[a] = v;
    else if (t.property == "Scale") p->second.scale[a] = v;
    else p->second.euler_hint[a] = v;
  }
  for (auto &[g, pose] : poses) {
    pose.rotation = Quat::euler(pose.euler_hint);
    g->set_local(pose);
  }
}

float wrap_frame(float frame, float end, int wrap) {
  if (!(end > 0.0f) || !std::isfinite(frame)) return 0.0f;
  auto pmod = [](float a, float m) { return std::fmod(std::fmod(a, m) + m, m); };  // (a negative Speed plays backwards)
  if (wrap == 1 || wrap == 3) return std::max(0.0f, std::min(end, frame));  // Once (holds the end), Clamp Forever
  if (wrap == 2) {  // Ping Pong
    const float m = pmod(frame, 2.0f * end);
    return m <= end ? m : 2.0f * end - m;
  }
  return pmod(frame, end);  // Loop
}

/* ---------------------------------------------------------------- the component */

void Animation::reflect(Reflector &r) {
  r.asset("Clip", clip, ".anim", "Animation Clip");
  r.help("The .anim clip it plays (Assets/Animations). Make and edit clips in the Animation window (Window > Animation).");
  r.field("Play Automatically", play_automatically);
  r.help("Starts playing when Play starts (Unity: Play Automatically).");
  r.field("Speed", speed, 0.01f, -10.0f, 10.0f);
  static const char *kWrap[] = {"Loop", "Once", "Ping Pong", "Clamp Forever"};
  r.enumeration("Wrap", wrap, kWrap, 4);
  r.help("At the clip's end: start again, stop on the last frame, play back and forth, or hold the last frame.");
}

void Animation::start(PlayContext &) {
  time = 0.0f;
  playing = play_automatically;
}

void Animation::update(PlayContext &ctx) {
  if (!playing || !owner) return;
  if (const Sequencer *s = owner->get<Sequencer>(); s && s->enabled && sequence_asset(s->sequence)) return;  // (a sequence plays the clips: ADR 0030)
  const AnimationClipPtr c = clip_asset(clip);
  if (!c) return;
  time += ctx.dt * speed;
  const int w = wrap == 0 && !c->loop ? 1 : wrap;  // a clip that doesn't loop holds its end under Loop
  apply_clip(*c, *owner, wrap_frame(time * c->frame_rate, c->end_frame(), w));
}

}  // namespace bl

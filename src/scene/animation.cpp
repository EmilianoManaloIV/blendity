// SPDX-License-Identifier: GPL-2.0-or-later
// Keyframe animation (task 0059, ADR 0029). The Auto interpolation is Unity's ClampedAuto: a cubic Hermite through the
// keys with each key's tangent the average of its two neighbours' slopes, flat at a local peak or dip, and limited so
// no span overshoots its two keys (Fritsch-Carlson monotone limits).
#include "animation.h"
#include "unity_anim.h"  // (round 407)

#include "material.h"
#include "pose_counts.h"
#include "sequence.h"

#include "../core/core.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

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

int AnimTrack::key_at(float frame) const {  // (round 406: by the keys' order - a summary row asks for every frame)
  auto it = std::lower_bound(keys.begin(), keys.end(), frame - 0.01f, [](const AnimKey &k, float f) { return k.frame < f; });
  if (it != keys.end() && std::fabs(it->frame - frame) < 0.01f) return (int)(it - keys.begin());
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

namespace {
size_t track_hash(const std::string &path, const std::string &property, int axis) {
  size_t h = std::hash<std::string>()(path);
  h ^= std::hash<std::string>()(property) + (size_t)0x9e3779b9u + (h << 6) + (h >> 2);
  h ^= (size_t)(axis + 1) * (size_t)0x85ebca6bu + (h << 6) + (h >> 2);
  return h;
}
}  // namespace

/* Round 472: a clip with many tracks (an avatar's 275 bones keyed: 2,475) is looked up through a hash of (path, property,
 * axis) -> index instead of a walk over every track. Built again when the number of tracks or the clip's layout differ
 * from what it was built for (any edit but a keys-only one touches the layout); a hit is checked against the track it
 * names, and of several tracks that match the first one wins, as the walk did. */
AnimTrack *AnimationClip::find(const std::string &path, const std::string &property, int axis) {
  constexpr size_t kIndexFrom = 24;  // (fewer tracks than this: the walk is cheaper than keeping an index)
  if (tracks.size() < kIndexFrom) {
    for (AnimTrack &t : tracks)
      if (t.path == path && t.property == property && t.axis == axis) return &t;
    return nullptr;
  }
  AnimTrackIndex &ix = track_index;
  if (!ix.valid || ix.tracks != tracks.size() || ix.layout != layout) {
    ix.map.clear();
    ix.map.reserve(tracks.size());
    for (size_t i = 0; i < tracks.size(); i++) ix.map.emplace(track_hash(tracks[i].path, tracks[i].property, tracks[i].axis), (uint32_t)i);
    ix.tracks = tracks.size(), ix.layout = layout, ix.valid = true;
  }
  const auto range = ix.map.equal_range(track_hash(path, property, axis));
  size_t best = tracks.size();
  for (auto it = range.first; it != range.second; ++it) {
    const size_t i = it->second;
    if (i >= best || i >= tracks.size()) continue;
    const AnimTrack &t = tracks[i];
    if (t.path == path && t.property == property && t.axis == axis) best = i;
  }
  return best < tracks.size() ? &tracks[best] : nullptr;
}

AnimTrack &AnimationClip::track(const std::string &path, const std::string &property, int axis) {
  if (AnimTrack *t = find(path, property, axis)) return *t;
  const bool indexed = track_index.valid && track_index.tracks == tracks.size() && track_index.layout == layout;
  tracks.push_back({path, property, axis, {}});
  if (indexed) {  // (the new track goes into the index as it is made: the next lookup finds it without a rebuild)
    track_index.map.emplace(track_hash(path, property, axis), (uint32_t)(tracks.size() - 1));
    track_index.tracks = tracks.size();
  }
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
  for (const AnimationClip::Contact &k : c.contacts) os << "contact\t" << k.from << "\t" << k.to << "\t" << (k.manual ? 1 : 0) << "\t" << k.bone << "\n";
  for (const AnimationClip::RagdollEvent &e : c.ragdoll)
    os << "ragdoll\t" << e.kind << "\t" << e.frame << "\t" << e.impulse.x << "\t" << e.impulse.y << "\t" << e.impulse.z << "\t" << e.bone << "\n";
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
  c.contacts.clear();
  c.ragdoll.clear();
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
    else if (word == "contact") {  // ("contact<TAB>from<TAB>to<TAB>manual<TAB>bone", task 0116)
      AnimationClip::Contact k;
      int manual = 0;
      if (!(ls >> k.from >> k.to >> manual) || !std::isfinite(k.from) || !std::isfinite(k.to)) continue;
      std::getline(ls >> std::ws, k.bone);
      k.manual = manual != 0;
      if (!k.bone.empty() && k.to >= k.from) c.contacts.push_back(k);
    }
    else if (word == "ragdoll") {  // ("ragdoll<TAB>kind<TAB>frame<TAB>x<TAB>y<TAB>z<TAB>bone", task 0121)
      AnimationClip::RagdollEvent e;
      if (!(ls >> e.kind >> e.frame >> e.impulse.x >> e.impulse.y >> e.impulse.z) || e.kind < 0 || e.kind > 2 || !std::isfinite(e.frame) ||
          !std::isfinite(e.impulse.x) || !std::isfinite(e.impulse.y) || !std::isfinite(e.impulse.z))
        continue;
      std::getline(ls >> std::ws, e.bone);
      c.ragdoll.push_back(e);
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
  const std::string path = clip_redirect(path_in);  // (round 407: a Unity clip's edits live in its Blendity copy)
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
  std::string why;
  const bool unity = is_unity_anim_text(text);  // (round 407: a Unity .anim as a clip of its own)
  if (unity ? !unity_anim_to_clip(text, *c, nullptr, &why) : !load_clip_text(text, *c)) {
    missing[path] = now_seconds();  // (review 407: a file that isn't a clip isn't parsed again every frame)
    return nullptr;
  }
  c->asset_path = path;
  if (!unity || c->name.empty()) c->name = fs::stem(path);
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

/* Round 407: a clip read from a Unity .anim is not written back over Unity's file (what Blendity can't carry would go
 * with it): its edits are saved as Blendity's own clip beside it, "<name> (Blendity).anim", and the clip is that file
 * from then on. The Unity file stays as the project had it; `export anim` writes a Unity file on purpose. */
std::string unity_clip_sibling(const std::string &path) {
  const std::string dir = fs::parent(path), stem = fs::stem(path);
  std::string out = (dir.empty() ? std::string() : dir + "/") + stem + " (Blendity).anim";
  for (int k = 2; fs::exists(resolve_asset_path(out)); k++) out = (dir.empty() ? std::string() : dir + "/") + stem + strprintf(" (Blendity %d).anim", k);
  return out;
}

/* Round 407 (review): where a Unity clip's edits went - clip_asset follows it, so a component, a sequencer card or an
 * undo text that still names the Unity file finds the edited clip; the editor rewrites the components' paths at its
 * next commit (clip_redirect). */
std::unordered_map<std::string, std::string> &clip_redirects() {
  static std::unordered_map<std::string, std::string> r;
  return r;
}

std::string clip_redirect(const std::string &path_in) {
  std::string path = clip_key(path_in);
  for (int hops = 0; hops < 16; hops++) {
    const auto it = clip_redirects().find(path);
    if (it == clip_redirects().end()) break;
    path = it->second;
  }
  return path;
}

size_t save_dirty_clip_assets() {
  size_t n = 0;
  std::vector<std::pair<std::string, std::string>> moved;  // (a Unity clip's key -> its Blendity copy's)
  for (auto &kv : clip_library()) {
    ClipEntry &e = kv.second;
    if (!e.clip || e.clip->version == e.saved_version) continue;
    std::string path = kv.first;
    if (e.clip->unity_yaml) {
      path = unity_clip_sibling(kv.first);
      e.clip->unity_yaml = false, e.clip->asset_path = path;
      moved.push_back({kv.first, path});
    }
    if (!fs::write_file(resolve_asset_path(path), save_clip_text(*e.clip))) continue;
    e.saved_version = e.clip->version;
    n++;
  }
  for (const auto &[from, to] : moved) {
    clip_library()[to] = clip_library()[from];
    clip_library().erase(from);
    clip_redirects()[from] = clip_key(to);
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

float best_frame_rate(const std::vector<float> &times) {
  float best = 30.0f, best_err = 1e30f;
  for (float rate : {24.0f, 25.0f, 30.0f, 50.0f, 60.0f, 120.0f}) {
    float err = 0.0f;
    for (float t : times) err += std::fabs(t * rate - std::round(t * rate));
    err = times.empty() ? 1.0f : err / (float)times.size();
    if (err < best_err - 1e-4f) best_err = err, best = rate;
  }
  return best_err < 0.05f ? best : 30.0f;
}

void reduce_keys(std::vector<AnimKey> &keys, float tolerance) {
  if (keys.size() < 3) return;
  std::vector<AnimKey> kept{keys[0]};
  for (size_t i = 1; i + 1 < keys.size(); i++) {
    const AnimKey &a = kept.back(), &b = keys[i + 1], &k = keys[i];
    const float span = b.frame - a.frame;
    const float on_line = span > 1e-6f ? a.value + (b.value - a.value) * (k.frame - a.frame) / span : a.value;
    if (std::fabs(k.value - on_line) > tolerance) kept.push_back(k);
  }
  kept.push_back(keys.back());
  keys = std::move(kept);
}

Vec3 euler_near(Quat q, Vec3 near) {
  const Vec3 a = q.to_euler(), b(180.0f - a.x, a.y + 180.0f, a.z + 180.0f);
  auto fold = [&](Vec3 e) {
    for (int k = 0; k < 3; k++) e[k] += 360.0f * std::round((near[k] - e[k]) / 360.0f);
    return e;
  };
  const Vec3 fa = fold(a), fb = fold(b);
  return length(fa - near) <= length(fb - near) ? fa : fb;
}

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
  bool stepped = false;  // (round 335: a switch, a whole number or a list - keyed Constant)
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
      found = stepped = true;
    }
  }
  void field(const char *n, bool &v) override {
    if (!hit(n)) return;
    if (write) v = value >= 0.5f;
    else value = v ? 1.0f : 0.0f;
    found = stepped = true;
  }
  void field(const char *n, Vec3 &v) override {
    if (hit(n)) take(v[std::max(0, std::min(2, axis))]);
  }
  void color(const char *n, Vec3 &v) override { field(n, v); }
  void enumeration(const char *n, int &v, const char *const *, int count) override {
    if (!hit(n)) return;
    if (write) v = std::max(0, std::min(count - 1, (int)std::lround(value)));
    else value = (float)v;
    found = stepped = true;
  }
  void text(const char *, std::string &) override {}
  void mesh(const char *, MeshPtr &) override {}
};

bool component_value(GameObject &g, const std::string &property, int axis, bool write, float &value, bool *stepped = nullptr) {
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
    if (stepped) *stepped = fa.stepped;
    return true;
  }
  return false;
}

/* Round 335: "Material <slot>.<Field>" - a field of the material in that slot of the object's MeshRenderer (Base
 * Color, Roughness, Emission Strength, a fog's density...). The slot's material itself: one shared by other objects
 * changes on them too, as editing it in the Inspector does. */
constexpr size_t kMaterialPrefix = 9;  // "Material "

/* The material a "Material <slot>." property names on g (its slot a whole number, in range), and the field's name. */
Material *material_of(GameObject &g, const std::string &property, std::string &field) {
  if (property.compare(0, kMaterialPrefix, "Material ") != 0) return nullptr;
  const size_t dot = property.find('.', kMaterialPrefix);
  if (dot == std::string::npos || dot == kMaterialPrefix) return nullptr;
  const std::string digits = property.substr(kMaterialPrefix, dot - kMaterialPrefix);
  if (digits.find_first_not_of("0123456789") != std::string::npos || digits.size() > 6) return nullptr;
  const size_t slot = (size_t)std::atoi(digits.c_str());
  MeshRenderer *mr = g.mesh_renderer();
  if (!mr || slot >= mr->materials.size() || !mr->materials[slot]) return nullptr;
  field = property.substr(dot + 1);
  return mr->materials[slot].get();
}

/* One field of a reflected thing, read or written (the stepped flag: a switch, whole number or list). */
template<class T> bool access_field(T &thing, const std::string &field, int axis, bool write, float &value, bool *stepped = nullptr) {
  FieldAccess fa;
  fa.want = field, fa.axis = axis, fa.write = write, fa.value = value;
  thing.reflect(fa);
  if (!fa.found) return false;
  value = fa.value;
  if (stepped) *stepped = fa.stepped;
  return true;
}

/* Round 335: "Material <slot>.<Field>" - a field of the material in that slot of the object's MeshRenderer (Base
 * Color, Roughness, Emission Strength, a fog's density...). The slot's material itself: one shared by other objects
 * changes on them too, as editing it in the Inspector does. Touched only when the value changes (a held key every
 * frame of Play would otherwise be a new picture every frame). */
bool material_value(GameObject &g, const std::string &property, int axis, bool write, float &value) {
  std::string field;
  Material *m = material_of(g, property, field);
  if (!m) return false;
  if (!write) return access_field(*m, field, axis, false, value);
  float before = 0.0f, after = value;
  if (!access_field(*m, field, axis, false, before) || !access_field(*m, field, axis, true, after)) return false;
  float now = 0.0f;
  access_field(*m, field, axis, false, now);
  if (now != before) m->touch();  // (the views and the path tracer see a new version)
  return true;
}

}  // namespace

bool anim_value_stepped(GameObject &g, const std::string &property) {
  if (property == "Active") return true;
  float v = 0.0f;
  bool stepped = false;
  std::string field;
  if (Material *m = material_of(g, property, field)) return access_field(*m, field, 0, false, v, &stepped) && stepped;
  return component_value(g, property, 0, false, v, &stepped) && stepped;
}

bool read_anim_value(GameObject &g, const std::string &property, int axis, float &out) {
  const Transform &t = g.local();
  const int a = std::max(0, std::min(2, axis));
  if (property == "Position") return out = t.position[a], true;
  if (property == "Rotation") return out = t.euler_hint[a], true;
  if (property == "Scale") return out = t.scale[a], true;
  if (property == "Active") return out = g.active ? 1.0f : 0.0f, true;  // (round 335: shown or hidden)
  if (material_value(g, property, axis, false, out)) return true;
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
  else if (property == "Active") return g.active = value >= 0.5f, edit_epoch::bump(), true;
  else if (property.compare(0, kMaterialPrefix, "Material ") == 0) return material_value(g, property, axis, true, value);
  else {
    edit_epoch::bump();  // (round 435, ADR 0115: a component's field written past every setter - a light, a shape key, a modifier)
    return component_value(g, property, axis, true, value);
  }
  g.set_local(t);
  return true;
}

bool camera_rig(const GameObject &g) {
  if (g.camera()) return true;
  for (const GameObject *c : g.children)
    if (camera_rig(*c)) return true;
  return false;
}

/* Round 348 (review): an IK the clip turns off (Weight 0) leaves its bones to their own keys; the ones the clip doesn't
 * key go to the rest pose - what an IK off shows whatever frame was shown before (MMD's unkeyed knees, a Weight keyed
 * alone). */
static void rest_unkeyed_fk(const std::vector<GameObject *> &weighted, const std::unordered_set<const GameObject *> &keyed) {
  for (GameObject *g : weighted) {
    const IKConstraint *ik = g->get<IKConstraint>();
    if (!ik || !ik_off(*ik)) continue;
    for (GameObject *b : ik_turned_bones(*g, *ik))
      if (!keyed.count(b) && b->get<Bone>()) b->set_local_rotation(b->get<Bone>()->rest_rotation);
  }
}

/* ---------------------------------------------------------------- apply_clip's bindings (round 472) */

namespace {

constexpr size_t kBlendPrefix = 24;  // "MeshRenderer.BlendShape/"
constexpr size_t kBindingsPerClip = 32;  // (roots one clip keeps bindings for: a crowd past it builds them as it did before)

std::atomic<uint64_t> g_binding_builds{0};
std::mutex &binding_mutex() {
  static std::mutex m;
  return m;
}

enum class BindKind : uint8_t { Position, Rotation, Scale, Other, BlendShape };

}  // namespace

/* One track's target: the object it writes, and how. */
struct TrackBinding {
  GameObject *g = nullptr;
  uint32_t track = 0;  // index in the clip's tracks
  uint32_t slot = 0;   // a transform track: the object's place among the pose's (shape: the shape key's index)
  BindKind kind = BindKind::Other;
  uint8_t axis = 0;
  bool weighted = false;  // IKConstraint.Weight (the constraints it turns off are rested after the pose)
};

struct ClipBindings {
  std::vector<TrackBinding> tracks;  // in the clip's order
  std::vector<GameObject *> slots;   // the objects a transform track sets, in the order the tracks first name them
  uint64_t names = 0;  // every object under the root - which, hung from what, called what (a name written past the stamp is caught here)
};

/* What a search of `root`'s branches by name depends on: each object below it, its parent and its name, in the walk's
 * order (the first child of a name is the one a path finds). A few nanoseconds an object - against the string map the
 * bindings replaced, built per call. */
static uint64_t names_below(const GameObject &root) {
  uint64_t h = 1469598103934665603ull;
  auto mix = [&h](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
  std::vector<const GameObject *> stack{&root};
  while (!stack.empty()) {
    const GameObject *g = stack.back();
    stack.pop_back();
    mix((uint64_t)(uintptr_t)g), mix((uint64_t)(uintptr_t)g->parent), mix(std::hash<std::string>()(g->name));
    for (size_t i = g->children.size(); i-- > 0;) stack.push_back(g->children[i]);
  }
  return h;
}

uint64_t clip_binding_builds() { return g_binding_builds.load(std::memory_order_relaxed); }

namespace {

/* The objects the tracks name, found once and each track tied to its property (the work apply_clip did on every call
 * before round 472). Task 0206: with many tracks, the objects found in one walk of the branches the tracks name
 * (searching a parent's children by name for each track was half a million name compares a frame under a thousand
 * children). It follows what anim_target follows - the first child of a name - and leaves to it the paths a walk can't
 * spell (a name empty or with a '/'). */
std::shared_ptr<const ClipBindings> bind_clip(const AnimationClip &clip, GameObject &root) {
  g_binding_builds.fetch_add(1, std::memory_order_relaxed);
  auto out = std::make_shared<ClipBindings>();
  std::unordered_map<std::string, GameObject *> found;
  if (clip.tracks.size() > 64) {
    std::unordered_set<std::string> named;  // every track's path and the paths above it
    for (const AnimTrack &t : clip.tracks)
      for (size_t e = t.path.find('/');; e = t.path.find('/', e + 1)) {
        named.insert(t.path.substr(0, e));
        if (e == std::string::npos) break;
      }
    std::vector<std::pair<GameObject *, std::string>> stack{{&root, std::string()}};
    std::unordered_set<std::string> seen;
    while (!stack.empty()) {
      auto [g, path] = std::move(stack.back());
      stack.pop_back();
      seen.clear();
      for (GameObject *c : g->children) {
        if (!seen.insert(c->name).second || c->name.empty() || c->name.find('/') != std::string::npos) continue;  // (child_named finds only the first)
        std::string p = g == &root ? c->name : path + "/" + c->name;
        if (!named.count(p)) continue;
        found.emplace(p, c);
        stack.push_back({c, std::move(p)});
      }
    }
  }
  std::unordered_map<GameObject *, uint32_t> slot_of;
  out->tracks.reserve(clip.tracks.size());
  for (size_t i = 0; i < clip.tracks.size(); i++) {
    const AnimTrack &t = clip.tracks[i];
    auto it = found.find(t.path);
    if (it == found.end()) it = found.emplace(t.path, anim_target(root, t.path)).first;
    TrackBinding b;
    b.g = it->second;
    b.track = (uint32_t)i;
    b.axis = (uint8_t)std::max(0, std::min(2, t.axis));
    if (b.g && is_transform_property(t.property)) {
      b.kind = t.property == "Position" ? BindKind::Position : t.property == "Scale" ? BindKind::Scale : BindKind::Rotation;
      auto s = slot_of.find(b.g);
      if (s == slot_of.end()) s = slot_of.emplace(b.g, (uint32_t)out->slots.size()).first, out->slots.push_back(b.g);
      b.slot = s->second;
    }
    else if (b.g && t.property.size() > kBlendPrefix && t.property.compare(0, kBlendPrefix, "MeshRenderer.BlendShape/") == 0) {
      const MeshFilter *mf = b.g->mesh_filter();
      const size_t n = mf && mf->mesh ? mf->mesh->shape_keys.size() : 0;
      for (size_t k = 0; k < n; k++)  // (the first key of that name: what the reflection's walk finds first)
        if (t.property.compare(kBlendPrefix, std::string::npos, mf->mesh->shape_keys[k].name) == 0) {
          b.kind = BindKind::BlendShape, b.slot = (uint32_t)k;
          break;
        }
    }
    else b.weighted = b.g && t.property == "IKConstraint.Weight";
    out->tracks.push_back(b);
  }
  out->names = names_below(root);
  return out;
}

/* Every bound object still has the name and parent it was bound with. */
bool bindings_hold(const ClipBindings &set, const GameObject &root) { return set.names == names_below(root); }

/* The bindings of `clip` under `root`: kept while the clip's layout, its number of tracks and the scene's structure stamp
 * hold (the stamp bumps for an object made, destroyed, reparented or renamed). Not kept past kBindingsPerClip roots: the
 * least recently used one goes. */
/* (What the applies themselves added to the structure stamp: an Active or a component's field written bumps it, and
 *  that is no object made, destroyed, reparented or renamed. Taken off the stamp the entries keep, so one root's apply
 *  does not make every other root's bindings look stale - two animated characters in Play rebuilt each other's on
 *  every frame before this. The main thread only, as every apply_clip is.) */
uint64_t &applies_own_bumps() {
  static uint64_t own = 0;
  return own;
}

std::shared_ptr<const ClipBindings> clip_bindings(const AnimationClip &clip, GameObject &root, uint64_t &stamp) {
  static uint64_t tick = 0;
  stamp = edit_epoch::structure() - applies_own_bumps();
  {
    std::lock_guard<std::mutex> lock(binding_mutex());
    for (ClipBindingEntry &e : clip.bindings.entries)
      if (e.root == &root && e.root_id == root.id && e.layout == clip.layout && e.tracks == clip.tracks.size() && e.stamp == stamp && bindings_hold(*e.set, root)) {
        e.used = ++tick;
        return e.set;
      }
  }
  std::shared_ptr<const ClipBindings> set = bind_clip(clip, root);
  std::lock_guard<std::mutex> lock(binding_mutex());
  ClipBindingEntry *slot = nullptr;
  for (ClipBindingEntry &e : clip.bindings.entries)
    if (e.root == &root) slot = &e;
  if (!slot && clip.bindings.entries.size() < kBindingsPerClip) slot = &clip.bindings.entries.emplace_back();
  if (!slot) {
    slot = &clip.bindings.entries.front();
    for (ClipBindingEntry &e : clip.bindings.entries)
      if (e.used < slot->used) slot = &e;
  }
  slot->root = &root, slot->root_id = root.id, slot->layout = clip.layout, slot->tracks = clip.tracks.size();
  slot->stamp = stamp, slot->used = ++tick, slot->set = set;
  return set;
}

/* The apply's own writes (an Active, a component's field) bump the structure stamp: counted as its own, so only what
 * happens between applies - to this root or any other - makes a binding look again. */
void bindings_applied(uint64_t structure_before) { applies_own_bumps() += edit_epoch::structure() - structure_before; }

/* A shape key's weight written the way write_anim_value's reflection would, without walking every field of the
 * MeshRenderer for each key: false when the mesh no longer has that key at that index (the caller writes the long way). */
bool write_blend_shape(GameObject &g, const AnimTrack &t, size_t shape, float v) {
  const MeshFilter *mf = g.mesh_filter();
  MeshRenderer *mr = g.get<MeshRenderer>();
  if (!mf || !mf->mesh || !mr || shape >= mf->mesh->shape_keys.size() || std::strcmp(mr->type_name(), "MeshRenderer") != 0) return false;
  const std::string &name = mf->mesh->shape_keys[shape].name;
  if (t.property.size() != kBlendPrefix + name.size() || t.property.compare(kBlendPrefix, std::string::npos, name) != 0) return false;
  edit_epoch::bump();  // (as write_anim_value: a component's field written past every setter)
  if (mr->shape_weight(name) != v) mr->set_shape_weight(name, v);
  return true;
}

}  // namespace

void apply_clip(const AnimationClip &clip, GameObject &root, float frame, int hold, bool hold_cameras, float hold_from) {
  BL_ZONE("apply clip");
  PoseTimer timer(PoseStat::ClipApply);  // (round 468)
  /* (Round 340: held from `hold_from` - a render range's first frame is a drawn one; the camera and its rig not held.) */
  const float held = hold_frame_from(hold_from, frame, hold);
  std::unordered_map<const GameObject *, bool> rigs;  // (each object asked once, not once a track)
  auto smooth = [&](const GameObject *g) {
    if (hold <= 1 || hold_cameras) return false;
    auto r = rigs.find(g);
    if (r == rigs.end()) r = rigs.emplace(g, camera_rig(*g)).first;
    return r->second;
  };
  /* Each object found once and its transform set once, however many of its numbers are animated. Round 472: the tracks'
   * objects and properties are bound once per (clip layout, root, structure stamp), not on every call. */
  uint64_t stamp = 0;
  const std::shared_ptr<const ClipBindings> set = clip_bindings(clip, root, stamp);
  const uint64_t structure_before = edit_epoch::structure();
  const ClipBindings &bound = *set;
  std::vector<Transform> poses(bound.slots.size());
  std::vector<char> posed(bound.slots.size(), 0);
  std::vector<GameObject *> weighted;  // (IKs whose Weight the clip sets)
  for (const TrackBinding &b : bound.tracks) {
    const AnimTrack &t = clip.tracks[b.track];
    if (t.keys.empty()) continue;
    GameObject *g = b.g;
    if (!g) continue;  // (renamed or gone: the window shows the track as missing)
    const float v = t.sample(smooth(g) ? frame : held);  // (round 340: a camera and its rig move on every frame)
    if (!std::isfinite(v)) continue;
    if (b.kind == BindKind::BlendShape && write_blend_shape(*g, t, b.slot, v)) continue;
    if (b.kind == BindKind::Other || b.kind == BindKind::BlendShape) {
      write_anim_value(*g, t.property, t.axis, v);
      if (b.weighted) weighted.push_back(g);
      continue;
    }
    Transform &p = poses[b.slot];
    if (!posed[b.slot]) p = g->local(), posed[b.slot] = 1;
    if (b.kind == BindKind::Position) p.position[b.axis] = v;
    else if (b.kind == BindKind::Scale) p.scale[b.axis] = v;
    else p.euler_hint[b.axis] = v;
  }
  for (size_t i = 0; i < poses.size(); i++) {
    if (!posed[i]) continue;
    poses[i].rotation = Quat::euler(poses[i].euler_hint);
    bound.slots[i]->set_local(poses[i]);
  }
  if (!weighted.empty()) {
    std::unordered_set<const GameObject *> keyed;
    for (size_t i = 0; i < poses.size(); i++)
      if (posed[i]) keyed.insert(bound.slots[i]);
    rest_unkeyed_fk(weighted, keyed);
  }
  bindings_applied(structure_before);
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
  static const char *kHold[] = {"1s", "2s", "3s", "4s"};
  int h = std::max(0, std::min(3, hold - 1));
  r.enumeration("Hold", h, kHold, 4);
  r.help("Animation on 2s or 3s (task 0149): the pose changes every 2 or 3 frames of the clip, as hand-drawn animation is\n"
         "shot, while the camera and the rest of the scene move every frame. Renders hold the same frames; so do the\n"
         "Animation window's Preview and its ruler's marks (the frames a new pose is drawn on).");
  hold = h + 1;
  r.field("Hold the Camera", hold_camera);
  r.help("A camera this clip animates (or the rig carrying one) is held too. Off (anime's way): the characters move on 2s\n"
         "or 3s, the camera's pan and push-in every frame.");
}

void Animation::start(PlayContext &) {
  time = 0.0f;
  playing = play_automatically;
}

void Animation::update(PlayContext &ctx) {
  if (!playing || !owner) return;
  if (const Sequencer *s = owner->get<Sequencer>(); s && s->enabled && sequence_asset(s->sequence)) return;  // (a sequence plays the clips: ADR 0030)
  if (ctx.scene && ctx.scene->director && scene_sequence_drives(*ctx.scene, *owner)) return;  // (the scene's sequence has it, task 0070)
  const AnimationClipPtr c = clip_asset(clip);
  if (!c) return;
  time += ctx.dt * speed;
  const int w = wrap == 0 && !c->loop ? 1 : wrap;  // a clip that doesn't loop holds its end under Loop
  apply_clip(*c, *owner, wrap_frame(time * c->frame_rate, c->end_frame(), w), hold, hold_camera);
}

}  // namespace bl

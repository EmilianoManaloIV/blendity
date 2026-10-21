// SPDX-License-Identifier: GPL-2.0-or-later
// Sequences of animation clips (task 0062, ADR 0030): Premiere's timeline for clips. Two instances overlapping on a
// track crossfade linearly over the overlap (a cross dissolve); a higher track's numbers replace the lower ones'.
// Triggers jump the playhead in Play and blend from the pose of the moment.
#include "sequence.h"

#include "material.h"

#include "../core/core.h"
#include "../platform/platform.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace bl {

/* ---------------------------------------------------------------- the timeline */

float SeqInstance::clip_out(const AnimationClip *c) const {
  const float end = c ? c->end_frame() : 60.0f;
  return trim_out >= 0.0f ? std::min(trim_out, end) : end;
}

float SeqInstance::duration(const AnimationClip *c) const {
  const float s = std::fabs(speed) > 1e-3f ? std::fabs(speed) : 1e-3f;
  return std::max(1.0f, (clip_out(c) - trim_in) / s);
}

float SeqInstance::clip_frame(float seq_frame) const { return trim_in + (seq_frame - start) * std::fabs(speed); }

void SeqTrack::sort() {
  std::stable_sort(items.begin(), items.end(), [](const SeqInstance &a, const SeqInstance &b) { return a.start < b.start; });
}

float Sequence::end_frame() const {
  float end = 0.0f;
  for (const SeqTrack &t : tracks)
    for (const SeqInstance &i : t.items) end = std::max(end, i.start + i.duration(clip_asset(i.clip).get()));
  return end;
}

SeqInstance *Sequence::find(const std::string &n, int *track, int *index) {
  for (size_t t = 0; t < tracks.size(); t++)
    for (size_t i = 0; i < tracks[t].items.size(); i++) {
      if (tracks[t].items[i].name != n) continue;
      if (track) *track = (int)t;
      if (index) *index = (int)i;
      return &tracks[t].items[i];
    }
  return nullptr;
}

const SeqInstance *Sequence::find(const std::string &n) const { return const_cast<Sequence *>(this)->find(n); }

std::string Sequence::unique_name(const std::string &base) const {
  const std::string b = base.empty() ? "Clip" : base;
  if (!find(b)) return b;
  for (int n = 2;; n++) {
    const std::string name = b + " " + std::to_string(n);
    if (!find(name)) return name;
  }
}

/* ---------------------------------------------------------------- the file */

/* blendity_seq 1 / name / rate, then "track <name>" lines each followed by its
 * "clip <start> <in> <out> <speed><TAB><name><TAB><clip path>" lines, and "trigger <blend><TAB><key><TAB><instance>"
 * lines (tabs: names hold spaces). */
std::string save_sequence_text(const Sequence &s) {
  std::ostringstream os;
  os.precision(9);
  os << "blendity_seq 1\nname " << s.name << "\nrate " << s.frame_rate << "\n";
  for (const SeqTrack &t : s.tracks) {
    os << "track " << t.name << "\n";
    for (const SeqInstance &i : t.items)
      os << "clip " << i.start << " " << i.trim_in << " " << i.trim_out << " " << i.speed << "\t" << i.name << "\t" << i.clip << (i.target.empty() ? "" : "\t" + i.target) << "\n";
  }
  for (const SeqTrigger &g : s.triggers) os << "trigger " << g.blend << "\t" << g.key << "\t" << g.instance << "\n";
  return os.str();
}

namespace {

std::vector<std::string> tab_fields(const std::string &line) {
  std::vector<std::string> f;
  size_t s = 0;
  for (size_t e = line.find('\t');; e = line.find('\t', s)) {
    f.push_back(line.substr(s, e == std::string::npos ? std::string::npos : e - s));
    if (e == std::string::npos) break;
    s = e + 1;
  }
  return f;
}

bool finite_all(std::initializer_list<float> v) {
  return std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x); });
}

}  // namespace

bool load_sequence_text(const std::string &text, Sequence &s) {
  std::istringstream is(text);
  std::string line;
  if (!std::getline(is, line) || line.rfind("blendity_seq", 0) != 0) return false;
  s.tracks.clear(), s.triggers.clear();
  while (std::getline(is, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::istringstream ls(line);
    std::string word;
    ls >> word;
    if (word == "name") std::getline(ls >> std::ws, s.name);
    else if (word == "rate") ls >> s.frame_rate;
    else if (word == "track") {
      s.tracks.emplace_back();
      std::getline(ls >> std::ws, s.tracks.back().name);
    }
    else if (word == "clip" && !s.tracks.empty()) {
      const std::vector<std::string> f = tab_fields(line);
      SeqInstance i;
      std::istringstream nums(f[0].substr(4));
      if (f.size() < 3 || !(nums >> i.start >> i.trim_in >> i.trim_out >> i.speed)) continue;
      if (!finite_all({i.start, i.trim_in, i.trim_out, i.speed}) || f[2].empty()) continue;  // (a bad line: no instance)
      i.start = std::max(0.0f, i.start), i.trim_in = std::max(0.0f, i.trim_in);
      i.speed = std::fabs(i.speed) < 1e-3f ? 1.0f : std::min(10.0f, std::fabs(i.speed));  // (no reverse play: forwards)
      i.name = f[1], i.clip = f[2];
      if (f.size() > 3) i.target = f[3];  // (task 0070; older files: none)
      s.tracks.back().items.push_back(std::move(i));
    }
    else if (word == "trigger") {
      const std::vector<std::string> f = tab_fields(line);
      SeqTrigger g;
      std::istringstream nums(f[0].substr(7));
      if (f.size() < 3 || !(nums >> g.blend) || !std::isfinite(g.blend)) continue;
      g.blend = std::max(0.0f, std::min(10.0f, g.blend)), g.key = f[1], g.instance = f[2];
      s.triggers.push_back(std::move(g));
    }
  }
  if (!(s.frame_rate > 0.0f) || !std::isfinite(s.frame_rate)) s.frame_rate = 60.0f;
  for (SeqTrack &t : s.tracks) t.sort();
  s.touch();
  return true;
}

/* ---------------------------------------------------------------- the assets */

namespace {

struct SeqEntry {
  SequencePtr seq;
  uint64_t saved_version = 0;
};
std::unordered_map<std::string, SeqEntry> &seq_library() {
  static std::unordered_map<std::string, SeqEntry> lib;
  return lib;
}
std::string seq_key(std::string p) {
  std::replace(p.begin(), p.end(), '\\', '/');
  return p;
}
bool path_under(const std::string &p, const std::string &dir) {
  return p == dir || (p.size() > dir.size() && p.compare(0, dir.size(), dir) == 0 && p[dir.size()] == '/');
}

}  // namespace

SequencePtr sequence_asset(const std::string &path_in) {
  if (path_in.empty()) return nullptr;
  const std::string path = seq_key(path_in);
  auto &lib = seq_library();
  if (auto it = lib.find(path); it != lib.end()) return it->second.seq;
  static std::unordered_map<std::string, double> missing;  // (looked for again after a second, not every frame)
  if (auto m = missing.find(path); m != missing.end() && now_seconds() - m->second < 1.0) return nullptr;
  std::string text;
  if (!fs::read_file(resolve_asset_path(path), text)) {
    missing[path] = now_seconds();
    return nullptr;
  }
  missing.erase(path);
  auto s = std::make_shared<Sequence>();
  if (!load_sequence_text(text, *s)) {
    missing[path] = now_seconds();  // (not a sequence: not read again every frame)
    return nullptr;
  }
  s->asset_path = path;
  s->name = fs::stem(path);
  lib[path] = {s, s->version};
  return s;
}

SequencePtr create_sequence_asset(const std::string &path_in, const std::string &name) {
  const std::string path = seq_key(path_in);
  auto s = std::make_shared<Sequence>();
  s->name = name.empty() ? fs::stem(path) : name;
  s->asset_path = path;
  s->tracks.resize(2);
  s->tracks[0].name = "Track 1", s->tracks[1].name = "Track 2";
  const std::string file = resolve_asset_path(path);
  fs::make_dirs(fs::parent(file));
  if (!fs::write_file(file, save_sequence_text(*s))) return nullptr;
  seq_library()[path] = {s, s->version};
  return s;
}

size_t save_dirty_sequence_assets() {
  size_t n = 0;
  for (auto &kv : seq_library()) {
    SeqEntry &e = kv.second;
    if (!e.seq || e.seq->version == e.saved_version) continue;
    if (!fs::write_file(resolve_asset_path(kv.first), save_sequence_text(*e.seq))) continue;
    e.saved_version = e.seq->version;
    n++;
  }
  return n;
}

size_t retarget_sequence_assets(const std::string &from_in, const std::string &to_in) {
  const std::string from = seq_key(from_in), to = seq_key(to_in);
  auto &lib = seq_library();
  std::vector<std::pair<std::string, SeqEntry>> moved;
  for (auto it = lib.begin(); it != lib.end();) {
    if (!path_under(it->first, from)) {
      ++it;
      continue;
    }
    moved.push_back({to + it->first.substr(from.size()), it->second});
    it = lib.erase(it);
  }
  for (auto &kv : moved) {
    if (kv.second.seq) kv.second.seq->asset_path = kv.first, kv.second.seq->name = fs::stem(kv.first);
    lib[kv.first] = kv.second;
  }
  return moved.size();
}

size_t forget_sequence_assets(const std::string &path_in) {
  const std::string path = seq_key(path_in);
  auto &lib = seq_library();
  size_t n = 0;
  for (auto it = lib.begin(); it != lib.end();) {
    if (!path_under(it->first, path)) {
      ++it;
      continue;
    }
    it = lib.erase(it);
    n++;
  }
  return n;
}

void clear_sequence_assets() { seq_library().clear(); }

size_t retarget_sequence_clips(const std::string &from_in, const std::string &to_in) {
  const std::string from = seq_key(from_in), to = seq_key(to_in);
  size_t n = 0;
  for (auto &kv : seq_library()) {
    if (!kv.second.seq) continue;
    bool changed = false;
    for (SeqTrack &t : kv.second.seq->tracks)
      for (SeqInstance &i : t.items) {
        std::string p = seq_key(i.clip);
        if (!path_under(p, from)) continue;
        i.clip = to + p.substr(from.size());
        changed = true, n++;
      }
    if (changed) kv.second.seq->touch();
  }
  return n;
}

/* ---------------------------------------------------------------- sampling */

/* FNV-1a over the path, a separator, the property and the axis (a 64-bit clash between two numbers of one character is
 * not a worry). */
uint64_t anim_value_key(const std::string &path, const std::string &property, int axis) {
  uint64_t h = 14695981039346656037ull;
  auto mix = [&](unsigned char c) { h = (h ^ c) * 1099511628211ull; };
  for (char c : path) mix((unsigned char)c);
  mix(0x1f);
  for (char c : property) mix((unsigned char)c);
  mix(0x1f);
  mix((unsigned char)('0' + axis));
  return h ? h : 1;  // (0 is no key)
}

void AnimValues::clear() {
  values.clear();
  std::fill(slots_.begin(), slots_.end(), 0u);
}

int AnimValues::find(uint64_t key) const {
  const size_t mask = slots_.size() - 1;
  for (size_t i = (size_t)(key ^ (key >> 29)) & mask;; i = (i + 1) & mask)
    if (!slots_[i] || values[slots_[i] - 1].key == key) return (int)i;
}

void AnimValues::set_key(uint64_t key, const AnimTrack *t, float value, GameObject *target) {
  if (slots_.size() < 2 * (values.size() + 1)) {  // (at most half full: the probes stay short)
    slots_.assign(std::max<size_t>(64, slots_.size() * 2), 0u);
    for (size_t i = 0; i < values.size(); i++) slots_[(size_t)find(values[i].key)] = (uint32_t)i + 1;
  }
  const int s = find(key);
  if (slots_[(size_t)s]) return (void)(values[slots_[(size_t)s] - 1].value = value);
  values.push_back({key, t, value, target});
  slots_[(size_t)s] = (uint32_t)values.size();
}

void AnimValues::set(const AnimTrack &t, float value) { set_key(anim_value_key(t.path, t.property, t.axis), &t, value); }

const float *AnimValues::get(uint64_t key) const {
  if (slots_.empty()) return nullptr;
  const int s = find(key);
  return slots_[(size_t)s] ? &values[slots_[(size_t)s] - 1].value : nullptr;
}

const float *AnimValues::get(const std::string &path, const std::string &property, int axis) const {
  return get(anim_value_key(path, property, axis));
}

float blend_anim_value(const std::string &property, float a, float b, float t) {
  if (property == "Rotation") b = a + (b - a - 360.0f * std::round((b - a) / 360.0f));  // (the shorter way round)
  return a + (b - a) * t;
}

void sample_clip_values(const AnimationClip &clip, float frame, AnimValues &out) {
  for (const AnimTrack &t : clip.tracks) {
    if (t.keys.empty()) continue;
    const float v = t.sample(frame);
    if (std::isfinite(v)) out.set(t, v);
  }
}

namespace {

/* One instance's numbers at a sequence frame (its clip, trimmed, at its speed; a missing clip gives nothing), keyed by
 * its target too: the same bone path on two characters is two numbers. */
void sample_instance(const SeqInstance &i, float frame, AnimValues &out, const Scene *scene) {
  const AnimationClipPtr c = clip_asset(i.clip);
  if (!c) return;
  GameObject *target = scene && !i.target.empty() ? scene->find_by_name(i.target) : nullptr;
  if (scene && !i.target.empty() && !target) return;  // (its object is gone: nothing to move)
  const uint64_t salt = target ? target->id * 0x9E3779B97F4A7C15ull : 0;
  const float f = std::max(i.trim_in, std::min(i.clip_out(c.get()), i.clip_frame(frame)));
  for (const AnimTrack &t : c->tracks) {
    if (t.keys.empty()) continue;
    const float v = t.sample(f);
    if (std::isfinite(v)) out.set_key(anim_value_key(t.path, t.property, t.axis) ^ salt, &t, v, target);
  }
}


/* The bars of a track covering `frame`, per object: for each object, at most the last two that start before it (a cross
 * dissolve); bars on different objects play side by side. */
void covering(const SeqTrack &t, float frame, std::vector<std::pair<const SeqInstance *, const SeqInstance *>> &out) {
  out.clear();
  for (const SeqInstance &i : t.items) {
    if (i.start > frame) break;
    if (frame >= i.start + i.duration(clip_asset(i.clip).get())) continue;
    auto it = std::find_if(out.begin(), out.end(), [&](const auto &p) { return p.second->target == i.target; });
    if (it == out.end()) out.push_back({nullptr, &i});
    else it->first = it->second, it->second = &i;
  }
}

}  // namespace

static void sample_pair(const SeqInstance *a, const SeqInstance *b, float frame, AnimValues &out, const Scene *scene, AnimValues &va, AnimValues &vb);

void sample_sequence(const Sequence &s, float frame, AnimValues &out, const Scene *scene) {
  static thread_local AnimValues va, vb;  // (kept warm: no allocation a frame)
  static thread_local std::vector<std::pair<const SeqInstance *, const SeqInstance *>> pairs;
  for (const SeqTrack &t : s.tracks) {
    covering(t, frame, pairs);
    for (const auto &[a, b] : pairs) sample_pair(a, b, frame, out, scene, va, vb);
  }
}

/* One object's bar at `frame` on a track, or two crossfading. */
static void sample_pair(const SeqInstance *a, const SeqInstance *b, float frame, AnimValues &out, const Scene *scene, AnimValues &va, AnimValues &vb) {
  {
    vb.clear();
    sample_instance(*b, frame, vb, scene);
    va.clear();
    if (a) sample_instance(*a, frame, va, scene);
    if (!a) {
      for (const AnimValue &v : vb.values) out.set_key(v.key, v.track, v.value, v.target);
      return;
    }
    /* The overlap: from b's start to a's end, b's weight rising from 0 to 1. A number only one of them has is its. */
    const float a_end = a->start + a->duration(clip_asset(a->clip).get()), b_end = b->start + b->duration(clip_asset(b->clip).get());
    const float w = std::max(0.0f, std::min(1.0f, (frame - b->start) / std::max(1e-3f, std::min(a_end, b_end) - b->start)));
    for (const AnimValue &v : va.values) {
      const float *other = vb.get(v.key);
      out.set_key(v.key, v.track, other ? blend_anim_value(v.track->property, v.value, *other, w) : v.value, v.target);
    }
    for (const AnimValue &v : vb.values)
      if (!va.get(v.key)) out.set_key(v.key, v.track, v.value, v.target);
  }
}

namespace {
struct PathFrom {  // a track's path string and the object it starts from
  const std::string *path;
  GameObject *from;
  bool operator==(const PathFrom &o) const { return path == o.path && from == o.from; }
};
struct PathFromHash {
  size_t operator()(const PathFrom &k) const { return std::hash<const void *>()(k.path) * 31u ^ std::hash<const void *>()(k.from); }
};
}  // namespace

void apply_anim_values(const AnimValues &vals, GameObject *root) {
  /* Each object found once: by the track's path string and where it starts (tracks of one object share none, so a few
   * lookups more at worst). */
  std::unordered_map<PathFrom, GameObject *, PathFromHash> found;
  std::unordered_map<GameObject *, Transform> poses;
  for (const AnimValue &v : vals.values) {
    const AnimTrack &t = *v.track;
    GameObject *from = v.target ? v.target : root;
    if (!from) continue;
    const PathFrom key{&t.path, from};
    auto it = found.find(key);
    if (it == found.end()) it = found.emplace(key, anim_target(*from, t.path)).first;
    GameObject *g = it->second;
    if (!g || !std::isfinite(v.value)) continue;
    if (!is_transform_property(t.property)) {
      write_anim_value(*g, t.property, t.axis, v.value);
      continue;
    }
    auto p = poses.find(g);
    if (p == poses.end()) p = poses.emplace(g, g->local()).first;
    const int a = std::max(0, std::min(2, t.axis));
    if (t.property == "Position") p->second.position[a] = v.value;
    else if (t.property == "Scale") p->second.scale[a] = v.value;
    else p->second.euler_hint[a] = v.value;
  }
  for (auto &[g, pose] : poses) {
    pose.rotation = Quat::euler(pose.euler_hint);
    g->set_local(pose);
  }
}

void read_anim_values(const AnimValues &names, GameObject *root, AnimValues &out) {
  out.clear();
  for (const AnimValue &v : names.values) {
    GameObject *from = v.target ? v.target : root;
    GameObject *g = from ? anim_target(*from, v.track->path) : nullptr;
    float now = 0.0f;
    if (g && read_anim_value(*g, v.track->property, v.track->axis, now)) out.set_key(v.key, v.track, now, v.target);
  }
}

bool scene_sequence_drives(const Scene &scene, const GameObject &g) {
  const SequencePtr s = scene.sequence.empty() ? nullptr : sequence_asset(scene.sequence);
  if (!s) return false;
  for (const SeqTrack &t : s->tracks)
    for (const SeqInstance &i : t.items)
      if (i.target == g.name) return true;
  return false;
}

int trigger_key_code(const std::string &name_in) {
  std::string n = name_in;
  for (char &c : n) c = (char)std::toupper((unsigned char)c);
  if (n.size() == 1 && ((n[0] >= 'A' && n[0] <= 'Z') || (n[0] >= '0' && n[0] <= '9'))) return n[0];
  if (n == "SPACE") return platform::KEY_SPACE;
  if (n.size() >= 2 && n.size() <= 3 && n[0] == 'F' && std::all_of(n.begin() + 1, n.end(), [](char c) { return c >= '0' && c <= '9'; })) {
    const int f = std::atoi(n.c_str() + 1);
    if (f >= 1 && f <= 12) return platform::KEY_F1 + f - 1;
  }
  return 0;
}

/* ---------------------------------------------------------------- the component */

void Sequencer::reflect(Reflector &r) {
  r.asset("Sequence", sequence, ".seq", "Sequence");
  r.help("The .seq sequence it plays (Assets/Animations). Lay clips out in the Sequencer window (Window > Sequencer).");
  r.field("Play Automatically", play_automatically);
  r.help("Starts the timeline when Play starts.");
  r.field("Speed", speed, 0.01f, 0.0f, 10.0f);
  static const char *kWrap[] = {"Loop", "Once"};
  r.enumeration("Wrap", wrap, kWrap, 2);
  r.help("At the timeline's end: start again, or hold the last frame. A trigger's key jumps to its clip either way.");
  static const char *kHold[] = {"1s", "2s", "3s", "4s"};
  int h = std::max(0, std::min(3, hold - 1));
  r.enumeration("Hold", h, kHold, 4);
  r.help("Animation on 2s or 3s (task 0149): the timeline's clips change pose every 2 or 3 frames, as hand-drawn\n"
         "animation is shot. Renders hold the same frames.");
  hold = h + 1;
}

void Sequencer::start(PlayContext &ctx) {
  scene_ = ctx.scene;  // (a jump before the first update finds the bars' objects)
  time = 0.0f;
  playing = play_automatically;
  blend_left = blend_total = 0.0f;
  blend_from.clear();
  was_down.clear();
}

float Sequencer::frame() const {
  const SequencePtr s = sequence_asset(sequence);
  if (!s) return 0.0f;
  const float end = s->end_frame();
  return wrap_frame(time * s->frame_rate, wrap == 1 ? std::max(0.0f, end - 1e-3f) : end, wrap == 1 ? 1 : 0);  // (Once: just inside the end, so the last bar still covers it)
}

bool Sequencer::jump(const std::string &instance, float blend) {
  const SequencePtr s = sequence_asset(sequence);
  const SeqInstance *i = s ? s->find(instance) : nullptr;
  if (!i) return false;
  /* From the pose of the moment: the current numbers of what the sequence animates there and at the target. */
  AnimValues names;
  sample_sequence(*s, frame(), names, scene_);
  sample_sequence(*s, i->start, names, scene_);
  read_anim_values(names, owner, blend_from);
  blend_total = blend_left = std::max(0.0f, blend);
  time = i->start / s->frame_rate;
  playing = true;
  return true;
}

void Sequencer::update(PlayContext &ctx) {
  scene_ = ctx.scene;
  const SequencePtr s = sequence_asset(sequence);
  if (!s) return;
  was_down.resize(s->triggers.size(), 0);
  for (size_t k = 0; k < s->triggers.size(); k++) {  // a trigger fires on its key's press
    const int code = trigger_key_code(s->triggers[k].key);
    const bool down = code && ctx.key_down && ctx.key_down(code);
    if (down && !was_down[k]) jump(s->triggers[k].instance, s->triggers[k].blend);
    was_down[k] = down;
  }
  if (!playing) return;
  time += ctx.dt * speed;
  scratch.clear();
  sample_sequence(*s, hold_frame(frame(), hold), scratch, scene_);
  if (blend_left > 0.0f && blend_total > 0.0f) {
    blend_left = std::max(0.0f, blend_left - ctx.dt);
    const float t = 1.0f - blend_left / blend_total;
    for (AnimValue &v : scratch.values)
      if (const float *from = blend_from.get(v.key)) v.value = blend_anim_value(v.track->property, *from, v.value, t);
  }
  apply_anim_values(scratch, owner);
}

}  // namespace bl

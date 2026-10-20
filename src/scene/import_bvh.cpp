// SPDX-License-Identifier: GPL-2.0-or-later
// BVH motion capture (task 0140, ADR 0047): Biovision's text format (HIERARCHY of joints with OFFSETs and CHANNELS,
// then MOTION: a row of channel values a frame). The joints become bones at their offsets, the motion a clip, as a
// model import: the skeleton to pose, the clip under Assets/Animations. BVH is right-handed Y-up like FBX (X mirrored
// here), in whatever unit the capture used: a skeleton taller than 10 units is taken to be in centimetres. A joint's
// position channels replace its OFFSET on those axes (Blender's reading of the format; some tools add them instead).
#include "import.h"

#include "../core/core.h"
#include "../../extern/fast_float/fast_float.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace bl {

namespace {

constexpr size_t kMaxJoints = 4096, kMaxFrames = 200000;

struct BvhJoint {
  std::string name;
  int parent = -1;
  Vec3 offset;
  std::vector<int> channels;  // 0..2 X/Y/Zposition, 3..5 X/Y/Zrotation, in the file's order
  int depth = 0;
};

struct Lexer {
  const char *p, *e;
  int line = 1;
  void ws() {
    while (p < e && std::isspace((unsigned char)*p)) line += *p++ == '\n';
  }
  std::string word() {
    ws();
    const char *w = p;
    while (p < e && !std::isspace((unsigned char)*p) && *p != '{' && *p != '}') p++;
    if (p == w && p < e) p++;  // (a brace on its own)
    return std::string(w, (size_t)(p - w));
  }
  bool number(float &v) {
    ws();
    const auto r = fast_float::from_chars(p, e, v);
    if (r.ec != std::errc()) return false;
    p = r.ptr;
    return std::isfinite(v);
  }
  bool done() {
    ws();
    return p >= e;
  }
};

int channel_of(const std::string &w) {
  static const char *const names[6] = {"Xposition", "Yposition", "Zposition", "Xrotation", "Yrotation", "Zrotation"};
  for (int i = 0; i < 6; i++)
    if (w == names[i]) return i;
  return -1;
}

/* The hierarchy: ROOT / JOINT name { OFFSET x y z CHANNELS n ... [JOINT ...] [End Site { OFFSET ... }] }. */
bool read_hierarchy(Lexer &lx, std::vector<BvhJoint> &joints, std::string &error) {
  auto fail = [&](const std::string &what) { return error = strprintf("BVH line %d: %s", lx.line, what.c_str()), false; };
  if (lx.word() != "HIERARCHY") return fail("expected HIERARCHY");
  std::vector<int> stack;  // the open joints
  while (!lx.done()) {
    std::string w = lx.word();
    if (w == "MOTION") return !joints.empty() || fail("no joints");
    if (w == "ROOT" || w == "JOINT") {
      if (joints.size() >= kMaxJoints) return fail("too many joints");
      BvhJoint j;
      j.name = lx.word();
      j.parent = stack.empty() ? -1 : stack.back();
      j.depth = (int)stack.size();
      if (lx.word() != "{") return fail("expected { after " + j.name);
      joints.push_back(j);
      stack.push_back((int)joints.size() - 1);
      continue;
    }
    if (w == "End") {
      lx.word();  // Site
      if (lx.word() != "{") return fail("expected { after End Site");
      BvhJoint j;  // (an end site: a joint with no channels, so the last bone has a length)
      j.name = (stack.empty() ? std::string("End") : joints[(size_t)stack.back()].name) + " End";
      j.parent = stack.empty() ? -1 : stack.back();
      j.depth = (int)stack.size();
      joints.push_back(j);
      stack.push_back((int)joints.size() - 1);
      continue;
    }
    if (w == "OFFSET") {
      if (stack.empty()) return fail("OFFSET outside a joint");
      Vec3 &o = joints[(size_t)stack.back()].offset;
      if (!lx.number(o.x) || !lx.number(o.y) || !lx.number(o.z)) return fail("bad OFFSET");
      continue;
    }
    if (w == "CHANNELS") {
      if (stack.empty()) return fail("CHANNELS outside a joint");
      float n = 0.0f;
      if (!lx.number(n) || n < 0.0f || n > 6.0f) return fail("bad CHANNELS count");
      for (int k = 0; k < (int)n; k++) {
        const int c = channel_of(lx.word());
        if (c < 0) return fail("unknown channel");
        joints[(size_t)stack.back()].channels.push_back(c);
      }
      continue;
    }
    if (w == "}") {
      if (stack.empty()) return fail("a } too many");
      stack.pop_back();
      continue;
    }
    if (w.empty()) break;
    return fail("unexpected " + w);
  }
  return fail("no MOTION");
}

/* MOTION: Frames: n, Frame Time: s, then n rows of every joint's channels. Rows short of values are dropped. */
bool read_motion(Lexer &lx, size_t channels, size_t &frames, float &frame_time, std::vector<float> &rows, std::string &error) {
  auto fail = [&](const std::string &what) { return error = strprintf("BVH line %d: %s", lx.line, what.c_str()), false; };
  float n = 0.0f;
  const std::string fw = lx.word();  // ("Frames:" and the count, with or without a space between)
  const bool joined = fw.size() > 7 && fw.rfind("Frames:", 0) == 0;
  if (fw != "Frames:" && !joined) return fail("expected Frames:");
  if (joined) n = (float)std::atof(fw.c_str() + 7);
  else if (!lx.number(n)) return fail("expected the frame count");
  if (n < 0.0f || n > (float)kMaxFrames) return fail("bad frame count");
  frames = (size_t)n;
  std::string w = lx.word();  // "Frame"
  w = lx.word();              // "Time:"
  if (w != "Time:" || !lx.number(frame_time) || !(frame_time > 1e-5f) || frame_time > 10.0f) return fail("expected Frame Time:");
  for (size_t f = 0; f < frames; f++)
    for (size_t c = 0; c < channels; c++) {
      float v = 0.0f;
      if (!lx.number(v)) {
        frames = f;  // (cut short: the whole frames so far)
        rows.resize(frames * channels);
        return frames > 0 || fail("no frames");
      }
      rows.push_back(v);
    }
  return frames > 0 || fail("no frames");
}

Vec3 to_blendity(Vec3 v, float scale) { return Vec3(-v.x, v.y, v.z) * scale; }
Quat to_blendity(Quat q) { return normalize(Quat(q.x, -q.y, -q.z, q.w)); }

/* A joint's rotation from its rotation channels, in the file's order (each about its own axis, applied one after
 * another as written), right-handed, degrees. */
Quat channel_rotation(const BvhJoint &j, const float *row) {
  Quat q;
  for (size_t c = 0; c < j.channels.size(); c++) {
    const int ch = j.channels[c];
    if (ch < 3) continue;
    const Vec3 axis = ch == 3 ? Vec3(1, 0, 0) : ch == 4 ? Vec3(0, 1, 0) : Vec3(0, 0, 1);
    q = q * Quat::axis_angle(axis, row[c] * kDeg2Rad);
  }
  return normalize(q);
}

Vec3 channel_position(const BvhJoint &j, const float *row, bool &any) {
  Vec3 p = j.offset;
  any = false;
  for (size_t c = 0; c < j.channels.size(); c++)
    if (j.channels[c] < 3) p[j.channels[c]] = row[c], any = true;
  return p;
}

}  // namespace

bool import_bvh_file(const std::string &path, ImportResult &out) {
  std::string text;
  if (!fs::read_file(path, text)) return out.error = "cannot read " + path, false;
  if (out.source.empty()) out.source = path;
  Lexer lx{text.data(), text.data() + text.size()};
  std::vector<BvhJoint> joints;
  if (!read_hierarchy(lx, joints, out.error)) return false;
  size_t channels = 0;
  std::vector<size_t> first_channel(joints.size());
  for (size_t i = 0; i < joints.size(); i++) first_channel[i] = channels, channels += joints[i].channels.size();
  size_t frames = 0;
  float frame_time = 1.0f / 30.0f;
  std::vector<float> rows;
  if (!read_motion(lx, channels, frames, frame_time, rows, out.error)) return false;
  /* The unit: a skeleton taller than 10 of them is in centimetres (the usual capture unit); metres otherwise. */
  float low = 1e30f, high = -1e30f;
  {
    std::vector<Vec3> world(joints.size());
    for (size_t i = 0; i < joints.size(); i++) {
      world[i] = joints[i].offset + (joints[i].parent >= 0 ? world[(size_t)joints[i].parent] : Vec3(0.0f));
      low = std::min(low, world[i].y), high = std::max(high, world[i].y);
    }
  }
  const float scale = high - low > 10.0f ? 0.01f : 1.0f;
  if (high - low > 1e6f) return out.error = "BVH: a skeleton a million units tall", false;
  /* The joints as bones, at rest where their offsets put them (BVH has no rest orientation: identity). Names are
   * paths below the root: no slashes, and two siblings of one name (two end sites) told apart. */
  const size_t first = out.nodes.size();
  for (size_t i = 0; i < joints.size(); i++) {
    for (char &c : joints[i].name)
      if (c == '/') c = '_';
    for (size_t j = 0; j < i; j++)
      if (joints[j].parent == joints[i].parent && joints[j].name == joints[i].name) joints[i].name += strprintf(".%03zu", i);
    ImportedNode n;
    n.name = joints[i].name;
    n.is_bone = true;
    n.parent = joints[i].parent >= 0 ? (int)(first + (size_t)joints[i].parent) : -1;
    n.position = to_blendity(joints[i].offset, scale);
    out.nodes.push_back(std::move(n));
  }
  /* The motion as a clip: each joint's rotation (and the root's, or any joint's, position) a frame at a time. */
  AnimationClip clip;
  clip.name = fs::stem(path);
  clip.frame_rate = std::max(1.0f, std::min(1000.0f, 1.0f / frame_time));
  clip.length = (float)(frames > 0 ? frames - 1 : 0);
  clip.loop = true;
  for (size_t i = 0; i < joints.size(); i++) {
    const BvhJoint &j = joints[i];
    if (j.channels.empty()) continue;
    const std::string jpath = import_node_path(out, (int)(first + i));
    std::vector<float> fr(frames), rx(frames), ry(frames), rz(frames), px(frames), py(frames), pz(frames);
    bool moves = false, turns = false;
    Vec3 last(0.0f);
    for (size_t f = 0; f < frames; f++) {
      const float *row = rows.data() + f * channels + first_channel[i];
      fr[f] = (float)f;
      const Quat q = to_blendity(channel_rotation(j, row));
      last = euler_near(q, last);
      rx[f] = last.x, ry[f] = last.y, rz[f] = last.z;
      turns = turns || std::fabs(last.x) > 1e-4f || std::fabs(last.y) > 1e-4f || std::fabs(last.z) > 1e-4f;
      bool any = false;
      const Vec3 p = to_blendity(channel_position(j, row, any), scale);
      px[f] = p.x, py[f] = p.y, pz[f] = p.z;
      moves = moves || (any && length(p - out.nodes[first + i].position) > 1e-6f);
    }
    auto varies = [](const std::vector<float> &v, float tol) { return std::any_of(v.begin(), v.end(), [&](float x) { return std::fabs(x - v[0]) > tol; }); };
    auto put = [&](const char *property, int axis, const std::vector<float> &v, float tolerance) {
      if (!varies(v, tolerance * 0.1f)) return;  // (a number that never moves: no track; angles jitter by float noise)
      std::vector<AnimKey> k(frames);
      for (size_t f = 0; f < frames; f++) k[f].frame = fr[f], k[f].value = v[f], k[f].interp = AnimLinear;
      reduce_keys(k, tolerance);
      clip.track(jpath, property, axis).keys = std::move(k);
    };
    if (turns) put("Rotation", 0, rx, 0.01f), put("Rotation", 1, ry, 0.01f), put("Rotation", 2, rz, 0.01f);
    if (moves) put("Position", 0, px, 1e-4f), put("Position", 1, py, 1e-4f), put("Position", 2, pz, 1e-4f);
  }
  if (!clip.tracks.empty()) out.clips.push_back(std::move(clip));
  out.notes.push_back(strprintf("BVH: %zu joints, %zu frames at %.1f fps, %s", joints.size(), frames, 1.0f / frame_time,
                                scale == 0.01f ? "in centimetres (scaled to metres)" : "in metres"));
  out.summary = strprintf("%zu joint(s), %zu frame(s) at %.0f fps", joints.size(), frames, 1.0f / frame_time);
  return true;
}

}  // namespace bl

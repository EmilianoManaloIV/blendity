// SPDX-License-Identifier: GPL-2.0-or-later
#include "import_anim.h"

#include "../../extern/ufbx/ufbx.h"
#include "../core/core.h"
#include "../core/json.h"
#include "animation.h"

#include <algorithm>
#include <cmath>

namespace bl {

namespace {

constexpr size_t kMaxFrames = 200000;  // (an hour at 50 fps: a damaged time range is cut here)
constexpr float kPosTol = 1e-4f, kAngleTol = 0.01f;  // what a baked key may stray from the line between its neighbours

std::string node_path(const ImportResult &out, int i) { return import_node_path(out, i); }

std::vector<AnimKey> reduced(std::vector<AnimKey> keys, float tolerance) {
  reduce_keys(keys, tolerance);
  return keys;
}

float angle_between(Quat a, Quat b) {
  const float d = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
  return 2.0f * std::acos(std::min(1.0f, d));
}

std::vector<AnimKey> linear_keys(const std::vector<float> &frames, const std::vector<float> &values, int interp = AnimLinear) {
  std::vector<AnimKey> keys;
  for (size_t k = 0; k < frames.size() && k < values.size(); k++) {
    AnimKey key;
    key.frame = frames[k], key.value = values[k], key.interp = interp;
    keys.push_back(key);
  }
  return keys;
}

/* An object's transform baked a frame at a time: a track for each number that changes over the clip, its keys reduced
 * to the frames that matter. The rotation becomes Euler angles, each frame's nearest the frame before (no wraps), from
 * the object's rest angles. */
void bake_transform(AnimationClip &clip, const std::string &path, const std::vector<float> &frames, const std::vector<Vec3> &pos, const std::vector<Quat> &rot,
                    const std::vector<Vec3> &scl, Vec3 rest_euler) {
  /* A number that never moves gets no track: a thousandth of a degree, a hundredth of a millimetre (an angle read
   * back from a quaternion jitters by more than float noise from one machine to another). */
  auto varies = [](const std::vector<float> &v, float tol) {
    return std::any_of(v.begin(), v.end(), [&](float x) { return std::fabs(x - v[0]) > tol; });
  };
  std::vector<Vec3> eul;
  Vec3 last = rest_euler;
  for (const Quat &q : rot) eul.push_back(last = euler_near(q, last));
  for (int a = 0; a < 3; a++) {
    std::vector<float> p, s, e;
    for (size_t k = 0; k < frames.size(); k++) p.push_back(pos[k][a]), s.push_back(scl[k][a]), e.push_back(eul[k][a]);
    if (varies(p, 1e-5f)) clip.track(path, "Position", a).keys = reduced(linear_keys(frames, p), kPosTol);
    if (varies(s, 1e-5f)) clip.track(path, "Scale", a).keys = reduced(linear_keys(frames, s), kPosTol);
    if (varies(e, 1e-3f)) clip.track(path, "Rotation", a).keys = reduced(linear_keys(frames, e), kAngleTol);
  }
}

std::string ustr(const ufbx_string &s) { return std::string(s.data, s.length); }

/* A blend channel's key name as the mesh import names it (Maya's "blendShape1.Smile" without the deformer; a nameless
 * channel after its shape: the target, else the last keyframe's). */
std::string channel_name(const ufbx_blend_deformer *bd, const ufbx_blend_channel *ch) {
  const ufbx_blend_shape *sh = ch->target_shape ? ch->target_shape : ch->keyframes.count ? ch->keyframes.data[ch->keyframes.count - 1].shape : nullptr;
  std::string name = ustr(ch->name).empty() && sh ? ustr(sh->name) : ustr(ch->name);
  const std::string prefix = ustr(bd->name) + ".";
  if (name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0) name = name.substr(prefix.size());
  return name;
}

}  // namespace

void fbx_import_clips(const ufbx_scene *scene, const std::unordered_map<const ufbx_node *, int> &index, ImportResult &out) {
  const double fps = scene->settings.frames_per_second >= 1.0 && scene->settings.frames_per_second <= 1000.0 ? scene->settings.frames_per_second : 30.0;
  for (size_t si = 0; si < scene->anim_stacks.count; si++) {
    const ufbx_anim_stack *stack = scene->anim_stacks.data[si];
    const double t0 = stack->time_begin, t1 = stack->time_end;
    if (!(t1 > t0) || !std::isfinite(t0) || !std::isfinite(t1) || !stack->anim) continue;
    const double span = std::min(t1 - t0, (double)kMaxFrames / fps);  // (bounded before the rounding: a damaged range)
    const size_t nframes = (size_t)std::lround(span * fps) + 1;
    std::vector<float> frames(nframes);
    for (size_t f = 0; f < nframes; f++) frames[f] = (float)f;
    AnimationClip clip;
    clip.name = ustr(stack->name).empty() ? strprintf("Clip %zu", si + 1) : ustr(stack->name);
    clip.frame_rate = (float)fps;
    clip.length = (float)(nframes - 1);
    for (size_t ni = 0; ni < scene->nodes.count; ni++) {  // (the file's order: the same clip every import)
      const ufbx_node *node = scene->nodes.data[ni];
      const auto found = index.find(node);
      if (found == index.end()) continue;
      const int i = found->second;
      std::vector<Vec3> pos(nframes), scl(nframes);
      std::vector<Quat> rot(nframes);
      for (size_t f = 0; f < nframes; f++) {
        const ufbx_transform t = ufbx_evaluate_transform(stack->anim, node, t0 + f / fps);
        pos[f] = {-(float)t.translation.x, (float)t.translation.y, (float)t.translation.z};
        rot[f] = normalize(Quat((float)t.rotation.x, -(float)t.rotation.y, -(float)t.rotation.z, (float)t.rotation.w));
        scl[f] = {(float)t.scale.x, (float)t.scale.y, (float)t.scale.z};
      }
      bake_transform(clip, node_path(out, i), frames, pos, rot, scl, out.nodes[(size_t)i].rotation.to_euler());
      if (!node->mesh) continue;
      for (size_t bi = 0; bi < node->mesh->blend_deformers.count; bi++) {
        const ufbx_blend_deformer *bd = node->mesh->blend_deformers.data[bi];
        for (size_t ci = 0; ci < bd->channels.count; ci++) {
          const ufbx_blend_channel *ch = bd->channels.data[ci];
          std::vector<float> w(nframes);
          for (size_t f = 0; f < nframes; f++) w[f] = std::max(0.0f, std::min(1.0f, (float)ufbx_evaluate_blend_weight(stack->anim, ch, t0 + f / fps)));
          if (std::none_of(w.begin(), w.end(), [&](float x) { return std::fabs(x - w[0]) > 1e-6f; })) continue;
          clip.track(node_path(out, i), "MeshRenderer.BlendShape/" + channel_name(bd, ch), 0).keys = reduced(linear_keys(frames, w), 1e-4f);
        }
      }
    }
    if (!clip.tracks.empty()) out.clips.push_back(std::move(clip));
  }
}

namespace {

struct Sampler {
  std::vector<float> times, values;
  int comps = 0;
  int interp = 0;  // 0 linear, 1 step, 2 cubic spline
  size_t count() const { return times.size(); }
  /* The value at t (per component), between its keys as the file says; a cubic spline's tangents are per second. */
  void at(float t, std::vector<float> &v) const {
    const size_t n = times.size(), stride = (size_t)comps * (interp == 2 ? 3 : 1);
    v.assign((size_t)comps, 0.0f);
    if (!n || values.size() < n * stride) return;
    auto value = [&](size_t k, int c) { return values[k * stride + (interp == 2 ? (size_t)comps : 0) + (size_t)c]; };
    if (t <= times[0] || n == 1) return void(std::generate(v.begin(), v.end(), [&, c = 0]() mutable { return value(0, c++); }));
    if (t >= times[n - 1]) return void(std::generate(v.begin(), v.end(), [&, c = 0]() mutable { return value(n - 1, c++); }));
    const size_t k = (size_t)(std::upper_bound(times.begin(), times.end(), t) - times.begin()) - 1;
    const float dt = std::max(1e-6f, times[k + 1] - times[k]), u = (t - times[k]) / dt;
    for (int c = 0; c < comps; c++) {
      const float a = value(k, c), b = value(k + 1, c);
      if (interp == 1) v[(size_t)c] = a;
      else if (interp == 0) v[(size_t)c] = a + (b - a) * u;
      else {
        const float m0 = values[k * stride + 2 * (size_t)comps + (size_t)c] * dt, m1 = values[(k + 1) * stride + (size_t)c] * dt;  // (out tangent, in tangent)
        const float u2 = u * u, u3 = u2 * u;
        v[(size_t)c] = (2 * u3 - 3 * u2 + 1) * a + (u3 - 2 * u2 + u) * m0 + (-2 * u3 + 3 * u2) * b + (u3 - u2) * m1;
      }
    }
  }
  Quat quat_at(float t) const {
    std::vector<float> v;
    if (interp != 0 || times.size() < 2) {
      at(t, v);
      return v.size() == 4 ? normalize(Quat(v[0], v[1], v[2], v[3])) : Quat();
    }
    const size_t n = times.size();
    if (t <= times[0]) return normalize(Quat(values[0], values[1], values[2], values[3]));
    if (t >= times[n - 1]) return normalize(Quat(values[(n - 1) * 4], values[(n - 1) * 4 + 1], values[(n - 1) * 4 + 2], values[(n - 1) * 4 + 3]));
    const size_t k = (size_t)(std::upper_bound(times.begin(), times.end(), t) - times.begin()) - 1;
    const float u = (t - times[k]) / std::max(1e-6f, times[k + 1] - times[k]);
    const Quat a(values[k * 4], values[k * 4 + 1], values[k * 4 + 2], values[k * 4 + 3]), b(values[(k + 1) * 4], values[(k + 1) * 4 + 1], values[(k + 1) * 4 + 2], values[(k + 1) * 4 + 3]);
    return slerp(normalize(a), normalize(b), u);
  }
};

float best_rate(const std::vector<const Sampler *> &samplers) {
  std::vector<float> times;
  for (const Sampler *s : samplers) times.insert(times.end(), s->times.begin(), s->times.end());
  return best_frame_rate(times);
}

}  // namespace

void gltf_import_clips(const Json &doc, const std::vector<int> &node_index, const std::function<std::vector<float>(int, int &)> &read_accessor, ImportResult &out) {
  size_t ai = 0;
  for (const Json &anim : doc["animations"].arr) {
    ai++;
    std::vector<Sampler> samplers;
    for (const Json &js : anim["samplers"].arr) {
      Sampler s;
      int tc = 0, vc = 0;
      s.times = read_accessor(js["input"].integer(), tc);
      s.values = read_accessor(js["output"].integer(), vc);
      s.comps = vc;
      const std::string interp = js["interpolation"].str;
      s.interp = interp == "STEP" ? 1 : interp == "CUBICSPLINE" ? 2 : 0;
      const bool finite = std::all_of(s.times.begin(), s.times.end(), [](float t) { return std::isfinite(t) && t >= 0.0f && t < 1e6f; });
      if (tc != 1 || !finite || !std::is_sorted(s.times.begin(), s.times.end()) || s.times.size() > kMaxFrames) s.times.clear();
      samplers.push_back(std::move(s));
    }
    /* The clip runs from the first key (Blender keys from its scene's first frame, 1/24 s in): times from there. */
    float start = 1e30f, end = 0.0f;
    for (const Sampler &s : samplers)
      if (!s.times.empty()) start = std::min(start, s.times.front()), end = std::max(end, s.times.back());
    if (!(start < end) && !(start <= end)) continue;
    std::vector<const Sampler *> all;
    for (Sampler &s : samplers) {
      for (float &t : s.times) t -= start;
      if (!s.times.empty()) all.push_back(&s);
    }
    if (all.empty()) continue;
    end -= start;
    const float rate = best_rate(all);
    const size_t nframes = (size_t)std::lround(std::min(end, (float)kMaxFrames / rate) * rate) + 1;
    AnimationClip clip;
    clip.name = anim["name"].str.empty() ? strprintf("Animation %zu", ai) : anim["name"].str;
    clip.frame_rate = rate;
    clip.length = (float)(nframes - 1);
    std::vector<float> frames(nframes);
    for (size_t f = 0; f < nframes; f++) frames[f] = (float)f;
    for (const Json &ch : anim["channels"].arr) {
      const int si = ch["sampler"].integer(), ni = ch["target"]["node"].integer();
      const int node = ni >= 0 && (size_t)ni < node_index.size() ? node_index[(size_t)ni] : -1;
      if (si < 0 || (size_t)si >= samplers.size() || node < 0 || samplers[(size_t)si].times.empty()) continue;
      const Sampler &s = samplers[(size_t)si];
      const std::string path = ch["target"]["path"].str, npath = node_path(out, node);
      const ImportedNode &n = out.nodes[(size_t)node];
      std::vector<float> v;
      if (path == "rotation" && s.comps == 4) {
        std::vector<Vec3> pos(nframes, n.position), scl(nframes, n.scale);
        std::vector<Quat> rot(nframes);
        for (size_t f = 0; f < nframes; f++) {
          const Quat q = s.quat_at((float)f / rate);
          rot[f] = normalize(Quat(q.x, -q.y, -q.z, q.w));  // (mirrored like the nodes' rest rotations)
        }
        bake_transform(clip, npath, frames, pos, rot, scl, n.rotation.to_euler());
        continue;
      }
      const bool baked = s.interp == 2;  // (a cubic spline: a frame at a time, linear)
      std::vector<float> at_frames;
      if (baked) at_frames = frames;
      else
        for (float t : s.times) at_frames.push_back(t * rate);
      if ((path == "translation" || path == "scale") && s.comps == 3) {
        std::vector<float> k[3];
        for (float fr : at_frames) {
          s.at(fr / rate, v);
          const Vec3 x = path == "translation" ? Vec3(-v[0], v[1], v[2]) : Vec3(v[0], v[1], v[2]);
          for (int a = 0; a < 3; a++) k[a].push_back(x[a]);
        }
        for (int a = 0; a < 3; a++) {
          if (std::none_of(k[a].begin(), k[a].end(), [&](float x) { return std::fabs(x - k[a][0]) > 1e-6f; })) continue;
          AnimTrack &t = clip.track(npath, path == "translation" ? "Position" : "Scale", a);
          t.keys = linear_keys(at_frames, k[a], s.interp == 1 ? AnimConstant : AnimLinear);
          if (baked) reduce_keys(t.keys, kPosTol);
        }
        continue;
      }
      if (path == "weights" && n.mesh && s.comps >= 1) {
        const size_t targets = n.mesh->shape_keys.size();
        if (!targets || s.values.size() < s.times.size() * targets * (baked ? 3 : 1)) continue;
        Sampler per = s;  // (the weights of all targets interleaved per key: read as that many components)
        per.comps = (int)targets;
        for (size_t t = 0; t < targets; t++) {
          std::vector<float> w;
          for (float fr : at_frames) per.at(fr / rate, v), w.push_back(std::max(0.0f, std::min(1.0f, v[t])));
          if (std::none_of(w.begin(), w.end(), [&](float x) { return std::fabs(x - w[0]) > 1e-6f; })) continue;
          clip.track(npath, "MeshRenderer.BlendShape/" + n.mesh->shape_keys[t].name, 0).keys = linear_keys(at_frames, w, s.interp == 1 ? AnimConstant : AnimLinear);
          if (baked) reduce_keys(clip.track(npath, "MeshRenderer.BlendShape/" + n.mesh->shape_keys[t].name, 0).keys, 1e-4f);
        }
      }
    }
    if (!clip.tracks.empty()) out.clips.push_back(std::move(clip));
  }
}

}  // namespace bl

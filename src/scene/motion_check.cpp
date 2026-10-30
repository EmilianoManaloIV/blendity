// SPDX-License-Identifier: GPL-2.0-or-later
// Motion Check, smoothing and Lock in Place (task 0284). See motion_check.h.
#include "motion_check.h"

#include "gait.h"
#include "../core/core.h"

#include <algorithm>
#include <cmath>

namespace bl::motion {

namespace {

constexpr float kDeg = 57.2957795f;

float angle_between(Quat a, Quat b) {
  const float d = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
  return 2.0f * std::acos(std::min(1.0f, d)) * kDeg;
}

std::vector<GameObject *> bones_under(Scene &scene, GameObject &root) {
  std::vector<GameObject *> out;
  scene.for_each([&](GameObject &g) {
    if (g.get<Bone>() && scene.is_ancestor(&root, &g)) out.push_back(&g);
  });
  return out;
}

/* Every bone's local turn and the feet's heads, frame by frame. */
struct Sampled {
  int first = 0, last = 0;
  std::vector<GameObject *> bones;
  std::vector<std::vector<Quat>> turn;  // [frame][bone]
  std::vector<std::vector<Vec3>> head;  // [frame][bone]
};

Sampled sample(Scene &scene, GameObject &root, const AnimationClip &clip) {
  Sampled s;
  s.bones = bones_under(scene, root);
  s.first = 0, s.last = (int)std::ceil(clip.end_frame());
  body::for_each_frame(scene, root, clip, s.first, s.last, [&](int) {
    std::vector<Quat> t;
    std::vector<Vec3> h;
    for (GameObject *b : s.bones) t.push_back(b->local().rotation), h.push_back(b->world_position());
    s.turn.push_back(t), s.head.push_back(h);
  });
  return s;
}

/* The feet as the rig has them (by names, then shape: FK rigs too), each planted while low and slow (as contacts are
 * found, task 0116); a planted stretch drifting sideways further than the setting is a slide. */
void find_slides(Scene &scene, GameObject &root, const Sampled &sm, const CheckSettings &cs, float clip_fps, std::vector<Issue> &out) {
  GameObject *arm = const_cast<GameObject *>(body::find_armature(root));
  if (!arm) return;
  const gait::Rig rig = gait::analyse(scene, *arm);
  const body::ContactSettings contact;
  const float fps = std::max(1.0f, (float)clip_fps);
  for (const gait::Leg &leg : rig.legs) {
    const auto it = std::find(sm.bones.begin(), sm.bones.end(), leg.foot);
    if (it == sm.bones.end()) continue;
    const size_t bi = (size_t)(it - sm.bones.begin());
    float ground = 1e9f;
    for (const auto &h : sm.head) ground = std::min(ground, h[bi].y);
    auto planted = [&](size_t f) {
      if (f + 1 >= sm.head.size()) return false;
      const Vec3 d = sm.head[f + 1][bi] - sm.head[f][bi];
      return sm.head[f][bi].y - ground < contact.height && length(d) * fps < contact.speed;
    };
    for (size_t f = 0; f < sm.head.size();) {
      if (!planted(f)) {
        f++;
        continue;
      }
      size_t e = f;
      while (planted(e + 1)) e++;
      float drift = 0.0f;
      for (size_t k = f; k <= e + 1 && k < sm.head.size(); k++) {
        const Vec3 d = sm.head[k][bi] - sm.head[f][bi];
        drift = std::max(drift, length(Vec3(d.x, 0.0f, d.z)));
      }
      if ((int)(e + 1 - f) >= contact.min_frames && drift * 100.0f > cs.slide_cm)
        out.push_back({Issue::Sliding, leg.foot->name, sm.first + (int)f, sm.first + (int)e + 1, drift * 100.0f});
      f = e + 2;
    }
  }
}

void find_pops_and_shakes(const Sampled &sm, const CheckSettings &cs, std::vector<Issue> &out) {
  const int n = (int)sm.turn.size();
  for (size_t b = 0; b < sm.bones.size(); b++) {
    std::vector<float> speed((size_t)std::max(0, n - 1));
    for (int f = 0; f + 1 < n; f++) speed[(size_t)f] = angle_between(sm.turn[(size_t)f][b], sm.turn[(size_t)f + 1][b]);
    /* A pop: frame f turns far away and comes back (fast in and fast out, slow either side, the frames around it close). */
    for (int f = 1; f + 1 < n; f++) {
      const float in = speed[(size_t)f - 1], back = speed[(size_t)f], jump = std::min(in, back);
      const float around = std::max(f >= 2 ? speed[(size_t)f - 2] : 0.0f, f + 1 < (int)speed.size() ? speed[(size_t)f + 1] : 0.0f);
      const bool returns = angle_between(sm.turn[(size_t)f - 1][b], sm.turn[(size_t)f + 1][b]) < 0.5f * jump;
      if (jump > cs.pop_deg && jump > cs.pop_ratio * std::max(around, 0.5f) && returns)
        out.push_back({Issue::Pop, sm.bones[b]->name, sm.first + f - 1, sm.first + f + 1, jump});
    }
    /* Shaking: the turn's signed change flipping direction frame after frame with a large second difference. */
    int run = 0, start = 0;
    float sum = 0.0f;
    auto flush = [&](int end) {
      if (run >= cs.shake_min_frames && sum / run > cs.shake_deg) out.push_back({Issue::Shaking, sm.bones[b]->name, sm.first + start, sm.first + end, sum / run});
      run = 0, sum = 0.0f;
    };
    for (int f = 1; f + 1 < n; f++) {
      const Quat d0 = normalize(sm.turn[(size_t)f][b] * sm.turn[(size_t)f - 1][b].conjugate());
      const Quat d1 = normalize(sm.turn[(size_t)f + 1][b] * sm.turn[(size_t)f][b].conjugate());
      const float flip = d0.x * d1.x + d0.y * d1.y + d0.z * d1.z;  // (the two steps' axes opposed: back and forth)
      const float jerk = angle_between(d0, d1);
      if (flip < 0.0f && jerk > cs.shake_deg) {
        if (!run) start = f - 1;
        run++, sum += jerk;
        continue;
      }
      flush(f);
    }
    flush(n - 1);
  }
}

}  // namespace

std::string Issue::text() const {
  if (kind == Sliding) return strprintf("%s slides %.1f cm while planted, frames %d-%d (Fix: Lock in Place)", bone.c_str(), amount, from, to);
  if (kind == Pop) return strprintf("%s pops %.0f degrees in one frame, %d-%d (Fix: smooth it)", bone.c_str(), amount, from, to);
  return strprintf("%s shakes (%.1f degrees a frame back and forth), frames %d-%d (Fix: smooth it)", bone.c_str(), amount, from, to);
}

std::vector<Issue> check(Scene &scene, GameObject &root, const AnimationClip &clip, const CheckSettings &cs) {
  std::vector<Issue> out;
  const Sampled sm = sample(scene, root, clip);
  if (sm.turn.size() < 3) return out;
  find_slides(scene, root, sm, cs, clip.frame_rate, out);
  find_pops_and_shakes(sm, cs, out);
  std::sort(out.begin(), out.end(), [](const Issue &a, const Issue &b) { return a.from < b.from; });
  return out;
}

size_t smooth(AnimationClip &clip, const std::vector<std::string> &paths, float from, float to, float strength, float radius) {
  size_t changed = 0;
  strength = std::max(0.0f, std::min(1.0f, strength));
  const float sigma = std::max(0.5f, radius * 0.5f);
  for (const std::string &path : paths) {
    AnimTrack *rx = clip.find(path, "Rotation", 0);
    std::vector<float> frames;
    for (const char *prop : {"Rotation", "Position"})
      for (int a = 0; a < 3; a++)
        if (AnimTrack *t = clip.find(path, prop, a))
          for (const AnimKey &k : t->keys) frames.push_back(k.frame);
    std::sort(frames.begin(), frames.end());
    frames.erase(std::unique(frames.begin(), frames.end(), [](float a, float b) { return std::fabs(a - b) < 0.01f; }), frames.end());
    /* Every keyed frame's turn and place as they are now: the average reads these, not the ones already smoothed. */
    std::vector<Quat> q;
    std::vector<Vec3> e, p;
    for (float f : frames) {
      Vec3 ev, pv;
      for (int a = 0; a < 3; a++) {
        const AnimTrack *tr = clip.find(path, "Rotation", a), *tp = clip.find(path, "Position", a);
        ev[a] = tr ? tr->sample(f) : 0.0f, pv[a] = tp ? tp->sample(f) : 0.0f;
      }
      e.push_back(ev), p.push_back(pv), q.push_back(Quat::euler(ev));
    }
    for (size_t i = 0; i < frames.size(); i++) {
      if (frames[i] < from - 0.01f || frames[i] > to + 0.01f) continue;
      Quat acc(0, 0, 0, 0);
      Vec3 pacc;
      float wsum = 0.0f;
      for (size_t j = 0; j < frames.size(); j++) {
        const float d = frames[j] - frames[i];
        if (std::fabs(d) > radius) continue;
        const float w = std::exp(-0.5f * d * d / (sigma * sigma));
        Quat qj = q[j];
        if (qj.x * q[i].x + qj.y * q[i].y + qj.z * q[i].z + qj.w * q[i].w < 0.0f) qj = Quat(-qj.x, -qj.y, -qj.z, -qj.w);  // (one hemisphere)
        acc = Quat(acc.x + qj.x * w, acc.y + qj.y * w, acc.z + qj.z * w, acc.w + qj.w * w);
        pacc = pacc + p[j] * w;
        wsum += w;
      }
      if (wsum <= 0.0f) continue;
      const Quat avg = normalize(acc);
      const Quat out = slerp(q[i], avg, strength);
      if (rx) {
        const Vec3 n = euler_near(out, e[i]);
        for (int a = 0; a < 3; a++)
          if (AnimTrack *t = clip.find(path, "Rotation", a); t && t->key_at(frames[i]) >= 0) t->set_key(frames[i], n[a]);  // (no key added)
      }
      for (int a = 0; a < 3; a++)
        if (AnimTrack *t = clip.find(path, "Position", a); t && t->key_at(frames[i]) >= 0) t->set_key(frames[i], p[i][a] + (pacc[a] / wsum - p[i][a]) * strength);
      changed++;
    }
  }
  return changed;
}

/* All of the hold inside the range; easing in over `blend` frames before it and out over `blend` after (smoothstep). */
static float lock_weight(int f, int from, int to, int blend) {
  if (f >= from && f <= to) return 1.0f;
  if (blend <= 0) return 0.0f;
  const float d = (float)(f < from ? from - f : f - to), x = std::max(0.0f, 1.0f - d / (float)(blend + 1));
  return x * x * (3.0f - 2.0f * x);
}

int lock_in_place(Scene &scene, GameObject &root, AnimationClip &clip, GameObject &bone, int from, int to, int blend) {
  if (to < from) return 0;
  GameObject *arm = nullptr;
  for (GameObject *a = &bone; a; a = a->parent)
    if (a->get<Armature>()) arm = a;
  if (!arm) return 0;
  const body::PoseGuard guard(scene, root, clip);  // (the pose and the animated values back after)
  apply_clip(clip, root, (float)from);
  solve_constraints(scene);
  const Vec3 hold = bone.world_position();
  const std::vector<GameObject *> bones = bones_under(scene, *arm);
  int keyed = 0;
  /* An end IK drives: its target held instead (IK then keeps the end there; FK keys would be undone by it). */
  for (const body::Effector &e : body::effectors(scene, root, *arm)) {
    if (e.bone != &bone || !e.target || e.target_path.empty()) continue;
    for (int f = std::max(0, from - blend); f <= std::min((int)std::ceil(clip.end_frame()), to + blend); f++) {
      apply_clip(clip, root, (float)f);
      solve_constraints(scene);
      const Vec3 free = e.target->world_position();
      e.target->set_world_position(free + (hold - bone.world_position()) * lock_weight(f, from, to, blend));  // (the end's miss, moved onto its target)
      const Vec3 p = e.target->local().position;
      for (int a = 0; a < 3; a++) clip.track(e.target_path, "Position", a).set_key((float)f, p[a]);
      keyed++;
    }
    return keyed;
  }
  autopose::Settings st;
  st.balance = false;
  std::vector<GameObject *> others;  // (the rig's other feet: held where the clip has them while this one is locked)
  for (const gait::Leg &l : gait::analyse(scene, *arm).legs)
    if (l.foot && l.foot != &bone) others.push_back(l.foot);
  const int first = std::max(0, from - blend), last = std::min((int)std::ceil(clip.end_frame()), to + blend);
  for (int f = first; f <= last; f++) {
    apply_clip(clip, root, (float)f);
    solve_constraints(scene);
    const Vec3 free = bone.world_position();
    const float w = lock_weight(f, from, to, blend);
    autopose::solve(*arm, {{&bone, free + (hold - free) * w, 1.0f}}, others, nullptr, st);
    for (GameObject *b : bones) {
      const std::string path = anim_path(root, *b);
      Vec3 was;  // (the angles the clip has here: the new ones on the same branch, a 360 turn kept a turn)
      for (int a = 0; a < 3; a++) was[a] = clip.track(path, "Rotation", a).sample((float)f);
      const Vec3 e = euler_near(b->local().rotation, was);
      for (int a = 0; a < 3; a++) clip.track(path, "Rotation", a).set_key((float)f, e[a]);
      if (b == bones.front() || !b->parent || !b->parent->get<Bone>())  // (the root bone's place too: the hips may move)
        for (int a = 0; a < 3; a++) clip.track(path, "Position", a).set_key((float)f, b->local().position[a]);
    }
    keyed++;
  }
  return keyed;
}

}  // namespace bl::motion

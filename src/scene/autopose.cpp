// SPDX-License-Identifier: GPL-2.0-or-later
#include "autopose.h"
#include "spring_bones.h"
#include "../core/core.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <array>
#include <cstring>

namespace bl {
namespace autopose {

namespace {

/* Bones the pose leaves as they are: fingers, toes, eyes, the jaw, end bones and spring chains (their own motion). */
bool kept(const GameObject &g) {
  const std::string n = to_lower(g.name);
  for (const char *w : {"thumb", "index", "middle", "ring", "pinky", "little", "finger", "toe", "eye", "jaw", "_end", "end_"})
    if (n.find(w) != std::string::npos) return true;
  return spring::chain_of(const_cast<GameObject &>(g)) != nullptr;
}

bool spine_like(const GameObject &g) {
  const std::string n = to_lower(g.name);
  for (const char *w : {"spine", "chest", "neck", "head", "pelvis"})
    if (n.find(w) != std::string::npos) return true;
  return false;
}

Vec3 rotation_vector(Quat q) {
  if (q.w < 0.0f) q = {-q.x, -q.y, -q.z, -q.w};
  const float s = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
  if (s < 1e-9f) return Vec3(q.x, q.y, q.z) * 2.0f;
  return Vec3(q.x, q.y, q.z) * (2.0f * std::atan2(s, q.w) / s);
}

/* Solves the m x m system a y = b in place (Gaussian elimination with partial pivoting; a is J J^T + l^2 I, so it is
 * symmetric positive definite and never singular). */
void solve_small(std::vector<double> &a, std::vector<double> &b, int m) {
  for (int c = 0; c < m; c++) {
    int p = c;
    for (int r = c + 1; r < m; r++)
      if (std::fabs(a[(size_t)r * m + c]) > std::fabs(a[(size_t)p * m + c])) p = r;
    if (p != c) {
      for (int k = 0; k < m; k++) std::swap(a[(size_t)c * m + k], a[(size_t)p * m + k]);
      std::swap(b[(size_t)c], b[(size_t)p]);
    }
    const double d = a[(size_t)c * m + c];
    if (std::fabs(d) < 1e-15) continue;
    for (int r = c + 1; r < m; r++) {
      const double f = a[(size_t)r * m + c] / d;
      if (f == 0.0) continue;
      for (int k = c; k < m; k++) a[(size_t)r * m + k] -= f * a[(size_t)c * m + k];
      b[(size_t)r] -= f * b[(size_t)c];
    }
  }
  for (int c = m - 1; c >= 0; c--) {
    double s = b[(size_t)c];
    for (int k = c + 1; k < m; k++) s -= a[(size_t)c * m + k] * b[(size_t)k];
    const double d = a[(size_t)c * m + c];
    b[(size_t)c] = std::fabs(d) < 1e-15 ? 0.0 : s / d;
  }
}

bool hinge_like(const GameObject &g) {
  const std::string n = to_lower(g.name);
  for (const char *w : {"forearm", "lowerarm", "elbow", "shin", "calf", "knee", "lowerleg"})
    if (n.find(w) != std::string::npos) return true;
  return n.find("leg") != std::string::npos && n.find("upleg") == std::string::npos && n.find("upperleg") == std::string::npos &&
         n.find("thigh") == std::string::npos;
}

}  // namespace

Result solve(GameObject &armature, const std::vector<Goal> &goals, const std::vector<GameObject *> &pins, const body::Body *body, const Settings &s) {
  Result res;
  const auto t0 = std::chrono::steady_clock::now();
  /* The joints: every body bone under the armature, parents first, each with the index of its nearest joint above. */
  std::vector<GameObject *> bones;
  std::vector<GameObject *> stack{&armature};
  while (!stack.empty()) {
    GameObject *g = stack.back();
    stack.pop_back();
    for (auto it = g->children.rbegin(); it != g->children.rend(); ++it) stack.push_back(*it);
    if (g->get<Bone>() && !kept(*g)) bones.push_back(g);
  }
  auto depth = [](const GameObject *g) {
    int d = 0;
    for (; g->parent; g = g->parent) d++;
    return d;
  };
  std::stable_sort(bones.begin(), bones.end(), [&](const GameObject *a, const GameObject *b) { return depth(a) < depth(b); });
  const int B = (int)bones.size();
  res.bones = bones.size();
  if (!B || (goals.empty() && pins.empty())) return res;
  auto index_of = [&](const GameObject *g) {  // (the nearest joint at or above g; -1 none)
    for (; g; g = g->parent)
      for (int b = 0; b < B; b++)
        if (bones[(size_t)b] == g) return b;
    return -1;
  };
  std::vector<int> parent(B, -1);
  for (int b = 0; b < B; b++) parent[(size_t)b] = index_of(bones[(size_t)b]->parent);
  GameObject *hips = body ? body::hips_of(armature, body) : body::hips_of(armature);
  const int hips_i = hips ? index_of(hips) : -1;
  const bool move_hips = hips_i >= 0 && bones[(size_t)hips_i] == hips;
  /* The starting pose, the rest each joint's limit is measured from, and the bend axis of the hinges (knees, elbows:
   * the plane they already bend in, else their own X). */
  std::vector<Quat> start(B), rest(B);
  std::vector<Vec3> hinge(B, Vec3(0.0f));
  for (int b = 0; b < B; b++) {
    GameObject *g = bones[(size_t)b];
    start[(size_t)b] = g->local().rotation;
    rest[(size_t)b] = g->get<Bone>()->rest_rotation;
    if (!hinge_like(*g) || !g->parent) continue;
    const Vec3 up = normalize(g->parent->world_rotation().rotate(Vec3(0, 1, 0))), down = normalize(g->world_rotation().rotate(Vec3(0, 1, 0)));
    Vec3 a = cross(up, down);
    if (length(a) < 1e-3f) a = g->world_rotation().rotate(Vec3(1, 0, 0));
    hinge[(size_t)b] = normalize(g->parent->world_rotation().conjugate().rotate(normalize(a)));  // (in the parent's space)
  }
  struct Point {
    GameObject *bone;
    Vec3 target;
    float w;
    std::vector<uint8_t> above;  // [joint]: it moves this point
  };
  std::vector<Point> pts;
  for (const Goal &g : goals)
    if (g.bone) pts.push_back({g.bone, g.target, std::max(0.0f, g.weight), {}});
  for (GameObject *p : pins)
    if (p) pts.push_back({p, p->world_position(), 10.0f, {}});  // (pins far firmer than goals: the feet don't slide to reach)
  for (Point &p : pts) {
    p.above.assign((size_t)B, 0);
    for (int b = index_of(p.bone); b >= 0; b = parent[(size_t)b]) p.above[(size_t)b] = 1;
  }
  /* The ends kept turned as they were (a planted foot stays flat, a dragged hand keeps its twist). */
  std::vector<std::pair<int, Quat>> held;
  for (const Point &p : pts) {
    const int i = index_of(p.bone);
    if (i >= 0 && bones[(size_t)i] == p.bone) held.push_back({i, p.bone->world_rotation()});
  }
  /* Balance over the pins on the ground (within 10 cm of the lowest): the feet's, not a locked hand's; one foot too. */
  std::vector<Vec3> ground;
  float low = 1e30f;
  for (GameObject *p : pins)
    if (p) low = std::min(low, p->world_position().y);
  for (GameObject *p : pins)
    if (p && p->world_position().y <= low + 0.1f) ground.push_back(p->world_position());
  const bool balance = s.balance && body && body->valid() && !ground.empty();
  Vec3 support(0.0f);
  for (const Vec3 &g : ground) support = support + g / (float)ground.size();
  /* Each weighed bone's joint (its mass counted there and in every joint above). */
  std::vector<std::pair<const body::BoneMass *, int>> masses;
  if (balance)
    for (const body::BoneMass &bm : body->bones)
      if (bm.mass > 0.0f && bm.object) masses.push_back({&bm, index_of(bm.object)});
  const int P = (int)pts.size(), m = 3 * P + (balance ? 2 : 0), n = 3 * B + (move_hips ? 3 : 0);
  std::vector<double> J((size_t)m * n), e((size_t)m), A((size_t)m * m);
  const float limit = s.limit_deg * 3.14159265f / 180.0f;
  const int main_its = std::max(1, s.iterations), settle = pins.empty() ? 0 : 60;
  const Vec3 hips_start = hips ? hips->world_position() : Vec3(0.0f);
  std::vector<Vec3> sub_c(B);
  std::vector<float> sub_m(B);
  for (int it = 0; it < main_its + settle; it++) {
    if (it == main_its)
      for (size_t p = 0; p < goals.size() && p < pts.size(); p++) pts[p].w *= 0.03f;
    if (it >= main_its) {  // (settled: the pins back in place)
      float worst = 0.0f;
      for (int p = (int)goals.size(); p < P; p++) worst = std::max(worst, length(pts[(size_t)p].target - pts[(size_t)p].bone->world_position()));
      if (worst < 5e-4f) break;
    }
    std::fill(J.begin(), J.end(), 0.0);
    /* The center of mass and each joint's share of it: the masses at their joints, summed up the tree in one pass. */
    Vec3 com(0.0f);
    if (balance) {
      std::fill(sub_c.begin(), sub_c.end(), Vec3(0.0f));
      std::fill(sub_m.begin(), sub_m.end(), 0.0f);
      float total = 0.0f;
      for (const auto &mi : masses) {
        const Vec3 c = (mi.first->object->world_matrix() * mi.first->rest_inv).point(mi.first->center);
        com = com + c * mi.first->mass, total += mi.first->mass;
        if (mi.second >= 0) sub_c[(size_t)mi.second] = sub_c[(size_t)mi.second] + c * mi.first->mass, sub_m[(size_t)mi.second] += mi.first->mass;
      }
      com = com / std::max(1e-6f, total);
      for (int b = B - 1; b >= 0; b--)
        if (parent[(size_t)b] >= 0) sub_c[(size_t)parent[(size_t)b]] = sub_c[(size_t)parent[(size_t)b]] + sub_c[(size_t)b], sub_m[(size_t)parent[(size_t)b]] += sub_m[(size_t)b];
    }
    for (int p = 0; p < P; p++) {
      /* Each goal's error at most 10 cm a step (Buss's clamping): a target out of reach can't drag the pins after it. */
      Vec3 gap = pts[(size_t)p].target - pts[(size_t)p].bone->world_position();
      if (length(gap) > 0.1f) gap = gap * (0.1f / length(gap));
      const Vec3 d = gap * pts[(size_t)p].w;
      for (int a = 0; a < 3; a++) e[(size_t)(3 * p + a)] = d[a];
    }
    if (balance) {
      e[(size_t)(3 * P)] = (support.x - com.x) * s.balance_weight;
      e[(size_t)(3 * P + 1)] = (support.z - com.z) * s.balance_weight;
    }
    /* Each joint's axes: the world's three, or a hinge's one (its bend axis, now): a knee is never asked to bend
     * sideways, so every step is one it may take. */
    std::vector<std::array<Vec3, 3>> axes((size_t)B);
    for (int b = 0; b < B; b++) {
      if (length(hinge[(size_t)b]) > 0.5f && bones[(size_t)b]->parent)
        axes[(size_t)b] = {normalize(bones[(size_t)b]->parent->world_rotation().rotate(hinge[(size_t)b])), Vec3(0.0f), Vec3(0.0f)};
      else
        axes[(size_t)b] = {Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1)};
    }
    for (int b = 0; b < B; b++) {
      GameObject *bone = bones[(size_t)b];
      const Vec3 head = bone->world_position();
      for (int k = 0; k < 3; k++) {
        const Vec3 axis = axes[(size_t)b][(size_t)k];
        if (length(axis) < 0.5f) continue;
        const int col = 3 * b + k;
        for (int p = 0; p < P; p++) {
          if (!pts[(size_t)p].above[(size_t)b] || pts[(size_t)p].bone == bone) continue;  // (a bone's own turn doesn't move its head)
          const Vec3 d = cross(axis, pts[(size_t)p].bone->world_position() - head) * pts[(size_t)p].w;
          for (int a = 0; a < 3; a++) J[(size_t)(3 * p + a) * n + col] = d[a];
        }
        if (balance && sub_m[(size_t)b] > 0.0f) {
          const Vec3 d = cross(axis, sub_c[(size_t)b] - head * sub_m[(size_t)b]) * (s.balance_weight / body->mass);
          J[(size_t)(3 * P) * n + col] = d.x, J[(size_t)(3 * P + 1) * n + col] = d.z;
        }
      }
    }
    if (move_hips)
      for (int k = 0; k < 3; k++) {
        const int col = 3 * B + k;
        for (int p = 0; p < P; p++)
          if (pts[(size_t)p].above[(size_t)hips_i]) J[(size_t)(3 * p + k) * n + col] = pts[(size_t)p].w;
        if (balance && k != 1) J[(size_t)(3 * P + (k == 0 ? 0 : 1)) * n + col] = s.balance_weight * sub_m[(size_t)hips_i] / body->mass;
      }
    /* Damped least squares in two priorities: the goals and pins first, d1 = J1^T (J1 J1^T + l^2 I)^-1 e1; then the
     * balance with only the joints that move no goal and no pin (the torso and arms, when the hips are dragged with the
     * feet down), d2 = J2f^T (J2f J2f^T + l^2 I)^-1 (e2 - J2 d1). Those joints leave the goals exactly where they are,
     * so balance leans the body and never pulls a goal back (a damped null-space projection leaked: a crouch stopped
     * 5 cm short). */
    const int m1 = 3 * P;
    const double l2 = (double)s.damping * s.damping;
    for (int r = 0; r < m1; r++)
      for (int c = 0; c <= r; c++) {
        double sum = 0.0;
        for (int k = 0; k < n; k++) sum += J[(size_t)r * n + k] * J[(size_t)c * n + k];
        A[(size_t)r * m1 + c] = A[(size_t)c * m1 + r] = sum + (r == c ? l2 : 0.0);
      }
    std::vector<double> y(e.begin(), e.begin() + m1), A1(A.begin(), A.begin() + (size_t)m1 * m1);
    solve_small(A1, y, m1);
    std::vector<double> d((size_t)n, 0.0);
    for (int k = 0; k < n; k++)
      for (int r = 0; r < m1; r++) d[(size_t)k] += J[(size_t)r * n + k] * y[(size_t)r];
    if (balance) {
      std::vector<uint8_t> free_col((size_t)n, 1);
      for (int b = 0; b < B; b++)
        for (int p = 0; p < P; p++)
          if (pts[(size_t)p].above[(size_t)b] && pts[(size_t)p].bone != bones[(size_t)b]) free_col[(size_t)(3 * b)] = free_col[(size_t)(3 * b + 1)] = free_col[(size_t)(3 * b + 2)] = 0;
      for (int k = 3 * B; k < n; k++) free_col[(size_t)k] = 0;  // (the hips' place moves every point under them)
      double e2[2], B2[4] = {l2, 0.0, 0.0, l2};
      for (int q = 0; q < 2; q++) {
        e2[q] = e[(size_t)(m1 + q)];
        for (int k = 0; k < n; k++) e2[q] -= J[(size_t)(m1 + q) * n + k] * d[(size_t)k];
        for (int c = 0; c < 2; c++)
          for (int k = 0; k < n; k++)
            if (free_col[(size_t)k]) B2[q * 2 + c] += J[(size_t)(m1 + q) * n + k] * J[(size_t)(m1 + c) * n + k];
      }
      const double det = B2[0] * B2[3] - B2[1] * B2[2];
      if (std::fabs(det) > 1e-18) {
        const double y0 = (B2[3] * e2[0] - B2[1] * e2[1]) / det, y1 = (B2[0] * e2[1] - B2[2] * e2[0]) / det;
        for (int k = 0; k < n; k++)
          if (free_col[(size_t)k]) d[(size_t)k] += J[(size_t)m1 * n + k] * y0 + J[(size_t)(m1 + 1) * n + k] * y1;
      }
    }
    if (move_hips) {
      /* With the feet pinned the hips lean (30 cm sideways at most) and crouch (a metre down), no further: past what
       * the legs can bend, a far target would walk the body off its feet. */
      Vec3 off = hips->world_position() + Vec3((float)d[(size_t)(3 * B)], (float)d[(size_t)(3 * B + 1)], (float)d[(size_t)(3 * B + 2)]) - hips_start;
      if (!pins.empty()) {
        const float side = std::sqrt(off.x * off.x + off.z * off.z);
        if (side > s.max_hips_shift) off.x *= s.max_hips_shift / side, off.z *= s.max_hips_shift / side;
        off.y = std::max(-1.0f, std::min(s.max_hips_shift, off.y));
      }
      hips->set_world_position(hips_start + off);
    }
    const float pull = s.rest_pull * std::max(0.0f, 1.0f - (float)it / (float)main_its);  // (none by the end: the goals reached)
    for (int b = 0; b < B; b++) {  // (parents first: each joint turns about its head, what hangs below follows; 0.3 rad a step at most)
      GameObject *bone = bones[(size_t)b];
      Vec3 w = axes[(size_t)b][0] * (float)d[(size_t)(3 * b)] + axes[(size_t)b][1] * (float)d[(size_t)(3 * b + 1)] + axes[(size_t)b][2] * (float)d[(size_t)(3 * b + 2)];
      const float a = length(w);
      if (a > 0.3f) w = w * (0.3f / a);
      if (length(w) > 1e-9f) bone->set_world_rotation(normalize(Quat::axis_angle(w, length(w)) * bone->world_rotation()));
      Quat local = bone->local().rotation;
      /* A hinge turns only about its bend axis (from the starting pose): a knee doesn't bend sideways. */
      if (length(hinge[(size_t)b]) > 0.5f) {
        const Quat rel = normalize(local * start[(size_t)b].conjugate());
        const Vec3 ax = hinge[(size_t)b], v(rel.x, rel.y, rel.z);
        const Vec3 tw = ax * dot(v, ax);
        const Quat twist = normalize(Quat(tw.x, tw.y, tw.z, rel.w));
        local = normalize(twist * start[(size_t)b]);
      }
      /* Within its limit of the rest pose (or of where it started, if the animator had it further), pulled a little
       * back towards the starting pose. */
      const float lim = std::max((spine_like(*bone) ? 0.5f : length(hinge[(size_t)b]) > 0.5f ? 2.0f : 1.0f) * limit,  // (a knee bends to 150 degrees)
                                  length(rotation_vector(normalize(start[(size_t)b] * rest[(size_t)b].conjugate()))));
      const Quat rel = normalize(local * rest[(size_t)b].conjugate());
      const float ang = length(rotation_vector(rel));
      if (ang > lim) local = normalize(slerp(Quat(), rel, lim / ang) * rest[(size_t)b]);
      if (pull > 0.0f) local = normalize(slerp(local, start[(size_t)b], pull));
      bone->set_local_rotation(local);
    }
    for (const auto &h : held) bones[(size_t)h.first]->set_world_rotation(h.second);
    res.iterations = it + 1;
  }
  for (const Goal &g : goals)
    if (g.bone) res.error = std::max(res.error, length(g.target - g.bone->world_position()));
  for (int p = (int)goals.size(); p < P; p++) res.pin_error = std::max(res.pin_error, length(pts[(size_t)p].target - pts[(size_t)p].bone->world_position()));
  if (balance) {
    std::vector<ArmatureBone> now;
    armature_bones(*body->root, now);
    const Vec3 c = body::state(*body, now).com;
    res.balance = std::sqrt((c.x - support.x) * (c.x - support.x) + (c.z - support.z) * (c.z - support.z));
  }
  res.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  return res;
}

}  // namespace autopose
}  // namespace bl

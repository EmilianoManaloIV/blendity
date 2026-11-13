// SPDX-License-Identifier: GPL-2.0-or-later
#include "autopose.h"
#include "spring_bones.h"
#include "pose_counts.h"
#include "../core/core.h"
#include "../core/edit_epoch.h"
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

Result solve_reference(GameObject &armature, const std::vector<Goal> &goals, const std::vector<GameObject *> &pins, const body::Body *body, const Settings &s) {
  BL_ZONE("autopose solve (reference)");
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
  auto pin_at = [&](size_t k) { return s.pin_poses && k < s.pin_poses->size() ? (*s.pin_poses)[k].first : pins[k]->world_position(); };
  for (size_t k = 0; k < pins.size(); k++)
    if (pins[k]) pts.push_back({pins[k], pin_at(k), 10.0f, {}});  // (pins far firmer than goals: the feet don't slide to reach)
  for (Point &p : pts) {
    p.above.assign((size_t)B, 0);
    for (int b = index_of(p.bone); b >= 0; b = parent[(size_t)b]) p.above[(size_t)b] = 1;
  }
  /* The ends kept turned as they were (a planted foot stays flat, a dragged hand keeps its twist). */
  std::vector<std::pair<int, Quat>> held;
  for (const Point &p : pts) {
    const int i = index_of(p.bone);
    if (i < 0 || bones[(size_t)i] != p.bone) continue;
    Quat turn = p.bone->world_rotation();
    for (size_t k = 0; s.pin_poses && k < pins.size() && k < s.pin_poses->size(); k++)
      if (pins[k] == p.bone) turn = (*s.pin_poses)[k].second;  // (a pin's own turn from before the body moved)
    held.push_back({i, turn});
  }
  /* Balance over the pins on the ground (within 10 cm of the lowest): the feet's, not a locked hand's; one foot too. */
  std::vector<Vec3> ground;
  float low = 1e30f;
  for (size_t k = 0; k < pins.size(); k++)
    if (pins[k]) low = std::min(low, pin_at(k).y);
  for (size_t k = 0; k < pins.size(); k++)
    if (pins[k] && pin_at(k).y <= low + 0.1f) ground.push_back(pin_at(k));
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
  return res;  // (round 473: the reference counts nothing - the Profiler's solver rows are the buffer solver's)
}

/* ------------------------------------------------------------ the pose buffer (round 473, ADR 0127) */

namespace {

bool same_turn(const Quat &a, const Quat &b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; }

/* Forward kinematics over the nodes this solve reads: the same products, in the same order, as
 * GameObject::world_matrix() and world_rotation() make. */
void rig_fk(Rig &r) {
  for (Rig::Node &n : r.nodes) {
    if (!n.used) continue;
    const Mat4 local = Mat4::trs(n.local.position, n.local.rotation, n.local.scale);
    if (n.parent >= 0) {
      const Rig::Node &p = r.nodes[(size_t)n.parent];
      n.world = p.world * local, n.turn = normalize(p.turn * n.local.rotation);
      continue;
    }
    n.world = r.has_root ? r.root_world * local : local;
    n.turn = r.has_root ? normalize(r.root_turn * n.local.rotation) : n.local.rotation;
  }
}

/* A node's world turn from the locals as they are this instant (the held ends are turned one after another, and one
 * may hang under another). */
Quat rig_turn_now(const Rig &r, int node) {
  const Rig::Node &n = r.nodes[(size_t)node];
  if (n.parent >= 0) return normalize(rig_turn_now(r, n.parent) * n.local.rotation);
  return r.has_root ? normalize(r.root_turn * n.local.rotation) : n.local.rotation;
}

/* What set_world_rotation(q) leaves as a node's local turn. */
Quat rig_local_for(const Rig &r, int node, const Quat &parent_turn, const Quat &q) {
  const bool parented = r.nodes[(size_t)node].parent >= 0 || r.has_root;
  return normalize(parented ? parent_turn.conjugate() * q : q);
}

Vec3 rig_place(const Rig &r, const Rig::Scratch::Point &p) { return p.node >= 0 ? r.nodes[(size_t)p.node].world.translation() : p.fixed; }

void rig_mark_used(Rig &r, int node) {
  for (; node >= 0 && !r.nodes[(size_t)node].used; node = r.nodes[(size_t)node].parent) r.nodes[(size_t)node].used = true;
}

Vec3 rig_mass_center(const Rig &r, const Rig::Scratch::Mass &m) {
  const Mat4 &w = m.node >= 0 ? r.nodes[(size_t)m.node].world : m.fixed;
  return (w * m.bm->rest_inv).point(m.bm->center);
}

template <class T> size_t held_bytes(const std::vector<T> &v) { return v.capacity() * sizeof(T); }

}  // namespace

size_t Rig::scratch_bytes() const {
  const Scratch &c = scratch;
  return held_bytes(c.pts) + held_bytes(c.above) + held_bytes(c.free_col) + held_bytes(c.held) + held_bytes(c.ground) + held_bytes(c.sub_c) +
         held_bytes(c.hinge) + held_bytes(c.axes) + held_bytes(c.sub_m) + held_bytes(c.masses) + held_bytes(c.start) + held_bytes(c.J) +
         held_bytes(c.e) + held_bytes(c.A) + held_bytes(c.A1) + held_bytes(c.y) + held_bytes(c.d);
}

Rig build_rig(GameObject &armature) {
  Rig r;
  r.armature = &armature;
  r.structure = edit_epoch::structure();
  /* Every object under the armature, parents first (the walk the solver has always made). */
  std::vector<std::pair<GameObject *, int>> stack{{&armature, -1}};
  std::vector<int> depths;  // (below the armature, worked out once here: the joints are ordered by it)
  while (!stack.empty()) {
    const std::pair<GameObject *, int> top = stack.back();
    stack.pop_back();
    const int index = (int)r.nodes.size();
    Rig::Node n;
    n.g = top.first, n.parent = top.second;
    r.nodes.push_back(n);
    depths.push_back(top.second >= 0 ? depths[(size_t)top.second] + 1 : 0);
    r.node_of[top.first] = index;
    for (auto it = top.first->children.rbegin(); it != top.first->children.rend(); ++it) stack.push_back({*it, index});
  }
  /* The joints: the body bones, by depth (the order the Jacobian's columns have always had). */
  std::vector<int> order;
  for (int i = 0; i < (int)r.nodes.size(); i++)
    if (r.nodes[(size_t)i].g->get<Bone>() && !kept(*r.nodes[(size_t)i].g)) order.push_back(i);
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return depths[(size_t)a] < depths[(size_t)b]; });
  r.joints.resize(order.size());
  for (size_t j = 0; j < order.size(); j++) {
    Rig::Node &n = r.nodes[(size_t)order[j]];
    n.joint = (int)j;
    r.joints[j].node = order[j];
    r.joints[j].rest = n.g->get<Bone>()->rest_rotation;
    r.joints[j].hinge_like = hinge_like(*n.g), r.joints[j].spine_like = spine_like(*n.g);
  }
  for (Rig::Node &n : r.nodes) n.joint_above = n.joint >= 0 ? n.joint : n.parent >= 0 ? r.nodes[(size_t)n.parent].joint_above : -1;
  for (Rig::Joint &j : r.joints) {
    const int up = r.nodes[(size_t)j.node].parent;
    j.parent = up >= 0 ? r.nodes[(size_t)up].joint_above : -1;
  }
  read(r);
  return r;
}

void read_root(Rig &r) {
  if (!r.valid()) return;
  r.has_root = r.armature->parent != nullptr;
  r.root_world = r.has_root ? r.armature->parent->world_matrix() : Mat4();
  r.root_turn = r.has_root ? r.armature->parent->world_rotation() : Quat();
}

void read_pose(Rig &r) {
  if (!r.valid()) return;
  read_root(r);
  for (Rig::Node &n : r.nodes) n.local = n.g->local();
}

void read(Rig &r) {
  read_pose(r);
  for (Rig::Node &n : r.nodes) n.start = n.local;
}

void to_start(Rig &r) {
  for (Rig::Node &n : r.nodes) n.local = n.start;
}

bool stale(const Rig &r) { return !r.valid() || r.structure != edit_epoch::structure(); }

size_t write(Rig &r) {
  size_t n = 0;
  for (const Rig::Joint &j : r.joints) {
    const Rig::Node &node = r.nodes[(size_t)j.node];
    const Transform &now = node.g->local();
    const bool placed = !(now.position.x == node.local.position.x && now.position.y == node.local.position.y && now.position.z == node.local.position.z);
    const bool turned = !same_turn(now.rotation, node.local.rotation) && !same_turn(now.rotation, normalize(node.local.rotation));
    if (placed) node.g->set_local_position(node.local.position), n++;
    if (turned) node.g->set_local_rotation(node.local.rotation), n++;
    const bool scaled = !(now.scale.x == node.local.scale.x && now.scale.y == node.local.scale.y && now.scale.z == node.local.scale.z);
    if (scaled) node.g->set_local_scale(node.local.scale), n++;  // (changed mid-drag: back to the start, as the old full restore did)
  }
  return n;
}

Result solve(Rig &r, const std::vector<Goal> &goals, const std::vector<GameObject *> &pins, const body::Body *body, const Settings &s) {
  BL_ZONE("autopose solve");
  Result res;
  const auto t0 = std::chrono::steady_clock::now();
  const int B = (int)r.joints.size();
  res.bones = r.joints.size();
  if (!r.valid() || !B || (goals.empty() && pins.empty())) return res;
  for (const Goal &g : goals)
    if (!std::isfinite(g.target.x) || !std::isfinite(g.target.y) || !std::isfinite(g.target.z)) return res;  // (refused: nothing moves)
  Rig::Scratch &c = r.scratch;
  auto node_of = [&](const GameObject *g) {
    const auto it = r.node_of.find(g);
    return it == r.node_of.end() ? -1 : it->second;
  };
  GameObject *hips = r.hips ? r.hips : body ? body::hips_of(*r.armature, body) : body::hips_of(*r.armature);
  const int hips_node = hips ? node_of(hips) : -1;
  const int hips_i = hips_node >= 0 ? r.nodes[(size_t)hips_node].joint_above : -1;
  const bool move_hips = hips_i >= 0 && r.joints[(size_t)hips_i].node == hips_node;
  /* What this solve reads: the joints, the points' and the masses' bones, and everything above them. */
  for (Rig::Node &n : r.nodes) n.used = false;
  for (const Rig::Joint &j : r.joints) rig_mark_used(r, j.node);
  c.pts.clear();
  for (const Goal &g : goals) {
    if (!g.bone) continue;
    Rig::Scratch::Point p;
    p.bone = g.bone, p.node = node_of(g.bone), p.target = g.target, p.w = std::max(0.0f, g.weight);
    c.pts.push_back(p);
  }
  const int G = (int)c.pts.size();
  for (size_t k = 0; k < pins.size(); k++) {
    if (!pins[k]) continue;
    Rig::Scratch::Point p;
    p.bone = pins[k], p.pin = (int)k, p.node = node_of(pins[k]), p.w = 10.0f;  // (pins far firmer than goals: the feet don't slide to reach)
    c.pts.push_back(p);
  }
  const int P = (int)c.pts.size();
  for (Rig::Scratch::Point &p : c.pts) {
    if (p.node >= 0) rig_mark_used(r, p.node);
    p.joint = p.node >= 0 ? r.nodes[(size_t)p.node].joint_above : -1;
    p.own = p.joint >= 0 && r.joints[(size_t)p.joint].node == p.node;
  }
  c.masses.clear();
  const bool weighed = s.balance && body && body->valid();
  if (weighed)
    for (const body::BoneMass &bm : body->bones) {
      if (!(bm.mass > 0.0f) || !bm.object) continue;
      Rig::Scratch::Mass m;
      m.bm = &bm, m.node = node_of(bm.object);
      m.joint = m.node >= 0 ? r.nodes[(size_t)m.node].joint_above : -1;
      if (m.node >= 0) rig_mark_used(r, m.node);
      else m.fixed = bm.object->world_matrix();
      c.masses.push_back(m);
    }
  rig_fk(r);
  /* The starting pose and the bend axis of the hinges (knees, elbows: the plane they already bend in, else their own X). */
  c.start.resize((size_t)B);
  c.hinge.assign((size_t)B, Vec3(0.0f));
  for (int b = 0; b < B; b++) {
    const Rig::Joint &j = r.joints[(size_t)b];
    const Rig::Node &n = r.nodes[(size_t)j.node];
    c.start[(size_t)b] = n.local.rotation;
    if (!j.hinge_like || !n.g->parent) continue;
    const Quat above = n.parent >= 0 ? r.nodes[(size_t)n.parent].turn : r.root_turn;
    const Vec3 up = normalize(above.rotate(Vec3(0, 1, 0))), down = normalize(n.turn.rotate(Vec3(0, 1, 0)));
    Vec3 a = cross(up, down);
    if (length(a) < 1e-3f) a = n.turn.rotate(Vec3(1, 0, 0));
    c.hinge[(size_t)b] = normalize(above.conjugate().rotate(normalize(a)));  // (in the parent's space)
  }
  /* The points' places: a bone outside the armature where it is (read once), a pin where it is held. */
  for (int p = 0; p < P; p++) {
    Rig::Scratch::Point &pt = c.pts[(size_t)p];
    if (pt.node < 0) pt.fixed = pt.bone->world_position();
    if (p < G) continue;
    pt.target = s.pin_poses && (size_t)pt.pin < s.pin_poses->size() ? (*s.pin_poses)[(size_t)pt.pin].first : rig_place(r, pt);
  }
  c.above.assign((size_t)P * (size_t)B, 0);
  for (int p = 0; p < P; p++)
    for (int b = c.pts[(size_t)p].joint; b >= 0; b = r.joints[(size_t)b].parent) c.above[(size_t)p * B + b] = 1;
  /* The ends kept turned as they were (a planted foot stays flat, a dragged hand keeps its twist). */
  c.held.clear();
  for (const Rig::Scratch::Point &pt : c.pts) {
    if (!pt.own) continue;
    Quat turn = r.nodes[(size_t)pt.node].turn;
    for (size_t q = 0; s.pin_poses && q < pins.size() && q < s.pin_poses->size(); q++)
      if (pins[q] == pt.bone) turn = (*s.pin_poses)[q].second;  // (a pin's own turn from before the body moved)
    c.held.push_back({pt.joint, turn});
  }
  /* Balance over the pins on the ground (within 10 cm of the lowest): the feet's, not a locked hand's; one foot too. */
  c.ground.clear();
  float low = 1e30f;
  for (int p = G; p < P; p++) low = std::min(low, c.pts[(size_t)p].target.y);
  for (int p = G; p < P; p++)
    if (c.pts[(size_t)p].target.y <= low + 0.1f) c.ground.push_back(c.pts[(size_t)p].target);
  const bool balance = weighed && !c.ground.empty();
  Vec3 support(0.0f);
  for (const Vec3 &g : c.ground) support = support + g / (float)c.ground.size();
  if (!balance) c.masses.clear();
  const int m = 3 * P + (balance ? 2 : 0), n = 3 * B + (move_hips ? 3 : 0), m1 = 3 * P;
  c.J.resize((size_t)m * n), c.e.resize((size_t)m), c.A.resize((size_t)m * m), c.A1.resize((size_t)m1 * m1), c.y.resize((size_t)m1), c.d.resize((size_t)n);
  c.axes.resize((size_t)B * 3), c.free_col.resize((size_t)n), c.sub_c.resize((size_t)B), c.sub_m.resize((size_t)B);
  const float limit = s.limit_deg * 3.14159265f / 180.0f;
  const int main_its = std::max(1, s.iterations), settle = pins.empty() ? 0 : 60;
  const Vec3 hips_start = hips_node >= 0 ? r.nodes[(size_t)hips_node].world.translation() : hips ? hips->world_position() : Vec3(0.0f);
  auto worst_gap = [&](int from, int to) {
    float worst = 0.0f;
    for (int p = from; p < to; p++) worst = std::max(worst, length(c.pts[(size_t)p].target - rig_place(r, c.pts[(size_t)p])));
    return worst;
  };
  for (int it = 0; it < main_its + settle; it++) {
    if (it) rig_fk(r);
    if (it == main_its)
      for (int p = 0; p < G; p++) c.pts[(size_t)p].w *= 0.03f;
    if (it >= main_its && worst_gap(G, P) < 5e-4f) break;  // (settled: the pins back in place)
    std::fill(c.J.begin(), c.J.end(), 0.0);
    /* The center of mass and each joint's share of it: the masses at their joints, summed up the tree in one pass. */
    Vec3 com(0.0f);
    if (balance) {
      std::fill(c.sub_c.begin(), c.sub_c.end(), Vec3(0.0f));
      std::fill(c.sub_m.begin(), c.sub_m.end(), 0.0f);
      float total = 0.0f;
      for (const Rig::Scratch::Mass &mi : c.masses) {
        const Vec3 at = rig_mass_center(r, mi);
        com = com + at * mi.bm->mass, total += mi.bm->mass;
        if (mi.joint >= 0) c.sub_c[(size_t)mi.joint] = c.sub_c[(size_t)mi.joint] + at * mi.bm->mass, c.sub_m[(size_t)mi.joint] += mi.bm->mass;
      }
      com = com / std::max(1e-6f, total);
      for (int b = B - 1; b >= 0; b--) {
        const int up = r.joints[(size_t)b].parent;
        if (up >= 0) c.sub_c[(size_t)up] = c.sub_c[(size_t)up] + c.sub_c[(size_t)b], c.sub_m[(size_t)up] += c.sub_m[(size_t)b];
      }
    }
    for (int p = 0; p < P; p++) {
      /* Each goal's error at most 10 cm a step (Buss's clamping): a target out of reach can't drag the pins after it. */
      Vec3 gap = c.pts[(size_t)p].target - rig_place(r, c.pts[(size_t)p]);
      if (length(gap) > 0.1f) gap = gap * (0.1f / length(gap));
      const Vec3 d = gap * c.pts[(size_t)p].w;
      for (int a = 0; a < 3; a++) c.e[(size_t)(3 * p + a)] = d[a];
    }
    if (balance) {
      c.e[(size_t)(3 * P)] = (support.x - com.x) * s.balance_weight;
      c.e[(size_t)(3 * P + 1)] = (support.z - com.z) * s.balance_weight;
    }
    /* Each joint's axes: the world's three, or a hinge's one (its bend axis, now). */
    for (int b = 0; b < B; b++) {
      const Rig::Node &node = r.nodes[(size_t)r.joints[(size_t)b].node];
      Vec3 *ax = &c.axes[(size_t)b * 3];
      if (length(c.hinge[(size_t)b]) > 0.5f && node.g->parent) {
        const Quat above = node.parent >= 0 ? r.nodes[(size_t)node.parent].turn : r.root_turn;
        ax[0] = normalize(above.rotate(c.hinge[(size_t)b])), ax[1] = Vec3(0.0f), ax[2] = Vec3(0.0f);
        continue;
      }
      ax[0] = Vec3(1, 0, 0), ax[1] = Vec3(0, 1, 0), ax[2] = Vec3(0, 0, 1);
    }
    for (int b = 0; b < B; b++) {
      const Vec3 head = r.nodes[(size_t)r.joints[(size_t)b].node].world.translation();
      for (int k = 0; k < 3; k++) {
        const Vec3 axis = c.axes[(size_t)b * 3 + k];
        if (length(axis) < 0.5f) continue;
        const int col = 3 * b + k;
        for (int p = 0; p < P; p++) {
          const Rig::Scratch::Point &pt = c.pts[(size_t)p];
          if (!c.above[(size_t)p * B + b] || (pt.own && pt.joint == b)) continue;  // (a bone's own turn doesn't move its head)
          const Vec3 d = cross(axis, rig_place(r, pt) - head) * pt.w;
          for (int a = 0; a < 3; a++) c.J[(size_t)(3 * p + a) * n + col] = d[a];
        }
        if (balance && c.sub_m[(size_t)b] > 0.0f) {
          const Vec3 d = cross(axis, c.sub_c[(size_t)b] - head * c.sub_m[(size_t)b]) * (s.balance_weight / body->mass);
          c.J[(size_t)(3 * P) * n + col] = d.x, c.J[(size_t)(3 * P + 1) * n + col] = d.z;
        }
      }
    }
    if (move_hips)
      for (int k = 0; k < 3; k++) {
        const int col = 3 * B + k;
        for (int p = 0; p < P; p++)
          if (c.above[(size_t)p * B + hips_i]) c.J[(size_t)(3 * p + k) * n + col] = c.pts[(size_t)p].w;
        if (balance && k != 1) c.J[(size_t)(3 * P + (k == 0 ? 0 : 1)) * n + col] = s.balance_weight * c.sub_m[(size_t)hips_i] / body->mass;
      }
    /* Damped least squares in two priorities, as solve_reference: the goals and pins first, then the balance with
     * only the joints that move no goal and no pin. */
    const double l2 = (double)s.damping * s.damping;
    for (int row = 0; row < m1; row++)
      for (int col = 0; col <= row; col++) {
        double sum = 0.0;
        for (int k = 0; k < n; k++) sum += c.J[(size_t)row * n + k] * c.J[(size_t)col * n + k];
        c.A[(size_t)row * m1 + col] = c.A[(size_t)col * m1 + row] = sum + (row == col ? l2 : 0.0);
      }
    std::copy(c.e.begin(), c.e.begin() + m1, c.y.begin());
    std::copy(c.A.begin(), c.A.begin() + (size_t)m1 * m1, c.A1.begin());
    solve_small(c.A1, c.y, m1);
    std::fill(c.d.begin(), c.d.end(), 0.0);
    for (int k = 0; k < n; k++)
      for (int row = 0; row < m1; row++) c.d[(size_t)k] += c.J[(size_t)row * n + k] * c.y[(size_t)row];
    if (balance) {
      std::fill(c.free_col.begin(), c.free_col.end(), (uint8_t)1);
      for (int b = 0; b < B; b++)
        for (int p = 0; p < P; p++)
          if (c.above[(size_t)p * B + b] && !(c.pts[(size_t)p].own && c.pts[(size_t)p].joint == b))
            c.free_col[(size_t)(3 * b)] = c.free_col[(size_t)(3 * b + 1)] = c.free_col[(size_t)(3 * b + 2)] = 0;
      for (int k = 3 * B; k < n; k++) c.free_col[(size_t)k] = 0;  // (the hips' place moves every point under them)
      double e2[2], B2[4] = {l2, 0.0, 0.0, l2};
      for (int q = 0; q < 2; q++) {
        e2[q] = c.e[(size_t)(m1 + q)];
        for (int k = 0; k < n; k++) e2[q] -= c.J[(size_t)(m1 + q) * n + k] * c.d[(size_t)k];
        for (int col = 0; col < 2; col++)
          for (int k = 0; k < n; k++)
            if (c.free_col[(size_t)k]) B2[q * 2 + col] += c.J[(size_t)(m1 + q) * n + k] * c.J[(size_t)(m1 + col) * n + k];
      }
      const double det = B2[0] * B2[3] - B2[1] * B2[2];
      if (std::fabs(det) > 1e-18) {
        const double y0 = (B2[3] * e2[0] - B2[1] * e2[1]) / det, y1 = (B2[0] * e2[1] - B2[2] * e2[0]) / det;
        for (int k = 0; k < n; k++)
          if (c.free_col[(size_t)k]) c.d[(size_t)k] += c.J[(size_t)m1 * n + k] * y0 + c.J[(size_t)(m1 + 1) * n + k] * y1;
      }
    }
    if (move_hips) {
      /* With the feet pinned the hips lean (30 cm sideways at most) and crouch (a metre down), no further. */
      Rig::Node &hn = r.nodes[(size_t)hips_node];
      const Vec3 step((float)c.d[(size_t)(3 * B)], (float)c.d[(size_t)(3 * B + 1)], (float)c.d[(size_t)(3 * B + 2)]);
      Vec3 off = hn.world.translation() + step - hips_start;
      if (!pins.empty()) {
        const float side = std::sqrt(off.x * off.x + off.z * off.z);
        if (side > s.max_hips_shift) off.x *= s.max_hips_shift / side, off.z *= s.max_hips_shift / side;
        off.y = std::max(-1.0f, std::min(s.max_hips_shift, off.y));
      }
      const bool parented = hn.parent >= 0 || r.has_root;
      const Mat4 &above = hn.parent >= 0 ? r.nodes[(size_t)hn.parent].world : r.root_world;
      hn.local.position = parented ? above.inverse().point(hips_start + off) : hips_start + off;
    }
    const float pull = s.rest_pull * std::max(0.0f, 1.0f - (float)it / (float)main_its);  // (none by the end: the goals reached)
    /* Parents first: each joint turns about its head, what hangs below follows; 0.3 rad a step at most. The nodes
     * between joints only carry the turn down. */
    for (Rig::Node &node : r.nodes) {
      if (!node.used) continue;
      const bool parented = node.parent >= 0 || r.has_root;
      const Quat above = node.parent >= 0 ? r.nodes[(size_t)node.parent].turn : r.root_turn;
      node.turn = parented ? normalize(above * node.local.rotation) : node.local.rotation;
      const int b = node.joint;
      if (b < 0) continue;
      const Vec3 *ax = &c.axes[(size_t)b * 3];
      Vec3 w = ax[0] * (float)c.d[(size_t)(3 * b)] + ax[1] * (float)c.d[(size_t)(3 * b + 1)] + ax[2] * (float)c.d[(size_t)(3 * b + 2)];
      const float a = length(w);
      if (a > 0.3f) w = w * (0.3f / a);
      Quat local = node.local.rotation;
      if (length(w) > 1e-9f) local = rig_local_for(r, r.joints[(size_t)b].node, above, normalize(Quat::axis_angle(w, length(w)) * node.turn));
      /* A hinge turns only about its bend axis (from the starting pose): a knee doesn't bend sideways. */
      const bool hinged = length(c.hinge[(size_t)b]) > 0.5f;
      if (hinged) {
        const Quat rel = normalize(local * c.start[(size_t)b].conjugate());
        const Vec3 axh = c.hinge[(size_t)b], v(rel.x, rel.y, rel.z);
        const Vec3 tw = axh * dot(v, axh);
        local = normalize(normalize(Quat(tw.x, tw.y, tw.z, rel.w)) * c.start[(size_t)b]);
      }
      /* Within its limit of the rest pose (or of where it started, if the animator had it further), pulled a little
       * back towards the starting pose. */
      const Rig::Joint &j = r.joints[(size_t)b];
      const float lim = std::max((j.spine_like ? 0.5f : hinged ? 2.0f : 1.0f) * limit,  // (a knee bends to 150 degrees)
                                  length(rotation_vector(normalize(c.start[(size_t)b] * j.rest.conjugate()))));
      const Quat rel = normalize(local * j.rest.conjugate());
      const float ang = length(rotation_vector(rel));
      if (ang > lim) local = normalize(slerp(Quat(), rel, lim / ang) * j.rest);
      if (pull > 0.0f) local = normalize(slerp(local, c.start[(size_t)b], pull));
      node.local.rotation = normalize(local);
      node.turn = parented ? normalize(above * node.local.rotation) : node.local.rotation;
    }
    for (const auto &h : c.held) {
      Rig::Node &node = r.nodes[(size_t)r.joints[(size_t)h.first].node];
      const Quat above = node.parent >= 0 ? rig_turn_now(r, node.parent) : r.root_turn;
      node.local.rotation = rig_local_for(r, r.joints[(size_t)h.first].node, above, h.second);
    }
    res.iterations++;
  }
  rig_fk(r);
  res.error = worst_gap(0, G), res.pin_error = worst_gap(G, P);
  if (balance) {
    Vec3 com(0.0f);
    float total = 0.0f;
    for (const Rig::Scratch::Mass &mi : c.masses) com = com + rig_mass_center(r, mi) * mi.bm->mass, total += mi.bm->mass;
    com = com / std::max(1e-6f, total);
    res.balance = std::sqrt((com.x - support.x) * (com.x - support.x) + (com.z - support.z) * (com.z - support.z));
  }
  res.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  pose_add(PoseStat::Solver, res.ms);
  pose_add_solver_iterations((uint64_t)std::max(0, res.iterations));
  return res;
}

Result solve(GameObject &armature, const std::vector<Goal> &goals, const std::vector<GameObject *> &pins, const body::Body *body, const Settings &s) {
  Rig rig = build_rig(armature);
  Result res = solve(rig, goals, pins, body, s);
  if (res.iterations > 0) res.writes = write(rig);
  pose_add_solver_writes(res.writes);
  return res;
}

}  // namespace autopose
}  // namespace bl

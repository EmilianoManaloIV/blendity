// SPDX-License-Identifier: GPL-2.0-or-later
// Motion trails and onion skin (task 0282, ADR 0087): the rig's posed frames around the playhead, sampled a few each
// editor frame (the clip applied and IK solved, the scene put back) and kept until the clip changes. A trail is the path
// of a hand, a foot, the head or the hips, its keys as dots: drag a dot to pose that frame (Puppet Posing, with Reach),
// Alt-drag it sideways to move the key in time. Onion skin draws the frames before and after as tinted ghosts.
#include "editor.h"

#include "../scene/humanoid.h"

#include <cstring>

namespace bl {

/* A pose as a ghost: each bone a line from its head to its tip. */
static void onion_ghost(Renderer3D &r3d, const std::vector<std::pair<Vec3, Vec3>> &bones, uint32_t col) {
  for (const auto &b : bones) r3d.line(b.first, b.second, col, false);
}

namespace {

constexpr int kTrailSpan = 30;     // frames each side of the playhead
constexpr int kSamplesPerFrame = 6;  // posed frames sampled each editor frame (the rest the next)
constexpr float kRetimePx = 8.0f;  // pixels for one frame of an Alt-drag

GameObject *rig_of(GameObject *g) {
  for (GameObject *a = g; a; a = a->parent)
    if (a->get<Armature>()) return a;
  return nullptr;
}

}  // namespace

/* The cache made current for the clip and the rig: a changed clip or rig starts it again; then a few more frames. */
void Editor::trail_refresh() {
  AnimationClipPtr clip = anim_clip();
  GameObject *root = anim_root();
  GameObject *arm = rig_of(active_object());
  TrailCache &c = trails_;
  if (!clip || !root || !arm || !(trails_.show || trails_.onion > 0 || zoe_.on)) return c.frames.clear(), c.todo.clear(), void(c.clip = nullptr);
  const uint64_t h = clip->version ^ (uint64_t)(uintptr_t)clip.get();  // (any key edit bumps the version)
  if (c.clip != clip.get() || c.hash != h || c.arm != arm->id) {
    c.clip = clip.get(), c.hash = h, c.arm = arm->id;
    c.frames.clear(), c.todo.clear();
  }
  const int now = (int)std::lround(anim_.frame), end = (int)std::ceil(clip->length);
  for (int f = std::max(0, now - kTrailSpan); f <= std::min(end, now + kTrailSpan); f++)
    if (!c.frames.count(f) && std::find(c.todo.begin(), c.todo.end(), f) == c.todo.end()) c.todo.push_back(f);
  if (c.todo.empty()) return;
  std::sort(c.todo.begin(), c.todo.end(), [&](int a, int b) { return std::abs(a - now) < std::abs(b - now); });  // (nearest first)
  const body::PoseGuard guard(*scene_, *root, *clip);  // (the pose and the animated values back after: task 0284's review)
  std::vector<GameObject *> bones;
  scene_->for_each([&](GameObject &g) {
    if (g.get<Bone>() && scene_->is_ancestor(arm, &g)) bones.push_back(&g);
  });
  for (int n = 0; n < kSamplesPerFrame && !c.todo.empty(); n++) {
    const int f = c.todo.front();
    c.todo.erase(c.todo.begin());
    apply_clip(*clip, *root, (float)f);
    solve_constraints(*scene_);
    auto &pose = c.frames[f];
    for (GameObject *b : bones) pose.push_back({b->id, {b->world_position(), body::bone_tip(*b)}});
  }
  ui_.redraw = true;
}

/* The bones the trails follow: the selected handles, else the hands. */
std::vector<uint64_t> Editor::trail_bones() {
  std::vector<uint64_t> out;
  const auto &handles = rig_ends();
  for (uint64_t id : handles)
    if (GameObject *g = scene_->find(id))
      if (is_selected(g->id)) out.push_back(id);
  if (out.empty() && handles.size() >= 2) out = {handles[0], handles[1]};
  return out;
}

/* A bone's place at a cached frame (false when the frame isn't sampled yet). */
bool Editor::trail_at(uint64_t bone, int f, Vec3 &head) const {
  const auto it = trails_.frames.find(f);
  if (it == trails_.frames.end()) return false;
  for (const auto &b : it->second)
    if (b.first == bone) return head = b.second.first, true;
  return false;
}

/* The rig's pose keys on a bone's trail: every frame any of the rig's bones or controls has a key on (posing keys the
 * whole body, and an IK hand's keys are on its control). */
std::vector<int> Editor::trail_keys(uint64_t bone) {
  std::vector<int> out;
  AnimationClipPtr clip = anim_clip();
  GameObject *root = anim_root(), *g = scene_->find(bone);
  GameObject *arm = g ? rig_of(g) : nullptr;
  if (!clip || !root || !arm) return out;
  std::vector<std::string> paths;
  scene_->for_each([&](GameObject &o) {
    if (&o == arm || scene_->is_ancestor(arm, &o)) paths.push_back(anim_path(*root, o));
  });
  for (const AnimTrack &t : clip->tracks)
    if (std::find(paths.begin(), paths.end(), t.path) != paths.end())
      for (const AnimKey &k : t.keys) out.push_back((int)std::lround(k.frame));
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

void Editor::draw_trails() {
  if (trails_.frames.empty()) return;
  draw_onion();
  if (!trails_.show) return;
  const int now = (int)std::lround(anim_.frame);
  for (uint64_t id : trail_bones()) {
    Vec3 prev;
    bool have = false;
    for (const auto &fr : trails_.frames) {
      Vec3 p;
      if (!trail_at(id, fr.first, p)) continue;
      if (have) scene_r3d_.line(prev, p, fr.first <= now ? Color::hex(0x60A0FF, 220) : Color::hex(0x70E070, 220), false);
      scene_r3d_.point(p, (float)ui_.px(2), Color::hex(0xD0D0D0, 160), false);
      prev = p, have = true;
    }
    for (int k : trail_keys(id)) {
      Vec3 p;
      if (!trail_at(id, k, p)) continue;
      const bool hot = trails_.drag_bone == id && trails_.drag_key == k;
      scene_r3d_.point(p, (float)ui_.px(hot ? 7 : 5), hot ? Color::hex(0xFFE060, 255) : Color::hex(0xFFFFFF, 240), false);
    }
  }
}

/* Onion skin: `onion` frames before (blue) and after (green) the playhead, every `onion_step` frames, fading out. */
void Editor::draw_onion() {
  if (trails_.onion <= 0) return;
  const int now = (int)std::lround(anim_.frame);
  for (int k = trails_.onion; k >= 1; k--)
    for (int side : {-1, 1}) {
      const auto it = trails_.frames.find(now + side * k * trails_.onion_step);
      if (it == trails_.frames.end()) continue;
      std::vector<std::pair<Vec3, Vec3>> bones;
      for (const auto &b : it->second) bones.push_back(b.second);
      const uint8_t a = (uint8_t)std::max(40, 200 - 50 * (k - 1));
      onion_ghost(scene_r3d_, bones, side < 0 ? Color::hex(0x5090FF, a) : Color::hex(0x60D060, a));
    }
}

/* A press on a trail's key dot: a drag poses that frame; with Alt, a sideways drag moves the key in time. */
bool Editor::trail_update(const Recti &view) {
  auto &in = ui_.in;
  if (!trails_.show || trails_.frames.empty()) return trails_.drag_bone = 0, false;
  const float x = (float)(in.mx - view.x), y = (float)(in.my - view.y);
  if (trails_.drag_bone) return trail_drag(x, y);
  const bool press = scene_hovered_ && in.pressed[0] && !ui_.any_active();
  for (uint64_t id : trail_bones())
    for (int k : trail_keys(id)) {
      Vec3 p;
      Vec2 sp;
      float depth = 0.0f;
      GameObject *g = scene_->find(id);
      if (!g || !trail_at(id, k, p) || !scene_r3d_.project(p, sp, depth)) continue;
      if (ui_.record_buttons) ui_.button_rects[strprintf("trail:%s %d", g->name.c_str(), k)] = Recti{view.x + (int)sp.x - 5, view.y + (int)sp.y - 5, 10, 10};
      if (!press || length(Vec2(sp.x - x, sp.y - y)) > (float)ui_.px(8)) continue;
      trails_.drag_bone = id, trails_.drag_key = k, trails_.retime = in.alt(), trails_.press = Vec2(x, y);
      if (!trails_.retime && !autopose_.on) {  // (posing a frame is AutoPosing's)
        refuse("Posing from a trail needs AutoPosing (Alt-drag moves the key in time without it)", "autopose on", "Turn AutoPosing on");
        trails_.drag_bone = 0;
        ui_.consume_click();
        return true;
      }
      if (!trails_.retime) {  // (the playhead to the key, then the bone's handle drag from there: one code path poses)
        anim_set_frame((float)k);
        puppet_.dragging = id, puppet_.start = g->world_position(), puppet_.press = Vec2(x, y), puppet_.moved = false;
        puppet_.normal = scene_r3d_.screen_ray(x, y).dir;
        trails_.drag_bone = 0;
      }
      ui_.consume_click();
      return true;
    }
  return false;
}

bool Editor::trail_drag(float x, float y) {  // (an Alt-drag: the key's new frame on release)
  auto &in = ui_.in;
  (void)y;
  if (in.down[0]) return true;
  const int moved = (int)std::lround((x - trails_.press.x) / kRetimePx);
  const uint64_t id = trails_.drag_bone;
  const int from = trails_.drag_key;
  trails_.drag_bone = 0;
  if (moved != 0) trail_retime(id, from, from + moved);
  return true;
}

/* The rig's keys on frame `from` moved to `to`, kept strictly between the keys around them (task 0282: a retime moves
 * keys, it doesn't resample). One undo step. */
void Editor::trail_retime(uint64_t bone, int from, int to) {
  GameObject *arm = rig_of(scene_->find(bone));
  GameObject *root = anim_root();
  AnimationClipPtr clip = anim_clip();
  if (!arm || !root || !clip) return;
  const std::vector<int> keys = trail_keys(bone);
  const auto at = std::find(keys.begin(), keys.end(), from);
  if (at == keys.end()) return;
  const int lo = at == keys.begin() ? 0 : *(at - 1) + 1, hi = at + 1 == keys.end() ? (int)std::ceil(clip->length) : *(at + 1) - 1;
  to = std::max(lo, std::min(hi, to));
  if (to == from) return Log::info("Retime: frame %d can't pass its neighbouring keys", from);
  std::vector<std::string> paths;
  scene_->for_each([&](GameObject &g) {
    if (scene_->is_ancestor(arm, &g) || &g == arm) paths.push_back(anim_path(*root, g));
  });
  size_t moved = 0;
  anim_edit_keys("Retime Key", [&](AnimationClip &c) {
    for (AnimTrack &t : c.tracks) {
      if (std::find(paths.begin(), paths.end(), t.path) == paths.end()) continue;
      const int i = t.key_at((float)from);
      if (i < 0) continue;
      AnimKey k = t.keys[(size_t)i];
      t.keys.erase(t.keys.begin() + i);
      k.frame = (float)to;
      t.put_key(k);
      moved++;
    }
  });
  Log::info("Retime: %zu keys moved from frame %d to %d", moved, from, to);
}

/* What the handles, the trails and the onion skin draw, as one number for the Scene view's picture cache: their switches,
 * the pins, the hovered and dragged handle, the playhead (their colours), the clip and how much of it is sampled. */
uint64_t Editor::puppet_overlay_key() const {
  uint64_t h = 1469598103934665603ull;
  auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
  mix((uint64_t)autopose_.on), mix(puppet_.hover), mix(puppet_.dragging), mix(trails_.drag_bone);
  for (uint64_t id : autopose_.locks) mix(id);
  mix((uint64_t)trails_.show), mix((uint64_t)trails_.onion), mix((uint64_t)trails_.onion_step);
  uint32_t f;
  std::memcpy(&f, &anim_.frame, 4);
  if (autopose_.on) mix(f), mix(puppet_.arm), mix(puppet_.key);  // (a handle's pin can be a contact at the playhead)
  if (!trails_.show && trails_.onion <= 0) return h;
  mix(f), mix(trails_.hash), mix((uint64_t)trails_.frames.size()), mix(trails_.arm);
  for (uint64_t id : selection_) mix(id);  // (the trails follow the selected ends)
  return h;
}

/* anim trails on|off | onion off|<frames> [step] (task 0282) */
bool Editor::trail_command(const std::vector<std::string> &t) {
  const std::string verb = t.size() > 1 ? to_lower(t[1]) : "";
  if (verb == "trails") {
    trails_.show = !(t.size() > 2 && to_lower(t[2]) == "off");
    Log::info("Motion trails %s", trails_.show ? "on: the selected hands', feet', head's or hips' paths, their keys as dots" : "off");
    return true;
  }
  if (verb != "onion") return false;
  trails_.onion = t.size() > 2 && to_lower(t[2]) != "off" ? std::max(0, std::min(10, std::atoi(t[2].c_str()))) : 0;
  if (t.size() > 3) trails_.onion_step = std::max(1, std::min(30, std::atoi(t[3].c_str())));
  Log::info("Onion skin: %d frames each side, every %d", trails_.onion, trails_.onion_step);
  return true;
}

}  // namespace bl

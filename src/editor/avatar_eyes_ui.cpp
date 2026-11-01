// SPDX-License-Identifier: GPL-2.0-or-later
// The Avatar window's Eye Tracking and Texture Atlas (round 360, task 0337), after the CATS plugin's (its Material
// Combiner atlas): each a button and an `avatar <verb>` - the eyes are scene/avatar_eyes', the atlas the texture bake's.
#include "editor.h"

#include "../scene/avatar_eyes.h"
#include "../scene/avatar_fix.h"
#include "../core/core.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <tuple>

namespace bl {

/* avatar eyes: the eye bones and the split blinks (the window's options). */
bool Editor::avatar_eyes(GameObject &root) {
  std::vector<std::string> lines;
  std::string why;
  const bool ok = setup_eyes(*scene_, root, eye_options_, lines, why);
  for (const std::string &l : lines) Log::info("%s", l.c_str());
  if (!ok) return Log::warn("Eye Tracking: %s", why.c_str()), false;
  Log::info("Eye Tracking: set up on %s (VRChat's Avatar Descriptor > Eye Look takes LeftEye and RightEye)", root.name.c_str());
  return true;
}

/* avatar eyetest <yaw> <pitch> <blink> | reset: CATS' test - the eyes turned, the blinks shut; not an edit to keep: the
 * next edit, save or export puts them back first (review 360: a test blink had shipped shut). */
void Editor::avatar_eye_test(GameObject &root, const std::vector<std::string> &t) {
  const bool reset = t.size() < 3 || to_lower(t[2]) == "reset";
  const float yaw = reset ? 0.0f : (float)std::atof(t[2].c_str());
  const float pitch = reset || t.size() < 4 ? 0.0f : (float)std::atof(t[3].c_str());
  const float blink = reset || t.size() < 5 ? 0.0f : (float)std::atof(t[4].c_str());
  if (!std::isfinite(yaw) || !std::isfinite(pitch) || !std::isfinite(blink)) return Log::warn("Eye test: numbers, please (yaw and pitch in degrees, blink 0 to 1)");
  eye_test(*scene_, root, yaw, pitch, std::clamp(blink, 0.0f, 1.0f));
  eye_test_root_ = reset ? 0 : root.id;
  ui_.redraw = true;
}

/* The eye test's pose and blinks undone (before an edit is kept, a save or an export). */
void Editor::eye_test_off() {
  if (!eye_test_root_) return;
  if (GameObject *root = scene_->find(eye_test_root_)) eye_test(*scene_, *root, 0.0f, 0.0f, 0.0f);
  eye_test_root_ = 0;
}

/* avatar atlas [size]: each of the avatar's meshes baked into one texture set (its materials side by side on new UVs,
 * as the CATS plugin's Material Combiner packs them), one material, one slot. */
bool Editor::avatar_atlas(GameObject &root, int size) {
  put_at_rest(root);  // (review 360: a posed mesh had been laid out in its pose)
  const std::vector<uint64_t> keep = selection_;
  const uint64_t keep_active = active_;
  int n = 0;
  std::string skipped;
  for (GameObject *g : avatar_meshes(*scene_, root)) {
    const MeshRenderer *gr = g->get<MeshRenderer>();
    if (!gr || gr->materials.size() < 2) continue;  // (one material already: nothing to combine)
    selection_ = {g->id};
    active_ = g->id;
    if (!bake_object_textures(size, false)) {
      skipped += (skipped.empty() ? "" : ", ") + g->name;
      continue;
    }
    MeshRenderer *mr = g->get<MeshRenderer>();
    Mesh &m = *mesh_make_mutable(g->get<MeshFilter>()->mesh);
    m.face_material.assign(m.face_count(), 0);  // (every slot holds the baked material: one slot)
    m.touch();
    if (mr && !mr->materials.empty()) mr->materials.resize(1);
    n++;
  }
  selection_ = keep;
  active_ = keep_active;
  if (!skipped.empty()) Log::warn("Texture Atlas: not baked: %s (a Decimate, Mirror or Outline changes its geometry - apply it first)", skipped.c_str());
  if (!n) return Log::warn("Texture Atlas: no mesh of two or more materials baked"), false;
  Log::info("Texture Atlas: %d mesh(es), each one %d x %d texture set and one material", n, size);
  return true;
}

bool Editor::avatar_eyes_command(const std::string &verb, const std::vector<std::string> &t, GameObject &root) {
  if (verb == "eyetest") return avatar_eye_test(root, t), true;
  bool changed = false;
  if (verb == "eyes") changed = avatar_eyes(root);
  else if (verb == "atlas") changed = avatar_atlas(root, std::clamp(t.size() > 2 ? std::atoi(t[2].c_str()) : 2048, 256, 4096));
  else return false;
  avatar_cache_.at = -1.0;
  if (changed) mark_changed("Avatar: " + verb);
  return true;
}

/* CATS' Eye Tracking (with its test) and the Texture Atlas. */
void Editor::draw_eyes_section(ui::Layout &lay, GameObject *root) {
  auto &u = ui_;
  lay.space(u.px(4));
  u.label(lay.row(), "Eye Tracking", u.theme.text_bright);
  for (auto [label, on, tip] : {std::tuple<const char *, bool *, const char *>{"Eye Movement", &eye_options_.movement, "LeftEye and RightEye bones at the eyes, the eyes' vertices following them (CATS' Disable Eye Movement unticks it)."},
                                 {"Eye Blinking", &eye_options_.blinking, "A Blink key (and LowerLid) split into VRChat's left and right keys (CATS' Disable Eye Blinking unticks it)."}}) {
    Recti r = lay.row();
    const int box = r.h - u.px(4);
    u.checkbox({r.x + u.px(4), r.y + u.px(2), box, box}, *on);
    u.label({r.x + box + u.px(10), r.y, r.w - box - u.px(14), r.h}, label);
    u.tooltip(tip);
  }
  const bool on = root != nullptr;
  auto run = [this](const char *l) { return [this, l] { command(l); }; };
  button_grid(lay, {{"Set Up Eyes", "The eye bones and the blink keys, as the CATS plugin's Eye Tracking. Console: avatar eyes.", on, run("avatar eyes")},
                    {"Test: Look Left", "The eyes turned 25 degrees (not kept: Reset puts them back). Console: avatar eyetest 25 0 0.", on, run("avatar eyetest 25 0 0")},
                    {"Test: Blink", "The blinks shut. Console: avatar eyetest 0 0 1.", on, run("avatar eyetest 0 0 1")},
                    {"Test: Reset", "The eyes and blinks back. Console: avatar eyetest reset.", on, run("avatar eyetest reset")}},
              2);
  lay.space(u.px(4));
  u.label(lay.row(), "Texture Atlas", u.theme.text_bright);
  button_grid(lay, {{"Make Atlas (2048)", "Each of the avatar's meshes baked into one 2048 texture set and one material - a draw call each (CATS' Material Combiner; Unity imports at 2048). Console: avatar atlas [size].", on, run("avatar atlas 2048")}}, 1);
}

}  // namespace bl

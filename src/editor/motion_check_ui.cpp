// SPDX-License-Identifier: GPL-2.0-or-later
// Motion Check, the smooth brush and Lock in Place in the editor (task 0284, ADR 0087): the Animation window's Motion
// Check button reports what it finds in the Report dialog, each row going to its frame and carrying its Fix; Smooth
// turns a drag over the dope sheet into a brush that evens the selected bones' keys under it; Lock in Place holds the
// selected hand or foot from the playhead to the rig's next key. Each is one undo step.
#include "editor.h"

#include "../scene/motion_check.h"

namespace bl {

namespace {
GameObject *rig_above_of(GameObject *g) {
  for (GameObject *a = g; a; a = a->parent)
    if (a->get<Armature>()) return a;
  return nullptr;
}
}  // namespace

/* The bones a brush or a lock works on: the selected ones under the animated root, else every bone of its rig. */
std::vector<std::string> Editor::anim_brush_paths() {
  std::vector<std::string> out;
  GameObject *root = anim_root();
  if (!root) return out;
  scene_->for_each([&](GameObject &g) {
    if (g.get<Bone>() && scene_->is_ancestor(root, &g) && is_selected(g.id)) out.push_back(anim_path(*root, g));
  });
  if (!out.empty()) return out;
  scene_->for_each([&](GameObject &g) {
    if (g.get<Bone>() && scene_->is_ancestor(root, &g)) out.push_back(anim_path(*root, g));
  });
  return out;
}

/* Motion Check: the clip checked, its findings shown with a jump and a Fix each. */
void Editor::motion_check_run() {
  GameObject *root = anim_root();
  AnimationClipPtr clip = anim_clip();
  if (!root || !clip) return refuse("Motion Check: select an animated character with a clip", "window Animation", "Open the Animation window");
  ScopedTimer t;
  motion_issues_ = motion::check(*scene_, *root, *clip);
  std::vector<std::string> lines;
  std::vector<std::pair<std::string, std::string>> actions;
  for (size_t i = 0; i < motion_issues_.size(); i++) {
    lines.push_back(motion_issues_[i].text());
    actions.push_back({strprintf("anim frame %d", motion_issues_[i].from), strprintf("motioncheck fix %zu", i)});
  }
  if (lines.empty()) lines.push_back("Nothing found: no sliding feet, pops or shaking.");
  show_report(strprintf("Motion Check (%.0f ms)", t.ms()), lines);
  report_actions_ = actions;  // (after show_report, which clears them)
}

/* One finding fixed: Lock in Place for a slide, smoothing for a pop (its frames) or a shake (its stretch). */
void Editor::motion_check_fix(size_t i) {
  GameObject *root = anim_root();
  if (!root || i >= motion_issues_.size()) return Log::warn("motioncheck fix <n>: run Motion Check first");
  const motion::Issue is = motion_issues_[i];
  GameObject *bone = nullptr;
  scene_->for_each([&](GameObject &g) {
    if (!bone && g.name == is.bone && scene_->is_ancestor(root, &g)) bone = &g;
  });
  if (!bone) return Log::warn("Motion Check: %s isn't under the animated object any more", is.bone.c_str());
  size_t n = 0;
  anim_edit_keys(is.kind == motion::Issue::Sliding ? "Lock in Place" : "Smooth Motion", [&](AnimationClip &c) {
    if (is.kind == motion::Issue::Sliding) n = (size_t)motion::lock_in_place(*scene_, *root, c, *bone, is.from, is.to, 3);
    else n = motion::smooth(c, {anim_path(*root, *bone)}, (float)is.from, (float)is.to, 1.0f, is.kind == motion::Issue::Pop ? 2.0f : 3.0f);
  });
  Log::info("Motion Check: %s - %zu %s", is.text().c_str(), n, is.kind == motion::Issue::Sliding ? "frames locked" : "keys smoothed");
  if (dialog_ == Dialog::Report && !report_actions_.empty()) motion_check_run();  // (the report again, without what was fixed)
}

/* The smooth brush: a drag over the sheet evens the brush's bones' keys within its radius of the frame under it. */
bool Editor::anim_smooth_brush(const Recti &sheet, float end) {
  auto &in = ui_.in;
  if (!anim_.smooth) return false;
  if (in.pressed[0]) anim_.smooth_stroke = ui_.hovered(sheet), anim_.smooth_last_x = -100000;  // (a stroke begun elsewhere isn't the brush's)
  if (!in.down[0] || !anim_.smooth_stroke) return anim_.smooth_stroke = anim_.smooth_stroke && in.down[0], false;
  if (std::abs(in.mx - anim_.smooth_last_x) < ui_.px(2)) return true;  // (by the pointer's travel, not the frames it is held for)
  anim_.smooth_last_x = in.mx;
  GameObject *root = anim_root();
  if (!root || !anim_clip()) return false;
  const float f = anim_frame_at(sheet, in.mx, end), r = (float)anim_.smooth_radius;
  anim_edit_keys("Smooth Brush", [&](AnimationClip &c) { motion::smooth(c, anim_brush_paths(), f - r, f + r, 0.35f, r); });
  return true;
}

/* Lock in Place from the toolbar: the selected bone held from the playhead to the rig's next key (10 frames when none). */
void Editor::anim_lock_selected() {
  GameObject *root = anim_root();
  AnimationClipPtr clip = anim_clip();
  GameObject *bone = active_object();
  if (!root || !clip || !bone || !bone->get<Bone>() || !scene_->is_ancestor(root, bone))
    return refuse("Lock in Place: select the hand or foot to hold (a bone of the animated character)", "", "");
  const int from = (int)std::lround(anim_.frame);
  int to = from + 10;
  GameObject *arm = rig_above_of(bone);
  for (const AnimTrack &t : clip->tracks)
    for (const AnimKey &k : t.keys)
      if (k.frame > from + 0.5f && k.frame < to) {
        GameObject *g = scene_->find_by_name(t.path.substr(t.path.rfind('/') == std::string::npos ? 0 : t.path.rfind('/') + 1));
        if (!arm || (g && scene_->is_ancestor(arm, g))) to = (int)std::lround(k.frame);
      }
  int n = 0;
  anim_edit_keys("Lock in Place", [&](AnimationClip &c) { n = motion::lock_in_place(*scene_, *root, c, *bone, from, to, 3); });
  Log::info("Lock in Place: %s held from frame %d to %d (%d frames keyed)", bone->name.c_str(), from, to, n);
}

/* motioncheck [fix <n>] | anim smooth <from> <to> [strength] [radius] | anim lock <bone> <from> <to> [blend] (task 0284) */
void Editor::motioncheck_command(const std::vector<std::string> &t) {
  if (t.size() > 2 && to_lower(t[1]) == "fix") return motion_check_fix((size_t)std::max(0, std::atoi(t[2].c_str())));
  motion_check_run();
}

bool Editor::anim_motion_command(const std::vector<std::string> &t) {
  const std::string verb = t.size() > 1 ? to_lower(t[1]) : "";
  GameObject *root = anim_root();
  if (verb == "smooth" && t.size() > 3) {
    const float from = (float)std::atof(t[2].c_str()), to = (float)std::atof(t[3].c_str());
    const float s = t.size() > 4 ? (float)std::atof(t[4].c_str()) : 1.0f, r = t.size() > 5 ? (float)std::atof(t[5].c_str()) : 3.0f;
    size_t n = 0;
    if (root && anim_clip()) anim_edit_keys("Smooth Motion", [&](AnimationClip &c) { n = motion::smooth(c, anim_brush_paths(), from, to, s, r); });
    Log::info("Smooth: %zu keys evened", n);
    return true;
  }
  if (verb == "lock" && t.size() > 4) {
    GameObject *bone = scene_->find_by_name(t[2]);
    if (!root || !anim_clip() || !bone || !bone->get<Bone>()) return Log::warn("anim lock <bone> <from> <to> [blend]: no bone named '%s' to animate", t[2].c_str()), true;
    int n = 0;
    anim_edit_keys("Lock in Place", [&](AnimationClip &c) {
      n = motion::lock_in_place(*scene_, *root, c, *bone, std::atoi(t[3].c_str()), std::atoi(t[4].c_str()), t.size() > 5 ? std::atoi(t[5].c_str()) : 3);
    });
    Log::info("Lock in Place: %s, %d frames keyed", bone->name.c_str(), n);
    return true;
  }
  return false;
}

}  // namespace bl

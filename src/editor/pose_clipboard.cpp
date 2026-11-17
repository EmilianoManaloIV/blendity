// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Emiliano Manalo and contributors
// Blender 5.3's animation tools on the Scene view's selection (round 541, task 0521, ADR 0148): Copy / Paste Pose with an
// influence and a flipped mode, Align to Transform Orientation, Convert Rotation, and Copy / Paste World-Space Animation.
// The behaviour follows Blender's editors/armature/pose_transform.cc (Paste Pose, the factor and the flip, after the blend),
// editors/transform/transform_mode_align.cc (each element's rotation becomes the orientation's axes, about its own origin),
// editors/animation/anim_convert.cc (Convert Rotation) and editors/animation/world_space_copy.cc (the pairing and the
// parents-first paste); the code is written for Blendity's Transforms and clips (animation.cpp holds the sampling and the
// key writing). "Pose Mode" here is the Scene view with only bones selected; the commands work on plain objects as well.
#include "editor.h"

namespace bl {

namespace {

bool is_digits(const std::string &s) { return !s.empty() && s.find_first_not_of("0123456789") == std::string::npos; }
bool separator(char c) { return c == '.' || c == '_' || c == '-' || c == ' '; }

/* The side words Blender's BLI_string_flip_side_name swaps, in each case. */
struct SideWords {
  const char *a, *b;
  bool letter;  // a single letter: needs a separator beside it (so "Spine" is not "Spine" + "e")
};
const SideWords kSides[] = {{"left", "right", false}, {"Left", "Right", false}, {"LEFT", "RIGHT", false}, {"L", "R", true}, {"l", "r", true}};

/* The name on the other side of the body ("Arm.L" and "Arm.R", "LeftHand" and "RightHand", "leg_r" and "leg_l"); the name
 * itself when it has no side. A trailing ".001" stays where it is. */
std::string flip_side_name(const std::string &name) {
  std::string base = name, number;
  if (const size_t dot = base.rfind('.'); dot != std::string::npos && is_digits(base.substr(dot + 1))) number = base.substr(dot), base.resize(dot);
  for (const SideWords &w : kSides)
    for (int from = 0; from < 2; from++) {
      const std::string has = from ? w.b : w.a, to = from ? w.a : w.b;
      if (base.size() > has.size() && base.compare(base.size() - has.size(), has.size(), has) == 0 && (!w.letter || separator(base[base.size() - has.size() - 1])))
        return base.substr(0, base.size() - has.size()) + to + number;
      if (base.size() > has.size() && base.compare(0, has.size(), has) == 0 && (!w.letter || separator(base[has.size()]))) return to + base.substr(has.size()) + number;
    }
  return name;
}

/* The turn from `a` to `b` taken `f` of the way (slerp, and past 0..1 the same axis carried on: Blender's factor is unbounded). */
Quat blend_rotation(Quat a, Quat b, float f) {
  if (f == 0.0f) return a;
  if (f == 1.0f) return b;
  Quat rel = normalize(a.conjugate() * b);
  if (rel.w < 0.0f) rel = {-rel.x, -rel.y, -rel.z, -rel.w};  // (the short way)
  const float s = std::sqrt(rel.x * rel.x + rel.y * rel.y + rel.z * rel.z);
  if (s < 1e-7f) return a;
  const float angle = 2.0f * std::atan2(s, rel.w);
  return normalize(a * Quat::axis_angle({rel.x / s, rel.y / s, rel.z / s}, angle * f));
}

/* Paste Pose's flip: the pose mirrored across the YZ plane - Position X and the rotation's Euler Y and Z negated. */
Transform mirrored_pose(Transform t) {
  t.position.x = -t.position.x;
  t.rotation = normalize(Quat(t.rotation.x, -t.rotation.y, -t.rotation.z, t.rotation.w));
  t.euler_hint = euler_near(t.rotation, Vec3(t.euler_hint.x, -t.euler_hint.y, -t.euler_hint.z));
  return t;
}

/* `current` taken `f` of the way to `pasted`: location and scale by line, the rotation on the slerp. */
Transform blended_pose(const Transform &current, const Transform &pasted, float f) {
  Transform out = current;
  out.position = current.position + (pasted.position - current.position) * f;
  out.scale = current.scale + (pasted.scale - current.scale) * f;
  out.rotation = blend_rotation(current.rotation, pasted.rotation, f);
  out.euler_hint = euler_near(out.rotation, current.euler_hint);  // (the Inspector's angles stay near what they were)
  return out;
}

void collect_branch(GameObject *g, std::vector<GameObject *> &out) {
  out.push_back(g);
  for (GameObject *c : g->children) collect_branch(c, out);
}

int depth_of(const GameObject *g) {
  int d = 0;
  for (; g && g->parent; g = g->parent) d++;
  return d;
}

bool has_rotation_tracks(AnimationClip &clip, const std::string &path, const char *property, int count) {
  for (int a = 0; a < count; a++)
    if (const AnimTrack *t = clip.find(path, property, a); t && !t->keys.empty()) return true;
  return false;
}

}  // namespace

/* ---------------------------------------------------------------- Pose context */

bool Editor::pose_context() const {
  if (edit_mode_ || selection_.empty()) return false;
  const std::vector<GameObject *> sel = selected_objects(false);
  if (sel.empty()) return false;
  for (const GameObject *g : sel)
    if (!g->get<Bone>()) return false;
  return true;
}

/* ---------------------------------------------------------------- Copy / Paste Pose */

void Editor::pose_copy() {
  const std::vector<GameObject *> sel = selected_objects(false);
  if (sel.empty()) return refuse("Copy Pose: select the bones (or objects) to copy first", "", "");
  pose_clip_.clear();
  for (const GameObject *g : sel) pose_clip_.push_back({g->name, g->local()});
  Log::info("Copy Pose: %zu bone(s) copied", pose_clip_.size());
}

/* Pastes `clip` (a copied pose) onto the rig: each copied bone onto the one of its name (its mirrored name when Flipped), blended
 * `influence` of the way from its pose now, then flipped as Blender does (so Flipped at influence 0 mirrors the pose that is
 * there). `selected_only` leaves the bones that are not selected alone. The rig is the active bone's: the nearest root carrying
 * it (an Animation, an Armature or the parent of one), so a second rig under the same group is not touched. `before` gets the
 * transform of every bone either form of a copied name finds, touched or not - F9 re-runs with Flipped on or off from it. */
bool Editor::pose_paste(const std::vector<PoseClipBone> &clip, float influence, bool flipped, bool selected_only, std::vector<std::pair<uint64_t, Transform>> *before) {
  if (clip.empty()) return refuse("Paste Pose: Copy Pose first", "pose copy", "Copy Pose"), false;
  if (!std::isfinite(influence)) return refuse("Paste Pose: the influence is not a number", "", ""), false;
  std::vector<GameObject *> candidates;
  const std::vector<GameObject *> sel = selected_objects(false);
  if (selected_only) candidates = sel;
  else {
    GameObject *rig = active_object();
    for (GameObject *a = rig; a; a = a->parent) {
      if (a->get<Animation>() || a->get<Armature>() || rig_root_of(a) == a) {
        rig = a;
        break;
      }
      if (!a->parent) rig = a;  // (no rig found above: the whole tree it hangs in)
    }
    if (rig) collect_branch(rig, candidates);
    for (GameObject *g : sel)
      if (std::find(candidates.begin(), candidates.end(), g) == candidates.end()) candidates.push_back(g);
  }
  auto find = [&](const std::string &name) {
    const auto it = std::find_if(candidates.begin(), candidates.end(), [&](const GameObject *g) { return g->name == name; });
    return it == candidates.end() ? nullptr : *it;
  };
  if (before)
    for (const PoseClipBone &c : clip)
      for (GameObject *g : {find(c.name), find(flip_side_name(c.name))})
        if (g && std::none_of(before->begin(), before->end(), [&](const std::pair<uint64_t, Transform> &b) { return b.first == g->id; })) before->push_back({g->id, g->local()});
  int pasted = 0;
  for (const PoseClipBone &c : clip) {
    GameObject *g = find(flipped ? flip_side_name(c.name) : c.name);
    if (!g) continue;
    Transform t = blended_pose(g->local(), c.local, influence);
    g->set_local(flipped ? mirrored_pose(t) : t);
    pasted++;
  }
  if (!pasted) return refuse("Paste Pose: no bone of the copied names" + std::string(selected_only ? " is selected" : " in this rig"), "", ""), false;
  return true;
}

/* The undo step around a paste, and F9's panel for it. */
void Editor::pose_paste_step(float influence, bool flipped, bool selected_only) {
  if (playing_) return refuse("Paste Pose: not while Play runs", "stop", "stop Play");
  std::vector<std::pair<uint64_t, Transform>> before;
  if (!pose_paste(pose_clip_, influence, flipped, selected_only, &before)) return;
  mark_changed(flipped ? "Paste Pose Flipped" : "Paste Pose");
  record_pose_last(std::move(before), pose_clip_, influence, flipped, selected_only);  // (F9 keeps the pose it pasted: a later Copy Pose does not change it)
}

/* ---------------------------------------------------------------- Align to Transform Orientation */

/* Each selected object's rotation becomes the orientation's axes, about its own origin: Global the world's (no rotation),
 * Local the active one's. `orientation` 0 Global, 1 Local, else the editor's current setting, which is left as it is.
 * Position and scale stay and the children follow. */
void Editor::pose_align(int orientation) {
  if (playing_) return refuse("Align to Transform Orientation: not while Play runs", "stop", "stop Play");
  std::vector<GameObject *> sel = selected_objects(false);
  if (sel.empty()) return refuse("Align to Transform Orientation: select a bone or an object first", "", "");
  const GameObject *active = active_object();
  const bool local = orientation < 0 ? space_local_ : orientation == 1;
  const Quat to = local && active ? active->world_rotation() : Quat();
  std::stable_sort(sel.begin(), sel.end(), [](const GameObject *a, const GameObject *b) { return depth_of(a) < depth_of(b); });  // (a parent turns before its selected child)
  for (GameObject *g : sel) g->set_world_rotation(to);
  mark_changed("Align to Transform Orientation");
}

/* ---------------------------------------------------------------- Convert Rotation */

void Editor::anim_rotation_convert(int mode, bool bake) {
  GameObject *root = anim_root();
  AnimationClipPtr clip = anim_clip();
  if (playing_) return refuse("Convert Rotation: not while Play runs", "stop", "stop Play");
  if (!root || !clip) return refuse("Convert Rotation: select an animated object (or bone) first", "window Animation", "open Animation");
  std::vector<std::string> to_q, to_euler;
  for (const GameObject *g : selected_objects(false)) {
    bool under = false;
    for (const GameObject *x = g; x && !under; x = x->parent) under = x == root;
    if (!under) continue;
    const std::string path = anim_path(*root, *g);
    const bool has_q = has_rotation_tracks(*clip, path, "RotationQ", 4), has_euler = has_rotation_tracks(*clip, path, "Rotation", 3);
    if (mode != 2 && has_euler && !(mode == 0 && has_q)) to_q.push_back(path);
    if (mode != 1 && has_q && (mode == 0 || !has_euler)) to_euler.push_back(path);
  }
  if (to_q.empty() && to_euler.empty()) return refuse("Convert Rotation: the selected objects have no rotation keys to convert", "", "");
  int done = 0;
  anim_edit_keys("Convert Rotation", [&](AnimationClip &c) {
    done += convert_rotation_tracks(c, root, to_q, true, bake);
    done += convert_rotation_tracks(c, root, to_euler, false, bake);
  });
  anim_.selected.clear();  // (the tracks were renumbered)
  Log::info("Convert Rotation: %d object(s) converted%s", done, bake ? " (baked on every frame)" : "");
}

/* ---------------------------------------------------------------- World-space copy / paste */

void Editor::anim_world_copy(bool custom, float start, float end) {
  GameObject *root = anim_root();
  AnimationClipPtr clip = anim_clip();
  if (!root || !clip) return refuse("Copy World-Space Animation: select an animated object (or bone) first", "window Animation", "open Animation");
  const float first = custom ? start : 0.0f, last = custom ? end : clip->end_frame();
  WorldCopy copy;
  std::string why;
  if (!copy_world_space(*clip, *root, selected_objects(false), first, last, copy, why)) return refuse("Copy World-Space Animation: " + why, "", "");
  world_clip_ = std::move(copy);
  Log::info("Copy World-Space Animation: %zu object(s), frames %d to %d", world_clip_.sources.size(), world_clip_.first, world_clip_.last);
}

void Editor::anim_world_paste(bool from_playhead) {
  GameObject *root = anim_root();
  AnimationClipPtr clip = anim_clip();
  if (playing_) return refuse("Paste World-Space Animation: not while Play runs", "stop", "stop Play");
  if (!root || !clip) return refuse("Paste World-Space Animation: select an animated object (or bone) first", "window Animation", "open Animation");
  /* Pasted onto a copy of the clip first: a refusal then leaves no undo step, and the work is done once. */
  AnimationClip trial = *clip;
  std::string why;
  const float at = from_playhead ? anim_.frame : (float)world_clip_.first;
  const std::vector<GameObject *> targets = selected_objects(false);
  const int n = paste_world_space(trial, *root, world_clip_, targets, at, why);
  if (n == 0) return refuse("Paste World-Space Animation: " + why, "", "");
  anim_edit_keys("Paste World-Space Animation", [&](AnimationClip &c) { c.tracks = trial.tracks; });
  anim_.selected.clear();
  Log::info("Paste World-Space Animation: pasted %d of %zu object(s), keyed on frames %g to %g", n, targets.size(), at, at + (float)(world_clip_.last - world_clip_.first));
}

/* ---------------------------------------------------------------- console */

/* pose copy | paste [influence] [flipped] [selected] | align [global|local] (round 541). */
void Editor::pose_command(const std::vector<std::string> &t) {
  const std::string verb = t.size() > 1 ? to_lower(t[1]) : "";
  if (verb == "copy") return pose_copy();
  if (verb == "paste") {
    float influence = 1.0f;
    bool flipped = false, selected_only = false;
    for (size_t i = 2; i < t.size(); i++) {
      const std::string w = to_lower(t[i]);
      if (w == "flipped") flipped = true;
      else if (w == "selected") selected_only = true;
      else {
        char *end = nullptr;
        const double v = std::strtod(t[i].c_str(), &end);
        if (end == t[i].c_str() || *end) return Log::warn("pose paste [influence] [flipped] [selected]: '%s' is not an influence (a number), 'flipped' or 'selected'", t[i].c_str());
        influence = (float)v;
      }
    }
    return pose_paste_step(influence, flipped, selected_only);
  }
  if (verb == "align") {
    const std::string how = t.size() > 2 ? to_lower(t[2]) : "";
    if (!how.empty() && how != "global" && how != "local") return Log::warn("pose align [global|local]");
    return pose_align(how == "global" ? 0 : how == "local" ? 1 : -1);  // (the setting itself is not changed)
  }
  Log::warn("pose copy | paste [influence] [flipped] [selected] | align [global|local]");
}

}  // namespace bl

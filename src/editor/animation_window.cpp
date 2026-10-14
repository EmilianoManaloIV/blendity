// SPDX-License-Identifier: GPL-2.0-or-later
// The Animation window (task 0059, ADR 0029): Unity's dopesheet. The clip of the selected object's Animation
// component, its tracks down the left, their keys as diamonds along a ruler on the right. Preview poses the scene
// without an undo step (and puts it back after); Record turns edits of the animated objects into keys at the playhead.
#include "editor.h"

#include "../scene/animation.h"
#include "../scene/scene.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace bl {

using ui::Icon;

namespace {

const char *kAxisNames[] = {"x", "y", "z"};

/* The numbers of a component a track can animate: its float, int, bool, vector and colour fields ("Field", axes). */
struct NumericFields : Reflector {
  std::vector<std::pair<std::string, int>> out;  // (field, how many axes)
  void field(const char *n, float &, float, float, float) override { out.push_back({n, 1}); }
  void field(const char *n, int &, int, int) override { out.push_back({n, 1}); }
  void field(const char *n, bool &) override { out.push_back({n, 1}); }
  void field(const char *n, Vec3 &) override { out.push_back({n, 3}); }
  void color(const char *n, Vec3 &) override { out.push_back({n, 3}); }
  void enumeration(const char *, int &, const char *const *, int) override {}
  void text(const char *, std::string &) override {}
  void mesh(const char *, MeshPtr &) override {}
};

/* What Record compares by: the object (by id: a rename or a move in the Hierarchy isn't a change), the number. */
std::string shown_key(uint64_t id, const std::string &property, int axis) { return std::to_string(id) + "|" + property + "|" + std::to_string(axis); }

/* A file name from an object's name: no folders, nothing Windows refuses. */
std::string file_safe(std::string n) {
  for (char &ch : n)
    if (std::strchr("/\\:*?\"<>|", ch)) ch = '_';
  while (!n.empty() && (n.front() == '.' || n.front() == ' ')) n.erase(n.begin());
  return n.empty() ? std::string("Clip") : n;
}

/* Every object at and below `root`. */
std::vector<GameObject *> subtree(GameObject *root) {
  std::vector<GameObject *> out, stack{root};
  while (!stack.empty()) {
    GameObject *g = stack.back();
    stack.pop_back();
    out.push_back(g);
    for (GameObject *c : g->children) stack.push_back(c);
  }
  return out;
}

}  // namespace

/* ---------------------------------------------------------------- what is animated */

/* The object the window animates: the nearest one at or above the active object with an Animation component, else
 * the active object itself (which can then get a clip). */
GameObject *Editor::anim_root() {
  for (GameObject *a = active_object(); a; a = a->parent)
    if (a->get<Animation>()) return a;
  return active_object();
}

AnimationClipPtr Editor::anim_clip() {
  GameObject *root = anim_root();
  const Animation *an = root ? root->get<Animation>() : nullptr;
  return an ? clip_asset(an->clip) : nullptr;
}

/* Create New Clip: Assets/Animations/<root's name>.anim (a free name), and an Animation component playing it. */
AnimationClipPtr Editor::anim_new_clip(GameObject *root, const std::string &name_in) {
  if (!root) {
    Log::warn("Animation: select an object to animate");
    return nullptr;
  }
  if (playing_) return nullptr;
  const std::string base = file_safe(name_in.empty() ? root->name : name_in);
  std::string path = "Assets/Animations/" + base + ".anim";
  for (int k = 1; fs::exists(resolve_asset_path(path)); k++) path = strprintf("Assets/Animations/%s %d.anim", base.c_str(), k);
  AnimationClipPtr c = create_clip_asset(path, fs::stem(path));
  if (!c) {
    Log::warn("Animation: couldn't write %s", path.c_str());
    return nullptr;
  }
  Animation *an = root->get<Animation>();
  if (!an) an = root->add<Animation>();
  an->clip = path;
  anim_.root = root->id;
  anim_clip_paths_listed_ = -100;
  invalidate_project_listing();
  mark_changed("Create Animation Clip");
  Log::info("Animation: %s on '%s'", path.c_str(), root->name.c_str());
  return c;
}

/* ---------------------------------------------------------------- preview and record */

void Editor::anim_snapshot_shown() {
  anim_.shown.clear();
  GameObject *root = anim_root();
  AnimationClipPtr clip = anim_clip();
  anim_.shown_root = root ? root->id : 0;
  anim_.shown_clip = clip.get();
  if (!root) return;
  for (GameObject *g : subtree(root))
    for (const char *prop : {"Position", "Rotation", "Scale"})
      for (int a = 0; a < 3; a++) {
        float v = 0.0f;
        if (read_anim_value(*g, prop, a, v)) anim_.shown[shown_key(g->id, prop, a)] = v;
      }
  if (!clip) return;
  for (const AnimTrack &t : clip->tracks) {
    GameObject *g = anim_target(*root, t.path);
    float v = 0.0f;
    if (g && read_anim_value(*g, t.property, t.axis, v)) anim_.shown[shown_key(g->id, t.property, t.axis)] = v;
  }
}

/* The clip at the playhead onto the scene (Preview only), IK after it, and what's shown remembered for Record. */
void Editor::anim_apply() {
  GameObject *root = anim_root();
  AnimationClipPtr clip = anim_clip();
  if (anim_.preview && root && clip) {
    apply_clip(*clip, *root, anim_.frame);
    solve_constraints(*scene_);
    scene_dirty_ = true;
  }
  anim_snapshot_shown();
}

void Editor::anim_set_frame(float f) {
  anim_.frame = std::max(0.0f, std::round(f));
  anim_apply();
}

/* Preview on: the pose now is kept to put back. Off: it's put back (scrubbing never dirties the scene, as in Unity). */
void Editor::anim_set_preview(bool on) {
  if (on == anim_.preview) return;
  GameObject *root = anim_root();
  if (on && (playing_ || !root)) {
    if (playing_) Log::warn("Animation: Preview and Record are for editing; Stop Play first");
    return;
  }
  if (on) {
    /* What the clip can move: the objects below its root, and the bones IK turns after it (a chain driven by an
     * animated target may sit outside the root). */
    anim_.saved_pose.clear();
    for (GameObject *g : subtree(root)) anim_.saved_pose.push_back({g->id, g->local()});
    scene_->for_each([&](GameObject &g) {
      if (g.get<Bone>() && ik_drives(*scene_, g)) anim_.saved_pose.push_back({g.id, g.local()});
    });
    anim_.saved_values.clear();
    if (AnimationClipPtr clip = anim_clip(); clip && root)
      for (const AnimTrack &t : clip->tracks) {
        GameObject *g = anim_target(*root, t.path);
        float v = 0.0f;
        if (g && read_anim_value(*g, t.property, t.axis, v)) anim_.saved_values.push_back({g->id, t.property, t.axis, v});
      }
    anim_.preview = true;
    anim_apply();
    return;
  }
  anim_.preview = anim_.record = anim_.playing = false;
  commit_undo();  // (edits made while previewing are their own steps, before the pose goes back)
  for (const auto &[id, t] : anim_.saved_pose)
    if (GameObject *g = scene_->find(id)) g->set_local(t);
  for (const auto &[id, prop, axis, v] : anim_.saved_values)
    if (GameObject *g = scene_->find(id)) write_anim_value(*g, prop, axis, v);
  anim_.saved_pose.clear();
  anim_.saved_values.clear();
  solve_constraints(*scene_);
  stable_ = scene_->clone();  // (the next step's "before" is the scene's own pose, not the previewed one)
  scene_dirty_ = true;
}

/* Another scene (New, Open): the objects the state names are gone; nothing to put back. */
void Editor::anim_reset() {
  anim_ = AnimEditorState{};
  clip_before_.clear();
}

/* A clip's text before its first edit since the last undo step (as note_texture_edit). */
void Editor::note_clip_edit(AnimationClip &c) {
  if (c.asset_path.empty()) return;
  for (const auto &kv : clip_before_)
    if (kv.first == c.asset_path) return;
  clip_before_.push_back({c.asset_path, save_clip_text(c)});
}

void Editor::restore_clip_texts(const std::vector<std::pair<std::string, std::string>> &texts, std::vector<std::pair<std::string, std::string>> *current) {
  for (const auto &kv : texts) {
    AnimationClipPtr c = clip_asset(kv.first);
    if (!c) continue;  // renamed or deleted since
    if (current) current->push_back({kv.first, save_clip_text(*c)});
    const std::string keep_path = c->asset_path, keep_name = c->name;
    load_clip_text(kv.second, *c);
    c->asset_path = keep_path, c->name = keep_name;
  }
}

/* Record: every number of the animated objects that differs from what was shown becomes a key at the playhead (a
 * vector property keys all three axes, as Unity does). Called from mark_changed. */
bool Editor::anim_changed(uint64_t id, const std::string &property, int axis, float v) const {
  const auto it = anim_.shown.find(shown_key(id, property, axis));
  return it != anim_.shown.end() && std::fabs(it->second - v) > 1e-5f * (1.0f + std::fabs(v));  // (not shown before: not a change)
}

/* A vector property of `g` keyed (all three axes, as Unity) when one of them changed. Rotations take the turn
 * nearest the one shown, so a gizmo's -170 after 170 keys 190, not a spin the long way round. */
bool Editor::anim_record_vector(AnimationClip &clip, GameObject &g, const std::string &path, const char *prop, float frame) {
  float v[3] = {0, 0, 0};
  bool changed = false;
  for (int a = 0; a < 3; a++) {
    read_anim_value(g, prop, a, v[a]);
    changed = changed || anim_changed(g.id, prop, a, v[a]);
  }
  if (!changed) return false;
  for (int a = 0; a < 3; a++) {
    const auto it = anim_.shown.find(shown_key(g.id, prop, a));
    if (std::string(prop) == "Rotation" && it != anim_.shown.end()) v[a] = it->second + std::remainder(v[a] - it->second, 360.0f);
    clip.track(path, prop, a).set_key(frame, v[a]);
  }
  return true;
}

/* Record: what changed since it was shown becomes keys at the playhead (a whole frame). Bones IK turns are left to
 * IK (their targets are what gets keyed); objects new under the root, or a root or clip changed since the last look,
 * just get looked at. Called from mark_changed. */
void Editor::anim_record_changes() {
  GameObject *root = anim_root();
  AnimationClipPtr clip = anim_clip();
  if (!anim_.record || !anim_.preview || !root || !clip || playing_) return;
  if (anim_.shown_root != root->id || anim_.shown_clip != clip.get()) return anim_snapshot_shown();
  const std::string before = save_clip_text(*clip);
  const float frame = std::round(anim_.frame);
  bool keyed = false;
  for (GameObject *g : subtree(root)) {
    if (g->get<Bone>() && ik_drives(*scene_, *g)) continue;
    const std::string path = anim_path(*root, *g);
    for (const char *prop : {"Position", "Rotation", "Scale"}) keyed = anim_record_vector(*clip, *g, path, prop, frame) || keyed;
  }
  for (AnimTrack &t : clip->tracks) {  // the components' numbers already animated
    if (is_transform_property(t.property)) continue;
    GameObject *g = anim_target(*root, t.path);
    float v = 0.0f;
    if (!g || !read_anim_value(*g, t.property, t.axis, v) || !anim_changed(g->id, t.property, t.axis, v)) continue;
    t.set_key(frame, v);
    keyed = true;
  }
  if (keyed) {
    bool noted = false;
    for (const auto &kv : clip_before_) noted = noted || kv.first == clip->asset_path;
    if (!noted && !clip->asset_path.empty()) clip_before_.push_back({clip->asset_path, before});
    clip->touch();
  }
  anim_snapshot_shown();
}

/* Playback in Preview: the playhead runs at the clip's frame rate and loops at its end. */
void Editor::anim_tick() {
  if (!anim_.playing || !anim_.preview) return;
  AnimationClipPtr clip = anim_clip();
  if (!clip) {
    anim_.playing = false;
    return;
  }
  const float end = std::max(1.0f, clip->end_frame());
  const float f = anim_.play_from + (float)(now_seconds() - anim_.play_clock) * clip->frame_rate;
  anim_.frame = std::fmod(f, end);
  anim_apply();
  ui_.redraw = true;
}

/* ---------------------------------------------------------------- editing keys */

void Editor::anim_edit_keys(const char *label, const std::function<void(AnimationClip &)> &edit) {
  AnimationClipPtr clip = anim_clip();
  if (!clip || playing_) return;  // (Play's changes are thrown away on Stop; a clip's would not be)
  note_clip_edit(*clip);
  edit(*clip);
  clip->touch();
  mark_changed(label);
  anim_apply();
}

/* A key at the playhead on `t` with the number's value now (the curve's value when its object is missing). */
void Editor::anim_add_key(size_t track, float frame) {
  GameObject *root = anim_root();
  anim_edit_keys("Add Key", [&](AnimationClip &c) {
    if (track >= c.tracks.size()) return;
    AnimTrack &t = c.tracks[track];
    float v = t.sample(frame);
    if (GameObject *g = root ? anim_target(*root, t.path) : nullptr) read_anim_value(*g, t.property, t.axis, v);
    t.set_key(frame, v);
  });
}

void Editor::anim_delete_selected() {
  if (anim_.selected.empty()) return;
  const auto sel = anim_.selected;
  anim_edit_keys("Delete Keys", [&](AnimationClip &c) {
    for (const auto &[track, frame] : sel) {
      if (track < 0 || (size_t)track >= c.tracks.size()) continue;
      AnimTrack &t = c.tracks[(size_t)track];
      const int k = t.key_at(frame);
      if (k >= 0) t.keys.erase(t.keys.begin() + k);
    }
  });
  anim_.selected.clear();
}

/* The selected keys `delta` frames later (whole frames; a key landing on another replaces it). */
void Editor::anim_move_selected(float delta) {
  if (anim_.selected.empty() || delta == 0.0f) return;
  auto sel = anim_.selected;
  anim_edit_keys("Move Keys", [&](AnimationClip &c) {
    std::vector<std::pair<int, AnimKey>> taken;
    for (const auto &[track, frame] : sel) {
      if (track < 0 || (size_t)track >= c.tracks.size()) continue;
      AnimTrack &t = c.tracks[(size_t)track];
      const int k = t.key_at(frame);
      if (k < 0) continue;
      taken.push_back({track, t.keys[(size_t)k]});
      t.keys.erase(t.keys.begin() + k);
    }
    for (auto &[track, key] : taken) {
      key.frame = std::max(0.0f, key.frame + delta);
      c.tracks[(size_t)track].put_key(key);  // (its tangents too)
    }
  });
  for (auto &s : anim_.selected) s.second = std::max(0.0f, s.second + delta);
}

void Editor::anim_set_interp(int interp) {
  const auto sel = anim_.selected;
  anim_edit_keys("Key Interpolation", [&](AnimationClip &c) {
    for (const auto &[track, frame] : sel) {
      if (track < 0 || (size_t)track >= c.tracks.size()) continue;
      const int k = c.tracks[(size_t)track].key_at(frame);
      if (k >= 0) c.tracks[(size_t)track].keys[(size_t)k].interp = interp;
    }
  });
}

void Editor::anim_copy() {
  AnimationClipPtr clip = anim_clip();
  if (!clip || anim_.selected.empty()) return;
  anim_.clipboard.clear();
  float first = 1e30f;
  for (const auto &s : anim_.selected) first = std::min(first, s.second);
  for (const auto &[track, frame] : anim_.selected) {
    if (track < 0 || (size_t)track >= clip->tracks.size()) continue;
    const int k = clip->tracks[(size_t)track].key_at(frame);
    if (k < 0) continue;
    const AnimTrack &tr = clip->tracks[(size_t)track];
    AnimKey key = tr.keys[(size_t)k];
    key.frame -= first;
    anim_.clipboard.push_back({tr.path, tr.property, tr.axis, key});
  }
}

void Editor::anim_paste() {
  if (anim_.clipboard.empty()) return;
  const auto board = anim_.clipboard;
  const float at = anim_.frame;
  anim_edit_keys("Paste Keys", [&](AnimationClip &c) {
    for (const auto &[path, property, axis, key] : board) {
      AnimKey k = key;
      k.frame += at;
      c.track(path, property, axis).put_key(k);
    }
  });
}

/* Add Property: Position / Rotation / Scale or a component's number of `g`, a key at the playhead with its value. */
void Editor::anim_add_property(GameObject *g, const std::string &property, int axes) {
  GameObject *root = anim_root();
  if (!root || !g) return;
  const std::string path = anim_path(*root, *g);
  anim_edit_keys("Add Property", [&](AnimationClip &c) {
    for (int a = 0; a < axes; a++) {
      float v = 0.0f;
      read_anim_value(*g, property, a, v);
      c.track(path, property, a).set_key(anim_.frame, v);
    }
  });
}

std::vector<float> Editor::anim_key_frames() {
  std::vector<float> fs;
  if (AnimationClipPtr clip = anim_clip())
    for (const AnimTrack &t : clip->tracks)
      for (const AnimKey &k : t.keys) fs.push_back(k.frame);
  std::sort(fs.begin(), fs.end());
  fs.erase(std::unique(fs.begin(), fs.end()), fs.end());
  return fs;
}

void Editor::anim_step_key(int dir) {
  const std::vector<float> fs = anim_key_frames();
  float to = anim_.frame;
  for (float f : fs) {
    if (dir > 0 && f > anim_.frame + 0.01f) return anim_set_frame(f);
    if (dir < 0 && f < anim_.frame - 0.01f) to = f;
  }
  if (dir < 0) anim_set_frame(to);
}

/* ---------------------------------------------------------------- the window */

const std::vector<std::string> &Editor::anim_clip_paths() {
  if (anim_clip_paths_listed_ >= 0.0) return anim_clip_paths_;  // (invalidate_project_listing walks it again)
  anim_clip_paths_listed_ = now_seconds();
  anim_clip_paths_.clear();
  std::vector<std::string> dirs{assets_dir_};
  while (!dirs.empty()) {
    const std::string d = dirs.back();
    dirs.pop_back();
    for (const DirEntry &de : fs::list(d)) {
      const std::string p = fs::join(d, de.name);
      if (de.is_dir) dirs.push_back(p);
      else if (fs::extension(de.name) == ".anim") anim_clip_paths_.push_back(make_asset_relative(p));
    }
  }
  for (std::string &p : anim_clip_paths_) std::replace(p.begin(), p.end(), '\\', '/');
  std::sort(anim_clip_paths_.begin(), anim_clip_paths_.end());
  return anim_clip_paths_;
}

void Editor::draw_anim_toolbar(const Recti &bar, GameObject *root) {
  auto &u = ui_;
  int x = bar.x + u.px(4);
  const int bh = bar.h - u.px(6), y = bar.y + u.px(3);
  auto button = [&](int w, const char *label, bool on, const char *tip) {
    const Recti b{x, y, w, bh};
    x += w + u.px(3);
    const bool hit = u.button(b, label, on);
    u.tooltip(tip);
    return hit;
  };
  if (button(u.px(64), "Preview", anim_.preview, "Shows the clip at the playhead on the scene; turning it off puts the scene back.")) anim_set_preview(!anim_.preview);
  const Recti rec{x, y, u.px(28), bh};
  if (button(u.px(28), "", anim_.record, "Record (Unity's red button): changes to the animated objects become keys at the playhead.")) {
    anim_.record = !anim_.record;
    if (anim_.record) anim_set_preview(true);
  }
  u.canvas.fill_circle((float)(rec.x + rec.w / 2), (float)(rec.y + rec.h / 2), (float)u.px(5), anim_.record ? Color::hex(0xFF3B3B) : Color::hex(0x884444));
  if (button(u.px(26), "|<", false, "First frame")) anim_set_frame(0.0f);
  if (button(u.px(26), "<", false, "Previous key")) anim_step_key(-1);
  if (button(u.px(30), anim_.playing ? "||" : ">", anim_.playing, "Play the clip in the Scene view (Preview)")) {
    anim_.playing = !anim_.playing;
    if (anim_.playing) anim_set_preview(true), anim_.play_from = anim_.frame, anim_.play_clock = now_seconds();
  }
  if (button(u.px(26), ">", false, "Next key")) anim_step_key(1);
  AnimationClipPtr clip = anim_clip();
  if (button(u.px(26), ">|", false, "Last frame") && clip) anim_set_frame(clip->end_frame());
  int fr = (int)anim_.frame;
  if (u.int_field(u.id("anim_frame"), {x, y, u.px(56), bh}, fr, 0, 100000)) anim_set_frame((float)fr);
  x += u.px(60);
  /* The clip menu: the clip assets, then Create New Clip. */
  if (!root) return;
  std::vector<std::string> names{"(no clip)"};
  const std::vector<std::string> &paths = anim_clip_paths();
  int cur = 0;
  for (size_t i = 0; i < paths.size(); i++) {
    names.push_back(fs::stem(paths[i]));
    if (clip && clip->asset_path == paths[i]) cur = (int)i + 1;
  }
  names.push_back("Create New Clip...");
  std::vector<const char *> cn;
  for (const std::string &n : names) cn.push_back(n.c_str());
  int pick = cur;
  if (u.combo(u.id("anim_clip"), {x, y, u.px(150), bh}, pick, cn.data(), (int)cn.size()) && pick != cur) anim_pick_clip(root, pick == (int)names.size() - 1 ? "" : pick ? paths[(size_t)pick - 1] : "-");
  x += u.px(154);
  if (button(u.px(96), "Add Property", false, "Animate a number of the selected object (or the root): Position, Rotation, Scale or a component's.") && clip)
    u.open_popup(u.id("anim_add_prop"), {x - u.px(99), bar.bottom(), 0, 0});
  if (button(u.px(76), "Dopesheet", !anim_.curves, "The keys as diamonds along the time line (Unity's Dopesheet).")) anim_.curves = false;
  if (button(u.px(64), "Curves", anim_.curves,
             "Each property's value over time: drag keys in time and value, and their tangent handles to shape the curve.\n"
             "Click track names to pick which curves show; F frames them; the wheel zooms the values.")) {
    anim_.curves = true;
    anim_.v_framed.clear();
  }
}

/* The clip menu's choice: a clip asset for the root's Animation, none ("-"), or a new clip (""). */
void Editor::anim_pick_clip(GameObject *root, const std::string &path) {
  if (path.empty()) {
    anim_new_clip(root, "");
    return;
  }
  anim_set_preview(false);
  Animation *an = root->get<Animation>();
  if (!an) an = root->add<Animation>();
  an->clip = path == "-" ? std::string() : path;
  anim_.root = root->id;
  anim_clip_changed();
  mark_changed("Animation Clip");
}

void Editor::draw_anim_add_property(GameObject *root) {
  auto &u = ui_;
  GameObject *a = active_object();
  bool under = false;
  for (GameObject *x = a; x && !under; x = x->parent) under = x == root;
  GameObject *g = under ? a : root;
  u.popup(u.id("anim_add_prop"), u.px(240), [this, g] {
    auto &u = ui_;
    if (!g) return;
    for (const char *prop : {"Position", "Rotation", "Scale"})
      if (u.menu_item(g->name + " : " + prop)) anim_add_property(g, prop, 3);
    for (auto &c : g->components) {
      NumericFields nf;
      c->reflect(nf);
      for (const auto &[field, axes] : nf.out)
        if (u.menu_item(g->name + " : " + c->type_name() + "." + field)) anim_add_property(g, std::string(c->type_name()) + "." + field, axes);
    }
  });
}

float Editor::anim_frame_at(const Recti &sheet, int x, float end) const { return (float)(x - sheet.x) / (float)std::max(1, sheet.w) * end; }
int Editor::anim_x_of(const Recti &sheet, float frame, float end) const { return sheet.x + (int)std::lround(frame / end * (float)sheet.w); }

/* The key under (mx, my) in the sheet: its track and frame. */
bool Editor::anim_key_under(const AnimationClip &clip, const Recti &sheet, int row_h, float end, int mx, int my, int &track, float &frame) {
  const int r = (my - sheet.y) / std::max(1, row_h) + anim_.scroll;
  if (my < sheet.y || r < 0 || (size_t)r >= clip.tracks.size()) return false;
  for (const AnimKey &k : clip.tracks[(size_t)r].keys) {
    if (std::abs(anim_x_of(sheet, k.frame, end) - mx) > ui_.px(5)) continue;
    track = r, frame = k.frame;
    return true;
  }
  return false;
}

bool Editor::anim_selected(int track, float frame) const {
  for (const auto &s : anim_.selected)
    if (s.first == track && std::fabs(s.second - frame) < 0.01f) return true;
  return false;
}

/* Mouse and keys over the dopesheet. */
void Editor::anim_sheet_input(const AnimationClip &clip, const Recti &ruler, const Recti &sheet, int row_h, float end) {
  auto &u = ui_;
  auto &in = u.in;
  const int mx = in.mx, my = in.my;
  if (anim_.drag == 1 && in.down[0]) anim_set_frame(anim_frame_at(sheet, mx, end));
  if (!in.down[0] && anim_.drag) anim_end_drag(clip, sheet, row_h, end);
  if (u.hovered(ruler) && in.pressed[0]) {
    anim_.drag = 1;
    anim_set_frame(anim_frame_at(sheet, mx, end));
    return;
  }
  if (!u.hovered(sheet)) return;
  int track = -1;
  float frame = 0.0f;
  const bool on_key = anim_key_under(clip, sheet, row_h, end, mx, my, track, frame);
  if (in.double_clicked[0] && !on_key) {
    const int r = (my - sheet.y) / std::max(1, row_h) + anim_.scroll;
    if (r >= 0 && (size_t)r < clip.tracks.size()) anim_add_key((size_t)r, std::round(anim_frame_at(sheet, mx, end)));
    return;
  }
  if (in.pressed[1] && on_key) {
    if (!anim_selected(track, frame)) anim_.selected = {{track, frame}};
    u.open_popup(u.id("anim_key_ctx"), {mx, my, 0, 0});
    return;
  }
  if (!in.pressed[0]) return;
  if (!on_key) {
    if (!in.shift()) anim_.selected.clear();
    anim_.drag = 3, anim_.drag_x = mx, anim_.drag_y = my;
    return;
  }
  if (in.shift()) {
    auto it = std::find_if(anim_.selected.begin(), anim_.selected.end(), [&](const auto &s) { return s.first == track && std::fabs(s.second - frame) < 0.01f; });
    if (it != anim_.selected.end()) anim_.selected.erase(it);
    else anim_.selected.push_back({track, frame});
  }
  else if (!anim_selected(track, frame)) anim_.selected = {{track, frame}};
  anim_.drag = 2, anim_.drag_x = mx, anim_.drag_y = my;
}

void Editor::anim_end_drag(const AnimationClip &clip, const Recti &sheet, int row_h, float end) {
  const int kind = anim_.drag;
  anim_.drag = 0;
  const int mx = ui_.in.mx, my = ui_.in.my;
  if (kind == 2) {
    anim_move_selected(std::round(anim_frame_at(sheet, mx, end) - anim_frame_at(sheet, anim_.drag_x, end)));
    return;
  }
  if (kind != 3) return;
  /* Box select: every key inside. */
  const int x0 = std::min(mx, anim_.drag_x), x1 = std::max(mx, anim_.drag_x), y0 = std::min(my, anim_.drag_y), y1 = std::max(my, anim_.drag_y);
  for (size_t r = 0; r < clip.tracks.size(); r++) {
    const int cy = sheet.y + ((int)r - anim_.scroll) * row_h + row_h / 2;
    if (cy < y0 || cy > y1) continue;
    for (const AnimKey &k : clip.tracks[r].keys) {
      const int kx = anim_x_of(sheet, k.frame, end);
      if (kx >= x0 && kx <= x1 && !anim_selected((int)r, k.frame)) anim_.selected.push_back({(int)r, k.frame});
    }
  }
}

/* The ruler: frame numbers every so many frames. */
void Editor::draw_anim_ruler(const Recti &ruler, const Recti &sheet, float end) {
  auto &u = ui_;
  const int step = std::max(1, (int)std::ceil(end / std::max(1.0f, (float)ruler.w / (float)u.px(50)) / 5.0f) * 5);
  for (int f = 0; f <= (int)end; f += step) {
    const int fx = anim_x_of(sheet, (float)f, end);
    u.canvas.vline(fx, ruler.y + ruler.h / 2, ruler.bottom(), Color::hex(0x666666));
    u.label({fx + u.px(2), ruler.y, u.px(40), ruler.h}, std::to_string(f), u.theme.text_dim);
  }
}

/* The tracks: names on the left (missing objects in yellow); in the Dopesheet their keys on the right, in the Curves
 * view a click on a name picks it to show (Ctrl+click adds). The wheel over them scrolls. */
void Editor::draw_anim_rows(const AnimationClip &clip, GameObject *root, const Recti &list, const Recti &body, const Recti &sheet, int row_h, float end) {
  auto &u = ui_;
  auto &in = u.in;
  const int fit = std::max(1, list.h / std::max(1, row_h));
  const Recti scroll_area = anim_.curves ? list : body;
  if (u.hovered(scroll_area) && in.wheel_y != 0.0f) anim_.scroll -= (int)in.wheel_y, in.wheel_y = 0.0f;
  anim_.scroll = std::max(0, std::min(anim_.scroll, (int)clip.tracks.size() - fit));
  for (size_t t = (size_t)anim_.scroll; t < clip.tracks.size(); t++) {
    const AnimTrack &tr = clip.tracks[t];
    const Recti lr{list.x, list.y + ((int)t - anim_.scroll) * row_h, list.w, row_h};
    if (lr.y > body.bottom()) break;
    const bool picked = std::find(anim_.curve_tracks.begin(), anim_.curve_tracks.end(), (int)t) != anim_.curve_tracks.end();
    if (anim_.curves && picked) u.canvas.fill_rect(lr, u.theme.selection);
    else if (t % 2) u.canvas.fill_rect({body.x, lr.y, anim_.curves ? list.w : body.w, row_h}, Color::hex(0x262626));
    const bool missing = anim_target(*root, tr.path) == nullptr;
    u.label({lr.x + u.px(6), lr.y, lr.w - u.px(8), lr.h}, tr.label(), missing ? u.theme.warning : u.theme.text);
    if (u.record_buttons) u.button_rects["anim_track:" + tr.label()] = lr;  // (tests)
    if (anim_.curves) {
      if (u.hovered(lr) && in.pressed[0]) anim_pick_curve((int)t, in.ctrl());
      continue;
    }
    for (const AnimKey &k : tr.keys) {
      const float cx = (float)anim_x_of(sheet, k.frame, end), cy = (float)(lr.y + row_h / 2);
      const bool sel = anim_selected((int)t, k.frame);
      const float s = (float)u.px(5);
      u.canvas.fill_triangle(Vec2(cx - s, cy), Vec2(cx, cy - s), Vec2(cx + s, cy), sel ? Color::hex(0x5AA9FF) : Color::hex(0xCCCCCC));
      u.canvas.fill_triangle(Vec2(cx - s, cy), Vec2(cx + s, cy), Vec2(cx, cy + s), sel ? Color::hex(0x5AA9FF) : Color::hex(0xCCCCCC));
    }
  }
}

/* A track name clicked in the Curves view: shown alone (Ctrl: added or taken away); the axis frames to it. */
void Editor::anim_pick_curve(int track, bool add) {
  auto it = std::find(anim_.curve_tracks.begin(), anim_.curve_tracks.end(), track);
  if (!add) anim_.curve_tracks = {track};
  else if (it != anim_.curve_tracks.end()) anim_.curve_tracks.erase(it);
  else anim_.curve_tracks.push_back(track);
  anim_.v_framed.clear();
  anim_.selected.erase(std::remove_if(anim_.selected.begin(), anim_.selected.end(),
                                      [&](const auto &s) { return std::find(anim_.curve_tracks.begin(), anim_.curve_tracks.end(), s.first) == anim_.curve_tracks.end(); }),
                       anim_.selected.end());
}

void Editor::draw_animation_window(const Recti &r) {
  auto &u = ui_;
  auto &in = u.in;
  if (u.hovered(r) && in.pressed[0]) focused_ = WindowKind::Animation;
  GameObject *root = anim_root();
  const int bh = u.row_h() + u.px(6);
  const Recti bar{r.x, r.y, r.w, bh};
  u.canvas.fill_rect(bar, u.theme.header);
  draw_anim_toolbar(bar, root);
  AnimationClipPtr clip = anim_clip();
  const Recti body{r.x, bar.bottom(), r.w, r.h - bh};
  if (!root || !clip) anim_drag_cancel();  // (nothing to drag in; a drag under way is dropped, not committed later)
  if (!root) {
    u.label({body.x + u.px(10), body.y + u.px(10), body.w, u.row_h()}, "Select an object to animate.", u.theme.text_dim);
    return;
  }
  if (!clip) {
    u.label({body.x + u.px(10), body.y + u.px(10), body.w - u.px(20), u.row_h()}, "To begin animating " + root->name + ", create an Animation Clip.",
            u.theme.text_dim);
    if (u.button({body.x + u.px(10), body.y + u.px(16) + u.row_h(), u.px(110), u.row_h() + u.px(4)}, "Create")) anim_new_clip(root, "");
    return;
  }
  draw_anim_add_property(root);
  const int row_h = u.row_h(), list_w = std::max(u.px(170), r.w * 3 / 10);
  const Recti list{body.x, body.y + row_h, list_w, body.h - row_h}, ruler{body.x + list_w, body.y, body.w - list_w - u.px(8), row_h};
  const Recti sheet{ruler.x, ruler.bottom(), ruler.w, body.h - row_h};
  const float end = std::max(30.0f, clip->end_frame()) * 1.1f;
  u.canvas.fill_rect({body.x, body.y, body.w, row_h}, Color::hex(0x2A2A2A));
  draw_anim_ruler(ruler, sheet, end);
  draw_anim_rows(*clip, root, list, body, sheet, row_h, end);
  anim_.sheet = sheet, anim_.sheet_end = end;
  if (anim_.curves) draw_anim_curves(*clip, sheet, end);
  /* The playhead (red while recording). */
  const int px = anim_x_of(sheet, anim_.frame, end);
  u.canvas.vline(px, ruler.y, body.bottom(), anim_.record ? Color::hex(0xFF3B3B) : Color::hex(0xFFFFFF));
  if (anim_.drag == 3) u.canvas.rect_outline({std::min(in.mx, anim_.drag_x), std::min(in.my, anim_.drag_y), std::abs(in.mx - anim_.drag_x), std::abs(in.my - anim_.drag_y)}, u.theme.focus);
  if (u.record_buttons) u.button_rects["anim_sheet"] = sheet, u.button_rects["anim_ruler"] = ruler;  // (tests)
  if (anim_.curves) anim_curve_input(*clip, ruler, sheet, end);
  else anim_sheet_input(*clip, ruler, sheet, row_h, end);
  u.popup(u.id("anim_key_ctx"), u.px(180), [this] {
    if (ui_.menu_item("Clamped Auto")) anim_set_tangent(TanAuto);
    if (ui_.menu_item("Free Smooth")) anim_set_tangent(TanFree);
    if (ui_.menu_item("Flat")) anim_set_tangent(TanFlat);
    if (ui_.menu_item("Broken")) anim_set_tangent(TanBroken);
    ui_.menu_separator();
    if (ui_.menu_item("Auto (curve)")) anim_set_interp(AnimAuto);
    if (ui_.menu_item("Linear")) anim_set_interp(AnimLinear);
    if (ui_.menu_item("Constant")) anim_set_interp(AnimConstant);
    ui_.menu_separator();
    if (ui_.menu_item("Delete Keys", "Del")) anim_delete_selected();
  });
  if (!u.hovered(r) || u.wants_keyboard()) return;
  if (in.key_pressed[platform::KEY_DELETE] || in.key_pressed[platform::KEY_BACKSPACE]) {
    anim_delete_selected();
    in.key_pressed[platform::KEY_DELETE] = in.key_pressed[platform::KEY_BACKSPACE] = false;
  }
  if (in.ctrl() && in.key_pressed[platform::KEY_C]) anim_copy(), in.key_pressed[platform::KEY_C] = false;
  if (in.ctrl() && in.key_pressed[platform::KEY_V]) anim_paste(), in.key_pressed[platform::KEY_V] = false;
}

/* ---------------------------------------------------------------- console */

/* anim tangent <object|.> <property> <axis> <frame> auto|free|flat|broken [in] [out]: the mode and slopes in one undo
 * step (slopes kept to +-1e6). */
void Editor::anim_tangent_command(const std::vector<std::string> &t, AnimationClip &clip, const std::string &path, const std::string &prop, int axis, float frame) {
  const std::string how = to_lower(t.size() > 6 ? t[6] : "auto");
  const int mode = how == "free" ? TanFree : how == "flat" ? TanFlat : how == "broken" ? TanBroken : how == "auto" ? TanAuto : -1;
  if (mode < 0) return Log::warn("anim tangent: auto, free, flat or broken, not '%s'", how.c_str());
  AnimTrack *tr = clip.find(path, prop, axis);
  const int k = tr ? tr->key_at(frame) : -1;
  if (k < 0) return Log::warn("anim tangent: no key on frame %g of %s.%d", frame, prop.c_str(), axis);
  const size_t index = (size_t)(tr - clip.tracks.data());
  auto slope = [&](size_t i, float def) {
    const float v = t.size() > i ? (float)std::atof(t[i].c_str()) : def;
    return std::isfinite(v) ? std::max(-1e6f, std::min(1e6f, v)) : def;
  };
  anim_edit_keys("Key Tangents", [&](AnimationClip &c) {
    AnimTrack &track = c.tracks[index];
    const size_t i = (size_t)track.key_at(frame);
    const float in = slope(7, track.in_tangent(i)), out = slope(8, t.size() > 7 ? in : track.out_tangent(i));
    AnimKey &key = track.keys[i];
    key.tangent = mode, key.in_slope = in, key.out_slope = mode == TanFree ? in : out;
    if (mode == TanFree || mode == TanBroken) key.interp = AnimAuto;
  });
}

/* anim new [name] | key <object> <property> <axis> <frame> [value] | interp <object> <property> <axis> <frame>
 * auto|linear|constant | frame <n> | preview on|off | record on|off | play | stop (task 0059). The object is by name;
 * "." is the root. */
void Editor::anim_command(const std::vector<std::string> &t) {
  auto arg = [&](size_t i, const std::string &def) { return t.size() > i ? t[i] : def; };
  const std::string verb = to_lower(arg(1, ""));
  auto on = [&](size_t i) { return to_lower(arg(i, "on")) != "off"; };
  if (verb == "new") return (void)anim_new_clip(anim_root(), arg(2, ""));
  if (verb == "frame") return anim_set_frame((float)std::atof(arg(2, "0").c_str()));
  if (verb == "preview") return anim_set_preview(on(2));
  if (verb == "record") {
    anim_.record = on(2);
    if (anim_.record) anim_set_preview(true);
    return;
  }
  if (verb == "play") {
    anim_set_preview(true);
    anim_.playing = true, anim_.play_from = anim_.frame, anim_.play_clock = now_seconds();
    return;
  }
  if (verb == "stop") return (void)(anim_.playing = false);
  GameObject *root = anim_root();
  AnimationClipPtr clip = anim_clip();
  if ((verb == "key" || verb == "interp" || verb == "tangent") && root && clip && t.size() >= 6) {
    GameObject *g = arg(2, ".") == "." ? root : scene_->find_by_name(t[2]);
    bool under = false;
    for (GameObject *x = g; x && !under; x = x->parent) under = x == root;
    if (!g || !under) return Log::warn("anim %s: no object '%s' below '%s'", verb.c_str(), t[2].c_str(), root->name.c_str());
    const std::string path = anim_path(*root, *g), prop = t[3];
    const int axis = std::max(0, std::min(2, std::atoi(t[4].c_str())));
    const float frame = (float)std::atof(t[5].c_str());
    if (verb == "tangent") return anim_tangent_command(t, *clip, path, prop, axis, frame);
    if (verb == "interp") {
      const std::string how = to_lower(arg(6, "auto"));
      const int k = how == "linear" ? AnimLinear : how == "constant" ? AnimConstant : AnimAuto;
      AnimTrack *tr = clip->find(path, prop, axis);
      if (tr && tr->key_at(frame) >= 0) anim_.selected = {{(int)(tr - clip->tracks.data()), frame}}, anim_set_interp(k);
      return;
    }
    float v = 0.0f;
    if (t.size() > 6) v = (float)std::atof(t[6].c_str());
    else read_anim_value(*g, prop, axis, v);
    anim_edit_keys("Add Key", [&](AnimationClip &c) { c.track(path, prop, axis).set_key(frame, v); });
    return;
  }
  Log::warn("anim new [name] | key <object|.> <property> <axis> <frame> [value] | interp <object|.> <property> <axis> <frame> "
            "auto|linear|constant | frame <n> | preview on|off | record on|off | play | stop");
}

}  // namespace bl

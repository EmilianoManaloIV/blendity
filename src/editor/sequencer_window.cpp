// SPDX-License-Identifier: GPL-2.0-or-later
// The Sequencer window (task 0062, ADR 0030): Premiere's timeline for animation clips. The sequence of the selected
// object's Sequencer component: tracks of clip bars along a ruler (drag a .anim from the Project window onto a track,
// drag a bar to move it, its ends to trim it; overlaps crossfade), the selected bar's settings, and the triggers that
// jump to a clip in Play. Preview poses the scene without an undo step, as the Animation window's.
#include "editor.h"

#include "../scene/sequence.h"
#include "../scene/scene.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace bl {

namespace {

std::string seq_file_safe(std::string n) {
  for (char &ch : n)
    if (std::strchr("/\\:*?\"<>|", ch)) ch = '_';
  while (!n.empty() && (n.front() == '.' || n.front() == ' ')) n.erase(n.begin());
  return n.empty() ? std::string("Sequence") : n;
}

std::vector<GameObject *> seq_subtree(GameObject *root) {
  std::vector<GameObject *> out, stack{root};
  while (!stack.empty()) {
    GameObject *g = stack.back();
    stack.pop_back();
    out.push_back(g);
    for (GameObject *c : g->children) stack.push_back(c);
  }
  return out;
}

/* A bar's colour from its clip's name: the same clip is the same colour wherever it is. */
uint32_t bar_colour(const std::string &clip) {
  uint32_t h = 2166136261u;
  for (char c : clip) h = (h ^ (uint8_t)c) * 16777619u;
  static const uint32_t kColours[] = {0x3F6FA8, 0x4E8A55, 0x8A5A9E, 0xA0723A, 0x3E8C8C, 0x9A4B5C};
  return Color::hex(kColours[h % 6]);
}

}  // namespace

/* ---------------------------------------------------------------- what is sequenced */

GameObject *Editor::seq_root() {
  for (GameObject *a = active_object(); a; a = a->parent)
    if (a->get<Sequencer>()) return a;
  GameObject *rig = rig_root_of(active_object());
  return rig ? rig : active_object();
}

SequencePtr Editor::seq_sequence() {
  GameObject *root = seq_root();
  const Sequencer *s = root ? root->get<Sequencer>() : nullptr;
  return s ? sequence_asset(s->sequence) : nullptr;
}

/* Create New Sequence: Assets/Animations/<root's name> Sequence.seq (a free name), and a Sequencer playing it. */
SequencePtr Editor::seq_new(GameObject *root, const std::string &name_in) {
  if (!root) {
    Log::warn("Sequencer: select an object to sequence");
    return nullptr;
  }
  if (playing_) return nullptr;
  const std::string base = seq_file_safe(name_in.empty() ? root->name + " Sequence" : name_in);
  std::string path = "Assets/Animations/" + base + ".seq";
  for (int k = 1; fs::exists(resolve_asset_path(path)); k++) path = strprintf("Assets/Animations/%s %d.seq", base.c_str(), k);
  SequencePtr s = create_sequence_asset(path, fs::stem(path));
  if (!s) {
    Log::warn("Sequencer: couldn't write %s", path.c_str());
    return nullptr;
  }
  Sequencer *c = root->get<Sequencer>();
  if (!c) c = root->add<Sequencer>();
  c->sequence = path;
  seqr_.root = root->id;
  seqr_.selected_name.clear();
  invalidate_project_listing();
  mark_changed("Create Sequence");
  Log::info("Sequencer: %s on '%s'", path.c_str(), root->name.c_str());
  return s;
}

/* A sequence's text before its first edit since the last undo step (beside the clips': the undo state tells them apart
 * by extension). */
void Editor::note_sequence_edit(Sequence &s) {
  if (s.asset_path.empty()) return;
  for (const auto &kv : clip_before_)
    if (kv.first == s.asset_path) return;
  clip_before_.push_back({s.asset_path, save_sequence_text(s)});
}

bool Editor::seq_edit(const char *label, const std::function<void(Sequence &)> &edit) {
  SequencePtr s = seq_sequence();
  if (!s || playing_) return false;  // (Play's changes are thrown away on Stop; an asset's would not be)
  note_sequence_edit(*s);
  edit(*s);
  for (SeqTrack &t : s->tracks) t.sort();
  s->touch();
  mark_changed(label);
  seq_apply();
  return true;
}

/* ---------------------------------------------------------------- preview */

void Editor::seq_apply() {
  if (!seqr_.preview) return;
  GameObject *root = scene_->find(seqr_.root);  // (the root Preview saved: selecting another object doesn't pose that one)
  const Sequencer *c = root ? root->get<Sequencer>() : nullptr;
  SequencePtr s = c ? sequence_asset(c->sequence) : nullptr;
  if (!s) return;
  AnimValues v;
  sample_sequence(*s, seqr_.frame, v);
  apply_anim_values(v, *root);
  solve_constraints(*scene_);
  scene_dirty_ = true;
}

void Editor::seq_set_frame(float f) {
  seqr_.frame = std::isfinite(f) ? std::max(0.0f, std::min(1e6f, std::round(f))) : 0.0f;
  seq_apply();
}

/* What Preview puts back: the transforms below the root (and the bones IK turns), and the component numbers any clip
 * of the sequence animates. */
void Editor::seq_save_pose(GameObject &root) {
  seqr_.saved_pose.clear();
  for (GameObject *g : seq_subtree(&root)) seqr_.saved_pose.push_back({g->id, g->local()});
  scene_->for_each([&](GameObject &g) {
    if (g.get<Bone>() && ik_drives(*scene_, g)) seqr_.saved_pose.push_back({g.id, g.local()});
  });
  seqr_.saved_values.clear();
  SequencePtr s = seq_sequence();
  if (!s) return;
  AnimValues names, now;
  for (const SeqTrack &t : s->tracks)
    for (const SeqInstance &i : t.items)
      if (AnimationClipPtr c = clip_asset(i.clip)) sample_clip_values(*c, 0.0f, names);
  read_anim_values(names, root, now);
  for (const AnimValue &v : now.values) {
    if (is_transform_property(v.track->property)) continue;
    if (GameObject *g = anim_target(root, v.track->path)) seqr_.saved_values.push_back({g->id, v.track->property, v.track->axis, v.value});
  }
}

/* Preview on: the pose now is kept to put back (the Animation window's Preview steps aside: one thing poses the scene
 * at a time). Off: it's put back. */
void Editor::seq_set_preview(bool on) {
  if (on == seqr_.preview) return;
  GameObject *root = seq_root();
  if (on && (playing_ || !root)) {
    if (playing_) Log::warn("Sequencer: Preview is for editing; Stop Play first");
    return;
  }
  if (on) {
    anim_set_preview(false);
    seq_save_pose(*root);
    seqr_.preview = true;
    seqr_.root = root->id;
    seq_apply();
    return;
  }
  seqr_.preview = seqr_.playing = false;
  commit_undo();
  for (const auto &[id, t] : seqr_.saved_pose)
    if (GameObject *g = scene_->find(id)) g->set_local(t);
  for (const auto &[id, prop, axis, v] : seqr_.saved_values)
    if (GameObject *g = scene_->find(id)) write_anim_value(*g, prop, axis, v);
  seqr_.saved_pose.clear();
  seqr_.saved_values.clear();
  solve_constraints(*scene_);
  stable_ = scene_->clone();
  scene_dirty_ = true;
}

void Editor::seq_tick() {
  if (!seqr_.playing || !seqr_.preview) return;
  SequencePtr s = seq_sequence();
  if (!s) {
    seqr_.playing = false;
    return;
  }
  const float end = std::max(1.0f, s->end_frame());
  seqr_.frame = std::fmod(seqr_.play_from + (float)(now_seconds() - seqr_.play_clock) * s->frame_rate, end);
  seq_apply();
  ui_.redraw = true;
}

/* Another scene, or the sequence changed under the window (undo): nothing it holds is still true. */
void Editor::seq_reset() {
  seqr_ = SeqEditorState{};
}

void Editor::seq_drag_cancel() {
  seqr_.drag = 0;
}

/* ---------------------------------------------------------------- editing */

/* A clip onto a track at a frame (a new instance named after the clip). */
bool Editor::seq_add_clip(const std::string &clip_in, int track, float start) {
  std::string clip = clip_in;
  std::replace(clip.begin(), clip.end(), '\\', '/');
  if (!clip_asset(clip)) {
    Log::warn("Sequencer: no clip %s", clip.c_str());
    return false;
  }
  SequencePtr s = seq_sequence();
  if (!s || !std::isfinite(start)) return false;
  if (track < 0 || track > (int)s->tracks.size()) {  // (the next track up is made; further is a typo)
    Log::warn("Sequencer: track %d isn't there (0 to %zu)", track, s->tracks.size());
    return false;
  }
  return seq_edit("Add Clip to Sequence", [&](Sequence &q) {
    if (track == (int)q.tracks.size()) {
      q.tracks.emplace_back();
      q.tracks.back().name = "Track " + std::to_string(q.tracks.size());
    }
    SeqInstance i;
    i.clip = clip, i.start = std::max(0.0f, std::round(start)), i.name = q.unique_name(fs::stem(clip));
    q.tracks[(size_t)track].items.push_back(i);
    seqr_.selected_name = i.name;
  });
}

/* The selected instance: its track and index, found again by name (sorting moves it). */
SeqInstance *Editor::seq_selected(Sequence &s, int *track, int *index) {
  if (seqr_.selected_name.empty()) return nullptr;
  return s.find(seqr_.selected_name, track, index);
}

bool Editor::seq_move(const std::string &name, int track, float start) {
  SequencePtr s = seq_sequence();
  int t = -1, k = -1;
  if (!s || !s->find(name, &t, &k) || track < 0 || track >= (int)s->tracks.size() || !std::isfinite(start)) return false;
  return seq_edit("Move Clip", [&](Sequence &q) {
    SeqInstance i = q.tracks[(size_t)t].items[(size_t)k];
    q.tracks[(size_t)t].items.erase(q.tracks[(size_t)t].items.begin() + k);
    i.start = std::max(0.0f, std::round(start));
    q.tracks[(size_t)track].items.push_back(i);
  });
}

/* Trimmed to clip frames [in, out] (out < 0: the clip's end); at least a frame long. */
bool Editor::seq_trim(const std::string &name, float trim_in, float trim_out, float start) {
  SequencePtr s = seq_sequence();
  SeqInstance *i = s ? s->find(name) : nullptr;
  if (!i || !std::isfinite(trim_in) || !std::isfinite(trim_out) || !std::isfinite(start)) return false;
  const float end = clip_asset(i->clip) ? clip_asset(i->clip)->end_frame() : 60.0f;
  return seq_edit("Trim Clip", [&](Sequence &q) {
    SeqInstance *x = q.find(name);
    x->trim_in = std::max(0.0f, std::min(std::round(trim_in), end - 1.0f));
    x->trim_out = trim_out < 0.0f ? -1.0f : std::max(x->trim_in + 1.0f, std::min(std::round(trim_out), end));
    x->start = std::max(0.0f, std::round(start));
  });
}

void Editor::seq_delete_selected() {
  SequencePtr s = seq_sequence();
  int t = -1, k = -1;
  if (!s || !seq_selected(*s, &t, &k)) return;
  seq_edit("Delete Clip", [&](Sequence &q) { q.tracks[(size_t)t].items.erase(q.tracks[(size_t)t].items.begin() + k); });
  seqr_.selected_name.clear();
}

/* ---------------------------------------------------------------- the window */

float Editor::seq_frame_at(const Recti &lanes, int x, float end) const { return (float)(x - lanes.x) / (float)std::max(1, lanes.w) * end; }
int Editor::seq_x_of(const Recti &lanes, float frame, float end) const { return lanes.x + (int)std::lround(frame / end * (float)lanes.w); }

/* The bar under (mx, my): its instance name, and which part (0 the middle, 1 the left end, 2 the right end). */
bool Editor::seq_bar_under(const Sequence &s, const Recti &lanes, int lane_h, float end, int mx, int my, std::string &name, int &part) {
  const int t = (my - lanes.y) / std::max(1, lane_h);
  if (my < lanes.y || t < 0 || t >= (int)s.tracks.size()) return false;
  bool hit = false;
  for (const SeqInstance &i : s.tracks[(size_t)t].items) {  // (the last hit: a later bar is drawn on top, as Premiere's)
    const int x0 = seq_x_of(lanes, i.start, end), x1 = seq_x_of(lanes, i.start + i.duration(clip_asset(i.clip).get()), end);
    const int g = std::max(2, std::min(ui_.px(6), (x1 - x0) / 3));  // (a short bar's grips shrink with it)
    if (mx < x0 - g / 2 || mx > x1 + g / 2) continue;
    name = i.name, hit = true;
    part = 0;
    if (mx <= x0 + g) part = 1;
    else if (mx >= x1 - g) part = 2;
  }
  return hit;
}

void Editor::draw_seq_toolbar(const Recti &bar, GameObject *root) {
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
  if (button(u.px(64), "Preview", seqr_.preview, "Shows the sequence at the playhead on the scene; turning it off puts the scene back.")) seq_set_preview(!seqr_.preview);
  if (button(u.px(26), "|<", false, "First frame")) seq_set_frame(0.0f);
  if (button(u.px(30), seqr_.playing ? "||" : ">", seqr_.playing, "Play the sequence in the Scene view (Preview)")) {
    seqr_.playing = !seqr_.playing;
    if (seqr_.playing) seq_set_preview(true), seqr_.play_from = seqr_.frame, seqr_.play_clock = now_seconds();
  }
  int fr = (int)seqr_.frame;
  if (u.int_field(u.id("seq_frame"), {x, y, u.px(56), bh}, fr, 0, 1000000)) seq_set_frame((float)fr);
  x += u.px(60);
  if (!root) return;
  /* The sequence menu: the sequence assets, then Create New Sequence. */
  SequencePtr s = seq_sequence();
  std::vector<std::string> names{"(no sequence)"};
  const std::vector<std::string> &paths = seq_paths();
  int cur = 0;
  for (size_t i = 0; i < paths.size(); i++) {
    names.push_back(fs::stem(paths[i]));
    if (s && s->asset_path == paths[i]) cur = (int)i + 1;
  }
  names.push_back("Create New Sequence...");
  std::vector<const char *> cn;
  for (const std::string &n : names) cn.push_back(n.c_str());
  int pick = cur;
  if (u.combo(u.id("seq_pick"), {x, y, u.px(160), bh}, pick, cn.data(), (int)cn.size()) && pick != cur) {
    if (pick == (int)names.size() - 1) seq_new(root, "");
    else seq_pick(root, pick ? paths[(size_t)pick - 1] : "");
  }
  x += u.px(164);
  if (button(u.px(76), "Add Track", false, "A new track on top: its clips win over the ones below for the numbers they animate.") && s)
    seq_edit("Add Track", [](Sequence &q) {
      q.tracks.emplace_back();
      q.tracks.back().name = "Track " + std::to_string(q.tracks.size());
    });
}

/* The sequence menu's choice: a sequence asset for the root's Sequencer, or none (""). */
void Editor::seq_pick(GameObject *root, const std::string &path) {
  seq_set_preview(false);
  Sequencer *c = root->get<Sequencer>();
  if (!c) c = root->add<Sequencer>();
  c->sequence = path;
  seqr_.root = root->id;
  seqr_.selected_name.clear();
  seq_drag_cancel();
  mark_changed("Sequence");
}

const std::vector<std::string> &Editor::seq_paths() {
  if (seq_paths_listed_ >= 0.0) return seq_paths_;
  seq_paths_listed_ = now_seconds();
  seq_paths_.clear();
  std::vector<std::string> dirs{assets_dir_};
  while (!dirs.empty()) {
    const std::string d = dirs.back();
    dirs.pop_back();
    for (const DirEntry &de : fs::list(d)) {
      const std::string p = fs::join(d, de.name);
      if (de.is_dir) dirs.push_back(p);
      else if (fs::extension(de.name) == ".seq") seq_paths_.push_back(make_asset_relative(p));
    }
  }
  for (std::string &p : seq_paths_) std::replace(p.begin(), p.end(), '\\', '/');
  std::sort(seq_paths_.begin(), seq_paths_.end());
  return seq_paths_;
}

/* The bars: each instance from its start for its length, its name on it; where two on a track overlap, the crossfade
 * as a cross; the dragged bar where it would land. */
void Editor::draw_seq_lanes(const Sequence &s, const Recti &list, const Recti &lanes, int lane_h, float end) {
  auto &u = ui_;
  auto &in = u.in;
  for (size_t t = 0; t < s.tracks.size(); t++) {
    const SeqTrack &tr = s.tracks[t];
    const int ly = lanes.y + (int)t * lane_h;
    if (ly > lanes.bottom()) break;
    if (t % 2) u.canvas.fill_rect({list.x, ly, list.w + lanes.w + u.px(8), lane_h}, Color::hex(0x262626));
    u.label({list.x + u.px(6), ly, list.w - u.px(8), lane_h}, tr.name, u.theme.text);
    drop_seq_tracks_.push_back({Recti{lanes.x, ly, lanes.w, lane_h}, (int)t});
    for (size_t k = 0; k < tr.items.size(); k++) {
      const SeqInstance &i = tr.items[k];
      const AnimationClipPtr c = clip_asset(i.clip);
      const int x0 = seq_x_of(lanes, i.start, end), x1 = seq_x_of(lanes, i.start + i.duration(c.get()), end);
      const Recti b{x0, ly + u.px(3), std::max(u.px(4), x1 - x0), lane_h - u.px(6)};
      u.canvas.fill_rect(b, c ? bar_colour(i.clip) : Color::hex(0x7A6A2A));
      if (i.name == seqr_.selected_name) u.canvas.rect_outline(b, u.theme.focus, u.px(2));
      u.label({b.x + u.px(4), b.y, b.w - u.px(6), b.h}, c ? i.name : i.name + " (missing)", u.theme.text_bright);
      if (u.record_buttons) u.button_rects["seq_bar:" + i.name] = b;  // (tests)
      if (k == 0) continue;
      /* The overlap with the one before: a cross over it (Premiere's cross dissolve). */
      const SeqInstance &p = tr.items[k - 1];
      const float p_end = p.start + p.duration(clip_asset(p.clip).get());
      if (p_end <= i.start) continue;
      const int ox1 = seq_x_of(lanes, std::min(p_end, i.start + i.duration(c.get())), end);
      const uint32_t cross = Color::hex(0xE0E0E0);
      u.canvas.line((float)x0, (float)b.y, (float)ox1, (float)b.bottom(), cross);
      u.canvas.line((float)x0, (float)b.bottom(), (float)ox1, (float)b.y, cross);
    }
  }
  if (seqr_.drag < 2 || seqr_.drag > 4) return;
  /* The dragged bar's outline where it would land. */
  SeqInstance ghost;
  int track = 0;
  if (!seq_drag_result(s, lanes, lane_h, end, in.mx, in.my, ghost, track)) return;
  const AnimationClipPtr c = clip_asset(ghost.clip);
  const int gy = lanes.y + track * lane_h + u.px(3);
  const int gx0 = seq_x_of(lanes, ghost.start, end), gx1 = seq_x_of(lanes, ghost.start + ghost.duration(c.get()), end);
  u.canvas.rect_outline({gx0, gy, std::max(u.px(4), gx1 - gx0), lane_h - u.px(6)}, Color::hex(0xFFFFFF), u.px(2));
}

/* What the drag under way would make of the dragged instance: moved (to the lane under the mouse) or trimmed. */
bool Editor::seq_drag_result(const Sequence &s, const Recti &lanes, int lane_h, float end, int mx, int my, SeqInstance &out, int &track) const {
  const SeqInstance *i = s.find(seqr_.drag_name);
  if (!i || seqr_.drag_seq != s.asset_path) return false;
  out = *i;
  track = seqr_.drag_track;
  const float df = std::round(seq_frame_at(lanes, mx, end) - seq_frame_at(lanes, seqr_.drag_x, end));
  const AnimationClipPtr c = clip_asset(i->clip);
  const float clip_end = c ? c->end_frame() : 60.0f, sp = std::max(1e-3f, std::fabs(i->speed));
  if (seqr_.drag == 2) {
    out.start = std::max(0.0f, i->start + df);
    track = std::max(0, std::min((int)s.tracks.size() - 1, (my - lanes.y) / std::max(1, lane_h)));
    return true;
  }
  if (seqr_.drag == 3) {  // the left end: the in point and the start move together
    const float din = std::max(-i->trim_in, std::min(df * sp, i->clip_out(c.get()) - 1.0f - i->trim_in));
    out.trim_in = i->trim_in + din;
    out.start = std::max(0.0f, i->start + din / sp);
    return true;
  }
  const float out_now = i->clip_out(c.get());  // the right end: the out point
  out.trim_out = std::max(i->trim_in + 1.0f, std::min(clip_end, out_now + df * sp));
  return true;
}

void Editor::seq_lanes_input(const Sequence &s, const Recti &ruler, const Recti &lanes, int lane_h, float end) {
  auto &u = ui_;
  auto &in = u.in;
  const int mx = in.mx, my = in.my;
  if (seqr_.drag == 1 && in.down[0]) seq_set_frame(seq_frame_at(lanes, mx, end));
  if (!in.down[0] && seqr_.drag) seq_end_drag(s, lanes, lane_h, end);
  if (u.hovered(ruler) && in.pressed[0]) {
    seqr_.drag = 1;
    seq_set_frame(seq_frame_at(lanes, mx, end));
    return;
  }
  if (!u.hovered(lanes)) return;
  std::string name;
  int part = 0;
  const bool on_bar = seq_bar_under(s, lanes, lane_h, end, mx, my, name, part);
  if (in.pressed[1] && on_bar) {
    seqr_.selected_name = name;
    u.open_popup(u.id("seq_bar_ctx"), {mx, my, 0, 0});
    return;
  }
  if (!in.pressed[0]) return;
  if (!on_bar) {
    seqr_.selected_name.clear();
    return;
  }
  seqr_.selected_name = name;
  int t = -1;
  const_cast<Sequence &>(s).find(name, &t);
  seqr_.drag = part == 1 ? 3 : part == 2 ? 4 : 2;
  seqr_.drag_name = name, seqr_.drag_seq = s.asset_path, seqr_.drag_track = t;
  seqr_.drag_x = mx, seqr_.drag_y = my;
}

void Editor::seq_end_drag(const Sequence &s, const Recti &lanes, int lane_h, float end) {
  const int kind = seqr_.drag;
  SeqInstance done;
  int track = 0;
  const bool ok = kind >= 2 && seq_drag_result(s, lanes, lane_h, end, ui_.in.mx, ui_.in.my, done, track);
  seq_drag_cancel();
  if (!ok || (ui_.in.mx == seqr_.drag_x && ui_.in.my == seqr_.drag_y)) return;  // (a click: nothing moved)
  if (kind == 2) seq_move(done.name, track, done.start);
  else seq_trim(done.name, done.trim_in, done.trim_out, done.start);
}

/* The selected bar's settings (Premiere's Effect Controls, roughly): name, speed, in and out. */
void Editor::draw_seq_details(Sequence &s, const Recti &r) {
  auto &u = ui_;
  SeqInstance *i = seq_selected(s);
  int x = r.x + u.px(6);
  const int y = r.y + u.px(3), h = u.row_h();
  if (!i) {
    u.label({x, y, r.w, h}, "Drag a clip (.anim) from the Project window onto a track; click a bar for its settings.", u.theme.text_dim);
    return;
  }
  const SeqInstance before = *i;
  auto label = [&](const char *text, int w) {
    u.label({x, y, w, h}, text, u.theme.text_dim);
    x += w;
  };
  label("Name", u.px(40));
  std::string name = i->name;
  bool committed = false;
  u.text_field(u.id("seq_name"), {x, y, u.px(120), h}, name, &committed);
  x += u.px(126);
  if (committed && name != before.name && !name.empty()) seq_rename(before.name, name);
  float speed = i->speed, tin = i->trim_in, tout = i->clip_out(clip_asset(i->clip).get());
  label("Speed", u.px(44));
  const bool sp = u.float_field(u.id("seq_speed"), {x, y, u.px(56), h}, speed, 0.01f, 0.05f, 10.0f);
  x += u.px(62);
  label("In", u.px(22));
  const bool ti = u.float_field(u.id("seq_in"), {x, y, u.px(56), h}, tin, 1.0f, 0.0f, 1e6f, "%.0f");
  x += u.px(62);
  label("Out", u.px(30));
  const bool to = u.float_field(u.id("seq_out"), {x, y, u.px(56), h}, tout, 1.0f, 0.0f, 1e6f, "%.0f");
  x += u.px(62);
  if (sp && std::isfinite(speed)) seq_edit("Clip Speed", [&](Sequence &q) { q.find(before.name)->speed = std::max(0.05f, std::min(10.0f, speed)); });
  if (ti || to) seq_trim(before.name, tin, to ? tout : before.trim_out, before.start);
  if (u.button({x, y, u.px(60), h}, "Go To")) seq_set_frame(before.start);
}

bool Editor::seq_rename(const std::string &from, const std::string &to) {
  SequencePtr s = seq_sequence();
  if (!s || !s->find(from) || to.empty() || from == to) return false;
  if (s->find(to)) {
    Log::warn("Sequencer: there is already a clip named '%s'", to.c_str());
    return false;
  }
  return seq_edit("Rename Clip", [&](Sequence &q) {
    q.find(from)->name = to;
    for (SeqTrigger &g : q.triggers)
      if (g.instance == from) g.instance = to;  // (triggers follow the name)
    if (seqr_.selected_name == from) seqr_.selected_name = to;
  });
}

/* The triggers: a key, the clip it jumps to, the blend time; Add Trigger. */
void Editor::draw_seq_triggers(Sequence &s, const Recti &r) {
  auto &u = ui_;
  const int h = u.row_h();
  int y = r.y + u.px(3);
  u.label({r.x + u.px(6), y, r.w, h}, "Triggers (in Play, the key jumps to the clip, blending over the seconds)", u.theme.text_dim);
  y += h + u.px(2);
  std::vector<std::string> names;
  for (const SeqTrack &t : s.tracks)
    for (const SeqInstance &i : t.items) names.push_back(i.name);
  std::vector<const char *> cn;
  for (const std::string &n : names) cn.push_back(n.c_str());
  for (size_t k = 0; k < s.triggers.size() && y + h <= r.bottom(); k++, y += h + u.px(2)) {
    const SeqTrigger g = s.triggers[k];
    int x = r.x + u.px(6);
    std::string key = g.key;
    bool committed = false;
    u.text_field(u.id("seq_tkey" + std::to_string(k)), {x, y, u.px(56), h}, key, &committed, "Key");
    x += u.px(62);
    if (committed && key != g.key) seq_edit("Trigger Key", [&](Sequence &q) { q.triggers[k].key = key; });
    int pick = (int)(std::find(names.begin(), names.end(), g.instance) - names.begin());
    if (!cn.empty() && u.combo(u.id("seq_tclip" + std::to_string(k)), {x, y, u.px(140), h}, pick, cn.data(), (int)cn.size()) && pick < (int)names.size())
      seq_edit("Trigger Clip", [&](Sequence &q) { q.triggers[k].instance = names[(size_t)pick]; });
    x += u.px(146);
    float blend = g.blend;
    if (u.float_field(u.id("seq_tblend" + std::to_string(k)), {x, y, u.px(56), h}, blend, 0.01f, 0.0f, 10.0f)) seq_edit("Trigger Blend", [&](Sequence &q) { q.triggers[k].blend = blend; });
    x += u.px(62);
    if (u.button({x, y, u.px(24), h}, "x")) {
      seq_edit("Remove Trigger", [&](Sequence &q) { q.triggers.erase(q.triggers.begin() + (std::ptrdiff_t)k); });
      break;
    }
  }
  if (y + h <= r.bottom() && u.button({r.x + u.px(6), y, u.px(100), h}, "Add Trigger") && !names.empty())
    seq_edit("Add Trigger", [&](Sequence &q) { q.triggers.push_back({std::to_string(std::min<size_t>(9, q.triggers.size() + 1)), names[0], 0.25f}); });
}

void Editor::draw_sequencer_window(const Recti &r) {
  auto &u = ui_;
  auto &in = u.in;
  if (u.hovered(r) && in.pressed[0]) focused_ = WindowKind::Sequencer;
  seqr_.drawn = true;
  if (seqr_.drag >= 2 && in.key_pressed[platform::KEY_ESCAPE]) seq_drag_cancel(), in.key_pressed[platform::KEY_ESCAPE] = false;  // (as Premiere: Esc drops a drag)
  GameObject *root = seq_root();
  const int bh = u.row_h() + u.px(6);
  const Recti bar{r.x, r.y, r.w, bh};
  u.canvas.fill_rect(bar, u.theme.header);
  draw_seq_toolbar(bar, root);
  SequencePtr s = seq_sequence();
  const Recti body{r.x, bar.bottom(), r.w, r.h - bh};
  if (!root || !s) seq_drag_cancel();
  if (!root) {
    u.label({body.x + u.px(10), body.y + u.px(10), body.w, u.row_h()}, "Select an object to sequence its clips.", u.theme.text_dim);
    return;
  }
  if (!s) {
    u.label({body.x + u.px(10), body.y + u.px(10), body.w - u.px(20), u.row_h()}, "To lay out clips for " + root->name + ", create a Sequence.", u.theme.text_dim);
    if (u.button({body.x + u.px(10), body.y + u.px(16) + u.row_h(), u.px(110), u.row_h() + u.px(4)}, "Create")) seq_new(root, "");
    return;
  }
  const int row_h = u.row_h(), lane_h = row_h + u.px(10), list_w = std::max(u.px(110), r.w / 6);
  const int trig_h = (int)std::min<size_t>(s->triggers.size() + 2, 6) * (row_h + u.px(2)) + u.px(6), det_h = row_h + u.px(8);
  const Recti ruler{body.x + list_w, body.y, body.w - list_w - u.px(8), row_h};
  const Recti lanes{ruler.x, ruler.bottom(), ruler.w, std::max(lane_h, body.h - row_h - det_h - trig_h)};
  const Recti list{body.x, lanes.y, list_w, lanes.h};
  const Recti details{body.x, lanes.bottom(), body.w, det_h}, triggers{body.x, details.bottom(), body.w, trig_h};
  const float end = std::max(60.0f, s->end_frame()) * 1.15f;
  u.canvas.fill_rect({body.x, body.y, body.w, row_h}, Color::hex(0x2A2A2A));
  draw_anim_ruler(ruler, lanes, end);
  draw_seq_lanes(*s, list, lanes, lane_h, end);
  seqr_.lanes = lanes, seqr_.lane_h = lane_h, seqr_.end = end;
  const int px = seq_x_of(lanes, seqr_.frame, end);
  u.canvas.vline(px, ruler.y, lanes.bottom(), Color::hex(0xFFFFFF));
  if (u.record_buttons) u.button_rects["seq_lanes"] = lanes, u.button_rects["seq_ruler"] = ruler;  // (tests)
  seq_lanes_input(*s, ruler, lanes, lane_h, end);
  u.canvas.fill_rect(details, Color::hex(0x2C2C2C));
  draw_seq_details(*s, details);
  draw_seq_triggers(*s, triggers);
  u.popup(u.id("seq_bar_ctx"), u.px(160), [this] {
    SequencePtr q = seq_sequence();
    SeqInstance *i = q ? seq_selected(*q) : nullptr;
    if (!i) return;
    const std::string name = i->name;
    const float start = i->start;
    if (ui_.menu_item("Go To")) seq_set_frame(start);
    for (float sp : {0.5f, 1.0f, 2.0f})
      if (ui_.menu_item(strprintf("Speed %gx", sp), nullptr, std::fabs(i->speed - sp) < 1e-4f))
        seq_edit("Clip Speed", [&](Sequence &x) { x.find(name)->speed = sp; });
    ui_.menu_separator();
    if (ui_.menu_item("Delete", "Del")) seq_delete_selected();
  });
  if (!u.hovered(r) || u.wants_keyboard()) return;
  if (in.key_pressed[platform::KEY_DELETE] || in.key_pressed[platform::KEY_BACKSPACE]) {
    seq_delete_selected();
    in.key_pressed[platform::KEY_DELETE] = in.key_pressed[platform::KEY_BACKSPACE] = false;
  }
}

/* A .anim dropped from the Project window: onto the track under the mouse, starting where it was dropped. */
bool Editor::seq_drop_clip(const std::string &abs_path, int mx, int my) {
  for (const auto &[rect, track] : drop_seq_tracks_) {
    if (!rect.contains(mx, my)) continue;
    return seq_add_clip(make_asset_relative(abs_path), track, seq_frame_at(seqr_.lanes, mx, seqr_.end));
  }
  return false;  // (dropped elsewhere: a drag let go of)
}

/* ---------------------------------------------------------------- console */

/* seq new [name] | add <clip> <track> <start> | move <clip> <track> <start> | trim <clip> <in> <out> | speed <clip> <s> |
 * rename <clip> <name> | remove <clip> | track | trigger <key> <clip> [blend] | jump <clip> [blend] | frame <n> |
 * preview on|off | play | stop. Clips are named by their instance names. */
void Editor::seq_command(const std::vector<std::string> &t) {
  auto arg = [&](size_t i, const std::string &def) { return t.size() > i ? t[i] : def; };
  auto num = [&](size_t i, float def) { return t.size() > i ? (float)std::atof(t[i].c_str()) : def; };
  auto index = [&](size_t i) {  // a track number: -1 when it isn't one (NaN, huge)
    const double v = t.size() > i ? std::atof(t[i].c_str()) : -1.0;
    return std::isfinite(v) && v >= 0.0 && v < 1e6 ? (int)v : -1;
  };
  const std::string verb = to_lower(arg(1, ""));
  if (verb == "new") return (void)seq_new(seq_root(), arg(2, ""));
  if (verb == "frame") return seq_set_frame(num(2, 0.0f));
  if (verb == "preview") return seq_set_preview(to_lower(arg(2, "on")) != "off");
  if (verb == "play") {
    seq_set_preview(true);
    seqr_.playing = true, seqr_.play_from = seqr_.frame, seqr_.play_clock = now_seconds();
    return;
  }
  if (verb == "stop") return (void)(seqr_.playing = false);
  if (verb == "jump") return seq_jump_command(arg(2, ""), num(3, -1.0f));
  SequencePtr s = seq_sequence();
  if (!s) return Log::warn("seq %s: the selected object has no sequence (seq new)", verb.c_str());
  if (verb == "add" && t.size() >= 5) return (void)seq_add_clip(t[2], index(3), num(4, 0));
  if (verb == "move" && t.size() >= 5) return (void)seq_move(t[2], index(3), num(4, 0));
  if (verb == "trim" && t.size() >= 5) {
    const SeqInstance *i = s->find(t[2]);
    if (i) seq_trim(t[2], num(3, 0), num(4, -1), i->start);
    return;
  }
  if (verb == "speed" && t.size() >= 4 && s->find(t[2])) {
    const float sp = num(3, 1.0f);
    if (std::isfinite(sp) && sp > 0.0f) seq_edit("Clip Speed", [&](Sequence &q) { q.find(t[2])->speed = std::max(0.05f, std::min(10.0f, sp)); });
    return;
  }
  if (verb == "rename" && t.size() >= 4) return (void)seq_rename(t[2], t[3]);
  if (verb == "remove" && t.size() >= 3 && s->find(t[2])) {
    seqr_.selected_name = t[2];
    return seq_delete_selected();
  }
  if (verb == "track")
    return (void)seq_edit("Add Track", [](Sequence &q) {
      q.tracks.emplace_back();
      q.tracks.back().name = "Track " + std::to_string(q.tracks.size());
    });
  if (verb == "trigger" && t.size() >= 4) {
    if (!s->find(t[3])) return Log::warn("seq trigger: no clip named '%s' in the sequence", t[3].c_str());
    if (!trigger_key_code(t[2])) return Log::warn("seq trigger: a key is a letter, a digit, Space or F1-F12, not '%s'", t[2].c_str());
    const float blend = std::max(0.0f, std::min(10.0f, num(4, 0.25f)));
    seq_edit("Add Trigger", [&](Sequence &q) { q.triggers.push_back({t[2], t[3], std::isfinite(blend) ? blend : 0.25f}); });
    return;
  }
  Log::warn("seq new [name] | add <clip.anim> <track> <start> | move <name> <track> <start> | trim <name> <in> <out> | "
            "speed <name> <s> | rename <name> <new> | remove <name> | track | trigger <key> <name> [blend] | jump <name> [blend] | "
            "frame <n> | preview on|off | play | stop");
}

/* seq jump <clip> [blend] (Play): the Sequencer of the selected object (or the scene's first) jumps, as its trigger
 * would. */
void Editor::seq_jump_command(const std::string &name, float blend) {
  if (!playing_) return Log::warn("seq jump: in Play only (Ctrl+P)");
  Sequencer *c = nullptr;
  for (GameObject *a = active_object(); a && !c; a = a->parent) c = a->get<Sequencer>();
  if (!c) scene_->for_each([&](GameObject &g) {
      if (!c) c = g.get<Sequencer>();
    });
  if (!c) return Log::warn("seq jump: no Sequencer in the scene");
  const SequencePtr s = sequence_asset(c->sequence);
  float b = blend;
  if (!(b >= 0.0f) && s)  // (no blend given: the trigger's for that clip, else a quarter second)
    for (const SeqTrigger &g : s->triggers)
      if (g.instance == name) b = g.blend;
  if (!c->jump(name, b >= 0.0f ? b : 0.25f)) Log::warn("seq jump: no clip named '%s' in the sequence", name.c_str());
}

}  // namespace bl

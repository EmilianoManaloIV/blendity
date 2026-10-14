// SPDX-License-Identifier: GPL-2.0-or-later
// The Animation window's Curves view (task 0061): Unity's curve editor. Each shown track as a curve of its value over
// time; keys dragged in time and value; tangent handles shaping the curve (Free Smooth, or Broken for a sharp turn).
// A drag edits a copy of the clip so the curve follows the mouse, and lands as one undo step when the button comes up.
#include "editor.h"

#include "../scene/animation.h"

#include <algorithm>
#include <cmath>

namespace bl {

namespace {

/* x red, y green, z blue (Unity's colours); a number of a component that isn't a vector, a colour by its place. */
uint32_t track_colour(const AnimTrack &t, size_t index) {
  static const uint32_t axes[] = {0xFFE25555, 0xFF6CCB5F, 0xFF5A8CFF};
  static const uint32_t others[] = {0xFFE0B040, 0xFFC070E0, 0xFF40C8C8, 0xFFE08040};
  const bool vector = is_transform_property(t.property) || t.axis > 0;
  return vector ? axes[std::max(0, std::min(2, t.axis))] : others[index % 4];
}

/* A round step for grid lines over a span: 1, 2 or 5 times a power of ten, about `lines` of them. */
float nice_step(float span, int lines) {
  const float raw = span / (float)std::max(1, lines);
  const float p = std::pow(10.0f, std::floor(std::log10(std::max(raw, 1e-6f))));
  for (float m : {1.0f, 2.0f, 5.0f})
    if (raw <= m * p) return m * p;
  return 10.0f * p;
}

}  // namespace

/* ---------------------------------------------------------------- mapping */

float Editor::anim_y_of(const Recti &a, float v) const {
  const float span = std::max(1e-6f, anim_.v_max - anim_.v_min);
  return (float)a.bottom() - (v - anim_.v_min) / span * (float)a.h;
}

float Editor::anim_value_at(const Recti &a, int y) const {
  const float span = std::max(1e-6f, anim_.v_max - anim_.v_min);
  return anim_.v_min + (float)(a.bottom() - y) / (float)std::max(1, a.h) * span;
}

/* The tracks the Curves view shows: the ones picked in the list, else all. */
std::vector<size_t> Editor::anim_curve_tracks(const AnimationClip &c) const {
  std::vector<size_t> out;
  for (int t : anim_.curve_tracks)
    if (t >= 0 && (size_t)t < c.tracks.size()) out.push_back((size_t)t);
  if (!out.empty()) return out;
  for (size_t t = 0; t < c.tracks.size(); t++) out.push_back(t);
  return out;
}

/* F: the value axis round everything shown (keys and the curves between them), a tenth more each side. */
void Editor::anim_frame_values(const AnimationClip &c) {
  float lo = 1e30f, hi = -1e30f;
  for (size_t t : anim_curve_tracks(c)) {
    const AnimTrack &tr = c.tracks[t];
    if (tr.keys.empty()) continue;
    const float f0 = tr.keys.front().frame, f1 = tr.keys.back().frame;
    for (int s = 0; s <= 64; s++) {
      const float v = tr.sample(f0 + (f1 - f0) * (float)s / 64.0f);
      lo = std::min(lo, v), hi = std::max(hi, v);
    }
  }
  if (lo > hi) lo = -1.0f, hi = 1.0f;
  const float pad = std::max(0.5f, (hi - lo) * 0.1f);
  anim_.v_min = lo - pad, anim_.v_max = hi + pad;
  anim_.v_framed = c.asset_path;
}

/* Where key i's tangent handle is drawn: along its slope, a fixed length on screen, before (side 0) or after it. */
Vec2 Editor::anim_handle_at(const AnimTrack &t, size_t i, int side, const Recti &a, float end) const {
  const AnimKey &k = t.keys[i];
  const float slope = side == 0 ? t.in_tangent(i) : t.out_tangent(i);
  const float px_frame = (float)a.w / std::max(1e-6f, end), px_value = (float)a.h / std::max(1e-6f, anim_.v_max - anim_.v_min);
  Vec2 d(px_frame, -slope * px_value);  // (screen y grows downwards)
  const float len = std::sqrt(d.x * d.x + d.y * d.y);
  d = len > 1e-6f ? d * ((float)ui_.px(40) / len) : Vec2((float)ui_.px(40), 0.0f);
  const Vec2 key((float)anim_x_of(a, k.frame, end), anim_y_of(a, k.value));
  return side == 0 ? key - d : key + d;
}

/* A key's handle is there when the curve on that side is one (not a line, not held) and there is a side. */
bool Editor::anim_handle_shown(const AnimTrack &t, size_t i, int side) const {
  if (side == 0) return i > 0 && t.keys[i - 1].interp == AnimAuto;
  return i + 1 < t.keys.size() && t.keys[i].interp == AnimAuto;
}

/* ---------------------------------------------------------------- drawing */

void Editor::draw_anim_curves(const AnimationClip &clip, const Recti &a, float end) {
  auto &u = ui_;
  if (anim_.v_framed != clip.asset_path) anim_frame_values(clip);
  const AnimationClip &c = anim_.drag_clip_on ? anim_.drag_clip : clip;  // (a drag under way: the edited copy)
  u.canvas.push_clip(a);
  /* Value grid lines with their numbers; zero brighter. */
  const float step = nice_step(anim_.v_max - anim_.v_min, std::max(2, a.h / u.px(40)));
  const double first = std::ceil(anim_.v_min / step), last = std::floor(anim_.v_max / step);
  for (double n = first; n <= last && n - first < 200.0; n += 1.0) {  // (whole steps: a float sum can stall)
    const float v = (float)(n * step);
    const int y = (int)anim_y_of(a, v);
    u.canvas.hline(a.x, a.right(), y, std::fabs(v) < step * 0.01f ? Color::hex(0x777777) : Color::hex(0x3A3A3A));
    u.label({a.x + u.px(3), y - u.row_h(), u.px(60), u.row_h()}, strprintf("%g", std::fabs(v) < step * 0.01f ? 0.0f : v), u.theme.text_dim);
  }
  for (size_t t : anim_curve_tracks(c)) draw_anim_curve(c, t, a, end);
  u.canvas.pop_clip();
}

void Editor::draw_anim_curve(const AnimationClip &c, size_t t, const Recti &a, float end) {
  auto &u = ui_;
  const AnimTrack &tr = c.tracks[t];
  if (tr.keys.empty()) return;
  const uint32_t colour = track_colour(tr, t);
  Vec2 prev((float)a.x, anim_y_of(a, tr.sample(anim_frame_at(a, a.x, end))));
  for (int x = a.x + 2; x <= a.right(); x += 2) {
    const Vec2 p((float)x, anim_y_of(a, tr.sample(anim_frame_at(a, x, end))));
    u.canvas.line(prev.x, prev.y, p.x, p.y, colour, 1.5f);
    prev = p;
  }
  for (size_t i = 0; i < tr.keys.size(); i++) {
    const AnimKey &k = tr.keys[i];
    const float kx = (float)anim_x_of(a, k.frame, end), ky = std::max(-1e5f, std::min(1e5f, anim_y_of(a, k.value)));
    const bool sel = anim_selected((int)t, k.frame);
    const float s = (float)u.px(sel ? 4 : 3);
    u.canvas.fill_rect({(int)(kx - s), (int)(ky - s), (int)(2 * s), (int)(2 * s)}, sel ? u.theme.key_selected : u.theme.key_group);
    if (!sel) continue;
    /* The selected key's handles: before it (unless it's the first) and after it (unless the last, or held). */
    for (int side : {0, 1}) {
      if (!anim_handle_shown(tr, i, side)) continue;
      const Vec2 h = anim_handle_at(tr, i, side, a, end);
      u.canvas.line(kx, ky, h.x, h.y, Color::hex(0xAAAAAA), 1.0f);
      u.canvas.fill_circle(h.x, h.y, (float)u.px(3), Color::hex(0xFFFFFF));
    }
  }
}

/* ---------------------------------------------------------------- editing */

/* The selected keys moved by (frames, value) in a copy of the clip (the drag's preview). */
void Editor::anim_preview_move(const AnimationClip &clip, float dframes, float dvalue) {
  anim_.drag_clip = clip;
  anim_.drag_clip_on = true;
  anim_.drag_df = dframes, anim_.drag_dv = dvalue;
  /* Every moved key out first, then all put back: one landing where another of them was can't be moved twice. */
  std::vector<std::pair<size_t, AnimKey>> taken;
  for (const auto &[track, frame] : anim_.selected) {
    if (track < 0 || (size_t)track >= anim_.drag_clip.tracks.size()) continue;
    AnimTrack &t = anim_.drag_clip.tracks[(size_t)track];
    const int k = t.key_at(frame);
    if (k < 0) continue;
    taken.push_back({(size_t)track, t.keys[(size_t)k]});
    t.keys.erase(t.keys.begin() + k);
  }
  for (auto &[track, key] : taken) {
    key.frame = std::max(0.0f, key.frame + dframes), key.value += dvalue;
    anim_.drag_clip.tracks[track].put_key(key);
  }
  anim_show_drag();
}

/* In Preview the scene follows the drag (Unity's way), from the edited copy. */
void Editor::anim_show_drag() {
  GameObject *root = anim_root();
  if (!anim_.preview || !root) return;
  apply_clip(anim_.drag_clip, *root, anim_.frame);
  solve_constraints(*scene_);
  scene_dirty_ = true;
}

/* A handle dragged to (mx, my): the key's slope through the mouse; Free Smooth unless the key is Broken (then only
 * that side). */
void Editor::anim_preview_handle(const AnimationClip &clip, const Recti &a, float end, int mx, int my) {
  anim_.drag_clip = clip;
  anim_.drag_clip_on = true;
  if (anim_.drag_track < 0 || (size_t)anim_.drag_track >= anim_.drag_clip.tracks.size()) return;
  AnimTrack &t = anim_.drag_clip.tracks[(size_t)anim_.drag_track];
  const int i = t.key_at(anim_.drag_key);
  if (i < 0) return;
  AnimKey &k = t.keys[(size_t)i];
  float df = anim_frame_at(a, mx, end) - k.frame;
  if (anim_.drag_side == 0) df = std::min(df, -1e-3f);  // (the handle stays on its side)
  else df = std::max(df, 1e-3f);
  const float slope = std::max(-1e6f, std::min(1e6f, (anim_value_at(a, my) - k.value) / df));
  if (k.tangent != TanBroken) k.tangent = TanFree, k.in_slope = k.out_slope = slope;
  else (anim_.drag_side == 0 ? k.in_slope : k.out_slope) = slope;
  anim_show_drag();
}

/* A tangent mode for the selected keys; from Clamped Auto they keep the slopes they had, so nothing jumps. */
void Editor::anim_set_tangent(int mode) {
  const auto sel = anim_.selected;
  anim_edit_keys("Key Tangents", [&](AnimationClip &c) {
    for (const auto &[track, frame] : sel) {
      if (track < 0 || (size_t)track >= c.tracks.size()) continue;
      AnimTrack &t = c.tracks[(size_t)track];
      const int k = t.key_at(frame);
      if (k < 0) continue;
      const float in = t.in_tangent((size_t)k), out = t.out_tangent((size_t)k);
      AnimKey &key = t.keys[(size_t)k];
      key.tangent = mode;
      key.in_slope = in, key.out_slope = mode == TanFree ? in : out;
      if (mode == TanFree || mode == TanBroken) key.interp = AnimAuto;  // (a curve leaves it, or there's nothing to shape)
    }
  });
}

/* ---------------------------------------------------------------- the mouse */

/* A selected key's handle under (mx, my): its track, key frame and side. */
bool Editor::anim_handle_under(const AnimationClip &c, const Recti &a, float end, int mx, int my) {
  const std::vector<size_t> shown = anim_curve_tracks(c);
  for (const auto &[track, frame] : anim_.selected) {
    if (track < 0 || std::find(shown.begin(), shown.end(), (size_t)track) == shown.end()) continue;
    const AnimTrack &t = c.tracks[(size_t)track];
    const int i = t.key_at(frame);
    if (i < 0) continue;
    for (int side : {0, 1}) {
      if (!anim_handle_shown(t, (size_t)i, side)) continue;
      const Vec2 h = anim_handle_at(t, (size_t)i, side, a, end);
      if (!(std::hypot(h.x - (float)mx, h.y - (float)my) <= (float)ui_.px(6))) continue;  // (NaN: no hit)
      anim_.drag_track = track, anim_.drag_key = frame, anim_.drag_side = side;
      return true;
    }
  }
  return false;
}

/* A key of a shown track under (mx, my). */
bool Editor::anim_curve_key_under(const AnimationClip &c, const Recti &a, float end, int mx, int my, int &track, float &frame) {
  for (size_t t : anim_curve_tracks(c))
    for (const AnimKey &k : c.tracks[t].keys) {
      if (std::abs(anim_x_of(a, k.frame, end) - mx) > ui_.px(5) || std::fabs(anim_y_of(a, k.value) - (float)my) > (float)ui_.px(5)) continue;
      track = (int)t, frame = k.frame;
      return true;
    }
  return false;
}

/* The shown curve passing nearest (mx, my), within a few pixels: -1 when none. */
int Editor::anim_curve_near(const AnimationClip &c, const Recti &a, float end, int mx, int my) {
  int best = -1;
  float best_d = (float)ui_.px(6);
  const float f = anim_frame_at(a, mx, end);
  for (size_t t : anim_curve_tracks(c)) {
    if (c.tracks[t].keys.empty()) continue;
    const float d = std::fabs(anim_y_of(a, c.tracks[t].sample(f)) - (float)my);
    if (d <= best_d) best_d = d, best = (int)t;
  }
  return best;
}

void Editor::anim_curve_input(const AnimationClip &clip, const Recti &ruler, const Recti &a, float end) {
  auto &u = ui_;
  auto &in = u.in;
  const int mx = in.mx, my = in.my;
  if (anim_.drag == 1 && in.down[0]) anim_set_frame(anim_frame_at(a, mx, end));
  if (anim_.drag && !in.down[0]) return anim_curve_end_drag(clip, a, end);
  anim_curve_drag(clip, a, end);
  if (u.hovered(ruler) && in.pressed[0]) {
    anim_.drag = 1;
    return anim_set_frame(anim_frame_at(a, mx, end));
  }
  if (!u.hovered(a)) return;
  if (in.wheel_y != 0.0f) {  // the value axis zooms round the mouse, between a hair and ten million
    const float v = anim_value_at(a, my), z = std::pow(1.15f, -in.wheel_y);
    const float span = (anim_.v_max - anim_.v_min) * z, lo = std::max(1e-4f, std::fabs(v) * 1e-5f);
    if (span >= lo && span <= 1e7f) anim_.v_min = v + (anim_.v_min - v) * z, anim_.v_max = v + (anim_.v_max - v) * z;
    in.wheel_y = 0.0f;
  }
  if (in.key_pressed[platform::KEY_F] && !u.wants_keyboard()) anim_frame_values(clip), in.key_pressed[platform::KEY_F] = false;
  int track = -1;
  float frame = 0.0f;
  const bool on_key = anim_curve_key_under(clip, a, end, mx, my, track, frame);
  if (in.double_clicked[0] && !on_key) return anim_curve_add_key(clip, a, end, mx, my);
  if (in.pressed[1] && on_key) {
    if (!anim_selected(track, frame)) anim_.selected = {{track, frame}};
    return u.open_popup(u.id("anim_key_ctx"), {mx, my, 0, 0});
  }
  if (!in.pressed[0]) return;
  anim_.drag_x = mx, anim_.drag_y = my;
  anim_.drag_clip_path = clip.asset_path;
  anim_.drag_df = anim_.drag_dv = 0.0f;
  if (anim_handle_under(clip, a, end, mx, my)) return (void)(anim_.drag = 5);
  if (!on_key) {
    if (!in.shift()) anim_.selected.clear();
    anim_.drag = 3;
    return;
  }
  if (in.shift() && anim_selected(track, frame)) {
    anim_.selected.erase(std::find_if(anim_.selected.begin(), anim_.selected.end(), [&](const auto &s) { return s.first == track && std::fabs(s.second - frame) < 0.01f; }));
    return;
  }
  if (!anim_selected(track, frame)) {
    if (!in.shift()) anim_.selected.clear();
    anim_.selected.push_back({track, frame});
  }
  anim_.drag = 4;
}

/* While a key or a handle is dragged: the preview follows the mouse (Shift: keys move in one direction only). */
void Editor::anim_curve_drag(const AnimationClip &clip, const Recti &a, float end) {
  const auto &in = ui_.in;
  if (anim_.drag == 5) return anim_preview_handle(clip, a, end, in.mx, in.my);
  if (anim_.drag != 4) return;
  float df = std::round(anim_frame_at(a, in.mx, end) - anim_frame_at(a, anim_.drag_x, end));
  float dv = anim_value_at(a, in.my) - anim_value_at(a, anim_.drag_y);
  if (in.shift()) (std::abs(in.mx - anim_.drag_x) >= std::abs(in.my - anim_.drag_y) ? dv : df) = 0.0f;
  anim_preview_move(clip, df, dv);
}

void Editor::anim_curve_end_drag(const AnimationClip &clip, const Recti &a, float end) {
  const int kind = anim_.drag;
  const bool mine = anim_.drag_clip_on && anim_.drag_clip_path == clip.asset_path;  // (another clip: nothing to commit)
  const std::vector<AnimTrack> tracks = mine ? anim_.drag_clip.tracks : std::vector<AnimTrack>();
  const float df = anim_.drag_df, dv = anim_.drag_dv;
  anim_drag_cancel();
  if (kind == 3) return anim_curve_box_select(clip, a, end);
  if ((kind != 4 && kind != 5) || !mine) return;
  if (kind == 4 && df == 0.0f && dv == 0.0f) return;  // (a click: nothing moved)
  const uint64_t before = clip.version;
  anim_edit_keys(kind == 4 ? "Move Keys" : "Key Tangents", [&](AnimationClip &c) { c.tracks = tracks; });
  if (clip.version == before) return;  // (refused: Play)
  for (auto &s : anim_.selected) s.second = std::max(0.0f, s.second + df);
}

void Editor::anim_drag_cancel() {
  anim_.drag = 0;
  anim_.drag_clip_on = false;
  anim_.drag_track = -1;
}

void Editor::anim_clip_changed() {
  anim_drag_cancel();
  anim_.curve_tracks.clear();
  anim_.selected.clear();
  anim_.shown_clip = nullptr;
  anim_.v_framed.clear();
}

void Editor::anim_curve_box_select(const AnimationClip &clip, const Recti &a, float end) {
  const auto &in = ui_.in;
  const int x0 = std::min(in.mx, anim_.drag_x), x1 = std::max(in.mx, anim_.drag_x), y0 = std::min(in.my, anim_.drag_y), y1 = std::max(in.my, anim_.drag_y);
  for (size_t t : anim_curve_tracks(clip))
    for (const AnimKey &k : clip.tracks[t].keys) {
      const int kx = anim_x_of(a, k.frame, end), ky = (int)anim_y_of(a, k.value);
      if (kx < x0 || kx > x1 || ky < y0 || ky > y1 || anim_selected((int)t, k.frame)) continue;
      anim_.selected.push_back({(int)t, k.frame});
    }
}

/* Double-click on a curve: a key on it there, with the curve's value (its shape stays). */
void Editor::anim_curve_add_key(const AnimationClip &clip, const Recti &a, float end, int mx, int my) {
  const int t = anim_curve_near(clip, a, end, mx, my);
  if (t < 0) return;
  const AnimTrack &tr = clip.tracks[(size_t)t];
  const float f = std::max(0.0f, std::round(anim_frame_at(a, mx, end)));
  const float v = tr.sample(f), slope = (tr.sample(f + 0.01f) - tr.sample(f - 0.01f)) / 0.02f;
  anim_edit_keys("Add Key", [&](AnimationClip &c) {
    AnimKey k;
    k.frame = f, k.value = v, k.tangent = TanFree, k.in_slope = k.out_slope = std::isfinite(slope) ? slope : 0.0f;  // (as Unity: the shape stays)
    c.tracks[(size_t)t].put_key(k);
  });
  anim_.selected = {{t, f}};
}

}  // namespace bl

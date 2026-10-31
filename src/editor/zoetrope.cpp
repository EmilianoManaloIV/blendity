// SPDX-License-Identifier: GPL-2.0-or-later
// The Zoetrope (task 0283, ADR 0087): a strip in the Animation window showing the frames around the playhead side by
// side - seven of them, every 1, 2 or 4 frames (Ctrl+wheel over the strip) - each the rig as a figure seen from the
// Scene view's camera, with its hands, feet, head and hips as handles. Drag a handle in any cell and that frame is posed
// (Puppet Posing, with Reach) and keyed, the playhead staying where it is. The figures come from the trails' cache:
// sampled a few frames at a time, never all at once.
#include "editor.h"

namespace bl {

namespace {
constexpr int kCells = 7;
}

int Editor::zoe_frame(int cell) const { return (int)std::lround(anim_.frame) + (cell - kCells / 2) * zoe_.step; }

/* A cached frame's bones in the strip's 2D: the Scene camera's projection, scaled so the playhead's figure fills a
 * cell (the same scale for every cell, so the motion reads). False when the frame isn't sampled yet. */
bool Editor::zoe_project(int f, const Recti &cell, std::vector<std::pair<uint64_t, std::pair<Vec2, Vec2>>> &out) {
  out.clear();
  const auto it = trails_.frames.find(f);
  if (it == trails_.frames.end()) return false;
  const Mat4 &vp = scene_r3d_.view_proj();
  auto ndc = [&](Vec3 p) {
    const Vec4 c = vp * Vec4{p.x, p.y, p.z, 1.0f};
    return std::fabs(c.w) > 1e-6f ? Vec2(c.x / c.w, -c.y / c.w) : Vec2(0, 0);
  };
  for (const auto &b : it->second) out.push_back({b.first, {ndc(b.second.first), ndc(b.second.second)}});
  const float s = std::min(cell.w, cell.h) * 0.8f / std::max(1e-4f, zoe_.span);
  for (auto &b : out) {
    b.second.first = Vec2(cell.x + cell.w * 0.5f + (b.second.first.x - zoe_.center.x) * s, cell.y + cell.h * 0.5f + (b.second.first.y - zoe_.center.y) * s);
    b.second.second = Vec2(cell.x + cell.w * 0.5f + (b.second.second.x - zoe_.center.x) * s, cell.y + cell.h * 0.5f + (b.second.second.y - zoe_.center.y) * s);
  }
  return true;
}

/* The figures' framing from the playhead's pose: its middle and its larger extent, in the camera's view. */
void Editor::zoe_frame_fit() {
  const auto it = trails_.frames.find((int)std::lround(anim_.frame));
  if (it == trails_.frames.end() || it->second.empty()) return;
  const Mat4 &vp = scene_r3d_.view_proj();
  Vec2 lo(1e9f, 1e9f), hi(-1e9f, -1e9f);
  for (const auto &b : it->second)
    for (Vec3 p : {b.second.first, b.second.second}) {
      const Vec4 c = vp * Vec4{p.x, p.y, p.z, 1.0f};
      if (std::fabs(c.w) < 1e-6f) continue;
      const Vec2 q(c.x / c.w, -c.y / c.w);
      lo = Vec2(std::min(lo.x, q.x), std::min(lo.y, q.y)), hi = Vec2(std::max(hi.x, q.x), std::max(hi.y, q.y));
    }
  zoe_.center = Vec2((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f);
  zoe_.span = std::max(hi.x - lo.x, hi.y - lo.y);
}

void Editor::draw_zoetrope(const Recti &strip) {
  auto &u = ui_;
  u.canvas.fill_rect(strip, u.theme.panel);
  zoe_frame_fit();
  const int gap = u.px(4), w = (strip.w - gap * (kCells + 1)) / kCells;
  const auto &ends = rig_ends();
  for (int i = 0; i < kCells; i++) {
    const Recti cell{strip.x + gap + i * (w + gap), strip.y + gap, w, strip.h - gap * 2};
    const int f = zoe_frame(i);
    u.canvas.fill_rect(cell, i == kCells / 2 ? u.theme.header : u.theme.strip);
    u.label({cell.x + u.px(4), cell.y, cell.w, u.row_h()}, std::to_string(f), i == kCells / 2 ? u.theme.text_bright : u.theme.text_dim);
    if (u.record_buttons) u.button_rects["zoe:" + std::to_string(i)] = cell;
    std::vector<std::pair<uint64_t, std::pair<Vec2, Vec2>>> bones;
    if (!zoe_project(f, cell, bones)) {
      u.label({cell.x + u.px(4), cell.y + cell.h / 2, cell.w, u.row_h()}, "...", u.theme.text_dim);
      continue;
    }
    for (const auto &b : bones) u.canvas.line(b.second.first.x, b.second.first.y, b.second.second.x, b.second.second.y, u.theme.text);
    for (const auto &b : bones) {
      if (std::find(ends.begin(), ends.end(), b.first) == ends.end()) continue;
      const bool hot = zoe_.drag_bone == b.first && zoe_.drag_cell == i;
      u.canvas.fill_circle(b.second.first.x, b.second.first.y, (float)u.px(hot ? 5 : 4), hot ? Color::hex(0xFFE060) : Color::hex(0xFFFFFF));
      GameObject *g = scene_->find(b.first);
      if (u.record_buttons && g)
        u.button_rects["zoe:" + std::to_string(i) + " " + g->name] = Recti{(int)b.second.first.x - 5, (int)b.second.first.y - 5, 10, 10};
    }
  }
  zoetrope_input(strip, w, gap);
}

/* A press on a cell's handle starts posing that frame; Ctrl+wheel over the strip changes the step. */
void Editor::zoetrope_input(const Recti &strip, int w, int gap) {
  auto &u = ui_;
  auto &in = u.in;
  if (zoe_.drag_bone) return zoe_drag();
  if (!u.hovered(strip)) return;
  if (in.ctrl() && in.wheel_y != 0.0f) {
    static const int kSteps[] = {1, 2, 4};
    int k = zoe_.step == 1 ? 0 : zoe_.step == 2 ? 1 : 2;
    k = std::max(0, std::min(2, k + (in.wheel_y > 0 ? 1 : -1)));
    zoe_.step = kSteps[k];
    in.wheel_y = 0.0f;
    return;
  }
  if (!in.pressed[0]) return;
  const auto &ends = rig_ends();
  for (int i = 0; i < kCells; i++) {
    const Recti cell{strip.x + gap + i * (w + gap), strip.y + gap, w, strip.h - gap * 2};
    if (!cell.contains(in.mx, in.my)) continue;
    std::vector<std::pair<uint64_t, std::pair<Vec2, Vec2>>> bones;
    if (!zoe_project(zoe_frame(i), cell, bones)) return;
    for (const auto &b : bones) {
      if (std::find(ends.begin(), ends.end(), b.first) == ends.end()) continue;
      if (length(Vec2(b.second.first.x - in.mx, b.second.first.y - in.my)) > (float)u.px(7)) continue;
      if (!autopose_.on) {  // (posing a frame is AutoPosing's)
        refuse("Posing in the Zoetrope needs AutoPosing", "autopose on", "Turn AutoPosing on");
        u.consume_click();
        return;
      }
      zoe_.drag_bone = b.first, zoe_.drag_cell = i, zoe_.frame = zoe_frame(i), zoe_.press = Vec2((float)in.mx, (float)in.my);
      if (GameObject *root = anim_root())  // (the scene as it is - its pose and animated values - put back when the drag ends)
        if (AnimationClipPtr clip = anim_clip()) zoe_.guard = std::make_unique<body::PoseGuard>(*scene_, *root, *clip);
      zoe_.scale = std::min(cell.w, cell.h) * 0.8f / std::max(1e-4f, zoe_.span);
      u.consume_click();
      return;
    }
  }
}

/* The drag: the frame applied (the playhead's own pose put back after), the bone moved in the camera's plane by the
 * pointer's travel at the cell's scale, keyed at the cell's frame on release. */
void Editor::zoe_drag() {
  auto &in = ui_.in;
  GameObject *g = scene_->find(zoe_.drag_bone);
  GameObject *root = anim_root();
  AnimationClipPtr clip = anim_clip();
  if (!g || !root || !clip) return void(zoe_.drag_bone = 0);
  const float playhead = anim_.frame;
  anim_.frame = (float)zoe_.frame;  // (the cell's frame for the drag: its pose, its key)
  if (!autopose_.bone) {
    anim_apply_held(*clip, *root, anim_.frame);  // (round 340: held as the Preview holds)
    solve_constraints(*scene_);
    zoe_.start = g->world_position();
  }
  /* The camera's right and up from the view-projection's inverse: a pixel of the cell is this much of the world. */
  const Mat4 inv = scene_r3d_.view_proj().inverse();
  auto world_of = [&](Vec2 ndc, float z) {
    const Vec4 c = inv * Vec4{ndc.x, ndc.y, z, 1.0f};
    return Vec3(c.x / c.w, c.y / c.w, c.z / c.w);
  };
  const Vec4 sc = scene_r3d_.view_proj() * Vec4{zoe_.start.x, zoe_.start.y, zoe_.start.z, 1.0f};
  const float z = sc.z / sc.w;
  const Vec2 d((in.mx - zoe_.press.x) / zoe_.scale, -(in.my - zoe_.press.y) / zoe_.scale);
  const Vec2 base(sc.x / sc.w, sc.y / sc.w);
  const Vec3 target = world_of(Vec2(base.x + d.x, base.y + d.y), z);
  if (length(Vec2(in.mx - zoe_.press.x, in.my - zoe_.press.y)) > (float)ui_.px(2)) autopose_gizmo(g->id, target);
  if (!in.down[0]) {
    if (autopose_.bone) autopose_end();
    zoe_.drag_bone = 0;
    anim_.frame = playhead;
    zoe_.guard.reset();  // (the scene as the drag found it, previewed or not)
    if (anim_.preview) anim_set_frame(playhead);  // (previewing: the playhead's pose again, with any Reach the edit made)
    return;
  }
  anim_.frame = playhead;
}

/* anim zoetrope on|off [step] (task 0283) */
bool Editor::zoetrope_command(const std::vector<std::string> &t) {
  if (t.size() < 2 || to_lower(t[1]) != "zoetrope") return false;
  zoe_.on = !(t.size() > 2 && to_lower(t[2]) == "off");
  if (t.size() > 3) zoe_.step = std::max(1, std::min(4, std::atoi(t[3].c_str())));
  Log::info("Zoetrope %s", zoe_.on ? strprintf("on: 7 frames, every %d", zoe_.step).c_str() : "off");
  return true;
}

}  // namespace bl

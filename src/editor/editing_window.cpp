// SPDX-License-Identifier: GPL-2.0-or-later
// Window > Editing and the console's `develop` (task 0181, ADR 0058): develop a render or an image with a stack of
// adjustments - Lightroom's Basic panel and Photoshop's basics - non-destructively (the source is never written; the
// stack saves as an .edit file). The document and the adjustments are image/edit_doc.cpp and image/adjustments.cpp.
#include "editor.h"

#include "../image/image.h"

#include <cmath>
#include <cstdlib>
#include <ctime>

namespace bl {

namespace {

const std::vector<platform::FileFilter> &image_filters() {
  static const std::vector<platform::FileFilter> f = {{"Images", {"png", "jpg", "jpeg", "tga", "bmp", "hdr", "exr"}}, {"Edits", {"edit"}}};
  return f;
}

const char *const kCurveChannels[] = {"RGB", "Red", "Green", "Blue"};

/* Pixels of `src` (w x h, 0xAARRGGBB) scaled into `dst` of the frame buffer, nearest, inside `clip` and from the
 * left edge `from_x` on. */
void draw_scaled(Image *fb, const std::vector<uint32_t> &src, int w, int h, const Recti &dst, const Recti &clip, int from_x) {
  if (w <= 0 || h <= 0 || dst.w <= 0 || dst.h <= 0) return;
  const Recti r = dst.intersect(clip);
  for (int y = r.y; y < r.bottom(); y++) {
    const int sy = std::min(h - 1, (int)((y - dst.y) * (int64_t)h / dst.h));
    uint32_t *row = fb->row(y);
    for (int x = std::max(r.x, from_x); x < r.right(); x++) {
      const uint32_t c = src[(size_t)sy * w + std::min(w - 1, (int)((x - dst.x) * (int64_t)w / dst.w))], a = c >> 24;
      if (a == 255) {
        row[x] = c;
        continue;
      }
      const uint32_t chk = ((x - dst.x) / 8 + (y - dst.y) / 8) & 1 ? 0x99u : 0x66u;  // (transparency over a checker)
      auto mix = [&](uint32_t v) { return (v * a + chk * (255 - a) + 127) / 255; };
      row[x] = 0xFF000000u | mix(c >> 16 & 255) << 16 | mix(c >> 8 & 255) << 8 | mix(c & 255);
    }
  }
}

}  // namespace

/* ---- opening ---- */

bool Editor::edit_open_file(const std::string &path) {
  std::string err;
  const bool is_edit = fs::extension(path) == ".edit";
  if (!(is_edit ? edit_doc_.load(path, err) : edit_doc_.open_file(path, err))) {
    Log::warn("Editing: couldn't open %s: %s", path.c_str(), err.c_str());
    return false;
  }
  edit_opened_++;
  edit_mask_select(-1, -1);  // (the masks selected belonged to the last picture)
  edit_texture_asset_.clear();
  edit_zoom_ = 0, edit_pan_ = Vec2(0, 0);
  Log::info("Editing: %s (%d x %d)%s", fs::filename(path).c_str(), edit_doc_.source().w, edit_doc_.source().h,
            is_edit ? strprintf(", %zu adjustments", edit_doc_.stack().size()).c_str() : "");
  return true;
}

bool Editor::edit_open_render() {
  if (!render_has_result_ || render_img_.width <= 0) {
    Log::warn("Editing: nothing rendered yet (Render > Render, F12, first)");
    return false;
  }
  const int w = render_img_.width, h = render_img_.height;
  const size_t n = (size_t)w * h;
  edit::Image img;
  img.resize(w, h);
  const bool linear = render_linear_.size() == n * 3;  // (the finished render's light; else its 8-bit picture)
  for (size_t i = 0; i < n; i++) {
    const uint32_t p = render_img_.pixels[i];
    for (int k = 0; k < 3; k++) img.px[i * 4 + k] = linear ? render_linear_[i * 3 + k] : srgb_to_linear(((p >> (16 - 8 * k)) & 255) / 255.0f);
    img.px[i * 4 + 3] = render_transparent_ ? (p >> 24) / 255.0f : 1.0f;
  }
  edit_doc_.open_pixels(std::move(img), "Render");
  if (linear) edit_doc_.set_view(view_transform_from_setting(scene_->render.view_transform), scene_->render.exposure);
  edit_opened_++;
  edit_mask_select(-1, -1);  // (the masks selected belonged to the last picture)
  edit_texture_asset_.clear();
  edit_zoom_ = 0, edit_pan_ = Vec2(0, 0);
  Log::info("Editing: the render (%d x %d, %s)", w, h, linear ? "its light, through the render's view transform" : "its 8-bit picture");
  return true;
}

void Editor::edit_open_dialog() {
  std::string path;
  if (pick_open_path("Open in Editing", fs::join(project_root_, "Renders"), image_filters(), path) == PathPick::Chosen) edit_open_file(path);
}

std::string Editor::edit_default_export_path() const {
  const std::string &named = !edit_doc_.edit_path().empty() ? edit_doc_.edit_path() : edit_doc_.source_path();
  const std::string base = named.empty() ? "render" : fs::stem(named);
  return fs::join(fs::join(project_root_, "Renders"), base + "_edit.png");
}

bool Editor::edit_export(const std::string &path) {
  std::string err;
  const std::string p = path.empty() ? edit_default_export_path() : path;
  fs::make_dirs(fs::parent(p));
  if (!edit_doc_.export_file(p, err)) {
    Log::warn("Editing: couldn't export: %s", err.c_str());
    return false;
  }
  Log::info("Editing: exported %s", p.c_str());
  return true;
}

void Editor::edit_export_dialog() {
  std::string path;
  const PathPick pick = pick_save_path("Export Edited Image", edit_default_export_path(),
                                       {{"PNG", {"png"}}, {"JPEG", {"jpg"}}, {"OpenEXR", {"exr"}}, {"Radiance HDR", {"hdr"}}}, path);
  if (pick == PathPick::Cancelled) return;
  edit_export(pick == PathPick::Chosen ? path : "");
}

/* Where Save writes without a name: the .edit it came from, beside its image, or a new one per render in Renders/. */
std::string Editor::edit_default_save_path() const {
  if (!edit_doc_.edit_path().empty()) return edit_doc_.edit_path();
  if (!edit_doc_.source_path().empty()) return edit_doc_.source_path() + ".edit";
  const std::time_t now = std::time(nullptr);
  char stamp[32];
  std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::localtime(&now));
  return fs::join(fs::join(project_root_, "Renders"), std::string("render_") + stamp + ".edit");
}

bool Editor::edit_save(const std::string &path) {
  std::string err;
  std::string p = path;
  if (p.empty()) p = edit_default_save_path();
  if (!edit_doc_.save(p, err)) {
    Log::warn("Editing: couldn't save: %s", err.c_str());
    return false;
  }
  Log::info("Editing: saved %s (the source is untouched)", p.c_str());
  return true;
}

/* ---- the console: develop open <file>|render | add <type> | set <Type>.<Field> <v> | remove <i> | move <i> <j> |
 * enable <i> on|off | curve <channel> x,y ... | preset <name> | undo | redo | export [file] | save [file] | split
 * on|off|<0-1> | list ---- */

void Editor::edit_list() {
  if (!edit_doc_.is_open()) {
    Log::info("Editing: nothing open (develop open <file> | develop open render)");
    return;
  }
  Log::info("Editing: %s, %d x %d, %zu adjustments", edit_doc_.label().c_str(), edit_doc_.source().w, edit_doc_.source().h, edit_doc_.stack().size());
  for (size_t i = 0; i < edit_doc_.stack().size(); i++) Log::info("  %zu  %s", i, edit::to_text(edit_doc_.stack()[i]).c_str());
}

void Editor::edit_command(const std::vector<std::string> &t, const std::string &line) {
  const std::string verb = t.size() > 1 ? to_lower(t[1]) : "list";
  const size_t at = t.size() > 1 ? line.find(t[1]) + t[1].size() : line.size();
  std::string rest = at < line.size() ? line.substr(at) : "";
  while (!rest.empty() && rest.front() == ' ') rest.erase(0, 1);
  if (verb == "open") {
    if (to_lower(rest) == "render") edit_open_render();
    else if (to_lower(rest).compare(0, 8, "texture ") == 0) edit_open_texture(rest.substr(8));
    else if (!rest.empty()) edit_open_file(rest);
    else Log::warn("develop open <file> | develop open render | develop open texture <name>");
    return;
  }
  if (verb == "list") {
    edit_list();
    return;
  }
  if (verb == "look" || verb == "apply") {  // (task 0183)
    if (!edit_doc_.is_open()) Log::warn("Editing: nothing open");
    else if (verb == "look") edit_use_as_camera_look();
    else edit_apply_to_texture();
    return;
  }
  if (!edit_doc_.is_open()) {
    Log::warn("Editing: nothing open (develop open <file> | develop open render)");
    return;
  }
  if (verb == "add") {
    if (edit_doc_.add(rest) < 0) Log::warn("develop add: no adjustment '%s' (Editing > Add lists them)", rest.c_str());
    return;
  }
  if (verb == "set" && t.size() > 3) {
    const size_t from = line.find(t[2], at);
    const std::string path = line.substr(from, line.rfind(' ') - from);
    if (!edit_doc_.set_path(path, (float)std::atof(t.back().c_str()))) Log::warn("develop set: no field '%s' (Type.Field, e.g. Exposure.Exposure)", path.c_str());
    return;
  }
  if (verb == "remove" && t.size() > 2) {
    if (!edit_doc_.remove(std::atoi(t[2].c_str()))) Log::warn("develop remove: no adjustment %s", t[2].c_str());
    return;
  }
  if (verb == "move" && t.size() > 3) {
    edit_doc_.move(std::atoi(t[2].c_str()), std::atoi(t[3].c_str()));
    return;
  }
  if (verb == "enable" && t.size() > 3) {
    edit_doc_.set_enabled(std::atoi(t[2].c_str()), to_lower(t[3]) != "off");
    return;
  }
  if (verb == "mask") {  // (task 0182)
    edit_mask_command(t);
    return;
  }
  if (verb == "curve" && t.size() > 2) {
    edit_curve_command(t);
    return;
  }
  if (verb == "preset") {
    if (!edit_doc_.apply_preset(rest)) Log::warn("develop preset: none named '%s'", rest.c_str());
    return;
  }
  if (verb == "undo" || verb == "redo") {
    if (!(verb == "undo" ? edit_doc_.undo() : edit_doc_.redo())) Log::info("Editing: nothing to %s", verb.c_str());
    return;
  }
  if (verb == "export") {
    edit_export(rest);
    return;
  }
  if (verb == "save") {
    edit_save(rest);
    return;
  }
  if (verb == "split") {
    const std::string v = to_lower(rest);
    edit_split_ = v == "off" ? -1.0f : v == "on" || v.empty() ? 0.5f : clampf((float)std::atof(v.c_str()), 0.0f, 1.0f);
    return;
  }
  Log::warn("develop open <file>|render | add <type> | set <Type>.<Field> <v> | remove <i> | move <i> <j> | enable <i> on|off | "
            "curve rgb|red|green|blue x,y ... | preset <name> | undo | redo | export [file] | save [file] | split on|off | list");
}

/* develop curve <channel> x,y x,y ...: the last Tone Curve's points (one added when there is none). */
void Editor::edit_curve_command(const std::vector<std::string> &t) {
  int ch = -1;
  for (int k = 0; k < 4; k++)
    if (to_lower(t[2]) == to_lower(kCurveChannels[k]) || (t[2].size() == 1 && std::tolower((unsigned char)t[2][0]) == "argb"[k])) ch = k;
  if (ch < 0) {
    Log::warn("develop curve rgb|red|green|blue x,y x,y ...");
    return;
  }
  std::vector<Vec2> pts;
  for (size_t i = 3; i < t.size(); i++) {
    float x, y;
    if (std::sscanf(t[i].c_str(), "%f,%f", &x, &y) == 2) pts.push_back(Vec2(x, y));
  }
  int i = edit_doc_.find("Tone Curve");
  if (i < 0) i = edit_doc_.add("Tone Curve");
  edit_doc_.set_curve(i, ch, pts);
}

/* ---- the window ---- */

/* The size of preview the window shows (task 0191): the canvas's longest side, or none (0: the full picture) when the
 * zoom shows more pixels than that. */
int Editor::edit_preview_side() const {
  /* In steps of 512 (rounded up): resizing the window doesn't make a new preview every frame. */
  const int canvas = std::max(edit_canvas_rect_.w, edit_canvas_rect_.h);
  const int side = std::max(512, std::min(4096, (canvas + 511) / 512 * 512));
  const int longest = std::max(edit_doc_.source().w, edit_doc_.source().h);
  if (edit_zoom_ > 0.0f && edit_zoom_ * longest > side) return 0;
  return side;
}

void Editor::edit_update_display() {
  const int side = edit_preview_side();
  const std::string view = strprintf("%d|%d|%g|%d", edit_opened_, (int)edit_doc_.view(), edit_doc_.view_exposure(), side);
  const std::string key = view + strprintf("|%llu", (unsigned long long)edit_doc_.revision());
  if (key == edit_shown_key_) return;
  const edit::Image &res = side > 0 ? edit_doc_.preview(side) : edit_doc_.result();
  edit_shown_side_ = side;
  edit_shown_scale_ = side > 0 && &edit_doc_.preview_source(side) != &edit_doc_.source() ? edit_doc_.preview_scale() : 1.0f;
  edit_shown_ms_ = edit_shown_scale_ < 1.0f ? edit_doc_.last_preview_ms() : edit_doc_.last_eval_ms();
  edit::to_display(res, edit_after_px_, edit_doc_.view(), edit_doc_.view_exposure());
  edit_after_w_ = res.w, edit_after_h_ = res.h;
  if (view != edit_before_key_) {  // (the source's pixels: only when another image opens, the view or the preview's size changes)
    const edit::Image &src = side > 0 ? edit_doc_.preview_source(side) : edit_doc_.source();
    edit::to_display(src, edit_before_px_, edit_doc_.view(), edit_doc_.view_exposure());
    edit_before_w_ = src.w, edit_before_h_ = src.h;
    edit_before_key_ = view;
  }
  edit_shown_key_ = key;
}

void Editor::draw_editing_canvas(const Recti &r) {
  auto &u = ui_;
  edit_canvas_rect_ = r;
  u.canvas.fill_rect(r, Color::hex(0x262626));
  if (!edit_doc_.is_open()) {
    u.label(r, "Open Render, Open Image, or drop an image here", u.theme.text_dim, ui::Align::Center);
    return;
  }
  edit_update_display();
  /* The picture's full size (a preview is shown scaled up to it), from the stack's fields. */
  int iw = 0, ih = 0;
  edit_doc_.step_input_size((int)edit_doc_.stack().size(), iw, ih);
  if (iw <= 0 || ih <= 0) return;
  const float fit = std::min((r.w - u.px(20)) / (float)iw, (r.h - u.px(20)) / (float)ih);
  if (u.hovered(r) && u.in.wheel_y != 0) {
    if (edit_zoom_ <= 0) edit_zoom_ = fit;
    edit_zoom_ = clampf(edit_zoom_ * std::pow(1.2f, u.in.wheel_y), 0.02f, 32.0f);
  }
  if (u.hovered(r) && u.in.down[2]) edit_pan_ += Vec2((float)u.in.dx(), (float)u.in.dy());
  if (u.hovered(r) && u.in.double_clicked[0]) edit_zoom_ = 0, edit_pan_ = Vec2(0, 0);
  const float sc = edit_zoom_ > 0 ? edit_zoom_ : fit;
  const int dw = std::max(1, (int)(iw * sc)), dh = std::max(1, (int)(ih * sc));
  const Recti dst{r.x + (r.w - dw) / 2 + (int)edit_pan_.x, r.y + (r.h - dh) / 2 + (int)edit_pan_.y, dw, dh};
  u.canvas.push_clip(r);
  Image *fb = u.canvas.target();
  draw_scaled(fb, edit_after_px_, edit_after_w_, edit_after_h_, dst, r, r.x);
  if (edit_split_ >= 0.0f) {  // before | after: the source left of the line (scaled into the same frame)
    const int sx = dst.x + (int)(dst.w * edit_split_);
    draw_scaled(fb, edit_before_px_, edit_before_w_, edit_before_h_, dst, Recti{r.x, r.y, std::max(0, sx - r.x), r.h}, r.x);
    u.canvas.vline(sx, dst.y, dst.bottom(), u.theme.accent);
    u.label({sx - u.px(64), dst.y + u.px(4), u.px(60), u.row_h()}, "Before", u.theme.text_bright, ui::Align::Right);
    u.label({sx + u.px(4), dst.y + u.px(4), u.px(60), u.row_h()}, "After", u.theme.text_bright);
    const Recti grab{sx - u.px(6), dst.y, u.px(12), dst.h};
    if (u.hovered(grab) && u.in.pressed[0]) edit_split_drag_ = true;
    if (!u.in.down[0]) edit_split_drag_ = false;
    if (edit_split_drag_) edit_split_ = clampf((u.in.mx - dst.x) / (float)dst.w, 0.0f, 1.0f), u.redraw = true;
  }
  draw_edit_mask_overlay(dst, r);
  edit_mask_input(dst, r);
  u.canvas.rect_outline({dst.x - 1, dst.y - 1, dst.w + 2, dst.h + 2}, Color::hex(0x101010));
  u.label({r.x + u.px(8), r.bottom() - u.row_h() - u.px(4), r.w - u.px(16), u.row_h()},
          strprintf("%s  |  %d x %d%s  |  %.0f%%  |  %.1f ms  |  wheel zoom, middle-drag pan, double-click fit", edit_doc_.label().c_str(), iw, ih,
                    edit_shown_scale_ < 1.0f ? strprintf(" (a %d x %d preview)", edit_after_w_, edit_after_h_).c_str() : "", sc * 100.0f, edit_shown_ms_),
          u.theme.text_dim);
  u.canvas.pop_clip();
}

/* One slider of a step: an undo step when the drag starts, none while it goes on. */
void Editor::draw_edit_field(ui::Layout &lay, int step, const edit::FieldInfo &f, float value) {
  auto &u = ui_;
  Recti row = lay.row();
  const int lw = std::min(u.px(120), row.w / 2);
  u.label({row.x, row.y, lw, row.h}, f.name, u.theme.text_dim);
  const ui::Id id = u.id(strprintf("ed_%d_%s", step, f.name));
  float v = value;
  const Recti fr{row.x + lw, row.y, row.w - lw - u.px(56), row.h};
  bool changed = u.slider(id, fr, v, f.mn, f.mx);
  const ui::Id nid = u.id(strprintf("edn_%d_%s", step, f.name));
  changed = u.float_field(nid, {fr.right() + u.px(4), row.y, u.px(52), row.h}, v, (f.mx - f.mn) / 400.0f, f.mn, f.mx, "%.3g") || changed;
  if (u.hovered(row) && u.in.double_clicked[0] && u.hovered(fr)) v = f.def, changed = true;  // (double-click: the default)
  if (!changed) return;
  /* An undo step when this field's interaction starts (the release frame of a drag, no longer active, still belongs
   * to it: edit_drag_id_ is cleared after the panel once the button is up). */
  edit_doc_.set(step, f.name, v, edit_drag_id_ != id);
  edit_drag_id_ = u.is_active(id) || u.is_active(nid) || u.in.down[0] ? id : 0;
}

/* The Tone Curve's channel picker and its box: the curve drawn, then the mouse (click adds a point, drag moves one,
 * right-click removes one). */
void Editor::draw_edit_curve(ui::Layout &lay, int step) {
  auto &u = ui_;
  Recti crow = lay.row();
  int ch = edit_curve_channel_;
  u.combo(u.id(strprintf("ed_curve_ch_%d", step)), crow, ch, kCurveChannels, 4);
  edit_curve_channel_ = ch;
  Recti box = lay.row(std::min(crow.w, u.px(180)));
  box.w = box.h;
  draw_curve_box(box, edit_doc_.stack()[(size_t)step].curve[ch], ch);
  u.mark_widget(box);
  u.tooltip("Click to add a point, drag to move one, right-click to remove it (on the display axis: 0 black, 1 white).");
  edit_curve_input(box, step, ch);
}

void Editor::draw_curve_box(const Recti &box, const std::vector<Vec2> &pts, int ch) {
  auto &u = ui_;
  u.canvas.fill_rect(box, u.theme.well);
  for (int k = 1; k < 4; k++) {
    u.canvas.hline(box.x, box.right(), box.y + box.h * k / 4, u.theme.border);
    u.canvas.vline(box.x + box.w * k / 4, box.y, box.bottom(), u.theme.border);
  }
  const uint32_t col = ch == 0 ? u.theme.text_bright : ch == 1 ? Color::hex(0xE05050) : ch == 2 ? Color::hex(0x50C050) : Color::hex(0x5080E0);
  Vec2 prev = curve_to_px(box, Vec2(0, edit::curve_eval(pts, 0.0f)));
  for (int i = 1; i <= 64; i++) {
    const float x = i / 64.0f;
    const Vec2 p = curve_to_px(box, Vec2(x, pts.size() < 2 ? x : edit::curve_eval(pts, x)));
    u.canvas.line(prev.x, prev.y, p.x, p.y, col, 1.5f);
    prev = p;
  }
  for (const Vec2 &p : pts) u.canvas.fill_circle(curve_to_px(box, p).x, curve_to_px(box, p).y, (float)u.px(4), col);
}

Vec2 Editor::curve_to_px(const Recti &box, Vec2 p) const { return Vec2(box.x + p.x * box.w, box.bottom() - p.y * box.h); }

/* The point under the mouse in the box, -1 when none. */
int Editor::curve_point_near(const Recti &box, const std::vector<Vec2> &pts) const {
  int near = -1;
  for (size_t i = 0; i < pts.size(); i++)
    if (std::fabs(curve_to_px(box, pts[i]).x - ui_.in.mx) < ui_.px(7) && std::fabs(curve_to_px(box, pts[i]).y - ui_.in.my) < ui_.px(7)) near = (int)i;
  return near;
}

/* One undo step for a click and the drag after it; the drag belongs to this step's channel only. */
void Editor::edit_curve_input(const Recti &box, int step, int ch) {
  auto &u = ui_;
  if (!u.in.down[0]) edit_curve_drag_ = -1;
  if (edit_curve_drag_ >= 0) {
    if (edit_curve_step_ == step && edit_curve_ch_ == ch) edit_curve_drag_to(box, step, ch);
    return;
  }
  if (!u.hovered(box) || u.any_popup_open()) return;
  std::vector<Vec2> pts = edit_doc_.stack()[(size_t)step].curve[ch];
  if (pts.size() < 2) pts = {Vec2(0, 0), Vec2(1, 1)};
  int near = curve_point_near(box, pts);
  if (u.in.pressed[1] && near > 0 && near + 1 < (int)pts.size()) {
    pts.erase(pts.begin() + near);
    edit_doc_.set_curve(step, ch, pts);
    u.redraw = true;
    return;
  }
  if (!u.in.pressed[0]) return;
  edit_doc_.begin_change();
  if (near < 0) {
    const Vec2 added(clampf((u.in.mx - box.x) / (float)box.w, 0, 1), clampf((box.bottom() - u.in.my) / (float)box.h, 0, 1));
    pts.push_back(added);
    edit_doc_.set_curve(step, ch, pts, false);  // (sorted there: find the new point to drag it)
    const std::vector<Vec2> &now = edit_doc_.stack()[(size_t)step].curve[ch];
    for (size_t i = 0; i < now.size(); i++)
      if (now[i].x == added.x && now[i].y == added.y) near = (int)i;
  }
  edit_curve_drag_ = near, edit_curve_step_ = step, edit_curve_ch_ = ch;
  u.redraw = true;
}

void Editor::edit_curve_drag_to(const Recti &box, int step, int ch) {
  auto &u = ui_;
  std::vector<Vec2> next = edit_doc_.stack()[(size_t)step].curve[ch];
  const int k = edit_curve_drag_;
  if (k >= (int)next.size()) return;
  const float lo = k > 0 ? next[(size_t)k - 1].x + 0.01f : 0.0f;
  const float hi = k + 1 < (int)next.size() ? next[(size_t)k + 1].x - 0.01f : 1.0f;
  next[(size_t)k] = Vec2(clampf((u.in.mx - box.x) / (float)box.w, lo, std::max(lo, hi)), clampf((box.bottom() - u.in.my) / (float)box.h, 0, 1));
  std::vector<edit::Adjustment> s = edit_doc_.stack();
  s[(size_t)step].curve[ch] = next;
  edit_doc_.set_stack(s, false);
  u.redraw = true;
}

/* A step's header (on / off, its name, up, down, remove) and its fields. */
void Editor::draw_edit_step(ui::Layout &lay, int step) {
  auto &u = ui_;
  const edit::Adjustment &a = edit_doc_.stack()[(size_t)step];
  const edit::AdjustmentInfo *info = a.info();
  if (!info) return;
  Recti h = lay.row(u.row_h() + u.px(2));
  u.canvas.fill_rect({h.x - u.px(4), h.y, h.w + u.px(8), h.h}, u.theme.header);
  bool on = a.enabled;
  if (u.checkbox({h.x, h.y, h.h, h.h}, on)) edit_doc_.set_enabled(step, on);
  u.tooltip("Turn this adjustment off without removing it.");
  u.label({h.x + h.h + u.px(4), h.y, h.w - h.h * 4, h.h}, info->name, u.theme.text_bright);
  u.mark_widget({h.x + h.h + u.px(4), h.y, h.w - h.h * 4, h.h});
  u.tooltip(info->help);
  const int bw = h.h;
  const bool up = u.button({h.right() - 3 * bw, h.y, bw, h.h}, "^");
  u.tooltip("Move up (applied earlier)");
  const bool down = u.button({h.right() - 2 * bw, h.y, bw, h.h}, "v");
  u.tooltip("Move down (applied later)");
  const bool gone = u.icon_button({h.right() - bw, h.y, bw, h.h}, ui::Icon::Close, false, "Remove");
  if (up || down || gone) {  // (the step's slot now holds another adjustment: its fields wait for the next frame)
    if (gone) edit_doc_.remove(step);
    else edit_doc_.move(step, step + (up ? -1 : 1));
    return;
  }
  draw_edit_step_fields(lay, step);
  draw_edit_mask(lay, step);
}

void Editor::draw_edit_step_fields(ui::Layout &lay, int step) {
  auto &u = ui_;
  const edit::Adjustment &a = edit_doc_.stack()[(size_t)step];
  const edit::AdjustmentInfo *info = a.info();
  if (a.type == "Tone Curve") {
    draw_edit_curve(lay, step);
    return;
  }
  if (a.type == "HSL") {  // eight bands of three: one band's at a time
    u.combo(u.id(strprintf("ed_hsl_%d", step)), lay.row(), edit_hsl_band_, edit::hsl_band_names(), 8);
    for (int k = 0; k < 3; k++) draw_edit_field(lay, step, info->fields[(size_t)edit_hsl_band_ * 3 + k], a.v[(size_t)edit_hsl_band_ * 3 + k]);
    return;
  }
  for (size_t f = 0; f < info->fields.size() && f < a.v.size(); f++) draw_edit_field(lay, step, info->fields[f], a.v[f]);
}

void Editor::draw_edit_panel(const Recti &left) {
  auto &u = ui_;
  u.canvas.fill_rect(left, u.theme.pane);
  u.canvas.vline(left.x, left.y, left.bottom(), u.theme.border);
  int off = u.begin_scroll(u.id("edit_panel"), left, edit_panel_h_);
  ui::Layout lay{{left.x + u.px(8), left.y, left.w - u.px(20), left.h}, left.y + u.px(8) - off};
  lay.row_h = u.row_h();
  const int top = lay.y;
  Recti b = lay.row(u.row_h() + u.px(6));
  const int bw = (b.w - u.px(12)) / 4;
  if (u.button({b.x, b.y, bw, b.h}, "Open...", false, ui::Icon::Folder)) defer([this] { edit_open_dialog(); });
  u.tooltip("Open an image (PNG, JPEG, TGA, BMP, HDR, EXR) or a saved .edit. Dropping an image here opens it too.");
  if (u.button({b.x + bw + u.px(4), b.y, bw, b.h}, "Render", false, ui::Icon::Camera)) edit_open_render();
  u.tooltip("Open the Render window's last render: its light, shown through the render's view transform.");
  const bool open = edit_doc_.is_open();
  if (u.button_enabled({b.x + 2 * (bw + u.px(4)), b.y, bw, b.h}, "Export...", open, "Nothing open: Open an image or Render first.", false, ui::Icon::File) && open) defer([this] { edit_export_dialog(); });
  u.tooltip("Write the result as PNG / JPEG (through the view) or EXR / HDR (linear light).");
  if (u.button_enabled({b.x + 3 * (bw + u.px(4)), b.y, bw, b.h}, "Save", open, "Nothing open: Open an image or Render first.", false, ui::Icon::Check) && open) edit_save("");
  u.tooltip("Save the adjustments as an .edit file beside the image (the image itself is never changed).");
  if (open) {
    Recti row = lay.row();
    const int hw = (row.w - u.px(8)) / 3;
    static std::vector<const char *> preset_names;
    if (preset_names.empty()) {
      preset_names.push_back("Preset...");
      for (const edit::Preset &p : edit::builtin_presets()) preset_names.push_back(p.name);
    }
    int pick = 0;
    if (u.combo(u.id("edit_preset"), {row.x, row.y, hw, row.h}, pick, preset_names.data(), (int)preset_names.size()) && pick > 0)
      edit_doc_.apply_preset(preset_names[(size_t)pick]);
    u.tooltip("Replace the adjustments with a preset's (undoable).");
    if (u.button({row.x + hw + u.px(4), row.y, hw, row.h}, "Before / After", edit_split_ >= 0.0f)) edit_split_ = edit_split_ >= 0.0f ? -1.0f : 0.5f;
    u.tooltip("Split the picture: the source left of the line, the result right (drag the line).");
    const bool can_undo = edit_doc_.can_undo();
    if (u.button_enabled({row.x + 2 * (hw + u.px(4)), row.y, hw / 2 - u.px(2), row.h}, "Undo", can_undo, "Nothing to undo in the Editing window.") && can_undo) edit_doc_.undo();
    if (u.button_enabled({row.x + 2 * (hw + u.px(4)) + hw / 2, row.y, hw / 2, row.h}, "Redo", edit_doc_.can_redo(), "Nothing to redo in the Editing window.")) edit_doc_.redo();
    draw_edit_targets(lay);
    static std::vector<const char *> add_names;
    if (add_names.empty()) {
      add_names.push_back("Add Adjustment...");
      for (const edit::AdjustmentInfo &i : edit::adjustment_infos()) add_names.push_back(i.name);
    }
    int add = 0;
    if (u.combo(u.id("edit_add"), lay.row(u.row_h() + u.px(2)), add, add_names.data(), (int)add_names.size()) && add > 0)
      edit_doc_.add(add_names[(size_t)add]);
    u.tooltip("Adjustments apply top to bottom, in linear light. Each one can be turned off, moved or removed.");
    lay.space(u.px(4));
    for (int i = 0; i < (int)edit_doc_.stack().size(); i++) {
      draw_edit_step(lay, i);
      lay.space(u.px(4));
    }
    if (edit_doc_.stack().empty()) u.label(lay.row(), "No adjustments yet: Add one, or pick a preset.", u.theme.text_dim);
  }
  edit_panel_h_ = lay.y - top + u.px(16);
  u.end_scroll();
}

/* Where the result goes besides a file: the texture it came from, the camera. */
void Editor::draw_edit_targets(ui::Layout &lay) {
  auto &u = ui_;
  Recti row = lay.row();
  const bool texture = !edit_texture_asset_.empty();
  const int bw = texture ? (row.w - u.px(4)) / 2 : row.w;
  if (texture && u.button({row.x, row.y, bw, row.h}, "Apply to Texture")) edit_apply_to_texture();
  if (texture) u.tooltip("Put the result on the texture as an Image layer on top (its procedural layers stay underneath).");
  if (u.button_enabled({row.right() - bw, row.y, bw, row.h}, "Use as Camera Look", edit_doc_.is_open(), "Nothing open: Open an image or Render first.")) edit_use_as_camera_look();
  u.tooltip("Give the Main Camera these adjustments as camera filters, so the Game view and F12 carry the look.");
}

void Editor::draw_editing_window(const Recti &r) {
  auto &u = ui_;
  edit_window_rect_ = r;
  const int pw = std::min(u.px(330), r.w * 2 / 5);
  /* The panel first, so the picture drawn after it shows what the panel just changed. */
  const uint64_t before = edit_doc_.revision();
  draw_edit_panel({r.right() - pw, r.y, pw, r.h});
  if (edit_doc_.revision() != before) u.redraw = true;
  draw_editing_canvas({r.x, r.y, r.w - pw, r.h});
  if (!u.any_popup_open() && edit_drag_id_ != 0 && !u.in.down[0]) edit_drag_id_ = 0;
}

/* The Editing window is the active tab of its dock leaf and the mouse is over it (a drop or Ctrl+Z goes to it). */
bool Editor::edit_window_has_mouse() {
  DockNode *n = dock_find(WindowKind::Editing);
  if (!n || n->active < 0 || n->active >= (int)n->tabs.size() || n->tabs[(size_t)n->active] != WindowKind::Editing) return false;
  return edit_window_rect_.contains(ui_.in.mx, ui_.in.my);
}

}  // namespace bl

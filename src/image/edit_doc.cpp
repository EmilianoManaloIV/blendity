// SPDX-License-Identifier: GPL-2.0-or-later
// The Editing window's document (task 0181, ADR 0058): see edit_doc.h.
#include "edit_doc.h"

#include "../core/core.h"
#include "../core/jobs.h"
#include "../render/canvas.h"
#include "../render/display.h"
#include "image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace bl::edit {

namespace {

const size_t kHistory = 200;

}  // namespace

Image image_from_bitmap(const Bitmap &b) {
  Image img;
  img.resize(b.width, b.height);
  const size_t n = (size_t)b.width * b.height;
  if (b.is_float) {
    std::copy(b.rgbaf.begin(), b.rgbaf.begin() + (ptrdiff_t)std::min(b.rgbaf.size(), n * 4), img.px.begin());
    return img;
  }
  float lut[256];
  for (int i = 0; i < 256; i++) lut[i] = srgb_to_linear(i / 255.0f);
  for (size_t i = 0; i < n * 4 && i < b.rgba8.size(); i++) img.px[i] = (i % 4 == 3) ? b.rgba8[i] / 255.0f : lut[b.rgba8[i]];
  return img;
}

namespace {

std::vector<float> rgb_of(const Image &img) {
  std::vector<float> rgb((size_t)img.w * img.h * 3);
  for (size_t i = 0; i < (size_t)img.w * img.h; i++)
    for (int k = 0; k < 3; k++) rgb[i * 3 + k] = img.px[i * 4 + k];
  return rgb;
}

}  // namespace

const std::vector<Preset> &builtin_presets() {
  static const std::vector<Preset> p = {
      {"Neutral", ""},
      {"Film",
       "Contrast | Contrast=12\nHighlights | Highlights=-25\nShadows | Shadows=20\nVibrance / Saturation | Vibrance=-10 | "
       "Saturation=-8\nTone Curve | curve0=0,0.04 0.25,0.23 0.75,0.79 1,0.96 | curve1=0,0 1,1 | curve2=0,0 1,1 | curve3=0,0 1,1\n"
       "Vignette | Amount=-15 | Midpoint=50 | Roundness=0 | Feather=60"},
      {"Anime Pop", "Shadows | Shadows=25\nContrast | Contrast=10\nVibrance / Saturation | Vibrance=35 | Saturation=10\nClarity | Clarity=-10"},
      {"Manga Contrast",
       "Vibrance / Saturation | Vibrance=0 | Saturation=-100\nContrast | Contrast=50\nWhites | Whites=30\nBlacks | Blacks=-40\n"
       "Sharpen | Amount=60 | Radius=1 | Masking=20"},
      {"Cinematic Teal-Orange",
       "Contrast | Contrast=15\nTemperature / Tint | Temperature=8 | Tint=0\nHSL | Orange Saturation=20 | Aqua Hue=-20 | "
       "Aqua Saturation=15 | Blue Hue=-15 | Green Saturation=-30\nVignette | Amount=-10 | Midpoint=50 | Roundness=0 | Feather=60"},
  };
  return p;
}

bool Doc::open_file(const std::string &path, std::string &error) {
  Bitmap b;
  if (!load_image(path, b, error)) return false;
  if (b.width <= 0 || b.height <= 0) {
    error = "empty image";
    return false;
  }
  open_pixels(image_from_bitmap(b), path);
  source_path_ = path;
  return true;
}

/* One row of `out` as the average of the source pixels it covers (an area filter: no aliasing when shrinking). */
void downsample_row(const Image &in, Image &out, int y) {
  const float sx = (float)in.w / out.w, sy = (float)in.h / out.h;
  const int r0 = (int)(y * sy), r1 = std::max(r0 + 1, std::min(in.h, (int)((y + 1) * sy)));
  for (int x = 0; x < out.w; x++) {
    const int c0 = (int)(x * sx), c1 = std::max(c0 + 1, std::min(in.w, (int)((x + 1) * sx)));
    float acc[4] = {0, 0, 0, 0};
    for (int r = r0; r < r1; r++)
      for (int c = c0; c < c1; c++)
        for (int k = 0; k < 4; k++) acc[k] += in.at(c, r)[k];
    const float n = 1.0f / ((r1 - r0) * (c1 - c0));
    for (int k = 0; k < 4; k++) out.at(x, y)[k] = acc[k] * n;
  }
}

void Doc::open_pixels(Image img, const std::string &label) {
  source_ = std::move(img);
  source_path_.clear();
  edit_path_.clear();
  label_ = label;
  view_ = ViewTransform::Standard, view_exposure_ = 0.0f;  // (an image is shown as it is; a render sets its own after)
  revision_++;
  stack_.clear();
  undo_.clear();
  redo_.clear();
  full_.clear(), preview_.clear();
  full_.spare.clear(), preview_.spare.clear();  // (another picture's sizes)
  full_.max_spare = 1;  // (full-size buffers are the big ones: one spare, and none after an export)
  preview_src_ = Image(), preview_side_ = 0;
}

void Doc::begin_change() {
  revision_++;
  undo_.push_back(stack_);
  if (undo_.size() > kHistory) undo_.erase(undo_.begin());
  redo_.clear();
}

int Doc::add(const std::string &type) {
  Adjustment a = make_adjustment(type);
  if (a.type.empty()) return -1;
  begin_change();
  stack_.push_back(a);
  return (int)stack_.size() - 1;
}

bool Doc::remove(int index) {
  if (index < 0 || index >= (int)stack_.size()) return false;
  begin_change();
  stack_.erase(stack_.begin() + index);
  return true;
}

bool Doc::move(int from, int to) {
  const int n = (int)stack_.size();
  if (from < 0 || from >= n || to < 0 || to >= n || from == to) return false;
  begin_change();
  Adjustment a = stack_[(size_t)from];
  stack_.erase(stack_.begin() + from);
  stack_.insert(stack_.begin() + to, a);
  return true;
}

bool Doc::set(int index, const std::string &field, float value, bool record) {
  if (index < 0 || index >= (int)stack_.size()) return false;
  Adjustment probe = stack_[(size_t)index];
  if (!probe.set(field, value)) return false;
  if (record) begin_change();
  revision_++;
  stack_[(size_t)index] = probe;
  return true;
}

bool Doc::set_enabled(int index, bool on) {
  if (index < 0 || index >= (int)stack_.size() || stack_[(size_t)index].enabled == on) return false;
  begin_change();
  stack_[(size_t)index].enabled = on;
  return true;
}

bool Doc::set_curve(int index, int channel, const std::vector<Vec2> &pts, bool record) {
  if (index < 0 || index >= (int)stack_.size() || channel < 0 || channel > 3) return false;
  std::vector<Vec2> sorted = pts;
  for (Vec2 &p : sorted) p = Vec2(std::max(0.0f, std::min(1.0f, p.x)), std::max(0.0f, std::min(1.0f, p.y)));
  std::sort(sorted.begin(), sorted.end(), [](Vec2 a, Vec2 b) { return a.x < b.x; });
  if (record) begin_change();
  revision_++;
  stack_[(size_t)index].curve[channel] = sorted;
  return true;
}

bool Doc::set_mask(int index, const std::vector<MaskPart> &mask, bool record) {
  if (index < 0 || index >= (int)stack_.size()) return false;
  for (const MaskPart &m : mask)
    for (float v : m.p)
      if (!std::isfinite(v)) return false;  // (a number that can't be saved)
  if (record) begin_change();
  revision_++;
  stack_[(size_t)index].mask = mask;
  return true;
}

void Doc::step_input_size(int step, int &w, int &h) {
  w = source_.w, h = source_.h;
  for (int i = 0; i < step && i < (int)stack_.size(); i++) output_size(stack_[(size_t)i], w, h, w, h);  // (from the fields: no pixels)
}

bool Doc::geometry_after(int step) const {
  for (int i = step + 1; i < (int)stack_.size(); i++)
    if (stack_[(size_t)i].enabled && stack_[(size_t)i].info() && stack_[(size_t)i].info()->geometry) return true;
  return false;
}

int Doc::find(const std::string &type) const {
  const AdjustmentInfo *info = find_adjustment(type);
  for (int i = (int)stack_.size() - 1; info && i >= 0; i--)
    if (stack_[(size_t)i].type == info->name) return i;
  return -1;
}

bool Doc::set_path(const std::string &path, float value) {
  const size_t dot = path.find('.');
  const std::string type = path.substr(0, dot);
  const AdjustmentInfo *info = find_adjustment(type);
  if (!info || info->fields.empty()) return false;
  const std::string field = dot == std::string::npos ? info->fields[0].name : path.substr(dot + 1);
  Adjustment fresh = make_adjustment(type);
  if (!fresh.set(field, value)) return false;  // (an unknown field adds nothing)
  const int i = find(type);
  if (i >= 0) return set(i, field, value);
  begin_change();  // (added and set: one undo step)
  stack_.push_back(fresh);
  return true;
}

void Doc::set_stack(const std::vector<Adjustment> &s, bool record) {
  if (record) begin_change();
  revision_++;
  stack_ = s;
}

bool Doc::apply_preset(const std::string &name) {
  for (const Preset &p : builtin_presets()) {
    std::string a = p.name, b = name;
    for (char &c : a) c = (char)std::tolower((unsigned char)c);
    for (char &c : b) c = (char)std::tolower((unsigned char)c);
    if (a != b) continue;
    begin_change();
    stack_from_text(p.lines);
    return true;
  }
  return false;
}

bool Doc::undo() {
  if (undo_.empty()) return false;
  redo_.push_back(stack_);
  revision_++;
  stack_ = undo_.back();
  undo_.pop_back();
  return true;
}

bool Doc::redo() {
  if (redo_.empty()) return false;
  undo_.push_back(stack_);
  revision_++;
  stack_ = redo_.back();
  redo_.pop_back();
  return true;
}

void Pipeline::clear() {
  for (Image &img : cache) recycle(img);
  cache.clear(), keys.clear();
}

void Pipeline::recycle(Image &img) {
  if (img.px.empty()) return;
  if (spare.size() >= max_spare) spare.erase(spare.begin());  // (the oldest goes: sizes change with crops and previews)
  spare.push_back(std::move(img));
  img = Image();
}

/* A buffer for a w x h result: a spare one (its old pixels overwritten by the step), else a new one. */
Image Pipeline::take(int w, int h) {
  for (size_t i = 0; i < spare.size(); i++) {
    if (spare[i].px.size() != (size_t)w * h * 4) continue;
    Image img = std::move(spare[i]);
    spare.erase(spare.begin() + (long)i);
    img.w = w, img.h = h;
    return img;
  }
  spare.clear();  // (a size none of them has: they were another size's, so no use keeping them)
  return Image();
}

/* `steps` adjustments on `src` through pipeline `p`: from the first step whose settings differ from its cache's, back
 * to the nearest result still kept; then only the last two results and the input to the changed step are kept. */
const Image &Doc::run(Pipeline &p, const Image &src, int steps) {
  const ScopedTimer t;
  steps = std::max(0, std::min((int)stack_.size(), steps));
  int first = 0;
  while (first < steps && first < (int)p.keys.size() && p.keys[(size_t)first] == to_text(stack_[(size_t)first])) first++;
  if (first < steps) {  // (the steps after `steps` are the next full run's to check)
    for (size_t i = (size_t)first; i < p.cache.size(); i++) p.recycle(p.cache[i]);
    p.cache.resize((size_t)first), p.keys.resize((size_t)first);
  }
  int start = first;
  while (start > 0 && p.cache[(size_t)start - 1].empty()) start--;
  p.evaluated = steps - start;
  for (int i = start; i < steps; i++) {
    const Image &in = i == 0 ? src : p.cache[(size_t)i - 1];
    Image out = p.take(in.w, in.h);
    apply(stack_[(size_t)i], in, out, p.scale);
    if (i < first) {
      p.cache[(size_t)i] = std::move(out);
      continue;
    }
    p.cache.push_back(std::move(out));
    p.keys.push_back(to_text(stack_[(size_t)i]));
  }
  for (int i = 0; i < steps && i < (int)p.cache.size(); i++)
    if (i != steps - 1 && i != steps - 2 && i != first - 1) p.recycle(p.cache[(size_t)i]);
  p.ms = t.ms();
  return steps == 0 ? src : p.cache[(size_t)steps - 1];
}

const Image &Doc::result_upto(int steps) { return run(full_, source_, steps); }

/* The source made no larger than `max_side` (an area average), made again when the size asked for changes. */
const Image &Doc::preview_source(int max_side) {
  const int longest = std::max(source_.w, source_.h);
  if (max_side <= 0 || longest <= max_side) return source_;
  if (preview_side_ == max_side && !preview_src_.empty()) return preview_src_;
  const float s = (float)max_side / longest;
  const int w = std::max(1, (int)std::lround(source_.w * s)), h = std::max(1, (int)std::lround(source_.h * s));
  preview_src_.resize(w, h);
  JobSystem::global().parallel_for(h, 8, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++) downsample_row(source_, preview_src_, (int)y);
  });
  preview_side_ = max_side;
  preview_.clear();
  preview_.spare.clear();  // (the old size's)
  preview_.scale = (float)w / source_.w;
  return preview_src_;
}

const Image &Doc::preview_upto(int steps, int max_side) {
  const Image &src = preview_source(max_side);
  if (&src == &source_) return result_upto(steps);
  return run(preview_, src, steps);
}

const Image &Doc::result() { return result_upto((int)stack_.size()); }

void to_display(const Image &img, std::vector<uint32_t> &out, ViewTransform vt, float exposure) {
  out.resize((size_t)img.w * img.h);
  std::vector<float> rgb = rgb_of(img);
  display::encode_span(rgb.data(), out.data(), out.size(), vt, exposure);
  for (size_t i = 0; i < out.size(); i++) {  // (the image's own alpha kept)
    const float a = std::max(0.0f, std::min(1.0f, img.px[i * 4 + 3]));
    out[i] = (out[i] & 0x00ffffffu) | ((uint32_t)std::lround(a * 255.0f) << 24);
  }
}

bool Doc::export_file(const std::string &path, std::string &error) {
  if (!is_open()) {
    error = "nothing open";
    return false;
  }
  const Image &r = result();
  const std::string ext = fs::extension(path);
  bool ok = false;
  if (ext == ".exr" || ext == ".hdr") {
    std::vector<float> rgb = rgb_of(r);
    const float g = std::exp2(view_exposure_);
    for (float &v : rgb) v *= g;
    std::vector<float> alpha((size_t)r.w * r.h);
    for (size_t i = 0; i < alpha.size(); i++) alpha[i] = r.px[i * 4 + 3];
    ok = ext == ".hdr" ? write_hdr(path, rgb.data(), r.w, r.h) : (exr_available() && write_exr(path, rgb.data(), r.w, r.h, false, alpha.data()));
    if (!ok && ext == ".exr" && !exr_available()) error = "this build writes no EXR (use .hdr)";
  } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
    std::vector<uint32_t> px;
    to_display(r, px, view_, view_exposure_);
    ok = ext == ".png" ? write_png(path, px.data(), r.w, r.h, r.w, true) : write_jpeg(path, px.data(), r.w, r.h, r.w);
  } else {
    error = "unknown format (png, jpg, exr, hdr)";
    return false;
  }
  if (!ok && error.empty()) error = "could not write " + path;
  full_.spare.clear();  // (the full size's spare buffers: an export is done, the window works on its preview)
  return ok;
}

std::string Doc::stack_text() const {
  std::string s;
  for (const Adjustment &a : stack_) s += to_text(a) + "\n";
  return s;
}

void Doc::stack_from_text(const std::string &text) {
  revision_++;
  stack_.clear();
  std::istringstream in(text);
  std::string line;
  Adjustment a;
  while (std::getline(in, line))
    if (from_text(line, a)) stack_.push_back(a);
}

bool Doc::save(const std::string &path, std::string &error) {
  if (!is_open()) {
    error = "nothing open";
    return false;
  }
  if (source_path_.empty() && !save_source(path, error)) return false;
  const std::string head = strprintf("blendity-edit 1\nsource %s\nview %d %.9g\n", source_path_.c_str(), (int)view_, view_exposure_);
  if (!fs::write_file(path, head + stack_text())) {
    error = "could not write " + path;
    return false;
  }
  edit_path_ = path;
  return true;
}

/* A render's pixels beside its .edit: a half-float EXR with alpha when the build writes EXR (what the EXR reader keeps),
 * else RGBE HDR. */
bool Doc::save_source(const std::string &path, std::string &error) {
  const bool exr = exr_available();
  const std::string src = path + (exr ? ".source.exr" : ".source.hdr");
  std::vector<float> rgb = rgb_of(source_), alpha((size_t)source_.w * source_.h);
  for (size_t i = 0; i < alpha.size(); i++) alpha[i] = source_.px[i * 4 + 3];
  if (!(exr ? write_exr(src, rgb.data(), source_.w, source_.h, true, alpha.data()) : write_hdr(src, rgb.data(), source_.w, source_.h))) {
    error = "could not write " + src;
    return false;
  }
  source_path_ = src;
  return true;
}

/* The stack alone, as an .edit file without a source (an applied texture edit's adjustments). */
bool Doc::save_stack(const std::string &path, std::string &error) {
  if (fs::write_file(path, "blendity-edit 1\n" + stack_text())) return true;
  error = "could not write " + path;
  return false;
}

/* An .edit file's adjustments onto the picture open now (its source and view left alone). */
bool Doc::load_stack(const std::string &path, std::string &error) {
  std::string text;
  if (!fs::read_file(path, text) || text.compare(0, 14, "blendity-edit ") != 0) {
    error = "no stack in " + path;
    return false;
  }
  stack_from_text(text.substr(text.find('\n') + 1));
  undo_.clear(), redo_.clear();
  return true;
}

bool Doc::load(const std::string &path, std::string &error) {
  std::string text;
  if (!fs::read_file(path, text)) {
    error = "could not read " + path;
    return false;
  }
  if (text.compare(0, 14, "blendity-edit ") != 0) {
    error = "not an .edit file";
    return false;
  }
  std::istringstream in(text);
  std::string line, source, rest;
  int vt = 0;
  float ex = 0.0f;
  std::getline(in, line);
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.compare(0, 7, "source ") == 0) source = line.substr(7);
    else if (line.compare(0, 5, "view ") == 0) std::sscanf(line.c_str() + 5, "%d %f", &vt, &ex);
    else rest += line + "\n";
  }
  if (!open_file(source, error)) return false;
  stack_from_text(rest);
  /* The built-in views only (an OpenColorIO view's number belongs to one configuration). */
  set_view(vt >= 0 && vt <= 2 ? (ViewTransform)vt : ViewTransform::Standard, std::isfinite(ex) ? ex : 0.0f);
  label_ = path;
  edit_path_ = path;
  return true;
}

}  // namespace bl::edit

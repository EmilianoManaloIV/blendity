// SPDX-License-Identifier: GPL-2.0-or-later
// The Editing window's document (task 0181, ADR 0058): see edit_doc.h.
#include "edit_doc.h"

#include "../core/core.h"
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
  invalidate_from(0);
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
  bool moved = false;
  for (int i = 0; i < step && i < (int)stack_.size(); i++) moved = moved || (stack_[(size_t)i].enabled && stack_[(size_t)i].info() && stack_[(size_t)i].info()->geometry);
  const Image &in = moved ? result_upto(step) : source_;
  w = in.w, h = in.h;
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

void Doc::invalidate_from(int step) {
  if (step < (int)cache_.size()) cache_.resize((size_t)std::max(0, step));
  if (step < (int)cache_key_.size()) cache_key_.resize((size_t)std::max(0, step));
}

const Image &Doc::result_upto(int steps) {
  const ScopedTimer t;
  steps = std::max(0, std::min((int)stack_.size(), steps));
  /* The first step whose settings differ from what its cache was made with (a change there reruns it and after),
   * then back to the nearest result still kept. */
  int first = 0;
  while (first < steps && first < (int)cache_key_.size() && cache_key_[(size_t)first] == to_text(stack_[(size_t)first])) first++;
  if (first < steps) invalidate_from(first);  // (the steps after `steps` are the next full result's to check)
  int start = first;
  while (start > 0 && cache_[(size_t)start - 1].empty()) start--;
  last_evaluated_ = steps - start;
  for (int i = start; i < steps; i++) {
    Image out;
    apply(stack_[(size_t)i], i == 0 ? source_ : cache_[(size_t)i - 1], out);
    if (i < first) {
      cache_[(size_t)i] = std::move(out);
      continue;
    }
    cache_.push_back(std::move(out));
    cache_key_.push_back(to_text(stack_[(size_t)i]));
  }
  drop_cache(steps, first);
  last_ms_ = t.ms();
  return steps == 0 ? source_ : cache_[(size_t)steps - 1];
}

/* Keeps the result of the last step, the one before it and the input to the step changed last; frees the rest. */
void Doc::drop_cache(int steps, int changed) {
  for (int i = 0; i < steps && i < (int)cache_.size(); i++)
    if (i != steps - 1 && i != steps - 2 && i != changed - 1) cache_[(size_t)i] = Image();
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

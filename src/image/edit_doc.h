// SPDX-License-Identifier: GPL-2.0-or-later
// The Editing window's document (task 0181, ADR 0058): a source image in linear float, an ordered stack of
// adjustments (adjustments.h), a cached result per step - a change re-evaluates from the first step whose settings
// differ, so a slider on the last step reruns only that step - and its own undo history. Saved as an .edit text file
// (the source's path and the stack, one adjustment a line), so the source is never written over.
#pragma once

#include "../render/shading.h"
#include "adjustments.h"

#include <string>
#include <vector>

namespace bl::edit {

struct Preset {
  const char *name;
  const char *lines;  // adjustment lines (to_text's form), "\n" between
};
const std::vector<Preset> &builtin_presets();

/* A stack's results on one picture (the source, or its preview), keyed by each step's text and the scale: a few kept,
 * the buffers of the dropped ones reused (a 4K picture is 133 MB: allocating and clearing one costs as much as a
 * step). */
struct Pipeline {
  std::vector<Image> cache;       // cache[i]: through steps 0..i (empty when dropped; its key kept)
  std::vector<std::string> keys;  // to_text of step i when cached
  std::vector<Image> spare;       // buffers to write the next results into
  float scale = 1.0f;             // the picture's size over the source's (pixel-sized fields follow it)
  size_t max_spare = 2;
  int evaluated = 0;
  double ms = 0;
  void clear();
  void recycle(Image &img);
  Image take(int w, int h);
};

class Doc {
 public:
  /* A file (PNG, JPEG, TGA, BMP, HDR, EXR; 8-bit files decoded from sRGB to linear) or pixels handed over. */
  bool open_file(const std::string &path, std::string &error);
  void open_pixels(Image img, const std::string &label);
  bool is_open() const { return !source_.empty(); }
  const Image &source() const { return source_; }
  const std::string &source_path() const { return source_path_; }
  const std::string &label() const { return label_; }
  const std::string &edit_path() const { return edit_path_; }  // the .edit file it was saved to or loaded from ("" before)
  /* How the linear result is shown and written to 8-bit files: a view transform and exposure (a render opened with
   * its own, so it looks as in the Render window; Standard for an image). */
  void set_view(ViewTransform vt, float exposure) { view_ = vt, view_exposure_ = exposure; }
  ViewTransform view() const { return view_; }
  float view_exposure() const { return view_exposure_; }

  const std::vector<Adjustment> &stack() const { return stack_; }
  /* Each edit records an undo step first (a slider drag: begin_change once, then set without recording). */
  void begin_change();
  int add(const std::string &type);  // its index, -1 when the type is unknown
  bool remove(int index);
  bool move(int from, int to);
  bool set(int index, const std::string &field, float value, bool record = true);
  bool set_enabled(int index, bool on);
  bool set_mask(int index, const std::vector<MaskPart> &mask, bool record = true);  // (task 0182)
  bool set_curve(int index, int channel, const std::vector<Vec2> &pts, bool record = true);
  /* "Exposure.Exposure" or "Exposure" (its first field): the last step of that type, added when there is none. */
  bool set_path(const std::string &path, float value);
  int find(const std::string &type) const;  // the last step of that type, -1 when none
  void set_stack(const std::vector<Adjustment> &s, bool record = true);
  bool apply_preset(const std::string &name);  // replaces the stack
  bool undo();
  bool redo();
  bool can_undo() const { return !undo_.empty(); }
  bool can_redo() const { return !redo_.empty(); }

  /* The stack's result (cached; recomputed from the first changed step). */
  const Image &result();
  /* The result through `steps` adjustments only (a step's input: what its mask reads). Only a few results are kept
   * (the last two and the input to the step changed last: a slider moves without rerunning the steps before it), so
   * a 4K stack holds three pictures, not one a step. */
  const Image &result_upto(int steps);
  /* The same on a preview of the source no larger than `max_side` on its longest side (the source itself when it
   * is smaller): what the window shows, as Lightroom's previews (task 0191). */
  const Image &preview(int max_side) { return preview_upto((int)stack_.size(), max_side); }
  const Image &preview_upto(int steps, int max_side);
  const Image &preview_source(int max_side);
  float preview_scale() const { return preview_.scale; }
  double last_preview_ms() const { return preview_.ms; }
  int preview_evaluated_steps() const { return preview_.evaluated; }
  int evaluated_steps() const { return full_.evaluated; }
  /* Counts changes to the stack (and openings): a cheap key for what shows it. */
  uint64_t revision() const { return revision_; }
  /* The size of the picture step `step` acts on (the source's, unless a crop or turn comes before it). */
  void step_input_size(int step, int &w, int &h);
  bool geometry_after(int step) const;  // an enabled crop or turn after `step` (its picture isn't the one shown)  // steps rerun by the last result() (for the tests)
  double last_eval_ms() const { return full_.ms; }

  /* PNG / JPEG (8-bit sRGB through the view), EXR / HDR (linear float, the view's exposure applied), by extension. */
  bool export_file(const std::string &path, std::string &error);
  /* The .edit text: "blendity-edit 1", "source <path>", "view <transform> <exposure>", then a line per adjustment.
   * A source that isn't a file (a render) is written beside it first: <path>.source.exr (half floats, with alpha) when
   * the build writes EXR, else <path>.source.hdr (RGBE: about 8 bits of mantissa, no alpha). */
  bool save(const std::string &path, std::string &error);
  bool save_stack(const std::string &path, std::string &error);  // (task 0183: the stack alone)
  bool load_stack(const std::string &path, std::string &error);
  bool load(const std::string &path, std::string &error);
  std::string stack_text() const;
  void stack_from_text(const std::string &text);

 private:
  const Image &run(Pipeline &p, const Image &src, int steps);
  bool save_source(const std::string &path, std::string &error);
  Image source_;
  std::string source_path_, label_, edit_path_;
  std::vector<Adjustment> stack_;
  std::vector<std::vector<Adjustment>> undo_, redo_;
  Pipeline full_, preview_;
  Image preview_src_;
  int preview_side_ = 0;
  ViewTransform view_ = ViewTransform::Standard;
  float view_exposure_ = 0.0f;
  uint64_t revision_ = 1;
};

/* A decoded image in linear float (an 8-bit one from sRGB; alpha as it is). */
Image image_from_bitmap(const Bitmap &b);

/* 8-bit sRGB pixels (0xAARRGGBB, the image's alpha) of a linear image through a view transform. */
void to_display(const Image &img, std::vector<uint32_t> &out, ViewTransform vt = ViewTransform::Standard, float exposure = 0.0f);

}  // namespace bl::edit

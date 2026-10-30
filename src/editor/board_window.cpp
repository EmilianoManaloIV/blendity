// SPDX-License-Identifier: GPL-2.0-or-later
// The reference board (task 0299, ADR 0089): PureRef's endless canvas as a window. Images come in dropped from the
// system, dragged from the Project window, picked with Add Image or pasted; they are moved, scaled and turned by the
// selection's handles, and the board keeps itself in Assets/Boards/<name>.board a second after each change (and on
// closing). Its own undo; wheel zoom about the pointer, middle-drag pan, Home to fit, F to frame the selection.
//
// Pictures load on a thread of their own into the board's cache - at most 2048 pixels on their long side, with their
// mips, HDR and EXR tone-mapped - so a board of large photos neither stalls the editor nor thrashes the texture cache.
#include "editor.h"

#include "../core/jobs.h"
#include "../core/json.h"
#include "../image/image.h"

#include <chrono>
#include <cmath>
#include <cstring>

namespace bl {

using namespace platform;

namespace {
constexpr float kBoardZoomMin = 0.02f, kBoardZoomMax = 32.0f, kBoardWheelStep = 1.15f;
constexpr float kBoardKeyZoom = 1.25f;  // (Ctrl+= / Ctrl+-: a pen display's dial, task 0303 - the pad's step too)
constexpr double kBoardSaveAfter = 1.0;  // (seconds after the last change)
constexpr double kBoardNudgeJoin = 0.6;  // (arrow nudges this close together are one undo step)
constexpr size_t kBoardUndoMax = 100;
constexpr int kBoardPictureMax = 2048;   // (a picture's long side in the board's cache)
const uint32_t kBoardBack = 0xFF2A2A2C, kBoardSel = 0xFF4DA3FF;

const std::vector<platform::FileFilter> &board_image_filters() {
  static const std::vector<platform::FileFilter> f = {{"Images", {"png", "jpg", "jpeg", "tga", "bmp", "hdr", "exr"}}};
  return f;
}

inline uint32_t lerp_px(uint32_t a, uint32_t b, uint32_t c, uint32_t d, float tx, float ty) {
  uint32_t out = 0;
  for (int sh = 0; sh < 32; sh += 8) {
    const float top = ((a >> sh) & 255) + (((b >> sh) & 255) - (float)((a >> sh) & 255)) * tx;
    const float bot = ((c >> sh) & 255) + (((d >> sh) & 255) - (float)((c >> sh) & 255)) * tx;
    out |= (uint32_t)std::lround(top + (bot - top) * ty) << sh;
  }
  return out;
}

/* Half a level's size, each pixel the mean of the 2x2 under it (an odd edge repeats). */
BoardPicture::Level half_level(const BoardPicture::Level &l) {
  BoardPicture::Level o;
  o.w = std::max(1, l.w / 2), o.h = std::max(1, l.h / 2);
  o.argb.resize((size_t)o.w * o.h);
  for (int y = 0; y < o.h; y++)
    for (int x = 0; x < o.w; x++) {
      const int x0 = std::min(l.w - 1, x * 2), x1 = std::min(l.w - 1, x * 2 + 1), y0 = std::min(l.h - 1, y * 2), y1 = std::min(l.h - 1, y * 2 + 1);
      const uint32_t p[4] = {l.argb[(size_t)y0 * l.w + x0], l.argb[(size_t)y0 * l.w + x1], l.argb[(size_t)y1 * l.w + x0], l.argb[(size_t)y1 * l.w + x1]};
      uint32_t out = 0;
      for (int sh = 0; sh < 32; sh += 8) out |= ((((p[0] >> sh) & 255) + ((p[1] >> sh) & 255) + ((p[2] >> sh) & 255) + ((p[3] >> sh) & 255) + 2) / 4) << sh;
      o.argb[(size_t)y * o.w + x] = out;
    }
  return o;
}

/* A picture for the board, on a loader thread: decoded, an HDR or EXR tone-mapped (clamped, to sRGB), made at most
 * kBoardPictureMax on its long side, its mips made. */
std::shared_ptr<BoardPicture> load_board_picture_unguarded(const std::string &path) {
  auto out = std::make_shared<BoardPicture>();
  Bitmap bmp;
  std::string err;
  if (!load_image(path, bmp, err) || bmp.width <= 0 || bmp.height <= 0) {
    out->error = err.empty() ? std::string("unreadable") : err;
    return out;
  }
  out->w = bmp.width, out->h = bmp.height;
  BoardPicture::Level l;
  l.w = bmp.width, l.h = bmp.height;
  l.argb.resize((size_t)l.w * l.h);
  for (size_t i = 0; i < l.argb.size(); i++) {
    if (bmp.is_float) {
      auto c = [&](int k) { return (uint32_t)std::lround(saturate(linear_to_srgb(std::max(0.0f, bmp.rgbaf[i * 4 + k]))) * 255.0f); };
      l.argb[i] = ((uint32_t)std::lround(saturate(bmp.rgbaf[i * 4 + 3]) * 255.0f) << 24) | (c(0) << 16) | (c(1) << 8) | c(2);
      continue;
    }
    const uint8_t *p = &bmp.rgba8[i * 4];
    l.argb[i] = ((uint32_t)p[3] << 24) | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
  }
  while (std::max(l.w, l.h) > kBoardPictureMax) l = half_level(l);
  out->levels.push_back(l);
  while (out->levels.back().w > 1 || out->levels.back().h > 1) out->levels.push_back(half_level(out->levels.back()));
  return out;
}

/* A failure on the loader thread (out of memory on a huge picture) becomes the picture's error, never the editor's. */
std::shared_ptr<BoardPicture> load_board_picture(const std::string &path) {
  try {
    return load_board_picture_unguarded(path);
  }
  catch (const std::exception &e) {
    auto out = std::make_shared<BoardPicture>();
    out->error = e.what();
    return out;
  }
}

constexpr size_t kBoardLoadsAtOnce = 3;  // (pictures decoding at the same time: memory stays bounded on any platform)
}  // namespace

/* ---- the view ---- */

Vec2 Editor::board_to_screen(Vec2 p) const {
  return {board_rect_.x + board_rect_.w * 0.5f + board_.pan.x + p.x * board_.zoom, board_rect_.y + board_rect_.h * 0.5f + board_.pan.y + p.y * board_.zoom};
}

Vec2 Editor::board_from_screen(Vec2 s) const {
  return {(s.x - board_rect_.x - board_rect_.w * 0.5f - board_.pan.x) / board_.zoom, (s.y - board_rect_.y - board_rect_.h * 0.5f - board_.pan.y) / board_.zoom};
}

Vec2 Editor::board_middle() const { return {board_rect_.x + board_rect_.w * 0.5f, board_rect_.y + board_rect_.h * 0.5f}; }

void Editor::board_zoom_at(float factor, Vec2 at) {
  if (!std::isfinite(factor) || factor <= 0.0f) return;
  const Vec2 p = board_from_screen(at);
  board_.zoom = clampf(board_.zoom * factor, kBoardZoomMin, kBoardZoomMax);
  const Vec2 s = board_to_screen(p);
  board_.pan = board_.pan + (at - s);
  ui_.redraw = true;
}

/* The view fitted to the items (all, or the selection's: F), with a margin. Not before the board has a size. */
void Editor::board_frame(bool selection) {
  Vec2 lo, hi;
  if (board_rect_.w < 8 || board_rect_.h < 8) return;
  if (!board_.bounds(selection ? board_sel_ : std::vector<uint64_t>{}, lo, hi)) {
    board_.zoom = 1.0f, board_.pan = {};
    return;
  }
  const Vec2 size = hi - lo;
  const float margin = 0.9f;
  board_.zoom = clampf(std::min(board_rect_.w * margin / std::max(1.0f, size.x), board_rect_.h * margin / std::max(1.0f, size.y)), kBoardZoomMin, kBoardZoomMax);
  const Vec2 mid = (lo + hi) * 0.5f;
  board_.pan = Vec2{-mid.x * board_.zoom, -mid.y * board_.zoom};
  ui_.redraw = true;
}

/* The board's tab is in front, was drawn just now in this window, and the point is over its picture. */
bool Editor::board_has_point(int x, int y) const {
  return board_rect_.contains(x, y) && active_window_ == board_window_ && ui_.frame_number() <= board_frame_ + 2;
}

bool Editor::board_has_mouse() const { return board_has_point(ui_.in.mx, ui_.in.my); }

/* ---- the pictures ---- */

/* An image's picture: from the cache, or its load started (nullptr until it lands; a failed one keeps its error, so a
 * broken file isn't read again every frame). */
std::shared_ptr<BoardPicture> Editor::board_picture(const std::string &rel) {
  const std::string abs = resolve_asset_path(rel);
  auto it = board_pics_.find(abs);
  if (it != board_pics_.end()) return it->second;
  if (!board_loading_.count(abs) && std::find(board_queue_.begin(), board_queue_.end(), abs) == board_queue_.end()) board_queue_.push_back(abs);
  board_start_loads();
  return nullptr;
}

/* The queue's next pictures started, so at most kBoardLoadsAtOnce decode at a time (review 322: a folder of a hundred
 * photos is a hundred threads and tens of gigabytes otherwise). */
void Editor::board_start_loads() {
  while (board_loading_.size() < kBoardLoadsAtOnce && !board_queue_.empty()) {
    const std::string abs = board_queue_.front();
    board_queue_.pop_front();
    if (!board_loading_.count(abs) && !board_pics_.count(abs)) board_loading_[abs] = std::async(std::launch::async, load_board_picture, abs);
  }
}

/* Each image without its size takes it from its picture once that is cached - also after a drag put back the items it
 * began with, an undo or redo restored a step taken while the picture loaded, or a board was opened (review 322: those
 * stayed "Loading" for the session). */
void Editor::board_fill_sizes(std::vector<BoardItem> &items) {
  for (BoardItem &it : items) {
    if (it.kind != BoardItem::Image || it.w > 0.0f) continue;
    const auto p = board_pics_.find(resolve_asset_path(it.path));
    if (p == board_pics_.end() || !p->second->error.empty()) continue;
    it.w = (float)p->second->w, it.h = (float)p->second->h;
  }
}

/* Pictures no item names - on the board, in its undo steps or its drag - go (review 322: the cache only grew). */
void Editor::board_trim_pictures() {
  std::set<std::string> used;
  auto note = [&](const std::vector<BoardItem> &items) {
    for (const BoardItem &it : items)
      if (it.kind == BoardItem::Image) used.insert(resolve_asset_path(it.path));
  };
  note(board_.items), note(board_drag_start_);
  for (const auto &step : board_undo_) note(step);
  for (const auto &step : board_redo_) note(step);
  for (auto it = board_pics_.begin(); it != board_pics_.end();) it = used.count(it->first) ? std::next(it) : board_pics_.erase(it);
}

/* Loads that have landed go into the cache: each item still waiting for its size takes it, a row dropped together is
 * laid out once all of it has landed, and an image that can't be read is taken off (with its copy, when the board
 * made one). */
void Editor::board_poll_pictures(bool wait) {
  for (auto it = board_loading_.begin(); it != board_loading_.end();) {
    if (!wait && it->second.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
      ++it;
      continue;
    }
    std::shared_ptr<BoardPicture> pic = it->second.get();
    const std::string abs = it->first;
    board_pics_[abs] = pic;
    it = board_loading_.erase(it);
    board_picture_landed(abs, *pic);
    ui_.redraw = true;
  }
  board_start_loads();
  board_fill_sizes(board_.items);
  if (board_drag_) board_fill_sizes(board_drag_start_);
  if (!board_loading_.empty()) ui_.next_wakeup = ui_.next_wakeup > 0 ? std::min(ui_.next_wakeup, ui_.time + 0.05) : ui_.time + 0.05;
}

void Editor::board_picture_landed(const std::string &abs, const BoardPicture &pic) {
  std::vector<uint64_t> gone;
  bool sized = false;
  for (BoardItem &it : board_.items) {
    if (it.kind != BoardItem::Image || resolve_asset_path(it.path) != abs || it.w > 0.0f) continue;
    if (!pic.error.empty()) {
      gone.push_back(it.id);
      continue;
    }
    it.w = (float)pic.w, it.h = (float)pic.h, sized = true;
  }
  if (!pic.error.empty()) board_drop_unreadable(abs, pic.error, gone);
  board_layout_rows();
  if (sized || !gone.empty()) board_changed();
}

/* An image that turned out unreadable: its items go, and a copy the board made of it in References is deleted. */
void Editor::board_drop_unreadable(const std::string &abs, const std::string &error, const std::vector<uint64_t> &ids) {
  Log::warn("Board: can't read %s (%s)", abs.c_str(), error.c_str());
  auto drop = [&](std::vector<BoardItem> &items) {  // (from the board, its drag and its undo steps: they never were)
    std::vector<BoardItem> kept;
    for (const BoardItem &it : items)
      if (resolve_asset_path(it.path) != abs || it.w > 0.0f) kept.push_back(it);
    items.swap(kept);
  };
  (void)ids;
  drop(board_.items), drop(board_drag_start_);
  for (auto &step : board_undo_) drop(step);
  for (auto &step : board_redo_) drop(step);
  board_select_existing();
  board_pics_.erase(abs);  // (a good file put there later is read afresh)
  if (board_copied_.erase(abs)) fs::remove_file(abs);
}

/* Rows dropped together: laid out left to right from their anchor once every image in them has its size. */
void Editor::board_layout_rows() {
  for (auto r = board_rows_.begin(); r != board_rows_.end();) {
    bool ready = true;
    for (uint64_t id : r->second) {
      const BoardItem *it = board_.find(id);
      ready = ready && (!it || it->w > 0.0f);
    }
    if (!ready) {
      ++r;
      continue;
    }
    float x = r->first.x;
    for (uint64_t id : r->second) {
      BoardItem *it = board_.find(id);
      if (!it) continue;
      if (it->pos.x == r->first.x && it->pos.y == r->first.y) it->pos = {x + it->w * 0.5f, r->first.y};  // (moved meanwhile: kept where put)
      x += it->w + 16.0f;
    }
    r = board_rows_.erase(r);
  }
}

/* ---- changes, undo and the file ---- */

/* Before a change: the items as they were go on the undo stack. */
void Editor::board_begin_change() {
  board_undo_.push_back(board_.items);
  if (board_undo_.size() > kBoardUndoMax) board_undo_.erase(board_undo_.begin());
  board_redo_.clear();
}

/* After a change: saved a second after the last one (the editor wakes for it). */
void Editor::board_changed() {
  board_dirty_ = true;
  board_changed_at_ = clock_now();
  ui_.redraw = true;
}

void Editor::board_undo() {
  if (board_undo_.empty() || board_drag_) return;
  board_redo_.push_back(board_.items);
  board_.items = board_undo_.back();
  board_undo_.pop_back();
  board_fill_sizes(board_.items);
  board_select_existing();
  board_changed();
}

void Editor::board_redo() {
  if (board_redo_.empty() || board_drag_) return;
  board_undo_.push_back(board_.items);
  board_.items = board_redo_.back();
  board_redo_.pop_back();
  board_fill_sizes(board_.items);
  board_select_existing();
  board_changed();
}

/* The selection keeps only items still on the board. */
void Editor::board_select_existing() {
  std::vector<uint64_t> kept;
  for (uint64_t id : board_sel_)
    if (board_.find(id)) kept.push_back(id);
  board_sel_.swap(kept);
}

std::string Editor::board_dir() const { return fs::join(assets_dir_, "Boards"); }

/* Every frame, whether the board is shown or not: pictures that landed taken in, and a second after the last change
 * the board written (the editor woken for it). */
void Editor::board_frame_tick() {
  board_poll_pictures(false);
  if (!board_dirty_) return;
  if (clock_now() - board_changed_at_ >= kBoardSaveAfter) {
    board_save();
    return;
  }
  const double wake = ui_.time + (kBoardSaveAfter - (clock_now() - board_changed_at_)) + 0.01;
  ui_.next_wakeup = ui_.next_wakeup > 0 ? std::min(ui_.next_wakeup, wake) : wake;
}

bool Editor::board_save() {
  if (assets_dir_.empty()) return false;
  if (board_file_.empty()) {
    fs::make_dirs(board_dir());
    const std::string name = board_clean_name(board_.name);
    board_.name = name;
    board_file_ = fs::join(board_dir(), name + ".board");
    for (int k = 2; fs::exists(board_file_); k++) board_.name = strprintf("%s %d", name.c_str(), k), board_file_ = fs::join(board_dir(), board_.name + ".board");
  }
  if (!fs::write_file(board_file_, board_to_json(board_))) {
    Log::warn("Board: can't write %s", board_file_.c_str());
    return false;
  }
  board_dirty_ = false;
  project_listed_ = -100;  // (the Project window shows it)
  if (board_last_ != board_.name) board_last_ = board_.name, save_prefs();  // (opened again next time)
  return true;
}

/* A board's name made safe for its file (no trailing dots or spaces: Windows drops them). */
std::string Editor::board_clean_name(std::string name) const {
  for (const char *bad : {"/", "\\", ":", "*", "?", "\"", "<", ">", "|", "\n", "\r"})
    for (size_t at; (at = name.find(bad)) != std::string::npos;) name.erase(at, 1);
  while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();
  while (!name.empty() && name.front() == ' ') name.erase(0, 1);
  return name.empty() ? std::string("Board") : name;
}

void Editor::board_new(const std::string &name) {
  if (board_dirty_) board_save();
  board_ = Board{};
  board_.name = board_clean_name(name);
  board_file_.clear();
  board_undo_.clear(), board_redo_.clear(), board_sel_.clear(), board_rows_.clear();
  board_trim_pictures();
  board_dirty_ = false;
  board_opened_once_ = true;
  ui_.redraw = true;
}

bool Editor::board_open(const std::string &name) {
  const std::string path = fs::extension(name) == ".board" && fs::exists(name) ? name : fs::join(board_dir(), board_clean_name(name) + ".board");
  std::string text, err;
  Board b;
  if (!fs::read_file(path, text) || !board_from_json(text, b, err)) {
    Log::warn("Board: can't open %s%s", path.c_str(), err.empty() ? "" : (" (" + err + ")").c_str());
    return false;
  }
  if (board_dirty_) board_save();
  board_ = std::move(b);
  board_.name = fs::stem(path);
  board_file_ = path;
  board_undo_.clear(), board_redo_.clear(), board_sel_.clear(), board_rows_.clear();
  board_fill_sizes(board_.items);
  board_trim_pictures();
  board_dirty_ = false;
  board_opened_once_ = true;
  if (board_last_ != board_.name) board_last_ = board_.name, save_prefs();
  ui_.redraw = true;
  return true;
}

/* The first time the board is used in a session: the board used last is opened again (review 322: a new empty one
 * looked like the old one lost, and each session's first save made another). */
void Editor::board_open_last() {
  if (board_opened_once_) return;
  board_opened_once_ = true;
  if (board_last_.empty() || !board_file_.empty() || !board_.items.empty() || board_dirty_) return;
  if (fs::exists(fs::join(board_dir(), board_clean_name(board_last_) + ".board"))) board_open(board_last_);
}

std::vector<std::string> Editor::board_names() const {
  std::vector<std::string> out;
  for (const DirEntry &e : fs::list(board_dir()))
    if (!e.is_dir && fs::extension(e.name) == ".board") out.push_back(fs::stem(e.name));
  return out;
}

/* ---- getting images in ---- */

/* An image kept with the project: one from outside Assets is copied into Assets/Textures/References (shared with the
 * Reference Image: keep_in_references), so the board doesn't name a path on this machine. */
std::string Editor::board_keep_image(const std::string &path) {
  const std::string abs = resolve_asset_path(path);
  if (make_asset_relative(abs) != abs || assets_dir_.empty()) return make_asset_relative(abs);
  bool copied = false;
  const std::string kept = keep_in_references(abs, &copied);
  if (copied) board_copied_.insert(resolve_asset_path(kept));
  return make_asset_relative(kept);
}

/* A file from outside the project copied into Assets/Textures/References: the same file there is used again, another
 * file of the same name gets the next free name ("image 2.png") - never the wrong picture (review 322). */
std::string Editor::keep_in_references(const std::string &abs, bool *copied) {
  if (copied) *copied = false;
  const std::string dir = fs::join(assets_dir_, "Textures/References");
  fs::make_dirs(dir);
  /* The files' sizes, from one listing each: a file of the same name is read (both) only when the sizes match. */
  std::map<std::string, uint64_t> sizes;
  for (const DirEntry &e : fs::list(dir)) sizes[e.name] = e.size;
  uint64_t src_size = 0;
  for (const DirEntry &e : fs::list(fs::parent(abs)))
    if (e.name == fs::filename(abs)) src_size = e.size;
  std::string src_bytes;
  bool src_read = false;
  const std::string stem = fs::stem(abs), ext = fs::extension(abs);
  for (int k = 1; k < 10000; k++) {
    const std::string name = k == 1 ? stem + ext : strprintf("%s %d%s", stem.c_str(), k, ext.c_str());
    const std::string dst = fs::join(dir, name);
    const auto there = sizes.find(name);
    if (there == sizes.end() && !fs::exists(dst)) {
      if (!fs::copy_file(abs, dst)) return abs;
      if (copied) *copied = true;
      board_pics_.erase(dst);  // (a picture cached from an earlier file of this name: read afresh)
      Log::info("copied %s into Assets/Textures/References%s", fs::filename(abs).c_str(), k == 1 ? "" : (" as " + name).c_str());
      return dst;
    }
    if (there == sizes.end() || there->second != src_size) continue;
    if (!src_read) fs::read_file(abs, src_bytes), src_read = true;
    std::string bytes;
    if (fs::read_file(dst, bytes) && bytes == src_bytes) return dst;  // (the same file: used again)
  }
  return abs;
}

/* An image placed with its centre at a board point (its size arrives with its picture); its id, 0 when there's no such
 * file. */
uint64_t Editor::board_add_image(const std::string &path, Vec2 at, bool undo_step) {
  if (!fs::exists(resolve_asset_path(path))) {
    Log::warn("Board: no such file %s", path.c_str());
    return 0;
  }
  const std::string kept = board_keep_image(path);
  if (undo_step) board_begin_change();
  BoardItem it;
  it.kind = BoardItem::Image, it.id = board_.next_id++, it.path = kept, it.pos = at;
  if (std::shared_ptr<BoardPicture> pic = board_picture(kept); pic && pic->error.empty()) it.w = (float)pic->w, it.h = (float)pic->h;
  board_.items.push_back(it);
  board_sel_ = {it.id};
  board_changed();
  return it.id;
}

/* Several images in a row from a board point (laid out when their sizes land). One undo step. Files the board can't
 * take are named. */
int Editor::board_add_images(const std::vector<std::string> &paths, Vec2 at) {
  board_begin_change();
  std::vector<uint64_t> added;
  std::string skipped;
  for (const std::string &p : paths) {
    const uint64_t id = image_extension_supported(fs::extension(p)) ? board_add_image(p, at, false) : 0;
    if (id) added.push_back(id);
    else skipped += (skipped.empty() ? "" : ", ") + fs::filename(p);
  }
  if (!skipped.empty()) Log::warn("Board: takes images only - %s left out", skipped.c_str());
  if (added.empty()) {
    board_undo_.pop_back();
    return 0;
  }
  board_sel_ = added;
  board_rows_.push_back({at, added});
  board_layout_rows();
  return (int)added.size();
}

/* Ctrl+V: an image on the clipboard is saved into Assets/Textures/References and placed at the view's middle; a file
 * path on it is added as that file. */
bool Editor::board_paste() {
  if (assets_dir_.empty()) return false;
  const Vec2 mid = board_from_screen(board_middle());
  if (board_paste_for_test_.empty() && board_paste_file(mid)) return true;  // (a file copied in Explorer: added by its path)
  std::string bytes = board_paste_for_test_.empty() ? get_clipboard_image(window_) : board_paste_for_test_;
  board_paste_for_test_.clear();
  if (bytes.empty()) return board_paste_path(mid);
  /* Kept as it came - a PNG, or a screenshot's bitmap as a BMP - and read on the loader thread like any other image
   * (review 322: decoding and re-encoding a 4K screenshot here stalled the editor). */
  const bool png = bytes.size() > 8 && std::memcmp(bytes.data(), "\x89PNG", 4) == 0, bmp = bytes.size() > 54 && bytes[0] == 'B' && bytes[1] == 'M';
  if (!png && !bmp) {
    Log::warn("Board: the clipboard holds a picture in a form Blendity doesn't read");
    return false;
  }
  const std::string dir = fs::join(assets_dir_, "Textures/References");
  fs::make_dirs(dir);
  std::string file;
  for (int k = 1; file.empty() || fs::exists(file); k++) file = fs::join(dir, strprintf("Pasted %03d.%s", k, png ? "png" : "bmp"));
  if (!fs::write_file(file, bytes)) return false;
  board_pics_.erase(file);  // (a name used before: its picture read afresh)
  return board_add_image(file, mid, true) != 0;
}

/* A file copied in the system's file browser (Windows' CF_HDROP): an image added by its path, anything else left. */
bool Editor::board_paste_file(Vec2 at) {
  const std::string path = get_clipboard_file(window_);
  if (path.empty()) return false;
  if (!image_extension_supported(fs::extension(path))) {
    Log::info("Board: %s isn't an image", fs::filename(path).c_str());
    return true;
  }
  board_add_image(path, at, true);
  return true;  // (handled: added, or why not logged)
}

/* A file path on the clipboard (text) that names an image: added as that file. */
bool Editor::board_paste_path(Vec2 at) {
  std::string text = ui_.clipboard_get ? ui_.clipboard_get() : std::string();
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
  if (text.size() > 1 && text.front() == '"' && text.back() == '"') text = text.substr(1, text.size() - 2);
  if (!text.empty() && image_extension_supported(fs::extension(text)) && fs::exists(text)) return board_add_image(text, at, true) != 0;
  Log::info("Board: no image on the clipboard");
  return false;
}

/* ---- the selection ---- */

bool Editor::board_selected(uint64_t id) const {
  for (uint64_t s : board_sel_)
    if (s == id) return true;
  return false;
}

/* The selection's bounds on screen, false when nothing is selected. */
bool Editor::board_sel_box(Vec2 &lo, Vec2 &hi) const {
  Vec2 a, b;
  if (board_sel_.empty() || !board_.bounds(board_sel_, a, b)) return false;
  lo = board_to_screen(a), hi = board_to_screen(b);
  return true;
}

float Editor::board_handle_radius() const { return (float)ui_.px(tablet_ ? 12 : 7); }  // (larger for a finger in Tablet Mode)

/* A handle under a screen point: 1-4 the corners (top left, top right, bottom right, bottom left), 5 the turn handle
 * above the top's middle (below the bottom when the top is near the board's edge), 0 none. */
int Editor::board_handle_at(Vec2 m) const {
  Vec2 lo, hi;
  if (!board_sel_box(lo, hi)) return 0;
  const float r = board_handle_radius();
  const Vec2 c[4] = {lo, {hi.x, lo.y}, hi, {lo.x, hi.y}};
  for (int k = 0; k < 4; k++)
    if (std::fabs(m.x - c[k].x) <= r && std::fabs(m.y - c[k].y) <= r) return k + 1;
  return length(m - board_turn_handle(lo, hi)) <= r + 1.0f ? 5 : 0;
}

Vec2 Editor::board_turn_handle(Vec2 lo, Vec2 hi) const {
  const float off = (float)ui_.px(22);
  const float y = lo.y - off < board_rect_.y + off ? hi.y + off : lo.y - off;
  return {(lo.x + hi.x) * 0.5f, y};
}

/* The board's transform verbs on the selection, as one undo step each. */
void Editor::board_move(Vec2 d) {
  if (board_sel_.empty()) return;
  board_begin_change();
  for (uint64_t id : board_sel_)
    if (BoardItem *it = board_.find(id); it && !it->locked) it->pos = it->pos + d;
  board_changed();
}

void Editor::board_scale(float f, Vec2 pivot) {
  if (board_sel_.empty() || !std::isfinite(f) || f <= 0.0f) return;
  board_begin_change();
  for (uint64_t id : board_sel_)
    if (BoardItem *it = board_.find(id); it && !it->locked) it->scale = clampf(it->scale * f, 1e-4f, 1e4f), it->pos = pivot + (it->pos - pivot) * f;
  board_changed();
}

void Editor::board_turn(float degrees, Vec2 pivot) {
  if (board_sel_.empty() || !std::isfinite(degrees)) return;
  board_begin_change();
  const float a = degrees * kDeg2Rad, c = std::cos(a), s = std::sin(a);
  for (uint64_t id : board_sel_) {
    BoardItem *it = board_.find(id);
    if (!it || it->locked) continue;
    const Vec2 d = it->pos - pivot;
    it->pos = pivot + Vec2{d.x * c - d.y * s, d.x * s + d.y * c};
    it->angle = std::fmod(it->angle + degrees, 360.0f);
  }
  board_changed();
}

void Editor::board_delete() {
  if (board_sel_.empty()) return;
  board_begin_change();
  std::vector<BoardItem> kept;
  for (const BoardItem &it : board_.items)
    if (!board_selected(it.id) || it.locked) kept.push_back(it);
  board_.items.swap(kept);
  board_select_existing();
  board_changed();
}

void Editor::board_duplicate() {
  if (board_sel_.empty()) return;
  board_begin_change();
  std::vector<uint64_t> made;
  const size_t n = board_.items.size();
  for (size_t i = 0; i < n; i++) {
    if (!board_selected(board_.items[i].id)) continue;
    BoardItem copy = board_.items[i];
    copy.id = board_.next_id++;
    copy.pos = copy.pos + Vec2{24.0f, 24.0f} / board_.zoom;
    copy.locked = false;
    board_.items.push_back(copy);
    made.push_back(copy.id);
  }
  board_sel_ = made;
  board_changed();
}

/* The selection to the top (front) or the bottom (back), in its own order. */
void Editor::board_restack(bool front) {
  if (board_sel_.empty()) return;
  board_begin_change();
  std::vector<BoardItem> sel, rest;
  for (const BoardItem &it : board_.items) (board_selected(it.id) ? sel : rest).push_back(it);
  board_.items = front ? rest : sel;
  board_.items.insert(board_.items.end(), (front ? sel : rest).begin(), (front ? sel : rest).end());
  board_changed();
}

/* ---- drawing ---- */

/* An image item on the board: its picture's level for the screen size, bilinear, through its turn, flips and crop;
 * gray and opacity applied. The screen-to-picture map is affine, worked out once per item. */
void Editor::draw_board_image(const BoardItem &it) {
  auto &cv = ui_.canvas;
  Vec2 c[4];
  it.corners(c);
  float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
  for (Vec2 &p : c) {
    p = board_to_screen(p);
    x0 = std::min(x0, p.x), y0 = std::min(y0, p.y), x1 = std::max(x1, p.x), y1 = std::max(y1, p.y);
  }
  const Recti clip = cv.clip();
  x0 = std::max(x0, (float)clip.x), y0 = std::max(y0, (float)clip.y), x1 = std::min(x1, (float)clip.right()), y1 = std::min(y1, (float)clip.bottom());
  if (x1 <= x0 || y1 <= y0) return;
  const Recti box{(int)std::floor(x0), (int)std::floor(y0), (int)std::ceil(x1 - x0) + 1, (int)std::ceil(y1 - y0) + 1};
  std::shared_ptr<BoardPicture> pic = board_picture(it.path);
  if (!pic || !pic->error.empty() || pic->levels.empty() || it.w <= 0.0f) {
    if (it.w > 0.0f) cv.fill_polygon(c, 4, 0xFF404044);
    ui_.label(box, (pic && !pic->error.empty() ? "Missing or unreadable: " : "Loading ") + fs::filename(it.path), ui_.theme.text_dim, ui::Align::Center);
    return;
  }
  const Vec2 s = it.size();
  const float texels_per_px = (pic->levels[0].w * (it.crop[2] - it.crop[0])) / std::max(1e-3f, s.x * board_.zoom);
  size_t level = 0;
  while (level + 1 < pic->levels.size() && texels_per_px >= (float)(2u << level)) level++;
  const BoardPicture::Level &L = pic->levels[level];
  /* Screen (x, y) -> the item's local (u, v): affine, from three points. */
  const Vec2 o = it.to_local(board_from_screen({0, 0})), ex = it.to_local(board_from_screen({1, 0})) - o, ey = it.to_local(board_from_screen({0, 1})) - o;
  const float cw = it.crop[2] - it.crop[0], ch = it.crop[3] - it.crop[1];
  Image *dst = cv.target();
  JobSystem::global().parallel_for(box.h, 8, [&](int64_t ya, int64_t yb) {
    for (int y = box.y + (int)ya; y < box.y + (int)yb; y++) {
      uint32_t *row = dst->row(y);
      for (int x = box.x; x < box.right(); x++) {
        const float lx = o.x + ex.x * (x + 0.5f) + ey.x * (y + 0.5f), ly = o.y + ex.y * (x + 0.5f) + ey.y * (y + 0.5f);
        if (lx < 0.0f || ly < 0.0f || lx > 1.0f || ly > 1.0f) continue;
        const float fx = (it.crop[0] + lx * cw) * L.w - 0.5f, fy = (it.crop[1] + ly * ch) * L.h - 0.5f;
        const int ix = std::max(0, std::min(L.w - 1, (int)std::floor(fx))), iy = std::max(0, std::min(L.h - 1, (int)std::floor(fy)));
        const int ix1 = std::min(L.w - 1, ix + 1), iy1 = std::min(L.h - 1, iy + 1);
        const uint32_t *p0 = L.argb.data() + (size_t)iy * L.w, *p1 = L.argb.data() + (size_t)iy1 * L.w;
        uint32_t px = lerp_px(p0[ix], p0[ix1], p1[ix], p1[ix1], saturate(fx - ix), saturate(fy - iy));
        if (it.gray) {
          const uint32_t g = (uint32_t)std::lround(0.299f * ((px >> 16) & 255) + 0.587f * ((px >> 8) & 255) + 0.114f * (px & 255));
          px = (px & 0xFF000000u) | (g << 16) | (g << 8) | g;
        }
        const uint32_t a = (uint32_t)std::lround(((px >> 24) & 255) * saturate(it.opacity));
        row[x] = Color::over(row[x], (px & 0xFFFFFF) | (a << 24));
      }
    }
  });
}

/* The selection's outline, its corners and the turn handle; the marquee while it is dragged. */
void Editor::draw_board_selection() {
  auto &cv = ui_.canvas;
  for (uint64_t id : board_sel_) {
    const BoardItem *it = board_.find(id);
    if (!it) continue;
    Vec2 c[4];
    it->corners(c);
    for (int k = 0; k < 4; k++) {
      const Vec2 a = board_to_screen(c[k]), b = board_to_screen(c[(k + 1) % 4]);
      cv.line(a.x, a.y, b.x, b.y, it->locked ? ui_.theme.text_dim : kBoardSel, 1.5f);
    }
  }
  Vec2 lo, hi;
  if (board_sel_box(lo, hi)) {
    const float r = board_handle_radius() * 0.6f;
    const Vec2 c[4] = {lo, {hi.x, lo.y}, hi, {lo.x, hi.y}};
    for (const Vec2 &p : c) {
      const Recti h{(int)(p.x - r), (int)(p.y - r), (int)(2 * r), (int)(2 * r)};
      cv.fill_rect(h, 0xFFFFFFFF), cv.rect_outline(h, kBoardSel);
    }
    const Vec2 turn = board_turn_handle(lo, hi);
    cv.line(turn.x, turn.y < lo.y ? lo.y : hi.y, turn.x, turn.y, kBoardSel, 1.0f);
    cv.fill_circle(turn.x, turn.y, r + 1.0f, 0xFFFFFFFF);
    cv.circle(turn.x, turn.y, r + 1.0f, kBoardSel, 1.5f);
  }
  if (board_drag_ == 4) {
    const Vec2 a = board_drag_from_, b{(float)ui_.in.mx, (float)ui_.in.my};
    const Recti m{(int)std::min(a.x, b.x), (int)std::min(a.y, b.y), (int)std::fabs(b.x - a.x), (int)std::fabs(b.y - a.y)};
    cv.fill_rect(m, Color::hex(0x4DA3FF, 40));
    cv.rect_outline(m, kBoardSel);
  }
}

/* ---- input ---- */

/* The pointer on the board: the wheel zooms about it; a middle-drag pans; a press on a handle scales or turns the
 * selection, on an item picks it, on nothing starts a marquee; two fingers pinch and pan; the arrows nudge. */
void Editor::board_input(bool hovered) {
  auto &u = ui_;
  auto &in = u.in;
  const Vec2 m{(float)in.mx, (float)in.my};
  if (board_drag_) {
    board_drag_update(m);
    return;
  }
  if (!hovered) return;
  if (in.wheel_y != 0.0f) board_zoom_at(std::pow(kBoardWheelStep, in.wheel_y), m), in.wheel_y = 0.0f;
  if (std::isfinite(in.touch_zoom) && in.touch_zoom > 0.0f && in.touch_zoom != 1.0f) board_zoom_at(in.touch_zoom, m), in.touch_zoom = 1.0f;
  if (std::isfinite(in.touch_pan_x) && std::isfinite(in.touch_pan_y)) board_.pan = board_.pan + Vec2{in.touch_pan_x, in.touch_pan_y}, in.touch_pan_x = in.touch_pan_y = 0.0f;
  board_nudge_keys();
  if (in.pressed[2]) {
    board_drag_begin(5, m);
    return;
  }
  if (!in.pressed[0]) return;
  u.consume_click();
  const int handle = board_handle_at(m);
  if (handle) {
    board_drag_corner_ = handle;  // (before the drag begins: its pivot is the opposite corner)
    board_drag_begin(handle == 5 ? 3 : 2, m);
    return;
  }
  board_press_item(m);
}

/* A press on the board, not on a handle: on an item it picks (Shift: adds or takes away) and starts a move; on nothing
 * a marquee. */
void Editor::board_press_item(Vec2 m) {
  auto &in = ui_.in;
  const uint64_t hit = board_.item_at(board_from_screen(m));
  if (!hit) {
    if (!in.shift()) board_sel_.clear();
    board_drag_begin(4, m);
    return;
  }
  if (in.shift() && board_selected(hit)) {
    board_sel_.erase(std::find(board_sel_.begin(), board_sel_.end(), hit));
    return;
  }
  if (in.shift()) board_sel_.push_back(hit);
  else if (!board_selected(hit)) board_sel_ = {hit};
  board_drag_begin(1, m);
}

/* The arrows: the selection nudged a screen pixel (Shift: 10); a held key's repeats are one undo step. */
void Editor::board_nudge_keys() {
  auto &in = ui_.in;
  if (ui_.wants_keyboard() || board_sel_.empty()) return;
  const float step = (in.shift() ? 10.0f : 1.0f) / board_.zoom;
  Vec2 d{0, 0};
  if (in.key_pressed[KEY_LEFT]) d.x -= step;
  if (in.key_pressed[KEY_RIGHT]) d.x += step;
  if (in.key_pressed[KEY_UP]) d.y -= step;
  if (in.key_pressed[KEY_DOWN]) d.y += step;
  if (d.x == 0.0f && d.y == 0.0f) return;
  for (int k : {KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN}) in.key_pressed[k] = false;
  const bool joined = clock_now() - board_nudge_at_ < kBoardNudgeJoin;
  board_nudge_at_ = clock_now();
  if (!joined) board_begin_change();
  for (uint64_t id : board_sel_)
    if (BoardItem *it = board_.find(id); it && !it->locked) it->pos = it->pos + d;
  board_changed();
}

/* A drag begins: the items as they were are kept (for the drag's math and its one undo step). */
void Editor::board_drag_begin(int kind, Vec2 m) {
  board_drag_ = kind;
  board_drag_from_ = board_drag_last_ = m;
  board_drag_start_ = board_.items;
  board_drag_moved_ = false;
  Vec2 lo, hi;
  if (board_.bounds(board_sel_, lo, hi)) {
    const Vec2 c[4] = {lo, {hi.x, lo.y}, hi, {lo.x, hi.y}};
    board_drag_pivot_ = kind == 3 ? (lo + hi) * 0.5f : c[(board_drag_corner_ + 1) % 4];  // (a corner's opposite: corners 1-4)
  }
}

void Editor::board_drag_update(Vec2 m) {
  auto &in = ui_.in;
  if (!in.down[board_drag_ == 5 ? 2 : 0]) {
    board_drag_end();
    return;
  }
  ui_.redraw = true;
  if (board_drag_ == 5) {
    board_.pan = board_.pan + (m - board_drag_last_);
    board_drag_last_ = m;
    return;
  }
  if (length(m - board_drag_from_) > 2.0f) board_drag_moved_ = true;
  if (board_drag_ == 4 || !board_drag_moved_) return;  // (the marquee is chosen when it ends; a press that hasn't moved changes nothing)
  board_.items = board_drag_start_;
  const Vec2 a = board_from_screen(board_drag_from_), b = board_from_screen(m);
  for (uint64_t id : board_sel_) {
    BoardItem *it = board_.find(id);
    if (it && !it->locked) board_drag_item(*it, a, b);
  }
}

void Editor::board_drag_item(BoardItem &it, Vec2 a, Vec2 b) {
  if (board_drag_ == 1) it.pos = it.pos + (b - a);
  if (board_drag_ == 2) {
    const float f = clampf(length(b - board_drag_pivot_) / std::max(1e-3f, length(a - board_drag_pivot_)), 1e-3f, 1e3f);
    it.scale = clampf(it.scale * f, 1e-4f, 1e4f), it.pos = board_drag_pivot_ + (it.pos - board_drag_pivot_) * f;
  }
  if (board_drag_ != 3) return;
  float deg = (std::atan2(b.y - board_drag_pivot_.y, b.x - board_drag_pivot_.x) - std::atan2(a.y - board_drag_pivot_.y, a.x - board_drag_pivot_.x)) / kDeg2Rad;
  if (ui_.in.ctrl()) deg = std::round(deg / 15.0f) * 15.0f;
  const float r = deg * kDeg2Rad, c = std::cos(r), s = std::sin(r);
  const Vec2 d = it.pos - board_drag_pivot_;
  it.pos = board_drag_pivot_ + Vec2{d.x * c - d.y * s, d.x * s + d.y * c};
  it.angle += deg;
}

void Editor::board_drag_end() {
  const int kind = board_drag_;
  board_drag_ = 0;
  if (kind == 4) {
    board_marquee_end();
    return;
  }
  if (kind == 5) return;
  board_fill_sizes(board_drag_start_);
  if (!board_drag_moved_) {  // (a press and release: nothing moved, nothing to undo)
    board_.items = board_drag_start_;
    return;
  }
  std::vector<BoardItem> after = board_.items;  // (one undo step for the whole drag)
  board_.items = board_drag_start_;
  board_begin_change();
  board_.items = std::move(after);
  board_changed();
}

/* The marquee: the items whose outline it meets (a click on nothing has already cleared the selection). */
void Editor::board_marquee_end() {
  if (!board_drag_moved_) return;
  const Vec2 a = board_from_screen(board_drag_from_), b = board_from_screen({(float)ui_.in.mx, (float)ui_.in.my});
  const Vec2 lo{std::min(a.x, b.x), std::min(a.y, b.y)}, hi{std::max(a.x, b.x), std::max(a.y, b.y)};
  for (const BoardItem &it : board_.items)
    if (!board_selected(it.id) && board_marquee_meets(it, lo, hi)) board_sel_.push_back(it.id);
}

/* A turned item meets the rectangle when a corner of either is inside the other or two edges cross. */
bool Editor::board_marquee_meets(const BoardItem &it, Vec2 lo, Vec2 hi) const {
  Vec2 c[4];
  it.corners(c);
  const Vec2 r[4] = {lo, {hi.x, lo.y}, hi, {lo.x, hi.y}};
  for (const Vec2 &p : c)
    if (p.x >= lo.x && p.x <= hi.x && p.y >= lo.y && p.y <= hi.y) return true;
  for (const Vec2 &p : r)
    if (it.contains(p)) return true;
  auto side = [](Vec2 a, Vec2 b, Vec2 p) { return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x); };
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++) {
      const Vec2 p = c[i], q = c[(i + 1) % 4], s = r[j], t = r[(j + 1) % 4];
      if (side(p, q, s) * side(p, q, t) < 0.0f && side(s, t, p) * side(s, t, q) < 0.0f) return true;
    }
  return false;
}

/* ---- the window ---- */

void Editor::draw_board_window(const Recti &r) {
  auto &u = ui_;
  board_open_last();
  const int bh = u.row_h() + u.px(6);
  draw_board_toolbar({r.x, r.y, r.w, bh});
  board_rect_ = {r.x, r.y + bh, r.w, std::max(0, r.h - bh)};
  board_frame_ = u.frame_number();
  board_window_ = active_window_;
  if (board_rect_.w < 8 || board_rect_.h < 8) return;
  const bool hovered = u.hovered(board_rect_) && !u.any_popup_open();
  board_input(hovered);
  u.canvas.fill_rect(board_rect_, kBoardBack);
  u.canvas.push_clip(board_rect_);
  for (const BoardItem &it : board_.items)
    if (it.kind == BoardItem::Image) draw_board_image(it);
  draw_board_selection();
  if (board_.items.empty()) {
    const Vec2 m = board_middle();
    u.label({board_rect_.x, (int)m.y - u.row_h(), board_rect_.w, u.row_h()}, "Drop images here, or paste one (Ctrl+V)", u.theme.text_dim, ui::Align::Center);
    u.label({board_rect_.x, (int)m.y, board_rect_.w, u.row_h()}, "or drag them from the Project window", u.theme.text_dim, ui::Align::Center);
  }
  u.canvas.pop_clip();
}

void Editor::draw_board_toolbar(const Recti &bar) {
  auto &u = ui_;
  u.canvas.fill_rect(bar, u.theme.bar);
  const int h = u.row_h(), y = bar.y + u.px(3), gap = u.px(4);
  int x = bar.x + gap * 2;
  auto put = [&](int w, const char *key) {
    const Recti rr{x, y, w, h};
    x += w + gap;
    if (u.record_buttons) u.button_rects[key] = rr;
    return rr;
  };
  const Recti boards = put(u.px(78), "board:Boards");
  if (u.button(boards, "Boards", false, ui::Icon::Menu)) u.open_popup(u.id("board_menu"), {boards.x, boards.bottom(), 0, 0});
  u.tooltip("Your reference boards (Assets/Boards): a new one, or another to open. Each keeps itself a second after a change.");
  if (u.button(put(u.px(96), "board:Add Image"), "Add Image...")) defer([this] { board_add_image_dialog(); });
  u.tooltip("Pick an image file to put on the board (images from outside the project are copied into Assets/Textures/References).");
  if (u.button(put(u.px(60), "board:Paste"), "Paste")) run_console_command("board paste");
  u.tooltip("The image on the clipboard onto the board (Ctrl+V over the board): a screenshot, a picture copied from a browser.");
  const Recti zoom{bar.right() - gap * 2 - u.px(64), y, u.px(64), h};
  const bool room = zoom.x >= x + gap;  // (a narrow board: the zoom button goes first - Home still fits)
  const std::string state = board_.name + (board_dirty_ ? "  (unsaved)" : board_file_.empty() ? "" : "  (saved)");
  const int lw = (room ? zoom.x : bar.right()) - x - gap * 2;
  if (lw > u.px(24)) u.label({x + gap * 2, y, lw, h}, state, u.theme.text_dim);
  u.popup(u.id("board_menu"), u.px(220), [this] { draw_board_menu(); });
  if (!room) return;
  if (u.button(zoom, strprintf("%d%%", (int)std::lround(board_.zoom * 100.0f)))) run_console_command("board fit");
  if (u.record_buttons) u.button_rects["board:zoom"] = zoom;
  u.tooltip("The board's zoom: click to fit every image (Home). The wheel zooms about the pointer, a middle-drag pans, F frames\n"
            "the selection.");
}

void Editor::draw_board_menu() {
  auto &u = ui_;
  if (u.menu_item("New Board")) run_console_command("board new");
  u.tooltip("An empty board (this one is saved first).");
  u.menu_separator();
  for (const std::string &n : board_names())
    if (u.menu_item(n, nullptr, n == board_.name)) run_console_command("board open " + n);
}

void Editor::board_add_image_dialog() {
  std::string path;
  if (pick_open_path("Add Image to the Board", assets_dir_, board_image_filters(), path) != PathPick::Chosen) return;
  board_add_image(path, board_from_screen(board_middle()), true);
}

/* A Project window image being dragged over the board (it lands there when let go). */
bool Editor::board_asset_drag_over() const {
  return asset_drag_.dragging && !asset_drag_.mat && image_extension_supported(fs::extension(asset_drag_.path)) && board_has_mouse();
}

/* ---- the console ---- */

/* board new [name]|open <name>|save|list|add <path> [x y]|paste|select <i>|all|none|move <dx> <dy>|scale <f>|turn <deg>|
 * delete|duplicate|front|back|fit|frame|zoom <f>|in|out|undo|redo  (positions in board units; items by their place, 0 the
 * bottom). While a drag is under way only list runs (the drag holds the items). */
bool Editor::board_command(const std::vector<std::string> &t) {
  const std::string v = t.size() > 1 ? to_lower(t[1]) : "list";
  board_open_last();
  if (board_drag_ && v != "list") {
    Log::info("board: a drag is under way");
    return true;
  }
  auto num = [&](size_t i, float def) {
    const float x = i < t.size() ? (float)std::atof(t[i].c_str()) : def;
    return std::isfinite(x) ? x : def;
  };
  Vec2 lo, hi;
  const bool has_sel = board_.bounds(board_sel_, lo, hi);
  const Vec2 mid = has_sel ? (lo + hi) * 0.5f : Vec2{};
  if (v == "new") board_new(t.size() > 2 ? join_words(t, 2) : "Board");
  else if (v == "open") board_open(join_words(t, 2));
  else if (v == "save") board_save();
  else if (v == "list") {
    for (const std::string &n : board_names()) Log::info("%s%s", n == board_.name ? "* " : "  ", n.c_str());
    Log::info("board %s: %zu items, %zu selected", board_.name.c_str(), board_.items.size(), board_sel_.size());
  }
  else if (v == "add" && t.size() > 2) board_add_image(t[2], {num(3, 0.0f), num(4, 0.0f)}, true);
  else if (v == "paste") board_paste();
  else if (v == "select" && t.size() > 2) board_select_command(to_lower(t[2]));
  else if (v == "move") board_move({num(2, 0.0f), num(3, 0.0f)});
  else if (v == "scale") board_scale(num(2, 1.0f), mid);
  else if (v == "turn") board_turn(num(2, 0.0f), mid);
  else if (v == "delete") board_delete();
  else if (v == "duplicate") board_duplicate();
  else if (v == "front" || v == "back") board_restack(v == "front");
  else if (v == "fit") board_frame(false);
  else if (v == "frame") board_frame(true);
  else if (v == "zoom") board_zoom_at(num(2, 1.0f), board_middle());
  else if (v == "in" || v == "out") board_zoom_at(v == "in" ? kBoardKeyZoom : 1.0f / kBoardKeyZoom, board_has_mouse() ? Vec2{(float)ui_.in.mx, (float)ui_.in.my} : board_middle());  // (task 0303: Ctrl+= / Ctrl+-)
  else if (v == "undo") board_undo();
  else if (v == "redo") board_redo();
  else Log::info("usage: board new [name]|open <name>|save|list|add <path> [x y]|paste|select <i>|all|none|move <dx> <dy>|scale <f>|turn <deg>|delete|duplicate|front|back|fit|frame|zoom <f>|in|out|undo|redo");
  ui_.redraw = true;
  return true;
}

void Editor::board_select_command(const std::string &w) {
  if (w == "all") {
    board_sel_.clear();
    for (const BoardItem &it : board_.items) board_sel_.push_back(it.id);
    return;
  }
  if (w == "none") {
    board_sel_.clear();
    return;
  }
  if (w.empty() || w.find_first_not_of("0123456789") != std::string::npos) {
    Log::info("usage: board select <item number from 0>|all|none");
    return;
  }
  const int i = std::atoi(w.c_str());
  if (i >= 0 && i < (int)board_.items.size()) board_sel_ = {board_.items[(size_t)i].id};
}

/* For a test: every picture still loading, waited for and landed. */
void Editor::board_wait_for_test() { board_poll_pictures(true); }

}  // namespace bl

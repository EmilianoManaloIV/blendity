// SPDX-License-Identifier: GPL-2.0-or-later
// Papers (round 324, ADR 0090): the Draw tab's Paper source draws on a page of its own - a sketch in a file of its own,
// Assets/Papers/<name>.paper - not on the scene's sketch, so nothing drawn there is in the world. Many papers; each
// keeps its own undo, and is saved a second after a change (and on closing), as the reference board is. The sketch
// tool works on sketch_doc(): the paper while the pad shows it, else the scene's sketch.
#include "editor.h"

#include <cmath>

namespace bl {

namespace {
constexpr double kPaperSaveAfter = 1.0;  // (seconds after the last change)
constexpr size_t kPaperUndoMax = 100;
constexpr size_t kPaperUndoBytes = (size_t)64 << 20;  // (the history's memory: a paper of big Airbrush strokes is megabytes a step)

/* About how much memory a sketch holds (its strokes' points). */
size_t sketch_bytes(const Sketch &s) {
  size_t n = sizeof(Sketch) + s.layers.size() * sizeof(SketchLayer);
  for (const SketchStroke &st : s.strokes) n += sizeof(SketchStroke) + (st.pts.size() + st.raw.size()) * sizeof(SketchPoint);
  return n;
}
}  // namespace

/* ---- the sketch being drawn ---- */

/* True while the Draw tab's source is Paper: the sketch tool then works on the paper - only the pad binds on Paper (the
 * Scene and Game views don't: sketch_in_world), and the commands bind the pad's view (sketch_bind_work_view). Review
 * 324: tied to the bound view, a command with the Draw tab hidden fell back to the scene's sketch. */
bool Editor::sketch_on_paper() const { return draw_source_ == 2; }

Sketch &Editor::sketch_doc() {
  if (sketch_doc_override_) return *sketch_doc_override_;  // (a board's thumbnail of a paper being drawn: task 0305)
  return sketch_on_paper() ? paper_ : scene_->sketch;
}
const Sketch &Editor::sketch_doc() const {
  if (sketch_doc_override_) return *sketch_doc_override_;
  return sketch_on_paper() ? paper_ : scene_->sketch;
}

/* A sketch edit made: the paper's own undo step, or the scene's. */
void Editor::sketch_changed(const std::string &label) {
  if (sketch_on_paper()) paper_changed(label);
  else mark_changed(label);
}

/* A scene-only sketch command (Build, Lift, a scene canvas) asked for on Paper: said, and refused. */
bool Editor::sketch_refuse_on_paper(const char *what) const {
  if (!sketch_on_paper()) return false;
  Log::info("%s: Paper isn't in the scene - switch the Draw tab's source to a camera", what);
  return true;
}

/* ---- undo and the file ---- */

/* After a change: the state before it on the undo stack, and a save a second later. */
void Editor::paper_changed(const std::string &label) {
  (void)label;
  paper_undo_.push_back(paper_kept_);
  paper_trim_undo();
  paper_redo_.clear();
  paper_kept_ = paper_;
  paper_dirty_ = true;
  paper_changed_at_ = clock_now();
  paper_version_++;
  ui_.redraw = true;
}

/* The history kept to 100 steps and to 64 MB, the oldest steps dropped first (review 324). */
void Editor::paper_trim_undo() {
  size_t bytes = 0;
  for (const Sketch &s : paper_undo_) bytes += sketch_bytes(s);
  while (!paper_undo_.empty() && (paper_undo_.size() > kPaperUndoMax || bytes > kPaperUndoBytes)) {
    bytes -= sketch_bytes(paper_undo_.front());
    paper_undo_.erase(paper_undo_.begin());
  }
}

void Editor::paper_step(bool undo) {
  auto &from = undo ? paper_undo_ : paper_redo_;
  auto &to = undo ? paper_redo_ : paper_undo_;
  if (from.empty() || sketch_.stroking || sketch_.erasing) return;
  to.push_back(paper_);
  paper_ = from.back();
  from.pop_back();
  paper_kept_ = paper_;
  paper_dirty_ = true;
  paper_changed_at_ = clock_now();
  paper_version_++;
  ui_.redraw = true;
}

std::string Editor::paper_dir() const { return fs::join(assets_dir_, "Papers"); }

std::string Editor::paper_path(const std::string &name) const { return fs::join(paper_dir(), board_clean_name(name) + ".paper"); }

/* Every frame: a second after the last change the paper is written (the editor woken for it). */
void Editor::paper_frame_tick() {
  if (!paper_dirty_) return;
  if (clock_now() - paper_changed_at_ >= kPaperSaveAfter) {
    paper_save();
    return;
  }
  const double wake = ui_.time + (kPaperSaveAfter - (clock_now() - paper_changed_at_)) + 0.01;
  ui_.next_wakeup = ui_.next_wakeup > 0 ? std::min(ui_.next_wakeup, wake) : wake;
}

bool Editor::paper_save() {
  if (assets_dir_.empty() || paper_name_.empty()) return false;
  fs::make_dirs(paper_dir());
  const std::string path = paper_path(paper_name_);
  if (!fs::write_file(path, save_paper_text(paper_, paper_aspect_, draw_paper_))) {
    Log::warn("Paper: can't write %s", path.c_str());
    return false;
  }
  paper_dirty_ = false;
  paper_saves_[to_lower(fs::normalize(path))]++;  // (a board's thumbnail of it is made again: task 0305)
  project_listed_ = -100;  // (the Project window shows it)
  if (paper_last_ != paper_name_) paper_last_ = paper_name_, save_prefs();  // (opened again next time)
  return true;
}

/* A paper as a board names it: its path in the project (task 0305). */
std::string Editor::paper_rel(const std::string &name) const { return make_asset_relative(paper_path(name)); }

/* A fresh page, the state the history starts from. */
void Editor::paper_reset(const std::string &name) {
  sketch_solo_ = -1;  // (a solo is a layer of the sketch it was set on: review 324)
  paper_ = Sketch{};
  paper_.follow_camera = false;  // (a paper has no camera: its guide stays where it is put)
  paper_kept_ = paper_;
  paper_undo_.clear(), paper_redo_.clear();
  paper_name_ = name;
  paper_dirty_ = false;
  paper_version_++;
  ui_.redraw = true;
}

/* A name no paper has yet: `base`, else "base 2", "base 3"... */
std::string Editor::paper_free_name(const std::string &base) const {
  const std::string clean = board_clean_name(base);
  std::string name = clean;
  for (int k = 2; fs::exists(paper_path(name)); k++) name = strprintf("%s %d", clean.c_str(), k);
  return name;
}

void Editor::paper_new(const std::string &name) {
  if (paper_dirty_) paper_save();
  paper_reset(paper_free_name(name.empty() ? "Paper" : name));
  paper_save();  // (on disk at once: a board can pin it, the Open list shows it)
}

/* A paper file from outside Assets/Papers copied in under a free name (review 324: saved by its name alone, it would
 * have overwritten the project's paper of that name); "" when it can't be. */
std::string Editor::paper_take_in(const std::string &path) {
  if (fs::normalize(fs::parent(path)) == fs::normalize(paper_dir())) return path;
  fs::make_dirs(paper_dir());
  const std::string to = paper_path(paper_free_name(fs::stem(path)));
  if (!fs::copy_file(path, to)) {
    Log::warn("Paper: can't copy %s into %s", path.c_str(), paper_dir().c_str());
    return "";
  }
  project_listed_ = -100;
  return to;
}

bool Editor::paper_open(const std::string &name) {
  const bool by_path = fs::extension(name) == ".paper" && fs::exists(name);
  const bool is_open = !paper_name_.empty() && (by_path ? to_lower(fs::normalize(name)) == to_lower(fs::normalize(paper_path(paper_name_))) : board_clean_name(name) == paper_name_);
  if (is_open) return true;  // (the open one: as it is, its unsaved edits and history kept - reviews 324, 326)
  const bool outside = by_path;
  if (paper_dirty_) paper_save();  // (before reading: the file may be this paper's)
  const std::string path = outside ? paper_take_in(name) : paper_path(name);
  if (path.empty()) return false;
  std::string text;
  Sketch sk;
  float aspect = paper_aspect_;
  uint32_t color = draw_paper_;
  if (!fs::read_file(path, text) || !load_paper_text(text, sk, aspect, color)) {
    Log::warn("Paper: can't open %s", path.c_str());
    return false;
  }
  paper_reset(fs::stem(path));
  paper_ = std::move(sk);
  paper_kept_ = paper_;
  paper_aspect_ = aspect;
  draw_paper_ = color | 0xFF000000u;
  pad_pic_key_ = 0;
  if (paper_last_ != paper_name_) paper_last_ = paper_name_, save_prefs();
  return true;
}

/* Paper chosen as the source: the paper used last, or a new one when there is none. */
void Editor::paper_ensure_open() {
  if (!paper_name_.empty()) return;
  if (!paper_last_.empty() && fs::exists(paper_path(paper_last_)) && paper_open(paper_last_)) return;
  paper_new("Paper");
}

bool Editor::paper_rename(const std::string &to) {
  if (paper_name_.empty()) return false;
  const std::string name = board_clean_name(to);
  if (to.find_first_not_of(" .") == std::string::npos || name == "Board") {  // (nothing left of the name: review 324)
    Log::warn("Paper: \"%s\" isn't a name a file can have", to.c_str());
    return false;
  }
  if (name == paper_name_) return true;
  if (fs::exists(paper_path(name))) {
    Log::warn("Paper: a paper named %s exists", name.c_str());
    return false;
  }
  const std::string from = paper_path(paper_name_);
  paper_save();
  if (!fs::move(from, paper_path(name))) {
    Log::warn("Paper: can't rename %s", from.c_str());
    return false;
  }
  paper_relink_boards(make_asset_relative(from), paper_rel(name));  // (the boards it is on follow it: task 0305)
  paper_name_ = name;
  paper_last_ = name;
  save_prefs();
  project_listed_ = -100;
  return true;
}

/* Every board that names `from_rel` names `to_rel` instead: the open one in memory, the others in their files. */
void Editor::paper_relink_boards(const std::string &from_rel, const std::string &to_rel) {
  const std::string from = to_lower(from_rel);  // (as the file system matches names: review 326)
  auto relink = [&](std::vector<BoardItem> &items) {
    bool changed = false;
    for (BoardItem &it : items)
      if (it.kind == BoardItem::Paper && to_lower(it.path) == from) it.path = to_rel, changed = true;
    return changed;
  };
  const bool open_changed = relink(board_.items);
  for (auto &step : board_undo_) relink(step);  // (an undo after the rename keeps the paper: review 326)
  for (auto &step : board_redo_) relink(step);
  if (open_changed) board_changed();
  for (const std::string &n : board_names()) {
    if (n == board_.name) continue;
    const std::string file = fs::join(board_dir(), n + ".board");
    std::string text, err;
    Board b;
    if (!fs::read_file(file, text) || !board_from_json(text, b, err)) continue;
    if (relink(b.items)) fs::write_file(file, board_to_json(b));
  }
}

void Editor::paper_duplicate() {
  if (paper_name_.empty()) return;
  const Sketch copy = paper_;
  paper_new(paper_name_ + " copy");
  paper_ = copy;
  paper_kept_ = paper_;
  paper_save();
}

/* The paper to the trash; another one (or a new one) opened in its place. */
void Editor::paper_delete() {
  if (paper_name_.empty()) return;
  const std::string path = paper_path(paper_name_);
  std::string err;
  if (fs::exists(path) && !fs::move_to_trash(path, &err)) {
    Log::warn("Paper: can't delete %s (%s)", path.c_str(), err.c_str());
    return;  // (still open, its edits still to be saved: review 324)
  }
  paper_dirty_ = false;
  paper_name_.clear();
  paper_last_.clear();
  project_listed_ = -100;
  const std::vector<std::string> left = paper_names();
  if (!left.empty()) paper_open(left.front());
  else paper_new("Paper");
}

std::vector<std::string> Editor::paper_names() const {
  std::vector<std::string> out;
  for (const DirEntry &e : fs::list(paper_dir()))
    if (!e.is_dir && fs::extension(e.name) == ".paper") out.push_back(fs::stem(e.name));
  return out;
}

/* paper new [name]|open <name>|rename <name>|duplicate|delete|list|save|undo|redo */
bool Editor::paper_command(const std::vector<std::string> &t) {
  const std::string v = t.size() > 1 ? to_lower(t[1]) : "list";
  const std::string rest = t.size() > 2 ? join_words(t, 2) : "";
  if (v == "new") paper_new(rest);
  else if (v == "open" && !rest.empty()) paper_open(rest);
  else if (v == "rename" && !rest.empty()) paper_rename(rest);
  else if (v == "duplicate") paper_duplicate();
  else if (v == "delete") paper_delete();
  else if (v == "save") paper_save();
  else if (v == "undo" || v == "redo") paper_step(v == "undo");
  else if (v == "list")
    for (const std::string &n : paper_names()) Log::info("%s%s", n == paper_name_ ? "* " : "  ", n.c_str());
  else Log::info("usage: paper new [name]|open <name>|rename <name>|duplicate|delete|list|save|undo|redo");
  ui_.redraw = true;
  return true;
}

/* More > Paper on the Draw tab: the papers, and what to do with the open one. */
void Editor::draw_paper_menu() {
  auto &u = ui_;
  if (u.menu_item("New Paper")) run_console_command("paper new");
  u.tooltip("A new, empty paper, saved in Assets/Papers. The pad shows it.");
  u.submenu("Open Paper", u.px(200), [this] {
    const std::vector<std::string> names = paper_names();
    if (names.empty()) ui_.menu_item("(no papers yet)", nullptr, false, false);
    for (const std::string &n : names)
      if (ui_.menu_item(n, nullptr, draw_source_ == 2 && n == paper_name_)) run_console_command("paper open " + n);
  });
  u.tooltip("Draw on one of your papers.");
  if (u.menu_item("Duplicate Paper", nullptr, false, !paper_name_.empty())) run_console_command("paper duplicate");
  u.tooltip("A copy of this paper to draw on, the original kept.");
  if (u.menu_item("Delete Paper", nullptr, false, !paper_name_.empty())) run_console_command("paper delete");
  u.tooltip("This paper to the trash (the system's: it can be restored from there).");
  u.menu_separator();
  if (u.menu_item("Pin to the Board", nullptr, false, !paper_name_.empty())) run_console_command("board addpaper " + paper_name_);
  u.tooltip("This paper onto the reference board, beside your images: double-click it there to draw on it again.");
}

}  // namespace bl

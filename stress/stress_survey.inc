// SPDX-License-Identifier: GPL-2.0-or-later
// The performance survey's stress sections (round 504, task 0488, Pass 25 Speed): the parts of the program the suite
// did not time before - image adjustments and masks (`image_edit`), Sculpt and paint strokes (`sculpt_paint`), the
// sequencer and video (`sequencer`), the VRM / MMD / Godot importers (`importers`), the Unity exporter
// (`unity_export`), the Materials / Textures / History windows and the command palette (`asset_windows`) and what a
// first use costs (`cold_first_use`). Included by stress_main.cpp after test_blockout; every section is named-only
// (minutes at full size), and `--quick` runs each at its smallest size.
//
// A row's last cell is its verdict against ADR 0105's budgets: 16.7 ms for a live drag, scroll or typed letter (judged by the
// median frame, with the longest frame also under 100 ms), 50 ms for a frame while a job runs, 100 ms for the one frame an
// indivisible operation lands in, and 100 ms for any single call that blocks the window. A row nothing is judged on says `-`.
// Every file a section reads it made itself, in the scratch project (BLENDITY_PROJECT): nothing of Assets/ or research/.
#include "../src/render/canvas.h"
#include "../src/render/video_encode.h"

#include <memory>

namespace survey {

constexpr double kLive = 16.7, kJob = 50.0, kOnce = 100.0;
using ET = platform::EventType;

/* What the sections make in the scratch project, taken away again (the Materials and Textures windows list every file of Assets: a
 * count of 200 must not be 200 and the last run's leftovers). */
static void remove_survey_assets() {
  const char *proj = std::getenv("BLENDITY_PROJECT");
  if (!proj) return;
  std::error_code ec;
  namespace sfs = std::filesystem;
  /* Only the stress program's own scratch project (the temp folder's blendity_stress/project): a project set by hand is never touched. */
  const sfs::path scratch = sfs::temp_directory_path(ec) / "blendity_stress" / "project";
  if (ec || !sfs::exists(scratch, ec) || !sfs::equivalent(scratch, sfs::path(proj), ec)) return;
  auto remove_named = [&](const std::string &dir, const std::string &prefix) {
    std::vector<sfs::path> hit;
    for (const auto &e : sfs::directory_iterator(fs::join(proj, dir), ec))
      if (e.path().filename().string().rfind(prefix, 0) == 0) hit.push_back(e.path());
    for (const sfs::path &p : hit) sfs::remove_all(p, ec);
  };
  sfs::remove_all(fs::join(proj, "Assets/Survey"), ec);
  remove_named("Assets/Imported", "Survey");
  remove_named("Assets/Materials", "Survey");
  remove_named("Assets/Textures", "Survey");
  remove_named("Assets/Textures/MeshMaps", "Survey");
  remove_named("Assets/Meshes", "Survey");
  remove_named("Assets/Animations", "Survey");
  remove_named("Assets/Animations", "Scene Sequence");  // (what `seq new` makes)
  remove_named("Exports", "Survey");
  remove_named("Renders", "Survey");  // (the render sections' own folders, named for their scene)
}

/* The sections run with the console's echo off (thousands of commands), and put it back (BLENDITY_SURVEY_LOG=1 leaves it on, to see
 * what a command said when a row looks wrong). */
struct Quiet {
  bool was = Log::echo_stdout;
  Quiet() {
    Log::echo_stdout = std::getenv("BLENDITY_SURVEY_LOG") != nullptr;
    remove_survey_assets();
  }
  ~Quiet() {
    Log::echo_stdout = was;
    remove_survey_assets();
  }
};

/* The frames of one measured action. */
struct Frames {
  std::vector<double> ms;
  void add(double v) { ms.push_back(v); }
  double sum() const {
    double s = 0;
    for (double v : ms) s += v;
    return s;
  }
  double mean() const { return ms.empty() ? 0.0 : sum() / (double)ms.size(); }
  double longest() const { return ms.empty() ? 0.0 : *std::max_element(ms.begin(), ms.end()); }
  double at_rank(double q) const {
    if (ms.empty()) return 0.0;
    std::vector<double> s = ms;
    std::sort(s.begin(), s.end());
    return s[std::min(s.size() - 1, (size_t)(q * (double)(s.size() - 1) + 0.5))];
  }
  double median() const { return at_rank(0.5); }
  double p95() const { return at_rank(0.95); }
};

static const char *live_verdict(const Frames &f) { return f.ms.empty() ? "-" : (f.median() > kLive || f.longest() > kOnce) ? "OVER" : "ok"; }
/* The median is the nearest-rank one (the upper middle for an even count). */
static const char *once_verdict(double ms, double budget = kOnce) { return ms > budget ? "OVER" : "ok"; }

static const std::vector<std::string> &live_header() {
  static const std::vector<std::string> h = {"row", "size", "median ms", "p95 ms", "longest ms", "frames", "budget"};
  return h;
}
static void live_row(Report &rep, const std::string &name, const std::string &size, const Frames &f) {
  rep.row({name, size, f1(f.median()), f1(f.p95()), f1(f.longest()), std::to_string(f.ms.size()), live_verdict(f)});
}

static platform::Event mouse(ET t, int x, int y, int mods = 0) {
  platform::Event e;
  e.type = t, e.x = x, e.y = y, e.mods = mods;
  return e;
}
static platform::Event key_event(int key, int mods = 0) {
  platform::Event e;
  e.type = ET::KeyDown, e.key = key, e.mods = mods;
  return e;
}
static platform::Event wheel_event(int x, int y, float dy) {
  platform::Event e;
  e.type = ET::Wheel, e.x = x, e.y = y, e.wheel_y = dy;
  return e;
}
static platform::Event letter(char c) {
  platform::Event e;
  e.type = ET::Text, e.codepoint = (uint32_t)c;
  return e;
}

/* A frame with the events it carries, untimed. */
static void step(Editor &ed, std::vector<platform::Event> events = {}) { ed.step_frame_headless(std::move(events)); }

/* One frame's ms, with the events it carries. */
static double frame(Editor &ed, std::vector<platform::Event> events = {}) {
  ScopedTimer t;
  ed.step_frame_headless(std::move(events));
  return t.ms();
}
/* A console line and the frame that shows it: the line's own ms in `call`, the two together returned. */
static double cmd_frame(Editor &ed, const std::string &line, double *call = nullptr) {
  ScopedTimer t;
  ed.command(line);
  if (call) *call = t.ms();
  ed.step_frame_headless();
  return t.ms();
}

/* The same, for a command that may throw (a fault in the program is a row that says so, not the end of the run). */
static double cmd_frame_guarded(Editor &ed, const std::string &line, std::string &threw) {
  ScopedTimer t;
  try {
    ed.command(line);
    ed.step_frame_headless();
  } catch (const std::exception &e) {
    threw = e.what();
  } catch (...) {
    threw = "an exception that is not a std::exception";
  }
  return t.ms();
}

static std::unique_ptr<Editor> fresh(int w = 1600, int h = 900, bool async = false) {
  auto ed = std::make_unique<Editor>();
  ed->init_headless(w, h);
  if (async) ed->set_async_jobs_for_test(true);
  for (int i = 0; i < 3; i++) ed->step_frame_headless();
  return ed;
}

/* A folder of the scratch project, under Assets (the sequencer and the importers take project paths). */
static std::string asset_dir(const std::string &name) {
  const char *proj = std::getenv("BLENDITY_PROJECT");
  const std::string dir = fs::join(fs::join(proj ? proj : ".", "Assets/Survey"), name);
  fs::make_dirs(dir);
  return dir;
}

/* A w x h PNG of a gradient with a little texture (so a codec has something to do). */
static bool write_test_png(const std::string &path, int w, int h, uint32_t seed = 1) {
  std::vector<uint32_t> px((size_t)w * (size_t)h);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      const uint32_t n = (uint32_t)(x * 7 + y * 13) * 2654435761u + seed;
      const uint32_t r = (uint32_t)(x * 255 / std::max(1, w - 1)), g = (uint32_t)(y * 255 / std::max(1, h - 1)), b = 96u + ((n >> 24) & 31u);
      px[(size_t)y * (size_t)w + (size_t)x] = 0xFF000000u | r << 16 | g << 8 | b;
    }
  return write_png(path, px.data(), w, h, w);
}

static size_t object_count(Editor &ed) {
  size_t n = 0;
  ed.scene().for_each([&](GameObject &) { n++; });
  return n;
}

static std::string size_text(size_t n) { return n >= 1000000 ? strprintf("%.1fM", (double)n / 1e6) : n >= 1000 ? strprintf("%.0fk", (double)n / 1e3) : std::to_string(n); }

/* ===================================================================== image_edit */

static edit::Image gradient_image(int n) {
  edit::Image img;
  img.resize(n, n);
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++) {
      float *p = img.at(x, y);
      p[0] = 0.8f * (float)x / (float)n, p[1] = 0.6f * (float)y / (float)n, p[2] = 0.3f + 0.2f * std::sin((float)x * 0.01f) * std::cos((float)y * 0.013f), p[3] = 1.0f;
    }
  return img;
}

static edit::Adjustment adjustment_at_30_percent(const edit::AdjustmentInfo &info) {
  edit::Adjustment a = edit::make_adjustment(info.name);
  for (size_t f = 0; f < a.v.size(); f++) a.v[f] = info.fields[f].def + (info.fields[f].mx - info.fields[f].def) * 0.3f;
  if (a.type == "Tone Curve") a.curve[0] = {Vec2(0, 0), Vec2(0.5f, 0.6f), Vec2(1, 1)};
  return a;
}

/* The calls themselves on a picture n x n: each adjustment's apply, and each mask kind's weights through the document. */
static void image_edit_calls(Report &rep, int n, std::vector<std::pair<std::string, double>> &apply_ms, std::map<std::string, double> &mask_apply_ms) {
  edit::Image img = gradient_image(n);
  {
    edit::Image out;
    edit::apply(adjustment_at_30_percent(edit::adjustment_infos()[0]), img, out);  // (the output buffer made once, untimed: the rows are the adjustments')
    for (const edit::AdjustmentInfo &info : edit::adjustment_infos()) {
      const edit::Adjustment a = adjustment_at_30_percent(info);
      apply_ms.push_back({info.name, time_ms([&] { edit::apply(a, img, out); })});
    }
  }
  edit::Doc doc;
  doc.open_pixels(std::move(img), "survey");
  doc.add("Exposure");
  doc.set_path("Exposure.Exposure", 1.0f);
  doc.result();
  const edit::MaskKind kinds[] = {edit::MaskKind::Linear, edit::MaskKind::Radial, edit::MaskKind::Luminance, edit::MaskKind::Color, edit::MaskKind::Painted};
  for (edit::MaskKind k : kinds) {
    edit::MaskPart m = edit::make_mask_part(k, n, n);
    if (k == edit::MaskKind::Painted)
      for (int i = 0; i < 20; i++) edit::paint_dab(m, 0.2f + 0.03f * (float)i, 0.5f, 0.08f, 1.0f, 0.5f, false);
    doc.set_mask(0, {m});
    mask_apply_ms[edit::mask_kind_name(k)] = time_ms([&] { doc.result(); });  // (the masked step re-evaluated at full size)
  }
  rep.note(strprintf("image_edit %d x %d: %zu adjustments applied, %zu mask kinds evaluated at full size.", n, n, apply_ms.size(), mask_apply_ms.size()));
}

/* What the user's Editing window does: open, add a step, move its slider, mask it and paint on the mask; by the console, with the frames. */
static void image_edit_window(Report &rep, int n, const std::vector<std::pair<std::string, double>> &apply_ms, const std::map<std::string, double> &mask_apply_ms) {
  const std::string size = strprintf("%d x %d", n, n);
  const std::string png = fs::join(asset_dir("image_edit"), strprintf("pic%d.png", n));
  write_test_png(png, n, n);
  auto ed = fresh(1600, 900, true);
  ed->command("window Editing");
  ed->step_frame_headless();
  {
    double call = 0;
    const double first = cmd_frame(*ed, "develop open " + png, &call);  // (the command takes the rest of its line as the path: a space in it is fine)
    const double next = frame(*ed);
    rep.table({"open", "size", "call ms", "longest frame ms", "budget"});
    rep.row({"develop open (decode, first preview)", size, f1(call), f1(std::max(first, next)), once_verdict(std::max(first, next))});
  }
  rep.table({"adjustment", "size", "apply ms (full size)", "add step: longest frame ms", "slider: longest frame ms", "budget"});
  for (const edit::AdjustmentInfo &info : edit::adjustment_infos()) {
    const std::string name = info.name;
    double add = std::max(cmd_frame(*ed, "develop add " + name), frame(*ed)), slider = -1.0;
    if (!info.fields.empty() && !info.geometry) {
      const edit::FieldInfo &f = info.fields[0];
      const std::string path = name + "." + f.name;
      for (int i = 1; i <= 3; i++)
        slider = std::max(slider, cmd_frame(*ed, strprintf("develop set %s %g", path.c_str(), f.def + (f.mx - f.def) * 0.1f * (float)i)));
    }
    double apply = 0;
    for (const auto &a : apply_ms)
      if (a.first == name) apply = a.second;
    const bool over = apply > kOnce || add > kOnce || slider > kLive;
    rep.row({name, size, f1(apply), f1(add), slider < 0.0 ? std::string("-") : f1(slider), over ? "OVER" : "ok"});
    ed->command("develop remove 0");
    ed->step_frame_headless();
  }
  /* Masks: one Exposure step, each kind of mask made on it, then the parts' changes. */
  ed->command("develop add Exposure");
  ed->command("develop set Exposure.Exposure 1");
  ed->step_frame_headless();
  rep.table({"mask operation", "size", "call ms", "longest frame ms", "apply ms (full size)", "budget"});
  auto mask_row = [&](const std::string &what, const std::string &line, double apply) {
    double call = 0;
    const double first = cmd_frame(*ed, line, &call);
    const double longest = std::max(first, frame(*ed));
    rep.row({what, size, f1(call), f1(longest), apply > 0 ? f1(apply) : std::string("-"), (longest > kOnce || call > kOnce || apply > kOnce) ? "OVER" : "ok"});
  };
  auto apply_of = [&](const char *kind) {
    auto it = mask_apply_ms.find(kind);
    return it == mask_apply_ms.end() ? 0.0 : it->second;
  };
  for (const char *kind : {"linear", "radial", "luminance", "color"}) {
    mask_row(std::string("make a ") + kind + " mask", strprintf("develop mask 0 add %s", kind), apply_of(kind));
    ed->command("develop mask 0 clear");
    ed->step_frame_headless();
  }
  mask_row("make a radial mask with feather 0.1", "develop mask 0 add radial 0.5 0.5 0.3 0.3 0.1", 0);
  ed->command("develop mask 0 clear");
  mask_row("remake it with feather 0.9", "develop mask 0 add radial 0.5 0.5 0.3 0.3 0.9", 0);
  mask_row("invert the part", "develop mask 0 invert 0 on", 0);
  ed->command("develop mask 0 clear");
  ed->step_frame_headless();
  /* A paint stroke on a painted mask: a dab and the frame that shows it, 40 along a line. */
  Frames stroke;
  const int dabs = 40;
  for (int i = 0; i < dabs; i++) stroke.add(cmd_frame(*ed, strprintf("develop mask 0 paint %g 0.5 0.05", 0.1 + 0.8 * i / dabs)));
  ed->command("develop mask 0 clear");
  ed->step_frame_headless();
  rep.table(live_header());
  live_row(rep, "paint stroke on a mask (a dab a frame)", size, stroke);
}

static void test_image_edit(Report &rep, const Options &o) {
  Quiet quiet;
  rep.title("Image editing (round 504)",
            "The Editing window's adjustments, masks and mask paint at 1024 x 1024 and 4096 x 4096 (quick: 1024 only). 'apply ms' is the call "
            "on the whole picture; 'add step' and 'slider' are the longest frames of the window with the picture open (the preview the window "
            "evaluates is at most 1600 pixels): adding a step is one frame of at most 100 ms, a slider a live drag at 16.7 ms. A mask's call "
            "is the console's, its apply the document's whole-picture result. The paint stroke is a stroke of mask paint on the picture (the console's `develop mask paint`, a dab and its frame at a time, each dab "
            "working: the brush's spacing is the caller's here), not a brush on the canvas. The editor runs its jobs on workers, as the app does.");
  for (int n : o.quick ? std::vector<int>{1024} : std::vector<int>{1024, 4096}) {
    std::vector<std::pair<std::string, double>> apply_ms;
    std::map<std::string, double> mask_apply_ms;
    image_edit_calls(rep, n, apply_ms, mask_apply_ms);
    image_edit_window(rep, n, apply_ms, mask_apply_ms);
  }
}

/* ===================================================================== sculpt_paint */

/* A sphere of about `faces` quads alone in a fresh editor, centred in the Scene view. */
struct Rig {
  std::unique_ptr<Editor> ed;
  GameObject *g = nullptr;
  Recti view{};
  int cx = 0, cy = 0, radius_px = 40;  // (the object's centre in the window, and its radius on screen: a stroke starts on the object)
  std::string name;
  size_t faces = 0;
};

static Rig make_rig(int faces) {
  Rig r;
  r.ed = fresh(1600, 900, true);  // (jobs on workers, as the app runs them: mesh maps, the render mesh)
  Editor &ed = *r.ed;
  ed.command("select Cube");
  ed.command("delete");
  ed.command("create Sphere");
  r.g = ed.active_object_for_test();
  if (!r.g || !r.g->mesh_filter()) return r;
  const int rings = std::max(8, (int)std::sqrt((double)faces / 2.0));
  r.g->mesh_filter()->mesh = primitives::uv_sphere(0.5f, rings * 2, rings);
  r.g->name = "SurveySphere";  // (its mesh-map files are named for it: the section's cleanup takes them away)
  r.faces = r.g->mesh_filter()->mesh->face_count();
  r.name = r.g->name;
  ed.mark_changed_for_test("survey sphere");
  for (int i = 0; i < 5; i++) step(ed);
  for (int i = 0; i < 400 && ed.scene_camera_for_test().animating; i++) step(ed);
  ed.command("select " + r.name);
  const Vec3 at = r.g->world_position();
  ed.command(strprintf("camera 0 15 1.8 %g %g %g", at.x, at.y, at.z));  // (the view close on the sphere, as a user working on it has it: it fills much of the view)
  for (int i = 0; i < 400 && ed.scene_camera_for_test().animating; i++) step(ed);
  for (int i = 0; i < 3; i++) step(ed);
  r.view = ed.scene_view_rect();
  return r;
}

/* Frames until the sphere is in the id plane under its own centre (its render mesh is built on a worker). */
static bool wait_for_hit(Rig &r, int max_frames = 600) {
  Editor &ed = *r.ed;
  auto measure_radius = [&] {
    Vec2 c, e;
    if (ed.project_for_test(r.g->world_position(), c) && ed.project_for_test(r.g->world_matrix().point(Vec3(0.5f, 0, 0)), e)) r.radius_px = std::max(20, (int)std::fabs(e.x - c.x));
  };
  for (int i = 0; i < max_frames; i++) {
    step(ed);
    Vec2 px;
    if (!ed.project_for_test(r.g->world_position(), px)) continue;
    Vec3 w;
    float d;
    if (ed.sculpt_hit_for_test(px.x, px.y, w, d)) {
      r.cx = r.view.x + (int)px.x, r.cy = r.view.y + (int)px.y;
      measure_radius();
      return true;
    }
    if (ed.paint_for_test() || ed.weight_paint_active_for_test()) {  // (the other modes' brushes have no hit test here: the centre is the object's)
      r.cx = r.view.x + (int)px.x, r.cy = r.view.y + (int)px.y;
      measure_radius();
      if (i > 20) return true;
    }
  }
  return false;
}

/* A stroke's frames: all of them, and those in which the brush did work. */
struct StrokeFrames {
  Frames all, worked;
};

/* A drag as the user makes it: press, `moves` moves along a spiral inward on the sphere, release; every frame timed.
 *
 * What the first run of this section found: the brushes (sculpt_stroke_to, paint_stroke_to, wp_stroke_to) put a dab down only when the pointer
 * is a dab spacing (a quarter of the brush radius) from the last dab, and the last dab's place is not moved by a shorter move. A drag of 3 pixels a
 * frame, a mouse moved slowly, does its work in one frame of four: the median frame of the stroke was the idle one (1.6 ms) and the
 * stroke's cost hid in the p95. So the pointer here moves 1.5 spacings a frame (the speed of a quick stroke) and `progress` - a counter the
 * brush's own work moves (dabs made, texels covered, weights summed) - says which frames did the work; the row's times are those frames'. */
template<class Progress>
static StrokeFrames stroke_frames(Editor &ed, const Rig &r, int brush_px, int moves, Progress &&progress) {
  StrokeFrames out;
  const float step_px = std::max(1.0f, (float)brush_px * ed.ui_for_test().scale * 0.25f);
  /* A spiral inward, so that a long stroke goes over new ground, a move being 1.5 dab spacings along it. */
  const float radius = 0.65f * (float)r.radius_px;
  std::vector<Vec2> path;
  float angle = 0.0f;
  for (int k = 0; k <= moves; k++) {
    const float rk = std::max(0.15f * radius, radius * (1.0f - 0.5f * (float)k / (float)std::max(1, moves)));
    path.push_back(Vec2((float)r.cx + rk * std::cos(angle), (float)r.cy + rk * std::sin(angle)));
    angle += 1.5f * step_px / rk;
  }
  auto at = [&](int k) { return path[(size_t)k]; };
  auto timed = [&](platform::Event e) {
    const double before = progress();
    const double ms = frame(ed, {e});
    out.all.add(ms);
    if (progress() != before) out.worked.add(ms);
  };
  Vec2 p = at(0);
  step(ed, {mouse(ET::MouseMove, (int)p.x, (int)p.y)});
  timed(mouse(ET::MouseDown, (int)p.x, (int)p.y));
  for (int i = 1; i <= moves; i++) {
    p = at(i);
    timed(mouse(ET::MouseMove, (int)p.x, (int)p.y));
  }
  step(ed, {mouse(ET::MouseUp, (int)p.x, (int)p.y)});
  step(ed);
  return out;
}

static const std::vector<std::string> &stroke_header() {
  static const std::vector<std::string> h = {"row", "size", "median ms", "p95 ms", "longest ms", "frames that worked", "frames", "moved", "budget"};
  return h;
}
/* The times are those of the frames that worked; none worked: the stroke did nothing, OVER. */
static void stroke_row(Report &rep, const std::string &name, const std::string &size, const StrokeFrames &f, const std::string &moved) {
  rep.row({name, size, f1(f.worked.median()), f1(f.worked.p95()), f1(f.worked.longest()), std::to_string(f.worked.ms.size()), std::to_string(f.all.ms.size()), moved,
           f.worked.ms.empty() ? "OVER" : live_verdict(f.worked)});
}

/* A sphere with Sculpt on and a hit under its centre; false (and a note) when it won't start. */
static bool sculpt_setup(Report &rep, Rig &r, int faces, const std::string &size) {
  r = make_rig(faces);
  if (!r.g) return rep.note("sculpt_paint: no object could be made for " + size), false;
  Editor &ed = *r.ed;
  ed.command("sculpt on");
  if (!ed.sculpt_for_test() || !wait_for_hit(r)) return rep.note("sculpt_paint: Sculpt Mode could not find the " + size + " sphere under the view's centre; its rows are left out"), false;
  ed.command(strprintf("sculpt size %d", std::max(10, r.radius_px / 3)));
  ed.command("sculpt front on");
  return true;
}

static void sculpt_rows(Report &rep, int faces, int moves, size_t &problems) {
  Rig r;
  const std::string size = size_text((size_t)faces) + " faces";
  if (!sculpt_setup(rep, r, faces, size)) return;
  rep.note(strprintf("The %s sphere (%zu faces)'s centre is at %d, %d in the window, its radius %d px; the Scene view is %d x %d at %d, %d.", size.c_str(), r.faces, r.cx, r.cy, r.radius_px, r.view.w, r.view.h,
                     r.view.x, r.view.y));
  for (int b = 0; b < (int)(sizeof(kSculptBrushes) / sizeof(kSculptBrushes[0])); b++) {
    Editor &ed = *r.ed;
    ed.command("sculpt brush " + to_lower(kSculptBrushes[b]));
    const size_t steps = ed.undo_steps_for_test();
    const std::vector<Vec3> before = r.g->mesh_filter()->mesh->positions;
    const StrokeFrames f = stroke_frames(ed, r, std::max(10, r.radius_px / 3), moves, [&] { return (double)ed.sculpt_dabs_for_test(); });
    const std::vector<Vec3> &after = r.g->mesh_filter()->mesh->positions;
    size_t moved = 0;
    for (size_t v = 0; v < before.size() && v < after.size(); v++) moved += std::memcmp(&before[v], &after[v], sizeof(Vec3)) != 0;
    stroke_row(rep, std::string("Sculpt, ") + kSculptBrushes[b], size, f, size_text(moved));
    problems += ppstress::invalid(*r.g->mesh_filter()->mesh).empty() ? 0 : 1;
    /* Back to the mesh the stroke started on: undone (at most 64 tries), and when the undo stack is full and nothing was undone, a new sphere. */
    for (int tries = 0; tries < 64 && ed.undo_steps_for_test() > steps; tries++) ed.command("undo");
    step(ed);
    const std::vector<Vec3> &now = r.g->mesh_filter()->mesh->positions;
    if (now.size() == before.size() && std::memcmp(now.data(), before.data(), before.size() * sizeof(Vec3)) == 0) continue;
    rep.note("sculpt_paint: the undo did not bring the sphere back (the stack's cap); a new sphere for the next brush");
    const int next = b + 1;
    if (next >= (int)(sizeof(kSculptBrushes) / sizeof(kSculptBrushes[0])) || !sculpt_setup(rep, r, faces, size)) break;
  }
}

/* Texture paint on the object's mesh maps (the Paint Mode setup the unit test uses), then a stroke. */
static void texture_paint_rows(Report &rep, int faces, int moves) {
  Rig r = make_rig(faces);
  const std::string size = size_text((size_t)faces) + " faces";
  if (!r.g) return;
  Editor &ed = *r.ed;
  ed.command("surface new SurveySurf");
  ed.command("meshmaps 256 4");
  for (int i = 0; i < 4000 && ed.meshmaps_busy_for_test(); i++) step(ed), std::this_thread::sleep_for(std::chrono::milliseconds(2));
  ed.command("surface maps");
  ed.command("paint on");
  if (!ed.paint_for_test() || !wait_for_hit(r)) return rep.note("sculpt_paint: Paint Mode did not start on the " + size + " sphere; the texture paint row is left out");
  ed.command(strprintf("paint size %d", std::max(10, r.radius_px / 3)));
  ed.command("paint color 0.9 0.2 0.1");
  auto covered = [&] {
    const PaintImagesPtr im = ed.paint_images_for_test();
    double n = 0;
    if (im)
      for (uint8_t c : im->cover) n += c;
    return n;
  };
  const StrokeFrames f = stroke_frames(ed, r, std::max(10, r.radius_px / 3), moves, covered);
  const PaintImagesPtr im = ed.paint_images_for_test();
  size_t painted = 0;
  if (im)
    for (uint8_t c : im->cover) painted += c != 0;
  stroke_row(rep, "Texture paint, a stroke (round brush, 256 px maps)", size, f, size_text(painted) + " texels");
}

/* Weight paint is the program's vertex paint (there is no vertex-colour mode): a stroke on a bone's group. */
static void weight_paint_rows(Report &rep, int faces, int moves) {
  Rig r = make_rig(faces);
  const std::string size = size_text((size_t)faces) + " faces";
  if (!r.g) return;
  Editor &ed = *r.ed;
  ed.command("create Armature");
  ed.command("bone add Bone 1");
  ed.command("select " + r.name);
  ed.command("skin bind " + r.name);  // (the groups the strokes write: one for each bone)
  for (int i = 0; i < 4000 && ed.skin_job_running_for_test(); i++) step(ed), std::this_thread::sleep_for(std::chrono::milliseconds(2));
  ed.command("select " + r.name);
  ed.command("weightpaint on");
  step(ed);
  if (r.g->mesh_filter()->mesh->vertex_groups.empty()) return rep.note("sculpt_paint: the sphere has no vertex groups (the skin bind did not run); the weight paint row is left out");
  if (!ed.weight_paint_active_for_test() || !wait_for_hit(r)) return rep.note("sculpt_paint: Weight Paint did not start on the " + size + " sphere; its row is left out");
  ed.command(strprintf("wpbrush add %d 0.5 1", std::max(10, r.radius_px / 3)));
  auto weights = [&] {
    double n = 0;
    for (const VertexGroup &g : r.g->mesh_filter()->mesh->vertex_groups)
      for (float w : g.weights) n += w;
    return n;
  };
  const StrokeFrames f = stroke_frames(ed, r, std::max(10, r.radius_px / 3), moves, weights);
  stroke_row(rep, "Weight paint (the vertex paint), a stroke with the Add brush", size, f, "-");
}

static void test_sculpt_paint(Report &rep, const Options &o) {
  Quiet quiet;
  rep.title("Sculpt and paint strokes (round 504)",
            "Real mouse events through step_frame_headless on a sphere alone in the Scene view, with the jobs on workers as the app has them: a stroke of "
            "40 moves along a spiral inward on the sphere (quick: 12 on a 20k-face sphere), 1.5 dab spacings a move, with each Sculpt brush on a 100k- and a "
            "500k-face sphere, then a texture-paint and a weight-paint stroke (Blendity's vertex paint is the weight paint) on the first size. A brush "
            "puts a dab down only when the pointer is a dab spacing from the last one, so a stroke has frames in which it works and frames that only "
            "move the pointer: 'frames that worked' counts the first (the brush's own counter moved: dabs made, texels covered, weights summed) and the "
            "times are theirs (weight paint: a frame worked when the weights changed - a dab changes only the vertices nearer its centre than the stroke's earlier dabs were), judged against the live-drag budget (16.7 ms median, none over 100 ms). Slow strokes of a few pixels a frame work "
            "every fourth frame: the first run's median of 1.6 ms was the idle frames'. A mask stroke in the Editing window is one console dab a frame, "
            "each working. After the strokes the meshes are checked for structure.");
  rep.table(stroke_header());
  const int moves = o.quick ? 12 : 40;
  size_t problems = 0;
  const std::vector<int> sizes = o.quick ? std::vector<int>{20000} : std::vector<int>{100000, 500000};
  for (int faces : sizes) sculpt_rows(rep, faces, moves, problems);
  texture_paint_rows(rep, sizes[0], moves);
  weight_paint_rows(rep, sizes[0], moves);
  rep.note(strprintf("Structural problems in the sculpted meshes: %zu.", problems));
}

/* ===================================================================== sequencer */

/* A sequence of `strips` strips on up to eight tracks (every fourth a footage strip when there is a file), the commands' own ms and frames returned. */
static Frames build_sequence(Editor &ed, int strips, const std::vector<std::string> &images, const std::string &video) {
  ed.command("seq new");
  ed.command("window Game");
  ed.command("window Sequencer");
  ed.step_frame_headless();
  Frames add;
  for (int i = 0; i < strips; i++) {
    const int track = i % 8;
    const int start = (i * 7) % 90;
    if (!video.empty() && i % 4 == 3) add.add(cmd_frame(ed, strprintf("seq video %s %d %d 60", video.c_str(), track, start)));
    else add.add(cmd_frame(ed, strprintf("seq image %s %d %d 60", images[(size_t)i % images.size()].c_str(), track, start)));
  }
  return add;
}

/* A frame with the pointer moving over the Sequencer window (the user's hand is on the mouse while the playhead moves: the window redraws). */
static double hover_frame(Editor &ed, int k) {
  const Recti r = ed.window_rect_for_test(WindowKind::Sequencer);
  return frame(ed, {mouse(ET::MouseMove, r.x + r.w / 2 + (k % 5) * 3, r.y + r.h / 2)});
}

/* A discrete command's row (adding a strip): judged as one frame of an operation, not as a drag. */
static void discrete_row(Report &rep, const std::string &name, const std::string &size, const Frames &f) {
  rep.row({name, size, f1(f.median()), f1(f.p95()), f1(f.longest()), std::to_string(f.ms.size()), once_verdict(f.longest())});
}

static void sequencer_scene_rows(Report &rep, Editor &ed, int strips, const std::vector<std::string> &images, const std::string &video, int frames) {
  const std::string size = strprintf("%d strips", strips);
  discrete_row(rep, "add the strips (a command and its frame each), " + size, size, build_sequence(ed, strips, images, video));
  ed.command("seq preview on");
  ed.step_frame_headless();
  Frames scrub;
  for (int k = 0; k < frames; k++) {
    ScopedTimer t;
    ed.command(strprintf("seq frame %d", (k * 3) % 120));
    hover_frame(ed, k);
    scrub.add(t.ms());
  }
  live_row(rep, "scrub the timeline, " + size, size, scrub);  // (the size in the label: perf_suite keys a row by its first cell)
  ed.command("seq frame 0");
  ed.command("seq play");
  Frames play;
  for (int k = 0; k < frames; k++) play.add(hover_frame(ed, k));
  ed.command("seq stop");
  live_row(rep, "play the timeline, " + size, size, play);
}

static void test_sequencer(Report &rep, const Options &o) {
  Quiet quiet;
  rep.title("Sequencer and video (round 504)",
            "A timeline of 1, 8 and 32 image strips (every fourth a video strip where this build has a video codec; quick: 1 and 8): the strips "
            "added, the timeline scrubbed and played with the Sequencer window and its preview open, a cross-fade of two overlapping strips scrubbed "
            "through, and a render of N frames to a video (the longest frame while it renders against 50 ms, the total, and the encoder alone). "
            "Rows are frames of a live action against 16.7 ms (the median, nearest rank); adding a strip is a command, one frame against 100 ms. "
            "The editors run their jobs on workers, as the app does. The strips' files are named by project path (the sequencer's commands split on spaces).");
  const std::string dir = asset_dir("sequencer"), rel = "Assets/Survey/sequencer";
  std::vector<std::string> images;  // (project-relative: a temp folder with a space in its name would split the command)
  for (int i = 0; i < 4; i++) {
    write_test_png(fs::join(dir, strprintf("card%d.png", i)), 640, 360, (uint32_t)i + 1);
    images.push_back(strprintf("%s/card%d.png", rel.c_str(), i));
  }
  const int frames = o.quick ? 10 : 40;
  /* A video file for the footage strips: the renderer's own encoder, when this build has one. */
  std::string video;
  const int video_frames = o.quick ? 12 : 120;
  if (video::available()) {
    std::vector<std::string> pngs;
    for (int i = 0; i < video_frames; i++) {
      pngs.push_back(fs::join(dir, strprintf("v%03d.png", i)));
      write_test_png(pngs.back(), 320, 180, (uint32_t)i + 11);
    }
    video::init();
    const std::string out = fs::join(dir, "footage.mp4");
    std::string err;
    ScopedTimer t;
    const bool ok = video::encode_png_frames(pngs, 24.0f, out, &err);
    const double ms = t.ms();
    if (ok) video = rel + "/footage.mp4";
    rep.table({"video", "total s", "frames", "frame size", "budget"});  // (the time before the count: perf_suite compares a row's first time column)
    rep.row({strprintf("encode_png_frames, %s", video::description().c_str()), f2(ms / 1000.0), std::to_string(video_frames), "320 x 180", "-"});
    if (!ok) rep.note("sequencer: the encoder said: " + err + " - no video strips");
  }
  else rep.note("sequencer: this build has no video codec (BL_WITH_FFMPEG): no video strips and no encoder rows.");
  rep.table(live_header());
  for (int strips : o.quick ? std::vector<int>{1, 8} : std::vector<int>{1, 8, 32}) {
    auto ed = fresh(1600, 900, true);
    sequencer_scene_rows(rep, *ed, strips, images, video, frames);
  }
  {
    auto ed = fresh(1600, 900, true);
    ed->command("seq new");
    ed->command("window Game");
    ed->command("window Sequencer");
    ed->command("seq preview on");
    ed->command(strprintf("seq image %s 0 0 60", images[0].c_str()));
    ed->command(strprintf("seq image %s 0 30 60", images[1].c_str()));
    ed->step_frame_headless();
    Frames fade;
    for (int k = 0; k < frames; k++) {
      ScopedTimer t;
      ed->command(strprintf("seq frame %d", 30 + (k * 30) / frames));
      hover_frame(*ed, k);
      fade.add(t.ms());
    }
    live_row(rep, "scrub through a cross-fade (two strips overlapping 30 frames)", "2 strips", fade);
  }
  /* The render to a video, as the user waits for it: frames while a job runs. */
  {
    const int n = o.quick ? 6 : 24;
    auto ed = fresh(1280, 720, true);
    ed->command("stress 100 Cube");
    ed->scene().render.engine = 0, ed->scene().render.width = 640, ed->scene().render.height = 360;
    build_sequence(*ed, 8, images, video);  // (Render Animation renders the scene's sequence: 8 strips over 100 cubes)
    /* The folders Render Animation makes in Renders/ are this run's: the ones that were not there before are taken away. */
    const char *proj = std::getenv("BLENDITY_PROJECT");
    std::set<std::string> renders_before;
    for (const DirEntry &de : fs::list(fs::join(proj ? proj : ".", "Renders"))) renders_before.insert(de.name);
    const StallRun r = run_stall(*ed, [&] { ed->command(strprintf("renderanim 1 %d 24", n)); }, [&] { return ed->rendering_for_test(); }, 600000.0);
    for (const DirEntry &de : fs::list(fs::join(proj ? proj : ".", "Renders")))
      if (!renders_before.count(de.name) && de.is_dir) {
        std::error_code ec;
        std::filesystem::remove_all(fs::join(fs::join(proj ? proj : ".", "Renders"), de.name), ec);
      }
    rep.table({"render to video", "total s", "longest frame ms", "frames", "in the tray", "budget"});
    rep.row({strprintf("renderanim of an 8-strip sequence at 640 x 360 (%s)", video::available() ? "an MP4" : "PNG frames"), f2(r.total / 1000.0), f1(r.longest), std::to_string(n), r.tray ? r.label : "-",
             once_verdict(r.longest, kJob)});
  }
}

/* ===================================================================== importers */

/* A PMX with `verts` vertices on a grid, one bone, one material (the smallest valid file, scaled): the program's own fixture is eight vertices. */
static std::string pmx_scaled(int verts) {
  const int g = std::max(2, (int)std::ceil(std::sqrt((double)verts)));
  const int nv = g * g;
  R156Bin b;
  b.s = "PMX ";
  b.f(2.0f);
  b.u8(8);
  for (int v : {0, 0, 4, 1, 1, 2, 1, 1}) b.u8(v);  // (UTF-16, no extra UVs, four-byte vertex indices)
  b.text("survey", false), b.text("survey", false), b.text("", false), b.text("", false);
  b.i32(nv);
  for (int y = 0; y < g; y++)
    for (int x = 0; x < g; x++) {
      b.v3({(float)x * 0.1f, (float)y * 0.1f, std::sin((float)x * 0.3f) * std::cos((float)y * 0.2f)}), b.v3({0, 0, -1}), b.f((float)x / (float)g), b.f((float)y / (float)g);
      b.u8(0), b.u16(0), b.f(1.0f);
    }
  b.i32(6 * (g - 1) * (g - 1));
  for (int y = 0; y + 1 < g; y++)
    for (int x = 0; x + 1 < g; x++) {
      const int a = y * g + x, c = a + g;
      for (int i : {a, a + 1, c, c, a + 1, c + 1}) b.i32(i);
    }
  b.i32(0);  // (textures)
  b.i32(1);
  b.text("skin", false), b.text("skin", false);
  b.f(0.8f), b.f(0.6f), b.f(0.5f), b.f(1.0f), b.v3({0, 0, 0}), b.f(5.0f), b.v3({0.2f, 0.2f, 0.2f});
  b.u8(0x10 | 0x04);
  b.f(0.1f), b.f(0.05f), b.f(0.2f), b.f(1.0f), b.f(1.5f);
  b.u8(0xFF), b.u8(0xFF), b.u8(0);  // (no texture, no sphere map)
  b.u8(1), b.u8(2);                  // (MMD's shared toon03)
  b.text("", false), b.i32(6 * (g - 1) * (g - 1));
  b.i32(1);  // one bone, "センター", no tail bone
  b.text("センター", false), b.text("center", false);
  b.v3({0, 0, 0});
  b.u16(0xFFFF), b.i32(0);
  b.u16(0x0002 | 0x0008 | 0x0010);
  b.v3({0, 1, 0});
  b.i32(0), b.i32(0), b.i32(0), b.i32(0);  // (morphs, display frames, rigid bodies, joints)
  return b.s;
}

/* A VMD motion of `keys` keys on the センター bone (Shift-JIS), the file's header and its empty morph, camera, light, shadow and IK tables. */
static std::string vmd_scaled(int keys) {
  R156Bin b;
  b.s = "Vocaloid Motion Data 0002";
  b.s.resize(30, '\0');
  b.s += "survey";
  b.s.resize(50, '\0');
  b.i32(keys);
  for (int k = 0; k < keys; k++) {
    std::string name = "\x83\x5a\x83\x93\x83\x5e\x81\x5b";
    name.resize(15, '\0');
    b.s += name;
    b.i32(k);
    b.v3({0.01f * (float)(k % 50), 0, 0});
    const float a = 0.02f * (float)(k % 40);
    b.f(0), b.f(std::sin(a)), b.f(0), b.f(std::cos(a));
    b.s += std::string(64, '\x14');
  }
  for (int t = 0; t < 5; t++) b.i32(0);
  return b.s;
}

/* A binary glTF (.vrm: glTF with the VRM 0.x extension): a skinned grid of `tris` triangles on three humanoid bones. */
static std::string vrm_scaled(int tris) {
  const int g = std::max(2, (int)std::ceil(std::sqrt((double)tris / 2.0)) + 1);
  const int nv = g * g, ni = 6 * (g - 1) * (g - 1);
  std::string bin;
  auto put = [&](const void *p, size_t n) { bin.append((const char *)p, n); };
  float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
  for (int y = 0; y < g; y++)
    for (int x = 0; x < g; x++) {
      const float p[3] = {(float)x / (float)(g - 1) * 1.0f - 0.5f, (float)y / (float)(g - 1) * 1.5f, 0.1f * std::sin((float)x * 0.2f)};
      for (int k = 0; k < 3; k++) lo[k] = std::min(lo[k], p[k]), hi[k] = std::max(hi[k], p[k]);
      put(p, 12);
    }
  const size_t pos_bytes = bin.size();
  for (int y = 0; y + 1 < g; y++)
    for (int x = 0; x + 1 < g; x++) {
      const uint32_t a = (uint32_t)(y * g + x), c = a + (uint32_t)g, idx[6] = {a, a + 1, c, c, a + 1, c + 1};
      put(idx, sizeof(idx));
    }
  const size_t idx_bytes = bin.size() - pos_bytes;
  for (int y = 0; y < g; y++)
    for (int x = 0; x < g; x++) {
      const uint8_t j[4] = {(uint8_t)(y * 3 / g), 0, 0, 0};
      put(j, 4);
    }
  const size_t joint_bytes = (size_t)nv * 4;
  for (int i = 0; i < nv; i++) {
    const float w[4] = {1, 0, 0, 0};
    put(w, 16);
  }
  const size_t weight_bytes = (size_t)nv * 16;
  float ibm[3][16] = {};
  for (int j = 0; j < 3; j++) ibm[j][0] = ibm[j][5] = ibm[j][10] = ibm[j][15] = 1.0f, ibm[j][13] = -0.5f * (float)j;
  put(ibm, sizeof(ibm));
  std::string json = strprintf(
      "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0,1]}],"
      "\"nodes\":[{\"name\":\"Body\",\"mesh\":0,\"skin\":0},{\"name\":\"Hips\",\"children\":[2]},{\"name\":\"Spine\",\"translation\":[0,0.5,0],\"children\":[3]},"
      "{\"name\":\"Head\",\"translation\":[0,0.5,0]}],"
      "\"meshes\":[{\"name\":\"Body\",\"primitives\":[{\"attributes\":{\"POSITION\":0,\"JOINTS_0\":2,\"WEIGHTS_0\":3},\"indices\":1}]}],"
      "\"skins\":[{\"joints\":[1,2,3],\"inverseBindMatrices\":4,\"skeleton\":1}],"
      "\"buffers\":[{\"byteLength\":%zu}],"
      "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":%zu},{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu},"
      "{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu},{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu},{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":192}],"
      "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":%d,\"type\":\"VEC3\",\"min\":[%g,%g,%g],\"max\":[%g,%g,%g]},"
      "{\"bufferView\":1,\"componentType\":5125,\"count\":%d,\"type\":\"SCALAR\"},"
      "{\"bufferView\":2,\"componentType\":5121,\"count\":%d,\"type\":\"VEC4\"},{\"bufferView\":3,\"componentType\":5126,\"count\":%d,\"type\":\"VEC4\"},"
      "{\"bufferView\":4,\"componentType\":5126,\"count\":3,\"type\":\"MAT4\"}],"
      "\"extensionsUsed\":[\"VRM\"],\"extensions\":{\"VRM\":{\"specVersion\":\"0.0\",\"meta\":{\"title\":\"Survey\"},"
      "\"humanoid\":{\"humanBones\":[{\"bone\":\"hips\",\"node\":1},{\"bone\":\"spine\",\"node\":2},{\"bone\":\"head\",\"node\":3}]}}}}",
      bin.size(), pos_bytes, pos_bytes, idx_bytes, pos_bytes + idx_bytes, joint_bytes, pos_bytes + idx_bytes + joint_bytes, weight_bytes, pos_bytes + idx_bytes + joint_bytes + weight_bytes, nv,
      (double)lo[0], (double)lo[1], (double)lo[2], (double)hi[0], (double)hi[1], (double)hi[2], ni, nv, nv);
  while (json.size() % 4) json += ' ';
  while (bin.size() % 4) bin += '\0';
  R156Bin out;
  out.s = "glTF";
  out.i32(2);
  out.i32((int64_t)(12 + 8 + json.size() + 8 + bin.size()));
  out.i32((int64_t)json.size());
  out.s += "JSON" + json;
  out.i32((int64_t)bin.size());
  out.s += std::string("BIN\0", 4) + bin;
  return out.s;
}

/* A Godot 4 scene of `nodes` MeshInstance3D nodes (every fiftieth an OmniLight3D) with a material from a .tres beside it, in a project folder. */
static std::string godot_scaled(const std::string &dir, int nodes) {
  fs::make_dirs(dir);
  fs::write_file(fs::join(dir, "project.godot"), "config_version=5\n\n[application]\nconfig/name=\"survey\"\n");
  fs::write_file(fs::join(dir, "survey.tres"), "[gd_resource type=\"StandardMaterial3D\" format=3]\n\n[resource]\nalbedo_color = Color(0.2, 0.6, 0.9, 1)\n");
  std::string s = "[gd_scene load_steps=4 format=3]\n\n[ext_resource type=\"Material\" path=\"res://survey.tres\" id=\"1_mat\"]\n\n"
                  "[sub_resource type=\"StandardMaterial3D\" id=\"2\"]\nalbedo_color = Color(0.8, 0.2, 0.2, 1)\n\n"
                  "[sub_resource type=\"BoxMesh\" id=\"3\"]\nsize = Vector3(1, 1, 1)\n\n[node name=\"Root\" type=\"Node3D\"]\n\n";
  const int side = std::max(1, (int)std::ceil(std::sqrt((double)nodes)));
  for (int i = 0; i < nodes; i++) {
    const double x = (i % side) * 2.0, z = (i / side) * 2.0;
    const std::string t = strprintf("Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, %g, 0, %g)", x, z);
    if (i % 50 == 49) s += strprintf("[node name=\"Lamp%d\" type=\"OmniLight3D\" parent=\".\"]\ntransform = %s\nlight_energy = 2.0\n\n", i, t.c_str());
    else
      s += strprintf("[node name=\"Mesh%d\" type=\"MeshInstance3D\" parent=\".\"]\ntransform = %s\nmesh = SubResource(\"3\")\n%s\n\n", i, t.c_str(),
                     i % 2 ? "surface_material_override/0 = SubResource(\"2\")" : "material_override = ExtResource(\"1_mat\")");
  }
  const std::string path = fs::join(dir, strprintf("Survey%d.tscn", nodes));
  fs::write_file(path, s);
  return path;
}

/* An import run frame by frame: the frames while it ran, and the one it landed in. */
struct ImportTrace {
  Frames frames;
  double total = 0, landing = 0, longest_before = 0;
  size_t made = 0;
};
template<class Running> static ImportTrace trace_import(Editor &ed, const std::string &line, Running &&running) {
  ImportTrace t;
  const size_t before = object_count(ed);
  ScopedTimer whole;
  t.frames.add(cmd_frame(ed, line));
  size_t guard = 0;
  while (running() && whole.ms() < 600000.0 && guard++ < 1000000) t.frames.add(frame(ed));
  for (size_t i = 0; i < t.frames.ms.size(); i++) t.longest_before = std::max(t.longest_before, t.frames.ms[i]);
  /* The job is done when the worker says so; the objects arrive in the frame after it (import_job_poll -> import_landed): that frame, timed, is the
   * landing. A few more frames are looked at when nothing arrived in it, and the slowest of them counts. */
  double worst = 0;
  for (int i = 0; i < 10; i++) {
    const double f = frame(ed);
    worst = std::max(worst, f);
    if (object_count(ed) > before) {
      t.landing = f;
      break;
    }
  }
  if (t.landing == 0) t.landing = worst;
  t.frames.add(t.landing);
  t.total = whole.ms();
  t.made = object_count(ed) - std::min(before, object_count(ed));
  return t;
}
/* A synchronous import is one frame that is its landing frame. */
static ImportTrace one_frame_import(Editor &ed, const std::string &line) {
  ImportTrace t;
  const size_t before = object_count(ed);
  ScopedTimer whole;
  const double f = cmd_frame(ed, line);
  t.frames.add(f);
  t.total = whole.ms();
  t.landing = f;
  t.made = object_count(ed) - std::min(before, object_count(ed));
  return t;
}

/* `clip`: a motion makes an animation clip, not objects: it counts as made when the console said so (the objects it made are none). */
static void import_row(Report &rep, const std::string &what, const std::string &size, const ImportTrace &t, bool clip = false) {
  const bool over = t.longest_before > kJob || t.landing > kOnce;
  const bool nothing = !clip && t.made == 0;
  rep.row({what, size, f1(t.total), f1(t.longest_before), f1(t.landing), std::to_string(t.frames.ms.size()), clip ? std::string("a clip") : nothing ? std::string("nothing imported") : size_text(t.made) + " objects",
           nothing || over ? "OVER" : "ok"});
}

static void test_importers(Report &rep, const Options &o) {
  Quiet quiet;
  rep.title("Importers (round 504)",
            "VRM, MMD (PMX and a VMD motion) and Godot (.tscn with its .tres material) imports of files this section writes into the scratch project "
            "(a skinned glTF with the VRM extension, a one-bone PMX grid, a one-bone VMD, a generated scene), by the console's import in a fresh "
            "editor with jobs on workers. Total ms, the longest frame of the whole import, the landing frame (the one the objects arrive in; the "
            "model imports read on a worker; the landing is the frame after the worker says it is done, timed on its own; the longest frame is over the frames before it; the Godot scene and the motion are one call), the frames it took, what it made (nothing: OVER). The budget: frames while the job runs "
            "50 ms, the landing frame 100 ms. A Godot .escn has no import path of its own in the program: it is only read where a scene instances it.");
  rep.table({"import", "size", "total ms", "longest frame before landing ms", "landing frame ms", "frames", "made", "budget"});
  const std::string dir = asset_dir("importers");
  auto model_running = [](Editor &ed) { return [&ed] { return ed.import_job_running_for_test(); }; };
  for (int tris : o.quick ? std::vector<int>{2000} : std::vector<int>{20000, 200000}) {
    const std::string path = fs::join(dir, strprintf("SurveyAvatar%d.vrm", tris));
    fs::write_file(path, vrm_scaled(tris));
    auto ed = fresh(1280, 720, true);
    import_row(rep, "VRM (a skinned glTF, three humanoid bones)", size_text((size_t)tris) + " triangles", trace_import(*ed, "import " + path + " asis", model_running(*ed)));
  }
  for (int verts : o.quick ? std::vector<int>{2000} : std::vector<int>{20000, 200000}) {
    const std::string path = fs::join(dir, strprintf("SurveyModel%d.pmx", verts));
    fs::write_file(path, pmx_scaled(verts));
    auto ed = fresh(1280, 720, true);
    import_row(rep, "MMD model (.pmx)", size_text((size_t)verts) + " vertices", trace_import(*ed, "import " + path + " asis", model_running(*ed)));
    for (int keys : o.quick ? std::vector<int>{300} : std::vector<int>{3000, 30000}) {
      const std::string vmd = fs::join(dir, strprintf("SurveyMotion%d.vmd", keys));
      fs::write_file(vmd, vmd_scaled(keys));
      import_row(rep, "MMD motion (.vmd) onto the model", size_text((size_t)keys) + " keys", one_frame_import(*ed, "import " + vmd), true);
    }
  }
  for (int nodes : o.quick ? std::vector<int>{100} : std::vector<int>{100, 1000, 10000}) {
    const std::string path = godot_scaled(fs::join(dir, "godot"), nodes);
    auto ed = fresh(1280, 720, true);
    import_row(rep, "Godot scene (.tscn with a .tres)", size_text((size_t)nodes) + " nodes", one_frame_import(*ed, "import " + path));
  }
}

/* ===================================================================== unity_export */

static std::string hex_guid(uint32_t a, uint32_t b) { return strprintf("%08x%08x%08x%08x", a, b, a * 2654435761u, b ^ 0x9e3779b9u); }

/* An unpacked Unity package in a folder of the project: a scene of `objects` cubes (a GameObject, its Transform, a MeshFilter and a MeshRenderer each),
 * materials with a texture each, and the .meta files that give every asset its GUID. */
static void write_unity_package(const std::string &folder, int objects, int materials, int textures, size_t &bytes) {
  /* One flat folder: export unitypackage's folder walk keeps a reference to the parent item's path while it adds the children
   * (Editor::export_unity_package's `walk`), which a vector growing under it leaves dangling - a package with sub-folders of more than a few
   * files made it throw bad_alloc here (round 504's sweep lists it). Flat, the walk is safe. */
  fs::make_dirs(folder);
  auto put = [&](const std::string &path, const std::string &text) {
    fs::write_file(path, text);
    bytes += text.size();
  };
  auto meta = [&](const std::string &path, const std::string &guid, bool folder_meta = false) {
    put(path + ".meta", strprintf("fileFormatVersion: 2\nguid: %s\n%s", guid.c_str(), folder_meta ? "folderAsset: yes\n" : ""));
  };
  std::vector<std::string> tex_guid, mat_guid;
  for (int i = 0; i < textures; i++) {
    const std::string p = fs::join(folder, strprintf("T%d.png", i));
    write_test_png(p, 64, 64, (uint32_t)i + 5);
    tex_guid.push_back(hex_guid((uint32_t)i + 1, 0x7e57));
    meta(p, tex_guid.back());
  }
  for (int i = 0; i < materials; i++) {
    const std::string p = fs::join(folder, strprintf("M%d.mat", i));
    mat_guid.push_back(hex_guid((uint32_t)i + 1, 0x4a7));
    put(p, strprintf("%%YAML 1.1\n%%TAG !u! tag:unity3d.com,2011:\n--- !u!21 &2100000\nMaterial:\n  serializedVersion: 8\n  m_Name: M%d\n"
                     "  m_Shader: {fileID: 46, guid: 0000000000000000f000000000000000, type: 0}\n  m_SavedProperties:\n    serializedVersion: 3\n    m_TexEnvs:\n"
                     "    - _MainTex:\n        m_Texture: {fileID: 2800000, guid: %s, type: 3}\n        m_Scale: {x: 1, y: 1}\n        m_Offset: {x: 0, y: 0}\n"
                     "    m_Floats:\n    - _Metallic: 0\n    - _Glossiness: 0.5\n    m_Colors:\n    - _Color: {r: 0.8, g: 0.%d, b: 0.8, a: 1}\n",
                     i, textures ? tex_guid[(size_t)i % tex_guid.size()].c_str() : "00000000000000000000000000000000", i % 10));
    meta(p, mat_guid.back());
  }
  std::string scene = "%YAML 1.1\n%TAG !u! tag:unity3d.com,2011:\n";
  const int side = std::max(1, (int)std::ceil(std::sqrt((double)objects)));
  for (int i = 0; i < objects; i++) {
    const int id = 1000 + i * 4;
    scene += strprintf("--- !u!1 &%d\nGameObject:\n  m_ObjectHideFlags: 0\n  serializedVersion: 6\n  m_Component:\n  - component: {fileID: %d}\n  - component: {fileID: %d}\n"
                       "  - component: {fileID: %d}\n  m_Layer: 0\n  m_Name: Cube%d\n  m_IsActive: 1\n",
                       id, id + 1, id + 2, id + 3, i);
    scene += strprintf("--- !u!4 &%d\nTransform:\n  m_GameObject: {fileID: %d}\n  m_LocalRotation: {x: 0, y: 0, z: 0, w: 1}\n  m_LocalPosition: {x: %d, y: 0, z: %d}\n"
                       "  m_LocalScale: {x: 1, y: 1, z: 1}\n  m_Children: []\n  m_Father: {fileID: 0}\n  m_RootOrder: %d\n",
                       id + 1, id, (i % side) * 2, (i / side) * 2, i);
    scene += strprintf("--- !u!33 &%d\nMeshFilter:\n  m_GameObject: {fileID: %d}\n  m_Mesh: {fileID: 10202, guid: 0000000000000000e000000000000000, type: 0}\n", id + 2, id);
    scene += strprintf("--- !u!23 &%d\nMeshRenderer:\n  m_GameObject: {fileID: %d}\n  m_Enabled: 1\n  m_Materials:\n  - {fileID: 2100000, guid: %s, type: 2}\n", id + 3, id,
                       materials ? mat_guid[(size_t)i % mat_guid.size()].c_str() : "0000000000000000f000000000000000");
  }
  const std::string scene_path = fs::join(folder, "Main.unity");
  put(scene_path, scene);
  meta(scene_path, hex_guid(0x5ce, 0x1));
}

static size_t file_bytes(const std::string &path) {
  std::error_code ec;
  const auto n = std::filesystem::file_size(path, ec);
  return ec ? 0 : (size_t)n;
}

static void test_unity_export(Report &rep, const Options &o) {
  Quiet quiet;
  rep.title("Unity export (round 504)",
            "An unpacked Unity package this section writes under Assets/Imported of the scratch project - a scene of 100, 1,000 and 10,000 cubes "
            "(quick: 100), a material for every ten objects (at most 200) and a texture for every second material - imported, then written back by "
            "export unitypackage (a worker in the app; the whole job inside the call in a headless editor, which cannot be timed otherwise: no verdict), then the package's materials read as text with unity materials. Total "
            "s, the longest frame (the call is one frame here), the package's bytes. A call that blocks for over 100 ms is OVER. The 100-object "
            "package is imported again from the file it was exported to, and its object count compared.");
  rep.table({"step", "objects", "total s", "longest frame ms", "bytes", "budget"});
  const char *proj = std::getenv("BLENDITY_PROJECT");
  size_t problems = 0;
  for (int objects : o.quick ? std::vector<int>{100} : std::vector<int>{100, 1000, 10000}) {
    const std::string name = strprintf("Survey%d", objects);
    const std::string rel = "Assets/Imported/" + name;
    const std::string folder = fs::join(proj ? proj : ".", rel);
    size_t bytes = 0;
    const int materials = std::min(200, std::max(1, objects / 10)), textures = std::max(1, materials / 2);
    write_unity_package(folder, objects, materials, textures, bytes);
    auto ed = fresh(1280, 720, true);
    const std::string scene_file = rel + "/Main.unity";
    const ImportTrace imp = one_frame_import(*ed, "import " + scene_file);
    rep.row({imp.made == 0 ? "import the package's scene (nothing imported)" : "import the package's scene", std::to_string(objects), f2(imp.total / 1000.0), f1(imp.landing), size_text(bytes),
             imp.made == 0 ? "OVER" : once_verdict(imp.landing)});
    problems += imp.made == 0;
    const std::string out = fs::join(fs::join(proj ? proj : ".", "Exports"), name + ".unitypackage");
    ScopedTimer t;
    std::string threw;
    const double first = cmd_frame_guarded(*ed, "export unitypackage " + rel, threw);
    const size_t pkg = file_bytes(out);
    /* A headless editor runs the whole export inside the call; in the app only the scan blocks and the writing is on a worker. Nothing of the app's
     * blocking part can be timed without a product change, so a package that came out has no verdict (-); one that did not, or that threw, is OVER. */
    const bool failed = !threw.empty() || pkg == 0;
    rep.row({!threw.empty() ? "export unitypackage (threw)" : pkg == 0 ? "export unitypackage (no package written)" : "export unitypackage (the whole job, inline)", std::to_string(objects), f2(t.ms() / 1000.0), f1(first),
             size_text(pkg), failed ? "OVER" : "-"});
    problems += failed;
    if (!threw.empty()) rep.note("export unitypackage threw: " + threw + " (a fault in the program, listed in the sweep)");
    ScopedTimer tm;
    const std::string report_file = fs::join(folder, "materials.txt");
    const double mf = cmd_frame(*ed, "unity materials all to \"" + report_file + "\"");
    rep.row({"unity materials (the report)", std::to_string(objects), f2(tm.ms() / 1000.0), f1(mf), size_text(file_bytes(report_file)), once_verdict(mf)});
    if (objects == 100 && pkg > 0) {  // (a re-import of what was exported: the package unpacked again, its scene file there and read by the importer)
      auto ed2 = fresh(1280, 720, true);
      const std::string scene_abs = fs::join(folder, "Main.unity");
      std::error_code ec;
      std::filesystem::remove(scene_abs, ec);  // (so that it is the package that puts it back)
      const ImportTrace again = one_frame_import(*ed2, "import " + out);
      const bool back = fs::exists(scene_abs);
      const ImportTrace scene_again = back ? one_frame_import(*ed2, "import " + scene_file) : ImportTrace();
      const bool same = back && scene_again.made == imp.made;
      rep.row({same ? "import the exported package again (unpacks it)" : "import the exported package again (not the same scene)", std::to_string(objects), f2(again.total / 1000.0), f1(again.landing), size_text(pkg),
               same ? once_verdict(again.landing) : "OVER"});
      problems += !same;
      rep.note(strprintf("Re-import: the scene file %s by the package; imported from there it makes %zu objects against %zu from the original.", back ? "came back" : "did NOT come back", scene_again.made, imp.made));
    }
  }
  rep.note(strprintf("Unity export problems (nothing imported, no package, a throw, a re-import that differs): %zu.", problems));
  rep.note("The package is one flat folder: with sub-folders of a few files each, export unitypackage threw 'bad allocation' while this section was written "
           "(Editor::export_unity_package's walk passes the parent item's pathname by reference while it pushes children into the same vector: a likely use after a reallocation). Listed in the sweep, not fixed here.");
  rep.note("The other Unity exporter, export anim, needs a clip made in the editor: not driven here (a clip is an Animation window's work; export tscn is the Godot scene's).");
}

/* ===================================================================== asset_windows */

static void window_rows(Report &rep, Editor &ed, WindowKind kind, const char *what, const std::string &size, int scrolls, const std::function<void(const std::string &)> &set_search) {
  const std::string title = window_title(kind);
  const double first = cmd_frame(ed, "window " + title);
  const double next = frame(ed);
  rep.table({"first open", "size", "first frame ms", "frame after ms", "budget"});
  rep.row({std::string("open the ") + what + " window", size, f1(first), f1(next), once_verdict(std::max(first, next))});
  const Recti r = ed.window_rect_for_test(kind);
  rep.table(live_header());
  Frames scroll;
  for (int i = 0; i < scrolls; i++) scroll.add(frame(ed, {wheel_event(r.x + r.w / 2, r.y + r.h / 2, i < scrolls / 2 ? -3.0f : 3.0f)}));
  live_row(rep, std::string("scroll the ") + what + " window", size, scroll);
  if (!set_search) return;
  Frames typing;
  const std::string query = "survey 1";
  for (size_t i = 1; i <= query.size(); i++) {
    set_search(query.substr(0, i));
    typing.add(frame(ed));
  }
  set_search("");
  frame(ed);
  live_row(rep, std::string("type a filter in the ") + what + " window (a letter a frame)", size, typing);
}

static void test_asset_windows(Report &rep, const Options &o) {
  Quiet quiet;
  rep.title("Materials, Textures and History windows, command palette (round 504)",
            "10, 200 and 2,000 materials, textures and undo steps (quick: 10): the window's first open (the command and its frame, and the frame after), "
            "scrolling it (a wheel step a frame) and typing a filter (the search text set a letter at a time, one frame each - the text field itself "
            "is not clicked). The command palette: Ctrl+K and each letter typed. Live rows against 16.7 ms (the median, nearest rank, none over 100 ms), "
            "the first open against 100 ms. The editors run their jobs on workers, as the app does. History: a change and a frame a step, 10 and 200 (the "
            "stack's cap, Editor::trim_undo; the size cell is what it held).");
  const std::vector<int> counts = o.quick ? std::vector<int>{10} : std::vector<int>{10, 200, 2000};
  const int scrolls = o.quick ? 10 : 30;
  for (int n : counts) {
    remove_survey_assets();
    auto ed = fresh(1600, 900, true);
    for (int i = 0; i < n; i++) ed->command(strprintf("newmat Survey Mat %d", i));
    ed->step_frame_headless();
    window_rows(rep, *ed, WindowKind::Materials, "Materials", std::to_string(n) + " materials", scrolls, [&](const std::string &s) { ed->set_material_search_for_test(s); });
  }
  for (int n : counts) {
    remove_survey_assets();
    auto ed = fresh(1600, 900, true);
    for (int i = 0; i < n; i++) ed->command(strprintf("newtex Survey%d", i));
    ed->step_frame_headless();
    window_rows(rep, *ed, WindowKind::Textures, "Textures", std::to_string(n) + " textures", scrolls, [&](const std::string &s) { ed->set_texture_search_for_test(s); });
  }
  /* A change is one undo step only when a frame commits it (mark_changed sets it pending), and the stack keeps 200 at most (trim_undo): a frame a change,
   * and the size cell says what the stack held. */
  for (int n : o.quick ? std::vector<int>{10} : std::vector<int>{10, 200}) {
    auto ed = fresh(1600, 900, true);
    for (int i = 0; i < n; i++) {
      ed->mark_changed_for_test(strprintf("Survey step %d", i));
      ed->step_frame_headless();
    }
    const size_t held = ed->undo_steps_for_test();
    window_rows(rep, *ed, WindowKind::History, "History", strprintf("%zu undo steps%s", held, held >= 200 ? " (the cap)" : ""), scrolls, nullptr);
  }
  {
    auto ed = fresh();
    rep.table(live_header());
    Frames open, typing;
    open.add(frame(*ed, {key_event(platform::KEY_K, platform::MOD_CTRL)}));
    for (char c : std::string("unwrap bevel extrude")) typing.add(frame(*ed, {letter(c)}));
    frame(*ed, {key_event(platform::KEY_ESCAPE)});
    ed->step_frame_headless();
    rep.row({"command palette: Ctrl+K (the frame it opens in)", "-", f1(open.median()), f1(open.p95()), f1(open.longest()), "1", once_verdict(open.longest())});
    live_row(rep, "command palette: each letter typed", "20 letters", typing);
  }
}

/* ===================================================================== cold_first_use */

struct ColdRow {
  std::string what;
  double first = 0, after = 0, second = 0;
  int started = -1;  // 1: both uses were seen to start the thing; 0: one was not; -1: there is no way to tell
};

static void cold_row(Report &rep, const ColdRow &r) {
  const double ratio = r.second > 0.0 ? r.first / r.second : 0.0;
  /* A first use over 100 ms, or one that costs over 3 times the second (when it is over 5 ms: below that the ratio is noise). */
  const bool marked = r.first > kOnce || (r.first > 5.0 && ratio > 3.0);
  /* A use that was refused is no fast ok: its row says so and has no verdict; one that can't be told has none either. */
  const std::string mark = r.started == 0 ? "NOT STARTED" : marked ? "cold" : "-";
  rep.row({r.what, f1(r.first), f1(r.after), f1(r.second), strprintf("%.1fx", ratio), mark, r.started == 1 ? once_verdict(r.first) : "-"});
}

/* A thing used twice: `use` runs it (a command and its frame, say) and returns the ms; `reset` puts it back; `started` (when there is a way to tell) says
 * it began, asked after each use. */
static ColdRow cold_use(Editor &ed, const std::string &what, const std::function<double()> &use, const std::function<void()> &reset, const std::function<bool()> &started = nullptr) {
  ColdRow r;
  r.what = what;
  r.first = use();
  bool began = !started || started();
  r.after = frame(ed);
  reset();
  ed.step_frame_headless();
  r.second = use();
  began = began && (!started || started());
  reset();
  ed.step_frame_headless();
  r.started = !started ? -1 : began ? 1 : 0;
  return r;
}

/* Undo back to `base`, at most 64 steps. */
static void undo_to(Editor &ed, size_t base) {
  for (int tries = 0; tries < 64 && ed.undo_steps_for_test() > base; tries++) ed.command("undo");
}

static void test_cold_first_use(Report &rep, const Options &o) {
  Quiet quiet;
  rep.title("Cold first use (round 504)",
            "One fresh editor, nothing warmed before: it is made, put straight into a layout of the Scene view alone (no frame drawn before, so no other window "
            "is built), and the first open of every window in the registry (the command and the frame that draws it, then the frame after it) and the first use "
            "of every tool and mode (Edit Mode and each Edit Mode operator, the draw tool, Sculpt, Paint, Weight Paint, Puppet posing, Play, Blockout's console "
            "tools) are timed, each against the second use of the same thing after it was put back (a window is put back by the Scene-only layout). A row is "
            "marked 'cold' when its first use is over 100 ms, or over 3 times its second (when over 5 ms). Verdict: the first use against 100 ms, only where "
            "the section could see the thing start (a window with a rectangle, a mode on, Edit Mode on, objects made); a use it could not check, and the draw "
            "and Room tools, have '-', a use that did not start says NOT STARTED. Jobs run on workers, as in the app. Pose Mode is Puppet posing here (the "
            "program has no separate Pose Mode).");
  auto ed = std::make_unique<Editor>();
  ed->init_headless(1600, 900);
  ed->set_async_jobs_for_test(true);
  ed->command("layout L(0:0)");  // (before any frame: the Default layout's windows are not built)
  ed->step_frame_headless();
  ed->step_frame_headless();
  rep.table({"first use", "first ms", "frame after ms", "second ms", "first / second", "marked", "budget"});
  /* Windows: the registry's own list; closed again by a layout of the Scene view alone. */
  int count = 0;
  const Module *mods = modules(count);
  for (int i = 0; i < count; i++) {
    const WindowKind kind = mods[i].kind;
    if (kind == WindowKind::Scene) continue;
    const std::string title = window_title(kind);
    cold_row(rep, cold_use(*ed, std::string("window: ") + title, [&] { return cmd_frame(*ed, "window " + title); }, [&] { ed->command("layout L(0:0)"); },
                           [&] { return ed->window_rect_for_test(kind).w > 0; }));
  }
  ed->command("layout L(0:0)");
  ed->step_frame_headless();
  ed->command("select Cube");
  ed->command("delete");
  ed->command("create Sphere");
  GameObject *sphere = ed->active_object_for_test();
  const std::string sphere_name = sphere ? sphere->name : "Sphere";
  for (int i = 0; i < 400 && ed->scene_camera_for_test().animating; i++) ed->step_frame_headless();
  /* Edit Mode and each of its operators. */
  cold_row(rep, cold_use(*ed, "Edit Mode", [&] { return cmd_frame(*ed, "edit"); }, [&] { ed->command("edit off"); }, [&] { return ed->in_edit_mode(); }));
  const size_t ops = edit_op_table().size();
  const size_t limit = o.quick ? std::min<size_t>(ops, 12) : ops;
  for (size_t i = 0; i < limit; i++) {
    const EditOpInfo &op = edit_op_table()[i];
    const std::string name = op.op;
    const char *mode = (op.elements & 1) ? "vertex" : (op.elements & 2) ? "edge" : "face";
    const size_t base = ed->undo_steps_for_test();
    auto use = [&] {
      ed->command(std::string("edit ") + mode + " all");
      ed->step_frame_headless();
      return cmd_frame(*ed, "edittool " + name);
    };
    auto reset = [&] {
      ed->step_frame_headless({key_event(platform::KEY_ESCAPE)});
      undo_to(*ed, base);
    };
    cold_row(rep, cold_use(*ed, std::string("Edit Mode operator: ") + op.label, use, reset, [&] { return ed->in_edit_mode(); }));
  }
  ed->command("edit off");
  ed->step_frame_headless();
  cold_row(rep, cold_use(*ed, "draw tool (rectangle)", [&] { return cmd_frame(*ed, "draw rectangle"); }, [&] { ed->step_frame_headless({key_event(platform::KEY_ESCAPE)}); }));
  cold_row(rep, cold_use(*ed, "Sculpt Mode", [&] { return cmd_frame(*ed, "sculpt on"); }, [&] { ed->command("sculpt off"); }, [&] { return ed->sculpt_for_test(); }));
  /* Paint Mode needs the object's mesh maps and a surface; the cold use of "paint on" is without them (it starts, or says it can't). */
  cold_row(rep, cold_use(*ed, "Paint Mode", [&] { return cmd_frame(*ed, "paint on"); }, [&] { ed->command("paint off"); }, [&] { return ed->paint_for_test(); }));
  /* Weight Paint and Puppet posing need a rig: an armature with a bone, the sphere selected again. */
  ed->command("create Armature");
  ed->command("bone add Bone 1");
  ed->command("select " + sphere_name);
  ed->step_frame_headless();
  cold_row(rep, cold_use(*ed, "Weight Paint Mode", [&] { return cmd_frame(*ed, "weightpaint on"); }, [&] { ed->command("weightpaint off"); }, [&] { return ed->weight_paint_active_for_test(); }));
  cold_row(rep, cold_use(*ed, "Puppet posing (autopose)", [&] { return cmd_frame(*ed, "autopose on"); }, [&] { ed->command("autopose off"); }, [&] { return ed->autopose_on_for_test(); }));
  cold_row(rep, cold_use(*ed, "Play mode", [&] { return cmd_frame(*ed, "play"); }, [&] { ed->command("stop"); }, [&] { return ed->playing_for_test(); }));
  /* Blockout's tools, by their console commands only. */
  {
    const size_t base = ed->undo_steps_for_test();
    cold_row(rep, cold_use(*ed, "Blockout: new", [&] { return cmd_frame(*ed, "blockout new"); }, [&] { undo_to(*ed, base); }, [&] { return ed->undo_steps_for_test() > base; }));
  }
  ed->command("blockout new");
  ed->step_frame_headless();
  for (const char *add : {"box", "room", "stairs"}) {
    const size_t base = ed->undo_steps_for_test();
    cold_row(rep, cold_use(*ed, std::string("Blockout: add ") + add, [&] { return cmd_frame(*ed, std::string("blockout add ") + add); },
                           [&] {
                             ed->command("blockout wait");
                             undo_to(*ed, base);
                           },
                           [&] { return ed->undo_steps_for_test() > base; }));
  }
  cold_row(rep, cold_use(*ed, "Blockout: Room tool", [&] { return cmd_frame(*ed, "tool room"); }, [&] { ed->command("tool move"); }));
  rep.note("Rows are first-use costs in one process: tools used before in this section (a window the Scene view's layout shares, a codec) warm later ones, so a row's first use is the first in the order listed.");
}

}  // namespace survey

static void test_image_edit(Report &rep, const Options &o) { survey::test_image_edit(rep, o); }
static void test_sculpt_paint(Report &rep, const Options &o) { survey::test_sculpt_paint(rep, o); }
static void test_sequencer(Report &rep, const Options &o) { survey::test_sequencer(rep, o); }
static void test_importers(Report &rep, const Options &o) { survey::test_importers(rep, o); }
static void test_unity_export(Report &rep, const Options &o) { survey::test_unity_export(rep, o); }
static void test_asset_windows(Report &rep, const Options &o) { survey::test_asset_windows(rep, o); }
static void test_cold_first_use(Report &rep, const Options &o) { survey::test_cold_first_use(rep, o); }

/* The survey's sections are minutes long: a run without --only leaves them out (tools/perf_suite.py names them). */
static bool survey_named_only(const char *name) {
  for (const char *n : {"image_edit", "sculpt_paint", "sequencer", "importers", "unity_export", "asset_windows", "cold_first_use"})
    if (std::string(name) == n) return true;
  return false;
}

// SPDX-License-Identifier: GPL-2.0-or-later
// The stress section `posing` (round 468, task 0444): what a frame costs while an avatar is posed, scrubbed and played -
// the stand-in avatar alone, a world alone, the avatar in the world with its lighting baked - with the views on the CPU
// and on the card, each row driven by real events through `perf drag` (src/editor/perf_script.cpp). Included by
// stress_main.cpp after test_world_like; it is named-only (minutes), like `responsiveness`.
//
// A row's verdict (the last cell): `-` for the CPU rows; on the card, OVER when a drag, scrub or play row's mean is over 16.7
// ms or its press, first-move or release frame is over 100 ms (ADR 0105), or an idle / orbit row's longest frame is; `NO VIEW`
// when a drag, scrub or play row's frames drew no view at all (a layout that hides the Scene view measures nothing).

namespace posing_stress {

constexpr double kBudgetMs = 16.7, kCapMs = 100.0;
/* The Scene view alone above the Animation window (the built-in "Animation" workspace also shows the Game view). */
const char *const kSceneOnlyLayout = "layout H(0.74,V(0.6,L(0:0),L(16,17,4,5:0)),L(3,15,2:0))";

struct PartsMs {
  double constraints = 0, skin_keys = 0, skinning = 0, shape_keys = 0, in_place = 0, outlines = 0, uploads = 0, hashes = 0, draw_items = 0, instances = 0, overlays = 0, windows = 0;
};

struct Sample {
  bool ok = true;
  std::string error, notes;
  double mean = 0, longest = 0, press = -1, first = -1, release = -1;
  double views = 0;  // views drawn over the measured frames (the card's and the CPU's)
  double shown = 0;  // ...and the frames' views reused from the cache: a view was on screen (round 468, test engineer: see verdict)
  PartsMs parts;
};

/* Plain: idle and orbit rows (the longest frame against 100 ms). Rate: drag, scrub and play rows (the mean against 16.7 ms). */
enum class Kind { Plain, Rate };

static size_t log_mark() {
  std::vector<LogEntry> skipped;
  return Log::fetch(0, skipped);
}
static std::string log_find(size_t at, const char *prefix) {
  std::vector<LogEntry> said;
  Log::fetch(at, said);
  for (const LogEntry &e : said)
    if (e.text.rfind(prefix, 0) == 0) return e.text;
  return std::string();
}

static PartsMs parts_between(const FrameProfile &a, const FrameProfile &b, const PoseCounts &pa, const PoseCounts &pb, const MeshCounts &ma, const MeshCounts &mb, double n) {
  const PoseCounts p = pb - pa;
  const MeshCounts m = mb - ma;
  PartsMs r;
  r.constraints = p.ms_constraints / n, r.skin_keys = p.ms_skin_keys / n, r.skinning = p.ms_skin_deforms / n;
  r.shape_keys = (p.ms_shape_hashes + p.ms_shape_applies) / n, r.in_place = m.ms_in_place / n, r.outlines = p.ms_outline_shells / n;
  r.uploads = (b.ms_mesh_uploads - a.ms_mesh_uploads) / n, r.hashes = (b.ms_hash - a.ms_hash) / n, r.draw_items = (b.ms_collect - a.ms_collect) / n;
  r.instances = (b.ms_gpu_instances - a.ms_gpu_instances) / n, r.overlays = (b.ms_overlays - a.ms_overlays) / n;
  for (int k = 0; k < 32; k++) r.windows += (b.ms_window[k] - a.ms_window[k]) / n;
  return r;
}

static std::string verdict(const Sample &s, Kind kind, bool card) {
  /* A short window can draw no new picture (a playing clip in a world on the CPU: one render of 8 frames, sometimes none) while the Scene view
   * is on screen all the while, its picture reused; NO VIEW is for a layout that shows no 3D view, so a reused picture counts too. */
  if (kind == Kind::Rate && s.views + s.shown <= 0.0) return "NO VIEW";
  if (!card) return "-";
  const bool over = kind == Kind::Plain ? s.longest > kCapMs : (s.mean > kBudgetMs || s.press > kCapMs || s.first > kCapMs || s.release > kCapMs);
  return over ? "OVER" : "ok";
}

static std::string ms_or_dash(double v) { return v < 0.0 ? std::string("-") : f1(v); }

static const std::vector<std::string> &row_header() {
  static const std::vector<std::string> h = {"row", "ms / frame", "longest ms", "press ms", "first move ms", "release ms", "constraints", "skin keys", "skinning", "shape keys", "in place",
                                             "outlines", "mesh uploads", "scene hashes", "draw items", "instances", "overlays", "windows", "budget"};
  return h;
}

struct Bench {
  Report &rep;
  const Options &o;
  int n;
  std::map<std::string, double> mean;  // "setup/device/what" -> a row's mean ms (for "does the world matter")
  std::vector<std::string> left_out;   // settings with no console switch
  Bench(Report &r, const Options &opt) : rep(r), o(opt), n(opt.quick ? 8 : 30) {}

  std::string scene(const char *base) const { return std::string("benchscene ") + base + (o.quick ? "quick" : ""); }

  std::unique_ptr<Editor> fresh(const std::string &scene_cmd) {
    auto ed = std::make_unique<Editor>();
    ed->init_headless(1600, 1000);
    ed->set_async_jobs_for_test(true);
    ed->command(scene_cmd);
    for (int i = 0; i < 3; i++) ed->step_frame_headless();
    return ed;
  }

  /* The views on the card: false (and the status) when there is none. */
  bool to_card(Editor &ed, std::string &status) {
    ed.viewport_device_for_test(2);
    ScopedTimer t;
    while (t.ms() < 30000.0) {
      ed.step_frame_headless();
      status = ed.gpu_view_status();
      if (status.rfind("GPU:", 0) == 0) return true;
      if (status.rfind("CPU: ", 0) == 0) return false;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
  }

  /* `camera yaw pitch distance px py pz`: in front of the avatar standing at `at`, its middle the pivot. */
  void cam(Editor &ed, Vec3 at, float dist = 2.8f, float height = 0.9f) { ed.command(strprintf("camera 0 10 %g %g %g %g", dist, at.x, at.y + height, at.z)); }
  void steps(Editor &ed, int k) {
    for (int i = 0; i < k; i++) ed.step_frame_headless();
  }

  /* ------------------------------------------------------------- samples */

  /* `pace` > 0: each frame is followed by a wait to that many ms (outside its timing) - a clip plays by the real clock, so frames
   * stepped back to back would hardly advance it; a window's frames are 16.7 ms apart. */
  Sample frames_sample(Editor &ed, const std::function<void(int)> &step, double pace = 0.0) {
    for (int i = 0; i < 2; i++) step(-1);
    const FrameProfile p0 = ed.frame_profile_totals();
    const PoseCounts c0 = pose_counts();
    const MeshCounts m0 = mesh_counts();
    FrameProfile acc;
    double sum = 0;
    Sample s;
    for (int i = 0; i < n; i++) {
      ScopedTimer f;
      step(i);
      const double ms = f.ms();
      sum += ms, s.longest = std::max(s.longest, ms);
      acc.add(ed.frame_profile());
      if (pace > ms) std::this_thread::sleep_for(std::chrono::microseconds((long long)((pace - ms) * 1000.0)));
    }
    const FrameProfile p1 = ed.frame_profile_totals();
    const PoseCounts c1 = pose_counts();
    const MeshCounts m1 = mesh_counts();
    s.mean = sum / n;
    s.views = (double)(p1.view_renders - p0.view_renders + p1.gpu_views - p0.gpu_views);
    s.shown = (double)(p1.view_cache_hits - p0.view_cache_hits);
    s.parts = parts_between(p0, p1, c0, c1, m0, m1, n);
    s.notes = strprintf("view renders %.0f over %d frames; parts ms (a frame's mean; nested, not to add up): %s; counts (a frame's mean): %s", s.views, n,
                        Editor::posing_parts_text(acc, c1 - c0, m1 - m0, n).c_str(), Editor::posing_counts_text(acc, c1 - c0, m1 - m0, n).c_str());
    return s;
  }

  /* `perf drag ...` played at once, then its undo step undone; the numbers are the script's own. */
  Sample drag_sample(Editor &ed, const std::string &cmd) {
    Sample s;
    const size_t at = log_mark();
    const uint64_t undo0 = ed.frame_profile_totals().undo_steps;
    ed.command(cmd + strprintf(" frames %d", n));
    const PerfDragResult &r = ed.perf_drag_result_for_test();
    if (!r.ok) {
      s.ok = false, s.error = r.error.empty() ? "the script did not finish" : r.error;
      return s;
    }
    s.mean = r.ms_mean, s.longest = r.ms_longest, s.press = r.ms_press, s.first = r.ms_first, s.release = r.ms_release;
    s.views = (double)(r.after.view_renders - r.before.view_renders + r.after.gpu_views - r.before.gpu_views);
    s.shown = (double)(r.after.view_cache_hits - r.before.view_cache_hits);
    s.parts = parts_between(r.before, r.after, r.pose_before, r.pose_after, r.mesh_before, r.mesh_after, n);
    const std::string head = log_find(at, "perf drag ");
    const size_t prior = head.find("; the pose prior");
    s.notes = strprintf("view renders %.0f over %d frames; %s; %s%s", s.views, n, log_find(at, "perf drag parts ms").c_str(), log_find(at, "perf drag counts").c_str(),
                        prior == std::string::npos ? "" : (" (" + head.substr(prior + 2) + ")").c_str());
    const uint64_t made = ed.frame_profile_totals().undo_steps - undo0;
    for (uint64_t i = 0; i < made; i++) ed.command("undo");
    steps(ed, 2);
    return s;
  }

  void emit(const std::string &name, const Sample &s, Kind kind, bool card) {
    if (!s.ok) {
      std::vector<std::string> cells(19, "-");
      cells[0] = name, cells[18] = "not run";
      rep.row(cells);
      rep.note(name + ": not run - " + s.error);
      return;
    }
    const PartsMs &p = s.parts;
    rep.row({name, f2(s.mean), f1(s.longest), ms_or_dash(s.press), ms_or_dash(s.first), ms_or_dash(s.release), f2(p.constraints), f2(p.skin_keys), f2(p.skinning), f2(p.shape_keys), f2(p.in_place),
             f2(p.outlines), f2(p.uploads), f2(p.hashes), f2(p.draw_items), f2(p.instances), f2(p.overlays), f2(p.windows), verdict(s, kind, card)});
    rep.note(name + ": " + s.notes);
  }

  /* ------------------------------------------------------------- the base rows */

  void idle_orbit_rows(Editor &ed, const std::string &prefix, const std::string &key, bool card, double dist) {
    Sample idle = frames_sample(ed, [&](int) { ed.step_frame_headless(); });
    emit(prefix + "idle", idle, Kind::Plain, card);
    mean[key + "idle"] = idle.mean;
    Sample orbit = frames_sample(ed, [&](int k) {
      ed.command(strprintf("camera %d 15 %g", 20 + k * 3, dist));
      ed.step_frame_headless();
    });
    emit(prefix + "orbiting the Scene view", orbit, Kind::Plain, card);
    mean[key + "orbit"] = orbit.mean;
  }

  void drag_row(Editor &ed, const std::string &prefix, const std::string &key, const char *tag, const char *what, bool card, const std::vector<std::string> &before, const std::string &drag,
                const std::vector<std::string> &after = {}) {
    for (const std::string &c : before) ed.command(c);
    steps(ed, 3);
    const Sample s = drag_sample(ed, drag);
    for (const std::string &c : after) ed.command(c);
    steps(ed, 2);
    emit(prefix + what, s, Kind::Rate, card);
    if (s.ok) mean[key + tag] = s.mean;
  }

  void avatar_drag_rows(Editor &ed, const std::string &prefix, const std::string &key, bool card, Vec3 at) {
    cam(ed, at);
    drag_row(ed, prefix, key, "fk", "the Rotate gizmo on the chest (FK)", card, {"select AvatarLike", "tool rotate"}, "perf drag gizmo rz 60 part chest");
    drag_row(ed, prefix, key, "ik", "the Move gizmo on the left hand's IK target", card, {"select LeftHandIK", "tool move"}, "perf drag gizmo x 80");
    /* (A pole turns nothing while its limb is straight: the hand's target is pulled in first - a short drag that stays, undone
     * after the row - and the pole is moved up, which swings the bent elbow.) */
    drag_row(ed, prefix, key, "pole", "the Move gizmo on the left elbow's pole (the arm bent first)", card,
             {"select LeftHandIK", "tool move", "perf drag gizmo x 80 frames 4", "select LeftElbowPole", "tool move"}, "perf drag gizmo y 80", {"undo"});
    drag_row(ed, prefix, key, "puppet", "a Puppet handle (the left hand, AutoPosing on)", card, {"assist on physics", "select AvatarLike", "autopose on"}, "perf drag puppet lhand 40 30", {"autopose off"});
    drag_row(ed, prefix, key, "keyg", "G on the left hand's IK target", card, {"select LeftHandIK"}, "perf drag key g 40 20");
    drag_row(ed, prefix, key, "keyr", "R on the chest (the Blender keymap)", card, {"keymap Blender", "perf pick chest"}, "perf drag key r 40 20", {"keymap Unity"});
  }

  /* Scrub and play rows need a layout that draws the Scene view: the built-in Animation workspace (Scene + Game view above the
   * Animation window), or the raw dock string with the Scene view alone. Rows say which they ran in. */
  void anim_rows(Editor &ed, const std::string &prefix, const std::string &key, bool card, Vec3 at) {
    for (int game = 1; game >= 0; game--) {
      const char *shown = game ? ", Scene + Game view" : ", Scene view only";
      ed.command(game ? "layout Animation" : kSceneOnlyLayout);
      steps(ed, 4);
      cam(ed, at);
      ed.command("select AvatarLike");
      steps(ed, 3);
      Sample scrub = drag_sample(ed, "perf drag ruler 0 40");
      emit(prefix + "a scrub on the Animation window's ruler" + shown, scrub, Kind::Rate, card);
      if (scrub.ok) mean[key + (game ? "scrub" : "scrub_scene")] = scrub.mean;
      ed.command("anim frame 0"), ed.command("anim preview off");
      steps(ed, 2);
      ed.command("anim play");
      Sample play = frames_sample(ed, [&](int) { ed.step_frame_headless(); }, kBudgetMs);
      ed.command("anim stop"), ed.command("anim frame 0"), ed.command("anim preview off");
      steps(ed, 2);
      emit(prefix + "the clip playing" + shown, play, Kind::Rate, card);
      mean[key + (game ? "play" : "play_scene")] = play.mean;
    }
    play_row(ed, prefix, key, card);
    ed.command("layout Default");
    steps(ed, 3);
  }

  /* Play with the springs: the game view docks itself; the note says how many chains stepped. */
  void play_row(Editor &ed, const std::string &prefix, const std::string &key, bool card) {
    ed.command("assist on physics");
    ed.command("select AvatarLike");
    ed.command("play");
    const PoseCounts c0 = pose_counts();
    Sample s = frames_sample(ed, [&](int) { ed.step_frame_headless(); }, kBudgetMs);
    const PoseCounts c1 = pose_counts();
    ed.command("stop");
    steps(ed, 2);
    s.notes += strprintf("; %llu spring steps stepped %llu chains (%.1f chains a frame)", (unsigned long long)(c1.spring_steps - c0.spring_steps), (unsigned long long)(c1.spring_chains - c0.spring_chains),
                         (double)(c1.spring_chains - c0.spring_chains) / (n + 2));
    emit(prefix + "Play with the springs", s, Kind::Rate, card);
    mean[key + "playmode"] = s.mean;
  }

  void world_rows(Editor &ed, const std::string &prefix, const std::string &key, bool card) {
    idle_orbit_rows(ed, prefix, key, card, 30.0);
    cam(ed, Vec3(-33.0f, 0.0f, -12.0f), 7.0f, 0.0f);  // (Part 000's corner of the world)
    drag_row(ed, prefix, key, "prop", "a prop moved with the Move gizmo", card, {"select Part 000", "tool move"}, "perf drag gizmo x 80");
  }

  void setup_rows(Editor &ed, const std::string &prefix, const std::string &key, bool card, bool avatar, bool world, Vec3 at) {
    if (world) world_rows(ed, prefix, key, card);
    if (!avatar) return;
    if (!world) idle_orbit_rows(ed, prefix, key, card, 3.5);
    else cam(ed, at);
    avatar_drag_rows(ed, prefix, key, card, at);
    anim_rows(ed, prefix, key, card, at);
  }

  /* ------------------------------------------------------------- the three setups */

  void bake(Editor &ed, const std::string &label) {
    ed.scene().lighting.texels_per_unit = 2, ed.scene().lighting.indirect_samples = o.quick ? 4 : 16;
    const StallRun r = run_stall(ed, [&] { ed.command("bake start"); }, [&] { return ed.baking_for_test(); }, 900000.0);
    rep.note(strprintf("%s: its lighting baked first (180 parts at 2 texels/m): %.1f s over %d frames, the longest %.0f ms", label.c_str(), r.total / 1000.0, r.frames, r.longest));
  }

  void count_note(Editor &ed) {
    size_t bones = 0, skinned = 0, plain = 0, tris = 0, springs = 0, constraints = 0, outlines = 0, keys = 0;
    ed.scene().for_each([&](GameObject &g) {
      bones += g.get<Bone>() ? 1 : 0, springs += g.get<SpringBone>() ? 1 : 0, constraints += g.get<VRCConstraintComponent>() ? 1 : 0, outlines += g.get<Outline>() ? 1 : 0;
      const MeshFilter *mf = g.get<MeshFilter>();
      if (!mf || !mf->mesh || !g.parent || g.parent->name != "AvatarLike") return;
      (g.get<SkinModifier>() ? skinned : plain)++;
      keys += mf->mesh->shape_keys.size();
      for (size_t f = 0; f < mf->mesh->face_count(); f++) tris += mf->mesh->face_size(f) - 2;
    });
    rep.note(strprintf("The stand-in avatar: %zu bones, %zu skinned meshes and %zu plain, %zu triangles, %zu spring chains, %zu rotation constraints, %zu outlines, %zu shape keys (full: 270-300 bones, 15 + 1 meshes, 133,000-147,000 triangles, 60 chains, 28 constraints).",
                       bones, skinned, plain, tris, springs, constraints, outlines, keys));
  }

  void run_setup(const char *label, const char *scene_base, bool bake_it, bool avatar, bool world, Vec3 at, bool settings) {
    auto ed = fresh(scene(scene_base));
    if (bake_it) bake(*ed, label);
    if (avatar) count_note(*ed);
    bool card_ok = true;
    std::string status;
    for (int device = 0; device < 2; device++) {
      const bool card = device == 1;
      if (card && !(card_ok = to_card(*ed, status))) {
        rep.note(strprintf("%s: no card here (%s): the card rows are left out.", label, status.c_str()));
        break;
      }
      if (!card) ed->viewport_device_for_test(1);
      steps(*ed, 4);
      const std::string dev = card ? "card" : "CPU";
      setup_rows(*ed, std::string(label) + ", " + dev + ": ", std::string(label) + "/" + dev + "/", card, avatar, world, at);
    }
    if (settings) settings_rows(*ed, card_ok, at);
  }

  /* ------------------------------------------------------------- the settings */

  struct Setting {
    const char *name;
    bool drag = false, play = false, scrub = false;
    std::string drag_extra;
    std::function<void(Editor &)> on, off;
    std::function<std::string(Editor &)> note;
  };

  std::vector<Setting> settings_list(std::unique_ptr<LinkReader> &reader) {
    auto cmds = [](std::vector<std::string> list) { return [list](Editor &ed) { for (const std::string &c : list) ed.command(c); }; };
    std::vector<Setting> v;
    auto add = [&](const char *name, bool d, bool p, bool s, std::function<void(Editor &)> on, std::function<void(Editor &)> off) {
      Setting x;
      x.name = name, x.drag = d, x.play = p, x.scrub = s, x.on = std::move(on), x.off = std::move(off);
      v.push_back(std::move(x));
    };
    add("Record on", true, false, false, cmds({"anim record on"}), cmds({"anim record off"}));
    add("Preview off", true, false, false, cmds({"anim preview off"}), cmds({"anim preview on", "anim preview off"}));
    add("the Game view beside the Scene view", true, true, false, cmds({"layout 2 by 3"}), cmds({"layout Default"}));
    add("the Game view beside the Scene view and Other Views Follow Drags on", true, false, false, cmds({"layout 2 by 3", "views follow on"}), cmds({"views follow off", "layout Default"}));
    add("trails, onion skins and the zoetrope (Record on)", true, false, true, cmds({"anim record on", "anim trails on", "anim onion 2", "anim zoetrope on"}),
        cmds({"anim zoetrope off", "anim onion off", "anim trails off", "anim record off"}));
    add("Centre of Mass shown", true, true, false, cmds({"assist on physics", "body show on"}), cmds({"body show off"}));
    add("the Avatar window open", true, true, false, cmds({"layout H(0.74,V(0.6,L(0:0),H(0.6,L(16,17,4,5:0),L(25:0))),L(3,15,2:0))"}), cmds({"layout Default"}));  // (the Avatar window in a leaf of its own, beside the Animation window: as a tab it would hide the Scene view)
    add("the Inspector on the Armature", false, true, false, cmds({"select AvatarRig"}), cmds({"select AvatarLike"}));
    add("the Inspector on a skinned mesh", false, true, false, cmds({"select HeadMesh"}), cmds({"select AvatarLike"}));
    add("the Rigging window open (its Walk section)", true, true, false, cmds({"assist on walk", "layout H(0.74,V(0.6,L(0:0),L(16,17,4,5:0)),V(0.5,L(3,2:0),L(15:0)))"}), cmds({"layout Default"}));  // (the Rigging window in a leaf of its own)
    for (const char *mode : {"automatic", "manual"}) {
      Setting x;
      x.name = strcmp(mode, "automatic") == 0 ? "Realtime GI on, Lighting Updates Automatic" : "Realtime GI on, Lighting Updates Manual";
      x.drag = true;
      const std::string m = mode;
      x.on = cmds({"set Lighting.RealtimeGI true", "lighting mode " + m});
      x.off = cmds({"set Lighting.RealtimeGI false", "lighting mode manual"});
      x.note = [](Editor &ed) {
        std::string why;
        const bool live = ed.live_gi_needed_for_test(&why);
        return strprintf("%s; Live GI %s; the lightmaps %s", why.c_str(), live ? "ran" : "did not run", ed.lighting_data_for_test().empty() ? "were dropped" : "stayed attached");
      };
      v.push_back(std::move(x));
    }
    Setting link;
    link.name = "Live Link with a client", link.drag = link.play = true;
    link.on = [&reader](Editor &ed) {
      ed.livelink_start(0);
      livelink::Link *l = ed.livelink_for_test();
      reader = std::make_unique<LinkReader>();
      if (l && l->running()) reader->open(l->port());
      for (int i = 0; i < 30; i++) ed.step_frame_headless();
      if (l) l->wait_meshes_for_test();
    };
    link.off = [&reader](Editor &ed) {
      if (reader) reader->close();
      reader.reset();
      ed.command("livelink stop");
    };
    v.push_back(std::move(link));
    Setting snap;
    snap.name = "surface snapping (Ctrl+Shift held)", snap.drag = true, snap.drag_extra = " mods ctrl+shift", snap.on = snap.off = cmds({});
    v.push_back(std::move(snap));
    add("an F12 Live Preview", true, false, false, cmds({"set Render.LivePreview true"}), cmds({"set Render.LivePreview false"}));
    add("a path-traced view open (Camera Preview rendered, locked to the Main Camera)", true, false, false, cmds({"select Main Camera", "camerapreview rendered", "camerapreview lock"}),
        cmds({"camerapreview unlock", "camerapreview shaded"}));
    return v;
  }

  void settings_rows(Editor &ed, bool card, Vec3 at) {
    const std::string prefix = std::string("in the world, ") + (card ? "card" : "CPU") + ", ";
    rep.table(row_header());
    std::unique_ptr<LinkReader> reader;
    for (const Setting &s : settings_list(reader)) {
      for (int mode = 0; mode < 3; mode++) {
        if (!(mode == 0 ? s.drag : mode == 1 ? s.play : s.scrub)) continue;
        ed.command(mode == 0 ? "layout Default" : mode == 1 ? kSceneOnlyLayout : "layout Animation");
        steps(ed, 4);
        cam(ed, at);
        ed.command("select AvatarLike");
        s.on(ed);
        steps(ed, 3);
        const std::string row = prefix + s.name + (mode == 0 ? ": the IK-target drag" : mode == 1 ? ": the clip playing (the Scene view alone unless the setting adds a view)" : ": a scrub");
        if (mode == 0) {
          ed.command("select LeftHandIK"), ed.command("tool move");
          steps(ed, 2);
          emit(row, drag_sample(ed, "perf drag gizmo x 80" + s.drag_extra), Kind::Rate, card);
        }
        else if (mode == 1) {
          ed.command("anim play");
          const Sample p = frames_sample(ed, [&](int) { ed.step_frame_headless(); }, kBudgetMs);
          ed.command("anim stop"), ed.command("anim frame 0"), ed.command("anim preview off");
          emit(row, p, Kind::Rate, card);
        }
        else emit(row, drag_sample(ed, "perf drag ruler 0 40"), Kind::Rate, card);
        if (s.note) rep.note(std::string(s.name) + ": " + s.note(ed));
        if (s.name == std::string("the Avatar window open")) rep.note("The Avatar window re-checks the avatar twice a second: read its row's longest frame.");
        s.off(ed);
        steps(ed, 2);
      }
    }
    ed.command("layout Default");
    rep.note("Every setting of the brief has a console switch; none was left out. (Surface snapping is the Ctrl+Shift held through `perf drag ... mods`; a path-traced view is Camera Preview rendered, locked.)");
  }

  /* ------------------------------------------------------------- cold first use */

  void cold_rows(Vec3 at) {
    rep.table({"first use", "ms", "budget ms", "budget"});
    auto row = [&](const std::string &name, double ms) { rep.row({name, f1(ms), f1(kCapMs), ms > kCapMs ? "OVER" : "ok"}); };
    std::string status;
    auto cold = [&]() {
      auto ed = fresh(scene("avatarlike"));
      if (!to_card(*ed, status)) ed->viewport_device_for_test(1);
      steps(*ed, 3);
      cam(*ed, at);
      return ed;
    };
    {
      auto ed = cold();
      GameObject *chest = ed->scene().find_by_name("Chest");
      for (int k = 0; k < 2 && chest; k++) {
        chest->set_local_rotation(Quat::axis_angle({0, 0, 1}, 0.2f + 0.1f * k));
        ScopedTimer t;
        ed->step_frame_headless();
        row(k ? "the second posed frame (one bone turned, one frame)" : "the first posed frame after the avatar is built (one bone turned, one frame)", t.ms());
      }
    }
    {
      auto ed = cold();
      ed->command("assist on physics"), ed->command("select AvatarLike"), ed->command("autopose on");
      steps(*ed, 3);
      for (int k = 0; k < 2; k++) {
        const Sample s = drag_sample(*ed, "perf drag puppet lhand 40 30");
        if (!s.ok) {
          rep.note("The Puppet drag was refused: " + s.error);
          break;
        }
        /* (The drag itself begins once the mouse has moved a few pixels - the body model, the rig analysis and the pose prior
         *  are that frame's - which is the first move frame of a short drag and a later one of a long drag: the longest frame
         *  holds it either way.) */
        row(k ? "the second Puppet drag, its press frame" : "the first Puppet drag, its press frame", s.press);
        row(k ? "the second Puppet drag, its first move frame" : "the first Puppet drag, its first move frame", s.first);
        row(k ? "the second Puppet drag, its longest frame" : "the first Puppet drag, its longest frame (the body model, the rig analysis, the pose prior)", std::max(s.first, s.longest));
        rep.note(s.notes);
      }
    }
    for (const char *what : {"anim preview on", "anim record on"}) {
      auto ed = cold();
      ed->command("layout Animation");
      steps(*ed, 4);
      ed->command("select AvatarLike");
      steps(*ed, 2);
      ScopedTimer t;
      ed->command(what);
      ed->step_frame_headless();
      row(std::string("the first ") + (what[5] == 'p' ? "Preview on" : "Record on") + " (the command and its frame)", t.ms());
    }
    {
      auto ed = cold();
      ed->command("select AvatarLike");
      ScopedTimer t;
      ed->command("play");
      ed->step_frame_headless();
      row("entering Play (the command and its frame)", t.ms());
      steps(*ed, 3);
      ScopedTimer t2;
      ed->command("stop");
      ed->step_frame_headless();
      row("leaving Play (the command and its frame)", t2.ms());
    }
  }

  /* ------------------------------------------------------------- the assistants */

  void stall_rows() {
    rep.table({"operation", "longest frame ms", "mean frame ms", "total s", "frames", "in the tray", "budget"});
    auto ed = fresh(scene("avatarlike"));
    std::string status;
    if (!to_card(*ed, status)) ed->viewport_device_for_test(1);
    steps(*ed, 3);
    cam(*ed, Vec3(0.0f));
    auto one = [&](const char *what, const std::string &cmd, double budget, const std::function<bool()> &running = [] { return false; }) {
      ed->command("select AvatarRig");
      steps(*ed, 2);
      const uint64_t undo0 = ed->frame_profile_totals().undo_steps;
      const size_t at = log_mark();
      stall_row(rep, what, run_stall(*ed, [&] { ed->command(cmd); }, running, 120000.0), budget);
      const std::string said = log_find(at, "Walk Cycle:");
      if (!said.empty()) rep.note(said);
      for (uint64_t i = ed->frame_profile_totals().undo_steps; i > undo0; i--) ed->command("undo");
      steps(*ed, 2);
    };
    ed->command("assist on walk"), ed->command("assist on physics");
    one("Walk Cycle (walk cycle)", "walk cycle", 100.0);
    ed->command("rig humanoid");
    one("Retarget the clip onto a second rig (anim retarget Humanoid)", "anim retarget Humanoid", 100.0);
    one("AutoPhysics (body physics)", "body physics", 50.0, [&] { return ed->body_solving_for_test(); });
    if (!ragdoll::available()) rep.row({"the ragdoll (body ragdoll show)", "-", "-", "-", "-", "not run here (needs Blender's libraries: Jolt)", "-"});
    else one("the ragdoll (body ragdoll show)", "body ragdoll show", 50.0, [&] { return ed->body_solving_for_test(); });
    one("FK / IK switch (autopose fk larm)", "autopose fk larm", 100.0);
    one("FK / IK switch back (autopose ik larm)", "autopose ik larm", 100.0);
    one("Match IK to FK (autopose match larm)", "autopose match larm", 100.0);
  }

  /* ------------------------------------------------------------- settle, late, the world */

  void settle_rows() {
    rep.table({"at rest", "frames that asked for another", "views drawn", "constraint moves"});
    const int frames = o.quick ? 40 : 120;
    for (int far = 0; far < 2; far++) {
      auto ed = fresh(scene("avatarlike"));
      const Vec3 at = far ? Vec3(5000.0f, 0.0f, 0.0f) : Vec3(0.0f);
      if (GameObject *a = ed->scene().find_by_name("AvatarLike")) a->set_world_position(at);
      steps(*ed, 2);
      cam(*ed, at);
      ed->command("select LeftHandIK"), ed->command("tool move");
      steps(*ed, 3);
      drag_sample(*ed, "perf drag gizmo x 80");
      steps(*ed, 6);
      const FrameProfile p0 = ed->frame_profile_totals();
      const PoseCounts c0 = pose_counts();
      int asked = 0;
      for (int i = 0; i < frames; i++) {
        ed->step_frame_headless();
        asked += ed->frame_asked_for_another_for_test() ? 1 : 0;
      }
      const FrameProfile p1 = ed->frame_profile_totals();
      const PoseCounts c1 = pose_counts();
      rep.row({far ? "the avatar 5,000 m from the origin, after a pose" : "the avatar at the origin, after a pose", std::to_string(asked), std::to_string((long long)(p1.view_renders - p0.view_renders + p1.gpu_views - p0.gpu_views)),
               std::to_string((unsigned long long)(c1.constraint_moves - c0.constraint_moves))});
    }
    rep.note(strprintf("%d frames with no input each; expected 0 for every cell.", frames));
  }

  void late_row() {
    auto ed = fresh(scene("avatarlike"));
    cam(*ed, Vec3(0.0f));
    ed->command("select LeftHandIK"), ed->command("tool move");
    steps(*ed, 3);
    ed->command(strprintf("perf drag gizmo x 80 queue frames %d", n));
    /* After each frame of the drag the constraints are solved once more here: if that moves the hand, the frame just drawn
     * showed a pose the solver had not caught up with (the target moved after the frame's own solve) - whether or not the
     * target is within reach. */
    double worst = 0.0, last = 0.0;
    int late = 0, measured = 0;
    for (int i = 0; i < n + 60 && ed->perf_script_active_for_test(); i++) {
      ed->step_frame_headless();
      const GameObject *hand = ed->scene().find_by_name("LeftHand");
      if (!hand) break;
      const Vec3 shown = hand->world_position();
      solve_constraints(ed->scene());
      const double mm = length(hand->world_position() - shown) * 1000.0;
      worst = std::max(worst, mm), late += mm > 0.1 ? 1 : 0, measured++, last = mm;
    }
    rep.note(strprintf("Is the pose a frame late: after each of %d frames of a queued IK-target drag the constraints were solved once more; the left hand then moved by up to %.3f mm (%.3f mm after the last frame), by more than 0.1 mm after %d of the frames. Above 0: the frame drew the limb where the solver had it BEFORE the target moved - the handle a frame ahead of the hand.",
                       measured, worst, last, late));
  }

  void world_note() {
    auto get = [&](const char *k) { auto it = mean.find(k); return it == mean.end() ? -1.0 : it->second; };
    for (const char *dev : {"CPU", "card"}) {
      const double in_world = get((std::string("in the world/") + dev + "/ik").c_str()), alone = get((std::string("alone/") + dev + "/ik").c_str());
      const double orbit = get((std::string("world alone/") + dev + "/orbit").c_str());
      if (in_world < 0 || alone < 0) continue;
      rep.note(strprintf("Does the world matter (%s): the IK-target drag frame is %.2f ms in the world and %.2f ms alone: %+.2f ms; the world's own orbiting frame is %.2f ms.", dev, in_world, alone,
                         in_world - alone, orbit));
    }
  }

  void moved_rows() {
    rep.table({"alone (round 468: from vgroups)", "ms", "second ms"});
    const SkinPoseTiming skin = skin_pose_timing();
    rep.row({strprintf("Skin pose change, %zu vertices, 3 bones: Linear / Dual Quaternion", skin.vertices), f2(skin.ms[0]), f2(skin.ms[1])});
    const IkTiming ik = ik_timing();
    rep.row({"IK: a humanoid's 4 chains, targets moved / nothing moved", strprintf("%.4f", ik.moved), strprintf("%.4f", ik.still)});
    rep.row({"IK: the same rig among 20k objects, nothing moved", strprintf("%.4f", ik.big), "-"});
    rep.note(strprintf("Task 0047's target: a pose change on a 50k-vertex mesh in at most 8 ms: Linear %.2f ms, Dual Quaternion %.2f ms: %s", skin.ms[0], skin.ms[1], std::max(skin.ms[0], skin.ms[1]) <= 8.0 ? "met" : "NOT MET"));
    rep.note(strprintf("Task 0048: with 20k other objects the per-frame IK pass costs %.3f ms when nothing moved; four chains cost %.4f ms moved, %.4f ms still; the left foot is off its target by at most %g (under 1e-4).",
                       ik.big, ik.moved, ik.still, ik.worst));
  }
};

}  // namespace posing_stress

static void test_posing(Report &rep, const Options &o) {
  using namespace posing_stress;
  rep.title("Posing, scrubbing and playback (round 468)",
            "Round 468 (task 0444): what a frame costs while a skinned avatar is posed, scrubbed and played - `benchscene avatarlike` (283-bone-sized stand-in) alone, "
            "`worldlike` alone, `avatarworld` (the avatar standing in the world, its lighting baked) - headless 1600 x 1000 with the views on the CPU and on the card, "
            "each drag played as real mouse and key events by `perf drag`. Columns after the first three: the press, first move and release frames of a drag; then "
            "where the frame went, ms a frame: the constraint solve, the skin's cache keys, the skinning, shape keys, the in-place mesh moves, outline shells, "
            "uploads to the card, scene hashes, draw items, instance arrays, overlays and the windows' own time (nested, not to add up). The budget: a drag, scrub or play "
            "row with the views on the card at most 16.7 ms a frame, no press, first-move or release frame over 100 ms (a headless frame is stricter than a window: the "
            "picture is copied back); `NO VIEW` marks a row whose frames drew no view; the clip-playing and Play rows pace their frames to 16.7 ms (a clip plays by the real clock). The goal is judged on the windowed rows of tools/bench_world.py.");
  Log::echo_stdout = false;
  Bench b(rep, o);
  const Vec3 world_at(0.0f, 0.0f, -15.0f);
  /* First, while the process has posed nothing: its first posed frame is the row a user's first drag after starting the
   * editor feels (the later blocks are an editor's first Puppet drag, Preview, Record and Play). */
  b.cold_rows(Vec3(0.0f));
  rep.table(row_header());
  b.run_setup("alone", "avatarlike", false, true, false, Vec3(0.0f), false);
  b.run_setup("world alone", "worldlike", false, false, true, Vec3(0.0f), false);
  b.run_setup("in the world", "avatarworld", true, true, true, world_at, true);
  b.world_note();
  b.stall_rows();
  b.settle_rows();
  b.late_row();
  b.moved_rows();
  Log::echo_stdout = true;
}

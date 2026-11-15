// SPDX-License-Identifier: GPL-2.0-or-later
// The Blockout object in the editor (ADR 0129, task 0453): its mesh follows its pieces - asked for by a hash of the
// snapshot, built on a worker (a thread inside JobSystem::run_background, as eval_job_request does) and adopted when it
// is still wanted - and the ways to make, see, pick and refuse to edit one.
#include "editor.h"
#include "../scene/blockout_piece_stack.h"  // (round 514)

#include "blockout_pick.h"

#include "../core/core.h"
#include "../core/jobs.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace bl {

namespace {

bool finite3_bits(Vec3 v) { return finite_bits(v.x) && finite_bits(v.y) && finite_bits(v.z); }

/* Whether this part of the build ran by the exact path, the field path or both. */
std::string path_text(const blockout::Report &r) {
  bool exact = false, field = false;
  for (const blockout::ClusterReport &c : r.clusters) (c.path == blockout::Path::Crisp ? exact : field) = true;
  if (exact && field) return "exact + field";
  return exact ? "exact" : field ? "field" : "none";
}

BlockoutStatus make_status(const blockout::Report &r, size_t piece_count, bool ok, const std::vector<std::pair<uint64_t, std::string>> &refused) {
  BlockoutStatus s;
  s.built = true;
  s.pieces = piece_count + refused.size();
  s.refused = refused.size();
  for (const auto &k : refused) s.skipped.push_back(k);  // (mesh pieces the snapshot left out: named like the kernel's own)
  /* (Round 498: a styled window is several kernel pieces. When the kernel leaves out one of them the window is still used: its status line is a note, "a part
   * of it was left out", named once for each reason. A piece of the user's own is skipped as before.) */
  auto add_named = [](std::vector<std::pair<uint64_t, std::string>> &to, const blockout::Skipped &k) {
    const std::pair<uint64_t, std::string> named{blockout_piece_owner(k.id), blockout_id_generated(k.id) ? "a part of it was left out: " + k.why : k.why};
    if (std::find(to.begin(), to.end(), named) == to.end()) to.push_back(named);
  };
  for (const blockout::Skipped &k : r.skipped) add_named(blockout_id_generated(k.id) ? s.notes : s.skipped, k);
  for (const blockout::Skipped &k : r.notes) add_named(s.notes, k);
  s.used = s.pieces > s.skipped.size() ? s.pieces - s.skipped.size() : 0;
  s.clusters = r.clusters.size();
  for (const blockout::ClusterReport &c : r.clusters) {
    s.unresolved += c.unresolved;
    s.blend_regions += c.blend_regions, s.blend_below += c.blend_below;
    /* (A cluster at a time: one built whole after a failed seam is said with its own tries, whatever another cluster did.) */
    if (c.blend_fallback && !c.blend_dropped) s.blend_fallback = true, s.seam_retries += c.seam_retries;
    s.blend_dropped = s.blend_dropped || c.blend_dropped;
  }
  s.path = path_text(r);
  s.ms = r.ms_total;
  if (!ok) s.refusal = r.why.empty() ? std::string("the build was refused") : r.why;
  return s;
}

}  // namespace

/* ===================================================================== */
/* The live rebuild                                                       */
/* ===================================================================== */

/* Each Blockout whose snapshot differs from the one its mesh was built from asks for a build. Gated by a walk mark of its
 * own (ADR 0115): a scene that did not change costs nothing. `force`: a caller that needs the mesh now (wait, rebuild). */
void Editor::update_blockouts(bool force) {
  if (playing_ || !scene_) return;
  if (!force && !walk_due(blockout_walk_)) {
    walks_skipped_++;
    return;
  }
  WalkScope scope{*this, blockout_walk_};
  blockout_sync_leaves();  // (round 515: a door's leaves are objects - made, kept in step with the door and turned, before the walk below reads the scene)
  std::vector<uint64_t> sources;  // (the objects the enabled pieces of enabled Blockouts use as meshes: not drawn, collided with, baked or exported)
  scene_->for_each([&](GameObject &g) {
    Blockout *b = g.get<Blockout>();
    if (!b) {  // (round 482: a baked Blockout's mesh pieces still use their objects: the baked mesh holds them)
      if (g.get<BlockoutBaked>() && g.active_in_hierarchy()) blockout_baked_sources(g, sources);
      return;
    }
    MeshFilter *mf = g.get<MeshFilter>();
    if (!mf) mf = g.add<MeshFilter>();
    if (!g.get<MeshRenderer>()) g.add<MeshRenderer>()->contribute_gi = true;  // (a Blockout made by Add Component: static geometry like the created ones)
    std::vector<blockout::Piece> pieces;
    blockout::Settings settings;
    BlockoutSnapshotNotes notes;
    blockout_snapshot(g, pieces, settings, &notes);
    if (b->enabled && g.active_in_hierarchy()) sources.insert(sources.end(), notes.sources.begin(), notes.sources.end());
    const uint64_t h = blockout_snapshot_hash(pieces, settings, &notes);
    b->state.refused = std::move(notes.refused);
    b->state.window_fits = std::move(notes.window_fits);
    b->state.swing_cuts = std::move(notes.swing_cuts);
    if (b->state.hash_valid && b->state.hash == h && mf->mesh) return blockout_unwant(g.id);
    blockout_request(g, *b, std::move(pieces), settings, h, std::move(notes.surface));
  });
  blockout_publish_piece_sources(std::move(sources));
}

/* The pieces are back at what the mesh already is (an undo during a build, a drag over the piece's own start): the build waiting
 * for this Blockout is not wanted. The running one finishes, and blockout_finish lands it only if the pieces have moved on by
 * then - round 477's rule, a running build finishes and lands. (Round 502: it was cancelled here, so a drag that kept passing
 * its start while builds were slower than the passes stopped every build before it finished and the mesh froze.) Under the
 * measured cancel-stale policy it is stopped as before. */
void Editor::blockout_unwant(uint64_t obj) {
  for (const auto &j : blockout_jobs_) {
    if (j->obj != obj) continue;
    j->has_next = false;
    if (blockout_cancel_stale_) j->cancel.store(true);
  }
}

/* A build asked for: on the spot where jobs don't run on workers (unit tests, console scripts - the next command sees the
 * mesh), else on a worker, the latest snapshot winning. */
void Editor::blockout_request(GameObject &g, Blockout &b, std::vector<blockout::Piece> &&pieces, const blockout::Settings &settings, uint64_t hash, BlockoutSurface &&surface) {
  if (!jobs_async() || JobSystem::in_background()) {
    blockout_main_builds_++;
    blockout_piece_stacks_apply(pieces, surface);  // (round 514: as on the worker)
    MeshPtr mesh = std::make_shared<Mesh>();
    blockout::Faces faces;
    blockout::Report report;
    if (!b.state.cache) b.state.cache = std::make_unique<blockout::Cache>();
    const bool ok = blockout::build(pieces, settings, *mesh, &faces, &report, b.state.cache.get());
    std::vector<std::string> slots;
    if (ok) slots = blockout_finish_mesh(*mesh, faces, pieces, surface);
    blockout_land(g, b, hash, blockout_user_piece_count(pieces), mesh, std::move(faces), report, ok, std::move(slots));
    return;
  }
  for (const auto &j : blockout_jobs_) {
    if (j->obj != g.id) continue;
    if ((!j->has_next && j->hash == hash) || (j->has_next && j->next_hash == hash)) return;  // (already on it)
    if (!j->has_next) blockout_cancelled_++;  // (superseded: counted once a build)
    j->has_next = true, j->next_hash = hash;  // (the pieces are read again when the running job returns: they may have moved on)
    if (blockout_cancel_stale_) j->cancel.store(true);  // (the running build is stale: it stops at its next check)
    return;
  }
  blockout_start(g, b, std::move(pieces), settings, hash, std::move(surface));
}

void Editor::blockout_start(GameObject &g, Blockout &b, std::vector<blockout::Piece> &&pieces, const blockout::Settings &settings, uint64_t hash, BlockoutSurface &&surface) {
  auto job = std::make_unique<BlockoutJob>();
  job->obj = g.id, job->hash = hash, job->name = g.name;
  job->pieces = std::move(pieces), job->settings = settings, job->surface = std::move(surface);
  job->cache = std::move(b.state.cache);  // (the job owns it while it runs; it comes back with the job)
  if (!job->cache) job->cache = std::make_unique<blockout::Cache>();
  BlockoutJob *raw = job.get();
  const int delay_ms = blockout_delay_ms_for_test_;  // (round 502: a test standing in for a slow build)
  raw->thread = std::thread([raw, delay_ms] {
    if (delay_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    JobSystem::global().run_background([raw] {
      blockout_piece_stacks_apply(raw->pieces, raw->surface);  // (round 514: a piece's modifier stack makes it a mesh piece, here on the worker)
      MeshPtr mesh = std::make_shared<Mesh>();
      raw->ok = blockout::build(raw->pieces, raw->settings, *mesh, &raw->faces, &raw->report, raw->cache.get(), &raw->cancel);
      if (!raw->ok) return;
      raw->slots = blockout_finish_mesh(*mesh, raw->faces, raw->pieces, raw->surface);  // (round 482: slots, UVs and shading on the worker too)
      raw->mesh = std::move(mesh);
    });
    raw->done.store(true, std::memory_order_release);
  });
  blockout_jobs_.push_back(std::move(job));
  blockout_started_++;
  ui_.redraw = true;
}

/* The renderer's materials from the build's slot list (project paths; "" the default material, which a missing file is too). A build that
 * needs nothing but the default leaves the renderer alone until a build has put slots in it - a scene from before round 482 may carry a
 * material set by hand. Once a build has set them (the Blockout's `slots_set`, saved, kept through Bake and Unbake), every build writes the
 * list: one that needs only the default clears what an earlier build put there. */
void Editor::blockout_set_slots(MeshRenderer &mr, Blockout &b, const std::vector<std::string> &slots) {
  const bool plain = slots.empty() || (slots.size() == 1 && slots[0].empty());
  if (plain) {
    if (b.slots_set) mr.materials.clear();
    return;
  }
  mr.materials.clear();
  for (const std::string &path : slots) mr.materials.push_back(path.empty() ? nullptr : material_asset(path));
  b.slots_set = true;
}

/* A build's result becomes the object's: the mesh, the faces' origins and the status together, so what the Scene view picks
 * through belongs to what it draws. A refusal keeps the last mesh and says why once per reason. Nothing here marks the scene
 * changed or adds an undo step: the edit of the pieces did. */
void Editor::blockout_land(GameObject &g, Blockout &b, uint64_t hash, size_t piece_count, MeshPtr mesh, blockout::Faces &&faces,
                           const blockout::Report &report, bool ok, std::vector<std::string> &&slots) {
  MeshFilter *mf = g.get<MeshFilter>();
  if (!mf) mf = g.add<MeshFilter>();
  MeshRenderer *mr = g.get<MeshRenderer>();
  if (!mr) mr = g.add<MeshRenderer>(), mr->contribute_gi = true;
  BlockoutState &s = b.state;
  s.status = make_status(report, piece_count, ok, s.refused);
  if (ok && mesh) {
    mf->mesh = mesh;
    blockout_set_slots(*mr, b, slots);  // (the same step as the mesh: a face's slot number always means what the list says)
    s.faces = std::move(faces);
    s.faces_mesh = mesh.get(), s.faces_version = mesh->version, s.faces_hash = hash;
    s.status.faces = mesh->face_count();
    s.logged_why.clear();
    blockout_landed_++;
  }
  else {
    if (!mf->mesh) mf->mesh = std::make_shared<Mesh>();
    s.status.faces = mf->mesh->face_count();
    if (s.status.refusal != s.logged_why) {
      Log::warn("Blockout '%s': %s - the last mesh stays", g.name.c_str(), s.status.refusal.c_str());
      s.logged_why = s.status.refusal;
    }
  }
  s.hash = hash, s.hash_valid = true;
  edit_epoch::bump();  // (the kernel's touch() ran on the worker, maybe before this frame's hash: the views draw the mesh that landed)
  ui_.redraw = true;
}

/* A returned job: the cache goes back to its Blockout whatever happened (a cancelled build leaves it as it was). Its result
 * lands when the Blockout still exists, unless the pieces are back at what the mesh already is (an undo) - so during a drag
 * the mesh follows one build behind, also when the piece moved after this frame asked (the gizmo runs between the asking and
 * the poll). Then the pieces as they are now are built at once if the mesh is not theirs yet. */
void Editor::blockout_finish(size_t index) {
  std::unique_ptr<BlockoutJob> job = std::move(blockout_jobs_[index]);
  blockout_jobs_.erase(blockout_jobs_.begin() + (std::ptrdiff_t)index);
  if (job->thread.joinable()) job->thread.join();
  GameObject *g = scene_ ? scene_->find(job->obj) : nullptr;
  Blockout *b = g ? g->get<Blockout>() : nullptr;
  if (!b) return;
  b->state.cache = std::move(job->cache);
  ui_.redraw = true;
  if (playing_) return;
  std::vector<blockout::Piece> pieces;
  blockout::Settings settings;
  BlockoutSnapshotNotes notes;
  blockout_snapshot(*g, pieces, settings, &notes);
  const uint64_t now = blockout_snapshot_hash(pieces, settings, &notes);
  b->state.refused = std::move(notes.refused);
  b->state.window_fits = std::move(notes.window_fits);
  b->state.swing_cuts = std::move(notes.swing_cuts);
  auto mesh_is_now = [&] {
    const MeshFilter *mf = g->get<MeshFilter>();
    return b->state.hash_valid && b->state.hash == now && mf && mf->mesh;
  };
  const bool wanted = now == job->hash || (!blockout_cancel_stale_ && !mesh_is_now());
  if (wanted && !job->cancel.load())
    blockout_land(*g, *b, job->hash, blockout_user_piece_count(job->pieces), job->mesh, std::move(job->faces), job->report, job->ok, std::move(job->slots));
  if (mesh_is_now()) return;
  blockout_start(*g, *b, std::move(pieces), settings, now, std::move(notes.surface));
}

void Editor::blockout_jobs_poll() {
  if (blockout_hold_) return;
  for (size_t i = 0; i < blockout_jobs_.size();) {
    if (!blockout_jobs_[i]->done.load(std::memory_order_acquire)) {
      i++;
      continue;
    }
    blockout_finish(i);
  }
}

/* The running builds of `obj` (0: all) joined and adopted, what changed since the last frame asked for first: for the paths
 * that need the true mesh (export, a bake, Play, a script's next command). */
void Editor::blockout_jobs_wait(uint64_t obj) {
  if (!scene_) return;
  for (;;) {
    update_blockouts(true);  // (also after a job returned: a dropped or stale result leaves the newest state to ask for)
    size_t i = 0;
    while (i < blockout_jobs_.size() && obj && blockout_jobs_[i]->obj != obj) i++;
    if (i >= blockout_jobs_.size()) return;
    if (blockout_jobs_[i]->thread.joinable()) blockout_jobs_[i]->thread.join();
    blockout_jobs_[i]->done.store(true, std::memory_order_release);
    blockout_finish(i);
  }
}

/* ===================================================================== */
/* Making them                                                            */
/* ===================================================================== */

namespace {

/* "[subtract|intersect] box|cylinder|sphere|wedge|stairs|arch|outline [numbers]", "mesh <object>", "room [box|cylinder|wedge|outline]
 * [numbers]", "door|window [width head [sill]]" - a piece's kind, operation and sizes as the menus and the console say them. */
struct PieceWords {
  int shape = 0, op = 0;
  int opening = 0;     // 1 door, 2 window
  int preset = 0;      // round 500: a BlockoutPreset ("column", "roof", "stairs l", ...): one piece that makes its own solids
  int style = 0;       // a window's style (round 498; BlockoutWindowStyle): "window grid 1.2 1.1 0.9"; a door's (round 499; BlockoutDoorStyle): "door sliding 1.6 2.1"
  bool room = false;
  std::vector<float> numbers;
  std::string mesh_name;  // "mesh <object>": the object's name as it was typed (may be empty: chosen later in the Inspector)
  bool ok = false;
};

bool is_number_word(const std::string &w) { return !w.empty() && (std::isdigit((unsigned char)w[0]) || w[0] == '-' || w[0] == '.'); }

PieceWords parse_piece_words(const std::string &what) {
  PieceWords p;
  bool shape_seen = false, numbers_ok = false, style_seen = false;
  size_t used = 0;
  const std::vector<std::string> words = split_ws(what);
  static const struct { const char *word; int shape; } kShapes[] = {{"box", 0}, {"cube", 0}, {"cylinder", 1}, {"sphere", 2}, {"wedge", 3}, {"ramp", 3},
                                                                    {"stairs", 4}, {"arch", 5}, {"outline", 6}, {"mesh", 7}};
  for (size_t i = 0; i < words.size(); i++) {
    const std::string w = to_lower(words[i]);
    int shape = -1;
    for (const auto &k : kShapes)
      if (w == k.word) shape = k.shape;
    if (w == "subtract" || w == "cut") p.op = 1;
    else if (w == "intersect") p.op = 2;
    else if (w == "add") p.op = 0;
    else if (p.opening != 0 && !style_seen && !shape_seen && blockout_style_from_word(p.opening, w) >= 0) p.style = blockout_style_from_word(p.opening, w), style_seen = true;
    else if (p.opening != 0 && style_seen && p.style == 0 && w == "only") continue;  // ("opening only": the second word of Opening Only)
    else if (!shape_seen && !p.room && p.opening == 0 && blockout_preset_from_words(words, i, used) > 0) {
      p.preset = blockout_preset_from_words(words, i, used), shape_seen = numbers_ok = true;
      i += used - 1;
    }
    else if (!shape_seen && w == "room") p.room = true, numbers_ok = true;
    else if (!shape_seen && (w == "door" || w == "window")) p.opening = w == "door" ? 1 : 2, numbers_ok = true;
    else if (!shape_seen && shape == 7) {
      /* The rest of the line is the object's name (it may hold spaces and digits). */
      p.shape = 7, shape_seen = true;
      for (size_t j = i + 1; j < words.size(); j++) p.mesh_name += (p.mesh_name.empty() ? "" : " ") + words[j];
      break;
    }
    else if (!shape_seen && shape >= 0) p.shape = shape, shape_seen = numbers_ok = true;
    else if (numbers_ok && is_number_word(w)) p.numbers.push_back((float)std::atof(w.c_str()));
    else return p;  // (a word it does not know: not ok)
  }
  /* A negative size is not a piece the console makes. (A size of 0 is made, as in round 477: the Inspector's status
   * then names the piece as skipped and says why.) An outline's corners are the one place a number may be negative. */
  for (float n : p.numbers)
    if (!finite_bits(n) || (n < 0.0f && p.shape != 6 && p.preset != kPresetSpiral)) return p;
  if (p.opening) p.room = false, p.shape = 0, p.op = 1;  // (an opening is a box that cuts, whatever else was said)
  if (p.preset) p.room = false, p.shape = 0, p.op = 0;   // (round 500: a preset is added, whatever else was said)
  if (p.room && (p.shape == 2 || p.shape == 4 || p.shape == 5 || p.shape == 7)) return p;  // (no rooms of spheres from the menus and the console yet; stairs, arches and meshes are never rooms)
  p.ok = shape_seen || p.room || p.opening;
  return p;
}

/* What a piece is called when it is made: "Box", "Subtract Cylinder", "Room", "Room Cylinder", "Door", "Intersect Box", "Wedge", "Room Outline". */
std::string piece_base_name(const PieceWords &w) {
  if (w.preset) return kBlockoutPresetNames[std::min(kBlockoutPresets - 1, std::max(0, w.preset))];
  if (w.opening) return w.opening == 1 ? "Door" : "Window";
  if (w.room) return w.shape == 1 ? "Room Cylinder" : w.shape == 3 ? "Room Wedge" : w.shape == 6 ? "Room Outline" : "Room";
  const std::string shape = kBlockoutShapeNames[std::min(7, std::max(0, w.shape))];
  return w.op == 1 ? "Subtract " + shape : w.op == 2 ? "Intersect " + shape : shape;
}

}  // namespace

/* A piece under `parent`, named for what it is, at `at` in the world (nullptr: the parent's origin). `wall`: a new room's three
 * thicknesses (the Blockout's Default Wall Thickness). */
static GameObject *add_piece(Scene &scene, GameObject &parent, const std::string &name, const PieceWords &w, const Vec3 *at, float wall) {
  GameObject *g = scene.create(name, &parent);
  BlockoutPiece *bp = g->add<BlockoutPiece>();
  bp->shape = w.shape, bp->operation = w.op;
  const std::vector<float> &n = w.numbers;
  if (w.preset) {  // (round 500: the preset's own defaults, then the numbers it was given)
    blockout_preset_defaults(*bp, w.preset);
    blockout_preset_numbers(*bp, n);
    if (at) g->set_world_position(*at);
    return g;
  }
  if (w.opening) {
    bp->opening = w.opening;
    bp->width = w.opening == 1 ? 0.9f : 1.2f, bp->head = w.opening == 1 ? 2.1f : 2.0f, bp->sill = w.opening == 1 ? 0.0f : 0.9f;
    /* The style comes first so that its preset size is what the typed numbers then replace (an explicit size wins, even one that equals another style's preset). */
    if (w.style != 0) blockout_opening_set_style(*bp, w.style);
    if (n.size() >= 1) bp->width = n[0];
    if (n.size() >= 3) bp->sill = n[2];
    /* (Round 498: the numbers a style reads keep the component's defaults; a Round window is a circle - Head = Sill + Width - unless the numbers say a Head.) A Revolving door
     * (round 499): its Width is the two entries through its ring, 1.2 m unless the numbers say - the drum is 1 m in radius, or wide enough for them. */
    if (w.opening == 2 && bp->style == kWindowRound && n.size() < 2 && finite_bits(bp->width) && finite_bits(bp->sill) && bp->width > 0.0f) bp->head = std::min(kBlockoutMaxSize, bp->sill + bp->width);
    if (w.opening == 1 && bp->style == kDoorRevolving) bp->width = n.size() >= 1 ? n[0] : 1.2f, bp->drum_radius = std::max(1.0f, 0.625f * bp->width);  // (Width is the two entries: 1.2 m by default in a drum of 1 m; a wider one grows the drum so that the entries stay within 1.6 of its radii)
    if (n.size() >= 2) bp->head = n[1];
    if (n.size() == 2 && bp->head <= bp->sill) bp->sill = 0.5f * bp->head;  // (a low window: its sill under its head)
    if (at) g->set_world_position(*at);
    return g;
  }
  if (w.room) {
    bp->room = true;
    bp->wall = bp->floor = bp->ceiling = wall;
    if (w.shape == 0 || w.shape == 3) bp->size = {4.0f, 3.0f, 4.0f};
    else if (w.shape == 6) bp->height = 3.0f;
    else bp->radius = 2.0f, bp->height = 3.0f;
  }
  const bool boxy = w.shape == 0 || w.shape == 3 || w.shape == 4;  // (a box, a wedge and stairs are sized by x y z)
  if (w.shape == 3) bp->size = {2.0f, 1.0f, 4.0f};
  if (w.shape == 3 && w.room) bp->size = {4.0f, 3.0f, 4.0f};
  if (w.shape == 4) bp->size = {2.0f, 2.0f, 4.0f};
  if (w.shape == 5) bp->radius = 1.5f, bp->inner_radius = 1.0f, bp->height = 0.5f, bp->segments = 12;
  if (w.shape == 6) bp->outline = {Vec2(-2.0f, -2.0f), Vec2(2.0f, -2.0f), Vec2(2.0f, 2.0f), Vec2(-2.0f, 2.0f)};
  if (boxy && n.size() >= 3) bp->size = {n[0], n[1], n[2]};
  if (boxy && n.size() == 1) bp->size = {n[0], n[0], n[0]};
  if (w.shape == 4 && n.size() >= 4) bp->steps = std::max(1, std::min(256, (int)std::lround(n[3])));
  if (w.shape == 1 && n.size() >= 1) bp->radius = n[0];
  if (w.shape == 1 && n.size() >= 2) bp->height = n[1];
  if (w.shape == 2 && n.size() >= 1) bp->radius = n[0];
  if (w.shape == 5 && n.size() >= 1) bp->radius = n[0];            // arch: outer radius, inner radius, angle, depth, segments
  if (w.shape == 5 && n.size() >= 2) bp->inner_radius = n[1];
  if (w.shape == 5 && n.size() >= 3) bp->angle = n[2];
  if (w.shape == 5 && n.size() >= 4) bp->height = n[3];
  if (w.shape == 5 && n.size() >= 5) bp->segments = std::max(3, std::min(256, (int)std::lround(n[4])));
  if (w.shape == 6) {  // outline: [height] then the corners as x z pairs (an odd count: the first number is the height)
    size_t first = n.size() % 2;
    if (first) bp->height = n[0];
    if (n.size() - first >= 6) {
      bp->outline.clear();
      for (size_t i = first; i + 1 < n.size(); i += 2) bp->outline.push_back(Vec2(n[i], n[i + 1]));
    }
  }
  if (w.shape == 7) {
    bp->mesh_object = w.mesh_name;
    if (const GameObject *src = w.mesh_name.empty() ? nullptr : scene.find_by_name(w.mesh_name)) {
      g->set_world_matrix(src->world_matrix());  // (where the object stands: the piece takes its place, and is placed by its own transform from then on)
      return g;
    }
  }
  if (at) g->set_world_position(*at);
  return g;
}

/* GameObject > Blockout: an object that carries the result, and one 2 x 2 x 2 m box to start from - or, from a piece's menu
 * entry with nothing selected, that piece instead. */
GameObject *Editor::create_blockout(const std::string &first_piece) {
  GameObject *g = scene_->create(unique_object_name("Blockout"), nullptr);
  g->add<Blockout>();
  g->add<MeshFilter>()->mesh = std::make_shared<Mesh>();
  g->add<MeshRenderer>()->contribute_gi = true;  // (round 482: static geometry - it takes part in baked lighting)
  g->set_world_position(cam_.pivot);
  GameObject *piece = nullptr;
  if (first_piece.empty()) {
    PieceWords w;
    w.numbers = {2.0f, 2.0f, 2.0f};
    piece = add_piece(*scene_, *g, "Box", w, nullptr, g->get<Blockout>()->default_wall);
  }
  else {
    const PieceWords w = parse_piece_words(first_piece);
    piece = add_piece(*scene_, *g, unique_object_name(piece_base_name(w)), w, nullptr, g->get<Blockout>()->default_wall);
  }
  expanded_.insert(g->id);
  select(piece->id);
  update_blockouts(true);  // (its first mesh now: the step recorded holds it)
  mark_changed("Create Blockout");
  record_blockout_last(piece);
  return g;
}

/* A Blockout with no piece, at the world's origin and unturned: the Room tool's pieces are placed in the world, so their numbers are the
 * Blockout's own. */
GameObject *Editor::create_empty_blockout() {
  GameObject *g = scene_->create(unique_object_name("Blockout"), nullptr);
  g->add<Blockout>();
  g->add<MeshFilter>()->mesh = std::make_shared<Mesh>();
  g->add<MeshRenderer>()->contribute_gi = true;
  return g;
}

/* The Blockout of the selected piece, else the selected Blockout (nullptr in `target`: none - the caller makes one) and its scale. The sizes are written in
 * the Blockout's own units: a uniformly scaled Blockout takes them divided by its scale, an unevenly scaled one has no piece of the dragged size
 * whatever its turn. False, with a warning, for one that is scaled unevenly or sheared. */
bool Editor::room_target(GameObject *&target, float &scale) {
  GameObject *sel = active_object();
  target = sel && sel->get<Blockout>() ? sel : sel ? blockout_of_piece(*sel) : nullptr;
  /* Round 495: a piece drawn on a face of a Blockout is that Blockout's, whatever is selected. */
  GameObject *drawn_on = scene_->find(room_.surface);
  if (drawn_on && drawn_on->get<Blockout>()) target = drawn_on;
  scale = 1.0f;
  if (!target) return true;
  const Mat4 &tw = target->world_matrix();
  const float sx = length(tw.dir({1.0f, 0.0f, 0.0f})), sy = length(tw.dir({0.0f, 1.0f, 0.0f})), sz = length(tw.dir({0.0f, 0.0f, 1.0f}));
  const float lo = std::min(sx, std::min(sy, sz)), hi = std::max(sx, std::max(sy, sz));
  /* Axes that are not square to each other (a turned Blockout under an unevenly scaled parent) shear the piece: no size is right for it. */
  bool square = lo > 1e-6f && finite_bits(hi);
  if (square) {
    const Vec3 ax = normalize(tw.dir({1.0f, 0.0f, 0.0f})), ay = normalize(tw.dir({0.0f, 1.0f, 0.0f})), az = normalize(tw.dir({0.0f, 0.0f, 1.0f}));
    square = std::fabs(dot(ax, ay)) < 1e-3f && std::fabs(dot(ay, az)) < 1e-3f && std::fabs(dot(ax, az)) < 1e-3f;
  }
  if (!square || hi > lo * 1.001f) {
    Log::warn("Room: '%s' is scaled unevenly or sheared (or flat): a room of the dragged size cannot be made in it - make it in a Blockout with an even scale", target->name.c_str());
    return false;
  }
  scale = sx;
  return true;
}

namespace {

/* A direction in the surface turned a quarter, `quarters` times, round the normal (right-handed: the normal crossed with it). */
Vec3 turned_in_surface(Vec3 d, Vec3 normal, int quarters) {
  for (int k = 0; k < (quarters & 3); k++) d = cross(normal, d);
  return d;
}

}  // namespace

/* The piece the two drags describe, whether shown or made (round 496). A box, a cylinder and a room stand on the footprint. A wedge and stairs rise
 * away from where the drag began along the rectangle's LONGER side (a wedge is tall at the far end, stairs climb toward it); an arch spans the longer
 * side with its feet on the edge the drag began on and bulges across the shorter; each is turned `turn` quarters round the normal about the middle of
 * its footprint, its two sizes swapping so the footprint stays. Pushed into the surface (a negative height) a wedge or stairs is turned over, its
 * flat floor on the face: the slope or the treads are the floor of what it cuts, open at the face. */
DrawnPiece blockout_drawn_piece(bool solid, bool circle, int kind_piece, int op, Vec3 origin, Vec3 u, Vec3 normal, float du, float dz, float height, float scale, int turn) {
  DrawnPiece d;
  const Vec3 z = cross(u, normal);
  const float h = solid ? height : std::fabs(height);
  const int kind = solid && !circle ? kind_piece : 0;
  d.shape = circle ? 1 : kind, d.room = !solid, d.op = op, d.height = h;
  const float side = 1.0f / scale, ah = std::fabs(h) * side;
  d.centre = origin + normal * (h * 0.5f);
  d.rotation = quat_from_axes(u, normal, z);
  d.foot = origin;
  if (circle) {
    d.radius = std::fabs(du) * side, d.depth = ah;
    return d;
  }
  if (kind == 0) {
    d.centre = d.centre + u * (du * 0.5f) + z * (dz * 0.5f);
    d.size = {std::fabs(du) * side, ah, std::fabs(dz) * side};
    d.foot = origin + u * (du * 0.5f) + z * (dz * 0.5f);
    return d;
  }
  d.directional = true;
  /* (A square: a wedge and stairs rise along the second side as they always did, an arch spans the first.) */
  const bool along_z = kind == 5 ? std::fabs(dz) > std::fabs(du) : std::fabs(dz) >= std::fabs(du);
  const float longer = std::max(std::fabs(du), std::fabs(dz)), shorter = std::min(std::fabs(du), std::fabs(dz));
  if (kind == 5) {
    /* The span's axis and the bulge's: the longer side from where the drag began, and across the other side the way it was dragged. */
    const Vec3 span = along_z ? z : u, across = along_z ? u : z;
    const float across_sign = (along_z ? du : dz) < 0.0f ? -1.0f : 1.0f, span_extent = along_z ? dz : du;
    const float radius = longer * 0.5f;
    const Vec3 feet = origin + span * (span_extent * 0.5f);  // (the middle of the feet's edge)
    const Vec3 middle = feet + across * (across_sign * radius * 0.5f);  // (the middle of the span x radius footprint it turns about)
    const Vec3 s = turned_in_surface(span, normal, turn), b = turned_in_surface(across * across_sign, normal, turn);
    d.foot = middle - b * (radius * 0.5f);
    d.centre = d.foot + normal * (h * 0.5f);
    d.rotation = quat_from_axes(s, b, cross(s, b));
    d.rise = b, d.reach = radius;
    d.radius = radius * side, d.depth = ah;
    d.inner_radius = op == 0 ? d.radius * 0.5f : 0.0f;  // (the component's own proportion: 0.5 and 0.25)
    return d;
  }
  /* Wedge (3) and stairs (4): the way away from the press along the longer side, then the turn. */
  const Vec3 away = turned_in_surface(along_z ? z * (dz < 0.0f ? -1.0f : 1.0f) : u * (du < 0.0f ? -1.0f : 1.0f), normal, turn);
  const bool odd = (turn & 1) != 0;
  d.size = {(odd ? longer : shorter) * side, ah, (odd ? shorter : longer) * side};
  d.centre = d.centre + u * (du * 0.5f) + z * (dz * 0.5f);
  d.foot = d.centre - normal * (h * 0.5f);
  const Vec3 up = h < 0.0f ? normal * -1.0f : normal, toward_z = kind == 3 ? away * -1.0f : away;  // (a wedge is tall at its -Z)
  d.rotation = quat_from_axes(cross(up, toward_z), up, toward_z);
  d.rise = away, d.reach = longer * 0.3f;
  return d;
}

BlockoutPiece blockout_piece_from_drawn(const DrawnPiece &d, float wall) {
  BlockoutPiece bp;
  bp.shape = d.shape, bp.operation = d.op, bp.room = d.room;
  bp.size = d.size, bp.radius = d.radius, bp.inner_radius = d.inner_radius, bp.height = d.depth;
  if (d.shape == 5) bp.segments = 12;  // (an arch the tool makes: add_piece's)
  if (d.room) bp.wall = bp.floor = bp.ceiling = wall;
  return bp;
}

/* What the Room tool's two drags (and `blockout room`) make: a room, or a solid when the tool's Solid option is on (Add or Subtract),
 * standing on the surface. `u`, `normal` and cross(u, normal) are its right-handed axes; the footprint runs from `origin` by `du` along u
 * and `dz` along the third axis (signed), or is a circle of radius `du` round `origin`; `height` is along the normal (a room always goes
 * up the normal: its floor's top is the surface). The piece goes into the Blockout of the selected piece, else the selected Blockout, else
 * a new one. nullptr (and a warning) when the numbers are no piece. */
GameObject *Editor::room_make(Vec3 origin, Vec3 u, Vec3 normal, float du, float dz, float height, bool circle) {
  const bool solid = room_.solid;
  if (!finite3_bits(origin) || !finite3_bits(u) || !finite3_bits(normal) || !finite_bits(du) || !finite_bits(dz) || !finite_bits(height)) {
    Log::warn("Room: the numbers are not finite - nothing made");
    return nullptr;
  }
  const float h = solid ? height : std::fabs(height);
  const float small = 1e-3f;
  if (std::fabs(h) < small || (circle ? std::fabs(du) < small : std::fabs(du) < small || std::fabs(dz) < small)) {
    Log::warn("Room: a footprint or a height of no size - nothing made");
    return nullptr;
  }
  GameObject *target = nullptr;
  float scale = 1.0f;
  if (!room_target(target, scale)) return nullptr;
  if (!target) target = create_empty_blockout();
  /* Round 495: a wedge, stairs or an arch over the rectangle, and the operation chosen - or, by the direction, Subtract when pushed in. */
  const int kind = solid && !circle ? room_.piece : 0;
  PieceWords w;
  /* (Auto in a Blockout with no matter yet - a new one too - adds: there is nothing to cut, and a lone cutter is nothing to see.) */
  w.room = !solid, w.shape = circle ? 1 : kind, w.op = blockout_draw_operation(solid, room_.operation, room_.subtract, room_.flip, h, blockout_nothing_to_cut(*target)), w.ok = true;
  GameObject *piece = add_piece(*scene_, *target, unique_object_name(piece_base_name(w)), w, nullptr, room_.wall / scale);  // (a thickness is in the Blockout's units too)
  BlockoutPiece *bp = piece->get<BlockoutPiece>();
  /* The numbers and the place come from the one function the Scene view's preview uses too. An arch stands in its XY plane round its origin
   * and is `height` deep along Z: it lies in the surface, and the height pushes it in or pulls it out. A cutting arch is filled (an arched
   * opening); one that adds is a ring. */
  const DrawnPiece d = blockout_drawn_piece(solid, circle, kind, w.op, origin, u, normal, du, dz, height, scale, room_turn(kind));
  const Vec3 centre = d.centre;
  if (circle) bp->radius = d.radius, bp->height = d.depth;
  else if (kind == 5) bp->radius = d.radius, bp->height = d.depth, bp->inner_radius = d.inner_radius;
  else bp->size = d.size;
  piece->set_world_rotation(d.rotation);
  piece->set_world_position(centre);
  expanded_.insert(target->id);
  select(piece->id);
  update_blockouts(true);
  mark_changed(std::string("Create Blockout ") + piece_base_name(w));
  record_blockout_last(piece);
  if (solid && h < 0.0f && kind == 0) {  // (a box or cylinder dug down from the surface: F9's height keeps the surface where it is, not the far end; a wedge or stairs is turned over and stands on it already, an arch's Y is not its depth)
    obj_last_.base_p = centre - normal * (h * 0.5f);  // (the face under the piece's middle - not the corner the drag began at: F9 put a rectangle's pocket there, the second review)
    obj_last_.base_up = -piece->world_matrix().dir({0.0f, 1.0f, 0.0f});
  }
  if (kind == 5) {  // (an arch drawn on a face: F9's Depth keeps the face and grows away from it, as a box's Height does)
    obj_last_.base_p = centre - normal * (h * 0.5f);
    obj_last_.base_up = normal * (h < 0.0f ? -scale : scale);
  }
  return piece;
}

/* The Polyline option's room (or solid): an Outline piece whose corners are `corners` (world points in the surface plane, the first the surface point
 * clicked first) pulled up `height` along `normal`, standing on the surface. `u`, `normal` and cross(u, normal) are the right-handed axes the corners are
 * measured in. The piece's origin is the middle of the outline's box at half the height, so it turns about it as every piece does. nullptr (and a
 * warning) when the numbers are no outline: fewer than three corners, none of a size, or a height of none. */
GameObject *Editor::room_make_outline(const std::vector<Vec3> &corners, Vec3 u, Vec3 normal, float height) {
  const bool solid = room_.solid;
  const Vec3 z = cross(u, normal);
  if (corners.size() < 3 || corners.size() > 256 || !finite3_bits(u) || !finite3_bits(normal) || !finite_bits(height)) {
    Log::warn("Room: an outline needs 3 to 256 finite corners - nothing made");
    return nullptr;
  }
  for (const Vec3 &c : corners)
    if (!finite3_bits(c)) {
      Log::warn("Room: the numbers are not finite - nothing made");
      return nullptr;
    }
  const float h = solid ? height : std::fabs(height);
  std::vector<Vec2> flat;  // (each corner as (along u, along z) from the first)
  float x0 = 1e30f, x1 = -1e30f, z0 = 1e30f, z1 = -1e30f;
  double area = 0.0;
  for (const Vec3 &c : corners) {
    const Vec2 q(dot(c - corners[0], u), dot(c - corners[0], z));
    flat.push_back(q);
    x0 = std::min(x0, q.x), x1 = std::max(x1, q.x), z0 = std::min(z0, q.y), z1 = std::max(z1, q.y);
  }
  for (size_t i = 0; i < flat.size(); i++) area += (double)flat[i].x * flat[(i + 1) % flat.size()].y - (double)flat[(i + 1) % flat.size()].x * flat[i].y;
  if (std::fabs(h) < 1e-3f || x1 - x0 < 1e-3f || z1 - z0 < 1e-3f || !(std::fabs(area) > 1e-6)) {
    Log::warn("Room: an outline or a height of no size - nothing made");
    return nullptr;
  }
  GameObject *target = nullptr;
  float scale = 1.0f;
  if (!room_target(target, scale)) return nullptr;
  if (!target) target = create_empty_blockout();
  PieceWords w;
  w.room = !solid, w.shape = 6, w.op = blockout_draw_operation(solid, room_.operation, room_.subtract, room_.flip, h, blockout_nothing_to_cut(*target)), w.ok = true;
  GameObject *piece = add_piece(*scene_, *target, unique_object_name(piece_base_name(w)), w, nullptr, room_.wall / scale);
  BlockoutPiece *bp = piece->get<BlockoutPiece>();
  const float cx = 0.5f * (x0 + x1), cz = 0.5f * (z0 + z1);
  bp->outline.clear();
  for (const Vec2 &q : flat) bp->outline.push_back(Vec2((q.x - cx) / scale, (q.y - cz) / scale));
  bp->height = std::fabs(h) / scale;
  piece->set_world_rotation(quat_from_axes(u, normal, z));
  piece->set_world_position(corners[0] + u * cx + z * cz + normal * (h * 0.5f));
  expanded_.insert(target->id);
  select(piece->id);
  update_blockouts(true);
  mark_changed(std::string("Create Blockout ") + piece_base_name(w));
  record_blockout_last(piece);
  if (solid && h < 0.0f) {  // (a solid dug down from the surface: as room_make's)
    obj_last_.base_p = corners[0] + u * cx + z * cz;  // (the face under the piece's middle, not the first corner)
    obj_last_.base_up = -piece->world_matrix().dir({0.0f, 1.0f, 0.0f});
  }
  return piece;
}

/* A piece just made: Adjust Last Operation (F9) re-runs it from its sizes. The floor's middle is kept where it was so a new height
 * grows the piece upward from it; width and depth grow about the centre. */
void Editor::record_blockout_last(GameObject *g) {
  const BlockoutPiece *bp = g ? g->get<BlockoutPiece>() : nullptr;
  if (!bp) return;
  ObjLastOp o;
  o.kind = "blockout", o.what = g->name, o.obj = g->id;
  o.op = bp->operation;
  /* n: 0 a box, wedge or stairs (Width, Depth, Height), 1 a cylinder, 2 a sphere, 3 a door or window, 4 an outline (Height), 5 an arch
   * (Radius, Inner Radius, Depth), 6 a mesh piece (nothing to adjust). */
  if (bp->is_preset()) o.n = 7, o.pre = *bp;  // (round 500: n 7 a preset - F9 edits a copy of its numbers)
  else if (bp->opening) o.a = bp->width, o.b = bp->head, o.c = bp->sill, o.n = 3;
  else if (bp->shape == 0 || bp->shape == 3 || bp->shape == 4) o.a = bp->size.x, o.b = bp->size.z, o.c = bp->size.y, o.n = 0;
  else if (bp->shape == 1) o.a = bp->radius, o.c = bp->height, o.n = 1;
  else if (bp->shape == 2) o.a = bp->radius, o.n = 2;
  else if (bp->shape == 6) o.c = bp->height, o.n = 4;
  else if (bp->shape == 5) o.a = bp->radius, o.b = bp->inner_radius, o.c = bp->height, o.n = 5;
  else o.n = 6;
  o.d = bp->wall, o.e = bp->floor, o.f = bp->ceiling;
  if (bp->opening == 2) {  // (round 498: F9 shows a window's style and its numbers)
    o.style = std::min(kBlockoutWindowStyles - 1, std::max(0, bp->style));
    o.frame_width = bp->frame_width, o.frame_depth = bp->frame_depth, o.arch_rise = bp->arch_rise, o.sill_overhang = bp->sill_overhang;
    o.panes_across = bp->panes_across, o.panes_up = bp->panes_up, o.glass = bp->glass, o.sill_board = bp->sill_board;
  }
  o.opening = bp->opening;
  if (bp->opening == 1) {  // (round 499: and a door's)
    o.style = std::min(kBlockoutDoorStyles - 1, std::max(0, bp->style));
    o.frame_width = bp->frame_width, o.frame_depth = bp->frame_depth, o.arch_rise = bp->arch_rise;
    o.leaf = bp->leaf, o.wings = bp->wings, o.leaf_thickness = bp->leaf_thickness, o.open_amount = bp->open_amount, o.hinge = bp->hinge_side, o.slide = bp->slide_direction, o.drum_radius = bp->drum_radius;
  }
  o.flag = bp->is_room();
  o.turnable = blockout_turnable(*bp);  // (F9's Turn row counts its quarter turns from here: 0)
  const Mat4 w = g->world_matrix();
  const float half = blockout_floor_below_origin(*bp);
  o.base_p = w.point({0.0f, -half, 0.0f});
  o.base_up = w.dir({0.0f, 1.0f, 0.0f});
  if (bp->shape == 5) o.base_up = Vec3(0.0f);  // (an arch keeps its middle - unless the tool drew it on a face: room_make says which way it grows)
  o.serial = obj_last_serial_now();
  obj_last_ = std::move(o);
  obj_last_open_ = true;
  obj_last_note_result();
}

/* The entries every menu that creates objects shares (GameObject, the Hierarchy's +, the Tools window's grid is its own). */
void Editor::blockout_menu_items(bool child) {
  (void)child;  // (a piece goes under the selected Blockout whatever the menu: there is nothing else to be a child of)
  auto &u = ui_;
  if (u.menu_item("New Blockout", nullptr, false, true, ui::Icon::Cube)) create_object("Blockout");
  u.tooltip("An object whose mesh is built from the pieces below it, starting with one 2 m box.");
  for (const char *op : {"Add", "Subtract"})
    for (const char *shape : {"Box", "Cylinder", "Sphere", "Wedge", "Stairs", "Arch", "Outline", "Mesh"}) {
      const std::string op_s = op, shape_s = shape;
      if (u.menu_item(op_s + " " + shape_s, nullptr, false, true, op_s == "Add" ? ui::Icon::Plus : ui::Icon::Close))
        create_object("Blockout Piece: " + (op_s == "Add" ? shape_s : op_s + " " + shape_s));
      if (shape_s == "Wedge") u.tooltip("A ramp: tall at -Z, sloping down to +Z.");
      if (shape_s == "Stairs") u.tooltip("A flight of steps climbing towards +Z.");
      if (shape_s == "Arch") u.tooltip("An arch standing in the XY plane: its radius, inner radius and the angle it spans.");
      if (shape_s == "Outline") u.tooltip("A closed outline in the XZ plane pulled up: drag its corners in the Scene view, or click the + between two to add one.");
      if (shape_s == "Mesh") u.tooltip("Any closed mesh of the scene as a piece: the selected object (or choose one in the Inspector). It is not drawn while it is a piece.");
    }
  struct RoomEntry { const char *label, *kind, *tip; };
  for (const RoomEntry &e : {RoomEntry{"Room (Box)", "Room Box", "A room: its box is the air, with walls, a floor and a ceiling of its own that merge with the rooms it overlaps."},
                             {"Room (Cylinder)", "Room Cylinder", "A round room: its cylinder is the air, with a wall, a floor and a ceiling of its own."},
                             {"Room (Wedge)", "Room Wedge", "A room with a sloped ceiling: its wedge is the air."},
                             {"Room (Outline)", "Room Outline", "A room of any outline (an L, a corridor with a bend): its outline is the air, the walls are as thick all the way round."},
                             {"Door", "Door", "A door through the wall under the mouse in the Scene view, or the selected room's first wall: a cut from the floor up."},
                             {"Window", "Window", "A window through the wall under the mouse in the Scene view, or the selected room's first wall: a cut with a sill."}}) {
    if (u.menu_item(e.label, nullptr, false, true, ui::Icon::Plus)) create_object(blockout_ui_kind(std::string("Blockout Piece: ") + e.kind));  // (round 499: a Door or Window has the style its dropdown shows)
    u.tooltip(e.tip);
  }
  /* Round 500: the architectural presets, each one piece with numbers (Inspector, F9, handles). */
  struct ArchEntry { const char *label, *kind, *tip; };
  u.submenu("Architecture", u.px(190), [this] {  // (a submenu of its own: nine more entries would make the Blockout menu taller than a small screen)
    auto &u = ui_;
    for (const ArchEntry &e : {ArchEntry{"Column", "Column", "A column: a round or square shaft with a plinth and a capital, tapering if you like."},
                              {"Pilaster", "Pilaster", "A flat column against a wall: against the wall under the mouse in the Scene view, or the selected room's first wall."},
                              {"Beam", "Beam", "A beam or lintel: a box with a length, a depth and a width."},
                              {"Railing", "Railing", "Posts and rails along a line (or along every side of an outline you give it)."},
                              {"Roof", "Roof", "A gable, hip or shed roof over a rectangle (or an outline with square corners), by pitch, with an overhang."},
                              {"Stairs L", "Stairs L", "Two flights of stairs with a landing, turning a quarter."},
                              {"Stairs U", "Stairs U", "Two flights of stairs with a landing, turning back."},
                              {"Spiral Stairs", "Stairs Spiral", "Steps turning round a post."},
                              {"Arcade", "Arcade", "Arches on columns along a line."}}) {
      if (u.menu_item(e.label, nullptr, false, true, ui::Icon::Plus)) create_object(std::string("Blockout Piece: ") + e.kind);
      u.tooltip(e.tip);
    }
  });
}

std::string Editor::blockout_ui_kind(const std::string &kind) const {
  const bool door = kind == "Blockout Piece: Door", window = kind == "Blockout Piece: Window";
  const int style = door ? blockout_door_style_ : window ? blockout_window_style_ : 0;
  int count = 0;
  const char *const *names = blockout_style_names(door ? 1 : 2, count);
  if ((!door && !window) || style <= 0 || style >= count) return kind;
  return kind + " " + to_lower(names[style]);
}

/* A piece under the selected Blockout, or under the Blockout the selected piece belongs to; with neither, a new Blockout
 * with that piece. `what`: "[subtract] box|cylinder|sphere [x y z | radius height | radius]". */
GameObject *Editor::create_blockout_piece(const std::string &what) {
  PieceWords w = parse_piece_words(what);
  if (!w.ok) {
    Log::warn("Blockout piece: '%s' - say box, cylinder, sphere, wedge, stairs, arch, outline or mesh <object> (optionally 'subtract' or 'intersect' first), room "
              "[box|cylinder|wedge|outline], or door|window, then its sizes (x y z | radius height | radius | width head [sill] | stairs: x y z steps | arch: radius "
              "inner angle depth | outline: [height] x z x z ...)", what.c_str());
    return nullptr;
  }
  GameObject *sel = active_object();
  GameObject *target = sel && sel->get<Blockout>() ? sel : sel ? blockout_of_piece(*sel) : nullptr;
  if (w.shape == 7 && !w.room && !w.mesh_name.empty() && !scene_->find_by_name(w.mesh_name)) {
    Log::warn("Blockout piece: there is no object named '%s' to use as a mesh", w.mesh_name.c_str());
    return nullptr;
  }
  if (w.shape == 7 && w.mesh_name.empty()) {
    /* The menu entry and "blockout add mesh" alone: the selected object that has a mesh and is no part of a Blockout. */
    for (GameObject *g : selected_objects(false)) {
      const MeshFilter *mf = g->get<MeshFilter>();
      if (!mf || !mf->mesh || g->get<Blockout>() || g->get<BlockoutPiece>() || blockout_of_piece(*g) || (target && g == target)) continue;
      w.mesh_name = g->name;
      break;
    }
  }
  if (!target && w.opening) {
    Log::warn("Blockout piece: a door or window goes in a room's wall - select a Blockout or one of its rooms first");
    return nullptr;
  }
  if (!target) return create_blockout(w.shape == 7 && !w.mesh_name.empty() ? "mesh " + w.mesh_name : what);
  const std::string base = piece_base_name(w);
  const float wall = target->get<Blockout>()->default_wall;
  if (w.opening) return create_opening(*target, w.opening, w.numbers, sel, w.style);
  const Vec3 at = cam_.pivot;
  GameObject *piece = add_piece(*scene_, *target, unique_object_name(base), w, &at, wall);
  if (w.preset == kPresetPilaster) blockout_stand_pilaster(*target, *piece, sel);  // (round 500: against the wall under the mouse or the selected room's first wall; none: where the pivot is)
  expanded_.insert(target->id);
  select(piece->id);
  update_blockouts(true);
  mark_changed(std::string("Create Blockout ") + base);
  record_blockout_last(piece);
  return piece;
}

/* A Pilaster stood against a wall (round 500): its back on the wall's face at the spot, its +Z out of the wall into the room, its bottom on the room's floor - blockout_stand_opening's
 * placement without the opening's depth. False (the piece stays where it was): no wall under the mouse and no room to take the first wall of. */
bool Editor::blockout_stand_pilaster(GameObject &bo, GameObject &piece, const GameObject *sel) {
  if (const Blockout *b = bo.get<Blockout>())
    if (!b->state.hash_valid) blockout_jobs_wait(bo.id);
  BlockoutHit wall;
  bool found = scene_hovered_ && !ui_.any_popup_open() && blockout_wall_under_mouse(bo, wall);
  if (!found) {
    std::vector<const GameObject *> pieces;
    blockout_piece_objects(bo, pieces);
    auto is_room = [](const GameObject *p) { return p->get<BlockoutPiece>()->is_room(); };
    const GameObject *room = nullptr;
    if (sel && sel->get<BlockoutPiece>() && std::find(pieces.begin(), pieces.end(), sel) != pieces.end() && is_room(sel)) room = sel;
    for (const GameObject *p : pieces)
      if (!room && is_room(p)) room = p;
    found = room && blockout_first_wall(bo, *room, wall);
  }
  const GameObject *room = found ? scene_->find(wall.piece) : nullptr;
  const BlockoutPiece *rp = room ? room->get<BlockoutPiece>() : nullptr;
  if (!rp || !finite3_bits(wall.point) || !finite3_bits(wall.normal)) return false;
  const Mat4 m = blockout_piece_matrix(*room, bo);
  const Vec3 up = normalize(m.dir({0, 1, 0}));
  Vec3 through = wall.normal - up * dot(wall.normal, up);
  if (!(length(through) > 1e-4f) || !(length(up) > 0.5f)) return false;
  through = normalize(through);
  const Vec3 floor_point = m.point({0.0f, -blockout_floor_below_origin(*rp), 0.0f});
  const Vec3 origin = wall.point + up * (dot(floor_point, up) - dot(wall.point, up));
  if (!finite3_bits(origin)) return false;
  piece.set_local_position(origin);
  piece.set_local_rotation(quat_from_axes(cross(up, through), up, through));
  piece.set_local_scale(Vec3(1.0f));
  return true;
}

/* A door or window under `bo`: on the wall under the mouse in the Scene view (when it is over the view and no menu is open) or, with
 * no wall there, in the middle of the selected room's first wall - the selected piece if it is a room, else the Blockout's first
 * room. The Blockout's mesh is brought up to date first: the wall under the mouse is a face of it. */
GameObject *Editor::create_opening(GameObject &bo, int opening, const std::vector<float> &numbers, const GameObject *sel, int style) {
  PieceWords w;
  w.opening = opening, w.numbers = numbers, w.style = style, w.ok = true;
  const std::string base = piece_base_name(w);
  /* (The wall under the mouse is a face of the mesh shown, whose faces still name their pieces while a newer build
   * runs: no wait for that build here - a large Blockout would hold the click. Only a Blockout never built waits.) */
  if (const Blockout *b = bo.get<Blockout>())
    if (!b->state.hash_valid) blockout_jobs_wait(bo.id);
  BlockoutHit wall;
  const GameObject *room = nullptr;
  const bool pointing = scene_hovered_ && !ui_.any_popup_open();
  bool found = pointing && blockout_wall_under_mouse(bo, wall);
  const bool off_wall = pointing && !found;  // (the mouse is on a floor, a ceiling or nothing: said below, where the opening goes instead)
  if (!found) {
    std::vector<const GameObject *> pieces;
    blockout_piece_objects(bo, pieces);
    auto is_room = [](const GameObject *p) { return p->get<BlockoutPiece>()->is_room(); };
    if (sel && sel->get<BlockoutPiece>() && std::find(pieces.begin(), pieces.end(), sel) != pieces.end() && is_room(sel)) room = sel;
    for (const GameObject *p : pieces)
      if (!room && is_room(p)) room = p;
    found = room && blockout_first_wall(bo, *room, wall);
  }
  if (!found && room) {
    Log::warn("Blockout piece: room '%s' has no upright wall (every side leans more than 2 degrees from vertical, or it is flat) - turn it upright, or point at an upright wall, to put a %s in", room->name.c_str(), base.c_str());
    return nullptr;
  }
  if (!found) {
    Log::warn("Blockout piece: no room to put a %s in - point at a room's wall in the Scene view, or add a room (Room (Box)) first", base.c_str());
    return nullptr;
  }
  GameObject *piece = add_piece(*scene_, bo, unique_object_name(base), w, nullptr, 0.2f);
  std::string why;
  if (!blockout_stand_opening(bo, *piece, wall, &why)) {
    scene_->destroy(std::vector<GameObject *>{piece});
    Log::warn("Blockout piece: %s can't be put there - %s", base.c_str(), why.empty() ? "the room is flat or its numbers are not finite" : why.c_str());
    return nullptr;
  }
  if (off_wall && room) Log::warn("Blockout piece: the mouse is not on a wall (a floor, a ceiling or nothing) - the %s went in the middle of '%s''s first wall; point at a wall to place it there", base.c_str(), room->name.c_str());
  expanded_.insert(bo.id);
  select(piece->id);
  update_blockouts(true);
  mark_changed(std::string("Create Blockout ") + base);
  record_blockout_last(piece);
  return piece;
}

/* ===================================================================== */
/* Seeing and picking them                                              */
/* ===================================================================== */

namespace {

/* The box a piece's wire is drawn in, in the piece's own space: a box's own corners; a cylinder's or a sphere's bounding box, the
 * shape inscribed in it. A room's shell is the air's box grown by `g` (RoomGrowth), the way the kernel grows it. */
struct WireBox {
  int shape = 0;
  int segments = 24, rings = 12;
  Vec3 lo, hi;
  bool ok = false;
};

WireBox wire_box(const BlockoutPiece &p, Vec3 box_size, const RoomGrowth *g) {
  WireBox b;
  b.shape = std::min(2, std::max(0, p.shape));  // (a wedge, stairs, an arch, an outline or a mesh has its own wire: shape_wire)
  b.segments = std::min(256, std::max(3, p.segments));
  b.rings = std::min(128, std::max(2, p.rings));
  Vec3 h;
  if (b.shape == 0) h = Vec3(std::fabs(box_size.x), std::fabs(box_size.y), std::fabs(box_size.z)) * 0.5f;
  else if (b.shape == 1) h = Vec3(std::fabs(p.radius), std::fabs(p.height) * 0.5f, std::fabs(p.radius));
  else h = Vec3(std::fabs(p.radius));
  b.lo = -h, b.hi = h;
  if (g) {
    b.lo = {b.lo.x - (float)g->x, b.lo.y - (float)g->down, b.lo.z - (float)g->z};
    b.hi = {b.hi.x + (float)g->x, b.hi.y + (float)g->up, b.hi.z + (float)g->z};
  }
  b.ok = finite3_bits(b.lo) && finite3_bits(b.hi);
  return b;
}

/* A piece's wire in its own space: a box's 12 edges, a cylinder's two rings and its sides' lines, a sphere's three great
 * circles through its faceting (the equator's segments, the meridians' rings). */
void piece_wire(const WireBox &b, const Mat4 &w, const std::function<void(Vec3, Vec3)> &seg) {
  if (!b.ok) return;
  auto line = [&](Vec3 a, Vec3 c) { seg(w.point(a), w.point(c)); };
  const Vec3 mid = (b.lo + b.hi) * 0.5f, h = (b.hi - b.lo) * 0.5f;
  const int n = b.segments;
  if (b.shape == 0) {
    for (int e = 0; e < 12; e++) {
      const int axis = e / 4, a = (axis + 1) % 3, c = (axis + 2) % 3;
      Vec3 p0, p1;
      p0[a] = p1[a] = (e & 1) ? b.hi[a] : b.lo[a];
      p0[c] = p1[c] = (e & 2) ? b.hi[c] : b.lo[c];
      p0[axis] = b.lo[axis], p1[axis] = b.hi[axis];
      line(p0, p1);
    }
    return;
  }
  if (b.shape == 1) {
    for (int i = 0; i < n; i++) {
      const float a0 = 2.0f * kPi * (float)i / (float)n, a1 = 2.0f * kPi * (float)(i + 1) / (float)n;
      const Vec3 b0(mid.x + h.x * std::cos(a0), b.lo.y, mid.z + h.z * std::sin(a0)), b1(mid.x + h.x * std::cos(a1), b.lo.y, mid.z + h.z * std::sin(a1));
      const Vec3 t0(b0.x, b.hi.y, b0.z), t1(b1.x, b.hi.y, b1.z);
      line(b0, b1);
      line(t0, t1);
      line(b0, t0);
    }
    return;
  }
  const int m = 2 * b.rings;  // (the meridians pass through the poles: two ring steps a circle's half)
  for (int i = 0; i < n; i++) {
    const float a0 = 2.0f * kPi * (float)i / (float)n, a1 = 2.0f * kPi * (float)(i + 1) / (float)n;
    line(mid + Vec3(h.x * std::cos(a0), 0, h.z * std::sin(a0)), mid + Vec3(h.x * std::cos(a1), 0, h.z * std::sin(a1)));
  }
  for (int i = 0; i < m; i++) {
    const float a0 = 2.0f * kPi * (float)i / (float)m, a1 = 2.0f * kPi * (float)(i + 1) / (float)m;
    line(mid + Vec3(h.x * std::cos(a0), h.y * std::sin(a0), 0), mid + Vec3(h.x * std::cos(a1), h.y * std::sin(a1), 0));
    line(mid + Vec3(0, h.y * std::cos(a0), h.z * std::sin(a0)), mid + Vec3(0, h.y * std::cos(a1), h.z * std::sin(a1)));
  }
}

using LineFn = std::function<void(Vec3, Vec3)>;

/* A closed ring of points, and the same ring lifted: its sides. `top` and `bottom` are the two rings (same count). */
void ring_wire(const std::vector<Vec3> &bottom, const std::vector<Vec3> &top, const LineFn &line) {
  const size_t n = bottom.size();
  for (size_t i = 0; i < n; i++) {
    line(bottom[i], bottom[(i + 1) % n]);
    line(top[i], top[(i + 1) % n]);
    line(bottom[i], top[i]);
  }
}

/* A profile (z, y) pulled along X from -x to x: its corners as lines at both ends and the edges between. */
void prism_wire(const std::vector<Vec2> &zy, float x, const LineFn &line) {
  const size_t n = zy.size();
  for (size_t i = 0; i < n; i++) {
    const Vec2 a = zy[i], b = zy[(i + 1) % n];
    line(Vec3(-x, a.y, a.x), Vec3(-x, b.y, b.x));
    line(Vec3(x, a.y, a.x), Vec3(x, b.y, b.x));
    line(Vec3(-x, a.y, a.x), Vec3(x, a.y, a.x));
  }
}

/* The wedge's profile as the kernel lays it out (a room's shell: each side moved out by `g`, the slope's by the ceiling's thickness). */
std::vector<Vec2> wedge_profile(const BlockoutPiece &p, const RoomGrowth *g) {
  const float hy = std::fabs(p.size.y) * 0.5f, hz = std::fabs(p.size.z) * 0.5f;
  if (!g) return {Vec2(-hz, -hy), Vec2(hz, -hy), Vec2(-hz, hy)};
  const float len = std::sqrt(hy * hy + hz * hz);
  if (!(len > 0.0f)) return {Vec2(-hz, -hy), Vec2(hz, -hy), Vec2(-hz, hy)};
  const float na = hy / len, nb = hz / len;  // (the slope's outward normal as (z, y))
  const float down = hy + (float)g->down, back = hz + (float)g->z, c = (float)g->up;
  if (!(na > 1e-6f) || !(nb > 1e-6f)) return {Vec2(-back, -down), Vec2(hz, -down), Vec2(-back, hy)};
  return {Vec2(-back, -down), Vec2((c + nb * down) / na, -down), Vec2(-back, (c + na * back) / nb)};
}

/* The stairs' side profile (z, y): the ground, the back, then the treads and risers down to the ground again. */
std::vector<Vec2> stairs_profile(const BlockoutPiece &p) {
  const float height = std::fabs(p.size.y), depth = std::fabs(p.size.z);
  const int n = std::max(1, std::min(256, p.steps));
  const float run = depth / (float)n, rise = height / (float)n;
  std::vector<Vec2> out = {Vec2(-depth * 0.5f, -height * 0.5f), Vec2(depth * 0.5f, -height * 0.5f), Vec2(depth * 0.5f, height * 0.5f)};
  for (int i = n - 1; i >= 0; i--) {
    const float z0 = -depth * 0.5f + run * (float)i, tread = (i + 1 == n ? height : rise * (float)(i + 1)) - height * 0.5f;
    out.push_back(Vec2(z0, tread));
    out.push_back(Vec2(z0, i == 0 ? -height * 0.5f : rise * (float)i - height * 0.5f));
  }
  return out;
}

/* An outline's corners, shifted out by `by` (a room's shell: the wall) at each corner along the bisector, as the walls' outer faces meet. */
std::vector<Vec2> outline_offset(const std::vector<Vec2> &pts, float by) {
  const size_t n = pts.size();
  if (by == 0.0f || n < 3) return pts;
  double area = 0.0;
  for (size_t i = 0; i < n; i++) area += (double)pts[i].x * pts[(i + 1) % n].y - (double)pts[(i + 1) % n].x * pts[i].y;
  const float turn = area >= 0.0 ? 1.0f : -1.0f;
  auto outward = [&](size_t a, size_t b) {  // (the unit normal of the side from a to b, pointing away from the inside)
    const Vec2 d(pts[b].x - pts[a].x, pts[b].y - pts[a].y);
    const float l = std::hypot(d.x, d.y);
    return l > 0.0f ? Vec2(turn * d.y / l, -turn * d.x / l) : Vec2(0.0f, 0.0f);
  };
  std::vector<Vec2> out;
  for (size_t i = 0; i < n; i++) {
    const Vec2 a = outward((i + n - 1) % n, i), b = outward(i, (i + 1) % n);
    const float k = 1.0f + a.x * b.x + a.y * b.y;
    const float s = by / std::max(0.2f, k);  // (a miter, held back at very sharp corners)
    out.push_back(Vec2(pts[i].x + (a.x + b.x) * s, pts[i].y + (a.y + b.y) * s));
  }
  return out;
}

/* The wire of a wedge, stairs, an arch or an outline in the piece's own space - what the kernel builds, with `g` a room's growth. */
void shape_wire(const BlockoutPiece &p, const RoomGrowth *g, const LineFn &line) {
  const int k = p.shape;
  if (k == 3 || k == 4) {
    const float hx = std::fabs(p.size.x) * 0.5f + (g ? (float)g->x : 0.0f);
    if (finite_bits(hx) && finite3_bits(p.size)) prism_wire(k == 3 ? wedge_profile(p, g) : stairs_profile(p), hx, line);
    return;
  }
  if (k == 6) {
    std::vector<Vec2> pts;
    for (const Vec2 &q : p.outline)
      if (finite_bits(q.x) && finite_bits(q.y)) pts.push_back(q);
    if (pts.size() < 3 || !finite_bits(p.height)) return;
    const float hh = std::fabs(p.height) * 0.5f;
    if (g) pts = outline_offset(pts, (float)g->x);
    std::vector<Vec3> bottom, top;
    for (const Vec2 &q : pts) bottom.push_back(Vec3(q.x, -hh - (g ? (float)g->down : 0.0f), q.y)), top.push_back(Vec3(q.x, hh + (g ? (float)g->up : 0.0f), q.y));
    ring_wire(bottom, top, line);
    return;
  }
  if (k != 5) return;
  const float outer = std::fabs(p.radius), inner = std::min(std::fabs(p.inner_radius), outer), hh = std::fabs(p.height) * 0.5f;
  const float angle = std::min(360.0f, std::max(0.001f, p.angle));
  if (!finite_bits(outer) || !finite_bits(inner) || !finite_bits(hh) || !finite_bits(angle)) return;
  const int seg = std::max(std::max(1, std::min(256, p.segments)), (int)std::ceil(angle / 90.0f));
  const bool pie = inner <= 1e-6f * outer;
  for (int i = 0; i <= seg; i++) {
    const float t = (90.0f - angle * 0.5f + angle * (float)i / (float)seg) * kPi / 180.0f;
    const Vec3 o(outer * std::cos(t), outer * std::sin(t), 0.0f), n(inner * std::cos(t), inner * std::sin(t), 0.0f);
    for (float z : {-hh, hh}) {
      const Vec3 dz(0.0f, 0.0f, z);
      if (i < seg) {
        const float t1 = (90.0f - angle * 0.5f + angle * (float)(i + 1) / (float)seg) * kPi / 180.0f;
        line(o + dz, Vec3(outer * std::cos(t1), outer * std::sin(t1), z));
        if (!pie) line(n + dz, Vec3(inner * std::cos(t1), inner * std::sin(t1), z));
      }
      line(n + dz, o + dz);  // (a block's edge, inner to outer)
    }
    line(o + Vec3(0.0f, 0.0f, -hh), o + Vec3(0.0f, 0.0f, hh));
    if (!pie) line(n + Vec3(0.0f, 0.0f, -hh), n + Vec3(0.0f, 0.0f, hh));
  }
}

/* A mesh piece's wire: the edges of the mesh it uses (every few of them for a very big one). */
void mesh_piece_wire(const BlockoutPiece &bp, const Mat4 &w, const LineFn &line) {
  const GameObject *owner = bp.owner;
  const GameObject *src = owner && owner->scene && !bp.mesh_object.empty() ? owner->scene->find_by_name(bp.mesh_object) : nullptr;
  const MeshFilter *mf = src ? src->get<MeshFilter>() : nullptr;
  if (!mf || !mf->mesh || !bp.mesh_problem().empty()) return;
  const Mesh &m = *mf->mesh;
  const auto &edges = m.edge_cache();
  const size_t stride = std::max<size_t>(1, edges.size() / 40000);
  for (size_t i = 0; i < edges.size(); i += stride) {
    if (edges[i].first >= m.vert_count() || edges[i].second >= m.vert_count()) continue;
    line(w.point(m.positions[edges[i].first]), w.point(m.positions[edges[i].second]));
  }
}

}  // namespace

/* Each piece of a selected Blockout, and a selected piece, as a wire: Add in the selection colour, Subtract in a yellow of
 * the Scene view's own (a theme's warning colour can be the selection's orange), Intersect in a violet of its own, the selected
 * piece brighter. A room draws its air and, fainter, its shell. A piece has no mesh to show itself by. */
static constexpr uint32_t kCutYellow = Color::hex(0xF4BC02);
static constexpr uint32_t kIntersectViolet = Color::hex(0xB06CF5);

/* A preset's outline as it is drawn (round 500): the wire of each kernel piece it makes - posts and rails, a column's plinth, shaft and capital, a roof's hull - so the shape selected is
 * the shape built, not its bounding box. */
static void preset_outline(const Mat4 &w, const BlockoutPiece &bp, const LineFn &seg) {
  std::vector<blockout::Piece> made;
  if (!blockout_preset_pieces(bp, 1, w, made, 0.0f, nullptr)) {  // (one that cannot be made: its box)
    Vec3 lo, hi;
    if (!blockout_preset_bounds(bp, lo, hi)) return;
    BlockoutPiece box;
    box.size = hi - lo;
    piece_wire(wire_box(box, box.size, nullptr), w * Mat4::translate((lo + hi) * 0.5f), seg);
    return;
  }
  for (const blockout::Piece &p : made) {
    const Mat4 &m = p.to_blockout;
    if (p.shape == blockout::Shape::Convex) {
      const std::vector<Vec3> edges = blockout::convex_edges(p.points);
      for (size_t i = 0; i + 1 < edges.size(); i += 2) seg(m.point(edges[i]), m.point(edges[i + 1]));
      continue;
    }
    BlockoutPiece t;
    t.shape = (int)p.shape, t.size = p.size, t.radius = p.radius, t.height = p.height, t.segments = p.segments, t.steps = p.steps, t.fill_under = p.fill_under;
    t.inner_radius = p.inner_radius, t.angle = p.angle;
    if (p.shape == blockout::Shape::Box || p.shape == blockout::Shape::Cylinder) piece_wire(wire_box(t, t.size, nullptr), m, seg);
    else shape_wire(t, nullptr, [&](Vec3 a, Vec3 b) { seg(m.point(a), m.point(b)); });
  }
}

/* The outline of a Round or Arched window, an Arched door or a Revolving door as its real shape (round 500; rounds 498-499 drew their bounding box): the hole's ellipse, or its
 * rectangle with the round head, at both faces of its Depth with a line between them at every fourth corner; a Revolving door's drum as two rings and four lines. False: any other opening. */
static bool styled_opening_outline(const BlockoutPiece &bp, const Mat4 &w, const LineFn &seg) {
  const bool door = bp.opening == 1;
  const bool round = !door && bp.is_styled_window() && bp.style == kWindowRound;
  const bool arched = bp.is_styled_opening() && (door ? bp.style == kDoorArched : bp.style == kWindowArched);
  const bool drum = door && bp.is_styled_door() && bp.style == kDoorRevolving;
  if (!round && !arched && !drum) return false;
  if (!finite_bits(bp.width) || !finite_bits(bp.head) || !finite_bits(bp.sill) || !finite_bits(bp.depth) || !finite_bits(bp.arch_rise) || !finite_bits(bp.drum_radius)) return false;
  const float a = std::fabs(bp.width) * 0.5f, hz = std::fabs(bp.depth) * 0.5f, h = bp.head - bp.sill, n = 24.0f;
  if (!(a > 0.0f) || !(h > 0.0f)) return false;
  if (drum) {
    const float r = std::min(100.0f, std::fabs(bp.drum_radius));
    for (float y : {bp.sill, bp.head})
      for (int i = 0; i < 32; i++) {
        const float t0 = 2.0f * kPi * (float)i / 32.0f, t1 = 2.0f * kPi * (float)(i + 1) / 32.0f;
        seg(w.point({r * std::cos(t0), y, r * std::sin(t0)}), w.point({r * std::cos(t1), y, r * std::sin(t1)}));
      }
    for (int i = 0; i < 4; i++) {
      const float t = 0.5f * kPi * (float)i;
      seg(w.point({r * std::cos(t), bp.sill, r * std::sin(t)}), w.point({r * std::cos(t), bp.head, r * std::sin(t)}));
    }
    return true;
  }
  std::vector<Vec2> xy;
  if (round) {
    for (int i = 0; i < (int)n; i++) xy.push_back(Vec2(a * std::cos(2.0f * kPi * (float)i / n), bp.sill + 0.5f * h + 0.5f * h * std::sin(2.0f * kPi * (float)i / n)));
  }
  else {
    const float rise = std::min(h, bp.arch_rise > 0.0f ? bp.arch_rise : a), spring = bp.head - rise;
    xy.push_back(Vec2(-a, bp.sill)), xy.push_back(Vec2(a, bp.sill));
    for (int i = 0; i <= (int)n; i++) xy.push_back(Vec2(a * std::cos(kPi * (float)i / n), spring + rise * std::sin(kPi * (float)i / n)));
  }
  for (size_t i = 0; i < xy.size(); i++) {
    const Vec2 p = xy[i], q = xy[(i + 1) % xy.size()];
    for (float z : {-hz, hz}) seg(w.point({p.x, p.y, z}), w.point({q.x, q.y, z}));
    if (i % 4 == 0) seg(w.point({p.x, p.y, -hz}), w.point({p.x, p.y, hz}));
  }
  return true;
}

/* A piece's outline as it is drawn, in the world. */
static void piece_outline(const Mat4 &w, const BlockoutPiece &bp, const LineFn &seg) {
  if (bp.is_preset()) return preset_outline(w, bp, seg);
  const bool opening = bp.opening != 0;
  if (opening && styled_opening_outline(bp, w, seg)) return;
  if (!opening && bp.shape == 7) return mesh_piece_wire(bp, w, seg);
  /* A wedge, stairs, an arch, an outline: lines in the piece's own space, put in the world here. */
  if (!opening && bp.shape >= 3 && bp.shape <= 6) return shape_wire(bp, nullptr, [&](Vec3 a, Vec3 b) { seg(w.point(a), w.point(b)); });
  /* An opening's box lies between its sill and head above the origin. */
  piece_wire(wire_box(bp, bp.box_size(), nullptr), opening ? w * bp.shape_offset() : w, seg);
}

/* The Draw Piece tool's preview (round 496) draws a piece that is not made yet with the code that draws a made one: a room's shell too. */
void blockout_piece_wire(const BlockoutPiece &bp, const Mat4 &world, bool shell, const std::function<void(Vec3, Vec3)> &seg) {
  if (!shell) return piece_outline(world, bp, seg);
  RoomGrowth grow;
  if (!bp.is_room() || !blockout_room_growth(bp.to_piece(0, world), grow)) return;
  const bool shaped = bp.shape >= 3 && bp.shape <= 6;
  if (shaped) shape_wire(bp, &grow, [&](Vec3 a, Vec3 b) { seg(world.point(a), world.point(b)); });
  else piece_wire(wire_box(bp, bp.box_size(), &grow), world, seg);
}

uint32_t blockout_operation_colour(int op, bool room) { return room || op == 0 ? kSelectOrange : op == 1 ? kCutYellow : kIntersectViolet; }

bool blockout_turnable(const BlockoutPiece &bp) {
  if (bp.is_preset()) return bp.preset == kPresetStairsL || bp.preset == kPresetStairsU || bp.preset == kPresetSpiral;  // (round 500: stairs turn about their origin, their sizes stay)
  return !bp.opening && (bp.shape == 3 || bp.shape == 4 || bp.shape == 5);
}

bool blockout_outlines_shown(const GameObject &blockout, const std::function<bool(uint64_t)> &selected) {
  if (!blockout.get<Blockout>() || !blockout.active_in_hierarchy()) return false;
  if (selected(blockout.id)) return true;
  std::vector<const GameObject *> pieces;
  blockout_piece_objects(blockout, pieces);
  return std::any_of(pieces.begin(), pieces.end(), [&](const GameObject *p) { return selected(p->id); });
}

namespace {

/* How far along a pixel's ray the depth buffer's value `z` lies, in metres (the buffer's numbers crowd toward the far plane: a
 * quarter of a metre at nine metres is a few parts in a hundred thousand of them, and no fixed allowance in them fits near and
 * far alike). 1e30 when `z` is the background's or past what can be told. */
float depth_metres(const Renderer3D &r3d, const Ray &ray, float z) {
  auto depth_at = [&](float t, float &out) {
    Vec2 s;
    return r3d.project(ray.origin + ray.dir * t, s, out);
  };
  /* The ray starts on the near plane: a hand's width before the eye in a perspective view, a kilometre and more behind it in
   * an orthographic one. So the far end is looked for, not taken as a number. */
  float lo = 0.0f, hi = 1.0f, zhi = 0.0f;
  if (!(z < 1.0f)) return 1e30f;
  while (hi < 1e8f && depth_at(hi, zhi) && zhi < z) lo = hi, hi *= 2.0f;
  if (!(hi < 1e8f)) return 1e30f;
  for (int i = 0; i < 40; i++) {
    const float mid = 0.5f * (lo + hi);
    float zm;
    const bool before = depth_at(mid, zm) && zm < z;  // (past the far plane nothing projects: that is behind `z` too)
    (before ? lo : hi) = mid;
  }
  return 0.5f * (lo + hi);
}

/* How far behind the picture an outline may be and still count as on it (an outline lies in the faces it runs along): two parts
 * in a thousand of its distance from the eye, and a millimetre. An orthographic view has no such distance - its rays are side by
 * side and start far behind the eye - so there it is two parts in a thousand of the height of what the view shows. */
float hidden_allowance(const Renderer3D &r3d, const RenderTarget &rt, const Ray &ray, float x, float y, float along) {
  const Ray beside = r3d.screen_ray(x, y + 100.0f);
  const bool orthographic = dot(ray.dir, beside.dir) > 0.999999f;
  const float shown = length(beside.origin - ray.origin) * 0.01f * (float)rt.height;
  return 0.002f * (orthographic ? shown : along) + 1e-3f;
}

/* How near a view pixel is to a line of the world as it is drawn, in pixels; 1e30 when it is `reach` or more away, when the line
 * is behind the eye, or when it is behind the picture where it is nearest (a pixel around, and `hidden_allowance`). */
float outline_distance(const Renderer3D &r3d, const RenderTarget &rt, Vec3 a, Vec3 b, float px, float py, float reach) {
  Vec2 sa, sb;
  float za, zb;
  if (!r3d.project(a, sa, za) || !r3d.project(b, sb, zb)) return 1e30f;
  const Vec2 d = sb - sa, m(px, py);
  const float len2 = dot(d, d);
  const float t = len2 > 1e-12f ? std::max(0.0f, std::min(1.0f, dot(m - sa, d) / len2)) : 0.0f;
  const Vec2 at = sa + d * t;
  const float away = length(at - m);
  if (!(away < reach)) return 1e30f;  // (before the depth is asked: most lines of a building are nowhere near the click)
  float behind = 0.0f;  // (the farthest the picture is at that place)
  for (int j = -1; j <= 1; j++)
    for (int i = -1; i <= 1; i++) behind = std::max(behind, rt.depth_at((int)at.x + i, (int)at.y + j));
  const Ray ray = r3d.screen_ray(at.x, at.y);
  const float line = depth_metres(r3d, ray, za + (zb - za) * t), picture = depth_metres(r3d, ray, behind);
  if (line > picture + hidden_allowance(r3d, rt, ray, at.x, at.y, picture)) return 1e30f;
  return away;
}

}  // namespace

uint64_t blockout_pick_outline(Scene &scene, const Renderer3D &r3d, const RenderTarget &rt, const std::function<bool(uint64_t)> &selected, int view_x,
                               int view_y, float reach) {
  uint64_t hit = 0;
  float best = reach;
  const float px = (float)view_x + 0.5f, py = (float)view_y + 0.5f;
  std::vector<const GameObject *> pieces;
  scene.for_each([&](GameObject &g) {
    if (!blockout_outlines_shown(g, selected)) return;
    pieces.clear();  // (the list is added to: without this each Blockout would be asked about the ones before it again)
    blockout_piece_objects(g, pieces);
    for (const GameObject *p : pieces) {
      const BlockoutPiece *bp = p->get<BlockoutPiece>();
      const bool cuts = bp->opening != 0 || bp->operation == 1 || (bp->operation == 2 && !bp->room);
      if (!cuts) continue;  // (a piece with a surface is clicked on its surface: its outline runs along edges two pieces share)
      piece_outline(p->world_matrix(), *bp, [&](Vec3 a, Vec3 b) {
        const float d = outline_distance(r3d, rt, a, b, px, py, best);
        if (d < best) best = d, hit = p->id;
      });
    }
  });
  return hit;
}

void Editor::draw_blockouts() {
  const std::function<bool(uint64_t)> selected = [this](uint64_t id) { return is_selected(id); };
  scene_->for_each([&](GameObject &g) {
    if (!g.get<Blockout>() || !g.active_in_hierarchy()) return;
    /* Round 494: every piece shows its outline while anything of the Blockout is selected - faintly when it is another piece that
     * is (the owner: a door seen head-on is air, and could not be found to click). */
    if (!blockout_outlines_shown(g, selected)) return;
    const bool whole = is_selected(g.id);
    std::vector<const GameObject *> pieces;
    blockout_piece_objects(g, pieces);
    for (const GameObject *p : pieces) {
      const bool mine = is_selected(p->id);
      const BlockoutPiece *bp = p->get<BlockoutPiece>();
      const bool opening = bp->opening != 0;
      const uint32_t base = opening || bp->operation == 1 ? kCutYellow : bp->operation == 2 && !bp->room ? kIntersectViolet : kSelectOrange;
      const uint32_t col = (base & 0x00FFFFFFu) | ((mine ? 255u : whole ? 150u : 110u) << 24);
      const float width = mine ? 4e-4f : whole ? 3e-4f : 2.5e-4f;
      const Mat4 w = p->world_matrix();
      auto draw = [&](uint32_t c, float wd) { return [this, c, wd](Vec3 a, Vec3 b) { scene_r3d_.line(a, b, c, true, wd); }; };
      auto own = [&](uint32_t c, float wd) { return [this, c, wd, w](Vec3 a, Vec3 b) { scene_r3d_.line(w.point(a), w.point(b), c, true, wd); }; };
      const bool shaped = !opening && bp->shape >= 3 && bp->shape <= 6;
      piece_outline(w, *bp, draw(col, width));
      if (!mine && !whole) continue;  // (a room's grown shell is drawn for the pieces in hand only)
      if (!bp->is_room()) continue;
      RoomGrowth grow;
      if (!blockout_room_growth(bp->to_piece(p->id, blockout_piece_matrix(*p, g)), grow)) continue;
      const uint32_t faint = (base & 0x00FFFFFFu) | ((mine ? 120u : 70u) << 24);
      if (shaped) shape_wire(*bp, &grow, own(faint, width * 0.75f));
      else piece_wire(wire_box(*bp, bp->box_size(), &grow), w, draw(faint, width * 0.75f));
    }
  });
}

/* The pieces' own faces, when the mesh was built from the pieces exactly as they are: while a build is in flight, and after one
 * that was refused, the mesh is still the one before the edit, and the pieces as they are now say nothing about its faces (`faces`
 * is then empty). */
struct Editor::BlockoutOwners {
  std::unique_ptr<blockout::FaceOwners> faces;
  explicit BlockoutOwners(const GameObject &g) {
    const Blockout *b = g.get<Blockout>();
    std::vector<blockout::Piece> pieces;
    blockout::Settings settings;
    BlockoutSnapshotNotes notes;  // (the hash the faces were landed under holds the materials and the scale too: the same notes)
    blockout_snapshot(g, pieces, settings, &notes);
    const bool landed = b && b->state.hash_valid && b->state.faces_hash == blockout_snapshot_hash(pieces, settings, &notes);
    blockout_piece_stacks_cached(pieces, notes.surface);  // (round 514, after the hash: the stacked pieces as the meshes the build used - from the cache, never evaluated)
    if (landed) faces = std::make_unique<blockout::FaceOwners>(pieces);
  }
};

/* The face of the Blockout's mesh under a view pixel, with the piece and side that made it (the faces' origins say which), the
 * point and the face's normal toward the viewer, in the Blockout's space; false when there is no face or the origins are not the
 * mesh's own (another writer's mesh). `owners`: a caller that asks many times reads the pieces once. */
bool Editor::blockout_hit(GameObject &g, int view_x, int view_y, BlockoutHit &out, const BlockoutOwners *owners) {
  Blockout *b = g.get<Blockout>();
  const MeshFilter *mf = g.get<MeshFilter>();
  if (!b || !mf || !mf->mesh) return false;
  const Mesh &m = *mf->mesh;
  const BlockoutState &s = b->state;
  if (s.faces_mesh != &m || s.faces_version != m.version || s.faces.piece.size() != m.face_count()) return false;  // (another writer's mesh: no origins)
  const Ray ray = scene_r3d_.screen_ray((float)view_x + 0.5f, (float)view_y + 0.5f);  // (the pixel's middle, as the id buffer reads it: at an outline its corner can miss the mesh)
  const Mat4 inv = g.world_matrix().inverse();
  const Ray lr{inv.point(ray.origin), inv.dir(ray.dir)};
  const RenderMesh &rm = m.render_mesh();
  float best = 1e30f;
  size_t best_tri = 0;
  for (size_t t = 0; t < rm.tri_count(); t++) {
    const float d = ray_triangle(lr, rm.positions[rm.indices[t * 3]], rm.positions[rm.indices[t * 3 + 1]], rm.positions[rm.indices[t * 3 + 2]]);
    if (d > 0 && d < best) best = d, best_tri = t;
  }
  if (best >= 1e30f || best_tri >= rm.tri_face.size()) return false;
  const uint32_t face = rm.tri_face[best_tri];
  if (face >= s.faces.piece.size() || face >= s.faces.side.size()) return false;
  /* (Round 498: a face of a styled window's frame or pane names a made piece; what the user clicked is the window: its object. `owned` says so on each way out.) */
  auto owned = [&out] {
    out.piece = blockout_piece_owner(out.piece);
    return true;
  };
  out.piece = s.faces.piece[face], out.side = s.faces.side[face];
  out.point = lr.origin + lr.dir * best;
  const Vec3 a = rm.positions[rm.indices[best_tri * 3]], c1 = rm.positions[rm.indices[best_tri * 3 + 1]], c2 = rm.positions[rm.indices[best_tri * 3 + 2]];
  Vec3 n = cross(c1 - a, c2 - a);
  const float len = length(n);
  if (!(len > 0.0f)) return false;
  n = n * (1.0f / len);
  out.normal = dot(n, lr.dir) > 0.0f ? -n : n;  // (the face the viewer sees: out of the matter)
  /* Faces of several pieces in one plane are one face of the mesh, named for the first of them: the piece whose own face is under
   * the pointer is the one meant (the user, after round 479: a click on one slab of a floor selected another). A blended face has
   * no piece's plane to ask. */
  if (out.side == blockout::kSideBlend) return owned();
  const Vec3 on = out.point - n * dot(n, out.point - a);  // (on the triangle's plane: from far away the ray's own point is off it by more than a face's thickness)
  if (owners) {
    if (owners->faces) owners->faces->at(on, n, out.piece, out.side);
    return owned();
  }
  const BlockoutOwners own(g);
  if (own.faces) own.faces->at(on, n, out.piece, out.side);
  return owned();
}

/* The piece that made the face under a view pixel; 0 when there is none to name - a blended face, a mesh whose origins are not
 * its own, a piece that is gone or another Blockout's. */
uint64_t Editor::blockout_pick_piece(GameObject &g, int view_x, int view_y, const BlockoutOwners *owners) {
  BlockoutHit hit;
  if (!blockout_hit(g, view_x, view_y, hit, owners)) return 0;
  const GameObject *piece = scene_->find(hit.piece);
  if (!piece || !piece->get<BlockoutPiece>()) return 0;
  return blockout_of_piece(*piece) == &g ? piece->id : 0;  // (a duplicate holds its original's origins until its own build lands)
}

/* A box select over a Blockout's mesh adds the pieces that made its faces in the box (`box` is in window pixels, inside the view), read
 * at up to 16 x 16 places of it where the Blockout is what is drawn - so a piece that shows smaller than a sixteenth of a large box
 * can fall between two of them. False when no piece can be named there: the Blockout itself is then what the box selects. */
bool Editor::blockout_box_select(GameObject &g, const Recti &view, const Recti &box) {
  const int step_x = std::max(1, (box.w + 15) / 16), step_y = std::max(1, (box.h + 15) / 16);
  const BlockoutOwners owners(g);
  std::vector<uint64_t> found;
  for (int y = box.y + std::min(step_y, box.h) / 2; y < box.bottom(); y += step_y)
    for (int x = box.x + std::min(step_x, box.w) / 2; x < box.right(); x += step_x) {
      const GameObject *under = scene_->find(scene_rt_.id_at(x - view.x, y - view.y));
      if (under != &g) continue;
      const uint64_t piece = blockout_pick_piece(g, x - view.x, y - view.y, &owners);
      if (piece && std::find(found.begin(), found.end(), piece) == found.end()) found.push_back(piece);
    }
  for (uint64_t id : found) select(id, SEL_ADD);
  return !found.empty();
}

/* ===================================================================== */
/* Doors and windows on walls                                             */
/* ===================================================================== */

/* A room's wall under the mouse: the face under the pointer names a room (not an opening, not a plain piece) and is a wall in the Blockout's frame - a face within 30
 * degrees of horizontal (a floor, a ceiling, a gentle slope) is not. A steeper face that still leans more than 2 degrees is returned and then refused by
 * blockout_stand_opening, which says how much it leans. The mouse is read where the last frame left it. */
bool Editor::blockout_wall_under_mouse(GameObject &bo, BlockoutHit &out) {
  if (!scene_rect_.contains(ui_.in.mx, ui_.in.my)) return false;
  BlockoutHit hit;
  if (!blockout_hit(bo, ui_.in.mx - scene_rect_.x, ui_.in.my - scene_rect_.y, hit)) return false;
  const GameObject *room = scene_->find(hit.piece);
  const BlockoutPiece *bp = room ? room->get<BlockoutPiece>() : nullptr;
  if (!bp || !bp->is_room() || blockout_of_piece(*room) != &bo) return false;
  /* (The Blockout's up, not the room's: a room drawn onto a wall lies on its side, and its own floor would be the wall.) */
  if (std::fabs(hit.normal.y) > 0.5f) return false;
  out = hit;
  return true;
}

/* A room's most upright wall, where an opening goes when the mouse is on no wall: the middle of the air's side, with the point and the normal as a hit on it from
 * inside would give them (the normal into the air). The sides are tried in a fixed order - a box's -X, +X, -Z, +Z, then its caps (a room lying on its side has walls
 * there), a cylinder's facets from the one nearest -X, then its caps, an outline's segments in order - and the one whose normal, in the Blockout's frame, is nearest to
 * horizontal wins; a tie keeps the first, so an upright room gets what it always got (-X). False when no side is within 2 degrees of vertical (the caller says so). */
bool Editor::blockout_first_wall(GameObject &bo, const GameObject &room, BlockoutHit &out) {
  const BlockoutPiece *bp = room.get<BlockoutPiece>();
  if (!bp) return false;
  const Mat4 m = blockout_piece_matrix(room, bo);
  bool have = false;
  float best = 1e30f;
  auto consider = [&](Vec3 at, Vec3 in, uint32_t side) {
    const Vec3 p = m.point(at), n = normalize(m.dir(in));
    if (!finite3_bits(p) || !finite3_bits(n) || !(length(n) > 0.5f)) return;
    const float lean = std::fabs(n.y);
    if (have && !(lean < best - 1e-6f)) return;
    have = true, best = lean;
    out.piece = room.id, out.side = side, out.point = p, out.normal = n;
  };
  if (bp->shape == 6) {  // an outline: the middle of each side, the normal into the air (the side's left when the corners run anticlockwise as (x, z), its right otherwise)
    const size_t n = bp->outline.size();
    double area = 0.0;
    for (size_t i = 0; i < n; i++) area += (double)bp->outline[i].x * bp->outline[(i + 1) % n].y - (double)bp->outline[(i + 1) % n].x * bp->outline[i].y;
    if (n < 3 || !finite_bits(area)) return false;
    const float turn = area >= 0.0 ? 1.0f : -1.0f;
    for (size_t i = 0; i < n; i++) {
      const Vec2 a = bp->outline[i], b = bp->outline[(i + 1) % n];
      const float dx = b.x - a.x, dz = b.y - a.y, l = std::hypot(dx, dz);
      if (!(l > 1e-6f) || !finite_bits(l)) continue;
      consider(Vec3(0.5f * (a.x + b.x), 0.0f, 0.5f * (a.y + b.y)), Vec3(-turn * dz / l, 0.0f, turn * dx / l), (uint32_t)(2 + i));
    }
  }
  else if (bp->shape == 1) {
    /* A cylinder is faceted: the middle of a facet lies nearer the axis than the radius (with 5 segments by a fifth of it - a door at the radius would stand outside the
     * wall). The facet nearest -X is the first. */
    const float reach = std::fabs(bp->radius), half = std::fabs(bp->height) * 0.5f;
    if (!finite_bits(reach) || !finite_bits(half)) return false;
    const int seg = std::max(3, std::min(256, bp->segments));
    const float pi = 3.14159265358979f, first = (float)((seg - 1) / 2);
    for (int j = 0; j < seg; j++) {
      const float mid = 2.0f * pi * ((float)((((int)first + j) % seg)) + 0.5f) / (float)seg;
      const Vec3 in(-std::cos(mid), 0.0f, -std::sin(mid));
      consider(in * (-reach * std::cos(pi / (float)seg)), in, (uint32_t)j);
    }
    consider(Vec3(0.0f, -half, 0.0f), Vec3(0.0f, 1.0f, 0.0f), 0);
    consider(Vec3(0.0f, half, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 0);
  }
  else {
    const bool boxy = bp->shape == 0 || bp->shape == 3;  // (a wedge's tall end is its -Z side; its +Z end and its top are no walls)
    const Vec3 h = boxy ? Vec3(std::fabs(bp->size.x), std::fabs(bp->size.y), std::fabs(bp->size.z)) * 0.5f : Vec3(std::fabs(bp->radius));
    if (!finite3_bits(h)) return false;
    consider(Vec3(-h.x, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), 0);
    consider(Vec3(h.x, 0.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f), 1);
    consider(Vec3(0.0f, 0.0f, -h.z), Vec3(0.0f, 0.0f, 1.0f), 2);
    if (bp->shape != 3) consider(Vec3(0.0f, 0.0f, h.z), Vec3(0.0f, 0.0f, -1.0f), 3);
    if (bp->shape != 3) consider(Vec3(0.0f, -h.y, 0.0f), Vec3(0.0f, 1.0f, 0.0f), 4), consider(Vec3(0.0f, h.y, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 5);
  }
  return have && best <= kOpeningWallLean;
}

/* How thick the Blockout's matter is behind a point of its surface, measured against `out_of` (which points out of the matter):
 * to the next face of the mesh that way. Two rooms that only touch stand wall to wall, twice as thick as either's Wall; a room
 * beside another shares what is left. `fallback` when the mesh has no face behind the point (not built yet) or the answer is no
 * wall (more than 100 m). */
static float blockout_matter_behind(const GameObject &bo, Vec3 point, Vec3 out_of, float fallback) {
  const MeshFilter *mf = bo.get<MeshFilter>();
  if (!mf || !mf->mesh || mf->mesh->face_count() == 0) return fallback;
  const RenderMesh &rm = mf->mesh->render_mesh();
  const Ray ray{point - out_of * 1e-3f, -out_of};
  float best = 1e30f;
  for (size_t t = 0; t < rm.tri_count(); t++) {
    const float d = ray_triangle(ray, rm.positions[rm.indices[t * 3]], rm.positions[rm.indices[t * 3 + 1]], rm.positions[rm.indices[t * 3 + 2]]);
    if (d > 0.0f && d < best) best = d;
  }
  return best < 100.0f ? best + 1e-3f : fallback;
}

/* The height of the room's floor under `point` (down is -Y in the Blockout's frame): the first face of the Blockout's mesh below it that the ROOM made (an Add box, stairs or
 * furniture standing in the room is no floor; when the mesh's faces do not name their pieces - a newer build is running - any face counts) and whose hit lies inside the
 * room's own air (`inv_room` takes it into the room's space, `lo` .. `hi` its box there): another room's floor just outside is not used. */
static bool blockout_floor_under(const GameObject &bo, uint64_t room_id, const Mat4 &inv_room, Vec3 lo, Vec3 hi, Vec3 point, float &y) {
  const MeshFilter *mf = bo.get<MeshFilter>();
  const Blockout *b = bo.get<Blockout>();
  if (!mf || !mf->mesh || !b || mf->mesh->face_count() == 0 || !finite3_bits(point)) return false;
  const Mesh &mesh = *mf->mesh;
  const BlockoutState &s = b->state;
  const bool named = s.faces_mesh == &mesh && s.faces_version == mesh.version && s.faces.piece.size() == mesh.face_count();
  const RenderMesh &rm = mesh.render_mesh();
  const Ray ray{point, Vec3(0.0f, -1.0f, 0.0f)};
  float best = 1e30f;
  for (size_t t = 0; t < rm.tri_count(); t++) {
    if (named && (t >= rm.tri_face.size() || rm.tri_face[t] >= s.faces.piece.size() || s.faces.piece[rm.tri_face[t]] != room_id)) continue;
    const float d = ray_triangle(ray, rm.positions[rm.indices[t * 3]], rm.positions[rm.indices[t * 3 + 1]], rm.positions[rm.indices[t * 3 + 2]]);
    if (d > 0.0f && d < best) best = d;
  }
  if (!(best < 100.0f)) return false;
  y = point.y - best;
  const Vec3 in = inv_room.point(Vec3(point.x, y, point.z));
  const float e = 1e-3f;
  return finite3_bits(in) && in.x >= lo.x - e && in.x <= hi.x + e && in.y >= lo.y - e && in.y <= hi.y + e && in.z >= lo.z - e && in.z <= hi.z + e;
}

/* Turns `opening` to the wall and stands it upright in it: through the wall along the wall's normal (its horizontal part), up along the Blockout's up whatever the room's
 * own turn (round 499 fix: a room on its side gave its up to the door, and the door lay flat), centred in the wall's thickness at the spot. Its depth is the wall's thickness
 * there plus 0.1 m either side. The opening's origin is the floor line - its sill and head are measured from it - and it is a child of the Blockout, so its local transform
 * is the Blockout-space one.
 * The wall must be vertical within 2 degrees in the Blockout's frame. The feet: an upright room keeps its own floor line; a room turned about another axis has none, so the
 * feet stand on the floor under the opening - a ray down from each jamb, on the room's side of the wall (a click on the outer face too), to the first face the room made inside
 * its own air (not furniture, not another room's floor) - and on the HIGHER of the two (on a sloped
 * floor that leaves a small step of wall at the low jamb rather than a cut through the floor slab); with no floor found, on the room's lowest point.
 * False when the wall leans (`why` says by how much), or the room is flat or the numbers are not finite. */
bool Editor::blockout_stand_opening(GameObject &bo, GameObject &opening, const BlockoutHit &wall, std::string *why) {
  const GameObject *room = scene_->find(wall.piece);
  const BlockoutPiece *rp = room ? room->get<BlockoutPiece>() : nullptr;
  BlockoutPiece *op = opening.get<BlockoutPiece>();
  if (!rp || !op || !finite3_bits(wall.point) || !finite3_bits(wall.normal)) return false;
  const Mat4 m = blockout_piece_matrix(*room, bo);
  for (const Vec3 axis : {Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1)})  // (a room flattened to nothing in an axis has no wall to stand in)
    if (!(length(m.dir(axis)) > 1e-6f)) return false;
  const Vec3 up(0.0f, 1.0f, 0.0f);
  if (std::fabs(wall.normal.y) > kOpeningWallLean) {
    if (why) *why = strprintf("that wall leans %.0f degrees - a door or window stands in an upright wall", (double)(std::asin(std::min(1.0f, std::fabs(wall.normal.y))) * 57.29578f));
    return false;
  }
  Vec3 through = wall.normal - up * dot(wall.normal, up);
  if (!(length(through) > 1e-4f)) return false;
  through = normalize(through);
  const float own = finite_bits(rp->wall) ? std::max(0.0f, std::min(100.0f, rp->wall)) : 0.0f;
  const float thick = blockout_matter_behind(bo, wall.point, through, own);
  const Vec3 centre = wall.point - through * (0.5f * thick);  // (the wall's middle: the matter lies behind the face)
  float feet = 0.0f;
  if (!blockout_opening_feet(bo, wall, op->width, feet)) return false;
  const Vec3 origin = centre + up * (feet - dot(centre, up));  // (feet: the floor's height, as dot(floor point, up) was)
  if (!finite3_bits(origin)) return false;
  op->depth = thick + 0.2f;
  opening.set_local_position(origin);
  opening.set_local_rotation(quat_from_axes(cross(up, through), up, through));
  opening.set_local_scale(Vec3(1.0f));
  return true;
}

/* The height, in the Blockout's frame, an opening `width` wide stands on in `wall`: blockout_stand_opening's feet, moved out
 * unchanged (round 513) so Draw Piece measures a drawn door or window from the same floor - a room on its side has no floor
 * line of its own, and the drag's sill and reach are checked against this. The rules are blockout_stand_opening's (above).
 * False when the room is missing or flat-walled, or the numbers are not finite. */
bool Editor::blockout_opening_feet(GameObject &bo, const BlockoutHit &wall, float width, float &feet_y) {
  const GameObject *room = scene_->find(wall.piece);
  const BlockoutPiece *rp = room ? room->get<BlockoutPiece>() : nullptr;
  if (!rp || !finite3_bits(wall.point) || !finite3_bits(wall.normal)) return false;
  const Mat4 m = blockout_piece_matrix(*room, bo);
  const Vec3 up(0.0f, 1.0f, 0.0f);
  Vec3 through = wall.normal - up * dot(wall.normal, up);
  if (!(length(through) > 1e-4f)) return false;
  through = normalize(through);
  const float own = finite_bits(rp->wall) ? std::max(0.0f, std::min(100.0f, rp->wall)) : 0.0f;
  const float thick = blockout_matter_behind(bo, wall.point, through, own);
  const Vec3 centre = wall.point - through * (0.5f * thick);
  const float hy = blockout_floor_below_origin(*rp);
  Vec3 floor_point = m.point({0.0f, -hy, 0.0f});
  if (dot(normalize(m.dir({0, 1, 0})), up) < 0.9999f) {  // (not upright: no floor line of its own)
    Vec3 lo, hi;
    blockout_piece_bounds(*rp, lo, hi);
    float low = 1e30f;  // (the room's lowest point: the feet when no floor is found)
    for (int k = 0; k < 8; k++) low = std::min(low, m.point({k & 1 ? hi.x : lo.x, k & 2 ? hi.y : lo.y, k & 4 ? hi.z : lo.z}).y);
    /* The rays start on the room's side of the wall (a click on the OUTER face gives a normal that points out of the room): 5 cm clear of the wall's inner face. */
    const Vec3 across = cross(up, through), room_centre = m.point(Vec3(0.0f));
    const float side = dot(room_centre - centre, through) > 0.0f ? 1.0f : -1.0f;
    const Vec3 air = centre + through * (side * (0.5f * thick + 0.05f));
    const float half = 0.5f * (finite_bits(width) ? std::max(0.0f, width) : 0.0f);
    const Mat4 inv_room = m.inverse();
    float ya = 0.0f, yb = 0.0f, feet = low;
    const Vec3 a = air + across * half, b = air - across * half;
    /* A start at the click's height, else at the room's middle (a click at the wall's foot starts inside the floor slab). */
    for (const float start_y : {wall.point.y, room_centre.y}) {
      const bool fa = blockout_floor_under(bo, wall.piece, inv_room, lo, hi, Vec3(a.x, start_y, a.z), ya), fb = blockout_floor_under(bo, wall.piece, inv_room, lo, hi, Vec3(b.x, start_y, b.z), yb);
      if (!fa && !fb) continue;
      feet = fa && fb ? std::max(ya, yb) : fa ? ya : yb;
      break;
    }
    if (!finite_bits(feet)) return false;
    floor_point.y = feet;
  }
  feet_y = floor_point.y;
  return finite_bits(feet_y);
}

/* ===================================================================== */
/* Edit Mode refused, the Inspector's status, the console                 */
/* ===================================================================== */

/* A Blockout's mesh (and a piece, which has none) is rebuilt from its pieces: editing it by hand would be thrown away at
 * the next change, so it is not offered. */
bool Editor::blockout_refuses_edit_mode(const GameObject *g) {
  if (!g || !(g->get<Blockout>() || g->get<BlockoutPiece>())) return false;
  const bool of_baked = !g->get<Blockout>() && !blockout_of_piece(*g);  // (a piece under a baked object's holder has no Blockout above it)
  Log::warn(of_baked ? "A Blockout's mesh is rebuilt from its pieces: move or resize the pieces. This piece belongs to a baked object: Unbake brings its Blockout back (blockout unbake)."
                     : "A Blockout's mesh is rebuilt from its pieces: move or resize the pieces. Bake to Mesh makes it editable (blockout bake).");
  return true;
}

/* ===================================================================== */
/* Bake to Mesh and Unbake (round 482)                                    */
/* ===================================================================== */

/* Whether the mesh of a baked object is no longer the one the bake left: edited in Edit Mode, sculpted, replaced. The mesh's content is
 * compared (not which mesh object it is): an undo that gives the same mesh back is no edit. */
bool Editor::blockout_baked_edited(const GameObject &g) const {
  const BlockoutBaked *bk = g.get<BlockoutBaked>();
  const MeshFilter *mf = g.get<MeshFilter>();
  if (!bk || !mf || !mf->mesh) return false;
  /* The answer is kept for the mesh it was worked out for (the Inspector asks every frame it shows the object): hashing a big mesh is not
   * free. Any edit moves its version, its deform version or the mesh itself. */
  const Mesh &m = *mf->mesh;
  if (bk->seen_mesh == &m && bk->seen_uid == m.uid.v && bk->seen_version == m.version && bk->seen_deform == m.deform_version && bk->seen_mask == sculpt_mask_version_) return bk->seen_edited;
  bk->seen_edited = bk->mesh_hash != std::to_string(blockout_mesh_hash(m));
  bk->seen_mesh = &m, bk->seen_uid = m.uid.v, bk->seen_version = m.version, bk->seen_deform = m.deform_version, bk->seen_mask = sculpt_mask_version_;
  return bk->seen_edited;
}

static bool has_blockout_below(const GameObject &g) {
  for (const GameObject *c : g.children)
    if (c->get<Blockout>() || has_blockout_below(*c)) return true;
  return false;
}

/* The pieces' branches directly under `g`: a piece, or an object with a piece somewhere below it (a group of pieces). Another Blockout is
 * not a piece's branch and is not entered (it keeps its place). */
static bool has_piece_below(const GameObject &g) {
  if (g.get<Blockout>()) return false;
  if (g.get<BlockoutPiece>()) return true;
  for (const GameObject *c : g.children)
    if (has_piece_below(*c)) return true;
  return false;
}

/* Bake to Mesh: the object keeps its mesh, materials and UVs, loses the Blockout component and gets a BlockoutBaked that remembers its
 * settings; the pieces move, with their own transforms untouched, under one disabled child - "Blockout Pieces" - so nothing rebuilds the
 * mesh and Edit Mode, sculpting and modifiers take it. One undo step. */
bool Editor::blockout_bake(GameObject &g) {
  Blockout *b = g.get<Blockout>();
  if (!b) {
    Log::warn("Bake to Mesh: '%s' is not a Blockout", g.name.c_str());
    return false;
  }
  blockout_jobs_wait(g.id);  // (the mesh the pieces make now, not the one before the last edit)
  const MeshFilter *mf = g.get<MeshFilter>();
  if (!mf || !mf->mesh || mf->mesh->face_count() == 0) {
    Log::warn("Bake to Mesh: '%s' has no mesh yet (no pieces, or the build was refused: see its status)", g.name.c_str());
    return false;
  }
  if (!b->state.status.refusal.empty())
    Log::warn("Bake to Mesh: the last build of '%s' was refused (%s): the mesh it keeps - from before - is what is baked", g.name.c_str(), b->state.status.refusal.c_str());
  /* The pieces' branches. One that also holds another Blockout cannot be moved (that Blockout would go under the switched-off child and vanish) and
   * cannot be split: the bake is refused, naming it. */
  std::vector<GameObject *> moving;
  for (GameObject *c : g.children)
    if (!c->get<Blockout>() && has_piece_below(*c)) moving.push_back(c);
  for (GameObject *c : moving)
    if (has_blockout_below(*c)) {
      Log::warn("Bake to Mesh: '%s' holds pieces and another Blockout: move that Blockout out of it first", c->name.c_str());
      return false;
    }
  /* Round 515: a door's leaves are objects of their own (ADR 0136): they stay in the scene, enabled, at the same world place - under the baked object, since the pieces' holder is
   * switched off. Each remembers its door, for Unbake. */
  std::vector<GameObject *> leaves;
  std::function<void(GameObject &)> find_leaves = [&](GameObject &branch) {
    for (GameObject *c : branch.children) {
      if (c->get<BlockoutLeaf>()) leaves.push_back(c);
      else find_leaves(*c);
    }
  };
  for (GameObject *c : moving) find_leaves(*c);
  for (GameObject *l : leaves) {
    l->get<BlockoutLeaf>()->door = std::to_string(l->parent->id);
    scene_->set_parent(l, &g, -1, true);
  }
  BlockoutBaked *bk = g.add<BlockoutBaked>();
  bk->cell = b->cell, bk->method = b->method, bk->blend_detail = b->blend_detail, bk->default_wall = b->default_wall, bk->texture_scale = b->texture_scale;
  bk->slots_set = b->slots_set;
  for (int k = 0; k < kBlockoutClasses; k++) bk->material[k] = b->material[k];
  bk->mesh_hash = std::to_string(blockout_mesh_hash(*mf->mesh));
  GameObject *holder = scene_->create(kBlockoutPiecesName, &g);
  bk->pieces_id = std::to_string(holder->id);
  for (GameObject *c : moving) scene_->set_parent(c, holder, -1, false);  // (the local transforms as they are: the pieces come back bit for bit)
  holder->active = false;
  g.remove_component(b);  // (a mesh piece's object stays out of the views: the next walk takes its sources from under the holder)
  expanded_.insert(g.id);
  select(g.id);
  mark_changed("Bake Blockout to Mesh");
  Log::info("Baked '%s' to a mesh: %zu faces, %zu pieces under '%s' (blockout unbake brings the Blockout back)", g.name.c_str(), mf->mesh->face_count(), moving.size(), kBlockoutPiecesName);
  return true;
}

/* Unbake: the Blockout component comes back with the settings, the pieces return from under "Blockout Pieces" and the mesh is built from
 * them again - what is there now, if they were edited or deleted. A mesh edited since the bake is lost: that asks first (a dialog), unless
 * `force`. One undo step. */
bool Editor::blockout_unbake(GameObject &g, bool force) {
  BlockoutBaked *bk = g.get<BlockoutBaked>();
  if (!bk) {
    Log::warn("Unbake: '%s' is not a baked Blockout (Bake to Mesh makes one)", g.name.c_str());
    return false;
  }
  if (edit_mode_ && edit_object() == &g) {
    Log::warn("Unbake: leave Edit Mode on '%s' first", g.name.c_str());
    return false;
  }
  GameObject *holder = blockout_pieces_holder(g);  // (by the id the bake stored, else by name)
  if (!holder && !force) {  // (changed on review: it used to bring back an empty Blockout, the baked mesh lost until Undo)
    Log::warn("Unbake: '%s' has no pieces left (the '%s' child is gone): it stays a mesh (blockout unbake force makes it an empty Blockout)", g.name.c_str(), kBlockoutPiecesName);
    return false;
  }
  if (!force && blockout_baked_edited(g)) {
    unbake_ask_ = g.id;
    dialog_ = Dialog::Unbake;
    Log::info("Unbake: the mesh of '%s' was edited since the bake and the edits would be lost - confirm in the dialog (blockout unbake force skips it)", g.name.c_str());
    return false;
  }
  Blockout *b = g.add<Blockout>();
  b->cell = bk->cell, b->method = bk->method, b->blend_detail = bk->blend_detail, b->default_wall = bk->default_wall, b->texture_scale = bk->texture_scale;
  for (int k = 0; k < kBlockoutClasses; k++) b->material[k] = bk->material[k];
  b->slots_set = bk->slots_set;
  size_t pieces = 0;
  if (holder) {
    const std::vector<GameObject *> back = holder->children;  // (a copy: the list changes as they move)
    for (GameObject *c : back) scene_->set_parent(c, &g, -1, false), pieces++;
    scene_->destroy(holder);
  }
  else Log::warn("Unbake: '%s' has no '%s' child - the Blockout comes back with no pieces", g.name.c_str(), kBlockoutPiecesName);
  /* Round 515: the leaves the bake left under this object go back under their doors (world place kept); one whose door is gone stays where it is, an ordinary object. */
  std::vector<GameObject *> waiting;
  for (GameObject *c : g.children)
    if (c->get<BlockoutLeaf>()) waiting.push_back(c);
  for (GameObject *l : waiting) {
    BlockoutLeaf *mark = l->get<BlockoutLeaf>();
    GameObject *door = scene_->find(std::strtoull(mark->door.c_str(), nullptr, 10));
    if (!door || door == &g || !scene_->is_ancestor(&g, door) || !door->get<BlockoutPiece>()) continue;
    scene_->set_parent(l, door, -1, true);
    mark->door.clear();
  }
  g.remove_component(bk);
  expanded_.insert(g.id);
  select(g.id);
  update_blockouts(true);  // (asks for the build as the other blockout commands do: on the spot with synchronous jobs, a worker's otherwise - not waited for)
  mark_changed("Unbake Blockout");
  Log::info("Unbaked '%s': %zu pieces are back and the mesh is built from them", g.name.c_str(), pieces);
  return true;
}

/* Round 498: styled windows the grid could not show at the Blockout's Field Cell (BlockoutWindowFit): one line for the windows made with their frame held at a
 * cell and no pane, one for those that became the plain hole; many windows are counted, and the Field Cell named is the one the finest of them needs. */
void Editor::blockout_window_fit_lines(const Blockout &b, std::vector<std::pair<std::string, bool>> &lines) const {
  auto metres = [](float v) { return strprintf(std::fabs(v * 100.0f - std::round(v * 100.0f)) < 0.005f ? "%.2f" : "%.3f", v); };
  for (const bool door : {false, true})  // (round 499: a door's line says Door - the windows' lines are as they were)
    for (const bool plain : {false, true}) {
      size_t n = 0;
      float finest = 1e30f;
      const GameObject *first = nullptr;
      for (const BlockoutWindowFit &w : b.state.window_fits) {
        if (w.plain != plain || w.door != door) continue;
        if (!n++) first = scene_ ? scene_->find(w.id) : nullptr;
        finest = std::min(finest, w.narrowest);
      }
      if (!n) continue;
      /* (A part shows from 1.5 cells: the Field Cell that shows the finest is a third less than its width, rounded down to the millimetre.) */
      const std::string cell = metres(b.cell), want = metres(std::max(0.001f, std::floor(finest / 1.5f * 1000.0f + 0.01f) / 1000.0f));
      const char *what = plain ? "its parts are finer" : door ? "its frame or ring is finer" : "its frame is finer", *kind = door ? "Door" : "Window", *kinds = door ? "doors" : "windows";
      if (n == 1)
        lines.push_back({strprintf("%s '%s': %s than Field Cell %s m - %slower Field Cell to %s m to see it", kind, first ? first->name.c_str() : (door ? "a door that is gone" : "a window that is gone"),
                                   what, cell.c_str(), plain ? "made as the plain hole; " : "", want.c_str()), true});
      else
        lines.push_back({strprintf("%zu %s: %s than Field Cell %s m - %slower Field Cell to %s m to see them", n, kinds, plain ? "their parts are finer" : door ? "their frames or rings are finer" : "their frames are finer",
                                   cell.c_str(), plain ? "made as plain holes; " : "", want.c_str()), true});
    }
}

std::vector<std::pair<std::string, bool>> Editor::blockout_status_lines(const GameObject &g, const Blockout &b) const {
  std::vector<std::pair<std::string, bool>> lines;
  const BlockoutStatus &s = b.state.status;
  for (const auto &j : blockout_jobs_)
    if (j->obj == g.id) lines.push_back({"Building...", false});
  /* Round 496 (ADR 0135): a scale already in the scene that shears a turned piece - the Blockout's, a folder's or a parent piece's (Apply Scale to Pieces),
   * or an object above the Blockout (not its to change). */
  std::vector<BlockoutUnevenScale> uneven;
  blockout_uneven_scales(g, uneven);
  for (const BlockoutUnevenScale &k : uneven) {
    const GameObject *o = scene_ ? scene_->find(k.id) : nullptr;
    lines.push_back({strprintf("'%s' is scaled unevenly: turned pieces are sheared%s", o ? o->name.c_str() : "an object", k.above ? " (above the Blockout: not the Blockout's to change)" : ""), true});
  }
  blockout_piece_stack_status(g, lines);  // (round 514: the pieces that carry modifiers)
  if (!s.built) {
    lines.push_back({"Not built yet", false});
    return lines;
  }
  if (s.pieces == 0) lines.push_back({"No pieces: the mesh is empty", false});
  else
    lines.push_back({strprintf("%zu of %zu pieces used, %zu faces, %s path, %.1f ms", s.used, s.pieces, s.faces, s.path.c_str(), s.ms), false});
  for (const auto &k : s.skipped) {
    const GameObject *p = scene_ ? scene_->find(k.first) : nullptr;
    lines.push_back({strprintf("Skipped '%s': %s", p ? p->name.c_str() : "a piece that is gone", k.second.c_str()), true});
  }
  for (const auto &k : s.notes) {
    const GameObject *p = scene_ ? scene_->find(k.first) : nullptr;
    lines.push_back({strprintf("'%s': %s", p ? p->name.c_str() : "a piece that is gone", k.second.c_str()), true});
  }
  blockout_window_fit_lines(b, lines);
  for (const std::string &c : b.state.swing_cuts) lines.push_back({c, true});  // (a preset's notes about the grid; round 499's swing-cut lines are gone with the cut: ADR 0136)
  if (!s.refusal.empty()) lines.push_back({"Refused: " + s.refusal + " - the last mesh stays", true});
  if (s.unresolved)
    lines.push_back({strprintf("%zu places could not be resolved (two corners in one cell): lower Field Cell", s.unresolved), true});
  if (s.blend_regions) lines.push_back({strprintf("%zu blends meshed in boxes of their own", s.blend_regions), false});
  if (s.blend_fallback)
    lines.push_back({strprintf("A blend's seam would not close (%zu more boxes tried): that cluster was built whole on the grid", s.seam_retries), true});
  if (s.blend_below) lines.push_back({strprintf("%zu pieces: Blend radius below a thirty-second of Field Cell: built unblended", s.blend_below), true});
  if (s.blend_dropped) lines.push_back({"A cluster's blends were left out: no grid closed over it with them (its joins are crisp)", true});
  return lines;
}

/* The Inspector's status lines under the Blockout's fields; each is recorded for the tests ("shown:" + text). */
void Editor::draw_blockout_extras(GameObject &g, Blockout &b, ui::Layout &lay) {
  auto &u = ui_;
  for (const auto &l : blockout_status_lines(g, b)) {
    const Recti row = lay.row(u.row_h() + u.px(2));
    u.label({row.x + u.px(4), row.y, row.w - u.px(8), row.h}, l.first, l.second ? u.theme.warning : u.theme.text_dim);
    u.tooltip(l.first);
    if (u.record_buttons) u.button_rects["shown:" + l.first] = row;
  }
  std::vector<BlockoutUnevenScale> uneven;
  blockout_uneven_scales(g, uneven);
  if (std::any_of(uneven.begin(), uneven.end(), [](const BlockoutUnevenScale &k) { return !k.above; })) {
    const Recti fold = lay.row(u.row_h() + u.px(2));
    if (u.button({fold.x + u.px(4), fold.y, fold.w - u.px(8), fold.h}, "Apply Scale to Pieces"))
      defer([this, id = g.id] {
        if (GameObject *o = scene_->find(id)) blockout_apply_scale({o}, "Apply Scale to Pieces");
      });
    u.tooltip("Moves the scale of this Blockout, of its folders and of its parent pieces into the pieces: their places and their numbers (Width, Radius...) grow\n"
              "and those objects' scale becomes 1 1 1, so no turned piece is sheared. The Blockout's own scale also grows every room's Wall, Floor and Ceiling and the\n"
              "Texture Scale, so the world looks as it did (one wall factor for an uneven X / Z scale). One Undo step.");
  }
  const Recti row = lay.row(u.row_h() + u.px(2));
  if (u.button({row.x + u.px(4), row.y, row.w - u.px(8), row.h}, "Bake to Mesh"))  // (round 482; after the panel's loop: it removes the component being drawn)
    defer([this, id = g.id] {
      if (GameObject *o = scene_->find(id)) blockout_bake(*o);
    });
  u.tooltip("Turns the Blockout into an ordinary mesh you can edit (Edit Mode, sculpting, modifiers): the pieces go under a disabled child and the\n"
            "mesh no longer follows them. Unbake brings the Blockout back from them. One Undo step.");
}

/* The scale of the roots, and of the folders and parent pieces between them and their pieces, moved into the pieces (ADR 0135): their places and numbers
 * take it, those objects' own scales go to 1 (a mirror stays), the world sizes stay. One undo step. False when there is no scale to move. */
bool Editor::blockout_apply_scale(const std::vector<GameObject *> &roots, const char *label) {
  BlockoutScaleSet set;
  blockout_scale_collect(roots, set);
  auto off_one = [](const Vec3 &s) {
    for (int a = 0; a < 3; a++)
      if (s[a] != 0.0f && std::fabs(std::fabs(s[a]) - 1.0f) > 1e-6f) return true;  // (a flat axis, 0, has nothing to move)
    return false;
  };
  bool any = false;
  for (const BlockoutScaleSet::Entry &e : set.entries) any = any || (e.role != BlockoutScaleSet::Leaf && off_one(e.local0.scale));
  if (!any) return false;
  blockout_scale_apply(*scene_, set, Mat4::identity(), true);
  edit_epoch::bump();
  update_blockouts(true);
  mark_changed(label);
  return true;
}

/* A wedge, stairs or arch turned `quarters` quarter turns round the surface's normal (its own up - an arch's own Z - as it stands), about the middle of its
 * footprint: a wedge or stairs about its own origin with its two footprint sizes swapped when the turn is odd, so the world box stays; an arch about the middle
 * of its span x radius box. False for a piece that has no such direction. No undo step: the callers make theirs. */
bool Editor::blockout_turn_piece(GameObject &g, int quarters) {
  BlockoutPiece *bp = g.get<BlockoutPiece>();
  if (!bp || !blockout_turnable(*bp)) return false;
  quarters = ((quarters % 4) + 4) % 4;
  if (!quarters) return true;
  {  // (a scale an older scene left on the piece goes into its numbers first: the turn swaps the numbers and the box must stay the same)
    BlockoutScaleSet set;
    blockout_scale_collect({&g}, set);
    const Vec3 sc = g.local().scale;
    const bool scaled = std::fabs(std::fabs(sc.x) - 1.0f) > 1e-6f || std::fabs(std::fabs(sc.y) - 1.0f) > 1e-6f || std::fabs(std::fabs(sc.z) - 1.0f) > 1e-6f;
    if (scaled && !set.empty()) blockout_scale_apply(*scene_, set, Mat4::identity(), true);
  }
  const Mat4 w = g.world_matrix();
  const bool arch = bp->shape == (int)blockout::Shape::Arch;
  const Vec3 axis = normalize(w.dir(arch ? Vec3(0.0f, 0.0f, 1.0f) : Vec3(0.0f, 1.0f, 0.0f)));
  const Quat q = Quat::axis_angle(axis, (float)quarters * 0.5f * kPi);
  const Vec3 pos = g.world_position(), pivot = arch ? w.point(Vec3(0.0f, bp->radius * 0.5f, 0.0f)) : pos;
  g.set_world_rotation(normalize(q * g.world_rotation()));
  g.set_world_position(pivot + q.rotate(pos - pivot));
  if (!arch && !bp->is_preset() && (quarters & 1)) std::swap(bp->size.x, bp->size.z);
  return true;
}

/* blockout turn [quarters]: the selected wedges, stairs and arches turned (default one quarter turn; negative turns back), one undo step. */
void Editor::blockout_turn_command(const std::vector<std::string> &t) {
  char *end = nullptr;
  const long n = t.size() > 2 ? std::strtol(t[2].c_str(), &end, 10) : 1;
  if (t.size() > 3 || (t.size() > 2 && (t[2].empty() || *end != '\0'))) return Log::warn("blockout turn [quarters]: a whole number of quarter turns (default 1)");
  bool any = false;
  for (GameObject *g : selected_objects(false)) {
    const BlockoutPiece *bp = g->get<BlockoutPiece>();
    if (!bp || !blockout_of_piece(*g) || !blockout_turnable(*bp)) continue;
    any = true;
    blockout_turn_piece(*g, (int)(n % 4));
  }
  if (!any) return Log::warn("blockout turn: select a wedge, stairs or arch piece first");
  if (n % 4 == 0) return;  // (no undo step for nothing)
  update_blockouts(true);
  mark_changed("Turn Blockout Piece");
}

/* The Inspector's extras for a piece (round 496): Turn 90 for a wedge, stairs or arch, and a scale an older scene left on it, folded into its numbers. */
void Editor::draw_blockout_piece_extras(GameObject &g, BlockoutPiece &bp, ui::Layout &lay) {
  auto &u = ui_;
  if (blockout_turnable(bp) && blockout_of_piece(g)) {
    const Recti turn = lay.row(u.row_h() + u.px(2));
    if (u.button({turn.x + u.px(4), turn.y, turn.w - u.px(8), turn.h}, "Turn 90"))
      defer([this, id = g.id] {
        GameObject *o = scene_->find(id);
        if (!o || !blockout_turn_piece(*o, 1)) return;
        update_blockouts(true);
        mark_changed("Turn Blockout Piece");
      });
    /* (Round 513, HCI: the key comes from the keymap - Ctrl+F9 in presets where F9 is a mode - and no "Console:" tail: the button is the control.) */
    u.tooltip("Turns the piece a quarter turn round the surface's normal, about the middle of its footprint: a wedge's or stairs' two footprint sizes swap so it\n"
              "stays in the same box, an arch spans the other way. One Undo step. The same turn is the Turn row of Adjust Last Operation (" + hint_key("edit.adjust_last", "F9") +
              ") right after the piece is drawn.");
  }
  const Vec3 s = g.local().scale;
  const bool numbers = bp.shape != (int)blockout::Shape::Mesh || bp.opening != 0;
  const bool scaled = std::fabs(std::fabs(s.x) - 1.0f) > 1e-6f || std::fabs(std::fabs(s.y) - 1.0f) > 1e-6f || std::fabs(std::fabs(s.z) - 1.0f) > 1e-6f;
  if (!numbers || !scaled || !blockout_of_piece(g)) return;
  const Recti row = lay.row(u.row_h() + u.px(2));
  if (u.button({row.x + u.px(4), row.y, row.w - u.px(8), row.h}, "Apply Scale to Size"))
    defer([this, id = g.id] {
      if (GameObject *o = scene_->find(id)) blockout_apply_scale({o}, "Apply Scale to Size");
    });
  u.tooltip("Moves this object's Scale into the piece's own numbers (Width, Height, Depth, Radius...): the same shape, Scale 1 1 1. On a parent piece it also moves the Scale of the folders and pieces below it. One Undo step.");
}

/* blockout applyscale [size]: Apply Scale to Pieces - the selected Blockouts' (and the selected pieces' Blockouts'; none selected: every one) scale and that of the
 * folders and parent pieces in them moved into the pieces; `size`: Apply Scale to Size of the selected pieces. */
void Editor::blockout_apply_scale_command(const std::vector<std::string> &t) {
  const bool size = t.size() > 2 && to_lower(t[2]) == "size";
  if (t.size() > 2 && !size) return Log::warn("blockout applyscale [size]: Apply Scale to Pieces for the Blockouts, or Apply Scale to Size for the selected pieces");
  std::vector<GameObject *> roots;
  auto note = [&](GameObject *g) {
    if (g && std::find(roots.begin(), roots.end(), g) == roots.end()) roots.push_back(g);
  };
  for (GameObject *g : selected_objects(false)) {
    if (size) note(g->get<BlockoutPiece>() ? g : nullptr);
    else note(g->get<Blockout>() ? g : blockout_of_piece(*g));
  }
  const bool nothing_selected = selected_objects(false).empty();
  if (roots.empty() && !size && nothing_selected) scene_->for_each([&](GameObject &g) { if (g.get<Blockout>()) note(&g); });
  if (roots.empty() && !size && !nothing_selected) return Log::warn("blockout applyscale: the selection holds no Blockout and no piece (select none to take every Blockout)");
  if (roots.empty()) return Log::warn("blockout applyscale size: select a piece first");
  if (!blockout_apply_scale(roots, size ? "Apply Scale to Size" : "Apply Scale to Pieces")) Log::info("blockout applyscale: no scale to move");
}

/* The Inspector's extras for a baked object (round 482): what it is and the Unbake button. */
void Editor::draw_blockout_baked_extras(GameObject &g, BlockoutBaked &, ui::Layout &lay) {
  auto &u = ui_;
  const bool edited = blockout_baked_edited(g);
  const Recti note = lay.row(u.row_h() + u.px(2));
  const std::string text = edited ? "Baked to a mesh, edited since: Unbake asks first" : "Baked to a mesh: the pieces are under '" + std::string(kBlockoutPiecesName) + "'";
  u.label({note.x + u.px(4), note.y, note.w - u.px(8), note.h}, text, edited ? u.theme.warning : u.theme.text_dim);
  const Recti row = lay.row(u.row_h() + u.px(2));
  if (u.button({row.x + u.px(4), row.y, row.w - u.px(8), row.h}, "Unbake"))
    defer([this, id = g.id] {
      if (GameObject *o = scene_->find(id)) blockout_unbake(*o, false);
    });
  u.tooltip("Brings the Blockout back: its component, the pieces from under the disabled child, and the mesh built from them. A mesh edited since the\n"
            "bake is lost (it asks first). One Undo step.");
}

/* The room fields of the selected pieces (the rooms among them): `blockout set wall|floor|ceiling <metres>`, `walls merge|keep`,
 * `opentop on|off`. */
void Editor::blockout_set_command(const std::vector<std::string> &t) {
  const std::string key = to_lower(t[2]), value = t.size() > 3 ? to_lower(t[3]) : "";
  if (key == "blend") {  /* `blockout set blend <metres>` (round 481): the Blend radius of the selected pieces that take one, one undo step */
    const bool is_number = !value.empty() && (std::isdigit((unsigned char)value[0]) || value[0] == '-' || value[0] == '.' || value[0] == '+');
    char *end = nullptr;
    const float metres = (float)std::strtod(value.c_str(), &end);
    if (!is_number || *end != '\0' || !finite_bits(metres) || metres < 0.0f) return Log::warn("blockout set blend <metres>: a Blend radius is 0 metres or more");
    bool any = false, changed = false;
    for (GameObject *g : selected_objects(false)) {
      BlockoutPiece *bp = g->get<BlockoutPiece>();
      if (!bp || !blockout_of_piece(*g) || !bp->takes_blend()) continue;
      any = true;
      const float to = std::min(100.0f, metres);
      changed = changed || bp->blend != to;
      bp->blend = to;
    }
    if (!any) return Log::warn("blockout set blend: select a box, cylinder or sphere piece that adds or subtracts (rooms, Intersect pieces and other shapes take no Blend radius)");
    if (!changed) return;  // (no undo step for nothing)
    mark_changed("Edit Blockout Blend");
    update_blockouts(true);
    return;
  }
  std::vector<BlockoutPiece *> rooms;
  for (GameObject *g : selected_objects(false))
    if (BlockoutPiece *bp = g->get<BlockoutPiece>())
      if (bp->is_room()) rooms.push_back(bp);
  if (rooms.empty()) return Log::warn("blockout set %s: select a room piece first", key.c_str());
  const bool thickness = key == "wall" || key == "floor" || key == "ceiling";
  const bool is_number = !value.empty() && (std::isdigit((unsigned char)value[0]) || value[0] == '-' || value[0] == '.');
  if (thickness && !is_number) return Log::warn("blockout set %s <metres>", key.c_str());
  if (key == "walls" && value != "merge" && value != "keep" && value != "0" && value != "1") return Log::warn("blockout set walls merge|keep");
  if (key == "opentop" && value != "on" && value != "off" && value != "true" && value != "false" && value != "1" && value != "0" && value != "yes" && value != "no")
    return Log::warn("blockout set opentop on|off");
  const float metres = thickness ? (float)std::atof(value.c_str()) : 0.0f;
  if (thickness && (!finite_bits(metres) || metres < 0.0f)) return Log::warn("blockout set %s: a thickness is 0 metres or more", key.c_str());
  const bool on = value == "on" || value == "true" || value == "1" || value == "yes";
  bool changed = false;
  for (BlockoutPiece *bp : rooms) {
    const BlockoutPiece before = *bp;
    if (key == "wall") bp->wall = metres;
    else if (key == "floor") bp->floor = metres;
    else if (key == "ceiling") bp->ceiling = metres;
    else if (key == "walls") bp->walls = value == "keep" || value == "1" ? 1 : 0;
    else bp->open_top = on;
    changed = changed || before.wall != bp->wall || before.floor != bp->floor || before.ceiling != bp->ceiling || before.walls != bp->walls || before.open_top != bp->open_top;
  }
  if (!changed) return;  // (no undo step for nothing)
  mark_changed("Edit Blockout Room");
  update_blockouts(true);
}

namespace {

bool window_number(const std::string &word, float &out) {
  const bool is_number = !word.empty() && (std::isdigit((unsigned char)word[0]) || word[0] == '-' || word[0] == '.' || word[0] == '+');
  char *end = nullptr;
  const double v = std::strtod(word.c_str(), &end);
  if (!is_number || *end != '\0' || !std::isfinite(v)) return false;
  out = (float)std::max(-1e30, std::min(1e30, v));
  return true;
}

bool window_switch(const std::string &word, bool &on) {
  if (word == "on" || word == "true" || word == "1" || word == "yes") return on = true, true;
  if (word == "off" || word == "false" || word == "0" || word == "no") return on = false, true;
  return false;
}

}  // namespace

namespace {

bool opening_numbers_differ(const BlockoutPiece &a, const BlockoutPiece &b) {
  /* (A NaN is the same number as a NaN - `!=` says it is not, so a NaN Open Amount would be a change on every frame.) */
  auto ne = [](float x, float y) { return x != y && !(x != x && y != y); };
  return a.style != b.style || ne(a.width, b.width) || ne(a.head, b.head) || ne(a.frame_width, b.frame_width) || ne(a.frame_depth, b.frame_depth) || a.panes_across != b.panes_across ||
         a.panes_up != b.panes_up || a.glass != b.glass || a.sill_board != b.sill_board || ne(a.sill_overhang, b.sill_overhang) || a.leaf != b.leaf || a.wings != b.wings ||
         ne(a.leaf_thickness, b.leaf_thickness) || ne(a.open_amount, b.open_amount) || a.hinge_side != b.hinge_side || a.slide_direction != b.slide_direction ||
         ne(a.drum_radius, b.drum_radius);
}

/* "left" or "right" (also "l", "r", "-x", "+x"); false: neither. */
bool door_side(const std::string &word, int &side) {
  if (word == "left" || word == "l" || word == "-x") return side = 0, true;
  if (word == "right" || word == "r" || word == "+x") return side = 1, true;
  return false;
}

}  // namespace

/* blockout set style <name> | frame <width> [depth] | panes <across> <up> | glass on|off | sillboard on|off [overhang] (round 498, task 0482) and, for doors (round 499,
 * task 0483), leaf on|off [thickness] | open <0..1> | hinge left|right | slide left|right | drum <radius>: a styled opening's numbers on the selected windows or doors, one
 * undo step; a bad argument warns and changes nothing. Style keeps every other number (the ones the new style does not read stay in the piece for when it is used again).
 * `style` and `frame` are every selected opening's (a style word changes those whose kind has it); the rest belong to one kind. */
void Editor::blockout_window_command(const std::vector<std::string> &t) {
  const std::string key = to_lower(t[2]);
  std::vector<std::string> v;
  for (size_t i = 3; i < t.size(); i++) v.push_back(to_lower(t[i]));
  int across = 0, up = 0, side = 0;
  float width = 0.0f, depth = -1.0f, overhang = -1.0f, amount = 0.0f;
  bool on = false;
  std::string usage;
  const bool door_key = key == "leaf" || key == "open" || key == "hinge" || key == "slide" || key == "drum";
  if (key == "style") {
    const bool known = !v.empty() && (blockout_window_style_from_word(v[0]) >= 0 || blockout_door_style_from_word(v[0]) >= 0);
    const bool only = known && v.size() == 2 && v[1] == "only" && blockout_window_style_from_word(v[0]) == kWindowOpeningOnly;
    if (!known || v.size() > 2 || (v.size() == 2 && !only)) usage = std::string("blockout set style ") + blockout_style_words(2) + "  (a door's: " + blockout_style_words(1) + ")";
  }
  else if (key == "frame") {
    if (v.empty() || v.size() > 2 || !window_number(v[0], width) || !(width > 0.0f) || (v.size() == 2 && (!window_number(v[1], depth) || !(depth > 0.0f))))
      usage = "blockout set frame <width> [depth]: metres, above 0";
  }
  else if (key == "panes") {
    float a = 0.0f, b = 0.0f;
    if (v.size() != 2 || !window_number(v[0], a) || !window_number(v[1], b) || a != std::floor(a) || b != std::floor(b)) usage = "blockout set panes <across> <up>: whole numbers (1 to 12)";
    across = (int)std::min(12.0f, std::max(1.0f, a)), up = (int)std::min(12.0f, std::max(1.0f, b));
  }
  else if (key == "glass") {
    if (v.size() != 1 || !window_switch(v[0], on)) usage = "blockout set glass on|off";
  }
  else if (key == "leaf") {
    if (v.empty() || v.size() > 2 || !window_switch(v[0], on) || (v.size() == 2 && (!window_number(v[1], amount) || !(amount > 0.0f)))) usage = "blockout set leaf on|off [thickness metres, above 0]";
  }
  else if (key == "open") {
    if (v.size() != 1 || !window_number(v[0], amount) || amount < 0.0f || amount > 1.0f) usage = "blockout set open <0 to 1>: 0 closed, 1 open";
  }
  else if (key == "hinge" || key == "slide") {
    if (v.size() != 1 || !door_side(v[0], side)) usage = "blockout set " + key + " left|right";
  }
  else if (key == "drum") {
    if (v.size() != 1 || !window_number(v[0], amount) || !(amount > 0.0f)) usage = "blockout set drum <radius metres, above 0>";
  }
  else {  // sillboard
    if (v.empty() || v.size() > 2 || !window_switch(v[0], on) || (v.size() == 2 && (!window_number(v[1], overhang) || overhang < 0.0f))) usage = "blockout set sillboard on|off [overhang metres]";
  }
  if (!usage.empty()) return Log::warn("%s", usage.c_str());
  std::vector<BlockoutPiece *> windows, doors;
  for (GameObject *g : selected_objects(false)) {
    BlockoutPiece *bp = g->get<BlockoutPiece>();
    if (bp && blockout_of_piece(*g) && bp->opening == 2) windows.push_back(bp);
    if (bp && blockout_of_piece(*g) && bp->opening == 1) doors.push_back(bp);
  }
  /* `style` and `frame` are every selected opening's: a style word changes the openings whose kind has that word (a door word the doors, a window word the windows), a word
   * no selected kind has is refused with the words that fit; the other keys belong to one kind. */
  const bool both = key == "style" || key == "frame";
  const bool for_doors = door_key || (both && windows.empty());
  std::vector<BlockoutPiece *> pieces = both ? windows : for_doors ? doors : windows;
  if (both) pieces.insert(pieces.end(), doors.begin(), doors.end());
  if (pieces.empty()) return Log::warn("blockout set %s: select a %s piece first (blockout add %s)", key.c_str(), both ? "door or window" : for_doors ? "door" : "window", for_doors ? "door" : "window");
  if (key == "style") {
    const bool any_knows = std::any_of(pieces.begin(), pieces.end(), [&](const BlockoutPiece *p) { return blockout_style_from_word(p->opening, v[0]) >= 0; });
    if (!any_knows) {
      std::string words;
      if (!windows.empty()) words += std::string("a window's: ") + blockout_style_words(2);
      if (!doors.empty()) words += std::string(words.empty() ? "" : "; ") + "a door's: " + blockout_style_words(1);
      return Log::warn("blockout set style: '%s' is no style of the selected %s - %s", v[0].c_str(), windows.empty() ? "door" : doors.empty() ? "window" : "openings", words.c_str());
    }
  }
  bool changed = false;
  auto clamp_m = [](float m) { return std::min(kBlockoutMaxSize, std::max(kBlockoutMinSize, m)); };
  for (BlockoutPiece *bp : pieces) {
    const BlockoutPiece before = *bp;
    if (key == "style") {
      const int style = blockout_style_from_word(bp->opening, v[0]);
      if (style >= 0) blockout_opening_set_style(*bp, style);  // (a piece of a kind that has no such word is left as it is)
    }
    else if (key == "frame") bp->frame_width = clamp_m(width), bp->frame_depth = depth > 0.0f ? clamp_m(depth) : bp->frame_depth;
    else if (key == "panes") bp->panes_across = across, bp->panes_up = up;
    else if (key == "glass") bp->glass = on;
    else if (key == "leaf") (bp->style == kDoorRevolving ? bp->wings : bp->leaf) = on, bp->leaf_thickness = amount > 0.0f ? clamp_m(amount) : bp->leaf_thickness;  // (round 515: a Revolving door's Leaf is its wings)
    else if (key == "open") bp->open_amount = amount;
    else if (key == "hinge") bp->hinge_side = side;
    else if (key == "slide") bp->slide_direction = side;
    else if (key == "drum") bp->drum_radius = clamp_m(amount);
    else bp->sill_board = on, bp->sill_overhang = overhang >= 0.0f ? std::min(kBlockoutMaxSize, overhang) : bp->sill_overhang;
    changed = changed || opening_numbers_differ(before, *bp);
  }
  if (!changed) return;  // (no undo step for nothing)
  mark_changed(for_doors && !both ? "Edit Blockout Door" : "Edit Blockout Window");
  update_blockouts(true);
}

/* The project path of the material a console word names: "none" (or "default" / "-") is no material; else a .mat of the project by its path (with or
 * without "Assets/" and ".mat") or by its name. False, with a warning, for one that is not there. */
bool Editor::blockout_material_path(const std::string &word, std::string &out) {
  const std::string lower = to_lower(word);
  if (lower == "none" || lower == "default" || lower == "-") return out.clear(), true;
  auto stripped = [](std::string s) {
    s = to_lower(s);
    std::replace(s.begin(), s.end(), '\\', '/');
    if (s.size() > 4 && s.compare(s.size() - 4, 4, ".mat") == 0) s.resize(s.size() - 4);
    if (s.rfind("assets/", 0) == 0) s.erase(0, 7);
    if (s.rfind("materials/", 0) == 0) s.erase(0, 10);
    return s;
  };
  const std::string want = stripped(word);
  const std::vector<std::string> &paths = material_asset_paths();
  for (const std::string &p : paths)
    if (stripped(p) == want) return out = p, true;
  for (const std::string &p : paths)
    if (to_lower(fs::stem(p)) == want) return out = p, true;
  /* A path typed in full for a file the listing (kept for a couple of seconds) does not hold yet. */
  std::string typed = word;
  std::replace(typed.begin(), typed.end(), '\\', '/');
  for (const std::string &candidate : {typed, typed + ".mat", "Assets/" + typed, "Assets/" + typed + ".mat"})
    if (candidate.size() > 4 && candidate.compare(candidate.size() - 4, 4, ".mat") == 0 && material_asset(candidate)) return out = candidate, true;
  Log::warn("blockout set material: no material asset '%s' in the project (Assets > Create Material makes one; 'none' clears the field)", word.c_str());
  return false;
}

/* blockout set material floor|walls|ceiling|outside|cut|solid <material or none> and blockout set texscale <metres> (round 482): on the selected
 * pieces (their own material, for the classes they have), else on the selected Blockouts - every one with none selected. One undo step. */
void Editor::blockout_surface_command(const std::vector<std::string> &t) {
  const std::string key = to_lower(t[2]);
  std::vector<GameObject *> blockouts;
  std::vector<BlockoutPiece *> pieces;
  auto note = [&](GameObject *o) {
    if (o && std::find(blockouts.begin(), blockouts.end(), o) == blockouts.end()) blockouts.push_back(o);
  };
  for (GameObject *g : selected_objects(false)) {
    if (BlockoutPiece *bp = g->get<BlockoutPiece>(); bp && blockout_of_piece(*g)) pieces.push_back(bp), note(blockout_of_piece(*g));  // (a piece stands for its own Blockout)
    if (g->get<Blockout>()) note(g);
  }
  if (key == "material" && t.size() > 3 && !pieces.empty()) blockouts.clear();  // (selected pieces: their own materials)
  else pieces.clear();
  if (blockouts.empty() && pieces.empty()) scene_->for_each([&](GameObject &g) { if (g.get<Blockout>()) blockouts.push_back(&g); });
  if (blockouts.empty() && pieces.empty()) return Log::warn("blockout set %s: no Blockout in the scene", key.c_str());
  bool changed = false;
  if (key == "texscale") {
    const std::string value = t.size() > 3 ? t[3] : "";
    char *end = nullptr;
    const float metres = (float)std::strtod(value.c_str(), &end);
    if (value.empty() || *end != '\0' || !finite_bits(metres) || !(metres > 0.0f)) return Log::warn("blockout set texscale <metres>: the metres one texture tile covers, above 0");
    const float to = std::min(1000.0f, std::max(0.001f, metres));
    for (GameObject *g : blockouts) {
      Blockout *b = g->get<Blockout>();
      changed = changed || b->texture_scale != to;
      b->texture_scale = to;
    }
    if (!changed) return;  // (no undo step for nothing)
    mark_changed("Edit Blockout Texture Scale");
    return update_blockouts(true);
  }
  int cls = -1;
  const std::string which = t.size() > 3 ? to_lower(t[3]) : "";
  for (int k = 0; k < kBlockoutClasses; k++)
    if (which == to_lower(kBlockoutClassNames[k]) || (k == kClassWalls && which == "wall")) cls = k;
  if (t.size() < 5 || cls < 0) return Log::warn("blockout set material floor|walls|ceiling|outside|cut|solid|glass|leaf|roof <material name or path, or none>");
  std::string name;
  for (size_t i = 4; i < t.size(); i++) name += (name.empty() ? "" : " ") + t[i];
  std::string path;
  if (!blockout_material_path(name, path)) return;
  size_t fits = 0;
  for (GameObject *g : blockouts) {
    Blockout *b = g->get<Blockout>();
    changed = changed || b->material[cls] != path;
    b->material[cls] = path;
  }
  for (BlockoutPiece *bp : pieces) {
    if (!bp->has_class(cls)) continue;
    fits++;
    changed = changed || bp->material[cls] != path;
    bp->material[cls] = path;
  }
  if (!pieces.empty() && !fits) return Log::warn("blockout set material %s: the selected pieces make no %s faces (a room: Floor, Walls, Ceiling, Outside; a Subtract piece: Cut; an Add piece: Solid; a styled window: Cut, Solid and Glass - Glass only with its Glass on; a styled door: Cut, Solid and Leaf - Leaf only with its Leaf on)", which.c_str(), which.c_str());
  if (!changed) return;
  mark_changed("Edit Blockout Material");
  update_blockouts(true);
}

/* blockout set <word> <value> for an architectural preset (round 500, task 0484): the numbers of the selected presets that read that word (blockout_preset_set holds each to its range), one undo
 * step; a preset that does not read it is left as it is. False: the word is no preset's (the caller goes on). */
bool Editor::blockout_preset_command(const std::vector<std::string> &t) {
  const std::string key = to_lower(t[2]);
  const std::vector<std::string> values(t.begin() + std::min<size_t>(3, t.size()), t.end());
  bool ok = false;
  BlockoutPiece scratch;
  if (!blockout_preset_set(scratch, key, values, ok)) return false;
  std::vector<BlockoutPiece *> presets;
  for (GameObject *g : selected_objects(false))
    if (BlockoutPiece *bp = g->get<BlockoutPiece>(); bp && bp->is_preset() && blockout_of_piece(*g)) presets.push_back(bp);
  if (presets.empty()) {
    Log::warn("blockout set %s: select a preset piece first (blockout add column|pilaster|beam|railing|roof|stairs l|stairs u|stairs spiral|arcade)", key.c_str());
    return true;
  }
  bool any_ok = false, changed = false;
  for (BlockoutPiece *bp : presets) {
    const BlockoutPiece before = *bp;
    bool one = false;
    blockout_preset_set(*bp, key, values, one);
    any_ok = any_ok || one;
    changed = changed || before.square != bp->square || before.base_height != bp->base_height || before.capital_height != bp->capital_height || before.base_width != bp->base_width ||
              before.capital_width != bp->capital_width || before.taper != bp->taper || before.length != bp->length || before.post_spacing != bp->post_spacing ||
              before.post_size != bp->post_size || before.rail_size != bp->rail_size || before.rail_height != bp->rail_height || before.bottom_rail != bp->bottom_rail ||
              before.roof_style != bp->roof_style || before.span != bp->span || before.pitch != bp->pitch || before.overhang != bp->overhang || before.thickness != bp->thickness ||
              before.total_height != bp->total_height || before.stair_width != bp->stair_width || before.turn != bp->turn || before.column_width != bp->column_width ||
              before.arch_thickness != bp->arch_thickness || before.count != bp->count || before.radius != bp->radius || before.height != bp->height || before.segments != bp->segments ||
              before.steps != bp->steps || before.fill_under != bp->fill_under || before.inner_radius != bp->inner_radius || before.arch_rise != bp->arch_rise || before.depth != bp->depth;
  }
  if (!any_ok) {
    Log::warn("blockout set %s: that number is not one the selected presets have, or its value is wrong (blockout set %s)", key.c_str(), blockout_preset_set_words());
    return true;
  }
  if (!changed) return true;  // (no undo step for nothing)
  mark_changed("Edit Blockout Preset");
  update_blockouts(true);
  return true;
}

/* blockout outline <x z> <x z> <x z> ...: the selected Outline piece's corners (metres in the piece's own XZ plane, 3 to 256), as dragging its
 * point handles would set them. With no numbers: the corners it has. */
void Editor::blockout_outline_command(const std::vector<std::string> &t) {
  GameObject *g = active_object();
  BlockoutPiece *bp = g ? g->get<BlockoutPiece>() : nullptr;
  const bool preset_outline = bp && bp->is_preset() && (bp->preset == kPresetRailing || bp->preset == kPresetRoof);  // (round 500: the plan a railing runs along and a roof covers)
  if (!bp || !blockout_of_piece(*g) || (bp->shape != (int)blockout::Shape::Outline && !preset_outline) || (bp->opening != 0))
    return Log::warn("blockout outline <x z> <x z> <x z> ...: select an Outline piece (blockout add outline), a Railing or a Roof first");
  if (t.size() <= 2) return Log::info("outline of '%s': %s", g->name.c_str(), blockout_outline_text(bp->outline).c_str());
  std::vector<Vec2> pts;
  for (size_t i = 2; i < t.size(); i++) {
    const bool number = !t[i].empty() && (std::isdigit((unsigned char)t[i][0]) || t[i][0] == '-' || t[i][0] == '.' || t[i][0] == '+');
    const float v = (float)std::atof(t[i].c_str());
    if (!number || !finite_bits(v)) return Log::warn("blockout outline: '%s' is not a number", t[i].c_str());
    if (i % 2 == 0) pts.push_back(Vec2(v, 0.0f));
    else pts.back().y = v;
  }
  if ((t.size() - 2) % 2 != 0 || pts.size() < 3 || pts.size() > 256)
    return Log::warn("blockout outline: give 3 to 256 corners as x z pairs (%zu numbers given)", t.size() - 2);
  bool same = pts.size() == bp->outline.size();
  for (size_t i = 0; same && i < pts.size(); i++) same = pts[i].x == bp->outline[i].x && pts[i].y == bp->outline[i].y;
  if (same) return;  // (no undo step for nothing)
  bp->outline = pts;
  mark_changed("Edit Blockout Outline");
  update_blockouts(true);
}

/* blockout new | add box|cylinder|sphere|wedge|stairs|arch|outline|mesh [subtract|intersect] [sizes] | add room | add door|window | outline <x z>... | set cell|method|wall|floor|ceiling|walls|opentop <value> | rebuild | wait | info */
void Editor::blockout_command(const std::vector<std::string> &t) {
  const std::string sub = t.size() > 1 ? to_lower(t[1]) : "info";
  auto usage = [] {
    Log::info("blockout new | add box|cylinder|sphere|wedge|stairs|arch|outline|mesh [subtract|intersect] [x y z | radius height | radius | stairs: x y z steps | "
              "arch: radius inner angle depth | outline: [height] x z x z ... | mesh <object>] | add room [box|cylinder|wedge|outline] [sizes] | "
              "add door|window [width head [sill]] | add door [opening|single|double|sliding|arched|revolving] [width head] | add window [opening|single|double|arched|fixed|grid|round] [width head [sill]] | outline <x z> <x z> <x z> ... | set cell|method|detail|blend|wall|floor|ceiling|walls|opentop <value> | handle <side> <metres> [both] | "
              "room <x0 z0 x1 z1 height> | room circle <x z radius height> | roomopt circle|solid|subtract on|off, piece <word>, operation <word>, wall <metres> | "
              "draw room|box|cylinder|wedge|stairs|arch add|subtract|intersect|auto at <x y z> normal <x y z> size <du dz height> | "
              "applyscale [size] (the Blockouts' scale into their pieces; size: the selected pieces' own) | "
              "turn [quarters] (the selected wedges, stairs and arches a quarter turn round the surface's normal) | "
              "set material floor|walls|ceiling|outside|cut|solid|glass|leaf|roof <material or none> | set texscale <metres> | set style|frame|panes|glass|sillboard (a window's, round 498) | set style|frame|leaf|open|hinge|slide|drum (a door's, round 499) | leaf select|door (the selected doors' leaf objects, and back to the doors, round 515) | bake (the selected Blockout or a piece's; nothing selected bakes nothing) | unbake [force] (the selected baked objects; nothing selected unbakes nothing) | rebuild | wait | info");
  };
  /* The Blockouts a command is about: the selected ones and the ones the selected pieces belong to; with none, every one. */
  std::vector<GameObject *> targets;
  auto note = [&](GameObject *g) {
    if (g && std::find(targets.begin(), targets.end(), g) == targets.end()) targets.push_back(g);
  };
  for (GameObject *g : selected_objects(false)) note(g->get<Blockout>() ? g : blockout_of_piece(*g));
  if (targets.empty()) scene_->for_each([&](GameObject &g) { if (g.get<Blockout>()) note(&g); });
  if (sub == "new") {
    create_object("Blockout");
  }
  else if (sub == "add") {
    std::string what;
    for (size_t i = 2; i < t.size(); i++) what += (what.empty() ? "" : " ") + t[i];
    create_blockout_piece(what);
  }
  else if (sub == "handle") blockout_handle_command(t);   // (round 479)
  else if (sub == "outline") blockout_outline_command(t);  // (round 480)
  else if (sub == "room" || sub == "draw") room_command(t);  // (round 495: `draw` - any piece on any surface point)
  else if (sub == "roomopt") room_options_command(t);
  else if (sub == "applyscale") blockout_apply_scale_command(t);  // (round 496)
  else if (sub == "leaf") blockout_leaf_command(t);  // (round 515)
  else if (sub == "turn") blockout_turn_command(t);
  else if (sub == "set") {
    const std::string key = t.size() > 2 ? to_lower(t[2]) : "", value = t.size() > 3 ? to_lower(t[3]) : "";
    if (key == "wall" || key == "floor" || key == "ceiling" || key == "walls" || key == "opentop" || key == "blend") return blockout_set_command(t);
    if (key == "material" || key == "texscale") return blockout_surface_command(t);  // (round 482)
    if (!key.empty() && blockout_preset_command(t)) return;  // (round 500: a preset's number words)
    if (key == "style" || key == "frame" || key == "panes" || key == "glass" || key == "sillboard" || key == "leaf" || key == "open" || key == "hinge" || key == "slide" || key == "drum")
      return blockout_window_command(t);  // (round 498; doors' words round 499)
    if ((key != "cell" && key != "method" && key != "detail") || value.empty())
      return Log::warn("blockout set cell <metres> | method auto|exact|field | detail low|medium|high | blend <metres> | wall|floor|ceiling <metres> | walls merge|keep | opentop on|off | "
                       "material floor|walls|ceiling|outside|cut|solid|glass|leaf|roof <material or none> | texscale <metres> | style <name> | frame <width> [depth] | panes <across> <up> | "
                       "glass on|off | sillboard on|off [overhang] | a door's: leaf on|off [thickness] | open <0..1> | hinge left|right | slide left|right | drum <radius> | "
                       "a preset's: %s", blockout_preset_set_words());
    const bool detail_ok = value == "low" || value == "medium" || value == "high" || value == "0" || value == "1" || value == "2";
    if (key == "detail" && !detail_ok) return Log::warn("blockout set detail low|medium|high");
    if (targets.empty()) return Log::warn("blockout set: no Blockout in the scene");
    for (GameObject *g : targets) {
      Blockout *b = g->get<Blockout>();
      if (key == "cell") b->cell = (float)std::atof(value.c_str());
      else if (key == "detail") b->blend_detail = value == "low" || value == "0" ? 0 : value == "high" || value == "2" ? 2 : 1;
      else b->method = value == "exact" || value == "1" ? 1 : value == "field" || value == "2" ? 2 : 0;
    }
    mark_changed("Edit Blockout");
    update_blockouts(true);
  }
  else if (sub == "rebuild") {
    if (targets.empty()) return Log::warn("blockout rebuild: no Blockout in the scene");
    blockout_jobs_wait();  // (a running build would bring its cache back, and its hash would pass for the one asked for here)
    for (GameObject *g : targets) {
      Blockout *b = g->get<Blockout>();
      b->state.hash_valid = false;  // (the cache too: a build from scratch)
      b->state.cache.reset();
    }
    update_blockouts(true);
  }
  else if (sub == "wait") {
    blockout_jobs_wait();
    blockout_stacks_settle();  // (round 502: and the modifier stacks over the meshes that landed - a script's next command reads the drawn mesh)
  }
  else if (sub == "join") blockout_join_command(t);  // (round 501: blockout_join.cpp)
  else if (sub == "bake") {  // (round 482)
    std::vector<GameObject *> live;  // (the selected Blockouts and those of the selected pieces: with nothing selected nothing is baked - a bake is not one to run on everything)
    const std::vector<GameObject *> sel = selected_objects(false);
    for (GameObject *g : targets)
      if (g->get<Blockout>() && std::any_of(sel.begin(), sel.end(), [&](GameObject *s) { return s == g || blockout_of_piece(*s) == g; })) live.push_back(g);
    if (live.empty()) return Log::warn("blockout bake: select a Blockout (or one of its pieces) first");
    for (GameObject *g : live) blockout_bake(*g);
  }
  else if (sub == "unbake") {
    const bool force = t.size() > 2 && to_lower(t[2]) == "force";
    if (t.size() > 2 && !force) return Log::warn("blockout unbake [force]: force skips the question when the baked mesh was edited");
    std::vector<GameObject *> baked;
    for (GameObject *g : selected_objects(false))
      if (g->get<BlockoutBaked>()) baked.push_back(g);
    if (baked.empty()) return Log::warn("blockout unbake: select a baked object first (nothing is unbaked unselected: it throws a mesh's edits away)");
    size_t edited = 0;
    for (const GameObject *g : baked) edited += !force && blockout_baked_edited(*g);
    if (edited > 1) return Log::warn("blockout unbake: %zu of the selected objects were edited since their bake and each asks first - unbake them one at a time", edited);
    for (GameObject *g : baked) blockout_unbake(*g, force);
  }
  else if (sub == "info") {
    update_blockouts(true);  // (what a script changed since the last frame, asked for first)
    if (targets.empty()) return Log::info("blockout: no Blockout in the scene (blockout new makes one)");
    for (GameObject *g : targets) {
      Log::info("Blockout '%s':", g->name.c_str());
      for (const auto &l : blockout_status_lines(*g, *g->get<Blockout>())) Log::info("  %s", l.first.c_str());
    }
  }
  else usage();
}

/* ===================================================================== */
/* A door's leaves as objects (round 515, task 0497, ADR 0136)            */
/* ===================================================================== */

namespace {

constexpr float kOpenEps = 1e-3f;  // (an Open Amount that differs from another by less than this - a tenth of a degree - is the same)

float open_clamped(float v) { return std::min(1.0f, std::max(0.0f, v)); }

/* Whether `c` is a leaf made by `door` (an empty owner - a file from before the field - counts as the parent's). */
bool is_leaf_of(const GameObject &c, const GameObject &door) {
  const BlockoutLeaf *mark = c.get<BlockoutLeaf>();
  return mark && (mark->made_by.empty() || mark->made_by == std::to_string(door.id));
}

/* The door's leaf objects: its children that are its own leaves, in the order they stand. */
std::vector<GameObject *> leaf_children(const GameObject &door) {
  std::vector<GameObject *> out;
  for (GameObject *c : door.children)
    if (is_leaf_of(*c, door)) out.push_back(c);
  return out;
}

/* The material of a door's leaf: the door's own Leaf material, else the Blockout's. */
std::string leaf_material(const BlockoutPiece &door, const Blockout &bo) {
  return door.material[kClassLeaf].empty() ? bo.material[kClassLeaf] : door.material[kClassLeaf];
}

/* The leaf's local place at `open`: position, turn about Y, and scale 1 (Scale on the Blockout or the door went into the numbers: ADR 0135). */
void leaf_place(GameObject &leaf, const BlockoutLeafPlan &plan, float open) {
  Vec3 position;
  float yaw = 0.0f;
  blockout_leaf_pose(plan, open, position, yaw);
  leaf.set_local_position(position);
  leaf.set_local_euler(Vec3(0.0f, yaw, 0.0f));
  leaf.set_local_scale(Vec3(1.0f, 1.0f, 1.0f));
}

/* A leaf the door no longer owns - taken out of it by hand - is an ordinary object from now on. */
void let_go(GameObject &leaf) {
  if (BlockoutLeaf *mark = leaf.get<BlockoutLeaf>()) leaf.remove_component(mark);
}

/* The leaves under `piece` that another door made. A copy of the whole door (Ctrl+D, paste) has its leaves marked with the original's id: the original still has a leaf of that index
 * of its own, so the copy's are the copy's - adopted, but only into a slot this door has a plan for and has no leaf in yet. Any other (a leaf the user dragged here from another door, one
 * pasted under a door with Leaf off or under a room or a box) stays what the user made it, an ordinary object: never destroyed. */
void claim_leaves(Scene &scene, GameObject &door) {
  const std::string mine = std::to_string(door.id);
  std::vector<GameObject *> strangers;
  for (GameObject *c : door.children)
    if (c->get<BlockoutLeaf>() && !is_leaf_of(*c, door)) strangers.push_back(c);
  if (strangers.empty()) return;
  const BlockoutPiece *bp = door.get<BlockoutPiece>();
  std::vector<BlockoutLeafPlan> plans;
  if (bp && bp->enabled) blockout_leaf_plans(*bp, plans);
  std::vector<bool> taken(plans.size(), false);
  for (const GameObject *c : door.children)
    if (is_leaf_of(*c, door) && c->get<BlockoutLeaf>()->index >= 0 && (size_t)c->get<BlockoutLeaf>()->index < taken.size()) taken[(size_t)c->get<BlockoutLeaf>()->index] = true;
  for (GameObject *c : strangers) {
    BlockoutLeaf *mark = c->get<BlockoutLeaf>();
    const GameObject *from = scene.find((uint64_t)std::strtoull(mark->made_by.c_str(), nullptr, 10));
    bool twin = false;
    if (from && from != &door)
      for (const GameObject *o : from->children) twin = twin || (is_leaf_of(*o, *from) && o->get<BlockoutLeaf>()->index == mark->index);
    const size_t index = (size_t)std::max(0, mark->index);
    const bool wanted = index < taken.size() && !taken[index];
    if ((twin || !from) && wanted) mark->made_by = mine, taken[index] = true;
    else let_go(*c);
  }
}

}  // namespace

/* The leaf's mesh, renderer, collider and key, made from the plan: a mesh in the Blockout's Leaf material, not Contribute GI (a leaf moves, so the lightmap is baked with the doorway
 * open and probes light the leaf), and a Box collider fitted to the slab(s). */
void Editor::blockout_leaf_made(GameObject &leaf, const BlockoutLeafPlan &plan, const std::string &material, float texture_scale, uint64_t key) {
  MeshFilter *mf = leaf.get<MeshFilter>();
  if (!mf) mf = leaf.add<MeshFilter>();
  mf->mesh = blockout_leaf_mesh(plan, texture_scale);
  MeshRenderer *mr = leaf.get<MeshRenderer>();
  if (!mr) mr = leaf.add<MeshRenderer>();
  mr->contribute_gi = false;
  mr->materials.clear();
  if (!material.empty()) mr->materials.push_back(material_asset(material));
  ColliderComponent *col = leaf.get<ColliderComponent>();
  if (!col) col = leaf.add<ColliderComponent>();
  Vec3 lo(1e30f), hi(-1e30f);
  for (const auto &box : plan.boxes)
    for (int a = 0; a < 3; a++) lo[a] = std::min(lo[a], box.first[a]), hi[a] = std::max(hi[a], box.second[a]);
  col->shape = 0, col->center = (lo + hi) * 0.5f, col->size = hi - lo, col->fit = true;
  /* Kinematic in Play mode: a body that moves when its transform does (a script, an animation), and pushes nothing by gravity. One the user changed is kept; one the user removed comes back when the leaf is made again. */
  if (!leaf.get<Rigidbody>()) {
    Rigidbody *rb = leaf.add<Rigidbody>();
    rb->is_kinematic = true, rb->use_gravity = false;
  }
  BlockoutLeaf *mark = leaf.get<BlockoutLeaf>();
  mark->index = plan.index, mark->made_from = std::to_string(key);
}

/* One door: its leaves made, made again (the numbers of their shape changed), removed or turned to match it. */
void Editor::blockout_sync_door(GameObject &door) {
  BlockoutPiece *bp = door.get<BlockoutPiece>();
  claim_leaves(*scene_, door);
  /* A door that has no leaf objects and wants none costs nothing more than this look at its children. */
  bool has_leaf = false;
  for (const GameObject *c : door.children) has_leaf = has_leaf || c->get<BlockoutLeaf>();
  if (!has_leaf && (!bp->is_styled_door() || !bp->enabled || !bp->leaf_on())) {
    blockout_leaves_seen_.erase(door.id);
    return;
  }
  const std::vector<GameObject *> leaves = leaf_children(door);
  const GameObject *owner = blockout_of_piece(door);
  const Blockout *bo = owner ? owner->get<Blockout>() : nullptr;
  if (!bo) return;  // (a door under a baked object's holder or outside any Blockout: Bake and Unbake look after its leaves)
  std::vector<BlockoutLeafPlan> plans;
  if (bp->enabled) blockout_leaf_plans(*bp, plans);
  std::vector<uint64_t> &seen = blockout_leaves_seen_[door.id];
  /* A leaf that was there at the last walk and is not the door's child now: deleted by hand (the door's Leaf goes off, with the deletion in the same undo step) or taken out of the
   * door (it is an ordinary object, and the door makes a new leaf). */
  bool deleted = false;
  for (uint64_t id : seen) {
    if (std::any_of(leaves.begin(), leaves.end(), [&](const GameObject *l) { return l->id == id; })) continue;
    if (GameObject *o = scene_->find(id)) let_go(*o);
    else deleted = true;
  }
  if (deleted && !plans.empty()) {
    (bp->style == kDoorRevolving ? bp->wings : bp->leaf) = false;
    scene_->destroy(leaves);
    seen.clear();
    blockout_join_step("Delete Door Leaf");
    Log::info("Door '%s': a leaf was deleted, so its Leaf is off now (turn it on to make the leaves again)", door.name.c_str());
    return;
  }
  /* The leaves the plans want, by index; a leaf the door made that no plan wants (an index too high, a door that wants none) goes. A second leaf of one index - a copy of a leaf made
   * with Ctrl+D - is not the door's doing: the one it knew stays, the copy becomes an ordinary object. */
  std::vector<GameObject *> slot(plans.size(), nullptr), gone;
  std::vector<GameObject *> ordered(leaves);
  std::stable_sort(ordered.begin(), ordered.end(), [&](const GameObject *a, const GameObject *b) {
    const bool sa = std::find(seen.begin(), seen.end(), a->id) != seen.end(), sb = std::find(seen.begin(), seen.end(), b->id) != seen.end();
    return sa && !sb;  // (the leaves of the last walk first)
  });
  for (GameObject *l : ordered) {
    const size_t index = (size_t)std::max(0, l->get<BlockoutLeaf>()->index);
    if (index >= slot.size()) gone.push_back(l);
    else if (!slot[index]) slot[index] = l;
    else let_go(*l);
  }
  if (!gone.empty()) scene_->destroy(gone);
  const float open = blockout_door_open(*bp), scale = bo->safe_texture_scale();
  const std::string material = leaf_material(*bp, *bo);
  std::vector<bool> fresh(plans.size(), false);
  for (size_t i = 0; i < plans.size(); i++) {
    const BlockoutLeafPlan &plan = plans[i];
    const uint64_t key = blockout_leaf_key(plan, scale, material);
    if (!slot[i]) {
      slot[i] = scene_->create(plan.name, &door);
      slot[i]->add<BlockoutLeaf>();
    }
    GameObject &leaf = *slot[i];
    BlockoutLeaf *mark = leaf.get<BlockoutLeaf>();
    mark->made_by = std::to_string(door.id);
    /* A leaf kept when the style changed takes the style's name (Leaf, or Leaf L / Leaf R); a name the user gave it is theirs. */
    if (leaf.name != plan.name && (leaf.name == "Leaf" || leaf.name == "Leaf L" || leaf.name == "Leaf R" || leaf.name == "Wings")) leaf.name = plan.name;
    if (mark->made_from == std::to_string(key) && leaf.get<MeshFilter>()) continue;
    blockout_leaf_made(leaf, plan, material, scale, key);
    leaf_place(leaf, plan, open);
    mark->open_seen = open;
    fresh[i] = true;
  }
  /* Turned by hand: a leaf the door agreed with at the last walk that now stands at another angle writes it to the door's Open Amount (same undo step); the other leaves follow.
   * Not while the turn is still going (a Rotate drag, a held slider): the angle is read when it is let go. */
  /* The first walk after a preview ended: a leaf the preview left at an angle the door does not say is put back to the door's number, not written back into whatever step comes last. */
  if (blockout_preview_ended_)
    for (size_t i = 0; i < plans.size(); i++) {
      float angle = 0.0f;
      if (blockout_leaf_open_of(plans[i], slot[i]->local(), angle) && std::fabs(open_clamped(angle) - open) > kOpenEps) leaf_place(*slot[i], plans[i], open);
      slot[i]->get<BlockoutLeaf>()->open_seen = open;
    }
  float now = open, turned_angle = 0.0f;
  size_t turned = plans.size();
  /* Not while an animation, a clip, the Sequencer or Play moves objects behind the undo stack: a leaf an animation turns is not the user's turn, and no step is amended for it. */
  const bool follow_only = interacting() || blockout_preview_now_;
  for (size_t i = 0; i < plans.size() && turned == plans.size() && !follow_only; i++) {
    const BlockoutLeaf *mark = slot[i]->get<BlockoutLeaf>();
    float angle = 0.0f;
    if (fresh[i] || std::fabs(mark->open_seen - open) > kOpenEps || !blockout_leaf_open_of(plans[i], slot[i]->local(), angle)) continue;
    if (std::fabs(open_clamped(angle) - open) > kOpenEps) turned = i, now = open_clamped(angle), turned_angle = angle;
  }
  if (turned < plans.size()) {
    bp->open_amount = now;
    slot[turned]->get<BlockoutLeaf>()->open_seen = now;
    /* The number joins the user's step: still to be kept - its label stays; kept already (the walk runs the frame after it) - that step is amended, so that one undo returns both. */
    blockout_join_step("Swing Door Leaf");
  }
  for (size_t i = 0; i < plans.size(); i++) {
    BlockoutLeaf *mark = slot[i]->get<BlockoutLeaf>();
    /* The leaf that was turned stays where it is - unless the angle was out of range (a leaf swung past 90 degrees or back through its hinge): then the number is held and the leaf with it. */
    const bool exact = i == turned && std::fabs(turned_angle - now) <= kOpenEps;
    if (exact || (i != turned && std::fabs(mark->open_seen - now) <= kOpenEps)) continue;
    leaf_place(*slot[i], plans[i], now);
    mark->open_seen = now;
  }
  seen.clear();
  for (GameObject *l : slot) seen.push_back(l->id);
  if (seen.empty()) blockout_leaves_seen_.erase(door.id);
}

/* A change the sync makes to the scene: it belongs to the step the user's action makes. Not kept yet - that step's label stays; kept already (the walk ran the frame after) - that step
 * is amended, so one undo returns both. Nothing is kept for it alone unless there is no step to join. */
void Editor::blockout_join_step(const char *label) {
  edit_epoch::bump();
  if (pending_change_) mark_changed(pending_label_);
  else if (!undo_.empty()) amend_last_step();
  else mark_changed(label);
}

/* Every walk: the leaves of every styled door in step with it (see blockout_sync_door); a leaf whose parent is no door (moved out by hand) is an ordinary object. */
void Editor::blockout_sync_leaves() {
  if (!scene_) return;
  if (blockout_leaves_scene_ != scene_.get()) {  // (a load, an undo or a new scene: the ids of the last walk are another scene's)
    blockout_leaves_seen_.clear();
    blockout_leaves_scene_ = scene_.get();
  }
  /* Play, the Animation window's Preview or Play, the Sequencer's: objects move behind the undo stack, so no leaf's pose is the user's turn. (Not Live Link's clients: a hand turn
   * made while one is connected is a real turn.) */
  blockout_preview_now_ = playing_ || anim_.preview || anim_.playing || seqr_.preview || seqr_.playing;
  blockout_preview_ended_ = blockout_preview_was_ && !blockout_preview_now_;
  blockout_preview_was_ = blockout_preview_now_;
  std::vector<uint64_t> &doors = blockout_sync_doors_, &strays = blockout_sync_strays_;
  doors.clear(), strays.clear();
  scene_->for_each([&](GameObject &g) {
    if (g.get<BlockoutPiece>()) doors.push_back(g.id);
    else if (g.get<BlockoutLeaf>() && !(g.parent && g.parent->get<BlockoutPiece>())) strays.push_back(g.id);
  });
  for (uint64_t id : strays) {
    GameObject *leaf = scene_->find(id);
    if (leaf && !(leaf->parent && leaf->parent->get<BlockoutBaked>())) let_go(*leaf);  // (a baked object's leaves wait there for Unbake)
  }
  for (uint64_t id : doors)
    if (GameObject *door = scene_->find(id)) blockout_sync_door(*door);
  for (auto it = blockout_leaves_seen_.begin(); it != blockout_leaves_seen_.end();)
    it = scene_->find(it->first) ? std::next(it) : blockout_leaves_seen_.erase(it);
}

/* blockout leaf select | door: the selected doors' leaf objects, so a script can turn them; and from the leaves back to their doors. Nothing changes when there is none to select. */
void Editor::blockout_leaf_command(const std::vector<std::string> &t) {
  const std::string what = t.size() > 2 ? to_lower(t[2]) : "";
  if (what != "select" && what != "door") return Log::warn("blockout leaf select | door: select the leaf objects of the selected doors, or the doors of the selected leaves");
  std::vector<uint64_t> ids;
  for (GameObject *g : selected_objects(false)) {
    if (what == "select")
      for (GameObject *l : leaf_children(*g)) ids.push_back(l->id);
    else if (g->get<BlockoutLeaf>() && g->parent && std::find(ids.begin(), ids.end(), g->parent->id) == ids.end())
      ids.push_back(g->parent->id);
  }
  if (ids.empty()) return Log::warn(what == "select" ? "blockout leaf select: select a styled door with its Leaf on first (blockout add door single)" : "blockout leaf door: select a door's leaf object first (blockout leaf select)");
  select(ids.front());
  for (size_t i = 1; i < ids.size(); i++) select(ids[i], SEL_ADD);
}

/* The Inspector's note on a leaf object: whose it is, and that its shape is the door's. */
void Editor::draw_blockout_leaf_extras(GameObject &g, BlockoutLeaf &, ui::Layout &lay) {
  auto &u = ui_;
  const GameObject *door = g.parent && g.parent->get<BlockoutPiece>() ? g.parent : nullptr;
  const Recti row = lay.row(u.row_h() + u.px(2));
  const std::string text = door ? "Made by Door '" + door->name + "' - its shape follows the door's numbers" : "Not under a door any more - an ordinary object from the next frame";
  u.label({row.x + u.px(4), row.y, row.w - u.px(8), row.h}, text, u.theme.text_dim);
  u.tooltip("This leaf is its own object under the door, with its pivot at the hinge. Open Amount (the door's) and the Rotate tool turn it; its size, thickness and material follow the door.\n"
            "Delete it and the door's Leaf goes off. A copy of it (Ctrl+D), or it moved out of its door or under another, is an ordinary object: the door makes its own again. Bake to Mesh keeps it as a separate object.");
  if (u.record_buttons) u.button_rects["shown:" + text] = row;
}

}  // namespace bl

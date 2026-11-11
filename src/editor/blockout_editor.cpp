// SPDX-License-Identifier: GPL-2.0-or-later
// The Blockout object in the editor (ADR 0129, task 0453): its mesh follows its pieces - asked for by a hash of the
// snapshot, built on a worker (a thread inside JobSystem::run_background, as eval_job_request does) and adopted when it
// is still wanted - and the ways to make, see, pick and refuse to edit one.
#include "editor.h"

#include "../core/core.h"
#include "../core/jobs.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace bl {

namespace {

/* Whether this part of the build ran by the exact path, the field path or both. */
std::string path_text(const blockout::Report &r) {
  bool exact = false, field = false;
  for (const blockout::ClusterReport &c : r.clusters) (c.path == blockout::Path::Crisp ? exact : field) = true;
  if (exact && field) return "exact + field";
  return exact ? "exact" : field ? "field" : "none";
}

BlockoutStatus make_status(const blockout::Report &r, size_t piece_count, bool ok) {
  BlockoutStatus s;
  s.built = true;
  s.pieces = piece_count;
  for (const blockout::Skipped &k : r.skipped) s.skipped.push_back({k.id, k.why});
  s.used = piece_count > s.skipped.size() ? piece_count - s.skipped.size() : 0;
  s.clusters = r.clusters.size();
  for (const blockout::ClusterReport &c : r.clusters) s.unresolved += c.unresolved;
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
  scene_->for_each([&](GameObject &g) {
    Blockout *b = g.get<Blockout>();
    if (!b) return;
    MeshFilter *mf = g.get<MeshFilter>();
    if (!mf) mf = g.add<MeshFilter>();
    if (!g.get<MeshRenderer>()) g.add<MeshRenderer>();
    std::vector<blockout::Piece> pieces;
    blockout::Settings settings;
    blockout_snapshot(g, pieces, settings);
    const uint64_t h = blockout_snapshot_hash(pieces, settings);
    if (b->state.hash_valid && b->state.hash == h && mf->mesh) return blockout_unwant(g.id);
    blockout_request(g, *b, std::move(pieces), settings, h);
  });
}

/* The pieces are back at what the mesh already is (an undo during a build): what runs or waits for this Blockout is not wanted. */
void Editor::blockout_unwant(uint64_t obj) {
  for (const auto &j : blockout_jobs_) {
    if (j->obj != obj) continue;
    j->has_next = false;
    j->cancel.store(true);
  }
}

/* A build asked for: on the spot where jobs don't run on workers (unit tests, console scripts - the next command sees the
 * mesh), else on a worker, the latest snapshot winning. */
void Editor::blockout_request(GameObject &g, Blockout &b, std::vector<blockout::Piece> &&pieces, const blockout::Settings &settings, uint64_t hash) {
  if (!jobs_async() || JobSystem::in_background()) {
    blockout_main_builds_++;
    MeshPtr mesh = std::make_shared<Mesh>();
    blockout::Faces faces;
    blockout::Report report;
    if (!b.state.cache) b.state.cache = std::make_unique<blockout::Cache>();
    const bool ok = blockout::build(pieces, settings, *mesh, &faces, &report, b.state.cache.get());
    blockout_land(g, b, hash, pieces.size(), mesh, std::move(faces), report, ok);
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
  blockout_start(g, b, std::move(pieces), settings, hash);
}

void Editor::blockout_start(GameObject &g, Blockout &b, std::vector<blockout::Piece> &&pieces, const blockout::Settings &settings, uint64_t hash) {
  auto job = std::make_unique<BlockoutJob>();
  job->obj = g.id, job->hash = hash, job->name = g.name;
  job->pieces = std::move(pieces), job->settings = settings;
  job->cache = std::move(b.state.cache);  // (the job owns it while it runs; it comes back with the job)
  if (!job->cache) job->cache = std::make_unique<blockout::Cache>();
  BlockoutJob *raw = job.get();
  raw->thread = std::thread([raw] {
    JobSystem::global().run_background([raw] {
      MeshPtr mesh = std::make_shared<Mesh>();
      raw->ok = blockout::build(raw->pieces, raw->settings, *mesh, &raw->faces, &raw->report, raw->cache.get(), &raw->cancel);
      if (raw->ok) raw->mesh = std::move(mesh);
    });
    raw->done.store(true, std::memory_order_release);
  });
  blockout_jobs_.push_back(std::move(job));
  blockout_started_++;
  ui_.redraw = true;
}

/* A build's result becomes the object's: the mesh, the faces' origins and the status together, so what the Scene view picks
 * through belongs to what it draws. A refusal keeps the last mesh and says why once per reason. Nothing here marks the scene
 * changed or adds an undo step: the edit of the pieces did. */
void Editor::blockout_land(GameObject &g, Blockout &b, uint64_t hash, size_t piece_count, MeshPtr mesh, blockout::Faces &&faces,
                           const blockout::Report &report, bool ok) {
  MeshFilter *mf = g.get<MeshFilter>();
  if (!mf) mf = g.add<MeshFilter>();
  if (!g.get<MeshRenderer>()) g.add<MeshRenderer>();
  BlockoutState &s = b.state;
  s.status = make_status(report, piece_count, ok);
  if (ok && mesh) {
    mf->mesh = mesh;
    s.faces = std::move(faces);
    s.faces_mesh = mesh.get(), s.faces_version = mesh->version;
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
  blockout_snapshot(*g, pieces, settings);
  const uint64_t now = blockout_snapshot_hash(pieces, settings);
  auto mesh_is_now = [&] {
    const MeshFilter *mf = g->get<MeshFilter>();
    return b->state.hash_valid && b->state.hash == now && mf && mf->mesh;
  };
  const bool wanted = now == job->hash || (!blockout_cancel_stale_ && !mesh_is_now());
  if (wanted && !job->cancel.load())
    blockout_land(*g, *b, job->hash, job->pieces.size(), job->mesh, std::move(job->faces), job->report, job->ok);
  if (mesh_is_now()) return;
  blockout_start(*g, *b, std::move(pieces), settings, now);
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

/* "[subtract] box|cylinder|sphere [numbers]" - a piece's kind, operation and sizes as the menus and the console say them. */
struct PieceWords {
  int shape = 0, op = 0;
  std::vector<float> numbers;
  bool ok = false;
};

PieceWords parse_piece_words(const std::string &what) {
  PieceWords p;
  bool shape_seen = false;
  for (const std::string &raw : split_ws(what)) {
    const std::string w = to_lower(raw);
    if (w == "subtract" || w == "cut") p.op = 1;
    else if (w == "add") p.op = 0;
    else if (!shape_seen && (w == "box" || w == "cube")) p.shape = 0, shape_seen = true;
    else if (!shape_seen && w == "cylinder") p.shape = 1, shape_seen = true;
    else if (!shape_seen && w == "sphere") p.shape = 2, shape_seen = true;
    else if (shape_seen && (std::isdigit((unsigned char)w[0]) || w[0] == '-' || w[0] == '.')) p.numbers.push_back((float)std::atof(w.c_str()));
    else return p;  // (a word it does not know: not ok)
  }
  p.ok = shape_seen;
  return p;
}

}  // namespace

/* A piece under `parent`, named for what it is, at `at` in the world (nullptr: the parent's origin). */
static GameObject *add_piece(Scene &scene, GameObject &parent, const std::string &name, const PieceWords &w, const Vec3 *at) {
  GameObject *g = scene.create(name, &parent);
  BlockoutPiece *bp = g->add<BlockoutPiece>();
  bp->shape = w.shape, bp->operation = w.op;
  const std::vector<float> &n = w.numbers;
  if (w.shape == 0 && n.size() >= 3) bp->size = {n[0], n[1], n[2]};
  if (w.shape == 0 && n.size() == 1) bp->size = {n[0], n[0], n[0]};
  if (w.shape == 1 && n.size() >= 1) bp->radius = n[0];
  if (w.shape == 1 && n.size() >= 2) bp->height = n[1];
  if (w.shape == 2 && n.size() >= 1) bp->radius = n[0];
  if (at) g->set_world_position(*at);
  return g;
}

/* GameObject > Blockout: an object that carries the result, and one 2 x 2 x 2 m box to start from - or, from a piece's menu
 * entry with nothing selected, that piece instead. */
GameObject *Editor::create_blockout(const std::string &first_piece) {
  GameObject *g = scene_->create(unique_object_name("Blockout"), nullptr);
  g->add<Blockout>();
  g->add<MeshFilter>()->mesh = std::make_shared<Mesh>();
  g->add<MeshRenderer>();
  g->set_world_position(cam_.pivot);
  GameObject *piece = nullptr;
  if (first_piece.empty()) {
    PieceWords w;
    w.numbers = {2.0f, 2.0f, 2.0f};
    piece = add_piece(*scene_, *g, "Box", w, nullptr);
  }
  else {
    const PieceWords w = parse_piece_words(first_piece);
    piece = add_piece(*scene_, *g, unique_object_name(w.op ? std::string("Subtract ") + kBlockoutShapeNames[w.shape] : std::string(kBlockoutShapeNames[w.shape])), w, nullptr);
  }
  expanded_.insert(g->id);
  select(piece->id);
  update_blockouts(true);  // (its first mesh now: the step recorded holds it)
  mark_changed("Create Blockout");
  return g;
}

/* The entries every menu that creates objects shares (GameObject, the Hierarchy's +, the Tools window's grid is its own). */
void Editor::blockout_menu_items(bool child) {
  (void)child;  // (a piece goes under the selected Blockout whatever the menu: there is nothing else to be a child of)
  auto &u = ui_;
  if (u.menu_item("New Blockout", nullptr, false, true, ui::Icon::Cube)) create_object("Blockout");
  u.tooltip("An object whose mesh is built from the pieces below it, starting with one 2 m box.");
  for (const char *op : {"Add", "Subtract"})
    for (const char *shape : {"Box", "Cylinder", "Sphere"}) {
      const std::string op_s = op, shape_s = shape;
      if (u.menu_item(op_s + " " + shape_s, nullptr, false, true, op_s == "Add" ? ui::Icon::Plus : ui::Icon::Close))
        create_object("Blockout Piece: " + (op_s == "Add" ? shape_s : op_s + " " + shape_s));
    }
}

/* A piece under the selected Blockout, or under the Blockout the selected piece belongs to; with neither, a new Blockout
 * with that piece. `what`: "[subtract] box|cylinder|sphere [x y z | radius height | radius]". */
GameObject *Editor::create_blockout_piece(const std::string &what) {
  const PieceWords w = parse_piece_words(what);
  if (!w.ok) {
    Log::warn("Blockout piece: '%s' - say box, cylinder or sphere, optionally 'subtract' first, then its sizes (x y z | radius height | radius)", what.c_str());
    return nullptr;
  }
  GameObject *sel = active_object();
  GameObject *target = sel && sel->get<Blockout>() ? sel : sel ? blockout_of_piece(*sel) : nullptr;
  if (!target) return create_blockout(what);
  const Vec3 at = cam_.pivot;
  const std::string base = w.op ? std::string("Subtract ") + kBlockoutShapeNames[w.shape] : std::string(kBlockoutShapeNames[w.shape]);
  GameObject *piece = add_piece(*scene_, *target, unique_object_name(base), w, &at);
  expanded_.insert(target->id);
  select(piece->id);
  update_blockouts(true);
  mark_changed(std::string("Create Blockout ") + base);
  return piece;
}

/* ===================================================================== */
/* Seeing and picking them                                                */
/* ===================================================================== */

namespace {

bool finite3_bits(Vec3 v) { return finite_bits(v.x) && finite_bits(v.y) && finite_bits(v.z); }

/* A piece's wire in its own space: a box's 12 edges, a cylinder's two rings and its sides' lines, a sphere's three great
 * circles through its faceting (the equator's segments, the meridians' rings). */
void piece_wire(const BlockoutPiece &p, const Mat4 &w, const std::function<void(Vec3, Vec3)> &seg) {
  auto line = [&](Vec3 a, Vec3 b) { seg(w.point(a), w.point(b)); };
  const int shape = std::min(2, std::max(0, p.shape));
  const int n = std::min(256, std::max(3, p.segments));
  const float r = std::fabs(p.radius);
  if (shape == 0) {
    if (!finite3_bits(p.size)) return;
    const Vec3 h = Vec3(std::fabs(p.size.x), std::fabs(p.size.y), std::fabs(p.size.z)) * 0.5f;
    for (int e = 0; e < 12; e++) {
      const int axis = e / 4, a = (axis + 1) % 3, b = (axis + 2) % 3;
      Vec3 p0, p1;
      p0[a] = p1[a] = (e & 1) ? h[a] : -h[a];
      p0[b] = p1[b] = (e & 2) ? h[b] : -h[b];
      p0[axis] = -h[axis], p1[axis] = h[axis];
      line(p0, p1);
    }
    return;
  }
  if (!finite_bits(p.radius)) return;
  if (shape == 1) {
    if (!finite_bits(p.height)) return;
    const float hh = std::fabs(p.height) * 0.5f;
    for (int i = 0; i < n; i++) {
      const float a0 = 2.0f * kPi * (float)i / (float)n, a1 = 2.0f * kPi * (float)(i + 1) / (float)n;
      const Vec3 b0(r * std::cos(a0), -hh, r * std::sin(a0)), b1(r * std::cos(a1), -hh, r * std::sin(a1));
      const Vec3 t0(b0.x, hh, b0.z), t1(b1.x, hh, b1.z);
      line(b0, b1);
      line(t0, t1);
      line(b0, t0);
    }
    return;
  }
  const int m = 2 * std::min(128, std::max(2, p.rings));  // (the meridians pass through the poles: two ring steps a circle's half)
  for (int i = 0; i < n; i++) {
    const float a0 = 2.0f * kPi * (float)i / (float)n, a1 = 2.0f * kPi * (float)(i + 1) / (float)n;
    line({r * std::cos(a0), 0, r * std::sin(a0)}, {r * std::cos(a1), 0, r * std::sin(a1)});
  }
  for (int i = 0; i < m; i++) {
    const float a0 = 2.0f * kPi * (float)i / (float)m, a1 = 2.0f * kPi * (float)(i + 1) / (float)m;
    line({r * std::cos(a0), r * std::sin(a0), 0}, {r * std::cos(a1), r * std::sin(a1), 0});
    line({0, r * std::cos(a0), r * std::sin(a0)}, {0, r * std::cos(a1), r * std::sin(a1)});
  }
}

}  // namespace

/* Each piece of a selected Blockout, and a selected piece, as a wire: Add in the selection colour, Subtract in a yellow of
 * the Scene view's own (a theme's warning colour can be the selection's orange), the selected piece brighter. A piece has no
 * mesh to show itself by. */
static constexpr uint32_t kCutYellow = Color::hex(0xF4BC02);

void Editor::draw_blockouts() {
  scene_->for_each([&](GameObject &g) {
    if (!g.get<Blockout>() || !g.active_in_hierarchy()) return;
    const bool all = is_selected(g.id);
    std::vector<const GameObject *> pieces;
    blockout_piece_objects(g, pieces);
    for (const GameObject *p : pieces) {
      const bool mine = is_selected(p->id);
      if (!all && !mine) continue;
      const BlockoutPiece *bp = p->get<BlockoutPiece>();
      const uint32_t base = bp->operation == 1 ? kCutYellow : kSelectOrange;
      const uint32_t col = (base & 0x00FFFFFFu) | ((mine ? 255u : 150u) << 24);
      piece_wire(*bp, p->world_matrix(), [&](Vec3 a, Vec3 b) { scene_r3d_.line(a, b, col, true, mine ? 4e-4f : 3e-4f); });
    }
  });
}

/* The piece that made the face under a view pixel (the faces' origins say which); 0 when there is none to name - a blended
 * face, a mesh whose origins are not its own, a piece that is gone or another Blockout's. */
uint64_t Editor::blockout_pick_piece(GameObject &g, int view_x, int view_y) {
  Blockout *b = g.get<Blockout>();
  const MeshFilter *mf = g.get<MeshFilter>();
  if (!b || !mf || !mf->mesh) return 0;
  const Mesh &m = *mf->mesh;
  const BlockoutState &s = b->state;
  if (s.faces_mesh != &m || s.faces_version != m.version || s.faces.piece.size() != m.face_count()) return 0;  // (another writer's mesh: no origins)
  const Ray ray = scene_r3d_.screen_ray((float)view_x, (float)view_y);
  const Mat4 inv = g.world_matrix().inverse();
  const Ray lr{inv.point(ray.origin), inv.dir(ray.dir)};
  const RenderMesh &rm = m.render_mesh();
  float best = 1e30f;
  size_t best_tri = 0;
  for (size_t t = 0; t < rm.tri_count(); t++) {
    const float d = ray_triangle(lr, rm.positions[rm.indices[t * 3]], rm.positions[rm.indices[t * 3 + 1]], rm.positions[rm.indices[t * 3 + 2]]);
    if (d > 0 && d < best) best = d, best_tri = t;
  }
  if (best >= 1e30f || best_tri >= rm.tri_face.size()) return 0;
  const uint32_t face = rm.tri_face[best_tri];
  if (face >= s.faces.piece.size()) return 0;
  const GameObject *piece = scene_->find(s.faces.piece[face]);
  if (!piece || !piece->get<BlockoutPiece>()) return 0;
  return blockout_of_piece(*piece) == &g ? piece->id : 0;  // (a duplicate holds its original's origins until its own build lands)
}

/* ===================================================================== */
/* Edit Mode refused, the Inspector's status, the console                 */
/* ===================================================================== */

/* A Blockout's mesh (and a piece, which has none) is rebuilt from its pieces: editing it by hand would be thrown away at
 * the next change, so it is not offered. */
bool Editor::blockout_refuses_edit_mode(const GameObject *g) {
  if (!g || !(g->get<Blockout>() || g->get<BlockoutPiece>())) return false;
  Log::warn("A Blockout's mesh is rebuilt from its pieces: move or resize the pieces.");
  return true;
}

std::vector<std::pair<std::string, bool>> Editor::blockout_status_lines(const GameObject &g, const Blockout &b) const {
  std::vector<std::pair<std::string, bool>> lines;
  const BlockoutStatus &s = b.state.status;
  for (const auto &j : blockout_jobs_)
    if (j->obj == g.id) lines.push_back({"Building...", false});
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
  if (!s.refusal.empty()) lines.push_back({"Refused: " + s.refusal + " - the last mesh stays", true});
  if (s.unresolved)
    lines.push_back({strprintf("%zu places could not be resolved (two corners in one cell): lower Field Cell", s.unresolved), true});
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
}

/* blockout new | add box|cylinder|sphere [subtract] [sizes] | set cell|method <value> | rebuild | wait | info */
void Editor::blockout_command(const std::vector<std::string> &t) {
  const std::string sub = t.size() > 1 ? to_lower(t[1]) : "info";
  auto usage = [] {
    Log::info("blockout new | add box|cylinder|sphere [subtract] [x y z | radius height | radius] | set cell|method <value> | rebuild | wait | info");
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
  else if (sub == "set") {
    const std::string key = t.size() > 2 ? to_lower(t[2]) : "", value = t.size() > 3 ? to_lower(t[3]) : "";
    if ((key != "cell" && key != "method") || value.empty()) return Log::warn("blockout set cell <metres> | method auto|exact|field");
    if (targets.empty()) return Log::warn("blockout set: no Blockout in the scene");
    for (GameObject *g : targets) {
      Blockout *b = g->get<Blockout>();
      if (key == "cell") b->cell = (float)std::atof(value.c_str());
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

}  // namespace bl

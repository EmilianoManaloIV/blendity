// SPDX-License-Identifier: GPL-2.0-or-later
// Skin weights from the editor (task 0057, ADR 0028): automatic weights by inpainting, the distance (envelope) ones,
// and transfer from a rigged body onto clothing. The solve runs on a copy of the mesh - on a worker thread when the
// mesh is large, so the editor keeps drawing - and its groups land as one undo step when it ends.
#include "editor.h"

#include "../scene/scene.h"
#include "../core/jobs.h"
#include "../scene/skin_weights.h"

#include <algorithm>
#include <cstring>
#include <exception>

namespace bl {

namespace {

/* The named groups of `from` into `to` (replacing groups of the same names, adding the others). */
void apply_groups(Mesh &to, const Mesh &from, const std::vector<std::string> &names) {
  for (const std::string &n : names) {
    const int src = from.find_group(n);
    if (src < 0) continue;
    const int dst = to.find_group(n);
    if (dst >= 0) to.vertex_groups[(size_t)dst].weights = from.vertex_groups[(size_t)src].weights;
    else to.vertex_groups.push_back(from.vertex_groups[(size_t)src]);
  }
  to.touch();
}

/* What a solve needs of a mesh - positions, faces, groups - without its render caches (tens of MB at 50k vertices). */
Mesh weights_copy(const Mesh &m) {
  Mesh c;
  c.positions = m.positions;
  c.face_offsets = m.face_offsets;
  c.corner_verts = m.corner_verts;
  c.loose_edges = m.loose_edges;
  c.vertex_groups = m.vertex_groups;
  return c;
}

uint64_t positions_hash(const Mesh &m) {
  uint64_t h = 1469598103934665603ull;  // FNV-1a over the bytes
  const auto *b = reinterpret_cast<const unsigned char *>(m.positions.data());
  for (size_t i = 0, n = m.positions.size() * sizeof(Vec3); i < n; i++) h = (h ^ b[i]) * 1099511628211ull;
  return h;
}

/* The source's groups a transfer should carry: its armature's bones when it has a Skin modifier (selection groups and
 * the like stay home), else all of them; never InpaintMask. */
std::vector<std::string> transfer_names(const GameObject &source, const Mesh &mesh) {
  std::vector<std::string> bones;
  if (const SkinModifier *sk = source.get<SkinModifier>()) {
    std::vector<BoneSegment> segs;
    skin_bone_segments(*sk, segs);
    for (const BoneSegment &b : segs) bones.push_back(b.name);
  }
  std::vector<std::string> out;
  for (const VertexGroup &vg : mesh.vertex_groups) {
    if (vg.name == kInpaintMask) continue;
    if (!bones.empty() && std::find(bones.begin(), bones.end(), vg.name) == bones.end()) continue;
    out.push_back(vg.name);
  }
  return out;
}

}  // namespace

SkinWeightJob::~SkinWeightJob() {
  cancel = true;  // (an editor closing mid-solve doesn't wait for the whole solve)
  if (worker.joinable()) worker.join();
}

void SkinWeightJob::run() {
  InpaintOptions o;
  o.progress = &progress;
  o.cancel = &cancel;
  try {
    JobSystem::global().run_background([&] { ok = solve(result, o, &report, &error); });
  }
  catch (const std::exception &e) {  // (out of memory on a huge mesh: a failed bind, not a wedged editor)
    ok = false;
    error = e.what();
  }
  done = true;
}

bool Editor::skin_bind(GameObject *g, SkinBind how) {
  SkinModifier *sk = g ? g->get<SkinModifier>() : nullptr;
  MeshFilter *mf = g ? g->mesh_filter() : nullptr;
  if (!sk || !mf || !mf->mesh) {
    Log::warn("skin bind: select a mesh with a Skin modifier");
    return false;
  }
  std::string err;
  if (how == SkinBind::Envelope) {
    if (!skin_bind_automatic(*sk, *mesh_make_mutable(mf->mesh), &err)) {
      Log::warn("skin bind: %s", err.c_str());
      return false;
    }
    Log::info("'%s' bound to '%s' with envelope (distance) weights", g->name.c_str(), sk->object.c_str());
    mark_changed("Skin: Envelope Weights");
    return true;
  }
  std::vector<BoneSegment> bones;
  if (!skin_bone_segments(*sk, bones, &err)) {
    Log::warn("skin bind: %s", err.c_str());
    return false;
  }
  std::vector<std::string> names;
  for (const BoneSegment &b : bones) names.push_back(b.name);
  return run_skin_job(g, "Skin: Automatic Weights", names, [bones](Mesh &m, const InpaintOptions &o, InpaintReport *r, std::string *e) {
    return automatic_skin_weights(m, bones, o, r, e);
  });
}

bool Editor::skin_transfer(GameObject *source, GameObject *target) {
  MeshFilter *sf = source ? source->mesh_filter() : nullptr, *tf = target ? target->mesh_filter() : nullptr;
  if (!sf || !sf->mesh || !tf || !tf->mesh || source == target) {
    Log::warn("skin transfer <source> [target]: two different meshes, the source with vertex groups (a rigged body)");
    return false;
  }
  /* The source at rest, in the target's space (both are compared where they stand), with only the groups it carries. */
  const std::vector<std::string> names = transfer_names(*source, *sf->mesh);
  auto src = std::make_shared<Mesh>(weights_copy(*sf->mesh));
  const Mat4 to_target = target->world_matrix().inverse() * source->world_matrix();
  for (Vec3 &p : src->positions) p = to_target.point(p);
  src->vertex_groups.erase(std::remove_if(src->vertex_groups.begin(), src->vertex_groups.end(),
                                          [&](const VertexGroup &vg) { return std::find(names.begin(), names.end(), vg.name) == names.end(); }),
                           src->vertex_groups.end());
  if (names.empty()) {
    Log::warn("skin transfer: '%s' has no vertex groups to transfer", source->name.c_str());
    return false;
  }
  return run_skin_job(
      target, "Skin: Transfer Weights", names,
      [src](Mesh &m, const InpaintOptions &o, InpaintReport *r, std::string *e) { return transfer_skin_weights(*src, m, o, r, e); }, source->id);
}

bool Editor::run_skin_job(GameObject *g, const char *label, std::vector<std::string> names, SkinWeightJob::Solve solve, uint64_t source) {
  if (skin_job_) {
    Log::warn("%s: a skin solve is still running (Rigging window: Cancel, or wait)", label);
    return false;
  }
  const MeshPtr &mesh = g->mesh_filter()->mesh;
  auto job = std::make_unique<SkinWeightJob>();
  job->object = g->id;
  job->source = source;
  job->label = label;
  job->groups = std::move(names);
  job->topology = meshops::topology_key(*mesh);
  job->positions = positions_hash(*mesh);
  job->result = weights_copy(*mesh);
  job->solve = std::move(solve);
  SkinWeightJob *j = job.get();
  skin_job_ = std::move(job);
  if (j->result.vert_count() < skin_async_min_verts_) {
    j->run();
    return finish_skin_job();
  }
  Log::info("%s: solving %zu vertices of '%s' in the background", label, j->result.vert_count(), g->name.c_str());
  j->worker = std::thread([j] { j->run(); });
  return true;
}

void Editor::poll_skin_job() {
  if (!skin_job_) return;
  ui_.redraw = true;  // (keep frames coming while it runs, even with the Rigging window closed)
  if (!skin_job_->done || edit_mode_) return;  // (not under an Edit Mode session: it lands after, if the mesh is the same)
  finish_skin_job();
}

bool Editor::finish_skin_job() {
  std::unique_ptr<SkinWeightJob> job = std::move(skin_job_);
  if (job->worker.joinable()) job->worker.join();
  const char *label = job->label.c_str();
  if (job->cancel) {  // (cancelled, or the scene it was for went: undo, Play, another scene)
    Log::info("%s: cancelled", label);
    return false;
  }
  if (!job->ok) {
    Log::warn("%s: %s", label, job->error.c_str());
    return false;
  }
  GameObject *g = scene_->find(job->object);
  MeshFilter *mf = g ? g->mesh_filter() : nullptr;
  if (!mf || !mf->mesh || meshops::topology_key(*mf->mesh) != job->topology || positions_hash(*mf->mesh) != job->positions) {
    Log::warn("%s: the mesh changed while it was solving; run it again", label);
    return false;
  }
  if (!job->report.converged) Log::warn("%s: the solve stopped at its iteration limit; the weights are approximate", label);
  apply_groups(*mesh_make_mutable(mf->mesh), job->result, job->groups);
  give_skin_modifier(g, job->source);
  const InpaintReport &r = job->report;
  Log::info("%s on '%s': %zu vertices kept, %zu inpainted, %zu by distance (%s, %.0f ms)", label, g->name.c_str(), r.known, r.inpainted, r.fallback,
            r.solver, r.ms);
  mark_changed(label);
  return true;
}

/* A transfer's target follows the same skeleton as its source (Blender: the Armature modifier you add by hand). */
void Editor::give_skin_modifier(GameObject *target, uint64_t source) {
  const GameObject *src = source ? scene_->find(source) : nullptr;
  const SkinModifier *from = src ? src->get<SkinModifier>() : nullptr;
  if (!from || target->get<SkinModifier>()) return;
  SkinModifier *sk = target->add<SkinModifier>();
  sk->object = from->object;
  sk->max_influences = from->max_influences;
}

/* skin bind [object] [inpaint|envelope], skin transfer <source> [target], skin cancel (task 0057). */
void Editor::skin_command(const std::vector<std::string> &t) {
  auto arg = [&](size_t i, const std::string &def) { return t.size() > i ? t[i] : def; };
  const std::string verb = to_lower(arg(1, ""));
  if (verb == "cancel") {
    if (skin_job_) skin_job_->cancel = true;
    return;
  }
  if (verb == "transfer") {
    skin_transfer(scene_->find_by_name(arg(2, "")), t.size() > 3 ? scene_->find_by_name(t[3]) : active_object());
    return;
  }
  if (verb != "bind") {
    Log::warn("skin bind [object] [inpaint|envelope], skin transfer <source> [target], skin cancel");
    return;
  }
  const std::string how = to_lower(t.back());
  const bool named_how = t.size() > 2 && (how == "inpaint" || how == "envelope");
  const size_t words = t.size() - (named_how ? 1 : 0);
  skin_bind(words > 2 ? scene_->find_by_name(t[2]) : active_object(), named_how && how == "envelope" ? SkinBind::Envelope : SkinBind::Inpaint);
}

/* While a solve runs: what it is, how far, and Cancel. */
void Editor::draw_skin_job_progress(ui::Layout &lay) {
  if (!skin_job_) return;
  auto &u = ui_;
  const Recti row = lay.row(u.row_h() + u.px(2));
  const float f = std::max(0.0f, std::min(1.0f, skin_job_->progress.load()));
  const Recti bar{row.x + u.px(4), row.y + u.px(3), row.w - u.px(92), row.h - u.px(6)};
  u.frame(bar, u.theme.field, u.theme.border, u.px(3));
  u.canvas.fill_rect({bar.x, bar.y, (int)((float)bar.w * f), bar.h}, u.theme.accent);
  u.label(bar, strprintf("%s  %d%%", skin_job_->label.c_str(), (int)(f * 100.0f)), u.theme.text_bright, ui::Align::Center);
  if (u.button({bar.right() + u.px(6), row.y, u.px(80), row.h}, "Cancel")) skin_job_->cancel = true;
  u.redraw = true;  // (keep drawing while it runs)
}

}  // namespace bl

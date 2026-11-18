// SPDX-License-Identifier: GPL-2.0-or-later
// Adjust Last outside Edit Mode's operators (task 0266, ADR 0085's follow-up): a primitive just added (its size and
// segments), a UV operator with numbers (Unwrap's and Pack's margin, Smart UV's angle) and a Draw shape with a count
// (a circle's or an arc's segments, a polygon's sides, a rectangle's fillet) get Blender's redo panel. Changing a field
// runs the operation again from the state before it and amends the same undo step: the step holds the scene before,
// so only the editor's after-snapshot is taken again - no new step, Ctrl+Z still goes back to before the operation.
// The panel lasts while that step is the newest; anything else committed ends it, as Blender's does. One exception (round 503): a console `blockout set` or an Inspector
// edit of the numbers of F9's own Blockout piece keeps the panel, refreshed from the piece and tied to that step (refresh_blockout_last), so the rows show what the piece has
// and the next F9 edit does not write the old numbers back; an edit that reaches only another piece, or a `blockout set` of a room's wall, a blend or a material, ends it.
#include "editor.h"
#include "../scene/uv.h"

namespace bl {

static bool primitive_has_numbers(const std::string &kind) {
  return kind == "Cube" || kind == "Sphere" || kind == "Icosphere" || kind == "Quad Sphere" || kind == "Cylinder" || kind == "Cone" || kind == "Torus" || kind == "Plane";
}

/* The primitive's mesh from the panel's numbers (the same defaults create_primitive uses). */
static MeshPtr primitive_mesh(const Editor::ObjLastOp &o) {
  const std::string &k = o.what;
  if (k == "Cube") return primitives::cube(o.a);
  if (k == "Sphere") return primitives::uv_sphere(o.a, o.n, o.m);
  if (k == "Icosphere") return primitives::ico_sphere(o.a, o.n);
  if (k == "Quad Sphere") return primitives::quad_sphere(o.a, o.n, o.m);
  if (k == "Cylinder") return primitives::cylinder(o.a, o.b, o.n);
  if (k == "Cone") return primitives::cone(o.a, o.b, o.n);
  if (k == "Torus") return primitives::torus(o.a, o.b, o.n, o.m);
  if (k == "Plane") return primitives::plane(o.a, o.n);
  return nullptr;
}

/* The serial the step will have once committed: the operation marked a change this frame (pending) or already did. */
uint64_t Editor::obj_last_serial_now() const { return undo_serial_ + (pending_change_ ? 1 : 0); }

void Editor::record_primitive_last(GameObject *g, const std::string &kind) {
  if (!g || !primitive_has_numbers(kind)) return;
  ObjLastOp o;
  o.kind = "primitive", o.what = kind, o.obj = g->id;
  if (kind == "Cube") o.a = 1.0f;
  else if (kind == "Sphere") o.a = 0.5f, o.n = 24, o.m = 16;
  else if (kind == "Icosphere") o.a = 0.5f, o.n = 2;
  else if (kind == "Quad Sphere") o.a = 0.5f, o.n = 4, o.m = 0;
  else if (kind == "Cylinder") o.a = 0.5f, o.b = 2.0f, o.n = 24;
  else if (kind == "Cone") o.a = 0.5f, o.b = 1.0f, o.n = 24;
  else if (kind == "Torus") o.a = 0.5f, o.b = 0.2f, o.n = 32, o.m = 16;
  else if (kind == "Plane") o.a = 10.0f, o.n = 10;
  o.serial = obj_last_serial_now();
  obj_last_ = std::move(o);
  obj_last_open_ = true;
  obj_last_note_result();
}

void Editor::record_uv_last(uint64_t obj, const std::string &op, MeshPtr before, const std::vector<uint8_t> *mask) {
  if (op != "unwrap" && op != "smart" && op != "pack") return;
  ObjLastOp o;
  o.kind = "uv", o.what = op, o.obj = obj, o.before = std::move(before);
  o.a = 0.02f;   // margin
  o.b = 66.0f;   // Smart UV's angle limit
  o.flag = true; // Pack's rotation
  if (mask) o.mask = *mask, o.has_mask = true;
  o.serial = obj_last_serial_now();
  obj_last_ = std::move(o);
  obj_last_open_ = true;
  obj_last_note_result();
}

void Editor::record_draw_last(uint64_t obj, MeshPtr before, const Mat4 &before_xf) {
  const int shape = draw_.shape;
  if (shape != 1 && shape != 2 && shape != 3 && shape != 4) return;  // (a rectangle's fillet, a circle's or an arc's segments, a polygon's sides)
  ObjLastOp o;
  o.kind = "draw", o.what = kDrawShapes[shape], o.obj = obj, o.before = std::move(before), o.before_xf = before_xf;
  o.draw_pts = draw_.pts;
  o.draw_snaps = draw_.snaps;
  o.draw_plane_p = draw_.plane_p, o.draw_plane_n = draw_.plane_n, o.draw_axis_u = draw_.axis_u;
  o.draw_has_plane = draw_.has_plane, o.draw_face = draw_.face, o.draw_shape = shape;
  o.n = shape == 4 ? draw_sides_ : draw_segments_;
  o.a = draw_fillet_;
  o.serial = obj_last_serial_now();
  obj_last_ = std::move(o);
  obj_last_open_ = true;
  obj_last_note_result();
}

bool Editor::obj_last_valid() const {
  if (obj_last_.kind.empty() || playing_ || obj_last_.serial != undo_serial_ || pending_change_) return false;
  const GameObject *g = scene_->find(obj_last_.obj);
  if (obj_last_.kind == "blockout") return g && g->get<BlockoutPiece>() && blockout_of_piece(*g);  // (a piece has no mesh to compare: the step's serial says it is still the newest)
  if (obj_last_.kind == "pose") return g != nullptr;  // (round 541: Paste Pose - a pose has no mesh either)
  const MeshFilter *mf = g ? g->get<MeshFilter>() : nullptr;
  if (!mf || !mf->mesh || mf->mesh.get() != obj_last_.result || mf->mesh->version != obj_last_.result_version) return false;  // (undone, replaced, edited since)
  return obj_last_.kind != "draw" || (edit_mode_ && edit_obj_ == g->id);  // (a Draw shape is adjusted in the Edit Mode it was drawn in)
}

/* The mesh the operation (or its re-run) left on the object: the panel lasts while it is still there, unedited. */
void Editor::obj_last_note_result() {
  const GameObject *g = scene_->find(obj_last_.obj);
  const MeshFilter *mf = g ? g->get<MeshFilter>() : nullptr;
  obj_last_.result = mf && mf->mesh ? mf->mesh.get() : nullptr;
  obj_last_.result_version = mf && mf->mesh ? mf->mesh->version : 0;
}

/* The step's after-snapshot taken again: the change belongs to the step the operation made. */
void Editor::amend_last_step() {
  pending_change_ = false;
  settle_meshes();
  obj_last_note_result();
  stable_ = scene_->clone();
  scene_dirty_ = true;
  ui_.redraw = true;
}

/* Run the recorded operation again with the panel's numbers, from the state before it. */
void Editor::obj_last_rerun() {
  ObjLastOp &o = obj_last_;
  GameObject *g = scene_->find(o.obj);
  if (o.kind == "blockout") return blockout_last_rerun();
  if (o.kind == "pose") return pose_last_rerun();  // (round 541)
  MeshFilter *mf = g ? g->get<MeshFilter>() : nullptr;
  if (!mf) return;
  if (o.kind == "primitive") {
    if (MeshPtr m = primitive_mesh(o)) mf->mesh = m;
    return amend_last_step();
  }
  if (o.kind == "uv" && o.before) {
    mf->mesh = o.before;
    Mesh &m = *mesh_make_mutable(mf->mesh);
    const uvops::Mask *pm = o.has_mask ? &o.mask : nullptr;
    const float margin = std::max(0.0f, std::min(0.25f, o.a));
    if (o.what == "unwrap") uvops::unwrap_lscm(m, pm, margin);
    else if (o.what == "smart") uvops::smart_project(m, pm, std::max(1.0f, std::min(89.0f, o.b)), margin);
    else uvops::pack_islands(m, pm, margin, o.flag);
    m.touch();
    return amend_last_step();
  }
  if (o.kind == "draw" && o.before && edit_mode_ && edit_object() == g) {
    mf->mesh = o.before;
    g->set_world_matrix(o.before_xf);
    const DrawTool keep = draw_;
    const int keep_segments = draw_segments_, keep_sides = draw_sides_;
    const float keep_fillet = draw_fillet_;
    draw_.pts = o.draw_pts, draw_.snaps = o.draw_snaps, draw_.shape = o.draw_shape;
    draw_.plane_p = o.draw_plane_p, draw_.plane_n = o.draw_plane_n, draw_.axis_u = o.draw_axis_u;
    draw_.has_plane = o.draw_has_plane, draw_.face = o.draw_face, draw_.bent = false;
    if (o.draw_shape == 4) draw_sides_ = std::max(3, std::min(256, o.n));
    else draw_segments_ = std::max(3, std::min(256, o.n));
    draw_fillet_ = std::max(0.0f, o.a);
    if (!draw_.pts.empty()) {
      const Vec3 last = draw_.pts.back();
      draw_.pts.pop_back();
      bool closed = false;
      const std::vector<Vec3> outline = draw_outline(last, true, closed);
      draw_commit(outline, closed);
    }
    draw_ = keep;
    draw_segments_ = keep_segments, draw_sides_ = keep_sides, draw_fillet_ = keep_fillet;  // (the tool's own settings stay as they were)
    return amend_last_step();
  }
}

/* ---- Round 541 (task 0521): Paste Pose's F9 - Influence and Flipped, re-run from the pose before the paste ---- */

void Editor::record_pose_last(std::vector<std::pair<uint64_t, Transform>> before, std::vector<PoseClipBone> clip, float influence, bool flipped, bool selected_only) {
  if (before.empty()) return;
  ObjLastOp o;
  o.kind = "pose", o.what = "Paste Pose", o.obj = before.front().first;
  o.a = influence, o.flag = flipped, o.m = selected_only ? 1 : 0;
  o.pose_before = std::move(before), o.pose_clip = std::move(clip);
  o.serial = obj_last_serial_now();
  obj_last_ = std::move(o);
  obj_last_open_ = true;
}

/* The bones back as they were, the paste made again with the panel's numbers; Record keys the new pose over the old. */
void Editor::pose_last_rerun() {
  ObjLastOp &o = obj_last_;
  for (const auto &[id, t] : o.pose_before)
    if (GameObject *b = scene_->find(id)) b->set_local(t);
  pose_paste(o.pose_clip, o.a, o.flag, o.m != 0);
  edit_epoch::bump();
  anim_record_changes();
  amend_last_step();
}

/* A Blockout piece just made, from the panel's numbers: every one is absolute and the floor's middle is the one recorded when the piece was made,
 * so ten changes leave no drift. The Blockout's mesh follows on the next frame (round 477's rebuild). */
void Editor::blockout_last_rerun() {
  ObjLastOp &o = obj_last_;
  GameObject *g = scene_->find(o.obj);
  BlockoutPiece *bp = g ? g->get<BlockoutPiece>() : nullptr;
  if (!bp) return;
  auto size = [](float v) { return finite_bits(v) ? std::max(kBlockoutMinSize, std::min(10000.0f, v)) : kBlockoutMinSize; };
  auto thick = [](float v) { return finite_bits(v) ? std::max(0.0f, std::min(1000.0f, v)) : 0.0f; };
  if (o.n == 7) {  // (round 500: a preset: the numbers are the copy's, from the piece as it was made; its bottom stays where it was)
    *bp = o.pre;
    g->set_world_position(o.base_p);
    if (o.turnable && o.turn != o.turn_done) {
      const int quarters = o.turn - o.turn_done;
      o.turn_done = o.turn;
      blockout_turn_piece(*g, quarters);
    }
    edit_epoch::bump();
    amend_last_step();
    return;
  }
  if (bp->opening) {
    bp->width = size(o.a);
    bp->sill = thick(o.c);
    bp->head = std::max(size(o.b), bp->sill + kBlockoutMinSize);
    if (bp->opening == 2 && o.style >= 0) {  // (round 498: the style and the numbers it reads, as the rows hold them)
      bp->style = std::min(kBlockoutWindowStyles - 1, std::max(0, o.style));
      bp->frame_width = size(o.frame_width), bp->frame_depth = size(o.frame_depth), bp->arch_rise = thick(o.arch_rise), bp->sill_overhang = thick(o.sill_overhang);
      bp->panes_across = std::min(12, std::max(1, o.panes_across)), bp->panes_up = std::min(12, std::max(1, o.panes_up));
      bp->glass = o.glass, bp->sill_board = o.sill_board;
    }
    if (bp->opening == 1 && o.style >= 0) {  // (round 499: a door's style and numbers)
      bp->style = std::min(kBlockoutDoorStyles - 1, std::max(0, o.style));
      bp->frame_width = size(o.frame_width), bp->frame_depth = size(o.frame_depth), bp->arch_rise = thick(o.arch_rise);
      bp->leaf = o.leaf, bp->wings = o.wings, bp->leaf_thickness = size(o.leaf_thickness), bp->open_amount = finite_bits(o.open_amount) ? std::min(1.0f, std::max(0.0f, o.open_amount)) : 0.0f;
      bp->hinge_side = o.hinge == 1 ? 1 : 0, bp->slide_direction = o.slide == 1 ? 1 : 0, bp->drum_radius = size(o.drum_radius);
    }
  }
  else {
    if (bp->shape == 0 || bp->shape == 3 || bp->shape == 4) bp->size = {size(o.a), size(o.c), size(o.b)};
    else if (bp->shape == 1) bp->radius = size(o.a), bp->height = size(o.c);
    else if (bp->shape == 2) bp->radius = size(o.a);
    else if (bp->shape == 6) bp->height = size(o.c);
    else if (bp->shape == 5) bp->radius = size(o.a), bp->inner_radius = std::max(0.0f, std::min(o.b, bp->radius - kBlockoutMinSize)), bp->height = size(o.c);
    if (bp->is_room()) bp->wall = thick(o.d), bp->floor = thick(o.e), bp->ceiling = thick(o.f);
    else bp->operation = std::max(0, std::min(2, o.op));
    /* (An arch is as deep along its Z as `height`: one the tool drew on a face grows from the face - round 495; base_up is 0 for any other.) */
    g->set_world_position(o.base_p + o.base_up * (bp->shape == 5 ? bp->height * 0.5f : blockout_floor_below_origin(*bp)));
    if (o.turnable && o.turn != o.turn_done) {
      /* Round 496: the Turn row. The sizes above are the unturned ones' numbers as they stand in the rows; a turn swaps a wedge's or stairs' footprint
       * sizes in the piece and in the rows alike, and an arch that turns about its footprint's middle moves its feet - so the place it is remembered
       * at moves too. */
      const int quarters = o.turn - o.turn_done;
      o.turn_done = o.turn;
      blockout_turn_piece(*g, quarters);
      if (bp->shape != 5 && (quarters & 1)) std::swap(o.a, o.b);
      if (bp->shape == 5) o.base_p = g->world_position() - o.base_up * (bp->height * 0.5f);
    }
  }
  edit_epoch::bump();  // (the Blockout's walk looks again)
  amend_last_step();
}

/* Round 560 (task 0542): a panel taller than the Scene view grew up over the view's toolbar and under the tab strip, and
 * its rows could not be reached. It now stays inside the view; the header stays put and the rows below it scroll. */
Editor::AdjustPanelPlace Editor::adjust_panel_place(const Recti &view, int w, int head_h, int body_h) {
  auto &u = ui_;
  const int margin = u.px(10);
  const int room = std::max(head_h, view.h - 2 * margin);
  const int h = std::min(head_h + body_h, room);
  AdjustPanelPlace p;
  p.r = {view.x + margin, std::max(view.y, view.bottom() - h - margin), w, h};
  p.body = {p.r.x, p.r.y + head_h, w, h - head_h};
  p.scrolls = body_h > p.body.h && p.body.h > 0;
  return p;
}

/* The panel, at the bottom left of the Scene view as Edit Mode's is: a title, then the operation's numbers. */
void Editor::draw_obj_last_panel(const Recti &view) {
  auto &u = ui_;
  ObjLastOp &o = obj_last_;
  struct Row { const char *label; float *f; int *i; bool *b; float speed, mn, mx; int imn, imx; const char *const *words = nullptr; };  // (words: the int's choices by name, a dropdown)
  static const char *const kOperations[] = {"Add", "Subtract", "Intersect"};
  std::vector<Row> rows;
  std::string title;
  if (o.kind == "primitive") {
    title = "Add " + o.what;
    const bool radial = o.what == "Sphere" || o.what == "Icosphere" || o.what == "Quad Sphere" || o.what == "Cylinder" || o.what == "Cone";
    static const char *const kQuadSphereMethods[] = {"Even Area", "Even Angle"};
    if (o.what == "Torus") rows.push_back({"Major Radius", &o.a, nullptr, nullptr, 0.01f, 0.001f, 1000.0f, 0, 0}), rows.push_back({"Minor Radius", &o.b, nullptr, nullptr, 0.01f, 0.001f, 1000.0f, 0, 0});
    else rows.push_back({radial ? "Radius" : "Size", &o.a, nullptr, nullptr, 0.01f, 0.001f, 10000.0f, 0, 0});
    if (o.what == "Cylinder" || o.what == "Cone") rows.push_back({"Height", &o.b, nullptr, nullptr, 0.01f, 0.001f, 10000.0f, 0, 0});
    if (o.what == "Sphere" || o.what == "Cylinder" || o.what == "Cone" || o.what == "Torus") rows.push_back({"Segments", nullptr, &o.n, nullptr, 0, 0, 0, 3, 256});
    if (o.what == "Sphere") rows.push_back({"Rings", nullptr, &o.m, nullptr, 0, 0, 0, 2, 256});
    if (o.what == "Torus") rows.push_back({"Ring Segments", nullptr, &o.m, nullptr, 0, 0, 0, 3, 256});
    if (o.what == "Icosphere") rows.push_back({"Subdivisions", nullptr, &o.n, nullptr, 0, 0, 0, 0, 6});
    if (o.what == "Quad Sphere") {
      rows.push_back({"Subdivisions", nullptr, &o.n, nullptr, 0, 0, 0, 1, 128});
      rows.push_back({"Method", nullptr, &o.m, nullptr, 0, 0, 0, 0, 1, kQuadSphereMethods});
    }
    if (o.what == "Plane") rows.push_back({"Subdivisions", nullptr, &o.n, nullptr, 0, 0, 0, 1, 256});
  }
  else if (o.kind == "uv") {
    title = o.what == "unwrap" ? "Unwrap" : o.what == "smart" ? "Smart UV Project" : "Pack Islands";
    if (o.what == "smart") rows.push_back({"Angle Limit", &o.b, nullptr, nullptr, 0.5f, 1.0f, 89.0f, 0, 0});
    rows.push_back({"Margin", &o.a, nullptr, nullptr, 0.002f, 0.0f, 0.25f, 0, 0});
    if (o.what == "pack") rows.push_back({"Rotate", nullptr, nullptr, &o.flag, 0, 0, 0, 0, 0});
  }
  else if (o.kind == "blockout") {  // (round 479: the piece just made; n 0 box, wedge or stairs, 1 cylinder, 2 sphere, 3 door or window, 4 outline, 5 arch, 6 mesh)
    title = "Blockout " + o.what;
    const float mn = kBlockoutMinSize, mx = 10000.0f;
    if (o.n == 3) {
      const bool door = o.opening == 1;  // (round 499: a door's style words and rows)
      if (o.style >= 0)
        rows.push_back({"Style", nullptr, &o.style, nullptr, 0, 0, 0, 0, (door ? kBlockoutDoorStyles : kBlockoutWindowStyles) - 1, door ? kBlockoutDoorStyleNames : kBlockoutWindowStyleNames});  // (round 498: a dropdown of words)
      rows.push_back({"Width", &o.a, nullptr, nullptr, 0.01f, mn, mx, 0, 0});
      rows.push_back({"Head", &o.b, nullptr, nullptr, 0.01f, mn, mx, 0, 0});
      rows.push_back({"Sill", &o.c, nullptr, nullptr, 0.01f, 0.0f, mx, 0, 0});
      if (door && o.style > kDoorOpeningOnly) {  // (round 499: a door's rows, as the Inspector shows them)
        const bool revolving = o.style == kDoorRevolving;
        if (!revolving) {
          rows.push_back({"Frame Width", &o.frame_width, nullptr, nullptr, 0.005f, mn, mx, 0, 0});
          rows.push_back({"Frame Depth", &o.frame_depth, nullptr, nullptr, 0.005f, mn, mx, 0, 0});
          if (o.style == kDoorArched) rows.push_back({"Arch Rise", &o.arch_rise, nullptr, nullptr, 0.01f, 0.0f, mx, 0, 0});
        }
        /* (Round 515: the leaf is an object under the door; for a Revolving door the Leaf row is its wings, off by default.) */
        rows.push_back({"Leaf", nullptr, nullptr, revolving ? &o.wings : &o.leaf, 0, 0, 0, 0, 0});
        if (revolving ? o.wings : o.leaf) {
          rows.push_back({"Leaf Thickness", &o.leaf_thickness, nullptr, nullptr, 0.005f, mn, mx, 0, 0});
          rows.push_back({"Open Amount", &o.open_amount, nullptr, nullptr, 0.01f, 0.0f, 1.0f, 0, 0});
          if (o.style == kDoorSingle || o.style == kDoorArched) rows.push_back({"Hinge Side", nullptr, &o.hinge, nullptr, 0, 0, 0, 0, 1, kBlockoutSideNames});
          if (o.style == kDoorSliding) rows.push_back({"Slide Direction", nullptr, &o.slide, nullptr, 0, 0, 0, 0, 1, kBlockoutSideNames});
        }
        if (revolving) rows.push_back({"Drum Radius", &o.drum_radius, nullptr, nullptr, 0.01f, mn, mx, 0, 0});
      }
      if (!door && o.style > kWindowOpeningOnly) {  // (only the rows the style reads, as the Inspector shows them)
        rows.push_back({"Frame Width", &o.frame_width, nullptr, nullptr, 0.005f, mn, mx, 0, 0});
        rows.push_back({"Frame Depth", &o.frame_depth, nullptr, nullptr, 0.005f, mn, mx, 0, 0});
        if (o.style == kWindowGrid) {
          rows.push_back({"Panes Across", nullptr, &o.panes_across, nullptr, 0, 0, 0, 1, 12});
          rows.push_back({"Panes Up", nullptr, &o.panes_up, nullptr, 0, 0, 0, 1, 12});
        }
        if (o.style == kWindowArched) rows.push_back({"Arch Rise", &o.arch_rise, nullptr, nullptr, 0.01f, 0.0f, mx, 0, 0});
        rows.push_back({"Glass", nullptr, nullptr, &o.glass, 0, 0, 0, 0, 0});
        if (o.style != kWindowFixed && o.style != kWindowRound) {
          rows.push_back({"Sill Board", nullptr, nullptr, &o.sill_board, 0, 0, 0, 0, 0});
          if (o.sill_board) rows.push_back({"Sill Overhang", &o.sill_overhang, nullptr, nullptr, 0.005f, 0.0f, mx, 0, 0});
        }
      }
    }
    else if (o.n == 7) {  // (round 500: a preset's numbers, as the Inspector shows them: what the preset reads)
      BlockoutPiece &p = o.pre;
      const int k = p.preset;
      const bool column = k == kPresetColumn || k == kPresetPilaster, stairs = k == kPresetStairsL || k == kPresetStairsU;
      auto f = [&](const char *label, float *v, float speed, float lo, float hi) { rows.push_back({label, v, nullptr, nullptr, speed, lo, hi, 0, 0}); };
      auto i = [&](const char *label, int *v, int lo, int hi) { rows.push_back({label, nullptr, v, nullptr, 0, 0, 0, lo, hi}); };
      auto b = [&](const char *label, bool *v) { rows.push_back({label, nullptr, nullptr, v, 0, 0, 0, 0, 0}); };
      if (column) {
        f("Radius", &p.radius, 0.01f, mn, mx), f("Height", &p.height, 0.01f, mn, mx);
        if (k == kPresetColumn) b("Square Shaft", &p.square);
        f("Base Height", &p.base_height, 0.01f, 0.0f, mx), f("Capital Height", &p.capital_height, 0.01f, 0.0f, mx), f("Base Width", &p.base_width, 0.01f, mn, mx);
        f("Capital Width", &p.capital_width, 0.01f, mn, mx), f("Taper", &p.taper, 0.01f, 0.05f, 2.0f);
        if (k == kPresetColumn) i("Segments", &p.segments, 3, 256);
      }
      if (k == kPresetBeam) f("Length", &p.size.x, 0.01f, mn, mx), f("Depth", &p.size.y, 0.01f, mn, mx), f("Width", &p.size.z, 0.01f, mn, mx);
      if (k == kPresetRailing) {
        f("Length", &p.length, 0.01f, mn, mx), f("Rail Height", &p.rail_height, 0.01f, mn, mx), f("Post Spacing", &p.post_spacing, 0.01f, mn, mx);
        f("Post Size", &p.post_size, 0.005f, mn, mx), f("Rail Size", &p.rail_size, 0.005f, mn, mx), b("Bottom Rail", &p.bottom_rail);
      }
      if (k == kPresetRoof) {
        rows.push_back({"Roof Style", nullptr, &p.roof_style, nullptr, 0, 0, 0, 0, kBlockoutRoofStyles - 1, kBlockoutRoofStyleNames});
        f("Length", &p.length, 0.01f, mn, mx), f("Span", &p.span, 0.01f, mn, mx), f("Pitch", &p.pitch, 0.5f, 0.0f, 85.0f), f("Overhang", &p.overhang, 0.01f, 0.0f, mx);
        f("Thickness", &p.thickness, 0.01f, 0.0f, mx);
      }
      if (stairs) f("Flight Width", &p.stair_width, 0.01f, mn, mx), f("Total Height", &p.total_height, 0.01f, mn, mx), f("Length", &p.length, 0.01f, mn, mx);
      if (k == kPresetSpiral) f("Radius", &p.radius, 0.01f, mn, mx), f("Inner Radius", &p.inner_radius, 0.01f, 0.0f, mx), f("Total Height", &p.total_height, 0.01f, mn, mx), f("Turn", &p.turn, 1.0f, -1440.0f, 1440.0f);
      if (stairs || k == kPresetSpiral) i("Steps", &p.steps, 1, 256), b("Fill Under", &p.fill_under);
      if (k == kPresetArcade) {
        i("Count", &p.count, 1, 256);
        f("Span", &p.span, 0.01f, mn, mx), f("Height", &p.height, 0.01f, mn, mx), f("Column Width", &p.column_width, 0.01f, mn, mx), f("Arch Rise", &p.arch_rise, 0.01f, 0.0f, mx);
        f("Depth", &p.depth, 0.01f, mn, mx), f("Arch Thickness", &p.arch_thickness, 0.01f, mn, mx);
      }
      if (o.turnable) rows.push_back({"Quarter Turns", nullptr, &o.turn, nullptr, 0, 0, 0, 0, 3});  // (stairs: quarter turns about the origin; a spiral has its own Turn in degrees)
    }
    else if (o.n == 6) rows.push_back({"Operation", nullptr, &o.op, nullptr, 0, 0, 0, 0, 2, kOperations});  // (a mesh piece's numbers are its mesh's)
    else {
      if (o.n != 4) rows.push_back({o.n == 0 ? "Width" : "Radius", &o.a, nullptr, nullptr, 0.01f, mn, mx, 0, 0});
      if (o.n == 5) rows.push_back({"Inner Radius", &o.b, nullptr, nullptr, 0.01f, 0.0f, mx, 0, 0});
      if (o.n == 0) rows.push_back({"Depth", &o.b, nullptr, nullptr, 0.01f, mn, mx, 0, 0});
      if (o.n != 2) rows.push_back({o.n == 5 ? "Depth" : "Height", &o.c, nullptr, nullptr, 0.01f, mn, mx, 0, 0});
      if (o.turnable) rows.push_back({"Turn", nullptr, &o.turn, nullptr, 0, 0, 0, 0, 3});  // (round 496: quarter turns round the surface's normal, the footprint kept)
      if (o.flag) {
        rows.push_back({"Wall", &o.d, nullptr, nullptr, 0.01f, 0.0f, 1000.0f, 0, 0});
        rows.push_back({"Floor", &o.e, nullptr, nullptr, 0.01f, 0.0f, 1000.0f, 0, 0});
        rows.push_back({"Ceiling", &o.f, nullptr, nullptr, 0.01f, 0.0f, 1000.0f, 0, 0});
      }
      else rows.push_back({"Operation", nullptr, &o.op, nullptr, 0, 0, 0, 0, 2, kOperations});  // (0 Add, 1 Subtract, 2 Intersect: words, round 496)
    }
  }
  else if (o.kind == "pose") {  // (round 541: Influence is soft 0-1 as Blender's factor; the field takes more)
    title = o.what;
    rows.push_back({"Influence", &o.a, nullptr, nullptr, 0.01f, -10.0f, 10.0f, 0, 0});
    rows.push_back({"Flipped", nullptr, nullptr, &o.flag, 0, 0, 0, 0, 0});
  }
  else {
    title = "Draw " + o.what;
    if (o.draw_shape == 1) rows.push_back({"Fillet", &o.a, nullptr, nullptr, 0.005f, 0.0f, 100.0f, 0, 0});
    else rows.push_back({o.draw_shape == 4 ? "Sides" : "Segments", nullptr, &o.n, nullptr, 0, 0, 0, 3, 256});
  }
  const int rh = u.row_h(), pad = u.px(6), w = std::min(u.px(270), view.w - u.px(20));
  const int nrows = obj_last_open_ ? (int)rows.size() : 0;
  const int head_h = u.px(3) + rh + u.px(2);  // (the first row starts here: hr's bottom and 2 px)
  const int body_h = rh + u.px(6) + nrows * (rh + u.px(2)) + (nrows ? pad : 0) - head_h;  // (the panel's height as before, less the header)
  const AdjustPanelPlace place = adjust_panel_place(view, w, head_h, body_h);
  const Recti r = place.r;
  obj_last_rect_ = r;
  u.frame(r, (u.theme.popup & 0x00FFFFFFu) | (235u << 24), u.theme.border, u.px(4));
  const Recti hr{r.x + pad, r.y + u.px(3), r.w - pad * 2, rh};
  const int a = u.font.line_height() - u.px(4);
  u.draw_icon(obj_last_open_ ? ui::Icon::ArrowDown : ui::Icon::ArrowRight, {hr.x, hr.y + (rh - a) / 2, a, a}, u.theme.text);
  u.label({hr.x + a + u.px(6), hr.y, hr.w - a - rh, rh}, title, u.theme.text_bright);
  if (u.record_buttons) u.button_rects["adjust:title"] = hr;
  if (u.hovered(hr) && u.in.pressed[0]) {
    obj_last_open_ = !obj_last_open_;
    u.consume_click();
  }
  bool changed = false;
  const int style_was = o.style;
  const int off = place.scrolls ? u.begin_scroll(u.id("objlast_scroll_" + o.kind + o.what), place.body, body_h) : 0;  // (each operation opens at its first row)
  const int rw = r.w - (place.scrolls ? u.px(10) : 0);  // (the rows leave the scrollbar its strip)
  int y = hr.bottom() + u.px(2) - off;
  for (size_t k = 0; k < (size_t)nrows; k++, y += rh + u.px(2)) {
    const Row &row = rows[k];
    const Recti lr{r.x + pad, y, u.px(110), rh}, fr{r.x + pad + u.px(112), y, rw - pad * 2 - u.px(112), rh};
    u.label(lr, row.label);
    if (u.record_buttons) u.button_rects[std::string("adjust:") + row.label] = fr;
    const ui::Id id = u.id(std::string("objlast_") + row.label);
    if (row.f) changed |= u.float_field(id, fr, *row.f, row.speed, row.mn, row.mx, "%.3g");
    else if (row.i && row.words) changed |= u.combo(id, fr, *row.i, row.words, row.imx + 1);
    else if (row.i) changed |= u.int_field(id, fr, *row.i, row.imn, row.imx);
    else if (row.b) changed |= u.checkbox({fr.x, fr.y + u.px(2), rh - u.px(4), rh - u.px(4)}, *row.b);
  }
  if (place.scrolls) u.end_scroll();
  if (changed && o.kind == "blockout" && o.n == 3 && o.opening != 0 && style_was >= 0 && o.style >= 0 && o.style != style_was && !(o.opening == 2 && style_was == kWindowRound)) {  // (a new style's preset size, over the old style's only; a Round window keeps its size)
    float ow, oh, nw, nh;
    blockout_opening_preset(o.opening, style_was, ow, oh);
    blockout_opening_preset(o.opening, o.style, nw, nh);
    if (o.a == ow && o.b == oh) o.a = nw, o.b = nh;  // (a Round window keeps its size when switched away: its Head is its circle's, as the Inspector does)
  }
  if (changed && o.kind == "blockout" && o.n == 3 && o.opening == 2 && o.style == kWindowRound && style_was != kWindowRound) o.b = o.c + o.a;  // (a Round window starts as a circle: Head = Sill + Width)
  if (changed) obj_last_rerun();
}

}  // namespace bl

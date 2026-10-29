// SPDX-License-Identifier: GPL-2.0-or-later
// The status bar's hint as a table (task 0254, ADR 0085 F3): rows of {name, when, text} in priority order - the Wheel,
// the modals, the tools with steps, the jobs, the modes, Record, the gizmo drag, the UV editor, Tablet Mode, then the
// Object Mode tools with the feel's navigation. Every mode contributes a row that names it, the shortcuts in the text
// come from the keymap, and the tests can list the rows and ask which one is showing (Nielsen H10: help and
// documentation where the hand is; recognition over recall).
#include "editor.h"

#include "../core/core.h"

#include <cstring>

namespace bl {

namespace {

std::string capitalised(std::string s) {
  if (!s.empty()) s[0] = (char)std::toupper((unsigned char)s[0]);
  return s;
}

}  // namespace

/* The preset's chord for a label (a menu item, a tooltip, a button): nothing when the preset has none or the action is
 * not one (F4: the label says what the keys do, not what Unity's would). */
std::string Editor::key(const char *action) const { return action && *action ? shortcut_text(action) : std::string(); }

std::string Editor::with_key(const std::string &label, const char *action) const {
  const std::string k = key(action);
  return k.empty() ? label : label + " (" + k + ")";
}

/* An edit operation's keys: an action id in the table gives the preset's chord; a gesture ("Double-click") stays as written. */
std::string Editor::op_keys(const EditOpInfo &o) const {
  if (!o.keys || !o.keys[0]) return std::string();
  return std::strchr(o.keys, '.') ? shortcut_text(o.keys) : std::string(o.keys);
}

/* A chord from the keymap for the hint's text, or the action's name when the preset has none. */
std::string Editor::hint_key(const char *action, const char *fallback) const {
  const std::string k = shortcut_text(action);
  return k.empty() ? std::string(fallback) : k;
}

const std::vector<Editor::HintRow> &Editor::hint_rows() const {
  static const std::vector<HintRow> rows = {
      {"wheel", [](const Editor &e) { return e.qm_.open; },
       [](const Editor &) { return std::string("The Wheel: tap or flick a slice  |  1-8, or its first letters  |  Shift: keep it open  |  Esc: back, close"); }},
      {"knife", [](const Editor &e) { return e.knife_.active; },
       [](const Editor &e) {
         return std::string(e.knife_.has_first ? "Knife: click the second point to cut" : "Knife: click the first point of the cut") + "  |  Esc / Enter: done";
       }},
      {"transform modal", [](const Editor &e) { return e.xf_.active; },
       [](const Editor &e) {
         static const char *const kModal[] = {"Move", "Rotate", "Scale"};
         return std::string(kModal[std::max(0, std::min(2, e.xf_.mode))]) + ": move the mouse  |  X Y Z: lock an axis  |  type a value  |  Enter: apply  |  Esc: cancel";
       }},
      {"operator modal", [](const Editor &e) { return e.modal_.active; },
       [](const Editor &e) { return capitalised(e.modal_.op) + ": move the mouse away from the centre for more  |  Shift: finer  |  ] / [: segments or count  |  type a value  |  Enter: apply  |  Esc: cancel"; }},
      {"push/pull", [](const Editor &e) { return e.pp_.active; },
       [](const Editor &) { return std::string("Push/Pull: move the mouse to push or pull the face  |  type a distance  |  Ctrl: keep the original face  |  click or Enter: done  |  Esc or right-click: cancel"); }},
      {"edit origin", [](const Editor &e) { return e.origin_edit_; },
       [](const Editor &) { return std::string("Edit Origin: drag the gizmo to move the object's pivot (the mesh stays)  |  Done, or Esc: finish"); }},
      {"pilot", [](const Editor &e) { return e.pilot_cam_ != 0; },
       [](const Editor &e) {
         const GameObject *g = e.scene_->find(e.pilot_cam_);
         return "Piloting " + (g ? g->name : std::string("the camera")) + ": fly the view and the camera follows  |  Stop Piloting, or Esc: leave it where it is";
       }},
      {"draw", [](const Editor &e) { return e.draw_.active; },
       [](const Editor &e) {
         return std::string("Draw ") + kDrawShapes[std::max(0, std::min(kDrawShapeCount - 1, e.draw_.shape))] +
                ": click the points on the surface or the grid  |  type a length  |  Enter or right-click: finish  |  Esc: cancel";
       }},
      {"sketch", [](const Editor &e) { return e.sketch_.active && !e.tablet_; },
       [](const Editor &) { return std::string("Sketch: the pen draws, its eraser erases  |  Ctrl: freehand  |  arrows, W S: move the view  |  Esc: stop"); }},
      {"gizmo drag", [](const Editor &e) { return e.drag_ == Drag::Gizmo; },
       [](const Editor &) { return std::string("Dragging: type a number for an exact amount  |  V: snap to a vertex  |  Ctrl+Shift: drop onto a surface  |  Esc: cancel"); }},
      {"uv editor", [](const Editor &e) { return e.uv_rect_.w > 0 && e.uv_rect_.contains(e.ui_.in.mx, e.ui_.in.my); },
       [](const Editor &e) {
         std::string keys;  // (the preset's chords for the three tools; a preset without them: the toolbar)
         for (const char *id : {"tool.move", "tool.rotate", "tool.scale"}) {
           const std::string k = e.key(id);
           if (!k.empty()) keys += (keys.empty() ? "" : " ") + k;
         }
         return "UV Editor: LMB: select  |  " + (keys.empty() ? std::string("the toolbar") : keys) + ": move, turn, scale the islands  |  MMB drag: pan  |  Wheel: zoom  |  Unwrap: the toolbar";
       }},
      {"remesh job", [](const Editor &e) { return e.sculpt_remesh_ != nullptr; },
       [](const Editor &) { return std::string("Quad Remesh: working on a worker...  |  Esc in the Scene view (Sculpt Mode), the tray's Cancel or `sculpt remesh cancel`: stop it at its next stage"); }},
      {"sculpt", [](const Editor &e) { return e.sculpt_; },
       [](const Editor &e) {
         return std::string("Sculpt (") + kSculptBrushes[std::max(0, std::min(10, e.sculpt_brush_.kind))] +
                "): drag over the mesh  |  Shift: smooth  |  Alt: carve (Draw, Clay, Inflate)  |  Ctrl: mask  |  Ctrl+Shift: isolate the group  |  " + e.brush_keys() + ": size  |  Esc: leave" +
                (e.sculpt_subdiv_levels() ? strprintf("  |  Dynamic Subdivision %d (the mask shows on the cage only)", e.sculpt_subdiv_levels()) : std::string()) +
                (e.sculpt_dyntopo_ ? strprintf("  |  Dyntopo %.2f", e.sculpt_dyntopo_detail_) : std::string());
       }},
      {"mesh maps job", [](const Editor &e) { return e.meshmaps_job_ != nullptr; },
       [](const Editor &) { return std::string("Mesh Maps: baking on a worker...  |  Esc in the Scene view, the tray's Cancel or `meshmaps cancel`: stop it at the next row"); }},
      {"paint", [](const Editor &e) { return e.paint_; },
       [](const Editor &e) { return "Paint: drag over the model (the pen's eraser end erases)  |  " + e.brush_keys() + ": size  |  Esc: leave  |  paint color|size|fill ..."; }},
      {"strokes", [](const Editor &e) { return e.strokeobj_; },
       [](const Editor &e) {
         return std::string("Strokes (") + (e.strokeobj_kind_ == 1 ? "ribbon" : "card") +
                "): tap the surface for a card, drag for a ribbon  |  Shift: erase  |  " + e.brush_keys() + ": size  |  Esc: leave  |  strokeobj kind|size|normals ...";
       }},
      {"scatter", [](const Editor &e) { return e.scatter_; },
       [](const Editor &e) { return "Scatter: drag over the surface to drop instances of the source object  |  Alt, or the pen's eraser: remove  |  " + e.brush_keys() + ": radius  |  Esc: leave  |  scatter <object>"; }},
      {"weight paint", [](const Editor &e) { return e.weight_paint_; },
       [](const Editor &e) { return "Weight Paint: drag to add weight to the active bone's group  |  Ctrl: subtract  |  the pen's eraser erases  |  " + e.brush_keys() + ": radius  |  Esc: leave"; }},
      {"surface job", [](const Editor &e) { return e.surface_job_ != nullptr; },
       [](const Editor &) { return std::string("Surface: evaluating the stack on a worker (the views show the last result until it lands)"); }},
      {"hidden faces", [](const Editor &e) { return e.hidden_face_count(e.active_object()) != 0; },
       [](const Editor &e) {
         const size_t hidden = e.hidden_face_count(e.active_object());
         return strprintf("%zu face%s of '%s' hidden (Sculpt > Show All, or `sculpt show all`)", hidden, hidden == 1 ? "" : "s", e.active_object()->name.c_str());
       }},
      {"tablet scene", [](const Editor &e) { return e.tablet_ && e.scene_hovered_; },
       [](const Editor &) { return std::string("One finger: orbit, tap: select, double tap: frame  |  Two fingers: zoom, pan, twist  |  Pen side button, or a hold: the Wheel"); }},
      {"tablet", [](const Editor &e) { return e.tablet_; },
       [](const Editor &) { return std::string("Tablet Mode: a finger scrolls lists  |  Pen side button, or a pen or finger held still: the Wheel"); }},
      {"elsewhere", [](const Editor &e) { return !e.scene_hovered_; },
       [](const Editor &e) { return e.hint_key("edit.palette", "Ctrl+K") + ": find any command  |  " + e.hint_key("edit.favorites", "Shift+Q") + ": Quick Favorites"; }},
      {"edit mode", [](const Editor &e) { return e.edit_mode_; },
       [](const Editor &e) {
         static const char *const kEls[] = {"vertices", "edges", "faces"};
         static const char *const kTools[] = {"drag: pan", "the gizmo moves them", "the gizmo turns them", "the gizmo scales them", "the gizmo moves, turns, scales them"};
         return std::string("Edit Mode (") + kEls[(int)e.elem_] + "): LMB: select  |  Shift or Ctrl+LMB: toggle  |  " + kTools[(int)e.tool_] + "  |  " + e.navigation_hint();
       }},
      {"record", [](const Editor &e) { return e.anim_.record && !e.playing_; },
       [](const Editor &e) { return strprintf("RECORD: every gizmo edit keys the pose at frame %.0f (the Animation window's Record turns it off)", e.anim_.frame); }},
      {"baking", [](const Editor &e) { return e.baking_; },
       [](const Editor &) { return std::string("Baking lighting: the status bar's tray shows the progress, its Cancel stops it; the views keep working meanwhile"); }},
      {"rendering", [](const Editor &e) { return e.rendering_ && !e.render_preview_; },
       [](const Editor &) { return std::string("Rendering: the Render window shows the picture as it resolves  |  Stop there, or the tray's Cancel  |  Save... when it is done"); }},
      {"object tools", [](const Editor &) { return true; },
       [](const Editor &e) {
         switch (e.tool_) {
           case Tool::View: return "View tool: LMB drag: pan  |  " + e.navigation_hint();
           case Tool::Move: return "Move: LMB: select  |  drag an arrow or plane: move along it  |  " + e.navigation_hint();
           case Tool::Rotate: return "Rotate: LMB: select  |  drag a ring: turn about its axis  |  " + e.navigation_hint();
           case Tool::Scale: return "Scale: LMB: select  |  drag a handle: scale along it, the centre: all axes  |  " + e.navigation_hint();
           default: return "Transform: LMB: select  |  arrows move, rings turn, the centre box scales  |  " + e.navigation_hint();
         }
       }},
  };
  return rows;
}

/* The feel's navigation, for the rows that end with it. */
std::string Editor::navigation_hint() const {
  switch (nav_style_) {
    case NavStyle::Unity: return "RMB: fly  |  MMB: pan  |  Alt+LMB: orbit  |  Wheel: zoom";
    case NavStyle::Unreal: return "RMB: fly  |  MMB: pan  |  Alt+LMB: orbit  |  Wheel: zoom";
    case NavStyle::Blender: return "MMB: orbit  |  Shift+MMB: pan  |  Wheel: zoom";
    case NavStyle::Maya: return "Alt+LMB: orbit  |  Alt+MMB: pan  |  Alt+RMB: zoom";
  }
  return "";
}

const Editor::HintRow &Editor::hint_row_now() const {
  const std::vector<HintRow> &rows = hint_rows();
  for (const HintRow &r : rows)
    if (r.when(*this)) return r;
  return rows.back();
}

/* What the mouse and the keys do where the mouse is: the first row whose condition holds. */
std::string Editor::statusbar_hint() const { return hint_row_now().text(*this); }

std::vector<std::string> Editor::hint_row_names_for_test() const {
  std::vector<std::string> names;
  for (const HintRow &r : hint_rows()) names.push_back(r.name);
  return names;
}

std::string Editor::statusbar_hint_row_for_test() const { return hint_row_now().name; }

}  // namespace bl

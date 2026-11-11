// SPDX-License-Identifier: GPL-2.0-or-later
// `perf drag`, `perf clip` and `perf pick` (round 468, task 0444): a drag played as real mouse and key events through the
// editor's own input path, so the gizmo, Puppet Posing, G / R, Record and the Animation window's ruler run exactly as
// they do under a hand - and what a frame of it costs is logged with its parts. A small state machine the frame steps:
// Editor::frame calls perf_script_before (the system's mouse and keys dropped while it plays, the step's events
// appended) and perf_script_after (the frame's time recorded, the next step chosen).
//   Probe  a mouse move over the Scene view, so the gizmo says where the asked handle is (gizmo only)
//   Aim    a mouse move to the press point
//   Press  the mouse down (for G / R: the key, tapped)
//   Move   N mouse moves, each an equal step further; the first is the "first move"
//   Release the mouse up (for G / R: a click that confirms)
//   After  one plain frame
// A refusal logs why, presses nothing (or releases what it pressed) and leaves no script active.
#include "editor.h"

#include "../scene/humanoid.h"

#include <cmath>
#include <cstdlib>

namespace bl {

using namespace platform;

namespace {

bool parse_number(const std::string &s, float &out) {
  char *end = nullptr;
  const double v = std::strtod(s.c_str(), &end);
  if (end == s.c_str() || *end || !std::isfinite(v)) return false;
  out = (float)v;
  return true;
}

/* "ctrl+shift" -> the modifier bits; -1 for a word that isn't one. */
int parse_mods(const std::string &s) {
  int mods = 0;
  for (size_t at = 0; at <= s.size();) {
    size_t end = s.find('+', at);
    if (end == std::string::npos) end = s.size();
    const std::string w = to_lower(s.substr(at, end - at));
    at = end + 1;
    if (w == "ctrl") mods |= MOD_CTRL;
    else if (w == "shift") mods |= MOD_SHIFT;
    else if (w == "alt") mods |= MOD_ALT;
    else return -1;
  }
  return mods;
}

/* Lower case without spaces, dashes or underscores: "Left Upper Arm", "left_upper_arm" and "leftupperarm" are one name. */
std::string squeezed(const std::string &s) {
  std::string out;
  for (char c : s)
    if (c != ' ' && c != '_' && c != '-') out += (char)std::tolower((unsigned char)c);
  return out;
}

Event mouse_event(EventType t, int x, int y, int mods) {
  Event e;
  e.type = t;
  e.x = x, e.y = y, e.mods = mods;
  return e;
}

Event key_event(EventType t, int key, int mods) {
  Event e;
  e.type = t;
  e.key = key, e.mods = mods;
  return e;
}

/* What the system sends that a playing script takes the place of (a hand on the mouse must not disturb a measurement).
 * A key's release passes: it can start nothing, and a key that was down when the script began (the Enter that entered
 * the command) must not stay down. A lost focus is held back by perf_script_before - it would let go of the script's
 * own button - and handed on when the script has ended. */
bool is_input_event(EventType t) {
  switch (t) {
    case EventType::MouseMove: case EventType::MouseDown: case EventType::MouseUp: case EventType::Wheel:
    case EventType::KeyDown: case EventType::Text: case EventType::Gesture: case EventType::Drop: return true;
    default: return false;
  }
}

const char *kPerfDragUsage =
    "perf drag gizmo <x|y|z|rx|ry|rz|free> <pixels> [frames N] [part <part>] [mods ctrl+shift+alt] | puppet <lhand|rhand|lfoot|rfoot|head|hips> <dx> <dy> | "
    "key <g|r> <dx> <dy> | ruler <from frame> <to frame> | at <x0> <y0> <x1> <y1> | stop   (any of: queue, loop, frames N)";

/* The short words for the handles' parts (a part is otherwise named as humanoid::kPartNames has it). */
int part_by_word(const std::string &word) {
  const std::string w = squeezed(word);
  static const std::pair<const char *, int> shorts[] = {{"lhand", humanoid::LHand}, {"rhand", humanoid::RHand}, {"lfoot", humanoid::LFoot},
                                                         {"rfoot", humanoid::RFoot}, {"head", humanoid::Head}, {"hips", humanoid::Hips}};
  for (const auto &s : shorts)
    if (w == s.first) return s.second;
  for (int p = 0; p < humanoid::kParts; p++)
    if (w == squeezed(humanoid::kPartNames[p])) return p;
  return -1;
}

/* The first armature at or below `g`, depth first (an imported character's root sits above its model's root). */
GameObject *armature_below(GameObject *g) {
  std::vector<GameObject *> todo;
  if (g) todo.push_back(g);
  while (!todo.empty()) {
    GameObject *o = todo.back();
    todo.pop_back();
    if (o->get<Armature>()) return o;
    for (auto it = o->children.rbegin(); it != o->children.rend(); ++it) todo.push_back(*it);
  }
  return nullptr;
}

/* The rig a perf verb works on: the one above the active object (`above`: Puppet Posing's lookup), else the first
 * armature below the active object, else the scene's first armature - whatever its humanoid map makes of it (a rig
 * whose arms the analysis doesn't find still has a chest and legs to measure). */
GameObject *rig_for(GameObject *above, GameObject *active, const Scene &scene) {
  if (above) return above;
  if (GameObject *below = armature_below(active)) return below;
  GameObject *first = nullptr;
  scene.for_each([&](GameObject &g) {
    if (!first && g.get<Armature>()) first = &g;
  });
  return first;
}

}  // namespace

/* ------------------------------------------------------------- perf pick / perf clip */

/* The bone of a humanoid part on the rig above the active object (the lookup Puppet Posing uses), else on the rig below
 * it or the scene's first (rig_for). */
GameObject *Editor::perf_part_bone(const std::string &word, std::string &error) {
  GameObject *arm = rig_for(puppet_rig_of(active_object()), active_object(), *scene_);
  if (!arm) return error = "no rig in the scene", nullptr;
  const int part = part_by_word(word);
  if (part < 0) return error = "no humanoid part called '" + word + "' (a name such as Left Hand or Chest, or lhand rhand lfoot rfoot head hips)", nullptr;
  GameObject *bone = humanoid::map(*scene_, *arm).part[part];
  if (!bone) return error = std::string("the rig ") + arm->name + " has no " + humanoid::kPartNames[part], nullptr;
  return bone;
}

void Editor::perf_pick(const std::string &word) {
  std::string error;
  GameObject *bone = perf_part_bone(word, error);
  if (!bone) return Log::warn("perf pick: %s", error.c_str());
  select(bone->id);
  Log::info("perf pick: %s is '%s'", word.c_str(), bone->name.c_str());
}

/* A clip on the active rig for the scrub and play rows: every bone under its armature keyed on Rotation x, y and z at
 * frames 0, N/2 and N with a small turn (a few degrees, different per bone, back to the start at N), and each IK target
 * under the rig's root on Position with a small move; the playhead at frame 0. No dependence on the rig's names. */
void Editor::perf_clip(const std::vector<std::string> &t) {
  int n = 30;
  for (size_t i = 2; i + 1 < t.size(); i++)
    if (to_lower(t[i]) == "frames") n = std::max(2, std::min(100000, std::atoi(t[i + 1].c_str())));
  GameObject *arm = rig_for(puppet_rig_of(active_object()), active_object(), *scene_);
  if (!arm) return Log::warn("perf clip: no rig in the scene");
  GameObject *root = rig_root_of(arm);
  if (!root) root = arm;
  std::vector<GameObject *> bones, targets;
  std::vector<GameObject *> todo{arm};
  while (!todo.empty()) {
    GameObject *g = todo.back();
    todo.pop_back();
    for (auto it = g->children.rbegin(); it != g->children.rend(); ++it) todo.push_back(*it);
    if (g != arm && g->get<Bone>()) bones.push_back(g);
  }
  scene_->for_each([&](GameObject &g) {
    const RigControl *rc = g.get<RigControl>();
    if (rc && rc->role == RigControl::IKTarget && (&g == root || scene_->is_ancestor(root, &g))) targets.push_back(&g);
  });
  /* The Animation window works on the clip of the object at or above the active one: with an object above the rig's root
   * active (an imported character's own root) the new clip would not be the open one and the keys below would go
   * nowhere. The rig's root is selected, and stays selected. */
  select(root->id);
  if (!anim_new_clip(root, "Perf Clip")) return Log::warn("perf clip: no clip could be made on '%s'", root->name.c_str());
  const float mid = std::max(1.0f, std::floor((float)n / 2.0f)), last = (float)n;
  anim_edit_keys("Perf Clip", [&](AnimationClip &clip) {
    for (size_t k = 0; k < bones.size(); k++) {
      const std::string path = anim_path(*root, *bones[k]);
      const float turn = (3.0f + (float)(k % 5) * 1.5f) * (k % 2 ? -1.0f : 1.0f);
      for (int axis = 0; axis < 3; axis++) {
        float v = 0.0f;
        read_anim_value(*bones[k], "Rotation", axis, v);
        AnimTrack &track = clip.track(path, "Rotation", axis);
        track.set_key(0.0f, v);
        track.set_key(mid, v + turn * (axis == 0 ? 1.0f : axis == 1 ? 0.6f : 0.4f));
        track.set_key(last, v);
      }
    }
    for (size_t k = 0; k < targets.size(); k++) {
      const std::string path = anim_path(*root, *targets[k]);
      const float move = (0.04f + (float)(k % 4) * 0.02f) * (k % 2 ? -1.0f : 1.0f);
      for (int axis = 0; axis < 3; axis++) {
        float v = 0.0f;
        read_anim_value(*targets[k], "Position", axis, v);
        AnimTrack &track = clip.track(path, "Position", axis);
        track.set_key(0.0f, v);
        track.set_key(mid, v + move * (axis == 1 ? 1.0f : 0.5f));
        track.set_key(last, v);
      }
    }
  });
  anim_set_frame(0.0f);
  const AnimationClipPtr made = anim_clip();
  if (!made || made->tracks.empty()) return Log::warn("perf clip: the clip on '%s' got no keys", root->name.c_str());
  Log::info("perf clip: %zu bone(s) and %zu IK target(s) keyed at frames 0, %d and %d on '%s'", bones.size(), targets.size(), (int)mid, n, root->name.c_str());
}

/* ------------------------------------------------------------- perf drag: the command */

void Editor::perf_script_command(const std::string &sub, const std::vector<std::string> &t) {
  if (sub == "clip") return perf_clip(t);
  if (sub == "pick") {
    std::string word;
    for (size_t i = 2; i < t.size(); i++) word += (word.empty() ? "" : " ") + t[i];
    return perf_pick(word);
  }
  if (t.size() > 2 && to_lower(t[2]) == "stop") return perf_script_stop("perf drag stop");
  if (perf_.active) return Log::warn("perf drag: one is playing already (perf drag stop ends it)");
  PerfScript s;
  std::string part;
  if (!perf_drag_parse(t, s, part) || !perf_drag_begin(s, part)) return;
  const bool focus_lost = perf_.focus_lost;  // (held back by the script before this one, not handed on yet: no frame ran between them)
  perf_ = s;
  perf_.active = true, perf_.focus_lost = focus_lost;
  for (int k : {KEY_SHIFT, KEY_CTRL, KEY_ALT}) ui_.in.key_down[k] = false;  // (a modifier held as the command was entered is not the script's: its `mods` are)
  perf_result_ = PerfDragResult{};
  perf_result_.what = s.what;
  const bool at_once = headless_ && !s.queued && !s.loop;
  if (!at_once) return Log::info("perf drag %s: queued (%d move frame(s)%s; one step a frame)", s.what.c_str(), s.frames, s.loop ? ", looping until perf drag stop" : "");
  for (int i = 0; i < s.frames + 50 && perf_.active; i++) step_frame_headless();  // (capped: a step that can't finish is a bug, not a hang)
  if (perf_.active) perf_script_stop("the frame cap");
}

/* The words of a `perf drag` line: the kind, its numbers, and the options anywhere after it. False (with a warning) for
 * anything it doesn't understand. */
bool Editor::perf_drag_parse(const std::vector<std::string> &t, PerfScript &s, std::string &part) {
  if (t.size() < 3) return Log::warn("perf drag: %s", kPerfDragUsage), false;
  const std::string kind = to_lower(t[2]);
  std::vector<std::string> pos;
  for (size_t i = 3; i < t.size(); i++) {
    const std::string w = to_lower(t[i]);
    if (w == "queue") s.queued = true;
    else if (w == "loop") s.loop = true;
    else if (w == "frames" || w == "mods") {
      if (i + 1 >= t.size()) return Log::warn("perf drag: '%s' needs a value", w.c_str()), false;
      const std::string &v = t[++i];
      if (w == "frames") s.frames = std::max(1, std::min(100000, std::atoi(v.c_str())));
      else if ((s.mods = parse_mods(v)) < 0) return Log::warn("perf drag: unknown modifier in '%s' (ctrl, shift, alt, joined with +)", v.c_str()), false;
    }
    else if (w == "part") {
      for (i++; i < t.size(); i++) {
        const std::string n = to_lower(t[i]);
        if (n == "frames" || n == "mods" || n == "queue" || n == "loop") break;
        part += (part.empty() ? "" : " ") + t[i];
      }
      i--;
      if (part.empty()) return Log::warn("perf drag: 'part' needs a humanoid part (chest, Left Hand, ...)"), false;
    }
    else pos.push_back(t[i]);
  }
  auto numbers = [&](size_t first, size_t count, float *out) {
    if (pos.size() != first + count) return Log::warn("perf drag %s: %s", kind.c_str(), kPerfDragUsage), false;
    for (size_t k = 0; k < count; k++)
      if (!parse_number(pos[first + k], out[k])) return Log::warn("perf drag %s: '%s' is not a number", kind.c_str(), pos[first + k].c_str()), false;
    return true;
  };
  float v[4] = {0, 0, 0, 0};
  if (kind == "gizmo") {
    if (!numbers(1, 1, v)) return false;
    s.handle = perf_gizmo_handle(pos[0]);
    if (s.handle < 0) return Log::warn("perf drag gizmo: no handle called '%s' (x y z free rx ry rz)", pos[0].c_str()), false;
    s.kind = PerfScript::Gizmo, s.what = "gizmo " + to_lower(pos[0]), s.pixels = v[0];
    return true;
  }
  if (kind == "puppet") {
    if (!numbers(1, 2, v)) return false;
    if (puppet_handle_index(pos[0]) < 0) return Log::warn("perf drag puppet: no control called '%s' (lhand rhand lfoot rfoot head hips)", pos[0].c_str()), false;
    s.kind = PerfScript::Puppet, s.what = "puppet " + to_lower(pos[0]), s.dx = v[0], s.dy = v[1];
    return true;
  }
  if (kind == "key") {
    if (pos.empty() || (to_lower(pos[0]) != "g" && to_lower(pos[0]) != "r")) return Log::warn("perf drag key: the key is g or r"), false;
    if (!numbers(1, 2, v)) return false;
    s.kind = PerfScript::Key, s.what = "key " + to_lower(pos[0]), s.key = to_lower(pos[0]) == "g" ? KEY_G : KEY_R, s.dx = v[0], s.dy = v[1];
    return true;
  }
  if (kind == "ruler") {
    if (!numbers(0, 2, v)) return false;
    s.kind = PerfScript::Ruler, s.what = "ruler", s.from = v[0], s.to = v[1];
    return true;
  }
  if (kind == "at") {
    if (!numbers(0, 4, v)) return false;
    s.kind = PerfScript::At, s.what = "at", s.x0 = v[0], s.y0 = v[1], s.x1 = v[2], s.y1 = v[3];
    return true;
  }
  return Log::warn("perf drag: unknown kind '%s': %s", kind.c_str(), kPerfDragUsage), false;
}

/* Checks a script can start (a rig, an active object, the Animation window), selects its `part`, and works out where it
 * presses; the gizmo's point comes from its probe frame. False (with a warning that says why) leaves nothing pressed. */
bool Editor::perf_drag_begin(PerfScript &s, const std::string &part) {
  auto no = [&](const std::string why) {  // (by value: it is also called with perf_result_.error, which it resets)
    Log::warn("perf drag %s: %s", s.what.c_str(), why.c_str());
    perf_result_ = PerfDragResult{};
    perf_result_.what = s.what, perf_result_.error = why;
    return false;
  };
  /* The script's events go to the main window, and what it aims at - the gizmo's probe, the Puppet controls' hover, the
   * Animation window's ruler - is kept once for the editor, not per window: with another editor window open a press
   * could land on whatever the main window shows at another window's coordinates. (Before `part` selects anything.) */
  if (perf_other_window()) return no("another editor window is open: a script plays in the main window alone (dock or close the other window first)");
  if (!part.empty()) {
    std::string error;
    GameObject *bone = perf_part_bone(part, error);
    if (!bone) return no(error);
    select(bone->id);
  }
  if (s.kind != PerfScript::At && s.kind != PerfScript::Ruler && !active_object()) return no("no active object (select one first)");
  if (s.kind == PerfScript::Gizmo) {
    gizmo_probe_ = GizmoProbe{};
    gizmo_probe_.want = s.handle;
    s.stage = PerfScript::Probe;
    return true;
  }
  s.stage = PerfScript::Aim;
  return perf_drag_aim(s) ? true : no(perf_result_.error);
}

/* The press point and the end point of a puppet, key, ruler or at script (perf_result_.error says why when not). */
bool Editor::perf_drag_aim(PerfScript &s) {
  auto no = [&](const std::string why) {
    perf_result_ = PerfDragResult{};
    perf_result_.error = why;
    return false;
  };
  int x = 0, y = 0;
  std::string why;
  if (s.kind == PerfScript::At) return true;
  if (s.kind == PerfScript::Puppet) {
    if (!puppet_press_point(s.what.substr(7), x, y, why)) return no(why);
    s.x0 = (float)x, s.y0 = (float)y, s.x1 = s.x0 + s.dx, s.y1 = s.y0 + s.dy;
    return true;
  }
  if (s.kind == PerfScript::Key) {
    /* (In front of the camera AND inside the Scene view: G or R pressed with the mouse over another window goes to that
     *  window, or starts a modal whose mouse is outside the view.) */
    if (!project_to_window(active_object()->world_position(), x, y) || !scene_view_rect().contains(x, y)) return no("the active object's origin is not on screen");
    s.x0 = (float)x, s.y0 = (float)y, s.x1 = s.x0 + s.dx, s.y1 = s.y0 + s.dy;
    return true;
  }
  /* The Animation window's ruler: from the frame's x to the other's, on the ruler's row. */
  if (frames_ - anim_.drawn_at > 1 || anim_.sheet.w <= 0) return no("the Animation window isn't open (window Animation)");
  if (!anim_clip()) return no("the Animation window has no clip (perf clip makes one on the active rig)");
  anim_set_preview(true);  // (a scrub poses the scene only with Preview on)
  const Recti &ruler = anim_.ruler;
  const int xa = anim_x_of(anim_.sheet, s.from, anim_.sheet_end), xb = anim_x_of(anim_.sheet, s.to, anim_.sheet_end), ry = ruler.y + ruler.h / 2;
  if (xa < ruler.x || xa > ruler.right() || xb < ruler.x || xb > ruler.right()) return no("those frames are not on the ruler (it shows other frames)");
  s.x0 = (float)xa, s.y0 = (float)ry, s.x1 = (float)xb, s.y1 = (float)ry;
  return true;
}

/* ------------------------------------------------------------- perf drag: the frame's hooks */

bool Editor::perf_other_window() const {
  return std::any_of(windows_.begin(), windows_.end(), [](const std::unique_ptr<EditorWindow> &w) { return !w->close; });
}

void Editor::perf_script_before(std::vector<Event> &events) {
  if (!perf_.active) {
    if (!perf_.focus_lost) return;
    perf_.focus_lost = false;  // (the focus went while a script played: now that it has ended, the keys and buttons let go)
    Event lost;
    lost.type = EventType::FocusLost;
    events.insert(events.begin(), lost);  // (first: it happened before this frame's own events)
    return;
  }
  std::vector<Event> kept;
  perf_.esc = perf_other_window();  // (a window opened under a playing script: stopped as a real Esc stops it)
  for (Event &e : events) {
    if (e.type == EventType::KeyDown && e.key == KEY_ESCAPE) perf_.esc = true;
    if (e.type == EventType::FocusLost) {
      perf_.focus_lost = true;
      continue;
    }
    if (!is_input_event(e.type)) kept.push_back(std::move(e));  // (Repaint, Resize, DpiChanged, the close and key releases stay)
  }
  events.swap(kept);
  perf_script_events(events);
  perf_.sent = true;
}

void Editor::perf_script_after(double ms) {
  if (!perf_.sent) return;  // (started inside this frame: its first step plays in the next)
  perf_.sent = false;
  perf_script_advance(ms);
}

/* The events of the step about to play, appended after the system's non-input ones. */
void Editor::perf_script_events(std::vector<Event> &events) {
  PerfScript &s = perf_;
  const int m = s.mods;
  const bool key = s.kind == PerfScript::Key;
  auto release = [&] {  // (something held: the button, or G / R's modal)
    if (key) events.push_back(key_event(EventType::KeyDown, KEY_ESCAPE, 0)), events.push_back(key_event(EventType::KeyUp, KEY_ESCAPE, 0));
    else events.push_back(mouse_event(EventType::MouseUp, s.cx, s.cy, m));
  };
  if (s.esc && s.stage != PerfScript::Abort) {  // a real Esc (or another editor window opened): stops it and lets go
    const char *by = perf_other_window() ? "stopped: another editor window opened" : "stopped by Esc";
    perf_result_.ok = false, perf_result_.error = by;
    Log::info("perf drag %s: %s after %d move frame(s)", s.what.c_str(), by, s.moves_done);
    gizmo_probe_.want = -1;
    s.stage = s.button_down ? PerfScript::Abort : PerfScript::Done;
    if (s.button_down) release();
    else s.active = false;
    return;
  }
  if (s.stop_asked && s.stage == PerfScript::Move) s.stage = PerfScript::Release;
  switch (s.stage) {
    case PerfScript::Probe: {
      const Recti v = scene_view_rect();
      s.cx = v.x + v.w / 2, s.cy = v.y + v.h / 2;
      events.push_back(mouse_event(EventType::MouseMove, s.cx, s.cy, m));
      break;
    }
    case PerfScript::Aim:
      s.cx = (int)std::lround(s.x0), s.cy = (int)std::lround(s.y0);
      events.push_back(mouse_event(EventType::MouseMove, s.cx, s.cy, m));
      break;
    case PerfScript::Press:
      if (key) events.push_back(key_event(EventType::KeyDown, s.key, m)), events.push_back(key_event(EventType::KeyUp, s.key, m));
      else events.push_back(mouse_event(EventType::MouseDown, s.cx, s.cy, m));
      break;
    case PerfScript::Move: {
      if (s.moves_done == 0) {  // (the totals the move frames are measured between)
        s.before_total = prof_total_, s.pose_before = pose_counts(), s.mesh_before = mesh_counts();
      }
      /* A triangle wave over the path: 1..N out, then (looping) back to 0 and out again, never releasing. */
      const int n = s.frames, t = s.moves_done + 1, w = t % (2 * n), idx = w <= n ? w : 2 * n - w;
      const float f = (float)idx / (float)n;
      s.cx = (int)std::lround(s.x0 + (s.x1 - s.x0) * f), s.cy = (int)std::lround(s.y0 + (s.y1 - s.y0) * f);
      events.push_back(mouse_event(EventType::MouseMove, s.cx, s.cy, m));
      break;
    }
    case PerfScript::Release:
      perf_result_.after = prof_total_, perf_result_.pose_after = pose_counts(), perf_result_.mesh_after = mesh_counts();
      if (key) events.push_back(mouse_event(EventType::MouseDown, s.cx, s.cy, m));
      events.push_back(mouse_event(EventType::MouseUp, s.cx, s.cy, m));
      break;
    case PerfScript::After:
      if (m) events.push_back(mouse_event(EventType::MouseMove, s.cx, s.cy, 0));  // (the modifiers let go)
      break;
    case PerfScript::Abort: release(); break;
    case PerfScript::Done: break;
  }
}

/* The frame that just ran, recorded, and the next step chosen. */
void Editor::perf_script_advance(double ms) {
  PerfScript &s = perf_;
  std::string why;
  switch (s.stage) {
    case PerfScript::Probe: {
      const GizmoProbe pr = gizmo_probe_;
      gizmo_probe_.want = -1;
      const std::string handle = s.what.substr(s.what.find(' ') + 1);
      if (!pr.drawn) return perf_script_refuse("the gizmo isn't drawn (nothing active, or the tool isn't Move or Rotate)");
      if (!pr.found || !scene_view_rect().contains((int)std::lround(pr.x), (int)std::lround(pr.y)))
        return perf_script_refuse("the " + handle + " handle is not on screen (the tool must be Move for x y z free, Rotate for rx ry rz)");
      /* (Whole pixels, as a hand's events are: a test can send the same ones.) */
      s.x0 = std::round(pr.x), s.y0 = std::round(pr.y), s.x1 = std::round(pr.x + pr.dx * s.pixels), s.y1 = std::round(pr.y + pr.dy * s.pixels);
      s.stage = PerfScript::Aim;
      return;
    }
    case PerfScript::Aim:
      if (!perf_check_aimed(why)) return perf_script_refuse(why);
      s.stage = PerfScript::Press;
      return;
    case PerfScript::Press:
      s.ms_press = ms;
      s.button_down = s.kind != PerfScript::Key || xf_.active;
      if (!perf_check_pressed(why)) return perf_script_refuse(why);
      perf_result_.press_x = (int)std::lround(s.x0), perf_result_.press_y = (int)std::lround(s.y0);
      s.stage = PerfScript::Move;
      return;
    case PerfScript::Move:
      s.acc_moves.add(prof_last_);
      s.moves_done++;
      s.prior_ran = autopose_.used_prior, s.prior_ms = autopose_.prior_ms;
      if (s.moves_done == 1) s.ms_first = ms;
      else s.ms_sum += ms, s.sum_frames++, s.ms_longest = std::max(s.ms_longest, ms);
      if (!s.loop && s.moves_done >= s.frames) s.stage = PerfScript::Release;
      return;
    case PerfScript::Release:
      s.ms_release = ms;
      s.acc_release.add(prof_last_);
      s.button_down = false;
      s.stage = PerfScript::After;
      return;
    case PerfScript::After:
      s.ms_after = ms;
      s.acc_release.add(prof_last_);
      perf_script_finish();
      return;
    case PerfScript::Abort:
    case PerfScript::Done:
      s.button_down = false, s.active = false, s.stage = PerfScript::Done;
      return;
  }
}

/* After the aim frame: is the asked handle under the mouse (the gizmo's own hit test, the Puppet control's hover)? */
bool Editor::perf_check_aimed(std::string &why) {
  const PerfScript &s = perf_;
  if (s.kind == PerfScript::Gizmo && gizmo_hot_ != s.handle) {
    why = "the gizmo's hit test doesn't hold the " + s.what.substr(6) + " handle at its own probed point (another handle is nearer)";
    return false;
  }
  if (s.kind == PerfScript::Puppet) {
    const int k = puppet_handle_index(s.what.substr(7));
    if (k < 0 || !puppet_.part[k] || puppet_.hover != puppet_.part[k]) {
      why = "the " + s.what.substr(7) + " control isn't under its own point (something is over it, or the view moved)";
      return false;
    }
  }
  /* The ruler: drawn in the frame that just ran, the mouse on it and nothing over it (a menu, a popup) - else the press
   * would be whatever else is at that point, and its release a click there. */
  if (s.kind == PerfScript::Ruler && (frames_ - anim_.drawn_at > 1 || !anim_.ruler_hot)) {
    why = "the ruler isn't under its own point (a menu is over it, or the Animation window closed)";
    return false;
  }
  return true;
}

/* After the press frame: did the drag start? A bone IK turns refuses a gizmo press (it says so itself); Esc and a
 * click elsewhere in the same frame would leave nothing running too. */
bool Editor::perf_check_pressed(std::string &why) {
  const PerfScript &s = perf_;
  if (s.kind == PerfScript::Gizmo && drag_ != Drag::Gizmo) {
    why = "the gizmo started no drag (a bone IK turns is refused: its target moves it instead)";
    return false;
  }
  if (s.kind == PerfScript::Puppet && !puppet_.dragging) {
    why = "the Puppet control started no drag";
    return false;
  }
  if (s.kind == PerfScript::Key && !xf_.active) {
    why = std::string("the key started no modal transform (nothing selected, or the mouse is not over the Scene view") +
          (s.key == KEY_R ? "; R is the Scale tool's key in the Unity keymap: `keymap Blender` makes it rotate)" : ")");
    return false;
  }
  if (s.kind == PerfScript::Ruler && anim_.drag != 1) {
    why = "the ruler took no press (the Animation window is covered or has no clip)";
    return false;
  }
  return true;
}

/* A refusal: says why, and lets go of what the script pressed (the next frame) - or ends it at once when nothing is held. */
void Editor::perf_script_refuse(const std::string &why) {
  PerfScript &s = perf_;
  Log::warn("perf drag %s: %s", s.what.c_str(), why.c_str());
  perf_result_.ok = false, perf_result_.error = why;
  gizmo_probe_.want = -1;
  if (s.button_down) s.stage = PerfScript::Abort;
  else s.active = false, s.stage = PerfScript::Done;
}

/* `perf drag stop` (and the windowed check's end): a looping drag releases where it is and reports; one not yet pressed
 * just ends. */
void Editor::perf_script_stop(const std::string &why) {
  PerfScript &s = perf_;
  if (!s.active) return;
  gizmo_probe_.want = -1;
  if (s.button_down && s.stage == PerfScript::Move) {  // (the next step releases; the result is logged after the frame that follows)
    s.stop_asked = true;
    return;
  }
  if (s.button_down) return;  // (already releasing)
  Log::info("perf drag %s: stopped (%s)", s.what.c_str(), why.c_str());
  s.active = false, s.stage = PerfScript::Done;
}

/* The script played to its end: its numbers kept for tests and the stress section, and logged in the lines
 * tools/bench_world.py parses. */
void Editor::perf_script_finish() {
  PerfScript &s = perf_;
  PerfDragResult &r = perf_result_;
  r.ok = true, r.error.clear(), r.what = s.what;
  r.frames = s.sum_frames > 0 ? s.sum_frames : 1;
  r.ms_press = s.ms_press, r.ms_first = s.ms_first, r.ms_release = s.ms_release, r.ms_after = s.ms_after;
  r.ms_mean = s.sum_frames > 0 ? s.ms_sum / s.sum_frames : s.ms_first;
  r.ms_longest = s.sum_frames > 0 ? s.ms_longest : s.ms_first;
  r.end_x = s.cx, r.end_y = s.cy;
  r.before = s.before_total, r.pose_before = s.pose_before, r.mesh_before = s.mesh_before;
  const FrameProfile moves = s.acc_moves;
  const PoseCounts pose = r.pose_after - r.pose_before;
  const MeshCounts mesh = r.mesh_after - r.mesh_before;
  std::string prior;
  if (s.kind == PerfScript::Puppet) prior = s.prior_ran ? strprintf("; the pose prior ran %.1f ms", s.prior_ms) : std::string("; the pose prior did not run");
  Log::info("perf drag %s: press %.1f ms, first move %.1f ms, %d drag frame(s) mean %.2f ms (longest %.1f ms), release %.1f ms, the frame after %.1f ms%s", s.what.c_str(), r.ms_press,
            r.ms_first, r.frames, r.ms_mean, r.ms_longest, r.ms_release, r.ms_after, prior.c_str());
  Log::info("perf drag parts ms (a drag frame's mean; nested, not to add up): %s", posing_parts_text(moves, pose, mesh, s.moves_done).c_str());
  Log::info("perf drag counts (a drag frame's mean): %s", posing_counts_text(moves, pose, mesh, s.moves_done).c_str());
  const FrameProfile &rel = s.acc_release;
  Log::info("perf drag release ms: undo step %.2f (settle %.2f, clone %.2f, assets %.2f), record %.2f", rel.ms_commit, rel.ms_commit_settle, rel.ms_commit_clone, rel.ms_commit_assets, rel.ms_record);
  s.active = false, s.stage = PerfScript::Done;
}

}  // namespace bl

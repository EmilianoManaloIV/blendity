// SPDX-License-Identifier: GPL-2.0-or-later
// Animation assistants (task 0124, ADR 0045): the tools that write animation for the animator - the physics tools
// (center of mass, contacts, flight, AutoPhysics, the ragdoll, AutoPosing, spring bones), Walk Cycle, Footsteps,
// Locomotion in Play and AI inbetweening - each switched on or off in Preferences > Animation Assistants. Off, a
// tool's buttons, panels and overlays are gone and its console command says how to turn it on; nothing of it runs.
// Hand animation (keys, curves, posing, IK and FK controls) is never one of them: it works the same either way.
#include "editor.h"

namespace bl {

namespace {
struct AssistInfo {
  const char *key;   // the console's and the preferences file's name
  const char *name;  // as Preferences shows it
  const char *help;
  bool default_on;
};
const AssistInfo kAssists[(int)Editor::Assist::Count] = {
    {"physics", "Physics Tools",
     "Center of Mass, Contacts, Flight, AutoPhysics, Ragdoll, AutoPosing and Spring Bones (Animation and Rigging windows).", true},
    {"walk", "Walk Cycle", "Walking and running cycles made from the rig (Animation window > Walk Cycle).", false},
    {"footsteps", "Footsteps", "A character walking along a path on footprints you can move (Walk Along Path).", false},
    {"locomotion", "Locomotion", "The Locomotion component walks characters in Play from the keys or a script.", false},
    {"ai", "AI Inbetweening", "Motion between two key poses from a trained network (an optional download).", false},
};
}  // namespace

bool Editor::assist_on(Assist a) const { return a < Assist::Count && assist_[(int)a]; }

/* With `a` off: false, and the log says how to turn it on (a command or button reached anyway). */
bool Editor::assist_require(Assist a) {
  if (assist_on(a)) return true;
  const AssistInfo &i = kAssists[(int)a];
  Log::warn("%s is switched off: Preferences > Animation Assistants, or the console's `assist on %s`", i.name, i.key);
  return false;
}


/* load_prefs: "assist_<key>=0|1". */
bool Editor::assist_pref(const std::string &k, const std::string &v) {
  if (k.rfind("assist_", 0) != 0) return false;
  for (int i = 0; i < (int)Assist::Count; i++)
    if (k.substr(7) == kAssists[i].key) assist_[i] = v == "1";
  Armature::show_body_mass = assist_[(int)Assist::Physics];
  Locomotion::live = assist_[(int)Assist::Locomotion];
  return true;
}

std::string Editor::assist_prefs_text() const {
  std::string s;
  for (int i = 0; i < (int)Assist::Count; i++) s += strprintf("assist_%s=%d\n", kAssists[i].key, assist_[i] ? 1 : 0);
  return s;
}

/* Preferences > Animation Assistants: a switch each. */
void Editor::draw_assist_settings() {
  auto &u = ui_;
  Recti r = u.popup_row(u.row_h() + u.px(4));
  u.label({r.x + u.px(12), r.y, u.px(160), r.h}, "Animation Assistants");
  u.tooltip("Tools that write animation for you. Each is optional: hand animation (keys, curves, posing) works the same with every one off.");
  for (int i = 0; i < (int)Assist::Count; i++) {
    Recti row = i == 0 ? r : u.popup_row(u.row_h() + u.px(2));
    const Recti box{row.x + u.px(180), row.y + u.px(2), row.h - u.px(4), row.h - u.px(4)};
    bool on = assist_[i];
    if (u.checkbox(box, on)) assist_set((Assist)i, on);
    u.label({box.right() + u.px(6), row.y, u.px(160), row.h}, kAssists[i].name);
    u.tooltip(kAssists[i].help);
  }
}

void Editor::assist_set(Assist a, bool on) {
  if (a >= Assist::Count || assist_[(int)a] == on) return;
  assist_[(int)a] = on;
  Armature::show_body_mass = assist_[(int)Assist::Physics];
  Locomotion::live = assist_[(int)Assist::Locomotion];
  if (a == Assist::Locomotion && on && playing_) play_ground_ = locomotion::ground_for(*scene_);  // (switched on in Play)
  if (!on && a == Assist::Physics) {  // (what it showed goes with it)
    body_.ghost = false, body_.ghost_bones.clear(), body_.show_com = false;
    body_.solve.reset();
    autopose_.on = false;
  }
  ui_.redraw = true;
  save_prefs();
  Log::info("%s %s", kAssists[(int)a].name, on ? "on" : "off");
}

/* assist list | on <name> | off <name> */
void Editor::assist_command(const std::vector<std::string> &t) {
  const std::string sub = t.size() > 1 ? to_lower(t[1]) : "list";
  if ((sub == "on" || sub == "off") && t.size() > 2) {
    for (int i = 0; i < (int)Assist::Count; i++)
      if (to_lower(t[2]) == kAssists[i].key) return assist_set((Assist)i, sub == "on");
    return Log::warn("assist %s <name>: no assistant '%s' (assist list)", sub.c_str(), t[2].c_str());
  }
  for (int i = 0; i < (int)Assist::Count; i++) Log::info("  %-10s %-3s %s", kAssists[i].key, assist_[i] ? "on" : "off", kAssists[i].name);
  if (sub != "list") Log::warn("assist list | on <name> | off <name>");
}

}  // namespace bl

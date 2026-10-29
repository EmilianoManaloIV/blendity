// SPDX-License-Identifier: GPL-2.0-or-later
// Walk Cycle in the editor (task 0125): the Rigging window's Walk Cycle section (a preset, its numbers, Generate,
// presets saved as .gait files), the Animation window's Walk Cycle button and the console's `walk`. An assistant: off
// unless switched on (Preferences > Animation Assistants), and what it makes is ordinary keys.
#include "editor.h"
#include "../scene/gait.h"

namespace bl {

namespace {
const char *const kHumanPresets[] = {"walk", "jog", "run", "sneak", "limp", "custom"};
const char *const kAnimalPresets[] = {"walk", "trot", "gallop", "custom"};  // ("custom": a loaded, blended or other rig's style)

/* The style's numbers, as the Inspector-style rows draw them. */
void reflect_style(Reflector &r, gait::Style &s) {
  r.field("Speed", s.speed, 0.05f, 0.05f, 20.0f);
  r.help("Metres a second for a 0.9 m leg; a bigger or smaller character walks at the speed that looks the same (the Froude number).");
  r.field("Stride", s.stride, 0.01f, 0.3f, 1.6f);
  r.help("x the natural stride at that speed: longer, slower steps above 1.");
  r.field("Feet Down", s.duty, 0.01f, 0.1f, 0.95f);
  r.help("The share of a cycle each foot is on the ground: over 0.5 walking, under 0.5 running (both feet off the ground between).");
  r.field("Foot Lift", s.lift, 0.005f, 0.0f, 1.0f);
  r.field("Heel-Toe Roll", s.roll, 0.5f, 0.0f, 60.0f);
  r.field("Bounce", s.bounce, 0.002f, 0.0f, 0.3f);
  r.field("Sway", s.sway, 0.002f, 0.0f, 0.3f);
  r.field("Hip Twist", s.twist, 0.5f, 0.0f, 45.0f);
  r.field("Hip Drop", s.tilt, 0.5f, 0.0f, 30.0f);
  r.field("Arm Swing", s.arm_swing, 0.5f, 0.0f, 90.0f);
  r.field("Elbows", s.elbow, 0.5f, 0.0f, 150.0f);
  r.field("Lean", s.lean, 0.5f, -30.0f, 45.0f);
  r.field("Crouch", s.crouch, 0.005f, 0.0f, 0.5f);
}
}  // namespace

/* The animated character's rig, read for walking (and the preset list it takes). */
gait::Rig Editor::walk_rig() {
  GameObject *root = anim_root();
  GameObject *arm = root ? const_cast<GameObject *>(body::find_armature(*root)) : nullptr;
  if (!arm) arm = rig_root();
  gait::Rig r;
  if (!arm) return r.problem = "select a character with a skeleton", r;
  return gait::analyse(*scene_, *arm);
}

/* A preset's numbers into the settings (keeping cycles and in-place). */
void Editor::walk_preset(const std::string &name) {
  const gait::Rig r = walk_rig();
  gait::Style s;
  if (!gait::preset(name, r, s)) return Log::warn("walk: no preset '%s' for %s", name.c_str(), r.quadruped ? "four legs" : "two legs");
  walk_.settings.style = s;
}

/* Generate: the gait keyed into the character's clip (a new one when it has none), one undo step. */
void Editor::walk_generate() {
  if (!assist_require(Assist::Walk)) return;
  if (playing_) return refuse("Walk Cycle: stop Play first", "stop", "Stop Play");
  GameObject *root = anim_root();
  const gait::Rig r = walk_rig();
  if (!root || !r.valid()) return Log::warn("Walk Cycle: %s", root ? r.problem.c_str() : "select a character with a skeleton");
  /* Made on a copy first: a failure leaves no clip behind and nothing changed. */
  AnimationClipPtr clip = anim_clip();
  const AnimationClip base = clip ? *clip : AnimationClip();
  AnimationClip out;
  std::string error;
  walk_.settings.fps = clip && clip->frame_rate > 0.0f ? clip->frame_rate : 30.0f;
  const int frames = gait::generate(*scene_, *root, r, walk_.settings, base, out, error);
  if (!frames) return Log::warn("Walk Cycle: %s", error.c_str());
  if (!clip) clip = anim_new_clip(root, root->name + " " + walk_.settings.style.name);
  if (!clip) return;
  anim_edit_keys("Walk Cycle", [&](AnimationClip &c) {
    c.tracks = out.tracks, c.contacts = out.contacts, c.length = std::max(c.length, out.length), c.frame_rate = out.frame_rate;
  });
  const gait::Cycle cy = gait::cycle_of(r, walk_.settings.style);
  Log::info("Walk Cycle: %s, %d frames at %.0f fps (%d cycle%s, about %.2f m a cycle at %.2f m/s)%s - ordinary keys: edit them as any",
            walk_.settings.style.name.c_str(), frames, walk_.settings.fps, walk_.settings.cycles, walk_.settings.cycles == 1 ? "" : "s", cy.length,
            cy.speed, walk_.settings.in_place ? ", in place" : "");
}

/* The Rigging window's Walk Cycle section. */
void Editor::draw_walk_section(ui::Layout &lay) {
  auto &u = ui_;
  const gait::Rig r = walk_rig();
  if (!r.valid()) {
    u.label(lay.row(), "Walk Cycle: " + r.problem, u.theme.text_dim);
    return;
  }
  const char *const *names = r.quadruped ? kAnimalPresets : kHumanPresets;
  const int count = r.quadruped ? 4 : 6;
  int pick = count - 1;  // ("custom" unless the style is one of this rig's presets)
  for (int i = 0; i + 1 < count; i++)
    if (walk_.settings.style.name == names[i]) pick = i;
  const int before = pick;
  gait::WalkSettings &ws = walk_.settings;
  const bool changed = draw_reflected([&](Reflector &rf) {
    rf.enumeration("Preset", pick, names, count);
    rf.help("A starting point; every number below can be changed. Two legs: walk, jog, run, sneak, limp. Four legs: walk, trot, gallop.");
    rf.field("Cycles", ws.cycles, 1, 64);
    rf.field("In Place", ws.in_place);
    rf.help("On: a treadmill loop (games play it while the character moves). Off: the body travels forward through the clip.");
    reflect_style(rf, ws.style);
  }, lay);
  if (changed && pick != before && pick + 1 < count) walk_preset(names[pick]);
  Recti row = lay.row(u.row_h() + u.px(2));
  if (u.button({row.x + u.px(4), row.y, row.w - u.px(8), row.h}, "Generate into the Clip")) walk_generate();
  u.tooltip("Keys the hips, the feet's IK targets, the feet, spine, head and arms for the whole cycle (and the planted frames as contacts):\n"
            "ordinary keys, one undo step. Console: walk cycle [preset] [speed] [cycles] [inplace|travel].");
}

/* walk cycle [preset] [speed] [cycles] [inplace|travel] | set <number> <value> | save <file> | load <file> | presets */
void Editor::walk_command(const std::vector<std::string> &t) {
  const std::string sub = t.size() > 1 ? to_lower(t[1]) : "";
  if (sub == "path" || sub == "steps") return steps_command(t);  // (footsteps: their own switch)
  if (!assist_require(Assist::Walk)) return;
  if (sub == "cycle") {
    if (t.size() > 2) walk_preset(to_lower(t[2]));
    if (t.size() > 3) walk_.settings.style.speed = std::max(0.05f, (float)std::atof(t[3].c_str()));
    if (t.size() > 4) walk_.settings.cycles = std::max(1, std::min(64, std::atoi(t[4].c_str())));
    if (t.size() > 5) walk_.settings.in_place = to_lower(t[5]) != "travel";
    return walk_generate();
  }
  if (sub == "set" && t.size() > 3) {
    gait::Style s = walk_.settings.style;
    if (!gait::style_from_text(t[2] + "=" + t[3], s, true)) return Log::warn("walk set <number> <value>: speed, stride, duty, lift, roll, bounce, ...");
    walk_.settings.style = s;  // (onto the style as it is: only that number changes)
    return;
  }
  if ((sub == "save" || sub == "load") && t.size() > 2) {
    std::string path = t[2];
    if (fs::extension(path) != ".gait") path += ".gait";
    const bool absolute = path.size() > 1 && (path[0] == '/' || path[0] == '\\' || path[1] == ':');
    if (path.find("..") != std::string::npos) return Log::warn("walk %s: a path inside Assets, or a full path", sub.c_str());
    if (!absolute) path = fs::join(assets_dir_, path);  // (relative: in the project's Assets)
    if (sub == "save") {
      if (!fs::write_file(path, gait::style_text(walk_.settings.style))) return Log::warn("walk save: can't write %s", path.c_str());
      return Log::info("Walk style saved: %s", path.c_str());
    }
    std::string text;
    gait::Style s;
    if (!fs::read_file(path, text) || !gait::style_from_text(text, s)) return Log::warn("walk load: %s isn't a .gait style", path.c_str());
    walk_.settings.style = s;
    return Log::info("Walk style loaded: %s", s.name.c_str());
  }
  const gait::Rig r = walk_rig();
  std::string list;
  for (const std::string &n : gait::preset_names(r.valid() && r.quadruped)) list += n + " ";
  Log::info("Walk presets for this rig: %s(now: %s)", list.c_str(), walk_.settings.style.name.c_str());
  if (sub != "presets") Log::warn("walk cycle [preset] [speed] [cycles] [inplace|travel] | set <number> <value> | save <file> | load <file> | presets");
}

}  // namespace bl

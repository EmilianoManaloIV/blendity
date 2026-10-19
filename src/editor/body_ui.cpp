// SPDX-License-Identifier: GPL-2.0-or-later
// Physics for animation in the editor (tasks 0115-0122): the body's center of mass over the clip, drawn in the Scene
// view; the console's `body`.
#include "editor.h"
#include <cstring>

namespace bl {

namespace {
/* What the trajectory depends on: the animated object, its clip as it is, and the body (body::body_key: the bones'
 * rests, the mass, the skinned meshes and where they stand - by value, so posing and putting back doesn't count). */
uint64_t path_key(const Scene &scene, const GameObject &root, const GameObject &arm, const AnimationClip &clip) {
  uint64_t h = body::body_key(scene, arm);
  auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
  mix(root.id);
  mix((uint64_t)(uintptr_t)&clip);
  mix(clip.version);
  return h;
}
/* The flight's ghost: the corrected body at the playhead, in the frame's colour. */
void draw_ghost(Renderer3D &r3d, const std::vector<std::pair<Vec3, Vec3>> &bones, uint32_t col) {
  for (const auto &b : bones) r3d.line(b.first, b.second, col, false);
}
}  // namespace

/* The animated object's body and its center of mass at every frame of its clip, when something they depend on
 * changed. False when there is no armature or clip to show. */
bool Editor::body_refresh() {
  GameObject *root = anim_root();
  AnimationClipPtr clip = anim_clip();
  const GameObject *arm = root ? body::find_armature(*root) : nullptr;
  if (!root || !clip || !arm) {
    body_.path.clear();
    body_.body = body::Body();
    return false;
  }
  const uint64_t key = path_key(*scene_, *root, *arm, *clip);
  if (key == body_.key && body_.body.valid()) return true;
  body_.key = key;
  body_.body = body::build(*scene_, *arm, arm->get<Armature>()->body_mass);
  body_.builds++;
  body_.first = 0.0f;
  body_.last = std::max(1.0f, clip->end_frame());
  const int frames = std::max(2, std::min(2000, (int)std::lround(body_.last - body_.first) + 1));
  body_.path = body::com_trajectory(*scene_, *root, *clip, body_.body, body_.first, body_.last, frames);
  return body_.body.valid();
}

bool Editor::flight_current(const AnimationClip *clip) {
  const GameObject *root = anim_root();
  return body_.ghost && clip && root && body_.flight_clip.lock().get() == clip && body_.flight_root == root->id &&
         body_.flight_clip_version == clip->version;
}

/* The center of mass: its path over the clip, where it is now (a ball) and straight below it on the ground. */
void Editor::draw_body_overlay() {
  if (!assist_on(Assist::Physics)) return;
  AnimationClipPtr clip = anim_clip();
  if (flight_current(clip.get()) && !body_.ghost_bones.empty()) {
    const size_t f = (size_t)std::max(0, std::min((int)body_.ghost_bones.size() - 1, (int)std::lround(anim_.frame)));
    const uint8_t m = f < body_.flight.marks.size() ? body_.flight.marks[f] : 0;
    draw_ghost(scene_r3d_, body_.ghost_bones[f], m == body::MarkImpossible ? Color::hex(0xE04848, 230) : Color::hex(0x3C8CE6, 230));
  }
  if (!body_.show_com || !body_refresh() || body_.path.size() < 2) return;
  const uint32_t path_col = Color::hex(0xFFB020, 220), now_col = Color::hex(0xFF3030, 255), ground_col = Color::hex(0xFF3030, 140);
  for (size_t i = 0; i + 1 < body_.path.size(); i++) scene_r3d_.line(body_.path[i], body_.path[i + 1], path_col, false);
  std::vector<ArmatureBone> now;
  armature_bones(*body_.body.root, now);
  const Vec3 c = body::state(body_.body, now).com;
  scene_r3d_.point(c, (float)ui_.px(6), now_col, false);
  const Vec3 g(c.x, 0.0f, c.z);
  scene_r3d_.line(c, g, ground_col, false);
  scene_r3d_.point(g, (float)ui_.px(3), ground_col, false);
}


/* Under the ruler: each contact as a bar (one row per bone), the flight's frames in Cascadeur's colours. */
void Editor::draw_body_strip(const Recti &ruler, float end) {
  if (!assist_on(Assist::Physics)) return;
  AnimationClipPtr clip = anim_clip();
  if (!clip) return;
  auto &u = ui_;
  const int bar = std::max(2, u.px(3));
  std::vector<std::string> bones;
  for (const AnimationClip::Contact &k : clip->contacts) {
    if (std::find(bones.begin(), bones.end(), k.bone) == bones.end()) bones.push_back(k.bone);
    const int row = (int)(std::find(bones.begin(), bones.end(), k.bone) - bones.begin());
    const int x0 = anim_x_of(ruler, k.from, end), x1 = anim_x_of(ruler, k.to + 1.0f, end), y = ruler.bottom() - bar * (row + 1);
    if (y < ruler.y) break;
    u.canvas.fill_rect({x0, y, std::max(1, x1 - x0), bar - 1}, k.manual ? Color::hex(0xE08A2E) : Color::hex(0x9C6A3A));
  }
  if (!flight_current(clip.get())) return;
  static const uint32_t kMark[4] = {0, 0x3CB46E, 0x3C8CE6, 0xE04848};  // ground, fine, changed, impossible
  for (size_t f = 0; f < body_.flight.marks.size(); f++) {
    const uint8_t m = body_.flight.marks[f];
    if (!m) continue;
    const int x0 = anim_x_of(ruler, (float)f, end), x1 = anim_x_of(ruler, (float)f + 1.0f, end);
    u.canvas.fill_rect({x0, ruler.y, std::max(1, x1 - x0), bar}, Color::hex(kMark[m]));
  }
}

/* body contacts find | pin | clear | list | add <bone> <from> <to> | remove <bone> <frame> (task 0116). */
void Editor::body_contacts_command(const std::vector<std::string> &t) {
  GameObject *root = anim_root();
  AnimationClipPtr clip = anim_clip();
  GameObject *arm = root ? const_cast<GameObject *>(body::find_armature(*root)) : nullptr;
  if (!root || !clip || !arm) return Log::warn("body contacts: select an animated character with a clip");
  const std::string sub = t.size() > 2 ? to_lower(t[2]) : "list";
  if (playing_ && sub != "list") return Log::warn("body contacts: stop Play first (a clip isn't edited in Play)");
  const std::vector<body::Effector> eff = body::effectors(*scene_, *root, *arm);
  if (sub == "find") {
    const std::vector<AnimationClip::Contact> found = body::find_contacts(*scene_, *root, *clip, eff, body::ContactSettings());
    anim_edit_keys("Find Contacts", [&](AnimationClip &c) { c.contacts = found; });
    return Log::info("Contacts: %zu over %zu ends (feet and hands with IK)%s", found.size(), eff.size(),
                     eff.empty() ? ": none has IK (Rigging > Add Humanoid IK)" : "");
  }
  if (sub == "pin") {
    size_t keys = 0, skipped = 0, replaced = 0;
    anim_edit_keys("Pin Contacts", [&](AnimationClip &c) { keys = body::pin_contacts(*scene_, *root, c, eff, &skipped, &replaced); });
    return Log::info("Contacts pinned: %zu keys, %zu of them changed keys the clip had%s", keys, replaced,
                     skipped ? strprintf(" (%zu contact(s) without an IK target in the clip, or outside it, left)", skipped).c_str() : "");
  }
  if (sub == "clear") {
    anim_edit_keys("Clear Contacts", [](AnimationClip &c) { c.contacts.clear(); });
    return Log::info("Contacts cleared");
  }
  if (sub == "add" && t.size() > 5) {
    const AnimationClip::Contact k{t[3], (float)std::atof(t[4].c_str()), (float)std::atof(t[5].c_str()), true};
    if (!(k.to >= k.from)) return Log::warn("body contacts add <bone> <from> <to>: from before to");
    anim_edit_keys("Add Contact", [&](AnimationClip &c) { c.contacts.push_back(k); });
    return Log::info("Contact: %s %.0f-%.0f", k.bone.c_str(), k.from, k.to);
  }
  if (sub == "remove" && t.size() > 4) {
    const float f = (float)std::atof(t[4].c_str());
    anim_edit_keys("Remove Contact", [&](AnimationClip &c) {
      c.contacts.erase(std::remove_if(c.contacts.begin(), c.contacts.end(), [&](const AnimationClip::Contact &k) { return k.bone == t[3] && k.from <= f && k.to >= f; }),
                       c.contacts.end());
    });
    return;
  }
  for (const AnimationClip::Contact &k : clip->contacts) Log::info("  %s %.0f-%.0f%s", k.bone.c_str(), k.from, k.to, k.manual ? " (by hand)" : "");
  if (sub != "list") Log::warn("body contacts find | pin | clear | list | add <bone> <from> <to> | remove <bone> <frame>");
}

/* body flight show | apply | off (task 0117): the corrected flight as a ghost and frame colours, then into the clip. */
void Editor::body_flight_command(const std::vector<std::string> &t) {
  const std::string sub = t.size() > 2 ? to_lower(t[2]) : "show";
  if (sub != "apply") body_.solve.reset();  // (an AutoPhysics solve under way would replace what this shows)
  if (sub == "off") {
    body_.ghost = false;
    body_.ghost_bones.clear();
    return;
  }
  GameObject *root = anim_root();
  AnimationClipPtr clip = anim_clip();
  if (!root || !clip || !body_refresh()) return Log::warn("body flight: select an animated character with a clip");
  if (sub == "apply") {
    if (playing_) return Log::warn("body flight: stop Play first (a clip isn't edited in Play)");
    if (!flight_current(clip.get())) return Log::warn("body flight: show it first (for this character's clip, as it is now)");
    if (!body_.flight.keys) return Log::warn("%s: nothing to apply (impossible: red frames)", body_.kind.c_str());
    const AnimationClip corrected = body_.flight.corrected;
    const std::string label = "Apply " + body_.kind;
    anim_edit_keys(label.c_str(), [&](AnimationClip &c) { c.tracks = corrected.tracks; });
    body_.ghost = false;
    body_.ghost_bones.clear();
    return Log::info("%s applied: %zu keys (%zu of them changed keys the clip had)", body_.kind.c_str(), body_.flight.keys, body_.flight.replaced);
  }
  std::string error;
  if (!body::correct_flight(*scene_, *root, *clip, body_.body, body::FlightSettings(), body_.flight, error)) {
    body_.ghost = false;
    return Log::warn("Flight: %s", error.c_str());
  }
  body_show_result(*root, clip, "Flight");
  size_t red = 0, blue = 0;
  for (uint8_t m : body_.flight.marks) red += m == body::MarkImpossible, blue += m == body::MarkChanged;
  for (const body::Flight &fl : body_.flight.flights)
    Log::info("Flight %d-%d: take-off %.2f m/s, moved up to %.3f m and %.1f degrees%s%s", fl.takeoff, fl.landing, length(fl.v0), fl.shift, fl.turn,
              fl.possible ? "" : ": impossible - ", fl.why.c_str());
  Log::info("Flight: %zu frame(s) changed (blue), %zu impossible (red); Apply writes it", blue, red);
}

/* A correction worked out (body_.flight) shown: for this clip as it is now, its corrected bones at every frame once (the
 * ghost then costs nothing a frame). */
void Editor::body_show_result(GameObject &root, const AnimationClipPtr &clip, const char *kind) {
  body_.ghost = true;
  body_.kind = kind;
  body_.flight_clip_version = clip->version;
  body_.flight_clip = clip;
  body_.flight_root = root.id;
  body_.ghost_bones.clear();
  const int last = (int)body_.flight.marks.size() - 1;
  std::vector<ArmatureBone> bones;
  body::for_each_frame(*scene_, root, body_.flight.corrected, 0, last, [&](int) {
    armature_bones(*body_.body.root, bones);
    std::vector<std::pair<Vec3, Vec3>> segs;
    for (const ArmatureBone &b : bones) segs.push_back({b.object->world_position(), body::bone_tip(*b.object)});
    body_.ghost_bones.push_back(segs);
  });
}

/* body physics show | apply | off (task 0119): the clip sampled now, solved on a worker, shown when it's done. */
void Editor::body_physics_command(const std::vector<std::string> &t) {
  const std::string sub = t.size() > 2 ? to_lower(t[2]) : "show";
  if (sub == "apply") return body_flight_command({"body", "flight", "apply"});
  body_.solve.reset();  // (one under way is stopped: a new one, or none)
  if (sub == "off") {
    body_.ghost = false;
    body_.ghost_bones.clear();
    return;
  }
  GameObject *root = anim_root();
  AnimationClipPtr clip = anim_clip();
  if (!root || !clip || !body_refresh()) return Log::warn("body physics: select an animated character with a clip");
  if (playing_) return Log::warn("body physics: stop Play first (a clip isn't solved while it plays)");
  std::string error;
  std::shared_ptr<body::AutoPhysicsPrep> prep = body::autophysics_prepare(*scene_, *root, *clip, body_.body, body::AutoPhysicsSettings(), error);
  if (!prep) return Log::warn("AutoPhysics: %s", error.c_str());
  const int frames = (int)body::autophysics_input(*prep).com.size();
  body_start("AutoPhysics", clip, *root, [prep](BodyView::Solve &s, body::FlightResult &out, std::string &e) {
    body::autophysics_input(*prep).cancel = &s.cancel;
    const body::CentroidalResult r = body::solve_centroidal(body::autophysics_input(*prep));
    s.ms = r.ms;
    return body::autophysics_finish(*prep, r, out, e);  // (no scene objects: the sampled clip and the solve only)
  });
  Log::info("AutoPhysics: solving %d frames...", frames);
}

/* A correction worked out on a worker (it reads only what `work` captured); body_poll shows it when it's done. */
void Editor::body_start(const char *kind, const AnimationClipPtr &clip, GameObject &root,
                        const std::function<bool(BodyView::Solve &, body::FlightResult &, std::string &)> &work) {
  auto s = std::make_unique<BodyView::Solve>();
  s->kind = kind;
  s->clip = clip, s->clip_version = clip->version, s->root = root.id;
  BodyView::Solve *sp = s.get();  // (the solve outlives its thread: its destructor joins it)
  s->thread = std::thread([sp, work] {
    const auto t0 = std::chrono::steady_clock::now();
    sp->ok = work(*sp, sp->result, sp->error);
    if (sp->ms <= 0.0) sp->ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    sp->done = true;
  });
  body_.solve = std::move(s);
  body_.ghost = false;
}

/* body ragdoll show [strength] | apply | off | hit <bone> <frame> <x> <y> <z> | limp <frame> | strong <frame> | events |
 * clear (task 0121): the clip played by the active ragdoll, on a worker; its events kept in the clip. */
void Editor::body_ragdoll_command(const std::vector<std::string> &t) {
  const std::string sub = t.size() > 2 ? to_lower(t[2]) : "show";
  if (sub == "apply") return body_flight_command({"body", "flight", "apply"});
  if (sub == "off") return body_physics_command({"body", "physics", "off"});
  GameObject *root = anim_root();
  AnimationClipPtr clip = anim_clip();
  if (!root || !clip || !body_refresh()) return Log::warn("body ragdoll: select an animated character with a clip");
  if (playing_) return Log::warn("body ragdoll: stop Play first");
  using E = AnimationClip::RagdollEvent;
  if (sub == "hit" && t.size() > 6) {
    E e;
    e.kind = E::Impulse, e.bone = t[3], e.frame = (float)std::atof(t[4].c_str());
    e.impulse = Vec3((float)std::atof(t[5].c_str()), (float)std::atof(t[6].c_str()), t.size() > 7 ? (float)std::atof(t[7].c_str()) : 0.0f);
    if (!scene_->find_by_name(e.bone)) return Log::warn("body ragdoll hit: no bone named '%s'", e.bone.c_str());
    anim_edit_keys("Ragdoll Hit", [&](AnimationClip &c) { c.ragdoll.push_back(e); });
    return Log::info("Ragdoll: %s hit at frame %.0f with (%.0f, %.0f, %.0f) N s", e.bone.c_str(), e.frame, e.impulse.x, e.impulse.y, e.impulse.z);
  }
  if ((sub == "limp" || sub == "strong") && t.size() > 3) {
    E e;
    e.kind = sub == "limp" ? E::MotorsOff : E::MotorsOn, e.frame = (float)std::atof(t[3].c_str());
    anim_edit_keys(sub == "limp" ? "Ragdoll Limp" : "Ragdoll Strong", [&](AnimationClip &c) { c.ragdoll.push_back(e); });
    return Log::info("Ragdoll: the motors %s from frame %.0f", sub == "limp" ? "off" : "on", e.frame);
  }
  if (sub == "clear") {
    anim_edit_keys("Clear Ragdoll Events", [](AnimationClip &c) { c.ragdoll.clear(); });
    return Log::info("Ragdoll events cleared");
  }
  if (sub == "events") {
    for (const E &e : clip->ragdoll)
      Log::info("  frame %.0f: %s", e.frame, e.kind == E::Impulse ? strprintf("hit %s (%.0f, %.0f, %.0f)", e.bone.c_str(), e.impulse.x, e.impulse.y, e.impulse.z).c_str()
                                                       : e.kind == E::MotorsOff ? "limp (motors off)" : "strong (motors on)");
    return;
  }
  if (sub != "show") return Log::warn("body ragdoll show [strength] | apply | off | hit <bone> <frame> <x> <y> <z> | limp <frame> | strong <frame> | events | clear");
  if (!ragdoll::available()) return Log::warn("Ragdoll: needs Blender's libraries (Jolt Physics); this build has none");
  ragdoll::Settings rs;
  if (t.size() > 3) {
    char *end = nullptr;
    const float v = std::strtof(t[3].c_str(), &end);
    if (end == t[3].c_str() || !std::isfinite(v)) return Log::warn("body ragdoll show [strength 0..1]: '%s' isn't a number", t[3].c_str());
    rs.strength = std::max(0.0f, std::min(1.0f, v));
  }
  body_.solve.reset();
  std::string error;
  std::shared_ptr<ragdoll::Prep> prep = ragdoll::prepare(*scene_, *root, *clip, body_.body, rs, error);
  if (!prep) return Log::warn("Ragdoll: %s", error.c_str());
  for (const std::string &b : ragdoll::unmatched_events(*prep))
    Log::warn("Ragdoll: the hit on %s does nothing (it isn't one of the body's weighed bones)", b.c_str());
  body_start("Ragdoll", clip, *root, [prep](BodyView::Solve &s, body::FlightResult &out, std::string &e) {
    return ragdoll::run(*prep, out, e, nullptr, &s.cancel);
  });
  Log::info("Ragdoll: simulating at strength %.2f...", rs.strength);
}

/* Each frame: a finished solve keyed and shown, when it is still for the clip on screen as it is. */
void Editor::body_poll() {
  if (!body_.solve || !body_.solve->done.load() || playing_) return;  // (kept till Play ends: showing it poses the scene)
  std::unique_ptr<BodyView::Solve> s = std::move(body_.solve);
  s->thread.join();
  GameObject *root = anim_root();
  AnimationClipPtr clip = anim_clip();
  if (!root || !clip || clip != s->clip.lock() || clip->version != s->clip_version || root->id != s->root)
    return Log::info("%s: the clip changed while it was worked out; run it again", s->kind.c_str());
  if (!s->ok) return Log::warn("%s: %s", s->kind.c_str(), s->error.c_str());
  body_.flight = std::move(s->result);
  body_show_result(*root, clip, s->kind.c_str());
  const body::Flight &fl = body_.flight.flights.front();
  size_t red = 0, blue = 0;
  for (uint8_t m : body_.flight.marks) red += m == body::MarkImpossible, blue += m == body::MarkChanged;
  Log::info("%s: %d frames in %.0f ms; the body moved up to %.3f m and %.1f degrees, %zu frame(s) changed (blue), %zu impossible (red)%s%s",
            s->kind.c_str(), (int)body_.flight.marks.size(), s->ms, fl.shift, fl.turn, blue, red, fl.possible ? "; Apply writes it" : ": ", fl.why.c_str());
}

/* body show on|off | mass [kg] | com [frame] | contacts ... | flight ...: the console's physics for animation. */
void Editor::body_command(const std::vector<std::string> &t) {
  if (!assist_require(Assist::Physics)) return;
  const std::string sub = t.size() > 1 ? to_lower(t[1]) : "mass";
  if (sub == "contacts") return body_contacts_command(t);
  if (sub == "flight") return body_flight_command(t);
  if (sub == "physics") return body_physics_command(t);
  if (sub == "ragdoll") return body_ragdoll_command(t);
  if (sub == "show") {
    body_.show_com = t.size() < 3 || to_lower(t[2]) != "off";
    ui_.redraw = true;
    return Log::info("Center of mass %s", body_.show_com ? "shown" : "hidden");
  }
  GameObject *root = anim_root();
  GameObject *arm = root ? const_cast<GameObject *>(body::find_armature(*root)) : nullptr;
  if (!arm) return Log::warn("body: select an animated character (an object with an Armature, or above one)");
  if (sub == "mass") {
    if (t.size() > 2) {
      arm->get<Armature>()->body_mass = std::max(0.01f, (float)std::atof(t[2].c_str()));
      mark_changed("Body Mass");
    }
    const body::Body b = body::build(*scene_, *arm, arm->get<Armature>()->body_mass);
    Log::info("Body: %.1f kg over %zu bones%s", b.mass, b.bones.size(), b.from_capsules ? " (no skinned mesh: capsules round the bones)" : "");
    std::vector<const body::BoneMass *> order;
    for (const body::BoneMass &bm : b.bones)
      if (bm.mass > 0.0f) order.push_back(&bm);
    std::sort(order.begin(), order.end(), [](const body::BoneMass *a, const body::BoneMass *c) { return a->mass > c->mass; });
    for (size_t i = 0; i < order.size() && i < 8; i++) Log::info("  %-24s %6.2f kg", order[i]->name.c_str(), order[i]->mass);
    return;
  }
  if (sub == "com") {
    if (!body_refresh()) return Log::warn("body: the character has no clip (Animation window: Create New Clip)");
    const float f = t.size() > 2 ? (float)std::atof(t[2].c_str()) : anim_.frame;
    const float span = std::max(1e-6f, body_.last - body_.first), u = (f - body_.first) / span;  // (the path's samples spread over the clip)
    const size_t i = (size_t)std::max(0, std::min((int)body_.path.size() - 1, (int)std::lround(u * (float)(body_.path.size() - 1))));
    const Vec3 c = body_.path[i];
    return Log::info("Center of mass at frame %.0f: (%.3f, %.3f, %.3f)", f, c.x, c.y, c.z);
  }
  Log::warn("body show on|off | mass [kg] | com [frame] | contacts ... | flight show|apply|off | physics show|apply|off");
}

}  // namespace bl

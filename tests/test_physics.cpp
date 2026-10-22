// SPDX-License-Identifier: GPL-2.0-or-later
// blendity_tests: the physics tests (task 0177: one file a subsystem; the groups keep their rounds' order).
#include "test_common.h"

static void physics_tests() {
  test("physics: Jolt drops a box onto a plane and it comes to rest", [] {
    if (!physics_jolt_available()) return;
    Scene s;
    GameObject *ground = s.create("Ground");
    ground->add<MeshFilter>()->mesh = primitives::plane(10.0f, 1);
    ground->add<MeshRenderer>();
    GameObject *box = s.create("Box");
    box->add<MeshFilter>()->mesh = primitives::cube();
    box->add<MeshRenderer>();
    box->add<Rigidbody>()->bounciness = 0.0f;
    box->set_local_position({0, 3, 0});
    PlayContext ctx;
    s.start(ctx);
    CHECK(s.physics != nullptr);
    ctx.dt = 1.0f / 60.0f;
    float lowest = 3;
    for (int i = 0; i < 240; i++) {  // 4 s
      s.update(ctx);
      lowest = std::min(lowest, box->world_position().y);
    }
    CHECK_NEAR(box->world_position().y, 0.5f, 0.05f);  // a 1 m cube resting on y = 0
    CHECK(lowest > 0.3f);                              // never fell through
    CHECK(length(box->get<Rigidbody>()->velocity) < 0.05f);
  });
}

static void tests_round137_to_round140() {
  /* ---------------------------------------------------------------- round 137: the body's masses and center of mass (task 0115) */
  /* An armature "Rig": Lower (0..1 up +Y) and Upper (1..2), skinned to one mesh of two closed boxes: a 1 m cube centred at
   * (0, 0.5, 0) all Lower's, a 1 x 1 x 2 box centred at (0, 1.5, 0) all Upper's (twice the volume). */
  struct R136Body {
    Scene s;
    GameObject *rig = nullptr, *lower = nullptr, *upper = nullptr, *skin = nullptr;
    R136Body(bool skinned = true) {
      rig = s.create("Rig");
      rig->add<Armature>();
      lower = s.create("Lower", rig);
      lower->add<Bone>();
      upper = s.create("Upper", lower);
      upper->set_local_position({0, 1, 0});
      upper->add<Bone>()->rest_position = {0, 1, 0};
      if (!skinned) return;
      MeshPtr m = std::make_shared<Mesh>();
      VertexGroup lo{"Lower", {}}, up{"Upper", {}};
      const MeshPtr cube = primitives::cube(1.0f);
      for (int part = 0; part < 2; part++) {
        const uint32_t base = (uint32_t)m->vert_count();
        for (const Vec3 &p : cube->positions) {
          m->add_vert(part == 0 ? p + Vec3(0, 0.5f, 0) : Vec3(p.x, p.y, p.z * 2.0f) + Vec3(0, 1.5f, 0));
          lo.weights.push_back(part == 0 ? 1.0f : 0.0f);
          up.weights.push_back(part == 0 ? 0.0f : 1.0f);
        }
        for (size_t f = 0; f < cube->face_count(); f++) {
          std::vector<uint32_t> face;
          for (uint32_t c = cube->face_offsets[f]; c < cube->face_offsets[f + 1]; c++) face.push_back(base + cube->corner_verts[c]);
          m->add_face(face.data(), face.size());
        }
      }
      m->vertex_groups = {lo, up};
      skin = s.create("Body");
      skin->add<MeshFilter>()->mesh = m;
      skin->add<SkinModifier>()->object = "Rig";
    }
  };
  test("body: each bone weighs its share of the skinned mesh's volume, with its center and inertia (two boxes: 25 and 50 of 75 kg)", [] {
    R136Body r;
    const body::Body b = body::build(r.s, *r.rig, 75.0f);
    const body::BoneMass *lo = nullptr, *up = nullptr;
    for (const body::BoneMass &bm : b.bones) (bm.name == "Lower" ? lo : up) = &bm;
    CHECK(lo && up && b.valid() && !b.from_capsules);
    if (!lo || !up) return;
    std::printf("    Lower %.3f kg at (%.3f, %.3f, %.3f); Upper %.3f kg at (%.3f, %.3f, %.3f); inertia of Upper %.3f %.3f %.3f\n", lo->mass, lo->center.x,
                lo->center.y, lo->center.z, up->mass, up->center.x, up->center.y, up->center.z, up->inertia(0, 0), up->inertia(1, 1), up->inertia(2, 2));
    CHECK_NEAR(lo->mass, 25.0f, 1e-3);
    CHECK_NEAR(up->mass, 50.0f, 1e-3);
    CHECK_NEAR(b.mass, 75.0f, 1e-3);
    CHECK(length(lo->center - Vec3(0, 0.5f, 0)) < 1e-4f && length(up->center - Vec3(0, 1.5f, 0)) < 1e-4f);
    /* A box's inertia: m (b^2 + c^2) / 12 about each axis, the other two sides b and c (1 x 1 x 2 here). */
    CHECK_NEAR(up->inertia(0, 0), 50.0f * (1 + 4) / 12.0f, 1e-2);
    CHECK_NEAR(up->inertia(1, 1), 50.0f * (1 + 4) / 12.0f, 1e-2);
    CHECK_NEAR(up->inertia(2, 2), 50.0f * (1 + 1) / 12.0f, 1e-2);
    CHECK(std::fabs(up->inertia(0, 1)) < 1e-3f && std::fabs(up->inertia(0, 2)) < 1e-3f && std::fabs(up->inertia(1, 2)) < 1e-3f);
    CHECK_NEAR(lo->inertia(1, 1), 25.0f * 2 / 12.0f, 1e-2);
  });
  test("body: the center of mass and the whole body's inertia follow the pose; an armature without a skin gets capsules round its bones", [] {
    R136Body r;
    const body::Body b = body::build(r.s, *r.rig, 75.0f);
    std::vector<ArmatureBone> now;
    armature_bones(*r.rig, now);
    const body::State rest = body::state(b, now);
    r.upper->set_local_rotation(Quat::euler({0, 0, 90}));  // (Upper turned a quarter about z at its joint: its box goes to -x)
    armature_bones(*r.rig, now);
    const body::State bent = body::state(b, now);
    const Vec3 want_rest(0, (25 * 0.5f + 50 * 1.5f) / 75.0f, 0), want_bent((50 * -0.5f) / 75.0f, (25 * 0.5f + 50 * 1.0f) / 75.0f, 0);
    std::printf("    at rest (%.4f, %.4f, %.4f), bent (%.4f, %.4f, %.4f); the bent body's inertia %.2f %.2f %.2f\n", rest.com.x, rest.com.y, rest.com.z,
                bent.com.x, bent.com.y, bent.com.z, bent.inertia(0, 0), bent.inertia(1, 1), bent.inertia(2, 2));
    CHECK(length(rest.com - want_rest) < 1e-4f && length(bent.com - want_bent) < 1e-4f);
    /* The whole body about its center (parallel axes): about z, the two boxes' own plus their masses at their distances. */
    const Vec3 dl = Vec3(0, 0.5f, 0) - want_bent, du = Vec3(-0.5f, 1.0f, 0) - want_bent;
    const float izz = 25.0f * 2 / 12.0f + 50.0f * 2 / 12.0f + 25.0f * (dl.x * dl.x + dl.y * dl.y) + 50.0f * (du.x * du.x + du.y * du.y);
    CHECK_NEAR(bent.inertia(2, 2), izz, 1e-2);
    R136Body bare(false);
    const body::Body cap = body::build(bare.s, *bare.rig, 75.0f);
    armature_bones(*bare.rig, now);
    const body::State cs = body::state(cap, now);
    std::printf("    no skin: %s, %.1f kg, center (%.4f, %.4f, %.4f)\n", cap.from_capsules ? "capsules" : "NOT capsules", cap.mass, cs.com.x, cs.com.y, cs.com.z);
    CHECK(cap.from_capsules && std::fabs(cap.mass - 75.0f) < 1e-3f && std::fabs(cs.com.x) < 1e-4f && std::fabs(cs.com.z) < 1e-4f && cs.com.y > 0.5f);
    Scene empty;
    GameObject *lone = empty.create("Lone");
    lone->add<Armature>();
    CHECK(!body::build(empty, *lone, 75.0f).valid());  // (no bones: no body, no crash)
  });
  test("body: the center of mass at every frame of a clip (the scene posed and put back as it was)", [] {
    R136Body r;
    const body::Body b = body::build(r.s, *r.rig, 75.0f);
    AnimationClip clip;
    AnimTrack &x = clip.track("", "Position", 0);
    x.set_key(0.0f, 0.0f, AnimLinear);
    x.set_key(10.0f, 10.0f, AnimLinear);
    const Vec3 before = r.rig->local().position;
    const std::vector<Vec3> path = body::com_trajectory(r.s, *r.rig, clip, b, 0.0f, 10.0f, 11);
    float worst = 0;
    for (size_t i = 0; i < path.size(); i++) worst = std::max(worst, std::fabs(path[i].x - (float)i));
    std::printf("    %zu frames, the largest step error %.5f; the rig back at x %.3f\n", path.size(), worst, r.rig->local().position.x);
    CHECK(path.size() == 11 && worst < 1e-4f);
    CHECK(length(r.rig->local().position - before) < 1e-6f);
  });
  /* ---------------------------------------------------------------- round 137: contacts and fulcrum points (task 0116) */
  test("contacts: an IK leg's rests are found from its motion (low and still), a manual contact is kept, and pinning holds the target still through each", [] {
    R136Body r;
    GameObject *foot = r.s.create("Foot", r.rig);
    foot->set_local_position({0.5f, 1.2f, 0});
    IKConstraint *ik = r.upper->add<IKConstraint>();
    ik->target = "Foot";
    ik->chain_length = 2;
    solve_constraints(r.s);
    /* The target rests (drifting a millimetre a frame) 0-10, lifts and moves on 10-20, rests 20-30. */
    AnimationClip clip;
    clip.frame_rate = 30.0f;
    clip.length = 30.0f;
    for (int f = 0; f <= 30; f++) {
      const float lift = f > 10 && f < 20 ? 0.4f * std::sin((f - 10) / 10.0f * 3.14159265f) : 0.0f;
      const float x = f <= 10 ? 0.5f + 0.001f * f : f >= 20 ? 0.7f + 0.001f * (f - 20) : 0.51f + 0.019f * (f - 10);
      clip.track("Foot", "Position", 0).set_key((float)f, x, AnimLinear);
      clip.track("Foot", "Position", 1).set_key((float)f, 1.2f + lift, AnimLinear);
      clip.track("Foot", "Position", 2).set_key((float)f, 0.0f, AnimLinear);
    }
    clip.contacts.push_back({"Lower", 3.0f, 4.0f, true});  // (by hand, on another bone: kept)
    const std::vector<body::Effector> eff = body::effectors(r.s, *r.rig, *r.rig);
    CHECK(eff.size() == 1 && eff[0].bone == r.upper && eff[0].target == foot && eff[0].target_path == "Foot");
    body::ContactSettings cs;
    cs.speed = 0.1f;
    clip.contacts = body::find_contacts(r.s, *r.rig, clip, eff, cs);
    std::string list;
    for (const auto &k : clip.contacts) list += strprintf("%s %.0f-%.0f%s; ", k.bone.c_str(), k.from, k.to, k.manual ? " (manual)" : "");
    std::printf("    contacts: %s\n", list.c_str());
    int found = 0;
    for (const auto &k : clip.contacts) {
      if (k.manual) continue;
      found++;
      const bool first = k.from <= 1.0f && k.to >= 9.0f && k.to <= 11.0f, second = k.from >= 19.0f && k.from <= 21.0f && k.to >= 29.0f;
      CHECK(k.bone == "Upper" && (first || second));
    }
    CHECK(found == 2 && clip.contacts.size() == 3);
    size_t unpinned = 0;
    const size_t keys = body::pin_contacts(r.s, *r.rig, clip, eff, &unpinned);
    float drift = 0;
    for (const auto &k : clip.contacts) {
      if (k.manual) continue;
      const float x0 = clip.track("Foot", "Position", 0).sample(k.from);
      for (float f = k.from; f <= k.to; f += 1.0f) drift = std::max(drift, std::fabs(clip.track("Foot", "Position", 0).sample(f) - x0));
    }
    std::printf("    pinned with %zu keys (%zu contact(s) on a bone without IK skipped); the target's drift in contact %.6f\n", keys, unpinned, drift);
    CHECK(keys > 0 && unpinned == 1 && drift < 1e-6f);
    AnimationClip back;
    CHECK(load_clip_text(save_clip_text(clip), back) && back.contacts.size() == clip.contacts.size() && back.contacts[0].bone == clip.contacts[0].bone);
  });

  /* ---------------------------------------------------------------- round 137: ballistic flight and angular momentum (task 0117) */
  /* A jump the wrong shape: the rig rises in a straight line to 1 m by frame 15 and falls back by 25 (30 fps), on the
   * ground (a manual contact) 0-5 and 25-30. `tuck`: Upper turns half a turn about z through the flight. */
  auto r138_jump = [](R136Body &r, bool tuck, float far = 0.0f) {
    AnimationClip clip;
    clip.frame_rate = 30.0f;
    clip.length = 30.0f;
    for (int f = 0; f <= 30; f++) {
      const float y = f <= 5 || f >= 25 ? 0.0f : f <= 15 ? (f - 5) / 10.0f : (25 - f) / 10.0f;
      clip.track("", "Position", 0).set_key((float)f, f <= 5 ? 0.0f : f >= 25 ? far : far * (f - 5) / 20.0f, AnimLinear);
      clip.track("", "Position", 1).set_key((float)f, y, AnimLinear);
      if (tuck) clip.track("Lower/Upper", "Rotation", 2).set_key((float)f, f <= 5 ? 0.0f : f >= 25 ? 180.0f : 9.0f * (f - 5), AnimLinear);
    }
    clip.contacts = {{"Upper", 0.0f, 5.0f, true}, {"Upper", 25.0f, 30.0f, true}};
    (void)r;
    return clip;
  };
  test("flight: in the air the center of mass follows a parabola under gravity between take-off and landing; the keyed correction gives exactly that", [&] {
    R136Body r;
    const body::Body b = body::build(r.s, *r.rig, 75.0f);
    const AnimationClip clip = r138_jump(r, false);
    body::FlightSettings fs;
    fs.keep_momentum = false;
    body::FlightResult res;
    std::string error;
    const bool ok = body::correct_flight(r.s, *r.rig, clip, b, fs, res, error);
    const float dt = 1.0f / 30.0f;
    float worst = 0;  // (the second difference of a parabola: g dt^2 everywhere)
    for (int f = 6; f <= 24; f++) {
      const float acc = (res.com_after[(size_t)f + 1].y - 2 * res.com_after[(size_t)f].y + res.com_after[(size_t)f - 1].y) / (dt * dt);
      worst = std::max(worst, std::fabs(acc + 9.81f));
    }
    const std::vector<Vec3> keyed = body::com_trajectory(r.s, *r.rig, res.corrected, b, 0.0f, 30.0f, 31);
    float realised = 0;
    for (int f = 0; f <= 30; f++) realised = std::max(realised, length(keyed[(size_t)f] - res.com_after[(size_t)f]));
    std::printf("    %s: %zu flight(s), take-off %.2f m/s up; the worst acceleration error %.4f m/s^2; the keyed clip within %.6f m; %zu keys\n",
                ok ? "corrected" : error.c_str(), res.flights.size(), res.flights.empty() ? 0.0f : res.flights[0].v0.y, worst, realised, res.keys);
    CHECK(ok && res.flights.size() == 1 && res.flights[0].takeoff == 5 && res.flights[0].landing == 25 && res.flights[0].possible);
    CHECK(worst < 1e-2f && realised < 1e-4f);
    CHECK(length(res.com_after[5] - res.com_before[5]) < 1e-6f && length(res.com_after[25] - res.com_before[25]) < 1e-6f);
    int changed = 0, ground = 0;
    for (size_t f = 0; f < res.marks.size(); f++) {
      changed += res.marks[f] == body::MarkChanged;
      ground += res.marks[f] == body::MarkGround;
    }
    CHECK(changed >= 15 && ground == 12);  // (0-5 and 25-30 on the ground; the flight's frames moved: it was the wrong shape)
  });
  test("flight: a tuck's angular momentum is kept through the flight; the landing pose kept too when asked; an impossible jump is marked and not written", [&] {
    R136Body r;
    const body::Body b = body::build(r.s, *r.rig, 75.0f);
    const AnimationClip clip = r138_jump(r, true);
    const std::vector<Vec3> L_before = body::angular_momentum(r.s, *r.rig, clip, b);
    body::FlightSettings fs;
    fs.keep_landing = false;
    body::FlightResult res;
    std::string error;
    CHECK(body::correct_flight(r.s, *r.rig, clip, b, fs, res, error));
    const std::vector<Vec3> L_after = body::angular_momentum(r.s, *r.rig, res.corrected, b);
    const Vec3 L0 = L_after[7];
    float spread_before = 0, spread_after = 0;
    for (int f = 7; f <= 23; f++) {
      spread_before = std::max(spread_before, length(L_before[(size_t)f] - L_before[7]));
      spread_after = std::max(spread_after, length(L_after[(size_t)f] - L0));
    }
    std::printf("    |L| at take-off %.3f; its largest change in the air: %.3f before, %.3f after (%.1f%%)\n", length(L0), spread_before, spread_after,
                100.0f * spread_after / std::max(1e-6f, length(L0)));
    CHECK(length(L0) > 1e-3f && spread_after < 0.01f * length(L0) && spread_after < spread_before);
    fs.keep_landing = true;
    body::FlightResult kept;
    CHECK(body::correct_flight(r.s, *r.rig, clip, b, fs, kept, error));
    const std::vector<Vec3> c_kept = body::com_trajectory(r.s, *r.rig, kept.corrected, b, 24.0f, 25.0f, 2);
    CHECK(length(c_kept[1] - kept.com_before[25]) < 1e-5f);  // (the landing frame untouched)
    const AnimationClip far = r138_jump(r, false, 100.0f);
    body::FlightResult wild;
    CHECK(body::correct_flight(r.s, *r.rig, far, b, body::FlightSettings(), wild, error));
    std::printf("    100 m in 0.7 s: %s (%s), %zu keys\n", wild.flights[0].possible ? "possible" : "impossible", wild.flights[0].why.c_str(), wild.keys);
    CHECK(!wild.flights[0].possible && wild.keys == 0 && wild.marks[15] == body::MarkImpossible);
    AnimationClip none = clip;
    none.contacts.clear();
    CHECK(!body::correct_flight(r.s, *r.rig, none, b, fs, res, error) && error.find("contacts") != std::string::npos);
  });
  test("physics in the editor: Body Mass, contacts by hand and found, the flight shown as a ghost with its frames' colours, Apply as one undo step", [] {
    Editor ed;
    r34_open(ed);
    Scene &s = ed.scene();
    GameObject *rig = s.create("Rig");
    rig->add<Armature>();
    GameObject *lower = s.create("Lower", rig);
    lower->add<Bone>();
    GameObject *upper = s.create("Upper", lower);
    upper->set_local_position({0, 1, 0});
    upper->add<Bone>()->rest_position = {0, 1, 0};
    ed.mark_changed_for_test("Add the rig");
    ed.command("select Rig");
    ed.command("anim new Jump");
    AnimationClipPtr clip = ed.anim_clip_for_test();
    CHECK(clip != nullptr);
    if (!clip) return;
    clip->frame_rate = 30.0f;
    clip->length = 30.0f;
    for (int f = 0; f <= 30; f++)
      clip->track("", "Position", 1).set_key((float)f, f <= 5 || f >= 25 ? 0.0f : f <= 15 ? (f - 5) / 10.0f : (25 - f) / 10.0f, AnimLinear);
    clip->touch();
    ed.command("body mass 80");
    CHECK_NEAR(rig->get<Armature>()->body_mass, 80.0f, 1e-4);
    ed.command("body contacts add Upper 0 5");
    ed.command("body contacts add Upper 25 30");
    CHECK(clip->contacts.size() == 2 && clip->contacts[0].manual);
    ed.command("body flight show");
    ed.step_frame_headless();
    const bool shown = clip->find("Lower", "Position", 1) == nullptr;  // (shown only: nothing written yet)
    ed.command("body flight apply");
    const AnimTrack *hips = clip->find("Lower", "Position", 1);
    const float y15 = hips ? hips->sample(15.0f) : 0.0f;
    ed.command("undo");
    AnimationClipPtr after_undo = ed.anim_clip_for_test();
    const bool undone = after_undo && after_undo->find("Lower", "Position", 1) == nullptr;
    std::printf("    shown without writing: %s; applied: the hips at frame 15 moved %.3f m; undone: %s\n", shown ? "yes" : "no", y15, undone ? "yes" : "no");
    CHECK(shown && hips && std::fabs(y15) > 1e-3f && undone);
    ed.command("body show on");
    ed.step_frame_headless();  // (the center of mass's path drawn: no crash, no change to the scene)
    CHECK(length(rig->local().position) < 1e-6f);
  });
  test("physics (review): pinning and a flight carry IK targets right with a moving root; a contact outside the clip is skipped; a manual contact trims a found one", [] {
    R136Body r;
    GameObject *foot = r.s.create("Foot", r.rig);
    foot->set_local_position({0.5f, 1.2f, 0});
    IKConstraint *ik = r.upper->add<IKConstraint>();
    ik->target = "Foot";
    solve_constraints(r.s);
    /* The rig walks along x (root motion); the target drifts in its own space, so in the world it moves twice. */
    AnimationClip clip;
    clip.frame_rate = 30.0f;
    clip.length = 30.0f;
    for (int f = 0; f <= 30; f++) {
      clip.track("", "Position", 0).set_key((float)f, 0.05f * f, AnimLinear);
      clip.track("Foot", "Position", 0).set_key((float)f, 0.5f + 0.002f * f, AnimLinear);
      clip.track("Foot", "Position", 1).set_key((float)f, 1.2f, AnimLinear);
    }
    clip.contacts = {{"Upper", 5.0f, 12.0f, true}, {"Upper", 100.0f, 110.0f, true}};
    const std::vector<body::Effector> eff = body::effectors(r.s, *r.rig, *r.rig);
    size_t skipped = 0, replaced = 0;
    body::pin_contacts(r.s, *r.rig, clip, eff, &skipped, &replaced);
    float drift = 0;
    Vec3 first(0.0f);
    body::for_each_frame(r.s, *r.rig, clip, 5, 12, [&](int f) {
      const Vec3 w = foot->world_position();
      if (f == 5) first = w;
      drift = std::max(drift, length(w - first));
    });
    std::printf("    pinned under a walking root: the target's world drift %.6f; skipped %zu (outside the clip), %zu keys replaced; end %.0f\n", drift, skipped,
                replaced, clip.end_frame());
    CHECK(drift < 1e-5f && skipped == 1 && replaced > 0 && clip.end_frame() <= 30.0f);
    /* A manual contact over part of a rest: the rest outside it is still found. */
    AnimationClip rest;
    rest.frame_rate = 30.0f;
    rest.length = 30.0f;
    rest.track("Foot", "Position", 1).set_key(0.0f, 1.2f, AnimLinear);
    rest.track("Foot", "Position", 1).set_key(30.0f, 1.2f, AnimLinear);
    rest.contacts = {{"Upper", 10.0f, 12.0f, true}};
    const std::vector<AnimationClip::Contact> found = body::find_contacts(r.s, *r.rig, rest, eff, body::ContactSettings());
    std::string list;
    for (const auto &k : found) list += strprintf("%.0f-%.0f%s ", k.from, k.to, k.manual ? "m" : "");
    std::printf("    a still foot with a manual contact 10-12: %s\n", list.c_str());
    CHECK(found.size() == 3);
  });
  test("physics (review): a flight with a moving root keeps the feet with the body; a smooth key already there keeps its curve; a fast double spin keeps its momentum; an imported skeleton's root bone is the hips", [] {
    R136Body r;
    GameObject *foot = r.s.create("Foot", r.rig);
    foot->set_local_position({0.5f, 1.2f, 0});
    r.upper->add<IKConstraint>()->target = "Foot";
    solve_constraints(r.s);
    const body::Body b = body::build(r.s, *r.rig, 75.0f);
    AnimationClip clip;
    clip.frame_rate = 30.0f;
    clip.length = 30.0f;
    for (int f = 0; f <= 30; f++) {
      clip.track("", "Position", 0).set_key((float)f, 0.05f * f, AnimLinear);
      clip.track("", "Position", 1).set_key((float)f, f <= 5 || f >= 25 ? 0.0f : f <= 15 ? (f - 5) / 10.0f : (25 - f) / 10.0f, AnimLinear);
      clip.track("Foot", "Position", 0).set_key((float)f, 0.5f, AnimLinear);
      clip.track("Foot", "Position", 1).set_key((float)f, 1.2f, AnimLinear);
    }
    clip.track("Lower", "Position", 1).set_key(25.0f, 0.0f, AnimAuto);  // (the animator's own smooth key at the landing)
    clip.contacts = {{"Upper", 0.0f, 5.0f, true}, {"Upper", 25.0f, 30.0f, true}};
    body::FlightResult res;
    std::string error;
    CHECK(body::correct_flight(r.s, *r.rig, clip, b, body::FlightSettings(), res, error));
    /* The foot's target in the hips' space: as it was, every frame (the body moved as one, the legs not stretched). */
    std::vector<Vec3> before, after;
    body::for_each_frame(r.s, *r.rig, clip, 0, 30, [&](int) { before.push_back(r.lower->world_matrix().inverse().point(foot->world_position())); });
    body::for_each_frame(r.s, *r.rig, res.corrected, 0, 30, [&](int) { after.push_back(r.lower->world_matrix().inverse().point(foot->world_position())); });
    float worst = 0;
    for (size_t f = 0; f < before.size(); f++) worst = std::max(worst, length(after[f] - before[f]));
    const AnimTrack *hy = res.corrected.find("Lower", "Position", 1);
    const int landing = hy ? hy->key_at(25.0f) : -1;
    std::printf("    the foot in the hips' space moved at most %.6f; the landing key's interpolation %d (kept: %d); %zu keys replaced\n", worst,
                landing >= 0 ? hy->keys[(size_t)landing].interp : -1, AnimAuto, res.replaced);
    CHECK(worst < 1e-4f);
    CHECK(landing >= 0 && hy->keys[(size_t)landing].interp == AnimAuto && res.replaced > 0);
    /* Two whole turns of Upper in 20 frames: a large correction, still the take-off's momentum. */
    AnimationClip spin;
    spin.frame_rate = 30.0f;
    spin.length = 30.0f;
    for (int f = 0; f <= 30; f++) {
      spin.track("", "Position", 1).set_key((float)f, f <= 5 || f >= 25 ? 0.0f : f <= 15 ? (f - 5) / 10.0f : (25 - f) / 10.0f, AnimLinear);
      spin.track("Lower/Upper", "Rotation", 2).set_key((float)f, f <= 5 ? 0.0f : f >= 25 ? 720.0f : 36.0f * (f - 5), AnimLinear);
    }
    spin.contacts = {{"Upper", 0.0f, 5.0f, true}, {"Upper", 25.0f, 30.0f, true}};
    R136Body plain;
    const body::Body pb = body::build(plain.s, *plain.rig, 75.0f);
    body::FlightSettings fs;
    fs.keep_landing = false;
    CHECK(body::correct_flight(plain.s, *plain.rig, spin, pb, fs, res, error));
    const std::vector<Vec3> L = body::angular_momentum(plain.s, *plain.rig, res.corrected, pb);
    float spread = 0;
    for (int f = 7; f <= 23; f++) spread = std::max(spread, length(L[(size_t)f] - L[7]));
    std::printf("    a double spin: |L| %.2f, its largest change in the air %.2f (%.1f%%), the body turned up to %.0f degrees\n", length(L[7]), spread,
                100.0f * spread / std::max(1e-6f, length(L[7])), res.flights[0].turn);
    CHECK(spread < 0.01f * length(L[7]));
    Scene s;
    GameObject *top = s.create("Hips");
    top->add<Armature>();
    top->add<Bone>();
    GameObject *leg = s.create("Leg", top);
    leg->add<Bone>();
    CHECK(body::hips_of(*top) == top);
  });
  test("physics in the editor (review): the center of mass's overlay builds the body once, not every frame; Apply never writes one character's flight into another's clip", [] {
    Editor ed;
    r34_open(ed);
    Scene &s = ed.scene();
    auto make = [&](const char *name, float x) {
      GameObject *rig = s.create(name);
      rig->add<Armature>();
      rig->set_local_position({x, 0, 0});
      GameObject *lower = s.create(std::string(name) + "Lower", rig);
      lower->add<Bone>();
      GameObject *upper = s.create(std::string(name) + "Upper", lower);
      upper->set_local_position({0, 1, 0});
      upper->add<Bone>()->rest_position = {0, 1, 0};
      MeshPtr m = primitives::cube(0.5f);
      m->vertex_groups = {{std::string(name) + "Lower", std::vector<float>(m->vert_count(), 1.0f)}};
      GameObject *skin = s.create(std::string(name) + "Skin");
      skin->add<MeshFilter>()->mesh = m;
      skin->add<SkinModifier>()->object = name;
      return rig;
    };
    make("A", 0.0f);
    make("B", 3.0f);
    ed.mark_changed_for_test("Add the rigs");
    for (const char *name : {"B", "A"}) {
      ed.command(std::string("select ") + name);
      ed.command(std::string("anim new ") + name + "Jump");
      AnimationClipPtr clip = ed.anim_clip_for_test();
      if (!clip) continue;
      clip->frame_rate = 30.0f;
      clip->length = 30.0f;
      for (int f = 0; f <= 30; f++)
        clip->track("", "Position", 1).set_key((float)f, f <= 5 || f >= 25 ? 0.0f : f <= 15 ? (f - 5) / 10.0f : (25 - f) / 10.0f, AnimLinear);
      clip->contacts = {{std::string(name) + "Upper", 0.0f, 5.0f, true}, {std::string(name) + "Upper", 25.0f, 30.0f, true}};
      clip->touch();
    }
    ed.command("body show on");
    for (int k = 0; k < 4; k++) ed.step_frame_headless();
    const size_t builds = ed.body_builds_for_test();
    ed.command("body flight show");
    const bool shown = ed.body_ghost_for_test();
    ed.command("select B");
    AnimationClipPtr b = ed.anim_clip_for_test();
    const size_t tracks = b ? b->tracks.size() : 0;
    ed.command("body flight apply");
    std::printf("    4 frames with the overlay: %zu body built; A's flight shown: %s; B's tracks after Apply on B: %zu (was %zu)\n", builds, shown ? "yes" : "no",
                b ? b->tracks.size() : 0, tracks);
    CHECK(builds == 1 && shown && b && b->tracks.size() == tracks);
  });
  /* ---------------------------------------------------------------- round 138: the AutoPhysics spike (task 0118, ADR 0043) */
  test("qp: a small problem's known answer, an equality, bounds, a warm start; a long banded one solves in its band", [] {
    {  // min (x - 1)^2 + (y - 2)^2 subject to x + y <= 2: (0.5, 1.5)
      qp::Problem pr(2);
      pr.least_squares({{0, 1.0}}, 1.0, 2.0);
      pr.least_squares({{1, 1.0}}, 2.0, 2.0);
      pr.constrain({{0, 1.0}, {1, 1.0}}, -1e30, 2.0);
      const qp::Result r = qp::solve(pr);
      std::printf("    the corner: %s in %d steps, (%.6f, %.6f)\n", r.status.c_str(), r.iterations, r.solved ? r.x[0] : 0.0, r.solved ? r.x[1] : 0.0);
      CHECK(r.solved && std::fabs(r.x[0] - 0.5) < 1e-5 && std::fabs(r.x[1] - 1.5) < 1e-5);
      pr.constrain({{0, 1.0}, {1, -1.0}}, 0.0, 0.0);  // (and x = y: (1, 1))
      pr.constrain({{0, 1.0}}, -1e30, 0.75);           // (and x <= 0.75: (0.75, 0.75))
      const qp::Result w = qp::solve(pr, qp::Settings(), &r);
      std::printf("    with x = y and x <= 0.75: (%.6f, %.6f)\n", w.solved ? w.x[0] : 0.0, w.solved ? w.x[1] : 0.0);
      CHECK(w.solved && std::fabs(w.x[0] - 0.75) < 1e-5 && std::fabs(w.x[1] - 0.75) < 1e-5);
    }
    {  // A chain: x0 = 0, x(n-1) = 1, each step's square small: a straight line, the band 1 whatever its length.
      const int n = 2000;
      qp::Problem pr(n);
      for (int i = 1; i < n; i++) pr.least_squares({{i, 1.0}, {i - 1, -1.0}}, 0.0, 1.0);
      pr.constrain({{0, 1.0}}, 0.0, 0.0);
      pr.constrain({{n - 1, 1.0}}, 1.0, 1.0);
      const qp::Result r = qp::solve(pr);
      double err = 0.0;
      for (int i = 0; r.solved && i < n; i++) err = std::max(err, std::fabs(r.x[i] - (double)i / (n - 1)));
      std::printf("    a chain of %d: %s, band %d, %d steps, %d factorisations, worst %.2g\n", n, r.status.c_str(), r.band, r.iterations, r.factorisations, err);
      CHECK(r.solved && r.band == 1 && err < 1e-4);
    }
    {  // Nothing to stand on: x <= 0 and x >= 1 - refused, not solved.
      qp::Problem pr(1);
      pr.least_squares({{0, 1.0}}, 0.0, 1.0);
      const int row = pr.constrain({{0, 1.0}}, 1.0, 0.0);
      const qp::Result r = qp::solve(pr);
      CHECK(row == 0 && !r.solved && !r.status.empty());
    }
  });
  /* Four clips as an animator might key them (30 fps, 100 frames, 75 kg): their centers and momenta, feet and hands. */
  struct SpikeClip {
    const char *name;
    body::CentroidalInput in;
    int flight_from = -1, flight_to = -1;  // (frames in the air, when there are)
    float may_move = 0.3f;                 // (how far its center may go: what the clip asks of the body)
  };
  auto spike_clips = [] {
    auto smooth = [](float t) { t = std::min(1.0f, std::max(0.0f, t)); return t * t * (3.0f - 2.0f * t); };
    std::vector<SpikeClip> clips;
    const int n = 100;
    {  // A jump: a crouch, a flight keyed as a sine arc (not a parabola), a landing.
      SpikeClip c{"a jump", {}, 40, 59};
      for (int k = 0; k < n; k++) {
        float y = 1.0f, z = 0.0f;
        if (k < 40) y = 1.0f - 0.15f * std::sin(3.14159f * smooth((k - 25) / 15.0f)), z = 0.1f * smooth((k - 30) / 10.0f);
        else if (k < 60) y = 0.95f + 0.35f * std::sin(3.14159f * (k - 40) / 20.0f), z = 0.1f + 0.5f * (k - 40) / 20.0f;
        else y = 1.0f - 0.15f * std::sin(3.14159f * smooth((k - 60) / 25.0f)), z = 0.6f + 0.1f * smooth((k - 60) / 10.0f);
        if (k >= 40 && k < 60) y = std::max(y, 0.95f);
        c.in.com.push_back({0.0f, y, z});
      }
      c.in.contacts = {{{-0.12f, 0, 0.1f}, 0, 39}, {{0.12f, 0, 0.1f}, 0, 39}, {{-0.12f, 0, 0.7f}, 60, 99}, {{0.12f, 0, 0.7f}, 60, 99}};
      clips.push_back(c);
    }
    {  // A run: 3 m/s, each foot down 8 frames then 4 in the air, the center bobbing 3 cm as keyed.
      SpikeClip c{"a run", {}, -1, -1, 0.1f};  // (nearly possible as keyed: a few cm)
      for (int k = 0; k < n; k++) c.in.com.push_back({0.0f, 0.95f + 0.03f * std::cos(2.0f * 3.14159f * k / 12.0f), 3.0f * k / 30.0f});
      for (int s = 0; s * 12 < n; s++) {
        const int from = s * 12, to = std::min(n - 1, s * 12 + 7);
        c.in.contacts.push_back({{s % 2 ? 0.1f : -0.1f, 0.0f, 3.0f * (from + to) * 0.5f / 30.0f}, from, to});
      }
      clips.push_back(c);
    }
    {  // A flip: a full second in the air, the spin keyed slowing down (60 -> 30 kg m^2/s) - not possible.
      SpikeClip c{"a flip", {}, 40, 69, 0.4f};  // (a big spin from the feet in a third of a second pushes the body sideways)
      for (int k = 0; k < n; k++) {
        float y = 1.0f;
        if (k < 40) y = 1.0f - 0.2f * std::sin(3.14159f * smooth((k - 25) / 15.0f));
        else if (k < 70) y = 0.95f + 0.9f * std::sin(3.14159f * (k - 40) / 30.0f);
        else y = 1.0f - 0.2f * std::sin(3.14159f * smooth((k - 70) / 25.0f));
        c.in.com.push_back({0.0f, std::max(y, 0.75f), 0.0f});
        float L = 0.0f;
        if (k >= 30 && k < 40) L = 60.0f * (k - 30) / 10.0f;
        else if (k >= 40 && k < 70) L = 60.0f - 30.0f * (k - 40) / 29.0f;
        else if (k >= 70 && k < 80) L = 30.0f * (80 - k) / 10.0f;
        c.in.momentum.push_back({L, 0.0f, 0.0f});
      }
      c.in.contacts = {{{-0.12f, 0, 0}, 0, 39}, {{0.12f, 0, 0}, 0, 39}, {{-0.12f, 0, 0}, 70, 99}, {{0.12f, 0, 0}, 70, 99}};
      clips.push_back(c);
    }
    {  // A fall onto the hands: the center tips forward and down; the hands land at frame 55.
      SpikeClip c{"a fall onto the hands", {}, -1, -1, 0.25f};
      for (int k = 0; k < n; k++) {
        const float t = smooth((k - 20) / 40.0f);
        c.in.com.push_back({0.0f, 1.0f - 0.65f * t, 0.5f * t});
      }
      c.in.contacts = {{{-0.12f, 0, 0}, 0, 99}, {{0.12f, 0, 0}, 0, 99}, {{-0.2f, 0, 0.85f}, 55, 99}, {{0.2f, 0, 0.85f}, 55, 99}};
      clips.push_back(c);
    }
    return clips;
  };
  test("autophysics spike: four clips (a jump, a run, a flip, a fall onto the hands) made possible in centroidal dynamics - time, the laws' residuals, how far each moves", [&] {
    double worst_ms = 0.0;
    for (SpikeClip &c : spike_clips()) {
      const body::CentroidalResult r = body::solve_centroidal(c.in);
      worst_ms = std::max(worst_ms, r.ms);
      /* What the clip asked for, measured the same way: forces as the solver would need them can't fix the flight. */
      double flight_err = 0.0, spin_spread = 0.0;
      if (c.flight_from >= 0 && r.solved) {
        for (int k = c.flight_from + 1; k < c.flight_to; k++) {
          const double ay = (r.com[k + 1].y - 2.0 * r.com[k].y + r.com[k - 1].y) / (c.in.dt * c.in.dt);
          flight_err = std::max(flight_err, std::fabs(ay + 9.81));
        }
        double lo = 1e30, hi = -1e30;
        for (int k = c.flight_from; k <= c.flight_to; k++) lo = std::min(lo, (double)r.momentum[k].x), hi = std::max(hi, (double)r.momentum[k].x);
        spin_spread = hi - lo;
      }
      std::printf("    %-22s %s: %d unknowns, %d constraints, band %d; %d passes, %d steps, %.0f ms; Newton %.1e g, Euler %.1e m; moved %.3f m, "
                  "momentum %.1f kg m^2/s; forces up to %.2f body weights",
                  c.name, r.status.c_str(), r.unknowns, r.constraints, r.band, r.passes, r.iterations, r.ms, r.linear_residual, r.angular_residual, r.moved,
                  r.momentum_moved, r.max_force);
      if (c.flight_from >= 0) std::printf("; in the air: fall %.3f m/s^2 off g, spin spread %.2f", flight_err, spin_spread);
      std::printf("\n");
      CHECK(r.solved);
      CHECK(r.linear_residual < 2e-3 && r.angular_residual < 2e-3);  // (the laws hold to a fraction of a percent of the body's weight)
      CHECK(r.moved < c.may_move);
      if (c.flight_from >= 0) CHECK(flight_err < 0.2 && spin_spread < 0.5);
    }
    std::printf("    the slowest: %.0f ms for 100 frames (GO under about 1 s)\n", worst_ms);
    // (the time is a stress row: `physics`, task 0177)
  });
  test("autophysics spike: a clip that is already possible comes back nearly as it was; bad input is refused", [&] {
    body::CentroidalInput in;  // (standing still on both feet: possible as it is)
    for (int k = 0; k < 60; k++) in.com.push_back({0.0f, 1.0f, 0.0f});
    in.contacts = {{{-0.12f, 0, 0}, 0, 59}, {{0.12f, 0, 0}, 0, 59}};
    const body::CentroidalResult r = body::solve_centroidal(in);
    std::printf("    standing: %s, moved %.5f m, forces up to %.3f body weights\n", r.status.c_str(), r.moved, r.max_force);
    CHECK(r.solved && r.moved < 1e-3 && std::fabs(r.max_force - 0.5) < 0.01);  // (each foot half the weight)
    body::CentroidalInput bad = in;
    bad.com[3].x = NAN;
    CHECK(!body::solve_centroidal(bad).solved);
    bad = in;
    bad.contacts[0].to = 80;
    CHECK(!body::solve_centroidal(bad).solved);
    bad = in;
    bad.com.resize(2);
    CHECK(!body::solve_centroidal(bad).solved);
  });
  /* The rig of round 137 with an IK end: Upper reaches for "Foot" (under the rig, at Upper's tip), so the body stands on
   * Upper's tip; `jump` keys the root up in a straight line and back (r138_jump's shape), on the ground 0-5 and 25-30. */
  auto r138_rig = [](R136Body &r) {
    GameObject *foot = r.s.create("Foot", r.rig);
    foot->set_local_position({0, 2, 0});
    IKConstraint *ik = r.upper->add<IKConstraint>();
    ik->target = "Foot";
    ik->chain_length = 1;  // (Upper only: the hips stay the body's to move)
    solve_constraints(r.s);
    return foot;
  };
  test("autophysics: a jump keyed in straight lines comes back possible - a parabola in the air, the foot held on the ground, the clip as keyed realises it", [&] {
    R136Body r;
    GameObject *foot = r138_rig(r);
    const body::Body b = body::build(r.s, *r.rig, 75.0f);
    AnimationClip clip = r138_jump(r, false);
    body::FlightResult res;
    std::string error;
    const bool ok = body::autophysics(r.s, *r.rig, clip, b, body::AutoPhysicsSettings(), res, error);
    const float dt = 1.0f / 30.0f;
    float worst = 0;  // (in the air, the solved center falls at g)
    for (int f = 7; ok && f <= 23; f++) {
      const float acc = (res.com_after[(size_t)f + 1].y - 2 * res.com_after[(size_t)f].y + res.com_after[(size_t)f - 1].y) / (dt * dt);
      worst = std::max(worst, std::fabs(acc + 9.81f));
    }
    const std::vector<Vec3> keyed = ok ? body::com_trajectory(r.s, *r.rig, res.corrected, b, 0.0f, 30.0f, 31) : std::vector<Vec3>();
    float realised = 0, held = 0;
    for (int f = 0; ok && f <= 30; f++) realised = std::max(realised, length(keyed[(size_t)f] - res.com_after[(size_t)f]));
    /* The foot's target in the world through each contact: where the clip had it. */
    std::vector<Vec3> before, after;
    body::for_each_frame(r.s, *r.rig, clip, 0, 30, [&](int) { before.push_back(foot->world_position()); });
    if (ok) body::for_each_frame(r.s, *r.rig, res.corrected, 0, 30, [&](int) { after.push_back(foot->world_position()); });
    for (int f : {0, 3, 5, 25, 28, 30})
      if (ok) held = std::max(held, length(after[(size_t)f] - before[(size_t)f]));
    int blue = 0, red = 0;
    for (uint8_t m : res.marks) blue += m == body::MarkChanged, red += m == body::MarkImpossible;
    std::printf("    %s: moved up to %.3f m and %.1f degrees, %d frames changed, %d impossible; in the air off g by %.3f m/s^2; the keyed clip "
                "within %.4f m; the foot held within %.5f m; %zu keys\n",
                ok ? "solved" : error.c_str(), ok ? res.flights[0].shift : 0.0f, ok ? res.flights[0].turn : 0.0f, blue, red, worst, realised, held, res.keys);
    CHECK(ok && res.flights.size() == 1 && res.flights[0].possible && red == 0 && blue > 10 && res.keys > 0);
    CHECK(worst < 0.05f);
    CHECK(realised < 0.01f);  // (the hips carry the correction; the IK end bends as it must)
    CHECK(held < 1e-4f);
  });
  test("autophysics: a clip that is already possible comes back as it was (green); without contacts or IK ends it says why; a solve can be stopped", [&] {
    R136Body r;
    r138_rig(r);
    const body::Body b = body::build(r.s, *r.rig, 75.0f);
    AnimationClip still;  // (standing on Upper's tip the whole clip)
    still.frame_rate = 30.0f;
    still.length = 30.0f;
    for (int f = 0; f <= 30; f++) still.track("", "Position", 1).set_key((float)f, 0.0f, AnimLinear);
    still.contacts = {{"Upper", 0.0f, 30.0f, true}};
    body::FlightResult res;
    std::string error;
    const bool ok = body::autophysics(r.s, *r.rig, still, b, body::AutoPhysicsSettings(), res, error);
    int green = 0;
    for (uint8_t m : res.marks) green += m == body::MarkFine;
    std::printf("    standing: %s, moved %.5f m and %.3f degrees, %d of %zu frames green\n", ok ? "solved" : error.c_str(), ok ? res.flights[0].shift : 0.0f,
                ok ? res.flights[0].turn : 0.0f, green, res.marks.size());
    CHECK(ok && green == 31 && res.flights[0].shift < 1e-3f);
    AnimationClip none = still;
    none.contacts.clear();
    CHECK(!body::autophysics(r.s, *r.rig, none, b, body::AutoPhysicsSettings(), res, error) && error.find("contacts") != std::string::npos);
    AnimationClip elsewhere = still;
    elsewhere.contacts = {{"Lower", 0.0f, 30.0f, true}};  // (Lower isn't an IK end)
    CHECK(!body::autophysics(r.s, *r.rig, elsewhere, b, body::AutoPhysicsSettings(), res, error) && error.find("foot or hand") != std::string::npos);
    std::shared_ptr<body::AutoPhysicsPrep> prep = body::autophysics_prepare(r.s, *r.rig, still, b, body::AutoPhysicsSettings(), error);
    std::atomic<bool> stop{true};
    CHECK(prep != nullptr);
    if (!prep) return;
    body::autophysics_input(*prep).cancel = &stop;
    const body::CentroidalResult cancelled = body::solve_centroidal(body::autophysics_input(*prep));
    CHECK(!cancelled.solved && cancelled.status == "cancelled" && !body::autophysics_finish(*prep, cancelled, res, error));
  });
  test("autophysics in the editor: solved on a worker while the editor runs, shown as a ghost, Apply one undo step; a clip edited meanwhile isn't shown", [] {
    Editor ed;
    r34_open(ed);
    Scene &s = ed.scene();
    GameObject *rig = s.create("Rig");
    rig->add<Armature>();
    GameObject *lower = s.create("Lower", rig);
    lower->add<Bone>();
    GameObject *upper = s.create("Upper", lower);
    upper->set_local_position({0, 1, 0});
    upper->add<Bone>()->rest_position = {0, 1, 0};
    GameObject *foot = s.create("Foot", rig);
    foot->set_local_position({0, 2, 0});
    IKConstraint *ik = upper->add<IKConstraint>();
    ik->target = "Foot";
    ik->chain_length = 1;
    ed.mark_changed_for_test("Add the rig");
    ed.command("select Rig");
    ed.command("anim new Jump");
    AnimationClipPtr clip = ed.anim_clip_for_test();
    CHECK(clip != nullptr);
    if (!clip) return;
    clip->frame_rate = 30.0f;
    clip->length = 30.0f;
    for (int f = 0; f <= 30; f++)
      clip->track("", "Position", 1).set_key((float)f, f <= 5 || f >= 25 ? 0.0f : f <= 15 ? (f - 5) / 10.0f : (25 - f) / 10.0f, AnimLinear);
    clip->touch();
    ed.command("body contacts add Upper 0 5");
    ed.command("body contacts add Upper 25 30");
    /* Frames until the worker's result is picked up (by the clock: a slow machine just takes more of them). */
    auto run_until_solved = [&ed] {
      int n = 0;
      const auto t0 = std::chrono::steady_clock::now();
      while (ed.body_solving_for_test() && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(60)) ed.step_frame_headless(), n++;
      return n;
    };
    ed.command("body physics show");
    const bool started = ed.body_solving_for_test(), keeps_drawing = ed.wants_continuous_redraw_for_test();
    const int frames = run_until_solved();
    const bool shown = ed.body_ghost_for_test() && clip->find("Lower", "Position", 1) == nullptr;  // (shown only: nothing written yet)
    ed.command("body physics apply");
    const AnimTrack *hips = clip->find("Lower", "Position", 1);
    const float y15 = hips ? hips->sample(15.0f) : 0.0f;
    ed.command("undo");
    AnimationClipPtr after_undo = ed.anim_clip_for_test();
    const bool undone = after_undo && after_undo->find("Lower", "Position", 1) == nullptr;
    /* Edited while it solves: the result is for the clip as it was, and isn't shown. */
    ed.command("body physics show");
    if (after_undo) {
      after_undo->track("", "Position", 1).set_key(15.0f, 1.2f, AnimLinear);
      after_undo->touch();
    }
    run_until_solved();
    const bool stale_hidden = !ed.body_solving_for_test() && !ed.body_ghost_for_test();  // (finished, and dropped)
    ed.command("body physics show");
    ed.command("body physics off");  // (stopped: the worker ends, nothing shown)
    std::printf("    started on a worker: %s, done after %d editor frames; shown without writing: %s; applied: the hips at 15 moved %.3f m; undone: %s; "
                "edited meanwhile: %s; off: %s\n",
                started ? "yes" : "no", frames, shown ? "yes" : "no", y15, undone ? "yes" : "no", stale_hidden ? "not shown" : "SHOWN",
                ed.body_solving_for_test() ? "STILL SOLVING" : "stopped");
    CHECK(started && keeps_drawing && frames >= 1 && shown && hips && std::fabs(y15) > 1e-3f && undone);  // (the editor wakes to pick it up)
    CHECK(stale_hidden && !ed.body_solving_for_test() && !ed.body_ghost_for_test());
  });
  /* ---------------------------------------------------------------- round 140: the active ragdoll (task 0121, ADR 0044) */
  /* Round 137's two-box body (Lower 25 kg, Upper 50 kg, standing 2 m) with Upper tipping 60 degrees about z over a second. */
  auto r140_clip = [] {
    AnimationClip clip;
    clip.frame_rate = 30.0f;
    clip.length = 60.0f;
    for (int f = 0; f <= 60; f++) clip.track("Lower/Upper", "Rotation", 2).set_key((float)f, f <= 30 ? 2.0f * f : 60.0f, AnimLinear);
    return clip;
  };
  test("ragdoll: at full strength the motors follow the clip within a few degrees; limp from frame 15 it falls and comes to rest on the ground", [&] {
    if (!ragdoll::available()) return (void)std::printf("    skipped (Jolt: Blender's libraries)\n");
    R136Body r;
    const body::Body b = body::build(r.s, *r.rig, 75.0f);
    const AnimationClip clip = r140_clip();
    body::FlightResult res;
    std::string error;
    ragdoll::Report rep;
    const bool ok = ragdoll::simulate(r.s, *r.rig, clip, b, ragdoll::Settings(), res, error, &rep);
    std::printf("    strong: %s, %d bodies, %d frames in %.0f ms; the largest difference from the clip %.1f degrees; %zu keys\n", ok ? "simulated" : error.c_str(),
                rep.bodies, rep.frames, rep.ms, rep.max_angle, res.keys);
    CHECK(ok && rep.bodies == 2 && rep.max_angle < 8.0f && res.keys > 0);
    AnimationClip limp = clip;
    limp.ragdoll.push_back({AnimationClip::RagdollEvent::MotorsOff, 15.0f, "", Vec3(0.0f)});  // (tipped 30 degrees by then: it goes over)
    body::FlightResult fell;
    CHECK(ragdoll::simulate(r.s, *r.rig, limp, b, ragdoll::Settings(), fell, error, &rep));
    const std::vector<Vec3> c = body::com_trajectory(r.s, *r.rig, fell.corrected, b, 0.0f, 60.0f, 61);
    std::printf("    limp from frame 15: the center of mass from %.3f m to %.3f m high; its last step %.5f m\n", c.front().y, c.back().y, length(c[60] - c[59]));
    CHECK(c.back().y < c.front().y - 0.3f && length(c[60] - c[59]) < 2e-3f && c.back().y > 0.0f);
  });
  test("ragdoll: an impulse on a bone moves that bone most; the same clip simulates to the same keys; events are saved with the clip", [&] {
    if (!ragdoll::available()) return (void)std::printf("    skipped (Jolt: Blender's libraries)\n");
    R136Body r;
    const body::Body b = body::build(r.s, *r.rig, 75.0f);
    AnimationClip calm = r140_clip(), hit = r140_clip();
    hit.ragdoll.push_back({AnimationClip::RagdollEvent::Impulse, 10.0f, "Upper", Vec3(0, 0, 150.0f)});
    body::FlightResult a, h, again;
    std::string error;
    CHECK(ragdoll::simulate(r.s, *r.rig, calm, b, ragdoll::Settings(), a, error) && ragdoll::simulate(r.s, *r.rig, hit, b, ragdoll::Settings(), h, error) &&
          ragdoll::simulate(r.s, *r.rig, hit, b, ragdoll::Settings(), again, error));
    /* Each bone's middle in the world two frames after the hit, with the hit and without. */
    auto middles = [&](const body::FlightResult &x) {
      std::vector<Vec3> m;
      body::for_each_frame(r.s, *r.rig, x.corrected, 12, 12, [&](int) {
        for (GameObject *g : {r.lower, r.upper}) m.push_back((g->world_position() + body::bone_tip(*g)) * 0.5f);
      });
      return m;
    };
    const std::vector<Vec3> ma = middles(a), mh = middles(h);
    const float lower = length(mh[0] - ma[0]), upper = length(mh[1] - ma[1]);
    float same = 0;
    for (const AnimTrack &t : h.corrected.tracks)
      if (const AnimTrack *u = again.corrected.find(t.path, t.property, t.axis))
        for (int f = 0; f <= 60; f++) same = std::max(same, std::fabs(t.sample((float)f) - u->sample((float)f)));
    std::printf("    two frames after the hit: Upper's middle moved %.3f m, Lower's %.3f m; run twice: the largest difference %.6f\n", upper, lower, same);
    CHECK(upper > 0.01f && upper > lower && same < 1e-4f);
    AnimationClip back;
    CHECK(load_clip_text(save_clip_text(hit), back) && back.ragdoll.size() == 1 && back.ragdoll[0].bone == "Upper" &&
          std::fabs(back.ragdoll[0].impulse.z - 150.0f) < 1e-4f && back.ragdoll[0].frame == 10.0f);
  });
  test("ragdoll (review): a weaker body follows less; a hit on the hips knocks the body back even at full strength; an absurd impulse leaves finite keys", [&] {
    if (!ragdoll::available()) return (void)std::printf("    skipped (Jolt: Blender's libraries)\n");
    R136Body r;
    const body::Body b = body::build(r.s, *r.rig, 75.0f);
    const AnimationClip clip = r140_clip();
    std::string error;
    float angle[2] = {0, 0};
    for (int k = 0; k < 2; k++) {
      ragdoll::Settings s;
      s.strength = k ? 0.2f : 1.0f;
      body::FlightResult res;
      ragdoll::Report rep;
      CHECK(ragdoll::simulate(r.s, *r.rig, clip, b, s, res, error, &rep));
      angle[k] = rep.max_angle;
    }
    AnimationClip hit = clip;
    hit.ragdoll.push_back({AnimationClip::RagdollEvent::Impulse, 10.0f, "Lower", Vec3(200.0f, 0, 0)});
    body::FlightResult calm, knocked;
    CHECK(ragdoll::simulate(r.s, *r.rig, clip, b, ragdoll::Settings(), calm, error) && ragdoll::simulate(r.s, *r.rig, hit, b, ragdoll::Settings(), knocked, error));
    auto hips_x = [](body::FlightResult &x, float f) {
      const AnimTrack *t = x.corrected.find("Lower", "Position", 0);
      return t ? t->sample(f) : 0.0f;
    };
    const float back = hips_x(knocked, 14.0f) - hips_x(calm, 14.0f);
    AnimationClip wild = clip;
    wild.ragdoll.push_back({AnimationClip::RagdollEvent::Impulse, 5.0f, "Upper", Vec3(1e12f, 0, 0)});
    body::FlightResult w;
    const bool ok = ragdoll::simulate(r.s, *r.rig, wild, b, ragdoll::Settings(), w, error);
    bool finite = true;
    for (const AnimTrack &t : w.corrected.tracks)
      for (const AnimKey &k : t.keys) finite = finite && std::isfinite(k.value);
    std::printf("    the largest difference from the clip: %.1f degrees at full strength, %.1f at 0.2; a hit on the hips moved them %.3f m; an absurd hit: %s, %s keys\n",
                angle[0], angle[1], back, ok ? "simulated" : error.c_str(), finite ? "finite" : "NOT FINITE");
    CHECK(angle[1] > angle[0] + 2.0f && back > 0.02f && finite);
  });
  test("ragdoll in the editor: events by the console, simulated on a worker, shown, Apply one undo step", [] {
    if (!ragdoll::available()) return (void)std::printf("    skipped (Jolt: Blender's libraries)\n");
    Editor ed;
    r34_open(ed);
    Scene &s = ed.scene();
    GameObject *rig = s.create("Rig");
    rig->add<Armature>();
    GameObject *lower = s.create("Lower", rig);
    lower->add<Bone>();
    GameObject *upper = s.create("Upper", lower);
    upper->set_local_position({0, 1, 0});
    upper->add<Bone>()->rest_position = {0, 1, 0};
    ed.mark_changed_for_test("Add the rig");
    ed.command("select Rig");
    ed.command("anim new Fall");
    AnimationClipPtr clip = ed.anim_clip_for_test();
    CHECK(clip != nullptr);
    if (!clip) return;
    clip->frame_rate = 30.0f;
    clip->length = 30.0f;
    clip->track("Lower", "Rotation", 2).set_key(0.0f, 0.0f, AnimLinear);
    clip->touch();
    ed.step_frame_headless();
    ed.command("body ragdoll limp 5");
    ed.step_frame_headless();
    const bool event = ed.anim_clip_for_test() && ed.anim_clip_for_test()->ragdoll.size() == 1;
    ed.command("body ragdoll show");
    const bool started = ed.body_solving_for_test();
    const auto t0 = std::chrono::steady_clock::now();
    while (ed.body_solving_for_test() && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(60)) ed.step_frame_headless();
    const bool shown = ed.body_ghost_for_test();
    ed.command("body ragdoll apply");
    ed.step_frame_headless();
    AnimationClipPtr c2 = ed.anim_clip_for_test();
    const AnimTrack *up = c2 ? c2->find("Lower/Upper", "Rotation", 0) : nullptr;
    ed.command("undo");
    ed.step_frame_headless();
    AnimationClipPtr c3 = ed.anim_clip_for_test();
    const bool undone = c3 && c3->find("Lower/Upper", "Rotation", 0) == nullptr;
    std::printf("    the event in the clip: %s; simulated on a worker: %s; shown: %s; applied: %s; undone: %s\n", event ? "yes" : "no", started ? "yes" : "no",
                shown ? "yes" : "no", up ? "yes" : "no", undone ? "yes" : "no");
    CHECK(event && started && shown && up && undone);
  });
}

static void tests_round139_to_round172() {
  /* ---------------------------------------------------------------- round 139: spring bones (task 0120) */
  /* A bone: its place in its parent, its rest the same, `len` along its +Y. */
  auto r139_bone = [](Scene &s, const std::string &name, GameObject *parent, Vec3 pos, Quat rot = Quat(), float len = 0.1f) {
    GameObject *g = s.create(name, parent);
    g->set_local_position(pos);
    g->set_local_rotation(rot);
    Bone *b = g->add<Bone>();
    b->length = len;
    b->rest_position = pos;
    b->rest_rotation = rot;
    return g;
  };
  /* A run of `n` bones named prefix1..n below `parent`, each `len` long. */
  auto r139_run = [&](Scene &s, const std::string &prefix, GameObject *parent, int n, Vec3 first_pos = Vec3(0, 0.1f, 0), Quat rot = Quat(), float len = 0.1f) {
    GameObject *g = parent;
    for (int i = 1; i <= n; i++) g = r139_bone(s, prefix + std::to_string(i), g, i == 1 ? first_pos : Vec3(0, len, 0), i == 1 ? rot : Quat(), len);
    return g;
  };
  test("spring bones: a bone's name says whether it is the body's or hair, an ear, a tail or cloth (Mixamo, Unity, Rigify, VRM, Japanese rigs)", [] {
    const char *body[] = {"mixamorig:LeftUpLeg", "mixamorig:LeftHandIndex1", "mixamorig:HeadTop_End", "LeftUpperLeg", "DEF-thigh.L", "Spine2",
                          "RightToeBase", "J_Bip_C_Head", "jaw", "Eye_L", "ForeArm.R", "Chest", "Hips", "UpperChest", "mixamorig:Spine1"};
    struct S {
      const char *name;
      int label;
    } sec[] = {{"J_Sec_Hair1_03", 0}, {"Ponytail1", 0}, {"tail.002", 2}, {"Mimi_L", 1}, {"Skirt_Front_01", 3}, {"HairFront", 0}, {"Ear.L.001", 1},
               {"Cape2", 3}, {"shippo_1", 2}, {"kami_back", 0}, {"J_Sec_L_Bust1", 4}, {"Bip001 Ponytail1", 0},
               {"Breast_L", 4}, {"chest_jiggle", 4}, {"Belly", 4}, {"ThighSoft.L", 4}, {"\xe5\xb7\xa6\xe8\x83\xb8", 4},  // (soft tissue, task 0153: 左胸)
               {"\xe5\xb0\xbb\xe5\xb0\xbe", 2}, {"\xe8\x83\xb8\xe3\x83\xaa\xe3\x83\x9c\xe3\x83\xb3", 3}};  // (尻尾 a tail, 胸リボン a ribbon: not soft)
    const char *neither[] = {"Bone.003", "Earring", "Thing", "Detail1"};
    int wrong = 0;
    for (const char *n : body) wrong += !spring::is_body_bone(n) || spring::secondary_label(n) >= 0;
    for (const S &s : sec) wrong += spring::is_body_bone(s.name) || spring::secondary_label(s.name) != s.label;
    for (const char *n : neither) wrong += spring::is_body_bone(n) || spring::secondary_label(n) >= 0;
    std::printf("    %d of %zu names read wrong\n", wrong, sizeof(body) / sizeof(*body) + sizeof(sec) / sizeof(*sec) + sizeof(neither) / sizeof(*neither));
    CHECK(wrong == 0);
  });
  /* A Mixamo-named body with hair (a root and five strands), two ears, a tail, a two-strip skirt, an unnamed extra of two
   * bones and a one-bone helper. */
  struct R139Rig {
    GameObject *root = nullptr, *hips = nullptr, *head = nullptr, *hair_root = nullptr;
  };
  auto r139_rig = [&](Scene &s) {
    R139Rig r;
    r.root = s.create("Character");
    r.root->add<Armature>();
    r.hips = r139_bone(s, "mixamorig:Hips", r.root, {0, 1, 0});
    GameObject *spine = r139_bone(s, "mixamorig:Spine", r.hips, {0, 0.1f, 0});
    GameObject *neck = r139_bone(s, "mixamorig:Neck", spine, {0, 0.3f, 0});
    r.head = r139_bone(s, "mixamorig:Head", neck, {0, 0.1f, 0}, Quat(), 0.2f);
    r139_bone(s, "mixamorig:HeadTop_End", r.head, {0, 0.2f, 0});
    for (const char *side : {"Left", "Right"}) {
      GameObject *leg = r139_bone(s, std::string("mixamorig:") + side + "UpLeg", r.hips, {0, -0.1f, 0}, Quat::euler({180, 0, 0}), 0.45f);
      GameObject *shin = r139_bone(s, std::string("mixamorig:") + side + "Leg", leg, {0, 0.45f, 0}, Quat(), 0.43f);
      GameObject *foot = r139_bone(s, std::string("mixamorig:") + side + "Foot", shin, {0, 0.43f, 0});
      r139_bone(s, std::string("mixamorig:") + side + "ToeBase", foot, {0, 0.1f, 0});
      GameObject *arm = r139_bone(s, std::string("mixamorig:") + side + "Arm", spine, {0, 0.25f, 0}, Quat::euler({0, 0, 90}), 0.3f);
      GameObject *hand = r139_bone(s, std::string("mixamorig:") + side + "Hand", arm, {0, 0.3f, 0});
      r139_run(s, std::string("mixamorig:") + side + "HandIndex", hand, 3);
      r139_run(s, std::string("Ear_") + side + "_", r.head, 2);
    }
    r.hair_root = r139_bone(s, "HairRoot", r.head, {0, 0.15f, 0});
    for (int k = 1; k <= 5; k++) r139_run(s, "Strand" + std::to_string(k) + "_", r.hair_root, 3, {0, 0.02f, 0}, Quat::euler({180, 0, 0}));
    r139_run(s, "Tail", r.hips, 4, {0, 0, -0.1f}, Quat::euler({-150, 0, 0}));
    r139_run(s, "Skirt_F_", r.hips, 2, {0, -0.05f, 0.1f}, Quat::euler({180, 0, 0}));
    r139_run(s, "Skirt_B_", r.hips, 2, {0, -0.05f, -0.1f}, Quat::euler({180, 0, 0}));
    r139_run(s, "Bone.", spine, 2, {0.1f, 0.1f, 0});  // (unnamed, two bones: listed unticked)
    r139_bone(s, "Helper", spine, {-0.1f, 0.1f, 0});   // (unnamed, one bone: not offered)
    return r;
  };
  test("spring bones: Find picks the hair strands, ears, tail and skirt by name (labelled), lists an unnamed extra unticked, and no limb, finger or toe", [&] {
    Scene s;
    const R139Rig r = r139_rig(s);
    const std::vector<spring::Found> found = spring::find(*r.root, false);
    std::string list;
    int named = 0, unnamed = 0, body = 0, labels[4] = {0, 0, 0, 0};
    for (const spring::Found &f : found) {
      list += strprintf("%s(%zu)%s ", f.first->name.c_str(), f.bones, f.named ? spring::kLabels[f.label] : "?");
      if (spring::is_body_bone(f.first->name)) body++;
      if (f.named) named++, labels[f.label]++;
      else unnamed++;
    }
    std::printf("    %s\n", list.c_str());
    CHECK(named == 10 && labels[0] == 5 && labels[1] == 2 && labels[2] == 1 && labels[3] == 2);  // (5 strands, 2 ears, a tail, 2 skirt strips)
    CHECK(unnamed == 1 && body == 0);
    bool strands_whole = true;
    for (const spring::Found &f : found)
      if (f.first->name.rfind("Strand", 0) == 0) strands_whole = strands_whole && f.bones == 3 && f.first->name.find("_1") != std::string::npos;
    CHECK(strands_whole);  // (each strand from its first bone, the hair root shared and left to the animation)
  });
  test("spring bones: from a selected parent - a hair root's five strands are five chains; a body bone under it is left out", [&] {
    Scene s;
    const R139Rig r = r139_rig(s);
    r139_run(s, "mixamorig:Jaw", r.hair_root, 1);  // (a body bone where it shouldn't be)
    const std::vector<GameObject *> chains = spring::chains_under(*r.hair_root);
    int jaw = 0;
    for (GameObject *c : chains) jaw += spring::is_body_bone(c->name);
    std::printf("    %zu chains under the hair root\n", chains.size());
    CHECK(chains.size() == 5 && jaw == 0);
    const std::vector<GameObject *> single = spring::chains_under(*s.find_by_name("Tail1"));
    CHECK(single.size() == 1 && spring::chain_bones(*single[0]).size() == 4);
  });
  /* A hip bone and a three-bone tail hanging straight down from it (rest: nothing to do), on an armature at the origin. */
  struct R139Tail {
    Scene s;
    GameObject *root = nullptr, *hip = nullptr, *tail1 = nullptr, *tip = nullptr;
  };
  auto r139_tail = [&](R139Tail &t, bool down = true) {
    t.root = t.s.create("Rig");
    t.root->add<Armature>();
    t.hip = r139_bone(t.s, "Hips", t.root, {0, 1, 0}, Quat(), 0.1f);
    t.tip = r139_run(t.s, "Tail", t.hip, 3, {0, 0, 0}, down ? Quat::euler({180, 0, 0}) : Quat::euler({-90, 0, 0}), 0.2f);
    t.tail1 = t.s.find_by_name("Tail1");
    spring::add(*t.tail1, 2);
  };
  auto tip_world = [](GameObject *g) { return g->world_matrix().point(Vec3(0, g->get<Bone>()->length, 0)); };
  test("spring bones: a chain at rest stays at rest; a hip that stops makes the tail swing past and settle; the body's bones keep it outside", [&] {
    {
      R139Tail t;
      r139_tail(t);
      const Vec3 before = tip_world(t.tip);
      for (int f = 0; f < 120; f++) spring::step(t.s, 1.0f / 60.0f);
      std::printf("    at rest: the tip moved %.6f m in 2 s\n", length(tip_world(t.tip) - before));
      CHECK(length(tip_world(t.tip) - before) < 1e-4f);
    }
    {
      R139Tail t;
      r139_tail(t);
      float lag = 0, past = 0, last = 0;
      for (int f = 0; f < 300; f++) {  // (the hips go +x at 2 m/s for half a second, then stop)
        const float x = std::min(f, 30) * (2.0f / 60.0f);
        t.hip->set_local_position({x, 1, 0});
        spring::step(t.s, 1.0f / 60.0f);
        const float rel = tip_world(t.tip).x - x;
        if (f < 30) lag = std::min(lag, rel);
        else past = std::max(past, rel);
        last = rel;
      }
      std::printf("    moving: the tip %.3f m behind; stopped: %.3f m past, then %.5f m after 4 s\n", -lag, past, last);
      CHECK(lag < -0.01f && past > 0.01f && std::fabs(last) < 2e-3f);  // (a tail's stiffness: centimetres at 2 m/s, then still)
    }
    for (bool collide : {true, false}) {  // (a thigh across where the tail hangs: kept out of it, or not)
      R139Tail t;
      r139_tail(t);
      GameObject *thigh = r139_bone(t.s, "LeftUpLeg", t.hip, {-0.3f, -0.35f, 0.03f}, Quat::euler({0, 0, -90}), 0.6f);  // (a little in front)
      (void)thigh;
      t.tail1->get<SpringBone>()->collide = collide;
      for (int f = 0; f < 240; f++) spring::step(t.s, 1.0f / 60.0f);
      const Vec3 p = tip_world(t.s.find_by_name("Tail2"));
      const float to_axis = std::sqrt((p.z - 0.03f) * (p.z - 0.03f) + (p.y - 0.65f) * (p.y - 0.65f));  // (from the thigh's line, x across)
      std::printf("    collide %s: the second bone's tip %.3f m from the thigh's axis (its radius %.3f + the chain's %.3f)\n", collide ? "on" : "off", to_axis,
                  0.6f / 7.0f, t.tail1->get<SpringBone>()->radius);
      if (collide) CHECK(to_axis >= 0.6f / 7.0f + t.tail1->get<SpringBone>()->radius - 5e-3f);
      else CHECK(to_axis < 0.6f / 7.0f);
    }
  });
  test("spring bones: Bake keys each chain bone from the clip, the same keys when baked again; the chain's own keys set aside", [&] {
    R139Tail t;
    r139_tail(t);
    AnimationClip clip;
    clip.frame_rate = 30.0f;
    clip.length = 40.0f;
    for (int f = 0; f <= 40; f++) clip.track("Hips", "Position", 0).set_key((float)f, f < 15 ? f * 0.06f : 0.9f, AnimLinear);
    AnimationClip once, twice;
    const size_t k1 = spring::bake(t.s, *t.root, clip, once), k2 = spring::bake(t.s, *t.root, once, twice);
    float diff = 0, swing = 0;
    for (const AnimTrack &a : once.tracks) {
      const AnimTrack *b = twice.find(a.path, a.property, a.axis);
      for (int f = 0; f <= 40; f++) {
        diff = std::max(diff, b ? std::fabs(a.sample((float)f) - b->sample((float)f)) : 1e9f);
        if (a.property == "Rotation" && a.path == "Hips/Tail1") swing = std::max(swing, std::fabs(a.sample((float)f) - a.sample(0.0f)));
      }
    }
    std::printf("    %zu keys, then %zu; the largest difference %.6f degrees; Tail1 swung up to %.1f degrees; the scene back: %s\n", k1, k2, diff, swing,
                length(t.hip->local().position - Vec3(0, 1, 0)) < 1e-6f ? "yes" : "NO");
    CHECK(k1 == 3 * 3 * 41 && k1 == k2 && diff < 1e-3f && swing > 5.0f);
    CHECK(length(t.hip->local().position - Vec3(0, 1, 0)) < 1e-6f);
  });
  test("spring bones in the editor: Find, untick, None, Apply; an ignored chain isn't offered again until Unignore; Remove on a parent is one undo step", [&] {
    Editor ed;
    r34_open(ed);
    r139_rig(ed.scene());
    ed.mark_changed_for_test("Add the rig");
    ed.command("select mixamorig:Hips");
    ed.command("spring find");
    ed.command("spring tick Skirt_B_1 off");
    ed.command("spring label Ear_Right_1 none");
    ed.command("spring label Tail1 cloth");
    ed.command("spring apply");
    ed.step_frame_headless();  // (each edit its own undo step, as frames make them)
    auto has = [&](const char *n) { GameObject *g = ed.scene().find_by_name(n); return g && g->get<SpringBone>() != nullptr; };  // (undo makes a new scene)
    Scene &s = ed.scene();
    const bool picked = has("Strand1_1") && has("Strand5_1") && has("Ear_Left_1") && has("Skirt_F_1") && has("Tail1");
    const bool left = !has("Skirt_B_1") && !has("Ear_Right_1") && !has("Bone.1") && !has("mixamorig:LeftHandIndex1");
    const SpringBone *tail = s.find_by_name("Tail1") ? s.find_by_name("Tail1")->get<SpringBone>() : nullptr;
    const bool cloth = tail && tail->label == 3 && std::fabs(tail->stiffness - 0.1f) < 1e-6f;
    ed.command("spring find");
    bool ear_offered = false;
    for (const spring::Found &f : spring::find(*s.find_by_name("Character"), false)) ear_offered = ear_offered || f.first->name == "Ear_Right_1";
    ed.command("spring unignore Ear_Right_1");
    ed.step_frame_headless();
    bool ear_back = false;
    for (const spring::Found &f : spring::find(*s.find_by_name("Character"), false)) ear_back = ear_back || f.first->name == "Ear_Right_1";
    std::printf("    applied to the ticked: %s; unticked / None / unnamed / fingers left: %s; Tail relabelled Cloth: %s; the None ear offered again: %s; "
                "after Unignore: %s\n", picked ? "yes" : "no", left ? "yes" : "no", cloth ? "yes" : "no", ear_offered ? "YES" : "no", ear_back ? "yes" : "no");
    CHECK(picked && left && cloth && !ear_offered && ear_back);
    /* Remove on the hair root: its five strands at once, one undo step, undone whole. */
    ed.command("spring remove HairRoot");
    ed.step_frame_headless();
    int strands = 0;
    for (int k = 1; k <= 5; k++) strands += has(("Strand" + std::to_string(k) + "_1").c_str());
    ed.command("undo");
    int back = 0;
    for (int k = 1; k <= 5; k++) back += has(("Strand" + std::to_string(k) + "_1").c_str());
    std::printf("    Remove on the hair root: %d strands left with springs; after one undo: %d\n", strands, back);
    CHECK(strands == 0 && back == 5);
  });
  test("spring bones in Play: stepped by the editor after IK; a disabled one (baked) stays as posed; Find's list survives Play and Stop", [&] {
    for (bool enabled : {true, false}) {
      Editor ed;
      r34_open(ed);
      Scene &s0 = ed.scene();
      GameObject *root = s0.create("Rig");
      root->add<Armature>();
      GameObject *hip = r139_bone(s0, "Hips", root, {0, 1, 0});
      r139_run(s0, "Tail", hip, 3, {0, 0, 0}, Quat::euler({180, 0, 0}), 0.2f);
      spring::add(*s0.find_by_name("Tail1"), 2);
      s0.find_by_name("Tail1")->get<SpringBone>()->enabled = enabled;
      ed.mark_changed_for_test("Add the rig");
      ed.command("select Hips");
      ed.command("spring find all");
      ed.command("play");
      float lag = 0;
      for (int f = 0; f < 40; f++) {
        Scene &s = ed.scene();  // (Play's own copy)
        GameObject *h = s.find_by_name("Hips"), *tip = s.find_by_name("Tail3");
        h->set_local_position({f * 0.05f, 1, 0});  // (5 cm a frame: a clear lag whatever the frame time, a sanitizer build's too)
        std::this_thread::sleep_for(std::chrono::milliseconds(4));  // (Play's step follows the clock)
        ed.step_frame_headless();
        lag = std::min(lag, tip_world(tip).x - f * 0.05f);
      }
      ed.command("stop");
      ed.command("spring apply");  // (the list from before Play: its bones looked up again, nothing stale)
      ed.step_frame_headless();
      std::printf("    %s: the tail's tip %.4f m behind the hips in Play\n", enabled ? "live" : "disabled", -lag);
      CHECK(enabled ? lag < -0.003f : std::fabs(lag) < 1e-5f);
    }
  });
  test("spring bones: the same swing at 30, 60 and 144 frames a second; a teleport starts the chain afresh instead of whipping it", [&] {
    float past[3] = {0, 0, 0};
    const float rates[3] = {30.0f, 60.0f, 144.0f};
    for (int k = 0; k < 3; k++) {
      R139Tail t;
      r139_tail(t);
      const float dt = 1.0f / rates[k];
      for (float time = 0; time < 3.0f; time += dt) {
        const float x = std::min(time, 0.5f) * 2.0f;
        t.hip->set_local_position({x, 1, 0});
        spring::step(t.s, dt);
        if (time > 0.5f) past[k] = std::max(past[k], tip_world(t.tip).x - x);
      }
    }
    std::printf("    swung past: %.4f m at 30, %.4f at 60, %.4f at 144 frames a second\n", past[0], past[1], past[2]);
    CHECK(past[1] > 0.005f && std::fabs(past[0] - past[1]) < 0.25f * past[1] && std::fabs(past[2] - past[1]) < 0.25f * past[1]);
    R139Tail t;
    r139_tail(t);
    spring::step(t.s, 1.0f / 60.0f);
    t.hip->set_local_position({50, 1, 0});  // (moved 50 m in a frame)
    spring::step(t.s, 1.0f / 60.0f);
    const Vec3 hang = tip_world(t.tip) - t.hip->world_position();
    std::printf("    after a 50 m jump: the tip %.4f m from straight below the hips\n", std::sqrt(hang.x * hang.x + hang.z * hang.z));
    CHECK(std::sqrt(hang.x * hang.x + hang.z * hang.z) < 1e-3f);
  });
  test("spring bones (review): Add on a strand's middle bone takes the whole strand; Add again keeps tuning and None; None takes off only its own chain", [&] {
    Editor ed;
    r34_open(ed);
    r139_rig(ed.scene());
    r139_run(ed.scene(), "Sub", ed.scene().find_by_name("Tail4"), 2);  // (a chain below the tail's: Tail1..4 then Sub1..2)
    ed.mark_changed_for_test("Add the rig");
    ed.command("select mixamorig:Hips");
    ed.command("spring add Strand2_2");
    ed.step_frame_headless();
    Scene &s = ed.scene();
    auto sb = [&](const char *n) { GameObject *g = ed.scene().find_by_name(n); return g ? g->get<SpringBone>() : nullptr; };
    const bool whole = sb("Strand2_1") && !sb("Strand2_2") && spring::chain_bones(*s.find_by_name("Strand2_1")).size() == 3;
    sb("Strand2_1")->stiffness = 0.77f;
    ed.command("spring label Strand4_1 none");
    ed.step_frame_headless();
    ed.command("spring add HairRoot");
    ed.step_frame_headless();
    const bool kept = sb("Strand2_1") && std::fabs(sb("Strand2_1")->stiffness - 0.77f) < 1e-6f && !sb("Strand4_1") && sb("Strand1_1") && sb("Strand5_1");
    ed.command("spring add Tail1");
    ed.step_frame_headless();
    const bool both = sb("Tail1") && sb("Tail4") == nullptr && spring::chain_bones(*s.find_by_name("Tail1")).size() == 6;
    std::printf("    a middle bone: the whole strand (%s); Add again kept tuning and None (%s); the tail one chain of 6: %s\n", whole ? "yes" : "no",
                kept ? "yes" : "no", both ? "yes" : "no");
    CHECK(whole && kept && both);
    /* None on a chain with another below a fork: only its own. */
    r139_run(ed.scene(), "BranchA", ed.scene().find_by_name("Skirt_F_2"), 2);
    r139_run(ed.scene(), "BranchB", ed.scene().find_by_name("Skirt_F_2"), 2);
    ed.command("spring add Skirt_F_1");
    ed.step_frame_headless();
    const bool three = sb("Skirt_F_1") && sb("BranchA1") && sb("BranchB1");
    ed.command("spring label Skirt_F_1 none");
    ed.step_frame_headless();
    CHECK(three && !sb("Skirt_F_1") && sb("BranchA1") && sb("BranchB1"));
  });
  test("spring bones: a label and its settings survive saving and loading; the ignored mark too", [&] {
    Scene s;
    r139_rig(s);
    spring::add(*s.find_by_name("Tail1"), 2);
    s.find_by_name("Tail1")->get<SpringBone>()->stiffness = 0.42f;
    s.find_by_name("Ear_Left_1")->get<Bone>()->spring_ignored = true;
    const std::string path = fs::join(fs::temp_dir(), "blendity-test-springs.scene");
    CHECK(save_scene(s, path));
    Scene back;
    std::string error;
    CHECK(load_scene(path, back, error));
    fs::remove_file(path);
    GameObject *tail = back.find_by_name("Tail1"), *ear = back.find_by_name("Ear_Left_1");
    const SpringBone *sb = tail ? tail->get<SpringBone>() : nullptr;
    CHECK(sb && sb->label == 2 && std::fabs(sb->stiffness - 0.42f) < 1e-6f && ear && ear->get<Bone>()->spring_ignored);
  });
  /* ---------------------------------------------------------------- round 144: animation assistants and the rig read for walking (task 0124) */
  test("assistants: the physics tools on and the rest off at first; a switched-off one does nothing and says how to turn it on; kept in the preferences", [] {
    const std::string prefs = fs::join(test_dir(), "r144_prefs.txt");
    fs::remove_file(prefs);
    {
      Editor ed;
      ed.init_headless(1200, 800);
      ed.prefs_for_test(prefs);
      const bool first = ed.assist_on(Editor::Assist::Physics) && !ed.assist_on(Editor::Assist::Walk) && !ed.assist_on(Editor::Assist::Footsteps) &&
                         !ed.assist_on(Editor::Assist::Locomotion) && !ed.assist_on(Editor::Assist::Ai);
      ed.command("assist off physics");
      ed.command("body show on");     // (refused: off)
      ed.command("autopose on");      // (refused: off)
      const bool refused = !ed.body_show_com_for_test() && !ed.autopose_on_for_test();
      for (int f = 0; f < 3; f++) ed.step_frame_headless();  // (the windows draw without them: no crash)
      ed.command("assist on walk");
      ed.command("assist on nonsense");  // (warned, nothing changes)
      ed.save_prefs_for_test();
      std::printf("    at first: physics on, the rest off: %s; physics off refuses body / autopose: %s\n", first ? "yes" : "no", refused ? "yes" : "no");
      CHECK(first && refused);
    }
    {
      Editor ed;
      ed.init_headless(1200, 800);
      ed.prefs_for_test(prefs);
      ed.load_prefs_for_test();
      CHECK(!ed.assist_on(Editor::Assist::Physics) && ed.assist_on(Editor::Assist::Walk) && !ed.assist_on(Editor::Assist::Ai));
      ed.command("assist on physics");
      ed.command("body show on");
      CHECK(ed.assist_on(Editor::Assist::Physics) && ed.body_show_com_for_test());
    }
    fs::remove_file(prefs);
  });
  test("gait: the humanoid and quadruped rigs read for walking - legs, feet, sides, IK controls, hips, spine, head and measures", [] {
    {
      Scene s;
      GameObject *rig = create_rig(s, false, Vec3(0.0f));
      const gait::Rig r = gait::analyse(s, *rig);
      std::printf("    humanoid: %s; %zu legs (%s %s), %zu arms, hips %s, head %s, %zu spine bones; hip height %.3f, leg %.3f, stance %.3f\n",
                  r.valid() ? "ok" : r.problem.c_str(), r.legs.size(), r.legs.size() > 0 ? r.legs[0].upper->name.c_str() : "-",
                  r.legs.size() > 1 ? r.legs[1].upper->name.c_str() : "-", r.arms.size(), r.hips ? r.hips->name.c_str() : "-",
                  r.head ? r.head->name.c_str() : "-", r.spine.size(), r.hip_height, r.leg_length, r.stance_width);
      CHECK(r.valid() && !r.quadruped && r.legs.size() == 2 && r.arms.size() == 2);
      CHECK(r.legs[0].side == 0 && r.legs[1].side == 1 && r.legs[0].foot && r.legs[0].foot->name == "LeftFoot" && r.legs[0].ik_target &&
            r.legs[0].ik_target->name == "LeftFootIK");
      CHECK(r.hips && r.hips->name == "Hips" && r.head && r.head->name == "Head" && r.spine.size() == 3);
      CHECK(std::fabs(r.leg_length - 0.88f) < 0.02f && r.hip_height > 0.85f && r.stance_width > 0.15f);
    }
    {
      Scene s;
      GameObject *rig = create_rig(s, true, Vec3(0.0f));
      const gait::Rig r = gait::analyse(s, *rig);
      int front = 0;
      for (const gait::Leg &l : r.legs) front += l.front;
      std::printf("    quadruped: %s; %zu legs, %d front, first %s\n", r.valid() ? "ok" : r.problem.c_str(), r.legs.size(), front,
                  r.legs.empty() ? "-" : r.legs[0].upper->name.c_str());
      CHECK(r.valid() && r.quadruped && r.legs.size() == 4 && front == 2 && r.legs[0].front && r.legs[0].upper->name.find("Front") != std::string::npos);
    }
  });
  test("gait: Mixamo and Rigify names, an unnamed stick figure by its shape; a rig without legs or with one is refused with why", [&] {
    auto stick = [&](Scene &s, const std::vector<std::string> &names, bool second_leg = true) {
      /* hips at 1 m, a spine and head, legs (upper, lower, foot) and arms (upper, lower, hand) - named as given */
      GameObject *arm = s.create("Rig");
      arm->add<Armature>();
      const Quat down = Quat::euler({180, 0, 0}), out_l = Quat::euler({0, 0, 90}), out_r = Quat::euler({0, 0, -90});
      GameObject *hips = r139_bone(s, names[0], arm, {0, 1, 0}, Quat(), 0.1f);
      GameObject *spine = r139_bone(s, names[1], hips, {0, 0.1f, 0}, Quat(), 0.3f);
      r139_bone(s, names[2], spine, {0, 0.3f, 0}, Quat(), 0.2f);
      for (int side = 0; side < (second_leg ? 2 : 1); side++) {
        const float x = side ? 0.1f : -0.1f;
        GameObject *up = r139_bone(s, names[3 + side * 3], hips, {x, 0, 0}, down, 0.45f);
        GameObject *lo = r139_bone(s, names[4 + side * 3], up, {0, 0.45f, 0}, Quat(), 0.43f);
        r139_bone(s, names[5 + side * 3], lo, {0, 0.43f, 0}, Quat::euler({-90, 0, 0}), 0.12f);
        GameObject *ua = r139_bone(s, names[9 + side * 3], spine, {x * 1.5f, 0.25f, 0}, side ? out_r : out_l, 0.28f);
        GameObject *la = r139_bone(s, names[10 + side * 3], ua, {0, 0.28f, 0}, Quat(), 0.25f);
        r139_bone(s, names[11 + side * 3], la, {0, 0.25f, 0}, Quat(), 0.08f);
      }
      return arm;
    };
    const std::vector<std::string> mixamo = {"mixamorig:Hips", "mixamorig:Spine", "mixamorig:Head", "mixamorig:LeftUpLeg", "mixamorig:LeftLeg",
                                             "mixamorig:LeftFoot", "mixamorig:RightUpLeg", "mixamorig:RightLeg", "mixamorig:RightFoot",
                                             "mixamorig:LeftArm", "mixamorig:LeftForeArm", "mixamorig:LeftHand", "mixamorig:RightArm",
                                             "mixamorig:RightForeArm", "mixamorig:RightHand"};
    const std::vector<std::string> rigify = {"spine", "spine.001", "head", "thigh.L", "shin.L", "foot.L", "thigh.R", "shin.R", "foot.R",
                                             "upper_arm.L", "forearm.L", "hand.L", "upper_arm.R", "forearm.R", "hand.R"};
    std::vector<std::string> unnamed;
    for (int i = 0; i < 15; i++) unnamed.push_back(i ? strprintf("Bone.%03d", i) : std::string("Bone"));
    int ok = 0;
    const std::vector<std::string> *sets[3] = {&mixamo, &rigify, &unnamed};
    for (const std::vector<std::string> *names : sets) {
      Scene s;
      GameObject *arm = stick(s, *names);
      const gait::Rig r = gait::analyse(s, *arm);
      const bool legs = r.valid() && r.legs.size() == 2 && r.legs[0].side == 0 && r.legs[1].side == 1 && r.legs[0].foot && r.hips &&
                        r.hips->name == (*names)[0];
      std::printf("    %-16s %s: %zu legs, %zu arms, hips %s, left foot %s\n", (*names)[3].c_str(), r.valid() ? "ok" : r.problem.c_str(), r.legs.size(),
                  r.arms.size(), r.hips ? r.hips->name.c_str() : "-", r.legs.empty() || !r.legs[0].foot ? "-" : r.legs[0].foot->name.c_str());
      ok += legs;
    }
    CHECK(ok == 3);
    Scene one;
    const gait::Rig r1 = gait::analyse(one, *stick(one, mixamo, false));
    Scene none;
    GameObject *bare = none.create("Rig");
    bare->add<Armature>();
    r139_bone(none, "Lonely", bare, {0, 1, 0});
    const gait::Rig r0 = gait::analyse(none, *bare);
    std::printf("    one leg: '%s'; no legs: '%s'\n", r1.problem.c_str(), r0.problem.c_str());
    CHECK(!r1.valid() && !r0.valid() && !r1.problem.empty() && !r0.problem.empty());
  });
  /* ---------------------------------------------------------------- round 171: strands - bend, root to tip, wind, fitted colliders, separation (task 0153, ADR 0049) */
  /* A chain's tip after `frames` steps, the hips moving +x at `speed` for the first `moving` frames; the chain's spring
   * set up by `tune` first. */
  auto r171_tip_after = [&](R139Tail &t, const std::function<void(SpringBone &)> &tune, int frames, int moving, float speed) {
    r139_tail(t);
    tune(*t.tail1->get<SpringBone>());
    Vec3 tip(0.0f);
    for (int f = 0; f < frames; f++) {
      const float x = std::min(f, moving) * (speed / 60.0f);
      t.hip->set_local_position({x, 1, 0});
      spring::step(t.s, 1.0f / 60.0f);
      tip = tip_world(t.tip);
    }
    return tip;
  };
  test("strands: Bend holds a curled chain's shape under gravity while 0 lets it hang straight; Root To Tip frees the tip while the root keeps following", [&] {
    /* A chain curled 40 degrees at each joint: with bend 1 and no pull it keeps the curl (its joints' angles: with a free
     * root the curl may turn about it until its weight hangs below, task 0168); with bend 0 it hangs straight. */
    float joint[2] = {0, 0};
    auto curled = [&](float bend, Vec3 &curled_tip, Vec3 &straight_tip) {
      R139Tail t;
      r139_tail(t);
      for (const char *n : {"Tail2", "Tail3"}) t.s.find_by_name(n)->set_local_rotation(Quat::euler({40.0f, 0.0f, 0.0f}));
      curled_tip = tip_world(t.tip);
      straight_tip = t.hip->world_position() + Vec3(0.0f, -0.6f, 0.0f);
      SpringBone &sb = *t.tail1->get<SpringBone>();
      sb.stiffness = 0.0f, sb.bend = bend, sb.damping = 0.3f, sb.gravity = 1.0f, sb.collide = false;
      for (int f = 0; f < 240; f++) spring::step(t.s, 1.0f / 60.0f);
      const Vec3 p[4] = {t.tail1->world_position(), tip_world(t.tail1), tip_world(t.s.find_by_name("Tail2")), tip_world(t.tip)};
      for (int i = 0; i < 2; i++)
        joint[i] = std::acos(std::max(-1.0f, std::min(1.0f, dot(normalize(p[i + 1] - p[i]), normalize(p[i + 2] - p[i + 1]))))) * 180.0f / kPi;
      return tip_world(t.tip);
    };
    Vec3 ct, st;
    curled(1.0f, ct, st);
    const float held0 = joint[0], held1 = joint[1];
    const Vec3 hung = curled(0.0f, ct, st);
    const float apart = length(ct - st);
    std::printf("    curled chain: bend 1 keeps its joints at %.1f and %.1f degrees (the curl's 40), bend 0 ends %.3f m from straight down (the curl's tip %.3f m from it)\n",
                held0, held1, length(hung - st), apart);
    CHECK(apart > 0.1f && std::fabs(held0 - 40.0f) < 4.0f && std::fabs(held1 - 40.0f) < 4.0f && length(hung - st) < 0.02f * 0.6f);
    /* Root To Tip: the tip lags more with 1 than with 0 under the same move; the first bone's tip lags alike. */
    auto lag_of = [&](float r2t, float &first_bone_lag) {
      R139Tail t;
      r139_tail(t);
      SpringBone &sb = *t.tail1->get<SpringBone>();
      sb.stiffness = 0.5f, sb.root_to_tip = r2t, sb.bend = 0.0f, sb.collide = false;
      float lag = 0.0f;
      first_bone_lag = 0.0f;
      for (int f = 0; f < 30; f++) {
        const float x = f * (2.0f / 60.0f);
        t.hip->set_local_position({x, 1, 0});
        spring::step(t.s, 1.0f / 60.0f);
        lag = std::max(lag, x - tip_world(t.tip).x);
        first_bone_lag = std::max(first_bone_lag, x - tip_world(t.tail1).x);
      }
      return lag;
    };
    float fb0 = 0.0f, fb1 = 0.0f;
    const float lag0 = lag_of(0.0f, fb0), lag1 = lag_of(1.0f, fb1);
    std::printf("    root to tip 0: the tip lags %.3f m (the first bone %.3f); 1: %.3f m (the first bone %.3f)\n", lag0, fb0, lag1, fb1);
    CHECK(lag1 > lag0 * 1.2f && std::fabs(fb1 - fb0) < 0.15f * std::max(fb0, 1e-3f));
  });

  test("strands: a Wind deflects a hanging chain by its strength and the chain's Wind, turbulence makes it stir, and a bake with wind is the same twice", [&] {
    auto deflection = [&](float strength, float chain_wind, float turbulence, float *stir) {
      R139Tail t;
      r139_tail(t);
      GameObject *w = t.s.create("Breeze");
      Wind *wind = w->add<Wind>();
      wind->strength = strength, wind->turbulence = turbulence, wind->gusts = 0.0f;
      w->set_local_rotation(Quat::euler({0.0f, 90.0f, 0.0f}));  // (forward +Z turned to +X)
      SpringBone &sb = *t.tail1->get<SpringBone>();
      sb.wind = chain_wind, sb.stiffness = 0.1f, sb.collide = false;
      spring::reset(t.s);
      std::vector<float> xs;
      for (int f = 0; f < 240; f++) {
        spring::step(t.s, 1.0f / 60.0f);
        if (f >= 120) xs.push_back(tip_world(t.tip).x);
      }
      double mean = 0.0, var = 0.0;
      for (float x : xs) mean += x;
      mean /= std::max<size_t>(1, xs.size());
      for (float x : xs) var += (x - mean) * (x - mean);
      if (stir) *stir = (float)std::sqrt(var / std::max<size_t>(1, xs.size()));
      return (float)mean;
    };
    float stir_steady = 0.0f, stir_turbulent = 0.0f;
    const float blown = deflection(5.0f, 1.0f, 0.0f, &stir_steady), calm = deflection(0.0f, 1.0f, 0.0f, nullptr), deaf = deflection(5.0f, 0.0f, 0.0f, nullptr),
                stirred = deflection(5.0f, 1.0f, 1.0f, &stir_turbulent);
    std::printf("    5 m/s: the tip %.3f m downwind (steady, stirring %.4f m); 0 m/s %.4f m; the chain's Wind 0 %.4f m; turbulent %.3f m, stirring %.4f m\n", blown, stir_steady, calm, deaf,
                stirred, stir_turbulent);
    CHECK(blown > 0.05f && std::fabs(calm) < 0.005f && std::fabs(deaf) < 0.005f && stir_turbulent > 0.001f && stir_turbulent > 3.0f * stir_steady);
    /* A bake under the wind: the same keys twice (the wind's time is the simulated time, reset by the bake). */
    R139Tail t;
    r139_tail(t);
    GameObject *w = t.s.create("Breeze");
    Wind *wind = w->add<Wind>();
    wind->strength = 4.0f, wind->turbulence = 0.8f, wind->gusts = 0.5f;
    AnimationClip clip;
    clip.frame_rate = 60.0f;
    clip.track("Hips", "Position", 0).set_key(0.0f, 0.0f, AnimLinear);
    clip.track("Hips", "Position", 0).set_key(60.0f, 1.0f, AnimLinear);
    AnimationClip a, b;
    const size_t ka = spring::bake(t.s, *t.root, clip, a), kb = spring::bake(t.s, *t.root, clip, b);
    bool same = ka == kb && a.tracks.size() == b.tracks.size();
    for (size_t i = 0; same && i < a.tracks.size(); i++) {
      same = a.tracks[i].keys.size() == b.tracks[i].keys.size();
      for (size_t k = 0; same && k < a.tracks[i].keys.size(); k++) same = a.tracks[i].keys[k].value == b.tracks[i].keys[k].value;
    }
    std::printf("    baked twice under the wind: %zu and %zu keys, %s\n", ka, kb, same ? "the same" : "DIFFERENT");
    CHECK(ka > 0 && same);
  });

  test("strands: a body bone's collider is as wide as the skin it moves (a seventh of the length without one), and a chain hanging into the skin rests outside it", [&] {
    Scene s;
    GameObject *root = s.create("Rig");
    root->add<Armature>();
    GameObject *tube_bone = r139_bone(s, "Tube", root, {0, 0, 0}, Quat(), 1.0f);  // (a metre up the Y axis)
    const float without = spring::body_colliders(*root).empty() ? -1.0f : spring::body_colliders(*root)[0].r;
    /* A tube of radius 0.2 m around it, skinned to it. */
    MeshPtr tube = std::make_shared<Mesh>();
    const int rings = 11, seg = 16;
    for (int r = 0; r < rings; r++)
      for (int k = 0; k < seg; k++) {
        const float a = k * 2.0f * kPi / seg;
        tube->add_vert({0.2f * std::cos(a), r * 0.1f, 0.2f * std::sin(a)});
      }
    for (int r = 0; r + 1 < rings; r++)
      for (int k = 0; k < seg; k++) {
        const uint32_t a = (uint32_t)(r * seg + k), b = (uint32_t)(r * seg + (k + 1) % seg);
        tube->add_face({a, a + seg, b + seg, b});
      }
    tube->vertex_groups.push_back({"Tube", std::vector<float>(tube->vert_count(), 1.0f)});
    tube->touch();
    GameObject *skin = s.create("Skin");
    skin->add<MeshFilter>()->mesh = tube;
    skin->add<SkinModifier>()->object = "Rig";
    float fitted = -1.0f;
    for (const spring::Collider &c : spring::body_colliders(*root)) fitted = c.r;
    std::printf("    the Tube bone's collider: %.3f m without a skin (a seventh), %.3f m fitted to the 0.2 m tube\n", without, fitted);
    CHECK(std::fabs(without - 1.0f / 7.0f) < 1e-3f && std::fabs(fitted - 0.2f) < 0.02f);
    /* A chain from a point 0.1 m off the axis, hanging down into the tube: kept outside the fitted radius. */
    GameObject *hip = r139_bone(s, "Hips", root, {0.1f, 1.3f, 0.0f}, Quat(), 0.05f);
    GameObject *tip = r139_run(s, "Hair", hip, 3, {0, 0, 0}, Quat::euler({180, 0, 0}), 0.2f);
    GameObject *hair1 = s.find_by_name("Hair1");
    spring::add(*hair1, 0);
    hair1->get<SpringBone>()->radius = 0.01f;
    hair1->get<SpringBone>()->separate = false;
    for (int f = 0; f < 240; f++) spring::step(s, 1.0f / 60.0f);
    const Vec3 p = tip_world(tip);
    const float off_axis = std::sqrt(p.x * p.x + p.z * p.z);
    std::printf("    the hair's tip %.3f m from the tube's axis (the tube 0.200 + the chain's 0.010)\n", off_axis);
    CHECK(off_axis >= 0.2f + 0.01f - 5e-3f);
    (void)tube_bone;
  });

  test("strands: Separate keeps two chains apart by their radii, the same hang at 30, 60 and 144 frames a second with bend, wind and separation on, and no NaN after a thousand jerks", [&] {
    /* Two strands 1 cm apart, radius 2 cm each: strand A's animation leans 30 degrees across B (lean), or both hang
     * straight as laid out (no lean). With no stiffness both fall straight, 1 cm apart; Separate keeps them their
     * radii apart only where the animation itself has them further apart than that. */
    auto apart_after = [&](bool separate, bool lean, float stiffness) {
      Scene s;
      GameObject *root = s.create("Rig");
      root->add<Armature>();
      GameObject *tips[2];
      for (int k = 0; k < 2; k++) {
        GameObject *hip = r139_bone(s, k ? "HipsB" : "HipsA", root, {k * 0.01f, 1.0f, 0.0f}, Quat(), 0.05f);
        tips[k] = r139_run(s, k ? "StrandB" : "StrandA", hip, 3, {0, 0, 0}, Quat::euler({lean && k == 0 ? 150.0f : 180.0f, 0, 0}), 0.2f);
        GameObject *first = s.find_by_name(k ? "StrandB1" : "StrandA1");
        spring::add(*first, 0);
        SpringBone &sb = *first->get<SpringBone>();
        sb.radius = 0.02f, sb.separate = separate, sb.collide = false, sb.stiffness = stiffness, sb.bend = 0.0f;
      }
      for (int f = 0; f < 120; f++) spring::step(s, 1.0f / 60.0f);
      return length(tip_world(tips[0]) - tip_world(tips[1]));
    };
    const float kept = apart_after(true, true, 0.0f), free = apart_after(false, true, 0.0f), laid = apart_after(true, false, 1.0f);
    std::printf("    two strands 1 cm apart with radius 2 cm, one leaning across the other: %.3f m apart with Separate, %.3f m without; laid out straight: %.3f m\n", kept, free, laid);
    CHECK(kept >= 0.035f && free < 0.015f && laid < 0.015f);
    /* The frame rates. */
    float past[3] = {0, 0, 0};
    const float rates[3] = {30.0f, 60.0f, 144.0f};
    for (int k = 0; k < 3; k++) {
      R139Tail t;
      r139_tail(t);
      GameObject *w = t.s.create("Breeze");
      w->add<Wind>()->strength = 2.0f;
      SpringBone &sb = *t.tail1->get<SpringBone>();
      sb.bend = 0.5f, sb.root_to_tip = 0.5f, sb.separate = true;
      spring::reset(t.s);
      const float dt = 1.0f / rates[k];
      for (float time = 0; time < 3.0f; time += dt) {
        const float x = std::min(time, 0.5f) * 2.0f;
        t.hip->set_local_position({x, 1, 0});
        spring::step(t.s, dt);
        if (time > 0.5f) past[k] = std::max(past[k], tip_world(t.tip).x - x);
      }
    }
    std::printf("    swung past: %.4f m at 30, %.4f at 60, %.4f at 144 frames a second\n", past[0], past[1], past[2]);
    CHECK(past[1] > 0.005f && std::fabs(past[0] - past[1]) < 0.25f * past[1] && std::fabs(past[2] - past[1]) < 0.25f * past[1]);
    /* A thousand jerks of up to 10 m under a wind: every tip finite. */
    R139Tail t;
    r139_tail(t);
    t.s.create("Breeze")->add<Wind>()->strength = 6.0f;
    SpringBone &sb = *t.tail1->get<SpringBone>();
    sb.bend = 0.7f, sb.separate = true;
    bool finite = true;
    for (int f = 0; f < 1000 && finite; f++) {
      const Vec3 p((texture_rand((uint32_t)f, 1u, 171u) - 0.5f) * 20.0f, 1.0f + (texture_rand((uint32_t)f, 2u, 171u) - 0.5f) * 20.0f, (texture_rand((uint32_t)f, 3u, 171u) - 0.5f) * 20.0f);
      t.hip->set_local_position(p);
      spring::step(t.s, 1.0f / 60.0f);
      const Vec3 q = tip_world(t.tip);
      finite = std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z);
    }
    CHECK(finite);
    /* The settings and a Wind survive saving and loading. */
    sb.wind = 0.5f, sb.root_to_tip = 0.25f;
    Wind &wf = *t.s.find_by_name("Breeze")->get<Wind>();
    wf.turbulence = 0.35f, wf.gusts = 0.45f, wf.spherical = true;
    Scene back;
    std::string error;
    CHECK(load_scene_text(save_scene_text(t.s), back, error));
    const GameObject *b1 = back.find_by_name("Tail1"), *bw = back.find_by_name("Breeze");
    const SpringBone *bs = b1 ? b1->get<SpringBone>() : nullptr;
    const Wind *ww = bw ? bw->get<Wind>() : nullptr;
    CHECK(bs && std::fabs(bs->bend - 0.7f) < 1e-6f && bs->separate && std::fabs(bs->wind - 0.5f) < 1e-6f && std::fabs(bs->root_to_tip - 0.25f) < 1e-6f && ww &&
          std::fabs(ww->strength - 6.0f) < 1e-6f && std::fabs(ww->turbulence - 0.35f) < 1e-6f && std::fabs(ww->gusts - 0.45f) < 1e-6f && ww->spherical);
    /* The hair label's defaults set the new fields. */
    SpringBone hair;
    spring::apply_defaults(hair, 0);
    CHECK(std::fabs(hair.bend - 0.2f) < 1e-6f && std::fabs(hair.root_to_tip - 0.6f) < 1e-6f && hair.separate && std::fabs(hair.wind - 1.0f) < 1e-6f);
  });

  test("strands: the bones keep their lengths within 0.1 % under a move with bend on, and a curl holds its angle swung out of its plane", [&] {
    /* The chain curled 40 degrees at each joint about X (the curl lies in the YZ plane), bend 1, no pull; the hips
     * swung a metre along X over the first second - the swing takes the chain out of the curl's plane. The joint
     * angles stay at the curl's, where a world-space turn would have flattened them; the lengths stay. */
    R139Tail t;
    r139_tail(t);
    for (const char *n : {"Tail2", "Tail3"}) t.s.find_by_name(n)->set_local_rotation(Quat::euler({40.0f, 0.0f, 0.0f}));
    SpringBone &sb = *t.tail1->get<SpringBone>();
    sb.stiffness = 0.0f, sb.bend = 1.0f, sb.damping = 0.3f, sb.gravity = 1.0f, sb.collide = false;
    GameObject *b[3] = {t.tail1, t.s.find_by_name("Tail2"), t.tip};
    auto joints = [&](Vec3 *p) {  // the three bones' heads and the last tip
      for (int i = 0; i < 3; i++) p[i] = b[i]->world_position();
      p[3] = tip_world(t.tip);
    };
    Vec3 p0[4], p[4];
    joints(p0);
    float worst = 0.0f, lean = 0.0f, angle[2] = {40.0f, 40.0f};  // (the joint angles furthest from the curl's over the swing, from the tenth frame)
    for (int f = 0; f < 240; f++) {
      t.hip->set_local_position({std::min(f, 60) / 60.0f, 1, 0});
      spring::step(t.s, 1.0f / 60.0f);
      joints(p);
      for (int i = 0; i < 3; i++) worst = std::max(worst, std::fabs(length(p[i + 1] - p[i]) - length(p0[i + 1] - p0[i])) / length(p0[i + 1] - p0[i]));
      lean = std::max(lean, -normalize(p[1] - p[0]).x);
      for (int i = 0; i < 2 && f >= 10; i++) {
        const Vec3 d0 = normalize(p[i + 1] - p[i]), d1 = normalize(p[i + 2] - p[i + 1]);
        const float a = std::acos(std::max(-1.0f, std::min(1.0f, dot(d0, d1)))) * 180.0f / kPi;
        if (std::fabs(a - 40.0f) > std::fabs(angle[i] - 40.0f)) angle[i] = a;
      }
    }
    std::printf("    swung a metre along X (the first bone leaning up to %.2f back along X): the bones' lengths within %.4f %%, the joints never further from the curl's 40 degrees than %.1f and %.1f\n", lean, worst * 100.0f, angle[0], angle[1]);
    CHECK(worst < 1e-3f && lean > 0.1f && std::fabs(angle[0] - 40.0f) < 10.0f && std::fabs(angle[1] - 40.0f) < 10.0f);
  });

  /* ---------------------------------------------------------------- round 172: soft tissue, the Jiggle modifier, hair rigged from its mesh (task 0153, ADR 0049) */
  test("soft tissue: a Soft bone on a running body trails its acceleration within its cone, keeps its volume, rests within a second, and its settings save", [&] {
    /* A chest bone up from the hips, a bust bone 10 cm forward off it; the hips bob 5 cm at 3 Hz (a run) for two
     * seconds, then stand. */
    auto run = [&](float lag, float limit_deg, float &trail, float &worst_angle, float &worst_volume, float &rest_after, Scene *keep) {
      Scene local;
      Scene &s = keep ? *keep : local;
      GameObject *root = s.create("Rig");
      root->add<Armature>();
      GameObject *hip = r139_bone(s, "Hips", root, {0, 1, 0}, Quat(), 0.2f);
      GameObject *chest = r139_bone(s, "Chest", hip, {0, 0.2f, 0}, Quat(), 0.3f);
      GameObject *bust = r139_bone(s, "Bust_L", chest, {0.1f, 0.2f, 0.05f}, Quat::euler({90, 0, 0}), 0.1f);  // (+Y turned to +Z: forward)
      spring::add(*bust, 4);
      SpringBone &sb = *bust->get<SpringBone>();
      CHECK(sb.label == 4 && std::fabs(sb.lag - 0.5f) < 1e-6f && std::fabs(sb.limit - 25.0f) < 1e-6f && std::fabs(sb.volume - 1.0f) < 1e-6f && sb.wind == 0.0f);
      sb.lag = lag, sb.limit = limit_deg, sb.collide = false;
      trail = 0.0f, worst_angle = 0.0f, worst_volume = 0.0f, rest_after = 0.0f;
      const Vec3 fwd(0.0f, 0.0f, 1.0f);
      for (int f = 0; f < 240; f++) {
        const float t = f / 60.0f;
        const float y = t < 2.0f ? 1.0f + 0.05f * std::sin(2.0f * kPi * 3.0f * t) : 1.0f;
        hip->set_local_position({0, y, 0});
        spring::step(s, 1.0f / 60.0f);
        const Vec3 head = bust->world_position(), tip = tip_world(bust), target = head + fwd * 0.1f;
        const Vec3 d = tip - head;
        const float angle = std::acos(std::max(-1.0f, std::min(1.0f, dot(normalize(d), fwd)))) * 180.0f / kPi;
        const Vec3 sc = bust->local().scale;
        if (t < 2.0f) trail = std::max(trail, length(tip - target)), worst_angle = std::max(worst_angle, angle), worst_volume = std::max(worst_volume, std::fabs(sc.x * sc.y * sc.z - 1.0f));
        if (t >= 3.0f) rest_after = std::max(rest_after, length(tip - target));
      }
    };
    float trail = 0, angle = 0, volume = 0, rest = 0, trail_narrow = 0, angle_narrow = 0, v2 = 0, r2 = 0;
    Scene saved;
    run(0.5f, 25.0f, trail, angle, volume, rest, &saved);
    run(1.0f, 5.0f, trail_narrow, angle_narrow, v2, r2, nullptr);
    /* A landing: the hips drop 10 cm over three frames and stop; the tissue thrown down past where the spring's own
     * lag takes it, by Lag (the bob's velocity lag hides the acceleration term in a peak-distance measure). */
    auto landing = [&](float lag) {
      R139Tail t;
      t.root = t.s.create("Rig");
      t.root->add<Armature>();
      t.hip = r139_bone(t.s, "Hips", t.root, {0, 1, 0}, Quat(), 0.2f);
      GameObject *chest = r139_bone(t.s, "Chest", t.hip, {0, 0.2f, 0}, Quat(), 0.3f);
      GameObject *bust = r139_bone(t.s, "Bust_L", chest, {0.1f, 0.2f, 0.05f}, Quat::euler({90, 0, 0}), 0.1f);
      spring::add(*bust, 4);
      bust->get<SpringBone>()->lag = lag;
      float down = 0.0f;
      for (int f = 0; f < 90; f++) {
        const float y = f < 30 ? 1.0f : f < 33 ? 1.0f - (f - 29) * 0.1f / 3.0f : 0.9f;
        t.hip->set_local_position({0, y, 0});
        spring::step(t.s, 1.0f / 60.0f);
        if (f >= 33) down = std::max(down, (bust->world_position().y + 0.0f) - tip_world(bust).y);
      }
      return down;
    };
    const float thrown = landing(0.5f), thrown0 = landing(0.0f);
    std::printf("    a bust bone on a 3 Hz run: swings up to %.1f degrees (limit 25; limit 5: %.1f), the scales' product off by %.2f %%, %.2f mm from its place a second after the run stops; "
                "a landing throws it %.1f mm down (Lag 0: %.1f mm)\n",
                angle, angle_narrow, volume * 100.0f, rest * 1000.0f, thrown * 1000.0f, thrown0 * 1000.0f);
    CHECK(thrown > thrown0 + 0.003f && angle <= 26.0f && angle_narrow <= 6.0f && trail_narrow > 0.002f && volume < 0.01f && rest < 0.001f);
    (void)trail;
    /* The scales back to 1 when the springs are reset; the settings saved and loaded. */
    spring::reset(saved);
    const Vec3 sc = saved.find_by_name("Bust_L")->local().scale;
    CHECK(std::fabs(sc.x - 1.0f) < 1e-5f && std::fabs(sc.y - 1.0f) < 1e-5f && std::fabs(sc.z - 1.0f) < 1e-5f);
    SpringBone &sb = *saved.find_by_name("Bust_L")->get<SpringBone>();
    sb.lag = 0.7f, sb.limit = 12.0f, sb.volume = 0.4f;
    Scene back;
    std::string error;
    CHECK(load_scene_text(save_scene_text(saved), back, error));
    const SpringBone *bs = back.find_by_name("Bust_L") ? back.find_by_name("Bust_L")->get<SpringBone>() : nullptr;
    CHECK(bs && bs->label == 4 && std::fabs(bs->lag - 0.7f) < 1e-6f && std::fabs(bs->limit - 12.0f) < 1e-6f && std::fabs(bs->volume - 0.4f) < 1e-6f);
  });

  test("soft tissue: a Jiggle modifier moves its vertex group with the body's acceleration, within its limit, and nothing outside the group", [&] {
    Scene s;
    GameObject *box = s.create("Box");
    MeshPtr m = std::make_shared<Mesh>(*primitives::cube());
    std::vector<float> w(m->vert_count(), 0.0f);
    for (size_t v = 0; v < m->vert_count(); v++) w[v] = m->positions[v].y > 0.0f ? 1.0f : 0.0f;  // (the top half jiggles)
    m->vertex_groups.push_back({"Soft", w});
    m->touch();
    box->add<MeshFilter>()->mesh = m;
    JiggleModifier *j = box->add<JiggleModifier>();
    j->vertex_group = "Soft", j->lag = 1.0f, j->limit = 0.03f;
    spring::reset(s);
    float moved_top = 0.0f, moved_bottom = 0.0f, rest = 0.0f;
    for (int f = 0; f < 240; f++) {
      const float t = f / 60.0f;
      box->set_local_position({0, t < 1.0f ? 0.2f * std::sin(2.0f * kPi * 4.0f * t) : 0.0f, 0});
      spring::step(s, 1.0f / 60.0f);
      const Mesh *e = box->evaluated_mesh();
      if (!e || e->vert_count() != m->vert_count()) break;
      for (size_t v = 0; v < m->vert_count(); v++) {
        const float d = length(e->positions[v] - m->positions[v]);
        if (t < 1.0f) (w[v] > 0.0f ? moved_top : moved_bottom) = std::max(w[v] > 0.0f ? moved_top : moved_bottom, d);
        if (t >= 2.0f) rest = std::max(rest, d);
      }
    }
    std::printf("    the box shaken at 4 Hz: the top half moved up to %.1f mm (limit 30), the bottom %.2f mm; %.2f mm a second after it stops\n", moved_top * 1000.0f, moved_bottom * 1000.0f,
                rest * 1000.0f);
    CHECK(moved_top > 0.002f && moved_top <= 0.0301f && moved_bottom < 1e-6f && rest < 0.001f);
  });

  test("hair rig: twelve hair cards become twelve chains of three spring bones under a hair rig, the mesh skinned to them with weights summing to one, and undo removes all", [&] {
    Editor ed;
    r34_open(ed);
    /* Twelve cards round a scalp at y = 1, each a strip of three quads hanging outward and down to y = 0.6. */
    MeshPtr hair = std::make_shared<Mesh>();
    for (int c = 0; c < 12; c++) {
      const float a = c * 2.0f * kPi / 12.0f;
      const Vec3 out(std::cos(a), 0.0f, std::sin(a)), side(-std::sin(a) * 0.02f, 0.0f, std::cos(a) * 0.02f);
      uint32_t first = (uint32_t)hair->vert_count();
      for (int r = 0; r < 4; r++) {
        const Vec3 p = out * (0.1f + 0.03f * r) + Vec3(0.0f, 1.0f - 0.1333f * r, 0.0f);
        hair->add_vert(p - side);
        hair->add_vert(p + side);
      }
      for (int r = 0; r < 3; r++) hair->add_face({first + 2 * r, first + 2 * r + 1, first + 2 * r + 3, first + 2 * r + 2});
    }
    hair->touch();
    GameObject *obj = ed.scene().create("Hair");
    obj->add<MeshFilter>()->mesh = hair;
    obj->add<MeshRenderer>();
    ed.mark_changed_for_test("Hair Mesh");  // (an undo step with the mesh in place: the rig's undo lands here)
    ed.command("select Hair");
    ed.step_frame_headless();
    ed.command("hair rig");
    ed.step_frame_headless();
    GameObject *rig = ed.scene().find_by_name("Hair Hair Rig");
    int chains = 0, bones = 0;
    ed.scene().for_each([&](GameObject &g) {
      if (!rig) return;
      for (GameObject *p = g.parent; p; p = p->parent)
        if (p == rig) {
          bones += g.get<Bone>() != nullptr;
          chains += g.get<SpringBone>() != nullptr;
        }
    });
    obj = ed.scene().find_by_name("Hair");
    const SkinModifier *sk = obj ? obj->get<SkinModifier>() : nullptr;
    const Mesh *hm = obj && obj->mesh_filter() ? obj->mesh_filter()->mesh.get() : nullptr;
    float worst_sum = 0.0f;
    size_t groups = 0;
    if (hm) {
      groups = hm->vertex_groups.size();
      for (size_t v = 0; v < hm->vert_count(); v++) {
        float sum = 0.0f;
        for (const VertexGroup &g : hm->vertex_groups)
          if (g.weights.size() == hm->vert_count()) sum += g.weights[v];
        worst_sum = std::max(worst_sum, std::fabs(sum - 1.0f));
      }
    }
    std::printf("    hair rig: %d chains, %d bones, %zu vertex groups, the weights' sums within %.4f of 1\n", chains, bones, groups, worst_sum);
    CHECK(rig && chains == 12 && bones == 36 && sk && sk->object == "Hair Hair Rig" && groups == 36 && worst_sum < 1e-3f);
    const SpringBone *one = ed.scene().find_by_name("Hair Hair1_1") ? ed.scene().find_by_name("Hair Hair1_1")->get<SpringBone>() : nullptr;
    CHECK(one && one->label == 0 && spring::chain_bones(*ed.scene().find_by_name("Hair Hair1_1")).size() == 3);
    ed.command("undo");
    ed.step_frame_headless();
    obj = ed.scene().find_by_name("Hair");
    std::printf("    after undo: the rig %s, the mesh %s, its skin %s, %zu vertex groups\n", ed.scene().find_by_name("Hair Hair Rig") ? "still there" : "gone", obj ? "there" : "GONE",
                obj && obj->get<SkinModifier>() ? "still there" : "gone", obj && obj->mesh_filter() && obj->mesh_filter()->mesh ? obj->mesh_filter()->mesh->vertex_groups.size() : (size_t)0);
    CHECK(!ed.scene().find_by_name("Hair Hair Rig") && obj && !obj->get<SkinModifier>() && obj->mesh_filter()->mesh->vertex_groups.empty());
    /* One shell (a cube) cut into four locks of two bones. */
    ed.command("create Cube");
    ed.command("select Cube");
    ed.step_frame_headless();
    ed.command("hair rig 4 2");
    ed.step_frame_headless();
    int cube_chains = 0;
    ed.scene().for_each([&](GameObject &g) { cube_chains += g.get<SpringBone>() != nullptr && g.name.rfind("Cube Hair", 0) == 0 && spring::chain_bones(g).size() == 2; });
    CHECK(ed.scene().find_by_name("Cube Hair Rig") && cube_chains == 4);
  });

  test("hair rig: a skinned hair mesh hangs its rig under the character's head bone (scaled rig: the bones still end at the tips), the body's groups go, a posed head refuses, more cards than locks merge", [&] {
    Editor ed;
    r34_open(ed);
    auto cards = [&](int count) {
      MeshPtr hair = std::make_shared<Mesh>();
      for (int c = 0; c < count; c++) {
        const float a = c * 2.0f * kPi / count;
        const Vec3 out(std::cos(a), 0.0f, std::sin(a)), side(-std::sin(a) * 0.01f, 0.0f, std::cos(a) * 0.01f);
        uint32_t first = (uint32_t)hair->vert_count();
        for (int r = 0; r < 4; r++) {
          const Vec3 p = out * (0.1f + 0.03f * r) + Vec3(0.0f, 1.0f - 0.1333f * r, 0.0f);
          hair->add_vert(p - side);
          hair->add_vert(p + side);
        }
        for (int r = 0; r < 3; r++) hair->add_face({first + 2 * r, first + 2 * r + 1, first + 2 * r + 3, first + 2 * r + 2});
      }
      hair->touch();
      return hair;
    };
    /* A body with a Head bone, the rig scaled by 2 (an FBX's centimetres); the hair mesh skinned to it. */
    GameObject *body = ed.scene().create("Body");
    body->add<Armature>();
    body->set_local_scale(Vec3(2.0f));
    GameObject *head = r139_bone(ed.scene(), "Head", body, {0, 0.4f, 0}, Quat(), 0.1f);  // (at y 0.8 in the world, 0.2 m long)
    MeshPtr hair = cards(12);
    hair->vertex_groups.push_back({"Head", std::vector<float>(hair->vert_count(), 1.0f)});
    GameObject *obj = ed.scene().create("Hair");
    obj->add<MeshFilter>()->mesh = hair;
    obj->add<MeshRenderer>();
    obj->add<SkinModifier>()->object = "Body";
    ed.mark_changed_for_test("Skinned Hair");
    ed.command("select Hair");
    ed.step_frame_headless();
    /* Posed: refused. */
    head->set_local_rotation(Quat::euler({0, 30, 0}));
    ed.command("hair rig");
    ed.step_frame_headless();
    const bool refused = !ed.scene().find_by_name("Hair Hair Rig");
    head->set_local_rotation(Quat());
    ed.command("hair rig");
    ed.step_frame_headless();
    GameObject *rig = ed.scene().find_by_name("Hair Hair Rig");
    obj = ed.scene().find_by_name("Hair");
    const Mesh *hm = obj && obj->mesh_filter() ? obj->mesh_filter()->mesh.get() : nullptr;
    const SkinModifier *sk = obj ? obj->get<SkinModifier>() : nullptr;
    float worst_sum = 0.0f, tip_off = 0.0f;
    bool head_group = false;
    if (hm) {
      for (const VertexGroup &g : hm->vertex_groups) head_group = head_group || g.name == "Head";
      for (size_t v = 0; v < hm->vert_count(); v++) {
        float sum = 0.0f;
        for (const VertexGroup &g : hm->vertex_groups) sum += g.weights[v];
        worst_sum = std::max(worst_sum, std::fabs(sum - 1.0f));
      }
    }
    /* Each chain's last bone ends at its card's lowest vertex (the lengths in the bones' own units under the scaled rig). */
    int chains = 0;
    ed.scene().for_each([&](GameObject &g) {
      if (!g.get<SpringBone>() || g.name.rfind("Hair Hair", 0) != 0) return;
      chains++;
      const std::vector<GameObject *> run = spring::chain_bones(g);
      const Vec3 tip = tip_world(run.back());
      float nearest = 1e9f;
      for (const Vec3 &p : hm->positions) nearest = std::min(nearest, length(p - tip));
      tip_off = std::max(tip_off, nearest);
    });
    const std::vector<spring::Collider> caps = spring::body_colliders(*spring::chain_armature(*ed.scene().find_by_name("Hair Hair1_1")));
    std::printf("    skinned hair: posed head %s; the rig under %s, %d chains, the Head group %s, %zu groups, the weights' sums within %.4f of 1, the chains' tips within %.4f m of a vertex, %zu body capsules seen from a hair bone\n",
                refused ? "refused" : "RIGGED", rig && rig->parent ? rig->parent->name.c_str() : "nothing", chains, head_group ? "still there" : "gone", hm ? hm->vertex_groups.size() : (size_t)0, worst_sum,
                tip_off, caps.size());
    CHECK(refused && rig && rig->parent == ed.scene().find_by_name("Head") && chains == 12 && sk && sk->object == "Hair Hair Rig" && !head_group && hm->vertex_groups.size() == 36 && worst_sum < 1e-3f &&
          tip_off < 0.01f && caps.size() == 1);
    /* Forty cards into eight locks. */
    GameObject *many = ed.scene().create("Mane");
    many->add<MeshFilter>()->mesh = cards(40);
    many->add<MeshRenderer>();
    ed.mark_changed_for_test("Mane");
    ed.command("select Mane");
    ed.step_frame_headless();
    ed.command("hair rig 8 3");
    ed.step_frame_headless();
    int mane_chains = 0;
    ed.scene().for_each([&](GameObject &g) { mane_chains += g.get<SpringBone>() != nullptr && g.name.rfind("Mane Hair", 0) == 0; });
    CHECK(ed.scene().find_by_name("Mane Hair Rig") && mane_chains == 8);
  });

  test("soft tissue: a body already moving when Play starts gives no kick, a two-bone soft chain stretches only its last bone, the stretch is baked as scale keys, Volume 0 lets the volume change, and 100 chains of 3 bones step under 0.5 ms", [&] {
    /* A bust bone forward off a chest; the hips moving at a steady 1 m/s from the first frame: the trail in the first
     * quarter second no more than the steady one (no kick from a speed read as an acceleration). */
    auto bust_rig = [&](Scene &s, int bones, float volume) {
      GameObject *root = s.create("Rig");
      root->add<Armature>();
      GameObject *hip = r139_bone(s, "Hips", root, {0, 1, 0}, Quat(), 0.2f);
      GameObject *chest = r139_bone(s, "Chest", hip, {0, 0.2f, 0}, Quat(), 0.3f);
      GameObject *bust = r139_bone(s, "Bust_L", chest, {0.1f, 0.2f, 0.05f}, Quat::euler({90, 0, 0}), 0.1f);
      if (bones > 1) r139_bone(s, "Bust_L2", bust, {0, 0.1f, 0}, Quat(), 0.05f);
      spring::add(*bust, 4);
      bust->get<SpringBone>()->volume = volume;
      return hip;
    };
    {
      auto early_off = [&](float lag) {
        Scene s;
        GameObject *hip = bust_rig(s, 1, 1.0f);
        GameObject *bust = s.find_by_name("Bust_L");
        bust->get<SpringBone>()->lag = lag;
        float early = 0.0f;
        for (int f = 0; f < 15; f++) {
          hip->set_local_position({f / 60.0f, 1, 0});
          spring::step(s, 1.0f / 60.0f);
          early = std::max(early, length(tip_world(bust) - (bust->world_position() + Vec3(0.0f, 0.0f, 0.1f))));
        }
        return early;
      };
      const float with = early_off(1.0f), without = early_off(0.0f);
      std::printf("    a body moving at 1 m/s from the first frame: the bust %.1f mm off in the first quarter second with Lag 1, %.1f mm with Lag 0 (no kick from a speed read as an acceleration)\n",
                  with * 1000.0f, without * 1000.0f);
      CHECK(with < without + 0.001f);
    }
    /* Two bones: the first keeps its scale, the last carries the stretch; a bake writes that stretch as Scale keys. */
    {
      Scene s;
      GameObject *hip = bust_rig(s, 2, 1.0f);
      GameObject *b1 = s.find_by_name("Bust_L"), *b2 = s.find_by_name("Bust_L2");
      float first_off = 0.0f, last_off = 0.0f, stretched = 0.0f;
      for (int f = 0; f < 120; f++) {
        const float t = f / 60.0f;
        hip->set_local_position({0, 1, 0.05f * std::sin(2.0f * kPi * 3.0f * t)});  // (bobbing along the bone: it stretches)
        spring::step(s, 1.0f / 60.0f);
        const Vec3 s1 = b1->local().scale, s2 = b2->local().scale;
        first_off = std::max(first_off, std::fabs(s1.x * s1.y * s1.z - 1.0f) + std::fabs(s1.y - 1.0f));
        last_off = std::max(last_off, std::fabs(s2.x * s2.y * s2.z - 1.0f));
        stretched = std::max(stretched, std::fabs(s2.y - 1.0f));
      }
      AnimationClip clip;
      clip.frame_rate = 60.0f;
      for (int f = 0; f <= 60; f += 2) clip.track("Hips", "Position", 2).set_key((float)f, 0.05f * std::sin(2.0f * kPi * 3.0f * f / 60.0f), AnimLinear);
      AnimationClip baked;
      const size_t keys = spring::bake(s, *s.find_by_name("Rig"), clip, baked);
      float baked_stretch = 0.0f;
      int scale_tracks = 0;
      for (const AnimTrack &t : baked.tracks) {
        if (t.property != "Scale") continue;
        scale_tracks++;
        for (const AnimKey &k : t.keys) baked_stretch = std::max(baked_stretch, std::fabs(k.value - 1.0f));
      }
      std::printf("    two bones bobbing along them: the first bone's scale off by %.4f, the last's product off by %.4f, stretched up to %.1f %%; baked: %zu keys, %d Scale tracks, the stretch up to %.1f %%\n",
                  first_off, last_off, stretched * 100.0f, keys, scale_tracks, baked_stretch * 100.0f);
      CHECK(first_off < 1e-5f && last_off < 0.01f && stretched > 0.002f && scale_tracks == 3 && baked_stretch > 0.002f);
      const Vec3 after = b2->local().scale;
      CHECK(std::fabs(after.y - 1.0f) < 1e-5f);  // (the bake gives the stretch back)
    }
    /* Volume 0: the bone stretches without narrowing, so the product of its scales moves with the stretch. */
    {
      Scene s;
      GameObject *hip = bust_rig(s, 1, 0.0f);
      GameObject *b = s.find_by_name("Bust_L");
      float product_off = 0.0f, across_off = 0.0f;
      for (int f = 0; f < 120; f++) {
        hip->set_local_position({0, 1, 0.05f * std::sin(2.0f * kPi * 3.0f * f / 60.0f)});
        spring::step(s, 1.0f / 60.0f);
        const Vec3 sc = b->local().scale;
        product_off = std::max(product_off, std::fabs(sc.x * sc.y * sc.z - 1.0f)), across_off = std::max(across_off, std::fabs(sc.x - 1.0f));
      }
      std::printf("    Volume 0: the product of the scales off by up to %.4f, the width off by %.5f\n", product_off, across_off);
      CHECK(product_off > 0.002f && across_off < 1e-6f);
    }
    /* (A hundred chains' frame time is a stress row: `physics`, task 0177.) */
  });

  test("soft tissue: a Jiggle modifier evaluated for the Scene and the Game view at one clock applies the same offset, and a half-weighted vertex moves half as far", [&] {
    Scene s;
    GameObject *box = s.create("Box");
    MeshPtr m = std::make_shared<Mesh>(*primitives::cube());
    std::vector<float> w(m->vert_count(), 0.0f);
    int half = -1;
    for (size_t v = 0; v < m->vert_count(); v++)
      if (m->positions[v].y > 0.0f) w[v] = half < 0 ? 0.5f : 1.0f, half = half < 0 ? (int)v : half;
    m->vertex_groups.push_back({"Soft", w});
    m->touch();
    box->add<MeshFilter>()->mesh = m;
    JiggleModifier *j = box->add<JiggleModifier>();
    j->vertex_group = "Soft", j->lag = 1.0f, j->limit = 0.03f;
    spring::reset(s);
    float moved[2] = {0.0f, 0.0f}, half_ratio = -1.0f, differ = 0.0f;
    for (int f = 0; f < 60; f++) {
      box->set_local_position({0, 0.2f * std::sin(2.0f * kPi * 4.0f * f / 60.0f), 0});
      spring::step(s, 1.0f / 60.0f);
      const Mesh *e0 = box->evaluated_mesh(0), *e1 = box->evaluated_mesh(1);
      if (!e0 || !e1) break;
      for (size_t v = 0; v < m->vert_count(); v++) {
        const float d0 = length(e0->positions[v] - m->positions[v]), d1 = length(e1->positions[v] - m->positions[v]);
        if (w[v] == 1.0f) moved[0] = std::max(moved[0], d0), moved[1] = std::max(moved[1], d1);
        differ = std::max(differ, std::fabs(d0 - d1));
        if ((int)v == half && d0 > 0.005f) {
          float full = 0.0f;
          for (size_t u = 0; u < m->vert_count(); u++)
            if (w[u] == 1.0f) full = std::max(full, length(e0->positions[u] - m->positions[u]));
          half_ratio = d0 / full;
        }
      }
    }
    std::printf("    the Scene view's mesh moved up to %.1f mm, the Game view's %.1f mm (differing by %.4f mm); the half-weighted vertex moved %.2f of a full one\n", moved[0] * 1000.0f,
                moved[1] * 1000.0f, differ * 1000.0f, half_ratio);
    CHECK(moved[0] > 0.005f && moved[1] > 0.005f && differ < 1e-6f && std::fabs(half_ratio - 0.5f) < 0.1f);
  });

}

static void tests_round176() {
  /* ---------------------------------------------------------------- round 176: the Wind gizmo and the effectors' icons (task 0158) */
  test("wind gizmo: a selected Wind draws an arrow along its direction as long as its strength says, three streamlines with turbulence, and rings when spherical", [&] {
    Scene s;
    GameObject *b = s.create("Breeze");
    Wind *w = b->add<Wind>();
    w->strength = 3.0f, w->turbulence = 0.0f;
    b->set_local_rotation(Quat::euler({0, 90, 0}));  // (forward +Z turned to +X)
    std::vector<std::pair<Vec3, Vec3>> segs;
    Editor::wind_gizmo_segments(*b, *w, segs);
    const Vec3 dir = spring::wind_direction(*b);
    float reach = 0.0f;
    for (auto &sg : segs) reach = std::max(reach, std::max(length(sg.first), length(sg.second)));
    std::printf("    a 3 m/s wind turned to +X: direction (%.2f %.2f %.2f), %zu segments, the arrow reaching %.4f m (0.5 + 0.25 x 3)\n", dir.x, dir.y, dir.z, segs.size(), reach);
    CHECK(length(dir - Vec3(1, 0, 0)) < 1e-4f);  // (winds_of reads the same wind_direction: one function, spring_bones.cpp)
    CHECK(segs.size() == 3 && std::fabs(reach - 1.25f) < 1e-4f && length(segs[0].second - Vec3(1.25f, 0, 0)) < 1e-4f);
    w->turbulence = 0.5f;
    Editor::wind_gizmo_segments(*b, *w, segs);
    CHECK(segs.size() == 3 + 36);  // (three streamlines of twelve pieces)
    w->spherical = true;
    Editor::wind_gizmo_segments(*b, *w, segs);
    float ring = 0.0f;
    for (auto &sg : segs) ring = std::max(ring, std::fabs(length(sg.first) - 1.25f));
    CHECK(segs.size() == 72 && ring < 1e-4f);  // (three rings, no arrow)
    /* A wind pitched 80 degrees (nearly vertical: the basis's other reference): the rings still true circles. */
    b->set_local_rotation(Quat::euler({-80, 0, 0}));
    Editor::wind_gizmo_segments(*b, *w, segs);
    ring = 0.0f;
    for (auto &sg : segs) ring = std::max(ring, std::fabs(length(sg.first) - 1.25f));
    CHECK(ring < 1e-4f);
    w->spherical = false;
    Editor::wind_gizmo_segments(*b, *w, segs);
    const Vec3 d80 = spring::wind_direction(*b), barb = segs[1].second - segs[1].first;  // (the barbs at right angles to the shaft, off it by a tenth of the length)
    const float along = dot(barb, d80), across = length(barb - d80 * along);
    std::printf("    pitched 80 degrees: a barb %.4f along the shaft (0.25) and %.4f across it (0.125)\n", -along, across);
    CHECK(std::fabs(-along - 0.25f) < 1e-4f && std::fabs(across - 0.125f) < 1e-4f);
  });

  test("effector icons: a Wind, a Rotator and an Oscillator have Scene view icons, a plain object none, and the Wind's icon is clicked like a light's", [&] {
    Scene s;
    GameObject *b = s.create("Breeze");
    b->add<Wind>();
    /* The icons: a Wind, a Rotator and an Oscillator have one; a plain object has none. */
    GameObject *r = s.create("Spinner"), *o = s.create("Bob"), *plain = s.create("Plain");
    r->add<Rotator>(), o->add<Oscillator>();
    CHECK(Editor::scene_icon_of(*b) == ui::Icon::Wind && Editor::scene_icon_of(*r) == ui::Icon::Spin && Editor::scene_icon_of(*o) == ui::Icon::Wave &&
          Editor::scene_icon_of(*plain) == ui::Icon::None);
    /* Clicked in the Scene view like a light's icon; not when the icons layer is off. */
    Editor ed;
    r34_open(ed);
    ed.command("camera 0 1.2 9 0 0.8 3");
    GameObject *wind = ed.scene().create("Gust");
    wind->add<Wind>();
    wind->set_local_position({0, 0.8f, 3});
    ed.mark_changed_for_test("a wind");
    r34_steps(ed, 2);
    int x = 0, y = 0;
    CHECK(ed.screen_of_for_test(wind->world_position(), x, y));
    ed.command("select Main Camera");
    r34_steps(ed, 1);
    r34_click(ed, x, y);
    r34_steps(ed, 1);
    CHECK(ed.active_object_for_test() == wind);
    ed.command("overlay icons off");
    ed.command("select Main Camera");
    r34_steps(ed, 1);
    r34_click(ed, x, y);
    r34_steps(ed, 1);
    std::printf("    the wind's icon clicked: selected with the layer on, %s with it off\n", ed.active_object_for_test() == wind ? "SELECTED" : "not");
    CHECK(ed.active_object_for_test() != wind);
  });

  test("baked emission: a small emissive cube lights the floor as the analytic irradiance says, four times smoother than the hemisphere alone, the same twice, and on the GPU gather", [] {
    Environment env;
    env.mode = Environment::Color;
    env.color = Vec3(0.0f);
    env.strength = 0.0f;
    const MeshPtr plane = primitives::plane(4.0f, 4), cube = primitives::cube();
    BakeObject floor_o = r35_object(1, plane, Mat4());
    const Mat4 cube_m = Mat4::trs({0, 0.5f, 0}, Quat(), Vec3(0.1f));
    BakeObject lamp = r35_object(2, cube, cube_m);
    lamp.materials[0]->emission = Vec3(1.0f);
    lamp.materials[0]->emission_strength = 20.0f;
    BakeSettings s = r35_small(12.0f, 64);
    s.denoise = false;
    s.bounces = 2;
    /* The reference: the cube's faces integrated (a convex emitter of radiance 20: each face point that faces the
     * floor point sends L cos_l cos_p / d^2 per area; the lightmap holds irradiance / pi). A hemisphere gather would
     * need a million rays to see a 10 cm lamp this steadily. */
    auto reference = [&](float x) {
      const Vec3 p(x, 0.0f, 0.2f), c(0, 0.5f, 0);
      const float h = 0.05f, L = 20.0f;
      double E = 0.0;
      const int G = 40;
      const Vec3 axes[3] = {Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1)};
      for (int a = 0; a < 3; a++)
        for (int sgn = -1; sgn <= 1; sgn += 2) {
          const Vec3 nrm = axes[a] * (float)sgn, u = axes[(a + 1) % 3], v = axes[(a + 2) % 3];
          const double dA = (2.0 * h / G) * (2.0 * h / G);
          for (int i = 0; i < G; i++)
            for (int j = 0; j < G; j++) {
              const Vec3 q = c + nrm * h + u * (h * (2.0f * (i + 0.5f) / G - 1.0f)) + v * (h * (2.0f * (j + 0.5f) / G - 1.0f));
              const Vec3 d = p - q;
              const float dist = length(d);
              const Vec3 dir = d / dist;
              const float cos_l = dot(nrm, dir), cos_p = -dir.y;
              if (cos_l <= 0.0f || cos_p <= 0.0f) continue;
              E += L * cos_l * cos_p / (dist * dist) * dA;
            }
        }
      return (float)(E / kPi);
    };
    auto bake = [&](bool sampled, int gpu) {
      BakeSettings bs = s;
      bs.sample_emissive = sampled;
      bs.gpu_device = gpu;
      Lightmapper lm;
      return r35_run(lm, {floor_o, lamp}, {}, env, bs);
    };
    auto line = [&](const LightingData &ld, std::vector<float> &out, float x0, float x1, int n) {
      out.clear();
      for (int i = 0; i < n; i++) {
        Vec3 v;
        const float x = x0 + (x1 - x0) * i / (float)(n - 1);
        out.push_back(r35_sample(ld, 1, *plane, Mat4(), {x, 0, 0.2f}, v) ? v.x : -1.0f);
      }
    };
    const LightingData sampled = bake(true, -1), hemi = bake(false, -1), again = bake(true, -1);
    /* (a) Against the reference: within 5 % RMS of it along the line under the lamp. */
    double err = 0.0, scale = 0.0;
    std::vector<float> vs;
    line(sampled, vs, 0.0f, 1.2f, 7);
    for (int i = 0; i < 7; i++) {
      const float ref = reference(1.2f * i / 6.0f);
      err += (vs[(size_t)i] - ref) * (vs[(size_t)i] - ref), scale += ref * ref;
    }
    const double rms = std::sqrt(err / std::max(1e-12, scale));
    /* (b) Smoothness: the mean squared second difference along a line of texels, hemisphere-only against sampled. */
    auto roughness = [&](const LightingData &ld) {
      std::vector<float> t;
      line(ld, t, 0.15f, 1.35f, 48);
      double r = 0.0;
      for (size_t i = 1; i + 1 < t.size(); i++) r += (t[i - 1] - 2.0f * t[i] + t[i + 1]) * (t[i - 1] - 2.0f * t[i] + t[i + 1]);
      return r / (double)(t.size() - 2);
    };
    const double rough_hemi = roughness(hemi), rough_sampled = roughness(sampled);
    /* (c) The same twice. */
    std::vector<float> a, b;
    line(sampled, a, 0.0f, 1.5f, 40), line(again, b, 0.0f, 1.5f, 40);
    float twice = 0.0f;
    for (size_t i = 0; i < a.size(); i++) twice = std::max(twice, std::fabs(a[i] - b[i]));
    std::printf("    a 10 cm lamp 0.5 m over the floor: the sampled bake %.1f %% RMS from the analytic irradiance (under it %.3f vs %.3f); roughness hemisphere-only %.2e, "
                "sampled %.2e (%.1fx smoother); baked twice within %.2e\n",
                rms * 100.0, vs[0], reference(0.0f), rough_hemi, rough_sampled, rough_hemi / std::max(1e-12, rough_sampled), twice);
    CHECK(rms < 0.05 && rough_hemi > 4.0 * rough_sampled && twice < 1e-5f);
    /* (d) The GPU gather, when there is one: the first emissive hit weighted the same way, so the result agrees. */
    if (PathTracer::gpu_available() && !gpu::devices().empty()) {
      const LightingData gpu_ld = bake(true, gpu::devices()[0].index);
      std::vector<float> g;
      line(gpu_ld, g, 0.0f, 1.2f, 7);
      float worst = 0.0f;
      std::string row;
      for (int i = 0; i < 7; i++) {
        worst = std::max(worst, std::fabs(g[(size_t)i] - vs[(size_t)i]) / std::max(0.05f, vs[(size_t)i]));
        row += strprintf(" %.3f/%.3f", vs[(size_t)i], g[(size_t)i]);
      }
      std::printf("    the GPU gather's bake within %.1f %% of the CPU's along the line (CPU/GPU:%s)\n", worst * 100.0f, row.c_str());
      CHECK(worst < 0.06f);
    }
  });

  test("IK handles: a new target starts with the foot's rotation and turning it turns the foot while the knee stays; Rotate End off keeps the old pose; ik orient captures the offset; a driven foot refuses the gizmo", [&] {
    Editor ed;
    ed.init_headless(800, 500);
    ed.command("rig humanoid");
    ed.step_frame_headless();
    GameObject *t = ed.scene().find_by_name("RightFootIK"), *foot = ed.scene().find_by_name("RightFoot"), *shin = ed.scene().find_by_name("RightLowerLeg");
    CHECK(t && foot && shin && shin->get<IKConstraint>() && shin->get<IKConstraint>()->rotate_end);
    if (!t || !foot || !shin) return;
    auto fwd_of = [](GameObject *g) { return normalize(g->world_matrix().dir(Vec3(0, 1, 0))); };  // (a bone points along its Y)
    const Quat tq = t->world_rotation(), fq = foot->world_rotation(), off = shin->get<IKConstraint>()->end_offset;
    const Quat made = normalize(tq * off);  // (the handle starts world-aligned; the offset carries the foot's own turn)
    CHECK(std::fabs(tq.w) > 0.9999f && std::fabs(made.x * fq.x + made.y * fq.y + made.z * fq.z + made.w * fq.w) > 0.9999f);
    const Vec3 knee = shin->world_position(), foot_dir = fwd_of(foot);
    /* Turn the handle 30 degrees about the world's up: the foot turns 30, the knee doesn't move. */
    t->set_world_rotation(normalize(Quat::axis_angle(Vec3(0, 1, 0), 30.0f * kPi / 180.0f) * t->world_rotation()));
    ed.mark_changed_for_test("turn the handle");
    ed.step_frame_headless();
    const float turned = std::acos(std::max(-1.0f, std::min(1.0f, dot(foot_dir, fwd_of(foot))))) * 180.0f / kPi;
    std::printf("    the handle turned 30 degrees: the foot turned %.2f, the knee moved %.5f m\n", turned, length(shin->world_position() - knee));
    CHECK(std::fabs(turned - 30.0f) < 0.5f && length(shin->world_position() - knee) < 1e-5f);
    /* Rotate End off: the foot keeps its own pose (swings with the shin only). */
    shin->get<IKConstraint>()->rotate_end = false;
    foot->set_local_rotation(foot->get<Bone>()->rest_rotation);
    ed.mark_changed_for_test("rotate end off");
    ed.step_frame_headless();
    t->set_world_rotation(normalize(Quat::axis_angle(Vec3(0, 1, 0), 30.0f * kPi / 180.0f) * t->world_rotation()));
    ed.mark_changed_for_test("turn again");
    ed.step_frame_headless();
    const Quat rq = foot->local().rotation, rr = foot->get<Bone>()->rest_rotation;
    CHECK(std::fabs(rq.x * rr.x + rq.y * rr.y + rq.z * rr.z + rq.w * rr.w) > 0.9999f);
    /* ik orient: Rotate End back on with the offset that keeps the foot as it is now against the turned handle. */
    foot->set_local_rotation(normalize(Quat::axis_angle(Vec3(1, 0, 0), 20.0f * kPi / 180.0f) * foot->local().rotation));
    const Vec3 held = fwd_of(foot);
    ed.command("ik orient RightLowerLeg");
    ed.step_frame_headless();
    CHECK(shin->get<IKConstraint>()->rotate_end && dot(held, fwd_of(foot)) > 0.9999f);
    t->set_world_rotation(normalize(Quat::axis_angle(Vec3(0, 1, 0), -30.0f * kPi / 180.0f) * t->world_rotation()));
    ed.mark_changed_for_test("turn back");
    ed.step_frame_headless();
    auto yaw_of = [](Vec3 d) { return std::atan2(d.x, d.z) * 180.0f / kPi; };  // (the foot was pitched: its heading is what the handle's yaw turns)
    const float yawed = std::fabs(yaw_of(fwd_of(foot)) - yaw_of(held));
    std::printf("    the pitched foot's heading turned %.2f degrees for the handle's 30\n", yawed);
    CHECK(std::fabs(yawed - 30.0f) < 0.5f);
    /* The foot is driven now: the gizmo refuses it, as it refuses the shin. */
    CHECK(ik_drives(ed.scene(), *foot));
    /* Saved and loaded: Rotate End and its offset survive. */
    Scene back;
    std::string err;
    CHECK(load_scene_text(save_scene_text(ed.scene()), back, err));
    const IKConstraint *bk = back.find_by_name("RightLowerLeg") ? back.find_by_name("RightLowerLeg")->get<IKConstraint>() : nullptr;
    const IKConstraint *orig = shin->get<IKConstraint>();
    CHECK(bk && bk->rotate_end && std::fabs(bk->end_offset.x - orig->end_offset.x) < 1e-6f && std::fabs(bk->end_offset.w - orig->end_offset.w) < 1e-6f);
  });
}

/* Round 188 (task 0168, ADR 0049 amended): the strands' feel, measured. */
static void tests_round188() {
  auto bone = [](Scene &s, const std::string &name, GameObject *parent, Vec3 pos, Quat rot, float len) {
    GameObject *g = s.create(name, parent);
    g->set_local_position(pos);
    g->set_local_rotation(rot);
    Bone *b = g->add<Bone>();
    b->length = len;
    b->rest_position = pos;
    b->rest_rotation = rot;
    return g;
  };
  auto tip_world = [](GameObject *g) { return g->world_matrix().point(Vec3(0, g->get<Bone>()->length, 0)); };
  /* A head bone at 1.6 m and a strand of `n` bones `total` long from 10 cm behind it, along `rot` (the rest and the
   * animation's pose), labelled hair. */
  struct Strand {
    Scene s;
    GameObject *head = nullptr, *first = nullptr, *tip = nullptr;
  };
  auto strand = [&](Strand &st, int n, float total, Quat rot) {
    GameObject *root = st.s.create("Rig");
    root->add<Armature>();
    st.head = bone(st.s, "Head", root, {0, 1.6f, 0}, Quat(), 0.1f);
    GameObject *g = st.head;
    for (int i = 1; i <= n; i++) g = bone(st.s, "Hair" + std::to_string(i), g, i == 1 ? Vec3(0, 0, -0.1f) : Vec3(0, total / n, 0), i == 1 ? rot : Quat(), total / n);
    st.tip = g;
    st.first = st.s.find_by_name("Hair1");
    spring::add(*st.first, 0);
  };
  /* A run's settle time (the first time from which the tip stays within `tol` of where it ends) and its overshoots
   * (the tip's distance to its end rising again by more than 2 mm after falling: a ring). */
  struct Settle {
    float time = 0.0f;
    int overshoots = 0;
  };
  auto settle_of = [](const std::vector<Vec3> &path, float dt, float tol) {
    Settle r;
    const Vec3 end = path.back();
    size_t last_out = 0;
    for (size_t i = 0; i < path.size(); i++)
      if (length(path[i] - end) > tol) last_out = i + 1;
    r.time = last_out ? (last_out + 1) * dt : 0.0f;  // (path[i] is the frame after step i + 1: settled from the next one)
    float ext = length(path[0] - end);
    bool down = true;
    for (const Vec3 &p : path) {
      const float d = length(p - end);
      if (down && d < ext) ext = d;
      else if (down && d > ext + 2e-3f) r.overshoots++, down = false, ext = d;
      else if (!down && d > ext) ext = d;
      else if (!down && d < ext - 2e-3f) down = true, ext = d;
    }
    return r;
  };

  test("strands feel like film hair (task 0168): a 40 cm strand of eight bones released horizontally settles under gravity within 1.5 s with at most one overshoot; a head turned 90 degrees in 0.3 s brings the tip round within 1.0 s of the turn's end with at most one overshoot", [&] {
    const float dt = 1.0f / 60.0f;
    Strand a;
    strand(a, 8, 0.4f, Quat::euler({-90.0f, 0, 0}));  // (straight back, horizontal)
    std::vector<Vec3> path;
    for (int f = 0; f < 300; f++) {
      spring::step(a.s, dt);
      path.push_back(tip_world(a.tip));
    }
    const Settle sa = settle_of(path, dt, 0.01f);
    const Vec3 hang = path.back();
    std::printf("    released horizontally: settled within 1 cm after %.2f s, %d overshoot(s); the tip ends %.3f m below the head\n", sa.time, sa.overshoots,
                1.6f - hang.y);
    CHECK(sa.time <= 1.5f && sa.overshoots <= 1);
    /* The head turns 90 degrees about Y over 0.3 s with the strand pointing back from it (settled first): the strand
     * swings through a quarter circle, lagging the turn, then comes round. */
    Strand b;
    strand(b, 8, 0.4f, Quat::euler({-90.0f, 0, 0}));
    for (int f = 0; f < 120; f++) spring::step(b.s, dt);
    std::vector<Vec3> turn;
    float lag = 0.0f;
    for (int f = 0; f < 240; f++) {
      const float t = std::min(1.0f, (f + 1) * dt / 0.3f);
      b.head->set_local_rotation(Quat::euler({0.0f, 90.0f * t, 0.0f}));
      spring::step(b.s, dt);
      const Vec3 d = tip_world(b.tip) - b.head->world_position();
      const float yaw = std::atan2(-d.x, -d.z) * 180.0f / kPi;  // (0 straight back, 90 once turned)
      lag = std::max(lag, 90.0f * t - yaw);
      if ((f + 1) * dt >= 0.3f) turn.push_back(tip_world(b.tip));
    }
    const Settle sb = settle_of(turn, dt, 0.01f);
    std::printf("    head turned 90 degrees in 0.3 s: the tip lagged the turn by up to %.1f degrees, within 1 cm of its rest %.2f s after it, %d overshoot(s)\n", lag, sb.time,
                sb.overshoots);
    CHECK(sb.time <= 1.0f && sb.overshoots <= 1 && lag > 5.0f);
  });

  test("strands: the same motion at 30, 60 and 144 frames a second and with the frame time wavering (the tip 0.25 s after a horizontal strand is let go, within 1 cm), and a curl with Bend 0.5 keeps a share of its curl hanging under its own weight", [&] {
    /* The tip a quarter second after release, mid-fall: the dynamics, not only where it ends. */
    auto fall = [&](const std::vector<float> &dts) {
      Strand a;
      strand(a, 8, 0.4f, Quat::euler({-90.0f, 0, 0}));
      float time = 0.0f;
      for (size_t k = 0; time < 0.25f - 1e-4f; k++) {
        const float dt = dts[k % dts.size()];
        spring::step(a.s, dt);
        time += dt;
      }
      return tip_world(a.tip);
    };
    const Vec3 p60 = fall({1.0f / 60.0f}), p30 = fall({1.0f / 30.0f}), p144 = fall({1.0f / 144.0f}), pw = fall({1.0f / 60.0f, 1.0f / 59.0f});
    std::printf("    0.25 s after release, the tip at 30 / 144 / a wavering 59-60 frames a second against 60: %.4f / %.4f / %.4f m apart (it has fallen %.3f m)\n",
                length(p30 - p60), length(p144 - p60), length(pw - p60), 1.6f - p60.y);
    CHECK(length(p30 - p60) < 0.01f && length(p144 - p60) < 0.01f && length(pw - p60) < 0.01f && 1.6f - p60.y > 0.02f);
    /* A curl: the strand's bones turned 30 degrees each about X from horizontal, Bend 0.5, no pull: how far its tip
     * drops under gravity, as a share of the curl's height. */
    Strand c;
    strand(c, 6, 0.36f, Quat::euler({-90.0f, 0, 0}));
    for (int i = 2; i <= 6; i++) c.s.find_by_name("Hair" + std::to_string(i))->set_local_rotation(Quat::euler({-30.0f, 0, 0}));
    SpringBone &sb = *c.first->get<SpringBone>();
    sb.stiffness = 0.0f, sb.bend = 0.5f, sb.root_to_tip = 0.0f, sb.collide = false;
    /* How much of the curl's 30 degrees at each joint is left after four seconds hanging (the chain swings down about
     * its root with no pull: the joints' angles are what Bend holds). */
    auto joint_angles = [&] {
      std::vector<Vec3> p{c.first->world_position()};
      for (GameObject *g = c.first; g; g = g->children.empty() ? nullptr : g->children[0]) p.push_back(tip_world(g));
      float sum = 0.0f;
      for (size_t i = 1; i + 1 < p.size(); i++) {
        const Vec3 d0 = normalize(p[i] - p[i - 1]), d1 = normalize(p[i + 1] - p[i]);
        sum += std::acos(std::max(-1.0f, std::min(1.0f, dot(d0, d1)))) * 180.0f / kPi;
      }
      return sum / (float)(p.size() - 2);
    };
    const float curl0 = joint_angles();
    for (int f = 0; f < 240; f++) spring::step(c.s, 1.0f / 60.0f);
    const float kept = joint_angles() / std::max(1e-3f, curl0);
    std::printf("    a curl of %.1f degrees a joint with Bend 0.5, hanging from its root for 4 s: %.0f %% of it kept\n", curl0, 100.0f * kept);
    CHECK(kept > 0.1f && kept < 0.97f);
  });

  test("strands: a heavier chain (ten times hair's density) sags further and settles more slowly under the same Stiffness, and Density and Air survive saving", [&] {
    auto run = [&](float density, float &sag, float &settle) {
      Strand a;
      strand(a, 8, 0.4f, Quat::euler({-90.0f, 0, 0}));
      a.first->get<SpringBone>()->density = density;
      std::vector<Vec3> path;
      for (int f = 0; f < 300; f++) {
        spring::step(a.s, 1.0f / 60.0f);
        path.push_back(tip_world(a.tip));
      }
      sag = 1.6f - path.back().y;
      settle = settle_of(path, 1.0f / 60.0f, 0.01f).time;
    };
    float sag1, set1, sag10, set10;
    run(0.01f, sag1, set1);
    run(0.1f, sag10, set10);
    std::printf("    hair's density: the tip sags %.3f m, settled after %.2f s; ten times it: %.3f m, %.2f s\n", sag1, set1, sag10, set10);
    CHECK(sag10 > sag1 * 1.2f && set10 > set1);
    Strand b;
    strand(b, 2, 0.2f, Quat());
    SpringBone &sb = *b.first->get<SpringBone>();
    sb.density = 0.07f, sb.air = 1.5f;
    Scene back;
    std::string error;
    CHECK(load_scene_text(save_scene_text(b.s), back, error));
    const GameObject *h1 = back.find_by_name("Hair1");
    const SpringBone *bs = h1 ? h1->get<SpringBone>() : nullptr;
    CHECK(bs && std::fabs(bs->density - 0.07f) < 1e-6f && std::fabs(bs->air - 1.5f) < 1e-6f);
    SpringBone tail;
    spring::apply_defaults(tail, 2);
    CHECK(std::fabs(tail.density - 0.1f) < 1e-6f);
    /* A tail saved before the fields: its lines taken out, it loads the Tail label's Density and Air, not hair's. */
    b.first->get<SpringBone>()->label = 2;
    std::string text = save_scene_text(b.s), old;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);)
      if (line.find("Density") == std::string::npos && line.find("Air ") == std::string::npos && line.find("Air\t") == std::string::npos) old += line + "\n";
    Scene was;
    CHECK(load_scene_text(old, was, error));
    const GameObject *w1 = was.find_by_name("Hair1");
    const SpringBone *ws = w1 ? w1->get<SpringBone>() : nullptr;
    std::printf("    a tail saved before Density and Air: loads density %.3f, air %.2f\n", ws ? ws->density : -1.0f, ws ? ws->air : -1.0f);
    CHECK(ws && std::fabs(ws->density - 0.1f) < 1e-6f && std::fabs(ws->air - 0.5f) < 1e-6f);
  });
}

void run_physics_tests() {
  physics_tests();
  tests_round137_to_round140();
  tests_round139_to_round172();
  tests_round176();
  tests_round188();
}

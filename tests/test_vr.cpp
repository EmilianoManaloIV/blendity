// SPDX-License-Identifier: GPL-2.0-or-later
// VR View (round 383, task 0360): the stereo maths, the session's state machine on a stub runtime (no headset in the
// suite), a stereo frame of a known scene with the disparity two eyes 64 mm apart give, and the editor's `vr` verbs.
#include "test_common.h"

#include "../src/render/xr.h"
#include "../src/render/xr_nav.h"
#include "../src/scene/stroke_object.h"

#include <chrono>
#include <cmath>
#include <deque>
#include <functional>
#include <thread>

using namespace bl;

namespace {

/* A runtime that is told what to say: its events, whether frames should be drawn, and two eyes 64 mm apart looking
 * down -Z at 1.6 m, each `w` x `h` with a 90-degree field. It keeps every call and the last pixels of each eye. */
struct StubRuntime : xr::Runtime {
  std::deque<Event> events;
  bool create_ok = true, wait_ok = true, render = true;
  int w = 64, h = 64;
  float ipd = 0.064f;
  std::vector<std::string> calls;
  std::vector<std::vector<uint32_t>> eye_pixels{2};
  xr::Hand hands[2];  // (round 384: what the controllers say, set by the test)
  bool locate_hands(xr::Hand out[2]) override {
    out[0] = hands[0], out[1] = hands[1];
    return hands[0].tracked || hands[1].tracked;
  }
  float scale = 0.0f;
  bool create(float s, std::string &why) override {
    calls.push_back("create");
    scale = s;
    if (!create_ok) why = "no headset (stub)";
    return create_ok;
  }
  Event poll_event() override {
    if (events.empty()) return Event::None;
    const Event e = events.front();
    events.pop_front();
    return e;
  }
  bool begin_session(std::string &) override { return calls.push_back("begin_session"), true; }
  void end_session() override { calls.push_back("end_session"); }
  void destroy() override { calls.push_back("destroy"); }
  bool wait_frame(bool &should) override {
    calls.push_back("wait_frame");
    should = render;
    return wait_ok;
  }
  bool begin_frame() override { return calls.push_back("begin_frame"), true; }
  bool locate_eyes(std::vector<xr::Eye> &eyes) override {
    calls.push_back("locate_eyes");
    eyes.resize(2);
    for (int i = 0; i < 2; i++) {
      eyes[(size_t)i].pose = {Quat(), Vec3(i == 0 ? -ipd / 2 : ipd / 2, 1.6f, 0.0f)};
      eyes[(size_t)i].fov = {-0.785398f, 0.785398f, 0.785398f, -0.785398f};
      eyes[(size_t)i].width = w, eyes[(size_t)i].height = h;
    }
    return true;
  }
  bool submit_eye(int i, const uint32_t *px, int width, int height) override {
    calls.push_back("submit_eye");
    eye_pixels[(size_t)i].assign(px, px + (size_t)width * (size_t)height);
    return true;
  }
  bool end_frame(bool rendered) override { return calls.push_back(rendered ? "end_frame drawn" : "end_frame empty"), true; }
  std::string name() const override { return "Stub"; }
};

int count_of(const std::vector<std::string> &v, const char *s) { return (int)std::count(v.begin(), v.end(), std::string(s)); }

/* The red cube's centre in an eye's picture (x, y in pixels; -1 when none of it is seen). */
Vec2 red_centre(const std::vector<uint32_t> &px, int w, int h) {
  double sx = 0, sy = 0, n = 0;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      const uint32_t p = px[(size_t)y * w + x];
      const int r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
      if (r < g + 60 || r < b + 60) continue;
      sx += x, sy += y, n++;
    }
  return n > 0 ? Vec2((float)(sx / n), (float)(sy / n)) : Vec2(-1, -1);
}

}  // namespace

static void vr_math_tests() {
  test("VR's stereo maths (round 383): an off-centre projection from four angles (a symmetric one is Mat4::perspective), a field's edge at the screen's, OpenXR's turns and points mirrored into Blendity's axes, an eye's view of a point ahead, the stage at a scale", [] {
    const Mat4 a = xr::eye_projection({-0.6f, 0.6f, 0.5f, -0.5f}, 0.1f, 100.0f);
    const Mat4 b = Mat4::perspective(1.0f, std::tan(0.6f) / std::tan(0.5f), 0.1f, 100.0f);
    float worst = 0.0f;
    for (int i = 0; i < 16; i++) worst = std::max(worst, std::fabs(a.m[i] - b.m[i]));
    CHECK(worst < 1e-5f);
    /* Off-centre: the right edge's direction lands on x = 1, the left edge's on -1, the top's on y = 1. */
    const xr::Fov f{-0.4f, 0.9f, 0.7f, -0.3f};
    const Mat4 p = xr::eye_projection(f, 0.1f, 100.0f);
    auto ndc = [&](Vec3 v) {
      const float x = p.m[0] * v.x + p.m[8] * v.z, y = p.m[5] * v.y + p.m[9] * v.z;
      return Vec2(x / v.z, y / v.z);
    };
    std::printf("    the edges: right %.5f, left %.5f, top %.5f, bottom %.5f\n", ndc({std::tan(0.9f) * 3, 0, 3}).x, ndc({std::tan(-0.4f) * 3, 0, 3}).x,
                ndc({0, std::tan(0.7f) * 3, 3}).y, ndc({0, std::tan(-0.3f) * 3, 3}).y);
    CHECK(std::fabs(ndc({std::tan(0.9f) * 3, 0, 3}).x - 1) < 1e-5f && std::fabs(ndc({std::tan(-0.4f) * 3, 0, 3}).x + 1) < 1e-5f);
    CHECK(std::fabs(ndc({0, std::tan(0.7f) * 3, 3}).y - 1) < 1e-5f && std::fabs(ndc({0, std::tan(-0.3f) * 3, 3}).y + 1) < 1e-5f);
    /* A turn: OpenXR's forward (-Z) turned by a yaw is Blendity's forward (+Z) turned by the mirrored turn. */
    const Quat yaw = Quat::axis_angle({0, 1, 0}, 0.7f);
    const Vec3 xr_dir = yaw.rotate({0, 0, -1}), bl_dir = xr::to_blendity(yaw).rotate({0, 0, 1});
    CHECK(length(xr::to_blendity(xr_dir) - bl_dir) < 1e-5f);
    /* An eye 1.6 m up at a stage on the floor below (2, 3) facing +Z sees a point 5 m ahead at its centre, 5 m deep. */
    const Mat4 stage = xr::Stage{Vec3(2, 0, 3), 0.0f}.matrix();
    const Mat4 view = xr::eye_view(stage, {Quat(), Vec3(0, 1.6f, 0)});
    const Vec3 ahead = view.point({2, 1.6f, 8});
    std::printf("    a point 5 m ahead in the eye's view: (%.4f, %.4f, %.4f)\n", ahead.x, ahead.y, ahead.z);
    CHECK(length(ahead - Vec3(0, 0, 5)) < 1e-4f);
    /* The world at 1:2 - the eyes twice as high in it. */
    const Vec3 tall = xr::eye_position(xr::Stage{Vec3(), 0.0f, 2.0f}.matrix(), {Quat(), Vec3(0, 1.6f, 0)});
    CHECK(std::fabs(tall.y - 3.2f) < 1e-5f);
  });
}

static void vr_session_tests() {
  test("VR's session (round 383): Ready begins it, each frame waits, begins, locates both eyes, draws and submits each, and ends with the layer; a frame the runtime doesn't want drawn ends empty; Stopping ends the session, Exiting lets it go; a lost runtime says why; no headset at the start says why", [] {
    auto stub = std::make_unique<StubRuntime>();
    StubRuntime *rt = stub.get();
    xr::Session s(std::move(stub));
    s.resolution = 0.5f;
    std::string why;
    CHECK(s.start(why) && s.state() == xr::Session::State::Starting && rt->scale == 0.5f);
    int drawn = 0;
    const xr::Session::DrawEye draw = [&](int, const Mat4 &, const Mat4 &, Vec3, int w, int h, uint32_t *px) {
      drawn++;
      for (int i = 0; i < w * h; i++) px[i] = 0xFF112233u;
    };
    s.tick(Mat4::identity(), draw);
    CHECK(s.state() == xr::Session::State::Starting && s.frames() == 0);  // (no Ready yet: no frames)
    rt->events.push_back(xr::Runtime::Event::Ready);
    s.tick(Mat4::identity(), draw);
    std::printf("    after Ready: %s, %llu frame(s), %d eye(s) drawn; calls:", xr::state_name(s.state()), (unsigned long long)s.frames(), drawn);
    for (const std::string &c : rt->calls) std::printf(" %s", c.c_str());
    std::printf("\n");
    CHECK(s.state() == xr::Session::State::Running && s.frames() == 1 && s.rendered() == 1 && drawn == 2);
    CHECK(count_of(rt->calls, "begin_session") == 1 && count_of(rt->calls, "submit_eye") == 2 && count_of(rt->calls, "end_frame drawn") == 1);
    CHECK(rt->eye_pixels[1].size() == 64u * 64u && rt->eye_pixels[1][0] == 0xFF112233u);
    rt->render = false;
    s.tick(Mat4::identity(), draw);
    CHECK(s.frames() == 2 && s.rendered() == 1 && drawn == 2 && count_of(rt->calls, "end_frame empty") == 1);
    rt->events.push_back(xr::Runtime::Event::Stopping);
    s.tick(Mat4::identity(), draw);
    CHECK(s.state() == xr::Session::State::Stopping && count_of(rt->calls, "end_session") == 1);
    /* Ready again (the headset woke): the session begins again and frames go on (review 383). */
    rt->render = true;
    rt->events.push_back(xr::Runtime::Event::Ready);
    s.tick(Mat4::identity(), draw);
    CHECK(s.state() == xr::Session::State::Running && count_of(rt->calls, "begin_session") == 2 && s.rendered() == 2);
    rt->events.push_back(xr::Runtime::Event::Stopping);
    s.tick(Mat4::identity(), draw);
    rt->events.push_back(xr::Runtime::Event::Exiting);
    s.tick(Mat4::identity(), draw);
    CHECK(s.state() == xr::Session::State::Off && count_of(rt->calls, "destroy") == 1);
    /* The user's stop while running: let go at once, no xrEndSession outside Stopping (review 383). */
    auto stub4 = std::make_unique<StubRuntime>();
    StubRuntime *rt4 = stub4.get();
    xr::Session running(std::move(stub4));
    CHECK(running.start(why));
    rt4->events = {xr::Runtime::Event::Ready};
    running.tick(Mat4::identity(), draw);
    running.stop();
    CHECK(running.state() == xr::Session::State::Off && count_of(rt4->calls, "end_session") == 0 && count_of(rt4->calls, "destroy") == 1);
    /* Lost: a loss pending, or a frame the runtime won't wait for. */
    auto stub2 = std::make_unique<StubRuntime>();
    StubRuntime *rt2 = stub2.get();
    xr::Session lost(std::move(stub2));
    CHECK(lost.start(why));
    rt2->events = {xr::Runtime::Event::Ready};
    lost.tick(Mat4::identity(), draw);
    rt2->wait_ok = false;
    lost.tick(Mat4::identity(), draw);
    CHECK(lost.state() == xr::Session::State::Lost && !lost.why().empty());
    /* No headset at the start. */
    auto stub3 = std::make_unique<StubRuntime>();
    stub3->create_ok = false;
    xr::Session none(std::move(stub3));
    std::string none_why;
    CHECK(!none.start(none_why) && none_why == "no headset (stub)" && none.state() == xr::Session::State::Off);
  });
  test("VR View's stereo frame (round 383): a red cube 3 m ahead of a stage below the Scene view's pivot - both eyes see it at the same height, shifted by the disparity 64 mm between the eyes gives (about 5.5 pixels at 512 wide and a 90-degree field)", [] {
    Editor ed;
    ed.init_headless(640, 400);
    auto stub = std::make_unique<StubRuntime>();
    StubRuntime *rt = stub.get();
    rt->w = rt->h = 512;
    std::string why;
    CHECK(ed.vr_start_for_test(std::move(stub), why));
    const Mat4 stage = ed.vr_stage_for_test();
    ed.command("create Cube");
    GameObject *cube = ed.active_object_for_test();
    CHECK(cube != nullptr);
    if (!cube) return;
    cube->set_local_position(stage.point(xr::to_blendity(Vec3(0, 1.9f, -3.0f))));  // (30 cm above the eyes: up must be up in the picture)
    cube->set_local_scale(Vec3(0.3f, 0.3f, 0.3f));
    cube->mesh_renderer()->main_material();  // (its own material, to colour)
    cube->mesh_renderer()->materials[0]->base_color = Vec3(1, 0, 0);
    cube->mesh_renderer()->materials[0]->touch();
    rt->events.push_back(xr::Runtime::Event::Ready);
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    const Vec2 l = red_centre(rt->eye_pixels[0], 512, 512), r = red_centre(rt->eye_pixels[1], 512, 512);
    const float expect = 0.064f / 3.0f * 256.0f;  // (each eye's shift: half the 64 mm over 3 m, times the projection's 1 at 90 degrees, times half the width - both eyes' apart)
    std::printf("    the cube's centre: left eye (%.2f, %.2f), right eye (%.2f, %.2f); apart %.2f px, expected %.2f; %llu frames drawn\n", l.x, l.y, r.x, r.y, l.x - r.x, expect,
                (unsigned long long)(ed.vr_for_test() ? ed.vr_for_test()->rendered() : 0));
    CHECK(l.x > 0 && r.x > 0);
    CHECK(std::fabs((l.x - r.x) - expect) < 1.0f && std::fabs(l.y - r.y) < 0.5f);
    CHECK(std::fabs(l.x - 256.0f - expect / 2) < 1.5f);  // (the left eye sees it right of centre)
    const float above = 0.3f / 3.0f * 256.0f;  // (rows are top down: 30 cm up at 3 m is this many rows above the middle)
    std::printf("    the cube's height: row %.2f, expected %.2f\n", l.y, 255.5f - above);
    CHECK(std::fabs(l.y - (255.5f - above)) < 1.5f);
    CHECK(ed.vr_wants_frames_for_test());  // (the editor draws every frame while VR runs - idle, it would draw one a second)
    ed.command("vr stop");
    CHECK(ed.vr_for_test() == nullptr);
  });
  test("VR View's cost (review 383): a frame of two eyes at half an Index's size (1008 x 1120 each, drawn as the Game view draws) - measured on the main thread", [] {
    Editor ed;
    ed.init_headless(640, 400);
    auto stub = std::make_unique<StubRuntime>();
    StubRuntime *rt = stub.get();
    rt->w = 1008, rt->h = 1120;
    std::string why;
    CHECK(ed.vr_start_for_test(std::move(stub), why));
    rt->events.push_back(xr::Runtime::Event::Ready);
    ed.step_frame_headless();  // (the first: caches made)
    double worst = 0.0, total = 0.0;
    for (int i = 0; i < 5; i++) {
      const auto t0 = std::chrono::steady_clock::now();
      ed.step_frame_headless();
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      total += ms, worst = std::max(worst, ms);
    }
    std::printf("    a frame with both eyes at 1008 x 1120: %.1f ms on average, %.1f ms at most (the editor's frame, both eyes in it)\n", total / 5, worst);
    CHECK(ed.vr_for_test() && ed.vr_for_test()->rendered() >= 6);
  });
  test("VR View's verbs (round 383): vr status says why it's off; vr start without a runtime (no OpenXR here, or no headset) says why and stays off; vr here and vr scale set the stage", [] {
    Editor ed;
    ed.init_headless(640, 400);
    std::vector<LogEntry> log;
    const size_t since = Log::fetch(0, log);
    ed.command("vr status");
    ed.command("vr scale 2");
    log.clear();
    Log::fetch(since, log);
    bool off = false, scaled = false;
    for (const LogEntry &e : log) off = off || e.text.find("VR View: off") == 0, scaled = scaled || e.text.find("the world at 1:2") != std::string::npos;
    CHECK(off && scaled);
    const Mat4 s = ed.vr_stage_for_test();
    CHECK(std::fabs(s.point({0, 1, 0}).y - 2.0f) < 1e-5f);
    std::string why;
    std::unique_ptr<xr::Runtime> rt = xr::open_runtime(why);
    std::printf("    this build's runtime: %s%s\n", rt ? "the OpenXR loader (not started here: it would start SteamVR)" : "none - ", rt ? "" : why.c_str());
#if !defined(BL_WITH_OPENXR)
    CHECK(!rt && !why.empty());
    ed.command("vr start");
    CHECK(ed.vr_for_test() == nullptr);
#endif
  });
  test("the lesson's Try-it (round 383): vr status - VR View's state and why", [] {
    Editor ed;
    ed.init_headless(640, 400);
    std::vector<LogEntry> log;
    const size_t since = Log::fetch(0, log);
    ed.command("vr status");
    log.clear();
    Log::fetch(since, log);
    CHECK(!log.empty() && log.back().text.find("VR View") == 0);
  });
}

static xr::Hand held_at(Vec3 p, float squeeze = 0.0f) {
  xr::Hand h;
  h.tracked = true;
  h.aim = h.grip = {Quat(), p};
  h.squeeze = squeeze;
  return h;
}

static void vr_nav_tests() {
  test("VR's moving around (round 384): two hands pulled to half their distance scale the world 1:2 about their middle, which stays put; turned a quarter about the vertical, it turns; one hand moves it with the hand; near 1:1 it snaps; the stick walks where you look; a snap turn keeps the head where it was; the tape measures in the world's metres", [] {
    const xr::Stage s0{Vec3(3, 0, -2), 20.0f, 1.0f};
    const Vec3 a0(-0.2f, 1.2f, -0.5f), b0(0.2f, 1.2f, -0.5f);
    const xr::Grab g = xr::grab_begin(s0, a0, &b0);
    const Vec3 a1(-0.1f, 1.2f, -0.5f), b1(0.1f, 1.2f, -0.5f);
    const xr::Stage s1 = xr::grab_update(g, a1, &b1);
    const Vec3 mid0 = xr::stage_point(s0, (a0 + b0) * 0.5f), mid1 = xr::stage_point(s1, (a1 + b1) * 0.5f);
    std::printf("    hands to half their distance: 1:%.3f, yaw %.2f; the grabbed point moved %.6f m\n", s1.scale, s1.yaw, length(mid1 - mid0));
    CHECK(std::fabs(s1.scale - 2.0f) < 1e-4f && std::fabs(s1.yaw - 20.0f) < 1e-3f && length(mid1 - mid0) < 1e-4f);
    /* A quarter turn of the hands about their middle (seen from above): the world turns with them. */
    const Vec3 a2(0.0f, 1.2f, -0.7f), b2(0.0f, 1.2f, -0.3f);
    const xr::Stage s2 = xr::grab_update(g, a2, &b2);
    const Vec3 mid2 = xr::stage_point(s2, (a2 + b2) * 0.5f);
    std::printf("    a quarter turn of the hands: yaw %.2f (from 20), 1:%.3f, the grabbed point moved %.6f m\n", s2.yaw, s2.scale, length(mid2 - mid0));
    CHECK(std::fabs(std::fabs(s2.yaw - 20.0f) - 90.0f) < 1e-2f && std::fabs(s2.scale - 1.0f) < 1e-4f && length(mid2 - mid0) < 1e-4f);
    /* The grabbed point stays where the hands hold it: the world point under the left hand at the start is under it now. */
    const Vec3 w0 = xr::stage_point(s0, a0), w2 = xr::stage_point(s2, a2);
    CHECK(length(w2 - w0) < 1e-3f);
    /* Snapping: 1:1.03 is 1:1. */
    CHECK(xr::snap_scale(1.03f) == 1.0f && xr::snap_scale(1.2f) == 1.2f);
    /* One hand: moved half a metre right in the stage, the world moves with it. */
    const xr::Grab one = xr::grab_begin(s0, a0, nullptr);
    const xr::Stage s4 = xr::grab_update(one, a0 + Vec3(0.5f, 0, 0), nullptr);
    CHECK(length(xr::stage_point(s4, a0 + Vec3(0.5f, 0, 0)) - xr::stage_point(s0, a0)) < 1e-4f && s4.scale == 1.0f && s4.yaw == s0.yaw);
    /* The stick: a second fully forward at 2 m/s, looking along the stage's -Z (the world's yaw 20) - 2 m that way, level. */
    const xr::Stage s5 = xr::smooth_move(s0, Quat(), Vec2(0, 1), 2.0f, 1.0f);
    const Vec3 walked = s5.origin - s0.origin;
    const Vec3 want = Quat::euler({0, 20.0f, 0}).rotate({0, 0, 1}) * 2.0f;
    CHECK(length(walked - want) < 1e-4f);
    /* A snap turn: the head stays put in the world, the stage turns 30 degrees. */
    const Vec3 head(0.3f, 1.6f, 0.4f);
    const xr::Stage s6 = xr::snap_turn(s0, head, 30.0f);
    CHECK(std::fabs(s6.yaw - 50.0f) < 1e-4f && length(xr::stage_point(s6, head) - xr::stage_point(s0, head)) < 1e-4f);
    /* The tape: 0.4 m between the hands, 0.8 m in a world at 1:2. */
    CHECK(std::fabs(xr::measure(s0, a0, b0) - 0.4f) < 1e-5f && std::fabs(xr::measure(s1, a0, b0) - 0.8f) < 1e-4f);
  });
  test("VR's teleport (round 384): an aim 30 degrees down from 1.2 m at 7 m/s lands on the ground where a throw would; a box in the way stops it on the box; teleporting puts the head over the landing, the feet at its height", [] {
    const xr::Stage s{Vec3(0, 0, 0), 0.0f, 1.0f};
    xr::Hand h = held_at(Vec3(0, 1.2f, 0));
    h.aim.orientation = Quat::axis_angle({1, 0, 0}, -0.5235988f);  // (OpenXR: -Z ahead; turned 30 degrees down about +X)
    const xr::Arc arc = xr::teleport_arc(s, h, nullptr, 7.0f, 9.8f, 200, 0.005f);
    /* A throw from 1.2 m at 7 m/s, 30 degrees down: vy = -3.5, vx = 6.062; 1.2 + 3.5 t + 4.9 t^2 = 0 has t = 0.2286 s, so x = 1.386 m. */
    const float t = (-3.5f + std::sqrt(3.5f * 3.5f + 4 * 4.9f * 1.2f)) / (2 * 4.9f);
    const float expect = 7.0f * std::cos(0.5235988f) * t;
    std::printf("    the arc landed %d at (%.3f, %.3f, %.3f), %.3f m ahead (a throw: %.3f)\n", (int)arc.landed, arc.at.x, arc.at.y, arc.at.z, arc.at.z, expect);
    CHECK(arc.landed && std::fabs(arc.at.y) < 1e-4f && std::fabs(arc.at.z - expect) < 0.02f && std::fabs(arc.at.x) < 1e-4f);
    /* A box top at 0.5 m from 0.5 to 1 m ahead: the arc stops on it. */
    const xr::Arc on_box = xr::teleport_arc(s, h, [](Vec3 a, Vec3 b, Vec3 &hit, Vec3 &) {
      if (!(a.y > 0.5f && b.y <= 0.5f)) return false;
      const Vec3 p = a + (b - a) * ((a.y - 0.5f) / (a.y - b.y));
      if (p.z < 0.5f || p.z > 1.0f) return false;
      hit = p;
      return true;
    }, 7.0f, 9.8f, 200, 0.005f);
    CHECK(on_box.landed && std::fabs(on_box.at.y - 0.5f) < 1e-4f && on_box.at.z > 0.5f && on_box.at.z < 1.0f);
    const Vec3 head(0.1f, 1.6f, 0.2f);
    const xr::Stage moved = xr::teleport(s, head, on_box.at);
    const Vec3 hw = xr::stage_point(moved, head);
    CHECK(std::fabs(hw.x - on_box.at.x) < 1e-4f && std::fabs(hw.z - on_box.at.z) < 1e-4f && std::fabs(moved.origin.y - 0.5f) < 1e-5f);
    /* A wall in the way (its normal level): the arc stops on it without landing. */
    const xr::Arc on_wall = xr::teleport_arc(s, h, [](Vec3 a, Vec3 b, Vec3 &hit, Vec3 &n) {
      if (!(a.z < 0.8f && b.z >= 0.8f)) return false;
      hit = a + (b - a) * ((0.8f - a.z) / (b.z - a.z)), n = Vec3(0, 0, -1);
      return true;
    }, 7.0f, 9.8f, 200, 0.005f);
    CHECK(!on_wall.landed && std::fabs(on_wall.points.back().z - 0.8f) < 1e-4f);
  });
  test("VR's stage with a seated runtime's lift (round 384 review): the eyes, the hands, a snap turn, both grabs and a teleport all go through the same lift - the grabbed point stays under the hands, the head stays put through a turn and stands over the landing", [] {
    const xr::Stage s0{Vec3(1, 0, 2), 40.0f, 2.0f, 1.6f};
    const Vec3 head(0.2f, 0.0f, -0.3f);  // (a seated runtime: the head near its origin)
    CHECK(std::fabs(xr::stage_point(s0, head).y - 3.2f) < 1e-4f);
    const Vec3 eye = xr::eye_position(s0.matrix(), {Quat(), head});
    CHECK(length(eye - xr::stage_point(s0, head)) < 1e-4f);
    const xr::Stage turned = xr::snap_turn(s0, head, 30.0f);
    CHECK(length(xr::stage_point(turned, head) - xr::stage_point(s0, head)) < 1e-4f);
    const Vec3 a0(-0.2f, -0.4f, -0.5f), b0(0.2f, -0.4f, -0.5f), a1(-0.1f, -0.3f, -0.4f), b1(0.1f, -0.3f, -0.4f);
    const xr::Stage one = xr::grab_update(xr::grab_begin(s0, a0, nullptr), a1, nullptr);
    CHECK(length(xr::stage_point(one, a1) - xr::stage_point(s0, a0)) < 1e-4f);
    const xr::Stage two = xr::grab_update(xr::grab_begin(s0, a0, &b0), a1, &b1);
    std::printf("    lifted 1.6 m at 1:2: the grabbed middle moved %.6f m, 1:%.3f\n", length(xr::stage_point(two, (a1 + b1) * 0.5f) - xr::stage_point(s0, (a0 + b0) * 0.5f)), two.scale);
    CHECK(length(xr::stage_point(two, (a1 + b1) * 0.5f) - xr::stage_point(s0, (a0 + b0) * 0.5f)) < 1e-4f && std::fabs(two.scale - 4.0f) < 1e-3f);
    const xr::Stage moved = xr::teleport(s0, head, Vec3(5, 0.5f, 6));
    const Vec3 hw = xr::stage_point(moved, head);
    CHECK(std::fabs(hw.x - 5) < 1e-4f && std::fabs(hw.z - 6) < 1e-4f && std::fabs(moved.origin.y - 0.5f) < 1e-5f);
  });
  test("VR's grabs at their edges (round 384 review): one hand then the other joins - a new two-hand grab from there keeps the middle put; hands that start together grab as one; one hand above the other turns nothing; the scale stays within 1:0.01 to 1:100", [] {
    const xr::Stage s0{Vec3(0, 0, 0), 0.0f, 1.0f};
    const Vec3 a0(-0.2f, 1.2f, -0.5f), b0(0.2f, 1.2f, -0.5f);
    const xr::Stage s1 = xr::grab_update(xr::grab_begin(s0, a0, nullptr), a0 + Vec3(0.3f, 0, 0), nullptr);
    const Vec3 a1 = a0 + Vec3(0.3f, 0, 0), b1 = b0 + Vec3(0.3f, 0, 0);
    const xr::Grab both = xr::grab_begin(s1, a1, &b1);
    const Vec3 a2 = a1 + Vec3(0.05f, 0, 0), b2 = b1 - Vec3(0.05f, 0, 0);
    const xr::Stage s2 = xr::grab_update(both, a2, &b2);
    CHECK(length(xr::stage_point(s2, (a2 + b2) * 0.5f) - xr::stage_point(s1, (a1 + b1) * 0.5f)) < 1e-4f && std::fabs(s2.scale - 4.0f / 3.0f) < 1e-3f);
    /* Started 1 cm apart: a one-hand grab - no turn, no scale, however they move. */
    const Vec3 c0(0.0f, 1.2f, -0.5f), d0(0.01f, 1.2f, -0.5f);
    const Vec3 d1(0.0f, 1.2f, 0.2f);
    const xr::Stage s3 = xr::grab_update(xr::grab_begin(s0, c0, &d0), c0, &d1);
    CHECK(s3.scale == 1.0f && s3.yaw == 0.0f);
    /* One hand straight above the other, then twisted: no turn (the heading between them is noise). */
    const Vec3 e0(0.0f, 1.0f, -0.5f), f0(0.0f, 1.4f, -0.5f), f1(0.02f, 1.4f, -0.49f);
    const xr::Stage s4 = xr::grab_update(xr::grab_begin(s0, e0, &f0), e0, &f1);
    CHECK(s4.yaw == 0.0f);
    /* Frame by frame: a quarter turn, then one hand lifted over the other - the world keeps its turn (re-check 384: it
     * had jumped back to the start's), and turns on from there as the hands spread again. */
    xr::Grab live = xr::grab_begin(s0, a0, &b0);
    const Vec3 ta(0.0f, 1.2f, -0.7f), tb(0.0f, 1.2f, -0.3f), oa(0.0f, 1.0f, -0.5f);
    Vec3 ob(0.01f, 1.4f, -0.5f);
    const float turned = xr::grab_update(live, ta, &tb).yaw;
    const xr::Stage over = xr::grab_update(live, oa, &ob);
    const xr::Stage spread = xr::grab_update(live, oa, &(ob = Vec3(0.3f, 1.4f, -0.5f), ob));
    std::printf("    turned %.1f; a hand over the other: %.1f; spread again: %.1f\n", turned, over.yaw, spread.yaw);
    CHECK(std::fabs(std::fabs(turned) - 90.0f) < 1e-2f && std::fabs(over.yaw - turned) < 1e-3f && std::fabs(spread.yaw - turned) < 1e-3f);
    /* Pulled from 5 cm to 10 m apart: 1:0.01 at least; from 10 m to touching: at most 1:100. */
    const Vec3 g0(0.0f, 1.2f, 0.0f), h0(0.05f, 1.2f, 0.0f), h1(10.0f, 1.2f, 0.0f);
    const xr::Stage small = xr::grab_update(xr::grab_begin(s0, g0, &h0), g0, &h1);
    const xr::Stage big = xr::grab_update(xr::grab_begin(s0, g0, &h1), g0, &g0);
    std::printf("    pulled apart: 1:%.4f; pushed together: 1:%.2f\n", small.scale, big.scale);
    CHECK(small.scale >= xr::kMinScale && big.scale <= xr::kMaxScale && std::isfinite(big.origin.x));
  });
  test("VR's arc against a big mesh (round 384 review): a segment BVH finds the same first hits as every triangle tried, in a few node visits", [] {
    const int n = 300;  // (a 300 x 300 grid of bumps: 179,402 triangles)
    std::vector<Vec3> pos;
    std::vector<uint32_t> idx;
    for (int z = 0; z <= n; z++)
      for (int x = 0; x <= n; x++) pos.push_back({x * 0.1f, 0.2f * std::sin(x * 0.7f) * std::cos(z * 0.5f), z * 0.1f});
    for (int z = 0; z < n; z++)
      for (int x = 0; x < n; x++) {
        const uint32_t i = (uint32_t)(z * (n + 1) + x);
        idx.insert(idx.end(), {i, i + n + 1, i + 1, i + 1, i + n + 1, i + n + 2});
      }
    const double t0 = now_seconds();
    xr::SegmentBvh bvh;
    bvh.build(pos, idx);
    const double built = now_seconds() - t0;
    int agree = 0, hits = 0;
    double brute = 0.0, fast = 0.0;
    for (int k = 0; k < 40; k++) {
      const Vec3 a(0.5f + k * 0.7f, 1.0f, 3.0f + k * 0.5f), b = a + Vec3(0.3f, -1.5f, 0.2f);
      double c0 = now_seconds();
      float best = 2.0f;
      for (size_t t = 0; t + 2 < idx.size(); t += 3) {
        const Vec3 p0 = pos[idx[t]], e1 = pos[idx[t + 1]] - p0, e2 = pos[idx[t + 2]] - p0, d = b - a, hv = cross(d, e2);
        const float det = dot(e1, hv);
        if (std::fabs(det) < 1e-12f) continue;
        const Vec3 sv = a - p0;
        const float u = dot(sv, hv) / det;
        const Vec3 q = cross(sv, e1);
        const float v = dot(d, q) / det, f = dot(e2, q) / det;
        if (u >= 0 && v >= 0 && u + v <= 1 && f >= 0 && f <= 1 && f < best) best = f;
      }
      brute += now_seconds() - c0;
      c0 = now_seconds();
      float t = 0.0f;
      Vec3 nrm;
      const bool hit = bvh.first_hit(a, b, t, nrm);
      fast += now_seconds() - c0;
      hits += hit;
      agree += hit == (best <= 1.0f) && (!hit || (std::fabs(t - best) < 1e-5f && nrm.y > 0.0f));
    }
    std::printf("    %zu triangles: built in %.1f ms; 40 segments %d hits, %d agree; every triangle %.3f ms a segment, the BVH %.4f ms\n", idx.size() / 3, built * 1e3, hits, agree,
                brute / 40 * 1e3, fast / 40 * 1e3);
    CHECK(agree == 40 && hits > 30 && fast * 20 < brute);
  });
  test("VR View's hands in the editor (round 384): both squeezes pulled together scale the world 1:2; the right trigger held then let go teleports to where the arc lands; the right A button toggles the tape between the hands", [] {
    Editor ed;
    ed.init_headless(640, 400);
    auto stub = std::make_unique<StubRuntime>();
    StubRuntime *rt = stub.get();
    rt->w = rt->h = 64;
    std::string why;
    CHECK(ed.vr_start_for_test(std::move(stub), why));
    rt->events.push_back(xr::Runtime::Event::Ready);
    ed.step_frame_headless();
    const std::vector<uint32_t> bare = rt->eye_pixels[0];
    rt->hands[0] = held_at(Vec3(-0.2f, 1.2f, -0.5f), 1.0f), rt->hands[1] = held_at(Vec3(0.2f, 1.2f, -0.5f), 1.0f);
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    int drawn = 0;  // (the controllers and their rays drawn into the eye)
    for (size_t i = 0; i < bare.size() && i < rt->eye_pixels[0].size(); i++) drawn += bare[i] != rt->eye_pixels[0][i];
    std::printf("    the hands drew %d of the eye's %zu pixels\n", drawn, bare.size());
    CHECK(drawn > 20);
    rt->hands[0] = held_at(Vec3(-0.1f, 1.2f, -0.5f), 1.0f), rt->hands[1] = held_at(Vec3(0.1f, 1.2f, -0.5f), 1.0f);
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    const float scaled = ed.vr_stage_value_for_test().scale;
    rt->hands[0].squeeze = rt->hands[1].squeeze = 0.0f;
    ed.step_frame_headless();
    ed.command("vr scale 1");
    ed.command("vr here");  // (the grab scaled the world about the hands: back on the ground before the teleport)
    const Vec3 before = ed.vr_stage_value_for_test().origin;
    rt->hands[1] = held_at(Vec3(0, 1.2f, 0));
    rt->hands[1].aim.orientation = Quat::axis_angle({1, 0, 0}, -0.5235988f);
    rt->hands[1].trigger = 1.0f;
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    const bool arc = ed.vr_arc_for_test().landed;
    const Vec3 landing = ed.vr_arc_for_test().at;
    rt->hands[1].trigger = 0.0f;
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    const Vec3 after = ed.vr_stage_value_for_test().origin;
    rt->hands[1].primary = true;
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    rt->hands[1].primary = false;
    ed.step_frame_headless();
    std::printf("    pulled together: 1:%.3f; the arc landed %d, the stage moved %.3f m; the tape %d at %.3f m\n", scaled, (int)arc, length(after - before),
                (int)ed.vr_tape_for_test(), ed.vr_tape_length_for_test());
    CHECK(std::fabs(scaled - 2.0f) < 1e-3f && arc && length(after - before) > 0.5f);
    const Vec3 head_w = xr::stage_point(ed.vr_stage_value_for_test(), ed.vr_for_test()->head().position);
    CHECK(std::fabs(head_w.x - landing.x) < 1e-3f && std::fabs(head_w.z - landing.z) < 1e-3f);
    CHECK(ed.vr_tape_for_test() && std::fabs(ed.vr_tape_length_for_test() - xr::measure(ed.vr_stage_value_for_test(), rt->hands[0].grip.position, rt->hands[1].grip.position)) < 1e-4f);
    ed.command("vr stop");
  });
  test("VR View's hands, cancelled and on time (round 384 review): a squeeze while aiming cancels the teleport, so does the hand losing tracking; a grab moves the world on the frame its hands were found", [] {
    Editor ed;
    ed.init_headless(640, 400);
    auto stub = std::make_unique<StubRuntime>();
    StubRuntime *rt = stub.get();
    rt->w = rt->h = 32;
    std::string why;
    CHECK(ed.vr_start_for_test(std::move(stub), why));
    rt->events.push_back(xr::Runtime::Event::Ready);
    ed.step_frame_headless();
    auto aim = [&](float trigger) {
      rt->hands[1] = held_at(Vec3(0, 1.2f, 0));
      rt->hands[1].aim.orientation = Quat::axis_angle({1, 0, 0}, -0.5235988f);
      rt->hands[1].trigger = trigger;
    };
    const Vec3 start = ed.vr_stage_value_for_test().origin;
    aim(1.0f);
    ed.step_frame_headless();
    const bool aimed = ed.vr_arc_for_test().landed;
    rt->hands[0] = held_at(Vec3(-0.3f, 1.2f, -0.3f), 1.0f);  // (the left hand squeezes while the trigger is still held)
    ed.step_frame_headless();
    aim(0.0f);
    rt->hands[0].squeeze = 0.0f;
    ed.step_frame_headless();
    const bool squeeze_cancelled = length(ed.vr_stage_value_for_test().origin - start) < 1e-5f;
    aim(1.0f);
    ed.step_frame_headless();
    rt->hands[1].tracked = false;  // (tracking lost, then found with the trigger let go)
    ed.step_frame_headless();
    aim(0.0f);
    ed.step_frame_headless();
    const bool lost_cancelled = length(ed.vr_stage_value_for_test().origin - start) < 1e-5f;
    /* Both hands squeeze, then come together: the world is 1:2 on that same frame. */
    rt->hands[0] = held_at(Vec3(-0.2f, 1.2f, -0.5f), 1.0f), rt->hands[1] = held_at(Vec3(0.2f, 1.2f, -0.5f), 1.0f);
    ed.step_frame_headless();
    rt->hands[0] = held_at(Vec3(-0.1f, 1.2f, -0.5f), 1.0f), rt->hands[1] = held_at(Vec3(0.1f, 1.2f, -0.5f), 1.0f);
    ed.step_frame_headless();
    /* The eye drawn on that frame saw the world at 1:2 (re-check 384: the stored stage had been right while the drawn one lagged). */
    const std::vector<xr::Eye> &eyes = ed.vr_for_test()->last_eyes();
    const Mat4 drawn = ed.vr_last_view_for_test(), want = xr::eye_view(ed.vr_stage_for_test(), eyes.back().pose);
    float off = 0.0f;
    for (int k = 0; k < 16; k++) off = std::max(off, std::fabs(drawn.m[k] - want.m[k]));
    std::printf("    aimed %d; a squeeze cancelled %d, lost tracking cancelled %d; 1:%.3f on the frame the hands came together, the drawn view off by %.2g\n", (int)aimed,
                (int)squeeze_cancelled, (int)lost_cancelled, ed.vr_stage_value_for_test().scale, off);
    CHECK(aimed && squeeze_cancelled && lost_cancelled && std::fabs(ed.vr_stage_value_for_test().scale - 2.0f) < 1e-3f && off < 1e-5f);
    /* A wide low box ahead: once its BVH lands (made on a worker), the arc stands on its top. */
    rt->hands[0] = {}, rt->hands[1] = {};
    ed.step_frame_headless();
    ed.command("vr scale 1");
    ed.command("vr here");
    ed.scene().for_each([](GameObject &g) {  // (the default scene's meshes out of the way)
      if (auto *mr = g.get<MeshRenderer>()) mr->enabled = false;
    });
    ed.command("create Cube");
    GameObject *box = ed.active_object_for_test();
    CHECK(box != nullptr);
    if (!box) return;
    const Mat4 stage = ed.vr_stage_value_for_test().matrix();
    box->set_local_position(stage.point(xr::to_blendity(Vec3(0, 0, -1.5f))));
    box->set_local_scale(Vec3(2.0f, 0.5f, 2.0f));
    const float top = box->world_bounds().max.y;
    aim(1.0f);
    const double t0 = now_seconds();
    for (int i = 0; i < 400 && std::fabs(ed.vr_arc_for_test().at.y - top) > 1e-3f; i++) {
      ed.step_frame_headless();
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::printf("    the box's top at %.3f m: the arc landed %d at %.3f m, %.0f ms after the first aim\n", top, (int)ed.vr_arc_for_test().landed, ed.vr_arc_for_test().at.y,
                (now_seconds() - t0) * 1e3);
    CHECK(ed.vr_arc_for_test().landed && std::fabs(ed.vr_arc_for_test().at.y - top) < 1e-3f);
    auto settle = [&](const std::function<bool()> &done) {  // (a frame at least: the last arc is the old scene's)
      for (int i = 0; i < 400; i++) {
        ed.step_frame_headless();
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      return false;
    };
    /* The box's own mesh moved up in place (a sculpt or a deform: deform_version, not version) - the arc follows. */
    auto own = std::make_shared<Mesh>(*box->mesh_filter()->mesh);
    box->mesh_filter()->mesh = own;
    for (Vec3 &p : own->positions) p.y += 0.4f;  // (0.2 m in the world at the box's 0.5 height)
    own->deform_version++;
    const bool followed = settle([&] { return std::fabs(ed.vr_arc_for_test().at.y - (top + 0.2f)) < 1e-3f; });
    std::printf("    the box's mesh deformed 0.2 m up: the arc at %.3f m (followed %d)\n", ed.vr_arc_for_test().at.y, (int)followed);
    CHECK(followed);
    /* A 40-degree ramp: it lands (its normal 0.77 up); stretched 1.5 times upright it is 51.5 degrees (0.62 up) - the
     * normal through the inverse transpose says so, the plain matrix would not. */
    const xr::Stage st = ed.vr_stage_value_for_test();
    const Vec3 f = normalize(xr::stage_dir(st, Vec3(0, 0, -1))), up(0, 1, 0), side = normalize(cross(f, up));
    const float rise = std::tan(40.0f * 0.01745329f);
    auto ramp = std::make_shared<Mesh>();
    const uint32_t v0 = ramp->add_vert(f * -1.0f - side - up * rise), v1 = ramp->add_vert(f * -1.0f + side - up * rise), v2 = ramp->add_vert(f + side + up * rise),
                   v3 = ramp->add_vert(f - side + up * rise);
    ramp->add_face({v0, v1, v2, v3});
    ramp->sync_attributes();
    ramp->touch();
    box->mesh_filter()->mesh = ramp;
    const Vec3 centre = xr::stage_point(st, Vec3(0, 0, -1.5f));
    box->set_local_position(centre), box->set_local_scale(Vec3(1, 1, 1));
    auto on_plane = [&](Vec3 p) { return std::fabs(p.y - centre.y - dot(p - centre, f) * rise) < 1e-3f && std::fabs(p.y) > 1e-3f; };
    const bool on_ramp = settle([&] { return ed.vr_arc_for_test().landed && on_plane(ed.vr_arc_for_test().at); });
    const Vec3 ramp_at = ed.vr_arc_for_test().at;
    box->set_local_scale(Vec3(1, 1.5f, 1));
    const bool refused = settle([&] { return !ed.vr_arc_for_test().landed && !ed.vr_arc_for_test().points.empty(); });
    const Vec3 end = ed.vr_arc_for_test().points.empty() ? Vec3() : ed.vr_arc_for_test().points.back();
    const bool stopped_on_ramp = std::fabs(end.y - centre.y - dot(end - centre, f) * rise * 1.5f) < 1e-3f;
    std::printf("    a 40-degree ramp: landed %d at y %.3f; stretched to 51.5 degrees: refused %d, stopped on it %d\n", (int)on_ramp, ramp_at.y, (int)refused,
                (int)stopped_on_ramp);
    CHECK(on_ramp && refused && stopped_on_ramp);
    ed.command("vr stop");
  });
}

/* A recorded stroke: 7 points 5 cm apart along +X at 1 m up, the pressure rising, the hand turning a quarter about the
 * path as it goes. */
static StrokeRibbon recorded_stroke(bool tube) {
  StrokeRibbon rb;
  rb.tube = tube, rb.material = 2, rb.width = 0.01f;
  for (int i = 0; i < 7; i++) {
    const float a = i / 6.0f * 1.5707963f;
    rb.points.push_back({i * 0.05f, 1.0f, 0.0f});
    rb.ups.push_back({0.0f, std::cos(a), std::sin(a)});
    rb.widths.push_back(0.004f + 0.001f * i);
  }
  rb.facing = rb.surface_normal = rb.ups[0];
  return rb;
}

static void vr_sketch_tests() {
  test("VR's strokes as ribbons (round 385): each point's width across the path, centred on it, in the plane its up is the normal of (the hand's turn); the faces on the stroke's material slot; the normals its ups", [] {
    Strokes s;
    s.ribbons.push_back(recorded_stroke(false));
    const MeshPtr m = s.build_mesh();
    const StrokeRibbon &rb = s.ribbons[0];
    CHECK(m->positions.size() == 14u && m->face_count() == 12u);
    float worst_width = 0.0f, worst_centre = 0.0f, worst_plane = 0.0f, worst_normal = 0.0f;
    for (size_t i = 0; i < 7; i++) {
      const Vec3 l = m->positions[i * 2], r = m->positions[i * 2 + 1];
      worst_width = std::max(worst_width, std::fabs(length(r - l) - rb.widths[i]));
      worst_centre = std::max(worst_centre, length((l + r) * 0.5f - rb.points[i]));
      worst_plane = std::max(worst_plane, std::fabs(dot(r - l, rb.ups[i])));
      worst_normal = std::max(worst_normal, length(m->custom_normals[i * 2] - rb.ups[i]));
    }
    std::printf("    widths off %.2g, centres off %.2g, across the up %.2g, normals off %.2g\n", worst_width, worst_centre, worst_plane, worst_normal);
    CHECK(worst_width < 1e-5f && worst_centre < 1e-5f && worst_plane < 1e-5f && worst_normal < 1e-5f);
    bool all_slot = m->face_material.size() == m->face_count();
    for (int32_t f : m->face_material) all_slot = all_slot && f == 2;
    CHECK(all_slot);
  });
  test("VR's strokes as tubes (round 385): a ring of 8 a point at half its width from the path, square to it, the normals out from the path; both ends capped, their normals along the path", [] {
    Strokes s;
    s.ribbons.push_back(recorded_stroke(true));
    const MeshPtr m = s.build_mesh();
    const StrokeRibbon &rb = s.ribbons[0];
    const size_t ring = kTubeSides + 1;  // (a seam vertex a ring)
    CHECK(m->positions.size() == 7 * ring + 2 * (1 + kTubeSides) && m->custom_normals.size() == m->positions.size());
    float worst_r = 0.0f, worst_square = 0.0f, worst_n = 0.0f;
    for (size_t i = 0; i < 7; i++)
      for (size_t k = 0; k < ring; k++) {
        const Vec3 off = m->positions[i * ring + k] - rb.points[i];
        worst_r = std::max(worst_r, std::fabs(length(off) - rb.widths[i] * 0.5f));
        worst_square = std::max(worst_square, std::fabs(off.x));
        worst_n = std::max(worst_n, length(m->custom_normals[i * ring + k] - normalize(off)));
      }
    const Vec3 cap0 = m->custom_normals[7 * ring], cap1 = m->custom_normals[7 * ring + 1 + kTubeSides];
    std::printf("    radii off %.2g, along the path %.2g, normals off %.2g; caps (%.2f, %.2f, %.2f) and (%.2f, %.2f, %.2f); %zu faces\n", worst_r, worst_square, worst_n,
                cap0.x, cap0.y, cap0.z, cap1.x, cap1.y, cap1.z, m->face_count());
    CHECK(worst_r < 1e-5f && worst_square < 1e-5f && worst_n < 1e-4f && cap0.x < -0.999f && cap1.x > 0.999f);
    CHECK(m->face_count() == 6u * kTubeSides + 2u * kTubeSides);
    /* Every face wound outward (review 385: the sides had faced in): its normal with its corners' normals. */
    int inward = 0;
    for (size_t f = 0; f < m->face_count(); f++) {
      Vec3 avg;
      for (uint32_t c = m->face_offsets[f]; c < m->face_offsets[f + 1]; c++) avg = avg + m->custom_normals[m->corner_verts[c]];
      inward += dot(m->face_normal(f), avg) <= 0.0f;
    }
    std::printf("    faces wound inward: %d of %zu\n", inward, m->face_count());
    CHECK(inward == 0);
  });
  test("VR's strokes along a hard path (review 385): a vertical stroke drawn with the hand level, its up jittering, and a half roll between two points - the ribbon never folds over (no bow-tie), the tube never twists, the ribbon's normals square to the path", [] {
    StrokeRibbon rb;
    rb.width = 0.01f;
    for (int i = 0; i < 12; i++) {
      rb.points.push_back({0.0f, 1.0f + i * 0.02f, 0.0f});
      const float j = 0.03f * std::sin(i * 2.3f);
      rb.ups.push_back(i == 6 ? Vec3(0.0f, 0.0f, -1.0f) : Vec3(j, 1.0f, i < 6 ? 0.5f + j : -0.5f - j));  // (the up leaning along the path, jittering; a half roll at 6)
    }
    rb.facing = rb.surface_normal = Vec3(0, 0, 1);
    for (bool tube : {false, true}) {
      rb.tube = tube;
      Strokes s;
      s.ribbons.push_back(rb);
      const MeshPtr m = s.build_mesh();
      const size_t per = tube ? kTubeSides + 1 : 2;
      float worst = 1.0f, worst_square = 0.0f;
      for (size_t i = 1; i < 12; i++) {
        const Vec3 a = normalize(m->positions[(i - 1) * per + (tube ? 0 : 1)] - rb.points[i - 1]), b = normalize(m->positions[i * per + (tube ? 0 : 1)] - rb.points[i]);
        worst = std::min(worst, dot(a, b));
        if (!tube) worst_square = std::max(worst_square, std::fabs(dot(m->custom_normals[i * 2], Vec3(0, 1, 0))));
      }
      std::printf("    %s: neighbouring points' frames at least %.3f alike (1: the same); normals along the path %.3f\n", tube ? "tube" : "ribbon", worst, worst_square);
      CHECK(worst > 0.9f && worst_square < 1e-4f);
    }
  });
  test("VR's tubes round a sharp corner (re-check 385): a V turning 30 degrees a step, the hand's up out of the turn's plane - each ring turns no more than the path does (parallel transport, no pinch)", [] {
    StrokeRibbon rb;
    rb.tube = true, rb.width = 0.01f;
    Vec3 p(0, 1, 0), d(1, 0, 0);
    for (int i = 0; i < 14; i++) {
      rb.points.push_back(p);
      rb.ups.push_back(Vec3(0.0f, 0.71f, 0.71f));
      if (i >= 5 && i < 10) d = Quat::axis_angle({0, 1, 0}, 0.5235988f).rotate(d);  // (30 degrees a step, five times)
      p = p + d * 0.0025f;
    }
    rb.facing = rb.surface_normal = Vec3(0, 1, 0);
    Strokes s;
    s.ribbons.push_back(rb);
    const MeshPtr m = s.build_mesh();
    const size_t per = kTubeSides + 1;
    float worst = 1.0f;
    for (size_t i = 1; i < rb.points.size(); i++)
      for (size_t k = 0; k < per; k++) worst = std::min(worst, dot(normalize(m->positions[(i - 1) * per + k] - rb.points[i - 1]), normalize(m->positions[i * per + k] - rb.points[i])));
    std::printf("    neighbouring rings' points at least %.3f alike (a 30-degree bend: cos 30 = 0.866)\n", worst);
    CHECK(worst > 0.85f);
  });
  test("VR's strokes added one at a time (review 385): the mesh with the last stroke added is the whole list's mesh; 500 tube strokes of 100 points - adding the last against building them all, measured", [] {
    Strokes s;
    MeshPtr m = std::make_shared<Mesh>();
    for (int k = 0; k < 3; k++) {
      StrokeRibbon rb = recorded_stroke(k == 1);
      for (Vec3 &p : rb.points) p.z += k * 0.1f;
      s.ribbons.push_back(rb);
      m = s.with_last_ribbon(*m);
    }
    const MeshPtr whole = s.build_mesh();
    bool same = m->positions.size() == whole->positions.size() && m->face_count() == whole->face_count() && m->custom_normals.size() == whole->custom_normals.size() &&
                m->face_material == whole->face_material && m->corner_verts == whole->corner_verts;
    for (size_t i = 0; same && i < m->positions.size(); i++) same = length(m->positions[i] - whole->positions[i]) < 1e-6f && length(m->custom_normals[i] - whole->custom_normals[i]) < 1e-6f;
    CHECK(same);
    Strokes big;
    for (int k = 0; k < 500; k++) {
      StrokeRibbon rb;
      rb.tube = true, rb.width = 0.01f, rb.facing = rb.surface_normal = Vec3(0, 1, 0);
      for (int i = 0; i < 100; i++) rb.points.push_back({i * 0.01f, std::sin(i * 0.1f + k) * 0.1f, k * 0.02f});
      big.ribbons.push_back(rb);
    }
    const StrokeRibbon last = big.ribbons.back();
    big.ribbons.pop_back();
    const MeshPtr before = big.build_mesh();
    big.ribbons.push_back(last);
    double t0 = now_seconds();
    const MeshPtr all = big.build_mesh();
    const double full = now_seconds() - t0;
    t0 = now_seconds();
    const MeshPtr added = big.with_last_ribbon(*before);
    const double add = now_seconds() - t0;
    std::printf("    500 tubes, %zu vertices: building them all %.1f ms, adding the last %.1f ms\n", all->positions.size(), full * 1e3, add * 1e3);
    CHECK(added->positions.size() == all->positions.size());  // (the time is the copy's: VR's object is split past 100,000 vertices to bound it)
  });
  test("VR's strokes in the scene file (round 385): a ribbon's widths, ups, tube and slot round-trip; a line from before them still loads", [] {
    Strokes s;
    s.ribbons.push_back(recorded_stroke(true));
    s.ribbons.push_back(recorded_stroke(false));
    Strokes back;
    CHECK(strokes_from_text(strokes_to_text(s), back) && back.ribbons.size() == 2u);
    if (back.ribbons.size() != 2u) return;
    const StrokeRibbon &a = back.ribbons[0], &b = back.ribbons[1];
    CHECK(a.tube && !b.tube && a.material == 2 && a.widths.size() == 7u && a.ups.size() == 7u && std::fabs(a.widths[6] - 0.01f) < 1e-6f && length(a.ups[6] - s.ribbons[0].ups[6]) < 1e-5f);
    Strokes old;
    CHECK(strokes_from_text("R 0 0 1 0 1 0 0.05 2 0 0 0 1 0 0\n", old) && old.ribbons.size() == 1u && old.ribbons[0].widths.empty() && !old.ribbons[0].tube &&
          old.ribbons[0].material == 0);
    /* A group cut short leaves the plain ribbon; a slot past the cap is held at it (review 385). */
    Strokes cut;
    CHECK(strokes_from_text("R 0 0 1 0 1 0 0.05 2 0 0 0 1 0 0 W 0.01\nR 0 0 1 0 1 0 0.05 2 0 0 0 1 0 0 M 2000000000\n", cut) && cut.ribbons.size() == 2u);
    if (cut.ribbons.size() == 2u) CHECK(cut.ribbons[0].widths.empty() && cut.ribbons[1].material == kMaxStrokeSlot);
  });
  test("VR sketching in the editor (round 385): with sketching on, the right trigger draws a stroke along the hand's path as it moves - its width the pressure, its colour the palette's; it never teleports; A undoes it; the left stick picks the colour, the right stick the brush; X makes the next a tube", [] {
    Editor ed;
    ed.init_headless(640, 400);
    auto stub = std::make_unique<StubRuntime>();
    StubRuntime *rt = stub.get();
    rt->w = rt->h = 32;
    std::string why;
    CHECK(ed.vr_start_for_test(std::move(stub), why));
    rt->events.push_back(xr::Runtime::Event::Ready);
    ed.step_frame_headless();
    ed.command("vr sketch on");
    ed.command("vr colour 3");
    CHECK(ed.vr_sketch_for_test() && ed.vr_colour_for_test() == 2);
    /* A with a desktop edit last: nothing undone (review 385). */
    ed.command("create Cube");
    GameObject *cube = ed.active_object_for_test();
    const uint64_t cube_id = cube ? cube->id : 0;
    ed.step_frame_headless();
    rt->hands[1] = held_at(Vec3(0, 1.0f, -0.4f));
    rt->hands[1].primary = true;
    ed.step_frame_headless();
    rt->hands[1].primary = false;
    ed.step_frame_headless();
    CHECK(cube_id && ed.scene().find(cube_id) != nullptr);
    const Vec3 origin = ed.vr_stage_value_for_test().origin;
    rt->hands[0] = held_at(Vec3(-0.3f, 1.0f, -0.3f));
    auto draw_path = [&](float y) {
      for (int i = 0; i <= 30; i++) {
        rt->hands[1] = held_at(Vec3(i * 0.01f, y, -0.4f));
        rt->hands[1].trigger = 0.2f + 0.8f * i / 30.0f;
        ed.step_frame_headless();
      }
      rt->hands[1].trigger = 0.0f;
      ed.step_frame_headless();
    };
    draw_path(1.2f);
    GameObject *g = ed.vr_sketch_object_for_test();
    Strokes *s = g ? g->get<Strokes>() : nullptr;
    CHECK(s && s->ribbons.size() == 1u);
    if (!s || s->ribbons.empty()) return;
    const StrokeRibbon &rb = s->ribbons[0];
    const Mat4 stage = ed.vr_stage_for_test();
    const Vec3 first = stage.point(xr::to_blendity(Vec3(0, 1.2f, -0.45f))), last = stage.point(xr::to_blendity(Vec3(0.3f, 1.2f, -0.45f)));
    bool rising = true;
    for (size_t i = 1; i < rb.widths.size(); i++) rising = rising && rb.widths[i] >= rb.widths[i - 1];
    MeshRenderer *mr = g->get<MeshRenderer>();
    const bool coloured = mr && rb.material < (int)mr->materials.size() && mr->materials[(size_t)rb.material]->name == "Sketch 3";
    std::printf("    a stroke of %zu points from (%.3f, %.3f, %.3f) to (%.3f, %.3f, %.3f); widths %.4f to %.4f m; slot %d (%s); the stage moved %.4f m\n", rb.points.size(),
                rb.points.front().x, rb.points.front().y, rb.points.front().z, rb.points.back().x, rb.points.back().y, rb.points.back().z, rb.widths.front(), rb.widths.back(),
                rb.material, coloured ? "Sketch 3" : "?", length(ed.vr_stage_value_for_test().origin - origin));
    CHECK(rb.points.size() >= 10u && length(rb.points.front() - first) < 1e-3f && length(rb.points.back() - last) < 1e-3f && rising && rb.widths.front() < rb.widths.back());
    CHECK(std::fabs(rb.widths.back() - 0.01f) < 1e-4f && coloured && length(ed.vr_stage_value_for_test().origin - origin) < 1e-6f && !rb.tube);
    CHECK(g->name == "VR Sketch" && s->normals == 0 && ed.strokeobj_object_for_test() != g);  // (VR's own object; the desktop tool's is apart)
    /* Its id taken by another Stroke object (an undo frees it; the desk's next takes it): the next VR stroke makes its own again. */
    g->name = "Strokes";
    const uint64_t taken = g->id;
    draw_path(1.3f);
    GameObject *again = ed.vr_sketch_object_for_test();
    std::printf("    the id taken by \"Strokes\": the next stroke on %s (id %llu, was %llu)\n", again ? again->name.c_str() : "nothing", again ? (unsigned long long)again->id : 0ull,
                (unsigned long long)taken);
    CHECK(again && again->id != taken && again->name == "VR Sketch" && g->get<Strokes>()->ribbons.size() == 1u);
    /* Full (here past 50 vertices - the first stroke has 62; 100,000 by default): the next stroke starts "VR Sketch 2". */
    ed.vr_sketch_chunk_for_test(50);
    draw_path(1.35f);
    GameObject *part = ed.vr_sketch_object_for_test();
    std::printf("    \"VR Sketch\" past its vertices: the next stroke on %s\n", part ? part->name.c_str() : "nothing");
    CHECK(part && part != again && part->name == "VR Sketch 2" && part->get<Strokes>()->ribbons.size() == 1u && again->get<Strokes>()->ribbons.size() == 1u);
    ed.vr_sketch_chunk_for_test(100000);
    g = part;
    s = g->get<Strokes>();
    /* A: undone. */
    rt->hands[1].primary = true;
    ed.step_frame_headless();
    rt->hands[1].primary = false;
    ed.step_frame_headless();
    g = ed.vr_sketch_object_for_test();
    s = g ? g->get<Strokes>() : nullptr;
    const size_t after_undo = s ? s->ribbons.size() : 0;
    /* The left stick flicked right then left then right: one colour on; the right stick up: a bigger brush. */
    for (float x : {1.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f}) {
      rt->hands[0].stick = Vec2(x, 0.0f);
      ed.step_frame_headless();
    }
    const int colour = ed.vr_colour_for_test();
    rt->hands[1].stick = Vec2(0.0f, 1.0f);
    ed.step_frame_headless();
    rt->hands[1].stick = Vec2();
    ed.step_frame_headless();
    const float brush = ed.vr_brush_for_test();
    /* X: tubes; another stroke. */
    rt->hands[0].primary = true;
    ed.step_frame_headless();
    rt->hands[0].primary = false;
    draw_path(1.4f);
    g = ed.vr_sketch_object_for_test();
    s = g ? g->get<Strokes>() : nullptr;
    std::printf("    after A: %zu strokes; the colour %d; the brush %.4f m; tube %d, the next stroke a tube %d\n", after_undo, colour + 1, brush, (int)ed.vr_tube_for_test(),
                (int)(s && !s->ribbons.empty() && s->ribbons.back().tube));
    CHECK(after_undo == 0u && colour == 3 && std::fabs(brush - 0.015f) < 1e-5f && ed.vr_tube_for_test() && s && s->ribbons.size() == 1u && s->ribbons.back().tube);
    ed.command("vr stop");
  });
  test("the lesson's Try-it (round 385): vr sketch - sketching on, its colour and brush", [] {
    Editor ed;
    ed.init_headless(320, 240);
    std::vector<LogEntry> log;
    const size_t since = Log::fetch(0, log);
    ed.command("vr sketch");
    log.clear();
    Log::fetch(since, log);
    CHECK(ed.vr_sketch_for_test());
    CHECK(!log.empty() && log.back().text.find("VR sketch: on") == 0);
  });
}

/* The painted texels (their coverage) of the paint layer's images; `size` zeros before the first stroke makes them. */
static std::vector<uint8_t> painted(Editor &ed, size_t size = 0) {
  PaintImagesPtr im = ed.paint_images_for_test();
  return im && !im->cover.empty() ? im->cover : std::vector<uint8_t>(size, 0);
}

/* How many texels differ (a missing one is 0). */
static size_t differ(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b) {
  size_t n = 0;
  for (size_t i = 0; i < std::max(a.size(), b.size()); i++) n += (i < a.size() ? a[i] : 0) != (i < b.size() ? b[i] : 0);
  return n;
}

/* A cube 6 m across in Paint mode with a new surface, its front face at the view's centre (the desk's paint tests'). */
static GameObject *paint_cube(Editor &ed, Vec2 &px) {
  ed.init_headless(1400, 900);
  ed.set_clock_for_test(100.0);
  ed.command("select Cube");
  for (int i = 0; i < 5; i++) ed.step_frame_headless();
  GameObject *g = ed.active_object_for_test();
  if (!g) return nullptr;
  g->set_local_scale(Vec3(6.0f));
  ed.command("surface new VrPaintSurf");
  ed.step_frame_headless();
  ed.command("paint on");
  if (!ed.paint_for_test() || !ed.project_for_test(g->world_position(), px)) return nullptr;
  for (int i = 0; i < 30; i++) ed.step_frame_headless();
  ed.command("paint size 12");
  ed.command("paint hardness 1");
  ed.command("paint opacity 1");
  return g;
}

/* Where a view pixel's ray meets the plane through `hit` square to `axis`. */
static Vec3 on_plane(Editor &ed, Vec2 px, Vec3 hit, Vec3 axis) {
  const Ray ray = ed.screen_ray_for_test(px);
  const float t = dot(hit - ray.origin, axis) / dot(ray.dir, axis);
  return ray.origin + ray.dir * t;
}

static void vr_paint_tests() {
  test("VR's paint dab (round 386): a dab in the world - the disc square to the ray where it meets the surface - writes the texels a mouse dab of the same size there writes", [] {
    Editor ed;
    Vec2 px;
    GameObject *g = paint_cube(ed, px);
    CHECK(g != nullptr);
    if (!g) return;
    ed.command(strprintf("paint stroke %g %g %g %g 1", px.x, px.y, px.x, px.y));
    const std::vector<uint8_t> mouse = painted(ed), before(mouse.size(), 0);
    ed.command("undo");
    const Ray ray = ed.screen_ray_for_test(px);
    Vec3 hit;
    CHECK(ed.raycast_scene_for_test(ray, hit));
    const Vec3 axis = normalize(ray.dir);
    const float r_px = ed.paint_brush_for_test().radius * ed.ui_for_test().scale;
    const Vec3 rx = on_plane(ed, px + Vec2(10, 0), hit, axis), dy = on_plane(ed, px + Vec2(0, 10), hit, axis);
    const float radius = r_px * length(rx - hit) / 10.0f;
    ed.paint_world_stroke_for_test(hit, axis, normalize(rx - hit), normalize(dy - hit), radius);
    const std::vector<uint8_t> world = painted(ed);
    size_t a = 0, b = 0, both = 0;
    for (size_t i = 0; i < world.size() && i < mouse.size() && i < before.size(); i++) {
      const bool m = mouse[i] != before[i], w = world[i] != before[i];
      a += m, b += w, both += m && w;
    }
    std::printf("    a %.0f px dab (%.4f m in the world): the mouse painted %zu texels, the world dab %zu, %zu of them the same\n", r_px, radius, a, b, both);
    CHECK(a > 50 && b > 50 && both >= (size_t)(0.95 * std::max(a, b)));
  });
  test("VR's paint dab's limits (round 386): a surface facing away from the ray is not painted, nor one more than the radius behind the disc", [] {
    Editor ed;
    Vec2 px;
    GameObject *g = paint_cube(ed, px);
    CHECK(g != nullptr);
    if (!g) return;
    const Ray ray = ed.screen_ray_for_test(px);
    Vec3 hit;
    CHECK(ed.raycast_scene_for_test(ray, hit));
    const Vec3 axis = normalize(ray.dir), right = normalize(cross(axis, Vec3(0, 1, 0))), down = normalize(cross(axis, right));
    auto changed = [&] { return differ(std::vector<uint8_t>(), painted(ed)); };
    ed.paint_world_stroke_for_test(hit, axis * -1.0f, right, down, 0.3f);  // (the ray turned round: the face's back to it)
    const size_t backwards = changed();
    ed.paint_world_stroke_for_test(hit - axis * 1.0f, axis, right, down, 0.3f);  // (the disc 1 m in front of the face: more than its 0.3 m radius)
    const size_t floating = changed() - backwards;
    ed.paint_world_stroke_for_test(hit, axis, right, down, 0.3f);
    const size_t on = changed() - backwards - floating;
    std::printf("    facing away: %zu texels; 1 m off the surface: %zu; on it: %zu\n", backwards, floating, on);
    CHECK(backwards == 0u && floating == 0u && on > 50u);
    /* Mirrored (scale -6 on X: the winding turned): the face toward the ray still takes paint (review 386). */
    g->set_local_scale(Vec3(-6.0f, 6.0f, 6.0f));
    ed.step_frame_headless();
    Vec3 mhit;
    CHECK(ed.raycast_scene_for_test(ray, mhit));
    const size_t was = changed();
    ed.paint_world_stroke_for_test(mhit, axis, right, down, 0.3f);
    const size_t mirrored = changed() - was;
    std::printf("    mirrored: %zu texels\n", mirrored);
    CHECK(mirrored > 50u);
  });
  test("VR painting in the editor (round 386): with Paint on for an object and `vr paint on`, the right trigger held while the ray sweeps paints along it, one undo step; the grip erases; A undoes only a paint stroke; the sticks set the size and opacity; without Paint it refuses", [] {
    Editor ed;
    Vec2 px;
    GameObject *g = paint_cube(ed, px);
    CHECK(g != nullptr);
    if (!g) return;
    auto stub = std::make_unique<StubRuntime>();
    StubRuntime *rt = stub.get();
    rt->w = rt->h = 32;
    std::string why;
    CHECK(ed.vr_start_for_test(std::move(stub), why));
    rt->events.push_back(xr::Runtime::Event::Ready);
    ed.step_frame_headless();
    ed.command("vr paint on");
    CHECK(ed.vr_paint_for_test());
    ed.command("vr paint size 30");
    /* The right hand 15 m back from the cube's centre along the stage's -Z, aiming at it, swept sideways. */
    const Mat4 stage = ed.vr_stage_for_test(), from_world = stage.inverse();
    const Vec3 centre_xr = xr::to_blendity(from_world.point(g->world_position()));  // (to_blendity mirrors itself: back to OpenXR's axes)
    const std::vector<uint8_t> before;
    auto sweep = [&](float trigger, float squeeze) {
      for (int i = 0; i <= 20; i++) {
        rt->hands[1] = held_at(centre_xr + Vec3(-1.5f + i * 0.15f, 0.0f, 15.0f));
        rt->hands[1].trigger = trigger, rt->hands[1].squeeze = squeeze;
        ed.step_frame_headless();
        std::this_thread::sleep_for(std::chrono::milliseconds(i == 0 ? 50 : 1));  // (the cube's BVH lands on a worker)
      }
      rt->hands[1].trigger = 0.0f;
      ed.step_frame_headless();
    };
    sweep(0.0f, 0.0f);  // (aiming only: the BVH made)
    const bool aimed = ed.vr_paint_aimed_for_test();
    const size_t steps0 = ed.undo_steps_for_test();
    sweep(1.0f, 0.0f);
    ed.step_frame_headless();
    size_t sweep_steps = 0;  // (the stroke's own steps: its first on a new surface also makes the layer, "Add Paint Layer", as at the desk)
    for (size_t k = 0; k < ed.undo_steps_for_test() - steps0; k++) sweep_steps += ed.undo_label_for_test(k) == "Paint";
    std::printf("    the sweep's steps: '%s', '%s'\n", ed.undo_label_for_test(0).c_str(), ed.undo_label_for_test(1).c_str());
    auto count = differ;
    const std::vector<uint8_t> after = painted(ed);
    const size_t swept = count(before, after);
    sweep(1.0f, 1.0f);  // (the grip held: erased)
    const size_t erased = count(after, painted(ed));
    /* A: the erase undone, then the paint. */
    auto press_a = [&] {
      rt->hands[1].primary = true;
      ed.step_frame_headless();
      rt->hands[1].primary = false;
      ed.step_frame_headless();
    };
    press_a();
    const size_t back_to_paint = count(after, painted(ed));
    press_a();
    const size_t back_to_none = count(before, painted(ed));
    /* The sticks: the size up a step, the opacity down one. */
    const float r0 = ed.vr_paint_radius_for_test(), o0 = ed.paint_brush_for_test().opacity;
    rt->hands[1].stick = Vec2(0, 1), rt->hands[0] = held_at(Vec3(-0.3f, 1.0f, -0.3f)), rt->hands[0].stick = Vec2(-1, 0);
    ed.step_frame_headless();
    rt->hands[1].stick = Vec2(), rt->hands[0].stick = Vec2();
    ed.step_frame_headless();
    std::printf("    aimed %d; swept %zu texels in %zu step(s); the grip erased %zu; A: %zu differ from the painted, then %zu from none; radius %.3f -> %.3f m, opacity %.2f -> %.2f\n", (int)aimed,
                swept, sweep_steps, erased, back_to_paint, back_to_none, r0, ed.vr_paint_radius_for_test(), o0, ed.paint_brush_for_test().opacity);
    CHECK(aimed && swept > 4000u && sweep_steps == 1u && erased > 100u && back_to_paint == 0u && back_to_none == 0u);  // (review 386: a single dab is about 1,100 texels - the desk had ended the stroke after its first)
    CHECK(std::fabs(ed.vr_paint_radius_for_test() - r0 * 1.5f) < 1e-5f && std::fabs(ed.paint_brush_for_test().opacity - (o0 - 0.1f)) < 1e-5f);
    /* Off the face to the left, round over the cube in the air and back on from the right: what the two ends paint as
     * two strokes, no band across the middle the ray never crossed (review 386). */
    ed.command("undo");
    ed.step_frame_headless();
    std::vector<Vec3> left, round, right;
    for (float x = -1.5f; x >= -20.0f; x -= 2.0f) left.push_back({x, 0.0f, 0.0f});
    for (float y = 4.0f; y <= 20.0f; y += 4.0f) round.push_back({-20.0f, y, 0.0f});
    for (float x = -16.0f; x <= 20.0f; x += 4.0f) round.push_back({x, 20.0f, 0.0f});
    for (float y = 16.0f; y >= 0.0f; y -= 4.0f) round.push_back({20.0f, y, 0.0f});
    for (float x = 18.0f; x >= 1.5f; x -= 2.0f) right.push_back({x, 0.0f, 0.0f});
    auto stroke = [&](const std::vector<std::vector<Vec3>> &paths) {
      for (const auto &path : paths)
        for (const Vec3 &p : path) {
          rt->hands[1] = held_at(centre_xr + p + Vec3(0.0f, 0.0f, 15.0f));
          rt->hands[1].trigger = 1.0f;
          ed.step_frame_headless();
        }
      rt->hands[1].trigger = 0.0f;
      ed.step_frame_headless();
    };
    const std::vector<uint8_t> clean = painted(ed);
    stroke({left, round, right});
    const size_t looped = differ(clean, painted(ed));
    ed.command("undo");
    ed.step_frame_headless();
    stroke({left});
    stroke({right});
    const size_t apart = differ(clean, painted(ed));
    std::printf("    left, round in the air, right as one stroke: %zu texels; the two ends as two strokes: %zu\n", looped, apart);
    CHECK(looped > 500u && looped <= apart + apart / 20);
    ed.command("undo"), ed.command("undo");
    /* An undo mid-stroke: the stroke goes, and the desk can paint again (re-check 386). */
    rt->hands[1] = held_at(centre_xr + Vec3(0.0f, 0.0f, 15.0f));
    rt->hands[1].trigger = 1.0f;
    ed.step_frame_headless();
    ed.command("undo");
    const size_t steps_before_desk = ed.undo_steps_for_test();
    ed.command(strprintf("paint stroke %g %g %g %g 1", px.x, px.y, px.x, px.y));
    const std::string desk_step = ed.undo_label_for_test(0);
    const size_t desk_steps = ed.undo_steps_for_test() - steps_before_desk;
    rt->hands[1].trigger = 0.0f;
    ed.step_frame_headless();
    std::printf("    a desk stroke after an undo mid VR stroke: %zu new step(s), '%s'\n", desk_steps, desk_step.c_str());
    CHECK(desk_steps >= 1u && desk_step == "Paint");  // (locked out it would make none; the undo may have taken the layer's step, made again)
    /* Paint turned off at the desk: VR's painting goes off with it. */
    ed.command("paint off");
    ed.step_frame_headless();
    CHECK(!ed.vr_paint_for_test());
    ed.command("vr stop");
    ed.command("vr paint on");
    CHECK(!ed.vr_paint_for_test());
  });
  test("the lesson's Try-it (round 386): vr paint - refused until Paint is on for an object", [] {
    Editor ed;
    ed.init_headless(640, 400);
    std::vector<LogEntry> log;
    const size_t since = Log::fetch(0, log);
    ed.command("vr paint");
    log.clear();
    Log::fetch(since, log);
    CHECK(!ed.vr_paint_for_test() && !log.empty() && log.back().text.find("VR paint: turn Paint on") == 0);
  });
}

void run_vr_tests() {
  vr_math_tests();
  vr_session_tests();
  vr_nav_tests();
  vr_sketch_tests();
  vr_paint_tests();
}

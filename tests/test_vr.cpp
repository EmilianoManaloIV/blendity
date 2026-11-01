// SPDX-License-Identifier: GPL-2.0-or-later
// VR View (round 383, task 0360): the stereo maths, the session's state machine on a stub runtime (no headset in the
// suite), a stereo frame of a known scene with the disparity two eyes 64 mm apart give, and the editor's `vr` verbs.
#include "test_common.h"

#include "../src/render/xr.h"
#include "../src/render/xr_nav.h"

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

void run_vr_tests() {
  vr_math_tests();
  vr_session_tests();
  vr_nav_tests();
}

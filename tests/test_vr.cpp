// SPDX-License-Identifier: GPL-2.0-or-later
// VR View (round 383, task 0360): the stereo maths, the session's state machine on a stub runtime (no headset in the
// suite), a stereo frame of a known scene with the disparity two eyes 64 mm apart give, and the editor's `vr` verbs.
#include "test_common.h"

#include "../src/render/xr.h"

#include <chrono>
#include <cmath>
#include <deque>

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
    const Mat4 stage = xr::stage_in_world({2, 7, 3}, 0.0f);
    const Mat4 view = xr::eye_view(stage, {Quat(), Vec3(0, 1.6f, 0)});
    const Vec3 ahead = view.point({2, 1.6f, 8});
    std::printf("    a point 5 m ahead in the eye's view: (%.4f, %.4f, %.4f)\n", ahead.x, ahead.y, ahead.z);
    CHECK(length(ahead - Vec3(0, 0, 5)) < 1e-4f);
    /* The world at 1:2 - the eyes twice as high in it. */
    const Vec3 tall = xr::eye_position(xr::stage_in_world({0, 0, 0}, 0.0f, 2.0f), {Quat(), Vec3(0, 1.6f, 0)});
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

void run_vr_tests() {
  vr_math_tests();
  vr_session_tests();
}

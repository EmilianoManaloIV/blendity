// SPDX-License-Identifier: GPL-2.0-or-later
// Round 657: matched large-scene drags through the editor's event path.
#include "../src/editor/editor.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

using namespace bl;
using namespace bl::platform;

constexpr Vec3 start{0.0f, 1.0f, -15.0f};
constexpr int steps = 12;
constexpr int travel = 72;
struct Direction {
  const char *name;
  int x, y;
};
constexpr Direction directions[] = {{"right", 1, 0}, {"up", 0, -1}, {"left", -1, 0}, {"down", 0, 1}};

Event pointer(EventType type, int x, int y) {
  Event event;
  event.type = type;
  event.x = x;
  event.y = y;
  return event;
}

void click(Editor &ed, int x, int y) {
  ed.step_frame_headless({pointer(EventType::MouseMove, x, y)});
  ed.step_frame_headless({pointer(EventType::MouseDown, x, y)});
  ed.step_frame_headless({pointer(EventType::MouseUp, x, y)});
}

uint64_t append(uint64_t hash, uint32_t word) {
  return (hash ^ word) * 1099511628211ull;
}

struct Picture {
  uint64_t scene = 0, game = 0, depth = 0, ids = 0;
  size_t foreground = 0, target_pixels = 0;
};

uint64_t color(const Image &image, Recti rect) {
  uint64_t hash = 14695981039346656037ull;
  for (int y = rect.y; y < rect.bottom(); y++)
    for (int x = rect.x; x < rect.right(); x++)
      hash = append(hash, image.pixels[(size_t)y * image.width + x]);
  return hash;
}

Picture picture(Editor &ed, bool dual, uint32_t target) {
  const Recti view = ed.scene_view_rect();
  Picture p;
  p.scene = color(ed.framebuffer(), view);
  if (dual) p.game = color(ed.framebuffer(), ed.game_rect_for_test());
  p.depth = p.ids = 14695981039346656037ull;
  for (int y = 0; y < view.h; y++) {
    for (int x = 0; x < view.w; x++) {
      float z = ed.scene_depth_for_test(view.x + x, view.y + y);
      uint32_t bits;
      std::memcpy(&bits, &z, sizeof(bits));
      const uint32_t id = ed.scene_id_for_test(x, y);
      p.depth = append(p.depth, bits);
      p.ids = append(p.ids, id);
      p.foreground += z < 1.0f;
      p.target_pixels += id == target;
    }
  }
  return p;
}

void frame_data(const FrameProfile &p, const RasterStats &s, const RasterStats &g) {
  std::printf(",%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,"
              "%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu",
              p.ms_scene3d, p.ms_game, p.ms_ui, p.ms_collect, p.ms_hash, p.ms_shadow, p.ms_overlays, p.ms_gpu,
              p.ms_gpu_instances, p.ms_gpu_render, p.ms_gpu_readback, p.ms_gpu_shadow, p.ms_gpu_gbuffer, p.ms_gpu_lighting,
              (unsigned long long)p.view_renders, (unsigned long long)p.views_held, (unsigned long long)p.view_cache_hits,
              (unsigned long long)p.gpu_views, (unsigned long long)p.gpu_fallbacks, (unsigned long long)p.shadow_renders,
              (unsigned long long)p.cheap_shadows, (unsigned long long)p.filters_on_card,
              (unsigned long long)p.instance_rows_built, (unsigned long long)p.lights_culled);
  for (const RasterStats *r : {&s, &g})
    std::printf(",%d,%d,%zu,%zu,%.5f,%.5f,%.5f,%.5f,%.5f", r->objects_submitted, r->objects_culled, r->tris_submitted,
                r->tris_rasterized, r->ms_vertex, r->ms_setup, r->ms_bin, r->ms_raster, r->ms_shade);
}

bool drag(Editor &ed, GameObject &target, bool dual, const Direction &d, int run, bool verify) {
  target.set_world_position(start);
  ed.clear_selection_for_test();
  ed.step_frame_headless({pointer(EventType::MouseMove, -100, -100)});
  ed.step_frame_headless();
  const Recti rect = ed.scene_view_rect();
  int x = 0, y = 0;
  if (!ed.project_to_window(start, x, y) || !rect.contains(x + d.x * travel, y + d.y * travel) ||
      ed.scene_id_for_test(x - rect.x, y - rect.y) != target.id || ed.scene_depth_for_test(x, y) >= 1.0f) {
    std::fprintf(stderr, "Target not visible or path outside view at (%d,%d) in %s\n", x, y, d.name);
    return false;
  }
  click(ed, x, y);
  if (ed.active_object_for_test() != &target) {
    std::fprintf(stderr, "Pointer did not pick the requested object for %s\n", d.name);
    return false;
  }
  Event key;
  key.type = EventType::KeyDown;
  key.key = KEY_G;
  ed.step_frame_headless({key});
  key.type = EventType::KeyUp;
  ed.step_frame_headless({key});
  if (!ed.transform_active_for_test()) {
    std::fprintf(stderr, "G did not start a pointer transform for %s\n", d.name);
    return false;
  }

  const Picture initial = verify ? picture(ed, dual, (uint32_t)target.id) : Picture{};
  int previous = 0;
  for (int i = 1; i <= steps; i++) {
    const int px = x + d.x * travel * i / steps;
    const int py = y + d.y * travel * i / steps;
    ScopedTimer timer;
    ed.step_frame_headless({pointer(EventType::MouseMove, px, py)});
    const double dt = timer.ms();
    const Vec3 at = target.world_position();
    int now_x = 0, now_y = 0;
    if (!ed.project_to_window(at, now_x, now_y) ||
        d.x * (now_x - x) + d.y * (now_y - y) <= previous ||
        std::abs(d.y * (now_x - x) - d.x * (now_y - y)) > 2 ||
        !ed.transform_active_for_test() || ed.active_object_for_test() != &target) {
      std::fprintf(stderr, "Pointer movement failed at %s frame %d (%d,%d)->(%d,%d)\n", d.name, i, x, y, now_x, now_y);
      return false;
    }
    previous = d.x * (now_x - x) + d.y * (now_y - y);
    if (run < 0) continue;  // the same checked gesture warms the draw path before measurement
    std::printf("frame,%d,%s,%d,%d,%d,%.8g,%.8g,%.8g,%.5f", run, d.name, i, px, py, at.x, at.y, at.z, dt);
    const FrameProfile &profile = ed.frame_profile();
    const RasterStats game = dual && !profile.views_held ? ed.game_stats_for_test() : RasterStats{};
    frame_data(profile, ed.scene_stats_for_test(), game);
    if (verify) {
      const Picture last = picture(ed, dual, (uint32_t)target.id);
      std::printf(",%016llx,%016llx,%016llx,%016llx,%zu,%zu", (unsigned long long)last.scene, (unsigned long long)last.game,
                  (unsigned long long)last.depth, (unsigned long long)last.ids, last.foreground, last.target_pixels);
    }
    std::printf("\n");
  }
  const Vec3 last_pos = target.world_position();
  const int end_x = x + d.x * travel, end_y = y + d.y * travel;
  ed.step_frame_headless({pointer(EventType::MouseDown, end_x, end_y)});
  ed.step_frame_headless({pointer(EventType::MouseUp, end_x, end_y)});
  ed.step_frame_headless();
  if (ed.transform_active_for_test() || ed.active_object_for_test() != &target || length(last_pos - target.world_position()) > 1e-5f) {
    std::fprintf(stderr, "Mouse release did not commit %s\n", d.name);
    return false;
  }
  if (!verify) return true;

  const Picture final = picture(ed, dual, (uint32_t)target.id);
  const Recti committed = ed.scene_view_rect();
  int qx = 0, qy = 0;
  if (!ed.project_to_window(last_pos, qx, qy) ||
      ed.scene_id_for_test(qx - committed.x, qy - committed.y) != target.id ||
      ed.scene_depth_for_test(qx, qy) >= 1.0f || !final.foreground || !final.target_pixels ||
      final.scene == initial.scene || final.depth == initial.depth || final.ids == initial.ids ||
      (dual && final.game == initial.game)) {
    std::fprintf(stderr, "Committed drag output did not move as expected for %s\n", d.name);
    return false;
  }
  const Image &image = ed.framebuffer();
  const std::string file = std::string(d.name) + ".png";
  if (!write_png(file, image.pixels.data(), image.width, image.height, image.width)) return false;
  std::printf("commit,%s,%016llx,%016llx,%016llx,%016llx,%zu,%zu\n", d.name,
              (unsigned long long)final.scene, (unsigned long long)final.game, (unsigned long long)final.depth,
              (unsigned long long)final.ids, final.foreground, final.target_pixels);
  return true;
}

int run_probe(bool gpu, bool dual, bool follow, int repeats, bool verify, int start_order, int warmup) {
  constexpr int w = 1280, h = 720;
  Editor ed;
  ed.init_headless(w, h);
  ed.set_scene_hash_check_every_for_test(8);
  ed.viewport_device_for_test(gpu ? 2 : 1);
  ed.command("threads 8");
  ed.command("benchscene worldlikequick");
  ed.command(dual ? "layout H(0.5,L(0:0),L(1:0))" : "layout H(0.5,L(0:0),L(5:0))");
  ed.command("keymap Blender");
  ed.command("tool move");
  ed.command("snap origins off");
  ed.command("shading shaded");
  ed.command("overlay stats off");
  ed.command("overlay icons off");
  ed.command("viewscale 100");
  ed.command(follow ? "views follow on" : "views follow off");
  SceneCamera scene_camera;
  scene_camera.pivot = start;
  scene_camera.distance = 15.0f;
  scene_camera.yaw = -12.0f;
  scene_camera.pitch = 8.0f;
  ed.set_scene_camera_for_test(scene_camera);
  GameObject *main = ed.scene().find_by_name("Main Camera");
  if (!main) return 1;
  main->set_world_position({0.0f, 3.0f, -29.0f});
  main->set_local_rotation(Quat::euler({8.0f, 0.0f, 0.0f}));
  GameObject *target = ed.scene().create("Drag Target");
  target->set_world_position(start);
  target->add<MeshFilter>()->mesh = primitives::cube(1.4f);
  target->add<MeshRenderer>()->materials = {make_material("Drag target", {0.9f, 0.25f, 0.13f})};
  ed.mark_changed_for_test("Drag fixture");
  ed.command("stats");
  for (int i = 0; i < 8; i++) ed.step_frame_headless();
  ed.command("perf wait");
  std::printf("configuration,%s,%s,%s,%s,%llu\n", gpu ? "gpu" : "cpu", dual ? "dual" : "scene",
              follow ? "follow" : "hold", ed.gpu_view_status().c_str(), (unsigned long long)target->id);
  const Recti scene_rect = ed.scene_view_rect(), game_rect = ed.game_rect_for_test();
  std::printf("viewports,%d,%d,%d,%d,%d,%d,%d,%d\n", scene_rect.x, scene_rect.y, scene_rect.w, scene_rect.h,
              game_rect.x, game_rect.y, game_rect.w, game_rect.h);
  if (scene_rect.w < 2 || scene_rect.h < 2 || (dual && (game_rect.w < 2 || game_rect.h < 2))) return 1;
  FrameProfile before;
  for (int i = -warmup; i < repeats; i++) {
    if (i == 0) before = ed.frame_profile_totals();
    for (int j = 0; j < 4; j++) {
      const int rank = i % 2 == 0 ? j : 3 - j;
      if (!drag(ed, *target, dual, directions[(start_order + rank) % 4], i, verify && i >= 0)) return 1;
    }
  }
  const FrameProfile after = ed.frame_profile_totals();
  const auto gpu_draws = after.gpu_views - before.gpu_views;
  const auto fallbacks = after.gpu_fallbacks - before.gpu_fallbacks;
  if (gpu ? (gpu_draws == 0 || fallbacks != 0) : gpu_draws != 0) {
    std::fprintf(stderr, "Unexpected draw backend, %llu Vulkan draws and %llu fallback(s)\n",
                 (unsigned long long)gpu_draws, (unsigned long long)fallbacks);
    return 1;
  }
  std::printf("complete,%d,%llu,%llu\n", repeats * 4 * steps, (unsigned long long)gpu_draws, (unsigned long long)fallbacks);
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  const auto env = [](const char *name) { const char *v = std::getenv(name); return v && *v; };
  if (argc < 6 || argc > 8 || (std::strcmp(argv[1], "cpu") != 0 && std::strcmp(argv[1], "gpu") != 0) ||
      (std::strcmp(argv[2], "scene") != 0 && std::strcmp(argv[2], "dual") != 0) ||
      (std::strcmp(argv[3], "follow") != 0 && std::strcmp(argv[3], "hold") != 0) ||
      (std::strcmp(argv[5], "measure") != 0 && std::strcmp(argv[5], "verify") != 0) ||
      !env("BLENDITY_PROJECT") || !env("BLENDITY_TRASH") || !env("BLENDITY_PREFS_DIR") || !env("BLENDITY_CACHE_DIR")) {
    std::fprintf(stderr,
                 "Usage: perf_drag_0639 <cpu|gpu> <scene|dual> <follow|hold> <repeats> <measure|verify> [start rotation 0..3 [warmup repeats 0..3]]\n");
    return 2;
  }
  const int n = std::atoi(argv[4]);
  if (n < 1 || n > 30) return 2;
  auto digit = [](const char *s) { return std::strlen(s) == 1 && s[0] >= '0' && s[0] <= '3' ? s[0] - '0' : -1; };
  const int start_order = argc >= 7 ? digit(argv[6]) : 0, warmup = argc == 8 ? digit(argv[7]) : 0;
  if (start_order < 0 || warmup < 0) return 2;
  return run_probe(std::strcmp(argv[1], "gpu") == 0, std::strcmp(argv[2], "dual") == 0,
                   std::strcmp(argv[3], "follow") == 0, n, std::strcmp(argv[5], "verify") == 0, start_order, warmup);
}

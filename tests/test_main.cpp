// SPDX-License-Identifier: GPL-2.0-or-later
// blendity_tests - dependency-free unit tests (exit code = number of failures). The tests live in
// tests/test_<group>.cpp (task 0177); this file runs them and stands in for MoonRay.
#include "test_common.h"

int g_fail = 0, g_checks = 0;
std::string g_asset_root_at_start;  // the asset root main() set before any test (task 0081)

/* `blendity_tests --fake-moonray -in scene.rdla`: stands in for MoonRay (tasks 0112-0114). It reads the scene's size
 * and its RenderOutput's file name, prints MoonRay's progress, and writes an uncompressed float EXR of one colour
 * (0.5, 0.25, 0.125; alpha 1, or 0 on the left half when the scene hides its background). BLENDITY_FAKE_MOONRAY=slow
 * takes 20 s; =fail exits 3; =progress first writes a checkpoint at half the brightness (its checkpoint_file_name),
 * then prints progress for 1.5 s before the final image. */
static int fake_moonray(int argc, char **argv) {
  std::string in, text;
  for (int i = 2; i + 1 < argc; i++)
    if (std::string(argv[i]) == "-in") in = argv[i + 1];
  if (in.empty() || !fs::read_file(in, text)) {
    std::printf("Error: can't read the scene '%s'\n", in.c_str());
    return 2;
  }
  auto value = [&](const std::string &key) {
    const size_t k = text.find("[\"" + key + "\"] = ");
    if (k == std::string::npos) return std::string();
    const size_t a = k + key.size() + 7;  // (past ["key"] = )
    const size_t b = text.find_first_of(",\n", a);
    std::string v = text.substr(a, b - a);
    if (!v.empty() && v.front() == '"') v = v.substr(1, v.size() - 2);
    std::string plain;  // (Lua's escapes undone: a Windows path's backslashes are written doubled)
    for (size_t i = 0; i < v.size(); i++) plain += v[i] == '\\' && i + 1 < v.size() ? v[++i] : v[i];
    return plain;
  };
  const int w = std::atoi(value("image_width").c_str()), h = std::atoi(value("image_height").c_str());
  const std::string out = value("file_name");
  const char *mode = std::getenv("BLENDITY_FAKE_MOONRAY");
  const std::string m = mode ? mode : "";
  std::printf("Loading Scene File(s): %s\n", in.c_str());
  for (int k = 1; k <= 4; k++) {
    std::printf("  [+] Rendering [%-20s] %.1f%%\r", std::string(k * 5, '=').c_str(), k * 25.0);
    std::fflush(stdout);
    std::this_thread::sleep_for(std::chrono::milliseconds(m == "slow" ? 5000 : 20));
  }
  if (m == "fail") {
    std::printf("\nError: the fake renderer was told to fail\n");
    return 3;
  }
  if (w <= 0 || h <= 0 || out.empty()) return 4;
  const bool hide = text.find("\"force off\"") != std::string::npos;
  auto image = [&](float k) {
    std::vector<float> px((size_t)w * h * 4);
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) {
        float *p = &px[((size_t)y * w + x) * 4];
        const float a = hide && x < w / 2 ? 0.0f : 1.0f;
        p[0] = 0.5f * a * k, p[1] = 0.25f * a * k, p[2] = 0.125f * a * k, p[3] = a;
      }
    return test_exr_plain(w, h, px);
  };
  if (m == "progress") {  // (a checkpoint, then the render going on a while: the editor shows the checkpoint meanwhile)
    if (!fs::write_file(value("checkpoint_file_name"), image(0.5f))) return 6;
    for (int k = 0; k < 15; k++) {
      std::printf("  [+] Rendering [%-20s] %.1f%%\r", "==========", 50.0 + k);
      std::fflush(stdout);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
  if (!fs::write_file(out, image(1.0f))) return 5;
  std::printf("\nWrote %s\n", out.c_str());
  return 0;
}

/* The groups, one file each (task 0177): `--group <name>` runs one, `--list-groups` names them. */
void run_modeling_tests();
void run_render_tests();
void run_lighting_tests();
void run_gpu_tests();
void run_assets_tests();
void run_animation_tests();
void run_physics_tests();
void run_editor_tests();
void run_livelink_tests();
void run_input_tests();
struct TestGroup { const char *name; void (*run)(); };
static const TestGroup kTestGroups[] = {
    {"modeling", run_modeling_tests},
    {"render", run_render_tests},
    {"lighting", run_lighting_tests},
    {"gpu", run_gpu_tests},
    {"assets", run_assets_tests},
    {"animation", run_animation_tests},
    {"physics", run_physics_tests},
    {"editor", run_editor_tests},
    {"livelink", run_livelink_tests},
    {"input", run_input_tests},
};

int main(int argc, char **argv) {
  if (argc > 1 && std::string(argv[1]) == "--fake-moonray") return fake_moonray(argc, argv);
  register_builtin_components();
  research::register_features();
  Log::echo_stdout = false;
  set_env("BLENDITY_PROJECT", "");  // the scratch project
  set_env("BLENDITY_TRASH", "");
  set_env("BLENDITY_AI_DIR", fs::join(test_dir(), "no_ai"));  // (never the user's AI download: task 0129)
  /* Assets a test makes before any editor exists (clips, materials) go into the scratch project too: with no asset
   * root a relative path is the folder the tests run from - a real project's Assets (task 0081). */
  set_asset_root(std::getenv("BLENDITY_PROJECT"));
  g_asset_root_at_start = asset_root();
  /* GI Device Auto gathers on the CPU here, so a machine with a GPU and CI (without one) run the same
   * code; the GPU's own tests ask for it (task 0018). */
  if (!std::getenv("BLENDITY_GI_DEVICE")) set_env("BLENDITY_GI_DEVICE", "cpu");
  /* The same for OpenImageDenoise's Denoise Device Auto (task 0094). */
  if (!std::getenv("BLENDITY_DENOISE_DEVICE")) set_env("BLENDITY_DENOISE_DEVICE", "cpu");
  /* And Viewport Device Auto draws the views on the CPU (task 0096); the GPU view's own tests ask for it. */
  if (!std::getenv("BLENDITY_VIEWPORT_DEVICE")) set_env("BLENDITY_VIEWPORT_DEVICE", "cpu");
  /* And the path tracer's cards run GPU Compute (task 0170): the OptiX device's tests ask for it by name. */
  if (!std::getenv("BLENDITY_GPU_BACKEND")) set_env("BLENDITY_GPU_BACKEND", "vulkan");
  /* Scenes default to Automatic lighting updates here (the behaviour the tests before task 0020 were written
   * for); Manual's own tests ask for it. */
  if (!std::getenv("BLENDITY_LIGHTING_UPDATES")) set_env("BLENDITY_LIGHTING_UPDATES", "automatic");

  std::string group;
  for (int a = 1; a < argc; a++) {
    if (std::string(argv[a]) == "--group" && a + 1 < argc) group = argv[++a];
    else if (std::string(argv[a]) == "--list-groups") {
      for (const TestGroup &g : kTestGroups) std::printf("%s\n", g.name);
      return 0;
    }
  }
  bool ran = false;
  for (const TestGroup &g : kTestGroups) {
    if (!group.empty() && group != g.name) continue;
    g.run();
    ran = true;
  }
  if (!ran) {
    std::printf("no such group: %s (--list-groups names them)\n", group.c_str());
    return 2;
  }

  /* BLENDITY_RENDER_CACHE_VERIFY=1 re-renders every view the render cache would have reused: none may
   * differ from the cached picture. */
  if (std::getenv("BLENDITY_RENDER_CACHE_VERIFY")) {
    std::printf("render cache verify: %llu stale pictures\n", (unsigned long long)Editor::render_cache_mismatches_all());
    CHECK(Editor::render_cache_mismatches_all() == 0);
  }
  std::printf("\n%d checks, %d failed\n", g_checks, g_fail);
  return g_fail;
}

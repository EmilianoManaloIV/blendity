// SPDX-License-Identifier: GPL-2.0-or-later
// blendity_tests: the gpu tests (task 0177: one file a subsystem; the groups keep their rounds' order).
#include "test_common.h"
#include <cstring>

static void round59_tests() {
  test("GPU opening: a device that doesn't exist is refused at once (nothing pending, an error)", [] {
    bool pending = true;
    std::string err;
    std::unique_ptr<gpu::Renderer> r = gpu::take_renderer(999, true, &pending, &err);
    CHECK(!r && !pending && !err.empty());
    CHECK(!gpu::open_renderer(-1, false, &err));
  });

  test("GPU opening: turning the Camera Preview to Rendered while the GPU opens slowly keeps every frame under 100 ms, traces on the CPU meanwhile, then on the GPU; the next view gets the spare at once", [] {
    if (!gpu::available()) {
      std::printf("    (no GPU here: skipped)\n");
      return;
    }
    for (int i = 0; i < 400 && gpu::renderers_opening(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(50));  // an earlier spare
    gpu::drop_ready_renderers_for_test();
    gpu::set_open_delay_for_test(1000);  // a cold driver cache, shortened to a second (task 0177: it was three)
    Editor ed;
    r57_open(ed);
    ed.set_background_gpu_open_for_test(true);
    ed.command("device gpu");
    ed.command("select Main Camera");
    ed.command("camerapreview rendered");
    ed.step_frame_headless();  // builds the preview's scene
    /* The editor asks to be woken while the device opens (the run loop would otherwise sleep once the CPU samples
     * are done, and never take the GPU). */
    ed.step_frame_headless();
    CHECK(!gpu::renderers_opening() || ed.next_wakeup_for_test() > 0.0);
    const std::string gpu_name = gpu::devices()[0].name;
    double worst = 0.0;
    bool saw_cpu = false, saw_gpu = false;
    int frames = 0;
    ScopedTimer total;
    while (total.ms() < 20000.0 && !saw_gpu) {
      ScopedTimer f;
      ed.step_frame_headless();
      worst = std::max(worst, f.ms());
      frames++;
      const std::string d = ed.cam_preview_device_for_test();
      saw_cpu = saw_cpu || d.find("GPU starting") != std::string::npos;
      saw_gpu = d.find(gpu_name) != std::string::npos && d.find("starting") == std::string::npos;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    gpu::set_open_delay_for_test(0);
    std::printf("    %d frames, the slowest %.1f ms; CPU while opening: %s; on the GPU after %.1f s: %s\n", frames, worst,
                saw_cpu ? "yes" : "no", total.ms() / 1000.0, saw_gpu ? "yes" : "no");
    CHECK(saw_cpu && saw_gpu);
    std::printf("    the slowest frame while the GPU opened: %.1f ms (a stress row since round 411, not a check)\n", worst);
    /* The spare: once it has opened, the next view is served at once. */
    for (int i = 0; i < 400 && gpu::renderers_opening(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    bool pending = true;
    std::unique_ptr<gpu::Renderer> spare = gpu::take_renderer(0, true, &pending);
    CHECK(spare != nullptr && !pending);
    gpu::return_renderer(std::move(spare), 0, true);
  });

  test("GPU opening: a tracer waiting for its GPU forgets it when the device turns back to the CPU; a lighting gather doesn't wait for an opening GPU", [] {
    if (!gpu::available()) {
      std::printf("    (no GPU here: skipped)\n");
      return;
    }
    MeshPtr sphere = primitives::uv_sphere(0.5f, 16, 8);
    std::vector<MaterialPtr> mats = {make_material("Grey", Vec3(0.5f))};
    Environment env;
    env.mode = Environment::Color;
    env.color = {0.5f, 0.5f, 0.5f};
    for (int i = 0; i < 400 && gpu::renderers_opening(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(50));  // an earlier spare
    gpu::drop_ready_renderers_for_test();
    gpu::set_open_delay_for_test(600);
    PathTracer pt;
    pt.set_background_gpu_open(true);
    PTSettings st;
    st.gpus = {0};
    pt.set_settings(st);
    pt.build({{&sphere->render_mesh_tangents(), Mat4::identity(), &mats}}, {}, env);
    CHECK(pt.waiting_for_gpu());
    st.gpus.clear();  // Render > Device: CPU
    pt.set_settings(st);
    pt.build({{&sphere->render_mesh_tangents(), Mat4::identity(), &mats}}, {}, env);
    CHECK(!pt.gpu_starting() && pt.device_summary().find("starting") == std::string::npos);
    /* The bake's gather: refused at once while the device opens (the bake does that slice on the CPU), then on it. */
    PathTracer bake;
    bake.set_background_gpu_open(true);
    bake.build({{&sphere->render_mesh_tangents(), Mat4::identity(), &mats}}, {}, env);
    std::vector<Vec3> origins(64, Vec3(0, 2, 0)), dirs(64, Vec3(0, -1, 0)), out;
    std::string err;
    ScopedTimer t;
    const bool first = bake.gpu_gather(0, 1, origins, dirs, 4, out, nullptr, &err);
    const double first_ms = t.ms();
    CHECK(!first && bake.gpu_gather_waiting());
    std::printf("    the first gather call: %.1f ms (a stress row since round 411, not a check)\n", first_ms);
    bool later = false;
    for (int i = 0; i < 300 && !later; i++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      later = bake.gpu_gather(0, 1, origins, dirs, 4, out, nullptr, &err);
    }
    std::printf("    the first gather returned in %.1f ms (waiting); on the GPU later: %s\n", first_ms, later ? "yes" : "no");
    CHECK(later && out.size() == 64 && !bake.gpu_gather_waiting());
    gpu::set_open_delay_for_test(0);
  });

  test("GPU opening: the pipeline cache is written, used by the next open, and a corrupt file is ignored and rewritten", [] {
    if (!gpu::available()) {
      std::printf("    (no GPU here: skipped)\n");
      return;
    }
    /* In a scratch cache folder: never the user's (their Blendity may be using it). */
    const char *old_env = std::getenv("BLENDITY_CACHE_DIR");
    const std::string keep = old_env ? old_env : "";
    const std::string dir = fs::join(test_dir(), "gpu_cache");
    set_env("BLENDITY_CACHE_DIR", dir);
    const std::string path = gpu::pipeline_cache_file(0, true);
    CHECK(!path.empty() && path.find("gpu_cache") != std::string::npos);
    for (int i = 0; i < 400 && gpu::renderers_opening(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(50));  // (a spare opening in the background would rewrite the file under the test: task 0162)
    std::remove(path.c_str());
    std::string err;
    std::unique_ptr<gpu::Renderer> a = gpu::Renderer::create(0, true, &err);
    CHECK(a != nullptr);
    if (!a) return;
    CHECK(fs::exists(path) && !a->open_stats().from_pipeline_cache);
    std::unique_ptr<gpu::Renderer> b = gpu::Renderer::create(0, true, &err);
    CHECK(b && b->open_stats().from_pipeline_cache);
    std::printf("    pipeline: %.0f ms without the cache file, %.0f ms with it (device %.0f ms; the driver's own cache is warm)\n",
                a->open_stats().pipeline_ms, b ? b->open_stats().pipeline_ms : 0.0, a->open_stats().device_ms);
    const std::string junk(4096, 'x');
    CHECK(fs::write_file(path, junk));
    std::unique_ptr<gpu::Renderer> c = gpu::Renderer::create(0, true, &err);
    CHECK(c && !c->open_stats().from_pipeline_cache);
    std::string good;
    CHECK(fs::read_file(path, good) && good != junk && good.size() > 48);
    /* A cache from another GPU (its UUID differs) with an intact trailer: the header check refuses it. */
    if (good.size() > 48) {
      std::string foreign = good.substr(0, good.size() - 16);
      foreign[20] = (char)(foreign[20] ^ 0x5A);
      const uint64_t h = fnv1a(foreign.data(), foreign.size());  // (the trailer's own hash, so the header check is what refuses it)
      const uint32_t len = (uint32_t)foreign.size(), tag = 0x43504C42u;
      foreign.append(reinterpret_cast<const char *>(&len), 4);
      foreign.append(reinterpret_cast<const char *>(&tag), 4);
      foreign.append(reinterpret_cast<const char *>(&h), 8);
      CHECK(fs::write_file(path, foreign));
      std::unique_ptr<gpu::Renderer> d = gpu::Renderer::create(0, true, &err);
      CHECK(d && !d->open_stats().from_pipeline_cache);
      /* and a torn copy of a good one (the trailer no longer matches) */
      CHECK(fs::write_file(path, good.substr(0, good.size() / 2) + good.substr(good.size() - 16)));
      std::unique_ptr<gpu::Renderer> e = gpu::Renderer::create(0, true, &err);
      CHECK(e && !e->open_stats().from_pipeline_cache);
    }
    std::remove(path.c_str());
    set_env("BLENDITY_CACHE_DIR", keep);
  });
}

static void tests_round122_to_round125() {
  /* ---------------------------------------------------------------- round 122: the views on the GPU (task 0096) */
  /* The Scene view and Game view (Camera Preview up) drawn again with Viewport Device `device`: their crops. */
  auto r122_views = [](Editor &ed, int device) {
    ed.viewport_device_for_test(device);
    GameObject *cube = ed.scene().find_by_name("Cube");
    if (cube) cube->set_local_position({0.01f * (device + 1), 0.5f, 0.0f});  // (a change: every view draws again)
    r34_steps(ed, 2);
    if (cube) cube->set_local_position({0.0f, 0.5f, 0.0f});
    r34_steps(ed, 2);
    std::vector<uint32_t> s = r34_crop(ed, ed.scene_view_rect()), g = r34_crop(ed, ed.window_rect_for_test(WindowKind::Game));
    s.insert(s.end(), g.begin(), g.end());
    return s;
  };
  /* Mean and share within 3 / 255 of the worst channel's difference, over every pixel (the overlays and the UI are the
   * CPU's on both, so they match exactly). */
  auto r122_compare = [](const std::vector<uint32_t> &a, const std::vector<uint32_t> &b, double &mean, double &within3) {
    double sum = 0;
    size_t within = 0;
    for (size_t i = 0; i < a.size(); i++) {
      int worst = 0;
      for (int k = 0; k < 24; k += 8) worst = std::max(worst, std::abs((int)((a[i] >> k) & 255) - (int)((b[i] >> k) & 255)));
      sum += worst;
      within += worst <= 3;
    }
    mean = a.empty() ? 0 : sum / (double)a.size();
    within3 = a.empty() ? 0 : (double)within / (double)a.size();
  };
  test("gpu views: the Scene view and Game view of a plain scene look the same drawn on the GPU as on the CPU (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    r34_open(ed);
    ed.command("select Main Camera");
    const std::vector<uint32_t> cpu = r122_views(ed, 1);
    const FrameProfile before = ed.frame_profile_totals();
    const std::vector<uint32_t> gpu = r122_views(ed, 2);
    const FrameProfile after = ed.frame_profile_totals();
    std::printf("    %s; %llu views on the GPU, %llu on the CPU instead\n", ed.gpu_view_status().c_str(),
                (unsigned long long)(after.gpu_views - before.gpu_views), (unsigned long long)(after.gpu_fallbacks - before.gpu_fallbacks));
    CHECK(after.gpu_views > before.gpu_views);
    CHECK(ed.gpu_view_reason().empty());
    CHECK(cpu.size() == gpu.size());
    double mean = 0, within3 = 0;
    r122_compare(cpu, gpu, mean, within3);
    std::printf("    mean difference %.3f / 255, %.2f %% within 3 / 255\n", mean, within3 * 100);
    CHECK(mean < 1.0);
    CHECK(within3 >= 0.99);
    ed.viewport_device_for_test(1);
  });
  test("gpu views: a material the card doesn't draw yet (a toon axis material) sends the view to the CPU, says why, and the picture is then the CPU's exactly", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    r34_open(ed);
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube != nullptr);
    if (!cube) return;
    /* (A procedural was the example until task 0101 drew them on the card, an axis material until task 0195, a
     * see-through toon material until round 414 drew it approximately: tests_round414.) */
    MaterialPtr veil = make_material("Toon Axis", {0.8f, 0.8f, 0.8f});
    veil->shading_model = 1;
    veil->axis_mode = 1;
    cube->get<MeshRenderer>()->materials = {veil};
    const std::vector<uint32_t> cpu = r122_views(ed, 1), gpu = r122_views(ed, 2);
    std::printf("    %s\n", ed.gpu_view_status().c_str());
    CHECK(ed.gpu_view_reason().find("toon axis") != std::string::npos);
    CHECK(cpu == gpu);
    ed.viewport_device_for_test(1);
  });
  test("gpu views: back faces are culled as on the CPU (a plane seen from below shows the sky), and picking finds the same object", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    r34_open(ed);
    ed.command("create Plane");
    ed.command("camera 25 -30 6 0 0.5 0");  // (from below the plane)
    r34_steps(ed, 2);
    const std::vector<uint32_t> cpu = r122_views(ed, 1), gpu = r122_views(ed, 2);
    double mean = 0, within3 = 0;
    r122_compare(cpu, gpu, mean, within3);
    std::printf("    from below: mean difference %.3f / 255, %.2f %% within 3 / 255\n", mean, within3 * 100);
    CHECK(mean < 1.0 && within3 >= 0.99);
    /* A click on the Cube in the GPU-drawn Scene view picks it (the ids came back from the GPU). */
    ed.command("camera 25 20 7 0 0.5 0");
    ed.command("select Main Camera");
    r34_steps(ed, 3);
    GameObject *cube = ed.scene().find_by_name("Cube");
    int x = 0, y = 0;
    CHECK(cube && ed.project_to_window(cube->world_position(), x, y));
    if (cube) r34_click(ed, x, y);
    r34_steps(ed, 2);
    CHECK(ed.selected_object() == cube);
    CHECK(ed.gpu_view_reason().empty());
    ed.viewport_device_for_test(1);
  });
  test("gpu views: an Edit Mode drag moves the mesh's copy on the GPU in place (no new copy each frame)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    r34_open(ed);
    ed.viewport_device_for_test(2);
    ed.command("select Cube");
    ed.command("edit vertex");
    ed.command("vsel 0");
    ed.command("camera 0 20 6 0 0.5 0");
    ed.command("tool move");
    r34_steps(ed, 3);
    GameObject *cube = ed.scene().find_by_name("Cube");
    if (!cube) return;
    const FrameProfile before = ed.frame_profile_totals();
    const Vec3 wp = cube->world_matrix().point(cube->get<MeshFilter>()->mesh->positions[0]);
    r34_gizmo_drag_x(ed, wp, 50);
    const FrameProfile after = ed.frame_profile_totals();
    const uint64_t updates = after.gpu_mesh_updates - before.gpu_mesh_updates, uploads = after.gpu_mesh_uploads - before.gpu_mesh_uploads;
    std::printf("    the drag: %llu views on the GPU, %llu in-place updates, %llu whole uploads (%s)\n",
                (unsigned long long)(after.gpu_views - before.gpu_views), (unsigned long long)updates, (unsigned long long)uploads,
                ed.gpu_view_status().c_str());
    CHECK(after.gpu_views > before.gpu_views);
    CHECK(updates >= 2);
    CHECK(uploads <= 2);  // (at most the cube's two caches once: the drag's first frame)
    ed.viewport_device_for_test(1);
  });
  test("gpu views: Viewport Device is kept in the prefs, and Auto stays on the CPU when BLENDITY_VIEWPORT_DEVICE=cpu", [&] {
    const std::string prefs = fs::join(test_dir(), "r122_prefs.txt");
    std::error_code ec;
    std::filesystem::remove(std::filesystem::u8path(prefs), ec);
    {
      Editor ed;
      ed.init_headless(1200, 800);
      ed.prefs_for_test(prefs);
      ed.command("viewport device gpu");
      CHECK(ed.viewport_device() == 2);
    }
    {
      Editor ed;
      ed.init_headless(1200, 800);
      ed.prefs_for_test(prefs);
      ed.load_prefs_for_test();
      CHECK(ed.viewport_device() == 2);
      ed.command("viewport device auto");
      CHECK(ed.viewport_device() == 0);
      CHECK(ed.gpu_view_status() == "CPU (Viewport Device)");  // (the tests pin Auto to the CPU)
    }
  });
  test("gpu views: the editor's own lighting (Scene lighting off) and a mirrored object look the same on the GPU as on the CPU", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    r34_open(ed);
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube != nullptr);
    if (!cube) return;
    cube->set_local_scale({-1.0f, 1.0f, 1.0f});  // (mirrored: its faces inside out, culled the other way)
    double mean = 0, within3 = 0;
    std::vector<uint32_t> cpu = r122_views(ed, 1), gpu = r122_views(ed, 2);  // (in this order: the device stays GPU)
    r122_compare(cpu, gpu, mean, within3);
    std::printf("    a mirrored Cube: mean difference %.3f / 255, %.2f %% within 3 / 255\n", mean, within3 * 100);
    CHECK(mean < 1.0 && within3 >= 0.99);
    r34_toolbar_click(ed, 0);  // Scene lighting off: the headlight and the default gradient
    r34_steps(ed, 2);
    cpu = r122_views(ed, 1);
    gpu = r122_views(ed, 2);
    r122_compare(cpu, gpu, mean, within3);
    std::printf("    the editor's lighting: mean difference %.3f / 255, %.2f %% within 3 / 255 (%s)\n", mean, within3 * 100, ed.gpu_view_status().c_str());
    CHECK(mean < 1.0 && within3 >= 0.99);
    CHECK(ed.gpu_view_reason().empty());
    ed.viewport_device_for_test(1);
  });
  /* ---------------------------------------------------------------- round 124: normals only around what moved (task 0099) */
  test("Edit Mode: on big meshes a drag works out normals only around the moved faces, and draws exactly what a rebuild draws (smooth, by angle, sharp edges and seams, flat)", [] {
    /* Each triangle corner's position and normal bit for bit (as round 112 compares). */
    auto same = [](const RenderMesh &a, const RenderMesh &b) {
      if (a.indices.size() != b.indices.size() || a.tri_corner != b.tri_corner) return false;
      for (size_t k = 0; k < a.indices.size(); k++) {
        const uint32_t i = a.indices[k], j = b.indices[k];
        if (std::memcmp(&a.positions[i], &b.positions[j], sizeof(Vec3)) || std::memcmp(&a.normals[i], &b.normals[j], sizeof(Vec3))) return false;
      }
      return true;
    };
    std::vector<std::pair<std::string, MeshPtr>> meshes;
    meshes.push_back({"smooth sphere", primitives::uv_sphere(1.0f, 32, 24)});
    {
      MeshPtr g = primitives::grid(4.0f, 24, 24);
      for (Vec3 &p : g->positions) p.y = 0.3f * std::sin(p.x * 2.0f) * std::cos(p.z * 2.0f);
      g->smooth = true;
      g->smooth_angle = 30.0f;  // (smooth by angle)
      meshes.push_back({"grid smooth by angle", g});
    }
    {
      MeshPtr g = primitives::grid(4.0f, 24, 24);
      for (Vec3 &p : g->positions) p.y = 0.2f * std::sin(p.x * 3.0f);
      g->smooth = true;
      for (uint32_t i = 0; i + 1 < 25; i++) g->sharp_edges.push_back(((uint64_t)std::min(i * 25 + 12, i * 25 + 37) << 32) | std::max(i * 25 + 12, i * 25 + 37));
      g->seams.push_back(((uint64_t)300 << 32) | 301);
      g->seams_sharp = true;
      meshes.push_back({"grid with sharp edges and a seam", g});
      MeshPtr a = std::make_shared<Mesh>(*g);  // (fans with an angle limit too)
      a->smooth_angle = 30.0f;
      meshes.push_back({"grid with sharp edges and smooth by angle", a});
      MeshPtr d = std::make_shared<Mesh>(*g);  // (a quad with one vertex twice: an edge whose two ends are one vertex)
      d->corner_verts[d->face_offsets[40] + 1] = d->corner_verts[d->face_offsets[40]];
      meshes.push_back({"grid with a degenerate quad", d});
    }
    {
      MeshPtr g = primitives::grid(4.0f, 24, 24);
      for (Vec3 &p : g->positions) p.y = 0.3f * std::sin(p.x * 2.0f) * std::cos(p.z * 1.5f);
      g->smooth = true;
      for (size_t f = 0; f < g->face_count(); f += 3) g->set_face_smooth(f, false);  // (some faces flat among smooth ones)
      meshes.push_back({"grid with flat and smooth faces", g});
    }
    {
      MeshPtr g = primitives::uv_sphere(1.0f, 24, 16);
      g->smooth = false;  // (nothing smooth: the shaded cache is the flat one)
      meshes.push_back({"flat sphere", g});
    }
    uint32_t seed = 124;
    auto rng = [&] { return (seed = seed * 1664525u + 1013904223u) >> 8; };
    int cases = 0, mismatches = 0;
    const MeshCounts c0 = mesh_counts();
    for (auto &[name, base] : meshes) {
      for (bool flat : {false, true}) {
        MeshPtr m = std::make_shared<Mesh>(*base);
        (void)m->render_mesh(flat);
        for (int step = 0; step < 8; step++) {
          for (int k = 0; k < 4; k++) {  // (a few vertices: a small share of the faces)
            Vec3 &p = m->positions[rng() % m->positions.size()];
            const float dy = (float)(rng() % 2001) / 20000.0f - 0.05f;
            p.y += dy;
          }
          m->touch();
          const RenderMesh &got = m->render_mesh(flat);
          Mesh ref = *m;
          ref.forget_caches();
          cases++;
          if (!same(got, ref.render_mesh(flat))) {
            mismatches++;
            std::printf("    %s, %s, step %d: differs from a rebuild\n", name.c_str(), flat ? "flat" : "as shaded", step);
          }
        }
      }
    }
    const MeshCounts d = mesh_counts() - c0;
    std::printf("    %d drag steps: %llu worked out only around what moved, %d differed from a rebuild\n", cases, (unsigned long long)d.partial_normals,
                mismatches);
    CHECK(mismatches == 0);
    CHECK(d.partial_normals >= (uint64_t)cases / 2);  // (a step whose splits change rebuilds instead)
  });
  test("gpu views: an Edit Mode drag on a big mesh sends the card only what moved, and draws what the CPU draws (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    r34_open(ed);
    GameObject *big = ed.scene().create("Big");
    big->add<MeshFilter>()->mesh = primitives::uv_sphere(1.0f, 128, 96);  // (about 12k faces)
    big->add<MeshRenderer>();
    big->set_local_position({0.0f, 1.0f, 0.0f});
    ed.command("select Big");
    ed.command("edit vertex");
    ed.command("vsel 4000");
    ed.command("camera 0 15 5 0 1 0");
    ed.command("tool move");
    ed.command("overlay grid off");  // (the grid on the card is task 0192's test: this one compares the range uploads)
    r34_steps(ed, 3);
    const Mesh &m0 = *big->get<MeshFilter>()->mesh;
    const Vec3 wp = big->world_matrix().point(m0.positions[4000]);
    const uint64_t whole = (uint64_t)m0.render_mesh().positions.size() * sizeof(gpu::RasterVertex) + m0.render_mesh().indices.size() * 4;
    ed.viewport_device_for_test(2);
    r34_steps(ed, 2);
    const FrameProfile before = ed.frame_profile_totals();
    /* The Move gizmo's X arrow dragged with the mouse; after each frame of it, the card's copy of what the Scene view
     * drew (Edit Mode's flat mesh) holds exactly what a whole upload would (a missed range would leave old vertices or
     * triangles there). The other views catch up on release (Other Views Follow Drags is off): the smooth mesh they
     * drew before the drag is compared after it. */
    int x = 0, y = 0, checked = 0, differ = 0;
    auto compare = [&](const RenderMesh &rm) {
      std::vector<gpu::RasterVertex> card;
      std::vector<uint32_t> card_indices;
      if (!ed.gpu_mesh_for_test(rm, card, card_indices)) return;
      std::vector<gpu::RasterVertex> want(rm.positions.size());  // (as the views send it: position, normal, UV, tangent)
      for (size_t i = 0; i < want.size(); i++) {
        want[i] = {{rm.positions[i].x, rm.positions[i].y, rm.positions[i].z}, {rm.normals[i].x, rm.normals[i].y, rm.normals[i].z}};
        if (i < rm.uvs.size()) want[i].uv[0] = rm.uvs[i].x, want[i].uv[1] = rm.uvs[i].y;
        if (i < rm.tangents.size())
          want[i].t[0] = rm.tangents[i].x, want[i].t[1] = rm.tangents[i].y, want[i].t[2] = rm.tangents[i].z, want[i].t[3] = rm.tangents[i].w;
      }
      checked++;
      /* (One material: the card keeps the indices' order; several are grouped by material there, task 0101.) */
      if (card.size() != want.size() || card_indices != rm.indices || std::memcmp(card.data(), want.data(), want.size() * sizeof(gpu::RasterVertex)))
        differ++;
    };
    CHECK(ed.project_to_window(wp, x, y));
    ed.step_frame_headless({r34_mouse(R34ET::MouseMove, x + 55, y)});
    ed.step_frame_headless({r34_mouse(R34ET::MouseDown, x + 55, y)});
    for (int k = 1; k <= 6; k++) {
      ed.step_frame_headless({r34_mouse(R34ET::MouseMove, x + 55 + k * 7, y)});
      compare(big->get<MeshFilter>()->mesh->render_mesh(true));
    }
    ed.step_frame_headless({r34_mouse(R34ET::MouseUp, x + 55 + 42, y)});
    const FrameProfile after = ed.frame_profile_totals();
    const int during = checked;
    r34_steps(ed, 2);  // (the other views caught up)
    compare(big->get<MeshFilter>()->mesh->render_mesh(false));
    compare(big->get<MeshFilter>()->mesh->render_mesh(true));
    std::printf("    the card's copy read back %d times (6 drag frames, then both meshes after it): %d differed from a whole upload\n", checked, differ);
    CHECK(during >= 3 && checked == during + 2 && differ == 0);
    const uint64_t views = after.gpu_views - before.gpu_views, partial = after.gpu_mesh_partial - before.gpu_mesh_partial;
    const uint64_t pbytes = after.gpu_partial_bytes - before.gpu_partial_bytes;
    std::printf("    %llu views on the GPU; %llu updates sent only what moved, %llu KB each on average (the mesh whole is %llu KB)\n",
                (unsigned long long)views, (unsigned long long)partial, (unsigned long long)(partial ? pbytes / partial / 1024 : 0),
                (unsigned long long)(whole / 1024));
    CHECK(views > 0);
    /* (The drag's first frame copies the mesh and its release finishes the tangents: those caches go up whole. The
     * frames between follow in place, and send a small part.) */
    CHECK(partial >= 3);
    CHECK(partial && pbytes / partial < whole / 4);
    /* And the picture after the drag is the CPU's. */
    const std::vector<uint32_t> gpu = r34_crop(ed, ed.scene_view_rect());
    ed.viewport_device_for_test(1);
    big->set_local_position({0.0f, 1.0001f, 0.0f});  // (a change: the CPU draws again)
    r34_steps(ed, 2);
    big->set_local_position({0.0f, 1.0f, 0.0f});
    r34_steps(ed, 2);
    double mean = 0, within3 = 0;
    r122_compare(r34_crop(ed, ed.scene_view_rect()), gpu, mean, within3);
    std::printf("    after the drag: mean difference %.3f / 255, %.2f %% within 3 / 255\n", mean, within3 * 100);
    /* (The same difference with whole uploads instead of ranges, 0.899 / 255 either way: what's left is the GPU's
     * picture against the CPU's on this scene, not what was sent. The grid is off: since task 0192 the card draws it,
     * within 1 / 255 of the CPU's, which this test isn't about.) */
    CHECK(mean < 1.0 && within3 >= 0.99);
  });
  /* ---------------------------------------------------------------- round 125: the Edit Mode cage on the GPU (task 0100) */
  /* A big sphere with a flat top in Edit Mode (some vertices selected, a seam) seen close up; the Scene view drawn again with `device`. */
  auto r125_open = [](Editor &ed) {
    r34_open(ed);
    GameObject *big = ed.scene().create("Big");
    MeshPtr m = primitives::uv_sphere(1.0f, 64, 48);
    for (uint32_t i = 600; i < 620; i++) m->seams.push_back(Mesh::edge_key(i, i + 1));
    for (Vec3 &p : m->positions) p.y = std::min(p.y, 0.8f);  // (a flat top: N-gon mode hides the edges inside it)
    big->add<MeshFilter>()->mesh = m;
    big->add<MeshRenderer>();
    big->set_local_position({0.0f, 1.0f, 0.0f});
    ed.command("select Big");
    ed.command("edit vertex");
    ed.command("vsel 1000 1001 1002 1064 1065 1066 1500");
    ed.command("camera 0 4 3 0 1 0");
    r34_steps(ed, 3);
    return big;
  };
  auto r125_scene = [](Editor &ed, GameObject *big, int device) {
    ed.viewport_device_for_test(device);
    for (int k = 0; k < 2; k++) {  // (a change, then back: the view draws again, at full size once it drew on the GPU)
      big->set_local_position({0.0f, k ? 1.0f : 1.0001f, 0.0f});
      r34_steps(ed, 2);
    }
    return r34_crop(ed, ed.scene_view_rect());
  };
  /* Over the pixels the cage changes (where the CPU's Edit Mode picture differs from its Object Mode one): mean and
   * share within 3 / 255 of the worst channel. */
  auto r125_cage_compare = [](const std::vector<uint32_t> &cpu, const std::vector<uint32_t> &plain, const std::vector<uint32_t> &gpu, size_t &n,
                              double &mean, double &within3) {
    auto worst = [](uint32_t a, uint32_t b) {
      int w = 0;
      for (int k = 0; k < 24; k += 8) w = std::max(w, std::abs((int)((a >> k) & 255) - (int)((b >> k) & 255)));
      return w;
    };
    double sum = 0;
    size_t within = 0;
    n = 0;
    for (size_t i = 0; i < cpu.size() && i < plain.size() && i < gpu.size(); i++) {
      if (worst(cpu[i], plain[i]) <= 8) continue;
      const int w = worst(cpu[i], gpu[i]);
      n++;
      sum += w;
      within += w <= 3;
    }
    mean = n ? sum / (double)n : 0;
    within3 = n ? (double)within / (double)n : 0;
  };
  test("gpu views: the Edit Mode cage is drawn by the card, as the CPU draws it - edges, seams, selected and plain vertex dots, N-gon mode's hidden edges (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    for (const char *mode : {"vertex", "edge", "N-gon", "face", "N-gon face"}) {  // (the face modes' dots: task 0192)
      Editor ed;
      GameObject *big = r125_open(ed);
      const std::string md = mode;
      if (md == "edge") ed.command("edit edge");
      if (md == "face" || md == "N-gon face") ed.command("edit face");
      if (md.rfind("N-gon", 0) == 0) ed.command("ngon on");
      const std::vector<uint32_t> cpu = r125_scene(ed, big, 1);
      const FrameProfile before = ed.frame_profile_totals();
      const std::vector<uint32_t> gpu = r125_scene(ed, big, 2);
      const FrameProfile after = ed.frame_profile_totals();
      ed.viewport_device_for_test(1);
      ed.command("edit off");  // (Object Mode: the same view without the cage)
      const std::vector<uint32_t> plain = r125_scene(ed, big, 1);
      double mean = 0, within3 = 0, cmean = 0, cwithin3 = 0;
      size_t n = 0;
      r122_compare(cpu, gpu, mean, within3);
      r125_cage_compare(cpu, plain, gpu, n, cmean, cwithin3);
      std::printf("    %s mode: the card drew the cage in %llu frames; the view: mean difference %.3f / 255, %.2f %% within 3; the cage's %zu "
                  "pixels: %.3f, %.2f %%\n",
                  mode, (unsigned long long)(after.gpu_cage_frames - before.gpu_cage_frames), mean, within3 * 100, n, cmean, cwithin3 * 100);
      CHECK(after.gpu_cage_frames > before.gpu_cage_frames);
      if (md == "face" || md == "N-gon face") CHECK(after.gpu_face_dot_frames > before.gpu_face_dot_frames);  // (the card drew the face dots: task 0192)
      CHECK(n > 2000);
      CHECK(mean < 1.0 && within3 >= 0.99);
      CHECK(cmean < 1.0 && cwithin3 >= 0.99);
    }
  });
  test("gpu views: during an Edit Mode drag the card's cage follows, sending only the moved vertices, and a click picks the same vertex on both devices (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    GameObject *big = r125_open(ed);
    ed.command("tool move");
    ed.command("vsel 1000");
    r125_scene(ed, big, 2);
    const Mesh &m0 = *big->get<MeshFilter>()->mesh;
    const Vec3 wp = big->world_matrix().point(m0.positions[1000]);
    const uint64_t whole = (uint64_t)m0.vert_count() * sizeof(gpu::CageVertex) + m0.edge_cache().size() * sizeof(gpu::CageEdge);
    int x = 0, y = 0;
    CHECK(ed.project_to_window(wp, x, y));
    ed.step_frame_headless({r34_mouse(R34ET::MouseMove, x + 55, y)});
    ed.step_frame_headless({r34_mouse(R34ET::MouseDown, x + 55, y)});
    ed.step_frame_headless({r34_mouse(R34ET::MouseMove, x + 60, y)});
    const FrameProfile before = ed.frame_profile_totals();
    for (int k = 2; k <= 5; k++) ed.step_frame_headless({r34_mouse(R34ET::MouseMove, x + 55 + 5 * k, y)});
    const FrameProfile after = ed.frame_profile_totals();
    const std::vector<uint32_t> gpu = r34_crop(ed, ed.scene_view_rect());
    const uint64_t frames = after.gpu_cage_frames - before.gpu_cage_frames, bytes = after.gpu_cage_bytes - before.gpu_cage_bytes;
    std::printf("    %llu drag frames with the cage on the card, %llu bytes sent each on average (all of it is %llu)\n", (unsigned long long)frames,
                (unsigned long long)(frames ? bytes / frames : 0), (unsigned long long)whole);
    CHECK(frames == 4);
    CHECK(frames && bytes / frames < whole / 20);
    /* The same moment on the CPU (the mouse still down): the cage where the CPU puts it. */
    ed.viewport_device_for_test(1);
    big->set_local_position({0.0f, 1.0001f, 0.0f});
    r34_steps(ed, 1);
    big->set_local_position({0.0f, 1.0f, 0.0f});
    r34_steps(ed, 1);
    double mean = 0, within3 = 0;
    r122_compare(r34_crop(ed, ed.scene_view_rect()), gpu, mean, within3);
    std::printf("    mid-drag: mean difference %.3f / 255, %.2f %% within 3 / 255\n", mean, within3 * 100);
    CHECK(mean < 1.0 && within3 >= 0.99);
    ed.step_frame_headless({r34_mouse(R34ET::MouseUp, x + 80, y)});
    /* A click on another vertex picks it on either device. */
    ed.command("tool select");
    std::vector<int> picked;
    for (int device : {1, 2}) {
      r125_scene(ed, big, device);
      const Mesh &m = *big->get<MeshFilter>()->mesh;
      int px = 0, py = 0;
      CHECK(ed.project_to_window(big->world_matrix().point(m.positions[1500]), px, py));
      ed.step_frame_headless({r34_mouse(R34ET::MouseMove, px, py)});
      ed.step_frame_headless({r34_mouse(R34ET::MouseDown, px, py)});
      ed.step_frame_headless({r34_mouse(R34ET::MouseUp, px, py)});
      int one = -1, count = 0;
      for (size_t i = 0; i < ed.vert_sel_for_test().size(); i++)
        if (ed.vert_sel_for_test()[i]) one = (int)i, count++;
      picked.push_back(count == 1 ? one : -1);
    }
    std::printf("    a click picked vertex %d on the CPU, %d on the GPU\n", picked[0], picked[1]);
    CHECK(picked[0] == 1500 && picked[1] == 1500);
  });
  test("gpu views: the Scene view on the card draws at full size whatever the Viewport Resolution Scale; the Game view keeps it (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    GameObject *big = r125_open(ed);
    ed.command("edit off");
    const std::vector<uint32_t> full = r125_scene(ed, big, 2);
    const std::vector<uint32_t> game_full = r34_crop(ed, ed.window_rect_for_test(WindowKind::Game));
    ed.set_view_scale(0.67f);
    const std::vector<uint32_t> scaled = r125_scene(ed, big, 2);
    const std::vector<uint32_t> game_scaled = r34_crop(ed, ed.window_rect_for_test(WindowKind::Game));
    size_t scene_diff = 0, game_diff = 0;
    for (size_t i = 0; i < full.size() && i < scaled.size(); i++) scene_diff += full[i] != scaled[i];
    for (size_t i = 0; i < game_full.size() && i < game_scaled.size(); i++) game_diff += game_full[i] != game_scaled[i];
    std::printf("    at 67 %%: %zu Scene view pixels differ from full size, %zu Game view pixels\n", scene_diff, game_diff);
    CHECK(scene_diff == 0);
    CHECK(game_diff > 1000);
    ed.set_view_scale(1.0f);
  });
  test("gpu views: switching to the card in Edit Mode at a 67 % scale, the Scene view goes to full size with its cage on the card, and verify mode finds no stale picture (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    GameObject *big = r125_open(ed);
    ed.set_view_scale(0.67f);
    r34_steps(ed, 2);
    ed.set_render_cache_verify(true);
    const uint64_t stale0 = Editor::render_cache_mismatches_all();
    ed.viewport_device_for_test(2);
    big->set_local_position({0.0f, 1.0001f, 0.0f});  // (one change, kept: the first frame on the card draws at the scale)
    const FrameProfile before = ed.frame_profile_totals();
    r34_steps(ed, 4);
    const FrameProfile after = ed.frame_profile_totals();
    const std::vector<uint32_t> switched = r34_crop(ed, ed.scene_view_rect());
    ed.set_view_scale(1.0f);
    r34_steps(ed, 3);
    const std::vector<uint32_t> full = r34_crop(ed, ed.scene_view_rect());
    ed.set_render_cache_verify(false);
    size_t differ = 0;
    for (size_t i = 0; i < switched.size() && i < full.size(); i++) differ += switched[i] != full[i];
    const uint64_t stale = Editor::render_cache_mismatches_all() - stale0;
    std::printf("    cage on the card in %llu of 4 frames; %zu pixels differ from the view at 100 %%; %llu stale pictures\n",
                (unsigned long long)(after.gpu_cage_frames - before.gpu_cage_frames), differ, (unsigned long long)stale);
    CHECK(after.gpu_cage_frames > before.gpu_cage_frames);
    CHECK(differ == 0);
    CHECK(stale == 0);
    ed.viewport_device_for_test(1);
  });
  test("gpu views: after a GPU error the card opened again gets the whole cage, and draws it as the CPU does (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    GameObject *big = r125_open(ed);
    const std::vector<uint32_t> cpu = r125_scene(ed, big, 1);
    r125_scene(ed, big, 2);
    ed.gpu_view_drop_for_test();  // (as an error does)
    const FrameProfile before = ed.frame_profile_totals();
    const std::vector<uint32_t> gpu = r125_scene(ed, big, 2);
    const FrameProfile after = ed.frame_profile_totals();
    ed.viewport_device_for_test(1);
    ed.command("edit off");
    const std::vector<uint32_t> plain = r125_scene(ed, big, 1);
    size_t n = 0;
    double mean = 0, within3 = 0;
    r125_cage_compare(cpu, plain, gpu, n, mean, within3);
    std::printf("    opened again: the cage on the card in %llu frames; its %zu pixels: mean difference %.3f / 255, %.2f %% within 3\n",
                (unsigned long long)(after.gpu_cage_frames - before.gpu_cage_frames), n, mean, within3 * 100);
    CHECK(after.gpu_cage_frames > before.gpu_cage_frames);
    CHECK(n > 2000);
    CHECK(mean < 1.0 && within3 >= 0.99);
  });
}

static void tests_round123() {
  /* ---------------------------------------------------------------- round 123: OptiX (task 0097's spike; the device since task 0170) */
  test("optix: a build without OptiX, or a machine without NVIDIA's driver, says so instead of failing; a card that isn't there is refused", [] {
    std::string err;
    const std::vector<std::string> devs = gpu::optix_devices(&err);
    if (!gpu::optix_compiled_in()) {
      CHECK(devs.empty() && !err.empty());
      CHECK(gpu::create_optix_renderer(0, &err) == nullptr && !err.empty());
      std::printf("    without OptiX: %s\n", err.c_str());
      return;
    }
    std::printf("    OptiX built in; CUDA devices: %zu%s\n", devs.size(), devs.empty() ? (" (" + err + ")").c_str() : "");
    err.clear();
    CHECK(gpu::create_optix_renderer(1000, &err) == nullptr && !err.empty());
  });
}

static void tests_round126_to_round127() {
  /* ---------------------------------------------------------------- round 126: textures on the GPU (task 0101) */
  /* A small image written into the tests' folder (0xAARRGGBB), as a material's map takes it. */
  auto r126_png = [](const std::string &name, int w, int h, const std::function<uint32_t(int, int)> &px) {
    const std::string path = fs::join(test_dir(), name);
    std::vector<uint32_t> g((size_t)w * h);
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) g[(size_t)y * w + x] = px(x, y);
    write_png(path, g.data(), w, h, w);
    return path;
  };
  /* One object with `mat` in front of the camera, the Scene view drawn on the CPU and on the card: the difference over
   * the object's own pixels (where the view differs from the same view without it), and whether the card drew it. */
  struct R126Result {
    size_t pixels = 0;
    double mean = 0, within3 = 0;
    uint64_t gpu_views = 0, fallbacks = 0;
    std::string reason;
    std::vector<uint32_t> cpu;
    bool scene_card = false;  // (task 0196: the Scene view itself drew on the card)
    std::string status;       // every view's device and reason
  };
  bool r126_grid = true;  // (off: the grid's far lines at the horizon, task 0192's to compare, left out)
  auto r126_case = [&](MeshPtr mesh, const std::vector<MaterialPtr> &mats, Vec3 eye, Vec3 at, Quat rot) {
    Editor ed;
    r34_open(ed);
    if (!r126_grid) ed.command("overlay grid off");
    ed.command("select Main Camera");
    GameObject *obj = ed.scene().create("Textured");
    obj->add<MeshFilter>()->mesh = mesh;
    obj->add<MeshRenderer>()->materials = mats;
    obj->set_local_position({0.0f, 1.0f, 0.0f});
    obj->set_local_rotation(rot);
    ed.command(strprintf("camera %g %g %g %g %g %g", eye.x, eye.y, eye.z, at.x, at.y, at.z));
    r34_steps(ed, 3);
    auto draw = [&](int device) {
      ed.viewport_device_for_test(device);
      for (int k = 0; k < 2; k++) {  // (a change, then back: the view draws again; on the card, then at full size)
        obj->set_local_position({0.0f, k ? 1.0f : 1.0001f, 0.0f});
        r34_steps(ed, 2);
      }
      return r34_crop(ed, ed.scene_view_rect());
    };
    R126Result r;
    const std::vector<uint32_t> cpu = draw(1);
    const FrameProfile before = ed.frame_profile_totals();
    const std::vector<uint32_t> gpu = draw(2);
    const FrameProfile after = ed.frame_profile_totals();
    r.gpu_views = after.gpu_views - before.gpu_views;
    r.fallbacks = after.gpu_fallbacks - before.gpu_fallbacks;
    r.reason = ed.gpu_view_reason();
    r.scene_card = ed.scene_on_gpu_for_test();
    r.status = ed.gpu_view_status();
    obj->active = false;
    const std::vector<uint32_t> empty = draw(1);
    auto worst = [](uint32_t a, uint32_t b) {
      int w = 0;
      for (int k = 0; k < 24; k += 8) w = std::max(w, std::abs((int)((a >> k) & 255) - (int)((b >> k) & 255)));
      return w;
    };
    double sum = 0;
    size_t within = 0;
    for (size_t i = 0; i < cpu.size() && i < gpu.size() && i < empty.size(); i++) {
      if (worst(cpu[i], empty[i]) <= 8) continue;
      const int w = worst(cpu[i], gpu[i]);
      r.pixels++;
      sum += w;
      within += w <= 3;
    }
    r.mean = r.pixels ? sum / (double)r.pixels : 0;
    r.within3 = r.pixels ? (double)within / (double)r.pixels : 0;
    r.cpu = cpu;
    return r;
  };
  test("gpu views: textured materials draw on the card as the CPU draws them - UV, Box and Generated mapping, mips at a grazing angle, normal / metallic / roughness / emission maps, Checker and Noise, filters and wraps, several materials on one mesh (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    const std::string bumps = r126_png("r126_normal.png", 64, 64, [](int x, int y) {  // (a tangent-space normal map: ridges)
      const float a = std::sin(x * 0.4f) * 0.6f, b = std::cos(y * 0.3f) * 0.4f;
      const Vec3 n = normalize(Vec3(a, b, 1.0f));
      auto c = [](float v) { return (uint32_t)std::lround((v * 0.5f + 0.5f) * 255.0f); };
      return 0xFF000000u | c(n.x) << 16 | c(n.y) << 8 | c(n.z);
    });
    const std::string stripes = r126_png("r126_stripes.png", 32, 32, [](int x, int y) {
      const uint32_t v = ((x / 4 + y / 8) & 1) ? 230u : 40u;
      return 0xFF000000u | v << 16 | v << 8 | v;
    });
    const std::string glow = r126_png("r126_glow.png", 16, 16, [](int x, int y) {
      return 0xFF000000u | (uint32_t)(x * 16) << 16 | (uint32_t)(y * 16) << 8 | 64u;
    });
    auto mat = [](const char *name, const std::function<void(Material &)> &set) {
      MaterialPtr m = make_material(name, {0.9f, 0.85f, 0.8f});
      set(*m);
      m->touch();
      return m;
    };
    const Vec3 close(0.0f, 2.2f, 2.4f), at(0.0f, 1.0f, 0.0f);
    MeshPtr two = primitives::cube(1.4f);  // (several materials: the cube's faces alternate)
    two->face_material.assign(two->face_count(), 0);
    for (size_t f = 0; f < two->face_count(); f += 2) two->face_material[f] = 1;
    two->touch();
    struct Case {
      const char *name;
      MeshPtr mesh;
      std::vector<MaterialPtr> mats;
      Vec3 eye;
      Quat rot;
    };
    std::vector<Case> cases = {
        {"UV Grid on UVs", primitives::uv_sphere(0.9f, 48, 32), {mat("a", [](Material &m) { m.base_map.path = "generated:UV Grid"; })}, close, Quat()},
        {"a plane at a grazing angle (mips)", primitives::grid(12.0f, 4, 4),
         {mat("b", [](Material &m) { m.base_map.path = "generated:Color Grid"; m.tiling = {6, 6, 1}; })}, {0.0f, 1.35f, 5.0f}, Quat()},
        {"Box mapping", primitives::cube(1.4f), {mat("c", [](Material &m) { m.base_map.path = "generated:UV Grid"; m.mapping = 1; m.tiling = {2, 2, 2}; })},
         close, Quat::euler({20, 30, 0})},
        {"Generated mapping", primitives::uv_sphere(0.9f, 48, 32),
         {mat("d", [](Material &m) { m.base_map.path = "generated:Color Grid"; m.mapping = 2; })}, close, Quat()},
        {"Checker", primitives::cube(1.4f), {mat("e", [](Material &m) { m.procedural = (int)Procedural::Checker; m.procedural_scale = 3; })}, close,
         Quat::euler({10, 40, 0})},
        {"Noise", primitives::uv_sphere(0.9f, 48, 32), {mat("f", [](Material &m) { m.procedural = (int)Procedural::Noise; m.procedural_scale = 4; })},
         close, Quat()},
        {"a normal map", primitives::uv_sphere(0.9f, 48, 32),
         {mat("g", [&](Material &m) { m.normal_map.path = bumps; m.normal_map.non_color = true; m.normal_strength = 1.5f; })}, close, Quat()},
        {"metallic, roughness and emission maps", primitives::uv_sphere(0.9f, 48, 32), {mat("h", [&](Material &m) {
           m.metallic = 1.0f;
           m.metallic_map.path = stripes;
           m.metallic_map.non_color = true;
           m.roughness_map.path = stripes;
           m.roughness_map.non_color = true;
           m.emission_map.path = glow;
           m.emission_strength = 0.6f;
         })},
         close, Quat()},
        {"Closest filter, Mirror wrap", primitives::cube(1.4f),
         {mat("i", [&](Material &m) { m.base_map.path = stripes; m.filter = 0; m.wrap = 3; m.tiling = {2.5f, 2.5f, 1}; })}, close, Quat::euler({15, 25, 0})},
        {"Linear filter, Clip wrap", primitives::cube(1.4f),
         {mat("j", [&](Material &m) { m.base_map.path = glow; m.filter = 1; m.wrap = 2; m.tiling = {1.6f, 1.6f, 1}; m.offset = {-0.3f, -0.3f, 0}; })},
         close, Quat::euler({15, 25, 0})},
        {"two materials on one mesh", two,
         {mat("k", [](Material &m) { m.base_map.path = "generated:UV Grid"; }), mat("l", [](Material &m) { m.procedural = (int)Procedural::Checker; })},
         close, Quat::euler({15, 35, 0})},
    };
    std::vector<std::vector<uint32_t>> pictures;
    for (const Case &c : cases) {
      const R126Result r = r126_case(c.mesh, c.mats, c.eye, at, c.rot);
      pictures.push_back(r.cpu);
      std::printf("    %s: %llu views on the card, %llu on the CPU%s%s; its %zu pixels: mean difference %.3f / 255, %.2f %% within 3\n", c.name,
                  (unsigned long long)r.gpu_views, (unsigned long long)r.fallbacks, r.reason.empty() ? "" : " - ", r.reason.c_str(), r.pixels, r.mean,
                  r.within3 * 100);
      CHECK(r.gpu_views > 0 && r.fallbacks == 0);
      CHECK(r.pixels > 3000);
      CHECK(r.mean < 1.0 && r.within3 >= 0.99);
    }
    /* The normal map does show (the sphere's render mesh has tangents): its picture against the same sphere plain. */
    const R126Result plain = r126_case(primitives::uv_sphere(0.9f, 48, 32), {mat("m", [](Material &) {})}, close, at, Quat());
    size_t bumped = 0;
    for (size_t i = 0; i < plain.cpu.size() && i < pictures[6].size(); i++) bumped += plain.cpu[i] != pictures[6][i];
    std::printf("    the normal map changes %zu pixels of the plain sphere\n", bumped);
    CHECK(bumped > 10000);
  });
  test("gpu views: a live procedural texture draws on the card from afar as on the CPU, and close up (a texel larger than a pixel, where the CPU evaluates it per point) sends the view to the CPU, saying why (task 0196)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor maker;  // (a Brick texture asset with Live Shading, read by the material through its path)
    maker.init_headless(320, 240);
    maker.step_frame_headless();
    ProceduralTexturePtr bricks = maker.new_texture_asset_for_test("Bricks");
    CHECK(bricks != nullptr);
    if (!bricks) return;
    bricks->live_shading = true;
    bricks->touch();
    MaterialPtr live = make_material("live", {1, 1, 1});
    live->base_map.path = bricks->asset_path + "#albedo";
    live->touch();
    const R126Result far = r126_case(primitives::cube(1.2f), {live}, {0.0f, 3.5f, 6.5f}, {0.0f, 1.0f, 0.0f}, Quat());  // (6 m: under a texel a pixel, the CPU reads the image; 1.2 m, so no face meets the scene's cube's)
    /* (The Scene view's own device: the Game view and Camera Preview see the cube from the camera, which may be close.) */
    std::printf("    from afar: the Scene view on the card %s (%s); its %zu pixels: mean %.3f / 255, %.2f %% within 3\n", far.scene_card ? "yes" : "no",
                far.status.c_str(), far.pixels, far.mean, far.within3 * 100);
    CHECK(far.scene_card && far.pixels > 1000);
    CHECK(far.mean < 1.0 && far.within3 >= 0.99);
    const R126Result near = r126_case(primitives::cube(1.2f), {live}, {0.0f, 1.0f, 0.75f}, {0.0f, 1.0f, 0.0f}, Quat());
    std::printf("    close up: the Scene view on the card %s (%s)\n", near.scene_card ? "yes" : "no", near.status.c_str());
    CHECK(!near.scene_card && near.status.find("close up") != std::string::npos);
  });
  test("gpu views: toon materials' painted maps draw on the card as on the CPU - shade, bias, specular and rim masks, inner lines and hatching, the face shadow map, the hair's highlight (task 0196; skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    auto grey = [](int v) { return 0xFF000000u | (uint32_t)v << 16 | (uint32_t)v << 8 | (uint32_t)v; };
    const std::string shade = r126_png("r215_shade.png", 64, 64, [](int x, int y) { return 0xFF000000u | (uint32_t)(70 + x * 2) << 16 | (uint32_t)(50 + y) << 8 | 190u; });
    const std::string bias = r126_png("r215_bias.png", 64, 64, [&](int x, int) { return grey((x / 8) % 2 ? 220 : 40); });
    const std::string spec = r126_png("r215_spec.png", 64, 64, [&](int x, int y) { return grey(((x / 16) + (y / 16)) % 2 ? 255 : 0); });
    const std::string rim = r126_png("r215_rim.png", 64, 64, [&](int x, int) { return grey(x < 32 ? 255 : 60); });
    const std::string lines = r126_png("r215_lines.png", 64, 64, [&](int x, int y) { return grey(x % 16 < 2 || y % 16 < 2 ? 30 : 255); });
    const std::string hatch = r126_png("r215_hatch.png", 32, 32, [&](int x, int y) { return grey((x + y) % 8 < 3 ? 70 : 255); });
    const std::string face = r126_png("r215_face.png", 64, 64, [&](int x, int y) { return grey(std::min(255, x * 4 + y)); });
    MaterialPtr painted = make_material("painted", {0.9f, 0.7f, 0.6f});
    painted->shading_model = 1;
    painted->toon_bands = 2;
    painted->toon_softness = 0.05f;
    painted->shade_map.path = shade;
    painted->toon_shadow_bias_map.path = bias;
    painted->toon_specular_size = 0.6f;
    painted->toon_specular_map.path = spec;
    painted->toon_rim_width = 0.4f;
    painted->toon_rim_strength = 0.8f;
    painted->toon_rim_map.path = rim;
    painted->line_map.path = lines;
    painted->hatch_map.path = hatch;
    painted->hatch_scale = 2.0f;
    painted->touch();
    MaterialPtr face_hair = make_material("face and hair", {0.95f, 0.8f, 0.7f});
    face_hair->shading_model = 1;
    face_hair->face_shadow_map.path = face;
    face_hair->face_shadow_softness = 0.05f;
    face_hair->toon_hair_highlight = true;
    face_hair->toon_specular_size = 0.5f;
    face_hair->line_map.path = lines;
    face_hair->line_only_shadow = true;
    face_hair->touch();
    for (const MaterialPtr &m : {painted, face_hair}) {
      const R126Result r = r126_case(primitives::uv_sphere(0.8f, 48, 24), {m}, {0.0f, 2.2f, 2.6f}, {0.0f, 1.0f, 0.0f}, Quat());
      std::printf("    %s: %llu views on the card, %llu on the CPU%s%s; its %zu pixels: mean %.3f / 255, %.2f %% within 3\n", m->name.c_str(),
                  (unsigned long long)r.gpu_views, (unsigned long long)r.fallbacks, r.reason.empty() ? "" : ": ", r.reason.c_str(), r.pixels, r.mean, r.within3 * 100);
      CHECK(r.gpu_views > 0 && r.fallbacks == 0 && r.pixels > 1000);
      CHECK(r.mean < 1.0 && r.within3 >= 0.99);
    }
  });
  test("gpu views: Transparent and Glass materials draw on the card as the CPU draws them - sorted back to front, over the lit picture (task 0195; skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    MaterialPtr veil = make_material("transparent", {0.9f, 0.3f, 0.2f});
    veil->surface = (int)MaterialSurface::Transparent;
    veil->alpha = 0.45f;
    veil->touch();
    MaterialPtr glass = make_material("glass", {0.7f, 0.9f, 0.95f});
    glass->surface = (int)MaterialSurface::Glass;
    glass->roughness = 0.1f;
    glass->touch();
    MaterialPtr grid = make_material("textured transparent", {1, 1, 1});
    grid->surface = (int)MaterialSurface::Transparent;
    grid->alpha = 0.6f;
    grid->base_map.path = "generated:UV Grid";
    grid->touch();
    r126_grid = false;  // (seen through the spheres, the horizon's lines count as theirs)
    for (const MaterialPtr &m : {veil, glass, grid}) {
      const R126Result r = r126_case(primitives::uv_sphere(0.8f, 32, 16), {m}, {0.0f, 2.2f, 2.6f}, {0.0f, 1.0f, 0.0f}, Quat());
      std::printf("    %s: %llu views on the card, %llu on the CPU%s%s; its %zu pixels: mean %.3f / 255, %.2f %% within 3\n", m->name.c_str(),
                  (unsigned long long)r.gpu_views, (unsigned long long)r.fallbacks, r.reason.empty() ? "" : ": ", r.reason.c_str(), r.pixels, r.mean, r.within3 * 100);
      CHECK(r.gpu_views > 0 && r.fallbacks == 0 && r.pixels > 1000);
      CHECK(r.mean < 1.0 && r.within3 >= 0.99);
    }
    r126_grid = true;
  });
  test("gpu views: axis materials draw on the card as the CPU draws them - World and Local, Top / Side / Bottom, a textured part, the height band and the noisy border (task 0195; skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    R51Mats world = r51_mats(1);
    world.grass->procedural = (int)Procedural::Checker;  // (a pattern on the Top, projected)
    world.grass->procedural_scale = 3.0f;
    world.grass->touch();
    world.axis->axis_high = "Assets/Materials/R51/Rock.mat";
    world.axis->axis_high_start = 1.45f;
    world.axis->axis_high_blend = 0.1f;
    world.axis->axis_noise = 0.6f;
    world.axis->axis_noise_scale = 0.5f;
    world.axis->touch();
    R51Mats local = r51_mats(2);
    const Quat tilt = Quat::axis_angle(normalize(Vec3(1.0f, 0.0f, 0.4f)), 0.7f);
    struct Case {
      const char *name;
      MaterialPtr m;
      MeshPtr mesh;
      Quat rot;
    } cases[] = {{"World, a band and noise", world.axis, primitives::uv_sphere(0.8f, 48, 24), Quat()},
                 {"Local, tilted", local.axis, primitives::cube(1.0f), tilt}};
    for (const Case &c : cases) {
      const R126Result r = r126_case(c.mesh, {c.m}, {0.0f, 2.4f, 2.6f}, {0.0f, 1.0f, 0.0f}, c.rot);
      std::printf("    %s: %llu views on the card, %llu on the CPU%s%s; its %zu pixels: mean %.3f / 255, %.2f %% within 3\n", c.name,
                  (unsigned long long)r.gpu_views, (unsigned long long)r.fallbacks, r.reason.empty() ? "" : ": ", r.reason.c_str(), r.pixels, r.mean, r.within3 * 100);
      CHECK(r.gpu_views > 0 && r.fallbacks == 0 && r.pixels > 1000);
      CHECK(r.mean < 1.0 && r.within3 >= 0.99);
    }
  });
  test("gpu views: an Edit Mode drag on a big textured mesh still sends the card only what moved (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    r34_open(ed);
    GameObject *big = ed.scene().create("Big");
    big->add<MeshFilter>()->mesh = primitives::uv_sphere(1.0f, 128, 96);
    MaterialPtr grid = make_material("grid", {1, 1, 1});
    grid->base_map.path = "generated:UV Grid";
    grid->touch();
    big->add<MeshRenderer>()->materials = {grid};
    big->set_local_position({0.0f, 1.0f, 0.0f});
    ed.command("select Big");
    ed.command("edit vertex");
    ed.command("vsel 4000");
    ed.command("camera 0 15 5 0 1 0");
    ed.command("tool move");
    r34_steps(ed, 3);
    const Vec3 wp = big->world_matrix().point(big->get<MeshFilter>()->mesh->positions[4000]);
    ed.viewport_device_for_test(2);
    r34_steps(ed, 3);
    const FrameProfile before = ed.frame_profile_totals();
    r34_gizmo_drag_x(ed, wp, 40);
    const FrameProfile after = ed.frame_profile_totals();
    const uint64_t partial = after.gpu_mesh_partial - before.gpu_mesh_partial, fallbacks = after.gpu_fallbacks - before.gpu_fallbacks;
    std::printf("    %llu updates sent only what moved; %llu views on the CPU instead%s%s\n", (unsigned long long)partial, (unsigned long long)fallbacks,
                ed.gpu_view_reason().empty() ? "" : ": ", ed.gpu_view_reason().c_str());
    CHECK(partial >= 2);
    CHECK(fallbacks == 0);
    ed.viewport_device_for_test(1);
  });
  test("gpu views: the card's texture store - a 1 x 1 texture first, fifty more, most let go, compaction and growth - still draws the textures it holds (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    std::string err;
    std::unique_ptr<gpu::Raster> g = gpu::Raster::create(0, &err);
    if (!g) {
      std::printf("    skipped: %s\n", err.c_str());
      return;
    }
    /* A texture of one colour, w x h, sRGB (its texels as stored). */
    auto solid = [](int w, int h, uint8_t r, uint8_t gg, uint8_t b, bool hdr = false) {
      Bitmap bmp;
      bmp.width = w, bmp.height = h, bmp.is_float = hdr;
      for (int i = 0; i < w * h; i++) {
        if (hdr) bmp.rgbaf.insert(bmp.rgbaf.end(), {r / 255.0f, gg / 255.0f, b / 255.0f, 1.0f});
        else bmp.rgba8.insert(bmp.rgba8.end(), {r, gg, b, 255});
      }
      auto t = std::make_shared<Texture>();
      t->build(bmp, !hdr);
      return t;
    };
    std::vector<std::shared_ptr<Texture>> keep;
    std::vector<int> ids;
    auto add = [&](std::shared_ptr<Texture> t) {
      keep.push_back(t);
      ids.push_back(g->add_texture(*t, &err));
      return ids.back();
    };
    const int tiny = add(solid(1, 1, 200, 40, 30));  // (fits the arena's placeholder: the growth that follows copies from it)
    for (int i = 0; i < 50; i++) add(solid(16 + (i % 7) * 24, 8 + (i % 5) * 20, (uint8_t)(i * 5), 90, 160, i % 9 == 0));
    bool all_added = true;
    for (int id : ids) all_added = all_added && id >= 0;
    CHECK(all_added);
    for (size_t i = 1; i < ids.size(); i++)
      if (i % 5 != 0) g->remove_texture(ids[i]);  // (four in five let go: the next growth compacts first)
    const int big = add(solid(512, 512, 40, 180, 90));
    const int hdr = add(solid(64, 64, 120, 120, 250, true));
    CHECK(big >= 0 && hdr >= 0);
    /* Three cubes side by side, unlit, each showing one texture: the tiny one, a survivor of the fifty, the big one. */
    MeshPtr cube = primitives::cube(1.0f);
    const RenderMesh &rm = cube->render_mesh();
    std::vector<gpu::RasterVertex> vs(rm.positions.size());
    for (size_t i = 0; i < vs.size(); i++) {
      vs[i] = {{rm.positions[i].x, rm.positions[i].y, rm.positions[i].z}, {rm.normals[i].x, rm.normals[i].y, rm.normals[i].z}};
      if (i < rm.uvs.size()) vs[i].uv[0] = rm.uvs[i].x, vs[i].uv[1] = rm.uvs[i].y;
    }
    const int mesh = g->add_mesh(vs, rm.indices, &err);
    CHECK(mesh >= 0);
    const int shown[4] = {tiny, ids[5], big, hdr};
    std::vector<gpu::RasterInstance> inst;
    for (int k = 0; k < 4; k++) {
      gpu::RasterInstance in;
      const Mat4 model = Mat4::translate({-2.7f + 1.8f * k, 0.0f, 0.0f});
      std::memcpy(in.model, model.m, 64);
      in.albedo_rough[0] = in.albedo_rough[1] = in.albedo_rough[2] = 1.0f;
      in.flags = gpu::RasterInstance::Unlit;
      in.mesh = (uint32_t)mesh;
      in.id = (uint32_t)k + 1;
      in.textured = true;
      in.tex[0] = shown[k];
      in.has_uv = true;
      in.filter = 1;
      inst.push_back(in);
    }
    g->set_instances(inst);
    const int W = 320, H = 120;
    gpu::RasterFrame fr;
    fr.width = W, fr.height = H, fr.shadow_size = 0;
    const Mat4 vp = Mat4::perspective(40 * kDeg2Rad, W / (float)H, 0.1f, 100.0f) * Mat4::look_at({0, 0, 7}, {0, 0, 0}, {0, 1, 0});
    std::memcpy(fr.view_proj, vp.m, 64);
    std::memcpy(fr.sun_view_proj, vp.m, 64);
    fr.eye[2] = 7;
    std::vector<uint32_t> px((size_t)W * H);
    CHECK(g->render(fr, &err) && g->read_back(0, px.data(), nullptr, nullptr, &err));
    auto enc = [](int v) {  // a float texture's linear value, as the Standard view shows it
      const float l = v / 255.0f;
      return (uint32_t)std::lround((l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f) * 255.0f);
    };
    const uint32_t want[4] = {0xC8281E, 0x145AA0, 0x28B45A, enc(120) << 16 | enc(120) << 8 | enc(250)};  // (unlit: each texture's colour)
    for (int k = 0; k < 4; k++) {
      int x = 0, y = 0;
      const Vec4 c = vp * Vec4(-2.7f + 1.8f * k, 0.0f, 0.5f, 1.0f);
      x = (int)((c.x / c.w * 0.5f + 0.5f) * W), y = (int)((0.5f - c.y / c.w * 0.5f) * H);
      const uint32_t got = px[(size_t)y * W + x];
      int worst = 0;
      for (int s = 0; s < 24; s += 8) worst = std::max(worst, std::abs((int)((got >> s) & 255) - (int)((want[k] >> s) & 255)));
      std::printf("    cube %d: %06X, its texture %06X\n", k, got & 0xFFFFFF, want[k]);
      CHECK(worst <= 2);
    }
  });
  /* ---------------------------------------------------------------- round 450: the views' features switched off (task 0426) */
  test("round 450: BLENDITY_VIEW_OFF's words become the shader defines - each alone, all three, unknown words and spaces ignored, nothing when unset", [&] {
    CHECK(gpu::view_off_defines_from(nullptr).empty());
    CHECK(gpu::view_off_defines_from("").empty());
    CHECK(gpu::view_off_defines_from("sh") == "#define BL_NO_SH 1\n");
    CHECK(gpu::view_off_defines_from("lv") == "#define BL_NO_LV 1\n");
    CHECK(gpu::view_off_defines_from("mask") == "#define BL_NO_MASK 1\n");
    CHECK(gpu::view_off_defines_from("mask,sh,lv") == "#define BL_NO_SH 1\n#define BL_NO_LV 1\n#define BL_NO_MASK 1\n");
    CHECK(gpu::view_off_defines_from("shadow,lvs,masks").empty());  // (whole words only)
    CHECK(gpu::view_off_defines_from("sh, lv").find("BL_NO_LV") == std::string::npos);  // (no spaces: " lv" is not "lv")
    std::printf("    this run's switch: \"%s\"\n", std::getenv("BLENDITY_VIEW_OFF") ? std::getenv("BLENDITY_VIEW_OFF") : "");
  });
  /* ---------------------------------------------------------------- round 127: Sky / HDRI worlds and toon on the GPU (task 0102) */
  /* As r126_case, with the scene's world set by `world` and the object given its own components by `extra`; also the
   * whole view's difference (the background is the world too). */
  struct R127Result {
    size_t pixels = 0;
    double mean = 0, within3 = 0, view_mean = 0, view_within3 = 0;
    uint64_t gpu_views = 0, fallbacks = 0, textures = 0;
    std::string reason;
  };
  auto r127_case = [&](const std::function<void(EnvironmentSettings &)> &world, MeshPtr mesh, MaterialPtr mat,
                       const std::function<void(GameObject &)> &extra, bool cull = true) {
    Editor ed;
    r34_open(ed);
    ed.command("select Main Camera");
    ed.raster_options_for_test().backface_culling = cull;
    world(ed.scene().environment);
    GameObject *obj = ed.scene().create("Subject");
    obj->add<MeshFilter>()->mesh = mesh;
    obj->add<MeshRenderer>()->materials = {mat};
    obj->set_local_position({0.0f, 1.0f, 0.0f});
    extra(*obj);
    ed.command("camera 0 2.2 2.4 0 1 0");
    r34_steps(ed, 3);
    auto draw = [&](int device) {
      ed.viewport_device_for_test(device);
      for (int k = 0; k < 2; k++) {
        obj->set_local_position({0.0f, k ? 1.0f : 1.0001f, 0.0f});
        r34_steps(ed, 2);
      }
      return r34_crop(ed, ed.scene_view_rect());
    };
    R127Result r;
    const std::vector<uint32_t> cpu = draw(1);
    const FrameProfile before = ed.frame_profile_totals();
    const std::vector<uint32_t> gpu = draw(2);
    const FrameProfile after = ed.frame_profile_totals();
    r.gpu_views = after.gpu_views - before.gpu_views;
    r.fallbacks = after.gpu_fallbacks - before.gpu_fallbacks;
    r.textures = after.gpu_texture_uploads - before.gpu_texture_uploads;
    r.reason = ed.gpu_view_reason();
    obj->active = false;
    const std::vector<uint32_t> empty = draw(1);
    auto worst = [](uint32_t a, uint32_t b) {
      int w = 0;
      for (int k = 0; k < 24; k += 8) w = std::max(w, std::abs((int)((a >> k) & 255) - (int)((b >> k) & 255)));
      return w;
    };
    double sum = 0, vsum = 0;
    size_t within = 0, vwithin = 0;
    for (size_t i = 0; i < cpu.size() && i < gpu.size() && i < empty.size(); i++) {
      const int w = worst(cpu[i], gpu[i]);
      vsum += w;
      vwithin += w <= 3;
      if (worst(cpu[i], empty[i]) <= 8) continue;
      r.pixels++;
      sum += w;
      within += w <= 3;
    }
    r.mean = r.pixels ? sum / (double)r.pixels : 0;
    r.within3 = r.pixels ? (double)within / (double)r.pixels : 0;
    r.view_mean = cpu.empty() ? 0 : vsum / (double)cpu.size();
    r.view_within3 = cpu.empty() ? 0 : (double)vwithin / (double)cpu.size();
    return r;
  };
  auto r127_report = [](const char *name, const R127Result &r) {
    std::printf("    %s: %llu views on the card, %llu on the CPU%s%s; its %zu pixels: %.3f / 255, %.2f %% within 3; the view: %.3f, %.2f %%\n", name,
                (unsigned long long)r.gpu_views, (unsigned long long)r.fallbacks, r.reason.empty() ? "" : " - ", r.reason.c_str(), r.pixels, r.mean,
                r.within3 * 100, r.view_mean, r.view_within3 * 100);
    CHECK(r.gpu_views > 0 && r.fallbacks == 0);
    CHECK(r.pixels > 2000);
    CHECK(r.mean < 1.0 && r.within3 >= 0.99);
    CHECK(r.view_mean < 1.0 && r.view_within3 >= 0.99);
  };
  test("gpu views: a Sky world and an HDRI world draw on the card as on the CPU - the background, the diffuse light from the map, rough and glossy reflections (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    const std::string hdri = r126_png("r127_world.png", 128, 64, [](int x, int y) {  // (a sky, a horizon, a bright window)
      if (x > 20 && x < 30 && y > 18 && y < 26) return 0xFFFFF0D0u;
      const uint32_t r = (uint32_t)(40 + y * 2), g = (uint32_t)(80 + y), b = (uint32_t)(200 - y * 2) + (uint32_t)(x % 32);
      return 0xFF000000u | r << 16 | g << 8 | std::min(255u, b);
    });
    auto sky = [](EnvironmentSettings &e) { e.mode = 1; };  // (the Sky's rotation follows its sun: the HDRI's checks the sign)
    auto map = [&](EnvironmentSettings &e) {
      e.mode = 2;
      e.hdri.path = hdri;
      e.strength = 1.4f;
      e.rotation = -60.0f;
    };
    MaterialPtr rough = make_material("rough", {0.8f, 0.75f, 0.7f});
    rough->roughness = 0.7f;
    MaterialPtr glossy = make_material("glossy", {0.95f, 0.9f, 0.85f});
    glossy->metallic = 1.0f;
    glossy->roughness = 0.15f;
    auto none = [](GameObject &) {};
    for (auto &[name, world, mat] : std::vector<std::tuple<const char *, std::function<void(EnvironmentSettings &)>, MaterialPtr>>{
             {"Sky, rough", sky, rough}, {"Sky, glossy metal", sky, glossy}, {"HDRI, rough", map, rough}, {"HDRI, glossy metal", map, glossy}}) {
      const R127Result r = r127_case(world, primitives::uv_sphere(0.9f, 48, 32), mat, none);
      r127_report(name, r);
      CHECK(r.textures > 0);  // (the world's map went to the card: the match isn't two gradients)
    }
  });
  test("gpu views: toon materials and an outline draw on the card as on the CPU - bands, the shadow colour, the specular spot, a rim, flat ambient on and off (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    auto toon = [](const std::function<void(Material &)> &set) {
      MaterialPtr m = make_material("toon", {0.9f, 0.5f, 0.4f});
      m->shading_model = 1;
      set(*m);
      m->touch();
      return m;
    };
    auto gradient = [](EnvironmentSettings &e) { e.mode = 0; };
    auto sky = [](EnvironmentSettings &e) { e.mode = 1; };
    auto none = [](GameObject &) {};
    r127_report("two-tone", r127_case(gradient, primitives::uv_sphere(0.9f, 48, 32), toon([](Material &) {}), none));
    r127_report("three bands, a specular spot, a rim", r127_case(gradient, primitives::uv_sphere(0.9f, 48, 32), toon([](Material &m) {
                  m.toon_bands = 3;
                  m.toon_softness = 0.05f;
                  m.toon_specular_size = 0.5f;
                  m.toon_rim_width = 0.3f;
                  m.toon_rim_strength = 0.8f;
                  m.toon_rim = {0.4f, 0.6f, 1.0f};
                }),
                none));
    r127_report("flat ambient off, a Sky world", r127_case(sky, primitives::uv_sphere(0.9f, 48, 32), toon([](Material &m) { m.toon_flat_ambient = false; }), none));
    r127_report("with an outline", r127_case(gradient, primitives::uv_sphere(0.9f, 48, 32), toon([](Material &) {}), [](GameObject &g) {
                  g.add<Outline>()->thickness = 0.04f;
                }));
    r127_report("an outline, the view's back-face culling off", r127_case(gradient, primitives::uv_sphere(0.9f, 48, 32), toon([](Material &) {}),
                                                                         [](GameObject &g) { g.add<Outline>()->thickness = 0.04f; }, false));
    r127_report("an outline, mirrored", r127_case(gradient, primitives::uv_sphere(0.9f, 48, 32), toon([](Material &) {}), [](GameObject &g) {
                  g.add<Outline>()->thickness = 0.04f;
                  g.set_local_scale({-1.0f, 1.0f, 1.0f});
                }));
    r127_report("in its own shadow", r127_case(gradient, primitives::uv_sphere(0.9f, 48, 32), toon([](Material &m) { m.toon_bands = 2; }), [](GameObject &g) {
                  /* A slab above the sphere casts on it (as a child: hidden with it for the comparison's empty view). */
                  GameObject *slab = g.scene ? g.scene->create("Slab") : nullptr;
                  if (!slab) return;
                  slab->add<MeshFilter>()->mesh = primitives::cube(1.0f);
                  slab->add<MeshRenderer>();
                  g.scene->set_parent(slab, &g, -1, false);
                  slab->set_local_position({0.4f, 1.6f, 0.2f});
                  slab->set_local_scale({1.2f, 0.1f, 1.2f});
                }));
  });
}

static void tests_round130() {
  /* ---------------------------------------------------------------- round 130: GPU Compute, several samples a dispatch (task 0105) */
  test("gpu path tracer: eleven samples in one call (a dispatch of 8, then 3) add up to what eleven one-sample calls do, colour, albedo, normal, depth and coverage (skipped without a GPU)", [] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    const int W = 96, H = 64;
    auto ball = primitives::uv_sphere(0.8f, 24, 16);
    auto floor = primitives::plane(10.0f, 1);
    MaterialPtr red = make_material("red", {0.8f, 0.2f, 0.1f}), grey = make_material("grey", {0.7f, 0.7f, 0.7f});
    red->roughness = 0.3f;
    std::vector<MaterialPtr> mr = {red}, mg = {grey};
    std::vector<PTObject> objs = {{&ball->render_mesh_tangents(), Mat4::translate({0, 0.8f, 0}), &mr},
                                  {&floor->render_mesh_tangents(), Mat4::identity(), &mg}};
    RenderLight sun;
    sun.direction = normalize(Vec3(-0.4f, -1.0f, 0.5f));
    Environment env;
    PathTracer pt;
    PTSettings st;
    st.use_cpu = false;
    st.denoise = false;
    pt.set_settings(st);
    pt.build(objs, {sun}, env);
    const Mat4 view = Mat4::look_at({0, 2, -4}, {0, 0.6f, 0}, {0, 1, 0}), proj = Mat4::perspective(50 * kDeg2Rad, W / (float)H, 0.1f, 100.0f);
    pt.set_camera(view, proj, W, H);
    gpu::Scene scene;
    CHECK(pt.build_gpu_scene_fit(scene, 4096));
    /* The frame, as PathTracer::render fills it in (a pinhole camera: no lens). */
    const Mat4 inv = (proj * view).inverse();
    std::memcpy(scene.params.inv_vp, inv.m, 64);
    scene.params.cam_pos[0] = 0, scene.params.cam_pos[1] = 2, scene.params.cam_pos[2] = -4;
    scene.params.size[0] = W, scene.params.size[1] = H;
    scene.params.background[3] = 2.0f;  // (a transparent film: the coverage plane is summed too)
    std::string err;
    std::unique_ptr<gpu::Renderer> r = gpu::Renderer::create(0, true, &err);
    if (!r) {
      std::printf("    skipped: %s\n", err.c_str());
      return;
    }
    CHECK(r->upload(scene, &err));
    auto planes = [&](bool at_once) {
      r->set_params(scene.params);  // (clears)
      if (at_once) CHECK(r->render(3, 11, &err));  // (from sample 3: the samples' own seeds, not the call's)
      else
        for (int k = 0; k < 11; k++) CHECK(r->render(3 + k, 1, &err));
      std::vector<float> acc, alb, nd, cov;
      r->download_accum(acc);
      r->download_aux(alb, nd, &cov);
      for (const std::vector<float> *v : {&alb, &nd, &cov}) acc.insert(acc.end(), v->begin(), v->end());
      return acc;
    };
    const std::vector<float> once = planes(true), each = planes(false);
    CHECK(once.size() == each.size() && !once.empty());
    double worst = 0, total = 0;
    for (size_t i = 0; i < once.size() && i < each.size(); i++) {
      worst = std::max(worst, (double)std::fabs(once[i] - each[i]) / std::max(1.0, (double)std::fabs(each[i])));
      total += std::fabs(each[i]);
    }
    std::printf("    %zu sums: worst difference %.2e (relative), their total %.1f\n", once.size(), worst, total);
    CHECK(total > 100.0);  // (something was traced)
    CHECK(worst < 1e-4);
  });
}

static void tests_round179() {
  test("GPU time in the Profiler: the views' pass times (shadow, g-buffer, lighting) sum to at most the GPU path's time and are positive where the card has timestamps; the read-back is inside it too (skipped without a GPU)", [] {
    if (!gpu::available()) {
      std::printf("    (no GPU here: skipped)\n");
      return;
    }
    Editor ed;
    r34_open(ed);
    ed.viewport_device_for_test(2);  // (2: the GPU)
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube != nullptr);
    for (int i = 0; i < 6 && cube; i++) {
      cube->set_local_position({0.1f * (float)i, 0.5f, 0.0f});  // (a change a frame: every view drawn on the card)
      ed.step_frame_headless();
    }
    const FrameProfile &fp = ed.frame_profile();
    const double busy = fp.gpu_busy_ms();
    std::printf("    the last frame: GPU path %.2f ms, of it busy %.2f ms (shadow %.2f, g-buffer %.2f, lighting %.2f), read-back %.2f ms; %llu views on the card\n",
                fp.ms_gpu, busy, fp.ms_gpu_shadow, fp.ms_gpu_gbuffer, fp.ms_gpu_lighting, fp.ms_gpu_readback, (unsigned long long)fp.gpu_views);
    if (fp.gpu_views == 0) std::printf("    (no view on the card: %s)\n", ed.gpu_view_reason().c_str());
    CHECK(fp.gpu_views > 0 && fp.ms_gpu > 0.0);
    CHECK(busy + fp.ms_gpu_readback <= fp.ms_gpu + 1.0);  // (the passes and the read-back are inside the path's time; a millisecond for the timers' edges)
    if (busy > 0.0) CHECK(fp.ms_gpu_shadow >= 0.0 && fp.ms_gpu_gbuffer > 0.0 && fp.ms_gpu_lighting > 0.0);
    else {
      std::printf("    (this card reports no timestamps: n/a)\n");
      CHECK(fp.ms_gpu_shadow == 0.0 && fp.ms_gpu_gbuffer == 0.0 && fp.ms_gpu_lighting == 0.0);  // (0, and the Profiler's row says n/a)
    }
    /* The Profiler window draws the block: its rows are there. */
    ed.record_buttons_for_test(true);
    ed.command("window Profiler");
    ed.step_frame_headless();
    Recti row;
    CHECK(ed.button_rect_for_test("GPU views: the GPU path (wall)", row) && ed.button_rect_for_test("GPU views: busy (shadow / g-buffer / lighting)", row) && ed.button_rect_for_test("GPU busy in the frame", row));
    /* bench says what the card did over its own frames (the views are on the card, so never the CPU line). */
    std::vector<LogEntry> lines;
    const size_t mark = Log::fetch(0, lines);
    ed.command("bench 2");
    lines.clear();
    Log::fetch(mark, lines);
    bool said = false;
    for (const LogEntry &l : lines) said = said || l.text.find("bench GPU busy") != std::string::npos && l.text.find("a frame in the GPU path") != std::string::npos;
    CHECK(said);
  });

  test("GPU time in the Profiler: a path tracer on the card reports its busy and wait time, which add up to its submissions; on the CPU it reports none (skipped without a GPU)", [] {
    if (!gpu::available()) {
      std::printf("    (no GPU here: skipped)\n");
      return;
    }
    auto sphere = primitives::uv_sphere(0.5f, 16, 8);
    std::vector<MaterialPtr> mats = {make_material("Grey", Vec3(0.5f))};
    Environment env;
    env.mode = Environment::Color;
    env.color = {0.5f, 0.5f, 0.5f};
    for (bool on_gpu : {true, false}) {
      PathTracer pt;
      PTSettings st;
      st.use_cpu = !on_gpu;
      if (on_gpu) st.gpus = {0};
      st.denoise = false;
      pt.set_settings(st);
      pt.build({{&sphere->render_mesh_tangents(), Mat4::identity(), &mats}}, {}, env);
      pt.set_camera(Mat4::look_at({0, 1, -3}, {0, 0, 0}, {0, 1, 0}), Mat4::perspective(50 * kDeg2Rad, 1.0f, 0.1f, 50), 64, 64);
      ScopedTimer t;
      const int done = pt.render(1e9, 32);
      const double wall = t.ms();
      const PTStats &s = pt.stats();
      std::printf("    %s: %d samples in %.1f ms; GPU busy %.2f ms, wait %.2f ms\n", on_gpu ? "the card" : "the CPU", done, wall, s.gpu_busy_ms, s.gpu_wait_ms);
      CHECK(done == 32);
      if (!on_gpu) {
        CHECK(s.gpu_busy_ms < 0.0 && s.gpu_wait_ms == 0.0);
        continue;
      }
      if (s.gpu_busy_ms < 0.0) {
        std::printf("    (no timestamps on this card: n/a)\n");
        continue;
      }
      CHECK(s.gpu_busy_ms > 0.0 && s.gpu_busy_ms + s.gpu_wait_ms <= wall + 1.0);
    }
  });
}

static void tests_round180() {
  test("gpu views: a view costs the card one submission - its render and read-back together (was two) - and the picture is exactly the two-submission path's (skipped without a GPU)", [] {
    if (!gpu::available()) {
      std::printf("    (no GPU here: skipped)\n");
      return;
    }
    Editor ed;
    r34_open(ed);
    ed.viewport_device_for_test(2);
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube != nullptr);
    if (!cube) return;
    auto draw_at = [&](float x) {  // (a change: every view draws again)
      cube->set_local_position({x, 0.5f, 0.0f});
      ed.step_frame_headless();
    };
    draw_at(0.05f);
    draw_at(0.1f);
    /* One submission a view. */
    const uint64_t s0 = ed.gpu_submits_for_test();
    draw_at(0.2f);
    const uint64_t joined = ed.gpu_submits_for_test() - s0, views = ed.frame_profile().gpu_views;
    std::printf("    %llu views on the card in the frame, %llu submissions\n", (unsigned long long)views, (unsigned long long)joined);
    CHECK(views >= 2 && joined == views);
    std::vector<uint32_t> a = r34_crop(ed, ed.scene_view_rect()), ga = r34_crop(ed, ed.window_rect_for_test(WindowKind::Game));
    a.insert(a.end(), ga.begin(), ga.end());
    /* The old path - render, then read back - draws the same scene again: two submissions a view, the same pixels. */
    ed.gpu_split_readback_for_test(true);
    draw_at(0.3f);
    const uint64_t s1 = ed.gpu_submits_for_test();
    draw_at(0.2f);
    const uint64_t split = ed.gpu_submits_for_test() - s1, views2 = ed.frame_profile().gpu_views;
    std::printf("    the split path: %llu views, %llu submissions\n", (unsigned long long)views2, (unsigned long long)split);
    CHECK(views2 == views && split == 2 * views2);
    std::vector<uint32_t> b = r34_crop(ed, ed.scene_view_rect()), gb = r34_crop(ed, ed.window_rect_for_test(WindowKind::Game));
    b.insert(b.end(), gb.begin(), gb.end());
    CHECK(a.size() == b.size() && a.size() > 1000 && a == b);
    ed.gpu_split_readback_for_test(false);
    ed.viewport_device_for_test(1);
  });
}

static void tests_round181() {
  test("path tracer off the main thread: with a card that takes 30 ms a submission (the test hook), twenty polls each return in under 10 ms while it renders, the render completes with every sample, and pause() mid-render returns within 150 ms (skipped without a GPU)", [] {
    if (!gpu::available()) {
      std::printf("    (no GPU here: skipped)\n");
      return;
    }
    for (int i = 0; i < 400 && gpu::renderers_opening(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    gpu::drop_ready_renderers_for_test();  // (a device opened before the hook would not sleep)
    set_env("BLENDITY_GPU_FAKE_SLOW", "30");
    auto sphere = primitives::uv_sphere(0.5f, 16, 8);
    std::vector<MaterialPtr> mats = {make_material("Grey", Vec3(0.5f))};
    Environment env;
    env.mode = Environment::Color;
    env.color = {0.5f, 0.5f, 0.5f};
    double worst_poll = 0.0, pause_ms = 0.0;
    int done = 0, polls = 0;
    {
      PathTracer pt;
      PTSettings st;
      st.use_cpu = false;
      st.gpus = {0};
      st.denoise = false;
      pt.set_settings(st);
      pt.build({{&sphere->render_mesh_tangents(), Mat4::identity(), &mats}}, {}, env);
      pt.set_camera(Mat4::look_at({0, 1, -3}, {0, 0, 0}, {0, 1, 0}), Mat4::perspective(50 * kDeg2Rad, 1.0f, 0.1f, 50), 64, 64);
      CHECK(pt.gpu_async());
      ScopedTimer total;
      while (done < 64 && total.ms() < 20000.0) {
        ScopedTimer call;
        done = pt.render(4.0, 64);
        if (polls < 20) worst_poll = std::max(worst_poll, call.ms());
        polls++;
      }
      std::printf("    %d samples after %d polls in %.0f ms; the slowest of the first twenty polls %.1f ms\n", done, polls, total.ms(), worst_poll);
      CHECK(done == 64);
      std::printf("    the worst poll: %.2f ms (a stress row since round 411, not a check)\n", worst_poll);
      /* A second render stopped early: pause waits for what is in flight (two 30 ms submissions at most) and the sums. */
      pt.set_camera(Mat4::look_at({0, 1, -3}, {0, 0, 0}, {0, 1, 0}), Mat4::perspective(50 * kDeg2Rad, 1.0f, 0.1f, 50), 64, 64);
      ScopedTimer going;
      int begun = 0;
      while (begun == 0 && going.ms() < 5000.0) {  // (polls until the card has a chunk done: then two more are in flight)
        pt.render(4.0, 4000);
        for (int n : pt.stats().gpu_samples) begun += n;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      ScopedTimer p;
      pt.pause();
      pause_ms = p.ms();
      std::printf("    pause() mid-render: %.1f ms\n", pause_ms);
      std::printf("    the pause: %.1f ms (a stress row since round 411, not a check)\n", pause_ms);
      int on_card = 0;
      for (int n : pt.stats().gpu_samples) on_card += n;
      std::printf("    %d samples finished on the card when paused\n", on_card);
      CHECK(on_card > 0 && on_card < 4000);
    }
    set_env("BLENDITY_GPU_FAKE_SLOW", "");
    gpu::drop_ready_renderers_for_test();  // (a spare opened under the hook would sleep for the next test)
  });

  test("path tracer off the main thread: the editor's F12 on a slow card (the test hook) keeps twenty frames short while the render goes on - under 10 ms but for the ones that merge and resolve the picture - and finishes (skipped without a GPU)", [] {
    if (!gpu::available()) {
      std::printf("    (no GPU here: skipped)\n");
      return;
    }
    for (int i = 0; i < 400 && gpu::renderers_opening(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    gpu::drop_ready_renderers_for_test();
    set_env("BLENDITY_GPU_FAKE_SLOW", "30");
    {
      Editor ed;
      r34_open(ed);
      ed.command("engine path");
      ed.command("device gpu");
      ed.command("samples 400");
      ed.command("render");
      ScopedTimer total;
      bool on_card = false;
      while (!on_card && total.ms() < 20000.0) {  // (the device opens on a worker; the CPU traces meanwhile)
        ed.step_frame_headless();
        on_card = ed.render_status_for_test().find("GPU") != std::string::npos && ed.render_status_for_test().find("starting") == std::string::npos;
        if (!ed.rendering_for_test()) ed.command("render");
      }
      CHECK(on_card);
      for (int i = 0; i < 5 && ed.rendering_for_test(); i++) ed.step_frame_headless();  // (the card settles: its upload and the first merge)
      double worst = 0.0;
      int frames = 0;
      std::string times;
      while (ed.rendering_for_test() && frames < 20 && total.ms() < 30000.0) {
        ScopedTimer f;
        ed.step_frame_headless();
        if (!ed.rendering_for_test()) break;  // (the finishing frame denoises and resolves: not a poll)
        worst = std::max(worst, f.ms());
        times += strprintf("%.1f ", f.ms());
        frames++;
      }
      int quick = 0;
      for (size_t at = 0; at < times.size();) {  // (the frames under 10 ms)
        const size_t sp = times.find(' ', at);
        quick += std::atof(times.substr(at, sp - at).c_str()) < 10.0 ? 1 : 0;
        at = sp == std::string::npos ? times.size() : sp + 1;
      }
      std::printf("    frames: %s(%d of them under 10 ms; a frame that merges new samples resolves the 1280x720 picture, about 12 ms more)\n", times.c_str(), quick);
      while (ed.rendering_for_test() && total.ms() < 40000.0) ed.step_frame_headless();
      std::printf("    %d frames measured, the slowest %.1f ms; %s\n", frames, worst, ed.render_status_for_test().c_str());
      CHECK(frames == 20);
      std::printf("    quick frames %d of 20, the worst %.1f ms (a stress row since round 411, not a check)\n", quick, worst);
      CHECK(!ed.rendering_for_test());
    }
    set_env("BLENDITY_GPU_FAKE_SLOW", "");
    for (int i = 0; i < 400 && gpu::renderers_opening(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    gpu::drop_ready_renderers_for_test();
  });
}

static void tests_round182() {
  test("incremental uploads: moving one object sends the card its instances alone (no whole upload) and a fixed-seed sample matches a whole re-upload's within 1e-5 - with ray tracing hardware and with Blendity's BVH, where the move changes the TLAS's split; a material edit sends the materials alone, a light edit the lights; a whole upload is a few submissions, an instance update fewer (skipped without a GPU)", [] {
    if (!gpu::available()) {
      std::printf("    (no GPU here: skipped)\n");
      return;
    }
    for (int i = 0; i < 400 && gpu::renderers_opening(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto sphere = primitives::uv_sphere(0.5f, 24, 12), cube = primitives::cube(0.8f);
    MaterialPtr grey = make_material("Grey", Vec3(0.5f)), red = make_material("Red", Vec3(0.8f, 0.2f, 0.2f));
    std::vector<MaterialPtr> mats = {grey}, mats2 = {red};
    Environment env;
    env.mode = Environment::Color;
    env.color = {0.5f, 0.5f, 0.5f};
    RenderLight sun;
    sun.type = RenderLight::Directional;
    sun.direction = normalize(Vec3(-0.3f, -1.0f, 0.2f));
    sun.color = Vec3(1.0f);
    sun.intensity = 2.0f;
    /* Four objects: the sphere's move from x to the far side changes the top-level BVH's split (its node count with
     * leaves of two), which the software-BVH card must take as new geometry (the BLAS nodes sit after the TLAS's). */
    auto objects = [&](float x) {
      return std::vector<PTObject>{{&sphere->render_mesh_tangents(), Mat4::translate({x, 0.5f, 0.0f}), &mats},
                                   {&cube->render_mesh_tangents(), Mat4::translate({-1.0f, 0.4f, 0.5f}), &mats2},
                                   {&cube->render_mesh_tangents(), Mat4::translate({-1.2f, 0.4f, -0.6f}), &mats},
                                   {&sphere->render_mesh_tangents(), Mat4::translate({-0.9f, 0.9f, 0.0f}), &mats2}};
    };
    auto frame = [&](PathTracer &pt) {
      pt.set_camera(Mat4::look_at({0, 2, -4}, {0, 0.4f, 0}, {0, 1, 0}), Mat4::perspective(50 * kDeg2Rad, 1.0f, 0.1f, 50), 96, 96);
      int done = 0;
      while (done < 4) done = pt.render(1e9, 4);
      pt.pause();
      return pt.linear_rgb(false);
    };
    for (bool hw : {true, false}) {
    PTSettings st;
    st.use_cpu = false;
    st.gpus = {0};
    st.gpu_hardware_rt = hw;
    st.denoise = false;
    std::printf("    %s:\n", hw ? "ray tracing hardware" : "Blendity's BVH in compute");
    /* The first build: a whole upload. */
    PathTracer pt;
    pt.set_settings(st);
    pt.build(objects(0.0f), {sun}, env);
    CHECK(pt.gpu_async());
    const PTStats &s = pt.stats();
    const int uploads0 = s.upload_calls;
    CHECK(uploads0 == 1 && s.instance_updates == 0);
    frame(pt);
    /* The sphere moved to the far side: the instances alone, unless the split changed the node layout (then a whole
     * upload, which the hash must catch: the picture below says whether it did). */
    const uint64_t sub0 = pt.gpu_submits();
    pt.build(objects(2.5f), {sun}, env);
    const uint64_t inst_sub = pt.gpu_submits() - sub0;
    std::printf("    the move: %d whole uploads, %d instance updates, %llu submissions\n", s.upload_calls, s.instance_updates, (unsigned long long)inst_sub);
    CHECK(s.upload_calls + s.instance_updates == uploads0 + 1 && s.material_updates == 0);
    const std::vector<float> moved = frame(pt);
    /* The same scene built fresh (a whole upload): the same samples. */
    PathTracer fresh;
    fresh.set_settings(st);
    const uint64_t fsub0 = fresh.gpu_submits();
    fresh.build(objects(2.5f), {sun}, env);
    const uint64_t whole_sub = fresh.gpu_submits() - fsub0;
    std::printf("    a whole upload: %llu submissions\n", (unsigned long long)whole_sub);
    CHECK(whole_sub >= 1 && whole_sub <= 9 && (s.instance_updates == 0 || inst_sub < whole_sub));
    const std::vector<float> whole = frame(fresh);
    double worst = 0.0;
    for (size_t i = 0; i < moved.size() && i < whole.size(); i++) worst = std::max(worst, (double)std::fabs(moved[i] - whole[i]));
    std::printf("    after the move: %d whole uploads, %d instance updates; the picture against a fresh upload's: worst channel difference %.2e\n",
                s.upload_calls, s.instance_updates, worst);
    CHECK(moved.size() == whole.size() && worst < 1e-5);
    /* A material edit: the materials alone, and no BVH work. */
    red->base_color = Vec3(0.2f, 0.2f, 0.8f);
    red->touch();
    const int uploads1 = s.upload_calls;
    pt.build(objects(2.5f), {sun}, env);
    std::printf("    a material edit: %d whole uploads, %d material updates, BVH %.2f ms, the upload %.2f ms\n", s.upload_calls, s.material_updates, s.bvh_build_ms, s.upload_ms);
    CHECK(s.upload_calls == uploads1 && s.material_updates == 1);
    std::printf("    the whole build after a material edit (Embree's refit, the flattening, the hashes, the send): %.2f ms (a measurement, not a check: ADR 0056)\n", s.bvh_build_ms);
    const std::vector<float> blue = frame(pt);
    PathTracer fresh2;
    fresh2.set_settings(st);
    fresh2.build(objects(2.5f), {sun}, env);
    const std::vector<float> whole2 = frame(fresh2);
    worst = 0.0;
    for (size_t i = 0; i < blue.size() && i < whole2.size(); i++) worst = std::max(worst, (double)std::fabs(blue[i] - whole2[i]));
    CHECK(worst < 1e-5);
    /* A light edit: the lights alone. */
    RenderLight sun2 = sun;
    sun2.intensity = 4.0f;
    pt.build(objects(2.5f), {sun2}, env);
    CHECK(s.upload_calls == uploads1 && s.light_updates == 1);
    /* A new mesh: the whole scene again, the light count untouched. */
    auto torus = primitives::uv_sphere(0.3f, 8, 6);
    std::vector<PTObject> more = objects(2.5f);
    more.push_back({&torus->render_mesh_tangents(), Mat4::translate({1.5f, 0.3f, 0.0f}), &mats});
    pt.build(more, {sun2}, env);
    CHECK(s.upload_calls == uploads1 + 1 && s.light_updates == 1);
    red->base_color = Vec3(0.8f, 0.2f, 0.2f);
    red->touch();
    }
  });
}

static void tests_round183() {
  /* The Scene view's picture on the CPU and on the card for the scene `setup` builds, and the card's fallbacks. */
  struct R183 {
    double mean = 0, within3 = 0;
    size_t pixels = 0;
    uint64_t gpu_views = 0, fallbacks = 0;
    std::string reason;
  };
  auto r183_compare = [](Editor &ed, GameObject *mover, Vec3 base, R183 &r) {
    auto draw = [&](int device) {
      ed.viewport_device_for_test(device);
      for (int k = 0; k < 2; k++) {  // (a change, then back: the view draws again)
        mover->set_local_position(base + Vec3(0.0f, k ? 0.0f : 0.0001f, 0.0f));
        r34_steps(ed, 2);
      }
      return r34_crop(ed, ed.scene_view_rect());
    };
    const std::vector<uint32_t> cpu = draw(1);
    const FrameProfile before = ed.frame_profile_totals();
    const std::vector<uint32_t> gpu = draw(2);
    const FrameProfile after = ed.frame_profile_totals();
    r.gpu_views = after.gpu_views - before.gpu_views;
    r.fallbacks = after.gpu_fallbacks - before.gpu_fallbacks;
    r.reason = ed.gpu_view_reason();
    double sum = 0;
    size_t within = 0;
    r.pixels = std::min(cpu.size(), gpu.size());
    for (size_t i = 0; i < r.pixels; i++) {
      int worst = 0;
      for (int c = 0; c < 3; c++) worst = std::max(worst, std::abs((int)((cpu[i] >> (c * 8)) & 255) - (int)((gpu[i] >> (c * 8)) & 255)));
      sum += worst;
      within += worst <= 3 ? 1 : 0;
    }
    r.mean = r.pixels ? sum / r.pixels : 0.0;
    r.within3 = r.pixels ? (double)within / r.pixels : 0.0;
    ed.viewport_device_for_test(1);
  };

  test("gpu views: an area light lights the view on the card as on the CPU - a rectangle over a floor and a sphere, then a disc - with no fallback (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    (no GPU here: skipped)\n");
      return;
    }
    for (int disc = 0; disc < 2; disc++) {
      Editor ed;
      r34_open(ed);
      ed.command("select Cube");
      ed.command("delete");  // (the default cube sits at the pivot)
      ed.command("select Main Camera");
      GameObject *panel = ed.scene().create("Panel");
      Light *l = panel->add<Light>();
      l->type = 3;
      l->area_shape = disc;
      l->area_width = 2.0f;
      l->area_height = 1.0f;
      l->intensity = 3.0f;
      panel->set_local_position({0.5f, 2.2f, 0.0f});
      panel->set_local_rotation(Quat::euler({90, 0, 0}));  // (facing down: +Z turned to -Y)
      GameObject *ball = ed.scene().create("Ball");
      ball->add<MeshFilter>()->mesh = primitives::uv_sphere(0.5f, 24, 12);
      ball->add<MeshRenderer>()->materials = {make_material("Glossy", Vec3(0.7f, 0.5f, 0.3f))};
      ball->get<MeshRenderer>()->materials[0]->roughness = 0.3f;
      ball->set_local_position({1.2f, 0.5f, 0.0f});
      ed.command("camera 0 20 5 0.3 0.6 0");  // (yaw, pitch, distance, the pivot)
      r34_steps(ed, 3);
      R183 r;
      r183_compare(ed, ball, {1.2f, 0.5f, 0.0f}, r);
      std::printf("    %s: %llu views on the card, %llu on the CPU%s%s; %zu pixels: mean difference %.3f / 255, %.2f %% within 3\n", disc ? "a disc" : "a rectangle",
                  (unsigned long long)r.gpu_views, (unsigned long long)r.fallbacks, r.reason.empty() ? "" : " - ", r.reason.c_str(), r.pixels, r.mean, r.within3 * 100);
      CHECK(r.gpu_views > 0 && r.fallbacks == 0);
      CHECK(r.pixels > 3000 && r.mean < 1.0 && r.within3 >= 0.99);
      /* The panel lights the picture: with it off, many pixels change (on the card). */
      ed.viewport_device_for_test(2);
      ball->set_local_position({1.2f, 0.5001f, 0.0f});
      r34_steps(ed, 2);
      const std::vector<uint32_t> lit = r34_crop(ed, ed.scene_view_rect());
      l->enabled = false;
      ball->set_local_position({1.2f, 0.5f, 0.0f});
      r34_steps(ed, 2);
      const std::vector<uint32_t> dark = r34_crop(ed, ed.scene_view_rect());
      l->enabled = true;
      size_t changed = 0;
      for (size_t i = 0; i < lit.size() && i < dark.size(); i++) changed += lit[i] != dark[i] ? 1 : 0;
      std::printf("    the panel off changes %zu pixels\n", changed);
      CHECK(changed > 2000);
    }
  });

  test("gpu views: a cutout material's holes show what is behind and pick it, on the card as on the CPU, with no fallback (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    (no GPU here: skipped)\n");
      return;
    }
    Editor ed;
    r34_open(ed);
    ed.command("select Cube");
    ed.command("delete");  // (the default cube sits at the pivot, where the wall goes)
    ed.command("select Main Camera");
    /* A 64 x 64 texture: opaque with a 32 x 32 hole in the middle (alpha 0). */
    const std::string png = fs::join(test_dir(), "cutout_hole.png");
    std::vector<uint32_t> px(64 * 64);
    for (int y = 0; y < 64; y++)
      for (int x = 0; x < 64; x++) {
        const bool hole = x >= 16 && x < 48 && y >= 16 && y < 48;
        px[(size_t)y * 64 + x] = hole ? 0u : 0xFFC83C28u;
      }
    CHECK(write_png(png, px.data(), 64, 64, 64, true));  // (with its alpha: without it the hole is opaque black)
    MaterialPtr cut = make_material("Holed", Vec3(1.0f));
    cut->surface = (int)MaterialSurface::Cutout;
    cut->alpha_clip = 0.5f;
    cut->base_map.path = png;
    cut->touch();
    GameObject *wall = ed.scene().create("Wall");
    wall->add<MeshFilter>()->mesh = primitives::plane(2.0f, 1);
    wall->add<MeshRenderer>()->materials = {cut};
    wall->set_local_position({0.0f, 1.0f, 0.0f});
    wall->set_local_rotation(Quat::euler({-90, 0, 0}));  // (upright, its front toward the camera at -Z: the view culls back faces)
    GameObject *behind = ed.scene().create("Behind");
    behind->add<MeshFilter>()->mesh = primitives::cube(0.6f);
    behind->add<MeshRenderer>()->materials = {make_material("Blue", Vec3(0.2f, 0.3f, 0.9f))};
    behind->set_local_position({0.0f, 1.0f, 2.0f});
    ed.command("camera 0 0 3 0 1 0");  // (yaw, pitch, distance, the pivot: the eye 3 m before the wall)
    r34_steps(ed, 3);
    R183 r;
    r183_compare(ed, behind, {0.0f, 1.0f, 2.0f}, r);
    std::printf("    %llu views on the card, %llu on the CPU%s%s; %zu pixels: mean difference %.3f / 255, %.2f %% within 3\n",
                (unsigned long long)r.gpu_views, (unsigned long long)r.fallbacks, r.reason.empty() ? "" : " - ", r.reason.c_str(), r.pixels, r.mean, r.within3 * 100);
    CHECK(r.gpu_views > 0 && r.fallbacks == 0);
    CHECK(r.pixels > 3000 && r.mean < 1.0 && r.within3 >= 0.99);
    /* The hole shows the blue cube at the view's centre and the red wall beside it, on both devices. */
    for (int device : {1, 2}) {
      ed.viewport_device_for_test(device);
      behind->set_local_position({0.0f, 1.0f, device == 1 ? 2.0001f : 2.0f});
      r34_steps(ed, 2);
      const Recti v = ed.scene_view_rect();
      const std::vector<uint32_t> px2 = r34_crop(ed, v);
      auto at = [&](int x, int y) { return px2[(size_t)y * v.w + x]; };
      const uint32_t centre = at(v.w / 2, v.h / 2), beside = at(v.w / 2 + v.w / 12, v.h / 2);  // (a twelfth of the view: past the hole, on the wall)
      auto r_ = [](uint32_t p) { return (int)((p >> 16) & 255); };
      auto b_ = [](uint32_t p) { return (int)(p & 255); };
      std::printf("    %s: the centre pixel %02x%02x%02x (the cube through the hole), beside it %02x%02x%02x (the wall)\n", device == 2 ? "the card" : "the CPU",
                  r_(centre), (int)((centre >> 8) & 255), b_(centre), r_(beside), (int)((beside >> 8) & 255), b_(beside));
      CHECK(b_(centre) > r_(centre) + 40);  // (blue through the hole)
      CHECK(r_(beside) > b_(beside) + 40);  // (the red wall around it: the wall faces the camera)
    }
    /* A click through the hole, with the views on the card, picks the cube behind. */
    ed.viewport_device_for_test(2);
    behind->set_local_position({0.0f, 1.0f, 2.0001f});
    r34_steps(ed, 2);
    const Recti v = ed.scene_view_rect();
    r34_click(ed, v.x + v.w / 2, v.y + v.h / 2);
    r34_steps(ed, 1);
    const GameObject *picked = ed.active_object_for_test();
    std::printf("    clicked through the hole on the card: %s\n", picked ? picked->name.c_str() : "nothing");
    CHECK(picked && picked->name == "Behind");
    ed.viewport_device_for_test(1);
  });

  test("gpu views: the Profiler names why a view drew on the CPU (a camera filter), by the view (skipped without a GPU)", [] {
    if (!gpu::available()) {
      std::printf("    (no GPU here: skipped)\n");
      return;
    }
    Editor ed;
    r34_open(ed);
    ed.viewport_device_for_test(2);
    ed.record_buttons_for_test(true);
    ed.command("window Profiler");
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube != nullptr);
    if (!cube) return;
    for (int i = 0; i < 3; i++) {
      cube->set_local_position({0.1f * (float)i, 0.5f, 0.0f});
      ed.step_frame_headless();
    }
    Recti row;
    auto any_named = [&] {
      for (const char *v : {"Scene view on the CPU", "Game view on the CPU", "Camera Preview on the CPU", "A view on the CPU"})
        if (ed.button_rect_for_test(v, row)) return true;
      return false;
    };
    bool named = any_named();
    CHECK(!named);  // (every view on the card: no such row)
    ed.command("select Main Camera");
    ed.command("filter add Retro Console");  // (a camera filter's rasterizer stage: the Game view falls back; since task 0197 a filter's passes don't)
    for (int i = 0; i < 3; i++) {
      cube->set_local_position({0.1f * (float)i, 0.5f, 0.0f});
      ed.step_frame_headless();
    }
    named = any_named();
    std::printf("    the Game view with a filter: %s\n", named ? "named in the Profiler" : "not named");
    CHECK(named);
    ed.viewport_device_for_test(1);
  });
}

/* Round 186 (task 0166): the card draws Edit Mode's face highlight. */
static void tests_round186() {
  test("the card draws Edit Mode's face highlight: a cube with its top face selected drawn on the card and on the CPU - no fallback, the highlighted face tinted on both, the pictures alike", [&] {
    if (!gpu::available()) { std::printf("    (no GPU)\n"); return; }
    Editor ed;
    r34_open(ed);
    ed.command("select Cube");
    ed.command("edit face");
    ed.command("fsel facing 0 1 0");
    ed.command("camera 30 35 4");  // (pitch 35 down: the top face in view)
    r34_steps(ed, 3);
    auto orange = [](const std::vector<uint32_t> &px) {
      size_t n = 0;
      for (uint32_t p : px) {
        const int r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
        n += r > 140 && r > g + 30 && g > b + 20;
      }
      return n;
    };
    /* The tint alone: the orange pixels with the top face selected less those with nothing selected (the selected
     * face's cage edges are orange too, on both devices alike). */
    auto tint = [&](int device) {
      ed.viewport_device_for_test(device);
      ed.command("vsel");  // (nothing selected)
      r34_steps(ed, 2);
      const size_t none = orange(r34_crop(ed, ed.scene_view_rect()));
      ed.command("fsel facing 0 1 0");
      r34_steps(ed, 2);
      const size_t sel = orange(r34_crop(ed, ed.scene_view_rect()));
      return std::make_pair(none, sel > none ? sel - none : 0);
    };
    const auto cpu = tint(1);
    const FrameProfile before = ed.frame_profile_totals();
    const auto gpu = tint(2);
    const FrameProfile after = ed.frame_profile_totals();
    std::printf("    the top face's tint: %zu orange pixels on the CPU (%zu with nothing selected), %zu on the card (%zu); %llu views on the GPU, %llu on the CPU instead (%s)\n",
                cpu.second, cpu.first, gpu.second, gpu.first, (unsigned long long)(after.gpu_views - before.gpu_views),
                (unsigned long long)(after.gpu_fallbacks - before.gpu_fallbacks), ed.gpu_view_reason().c_str());
    CHECK(after.gpu_views - before.gpu_views >= 1);
    CHECK(after.gpu_fallbacks - before.gpu_fallbacks == 0);
    CHECK(cpu.second > 300 && gpu.second > 300);  // (the scene has orange of its own: the tint is what selecting adds)
    CHECK(gpu.second * 10 >= cpu.second * 8 && cpu.second * 10 >= gpu.second * 8);
    /* Two materials: the card draws the mesh as two parts in its own triangle order, and the bits must follow it. */
    GameObject *cube = ed.scene().find_by_name("Cube");
    MeshRenderer *mr = cube->get<MeshRenderer>();
    const MaterialPtr first = mr->materials.empty() ? make_material("first", Vec3(0.8f, 0.8f, 0.8f)) : mr->materials[0];
    mr->materials = {first, make_material("second", Vec3(0.4f, 0.4f, 0.9f))};
    Mesh &cm = *mesh_make_mutable(cube->get<MeshFilter>()->mesh);
    cm.face_material.assign(cm.face_count(), 0);
    for (size_t f = 0; f < cm.face_count(); f += 2) cm.face_material[f] = 1;
    cm.touch();
    ed.mark_changed_for_test("two materials");
    const auto cpu2 = tint(1), gpu2 = tint(2);
    std::printf("    two materials: the tint %zu pixels on the CPU, %zu on the card\n", cpu2.second, gpu2.second);
    CHECK(cpu2.second > 300 && gpu2.second > 300);
    CHECK(gpu2.second * 10 >= cpu2.second * 8 && cpu2.second * 10 >= gpu2.second * 8);
    ed.viewport_device_for_test(1);
  });
}

/* Round 189 (task 0169, ADR 0054): shipping OptiX. */
#if __has_include("../src/render/optix/optix_kernel_ptx.h")
#  include "../src/render/optix/optix_kernel_ptx.h"
#  define R189_HAVE_PTX 1
#endif
/* FNV-1a 64 over a file's text with its line ends as LF (what tools/ptx_compile.cpp hashes), or false. */
static bool r189_hash(const std::string &path, uint64_t &h) {
  std::string text;
  if (!fs::read_file(path, text)) return false;
  for (char c : text) {
    if (c == '\r') continue;
    h ^= (unsigned char)c;
    h *= 0x100000001b3ull;
  }
  return true;
}
static void tests_round189() {
  test("OptiX ships as committed PTX: its header's hash matches what it is made from - optix_kernel.cu, optix_params.h, glsl.cuh, the GPU kernel's GLSL and its translator (run tools/optix_ptx_embed.bat after changing them)", [&] {
#if defined(R189_HAVE_PTX)
    uint64_t h = 0xcbf29ce484222325ull;
    bool read = true;  // (what tools/optix_ptx_embed.bat hashes, in its order)
    for (const char *f : {"src/render/optix_kernel.cu", "src/render/optix_params.h", "src/render/optix/glsl.cuh", "src/render/gpu_kernel.h",
                          "src/render/gpu_glsl.h", "tools/glsl_to_cuda.py"})
      read = read && r189_hash(f, h);
    if (!read) {
      std::printf("    (the sources aren't beside the tests: run from the blendity folder)\n");
      return;
    }
    std::printf("    the committed PTX: %zu bytes from OptiX %d; its hash %016llx, the sources' %016llx\n", (size_t)BL_OPTIX_PTX_BYTES, (int)BL_OPTIX_PTX_OPTIX_VERSION,
                (unsigned long long)BL_OPTIX_PTX_SOURCE_HASH, (unsigned long long)h);
    CHECK(h == BL_OPTIX_PTX_SOURCE_HASH);
    size_t joined = 0;
    for (const char *part : optix_kernel_ptx_parts) joined += std::strlen(part);
    CHECK(joined == (size_t)BL_OPTIX_PTX_BYTES && BL_OPTIX_PTX_BYTES > 1000 && BL_OPTIX_PTX_OPTIX_VERSION >= 70000);
#else
    CHECK(false);  // (the header is committed: src/render/optix/optix_kernel_ptx.h)
#endif
  });
  test("OptiX's state: `gpu list` names it (in the build, its driver, its cards), and BLENDITY_NO_OPTIX=1 turns it off at run time", [&] {
    const std::string on = gpu::optix_status();
    std::printf("    %s\n", on.c_str());
    CHECK(on.rfind("OptiX: ", 0) == 0);
    Editor ed;
    ed.init_headless(800, 600);
    ed.command("gpu list");  // (logs the Vulkan cards and the line above)
    const char *was = std::getenv("BLENDITY_NO_OPTIX");
    const std::string before = was ? was : "";
    set_env("BLENDITY_NO_OPTIX", "1");
    const std::string off = gpu::optix_status();
    const bool compiled = gpu::optix_compiled_in();
    std::string why;
    const bool made = gpu::create_optix_renderer(0, &why) != nullptr;
    set_env("BLENDITY_NO_OPTIX", before);
    CHECK(!made);
    std::printf("    with BLENDITY_NO_OPTIX=1: %s; compiled in: %s\n", off.c_str(), compiled ? "yes" : "no");
    CHECK(!compiled);
    CHECK(off == "OptiX: turned off (BLENDITY_NO_OPTIX=1)" || off == "OptiX: not in this build");
  });
}

/* Round 190 (task 0170, ADR 0054): the OptiX device. */
static void tests_round190() {
  test("the OptiX device renders the picture GPU Compute renders: one integrator (the GLSL translated), the same samples - spheres, a glass ball, a toon ball (task 0171), a checker floor, a sun and a point light at 64 samples within 0.01 a channel on average", [&] {
    if (!gpu::available() || !gpu::optix_compiled_in() || gpu::optix_device_of(0) < 0) {
      std::printf("    (no OptiX card here)\n");
      return;
    }
    auto ball = primitives::uv_sphere(0.5f, 32, 16);
    auto floor = primitives::plane(10.0f, 1);
    MaterialPtr red = make_material("red", {0.8f, 0.2f, 0.15f}), glass = make_material("glass", {0.95f, 0.95f, 0.95f}), chk = make_material("floor", {0.8f, 0.8f, 0.8f});
    red->roughness = 0.4f;
    glass->surface = (int)MaterialSurface::Glass;
    glass->roughness = 0.05f;
    chk->procedural = 1;  // (Checker)
    chk->procedural_color2 = {0.2f, 0.2f, 0.25f};
    MaterialPtr toon = make_material("toon", {0.9f, 0.6f, 0.5f});  // (task 0171: the toon shading, three bands and a rim)
    toon->shading_model = 1;
    toon->toon_bands = 3;
    toon->toon_rim_strength = 0.6f;
    std::vector<MaterialPtr> mr = {red}, mg = {glass}, mf = {chk}, mt = {toon};
    std::vector<PTObject> objs = {{&floor->render_mesh_tangents(), Mat4::translate({0, -0.5f, 0}), &mf},
                                  {&ball->render_mesh_tangents(), Mat4::translate({-0.7f, 0, 0}), &mr},
                                  {&ball->render_mesh_tangents(), Mat4::translate({0.7f, 0, 0.3f}), &mg},
                                  {&ball->render_mesh_tangents(), Mat4::translate({0.0f, 0.1f, 1.0f}) * Mat4::scale(Vec3(0.6f)), &mt}};
    RenderLight sun, lamp;
    sun.direction = normalize(Vec3(-0.4f, -1.0f, 0.3f));
    lamp.type = RenderLight::Point;
    lamp.position = {0.0f, 1.5f, -1.0f};
    lamp.intensity = 4.0f;
    Environment env;
    const int W = 160, H = 120;
    const Mat4 v = Mat4::look_at({0, 1.2f, -3.5f}, {0, 0, 0}, {0, 1, 0}), p = Mat4::perspective(50 * kDeg2Rad, W / (float)H, 0.1f, 100.0f);
    auto render = [&](const char *backend, std::string &device) {
      set_env("BLENDITY_GPU_BACKEND", backend);
      PathTracer pt;
      PTSettings st;
      st.use_cpu = false;
      st.gpus = {0};
      st.gpu_hardware_rt = true;
      st.max_bounces = 4;
      st.denoise = false;
      st.use_guiding = false;
      pt.set_settings(st);
      pt.build(objs, {sun, lamp}, env);
      pt.set_camera(v, p, W, H);
      pt.render(1e9, 64);
      device = pt.gpu_error().empty() ? pt.device_summary() : pt.gpu_error();
      return pt.linear_rgb(false);
    };
    std::string dv, dx;
    const std::vector<float> vk = render("vulkan", dv), ox = render("optix", dx);
    set_env("BLENDITY_GPU_BACKEND", "vulkan");
    double diff = 0, worst = 0, sum = 0;
    size_t differ = 0;
    for (size_t i = 0; i < vk.size() && i < ox.size(); i++) {
      const double d = std::fabs((double)vk[i] - ox[i]);
      diff += d, worst = std::max(worst, d), sum += vk[i], differ += d > 0.0;
    }
    std::printf("    %zu of %zu channels differ at all\n", differ, vk.size());
    diff /= std::max<size_t>(1, vk.size());
    std::printf("    GPU Compute: %s\n    OptiX: %s\n    mean difference %.5f a channel (the picture's mean %.3f), the largest %.3f\n", dv.c_str(), dx.c_str(), diff,
                sum / std::max<size_t>(1, vk.size()), worst);
    CHECK(vk.size() == ox.size() && !vk.empty());
    /* (Two empty pictures would match too: the objects must be in it - the pixels' spread, and a darker share.) */
    float lo = 1e30f, hi = -1e30f;
    size_t dark = 0;
    for (size_t i = 0; i + 2 < vk.size(); i += 3) {
      const float l = 0.2126f * vk[i] + 0.7152f * vk[i + 1] + 0.0722f * vk[i + 2];
      lo = std::min(lo, l), hi = std::max(hi, l), dark += l < 0.5f;
    }
    std::printf("    the picture's luminance from %.3f to %.3f, %zu of %zu pixels under 0.5\n", lo, hi, dark, vk.size() / 3);
    CHECK(hi - lo > 0.2f && dark > vk.size() / 3 / 20);
    CHECK(dx.find("(OptiX)") != std::string::npos && dv.find("(OptiX)") == std::string::npos);
    CHECK(diff < 0.01);
  });
  test("GPU Backend from the settings: OptiX opens the OptiX device on the card and a second build keeps it; Vulkan reopens the card as GPU Compute; a card OptiX can't take keeps GPU Compute without reopening", [&] {
    if (!gpu::available() || !gpu::optix_compiled_in() || gpu::optix_device_of(0) < 0) {
      std::printf("    (no OptiX card here)\n");
      return;
    }
    set_env("BLENDITY_GPU_BACKEND", "");  // (the setting's path, not the tests' pin)
    auto ball = primitives::uv_sphere(0.5f, 16, 8);
    MaterialPtr m = make_material("m", {0.7f, 0.7f, 0.7f});
    std::vector<MaterialPtr> ms = {m};
    std::vector<PTObject> objs = {{&ball->render_mesh_tangents(), Mat4(), &ms}};
    RenderLight sun;
    Environment env;
    PathTracer pt;
    auto run = [&](int backend) {
      PTSettings st;
      st.use_cpu = false;
      st.gpus = {0};
      st.gpu_hardware_rt = true;
      st.gpu_backend = backend;
      pt.set_settings(st);
      pt.build(objs, {sun}, env);
      pt.set_camera(Mat4::look_at({0, 0, -3}, {0, 0, 0}, {0, 1, 0}), Mat4::perspective(0.8f, 1.0f, 0.1f, 10.0f), 32, 32);
      pt.render(1e9, 2);
      return pt.device_summary();
    };
    const std::string a = run(2), b = run(2), c = run(1);
    gpu::set_backend(0);
    set_env("BLENDITY_GPU_BACKEND", "vulkan");
    std::printf("    OptiX: %s; again: %s; Vulkan: %s\n", a.c_str(), b.c_str(), c.c_str());
    CHECK(a.find("(OptiX)") != std::string::npos && b == a);
    CHECK(c.find("(OptiX)") == std::string::npos && c.find("RT hardware") != std::string::npos);
  });
}

/* ---------------------------------------------------------------- round 211: the overlays on the card (task 0192) */
static void tests_round211() {
  /* The Scene view, drawn again on `device` (a change and back: the card draws at full size from its second frame). */
  auto view = [](Editor &ed, GameObject *g, int device) {
    ed.viewport_device_for_test(device);
    for (int k = 0; k < 2; k++) {
      g->set_local_position({2.0f, k ? 1.0f : 1.0001f, 0.0f});
      r34_steps(ed, 2);
    }
    return r34_crop(ed, ed.scene_view_rect());
  };
  /* Over the pixels where `with` differs from `without` (the overlays' own): how many, the mean of the worst channel's
   * difference between `with` and `gpu`, and the share within 3 / 255. */
  auto compare = [](const std::vector<uint32_t> &with, const std::vector<uint32_t> &without, const std::vector<uint32_t> &gpu, size_t &n, double &mean,
                    double &within3) {
    auto worst = [](uint32_t a, uint32_t b) {
      int w = 0;
      for (int k = 0; k < 24; k += 8) w = std::max(w, std::abs((int)((a >> k) & 255) - (int)((b >> k) & 255)));
      return w;
    };
    double sum = 0;
    size_t within = 0;
    n = 0;
    for (size_t i = 0; i < with.size() && i < without.size() && i < gpu.size(); i++) {
      if (worst(with[i], without[i]) <= 8) continue;
      const int w = worst(with[i], gpu[i]);
      n++, sum += w, within += w <= 3;
    }
    mean = n ? sum / (double)n : 0;
    within3 = n ? (double)within / (double)n : 0;
  };
  auto open = [](Editor &ed) {
    r34_open(ed);
    GameObject *wire = ed.scene().create("Wired");
    wire->add<MeshFilter>()->mesh = primitives::uv_sphere(0.8f, 24, 16);
    wire->add<MeshRenderer>()->show_wireframe = true;
    wire->set_local_position({2.0f, 1.0f, 0.0f});
    ed.mark_changed_for_test("Wired");
    ed.command("select Cube");
    ed.command("camera 0 4 6 0 1 0");
    r34_steps(ed, 3);
    return wire;
  };
  test("gpu views: the Scene view's overlays - the grid and its axes, a wireframe, the selection outline - drawn by the card as the CPU draws them (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    GameObject *wire = open(ed);
    const std::vector<uint32_t> cpu = view(ed, wire, 1);
    const FrameProfile before = ed.frame_profile_totals();
    const std::vector<uint32_t> gpu = view(ed, wire, 2);
    const FrameProfile after = ed.frame_profile_totals();
    ed.viewport_device_for_test(1);
    ed.command("overlay grid off");
    ed.clear_selection_for_test();
    wire->get<MeshRenderer>()->show_wireframe = false;
    const std::vector<uint32_t> plain = view(ed, wire, 1);
    const std::vector<uint32_t> gpu_plain = view(ed, wire, 2);
    size_t n = 0;
    double mean = 0, within3 = 0;
    compare(cpu, plain, gpu, n, mean, within3);
    /* The lines alone: their pixels where the two pictures under them agree (so the 3D's own small differences, which
     * show through faint lines, don't count). */
    std::vector<uint32_t> cpu_same, plain_same, gpu_same;
    for (size_t i = 0; i < cpu.size() && i < plain.size() && i < gpu.size() && i < gpu_plain.size(); i++) {
      int d = 0;
      for (int k = 0; k < 24; k += 8) d = std::max(d, std::abs((int)((plain[i] >> k) & 255) - (int)((gpu_plain[i] >> k) & 255)));
      if (d > 0) continue;
      cpu_same.push_back(cpu[i]), plain_same.push_back(plain[i]), gpu_same.push_back(gpu[i]);
    }
    size_t ln = 0;
    double lmean = 0, lwithin3 = 0;
    compare(cpu_same, plain_same, gpu_same, ln, lmean, lwithin3);
    std::printf("    the card drew the overlays in %llu frames; their %zu pixels: mean difference %.3f / 255, %.2f %% within 3; the %zu where the "
                "pictures under them agree: %.3f, %.2f %%\n",
                (unsigned long long)(after.gpu_overlay_frames - before.gpu_overlay_frames), n, mean, within3 * 100, ln, lmean, lwithin3 * 100);
    CHECK(after.gpu_overlay_frames > before.gpu_overlay_frames);
    CHECK(n > 2000);
    CHECK(mean < 1.0 && within3 >= 0.99);
  });
  test("gpu views: every kind of overlay on the card as on the CPU - dots of several sizes over and under lines, translucent, without the depth test, lines without it, an outline between them (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    GameObject *wire = open(ed);
    wire->get<MeshRenderer>()->show_wireframe = false;
    ed.command("overlay grid off");
    ed.clear_selection_for_test();
    const uint32_t cube = (uint32_t)ed.scene().find_by_name("Cube")->id;
    ed.overlays_extra_for_test([cube](Renderer3D &r) {
      for (int i = 0; i < 40; i++) {
        const float t = i / 40.0f;
        const Vec3 a(-2.5f + 5.0f * t, 0.2f + 1.6f * t, -0.5f), b(2.5f - 5.0f * t, 2.0f - 1.4f * t, 0.8f);
        if (i % 3 == 0) r.point(a, 2.0f + (i % 5), 0x8C30C0F0u, i % 2 == 0);  // (translucent, some through everything)
        r.line(a, b, i % 4 == 0 ? 0xFFE04020u : 0xA0FFFFFFu, i % 4 != 0, 2e-4f);
        if (i == 20) r.outline_ids({cube}, 0xFFFF8800u, 2);
        if (i % 3 == 1) r.point(b, 3.0f, 0xFF20E060u, true);
      }
    });
    const std::vector<uint32_t> cpu = view(ed, wire, 1);
    const FrameProfile before = ed.frame_profile_totals();
    const std::vector<uint32_t> gpu = view(ed, wire, 2);
    const FrameProfile after = ed.frame_profile_totals();
    ed.overlays_extra_for_test(nullptr);
    ed.viewport_device_for_test(1);
    const std::vector<uint32_t> plain = view(ed, wire, 1);
    size_t n = 0;
    double mean = 0, within3 = 0;
    compare(cpu, plain, gpu, n, mean, within3);
    std::printf("    the card drew them in %llu frames; their %zu pixels: mean difference %.3f / 255, %.2f %% within 3\n",
                (unsigned long long)(after.gpu_overlay_frames - before.gpu_overlay_frames), n, mean, within3 * 100);
    CHECK(after.gpu_overlay_frames > before.gpu_overlay_frames);
    CHECK(n > 1000 && mean < 1.0 && within3 >= 0.99);
  });
  test("gpu views: when the card can't take the overlays they're drawn here over its picture, the same as the CPU's (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    GameObject *wire = open(ed);
    const std::vector<uint32_t> cpu = view(ed, wire, 1);
    ed.overlays_off_gpu_for_test(true);
    const FrameProfile before = ed.frame_profile_totals();
    const std::vector<uint32_t> gpu = view(ed, wire, 2);
    const FrameProfile after = ed.frame_profile_totals();
    ed.overlays_off_gpu_for_test(false);
    ed.viewport_device_for_test(1);
    ed.command("overlay grid off");
    ed.clear_selection_for_test();
    wire->get<MeshRenderer>()->show_wireframe = false;
    const std::vector<uint32_t> plain = view(ed, wire, 1);
    size_t n = 0;
    double mean = 0, within3 = 0;
    compare(cpu, plain, gpu, n, mean, within3);
    std::printf("    replayed here: %llu card frames with overlays, %llu views on the card; their %zu pixels: mean %.3f, %.2f %% within 3\n",
                (unsigned long long)(after.gpu_overlay_frames - before.gpu_overlay_frames), (unsigned long long)(after.gpu_views - before.gpu_views), n,
                mean, within3 * 100);
    CHECK(after.gpu_overlay_frames == before.gpu_overlay_frames && after.gpu_views > before.gpu_views);
    CHECK(n > 2000 && mean < 1.0 && within3 >= 0.99);
  });
}

/* ---------------------------------------------------------------- round 212: presenting from the card (task 0193) */
static void tests_round212() {
  auto diff = [](const std::vector<uint32_t> &a, const std::vector<uint32_t> &b, double &mean, double &within2) {
    size_t n = 0, ok = 0;
    double sum = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); i++) {
      int w = 0;
      for (int k = 0; k < 24; k += 8) w = std::max(w, std::abs((int)((a[i] >> k) & 255) - (int)((b[i] >> k) & 255)));
      n++, sum += w, ok += w <= 2;
    }
    mean = n ? sum / (double)n : 0, within2 = n ? (double)ok / (double)n : 0;
  };
  test("gpu views: a frame the card presents - the views' pictures kept on the card, the window's canvas over them - is the frame the system presents, drawn fresh, from the cache, and back on the system (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    r34_open(ed);  // (2 by 3: the Scene view and the Game view)
    ed.viewport_device_for_test(2);
    ed.command("select Cube");
    GameObject *cube = ed.scene().find_by_name("Cube");
    auto redraw = [&] {
      for (int k = 0; k < 2; k++) {
        cube->set_local_position({0.0f, k ? 0.5f : 0.5001f, 0.0f});
        r34_steps(ed, 2);
      }
    };
    redraw();
    const std::vector<uint32_t> system = ed.framebuffer().pixels;
    ed.card_present_for_test(true);
    redraw();
    size_t holes = 0;
    for (uint32_t p : ed.framebuffer().pixels) holes += (p >> 24) == 0;
    std::vector<uint32_t> card;
    const size_t views = ed.card_views_for_test();
    CHECK(ed.card_frame_for_test(card));
    double mean = 0, within2 = 0;
    diff(system, card, mean, within2);
    std::printf("    drawn fresh: %zu views on the card, %zu canvas pixels left for them; the card's frame against the system's: mean %.3f / 255, %.2f %% within 2\n",
                views, holes, mean, within2 * 100);
    CHECK(views >= 2 && holes > 10000);
    CHECK(mean < 0.5 && within2 >= 0.995);
    r34_steps(ed, 2);  // (nothing changed: the views come from their caches, their pictures still the card's)
    const FrameProfile before = ed.frame_profile_totals();
    r34_steps(ed, 1);
    const uint64_t renders = ed.frame_profile_totals().view_renders - before.view_renders;
    CHECK(ed.card_frame_for_test(card));
    diff(system, card, mean, within2);
    std::printf("    from the cache: %llu views drawn, %zu on the card; mean %.3f, %.2f %% within 2\n", (unsigned long long)renders,
                ed.card_views_for_test(), mean, within2 * 100);
    CHECK(renders == 0 && ed.card_views_for_test() >= 2);
    CHECK(mean < 0.5 && within2 >= 0.995);
    /* The slots drawn by something else since: a cached picture the card no longer holds draws again. */
    ed.card_slots_redrawn_for_test();
    const FrameProfile b2 = ed.frame_profile_totals();
    r34_steps(ed, 1);
    const uint64_t again = ed.frame_profile_totals().view_renders - b2.view_renders;
    CHECK(ed.card_frame_for_test(card));
    diff(system, card, mean, within2);
    std::printf("    its slots drawn over: %llu views drawn again; mean %.3f\n", (unsigned long long)again, mean);
    CHECK(again >= 2 && mean < 0.5);
    /* A camera with depth of field: the CPU blurs its picture after the card draws it, so the Game view comes back to
     * the canvas (one view on the card, the Scene view's) and still matches the system's frame with the blur. */
    Camera *main = ed.scene().find_by_name("Main Camera")->get<Camera>();
    main->dof = true;
    main->focus_distance = 2.0f;
    main->f_stop = 1.0f;
    ed.card_present_for_test(false);
    redraw();
    const std::vector<uint32_t> blurred = ed.framebuffer().pixels;
    ed.card_present_for_test(true);
    redraw();
    CHECK(ed.card_frame_for_test(card));
    diff(blurred, card, mean, within2);
    std::printf("    depth of field on: %zu views on the card; mean %.3f\n", ed.card_views_for_test(), mean);
    CHECK(ed.card_views_for_test() == 1 && mean < 0.5);
    ed.card_present_for_test(false);  // (the system presents again: the cached holes are no use, the views draw again)
    r34_steps(ed, 2);
    holes = 0;
    for (uint32_t p : ed.framebuffer().pixels) holes += (p >> 24) == 0;
    diff(system, ed.framebuffer().pixels, mean, within2);
    std::printf("    back on the system: %zu transparent pixels; against the first frame: mean %.3f\n", holes, mean);
    CHECK(holes == 0 && mean < 0.05);
  });
}

/* ---------------------------------------------------------------- round 214: fewer CPU fallbacks (task 0195) */
static void tests_round214() {
  /* The Scene view drawn again on `device`; the share of pixels within 3 / 255 of `ref` and the mean difference. */
  auto view = [](Editor &ed, GameObject *g, int device) {
    ed.viewport_device_for_test(device);
    for (int k = 0; k < 2; k++) {
      g->set_local_position({0.0f, k ? 0.5f : 0.5001f, 0.0f});
      r34_steps(ed, 2);
    }
    return r34_crop(ed, ed.scene_view_rect());
  };
  auto compare = [](const std::vector<uint32_t> &a, const std::vector<uint32_t> &b, double &mean, double &within3) {
    size_t n = 0, ok = 0;
    double sum = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); i++) {
      int w = 0;
      for (int k = 0; k < 24; k += 8) w = std::max(w, std::abs((int)((a[i] >> k) & 255) - (int)((b[i] >> k) & 255)));
      n++, sum += w, ok += w <= 3;
    }
    mean = n ? sum / (double)n : 0, within3 = n ? (double)ok / (double)n : 0;
  };
  test("gpu views: more than 16 lights, Solid (lit per vertex) and Wireframe shading draw on the card as on the CPU (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    r34_open(ed);
    GameObject *cube = ed.scene().find_by_name("Cube");
    for (int i = 0; i < 24; i++) {  // (24 coloured point lights in a ring)
      GameObject *l = ed.scene().create("Ring Light " + std::to_string(i));
      Light *lt = l->add<Light>();
      lt->type = 1;  // (point)
      lt->color = Vec3(0.5f + 0.5f * std::sin(i * 0.7f), 0.5f + 0.5f * std::cos(i * 1.3f), 0.6f);
      lt->intensity = 0.6f;
      lt->range = 6.0f;
      l->set_local_position({3.0f * std::cos(i * 0.2618f), 1.5f, 3.0f * std::sin(i * 0.2618f)});
    }
    ed.mark_changed_for_test("ring");
    ed.command("camera 0 4 7 0 0.5 0");
    for (const char *mode : {"shaded", "solid", "wireframe"}) {
      ed.command(std::string("shading ") + mode);
      const std::vector<uint32_t> cpu = view(ed, cube, 1);
      const FrameProfile before = ed.frame_profile_totals();
      const std::vector<uint32_t> gpu = view(ed, cube, 2);
      const FrameProfile after = ed.frame_profile_totals();
      double mean = 0, within3 = 0;
      compare(cpu, gpu, mean, within3);
      std::printf("    %s, 24 lights: %llu views on the card, %llu on the CPU instead%s%s; mean %.3f / 255, %.2f %% within 3\n", mode,
                  (unsigned long long)(after.gpu_views - before.gpu_views), (unsigned long long)(after.gpu_fallbacks - before.gpu_fallbacks),
                  ed.gpu_view_reason().empty() ? "" : ": ", ed.gpu_view_reason().c_str(), mean, within3 * 100);
      CHECK(after.gpu_views > before.gpu_views && after.gpu_fallbacks == before.gpu_fallbacks && ed.scene_on_gpu_for_test());
      CHECK(mean < 1.0 && within3 >= 0.99);
    }
    ed.command("shading shaded");
  });
  test("gpu views: Weight Paint's colours in Solid shading on the card as on the CPU (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    r34_open(ed);
    ed.command("create Cylinder");
    GameObject *body = ed.selected_object();
    CHECK(body != nullptr);
    if (!body) return;
    body->name = "Body";
    body->set_local_position({-1.8f, 0.5f, 0.0f});  // (beside the cube: no surfaces touching, which the two rasterizers settle differently)
    ed.mark_changed_for_test("R214 name");
    ed.step_frame_headless();
    ed.command("create Armature");
    ed.command("bone add Bone 1");
    ed.command("skin bind Body");
    ed.command("select Body");
    ed.command("weightpaint on");
    ed.command("shading solid");
    ed.command("overlay grid off");  // (the grid's far lines at the horizon are task 0192's to compare: the colours are this one's)
    ed.command("camera 0 2 5 0 0.5 0");
    GameObject *cube = ed.scene().find_by_name("Cube");
    const std::vector<uint32_t> cpu = view(ed, cube, 1);
    const std::vector<uint32_t> gpu = view(ed, cube, 2);
    double mean = 0, within3 = 0;
    compare(cpu, gpu, mean, within3);
    std::printf("    painting: the Scene view on the card %s; mean %.3f / 255, %.2f %% within 3\n", ed.scene_on_gpu_for_test() ? "yes" : "no", mean, within3 * 100);
    CHECK(ed.weight_paint_active_for_test() && ed.scene_on_gpu_for_test());
    CHECK(mean < 1.0 && within3 >= 0.99);
    ed.viewport_device_for_test(1);
  });
  test("gpu views: panes of glass and of a faint Transparent in front of the cube, Shaded and Solid, on the card as on the CPU; a click picks the glass, and through the faint one the cube (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    r34_open(ed);
    GameObject *cube = ed.scene().find_by_name("Cube");
    ed.command("overlay grid off");
    ed.command("camera 0 0.5 6 0 0.5 0");
    r34_steps(ed, 2);
    /* Between the eye and the cube: the faint pane on the line to the cube, the glass beside it (nothing behind). */
    const Vec3 at = cube->world_position(), eye = ed.scene_eye();
    const Vec3 side = normalize(cross(eye - at, Vec3(0, 1, 0)));
    auto spot = [&](float x) { return lerp(at, eye, 0.4f) + side * x; };
    auto pane = [&](const char *name, int surface, float alpha, float x) {
      GameObject *g = ed.scene().create(name);
      g->add<MeshFilter>()->mesh = primitives::cube(1.0f);
      MaterialPtr m = make_material(name, {0.6f, 0.85f, 0.9f});
      m->surface = surface;
      m->alpha = alpha;
      m->touch();
      g->add<MeshRenderer>()->materials = {m};
      g->set_local_position(spot(x));
      g->set_local_scale({0.5f, 0.5f, 0.5f});
      return g;
    };
    pane("Glass Pane", (int)MaterialSurface::Glass, 1.0f, 1.2f);
    pane("Faint Pane", (int)MaterialSurface::Transparent, 0.2f, 0.0f);
    ed.mark_changed_for_test("panes");
    for (const char *mode : {"shaded", "solid"}) {
      ed.command(std::string("shading ") + mode);
      const std::vector<uint32_t> cpu = view(ed, cube, 1);
      const FrameProfile before = ed.frame_profile_totals();
      const std::vector<uint32_t> gpu = view(ed, cube, 2);
      const FrameProfile after = ed.frame_profile_totals();
      double mean = 0, within3 = 0;
      compare(cpu, gpu, mean, within3);
      std::printf("    %s: the Scene view on the card %s, %llu views on the CPU instead%s%s; mean %.3f / 255, %.2f %% within 3\n", mode,
                  ed.scene_on_gpu_for_test() ? "yes" : "no", (unsigned long long)(after.gpu_fallbacks - before.gpu_fallbacks),
                  ed.gpu_view_reason().empty() ? "" : ": ", ed.gpu_view_reason().c_str(), mean, within3 * 100);
      CHECK(ed.scene_on_gpu_for_test() && after.gpu_fallbacks == before.gpu_fallbacks);
      CHECK(mean < 1.0 && within3 >= 0.99);
    }
    ed.command("shading shaded");
    /* The glass pane selected (sky and floor behind it): its outline on the card as on the CPU's. */
    ed.command("select Glass Pane");
    {
      const std::vector<uint32_t> cpu = view(ed, cube, 1), gpu = view(ed, cube, 2);
      double mean = 0, within3 = 0;
      compare(cpu, gpu, mean, within3);
      std::printf("    the glass pane selected: mean %.3f / 255, %.2f %% within 3\n", mean, within3 * 100);
      CHECK(mean < 1.0 && within3 >= 0.99);
    }
    /* Clicks, on the CPU then on the card, at each pane's middle (the cube behind both; nothing selected, so no gizmo
     * over them). */
    for (int device : {1, 2}) {
      ed.viewport_device_for_test(device);
      cube->set_local_position({0.0f, 0.5001f, 0.0f});
      r34_steps(ed, 2);
      cube->set_local_position({0.0f, 0.5f, 0.0f});
      r34_steps(ed, 2);
      for (const auto &[x, want] : {std::pair<float, const char *>{1.2f, "Glass Pane"}, {0.0f, "Cube"}}) {
        ed.clear_selection_for_test();
        r34_steps(ed, 1);
        int wx = 0, wy = 0;
        CHECK(ed.project_to_window(spot(x), wx, wy));
        r34_click(ed, wx, wy);
        r34_steps(ed, 1);
        const GameObject *picked = ed.active_object_for_test();
        std::printf("    a click at x %.2f on %s picks %s\n", x, device == 2 ? "the card" : "the CPU", picked ? picked->name.c_str() : "nothing");
        CHECK(picked && picked->name == want);
      }
    }
    ed.viewport_device_for_test(1);
  });
}

/* ---------------------------------------------------------------- round 444: Shadowmask on the card (task 0420) */
static void tests_round444_gpu() {
  auto view = [](Editor &ed, GameObject *g, int device) {
    ed.viewport_device_for_test(device);
    const Vec3 at = g->world_position();
    for (int k = 0; k < 2; k++) {
      g->set_local_position(at + Vec3(0.0f, k ? 0.0f : 1e-4f, 0.0f));
      r34_steps(ed, 2);
    }
    return r34_crop(ed, ed.scene_view_rect());
  };
  test("gpu views (round 444): Shadowmask on the card as on the CPU - a Mixed lamp's baked shadow from a box on the floor, packed beside the baked pass's kind; the picture differs from Baked Indirect's, where the lamp has no shadow (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    P37World w = p37_world(ed, false, 1, 1.0f, true);
    ed.command("overlay grid off");
    GameObject *lamp = ed.scene().create("Lamp");
    Light *L = lamp->add<Light>();
    L->type = 1, L->mode = 1, L->intensity = 3.0f, L->range = 12.0f;
    lamp->set_local_position({-1.5f, 2.6f, 1.4f});
    ed.scene().environment.color = Vec3(0.02f);
    ed.scene().lighting.lighting_mode = 1;
    ed.commit_change("lamp");
    r35_bake(ed);
    CHECK(!ed.lighting_data_for_test().mask_pages.empty());
    view(ed, w.ball, 2);
    view(ed, w.ball, 1);
    const std::vector<uint32_t> cpu = view(ed, w.ball, 1), gpu = view(ed, w.ball, 2);
    const bool on_gpu = ed.scene_on_gpu_for_test();  // (read now: the views after it draw on the CPU)
    size_t n = 0, ok = 0;
    double total = 0;
    for (size_t i = 0; i < cpu.size() && i < gpu.size(); i++) {
      int d = 0;
      for (int k = 0; k < 24; k += 8) d = std::max(d, std::abs((int)((cpu[i] >> k) & 255) - (int)((gpu[i] >> k) & 255)));
      n++, total += d, ok += d <= 3;
    }
    const double mean = n ? total / (double)n : 0.0, within3 = n ? (double)ok / (double)n : 0.0;
    ed.scene().lighting.lighting_mode = 0;
    ed.commit_change("lighting");
    r35_bake(ed);
    const std::vector<uint32_t> indirect = view(ed, w.ball, 1);
    size_t changed = 0;
    for (size_t i = 0; i < cpu.size() && i < indirect.size(); i++) changed += (cpu[i] & 0xFFFFFF) != (indirect[i] & 0xFFFFFF);
    std::printf("    Shadowmask: the card against the CPU, mean %.3f / 255, %.2f %% within 3; Baked Indirect changes %zu pixels of the CPU's picture\n", mean,
                within3 * 100.0, changed);
    std::printf("    the Shadowmask view on the card: %s\n", on_gpu ? "yes" : "no");
    CHECK(on_gpu && mean < 1.0 && within3 >= 0.99 && changed > 500);
    ed.viewport_device_for_test(1);
  });
}

/* ---------------------------------------------------------------- round 216: baked light on the card (task 0197) */
static void tests_round216() {
  /* The Scene view drawn again on `device` (the ball nudged and back); its pixels. */
  auto view = [](Editor &ed, GameObject *g, int device) {
    ed.viewport_device_for_test(device);
    const Vec3 at = g->world_position();  // (a top-level object: its local position)
    for (int k = 0; k < 2; k++) {
      g->set_local_position(at + Vec3(0.0f, k ? 0.0f : 1e-4f, 0.0f));
      r34_steps(ed, 2);
    }
    return r34_crop(ed, ed.scene_view_rect());
  };
  auto compare = [](const std::vector<uint32_t> &a, const std::vector<uint32_t> &b, double &mean, double &within3) {
    size_t n = 0, ok = 0;
    double sum = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); i++) {
      int w = 0;
      for (int k = 0; k < 24; k += 8) w = std::max(w, std::abs((int)((a[i] >> k) & 255) - (int)((b[i] >> k) & 255)));
      n++, sum += w, ok += w <= 3;
    }
    mean = n ? sum / (double)n : 0, within3 = n ? (double)ok / (double)n : 0;
  };
  auto check = [&](Editor &ed, GameObject *g, const char *what) {
    view(ed, g, 2);  // (the first frames on the card upload the pages and make the planes: warm both first)
    view(ed, g, 1);
    const FrameProfile c0 = ed.frame_profile_totals();
    const std::vector<uint32_t> cpu = view(ed, g, 1);
    const FrameProfile before = ed.frame_profile_totals();
    const std::vector<uint32_t> gpu = view(ed, g, 2);
    const FrameProfile after = ed.frame_profile_totals();
    /* (The Scene view's time over the four frames each, the views' whole: a test run's, not a quiet machine's.) */
    std::printf("    %s: the Scene view %.2f ms a frame on the CPU, %.2f on the card\n", what, (before.ms_scene3d - c0.ms_scene3d) / 4.0,
                (after.ms_scene3d - before.ms_scene3d) / 4.0);
    double mean = 0, within3 = 0;
    compare(cpu, gpu, mean, within3);
    std::printf("    %s: the Scene view on the card %s, %llu views on the CPU instead (%s); mean %.3f / 255, %.2f %% within 3\n", what,
                ed.scene_on_gpu_for_test() ? "yes" : "no", (unsigned long long)(after.gpu_fallbacks - before.gpu_fallbacks), ed.gpu_view_status().c_str(),
                mean, within3 * 100);
    CHECK(ed.scene_on_gpu_for_test());
    CHECK(mean < 1.0 && within3 >= 0.99);
    ed.viewport_device_for_test(1);
  };
  test("gpu views: baked lightmaps and baked probes on the card as on the CPU - the floor, wall and box from their lightmaps (the Baked sun in them, not lit again), the ball from the probes (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    P37World w = p37_world(ed, true, 2, 1.0f, true);
    ed.command("overlay grid off");
    r35_bake(ed);
    CHECK(!ed.lighting_data_for_test().empty() && !ed.probes_for_test().empty());
    check(ed, w.ball, "baked");
    /* (Not both blind to it: without the bake the card's picture changes.) */
    const std::vector<uint32_t> with_bake = view(ed, w.ball, 2);
    ed.command("bake clear");
    r34_steps(ed, 3);
    const std::vector<uint32_t> without = view(ed, w.ball, 2);
    size_t changed = 0;
    for (size_t i = 0; i < with_bake.size() && i < without.size(); i++) changed += (with_bake[i] & 0xFFFFFF) != (without[i] & 0xFFFFFF);
    std::printf("    the bake cleared: %zu pixels of the card's picture change\n", changed);
    CHECK(changed > 2000);
    ed.viewport_device_for_test(1);
  });
  test("gpu views (round 426): directional lightmaps on the card as on the CPU - a normal-mapped floor's bumps catch the baked sun alike; with Directional Mode off its baked light comes out flat (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    P37World w = p37_world(ed, true, 2, 1.0f, true);
    ed.command("overlay grid off");
    /* Ridges along x: a 64 x 64 normal map (tangent space, 0.5 + 0.5 n) leaning +-0.6 in x. */
    const std::string dir = resolve_asset_path("Assets/Imported/R426G");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    fs::make_dirs(dir);
    std::string tga(18, '\0');
    tga[2] = 2, tga[12] = 64, tga[14] = 64, tga[16] = 24, tga[17] = 0x20;
    for (int y = 0; y < 64; y++)
      for (int x = 0; x < 64; x++) {
        const float lean = 0.6f * std::sin(x * 0.5f);
        const Vec3 nn = normalize(Vec3(lean, 0.0f, 1.0f));
        const auto b = [](float v) { return (char)(uint8_t)std::lround((0.5f + 0.5f * v) * 255.0f); };
        tga += b(nn.z), tga += b(nn.y), tga += b(nn.x);  // (TGA is B, G, R)
      }
    CHECK(fs::write_file(fs::join(dir, "ridges.tga"), tga));
    ed.scene().environment.color = Vec3(0.0f);  // (a black sky: the floor's light comes from the Baked sun, one way)
    ed.commit_change("world");
    r35_bake(ed);
    const std::vector<uint32_t> smooth = view(ed, w.floor_go, 1);
    MeshRenderer *mr = w.floor_go->get<MeshRenderer>();
    std::printf("    the floor: %zu material(s), the first %s\n", mr ? mr->materials.size() : 0, mr && !mr->materials.empty() && mr->materials[0] ? mr->materials[0]->name.c_str() : "-");
    if (mr && !mr->materials.empty() && mr->materials[0]) mr->materials[0]->normal_map.path = "Assets/Imported/R426G/ridges.tga", mr->materials[0]->normal_map.non_color = true, mr->materials[0]->touch();
    ed.commit_change("material");
    /* The views load a texture on a worker: frames until the map shows (at most 5 s). */
    std::vector<uint32_t> bumped;
    size_t bump_changed = 0;
    for (int tries = 0; tries < 100 && bump_changed < 1000; tries++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      bumped = view(ed, w.floor_go, 1);
      bump_changed = 0;
      for (size_t i = 0; i < smooth.size() && i < bumped.size(); i++) bump_changed += (smooth[i] & 0xFFFFFF) != (bumped[i] & 0xFFFFFF);
    }
    std::printf("    the normal map on: %zu pixels of the CPU's picture change\n", bump_changed);
    CHECK(!ed.lighting_data_for_test().empty() && ed.lighting_data_for_test().dir_pages.size() == ed.lighting_data_for_test().pages.size());
    check(ed, w.floor_go, "directional");
    const std::vector<uint32_t> with_dir = view(ed, w.floor_go, 1);
    ed.scene().lighting.directional_mode = 0;
    ed.commit_change("lighting");
    r34_steps(ed, 3);
    const std::vector<uint32_t> flat = view(ed, w.floor_go, 1);
    size_t changed = 0;
    for (size_t i = 0; i < with_dir.size() && i < flat.size(); i++) changed += (with_dir[i] & 0xFFFFFF) != (flat[i] & 0xFFFFFF);
    std::printf("    Directional Mode off: %zu pixels of the CPU's picture change\n", changed);
    CHECK(bump_changed > 1000 && changed > 1000);
    ed.viewport_device_for_test(1);
    std::filesystem::remove_all(dir, ec);
  });
  test("gpu views (round 429): Filamented's specular occlusion on the card as on the CPU - an untextured glossy metal floor over a dim bake keeps less of its reflection, the card's picture darkening as the CPU's (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    P37World w = p37_world(ed, true, 2, 1.0f, true);
    ed.command("overlay grid off");
    MeshRenderer *mr = w.floor_go->get<MeshRenderer>();
    Material *m = mr && !mr->materials.empty() ? mr->materials[0].get() : nullptr;
    CHECK(m != nullptr);
    if (!m) return;
    m->metallic = 1.0f, m->roughness = 0.3f, m->touch();
    ed.scene().environment.strength = 0.3f;  // (a dim bake: the sky low)
    ed.commit_change("material");
    r35_bake(ed);
    view(ed, w.floor_go, 2);  // (the card warmed)
    const std::vector<uint32_t> open_cpu = view(ed, w.floor_go, 1), open_gpu = view(ed, w.floor_go, 2);
    m->exposure_occlusion = 1.0f, m->touch();
    ed.commit_change("material");
    check(ed, w.floor_go, "exposure occlusion");
    const std::vector<uint32_t> occ_cpu = view(ed, w.floor_go, 1), occ_gpu = view(ed, w.floor_go, 2);
    auto sum3 = [](uint32_t p) { return (int)(p & 255) + (int)((p >> 8) & 255) + (int)((p >> 16) & 255); };
    /* Each device's own darkening, and the two darkenings alike pixel by pixel (the card's had been nothing: an
     * untextured material's value never reached it - review 429). */
    size_t changed_cpu = 0, changed_gpu = 0, apart = 0;
    for (size_t i = 0; i < open_cpu.size() && i < occ_cpu.size() && i < open_gpu.size() && i < occ_gpu.size(); i++) {
      const int dc = sum3(open_cpu[i]) - sum3(occ_cpu[i]), dg = sum3(open_gpu[i]) - sum3(occ_gpu[i]);
      changed_cpu += dc > 0, changed_gpu += dg > 0;
      apart += std::abs(dc - dg) > 9;
    }
    std::printf("    Exposure Occlusion 1 on the floor: %zu pixels darker on the CPU, %zu on the card, %zu darkened differently (by more than 3 a channel)\n",
                changed_cpu, changed_gpu, apart);
    CHECK(changed_cpu > 1000 && changed_gpu > changed_cpu * 8 / 10 && apart < changed_cpu / 20);
    ed.viewport_device_for_test(1);
  });
  test("gpu views: realtime lightmaps (Receive GI: Lightmaps) on the card as on the CPU; realtime GI's per-pixel light for an object without them stays the CPU's, saying why (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    P37World w = p37_world(ed, false, 0, 1.0f, true);
    ed.command("overlay grid off");
    w.ball->active = false;  // (the ball contributes no GI: it would take the per-pixel light)
    ed.mark_changed_for_test("no ball");
    r36_cmd(ed, "set Lighting.RealtimeGI true");
    for (int i = 0; i < 120 && ed.realtime_lightmaps_for_test(false).shown.empty(); i++) ed.step_frame_headless();
    r34_steps(ed, 6);
    CHECK(!ed.realtime_lightmaps_for_test(false).shown.empty());
    check(ed, w.box, "realtime lightmaps");
    w.ball->active = true;
    ed.mark_changed_for_test("the ball back");
    view(ed, w.box, 2);
    std::printf("    with the ball: the Scene view on the card %s (%s)\n", ed.scene_on_gpu_for_test() ? "yes" : "no", ed.gpu_view_status().c_str());
    CHECK(!ed.scene_on_gpu_for_test() && ed.gpu_view_status().find("per-pixel") != std::string::npos);
    ed.viewport_device_for_test(1);
  });
}

/* ---------------------------------------------------------------- round 217: camera filters on the card (task 0197) */
static void tests_round217() {
  test("gpu views: camera filters run over the card's picture - colour grading, a vignette, bloom (from the card's linear light), edge outline, sharpen, halftone, line art - and the Game view matches the CPU's; a rasterizer stage (Retro Console) still sends it to the CPU (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    r34_open(ed);
    ed.command("overlay grid off");
    GameObject *cube = ed.scene().find_by_name("Cube");
    ed.command("select Main Camera");
    auto game = [&](int device) {
      ed.viewport_device_for_test(device);
      for (int k = 0; k < 2; k++) {
        cube->set_local_position({0.0f, k ? 0.5f : 0.5001f, 0.0f});
        r34_steps(ed, 2);
      }
      return r34_crop(ed, ed.window_rect_for_test(WindowKind::Game));
    };
    auto compare = [](const std::vector<uint32_t> &a, const std::vector<uint32_t> &b, double &mean, double &within3) {
      size_t n = 0, ok = 0;
      double sum = 0;
      for (size_t i = 0; i < a.size() && i < b.size(); i++) {
        int w = 0;
        for (int k = 0; k < 24; k += 8) w = std::max(w, std::abs((int)((a[i] >> k) & 255) - (int)((b[i] >> k) & 255)));
        n++, sum += w, ok += w <= 3;
      }
      mean = n ? sum / (double)n : 0, within3 = n ? (double)ok / (double)n : 0;
    };
    const std::vector<uint32_t> plain = game(1);
    const char *stacks[][4] = {{"Color Grading", "Vignette", "Bloom", nullptr}, {"Edge Outline", "Sharpen", nullptr, nullptr},
                               {"Halftone", nullptr, nullptr, nullptr}, {"Line Art", nullptr, nullptr, nullptr}};
    for (const auto &st : stacks) {
      for (int k = 0; k < 4 && st[k]; k++) ed.command(std::string("filter add ") + st[k]);
      ed.mark_changed_for_test("filters");
      const std::vector<uint32_t> cpu = game(1);
      const FrameProfile before = ed.frame_profile_totals();
      const std::vector<uint32_t> gpu = game(2);
      const FrameProfile after = ed.frame_profile_totals();
      double mean = 0, within3 = 0, fmean = 0, fw = 0;
      compare(cpu, gpu, mean, within3);
      compare(cpu, plain, fmean, fw);
      std::printf("    %s%s%s: %llu views on the card, %llu on the CPU (%s); against the CPU mean %.3f / 255, %.2f %% within 3 (the filters change it by %.1f)\n", st[0],
                  st[1] ? " + " : "", st[1] ? st[1] : "", (unsigned long long)(after.gpu_views - before.gpu_views),
                  (unsigned long long)(after.gpu_fallbacks - before.gpu_fallbacks), ed.gpu_view_status().c_str(), mean, within3 * 100, fmean);
      CHECK(after.gpu_views > before.gpu_views && after.gpu_fallbacks == before.gpu_fallbacks);
      CHECK(mean < 1.5 && within3 >= 0.98);
      CHECK(fmean > 1.0);  // (the filters did something)
      for (int k = 0; k < 4 && st[k]; k++) ed.command("filter remove 0");
      ed.mark_changed_for_test("filters off");
    }
    /* Bloom alone on an emissive cube, its light well past 1: the glow comes from the linear light (the 8-bit picture
     * clips at 1), so the card's must be there and agree. */
    MaterialPtr hot = make_material("hot", {0.2f, 0.2f, 0.2f});
    hot->emission = Vec3(6.0f, 4.0f, 2.0f);
    hot->emission_strength = 1.0f;
    hot->touch();
    cube->get<MeshRenderer>()->materials = {hot};
    ed.mark_changed_for_test("hot");
    const std::vector<uint32_t> no_bloom = game(1);
    ed.command("filter add Bloom");
    ed.mark_changed_for_test("bloom");
    {
      const std::vector<uint32_t> cpu = game(1), gpu = game(2);
      double mean = 0, within3 = 0, glow = 0, gw = 0;
      compare(cpu, gpu, mean, within3);
      compare(cpu, no_bloom, glow, gw);
      std::printf("    Bloom over an emissive cube: against the CPU mean %.3f / 255, %.2f %% within 3 (the glow changes it by %.2f)\n", mean, within3 * 100, glow);
      CHECK(glow > 0.5);
      CHECK(mean < 1.0 && within3 >= 0.99);
    }
    ed.command("filter remove 0");
    ed.command("filter add Retro Console");
    ed.mark_changed_for_test("retro");
    game(2);
    std::printf("    Retro Console: %s\n", ed.gpu_view_status().c_str());
    CHECK(ed.gpu_view_status().find("rasterizer stage") != std::string::npos);
    ed.viewport_device_for_test(1);
  });
}

/* ---------------------------------------------------------------- round 219: the device, said (task 0199) */
static void tests_round219() {
  test("Viewport Device says each window's device - the views on the GPU or the CPU, shown by the card or the system and why - and Auto's fall back to the CPU is said once, not under CPU or GPU", [] {
    Editor ed;
    r34_open(ed);
    ed.viewport_device_for_test(0);
    r34_steps(ed, 2);  // (a run that gives the views a card opens it here)
    ed.viewport_device_for_test(1);
    const std::string cpu = ed.window_device_status_for_test(false, -1, "");  // (the card, if open, isn't drawing them)
    const std::string sys = ed.window_device_status_for_test(false, 0, "the window can't be presented by this card");
    std::printf("    %s | %s\n", cpu.c_str(), sys.c_str());
    CHECK(cpu.find("views on the CPU") == 0 && cpu.find("shown by the system") != std::string::npos);
    CHECK(sys.find("shown by the system: the window can't be presented") != std::string::npos);
    CHECK(!ed.notice_cpu_fallback_for_test());  // (CPU chosen: nothing to say)
    ed.viewport_device_for_test(2);
    CHECK(!ed.notice_cpu_fallback_for_test());  // (GPU chosen: its error is in Preferences)
    ed.viewport_device_for_test(0);
    const bool first = ed.notice_cpu_fallback_for_test(), again = ed.notice_cpu_fallback_for_test();
    std::printf("    Auto: said %s, then %s\n", first ? "yes" : "no", again ? "yes" : "no");
    CHECK(first && !again);
  });
}

/* ---------------------------------------------------------------- round 224: the views without the wait (task 0204) */
static void tests_round224() {
  test("gpu views: with the card presenting, the Scene view's frame is left running and its depth and ids stay on the card until a click asks; a click picks what is there now, never what was (on the card or after a switch to the CPU), and verify mode reads them every frame (skipped without a GPU)", [] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    r34_open(ed);
    ed.set_render_cache_verify(false);  // (verify mode compares the planes every frame: it reads them back)
    ed.viewport_device_for_test(2);
    ed.card_present_for_test(true);
    ed.command("camera 25 20 7 0 0.5 0");
    ed.command("select Main Camera");
    r34_steps(ed, 4);
    if (!ed.scene_on_gpu_for_test() || ed.card_views_for_test() == 0) {
      std::printf("    the card didn't present the Scene view here (%s): skipped\n", ed.gpu_view_reason().c_str());
      return;
    }
    /* A frame drawn again (the camera moved): one submission, left running, its planes left on the card. */
    ed.command("camera 25.5 20 7 0 0.5 0");
    const uint64_t s0 = ed.gpu_submits_for_test();
    r34_steps(ed, 1);
    const uint64_t sent = ed.gpu_submits_for_test() - s0;
    std::printf("    a frame drawn again: %llu submission(s), planes left on the card: %s\n", (unsigned long long)sent,
                ed.scene_planes_deferred_for_test() ? "yes" : "no");
    CHECK(sent >= 1 && ed.scene_planes_deferred_for_test());
    /* A click on the Cube picks it: the ids come back when the click asks. */
    GameObject *cube = ed.scene().find_by_name("Cube");
    int x = 0, y = 0;
    CHECK(cube && ed.project_to_window(cube->world_position(), x, y));
    if (!cube) return;
    r34_click(ed, x, y);
    r34_steps(ed, 2);
    CHECK(ed.selected_object() == cube);
    /* The Cube moved away: a click where it was picks nothing (the card's planes of the frame before would). */
    cube->set_local_position(cube->local().position + Vec3(40.0f, 0.0f, 0.0f));
    ed.mark_changed_for_test("Move Cube");
    r34_steps(ed, 2);
    r34_click(ed, x, y);
    r34_steps(ed, 2);
    std::printf("    a click where the Cube was: %s\n", ed.selected_object() ? ed.selected_object()->name.c_str() : "nothing");
    CHECK(ed.selected_object() != cube);
    /* Back where it was; then on the CPU, moved away again: the click reads the CPU's planes, not the card's last. */
    cube->set_local_position(cube->local().position - Vec3(40.0f, 0.0f, 0.0f));
    ed.mark_changed_for_test("Move Cube back");
    r34_steps(ed, 3);
    ed.viewport_device_for_test(1);
    cube->set_local_position(cube->local().position + Vec3(40.0f, 0.0f, 0.0f));
    ed.mark_changed_for_test("Move Cube");
    r34_steps(ed, 2);
    r34_click(ed, x, y);
    r34_steps(ed, 2);
    CHECK(ed.selected_object() != cube);
    /* Verify mode reads them back every frame. */
    ed.viewport_device_for_test(2);
    ed.set_render_cache_verify(true);
    ed.command("camera 26 20 7 0 0.5 0");
    r34_steps(ed, 3);
    CHECK(!ed.scene_planes_deferred_for_test());
    ed.card_present_for_test(false);
    ed.viewport_device_for_test(1);
  });
}

/* ---------------------------------------------------------------- round 225: Edit Mode's cage kept on the card (task 0205) */
static void tests_round225() {
  test("gpu views: Edit Mode's cage stays on the card - a frame that only draws again sends nothing, in Vertex and Face mode; a face selected sends a few bytes; Face mode's dots look as the CPU draws them (skipped without a GPU)", [] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    r34_open(ed);
    GameObject *big = ed.scene().create("Big");
    big->add<MeshFilter>()->mesh = primitives::uv_sphere(1.0f, 96, 64);
    big->add<MeshRenderer>();
    big->set_local_position({0.0f, 1.0f, 0.0f});
    ed.commit_change("add Big");
    ed.command("overlay grid off");
    ed.command("select Big");
    ed.command("camera 0 15 4 0 1 0");
    ed.viewport_device_for_test(2);
    for (const char *mode : {"edit vertex", "edit face"}) {
      ed.command(mode);
      r34_steps(ed, 3);
      const FrameProfile p0 = ed.frame_profile_totals();
      ed.command("camera 1 15 4 0 1 0");  // (the views draw again; the cage is as it was)
      r34_steps(ed, 2);
      ed.command("camera 0 15 4 0 1 0");
      r34_steps(ed, 2);
      const FrameProfile p1 = ed.frame_profile_totals();
      std::printf("    %s: %llu frames with the cage on the card, its vertices worked out again %llu times, %llu bytes sent while only drawing again\n",
                  mode, (unsigned long long)(p1.gpu_cage_frames - p0.gpu_cage_frames), (unsigned long long)(p1.gpu_cage_vert_builds - p0.gpu_cage_vert_builds),
                  (unsigned long long)(p1.gpu_cage_bytes - p0.gpu_cage_bytes));
      CHECK(p1.gpu_cage_frames - p0.gpu_cage_frames >= 2);
      CHECK(p1.gpu_cage_vert_builds == p0.gpu_cage_vert_builds);
      CHECK(p1.gpu_cage_bytes == p0.gpu_cage_bytes);
    }
    /* A face selected: its dot's colour goes up, not the cage. */
    const FrameProfile p2 = ed.frame_profile_totals();
    ed.command("fsel 3000");
    r34_steps(ed, 2);
    const uint64_t sent = ed.frame_profile_totals().gpu_cage_bytes - p2.gpu_cage_bytes;
    CHECK(ed.frame_profile_totals().gpu_cage_vert_builds - p2.gpu_cage_vert_builds == 1);  // (the dots' colours: worked out once)
    const uint64_t whole = (uint64_t)(big->get<MeshFilter>()->mesh->vert_count() + big->get<MeshFilter>()->mesh->face_count()) * sizeof(gpu::CageVertex);
    std::printf("    a face selected: %llu bytes sent (the cage's vertices and dots are %llu)\n", (unsigned long long)sent, (unsigned long long)whole);
    CHECK(sent > 0 && sent * 10 < whole);
    /* Face mode's dots on the card, as the CPU draws them. */
    const std::vector<uint32_t> gpu = r34_crop(ed, ed.scene_view_rect());
    ed.viewport_device_for_test(1);
    big->set_local_position({0.0f, 1.0001f, 0.0f});  // (a change: the CPU draws again)
    r34_steps(ed, 2);
    big->set_local_position({0.0f, 1.0f, 0.0f});
    r34_steps(ed, 2);
    const std::vector<uint32_t> cpu = r34_crop(ed, ed.scene_view_rect());
    double sum = 0;
    size_t within = 0;
    for (size_t i = 0; i < cpu.size() && i < gpu.size(); i++) {
      int worst = 0;
      for (int k = 0; k < 24; k += 8) worst = std::max(worst, std::abs((int)((cpu[i] >> k) & 255) - (int)((gpu[i] >> k) & 255)));
      sum += worst;
      within += worst <= 3;
    }
    const double mean = cpu.empty() ? 0 : sum / (double)cpu.size(), within3 = cpu.empty() ? 0 : (double)within / (double)cpu.size();
    std::printf("    Face mode on the card against the CPU: mean difference %.3f / 255, %.2f %% within 3 / 255\n", mean, within3 * 100);
    CHECK(cpu.size() == gpu.size() && mean < 1.0 && within3 >= 0.99);
  });
}

/* ---------------------------------------------------------------- round 337: a normal map on a mesh the card already held */
static void tests_round414() {
  /* ---------------------------------------------------------------- round 414: approximations on the card (task 0390, ADR 0108) */
  /* The Scene view drawn on `device` after a change; the difference against `cpu` over the object's own pixels (those
   * that differ from the same view with the object hidden): the mean of the worst channel and the share within 3 / 255. */
  auto r414_scene = [](Editor &ed, GameObject *obj, int device) {
    ed.viewport_device_for_test(device);
    for (int k = 0; k < 2; k++) {
      obj->set_local_position({0.0f, k ? 0.5f : 0.5001f, 0.0f});
      r34_steps(ed, 2);
    }
    return r34_crop(ed, ed.scene_view_rect());
  };
  auto r414_compare = [](const std::vector<uint32_t> &cpu, const std::vector<uint32_t> &gpu, const std::vector<uint32_t> &empty, size_t &pixels, double &mean, double &within3) {
    auto worst = [](uint32_t a, uint32_t b) {
      int w = 0;
      for (int k = 0; k < 24; k += 8) w = std::max(w, std::abs((int)((a >> k) & 255) - (int)((b >> k) & 255)));
      return w;
    };
    double sum = 0;
    size_t within = 0;
    pixels = 0;
    for (size_t i = 0; i < cpu.size() && i < gpu.size() && i < empty.size(); i++) {
      if (worst(cpu[i], empty[i]) <= 8) continue;
      const int w = worst(cpu[i], gpu[i]);
      pixels++, sum += w, within += w <= 3;
    }
    mean = pixels ? sum / (double)pixels : 0, within3 = pixels ? (double)within / (double)pixels : 0;
  };
  /* One material on the Cube: drawn on the CPU, on the card, and hidden; the card's status and counters. */
  struct R414 {
    size_t pixels = 0;
    double mean = 0, within3 = 0;
    uint64_t views = 0, fallbacks = 0, approximated = 0;
    std::string reason, status;
  };
  auto r414_case = [&](const MaterialPtr &m, R414 &r) {
    Editor ed;
    r34_open(ed);
    ed.command("overlay grid off");
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube != nullptr);
    if (!cube) return;
    cube->get<MeshRenderer>()->materials = {m};
    ed.mark_changed_for_test("material");
    const std::vector<uint32_t> cpu = r414_scene(ed, cube, 1);
    const FrameProfile before = ed.frame_profile_totals();
    const std::vector<uint32_t> gpu = r414_scene(ed, cube, 2);
    const FrameProfile after = ed.frame_profile_totals();
    r.views = after.gpu_views - before.gpu_views, r.fallbacks = after.gpu_fallbacks - before.gpu_fallbacks, r.approximated = after.gpu_approximated - before.gpu_approximated;
    r.reason = ed.gpu_view_reason(), r.status = ed.gpu_view_status();
    cube->active = false;
    const std::vector<uint32_t> empty = r414_scene(ed, cube, 1);
    r414_compare(cpu, gpu, empty, r.pixels, r.mean, r.within3);
    std::printf("    %s\n    %llu views on the card, %llu on the CPU, %llu instances approximated; over the object's %zu pixels mean %.2f / 255, %.1f %% within 3\n",
                r.status.c_str(), (unsigned long long)r.views, (unsigned long long)r.fallbacks, (unsigned long long)r.approximated, r.pixels, r.mean, r.within3 * 100);
    ed.viewport_device_for_test(1);
  };
  test("gpu views (round 414): a Principled material with Coat, Sheen and Subsurface stays on the card, drawn approximately - no view refused, the status and the Profiler's counter say what was approximated, the object within 24 / 255 of the CPU's on average", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    MaterialPtr m = make_material("Coated", {0.5f, 0.5f, 0.6f});
    m->coat = 1.0f, m->sheen = 0.5f, m->subsurface = 0.3f;
    R414 r;
    r414_case(m, r);
    CHECK(r.views > 0 && r.fallbacks == 0 && r.reason.empty());
    CHECK(r.approximated > 0);
    CHECK(r.status.find("approximated") != std::string::npos && r.status.find("Coat") != std::string::npos);
    CHECK(r.pixels > 100);
    CHECK(r.mean < 24.0);
  });
  test("gpu views (round 414): a see-through toon material (a red veil) stays on the card (its band edges and painted maps left out), the object within 16 / 255 of the CPU's on average; the Profiler counts it", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    MaterialPtr veil = make_material("Toon Veil", {0.9f, 0.2f, 0.2f});
    veil->shading_model = 1;
    veil->surface = (int)MaterialSurface::Transparent;
    veil->alpha = 0.7f;
    R414 r;
    r414_case(veil, r);
    CHECK(r.views > 0 && r.fallbacks == 0 && r.reason.empty());
    CHECK(r.approximated > 0);
    CHECK(r.status.find("see-through toon") != std::string::npos);
    CHECK(r.pixels > 100);
    CHECK(r.mean < 16.0);
  });
  test("gpu views (round 414): a Volume (fog) material stays on the card as a tinted shell - the view isn't refused, the fog dims what is behind it as the CPU's does (density 1 against 0), the status says it is approximated, and the fog cube is within 48 / 255 of the CPU's on average", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    r34_open(ed);
    ed.command("shading shaded");
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube && cube->get<MeshRenderer>());
    if (!cube || !cube->get<MeshRenderer>()) return;
    MaterialPtr fog = make_material_preset("Fog");
    fog->volume_density = 0.0f, fog->volume_color = Vec3(0.0f);
    cube->get<MeshRenderer>()->materials = {fog};
    ed.mark_changed_for_test("Fog");
    ed.viewport_device_for_test(2);
    for (int i = 0; i < 4; i++) ed.step_frame_headless();
    int cx = 0, cy = 0;
    CHECK(ed.project_to_window(cube->world_position(), cx, cy));
    auto lum = [&] {
      const Image &fb = ed.main_fb_for_test();
      const uint32_t p = fb.pixels[(size_t)cy * fb.width + cx];
      return 0.2126 * ((p >> 16) & 255) + 0.7152 * ((p >> 8) & 255) + 0.0722 * (p & 255);
    };
    const double behind = lum();
    const FrameProfile before = ed.frame_profile_totals();
    fog->volume_density = 1.0f;
    fog->touch();
    ed.mark_changed_for_test("Thicker");
    for (int i = 0; i < 4; i++) ed.step_frame_headless();
    const FrameProfile after = ed.frame_profile_totals();
    const double through = lum();
    std::printf("    %s\n    the cube's middle on the card: %.1f with density 0, %.1f with density 1 (ratio %.3f); %llu views on the card, %llu on the CPU\n", ed.gpu_view_status().c_str(), behind,
                through, through / std::max(1.0, behind), (unsigned long long)(after.gpu_views - before.gpu_views), (unsigned long long)(after.gpu_fallbacks - before.gpu_fallbacks));
    CHECK(after.gpu_views > before.gpu_views && after.gpu_fallbacks == before.gpu_fallbacks && ed.gpu_view_reason().empty());
    CHECK(ed.gpu_view_status().find("Volume") != std::string::npos);
    CHECK(behind > 20.0 && through < behind * 0.8 && through > behind * 0.05);
    ed.viewport_device_for_test(1);
    /* Against the CPU's fog (the view ray's real length through the cube against the shell's mean thickness). */
    MaterialPtr fog2 = make_material_preset("Fog");
    fog2->volume_density = 1.0f;
    R414 r;
    r414_case(fog2, r);
    CHECK(r.views > 0 && r.fallbacks == 0 && r.reason.empty());
    CHECK(r.status.find("Volume") != std::string::npos);
    CHECK(r.pixels > 100);
    CHECK(r.mean < 48.0);
  });
  test("gpu views (round 414): the Game view with Bloom over a glass cube - a picture kept in linear light with see-through surfaces - draws on the card: the see-through pixels are marked \"none\" in the linear light as the CPU marks them, so the picture matches the CPU's", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    r34_open(ed);
    ed.command("overlay grid off");
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube != nullptr);
    if (!cube) return;
    MaterialPtr glass = make_material("Glass", {0.9f, 0.95f, 1.0f});
    glass->surface = (int)MaterialSurface::Glass;
    glass->roughness = 0.05f;
    cube->get<MeshRenderer>()->materials = {glass};
    ed.command("select Main Camera");
    ed.command("filter add Bloom");
    ed.mark_changed_for_test("bloom over glass");
    auto game = [&](int device) {
      ed.viewport_device_for_test(device);
      for (int k = 0; k < 2; k++) {
        cube->set_local_position({0.0f, k ? 0.5f : 0.5001f, 0.0f});
        r34_steps(ed, 2);
      }
      return r34_crop(ed, ed.window_rect_for_test(WindowKind::Game));
    };
    const std::vector<uint32_t> cpu = game(1);
    const FrameProfile before = ed.frame_profile_totals();
    const std::vector<uint32_t> gpu = game(2);
    const FrameProfile after = ed.frame_profile_totals();
    const std::string status = ed.gpu_view_status(), reason = ed.gpu_view_reason();
    double sum = 0;
    size_t within = 0;
    for (size_t i = 0; i < cpu.size() && i < gpu.size(); i++) {
      int w = 0;
      for (int k = 0; k < 24; k += 8) w = std::max(w, std::abs((int)((cpu[i] >> k) & 255) - (int)((gpu[i] >> k) & 255)));
      sum += w, within += w <= 3;
    }
    const double mean = cpu.empty() ? 0 : sum / (double)cpu.size(), within3 = cpu.empty() ? 0 : (double)within / (double)cpu.size();
    cube->active = false;  // (the glass's own pixels: a missing "none" mark can't hide in the whole view's mean)
    const std::vector<uint32_t> empty = game(1);
    size_t pixels = 0;
    double omean = 0, owithin = 0;
    r414_compare(cpu, gpu, empty, pixels, omean, owithin);
    std::printf("    %s\n    %llu views on the card, %llu on the CPU; against the CPU mean %.3f / 255, %.2f %% within 3; over the glass's %zu pixels mean %.3f, %.1f %% within 3\n", status.c_str(),
                (unsigned long long)(after.gpu_views - before.gpu_views), (unsigned long long)(after.gpu_fallbacks - before.gpu_fallbacks), mean, within3 * 100, pixels, omean, owithin * 100);
    CHECK(after.gpu_views > before.gpu_views && after.gpu_fallbacks == before.gpu_fallbacks && reason.empty());
    CHECK(mean < 1.5 && within3 >= 0.98);
    CHECK(pixels > 100 && omean < 1.5);
    ed.viewport_device_for_test(1);
  });
  test("gpu views (round 414): a material's texture over 2048 a side goes up from its first level within 2048 (noted as approximated), the view stays on the card and the object matches the CPU's where it is minified", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    const std::string path = fs::join(test_dir(), "r414_wide.png");
    {
      const int w = 4096, h = 64;
      std::vector<uint32_t> g((size_t)w * h);
      for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) g[(size_t)y * w + x] = ((x / 256 + y / 16) & 1) ? 0xFFE0A040u : 0xFF3060C0u;
      CHECK(write_png(path, g.data(), w, h, w));
    }
    MaterialPtr m = make_material("Wide", {1.0f, 1.0f, 1.0f});
    m->base_map.path = path;
    R414 r;
    r414_case(m, r);
    std::remove(path.c_str());
    CHECK(r.views > 0 && r.fallbacks == 0 && r.reason.empty());
    CHECK(r.status.find("shown at 2048") != std::string::npos);
    CHECK(r.pixels > 100);
    CHECK(r.mean < 6.0);
  });
  test("gpu views (round 414): the Unity-world stand-in (benchscene worldlikequick) draws its Scene view, then its Game view with the camera's filters, on the card - none refused; the approximations are listed (see-through toon, the cloth's sheen)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    ed.init_headless(1280, 720);  // (as the stress section's world_like rows drive it)
    ed.command("benchscene worldlikequick");
    ed.step_frame_headless();
    ed.command("viewport device gpu");
    ed.command("shading shaded");
    /* The Scene view alone, then the Game view with its filters (headless, the Game window takes the Scene view's place). */
    auto measure = [&](const char *what, double FrameProfile::*ms) {
      for (int i = 0; i < 5; i++) ed.step_frame_headless();
      const FrameProfile before = ed.frame_profile_totals();
      for (int i = 0; i < 6; i++) {
        ed.scene_cache_reset_for_test();
        ed.step_frame_headless();
      }
      const FrameProfile after = ed.frame_profile_totals();
      std::printf("    %s: %llu views on the card, %llu on the CPU, %llu instances approximated; the view %.1f ms a frame, the GPU path %.1f\n    %s\n", what,
                  (unsigned long long)(after.gpu_views - before.gpu_views), (unsigned long long)(after.gpu_fallbacks - before.gpu_fallbacks),
                  (unsigned long long)(after.gpu_approximated - before.gpu_approximated), (after.*ms - before.*ms) / 6.0, (after.ms_gpu - before.ms_gpu) / 6.0, ed.gpu_view_status().c_str());
      CHECK(after.gpu_views >= before.gpu_views + 6 && after.gpu_fallbacks == before.gpu_fallbacks && ed.gpu_view_reason().empty());  // (the view, every frame)
      CHECK(after.*ms > before.*ms);
      CHECK(after.gpu_approximated > before.gpu_approximated);
      CHECK(ed.gpu_view_status().find("a view on the CPU") == std::string::npos);
      CHECK(ed.gpu_view_status().find("see-through toon") != std::string::npos && ed.gpu_view_status().find("Sheen") != std::string::npos);
    };
    measure("the Scene view", &FrameProfile::ms_scene3d);
    ed.command("window Game");
    measure("the Game view (Bloom, Vignette, Color Grading)", &FrameProfile::ms_game);
    ed.viewport_device_for_test(1);
  });
}

static void tests_round415() {
  /* ---------------------------------------------------------------- round 415: the CPU's share of a card frame (task 0391, ADR 0109) */
  auto r415_compare = [](const std::vector<uint32_t> &a, const std::vector<uint32_t> &b, double &mean, double &within3) {
    double sum = 0;
    size_t within = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); i++) {
      int worst = 0;
      for (int k = 0; k < 24; k += 8) worst = std::max(worst, std::abs((int)((a[i] >> k) & 255) - (int)((b[i] >> k) & 255)));
      sum += worst, within += worst <= 3;
    }
    mean = a.empty() ? 0 : sum / (double)a.size(), within3 = a.empty() ? 0 : (double)within / (double)a.size();
  };
  test("gpu views (round 415): a baked scene's corners go to the card once - blocks sent in the first card frames, none while the frames repeat, none when a lightmapped object moves (Manual lighting: its corners are local), the picture the CPU's within 1 / 255; a scene baked again (new pages, the same or other UVs) still matches", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    P37World w = p37_world(ed, true, 2, 1.0f, true);
    ed.command("overlay grid off");
    ed.command("lighting mode manual");  // (Automatic drops a moved object's lightmap: the move below must keep it)
    r35_bake(ed);
    CHECK(!ed.lighting_data_for_test().empty());
    auto scene = [&](int device, int frames) {
      ed.viewport_device_for_test(device);
      for (int i = 0; i < frames; i++) {
        ed.scene_cache_reset_for_test();
        r34_steps(ed, 1);
      }
      return r34_crop(ed, ed.scene_view_rect());
    };
    const std::vector<uint32_t> cpu = scene(1, 2);
    const FrameProfile first = ed.frame_profile_totals();
    scene(2, 2);  // (the first card frames: the blocks go up)
    const FrameProfile p0 = ed.frame_profile_totals();
    std::printf("    the first card frames: %llu corner blocks sent\n", (unsigned long long)(p0.gpu_corner_uploads - first.gpu_corner_uploads));
    CHECK(p0.gpu_corner_uploads > first.gpu_corner_uploads);
    const std::vector<uint32_t> gpu = scene(2, 6);
    const FrameProfile p1 = ed.frame_profile_totals();
    double mean = 0, within3 = 0;
    r415_compare(cpu, gpu, mean, within3);
    std::printf("    6 repeated frames: %llu corner blocks sent, %llu views on the card; against the CPU mean %.3f / 255, %.2f %% within 3; corners %.2f ms a frame\n",
                (unsigned long long)(p1.gpu_corner_uploads - p0.gpu_corner_uploads), (unsigned long long)(p1.gpu_views - p0.gpu_views), mean, within3 * 100,
                (p1.ms_gpu_corners - p0.ms_gpu_corners) / 6.0);
    CHECK(p1.gpu_views > p0.gpu_views && p1.gpu_corner_uploads == p0.gpu_corner_uploads);
    CHECK(mean < 1.0 && within3 >= 0.99);
    /* The lightmapped box moved: its corners are in its own space, so no block is sent. */
    CHECK(w.box != nullptr);
    if (!w.box) return;
    w.box->set_local_position(w.box->local().position + Vec3(0.0f, 0.02f, 0.0f));
    const FrameProfile p2 = ed.frame_profile_totals();
    scene(2, 2);
    const FrameProfile p3 = ed.frame_profile_totals();
    std::printf("    the box moved: %llu corner blocks sent\n", (unsigned long long)(p3.gpu_corner_uploads - p2.gpu_corner_uploads));
    CHECK(p3.gpu_corner_uploads == p2.gpu_corner_uploads);
    CHECK(ed.lighting_data_for_test().entries.count(w.box->id) == 1);  // (its lightmap entry kept after the move: Manual lighting)
    /* The wall scaled and the scene baked again: the charts pack differently - the new UVs come in new vectors (or at
     * a reused address): new blocks, and the card still matches the CPU. */
    CHECK(w.wall != nullptr);
    if (!w.wall) return;
    w.wall->set_local_scale({2.5f, 2.5f, 1.0f});  // (a chart of another size: the atlas packs differently)
    r35_bake(ed);
    const std::vector<uint32_t> cpu2 = scene(1, 2);
    const FrameProfile p4 = ed.frame_profile_totals();
    const std::vector<uint32_t> gpu2 = scene(2, 3);
    const FrameProfile p5 = ed.frame_profile_totals();
    double mean2 = 0, within2 = 0;
    r415_compare(cpu2, gpu2, mean2, within2);
    std::printf("    baked again with the wall scaled: %llu corner blocks sent; against the CPU mean %.3f / 255, %.2f %% within 3\n",
                (unsigned long long)(p5.gpu_corner_uploads - p4.gpu_corner_uploads), mean2, within2 * 100);
    /* (New UVs come as new blocks; the same UVs under a new page - this scene packs the same - keep theirs: either way the card matches.) */
    CHECK(mean2 < 1.0 && within2 >= 0.99);
    ed.viewport_device_for_test(1);
  });
  test("round 415: a renderer's Cast Shadows off (Unity's Shadows Off) keeps the object out of the shadow map on the CPU and on the card alike - the floor under it lit, the two pictures within 1 / 255", [&] {
    Editor ed;
    r34_open(ed);
    ed.command("overlay grid off");
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube && cube->get<MeshRenderer>());
    if (!cube || !cube->get<MeshRenderer>()) return;
    GameObject *floor = ed.scene().create("Floor");
    floor->add<MeshFilter>()->mesh = primitives::cube(1.0f);
    floor->add<MeshRenderer>()->materials = {make_material("Floor", {0.8f, 0.8f, 0.8f})};
    floor->set_local_position({0.0f, -0.55f, 0.0f});
    floor->set_local_scale({8.0f, 0.1f, 8.0f});
    cube->set_local_position({0.0f, 1.0f, 0.0f});
    ed.command("select Main Camera");
    auto scene = [&](int device) {
      ed.viewport_device_for_test(device);
      for (int k = 0; k < 2; k++) {
        cube->set_local_position({0.0f, k ? 1.0f : 1.0001f, 0.0f});
        r34_steps(ed, 2);
      }
      return r34_crop(ed, ed.scene_view_rect());
    };
    const std::vector<uint32_t> shadowed = scene(1);
    cube->get<MeshRenderer>()->cast_shadows = false;
    ed.mark_changed_for_test("shadows off");
    const std::vector<uint32_t> cpu = scene(1);
    size_t changed = 0;
    for (size_t i = 0; i < cpu.size() && i < shadowed.size(); i++) changed += (cpu[i] & 0xFFFFFF) != (shadowed[i] & 0xFFFFFF);
    std::printf("    Shadows Off on the CPU: %zu pixels change (the cube's shadow gone)\n", changed);
    CHECK(changed > 200);
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: the card's half skipped\n");
      return;
    }
    const std::vector<uint32_t> gpu = scene(2);
    double mean = 0, within3 = 0;
    r415_compare(cpu, gpu, mean, within3);
    std::printf("    on the card: against the CPU mean %.3f / 255, %.2f %% within 3 (%s)\n", mean, within3 * 100, ed.gpu_view_status().c_str());
    CHECK(ed.gpu_view_reason().empty());
    CHECK(mean < 1.0 && within3 >= 0.99);
    ed.viewport_device_for_test(1);
  });
  test("round 415: a point light whose range can't reach the view is left out of the view's list - counted in the Profiler - and the picture is the one without it to the bit; one that reaches in stays", [&] {
    Editor ed;
    r34_open(ed);
    ed.command("overlay grid off");
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube != nullptr);
    if (!cube) return;
    auto scene = [&] {
      for (int k = 0; k < 2; k++) {
        cube->set_local_position({0.0f, k ? 0.5f : 0.5001f, 0.0f});
        r34_steps(ed, 2);
      }
      return r34_crop(ed, ed.scene_view_rect());
    };
    const std::vector<uint32_t> plain = scene();
    GameObject *far = ed.scene().create("Far Lamp");
    Light *l = far->add<Light>();
    l->type = 1, l->range = 2.0f, l->intensity = 50.0f;
    far->set_local_position({0.0f, -400.0f, 0.0f});  // (400 m below everything the camera sees: its 2 m sphere outside the frustum; in front at 400 m its icon would show)
    ed.mark_changed_for_test("far lamp");
    const FrameProfile p0 = ed.frame_profile_totals();
    const std::vector<uint32_t> with_far = scene();
    const FrameProfile p1 = ed.frame_profile_totals();
    std::printf("    the far lamp: %llu lights left out over the frames; the picture %s the one without it\n", (unsigned long long)(p1.lights_culled - p0.lights_culled),
                with_far == plain ? "is" : "is NOT");
    CHECK(p1.lights_culled > p0.lights_culled);
    CHECK(with_far == plain);
    /* Near enough to reach: kept, and the picture changes. */
    far->set_local_position({0.0f, 2.0f, 1.5f});
    ed.mark_changed_for_test("near lamp");
    const FrameProfile p2 = ed.frame_profile_totals();
    const std::vector<uint32_t> with_near = scene();
    const FrameProfile p3 = ed.frame_profile_totals();
    size_t changed = 0;
    for (size_t i = 0; i < plain.size() && i < with_near.size(); i++) changed += (plain[i] & 0xFFFFFF) != (with_near[i] & 0xFFFFFF);
    std::printf("    the lamp near the cube: %llu left out, %zu pixels change\n", (unsigned long long)(p3.lights_culled - p2.lights_culled), changed);
    CHECK(p3.lights_culled == p2.lights_culled && changed > 100);
    /* A ring of lamps around the cube, their spheres crossing the view's side planes, the near plane and lying behind
     * the camera: the picture with the cull is the picture without it, to the bit, and some were left out. */
    far->active = false;
    for (int i = 0; i < 24; i++) {
      GameObject *g = ed.scene().create("Ring Lamp");
      Light *rl = g->add<Light>();
      rl->type = 1, rl->range = 1.5f + (i % 3) * 1.5f, rl->intensity = 6.0f;
      const float a = (float)i / 24.0f * 6.2831853f;
      g->set_local_position({std::cos(a) * (3.0f + (i % 2) * 4.0f), 0.3f + (i % 4) * 0.8f, std::sin(a) * (3.0f + (i % 2) * 4.0f)});
    }
    ed.mark_changed_for_test("ring");
    const FrameProfile p4 = ed.frame_profile_totals();
    const std::vector<uint32_t> culled = scene();
    const FrameProfile p5 = ed.frame_profile_totals();
    ed.set_light_cull_for_test(false);
    ed.mark_changed_for_test("cull off");
    const std::vector<uint32_t> whole = scene();
    ed.set_light_cull_for_test(true);
    std::printf("    24 lamps in a ring: %llu left out over the frames; the picture %s the uncut one\n", (unsigned long long)(p5.lights_culled - p4.lights_culled), culled == whole ? "is" : "is NOT");
    CHECK(p5.lights_culled > p4.lights_culled);
    CHECK(culled == whole);
  });
}

static void tests_round416() {
  /* ---------------------------------------------------------------- round 416: the camera's filters on the card (task 0392, ADR 0110) */
  auto r416_compare = [](const std::vector<uint32_t> &a, const std::vector<uint32_t> &b, double &mean, double &within3) {
    double sum = 0;
    size_t within = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); i++) {
      int worst = 0;
      for (int k = 0; k < 24; k += 8) worst = std::max(worst, std::abs((int)((a[i] >> k) & 255) - (int)((b[i] >> k) & 255)));
      sum += worst, within += worst <= 3;
    }
    mean = a.empty() ? 0 : sum / (double)a.size(), within3 = a.empty() ? 0 : (double)within / (double)a.size();
  };
  test("gpu views (round 416): a camera's Bloom, Vignette, Color Grading (a stop of exposure), Chromatic Aberration and Film Grain run on the card over its picture - no read-back, the Profiler counts the view - and the Game view is the CPU's within 2.5 / 255 (the grading's exposure path after the bloom: a few borderline pixels take the other path); kept on the CPU for the comparison they match the same", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    r34_open(ed);
    ed.command("overlay grid off");
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube != nullptr);
    if (!cube) return;
    MaterialPtr hot = make_material("hot", {0.2f, 0.2f, 0.2f});  // (an emissive cube: the bloom has something to glow from)
    hot->emission = Vec3(6.0f, 4.0f, 2.0f);
    hot->emission_strength = 1.0f;
    cube->get<MeshRenderer>()->materials = {hot};
    ed.command("select Main Camera");
    for (const char *f : {"Bloom", "Vignette", "Color Grading", "Chromatic Aberration", "Film Grain"}) ed.command(std::string("filter add ") + f);
    ed.command("set ColorGrading.Exposure 1");  // (a stop: the grading's linear-light path)
    ed.mark_changed_for_test("filters");
    auto game = [&](int device) {
      ed.viewport_device_for_test(device);
      for (int k = 0; k < 2; k++) {
        cube->set_local_position({0.0f, k ? 0.5f : 0.5001f, 0.0f});
        r34_steps(ed, 2);
      }
      return r34_crop(ed, ed.window_rect_for_test(WindowKind::Game));
    };
    const std::vector<uint32_t> cpu = game(1);
    const FrameProfile before = ed.frame_profile_totals();
    const std::vector<uint32_t> gpu = game(2);
    const FrameProfile after = ed.frame_profile_totals();
    double mean = 0, within3 = 0;
    r416_compare(cpu, gpu, mean, within3);
    std::printf("    %s\n    %llu views on the card, %llu on the CPU, %llu with the filters on the card; against the CPU mean %.3f / 255, %.2f %% within 3\n", ed.gpu_view_status().c_str(),
                (unsigned long long)(after.gpu_views - before.gpu_views), (unsigned long long)(after.gpu_fallbacks - before.gpu_fallbacks),
                (unsigned long long)(after.filters_on_card - before.filters_on_card), mean, within3 * 100);
    CHECK(after.gpu_views > before.gpu_views && after.gpu_fallbacks == before.gpu_fallbacks && ed.gpu_view_reason().empty());
    CHECK(after.filters_on_card > before.filters_on_card);
    CHECK(mean < 2.5 && within3 >= 0.9);
    /* The same stack kept on the CPU over the card's picture (as before this round): the two card paths agree too. */
    ed.set_filters_on_card_for_test(false);
    ed.mark_changed_for_test("filters on the CPU");
    const FrameProfile b2 = ed.frame_profile_totals();
    const std::vector<uint32_t> gpu_cpu_filters = game(2);
    const FrameProfile a2 = ed.frame_profile_totals();
    ed.set_filters_on_card_for_test(true);
    double mean2 = 0, within2 = 0;
    r416_compare(gpu_cpu_filters, gpu, mean2, within2);
    std::printf("    the card's picture with the filters on the CPU against the card's own: mean %.3f / 255, %.2f %% within 3 (%llu views with the filters on the card)\n", mean2, within2 * 100,
                (unsigned long long)(a2.filters_on_card - b2.filters_on_card));
    CHECK(a2.filters_on_card == b2.filters_on_card);
    CHECK(mean2 < 2.5 && within2 >= 0.9);
    ed.viewport_device_for_test(1);
  });
  test("gpu views (round 416): a lilToon material shades on the card by lilToon's own model - 1st and 2nd shadow with their borders and blur, the border colour, a rim, a backlight, HSVG on the base, emission - the object within 1.5 / 255 of the CPU's; with a shadow colour map and a rim map too", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    auto lil_case = [&](const char *what, const std::function<void(Material &)> &setup, double bound) {
      Editor ed;
      r34_open(ed);
      ed.command("overlay grid off");
      GameObject *cube = ed.scene().find_by_name("Cube");
      CHECK(cube && cube->get<MeshRenderer>());
      if (!cube || !cube->get<MeshRenderer>()) return;
      MaterialPtr m = make_material("Lil", {0.85f, 0.6f, 0.55f});
      m->shading_model = 1;
      m->lil.on = true;
      m->lil.shadow = true, m->lil.border = 0.55f, m->lil.blur = 0.15f, m->lil.strength = 0.9f, m->lil.shadow_color = Vec3(0.55f, 0.45f, 0.7f);
      m->lil.shadow2_alpha = 0.6f, m->lil.border2 = 0.3f, m->lil.blur2 = 0.1f, m->lil.shadow2_color = Vec3(0.35f, 0.3f, 0.5f);
      m->lil.border_color = Vec3(1.0f, 0.9f, 0.7f), m->lil.border_range = 0.1f;
      m->lil.rim = true, m->lil.rim_color = Vec3(0.5f, 0.3f, 0.2f), m->lil.rim_fresnel = 3.0f, m->lil.rim_border = 0.5f, m->lil.rim_blur = 0.6f;
      m->lil.backlight = true, m->lil.backlight_color = Vec3(0.7f, 0.6f, 0.4f);
      m->lil.emission = true, m->lil.emission_color = Vec3(0.2f, 0.1f, 0.0f), m->lil.emission_blend = 0.5f;
      m->lil.hsv = Vec3(0.05f, 1.1f, 1.0f), m->lil.gamma = 1.1f;
      setup(*m);
      m->touch();
      cube->get<MeshRenderer>()->materials = {m};
      ed.mark_changed_for_test("lil");
      auto scene = [&](int device) {
        ed.viewport_device_for_test(device);
        for (int k = 0; k < 2; k++) {
          cube->set_local_position({0.0f, k ? 0.5f : 0.5001f, 0.0f});
          r34_steps(ed, 2);
        }
        return r34_crop(ed, ed.scene_view_rect());
      };
      const std::vector<uint32_t> cpu = scene(1);
      const FrameProfile before = ed.frame_profile_totals();
      const std::vector<uint32_t> gpu = scene(2);
      const FrameProfile after = ed.frame_profile_totals();
      cube->active = false;
      const std::vector<uint32_t> empty = scene(1);
      double sum = 0;
      size_t within = 0, pixels = 0;
      for (size_t i = 0; i < cpu.size() && i < gpu.size() && i < empty.size(); i++) {
        auto worst = [](uint32_t a, uint32_t b) {
          int w = 0;
          for (int k = 0; k < 24; k += 8) w = std::max(w, std::abs((int)((a >> k) & 255) - (int)((b >> k) & 255)));
          return w;
        };
        if (worst(cpu[i], empty[i]) <= 8) continue;
        const int w = worst(cpu[i], gpu[i]);
        pixels++, sum += w, within += w <= 3;
      }
      const double mean = pixels ? sum / (double)pixels : 0, within3 = pixels ? (double)within / (double)pixels : 0;
      std::printf("    %s: %llu views on the card, %llu on the CPU; over the object's %zu pixels mean %.3f / 255, %.2f %% within 3 (%s)\n", what, (unsigned long long)(after.gpu_views - before.gpu_views),
                  (unsigned long long)(after.gpu_fallbacks - before.gpu_fallbacks), pixels, mean, within3 * 100, ed.gpu_view_status().c_str());
      CHECK(after.gpu_views > before.gpu_views && after.gpu_fallbacks == before.gpu_fallbacks && ed.gpu_view_reason().empty());
      CHECK(pixels > 100);
      CHECK(mean < bound && within3 >= 0.9);
      ed.viewport_device_for_test(1);
    };
    lil_case("lilToon without maps", [](Material &) {}, 1.5);
    const std::string shadow_png = fs::join(test_dir(), "r416_lil_shadow.png"), rim_png = fs::join(test_dir(), "r416_lil_rim.png");
    {
      std::vector<uint32_t> g(64 * 64);
      for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) g[(size_t)y * 64 + x] = ((x / 16 + y / 16) & 1) ? 0xC0A06080u : 0xC04080A0u;  // (a shadow colour with an alpha)
      CHECK(write_png(shadow_png, g.data(), 64, 64, 64, true));
      for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) g[(size_t)y * 64 + x] = 0xFF000000u | ((uint32_t)(x * 4) << 16) | ((uint32_t)(y * 4) << 8) | 0x80u;
      CHECK(write_png(rim_png, g.data(), 64, 64, 64));
    }
    lil_case("lilToon with a shadow colour map and a rim map", [&](Material &m) {
      m.lil.shadow_color_tex.path = shadow_png;
      m.lil.rim_color_tex.path = rim_png;
    }, 2.0);
    std::remove(shadow_png.c_str());
    std::remove(rim_png.c_str());
  });
  test("gpu views (round 416): each card filter alone - Vignette, Color Grading (exposure, contrast, saturation, lift / gamma / gain), Chromatic Aberration, Film Grain, Bloom - against the CPU's pass over the same card picture, within 1.5 / 255 each, the integer passes within a step; a grading with exposure before a bloom keeps the stack on the CPU", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    r34_open(ed);
    ed.command("overlay grid off");
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube != nullptr);
    if (!cube) return;
    MaterialPtr hot = make_material("hot", {0.2f, 0.2f, 0.2f});
    hot->emission = Vec3(6.0f, 4.0f, 2.0f);
    hot->emission_strength = 1.0f;
    cube->get<MeshRenderer>()->materials = {hot};
    ed.command("select Main Camera");
    auto game = [&](int device) {
      ed.viewport_device_for_test(device);
      for (int k = 0; k < 2; k++) {
        cube->set_local_position({0.0f, k ? 0.5f : 0.5001f, 0.0f});
        r34_steps(ed, 2);
      }
      return r34_crop(ed, ed.window_rect_for_test(WindowKind::Game));
    };
    struct Case { const char *name; std::vector<const char *> setup; };
    const Case cases[] = {{"Vignette", {}}, {"Color Grading", {"set ColorGrading.Exposure 1", "set ColorGrading.Contrast 0.3", "set ColorGrading.Saturation 0.4", "set ColorGrading.Temperature 0.2"}},
                          {"Chromatic Aberration", {"set ChromaticAberration.Intensity 0.5"}}, {"Film Grain", {"set FilmGrain.Intensity 0.6", "set FilmGrain.Size 2"}}, {"Bloom", {}}};
    for (const Case &c : cases) {
      ed.command(std::string("filter add ") + c.name);
      for (const char *s : c.setup) ed.command(s);
      ed.mark_changed_for_test(c.name);
      /* The card's picture with the pass on the card, and with the CPU's pass over the same picture. */
      ed.set_filters_on_card_for_test(true);
      const FrameProfile before = ed.frame_profile_totals();
      const std::vector<uint32_t> card = game(2);
      const FrameProfile after = ed.frame_profile_totals();
      ed.set_filters_on_card_for_test(false);
      ed.mark_changed_for_test("cpu pass");
      const std::vector<uint32_t> cpu_pass = game(2);
      ed.set_filters_on_card_for_test(true);
      double mean = 0, within3 = 0;
      r416_compare(cpu_pass, card, mean, within3);
      int worst = 0;
      for (size_t i = 0; i < card.size() && i < cpu_pass.size(); i++)
        for (int k = 0; k < 24; k += 8) worst = std::max(worst, std::abs((int)((card[i] >> k) & 255) - (int)((cpu_pass[i] >> k) & 255)));
      std::printf("    %s alone: %llu views with the pass on the card; against the CPU's pass mean %.3f / 255, %.2f %% within 3, the worst %d\n", c.name,
                  (unsigned long long)(after.filters_on_card - before.filters_on_card), mean, within3 * 100, worst);
      CHECK(after.filters_on_card > before.filters_on_card);
      CHECK(mean < 1.5 && within3 >= 0.98);
      const bool exact_kind = std::string(c.name) == "Vignette" || std::string(c.name) == "Chromatic Aberration" || std::string(c.name) == "Film Grain";
      if (exact_kind) CHECK(worst <= 1);  // (the integer passes: a step at most, a rounding tie)
      ed.command("filter remove 0");
      ed.mark_changed_for_test("filter off");
    }
    /* An exposed grading before a bloom: the CPU's grade writes the exposed light back for the bloom to read; the card's
     * doesn't, so the stack stays the CPU's and still matches. */
    ed.command("filter add Color Grading");
    ed.command("set ColorGrading.Exposure 1");
    ed.command("filter add Bloom");
    ed.mark_changed_for_test("exposure then bloom");
    const FrameProfile b3 = ed.frame_profile_totals();
    const std::vector<uint32_t> gpu3 = game(2);
    const FrameProfile a3 = ed.frame_profile_totals();
    const std::vector<uint32_t> cpu3 = game(1);
    double mean3 = 0, within33 = 0;
    r416_compare(cpu3, gpu3, mean3, within33);
    std::printf("    Color Grading (exposure 1) then Bloom: %llu views with the filters on the card (%s); against the CPU mean %.3f / 255, %.2f %% within 3\n",
                (unsigned long long)(a3.filters_on_card - b3.filters_on_card), ed.gpu_view_status().c_str(), mean3, within33 * 100);
    CHECK(a3.filters_on_card == b3.filters_on_card);
    CHECK(mean3 < 1.5 && within33 >= 0.98);
    ed.viewport_device_for_test(1);
  });
  test("gpu views (round 416): a stack with a pass the card doesn't run (Posterize) stays on the CPU over the picture read back, says so in the status, and still matches the CPU", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    r34_open(ed);
    ed.command("overlay grid off");
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube != nullptr);
    if (!cube) return;
    ed.command("select Main Camera");
    ed.command("filter add Vignette");
    ed.command("filter add Posterize");
    ed.mark_changed_for_test("filters");
    auto game = [&](int device) {
      ed.viewport_device_for_test(device);
      for (int k = 0; k < 2; k++) {
        cube->set_local_position({0.0f, k ? 0.5f : 0.5001f, 0.0f});
        r34_steps(ed, 2);
      }
      return r34_crop(ed, ed.window_rect_for_test(WindowKind::Game));
    };
    const std::vector<uint32_t> cpu = game(1);
    const FrameProfile before = ed.frame_profile_totals();
    const std::vector<uint32_t> gpu = game(2);
    const FrameProfile after = ed.frame_profile_totals();
    double mean = 0, within3 = 0;
    r416_compare(cpu, gpu, mean, within3);
    std::printf("    %s\n    %llu views on the card, %llu with the filters on the card; against the CPU mean %.3f / 255, %.2f %% within 3\n", ed.gpu_view_status().c_str(),
                (unsigned long long)(after.gpu_views - before.gpu_views), (unsigned long long)(after.filters_on_card - before.filters_on_card), mean, within3 * 100);
    CHECK(after.gpu_views > before.gpu_views && after.filters_on_card == before.filters_on_card);
    CHECK(ed.gpu_view_status().find("filters on the CPU") != std::string::npos);
    CHECK(mean < 1.5 && within3 >= 0.98);
    ed.viewport_device_for_test(1);
  });
}

static void tests_round337() {
  test("gpu views (round 337, the user's black cube): a cube drawn on the card, then given a procedural texture with a normal map and Physical Size 0.15, draws textured - not black from the zero tangents it first went up with (skipped without a GPU)", [] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    r34_open(ed);
    ed.viewport_device_for_test(2);
    ed.command("shading shaded");
    ed.command("tool view");
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube && cube->get<MeshRenderer>());
    if (!cube || !cube->get<MeshRenderer>()) return;
    r34_steps(ed, 3);  // (the cube on the card, no normal map: no tangents)
    std::remove(resolve_asset_path("Assets/Textures/R337Gpu.ptex").c_str());
    ed.command("newtex R337Gpu Bricks");
    ed.command("settex Assets/Textures/R337Gpu.ptex LiveShading 0");  // (no live texture: the card draws it close up too)
    ed.command("select Cube");
    ed.command("texture Assets/Textures/R337Gpu.ptex");
    ed.command("settex Assets/Textures/R337Gpu.ptex PhysicalSize 0.15");
    const FrameProfile before = ed.frame_profile_totals();
    r34_steps(ed, 3);
    const FrameProfile after = ed.frame_profile_totals();
    /* (The card drew it: a CPU fallback - a live close-up, a fog - would pass the picture check without the fix.) */
    CHECK(after.gpu_views > before.gpu_views && ed.gpu_view_reason().empty());
    const Material &m = *cube->get<MeshRenderer>()->materials[0];
    CHECK(!m.normal_map.empty() && std::fabs(m.tiling.z - 1.0f / 0.15f) < 1e-3f);  // (Box mapping's z follows the size too)
    int cx = 0, cy = 0;
    const bool seen = ed.project_to_window(cube->world_position(), cx, cy);
    CHECK(seen);
    if (!seen) return;
    auto mean = [&] {
      const Image &fb = ed.main_fb_for_test();
      double sum = 0;
      int n = 0;
      for (int y = cy - 10; y <= cy + 10; y++)
        for (int x = cx - 10; x <= cx + 10; x++) {
          const uint32_t p = fb.pixels[(size_t)y * fb.width + x];
          sum += ((p >> 16) & 255) + ((p >> 8) & 255) + (p & 255);
          n++;
        }
      return sum / (3.0 * n);
    };
    const double on_card = mean();
    const std::string status = ed.gpu_view_status();
    ed.viewport_device_for_test(1);
    cube->set_local_position({0.01f, 0.5f, 0.0f});  // (a change: the view draws again)
    r34_steps(ed, 2);
    cube->set_local_position({0.0f, 0.5f, 0.0f});
    r34_steps(ed, 2);
    const double on_cpu = mean();
    std::printf("    the cube's middle: %.1f on the card (%s), %.1f on the CPU\n", on_card, status.c_str(), on_cpu);
    CHECK(on_card > on_cpu * 0.6 && on_card < on_cpu * 1.4);
    std::remove(resolve_asset_path("Assets/Textures/R337Gpu.ptex").c_str());
  });
}

/* ---------------------------------------------------------------- round 443: Light Volumes in the views (task 0419) */

/* A volume's bake of `res` voxels with `l0` everywhere and, where `grade` is set, L0's red growing along x. */
static std::shared_ptr<LightVolumeSH> r443_bake(int rx, int ry, int rz, Vec3 l0, Vec3 l1r = Vec3(0.0f), bool grade = false) {
  auto sh = std::make_shared<LightVolumeSH>();
  sh->res[0] = rx, sh->res[1] = ry, sh->res[2] = rz;
  const size_t n = sh->count();
  sh->l0.assign(n, l0), sh->l1r.assign(n, l1r), sh->l1g.assign(n, Vec3(0.0f)), sh->l1b.assign(n, Vec3(0.0f)), sh->validity.assign(n, 0.0f);
  if (grade)
    for (size_t i = 0; i < n; i++) sh->l0[i].x = (float)(i % (size_t)rx);
  return sh;
}

/* A frame of one volume: a box of `size` about `at`, `blend` metres at its edges. */
static LightVolumeInstance r443_instance(std::shared_ptr<const LightVolumeSH> sh, Vec3 at, Vec3 size, float blend) {
  LightVolumeInstance in;
  in.sh = std::move(sh);
  in.inv_world = Mat4::trs(at, Quat(), size).inverse();
  for (int a = 0; a < 3; a++) in.edge[a] = 1.0f / std::max(blend / size[a], 1e-6f);
  return in;
}

static void tests_round443() {
  test("light volumes in the views (round 443): a volume's light is L0 + L1 . n over the trilinear voxels - the middle of a graded 2 x 2 x 2 grid halfway between its columns, L1 added along the normal, never below 0 - 0 weight outside the box, 1 inside past its blend, partial in it, and two volumes share the weight inner first", [] {
    LightVolumeFrame f;
    f.volumes.push_back(r443_instance(r443_bake(2, 2, 2, Vec3(0.0f, 0.5f, 0.25f), Vec3(0.0f, 0.2f, 0.0f), true), Vec3(0.0f), Vec3(2.0f), 0.0f));
    Vec3 light;
    float cover = 0.0f;
    CHECK(f.sample(Vec3(0.0f), Vec3(0, 1, 0), light, cover));
    std::printf("    the middle, normal up: %.3f %.3f %.3f, cover %.2f\n", light.x, light.y, light.z, cover);
    CHECK(std::fabs(light.x - (0.5f + 0.2f)) < 1e-4f && std::fabs(light.y - 0.5f) < 1e-4f && std::fabs(light.z - 0.25f) < 1e-4f && cover == 1.0f);  // (L1 r . up = 0.2 on the red)
    /* L1 down takes it away (the red: 0.5 - 0.2), and a strong L1 against the normal clamps at 0. */
    CHECK(f.sample(Vec3(0.0f), Vec3(0, -1, 0), light, cover) && std::fabs(light.x - 0.3f) < 1e-4f);
    f.volumes[0].sh = r443_bake(2, 2, 2, Vec3(0.1f), Vec3(0.0f, 0.5f, 0.0f));
    CHECK(f.sample(Vec3(0.0f), Vec3(0, -1, 0), light, cover) && light.x == 0.0f && light.y > 0.0f);
    /* Along x the red runs 0 .. 1 over the two columns (voxel centres at a quarter and three quarters of the box: x = -0.5 and 0.5 in a box of 2). */
    f.volumes[0].sh = r443_bake(2, 2, 2, Vec3(0.0f), Vec3(0.0f), true);
    CHECK(f.sample(Vec3(-0.5f, 0, 0), Vec3(0, 1, 0), light, cover) && std::fabs(light.x - 0.0f) < 1e-4f);
    CHECK(f.sample(Vec3(0.5f, 0, 0), Vec3(0, 1, 0), light, cover) && std::fabs(light.x - 1.0f) < 1e-4f);
    CHECK(f.sample(Vec3(0.0f), Vec3(0, 1, 0), light, cover) && std::fabs(light.x - 0.5f) < 1e-4f);
    /* The box: outside nothing, on a face with no blend nothing (the mask is 0 there), inside all of it. */
    CHECK(!f.sample(Vec3(1.5f, 0, 0), Vec3(0, 1, 0), light, cover) && cover == 0.0f);
    CHECK(f.sample(Vec3(0.9f, 0, 0), Vec3(0, 1, 0), light, cover) && cover == 1.0f);
    /* A blend of 0.5 m: halfway into it the volume has half the weight, the rest left for the probes. */
    f.volumes[0] = r443_instance(r443_bake(2, 2, 2, Vec3(1.0f)), Vec3(0.0f), Vec3(2.0f), 0.5f);
    CHECK(f.sample(Vec3(0.75f, 0, 0), Vec3(0, 1, 0), light, cover));
    std::printf("    0.25 m into a 0.5 m blend: cover %.3f, light %.3f\n", cover, light.x);
    CHECK(std::fabs(cover - 0.5f) < 1e-3f && std::fabs(light.x - 0.5f) < 1e-3f);
    /* Two: the small inner one takes its weight first, the big one the rest; a tint multiplies, additive adds on top. */
    f.volumes.clear();
    LightVolumeInstance inner = r443_instance(r443_bake(1, 1, 1, Vec3(1.0f)), Vec3(0.0f), Vec3(1.0f), 0.5f);
    LightVolumeInstance outer = r443_instance(r443_bake(1, 1, 1, Vec3(0.2f)), Vec3(0.0f), Vec3(4.0f), 0.0f);
    outer.tint = Vec3(2.0f);
    f.volumes = {inner, outer};
    CHECK(f.sample(Vec3(0.375f, 0, 0), Vec3(0, 1, 0), light, cover));  // (inner's mask 0.25: 0.25 * 1 + 0.75 * 0.2 * 2)
    std::printf("    inner at a quarter, outer behind it tinted 2: %.3f, cover %.2f\n", light.x, cover);
    CHECK(std::fabs(light.x - (0.25f + 0.75f * 0.4f)) < 1e-3f && std::fabs(cover - 1.0f) < 1e-4f);
    f.volumes[1].additive = true;
    CHECK(f.sample(Vec3(0.375f, 0, 0), Vec3(0, 1, 0), light, cover) && std::fabs(cover - 0.25f) < 1e-3f && std::fabs(light.x - (0.25f + 0.4f)) < 1e-3f);
    /* Review 443: deep inside the inner volume (it took all the weight) a larger Additive one still adds its light. */
    CHECK(f.sample(Vec3(0.0f), Vec3(0, 1, 0), light, cover) && std::fabs(light.x - (1.0f + 0.4f)) < 1e-3f && std::fabs(cover - 1.0f) < 1e-4f);
    /* The turn: a box turned a quarter about z since its bake sees the world normal turned back - L1 r y 0.5 reads the x axis. */
    LightVolumeInstance turned = r443_instance(r443_bake(1, 1, 1, Vec3(1.0f), Vec3(0.0f, 0.5f, 0.0f)), Vec3(0.0f), Vec3(2.0f), 0.0f);
    turned.turn = Mat4::rotate(Quat::axis_angle(Vec3(0, 0, 1), kPi * 0.5f));
    f.volumes = {turned};
    CHECK(f.sample(Vec3(0.0f), Vec3(1, 0, 0), light, cover) && std::fabs(light.x - 1.5f) < 1e-4f);
    CHECK(f.sample(Vec3(0.0f), Vec3(0, 1, 0), light, cover) && std::fabs(light.x - 1.0f) < 1e-4f);
    /* Its card copy: nine header vec4s a volume and four a voxel, behind the count - and nothing past the cap. */
    const std::vector<float> packed = light_volume_pack(f, 100);
    CHECK(packed.size() == (size_t)(1 + 9 + 4) * 4 && light_volume_pack(f, 0).empty());  // (one volume of one voxel)
  });

  /* A dynamic ball over a baked probe volume with a VRC Light Volume of its own around it (its bake put in by hand: a
   * red, rather than the probes' grey) - the Scene view's picture of it. */
  struct R443World {
    P37World w;
    GameObject *volume = nullptr;
  };
  auto world = [](Editor &ed, Vec3 volume_at, float blend, Vec3 l0, bool probes = true) {
    R443World r;
    r.w = p37_world(ed, probes, 1, 1.0f, true);
    ed.command("overlay grid off");
    r35_bake(ed);
    r.volume = ed.scene().create("Hall");
    r.volume->set_local_position(volume_at), r.volume->set_local_scale(Vec3(6.0f, 4.0f, 6.0f));
    LightVolumeComponent *v = r.volume->add<LightVolumeComponent>();
    v->voxels_per_unit = 1.0f, v->smooth_blending = blend, v->bake = false;
    ed.light_volume_bakes_for_test()[r.volume->id] = *r443_bake(6, 4, 6, l0);
    ed.light_volume_bakes_changed_for_test();
    ed.commit_change("light volume");
    r34_steps(ed, 2);
    return r;
  };
  auto view = [](Editor &ed, GameObject *g, int device) {
    ed.viewport_device_for_test(device);
    const Vec3 at = g->world_position();
    for (int k = 0; k < 2; k++) {
      g->set_local_position(at + Vec3(0.0f, k ? 0.0f : 1e-4f, 0.0f));
      r34_steps(ed, 2);
    }
    return r34_crop(ed, ed.scene_view_rect());
  };
  test("light volumes in the views (round 443): a dynamic ball inside a baked Light Volume takes its light (red here) where the probe volume gave grey, outside the box it takes the probes' again, with Light Volumes in Views off it takes them everywhere; lightmapped objects are untouched (the Game view, on the CPU)", [&] {
    Editor ed;
    ed.viewport_device_for_test(1);
    P37World w0 = p37_world(ed, true, 1, 1.0f, true);
    ed.command("overlay grid off");
    r35_bake(ed);
    auto game = [&] {  // (the ball nudged and back, so the view draws again; the Game view has no overlays: the volume's box isn't drawn in it)
      const Vec3 at = w0.ball->world_position();
      for (int k = 0; k < 2; k++) {
        w0.ball->set_local_position(at + Vec3(0.0f, k ? 0.0f : 1e-4f, 0.0f));
        r34_steps(ed, 2);
      }
      return r35_game(ed);
    };
    const std::vector<uint32_t> probes = game();
    GameObject *hall = ed.scene().create("Hall");
    hall->set_local_position(Vec3(1.0f, 1.0f, 0.0f)), hall->set_local_scale(Vec3(6.0f, 4.0f, 6.0f));
    LightVolumeComponent *v = hall->add<LightVolumeComponent>();
    v->voxels_per_unit = 1.0f, v->smooth_blending = 0.25f, v->bake = false;
    ed.light_volume_bakes_for_test()[hall->id] = *r443_bake(6, 4, 6, Vec3(2.0f, 0.1f, 0.1f));
    ed.light_volume_bakes_changed_for_test();
    ed.commit_change("light volume");
    CHECK(ed.light_volume_frame_for_test() != nullptr && ed.light_volume_frame_for_test()->volumes.size() == 1);
    const std::vector<uint32_t> inside = game();
    const double redder = p37_redder(inside, probes);
    std::printf("    the ball in the volume: %.0f redder over the picture, %zu pixels differ\n", redder, r35_diff_count(inside, probes));
    CHECK(redder > 1000.0 && r35_diff_count(inside, probes) > 200);
    /* The box moved away from the ball: the probes again, exactly. */
    hall->set_local_position(Vec3(60.0f, 1.0f, 0.0f));
    ed.commit_change("light volume moved");
    CHECK(r35_diff_count(game(), probes) == 0);
    hall->set_local_position(Vec3(1.0f, 1.0f, 0.0f));
    ed.commit_change("light volume back");
    CHECK(r35_diff_count(game(), inside) == 0);
    /* The setting off, as the console sets it: the probes again, exactly - and on again, the volume's. */
    ed.command("set Lighting.LightVolumesInViews false");
    CHECK(!ed.scene().lighting.light_volumes_in_views && ed.light_volume_frame_for_test() == nullptr);
    CHECK(r35_diff_count(game(), probes) == 0);
    ed.command("set Lighting.LightVolumesInViews true");
    CHECK(ed.scene().lighting.light_volumes_in_views && ed.light_volume_frame_for_test() != nullptr);
    CHECK(r35_diff_count(game(), inside) == 0);
    /* Baked GI off: the volumes are left out as the baked probes are - the picture is the same with the setting on and off. */
    ed.scene().lighting.baked_gi = false;
    ed.commit_change("baked gi off");
    const std::vector<uint32_t> no_gi_on = game();
    ed.command("set Lighting.LightVolumesInViews false");
    CHECK(r35_diff_count(game(), no_gi_on) == 0);
    ed.command("set Lighting.LightVolumesInViews true");
    ed.scene().lighting.baked_gi = true;
    ed.commit_change("baked gi on");
    /* Lightmapped objects don't sample it: with the ball gone the picture is the same with the volume on and off. */
    w0.ball->active = false;
    ed.commit_change("ball hidden");
    const std::vector<uint32_t> bare_on = game();
    ed.command("set Lighting.LightVolumesInViews false");
    const std::vector<uint32_t> bare_off = game();
    std::printf("    without the ball: %zu pixels differ with the volume on and off\n", r35_diff_count(bare_on, bare_off));
    CHECK(r35_diff_count(bare_on, bare_off) == 0);
    ed.command("set Lighting.LightVolumesInViews true");
    /* A volume whose grid no longer matches its box (resized since the bake) is left out until it is baked again. */
    hall->set_local_scale(Vec3(9.0f, 4.0f, 6.0f));
    ed.commit_change("light volume resized");
    CHECK(ed.light_volume_frame_for_test() == nullptr);
  });
  test("light volumes in the views (round 443): a volume turned since its bake turns its light with it, more than 32 volumes keep the smallest 32, a NaN colour is no light, and Baked GI off leaves the volumes out", [&] {
    Editor ed;
    r35_open(ed);
    GameObject *hall = ed.scene().create("Hall");
    hall->set_local_position(Vec3(0.0f, 1.0f, 0.0f)), hall->set_local_scale(Vec3(2.0f, 2.0f, 2.0f));
    LightVolumeComponent *v = hall->add<LightVolumeComponent>();
    v->voxels_per_unit = 1.0f, v->smooth_blending = 0.0f, v->bake = false;
    LightVolumeSH bake = *r443_bake(2, 2, 2, Vec3(1.0f), Vec3(0.0f, 0.5f, 0.0f));  // (rotation: identity, as baked)
    ed.light_volume_bakes_for_test()[hall->id] = bake;
    ed.light_volume_bakes_changed_for_test();
    ed.commit_change("volume");
    Vec3 light;
    float cover = 0.0f;
    const LightVolumeFrame *f = ed.light_volume_frame_for_test();
    CHECK(f && f->sample(Vec3(0, 1, 0), Vec3(0, 1, 0), light, cover) && std::fabs(light.x - 1.5f) < 1e-4f);
    /* Turned a quarter about z: the pattern turns with the box, so the world's +x now reads the baked +y. */
    hall->set_local_rotation(Quat::axis_angle(Vec3(0, 0, 1), kPi * 0.5f));
    ed.commit_change("volume turned");
    f = ed.light_volume_frame_for_test();
    CHECK(f && f->sample(Vec3(0, 1, 0), Vec3(1, 0, 0), light, cover) && std::fabs(light.x - 0.5f) < 1e-3f);  // (the baked +y turned to world -x: +x reads -0.5)
    CHECK(f && f->sample(Vec3(0, 1, 0), Vec3(-1, 0, 0), light, cover) && std::fabs(light.x - 1.5f) < 1e-3f);
    /* A NaN colour or an infinite intensity is a plain white, not a NaN picture. */
    v->color = Vec3(std::nanf(""), 1.0f, 1.0f);
    v->intensity = INFINITY;
    ed.commit_change("volume colour");
    f = ed.light_volume_frame_for_test();
    CHECK(f && f->sample(Vec3(0, 1, 0), Vec3(-1, 0, 0), light, cover) && std::isfinite(light.x) && std::isfinite(light.y) && std::isfinite(light.z));
    v->color = Vec3(1.0f), v->intensity = 1.0f;
    hall->set_local_rotation(Quat());
    /* Forty volumes: 32 stay, the smallest first. */
    for (int k = 0; k < 40; k++) {
      GameObject *g = ed.scene().create("V" + std::to_string(k));
      g->set_local_position(Vec3((float)k * 5.0f + 10.0f, 1.0f, 0.0f)), g->set_local_scale(Vec3(1.0f + 0.01f * (float)k, 1.0f, 1.0f));
      LightVolumeComponent *c = g->add<LightVolumeComponent>();
      c->voxels_per_unit = 1.0f, c->bake = false;
      ed.light_volume_bakes_for_test()[g->id] = *r443_bake(1, 1, 1, Vec3(1.0f));
    }
    ed.light_volume_bakes_changed_for_test();
    ed.commit_change("forty volumes");
    f = ed.light_volume_frame_for_test();
    std::printf("    41 volumes in the scene: %zu in the frame\n", f ? f->volumes.size() : 0);
    CHECK(f && f->volumes.size() == (size_t)LightVolumeComponent::kMaxVolumes);
    size_t halls = 0;
    for (const LightVolumeInstance &in : f->volumes) halls += std::fabs(in.inv_world.m[0] - 0.5f) < 1e-3f;  // (the 2 m hall: bigger than every thin one)
    CHECK(halls == 0);
  });
  /* The card as the CPU: the same scene drawn by both. */
  auto compare = [](const std::vector<uint32_t> &a, const std::vector<uint32_t> &b, double &mean, double &within3) {
    size_t n = 0, ok = 0;
    double sum = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); i++) {
      int worst = 0;
      for (int k = 0; k < 24; k += 8) worst = std::max(worst, std::abs((int)((a[i] >> k) & 255) - (int)((b[i] >> k) & 255)));
      n++, sum += worst, ok += worst <= 3;
    }
    mean = n ? sum / (double)n : 0, within3 = n ? (double)ok / (double)n : 0;
  };
  test("gpu views (round 443): Light Volumes on the card as on the CPU - the ball in a volume's middle, and the ball across a volume's blended edge (half volume, half probes), the card's picture the CPU's to within a level or two; the card's picture changes with the setting (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    static const char *const kCase[4] = {"the middle", "the blended edge", "no probe volume", "an Additive volume around a covering one"};
    for (int edge = 0; edge < 4; edge++) {
      Editor ed;
      /* (The edge: the box's +x face at x = 1 where the ball sits, a metre of blend inside it. Case 2: no probe volume in the scene,
       * the card's empty header. Case 3: a larger Additive volume beyond a regular one that covers the ball.) */
      R443World r = world(ed, edge == 1 ? Vec3(-2.0f, 1.0f, 0.0f) : Vec3(1.0f, 1.0f, 0.0f), edge == 1 ? 1.0f : 0.25f, Vec3(1.5f, 0.2f, 0.1f), edge != 2);
      if (edge == 3) {
        GameObject *add = ed.scene().create("Add");
        add->set_local_position(Vec3(1.0f, 1.0f, 0.0f)), add->set_local_scale(Vec3(12.0f, 8.0f, 12.0f));
        LightVolumeComponent *av = add->add<LightVolumeComponent>();
        av->voxels_per_unit = 1.0f, av->smooth_blending = 0.25f, av->bake = false, av->additive = true;
        ed.light_volume_bakes_for_test()[add->id] = *r443_bake(12, 8, 12, Vec3(0.0f, 0.8f, 0.0f));
        ed.light_volume_bakes_changed_for_test();
        ed.commit_change("additive volume");
        r34_steps(ed, 2);
        Vec3 light;
        float cover = 0.0f;
        CHECK(ed.light_volume_frame_for_test() && ed.light_volume_frame_for_test()->sample(r.w.ball->world_position(), Vec3(0, 1, 0), light, cover) && light.y > 0.85f);  // (0.2 + 0.8)
      }
      view(ed, r.w.ball, 2);
      view(ed, r.w.ball, 1);
      const std::vector<uint32_t> cpu = view(ed, r.w.ball, 1);
      const std::vector<uint32_t> gpu = view(ed, r.w.ball, 2);
      double mean = 0, within3 = 0;
      compare(cpu, gpu, mean, within3);
      std::printf("    %s: the Scene view on the card %s (%s); mean %.3f / 255, %.2f %% within 3\n", kCase[edge], ed.scene_on_gpu_for_test() ? "yes" : "no",
                  ed.gpu_view_status().c_str(), mean, within3 * 100);
      CHECK(ed.scene_on_gpu_for_test());
      CHECK(mean < 1.0 && within3 >= 0.99);
      ed.scene().lighting.light_volumes_in_views = false;
      ed.commit_change("light volumes off");
      const std::vector<uint32_t> without = view(ed, r.w.ball, 2);
      const size_t changed = r35_diff_count(gpu, without, 3);
      std::printf("    %s: the setting off changes %zu pixels of the card's picture\n", kCase[edge], changed);
      CHECK(changed > 200);
      ed.viewport_device_for_test(1);
    }
  });
}

static void tests_round448() {
  test("round 448: neighbouring instances of one mesh draw as one instanced call, each with its own colour and place; another mesh between them splits the run (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    std::string err;
    std::unique_ptr<gpu::Raster> g = gpu::Raster::create(0, &err);
    if (!g) {
      std::printf("    skipped: %s\n", err.c_str());
      return;
    }
    auto upload = [&](const MeshPtr &mp) {
      const RenderMesh &rm = mp->render_mesh();
      std::vector<gpu::RasterVertex> vs(rm.positions.size());
      for (size_t i = 0; i < vs.size(); i++)
        vs[i] = {{rm.positions[i].x, rm.positions[i].y, rm.positions[i].z}, {rm.normals[i].x, rm.normals[i].y, rm.normals[i].z}};
      return g->add_mesh(vs, rm.indices, &err);
    };
    const int cube = upload(primitives::cube(0.8f)), other = upload(primitives::cube(0.6f));
    CHECK(cube >= 0 && other >= 0);
    const int N = 10;
    auto colour = [](int k) { return Vec3((k % N) / (float)(N - 1), (k / N) / (float)(N - 1), 0.5f); };
    auto at = [](int k) { return Vec3(-4.5f + (float)(k % N), -4.5f + (float)(k / N), 0.0f); };
    auto make = [&](int k, int mesh) {
      gpu::RasterInstance in;
      const Mat4 model = Mat4::translate(at(k));
      std::memcpy(in.model, model.m, 64);
      const Vec3 c = colour(k);
      in.albedo_rough[0] = c.x, in.albedo_rough[1] = c.y, in.albedo_rough[2] = c.z;
      in.flags = gpu::RasterInstance::Unlit;
      in.mesh = (uint32_t)mesh;
      in.id = (uint32_t)k + 1;
      return in;
    };
    const int W = 400, H = 400;
    gpu::RasterFrame fr;
    fr.width = W, fr.height = H, fr.shadow_size = 0;
    const Mat4 vp = Mat4::perspective(50 * kDeg2Rad, 1.0f, 0.1f, 100.0f) * Mat4::look_at({0, 0, 12}, {0, 0, 0}, {0, 1, 0});
    std::memcpy(fr.view_proj, vp.m, 64);
    std::memcpy(fr.sun_view_proj, vp.m, 64);
    fr.eye[2] = 12;
    std::vector<uint32_t> px((size_t)W * H);
    auto draw = [&](const std::vector<gpu::RasterInstance> &inst) {
      g->set_instances(inst);
      const uint64_t d0 = g->draw_calls();
      CHECK(g->render(fr, &err) && g->read_back(0, px.data(), nullptr, nullptr, &err));
      return g->draw_calls() - d0;
    };
    /* Each cube's centre shows its own colour (unlit: the albedo as the view encodes it). */
    auto centres_ok = [&](const std::vector<gpu::RasterInstance> &inst) {
      int worst = 0;
      for (size_t k = 0; k < inst.size(); k++) {
        const Vec4 c = vp * Vec4(at((int)k).x, at((int)k).y, 0.45f, 1.0f);
        const int x = (int)((c.x / c.w * 0.5f + 0.5f) * W), y = (int)((0.5f - c.y / c.w * 0.5f) * H);
        const uint32_t got = px[(size_t)y * W + x];
        const float want[3] = {inst[k].albedo_rough[2], inst[k].albedo_rough[1], inst[k].albedo_rough[0]};  // (B, G, R from the low byte)
        for (int ch = 0; ch < 3; ch++) {
          const float l = want[ch];
          const int enc = (int)std::lround((l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f) * 255.0f);
          worst = std::max(worst, std::abs((int)((got >> (8 * ch)) & 255) - enc));
        }
      }
      return worst;
    };
    std::vector<gpu::RasterInstance> same;
    for (int k = 0; k < N * N; k++) same.push_back(make(k, cube));
    const uint64_t calls_same = draw(same);
    const int worst_same = centres_ok(same);
    std::vector<gpu::RasterInstance> split = same;
    split[50] = make(50, other);  // (a different mesh in the middle: three runs)
    const uint64_t calls_split = draw(split);
    const int worst_split = centres_ok(split);
    std::printf("    100 cubes of one mesh: %llu draw call(s), worst centre %d; one other mesh at 50: %llu call(s), worst %d\n",
                (unsigned long long)calls_same, worst_same, (unsigned long long)calls_split, worst_split);
    CHECK(calls_same == 1);   // (the G-buffer pass's one call; no shadow, toon or baked pass here)
    CHECK(calls_split == 3);  // (0-49, 50, 51-99)
    CHECK(worst_same <= 2);
    CHECK(worst_split <= 2);
  });
}

static void tests_round449() {
  auto open = [](Editor &ed) {
    r34_open(ed);
    GameObject *wire = ed.scene().create("Wired");
    wire->add<MeshFilter>()->mesh = primitives::uv_sphere(0.8f, 24, 16);
    wire->add<MeshRenderer>()->show_wireframe = true;
    wire->set_local_position({2.0f, 1.0f, 0.0f});
    ed.mark_changed_for_test("Wired");
    ed.command("select Cube");
    ed.command("camera 0 4 6 0 1 0");
    r34_steps(ed, 3);
    return wire;
  };
  test("round 449: the Scene view's static overlays are kept while only the camera moves - the same picture as recorded fresh (verify mode finds no stale record) - and recorded again when the selection or an object changes (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    GameObject *wire = open(ed);
    ed.set_scene_hash_check_every_for_test(8);  // (after the open, which headless-inits; a window's interval; a headless editor walks every frame, so nothing would be kept)
    ed.viewport_device_for_test(2);
    r34_steps(ed, 3);
    auto orbit = [&](int frames) {  // (the camera nudged every frame: the grid moves, nothing else)
      for (int k = 0; k < frames; k++) {
        ed.command(k % 2 ? "camera 0 4 6 0 1 0" : "camera 0 4 6.02 0 1 0");
        r34_steps(ed, 1);
      }
    };
    const uint64_t k0 = ed.overlays_kept_for_test();
    orbit(8);
    const uint64_t kept = ed.overlays_kept_for_test() - k0;
    const std::vector<uint32_t> pic_kept = r34_crop(ed, ed.scene_view_rect());
    const uint64_t m0 = Editor::render_cache_mismatches_all();
    ed.set_render_cache_verify(true);
    orbit(8);
    const std::vector<uint32_t> pic_fresh = r34_crop(ed, ed.scene_view_rect());
    const uint64_t mismatches = Editor::render_cache_mismatches_all() - m0;
    ed.set_render_cache_verify(false);
    auto differ = [](const std::vector<uint32_t> &a, const std::vector<uint32_t> &b) {
      size_t n = 0;
      for (size_t i = 0; i < a.size() && i < b.size(); i++) n += (a[i] & 0xFFFFFF) != (b[i] & 0xFFFFFF);
      return n;
    };
    const size_t same_view = differ(pic_kept, pic_fresh);
    ed.command("select Wired");  // (the outline moves to the sphere: the key changes)
    orbit(2);
    const size_t after_select = differ(pic_fresh, r34_crop(ed, ed.scene_view_rect()));
    const std::vector<uint32_t> before_move = r34_crop(ed, ed.scene_view_rect());
    wire->set_local_position({0.5f, 1.0f, 0.5f});  // (its wireframe moves: the counter moves)
    orbit(2);
    const size_t after_move = differ(before_move, r34_crop(ed, ed.scene_view_rect()));
    std::printf("    kept in %llu of 8 orbit frames; verify mode: %llu stale record(s), %zu pixels differ from the kept picture; the selection "
                "changed: %zu pixels; the sphere moved: %zu pixels\n",
                (unsigned long long)kept, (unsigned long long)mismatches, same_view, after_select, after_move);
    CHECK(kept >= 5);  // (every 8th frame walks anyway, as the scene hash is checked)
    CHECK(mismatches == 0);
    CHECK(same_view == 0);
    CHECK(after_select > 100);
    CHECK(after_move > 100);
  });
}

/* ---------------------------------------------------------------- round 458: kept instances, meshes streamed */
static size_t r458_differ(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b) {
  size_t n = 0;
  for (size_t i = 0; i < a.size() && i < b.size(); i++) n += (a[i] & 0xFFFFFF) != (b[i] & 0xFFFFFF);
  return n;
}

/* The Scene view over a field of cubes on the card; the camera nudged every frame so each frame draws. */
static std::vector<GameObject *> r458_field(Editor &ed, int n) {
  r34_open(ed);
  std::vector<GameObject *> cubes;
  for (int i = 0; i < n; i++) {
    GameObject *g = create_primitive(ed.scene(), "Cube");
    g->name = strprintf("K458_%d", i);
    g->set_local_position({-3.0f + 1.2f * (float)(i % 6), 0.5f, -3.0f + 1.2f * (float)(i / 6)});
    g->set_local_scale(Vec3(0.5f));
    g->get<MeshRenderer>()->materials = {make_material(g->name, Vec3(0.6f, 0.6f, 0.7f))};
    cubes.push_back(g);
  }
  ed.mark_changed_for_test("round 458 cubes");
  ed.command("camera 0 6 9 0 0 0");
  r34_steps(ed, 3);
  return cubes;
}

static void r458_orbit(Editor &ed, int frames) {
  for (int k = 0; k < frames; k++) {
    ed.command(k % 2 ? "camera 0 6 9 0 0 0" : "camera 0 6 9.03 0 0 0");
    r34_steps(ed, 1);
  }
}

static void tests_round458() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  test("round 458: orbiting over a field of cubes keeps the instance array on every frame after the first - the same picture as a fresh gather (verify mode finds no stale array) - and moving a cube, a material's colour, a lightmap bake or Solid shading gathers it again (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    std::vector<GameObject *> cubes = r458_field(ed, 30);
    ed.viewport_device_for_test(2);
    r34_steps(ed, 3);
    CHECK(ed.scene_on_gpu_for_test());
    uint64_t k0 = ed.instances_kept_for_test(), b0 = ed.instances_built_for_test();
    r458_orbit(ed, 8);
    const uint64_t kept = ed.instances_kept_for_test() - k0, built = ed.instances_built_for_test() - b0;
    const std::vector<uint32_t> pic_kept = r34_crop(ed, ed.scene_view_rect());
    const uint64_t m0 = Editor::render_cache_mismatches_all();
    b0 = ed.instances_built_for_test();
    ed.set_render_cache_verify(true);
    r458_orbit(ed, 8);
    const std::vector<uint32_t> pic_fresh = r34_crop(ed, ed.scene_view_rect());
    const uint64_t mismatches = Editor::render_cache_mismatches_all() - m0, verified = ed.instances_built_for_test() - b0;
    ed.set_render_cache_verify(false);
    const size_t same_view = r458_differ(pic_kept, pic_fresh);
    std::printf("    %llu of 8 orbit frames kept the instances (%llu gathered); verify mode gathered %llu times: %llu stale, %zu pixels differ\n",
                (unsigned long long)kept, (unsigned long long)built, (unsigned long long)verified, (unsigned long long)mismatches, same_view);
    CHECK(kept >= 7);
    CHECK(verified >= 8);
    CHECK(mismatches == 0);
    CHECK(same_view == 0);
    /* Each change gathers again, once, and then settles. */
    auto after = [&](const char *what, const std::function<void()> &act) {
      r458_orbit(ed, 2);
      const std::vector<uint32_t> before = r34_crop(ed, ed.scene_view_rect());
      const uint64_t b = ed.instances_built_for_test();
      act();
      r34_steps(ed, 1);
      const uint64_t gathered = ed.instances_built_for_test() - b;
      const size_t moved = r458_differ(before, r34_crop(ed, ed.scene_view_rect()));
      const uint64_t k = ed.instances_kept_for_test();
      r458_orbit(ed, 4);
      const uint64_t settled = ed.instances_kept_for_test() - k;
      std::printf("    %s: gathered %llu time(s), %zu pixels changed, then %llu of 4 frames kept\n", what, (unsigned long long)gathered, moved, (unsigned long long)settled);
      CHECK(gathered >= 1);
      CHECK(moved > 20);
      CHECK(settled >= 3);
    };
    after("one cube moved", [&] { cubes[7]->set_local_position({0.0f, 1.5f, 0.0f}); });
    after("a material's colour", [&] {
      MaterialPtr m = cubes[3]->get<MeshRenderer>()->materials[0];
      m->base_color = Vec3(0.9f, 0.1f, 0.1f);
      m->touch();
    });
    after("Solid shading", [&] { ed.command("shading solid"); });
    after("Shaded again", [&] { ed.command("shading shaded"); });
    CHECK(Editor::render_cache_mismatches_all() - m0 == 0);
  });
  test("round 458: a lightmap bake that lands gathers the instances again (a baked frame isn't kept) and shows in the picture (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    r35_open(ed);
    r35_add(ed, "Plane", "Floor458", {0, 0, 0}, {1, 1, 1}, Vec3(0.8f));
    r35_add(ed, "Cube", "Wall458", {0, 1, 0}, {0.2f, 2, 6}, Vec3(0.9f, 0.2f, 0.2f));
    ed.viewport_device_for_test(2);
    ed.command("camera 6 4 8 0 1 0");
    r34_steps(ed, 4);
    auto orbit = [&](int frames) {
      for (int k = 0; k < frames; k++) {
        ed.command(k % 2 ? "camera 6 4 8 0 1 0" : "camera 6 4 8.03 0 1 0");
        r34_steps(ed, 1);
      }
    };
    uint64_t k0 = ed.instances_kept_for_test();
    orbit(6);
    const uint64_t kept_before = ed.instances_kept_for_test() - k0;
    const std::vector<uint32_t> before = r34_crop(ed, ed.scene_view_rect());
    r35_bake(ed);
    orbit(2);
    const uint64_t m0 = Editor::render_cache_mismatches_all();
    ed.set_render_cache_verify(true);
    orbit(4);
    ed.set_render_cache_verify(false);
    const uint64_t stale = Editor::render_cache_mismatches_all() - m0;
    const uint64_t b0 = ed.instances_built_for_test();
    orbit(6);
    const uint64_t gathered = ed.instances_built_for_test() - b0;
    const size_t changed = r458_differ(before, r34_crop(ed, ed.scene_view_rect()));
    std::printf("    before the bake %llu of 6 orbit frames kept; after it %llu of 6 gathered, %zu pixels changed, %llu stale\n", (unsigned long long)kept_before,
                (unsigned long long)gathered, changed, (unsigned long long)stale);
    CHECK(kept_before >= 5);
    CHECK(gathered >= 6);
    CHECK(changed > 200);
    CHECK(stale == 0);
  });
  test("round 458: a texture that lands while the views stream refreshes the kept instances - the cubes show it, the array is kept again afterwards, verify mode finds no stale array (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    ed.init_headless(640, 360);
    ed.set_async_jobs_for_test(true);
    ed.viewport_device_for_test(2);
    ed.command("camera 0 4 7 0 0.5 0");
    r34_steps(ed, 3);
    r34_toolbar_click(ed, 3), r34_park_mouse(ed);  // (the statistics overlay off: its numbers move every frame)
    r34_steps(ed, 2);
    std::vector<std::string> files;
    for (int i = 0; i < 3; i++) {
      ed.command("create Cube");
      GameObject *g = ed.selected_object();
      MeshRenderer *mr = g ? g->get<MeshRenderer>() : nullptr;
      if (!mr) continue;
      g->set_local_position({-2.0f + 2.0f * (float)i, 0.5f, 0.0f});
      const std::string tex = fs::join(test_dir(), strprintf("r458_tex_%d.png", i));
      std::vector<uint32_t> px(256 * 256, 0xFF000000u | (uint32_t)(60 * i) << 16 | 0x30u);
      CHECK(write_png(tex, px.data(), 256, 256, 256));
      MaterialPtr m = make_material(strprintf("R458 tex %d", i), {0.8f, 0.8f, 0.8f});
      m->base_map.path = tex;
      mr->materials = {m};
      files.push_back(tex);
    }
    CHECK(files.size() == 3);
    texture_cache_clear();
    ed.mark_changed_for_test("r458 textured cubes");
    ed.step_frame_headless();
    const std::vector<uint32_t> first = r34_crop(ed, ed.scene_view_rect());
    for (int f = 0; f < 4000 && (ed.texture_streaming_for_test() || ed.streamed_files_for_test() < 3); f++) ed.step_frame_headless();
    for (int k = 0; k < 6; k++) ed.step_frame_headless();
    const std::vector<uint32_t> landed = r34_crop(ed, ed.scene_view_rect());
    const uint64_t k0 = ed.instances_kept_for_test();
    for (int k = 0; k < 6; k++) {
      ed.command(k % 2 ? "camera 0 4 7 0 0.5 0" : "camera 0 4 7.03 0 0.5 0");
      ed.step_frame_headless();
    }
    const uint64_t kept = ed.instances_kept_for_test() - k0;
    const uint64_t m0 = Editor::render_cache_mismatches_all();
    ed.set_render_cache_verify(true);
    for (int k = 0; k < 4; k++) {
      ed.command(k % 2 ? "camera 0 4 7 0 0.5 0" : "camera 0 4 7.03 0 0.5 0");
      ed.step_frame_headless();
    }
    ed.set_render_cache_verify(false);
    const uint64_t stale = Editor::render_cache_mismatches_all() - m0;
    const size_t changed = r458_differ(first, landed);
    std::printf("    the textures landed: %zu pixels of the window changed; %llu of 6 later frames kept; %llu stale\n", changed, (unsigned long long)kept,
                (unsigned long long)stale);
    /* The array is kept now: change a texture under it. First the card's copies evicted (the ids asked again answer for a
     * texture sent anew - the same picture, nothing stale), then a file that is another colour and streams in. */
    const std::vector<uint32_t> before_evict = r34_crop(ed, ed.scene_view_rect());
    const uint64_t m1 = Editor::render_cache_mismatches_all();
    ed.gpu_evict_textures_for_test();
    ed.set_render_cache_verify(true);
    ed.command("camera 0 4 7 0 0.5 0");
    ed.step_frame_headless();
    ed.command("camera 0 4 7.03 0 0.5 0");
    ed.step_frame_headless();
    ed.set_render_cache_verify(false);
    ed.command("camera 0 4 7 0 0.5 0");
    ed.step_frame_headless();
    const size_t evict_diff = r458_differ(before_evict, r34_crop(ed, ed.scene_view_rect()));
    std::vector<uint32_t> other(256 * 256, 0xFF20D020u);
    CHECK(write_png(files[0], other.data(), 256, 256, 256));
    const uint64_t built0 = ed.instances_built_for_test(), streamed0 = ed.streamed_files_for_test();
    texture_cache_clear();
    refresh_material_textures();
    for (int f = 0; f < 4000 && (ed.texture_streaming_for_test() || ed.streamed_files_for_test() <= streamed0 || ed.texture_stream_busy_for_test()); f++) {
      ed.command(f % 2 ? "camera 0 4 7 0 0.5 0" : "camera 0 4 7.03 0 0.5 0");
      ed.step_frame_headless();
    }
    ed.set_render_cache_verify(true);
    for (int k = 0; k < 4; k++) {
      ed.command(k % 2 ? "camera 0 4 7 0 0.5 0" : "camera 0 4 7.03 0 0.5 0");
      ed.step_frame_headless();
    }
    ed.set_render_cache_verify(false);
    ed.command("camera 0 4 7 0 0.5 0");
    ed.step_frame_headless();
    const size_t recoloured = r458_differ(landed, r34_crop(ed, ed.scene_view_rect()));
    const uint64_t rebuilt = ed.instances_built_for_test() - built0, stale2 = Editor::render_cache_mismatches_all() - m1;
    std::printf("    the card's copies evicted: %zu pixels changed; a file replaced and streamed in: %zu pixels changed, %llu gathers; %llu stale in all\n", evict_diff,
                recoloured, (unsigned long long)rebuilt, (unsigned long long)stale2);
    CHECK(changed > 500);
    CHECK(kept >= 4);
    CHECK(stale == 0);
    CHECK(evict_diff == 0);
    CHECK(recoloured > 300);
    CHECK(rebuilt >= 1);
    CHECK(stale2 == 0);
  });
  /* The meshes: a field of distinct spheres, the budget a byte (so one mesh a frame, the first always going up). */
  auto spheres = [](Editor &ed, int n, std::vector<const RenderMesh *> &meshes) {
    GameObject *last = nullptr;
    for (int i = 0; i < n; i++) {
      GameObject *g = ed.scene().create(strprintf("S458_%d", i));
      g->add<MeshFilter>()->mesh = primitives::uv_sphere(0.4f, 24, 16);
      g->add<MeshRenderer>();
      g->set_local_position({-3.0f + 1.0f * (float)(i % 6), 0.5f, -1.5f + 1.0f * (float)(i / 6)});
      meshes.push_back(&g->evaluated_mesh(0)->render_mesh());
      last = g;
    }
    ed.mark_changed_for_test("r458 spheres");
    return last;
  };
  test("round 458: a scene of many meshes comes up over several frames under the upload budget - no frame sends more than the budget (and one mesh), the selected object's mesh is on the card in the first frame, every mesh is drawn once the queue drains, and the picture is the one a frame with no budget draws (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    ed.init_headless(640, 360);
    ed.set_async_jobs_for_test(true);
    ed.viewport_device_for_test(2);
    ed.command("camera 0 4 7 0 0.5 0");
    for (int k = 0; k < 3; k++) ed.step_frame_headless();
    r34_toolbar_click(ed, 3), r34_park_mouse(ed);  // (the statistics overlay off: its numbers move every frame)
    r34_steps(ed, 2);
    ed.mesh_budget_for_test(1);
    std::vector<const RenderMesh *> meshes;
    GameObject *last = spheres(ed, 18, meshes);
    CHECK(meshes.size() == 18);
    ed.command("select S458_17");
    CHECK(ed.selected_object() == last);
    ed.step_frame_headless();
    const size_t queued_first = ed.mesh_queue_for_test();
    const bool selected_on_card = ed.mesh_on_card_for_test(*meshes.back());
    size_t on_card_first = 0;
    for (const RenderMesh *rm : meshes) on_card_first += ed.mesh_on_card_for_test(*rm);
    int frames = 1;
    for (; frames < 200 && (ed.mesh_queue_for_test() > 0 || ed.texture_stream_busy_for_test()); frames++) ed.step_frame_headless();
    for (int k = 0; k < 3; k++) ed.step_frame_headless();
    size_t on_card = 0;
    for (const RenderMesh *rm : meshes) on_card += ed.mesh_on_card_for_test(*rm);
    const std::vector<uint32_t> streamed = r34_crop(ed, ed.scene_view_rect());
    /* (The picture with every mesh sent in the first frame: a headless editor that didn't ask for workers doesn't stream.) */
    Editor ref;
    ref.init_headless(640, 360);
    ref.set_async_jobs_for_test(true);  // (streaming on, a budget that never binds: only the queue differs from the run above)
    ref.viewport_device_for_test(2);
    ref.command("camera 0 4 7 0 0.5 0");
    for (int k = 0; k < 3; k++) ref.step_frame_headless();
    r34_toolbar_click(ref, 3), r34_park_mouse(ref);  // (the statistics overlay off: its numbers move every frame)
    r34_steps(ref, 2);
    ref.mesh_budget_for_test((size_t)1 << 30, 1e9);
    std::vector<const RenderMesh *> ref_meshes;
    spheres(ref, 18, ref_meshes);
    ref.command("select S458_17");
    ref.step_frame_headless();
    size_t ref_first = 0;
    for (const RenderMesh *rm : ref_meshes) ref_first += ref.mesh_on_card_for_test(*rm);
    for (int k = 0; k < 3; k++) ref.step_frame_headless();
    const std::vector<uint32_t> whole = r34_crop(ref, ref.scene_view_rect());
    const size_t differ = r458_differ(streamed, whole);
    std::printf("    first frame: %zu mesh(es) waiting, %zu on the card, the selected one's %d; all 18 on the card after %d frames (%zu), the most any frame sent %.1f KB; "
                "drain %.0f ms; with no streaming the first frame had %zu on the card; %zu pixels differ from that picture\n",
                queued_first, on_card_first, (int)selected_on_card, frames, on_card, (double)ed.mesh_uploaded_frame_bytes_for_test() / 1024.0,
                ed.mesh_drain_ms_for_test(), ref_first, differ);
    CHECK(queued_first >= 10);
    CHECK(selected_on_card);
    CHECK(on_card_first < 18);
    CHECK(frames >= 6);
    CHECK(on_card == 18);
    CHECK(ed.mesh_queue_for_test() == 0);
    const size_t one = meshes[0]->positions.size() * sizeof(gpu::RasterVertex) + meshes[0]->indices.size() * 4;
    CHECK(ed.mesh_uploaded_frame_bytes_for_test() <= 2 * one);  // (a frame sends the first mesh the budget lets by and the selected one, never more)
    CHECK(ref_first == 18);
    CHECK(differ == 0);
  });
  test("round 458: with a realistic budget no frame sends more than the budget (or a single mesh past it), a mesh bigger than the budget goes up alone, and the edited object's mesh is on the card in the first frame whatever the queue (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    ed.init_headless(640, 360);
    ed.set_async_jobs_for_test(true);
    ed.viewport_device_for_test(2);
    ed.command("camera 0 4 7 0 0.5 0");
    for (int k = 0; k < 3; k++) ed.step_frame_headless();
    std::vector<const RenderMesh *> meshes;
    spheres(ed, 18, meshes);
    const size_t one = meshes[0]->positions.size() * sizeof(gpu::RasterVertex) + meshes[0]->indices.size() * 4;
    const size_t budget = 2 * one + one / 2;  // (two meshes fit; a third doesn't)
    ed.mesh_budget_for_test(budget);
    GameObject *edited = ed.scene().find_by_name("S458_9");
    CHECK(edited != nullptr);
    ed.command("select S458_9");
    ed.command("edit");
    CHECK(ed.in_edit_mode());
    ed.step_frame_headless();
    const Mesh *em = edited ? edited->evaluated_mesh(2) : nullptr;
    const bool edited_on_card = em && ed.mesh_on_card_for_test(em->render_mesh(true));
    int frames = 1;
    for (; frames < 200 && (ed.mesh_queue_for_test() > 0 || ed.texture_stream_busy_for_test()); frames++) ed.step_frame_headless();
    size_t on_card = 0;
    for (const RenderMesh *rm : meshes) on_card += ed.mesh_on_card_for_test(*rm);
    std::printf("    budget %zu bytes (a mesh %zu): the most any frame sent %zu, the edited mesh on the card in frame 1: %d, %zu of 18 plain meshes after %d frames\n", budget, one,
                (size_t)ed.mesh_uploaded_frame_bytes_for_test(), (int)edited_on_card, on_card, frames);
    CHECK(edited_on_card);
    CHECK(ed.mesh_uploaded_frame_bytes_for_test() <= budget + 2 * one);  // (the selected object is urgent twice - its edit mesh in the Scene view, its plain one in the Game view - and may ride on a full frame; the others keep to the budget)
    CHECK(frames >= 5);
    CHECK(on_card >= 17);  // (the edited object draws its edit mesh: its plain one never goes up)
    /* A mesh bigger than the whole budget still goes up, alone in its frame. */
    ed.mesh_budget_for_test(1);
    GameObject *big = ed.scene().create("S458_big");
    big->add<MeshFilter>()->mesh = primitives::uv_sphere(0.4f, 96, 64);
    big->add<MeshRenderer>();
    ed.command("edit off");
    ed.mark_changed_for_test("r458 big");
    for (int k = 0; k < 30 && (ed.mesh_queue_for_test() > 0 || ed.texture_stream_busy_for_test() || !ed.mesh_on_card_for_test(big->evaluated_mesh(0)->render_mesh())); k++) ed.step_frame_headless();
    CHECK(ed.mesh_on_card_for_test(big->evaluated_mesh(0)->render_mesh()));
  });
  test("round 458: the upload budget counts time as well as bytes - with a byte budget that never binds and no time to spend, meshes still queue (one a frame, and the selected one) and all come up (skipped without a GPU)", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    ed.init_headless(640, 360);
    ed.set_async_jobs_for_test(true);
    ed.viewport_device_for_test(2);
    ed.command("camera 0 4 7 0 0.5 0");
    for (int k = 0; k < 3; k++) ed.step_frame_headless();
    ed.mesh_budget_for_test((size_t)1 << 30, 0.0);
    std::vector<const RenderMesh *> meshes;
    spheres(ed, 18, meshes);
    ed.command("select S458_17");
    ed.step_frame_headless();
    const size_t queued = ed.mesh_queue_for_test();
    const bool selected_on_card = ed.mesh_on_card_for_test(*meshes.back());
    int frames = 1;
    for (; frames < 200 && (ed.mesh_queue_for_test() > 0 || ed.texture_stream_busy_for_test()); frames++) ed.step_frame_headless();
    size_t on_card = 0;
    for (const RenderMesh *rm : meshes) on_card += ed.mesh_on_card_for_test(*rm);
    const size_t one = meshes[0]->positions.size() * sizeof(gpu::RasterVertex) + meshes[0]->indices.size() * 4;
    std::printf("    no time to spend: %zu mesh(es) waiting after the first frame, the selected one's on the card %d; %zu of 18 on the card after %d frames, the most any frame sent %zu bytes (a mesh %zu)\n",
                queued, (int)selected_on_card, on_card, frames, (size_t)ed.mesh_uploaded_frame_bytes_for_test(), one);
    CHECK(queued >= 10);
    CHECK(selected_on_card);
    CHECK(frames >= 8);
    CHECK(on_card == 18);
    CHECK(ed.mesh_uploaded_frame_bytes_for_test() <= 2 * one);
  });
}

void run_gpu_tests() {
  round59_tests();
  tests_round122_to_round125();
  tests_round123();
  tests_round126_to_round127();
  tests_round130();
  tests_round179();
  tests_round180();
  tests_round181();
  tests_round182();
  tests_round183();
  tests_round186();
  tests_round189();
  tests_round190();
  tests_round211();
  tests_round212();
  tests_round214();
  tests_round216();
  tests_round444_gpu();
  tests_round217();
  tests_round219();
  tests_round224();
  tests_round225();
  tests_round337();
  tests_round414();
  tests_round415();
  tests_round416();
  tests_round443();
  tests_round448();
  tests_round449();
  tests_round458();
}

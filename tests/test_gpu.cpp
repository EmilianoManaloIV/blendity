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
    CHECK(worst < 100.0);
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
    CHECK(!first && bake.gpu_gather_waiting() && first_ms < 100.0);
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
  test("gpu views: a material the card doesn't draw yet (a see-through toon material) sends the view to the CPU, says why, and the picture is then the CPU's exactly", [&] {
    if (!gpu::available()) {
      std::printf("    no Vulkan GPU: skipped\n");
      return;
    }
    Editor ed;
    r34_open(ed);
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube != nullptr);
    if (!cube) return;
    /* (A procedural was the example until task 0101 drew them on the card, an axis material until task 0195.) */
    MaterialPtr veil = make_material("Toon Veil", {0.8f, 0.8f, 0.8f});
    veil->shading_model = 1;
    veil->surface = (int)MaterialSurface::Transparent;
    veil->alpha = 0.5f;
    cube->get<MeshRenderer>()->materials = {veil};
    const std::vector<uint32_t> cpu = r122_views(ed, 1), gpu = r122_views(ed, 2);
    std::printf("    %s\n", ed.gpu_view_status().c_str());
    CHECK(ed.gpu_view_reason().find("toon") != std::string::npos);
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
      CHECK(worst_poll < 10.0);
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
      CHECK(pause_ms < 150.0);
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
      CHECK(frames == 20 && quick >= 16 && worst < 30.0);
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
  tests_round217();
  tests_round219();
}

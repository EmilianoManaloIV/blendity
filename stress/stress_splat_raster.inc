// SPDX-License-Identifier: GPL-2.0-or-later
// Round 532 (task 0512): Gaussian splats in the rasterizer - what a frame of 100k and 1M splats costs (ADR 0105's
// budgets: no editor frame over 50 ms while work runs). Included by stress_main.cpp.

static void test_splat_raster(Report &rep, const Options &o) {
  rep.title("Gaussian splats in the rasterizer (round 532)",
            "Random degree-3 splat clouds (pointcloud_random) seen from 5 m with a 60 degree lens at 1280 x 720. The sort is the back-to-front "
            "radix sort over the job system; project is EWA and SH for every splat; draw is the tile compositing on the CPU. The worker row is "
            "what a frame waits when the camera moved and a worker sorts (the frame draws the last order: it should be near 0). Then a headless "
            "Scene view orbiting a cloud, drawn by the CPU and (with a card) the card: the median and longest frame.");
  rep.table({"splats", "sort ms", "project ms", "draw ms", "frame wait with a worker sort ms", "drawn"});
  const std::vector<size_t> sizes = o.quick ? std::vector<size_t>{100000} : std::vector<size_t>{100000, 1000000};
  const int W = 1280, H = 720;
  for (size_t n : sizes) {
    PointCloudPtr pc = pointcloud_random(PointCloudType::GaussianSplats, n, 3, 532);
    if (!pc) continue;
    const Mat4 v = Mat4::look_at({0, 0, -5}, {0, 0, 0}, {0, 1, 0});
    const Mat4 p = Mat4::perspective(60 * kDeg2Rad, W / (float)H, 0.1f, 100.0f);
    std::vector<uint32_t> ord;
    const double sort_ms = time_ms([&] { gsplat_sort_back_to_front(*pc, p * v, ord); }, 3);
    SplatPass pass;
    SplatDraw d{pc, Mat4::identity(), 1, false, 1};
    SplatView sv{v, p, W, H, ViewTransform::Standard, 0.0f};
    pass.prepare({d}, sv, false);  // (the first frame sorts in place)
    double project_ms = 0;
    for (int k = 0; k < 3; k++) {
      pass.prepare({d}, sv, false);
      project_ms += pass.stats().ms_project / 3.0;
    }
    Image img;
    img.resize(W, H);
    RenderTarget rt;
    rt.attach(img, {0, 0, W, H});
    rt.resize_planes();
    std::fill(rt.depth.begin(), rt.depth.end(), 1.0f);
    const double draw_ms = time_ms([&] { pass.draw_cpu(rt); }, 3);
    /* The camera moves: a big cloud's frame hands the sort to a worker and waits for nothing. */
    SplatView moved = sv;
    moved.view = Mat4::look_at({0.3f, 0.1f, -5}, {0, 0, 0}, {0, 1, 0});
    SplatSorter &s = pass.sorter(1);
    ScopedTimer tw;
    s.order(d, moved.proj * moved.view, true);
    const double wait_ms = tw.ms();
    const bool async = s.last_was_async();
    s.wait();
    rep.row({std::to_string(n), f2(sort_ms), f2(project_ms), f2(draw_ms), async ? f2(wait_ms) : "(sorted in the frame)", std::to_string(pass.stats().drawn)});
  }
  /* The Scene view: a cloud orbited, CPU then card. */
  rep.table({"Scene view", "splats", "device", "median frame ms", "longest frame ms", "frames"});
  for (size_t n : sizes) {
    for (int device : {1, 2}) {
      if (device == 2 && !gpu::available()) continue;
      auto ed = std::make_unique<Editor>();
      ed->init_headless(1600, 900);
      ed->viewport_device_for_test(device);
      ed->command(strprintf("pointcloud random splats %zu 3 532", n));
      for (int i = 0; i < 4; i++) ed->step_frame_headless();
      const SceneCamera cam = ed->scene_camera_for_test();
      std::vector<double> ms;
      const int frames = o.quick ? 10 : 40;
      for (int k = 0; k < frames; k++) {
        ed->command(strprintf("camera %d %g %g %g %g %g", 30 + k * 3, (double)cam.pitch, (double)cam.distance, (double)cam.pivot.x,
                              (double)cam.pivot.y, (double)cam.pivot.z));
        ScopedTimer t;
        ed->step_frame_headless();
        ms.push_back(t.ms());
      }
      std::vector<double> sorted = ms;
      std::sort(sorted.begin(), sorted.end());
      rep.row({"orbit", std::to_string(n), device == 2 ? (ed->gpu_view_reason().empty() ? "card" : "card refused: " + ed->gpu_view_reason()) : "CPU",
               f1(sorted[sorted.size() / 2]), f1(sorted.back()), std::to_string(ms.size())});
      rep.note(strprintf("%s, %zu splats, the last frame's parts: %s", device == 2 ? "card" : "CPU", n, splat_stats_text().c_str()));
    }
  }
  /* Round 557: the card's first open - its pipelines - with an empty pipeline cache folder, then again from the cache that
   * open wrote (the driver's own per-program cache is the same in both: what differs is Blendity's file). */
  if (gpu::available() && !gpu::devices().empty()) {
    rep.table({"card open", "pipeline cache", "ms", "from the cache"});
    const std::string dir = fs::join(fs::temp_dir(), strprintf("blendity_stress_pcache_%llu", (unsigned long long)now_seconds()));
    fs::make_dirs(dir);
    const char *old = std::getenv("BLENDITY_CACHE_DIR");
    const std::string keep = old ? old : "";
#ifdef _WIN32
    _putenv_s("BLENDITY_CACHE_DIR", dir.c_str());
#else
    setenv("BLENDITY_CACHE_DIR", dir.c_str(), 1);
#endif
    for (const char *label : {"empty folder", "written by the first open"}) {
      std::string err;
      ScopedTimer t;
      std::unique_ptr<gpu::Raster> r = gpu::Raster::create(gpu::devices()[0].index, &err);
      const double open_ms = t.ms();
      rep.row({r ? r->info().name : "failed: " + err, label, f1(open_ms), r && r->pipelines_from_cache() ? "yes" : "no"});
    }
#ifdef _WIN32
    _putenv_s("BLENDITY_CACHE_DIR", keep.c_str());
#else
    if (old) setenv("BLENDITY_CACHE_DIR", keep.c_str(), 1);
    else unsetenv("BLENDITY_CACHE_DIR");
#endif
    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::u8path(dir), ec);
    rep.note("The SPIR-V is shipped with the build (`shaders/`) and the driver keeps its own cache per program; the first row is a "
             "new build's first open on a machine whose Blendity cache doesn't have this card's pipelines yet.");
  }
  rep.note("ADR 0105: a frame over 50 ms while work runs is a miss; the write-up (round 544) names any with its cause.");
}

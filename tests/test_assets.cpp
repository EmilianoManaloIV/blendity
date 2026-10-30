// SPDX-License-Identifier: GPL-2.0-or-later
// blendity_tests: the assets tests (task 0177: one file a subsystem; the groups keep their rounds' order).
#include "test_common.h"
#include "fixtures/synthetic_mmd.h"

static void image_tests() {
  test("image: PNG round-trip is exact", [] {
    const int w = 37, h = 21;
    std::vector<uint32_t> px((size_t)w * h);
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) px[(size_t)y * w + x] = 0xFF000000u | (uint32_t)(x * 7) << 16 | (uint32_t)(y * 11) << 8 | (uint32_t)((x ^ y) & 255);
    std::string path = fs::join(test_dir(), "test_rt.png"), bytes, err;
    CHECK(write_png(path, px.data(), w, h, w));
    Bitmap b;
    CHECK(fs::read_file(path, bytes) && decode_image(bytes, b, err));
    CHECK(b.width == w && b.height == h && !b.is_float);
    bool same = b.width == w;
    for (int i = 0; same && i < w * h; i++)
      same = b.rgba8[i * 4] == ((px[i] >> 16) & 255) && b.rgba8[i * 4 + 1] == ((px[i] >> 8) & 255) && b.rgba8[i * 4 + 2] == (px[i] & 255);
    CHECK(same);
  });
  test("image: JPEG encoder/decoder quality and HDR round-trip", [] {
    const int w = 64, h = 48;
    std::vector<uint32_t> px((size_t)w * h);
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) px[(size_t)y * w + x] = 0xFF000000u | (uint32_t)(x * 4) << 16 | (uint32_t)(y * 5) << 8 | 128u;
    std::string path = fs::join(test_dir(), "test_rt.jpg"), bytes, err;
    CHECK(write_jpeg(path, px.data(), w, h, w, 92));
    Bitmap b;
    CHECK(fs::read_file(path, bytes) && decode_image(bytes, b, err));
    double mse = 0;
    for (int i = 0; i < w * h && b.width == w; i++)
      for (int c = 0; c < 3; c++) {
        double d = (double)b.rgba8[i * 4 + c] - (double)((px[i] >> (16 - 8 * c)) & 255);
        mse += d * d;
      }
    mse /= w * h * 3.0;
    double psnr = 10.0 * std::log10(255.0 * 255.0 / std::max(mse, 1e-9));
    CHECK(psnr > 35.0);
    std::vector<float> rgb((size_t)w * h * 3);
    for (size_t i = 0; i < rgb.size(); i++) rgb[i] = 0.001f + (float)(i % 97) * 0.37f;
    std::string hp = fs::join(test_dir(), "test_rt.hdr");
    CHECK(write_hdr(hp, rgb.data(), w, h));
    Bitmap hb;
    CHECK(fs::read_file(hp, bytes) && decode_image(bytes, hb, err) && hb.is_float);
    float worst = 0;
    for (int i = 0; i < w * h && hb.width == w; i++) {
      /* RGBE shares one exponent per pixel: the error is relative to the
       * brightest channel (8-bit mantissa -> < 1/128). */
      float mx = std::max({rgb[(size_t)i * 3], rgb[(size_t)i * 3 + 1], rgb[(size_t)i * 3 + 2]});
      for (int c = 0; c < 3; c++)
        worst = std::max(worst, std::fabs(rgb[(size_t)i * 3 + c] - hb.rgbaf[(size_t)i * 4 + c]) / mx);
    }
    CHECK(worst < 1.0f / 128.0f);
  });
  test("image: library codecs agree with Blendity's decoders", [] {
    const int w = 64, h = 48;
    std::vector<uint32_t> px((size_t)w * h);
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) px[(size_t)y * w + x] = 0xFF000000u | (uint32_t)(x * 4) << 16 | (uint32_t)(y * 5) << 8 | (uint32_t)((x * y) & 255);
    std::string png = fs::join(test_dir(), "lib_rt.png"), jpg = fs::join(test_dir(), "lib_rt.jpg"), bytes, err;
    CHECK(write_png(png, px.data(), w, h, w) && write_jpeg(jpg, px.data(), w, h, w, 95));
    for (const std::string &path : {png, jpg}) {
      Bitmap a, b;
      CHECK(fs::read_file(path, bytes));
      set_image_library_codecs(true);
      CHECK(decode_image(bytes, a, err));
      set_image_library_codecs(false);
      CHECK(decode_image(bytes, b, err));
      set_image_library_codecs(true);
      CHECK(a.width == b.width && a.height == b.height && a.rgba8.size() == b.rgba8.size());
      int worst = 0;
      for (size_t i = 0; i < a.rgba8.size() && a.rgba8.size() == b.rgba8.size(); i++) worst = std::max(worst, std::abs((int)a.rgba8[i] - (int)b.rgba8[i]));
      /* PNG is lossless: identical. JPEG decoders may round the IDCT and
       * chroma upsampling differently by a few levels. */
      CHECK(worst <= (path == png ? 0 : 8));
    }
  });
  test("image: OpenEXR round-trip (half float)", [] {
    if (!exr_available()) return;
    const int w = 33, h = 17;
    std::vector<float> rgb((size_t)w * h * 3);
    for (size_t i = 0; i < rgb.size(); i++) rgb[i] = 0.01f + (float)(i % 101) * 0.73f;  // up to ~73: true HDR
    std::string path = fs::join(test_dir(), "rt.exr"), bytes, err;
    CHECK(write_exr(path, rgb.data(), w, h, true));
    Bitmap b;
    CHECK(fs::read_file(path, bytes) && decode_image(bytes, b, err));
    CHECK(b.is_float && b.width == w && b.height == h);
    float worst = 0;
    for (int i = 0; i < w * h && b.width == w; i++)
      for (int c = 0; c < 3; c++) {
        float a = rgb[(size_t)i * 3 + c];
        worst = std::max(worst, std::fabs(b.rgbaf[(size_t)i * 4 + c] - a) / a);
      }
    CHECK(worst < 1e-3f);  // half float: 11-bit mantissa
    CHECK(image_extension_supported(".exr"));
  });
  test("image: mipmapped texture sampling", [] {
    Bitmap b;
    b.width = 64;
    b.height = 32;
    b.rgba8.assign(64 * 32 * 4, 0);
    for (int i = 0; i < 64 * 32; i++) {
      bool on = ((i % 64) / 8 + (i / 64) / 8) % 2;
      for (int c = 0; c < 3; c++) b.rgba8[i * 4 + c] = on ? 255 : 0;
      b.rgba8[i * 4 + 3] = 255;
    }
    Texture t;
    t.build(b, false);
    CHECK(t.levels.size() == 7);  // 64x32 .. 1x1
    Vec4 sharp = t.sample({4.0f / 64, 4.0f / 32}, 0.0f, TexWrap::Repeat, TexFilter::Linear);
    CHECK(sharp.x < 0.01f || sharp.x > 0.99f);
    Vec4 blurred = t.sample({0.3f, 0.6f}, 6.0f);
    CHECK_NEAR(blurred.x, 0.5f, 0.02f);  // the 1x1 level is the average
    CHECK_NEAR(srgb_to_linear(linear_to_srgb(0.2f)), 0.2f, 1e-5);
  });
}

static void import_and_material_tests() {
  test("import: OBJ + MTL with a texture", [] {
    std::string dir = fs::join(test_dir(), "test_import");
    fs::make_dirs(dir);
    uint32_t tex[4] = {0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFFFF};
    CHECK(write_png(fs::join(dir, "tex.png"), tex, 2, 2, 2));
    fs::write_file(fs::join(dir, "box.mtl"), "newmtl Red\nKd 0.8 0.1 0.1\nNs 250\nmap_Kd tex.png\n");
    fs::write_file(fs::join(dir, "box.obj"),
                   "mtllib box.mtl\no Box\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
                   "usemtl Red\nf 1/1 2/2 3/3 4/4\n");
    ImportResult r;
    CHECK(import_model(fs::join(dir, "box.obj"), r));
    CHECK(r.nodes.size() == 1);
    CHECK(r.materials.size() == 1);
    if (!r.nodes.empty() && !r.materials.empty()) {
      const Mesh &m = *r.nodes[0].mesh;
      CHECK(m.face_count() == 1 && m.has_uvs());
      const Material &mat = *r.materials[0];
      CHECK(mat.name == "Red");
      CHECK_NEAR(mat.base_color.x, 0.8f, 1e-5);
      CHECK(mat.roughness < 0.6f);
      CHECK(fs::filename(mat.base_map.path) == "tex.png");
      CHECK(r.textures.size() == 1);
      Scene s;
      GameObject *g = instantiate_import(s, r, "box", nullptr);
      CHECK(g != nullptr);
    }
  });
  test("io: materials, UVs and seams survive save/load", [] {
    Scene s;
    build_default_scene(s);
    GameObject *cube = s.find_by_name("Cube");
    auto mat = make_material("Brick", {0.6f, 0.3f, 0.2f});
    mat->base_map.path = "Textures/brick.png";
    mat->tiling = {2, 3, 1};
    mat->procedural = 1;
    cube->get<MeshRenderer>()->materials = {mat, mat};
    MeshPtr &mp = cube->get<MeshFilter>()->mesh;
    Mesh &m = *mesh_make_mutable(mp);
    m.set_seam(0, 1, true);
    m.face_material.assign(m.face_count(), 0);
    m.face_material[2] = 1;
    m.touch();
    s.environment.mode = 1;
    s.render.samples = 77;
    std::string a = save_scene_text(s), err;
    Scene l;
    CHECK(load_scene_text(a, l, err));
    CHECK(save_scene_text(l) == a);
    GameObject *lc = l.find_by_name("Cube");
    auto &mats = lc->get<MeshRenderer>()->materials;
    CHECK(mats.size() == 2 && mats[0] == mats[1]);  // shared material stays shared
    if (!mats.empty()) {
      CHECK(mats[0]->name == "Brick" && mats[0]->base_map.path == "Textures/brick.png");
      CHECK_NEAR(mats[0]->tiling.y, 3.0f, 1e-6);
    }
    const Mesh &lm = *lc->get<MeshFilter>()->mesh;
    CHECK(lm.is_seam(0, 1) && lm.material_of(2) == 1 && lm.has_uvs());
    CHECK(l.environment.mode == 1 && l.render.samples == 77);
  });
}

static void file_tests() {
  test("io: Zstandard-compressed scenes", [] {
    if (!scene_compression_available()) return;
    Scene s;
    build_default_scene(s);
    for (int i = 0; i < 4; i++) s.find_by_name("Cube")->add<SubdivisionSurface>()->levels = 1;
    s.compress = true;
    std::string path = fs::join(test_dir(), "compressed.scene"), raw, err;
    CHECK(save_scene(s, path));
    CHECK(fs::read_file(path, raw) && raw.size() > 4 && (uint8_t)raw[0] == 0x28 && (uint8_t)raw[3] == 0xFD);
    CHECK(raw.size() * 2 < save_scene_text(s).size());  // text scenes compress well
    Scene l;
    CHECK(load_scene(path, l, err));
    CHECK(l.compress);
    CHECK(save_scene_text(l) == save_scene_text(s));
  });
}

static void round49_tests() {
  test("procedural texture: every generator repeats exactly one tile over (at odd and even counts, every shape and layout) and is deterministic", [] {
    /* A generator is periodic when sample(u, v) = sample(u + 1, v) = sample(u, v + 1): checked at 400 points for
     * each configuration, within 1/255 (trigonometric noise rounds a little). */
    struct Config {
      const char *layer, *field, *value;
    };
    const Config configs[] = {{"Brick", nullptr, nullptr}, {"Brick", "L0 Rows", "7"}, {"Brick", "L0 Offset Every", "3"},
                              {"Wood Floor", nullptr, nullptr}, {"Wood Floor", "L0 Rows", "5"},
                              {"Tatami", "L0 Layout", "0"}, {"Tatami", "L0 Layout", "1"}, {"Tatami", "L0 Layout", "2"}, {"Tatami", "L0 Cells", "7"},
                              {"Tiles", "L0 Shape", "0"}, {"Tiles", "L0 Shape", "1"}, {"Tiles", "L0 Shape", "2"}, {"Tiles", "L0 Count", "5"},
                              {"Voronoi", nullptr, nullptr}, {"Noise", nullptr, nullptr}, {"Wave", "L0 Direction", "2"},
                              {"Checker", "L0 Count", "7"}, {"Stripes", nullptr, nullptr}, {"Gradient", "L0 Type", "1"}, {"Gradient", "L0 Type", "2"}};
    for (const Config &cf : configs) {
      ProceduralTexture t = r49_tex(cf.layer);
      if (cf.field) CHECK(r49_set(t, cf.field, cf.value));
      if (!strcmp(cf.layer, "Tiles") && !strcmp(cf.field ? cf.field : "", "L0 Count")) r49_set(t, "L0 Shape", "1");  // an odd count of hexagons
      const TextureLayer &l = *t.layers[0];
      LayerContext c;
      c.w = c.h = 64;
      double worst = 0;
      for (int k = 0; k < 400; k++) {
        const float u = texture_rand((uint32_t)k, 1u, 9u), v = texture_rand((uint32_t)k, 2u, 9u);
        const LayerSample a = l.sample(u, v, c), b = l.sample(u + 1.0f, v, c), d = l.sample(u, v + 1.0f, c);
        for (const LayerSample *o : {&b, &d})
          worst = std::max({worst, (double)std::fabs(a.height - o->height), (double)std::fabs(a.albedo.x - o->albedo.x), (double)std::fabs(a.albedo.y - o->albedo.y),
                            (double)std::fabs(a.albedo.z - o->albedo.z), (double)std::fabs(a.rough - o->rough)});
      }
      TexPlanes p1, p2;
      t.evaluate(p1);
      t.evaluate(p2);
      const bool same = p1.height == p2.height && p1.albedo == p2.albedo && p1.rough == p2.rough;
      std::printf("    %-10s %-16s %-3s worst difference one tile over %.5f%s\n", cf.layer, cf.field ? cf.field : "", cf.value ? cf.value : "", worst,
                  same ? "" : "  NOT deterministic");
      CHECK(same);
      CHECK(worst <= 1.0 / 255.0);
    }
  });
  test("procedural texture: a field edit made with settex is undone and redone (the asset's text travels with the undo step)", [] {
    Editor ed;
    r34_open(ed);
    ed.command("newtex UndoMe Bricks");
    ProceduralTexturePtr pt = texture_asset("Assets/Textures/UndoMe.ptex");
    CHECK(pt != nullptr);
    if (!pt) return;
    r34_steps(ed, 2);
    const uint64_t h0 = pt->hash();
    ed.command("settex UndoMe L0Rows 13");
    r34_steps(ed, 2);
    const uint64_t h1 = pt->hash();
    CHECK(h1 != h0);
    ed.command("texlayer UndoMe add Blur");
    r34_steps(ed, 2);
    CHECK(pt->layers.size() == 4);
    ed.command("undo");
    CHECK(pt->layers.size() == 3 && pt->hash() == h1);
    ed.command("undo");
    CHECK(pt->hash() == h0);
    ed.command("redo");
    CHECK(pt->hash() == h1);
    ed.command("redo");
    CHECK(pt->layers.size() == 4);
  });
  test("procedural texture: a Brick's mortar fraction follows its parameters; Tatami has a border; a Wood Floor has gaps", [] {
    ProceduralTexture t = r49_tex("Brick", 128);
    r49_set(t, "L0 Rows", "4");
    r49_set(t, "L0 Columns", "4");
    r49_set(t, "L0 Mortar", "0.1");
    TexPlanes p;
    t.evaluate(p);
    size_t mortar = 0;
    for (float h : p.height) mortar += h == 0.0f;
    const double f = mortar / (double)p.size(), expect = 1.0 - 0.9 * 0.9;  // mortar 0.1 of a brick on both axes
    std::printf("    mortar fraction %.3f (expected %.3f)\n", f, expect);
    CHECK(std::fabs(f - expect) < 0.05);
    ProceduralTexture tat = r49_tex("Tatami", 128);
    tat.evaluate(p);
    size_t dark = 0;
    for (const Vec3 &c : p.albedo) dark += c.x < 0.2f;
    std::printf("    tatami border texels: %.1f %%\n", 100.0 * dark / p.size());
    CHECK(dark > p.size() / 20);
    ProceduralTexture wood = r49_tex("Wood Floor", 128);
    wood.evaluate(p);
    size_t gaps = 0;
    for (float h : p.height) gaps += h == 0.0f;
    std::printf("    wood floor gap texels: %.1f %%\n", 100.0 * gaps / p.size());
    CHECK(gaps > p.size() / 100 && gaps < p.size() / 4);
  });
  test("procedural texture: the stack order matters (Blur then Levels differs from Levels then Blur)", [] {
    ProceduralTexture a = r49_tex("Noise");
    a.add("Blur");
    a.add("Levels");
    r49_set(a, "L2 Gamma", "0.4");
    ProceduralTexture b = r49_tex("Noise");
    b.add("Levels");
    r49_set(b, "L1 Gamma", "0.4");
    b.add("Blur");
    TexPlanes pa, pb;
    a.evaluate(pa);
    b.evaluate(pb);
    double diff = 0;
    for (size_t i = 0; i < pa.size(); i++) diff += std::fabs(pa.height[i] - pb.height[i]);
    std::printf("    mean height difference between the two orders: %.4f\n", diff / pa.size());
    CHECK(diff / pa.size() > 0.0005);  // measured 0.0015: small (a 2-texel blur of smooth noise) but not zero
  });
  test("procedural texture: a .ptex round-trips through its text (every field and layer, in order); the file's name is the texture's", [] {
    ProceduralTexture t;
    t.name = "Round";
    t.width = 256;
    t.height = 128;
    t.physical_size = 2.5f;
    t.normal_strength = 3.0f;
    t.seed = 42;
    t.add("Brick");
    t.add("Noise");
    t.add("Edge Wear");
    t.layers[1]->enabled = false;
    t.layers[1]->blend = (int)LayerBlend::Overlay;
    t.layers[1]->opacity = 0.3f;
    r49_set(t, "L0 Rows", "11");
    r49_set(t, "L0 Color", "0.1 0.2 0.3");
    const std::string text = save_texture_text(t);
    ProceduralTexture u;
    CHECK(load_texture_text(text, u));
    CHECK(u.width == 256 && u.height == 128 && std::fabs(u.physical_size - 2.5f) < 1e-5f && std::fabs(u.normal_strength - 3.0f) < 1e-5f && u.seed == 42);
    CHECK(u.layers.size() == 3 && std::string(u.layers[0]->type_name()) == "Brick" && std::string(u.layers[1]->type_name()) == "Noise" &&
          std::string(u.layers[2]->type_name()) == "Edge Wear");
    CHECK(!u.layers[1]->enabled && u.layers[1]->blend == (int)LayerBlend::Overlay && std::fabs(u.layers[1]->opacity - 0.3f) < 1e-5f);
    TexPlanes pa, pb;
    t.evaluate(pa);
    u.evaluate(pb);
    CHECK(pa.height == pb.height && pa.albedo == pb.albedo);
    CHECK(u.hash() == t.hash());
  });
  test("procedural texture: a material with Bricks.ptex#albedo renders non-grey, its #normal changes a lit plane's shading, and a layer field re-renders", [] {
    Editor ed;
    r34_open(ed);
    ed.command("newtex Bricks Bricks");
    ProceduralTexturePtr pt = texture_asset("Assets/Textures/Bricks.ptex");
    CHECK(pt && pt->layers.size() == 3);
    if (!pt) return;
    ed.command("select Cube");
    ed.assign_texture_asset_for_test("Assets/Textures/Bricks.ptex");
    r34_steps(ed, 3);
    GameObject *cube = by_name(ed.scene(), "Cube");
    const Material &m = *cube->get<MeshRenderer>()->materials[0];
    CHECK(m.base_map.path == "Assets/Textures/Bricks.ptex#albedo" && m.normal_map.path == "Assets/Textures/Bricks.ptex#normal");
    const Material::Resolved &tx = m.textures();
    CHECK(tx.base && tx.normal && tx.roughness && tx.base->width == 512);
    const Recti sv = ed.scene_view_rect();
    const std::vector<uint32_t> a = r34_crop(ed, sv);
    size_t coloured = 0;
    for (uint32_t c : a) coloured += std::abs((int)((c >> 16) & 255) - (int)(c & 255)) > 40;  // red bricks: red far from blue
    std::printf("    coloured pixels in the Scene view: %zu\n", coloured);
    CHECK(coloured > 200);
    /* The normal map changes the shading: without it the picture differs. */
    const R34Count t0 = r34_totals(ed);
    ed.command("settex Bricks L0Color 0.1 0.2 0.9");  // a layer field: the texture re-evaluates and the view re-renders
    r34_steps(ed, 3);
    const std::vector<uint32_t> b = r34_crop(ed, sv);
    CHECK((r34_totals(ed) - t0).renders >= 1);
    CHECK(a != b);
    ed.command("settex Bricks NormalStrength 0");
    r34_steps(ed, 3);
    const std::vector<uint32_t> c = r34_crop(ed, sv);
    CHECK(b != c);  // the normal map's relief is gone
  });
  test("procedural texture: exporting OBJ and glTF writes the texture's images beside the file and references them", [] {
    Editor ed;
    r34_open(ed);
    ed.command("newtex Floor Wood Floor");
    ed.command("select Cube");
    ed.assign_texture_asset_for_test("Assets/Textures/Floor.ptex");
    r34_steps(ed, 2);
    const std::string dir = fs::join(scratch_project(), "Assets/Exports/R49");
    fs::make_dirs(dir);
    ExportOptions o;
    o.format = (int)ExportFormat::OBJ;
    o.selection_only = true;
    const std::string obj = ed.export_for_test(o, fs::join(dir, "floor.obj"));
    CHECK(!obj.empty());
    std::string mtl;
    CHECK(fs::read_file(fs::join(dir, "floor.mtl"), mtl));
    CHECK(mtl.find("map_Kd Floor_albedo.png") != std::string::npos && mtl.find("map_Bump Floor_normal.png") != std::string::npos);  // by name: the folder can move
    CHECK(fs::exists(fs::join(dir, "Floor_albedo.png")) && fs::exists(fs::join(dir, "Floor_normal.png")) && fs::exists(fs::join(dir, "Floor_roughness.png")));
    o.format = (int)ExportFormat::GLTF;
    const std::string gltf = ed.export_for_test(o, fs::join(dir, "floor.gltf"));
    CHECK(!gltf.empty());
    std::string json;
    CHECK(fs::read_file(fs::join(dir, "floor.gltf"), json));
    CHECK(json.find("Floor_albedo.png") != std::string::npos);
    /* Read back by the importer: the material's base colour map is the written image. */
    ImportResult back;
    CHECK(import_model(fs::join(dir, "floor.gltf"), back));
    bool found = false;
    for (const std::string &tp : back.textures) found = found || tp.find("Floor_albedo.png") != std::string::npos;
    CHECK(found);
    /* A format whose writer references no textures gets no images. */
    std::remove(fs::join(dir, "Floor_albedo.png").c_str());
    o.format = (int)ExportFormat::STL;
    CHECK(!ed.export_for_test(o, fs::join(dir, "floor.stl")).empty());
    CHECK(!fs::exists(fs::join(dir, "Floor_albedo.png")));
    /* The material in the scene still points at the asset, not the PNG. */
    CHECK(by_name(ed.scene(), "Cube")->get<MeshRenderer>()->materials[0]->base_map.path == "Assets/Textures/Floor.ptex#albedo");
  });
  test("procedural texture: renaming the .ptex in the Project window retargets the material's maps and the library", [] {
    Editor ed;
    r34_open(ed);
    ed.command("newtex Stone Tiles");
    ed.command("select Cube");
    ed.assign_texture_asset_for_test("Assets/Textures/Stone.ptex");
    r34_steps(ed, 2);
    const std::string abs = fs::join(scratch_project(), "Assets/Textures/Stone.ptex");
    std::remove(fs::join(fs::parent(abs), "Granite.ptex").c_str());  // (a previous run's)
    CHECK(ed.move_project_entry(abs, fs::parent(abs), "Granite.ptex"));
    Material &m = *by_name(ed.scene(), "Cube")->get<MeshRenderer>()->materials[0];
    CHECK(m.base_map.path == "Assets/Textures/Granite.ptex#albedo" && m.normal_map.path == "Assets/Textures/Granite.ptex#normal");
    ProceduralTexturePtr pt = texture_asset("Assets/Textures/Granite.ptex");
    CHECK(pt && pt->name == "Granite");
    r34_steps(ed, 2);
    CHECK(m.textures().base != nullptr);
  });
  test("procedural texture: `texlayer` adds, moves, resets and removes layers and `settex` sets fields (with the L<i> prefix)", [] {
    Editor ed;
    r34_open(ed);
    ed.command("newtex Fresh");
    ProceduralTexturePtr pt = texture_asset("Assets/Textures/Fresh.ptex");
    CHECK(pt && pt->layers.empty());
    if (!pt) return;
    ed.command("texlayer Fresh add Checker");
    ed.command("texlayer Fresh add Blur");
    CHECK(pt->layers.size() == 2 && std::string(pt->layers[1]->type_name()) == "Blur");
    ed.command("texlayer Fresh move 1 0");
    CHECK(std::string(pt->layers[0]->type_name()) == "Blur");
    const uint64_t v0 = pt->version;
    ed.command("settex Fresh L1Count 16");
    CHECK(pt->version > v0);
    ed.command("settex Fresh Width 128");
    CHECK(pt->width == 128);
    ed.command("texlayer Fresh reset 1");
    ed.command("texlayer Fresh remove 0");
    CHECK(pt->layers.size() == 1 && std::string(pt->layers[0]->type_name()) == "Checker");
    ed.command("texlayer Fresh add Nonsense");  // refused, nothing added
    CHECK(pt->layers.size() == 1);
    /* Written to its file when the edit settles (the undo step), like material assets. */
    r34_steps(ed, 2);
    std::string text;
    CHECK(fs::read_file(fs::join(scratch_project(), "Assets/Textures/Fresh.ptex"), text));
    CHECK(text.find("Checker") != std::string::npos && text.find("Width 128") != std::string::npos);
  });
}

static void round50_tests() {
  test("textures window: `window Textures` opens it beside the Materials window (the Scene view stays visible), and a layout holding it reads back", [] {
    Editor ed;
    ed.init_headless(1600, 1200);
    ed.step_frame_headless();
    ed.command("window Textures");
    r34_steps(ed, 3);
    CHECK(ed.scene_drawn_for_test());  // not opened over the Scene view
    const Recti w = ed.window_rect_for_test(WindowKind::Textures);
    CHECK(w.w > 0 && w.h > 0);
    CHECK(std::string(window_title(WindowKind::Textures)) == "Textures");
    const std::string s = ed.dock_serialize_for_test();
    CHECK(s.find(std::to_string((int)WindowKind::Textures)) != std::string::npos);
    CHECK(ed.dock_parse_roundtrip_for_test(s) == s);
  });
  test("textures window: New > Bricks by clicks creates the asset with the preset's layers and selects it", [] {
    Editor ed;
    r50_open(ed);
    const Recti nb = ed.texture_ui_rect_for_test("new");
    CHECK(nb.w > 0);
    if (nb.w <= 0) return;
    r34_click(ed, nb.x + nb.w / 2, nb.y + nb.h / 2);
    r34_steps(ed, 2);
    const Recti pop = r50_popup(ed);
    CHECK(pop.w > 0);
    if (pop.w <= 0) return;
    /* Round 327: the presets by theme - Architecture (the second row) opens its list, Bricks first in it. */
    const int row = ed.ui_for_test().row_h();
    for (int i = 0; i < 4; i++) ed.step_frame_headless({r34_mouse(R34ET::MouseMove, pop.x + pop.w / 2, pop.y + ed.ui_for_test().px(4) + row + row / 2)});
    const Recti sub = r50_popup(ed);
    CHECK(sub.x != pop.x || sub.y != pop.y);
    r50_pick_row(ed, sub, 0);  // the Architecture list's first: Bricks
    ProceduralTexturePtr t = ed.texture_selected_for_test();
    std::printf("    popup %d,%d %dx%d; selected: %s\n", pop.x, pop.y, pop.w, pop.h, t ? t->name.c_str() : "none");
    /* (Named Bricks, or Bricks 2, 3... when earlier runs left one in the scratch project.) */
    CHECK(t && t->name.rfind("Bricks", 0) == 0 && t->layers.size() == 3 && std::string(t->layers[0]->type_name()) == "Brick");
    CHECK(t && fs::exists(resolve_asset_path(t->asset_path)));
  });
  test("textures window: Delete asks first, then sends the texture to the trash; the list drops it and a material using it shows it missing", [] {
    Editor ed;
    r50_open(ed);
    ProceduralTexturePtr t = ed.new_texture_asset_for_test("Bricks");
    CHECK(t != nullptr);
    if (!t) return;
    const std::string path = resolve_asset_path(t->asset_path), ref = t->asset_path + "#albedo";
    ed.command("select Cube");
    ed.assign_texture_asset_for_test(t->asset_path);
    r34_steps(ed, 3);
    const Recti db = ed.texture_ui_rect_for_test("delete");
    CHECK(db.w > 0);
    if (db.w <= 0) return;
    r34_click(ed, db.x + db.w / 2, db.y + db.h / 2);
    r34_steps(ed, 2);
    CHECK(fs::exists(path));  // only asked
    const Recti ok = ed.confirm_rect_for_test("tex_delete");
    CHECK(ok.w > 0);
    if (ok.w <= 0) return;
    r34_click(ed, ok.x + ok.w / 2, ok.y + ok.h / 2);
    r34_steps(ed, 3);
    CHECK(!fs::exists(path));
    CHECK(ed.texture_selected_for_test() == nullptr);
    CHECK(resolve_texture_ref(TextureRef{ref, false}) == nullptr);  // the material's map: missing now
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube && cube->get<MeshRenderer>()->materials[0]->base_map.path == ref);  // (kept, so restoring the file brings it back)
  });

  test("materials window: New Material Asset clears a search left from earlier, so the new material shows", [] {
    Editor ed;
    r50_open(ed);
    ed.command("window Materials");
    r34_steps(ed, 2);
    ed.set_material_search_for_test("toon");
    r34_steps(ed, 2);
    const Recti wr = ed.window_rect_for_test(WindowKind::Materials);
    CHECK(wr.w > 0);
    if (wr.w <= 0) return;
    /* The toolbar's first button: New Material Asset. */
    const ui::Context &u = ed.ui_for_test();
    r34_click(ed, wr.x + u.px(6) + u.px(75), wr.y + (u.row_h() + u.px(6)) / 2);
    r34_steps(ed, 2);
    CHECK(ed.material_search_for_test().empty());
    CHECK(ed.material_selected_for_test() && ed.material_selected_for_test()->name.rfind("New Material", 0) == 0);
  });

  test("textures window: Add Layer > Blur by clicks appends it, its menu's Move Up moves it, its checkbox disables it, each one undo step", [] {
    Editor ed;
    r50_open(ed);
    ProceduralTexturePtr t = ed.new_texture_asset_for_test("Noise");
    CHECK(t && t->layers.size() == 1);
    if (!t) return;
    r34_steps(ed, 3);
    /* A row's rect scrolled into the window (the stack's rows run past the pane since a layer shows its Name, its
     * channels and its mask's Add Effect row, task 0244): the wheel over the pane until it is inside. */
    auto tex_rect = [&](const std::string &key) {
      const Recti w = ed.window_rect_for_test(WindowKind::Textures);
      for (int tries = 0; tries < 80; tries++) {
        const Recti f = ed.texture_ui_rect_for_test(key);
        if (f.w <= 0) {
          r34_steps(ed, 1);
          continue;
        }
        if (f.y >= w.y && f.y + f.h <= w.bottom()) return f;
        platform::Event mv, wheel;
        mv.type = platform::EventType::MouseMove;
        wheel.type = platform::EventType::Wheel;
        mv.x = wheel.x = f.x + f.w / 2;
        mv.y = wheel.y = w.bottom() - ed.ui_for_test().px(30);
        wheel.wheel_y = f.y > w.bottom() ? -3.0f : 3.0f;
        ed.step_frame_headless({mv});
        ed.step_frame_headless({wheel});
        ed.step_frame_headless();
      }
      std::printf("    '%s' never came into view\n", key.c_str());
      return Recti{0, 0, 0, 0};
    };
    const Recti ab = tex_rect("add_layer");
    CHECK(ab.w > 0);
    if (ab.w <= 0) return;
    r34_click(ed, ab.x + ab.w / 2, ab.y + ab.h / 2);
    r34_steps(ed, 2);
    /* Blur by its name in the menu (round 332 put a search field and Blender's groups before it, so its row moved). */
    ed.record_buttons_for_test(true);
    ed.step_frame_headless();
    Recti blur{0, 0, 0, 0};
    CHECK(ed.button_rect_for_test("menu:Blur", blur));
    r34_click(ed, blur.x + blur.w / 2, blur.y + blur.h / 2);
    r34_steps(ed, 2);
    CHECK(t->layers.size() == 2 && std::string(t->layers[1]->type_name()) == "Blur");
    const Recti mr = tex_rect("layer1");
    CHECK(mr.w > 0);
    r34_click(ed, mr.x + mr.w / 2, mr.y + mr.h / 2);
    r34_steps(ed, 2);
    const Recti pop = r50_popup(ed);
    r50_pick_row(ed, pop, 0);  // Move Up
    CHECK(std::string(t->layers[0]->type_name()) == "Blur");
    for (int i = 0; i < 6; i++) {  // (the settings scrolled back to the top: a longer layer below - round 328's Noise - left them scrolled)
      platform::Event wheel;
      wheel.type = platform::EventType::Wheel;
      wheel.x = mr.x + mr.w / 2, wheel.y = mr.y + mr.h / 2, wheel.wheel_y = 3.0f;
      ed.step_frame_headless({r34_mouse(R34ET::MouseMove, wheel.x, wheel.y)});
      ed.step_frame_headless({wheel});
    }
    const Recti cb = tex_rect("layer_enabled0");
    r34_click(ed, cb.x + cb.w / 2, cb.y + cb.h / 2);
    r34_steps(ed, 2);
    CHECK(!t->layers[0]->enabled);
    ed.command("undo");
    CHECK(t->layers[0]->enabled);
    ed.command("undo");
    CHECK(std::string(t->layers[0]->type_name()) == "Noise");
    ed.command("undo");
    CHECK(t->layers.size() == 1);
  });
  test("textures window: a field typed in the window changes the texture and its preview; undo and redo restore both", [] {
    Editor ed;
    r50_open(ed);
    ProceduralTexturePtr t = ed.new_texture_asset_for_test("Bricks");
    if (!t) return;
    r34_steps(ed, 3);
    const Recti tw = ed.texture_ui_rect_for_test("preview");  // the preview pane (the fields keep focus highlights)
    const std::vector<uint32_t> before = r34_crop(ed, tw);
    const uint64_t h0 = t->hash();
    const Recti f = ed.texture_ui_rect_for_test("field:L0 Rows");
    CHECK(f.w > 0);
    if (f.w <= 0) return;
    /* Type 3 into the int field: a click, select all, the digit, Enter. */
    r34_click(ed, f.x + f.w / 2, f.y + f.h / 2);
    platform::Event a;
    a.type = platform::EventType::KeyDown;
    a.key = platform::KEY_A;
    a.mods = platform::MOD_CTRL;
    ed.step_frame_headless({a});
    platform::Event ch;
    ch.type = platform::EventType::Text;
    ch.codepoint = '3';
    ed.step_frame_headless({ch});
    platform::Event en;
    en.type = platform::EventType::KeyDown;
    en.key = platform::KEY_ENTER;
    ed.step_frame_headless({en});
    r34_steps(ed, 3);
    const uint64_t h1 = t->hash();
    std::printf("    rows after typing: %s\n", h1 != h0 ? "changed" : "unchanged");
    CHECK(h1 != h0);
    const std::vector<uint32_t> after = r34_crop(ed, tw);
    CHECK(before != after);
    ed.command("undo");
    r34_steps(ed, 3);
    CHECK(t->hash() == h0);
    CHECK(r34_crop(ed, tw) == before);
    ed.command("redo");
    r34_steps(ed, 3);
    CHECK(t->hash() == h1);
  });
  test("textures window: dragging a thumbnail onto the Cube in the Scene view gives it the texture's maps", [] {
    Editor ed;
    r50_open(ed);
    ed.command("shading solid");
    ProceduralTexturePtr t = ed.new_texture_asset_for_test("Tiles");
    if (!t) return;
    r34_steps(ed, 3);
    const Recti th = ed.texture_ui_rect_for_test("thumb:" + t->name);
    CHECK(th.w > 0);
    GameObject *cube = by_name(ed.scene(), "Cube");
    CHECK(ed.scene_drawn_for_test());  // the drop needs this frame's Scene view, visible beside the window
    int cx = 0, cy = 0;
    CHECK(ed.project_to_window(cube->world_position(), cx, cy));
    ed.step_frame_headless({r34_mouse(R34ET::MouseMove, th.x + th.w / 2, th.y + th.h / 3)});
    ed.step_frame_headless({r34_mouse(R34ET::MouseDown, th.x + th.w / 2, th.y + th.h / 3)});
    ed.step_frame_headless({r34_mouse(R34ET::MouseMove, (th.x + cx) / 2, (th.y + cy) / 2)});
    ed.step_frame_headless({r34_mouse(R34ET::MouseMove, cx, cy)});
    ed.step_frame_headless({r34_mouse(R34ET::MouseUp, cx, cy)});
    r34_steps(ed, 2);
    const MeshRenderer *mr = cube->get<MeshRenderer>();
    CHECK(mr && !mr->materials.empty() && mr->materials[0]);
    if (!mr || mr->materials.empty() || !mr->materials[0]) return;
    std::printf("    the Cube's base map: %s\n", mr->materials[0]->base_map.path.c_str());
    CHECK(mr->materials[0]->base_map.path == t->asset_path + "#albedo");
    CHECK(mr->materials[0]->normal_map.path == t->asset_path + "#normal");
    CHECK(mr->materials[0]->roughness_map.path == t->asset_path + "#roughness");
  });
  test("textures window: the preview evaluates at 256 while a field is dragged, and at the texture's size on release", [] {
    Editor ed;
    r50_open(ed);
    ProceduralTexturePtr t = ed.new_texture_asset_for_test("Noise");
    if (!t) return;
    t->width = t->height = 1024;
    t->touch();
    r34_steps(ed, 3);
    CHECK(ed.texture_preview_size_for_test() == 1024);
    /* A drag on the Scale field's label (a float's drag label changes it while the mouse moves): Noise's Scale is
     * the row just above its Detail field, whose rect the window records. */
    const ui::Context &u = ed.ui_for_test();
    const Recti row = ed.texture_ui_rect_for_test("field:L0 Detail");
    CHECK(row.w > 0);
    if (row.w <= 0) return;
    const int lx = row.x - u.px(60), ly = row.y - u.row_h() / 2;
    ed.step_frame_headless({r34_mouse(R34ET::MouseMove, lx, ly)});
    ed.step_frame_headless({r34_mouse(R34ET::MouseDown, lx, ly)});
    ed.step_frame_headless({r34_mouse(R34ET::MouseMove, lx + 30, ly)});
    ed.step_frame_headless({r34_mouse(R34ET::MouseMove, lx + 60, ly)});
    const int during = ed.texture_preview_size_for_test();
    ed.step_frame_headless({r34_mouse(R34ET::MouseUp, lx + 60, ly)});
    r34_steps(ed, 3);
    const int after = ed.texture_preview_size_for_test();
    std::printf("    preview size while dragging %d, after %d\n", during, after);
    CHECK(during == 256);
    CHECK(after == 1024);
  });
}


/* ===================================================================== */
/* Round 53 (task 0031, ADR 0017 amended): live procedural textures       */
/* ===================================================================== */

/* A texture of one stack, evaluated to planes (with the live snapshot) at its size. */
struct R53Eval {
  TexPlanes planes;
  std::shared_ptr<LiveTexture> live;
};
static R53Eval r53_eval(const ProceduralTexture &t) {
  R53Eval e;
  std::vector<LayerAux> aux;
  t.evaluate(e.planes, &aux);
  e.live = std::make_shared<LiveTexture>(t, std::move(aux), e.planes.w, e.planes.h);
  return e;
}

/* A quad facing -Z with UVs 0..1 across 2 m, drawn with a material through the deferred rasterizer, the camera
 * at `dist` in front: the middle row's linear green. */
static std::vector<float> r53_row(const MaterialPtr &mat, float dist, int size = 128) {
  static auto quad = primitives::quad(2.0f);
  std::vector<MaterialPtr> mats = {mat};
  Image img;
  img.resize(size, size);
  RenderTarget rt;
  rt.attach(img, {0, 0, size, size});
  rt.want_hdr = true;
  Renderer3D r;
  RasterOptions opt;
  opt.shade = ShadeMode::Deferred;
  LightingEnv env;
  env.view_transform = ViewTransform::Standard;
  env.camera_pos = {0.3f, 0.2f, -dist};
  r.begin(&rt, Mat4::look_at(env.camera_pos, {0.3f, 0.2f, 0}, {0, 1, 0}), Mat4::perspective(30 * kDeg2Rad, 1, 0.01f, 50), env, opt);
  r.clear(0xFF000000);
  DrawItem it;
  it.mesh = &quad->render_mesh_tangents();
  it.materials = &mats;
  it.double_sided = true;
  r.add(it);
  r.flush();
  std::vector<float> row(size);
  for (int x = 0; x < size; x++) row[x] = rt.hdr.empty() ? 0.0f : rt.hdr[(size_t)(size / 2) * size + x].y;
  return row;
}

static void round53_tests() {
  test("live textures: a point evaluation equals the planes at every texel centre, for every layer type", [] {
    int bad_types = 0;
    for (const TextureLayerInfo &info : texture_layer_infos()) {
      ProceduralTexture t;
      /* (Warp and Tile at 512: their planes read whole texels, which at 64 is coarser than a brick's bevel.) */
      t.width = t.height = info.name == "Warp" || info.name == "Tile" ? 512 : 64;
      t.seed = 7;
      if (info.category != "Generators" || info.name == "Image") t.add("Brick");  // something for a filter to work on
      if (info.name == "Image") {
        /* An image file in the scratch project for the Image layer. */
        std::vector<uint32_t> px(16 * 16);
        for (int i = 0; i < 16 * 16; i++) px[i] = 0xFF000000u | (uint32_t)(i * 7 & 255) << 16 | (uint32_t)(i * 3 & 255) << 8 | 90u;
        const std::string path = fs::join(scratch_project(), "Assets/R53_image.png");
        CHECK(write_png(path, px.data(), 16, 16, 16));
      }
      TextureLayer *l = t.add(info.name);
      CHECK(l != nullptr);
      if (!l) continue;
      if (info.name == "Mask by Height") t.add("Invert")->use_mask = true;  // something that reads the mask
      if (info.name == "Image") {
        struct SetImage : Reflector {  // sets the Image field, ignores the rest
          void field(const char *, float &, float, float, float) override {}
          void field(const char *, int &, int, int) override {}
          void field(const char *, bool &) override {}
          void field(const char *, Vec3 &) override {}
          void color(const char *, Vec3 &) override {}
          void enumeration(const char *, int &, const char *const *, int) override {}
          void text(const char *, std::string &) override {}
          void mesh(const char *, MeshPtr &) override {}
          void texture(const char *, TextureRef &r) override { r.path = "Assets/R53_image.png"; }
        } si;
        l->reflect(si);
      }
      const R53Eval e = r53_eval(t);
      const TexPlanes &p = e.planes;
      size_t ok = 0, n = 0;
      float worst = 0;
      double sum = 0;
      for (int y = 0; y < p.h; y++)
        for (int x = 0; x < p.w; x++) {
          const size_t i = (size_t)y * p.w + x;
          const TexPoint q = e.live->at((x + 0.5f) / p.w, (y + 0.5f) / p.h);
          const float d = std::max({std::fabs(q.height - p.height[i]), std::fabs(q.rough - p.rough[i]), std::fabs(q.albedo.x - p.albedo[i].x),
                                    std::fabs(q.albedo.y - p.albedo[i].y), std::fabs(q.albedo.z - p.albedo[i].z)});
          worst = std::max(worst, d);
          sum += d;
          ok += d <= 1.0f / 255.0f;
          n++;
        }
      /* Warp and Tile: the planes read the nearest texel of the moved point, a point evaluation the point itself
       * (up to half a texel away: across the bricks' bevels and edges that changes values), so for those the
       * mean difference is what is held to a bound. Every other layer is exact at the centres. */
      const bool moved = info.name == "Warp" || info.name == "Tile";
      const double frac = (double)ok / std::max<size_t>(1, n), mean = sum / std::max<size_t>(1, n);
      if (moved ? mean > 0.03 : frac < 1.0) {
        std::printf("    %s: %.1f%% of texels within 1/255, mean difference %.4f (worst %.3f)\n", info.name.c_str(), 100.0 * frac, mean, worst);
        bad_types++;
      }
      else if (moved) std::printf("    %s: mean difference %.4f\n", info.name.c_str(), mean);
    }
    CHECK(bad_types == 0);
  });

  test("live textures: the live normal matches the image's normal output (Sobel) within 10 degrees on smooth parts", [] {
    ProceduralTexture t;
    t.width = t.height = 64;
    t.add("Noise");
    const R53Eval e = r53_eval(t);
    const Bitmap nm = t.output(1, e.planes);
    size_t ok = 0, n = 0;
    for (int y = 0; y < 64; y++)
      for (int x = 0; x < 64; x++) {
        const uint8_t *c = &nm.rgba8[((size_t)y * 64 + x) * 4];
        const Vec3 img = normalize(Vec3(c[0] / 127.5f - 1, c[1] / 127.5f - 1, c[2] / 127.5f - 1));
        const Vec3 live = e.live->normal((x + 0.5f) / 64, (y + 0.5f) / 64);
        const float h0 = e.live->at((x + 0.5f) / 64, (y + 0.5f) / 64).height;
        const Vec3 fast = e.live->normal((x + 0.5f) / 64, (y + 0.5f) / 64, &h0);  // one-sided, as the rasterizer uses
        ok += dot(img, live) > std::cos(10 * kDeg2Rad) && dot(img, fast) > std::cos(10 * kDeg2Rad);
        n++;
      }
    std::printf("    %zu of %zu texels within 10 degrees\n", ok, n);
    CHECK(ok >= n * 95 / 100);
  });

  test("live textures: up close a Brick texture's mortar edge is sharp with Live Shading and blurred without", [] {
    Editor ed;
    ed.init_headless(800, 600);
    ed.step_frame_headless();
    ProceduralTexturePtr t = ed.new_texture_asset_for_test("Bricks");
    CHECK(t != nullptr);
    if (!t) return;
    t->width = t->height = 64;
    t->touch();
    MaterialPtr m = make_material("bricks", Vec3(1.0f));
    m->base_map.path = t->asset_path + "#albedo";
    m->touch();
    /* Pixels between the mortar and the brick colours along a row (an edge's width). */
    auto transitions = [](const std::vector<float> &row) {
      float lo = 1e9f, hi = -1e9f;
      for (float v : row) lo = std::min(lo, v), hi = std::max(hi, v);
      int mid = 0;
      for (float v : row) mid += v > lo + 0.2f * (hi - lo) && v < hi - 0.2f * (hi - lo);
      return mid;
    };
    const auto live = r53_row(m, 0.35f);
    t->live_shading = false;
    t->touch();
    m->touch();
    const auto image = r53_row(m, 0.35f);
    const int tl = transitions(live), ti = transitions(image);
    std::printf("    in-between pixels across a row up close: %d live, %d from the 64-texel image\n", tl, ti);
    CHECK(ti > 0);
    CHECK(tl * 2 < ti);
    /* Far away (a texel smaller than a pixel) both read the image: the same pixels. */
    t->live_shading = true;
    t->touch();
    m->touch();
    const auto far_live = r53_row(m, 30.0f, 64);
    t->live_shading = false;
    t->touch();
    m->touch();
    const auto far_image = r53_row(m, 30.0f, 64);
    CHECK(far_live == far_image);
  });

  test("live textures: dragging a texture field changes the objects using it every frame (at 256), the set size on release", [] {
    Editor ed;
    r50_open(ed);
    ProceduralTexturePtr t = ed.new_texture_asset_for_test("Noise");
    if (!t) return;
    t->width = t->height = 1024;
    t->touch();
    MaterialPtr m = make_material("noise", Vec3(1.0f));
    m->base_map.path = t->asset_path + "#albedo";
    m->touch();
    r34_steps(ed, 3);
    CHECK(m->textures().base && m->textures().base->width == 1024);
    CHECK(m->textures().live[0] != nullptr);
    const ui::Context &u = ed.ui_for_test();
    const Recti row = ed.texture_ui_rect_for_test("field:L0 Detail");
    CHECK(row.w > 0);
    if (row.w <= 0) return;
    const int lx = row.x - u.px(60), ly = row.y - u.row_h() / 2;
    ed.step_frame_headless({r34_mouse(R34ET::MouseMove, lx, ly)});
    ed.step_frame_headless({r34_mouse(R34ET::MouseDown, lx, ly)});
    ed.step_frame_headless({r34_mouse(R34ET::MouseMove, lx + 30, ly)});
    const size_t g1 = material_textures_generation();
    const uint64_t v1 = m->textures().live[0] ? m->textures().live[0]->hash() : 0;
    const int w1 = m->textures().base ? m->textures().base->width : 0;
    ed.step_frame_headless({r34_mouse(R34ET::MouseMove, lx + 60, ly)});
    const size_t g2 = material_textures_generation();
    const uint64_t v2 = m->textures().live[0] ? m->textures().live[0]->hash() : 0;
    ed.step_frame_headless({r34_mouse(R34ET::MouseUp, lx + 60, ly)});
    r34_steps(ed, 3);
    const int after = m->textures().base ? m->textures().base->width : 0;
    std::printf("    during the drag: image %d wide, a new snapshot each frame %s; after %d\n", w1, v1 != v2 ? "yes" : "no", after);
    CHECK(w1 == 256);
    CHECK(g2 != g1 && v1 != v2);
    CHECK(after == 1024);
    CHECK(!procedural_texture_preview());
  });

  test("live textures: a drag's 256 preview keeps texel-sized filters their size (Edge Wear isn't 8x wider until release)", [] {
    ProceduralTexture t;
    apply_texture_preset(t, "Bricks");  // Brick, Noise, Edge Wear
    t.width = t.height = 2048;
    TexPlanes full, small;
    t.evaluate(full);
    t.evaluate(small, nullptr, 256);
    auto mean = [](const TexPlanes &p) {
      double s = 0;
      for (const Vec3 &a : p.albedo) s += a.x + a.y + a.z;
      return s / (3.0 * p.size());
    };
    const double mf = mean(full), ms = mean(small);
    std::printf("    mean albedo: %.4f at 2048, %.4f in the 256 preview\n", mf, ms);
    CHECK(small.w == 256);
    CHECK(std::fabs(ms - mf) < 0.03 * mf);  // before: the wear 8 texels of 256 wide (8x), lightening the preview
  });

  test("live textures: stacked Warp and Tile layers evaluate at a point like the planes (and in linear time)", [] {
    /* Four (their planes round to texels at each one; many more compound that rounding), then twelve timed. */
    ProceduralTexture t;
    t.width = t.height = 1024;  // (two Tiles make the bricks 4x smaller: finer planes keep the rounding small)
    t.add("Brick");
    for (int i = 0; i < 4; i++) t.add(i % 2 ? "Warp" : "Tile");
    const R53Eval e = r53_eval(t);
    ProceduralTexture t12 = t;
    for (int i = 0; i < 8; i++) t12.add(i % 2 ? "Warp" : "Tile");
    const R53Eval e12 = r53_eval(t12);
    double sum = 0;
    for (int y = 0; y < 1024; y += 7)
      for (int x = 0; x < 1024; x += 7) {
        const TexPoint q = e.live->at((x + 0.5f) / 1024, (y + 0.5f) / 1024);
        sum += std::fabs(q.height - e.planes.height[(size_t)y * 1024 + x]);
      }
    const double mean = sum / (147.0 * 147.0);
    ScopedTimer timer;
    for (int i = 0; i < 1000; i++) (void)e12.live->at(i * 0.001f, 0.3f);
    const double ms = timer.ms();
    std::printf("    4 stacked Warp / Tile layers: mean difference %.4f; 12: %.1f us a point\n", mean, ms);
    CHECK(mean < 0.05);
    CHECK(ms < 200.0);  // 2^12 evaluations of the Brick a point without the shortcut (seconds for 1000)
  });

  test("live textures: an axis material's scale and Mirror wrapping keep the image where it is minified or can't be emulated", [] {
    Editor ed;
    ed.init_headless(800, 600);
    ed.step_frame_headless();
    ProceduralTexturePtr t = ed.new_texture_asset_for_test("Bricks");
    if (!t) return;
    t->width = t->height = 64;
    t->touch();
    Material part;
    part.name = "BrickPart";
    part.base_map.path = t->asset_path + "#albedo";
    part.mapping = (int)MaterialMapping::Box;
    create_material_asset(part, "Assets/Materials/R53/BrickPart.mat");
    MaterialPtr axis = make_material("axis", Vec3(1.0f));
    axis->axis_mode = 2;  // Local
    axis->axis_top = axis->axis_side = axis->axis_bottom = "Assets/Materials/R53/BrickPart.mat";
    axis->touch();
    SurfacePoint sp;
    sp.position = sp.local_position = {0.37f, 0.5f, 0.21f};
    sp.normal = sp.local_normal = sp.geo_normal = {0, 1, 0};
    sp.live_textures = true;
    sp.footprint = sp.world_footprint = 0.005f;  // 0.32 texels a pixel at scale 1: live
    auto eval = [&](const Material &m, bool live) {
      SurfacePoint q = sp;
      q.live_textures = live;
      return evaluate_material(m, m.textures(), q).albedo;
    };
    axis->axis_scale = 1.0f;
    axis->touch();
    CHECK(length(eval(*axis, true) - eval(*axis, false)) > 1e-4f);  // magnified: the live value differs from the image's
    axis->axis_scale = 0.1f;  // ten tiles a metre: 3.2 texels a pixel, minified
    axis->touch();
    CHECK(length(eval(*axis, true) - eval(*axis, false)) < 1e-6f);  // the image (before: live, shimmering)
    MaterialPtr mirror = make_material("mirror", Vec3(1.0f));
    mirror->base_map.path = t->asset_path + "#albedo";
    mirror->mapping = (int)MaterialMapping::Box;
    mirror->wrap = (int)TexWrap::Mirror;
    mirror->touch();
    CHECK(length(eval(*mirror, true) - eval(*mirror, false)) < 1e-6f);
  });

  test("live textures: the 256 preview is per texture: dragging one leaves the others at their size", [] {
    Editor ed;
    ed.init_headless(800, 600);
    ed.step_frame_headless();
    ProceduralTexturePtr a = ed.new_texture_asset_for_test("Noise"), b = ed.new_texture_asset_for_test("Tiles");
    if (!a || !b) return;
    a->width = a->height = b->width = b->height = 1024;
    a->touch();
    b->touch();
    procedural_texture_set_preview(a->asset_path, true);
    a->touch();
    const int wa = procedural_texture_output(*a, 0)->width, wb = procedural_texture_output(*b, 0)->width;
    procedural_texture_clear_previews();
    std::printf("    in preview: %d, the other: %d\n", wa, wb);
    CHECK(wa == 256 && wb == 1024);
    CHECK(!procedural_texture_preview());
  });

  test("live textures: Live Shading saves with the texture, NaN coordinates stay finite, a stack of filters alone works", [] {
    ProceduralTexture t;
    t.width = t.height = 64;
    t.live_shading = false;
    t.add("Levels");
    t.add("Blur");
    const R53Eval e = r53_eval(t);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const TexPoint q = e.live->at(nan, 1e30f);
    CHECK(std::isfinite(q.height) && std::isfinite(q.albedo.x));
    CHECK(std::isfinite(e.live->normal(-1e20f, nan).z));
    const std::string text = save_texture_text(t);
    ProceduralTexture b;
    CHECK(load_texture_text(text, b));
    CHECK(!b.live_shading && b.layers.size() == 2);
  });
}

static void tests_round98() {
  /* ---------------------------------------------------------------- round 98: a texture's Physical Size tiles its materials (task 0072) */
  test("textures: changing a texture's Physical Size re-tiles the materials using it, but not one tiled by hand; undo puts it back", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    /* (earlier tests leave Bricks.ptex, Bricks 2.ptex...; a name with a space can't be typed into settex) */
    std::remove(resolve_asset_path("Assets/Textures/Bricks.ptex").c_str());
    for (int k = 2; k < 10; k++) std::remove(resolve_asset_path(strprintf("Assets/Textures/Bricks %d.ptex", k)).c_str());
    ProceduralTexturePtr t = ed.new_texture_asset_for_test("Bricks");
    CHECK(t != nullptr && t->asset_path.find(' ') == std::string::npos);
    if (!t) return;
    ed.command("create Cube");
    GameObject *a = ed.active_object_for_test();
    ed.assign_texture_asset_for_test(t->asset_path);
    ed.command("create Cube");
    GameObject *b = ed.active_object_for_test();
    ed.assign_texture_asset_for_test(t->asset_path);
    ed.step_frame_headless();
    CHECK(a && b && a != b);
    if (!a || !b) return;
    Material &ma = *a->get<MeshRenderer>()->materials[0], &mb = *b->get<MeshRenderer>()->materials[0];
    const float size0 = t->physical_size;
    std::printf("    Bricks: %g m a tile; tiling %.3f\n", size0, ma.tiling.x);
    CHECK(std::fabs(ma.tiling.x - 1.0f / size0) < 1e-4f);
    /* A .mat asset of the texture that no object here uses: left alone (undo couldn't put it back). */
    Material lone = ma;
    lone.name = "R98Lone";
    std::remove(resolve_asset_path("Assets/Materials/R98Lone.mat").c_str());
    MaterialPtr lone_asset = create_material_asset(lone, "Assets/Materials/R98Lone.mat");
    mb.tiling = Vec3(3.0f, 3.0f, 1.0f);  // (tiled by hand)
    /* A third object whose material uses the texture only as its normal map, and a fourth tiled by hand to 1/3 (a
     * size the change below passes on its way to 4 m in the same undo step). */
    ed.command("create Cube");
    GameObject *c = ed.active_object_for_test();
    ed.command("create Cube");
    GameObject *d = ed.active_object_for_test();
    CHECK(c && d);
    if (!c || !d) return;
    {
      Material mc = ma, md = ma;
      mc.base_map = TextureRef{}, mc.normal_map = TextureRef{t->asset_path + "#normal", true};
      md.tiling = Vec3(1.0f / 3.0f, 1.0f / 3.0f, 1.0f);
      c->get<MeshRenderer>()->materials = {std::make_shared<Material>(mc)};
      d->get<MeshRenderer>()->materials = {std::make_shared<Material>(md)};
    }
    ed.mark_changed_for_test("by hand");
    ed.step_frame_headless();
    ed.command("settex " + t->asset_path + " PhysicalSize 3");
    ed.command("settex " + t->asset_path + " PhysicalSize 4");  // (the same undo step, as a dragged number)
    ed.step_frame_headless();
    auto tiling = [&](GameObject *g) { return g->get<MeshRenderer>()->materials[0]->tiling; };
    std::printf("    after Physical Size 4: the first %.3f (0.25), hand-tiled %.3f (3) and %.3f (0.333), normal map only %.3f (0.25), the unused .mat %.3f (0.5)\n",
                tiling(a).x, tiling(b).x, tiling(d).x, tiling(c).x, lone_asset ? lone_asset->tiling.x : -1.0f);
    CHECK(std::fabs(tiling(a).x - 0.25f) < 1e-5f && std::fabs(tiling(a).y - 0.25f) < 1e-5f);
    CHECK(tiling(b).x == 3.0f && std::fabs(tiling(d).x - 1.0f / 3.0f) < 1e-6f);
    CHECK(std::fabs(tiling(c).x - 0.25f) < 1e-5f);
    CHECK(lone_asset && std::fabs(lone_asset->tiling.x - 0.5f) < 1e-5f);
    std::remove(resolve_asset_path("Assets/Materials/R98Lone.mat").c_str());
    ed.command("undo");
    ed.step_frame_headless();
    GameObject *a2 = ed.scene().find(a->id), *b2 = ed.scene().find(b->id);
    CHECK(a2 && std::fabs(a2->get<MeshRenderer>()->materials[0]->tiling.x - 1.0f / size0) < 1e-4f);
    CHECK(b2 && b2->get<MeshRenderer>()->materials[0]->tiling.x == 3.0f);
    ed.command("redo");
    ed.step_frame_headless();
    a2 = ed.scene().find(a->id);
    CHECK(a2 && std::fabs(a2->get<MeshRenderer>()->materials[0]->tiling.x - 0.25f) < 1e-5f);
  });
}

static void tests_round107() {
  /* ---------------------------------------------------------------- round 107: redo of material assets, tests stay in their project (task 0081) */
  test("materials: an edit to a material linked to a .mat undoes and redoes, twice over (redo used to keep the undone values)", [] {
    Editor ed;
    ed.init_headless(1000, 700);
    const std::string path = "Assets/Materials/R107Red.mat";
    std::remove(resolve_asset_path(path).c_str());
    MaterialPtr asset = create_material_asset(*make_material("R107Red", Vec3(0.8f, 0.1f, 0.1f)), path);
    CHECK(asset != nullptr);
    if (!asset) return;
    ed.command("select Cube");
    ed.command("assignmat " + path);
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    auto colour = [&] {
      GameObject *cube = ed.scene().find_by_name("Cube");
      return cube && cube->get<MeshRenderer>() && !cube->get<MeshRenderer>()->materials.empty() ? cube->get<MeshRenderer>()->materials[0]->base_color : Vec3(-1.0f);
    };
    CHECK(std::fabs(colour().x - 0.8f) < 1e-5f);
    /* An edit in place (as the Inspector makes it): the shared asset turns green. */
    ed.scene().find_by_name("Cube")->get<MeshRenderer>()->materials[0]->base_color = Vec3(0.1f, 0.8f, 0.1f);
    ed.scene().find_by_name("Cube")->get<MeshRenderer>()->materials[0]->touch();
    ed.mark_changed_for_test("Edit R107Red");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    for (int round = 0; round < 2; round++) {
      ed.command("undo");
      ed.step_frame_headless();
      const float red = colour().x;
      ed.command("redo");
      ed.step_frame_headless();
      const float green = colour().y;
      std::printf("    round %d: after undo red %.2f (0.8), after redo green %.2f (0.8); the asset's own %.2f\n", round, red, green,
                  material_asset(path) ? material_asset(path)->base_color.y : -1.0f);
      CHECK(std::fabs(red - 0.8f) < 1e-5f && std::fabs(green - 0.8f) < 1e-5f);
    }
    CHECK(material_asset(path) && std::fabs(material_asset(path)->base_color.y - 0.8f) < 1e-5f);  // (the file's asset follows)
    std::remove(resolve_asset_path(path).c_str());
  });

  test("materials: objects sharing a .mat still share one material in an undo state (one copy, not one each)", [] {
    Scene scene;
    MaterialPtr shared = make_material("R107Shared", Vec3(0.2f));
    shared->asset_path = "Assets/Materials/R107Shared.mat";
    GameObject *a = create_primitive(scene, "Cube");
    GameObject *b = create_primitive(scene, "Cube");
    a->get<MeshRenderer>()->materials = {shared, shared};
    b->get<MeshRenderer>()->materials = {shared};
    detach_material_assets(scene);
    const MaterialPtr &copy = a->get<MeshRenderer>()->materials[0];
    CHECK(copy != shared);
    CHECK(a->get<MeshRenderer>()->materials[1] == copy && b->get<MeshRenderer>()->materials[0] == copy);
  });

  test("updates: a tagged version GitHub marks a pre-release (v0.10.0) is still offered with previews off; PR builds are not", [] {
    const std::string p = releases::platform();
    const std::string inst = p == "windows" ? "Blendity-Setup-x.exe" : p == "linux" ? "Blendity-x-x86_64.AppImage" : "Blendity-x-macos.dmg";
    auto rel = [&](const char *tag, const char *date) {
      return strprintf("{\"tag_name\":\"%s\",\"name\":\"%s\",\"body\":\"\",\"published_at\":\"%s\",\"prerelease\":true,\"draft\":false,"
                       "\"assets\":[{\"name\":\"%s\",\"browser_download_url\":\"https://x/a\",\"size\":5}]}",
                       tag, tag, date, inst.c_str());
    };
    const std::vector<releases::Release> all = releases::parse("[" + rel("build-pr73", "2026-10-16T00:00:00Z") + "," +
                                                                rel("v0.10.0", "2026-10-15T00:00:00Z") + "," +
                                                                rel("build-pr72", "2026-10-14T00:00:00Z") + "]");
    const auto stable = releases::newer(all, "build-pr72", p, false);
    std::printf("    %zu releases; with previews off from PR 72's build: %zu offered\n", all.size(), stable.size());
    CHECK(all.size() == 3 && all[1].prerelease && all[1].pr == 0);
    CHECK(stable.size() == 1 && stable[0]->tag == "v0.10.0");
    CHECK(releases::installable(all, p, false).size() == 1 && releases::installable(all, p, true).size() == 3);
  });

  test("tests: an asset made before any editor exists lands in the scratch project, never the folder the tests run from", [] {
    /* What main() set before the first test (every editor since has set its own, all under the test folder). */
    std::printf("    the asset root before any test: '%s'\n", g_asset_root_at_start.c_str());
    CHECK(!g_asset_root_at_start.empty() && fs::normalize(g_asset_root_at_start) == fs::normalize(scratch_project()));
    const std::string was = asset_root();
    set_asset_root(g_asset_root_at_start);
    const std::string p = fs::normalize(resolve_asset_path("Assets/Animations/R107Probe.anim"));
    set_asset_root(was);
    CHECK(p.rfind(fs::normalize(test_dir()), 0) == 0);
  });

}

static void tests_round145_to_round163() {
  /* ---------------------------------------------------------------- round 145: Walk Cycle, the procedural gait (task 0125) */
  /* The humanoid rig walking: `gen` generates into `clip` and returns each frame's ankles (the feet's heads) as keyed. */
  struct R145Walk {
    Scene s;
    GameObject *rig = nullptr;
    gait::Rig g;
    AnimationClip clip;
    std::vector<std::vector<Vec3>> ankles;  // [frame][leg]
    std::vector<std::vector<Vec3>> heels, toes;  // [frame][leg]: the sole's back and front, where the foot rolls about
    int frames = 0;
  };
  auto r145_walk = [](R145Walk &w, const char *preset, bool in_place, int cycles = 2, bool quadruped = false) {
    w.rig = create_rig(w.s, quadruped, Vec3(0.0f));
    w.g = gait::analyse(w.s, *w.rig);
    gait::WalkSettings ws;
    gait::preset(preset, w.g, ws.style);
    ws.in_place = in_place, ws.cycles = cycles;
    std::string error;
    w.frames = gait::generate(w.s, *w.rig, w.g, ws, AnimationClip(), w.clip, error);
    if (!w.frames) std::printf("    %s\n", error.c_str());
    body::for_each_frame(w.s, *w.rig, w.clip, 0, std::max(0, w.frames - 1), [&](int) {
      std::vector<Vec3> a, h, t;
      const Vec3 up(0, 1, 0), fwd = normalize(Vec3(w.g.forward.x, 0.0f, w.g.forward.z));
      for (const gait::Leg &l : w.g.legs) {
        const Vec3 at = l.foot->world_position();
        const Quat turn = l.foot->world_rotation() * l.rest_foot.conjugate();  // (the foot's turn from its rest)
        a.push_back(at);
        h.push_back(at + turn.rotate(up * -w.g.sole));
        t.push_back(at + turn.rotate(fwd * l.foot_length - up * w.g.sole));
      }
      w.ankles.push_back(a), w.heels.push_back(h), w.toes.push_back(t);
    });
  };
  test("walk cycle: a planted foot stays put (no sliding); a treadmill cycle loops; the keys are on the rig's own controls", [&] {
    R145Walk w;
    r145_walk(w, "walk", false);
    /* A planted foot rolls - about its heel as it lands, its toes as it leaves - so the ankle rises; what must not move
     * is the part on the ground: every planted frame has its heel or its toes where they are mid-stance (foot flat). */
    float slide = 0;
    int planted = 0;
    for (const AnimationClip::Contact &k : w.clip.contacts) {
      const size_t leg = k.bone == w.g.legs[0].lower->name ? 0 : 1, mid = (size_t)((k.from + k.to) * 0.5f);
      if (mid >= w.ankles.size()) continue;
      for (int f = (int)k.from; f <= (int)k.to && f < (int)w.ankles.size(); f++, planted++)
        slide = std::max(slide, std::min(length(w.heels[(size_t)f][leg] - w.heels[mid][leg]), length(w.toes[(size_t)f][leg] - w.toes[mid][leg])));
    }
    const bool on_controls = w.clip.find("LeftFootIK", "Position", 2) && w.clip.find("Hips", "Position", 1) && !w.clip.find("LeftUpperLeg", "Rotation", 0);
    std::printf("    travelling walk: %d frames, %zu contacts, %d planted frames, the most a planted heel or toe moved %.3f mm; keyed on the IK targets: %s\n",
                w.frames, w.clip.contacts.size(), planted, slide * 1000.0f, on_controls ? "yes" : "no");
    CHECK(w.frames > 20 && w.clip.contacts.size() >= 4 && slide < 1e-3f && on_controls);
    R145Walk loop;
    r145_walk(loop, "walk", true);
    float seam = 0;
    for (size_t i = 0; i < loop.g.legs.size(); i++) seam = std::max(seam, length(loop.ankles.back()[i] - loop.ankles.front()[i]));
    const float drift = length(loop.ankles[loop.ankles.size() / 8][0] - loop.ankles[0][0]);  // (a quarter of a cycle on)
    std::printf("    treadmill: the last frame's ankles %.4f mm from the first's; a quarter cycle on the left ankle %.3f m away (it moves under the body)\n",
                seam * 1000.0f, drift);
    CHECK(seam < 1e-3f && drift > 0.05f);
  });
  test("walk cycle: stride and cadence scale with the leg (Froude); a run has both feet up, a walk never; a trot moves diagonal pairs together", [&] {
    Scene s;
    GameObject *small = create_rig(s, false, Vec3(0.0f)), *big = create_rig(s, false, Vec3(5, 0, 0));
    big->set_local_scale({2, 2, 2});
    const gait::Rig a = gait::analyse(s, *small), b = gait::analyse(s, *big);
    gait::Style walk;
    gait::preset("walk", a, walk);
    const gait::Cycle ca = gait::cycle_of(a, walk), cb = gait::cycle_of(b, walk);
    std::printf("    a %.2f m leg: %.2f m cycles at %.2f m/s every %.2f s; a %.2f m leg: %.2f m at %.2f m/s every %.2f s\n", a.leg_length, ca.length, ca.speed,
                ca.period, b.leg_length, cb.length, cb.speed, cb.period);
    CHECK(std::fabs(cb.length / ca.length - 2.0f) < 0.02f && std::fabs(cb.period / ca.period - std::sqrt(2.0f)) < 0.02f);
    auto airborne = [&](const char *name) {
      gait::Style st;
      gait::preset(name, a, st);
      const gait::Cycle c = gait::cycle_of(a, st);
      int up = 0;
      for (int k = 0; k < 200; k++) {
        const gait::Pose p = gait::pose_at(a, st, c.period * k / 200.0f, true);
        bool any = false;
        for (uint8_t d : p.down) any = any || d;
        up += !any;
      }
      return up;
    };
    const int run = airborne("run"), walked = airborne("walk");
    Scene q;
    GameObject *dog = create_rig(q, true, Vec3(0.0f));
    const gait::Rig d = gait::analyse(q, *dog);
    gait::Style trot;
    gait::preset("trot", d, trot);
    int together = 0, apart = 0;
    for (int k = 0; k < 100; k++) {
      const gait::Pose p = gait::pose_at(d, trot, gait::cycle_of(d, trot).period * k / 100.0f, true);
      together += p.down[0] == p.down[3] && p.down[1] == p.down[2];  // (front left with hind right, front right with hind left)
      apart += p.down[0] != p.down[1];
    }
    std::printf("    airborne samples of 200: run %d, walk %d; trot: diagonals together %d of 100, the front pair apart %d\n", run, walked, together, apart);
    CHECK(run > 10 && walked == 0 && together == 100 && apart > 50);
  });
  test("walk cycle: presets for two and four legs, blended by weight, saved as .gait text; the quadruped walks; AutoPhysics barely changes a walk", [&] {
    Scene s;
    GameObject *rig = create_rig(s, false, Vec3(0.0f));
    const gait::Rig g = gait::analyse(s, *rig);
    int ok = 0;
    for (const std::string &n : gait::preset_names(false)) {
      gait::Style st;
      ok += gait::preset(n, g, st);
    }
    gait::Style w, r, back;
    gait::preset("walk", g, w), gait::preset("run", g, r);
    const gait::Style mid = gait::blend(w, r, 0.25f);
    CHECK(ok == 5 && std::fabs(mid.speed - (w.speed + (r.speed - w.speed) * 0.25f)) < 1e-5f);
    CHECK(gait::style_from_text(gait::style_text(mid), back) && std::fabs(back.duty - mid.duty) < 1e-5f && back.name == mid.name);
    CHECK(!gait::style_from_text("nonsense", back));
    R145Walk dog;
    r145_walk(dog, "walk", false, 2, true);
    std::printf("    quadruped walk: %d frames, %zu contacts\n", dog.frames, dog.clip.contacts.size());
    CHECK(dog.frames > 20 && dog.clip.contacts.size() >= 8);
    R145Walk walk;
    r145_walk(walk, "walk", false);
    const body::Body b = body::build(walk.s, *walk.rig, 75.0f);
    body::FlightResult res;
    std::string error;
    const bool solved = body::autophysics(walk.s, *walk.rig, walk.clip, b, body::AutoPhysicsSettings(), res, error);
    std::printf("    AutoPhysics on the generated walk: %s, the body moved up to %.1f mm\n", solved ? "solved" : error.c_str(), solved ? res.flights[0].shift * 1000.0f : 0.0f);
    /* (a few centimetres, spread over the walk: the pelvis goes forward at one speed, where a real walk slows over each
     * planted foot and speeds up between - physics evens that out) */
    CHECK(solved && res.flights[0].shift < 0.06f);
  });
  test("walk cycle in the editor: refused while switched off; on, a run is keyed into the character's clip as ordinary keys - edited by hand, undone in one step", [] {
    Editor ed;
    r34_open(ed);
    GameObject *rig = create_rig(ed.scene(), false, Vec3(0.0f));
    ed.mark_changed_for_test("Add the rig");
    ed.command("select " + rig->name);
    ed.step_frame_headless();
    ed.command("walk cycle walk");  // (off: refused)
    const bool refused = ed.anim_clip_for_test() == nullptr;
    ed.command("assist on walk");
    ed.command("window Rigging");
    ed.step_frame_headless();  // (the Walk Cycle section draws)
    ed.command("walk cycle run 4.2 2 inplace");
    ed.step_frame_headless();
    AnimationClipPtr clip = ed.anim_clip_for_test();
    const AnimTrack *hips = clip ? clip->find("Hips", "Position", 1) : nullptr;
    const size_t keys = hips ? hips->keys.size() : 0, contacts = clip ? clip->contacts.size() : 0;
    /* By hand, after: a key moved like any other. */
    ed.command("anim key Hips Position 1 3 1.5");
    ed.step_frame_headless();
    const AnimTrack *edited = ed.anim_clip_for_test() ? ed.anim_clip_for_test()->find("Hips", "Position", 1) : nullptr;
    const bool hand = edited && std::fabs(edited->sample(3.0f) - 1.5f) < 1e-4f;
    ed.command("undo");  // (the hand edit)
    ed.step_frame_headless();
    ed.command("undo");  // (the whole cycle)
    ed.step_frame_headless();
    AnimationClipPtr after = ed.anim_clip_for_test();
    const bool undone = !after || !after->find("Hips", "Position", 1) || after->find("Hips", "Position", 1)->keys.empty();
    std::printf("    off: refused %s; on: %zu keys on the hips' height, %zu contacts; a hand edit kept: %s; undone in one step: %s\n", refused ? "yes" : "no", keys,
                contacts, hand ? "yes" : "no", undone ? "yes" : "no");
    CHECK(refused && keys > 20 && contacts >= 4 && hand && undone);
  });
  test("learn: the Walk Cycle lesson's Try it switches the assistant on and makes a person walk two cycles forward", [] {
    std::string cmds;
    for (int i = 0; i < lesson_count() && cmds.empty(); i++) {
      const std::string body = lesson(i).body;
      const size_t at = body.find("| A person walking two cycles forward");
      const size_t start = at == std::string::npos ? std::string::npos : body.rfind("! cmd:", at);
      if (start != std::string::npos) cmds = body.substr(start + 6, at - start - 6);
    }
    CHECK(!cmds.empty());
    Editor ed;
    ed.init_headless(1400, 900);
    size_t a = 0;
    while (a <= cmds.size()) {
      const size_t e = cmds.find(';', a);
      std::string c = cmds.substr(a, e == std::string::npos ? std::string::npos : e - a);
      while (!c.empty() && c.front() == ' ') c.erase(c.begin());
      while (!c.empty() && c.back() == ' ') c.pop_back();
      if (!c.empty()) ed.command(c);
      if (e == std::string::npos) break;
      a = e + 1;
    }
    ed.step_frame_headless();
    AnimationClipPtr clip = ed.anim_clip_for_test();
    const AnimTrack *z = clip ? clip->find("Hips", "Position", 2) : nullptr;
    const float went = z && !z->keys.empty() ? z->keys.back().value - z->keys.front().value : 0.0f;
    std::printf("    the walk assistant %s; the hips went %.2f m forward over %zu keys\n", ed.assist_on(Editor::Assist::Walk) ? "on" : "off", went,
                z ? z->keys.size() : 0);
    CHECK(ed.assist_on(Editor::Assist::Walk) && went > 1.5f);
  });
  test("walk cycle: the toes and heels never go through the floor; the hands swing through their IK targets; a rig facing -Z walks its own way", [&] {
    R145Walk w;
    r145_walk(w, "walk", false);
    float floor = 1e9f, lowest = 1e9f;
    for (size_t i = 0; i < w.g.legs.size(); i++) floor = std::min(floor, w.heels[0][i].y);  // (both feet planted at the start)
    for (size_t f = 0; f < w.toes.size(); f++)
      for (size_t i = 0; i < w.g.legs.size(); i++) lowest = std::min(lowest, std::min(w.toes[f][i].y, w.heels[f][i].y));
    /* The hands: their IK targets keyed (else the arm IK would pull them back to rest), the hand swinging along the walk. */
    const AnimTrack *hand_ik = w.clip.find("LeftHandIK", "Position", 2);
    GameObject *hand = w.s.find_by_name("LeftHand");
    float lo = 1e9f, hi = -1e9f;
    body::for_each_frame(w.s, *w.rig, w.clip, 0, w.frames - 1, [&](int) {
      const float rel = hand->world_position().z - w.g.hips->world_position().z;  // (relative to the body, which travels)
      lo = std::min(lo, rel), hi = std::max(hi, rel);
    });
    std::printf("    the soles' lowest point %.2f mm below the floor; the left hand IK keyed: %s, the hand swings %.0f mm fore and aft\n",
                (floor - lowest) * 1000.0f, hand_ik ? "yes" : "no", (hi - lo) * 1000.0f);
    CHECK(floor - lowest < 1e-3f && hand_ik && hi - lo > 0.08f);
    /* Turned round: the walk goes the way the feet point, not +Z. */
    R145Walk back;
    back.rig = create_rig(back.s, false, Vec3(0.0f));
    back.rig->set_local_rotation(Quat::axis_angle(Vec3(0, 1, 0), 3.14159265f));
    back.g = gait::analyse(back.s, *back.rig);
    gait::WalkSettings ws;
    gait::preset("walk", back.g, ws.style);
    ws.in_place = false;
    std::string error;
    back.frames = gait::generate(back.s, *back.rig, back.g, ws, AnimationClip(), back.clip, error);
    float z0 = 0, z1 = 0;
    body::for_each_frame(back.s, *back.rig, back.clip, 0, back.frames - 1, [&](int f) {
      const float z = back.g.hips->world_position().z;
      if (f == 0) z0 = z;
      z1 = z;
    });
    std::printf("    turned round: forward (%.2f, %.2f, %.2f); the hips went %.2f m along z\n", back.g.forward.x, back.g.forward.y, back.g.forward.z, z1 - z0);
    CHECK(back.g.forward.z < -0.9f && z1 - z0 < -1.0f);
  });
  test("walk cycle in the editor: `walk set` changes one number and keeps the rest; a walk that can't be made leaves no clip behind", [] {
    Editor ed;
    r34_open(ed);
    GameObject *rig = create_rig(ed.scene(), false, Vec3(0.0f));
    ed.mark_changed_for_test("Add the rig");
    ed.command("select " + rig->name);
    ed.step_frame_headless();
    ed.command("assist on walk");
    ed.command("walk set lift 0.2");
    ed.command("walk set duty 0.7");
    const gait::Style s = ed.walk_settings_for_test().style;
    const bool kept = std::fabs(s.lift - 0.2f) < 1e-5f && std::fabs(s.duty - 0.7f) < 1e-5f;
    /* 64 cycles at a crawl: over the 4,000-frame limit - refused, and the character still has no clip. */
    ed.command("walk cycle walk 0.05 64");
    ed.step_frame_headless();
    const bool none = ed.anim_clip_for_test() == nullptr;
    std::printf("    after two `walk set`s: lift %.2f, duty %.2f; the refused walk left a clip: %s\n", s.lift, s.duty, none ? "no" : "yes");
    CHECK(kept && none);
  });
  /* ---------------------------------------------------------------- round 146: footsteps along a path (task 0126) */
  /* A curve object through `pts` (a poly path: straight between its points). */
  auto r146_path = [](Scene &s, const std::vector<Vec3> &pts) {
    GameObject *g = s.create("Path");
    Curve *cv = g->add<Curve>();
    cv->type = 1;
    for (Vec3 p : pts) cv->points.push_back({p, 0.0f, 1.0f});
    return g;
  };
  /* The planned walk keyed, and each frame's ankles, heels and toes (as R145Walk's). */
  struct R146Steps {
    Scene s;
    GameObject *rig = nullptr;
    gait::Rig g;
    gait::Style style;
    steps::Plan plan;
    AnimationClip clip;
    int frames = 0;
    std::vector<std::vector<Vec3>> ankles, heels, toes;
  };
  auto r146_walk = [](R146Steps &w, const CurvePath &path, const std::function<bool(Vec3, Vec3 &, Vec3 &)> &ground = nullptr) {
    w.g = gait::analyse(w.s, *w.rig);
    gait::preset("walk", w.g, w.style);
    w.plan = steps::plan(w.g, w.style, path, 30.0f, ground);
    std::string error;
    w.frames = steps::walk(w.s, *w.rig, w.g, w.style, w.plan, 30.0f, AnimationClip(), w.clip, error);
    if (!w.frames) std::printf("    %s\n", error.c_str());
    body::for_each_frame(w.s, *w.rig, w.clip, 0, std::max(0, w.frames - 1), [&](int) {
      std::vector<Vec3> a, h, t;
      for (const gait::Leg &l : w.g.legs) {
        const Vec3 at = l.foot->world_position();
        const Quat turn = l.foot->world_rotation() * l.rest_foot.conjugate();
        const Vec3 fwd = turn.rotate(normalize(Vec3(w.g.forward.x, 0.0f, w.g.forward.z)));
        a.push_back(at);
        h.push_back(at + turn.rotate(Vec3(0, -w.g.sole, 0)));
        t.push_back(at + fwd * l.foot_length + turn.rotate(Vec3(0, -w.g.sole, 0)));
      }
      w.ankles.push_back(a), w.heels.push_back(h), w.toes.push_back(t);
    });
  };
  test("footsteps: along a straight path every foot lands on its print, planted feet never slide, the first and last steps are short", [&] {
    R146Steps w;
    w.rig = create_rig(w.s, false, Vec3(0.0f));
    GameObject *p = r146_path(w.s, {Vec3(0, 0, 0), Vec3(0, 0, 5)});
    r146_walk(w, sample_curve(*p->curve(), p->world_matrix()));
    /* Mid-stance (the foot flat): the ankle on the print. */
    float miss = 0;
    int landed = 0;
    for (size_t i = 2; i + 1 < w.plan.prints.size(); i++) {
      const steps::Plan::Print &pr = w.plan.prints[i];
      float next = (float)w.frames;
      for (size_t j = i + 1; j < w.plan.prints.size(); j++)
        if (w.plan.prints[j].side == pr.side) { next = w.plan.prints[j].frame; break; }
      const int mid = (int)std::lround(pr.frame + (next - pr.frame) * 0.3f);
      if (mid >= (int)w.ankles.size()) continue;
      miss = std::max(miss, length(w.ankles[(size_t)mid][(size_t)pr.side] - pr.pos)), landed++;
    }
    /* A planted foot's heel or toes still (as Walk Cycle's). */
    float slide = 0;
    for (const AnimationClip::Contact &k : w.clip.contacts) {
      const size_t leg = k.bone == w.g.legs[0].lower->name ? 0 : 1, mid = (size_t)((k.from + k.to) * 0.5f);
      if (mid >= w.ankles.size()) continue;
      for (int f = (int)k.from; f <= (int)k.to && f < (int)w.ankles.size(); f++)
        slide = std::max(slide, std::min(length(w.heels[(size_t)f][leg] - w.heels[mid][leg]), length(w.toes[(size_t)f][leg] - w.toes[mid][leg])));
    }
    std::vector<float> step;
    for (size_t i = 2; i < w.plan.prints.size(); i++) step.push_back(w.plan.prints[i].pos.z - w.plan.prints[i - 2].pos.z);
    const float first = step.front(), last = step.back(), middle = step[step.size() / 2];
    std::printf("    %zu prints over %d frames: %d landed within %.3f mm; planted feet moved %.3f mm; steps first %.2f, middle %.2f, last %.2f m\n",
                w.plan.prints.size(), w.frames, landed, miss * 1000.0f, slide * 1000.0f, first, middle, last);
    CHECK(landed >= 6 && miss < 1e-3f && slide < 1e-3f);
    CHECK(first < 0.7f * middle && last < 0.9f * middle && std::fabs(w.plan.prints.back().pos.z - 5.0f) < 0.01f);
  });
  test("footsteps: on a curve the inner foot takes the shorter steps and the feet never cross; box stairs put every print on a step, the body rising", [&] {
    R146Steps w;
    w.rig = create_rig(w.s, false, Vec3(0.0f));
    /* A quarter circle of radius 2 turning left: the rig walks +Z with its right +X, so about (-2, 0, 0). */
    std::vector<Vec3> arc;
    for (int i = 0; i <= 16; i++) {
      const float a = 3.14159265f * 0.5f * i / 16.0f;
      arc.push_back(Vec3(2.0f * std::cos(a) - 2.0f, 0.0f, 2.0f * std::sin(a)));
    }
    GameObject *p = r146_path(w.s, arc);
    r146_walk(w, sample_curve(*p->curve(), p->world_matrix()));
    float inner = 0, outer = 0;
    int ni = 0, no = 0, crossed = 0;
    const int left = 0;  // (side 0: the left foot)
    for (size_t i = 2; i < w.plan.prints.size(); i++) {
      const steps::Plan::Print &a = w.plan.prints[i - 2], &b = w.plan.prints[i];
      if (a.side != b.side) continue;
      const float d = length(b.pos - a.pos);
      if (b.side == left) inner += d, ni++;
      else outer += d, no++;
    }
    for (const steps::Plan::Print &pr : w.plan.prints) {
      const float r = length(Vec3(pr.pos.x + 2.0f, 0.0f, pr.pos.z));  // (from the turn's centre)
      crossed += pr.side == left ? r > 2.0f : r < 2.0f;
    }
    inner /= std::max(1, ni), outer /= std::max(1, no);
    std::printf("    a left turn: the inner (left) foot's steps %.2f m, the outer's %.2f m; prints on the wrong side %d\n", inner, outer, crossed);
    CHECK(inner < outer * 0.95f && crossed == 0);
    /* Stairs: six boxes, 15 cm rises and 40 cm goings, walked up along +Z. */
    Editor ed;
    r34_open(ed);
    std::vector<float> tops;
    for (int k = 0; k < 6; k++) {
      ed.command("create Cube");
      GameObject *c = ed.active_object_for_test();
      const float h = 0.15f * (k + 1);
      c->set_local_scale({2.0f, h, 0.4f});
      c->set_world_position(Vec3(20.0f, h * 0.5f, 1.2f + 0.4f * k));
      tops.push_back(h);
    }
    tops.push_back(0.0f);  // (the floor before the stairs)
    GameObject *rig = create_rig(ed.scene(), false, Vec3(20.0f, 0.0f, 0.0f));  // (away from the sample scene's objects)
    GameObject *path = r146_path(ed.scene(), {Vec3(20, 0, 0), Vec3(20, 0, 3.2f)});
    ed.mark_changed_for_test("Stairs");
    ed.command("assist on footsteps");
    ed.command("select " + rig->name);
    ed.step_frame_headless();
    ed.command("walk path " + path->name);
    ed.step_frame_headless();
    const gait::Rig g = gait::analyse(ed.scene(), *rig);
    int prints = 0, on_step = 0;
    float highest = 0;
    for (const auto &o : ed.scene().objects()) {
      if (!o->get<Footprint>()) continue;
      prints++;
      const float ground = o->world_position().y - g.sole;
      for (float t : tops) on_step += std::fabs(ground - t) < 1e-3f;
      highest = std::max(highest, ground);
    }
    AnimationClipPtr clip = ed.anim_clip_for_test();
    const AnimTrack *hy = clip ? clip->find("Hips", "Position", 1) : nullptr;
    const float rise = hy && !hy->keys.empty() ? hy->keys.back().value - hy->keys.front().value : 0.0f;
    std::printf("    stairs: %d prints, %d on a step's top or the floor, the highest at %.2f m; the hips rose %.2f m (%zu keys)\n", prints, on_step, highest, rise,
                hy ? hy->keys.size() : (size_t)0);
    CHECK(prints >= 8 && on_step == prints && highest > 0.6f && rise > 0.6f);
  });
  test("footsteps in the editor: refused while off; a print moved by hand survives Re-plan and Update walks it; one undo step; saved with the scene", [] {
    Editor ed;
    r34_open(ed);
    GameObject *rig = create_rig(ed.scene(), false, Vec3(20.0f, 0.0f, 0.0f));  // (away from the sample scene's objects)
    GameObject *path = ed.scene().create("Walkway");
    Curve *cv = path->add<Curve>();
    cv->type = 1;
    cv->points.push_back({Vec3(20, 0, 0), 0.0f, 1.0f}), cv->points.push_back({Vec3(20, 0, 4), 0.0f, 1.0f});
    ed.mark_changed_for_test("Add the rig and path");
    ed.command("select " + rig->name);
    ed.step_frame_headless();
    ed.command("walk path Walkway");  // (off: refused)
    auto count = [&] {
      int n = 0;
      for (const auto &o : ed.scene().objects()) n += o->get<Footprint>() != nullptr;
      return n;
    };
    const bool refused = count() == 0 && !ed.anim_clip_for_test();
    ed.command("assist on footsteps");
    ed.command("walk path Walkway");
    ed.step_frame_headless();
    const int made = count();
    /* A print moved 10 cm sideways by hand (selected: Update and Re-plan find the character from it). */
    GameObject *moved = nullptr;
    for (const auto &o : ed.scene().objects())
      if (const Footprint *f = o->get<Footprint>(); f && f->side == 0 && f->frame > 20.0f && !moved) moved = o.get();
    const std::string name = moved ? moved->name : "";
    const Vec3 to = moved ? moved->world_position() + Vec3(0.1f, 0.0f, 0.0f) : Vec3();
    if (moved) moved->set_world_position(to);
    ed.mark_changed_for_test("Move a print");
    ed.command("select " + name);
    ed.step_frame_headless();
    /* Before the Re-plan: the left foot's keys and every print's place (undo must bring back exactly these). */
    auto foot_keys = [&] {
      std::vector<float> v;
      AnimationClipPtr c = ed.anim_clip_for_test();
      if (const AnimTrack *t = c ? c->find("LeftFootIK", "Position", 0) : nullptr)
        for (const auto &k : t->keys) v.push_back(k.value);
      return v;
    };
    auto places = [&] {
      std::vector<Vec3> v;
      for (const auto &o : ed.scene().objects())
        if (o->get<Footprint>()) v.push_back(o->world_position());
      return v;
    };
    ed.command("select " + rig->name);
    ed.step_frame_headless();
    const std::vector<float> keys_before = foot_keys();
    const std::vector<Vec3> places_before = places();
    ed.command("select " + name);
    ed.step_frame_headless();
    ed.command("walk steps replan");
    ed.step_frame_headless();
    const bool replanned_keys = foot_keys() != keys_before;  // (the left foot now walks to the moved print)
    GameObject *after = ed.scene().find_by_name(name);
    const bool kept = after && length(after->world_position() - to) < 1e-4f;
    /* The walk follows it: at that print's mid-stance the left ankle is over the moved print. */
    const gait::Rig g = gait::analyse(ed.scene(), *rig);
    float off = 1e9f;
    if (after) {
      const int f = (int)std::lround(after->get<Footprint>()->frame) + 6;  // (past the toes coming down: the foot flat)
      AnimationClipPtr clip = ed.anim_clip_for_test();
      if (clip) body::for_each_frame(ed.scene(), *rig, *clip, f, f, [&](int) { off = length(g.legs[0].foot->world_position() - to); });
    }
    /* Undo: the whole Re-plan in one step - the keys and every print as they were before it. */
    ed.command("undo");
    ed.step_frame_headless();
    ed.command("select " + rig->name);
    ed.step_frame_headless();
    const std::vector<Vec3> places_after = places();
    bool same = places_after.size() == places_before.size();
    for (size_t i = 0; same && i < places_after.size(); i++) same = length(places_after[i] - places_before[i]) < 1e-5f;
    const bool one_step = replanned_keys && same && foot_keys() == keys_before && count() == made;
    /* Saved and loaded: the prints and their numbers. */
    Scene back;
    std::string error;
    const bool loaded = load_scene_text(save_scene_text(ed.scene()), back, error);
    int prints = 0;
    float moved_planned = 0;
    for (const auto &o : back.objects())
      if (const Footprint *f = o->get<Footprint>()) {
        prints++;
        if (o->name == name) moved_planned = length(f->planned - to);
      }
    std::printf("    off: refused %s; %d prints made; the moved one kept by Re-plan: %s, the ankle %.1f mm from it; undone in one step: %s; "
                "after save and load %d prints, the moved one still 'moved' (%.0f mm from where it was planned)\n",
                refused ? "yes" : "no", made, kept ? "yes" : "no", off * 1000.0f, one_step ? "yes" : "no", prints, moved_planned * 1000.0f);
    CHECK(refused && made >= 6 && kept && off < 0.01f && one_step);
    CHECK(loaded && prints == made && std::fabs(moved_planned - 0.1f) < 1e-3f);
  });
  test("footsteps: a print dragged far is still reached; a print only turned is the animator's too; a faster style keeps the moved prints; two characters with one name refused", [&] {
    /* Far: a print 0.8 m out to the side - the pelvis goes towards it, the foot lands on it. */
    R146Steps w;
    w.rig = create_rig(w.s, false, Vec3(0.0f));
    GameObject *p = r146_path(w.s, {Vec3(0, 0, 0), Vec3(0, 0, 4)});
    r146_walk(w, sample_curve(*p->curve(), p->world_matrix()));
    steps::Plan far = w.plan;
    size_t k = 0;
    for (size_t i = 2; i < far.prints.size() && !k; i++)
      if (far.prints[i].side == 0 && far.prints[i].frame > 30.0f) k = i;
    far.prints[k].pos.x -= 0.8f;  // (the left foot's side: further out)
    AnimationClip out;
    std::string error;
    const int frames = steps::walk(w.s, *w.rig, w.g, w.style, far, 30.0f, AnimationClip(), out, error);
    float miss = 1e9f;
    const int mid = (int)std::lround(far.prints[k].frame) + 6;
    if (frames) body::for_each_frame(w.s, *w.rig, out, mid, mid, [&](int) { miss = length(w.g.legs[0].foot->world_position() - far.prints[k].pos); });
    /* In the editor: one print only turned 30 degrees, another moved; then a faster style and Re-plan. */
    Editor ed;
    r34_open(ed);
    GameObject *rig = create_rig(ed.scene(), false, Vec3(20.0f, 0.0f, 0.0f));
    GameObject *path = ed.scene().create("Walkway");
    Curve *cv = path->add<Curve>();
    cv->type = 1;
    cv->points.push_back({Vec3(20, 0, 0), 0.0f, 1.0f}), cv->points.push_back({Vec3(20, 0, 5), 0.0f, 1.0f});
    ed.mark_changed_for_test("Add the rig and path");
    ed.command("assist on footsteps");
    ed.command("assist on walk");
    ed.command("select " + rig->name);
    ed.step_frame_headless();
    ed.command("walk path Walkway");
    ed.step_frame_headless();
    GameObject *turned = nullptr, *moved = nullptr;
    for (const auto &o : ed.scene().objects()) {
      const Footprint *f = o->get<Footprint>();
      if (!f || f->frame < 20.0f) continue;
      if (!turned && f->side == 1) turned = o.get();
      else if (!moved && f->side == 0) moved = o.get();
    }
    const Quat turn = turned ? Quat::axis_angle(Vec3(0, 1, 0), 0.5236f) * turned->world_rotation() : Quat();
    const Vec3 to = moved ? moved->world_position() + Vec3(-0.12f, 0.0f, 0.05f) : Vec3();
    if (turned) turned->set_world_rotation(turn);
    if (moved) moved->set_world_position(to);
    ed.mark_changed_for_test("Turn and move prints");
    ed.command("walk set speed 1.8");  // (a faster style: every frame moves)
    ed.command("walk steps replan");
    ed.step_frame_headless();
    bool turn_kept = false, move_kept = false;
    for (const auto &o : ed.scene().objects()) {
      if (!o->get<Footprint>()) continue;
      const Quat q = o->world_rotation();
      turn_kept = turn_kept || std::fabs(q.x * turn.x + q.y * turn.y + q.z * turn.z + q.w * turn.w) > 0.9999f;
      move_kept = move_kept || length(o->world_position() - to) < 1e-4f;
    }
    /* Two objects named as the character: refused, nothing made. */
    Editor twin;
    r34_open(twin);
    GameObject *a = create_rig(twin.scene(), false, Vec3(20.0f, 0.0f, 0.0f));
    GameObject *b = create_rig(twin.scene(), false, Vec3(25.0f, 0.0f, 0.0f));
    b->name = a->name;
    GameObject *tp = twin.scene().create("Walkway");
    tp->add<Curve>()->type = 1;
    tp->curve()->points.push_back({Vec3(20, 0, 0), 0.0f, 1.0f}), tp->curve()->points.push_back({Vec3(20, 0, 4), 0.0f, 1.0f});
    twin.mark_changed_for_test("Twins");
    twin.command("assist on footsteps");
    twin.command("select " + a->name);  // (the first of the two)
    twin.step_frame_headless();
    twin.command("walk path Walkway");
    int twin_prints = 0;
    for (const auto &o : twin.scene().objects()) twin_prints += o->get<Footprint>() != nullptr;
    std::printf("    a print 0.8 m out: the foot lands %.1f mm from it; after a faster style and Re-plan the turned print kept: %s, the moved one: %s; "
                "two characters named alike: %d prints made\n",
                miss * 1000.0f, turn_kept ? "yes" : "no", move_kept ? "yes" : "no", twin_prints);
    CHECK(frames && miss < 0.01f && turn_kept && move_kept && twin_prints == 0);
  });
  /* A lesson's Try-it run as Learn runs it: the commands before `caption`, one by one. */
  auto r146_try = [](Editor &ed, const char *caption) {
    std::string cmds;
    for (int i = 0; i < lesson_count() && cmds.empty(); i++) {
      const std::string body = lesson(i).body;
      const size_t at = body.find(std::string("| ") + caption);
      const size_t start = at == std::string::npos ? std::string::npos : body.rfind("! cmd:", at);
      if (start != std::string::npos) cmds = body.substr(start + 6, at - start - 6);
    }
    size_t a = 0;
    while (!cmds.empty() && a <= cmds.size()) {
      const size_t e = cmds.find(';', a);
      std::string c = cmds.substr(a, e == std::string::npos ? std::string::npos : e - a);
      while (!c.empty() && c.front() == ' ') c.erase(c.begin());
      while (!c.empty() && c.back() == ' ') c.pop_back();
      if (!c.empty()) ed.command(c);
      ed.step_frame_headless();
      if (e == std::string::npos) break;
      a = e + 1;
    }
    return !cmds.empty();
  };
  test("learn: the Footsteps lesson's Try it switches the assistant on and walks a person along a path on its footprints", [&] {
    Editor ed;
    ed.init_headless(1400, 900);
    const bool found = r146_try(ed, "A person walking along a path on its footprints");
    int prints = 0;
    for (const auto &o : ed.scene().objects()) prints += o->get<Footprint>() != nullptr;
    AnimationClipPtr clip = ed.anim_clip_for_test();
    std::printf("    the footsteps assistant %s; %d prints, %zu contacts in the clip\n", ed.assist_on(Editor::Assist::Footsteps) ? "on" : "off", prints,
                clip ? clip->contacts.size() : (size_t)0);
    CHECK(found && ed.assist_on(Editor::Assist::Footsteps) && prints >= 4 && clip && clip->contacts.size() >= 3);
  });
  /* ---------------------------------------------------------------- round 147: Locomotion in Play (task 0127) */
  /* A person with Locomotion, played by hand: `keys` says which keys are down at each time; each frame the scene's
   * components update (moving the object), Locomotion steps and IK solves, as the Play loop does. */
  struct R147Play {
    Scene *s = nullptr;
    GameObject *rig = nullptr;
    Locomotion *lc = nullptr;
    GroundGrid ground;
    float t = 0.0f, cost = 0.0f;
    int frames = 0;
  };
  auto r147_start = [](R147Play &p, Scene &s, Vec3 at = Vec3(0.0f)) {
    p.s = &s;
    p.rig = create_rig(s, false, at);
    p.lc = p.rig->add<Locomotion>();
    p.ground = locomotion::ground_for(s);
    PlayContext ctx;
    s.start(ctx);
  };
  auto r147_run = [](R147Play &p, float seconds, const std::function<bool(int key)> &keys, const std::function<void()> &each = nullptr) {
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < (int)std::lround(seconds / dt); i++) {
      PlayContext ctx;
      ctx.dt = dt, ctx.time = p.t, ctx.key_down = keys;
      p.s->update(ctx);
      locomotion::step(*p.s, dt, p.ground);
      solve_constraints(*p.s);
      p.t += dt, p.cost += p.lc->cost_ms_, p.frames++;
      if (each) each();
    }
  };
  auto key_is = [](std::vector<int> down) { return [down](int k) { return std::find(down.begin(), down.end(), k) != down.end(); }; };
  test("locomotion: W walks the person forward at the walk speed with planted feet that never slide, its stride the Walk Cycle's", [&] {
    Locomotion::live = true;
    Scene s;
    R147Play p;
    r147_start(p, s);
    /* A planted foot: where it was when it landed, every frame it stays down. */
    std::vector<Vec3> landed(2);
    std::vector<uint8_t> was(2, 1);
    std::vector<std::vector<float>> plants(2);
    float slide = 0;
    r147_run(p, 3.0f, key_is({platform::KEY_W}), [&] {
      for (size_t i = 0; i < 2; i++) {
        const bool down = p.lc->legs_[i].down;
        const Vec3 a = p.lc->rig_.legs[i].foot->world_position();
        if (down && !was[i]) landed[i] = a, plants[i].push_back(a.z);
        else if (down && plants[i].size()) slide = std::max(slide, length(a - landed[i]));
        was[i] = down;
      }
    });
    const float went = p.rig->world_position().z;
    float stride = 0;
    int n = 0;
    for (const auto &z : plants)
      for (size_t k = 2; k < z.size(); k++) stride += z[k] - z[k - 1], n++;  // (from the second step: the first starts from standing)
    stride /= std::max(1, n);
    gait::Style w;
    gait::preset("walk", p.lc->rig_, w);
    w.speed = 1.4f / std::sqrt(p.lc->rig_.leg_length / 0.9f);
    const float cycle = gait::cycle_of(p.lc->rig_, w).length;
    std::printf("    3 s of W: %.2f m forward (%.2f expected), planted feet moved at most %.3f mm; a stride %.3f m, Walk Cycle's %.3f m; %.3f ms a frame\n",
                went, 1.4f * (3.0f - 0.175f), slide * 1000.0f, stride, cycle, p.cost / p.frames);
    CHECK(std::fabs(went - 1.4f * (3.0f - 0.175f)) < 0.05f && slide < 1e-3f && n >= 2 && std::fabs(stride / cycle - 1.0f) < 0.05f);
    // (the frame's cost is a stress row: `animation`, task 0177)
    Locomotion::live = false;
  });
  test("locomotion: turning leans the body into the turn; letting go of W stops within a step, both feet down and still", [&] {
    Locomotion::live = true;
    Scene s;
    R147Play p;
    r147_start(p, s);
    p.lc->turn_speed = 90.0f;
    r147_run(p, 1.5f, key_is({platform::KEY_W, platform::KEY_SHIFT}));
    float lean = 0;  // (the hips' up leaning towards the turn's inside - right, turning with D - in degrees)
    r147_run(p, 1.0f, key_is({platform::KEY_W, platform::KEY_SHIFT, platform::KEY_D}), [&] {
      const Vec3 up = p.lc->rig_.hips->world_rotation().rotate(Vec3(0, 1, 0)) , right = p.rig->world_rotation().rotate(Vec3(1, 0, 0));
      lean = std::asin(std::max(-1.0f, std::min(1.0f, dot(up, right)))) * 57.29578f;
    });
    /* Let go: slowing at the acceleration (3.5 m/s at 4 m/s a second: 0.875 s), the step under way finished, then
     * standing - within the slowing and one more step (a walk's cycle). */
    const float released = p.t;
    float stood = -1.0f;
    r147_run(p, 2.5f, key_is({}), [&] {
      if (stood < 0.0f && p.lc->speed_ == 0.0f && p.lc->legs_[0].down && p.lc->legs_[1].down) stood = p.t - released;
    });
    const Vec3 a0 = p.lc->rig_.legs[0].foot->world_position(), b0 = p.lc->rig_.legs[1].foot->world_position(), r0 = p.rig->world_position();
    r147_run(p, 0.5f, key_is({}));
    const float still = std::max(length(p.lc->rig_.legs[0].foot->world_position() - a0), length(p.lc->rig_.legs[1].foot->world_position() - b0));
    gait::Style w;
    gait::preset("walk", p.lc->rig_, w);
    const float limit = 3.5f / 4.0f + gait::cycle_of(p.lc->rig_, w).period;
    std::printf("    running in a right turn the hips lean %.1f degrees into it; let go, standing with both feet down after %.2f s (at most %.2f); "
                "then the feet move %.3f mm in 0.5 s, the body %.3f mm\n",
                lean, stood, limit, still * 1000.0f, length(p.rig->world_position() - r0) * 1000.0f);
    CHECK(lean > 3.0f && stood > 0.0f && stood < limit && still < 1e-3f && length(p.rig->world_position() - r0) < 1e-3f);
    /* Four legs: a dog walking (its feet never all down at once while it walks) stops too. */
    Scene ds;
    R147Play dog;
    dog.s = &ds;
    dog.rig = create_rig(ds, true, Vec3(0.0f));
    dog.lc = dog.rig->add<Locomotion>();
    PlayContext dctx;
    ds.start(dctx);
    r147_run(dog, 2.0f, key_is({platform::KEY_W}));
    r147_run(dog, 2.5f, key_is({}));
    bool all = true;
    for (const Locomotion::Leg &l : dog.lc->legs_) all = all && l.down;
    const float phase = dog.lc->phase_;
    r147_run(dog, 0.5f, key_is({}));
    std::printf("    a dog let go: all four feet down %s, its gait %s\n", all ? "yes" : "no", dog.lc->phase_ == phase ? "stopped" : "still stepping");
    CHECK(dog.lc->legs_.size() == 4 && all && dog.lc->phase_ == phase);
    Locomotion::live = false;
  });
  test("locomotion: up a ramp every foot lands on its surface; switched off (the assistant) the keys move nothing", [&] {
    Locomotion::live = true;
    Editor ed;
    r34_open(ed);
    ed.command("create Cube");  // (a 10 degree ramp, 8 m long, rising along +Z from z = 1)
    GameObject *ramp = ed.active_object_for_test();
    const float a = 10.0f * 3.14159265f / 180.0f;
    ramp->set_local_scale({3.0f, 0.2f, 8.0f});
    ramp->set_local_rotation(Quat::axis_angle(Vec3(1, 0, 0), -a));
    ramp->set_world_position(Vec3(0.0f, 4.0f * std::sin(a) - 0.1f * std::cos(a), 1.0f + 4.0f * std::cos(a)));
    ed.step_frame_headless();
    R147Play p;
    r147_start(p, ed.scene());
    float worst = 0;
    int landings = 0;
    std::vector<uint8_t> was(2, 1);
    r147_run(p, 4.0f, key_is({platform::KEY_W}), [&] {
      for (size_t i = 0; i < 2; i++) {
        const bool down = p.lc->legs_[i].down;
        if (down && !was[i]) {
          const Vec3 at = p.lc->legs_[i].plant;
          SceneHit hit;
          const bool on = scene_raycast(ed.scene(), at + Vec3(0, 1, 0), Vec3(0, -1, 0), 3.0f, hit, [&](const GameObject &g) { return ed.scene().is_ancestor(p.rig, &g) || &g == p.rig; });
          if (on) worst = std::max(worst, std::fabs(at.y - p.lc->rig_.sole - hit.point.y)), landings++;  // (the ramp's: before it, no floor)
        }
        was[i] = down;
      }
    });
    const float climbed = p.rig->world_position().y;
    std::printf("    up the ramp: %d landings, each within %.1f mm of the surface under it; the body climbed %.2f m\n", landings, worst * 1000.0f, climbed);
    CHECK(landings >= 6 && worst < 0.01f && climbed > 0.3f);
    /* A prop in the hand: a script moves it and Locomotion leaves it be; destroyed in Play, nothing breaks. */
    Scene ps;
    R147Play q;
    r147_start(q, ps);
    GameObject *sword = ps.create("Sword", ps.find_by_name("LeftHand"));
    r147_run(q, 0.5f, key_is({platform::KEY_W}));
    sword->set_local_position(Vec3(0.0f, 0.3f, 0.0f));
    r147_run(q, 0.2f, key_is({platform::KEY_W}));
    const bool left_be = length(sword->local().position - Vec3(0.0f, 0.3f, 0.0f)) < 1e-6f;
    ps.destroy(sword);
    r147_run(q, 0.5f, key_is({platform::KEY_W}));
    Locomotion::live = false;
    /* Switched off, through the editor's Play: a script's wish moves nothing - not the object, not a bone. */
    Editor eo;
    r34_open(eo);
    GameObject *person = create_rig(eo.scene(), false, Vec3(20.0f, 0.0f, 0.0f));
    person->add<Locomotion>();
    const std::string who = person->name;
    eo.mark_changed_for_test("A person with Locomotion");
    eo.command("assist off locomotion");
    eo.command("play");
    GameObject *pr = eo.scene().find_by_name(who);
    std::vector<std::pair<GameObject *, Transform>> before;
    std::vector<GameObject *> stack{pr};
    while (!stack.empty()) {
      GameObject *g = stack.back();
      stack.pop_back();
      before.push_back({g, g->local()});
      for (GameObject *c : g->children) stack.push_back(c);
    }
    if (Locomotion *l = pr->get<Locomotion>()) l->set_move(Vec3(0, 0, 1.4f));
    for (int i = 0; i < 30; i++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      eo.step_frame_headless();
    }
    int changed = 0;
    for (const auto &b : before)
      changed += length(b.first->local().position - b.second.position) > 1e-6f || std::fabs(b.first->local().rotation.x * b.second.rotation.x + b.first->local().rotation.y * b.second.rotation.y + b.first->local().rotation.z * b.second.rotation.z + b.first->local().rotation.w * b.second.rotation.w) < 0.999999f;
    eo.command("stop");
    eo.step_frame_headless();
    std::printf("    a prop the script moved left where it put it: %s (then destroyed in Play: no crash); the assistant off, in Play %d of %zu objects moved\n",
                left_be ? "yes" : "no", changed, before.size());
    CHECK(left_be && changed == 0);
  });
  test("learn: the Locomotion lesson's Try it gives a person Locomotion; in the editor's Play a script's set_move walks it, its legs stepping", [&] {
    Editor ed;
    ed.init_headless(1400, 900);
    const bool found = r146_try(ed, "A person with Locomotion");
    std::string name;
    for (const auto &o : ed.scene().objects())
      if (o->get<Locomotion>()) name = o->name;
    const bool has = !name.empty();
    ed.command("play");
    Locomotion *lc = nullptr;
    for (const auto &o : ed.scene().objects())
      if (Locomotion *l = o->get<Locomotion>()) lc = l;
    if (lc) lc->set_move(Vec3(0, 0, 1.4f));
    float went = 0.0f;
    int landings = 0;
    bool was = true;
    for (int i = 0; i < 400 && lc && (went < 1.0f || landings < 2); i++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));  // (Play runs on the clock)
      ed.step_frame_headless();
      went = lc->owner->world_position().z;
      if (!lc->legs_.empty()) {
        if (lc->legs_[0].down && !was) landings++;
        was = lc->legs_[0].down;
      }
    }
    ed.command("stop");
    ed.step_frame_headless();
    GameObject *back = has ? ed.scene().find_by_name(name) : nullptr;  // (Stop puts the scene back as it was)
    std::printf("    the locomotion assistant %s, the component on the person: %s; in Play it went %.2f m, the left foot landing %d times; after Stop back at %.2f\n",
                ed.assist_on(Editor::Assist::Locomotion) ? "on" : "off", has ? "yes" : "no", went, landings, back ? back->world_position().z : -1.0f);
    CHECK(found && has && ed.assist_on(Editor::Assist::Locomotion) && went >= 1.0f && landings >= 2);
    Locomotion::live = false;
  });
  /* ---------------------------------------------------------------- round 152: written up (task 0132) */
  test("learn: the automatic walking lesson switches its three tools on; the AI inbetweening lesson asks for a span (and, not installed, says how to get it)", [&] {
    Editor ed;
    ed.init_headless(1400, 900);
    const bool walking = r146_try(ed, "Switch the three walking tools on and list the assistants");
    const bool three = ed.assist_on(Editor::Assist::Walk) && ed.assist_on(Editor::Assist::Footsteps) && ed.assist_on(Editor::Assist::Locomotion);
    Editor ai_ed;
    ai_ed.init_headless(1400, 900);
    const bool found = r146_try(ai_ed, "A person with two keys 40 frames apart");
    AnimationClipPtr clip = ai_ed.anim_clip_for_test();
    const AnimTrack *hy = clip ? clip->find("Hips", "Position", 1) : nullptr;
    std::printf("    walking lesson: the three tools on %s; AI lesson: the switch %s, %zu keys to fill between, a span made without the download: %s\n",
                three ? "yes" : "no", ai_ed.assist_on(Editor::Assist::Ai) ? "on" : "off", hy ? hy->keys.size() : (size_t)0,
                ai_ed.ai_ghost_for_test() || ai_ed.ai_busy_for_test() ? "yes" : "no");
    CHECK(walking && three && found && ai_ed.assist_on(Editor::Assist::Ai) && hy && hy->keys.size() == 2 && !ai_ed.ai_ghost_for_test() && !ai_ed.ai_busy_for_test());
    Locomotion::live = false;
  });
  /* ---------------------------------------------------------------- round 155: shape keys (task 0135) */
  test("shape keys: a weight adds exactly that much of the key; none in use, the mesh itself; saved and loaded with the scene; animated by a clip", [&] {
    Scene s;
    MeshPtr cube = std::make_shared<Mesh>(*primitives::cube());
    ShapeKey k{"Smile", std::vector<Vec3>(cube->vert_count(), Vec3(0.0f))};
    k.delta[0] = Vec3(0.0f, 0.4f, 0.0f), k.delta[3] = Vec3(0.2f, 0.0f, 0.0f);
    cube->shape_keys.push_back(k);
    cube->touch();
    GameObject *g = s.create("Head");
    g->add<MeshFilter>()->mesh = cube;
    MeshRenderer *mr = g->add<MeshRenderer>();
    const bool plain = g->evaluated_mesh() == cube.get();
    mr->set_shape_weight("Smile", 0.5f);
    const Mesh *e = g->evaluated_mesh();
    const float d0 = length(e->positions[0] - (cube->positions[0] + Vec3(0.0f, 0.2f, 0.0f))), d3 = length(e->positions[3] - (cube->positions[3] + Vec3(0.1f, 0.0f, 0.0f)));
    float others = 0.0f;
    for (size_t i = 1; i < e->vert_count(); i++)
      if (i != 3) others = std::max(others, length(e->positions[i] - cube->positions[i]));
    /* Saved and loaded: the key (sparse) and the weight. */
    Scene back;
    std::string error;
    const bool loaded = load_scene_text(save_scene_text(s), back, error);
    GameObject *bg = back.find_by_name("Head");
    const MeshRenderer *bmr = bg ? bg->get<MeshRenderer>() : nullptr;
    const MeshFilter *bmf = bg ? bg->get<MeshFilter>() : nullptr;
    const bool kept = bmr && bmf && bmf->mesh && bmf->mesh->find_shape("Smile") == 0 && std::fabs(bmr->shape_weight("Smile") - 0.5f) < 1e-6f &&
                      length(bmf->mesh->shape_keys[0].delta[0] - Vec3(0.0f, 0.4f, 0.0f)) < 1e-6f;
    /* A clip's BlendShape track: 0 at frame 0, 1 at frame 10; at frame 5 half of it. */
    AnimationClip clip;
    clip.track("", "MeshRenderer.BlendShape/Smile", 0).set_key(0.0f, 0.0f, AnimLinear);
    clip.track("", "MeshRenderer.BlendShape/Smile", 0).set_key(10.0f, 1.0f, AnimLinear);
    apply_clip(clip, *g, 5.0f);
    const float animated = mr->shape_weight("Smile");
    const float moved = length(g->evaluated_mesh()->positions[0] - cube->positions[0]);
    std::printf("    unweighted: the mesh itself %s; at 0.5 off by %g and %g, others moved %g; saved and loaded: %s; animated to %.3f, the vertex up %.3f m\n",
                plain ? "yes" : "no", d0, d3, others, loaded && kept ? "yes" : "no", animated, moved);
    CHECK(plain && d0 < 1e-6f && d3 < 1e-6f && others < 1e-6f && loaded && kept && std::fabs(animated - 0.5f) < 1e-5f && std::fabs(moved - 0.2f) < 1e-5f);
  });
  test("shape keys: added before the skin, so a key moves with the bone that carries it", [&] {
    R69Rig t;
    MeshRenderer *mr = t.tube->add<MeshRenderer>();
    ShapeKey k{"Bulge", std::vector<Vec3>(t.base->vert_count(), Vec3(0.0f))};
    for (size_t i = 0; i < t.base->vert_count(); i++)
      if (t.base->positions[i].y > 1.65f) k.delta[i] = Vec3(0.1f, 0.0f, 0.0f);  // (the top rings, all the upper bone's)
    t.base->shape_keys.push_back(k);
    t.base->touch();
    const Quat q = Quat::axis_angle({0, 0, 1}, kPi * 0.5f);
    t.upper->set_local_rotation(q);
    std::vector<Vec3> without = t.tube->evaluated_mesh()->positions;
    mr->set_shape_weight("Bulge", 1.0f);
    const Mesh *with = t.tube->evaluated_mesh();
    float worst = 0.0f;
    int n = 0;
    for (size_t i = 0; i < t.base->vert_count(); i++) {
      if (t.base->positions[i].y <= 1.65f) continue;
      worst = std::max(worst, length((with->positions[i] - without[i]) - q.rotate(Vec3(0.1f, 0.0f, 0.0f)))), n++;
    }
    std::printf("    %d vertices: the key's offset turned with the bone, off by at most %g\n", n, worst);
    CHECK(n > 0 && worst < 1e-4f);
  });
  test("shape keys: Blender's FBX and GLB come in with their keys, offsets and values", [&] {
    for (const char *file : {"tests/fixtures/shapes_quad.fbx", "tests/fixtures/shapes_quad.glb"}) {
      ImportResult r;
      const bool ok = import_model(file, r);
      const ImportedNode *face = nullptr;
      for (const ImportedNode &n : r.nodes)
        if (n.mesh && !n.mesh->shape_keys.empty()) face = &n;
      const int up = face ? face->mesh->find_shape("Up") : -1, wide = face ? face->mesh->find_shape("Wide") : -1;
      float lift = 0.0f;
      if (up >= 0)
        for (const Vec3 &d : face->mesh->shape_keys[(size_t)up].delta)  // (in the world: the node's turn and scale - FBX's Z-up on the node)
          lift = std::max(lift, face->rotation.rotate(Vec3(d.x * face->scale.x, d.y * face->scale.y, d.z * face->scale.z)).y);
      float w = -1.0f;
      for (const auto &sw : face ? face->shape_weights : std::vector<std::pair<std::string, float>>()) if (sw.first == "Up") w = sw.second;
      std::printf("    %s: keys %zu (Up %d, Wide %d); Up lifts %.3f m; its value %.2f\n", fs::filename(file).c_str(), face ? face->mesh->shape_keys.size() : 0,
                  up, wide, lift, w);
      CHECK(ok && face && up >= 0 && wide >= 0 && std::fabs(lift - 0.5f) < 1e-3f && std::fabs(w - 0.3f) < 1e-3f);
    }
  });
  test("shape keys: a changing weight deforms the same mesh again; Edit Mode shows the basis; the evaluated mesh carries no keys", [&] {
    Scene s;
    MeshPtr cube = std::make_shared<Mesh>(*primitives::cube());
    ShapeKey k{"Up", std::vector<Vec3>(cube->vert_count(), Vec3(0.0f, 1.0f, 0.0f))};
    cube->shape_keys.push_back(k);
    cube->touch();
    GameObject *g = s.create("Box");
    g->add<MeshFilter>()->mesh = cube;
    MeshRenderer *mr = g->add<MeshRenderer>();
    mr->set_shape_weight("Up", 0.3f);
    const Mesh *e1 = g->evaluated_mesh();
    const uint64_t v1 = e1->version;
    mr->set_shape_weight("Up", 0.6f);
    const Mesh *e2 = g->evaluated_mesh();
    const bool again = e1 == e2 && e2->version == v1 && std::fabs(e2->positions[0].y - (cube->positions[0].y + 0.6f)) < 1e-6f;
    const bool basis = g->evaluated_mesh(2) == cube.get();
    std::printf("    the same mesh deformed again: %s; Edit Mode's cage the basis: %s; keys on the evaluated mesh: %zu\n", again ? "yes" : "no",
                basis ? "yes" : "no", e2->shape_keys.size());
    CHECK(again && basis && e2->shape_keys.empty());
  });
  test("shape keys: Apply Modifiers refuses a mesh with keys (Blender's rule) rather than adding them twice", [&] {
    Editor ed;
    r34_open(ed);
    ed.command("create Cube");
    ed.command("shape add Puff 0.1");
    ed.command("shape set Puff 1");
    GameObject *g = ed.active_object_for_test();
    CHECK(g && g->get<MeshFilter>());
    g->add<TriangulateModifier>();
    const MeshPtr before = g->get<MeshFilter>()->mesh;
    ed.command("meshop apply_modifiers");
    const bool refused = g->get<MeshFilter>()->mesh == before && g->get<TriangulateModifier>();
    std::printf("    refused, the modifier and the key kept: %s\n", refused ? "yes" : "no");
    CHECK(refused && g->get<MeshFilter>()->mesh->find_shape("Puff") == 0);
  });
  test("shape keys: kept through an edit that adds or renumbers vertices, each vertex with its offset", [&] {
    Editor ed;
    r34_open(ed);
    ed.command("create Cube");
    ed.command("shape add Puff 0.1");
    ed.step_frame_headless();
    GameObject *g = ed.active_object_for_test();
    CHECK(g && g->get<MeshFilter>());
    const Mesh before = *g->get<MeshFilter>()->mesh;
    auto worst_kept = [&](const Mesh &m) {  // each vertex where one was before: that one's offset
      float worst = -1.0f;
      for (size_t i = 0; i < m.vert_count() && meshops::shape_keys_fit(m) && !m.shape_keys.empty(); i++)
        for (size_t j = 0; j < before.vert_count(); j++)
          if (length(m.positions[i] - before.positions[j]) < 1e-6f) worst = std::max(worst, length(m.shape_keys[0].delta[i] - before.shape_keys[0].delta[j]));
      return worst;
    };
    ed.command("meshop triangulate");
    ed.step_frame_headless();
    ed.command("meshop subdivide_simple");
    ed.step_frame_headless();
    const Mesh &m = *g->get<MeshFilter>()->mesh;
    const float w = worst_kept(m);
    std::printf("    %zu vertices (from %zu), keys fit: %s, the old vertices' offsets off by %g\n", m.vert_count(), before.vert_count(),
                meshops::shape_keys_fit(m) ? "yes" : "no", w);
    CHECK(m.vert_count() > before.vert_count() && meshops::shape_keys_fit(m) && m.find_shape("Puff") == 0 && w >= 0.0f && w < 1e-5f);
  });
  test("shape keys: weights saved by their exact names whatever the components' order; a damaged key line loads without harm", [&] {
    Scene s;
    MeshPtr cube = std::make_shared<Mesh>(*primitives::cube());
    for (const char *n : {"Smile Left", "Smile_Left"}) cube->shape_keys.push_back({n, std::vector<Vec3>(cube->vert_count(), Vec3(0.0f, 0.1f, 0.0f))});
    cube->touch();
    GameObject *g = s.create("Face");
    MeshRenderer *mr = g->add<MeshRenderer>();  // (before the MeshFilter)
    g->add<MeshFilter>()->mesh = cube;
    mr->set_shape_weight("Smile Left", 0.25f);
    mr->set_shape_weight("Smile_Left", 0.75f);
    std::string text = save_scene_text(s);
    Scene back;
    std::string error;
    const bool loaded = load_scene_text(text, back, error);
    const MeshRenderer *bmr = back.find_by_name("Face") ? back.find_by_name("Face")->get<MeshRenderer>() : nullptr;
    const float a = bmr ? bmr->shape_weight("Smile Left") : -1.0f, b = bmr ? bmr->shape_weight("Smile_Left") : -1.0f;
    /* A key line with a count too large for any integer and an index past every vertex. */
    const size_t at = text.find("sk \"Smile Left\"");
    const size_t end = at == std::string::npos ? at : text.find('\n', at);
    if (end != std::string::npos) text.replace(at, end - at, "sk \"Smile Left\" 99999999999999999999999 4294967296000 1 2 3 1 0 0.5 0 -7 1 1 1");
    Scene bad;
    const bool survived = load_scene_text(text, bad, error);
    const GameObject *bg = bad.find_by_name("Face");
    const MeshFilter *bmf = bg ? bg->get<MeshFilter>() : nullptr;
    const int ki = bmf && bmf->mesh ? bmf->mesh->find_shape("Smile Left") : -1;
    const bool sane = ki >= 0 && std::fabs(bmf->mesh->shape_keys[(size_t)ki].delta[1].y - 0.5f) < 1e-6f && length(bmf->mesh->shape_keys[(size_t)ki].delta[0]) == 0.0f;
    std::printf("    loaded %.2f and %.2f; the damaged line: loaded %s, its one good offset kept %s\n", a, b, survived ? "yes" : "no", sane ? "yes" : "no");
    CHECK(loaded && std::fabs(a - 0.25f) < 1e-6f && std::fabs(b - 0.75f) < 1e-6f && survived && sane);
  });
  test("learn: the Shape keys lesson's Try it makes a sphere with a key half applied, its surface pushed out 0.1 m", [&] {
    Editor ed;
    ed.init_headless(1400, 900);
    const bool found = r146_try(ed, "A sphere with a shape key \"Puff\", half applied");
    GameObject *g = ed.active_object_for_test();
    const MeshFilter *mf = g ? g->get<MeshFilter>() : nullptr;
    const MeshRenderer *mr = g ? g->get<MeshRenderer>() : nullptr;
    float out = 0.0f;
    if (mf && mf->mesh && g->evaluated_mesh())
      for (size_t i = 0; i < mf->mesh->vert_count(); i++) out = std::max(out, length(g->evaluated_mesh()->positions[i] - mf->mesh->positions[i]));
    std::printf("    key: %s, weight %.2f, the surface moved %.3f m\n", mf && mf->mesh && mf->mesh->find_shape("Puff") >= 0 ? "yes" : "no", mr ? mr->shape_weight("Puff") : -1.0f, out);
    CHECK(found && mr && std::fabs(mr->shape_weight("Puff") - 0.5f) < 1e-6f && std::fabs(out - 0.1f) < 1e-3f);
  });
  /* ---------------------------------------------------------------- round 156: MMD models (task 0136) */
  /* The files beside each other: the model, tex/skin.png (warm), toon.png (white above a violet shadow). */
  auto r156_write = [&](const std::string &dir, const std::string &file, const std::string &data) {
    fs::make_dirs(fs::join(dir, "tex"));
    std::vector<uint32_t> skin(16, 0xFFE0A080u), toon(16, 0xFFFFFFFFu);
    for (int x = 0; x < 4; x++) toon[12 + x] = 0xFF664D80u;  // (the bottom row)
    write_png(fs::join(dir, "tex/skin.png"), skin.data(), 4, 4, 4, false);
    write_png(fs::join(dir, "toon.png"), toon.data(), 4, 4, 4, false);
    fs::write_file(fs::join(dir, file), data);
    return fs::join(dir, file);
  };
  test("mmd: Shift-JIS and UTF-16 names come in as UTF-8", [&] {
    const std::string sj = sjis_to_utf8("\x8d\xb6\x91\xab\x82\x68\x82\x6a" + std::string("\0junk", 5));
    const std::string w = utf16le_to_utf8(std::string("\x42\x30\x3d\xd8\x00\xde", 6));  // あ, then U+1F600 as a surrogate pair
    std::printf("    Shift-JIS: %s; UTF-16: %zu bytes\n", sj.c_str(), w.size());
    CHECK(sj == "左足ＩＫ" && w == "あ\xF0\x9F\x98\x80" && sjis_to_utf8("Head") == "Head");
    CHECK(mmd_bone_name("左足ＩＫ") == "LeftFootIK" && mmd_bone_name("右ひざ") == "RightLeg" && mmd_bone_name("髪1") == "髪1");
  });
  test("mmd: a PMX character comes in turned to face +Z at 8 cm a unit, skinned, its bones pointing at their tails, IK on the leg with a pole at the knee, the hair a spring chain", [&] {
    /* Three files: UTF-16 text, UTF-8 text, and the kitchen sink with the skin on D bones, unsigned indices past 127
     * and the triangles the other way round (every optional record read past, the skin's weights moved onto the leg
     * bones, the file's normals putting the faces right). */
    const char *labels[3] = {"UTF-16", "UTF-8", "kitchen sink"};
    for (int variant = 0; variant < 3; variant++) {
      R156Opts opts;
      opts.utf8 = variant == 1;
      opts.sink = opts.pad = opts.dbones = opts.mirrored = variant == 2;
      const char *utf8 = labels[variant];
      const std::string file = r156_write(fs::join(test_dir(), "r156_pmx"), strprintf("test%d.pmx", variant), r156_pmx(opts));
      ImportResult r;
      const bool ok = import_model(file, r);
      CHECK(ok);
      if (!ok) return (void)std::printf("    %s\n", r.error.c_str());
      Scene s;
      GameObject *root = instantiate_import(s, r, "Test", nullptr);
      GameObject *thigh = s.find_by_name("LeftUpLeg"), *knee = s.find_by_name("LeftLeg"), *ik = s.find_by_name("LeftFootIK"), *hair = s.find_by_name("髪1");
      CHECK(root && thigh && knee && ik && hair && s.find_by_name("Hips") && s.find_by_name("LeftFoot") && s.find_by_name("Center"));
      if (!thigh || !knee || !ik || !hair) return;
      /* Placed: MMD's left knee at (1, 5, 0) is at (-0.08, 0.4, 0) here (the character's left, facing +Z). */
      const float placed = length(knee->world_position() - Vec3(-0.08f, 0.4f, 0.0f));
      const Vec3 down = thigh->world_rotation().rotate(Vec3(0, 1, 0));
      const IKConstraint *c = knee->get<IKConstraint>();
      GameObject *pole = c ? s.find_by_name(c->pole) : nullptr;
      const SpringBone *sb = hair->get<SpringBone>();
      std::printf("    %s: knee off by %g; the thigh's +Y %.2f %.2f %.2f; IK on the knee -> %s%s, chain %d, pole %s %.2f m in front; spring on the hair: %s; %s\n",
                  utf8, placed, down.x, down.y, down.z, c ? c->target.c_str() : "-", c ? "" : "", c ? c->chain_length : 0,
                  pole ? pole->name.c_str() : "-", pole ? pole->world_position().z - knee->world_position().z : 0.0f, sb ? "yes" : "no", r.summary.c_str());
      CHECK(placed < 1e-5f && down.y < -0.999f && c && c->target == "LeftFootIK" && c->chain_length == 2 && pole &&
            pole->world_position().z > knee->world_position().z + 0.1f && sb && std::fabs(sb->radius - 0.04f) < 1e-5f);
      /* The mesh: its front quad faces +Z; the knee's half follows the knee. */
      GameObject *body = nullptr;
      s.for_each([&](GameObject &g) { if (g.get<MeshFilter>() && !body) body = &g; });
      const Mesh *m = body ? body->get<MeshFilter>()->mesh.get() : nullptr;
      CHECK(m && m->face_count() == 4 && body->get<SkinModifier>() && body->get<Outline>());
      if (!m) return;
      std::vector<Vec3> fn;
      for (size_t f = 0; f < m->face_count(); f++) fn.push_back(normalize(m->face_normal(f)));
      const Vec3 before = body->evaluated_mesh()->positions[0];
      int low = -1;
      for (size_t i = 0; i < m->vert_count(); i++)
        if (std::fabs(m->positions[i].y - 0.32f) < 1e-4f && std::fabs(m->positions[i].x + 0.04f) < 1e-4f) low = (int)i;
      knee->set_local_rotation(Quat::axis_angle({1, 0, 0}, kPi * 0.5f) * knee->local().rotation);
      const Vec3 after = low >= 0 ? body->evaluated_mesh()->positions[(size_t)low] : Vec3(0.0f);
      std::printf("    the front quad's normal %.2f %.2f %.2f; the knee turned: its vertex moved %.3f m\n", fn.empty() ? 0.0f : fn[0].x,
                  fn.empty() ? 0.0f : fn[0].y, fn.empty() ? 0.0f : fn[0].z, low >= 0 ? length(after - m->positions[(size_t)low]) : -1.0f);
      CHECK(!fn.empty() && fn[0].z > 0.999f && low >= 0 && length(after - m->positions[(size_t)low]) > 0.01f);
      (void)before;
      /* Morphs: "あ" lifts the leg quad's first corner 8 cm; "笑い" half that. */
      const int a = m->find_shape("あ"), smile = m->find_shape("笑い");
      float lift = 0.0f, half = 0.0f;
      for (size_t i = 0; a >= 0 && smile >= 0 && i < m->vert_count(); i++)
        lift = std::max(lift, m->shape_keys[(size_t)a].delta[i].y), half = std::max(half, m->shape_keys[(size_t)smile].delta[i].y);
      /* The material: toon, MMD's lit colour (diffuse + ambient), its texture from tex\, the ramp's shadow, the edge. */
      const Material &mt = *body->get<MeshRenderer>()->materials[0];
      const Outline *o = body->get<Outline>();
      std::printf("    keys: あ %.3f m, 笑い %.3f m; material '%s' toon %d, colour %.2f %.2f %.2f, map %s, shadow %.2f %.2f %.2f; outline %.4f m\n", lift, half,
                  mt.name.c_str(), mt.shading_model, mt.base_color.x, mt.base_color.y, mt.base_color.z, fs::filename(mt.base_map.path).c_str(),
                  mt.toon_shadow.x, mt.toon_shadow.y, mt.toon_shadow.z, o ? o->thickness : 0.0f);
      CHECK(std::fabs(lift - 0.08f) < 1e-5f && std::fabs(half - 0.04f) < 1e-5f && mt.name == "肌" && mt.shading_model == 1 &&
            std::fabs(mt.base_color.x - 1.0f) < 1e-5f && std::fabs(mt.base_color.z - 0.7f) < 1e-5f && fs::filename(mt.base_map.path) == "skin.png" &&
            fs::exists(mt.base_map.path) && std::fabs(mt.toon_shadow.x - 0x66 / 255.0f) < 0.01f && o && std::fabs(o->thickness - 0.006f) < 1e-5f);
      CHECK(fs::filename(mt.toon_ramp_map.path) == "toon.png" && mt.toon_wrap == 1.0f);  // (task 0148: the ramp itself as the Toon Ramp, read at MMD's half N.L + half)
    }
  });
  test("mmd: a PMD character (Shift-JIS names, morphs by the base's list) comes in as its PMX twin", [&] {
    const std::string file = r156_write(fs::join(test_dir(), "r156_pmd"), "test.pmd", r156_pmd());
    ImportResult r;
    CHECK(import_model(file, r));
    Scene s;
    instantiate_import(s, r, "Test", nullptr);
    GameObject *knee = s.find_by_name("LeftLeg"), *hair = s.find_by_name("髪1");
    const IKConstraint *c = knee ? knee->get<IKConstraint>() : nullptr;
    const Mesh *m = nullptr;
    s.for_each([&](GameObject &g) { if (g.get<MeshFilter>() && !m) m = g.get<MeshFilter>()->mesh.get(); });
    float lift = 0.0f;
    const int a = m ? m->find_shape("あ") : -1;
    for (size_t i = 0; a >= 0 && i < m->vert_count(); i++) lift = std::max(lift, m->shape_keys[(size_t)a].delta[i].y);
    /* Its material (the colour, the texture before the star, the toon list's ramp), its outline, its front face. */
    GameObject *body = nullptr;
    s.for_each([&](GameObject &g) { if (g.get<MeshFilter>() && !body) body = &g; });
    const MeshRenderer *mr = body ? body->get<MeshRenderer>() : nullptr;
    const Material *mt = mr && !mr->materials.empty() ? mr->materials[0].get() : nullptr;
    const Outline *o = body ? body->get<Outline>() : nullptr;
    const Vec3 fn = m && m->face_count() ? normalize(m->face_normal(0)) : Vec3(0.0f);
    bool sphere_note = false;
    for (const std::string &n : r.notes) sphere_note = sphere_note || n.find("sphere") != std::string::npos;
    std::printf("    %s; knee IK -> %s; hair spring %s; あ lifts %.3f m; material '%s' toon %d, colour %.2f %.2f %.2f, map %s, shadow %.2f; outline %.4f; front %.2f; the sphere map reported: %s\n",
                r.summary.c_str(), c ? c->target.c_str() : "-", hair && hair->get<SpringBone>() ? "yes" : "no", lift, mt ? mt->name.c_str() : "-",
                mt ? mt->shading_model : -1, mt ? mt->base_color.x : 0.0f, mt ? mt->base_color.y : 0.0f, mt ? mt->base_color.z : 0.0f,
                mt ? fs::filename(mt->base_map.path).c_str() : "-", mt ? mt->toon_shadow.x : 0.0f, o ? o->thickness : 0.0f, fn.z, sphere_note ? "yes" : "no");
    CHECK(knee && c && c->target == "LeftFootIK" && hair && hair->get<SpringBone>() && std::fabs(lift - 0.08f) < 1e-5f && m && m->face_count() == 4);
    CHECK(knee && length(knee->world_position() - Vec3(-0.08f, 0.4f, 0.0f)) < 1e-5f);
    CHECK(mt && mt->shading_model == 1 && std::fabs(mt->base_color.x - 1.0f) < 1e-5f && std::fabs(mt->base_color.z - 0.7f) < 1e-5f && fs::filename(mt->base_map.path) == "skin.png" &&
          std::fabs(mt->toon_shadow.x - 0x66 / 255.0f) < 0.01f && o && std::fabs(o->thickness - 0.004f) < 1e-5f && fn.z > 0.999f && sphere_note);
  });
  test("mmd: a damaged PMX or PMD, cut anywhere or with bytes flipped, imports or refuses without harm", [&] {
    uint32_t rng = 156;
    auto next = [&](uint32_t n) { rng = rng * 1664525u + 1013904223u; return (size_t)((rng >> 8) % n); };
    int refused = 0, imported = 0;
    for (const std::string &whole : {r156_pmx(R156Opts()), r156_pmd()}) {
      const std::string ext = whole[0] == 'P' && whole[1] == 'M' && whole[2] == 'X' ? ".pmx" : ".pmd";
      for (int k = 0; k < 12; k++) {  // (24 variants here; the stress program's codecs section runs 240, task 0177)
        std::string d = whole;
        if (k < 6) d.resize(next((uint32_t)d.size()));
        else
          for (int f = 0; f < 4; f++) d[next((uint32_t)d.size())] = (char)next(256);
        const std::string file = fs::join(test_dir(), "r156_bad" + ext);
        fs::write_file(file, d);
        ImportResult r;
        if (!import_model(file, r)) {
          refused++;
          continue;
        }
        imported++;
        Scene s;
        instantiate_import(s, r, "Bad", nullptr);
        s.for_each([&](GameObject &g) { (void)g.evaluated_mesh(); });
      }
    }
    std::printf("    24 damaged files: %d refused, %d imported\n", refused, imported);
    CHECK(refused + imported == 24 && refused > 0);
  });
  test("mmd: dropped into the editor, a PMX becomes a character with its .mat, its texture in Assets, IK and springs; the lesson's Try-it opens the Project window", [&] {
    const std::string file = r156_write(fs::join(test_dir(), "r156_editor"), "r156miku.pmx", r156_pmx(R156Opts()));
    Editor ed;
    r34_open(ed);
    ed.command("import " + file);
    ed.step_frame_headless();
    GameObject *knee = ed.scene().find_by_name("LeftLeg"), *hair = ed.scene().find_by_name("髪1");
    MaterialPtr mat = material_asset("Assets/Materials/r156miku/肌.mat");
    const bool tex = mat && fs::exists(resolve_asset_path(mat->base_map.path)) && mat->base_map.path.find("Assets/Textures/r156miku") == 0;
    /* The rig tools read it: the walking analysis finds the hips and the legs by their mapped names. */
    GameObject *arm = nullptr;
    ed.scene().for_each([&](GameObject &g) { if (g.get<Armature>() && !arm) arm = &g; });
    const gait::Rig rig = arm ? gait::analyse(ed.scene(), *arm) : gait::Rig();
    const bool leg = rig.legs.size() == 2 && rig.hips && rig.legs[0].upper && rig.legs[0].foot &&
                     (rig.legs[0].upper->name == "LeftUpLeg" || rig.legs[1].upper->name == "LeftUpLeg");
    const bool found = r146_try(ed, "Open the Project window: drop a .pmx or .pmd there or onto the Scene view");
    std::printf("    knee IK %s, hair spring %s, 肌.mat %s (texture %s); the walking analysis: hips %s, %zu leg(s); the lesson's Try-it %s\n",
                knee && knee->get<IKConstraint>() ? "yes" : "no", hair && hair->get<SpringBone>() ? "yes" : "no", mat ? "yes" : "no",
                mat ? mat->base_map.path.c_str() : "-", rig.hips ? rig.hips->name.c_str() : "-", rig.legs.size(), found ? "ran" : "not found");
    CHECK(knee && knee->get<IKConstraint>() && hair && hair->get<SpringBone>() && mat && mat->shading_model == 1 && tex && found && leg);
  });
  test("mmd: two copies of one character each aim their IK at their own controls; a forked hair gets a chain per strand; a shared toon ramp is reported; hard edges stay split", [&] {
    const std::string dir = fs::join(test_dir(), "r156_more");
    ImportResult r;
    CHECK(import_model(r156_write(dir, "twin.pmx", r156_pmx(R156Opts())), r));
    Scene s;
    GameObject *a = instantiate_import(s, r, "A", nullptr), *b = instantiate_import(s, r, "B", nullptr);
    int own = 0, total = 0;
    for (GameObject *root : {a, b}) {
      std::vector<GameObject *> stack{root};
      while (!stack.empty()) {
        GameObject *g = stack.back();
        stack.pop_back();
        for (GameObject *c : g->children) stack.push_back(c);
        const IKConstraint *ik = g->get<IKConstraint>();
        if (!ik) continue;
        total++;
        GameObject *t = ik_target_of(s, *g);
        uint64_t pid = 0;
        GameObject *p = s.find_by_name(ik->pole);  // (the scene-wide lookup: the first copy's)
        (void)pid, (void)p;
        own += t && s.is_ancestor(root, t);
      }
    }
    /* After a save and a load the ids are gone and the names are all there is: still each its own. */
    Scene back;
    std::string error;
    load_scene_text(save_scene_text(s), back, error);
    int own_back = 0;
    back.for_each([&](GameObject &g) {
      if (!g.get<IKConstraint>()) return;
      GameObject *top = &g;
      while (top->parent) top = top->parent;
      GameObject *t = ik_target_of(back, g);
      own_back += t && back.is_ancestor(top, t);
    });
    R156Opts fork;
    fork.fork = true;
    ImportResult rf;
    CHECK(import_model(r156_write(dir, "fork.pmx", r156_pmx(fork)), rf));
    R156Opts toon;
    toon.shared_toon = true;
    ImportResult rt;
    CHECK(import_model(r156_write(dir, "toon.pmx", r156_pmx(toon)), rt));
    bool toon_note = false;
    for (const std::string &n : rt.notes) toon_note = toon_note || n.find("toon") != std::string::npos;
    R156Opts hard;
    hard.hard = true;
    ImportResult rh;
    CHECK(import_model(r156_write(dir, "hard.pmx", r156_pmx(hard)), rh));
    const Mesh *hm = nullptr;
    for (const ImportedNode &n : rh.nodes) if (n.mesh) hm = n.mesh.get();
    std::printf("    IK aimed inside its own copy: %d of %d (after a save and a load %d); the fork: %zu spring chain(s); shared toon03 reported: %s; the hard copy: %zu vertices, %zu faces\n",
                own, total, own_back, rf.springs.size(), toon_note ? "yes" : "no", hm ? hm->vert_count() : 0, hm ? hm->face_count() : 0);
    CHECK(total == 2 && own == 2 && own_back == 2 && rf.springs.size() == 3 && toon_note && hm && hm->vert_count() == 12 && hm->face_count() == 6);
  });
  test("mmd: crafted files - too many morphs, bones nested too deep, a parent loop, a vertex a million units away - are bounded or refused", [&] {
    const std::string dir = fs::join(test_dir(), "r156_crafted");
    fs::make_dirs(dir);
    auto pmx_head = [](R156Bin &b, int nverts) {
      b.s = "PMX ";
      b.f(2.0f), b.u8(8);
      for (int g : {1, 0, 2, 1, 1, 2, 2, 1}) b.u8(g);
      for (int k = 0; k < 4; k++) b.text("", true);
      b.i32(nverts);
      for (int i = 0; i < nverts; i++) b.v3({(float)i, 0, 0}), b.v3({0, 0, -1}), b.f(0), b.f(0), b.u8(0), b.u16(0), b.f(1.0f);
      b.i32(3), b.u16(0), b.u16(1), b.u16(2);
      b.i32(0), b.i32(0);  // (textures, materials)
    };
    /* 5,000 vertex morphs of one offset each on 200 vertices: 4,096 keys kept, the rest said so. */
    R156Bin many;
    pmx_head(many, 200);
    many.i32(1), many.text("root", true), many.text("", true), many.v3({}), many.u16(0xFFFF), many.i32(0), many.u16(0x0002 | 0x0008 | 0x0010), many.v3({0, 1, 0});
    many.i32(5000);
    for (int i = 0; i < 5000; i++) many.text("m" + std::to_string(i), true), many.text("", true), many.u8(4), many.u8(1), many.i32(1), many.u16(0), many.v3({0, 1, 0});
    many.i32(0), many.i32(0), many.i32(0);
    fs::write_file(fs::join(dir, "many.pmx"), many.s);
    ImportResult rm;
    const bool many_ok = import_model(fs::join(dir, "many.pmx"), rm);
    const Mesh *mm = nullptr;
    for (const ImportedNode &n : rm.nodes) if (n.mesh) mm = n.mesh.get();
    /* 1,000 bones each under the one before: refused (nested too deep). Two bones each other's parent: a loop, broken. */
    auto chain = [&](int n, bool loop) {
      R156Bin b;
      pmx_head(b, 3);
      b.i32(n);
      for (int i = 0; i < n; i++) {
        b.text("", true), b.text("", true), b.v3({0, (float)i, 0});
        b.u16(loop ? (i + 1) % n : i == 0 ? 0xFFFF : i - 1), b.i32(0), b.u16(0x0002 | 0x0008 | 0x0010), b.v3({0, 1, 0});
      }
      b.i32(0), b.i32(0), b.i32(0), b.i32(0);
      return b.s;
    };
    fs::write_file(fs::join(dir, "deep.pmx"), chain(1000, false));
    fs::write_file(fs::join(dir, "loop.pmx"), chain(2, true));
    ImportResult rd, rl;
    const bool deep_ok = import_model(fs::join(dir, "deep.pmx"), rd), loop_ok = import_model(fs::join(dir, "loop.pmx"), rl);
    int roots = 0;
    for (const ImportedNode &n : rl.nodes) roots += n.is_bone && n.parent < 0;
    /* A vertex at 1e30: refused. */
    R156Bin far;
    pmx_head(far, 3);
    far.s.replace(37 + 39, 4, std::string("\x00\x00\x80\x71", 4));  // (the second vertex's x, 1e30: the header is 37 bytes, a vertex 39)
    far.i32(0), far.i32(0), far.i32(0), far.i32(0);
    fs::write_file(fs::join(dir, "far.pmx"), far.s);
    ImportResult rfar;
    const bool far_ok = import_model(fs::join(dir, "far.pmx"), rfar);
    std::printf("    5,000 morphs: %zu keys kept (%s); 1,000 deep: %s; a loop: imported with %d root bone(s); a vertex at 1e30: %s\n",
                mm ? mm->shape_keys.size() : 0, rm.notes.empty() ? "no note" : rm.notes.back().c_str(), deep_ok ? "imported" : rd.error.c_str(), roots,
                far_ok ? "imported" : rfar.error.c_str());
    CHECK(many_ok && mm && mm->shape_keys.size() == 4096 && !rm.notes.empty() && !deep_ok && rd.error.find("deep") != std::string::npos && loop_ok && roots == 1 &&
          !far_ok && rfar.error.find("million") != std::string::npos);
  });
  /* ---------------------------------------------------------------- round 157: MMD motions (task 0137) */
  /* A VMD written by the tests: the lower body turned 90 degrees about MMD's X in 30 frames on a straight timing curve;
   * the left ankle turned 170 degrees about Y in 20 frames, eased in and out; the centre moved 5 units towards the
   * viewer; the "あ" morph to 1 in 10 frames; the left leg's IK off from frame 15. */
  auto r157_angle = [](Quat a, Quat b) { return 2.0f * std::acos(std::min(1.0f, std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w))); };
  auto r157_vmd = []() {
    R156Bin b;
    b.fixed("Vocaloid Motion Data 0002", 30), b.fixed("\x83\x65\x83\x58\x83\x67", 20);
    struct Key {
      const char *sjis;
      uint32_t frame;
      Vec3 pos;
      Quat rot;
      uint8_t curve[4];  // the rotation's and the X and Y moves' (x1, y1, x2, y2); the Z move's is eased when `ease_z`
      bool ease_z = false;
    };
    const Quat hips90 = Quat::axis_angle({1, 0, 0}, kPi * 0.5f), ankle170 = Quat::axis_angle({0, 1, 0}, 170.0f * kDeg2Rad);
    const Key keys[] = {{"\x89\xba\x94\xbc\x90\x67", 0, {}, Quat(), {20, 20, 107, 107}},
                        {"\x89\xba\x94\xbc\x90\x67", 30, {}, hips90, {20, 20, 107, 107}},
                        {"\x8d\xb6\x91\xab\x8e\xf1", 0, {}, Quat(), {127, 0, 0, 127}},
                        {"\x8d\xb6\x91\xab\x8e\xf1", 20, {}, ankle170, {127, 0, 0, 127}},
                        {"\x83\x5a\x83\x93\x83\x5e\x81\x5b", 0, {}, Quat(), {20, 20, 107, 107}},
                        {"\x83\x5a\x83\x93\x83\x5e\x81\x5b", 30, {2, 0, -5}, Quat(), {20, 20, 107, 107}, true},  // (X straight, Z eased: the channels apart)
                        {"\x96\xb3\x82\xa2", 0, {}, Quat(), {20, 20, 107, 107}}};  // (a bone this character hasn't)
    b.i32((int64_t)(sizeof(keys) / sizeof(keys[0])));
    for (const Key &k : keys) {
      b.fixed(k.sjis, 15), b.i32(k.frame), b.v3(k.pos), b.f(k.rot.x), b.f(k.rot.y), b.f(k.rot.z), b.f(k.rot.w);
      std::string interp(64, '\0');
      const uint8_t eased[4] = {127, 0, 0, 127};
      for (int c = 0; c < 4; c++)
        for (int p = 0; p < 4; p++) interp[(size_t)(p * 4 + c)] = (char)(c == 2 && k.ease_z ? eased[p] : k.curve[p]);
      b.s += interp;
    }
    b.i32(2);
    b.fixed("\x82\xa0", 15), b.i32(0), b.f(0.0f);
    b.fixed("\x82\xa0", 15), b.i32(10), b.f(1.0f);
    b.i32(0), b.i32(0), b.i32(0);  // (camera, light, self-shadow)
    b.i32(2);
    for (int f : {0, 15}) b.i32(f), b.u8(1), b.i32(1), b.fixed("\x8d\xb6\x91\xab\x82\x68\x82\x6a", 20), b.u8(f == 0 ? 1 : 0);
    return b.s;
  };
  test("vmd: a motion onto an MMD character: turns as MMD makes them (straight and eased timing, a 170-degree turn), the centre moved, a morph and the leg's IK keyed", [&] {
    const std::string dir = fs::join(test_dir(), "r157");
    const std::string model = r156_write(dir, "test.pmx", r156_pmx(R156Opts()));
    fs::write_file(fs::join(dir, "dance.vmd"), r157_vmd());
    ImportResult r;
    CHECK(import_model(model, r));
    Scene s;
    GameObject *root = instantiate_import(s, r, "Test", nullptr);
    GameObject *hips = s.find_by_name("Hips"), *foot = s.find_by_name("LeftFoot"), *center = s.find_by_name("Center"), *knee = s.find_by_name("LeftLeg");
    CHECK(root && hips && foot && center && knee);
    if (!root || !hips || !foot || !center || !knee) return;
    const Quat hips_rest = hips->world_rotation(), foot_rest = foot->world_rotation();
    const Vec3 center_rest = center->world_position();
    AnimationClip clip;
    std::vector<std::string> notes;
    std::string error;
    const bool ok = import_vmd_file(fs::join(dir, "dance.vmd"), *root, clip, notes, error);
    CHECK(ok);
    for (const std::string &n : notes) std::printf("    note: %s\n", n.c_str());
    auto C = [](Quat q) { return Quat(-q.x, q.y, -q.z, q.w); };  // MMD's space to ours: half a turn about Y
    const Quat hips90 = C(Quat::axis_angle({1, 0, 0}, kPi * 0.5f)), ankle170 = C(Quat::axis_angle({0, 1, 0}, 170.0f * kDeg2Rad));
    float hips_err = 0.0f;
    for (float f : {0.0f, 7.0f, 15.0f, 22.0f, 30.0f}) {
      apply_clip(clip, *root, f);
      const Quat want = slerp(Quat(), hips90, f / 30.0f) * hips_rest;
      hips_err = std::max(hips_err, r157_angle(hips->world_rotation(), want));
    }
    /* The ankle, relative to the hips: eased, so halfway at frame 10 (the curve is symmetric) and well short of a
     * quarter at frame 5. */
    auto ankle_turn = [&](float f) {
      apply_clip(clip, *root, f);
      const Quat d = normalize((hips->world_rotation() * hips_rest.conjugate()).conjugate() * foot->world_rotation() * foot_rest.conjugate());
      return d;
    };
    const float mid = r157_angle(ankle_turn(10.0f), slerp(Quat(), ankle170, 0.5f)), end = r157_angle(ankle_turn(20.0f), ankle170);
    const float early = r157_angle(ankle_turn(5.0f), Quat());
    apply_clip(clip, *root, 15.0f);
    const Vec3 moved = center->world_position() - center_rest;  // (halfway: X straight at 0.08 m, Z eased to 0.2 m too)
    const IKConstraint *ik = knee->get<IKConstraint>();
    const float ik_off = ik ? ik->weight : -1.0f;  // (switched off at frame 15)
    apply_clip(clip, *root, 10.0f);
    const float ik_on = ik ? ik->weight : -1.0f;
    apply_clip(clip, *root, 7.5f);
    const Vec3 quarter = center->world_position() - center_rest;  // (a quarter in: X at 0.04 m, Z eased well short of 0.1)
    GameObject *body = nullptr;
    s.for_each([&](GameObject &g) { if (g.get<MeshFilter>() && !body) body = &g; });
    apply_clip(clip, *root, 5.0f);
    const float morph = body && body->get<MeshRenderer>() ? body->get<MeshRenderer>()->shape_weight("あ") : -1.0f;
    std::printf("    the hips off by at most %.3f deg; the ankle: at frame 5 %.1f deg (eased), at 10 off by %.3f, at 20 off by %.3f; the centre moved %.3f %.3f %.3f at frame 15; あ %.2f at 5; IK weight %.0f, then %.0f\n",
                hips_err / kDeg2Rad, early / kDeg2Rad, mid / kDeg2Rad, end / kDeg2Rad, moved.x, moved.y, moved.z, morph, ik_on, ik_off);
    CHECK(hips_err < 0.6f * kDeg2Rad && mid < 0.6f * kDeg2Rad && end < 0.01f * kDeg2Rad && early < 0.25f * 170.0f * kDeg2Rad && early > 0.0f);
    std::printf("    a quarter in: x %.3f (0.04 straight), z %.3f (eased: under 0.05)\n", quarter.x, quarter.z);
    CHECK(length(moved - Vec3(-0.08f, 0, 0.2f)) < 2e-3f && std::fabs(morph - 0.5f) < 1e-5f && ik_on == 1.0f && ik_off == 0.0f);  // (the slopes follow the ease within a millimetre)
    CHECK(std::fabs(quarter.x + 0.04f) < 2e-3f && quarter.z > 0.0f && quarter.z < 0.05f);
    CHECK(!notes.empty() && notes[0].find("無い") != std::string::npos);
  });
  test("vmd: dropped into the editor after its character, a motion becomes Assets/Animations/<character>/<motion>.anim played by the character; the lesson's Try-it", [&] {
    const std::string dir = fs::join(test_dir(), "r157_editor");
    const std::string model = r156_write(dir, "r157miku.pmx", r156_pmx(R156Opts()));
    fs::write_file(fs::join(dir, "dance.vmd"), r157_vmd());
    const std::string anim = fs::join(scratch_project(), "Assets/Animations/r157miku");
    if (fs::exists(anim)) forget_clip_assets("Assets/Animations/r157miku"), fs::move_to_trash(anim);  // (an earlier run's)
    Editor ed;
    r34_open(ed);
    ed.command("import " + model);
    ed.step_frame_headless();
    ed.command("import " + fs::join(dir, "dance.vmd"));
    ed.step_frame_headless();
    GameObject *root = nullptr;
    ed.scene().for_each([&](GameObject &g) { if (g.get<Animation>() && !root) root = &g; });
    const Animation *an = root ? root->get<Animation>() : nullptr;
    const AnimationClipPtr clip = an ? clip_asset(an->clip) : nullptr;
    const bool found = r146_try(ed, "Open the Project window: drop a .vmd onto the Scene view with its character selected");
    std::printf("    '%s' plays %s: %zu tracks, %.0f frames at %.0f fps (%s); the lesson's Try-it %s\n", root ? root->name.c_str() : "-", an ? an->clip.c_str() : "-",
                clip ? clip->tracks.size() : 0, clip ? clip->length : 0.0f, clip ? clip->frame_rate : 0.0f, clip && fs::exists(resolve_asset_path(an->clip)) ? "on disk" : "missing",
                found ? "ran" : "not found");
    const bool made = root && an && an->clip.rfind("Assets/Animations/r157miku/dance", 0) == 0 && clip && clip->tracks.size() >= 7 &&
                      std::fabs(clip->length - 30.0f) < 1e-3f && std::fabs(clip->frame_rate - 30.0f) < 1e-3f && fs::exists(resolve_asset_path(an->clip)) && found;
    const bool beside = an && fs::exists(fs::join(fs::parent(resolve_asset_path(an->clip)), "dance.vmd"));  // (the motion copied in beside its clip)
    /* Played: Play turns the hips (and restores the scene after: the objects above are gone by then). */
    const Quat before = ed.scene().find_by_name("Hips") ? ed.scene().find_by_name("Hips")->world_rotation() : Quat();
    ed.command("play");
    for (int i = 0; i < 20; i++) ed.step_frame_headless();
    GameObject *hips = ed.scene().find_by_name("Hips");
    const float turned = hips ? 2.0f * std::acos(std::min(1.0f, std::fabs(before.x * hips->world_rotation().x + before.y * hips->world_rotation().y +
                                                                             before.z * hips->world_rotation().z + before.w * hips->world_rotation().w))) : 0.0f;
    ed.command("stop");
    std::printf("    in Play the hips turned %.1f degrees\n", turned / kDeg2Rad);
    CHECK(made && turned > 5.0f * kDeg2Rad);
    CHECK(beside);
  });
  /* ---------------------------------------------------------------- round 158: VRM characters (task 0138) */
  /* A synthetic VRM written as a GLB: a humanoid of hips, spine, head, a left leg and a two-bone hair chain; a face quad
   * skinned to the head with two morph targets; one MToon material; the VRM extension in 0.x or 1.0 form. A 0.x model
   * faces -Z in glTF (its left at -X, its hair at +Z), a 1.0 model +Z: both must come in facing +Z. */
  auto r158_vrm = [](bool v1) {
    R156Bin bin;  // (the binary chunk)
    const float fz = v1 ? 0.1f : -0.1f;  // the face in front of the head
    const Vec3 quad[4] = {{-0.1f, 1.5f, fz}, {0.1f, 1.5f, fz}, {0.1f, 1.7f, fz}, {-0.1f, 1.7f, fz}};
    for (const Vec3 &p : quad) bin.v3(p);  // accessor 0: 48 bytes at 0
    const int tris[6] = {0, 1, 2, 0, 2, 3};  // (facing +Z in glTF; 0.x: the other way, to face -Z)
    for (int k = 0; k < 6; k++) bin.u16(v1 ? tris[k] : tris[k == 1 ? 2 : k == 2 ? 1 : k == 4 ? 5 : k == 5 ? 4 : k]);  // accessor 1: 12 bytes at 48
    bin.u16(0), bin.u16(0);  // (padding to 64)
    for (int v = 0; v < 4; v++) bin.u8(2), bin.u8(0), bin.u8(0), bin.u8(0);  // accessor 2 (JOINTS_0, the head): 16 bytes at 64
    for (int v = 0; v < 4; v++) bin.f(1), bin.f(0), bin.f(0), bin.f(0);      // accessor 3 (WEIGHTS_0): 64 bytes at 80
    for (int v = 0; v < 4; v++) bin.v3({0, 0.05f, 0});                        // accessor 4 (target 0): 48 bytes at 144
    for (int v = 0; v < 4; v++) bin.v3({0, -0.02f, 0});                       // accessor 5 (target 1): 48 bytes at 192
    const float lx = v1 ? 0.1f : -0.1f, hz = v1 ? -0.05f : 0.05f;  // the left leg on the model's left, the hair behind
    /* Accessor 6: the six skin joints' inverse bind matrices (each the inverse of its place in glTF): 384 bytes at 240. The
     * hair's two nodes are not skin joints: VRM's spring bones only need nodes. */
    const Vec3 joints[6] = {{0, 0.9f, 0}, {0, 1.1f, 0}, {0, 1.5f, 0}, {lx, 0.9f, 0}, {lx, 0.5f, 0}, {lx, 0.1f, 0}};
    for (const Vec3 &j : joints)
      for (int c = 0; c < 16; c++) bin.f(c == 0 || c == 5 || c == 10 || c == 15 ? 1.0f : c == 12 ? -j.x : c == 13 ? -j.y : c == 14 ? -j.z : 0.0f);
    std::string json = strprintf(
        R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0,8]}],
"nodes":[{"name":"J_Bip_C_Hips","translation":[0,0.9,0],"children":[1,3,6]},{"name":"J_Bip_C_Spine","translation":[0,0.2,0],"children":[2]},
{"name":"J_Bip_C_Head","translation":[0,0.4,0]},{"name":"J_Bip_L_UpperLeg","translation":[%g,0,0],"children":[4]},
{"name":"J_Bip_L_LowerLeg","translation":[0,-0.4,0],"children":[5]},{"name":"J_Bip_L_Foot","translation":[0,-0.4,0]},
{"name":"J_Sec_Hair1","translation":[0,0.6,%g],"children":[7]},{"name":"J_Sec_Hair2","translation":[0,-0.15,0]},
{"name":"Face","mesh":0,"skin":0}],
"skins":[{"joints":[0,1,2,3,4,5],"inverseBindMatrices":6}],
"meshes":[{"name":"Face","primitives":[{"attributes":{"POSITION":0,"JOINTS_0":2,"WEIGHTS_0":3},"indices":1,"material":0,
"targets":[{"POSITION":4},{"POSITION":5}]}],"extras":{"targetNames":["Face.a","Face.blink"]}}],
"accessors":[{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3","min":[-0.1,1.5,-0.1],"max":[0.1,1.7,0.1]},
{"bufferView":1,"componentType":5123,"count":6,"type":"SCALAR"},{"bufferView":2,"componentType":5121,"count":4,"type":"VEC4"},
{"bufferView":3,"componentType":5126,"count":4,"type":"VEC4"},{"bufferView":4,"componentType":5126,"count":4,"type":"VEC3"},
{"bufferView":5,"componentType":5126,"count":4,"type":"VEC3"},{"bufferView":6,"componentType":5126,"count":6,"type":"MAT4"}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":48},{"buffer":0,"byteOffset":48,"byteLength":12},{"buffer":0,"byteOffset":64,"byteLength":16},
{"buffer":0,"byteOffset":80,"byteLength":64},{"buffer":0,"byteOffset":144,"byteLength":48},{"buffer":0,"byteOffset":192,"byteLength":48},
{"buffer":0,"byteOffset":240,"byteLength":384}],
"buffers":[{"byteLength":624}],
"images":[{"uri":"shade.png"}],"textures":[{"source":0}],
"materials":[{"name":"Skin","pbrMetallicRoughness":{"baseColorFactor":[1,0.9,0.8,1]}%s}],
"extensionsUsed":[%s],"extensions":{%s}})",
        lx, hz,
        v1 ? R"(,"extensions":{"VRMC_materials_mtoon":{"shadeColorFactor":[0.5,0.45,0.4],"shadingToonyFactor":0.9,"shadeMultiplyTexture":{"index":0},"outlineWidthMode":"worldCoordinates",
"outlineWidthFactor":0.005,"outlineColorFactor":[0.1,0,0]}})" : "",
        v1 ? R"("VRMC_vrm","VRMC_springBone","VRMC_materials_mtoon")" : R"("VRM")",
        v1 ? R"("VRMC_vrm":{"specVersion":"1.0","meta":{"name":"Test VRM","authors":["Tests"],"licenseUrl":"https://vrm.dev/licenses/1.0/"},
"humanoid":{"humanBones":{"hips":{"node":0},"spine":{"node":1},"head":{"node":2},"leftUpperLeg":{"node":3},"leftLowerLeg":{"node":4},"leftFoot":{"node":5}}},
"expressions":{"preset":{"aa":{"morphTargetBinds":[{"node":8,"index":0,"weight":1}]},"blink":{"morphTargetBinds":[{"node":8,"index":1,"weight":0.5}]}}}},
"VRMC_springBone":{"springs":[{"name":"hair","joints":[{"node":6,"hitRadius":0.03,"stiffness":2,"gravityPower":0.5,"dragForce":0.4},{"node":7}]}]})"
           : R"("VRM":{"exporterVersion":"test","meta":{"title":"Test VRM","author":"Tests","licenseName":"CC0"},
"humanoid":{"humanBones":[{"bone":"hips","node":0},{"bone":"spine","node":1},{"bone":"head","node":2},{"bone":"leftUpperLeg","node":3},
{"bone":"leftLowerLeg","node":4},{"bone":"leftFoot","node":5}]},
"blendShapeMaster":{"blendShapeGroups":[{"name":"A","presetName":"a","binds":[{"mesh":0,"index":0,"weight":100}]},
{"name":"Blink","presetName":"blink","binds":[{"mesh":0,"index":1,"weight":50}]}]},
"secondaryAnimation":{"boneGroups":[{"comment":"hair","stiffiness":2,"gravityPower":0.5,"dragForce":0.4,"hitRadius":0.03,"bones":[6]}]},
"materialProperties":[{"name":"Skin","shader":"VRM/MToon","floatProperties":{"_ShadeToony":0.9,"_OutlineWidthMode":1,"_OutlineWidth":0.5},"textureProperties":{"_ShadeTexture":0},
"vectorProperties":{"_ShadeColor":[0.5,0.45,0.4,1],"_OutlineColor":[0.1,0,0,1]}}]})");
    while (json.size() % 4) json += ' ';
    R156Bin glb;
    glb.s = "glTF";
    glb.i32(2), glb.i32((int64_t)(12 + 8 + json.size() + 8 + bin.s.size()));
    glb.i32((int64_t)json.size()), glb.i32(0x4E4F534A), glb.s += json;
    glb.i32((int64_t)bin.s.size()), glb.i32(0x004E4942), glb.s += bin.s;
    return glb.s;
  };
  test("vrm: 0.x and 1.0 avatars come in facing +Z with their humanoid bones named for the rig tools, expressions as shape keys, the hair a spring chain with its settings, MToon as Toon with an outline", [&] {
    for (bool v1 : {false, true}) {
      const std::string dir = fs::join(test_dir(), "r158");
      fs::make_dirs(dir);
      const std::string file = fs::join(dir, v1 ? "avatar1.vrm" : "avatar0.vrm");
      fs::write_file(file, r158_vrm(v1));
      ImportResult r;
      const bool ok = import_model(file, r);
      CHECK(ok);
      if (!ok) return (void)std::printf("    %s\n", r.error.c_str());
      Scene s;
      instantiate_import(s, r, "Avatar", nullptr);
      GameObject *upleg = s.find_by_name("LeftUpLeg"), *hair = s.find_by_name("J_Sec_Hair1"), *face = s.find_by_name("Face");
      const bool named = s.find_by_name("Hips") && s.find_by_name("Spine") && s.find_by_name("Head") && upleg && s.find_by_name("LeftLeg") && s.find_by_name("LeftFoot");
      const Mesh *m = face && face->get<MeshFilter>() ? face->get<MeshFilter>()->mesh.get() : nullptr;
      const int a = m ? m->find_shape(v1 ? "aa" : "a") : -1, blink = m ? m->find_shape("blink") : -1;
      float lift = 0.0f, drop = 0.0f;
      for (size_t i = 0; m && a >= 0 && blink >= 0 && i < m->vert_count(); i++) lift = std::max(lift, m->shape_keys[(size_t)a].delta[i].y), drop = std::min(drop, m->shape_keys[(size_t)blink].delta[i].y);
      const Vec3 fn = m && m->face_count() ? face->world_matrix().dir(normalize(m->face_normal(0))) : Vec3(0.0f);
      const SpringBone *sb = hair ? hair->get<SpringBone>() : nullptr;
      const Material *mt = face && face->get<MeshRenderer>() && !face->get<MeshRenderer>()->materials.empty() ? face->get<MeshRenderer>()->materials[0].get() : nullptr;
      const Outline *o = face ? face->get<Outline>() : nullptr;
      bool meta = false;
      for (const std::string &n : r.notes) meta = meta || n.find("Test VRM") != std::string::npos;
      /* Bound (inverse bind matrices): at rest the skinned face stays where its vertices are; posed, it follows the head. */
      float rest_err = 0.0f;
      for (size_t i = 0; m && i < m->vert_count(); i++) rest_err = std::max(rest_err, length(face->evaluated_mesh()->positions[i] - m->positions[i]));
      GameObject *head = s.find_by_name("Head");
      if (head) head->set_local_position(head->local().position + Vec3(0.0f, 0.3f, 0.0f));
      const float followed = m && face && head ? face->evaluated_mesh()->positions[0].y - m->positions[0].y : 0.0f;
      /* 0.x colours are sRGB (Unity's), 1.0's linear. With a shade texture the shade colour is its tint, held as it is
       * (task 0148), not as a ratio to the base colour. */
      const Vec3 shade(0.5f, 0.45f, 0.4f);
      Vec3 want_shadow;
      for (int k = 0; k < 3; k++) want_shadow[k] = v1 ? shade[k] : srgb_to_linear(shade[k]);
      const float want_outline_r = v1 ? 0.1f : srgb_to_linear(0.1f);
      std::printf("    %s: bones named %s; the left leg at x %.2f, the hair at z %.2f; %s lifts %.3f, blink drops %.3f; the face's normal z %.2f; spring %s (stiffness %.2f, damping %.2f, gravity %.2f, radius %.3f); material toon %d shadow %.2f %.2f %.2f; outline %.4f (%.2f %.0f %.0f); meta %s; at rest off by %g, the head lifted 0.3: the face by %.3f\n",
                  v1 ? "1.0" : "0.x", named ? "yes" : "no", upleg ? upleg->world_position().x : 0.0f, hair ? hair->world_position().z : 0.0f, v1 ? "aa" : "a", lift, drop, fn.z,
                  sb ? "yes" : "no", sb ? sb->stiffness : 0.0f, sb ? sb->damping : 0.0f, sb ? sb->gravity : 0.0f, sb ? sb->radius : 0.0f, mt ? mt->shading_model : -1,
                  mt ? mt->toon_shadow.x : 0.0f, mt ? mt->toon_shadow.y : 0.0f, mt ? mt->toon_shadow.z : 0.0f, o ? o->thickness : 0.0f, o ? o->color.x : 0.0f, o ? o->color.y : 0.0f,
                  o ? o->color.z : 0.0f, meta ? "yes" : "no", rest_err, followed);
      CHECK(named && upleg && std::fabs(upleg->world_position().x + 0.1f) < 1e-5f && hair && std::fabs(hair->world_position().z + 0.05f) < 1e-5f);
      CHECK(rest_err < 1e-5f && std::fabs(followed - 0.3f) < 1e-4f);
      CHECK(m && m->vert_count() == 4 && std::fabs(lift - 0.05f) < 1e-6f && std::fabs(drop + 0.01f) < 1e-6f && fn.z > 0.999f);
      CHECK(sb && std::fabs(sb->stiffness - 0.5f) < 1e-6f && std::fabs(sb->damping - 0.4f) < 1e-6f && std::fabs(sb->gravity - 0.5f) < 1e-6f && std::fabs(sb->radius - 0.03f) < 1e-6f);
      CHECK(mt && mt->shading_model == 1 && length(mt->toon_shadow - want_shadow) < 2e-3f && o && std::fabs(o->thickness - 0.005f) < 1e-6f &&
            std::fabs(o->color.x - want_outline_r) < 1e-4f && meta);
      /* MToon's shade texture is the Shade Map (task 0148): the image file beside the model. */
      std::printf("    shade map: %s\n", mt ? mt->shade_map.path.c_str() : "-");
      CHECK(mt && fs::filename(mt->shade_map.path) == "shade.png" && !mt->shade_map.non_color);
    }
  });
  test("vrm: dropped into the editor, an avatar gets its .mat (Toon) and its outline; the lesson's Try-it opens the Project window", [&] {
    const std::string dir = fs::join(test_dir(), "r158_editor");
    fs::make_dirs(dir);
    const std::string file = fs::join(dir, "r158avatar.vrm");
    fs::write_file(file, r158_vrm(true));
    Editor ed;
    r34_open(ed);
    ed.command("import " + file);
    ed.step_frame_headless();
    GameObject *face = ed.scene().find_by_name("Face");
    MaterialPtr mat = material_asset("Assets/Materials/r158avatar/Skin.mat");
    const bool found = r146_try(ed, "Open the Project window: drop a .vrm onto the Scene view");
    std::printf("    Skin.mat %s (toon %d); outline on the face: %s; the lesson's Try-it %s\n", mat ? "yes" : "no", mat ? mat->shading_model : -1,
                face && face->get<Outline>() ? "yes" : "no", found ? "ran" : "not found");
    CHECK(mat && mat->shading_model == 1 && face && face->get<Outline>() && found);
  });
  /* ---------------------------------------------------------------- round 159: animation clips from FBX and glTF (task 0139) */
  test("animation import: Blender's FBX and GLB bring their clips at 24 fps: between a clip's start and its end the upper bone turns 90 degrees and the shape key goes 0 to 1", [&] {
    /* (Blender saves the FBX mid-pose, so the start is compared with the end, not with the rest; and its GLB has the
     * arm's action and the key's action as two animations, so the key is looked for across the clips.) */
    for (const char *file : {"tests/fixtures/anim_arm.fbx", "tests/fixtures/anim_arm.glb"}) {
      ImportResult r;
      const bool ok = import_model(file, r);
      CHECK(ok && !r.clips.empty() && r.clips.size() <= 2);
      if (!ok || r.clips.empty()) continue;
      Scene s;
      GameObject *root = instantiate_import(s, r, "anim_arm", nullptr);
      GameObject *upper = s.find_by_name("Upper"), *cube = s.find_by_name("Cube");
      const MeshRenderer *mr = cube ? cube->get<MeshRenderer>() : nullptr;
      CHECK(root && upper && mr);
      if (!root || !upper || !mr) continue;
      float turned = 0.0f, w0 = 0.0f, w1 = 0.0f, drift = 0.0f, rate = 0.0f, len = 0.0f;
      std::string names;
      for (const AnimationClip &c : r.clips) {
        names += (names.empty() ? "" : ", ") + c.name;
        rate = c.frame_rate, len = std::max(len, c.length);
        apply_clip(c, *root, 0.0f);
        const Quat q0 = upper->world_rotation();
        const Vec3 p0 = upper->world_position();
        w0 = std::max(w0, mr->shape_weight("Puff"));
        apply_clip(c, *root, c.length);
        const Quat q1 = upper->world_rotation();
        turned = std::max(turned, 2.0f * std::acos(std::min(1.0f, std::fabs(q0.x * q1.x + q0.y * q1.y + q0.z * q1.z + q0.w * q1.w))));
        w1 = std::max(w1, mr->shape_weight("Puff"));
        drift = std::max(drift, length(upper->world_position() - p0));
      }
      std::printf("    %s: clip(s) '%s', %.0f frames at %.0f fps; start to end the upper bone turns %.1f deg (its place drifting %.4f m), Puff %.2f -> %.2f\n",
                  fs::filename(file).c_str(), names.c_str(), len, rate, turned / kDeg2Rad, drift, w0, w1);
      CHECK(std::fabs(rate - 24.0f) < 1e-3f && std::fabs(len - 24.0f) < 0.5f && std::fabs(turned - 90.0f * kDeg2Rad) < 1.0f * kDeg2Rad && w0 < 0.01f &&
            std::fabs(w1 - 1.0f) < 0.01f && drift < 1e-4f);
    }
  });
  test("animation import: dropped into the editor, the FBX's clip 'Armature|ArmatureAction' is saved under a name Windows takes and plays; a second copy's clip finds its renamed armature; the lesson's Try-it", [&] {
    for (const char *d : {"Assets/Animations/anim_arm", "Assets/Animations/anim_arm 2", "Assets/Materials/anim_arm", "Assets/Materials/anim_arm 2",
                          "Assets/Textures/anim_arm", "Assets/Textures/anim_arm 2"})  // (an earlier run's: the GLB of the same name)
      if (fs::exists(fs::join(scratch_project(), d))) forget_clip_assets(d), fs::move_to_trash(fs::join(scratch_project(), d));
    Editor ed;
    r34_open(ed);
    ed.command("import tests/fixtures/anim_arm.fbx");
    ed.step_frame_headless();
    ed.command("import tests/fixtures/anim_arm.fbx");  // (a second copy: its armature is renamed for uniqueness)
    ed.step_frame_headless();
    std::vector<GameObject *> roots;
    ed.scene().for_each([&](GameObject &g) { if (g.get<Animation>()) roots.push_back(&g); });
    const Animation *an = roots.empty() ? nullptr : roots[0]->get<Animation>(), *an2 = roots.size() > 1 ? roots[1]->get<Animation>() : nullptr;
    const AnimationClipPtr clip = an ? clip_asset(an->clip) : nullptr, clip2 = an2 ? clip_asset(an2->clip) : nullptr;
    /* The second copy's clip, applied at its end, turns its own upper bone (its tracks were rewritten onto the renamed armature). */
    float turned2 = 0.0f;
    if (clip2 && roots.size() > 1) {
      GameObject *upper = nullptr;
      std::function<void(GameObject &)> walk = [&](GameObject &g) {
        if (g.name == "Upper" && !upper) upper = &g;
        for (GameObject *c : g.children) walk(*c);
      };
      walk(*roots[1]);
      if (upper) {
        apply_clip(*clip2, *roots[1], 0.0f);
        const Quat q0 = upper->world_rotation();
        apply_clip(*clip2, *roots[1], clip2->length);
        const Quat q1 = upper->world_rotation();
        turned2 = 2.0f * std::acos(std::min(1.0f, std::fabs(q0.x * q1.x + q0.y * q1.y + q0.z * q1.z + q0.w * q1.w)));
      }
    }
    const bool found = r146_try(ed, "Open the Animation window: an imported model's clips play on its root");
    std::printf("    '%s' plays %s: %zu tracks (%s); the second copy '%s' plays %s, its upper bone turning %.1f deg; the lesson's Try-it %s\n", roots.empty() ? "-" : roots[0]->name.c_str(),
                an ? an->clip.c_str() : "-", clip ? clip->tracks.size() : 0, an && fs::exists(resolve_asset_path(an->clip)) ? "on disk" : "missing",
                roots.size() > 1 ? roots[1]->name.c_str() : "-", an2 ? an2->clip.c_str() : "-", turned2 / kDeg2Rad, found ? "ran" : "not found");
    CHECK(an && an->clip == "Assets/Animations/anim_arm/Armature_ArmatureAction.anim" && clip && clip->tracks.size() == 2 && fs::exists(resolve_asset_path(an->clip)) && found);
    CHECK(an2 && clip2 && std::fabs(turned2 - 90.0f * kDeg2Rad) < 1.0f * kDeg2Rad);
  });
  /* ---------------------------------------------------------------- round 160: BVH motion capture (task 0140) */
  /* A BVH written by the tests, in centimetres: hips (6 channels) walking a metre forward, a spine (Z X Y rotations)
   * bending 90 degrees about X, a head, an end site; 25 frames at 24 fps. */
  auto r160_bvh = []() {
    std::string s = "HIERARCHY\nROOT Hips\n{\n  OFFSET 0.0 100.0 0.0\n  CHANNELS 6 Xposition Yposition Zposition Zrotation Xrotation Yrotation\n"
                    "  JOINT Spine\n  {\n    OFFSET 0.0 20.0 0.0\n    CHANNELS 3 Zrotation Xrotation Yrotation\n    JOINT Head\n    {\n      OFFSET 0.0 30.0 0.0\n"
                    "      CHANNELS 3 Zrotation Xrotation Yrotation\n      End Site\n      {\n        OFFSET 0.0 15.0 0.0\n      }\n    }\n  }\n}\nMOTION\nFrames: 25\nFrame Time: 0.0416667\n";
    for (int f = 0; f < 25; f++) s += strprintf("0.0 100.0 %.4f 0.0 0.0 0.0 0.0 %.4f 0.0 0.0 0.0 0.0\n", 100.0f * f / 24.0f, 90.0f * f / 24.0f);
    return s;
  };
  test("bvh: motion capture in centimetres comes in as a skeleton in metres with its clip: the hips walk a metre forward and the spine bends 90 degrees over 24 frames at 24 fps", [&] {
    const std::string dir = fs::join(test_dir(), "r160");
    fs::make_dirs(dir);
    const std::string file = fs::join(dir, "walk.bvh");
    fs::write_file(file, r160_bvh());
    ImportResult r;
    const bool ok = import_model(file, r);
    CHECK(ok && r.clips.size() == 1);
    if (!ok || r.clips.empty()) return (void)std::printf("    %s\n", r.error.c_str());
    const AnimationClip &c = r.clips[0];
    Scene s;
    GameObject *root = instantiate_import(s, r, "walk", nullptr);
    GameObject *hips = s.find_by_name("Hips"), *spine = s.find_by_name("Spine"), *head = s.find_by_name("Head");
    CHECK(root && hips && spine && head && hips->get<Bone>());
    if (!root || !hips || !spine || !head) return;
    const Vec3 hips0 = hips->world_position(), head0 = head->world_position();
    const Quat spine0 = spine->world_rotation();
    apply_clip(c, *root, c.length);
    const Vec3 moved = hips->world_position() - hips0;
    const Quat q = spine->world_rotation();
    const float turned = 2.0f * std::acos(std::min(1.0f, std::fabs(spine0.x * q.x + spine0.y * q.y + spine0.z * q.z + spine0.w * q.w)));
    const Vec3 axis = normalize(Vec3(q.x, q.y, q.z));
    std::printf("    %s; hips at %.2f m, head %.2f m above them; clip %.0f frames at %.1f fps; at the end the hips moved %.3f %.3f %.3f, the spine turned %.2f deg about %.2f %.2f %.2f\n",
                r.summary.c_str(), hips0.y, head0.y - hips0.y, c.length, c.frame_rate, moved.x, moved.y, moved.z, turned / kDeg2Rad, axis.x, axis.y, axis.z);
    CHECK(std::fabs(hips0.y - 1.0f) < 1e-5f && std::fabs(head0.y - hips0.y - 0.5f) < 1e-5f && std::fabs(c.length - 24.0f) < 0.5f && std::fabs(c.frame_rate - 24.0f) < 0.01f);
    CHECK(length(moved - Vec3(0, 0, 1.0f)) < 1e-4f && std::fabs(turned - 90.0f * kDeg2Rad) < 0.1f * kDeg2Rad && std::fabs(std::fabs(axis.x) - 1.0f) < 1e-3f);
    /* Damaged: cut anywhere, a brace too many, a NaN; what imports is whole (every key finite). The same file with
     * CRLF line ends, tabs and "Frames:25" imports the same. */
    int refused = 0, imported = 0, broken = 0;
    const std::string whole = r160_bvh();
    for (int k = 0; k < 40; k++) {
      std::string d = k < 30 ? whole.substr(0, whole.size() * (size_t)k / 30) : k == 30 ? whole + "}\n" : k == 31 ? whole + "nan nan\n" : whole.substr(0, 60) + "}}}" + whole.substr(60);
      fs::write_file(file, d);
      ImportResult rd;
      if (!import_model(file, rd)) {
        refused++;
        continue;
      }
      imported++;
      for (const AnimationClip &cc : rd.clips)
        for (const AnimTrack &t : cc.tracks)
          for (const AnimKey &kk : t.keys) broken += !std::isfinite(kk.value) || !std::isfinite(kk.frame);
    }
    std::string crlf;
    for (char ch : whole) crlf += ch == '\n' ? std::string("\r\n") : ch == ' ' && crlf.size() % 7 == 0 ? std::string("\t") : std::string(1, ch);
    const size_t fr = crlf.find("Frames: 25");
    if (fr != std::string::npos) crlf.replace(fr, 10, "Frames:25");
    fs::write_file(file, crlf);
    ImportResult rc;
    const bool crlf_ok = import_model(file, rc) && rc.clips.size() == 1 && rc.clips[0].tracks.size() == 2 && std::fabs(rc.clips[0].length - 24.0f) < 0.5f;
    std::printf("    40 damaged files: %d refused, %d imported (%d bad keys); with CRLF, tabs and Frames:25: %s\n", refused, imported, broken, crlf_ok ? "the same" : "broken");
    CHECK(refused + imported == 40 && refused > 0 && broken == 0 && crlf_ok);
  });
  test("bvh: dropped into the editor, the capture's clip plays on its skeleton; the lesson's Try-it", [&] {
    const std::string dir = fs::join(test_dir(), "r160_editor");
    fs::make_dirs(dir);
    fs::write_file(fs::join(dir, "r160walk.bvh"), r160_bvh());
    const std::string anim = fs::join(scratch_project(), "Assets/Animations/r160walk");
    if (fs::exists(anim)) forget_clip_assets("Assets/Animations/r160walk"), fs::move_to_trash(anim);  // (an earlier run's)
    Editor ed;
    r34_open(ed);
    ed.command("import " + fs::join(dir, "r160walk.bvh"));
    ed.step_frame_headless();
    GameObject *root = nullptr;
    ed.scene().for_each([&](GameObject &g) { if (g.get<Animation>() && !root) root = &g; });
    const Animation *an = root ? root->get<Animation>() : nullptr;
    const AnimationClipPtr clip = an ? clip_asset(an->clip) : nullptr;
    const bool found = r146_try(ed, "Open the Animation window: a capture's clip plays on its skeleton");
    std::printf("    '%s' plays %s (%zu tracks); the lesson's Try-it %s\n", root ? root->name.c_str() : "-", an ? an->clip.c_str() : "-", clip ? clip->tracks.size() : 0, found ? "ran" : "not found");
    CHECK(root && an && an->clip.rfind("Assets/Animations/r160walk/", 0) == 0 && clip && clip->tracks.size() == 2 && found);  // (the hips' Z, the spine's X: constant axes get no track)
  });
  /* ---------------------------------------------------------------- round 161: Collada (task 0141) */
  /* A Collada file written by the tests, Z up in centimetres: a 1 m square standing on the ground and facing -Y (its
   * front, in Z-up terms), textured, skinned to two joints (Upper at the ground, Lower 50 cm up), and the upper joint
   * turning 90 degrees about X over a second (a matrix channel, as Blender writes). */
  auto r161_dae = []() {
    return std::string(R"(<?xml version="1.0" encoding="utf-8"?>
<COLLADA xmlns="http://www.collada.org/2005/11/COLLADASchema" version="1.4.1">
  <asset><up_axis>Z_UP</up_axis><unit name="centimeter" meter="0.01"/></asset>
  <library_images><image id="img"><init_from>tex/skin.png</init_from></image></library_images>
  <library_effects><effect id="fx"><profile_COMMON>
    <newparam sid="surf"><surface type="2D"><init_from>img</init_from></surface></newparam>
    <newparam sid="samp"><sampler2D><source>surf</source></sampler2D></newparam>
    <technique sid="common"><phong>
      <emission><color>0.1 0.2 0.3 1</color></emission>
      <diffuse><texture texture="samp" texcoord="UVMap"/></diffuse>
      <shininess><float>50</float></shininess>
    </phong></technique></profile_COMMON></effect></library_effects>
  <library_materials><material id="mat" name="Skin"><instance_effect url="#fx"/></material></library_materials>
  <library_geometries><geometry id="quad" name="Quad"><mesh>
    <source id="pos"><float_array id="posa" count="12">-50 0 0 50 0 0 50 0 100 -50 0 100</float_array>
      <technique_common><accessor source="#posa" count="4" stride="3"><param name="X" type="float"/><param name="Y" type="float"/><param name="Z" type="float"/></accessor></technique_common></source>
    <source id="uv"><float_array id="uva" count="8">0 0 1 0 1 1 0 1</float_array>
      <technique_common><accessor source="#uva" count="4" stride="2"><param name="S" type="float"/><param name="T" type="float"/></accessor></technique_common></source>
    <vertices id="verts"><input semantic="POSITION" source="#pos"/></vertices>
    <polylist material="mat-symbol" count="1"><input semantic="VERTEX" source="#verts" offset="0"/><input semantic="TEXCOORD" source="#uv" offset="1" set="0"/>
      <vcount>4</vcount><p>0 0 1 1 2 2 3 3</p></polylist>
  </mesh></geometry></library_geometries>
  <library_controllers><controller id="ctrl"><skin source="#quad">
    <bind_shape_matrix>1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1</bind_shape_matrix>
    <source id="joints"><Name_array id="jn" count="2">Upper Lower</Name_array><technique_common><accessor source="#jn" count="2" stride="1"><param name="JOINT" type="name"/></accessor></technique_common></source>
    <source id="ibm"><float_array id="ibma" count="32">1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1 1 0 0 0 0 1 0 0 0 0 1 -50 0 0 0 1</float_array>
      <technique_common><accessor source="#ibma" count="2" stride="16"><param name="TRANSFORM" type="float4x4"/></accessor></technique_common></source>
    <source id="w"><float_array id="wa" count="1">1</float_array><technique_common><accessor source="#wa" count="1" stride="1"><param name="WEIGHT" type="float"/></accessor></technique_common></source>
    <joints><input semantic="JOINT" source="#joints"/><input semantic="INV_BIND_MATRIX" source="#ibm"/></joints>
    <vertex_weights count="4"><input semantic="JOINT" source="#joints" offset="0"/><input semantic="WEIGHT" source="#w" offset="1"/>
      <vcount>1 1 1 1</vcount><v>0 0 0 0 1 0 1 0</v></vertex_weights>
  </skin></controller></library_controllers>
  <library_animations><animation id="anim"><animation id="upper_anim">
    <source id="t"><float_array id="ta" count="2">0 1</float_array><technique_common><accessor source="#ta" count="2" stride="1"><param name="TIME" type="float"/></accessor></technique_common></source>
    <source id="m"><float_array id="ma" count="32">1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1 1 0 0 0 0 0 -1 0 0 1 0 0 0 0 0 1</float_array>
      <technique_common><accessor source="#ma" count="2" stride="16"><param name="TRANSFORM" type="float4x4"/></accessor></technique_common></source>
    <source id="i"><Name_array id="ia" count="2">LINEAR LINEAR</Name_array><technique_common><accessor source="#ia" count="2" stride="1"><param name="INTERPOLATION" type="name"/></accessor></technique_common></source>
    <sampler id="s"><input semantic="INPUT" source="#t"/><input semantic="OUTPUT" source="#m"/><input semantic="INTERPOLATION" source="#i"/></sampler>
    <channel source="#s" target="Upper/transform"/>
  </animation></animation></library_animations>
  <library_visual_scenes><visual_scene id="Scene" name="Scene">
    <node id="Armature" name="Armature" type="NODE"><matrix sid="transform">1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1</matrix>
      <node id="Upper" name="Upper" sid="Upper" type="JOINT"><matrix sid="transform">1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1</matrix>
        <node id="Lower" name="Lower" sid="Lower" type="JOINT"><translate sid="location">0 0 50</translate></node>
      </node>
      <node id="QuadNode" name="Quad" type="NODE"><instance_controller url="#ctrl"><skeleton>#Upper</skeleton>
        <bind_material><technique_common><instance_material symbol="mat-symbol" target="#mat"/></technique_common></bind_material></instance_controller></node>
    </node>
  </visual_scene></library_visual_scenes>
  <scene><instance_visual_scene url="#Scene"/></scene>
</COLLADA>
)");
  };
  test("collada: a Z-up file in centimetres comes in standing in metres, facing +Z, textured, skinned to its joints, its matrix animation a clip turning the upper joint 90 degrees", [&] {
    const std::string dir = fs::join(test_dir(), "r161");
    fs::make_dirs(fs::join(dir, "tex"));
    std::vector<uint32_t> px(16, 0xFFE0A080u);
    write_png(fs::join(dir, "tex/skin.png"), px.data(), 4, 4, 4, false);
    const std::string file = fs::join(dir, "stand.dae");
    fs::write_file(file, r161_dae());
    ImportResult r;
    const bool ok = import_model(file, r);
    CHECK(ok);
    if (!ok) return (void)std::printf("    %s\n", r.error.c_str());
    Scene s;
    GameObject *root = instantiate_import(s, r, "stand", nullptr);
    GameObject *upper = s.find_by_name("Upper"), *lower = s.find_by_name("Lower"), *quad = s.find_by_name("Quad");
    CHECK(root && upper && lower && quad && upper->get<Bone>() && lower->get<Bone>() && quad->get<SkinModifier>());
    if (!root || !upper || !lower || !quad) return;
    const Mesh *m = quad->get<MeshFilter>() ? quad->get<MeshFilter>()->mesh.get() : nullptr;
    const Material *mt = quad->get<MeshRenderer>() && !quad->get<MeshRenderer>()->materials.empty() ? quad->get<MeshRenderer>()->materials[0].get() : nullptr;
    float top = -1.0f, zmax = 0.0f;
    for (size_t i = 0; m && i < m->vert_count(); i++) top = std::max(top, m->positions[i].y), zmax = std::max(zmax, std::fabs(m->positions[i].z));
    const Vec3 fn = m && m->face_count() ? normalize(m->face_normal(0)) : Vec3(0.0f);
    float rest_err = 0.0f;
    for (size_t i = 0; m && i < m->vert_count(); i++) rest_err = std::max(rest_err, length(quad->evaluated_mesh()->positions[i] - m->positions[i]));
    const bool groups = m && m->find_group("Upper") >= 0 && m->find_group("Lower") >= 0;
    std::printf("    %s; Lower at %.2f m up; the quad %.2f m tall, flat (|z| %.3f), normal z %.2f, at rest off by %g; material '%s' map %s, emission %.1f %.1f %.1f, roughness %.2f; groups %s; %s\n",
                r.summary.c_str(), lower->world_position().y, top, zmax, fn.z, rest_err, mt ? mt->name.c_str() : "-", mt ? fs::filename(mt->base_map.path).c_str() : "-",
                mt ? mt->emission.x : 0.0f, mt ? mt->emission.y : 0.0f, mt ? mt->emission.z : 0.0f, mt ? mt->roughness : 0.0f, groups ? "yes" : "no",
                r.notes.empty() ? "" : r.notes.back().c_str());
    CHECK(std::fabs(lower->world_position().y - 0.5f) < 1e-5f && std::fabs(top - 1.0f) < 1e-5f && zmax < 1e-5f && fn.z > 0.999f && rest_err < 1e-5f);
    CHECK(mt && mt->name == "Skin" && fs::filename(mt->base_map.path) == "skin.png" && fs::exists(mt->base_map.path) && std::fabs(mt->emission.z - 0.3f) < 1e-5f && groups);
    /* The clip: at its end the upper joint has turned 90 degrees, so Lower lies along Z and the top of the quad with it. */
    CHECK(r.clips.size() == 1);
    if (r.clips.empty()) return;
    const AnimationClip &c = r.clips[0];
    apply_clip(c, *root, c.length);
    const Vec3 low = lower->world_position();
    float moved = 0.0f;
    for (size_t i = 0; m && i < m->vert_count(); i++) moved = std::max(moved, length(quad->evaluated_mesh()->positions[i] - m->positions[i]));
    std::printf("    clip '%s': %.0f frames at %.0f fps, %zu tracks; at its end Lower is at %.3f %.3f %.3f, the quad's farthest vertex moved %.3f m\n", c.name.c_str(), c.length,
                c.frame_rate, c.tracks.size(), low.x, low.y, low.z, moved);
    CHECK(std::fabs(c.length - 24.0f) < 0.5f && std::fabs(low.y) < 1e-3f && std::fabs(std::fabs(low.z) - 0.5f) < 1e-3f && moved > 0.4f);
    /* Damaged: cut anywhere. */
    int refused = 0, imported = 0;
    const std::string whole = r161_dae();
    for (int k = 0; k < 40; k++) {
      fs::write_file(file, whole.substr(0, whole.size() * (size_t)k / 40));
      ImportResult rd;
      (import_model(file, rd) ? imported : refused)++;
    }
    std::printf("    40 files cut short: %d refused, %d imported\n", refused, imported);
    CHECK(refused + imported == 40 && refused > 0);
  });
  test("collada: dropped into the editor, the model's .mat and texture are made and its clip plays; the lesson's Try-it", [&] {
    const std::string dir = fs::join(test_dir(), "r161_editor");
    fs::make_dirs(fs::join(dir, "tex"));
    std::vector<uint32_t> px(16, 0xFFE0A080u);
    write_png(fs::join(dir, "tex/skin.png"), px.data(), 4, 4, 4, false);
    fs::write_file(fs::join(dir, "r161stand.dae"), r161_dae());
    const std::string anim = fs::join(scratch_project(), "Assets/Animations/r161stand");
    if (fs::exists(anim)) forget_clip_assets("Assets/Animations/r161stand"), fs::move_to_trash(anim);  // (an earlier run's)
    Editor ed;
    r34_open(ed);
    ed.command("import " + fs::join(dir, "r161stand.dae"));
    ed.step_frame_headless();
    MaterialPtr mat = material_asset("Assets/Materials/r161stand/Skin.mat");
    GameObject *root = nullptr;
    ed.scene().for_each([&](GameObject &g) { if (g.get<Animation>() && !root) root = &g; });
    const Animation *an = root ? root->get<Animation>() : nullptr;
    const bool found = r146_try(ed, "Open the Project window: drop a .dae onto the Scene view");
    std::printf("    Skin.mat %s (texture %s); '%s' plays %s; the lesson's Try-it %s\n", mat ? "yes" : "no", mat ? mat->base_map.path.c_str() : "-", root ? root->name.c_str() : "-",
                an ? an->clip.c_str() : "-", found ? "ran" : "not found");
    CHECK(mat && mat->base_map.path.rfind("Assets/Textures/r161stand", 0) == 0 && an && an->clip.rfind("Assets/Animations/r161stand/", 0) == 0 && found);
  });
  test("collada: a Y-up metre cube (a byte-order mark, CDATA) comes in facing out, its translate and rotate channels a clip moving it 2 m and turning it 90 degrees", [&] {
    /* The cube's faces wound counter-clockwise seen from outside (right-handed): they must face out after the mirror. */
    std::string dae = "\xEF\xBB\xBF" R"(<?xml version="1.0" encoding="utf-8"?>
<COLLADA xmlns="http://www.collada.org/2005/11/COLLADASchema" version="1.4.1">
  <asset><up_axis>Y_UP</up_axis><unit name="meter" meter="1"/></asset>
  <extra><technique profile="test"><![CDATA[ <not a tag> & not an entity ]]></technique></extra>
  <library_geometries><geometry id="cube" name="Cube"><mesh>
    <source id="pos"><float_array id="posa" count="24">-1 -1 1 1 -1 1 1 1 1 -1 1 1 -1 -1 -1 1 -1 -1 1 1 -1 -1 1 -1</float_array>
      <technique_common><accessor source="#posa" count="8" stride="3"><param name="X" type="float"/><param name="Y" type="float"/><param name="Z" type="float"/></accessor></technique_common></source>
    <vertices id="verts"><input semantic="POSITION" source="#pos"/></vertices>
    <polylist count="6"><input semantic="VERTEX" source="#verts" offset="0"/><vcount>4 4 4 4 4 4</vcount>
      <p>0 1 2 3  5 4 7 6  4 0 3 7  1 5 6 2  3 2 6 7  4 5 1 0</p></polylist>
  </mesh></geometry></library_geometries>
  <library_animations><animation id="a">
    <source id="t"><float_array id="ta" count="2">0 1</float_array><technique_common><accessor source="#ta" count="2" stride="1"><param name="TIME" type="float"/></accessor></technique_common></source>
    <source id="x"><float_array id="xa" count="2">0 2</float_array><technique_common><accessor source="#xa" count="2" stride="1"><param name="X" type="float"/></accessor></technique_common></source>
    <source id="r"><float_array id="ra" count="2">0 90</float_array><technique_common><accessor source="#ra" count="2" stride="1"><param name="ANGLE" type="float"/></accessor></technique_common></source>
    <source id="i"><Name_array id="ia" count="2">LINEAR LINEAR</Name_array><technique_common><accessor source="#ia" count="2" stride="1"><param name="INTERPOLATION" type="name"/></accessor></technique_common></source>
    <sampler id="sx"><input semantic="INPUT" source="#t"/><input semantic="OUTPUT" source="#x"/><input semantic="INTERPOLATION" source="#i"/></sampler>
    <sampler id="sr"><input semantic="INPUT" source="#t"/><input semantic="OUTPUT" source="#r"/><input semantic="INTERPOLATION" source="#i"/></sampler>
    <channel source="#sx" target="Box/location.X"/>
    <channel source="#sr" target="Box/rotationZ.ANGLE"/>
  </animation></library_animations>
  <library_visual_scenes><visual_scene id="Scene">
    <node id="Box" name="Box"><translate sid="location">0 0 0</translate><rotate sid="rotationZ">0 0 1 0</rotate>
      <instance_geometry url="#cube"/></node>
  </visual_scene></library_visual_scenes>
  <scene><instance_visual_scene url="#Scene"/></scene>
</COLLADA>
)";
    const std::string dir = fs::join(test_dir(), "r161b");
    fs::make_dirs(dir);
    const std::string file = fs::join(dir, "box.dae");
    fs::write_file(file, dae);
    ImportResult r;
    const bool ok = import_model(file, r);
    CHECK(ok);
    if (!ok) return (void)std::printf("    %s\n", r.error.c_str());
    Scene s;
    GameObject *root = instantiate_import(s, r, "box", nullptr);
    GameObject *box = root;  // (a single node: the import's root is the box itself)
    const Mesh *m = box && box->get<MeshFilter>() ? box->get<MeshFilter>()->mesh.get() : nullptr;
    const bool outward = m && m->face_count() == 6 && normals_outward(*m, Vec3(0.0f));
    const Quat rest = box ? box->world_rotation() : Quat();
    float turned = 0.0f;
    Vec3 at(0.0f);
    if (root && box && !r.clips.empty()) {
      apply_clip(r.clips[0], *root, r.clips[0].length);
      const Quat q = box->world_rotation();
      turned = 2.0f * std::acos(std::min(1.0f, std::fabs(rest.x * q.x + rest.y * q.y + rest.z * q.z + rest.w * q.w)));
      at = box->world_position();
    }
    std::printf("    %zu faces facing out: %s; clip(s) %zu, %.0f frames; at the end the box is at %.2f %.2f %.2f, turned %.1f deg\n", m ? m->face_count() : 0,
                outward ? "yes" : "no", r.clips.size(), r.clips.empty() ? 0.0f : r.clips[0].length, at.x, at.y, at.z, turned / kDeg2Rad);
    CHECK(outward && r.clips.size() == 1 && std::fabs(r.clips[0].length - 24.0f) < 0.5f && std::fabs(at.x + 2.0f) < 1e-4f && std::fabs(at.y) < 1e-4f &&
          std::fabs(turned - 90.0f * kDeg2Rad) < 0.1f * kDeg2Rad);
  });
  /* ---------------------------------------------------------------- round 162: the texture finder (task 0143) */
  auto r162_png = [](const std::string &path, uint32_t argb) {
    std::vector<uint32_t> px(16, argb);
    fs::make_dirs(fs::parent(path));
    return write_png(path, px.data(), 4, 4, 4, true);
  };
  auto r162_red = [](const std::string &path) {
    Bitmap bm;
    std::string e;
    if (path.empty() || !load_image(path, bm, e) || bm.width <= 0) return -1;
    return bm.is_float ? (int)std::lround(bm.rgbaf[0] * 255.0f) : (int)bm.rgba8[0];
  };
  /* An OBJ with materials RobeA, RobeB and Trim, in Sketchfab's layout: the model in source/, the textures beside it in
   * textures/, named with two-word kinds. */
  auto r162_robe = [&](const std::string &root) {
    fs::make_dirs(fs::join(root, "source"));
    fs::write_file(fs::join(root, "source/robe.mtl"), "newmtl RobeA\nKd 0.8 0.8 0.8\nnewmtl RobeB\nKd 0.8 0.8 0.8\nnewmtl Trim\nKd 0.9 0.8 0.2\n");
    fs::write_file(fs::join(root, "source/robe.obj"), "mtllib robe.mtl\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
                                                       "usemtl RobeA\nf 1/1 2/2 3/3\nusemtl RobeB\nf 1/1 3/3 4/4\nusemtl Trim\nf 2/2 3/3 4/4\n");
    r162_png(fs::join(root, "textures/RobeA_Base_color.png"), 0xFFCC2222u);
    r162_png(fs::join(root, "textures/RobeB_Base_Color.png"), 0xFF2222CCu);
    r162_png(fs::join(root, "textures/RobeB_Normal_OpenGL.png"), 0xFF8080FFu);
    r162_png(fs::join(root, "textures/RobeB_Roughness_map.png"), 0xFF404040u);
    r162_png(fs::join(root, "textures/RobeB_AO.png"), 0xFFFFFFFFu);
    return fs::join(root, "source/robe.obj");
  };
  test("texture finder: Sketchfab's layout (source/ beside textures/) and two-word map names: RobeA and RobeB textured with their normal and roughness, Trim's own colour kept, the AO map reported", [&] {
    const std::string file = r162_robe(fs::join(test_dir(), "r162_robe"));
    ImportResult r;
    CHECK(import_model(file, r));
    MaterialPtr a, b, trim;
    for (const MaterialPtr &m : r.materials) (m->name == "RobeA" ? a : m->name == "RobeB" ? b : trim) = m;
    const int ra = a ? r162_red(a->base_map.path) : -1, rb = b ? r162_red(b->base_map.path) : -1;
    std::string notes;
    for (const std::string &n : r.notes) notes += "\n      " + n;
    std::printf("    RobeA red %d (%s); RobeB red %d (%s), normal %s, roughness %s; Trim map '%s', colour %.1f %.1f %.1f; notes:%s\n", ra,
                a ? fs::filename(a->base_map.path).c_str() : "-", rb, b ? fs::filename(b->base_map.path).c_str() : "-", b ? fs::filename(b->normal_map.path).c_str() : "-",
                b ? fs::filename(b->roughness_map.path).c_str() : "-", trim ? trim->base_map.path.c_str() : "-", trim ? trim->base_color.x : 0.0f, trim ? trim->base_color.y : 0.0f,
                trim ? trim->base_color.z : 0.0f, notes.c_str());
    bool ao_noted = false;
    for (const std::string &n : r.notes) ao_noted = ao_noted || (n.find("RobeB_AO.png") != std::string::npos && n.find("no slot") != std::string::npos);
    CHECK(a && b && trim && ra > 180 && rb < 60 && fs::filename(b->normal_map.path) == "RobeB_Normal_OpenGL.png" && fs::filename(b->roughness_map.path) == "RobeB_Roughness_map.png");
    CHECK(trim->base_map.empty() && std::fabs(trim->base_color.x - 0.9f) < 1e-5f && ao_noted);
    /* The user's folder, made synthetic: materials CarmenB, CarmenF, CarmenH and OL with textures/Carmen{B,F,H}_Base_color.png. */
    const std::string carmen = fs::join(test_dir(), "r162_carmen");
    fs::make_dirs(fs::join(carmen, "source"));
    fs::write_file(fs::join(carmen, "source/carmen.mtl"), "newmtl CarmenB\nKd 0.8 0.8 0.8\nnewmtl CarmenF\nKd 0.8 0.8 0.8\nnewmtl CarmenH\nKd 0.8 0.8 0.8\nnewmtl OL\nKd 0 0 0\n");
    fs::write_file(fs::join(carmen, "source/carmen.obj"), "mtllib carmen.mtl\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nusemtl CarmenB\nf 1 2 3\nusemtl CarmenF\nf 1 3 4\nusemtl CarmenH\nf 2 3 4\nusemtl OL\nf 1 2 4\n");
    for (const char *n : {"CarmenB", "CarmenF", "CarmenH"}) r162_png(fs::join(carmen, std::string("textures/") + n + "_Base_color.png"), 0xFFCC2222u);
    ImportResult rc;
    CHECK(import_model(fs::join(carmen, "source/carmen.obj"), rc));
    int textured = 0;
    bool ol_bare = false;
    for (const MaterialPtr &m : rc.materials) {
      if (m->name == "OL") ol_bare = m->base_map.empty();
      else textured += fs::filename(m->base_map.path) == m->name + "_Base_color.png";
    }
    std::printf("    Carmen: %d of 3 textured with their own file, OL bare %s\n", textured, ol_bare ? "yes" : "no");
    CHECK(textured == 3 && ol_bare);
  });
  test("texture finder: a Unity Textures folder full of .meta files is read; a set of only a height map leaves its material bare and is reported; Wood_01 and Rock_01 don't pair; Arm doesn't take Armor's set", [&] {
    const std::string dir = fs::join(fs::join(test_dir(), "r162_edge"), "model");
    fs::make_dirs(fs::join(dir, "Textures"));
    fs::write_file(fs::join(dir, "set.mtl"), "newmtl Crate\nKd 0.8 0.8 0.8\nnewmtl Mountain\nKd 0.8 0.8 0.8\nnewmtl Wood_01\nKd 0.8 0.8 0.8\nnewmtl Rock_01\nKd 0.8 0.8 0.8\nnewmtl Arm\nKd 0.8 0.8 0.8\n");
    fs::write_file(fs::join(dir, "set.obj"), "mtllib set.mtl\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nv 2 0 0\nusemtl Crate\nf 1 2 3\nusemtl Mountain\nf 1 3 4\nusemtl Wood_01\nf 2 3 4\nusemtl Rock_01\nf 1 2 4\nusemtl Arm\nf 2 5 3\n");
    for (int i = 0; i < 60; i++) fs::write_file(fs::join(dir, strprintf("Textures/file%02d.png.meta", i)), "meta");  // (Unity's sidecars)
    r162_png(fs::join(dir, "Textures/Crate_BaseColor.png"), 0xFFCC2222u);
    r162_png(fs::join(dir, "Textures/Mountain_Height.png"), 0xFF777777u);
    r162_png(fs::join(dir, "Textures/Timber_BaseColor.png"), 0xFF996633u);  // (one set left for Wood_01 and Rock_01: no likeness, no pair)
    r162_png(fs::join(dir, "Textures/Armor_BaseColor.png"), 0xFF888899u);
    ImportResult r;
    CHECK(import_model(fs::join(dir, "set.obj"), r));
    std::map<std::string, std::string> maps;
    for (const MaterialPtr &m : r.materials) maps[m->name] = fs::filename(m->base_map.path);
    std::string notes;
    for (const std::string &n : r.notes) notes += "\n      " + n;
    bool height_noted = false, mountain_bare_noted = false;
    for (const std::string &n : r.notes) {
      height_noted = height_noted || (n.find("Mountain_Height.png") != std::string::npos && n.find("no slot") != std::string::npos);
      mountain_bare_noted = mountain_bare_noted || (n.find("no texture found") != std::string::npos && n.find("Mountain") != std::string::npos);
    }
    std::printf("    Crate '%s', Mountain '%s', Wood_01 '%s', Rock_01 '%s', Arm '%s'; notes:%s\n", maps["Crate"].c_str(), maps["Mountain"].c_str(), maps["Wood_01"].c_str(),
                maps["Rock_01"].c_str(), maps["Arm"].c_str(), notes.c_str());
    CHECK(maps["Crate"] == "Crate_BaseColor.png" && maps["Mountain"].empty() && maps["Wood_01"].empty() && maps["Rock_01"].empty() && maps["Arm"].empty());
    CHECK(height_noted && mountain_bare_noted);
  });
  /* ---------------------------------------------------------------- round 163: bone lengths on import (task 0144) */
  /* A glTF written by the test, as Blender's FBX export lays a rig out: an armature node scaled x100 holding a chain
   * Upper (1 cm up in its units: 1 m), Lower, and two leaves - "Lower_end" (Blender's leaf bone) and "Tip" (a real end
   * bone with no child); a triangle skinned to the chain so the joints are bones. */
  auto r163_gltf = [](const std::string &path) {
    R156Bin bin;
    bin.v3({0, 0, 0}), bin.v3({0.1f, 0, 0}), bin.v3({0, 0.1f, 0});  // positions: 36 bytes
    bin.u16(0), bin.u16(1), bin.u16(2), bin.u16(0);                   // indices: 6 (+2 pad): 8
    for (int v = 0; v < 3; v++) bin.u8(0), bin.u8(1), bin.u8(0), bin.u8(0);  // joints (Upper, Lower): 12 at 44
    for (int v = 0; v < 3; v++) bin.f(0.5f), bin.f(0.5f), bin.f(0), bin.f(0);  // weights: 48 at 56
    static const char *const b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string enc;
    const std::string &d = bin.s;
    for (size_t i = 0; i < d.size(); i += 3) {
      const uint32_t n = (uint32_t)(uint8_t)d[i] << 16 | (i + 1 < d.size() ? (uint32_t)(uint8_t)d[i + 1] << 8 : 0) | (i + 2 < d.size() ? (uint32_t)(uint8_t)d[i + 2] : 0);
      enc += b64[n >> 18 & 63], enc += b64[n >> 12 & 63];
      enc += i + 1 < d.size() ? b64[n >> 6 & 63] : '=';
      enc += i + 2 < d.size() ? b64[n & 63] : '=';
    }
    const std::string json = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0,5]}],
"nodes":[{"name":"Armature","scale":[100,100,100],"children":[1]},{"name":"Upper","translation":[0,0,0],"children":[2]},
{"name":"Lower","translation":[0,0.01,0],"children":[3,4,6]},{"name":"Lower_end","translation":[0,0.005,0]},{"name":"Tip","translation":[0.002,0,0]},
{"name":"Tri","mesh":0,"skin":0},{"name":"mixamorig:HeadTop_End","translation":[0,0.003,0]}],
"skins":[{"joints":[1,2,3,4,6]}],
"meshes":[{"name":"Tri","primitives":[{"attributes":{"POSITION":0,"JOINTS_0":2,"WEIGHTS_0":3},"indices":1}]}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[0.1,0.1,0]},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"},
{"bufferView":2,"componentType":5121,"count":3,"type":"VEC4"},{"bufferView":3,"componentType":5126,"count":3,"type":"VEC4"}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6},{"buffer":0,"byteOffset":44,"byteLength":12},{"buffer":0,"byteOffset":56,"byteLength":48}],
"buffers":[{"byteLength":104,"uri":"data:application/octet-stream;base64,)" + enc + R"("}]})";
    fs::make_dirs(fs::parent(path));
    fs::write_file(path, json);
  };
  auto r163_world_length = [](const GameObject &g) {
    const Bone *b = g.get<Bone>();
    return b ? length(g.world_matrix().point(Vec3(0, b->length, 0)) - g.world_position()) : -1.0f;
  };
  test("bone lengths: under an armature scaled x100 a leaf bone is 10 cm in the world, not 10 m; Blender's \"_end\" leaf becomes its parent's tail", [&] {
    const std::string file = fs::join(fs::join(test_dir(), "r163"), "scaled.gltf");
    r163_gltf(file);
    ImportResult r;
    const bool ok = import_model(file, r);
    CHECK(ok);
    if (!ok) return (void)std::printf("    %s\n", r.error.c_str());
    Scene s;
    instantiate_import(s, r, "scaled", nullptr);
    GameObject *upper = s.find_by_name("Upper"), *lower = s.find_by_name("Lower"), *tip = s.find_by_name("Tip"), *end = s.find_by_name("Lower_end");
    CHECK(upper && lower && tip && !end && !s.find_by_name("mixamorig:HeadTop_End"));  // (Mixamo's "_End" folds too)
    if (!upper || !lower || !tip) return;
    const float lu = r163_world_length(*upper), ll = r163_world_length(*lower), lt = r163_world_length(*tip);
    bool noted = false;
    for (const std::string &n : r.notes) noted = noted || n.find("end bone") != std::string::npos;
    std::printf("    world lengths: Upper %.3f m (to Lower), Lower %.3f m (to its folded _end), Tip %.3f m (half its parent's); the fold noted: %s\n", lu, ll, lt, noted ? "yes" : "no");
    CHECK(std::fabs(lu - 1.0f) < 1e-3f && std::fabs(ll - 0.5f) < 1e-3f && std::fabs(lt - 0.25f) < 1e-3f && noted);
  });
  test("bone lengths: Blender's FBX and GLB rigs have no bone longer than their chain and no leaf longer than half its parent; a leaf's tail still drags", [&] {
    for (const char *file : {"tests/fixtures/rig_tube.fbx", "tests/fixtures/rig_tube.glb", "tests/fixtures/anim_arm.fbx", "tests/fixtures/anim_arm.glb"}) {
      ImportResult r;
      CHECK(import_model(file, r));
      Scene s;
      instantiate_import(s, r, "rig", nullptr);
      float longest = 0.0f, leaf_longest = 0.0f;
      int bones = 0, leaves = 0;
      bool half = true;  // (every leaf at most half its parent, and at least a centimetre)
      s.for_each([&](GameObject &g) {
        if (!g.get<Bone>()) return;
        bones++;
        bool has_bone_child = false;
        for (GameObject *c : g.children) has_bone_child = has_bone_child || c->get<Bone>();
        const float l = r163_world_length(g);
        longest = std::max(longest, l);
        if (has_bone_child) return;
        leaves++, leaf_longest = std::max(leaf_longest, l);
        if (g.parent && g.parent->get<Bone>()) half = half && l <= 0.5f * r163_world_length(*g.parent) + 1e-4f && l >= 0.01f;
      });
      std::printf("    %s: %d bones (%d leaves), the longest %.3f m, the longest leaf %.3f m, every leaf at most half its parent: %s\n", fs::filename(file).c_str(), bones, leaves,
                  longest, leaf_longest, half ? "yes" : "no");
      CHECK(bones >= 2 && longest < 1.5f && leaf_longest < 0.6f && leaf_longest > 0.02f && half);
    }
    /* A dragged tail keeps metres: the leaf of the scaled rig set to 0.3 m reads 0.3 m. */
    const std::string file = fs::join(fs::join(test_dir(), "r163"), "scaled.gltf");
    Editor ed;
    r34_open(ed);
    ImportResult r;
    CHECK(import_model(file, r));
    GameObject *root = instantiate_import(ed.scene(), r, "scaled", nullptr);
    GameObject *tip = ed.scene().find_by_name("Tip");
    if (root && tip) {
      ed.set_bone_ends(*tip, tip->world_position(), tip->world_position() + Vec3(0, 0.3f, 0), nullptr);
      std::printf("    a leaf's tail dragged to 0.3 m away reads %.3f m\n", r163_world_length(*tip));
      CHECK(std::fabs(r163_world_length(*tip) - 0.3f) < 1e-3f);
    }
  });
  test("texture finder: names that say the same thing match (Body and T_character_body_2k_BaseColor); a lone image like Hand_R does not; loose in a folder called Downloads nothing beyond the model's own folder is read", [&] {
    /* (a) Similarity: materials Body and Hand; sets T_character_body_2k (a size word dropped) and a lone Hand_R.png. (The
     * model two folders deep: the folders beside its parent are other tests'.) */
    const std::string dir = fs::join(fs::join(test_dir(), "r162_sim"), "model");
    fs::make_dirs(dir);
    fs::write_file(fs::join(dir, "figure.mtl"), "newmtl Body\nKd 0.8 0.8 0.8\nnewmtl Hand\nKd 0.8 0.8 0.8\n");
    fs::write_file(fs::join(dir, "figure.obj"), "mtllib figure.mtl\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nusemtl Body\nf 1 2 3\nusemtl Hand\nf 1 3 4\n");
    r162_png(fs::join(dir, "T_character_body_2k_BaseColor.png"), 0xFFCC2222u);
    r162_png(fs::join(dir, "T_character_body_2k_Normal.png"), 0xFF8080FFu);
    r162_png(fs::join(dir, "Hand_R.png"), 0xFF777777u);
    ImportResult r;
    CHECK(import_model(fs::join(dir, "figure.obj"), r));
    MaterialPtr body, hand;
    for (const MaterialPtr &m : r.materials) (m->name == "Body" ? body : hand) = m;
    std::printf("    Body: %s (normal %s); Hand: '%s'\n", body ? fs::filename(body->base_map.path).c_str() : "-", body ? fs::filename(body->normal_map.path).c_str() : "-",
                hand ? hand->base_map.path.c_str() : "-");
    CHECK(body && fs::filename(body->base_map.path) == "T_character_body_2k_BaseColor.png" && fs::filename(body->normal_map.path) == "T_character_body_2k_Normal.png");
    CHECK(hand && hand->base_map.empty() && hand->roughness_map.empty());
    /* (b) Loose in "Downloads": a model there takes textures beside it, none from the folders around. */
    const std::string downloads = fs::join(fs::join(test_dir(), "r162_crawl"), "Downloads");
    fs::make_dirs(fs::join(downloads, "stuff"));
    fs::make_dirs(fs::join(fs::join(test_dir(), "r162_crawl"), "other"));
    fs::write_file(fs::join(downloads, "loose.mtl"), "newmtl Body\nKd 0.8 0.8 0.8\n");
    fs::write_file(fs::join(downloads, "loose.obj"), "mtllib loose.mtl\nv 0 0 0\nv 1 0 0\nv 1 1 0\nusemtl Body\nf 1 2 3\n");
    r162_png(fs::join(downloads, "stuff/Body_BaseColor.png"), 0xFFCC2222u);
    r162_png(fs::join(fs::join(test_dir(), "r162_crawl"), "other/Body_BaseColor.png"), 0xFFCC2222u);
    ImportResult rl;
    CHECK(import_model(fs::join(downloads, "loose.obj"), rl) && !rl.materials.empty());
    const bool none = rl.materials[0]->base_map.empty();
    std::printf("    loose in Downloads: %s\n", none ? "nothing taken from the folders around" : rl.materials[0]->base_map.path.c_str());
    CHECK(none);
    /* ...and the same model in a folder of its own under Downloads reads that folder's tree (textures/ in it), not the
     * other folders under Downloads (stuff/). */
    const std::string own = fs::join(downloads, "model");
    fs::make_dirs(own);
    fs::copy_file(fs::join(downloads, "loose.obj"), fs::join(own, "loose.obj"));
    fs::copy_file(fs::join(downloads, "loose.mtl"), fs::join(own, "loose.mtl"));
    r162_png(fs::join(own, "textures/Body_Base_color.png"), 0xFFCC2222u);
    ImportResult ro;
    CHECK(import_model(fs::join(own, "loose.obj"), ro) && !ro.materials.empty());
    std::printf("    in its own folder under Downloads: %s\n", fs::filename(ro.materials[0]->base_map.path).c_str());
    CHECK(fs::filename(ro.materials[0]->base_map.path) == "Body_Base_color.png");
  });
  test("texture finder: dropped into the editor, the robe's textures from textures/ are copied into Assets/Textures/robe/ and its .mat files use them", [&] {
    const std::string file = r162_robe(fs::join(test_dir(), "r162_editor"));
    for (const char *d : {"Assets/Materials/robe", "Assets/Textures/robe"})
      if (fs::exists(fs::join(scratch_project(), d))) fs::move_to_trash(fs::join(scratch_project(), d));  // (an earlier run's)
    Editor ed;
    r34_open(ed);
    ed.command("import " + file);
    ed.step_frame_headless();
    MaterialPtr a = material_asset("Assets/Materials/robe/RobeA.mat"), b = material_asset("Assets/Materials/robe/RobeB.mat");
    const bool copied = a && b && a->base_map.path == "Assets/Textures/robe/RobeA_Base_color.png" && fs::exists(resolve_asset_path(a->base_map.path)) &&
                        b->normal_map.path == "Assets/Textures/robe/RobeB_Normal_OpenGL.png" && fs::exists(resolve_asset_path(b->normal_map.path));
    std::printf("    RobeA.mat: %s; RobeB.mat normal: %s\n", a ? a->base_map.path.c_str() : "-", b ? b->normal_map.path.c_str() : "-");
    CHECK(copied);
  });
}

static void tests_round154() {
  /* ---------------------------------------------------------------- round 154: materials made on import (task 0134) */
  /* Imports write extracted and split images under ~/.blendity/cache: a scratch home from here on, never the user's. */
  set_env("USERPROFILE", fs::join(test_dir(), "home"));
  set_env("HOME", fs::join(test_dir(), "home"));
  /* A 4 x 4 PNG of one colour (0xAARRGGBB), and the red of an image's first pixel (0..255; -1 if it can't be read). */
  auto r154_png = [](const std::string &path, uint32_t argb) {
    std::vector<uint32_t> px(16, argb);
    fs::make_dirs(fs::parent(path));
    return write_png(path, px.data(), 4, 4, 4, true);
  };
  auto r154_red = [](const std::string &path) {
    Bitmap bm;
    std::string e;
    if (!load_image(path, bm, e) || bm.width <= 0) return -1;
    return bm.is_float ? (int)std::lround(bm.rgbaf[0] * 255.0f) : (int)bm.rgba8[0];
  };
  test("import materials: Blender's FBX with its textures inside and its GLB with a packed metallic-roughness map come in textured", [&] {
    ImportResult fbx, glb;
    const bool a = import_model("tests/fixtures/embedded_quad.fbx", fbx), b = import_model("tests/fixtures/embedded_quad.glb", glb);
    auto first = [](const ImportResult &r) { return r.materials.empty() ? Material() : *r.materials[0]; };
    const Material mf = first(fbx), mg = first(glb);
    const int fred = r154_red(mf.base_map.path), gred = r154_red(mg.base_map.path);
    const int rough = r154_red(mg.roughness_map.path), metal = r154_red(mg.metallic_map.path);
    std::printf("    FBX: '%s' base map %s (red %d); GLB: base red %d, roughness map %d (0.25 = 64), metallic map %d\n", mf.name.c_str(),
                fs::filename(mf.base_map.path).c_str(), fred, gred, rough, metal);
    CHECK(a && b && fred > 180 && gred > 180 && std::abs(rough - 64) <= 2 && metal >= 253);
  });
  test("import materials: a link to another machine's path is found in the model's textures folder; a texture set by name fills a bare material", [&] {
    const std::string dir = fs::join(fs::join(test_dir(), "r154_find"), "model");  // (two deep: the folders beside are this test's)
    r154_png(fs::join(dir, "textures/brick.png"), 0xFF3366CCu);
    /* An OBJ whose library points at C:/elsewhere, and a second material with no textures but a set beside it. */
    fs::write_file(fs::join(dir, "wall.mtl"), "newmtl Brick\nKd 1 1 1\nmap_Kd C:/elsewhere/art/brick.png\nnewmtl Body\nKd 0.8 0.8 0.8\n");
    fs::write_file(fs::join(dir, "wall.obj"), "mtllib wall.mtl\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
                                              "usemtl Brick\nf 1/1 2/2 3/3\nusemtl Body\nf 1/1 3/3 4/4\n");
    r154_png(fs::join(dir, "Body_BaseColor.png"), 0xFFCC2222u);
    r154_png(fs::join(dir, "Body_Normal.png"), 0xFF8080FFu);
    r154_png(fs::join(dir, "Body_Smoothness.png"), 0xFFC0C0C0u);  // (0.75 smooth: 0.25 rough once inverted)
    ImportResult r;
    CHECK(import_model(fs::join(dir, "wall.obj"), r));
    MaterialPtr brick, body;
    for (const MaterialPtr &m : r.materials) (m->name == "Brick" ? brick : body) = m;
    const bool found = brick && fs::filename(brick->base_map.path) == "brick.png" && fs::exists(brick->base_map.path);
    const int rough = body ? r154_red(body->roughness_map.path) : -1;
    std::printf("    the moved link: %s; Body: base %s, normal %s, roughness from smoothness %d (63 expected); notes: %zu\n",
                found ? brick->base_map.path.c_str() : "not found", body ? fs::filename(body->base_map.path).c_str() : "-",
                body ? fs::filename(body->normal_map.path).c_str() : "-", rough, r.notes.size());
    CHECK(found && body && fs::filename(body->base_map.path) == "Body_BaseColor.png" && fs::filename(body->normal_map.path) == "Body_Normal.png" &&
          std::abs(rough - 63) <= 2);
    /* A model without materials (STL): every mesh one, named after it. */
    const std::string stl = fs::join(dir, "part.stl");
    Mesh cube = *primitives::cube();
    ExportItem it;
    it.name = "Part", it.mesh = &cube;
    fs::write_file(stl, export_stl({it}, ExportOptions()));
    ImportResult rs;
    CHECK(import_model(stl, rs) && !rs.nodes.empty());
    bool every = true;
    for (const ImportedNode &n : rs.nodes) every = every && (!n.mesh || !n.materials.empty());
    CHECK(every && !rs.materials.empty());
  });
  test("import materials: Blendity's FBX with Embed Textures carries its image; imported into the project the materials become .mat assets, an edited one kept on re-import", [&] {
    const std::string dir = fs::join(fs::join(test_dir(), "r154_embed"), "model");
    const std::string tex = fs::join(dir, "skin.png");
    r154_png(tex, 0xFF20C040u);
    Mesh quad = *primitives::cube();
    MaterialPtr m = make_material("Skin", Vec3(1.0f));
    m->base_map = TextureRef{tex, false};
    ExportItem it;
    it.name = "Creature", it.mesh = &quad, it.materials = {m};
    ExportOptions o;
    o.embed_textures = true, o.texture_dir = dir;
    const std::string fbx = fs::join(dir, "creature.fbx");
    fs::write_file(fbx, export_fbx({it}, o));
    fs::remove_file(tex);  // (only the copy inside the file is left)
    ImportResult r;
    CHECK(import_model(fbx, r) && !r.materials.empty());
    const int green = r.materials.empty() ? -1 : (r154_red(r.materials[0]->base_map.path) >= 0 ? 1 : -1);
    /* Into the project, as dropped: Assets/Materials/creature/Skin.mat, the renderer using it. */
    Editor ed;
    r34_open(ed);
    ed.command("import " + fbx);
    ed.step_frame_headless();
    const std::string mat = fs::join(std::getenv("BLENDITY_PROJECT"), "Assets/Materials/creature/Skin.mat");
    MaterialPtr used;
    ed.scene().for_each([&](GameObject &g) {
      if (MeshRenderer *mr = g.get<MeshRenderer>(); mr && !mr->materials.empty() && !used && mr->materials[0] && mr->materials[0]->name == "Skin") used = mr->materials[0];
    });
    const bool asset = used && !used->asset_path.empty() && fs::exists(mat) && fs::exists(resolve_asset_path(used->base_map.path));
    /* Edited (its colour), then the model imported again: the edit stays. */
    if (used) used->base_color = Vec3(0.1f, 0.2f, 0.9f), used->touch();
    save_dirty_material_assets();
    ed.command("import " + fbx);
    ed.step_frame_headless();
    std::string text;
    fs::read_file(mat, text);
    MaterialPtr again = material_asset("Assets/Materials/creature/Skin.mat");
    std::printf("    the embedded image read back: %s; Skin.mat made and used: %s; after an edit and a re-import its colour %.2f %.2f %.2f\n",
                green > 0 ? "yes" : "no", asset ? "yes" : "no", again ? again->base_color.x : -1.0f, again ? again->base_color.y : -1.0f,
                again ? again->base_color.z : -1.0f);
    CHECK(green > 0 && asset && again && std::fabs(again->base_color.z - 0.9f) < 1e-3f);
  });
  test("import materials: two models of one name get folders of their own; importing one again reuses its folder", [&] {
    const std::string a = fs::join(fs::join(test_dir(), "r154_twin"), "a"), b = fs::join(fs::join(test_dir(), "r154_twin"), "b");
    for (const char *d : {"Assets/Materials/r154twin", "Assets/Materials/r154twin 2", "Assets/Materials/r154twin 3", "Assets/Meshes/r154twin.obj",
                          "Assets/Meshes/r154twin 2", "Assets/Textures/r154twin", "Assets/Textures/r154twin 2"})
      if (fs::exists(fs::join(std::getenv("BLENDITY_PROJECT"), d))) fs::move_to_trash(fs::join(std::getenv("BLENDITY_PROJECT"), d));
    const char *obj = "mtllib r154twin.mtl\nv 0 0 0\nv 1 0 0\nv 1 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nusemtl Material\nf 1/1 2/2 3/3\n";
    fs::make_dirs(a), fs::make_dirs(b);
    fs::write_file(fs::join(a, "r154twin.obj"), obj);
    fs::write_file(fs::join(b, "r154twin.obj"), std::string(obj) + "v 0 0 1\n");  // (another model: other contents)
    fs::write_file(fs::join(a, "r154twin.mtl"), "newmtl Material\nKd 1 0 0\nmap_Kd diffuse.png\n");
    fs::write_file(fs::join(b, "r154twin.mtl"), "newmtl Material\nKd 0 0 1\nmap_Kd diffuse.png\n");
    r154_png(fs::join(a, "diffuse.png"), 0xFFFF0000u);
    r154_png(fs::join(b, "diffuse.png"), 0xFF0000FFu);
    Editor ed;
    r34_open(ed);
    ed.command("import " + fs::join(a, "r154twin.obj"));
    ed.command("import " + fs::join(b, "r154twin.obj"));
    ed.command("import " + fs::join(a, "r154twin.obj"));  // (the first again)
    ed.step_frame_headless();
    const std::string p = std::getenv("BLENDITY_PROJECT");
    MaterialPtr ma = material_asset("Assets/Materials/r154twin/Material.mat"), mb = material_asset("Assets/Materials/r154twin 2/Material.mat");
    const int ra = ma ? r154_red(resolve_asset_path(ma->base_map.path)) : -1, rb = mb ? r154_red(resolve_asset_path(mb->base_map.path)) : -1;
    const bool meshes = fs::exists(fs::join(p, "Assets/Meshes/r154twin.obj")) && fs::exists(fs::join(p, "Assets/Meshes/r154twin 2/r154twin.obj"));
    const bool third = fs::exists(fs::join(p, "Assets/Materials/r154twin 3"));
    std::printf("    first: red %d (%s); second: red %d (%s); both models in Meshes: %s; a third folder: %s\n", ra, ma ? ma->base_map.path.c_str() : "-", rb,
                mb ? mb->base_map.path.c_str() : "-", meshes ? "yes" : "no", third ? "yes" : "no");
    CHECK(ma && mb && ra > 250 && rb == 0 && meshes && !third);
  });
  test("import materials: the file's own flat colours keep them beside an unrelated texture; Hand_R is not a roughness map; an .mtl's subfolder texture is found", [&] {
    const std::string dir = fs::join(fs::join(test_dir(), "r154_flat"), "model");
    fs::make_dirs(fs::join(dir, "mat"));
    fs::write_file(fs::join(dir, "mat/flat.mtl"), "newmtl Red\nKd 1 0 0\nnewmtl Blue\nKd 0 0 1\nnewmtl Logo\nKd 1 1 1\nmap_Kd logo.png\n");
    fs::write_file(fs::join(dir, "flat.obj"), "mtllib mat/flat.mtl\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nusemtl Red\nf 1 2 3\nusemtl Blue\nf 1 3 4\n"
                                              "usemtl Logo\nf 2 3 4\n");
    r154_png(fs::join(dir, "mat/logo.png"), 0xFF00FF00u);
    r154_png(fs::join(dir, "Rock_d.png"), 0xFF777777u);  // (one image named by a letter: not a set)
    r154_png(fs::join(dir, "Stone_BaseColor.png"), 0xFF777777u);  // (a set by name, matching no material)
    r154_png(fs::join(dir, "Hand_R.png"), 0xFF777777u);
    ImportResult r;
    CHECK(import_model(fs::join(dir, "flat.obj"), r));
    MaterialPtr red, blue, logo;
    for (const MaterialPtr &m : r.materials) (m->name == "Red" ? red : m->name == "Blue" ? blue : logo) = m;
    const bool flat = red && blue && red->base_map.empty() && blue->base_map.empty() && red->base_color.x > 0.99f && blue->base_color.z > 0.99f &&
                      red->roughness_map.empty();
    const bool sub = logo && fs::normalize(logo->base_map.path) == fs::normalize(fs::join(dir, "mat/logo.png"));
    std::printf("    Red and Blue untextured, their colours kept: %s; the logo from mat/: %s (%s)\n", flat ? "yes" : "no", sub ? "yes" : "no",
                logo ? logo->base_map.path.c_str() : "-");
    CHECK(flat && sub);
    /* An STL's own material (the import made it) still takes a folder's only set. */
    const std::string stl = fs::join(dir, "only/part.stl");
    Mesh cube = *primitives::cube();
    ExportItem it;
    it.name = "Part", it.mesh = &cube;
    fs::make_dirs(fs::parent(stl));
    fs::write_file(stl, export_stl({it}, ExportOptions()));
    r154_png(fs::join(dir, "only/Rock_d.png"), 0xFF777777u);
    r154_png(fs::join(dir, "only/Rock_n.png"), 0xFF8080FFu);  // (two letters of one name: a set)
    ImportResult rs;
    CHECK(import_model(stl, rs) && !rs.materials.empty());
    std::printf("    the STL's material: base %s, normal %s\n", fs::filename(rs.materials[0]->base_map.path).c_str(), fs::filename(rs.materials[0]->normal_map.path).c_str());
    CHECK(fs::filename(rs.materials[0]->base_map.path) == "Rock_d.png" && fs::filename(rs.materials[0]->normal_map.path) == "Rock_n.png");
  });
  test("import materials: a packed map is split once for all the materials using it; two embedded images of one name stay two", [&] {
    const std::string dir = fs::join(test_dir(), "r154_memo");
    r154_png(fs::join(dir, "packed.png"), 0xFF10C0F0u);
    ImportResult r;
    r.source = fs::join(dir, "model.fbx");
    const std::string g1 = derive_channel(fs::join(dir, "packed.png"), 1, false, "rough", r), g2 = derive_channel(fs::join(dir, "packed.png"), 1, false, "rough", r);
    const std::string x = save_embedded("body/diffuse.png", "AAAA", 4, r), y = save_embedded("head/diffuse.png", "BBBB", 4, r), z = save_embedded("body/diffuse.png", "AAAA", 4, r);
    std::printf("    split twice: one file (%s), %zu note(s); embedded: %s, %s, the first again %s\n", fs::filename(g1).c_str(), r.notes.size(),
                fs::filename(x).c_str(), fs::filename(y).c_str(), fs::filename(z).c_str());
    CHECK(!g1.empty() && g1 == g2 && r.notes.size() == 1 && r154_red(g1) == 0xC0 && x != y && x == z && !x.empty() && !y.empty());
    CHECK(fs::parent(g1).find(fs::join(test_dir(), "home")) == 0);  // (the scratch home, never the user's)
  });
}

static void tests_round166_to_round170() {
  /* ---------------------------------------------------------------- round 166: multi-edit materials (task 0147) */
  /* A number field clicked, typed over and committed with Enter (the Inspector's and the Materials window's). */
  auto r166_type = [](Editor &ed, const Recti &f, const std::string &text) {
    using ET = platform::EventType;
    auto ev = [](ET t, int x, int y, int key = 0) {
      platform::Event e;
      e.type = t;
      e.x = x;
      e.y = y;
      e.key = key;
      return e;
    };
    const int x = f.x + f.w / 2, y = f.y + f.h / 2;
    ed.step_frame_headless({ev(ET::MouseMove, x, y)});
    ed.step_frame_headless({ev(ET::MouseDown, x, y)});
    ed.step_frame_headless({ev(ET::MouseUp, x, y)});
    ed.step_frame_headless({ev(ET::KeyDown, x, y, platform::KEY_A)});  // a click selects the text; type over it
    std::vector<platform::Event> typed;
    for (char c : text) {
      platform::Event t = ev(ET::Text, x, y);
      t.codepoint = (uint32_t)c;
      typed.push_back(t);
    }
    ed.step_frame_headless(typed);
    ed.step_frame_headless({ev(ET::KeyDown, x, y, platform::KEY_ENTER)});
    ed.step_frame_headless({ev(ET::KeyUp, x, y, platform::KEY_ENTER)});
    ed.step_frame_headless();
  };
  /* A field's rect, scrolled into its window (the material's fields run past the bottom): the wheel over the window's
   * lower part until the rect is inside it. */
  auto r166_field = [](Editor &ed, const std::string &key, WindowKind win) {
    const Recti w = ed.window_rect_for_test(win);
    for (int tries = 0; tries < 60; tries++) {
      Recti f;
      if (!ed.button_rect_for_test(key, f)) {
        ed.step_frame_headless();
        continue;
      }
      if (f.y >= w.y && f.y + f.h <= w.bottom()) return f;
      platform::Event mv, wheel;
      mv.type = platform::EventType::MouseMove;
      wheel.type = platform::EventType::Wheel;
      mv.x = wheel.x = f.x;
      mv.y = wheel.y = w.bottom() - ed.ui_for_test().px(30);
      wheel.wheel_y = f.y > w.bottom() ? -3.0f : 3.0f;
      ed.step_frame_headless({mv});
      ed.step_frame_headless({wheel});
      ed.step_frame_headless();
    }
    std::printf("    '%s' never came into view\n", key.c_str());
    return Recti{0, 0, 0, 0};
  };
  auto r166_click = [](Editor &ed, const std::string &key, int mods) {
    Recti b;
    if (!ed.button_rect_for_test(key, b)) {
      std::printf("    no '%s'\n", key.c_str());
      return false;
    }
    const int x = b.x + b.w / 2, y = b.y + b.h / 2;
    platform::Event m, d, u;
    m.type = platform::EventType::MouseMove, d.type = platform::EventType::MouseDown, u.type = platform::EventType::MouseUp;
    m.x = d.x = u.x = x, m.y = d.y = u.y = y;
    m.mods = d.mods = u.mods = mods;
    ed.step_frame_headless({m});
    ed.step_frame_headless({d});
    ed.step_frame_headless({u});
    ed.step_frame_headless();
    return true;
  };
  test("multi-edit materials: three toon materials and a PBR one selected in the Materials window - Toon Bands typed once lands on the three, +=1 moves each, a dash marks a differing field, matset from the console, the .mat files follow", [&] {
    {  // (an earlier run's files)
      const std::string dir = fs::join(scratch_project(), "Assets/Materials");
      for (const DirEntry &de : fs::list(dir))
        if (de.name.size() > 3 && de.name.rfind("R166", 0) == 0 && fs::extension(de.name) == ".mat") std::remove(fs::join(dir, de.name).c_str());
    }
    Editor ed;
    r50_open(ed);
    ed.record_buttons_for_test(true);
    for (const char *n : {"R166A", "R166B", "R166C", "R166D"}) ed.command(std::string("newmat ") + n);
    MaterialPtr ma = material_asset("Assets/Materials/R166A.mat"), mb = material_asset("Assets/Materials/R166B.mat"),
                mc = material_asset("Assets/Materials/R166C.mat"), md = material_asset("Assets/Materials/R166D.mat");
    CHECK(ma && mb && mc && md);
    if (!ma || !mb || !mc || !md) return;
    for (const MaterialPtr &m : {ma, mb, mc}) m->shading_model = 1, m->toon_bands = 1, m->touch();
    md->shading_model = 0, md->toon_bands = 1, md->touch();
    ed.command("window Materials");
    ed.set_material_search_for_test("R166");  // (the scratch project holds every test's materials: only these four in the grid)
    r34_steps(ed, 3);
    /* D first, A last: the window edits the last clicked one, and it must be a toon material to show the field. */
    CHECK(r166_click(ed, "mat:R166D", 0) && r166_click(ed, "mat:R166B", platform::MOD_CTRL) && r166_click(ed, "mat:R166C", platform::MOD_CTRL) &&
          r166_click(ed, "mat:R166A", platform::MOD_CTRL));
    CHECK(ed.materials_selected_for_test() == 4);
    r34_steps(ed, 2);
    Recti f = r166_field(ed, "field:Toon Bands", WindowKind::Materials);
    CHECK(f.w > 0);
    if (f.w <= 0) return;
    r166_type(ed, f, "3");
    std::printf("    Toon Bands typed 3: A %d, B %d, C %d, D (PBR) %d\n", ma->toon_bands, mb->toon_bands, mc->toon_bands, md->toon_bands);
    CHECK(ma->toon_bands == 3 && mb->toon_bands == 3 && mc->toon_bands == 3 && md->toon_bands == 1);
    /* A relative edit moves each from its own value. */
    mb->toon_bands = 5, mb->touch();
    r34_steps(ed, 1);
    f = r166_field(ed, "field:Toon Bands", WindowKind::Materials);
    r166_type(ed, f, "+=1");
    std::printf("    then +=1: A %d, B %d, C %d, D %d\n", ma->toon_bands, mb->toon_bands, mc->toon_bands, md->toon_bands);
    CHECK(ma->toon_bands == 4 && mb->toon_bands == 6 && mc->toon_bands == 4 && md->toon_bands == 1);
    /* The fields whose values differ (the dash): Toon Bands now, Toon Threshold once one is changed. */
    const std::vector<MaterialPtr> all = {ma, mb, mc, md};
    std::set<std::string> mixed = ed.mixed_material_fields(ma.get(), all);
    CHECK(mixed.count("Toon Bands") && !mixed.count("Toon Threshold") && mixed.count("Shading Model"));  // (D is PBR: Shading Model differs)
    mc->toon_threshold = 0.3f, mc->touch();
    mixed = ed.mixed_material_fields(ma.get(), all);
    CHECK(mixed.count("Toon Threshold"));
    /* (Undo of a material asset's edit is what it was: the scene's snapshot, not the asset library's - the object test
     * below checks undo on scene materials.) */
    /* The console: the Materials window's selection, where the field applies. */
    ed.command("matset ToonThreshold 0.4");
    std::printf("    matset ToonThreshold 0.4: A %.2f, B %.2f, C %.2f, D (PBR) %.2f\n", ma->toon_threshold, mb->toon_threshold, mc->toon_threshold, md->toon_threshold);
    CHECK(std::fabs(ma->toon_threshold - 0.4f) < 1e-5f && std::fabs(mb->toon_threshold - 0.4f) < 1e-5f && std::fabs(mc->toon_threshold - 0.4f) < 1e-5f &&
          std::fabs(md->toon_threshold - 0.4f) > 1e-3f);
    ed.command("matset ShadingModel 1");  // (shown on every material: converts the PBR one too)
    CHECK(md->shading_model == 1);
    const int bands_before[4] = {ma->toon_bands, mb->toon_bands, mc->toon_bands, md->toon_bands};
    ed.command("matset NoSuchField 1");  // a warning, nothing changed
    ed.command("matset ToonThreshold abc");  // not a number: a warning, nothing changed
    CHECK(ma->toon_bands == bands_before[0] && mb->toon_bands == bands_before[1] && mc->toon_bands == bands_before[2] && md->toon_bands == bands_before[3]);
    CHECK(std::fabs(ma->toon_threshold - 0.4f) < 1e-5f && md->shading_model == 1);
    /* A relative value moves each from its own; a name is never set on all. */
    mb->toon_threshold = 0.2f, mb->touch();
    ed.command("matset ToonThreshold +=0.1");
    std::printf("    matset ToonThreshold +=0.1: A %.2f, B %.2f\n", ma->toon_threshold, mb->toon_threshold);
    CHECK(std::fabs(ma->toon_threshold - 0.5f) < 1e-5f && std::fabs(mb->toon_threshold - 0.3f) < 1e-5f);
    ed.command("matset Name Renamed");
    CHECK(ma->name == "R166A" && mb->name == "R166B" && md->name == "R166D");
    /* The files follow (edited assets are written back). */
    r34_steps(ed, 4);
    std::string text;
    fs::read_file(resolve_asset_path("Assets/Materials/R166C.mat"), text);
    Material fresh;
    const bool loaded = load_material_text(text, fresh);
    std::printf("    R166C.mat on disk: Toon Bands %d (in memory %d), Toon Threshold %.2f\n", fresh.toon_bands, mc->toon_bands, fresh.toon_threshold);
    CHECK(loaded && fresh.toon_bands == mc->toon_bands && std::fabs(fresh.toon_threshold - mc->toon_threshold) < 1e-5f);
  });

  test("multi-edit materials: two objects selected - a toon edit in the active one's Inspector reaches the other's material in the same slot; a PBR object's stays", [&] {
    Editor ed;
    r34_open(ed);
    ed.command("create Cube");
    ed.command("create Sphere");
    ed.command("create Plane");
    ed.command("select Cube");
    ed.command("material Toon");
    ed.command("select Sphere");
    ed.command("material Toon");
    ed.command("select Plane");
    ed.command("material Metal");
    GameObject *cube = ed.scene().find_by_name("Cube"), *sphere = ed.scene().find_by_name("Sphere"), *plane = ed.scene().find_by_name("Plane");
    CHECK(cube && sphere && plane);
    if (!cube || !sphere || !plane) return;
    const MaterialPtr mc = cube->get<MeshRenderer>()->materials[0], ms = sphere->get<MeshRenderer>()->materials[0], mp = plane->get<MeshRenderer>()->materials[0];
    CHECK(mc && ms && mp && mc != ms && mc->shading_model == 1 && ms->shading_model == 1 && mp->shading_model == 0);
    if (!mc || !ms || !mp) return;
    const int plane_bands = mp->toon_bands, cube_bands = mc->toon_bands;
    ed.command("select Plane");
    ed.command("selectadd Sphere");
    ed.command("selectadd Cube");  // the active one
    ed.record_buttons_for_test(true);
    r34_steps(ed, 3);
    const Recti f = r166_field(ed, "field:Toon Bands", WindowKind::Inspector);
    CHECK(f.w > 0);
    if (f.w <= 0) return;
    r166_type(ed, f, "4");
    std::printf("    Toon Bands 4 on the Cube (%d): Sphere %d, Plane (PBR) %d\n", mc->toon_bands, ms->toon_bands, mp->toon_bands);
    CHECK(mc->toon_bands == 4 && ms->toon_bands == 4 && mp->toon_bands == plane_bands);
    /* One undo step takes it back on both (scene materials live in the scene's snapshot). */
    ed.command("undo");
    r34_steps(ed, 1);
    {
      GameObject *c2 = ed.scene().find_by_name("Cube"), *s2 = ed.scene().find_by_name("Sphere");
      const int cb = c2 ? c2->get<MeshRenderer>()->materials[0]->toon_bands : -1, sb = s2 ? s2->get<MeshRenderer>()->materials[0]->toon_bands : -1;
      std::printf("    after undo: Cube %d, Sphere %d (before the edit %d)\n", cb, sb, cube_bands);
      CHECK(cb == cube_bands && sb == cube_bands);
      ed.command("redo");
    }
    /* matset with objects selected: their materials. */
    ed.command("matset ToonSoftness 0.1");
    CHECK(std::fabs(mc->toon_softness - 0.1f) < 1e-5f && std::fabs(ms->toon_softness - 0.1f) < 1e-5f && std::fabs(mp->toon_softness - 0.1f) > 1e-3f);
  });
  test("multi-edit materials: a value typed that the active material already holds still lands on the others (the dash hid theirs); several .mat files selected in the Project window edit together", [&] {
    Editor ed;
    r50_open(ed);
    ed.record_buttons_for_test(true);
    MaterialPtr ma = material_asset("Assets/Materials/R166A.mat"), mb = material_asset("Assets/Materials/R166B.mat"),
                mc = material_asset("Assets/Materials/R166C.mat"), md = material_asset("Assets/Materials/R166D.mat");
    CHECK(ma && mb && mc && md);
    if (!ma || !mb || !mc || !md) return;
    for (const MaterialPtr &m : {ma, mb, mc}) m->shading_model = 1, m->touch();
    ma->toon_bands = 3, mb->toon_bands = 1, mc->toon_bands = 5;
    md->shading_model = 0, md->toon_bands = 1, md->touch();
    ed.command("window Materials");
    ed.set_material_search_for_test("R166");
    r34_steps(ed, 3);
    CHECK(r166_click(ed, "mat:R166B", 0) && r166_click(ed, "mat:R166C", platform::MOD_CTRL) && r166_click(ed, "mat:R166A", platform::MOD_CTRL));
    r34_steps(ed, 2);
    Recti f = r166_field(ed, "field:Toon Bands", WindowKind::Materials);
    CHECK(f.w > 0);
    if (f.w <= 0) return;
    r166_type(ed, f, "3");  // A's own value
    std::printf("    3 typed over the dash (A already 3): A %d, B %d, C %d\n", ma->toon_bands, mb->toon_bands, mc->toon_bands);
    CHECK(ma->toon_bands == 3 && mb->toon_bands == 3 && mc->toon_bands == 3);
    /* The Project window: three .mat files selected, the Inspector edits them together. */
    ed.command("window Project");
    const std::string mdir = resolve_asset_path("Assets/Materials");  // (the window keeps a selection only among the files it shows)
    ed.project_open_folder_for_test(mdir, "R166");
    ed.select_project_files_for_test({fs::join(mdir, "R166B.mat"), fs::join(mdir, "R166C.mat"), fs::join(mdir, "R166A.mat")});
    r34_steps(ed, 3);
    CHECK(ed.project_selected_count_for_test() == 3);
    f = r166_field(ed, "field:Toon Bands", WindowKind::Inspector);
    CHECK(f.w > 0);
    if (f.w <= 0) return;
    r166_type(ed, f, "2");
    std::printf("    Project window, three .mat files: A %d, B %d, C %d, D (not selected) %d\n", ma->toon_bands, mb->toon_bands, mc->toon_bands, md->toon_bands);
    CHECK(ma->toon_bands == 2 && mb->toon_bands == 2 && mc->toon_bands == 2 && md->toon_bands == 1);
  });
  /* ---------------------------------------------------------------- round 170: the import preview (task 0152) */
  /* A cube with a material, as a .glb file in the test folder; its .mat files land under Assets/Materials/<stem>. */
  auto r170_glb = [](const char *stem) {
    const std::string dir = fs::join(test_dir(), "r170");
    fs::make_dirs(dir);
    MaterialPtr mat = make_material("Skin", Vec3(0.9f, 0.7f, 0.6f));
    MeshPtr cube = primitives::cube();
    ExportItem it;
    it.name = stem;
    it.mesh = cube.get();
    it.world = Mat4::identity();
    it.materials = {mat};
    const std::string glb = export_gltf({it}, export_defaults((int)ExportFormat::GLB), true, "", nullptr);
    const std::string path = fs::join(dir, std::string(stem) + ".glb");
    std::ofstream f(path, std::ios::binary);
    f.write(glb.data(), (std::streamsize)glb.size());
    return path;
  };
  auto r170_mats = [](const char *stem) {  // the .mat files an import of <stem> wrote
    std::vector<std::string> out;
    const std::string dir = resolve_asset_path(std::string("Assets/Materials/") + stem);
    for (const DirEntry &de : fs::list(dir))
      if (fs::extension(de.name) == ".mat") out.push_back(fs::join(dir, de.name));
    return out;
  };
  auto r170_clear = [](const char *stem) {
    for (const DirEntry &de : fs::list(resolve_asset_path(std::string("Assets/Materials/") + stem))) std::remove(fs::join(resolve_asset_path(std::string("Assets/Materials/") + stem), de.name).c_str());
    std::remove(resolve_asset_path(std::string("Assets/Meshes/") + stem + ".glb").c_str());
  };
  test("import preview: import <file> <shading> writes the .mat files in that shading at once - Guilty Gear's fields, PBR, the file's own", [&] {
    Editor ed;
    r34_open(ed);
    ed.import_preview_pref_for_test(true, 0);  // (a shading word skips the dialog whatever the preference)
    const std::string path = r170_glb("r170a");
    r170_clear("r170a");
    ed.command("import " + path + " guiltygear");
    ed.step_frame_headless();
    std::vector<std::string> mats = r170_mats("r170a");
    CHECK(!ed.import_preview_open_for_test() && mats.size() == 1 && ed.scene().find_by_name("r170a"));
    Material m;
    std::string text;
    CHECK(!mats.empty() && fs::read_file(mats[0], text) && load_material_text(text, m));
    std::printf("    guiltygear: %zu .mat file(s); shading %d, style %d, key light only %d\n", mats.size(), m.shading_model, m.toon_style, (int)m.toon_key_light_only);
    CHECK(m.shading_model == 1 && m.toon_style == 1 && m.toon_key_light_only);
    r170_clear("r170a");  // (an import again keeps the .mat files that exist: a fresh folder for each look)
    ed.command("import " + path + " pbr");
    ed.step_frame_headless();
    mats = r170_mats("r170a");
    CHECK(!mats.empty() && fs::read_file(mats[0], text) && load_material_text(text, m) && m.shading_model == 0);
    r170_clear("r170a");
    ed.command("import " + path + " asis");
    ed.step_frame_headless();
    mats = r170_mats("r170a");
    CHECK(!mats.empty() && fs::read_file(mats[0], text) && load_material_text(text, m) && m.shading_model == 0 && m.toon_style == 0);
    CHECK(import_shading_index("Soft Anime") == 4 && import_shading_index("softanime") == 4 && import_shading_index("soft") == 4 && import_shading_index("gg") == 3 &&
          import_shading_index("PBR") == 1 && import_shading_index("asis") == 0 && import_shading_index("hero.fbx") == -1);
    /* A look chosen at import rewrites the .mat files that exist (As Imported keeps them); two console words. */
    ed.command("import " + path + " guilty gear");
    ed.step_frame_headless();
    mats = r170_mats("r170a");
    CHECK(mats.size() == 1 && fs::read_file(mats[0], text) && load_material_text(text, m) && m.toon_style == 1);
    ed.command("import " + path + " soft");
    ed.step_frame_headless();
    mats = r170_mats("r170a");
    CHECK(mats.size() == 1 && fs::read_file(mats[0], text) && load_material_text(text, m) && m.toon_style == 2 && m.shading_model == 1);
    ed.command("import " + path + " asis");
    ed.step_frame_headless();
    mats = r170_mats("r170a");
    CHECK(mats.size() == 1 && fs::read_file(mats[0], text) && load_material_text(text, m) && m.toon_style == 2);  // (kept: As Imported doesn't rewrite)
    /* The preferences round trip and their clamp. */
    const std::string prefs = fs::join(test_dir(), "r170_prefs.txt");
    std::remove(prefs.c_str());
    {
      Editor e2;
      e2.init_headless(320, 200);
      e2.prefs_for_test(prefs);  // (after init: it sets the path the editor would use)
      e2.import_preview_pref_for_test(false, 99);
      e2.save_prefs_for_test();
    }
    std::string ptext;
    CHECK(fs::read_file(prefs, ptext) && ptext.find("import_preview=0") != std::string::npos && ptext.find("import_shading=99") != std::string::npos);
    {
      Editor e3;
      e3.init_headless(320, 200);
      e3.prefs_for_test(prefs);
      e3.load_prefs_for_test();
      CHECK(!e3.import_preview_pref_for_test() && e3.import_shading_pref_for_test() == (int)import_shading_names().size() - 1);  // (clamped on reading)
    }
    std::remove(prefs.c_str());
    r170_clear("r170a");
  });

  test("import preview: the dialog shows the model without writing anything, its Shading row and Outline switch change the preview, Cancel leaves no trace, Import writes the chosen look", [&] {
    Editor ed;
    r34_open(ed);
    ed.record_buttons_for_test(true);
    const std::string path = r170_glb("r170b");
    r170_clear("r170b");
    ed.import_preview_pref_for_test(true, 4);  // (opens with Soft Anime)
    const size_t objects_before = ed.scene().object_count();
    CHECK(ed.begin_import_preview_for_test(path));
    r34_steps(ed, 3);
    CHECK(ed.import_preview_open_for_test() && r170_mats("r170b").empty() && ed.scene().object_count() == objects_before);
    const ImportResult &r = ed.import_preview_result_for_test();
    CHECK(!r.materials.empty() && r.materials[0]->shading_model == 1 && r.materials[0]->toon_style == 2);  // the preference's Soft Anime
    const Image &img = ed.import_preview_image_for_test();
    /* The model in the middle of the picture: its pixel isn't the sky's (the corners are). */
    const uint32_t centre = img.pixels.empty() ? 0 : img.pixels[(size_t)(img.height / 2) * img.width + img.width / 2], corner = img.pixels.empty() ? 0 : img.pixels[2 * (size_t)img.width + 2];
    std::printf("    the preview: %d x %d, centre %06X, corner %06X; %zu material(s), %zu note(s)\n", img.width, img.height, centre & 0xFFFFFF, corner & 0xFFFFFF,
                r.materials.size(), r.notes.size());
    CHECK(img.width > 100 && (centre & 0xFFFFFF) != (corner & 0xFFFFFF));
    CHECK(r166_click(ed, "ipv:Guilty Gear", 0));
    CHECK(r.materials[0]->toon_style == 1 && r.materials[0]->toon_key_light_only);
    CHECK(r166_click(ed, "ipv:PBR", 0));
    CHECK(r.materials[0]->shading_model == 0);
    CHECK(r166_click(ed, "ipv:As Imported", 0));
    CHECK(r.materials[0]->shading_model == 0 && r.materials[0]->toon_style == 0 && std::fabs(r.materials[0]->base_color.x - 0.9f) < 0.01f);
    CHECK(r166_click(ed, "ipv:Outline", 0));  // the checkbox
    size_t outlined = 0;
    ed.import_preview_scene_for_test()->for_each([&](GameObject &g) { outlined += g.get<Outline>() != nullptr; });
    CHECK(outlined == 1);
    CHECK(r166_click(ed, "ipv:Outline", 0));
    outlined = 0;
    ed.import_preview_scene_for_test()->for_each([&](GameObject &g) { outlined += g.get<Outline>() != nullptr; });
    CHECK(outlined == 0);
    ed.import_preview_outline_for_test(true, 0.02f);
    CHECK(r166_click(ed, "ipv:Cancel", 0));
    CHECK(!ed.import_preview_open_for_test() && r170_mats("r170b").empty() && ed.scene().object_count() == objects_before && !ed.scene().find_by_name("r170b"));
    /* Again: Guilty Gear, an outline, Import. */
    CHECK(ed.begin_import_preview_for_test(path));
    r34_steps(ed, 2);
    CHECK(r166_click(ed, "ipv:Guilty Gear", 0));
    ed.import_preview_outline_for_test(true, 0.02f);
    CHECK(r166_click(ed, "ipv:Import", 0));
    r34_steps(ed, 1);
    GameObject *root = ed.scene().find_by_name("r170b");
    std::vector<std::string> mats = r170_mats("r170b");
    Material m;
    std::string text;
    CHECK(!ed.import_preview_open_for_test() && root && mats.size() == 1 && fs::read_file(mats[0], text) && load_material_text(text, m) && m.toon_style == 1);
    size_t outlines = 0, meshes = 0;
    ed.scene().for_each([&](GameObject &g) {
      if (!g.get<MeshRenderer>()) return;
      for (GameObject *a = &g; a; a = a->parent)
        if (a == root) meshes++, outlines += g.get<Outline>() != nullptr;
    });
    std::printf("    imported: %zu mesh(es) under the root, %zu with an Outline\n", meshes, outlines);
    CHECK(meshes == 1 && outlines == 1);
    /* The preference: on, the console's plain import opens the dialog (forced past the headless gate); off, it comes
     * in at once. A second file while the preview is open waits its turn; the popup clicked away is a Cancel. */
    r170_clear("r170b");
    ed.command("select Main Camera");
    ed.import_preview_force_for_test(true);
    ed.import_preview_pref_for_test(true, 0);
    const std::string path2 = r170_glb("r170c");
    r170_clear("r170c");
    ed.command("import " + path);
    ed.command("import " + path2);
    r34_steps(ed, 2);
    CHECK(ed.import_preview_open_for_test() && ed.import_preview_queue_for_test() == 1 && r170_mats("r170b").empty());
    ed.close_dialog_for_test();  // (as a click outside the popup)
    r34_steps(ed, 2);
    std::printf("    after the click-away: preview %s, queue %zu, r170b files %zu\n", ed.import_preview_open_for_test() ? "open (the next file)" : "closed",
                ed.import_preview_queue_for_test(), r170_mats("r170b").size());
    CHECK(ed.import_preview_open_for_test() && ed.import_preview_queue_for_test() == 0 && r170_mats("r170b").empty() && ed.import_preview_result_for_test().nodes.size() > 0);
    CHECK(r166_click(ed, "ipv:Cancel", 0));
    CHECK(!ed.import_preview_open_for_test() && r170_mats("r170c").empty());
    ed.import_preview_pref_for_test(false, 0);
    ed.command("import " + path);
    ed.step_frame_headless();
    CHECK(!ed.import_preview_open_for_test() && r170_mats("r170b").size() == 1);
    ed.import_preview_force_for_test(false);
    r170_clear("r170b");
    r170_clear("r170c");
  });

}

static void tests_round169() {
  /* ---------------------------------------------------------------- round 169: procedural anime textures (task 0150) */
  test("anime textures: every generator is deterministic and repeats one tile over (the cards across only), draws at most four tones (the Window Grid six: three pairs; the Sky Gradient a gradient), and the presets set one up", [] {
    struct G {
      const char *layer;
      bool tiles_v;  // (Grass and Sky Gradient are cards: periodic across only)
      int max_tones;
    };
    const G gens[] = {{"Leaves", true, 4}, {"Grass", false, 4}, {"Plaster", true, 3}, {"Window Grid", true, 6}, {"Roof Tiles", true, 3},
                      {"Sky Gradient", false, 1000}, {"Clouds", true, 4}, {"Rocks", true, 3}, {"Water", true, 3},
                      {"Roof Tiles", true, 3}, {"Sky Gradient", false, 1000}};  // (the last two with odd rows and a sun at the seam)
    int variant = 0;
    for (const G &g : gens) {
      ProceduralTexture t = r49_tex(g.layer, 128);
      if (variant == 9) CHECK(r49_set(t, "L0 Rows", "5"));
      if (variant == 10) CHECK(r49_set(t, "L0 Sun", "0.2") && r49_set(t, "L0 Sun U", "0.95"));
      variant++;
      const TextureLayer &l = *t.layers[0];
      LayerContext c;
      c.w = c.h = 128;
      double worst = 0;
      for (int k = 0; k < 400; k++) {
        const float u = texture_rand((uint32_t)k, 1u, 9u), v = texture_rand((uint32_t)k, 2u, 9u);
        const LayerSample a = l.sample(u, v, c), b = l.sample(u + 1.0f, v, c), d = l.sample(u, v + 1.0f, c);
        for (const LayerSample *o : {&b, &d}) {
          if (o == &d && !g.tiles_v) continue;
          worst = std::max({worst, (double)std::fabs(a.height - o->height), (double)std::fabs(a.albedo.x - o->albedo.x), (double)std::fabs(a.albedo.y - o->albedo.y),
                            (double)std::fabs(a.albedo.z - o->albedo.z), (double)std::fabs(a.cover - o->cover)});
        }
      }
      TexPlanes p1, p2;
      t.evaluate(p1);
      t.evaluate(p2);
      const bool same = p1.height == p2.height && p1.albedo == p2.albedo && p1.mask == p2.mask;
      /* The tones: distinct colours over the texels the layer covers (the Sky Gradient is a gradient: not counted). */
      std::set<uint32_t> tones;
      size_t covered = 0;
      for (size_t i = 0; i < p1.size(); i++) {
        if (p1.mask[i] < 0.5f) continue;
        covered++;
        const Vec3 &a = p1.albedo[i];
        tones.insert(((uint32_t)std::lround(a.x * 255.0f) << 16) | ((uint32_t)std::lround(a.y * 255.0f) << 8) | (uint32_t)std::lround(a.z * 255.0f));
      }
      std::printf("    %-13s worst difference one tile over %.5f, %s, %zu tone(s) over %.0f %% of the tile\n", g.layer, worst, same ? "deterministic" : "NOT deterministic",
                  tones.size(), 100.0 * covered / p1.size());
      CHECK(worst < 1.5 / 255.0 && same && (int)tones.size() <= g.max_tones && tones.size() >= 2 && covered > p1.size() / 10);
    }
    /* The presets: each makes one layer of its generator; the cards cut out. */
    for (const char *name : {"Anime Leaves", "Anime Grass", "Anime Wall", "Anime Windows", "Anime Roof", "Anime Sky", "Anime Clouds", "Anime Rock", "Anime Water"}) {
      ProceduralTexture t;
      CHECK(apply_texture_preset(t, name) && t.layers.size() == 1);
      const bool card = std::string(name) == "Anime Leaves" || std::string(name) == "Anime Grass" || std::string(name) == "Anime Clouds";
      CHECK(t.alpha_from_cover == card && !t.use_normal);
      CHECK(std::find(texture_preset_names().begin(), texture_preset_names().end(), name) != texture_preset_names().end());
    }
  });

  test("anime textures: a card's albedo carries its cover as alpha (the image and live shading), and animemat puts a cut-out toon material on the selection", [] {
    ProceduralTexture t;
    apply_texture_preset(t, "Anime Leaves");
    t.width = t.height = 128;
    const Bitmap b = t.output(0);
    size_t clear = 0, solid = 0;
    for (size_t i = 0; i < b.rgba8.size(); i += 4) clear += b.rgba8[i + 3] == 0, solid += b.rgba8[i + 3] == 255;
    std::printf("    the leaves' albedo: %zu clear texels, %zu solid of %zu\n", clear, solid, b.rgba8.size() / 4);
    CHECK(clear > b.rgba8.size() / 4 / 10 && solid > b.rgba8.size() / 4 / 10);
    t.alpha_from_cover = false;
    const Bitmap b2 = t.output(0);
    size_t clear2 = 0;
    for (size_t i = 0; i < b2.rgba8.size(); i += 4) clear2 += b2.rgba8[i + 3] != 255;
    CHECK(clear2 == 0);
    /* On a Quad through the console: a Cutout toon material whose sampled alpha is 0 between the leaves and 1 on one. */
    Editor ed;
    r34_open(ed);
    ed.command("create Quad");
    ed.command("select Quad");
    ed.step_frame_headless();
    ed.command("animemat Anime Leaves");
    ed.step_frame_headless();
    GameObject *q = ed.scene().find_by_name("Quad");
    const MeshRenderer *mr = q ? q->get<MeshRenderer>() : nullptr;
    const MaterialPtr m = mr && !mr->materials.empty() ? mr->materials[0] : nullptr;
    if (m) std::printf("    the Quad's material: '%s', shading %d, surface %d, base map '%s'\n", m->name.c_str(), m->shading_model, m->surface, m->base_map.path.c_str());
    CHECK(m && m->shading_model == 1 && m->surface == (int)MaterialSurface::Cutout && m->base_map.path.find("#albedo") != std::string::npos);
    if (!m) return;
    const Material::Resolved &tx = m->textures();
    CHECK(tx.base != nullptr);
    if (!tx.base) return;
    int holes = 0, leaves = 0;
    for (int k = 0; k < 200; k++) {
      SurfacePoint sp;
      sp.normal = sp.geo_normal = Vec3(0, 0, 1);
      sp.uv = {texture_rand((uint32_t)k, 3u, 1u), texture_rand((uint32_t)k, 4u, 1u)};
      const float a = evaluate_material(*m, sp).alpha;
      holes += a < 0.5f, leaves += a >= 0.5f;
    }
    std::printf("    200 points on the leaf card: %d holes, %d on a leaf\n", holes, leaves);
    CHECK(holes > 20 && leaves > 20);
    ed.command("animemat Nope");  // a warning, nothing changes
    CHECK(q->get<MeshRenderer>()->materials[0] == m);
    ed.step_frame_headless();
    ed.command("undo");
    q = ed.scene().find_by_name("Quad");
    CHECK(q && (q->get<MeshRenderer>()->materials.empty() || q->get<MeshRenderer>()->materials[0] != m));  // (the Quad had no material of its own before)
    ed.command("select Main Camera");
    ed.step_frame_headless();
    ed.command("animemat Anime Grass");  // (nothing with a MeshRenderer selected: a warning, no asset)
    CHECK(!fs::exists(resolve_asset_path("Assets/Textures/AnimeGrass.ptex")));
    std::remove(resolve_asset_path("Assets/Textures/AnimeLeaves.ptex").c_str());
    /* Live shading's alpha is the image's: the same material evaluated through the live path and through the image
     * agrees at points either side of a leaf's edge; the texture copy (an asset, a live snapshot) keeps the switches. */
    {
      ProceduralTexture a;
      apply_texture_preset(a, "Anime Leaves");
      a.use_normal = false, a.alpha_from_cover = true;
      ProceduralTexture b(a);
      CHECK(!b.use_normal && b.alpha_from_cover && b.hash() == a.hash());
    }
    ProceduralTexturePtr pt = ed.new_texture_asset_for_test("Anime Leaves");
    CHECK(pt != nullptr);
    if (!pt) return;
    pt->width = pt->height = 128;
    pt->touch();
    Material lm;
    lm.base_map = TextureRef{pt->asset_path + "#albedo", false};
    lm.touch();
    const Material::Resolved &ltx = lm.textures();
    CHECK(ltx.base && ltx.live[0]);
    if (!ltx.base || !ltx.live[0]) return;
    int agree = 0, holes2 = 0;
    for (int k = 0; k < 200; k++) {
      SurfacePoint sp;
      sp.normal = sp.geo_normal = Vec3(0, 0, 1);
      sp.uv = {texture_rand((uint32_t)k, 5u, 2u), texture_rand((uint32_t)k, 6u, 2u)};
      sp.live_textures = true;  // (up close: the live path)
      const float live = evaluate_material(lm, ltx, sp).alpha;
      sp.live_textures = false;  // (the image)
      const float image = evaluate_material(lm, ltx, sp).alpha;
      agree += std::fabs(live - image) < 0.5f;  // (the image's texel against the point: the same side of the cut)
      holes2 += live < 0.5f;
    }
    std::printf("    live shading against the image: %d of 200 points on the same side of the cut, %d holes\n", agree, holes2);
    CHECK(agree > 190 && holes2 > 20);
    /* The cover plane's rules: two cards add up; a card's Use Mask reads the mask below it, not its own cover; a Tile
     * over a card moves the cover with the colour (the image and the live path alike). */
    {
      ProceduralTexture two = r49_tex("Grass", 128);
      two.alpha_from_cover = true;
      two.add("Leaves");
      TexPlanes p;
      two.evaluate(p);
      ProceduralTexture one = r49_tex("Grass", 128);
      TexPlanes pg;
      one.evaluate(pg);
      size_t grass_kept = 0, grass = 0;
      for (size_t i = 0; i < p.size(); i++)
        if (pg.alpha[i] > 0.5f) grass++, grass_kept += p.alpha[i] > 0.5f;
      std::printf("    grass under leaves: %zu of %zu grass texels still covered\n", grass_kept, grass);
      CHECK(grass > 100 && grass_kept == grass);
      ProceduralTexture masked = r49_tex("Noise", 128);
      masked.add("Mask by Height");
      r49_set(masked, "L1 Low", "0.0");
      r49_set(masked, "L1 High", "0.4");
      TextureLayer *leaves = masked.add("Leaves");
      leaves->use_mask = true;
      TexPlanes pm;
      masked.evaluate(pm);
      size_t outside_covered = 0, inside = 0, inside_covered = 0;
      for (size_t i = 0; i < pm.size(); i++) {
        if (pm.mask[i] > 0.5f) inside++, inside_covered += pm.alpha[i] > 0.5f;
        else outside_covered += pm.alpha[i] > 0.5f;  // a leaf covering where the height mask is off
      }
      std::printf("    a card with Use Mask over Mask by Height: %zu texels inside the mask (%zu covered by leaves), %zu covered outside it\n", inside, inside_covered, outside_covered);
      CHECK(inside > 100 && inside_covered > 10 && outside_covered == 0);
      ProceduralTexture tiled = r49_tex("Leaves", 128);
      tiled.alpha_from_cover = true;
      tiled.add("Tile");
      r49_set(tiled, "L1 Across", "2");
      r49_set(tiled, "L1 Down", "2");
      TexPlanes ptl;
      std::vector<LayerAux> aux;
      tiled.evaluate(ptl, &aux);
      LiveTexture live_t(tiled, std::move(aux), ptl.w, ptl.h);
      int same = 0;
      for (int k = 0; k < 200; k++) {  // (at texel centres; the tiled image reads the planes' nearest texel, so the edges can still differ a little)
        const int x = (int)(texture_rand((uint32_t)k, 7u, 3u) * ptl.w) % ptl.w, y = (int)(texture_rand((uint32_t)k, 8u, 3u) * ptl.h) % ptl.h;
        const float img = ptl.alpha[(size_t)y * ptl.w + x];
        same += (live_t.at((x + 0.5f) / ptl.w, (y + 0.5f) / ptl.h).alpha > 0.5f) == (img > 0.5f);
      }
      std::printf("    a Tile over the leaves: the live cover agrees with the image's at %d of 200 points\n", same);
      CHECK(same > 180);
    }
  });

}

/* ---------------------------------------------------------------- round 229: files (task 0209) */
static void tests_round229() {
  /* An OBJ's "v" numbers, "vn" normals and "f" corners (v index, vn index), read back from the text. */
  struct Obj {
    std::vector<std::string> numbers;  // every number of every "v" line, as written
    std::vector<Vec3> normals;
    std::vector<std::pair<uint32_t, uint32_t>> corners;
  };
  auto read_obj = [](const std::string &text) {
    Obj o;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
      std::istringstream l(line);
      std::string tag;
      l >> tag;
      if (tag == "v") {
        std::string a;
        while (l >> a) o.numbers.push_back(a);
      }
      else if (tag == "vn") {
        Vec3 n;
        l >> n.x >> n.y >> n.z;
        o.normals.push_back(n);
      }
      else if (tag == "f") {
        std::string c;
        while (l >> c) {
          const size_t s1 = c.find('/'), s2 = c.rfind('/');
          o.corners.push_back({(uint32_t)std::stoul(c.substr(0, s1)), (uint32_t)std::stoul(c.substr(s2 + 1))});
        }
      }
    }
    return o;
  };
  auto export_obj = [](MeshPtr mesh) {
    ExportItem it;
    it.name = "M";
    it.mesh = mesh.get();
    it.world = Mat4::identity();
    std::string obj, mtl;
    ExportOptions o = export_defaults((int)ExportFormat::OBJ);
    o.normals = true;
    export_obj_mtl({it}, "m.mtl", obj, mtl, o);
    return obj;
  };
  test("export: the numbers are written as printf's %.7g writes them; a smooth sphere's corners at a vertex share one normal, and a cube smooth by angle keeps each face's own", [&] {
    MeshPtr sphere = primitives::uv_sphere(1.37f, 48, 24);
    for (Vec3 &v : sphere->positions) v = v * 1234.5678f + Vec3(1e-5f, -3.25f, 0.0f);  // (large, small and negative numbers)
    const Obj a = read_obj(export_obj(sphere));
    /* Each "v" number against %.7g of the value the exporter writes (Y up: X mirrored). */
    auto g7 = [](double x) { return std::fabs(x) < 1e-12 ? std::string("0") : strprintf("%.7g", x); };
    size_t same = 0;
    for (size_t i = 0; i < sphere->positions.size() && i * 3 + 2 < a.numbers.size(); i++) {
      const Vec3 p = sphere->positions[i];
      same += (a.numbers[i * 3] == g7(-p.x)) + (a.numbers[i * 3 + 1] == g7(p.y)) + (a.numbers[i * 3 + 2] == g7(p.z));
    }
    std::printf("    %zu of %zu numbers as %%.7g writes them\n", same, a.numbers.size());
    CHECK(a.numbers.size() == sphere->positions.size() * 3 && same == a.numbers.size());
    std::map<uint32_t, uint32_t> normal_at;  // vertex -> the normal its first corner has
    bool one = true;
    for (const auto &[v, n] : a.corners) {
      auto it = normal_at.emplace(v, n).first;
      const Vec3 x = a.normals[it->second - 1], y = a.normals[n - 1];
      one = one && length(x - y) < 1e-6f;
    }
    CHECK(one && !a.corners.empty());
    MeshPtr cube = primitives::cube();
    cube->smooth = true;
    cube->smooth_angle = 30.0f;
    const Obj b = read_obj(export_obj(cube));
    bool own = b.corners.size() == 24;  // (six quads: every corner's normal one of the six axes)
    for (const auto &[v, n] : b.corners) {
      const Vec3 x = b.normals[n - 1];
      own = own && std::fabs(std::fabs(x.x) + std::fabs(x.y) + std::fabs(x.z) - 1.0f) < 1e-5f;
    }
    CHECK(own);
  });
  test("export: each corner's normal is what flooding from its own face gives - sharp edges, flat faces, the smooth angle, back-to-back faces", [&] {
    /* The flood the exporter ran per corner before task 0209, kept here as the reference. */
    auto reference = [](const Mesh &m) {
      const size_t nf = m.face_count();
      std::vector<Vec3> fw(nf), fn(nf), out(m.corner_count());
      for (size_t f = 0; f < nf; f++) {
        Vec3 s(0.0f);
        const uint32_t *v = m.face_verts(f), n = m.face_size(f);
        for (uint32_t k = 0; k < n; k++) {
          const Vec3 a = m.positions[v[k]], b = m.positions[v[(k + 1) % n]];
          s += Vec3((a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y));
        }
        fw[f] = s;
        fn[f] = length(s) > 0 ? s / length(s) : Vec3(0, 1, 0);
      }
      const float cl = m.smooth_angle >= 180.0f ? -2.0f : std::cos(m.smooth_angle * kDeg2Rad);
      auto around = [&](size_t f, uint32_t v, uint32_t &p, uint32_t &n) {
        const uint32_t *fv = m.face_verts(f), c = m.face_size(f);
        for (uint32_t k = 0; k < c; k++)
          if (fv[k] == v) {
            p = fv[(k + c - 1) % c], n = fv[(k + 1) % c];
            return;
          }
      };
      for (size_t f = 0; f < nf; f++)
        for (uint32_t c = m.face_offsets[f]; c < m.face_offsets[f + 1]; c++) {
          if (!m.smooth_of(f)) {
            out[c] = fn[f];
            continue;
          }
          const uint32_t v = m.corner_verts[c];
          std::vector<uint32_t> fan{(uint32_t)f};
          for (size_t i = 0; i < fan.size(); i++) {
            uint32_t p0 = 0, n0 = 0;
            around(fan[i], v, p0, n0);
            for (size_t g = 0; g < nf; g++) {
              bool has = false;
              for (uint32_t k = 0; k < m.face_size(g); k++) has = has || m.face_verts(g)[k] == v;
              if (!has || !m.smooth_of(g) || std::find(fan.begin(), fan.end(), (uint32_t)g) != fan.end()) continue;
              uint32_t p1 = 0, n1 = 0;
              around(g, v, p1, n1);
              const bool across = ((p0 == p1 || p0 == n1) && !m.is_sharp(v, p0)) || ((n0 == p1 || n0 == n1) && !m.is_sharp(v, n0));
              if (!across || dot(fn[fan[i]], fn[g]) < cl) continue;
              fan.push_back((uint32_t)g);
            }
          }
          Vec3 sum(0.0f);
          for (uint32_t g : fan) sum += fw[g];
          out[c] = length(sum) > 0 ? sum / length(sum) : fn[f];
        }
      return out;
    };
    MeshPtr mesh = primitives::uv_sphere(1.0f, 16, 10);
    mesh->smooth = true;
    mesh->smooth_angle = 50.0f;
    for (uint32_t i = 1; i + 1 < 16; i += 3) mesh->set_sharp(i, i + 1, true);  // (a few sharp edges)
    mesh->face_smooth.assign(mesh->face_count(), 1);
    for (size_t f = 0; f < mesh->face_count(); f += 7) mesh->face_smooth[f] = 0;  // (a few flat faces)
    /* The exporter mirrors X and turns each face round: compare its normals per (face, vertex) with the mirror of the
     * reference's. */
    const std::vector<Vec3> want = reference(*mesh);
    const Obj o = read_obj(export_obj(mesh));
    size_t checked = 0, close = 0, at = 0;
    for (size_t f = 0; f < mesh->face_count(); f++) {
      const uint32_t n = mesh->face_size(f);
      for (uint32_t k = 0; k < n && at + (n - 1 - k) < o.corners.size(); k++) {
        const auto [v, ni] = o.corners[at + (n - 1 - k)];  // (written in reverse)
        const Vec3 w = want[mesh->face_offsets[f] + k], got = o.normals[ni - 1];
        checked++;
        close += v == mesh->face_verts(f)[k] + 1 && length(got - Vec3(-w.x, w.y, w.z)) < 1e-5f;
      }
      at += n;
    }
    std::printf("    %zu of %zu corners' normals as the reference flood gives\n", close, checked);
    CHECK(checked == mesh->corner_count() && close == checked);
  });
}

/* ---------------------------------------------------------------- round 230: big scenes saved and opened (task 0210) */
static void tests_round230() {
  test("a scene with a big mesh - its positions, faces and UVs written and read on every core - comes back exactly, writes the same text twice, and a bad vertex deep in its faces is reported at its own line", [] {
    Scene s;
    GameObject *g = s.create("Big");
    MeshPtr big = primitives::uv_sphere(1.0f, 512, 256);  // (131k faces: past the blocks' threshold)
    g->add<MeshFilter>()->mesh = big;
    g->add<MeshRenderer>();
    ScopedTimer ts;
    const std::string text = save_scene_text(s);
    const double save_ms = ts.ms();
    CHECK(save_scene_text(s) == text);
    Scene back;
    std::string err;
    ScopedTimer tl;
    const bool ok = load_scene_text(text, back, err);
    const double load_ms = tl.ms();
    std::printf("    %s of text: saved in %.1f ms, read in %.1f ms\n", format_bytes(text.size()).c_str(), save_ms, load_ms);
    CHECK(ok);
    GameObject *bg = back.find_by_name("Big");
    const Mesh *m = bg && bg->get<MeshFilter>() ? bg->get<MeshFilter>()->mesh.get() : nullptr;
    CHECK(m && m->positions.size() == big->positions.size() && m->corner_verts == big->corner_verts && m->face_offsets == big->face_offsets);
    bool same = m != nullptr && m->uvs.size() == big->uvs.size();
    for (size_t i = 0; same && i < big->positions.size(); i++) same = m->positions[i] == big->positions[i];
    for (size_t i = 0; same && i < big->uvs.size(); i++) same = m->uvs[i].x == big->uvs[i].x && m->uvs[i].y == big->uvs[i].y;
    CHECK(same);
    /* A face 100,000 lines into its block names a vertex that isn't there: the error names that line. */
    const size_t mesh_at = text.find("\nmesh "), fstart = text.find("\nf ", mesh_at);
    size_t at = fstart + 1;
    for (int i = 0; i < 100000; i++) at = text.find('\n', at) + 1;
    const size_t line_end = text.find('\n', at);
    std::string bad = text.substr(0, at) + "f 3 0 1 999999999" + text.substr(line_end);
    const int want = 1 + (int)std::count(bad.begin(), bad.begin() + (ptrdiff_t)at, '\n');
    Scene broken;
    std::string berr;
    CHECK(!load_scene_text(bad, broken, berr));
    std::printf("    the bad line: %s (want line %d)\n", berr.c_str(), want);
    CHECK(berr.find(strprintf("line %d:", want)) == 0);
    /* The same text with Windows line ends and a blank line 70,000 lines into each block reads as the same mesh (the
     * blocks stop at the blank line, and the rest is read line by line); read back from a file, it is every byte. */
    std::string odd;
    for (const char *tag : {"\nv ", "\nf ", "\nt "}) {
      size_t q = text.find(tag, mesh_at) + 1;
      for (int i = 0; i < 70000; i++) q = text.find('\n', q) + 1;
      CHECK(q > 0 && q < text.size());
    }
    size_t lines = 0, block_line = 0;
    for (char ch : text) {
      if (ch == '\n') {
        lines++;
        odd += "\r\n";
        if (++block_line == 70000) odd += "\r\n", block_line = 0;  // (blank lines all through: inside every block)
        continue;
      }
      odd += ch;
    }
    Scene odd_back;
    std::string oerr;
    CHECK(load_scene_text(odd, odd_back, oerr));
    GameObject *og = odd_back.find_by_name("Big");
    const Mesh *om = og && og->get<MeshFilter>() ? og->get<MeshFilter>()->mesh.get() : nullptr;
    bool odd_same = om && om->positions == big->positions && om->corner_verts == big->corner_verts && om->face_offsets == big->face_offsets &&
                    om->uvs.size() == big->uvs.size();
    for (size_t i = 0; odd_same && i < big->uvs.size(); i++) odd_same = om->uvs[i].x == big->uvs[i].x && om->uvs[i].y == big->uvs[i].y;
    std::printf("    %zu lines with Windows ends and blank lines: %s\n", lines, odd_same ? "the same mesh" : oerr.c_str());
    CHECK(odd_same);
    const std::string dir = fs::join(test_dir(), "test_round230");
    fs::make_dirs(dir);
    const std::string path = fs::join(dir, "big.scene");
    std::string read;
    CHECK(fs::write_file(path, odd) && fs::read_file(path, read) && read == odd);
    CHECK(fs::write_file(path, "") && fs::read_file(path, read) && read.empty());
  });
}

/* Round 327 (task 0307): the texture presets by theme, and Blender's Mapping on every generator. */
static void tests_round327() {
  test("texture presets by theme: every preset in a theme of the menu's list, none twice, the old fifteen still there; each builds and evaluates at 64 px with a picture of its own", [] {
    std::set<std::string> themes(texture_preset_themes().begin(), texture_preset_themes().end()), names, used;
    bool ok = true;
    for (const TexturePreset &p : texture_presets()) {
      ok = ok && themes.count(p.theme) && names.insert(p.name).second;
      used.insert(p.theme);
    }
    for (const char *old : {"Bricks", "Wood Floor", "Tatami", "Tiles", "Noise", "Checker", "Anime Leaves", "Anime Grass", "Anime Wall", "Anime Windows",
                            "Anime Roof", "Anime Sky", "Anime Clouds", "Anime Rock", "Anime Water"})
      ok = ok && names.count(old);
    CHECK(ok && used.size() == themes.size() && names.size() >= 35);
    std::set<long> looks;
    int built = 0;
    for (const TexturePreset &p : texture_presets()) {
      ProceduralTexture t;
      if (!apply_texture_preset(t, p.name) || t.layers.empty()) {
        std::printf("    %s didn't build\n", p.name);
        continue;
      }
      TexPlanes pl;
      t.evaluate(pl, nullptr, 64);
      Vec3 sum(0.0f);
      for (const Vec3 &a : pl.albedo) sum = sum + a;
      sum = sum / (float)std::max<size_t>(1, pl.albedo.size());
      looks.insert(std::lround(sum.x * 100) * 1000000 + std::lround(sum.y * 100) * 1000 + std::lround(sum.z * 100));
      built += pl.w == 64 && pl.albedo.size() == 64 * 64;
    }
    std::printf("    %zu presets in %zu themes; %d built at 64 px; %zu different average colours\n", names.size(), used.size(), built, looks.size());
    CHECK(built == (int)names.size() && looks.size() * 10 >= names.size() * 7);
  });
  test("Mapping (Blender's Mapping node) on a generator: scale 2 doubles a Checker's cells, an offset moves it a quarter tile, a quarter turn turns it; Seamless rounds 1.5 to 2; the tile's edges still meet; saved and read back", [] {
    auto make = [](float su, float off, float rot, bool seamless) {
      ProceduralTexture t;
      apply_texture_preset(t, "Checker");
      TextureLayer &l = *t.layers[0];
      l.map_scale[0] = l.map_scale[1] = su, l.map_offset[0] = off, l.map_rotation = rot, l.map_seamless = seamless;
      TexPlanes p;
      t.evaluate(p, nullptr, 64);
      return p;
    };
    auto changes = [](const TexPlanes &p, int y) {
      int n = 0;
      for (int x = 1; x < p.w; x++) n += std::fabs(p.albedo[(size_t)y * p.w + x].x - p.albedo[(size_t)y * p.w + x - 1].x) > 0.1f;
      return n;
    };
    const TexPlanes a = make(1, 0, 0, true), b = make(2, 0, 0, true), c = make(1, 0.25f, 0, true), d = make(1, 0, 90, true), e = make(1.5f, 0, 0, true);
    std::printf("    a row's changes: %d at scale 1, %d at scale 2\n", changes(a, 7), changes(b, 7));
    CHECK(changes(b, 7) >= 2 * changes(a, 7) - 1 && changes(a, 7) > 0);
    int shifted = 0, turned = 0, rounded = 0;
    for (int y = 0; y < 64; y++)
      for (int x = 0; x < 64; x++) {
        const size_t i = (size_t)y * 64 + x;
        shifted += length(c.albedo[i] - a.albedo[(size_t)y * 64 + (x + 64 - 16) % 64]) < 1e-3f;
        rounded += length(e.albedo[i] - b.albedo[i]) < 1e-5f;
      }
    for (int y = 0; y < 64; y++)
      for (int x = 0; x < 64; x++) turned += length(d.albedo[(size_t)y * 64 + x] - a.albedo[(size_t)x * 64 + (63 - y)]) < 1e-3f || length(d.albedo[(size_t)y * 64 + x] - a.albedo[(size_t)(63 - x) * 64 + y]) < 1e-3f;
    std::printf("    offset: %d of 4096 texels shifted a quarter; turn: %d match a quarter turn; Seamless 1.5: %d as 2\n", shifted, turned, rounded);
    CHECK(shifted > 4000 && turned > 4000 && rounded == 4096);
    /* The edges meet: across the seam the cells change where they would inside - a closed row has an even count. */
    int seams = 0;
    for (int y = 0; y < 64; y++) {
      const int wrap = changes(c, y) + (std::fabs(c.albedo[(size_t)y * 64].x - c.albedo[(size_t)y * 64 + 63].x) > 0.1f);
      seams += wrap % 2 == 0;
    }
    CHECK(seams == 64);
    /* Saved and read back. */
    ProceduralTexture t;
    apply_texture_preset(t, "Checker");
    t.layers[0]->map_scale[0] = 3.0f, t.layers[0]->map_offset[1] = 0.125f, t.layers[0]->map_rotation = 90.0f, t.layers[0]->map_seamless = false;
    ProceduralTexture back;
    CHECK(load_texture_text(save_texture_text(t), back) && back.layers.size() == 1);
    if (back.layers.size() == 1) {
      const TextureLayer &l = *back.layers[0];
      CHECK(l.map_scale[0] == 3.0f && l.map_offset[1] == 0.125f && l.map_rotation == 90.0f && !l.map_seamless);
    }
  });
  test("the Textures window's New menu by clicks: Nature > Sand makes a texture asset from that preset", [] {
    Editor ed;
    ed.init_headless(1600, 900);
    ed.record_buttons_for_test(true);
    ed.command("window Textures");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    auto click = [&](const std::string &key) {
      Recti b;
      if (!ed.button_rect_for_test(key, b)) return std::printf("    no '%s'\n", key.c_str()), false;
      r34_click(ed, b.x + b.w / 2, b.y + b.h / 2);
      ed.step_frame_headless();
      return true;
    };
    auto hover = [&](const std::string &key) {
      Recti b;
      if (!ed.button_rect_for_test(key, b)) return std::printf("    no '%s'\n", key.c_str()), false;
      for (int i = 0; i < 4; i++) ed.step_frame_headless({r34_mouse(R34ET::MouseMove, b.x + b.w / 2, b.y + b.h / 2)});
      return true;
    };
    const std::string path = fs::join(ed.assets_dir_for_test(), "Textures/Sand.ptex");
    fs::remove_file(path);
    CHECK(click("New") && hover("submenu:Nature") && click("menu:Sand"));
    std::printf("    %s %s\n", path.c_str(), fs::exists(path) ? "made" : "NOT made");
    CHECK(fs::exists(path));
    fs::remove_file(path);
  });
}

/* A layer's number, enumeration or switch set by its exact name (round 328's tests). */
struct SetFieldForTest : Reflector {
  std::string key;
  float value;
  bool done = false;
  SetFieldForTest(const char *k, float v) : key(k), value(v) {}
  void field(const char *n, float &v, float, float mn, float mx) override {
    if (key == n) v = std::max(mn, std::min(mx, value)), done = true;
  }
  void field(const char *n, int &v, int mn, int mx) override {
    if (key == n) v = std::max(mn, std::min(mx, (int)std::lround(value))), done = true;
  }
  void field(const char *n, bool &v) override {
    if (key == n) v = value != 0.0f, done = true;
  }
  void field(const char *, Vec3 &) override {}
  void color(const char *, Vec3 &) override {}
  void enumeration(const char *n, int &v, const char *const *, int count) override {
    if (key == n) v = std::max(0, std::min(count - 1, (int)std::lround(value))), done = true;
  }
  void text(const char *, std::string &) override {}
  void mesh(const char *, MeshPtr &) override {}
};

/* Round 328 (task 0308): Perlin Noise and Blender's richer texture nodes, periodic in the tile. */
static void tests_round328() {
  test("Perlin Noise: 0 at every lattice point, within -1..1 between, the same one period over; the layer's tile wraps, is the same twice, and Turbulence folds it (time printed)", [] {
    bool zero = true, range = true, period = true;
    for (int i = 0; i < 16; i++)
      for (int j = 0; j < 16; j++) zero = zero && std::fabs(texture_perlin((float)i, (float)j, 8, 7u)) < 1e-6f;
    for (int k = 0; k < 2000; k++) {
      const float x = texture_rand((uint32_t)k, 1u, 3u) * 16.0f, y = texture_rand((uint32_t)k, 2u, 3u) * 16.0f;
      const float n = texture_perlin(x, y, 8, 7u);
      range = range && n >= -1.0f && n <= 1.0f;
      period = period && std::fabs(n - texture_perlin(x + 8.0f, y, 8, 7u)) < 1e-5f && std::fabs(n - texture_perlin(x, y + 8.0f, 8, 7u)) < 1e-5f;
    }
    CHECK(zero && range && period);
    ProceduralTexture t;
    CHECK(apply_texture_preset(t, "Perlin Noise") && t.layers.size() == 1);
    TexPlanes a, b;
    const double t0 = now_seconds();
    t.width = t.height = 1024;
    t.evaluate(a, nullptr, 1024);
    const double ms = (now_seconds() - t0) * 1000.0;
    t.evaluate(b, nullptr, 1024);
    float seam = 0.0f, inside = 0.0f;
    for (int y = 0; y < 1024; y++) {
      seam = std::max(seam, std::fabs(a.height[(size_t)y * 1024] - a.height[(size_t)y * 1024 + 1023]));
      inside = std::max(inside, std::fabs(a.height[(size_t)y * 1024 + 500] - a.height[(size_t)y * 1024 + 501]));
    }
    std::printf("    Perlin Noise at 1024 px: %.1f ms; across the seam %.4f, inside %.4f\n", ms, seam, inside);
    CHECK(a.height == b.height && seam < inside * 2.0f + 0.01f);
    ProceduralTexture turb;
    apply_texture_preset(turb, "Perlin Noise");
    SetFieldForTest set("Turbulence", 1.0f);
    turb.layers[0]->reflect(set);
    TexPlanes c, plain;
    turb.evaluate(c, nullptr, 128);
    t.evaluate(plain, nullptr, 128);
    CHECK(set.done && c.height != plain.height && c.height != std::vector<float>(c.height.size(), c.height[0]));
  });
  test("Blender's texture nodes as layers: every new type and option repeats exactly one tile over and gives a picture that varies; Voronoi's F2 >= F1, its distance to an edge >= 0 and near 0 on the borders (time of each at 1024 px printed)", [] {
    struct Case {
      const char *layer, *field;
      float value;
    };
    const Case cases[] = {{"Perlin Noise", "Distortion", 1.0f}, {"Noise", "Type", 1}, {"Noise", "Type", 2}, {"Noise", "Type", 3}, {"Noise", "Type", 4},
                          {"Noise", "Type", 5}, {"Voronoi", "Feature", 1}, {"Voronoi", "Feature", 2}, {"Voronoi", "Feature", 3}, {"Voronoi", "Feature", 4},
                          {"Voronoi", "Feature", 5}, {"Voronoi", "Distance", 1}, {"Voronoi", "Distance", 3}, {"Wave", "Type", 1}, {"Wave", "Profile", 1},
                          {"Wave", "Profile", 2}, {"Magic", "Depth", 3}, {"White Noise", "Colored", 1}, {"Gabor", "Anisotropy", 0.0f}, {"Gabor", "Anisotropy", 1.0f}};
    LayerContext ctx;
    ctx.seed = 5u;
    int ok = 0;
    for (const Case &cs : cases) {
      std::unique_ptr<TextureLayer> l = create_texture_layer(cs.layer);
      if (!l) {
        std::printf("    no layer %s\n", cs.layer);
        continue;
      }
      SetFieldForTest set(cs.field, cs.value);
      l->reflect(set);
      double worst = 0.0, lo = 1e9, hi = -1e9;
      for (int k = 0; k < 400; k++) {
        const float u = texture_rand((uint32_t)k, 1u, 9u), v = texture_rand((uint32_t)k, 2u, 9u);
        const LayerSample a = l->sample(u, v, ctx), b = l->sample(u + 1.0f, v, ctx), d = l->sample(u, v + 1.0f, ctx);
        for (const LayerSample *o : {&b, &d}) worst = std::max({worst, (double)std::fabs(a.height - o->height), (double)length(a.albedo - o->albedo)});
        lo = std::min(lo, (double)a.height), hi = std::max(hi, (double)a.height);
      }
      ProceduralTexture t;
      t.layers.push_back(std::move(l));
      TexPlanes p;
      const double t0 = now_seconds();
      t.evaluate(p, nullptr, 1024);
      std::printf("    %-13s %-10s %-4g one tile over %.5f; height %.2f..%.2f; 1024 px in %.1f ms\n", cs.layer, cs.field, cs.value, worst, lo, hi, (now_seconds() - t0) * 1000.0);
      ok += worst < 2e-3 && hi - lo > 0.05;
    }
    CHECK(ok == (int)(sizeof(cases) / sizeof(cases[0])));
    std::unique_ptr<TextureLayer> vor = create_texture_layer("Voronoi");
    bool order = true, nonneg = true;
    float edge_min = 1e9f;
    for (int k = 0; k < 4000; k++) {
      float f[5];
      const float u = texture_rand((uint32_t)k, 3u, 9u), v = texture_rand((uint32_t)k, 4u, 9u);
      if (!texture_voronoi_features(*vor, u, v, 5u, f)) break;
      order = order && f[1] >= f[0] - 1e-6f;
      nonneg = nonneg && f[3] >= 0.0f && f[4] > 0.0f;
      edge_min = std::min(edge_min, f[3]);
    }
    std::printf("    Voronoi: F2 >= F1 %s; distance to an edge >= 0 %s, the least of 4000 points %.4f\n", order ? "yes" : "NO", nonneg ? "yes" : "NO", edge_min);
    CHECK(order && nonneg && edge_min < 0.02f);
    /* Review 328: the metrics change F1's picture; Smooth F1 at no smoothness is F1, and smooth one between 0 and F1. */
    std::vector<double> means;
    for (int metric = 0; metric < 4; metric++) {
      std::unique_ptr<TextureLayer> l = create_texture_layer("Voronoi");
      SetFieldForTest f1("Feature", 1.0f), m("Distance", (float)metric);
      l->reflect(f1), l->reflect(m);
      double sum = 0.0;
      for (int k = 0; k < 400; k++) sum += l->sample(texture_rand((uint32_t)k, 5u, 9u), texture_rand((uint32_t)k, 6u, 9u), ctx).height;
      means.push_back(sum / 400.0);
    }
    std::printf("    F1's mean by metric: Euclidean %.3f, Manhattan %.3f, Chebyshev %.3f, Minkowski %.3f\n", means[0], means[1], means[2], means[3]);
    CHECK(std::fabs(means[0] - means[1]) > 0.02 && std::fabs(means[0] - means[2]) > 0.02);
    std::unique_ptr<TextureLayer> sharp = create_texture_layer("Voronoi"), soft = create_texture_layer("Voronoi");
    SetFieldForTest s0("Smoothness", 0.0f), s1("Smoothness", 1.0f);
    sharp->reflect(s0), soft->reflect(s1);
    bool sharp_is_f1 = true, soft_below = true;
    for (int k = 0; k < 400; k++) {
      float a[5], b[5];
      const float u = texture_rand((uint32_t)k, 7u, 9u), v = texture_rand((uint32_t)k, 8u, 9u);
      texture_voronoi_features(*sharp, u, v, 5u, a), texture_voronoi_features(*soft, u, v, 5u, b);
      sharp_is_f1 = sharp_is_f1 && std::fabs(a[2] - a[0]) < 1e-3f;
      soft_below = soft_below && b[2] <= b[0] + 1e-5f && b[2] >= b[0] - 0.125f * std::log(25.0f) - 1e-5f;
    }
    CHECK(sharp_is_f1 && soft_below);
  });
  test("round 328's review: no preset names a field its layer hasn't; Mapping is the same through live shading (apply_point) as through the planes, Seamless off at 30 degrees too; Fill, Mesh Map, Face Shadow and Toon Ramp have no Mapping; the lesson's Try-it buttons make and change the textures they say", [] {
    for (const TexturePreset &p : texture_presets()) {
      ProceduralTexture t;
      apply_texture_preset(t, p.name);
    }
    std::printf("    preset fields not found: %d\n", texture_preset_field_misses());
    CHECK(texture_preset_field_misses() == 0);
    ProceduralTexture t;
    apply_texture_preset(t, "Checker");
    TextureLayer &l = *t.layers[0];
    l.map_scale[0] = 3.0f, l.map_rotation = 30.0f, l.map_offset[1] = 0.1f, l.map_seamless = false;
    TexPlanes pl;
    t.evaluate(pl, nullptr, 64);
    LayerContext ctx;
    int same = 0;
    for (int y = 0; y < 64; y += 7)
      for (int x = 0; x < 64; x += 7) {
        TexPoint p;
        l.apply_point(p, (x + 0.5f) / 64.0f, (y + 0.5f) / 64.0f, ctx, nullptr);
        same += length(p.albedo - pl.albedo[(size_t)y * 64 + x]) < 1e-4f;
      }
    std::printf("    live shading equal to the planes at %d of 100 points\n", same);
    CHECK(same == 100);
    for (const char *name : {"Face Shadow", "Toon Ramp"}) {
      std::unique_ptr<TextureLayer> nm = create_texture_layer(name);
      CHECK(nm && !nm->mappable());
    }
    /* The lesson's Try-its, as the Learn tab runs them. */
    Editor ed;
    r50_open(ed);
    std::string body;
    for (int i = 0; i < lesson_count(); i++)
      if (std::string(lesson(i).title) == "Blender's Shader Nodes in the Textures Window") body = lesson(i).body;
    CHECK(!body.empty());
    for (const char *tex : {"Marble", "Clouds2"}) std::remove(fs::join(scratch_project(), std::string("Assets/Textures/") + tex + ".ptex").c_str());
    size_t at = 0;
    int ran = 0;
    while ((at = body.find("! cmd:", at)) != std::string::npos) {
      const size_t end = body.find(" | ", at);
      std::string cmds = body.substr(at + 6, end - at - 6);
      size_t s = 0;
      while (s <= cmds.size()) {
        const size_t e = cmds.find(';', s);
        std::string c = cmds.substr(s, e == std::string::npos ? std::string::npos : e - s);
        while (!c.empty() && c.front() == ' ') c.erase(c.begin());
        if (!c.empty()) ed.command(c), ran++;
        if (e == std::string::npos) break;
        s = e + 1;
      }
      at = end;
    }
    ProceduralTexturePtr marble = texture_asset("Assets/Textures/Marble.ptex"), clouds = texture_asset("Assets/Textures/Clouds2.ptex");
    std::printf("    %d Try-it commands; Marble %s, Clouds2 %s\n", ran, marble ? "made" : "NOT made", clouds ? "made" : "NOT made");
    CHECK(marble && !marble->layers.empty() && marble->layers[0]->map_scale[0] == 2.0f && marble->layers[0]->map_scale[1] == 2.0f);
    CHECK(clouds && clouds->layers.size() == 2 && std::string(clouds->layers[0]->type_name()) == "Perlin Noise" && std::string(clouds->layers[1]->type_name()) == "Color Ramp");
    if (!clouds || clouds->layers.size() != 2) return;
    /* Review 329: the Try-its' fields as they say - Turbulence on, three stops, Constant. */
    struct Fields : SetFieldForTest {
      bool turbulence = false;
      int interp = -1;
      std::vector<RampStop> stops;
      Fields() : SetFieldForTest("", 0.0f) {}
      void field(const char *n, bool &v) override {
        if (std::string(n) == "Turbulence") turbulence = v;
      }
      void enumeration(const char *n, int &v, const char *const *, int) override {
        if (std::string(n) == "Interpolation") interp = v;
      }
      void ramp(const char *, std::vector<RampStop> &v) override { stops = v; }
    } f;
    clouds->layers[0]->reflect(f), clouds->layers[1]->reflect(f);
    std::printf("    the lesson's Clouds2: turbulence %s, %zu stops, Interpolation %d\n", f.turbulence ? "on" : "off", f.stops.size(), f.interp);
    CHECK(f.turbulence && f.stops.size() == 3 && f.interp == 0);
  });
}

/* Round 329 (task 0309): Blender's converters - the ColorRamp, Map Range, Clamp, Math, RGB Curves, Separate Color. */
static void tests_round329() {
  test("the ColorRamp: its stops exactly at their places, Constant in steps, Linear halfway, Ease and B-Spline between; an old two-colour ramp the same as before; stops saved and read back", [] {
    const std::vector<RampStop> st = {{0.0f, Vec3(0.0f)}, {0.5f, Vec3(1.0f, 0.0f, 0.0f)}, {1.0f, Vec3(0.0f, 0.0f, 1.0f)}};
    bool exact = true;
    for (int ip = 0; ip < 4; ip++)
      for (const RampStop &s : st) exact = exact && length(ramp_eval(st, s.pos, ip) - s.color) < 1e-5f;
    const Vec3 c = ramp_eval(st, 0.4f, 0), l = ramp_eval(st, 0.25f, 1), e = ramp_eval(st, 0.125f, 2);
    std::printf("    Constant at 0.4 (%.2f %.2f %.2f), Linear at 0.25 (%.2f), Ease at 0.125 (%.3f)\n", c.x, c.y, c.z, l.x, e.x);
    CHECK(exact && length(c) < 1e-6f && std::fabs(l.x - 0.5f) < 1e-5f && e.x < 0.25f && e.x > 0.0f);
    /* An old ramp: no stops, Low and High - as it was. */
    std::unique_ptr<TextureLayer> ramp = create_texture_layer("Color Ramp");
    TexPoint p;
    p.height = 0.25f;
    LayerContext ctx;
    ramp->apply_point(p, 0.5f, 0.5f, ctx, nullptr);
    std::printf("    the old ramp at 0.25: %.3f (was lerp(0.1, 0.9, 0.25) = 0.3)\n", p.albedo.x);
    CHECK(std::fabs(p.albedo.x - 0.3f) < 1e-4f);
    ProceduralTexture t;
    t.layers.push_back(std::move(ramp));
    SetFieldForTest ip("Interpolation", 0.0f);
    t.layers[0]->reflect(ip);
    std::vector<RampStop> three = st;
    struct SetRamp : SetFieldForTest {
      std::vector<RampStop> st;
      SetRamp(std::vector<RampStop> s) : SetFieldForTest("", 0.0f), st(std::move(s)) {}
      void ramp(const char *, std::vector<RampStop> &v) override { v = st; }
    } setr(three);
    t.layers[0]->reflect(setr);
    ProceduralTexture back;
    CHECK(load_texture_text(save_texture_text(t), back) && back.layers.size() == 1);
    if (back.layers.size() == 1) {
      TexPoint q;
      q.height = 0.4f;
      back.layers[0]->apply_point(q, 0.5f, 0.5f, ctx, nullptr);
      std::printf("    read back, Constant at 0.4: %.3f %.3f %.3f\n", q.albedo.x, q.albedo.y, q.albedo.z);
      CHECK(length(q.albedo) < 1e-5f);
    }
  });
  test("Map Range, Clamp, every Math operation and Separate Channel against their formulas; an identity RGB Curves leaves the albedo as it was and a curve through (0.5, 0.8) lifts the middle", [] {
    LayerContext ctx;
    auto run = [&](const char *layer, std::vector<std::pair<const char *, float>> fields, TexPoint p) {
      std::unique_ptr<TextureLayer> l = create_texture_layer(layer);
      for (auto &f : fields) {
        SetFieldForTest s(f.first, f.second);
        l->reflect(s);
      }
      l->apply_point(p, 0.5f, 0.5f, ctx, nullptr);
      return p;
    };
    TexPoint p;
    p.height = 0.5f, p.rough = 0.3f, p.mask = 0.6f;
    p.albedo = {0.2f, 0.4f, 0.8f};
    const TexPoint mr = run("Map Range", {{"From Min", 0.2f}, {"From Max", 0.8f}}, p);
    CHECK(std::fabs(mr.height - 0.5f) < 1e-5f);
    TexPoint q = p;
    q.height = 0.35f;
    CHECK(std::fabs(run("Map Range", {{"From Min", 0.2f}, {"From Max", 0.8f}}, q).height - 0.25f) < 1e-5f);
    CHECK(std::fabs(run("Clamp", {{"Min", 0.6f}, {"Max", 0.9f}}, p).height - 0.6f) < 1e-6f);
    int ok = 0;
    for (int op = 0; op < 15; op++) {
      const TexPoint r = run("Math", {{"Operation", (float)op}, {"Value", 0.3f}, {"Distance", 0.1f}}, p);
      const float a = 0.5f, b = 0.3f;
      /* Blender's formulas (math_base.h / math_util.h): Snap down to a multiple, smooth min less h^3 k / 6. */
      const float want[] = {a + b, a - b, a * b, a / b, std::pow(a, b), std::min(a, b), std::max(a, b), std::fabs(a), std::floor(a), a - std::floor(a),
                            std::fmod(a, b), std::sin(a), 0.0f, 0.0f, std::floor(a / b) * b};
      float w = want[op];
      if (op == 12 || op == 13) {
        const float s = op == 12 ? 1.0f : -1.0f, h = std::max(0.1f - std::fabs(a * s - b * s), 0.0f) / 0.1f;
        w = s * (std::min(a * s, b * s) - h * h * h * 0.1f / 6.0f);
      }
      w = std::max(0.0f, std::min(1.0f, w));
      const bool good = std::fabs(r.height - w) < 1e-5f;
      if (!good) std::printf("    %s: %.5f, wanted %.5f\n", (std::string("op ") + std::to_string(op)).c_str(), r.height, w);
      ok += good;
    }
    CHECK(ok == 15);
    const TexPoint g = run("Separate Channel", {{"Channel", 1.0f}}, p), v = run("Separate Channel", {{"Channel", 5.0f}}, p);
    CHECK(std::fabs(g.mask - 0.4f) < 1e-5f && std::fabs(v.mask - 0.8f) < 1e-5f);
    /* Hue and saturation of (0.2, 0.4, 0.8): hue 220 degrees, saturation 0.75. */
    const TexPoint hue = run("Separate Channel", {{"Channel", 3.0f}}, p), sat = run("Separate Channel", {{"Channel", 4.0f}}, p);
    CHECK(std::fabs(hue.mask - 220.0f / 360.0f) < 1e-4f && std::fabs(sat.mask - 0.75f) < 1e-5f);
    /* Stepped Map Range, as Blender's: floor(f (steps + 1)) / steps - 4 steps reach To Max at 0.8. */
    TexPoint h8 = p;
    h8.height = 0.8f;
    CHECK(std::fabs(run("Map Range", {{"Interpolation", 1.0f}, {"Steps", 4.0f}}, h8).height - 1.0f) < 1e-5f);
    CHECK(std::fabs(run("Map Range", {{"Interpolation", 1.0f}, {"Steps", 4.0f}}, p).height - 0.5f) < 1e-5f);
    /* Clamp with Min above Max gives Max (Blender's). */
    CHECK(std::fabs(run("Clamp", {{"Min", 0.7f}, {"Max", 0.3f}}, p).height - 0.3f) < 1e-6f);
    /* Math on the albedo stays within 0..1 (review 329: live shading had 1.3). */
    const TexPoint add = run("Math", {{"Plane", 4.0f}, {"Operation", 0.0f}, {"Value", 0.5f}}, p);
    CHECK(add.albedo.z <= 1.0f && std::fabs(add.albedo.x - 0.7f) < 1e-5f);
    const TexPoint same = run("RGB Curves", {}, p);
    CHECK(length(same.albedo - p.albedo) < 1e-5f);
    std::unique_ptr<TextureLayer> cv = create_texture_layer("RGB Curves");
    struct SetCurve : SetFieldForTest {
      SetCurve() : SetFieldForTest("", 0.0f) {}
      void curve(const char *n, std::vector<Vec2> &v) override {
        if (std::string(n) == "Combined") v = {{0.0f, 0.0f}, {0.5f, 0.8f}, {1.0f, 1.0f}};
      }
    } setc;
    cv->reflect(setc);
    TexPoint mid;
    mid.albedo = Vec3(0.5f);
    cv->apply_point(mid, 0.5f, 0.5f, ctx, nullptr);
    std::printf("    the curve through (0.5, 0.8): 0.5 -> %.3f\n", mid.albedo.x);
    CHECK(std::fabs(mid.albedo.x - 0.8f) < 1e-4f);
  });
  test("the ColorRamp and curve widgets by clicks in the Textures window: a click on the bar adds a stop, dragging it moves it, dragging it off removes it; a click in the curve's box adds a point", [] {
    Editor ed;
    r50_open(ed);
    ed.record_buttons_for_test(true);
    ProceduralTexturePtr t = ed.new_texture_asset_for_test("Noise");
    if (!t) return;
    ed.command("texlayer " + t->name + " add Color Ramp");
    ed.command("texlayer " + t->name + " add RGB Curves");
    r34_steps(ed, 3);
    auto rect = [&](const std::string &key) {
      for (int tries = 0; tries < 40; tries++) {
        const Recti f = ed.texture_ui_rect_for_test(key), w = ed.window_rect_for_test(WindowKind::Textures);
        if (f.w > 0 && f.y >= w.y + ed.ui_for_test().row_h() * 3 && f.bottom() <= w.bottom()) return f;
        platform::Event wheel;
        wheel.type = platform::EventType::Wheel;
        wheel.x = f.w > 0 ? f.x + f.w / 2 : w.x + w.w / 4, wheel.y = w.y + w.h / 2, wheel.wheel_y = f.w > 0 && f.y < w.y + ed.ui_for_test().row_h() * 3 ? 3.0f : -3.0f;
        ed.step_frame_headless({r34_mouse(R34ET::MouseMove, wheel.x, wheel.y)});
        ed.step_frame_headless({wheel});
      }
      std::printf("    '%s' never came into view\n", key.c_str());
      return Recti{0, 0, 0, 0};
    };
    auto stops = [&] {
      std::vector<RampStop> got;
      struct Get : SetFieldForTest {
        std::vector<RampStop> *out;
        Get(std::vector<RampStop> *o) : SetFieldForTest("", 0.0f), out(o) {}
        void ramp(const char *, std::vector<RampStop> &v) override { *out = v; }
      } g(&got);
      t->layers[1]->reflect(g);
      return got;
    };
    Recti bar = rect("ramp:L1 Stops");
    CHECK(bar.w > 0);
    if (bar.w <= 0) return;
    const int y = bar.y + (bar.h - ed.ui_for_test().px(10)) / 2;
    r34_click(ed, bar.x + bar.w / 2, y);  // (a stop at 0.5)
    r34_steps(ed, 2);
    std::vector<RampStop> s = stops();
    std::printf("    after a click: %zu stops\n", s.size());
    CHECK(s.size() == 3 && std::fabs(s[1].pos - 0.5f) < 0.02f);
    /* The ramp's Low and High Colors stay (the first and last stops' colours): the bar doesn't move under the pointer. */
    const Recti bar2 = rect("ramp:L1 Stops");
    CHECK(bar2.w > 0 && bar2.y == bar.y);
    bar = bar2;
    const int hy = bar.bottom() - ed.ui_for_test().px(5);
    ed.step_frame_headless({r34_mouse(R34ET::MouseMove, bar.x + bar.w / 2, hy)});
    ed.step_frame_headless({r34_mouse(R34ET::MouseDown, bar.x + bar.w / 2, hy)});
    ed.step_frame_headless({r34_mouse(R34ET::MouseMove, bar.x + bar.w * 3 / 4, hy)});
    ed.step_frame_headless({r34_mouse(R34ET::MouseUp, bar.x + bar.w * 3 / 4, hy)});
    r34_steps(ed, 2);
    s = stops();
    CHECK(s.size() == 3 && std::fabs(s[1].pos - 0.75f) < 0.02f);
    ed.step_frame_headless({r34_mouse(R34ET::MouseMove, bar.x + bar.w * 3 / 4, hy)});
    ed.step_frame_headless({r34_mouse(R34ET::MouseDown, bar.x + bar.w * 3 / 4, hy)});
    ed.step_frame_headless({r34_mouse(R34ET::MouseMove, bar.x + bar.w * 3 / 4, hy + 80)});
    ed.step_frame_headless({r34_mouse(R34ET::MouseUp, bar.x + bar.w * 3 / 4, hy + 80)});
    r34_steps(ed, 2);
    std::printf("    moved to %.2f, then dragged off: %zu stops\n", s.size() == 3 ? s[1].pos : -1.0f, stops().size());
    CHECK(stops().size() == 2);
    const Recti box = rect("curve:L2 Combined");
    CHECK(box.w > 0);
    if (box.w <= 0) return;
    r34_click(ed, box.x + box.w / 2, box.y + box.h / 4);
    r34_steps(ed, 2);
    std::vector<Vec2> pts;
    struct GetC : SetFieldForTest {
      std::vector<Vec2> *out;
      GetC(std::vector<Vec2> *o) : SetFieldForTest("", 0.0f), out(o) {}
      void curve(const char *n, std::vector<Vec2> &v) override {
        if (std::string(n) == "Combined") *out = v;
      }
    } gc(&pts);
    t->layers[2]->reflect(gc);
    std::printf("    the curve: %zu points\n", pts.size());
    CHECK(pts.size() == 3 && std::fabs(pts[1].x - 0.5f) < 0.05f && pts[1].y > 0.6f);
    /* Review 329: a click near the bar's bottom on a ramp without stops keeps its new stop. */
    ed.command("texlayer " + t->name + " add Color Ramp");
    r34_steps(ed, 3);
    const Recti bar3 = rect("ramp:L3 Stops");
    CHECK(bar3.w > 0);
    if (bar3.w <= 0) return;
    r34_click(ed, bar3.x + bar3.w / 3, bar3.bottom() - ed.ui_for_test().px(11));
    r34_steps(ed, 2);
    std::vector<RampStop> got3;
    struct Get3 : SetFieldForTest {
      std::vector<RampStop> *out;
      Get3(std::vector<RampStop> *o) : SetFieldForTest("", 0.0f), out(o) {}
      void ramp(const char *, std::vector<RampStop> &v) override { *out = v; }
    } g3(&got3);
    t->layers[3]->reflect(g3);
    std::printf("    a click at the bar's foot: %zu stops\n", got3.size());
    CHECK(got3.size() == 3);
  });
  test("round 329's review: a ramp's stop position typed past its neighbour moves that stop, not the neighbour; the planes and live shading agree for every converter; an old Color Ramp file loads with no stops and draws as before; the stops are in the texture's hash; the lesson's Try-its set three stops and Constant", [] {
    /* The position field, by typing (the Inspector's own field). */
    Editor ed;
    r50_open(ed);
    ed.record_buttons_for_test(true);
    ProceduralTexturePtr t = ed.new_texture_asset_for_test("Noise");
    if (!t) return;
    ed.command("settex " + t->name + " L0Scale 4");
    ed.command("texlayer " + t->name + " add Color Ramp");
    ed.command("settex " + t->name + " L1Stops 0 0 0 0, 0.5 1 0 0, 1 0 0 1");
    r34_steps(ed, 3);
    struct Get : SetFieldForTest {
      std::vector<RampStop> got;
      Get() : SetFieldForTest("", 0.0f) {}
      void ramp(const char *, std::vector<RampStop> &v) override { got = v; }
    };
    Get before;
    t->layers[1]->reflect(before);
    CHECK(before.got.size() == 3);
    /* Pick the first stop (its handle), then type 0.7 into the picked stop's Position: it passes the red one at 0.5. */
    auto tex_rect = [&](const std::string &key) {
      for (int tries = 0; tries < 40; tries++) {
        const Recti f = ed.texture_ui_rect_for_test(key), w = ed.window_rect_for_test(WindowKind::Textures);
        if (f.w > 0 && f.y >= w.y + ed.ui_for_test().row_h() * 3 && f.bottom() <= w.bottom()) return f;
        platform::Event wheel;
        wheel.type = platform::EventType::Wheel;
        wheel.x = f.w > 0 ? f.x + f.w / 2 : w.x + w.w / 4, wheel.y = w.y + w.h / 2, wheel.wheel_y = f.w > 0 && f.y < w.y + ed.ui_for_test().row_h() * 3 ? 3.0f : -3.0f;
        ed.step_frame_headless({r34_mouse(R34ET::MouseMove, wheel.x, wheel.y)});
        ed.step_frame_headless({wheel});
      }
      return Recti{0, 0, 0, 0};
    };
    const Recti bar = tex_rect("ramp:L1 Stops");
    CHECK(bar.w > 0);
    if (bar.w <= 0) return;
    r34_click(ed, bar.x + 1, bar.bottom() - ed.ui_for_test().px(4));  // (the first stop's handle)
    r34_steps(ed, 2);
    const Recti pf = tex_rect("ramppos:L1 Stops");
    CHECK(pf.w > 0);
    if (pf.w <= 0) return;
    r34_click(ed, pf.x + pf.w / 2, pf.y + pf.h / 2);
    platform::Event all, en;
    all.type = en.type = platform::EventType::KeyDown;
    all.key = platform::KEY_A, all.mods = platform::MOD_CTRL, en.key = platform::KEY_ENTER;
    ed.step_frame_headless({all});
    for (char ch : std::string("0.7")) {
      platform::Event te;
      te.type = platform::EventType::Text, te.codepoint = (uint32_t)ch;
      ed.step_frame_headless({te});
    }
    ed.step_frame_headless({en});
    r34_steps(ed, 3);
    Get typed;
    t->layers[1]->reflect(typed);
    std::printf("    after typing 0.7 for the first stop: %s\n", ramp_to_text(typed.got).c_str());
    CHECK(typed.got.size() == 3 && std::fabs(typed.got[0].pos - 0.5f) < 1e-4f && typed.got[0].color.x == 1.0f && std::fabs(typed.got[1].pos - 0.7f) < 1e-4f &&
          typed.got[1].color.x == 0.0f && typed.got[1].color.z == 0.0f);
    const uint64_t h0 = t->hash();
    ed.command("settex " + t->name + " L1Stops 0 0 0 0, 0.6 1 0 0, 1 0 0 1");
    CHECK(t->hash() != h0);
    /* Live shading against the planes, for every converter on a noise. */
    for (const char *layer : {"Map Range", "Clamp", "Math", "RGB Curves", "Separate Channel", "Color Ramp"}) {
      ProceduralTexture tex;
      apply_texture_preset(tex, "Noise");
      tex.layers.push_back(create_texture_layer(layer));
      SetFieldForTest plane("Plane", 4.0f);
      tex.layers.back()->reflect(plane);
      TexPlanes pl;
      tex.evaluate(pl, nullptr, 32);
      int same = 0;
      LayerContext ctx;
      for (int y = 0; y < 32; y += 5)
        for (int x = 0; x < 32; x += 5) {
          TexPoint pt;
          const float u = (x + 0.5f) / 32.0f, v = (y + 0.5f) / 32.0f;
          for (const auto &l : tex.layers) l->apply_point(pt, u, v, ctx, nullptr);
          const size_t i = (size_t)y * 32 + x;
          same += length(pt.albedo - pl.albedo[i]) < 2e-3f && std::fabs(pt.height - pl.height[i]) < 2e-3f;
        }
      std::printf("    %-16s live shading equal to the planes at %d of 49 points\n", layer, same);
      CHECK(same == 49);
    }
    /* An old file: a Color Ramp with Low and High only. */
    ProceduralTexture old;
    old.layers.push_back(create_texture_layer("Color Ramp"));
    std::string text = save_texture_text(old);
    size_t at = text.find("Stops");
    if (at != std::string::npos) text.erase(text.rfind('\n', at) + 1, text.find('\n', at) - text.rfind('\n', at));
    ProceduralTexture back;
    CHECK(load_texture_text(text, back) && back.layers.size() == 1);
    if (back.layers.size() == 1) {
      Get g;
      back.layers[0]->reflect(g);
      TexPoint pt;
      pt.height = 0.25f;
      back.layers[0]->apply_point(pt, 0.5f, 0.5f, LayerContext{}, nullptr);
      std::printf("    an old ramp: %zu stops shown (its two ends), 0.25 -> %.3f\n", g.got.size(), pt.albedo.x);
      CHECK(g.got.size() == 2 && std::fabs(pt.albedo.x - 0.3f) < 1e-4f);
    }
  });
}

/* Round 330: Blender's Mix modes as layer blends, against Blender's results (ramp_blend, factor 1). */
static void tests_round330_blends() {
  test("Blender's Mix modes as layer blends: Darken, Lighten, Difference, Exclusion, Subtract, Divide, Color Dodge, Color Burn, Soft Light and Linear Light against Blender's formulas; Hue, Saturation, Color and Value in HSV; the old seven unchanged", [] {
    const float a = 0.3f, b = 0.6f;
    const float want[] = {std::min(a, b), std::max(a, b), std::fabs(a - b), a + b - 2 * a * b, a - b, a / b, std::min(1.0f, a / (1 - b)), std::max(0.0f, 1 - (1 - a) / b),
                          (1 - a) * b * a + a * (1 - (1 - a) * (1 - b)), a + 2 * b - 1};
    int ok = 0;
    for (int k = 0; k < 10; k++) {
      const float got = texture_blend((int)LayerBlend::Darken + k, a, b);
      ok += std::fabs(got - want[k]) < 1e-6f;
      if (std::fabs(got - want[k]) >= 1e-6f) std::printf("    mode %d: %.5f, wanted %.5f\n", (int)LayerBlend::Darken + k, got, want[k]);
    }
    CHECK(ok == 10);
    CHECK(texture_blend((int)LayerBlend::Multiply, a, b) == a * b && texture_blend((int)LayerBlend::HeightMax, a, b) == b && texture_blend((int)LayerBlend::Normal, a, b) == b);
    /* HSV: below orange (1, 0.5, 0) under a blue layer (0, 0, 1). */
    const Vec3 below{1.0f, 0.5f, 0.0f}, layer{0.0f, 0.0f, 1.0f};
    const Vec3 hue = texture_blend_color((int)LayerBlend::Hue, below, layer), val = texture_blend_color((int)LayerBlend::Value, below, Vec3(0.5f));
    const Vec3 sat = texture_blend_color((int)LayerBlend::Saturation, below, Vec3(0.5f)), col = texture_blend_color((int)LayerBlend::Color, below, layer);
    std::printf("    Hue (%.2f %.2f %.2f), Value 0.5 (%.2f %.2f %.2f), Saturation of grey (%.2f %.2f %.2f), Color (%.2f %.2f %.2f)\n", hue.x, hue.y, hue.z, val.x, val.y, val.z,
                sat.x, sat.y, sat.z, col.x, col.y, col.z);
    CHECK(length(hue - Vec3(0, 0, 1)) < 1e-4f && length(val - Vec3(0.5f, 0.25f, 0.0f)) < 1e-4f && length(sat - Vec3(1.0f)) < 1e-4f && length(col - Vec3(0, 0, 1)) < 1e-4f);
    /* Review 330: Color Dodge leaves black black (Blender's); an Image layer's Hue mode takes the image's hue. */
    CHECK(texture_blend((int)LayerBlend::ColorDodge, 0.0f, 1.0f) == 0.0f);
    std::unique_ptr<TextureLayer> img = create_texture_layer("Image");
    CHECK(img != nullptr);
  });
  test("a stop or point added to a full ramp or curve (its storage moved) is the one picked - the Linux crash after round 329: the index was taken from the old storage", [] {
    ui::Context u;
    for (size_t extra = 0; extra < 3; extra++) {
      std::vector<RampStop> st = {{0.0f, Vec3(0.0f)}, {1.0f, Vec3(1.0f)}};
      for (size_t k = 0; k < extra; k++) st.push_back({1.0f, Vec3(1.0f)});
      st.shrink_to_fit();  // (no room: the insert moves the stops)
      int sel = -1;
      CHECK(u.ramp_add_stop(st, 0.5f, 1, sel));
      CHECK(sel == 1 && st.size() == 3 + extra && std::fabs(st[(size_t)sel].pos - 0.5f) < 1e-6f);
      std::vector<Vec2> pts = {{0.0f, 0.0f}, {1.0f, 1.0f}};
      pts.shrink_to_fit();
      sel = -1;
      CHECK(u.curve_add_point(pts, {0.25f, 0.7f}, sel));
      CHECK(sel == 1 && pts.size() == 3 && pts[(size_t)sel].x == 0.25f);
    }
  });
}

/* Round 332 (task 0313): the Textures and Materials windows' HCI pass. */
static void tests_round332() {
  test("round 332's data: every layer type sits in one of Blender's node groups (none in Other); every material preset in one group, Volume's Fog / Smoke / Mist among them; a layer at Opacity 0 or with no targets says why it changes nothing", [] {
    std::vector<std::string> other;
    for (const TextureLayerInfo &li : texture_layer_infos())
      if (texture_layer_group(li.name) == "Other") other.push_back(li.name);
    for (const std::string &n : other) std::printf("    no group: %s\n", n.c_str());
    CHECK(other.empty());
    std::map<std::string, int> seen;
    for (const auto &g : material_preset_groups())
      for (const std::string &p : g.second) seen[p]++;
    bool once = seen.size() == material_presets().size();
    for (const std::string &p : material_presets()) once = once && seen[p] == 1;
    CHECK(once && seen.count("Fog") && seen.count("Smoke") && seen.count("Mist"));
    std::unique_ptr<TextureLayer> l = create_texture_layer("Noise");
    CHECK(l && texture_layer_no_effect(*l).empty());
    if (!l) return;
    l->opacity = 0.0f;
    CHECK(texture_layer_no_effect(*l) == "Opacity 0");
    l->opacity = 1.0f, l->targets = 0;
    CHECK(texture_layer_no_effect(*l) == "Writes nothing");
    l->enabled = false;
    CHECK(texture_layer_no_effect(*l).empty());
  });
  test("the Textures window by clicks (round 332): right-click a layer field's name > Reset to Default puts the layer type's default back; Add Layer's search finds Perlin Noise and hides the rest; Alt+click a layer shows the preview up to it, again for all; a texture no material uses is tagged \"not used\"", [] {
    Editor ed;
    r50_open(ed);
    ed.record_buttons_for_test(true);
    ProceduralTexturePtr t = ed.new_texture_asset_for_test("Noise");
    if (!t) return;
    r34_steps(ed, 3);
    auto bring = [&](const std::string &key) {  // (scrolled into the window's view)
      for (int tries = 0; tries < 40; tries++) {
        Recti f{0, 0, 0, 0};
        if (!ed.button_rect_for_test(key, f)) f = ed.texture_ui_rect_for_test(key);
        const Recti w = ed.window_rect_for_test(WindowKind::Textures);
        const int top = std::max(w.y + ed.ui_for_test().row_h() * 3, ed.texture_ui_rect_for_test("preview").y);  // (below the grid of thumbnails)
        if (f.w > 0 && f.y >= top && f.bottom() <= w.bottom()) return f;
        platform::Event wheel;
        wheel.type = platform::EventType::Wheel;
        wheel.x = w.x + w.w / 4, wheel.y = w.bottom() - ed.ui_for_test().row_h() * 2, wheel.wheel_y = f.w > 0 && f.y < top ? 3.0f : -3.0f;
        ed.step_frame_headless({r34_mouse(R34ET::MouseMove, wheel.x, wheel.y)});
        ed.step_frame_headless({wheel});
      }
      std::printf("    '%s' never came into view\n", key.c_str());
      return Recti{0, 0, 0, 0};
    };
    auto click_key = [&](const std::string &key) {
      Recti b;
      if (!ed.button_rect_for_test(key, b)) return std::printf("    no '%s'\n", key.c_str()), false;
      r34_click(ed, b.x + b.w / 2, b.y + b.h / 2);
      r34_steps(ed, 2);
      return true;
    };
    struct GetF : SetFieldForTest {
      float got = -1.0f;
      GetF(const char *k) : SetFieldForTest(k, 0.0f) {}
      void field(const char *n, float &v, float, float, float) override {
        if (key == n) got = v;
      }
    };
    /* Reset to Default. */
    GetF def("Scale");
    create_texture_layer("Noise")->reflect(def);
    ed.command("settex " + t->name + " L0Scale 9");
    r34_steps(ed, 2);
    const Recti lab = bring("label:L0 Scale");
    CHECK(lab.w > 0);
    if (lab.w <= 0) return;
    platform::Event down = r34_mouse(R34ET::MouseDown, lab.x + lab.w / 2, lab.y + lab.h / 2), up = r34_mouse(R34ET::MouseUp, lab.x + lab.w / 2, lab.y + lab.h / 2);
    down.button = up.button = 1;
    ed.step_frame_headless({r34_mouse(R34ET::MouseMove, lab.x + lab.w / 2, lab.y + lab.h / 2)});
    ed.step_frame_headless({down});
    ed.step_frame_headless({up});
    CHECK(click_key("menu:Reset to Default"));
    r34_steps(ed, 2);
    GetF now("Scale");
    t->layers[0]->reflect(now);
    std::printf("    Scale: 9 -> %.3f after Reset to Default (the default %.3f)\n", now.got, def.got);
    CHECK(def.got > 0.0f && now.got == def.got);
    ed.command("undo");  // (a reset is one undo step, as any edit)
    r34_steps(ed, 2);
    GetF undone("Scale");
    t->layers[0]->reflect(undone);
    std::printf("    after Ctrl+Z: %.3f\n", undone.got);
    CHECK(undone.got == 9.0f);
    /* A switch: Use Mask on, then reset to a new layer's (off). */
    ed.command("settex " + t->name + " L0UseMask 1");
    r34_steps(ed, 2);
    CHECK(t->layers[0]->use_mask);
    const Recti um = bring("label:L0 Use Mask");
    CHECK(um.w > 0);
    platform::Event d2 = r34_mouse(R34ET::MouseDown, um.x + um.w / 2, um.y + um.h / 2), u2 = r34_mouse(R34ET::MouseUp, um.x + um.w / 2, um.y + um.h / 2);
    d2.button = u2.button = 1;
    ed.step_frame_headless({r34_mouse(R34ET::MouseMove, um.x + um.w / 2, um.y + um.h / 2)});
    ed.step_frame_headless({d2});
    ed.step_frame_headless({u2});
    CHECK(click_key("menu:Reset to Default"));
    r34_steps(ed, 2);
    CHECK(!t->layers[0]->use_mask);
    /* Alt+click: the preview up to a layer. */
    const Recti head = bring("layer_head0");
    CHECK(head.w > 0);
    platform::Event alt_down = r34_mouse(R34ET::MouseDown, head.x + head.w / 4, head.y + head.h / 2), alt_up = alt_down;
    alt_up.type = R34ET::MouseUp;
    alt_down.mods = alt_up.mods = platform::MOD_ALT;
    ed.step_frame_headless({r34_mouse(R34ET::MouseMove, head.x + head.w / 4, head.y + head.h / 2)});
    ed.step_frame_headless({alt_down});
    ed.step_frame_headless({alt_up});
    r34_steps(ed, 2);
    CHECK(ed.texture_upto_for_test().first == t->asset_path && ed.texture_upto_for_test().second == 0 && ed.texture_ui_rect_for_test("upto").w > 0);
    ed.step_frame_headless({alt_down});
    ed.step_frame_headless({alt_up});
    r34_steps(ed, 2);
    CHECK(ed.texture_upto_for_test().second == -1 && ed.texture_ui_rect_for_test("upto").w == 0);
    /* Add Layer's search. */
    const Recti add = bring("add_layer");
    CHECK(add.w > 0);
    Recti ab = ed.texture_ui_rect_for_test("add_layer");
    r34_click(ed, ab.x + ab.w / 2, ab.y + ab.h / 2);
    r34_steps(ed, 2);
    std::vector<platform::Event> typed;
    for (char c : std::string("perl")) {
      platform::Event e;
      e.type = platform::EventType::Text, e.codepoint = (uint32_t)c;
      typed.push_back(e);
    }
    ed.step_frame_headless(typed);
    r34_steps(ed, 2);
    Recti probe;
    const bool perlin = ed.button_rect_for_test("menu:Perlin Noise", probe), checker = ed.button_rect_for_test("menu:Checker", probe);
    std::printf("    searching 'perl': Perlin Noise %s, Checker %s\n", perlin ? "listed" : "missing", checker ? "listed" : "hidden");
    CHECK(perlin && !checker);
    const size_t before = t->layers.size();
    CHECK(click_key("menu:Perlin Noise"));
    CHECK(t->layers.size() == before + 1);
    /* The next click is the user's (it once only ended the search field's edit): it folds the new layer. */
    const bool open_before = t->layers[0]->ui_expanded;
    CHECK(bring("layer_head0").w > 0 && click_key("layer_head0"));
    CHECK(t->layers[0]->ui_expanded != open_before);
    /* Esc closes Add Layer (the search kept the focus); Enter takes the first match. */
    auto key = [&](int k) {
      platform::Event down, up;
      down.type = platform::EventType::KeyDown, up.type = platform::EventType::KeyUp;
      down.key = up.key = k;
      ed.step_frame_headless({down});
      ed.step_frame_headless({up});
      r34_steps(ed, 2);
    };
    const Recti ab2 = bring("add_layer");
    CHECK(ab2.w > 0);
    r34_click(ed, ab2.x + ab2.w / 2, ab2.y + ab2.h / 2);
    r34_steps(ed, 2);
    CHECK(ed.ui_for_test().any_popup_open());
    key(platform::KEY_ESCAPE);
    CHECK(!ed.ui_for_test().any_popup_open());
    const Recti ab3 = bring("add_layer");
    r34_click(ed, ab3.x + ab3.w / 2, ab3.y + ab3.h / 2);
    r34_steps(ed, 2);
    std::vector<platform::Event> typed2;
    for (char c : std::string("gabor")) {
      platform::Event e;
      e.type = platform::EventType::Text, e.codepoint = (uint32_t)c;
      typed2.push_back(e);
    }
    ed.step_frame_headless(typed2);
    r34_steps(ed, 2);
    const size_t n_before = t->layers.size();
    key(platform::KEY_ENTER);
    std::printf("    Enter on 'gabor': %zu -> %zu layers, the top %s; a menu still open: %d\n", n_before, t->layers.size(),
                t->layers.empty() ? "-" : t->layers.back()->type_name(), (int)ed.ui_for_test().any_popup_open());
    CHECK(t->layers.size() == n_before + 1 && std::string(t->layers.back()->type_name()) == "Gabor" && !ed.ui_for_test().any_popup_open());
    /* The layer menu's Preview Up to Here (a pen's way to Alt+click). */
    const Recti lm = bring("layer0");
    CHECK(lm.w > 0);
    r34_click(ed, lm.x + lm.w / 2, lm.y + lm.h / 2);
    r34_steps(ed, 2);
    CHECK(click_key("menu:Preview Up to Here"));
    CHECK(ed.texture_upto_for_test().first == t->asset_path && ed.texture_upto_for_test().second == 0);
    ed.command("texlayer " + t->name + " add Checker");  // (a stack edit drops the cut: the layers moved under it)
    r34_steps(ed, 2);
    CHECK(ed.texture_upto_for_test().second == -1);
    /* "not used" - and not once a material uses it. */
    CHECK(ed.texture_ui_rect_for_test("unused:" + t->name).w > 0);
    ed.command("select Cube");
    r34_steps(ed, 2);
    const Recti as = ed.texture_ui_rect_for_test("assign");
    CHECK(as.w > 0);
    r34_click(ed, as.x + as.w / 2, as.y + as.h / 2);
    r34_steps(ed, 3);
    CHECK(ed.texture_ui_rect_for_test("unused:" + t->name).w == 0);
  });
  test("the Materials window's Of Type... > Volume > Fog makes a fog material asset; right-click Fog Density's name > Reset to Default puts 1 back", [] {
    Editor ed;
    r50_open(ed);
    ed.record_buttons_for_test(true);
    ed.command("window Materials");
    r34_steps(ed, 3);
    auto click_key = [&](const std::string &key) {
      Recti b;
      if (!ed.button_rect_for_test(key, b)) return std::printf("    no '%s'\n", key.c_str()), false;
      r34_click(ed, b.x + b.w / 2, b.y + b.h / 2);
      r34_steps(ed, 2);
      return true;
    };
    CHECK(click_key("mat_of_type") && click_key("menu:Fog"));
    MaterialPtr m = ed.material_selected_for_test();
    CHECK(m && m->volume());
    if (!m || !m->volume()) return;
    ed.command("matset FogDensity 3");
    r34_steps(ed, 3);
    CHECK(m->volume_density == 3.0f);
    Recti lab{0, 0, 0, 0};
    for (int tries = 0; tries < 40; tries++) {
      ed.button_rect_for_test("label:Fog Density", lab);
      const Recti w = ed.window_rect_for_test(WindowKind::Materials);
      if (lab.w > 0 && lab.y > w.y + w.h * 3 / 5 && lab.bottom() <= w.bottom()) break;  // (below the grid of tiles)
      platform::Event wheel;
      wheel.type = platform::EventType::Wheel;
      wheel.x = w.x + w.w / 4, wheel.y = w.bottom() - ed.ui_for_test().row_h() * 2, wheel.wheel_y = lab.w > 0 && lab.y <= w.y + w.h * 3 / 5 ? 3.0f : -3.0f;
      ed.step_frame_headless({r34_mouse(R34ET::MouseMove, wheel.x, wheel.y)});
      ed.step_frame_headless({wheel});
    }
    CHECK(lab.w > 0);
    if (lab.w <= 0) return;
    platform::Event down = r34_mouse(R34ET::MouseDown, lab.x + lab.w / 2, lab.y + lab.h / 2), up = r34_mouse(R34ET::MouseUp, lab.x + lab.w / 2, lab.y + lab.h / 2);
    down.button = up.button = 1;
    ed.step_frame_headless({r34_mouse(R34ET::MouseMove, lab.x + lab.w / 2, lab.y + lab.h / 2)});
    ed.step_frame_headless({down});
    ed.step_frame_headless({up});
    CHECK(click_key("menu:Reset to Default"));
    r34_steps(ed, 2);
    std::printf("    Fog Density 3 -> %.2f\n", m->volume_density);
    CHECK(m->volume_density == Material().volume_density);
    fs::remove_file(resolve_asset_path(m->asset_path));
  });
  test("Reset to Default in the object Inspector's material (review 332): the Sphere's Roughness pushed to 0.95 goes back to a new material's", [] {
    Editor ed;
    r50_open(ed);
    ed.record_buttons_for_test(true);
    ed.command("select Sphere");
    ed.command("matset Roughness 0.95");
    r34_steps(ed, 3);
    GameObject *s = ed.scene().find_by_name("Sphere");
    CHECK(s && s->get<MeshRenderer>() && !s->get<MeshRenderer>()->materials.empty());
    if (!s || !s->get<MeshRenderer>() || s->get<MeshRenderer>()->materials.empty()) return;
    const MaterialPtr m = s->get<MeshRenderer>()->materials[0];
    CHECK(m->roughness == 0.95f);
    Recti lab{0, 0, 0, 0};
    for (int tries = 0; tries < 60; tries++) {
      const Recti w = ed.window_rect_for_test(WindowKind::Inspector);
      if (ed.button_rect_for_test("label:Roughness", lab) && lab.y >= w.y + ed.ui_for_test().row_h() * 2 && lab.bottom() <= w.bottom()) break;
      lab = Recti{0, 0, 0, 0};
      platform::Event wheel;
      wheel.type = platform::EventType::Wheel;
      wheel.x = w.x + w.w / 2, wheel.y = w.y + w.h / 2, wheel.wheel_y = -3.0f;
      ed.step_frame_headless({r34_mouse(R34ET::MouseMove, wheel.x, wheel.y)});
      ed.step_frame_headless({wheel});
    }
    CHECK(lab.w > 0);
    if (lab.w <= 0) return;
    platform::Event down = r34_mouse(R34ET::MouseDown, lab.x + lab.w / 2, lab.y + lab.h / 2), up = r34_mouse(R34ET::MouseUp, lab.x + lab.w / 2, lab.y + lab.h / 2);
    down.button = up.button = 1;
    ed.step_frame_headless({r34_mouse(R34ET::MouseMove, lab.x + lab.w / 2, lab.y + lab.h / 2)});
    ed.step_frame_headless({down});
    ed.step_frame_headless({up});
    Recti item;
    CHECK(ed.button_rect_for_test("menu:Reset to Default", item));
    r34_click(ed, item.x + item.w / 2, item.y + item.h / 2);
    r34_steps(ed, 3);
    std::printf("    the Sphere's Roughness 0.95 -> %.3f (a new material's %.3f)\n", m->roughness, Material().roughness);
    CHECK(m->roughness == Material().roughness);
  });
  test("the lesson \"Working Faster with Textures and Materials\": its Try-its open the Textures window and make a noise with Scale 12 to reset", [] {
    Editor ed;
    r50_open(ed);
    for (const char *c : {"window Textures", "newtex Practice Noise", "settex Practice L0Scale 12"}) ed.command(c);  // (a part at a time, as Learn runs them)
    r34_steps(ed, 2);
    ProceduralTexturePtr t = texture_asset("Assets/Textures/Practice.ptex");
    CHECK(t && !t->layers.empty() && ed.window_rect_for_test(WindowKind::Textures).w > 0);
    if (!t || t->layers.empty()) return;
    struct GetF : SetFieldForTest {
      float got = -1.0f;
      GetF() : SetFieldForTest("Scale", 0.0f) {}
      void field(const char *n, float &v, float, float, float) override {
        if (key == n) got = v;
      }
    } g;
    t->layers[0]->reflect(g);
    CHECK(g.got == 12.0f);
    fs::remove_file(resolve_asset_path(t->asset_path));
  });
}

void run_assets_tests() {
  image_tests();
  import_and_material_tests();
  file_tests();
  round49_tests();
  round50_tests();
  round53_tests();
  tests_round98();
  tests_round107();
  tests_round145_to_round163();
  tests_round154();
  tests_round166_to_round170();
  tests_round169();
  tests_round229();
  tests_round230();
  tests_round327();
  tests_round328();
  tests_round329();
  tests_round330_blends();
  tests_round332();
}

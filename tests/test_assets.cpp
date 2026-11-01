// SPDX-License-Identifier: GPL-2.0-or-later
// blendity_tests: the assets tests (task 0177: one file a subsystem; the groups keep their rounds' order).
#include "test_common.h"
#include <chrono>
#include <thread>
#include <filesystem>
#include "fixtures/synthetic_mmd.h"
#include "../src/core/json.h"
#include "../src/scene/budget.h"
#include "../src/scene/visemes.h"
#include "../src/scene/humanoid.h"
#include "../src/scene/colliders.h"
#include "../src/scene/lod.h"
#include "../src/scene/audio.h"
#include "../src/scene/unity_package.h"
#include "../src/scene/unity_scene.h"
#include "../src/core/audio_out.h"
#include "../src/deps/deps.h"
#include "../src/core/launch.h"
#include "../src/scene/physics.h"
#include "../src/scene/avatar_merge.h"
#include "../src/scene/avatar_eyes.h"
#include "../src/scene/avatar_optimize.h"
#include "../src/scene/spring_bones.h"
#include "../src/scene/avatar_tools.h"
#include "../extern/ufbx/ufbx.h"

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
    ed.command("assist off walk");  // (round 367: on at first now - switched off to see the refusal)
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

/* ---------------------------------------------------------------- round 351: shape keys in glTF and FBX exports */
/* Round 351: a .glb's JSON chunk, parsed (Null when the header is wrong). */
static Json r351_glb_json(const std::string &glb) {
  auto u32 = [&](size_t at) {
    uint32_t v = 0;
    if (at + 4 <= glb.size()) std::memcpy(&v, glb.data() + at, 4);
    return v;
  };
  if (glb.size() < 20 || u32(0) != 0x46546C67u || u32(16) != 0x4E4F534Au || 20 + (size_t)u32(12) > glb.size()) return Json();
  JsonParser p{glb.data() + 20, glb.data() + 20 + u32(12)};
  Json j = p.value();
  return p.ok ? j : Json();
}

/* A cube whose top lifts by 0.5 ("Up", weight 0.25) and whose +X side pushes out by 0.2 ("Side", weight 0). */
static MeshPtr r351_keyed_cube() {
  MeshPtr m = std::make_shared<Mesh>(*primitives::cube());
  ShapeKey up{"Up", std::vector<Vec3>(m->vert_count(), Vec3(0.0f))}, side{"Side", std::vector<Vec3>(m->vert_count(), Vec3(0.0f))};
  for (size_t v = 0; v < m->vert_count(); v++) {
    if (m->positions[v].y > 0.0f) up.delta[v] = Vec3(0, 0.5f, 0);
    if (m->positions[v].x > 0.0f) side.delta[v] = Vec3(0.2f, 0, 0);
  }
  m->shape_keys = {up, side};
  return m;
}

static float r351_largest(const ShapeKey &k) {
  float best = 0.0f;
  for (const Vec3 &d : k.delta) best = std::max(best, length(d));
  return best;
}

/* Round 351: an accessor's floats from a .glb's binary chunk (empty when it isn't there). */
static std::vector<float> r351_glb_floats(const std::string &glb, const Json &j, size_t accessor) {
  uint32_t jl = 0;
  if (glb.size() >= 20) std::memcpy(&jl, glb.data() + 12, 4);
  const size_t bin = 20 + (size_t)jl + 8;
  const Json &a = j["accessors"][accessor];
  const Json &v = j["bufferViews"][(size_t)a["bufferView"].number()];
  const size_t start = bin + (size_t)v["byteOffset"].number() + (size_t)a["byteOffset"].number();
  const size_t comps = a["type"].str == "SCALAR" ? 1 : a["type"].str == "VEC2" ? 2 : a["type"].str == "VEC3" ? 3 : 4;
  const size_t n = (size_t)a["count"].number() * comps;
  std::vector<float> out(n);
  if (start + n * 4 > glb.size()) return {};
  std::memcpy(out.data(), glb.data() + start, n * 4);
  return out;
}

/* A warning containing `text` among the log's entries since `since`. */
static bool r351_warned(size_t since, const char *text) {
  std::vector<LogEntry> log;
  Log::fetch(since, log);
  for (const LogEntry &e : log)
    if (e.level == LogLevel::Warning && e.text.find(text) != std::string::npos) return true;
  return false;
}

static size_t r351_log_now() {
  std::vector<LogEntry> log;
  return Log::fetch(0, log);
}

static void tests_round351() {
  test("Shape keys in exports (round 351): glTF writes them as morph targets with names and weights, FBX as blend shapes; both import back", [] {
    MeshPtr cube = r351_keyed_cube();
    ExportItem it;
    it.name = "Face", it.mesh = cube.get(), it.world = Mat4::identity(), it.shape_weights = {0.25f, 0.0f};
    const std::string glb = export_gltf({it}, export_defaults((int)ExportFormat::GLB), true, "", nullptr);
    const Json j = r351_glb_json(glb);
    const Json &mesh = j["meshes"][0];
    const Json &prim = mesh["primitives"][0];
    std::printf("    glTF: %zu targets, names %s / %s, weights %g %g\n", prim["targets"].size(), mesh["extras"]["targetNames"][0].str.c_str(),
                mesh["extras"]["targetNames"][1].str.c_str(), mesh["weights"][0].number(-1), mesh["weights"][1].number(-1));
    CHECK(prim["targets"].size() == 2 && mesh["extras"]["targetNames"][0].str == "Up" && mesh["extras"]["targetNames"][1].str == "Side");
    CHECK(std::fabs(mesh["weights"][0].number(-1) - 0.25) < 1e-6 && mesh["weights"][1].number(-1) == 0.0);
    CHECK(prim["targets"][0]["POSITION"].type == Json::Num && j["accessors"][(size_t)prim["targets"][0]["POSITION"].number()]["max"].size() == 3);
    /* Back in, from the files. */
    const std::string dir = fs::join(test_dir(), "r351");
    fs::make_dirs(dir);
    const std::string gp = fs::join(dir, "face.glb"), fp = fs::join(dir, "face.fbx");
    fs::write_file(gp, glb);
    fs::write_file(fp, export_fbx({it}, export_defaults((int)ExportFormat::FBX)));
    for (const std::string &path : {gp, fp}) {
      ImportResult r;
      const bool ok = import_model(path, r);
      const ImportedNode *n = nullptr;
      for (const ImportedNode &x : r.nodes) n = n ? n : x.mesh ? &x : nullptr;
      const std::vector<ShapeKey> keys = n ? n->mesh->shape_keys : std::vector<ShapeKey>();
      std::printf("    %s back: %zu keys%s\n", fs::extension(path).c_str(), keys.size(),
                  keys.size() == 2 ? strprintf(" (%s %.3f, %s %.3f)", keys[0].name.c_str(), r351_largest(keys[0]), keys[1].name.c_str(), r351_largest(keys[1])).c_str() : "");
      CHECK(ok && keys.size() == 2);
      if (keys.size() != 2) continue;
      CHECK(keys[0].name == "Up" && keys[1].name == "Side");
      CHECK(std::fabs(r351_largest(keys[0]) - 0.5f) < 1e-3f && std::fabs(r351_largest(keys[1]) - 0.2f) < 1e-3f);
    }
  });
  test("Shape keys in exports (round 351): the Shape Keys option off writes the mesh as its weights shape it; a key that moves nothing still survives FBX; a clip's weights go as a glTF weights channel", [] {
    MeshPtr cube = r351_keyed_cube();
    cube->shape_keys[1].delta.assign(cube->vert_count(), Vec3(0.0f));  // (Side moves nothing now)
    ExportItem it;
    it.name = "Face", it.mesh = cube.get(), it.world = Mat4::identity(), it.shape_weights = {1.0f, 0.0f};
    ExportOptions off = export_defaults((int)ExportFormat::GLB);
    off.shape_keys = false;
    CHECK(r351_glb_json(export_gltf({it}, off, true, "", nullptr))["meshes"][0]["primitives"][0]["targets"].size() == 0);
    const std::string dir = fs::join(test_dir(), "r351");
    fs::make_dirs(dir);
    const std::string fp = fs::join(dir, "still.fbx");
    fs::write_file(fp, export_fbx({it}, export_defaults((int)ExportFormat::FBX)));
    ImportResult r;
    import_model(fp, r);
    size_t keys = 0;
    for (const ImportedNode &n : r.nodes) keys += n.mesh ? n.mesh->shape_keys.size() : 0;
    std::printf("    FBX with a key that moves nothing: %zu keys back\n", keys);
    CHECK(keys == 2);
    /* A skinned item whose clip keys the weights: one weights channel on the mesh's node, keys x frames values. */
    auto sk = std::make_shared<ExportSkin>();
    sk->joints.push_back({"Root", -1, Vec3(0.0f), Quat(), Vec3(1.0f)});
    sk->vjoints.assign(cube->vert_count(), {0, 0, 0, 0});
    sk->vweights.assign(cube->vert_count(), {1.0f, 0.0f, 0.0f, 0.0f});
    ExportSkin::Anim an;
    an.name = "Talk", an.fps = 30.0f, an.frames = 3;
    an.t = {{Vec3(0.0f), Vec3(0.0f), Vec3(0.0f)}}, an.r = {{Quat(), Quat(), Quat()}}, an.s = {{Vec3(1.0f), Vec3(1.0f), Vec3(1.0f)}};
    an.shape_w = {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.5f}};
    sk->anims.push_back(an);
    it.skin = sk;
    const Json j = r351_glb_json(export_gltf({it}, export_defaults((int)ExportFormat::GLB), true, "", nullptr));
    const Json &chs = j["animations"][0]["channels"];
    int weights = -1;
    for (size_t c = 0; c < chs.size(); c++)
      if (chs[c]["target"]["path"].str == "weights") weights = (int)c;
    const Json &smp = weights >= 0 ? j["animations"][0]["samplers"][(size_t)chs[(size_t)weights]["sampler"].number()] : Json();
    const double count = weights >= 0 ? j["accessors"][(size_t)smp["output"].number()]["count"].number() : 0;
    std::printf("    the clip's weights channel: %s, %g values\n", weights >= 0 ? "on the mesh's node" : "missing", count);
    CHECK(weights >= 0 && j["nodes"][(size_t)chs[(size_t)weights]["target"]["node"].number()]["mesh"].type == Json::Num && count == 6);
  });
  test("Shape keys in exports (round 351): from the editor the basis and its keys go out; a Subdivision Surface on the mesh leaves them out and says why", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("create Cube");
    GameObject *g = ed.active_object_for_test();
    CHECK(g != nullptr);
    if (!g) return;
    g->get<MeshFilter>()->mesh = r351_keyed_cube();
    g->get<MeshRenderer>()->shape_weights = {{"Up", 1.0f}};
    const std::string dir = fs::join(test_dir(), "r351");
    fs::make_dirs(dir);
    const std::string path = fs::join(dir, "editor.glb");
    ed.command("export glb " + path);
    std::string glb;
    fs::read_file(path, glb);
    const Json j = r351_glb_json(glb);
    const Json &acc = j["accessors"][(size_t)j["meshes"][0]["primitives"][0]["attributes"]["POSITION"].number()];
    std::printf("    from the editor: %zu targets, the basis' top at y %g, its middle at %g (the weight 1 not baked in)\n", j["meshes"][0]["primitives"][0]["targets"].size(),
                acc["max"][1].number(), g->world_position().y);
    CHECK(j["meshes"][0]["primitives"][0]["targets"].size() == 2 && j["meshes"][0]["weights"][0].number(-1) == 1.0);
    CHECK(acc["max"][1].number() < g->world_position().y + 0.6);  // (the cube's top half a metre above its middle, not lifted another 0.5 by the weight)
    ed.command("component SubdivisionSurface");
    std::vector<LogEntry> log;
    const size_t since = Log::fetch(0, log);
    ed.command("export glb " + path);
    log.clear();
    Log::fetch(since, log);
    bool said = false;
    for (const LogEntry &e : log) said = said || (e.level == LogLevel::Warning && e.text.find("shape keys left out") != std::string::npos);
    glb.clear();
    fs::read_file(path, glb);
    CHECK(r351_glb_json(glb)["meshes"][0]["primitives"][0]["targets"].size() == 0 && said);
  });
  test("Shape keys in exports (review 351): through a turned, scaled and mirrored object each key's offset lands where the vertex does, signed, in glTF and FBX; a clip's weights go frame by frame", [] {
    MeshPtr cube = r351_keyed_cube();
    ExportItem it;
    it.name = "Turned", it.mesh = cube.get(), it.shape_weights = {0.0f, 0.0f};
    it.world = Mat4::trs(Vec3(1, 2, 3), Quat::axis_angle(Vec3(0, 1, 0), kPi * 0.5f), Vec3(-2, 1, 1));  // (mirrored in X, stretched, turned)
    const std::string dir = fs::join(test_dir(), "r351");
    fs::make_dirs(dir);
    const std::string gp = fs::join(dir, "turned.glb"), fp = fs::join(dir, "turned.fbx");
    fs::write_file(gp, export_gltf({it}, export_defaults((int)ExportFormat::GLB), true, "", nullptr));
    fs::write_file(fp, export_fbx({it}, export_defaults((int)ExportFormat::FBX)));
    for (const std::string &path : {gp, fp}) {
      ImportResult r;
      import_model(path, r);
      const ImportedNode *n = nullptr;
      for (const ImportedNode &x : r.nodes) n = n ? n : x.mesh ? &x : nullptr;
      CHECK(n && n->mesh->shape_keys.size() == 2);
      if (!n || n->mesh->shape_keys.size() != 2) continue;
      const Mat4 nodew = Mat4::trs(n->position, n->rotation, n->scale);  // (the imported node's own placement, if the file has one)
      float worst = 0.0f;
      for (size_t v = 0; v < cube->vert_count(); v++) {
        const Vec3 want_p = it.world.point(cube->positions[v]), want_d = it.world.dir(cube->shape_keys[0].delta[v]);
        size_t best = 0;
        float bd = 1e30f;
        for (size_t u = 0; u < n->mesh->vert_count(); u++) {
          const float d = length(nodew.point(n->mesh->positions[u]) - want_p);
          if (d < bd) bd = d, best = u;
        }
        worst = std::max(worst, length(nodew.dir(n->mesh->shape_keys[0].delta[best]) - want_d) + bd);
      }
      std::printf("    %s: the Up key's offsets against the turned, mirrored object's, worst %.5f\n", fs::extension(path).c_str(), worst);
      CHECK(worst < 1e-3f);
    }
  });
  test("Shape keys in exports (review 351): the weights channel's values go frame-major (each frame every key)", [] {
    MeshPtr cube = r351_keyed_cube();
    ExportItem it;
    it.name = "Face", it.mesh = cube.get(), it.world = Mat4::identity(), it.shape_weights = {0.0f, 0.0f};
    auto sk = std::make_shared<ExportSkin>();
    sk->joints.push_back({"Root", -1, Vec3(0.0f), Quat(), Vec3(1.0f)});
    sk->vjoints.assign(cube->vert_count(), {0, 0, 0, 0});
    sk->vweights.assign(cube->vert_count(), {1.0f, 0.0f, 0.0f, 0.0f});
    ExportSkin::Anim an;
    an.name = "Talk", an.fps = 30.0f, an.frames = 3;
    an.t = {{Vec3(0.0f), Vec3(0.0f), Vec3(0.0f)}}, an.r = {{Quat(), Quat(), Quat()}}, an.s = {{Vec3(1.0f), Vec3(1.0f), Vec3(1.0f)}};
    an.shape_w = {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.5f}};
    sk->anims.push_back(an);
    it.skin = sk;
    const std::string glb = export_gltf({it}, export_defaults((int)ExportFormat::GLB), true, "", nullptr);
    const Json j = r351_glb_json(glb);
    const Json &chs = j["animations"][0]["channels"];
    std::vector<float> vals;
    for (size_t c = 0; c < chs.size(); c++)
      if (chs[c]["target"]["path"].str == "weights") vals = r351_glb_floats(glb, j, (size_t)j["animations"][0]["samplers"][(size_t)chs[c]["sampler"].number()]["output"].number());
    std::printf("    the weights: %s\n", vals.size() == 6 ? strprintf("%g %g | %g %g | %g %g", vals[0], vals[1], vals[2], vals[3], vals[4], vals[5]).c_str() : "missing");
    CHECK(vals.size() == 6 && vals[0] == 0.0f && vals[1] == 0.0f && vals[2] == 1.0f && vals[3] == 0.0f && vals[4] == 0.0f && vals[5] == 0.5f);
  });
  test("Shape keys in exports (review 351): a rigged cube whose clip keys Smile exports the keys, the weight and the channel, and the Inspector's weight is put back; with Armatures off the posed mesh goes and the log says why", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.step_frame_headless();
    ed.command("select Cube");
    ed.command("shape add Smile");
    ed.command("create Armature");  // (the Cube in "Cube Rig", a Skin bound)
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    GameObject *rig = ed.scene().find_by_name("Cube Rig"), *cube = ed.scene().find_by_name("Cube");
    CHECK(rig && cube && cube->get<SkinModifier>());
    if (!rig || !cube) return;
    ed.select_for_test(rig->id, false);
    ed.command("anim new R351");
    ed.command("anim key Cube MeshRenderer.BlendShape/Smile 0 0 0");
    ed.command("anim key Cube MeshRenderer.BlendShape/Smile 0 10 1");
    ed.command("anim frame 5");
    ed.command("anim preview off");
    cube->get<MeshRenderer>()->shape_weights = {{"Smile", 0.3f}};
    ed.command("select Cube");
    const std::string path = fs::join(fs::join(test_dir(), "r351"), "rig.glb");
    fs::make_dirs(fs::parent(path));
    const size_t since = r351_log_now();
    ed.export_model_for_test(export_defaults((int)ExportFormat::GLB), path);
    std::string glb;
    fs::read_file(path, glb);
    const Json j = r351_glb_json(glb);
    bool channel = false;
    for (size_t c = 0; c < j["animations"][0]["channels"].size(); c++) channel = channel || j["animations"][0]["channels"][c]["target"]["path"].str == "weights";
    const float back = cube->get<MeshRenderer>()->shape_weight("Smile");
    std::printf("    rigged: targets %zu, weight %g, weights channel %d, the Inspector's weight after %g, warned %d\n", j["meshes"][0]["primitives"][0]["targets"].size(),
                j["meshes"][0]["weights"][0].number(-1), (int)channel, back, (int)r351_warned(since, "shape keys left out"));
    CHECK(j["meshes"][0]["primitives"][0]["targets"].size() == 1 && std::fabs(j["meshes"][0]["weights"][0].number(-1) - 0.3) < 1e-6 && channel);
    CHECK(std::fabs(back - 0.3f) < 1e-6f && !r351_warned(since, "shape keys left out"));
    /* Armatures and Animation off: the mesh as posed now, its keys applied, and the log says why they're not in the file. */
    ExportOptions off = export_defaults((int)ExportFormat::GLB);
    off.skins = false;
    const size_t since2 = r351_log_now();
    ed.export_model_for_test(off, path);
    glb.clear();
    fs::read_file(path, glb);
    CHECK(r351_glb_json(glb)["meshes"][0]["primitives"][0]["targets"].size() == 0 && r351_warned(since2, "posed by its Skin"));
  });
  test("Shape keys in exports (review 351): with Shape Keys off the weights shape the mesh; an outline carries the keys; Smooth Normals keeps them", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("create Cube");
    GameObject *g = ed.active_object_for_test();
    CHECK(g != nullptr);
    if (!g) return;
    g->get<MeshFilter>()->mesh = r351_keyed_cube();
    g->get<MeshRenderer>()->shape_weights = {{"Up", 1.0f}};
    const std::string path = fs::join(fs::join(test_dir(), "r351"), "opts.glb");
    fs::make_dirs(fs::parent(path));
    ExportOptions off = export_defaults((int)ExportFormat::GLB);
    off.shape_keys = false;
    ed.export_model_for_test(off, path);
    std::string glb;
    fs::read_file(path, glb);
    Json j = r351_glb_json(glb);
    const double top = j["accessors"][(size_t)j["meshes"][0]["primitives"][0]["attributes"]["POSITION"].number()]["max"][1].number();
    std::printf("    Shape Keys off: no targets %d, the top at %g (lifted: the weight applied; the middle at %g)\n", (int)(j["meshes"][0]["primitives"][0]["targets"].size() == 0), top,
                g->world_position().y);
    CHECK(j["meshes"][0]["primitives"][0]["targets"].size() == 0 && top > g->world_position().y + 0.9);
    ed.command("component Outline");
    ed.command("component SmoothNormalsModifier");
    const size_t since = r351_log_now();
    ed.export_model_for_test(export_defaults((int)ExportFormat::GLB), path);
    glb.clear();
    fs::read_file(path, glb);
    j = r351_glb_json(glb);
    size_t keyed = 0;
    for (size_t k = 0; k < j["meshes"].size(); k++) keyed += j["meshes"][k]["primitives"][0]["targets"].size() == 2 && j["meshes"][k]["weights"][0].number(-1) == 1.0;
    std::printf("    with an Outline and Smooth Normals: %zu of %zu meshes keyed, warned %d\n", keyed, j["meshes"].size(), (int)r351_warned(since, "shape keys left out"));
    CHECK(j["meshes"].size() == 2 && keyed == 2 && !r351_warned(since, "shape keys left out"));
  });
  test("the lesson's Try-it (round 351): create Cube, shape add Smile, export glb gives a file with a morph target", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    const std::string path = fs::join(fs::join(test_dir(), "r351"), "tryit.glb");
    fs::make_dirs(fs::parent(path));
    for (const std::string &c : {std::string("create Cube"), std::string("shape add Smile"), "export glb " + path}) ed.command(c);
    std::string glb;
    fs::read_file(path, glb);
    CHECK(r351_glb_json(glb)["meshes"][0]["extras"]["targetNames"][0].str == "Smile");
  });
}

/* ---------------------------------------------------------------- round 352: FBX carries the rig and its clip */
/* A two-bone rig on the keyed cube: Root at the bottom, Child half way up taking the top half, its clip turning Child
 * 90 degrees about Z over three frames while Up's weight rises. */
static ExportItem r352_rigged(MeshPtr &cube, const Mat4 &world) {
  cube = r351_keyed_cube();
  ExportItem it;
  it.name = "Body", it.mesh = cube.get(), it.world = world, it.shape_weights = {0.0f, 0.0f};
  auto sk = std::make_shared<ExportSkin>();
  sk->base_name = "Rig";
  sk->base = Mat4::trs(Vec3(0.0f, -0.5f, 0.0f), Quat(), Vec3(1.0f));
  sk->joints.push_back({"Root", -1, Vec3(0.0f), Quat(), Vec3(1.0f)});
  sk->joints.push_back({"Child", 0, Vec3(0.0f, 0.5f, 0.0f), Quat(), Vec3(1.0f)});
  for (size_t v = 0; v < cube->vert_count(); v++) {
    const bool top = cube->positions[v].y > 0.0f;
    sk->vjoints.push_back({(uint16_t)(top ? 1 : 0), 0, 0, 0});
    sk->vweights.push_back({1.0f, 0.0f, 0.0f, 0.0f});
  }
  ExportSkin::Anim an;
  an.name = "Bend", an.fps = 24.0f, an.frames = 3;
  an.t = {{Vec3(0.0f), Vec3(0.0f), Vec3(0.0f)}, {Vec3(0, 0.5f, 0), Vec3(0, 0.5f, 0), Vec3(0, 0.5f, 0)}};
  an.r = {{Quat(), Quat(), Quat()}, {Quat(), Quat::axis_angle(Vec3(0, 0, 1), kPi * 0.25f), Quat::axis_angle(Vec3(0, 0, 1), kPi * 0.5f)}};
  an.s = {{Vec3(1.0f), Vec3(1.0f), Vec3(1.0f)}, {Vec3(1.0f), Vec3(1.0f), Vec3(1.0f)}};
  an.shape_w = {{0.0f, 0.5f, 1.0f}, {0.0f, 0.0f, 0.0f}};
  sk->anims.push_back(an);
  it.skin = sk;
  return it;
}

/* What an import made of a rig: each bone's rest world position, the mesh's group weights, Child's Z turn at the clip's
 * last frame and Up's weight there. */
struct R352Rig {
  bool ok = false;
  std::vector<std::pair<std::string, Vec3>> bones;
  std::vector<std::pair<std::string, float>> group_sum;
  float child_turn = -999.0f, up_weight = -1.0f;
  size_t skins = 0, clips = 0;
};

static R352Rig r352_read(const std::string &path) {
  R352Rig r;
  ImportResult res;
  r.ok = import_model(path, res);
  r.skins = res.skins.size(), r.clips = res.clips.size();
  std::vector<Mat4> world(res.nodes.size());
  for (size_t i = 0; i < res.nodes.size(); i++) {
    const ImportedNode &n = res.nodes[i];
    const Mat4 local = Mat4::trs(n.position, n.rotation, n.scale);
    world[i] = n.parent >= 0 && n.parent < (int)i ? world[(size_t)n.parent] * local : local;
    if (n.is_bone) r.bones.push_back({n.name, world[i].translation()});
    if (!n.mesh) continue;
    for (const VertexGroup &vg : n.mesh->vertex_groups) {
      float s = 0.0f;
      for (float w : vg.weights) s += w;
      r.group_sum.push_back({vg.name, s});
    }
  }
  for (const AnimationClip &c : res.clips)
    for (const AnimTrack &t : c.tracks) {
      const float last = t.keys.empty() ? 0.0f : t.keys.back().frame;
      if (t.path.size() >= 5 && t.path.compare(t.path.size() - 5, 5, "Child") == 0 && t.property == "Rotation" && t.axis == 2) r.child_turn = t.sample(last);
      if (t.property.find("BlendShape/Up") != std::string::npos) r.up_weight = t.sample(last);
    }
  std::sort(r.bones.begin(), r.bones.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
  std::sort(r.group_sum.begin(), r.group_sum.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
  return r;
}

/* Review 352: ufbx's own skinning of an FBX's first mesh at `time` (seconds), in Blendity's space (X mirrored back),
 * by control point - which is the exported mesh's vertex order. */
static std::vector<Vec3> r352_skinned(const std::string &path, double time) {
  ufbx_load_opts opts = {};
  opts.target_axes = ufbx_axes_right_handed_y_up;
  opts.target_unit_meters = 1.0f;
  opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
  ufbx_error err;
  ufbx_scene *scene = ufbx_load_file(path.c_str(), &opts, &err);
  std::vector<Vec3> out;
  if (!scene) return out;
  ufbx_evaluate_opts eo = {};
  eo.evaluate_skinning = true;
  ufbx_scene *ev = scene->anim_stacks.count ? ufbx_evaluate_scene(scene, scene->anim_stacks.data[0]->anim, time, &eo, &err) : nullptr;
  const ufbx_scene *use = ev ? ev : scene;
  const ufbx_node *node = nullptr;
  for (size_t i = 0; i < use->nodes.count && !node; i++) node = use->nodes.data[i]->mesh ? use->nodes.data[i] : nullptr;
  if (node) {
    const ufbx_mesh *m = node->mesh;
    for (size_t v = 0; v < m->num_vertices; v++) {
      ufbx_vec3 p = m->skinned_position.values.data[v];
      if (m->skinned_is_local) p = ufbx_transform_position(&node->geometry_to_world, p);
      out.push_back(Vec3(-(float)p.x, (float)p.y, (float)p.z));
    }
  }
  if (ev) ufbx_free_scene(ev);
  ufbx_free_scene(scene);
  return out;
}

struct R352Names : Reflector {  // (the option labels a dialog shows)
  std::vector<std::string> names;
  void field(const char *n, float &, float, float, float) override { names.push_back(n); }
  void field(const char *n, int &, int, int) override { names.push_back(n); }
  void field(const char *n, bool &) override { names.push_back(n); }
  void field(const char *n, Vec3 &) override { names.push_back(n); }
  void color(const char *n, Vec3 &) override { names.push_back(n); }
  void enumeration(const char *n, int &, const char *const *, int) override { names.push_back(n); }
  void text(const char *n, std::string &) override { names.push_back(n); }
  void mesh(const char *n, MeshPtr &) override { names.push_back(n); }
};

static void tests_round352() {
  test("FBX carries the rig (round 352): an FBX of a rigged, keyed mesh imports with the same bones, weights, clip turn and shape-key weight as its glTF", [] {
    MeshPtr cube;
    const Mat4 world = Mat4::trs(Vec3(0.5f, 0.0f, -1.0f), Quat::axis_angle(Vec3(0, 1, 0), 0.6f), Vec3(1.0f));
    const ExportItem it = r352_rigged(cube, world);
    const std::string dir = fs::join(test_dir(), "r352");
    fs::make_dirs(dir);
    const std::string gp = fs::join(dir, "rig.glb"), fp = fs::join(dir, "rig.fbx");
    fs::write_file(gp, export_gltf({it}, export_defaults((int)ExportFormat::GLB), true, "", nullptr));
    fs::write_file(fp, export_fbx({it}, export_defaults((int)ExportFormat::FBX)));
    const R352Rig g = r352_read(gp), f = r352_read(fp);
    auto bones = [](const R352Rig &r) {
      std::string s;
      for (const auto &b : r.bones) s += strprintf("%s (%.2f %.2f %.2f) ", b.first.c_str(), b.second.x, b.second.y, b.second.z);
      return s;
    };
    std::printf("    glTF: %zu skin(s), bones %s, Child's turn %.1f, Up %.2f, %zu clip(s)\n", g.skins, bones(g).c_str(), g.child_turn, g.up_weight, g.clips);
    std::printf("    FBX:  %zu skin(s), bones %s, Child's turn %.1f, Up %.2f, %zu clip(s)\n", f.skins, bones(f).c_str(), f.child_turn, f.up_weight, f.clips);
    CHECK(f.ok && f.skins == 1 && f.clips >= 1 && f.bones.size() == 2 && g.bones.size() == 2);
    if (f.bones.size() != 2 || g.bones.size() != 2) return;
    for (size_t b = 0; b < 2; b++) CHECK(f.bones[b].first == g.bones[b].first && length(f.bones[b].second - g.bones[b].second) < 1e-3f);
    CHECK(f.group_sum.size() == g.group_sum.size());
    for (size_t k = 0; k < f.group_sum.size() && k < g.group_sum.size(); k++)
      CHECK(f.group_sum[k].first == g.group_sum[k].first && std::fabs(f.group_sum[k].second - g.group_sum[k].second) < 1e-3f);
    CHECK(std::fabs(f.child_turn - g.child_turn) < 0.5f && std::fabs(std::fabs(f.child_turn) - 90.0f) < 0.5f);
    CHECK(std::fabs(f.up_weight - 1.0f) < 1e-3f);
  });
  test("FBX carries the rig (round 352): from the editor a Cube with an Armature and a clip goes to FBX with its skin and its clip; Armatures and Animation is offered for FBX", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.step_frame_headless();
    ed.command("select Cube");
    ed.command("create Armature");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    GameObject *rig = ed.scene().find_by_name("Cube Rig");
    CHECK(rig != nullptr);
    if (!rig) return;
    ed.select_for_test(rig->id, false);
    ed.command("anim new R352");
    ed.command("anim key Bone Rotation 2 0 0");
    ed.command("anim key Bone Rotation 2 30 45");
    ed.command("anim preview off");
    ed.command("select Cube");
    const std::string path = fs::join(fs::join(test_dir(), "r352"), "editor.fbx");
    fs::make_dirs(fs::parent(path));
    ed.export_model_for_test(export_defaults((int)ExportFormat::FBX), path);
    const R352Rig f = r352_read(path);
    size_t bone = 0;
    for (const auto &b : f.bones) bone += b.first == "Bone";
    std::printf("    the editor's FBX: %zu skin(s), the Bone %zu, %zu clip(s)\n", f.skins, bone, f.clips);
    CHECK(f.ok && f.skins == 1 && bone == 1 && f.clips == 1);
    bool offered = false;
    ExportOptions o = export_defaults((int)ExportFormat::FBX);
    R352Names names;
    o.reflect(names);
    for (const std::string &n : names.names) offered = offered || n == "Armatures and Animation";
    CHECK(offered);
  });
  test("FBX carries the rig (review 352): ufbx's own skinning of the editor's exported rig at a keyed frame puts every vertex where Blendity's Skin puts it (the Cluster matrices), in binary and ASCII", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.step_frame_headless();
    ed.command("select Cube");
    ed.command("position 0.5 0.5 -1");
    ed.command("create Armature");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    GameObject *rig = ed.scene().find_by_name("Cube Rig"), *cube = ed.scene().find_by_name("Cube");
    CHECK(rig && cube);
    if (!rig || !cube) return;
    ed.select_for_test(rig->id, false);
    ed.command("anim new R352C");
    ed.command("anim key Bone Rotation 2 0 0");
    ed.command("anim key Bone Rotation 2 30 50");
    ed.command("anim key Bone Rotation 0 30 20");
    ed.command("anim preview on");
    ed.command("anim frame 30");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    const AnimationClipPtr clip = ed.anim_clip_for_test();
    const Mesh *em = cube->evaluated_mesh();
    CHECK(clip && em);
    if (!clip || !em) return;
    std::vector<Vec3> want;  // (Blendity's own Skin at frame 30, in the world)
    for (const Vec3 &p : em->positions) want.push_back(cube->world_matrix().point(p));
    ed.command("select Cube");
    const std::string dir = fs::join(test_dir(), "r352");
    fs::make_dirs(dir);
    const std::string bp = fs::join(dir, "skinned.fbx"), ap = fs::join(dir, "skinned_ascii.fbx");
    ExportOptions o = export_defaults((int)ExportFormat::FBX);
    ed.export_model_for_test(o, bp);
    o.ascii = true;
    ed.export_model_for_test(o, ap);
    for (const std::string &path : {bp, ap}) {
      const std::vector<Vec3> got = r352_skinned(path, 30.0 / clip->frame_rate);
      float worst = got.size() == want.size() ? 0.0f : 1e9f;
      for (size_t v = 0; v < got.size() && v < want.size(); v++) worst = std::max(worst, length(got[v] - want[v]));
      std::printf("    %s: ufbx skins %zu vertices at frame 30, the worst %.5f from Blendity's Skin\n", fs::filename(path).c_str(), got.size(), worst);
      CHECK(worst < 1e-3f);
    }
    const R352Rig back = r352_read(ap);
    CHECK(back.skins == 1 && back.clips == 1);
  });
  test("FBX carries the rig (review 352): a joint that moves no vertex still goes (a cluster with none); a turn about Y past 90 degrees stays one turn between the keys", [] {
    MeshPtr cube;
    ExportItem it = r352_rigged(cube, Mat4::identity());
    auto sk = std::make_shared<ExportSkin>(*it.skin);
    sk->joints.push_back({"Tip", 1, Vec3(0.0f, 0.5f, 0.0f), Quat(), Vec3(1.0f)});
    ExportSkin::Anim &an = sk->anims[0];
    an.t.push_back({Vec3(0, 0.5f, 0), Vec3(0, 0.5f, 0), Vec3(0, 0.5f, 0)});
    an.r.push_back({Quat(), Quat(), Quat()});
    an.s.push_back({Vec3(1.0f), Vec3(1.0f), Vec3(1.0f)});
    an.r[1] = {Quat(), Quat::axis_angle(Vec3(0, 1, 0), kPi * 60.0f / 180.0f), Quat::axis_angle(Vec3(0, 1, 0), kPi * 120.0f / 180.0f)};
    it.skin = sk;
    const std::string path = fs::join(fs::join(test_dir(), "r352"), "tip.fbx");
    fs::make_dirs(fs::parent(path));
    fs::write_file(path, export_fbx({it}, export_defaults((int)ExportFormat::FBX)));
    const R352Rig r = r352_read(path);
    CHECK(r.bones.size() == 3);
    /* Half way between 60 and 120 degrees: 90, about Y - not a swing through another axis. */
    ufbx_load_opts opts = {};
    ufbx_error err;
    ufbx_scene *scene = ufbx_load_file(path.c_str(), &opts, &err);
    CHECK(scene != nullptr);
    if (!scene) return;
    const ufbx_node *child = ufbx_find_node(scene, "Child");
    const ufbx_transform t = child && scene->anim_stacks.count ? ufbx_evaluate_transform(scene->anim_stacks.data[0]->anim, child, 1.5 / 24.0) : ufbx_transform{};
    const double angle = 2.0 * std::acos(std::min(1.0, std::fabs(t.rotation.w))) * 180.0 / kPi;
    const double off_axis = std::sqrt(t.rotation.x * t.rotation.x + t.rotation.z * t.rotation.z);
    std::printf("    half way between the 60 and 120 degree keys: %.2f degrees, off the Y axis %.4f\n", angle, off_axis);
    CHECK(std::fabs(angle - 90.0) < 0.5 && off_axis < 1e-3);
    ufbx_free_scene(scene);
  });
  test("FBX carries the rig (review 352): from the editor an Outline's shell is skinned too, a Mirror on the rig's mesh is applied, and two meshes on one armature share one skeleton and one clip", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.step_frame_headless();
    ed.command("select Cube");
    ed.command("position 2 0.5 0");
    ed.command("component MirrorModifier");
    ed.command("component Outline");
    ed.command("create Armature");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    ed.command("create Sphere");
    ed.command("component SkinModifier");
    ed.command("set SkinModifier.Armature Armature");
    ed.command("skin bind");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    GameObject *rig = ed.scene().find_by_name("Cube Rig");
    CHECK(rig != nullptr);
    if (!rig) return;
    ed.select_for_test(rig->id, false);
    ed.command("anim new R352B");
    ed.command("anim key Bone Rotation 2 0 0");
    ed.command("anim key Bone Rotation 2 30 45");
    ed.command("anim preview off");
    const std::string path = fs::join(fs::join(test_dir(), "r352"), "editor_many.fbx");
    fs::make_dirs(fs::parent(path));
    ExportOptions o = export_defaults((int)ExportFormat::FBX);
    o.selection_only = false;
    ed.export_model_for_test(o, path);
    ImportResult res;
    CHECK(import_model(path, res));
    size_t bones = 0, cube_min = 0;
    float xmin = 1e9f;
    for (const ImportedNode &n : res.nodes) {
      bones += n.is_bone && n.name == "Bone";
      if (!n.mesh || n.name != "Cube") continue;
      for (const Vec3 &p : n.mesh->positions) xmin = std::min(xmin, p.x);
      cube_min = n.mesh->vert_count();
    }
    std::printf("    the editor's scene: %zu skin(s) (Cube, its Outline, the Sphere), the Bone %zu time(s), %zu clip(s); the Cube's %zu vertices (8 before the Mirror), x from %.2f\n",
                res.skins.size(), bones, res.clips.size(), cube_min, xmin);
    CHECK(res.skins.size() == 3 && bones == 1 && res.clips.size() == 1);
    CHECK(cube_min > 8);  // (the Mirror applied: a centred cube mirrors onto itself, its vertices doubled)
  });
  test("the lesson's Try-it (round 352): create Cube, create Armature, export fbx all gives an FBX with a skin", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    const std::string path = fs::join(fs::join(test_dir(), "r352"), "tryit.fbx");
    fs::make_dirs(fs::parent(path));
    for (const std::string &c : {std::string("create Cube"), std::string("create Armature"), "export fbx all " + path}) ed.command(c);
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    CHECK(r352_read(path).skins == 1);
  });
}

/* ---------------------------------------------------------------- round 353: textures and lightmap UVs in exports */
/* A cube whose material takes a procedural texture's albedo and normal (a .ptex made by `newtex`), and the Color Grid as
 * its emission. */
static GameObject *r353_textured(Editor &ed) {
  ed.command("create Cube");
  GameObject *g = ed.active_object_for_test();
  if (!g) return nullptr;
  ed.command("newtex R353Bricks Bricks");
  MaterialPtr m = make_material("R353", Vec3(1.0f));
  m->base_map = TextureRef{"Assets/Textures/R353Bricks.ptex#albedo", false};
  m->normal_map = TextureRef{"Assets/Textures/R353Bricks.ptex#normal", true};
  m->emission_map = TextureRef{"generated:Color Grid", false};
  m->emission = Vec3(1.0f), m->emission_strength = 1.0f;  // (lit, so the map shows - glTF multiplies them)
  g->get<MeshRenderer>()->materials = {m};
  return g;
}

static void tests_round353() {
  test("Textures in exports (round 353): a relative path lands in the project; the .glb embeds the procedural texture's images and the Color Grid; the FBX references PNGs beside it", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    GameObject *g = r353_textured(ed);
    CHECK(g != nullptr);
    if (!g) return;
    const std::string project = std::getenv("BLENDITY_PROJECT");
    for (const char *d : {"Exports/R353", "Exports/R353F"}) { std::error_code ec; std::filesystem::remove_all(fs::join(project, d), ec); }  // (a new folder each run: the case that failed)
    ed.command("export glb Exports/R353/cube.glb");
    const std::string glb_path = fs::join(project, "Exports/R353/cube.glb");
    std::string glb;
    const bool there = fs::read_file(glb_path, glb);
    const Json j = r351_glb_json(glb);
    std::printf("    the .glb in the project: %d, %zu image(s) embedded, %zu texture(s)\n", (int)there, j["images"].size(), j["textures"].size());
    CHECK(there && j["images"].size() >= 3 && j["materials"][0]["emissiveTexture"]["index"].type == Json::Num);  // (albedo, normal, the Color Grid as the emission)
    bool embedded = true;
    for (size_t i = 0; i < j["images"].size(); i++) embedded = embedded && j["images"][i]["bufferView"].type == Json::Num;
    CHECK(embedded);
    ed.command("export fbx Exports/R353F/cube.fbx");
    const std::string dir = fs::join(project, "Exports/R353F");
    ImportResult r;
    CHECK(import_model(fs::join(dir, "cube.fbx"), r));
    const MaterialPtr m = r.materials.empty() ? nullptr : r.materials[0];
    auto found = [](const TextureRef &t) { return !t.empty() && fs::exists(resolve_asset_path(t.path)); };
    std::printf("    the FBX (its own new folder): base %d, normal %d, emission %d maps found\n", m ? (int)found(m->base_map) : 0, m ? (int)found(m->normal_map) : 0,
                m ? (int)found(m->emission_map) : 0);
    CHECK(m && found(m->base_map) && found(m->normal_map) && found(m->emission_map));
    /* The Color Grid as written: its pixels in the Color Grid's order (red, not blue, where it's red). */
    Bitmap grid;
    std::string err;
    const TexturePtr cg = texture_color_grid();
    CHECK(load_image(fs::join(dir, "Blendity_Color_Grid.png"), grid, err) && cg && !cg->levels.empty());
    if (grid.rgba8.empty() || !cg || cg->levels.empty()) return;
    int wrong = 0;
    for (size_t i = 0; i < 64 && i * 4 + 2 < grid.rgba8.size(); i++) {
      const uint32_t c = cg->levels[0].px8[i * 97 % cg->levels[0].px8.size()];
      const size_t k = (i * 97 % cg->levels[0].px8.size()) * 4;
      wrong += grid.rgba8[k] != (c & 0xFFu) || grid.rgba8[k + 2] != (c >> 16 & 0xFFu);
    }
    CHECK(wrong == 0);
  });
  test("Lightmap UVs in exports (round 353): the option writes a second UV set - glTF TEXCOORD_1 inside 0..1, FBX a second UV map - and off writes none", [] {
    MeshPtr cube = primitives::cube();
    ExportItem it;
    it.name = "Box", it.mesh = cube.get(), it.world = Mat4::identity();
    ExportOptions o = export_defaults((int)ExportFormat::GLB);
    CHECK(r351_glb_json(export_gltf({it}, o, true, "", nullptr))["meshes"][0]["primitives"][0]["attributes"]["TEXCOORD_1"].type != Json::Num);
    o.lightmap_uvs = true;
    const std::string glb = export_gltf({it}, o, true, "", nullptr);
    const Json j = r351_glb_json(glb);
    const Json &at = j["meshes"][0]["primitives"][0]["attributes"];
    const std::vector<float> uv2 = at["TEXCOORD_1"].type == Json::Num ? r351_glb_floats(glb, j, (size_t)at["TEXCOORD_1"].number()) : std::vector<float>();
    float lo = 1.0f, hi = 0.0f;
    for (float v : uv2) lo = std::min(lo, v), hi = std::max(hi, v);
    std::printf("    glTF: TEXCOORD_1 with %zu values in %.3f..%.3f\n", uv2.size(), lo, hi);
    CHECK(!uv2.empty() && lo >= 0.0f && hi <= 1.0f);
    ExportOptions f = export_defaults((int)ExportFormat::FBX);
    f.lightmap_uvs = true, f.ascii = true;
    const std::string fbx = export_fbx({it}, f);
    fs::make_dirs(fs::join(test_dir(), "r353"));
    fs::write_file(fs::join(test_dir(), "r353/uv2.glb"), glb);
    ExportOptions fb = f;
    fb.ascii = false;
    fs::write_file(fs::join(test_dir(), "r353/uv2.fbx"), export_fbx({it}, fb));  // (for a look in Blender)
    size_t maps = 0;
    for (size_t at2 = fbx.find("LayerElementUV:"); at2 != std::string::npos; at2 = fbx.find("LayerElementUV:", at2 + 1)) maps++;
    std::printf("    FBX: %zu UV map(s), Lightmap named %d\n", maps, (int)(fbx.find("\"Lightmap\"") != std::string::npos));
    CHECK(maps == 2 && fbx.find("\"Lightmap\"") != std::string::npos && fbx.find("Layer: 1") != std::string::npos);
    /* A mesh without UVs: the lightmap set is its first (TEXCOORD_0), and FBX's off writes one map. */
    MeshPtr bare = std::make_shared<Mesh>(*cube);
    bare->uvs.clear();
    ExportItem bi = it;
    bi.mesh = bare.get();
    const Json jb = r351_glb_json(export_gltf({bi}, o, true, "", nullptr));
    CHECK(jb["meshes"][0]["primitives"][0]["attributes"]["TEXCOORD_0"].type == Json::Num && jb["meshes"][0]["primitives"][0]["attributes"]["TEXCOORD_1"].type != Json::Num);
    ExportOptions foff = export_defaults((int)ExportFormat::FBX);
    foff.ascii = true;
    const std::string plain = export_fbx({it}, foff);
    size_t plain_maps = 0;
    for (size_t at3 = plain.find("LayerElementUV:"); at3 != std::string::npos; at3 = plain.find("LayerElementUV:", at3 + 1)) plain_maps++;
    CHECK(plain_maps == 1);
  });
  test("the lesson's Try-it (round 353): create Cube, newtex, texture, export glb gives a .glb with the texture embedded, in the project", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    std::remove(fs::join(std::getenv("BLENDITY_PROJECT"), "Exports/Learn Textured.glb").c_str());
    for (const char *c : {"create Cube", "newtex LearnBricks Bricks", "texture Assets/Textures/LearnBricks.ptex", "export glb lightmap Exports/Learn Textured.glb"}) ed.command(c);
    std::string glb;
    fs::read_file(fs::join(std::getenv("BLENDITY_PROJECT"), "Exports/Learn Textured.glb"), glb);
    const Json j = r351_glb_json(glb);
    CHECK(j["images"].size() >= 1 && j["meshes"][0]["primitives"][0]["attributes"]["TEXCOORD_1"].type == Json::Num);
  });
}

/* ---------------------------------------------------------------- round 354: an export never takes a stale mesh; one glTF skeleton a rig */
/* Steps the editor's frames until `done` or about 30 seconds (a worker's job landing), sleeping a little between. */
template <class Done>
static bool r354_until(Editor &ed, Done done) {
  for (int i = 0; i < 3000 && !done(); i++) {
    ed.step_frame_headless();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return done();
}

static void tests_round354() {
  test("Export waits (round 354): an export asked for while a skin bind still works on an unbound mesh waits in the tray, then writes it with the bind's weights - the objects asked for, though the selection moved", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("select Cube");
    ed.command("create Armature");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    ed.set_skin_async_min_verts_for_test(0);  // (the bind on a worker, as a 5000-vertex mesh's is)
    ed.command("create Sphere");  // (unbound: no weights until the bind lands)
    const std::string sphere = ed.active_object_for_test() ? ed.active_object_for_test()->name : "";  // (round 367: "Sphere (1)" beside another)
    ed.command("component SkinModifier");
    ed.command("set SkinModifier.Armature Armature");
    ed.command("skin bind");
    const std::string path = fs::join(fs::join(test_dir(), "r354"), "waited.glb");
    fs::make_dirs(fs::parent(path));
    std::remove(path.c_str());
    ed.command("export glb " + path);
    bool waits = false;
    for (const auto &j : ed.jobs_for_test()) waits = waits || j.label.rfind("Export waits for ", 0) == 0;
    std::printf("    right after the export: written %d, the tray says it waits %d\n", (int)fs::exists(path), (int)waits);
    CHECK(!fs::exists(path) && waits);
    ed.command("select Cube");  // (the selection moves while it waits: the Sphere still goes)
    CHECK(r354_until(ed, [&] { return fs::exists(path); }));
    ImportResult r;
    CHECK(import_model(path, r));
    std::string names;
    float bone = 0.0f;
    for (const ImportedNode &n : r.nodes) {
      if (!n.mesh) continue;
      names += n.name + " ";
      for (const VertexGroup &vg : n.mesh->vertex_groups)
        for (float w : vg.weights) bone += vg.name == "Bone" ? w : 0.0f;
    }
    std::printf("    when the bind landed: the file holds %s, the Bone's weights sum %.2f\n", names.c_str(), bone);
    CHECK(names == sphere + " " && bone > 1.0f);
  });
  test("Export waits (review 354): the tray's Export Now writes at once with a warning, Cancel drops the export, two exports both wait, New Scene drops them", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.record_buttons_for_test(true);
    ed.command("select Cube");
    ed.command("create Armature");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    ed.set_skin_async_min_verts_for_test(0);
    ed.command("create Sphere");  // (a dense mesh: its bind takes long enough to press the tray's buttons)
    GameObject *dense = ed.active_object_for_test();  // (by id: the sample scene has a Sphere of its own)
    ed.command("subdivide 3");
    ed.command("component SkinModifier");
    ed.command("set SkinModifier.Armature Armature");
    ed.command("skin bind");
    ed.hold_skin_job_for_test(true);  // (running until released, however fast the machine)
    const std::string dir = fs::join(test_dir(), "r354");
    fs::make_dirs(dir);
    const std::string a = fs::join(dir, "a.glb"), b = fs::join(dir, "b.fbx");
    std::remove(a.c_str()), std::remove(b.c_str());
    ed.command("export glb " + a);
    ed.command("export fbx " + b);
    size_t waiting = 0;
    for (const auto &j : ed.jobs_for_test()) waiting += j.label.rfind("Export waits for ", 0) == 0;
    CHECK(waiting == 2);
    /* Export Now through the tray's own button: the front row, the first export. */
    std::vector<LogEntry> log;
    const size_t since = Log::fetch(0, log);
    ed.step_frame_headless();
    Recti now{0, 0, 0, 0};
    CHECK(ed.button_rect_for_test("job:Export Now", now));
    r34_click(ed, now.x + now.w / 2, now.y + now.h / 2);
    r34_steps(ed, 2);
    log.clear();
    Log::fetch(since, log);
    bool said = false;
    for (const LogEntry &e : log) said = said || (e.level == LogLevel::Warning && e.text.find("without waiting") != std::string::npos);
    std::printf("    Export Now in the tray: a.glb written %d, warned %d, b.fbx still waiting %d\n", (int)fs::exists(a), (int)said, (int)!fs::exists(b));
    CHECK(fs::exists(a) && said && !fs::exists(b));
    /* Cancel through the tray: the second export dropped, nothing written. */
    Recti cancel{0, 0, 0, 0};
    CHECK(ed.button_rect_for_test("job:Cancel", cancel));
    r34_click(ed, cancel.x + cancel.w / 2, cancel.y + cancel.h / 2);
    r34_steps(ed, 2);
    bool still = false;
    for (const auto &j : ed.jobs_for_test()) still = still || j.label.rfind("Export waits for ", 0) == 0;
    CHECK(!still);
    ed.hold_skin_job_for_test(false);
    r354_until(ed, [&] { return ed.jobs_for_test().empty(); });
    CHECK(!fs::exists(b));
    /* Waiting, then New Scene: dropped and said. */
    CHECK(dense != nullptr);
    if (!dense) return;
    ed.select_for_test(dense->id, false);
    ed.command("skin bind");
    ed.command("export glb " + a);
    const size_t since2 = Log::fetch(0, log);
    ed.command("newscene");
    log.clear();
    Log::fetch(since2, log);
    bool dropped = false;
    for (const LogEntry &e : log) dropped = dropped || e.text.find("dropped: a new scene") != std::string::npos;
    CHECK(dropped);
  });
  test("Export waits (round 354): a Quad Remesh is waited for and the file has the remeshed triangles (the libraries' build: QuadriFlow)", [] {
    if (!meshops::quad_remesh_available()) return;
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("select Sphere");
    ed.command("sculpt on");
    ed.command("sculpt remesh 600");
    const std::string p = fs::join(fs::join(test_dir(), "r354"), "remeshed.glb");
    fs::make_dirs(fs::parent(p));
    std::remove(p.c_str());
    ed.command("export glb " + p);
    CHECK(!fs::exists(p));
    CHECK(r354_until(ed, [&] { return fs::exists(p); }));
    GameObject *g = ed.scene().find_by_name("Sphere");
    std::string glb;
    fs::read_file(p, glb);
    const Json j = r351_glb_json(glb);
    size_t tris = 0, now = 0;
    for (size_t k = 0; k < j["meshes"][0]["primitives"].size(); k++) tris += (size_t)j["accessors"][(size_t)j["meshes"][0]["primitives"][k]["indices"].number()]["count"].number() / 3;
    for (size_t f = 0; g && f < g->evaluated_mesh()->face_count(); f++) now += g->evaluated_mesh()->face_size(f) - 2;
    std::printf("    the file: %zu triangles, the remeshed sphere %zu\n", tris, now);
    CHECK(g && tris == now);
  });
  test("glTF shares the rig (round 354): a Cube and a Sphere on one armature export one skeleton and one clip, the Sphere's shape-key weights in it", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.step_frame_headless();
    ed.command("select Cube");
    ed.command("create Armature");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    ed.command("create Sphere");
    const std::string sphere = ed.active_object_for_test() ? ed.active_object_for_test()->name : "";  // (round 367: unique names)
    ed.command("shape add Puff");
    ed.command("component SkinModifier");
    ed.command("set SkinModifier.Armature Armature");
    ed.command("skin bind");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    GameObject *rig = ed.scene().find_by_name("Cube Rig");
    CHECK(rig != nullptr);
    if (!rig) return;
    ed.select_for_test(rig->id, false);
    ed.command("anim new R354");
    ed.command("anim key Bone Rotation 2 0 0");
    ed.command("anim key Bone Rotation 2 30 45");
    ed.command("anim preview off");
    const std::string path = fs::join(fs::join(test_dir(), "r354"), "shared.glb");
    fs::make_dirs(fs::parent(path));
    ExportOptions o = export_defaults((int)ExportFormat::GLB);
    o.selection_only = false;
    ed.export_model_for_test(o, path);
    std::string glb;
    fs::read_file(path, glb);
    const Json j = r351_glb_json(glb);
    size_t bones = 0, skinned = 0, weights = 0;
    for (size_t n = 0; n < j["nodes"].size(); n++) bones += j["nodes"][n]["name"].str == "Bone", skinned += j["nodes"][n]["skin"].type == Json::Num;
    for (size_t c = 0; c < j["animations"][0]["channels"].size(); c++) weights += j["animations"][0]["channels"][c]["target"]["path"].str == "weights";
    std::printf("    the file: %zu skin(s) on %zu mesh node(s), the Bone %zu time(s), %zu clip(s), %zu weights channel(s)\n", j["skins"].size(), skinned, bones,
                j["animations"].size(), weights);
    CHECK(j["skins"].size() == 1 && skinned == 2 && bones == 1 && j["animations"].size() == 1 && weights == 1);
    bool on_sphere = false;  // (the weights channel on the Sphere's own node)
    for (size_t c = 0; c < j["animations"][0]["channels"].size(); c++) {
      const Json &ch = j["animations"][0]["channels"][c];
      if (ch["target"]["path"].str == "weights") on_sphere = j["nodes"][(size_t)ch["target"]["node"].number()]["name"].str == sphere;
    }
    CHECK(on_sphere);
    ImportResult r;
    CHECK(import_model(path, r) && r.clips.size() == 1);
  });
}

/* ---------------------------------------------------------------- round 355: the performance budget */
static void tests_round355() {
  test("Budget (round 355): VRChat's ranks - the first rank each row fits, the worst row the avatar's rank, past Poor Very Poor", [] {
    const std::vector<BudgetTarget> &t = budget_targets();
    const int pc = budget_target_index("VRChat PC"), quest = budget_target_index("vrchat quest");
    CHECK(pc >= 0 && quest >= 0);
    if (pc < 0 || quest < 0) return;
    BudgetCounts c;
    c.value[(int)BudgetRow::Triangles] = 50000, c.value[(int)BudgetRow::SkinnedMeshes] = 1, c.value[(int)BudgetRow::Bones] = 80;
    BudgetResult r = budget_rank(t[(size_t)pc], c);
    std::printf("    PC, 50k triangles, 80 bones: %s (triangles %s, bones %s)\n", budget_rank_name(t[(size_t)pc], r.rank).c_str(),
                budget_rank_name(t[(size_t)pc], r.row_rank[(int)BudgetRow::Triangles]).c_str(), budget_rank_name(t[(size_t)pc], r.row_rank[(int)BudgetRow::Bones]).c_str());
    CHECK(budget_rank_name(t[(size_t)pc], r.rank) == "Good");
    c.value[(int)BudgetRow::Triangles] = 80000;
    CHECK(budget_rank_name(t[(size_t)pc], budget_rank(t[(size_t)pc], c).rank) == "Very Poor");
    c.value[(int)BudgetRow::Triangles] = 9000;
    r = budget_rank(t[(size_t)quest], c);
    CHECK(budget_rank_name(t[(size_t)quest], r.rank) == "Good" && budget_rank_name(t[(size_t)quest], r.row_rank[(int)BudgetRow::Triangles]) == "Good");
    c.value[(int)BudgetRow::Lights] = 1;  // (a light: Poor on PC, past Poor on Quest)
    CHECK(budget_rank_name(t[(size_t)pc], budget_rank(t[(size_t)pc], c).rank) == "Poor");  // (PC's Poor allows one light)
    CHECK(budget_rank_name(t[(size_t)quest], budget_rank(t[(size_t)quest], c).rank) == "Very Poor");
  });
  test("Budget (round 355): the counts of what's selected - a cube's 12 triangles, one mesh, one slot; an Outline doubles them; a spring chain and its bones; the scene's light", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("create Cube");
    GameObject *g = ed.active_object_for_test();
    CHECK(g != nullptr);
    if (!g) return;
    BudgetCounts c = budget_count({g});
    std::printf("    a cube: %.0f triangles, %.0f mesh, %.0f slot\n", c.value[(int)BudgetRow::Triangles], c.value[(int)BudgetRow::Meshes], c.value[(int)BudgetRow::MaterialSlots]);
    CHECK(c.value[(int)BudgetRow::Triangles] == 12 && c.value[(int)BudgetRow::Meshes] == 1 && c.value[(int)BudgetRow::MaterialSlots] == 1);
    ed.command("component Outline");
    c = budget_count({g});
    CHECK(c.value[(int)BudgetRow::Triangles] == 24 && c.value[(int)BudgetRow::MaterialSlots] == 2);
    std::vector<GameObject *> all;
    ed.scene().for_each([&](GameObject &o) { all.push_back(&o); });
    CHECK(budget_count(all).value[(int)BudgetRow::Lights] >= 1);
    ed.command("rig humanoid");
    ed.command("select Head");
    ed.command("bone add Head 0.1");
    ed.command("spring add Bone hair");
    all.clear();
    ed.scene().for_each([&](GameObject &o) { all.push_back(&o); });
    c = budget_count(all);
    std::printf("    the humanoid with a hair chain: %.0f bones, %.0f chain(s), %.0f spring bone(s)\n", c.value[(int)BudgetRow::Bones], c.value[(int)BudgetRow::SpringChains],
                c.value[(int)BudgetRow::SpringBones]);
    CHECK(c.value[(int)BudgetRow::SpringChains] >= 1 && c.value[(int)BudgetRow::SpringBones] >= 1 && c.value[(int)BudgetRow::Bones] >= 10);
  });
  test("Budget (round 355): no target, no ranking - exports quiet; `budget quest` chooses one; the Profiler shows the rank; an export's line counts what the file holds (no lights), and only for glTF and FBX", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.record_buttons_for_test(true);
    ed.command("create Sphere");
    GameObject *g = ed.active_object_for_test();
    CHECK(g != nullptr);
    if (!g) return;
    const std::string dir = fs::join(test_dir(), "r355");
    fs::make_dirs(dir);
    std::vector<LogEntry> log;
    auto said = [&](size_t since, const char *text) {
      log.clear();
      Log::fetch(since, log);
      for (const LogEntry &e : log)
        if (e.text.find(text) != std::string::npos) return true;
      return false;
    };
    size_t since = Log::fetch(0, log);
    ed.command("export glb " + fs::join(dir, "quiet.glb"));
    CHECK(!said(since, "Budget ("));
    ed.command("budget quest");
    ed.command("window Profiler");
    r34_steps(ed, 3);
    bool shown = false;
    for (const std::string &l : ed.button_labels_for_test()) shown = shown || l.rfind("shown:budget ", 0) == 0;
    CHECK(shown);
    since = Log::fetch(0, log);
    ExportOptions all = export_defaults((int)ExportFormat::GLB);
    all.selection_only = false;  // (the whole scene: its Sun not in the file, so not counted)
    ed.export_model_for_test(all, fs::join(dir, "scene.glb"));
    const bool line = said(since, "Budget (VRChat Quest): the file is");
    bool lights = false;
    for (const LogEntry &e : log) lights = lights || (e.text.find("Budget (") != std::string::npos && e.text.find("Lights") != std::string::npos);
    std::printf("    the scene's glb: the line %d, held by its lights %d\n", (int)line, (int)lights);
    CHECK(line && !lights);
    since = Log::fetch(0, log);
    ed.command("export stl " + fs::join(dir, "print.stl"));
    CHECK(!said(since, "Budget ("));
  });
  test("Budget (round 355): Decimate to Good lands near the limit, multiplies an existing Decimate, leaves small meshes whole, keeps a skinned, keyed mesh's weights and keys, and Undo takes it back", [] {
    if (!meshops::decimate_available()) return;  // (meshoptimizer: the libraries' build)
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("select Cube");
    ed.command("create Armature");  // (the Cube skinned)
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    ed.command("create Sphere");
    GameObject *g = ed.active_object_for_test();
    CHECK(g != nullptr);
    if (!g) return;
    ed.command("subdivide 2");
    ed.command("shape add Puff");
    ed.command("component SkinModifier");
    ed.command("set SkinModifier.Armature Armature");
    ed.command("skin bind");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    ed.select_for_test(g->id, false);
    ed.command("budget quest");
    const double limit = budget_targets()[(size_t)budget_target_index("VRChat Quest")].ranks[1].limit[(int)BudgetRow::Triangles];
    const double before = budget_count({g}).value[(int)BudgetRow::Triangles];
    ed.command("budget fix good");
    r34_steps(ed, 2);
    const double after = budget_count({g}).value[(int)BudgetRow::Triangles];
    const DecimateModifier *d = g->get<DecimateModifier>();
    size_t at = 0, skin_at = 0;
    for (size_t i = 0; i < g->components.size(); i++) at = g->components[i].get() == d ? i : at, skin_at = std::strcmp(g->components[i]->type_name(), SkinModifier::kName) == 0 ? i : skin_at;
    const Mesh *em = g->evaluated_mesh();
    std::printf("    Quest's Good (%.0f): %.0f triangles before, %.0f after (ratio %.3f); the Decimate at %zu, the Skin at %zu; groups %zu\n", limit, before, after, d ? d->ratio : -1.0f,
                at, skin_at, em ? em->vertex_groups.size() : (size_t)0);
    CHECK(before > limit && after <= limit && after > 0.85 * limit && d && at < skin_at);
    /* Exported: its weights and its key still there. */
    const std::string path = fs::join(fs::join(test_dir(), "r355"), "decimated.glb");
    fs::make_dirs(fs::parent(path));
    ed.export_model_for_test(export_defaults((int)ExportFormat::GLB), path);
    std::string glb;
    fs::read_file(path, glb);
    const Json j = r351_glb_json(glb);
    CHECK(j["skins"].size() == 1 && j["meshes"][0]["extras"]["targetNames"][0].str == "Puff");
    /* An existing Decimate is multiplied: from a ratio twice too high, one fix lands under the limit again. */
    const_cast<DecimateModifier *>(d)->ratio = std::min(1.0f, d->ratio * 2.0f);
    ed.mark_changed_for_test("raise the ratio");
    r34_steps(ed, 2);  // (its own undo step)
    ed.command("budget fix good");
    r34_steps(ed, 2);
    CHECK(budget_count({g}).value[(int)BudgetRow::Triangles] <= limit);
    ed.command("undo");
    r34_steps(ed, 2);
    GameObject *back = ed.scene().find(g->id);
    CHECK(back && back->get<DecimateModifier>() && budget_count({back}).value[(int)BudgetRow::Triangles] > limit);
  });
  test("the lesson's Try-it (round 355): create Cube, then budget quest ranks it", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    std::vector<LogEntry> log;
    const size_t since = Log::fetch(0, log);
    for (const char *c : {"create Cube", "budget quest"}) ed.command(c);
    log.clear();
    Log::fetch(since, log);
    bool ranked = false;
    for (const LogEntry &e : log) ranked = ranked || e.text.find("Budget (VRChat Quest)") != std::string::npos;
    CHECK(ranked);
  });
}

/* ---------------------------------------------------------------- round 356: the VRChat avatar kit */
static MeshPtr r356_mouths() {  // (a cube with three sculpted mouths: AA up, OH out in Z, CH wide in X)
  MeshPtr m = std::make_shared<Mesh>(*primitives::cube());
  const size_t n = m->vert_count();
  m->shape_keys = {{"AA", std::vector<Vec3>(n, Vec3(0, 0.1f, 0))}, {"OH", std::vector<Vec3>(n, Vec3(0, 0, 0.1f))}, {"CH", std::vector<Vec3>(n, Vec3(0.1f, 0, 0))}};
  return m;
}

static const ShapeKey *r356_key(const Mesh &m, const std::string &name) {
  for (const ShapeKey &k : m.shape_keys)
    if (k.name == name) return &k;
  return nullptr;
}

static void tests_round356() {
  test("Visemes (round 356): the 15 vrc.v_* keys mixed from AA, OH and CH - aa all AA, ou all OH, dd 0.3 AA + 0.7 CH, sil a hair - and refused without a base", [] {
    MeshPtr m = r356_mouths();
    std::string why;
    CHECK(make_visemes(*m, "AA", "OH", "CH", why));
    size_t made = 0;
    for (const std::string &v : vrchat_viseme_names()) made += r356_key(*m, v) != nullptr;
    const ShapeKey *aa = r356_key(*m, "vrc.v_aa"), *ou = r356_key(*m, "vrc.v_ou"), *dd = r356_key(*m, "vrc.v_dd"), *sil = r356_key(*m, "vrc.v_sil");
    std::printf("    %zu of 15 made; aa (%.4f %.4f %.4f), dd (%.3f %.3f %.3f), sil moves %.6f\n", made, aa ? aa->delta[0].x : -1, aa ? aa->delta[0].y : -1,
                aa ? aa->delta[0].z : -1, dd ? dd->delta[0].x : -1, dd ? dd->delta[0].y : -1, dd ? dd->delta[0].z : -1, sil ? length(sil->delta[0]) : -1);
    CHECK(made == 15 && aa && ou && dd && sil);
    if (!aa || !ou || !dd || !sil) return;
    CHECK(std::fabs(aa->delta[0].y - 0.09998f) < 1e-5f && std::fabs(ou->delta[0].z - 0.09994f) < 1e-5f);
    CHECK(std::fabs(dd->delta[0].y - 0.03f) < 1e-5f && std::fabs(dd->delta[0].x - 0.07f) < 1e-5f);
    CHECK(length(sil->delta[0]) > 0.0f && length(sil->delta[0]) < 1e-3f);
    CHECK(shape_key_problems(*m).empty());
    MeshPtr bare = std::make_shared<Mesh>(*primitives::cube());
    CHECK(!make_visemes(*bare, "AA", "OH", "CH", why) && why.find("AA") != std::string::npos);
  });
  test("Visemes (round 356): the Inspector's lines - a key that moves nothing, two of one name, visemes missing", [] {
    MeshPtr m = std::make_shared<Mesh>(*primitives::cube());
    const size_t n = m->vert_count();
    m->shape_keys = {{"Blink", std::vector<Vec3>(n, Vec3(0.0f))}, {"Smile", std::vector<Vec3>(n, Vec3(0, 0.1f, 0))}, {"Smile", std::vector<Vec3>(n, Vec3(0, 0.1f, 0))},
                     {"vrc.v_aa", std::vector<Vec3>(n, Vec3(0, 0.1f, 0))}};
    const std::vector<std::string> p = shape_key_problems(*m);
    bool still = false, twice = false, missing = false;
    for (const std::string &l : p) still = still || l.find("'Blink' moves nothing") != std::string::npos, twice = twice || l.find("two shape keys named 'Smile'") != std::string::npos,
                                    missing = missing || (l.find("visemes missing") != std::string::npos && l.find("sil") != std::string::npos);
    CHECK(still && twice && missing);
  });
  test("Avatar kit (round 356): Humanoid Rig fits a selected mesh; the checklist goes from amber to green through its Fixes; the Rigging window shows it; the Inspector shows a key's problem", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.record_buttons_for_test(true);
    ed.command("create Cube");
    GameObject *twin = ed.active_object_for_test();
    ed.command("create Cylinder");
    GameObject *body = ed.active_object_for_test();
    CHECK(body != nullptr && twin != nullptr);
    if (!body || !twin) return;
    twin->name = body->name;  // (found first by name: the Fix must select the body by its id)
    CHECK(ed.scene().find_by_name(body->name) != body);
    ed.command("position 0 1.25 0");  // (the cylinder is 2 m tall: from 0.25 to 2.25)
    const AABB bb = body->world_bounds();
    ed.command("rig humanoid");
    GameObject *rig = ed.active_object_for_test();
    GameObject *head = ed.scene().find_by_name("Head");
    CHECK(rig && head);
    if (!rig || !head) return;
    const float top = head->world_position().y + head->get<Bone>()->length;
    std::printf("    the mesh from y %.2f to %.2f; the rig's feet at %.2f, its head's top at %.2f\n", bb.min.y, bb.max.y, rig->world_position().y, top);
    CHECK(std::fabs(rig->world_position().y - bb.min.y) < 1e-3f && std::fabs(top - bb.max.y) < 0.02f);
    ed.select_for_test(body->id, false);
    ed.command("component SkinModifier");
    ed.command("set SkinModifier.Armature " + rig->name);
    ed.command("skin bind");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    ed.select_for_test(body->id, false);
    for (const char *c : {"shape add AA", "shape add OH", "shape add CH"}) ed.command(c);
    std::vector<AvatarCheck> list = avatar_check(ed.scene(), *rig);
    auto get = [&](const char *name) {
      for (const AvatarCheck &c : list)
        if (c.item == name) return c;
      return AvatarCheck();
    };
    std::printf("    before: visemes %d (%s), height %d (%s), Unity names %d\n", (int)get("Visemes").ok, get("Visemes").detail.c_str(), (int)get("Height 0.5 to 5 m").ok,
                get("Height 0.5 to 5 m").detail.c_str(), (int)get("Unity's bone names").ok);
    CHECK(!get("Visemes").ok && get("Height 0.5 to 5 m").ok && get("Humanoid bones").ok && get("Unity's bone names").ok);
    size_t visemes = 0;
    for (size_t i = 0; i < list.size(); i++) visemes = list[i].item == "Visemes" ? i + 1 : visemes;
    ed.command(strprintf("avatar fix %zu", visemes));
    list = avatar_check(ed.scene(), *rig);
    std::printf("    after avatar fix %zu: visemes %d (%s)\n", visemes, (int)get("Visemes").ok, get("Visemes").detail.c_str());
    CHECK(get("Visemes").ok);
    const MeshFilter *tmf = twin->get<MeshFilter>();
    CHECK(tmf && tmf->mesh && tmf->mesh->shape_keys.empty());  // (the twin left alone)
    /* Undo takes the visemes back; Redo brings them again. */
    r34_steps(ed, 2);
    ed.command("undo");
    r34_steps(ed, 2);
    list = avatar_check(ed.scene(), *rig);
    CHECK(!get("Visemes").ok);
    ed.command("redo");
    r34_steps(ed, 2);
    list = avatar_check(ed.scene(), *rig);
    CHECK(get("Visemes").ok);
    /* At the origin: under a moved parent, the Fix puts the root at the world's origin (not its local zero). */
    ed.command("create Empty");
    GameObject *holder = ed.active_object_for_test();
    CHECK(holder != nullptr);
    if (!holder) return;
    holder->set_local_position(Vec3(1.0f, 0.0f, 2.0f));
    ed.scene().set_parent(rig, holder, -1, true);
    rig->set_local_position(Vec3(0.5f, 0.0f, 0.0f));
    ed.mark_changed_for_test("move the rig");
    r34_steps(ed, 2);
    list = avatar_check(ed.scene(), *rig);
    size_t origin = 0;
    for (size_t i = 0; i < list.size(); i++) origin = list[i].item == "At the origin" ? i + 1 : origin;
    CHECK(!get("At the origin").ok);
    ed.command(strprintf("avatar fix %zu", origin));
    list = avatar_check(ed.scene(), *rig);
    std::printf("    under a parent at (1, 0, 2): after its Fix the root at (%.3f, %.3f, %.3f)\n", rig->world_position().x, rig->world_position().y, rig->world_position().z);
    CHECK(get("At the origin").ok);
    ed.select_for_test(rig->id, false);
    ed.command("window Avatar");
    r34_steps(ed, 3);
    bool shown = false;
    for (const std::string &l : ed.button_labels_for_test()) shown = shown || l.rfind("shown:avatar Visemes ok", 0) == 0;
    CHECK(shown);
    ed.select_for_test(body->id, false);
    ed.command("shape add Still");
    MeshFilter *mf = body->get<MeshFilter>();
    if (mf && mf->mesh) {
      Mesh &mm = *mesh_make_mutable(mf->mesh);
      for (ShapeKey &k : mm.shape_keys)
        if (k.name == "Still") std::fill(k.delta.begin(), k.delta.end(), Vec3(0.0f));
      mm.touch();
    }
    ed.command("window Inspector");
    r34_steps(ed, 3);
    bool said = false;
    for (const std::string &l : ed.button_labels_for_test()) said = said || l.find("'Still' moves nothing") != std::string::npos;
    CHECK(said);
  });
  test("Humanoid and Quadruped Rig (review 356): the presets' legs as before at no height, and every bone scaled with a fitted figure - the four feet level and mirrored", [] {
    for (const float height : {0.0f, 1.52f}) {
      Scene s;
      GameObject *dog = create_rig(s, true, Vec3(0.0f), height);
      const float k = height > 0.0f ? height / 0.76f : 1.0f;
      const char *legs[4] = {"FrontLeftFoot", "FrontRightFoot", "HindLeftFoot", "HindRightFoot"};
      Vec3 at[4];
      bool all = dog != nullptr;
      for (int i = 0; i < 4; i++) {
        GameObject *f = s.find_by_name(legs[i]);
        all = all && f;
        at[i] = f ? f->world_position() : Vec3(0.0f);
      }
      std::printf("    a quadruped at %.2f m: feet at y %.3f %.3f %.3f %.3f, x %.3f %.3f\n", height, at[0].y, at[1].y, at[2].y, at[3].y, at[0].x, at[1].x);
      CHECK(all);
      for (int i = 0; i < 4; i++) CHECK(std::fabs(at[i].y - 0.04f * k) < 0.01f * k);
      CHECK(std::fabs(at[0].x + at[1].x) < 1e-3f && std::fabs(at[2].x + at[3].x) < 1e-3f && std::fabs(at[0].x - -0.12f * k) < 1e-3f);
      CHECK(std::fabs(at[0].z - 0.3f * k) < 0.02f * k && std::fabs(at[2].z + 0.3f * k) < 0.02f * k);
    }
  });
  test("Avatar kit (review 356): the PC rank's Fix cuts the avatar's own meshes (the rig selected, the body a sibling) and leaves the budget's target None", [] {
    if (!meshops::decimate_available()) return;  // (meshoptimizer: the libraries' build)
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("rig humanoid");
    GameObject *rig = ed.active_object_for_test();
    ed.command("create Sphere");
    GameObject *body = ed.active_object_for_test();
    CHECK(rig && body);
    if (!rig || !body) return;
    ed.command("subdivide 4");  // (about 190,000 triangles: Very Poor on PC)
    ed.command("component SkinModifier");
    ed.command("set SkinModifier.Armature " + rig->name);
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    const double before = budget_count({body}).value[(int)BudgetRow::Triangles];
    ed.select_for_test(rig->id, false);
    std::vector<AvatarCheck> list = avatar_check(ed.scene(), *rig);
    size_t rank = 0;
    for (size_t i = 0; i < list.size(); i++) rank = list[i].item == "VRChat PC rank" ? i + 1 : rank;
    ed.command(strprintf("avatar fix %zu", rank));
    r34_steps(ed, 2);
    const double after = budget_count({body}).value[(int)BudgetRow::Triangles];
    list = avatar_check(ed.scene(), *rig);
    std::printf("    the body: %.0f triangles before, %.0f after the rank's Fix (PC's Good: 70,000); the rank now %s\n", before, after, list[rank - 1].detail.c_str());
    CHECK(before > 70000.0 && after <= 70000.0 && list[rank - 1].ok);
    std::vector<LogEntry> log;
    const size_t since = Log::fetch(0, log);
    const std::string dir = fs::join(test_dir(), "r356");
    fs::make_dirs(dir);
    ed.command("export glb " + fs::join(dir, "avatar.glb"));
    log.clear();
    Log::fetch(since, log);
    bool ranked = false;
    for (const LogEntry &e : log) ranked = ranked || e.text.find("Budget (") != std::string::npos;
    CHECK(!ranked);  // (the target still None: the Fix didn't choose one)
  });
  test("the lesson's Try-it (round 356): create Sphere, three mouths, visemes gives the 15 keys", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    for (const char *c : {"create Sphere", "shape add AA", "shape add OH", "shape add CH"}) ed.command(c);
    ed.command("visemes");
    GameObject *g = ed.active_object_for_test();
    const MeshFilter *mf = g ? g->get<MeshFilter>() : nullptr;
    size_t made = 0;
    for (const std::string &v : vrchat_viseme_names()) made += mf && mf->mesh && r356_key(*mf->mesh, v);
    CHECK(made == 15);
  });
}

/* ---------------------------------------------------------------- round 357: Fix Model (the CATS plugin's) */
/* The body's skinned vertices in the world, as the views show them. */
static std::vector<Vec3> r357_skinned(GameObject &body) {
  std::vector<Vec3> out;
  const Mesh *m = body.evaluated_mesh();
  if (!m) return out;
  for (const Vec3 &p : m->positions) out.push_back(body.world_matrix().point(p));
  return out;
}

static GameObject *r357_bone(GameObject *parent, Scene &s, const char *name, Vec3 local) {
  GameObject *b = s.create(name, parent);
  Bone *bone = b->add<Bone>();
  bone->length = 0.1f;
  b->set_local_position(local);
  bone->set_rest(b->local());
  return b;
}

static void tests_round357() {
  test("Fix Model (round 357): Mixamo and MMD names become Unity's; a Center above the hips merges into Hips; a twist bone stays; bones that move nothing and IK go; the skinned body doesn't move; a clip's track follows its bone", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.command("create Cylinder");
    GameObject *body = ed.active_object_for_test();
    CHECK(body != nullptr);
    if (!body) return;
    ed.command("position 0 1 0");
    ed.command("rig humanoid");
    GameObject *rig = ed.active_object_for_test();
    Scene &s = ed.scene();
    GameObject *chest = s.find_by_name("Chest"), *neck = s.find_by_name("Neck"), *hips = s.find_by_name("Hips");
    GameObject *ua = s.find_by_name("LeftUpperArm"), *la = s.find_by_name("LeftLowerArm"), *head = s.find_by_name("Head");
    CHECK(rig && chest && neck && hips && ua && la && head);
    if (!rig || !chest || !neck || !hips || !ua || !la || !head) return;
    /* A bone above the hips (MMD's Center), a twist bone between the arm's two, a bone moving nothing. */
    GameObject *center = r357_bone(rig, s, "Center", hips->local().position * 0.9f);
    s.set_parent(hips, center, -1, true);
    hips->get<Bone>()->set_rest(hips->local());
    GameObject *twist = r357_bone(ua, s, "LeftArmTwist", la->local().position * 0.5f);
    s.set_parent(la, twist, -1, true);
    la->get<Bone>()->set_rest(la->local());
    r357_bone(hips, s, "Unused", Vec3(0.0f, -0.05f, 0.1f))->get<Bone>()->deform = false;  // (no group: it moves nothing)
    /* Other programs' names: Mixamo's prefix, MMD's Japanese head. */
    hips->name = "mixamorig:Hips";
    head->name = "\xE9\xA0\xAD";  // (頭)
    ed.select_for_test(body->id, false);
    ed.command("component SkinModifier");
    ed.command("set SkinModifier.Armature " + rig->name);
    ed.command("skin bind");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    /* A clip keying the upper arm, played from the rig. */
    std::remove(resolve_asset_path("Assets/Animations/R357Arm.anim").c_str());
    AnimationClipPtr clip = create_clip_asset("Assets/Animations/R357Arm.anim", "R357Arm");
    CHECK(clip != nullptr);
    if (!clip) return;
    const std::string arm_path = anim_path(*rig, *ua);
    clip->track(arm_path, "Rotation", 2).set_key(0, 30.0f);
    rig->add<Animation>()->clip = clip->asset_path;
    ed.mark_changed_for_test("key the arm");  // (an undo step of its own: Undo of Fix Model goes back to here)
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    /* The pose: the upper arm down 30 degrees - the IK off, or its hand target would pull the arm back before the fix
     * (and not after: Fix Model removes it). */
    s.for_each([](GameObject &g) {
      if (IKConstraint *ik = g.get<IKConstraint>()) ik->weight = 0.0f;
    });
    const uint64_t ua_id = ua->id, twist_id = twist->id, chest_id = chest->id;
    ua->set_local_rotation(ua->get<Bone>()->rest_rotation * Quat::axis_angle(Vec3(0, 0, 1), 0.52f));
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    const std::vector<Vec3> before = r357_skinned(*body);
    ed.select_for_test(rig->id, false);
    ed.command("avatar fixmodel");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    /* The same pose again on the same bone (Fix Model put the rig at rest). */
    GameObject *ua2 = s.find(ua_id);
    CHECK(ua2 != nullptr);
    if (!ua2) return;
    ua2->set_local_rotation(ua2->get<Bone>()->rest_rotation * Quat::axis_angle(Vec3(0, 0, 1), 0.52f));
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    GameObject *b2 = s.find(body->id);  // (one mesh: nothing joined, its name kept)
    const std::vector<Vec3> after = b2 ? r357_skinned(*b2) : std::vector<Vec3>();
    float worst = after.size() == before.size() ? 0.0f : 1e9f;
    for (size_t i = 0; i < after.size() && after.size() == before.size(); i++) worst = std::max(worst, length(after[i] - before[i]));
    const bool names = s.find_by_name("Hips") && s.find_by_name("Head") && !s.find_by_name("mixamorig:Hips");
    const bool merged = !s.find_by_name("Center") && s.find_by_name("Hips") && s.find_by_name("Hips")->parent == rig && s.find(chest_id);
    bool iks = false;
    s.for_each([&](GameObject &g) { iks = iks || g.get<IKConstraint>(); });
    const MeshFilter *bmf = b2 ? b2->get<MeshFilter>() : nullptr;
    const bool group = bmf && bmf->mesh && bmf->mesh->find_group("Center") < 0 && bmf->mesh->find_group("Hips") >= 0 && s.find_by_name("LeftHand");
    const AnimTrack *t = clip->find(anim_path(*rig, *ua2), "Rotation", 2);
    std::printf("    names %d, merged %d, twist kept %d, Unused gone %d, IK left %d, groups %d, the track at %s; the posed body moved %.6f\n", (int)names, (int)merged,
                (int)(s.find(twist_id) != nullptr), (int)!s.find_by_name("Unused"), (int)iks, (int)group, t ? t->path.c_str() : "-", worst);
    CHECK(names && merged && s.find(twist_id) && !s.find_by_name("Unused") && !iks && group && t);
    CHECK(worst < 1e-3f);
    /* One Undo step brings the rig back as it was. */
    ed.command("undo");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    CHECK(ed.scene().find_by_name("Center") && ed.scene().find_by_name("mixamorig:Hips"));
    AnimationClipPtr back = clip_asset("Assets/Animations/R357Arm.anim");
    CHECK(back && back->find(arm_path, "Rotation", 2));  // (the clip's track back on its old path too)
  });
  test("Fix Model (round 357): the avatar's meshes joined into Body with the face's shape keys and both meshes' weights; two slots of one material become one", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("rig humanoid");
    GameObject *rig = ed.active_object_for_test();
    ed.command("create Cube");
    GameObject *torso = ed.active_object_for_test();
    ed.command("subdivide 2");
    ed.command("create Sphere");
    GameObject *face = ed.active_object_for_test();
    CHECK(rig && torso && face);
    if (!rig || !torso || !face) return;
    ed.command("position 0 1.7 0");
    {
      Mesh &fm = *mesh_make_mutable(face->get<MeshFilter>()->mesh);  // (the face's own key, the torso none)
      fm.shape_keys = {{"Smile", std::vector<Vec3>(fm.vert_count(), Vec3(0.0f, 0.05f, 0.0f))}};
      fm.touch();
    }
    CHECK(torso->get<MeshFilter>()->mesh->shape_keys.empty());
    for (GameObject *g : {torso, face}) {
      ed.select_for_test(g->id, false);
      ed.command("component SkinModifier");
      ed.command("set SkinModifier.Armature " + rig->name);
      ed.command("skin bind");
      for (int i = 0; i < 3; i++) ed.step_frame_headless();
    }
    /* The same material twice on the torso, as an import often leaves it. */
    MeshRenderer *tr = torso->get<MeshRenderer>();
    CHECK(tr != nullptr);
    if (!tr) return;
    MaterialPtr skin = std::make_shared<Material>();
    skin->name = "Skin";
    MaterialPtr copy = std::make_shared<Material>(*skin);
    copy->name = "Skin.001";
    tr->materials = {skin, copy};
    Mesh &tm = *mesh_make_mutable(torso->get<MeshFilter>()->mesh);
    tm.face_material.assign(tm.face_count(), 0);
    for (size_t f = 0; f < tm.face_count(); f += 2) tm.face_material[f] = 1;
    tm.touch();
    const size_t tv = torso->get<MeshFilter>()->mesh->vert_count(), fv = face->get<MeshFilter>()->mesh->vert_count();
    ed.select_for_test(rig->id, false);
    ed.command("avatar fixmodel");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    GameObject *body = ed.scene().find_by_name("Body");
    const MeshFilter *mf = body ? body->get<MeshFilter>() : nullptr;
    const Mesh *m = mf ? mf->mesh.get() : nullptr;
    const int smile = m ? m->find_shape("Smile") : -1;
    /* The face's vertices move with Smile, the torso's don't: as many moving offsets as the face has vertices. */
    size_t moving = 0;
    for (size_t i = 0; m && smile >= 0 && i < m->vert_count(); i++) moving += length(m->shape_keys[(size_t)smile].delta[i]) > 0.0f ? 1 : 0;
    std::printf("    Body: %zu vertices (%zu + %zu), Smile at %d moving %zu of them (the face's %zu), keys fit %d, groups fit %d, %zu slot(s)\n", m ? m->vert_count() : (size_t)0,
                tv, fv, smile, moving, fv, m ? (int)m->keys_fit() : -1, m ? (int)m->groups_fit() : -1, body && body->get<MeshRenderer>() ? body->get<MeshRenderer>()->materials.size() : (size_t)0);
    CHECK(m && m->vert_count() == tv + fv && smile >= 0 && moving == fv && m->keys_fit() && m->groups_fit());
    CHECK(body && body->get<MeshRenderer>() && body->get<MeshRenderer>()->materials.size() == 1);
    CHECK(!ed.scene().find(face->id) || ed.scene().find(face->id) == body);
  });
  test("Fix Model (review 357): MMD's split body - the upper body beside the lower, both under the waist - comes out Hips > Spine; a clip's keys on the moved bones still put the head where they did; an animated Center stays", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("rig humanoid");
    GameObject *rig = ed.active_object_for_test();
    Scene &s = ed.scene();
    GameObject *hips = s.find_by_name("Hips"), *spine = s.find_by_name("Spine"), *head = s.find_by_name("Head");
    CHECK(rig && hips && spine && head);
    if (!rig || !hips || !spine || !head) return;
    /* armature > Waist > {LowerBody (the hips, the legs), UpperBody (the spine)}: MMD's 腰 > {下半身, 上半身}. */
    GameObject *waist = r357_bone(rig, s, "\xE8\x85\xB0", hips->local().position);  // (腰)
    const uint64_t waist_id = waist->id;
    s.set_parent(hips, waist, -1, true);
    hips->get<Bone>()->set_rest(hips->local());
    s.set_parent(spine, waist, -1, true);
    spine->get<Bone>()->set_rest(spine->local());
    hips->name = "\xE4\xB8\x8B\xE5\x8D\x8A\xE8\xBA\xAB";   // (下半身)
    spine->name = "\xE4\xB8\x8A\xE5\x8D\x8A\xE8\xBA\xAB";  // (上半身)
    /* A clip turning the upper body and lifting the lower: keys in the waist's space. */
    std::remove(resolve_asset_path("Assets/Animations/R357Mmd.anim").c_str());
    AnimationClipPtr clip = create_clip_asset("Assets/Animations/R357Mmd.anim", "R357Mmd");
    CHECK(clip != nullptr);
    if (!clip) return;
    const std::string sp = anim_path(*rig, *spine);
    clip->track(sp, "Rotation", 0).set_key(10, 20.0f);
    clip->track(sp, "Rotation", 2).set_key(10, -15.0f);
    rig->add<Animation>()->clip = clip->asset_path;
    ed.mark_changed_for_test("key the body");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    /* The head where the clip's frame 10 puts it, before and after. */
    const uint64_t head_id = head->id;
    auto head_at = [&](AnimationClip &c) {
      for (const AnimTrack &t : c.tracks)
        if (GameObject *g = anim_target(*rig, t.path)) write_anim_value(*g, t.property, t.axis, t.sample(10.0f));
      return s.find(head_id)->world_position();
    };
    const Vec3 before = head_at(*clip);
    ed.select_for_test(rig->id, false);
    ed.command("avatar fixmodel");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    GameObject *h2 = s.find_by_name("Hips"), *s2 = s.find_by_name("Spine");
    const Vec3 after = head_at(*clip);
    std::printf("    the spine's parent %s, the waist %s; the head at frame 10 (%.3f %.3f %.3f) before, (%.3f %.3f %.3f) after\n", s2 && s2->parent ? s2->parent->name.c_str() : "-",
                s.find(waist_id) ? "kept" : "merged", before.x, before.y, before.z, after.x, after.y, after.z);
    CHECK(h2 && s2 && s2->parent == h2 && !s.find(waist_id));
    CHECK(length(after - before) < 1e-3f);
    /* A Center the clip moves isn't merged: its root motion would have nowhere to go. */
    Editor ed2;
    ed2.init_headless(1200, 900);
    ed2.command("rig humanoid");
    GameObject *rig2 = ed2.active_object_for_test();
    GameObject *hips2 = ed2.scene().find_by_name("Hips");
    CHECK(rig2 && hips2);
    if (!rig2 || !hips2) return;
    GameObject *center = r357_bone(rig2, ed2.scene(), "Center", hips2->local().position * 0.5f);
    ed2.scene().set_parent(hips2, center, -1, true);
    hips2->get<Bone>()->set_rest(hips2->local());
    std::remove(resolve_asset_path("Assets/Animations/R357Center.anim").c_str());
    AnimationClipPtr dance = create_clip_asset("Assets/Animations/R357Center.anim", "R357Center");
    if (!dance) return;
    dance->track(anim_path(*rig2, *center), "Position", 2).set_key(30, 2.0f);
    rig2->add<Animation>()->clip = dance->asset_path;
    ed2.select_for_test(rig2->id, false);
    ed2.command("avatar fixmodel");
    CHECK(ed2.scene().find(center->id) != nullptr);
  });
  test("Fix Model (review 357): the body under a smaller mesh is joined safely; a mesh with a Mirror isn't joined (said); with Remove Constraints off the IK and its targets stay", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("rig humanoid");
    GameObject *rig = ed.active_object_for_test();
    ed.command("create Cube");
    GameObject *hair = ed.active_object_for_test();
    ed.command("create Sphere");
    GameObject *body = ed.active_object_for_test();
    ed.command("create Cylinder");
    GameObject *cape = ed.active_object_for_test();
    CHECK(rig && hair && body && cape);
    if (!rig || !hair || !body || !cape) return;
    ed.scene().set_parent(body, hair, -1, true);  // (the bigger mesh below the smaller)
    for (GameObject *g : {hair, body, cape}) {
      ed.select_for_test(g->id, false);
      ed.command("component SkinModifier");
      ed.command("set SkinModifier.Armature " + rig->name);
      ed.command("skin bind");
      for (int i = 0; i < 3; i++) ed.step_frame_headless();
    }
    ed.select_for_test(cape->id, false);
    ed.command("component MirrorModifier");
    const size_t sv = body->get<MeshFilter>()->mesh->vert_count(), hv = hair->get<MeshFilter>()->mesh->vert_count();
    std::vector<LogEntry> log;
    const size_t since = Log::fetch(0, log);
    ed.select_for_test(rig->id, false);
    ed.command("avatar fixmodel");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    log.clear();
    Log::fetch(since, log);
    bool said = false;
    for (const LogEntry &e : log) said = said || e.text.find("not joined: its MirrorModifier") != std::string::npos;
    GameObject *b = ed.scene().find(body->id);
    std::printf("    Body %zu vertices (%zu + %zu), the cape kept apart %d, said %d\n", b ? b->get<MeshFilter>()->mesh->vert_count() : (size_t)0, sv, hv, (int)(ed.scene().find(cape->id) != nullptr), (int)said);
    CHECK(b && b->get<MeshFilter>()->mesh->vert_count() == sv + hv && ed.scene().find(cape->id) && said);
    /* Remove Constraints off: the IK keeps its targets (they move nothing, and stayed). */
    Editor ed2;
    ed2.init_headless(1200, 900);
    ed2.command("rig humanoid");
    GameObject *rig2 = ed2.active_object_for_test();
    if (!rig2) return;
    ed2.fix_model_options_for_test().remove_constraints = false;
    ed2.command("avatar fixmodel");
    size_t iks = 0, reached = 0;
    ed2.scene().for_each([&](GameObject &g) {
      const IKConstraint *ik = g.get<IKConstraint>();
      if (!ik) return;
      iks++;
      reached += ed2.scene().find_by_name(ik->target) ? 1 : 0;
    });
    std::printf("    with the constraints kept: %zu IKs, %zu with their targets\n", iks, reached);
    CHECK(iks == 4 && reached == 4);
  });
  test("the lesson's Try-it (round 357): rig humanoid, then avatar fixmodel - no IK left, the Humanoid bones kept", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("rig humanoid");
    ed.command("avatar fixmodel");
    bool iks = false;
    ed.scene().for_each([&](GameObject &g) { iks = iks || g.get<IKConstraint>(); });
    CHECK(!iks && ed.scene().find_by_name("Hips") && ed.scene().find_by_name("LeftHand") && ed.scene().find_by_name("Head"));
  });
  test("Join (round 357): shape keys carried through by name - the joined object's keys moved into the result's space, zero where a mesh has none", [] {
    Mesh a = *primitives::cube(), b = *primitives::cube();
    a.shape_keys = {{"Up", std::vector<Vec3>(a.vert_count(), Vec3(0, 0.1f, 0))}};
    b.shape_keys = {{"Wide", std::vector<Vec3>(b.vert_count(), Vec3(0.1f, 0, 0))}};
    const size_t na = a.vert_count();
    Mesh plain = a;
    meshops::append_mesh(plain, b, Mat4::identity(), {});  // (the Z-fight check's and Scatter's: no keys joined)
    CHECK(plain.find_shape("Wide") < 0 && plain.shape_keys[0].delta.size() == na);
    meshops::append_mesh(a, b, Mat4::trs(Vec3(2, 0, 0), Quat::axis_angle(Vec3(0, 1, 0), 1.5707963f), Vec3(1.0f)), {}, true);
    const int up = a.find_shape("Up"), wide = a.find_shape("Wide");
    CHECK(up >= 0 && wide >= 0 && a.keys_fit());
    if (up < 0 || wide < 0 || !a.keys_fit()) return;
    std::printf("    Up on the joined part %.3f; Wide on the first part %.3f, on the joined part (%.3f %.3f %.3f)\n", length(a.shape_keys[(size_t)up].delta[na]),
                length(a.shape_keys[(size_t)wide].delta[0]), a.shape_keys[(size_t)wide].delta[na].x, a.shape_keys[(size_t)wide].delta[na].y, a.shape_keys[(size_t)wide].delta[na].z);
    CHECK(length(a.shape_keys[(size_t)up].delta[na]) == 0.0f && length(a.shape_keys[(size_t)wide].delta[0]) == 0.0f);
    CHECK(std::fabs(a.shape_keys[(size_t)wide].delta[na].z + 0.1f) < 1e-4f && std::fabs(a.shape_keys[(size_t)wide].delta[na].x) < 1e-4f);  // (X turned 90 degrees about Y: -Z)
  });
}

/* ---------------------------------------------------------------- round 358: Model Options, Pose and Shape tools */
/* A rig with a skinned cylinder for a body; its upper left arm turned 40 degrees down. */
struct R358Avatar {
  GameObject *rig = nullptr, *body = nullptr, *arm = nullptr;
};
static R358Avatar r358_avatar(Editor &ed, bool pose) {
  R358Avatar a;
  ed.command("create Cylinder");
  a.body = ed.active_object_for_test();
  ed.command("position 0 1 0");
  ed.command("rig humanoid");
  a.rig = ed.active_object_for_test();
  a.arm = ed.scene().find_by_name("LeftUpperArm");
  if (!a.body || !a.rig || !a.arm) return a;
  ed.scene().for_each([](GameObject &g) {
    if (IKConstraint *ik = g.get<IKConstraint>()) ik->weight = 0.0f;  // (the hand's IK would pull the arm back)
  });
  ed.select_for_test(a.body->id, false);
  ed.command("component SkinModifier");
  ed.command("set SkinModifier.Armature " + a.rig->name);
  ed.command("skin bind");
  for (int i = 0; i < 3; i++) ed.step_frame_headless();
  if (pose) a.arm->set_local_rotation(a.arm->get<Bone>()->rest_rotation * Quat::axis_angle(Vec3(0, 0, 1), -0.7f));
  for (int i = 0; i < 2; i++) ed.step_frame_headless();
  return a;
}

static float r358_qdot(Quat a, Quat b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

static float r358_worst(const std::vector<Vec3> &a, const std::vector<Vec3> &b) {
  if (a.size() != b.size()) return 1e9f;
  float w = 0.0f;
  for (size_t i = 0; i < a.size(); i++) w = std::max(w, length(a[i] - b[i]));
  return w;
}

static void tests_round358() {
  test("Pose and Shape (round 358): Pose to Shape Key keeps the skinned pose; Apply as Rest Pose leaves the body where the pose had it, a key made at rest still moving it the same", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    R358Avatar a = r358_avatar(ed, true);
    CHECK(a.rig && a.body && a.arm);
    if (!a.rig || !a.body || !a.arm) return;
    /* A key made at rest: the whole body 5 cm up. */
    {
      Mesh &m = *mesh_make_mutable(a.body->get<MeshFilter>()->mesh);
      m.shape_keys.push_back({"Up", std::vector<Vec3>(m.vert_count(), Vec3(0, 0.05f, 0))});
      m.touch();
    }
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    const std::vector<Vec3> posed_plain = r357_skinned(*a.body);  // (the evaluated body, Up at 0)
    ed.select_for_test(a.body->id, false);
    ed.command("avatar posekey Wave");
    a.body->get<MeshRenderer>()->set_shape_weight("Up", 1.0f);
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    const std::vector<Vec3> posed = r357_skinned(*a.body);
    const Mesh &pm = *a.body->get<MeshFilter>()->mesh;
    const int wave = pm.find_shape("Wave");
    float key_err = wave >= 0 && posed_plain.size() == pm.vert_count() ? 0.0f : 1e9f;
    for (size_t i = 0; key_err == 0.0f && i < pm.vert_count(); i++)
      key_err = std::max(key_err, length(a.body->world_matrix().point(pm.positions[i] + pm.shape_keys[(size_t)wave].delta[i]) - posed_plain[i]));
    ed.command("avatar applyrest");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    const std::vector<Vec3> after = r357_skinned(*a.body);
    const Quat rest = a.arm->get<Bone>()->rest_rotation, now = a.arm->local().rotation;
    std::printf("    Wave matches the skinned pose to %.6f; after Apply as Rest the body (Up at 1) moved %.6f; the arm's rest is its pose %d\n", key_err, r358_worst(posed, after),
                (int)(std::fabs(r358_qdot(rest, now)) > 0.9999f));
    CHECK(key_err < 1e-4f && r358_worst(posed, after) < 1e-3f && std::fabs(r358_qdot(rest, now)) > 0.9999f);
    ed.command("undo");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    CHECK(ed.scene().find(a.arm->id) && std::fabs(r358_qdot(ed.scene().find(a.arm->id)->get<Bone>()->rest_rotation, now)) < 0.9999f);
  });
  test("Pose and Shape (round 358): Apply Shape Key as Basis with a Reverted key; Sort Shape Keys in VRChat's order", [] {
    Mesh m = *primitives::cube();
    const size_t n = m.vert_count();
    const Vec3 p0 = m.positions[0];
    m.shape_keys = {{"Smile", std::vector<Vec3>(n, Vec3(0.1f, 0, 0))}, {"vrc.v_ou", std::vector<Vec3>(n, Vec3(0, 0.1f, 0))},
                    {"vrc.blink_left", std::vector<Vec3>(n, Vec3(0, 0, 0.1f))}, {"vrc.v_aa", std::vector<Vec3>(n, Vec3(0, 0.2f, 0))}};
    std::string why;
    CHECK(apply_shape_key_as_basis(m, "Smile", why));
    const int rev = m.find_shape("Smile - Reverted");
    CHECK(m.find_shape("Smile") < 0 && rev >= 0 && std::fabs(m.positions[0].x - (p0.x + 0.1f)) < 1e-6f && rev >= 0 && std::fabs(m.shape_keys[(size_t)rev].delta[0].x + 0.1f) < 1e-6f);
    CHECK(!apply_shape_key_as_basis(m, "Nope", why) && why.find("Nope") != std::string::npos);
    sort_shape_keys(m);
    std::string order;
    for (const ShapeKey &k : m.shape_keys) order += k.name + ";";
    std::printf("    sorted: %s\n", order.c_str());
    CHECK(order == "vrc.v_aa;vrc.v_ou;vrc.blink_left;Smile - Reverted;");
  });
  test("Pose and Shape (round 358): A-Pose drops the arms 45 degrees and T-Pose levels them, applied as the rest; Connect Bones points a bone at its one child, the body unchanged", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    R358Avatar a = r358_avatar(ed, false);
    CHECK(a.rig && a.body);
    if (!a.rig || !a.body) return;
    const std::vector<Vec3> before = r357_skinned(*a.body);
    ed.select_for_test(a.rig->id, false);
    ed.command("avatar apose");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    const humanoid::Map am = humanoid::map(ed.scene(), *a.rig);
    const float followed = r358_worst(before, r357_skinned(*a.body));
    ed.command("avatar tpose");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    const humanoid::Map tm = humanoid::map(ed.scene(), *a.rig);
    std::printf("    A-Pose: the arms %.1f and %.1f degrees down; T-Pose: %.1f and %.1f\n", am.arm_drop[0], am.arm_drop[1], tm.arm_drop[0], tm.arm_drop[1]);
    CHECK(std::fabs(am.arm_drop[0] - 45.0f) < 3.0f && std::fabs(am.arm_drop[1] - 45.0f) < 3.0f && followed > 0.01f);
    CHECK(std::fabs(tm.arm_drop[0]) < 3.0f && std::fabs(tm.arm_drop[1]) < 3.0f);
    /* Connect Bones: Spine (one child, Chest) as long as the gap to it; the body where it was. */
    const std::vector<Vec3> rest = r357_skinned(*a.body);
    GameObject *sp0 = ed.scene().find_by_name("Spine");
    if (sp0) sp0->set_local_rotation(sp0->get<Bone>()->rest_rotation * Quat::axis_angle(Vec3(1, 0, 0), 0.5f));  // (posed: Connect starts from the rest)
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    ed.command("avatar connect");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    GameObject *spine = ed.scene().find_by_name("Spine"), *chest = ed.scene().find_by_name("Chest");
    CHECK(spine && chest);
    if (!spine || !chest) return;
    const float gap = length(chest->world_position() - spine->world_position());
    const Vec3 y = normalize(spine->world_matrix().dir(Vec3(0, 1, 0)));
    std::printf("    Spine: length %.4f, the gap %.4f, pointing at Chest %.4f; the body moved %.6f\n", spine->get<Bone>()->length * bone_world_unit(*spine), gap,
                dot(y, normalize(chest->world_position() - spine->world_position())), r358_worst(rest, r357_skinned(*a.body)));
    CHECK(std::fabs(spine->get<Bone>()->length * bone_world_unit(*spine) - gap) < 1e-3f && dot(y, normalize(chest->world_position() - spine->world_position())) > 0.9999f);
    CHECK(r358_worst(rest, r357_skinned(*a.body)) < 1e-3f);
  });
  test("Model Options (round 358): Separate by Material and by Shape Keys (the keyed part keeps its keys); Merge Weights; Apply Transforms turns a key's offsets with the vertices", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("create Cube");
    GameObject *cube = ed.active_object_for_test();
    CHECK(cube != nullptr);
    if (!cube) return;
    {
      Mesh &m = *mesh_make_mutable(cube->get<MeshFilter>()->mesh);
      m.face_material.assign(m.face_count(), 0);
      m.face_material[0] = 1;
      std::vector<Vec3> d(m.vert_count(), Vec3(0.0f));
      for (size_t i = 0; i < m.vert_count(); i++) d[i] = m.positions[i].y > 0.0f ? Vec3(0, 0.1f, 0) : Vec3(0.0f);
      m.shape_keys = {{"Top", d}};
      m.touch();
    }
    MeshRenderer *mr = cube->get<MeshRenderer>();
    mr->materials = {std::make_shared<Material>(), std::make_shared<Material>()};
    mr->materials[1]->base_color = Vec3(1, 0, 0);
    size_t objects = 0;
    ed.scene().for_each([&](GameObject &) { objects++; });
    ed.select_for_test(cube->id, false);
    ed.command("separate material");
    size_t after = 0;
    ed.scene().for_each([&](GameObject &) { after++; });
    CHECK(after == objects + 1);
    /* By shape keys: the top's faces (any vertex the key moves) apart; each part's keys fit it. */
    ed.command("create Cube");
    GameObject *c2 = ed.active_object_for_test();
    {
      Mesh &m = *mesh_make_mutable(c2->get<MeshFilter>()->mesh);
      m.shape_keys = {{"Lift", std::vector<Vec3>(m.vert_count(), Vec3(0.0f))}};
      m.shape_keys[0].delta[0] = Vec3(0, 0.1f, 0);
      m.touch();
    }
    const size_t faces = c2->get<MeshFilter>()->mesh->face_count();
    ed.mark_changed_for_test("key the cube");  // (its own undo step: Undo of Separate comes back to here)
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    ed.select_for_test(c2->id, false);
    ed.command("separate shapekeys");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    GameObject *part = nullptr;
    ed.scene().for_each([&](GameObject &g) { part = &g != c2 && g.name.rfind(c2->name, 0) == 0 && g.get<MeshFilter>() ? &g : part; });
    const Mesh *keyed = c2->get<MeshFilter>()->mesh.get(), *rest = part ? part->get<MeshFilter>()->mesh.get() : nullptr;
    std::printf("    by shape keys: %zu faces -> %zu keyed (kept, %zu key(s), fitting %d) + %zu without (%zu key(s))\n", faces, keyed->face_count(), keyed->shape_keys.size(),
                (int)keyed->keys_fit(), rest ? rest->face_count() : (size_t)0, rest ? rest->shape_keys.size() : (size_t)99);
    CHECK(rest && rest->face_count() + keyed->face_count() == faces && keyed->face_count() == 3 && keyed->keys_fit() && keyed->find_shape("Lift") >= 0 && rest->shape_keys.empty());
    ed.command("undo");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    CHECK(ed.scene().find(c2->id) && ed.scene().find(c2->id)->get<MeshFilter>()->mesh->face_count() == faces);
    /* Apply Transforms: a quarter turn about Y goes into the vertices and the key's offsets; the keyed shape in the world unchanged. */
    ed.command("create Cube");
    GameObject *c3 = ed.active_object_for_test();
    {
      Mesh &m = *mesh_make_mutable(c3->get<MeshFilter>()->mesh);
      m.shape_keys = {{"Wide", std::vector<Vec3>(m.vert_count(), Vec3(0.1f, 0, 0))}};
      m.touch();
    }
    c3->set_local_rotation(Quat::axis_angle(Vec3(0, 1, 0), 1.5707963f));
    ed.select_for_test(c3->id, false);
    ed.command("meshop apply_transform");
    const Mesh &m3 = *c3->get<MeshFilter>()->mesh;
    std::printf("    after Apply Transform: Wide's offset (%.3f %.3f %.3f)\n", m3.shape_keys[0].delta[0].x, m3.shape_keys[0].delta[0].y, m3.shape_keys[0].delta[0].z);
    CHECK(std::fabs(m3.shape_keys[0].delta[0].z + 0.1f) < 1e-4f && std::fabs(m3.shape_keys[0].delta[0].x) < 1e-4f);
    /* Merge Weights: a bone's group added into its parent's; the bone gone. */
    Editor e2;
    e2.init_headless(1200, 900);
    R358Avatar a = r358_avatar(e2, false);
    if (!a.rig || !a.body) return;
    GameObject *neck = e2.scene().find_by_name("Spine");  // (into Hips: both weighted on the body)
    const uint64_t neck_id = neck ? neck->id : 0;
    const float sum_before = [&] {
      float s = 0.0f;
      const Mesh &bm = *a.body->get<MeshFilter>()->mesh;
      for (const char *g : {"Spine", "Hips"})
        if (bm.find_group(g) >= 0)
          for (float w : bm.vertex_groups[(size_t)bm.find_group(g)].weights) s += w;
      return s;
    }();
    e2.select_for_test(neck_id, false);
    e2.command("avatar mergeweights");
    const Mesh &bm = *a.body->get<MeshFilter>()->mesh;
    float chest = 0.0f;
    if (bm.find_group("Hips") >= 0)
      for (float w : bm.vertex_groups[(size_t)bm.find_group("Hips")].weights) chest += w;
    std::printf("    Merge Weights: Spine %s, Spine + Hips %.3f before, Hips %.3f after\n", e2.scene().find(neck_id) ? "kept" : "merged", sum_before, chest);
    CHECK(!e2.scene().find(neck_id) && bm.find_group("Spine") < 0 && sum_before > 1.0f && std::fabs(chest - sum_before) < 1e-3f);
  });
  test("Model Options (review 358): Remove Doubles leaves coinciding vertices a key pulls apart (a closed mouth); a key named with spaces; the verbs run", [] {
    Mesh m;
    m.positions = {Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 1, 0), Vec3(1, 1, 0), Vec3(0, 2, 0), Vec3(0, 0, 0)};
    const uint32_t f0[3] = {0, 1, 2}, f1[3] = {3, 4, 5}, f2[3] = {6, 4, 1};
    m.add_face(f0, 3), m.add_face(f1, 3), m.add_face(f2, 3);
    m.shape_keys = {{"Open", std::vector<Vec3>(m.positions.size(), Vec3(0.0f))}};
    m.shape_keys[0].delta[3] = Vec3(0, 0.2f, 0);  // (the lower lip: vertex 3 sits on 2 until Open moves it)
    const size_t merged = remove_doubles_keeping_keys(m, 0.001f);
    std::printf("    Remove Doubles: %zu merged, %zu vertices left, the key fits %d\n", merged, m.vert_count(), (int)m.keys_fit());
    CHECK(merged == 1 && m.vert_count() == 6 && m.keys_fit());  // (6 onto 0 merged; 3 kept apart from 2)
    Editor ed;
    ed.init_headless(1200, 900);
    R358Avatar a = r358_avatar(ed, true);
    if (!a.rig || !a.body) return;
    ed.select_for_test(a.body->id, false);
    ed.command("avatar posekey Mouth Open");
    CHECK(a.body->get<MeshFilter>()->mesh->find_shape("Mouth Open") >= 0);
    ed.command("avatar basis Mouth Open");
    CHECK(a.body->get<MeshFilter>()->mesh->find_shape("Mouth Open - Reverted") >= 0);
    for (const char *c : {"avatar zeroweight", "avatar join", "avatar normals", "avatar normals flip", "avatar removedoubles", "avatar sortkeys", "avatar applytransforms"}) {
      ed.select_for_test(a.body->id, false);
      ed.command(c);
    }
    CHECK(ed.scene().find(a.body->id) != nullptr);
  });
  test("Pose and Shape (review 358): with the Armature showing its Rest Position, Apply as Rest Pose and A-Pose refuse; Connect Bones leaves a bone a clip moves", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    R358Avatar a = r358_avatar(ed, true);
    if (!a.rig || !a.arm) return;
    a.rig->get<Armature>()->rest_position = true;
    const Quat rest = a.arm->get<Bone>()->rest_rotation;
    ed.select_for_test(a.rig->id, false);
    ed.command("avatar applyrest");
    ed.command("avatar apose");
    CHECK(r358_qdot(a.arm->get<Bone>()->rest_rotation, rest) > 0.9999f);
    a.rig->get<Armature>()->rest_position = false;
    /* A clip keying the Chest: Connect leaves the Spine (its parent) as it is. */
    std::remove(resolve_asset_path("Assets/Animations/R358Chest.anim").c_str());
    AnimationClipPtr clip = create_clip_asset("Assets/Animations/R358Chest.anim", "R358Chest");
    GameObject *spine = ed.scene().find_by_name("Spine"), *chest = ed.scene().find_by_name("Chest");
    if (!clip || !spine || !chest) return;
    clip->track(anim_path(*a.rig, *chest), "Rotation", 0).set_key(0, 5.0f);
    a.rig->add<Animation>()->clip = clip->asset_path;
    const Quat spine_rest = spine->get<Bone>()->rest_rotation;
    ed.command("avatar connect");
    CHECK(r358_qdot(spine->get<Bone>()->rest_rotation, spine_rest) > 0.99999f);
  });
  test("the lesson's Try-it (round 358): rig humanoid, then avatar apose - the arms 45 degrees down", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("rig humanoid");
    GameObject *rig = ed.active_object_for_test();
    ed.command("avatar apose");
    CHECK(rig && std::fabs(humanoid::map(ed.scene(), *rig).arm_drop[0] - 45.0f) < 3.0f);
  });
}

/* ---------------------------------------------------------------- round 359: Decimation modes, Bone Root, Bone Merging */
/* A chain of `n` bones under `parent`, 6 cm apart up its Y, a SpringBone on its first. */
static GameObject *r359_chain(Scene &s, GameObject *parent, const std::string &name, Vec3 at, int n) {
  GameObject *first = nullptr, *p = parent;
  for (int i = 0; i < n; i++) {
    GameObject *b = r357_bone(p, s, (name + std::to_string(i + 1)).c_str(), i == 0 ? at : Vec3(0, 0.06f, 0));
    if (!first) first = b;
    p = b;
  }
  first->add<SpringBone>();
  return first;
}

static void tests_round359() {
  test("Decimation (round 359): CATS' modes choose the meshes - Safe only those without keys, Half under 4 keys, Smart and Full all - and Exclude keeps a face whole", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("rig humanoid");
    GameObject *rig = ed.active_object_for_test();
    std::vector<GameObject *> meshes;
    for (int keys : {0, 2, 5}) {
      ed.command("create Sphere");
      GameObject *g = ed.active_object_for_test();
      if (!g || !rig) return;
      g->name = keys == 5 ? "Face" : strprintf("Part%d", keys);
      Mesh &m = *mesh_make_mutable(g->get<MeshFilter>()->mesh);
      for (int k = 0; k < keys; k++) m.shape_keys.push_back({strprintf("K%d", k), std::vector<Vec3>(m.vert_count(), Vec3(0, 0.01f, 0))});
      m.touch();
      g->add<SkinModifier>()->object = rig->name;
      meshes.push_back(g);
    }
    auto names = [&](DecimateMode mode, std::vector<std::string> ex) {
      std::string out;
      for (GameObject *g : decimate_candidates(ed.scene(), *rig, mode, ex)) out += g->name + ";";
      return out;
    };
    std::printf("    Safe %s  Half %s  Smart %s  Full %s  Smart without Face %s\n", names(DecimateMode::Safe, {}).c_str(), names(DecimateMode::Half, {}).c_str(),
                names(DecimateMode::Smart, {}).c_str(), names(DecimateMode::Full, {}).c_str(), names(DecimateMode::Smart, {"Face"}).c_str());
    CHECK(names(DecimateMode::Safe, {}) == "Part0;" && names(DecimateMode::Half, {}) == "Part0;Part2;" && names(DecimateMode::Smart, {}) == "Part0;Part2;Face;");
    CHECK(names(DecimateMode::Full, {}) == "Part0;Part2;Face;" && names(DecimateMode::Smart, {"Face"}) == "Part0;Part2;");
    DecimateMode m;
    CHECK(decimate_mode_named("save", m) && m == DecimateMode::Safe && !decimate_mode_named("nope", m));
    /* A Full cut that doesn't happen (it fits already; or this build can't cut) leaves every key (review 359). */
    ed.select_for_test(rig->id, false);
    ed.command("avatar decimate full 999999999");
    CHECK(meshes[2]->get<MeshFilter>()->mesh->shape_keys.size() == 5 && meshes[1]->get<MeshFilter>()->mesh->shape_keys.size() == 2);
    /* An unskinned hat under the head is a mesh the modes may cut, as the checklist's Fix had cut it. */
    ed.command("create Cube");
    GameObject *hat = ed.active_object_for_test();
    if (hat && ed.scene().find_by_name("Head")) ed.scene().set_parent(hat, ed.scene().find_by_name("Head"), -1, true);
    CHECK(hat && names(DecimateMode::Safe, {}) == "Part0;" + hat->name + ";");
    if (!meshops::decimate_available()) return;  // (the cut itself: meshoptimizer, the libraries' build)
    for (GameObject *g : meshes) {
      ed.select_for_test(g->id, false);
      ed.command("subdivide 1");
    }
    ed.select_for_test(rig->id, false);
    const double before = budget_count(avatar_objects(ed.scene(), *rig)).value[(int)BudgetRow::Triangles];
    const double face = budget_count({meshes[2]}).value[(int)BudgetRow::Triangles];
    ed.command(strprintf("avatar decimate smart %.0f Face", before * 0.6));
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    const double after = budget_count(avatar_objects(ed.scene(), *rig)).value[(int)BudgetRow::Triangles];
    std::printf("    Smart to %.0f without Face: %.0f -> %.0f triangles; Face %.0f -> %.0f, its Decimate %d\n", before * 0.6, before, after, face,
                budget_count({meshes[2]}).value[(int)BudgetRow::Triangles], (int)(meshes[2]->get<DecimateModifier>() != nullptr));
    CHECK(after <= before * 0.6 && after > before * 0.45 && !meshes[2]->get<DecimateModifier>());
    ed.command(strprintf("avatar decimate full %.0f", before * 0.3));
    CHECK(meshes[2]->get<MeshFilter>()->mesh->shape_keys.empty() && meshes[2]->get<DecimateModifier>());
  });
  test("Bone Root and Bone Merging (round 359): hair chains with one parent and one name go under RootBone_hair (one PhysBone in the budget); Merge Bones at 50% halves a chain, the weights kept, the body unmoved", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("create Cylinder");
    GameObject *body = ed.active_object_for_test();
    ed.command("position 0 1 0");
    ed.command("rig humanoid");
    GameObject *rig = ed.active_object_for_test();
    Scene &s = ed.scene();
    GameObject *head = s.find_by_name("Head");
    if (!body || !rig || !head) return;
    GameObject *l = r359_chain(s, head, "Hair_L", Vec3(-0.08f, 0.15f, 0), 5);
    GameObject *r = r359_chain(s, head, "Hair_R", Vec3(0.08f, 0.15f, 0), 5);
    ed.select_for_test(body->id, false);
    ed.command("component SkinModifier");
    ed.command("set SkinModifier.Armature " + rig->name);
    ed.command("skin bind");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    const double chains = budget_count(avatar_objects(s, *rig)).value[(int)BudgetRow::SpringChains];
    const std::vector<Vec3> before = r357_skinned(*body);
    ed.select_for_test(rig->id, false);
    ed.command("avatar boneroot");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    GameObject *root = s.find_by_name("RootBone_hair");
    const double after_chains = budget_count(avatar_objects(s, *rig)).value[(int)BudgetRow::SpringChains];
    std::printf("    Bone Root: %s, the chains under it %d %d, PhysBones %.0f -> %.0f, the body moved %.6f\n", root ? root->name.c_str() : "-", (int)(root && l->parent == root),
                (int)(root && r->parent == root), chains, after_chains, r358_worst(before, r357_skinned(*body)));
    CHECK(root && l->parent == root && r->parent == root && root->parent == head && chains == 2.0 && after_chains == 1.0);
    CHECK(r358_worst(before, r357_skinned(*body)) < 1e-3f);
    /* Merge Bones at 50: each chain of 5 keeps its first and loses 2 of the other 4. */
    const size_t len_before = spring::chain_bones(*l).size();
    const uint64_t lid = l->id;  // (Undo replaces the objects: the id, not the pointer, finds it after)
    ed.command("avatar mergebones 50");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    const size_t len_after = spring::chain_bones(*l).size();
    std::printf("    Merge Bones 50%%: a chain of %zu now %zu; the body moved %.6f\n", len_before, len_after, r358_worst(before, r357_skinned(*body)));
    CHECK(len_before == 5 && len_after == 3 && r358_worst(before, r357_skinned(*body)) < 1e-3f);
    ed.command("undo");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    GameObject *l2 = ed.scene().find(lid);
    CHECK(l2 && spring::chain_bones(*l2).size() == 5);
  });
  test("Decimation and Bone Root (review 359): the words after `avatar decimate` - names after a comma, a name with a space, a name starting with a digit; the group word skips sides, numbers and namespaces", [] {
    DecimateMode mode = DecimateMode::Smart;
    double tris = 70000.0;
    std::vector<std::string> ex;
    parse_decimate_args({"smart", "5000", "Face,", "Eyes"}, mode, tris, ex);
    CHECK(mode == DecimateMode::Smart && tris == 5000.0 && ex.size() == 2 && ex[0] == "Face" && ex[1] == "Eyes");
    tris = 70000.0;
    parse_decimate_args({"half", "Body", "Mesh,", "2Hats"}, mode, tris, ex);
    CHECK(mode == DecimateMode::Half && tris == 70000.0 && ex.size() == 2 && ex[0] == "Body Mesh" && ex[1] == "2Hats");
    parse_decimate_args({}, mode, tris, ex);
    CHECK(ex.empty());
    std::printf("    group words: %s %s %s %s\n", chain_group_word("J_Sec_L_Hair1_01").c_str(), chain_group_word("L_Ear").c_str(), chain_group_word("R_Hair").c_str(), chain_group_word("Hair_L").c_str());
    CHECK(chain_group_word("J_Sec_L_Hair1_01") == "hair" && chain_group_word("L_Ear") == "ear" && chain_group_word("R_Hair") == "hair" && chain_group_word("Hair_L") == "hair");
  });
  test("Bone Root and Bone Merging (review 359): a clip on a grouped chain follows it and Undo takes Bone Root back; Merge Bones at 100% leaves one bone reaching the strand's end; the root is no collider", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("rig humanoid");
    GameObject *rig = ed.active_object_for_test();
    Scene &s = ed.scene();
    GameObject *head = s.find_by_name("Head");
    if (!rig || !head) return;
    GameObject *l = r359_chain(s, head, "L_Hair", Vec3(-0.08f, 0.15f, 0), 5);
    r359_chain(s, head, "R_Hair", Vec3(0.08f, 0.15f, 0), 5);
    const uint64_t lid = l->id;
    std::remove(resolve_asset_path("Assets/Animations/R359Hair.anim").c_str());
    AnimationClipPtr clip = create_clip_asset("Assets/Animations/R359Hair.anim", "R359Hair");
    if (!clip) return;
    clip->track(anim_path(*rig, *l), "Rotation", 0).set_key(0, 10.0f);
    rig->add<Animation>()->clip = clip->asset_path;
    ed.mark_changed_for_test("key the hair");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    ed.select_for_test(rig->id, false);
    ed.command("avatar boneroot");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    GameObject *root = s.find_by_name("RootBone_hair");
    GameObject *l1 = s.find(lid);
    CHECK(root && l1 && l1->parent == root && clip_asset("Assets/Animations/R359Hair.anim")->find(anim_path(*rig, *l1), "Rotation", 0));
    /* The root isn't a capsule the strands collide with. */
    CHECK(root && spring::is_bone_root(*root));
    ed.command("undo");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    CHECK(!ed.scene().find_by_name("RootBone_hair") && ed.scene().find(lid) && ed.scene().find(lid)->parent == ed.scene().find_by_name("Head"));
    /* Merge Bones at 100%: one bone left, its tip where the strand ended. */
    GameObject *first = ed.scene().find(lid);
    std::vector<GameObject *> chain = spring::chain_bones(*first);
    GameObject *last = chain.back();
    const Vec3 end = last->world_position() + normalize(last->world_matrix().dir(Vec3(0, 1, 0))) * (last->get<Bone>()->length * bone_world_unit(*last));
    const float reach = length(end - first->world_position());
    ed.command("avatar mergebones 100");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    GameObject *f2 = ed.scene().find(lid);
    const float len = f2 ? f2->get<Bone>()->length * bone_world_unit(*f2) : 0.0f;
    std::printf("    Merge Bones 100%%: %zu bone(s) left, the first's length %.3f, the strand reached %.3f\n", f2 ? spring::chain_bones(*f2).size() : (size_t)0, len, reach);
    CHECK(f2 && spring::chain_bones(*f2).size() == 1 && std::fabs(len - reach) < 1e-3f);
  });
  test("the lesson's Try-it (round 359): rig humanoid, then avatar boneroot - nothing to group, said", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("rig humanoid");
    std::vector<LogEntry> log;
    const size_t since = Log::fetch(0, log);
    ed.command("avatar boneroot");
    log.clear();
    Log::fetch(since, log);
    bool said = false;
    for (const LogEntry &e : log) said = said || e.text.find("nothing to group") != std::string::npos;
    CHECK(said);
  });
}

/* ---------------------------------------------------------------- round 360: Eye Tracking, Texture Atlas */
static void tests_round360() {
  test("Eye Tracking (round 360): two eye meshes on a humanoid become LeftEye (the rig's -X) and RightEye under Head at their middles, each eye following its bone; a Blink key splits into vrc.blink_left / right by side; the test turns and shuts them, Reset puts them back", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("create Cylinder");
    GameObject *body = ed.active_object_for_test();
    ed.command("position 0 1 0");
    ed.command("rig humanoid");
    GameObject *rig = ed.active_object_for_test();
    Scene &s = ed.scene();
    GameObject *head = s.find_by_name("Head");
    if (!body || !rig || !head) return;
    const Vec3 hp = head->world_position();
    GameObject *eye[2] = {};
    for (int i = 0; i < 2; i++) {
      ed.command("create Sphere");
      eye[i] = ed.active_object_for_test();
      eye[i]->name = i == 0 ? "Eye_L" : "Eye_R";
      eye[i]->set_local_scale(Vec3(0.02f));
      eye[i]->set_world_position(hp + Vec3(i == 0 ? -0.03f : 0.03f, 0.08f, 0.08f));
      s.set_parent(eye[i], head, -1, true);
    }
    /* The body's Blink: every vertex 1 cm down. */
    {
      Mesh &m = *mesh_make_mutable(body->get<MeshFilter>()->mesh);
      m.shape_keys = {{"Blink", std::vector<Vec3>(m.vert_count(), Vec3(0, -0.01f, 0))}};
      m.touch();
    }
    ed.select_for_test(body->id, false);
    ed.command("component SkinModifier");
    ed.command("set SkinModifier.Armature " + rig->name);
    ed.command("skin bind");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    ed.select_for_test(rig->id, false);
    ed.command("avatar eyes");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    GameObject *le = s.find_by_name("LeftEye"), *re = s.find_by_name("RightEye");
    CHECK(le && re && le->parent == head && re->parent == head);
    if (!le || !re) return;
    std::printf("    LeftEye at (%.3f %.3f %.3f), RightEye at (%.3f %.3f %.3f); the eyes' parents %s, %s\n", le->world_position().x, le->world_position().y, le->world_position().z,
                re->world_position().x, re->world_position().y, re->world_position().z, eye[0]->parent->name.c_str(), eye[1]->parent->name.c_str());
    CHECK(length(le->world_position() - eye[0]->world_position()) < 0.005f && length(re->world_position() - eye[1]->world_position()) < 0.005f);
    CHECK(eye[0]->parent == le && eye[1]->parent == re);
    const Mesh &bm = *body->get<MeshFilter>()->mesh;
    const int bl = bm.find_shape("vrc.blink_left"), br = bm.find_shape("vrc.blink_right");
    CHECK(bl >= 0 && br >= 0);
    if (bl < 0 || br < 0) return;
    const float side_x = body->world_matrix().inverse().point(hp).x;
    bool sides = true;
    for (size_t i = 0; i < bm.vert_count(); i++) {
      const bool left = bm.positions[i].x < side_x;
      sides = sides && (length(bm.shape_keys[(size_t)bl].delta[i]) > 0.0f) == left && (length(bm.shape_keys[(size_t)br].delta[i]) > 0.0f) == !left;
    }
    CHECK(sides);
    /* The test: turned and shut, then back. */
    const Quat rest = le->get<Bone>()->rest_rotation;
    ed.command("avatar eyetest 25 0 1");
    const float shut = body->get<MeshRenderer>()->shape_weight("vrc.blink_left");
    const bool turned = std::fabs(r358_qdot(le->local().rotation, rest)) < 0.999f;
    ed.command("avatar eyetest reset");
    std::printf("    the test: turned %d, the blink at %.1f; after Reset the blink at %.1f\n", (int)turned, shut, body->get<MeshRenderer>()->shape_weight("vrc.blink_left"));
    CHECK(turned && shut == 1.0f && body->get<MeshRenderer>()->shape_weight("vrc.blink_left") == 0.0f && std::fabs(r358_qdot(le->local().rotation, rest)) > 0.9999f);
    /* The checklist's eye item passes now. */
    bool eyes_ok = false;
    for (const AvatarCheck &c : avatar_check(s, *rig)) eyes_ok = eyes_ok || (c.item == "Eye bones (Eye Look)" && c.ok);
    CHECK(eyes_ok);
  });
  test("Eye Tracking (round 360): nothing to set up on a bare rig is said; split_key_by_side keeps each side's offsets; the checklist's eye item has its Fix", [] {
    Mesh m = *primitives::cube();
    m.shape_keys = {{"Blink", std::vector<Vec3>(m.vert_count(), Vec3(0, -0.1f, 0))}};
    CHECK(split_key_by_side(m, "Blink", 0.0f, "L", "R") && !split_key_by_side(m, "Nope", 0.0f, "L", "R"));
    const int l = m.find_shape("L"), r = m.find_shape("R");
    float ls = 0.0f, rs = 0.0f;
    for (size_t i = 0; l >= 0 && r >= 0 && i < m.vert_count(); i++) ls += length(m.shape_keys[(size_t)l].delta[i]), rs += length(m.shape_keys[(size_t)r].delta[i]);
    CHECK(l >= 0 && r >= 0 && std::fabs(ls - rs) < 1e-5f && ls > 0.0f);
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("rig humanoid");
    GameObject *rig = ed.active_object_for_test();
    if (!rig) return;
    std::string fix;
    for (const AvatarCheck &c : avatar_check(ed.scene(), *rig)) fix = c.item == "Eye bones (Eye Look)" ? c.fix : fix;
    CHECK(fix == "avatar eyes");
    std::vector<LogEntry> log;
    const size_t since = Log::fetch(0, log);
    ed.command("avatar eyes");
    log.clear();
    Log::fetch(since, log);
    bool said = false;
    for (const LogEntry &e : log) said = said || e.text.find("Eye Tracking: no eyes") != std::string::npos;
    CHECK(said && !ed.scene().find_by_name("LeftEye"));
  });
  test("Texture Atlas (round 360): a mesh with two materials baked into one texture set, one material, one slot, its UVs laid out anew", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("rig humanoid");
    GameObject *rig = ed.active_object_for_test();
    ed.command("create Cube");
    GameObject *cube = ed.active_object_for_test();
    if (!rig || !cube) return;
    MeshRenderer *mr = cube->get<MeshRenderer>();
    mr->materials = {std::make_shared<Material>(), std::make_shared<Material>()};
    mr->materials[0]->base_color = Vec3(1, 0, 0);
    mr->materials[1]->base_color = Vec3(0, 0, 1);
    {
      Mesh &m = *mesh_make_mutable(cube->get<MeshFilter>()->mesh);
      m.face_material.assign(m.face_count(), 0);
      for (size_t f = 0; f < m.face_count(); f += 2) m.face_material[f] = 1;
      m.touch();
    }
    cube->add<SkinModifier>()->object = rig->name;
    ed.select_for_test(rig->id, false);
    ed.command("avatar atlas 256");
    const Mesh &m = *cube->get<MeshFilter>()->mesh;
    const bool one_slot = std::all_of(m.face_material.begin(), m.face_material.end(), [](int f) { return f == 0; });
    std::printf("    after the atlas: %zu material(s), one slot %d, its albedo map %s\n", mr->materials.size(), (int)one_slot, mr->materials.empty() ? "-" : mr->materials[0]->base_map.path.c_str());
    CHECK(mr->materials.size() == 1 && one_slot && !mr->materials[0]->base_map.empty());
    /* The baked material and its images out of the scratch project (each run left one more, crowding the Materials
     * window's grid for the multi-select test). */
    for (const MaterialPtr &mat : mr->materials) {
      if (!mat->asset_path.empty()) std::remove(resolve_asset_path(mat->asset_path).c_str());
      for (const TextureRef *t : {&mat->base_map, &mat->normal_map, &mat->metallic_map, &mat->roughness_map, &mat->emission_map})
        if (!t->empty()) std::remove(resolve_asset_path(t->path).c_str());
    }
  });
  test("Eye Tracking (review 360): one Eyes mesh for both eyes skinned to LeftEye and RightEye; a Skin's limit group kept on the eyes; a second avatar's eyes its own; a re-run after an eye moved keeps it; a turned body's blinks on the right sides; the test undone before an edit", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("rig humanoid");
    GameObject *rig = ed.active_object_for_test();
    Scene &s = ed.scene();
    GameObject *head = s.find_by_name("Head");
    if (!rig || !head) return;
    const Vec3 hp = head->world_position();
    /* One "Eyes" mesh holding both: two small spheres joined. */
    ed.command("create Sphere");
    GameObject *eyes = ed.active_object_for_test();
    {
      Mesh both = *primitives::uv_sphere();
      Mesh &m = *mesh_make_mutable(eyes->get<MeshFilter>()->mesh);
      m = both;
      for (Vec3 &p : m.positions) p = p * 0.02f + Vec3(-0.03f, 0.08f, 0.08f);
      Mesh right = both;
      for (Vec3 &p : right.positions) p = p * 0.02f + Vec3(0.03f, 0.08f, 0.08f);
      meshops::append_mesh(m, right, Mat4::identity(), {});
      m.touch();
    }
    eyes->name = "Eyes";
    eyes->set_world_position(hp);
    s.set_parent(eyes, head, -1, true);
    ed.select_for_test(rig->id, false);
    ed.command("avatar eyes");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    GameObject *le = s.find_by_name("LeftEye"), *re = s.find_by_name("RightEye");
    const SkinModifier *es = eyes->get<SkinModifier>();
    std::printf("    the Eyes mesh: skinned %d, its parent %s, LeftEye x %.3f, RightEye x %.3f\n", (int)(es != nullptr), eyes->parent ? eyes->parent->name.c_str() : "-",
                le ? le->world_position().x - hp.x : 0.0f, re ? re->world_position().x - hp.x : 0.0f);
    CHECK(le && re && es && eyes->parent == rig && le->world_position().x < hp.x && re->world_position().x > hp.x);
    /* The test is undone by the next edit (its pose and blinks aren't kept). */
    const Quat lrest = le ? le->get<Bone>()->rest_rotation : Quat();
    ed.command("avatar eyetest 30 0 1");
    ed.command("avatar eyetest nan 0 0");  // (refused)
    ed.command("create Cube");
    CHECK(le && std::fabs(r358_qdot(le->local().rotation, lrest)) > 0.9999f);
    /* A second avatar: its own eye bones (by the rig, not the first found by name). */
    ed.command("rig humanoid");
    GameObject *rig2 = ed.active_object_for_test();
    GameObject *head2 = nullptr;
    std::vector<GameObject *> bones2;
    s.for_each([&](GameObject &g) { head2 = g.name == "Head" && rig2 && s.is_ancestor(rig2, &g) ? &g : head2; });
    if (!rig2 || !head2) return;
    rig2->set_world_position(Vec3(3, 0, 0));
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    GameObject *e2[2] = {};
    for (int i = 0; i < 2; i++) {
      ed.command("create Sphere");
      e2[i] = ed.active_object_for_test();
      e2[i]->name = i == 0 ? "Eye_L" : "Eye_R";
      e2[i]->set_local_scale(Vec3(0.02f));
      e2[i]->set_world_position(head2->world_position() + Vec3(i == 0 ? -0.03f : 0.03f, 0.08f, 0.08f));
      s.set_parent(e2[i], head2, -1, true);
    }
    ed.select_for_test(rig2->id, false);
    ed.command("avatar eyes");
    CHECK(e2[0]->parent && e2[0]->parent->name == "LeftEye" && s.is_ancestor(rig2, e2[0]->parent) && e2[0]->parent != le);
    /* A re-run after the left eye moved 1 cm: the bone at the eye, the eye where it was put. */
    const Vec3 moved = e2[0]->world_position() + Vec3(0, 0.01f, 0);
    e2[0]->set_world_position(moved);
    ed.command("avatar eyes");
    std::printf("    the re-run: the eye %.4f from where it was put, the bone %.4f from it\n", length(e2[0]->world_position() - moved), length(e2[0]->parent->world_position() - moved));
    CHECK(length(e2[0]->world_position() - moved) < 1e-4f && length(e2[0]->parent->world_position() - moved) < 1e-3f);
  });
  test("Eye Tracking (review 360): a Skin's limit group stays on the eye vertices; a body turned half round blinks each eye on its own side; the atlas lists a mesh it couldn't bake", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("create Cylinder");
    GameObject *body = ed.active_object_for_test();
    ed.command("position 0 1 0");
    ed.command("rig humanoid");
    GameObject *rig = ed.active_object_for_test();
    if (!body || !rig) return;
    ed.select_for_test(body->id, false);
    ed.command("component SkinModifier");
    ed.command("set SkinModifier.Armature " + rig->name);
    ed.command("skin bind");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    const Vec3 hp = ed.scene().find_by_name("Head")->world_position();
    body->set_local_rotation(Quat::axis_angle(Vec3(0, 1, 0), 3.14159265f));  // (turned half round: its own X is the rig's -X)
    {
      Mesh &m = *mesh_make_mutable(body->get<MeshFilter>()->mesh);
      const Mat4 inv = body->world_matrix().inverse();
      std::vector<float> l(m.vert_count(), 0.0f), r(m.vert_count(), 0.0f);
      l[0] = 1.0f, r[1] = 1.0f;
      m.positions[0] = inv.point(hp + Vec3(-0.03f, 0.08f, 0.08f));
      m.positions[1] = inv.point(hp + Vec3(0.03f, 0.08f, 0.08f));
      m.vertex_groups.push_back({"Eye_L", l});
      m.vertex_groups.push_back({"Eye_R", r});
      m.vertex_groups.push_back({"Deform", std::vector<float>(m.vert_count(), 1.0f)});
      m.shape_keys = {{"Blink", std::vector<Vec3>(m.vert_count(), Vec3(0, -0.01f, 0))}};
      m.touch();
    }
    body->get<SkinModifier>()->vertex_group = "Deform";
    ed.select_for_test(rig->id, false);
    ed.command("avatar eyes");
    const Mesh &m = *body->get<MeshFilter>()->mesh;
    const int deform = m.find_group("Deform"), bl = m.find_shape("vrc.blink_left");
    CHECK(deform >= 0 && m.vertex_groups[(size_t)deform].weights[0] == 1.0f && m.vertex_groups[(size_t)m.find_group("LeftEye")].weights[0] == 1.0f);
    /* vrc.blink_left moves the vertices on the rig's left (world x below the head's), whatever the body's turn. */
    bool sides = bl >= 0;
    for (size_t i = 0; sides && i < m.vert_count(); i++)
      sides = (length(m.shape_keys[(size_t)bl].delta[i]) > 0.0f) == (body->world_matrix().point(m.positions[i]).x < hp.x);
    CHECK(sides);
    /* The atlas: a two-material mesh with a Mirror can't be baked - said, by name. */
    ed.command("create Cube");
    GameObject *cube = ed.active_object_for_test();
    cube->get<MeshRenderer>()->materials = {std::make_shared<Material>(), std::make_shared<Material>()};
    cube->add<SkinModifier>()->object = rig->name;
    ed.select_for_test(cube->id, false);
    ed.command("component MirrorModifier");
    std::vector<LogEntry> log;
    const size_t since = Log::fetch(0, log);
    ed.select_for_test(rig->id, false);
    ed.command("avatar atlas 256");
    log.clear();
    Log::fetch(since, log);
    bool said = false;
    for (const LogEntry &e : log) said = said || (e.text.find("not baked") != std::string::npos && e.text.find(cube->name) != std::string::npos);
    CHECK(said);
  });
  test("the lesson's Try-it (round 360): rig humanoid, then avatar eyes - no eyes on a bare rig, said", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("rig humanoid");
    ed.command("avatar eyes");
    CHECK(!ed.scene().find_by_name("LeftEye"));
  });
}

/* ---------------------------------------------------------------- round 361: Merge Armatures, Attach Mesh, Translate, Export */
static void tests_round361() {
  test("Translate (round 361): MMD's morph and material names from the dictionary - whole names, then by parts; an English or unknown name left", [] {
    const char *const kIn[] = {"\xE3\x81\xBE\xE3\x81\xB0\xE3\x81\x9F\xE3\x81\x8D", "\xE3\x81\x82", "\xE3\x82\xA6\xE3\x82\xA3\xE3\x83\xB3\xE3\x82\xAF\xE5\x8F\xB3",
                               "\xE8\x82\x8C", "\xE9\xAB\xAA\xE5\x8F\xB3", "Smile", "\xE6\x9C\xAA\xE7\x9F\xA5"};
    const char *const kOut[] = {"Blink", "A", "Wink Right", "Skin", "Hair Right", "", ""};
    for (int i = 0; i < 7; i++) {
      const std::string got = translate_name(kIn[i]);
      if (got != kOut[i]) std::printf("    %d: '%s', wanted '%s'\n", i, got.c_str(), kOut[i]);
      CHECK(got == kOut[i]);
    }
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("rig humanoid");
    GameObject *rig = ed.active_object_for_test();
    ed.command("create Cube");
    GameObject *body = ed.active_object_for_test();
    if (!rig || !body) return;
    {
      Mesh &m = *mesh_make_mutable(body->get<MeshFilter>()->mesh);
      m.shape_keys = {{"\xE3\x81\xBE\xE3\x81\xB0\xE3\x81\x9F\xE3\x81\x8D", std::vector<Vec3>(m.vert_count(), Vec3(0, 0.01f, 0))}};
      m.touch();
    }
    body->get<MeshRenderer>()->materials = {std::make_shared<Material>()};
    body->get<MeshRenderer>()->materials[0]->name = "\xE8\x82\x8C";
    body->get<MeshRenderer>()->set_shape_weight("\xE3\x81\xBE\xE3\x81\xB0\xE3\x81\x9F\xE3\x81\x8D", 0.5f);
    body->add<SkinModifier>()->object = rig->name;
    /* A clip's track on the Japanese key (as a VMD's morphs come in), and the material an asset. */
    std::remove(resolve_asset_path("Assets/Animations/R361Blink.anim").c_str());
    AnimationClipPtr clip = create_clip_asset("Assets/Animations/R361Blink.anim", "R361Blink");
    if (!clip) return;
    clip->track(anim_path(*rig, *body), "MeshRenderer.BlendShape/\xE3\x81\xBE\xE3\x81\xB0\xE3\x81\x9F\xE3\x81\x8D", 0).set_key(0, 1.0f);
    rig->add<Animation>()->clip = clip->asset_path;
    for (const char *p : {"Assets/Materials/R361/Skin.mat", "Assets/Materials/R361/Skin 2.mat"}) std::remove(resolve_asset_path(p).c_str());
    Material skin_src;
    skin_src.name = "\xE8\x82\x8C";
    MaterialPtr asset = create_material_asset(skin_src, "Assets/Materials/R361/\xE8\x82\x8C.mat");
    if (asset) body->get<MeshRenderer>()->materials = {asset};
    ed.mark_changed_for_test("set up the names");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    ed.select_for_test(rig->id, false);
    ed.command("avatar translate");
    const Mesh &m = *body->get<MeshFilter>()->mesh;
    const MaterialPtr now = body->get<MeshRenderer>()->materials[0];
    std::printf("    translated: the key %d, its weight %.1f, the clip's track %d, the material %s at %s (the original still %s)\n", m.find_shape("Blink"),
                body->get<MeshRenderer>()->shape_weight("Blink"), (int)(clip->find(anim_path(*rig, *body), "MeshRenderer.BlendShape/Blink", 0) != nullptr), now->name.c_str(),
                now->asset_path.c_str(), asset ? asset->name.c_str() : "-");
    CHECK(m.find_shape("Blink") >= 0 && now->name == "Skin" && body->get<MeshRenderer>()->shape_weight("Blink") == 0.5f);
    CHECK(clip->find(anim_path(*rig, *body), "MeshRenderer.BlendShape/Blink", 0) != nullptr);
    CHECK(asset && asset->name != "Skin" && now != asset && now->asset_path.find("Skin") != std::string::npos);
    ed.command("undo");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    GameObject *b2 = ed.scene().find_by_name(body->name);
    CHECK(b2 && b2->get<MeshFilter>()->mesh->find_shape("Blink") < 0);
  });
  test("Merge Armatures and Attach Mesh (round 361): a hair rig with its own Head merged into the avatar's - its strand under the avatar's Head, its mesh skinned to the avatar and following the head; a ring given wholly to a hand", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("rig humanoid");
    GameObject *rig = ed.active_object_for_test();
    Scene &s = ed.scene();
    GameObject *head = s.find_by_name("Head");
    if (!rig || !head) return;
    /* The hair's own rig: a Head where the avatar's is, a strand under it, a mesh skinned to the strand. */
    GameObject *hrig = s.create("HairRig", nullptr);
    hrig->add<Armature>();
    GameObject *hhead = r357_bone(hrig, s, "Head", head->world_position() + Vec3(0, 0.02f, 0));  // (2 cm off the avatar's: the hair still doesn't jump)
    GameObject *strand = r357_bone(hhead, s, "Strand", Vec3(0, 0.1f, -0.05f));
    ed.command("create Cube");
    GameObject *hair = ed.active_object_for_test();
    if (!hair) return;
    hair->set_world_position(strand->world_position());
    hair->set_local_scale(Vec3(0.05f));
    {
      Mesh &m = *mesh_make_mutable(hair->get<MeshFilter>()->mesh);
      m.vertex_groups = {{"Strand", std::vector<float>(m.vert_count(), 1.0f)}};
      m.touch();
    }
    hair->add<SkinModifier>()->object = "HairRig";
    /* A clip on the hair rig's Head: a hat (not a bone) hanging from it is kept, under the avatar's Head. */
    ed.command("create Cone");
    GameObject *clip_hat = ed.active_object_for_test();
    if (!clip_hat) return;
    s.set_parent(clip_hat, hhead, -1, true);
    const uint64_t clip_hat_id = clip_hat->id;
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    const std::vector<Vec3> before = r357_skinned(*hair);
    const uint64_t strand_id = strand->id, hrig_id = hrig->id;
    ed.select_for_test(rig->id, false);
    ed.command("avatar mergearmature HairRig");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    GameObject *st = s.find(strand_id);
    std::printf("    merged: the strand under %s, the hair skinned to %s, HairRig %s; the hair moved %.6f at rest\n", st && st->parent ? st->parent->name.c_str() : "-",
                hair->get<SkinModifier>()->object.c_str(), s.find(hrig_id) ? "kept" : "gone", r358_worst(before, r357_skinned(*hair)));
    CHECK(st && st->parent == head && hair->get<SkinModifier>()->object == rig->name && !s.find(hrig_id) && r358_worst(before, r357_skinned(*hair)) < 1e-4f);
    CHECK(s.find(clip_hat_id) && s.find(clip_hat_id)->parent == head);
    /* The head turned: the hair turns with it. */
    head->set_local_rotation(head->get<Bone>()->rest_rotation * Quat::axis_angle(Vec3(0, 1, 0), 0.6f));
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    CHECK(r358_worst(before, r357_skinned(*hair)) > 0.01f);
    /* Attach Mesh on a hat already under the Head: it turns once with the head, not twice. */
    head->set_local_rotation(head->get<Bone>()->rest_rotation);
    ed.command("create Cube");
    GameObject *hat = ed.active_object_for_test();
    if (!hat) return;
    hat->set_world_position(head->world_position() + Vec3(0, 0.2f, 0));
    s.set_parent(hat, head, -1, true);
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    ed.select_for_test(hat->id, false);
    ed.command("avatar attach Head");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    const std::vector<Vec3> hat_rest = r357_skinned(*hat);
    head->set_local_rotation(head->get<Bone>()->rest_rotation * Quat::axis_angle(Vec3(0, 1, 0), 0.5236f));
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    const std::vector<Vec3> hat_turned = r357_skinned(*hat);
    /* Its top vertex's turn about the head's axis: 30 degrees, not 60. */
    Vec3 a = hat_rest.empty() ? Vec3(0.0f) : hat_rest[0] - head->world_position(), b = hat_turned.empty() ? Vec3(0.0f) : hat_turned[0] - head->world_position();
    a.y = 0.0f, b.y = 0.0f;
    const float turned = std::acos(std::clamp(dot(normalize(a), normalize(b)), -1.0f, 1.0f)) * 57.29578f;
    std::printf("    a hat under the head, attached: its parent %s, turned %.1f degrees with a 30-degree head\n", hat->parent ? hat->parent->name.c_str() : "-", turned);
    CHECK(hat->parent == rig && std::fabs(turned - 30.0f) < 2.0f);
    head->set_local_rotation(head->get<Bone>()->rest_rotation);
    /* Attach Mesh: a ring on the left hand. */
    ed.command("create Torus");
    GameObject *ring = ed.active_object_for_test();
    if (!ring) return;
    ed.command("avatar attach LeftHand");
    const Mesh &rm = *ring->get<MeshFilter>()->mesh;
    CHECK(rm.vertex_groups.size() == 1 && rm.vertex_groups[0].name == "LeftHand" && ring->get<SkinModifier>() && ring->get<SkinModifier>()->object == rig->name);
    ed.command("avatar attach NoSuchBone");
    CHECK(rm.vertex_groups.size() == 1 && ring->get<MeshFilter>()->mesh->vertex_groups[0].name == "LeftHand");
  });
  test("Export for VRChat (round 361): a body with three mouths through Fix Model and Create Visemes goes out as FBX - the checklist and rank said first, its skeleton, skin and 15 visemes read back by ufbx", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("create Cylinder");
    GameObject *body = ed.active_object_for_test();
    ed.command("position 0 1 0");
    ed.command("rig humanoid");
    GameObject *rig = ed.active_object_for_test();
    if (!body || !rig) return;
    ed.select_for_test(body->id, false);
    ed.command("component SkinModifier");
    ed.command("set SkinModifier.Armature " + rig->name);
    ed.command("skin bind");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    for (const char *c : {"shape add AA", "shape add OH", "shape add CH", "visemes"}) ed.command(c);
    ed.select_for_test(rig->id, false);
    ed.command("avatar fixmodel");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    const std::string dir = fs::join(test_dir(), "r361");
    fs::make_dirs(dir);
    const std::string path = fs::join(dir, "avatar.fbx");
    std::remove(path.c_str());
    std::vector<LogEntry> log;
    const size_t since = Log::fetch(0, log);
    ed.select_for_test(rig->id, false);
    ed.command("avatar export " + path);
    log.clear();
    Log::fetch(since, log);
    bool summary = false;
    for (const LogEntry &e : log) summary = summary || e.text.find("Export for VRChat:") != std::string::npos;
    ufbx_load_opts opts = {};
    ufbx_error err;
    ufbx_scene *sc = ufbx_load_file(path.c_str(), &opts, &err);
    size_t visemes = 0;
    for (size_t i = 0; sc && i < sc->blend_channels.count; i++) visemes += std::string(sc->blend_channels.data[i]->name.data).rfind("vrc.v_", 0) == 0 ? 1 : 0;
    std::printf("    the FBX: %s, %zu skin(s), %zu bones, %zu viseme channels; the summary said %d\n", sc ? "read" : err.description.data, sc ? sc->skin_deformers.count : (size_t)0,
                sc ? sc->bones.count : (size_t)0, visemes, (int)summary);
    CHECK(sc && sc->skin_deformers.count >= 1 && sc->bones.count >= 15 && visemes == 15 && summary);
    if (sc) ufbx_free_scene(sc);
  });
  test("the lesson's Try-it (round 361): rig humanoid, then avatar translate - nothing to translate, said", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("rig humanoid");
    std::vector<LogEntry> log;
    const size_t since = Log::fetch(0, log);
    ed.command("avatar translate");
    log.clear();
    Log::fetch(since, log);
    bool said = false;
    for (const LogEntry &e : log) said = said || e.text.find("none known") != std::string::npos;
    CHECK(said);
  });
}

/* ---------------------------------------------------------------- round 362: the world kit - Colliders, Mark Static */
static void tests_round362() {
  test("Colliders (round 362): Fit to Mesh sizes a box, a sphere and a capsule to the mesh; the collider's mesh is that shape where it says", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("create Cylinder");  // (radius 0.5, 2 tall)
    GameObject *g = ed.active_object_for_test();
    if (!g) return;
    ed.command("component Collider");
    ColliderComponent *c = g->get<ColliderComponent>();
    CHECK(c != nullptr);
    if (!c) return;
    c->fit = true;
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    std::printf("    the box fitted: size (%.2f %.2f %.2f), center (%.2f %.2f %.2f); Fit unticked %d\n", c->size.x, c->size.y, c->size.z, c->center.x, c->center.y, c->center.z, (int)!c->fit);
    CHECK(std::fabs(c->size.x - 1.0f) < 0.02f && std::fabs(c->size.y - 2.0f) < 0.02f && !c->fit);
    const Mesh &m = *g->evaluated_mesh();
    for (int shape : {0, 1, 2}) {
      c->shape = shape;
      fit_collider(*c, m);
      const std::shared_ptr<Mesh> cm = collider_mesh(*c, m);
      const AABB b = cm ? cm->bounds() : AABB();
      const Vec3 size = b.valid() ? b.max - b.min : Vec3(0.0f);
      std::printf("    %s: its mesh %.2f x %.2f x %.2f\n", shape == 0 ? "Box" : shape == 1 ? "Sphere" : "Capsule", size.x, size.y, size.z);
      CHECK(cm != nullptr);
      if (shape == 0) CHECK(std::fabs(size.x - 1.0f) < 0.02f && std::fabs(size.y - 2.0f) < 0.02f);
      if (shape == 1) CHECK(std::fabs(size.y - 2.0f) < 0.05f);  // (the largest half side: 1)
      if (shape == 2) CHECK(std::fabs(size.y - 2.0f) < 0.05f && std::fabs(size.x - 1.0f) < 0.05f);
    }
    CHECK(collider_export_name(2, 4, "Wall").empty() && collider_export_name(2, 0, "Wall") == "UCX_Wall_00" && collider_export_name(3, 0, "Wall") == "Wall-convcolonly" &&
          collider_export_name(3, 4, "Wall") == "Wall-colonly" && collider_export_name(1, 0, "Wall") == "Wall_Collider" && collider_export_name(1, 4, "Wall") == "Wall_MeshCollider");
  });
  test("Colliders (round 362): an export with colliders=unreal, godot or unity writes the collider as a mesh named for the engine; without, none", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("create Cube");
    GameObject *g = ed.active_object_for_test();
    if (!g) return;
    g->name = "Wall";
    ed.command("component Collider");
    const std::string dir = fs::join(test_dir(), "r362");
    fs::make_dirs(dir);
    for (auto [engine, want] : {std::pair<const char *, const char *>{"", ""}, {"colliders=unreal", "UCX_Wall_00"}, {"colliders=godot", "Wall-convcolonly"}, {"colliders=unity", "Wall_Collider"}}) {
      const std::string path = fs::join(dir, std::string("wall") + (engine[0] ? engine + 10 : "plain") + ".glb");
      ed.select_for_test(g->id, false);
      ed.command(std::string("export glb ") + engine + (engine[0] ? " " : "") + path);
      std::string glb;
      fs::read_file(path, glb);
      const Json j = r351_glb_json(glb);
      std::string names;
      for (size_t i = 0; i < j["nodes"].size(); i++) names += j["nodes"][i]["name"].str + ";";
      std::printf("    %s: %s\n", engine[0] ? engine : "no colliders", names.c_str());
      CHECK(want[0] ? names.find(want) != std::string::npos : names == "Wall;");
    }
  });
  test("Generate Lighting (round 362): nothing with Contribute GI is refused with its Fix; Mark Static marks the still meshes, not a falling one", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("newscene");
    ed.command("create Cube");
    GameObject *still = ed.active_object_for_test();
    ed.command("create Sphere");
    GameObject *ball = ed.active_object_for_test();
    if (!still || !ball) return;
    ed.command("component Rigidbody");
    ed.scene().for_each([](GameObject &g) {
      if (MeshRenderer *mr = g.get<MeshRenderer>()) mr->contribute_gi = false;
    });
    ed.scene().lighting.baked_gi = true;
    std::vector<LogEntry> log;
    const size_t since = Log::fetch(0, log);
    ed.command("bake start");
    log.clear();
    Log::fetch(since, log);
    bool refused = false;
    for (const LogEntry &e : log) refused = refused || e.text.find("no object has Contribute GI on") != std::string::npos;
    CHECK(refused);
    ed.command("lighting markstatic");
    CHECK(still->get<MeshRenderer>()->contribute_gi && !ball->get<MeshRenderer>()->contribute_gi);
    ed.command("bake cancel");
  });
  test("the lesson's Try-it (round 362): create Cube, component Collider - a box its mesh's size", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("create Cube");
    ed.command("component Collider");
    GameObject *g = ed.active_object_for_test();
    const ColliderComponent *c = g ? g->get<ColliderComponent>() : nullptr;
    CHECK(c && c->shape == 0 && std::fabs(c->size.x - 1.0f) < 1e-4f);
  });
  test("Colliders (review 362): adding one from the Inspector and Ctrl+Z twice takes it away (the fit joins the step); a scaled parent's floor collides at its world size; a switched-off collider doesn't collide; Mark Static leaves an animated parent's child", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("create Cylinder");
    GameObject *g = ed.active_object_for_test();
    if (!g) return;
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    /* A collider added and fitted inside one frame's step (as the Inspector's menu does): one Undo takes it away. */
    g->add<ColliderComponent>();
    ed.mark_changed_for_test("Add Component");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    const ColliderComponent *c = g->get<ColliderComponent>();
    CHECK(c && !c->fit && std::fabs(c->size.y - 2.0f) < 0.02f);
    ed.command("undo");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    GameObject *back = ed.scene().find(g->id);
    CHECK(back && !back->get<ColliderComponent>());
    ed.command("redo");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    back = ed.scene().find(g->id);
    CHECK(back && back->get<ColliderComponent>());
    /* Mark Static: a child of an animated parent moves - not marked. */
    ed.command("create Empty");
    GameObject *windmill = ed.active_object_for_test();
    ed.command("create Cube");
    GameObject *blade = ed.active_object_for_test();
    if (!windmill || !blade) return;
    ed.scene().set_parent(blade, windmill, -1, true);
    windmill->add<Animation>();
    blade->get<MeshRenderer>()->contribute_gi = false;
    ed.command("lighting markstatic");
    CHECK(!blade->get<MeshRenderer>()->contribute_gi);
    if (!physics_jolt_available()) return;
    /* A floor under a parent scaled 2: its box collider 20 m wide in the world; a ball 7 m out still lands on it. */
    Scene s;
    GameObject *level = s.create("Level");
    level->set_local_scale(Vec3(2.0f));
    GameObject *floor = s.create("Floor", level);
    floor->add<MeshFilter>()->mesh = primitives::plane(10.0f, 1);
    floor->add<MeshRenderer>();
    ColliderComponent *fc = floor->add<ColliderComponent>();
    fc->fit = false, fc->shape = 0, fc->size = Vec3(10.0f, 0.5f, 10.0f), fc->center = Vec3(0, -0.25f, 0);
    GameObject *ball = s.create("Ball");
    ball->add<MeshFilter>()->mesh = primitives::cube();
    ball->add<MeshRenderer>();
    ColliderComponent *bc = ball->add<ColliderComponent>();
    bc->fit = false, bc->shape = 1, bc->radius = 0.3f;
    ball->add<Rigidbody>()->bounciness = 0.0f;
    ball->set_local_position({7, 2, 0});
    /* A second ball over a switched-off collider (a bush): it falls through. */
    GameObject *bush = s.create("Bush");
    bush->add<MeshFilter>()->mesh = primitives::cube(1.0f);
    bush->add<MeshRenderer>();
    bush->set_local_position({-6, 0, -6});
    bush->add<ColliderComponent>()->enabled = false;
    GameObject *ball2 = s.create("Ball2");
    ball2->add<MeshFilter>()->mesh = primitives::cube(0.2f);
    ball2->add<MeshRenderer>();
    ball2->add<Rigidbody>()->bounciness = 0.0f;
    ball2->set_local_position({-6, 3, -6});
    PlayContext ctx;
    s.start(ctx);
    ctx.dt = 1.0f / 60.0f;
    for (int i = 0; i < 240; i++) s.update(ctx);
    std::printf("    on the scaled floor 7 m out: y %.3f; over the switched-off bush: y %.3f (the floor's top at 0)\n", ball->world_position().y, ball2->world_position().y);
    CHECK_NEAR(ball->world_position().y, 0.3f, 0.05f);
    CHECK(ball2->world_position().y < 0.5f);  // (through the bush, onto the floor)
  });
  test("Colliders (round 362): in Play, a ball's Sphere collider rests on a floor's Box collider at their radii (Jolt), the floor's mesh a thin plane", [] {
    if (!physics_jolt_available()) return;
    Scene s;
    GameObject *floor = s.create("Floor");
    floor->add<MeshFilter>()->mesh = primitives::plane(10.0f, 1);
    floor->add<MeshRenderer>();
    ColliderComponent *fc = floor->add<ColliderComponent>();
    fc->shape = 0, fc->size = Vec3(10.0f, 1.0f, 10.0f), fc->center = Vec3(0, -0.5f, 0);  // (a slab under the plane)
    GameObject *ball = s.create("Ball");
    ball->add<MeshFilter>()->mesh = primitives::cube();
    ball->add<MeshRenderer>();
    ColliderComponent *bc = ball->add<ColliderComponent>();
    bc->shape = 1, bc->radius = 0.3f;
    ball->add<Rigidbody>()->bounciness = 0.0f;
    ball->set_local_position({0, 2, 0});
    PlayContext ctx;
    s.start(ctx);
    ctx.dt = 1.0f / 60.0f;
    for (int i = 0; i < 240; i++) s.update(ctx);
    std::printf("    the ball (a cube's mesh, a 0.3 sphere's collider) rests at y %.3f\n", ball->world_position().y);
    CHECK_NEAR(ball->world_position().y, 0.3f, 0.05f);  // (its sphere on the slab's top at 0, not the cube's half side)
  });
}

/* ---------------------------------------------------------------- round 363: the world kit - LOD Groups */
static void tests_round363() {
  test("LOD Groups (round 363): the level by height on screen - LOD0 near, the next ones as it shrinks, the last (or nothing, Cull) when tiny; the levels' triangles at their ratios", [] {
    LODGroupComponent g;
    g.levels = 3;
    CHECK(lod_level(g, 0.9f) == 0 && lod_level(g, 0.3f) == 1 && lod_level(g, 0.15f) == 2 && lod_level(g, 0.01f) == 2);
    g.cull = true;
    CHECK(lod_level(g, 0.01f) == -1 && lod_level(g, 0.12f) == 2);
    CHECK(lod_export_name("Tree", 2) == "Tree_LOD2");
    if (!meshops::decimate_available()) return;  // (the levels' meshes: meshoptimizer)
    const MeshPtr sphere = primitives::uv_sphere(0.5f, 48, 32);
    auto tris = [](const Mesh &m) { return (double)m.render_mesh().indices.size() / 3.0; };
    const double full = tris(*sphere);
    const double l1 = tris(*lod_mesh(g, *sphere, 1)), l2 = tris(*lod_mesh(g, *sphere, 2));
    std::printf("    a sphere's levels: %.0f, %.0f, %.0f triangles (ratios 1, 0.5, 0.25)\n", full, l1, l2);
    CHECK(l1 < full * 0.6 && l1 > full * 0.4 && l2 < full * 0.35 && l2 > full * 0.15);
    CHECK(lod_mesh(g, *sphere, 1) == lod_mesh(g, *sphere, 1));  // (kept, not made a frame)
  });
  test("LOD Groups (round 363): the Game view draws a far tree's LOD2 and a near one's LOD0; an export writes Tree_LOD0.._LOD2", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("create Sphere");
    GameObject *tree = ed.active_object_for_test();
    if (!tree) return;
    tree->name = "Tree";
    tree->set_local_position(Vec3(20, 0, 0));  // (away from the scene's Cube at the origin)
    ed.command("component LODGroup");
    LODGroupComponent *lg = tree->get<LODGroupComponent>();
    CHECK(lg != nullptr);
    if (!lg) return;
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    const Mat4 proj = Mat4::perspective(60.0f * 3.14159265f / 180.0f, 1.0f, 0.1f, 1000.0f);
    auto drawn = [&](Vec3 eye) -> size_t {  // (the tree's triangles as the Game view draws it)
      const Mat4 view = Mat4::look_at(eye, tree->world_position(), Vec3(0, 1, 0));
      for (const DrawItem &it : ed.collect_items_for_test(true, view, proj, eye))
        if (length(it.model.translation() - tree->world_position()) < 1e-5f) return it.mesh ? it.mesh->indices.size() / 3 : 0;
      return 0;
    };
    /* 1 m away the 1 m tree stands 0.87 of the view (LOD0); 60 m away 0.014 (below LOD2's 0.1: the last level). */
    const size_t near_faces = drawn(tree->world_position() + Vec3(0, 0, 1)), far_faces = drawn(tree->world_position() + Vec3(0, 0, 60));
    const Mesh &base = *tree->evaluated_mesh(1);
    const size_t base_tris = base.render_mesh().indices.size() / 3, lod2_tris = lod_mesh(*lg, base, 2)->render_mesh().indices.size() / 3;
    std::printf("    the tree's triangles drawn near %zu (its mesh %zu), far %zu (LOD2 %zu)\n", near_faces, base_tris, far_faces, lod2_tris);
    CHECK(near_faces == base_tris && far_faces == lod2_tris);
    if (meshops::decimate_available()) CHECK(far_faces < base_tris / 2);
    /* A skinned or keyed tree deforms: the Game view draws it whole (its levels would freeze at their first pose). */
    {
      Mesh &km = *mesh_make_mutable(tree->get<MeshFilter>()->mesh);
      km.shape_keys = {{"Sway", std::vector<Vec3>(km.vert_count(), Vec3(0.01f, 0, 0))}};
      km.touch();
    }
    tree->get<MeshRenderer>()->set_shape_weight("Sway", 0.5f);
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    const size_t keyed_far = drawn(tree->world_position() + Vec3(0, 0, 60));
    CHECK(keyed_far == tree->evaluated_mesh(1)->render_mesh().indices.size() / 3);
    tree->get<MeshRenderer>()->shape_weights.clear();
    tree->get<MeshFilter>()->mesh->shape_keys.clear();
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    const std::string dir = fs::join(test_dir(), "r363");
    fs::make_dirs(dir);
    const std::string path = fs::join(dir, "tree.glb");
    ed.select_for_test(tree->id, false);
    ed.command("export glb " + path);
    std::string glb;
    fs::read_file(path, glb);
    const Json j = r351_glb_json(glb);
    std::string names;
    for (size_t i = 0; i < j["nodes"].size(); i++) names += j["nodes"][i]["name"].str + ";";
    std::printf("    exported: %s\n", names.c_str());
    CHECK(meshops::decimate_available() ? names == "Tree_LOD0;Tree_LOD1;Tree_LOD2;" : names == "Tree;");  // (no copies where nothing decimates)
  });
  test("the lesson's Try-it (round 363): create Sphere, component LODGroup - three levels", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("create Sphere");
    ed.command("component LODGroup");
    GameObject *g = ed.active_object_for_test();
    CHECK(g && g->get<LODGroupComponent>() && g->get<LODGroupComponent>()->levels == 3);
  });
}

/* ---------------------------------------------------------------- round 364: the Sequencer's cards and camera cuts */
static void tests_round364() {
  test("Sequencer cards (round 364): Image, Paper and Camera bars saved and read back; the topmost card and cut at a frame; a card animates nothing and has its own length", [] {
    Sequence s;
    s.tracks.resize(3);
    SeqInstance img, cut, top;
    img.kind = (int)SeqKind::Image, img.media = "Assets/Textures/a.png", img.name = "Board1", img.start = 0, img.trim_out = 120;
    top.kind = (int)SeqKind::Paper, top.media = "Assets/Papers/b.paper", top.name = "Board2", top.start = 100, top.trim_out = 40;
    cut.kind = (int)SeqKind::Camera, cut.media = "CamB", cut.name = "Cut CamB", cut.start = 30, cut.trim_out = 50;
    s.tracks[0].items = {img};
    s.tracks[1].items = {cut};
    s.tracks[2].items = {top};
    Sequence back;
    CHECK(load_sequence_text(save_sequence_text(s), back));
    CHECK(back.tracks.size() == 3 && back.tracks[0].items.size() == 1 && back.tracks[0].items[0].kind == (int)SeqKind::Image && back.tracks[0].items[0].media == "Assets/Textures/a.png");
    const SeqInstance *at50 = sequence_card_at(back, 50.0f), *at110 = sequence_card_at(back, 110.0f), *at200 = sequence_card_at(back, 200.0f);
    std::printf("    card at 50: %s, at 110: %s, at 200: %s; cut at 40: '%s', at 90: '%s'\n", at50 ? at50->name.c_str() : "-", at110 ? at110->name.c_str() : "-",
                at200 ? at200->name.c_str() : "-", sequence_camera_at(back, 40.0f).c_str(), sequence_camera_at(back, 90.0f).c_str());
    CHECK(at50 && at50->name == "Board1" && at110 && at110->name == "Board2" && !at200);
    CHECK(sequence_camera_at(back, 40.0f) == "CamB" && sequence_camera_at(back, 90.0f).empty());
    CHECK(std::fabs(back.tracks[0].items[0].duration(nullptr) - 120.0f) < 1e-3f);  // (not a clip's 60)
    AnimValues v;
    sample_sequence(back, 50.0f, v);
    CHECK(v.values.empty());
  });
  test("Sequencer cards (round 364): seq image puts a picture on the timeline, fitted over a render's frame with black bars; seq cut switches the camera in Preview and gives the old one back after; in Play the Sequencer cuts too", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("seq new Animatic");
    /* A 4 x 2 red picture: fitted over an 8 x 8 frame, a 8 x 4 band of red between black bars. */
    const std::string dir = fs::join(test_dir(), "r364");
    fs::make_dirs(dir);
    const std::string png = fs::join(dir, "board1.png");
    std::vector<uint32_t> red(8, 0xFFFF0000u);
    CHECK(write_png(png, red.data(), 4, 2, 4));
    ed.command("seq image " + png + " 0 0 48");
    Image frame;
    frame.resize(8, 8);
    ed.composite_render_card_for_test(frame, 10.0f);
    std::printf("    the frame's pixels: top 0x%08X, middle 0x%08X\n", frame.pixels[0], frame.pixels[4 * 8 + 4]);
    CHECK(frame.pixels[0] == 0xFF000000u && frame.pixels[4 * 8 + 4] == 0xFFFF0000u);
    Image after;
    after.resize(8, 8);
    std::fill(after.pixels.begin(), after.pixels.end(), 0xFF336699u);
    ed.composite_render_card_for_test(after, 60.0f);  // (past the card: the render as it was)
    CHECK(after.pixels[0] == 0xFF336699u);
    /* Review 364: a card trimmed past 60 frames keeps its length (no clip's end to clamp to); speed doesn't change it. */
    ed.command("seq trim board1 0 200");
    ed.command("seq speed board1 2");
    SequencePtr anim = ed.seq_for_test_sequence();
    const SeqInstance *board = anim ? anim->find("board1") : nullptr;
    std::printf("    the card trimmed to 0..200: %g frames long\n", board ? board->duration(nullptr) : -1.0f);
    CHECK(board && std::fabs(board->duration(nullptr) - 200.0f) < 1e-3f);
    /* Two cameras; a cut to the second from frame 10 to 40. */
    ed.command("create Camera");
    GameObject *a = ed.active_object_for_test();
    ed.command("create Camera");
    GameObject *b = ed.active_object_for_test();
    if (!a || !b) return;
    a->name = "CamA", b->name = "CamB";
    ed.scene().active_camera = a->id;
    ed.command("seq cut CamB 1 10 30");
    ed.command("seq preview on");
    ed.command("seq frame 20");
    const uint64_t during = ed.scene().active_camera;
    ed.command("seq frame 60");  // (review 364: scrubbed out of the cut, still in Preview)
    const uint64_t scrubbed_out = ed.scene().active_camera;
    ed.command("seq preview off");
    std::printf("    the camera during the cut %s, scrubbed past it %s, after Preview %s\n", during == b->id ? "CamB" : "not CamB",
                scrubbed_out == a->id ? "CamA" : "not CamA", ed.scene().active_camera == a->id ? "CamA" : "not CamA");
    CHECK(during == b->id && scrubbed_out == a->id && ed.scene().active_camera == a->id);
    /* In Play: the Sequencer component cuts. */
    Scene s;
    GameObject *c1 = s.create("C1"), *c2 = s.create("C2"), *dir_obj = s.create("Director");
    c1->add<Camera>(), c2->add<Camera>();
    s.active_camera = c1->id;
    std::remove(resolve_asset_path("Assets/Animations/R364Cuts.seq").c_str());
    SequencePtr seq = create_sequence_asset("Assets/Animations/R364Cuts.seq", "R364Cuts");
    if (!seq) return;
    seq->tracks.resize(2);
    SeqInstance cut, hold;
    cut.kind = (int)SeqKind::Camera, cut.media = "C2", cut.name = "Cut C2", cut.start = 0, cut.trim_out = 120;
    hold.kind = (int)SeqKind::Image, hold.media = "Assets/Textures/none.png", hold.name = "Hold", hold.start = 0, hold.trim_out = 300;
    seq->tracks[0].items = {cut};
    seq->tracks[1].items = {hold};  // (the sequence runs on past the cut)
    Sequencer *sq = dir_obj->add<Sequencer>();
    sq->sequence = seq->asset_path;
    PlayContext ctx;
    ctx.scene = &s;
    ctx.dt = 1.0f / 60.0f;
    sq->start(ctx);
    sq->update(ctx);
    const uint64_t in_cut = s.active_camera;
    sq->time = 3.0f;  // (frame 180: past the cut)
    sq->update(ctx);
    std::printf("    Play: the cut's camera %s, past the cut %s\n", in_cut == c2->id ? "C2" : "not C2", s.active_camera == c1->id ? "C1" : "not C1");
    CHECK(in_cut == c2->id && s.active_camera == c1->id);
  });
  test("the lesson's Try-it (round 364): seq new Animatic, window Sequencer - a sequence to lay cards and cuts on; a cut to no camera refused", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("seq new Animatic");
    ed.command("window Sequencer");
    ed.command("seq cut NoCamera 0 0 30");
    SequencePtr s = ed.seq_for_test_sequence();
    CHECK(s && (s->tracks.empty() || s->tracks[0].items.empty()));
  });
}

/* ---------------------------------------------------------------- round 365: the Sequencer's sound (task 0342) */
namespace r365 {
/* A WAV's bytes: `tag` 1 PCM / 3 float / 0xFFFE extensible (of PCM), `bits`, `channels`, the samples as given. */
std::string wav_bytes(int tag, int bits, int channels, int rate, const std::string &samples) {
  std::string out;
  auto u32 = [&](uint32_t v) { for (int k = 0; k < 4; k++) out += (char)(v >> (8 * k) & 255); };
  auto u16 = [&](uint16_t v) { out += (char)(v & 255), out += (char)(v >> 8); };
  const int fmt_size = tag == 0xFFFE ? 40 : 16, block = channels * bits / 8;
  out += "RIFF", u32((uint32_t)(4 + 8 + fmt_size + 8 + samples.size())), out += "WAVEfmt ", u32((uint32_t)fmt_size);
  u16((uint16_t)tag), u16((uint16_t)channels), u32((uint32_t)rate), u32((uint32_t)(rate * block)), u16((uint16_t)block), u16((uint16_t)bits);
  if (tag == 0xFFFE) {
    u16(22), u16((uint16_t)bits), u32(3), u16(1);  // (cbSize, valid bits, channel mask, the sub-format GUID's first two bytes: PCM)
    for (int k = 0; k < 14; k++) out += (char)0;
  }
  out += "data", u32((uint32_t)samples.size());
  return out + samples;
}
std::string s16(std::initializer_list<int> v) {
  std::string out;
  for (int x : v) out += (char)(x & 255), out += (char)((x >> 8) & 255);
  return out;
}
/* A mono 16-bit WAV of `seconds` holding `level` throughout, at `rate`. */
std::string dc_wav(float level, double seconds, int rate) {
  std::string samples;
  const int v = (int)std::lround(level * 32768.0f);
  for (long k = 0; k < (long)(seconds * rate); k++) samples += (char)(v & 255), samples += (char)((v >> 8) & 255);
  return wav_bytes(1, 16, 1, rate, samples);
}
}  // namespace r365

static void tests_round365() {
  test("Sequencer sound (round 365): WAVs decoded - 16-bit stereo, 8-bit mono, 24-bit, 32-bit float and WAVE_FORMAT_EXTENSIBLE; a compressed one and a non-WAV refused; a 16-bit WAV written and read back", [] {
    using namespace r365;
    AudioClip a;
    std::string why;
    CHECK(decode_wav(wav_bytes(1, 16, 2, 44100, s16({16384, -16384, 0, 32767})), a, why));
    CHECK(a.rate == 44100 && a.frames() == 2 && a.stereo[0] == 0.5f && a.stereo[1] == -0.5f && a.stereo[2] == 0.0f);
    CHECK(decode_wav(wav_bytes(1, 8, 1, 8000, std::string{(char)192, (char)128}), a, why));
    CHECK(a.frames() == 2 && a.stereo[0] == 0.5f && a.stereo[1] == 0.5f && a.stereo[2] == 0.0f);  // (mono: both sides)
    CHECK(decode_wav(wav_bytes(1, 24, 1, 48000, std::string{(char)0, (char)0, (char)0xC0}), a, why));
    CHECK(a.frames() == 1 && a.stereo[0] == -0.5f);
    float f = 0.25f;
    std::string fb(4, '\0');
    std::memcpy(&fb[0], &f, 4);
    CHECK(decode_wav(wav_bytes(3, 32, 1, 48000, fb), a, why) && a.stereo[0] == 0.25f);
    CHECK(decode_wav(wav_bytes(0xFFFE, 16, 1, 48000, s16({-16384})), a, why) && a.stereo[0] == -0.5f);
    CHECK(!decode_wav(wav_bytes(2, 4, 1, 48000, "xx"), a, why));
    std::printf("    a compressed WAV refused: %s\n", why.c_str());
    CHECK(why.find("compressed") != std::string::npos);
    CHECK(!decode_wav("ID3 an mp3", a, why) && why == "not a WAV file");
    /* Review 365: malformed and unusable ones. */
    std::string cut = wav_bytes(1, 16, 1, 8000, s16({16384, 16384}));
    cut[cut.size() - 4 - 4] = (char)100;  // (the data chunk claims 100 bytes; 4 are there)
    CHECK(decode_wav(cut, a, why) && a.frames() == 2);
    CHECK(!decode_wav(wav_bytes(1, 16, 0, 8000, s16({1})), a, why));
    std::printf("    0 channels: %s\n", why.c_str());
    std::string early = "RIFF____WAVEdata" + std::string("\x02\x00\x00\x00", 4) + s16({1});
    CHECK(!decode_wav(early, a, why) && why == "data before its format");
    CHECK(!decode_wav(wav_bytes(3, 16, 1, 8000, s16({1})), a, why) && why.find("float") != std::string::npos);
    CHECK(!decode_wav(wav_bytes(1, 8, 1, 1000, std::string(3600001, (char)128)), a, why) && why.find("hour") != std::string::npos);
    const std::vector<float> ramp = {-1.0f, 1.0f, -0.5f, 0.5f, 0.25f, -0.25f};
    CHECK(decode_wav(encode_wav16(ramp, 48000), a, why) && a.rate == 48000 && a.frames() == 3);
    bool same = a.stereo.size() == ramp.size();
    for (size_t k = 0; same && k < ramp.size(); k++) same = std::fabs(a.stereo[k] - ramp[k]) < 1.0f / 16000.0f;
    CHECK(same);
  });
  test("Sequencer sound (round 365): a sound bar mixed where it sits - silent before and after it, its volume, its trim's offset into the sound, a sound at half the rate resampled, Play's Speed; saved with its volume and read back", [] {
    auto dc = std::make_shared<AudioClip>();
    dc->rate = 48000;
    dc->stereo.assign(48000 * 2 * 2, 0.5f);  // (2 s of 0.5)
    auto ramp = std::make_shared<AudioClip>();
    ramp->rate = 24000;  // (half the output's rate: each value the time in seconds)
    for (int k = 0; k < 24000 * 2; k++) ramp->stereo.push_back(k / 24000.0f * 0.25f), ramp->stereo.push_back(k / 24000.0f * 0.25f);
    Sequence s;
    s.frame_rate = 60.0f;
    s.tracks.resize(1);
    SeqInstance bar;
    bar.kind = (int)SeqKind::Audio, bar.media = "dc.wav", bar.name = "Hum", bar.start = 30, bar.trim_in = 0, bar.trim_out = 60, bar.volume = 0.5f;
    s.tracks[0].items = {bar};
    const SoundSet sounds = {{"dc.wav", dc}, {"ramp.wav", ramp}};
    std::vector<float> out;
    mix_sequence_audio(s, sounds, 0.0, 96000, 48000, out);
    std::printf("    at 0.25 s %g, 0.75 s %g, 1.6 s %g\n", out[12000 * 2], out[36000 * 2], out[76800 * 2]);
    CHECK(out[12000 * 2] == 0.0f && std::fabs(out[36000 * 2] - 0.25f) < 1e-6f && out[76800 * 2] == 0.0f);  // (the bar: 0.5 s to 1.5 s, at half volume)
    /* The ramp from half a second in (trim_in 30 frames): 0.5 s at the bar's start, 0.75 s a quarter second on. */
    SeqInstance r;
    r.kind = (int)SeqKind::Audio, r.media = "ramp.wav", r.name = "Ramp", r.start = 0, r.trim_in = 30, r.trim_out = 120;
    s.tracks[0].items = {r};
    mix_sequence_audio(s, sounds, 0.0, 24000, 48000, out);
    std::printf("    the ramp trimmed half a second in: %g at 0 s, %g at 0.25 s (0.125, 0.1875)\n", out[0], out[12000 * 2]);
    CHECK(std::fabs(out[0] - 0.125f) < 1e-4f && std::fabs(out[12000 * 2] - 0.1875f) < 1e-4f && std::fabs(out[12001 * 2] - (0.75f + 1.0f / 48000.0f) * 0.25f) < 1e-4f);
    mix_sequence_audio(s, sounds, 0.0, 24000, 48000, out, 2.0);  // (twice as fast: a quarter second in, the sound is a half second further)
    CHECK(std::fabs(out[12000 * 2] - 0.25f) < 1e-4f);
    s.tracks[0].items = {bar};
    Sequence back;
    CHECK(load_sequence_text(save_sequence_text(s), back));
    const SeqInstance *b = back.tracks.empty() || back.tracks[0].items.empty() ? nullptr : &back.tracks[0].items[0];
    CHECK(b && b->kind == (int)SeqKind::Audio && b->media == "dc.wav" && b->volume == 0.5f && b->duration(nullptr) == 60.0f);
    CHECK(sequence_has_audio(back) && !sequence_card_at(back, 40.0f) && sequence_camera_at(back, 40.0f).empty());
  });
  test("Sequencer sound (round 365): seq sound puts a WAV on the timeline at its own length; Preview's Play starts the sound at the playhead, a jump moves it, Stop silences it; not a WAV refused; Render Animation writes sound.wav beside the frames (and an AAC stream into the MP4 when built with FFmpeg)", [] {
    using namespace r365;
    const std::string dir = fs::join(test_dir(), "r365");
    fs::make_dirs(dir);
    const std::string wav = fs::join(dir, "hum.wav"), png = fs::join(dir, "not_a_sound.png");
    CHECK(fs::write_file(wav, dc_wav(0.5f, 1.0, 44100)));
    std::vector<uint32_t> px(4, 0xFF808080u);
    CHECK(write_png(png, px.data(), 2, 2, 2));
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("engine raster");
    ed.scene().render.width = 64, ed.scene().render.height = 36;
    ed.command("seq new R365 Sound");
    ed.command("seq sound " + wav + " 0 30");
    ed.command("seq sound " + png + " 0 200");
    SequencePtr seq = ed.seq_for_test_sequence();
    const SeqInstance *hum = seq ? seq->find("hum") : nullptr;
    std::printf("    the sound bar: %s, %g frames long at %g fps; bars %zu\n", hum ? hum->name.c_str() : "-", hum ? hum->duration(nullptr) : -1.0f,
                seq ? seq->frame_rate : 0.0f, seq && !seq->tracks.empty() ? seq->tracks[0].items.size() : 0);
    CHECK(hum && hum->kind == (int)SeqKind::Audio && std::fabs(hum->duration(nullptr) - seq->frame_rate) < 1e-3f);
    if (!hum) return;
    size_t bars = 0;
    for (const SeqTrack &t : seq->tracks) bars += t.items.size();
    CHECK(bars == 1);  // (the PNG refused)
    if (!hum) return;
    ed.command("seq volume hum 0.5");
    double pos = 0.0;
    bool active = false;
    ed.command("seq frame 40");
    ed.command("seq play");
    ed.seq_audio_tick_for_test();
    CHECK(ed.seq_audio_for_test(pos, active) && active && std::fabs(pos - 40.0) < 1.0);
    std::vector<float> buf(480 * 2);
    ed.seq_audio_pull_for_test(buf.data(), 480);
    std::printf("    playing at frame 40: the sound at %g (0.25), the stream at frame %g\n", buf[0], pos);
    CHECK(std::fabs(buf[0] - 0.25f) < 1e-3f && std::fabs(buf[959] - 0.25f) < 1e-3f);
    ed.command("seq frame 200");  // (a jump past the sound)
    ed.seq_audio_tick_for_test();
    CHECK(ed.seq_audio_for_test(pos, active) && active && std::fabs(pos - 200.0) < 1e-3);
    ed.seq_audio_pull_for_test(buf.data(), 480);
    CHECK(buf[0] == 0.0f);
    /* Review 365: the sound only while the picture moves forward - held (paused) or a step back is silent; a step on
     * plays again from there. */
    ed.seq_audio_tick_for_test();
    const bool held = ed.seq_audio_for_test(pos, active) && !active;
    ed.command("seq frame 45");
    ed.seq_audio_tick_for_test();
    ed.command("seq frame 46");
    ed.seq_audio_tick_for_test();
    const bool on_again = ed.seq_audio_for_test(pos, active) && active && std::fabs(pos - 46.0) < 1.5;
    ed.command("seq frame 40");
    ed.seq_audio_tick_for_test();
    const bool back = ed.seq_audio_for_test(pos, active) && !active;
    std::printf("    held: %s; on again at 46: %s; a step back: %s\n", held ? "silent" : "SOUND", on_again ? "sound" : "SILENT", back ? "silent" : "SOUND");
    CHECK(held && on_again && back);
    ed.command("seq stop");
    ed.seq_audio_tick_for_test();
    CHECK(ed.seq_audio_for_test(pos, active) && !active);
    ed.command("seq preview off");
    /* Render Animation over the sound: frames 30 to 41 (12 frames at the sequence's rate) and their sound. */
    ed.command("renderanim 30 41");
    for (int i = 0; i < 200 && ed.render_animation_for_test().active; i++) ed.step_frame_headless();
    const auto &ra = ed.render_animation_for_test();
    std::string bytes, why;
    AudioClip got;
    const bool read = fs::read_file(fs::join(ra.dir, "sound.wav"), bytes) && decode_wav(bytes, got, why);
    const size_t want = (size_t)std::llround(ra.written.size() / (double)ra.fps * 48000.0);
    std::printf("    %zu frame(s) at %g fps; sound.wav %s: %zu samples (%zu), %g\n", ra.written.size(), ra.fps, read ? "read" : why.c_str(),
                got.frames(), want, got.frames() ? got.stereo[2000] : -1.0f);
    CHECK(ra.written.size() == 12 && read && got.frames() == want && std::fabs(got.stereo[2000] - 0.25f) < 1e-3f);
    const std::vector<std::string> args = ffmpeg_png_to_mp4_args("ffmpeg", "R/a", 60.0f, "R/a.mp4", "R/a/sound.wav");
    CHECK(args.size() == 25 && args[11] == "-i" && args[12] == "R/a/sound.wav" && args[17] == "-shortest" && args.back() == "R/a.mp4");
    auto clean = [&] {  // (the scratch project's Renders and sequences left as they were)
      for (const std::string &f : ra.written) std::remove(f.c_str());
      std::remove(fs::join(ra.dir, "sound.wav").c_str());
      std::remove(ra.video.c_str());
      std::error_code ec;
      std::filesystem::remove(ra.dir, ec);  // (only once empty)
      std::remove(resolve_asset_path(seq->asset_path).c_str());
    };
    for (int i = 0; i < 400 && ed.render_animation_for_test().encode; i++) {  // (an ffmpeg program reads the frames: let it finish)
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      ed.step_frame_headless();
    }
    if (!video::available()) return clean();
    for (int i = 0; i < 400 && ed.render_animation_for_test().encode; i++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      ed.step_frame_headless();
    }
    int rate = 0;
    const int streams = video::probe_audio(ra.video, &rate);
    std::printf("    the MP4: %d audio stream(s) at %d Hz, %d frames\n", streams, rate, video::probe_frames(ra.video));
    CHECK(streams == 1 && rate == 48000 && video::probe_frames(ra.video) == 12);
    clean();
  });
  test("Sequencer sound (round 365): the default audio device opened and fed (BLENDITY_TEST_AUDIO_OUT=1 only: a quiet 440 Hz tone for a third of a second; CI has no device)", [] {
    if (!std::getenv("BLENDITY_TEST_AUDIO_OUT")) return;
    audio_out::Stream out;
    std::atomic<size_t> fed{0};
    std::string why;
    const bool open = out.open(48000, [&](float *o, size_t n) {
      for (size_t k = 0; k < n; k++) o[k * 2] = o[k * 2 + 1] = 0.05f * std::sin(6.2831853f * 440.0f * (float)(fed + k) / 48000.0f);
      fed += n;
    }, &why);
    std::this_thread::sleep_for(std::chrono::milliseconds(330));
    out.close();
    std::printf("    the device: %s; %zu frames fed\n", open ? "open" : why.c_str(), fed.load());
    CHECK(open && fed.load() >= 9600);
  });
  test("the lesson's Try-it (round 365): seq new Film, window Sequencer - a sequence to drop WAV sounds on; seq sound refuses a file that isn't one", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("seq new Film");
    ed.command("window Sequencer");
    const std::string mp3 = fs::join(test_dir(), "r365_song.mp3");
    CHECK(fs::write_file(mp3, "ID3 not a wav"));
    ed.command("seq sound " + mp3 + " 0 0");  // (there, but not a WAV)
    SequencePtr s = ed.seq_for_test_sequence();
    CHECK(s && (s->tracks.empty() || s->tracks[0].items.empty()));
    if (s) std::remove(resolve_asset_path(s->asset_path).c_str());
    std::remove(mp3.c_str());
  });
}

/* ---------------------------------------------------------------- round 366: no MP4 says how to get one (task 0343) */
static void tests_round366() {
  test("Render Animation without a video maker (round 366): the message names the frames' folder and why, with a Fix that gets Blender's libraries (FFmpeg) - or turns them back on when they were turned off", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.step_frame_headless();  // (the toasts' first look takes the log so far as history)
    /* Which Fix goes with which state (review 366): a full build without FFmpeg points to an ffmpeg program;
     * libraries off with the full build there turns them on; otherwise they're downloaded. */
    const bool full = launch::status().full, installed = ed.full_build_installed_for_test();
    auto offer = [&](const std::string &dir) {
      ed.clear_toasts_for_test();
      ed.offer_video_libraries_for_test(dir);
      ed.step_frame_headless();
      const Editor::Toast *found = nullptr;
      for (const Editor::Toast &x : ed.toasts_for_test()) found = x.text.find("Render Animation") != std::string::npos ? &x : found;
      return found ? *found : Editor::Toast{};
    };
    const Editor::Toast on = offer("Renders/R366_animation_1");
    std::printf("    the toast: %s\n    its Fix: %s (%s)\n", on.text.c_str(), on.fix_label.c_str(), on.fix_cmd.c_str());
    CHECK(on.text.find("Renders/R366_animation_1") != std::string::npos && on.text.find("ffmpeg program") != std::string::npos);
    CHECK(on.fix_cmd == (full ? "preferences" : "update libs") && !on.fix_label.empty());
    ed.command("update libsoff");
    const Editor::Toast off = offer("Renders/R366_animation_2");
    std::printf("    turned off (the full build %s): %s (%s)\n", installed ? "there" : "not there", off.fix_label.c_str(), off.fix_cmd.c_str());
    CHECK(off.fix_cmd == (full ? "preferences" : installed ? "update libson" : "update libs"));
    CHECK(full || !installed || off.text.find("turned off") != std::string::npos);
    ed.command("update libson");
  });
  test("Render Animation without a video maker (round 366): a render's finish in the dependency-free build with no ffmpeg program ends in that message (the libraries' build makes the MP4 instead)", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("engine raster");
    ed.scene().render.width = 32, ed.scene().render.height = 18;
    ed.command("seq new R366 Video");
    const std::string png = fs::join(test_dir(), "r366_board.png");
    std::vector<uint32_t> px(4, 0xFF336699u);
    CHECK(write_png(png, px.data(), 2, 2, 2));
    ed.command("seq image " + png + " 0 0 3");  // (a 3-frame card: something to render)
    SequencePtr seq = ed.seq_for_test_sequence();
    ed.step_frame_headless();
    ed.clear_toasts_for_test();
    ed.command("renderanim 0 2");
    for (int i = 0; i < 100 && ed.render_animation_for_test().active; i++) ed.step_frame_headless();
    ed.step_frame_headless();
    const auto &ra = ed.render_animation_for_test();
    bool offered = false;
    for (const Editor::Toast &x : ed.toasts_for_test()) offered = offered || (x.text.find("no MP4") != std::string::npos && x.fix_cmd.rfind("update libs", 0) == 0);
    const bool maker = video::available() || !ed.ffmpeg_program().empty();
    std::printf("    %zu frame(s); a video maker here: %s; the offer shown: %s\n", ra.written.size(), maker ? "yes" : "no", offered ? "yes" : "no");
    CHECK(ra.written.size() == 3 && offered == !maker);
    for (int i = 0; i < 400 && ed.render_animation_for_test().encode; i++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      ed.step_frame_headless();
    }
    for (const std::string &f : ra.written) std::remove(f.c_str());
    std::remove(ra.video.c_str());
    std::error_code ec;
    std::filesystem::remove(ra.dir, ec);
    if (seq) std::remove(resolve_asset_path(seq->asset_path).c_str());
    std::remove(png.c_str());
  });
  test("the lesson's Try-it (round 366): libs lists Blender's libraries this build uses - FFmpeg among them or not", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    std::vector<LogEntry> before;
    const size_t from = Log::fetch(0, before);
    ed.command("libs");  // (the lesson's Try-it line)
    std::vector<LogEntry> lines;
    Log::fetch(from, lines);
    bool row = false;
    for (const LogEntry &e : lines) row = row || e.text.rfind("FFmpeg", 0) == 0;  // (its row: in use, or "not built in")
    CHECK(row);
  });
}

/* ---------------------------------------------------------------- round 367: the small fixes (task 0344) */
static void tests_round367() {
  test("create (round 367): a Capsule 2 m tall and 1 m across; a second one named \"Capsule (1)\" as Unity names it; an unknown kind refused, making nothing; a kind in any case", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    size_t before = 0;
    ed.scene().for_each([&](GameObject &) { before++; });
    ed.command("create Capsule");
    GameObject *a = ed.active_object_for_test();
    const std::string first = a ? a->name : "";
    ed.command("create Capsule");
    GameObject *b = ed.active_object_for_test();
    const Mesh *m = a && a->get<MeshFilter>() ? a->get<MeshFilter>()->mesh.get() : nullptr;
    AABB box;
    for (const Vec3 &p : m ? m->positions : std::vector<Vec3>()) box.add(p);
    std::printf("    %s and %s; the capsule %.3f tall, %.3f across, %zu faces\n", first.c_str(), b ? b->name.c_str() : "-", box.max.y - box.min.y, box.max.x - box.min.x,
                m ? m->face_count() : 0);
    CHECK(m && std::fabs(box.max.y - box.min.y - 2.0f) < 1e-4f && std::fabs(box.max.x - box.min.x - 1.0f) < 1e-3f);
    CHECK(b && b != a && b->name == first + " (1)");
    size_t mid = 0;
    ed.scene().for_each([&](GameObject &) { mid++; });
    ed.command("create Capsul");  // (a typo: refused, nothing made - it made an empty object named "Capsul")
    ed.command("create point light");
    size_t after = 0;
    bool typo = false, light = false;
    ed.scene().for_each([&](GameObject &g) {
      after++;
      typo = typo || g.name == "Capsul";
      light = light || (g.get<Light>() && g.name.rfind("Point Light", 0) == 0);
    });
    CHECK(mid == before + 2 && after == mid + 1 && !typo && light);
    ed.command("create armature");  // (review 367: the special kinds in any case too)
    GameObject *rig = ed.active_object_for_test();
    CHECK(rig && (rig->get<Bone>() || rig->get<Armature>()));
  });
  test("Join (round 367): after the selection ends on a light (Select All's way), the last mesh selected keeps the result instead of a refusal", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("create Cube");
    const std::string cube = ed.active_object_for_test()->name;
    ed.command("create Sphere");
    GameObject *sphere = ed.active_object_for_test();
    const std::string sname = sphere->name;
    const size_t faces = sphere->get<MeshFilter>()->mesh->face_count();
    ed.command("create Point Light");
    const std::string lname = ed.active_object_for_test()->name;
    ed.command("select " + cube);
    ed.command("selectadd " + sname);
    ed.command("selectadd " + lname);  // (the active one: a light)
    ed.command("join");
    GameObject *kept = ed.scene().find_by_name(sname);
    const bool cube_gone = ed.scene().find_by_name(cube) == nullptr;
    const size_t now = kept && kept->get<MeshFilter>() ? kept->get<MeshFilter>()->mesh->face_count() : 0;
    std::printf("    %s kept: %zu faces (was %zu); %s joined into it: %s\n", sname.c_str(), now, faces, cube.c_str(), cube_gone ? "yes" : "no");
    CHECK(kept && cube_gone && now == faces + 6);
  });
  test("a new Camera (round 367): where the Scene view is, looking where it looks (it faced +Z at the pivot)", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("camera 30 -20 8 1 0 2");
    ed.command("create Camera");
    GameObject *c = ed.active_object_for_test();
    const Quat q = Quat::euler({-20.0f, 30.0f, 0.0f});
    const Vec3 fwd = q.rotate({0, 0, 1}), eye = Vec3(1, 0, 2) - fwd * 8.0f;
    const Vec3 got = c ? c->world_rotation().rotate({0, 0, 1}) : Vec3(0.0f);
    std::printf("    the camera at (%.2f %.2f %.2f), the view's eye (%.2f %.2f %.2f); facing dot %.4f\n", c ? c->world_position().x : 0.0f,
                c ? c->world_position().y : 0.0f, c ? c->world_position().z : 0.0f, eye.x, eye.y, eye.z, dot(got, fwd));
    CHECK(c && c->get<Camera>() && length(c->world_position() - eye) < 1e-3f && dot(got, fwd) > 0.9999f);
  });
  test("the console (round 367): help lists every command; rotate, scale and set Transform.*; ui scale is the UI's; save to a path, new, open it again; saverender to a path", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    std::vector<LogEntry> lines;
    const size_t from = Log::fetch(0, lines);
    ed.command("help");
    lines.clear();
    Log::fetch(from, lines);
    std::string all;
    for (const LogEntry &e : lines) all += e.text + "\n";
    CHECK(all.find("zfight") != std::string::npos && all.find("wpstroke") != std::string::npos && all.find("rotate") != std::string::npos);
    ed.command("create Cube");
    GameObject *g = ed.active_object_for_test();
    const std::string name = g->name;
    ed.command("rotate 0 90 0");
    ed.command("scale 2");
    ed.command("set Transform.Position 1 2 3");
    const Vec3 e = g->local().euler_hint, s = g->local().scale, p = g->local().position;
    std::printf("    rotation (%g %g %g), scale (%g %g %g), position (%g %g %g)\n", e.x, e.y, e.z, s.x, s.y, s.z, p.x, p.y, p.z);
    CHECK(std::fabs(e.y - 90.0f) < 1e-4f && s.x == 2.0f && s.z == 2.0f && p.x == 1.0f && p.y == 2.0f && p.z == 3.0f);
    ed.command("scale 1 3 1");
    CHECK(g->local().scale.y == 3.0f && g->local().scale.x == 1.0f);
    ed.command("scale 0");  // (refused: a flat matrix)
    CHECK(g->local().scale.y == 3.0f);
    ed.command("ui scale 1.25");
    CHECK(std::fabs(ed.ui_scale_pref_for_test() - 1.25f) < 1e-6f);
    ed.command("ui scale 0");
    /* A save that can't be written (a folder under a file) leaves the scene's path and name, and no file. */
    const std::string blocker = fs::join(test_dir(), "r367_blocker");
    CHECK(fs::write_file(blocker, "x"));
    const std::string path_before = ed.scene().path, name_before = ed.scene().name;
    ed.command("save " + fs::join(blocker, "x"));
    CHECK(ed.scene().path == path_before && ed.scene().name == name_before && !fs::exists(fs::join(blocker, "x.scene")));
    std::remove(blocker.c_str());
    /* A bare name goes to Assets/Scenes, as Save As puts it; open takes the same word. */
    const std::string bare = resolve_asset_path("Assets/Scenes/R367Bare.scene");
    std::remove(bare.c_str());
    ed.command("save R367Bare");
    const bool in_scenes = fs::exists(bare);
    ed.command("open R367Bare");
    CHECK(in_scenes && ed.scene().path == bare);
    std::remove(bare.c_str());
    /* save <path> / new / open <path>, in the scratch project. */
    const std::string rel = "Assets/Scenes/R367 Console.scene";
    std::remove(resolve_asset_path(rel).c_str());
    ed.command("save Assets/Scenes/R367 Console");
    const bool saved = fs::exists(resolve_asset_path(rel));
    ed.command("new");
    const bool emptied = ed.scene().find_by_name(name) == nullptr;
    ed.command("open " + rel);
    GameObject *back = ed.scene().find_by_name(name);
    std::printf("    saved %s, gone after new %s, back after open %s\n", saved ? "yes" : "no", emptied ? "yes" : "no", back ? "yes" : "no");
    CHECK(saved && emptied && back && back->local().scale.y == 3.0f);
    ed.command("open Assets/Scenes/none.scene");  // (refused: no file)
    /* saverender <path>: no question asked. */
    ed.command("engine raster");
    ed.scene().render.width = 32, ed.scene().render.height = 18;
    ed.command("render");
    const std::string png = resolve_asset_path("Renders/r367 still.png");
    std::remove(png.c_str());
    ed.command("saverender Renders/r367 still.png");
    CHECK(fs::exists(png));
    std::remove(png.c_str());
    /* No extension: the format's; a type it doesn't write: refused, nothing written (review 367). */
    const std::string noext = resolve_asset_path("Renders/r367 plain"), bmp = resolve_asset_path("Renders/r367 still.bmp");
    ed.command("saverender Renders/r367 plain");
    ed.command("saverender Renders/r367 still.bmp");
    CHECK(fs::exists(noext + ".png") && !fs::exists(bmp));
    std::remove((noext + ".png").c_str());
    std::remove(resolve_asset_path(rel).c_str());
  });
  test("the lesson's Try-it (round 367): create Capsule, then rotate and scale it from the console", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("create Capsule");
    ed.command("rotate 0 0 90");
    ed.command("scale 0.5");
    GameObject *g = ed.active_object_for_test();
    CHECK(g && g->name.rfind("Capsule", 0) == 0 && std::fabs(g->local().euler_hint.z - 90.0f) < 1e-4f && g->local().scale.x == 0.5f);
  });
}

/* ---------------------------------------------------------------- round 369: Unity packages (task 0346) */
namespace r369 {
/* A tar entry: a ustar header (its name split into prefix and name past 100 bytes) and the data in 512-byte blocks. */
void tar_add(std::string &tar, std::string name, const std::string &data, char type = '0') {
  std::string h(512, '\0');
  std::string prefix;
  if (name.size() > 100) {
    const size_t cut = name.rfind('/', name.size() - 1 - 0);
    prefix = name.substr(0, cut), name = name.substr(cut + 1);
  }
  std::copy(name.begin(), name.end(), h.begin());
  const std::string mode = "0000644", size = strprintf("%011llo", (unsigned long long)data.size());
  std::copy(mode.begin(), mode.end(), h.begin() + 100);
  std::copy(size.begin(), size.end(), h.begin() + 124);
  h[156] = type;
  const std::string magic = "ustar";
  std::copy(magic.begin(), magic.end(), h.begin() + 257);
  h[263] = '0', h[264] = '0';
  std::copy(prefix.begin(), prefix.end(), h.begin() + 345);
  std::fill(h.begin() + 148, h.begin() + 156, ' ');
  unsigned sum = 0;
  for (char c : h) sum += (uint8_t)c;
  const std::string chk = strprintf("%06o", sum);
  std::copy(chk.begin(), chk.end(), h.begin() + 148);
  tar += h + data + std::string((512 - data.size() % 512) % 512, '\0');
}
/* gzip with stored deflate blocks (the reader inflates any deflate; tests needn't compress). */
std::string gzip_stored(const std::string &data, bool with_name) {
  std::string out = {(char)0x1f, (char)0x8b, 8, (char)(with_name ? 8 : 0), 0, 0, 0, 0, 0, 3};
  if (with_name) out += std::string("archtemp.tar") + '\0';
  size_t at = 0;
  do {
    const size_t n = std::min<size_t>(65535, data.size() - at);
    const bool last = at + n == data.size();
    out += (char)(last ? 1 : 0);
    out += (char)(n & 255), out += (char)(n >> 8), out += (char)(~n & 255), out += (char)((~n >> 8) & 255);
    out += data.substr(at, n);
    at += n;
  } while (at < data.size());
  for (int k = 0; k < 4; k++) out += '\0';  // (CRC32: not checked)
  for (int k = 0; k < 4; k++) out += (char)(data.size() >> (8 * k) & 255);
  return out;
}
void asset(std::string &tar, const std::string &guid, const std::string &pathname, const std::string *data, bool folder = false) {
  tar_add(tar, guid + "/", "", '5');
  if (data) tar_add(tar, guid + "/asset", *data);
  tar_add(tar, guid + "/asset.meta", "fileFormatVersion: 2\nguid: " + guid + "\n" + (folder ? "folderAsset: yes\n" : "") + "DefaultImporter:\n  userData: \n");
  tar_add(tar, guid + "/pathname", pathname + "\n00");
}
}  // namespace r369

static void tests_round369() {
  test("Unity packages (round 369): gzip with a name field, tar with ustar's prefix and GNU's long name, a .meta's GUID, and package paths kept inside their folder (.., absolute, drives, backslashes refused)", [] {
    using namespace r369;
    std::string tar;
    const std::string deep = "Assets/" + std::string(70, 'a') + "/" + std::string(60, 'b') + "/file.txt";
    tar_add(tar, deep, "deep");
    tar_add(tar, "././LongLink", std::string(130, 'c') + std::string("/x.txt") + '\0', 'L');
    tar_add(tar, "ignored", "long");
    std::string out, why;
    CHECK(gunzip(gzip_stored(tar, true), out, why) && out == tar);
    std::vector<TarEntry> e;
    CHECK(read_tar(out, e, why) && e.size() == 2);
    CHECK(e.size() == 2 && e[0].name == deep && e[0].data == "deep" && e[1].name == std::string(130, 'c') + "/x.txt" && e[1].data == "long");
    std::string bad = gzip_stored(tar, false);
    bad.resize(bad.size() / 2);
    CHECK(!gunzip(bad, out, why));
    std::printf("    a cut gzip refused: %s\n", why.c_str());
    CHECK(!gunzip("PK\x03\x04 a zip", out, why) && why == "not a gzip file");
    CHECK(meta_guid("fileFormatVersion: 2\r\nguid: 0123abcd\r\nNativeFormatImporter:\n") == "0123abcd" && meta_guid("nothing") == "");
    const std::string into = "Assets/Imported/Kit";
    CHECK(unity_package_target("Assets/Models/box.fbx", into) == "Assets/Imported/Kit/Models/box.fbx");
    CHECK(unity_package_target("Packages/com.vrchat/Runtime/a.cs", into) == "Assets/Imported/Kit/Packages/com.vrchat/Runtime/a.cs");
    for (const char *evil : {"Assets/../../evil.txt", "../x", "/etc/passwd", "C:/Windows/x.dll", "Assets\\..\\..\\x", "Assets", ""})
      CHECK(unity_package_target(evil, into).empty());
    /* Review 369: not UTF-8, control characters, Windows devices, parts ending in a dot or a space. */
    for (const char *evil : {"Assets/bad\xFF\xFE.txt", "Assets/tab\there.txt", "Assets/NUL", "Assets/aux.png", "Assets/COM1.txt", "Assets/x.", "Assets/x /y.txt"})
      CHECK(unity_package_target(evil, into).empty());
    CHECK(unity_package_target("Assets/Ünïcode ✓.png", into) == "Assets/Imported/Kit/Ünïcode ✓.png");
    CHECK(unity_package_folder("..") == "Package" && unity_package_folder("...") == "Package" && unity_package_folder("CON") == "Package" &&
          unity_package_folder(" My Kit. ") == "My Kit" && unity_package_folder("a/b:c") == "a_b_c");
    CHECK(unity_path_key("Assets/Imported/K/Case.TXT") == unity_path_key("assets/imported/k/case.txt.") && valid_utf8("plain") && !valid_utf8("\xC0\xAF"));
    /* A size the trailer lies about: inflating stops at the limit, refused - 5000 bytes against a limit of 1000. */
    std::string big = gzip_stored(std::string(5000, 'z'), false);
    for (int k = 0; k < 4; k++) big[big.size() - 4 + k] = k == 0 ? 10 : 0;  // (ISIZE 10)
    CHECK(!gunzip(big, out, why, 1000) && why == "too large to unpack");
    CHECK(!gunzip(big, out, why) && why.find("doesn't match") != std::string::npos);
  });
  test("Unity packages (round 369): import <file>.unitypackage unpacks into Assets/Imported/<package>/ with Unity's folders - a model, a texture, a script kept as it is, a folder - the GUIDs kept beside them; paths that would leave the folder never written", [] {
    using namespace r369;
    const std::string dir = fs::join(test_dir(), "r369");
    fs::make_dirs(dir);
    const std::string png_path = fs::join(dir, "red.png");
    std::vector<uint32_t> red(4, 0xFFFF0000u);
    CHECK(write_png(png_path, red.data(), 2, 2, 2));
    std::string png, obj = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", cs = "using UnityEngine;\npublic class Spin : MonoBehaviour { }\n", evil = "escaped";
    CHECK(fs::read_file(png_path, png));
    std::string tar;
    asset(tar, "11111111111111111111111111111111", "Assets/Kit Models", nullptr, true);
    asset(tar, "22222222222222222222222222222222", "Assets/Kit Models/tri.obj", &obj);
    asset(tar, "33333333333333333333333333333333", "Assets/Textures/red.png", &png);
    asset(tar, "44444444444444444444444444444444", "Assets/Scripts/Spin.cs", &cs);
    asset(tar, "55555555555555555555555555555555", "Assets/../../r369_evil.txt", &evil);
    asset(tar, "66666666666666666666666666666666", "C:/r369_evil.txt", &evil);
    std::string first = "first", second = "second", bad = "bad";
    asset(tar, "77777777777777777777777777777777", "Assets/Notes/Case.txt", &first);
    asset(tar, "88888888888888888888888888888888", "Assets/Notes/case.txt", &second);  // (one file on Windows: the second left out)
    asset(tar, "99999999999999999999999999999999", "Assets/bad\xFF.txt", &bad);  // (not UTF-8: left out, no crash)
    asset(tar, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "Assets/Notes/twice.txt", &first);
    asset(tar, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "Assets/Notes/twice2.txt", &second);  // (one GUID twice: neither trusted)
    const std::string pkg = fs::join(dir, "R369 Kit.unitypackage");
    CHECK(fs::write_file(pkg, gzip_stored(tar, true)));
    Editor ed;
    ed.init_headless(1200, 900);
    const std::string root = resolve_asset_path("Assets/Imported/R369 Kit");
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    ed.command("import " + pkg);
    const Editor::PackageJob *job = ed.last_package_for_test();
    std::string guids, got_cs;
    fs::read_file(fs::join(root, ".unity-guids"), guids);
    fs::read_file(fs::join(root, "Scripts/Spin.cs"), got_cs);
    std::printf("    models %d, textures %d, scripts %d, folders %d, left out %zu; why '%s'\n", job ? job->counts[0] : -1, job ? job->counts[1] : -1,
                job ? job->counts[2] : -1, job ? job->folders : -1, job ? job->skipped.size() : 0, job ? job->why.c_str() : "-");
    CHECK(job && job->why.empty() && job->counts[0] == 1 && job->counts[1] == 1 && job->counts[2] == 1 && job->folders == 1 && job->skipped.size() == 5);
    std::string case_txt;
    fs::read_file(fs::join(root, "Notes/Case.txt"), case_txt);
    CHECK(case_txt == "first" && !fs::exists(fs::join(root, "Notes/twice.txt")) && !fs::exists(fs::join(root, "Notes/twice2.txt")));
    CHECK(guids.find("88888888888888888888888888888888") == std::string::npos && guids.find("aaaaaaaa") == std::string::npos);
    CHECK(fs::exists(fs::join(root, "Kit Models/tri.obj")) && fs::exists(fs::join(root, "Textures/red.png")) && got_cs == cs);
    CHECK(guids.find("22222222222222222222222222222222\tAssets/Kit Models/tri.obj\tAssets/Imported/R369 Kit/Kit Models/tri.obj") != std::string::npos);
    for (const char *where : {"r369_evil.txt", "Assets/r369_evil.txt", "Assets/Imported/r369_evil.txt"})  // (where a careless join would land)
      CHECK(!fs::exists(resolve_asset_path(where)));
    /* The unpacked model imports as any file does. */
    ed.command("import " + fs::join(root, "Kit Models/tri.obj"));
    bool tri = false;
    ed.scene().for_each([&](GameObject &g) { tri = tri || (g.get<MeshFilter>() && g.get<MeshFilter>()->mesh && g.get<MeshFilter>()->mesh->face_count() == 1); });
    CHECK(tri);
    /* Not a package: refused with why, nothing made. */
    const std::string junk = fs::join(dir, "junk.unitypackage");
    CHECK(fs::write_file(junk, "not gzip"));
    ed.command("import " + junk);
    CHECK(ed.last_package_for_test() && ed.last_package_for_test()->why == "not a gzip file");
    std::filesystem::remove_all(root, ec);
    std::filesystem::remove_all(resolve_asset_path("Assets/Imported/junk"), ec);
  });
  test("the lesson's Try-it (round 369): window Project - where an unpacked package lands; import of a package that isn't there refused", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("window Project");
    ed.command("import Assets/none.unitypackage");
    CHECK(ed.last_package_for_test() == nullptr);
  });
}

/* ---------------------------------------------------------------- round 370: Unity prefabs and scenes (task 0347) */
namespace r370 {
const char *kLamp = R"(%YAML 1.1
%TAG !u! tag:unity3d.com,2011:
--- !u!1 &100
GameObject:
  m_ObjectHideFlags: 0
  serializedVersion: 6
  m_Component:
  - component: {fileID: 400}
  - component: {fileID: 10800}
  m_Layer: 0
  m_Name: Lamp
  m_IsActive: 1
--- !u!4 &400
Transform:
  m_GameObject: {fileID: 100}
  m_LocalRotation: {x: 0, y: 0, z: 0, w: 1}
  m_LocalPosition: {x: 0, y: 2, z: 0}
  m_LocalScale: {x: 1, y: 1, z: 1}
  m_Children: []
  m_Father: {fileID: 0}
--- !u!108 &10800
Light:
  m_GameObject: {fileID: 100}
  m_Enabled: 1
  serializedVersion: 10
  m_Type: 2
  m_Color: {r: 1, g: 0.5, b: 0.25, a: 1}
  m_Intensity: 3
  m_Range: 7.5
  m_SpotAngle: 30
)";
const char *kLevel = R"(%YAML 1.1
%TAG !u! tag:unity3d.com,2011:
--- !u!1 &1
GameObject:
  m_Component:
  - component: {fileID: 2}
  - component: {fileID: 3}
  - component: {fileID: 4}
  - component: {fileID: 5}
  m_Name: Floor
  m_IsActive: 1
--- !u!4 &2
Transform:
  m_GameObject: {fileID: 1}
  m_LocalRotation: {x: 0, y: 0, z: 0, w: 1}
  m_LocalPosition: {x: 0, y: 0, z: 0}
  m_LocalScale: {x: 2, y: 1, z: 2}
  m_Children:
  - {fileID: 22}
  - {fileID: 902}
  m_Father: {fileID: 0}
--- !u!33 &3
MeshFilter:
  m_GameObject: {fileID: 1}
  m_Mesh: {fileID: 10209, guid: 0000000000000000e000000000000000, type: 0}
--- !u!23 &4
MeshRenderer:
  m_GameObject: {fileID: 1}
  m_Enabled: 1
  m_Materials:
  - {fileID: 10303, guid: 0000000000000000f000000000000000, type: 0}
--- !u!65 &5
BoxCollider:
  m_GameObject: {fileID: 1}
  m_IsTrigger: 0
  serializedVersion: 2
  m_Size: {x: 10, y: 0.1, z: 10}
  m_Center: {x: 0, y: -0.05, z: 0}
--- !u!1 &21
GameObject:
  m_Component:
  - component: {fileID: 22}
  - component: {fileID: 23}
  - component: {fileID: 24}
  - component: {fileID: 25}
  - component: {fileID: 26}
  m_Name: Tri
  m_IsActive: 1
--- !u!4 &22
Transform:
  m_GameObject: {fileID: 21}
  m_LocalRotation: {x: 0, y: 0.70710677, z: 0, w: 0.70710677}
  m_LocalPosition: {x: 1, y: 0.5, z: -2}
  m_LocalScale: {x: 1, y: 1, z: 1}
  m_Children: []
  m_Father: {fileID: 2}
--- !u!33 &23
MeshFilter:
  m_GameObject: {fileID: 21}
  m_Mesh: {fileID: 4300000, guid: aaaa0000aaaa0000aaaa0000aaaa0001, type: 3}
--- !u!23 &24
MeshRenderer:
  m_GameObject: {fileID: 21}
  m_Enabled: 1
--- !u!114 &25
MonoBehaviour:
  m_GameObject: {fileID: 21}
  m_Enabled: 1
  m_Script: {fileID: 11500000, guid: aaaa0000aaaa0000aaaa0000aaaa0002, type: 3}
  m_Name:
  m_EditorClassIdentifier:
  speed: 2
  axis: {x: 0, y: 1, z: 0}
  message: 'It''s spinning: fast'
  targets:
  - {fileID: 2}
  - {fileID: 0}
--- !u!20 &26
Camera:
  m_GameObject: {fileID: 21}
  m_Enabled: 1
  near clip plane: 0.1
  far clip plane: 500
  field of view: 45
  orthographic: 0
--- !u!1001 &900
PrefabInstance:
  m_ObjectHideFlags: 0
  serializedVersion: 2
  m_Modification:
    serializedVersion: 3
    m_TransformParent: {fileID: 2}
    m_Modifications:
    - target: {fileID: 400, guid: aaaa0000aaaa0000aaaa0000aaaa0003, type: 3}
      propertyPath: m_LocalPosition.x
      value: 3
      objectReference: {fileID: 0}
    - target: {fileID: 100, guid: aaaa0000aaaa0000aaaa0000aaaa0003, type: 3}
      propertyPath: m_Name
      value: Porch Lamp
      objectReference: {fileID: 0}
    - target: {fileID: 10800, guid: aaaa0000aaaa0000aaaa0000aaaa0003, type: 3}
      propertyPath: m_Intensity
      value: 5
      objectReference: {fileID: 0}
    m_RemovedComponents: []
  m_SourcePrefab: {fileID: 100100000, guid: aaaa0000aaaa0000aaaa0000aaaa0003, type: 3}
--- !u!4 &902 stripped
Transform:
  m_CorrespondingSourceObject: {fileID: 400, guid: aaaa0000aaaa0000aaaa0000aaaa0003, type: 3}
  m_PrefabInstance: {fileID: 900}
  m_PrefabAsset: {fileID: 0}
)";
}  // namespace r370

static void tests_round370() {
  test("Unity YAML (round 370): documents by class and fileID, a stripped one; Unity's sequences at their key's indent; flow references; quoted strings; a mapping in a sequence item", [] {
    std::vector<UnityDoc> docs;
    std::string why;
    CHECK(parse_unity_yaml(r370::kLevel, docs, why));
    CHECK(docs.size() == 13);
    const UnityDoc *mono = nullptr, *pi = nullptr, *stripped = nullptr;
    for (const UnityDoc &d : docs) {
      mono = d.class_id == 114 ? &d : mono;
      pi = d.class_id == 1001 ? &d : pi;
      stripped = d.stripped ? &d : stripped;
    }
    CHECK(mono && mono->type == "MonoBehaviour" && mono->file_id == 25 && mono->body.guid("m_Script") == "aaaa0000aaaa0000aaaa0000aaaa0002");
    CHECK(mono && mono->body.num("speed") == 2.0 && mono->body.str("message") == "It's spinning: fast" && mono->body.get("targets") &&
          mono->body.get("targets")->seq.size() == 2 && mono->body.get("targets")->seq[0].str("fileID") == "2");
    const YamlNode *mods = pi && pi->body.get("m_Modification") ? pi->body.get("m_Modification")->get("m_Modifications") : nullptr;
    CHECK(mods && mods->seq.size() == 3 && mods->seq[0].str("propertyPath") == "m_LocalPosition.x" && mods->seq[1].str("value") == "Porch Lamp" &&
          mods->seq[0].file_id("target") == 400);
    CHECK(stripped && stripped->file_id == 902 && stripped->class_id == 4 && stripped->body.file_id("m_PrefabInstance") == 900);
    CHECK(!parse_unity_yaml("\x01\x02 binary", docs, why));
    const YamlNode meta = parse_yaml("fileFormatVersion: 2\nguid: abc\nModelImporter:\n  fileIDToRecycleName:\n    4300000: Tri\n    100000: //RootNode\n");
    CHECK(meta.get("ModelImporter") && meta.get("ModelImporter")->get("fileIDToRecycleName") && meta.get("ModelImporter")->get("fileIDToRecycleName")->str("4300000") == "Tri");
  });
  test("Unity scenes (round 370): import Level.unity - the hierarchy and transforms, Unity's built-in Plane, a BoxCollider, an OBJ's mesh by its .meta's fileID, a script kept as an Engine Component with its fields, a Camera, a nested Lamp prefab with its overrides; one Undo step takes it all away", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    const std::string dir = resolve_asset_path("Assets/Imported/R370");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    fs::make_dirs(fs::join(dir, ".unity-meta"));
    CHECK(fs::write_file(fs::join(dir, "tri.obj"), "o Tri\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"));
    CHECK(fs::write_file(fs::join(dir, "Spin.cs"), "public class Spin : UnityEngine.MonoBehaviour {}\n"));
    CHECK(fs::write_file(fs::join(dir, "Lamp.prefab"), r370::kLamp));
    CHECK(fs::write_file(fs::join(dir, "Level.unity"), r370::kLevel));
    CHECK(fs::write_file(fs::join(dir, ".unity-meta/aaaa0000aaaa0000aaaa0000aaaa0001.meta"),
                         "fileFormatVersion: 2\nguid: aaaa0000aaaa0000aaaa0000aaaa0001\nModelImporter:\n  fileIDToRecycleName:\n    100000: //RootNode\n    4300000: Tri\n"));
    CHECK(fs::write_file(fs::join(dir, ".unity-guids"),
                         "aaaa0000aaaa0000aaaa0000aaaa0001\tAssets/tri.obj\tAssets/Imported/R370/tri.obj\n"
                         "aaaa0000aaaa0000aaaa0000aaaa0002\tAssets/Spin.cs\tAssets/Imported/R370/Spin.cs\n"
                         "aaaa0000aaaa0000aaaa0000aaaa0003\tAssets/Lamp.prefab\tAssets/Imported/R370/Lamp.prefab\n"));
    size_t before = 0;
    ed.scene().for_each([&](GameObject &) { before++; });
    ed.command("import " + fs::join(dir, "Level.unity"));
    GameObject *root = ed.scene().find_by_name("Level"), *floor = ed.scene().find_by_name("Floor"), *tri = ed.scene().find_by_name("Tri");
    GameObject *lamp = ed.scene().find_by_name("Porch Lamp");
    CHECK(root && floor && tri && lamp && floor->parent == root && tri->parent == floor && lamp->parent == floor);
    if (!root || !floor || !tri || !lamp) return;
    const Mesh *plane = floor->get<MeshFilter>() ? floor->get<MeshFilter>()->mesh.get() : nullptr;
    const ColliderComponent *box = floor->get<ColliderComponent>();
    std::printf("    Floor: %zu faces, scale (%g %g %g), collider shape %d size (%g %g %g)\n", plane ? plane->face_count() : 0, floor->local().scale.x,
                floor->local().scale.y, floor->local().scale.z, box ? box->shape : -1, box ? box->size.x : 0.0f, box ? box->size.y : 0.0f, box ? box->size.z : 0.0f);
    CHECK(plane && plane->face_count() == primitives::plane()->face_count() && floor->get<MeshRenderer>() && floor->local().scale.x == 2.0f);
    CHECK(box && box->shape == 0 && std::fabs(box->size.x - 10.0f) < 1e-5f && std::fabs(box->center.y + 0.05f) < 1e-5f && !box->fit);
    const Mesh *tm = tri->get<MeshFilter>() ? tri->get<MeshFilter>()->mesh.get() : nullptr;
    const Vec3 e = tri->local().rotation.to_euler();
    std::printf("    Tri: %zu face(s), at (%g %g %g), turned (%g %g %g)\n", tm ? tm->face_count() : 0, tri->local().position.x, tri->local().position.y,
                tri->local().position.z, e.x, e.y, e.z);
    CHECK(tm && tm->face_count() == 1 && tri->local().position.z == -2.0f && std::fabs(std::fabs(e.y) - 90.0f) < 0.01f);
    const EngineComponent *spin = tri->get<EngineComponent>();
    CHECK(spin && spin->type == "Spin" && spin->script == "aaaa0000aaaa0000aaaa0000aaaa0002" && spin->data.find("speed: 2") != std::string::npos &&
          spin->data.find("--- !u!114 &25") == 0);
    const Camera *cam = tri->get<Camera>();
    CHECK(cam && cam->fov == 45.0f && cam->near_clip == 0.1f && cam->far_clip == 500.0f);
    const Light *light = lamp->get<Light>();
    std::printf("    Porch Lamp: at (%g %g %g), light type %d, range %g, intensity %g (the override isn't held: Unity keeps it)\n", lamp->local().position.x,
                lamp->local().position.y, lamp->local().position.z, light ? light->type : -1, light ? light->range : 0.0f, light ? light->intensity : 0.0f);
    CHECK(light && light->type == 1 && light->range == 7.5f && lamp->local().position.x == 3.0f && lamp->local().position.y == 2.0f);
    /* One Undo step. */
    ed.command("undo");
    size_t after = 0;
    ed.scene().for_each([&](GameObject &) { after++; });
    CHECK(after == before && !ed.scene().find_by_name("Level"));
    std::filesystem::remove_all(dir, ec);
  });
  test("Unity scenes (round 370, review): a prefab holding itself stops; a model placed as a prefab with a script added; names escaped as u+hex; a 2019 .meta's table and a part by its object's name; a parent loop under the root; a rotation override; the console's path match; the kept data byte for byte", [] {
    const YamlNode names = parse_yaml("m_Name: \"\\u9AEA\\u306E\\U0001F600 \\uD83D\\uDE00\"\n");
    CHECK(names.str("m_Name") == "\xE9\xAB\xAA\xE3\x81\xAE\xF0\x9F\x98\x80 \xF0\x9F\x98\x80");
    Editor ed;
    ed.init_headless(1200, 900);
    const std::string dir = resolve_asset_path("Assets/Imported/R370b"), dot_unity = fs::join(dir, "com.unity.r370");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    fs::make_dirs(fs::join(dir, ".unity-meta"));
    fs::make_dirs(dot_unity);
    CHECK(fs::write_file(fs::join(dir, "two.obj"), "o A\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\no B\nv 0 0 1\nv 1 0 1\nv 1 1 1\nv 0 1 1\nf 4 5 6\nf 4 6 7\n"));
    CHECK(fs::write_file(fs::join(dot_unity, "tri.obj"), "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"));
    CHECK(fs::write_file(fs::join(dir, ".unity-meta/bbbb0000bbbb0000bbbb0000bbbb0001.meta"),
                         "fileFormatVersion: 2\nguid: bbbb0000bbbb0000bbbb0000bbbb0001\nModelImporter:\n  internalIDToNameTable:\n  - first:\n      43: -2432090755550338912\n    second: B\n"));
    const std::string self = "%YAML 1.1\n--- !u!1 &1\nGameObject:\n  m_Name: Selfie\n--- !u!4 &2\nTransform:\n  m_GameObject: {fileID: 1}\n  m_Father: {fileID: 0}\n"
                             "--- !u!1001 &3\nPrefabInstance:\n  m_Modification:\n    m_TransformParent: {fileID: 2}\n    m_Modifications: []\n"
                             "  m_SourcePrefab: {fileID: 100100000, guid: bbbb0000bbbb0000bbbb0000bbbb0002, type: 3}\n";
    CHECK(fs::write_file(fs::join(dir, "Selfie.prefab"), self));
    /* The scene, CRLF as Unity writes on Windows. */
    std::string scene = std::string("%YAML 1.1\n") +
        "--- !u!1 &10\nGameObject:\n  m_Name: PartB\n--- !u!4 &11\nTransform:\n  m_GameObject: {fileID: 10}\n  m_Father: {fileID: 0}\n"
        "--- !u!33 &12\nMeshFilter:\n  m_GameObject: {fileID: 10}\n  m_Mesh: {fileID: -2432090755550338912, guid: bbbb0000bbbb0000bbbb0000bbbb0001, type: 3}\n"
        "--- !u!23 &13\nMeshRenderer:\n  m_GameObject: {fileID: 10}\n"
        "--- !u!1 &20\nGameObject:\n  m_Name: A\n--- !u!4 &21\nTransform:\n  m_GameObject: {fileID: 20}\n  m_Father: {fileID: 0}\n"
        "--- !u!33 &22\nMeshFilter:\n  m_GameObject: {fileID: 20}\n  m_Mesh: {fileID: 777, guid: bbbb0000bbbb0000bbbb0000bbbb0001, type: 3}\n"
        "--- !u!1 &30\nGameObject:\n  m_Name: LoopOne\n--- !u!4 &31\nTransform:\n  m_GameObject: {fileID: 30}\n  m_Father: {fileID: 41}\n"
        "--- !u!1 &40\nGameObject:\n  m_Name: LoopTwo\n--- !u!4 &41\nTransform:\n  m_GameObject: {fileID: 40}\n  m_Father: {fileID: 31}\n"
        "--- !u!1001 &50\nPrefabInstance:\n  m_Modification:\n    m_TransformParent: {fileID: 0}\n    m_Modifications: []\n"
        "  m_SourcePrefab: {fileID: 100100000, guid: bbbb0000bbbb0000bbbb0000bbbb0003, type: 3}\n"
        "--- !u!1 &51 stripped\nGameObject:\n  m_CorrespondingSourceObject: {fileID: 919132149155446097, guid: bbbb0000bbbb0000bbbb0000bbbb0003, type: 3}\n"
        "  m_PrefabInstance: {fileID: 50}\n"
        "--- !u!114 &52\nMonoBehaviour:\n  m_GameObject: {fileID: 51}\n  m_Script: {fileID: 542108242, guid: bbbb0000bbbb0000bbbb0000bbbb0004, type: 3}\n  Name: \"\\u30A2\"\n"
        "--- !u!1001 &60\nPrefabInstance:\n  m_Modification:\n    m_TransformParent: {fileID: 0}\n    m_Modifications:\n"
        "    - target: {fileID: 2, guid: bbbb0000bbbb0000bbbb0000bbbb0002, type: 3}\n      propertyPath: m_LocalRotation.x\n      value: 0.70710677\n"
        "    - target: {fileID: 2, guid: bbbb0000bbbb0000bbbb0000bbbb0002, type: 3}\n      propertyPath: m_LocalRotation.w\n      value: 0.70710677\n"
        "    - target: {fileID: 2, guid: bbbb0000bbbb0000bbbb0000bbbb0002, type: 3}\n      propertyPath: m_LocalEulerAnglesHint.x\n      value: 90\n"
        "  m_SourcePrefab: {fileID: 100100000, guid: bbbb0000bbbb0000bbbb0000bbbb0002, type: 3}\n";
    std::string crlf;
    for (char ch : scene) crlf += ch == '\n' ? std::string("\r\n") : std::string(1, ch);
    CHECK(fs::write_file(fs::join(dir, "Mixed.unity"), crlf));
    CHECK(fs::write_file(fs::join(dir, ".unity-guids"),
                         "bbbb0000bbbb0000bbbb0000bbbb0001\tAssets/two.obj\tAssets/Imported/R370b/two.obj\n"
                         "bbbb0000bbbb0000bbbb0000bbbb0002\tAssets/Selfie.prefab\tAssets/Imported/R370b/Selfie.prefab\n"
                         "bbbb0000bbbb0000bbbb0000bbbb0003\tAssets/tri.obj\tAssets/Imported/R370b/com.unity.r370/tri.obj\n"
                         "bbbb0000bbbb0000bbbb0000bbbb0004\tAssets/VRCSDK3A.dll\tAssets/Imported/R370b/VRCSDK3A.dll\n"));
    size_t before = 0;
    ed.scene().for_each([&](GameObject &) { before++; });
    ed.command("import " + fs::join(dir, "Mixed.unity"));
    GameObject *root = ed.scene().find_by_name("Mixed");
    size_t made = 0, selfies = 0;
    ed.scene().for_each([&](GameObject &g) {
      made++;
      selfies += g.name == "Selfie";
    });
    CHECK(root != nullptr);
    if (!root) return;
    /* Every new object under the root. */
    size_t top_new = 0;
    for (GameObject *g : ed.scene().roots) top_new += g != root && g->name != "Main Camera" && g->name != "Directional Light" && g->name != "Cube" && g->name != "Plane";
    GameObject *b = ed.scene().find_by_name("PartB"), *a = ed.scene().find_by_name("A"), *l1 = ed.scene().find_by_name("LoopOne"), *l2 = ed.scene().find_by_name("LoopTwo");
    const size_t fb = b && b->get<MeshFilter>() ? b->get<MeshFilter>()->mesh->face_count() : 0, fa = a && a->get<MeshFilter>() ? a->get<MeshFilter>()->mesh->face_count() : 0;
    std::printf("    %zu objects made, %zu Selfie(s); PartB %zu face(s) (B's 2), A %zu (A's 1); loop parents %s / %s; new top-level besides the root %zu\n",
                made - before, selfies, fb, fa, l1 && l1->parent ? l1->parent->name.c_str() : "-", l2 && l2->parent ? l2->parent->name.c_str() : "-", top_new);
    CHECK(selfies >= 1 && selfies <= 3 && made - before < 40);  // (the self-holding prefab stops at its second appearance)
    CHECK(fb == 2 && fa == 1);
    CHECK(l1 && l2 && (l1->parent == root || l2->parent == root) && l1->parent != nullptr && l2->parent != nullptr);
    /* The model placed as a prefab: its mesh, and the script added to it on its root. */
    GameObject *tri = ed.scene().find_by_name("tri");
    const EngineComponent *sdk = tri ? tri->get<EngineComponent>() : nullptr;
    std::printf("    the placed model: %s, its added component %s\n", tri ? "made" : "missing", sdk ? sdk->type.c_str() : "-");
    CHECK(tri && sdk && sdk->type == "MonoBehaviour (bbbb0000bbbb0000bbbb0000bbbb0004:542108242)" && sdk->data.find("--- !u!114 &52\r\n") == 0 &&
          sdk->data.find("Name: \"\\u30A2\"\r\n") != std::string::npos);
    /* The rotation override, made whole: 90 degrees about X. */
    GameObject *turned = nullptr;
    ed.scene().for_each([&](GameObject &g) { turned = g.name == "Selfie" && std::fabs(g.local().rotation.w - 0.70710677f) < 1e-4f ? &g : turned; });
    CHECK(turned && std::fabs(turned->local().rotation.to_euler().x - 90.0f) < 0.01f);
    /* The console: ".unity" in a folder's name doesn't make an .obj a Unity scene. */
    const size_t objs_before = made;
    ed.command("import " + fs::join(dot_unity, "tri.obj"));
    size_t now = 0;
    ed.scene().for_each([&](GameObject &) { now++; });
    CHECK(now > objs_before);
    std::filesystem::remove_all(dir, ec);
  });
  test("the lesson's Try-it (round 370): import of a .prefab that isn't there says so, making nothing", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    size_t before = 0, after = 0;
    ed.scene().for_each([&](GameObject &) { before++; });
    ed.command("window Project");
    ed.command("import Assets/none.prefab");
    ed.scene().for_each([&](GameObject &) { after++; });
    CHECK(after == before);
  });
}

/* ---------------------------------------------------------------- round 337: Physical Size 0.15 turned a cube black */
static void tests_round337() {
  test("Physical Size 0.15 on a cube's texture (the user's report): the cube stays textured in the CPU's Shaded view, its tiling 1 / 0.15 on every axis - z too, which Assign had left at 1 (the GPU half: test_gpu's round 337)", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    std::remove(resolve_asset_path("Assets/Textures/Bricks.ptex").c_str());
    for (int k = 2; k < 10; k++) std::remove(resolve_asset_path(strprintf("Assets/Textures/Bricks %d.ptex", k)).c_str());
    ProceduralTexturePtr t = ed.new_texture_asset_for_test("Bricks");
    CHECK(t != nullptr);
    if (!t) return;
    ed.command("select Cube");  // (the scene's own: a new one would sit inside it)
    GameObject *cube = ed.active_object_for_test();
    CHECK(cube && cube->get<MeshRenderer>());
    if (!cube || !cube->get<MeshRenderer>()) return;
    ed.assign_texture_asset_for_test(t->asset_path);
    ed.command("shading shaded");
    ed.command("tool view");
    ed.command("focus");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    int cx = 0, cy = 0;
    const bool seen = ed.project_to_window(cube->world_position(), cx, cy);
    CHECK(seen);
    if (!seen) return;
    const float size0 = t->physical_size;
    auto mean = [&] {
      const Image &fb = ed.main_fb_for_test();
      double sum = 0;
      int n = 0;
      for (int y = cy - 12; y <= cy + 12; y++)
        for (int x = cx - 12; x <= cx + 12; x++) {
          const uint32_t p = fb.pixels[(size_t)y * fb.width + x];
          sum += ((p >> 16) & 255) + ((p >> 8) & 255) + (p & 255);
          n++;
        }
      return sum / (3.0 * n);
    };
    const double before = mean();
    ed.command("settex " + t->asset_path + " PhysicalSize 0.15");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    const double after = mean();
    const Vec3 til = cube->get<MeshRenderer>()->materials[0]->tiling;
    std::printf("    the cube's middle: %.1f at %g m a tile, %.1f at 0.15 m (tiling %.3f %.3f %.3f, mapping %d, wrap %d)\n", before, size0, after,
                til.x, til.y, til.z, cube->get<MeshRenderer>()->materials[0]->mapping, cube->get<MeshRenderer>()->materials[0]->wrap);
    CHECK(after > before * 0.5);
    CHECK(std::fabs(til.x - 1.0f / 0.15f) < 1e-3f && std::fabs(til.y - 1.0f / 0.15f) < 1e-3f && std::fabs(til.z - 1.0f / 0.15f) < 1e-3f);
    std::remove(resolve_asset_path(t->asset_path).c_str());
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
  tests_round337();
  tests_round351();
  tests_round352();
  tests_round353();
  tests_round354();
  tests_round355();
  tests_round356();
  tests_round357();
  tests_round358();
  tests_round359();
  tests_round360();
  tests_round361();
  tests_round362();
  tests_round363();
  tests_round364();
  tests_round365();
  tests_round366();
  tests_round367();
  tests_round369();
  tests_round370();
}

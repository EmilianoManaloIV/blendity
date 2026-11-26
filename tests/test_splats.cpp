// SPDX-License-Identifier: GPL-2.0-or-later
// Point clouds and Gaussian splats (Pass 27): round 530's object and data (task 0510, ADR 0147).
#include "test_common.h"

#include "../src/scene/import.h"
#include "../src/scene/import_splat.h"
#include "../src/scene/pointcloud.h"
#include "../src/editor/learn.h"
#include "../src/scene/pointcloud_object.h"
#include "../src/scene/unity_package.h"

#include <cmath>
#include <cstring>
#include <limits>

#ifdef BL_WITH_ZSTD
#  include <zstd.h>
#endif

using namespace bl;

namespace {

GameObject *r530_find_cloud(Editor &ed) {
  GameObject *found = nullptr;
  ed.scene().for_each([&](GameObject &g) {
    if (!found && g.get<PointCloudObject>()) found = &g;
  });
  return found;
}

int r530_count_clouds(Editor &ed) {
  int n = 0;
  ed.scene().for_each([&](GameObject &g) { n += g.get<PointCloudObject>() != nullptr; });
  return n;
}

/* The log's warnings from `line` alone. */
size_t r530_warnings(Editor &ed, const std::string &line) {
  std::vector<LogEntry> tmp;
  const size_t mark = Log::fetch((size_t)1 << 40, tmp);
  ed.command(line);
  std::vector<LogEntry> v;
  Log::fetch(mark, v);
  size_t w = 0;
  for (const LogEntry &e : v) w += e.level != LogLevel::Info;
  return w;
}

bool r530_same(const PointCloud &a, const PointCloud &b) {
  auto eq3 = [](const std::vector<Vec3> &x, const std::vector<Vec3> &y) {
    if (x.size() != y.size()) return false;
    for (size_t i = 0; i < x.size(); i++)
      if (x[i].x != y[i].x || x[i].y != y[i].y || x[i].z != y[i].z) return false;
    return true;
  };
  if (a.type != b.type || a.sh_degree != b.sh_degree || a.count() != b.count() || a.opacity != b.opacity) return false;
  if (!eq3(a.position, b.position) || !eq3(a.color, b.color) || !eq3(a.scale, b.scale) || !eq3(a.sh_rest, b.sh_rest)) return false;
  if (a.rotation.size() != b.rotation.size()) return false;
  for (size_t i = 0; i < a.rotation.size(); i++)
    if (a.rotation[i].x != b.rotation[i].x || a.rotation[i].y != b.rotation[i].y || a.rotation[i].z != b.rotation[i].z || a.rotation[i].w != b.rotation[i].w)
      return false;
  return true;
}

}  // namespace

static void tests_round530() {
  test("splats: a random splat cloud has every array sized to its count and degree", [] {
    for (int d = 0; d <= kSplatMaxShDegree; d++) {
      PointCloudPtr pc = pointcloud_random(PointCloudType::GaussianSplats, 100, d, 7);
      CHECK(pc && pc->count() == 100 && pc->consistent());
      CHECK(pc->sh_rest.size() == 100u * (size_t)splat_sh_rest_count(d));
    }
    PointCloudPtr pts = pointcloud_random(PointCloudType::Points, 50, 3, 7);
    CHECK(pts && pts->consistent() && pts->sh_degree == 0 && pts->scale.empty() && pts->sh_rest.empty());
    CHECK(splat_sh_rest_count(1) == 3 && splat_sh_rest_count(2) == 8 && splat_sh_rest_count(3) == 15);
    CHECK(splat_sh_degree_for_rest(15) == 3 && splat_sh_degree_for_rest(14) == 2 && splat_sh_degree_for_rest(24) == 3);
  });

  test("splats: the same seed gives the same cloud, another seed another", [] {
    PointCloudPtr a = pointcloud_random(PointCloudType::GaussianSplats, 64, 2, 11), b = pointcloud_random(PointCloudType::GaussianSplats, 64, 2, 11),
                  c = pointcloud_random(PointCloudType::GaussianSplats, 64, 2, 12);
    CHECK(r530_same(*a, *b));
    CHECK(!r530_same(*a, *c));
  });

  test("splats: a degree-0 splat looks the same from every side; a degree-1 one changes with the view", [] {
    PointCloud pc;
    pc.type = PointCloudType::GaussianSplats;
    pc.resize(1);
    pc.color[0] = Vec3(0.5f, 0.25f, 0.75f);
    const Vec3 a = splat_color(pc, 0, Vec3(1, 0, 0)), b = splat_color(pc, 0, Vec3(0, 0, -3));
    CHECK(a.x == b.x && a.y == b.y && a.z == b.z && a.x == 0.5f);
    pc.set_sh_degree(1);
    pc.sh_rest[0] = Vec3(0.3f);  // the y band (m = -1): -C1 * y
    const Vec3 up = splat_color(pc, 0, Vec3(0, 1, 0)), down = splat_color(pc, 0, Vec3(0, -1, 0));
    CHECK_NEAR(up.x, 0.5f - 0.4886025f * 0.3f, 1e-5);
    CHECK_NEAR(down.x, 0.5f + 0.4886025f * 0.3f, 1e-5);
    /* A direction of zero length or NaN gives the base colour, never NaN. */
    const Vec3 z = splat_color(pc, 0, Vec3(0.0f)), n = splat_color(pc, 0, Vec3(std::nanf(""), 0, 0));
    CHECK(z.x == 0.5f && n.x == 0.5f);
  });

  test("splats: the colour is clamped at 0, never negative", [] {
    PointCloud pc;
    pc.type = PointCloudType::GaussianSplats;
    pc.sh_degree = 1;
    pc.resize(1);
    pc.color[0] = Vec3(0.1f);
    pc.sh_rest[0] = Vec3(5.0f);
    const Vec3 c = splat_color(pc, 0, Vec3(0, 1, 0));
    CHECK(c.x == 0.0f && c.y == 0.0f && c.z == 0.0f);
  });

  test("splats: the SH basis matches the 3DGS reference's values for a known direction", [] {
    float b[16];
    const float s = 1.0f / std::sqrt(3.0f);
    splat_sh_basis(Vec3(s, s, s), b);
    CHECK_NEAR(b[0], 0.2820948, 1e-6);
    CHECK_NEAR(b[1], -0.4886025 * s, 1e-6);
    CHECK_NEAR(b[4], 1.0925484 * s * s, 1e-6);
    CHECK_NEAR(b[6], 0.0, 1e-6);  // 2zz - xx - yy = 0
    CHECK_NEAR(b[12], 0.3731763 * s * (2 - 3 - 3) * s * s, 1e-6);
  });

  test("splats: a splat's covariance is its scale squared along its turned axes", [] {
    PointCloud pc;
    pc.type = PointCloudType::GaussianSplats;
    pc.resize(1);
    pc.scale[0] = Vec3(2.0f, 1.0f, 0.5f);
    float c[6];
    splat_covariance(pc, 0, c);
    CHECK_NEAR(c[0], 4.0, 1e-6);
    CHECK_NEAR(c[3], 1.0, 1e-6);
    CHECK_NEAR(c[5], 0.25, 1e-6);
    CHECK_NEAR(c[1], 0.0, 1e-6);
    pc.rotation[0] = Quat::axis_angle(Vec3(0, 0, 1), kPi * 0.5f);  // x to y
    splat_covariance(pc, 0, c);
    CHECK_NEAR(c[0], 1.0, 1e-5);
    CHECK_NEAR(c[3], 4.0, 1e-5);
  });

  test("splats: lowering the SH degree keeps the lower bands, raising it adds zero ones", [] {
    PointCloudPtr pc = pointcloud_random(PointCloudType::GaussianSplats, 10, 3, 3);
    const Vec3 first = pc->sh_rest[15 * 4 + 2];  // point 4, coefficient 2 (band 1)
    pc->set_sh_degree(1);
    CHECK(pc->consistent() && pc->sh_rest.size() == 30);
    CHECK(pc->sh_rest[3 * 4 + 2].x == first.x);
    pc->set_sh_degree(2);
    CHECK(pc->consistent() && pc->sh_rest[8 * 4 + 2].x == first.x && pc->sh_rest[8 * 4 + 5].x == 0.0f);
    pc->set_sh_degree(99);
    CHECK(pc->sh_degree == kSplatMaxShDegree && pc->consistent());
  });

  test("splats: Points to Gaussian Splats and back keeps the positions and colours", [] {
    PointCloudPtr pc = pointcloud_random(PointCloudType::Points, 20, 0, 5);
    const Vec3 p = pc->position[7], c = pc->color[7];
    pc->set_type(PointCloudType::GaussianSplats, 0.05f);
    CHECK(pc->consistent() && pc->scale[7].x == 0.05f && pc->opacity[7] == 1.0f);
    pc->set_sh_degree(2);
    pc->set_type(PointCloudType::Points);
    CHECK(pc->consistent() && pc->sh_degree == 0 && pc->scale.empty());
    CHECK(pc->position[7].x == p.x && pc->color[7].y == c.y);
  });

  test("splats: validate drops NaN centres, repairs NaN values, normalises rotations, refuses short arrays", [] {
    PointCloud pc;
    pc.type = PointCloudType::GaussianSplats;
    pc.sh_degree = 1;
    pc.resize(4);
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    pc.position[1] = Vec3(nan, 0, 0);
    pc.position[2] = Vec3(0, inf, 0);
    pc.color[3] = Vec3(nan, 1, 1);
    pc.opacity[3] = nan;
    pc.scale[3] = Vec3(-2, nan, 1);
    pc.rotation[3] = Quat(0, 0, 0, 0);
    pc.rotation[0] = Quat(0, 0, 0, 2);
    pc.sh_rest[3 * 3 + 1] = Vec3(nan, 0, 0);
    CHECK(pointcloud_validate(pc) == 2);
    CHECK(pc.count() == 2 && pc.consistent());
    CHECK(pc.rotation[0].w == 1.0f);
    CHECK(pc.color[1].x == 0.0f && pc.opacity[1] == 0.0f && pc.scale[1].x == 2.0f && pc.scale[1].y == kSplatMinScale);
    CHECK(pc.rotation[1].w == 1.0f && pc.sh_rest[3 + 1].x == 0.0f);
    PointCloud bad;
    bad.type = PointCloudType::GaussianSplats;
    bad.resize(3);
    bad.scale.pop_back();
    std::string why;
    CHECK(pointcloud_validate(bad, &why) < 0 && !why.empty());
  });

  test("splats: a count over the limit is refused, not allocated", [] {
    PointCloud pc;
    CHECK(!pc.resize((size_t)kPointCloudMaxPoints + 1));
    CHECK(pc.count() == 0);
    CHECK(pointcloud_random(PointCloudType::GaussianSplats, (size_t)kPointCloudMaxPoints + 1, 3, 1) == nullptr);
  });

  test("splats: an empty cloud has an empty box; a splat's box takes in three standard deviations", [] {
    PointCloud pc;
    CHECK(!pc.bounds().valid());
    pc.type = PointCloudType::GaussianSplats;
    pc.resize(1);
    pc.scale[0] = Vec3(0.1f, 0.2f, 0.3f);
    const AABB b = pc.bounds();
    CHECK_NEAR(b.max.x, 0.9, 1e-6);
    CHECK_NEAR(b.min.y, -0.9, 1e-6);
  });

  test("splats: Create > Point Cloud > Gaussian Splats makes a selected object with an empty splat cloud and one undo step", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.step_frame_headless();
    const size_t steps = ed.undo_steps_for_test();
    ed.command("create Gaussian Splats");
    ed.step_frame_headless();
    GameObject *g = r530_find_cloud(ed);
    CHECK(g && g->name == "Gaussian Splats");
    CHECK(g && g->get<PointCloudObject>()->data && g->get<PointCloudObject>()->data->is_splats());
    CHECK(g && (ed.active_object_for_test() == g && ed.selection_size_for_test() == 1));
    CHECK(ed.undo_steps_for_test() == steps + 1);
    ed.command("create point cloud");  // (any case, as the other kinds)
    CHECK(r530_count_clouds(ed) == 2);
  });

  test("splats: `pointcloud random splats 1000 3` fills a new cloud; `info` describes it", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.command("pointcloud random splats 1000 3 9");
    GameObject *g = r530_find_cloud(ed);
    CHECK(g != nullptr);
    if (!g) return;
    const PointCloudObject *pc = g->get<PointCloudObject>();
    CHECK(pc->data && pc->data->count() == 1000 && pc->data->sh_degree == 3 && pc->data->consistent());
    CHECK((ed.active_object_for_test() == g && ed.selection_size_for_test() == 1));
    CHECK(r530_warnings(ed, "pointcloud info") == 0);
    /* With it selected, random refills the same object. */
    ed.command("pointcloud random points 10");
    CHECK(r530_count_clouds(ed) == 1 && pc->data->count() == 10 && !pc->data->is_splats());
  });

  test("splats: `pointcloud` refuses bad words and counts with a warning and no undo step", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.command("pointcloud new splats");
    ed.step_frame_headless();
    const size_t steps = ed.undo_steps_for_test();
    for (const char *line : {"pointcloud random", "pointcloud random cubes 10", "pointcloud random splats -5", "pointcloud random splats nan",
                             "pointcloud random splats 1e30", "pointcloud random splats 16777217", "pointcloud size -1", "pointcloud size nan", "pointcloud type cubes", "pointcloud bogus"}) {
      const size_t w = r530_warnings(ed, line);
      if (w == 0) std::printf("    '%s' did not warn\n", line);
      CHECK(w >= 1);
    }
    ed.step_frame_headless();
    CHECK(ed.undo_steps_for_test() == steps);
    ed.clear_selection_for_test();
    CHECK(r530_warnings(ed, "pointcloud info") >= 1);
  });

  test("splats: undo takes a refill back to the cloud before it, sharing its arrays (no copy)", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.command("pointcloud random splats 200 2 1");
    ed.step_frame_headless();
    GameObject *g = r530_find_cloud(ed);
    CHECK(g != nullptr);
    if (!g) return;
    const PointCloud *before = g->get<PointCloudObject>()->data.get();
    ed.command("pointcloud degree 1");
    ed.step_frame_headless();
    CHECK(g->get<PointCloudObject>()->data->sh_degree == 1);
    ed.command("undo");
    g = r530_find_cloud(ed);
    CHECK(g && g->get<PointCloudObject>()->data->sh_degree == 2 && g->get<PointCloudObject>()->data->count() == 200);
    CHECK(g && g->get<PointCloudObject>()->data.get() == before);
  });

  test("splats: duplicate shares the cloud until one edits it; delete removes the object", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.command("pointcloud random splats 50 1 2");
    ed.step_frame_headless();
    GameObject *g = r530_find_cloud(ed);
    CHECK(g != nullptr);
    if (!g) return;
    ed.command("duplicate");
    CHECK(r530_count_clouds(ed) == 2);
    std::vector<GameObject *> all;
    ed.scene().for_each([&](GameObject &o) { if (o.get<PointCloudObject>()) all.push_back(&o); });
    CHECK(all.size() == 2 && all[0]->get<PointCloudObject>()->data == all[1]->get<PointCloudObject>()->data);
    /* The copy (selected now) changes its degree: the original keeps its own. */
    ed.command("pointcloud degree 0");
    CHECK(all[0]->get<PointCloudObject>()->data != all[1]->get<PointCloudObject>()->data);
    CHECK(all[0]->get<PointCloudObject>()->data->sh_degree + all[1]->get<PointCloudObject>()->data->sh_degree == 1);
    ed.command("delete");
    CHECK(r530_count_clouds(ed) == 1);
  });

  test("splats: a scene saves and loads a splat cloud exactly, two objects sharing one cloud stay shared", [] {
    Scene s;
    GameObject *a = create_point_cloud_object(s, PointCloudType::GaussianSplats);
    a->get<PointCloudObject>()->data = pointcloud_random(PointCloudType::GaussianSplats, 300, 3, 21);
    GameObject *b = create_point_cloud_object(s, PointCloudType::GaussianSplats);
    b->get<PointCloudObject>()->data = a->get<PointCloudObject>()->data;
    GameObject *p = create_point_cloud_object(s, PointCloudType::Points);
    p->get<PointCloudObject>()->data = pointcloud_random(PointCloudType::Points, 40, 0, 4);
    p->get<PointCloudObject>()->point_size = 0.125f;
    p->set_local_position(Vec3(1, 2, 3));
    const std::string text = save_scene_text(s);
    Scene back;
    std::string err;
    CHECK(load_scene_text(text, back, err));
    if (!err.empty()) std::printf("    %s\n", err.c_str());
    std::vector<GameObject *> got;
    back.for_each([&](GameObject &o) { if (o.get<PointCloudObject>()) got.push_back(&o); });
    CHECK(got.size() == 3);
    if (got.size() != 3) return;
    CHECK(r530_same(*got[0]->get<PointCloudObject>()->data, *a->get<PointCloudObject>()->data));
    CHECK(got[0]->get<PointCloudObject>()->data == got[1]->get<PointCloudObject>()->data);
    CHECK(r530_same(*got[2]->get<PointCloudObject>()->data, *p->get<PointCloudObject>()->data));
    CHECK(got[2]->get<PointCloudObject>()->point_size == 0.125f && got[2]->local().position.y == 2.0f);
    /* Saving what was loaded gives the same bytes. */
    CHECK(save_scene_text(back) == text);
  });

  test("splats: a scene file whose cloud chunk is cut short, has a bad count or a bad type is refused, not crashed on", [] {
    Scene s;
    GameObject *a = create_point_cloud_object(s, PointCloudType::GaussianSplats);
    a->get<PointCloudObject>()->data = pointcloud_random(PointCloudType::GaussianSplats, 30, 2, 8);
    const std::string text = save_scene_text(s);
    const size_t at = text.find("pointcloud 0 ");
    CHECK(at != std::string::npos);
    if (at == std::string::npos) return;
    const size_t eol = text.find('\n', at);
    const std::string head = text.substr(at, eol - at);
    auto refused = [](const std::string &t) {
      Scene back;
      std::string err;
      const bool ok = load_scene_text(t, back, err);
      return !ok && !err.empty();
    };
    /* Cut inside the chunk. */
    CHECK(refused(text.substr(0, eol + 40)));
    /* A count the chunk doesn't hold. */
    std::string t1 = text;
    t1.replace(at, head.size(), std::string(head).replace(head.find(" 30 bin"), 7, " 31 bin"));
    CHECK(refused(t1));
    /* A huge count. */
    std::string t2 = text;
    t2.replace(at, head.size(), std::string(head).replace(head.find(" 30 bin"), 7, " 999999999999 bin"));
    CHECK(refused(t2));
    /* An unknown type. */
    std::string t3 = text;
    t3.replace(at, head.size(), std::string(head).replace(head.find(" 1 2 30"), 7, " 7 2 30"));
    CHECK(refused(t3));
    /* A degree out of range. */
    std::string t4 = text;
    t4.replace(at, head.size(), std::string(head).replace(head.find(" 1 2 30"), 7, " 1 9 30"));
    CHECK(refused(t4));
  });

  test("splats (review): every cloud and every copy has its own uid, so a cache keyed on it never meets a reused address", [] {
    PointCloud a, b;
    CHECK(a.uid.v != b.uid.v && a.version == b.version);
    PointCloud c = a;
    CHECK(c.uid.v != a.uid.v);
    PointCloudPtr shared = std::make_shared<PointCloud>(a), keep = shared;
    const uint64_t before = shared->uid.v;
    pointcloud_make_mutable(shared);
    CHECK(shared->uid.v != before && keep->uid.v == before);
  });

  test("splats (review): validate keeps scales between 1e-8 and 1e15, so the covariance can be inverted", [] {
    PointCloud pc;
    pc.type = PointCloudType::GaussianSplats;
    pc.resize(1);
    pc.scale[0] = Vec3(0.0f, 1e30f, -3.0f);
    CHECK(pointcloud_validate(pc) == 0);
    CHECK(pc.scale[0].x == kSplatMinScale && pc.scale[0].y == kSplatMaxScale && pc.scale[0].z == 3.0f);
    float cov[6];
    splat_covariance(pc, 0, cov);
    for (float v : cov) CHECK(std::isfinite(v));
  });

  test("splats (review): a Points cloud's box takes in half its dot size", [] {
    PointCloud pc;
    pc.resize(1);
    const AABB b = pc.bounds(0.5f);
    CHECK_NEAR(b.max.x, 0.25, 1e-6);
    CHECK_NEAR(b.min.z, -0.25, 1e-6);
  });

  test("splats: the Inspector's Type switches the cloud to Points; Count is read-only; the render hash follows edits", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.command("pointcloud random splats 10 1 1");
    GameObject *g = r530_find_cloud(ed);
    CHECK(g != nullptr);
    if (!g) return;
    PointCloudObject &c = *g->get<PointCloudObject>();
    PointCloudPtr shared = c.data;  // (as an undo snapshot holds it)
    const uint64_t h0 = hash_component(c);
    ed.command("set PointCloud.Type 0");
    CHECK(!c.data->is_splats() && shared->is_splats());  // (the snapshot's own stays as it was)
    CHECK(hash_component(c) != h0);
    ed.command("set PointCloud.Count 99");
    CHECK(c.data->count() == 10);
  });

  test("splats: the Point Cloud component is registered and in the Hierarchy with its own icon", [] {
    CHECK(find_component_info("PointCloud") != nullptr);
    std::unique_ptr<Component> c = create_component("PointCloud");
    CHECK(c && c->render_role() == Component::RenderRole::PointCloud);
  });

  test("splats (hub): Create > Point Cloud > Test Splats makes 10,000 splats to look at, one undo step, and Undo takes it away", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.step_frame_headless();
    const size_t steps = ed.undo_steps_for_test();
    ed.command("create Test Splats");
    ed.step_frame_headless();
    GameObject *g = r530_find_cloud(ed);
    CHECK(g && g->get<PointCloudObject>()->data && g->get<PointCloudObject>()->data->count() == 10000);
    CHECK(g && g->get<PointCloudObject>()->data->is_splats() && g->get<PointCloudObject>()->data->sh_degree == 3);
    CHECK(ed.undo_steps_for_test() == steps + 1);
    ed.command("undo");
    ed.step_frame_headless();
    CHECK(r530_count_clouds(ed) == 0);
  });

  test("splats (hub): the Learn tab has a Point clouds and Gaussian splats section with a Try-it and questions", [] {
    bool section = false, tryit = false;
    int questions = 0;
    for (int i = 0; i < lesson_count(); i++) {
      const std::string body = lesson(i).body;
      const size_t at = body.find("# Point clouds and Gaussian splats");
      if (at == std::string::npos) continue;
      section = true;
      const size_t end = body.find("\n# ", at + 1);
      const std::string part = body.substr(at, end == std::string::npos ? std::string::npos : end - at);
      tryit = part.find("! cmd:create Test Splats") != std::string::npos;
      for (size_t q = part.find("\n? "); q != std::string::npos; q = part.find("\n? ", q + 1)) questions++;
    }
    CHECK(section && tryit && questions >= 2);
  });
}

/* ------------------------------------------------------------- round 531: import (task 0511) */

namespace {

constexpr float kR531FromZUp[3][3] = {{-1, 0, 0}, {0, 0, 1}, {0, -1, 0}};
constexpr float kR531MirrorX[3][3] = {{-1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

Vec3 r531_mul(const float m[3][3], Vec3 v) {
  return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z, m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z, m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
}

/* One splat as a 3DGS PLY stores it (before the activations), from a seed. */
struct R531Splat {
  float pos[3], dc[3], opacity, scale[3], rot[4] /* w x y z */, rest[3][24] /* per channel */;
};
R531Splat r531_splat(int i) {
  R531Splat s{};
  auto f = [&](int k) { return std::sin(1.7f * (float)(i * 97 + k) + 0.3f); };
  for (int k = 0; k < 3; k++) s.pos[k] = 2.0f * f(k), s.dc[k] = f(10 + k), s.scale[k] = -3.0f + f(20 + k);
  s.opacity = 2.0f * f(30);
  for (int k = 0; k < 4; k++) s.rot[k] = f(40 + k) + (k == 0 ? 1.5f : 0.0f);
  for (int c = 0; c < 3; c++)
    for (int k = 0; k < 24; k++) s.rest[c][k] = 0.2f * f(100 + 24 * c + k);
  return s;
}

/* The cloud the file means, in the file's own axes, by Blender's formulas (ply_import_gsplat.cc, IO_gsplat.hh). */
PointCloud r531_expected(int n, int per_channel, int degree) {
  PointCloud pc;
  pc.type = PointCloudType::GaussianSplats;
  pc.sh_degree = degree;
  pc.resize((size_t)n);
  const int keep = splat_sh_rest_count(degree);
  for (int i = 0; i < n; i++) {
    const R531Splat s = r531_splat(i);
    pc.position[(size_t)i] = Vec3(s.pos[0], s.pos[1], s.pos[2]);
    pc.color[(size_t)i] = Vec3(0.5f + kSplatShC0 * s.dc[0], 0.5f + kSplatShC0 * s.dc[1], 0.5f + kSplatShC0 * s.dc[2]);
    pc.opacity[(size_t)i] = 1.0f / (1.0f + std::exp(-s.opacity));
    pc.scale[(size_t)i] = Vec3(std::exp(s.scale[0]), std::exp(s.scale[1]), std::exp(s.scale[2]));
    const float l = std::sqrt(s.rot[0] * s.rot[0] + s.rot[1] * s.rot[1] + s.rot[2] * s.rot[2] + s.rot[3] * s.rot[3]);
    pc.rotation[(size_t)i] = Quat(s.rot[1] / l, s.rot[2] / l, s.rot[3] / l, s.rot[0] / l);
    for (int k = 0; k < keep; k++) pc.sh_rest[(size_t)(i * keep + k)] = Vec3(s.rest[0][k], s.rest[1][k], s.rest[2][k]);
  }
  (void)per_channel;
  return pc;
}

/* A 3DGS PLY: format 0 ascii, 1 little, 2 big endian; `per_channel` f_rest coefficients a channel; `normals` adds
 * nx ny nz as spz_to_ply writes them. */
std::string r531_ply(int format, int n, int per_channel, bool normals = true, const char *count_text = nullptr) {
  std::vector<std::string> props = {"x", "y", "z"};
  if (normals) props.insert(props.end(), {"nx", "ny", "nz"});
  props.insert(props.end(), {"f_dc_0", "f_dc_1", "f_dc_2"});
  for (int k = 0; k < 3 * per_channel; k++) props.push_back("f_rest_" + std::to_string(k));
  props.insert(props.end(), {"opacity", "scale_0", "scale_1", "scale_2", "rot_0", "rot_1", "rot_2", "rot_3"});
  std::string s = "ply\nformat ";
  s += format == 0 ? "ascii" : format == 1 ? "binary_little_endian" : "binary_big_endian";
  s += " 1.0\nelement vertex " + std::string(count_text ? count_text : std::to_string(n).c_str()) + "\n";
  for (const std::string &p : props) s += "property float " + p + "\n";
  s += "end_header\n";
  for (int i = 0; i < n; i++) {
    const R531Splat sp = r531_splat(i);
    std::vector<float> v = {sp.pos[0], sp.pos[1], sp.pos[2]};
    if (normals) v.insert(v.end(), {0.0f, 0.0f, 0.0f});
    v.insert(v.end(), {sp.dc[0], sp.dc[1], sp.dc[2]});
    for (int c = 0; c < 3; c++)
      for (int k = 0; k < per_channel; k++) v.push_back(sp.rest[c][k]);
    v.insert(v.end(), {sp.opacity, sp.scale[0], sp.scale[1], sp.scale[2], sp.rot[0], sp.rot[1], sp.rot[2], sp.rot[3]});
    for (float x : v) {
      if (format == 0) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.9g ", x);
        s += buf;
        continue;
      }
      unsigned char b[4];
      std::memcpy(b, &x, 4);
      if (format == 2) std::swap(b[0], b[3]), std::swap(b[1], b[2]);
      s.append((const char *)b, 4);
    }
    if (format == 0) s += "\n";
  }
  return s;
}

/* `got` is `want` carried by the axis change `m`: positions, covariances, opacities, and the colour seen from 40
 * directions (want's colour along d is got's along m d). The largest difference. */
double r531_diff(const PointCloud &got, const PointCloud &want, const float m[3][3]) {
  if (got.count() != want.count() || got.sh_degree != want.sh_degree) return 1e30;
  double worst = 0.0;
  auto at = [&](double a, double b) { worst = std::max(worst, std::fabs(a - b)); };
  for (size_t i = 0; i < got.count(); i++) {
    const Vec3 p = r531_mul(m, want.position[i]);
    at(got.position[i].x, p.x), at(got.position[i].y, p.y), at(got.position[i].z, p.z);
    at(got.opacity[i], want.opacity[i]);
    float cg[6], cw[6];
    splat_covariance(got, i, cg);
    splat_covariance(want, i, cw);
    const float W[3][3] = {{cw[0], cw[1], cw[2]}, {cw[1], cw[3], cw[4]}, {cw[2], cw[4], cw[5]}};
    float MW[3][3] = {}, MWMt[3][3] = {};
    for (int r = 0; r < 3; r++)
      for (int c = 0; c < 3; c++)
        for (int k = 0; k < 3; k++) MW[r][c] += m[r][k] * W[k][c];
    for (int r = 0; r < 3; r++)
      for (int c = 0; c < 3; c++)
        for (int k = 0; k < 3; k++) MWMt[r][c] += MW[r][k] * m[c][k];
    const float G[6] = {MWMt[0][0], MWMt[0][1], MWMt[0][2], MWMt[1][1], MWMt[1][2], MWMt[2][2]};
    const float scale = std::max(1e-6f, std::max(cw[0], std::max(cw[3], cw[5])));
    for (int k = 0; k < 6; k++) at(cg[k] / scale, G[k] / scale);
    for (int k = 0; k < 40; k++) {
      const float z = 1.0f - (2.0f * k + 1.0f) / 40.0f, r = std::sqrt(1.0f - z * z), ph = 2.4f * (float)k;
      const Vec3 d(r * std::cos(ph), r * std::sin(ph), z);
      const Vec3 a = splat_color(want, i, d), b = splat_color(got, i, r531_mul(m, d));
      at(a.x, b.x), at(a.y, b.y), at(a.z, b.z);
    }
  }
  return worst;
}

std::string r531_write(const std::string &name, const std::string &bytes) {
  const std::string dir = fs::join(test_dir(), "r531");
  fs::make_dirs(dir);
  const std::string path = fs::join(dir, name);
  fs::write_file(path, bytes);
  return path;
}

void r531_put32(std::string &s, uint32_t v) {
  for (int k = 0; k < 4; k++) s += (char)((v >> (8 * k)) & 0xFF);
}

/* An SPZ cloud's packed bytes from a seed (v1-v3 layout after the header; v4 streams take the same blocks). */
struct R531Spz {
  uint32_t version = 2, n = 0;
  int degree = 0, fractional = 12, coeffs = 0;
  std::string pos, alpha, color, scale, rot, sh;
};
R531Spz r531_spz(uint32_t version, uint32_t n, int degree) {
  R531Spz z;
  z.version = version, z.n = n, z.degree = degree;
  z.coeffs = degree == 0 ? 0 : degree == 1 ? 3 : degree == 2 ? 8 : degree == 3 ? 15 : 24;
  auto byte = [](uint32_t i, int k) { return (char)((i * 131u + (uint32_t)k * 71u + 17u) & 0xFF); };
  for (uint32_t i = 0; i < n; i++) {
    for (int k = 0; k < (version == 1 ? 6 : 9); k++) z.pos += version == 1 && (k % 2) ? (char)0x3C : byte(i, k);  // (float16: around 1.0)
    z.alpha += byte(i, 20);
    for (int k = 0; k < 3; k++) z.color += byte(i, 30 + k), z.scale += (char)(100 + (uint8_t)byte(i, 40 + k) % 60);
    if (version >= 3) {
      const uint32_t packed = (3u << 30) | ((uint32_t)(i * 37 % 300) << 20) | ((uint32_t)(i * 11 % 400) << 10) | (uint32_t)(i * 5 % 500);
      r531_put32(z.rot, packed);
    }
    else
      for (int k = 0; k < 3; k++) z.rot += (char)(110 + (uint8_t)byte(i, 50 + k) % 40);
    for (int k = 0; k < 3 * z.coeffs; k++) z.sh += byte(i, 60 + k);
  }
  return z;
}
std::string r531_spz_raw(const R531Spz &z, uint32_t claimed_n) {
  std::string s;
  r531_put32(s, 0x5053474e);
  r531_put32(s, z.version);
  r531_put32(s, claimed_n);
  s += (char)z.degree, s += (char)z.fractional, s += (char)0, s += (char)0;
  return s + z.pos + z.alpha + z.color + z.scale + z.rot + z.sh;
}

/* What Blender's SPZ reader (spz_read_common.hh) makes of the bytes, in SPZ's own axes. */
PointCloud r531_spz_expected(const R531Spz &z) {
  PointCloud pc;
  pc.type = PointCloudType::GaussianSplats;
  pc.sh_degree = std::min(z.degree, kSplatMaxShDegree);
  pc.resize(z.n);
  const int keep = splat_sh_rest_count(pc.sh_degree);
  for (uint32_t i = 0; i < z.n; i++) {
    const uint8_t *p = (const uint8_t *)z.pos.data();
    if (z.version == 1) {
      float c[3];
      for (int k = 0; k < 3; k++) {
        const uint16_t h = (uint16_t)(p[i * 6 + 2 * k] | (p[i * 6 + 2 * k + 1] << 8));
        const int e = (h >> 10) & 31, m = h & 1023;
        c[k] = (h & 0x8000 ? -1.0f : 1.0f) * std::ldexp(1.0f + (float)m / 1024.0f, e - 15);  // (normal halves only here)
      }
      pc.position[i] = Vec3(c[0], c[1], c[2]);
    }
    else {
      int32_t c[3];
      for (int k = 0; k < 3; k++) {
        c[k] = p[i * 9 + 3 * k] | (p[i * 9 + 3 * k + 1] << 8) | (p[i * 9 + 3 * k + 2] << 16);
        if (c[k] & 0x800000) c[k] |= (int32_t)0xFF000000;
      }
      const float s = 1.0f / (float)(1 << z.fractional);
      pc.position[i] = Vec3((float)c[0] * s, (float)c[1] * s, (float)c[2] * s);
    }
    pc.opacity[i] = (float)(uint8_t)z.alpha[i] / 255.0f;
    float col[3], sc[3];
    for (int k = 0; k < 3; k++) {
      col[k] = 0.5f + kSplatShC0 * (((float)(uint8_t)z.color[i * 3 + k] / 255.0f - 0.5f) / 0.15f);
      sc[k] = std::exp((float)(uint8_t)z.scale[i * 3 + k] / 16.0f - 10.0f);
    }
    pc.color[i] = Vec3(col[0], col[1], col[2]);
    pc.scale[i] = Vec3(sc[0], sc[1], sc[2]);
    if (z.version >= 3) {
      uint32_t packed = (uint8_t)z.rot[i * 4] | ((uint32_t)(uint8_t)z.rot[i * 4 + 1] << 8) | ((uint32_t)(uint8_t)z.rot[i * 4 + 2] << 16) | ((uint32_t)(uint8_t)z.rot[i * 4 + 3] << 24);
      const int largest = (int)(packed >> 30);
      float q[4];
      float sum = 0.0f;
      for (int k = 3; k >= 0; k--) {
        if (k == largest) continue;
        const float mag = (float)(packed & 511u) / 511.0f, sign = (packed >> 9) & 1u ? -1.0f : 1.0f;
        q[k] = 0.70710678f * mag * sign;
        sum += q[k] * q[k];
        packed >>= 10;
      }
      q[largest] = std::sqrt(std::max(0.0f, 1.0f - sum));
      pc.rotation[i] = Quat(q[0], q[1], q[2], q[3]);
    }
    else {
      const Vec3 v = Vec3((float)(uint8_t)z.rot[i * 3], (float)(uint8_t)z.rot[i * 3 + 1], (float)(uint8_t)z.rot[i * 3 + 2]) / 127.5f - Vec3(1.0f);
      pc.rotation[i] = Quat(v.x, v.y, v.z, std::sqrt(std::max(0.0f, 1.0f - dot(v, v))));
    }
    for (int k = 0; k < keep; k++) {
      const uint8_t *h = (const uint8_t *)z.sh.data() + (size_t)i * 3 * z.coeffs + 3 * k;
      pc.sh_rest[(size_t)i * keep + k] = Vec3(((float)h[0] - 128.0f) / 128.0f, ((float)h[1] - 128.0f) / 128.0f, ((float)h[2] - 128.0f) / 128.0f);
    }
  }
  std::string why;
  pointcloud_validate(pc, &why);
  return pc;
}

template <class T>
bool r619_same_bytes(const std::vector<T> &a, const std::vector<T> &b) {
  return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0);
}

void r619_check_cloud(const PointCloud &disk, const PointCloud &borrowed) {
  static_assert(sizeof(Vec3) == 3 * sizeof(float) && sizeof(Quat) == 4 * sizeof(float));
  CHECK(disk.consistent() && borrowed.consistent());
  CHECK(disk.type == borrowed.type && disk.sh_degree == borrowed.sh_degree && disk.count() == borrowed.count());
  CHECK(r619_same_bytes(disk.position, borrowed.position));
  CHECK(r619_same_bytes(disk.color, borrowed.color));
  CHECK(r619_same_bytes(disk.opacity, borrowed.opacity));
  CHECK(r619_same_bytes(disk.scale, borrowed.scale));
  CHECK(r619_same_bytes(disk.rotation, borrowed.rotation));
  CHECK(r619_same_bytes(disk.sh_rest, borrowed.sh_rest));
}

/* A .glb holding one POINTS primitive with KHR_gaussian_splatting's attributes (degree 1), from r531_splat's values
 * activated (the extension stores linear scale and opacity). */
std::string r531_glb(int n, bool with_extension = true) {
  std::string bin, json;
  std::vector<std::pair<std::string, int>> attrs = {{"POSITION", 3}, {"KHR_gaussian_splatting:SCALE", 3}, {"KHR_gaussian_splatting:ROTATION", 4},
                                                     {"KHR_gaussian_splatting:OPACITY", 1}, {"KHR_gaussian_splatting:SH_DEGREE_0_COEF_0", 3}};
  for (int k = 0; k < 3; k++) attrs.push_back({"KHR_gaussian_splatting:SH_DEGREE_1_COEF_" + std::to_string(k), 3});
  std::string acc, views, attr_json;
  for (size_t a = 0; a < attrs.size(); a++) {
    const size_t at = bin.size();
    for (int i = 0; i < n; i++) {
      const R531Splat s = r531_splat(i);
      const float l = std::sqrt(s.rot[0] * s.rot[0] + s.rot[1] * s.rot[1] + s.rot[2] * s.rot[2] + s.rot[3] * s.rot[3]);
      std::vector<float> v;
      if (a == 0) v = {s.pos[0], s.pos[1], s.pos[2]};
      else if (a == 1) v = {std::exp(s.scale[0]), std::exp(s.scale[1]), std::exp(s.scale[2])};
      else if (a == 2) v = {s.rot[1] / l, s.rot[2] / l, s.rot[3] / l, s.rot[0] / l};
      else if (a == 3) v = {1.0f / (1.0f + std::exp(-s.opacity))};
      else if (a == 4) v = {s.dc[0], s.dc[1], s.dc[2]};
      else v = {s.rest[0][a - 5], s.rest[1][a - 5], s.rest[2][a - 5]};
      bin.append((const char *)v.data(), v.size() * 4);
    }
    const char *type = attrs[a].second == 1 ? "SCALAR" : attrs[a].second == 3 ? "VEC3" : "VEC4";
    views += strprintf("%s{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu}", a ? "," : "", at, bin.size() - at);
    acc += strprintf("%s{\"bufferView\":%zu,\"componentType\":5126,\"count\":%d,\"type\":\"%s\"}", a ? "," : "", a, n, type);
    attr_json += strprintf("%s\"%s\":%zu", a ? "," : "", attrs[a].first.c_str(), a);
  }
  json = strprintf("{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":%zu}],\"bufferViews\":[%s],\"accessors\":[%s],"
                   "\"meshes\":[{\"name\":\"Splats\",\"primitives\":[{\"mode\":0,\"attributes\":{%s}%s}]}],\"nodes\":[{\"name\":\"SplatNode\",\"mesh\":0}],"
                   "\"scenes\":[{\"nodes\":[0]}],\"scene\":0%s}",
                   bin.size(), views.c_str(), acc.c_str(), attr_json.c_str(), with_extension ? ",\"extensions\":{\"KHR_gaussian_splatting\":{}}" : "",
                   with_extension ? ",\"extensionsUsed\":[\"KHR_gaussian_splatting\"]" : "");
  while (json.size() % 4) json += ' ';
  while (bin.size() % 4) bin += '\0';
  std::string glb = "glTF";
  r531_put32(glb, 2);
  r531_put32(glb, (uint32_t)(12 + 8 + json.size() + 8 + bin.size()));
  r531_put32(glb, (uint32_t)json.size());
  r531_put32(glb, 0x4E4F534Au);
  glb += json;
  r531_put32(glb, (uint32_t)bin.size());
  r531_put32(glb, 0x004E4942u);
  glb += bin;
  return glb;
}

}  // namespace

static void tests_round531() {
  test("splat import: SH band maps match Blender's analytic +90 degrees about X (spz_read_common.cc) for bands 1 to 3", [] {
    /* Blender's convert_axis_to_blender: (x, y, z) -> (x, -z, y), and its per-band tables. */
    const float M[3][3] = {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}};
    double t1[9], t2[25], t3[49];
    splat_sh_band_matrix(1, M, t1);
    splat_sh_band_matrix(2, M, t2);
    splat_sh_band_matrix(3, M, t3);
    const double b1[9] = {0, 1, 0, -1, 0, 0, 0, 0, 1};
    const double s3 = std::sqrt(3.0);
    const double b2[25] = {0, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, -0.5, 0, -s3 / 2, -1, 0, 0, 0, 0, 0, 0, -s3 / 2, 0, 0.5};
    const double a = std::sqrt(5.0 / 8.0), b = std::sqrt(3.0 / 8.0), s15 = std::sqrt(15.0);
    const double b3[49] = {0, 0, 0, -a, 0, b, 0,  /**/ 0, -1, 0, 0, 0, 0, 0, /**/ 0, 0, 0, -b, 0, -a, 0, /**/ a, 0, b, 0, 0, 0, 0,
                           0, 0, 0, 0, -0.25, 0, -s15 / 4, /**/ -b, 0, a, 0, 0, 0, 0, /**/ 0, 0, 0, 0, -s15 / 4, 0, 0.25};
    double worst = 0.0;
    for (int k = 0; k < 9; k++) worst = std::max(worst, std::fabs(t1[k] - b1[k]));
    for (int k = 0; k < 25; k++) worst = std::max(worst, std::fabs(t2[k] - b2[k]));
    for (int k = 0; k < 49; k++) worst = std::max(worst, std::fabs(t3[k] - b3[k]));
    std::printf("    largest difference from Blender's tables: %.2e\n", worst);
    CHECK(worst < 1e-5);
  });

  test("splat import: an axis change (a mirror included) keeps every splat's colour from every side and its ellipsoid", [] {
    for (const auto *m : {&kR531FromZUp, &kR531MirrorX}) {
      PointCloudPtr pc = pointcloud_random(PointCloudType::GaussianSplats, 50, 3, 77);
      PointCloud before = *pc;
      pointcloud_change_axes(*pc, *m);
      const double d = r531_diff(*pc, before, *m);
      std::printf("    largest difference %.2e\n", d);
      CHECK(d < 2e-4);
    }
  });

  test("splat import: a 3DGS PLY (binary little / big endian, ASCII, degree 3, with normals) becomes Gaussian Splats as Blender decodes it", [] {
    const PointCloud want = r531_expected(40, 15, 3);
    for (int format = 0; format < 3; format++) {
      const std::string bytes = r531_ply(format, 40, 15);
      CHECK(ply_is_point_cloud(bytes));
      SplatReadReport rep;
      PointCloudPtr pc = read_ply_point_cloud(bytes, rep);
      CHECK(pc != nullptr);
      if (!pc) {
        std::printf("    format %d: %s\n", format, rep.error.c_str());
        continue;
      }
      const double d = r531_diff(*pc, want, kR531FromZUp);
      std::printf("    format %d: %zu splats, degree %d, largest difference %.2e\n", format, pc->count(), pc->sh_degree, d);
      CHECK(pc->is_splats() && pc->count() == 40 && pc->sh_degree == 3 && d < 2e-4 && rep.notes.empty());
    }
  });

  test("splat import: a degree-4 PLY keeps bands 1 to 3 with a note; an incomplete band keeps the complete ones", [] {
    SplatReadReport r4, r2;
    PointCloudPtr pc4 = read_ply_point_cloud(r531_ply(1, 10, 24), r4);
    CHECK(pc4 && pc4->sh_degree == 3 && r4.notes.size() == 1);
    if (pc4) CHECK(r531_diff(*pc4, r531_expected(10, 24, 3), kR531FromZUp) < 2e-4);
    PointCloudPtr pc2 = read_ply_point_cloud(r531_ply(1, 10, 10), r2);  // (10 a channel: degree 2's 8 and two more)
    CHECK(pc2 && pc2->sh_degree == 2 && r2.notes.size() == 1);
  });

  test("splat import: a PLY of plain points becomes a Points cloud (not a refusal), its byte colours read as sRGB", [] {
    std::string s = "ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\n"
                    "property uchar red\nproperty uchar green\nproperty uchar blue\nelement face 0\nproperty list uchar int vertex_indices\nend_header\n"
                    "0 0 0 255 0 0\n1 2 3 0 255 0\n-1 -2 -3 128 128 128\n";
    CHECK(ply_is_point_cloud(s));
    ImportResult out;
    CHECK(import_model(r531_write("points.ply", s), out));
    CHECK(out.nodes.size() == 1 && out.nodes[0].points && !out.nodes[0].mesh);
    if (out.nodes.empty() || !out.nodes[0].points) return;
    const PointCloud &pc = *out.nodes[0].points;
    CHECK(!pc.is_splats() && pc.count() == 3);
    CHECK(pc.position[1].x == -1.0f && pc.position[1].y == 3.0f && pc.position[1].z == -2.0f);  // (Z up, as the PLY meshes)
    CHECK(pc.color[0].x == 1.0f && pc.color[0].y == 0.0f);
    CHECK_NEAR(pc.color[2].x, 0.2158605, 1e-4);  // (128 as sRGB)
    /* A PLY with faces stays a mesh. */
    std::string mesh = "ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\n"
                       "element face 1\nproperty list uchar int vertex_indices\nend_header\n0 0 0\n1 0 0\n0 1 0\n3 0 1 2\n";
    CHECK(!ply_is_point_cloud(mesh));
  });

  test("splat import: bad PLYs refuse cleanly - cut short, a huge or zero count, a bad count word - and NaN centres are left out", [] {
    const std::string good = r531_ply(1, 20, 3);
    for (size_t cut : {good.size() - 1, good.size() - 200, good.find("end_header") + 11}) {
      SplatReadReport r;
      CHECK(!read_ply_point_cloud(good.substr(0, cut), r) && !r.error.empty());
    }
    for (const char *count : {"4000000000", "99999999999999", "0", "-5", "nan"}) {
      SplatReadReport r;
      PointCloudPtr pc = read_ply_point_cloud(r531_ply(1, 3, 3, true, count), r);
      if (pc) std::printf("    count '%s' gave a cloud of %zu\n", count, pc->count());
      CHECK(!pc && !r.error.empty());
    }
    std::string nan = r531_ply(0, 5, 0);
    const size_t row = nan.find("end_header\n") + 11;
    nan.replace(row, nan.find(' ', row) - row, "nan");
    SplatReadReport r;
    PointCloudPtr pc = read_ply_point_cloud(nan, r);
    CHECK(pc && pc->count() == 4 && r.notes.size() == 1);
  });

  test("splat import: SPZ v1, v2 and v3 (gzip) decode as Blender's reader does, carried into Blendity's axes", [] {
    for (uint32_t version : {1u, 2u, 3u}) {
      const R531Spz z = r531_spz(version, 30, 3);
      SplatReadReport rep;
      PointCloudPtr pc = read_spz(gzip(r531_spz_raw(z, z.n)), rep);
      CHECK(pc != nullptr);
      if (!pc) {
        std::printf("    v%u: %s\n", version, rep.error.c_str());
        continue;
      }
      const double d = r531_diff(*pc, r531_spz_expected(z), kR531MirrorX);
      std::printf("    v%u: %zu splats, degree %d, largest difference %.2e\n", version, pc->count(), pc->sh_degree, d);
      CHECK(pc->count() == 30 && pc->sh_degree == 3 && d < 2e-4);
    }
    /* Degree 4: bands 1 to 3 kept, with a note. */
    const R531Spz z4 = r531_spz(3, 8, 4);
    SplatReadReport rep4;
    PointCloudPtr pc4 = read_spz(gzip(r531_spz_raw(z4, z4.n)), rep4);
    CHECK(pc4 && pc4->sh_degree == 3 && !rep4.notes.empty());
    if (pc4) CHECK(r531_diff(*pc4, r531_spz_expected(z4), kR531MirrorX) < 2e-4);
  });

  test("splat import: byte gzip output is exact and survives rejected replacements", [] {
    const std::string data("a\0b\xff", 4);
    std::string packed = gzip(data), why;
    std::vector<uint8_t> bytes{42};
    CHECK(gunzip(packed, bytes, why, data.size()));
    const std::vector<uint8_t> expected(data.begin(), data.end());
    CHECK(bytes == expected);
    CHECK(!gunzip(packed, bytes, why, data.size() - 1));
    CHECK(bytes == expected && why == "too large to unpack");
    packed[packed.size() - 4] = 3;
    CHECK(!gunzip(packed, bytes, why));
    CHECK(bytes == expected && why.find("length") != std::string::npos);
    // A forged ISIZE within the limit still cannot make the inflater produce extra bytes.
    CHECK(!gunzip(packed, bytes, why, 3));
    CHECK(bytes == expected && !why.empty());
    CHECK(gunzip(gzip(""), bytes, why, 0) && bytes.empty());
  });

  test("splat import: the gzip string caller keeps binary data and its previous output on failure", [] {
    const std::string data("a\0b\xff", 4);
    std::string out("previous"), why;
    const std::string packed = gzip(data);
    CHECK(gunzip(packed, out, why, data.size()) && out == data);
    CHECK(!gunzip(packed, out, why, data.size() - 1));
    CHECK(out == data && why == "too large to unpack");
    CHECK(gunzip(gzip(""), out, why, 0) && out.empty());
  });

  test("splat import: bad SPZ files refuse cleanly - wrong magic, version 9, degree 5, cut short, a count the data can't hold, not gzip", [] {
    const R531Spz z = r531_spz(2, 10, 1);
    std::string raw = r531_spz_raw(z, z.n);
    auto refused = [](const std::string &bytes) {
      SplatReadReport r;
      return !read_spz(bytes, r) && !r.error.empty();
    };
    std::string magic = raw;
    magic[0] = 'X';
    CHECK(refused(gzip(magic)));
    std::string ver = raw;
    ver[4] = 9;
    CHECK(refused(gzip(ver)));
    std::string deg = raw;
    deg[12] = 5;
    CHECK(refused(gzip(deg)));
    CHECK(refused(gzip(raw.substr(0, raw.size() - 1))));
    CHECK(refused(gzip(r531_spz_raw(z, 0x7FFFFFFF))));
    CHECK(refused(gzip(r531_spz_raw(z, 0))));
    const std::string gz = gzip(raw);
    CHECK(refused(gz.substr(0, gz.size() / 2)));
    CHECK(refused("hello"));
    CHECK(refused(std::string()));
  });

  test("splat import: SPZ v4 (zstd streams) decodes with Blender's libraries and is refused cleanly without them", [] {
    const R531Spz z = r531_spz(4, 25, 2);
    std::string head;
    r531_put32(head, 0x5053474e);
    r531_put32(head, 4);
    r531_put32(head, z.n);
    head += (char)z.degree, head += (char)z.fractional, head += (char)0, head += (char)6;
    r531_put32(head, 32);
    head += std::string(12, '\0');
    const std::string *blocks[6] = {&z.pos, &z.alpha, &z.color, &z.scale, &z.rot, &z.sh};
    std::string toc, data;
#ifdef BL_WITH_ZSTD
    for (const std::string *b : blocks) {
      std::string c(ZSTD_compressBound(b->size()), '\0');
      const size_t n = ZSTD_compress(&c[0], c.size(), b->data(), b->size(), 3);
      c.resize(n);
      const uint64_t cs = n, us = b->size();
      toc.append((const char *)&cs, 8), toc.append((const char *)&us, 8);
      data += c;
    }
    SplatReadReport rep;
    const std::string encoded = head + toc + data;
    std::string borrowed = encoded;
    PointCloudPtr pc = read_spz(borrowed, rep);
    CHECK(borrowed == encoded);
    CHECK(pc != nullptr);
    if (!pc) std::printf("    %s\n", rep.error.c_str());
    if (pc) CHECK(pc->count() == 25 && pc->sh_degree == 2 && r531_diff(*pc, r531_spz_expected(z), kR531MirrorX) < 2e-4);
    ImportResult disk;
    CHECK(import_spz_file(r531_write("r619-valid-v4.spz", encoded), disk));
    CHECK(disk.nodes.size() == 1 && disk.nodes[0].points);
    if (pc && disk.nodes.size() == 1 && disk.nodes[0].points) r619_check_cloud(*disk.nodes[0].points, *pc);
    /* A stream that says it holds other than the points need: refused. */
    std::string bad = head + toc + data;
    bad[32 + 8] ^= 1;
    const std::string original_bad = bad;
    SplatReadReport rb;
    CHECK(!read_spz(bad, rb) && !rb.error.empty());
    CHECK(bad == original_bad);
    ImportResult bad_disk;
    CHECK(!import_spz_file(r531_write("r619-bad-v4.spz", original_bad), bad_disk));
    CHECK(bad_disk.error == rb.error && bad_disk.nodes.empty());
#else
    for (const std::string *b : blocks) {
      const uint64_t cs = b->size(), us = b->size();
      toc.append((const char *)&cs, 8), toc.append((const char *)&us, 8);
      data += *b;
    }
    SplatReadReport rep;
    const std::string encoded = head + toc + data;
    std::string borrowed = encoded;
    CHECK(!read_spz(borrowed, rep) && rep.error.find("Zstandard") != std::string::npos);
    CHECK(borrowed == encoded);
    ImportResult disk;
    CHECK(!import_spz_file(r531_write("r619-no-zstd.spz", encoded), disk));
    CHECK(disk.error == rep.error && disk.nodes.empty());
#endif
  });

  test("splat import: a glTF KHR_gaussian_splatting primitive becomes a Gaussian Splats object; plain POINTS become Points", [] {
    ImportResult out;
    CHECK(import_model(r531_write("splats.glb", r531_glb(12)), out));
    if (!out.error.empty()) std::printf("    %s\n", out.error.c_str());
    const ImportedNode *node = nullptr;
    for (const ImportedNode &n : out.nodes)
      if (n.points) node = &n;
    CHECK(node && !node->mesh);
    if (!node) return;
    PointCloud want = r531_expected(12, 15, 1);
    const double d = r531_diff(*node->points, want, kR531MirrorX);
    std::printf("    %zu splats, degree %d, largest difference %.2e\n", node->points->count(), node->points->sh_degree, d);
    CHECK(node->points->is_splats() && node->points->sh_degree == 1 && d < 2e-4);
    ImportResult plain;
    CHECK(import_model(r531_write("points.glb", r531_glb(5, false)), plain));
    bool points = false;
    for (const ImportedNode &n : plain.nodes) points = points || (n.points && !n.points->is_splats() && n.points->count() == 5);
    CHECK(points);
  });

  test("splat import (review): an SPZ v4 whose table promises 2^28 points of tiny streams is refused before it allocates them", [] {
    std::string head;
    r531_put32(head, 0x5053474e);
    r531_put32(head, 4);
    r531_put32(head, 1u << 28);
    head += (char)4, head += (char)12, head += (char)0, head += (char)6;
    r531_put32(head, 32);
    head += std::string(12, '\0');
    const uint64_t n = 1ull << 28, per[6] = {9, 1, 3, 3, 4, 72};
    std::string toc, data;
    for (int s = 0; s < 6; s++) {
      const uint64_t cs = 16, us = n * per[s];
      toc.append((const char *)&cs, 8), toc.append((const char *)&us, 8);
      data += std::string(16, '\x28');
    }
    SplatReadReport rep;
    CHECK(!read_spz(head + toc + data, rep) && !rep.error.empty());
    std::printf("    %s\n", rep.error.c_str());
  });

  test("splat import (review): an unnormalised PLY rotation turns the splat as its unit quaternion does, mirrored or not", [] {
    PointCloud pc;
    pc.type = PointCloudType::GaussianSplats;
    pc.resize(1);
    pc.scale[0] = Vec3(1.0f, 0.2f, 0.05f);
    const Quat unit = Quat::axis_angle(normalize(Vec3(0.3f, 1.0f, -0.4f)), 0.7f);
    pc.rotation[0] = Quat(unit.x * 2.0f, unit.y * 2.0f, unit.z * 2.0f, unit.w * 2.0f);  // (|q|^2 = 4, as a trainer saves it)
    PointCloud want = pc;
    want.rotation[0] = unit;
    for (const auto *m : {&kR531MirrorX, &kR531FromZUp}) {
      PointCloud got = pc;
      pointcloud_change_axes(got, *m);
      CHECK(r531_diff(got, want, *m) < 1e-4);
    }
  });

  test("splat import: `import file.spz` in the editor makes one Gaussian Splats object named for the file, one undo step", [] {
    const R531Spz z = r531_spz(3, 16, 1);
    const std::string path = r531_write("garden.spz", gzip(r531_spz_raw(z, z.n)));
    Editor ed;
    ed.init_headless(1200, 800);
    ed.step_frame_headless();
    const size_t steps = ed.undo_steps_for_test();
    ed.command("import " + path);
    ed.step_frame_headless();
    GameObject *g = r530_find_cloud(ed);
    CHECK(g && g->name == "garden" && g->get<PointCloudObject>()->data && g->get<PointCloudObject>()->data->count() == 16);
    CHECK(ed.undo_steps_for_test() == steps + 1);
    CHECK(model_extension_supported(".spz"));
  });
}

/* ------------------------------------------------------------- round 531: the test engineer's edge cases */

namespace {

constexpr float kR531Identity[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

struct R531eProp {
  const char *type;
  std::string name;
};

/* One PLY value in the file's type: ASCII as text, binary in the type's bytes (little endian, or reversed for big). */
void r531e_put(std::string &s, const std::string &type, double v, int format) {
  if (format == 0) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.17g ", v);
    s += buf;
    return;
  }
  unsigned char b[8];
  size_t n = 4;
  if (type == "float") {
    const float f = (float)v;
    std::memcpy(b, &f, 4);
  }
  else if (type == "double") {
    std::memcpy(b, &v, 8);
    n = 8;
  }
  else if (type == "uchar") {
    b[0] = (unsigned char)v;
    n = 1;
  }
  else if (type == "ushort") {
    const uint16_t x = (uint16_t)v;
    std::memcpy(b, &x, 2);
    n = 2;
  }
  else if (type == "short") {
    const int16_t x = (int16_t)v;
    std::memcpy(b, &x, 2);
    n = 2;
  }
  else {  // int
    const int32_t x = (int32_t)v;
    std::memcpy(b, &x, 4);
  }
  if (format == 2) std::reverse(b, b + n);
  s.append((const char *)b, n);
}

/* A PLY of `rows` with the given property types. `before_header` / `before_body` are another element ahead of the
 * vertices (header lines ending in \n; its records in the file's encoding). `eol` ends the header and ASCII lines. */
std::string r531e_ply(int format, const std::vector<R531eProp> &props, const std::vector<std::vector<double>> &rows, const std::string &before_header = "",
                      const std::string &before_body = "", const char *eol = "\n", bool blank_lines = false) {
  const std::string e = eol;
  std::string s = "ply" + e + "format " + std::string(format == 0 ? "ascii" : format == 1 ? "binary_little_endian" : "binary_big_endian") + " 1.0" + e;
  s += before_header;
  s += "element vertex " + std::to_string(rows.size()) + e;
  for (const R531eProp &p : props) s += "property " + std::string(p.type) + " " + p.name + e;
  s += "end_header" + e;
  s += before_body;
  for (const std::vector<double> &row : rows) {
    for (size_t k = 0; k < props.size(); k++) r531e_put(s, props[k].type, row[k], format);
    if (format != 0) continue;
    s += e;
    if (blank_lines) s += e;
  }
  return s;
}

std::string r531e_glb(const std::string &bin_in, const std::string &views, const std::string &accessors, const std::string &primitives) {
  std::string bin = bin_in;
  std::string json = strprintf("{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":%zu}],\"bufferViews\":[%s],\"accessors\":[%s],"
                               "\"meshes\":[{\"name\":\"Mixed\",\"primitives\":[%s]}],\"nodes\":[{\"name\":\"MixedNode\",\"mesh\":0}],"
                               "\"scenes\":[{\"nodes\":[0]}],\"scene\":0,\"extensionsUsed\":[\"KHR_gaussian_splatting\"]}",
                               bin.size(), views.c_str(), accessors.c_str(), primitives.c_str());
  while (json.size() % 4) json += ' ';
  while (bin.size() % 4) bin += '\0';
  std::string glb = "glTF";
  r531_put32(glb, 2);
  r531_put32(glb, (uint32_t)(12 + 8 + json.size() + 8 + bin.size()));
  r531_put32(glb, (uint32_t)json.size());
  r531_put32(glb, 0x4E4F534Au);
  glb += json;
  r531_put32(glb, (uint32_t)bin.size());
  r531_put32(glb, 0x004E4942u);
  glb += bin;
  return glb;
}

std::string r531e_floats(const std::vector<float> &v) { return std::string((const char *)v.data(), v.size() * 4); }

/* A splat PLY's rows (r531_splat's values) with `rest` f_rest values a row, ordered as r531_ply writes them. */
std::vector<std::vector<double>> r531e_splat_rows(int n, int rest) {
  std::vector<std::vector<double>> rows;
  for (int i = 0; i < n; i++) {
    const R531Splat s = r531_splat(i);
    std::vector<double> v = {s.pos[0], s.pos[1], s.pos[2], s.dc[0], s.dc[1], s.dc[2]};
    for (int k = 0; k < rest; k++) v.push_back(s.rest[0][k]);
    v.insert(v.end(), {s.opacity, s.scale[0], s.scale[1], s.scale[2], s.rot[0], s.rot[1], s.rot[2], s.rot[3]});
    rows.push_back(v);
  }
  return rows;
}
std::vector<R531eProp> r531e_splat_props(int rest, const char *dc_type = "float", const char *opacity_type = "float") {
  std::vector<R531eProp> p = {{"float", "x"}, {"float", "y"}, {"float", "z"}, {dc_type, "f_dc_0"}, {dc_type, "f_dc_1"}, {dc_type, "f_dc_2"}};
  for (int k = 0; k < rest; k++) p.push_back({"float", "f_rest_" + std::to_string(k)});
  p.push_back({opacity_type, "opacity"});
  for (const char *n : {"scale_0", "scale_1", "scale_2", "rot_0", "rot_1", "rot_2", "rot_3"}) p.push_back({"float", n});
  return p;
}

}  // namespace

static void tests_round531_engineer() {
  test("splat import: a PLY whose points come after another element reads the points, not the other element (ASCII, little and big endian)", [] {
    const std::vector<std::vector<double>> rows = {{1, 2, 3}, {4, 5, 6}, {-1, 0.5, 2}};
    for (int format = 0; format < 3; format++) {
      std::string pre;
      for (int k = 0; k < 2; k++) {
        r531e_put(pre, "float", 1.5 + k, format);
        r531e_put(pre, "uchar", 7 + k, format);
        if (format == 0) pre += "\n";
      }
      const std::string ply = r531e_ply(format, {{"float", "x"}, {"float", "y"}, {"float", "z"}}, rows, "element camera 2\nproperty float focal\nproperty uchar id\n", pre);
      CHECK(ply_is_point_cloud(ply));
      SplatReadReport rep;
      PointCloudPtr pc = read_ply_point_cloud(ply, rep);
      CHECK(pc != nullptr);
      if (!pc) {
        std::printf("    format %d: %s\n", format, rep.error.c_str());
        continue;
      }
      CHECK(!pc->is_splats() && pc->count() == 3);
      if (pc->count() != 3) continue;
      CHECK(pc->position[1].x == -4.0f && pc->position[1].y == 6.0f && pc->position[1].z == -5.0f);  // (Z up -> Blendity: (-x, z, -y))
      CHECK(pc->position[2].x == 1.0f && pc->position[2].y == 2.0f && pc->position[2].z == -0.5f);
    }
  });

  test("splat import: a PLY's double, uchar, ushort and int properties are read as numbers (a byte colour as sRGB, an extra int ignored)", [] {
    const std::vector<std::vector<double>> rows = {{1.25, 2, 3, 255, 0, 128, 42}, {-4, 5.5, 6, 0, 255, 255, -7}};
    for (int format = 0; format < 3; format++) {
      const std::string ply = r531e_ply(format, {{"double", "x"}, {"double", "y"}, {"double", "z"}, {"uchar", "red"}, {"uchar", "green"}, {"uchar", "blue"}, {"int", "intensity"}}, rows);
      SplatReadReport rep;
      PointCloudPtr pc = read_ply_point_cloud(ply, rep);
      CHECK(pc && !pc->is_splats() && pc->count() == 2);
      if (!pc || pc->count() != 2) {
        std::printf("    format %d: %s\n", format, rep.error.c_str());
        continue;
      }
      CHECK(pc->position[0].x == -1.25f && pc->position[0].y == 3.0f && pc->position[0].z == -2.0f);
      CHECK(pc->color[0].x == 1.0f && pc->color[0].y == 0.0f);
      CHECK_NEAR(pc->color[0].z, 0.2158605, 1e-4);
      CHECK(pc->color[1].x == 0.0f && pc->color[1].y == 1.0f && pc->color[1].z == 1.0f);
    }
    /* 16-bit colours: 0..65535 as sRGB. */
    const std::string wide = r531e_ply(1, {{"float", "x"}, {"float", "y"}, {"float", "z"}, {"ushort", "red"}, {"ushort", "green"}, {"ushort", "blue"}}, {{0, 0, 0, 65535, 0, 32768}});
    SplatReadReport rw;
    PointCloudPtr pw = read_ply_point_cloud(wide, rw);
    CHECK(pw && pw->count() == 1);
    if (pw && pw->count() == 1) CHECK(pw->color[0].x == 1.0f && pw->color[0].y == 0.0f);
    if (pw && pw->count() == 1) CHECK_NEAR(pw->color[0].z, 0.21404, 1e-3);
  });

  test("splat import: a Gaussian splat PLY written with double f_dc and opacity decodes as with floats", [] {
    const int n = 6;
    const PointCloud want = r531_expected(n, 0, 0);
    for (int format = 0; format < 3; format++) {
      const std::string ply = r531e_ply(format, r531e_splat_props(0, "double", "double"), r531e_splat_rows(n, 0));
      SplatReadReport rep;
      PointCloudPtr pc = read_ply_point_cloud(ply, rep);
      CHECK(pc != nullptr);
      if (!pc) {
        std::printf("    format %d: %s\n", format, rep.error.c_str());
        continue;
      }
      CHECK(pc->is_splats() && pc->count() == (size_t)n && pc->sh_degree == 0);
      CHECK(r531_diff(*pc, want, kR531FromZUp) < 2e-4);
    }
  });

  test("splat import: a PLY with only x y z becomes white Points (ASCII and binary), not a refusal", [] {
    for (int format = 0; format < 3; format++) {
      const std::string ply = r531e_ply(format, {{"float", "x"}, {"float", "y"}, {"float", "z"}}, {{0, 0, 0}, {1, 2, 3}, {-1, -2, -3}});
      CHECK(ply_is_point_cloud(ply));
      SplatReadReport rep;
      PointCloudPtr pc = read_ply_point_cloud(ply, rep);
      CHECK(pc != nullptr);
      if (!pc) {
        std::printf("    format %d: %s\n", format, rep.error.c_str());
        continue;
      }
      CHECK(!pc->is_splats() && pc->count() == 3 && pc->consistent());
      if (pc->count() != 3) continue;
      for (const Vec3 &c : pc->color) CHECK(c.x == 1.0f && c.y == 1.0f && c.z == 1.0f);
      CHECK(rep.notes.empty());
    }
  });

  test("splat import: f_rest counts that are not a multiple of 3, or an incomplete first band, drop the SH with a note but keep the splats", [] {
    const int n = 5;
    const PointCloud want = r531_expected(n, 0, 0);
    for (int rest : {1, 2, 4, 5, 7, 3}) {
      const std::string ply = r531e_ply(1, r531e_splat_props(rest), r531e_splat_rows(n, rest));
      SplatReadReport rep;
      PointCloudPtr pc = read_ply_point_cloud(ply, rep);
      CHECK(pc != nullptr);
      if (!pc) {
        std::printf("    %d f_rest: %s\n", rest, rep.error.c_str());
        continue;
      }
      std::printf("    %d f_rest: degree %d, %zu note(s)%s%s\n", rest, pc->sh_degree, rep.notes.size(), rep.notes.empty() ? "" : ": ", rep.notes.empty() ? "" : rep.notes[0].c_str());
      CHECK(pc->is_splats() && pc->count() == (size_t)n && pc->consistent());
      CHECK(pc->sh_degree == 0 && pc->sh_rest.empty());
      CHECK(rep.notes.size() == 1);
      CHECK(r531_diff(*pc, want, kR531FromZUp) < 2e-4);
    }
  });

  test("splat import: a PLY with no x property is not a point cloud and refuses with a reason (never a cloud at the origin)", [] {
    const std::string no_x = r531e_ply(1, {{"float", "y"}, {"float", "z"}, {"uchar", "red"}}, {{1, 2, 3}, {4, 5, 6}});
    CHECK(!ply_is_point_cloud(no_x));
    SplatReadReport rep;
    CHECK(!read_ply_point_cloud(no_x, rep) && !rep.error.empty());
    /* A splat file missing its z. */
    std::vector<R531eProp> props = r531e_splat_props(0);
    props[2].name = "zz";
    const std::string no_z = r531e_ply(0, props, r531e_splat_rows(3, 0));
    SplatReadReport rz;
    CHECK(!ply_is_point_cloud(no_z) && !read_ply_point_cloud(no_z, rz) && !rz.error.empty());
    /* Through the importer it must not become a cloud. */
    ImportResult out;
    import_model(r531_write("no_x.ply", no_x), out);
    for (const ImportedNode &nd : out.nodes) CHECK(!nd.points);
  });

  test("splat import: an ASCII PLY with Windows line ends, blank lines, tabs and no final newline reads like the plain file", [] {
    const std::string plain = r531_ply(0, 12, 15);
    const size_t body = plain.find("end_header\n") + 11;
    std::string crlf;
    for (size_t i = 0; i < plain.size(); i++) {
      const char c = plain[i];
      if (c == '\n') crlf += i >= body ? "\r\n\r\n" : "\r\n";
      else if (c == ' ' && i >= body) crlf += " \t";
      else crlf += c;
    }
    SplatReadReport rep;
    PointCloudPtr pc = read_ply_point_cloud(crlf, rep);
    CHECK(pc != nullptr);
    if (!pc) {
      std::printf("    %s\n", rep.error.c_str());
      return;
    }
    CHECK(pc->is_splats() && pc->count() == 12 && pc->sh_degree == 3 && rep.notes.empty());
    CHECK(r531_diff(*pc, r531_expected(12, 15, 3), kR531FromZUp) < 2e-4);
    /* The same through the points reader's other face: CRLF in the header of a plain point file, blank lines between rows. */
    const std::string pts = r531e_ply(0, {{"float", "x"}, {"float", "y"}, {"float", "z"}, {"uchar", "red"}, {"uchar", "green"}, {"uchar", "blue"}},
                                      {{0, 0, 0, 255, 0, 0}, {1, 2, 3, 0, 255, 0}}, "", "", "\r\n", true);
    SplatReadReport rp;
    PointCloudPtr pp = read_ply_point_cloud(pts, rp);
    CHECK(pp && !pp->is_splats() && pp->count() == 2);
    if (pp && pp->count() == 2) CHECK(pp->position[1].x == -1.0f && pp->color[1].y == 1.0f);
    /* No newline after the last row. */
    std::string cut = r531_ply(0, 5, 0);
    cut.pop_back();
    SplatReadReport rc;
    PointCloudPtr pcut = read_ply_point_cloud(cut, rc);
    CHECK(pcut && pcut->count() == 5);
    if (pcut && pcut->count() == 5) CHECK(r531_diff(*pcut, r531_expected(5, 0, 0), kR531FromZUp) < 2e-4);
  });

  test("splat import: a PLY with faces (or edges, or an unskippable list before the points) stays a mesh and is never a cloud", [] {
    const std::string tri = "ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\n"
                            "element face 1\nproperty list uchar int vertex_indices\nend_header\n0 0 0\n1 0 0\n0 1 0\n3 0 1 2\n";
    CHECK(!ply_is_point_cloud(tri));
    ImportResult out;
    CHECK(import_model(r531_write("tri.ply", tri), out));
    CHECK(out.nodes.size() == 1 && out.nodes[0].mesh && !out.nodes[0].points);
    if (out.nodes.size() == 1 && out.nodes[0].mesh) CHECK(out.nodes[0].mesh->face_count() == 1);
    const std::string edges = "ply\nformat ascii 1.0\nelement vertex 2\nproperty float x\nproperty float y\nproperty float z\n"
                              "element edge 1\nproperty int vertex1\nproperty int vertex2\nend_header\n0 0 0\n1 0 0\n0 1\n";
    CHECK(!ply_is_point_cloud(edges));
    /* A binary face element ahead of the points: not a cloud, and the point reader can't skip its list either. */
    const std::string face_first = "ply\nformat binary_little_endian 1.0\nelement face 2\nproperty list uchar int vertex_indices\n"
                                   "element vertex 1\nproperty float x\nproperty float y\nproperty float z\nend_header\n" + std::string(40, '\0');
    CHECK(!ply_is_point_cloud(face_first));
    SplatReadReport rep;
    CHECK(!read_ply_point_cloud(face_first, rep) && !rep.error.empty());
    /* An empty face element after the points is still a cloud (binary). */
    const std::string empty_faces = "ply\nformat binary_little_endian 1.0\nelement vertex 1\nproperty float x\nproperty float y\nproperty float z\n"
                                    "element face 0\nproperty list uchar int vertex_indices\nend_header\n" + r531e_floats({1, 2, 3});
    CHECK(ply_is_point_cloud(empty_faces));
    SplatReadReport re;
    PointCloudPtr pc = read_ply_point_cloud(empty_faces, re);
    CHECK(pc && pc->count() == 1);
  });

  test("splat import: PLY headers that lie refuse cleanly - an unknown type, no format, a huge count the file can't hold (ASCII and binary), a skipped element that isn't there", [] {
    auto refused = [](const std::string &bytes) {
      SplatReadReport r;
      return !read_ply_point_cloud(bytes, r) && !r.error.empty();
    };
    CHECK(refused("ply\nformat ascii 1.0\nelement vertex 1\nproperty quux x\nproperty float y\nproperty float z\nend_header\n1 2 3\n"));
    CHECK(refused("ply\nelement vertex 1\nproperty float x\nproperty float y\nproperty float z\nend_header\n1 2 3\n"));
    CHECK(refused("ply\nformat sideways 1.0\nelement vertex 1\nproperty float x\nproperty float y\nproperty float z\nend_header\n1 2 3\n"));
    /* A count under the cap (2^28) that the three rows can't fill: refused before anything of that size is allocated. */
    CHECK(refused("ply\nformat ascii 1.0\nelement vertex 200000000\nproperty float x\nproperty float y\nproperty float z\nend_header\n1 2 3\n4 5 6\n7 8 9\n"));
    CHECK(refused("ply\nformat binary_little_endian 1.0\nelement vertex 200000000\nproperty float x\nproperty float y\nproperty float z\nend_header\n" + r531e_floats({1, 2, 3, 4, 5, 6})));
    CHECK(refused("ply\nformat binary_little_endian 1.0\nelement vertex 268435456\nproperty double x\nproperty double y\nproperty double z\nend_header\n" + std::string(48, '\1')));
    /* An element ahead of the points that claims more bytes than the file has. */
    CHECK(refused("ply\nformat binary_little_endian 1.0\nelement camera 1000\nproperty float a\nelement vertex 2\nproperty float x\nproperty float y\nproperty float z\nend_header\n" +
                  r531e_floats({1, 2, 3, 4, 5, 6})));
    /* A vertex element of nothing but a list. */
    CHECK(refused("ply\nformat ascii 1.0\nelement vertex 1\nproperty list uchar float x\nend_header\n3 1 2 3\n"));
    /* Not a PLY at all. */
    CHECK(refused("plx\nformat ascii 1.0\n"));
    CHECK(refused(std::string()));
  });

  test("splat import: SPZ flags 1 (anti-aliasing) and 2 (extensions) each add a note; the splats are unchanged", [] {
    for (uint32_t version : {2u, 3u}) {
      const R531Spz z = r531_spz(version, 10, 2);
      const PointCloud want = r531_spz_expected(z);
      for (int flags : {0, 1, 2, 3}) {
        std::string raw = r531_spz_raw(z, z.n);
        raw[14] = (char)flags;
        SplatReadReport rep;
        PointCloudPtr pc = read_spz(gzip(raw), rep);
        CHECK(pc != nullptr);
        if (!pc) {
          std::printf("    v%u flags %d: %s\n", version, flags, rep.error.c_str());
          continue;
        }
        CHECK(rep.notes.size() == (size_t)((flags & 1) + ((flags >> 1) & 1)));
        CHECK(pc->count() == 10 && pc->sh_degree == 2 && r531_diff(*pc, want, kR531MirrorX) < 2e-4);
      }
    }
  });

  test("splat import: SPZ fractional bits 0, 1 and 23 decode; 24 and more are refused with a message about the bits", [] {
    for (int frac : {0, 1, 23}) {
      R531Spz z = r531_spz(3, 10, 1);
      z.fractional = frac;
      SplatReadReport rep;
      PointCloudPtr pc = read_spz(gzip(r531_spz_raw(z, z.n)), rep);
      CHECK(pc != nullptr);
      if (!pc) {
        std::printf("    %d fractional bits: %s\n", frac, rep.error.c_str());
        continue;
      }
      CHECK(pc->count() == 10 && r531_diff(*pc, r531_spz_expected(z), kR531MirrorX) < 2e-4);
    }
    for (int frac : {24, 25, 31, 32, 255}) {
      R531Spz z = r531_spz(2, 10, 1);
      z.fractional = frac;
      SplatReadReport rep;
      CHECK(!read_spz(gzip(r531_spz_raw(z, z.n)), rep) && rep.error.find("fractional") != std::string::npos);
    }
  });

  test("splat import: an SPZ gzip with a lying trailer (size, checksum) or a decompression bomb refuses or reads cleanly, never overruns", [] {
    const R531Spz z = r531_spz(3, 10, 1);
    const std::string gz = gzip(r531_spz_raw(z, z.n));
    auto sane = [](const std::string &bytes) {
      SplatReadReport r;
      PointCloudPtr pc = read_spz(bytes, r);
      return pc ? (pc->consistent() && pc->count() <= 10) : !r.error.empty();
    };
    std::string huge = gz;  // ISIZE: the last four bytes claim 4 GB - 1
    for (size_t k = 1; k <= 4; k++) huge[huge.size() - k] = (char)0xFF;
    CHECK(sane(huge));
    std::string zero = gz;
    for (size_t k = 1; k <= 4; k++) zero[zero.size() - k] = 0;
    CHECK(sane(zero));
    std::string crc = gz;  // CRC32
    crc[crc.size() - 8] ^= 0x55;
    CHECK(sane(crc));
    CHECK(sane(gz.substr(0, gz.size() - 4)));  // no ISIZE
    CHECK(sane(gz.substr(0, gz.size() - 8)));  // no trailer at all
    /* A header that promises 2^28 points over a few megabytes of zeros: refused, and quickly (no 2^28-point allocation). */
    std::string raw;
    r531_put32(raw, 0x5053474e);
    r531_put32(raw, 3);
    r531_put32(raw, 1u << 28);
    raw += std::string("\0\x0c\0\0", 4);
    raw += std::string((size_t)4 << 20, '\0');
    SplatReadReport rb;
    CHECK(!read_spz(gzip(raw), rb) && !rb.error.empty());
    /* Zeros that do not even have the magic. */
    SplatReadReport rm;
    CHECK(!read_spz(gzip(std::string((size_t)4 << 20, '\0')), rm) && !rm.error.empty());
  });

  test("splat import: a glTF POINTS primitive whose accessor count the buffer can't hold is left out (no crash, no cloud of that size)", [] {
    const std::string bin = r531e_floats({0.5f, 1.0f, 2.0f});
    const std::string views = "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":12}";
    const std::string prim = "{\"mode\":0,\"attributes\":{\"POSITION\":0},\"extensions\":{\"KHR_gaussian_splatting\":{}}}";
    for (const char *count : {"1000000", "4000000000", "1000000000000", "-3"}) {
      const std::string acc = strprintf("{\"bufferView\":0,\"componentType\":5126,\"count\":%s,\"type\":\"VEC3\"}", count);
      ImportResult out;
      import_model(r531_write("overcount.glb", r531e_glb(bin, views, acc, prim)), out);
      for (const ImportedNode &nd : out.nodes) CHECK(!nd.points);
    }
    /* A count above the data but under the buffer's byte size: refused too (review 531 - it gave 5 points, 4 of them
     * zeros at the origin), with a note. */
    const std::string acc5 = "{\"bufferView\":0,\"componentType\":5126,\"count\":5,\"type\":\"VEC3\"}";
    ImportResult out5;
    import_model(r531_write("overcount5.glb", r531e_glb(bin, views, acc5, prim)), out5);
    for (const ImportedNode &nd : out5.nodes) CHECK(!nd.points);
    bool noted = false;
    for (const std::string &n : out5.notes) noted = noted || n.find("outside its buffer") != std::string::npos;
    CHECK(noted);
    /* A splat primitive whose SCALE can't be read keeps its centres as Points (a note), the other attributes' count is fine. */
    const std::string bin2 = r531e_floats({0, 0, 0, 1, 1, 1});
    const std::string views2 = "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":24}";
    const std::string acc2 = "{\"bufferView\":0,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"},"
                             "{\"bufferView\":0,\"componentType\":5126,\"count\":1000000000,\"type\":\"VEC3\"}";
    const std::string prim2 = "{\"mode\":0,\"attributes\":{\"POSITION\":0,\"KHR_gaussian_splatting:SCALE\":1},\"extensions\":{\"KHR_gaussian_splatting\":{}}}";
    ImportResult o2;
    import_model(r531_write("noscale.glb", r531e_glb(bin2, views2, acc2, prim2)), o2);
    const ImportedNode *node = nullptr;
    for (const ImportedNode &nd : o2.nodes)
      if (nd.points) node = &nd;
    CHECK(node != nullptr);
    if (node) CHECK(!node->points->is_splats() && node->points->count() == 2 && node->points->consistent());
  });

  test("splat import: a glTF mesh with triangles and points keeps its mesh and gets the cloud on the same node, in either primitive order", [] {
    std::vector<float> data = {0, 0, 0, 1, 0, 0, 0, 1, 0};  // one triangle
    for (float f : {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, -1.0f, -2.0f, -3.0f}) data.push_back(f);  // four points
    const std::string views = "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":48}";
    const std::string acc = "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},{\"bufferView\":1,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"}";
    const std::string tri = "{\"mode\":4,\"attributes\":{\"POSITION\":0}}", pts = "{\"mode\":0,\"attributes\":{\"POSITION\":1}}";
    for (int order = 0; order < 2; order++) {
      ImportResult out;
      CHECK(import_model(r531_write("mixed.glb", r531e_glb(r531e_floats(data), views, acc, order ? pts + "," + tri : tri + "," + pts)), out));
      const ImportedNode *node = nullptr;
      for (const ImportedNode &nd : out.nodes)
        if (nd.points) node = &nd;
      CHECK(node != nullptr);
      if (!node) {
        std::printf("    order %d: no cloud (%s)\n", order, out.error.c_str());
        continue;
      }
      CHECK(node->mesh && node->mesh->face_count() == 1);
      CHECK(!node->points->is_splats() && node->points->count() == 4 && node->points->consistent());
      if (node->points->count() == 4) CHECK(node->points->position[0].x == -1.0f && node->points->position[0].y == 2.0f && node->points->position[0].z == 3.0f);  // (glTF mirror: -x)
    }
  });

  test("splat import: an imported cloud survives saving and loading the scene (splats with SH, and points with colours)", [] {
    const std::string points = "ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\n"
                               "property uchar red\nproperty uchar green\nproperty uchar blue\nend_header\n0 0 0 255 0 0\n1 2 3 0 255 0\n-1 -2 -3 128 128 128\n";
    const std::string files[2] = {r531_ply(1, 12, 15), points};
    for (int k = 0; k < 2; k++) {
      ImportResult out;
      CHECK(import_model(r531_write(k ? "roundtrip_points.ply" : "roundtrip_splats.ply", files[k]), out));
      CHECK(out.nodes.size() == 1 && out.nodes[0].points);
      if (out.nodes.size() != 1 || !out.nodes[0].points) continue;
      Scene s;
      GameObject *g = instantiate_import(s, out, "Cloud", nullptr);
      CHECK(g && g->get<PointCloudObject>() && g->get<PointCloudObject>()->data);
      const std::string text = save_scene_text(s);
      Scene back;
      std::string err;
      CHECK(load_scene_text(text, back, err));
      if (!err.empty()) std::printf("    %s\n", err.c_str());
      PointCloudObject *got = nullptr;
      back.for_each([&](GameObject &o) {
        if (!got && o.get<PointCloudObject>()) got = o.get<PointCloudObject>();
      });
      CHECK(got && got->data);
      if (!got || !got->data) continue;
      const PointCloud &a = *out.nodes[0].points, &b = *got->data;
      CHECK(b.consistent() && b.type == a.type && b.sh_degree == a.sh_degree && b.count() == a.count());
      if (b.count() != a.count() || !b.consistent()) continue;
      if (a.is_splats()) {
        const double d = r531_diff(b, a, kR531Identity);
        std::printf("    splats after save / load: largest difference %.2e\n", d);
        CHECK(d < 1e-5 && a.sh_degree == 3);
      }
      else {
        double worst = 0.0;
        for (size_t i = 0; i < a.count(); i++) {
          worst = std::max(worst, (double)std::fabs(a.position[i].x - b.position[i].x) + std::fabs(a.position[i].y - b.position[i].y) + std::fabs(a.position[i].z - b.position[i].z));
          worst = std::max(worst, (double)std::fabs(a.color[i].x - b.color[i].x) + std::fabs(a.color[i].y - b.color[i].y) + std::fabs(a.color[i].z - b.color[i].z));
        }
        CHECK(worst < 1e-5);
      }
      CHECK(got->point_size == g->get<PointCloudObject>()->point_size);
    }
  });
}

/* ------------------------------------------------------------- round 561: the Learn section's steps (task 0543) */

static void tests_round561() {
  test("learn: 'Point clouds and Gaussian splats' walks clean in a fresh editor - splats, Points beside them, the Game view, F12 on the CPU, the card's device - with its questions", [] {
    int at = -1;
    for (int i = 0; i < lesson_count() && at < 0; i++)
      if (std::string(lesson(i).body).find("\n# Point clouds and Gaussian splats") != std::string::npos) at = i;
    CHECK(at >= 0);
    if (at < 0) return;
    Editor ed;
    ed.init_headless(1200, 800);
    ed.step_frame_headless();
    std::istringstream lines(std::string(lesson(at).body));
    std::string line;
    bool in = false;
    int steps = 0, questions = 0, warned = 0;
    while (std::getline(lines, line)) {
      if (line.rfind("# ", 0) == 0) in = line == "# Point clouds and Gaussian splats";
      if (!in) continue;
      if (line.rfind("? ", 0) == 0) questions++;
      if (line.rfind("! cmd:", 0) != 0) continue;
      steps++;
      const std::string cmds = line.substr(6, line.find(" | ") - 6);
      std::vector<LogEntry> sink;
      const size_t base = Log::fetch(0, sink);
      for (size_t a = 0; a <= cmds.size();) {  // (';'-separated, as the Learn tab runs them)
        const size_t e = cmds.find(';', a);
        std::string c = cmds.substr(a, e == std::string::npos ? std::string::npos : e - a);
        while (!c.empty() && c.front() == ' ') c.erase(c.begin());
        while (!c.empty() && c.back() == ' ') c.pop_back();
        if (!c.empty()) ed.command(c);
        if (e == std::string::npos) break;
        a = e + 1;
      }
      ed.step_frame_headless();
      std::vector<LogEntry> got;
      Log::fetch(base, got);
      bool w = false;
      for (const LogEntry &le : got)
        if (le.level != LogLevel::Info) w = true, std::printf("    '%s' warned: %s\n", cmds.c_str(), le.text.c_str());
      warned += w;
    }
    std::printf("    %d Try-it steps, %d warned, %d questions\n", steps, warned, questions);
    CHECK(steps >= 6);
    CHECK(warned == 0);
    CHECK(questions >= 6);
    /* What the steps leave (review 561): exactly two clouds - the splats and the Points refilled in place, level with
     * them beside them - and the path tracer on GPU Compute (the card's step: it falls back to the CPU without one). */
    int clouds = 0, splats = 0, points = 0;
    Vec3 points_at(0.0f);
    ed.scene().for_each([&](GameObject &g) {
      const PointCloudObject *pc = g.get<PointCloudObject>();
      if (!pc) return;
      clouds++;
      if (!pc->data || pc->data->count() != 10000) return;
      if (pc->data->is_splats()) splats++;
      else points++, points_at = g.world_position();
    });
    CHECK(clouds == 2);
    CHECK(splats == 1 && points == 1);
    CHECK(points_at.x == 2.5f && points_at.y == 0.5f && points_at.z == 0.0f);
    CHECK(ed.scene().render.engine == 1);
    CHECK(ed.scene().render.device == 1);
  });
}

/* ------------------------------------------------------------- round 567 (task 0549): a measured import */

static void tests_round567() {
  /* A sample of the stress section's sweep (ADR 0056): 20,000 splats where the sweep reads a million. The times are
   * printed, never judged. */
  test("splat import: a 20,000-splat PLY and SPZ read whole, and the reader says where its time went (printed, never judged)", [] {
    const int n = 20000;
    const R531Spz z = r531_spz(3, (uint32_t)n, 3);
    struct Case {
      const char *name;
      std::string bytes;
      bool spz;
    } cases[] = {{"PLY binary", r531_ply(1, n, 15), false}, {"SPZ v3 gzip", gzip(r531_spz_raw(z, z.n)), true}};
    for (const Case &c : cases) {
      SplatReadReport rep;
      ScopedTimer t;
      PointCloudPtr pc = c.spz ? read_spz(c.bytes, rep) : read_ply_point_cloud(c.bytes, rep);
      const double total = t.ms();
      CHECK(pc && pc->count() == (size_t)n && pc->sh_degree == 3 && pc->consistent());
      const SplatReadTimes &s = rep.times;
      std::printf("    %s, %d splats (%.1f MB): unpack %.2f, setup %.2f, decode %.2f, axes %.2f, validate %.2f ms; whole read %.2f ms\n", c.name, n,
                  (double)c.bytes.size() / 1048576.0, s.unpack_ms, s.setup_ms, s.decode_ms, s.axes_ms, s.validate_ms, total);
      /* The stages are each a part of the read, none negative; only an SPZ has anything to unpack. */
      CHECK(s.unpack_ms >= 0.0 && s.setup_ms >= 0.0 && s.decode_ms >= 0.0 && s.axes_ms >= 0.0 && s.validate_ms >= 0.0);
      CHECK(s.unpack_ms + s.setup_ms + s.decode_ms + s.axes_ms + s.validate_ms <= total + 1.0);
      if (!c.spz) CHECK(s.unpack_ms == 0.0);
    }
  });

  /* Round 567 reads an all-float little-endian PLY straight from its bytes and decodes on every core. The same splats
   * in the other two encodings still go through the general reader: the three clouds are equal to the float. */
  test("splat import: a PLY gives the same cloud, value for value, written little endian, big endian or as text", [] {
    for (int per_channel : {0, 3, 15}) {
      const int n = 9000;  // (several of the reader's chunks, the last one short)
      SplatReadReport rl, rb, ra;
      PointCloudPtr le = read_ply_point_cloud(r531_ply(1, n, per_channel), rl), be = read_ply_point_cloud(r531_ply(2, n, per_channel), rb),
                    as = read_ply_point_cloud(r531_ply(0, n, per_channel), ra);
      CHECK(le && be && as);
      if (!le || !be || !as) continue;
      CHECK(le->count() == (size_t)n);
      CHECK(r530_same(*le, *be));
      CHECK(r530_same(*le, *as));
    }
  });

  test("splat import: each splat is decoded by itself - a file's first points read the same alone as inside a larger file", [] {
    const int n = 9000;
    for (int cut : {1, 4095, 4097}) {
      SplatReadReport r1, r2, r3, r4;
      PointCloudPtr whole = read_ply_point_cloud(r531_ply(1, n, 15), r1), part = read_ply_point_cloud(r531_ply(1, cut, 15), r2);
      const R531Spz z = r531_spz(3, (uint32_t)n, 3), zc = r531_spz(3, (uint32_t)cut, 3);
      PointCloudPtr swhole = read_spz(gzip(r531_spz_raw(z, z.n)), r3), spart = read_spz(gzip(r531_spz_raw(zc, zc.n)), r4);
      CHECK(whole && part && swhole && spart);
      if (!whole || !part || !swhole || !spart) continue;
      for (PointCloudPtr *big : {&whole, &swhole}) {  // (the larger cloud cut to the smaller one's count)
        PointCloud &b = **big;
        const size_t rest = (size_t)b.sh_rest_per_point();
        b.position.resize((size_t)cut), b.color.resize((size_t)cut), b.opacity.resize((size_t)cut), b.scale.resize((size_t)cut), b.rotation.resize((size_t)cut);
        b.sh_rest.resize((size_t)cut * rest);
      }
      CHECK(r530_same(*whole, *part));
      CHECK(r530_same(*swhole, *spart));
    }
  });

  /* Review 567: the SPZ reader has no second encoding to compare with, so its values are pinned to the float against
   * the format's own formulas, a call a value (the reader must not round a scale differently when it decodes a
   * million at once): every byte a log scale and an opacity can be. */
  test("splat import: an SPZ's 256 scale and opacity bytes decode to exactly the format's values", [] {
    R531Spz z = r531_spz(3, 256, 0);
    for (int i = 0; i < 256; i++) {
      z.alpha[(size_t)i] = (char)i;
      for (int k = 0; k < 3; k++) z.scale[(size_t)(3 * i + k)] = (char)((i + 85 * k) & 0xFF);
    }
    SplatReadReport rep;
    PointCloudPtr pc = read_spz(gzip(r531_spz_raw(z, z.n)), rep);
    CHECK(pc && pc->count() == 256);
    if (!pc || pc->count() != 256) return;
    int scale_differs = 0, opacity_differs = 0;
    for (int i = 0; i < 256; i++) {
      float got[3] = {pc->scale[(size_t)i].x, pc->scale[(size_t)i].y, pc->scale[(size_t)i].z};
      for (int k = 0; k < 3; k++) {
        volatile float x = (float)((i + 85 * k) & 0xFF) / 16.0f - 10.0f;  // (volatile: one exp call, never a table or a vector of them)
        const float want = std::exp((float)x);
        scale_differs += got[k] != want;
      }
      opacity_differs += pc->opacity[(size_t)i] != (float)i / 255.0f;
    }
    CHECK(scale_differs == 0);
    CHECK(opacity_differs == 0);
  });

  /* A file with a NaN in it, and a rotation of no length, in the middle of a chunk: what the reader keeps is what it
   * kept before the round - the NaN point dropped, its neighbours untouched. */
  test("splat import: a point with no position is left out of a large PLY and the points round it are untouched", [] {
    const int n = 9000, bad = 5000;
    std::string ply = r531_ply(1, n, 3);
    SplatReadReport r0;
    PointCloudPtr good = read_ply_point_cloud(ply, r0);
    CHECK(good != nullptr);
    if (!good) return;
    const size_t body = ply.find("end_header\n") + 11, stride = (ply.size() - body) / (size_t)n;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(&ply[body + (size_t)bad * stride], &nan, 4);  // (x of point 5000)
    SplatReadReport r1;
    PointCloudPtr pc = read_ply_point_cloud(ply, r1);
    CHECK(pc && pc->count() == (size_t)n - 1 && pc->consistent());
    if (!pc) return;
    bool note = false;
    for (const std::string &s : r1.notes) note = note || s.find("left out") != std::string::npos;
    CHECK(note);
    const Vec3 a = pc->position[(size_t)bad - 1], b = good->position[(size_t)bad - 1], c = pc->position[(size_t)bad], d = good->position[(size_t)bad + 1];
    CHECK(a.x == b.x && a.y == b.y && a.z == b.z);
    CHECK(c.x == d.x && c.y == d.y && c.z == d.z);
  });

  test("splat import: a splat file dropped while jobs run on workers is read off the main thread and lands as one object", [] {
    const R531Spz z = r531_spz(3, 20000, 3);
    const std::string path = r531_write("orchard.spz", gzip(r531_spz_raw(z, z.n)));
    Editor ed;
    ed.init_headless(1200, 800);
    ed.set_async_jobs_for_test(true);
    ed.step_frame_headless();
    ed.command("import " + path);
    CHECK(r530_count_clouds(ed) == 0);  // (the command returns before the file is read)
    int frames = 0;
    for (; frames < 20000 && (ed.import_job_running_for_test() || r530_count_clouds(ed) == 0); frames++) ed.step_frame_headless();
    std::printf("    %d frames stepped while the file was read\n", frames);
    GameObject *g = r530_find_cloud(ed);
    CHECK(g && g->name == "orchard");
    CHECK(g && g->get<PointCloudObject>()->data && g->get<PointCloudObject>()->data->count() == 20000);
    CHECK(r530_count_clouds(ed) == 1);
  });
}

static void tests_round619() {
  test("splat import (round 619): legacy files and borrowed bytes produce the same six arrays", [] {
    for (uint32_t version : {1u, 2u, 3u}) {
      const R531Spz z = r531_spz(version, 300, 3);
      std::string raw = r531_spz_raw(z, z.n);
      raw[14] = 1;  // The anti-aliasing flag also produces an import note.
      const std::string encoded = gzip(raw);
      SplatReadReport report;
      PointCloudPtr borrowed_cloud;
      {
        std::string caller_bytes = encoded;
        borrowed_cloud = read_spz(caller_bytes, report);
        CHECK(caller_bytes == encoded);
        std::fill(caller_bytes.begin(), caller_bytes.end(), '\0');
      }
      CHECK(borrowed_cloud && borrowed_cloud->count() == z.n && borrowed_cloud->sh_degree == 3);
      CHECK(report.error.empty() && !report.notes.empty());
      const auto &times = report.times;
      for (double ms : {times.unpack_ms, times.setup_ms, times.decode_ms, times.axes_ms, times.validate_ms})
        CHECK(std::isfinite(ms) && ms >= 0.0);

      const std::string name = strprintf("r619-valid-v%u", version);
      const std::string path = r531_write(name + ".spz", encoded);
      ImportResult disk;
      CHECK(import_spz_file(path, disk));
      CHECK(disk.source == path && disk.error.empty() && disk.notes == report.notes);
      CHECK(disk.nodes.size() == 1 && disk.nodes[0].points && disk.nodes[0].name == name);
      if (borrowed_cloud && disk.nodes.size() == 1 && disk.nodes[0].points) {
        CHECK(r531_diff(*borrowed_cloud, r531_spz_expected(z), kR531MirrorX) < 2e-4);
        r619_check_cloud(*disk.nodes[0].points, *borrowed_cloud);
      }
    }
  });

  test("splat import (round 619): damaged files and borrowed bytes report the same refusal", [] {
    const R531Spz z = r531_spz(3, 30, 3);
    const std::string raw = r531_spz_raw(z, z.n);
    const std::string encoded = gzip(raw);
    std::string wrong_length = encoded;
    wrong_length[wrong_length.size() - 4] ^= 1;  // Damaged gzip ISIZE.
    const struct {
      const char *name;
      std::string bytes;
      const char *reason;
    } cases[] = {
        {"truncated-gzip", encoded.substr(0, encoded.size() / 2), "gzip stream can't be read"},
        {"bad-gzip-length", wrong_length, "length doesn't match the gzip trailer"},
        {"short-splat-data", gzip(raw.substr(0, raw.size() - 1)), "the data ends before its points do"},
        {"wrong-signature", "not an SPZ", "neither gzip nor NGSP"},
    };
    for (const auto &bad : cases) {
      SplatReadReport report;
      std::string borrowed = bad.bytes;
      CHECK(!read_spz(borrowed, report) && report.error.find(bad.reason) != std::string::npos);
      CHECK(borrowed == bad.bytes);
      ImportResult disk;
      CHECK(!import_spz_file(r531_write(std::string("r619-") + bad.name + ".spz", bad.bytes), disk));
      CHECK(disk.error == report.error && disk.nodes.empty());
    }
  });
}

void run_splats_tests() {
  tests_round530();
  tests_round531();
  tests_round531_engineer();
  tests_round561();
  tests_round567();
  tests_round619();
}

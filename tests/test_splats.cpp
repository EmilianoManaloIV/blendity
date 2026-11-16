// SPDX-License-Identifier: GPL-2.0-or-later
// Point clouds and Gaussian splats (Pass 27): round 530's object and data (task 0510, ADR 0147).
#include "test_common.h"

#include "../src/scene/pointcloud.h"
#include "../src/editor/learn.h"
#include "../src/scene/pointcloud_object.h"

#include <cmath>
#include <limits>

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

void run_splats_tests() {
  tests_round530();
}

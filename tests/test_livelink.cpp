// SPDX-License-Identifier: GPL-2.0-or-later
// blendity_tests: Live Link (task 0172, ADR 0055) - the server, the protocol, the editor's link - with a real client
// on 127.0.0.1, as a bridge would connect.
#include "test_common.h"
#include "../src/core/json.h"
#include "../src/core/net_server.h"

namespace {

struct Message {
  std::string line, bin;
  Json j;
  std::string type() const { return j["t"].str; }
};

/* Everything that arrives within `ms`, frames stepped in between (the editor sends from its frame). */
std::vector<Message> drain(Editor &ed, net::LineClient &c, int frames = 4, int ms = 150) {
  std::vector<Message> out;
  for (int f = 0; f < frames; f++) ed.step_frame_headless();
  if (ed.livelink_for_test()) ed.livelink_for_test()->wait_meshes_for_test();
  Message m;
  while (c.read(m.line, m.bin, ms)) {
    JsonParser p{m.line.data(), m.line.data() + m.line.size()};
    m.j = p.value();
    out.push_back(m);
  }
  return out;
}

/* Reads until a message of `type` (included), stepping frames: the sync of a new client. */
std::vector<Message> until(Editor &ed, net::LineClient &c, const char *type, int max_frames = 200) {
  std::vector<Message> out;
  for (int f = 0; f < max_frames; f++) {
    ed.step_frame_headless();
    Message m;
    while (c.read(m.line, m.bin, 5)) {
      JsonParser p{m.line.data(), m.line.data() + m.line.size()};
      m.j = p.value();
      out.push_back(m);
      if (m.type() == type) return out;
    }
  }
  return out;
}

int count_of(const std::vector<Message> &ms, const char *type) {
  return (int)std::count_if(ms.begin(), ms.end(), [&](const Message &m) { return m.type() == type; });
}

uint32_t u32(const std::string &b, size_t at) {
  uint32_t v = 0;
  if (at + 4 <= b.size()) std::memcpy(&v, b.data() + at, 4);
  return v;
}

/* A glTF binary's JSON chunk, parsed (Null when the header or the chunk is wrong). */
Json glb_json(const std::string &glb) {
  if (glb.size() < 20 || u32(glb, 0) != 0x46546C67u || u32(glb, 4) != 2u || u32(glb, 8) != glb.size() || u32(glb, 16) != 0x4E4F534Au) return Json();
  const uint32_t n = u32(glb, 12);
  if (20 + (size_t)n > glb.size()) return Json();
  JsonParser p{glb.data() + 20, glb.data() + 20 + n};
  Json j = p.value();
  return p.ok ? j : Json();
}

GameObject *make_cube(Editor &ed, const char *name) {
  ed.command("create Cube");
  GameObject *g = ed.active_object_for_test();
  if (!g) return nullptr;
  g->name = name;
  if (MeshRenderer *mr = g->mesh_renderer()) mr->main_material();
  return g;
}

/* The scripted session behind tests/fixtures/livelink_stream.bin: every byte the client received, and the messages. */
struct Session {
  std::string raw;
  std::vector<Message> ms;
};

Session record_session() {
  Session s;
  Editor ed;
  ed.init_headless(640, 400);
  GameObject *box = make_cube(ed, "Box");
  GameObject *root = ed.scene().create("Rig");
  root->add<Armature>();
  GameObject *upper = ed.scene().create("Upper", root);
  upper->add<Bone>();
  GameObject *lower = ed.scene().create("Lower", upper);
  lower->add<Bone>();
  lower->set_local_position(Vec3(0, 1, 0));
  if (!box || !ed.livelink_start(0)) return s;
  Material &mat = *box->mesh_renderer()->materials[0];
  mat.name = "Cel";
  mat.shading_model = 1;
  mat.toon_bands = 2;
  ed.livelink_for_test()->set_interval_for_test(0);
  net::LineClient c;
  if (!c.connect(ed.livelink_for_test()->port())) return s;
  auto take = [&](std::vector<Message> part) {
    for (Message &m : part) {
      s.raw += m.line + "\n" + m.bin;
      s.ms.push_back(std::move(m));
    }
  };
  take(until(ed, c, "sync_end"));
  take(drain(ed, c));
  box->set_local_position(Vec3(1, 2, 3));
  take(drain(ed, c));
  mat.base_color = Vec3(0.9f, 0.4f, 0.2f);
  mat.touch();
  take(drain(ed, c));
  MeshPtr m = std::make_shared<Mesh>(*box->mesh_filter()->mesh);
  m->positions[0] = m->positions[0] + Vec3(0.25f, 0, 0);
  m->touch();
  box->mesh_filter()->mesh = m;
  take(drain(ed, c));
  lower->set_local_rotation(Quat::euler(Vec3(30.0f, 0, 0)));  // (degrees)
  take(drain(ed, c));
  if (GameObject *sphere = ed.scene().find_by_name("Sphere")) ed.scene().destroy(sphere);
  take(drain(ed, c));
  ed.command("play");
  take(drain(ed, c));
  ed.command("stop");
  take(drain(ed, c));
  return s;
}

/* What must stay the same for the bridges: each message's type, id or key, name and vertex count, in order (the
 * meshes, built on a worker, as a sorted list of their own). */
std::vector<std::string> outline(const std::vector<Message> &ms) {
  std::vector<std::string> order, meshes;
  for (const Message &m : ms) {
    const std::string s = m.type() + "|" + std::to_string((long long)m.j["id"].number()) + "|" + std::to_string((long long)m.j["key"].number()) + "|" +
                          m.j["name"].str + "|" + std::to_string((long long)m.j["verts"].number());
    (m.type() == "mesh" ? meshes : order).push_back(s);
  }
  std::sort(meshes.begin(), meshes.end());
  order.insert(order.end(), meshes.begin(), meshes.end());
  return order;
}

std::vector<Message> parse_stream(const std::string &raw) {
  std::vector<Message> out;
  size_t at = 0;
  while (at < raw.size()) {
    const size_t nl = raw.find('\n', at);
    if (nl == std::string::npos) break;
    Message m;
    m.line = raw.substr(at, nl - at);
    const size_t n = net::line_bin_size(m.line);
    m.bin = raw.substr(nl + 1, n);
    JsonParser p{m.line.data(), m.line.data() + m.line.size()};
    m.j = p.value();
    out.push_back(m);
    at = nl + 1 + n;
  }
  return out;
}

}  // namespace

static void livelink_tests() {
  test("json: JsonWriter writes one line JsonParser reads back (escapes, nesting, numbers)", [] {
    JsonWriter w;
    const float v[3] = {1.5f, -2.0f, 0.125f};
    w.obj().key("t").str("say \"hi\"\n\\").key("n").integer(-42).key("v").nums(v, 3).key("o").obj().key("b").boolean(true).end().key("e").arr().end().end();
    CHECK(w.out.find('\n') == std::string::npos);
    JsonParser p{w.out.data(), w.out.data() + w.out.size()};
    const Json j = p.value();
    CHECK(p.ok && j["t"].str == "say \"hi\"\n\\" && j["n"].integer() == -42 && j["v"].size() == 3);
    CHECK_NEAR(j["v"][2].number(), 0.125, 1e-9);
    CHECK(j["o"]["b"].type == Json::Bool && j["o"]["b"].b && j["e"].type == Json::Arr && j["e"].size() == 0);
  });

  test("net: the server's lines and attachments reach a client whole; a newer keyed message replaces a queued one", [] {
    net::Server s;
    std::string err;
    CHECK(s.start(0, &err) && s.port() > 0);
    net::LineClient c;
    CHECK(c.connect(s.port()));
    std::vector<int> fresh;
    for (int i = 0; i < 100 && fresh.empty(); i++) {
      fresh = s.take_new_clients();
      if (fresh.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(fresh.size() == 1);
    if (fresh.empty()) return;
    s.ready(fresh[0]);
    std::string big(3 << 20, '\0');
    for (size_t i = 0; i < big.size(); i++) big[i] = (char)(i * 31 + 7);
    s.broadcast("{\"t\":\"a\"}", big);
    s.broadcast("{}", "xy");
    std::string line, bin;
    CHECK(c.read(line, bin, 3000) && line == "{\"t\":\"a\",\"bin\":3145728}" && bin == big);
    CHECK(c.read(line, bin, 3000) && line == "{\"bin\":2}" && bin == "xy");
    CHECK(net::line_bin_size("{\"name\":\"bin\",\"bin\":12}") == 12 && net::line_bin_size("{\"t\":1}") == 0);
    /* Held: A then B with one key leave B; C (no key) is a barrier, so D and E (the key again) leave E after it. */
    s.pause_sending_for_test(true);
    s.broadcast("{\"m\":\"A\"}", {}, 5);
    s.broadcast("{\"m\":\"B\"}", {}, 5);
    s.broadcast("{\"m\":\"C\"}");
    s.broadcast("{\"m\":\"D\"}", {}, 5);
    s.broadcast("{\"m\":\"E\"}", {}, 5);
    CHECK(s.stats().coalesced == 2);
    s.pause_sending_for_test(false);
    std::string got;
    for (int i = 0; i < 3 && c.read(line, bin, 3000); i++) got += line;
    CHECK(got == "{\"m\":\"B\"}{\"m\":\"C\"}{\"m\":\"E\"}");
    CHECK(!c.read(line, bin, 50));
    /* A deform queued before a mesh (no key: a barrier) is never replaced by one after it: the newer one follows. */
    s.pause_sending_for_test(true);
    s.broadcast("{\"m\":\"D1\"}", {}, 9);
    s.broadcast("{\"m\":\"M\"}");
    s.broadcast("{\"m\":\"D2\"}", {}, 9);
    s.pause_sending_for_test(false);
    got.clear();
    for (int i = 0; i < 3 && c.read(line, bin, 3000); i++) got += line;
    CHECK(got == "{\"m\":\"D1\"}{\"m\":\"M\"}{\"m\":\"D2\"}");
    c.close();
    for (int i = 0; i < 100 && s.client_count() > 0; i++) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(s.client_count() == 0);
    s.stop();
    CHECK(!s.running());
  });

  test("livelink: a client gets hello, the whole scene, and each mesh as a valid glTF binary (indices = 3 x triangles)", [] {
    Editor ed;
    ed.init_headless(640, 400);
    make_cube(ed, "Box");
    ed.command("create Sphere");
    ed.command("livelink start 0");
    livelink::Link *link = ed.livelink_for_test();
    CHECK(link && link->running() && link->port() > 0);
    if (!link || !link->running()) return;
    net::LineClient c;
    CHECK(c.connect(link->port()));
    auto ms = until(ed, c, "sync_end");
    CHECK(!ms.empty() && ms[0].type() == "hello" && ms[0].j["protocol"].str == livelink::kProtocol && ms[0].j["axes"].str == "gltf");
    CHECK(ms.size() > 1 && ms[1].type() == "sync" && ms[1].j["objects"].integer() == (int)ed.scene().object_count());
    CHECK(count_of(ms, "object") == (int)ed.scene().object_count());
    CHECK(count_of(ms, "light") >= 1 && count_of(ms, "camera") >= 1 && count_of(ms, "material") >= 1);
    auto more = drain(ed, c);
    ms.insert(ms.end(), more.begin(), more.end());
    int mesh_objects = 0, checked = 0;
    ed.scene().for_each([&](GameObject &g) {
      if (g.mesh_filter() && g.mesh_filter()->mesh) mesh_objects++;
    });
    CHECK(count_of(ms, "mesh") == mesh_objects && mesh_objects >= 2);
    for (const Message &m : ms) {
      if (m.type() != "mesh") continue;
      GameObject *g = ed.scene().find((uint64_t)m.j["id"].number());
      const Mesh *em = g ? g->evaluated_mesh(0) : nullptr;
      const Json gj = glb_json(m.bin);
      CHECK(em && gj.type == Json::Obj && gj["meshes"].size() == 1);
      if (!em || gj.type != Json::Obj) continue;
      size_t tris = 0;
      for (size_t f = 0; f < em->face_count(); f++) tris += em->face_size(f) - 2;
      size_t indices = 0, positions = 0, verts = 0;
      const Json &prims = gj["meshes"][0]["primitives"];
      for (size_t i = 0; i < prims.size(); i++) {
        indices += (size_t)gj["accessors"][prims[i]["indices"].integer()]["count"].number();
        positions += (size_t)gj["accessors"][prims[i]["attributes"]["POSITION"].integer()]["count"].number();
        verts += (size_t)gj["accessors"][prims[i]["attributes"]["_VERT"].integer()]["count"].number();
        CHECK(prims[i]["extras"]["slot"].type == Json::Num);
      }
      CHECK(indices == tris * 3 && positions == verts && positions >= em->positions.size());
      CHECK(m.j["verts"].integer() == (int)em->positions.size() && m.j["topology"].number() == (double)em->topology_serial());
      checked++;
    }
    CHECK(checked == mesh_objects);
    ed.command("livelink stop");
    CHECK(!link->running());
  });

  test("livelink: a moved object is one transform in glTF's axes; a material edit is one material with its colour", [] {
    Editor ed;
    ed.init_headless(640, 400);
    GameObject *g = make_cube(ed, "Box");
    CHECK(g && ed.livelink_start(0));
    if (!g) return;
    livelink::Link *link = ed.livelink_for_test();
    link->set_interval_for_test(0);
    net::LineClient c;
    CHECK(c.connect(link->port()));
    until(ed, c, "sync_end");
    drain(ed, c);
    g->set_local_position(Vec3(1, 2, 3));
    auto ms = drain(ed, c);
    CHECK(ms.size() == 1 && count_of(ms, "transform") == 1);
    if (ms.size() == 1) {
      const Json &p = ms[0].j["position"];
      CHECK(ms[0].j["id"].number() == (double)g->id);
      CHECK_NEAR(p[0].number(), -1.0, 1e-6);
      CHECK_NEAR(p[1].number(), 2.0, 1e-6);
      CHECK_NEAR(p[2].number(), 3.0, 1e-6);
      CHECK_NEAR(ms[0].j["rotation"][3].number(), 1.0, 1e-6);
    }
    Material &mat = *g->mesh_renderer()->materials[0];
    mat.base_color = Vec3(0.2f, 0.4f, 0.6f);
    mat.touch();
    ms = drain(ed, c);
    CHECK(ms.size() == 1 && count_of(ms, "material") == 1);
    if (ms.size() == 1) {
      const Json &bc = ms[0].j["gltf"]["pbrMetallicRoughness"]["baseColorFactor"];
      CHECK(bc.size() == 4);
      CHECK_NEAR(bc[0].number(), 0.2, 1e-6);
      CHECK_NEAR(bc[1].number(), 0.4, 1e-6);
      CHECK_NEAR(bc[2].number(), 0.6, 1e-6);
      CHECK(ms[0].j["maps"].type == Json::Obj);
    }
    /* Nothing changed: nothing sent. */
    CHECK(drain(ed, c).empty());
  });

  test("livelink: moved vertices are one deform (float32 per vertex, mirrored X); new faces a new mesh", [] {
    Editor ed;
    ed.init_headless(640, 400);
    GameObject *g = make_cube(ed, "Box");
    CHECK(g && ed.livelink_start(0));
    if (!g) return;
    ed.livelink_for_test()->set_interval_for_test(0);
    net::LineClient c;
    CHECK(c.connect(ed.livelink_for_test()->port()));
    until(ed, c, "sync_end");
    drain(ed, c);
    MeshPtr m = std::make_shared<Mesh>(*g->mesh_filter()->mesh);
    m->positions[0] = Vec3(0.75f, 0.5f, -0.25f);
    m->touch();
    g->mesh_filter()->mesh = m;
    auto ms = drain(ed, c);
    CHECK(ms.size() == 1 && count_of(ms, "deform") == 1);
    if (ms.size() == 1) {
      CHECK(ms[0].bin.size() == m->positions.size() * 12 && ms[0].j["verts"].integer() == (int)m->positions.size());
      float p[3];
      std::memcpy(p, ms[0].bin.data(), 12);
      CHECK(p[0] == -0.75f && p[1] == 0.5f && p[2] == -0.25f);
    }
    ed.command("subdivide 1");
    ms = drain(ed, c);
    CHECK(count_of(ms, "mesh") == 1 && count_of(ms, "deform") == 0);
  });

  test("livelink: a posed rig is one pose message with every bone; a deleted object is one delete", [] {
    Editor ed;
    ed.init_headless(640, 400);
    GameObject *root = ed.scene().create("Rig");
    root->add<Armature>();
    GameObject *b1 = ed.scene().create("Upper", root);
    b1->add<Bone>();
    GameObject *b2 = ed.scene().create("Lower", b1);
    b2->add<Bone>();
    b2->set_local_position(Vec3(0, 1, 0));
    CHECK(ed.livelink_start(0));
    ed.livelink_for_test()->set_interval_for_test(0);
    net::LineClient c;
    CHECK(c.connect(ed.livelink_for_test()->port()));
    auto sync = until(ed, c, "sync_end");
    bool bone_of = false;
    for (const Message &m : sync)
      if (m.type() == "object" && m.j["id"].number() == (double)b2->id) bone_of = m.j["bone_of"].number() == (double)root->id;
    CHECK(bone_of);
    drain(ed, c);
    b2->set_local_rotation(Quat::euler(Vec3(0.3f, 0, 0)));
    auto ms = drain(ed, c);
    CHECK(ms.size() == 1 && count_of(ms, "pose") == 1);
    if (ms.size() == 1) CHECK(ms[0].j["id"].number() == (double)root->id && ms[0].j["bones"].size() == 2 && ms[0].j["bones"][0].size() == 11);
    const uint64_t id = b2->id;
    ed.scene().destroy(b2);
    ms = drain(ed, c);
    CHECK(count_of(ms, "delete") == 1 && ms.back().j["id"].number() == (double)id);
  });

  test("livelink: in Play at most 60 transforms a second reach a client, in order, the last one where the object ended", [] {
    Editor ed;
    ed.init_headless(640, 400);
    GameObject *g = make_cube(ed, "Box");
    CHECK(g && ed.livelink_start(0));
    if (!g) return;
    net::LineClient c;
    CHECK(c.connect(ed.livelink_for_test()->port()));
    until(ed, c, "sync_end");
    drain(ed, c);
    ed.command("play");
    g = ed.scene().find_by_name("Box");
    CHECK(g != nullptr);
    if (!g) return;
    const double t0 = now_seconds();
    std::vector<Message> ms;
    for (int i = 1; i <= 300; i++) {
      g->set_local_position(Vec3(i * 0.01f, 0, 0));
      ed.step_frame_headless();
      Message m;
      while (c.read(m.line, m.bin, 0)) {
        JsonParser p{m.line.data(), m.line.data() + m.line.size()};
        m.j = p.value();
        ms.push_back(m);
      }
    }
    const double seconds = now_seconds() - t0;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    auto rest = drain(ed, c, 2);
    ms.insert(ms.end(), rest.begin(), rest.end());
    CHECK(count_of(ms, "play") == 1);
    std::vector<double> xs;
    for (const Message &m : ms)
      if (m.type() == "transform" && m.j["id"].number() == (double)g->id) xs.push_back(-m.j["position"][0].number());
    std::printf("    %zu transforms in %.2f s of 300 frames\n", xs.size(), seconds);
    CHECK(!xs.empty() && xs.size() <= (size_t)(60 * seconds + 2));
    CHECK(std::is_sorted(xs.begin(), xs.end()) && std::adjacent_find(xs.begin(), xs.end()) == xs.end());
    if (!xs.empty()) CHECK_NEAR(xs.back(), 3.0, 1e-5);
    ed.command("stop");
  });

  test("livelink: two clients get the same stream; one that leaves mid-send is dropped and the link goes on", [] {
    Editor ed;
    ed.init_headless(640, 400);
    GameObject *g = make_cube(ed, "Box");
    CHECK(g && ed.livelink_start(0));
    if (!g) return;
    livelink::Link *link = ed.livelink_for_test();
    link->set_interval_for_test(0);
    net::LineClient a, b;
    CHECK(a.connect(link->port()));
    until(ed, a, "sync_end");
    CHECK(b.connect(link->port()));
    until(ed, b, "sync_end");
    drain(ed, a);
    drain(ed, b);
    std::vector<std::string> la, lb;
    for (int i = 0; i < 5; i++) {
      g->set_local_position(Vec3(0, (float)i, 0));
      g->mesh_renderer()->materials[0]->roughness = 0.1f * i;
      ed.step_frame_headless();
    }
    for (const Message &m : drain(ed, a)) la.push_back(m.line);
    for (const Message &m : drain(ed, b, 0)) lb.push_back(m.line);
    CHECK(!la.empty() && la == lb);
    /* A big mesh, and a client that leaves while it goes out. */
    ed.command("create Sphere");
    ed.command("subdivide 3");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    a.close();
    link->wait_meshes_for_test();
    for (int i = 0; i < 100 && link->clients() > 1; i++) {
      ed.step_frame_headless();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(link->clients() == 1);
    g->set_local_position(Vec3(5, 0, 0));
    bool got = false;
    for (const Message &m : drain(ed, b)) got = got || (m.type() == "transform" && m.j["id"].number() == (double)g->id);
    CHECK(got);
    net::LineClient c;
    CHECK(c.connect(link->port()));
    auto ms = until(ed, c, "sync_end");
    CHECK(!ms.empty() && ms.back().type() == "sync_end");
  });

  test("export: glTF primitives name their slot, and a Toon material carries BLENDITY_toon with its fields", [] {
    auto mesh = primitives::cube(1.0f);
    MaterialPtr toon = make_material("Cel", {0.9f, 0.3f, 0.3f});
    toon->shading_model = 1;
    toon->toon_bands = 3;
    ExportOptions o = export_defaults((int)ExportFormat::GLB);
    const std::string glb = export_gltf({ExportItem{"Cube", mesh.get(), Mat4::identity(), {toon}}}, o, true, "", nullptr);
    const Json j = glb_json(glb);
    CHECK(j.type == Json::Obj && j["meshes"][0]["primitives"][0]["extras"]["slot"].integer() == 0);
    const Json &ext = j["materials"][0]["extensions"]["BLENDITY_toon"];
    CHECK(ext.type == Json::Obj && ext.obj.size() > 20);
    bool used = false;
    for (size_t i = 0; i < j["extensionsUsed"].size(); i++) used = used || j["extensionsUsed"][i].str == "BLENDITY_toon";
    CHECK(used);
    JsonParser p{nullptr, nullptr};
    const std::string mj = gltf_material_json(*toon);
    p = JsonParser{mj.data(), mj.data() + mj.size()};
    const Json m = p.value();
    CHECK(p.ok && m["name"].str == "Cel" && m["extensions"]["BLENDITY_toon"].type == Json::Obj);
  });
}

static void livelink_fixture_tests() {
  test("livelink: the recorded session (tests/fixtures/livelink_stream.bin, the bridges' smoke tests replay it) is what the link sends now", [] {
    const Session s = record_session();
    const std::vector<std::string> now = outline(s.ms);
    CHECK(now.size() > 20);
    std::printf("    %zu messages, %zu bytes; types:", s.ms.size(), s.raw.size());
    for (const char *t : {"object", "mesh", "material", "light", "camera", "transform", "deform", "pose", "delete", "play"})
      std::printf(" %s %d", t, count_of(s.ms, t));
    std::printf("\n");
    for (const char *t : {"transform", "deform", "pose", "delete"}) CHECK(count_of(s.ms, t) == 1);
    CHECK(count_of(s.ms, "play") == 2 && count_of(s.ms, "material") >= 3);
    const std::string path = "tests/fixtures/livelink_stream.bin";
    if (std::getenv("BLENDITY_LIVELINK_RECORD")) {
      CHECK(fs::write_file(path, s.raw));
      std::printf("    wrote %s\n", path.c_str());
      return;
    }
    if (!fs::is_dir("tests/fixtures")) {
      std::printf("    (not run from the source folder: the recording isn't compared)\n");
      return;
    }
    std::string raw;
    CHECK(fs::read_file(path, raw));
    const std::vector<std::string> was = outline(parse_stream(raw));
    CHECK(was == now);
    for (size_t i = 0; i < std::min(was.size(), now.size()); i++)
      if (was[i] != now[i]) {
        std::printf("    first difference at %zu: recorded '%s', now '%s' (BLENDITY_LIVELINK_RECORD=1 records it again)\n", i, was[i].c_str(), now[i].c_str());
        break;
      }
  });
}

void run_livelink_tests() {
  livelink_tests();
  livelink_fixture_tests();
}

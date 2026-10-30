// SPDX-License-Identifier: GPL-2.0-or-later
// blendity_tests: Live Link (task 0172, ADR 0055) - the server, the protocol, the editor's link - with a real client
// on 127.0.0.1, as a bridge would connect.
#include "test_common.h"
#include "../src/core/json.h"
#include "../src/core/net_server.h"
/* The Unreal plugin's protocol layer (task 0277): standard C++ only, compiled here so every build replays it. */
#include "../bridges/unreal/BlendityLiveLink/Source/BlendityLiveLink/Private/BlendityLinkProtocol.cpp"

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

static void tests_round267() {
  test("export: a Hand-painted material and an unlit one are written KHR_materials_unlit, listed in extensionsUsed; a PBR material is not", [] {
    auto mesh = primitives::cube(1.0f);
    MaterialPtr hp = make_material("Painted", {0.9f, 0.3f, 0.3f});
    hp->shading_model = 2;
    MaterialPtr pbr = make_material("Plain", {0.5f, 0.5f, 0.5f});
    MaterialPtr un = make_material("Unlit", {0.5f, 0.5f, 0.5f});
    un->unlit = true;
    ExportOptions o = export_defaults((int)ExportFormat::GLB);
    const std::string glb = export_gltf({ExportItem{"Painted", mesh.get(), Mat4::identity(), {hp}}, ExportItem{"Plain", mesh.get(), Mat4::identity(), {pbr}}, ExportItem{"Unlit", mesh.get(), Mat4::identity(), {un}}}, o, true, "", nullptr);  // (one slot a cube: three materials)
    const Json j = glb_json(glb);
    CHECK(j.type == Json::Obj && j["materials"].size() == 3);
    const bool first_unlit = j["materials"][0]["extensions"]["KHR_materials_unlit"].type == Json::Obj;
    const bool second_unlit = j["materials"][1]["extensions"]["KHR_materials_unlit"].type == Json::Obj;
    const bool third_unlit = j["materials"][2]["extensions"]["KHR_materials_unlit"].type == Json::Obj;
    bool used = false;
    for (size_t i = 0; i < j["extensionsUsed"].size(); i++) used = used || j["extensionsUsed"][i].str == "KHR_materials_unlit";
    std::printf("    glTF: Hand-painted unlit %d, PBR unlit %d, an unlit material %d, extensionsUsed lists it %d\n", (int)first_unlit, (int)second_unlit, (int)third_unlit, (int)used);
    CHECK(first_unlit && !second_unlit && third_unlit && used);
  });
}

static void tests_round294() {
  test("livelink discovery (task 0274): Start writes livelink.json beside the prefs with the protocol, the port and this process; a taken 40100 moves to the next port, said; a client's hello names it; Stop and the editor's end remove the file; a headless editor never starts by itself", [] {
    set_env("BLENDITY_PREFS_DIR", test_dir() + "/prefs294");  // (never the user's own ~/.blendity: a running Blendity's file)
    std::string path, text;
    {
      Editor ed;
      ed.init_headless(800, 600);
      path = ed.livelink_discovery_path();
      fs::remove_file(path);
      ed.livelink_autostart();
      const bool idle = !ed.livelink_for_test() || !ed.livelink_for_test()->running();
      std::printf("    headless, the preference on %d: started by itself %d\n", (int)ed.livelink_autostart_pref_for_test(), (int)!idle);
      CHECK(idle && ed.livelink_autostart_pref_for_test());
      /* 40100 taken by another server: the next one. */
      net::Server blocker;
      const bool blocked = blocker.start(livelink::kDefaultPort);
      CHECK(ed.livelink_start(livelink::kDefaultPort));
      const int port = ed.livelink_for_test()->port();
      CHECK(fs::read_file(path, text));
      JsonParser p{text.data(), text.data() + text.size()};
      const Json j = p.value();
      std::printf("    40100 held by another %d: listening on %d ('%s'); the file: protocol '%s', port %d, app '%s', scene '%s'\n", (int)blocked, port,
                  ed.livelink_note_for_test().c_str(), j["protocol"].str.c_str(), j["port"].integer(), j["app"].str.c_str(), j["scene"].str.c_str());
      CHECK(j["protocol"].str == livelink::kProtocol && j["port"].integer() == port && j["pid"].integer() > 0 && j["app"].str == "Blendity");
      CHECK(!blocked || (port == livelink::kDefaultPort + 1 && !ed.livelink_note_for_test().empty()));
      /* A bridge says who it is. */
      net::LineClient c;
      CHECK(c.connect(port));
      CHECK(c.send("{\"t\":\"client\",\"app\":\"Unity 6000.0\",\"bridge\":\"1.1\"}"));
      std::vector<std::string> names;
      for (int i = 0; i < 100; i++) {
        ed.step_frame_headless();
        std::string line, bin;
        while (c.read(line, bin, 5)) {}
        names = ed.livelink_for_test()->client_names();
        if (!names.empty() && names[0] != "a client") break;
      }
      std::printf("    a client's hello: %s\n", names.empty() ? "(none)" : names[0].c_str());
      CHECK(names.size() == 1 && names[0] == "Unity 6000.0 (bridge 1.1)");
      c.close();
      ed.livelink_stop();
      std::printf("    after Stop: the file %d\n", (int)fs::exists(path));
      CHECK(!fs::exists(path));
      blocker.stop();
      CHECK(ed.livelink_start(0));
      CHECK(fs::exists(path));
    }
    std::printf("    after the editor's end: the file %d\n", (int)fs::exists(path));
    CHECK(!fs::exists(path));
    CHECK(path.find("prefs294") != std::string::npos);
    set_env("BLENDITY_PREFS_DIR", "");
  });
  test("livelink discovery (task 0274): another Blendity's file is left alone - Stop removes only this process's", [] {
    set_env("BLENDITY_PREFS_DIR", test_dir() + "/prefs294");
    Editor ed;
    ed.init_headless(800, 600);
    const std::string path = ed.livelink_discovery_path();
    CHECK(ed.livelink_start(0));
    const std::string other = "{\"protocol\":\"blendity-livelink/1\",\"port\":40107,\"pid\":1,\"app\":\"Blendity\"}\n";
    CHECK(fs::write_file(path, other));  // (another Blendity started since and wrote its own)
    ed.livelink_stop();
    std::string text;
    const bool kept = fs::read_file(path, text) && text == other;
    std::printf("    the other's file kept after Stop %d\n", (int)kept);
    CHECK(kept && path.find("prefs294") != std::string::npos);
    fs::remove_file(path);
    set_env("BLENDITY_PREFS_DIR", "");
  });
}

/* q (x, y, z, w) turning v: v + 2w (q x v) + 2 q x (q x v). */
static blendity_link::Vec3 quat_turn(const blendity_link::Quat &q, const blendity_link::Vec3 &v) {
  const double tx = 2 * (q.y * v.z - q.z * v.y), ty = 2 * (q.z * v.x - q.x * v.z), tz = 2 * (q.x * v.y - q.y * v.x);
  return {v.x + q.w * tx + (q.y * tz - q.z * ty), v.y + q.w * ty + (q.z * tx - q.x * tz), v.z + q.w * tz + (q.x * ty - q.y * tx)};
}

static void tests_round297() {
  test("the Unreal plugin's protocol (task 0277): the recorded session framed whole by its own reader, every mesh read with its _VERT in range, the normals worked out again agreeing with the file's, a deform applied; the axes a mirror that keeps turns turning the same way; the discovery file and the hello", [] {
    namespace ul = blendity_link;
    const Session s = record_session();
    ul::Stream st;
    st.feed((const uint8_t *)s.raw.data(), s.raw.size() / 2);  // (in two pieces: a message cut in the middle waits)
    st.feed((const uint8_t *)s.raw.data() + s.raw.size() / 2, s.raw.size() - s.raw.size() / 2);
    ul::Message m;
    int n = 0, meshes = 0, bad_vert = 0, deforms = 0, deformed = 0;
    size_t agree = 0, total = 0;
    std::map<int64_t, ul::GlbMesh> got;
    ul::Vec3 box{};
    std::string box_name;
    std::map<int64_t, std::string> names;
    while (st.next(m)) {
      n++;
      if (m.type == "object") names[m.json["id"].id()] = m.json["name"].string();
      if (m.type == "transform" && names[m.json["id"].id()] == "Box") box = ul::position_to_unreal(m.json["position"]), box_name = "Box";
      if (m.type == "deform") {
        deforms++;
        auto it = got.find(m.json["id"].id());
        deformed += it != got.end() && ul::apply_deform(it->second, m.bin.data(), m.bin.size());
      }
      if (m.type != "mesh") continue;
      ul::GlbMesh g;
      std::string err;
      CHECK(ul::read_glb(m.bin.data(), m.bin.size(), g, &err));
      meshes++;
      const int verts = (int)m.json["verts"].number(0);
      for (int v : g.vertex_of) bad_vert += v < 0 || v >= verts;
      /* The mesh's own positions back as a deform (glTF's axes, per mesh vertex): the normals worked out again. */
      std::vector<float> pos((size_t)verts * 3, 0.0f);
      for (size_t i = 0; i < g.vertex_of.size(); i++) {
        const ul::Vec3 &p = g.positions[i];
        float *o = &pos[(size_t)g.vertex_of[i] * 3];
        o[0] = (float)(-p.y / 100.0), o[1] = (float)(p.z / 100.0), o[2] = (float)(p.x / 100.0);
      }
      const std::vector<ul::Vec3> file_normals = g.normals;
      ul::GlbMesh again = g;
      CHECK(ul::apply_deform(again, (const uint8_t *)pos.data(), pos.size() * 4));
      for (size_t i = 0; i < file_normals.size(); i++) {
        const ul::Vec3 &a = file_normals[i], &b = again.normals[i];
        agree += a.x * b.x + a.y * b.y + a.z * b.z > 0.5;
        total++;
      }
      got[m.json["id"].id()] = g;
    }
    std::printf("    %d messages framed, %zu bytes left; %d meshes, %d vertices outside their mesh; normals worked out again agree at %zu of %zu; %d of %d deforms applied\n",
                n, st.pending(), meshes, bad_vert, agree, total, deformed, deforms);
    CHECK(n > 20 && st.pending() == 0 && meshes > 0 && bad_vert == 0 && agree * 10 >= total * 9 && deforms == 1 && deformed == 1);
    /* The Box moved to Blendity's (1, 2, 3): glTF (-1, 2, 3), Unreal (300 cm forward, 100 cm right, 200 cm up). */
    std::printf("    the Box in Unreal: (%.1f, %.1f, %.1f) cm\n", box.x, box.y, box.z);
    CHECK(box_name == "Box" && std::fabs(box.x - 300) < 1e-6 && std::fabs(box.y - 100) < 1e-6 && std::fabs(box.z - 200) < 1e-6);
    /* A turn maps with the points: turning a mapped vector is mapping the turned one. */
    ul::Json q;
    const std::string qt = "[0.2,0.5,-0.3,0.787]";
    ul::parse_json(qt.data(), qt.size(), q);
    const double len = std::sqrt(0.04 + 0.25 + 0.09 + 0.787 * 0.787);
    const ul::Quat g{0.2 / len, 0.5 / len, -0.3 / len, 0.787 / len};
    const ul::Quat u = ul::rotation_to_unreal(q);
    const ul::Quat un{u.x / len, u.y / len, u.z / len, u.w / len};
    const ul::Vec3 v{0.3, -0.7, 0.4};
    const ul::Vec3 turned = quat_turn(g, v);
    const ul::Vec3 a = quat_turn(un, ul::direction_to_unreal(v.x, v.y, v.z)), b = ul::direction_to_unreal(turned.x, turned.y, turned.z);
    std::printf("    a turn: mapped then turned (%.4f, %.4f, %.4f), turned then mapped (%.4f, %.4f, %.4f)\n", a.x, a.y, a.z, b.x, b.y, b.z);
    CHECK(std::fabs(a.x - b.x) < 1e-9 && std::fabs(a.y - b.y) < 1e-9 && std::fabs(a.z - b.z) < 1e-9);
    /* The discovery file and the hello. */
    const std::string dir = test_dir() + "/ue297";
    fs::make_dirs(dir);
    const std::string file = dir + "/livelink.json";
    ul::Endpoint ep;
    CHECK(fs::write_file(file, "{\"protocol\":\"blendity-livelink/1\",\"port\":40103,\"pid\":7,\"app\":\"Blendity\",\"scene\":\"Yard\"}\n"));
    CHECK(ul::read_discovery(file, ep) && ep.port == 40103 && ep.scene == "Yard");
    CHECK(fs::write_file(file, "{\"protocol\":\"another/2\",\"port\":40103}"));
    CHECK(!ul::read_discovery(file, ep));
    const std::string h = ul::hello("Unreal 5.4 \"Editor\"");
    ul::Json hj;
    CHECK(ul::parse_json(h.data(), h.size() - 1, hj) && hj["t"].string() == "client" && hj["app"].string() == "Unreal 5.4 \"Editor\"" && hj["bridge"].string() == "1.1");
  });
  test("the Unreal plugin's protocol (round 317): a JSON array indexed by a literal, a negative number and any integer type - as the plugin's applier does (B[0] didn't compile in Unreal 5.8)", [] {
    namespace ul = blendity_link;
    const std::string s = "{\"bones\":[[7,1,2],[8]]}";
    ul::Json j;
    CHECK(ul::parse_json(s.data(), s.size(), j));
    const ul::Json &b = j["bones"][0];
    const size_t two = 2;
    const int64_t one64 = 1;
    const uint32_t zero32 = 0;
    const long one_l = 1;
    CHECK(b[0].id() == 7 && b[two].number() == 2 && b[one64].number() == 1 && b[zero32].id() == 7 && b[one_l].number() == 1);
    CHECK(b[-1].type == ul::Json::Null && b[-1].number(-7) == -7);  // (a negative index is past the end)
    CHECK(b[3].type == ul::Json::Null && j["bones"][1][0].id() == 8 && j["bones"][2].size() == 0);
  });
}

static platform::Event ll_mouse(platform::EventType t, int x, int y) {
  platform::Event e;
  e.type = t, e.x = x, e.y = y;
  return e;
}

static bool json_ok(const std::string &text) {
  JsonParser p{text.data(), text.data() + text.size()};
  p.value();
  return p.ok;
}

static std::string last_toast(Editor &ed) {
  ed.step_frame_headless();  // (a refusal becomes a toast on the frame after)
  const auto &t = ed.toasts_for_test();
  return t.empty() ? std::string() : t.front().text;  // (newest first)
}

static std::string all_toasts(const Editor &ed) {
  std::string s;
  for (const auto &t : ed.toasts_for_test()) s += " '" + t.text.substr(0, 90) + "'";
  return s;
}

static void tests_round298() {
  test("Install a Bridge (task 0278): a Unity project gets the package in Packages; a Godot project the add-on and its line in project.godot (beside another plugin, or a new section); an Unreal project the plugin in Plugins and its entry in the .uproject (a list there or not), still JSON; a folder that is no project refused; the window's buttons drawn, an engine's asking for a folder", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    if (ed.bridges_dir_for_test().empty()) {
      std::printf("    (no bridges folder beside the program or here: not run from the source folder)\n");
      return;
    }
    const std::string root = test_dir() + "/bridges298";
    fs::make_dirs(root);
    /* Unity. */
    const std::string unity = root + "/UnityGame";
    fs::make_dirs(unity + "/Assets"), fs::make_dirs(unity + "/ProjectSettings");
    ed.command("livelink install unity " + unity);
    const bool upkg = fs::exists(unity + "/Packages/com.blendity.livelink/package.json") && fs::exists(unity + "/Packages/com.blendity.livelink/Editor/LiveLinkClient.cs.meta");
    std::printf("    Unity: the package in Packages with its .meta files %d; '%s'\n", (int)upkg, last_toast(ed).c_str());
    CHECK(upkg);
    ed.command("livelink install unity " + root + "/NotAProject");
    std::printf("    a folder that isn't one: '%s'\n", last_toast(ed).c_str());
    CHECK(last_toast(ed).find("isn't a Unity project") != std::string::npos);
    /* Godot, beside another plugin and without the section. */
    const std::string godot = root + "/GodotGame", godot2 = root + "/GodotBare";
    fs::make_dirs(godot), fs::make_dirs(godot2);
    CHECK(fs::write_file(godot + "/project.godot", "config_version=5\n\n[application]\n\nconfig/name=\"G\"\n\n[editor_plugins]\n\nenabled=PackedStringArray(\"res://addons/other/plugin.cfg\")\n\n[rendering]\n\nx=1\n"));
    CHECK(fs::write_file(godot2 + "/project.godot", "config_version=5\n"));
    ed.command("livelink install godot " + godot);
    ed.command("livelink install godot " + godot2);
    std::string g1, g2;
    fs::read_file(godot + "/project.godot", g1), fs::read_file(godot2 + "/project.godot", g2);
    const std::string mine = "\"res://addons/blendity_livelink/plugin.cfg\"";
    const bool both = g1.find("enabled=PackedStringArray(" + mine + ", \"res://addons/other/plugin.cfg\")") != std::string::npos && g1.find("[rendering]") != std::string::npos;
    const bool added = g2.find("[editor_plugins]") != std::string::npos && g2.find("enabled=PackedStringArray(" + mine + ")") != std::string::npos;
    std::printf("    Godot: the add-on copied %d; enabled beside another %d; a new section %d\n", (int)fs::exists(godot + "/addons/blendity_livelink/plugin.cfg"), (int)both, (int)added);
    CHECK(fs::exists(godot + "/addons/blendity_livelink/plugin.gd") && both && added);
    const std::string godot3 = root + "/GodotEnd";  // ([editor_plugins] the last line, no newline after it)
    fs::make_dirs(godot3);
    CHECK(fs::write_file(godot3 + "/project.godot", "config_version=5\n\n[editor_plugins]"));
    ed.command("livelink install godot " + godot3);
    std::string g3;
    fs::read_file(godot3 + "/project.godot", g3);
    std::printf("    Godot, the section last with no newline: config first %d, enabled after the section %d\n", (int)(g3.rfind("config_version", 0) == 0),
                (int)(g3.find("enabled=") > g3.find("[editor_plugins]")));
    CHECK(g3.rfind("config_version", 0) == 0 && g3.find("enabled=") != std::string::npos && g3.find("enabled=") > g3.find("[editor_plugins]"));
    ed.command("livelink install godot " + godot);  // (again: not added twice)
    fs::read_file(godot + "/project.godot", g1);
    CHECK(g1.find(mine) == g1.rfind(mine));
    /* Unreal, with a Plugins list and without. */
    const std::string ue = root + "/UEGame", ue2 = root + "/UEBare";
    fs::make_dirs(ue), fs::make_dirs(ue2);
    CHECK(fs::write_file(ue + "/UEGame.uproject", "{\n\t\"FileVersion\": 3,\n\t\"Plugins\": [\n\t\t{\n\t\t\t\"Name\": \"ModelingToolsEditorMode\",\n\t\t\t\"Enabled\": true\n\t\t}\n\t]\n}\n"));
    CHECK(fs::write_file(ue2 + "/UEBare.uproject", "{\n\t\"FileVersion\": 3,\n\t\"EngineAssociation\": \"5.4\"\n}\n"));
    ed.command("livelink install unreal " + ue);
    ed.command("livelink install unreal " + ue2);
    std::string u1, u2;
    fs::read_file(ue + "/UEGame.uproject", u1), fs::read_file(ue2 + "/UEBare.uproject", u2);
    const bool listed = u1.find("\"BlendityLiveLink\"") != std::string::npos && u1.find("ModelingToolsEditorMode") != std::string::npos && json_ok(u1);
    const bool fresh = u2.find("\"BlendityLiveLink\"") != std::string::npos && json_ok(u2);
    std::printf("    Unreal: the plugin copied %d (its replay session %d); in a Plugins list %d; a new list %d (both still JSON)\n",
                (int)fs::exists(ue + "/Plugins/BlendityLiveLink/BlendityLiveLink.uplugin"), (int)fs::exists(ue + "/Plugins/BlendityLiveLink/Resources/livelink_stream.bin"), (int)listed, (int)fresh);
    CHECK(fs::exists(ue + "/Plugins/BlendityLiveLink/Source/BlendityLiveLink/Private/BlendityLinkProtocol.cpp") && listed && fresh);
    const std::string ue3 = root + "/UEOff";  // (listed but off: turned on)
    fs::make_dirs(ue3);
    CHECK(fs::write_file(ue3 + "/UEOff.uproject", "{\"Plugins\": [{\"Name\": \"BlendityLiveLink\", \"Enabled\": false}, {\"Name\": \"Other\", \"Enabled\": false}]}"));
    ed.command("livelink install unreal " + ue3);
    std::string u3;
    fs::read_file(ue3 + "/UEOff.uproject", u3);
    std::printf("    Unreal, listed but off: %s\n", u3.c_str());
    CHECK(u3 == "{\"Plugins\": [{\"Name\": \"BlendityLiveLink\", \"Enabled\": true}, {\"Name\": \"Other\", \"Enabled\": false}]}");
    /* The window: four buttons; an engine's asks for a folder (no dialog here: said). */
    ed.record_buttons_for_test(true);
    ed.command("window Live Link");
    ed.clear_toasts_for_test();  // (the installs' toasts sit over the bottom of the dock, where the window opens)
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    Recti r;
    const bool drawn = ed.button_rect_for_test("livelink:install blender", r) && ed.button_rect_for_test("livelink:install unreal", r);
    if (ed.button_rect_for_test("livelink:install godot", r)) {
      std::printf("    Godot Project... at %d,%d %dx%d\n", r.x, r.y, r.w, r.h);
      for (platform::EventType t : {platform::EventType::MouseMove, platform::EventType::MouseDown, platform::EventType::MouseUp}) ed.step_frame_headless({ll_mouse(t, r.x + r.w / 2, r.y + r.h / 2)});
      for (int i = 0; i < 2; i++) ed.step_frame_headless();
    }
    std::printf("    the window's buttons drawn %d; Godot Project... here: '%s'\n", (int)drawn, last_toast(ed).c_str());
    CHECK(drawn && last_toast(ed).find("no file dialog here") != std::string::npos);
  });
  test("Install a Bridge (task 0278): Blender - the add-on copied into its extensions and enabled by Blender in the background (BLENDITY_TEST_BLENDER names a Blender; skipped without one)", [] {
    const char *blender = std::getenv("BLENDITY_TEST_BLENDER");
    if (!blender || !*blender) {
      std::printf("    (BLENDITY_TEST_BLENDER not set: skipped)\n");
      return;
    }
    const std::string user = test_dir() + "/blender298";
    fs::make_dirs(user);
    set_env("BLENDITY_BLENDER", blender);
    set_env("BLENDER_USER_RESOURCES", user);
    Editor ed;
    ed.init_headless(800, 600);
    std::string version;
    ed.step_frame_headless();  // (what is logged before the first frame is history to the toasts)
    std::printf("    Blender: %s\n", ed.find_blender_for_test(version).c_str());
    ed.command("livelink install blender");
    ed.command("livelink install blender");  // (again at once: refused while the first runs)
    const std::string again = last_toast(ed);
    for (int i = 0; i < 1200 && ed.bridge_job_running_for_test(); i++) {
      ed.bridge_job_poll_for_test();
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    const bool copied = fs::exists(user + "/extensions/user_default/blendity_livelink/__init__.py");
    std::printf("    a second install at once: '%s'\n", again.c_str());
    CHECK(again.find("still enabling") != std::string::npos);
    const std::string said = last_toast(ed);
    std::printf("    copied %d; the toasts:%s; its preferences saved %d\n", (int)copied, all_toasts(ed).c_str(), (int)fs::exists(user + "/config/userpref.blend"));
    CHECK(copied && said.find("is on in") != std::string::npos && fs::exists(user + "/config/userpref.blend"));
    set_env("BLENDITY_BLENDER", "");
    set_env("BLENDER_USER_RESOURCES", "");
  });
}

/* Round 318: a real bridge in its engine follows a live session - the engine's own live-session test, run by the command
 * BLENDITY_TEST_BRIDGE names (a live-session test in each of bridges/blender, godot, unity and unreal), connects by itself through the
 * discovery file and writes BLENDITY_BRIDGE_DUMP.ready once it has the Box; then the edits arrive as changes, not as a
 * first sync. When Live Link stops (the discovery file goes) the bridge checks its scene and writes its report. */
static void tests_round318() {
  test("Live Link with a real bridge (round 318): the engine connects by itself, then follows a Box moved, recolored and deformed, a bone turned, the Sphere deleted, an object made and a dense mesh deformed ten times (BLENDITY_TEST_BRIDGE runs one; skipped without)", [] {
    const char *cmd = std::getenv("BLENDITY_TEST_BRIDGE");
    if (!cmd || !*cmd) {
      std::printf("    skipped: BLENDITY_TEST_BRIDGE names no bridge command\n");
      return;
    }
    const std::string dump = test_dir() + "/bridge_live.txt", ready = dump + ".ready";
    fs::remove_file(dump);
    fs::remove_file(ready);
    /* The discovery file in the test's own folder, never the artist's ~/.blendity; both cleared however the test ends. */
    struct Env {  // (the suite's own BLENDITY_PREFS_DIR, if it was given one, comes back)
      std::string prefs = std::getenv("BLENDITY_PREFS_DIR") ? std::getenv("BLENDITY_PREFS_DIR") : "";
      ~Env() {
        set_env("BLENDITY_BRIDGE_DUMP", "");
        set_env("BLENDITY_PREFS_DIR", prefs);
      }
    } env;
    fs::make_dirs(test_dir() + "/prefs318");
    set_env("BLENDITY_PREFS_DIR", test_dir() + "/prefs318");
    set_env("BLENDITY_BRIDGE_DUMP", dump);
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
    ed.command("create Sphere");
    GameObject *dense = ed.active_object_for_test();
    const bool started = box && dense && ed.livelink_start(0);
    CHECK(started && fs::exists(test_dir() + "/prefs318/livelink.json"));
    if (!started) return;
    dense->name = "Dense";
    dense->set_local_position(Vec3(0, 0, 8));
    dense->mesh_filter()->mesh = primitives::uv_sphere(1.0f, 256, 128);  // (about 33,000 vertices: the deform cost)
    const size_t dense_verts = dense->mesh_filter()->mesh->vert_count();
    Material &mat = *box->mesh_renderer()->materials[0];
    mat.name = "Cel";
    /* The engine on a thread; if it never ends, the test says so and leaves it rather than hang. */
    auto bridge_rc = std::make_shared<std::atomic<int>>(-1);
    auto done = std::make_shared<std::atomic<bool>>(false);
    const std::string command = cmd;
    std::thread engine([bridge_rc, done, command] {
      bridge_rc->store(std::system(command.c_str()));
      done->store(true);
    });
    auto run = [&](double seconds) {
      const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds((int)(seconds * 1000));
      while (std::chrono::steady_clock::now() < end) {
        ed.step_frame_headless();
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
      }
    };
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 2400 && !fs::exists(ready) && !done->load(); i++) run(0.25);  // (up to 10 minutes: Unity and Unreal start slowly)
    const double waited = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("    the bridge ready after %.1f s: %d; clients:", waited, (int)fs::exists(ready));
    for (const std::string &n : ed.livelink_for_test()->client_names()) std::printf(" '%s'", n.c_str());
    std::printf("\n");
    CHECK(fs::exists(ready));
    if (fs::exists(ready)) {
      box->set_local_position(Vec3(1, 2, 3));
      run(0.5);
      mat.base_color = Vec3(0.9f, 0.4f, 0.2f);
      mat.touch();
      run(0.5);
      MeshPtr m = std::make_shared<Mesh>(*box->mesh_filter()->mesh);
      m->positions[0] = m->positions[0] + Vec3(0.25f, 0, 0);
      m->touch();
      box->mesh_filter()->mesh = m;
      run(0.5);
      lower->set_local_rotation(Quat::euler(Vec3(30.0f, 0, 0)));  // (degrees)
      run(0.5);
      if (GameObject *sphere = ed.scene().find_by_name("Sphere")) ed.scene().destroy(sphere);
      run(0.5);
      GameObject *late = make_cube(ed, "Late");
      if (late) late->set_local_position(Vec3(0, 5, 0));
      run(1.5);
      const MeshPtr base = dense->mesh_filter()->mesh;
      for (int k = 1; k <= 10; k++) {  // (every vertex moves: a deform of the whole dense mesh each time, the last at radius 1.2)
        MeshPtr d = std::make_shared<Mesh>(*base);
        for (Vec3 &p : d->positions) p = p * (1.0f + 0.02f * (float)k);
        d->touch();
        dense->mesh_filter()->mesh = d;
        run(0.4);
      }
      run(2.0);
    }
    const livelink::Stats st = ed.livelink_for_test()->stats();
    std::printf("    sent: %llu objects, %llu transforms, %llu meshes, %llu deforms, %llu materials, %llu deletes, %llu poses; the dense mesh %zu vertices\n",
                (unsigned long long)st.objects, (unsigned long long)st.transforms, (unsigned long long)st.meshes, (unsigned long long)st.deforms,
                (unsigned long long)st.materials, (unsigned long long)st.deletes, (unsigned long long)st.poses, dense_verts);
    ed.livelink_stop();  // (the discovery file goes: the bridge checks its scene and writes its report)
    for (int i = 0; i < 1200 && !done->load(); i++) run(0.1);  // (the engine writes its report and quits: 2 minutes at most)
    const bool ended = done->load();
    if (ended) engine.join();
    else engine.detach();
    std::string report;
    fs::read_file(dump, report);
    std::printf("    the bridge's command %s %d; its report:\n%s", ended ? "exited" : "still running after 2 minutes; last code", bridge_rc->load(), report.c_str());
    CHECK(ended && bridge_rc->load() == 0 && report.find("LIVE OK") != std::string::npos && report.find("FAILED") == std::string::npos);
  });
}

void run_livelink_tests() {
  livelink_tests();
  livelink_fixture_tests();
  tests_round267();
  tests_round294();
  tests_round297();
  tests_round298();
  tests_round318();
}

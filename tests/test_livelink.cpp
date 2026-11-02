// SPDX-License-Identifier: GPL-2.0-or-later
// blendity_tests: Live Link (task 0172, ADR 0055) - the server, the protocol, the editor's link - with a real client
// on 127.0.0.1, as a bridge would connect.
#include "test_common.h"
#include "../src/scene/snapshot.h"
#include "../src/scene/animation.h"
#include "../src/scene/spring_bones.h"
#include "../src/scene/visemes.h"
#include "../src/render/canvas.h"
#include "../src/image/image.h"
#include <chrono>
#include <thread>
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

/* Round 381: LeftEye and RightEye bones 6 cm apart at `at` under the armature `root` (their rests set where they stand). */
void add_eyes(Editor &ed, GameObject &root, Vec3 at) {
  for (int side = 0; side < 2; side++) {
    GameObject *eye = ed.scene().create(side ? "RightEye" : "LeftEye", &root);
    eye->set_local_position(at + Vec3(side ? -0.03f : 0.03f, 0, 0));
    eye->add<Bone>()->set_rest(eye->local());
  }
}
/* A small cube skinned (to nothing yet) carrying VRChat's 15 visemes and a left blink, all at 0. */
GameObject *add_face(Editor &ed, const char *name, Vec3 at) {
  GameObject *face = make_cube(ed, name);
  if (!face) return nullptr;
  face->set_local_position(at);
  face->set_local_scale(Vec3(0.1f, 0.1f, 0.1f));
  face->add<SkinModifier>();
  Mesh &m = *mesh_make_mutable(face->get<MeshFilter>()->mesh);
  std::vector<std::string> keys = vrchat_viseme_names();
  keys.push_back("vrc.blink_left");
  for (const std::string &k : keys) {
    ShapeKey sk;
    sk.name = k;
    sk.delta.assign(m.vert_count(), Vec3(0, 0.01f, 0));
    m.shape_keys.push_back(sk);
  }
  m.touch();
  return face;
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
  root->set_local_scale(Vec3(2, 2, 2));  // (review 378: a scaled rig - an FBX armature at 0.01 - skins as Blendity does)
  GameObject *upper = ed.scene().create("Upper", root);
  upper->add<Bone>();
  GameObject *lower = ed.scene().create("Lower", upper);
  lower->add<Bone>();
  lower->set_local_position(Vec3(0, 1, 0));
  /* Round 378: a skinned cylinder on the rig with a "Smile" shape key: it goes at rest with its skin and keys. */
  GameObject *arm = ed.scene().create("Arm");
  arm->add<MeshFilter>()->mesh = primitives::cylinder(0.2f, 2.0f, 8);
  arm->add<MeshRenderer>();
  arm->set_local_position(Vec3(3, 0, 0));
  SkinModifier *skin = arm->add<SkinModifier>();
  skin->object = "Rig";
  Mesh &am = *mesh_make_mutable(arm->get<MeshFilter>()->mesh);
  skin_bind_automatic(*skin, am);
  ShapeKey smile;
  smile.name = "Smile";
  smile.delta.assign(am.vert_count(), Vec3(0.0f, 0.0f, 0.1f));
  am.shape_keys.push_back(smile);
  am.touch();
  arm->get<MeshRenderer>()->set_shape_weight("Smile", 0.25f);
  /* Round 379: a clip on the rig - "Wave" turns Upper 60 degrees about Z over 60 frames - sent baked at the sync; at the
   * end the rig is posed at its frame 30, which the bridges sample their own clip against. */
  AnimationClipPtr wave = create_clip_asset("Assets/Animations/livelink_wave.anim", "Wave");
  if (wave) {
    wave->track("Upper", "Rotation", 2).set_key(0.0f, 0.0f);
    wave->track("Upper", "Rotation", 2).set_key(60.0f, 60.0f);
    wave->track("Upper", "Position", 1).set_key(0.0f, 0.0f);
    wave->track("Upper", "Position", 1).set_key(60.0f, 0.5f);
    wave->length = 60.0f;
    wave->touch();
    Animation *an = root->add<Animation>();
    an->clip = "Assets/Animations/livelink_wave.anim";
    an->play_automatically = false;
  }
  /* Round 381: the rig an avatar's - eyes 6 cm apart at 1.5 m (their rests; the view between them), a Face skinned to it
   * with VRChat's 15 visemes and a blink (all at 0); the Box static (Contribute GI). */
  add_eyes(ed, *root, Vec3(0, 1.5f, 0.05f));
  if (GameObject *face = add_face(ed, "Face", Vec3(0, 1.5f, 0))) face->get<SkinModifier>()->object = "Rig";
  if (box) box->mesh_renderer()->contribute_gi = true;
  /* Round 380: a crate with an LOD Group (three levels) and a Box Collider off its centre, 1 x 2 x 1. */
  if (GameObject *crate = make_cube(ed, "Crate")) {
    crate->set_local_position(Vec3(0, 0, -3));
    crate->add<LODGroupComponent>();
    ColliderComponent *col = crate->add<ColliderComponent>();
    col->fit = false, col->center = Vec3(0.1f, 0, 0), col->size = Vec3(1, 2, 1);
  }
  if (!box || !ed.livelink_start(0)) return s;
  Material &mat = *box->mesh_renderer()->materials[0];
  mat.name = "Cel";
  mat.shading_model = 1;
  mat.toon_bands = 2;
  /* Round 377: a 4x4 checker as the Base Map, sent as PNG bytes (the bridges load it from them, not the path). */
  std::vector<uint32_t> tile(16);
  for (int i = 0; i < 16; i++) tile[(size_t)i] = ((i / 4 + i % 4) % 2) ? 0xFFFFFFFFu : 0xFF2040C0u;  // (white and a blue: 0xAARRGGBB)
  fs::make_dirs(fs::parent(resolve_asset_path("Assets/Textures/livelink_tile.png")));  // (a run of this test alone has no such folder yet)
  write_png(resolve_asset_path("Assets/Textures/livelink_tile.png"), tile.data(), 4, 4, 4);
  texture_cache_forget(resolve_asset_path("Assets/Textures/livelink_tile.png"));
  mat.base_map = TextureRef{"Assets/Textures/livelink_tile.png", false};
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
  /* Round 376: the look - a gradient sky with fog from 5 to 40 m, half a stop up, Bloom and a Vignette on the camera. */
  EnvironmentSettings &env = ed.scene().environment;
  env.mode = 0, env.sky = Vec3(0.2f, 0.4f, 0.8f), env.fog = true, env.fog_start = 5.0f, env.fog_end = 40.0f, env.fog_color = Vec3(0.7f, 0.6f, 0.5f);
  ed.scene().render.exposure = 0.5f;
  if (GameObject *cam = ed.scene().find_by_name("Main Camera")) {
    CameraFilters *f = cam->get<CameraFilters>() ? cam->get<CameraFilters>() : cam->add<CameraFilters>();
    if (BloomEffect *b = static_cast<BloomEffect *>(f->add("Bloom"))) b->intensity = 0.8f;
    if (VignetteEffect *v = static_cast<VignetteEffect *>(f->add("Vignette"))) v->intensity = 0.3f;
  }
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
  GameObject *rig = ed.scene().find_by_name("Rig");  // (Stop put the scene back: the objects are new)
  if (wave && rig) apply_clip(*wave, *rig, 30.0f);  // (round 379: the pose the bridges' clips must give at frame 30)
  take(drain(ed, c));
  /* Round 381: a spring chain on Lower (after Play, which would have swung it): VRChat's PhysBone in Unity. */
  if (GameObject *lw = ed.scene().find_by_name("Lower")) lw->add<SpringBone>()->stiffness = 0.4f;
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));  // (an armature's setup is looked at once a second)
  take(drain(ed, c));
  fs::remove_file(resolve_asset_path("Assets/Textures/livelink_tile.png"));
  fs::remove_file(resolve_asset_path("Assets/Animations/livelink_wave.anim"));
  forget_clip_assets("Assets/Animations/livelink_wave.anim");
  return s;
}

/* What must stay the same for the bridges: each message's type, id or key, name and vertex count, in order (the
 * meshes, built on a worker, as a sorted list of their own). */
std::vector<std::string> outline(const std::vector<Message> &ms) {
  std::vector<std::string> order, meshes;
  for (const Message &m : ms) {
    const std::string s = m.type() + "|" + std::to_string((long long)m.j["id"].number()) + "|" + std::to_string((long long)m.j["key"].number()) + "|" +
                          m.j["name"].str + "|" + std::to_string((long long)m.j["verts"].number());
    if (m.type() == "lodmesh") {  // (round 380: built on the worker; its vertex count depends on whether this build decimates)
      meshes.push_back("lodmesh|" + std::to_string((long long)m.j["id"].number()) + "|" + std::to_string((long long)m.j["level"].number()));
      continue;
    }
    const bool worker = m.type() == "mesh" || m.type() == "texture" || m.type() == "clip";  // (made on workers: when they land varies)
    (worker ? meshes : order).push_back(s);
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
    for (const char *t : {"object", "mesh", "material", "light", "camera", "transform", "deform", "pose", "delete", "play", "world", "render", "filters", "texture", "weights", "clip"})
      std::printf(" %s %d", t, count_of(s.ms, t));
    for (const char *t : {"lod", "lodmesh", "collider", "avatar", "springs"}) std::printf(" %s %d", t, count_of(s.ms, t));
    std::printf("\n");
    for (const char *t : {"transform", "delete"}) CHECK(count_of(s.ms, t) == 1);
    CHECK(count_of(s.ms, "pose") == 2);    // (a bone turned; round 379: the clip's frame 30)
    CHECK(count_of(s.ms, "deform") == 7);  // (the Box's vertex; round 378's skinned Arm and round 381's Face, each at rest built, posed, posed by the clip - this client doesn't skin)
    CHECK(count_of(s.ms, "clip") == 1);    // (round 379: Wave at the sync)
    CHECK(count_of(s.ms, "lod") == 1 && count_of(s.ms, "lodmesh") == 2 && count_of(s.ms, "collider") == 1);  // (round 380: the crate's, at the sync)
    CHECK(count_of(s.ms, "play") == 2 && count_of(s.ms, "material") >= 3);
    CHECK(count_of(s.ms, "world") == 2 && count_of(s.ms, "render") == 2 && count_of(s.ms, "filters") == 2);  // (round 376: at the sync and once changed)
    CHECK(count_of(s.ms, "texture") == 1);  // (round 377: the tile once, at the sync)
    CHECK(count_of(s.ms, "weights") == 2);  // (round 378: the Arm's Smile at the sync; round 381: the Face's keys)
    CHECK(count_of(s.ms, "avatar") == 1 && count_of(s.ms, "springs") == 1);  // (round 381: the rig's at the sync; its chain at the end)
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
    CHECK(n > 20 && st.pending() == 0 && meshes > 0 && bad_vert == 0 && agree * 10 >= total * 9 && deforms == 7 && deformed == 7);  // (the Box's; the skinned Arm's (round 378) and Face's (round 381): built, posed, posed by the clip - the recorder doesn't skin)
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
  test("Send to Blendity from Unreal (round 374): the plugin's snapshot.json read by Blendity - a yawed sun at Unreal's (100, 200, 300) cm lands at (2, 3, 1) m facing Unreal's +Y, its lux turned into Live Link's intensity, a spot's half cone doubled, a camera's horizontal fov made vertical, a Blueprint's class kept; the discovery file's inbox", [] {
    namespace ul = blendity_link;
    const double s45 = std::sqrt(0.5);
    ul::SnapActor sun, spot, cam, door;
    sun.name = "Sun", sun.light = 1, sun.intensity = 3.14159265358979 * 2.0;
    sun.loc[0] = 100, sun.loc[1] = 200, sun.loc[2] = 300, sun.quat[2] = s45, sun.quat[3] = s45;
    spot.name = "Spot \"A\"", spot.light = 3, spot.intensity = 5000, spot.outer_deg = 30, spot.inner_deg = 10, spot.radius_cm = 700;
    cam.name = "Cam", cam.camera = true, cam.hfov_deg = 90, cam.aspect = 16.0 / 9.0;
    door.name = "Door", door.path = "Room/Door", door.cls = "/Game/BP_Door.BP_Door_C";
    const std::string text = ul::snapshot_json("Unreal 5.8", "Yard", "Yard.fbx", {sun, spot, cam, door});
    Snapshot snap;
    std::string why;
    CHECK(read_snapshot(text, snap, why) && snap.objects.size() == 4 && snapshot_engine(snap) == "Unreal" && snap.model == "Yard.fbx");
    if (snap.objects.size() != 4) return;
    const SnapshotObject &o = snap.objects[0];
    const Vec3 fwd = o.transform.rotation.rotate(Vec3(0, 0, 1));
    std::printf("    the sun at (%g, %g, %g) facing (%g, %g, %g)\n", o.transform.position.x, o.transform.position.y, o.transform.position.z, fwd.x, fwd.y, fwd.z);
    CHECK(length(o.transform.position - Vec3(2, 3, 1)) < 1e-5f && length(fwd - Vec3(1, 0, 0)) < 1e-5f);  // (Unreal's +Y is Blendity's +X)
    Light sl, pl;
    snapshot_light(o.light, sl);
    snapshot_light(snap.objects[1].light, pl);
    CHECK(sl.type == 0 && std::fabs(sl.intensity - 2.0f) < 1e-5f && pl.type == 2 && std::fabs(pl.intensity - 5.0f) < 1e-5f && std::fabs(pl.spot_angle - 60.0f) < 1e-4f &&
          std::fabs(pl.inner_spot_angle - 20.0f) < 1e-4f && std::fabs(pl.range - 7.0f) < 1e-5f && snap.objects[1].name == "Spot \"A\"");
    Camera c;
    snapshot_camera(snap.objects[2].camera, c);
    CHECK(std::fabs(c.fov - 58.7155f) < 1e-3f && snap.objects[3].engine_class == "/Game/BP_Door.BP_Door_C" && snap.objects[3].path == "Room/Door");
    const std::string dir = test_dir() + "/ue374";
    fs::make_dirs(dir);
    ul::Endpoint ep;
    CHECK(fs::write_file(dir + "/livelink.json", "{\"protocol\":\"blendity-livelink/1\",\"port\":40100,\"inbox\":\"C:/p/Incoming\"}") &&
          ul::read_discovery(dir + "/livelink.json", ep) && ep.inbox == "C:/p/Incoming");
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
      /* Round 382: everything that flows since round 376, live - fog from 5 to 40 m; a 4x4 checker as the Box's Base Map;
       * "Arm" skinned to the rig with a Smile key at 0.5; the rig's clip "Wave"; "Crate" with three levels and a 1 x 2 x 1
       * Box Collider. */
      EnvironmentSettings &env = ed.scene().environment;
      env.fog = true, env.fog_start = 5.0f, env.fog_end = 40.0f;
      std::vector<uint32_t> tile(16);
      for (int i = 0; i < 16; i++) tile[(size_t)i] = ((i / 4 + i % 4) % 2) ? 0xFFFFFFFFu : 0xFF2040C0u;
      fs::make_dirs(fs::parent(resolve_asset_path("Assets/Textures/live382_tile.png")));  // (a short test folder, Unreal's, has none yet)
      write_png(resolve_asset_path("Assets/Textures/live382_tile.png"), tile.data(), 4, 4, 4);
      texture_cache_forget(resolve_asset_path("Assets/Textures/live382_tile.png"));
      mat.base_map = TextureRef{"Assets/Textures/live382_tile.png", false};
      mat.touch();
      GameObject *arm = ed.scene().create("Arm");
      arm->add<MeshFilter>()->mesh = primitives::cylinder(0.2f, 2.0f, 8);
      arm->add<MeshRenderer>();
      arm->set_local_position(Vec3(3, 0, 0));
      SkinModifier *skin = arm->add<SkinModifier>();
      skin->object = "Rig";
      Mesh &am = *mesh_make_mutable(arm->get<MeshFilter>()->mesh);
      std::string why;
      if (!skin_bind_automatic(*skin, am, &why)) std::printf("    the Arm's bind failed: %s\n", why.c_str());
      ShapeKey smile;
      smile.name = "Smile";
      smile.delta.assign(am.vert_count(), Vec3(0.0f, 0.0f, 0.1f));
      am.shape_keys.push_back(smile);
      am.touch();
      arm->get<MeshRenderer>()->set_shape_weight("Smile", 0.5f);
      AnimationClipPtr wave = create_clip_asset("Assets/Animations/live382_wave.anim", "Wave");
      wave->track("Upper", "Rotation", 2).set_key(0.0f, 0.0f);
      wave->track("Upper", "Rotation", 2).set_key(60.0f, 60.0f);
      wave->length = 60.0f;
      wave->touch();
      Animation *an = root->add<Animation>();
      an->clip = "Assets/Animations/live382_wave.anim";
      an->play_automatically = false;
      if (GameObject *crate = make_cube(ed, "Crate")) {
        crate->set_local_position(Vec3(0, 0, -3));
        crate->add<LODGroupComponent>();
        ColliderComponent *col = crate->add<ColliderComponent>();
        col->fit = false, col->size = Vec3(1, 2, 1);
      }
      run(2.0);
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
    std::printf("    sent: %llu objects, %llu transforms, %llu meshes, %llu deforms, %llu materials, %llu deletes, %llu poses, %llu textures (%llu the same); the dense mesh %zu vertices\n",
                (unsigned long long)st.objects, (unsigned long long)st.transforms, (unsigned long long)st.meshes, (unsigned long long)st.deforms,
                (unsigned long long)st.materials, (unsigned long long)st.deletes, (unsigned long long)st.poses, (unsigned long long)st.textures, (unsigned long long)st.textures_same, dense_verts);
    ed.livelink_stop();  // (the discovery file goes: the bridge checks its scene and writes its report)
    fs::remove_file(resolve_asset_path("Assets/Textures/live382_tile.png"));
    texture_cache_forget(resolve_asset_path("Assets/Textures/live382_tile.png"));
    fs::remove_file(resolve_asset_path("Assets/Animations/live382_wave.anim"));
    forget_clip_assets("Assets/Animations/live382_wave.anim");
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

static const Message *last_of(const std::vector<Message> &ms, const char *type) {
  const Message *found = nullptr;
  for (const Message &m : ms) found = m.type() == type ? &m : found;
  return found;
}

static void tests_round376() {
  test("Live Link's look (round 376): the world, the render's look and a camera's filters go at the sync; a fog switched on, an HDRI chosen, the exposure and view transform changed, Bloom added then the stack removed each go once - nothing while unchanged", [] {
    Editor ed;
    ed.init_headless(640, 400);
    CHECK(ed.livelink_start(0));
    ed.livelink_for_test()->set_interval_for_test(0);
    net::LineClient c;
    CHECK(c.connect(ed.livelink_for_test()->port()));
    const std::vector<Message> sync = until(ed, c, "sync_end");
    const Message *w0 = last_of(sync, "world"), *r0 = last_of(sync, "render"), *f0 = last_of(sync, "filters");
    CHECK(w0 && r0 && w0->j["mode"].str == "gradient" && w0->j["fog"]["enabled"].type == Json::Bool && w0->j["fields"].type == Json::Obj);
    CHECK(f0 && f0->j["effects"].size() == 0);  // (always at a sync, empty without a stack: a reconnecting engine drops an old one)
    CHECK(count_of(drain(ed, c), "world") == 0);  // (unchanged: nothing)
    Scene &sc = ed.scene();
    sc.environment.fog = true, sc.environment.fog_start = 3.0f, sc.environment.fog_end = 30.0f;
    const std::vector<Message> fog = drain(ed, c);
    const Message *w1 = last_of(fog, "world");
    CHECK(count_of(fog, "world") == 1 && w1 && w1->j["fog"]["enabled"].b && std::fabs(w1->j["fog"]["end"].number() - 30.0) < 1e-6);
    sc.environment.mode = 2, sc.environment.hdri = TextureRef{"Assets/Textures/sky.hdr", false};
    const std::vector<Message> hdri = drain(ed, c);
    const Message *w2 = last_of(hdri, "world");
    std::printf("    the HDRI sent as %s\n", w2 ? w2->j["hdri"].str.c_str() : "-");
    CHECK(w2 && w2->j["mode"].str == "hdri" && w2->j["hdri"].str == resolve_asset_path("Assets/Textures/sky.hdr"));
    sc.render.exposure = -1.0f, sc.render.view_transform = 2;
    const std::vector<Message> look = drain(ed, c);
    const Message *r1 = last_of(look, "render");
    CHECK(count_of(look, "render") == 1 && r1 && std::fabs(r1->j["exposure"].number() + 1.0) < 1e-6 && r1->j["view_transform"].str == "ACES" && count_of(look, "world") == 0);
    GameObject *cam = sc.find_by_name("Main Camera");
    CHECK(cam);
    if (!cam) return;
    /* Another camera's stack isn't the look: only the render camera's goes (review 376: two stacks fought over one). */
    GameObject *side = sc.create("Side Camera");
    side->add<Camera>();
    side->add<CameraFilters>()->add("Vignette");
    CHECK(count_of(drain(ed, c), "filters") == 0);
    CameraFilters *f = cam->add<CameraFilters>();
    static_cast<BloomEffect *>(f->add("Bloom"))->intensity = 1.5f;
    const std::vector<Message> bloom = drain(ed, c);
    const Message *f1 = last_of(bloom, "filters");
    CHECK(count_of(bloom, "filters") == 1 && f1 && (uint64_t)f1->j["id"].number() == cam->id && f1->j["effects"].size() == 1 &&
          f1->j["effects"][0]["type"].str == "Bloom" && std::fabs(f1->j["effects"][0]["fields"]["Intensity"].number() - 1.5) < 1e-6);
    CHECK(count_of(drain(ed, c), "filters") == 0);
    /* A second client joins while the first is connected: it gets the whole look; the first gets nothing again. */
    net::LineClient c2;
    CHECK(c2.connect(ed.livelink_for_test()->port()));
    const std::vector<Message> second = until(ed, c2, "sync_end");
    const Message *f3 = last_of(second, "filters");
    CHECK(last_of(second, "world") && last_of(second, "render") && f3 && f3->j["effects"].size() == 1);
    CHECK(count_of(drain(ed, c), "world") == 0);
    cam->remove_component(f);
    const std::vector<Message> gone = drain(ed, c);
    const Message *f2 = last_of(gone, "filters");
    CHECK(count_of(gone, "filters") == 1 && f2 && f2->j["effects"].size() == 0);  // (removed: an empty stack)
    /* The render camera deleted: the next camera's stack (the side camera's Vignette) is the look now. */
    cam->add<CameraFilters>()->add("Bloom");
    drain(ed, c);
    sc.destroy(cam);
    const std::vector<Message> deleted = drain(ed, c);
    const Message *f4 = last_of(deleted, "filters");
    std::printf("    the render camera deleted: filters for %s with %zu effect(s)\n", f4 && (uint64_t)f4->j["id"].number() == side->id ? "the side camera" : "?",
                f4 ? f4->j["effects"].size() : 0);
    CHECK(f4 && (uint64_t)f4->j["id"].number() == side->id && f4->j["effects"].size() == 1 && f4->j["effects"][0]["type"].str == "Vignette");
    sc.destroy(side);
    const std::vector<Message> none_left = drain(ed, c);  // (kept: last_of points into it - the sanitizers caught a pointer into a temporary)
    const Message *f5 = last_of(none_left, "filters");
    CHECK(f5 && f5->j["id"].number() == 0 && f5->j["effects"].size() == 0);  // (no camera: no look)
    const livelink::Stats &st = ed.livelink_for_test()->stats();
    std::printf("    sent: %llu world, %llu render, %llu filters\n", (unsigned long long)st.worlds, (unsigned long long)st.renders, (unsigned long long)st.filters);
    ed.livelink_stop();
  });
  test("the lesson's Try-it (round 376): window Lighting - the world and fog Live Link sends as they change", [] {
    Editor ed;
    ed.init_headless(640, 400);
    ed.command("window Lighting");
    CHECK(ed.window_docked_for_test(WindowKind::Lighting));
  });
}

/* A PNG's width and height from its IHDR (round 377). */
static bool png_size(const std::string &png, int &w, int &h) {
  if (png.size() < 24 || png.compare(1, 3, "PNG") != 0) return false;
  auto be = [&](size_t at) { return (int)((uint8_t)png[at] << 24 | (uint8_t)png[at + 1] << 16 | (uint8_t)png[at + 2] << 8 | (uint8_t)png[at + 3]); };
  w = be(16), h = be(20);
  return true;
}

static void tests_round377() {
  test("Live Link's textures (round 377): a material's map goes as PNG bytes with its key, named by the material; a rewritten image goes again once; the same pixels looked at again go nowhere; an image changing in place frame after frame (the paint view's preview) at most 4 a second, its last state after; an HDR colour map arrives as sRGB; a map no material names any more isn't sent", [] {
    Editor ed;
    ed.init_headless(640, 400);
    const std::string file = resolve_asset_path("Assets/Textures/r377_paint.png");
    std::vector<uint32_t> px(64 * 64, 0xFF808080u);
    CHECK(write_png(file, px.data(), 64, 64, 64));
    struct Cleanup {
      std::string f;
      ~Cleanup() { fs::remove_file(f); texture_cache_forget(f); }
    } cleanup{file};
    GameObject *box = make_cube(ed, "Painted");
    CHECK(box && ed.livelink_start(0));
    if (!box) return;
    MaterialPtr mat = box->mesh_renderer()->materials[0];
    mat->base_map = TextureRef{"Assets/Textures/r377_paint.png", false};
    mat->touch();
    ed.livelink_for_test()->set_interval_for_test(0);
    net::LineClient c;
    CHECK(c.connect(ed.livelink_for_test()->port()));
    std::vector<Message> sync = until(ed, c, "sync_end");
    for (const Message &m : drain(ed, c)) sync.push_back(m);
    const Message *tex = last_of(sync, "texture");
    int key = -1;
    for (const Message &m : sync)
      if (m.type() == "material" && m.j["maps"]["Base Map"].type == Json::Obj) key = (int)m.j["maps"]["Base Map"]["texture"].number(-1);
    int w = 0, h = 0;
    std::printf("    at the sync: %d texture message(s), the material names key %d, %zu PNG bytes\n", count_of(sync, "texture"), key, tex ? tex->bin.size() : 0);
    CHECK(tex && key > 0 && (int)tex->j["key"].number() == key && png_size(tex->bin, w, h) && w == 64 && h == 64 && tex->j["width"].number() == 64);
    /* Repainted (the file rewritten): once more. */
    std::fill(px.begin(), px.end(), 0xFFFF0000u);
    CHECK(write_png(file, px.data(), 64, 64, 64));
    texture_cache_forget(file);
    invalidate_material_textures();
    const std::vector<Message> again = drain(ed, c);
    std::printf("    repainted: %d texture message(s)\n", count_of(again, "texture"));
    CHECK(count_of(again, "texture") == 1 && last_of(again, "texture")->j["hash"].str != tex->j["hash"].str);
    /* Another texture's change bumps the textures' generation: this one's pixels the same - not even copied again
     * (review 377: every texture was copied on the main thread at each bump). */
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const livelink::Stats st0 = ed.livelink_for_test()->stats();
    invalidate_material_textures();
    const std::vector<Message> bumped = drain(ed, c);
    const livelink::Stats &st1 = ed.livelink_for_test()->stats();
    CHECK(count_of(bumped, "texture") == 0 && st1.textures == st0.textures && st1.textures_same == st0.textures_same);
    /* An image changing in place frame after frame (only the paint view's preview does that; a painted surface itself
     * changes once a stroke ends, when its outputs are made again) - at most one a quarter second, the last after. */
    TexturePtr live = resolve_texture_ref(mat->base_map);
    int during = 0;
    for (int f = 0; f < 6 && live; f++) {
      for (size_t i = 0; i < live->levels[0].px8.size(); i += 7) const_cast<Texture &>(*live).levels[0].px8[i] = 0xFF000000u | (uint32_t)(f * 40);
      during += count_of(drain(ed, c, 1, 20), "texture");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const int after = count_of(drain(ed, c), "texture");
    std::printf("    changed in place over 6 frames: %d sent during, %d after\n", during, after);
    CHECK(during <= 2 && after == 1);
    /* An HDR colour map (0.2 linear grey): its PNG in sRGB, as an 8-bit colour map's is (review 377: it went linear, dark). */
    const std::string hdr = resolve_asset_path("Assets/Textures/r377_grey.hdr");
    std::vector<float> grey(8 * 8 * 3, 0.2f);
    CHECK(write_hdr(hdr, grey.data(), 8, 8));
    struct Gone {
      std::string f;
      ~Gone() { fs::remove_file(f); texture_cache_forget(f); }
    } gone{hdr};
    mat->emission_map = TextureRef{"Assets/Textures/r377_grey.hdr", false};
    mat->touch();
    std::vector<Message> hdr_msgs = drain(ed, c);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    for (const Message &m : drain(ed, c)) hdr_msgs.push_back(m);
    const Message *grey_tex = nullptr;
    for (const Message &m : hdr_msgs)
      if (m.type() == "texture" && m.j["name"].str.find("grey") != std::string::npos) grey_tex = &m;
    int byte = -1;
    if (grey_tex) {
      const std::string png_file = test_dir() + "/r377_grey_back.png";
      fs::write_file(png_file, grey_tex->bin);
      TexturePtr back = texture_load(png_file, false);
      byte = back && !back->levels.empty() && !back->levels[0].px8.empty() ? (int)(back->levels[0].px8[0] & 0xFF) : -1;
      fs::remove_file(png_file);
    }
    std::printf("    the HDR's 0.2 grey as PNG byte %d (sRGB 124)\n", byte);
    CHECK(grey_tex && std::abs(byte - 124) <= 1);
    /* The map taken off: its image changing isn't sent any more. */
    mat->emission_map = TextureRef{};
    mat->touch();
    drain(ed, c);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::fill(grey.begin(), grey.end(), 0.7f);
    CHECK(write_hdr(hdr, grey.data(), 8, 8));
    texture_cache_forget(hdr);
    int grey_again = 0;
    for (const Message &m : drain(ed, c)) grey_again += m.type() == "texture" && m.j["name"].str.find("grey") != std::string::npos;
    CHECK(grey_again == 0);
    ed.livelink_stop();
  });
  test("the lesson's Try-it (round 377): window Textures - what you paint there reaches the engines as you paint it", [] {
    Editor ed;
    ed.init_headless(640, 400);
    ed.command("window Textures");
    CHECK(ed.window_docked_for_test(WindowKind::Textures));
  });
}

static const Message *last_for(const std::vector<Message> &ms, const char *type, uint64_t id) {
  const Message *found = nullptr;
  for (const Message &m : ms) found = m.type() == type && (uint64_t)m.j["id"].number() == id ? &m : found;
  return found;
}

static void tests_round378() {
  test("Live Link's skins and blend shapes (round 378): a skinned, keyed mesh goes once at rest with its skin (its joints the bone objects) and its keys as morph targets; a bone turned sends its pose, and its vertices only to a client that didn't say it skins; a shape weight changed sends one weights message to both", [] {
    Editor ed;
    ed.init_headless(800, 600);
    GameObject *body = ed.scene().create("Body");
    body->add<MeshFilter>()->mesh = primitives::cylinder(0.3f, 2.0f, 12);
    body->add<MeshRenderer>();
    ed.command("create Armature");
    GameObject *arm = ed.scene().find_by_name("Armature");
    CHECK(arm && !arm->children.empty());
    if (!arm || arm->children.empty()) return;
    SkinModifier *sk = body->add<SkinModifier>();
    sk->object = arm->name;
    Mesh &mm = *mesh_make_mutable(body->get<MeshFilter>()->mesh);
    std::string why;
    CHECK(skin_bind_automatic(*sk, mm, &why));
    ShapeKey smile;
    smile.name = "Smile";
    smile.delta.assign(mm.vert_count(), Vec3(0.0f, 0.1f, 0.0f));
    mm.shape_keys.push_back(smile);
    mm.touch();
    CHECK(ed.livelink_start(0));
    ed.livelink_for_test()->set_interval_for_test(0);
    net::LineClient plain, skins;
    CHECK(plain.connect(ed.livelink_for_test()->port()) && skins.connect(ed.livelink_for_test()->port()));
    CHECK(skins.send("{\"t\":\"client\",\"app\":\"Skinning test\",\"bridge\":\"1.2\",\"caps\":[\"skins\"]}"));
    std::vector<Message> a = until(ed, plain, "sync_end"), b = until(ed, skins, "sync_end");
    for (const Message &m : drain(ed, plain)) a.push_back(m);
    for (const Message &m : drain(ed, skins)) b.push_back(m);
    const Message *mesh = last_for(b, "mesh", body->id);
    CHECK(mesh && mesh->j["skin"]["armature"].number() == (double)arm->id && mesh->j["skin"]["joints"].size() >= 1 && mesh->j["shapes"].size() == 1 &&
          mesh->j["shapes"][0].str == "Smile");
    if (!mesh) return;
    const Json g = glb_json(mesh->bin);
    std::printf("    the rest mesh: %zu joint(s), %zu skin(s), %zu morph target(s)\n", mesh->j["skin"]["joints"].size(), g["skins"].size(),
                g["meshes"][0]["primitives"][0]["targets"].size());
    CHECK(g["skins"].size() == 1 && g["meshes"][0]["primitives"][0]["targets"].size() == 1 && g["meshes"][0]["primitives"][0]["attributes"]["JOINTS_0"].type == Json::Num);
    bool joints_are_bones = mesh->j["skin"]["joints"].size() > 0;
    for (const Json &id : mesh->j["skin"]["joints"].arr) joints_are_bones = joints_are_bones && ed.scene().find((uint64_t)id.number()) != nullptr;
    CHECK(joints_are_bones);
    /* A bone turned: its pose to both; the body's vertices only to the plain client. */
    GameObject *bone = arm->children[0];
    bone->set_local_rotation(Quat::euler(Vec3(0, 0, 30.0f)));
    const std::vector<Message> pa = drain(ed, plain), pb = drain(ed, skins);
    std::printf("    a bone turned: the plain client %d deform(s), the skinning one %d deform(s) and %d pose(s)\n", (int)(last_for(pa, "deform", body->id) != nullptr),
                (int)(last_for(pb, "deform", body->id) != nullptr), count_of(pb, "pose"));
    CHECK(last_for(pa, "deform", body->id) && !last_for(pb, "deform", body->id) && count_of(pb, "pose") >= 1 && count_of(pb, "mesh") == 0);
    /* A shape weight: one weights message each. */
    body->get<MeshRenderer>()->set_shape_weight("Smile", 0.5f);
    const std::vector<Message> wa = drain(ed, plain), wb = drain(ed, skins);
    const Message *w = last_for(wb, "weights", body->id);
    CHECK(count_of(wb, "weights") == 1 && count_of(wa, "weights") == 1 && w && std::fabs(w->j["weights"][0].number() - 0.5) < 1e-6 && !last_for(wb, "deform", body->id));
    CHECK(count_of(drain(ed, skins), "weights") == 0);  // (unchanged: nothing)
    std::printf("    rigs %llu, weights %llu\n", (unsigned long long)ed.livelink_for_test()->stats().rigs, (unsigned long long)ed.livelink_for_test()->stats().weights);
    ed.livelink_stop();
  });
  test("Live Link's rigs as they change (review 378): a rename sends no mesh; Max Influences or a bone's rest sends the skin again; Preserve Volume, Rest Position or the armature deleted sends a plain mesh, and the skin again after", [] {
    Editor ed;
    ed.init_headless(800, 600);
    GameObject *body = ed.scene().create("Body");
    body->add<MeshFilter>()->mesh = primitives::cylinder(0.3f, 2.0f, 12);
    body->add<MeshRenderer>();
    ed.command("create Armature");
    GameObject *arm = ed.scene().find_by_name("Armature");
    CHECK(arm && !arm->children.empty());
    if (!arm || arm->children.empty()) return;
    SkinModifier *sk = body->add<SkinModifier>();
    sk->object = arm->name;
    CHECK(skin_bind_automatic(*sk, *mesh_make_mutable(body->get<MeshFilter>()->mesh)));
    CHECK(ed.livelink_start(0));
    ed.livelink_for_test()->set_interval_for_test(0);
    net::LineClient c;
    CHECK(c.connect(ed.livelink_for_test()->port()));
    CHECK(c.send("{\"t\":\"client\",\"app\":\"Skinning test\",\"bridge\":\"1.2\",\"caps\":[\"skins\"]}"));
    std::vector<Message> s = until(ed, c, "sync_end");
    for (const Message &m : drain(ed, c)) s.push_back(m);
    CHECK(last_for(s, "mesh", body->id) && last_for(s, "mesh", body->id)->j["skin"]["joints"].size() >= 1 && count_of(s, "weights") == 0);  // (no keys: no weights)
    auto step = [&](const char *what, const std::function<void()> &change) {
      change();
      const std::vector<Message> ms = drain(ed, c);
      const Message *m = last_for(ms, "mesh", body->id);
      const int r = !m ? 0 : m->j["skin"].type == Json::Obj ? 2 : 1;  // (0 no mesh, 1 a plain one, 2 with its skin)
      std::printf("    %s: %s\n", what, r == 0 ? "no mesh" : r == 1 ? "a plain mesh" : "the mesh with its skin");
      return r;
    };
    CHECK(step("renamed", [&] { body->name = "Body Renamed"; }) == 0);
    CHECK(step("Max Influences 4 -> 2", [&] { sk->max_influences = 2; }) == 2);
    CHECK(step("a bone's rest moved", [&] {
            Bone *b = arm->children[0]->get<Bone>();
            if (b) b->rest_position = b->rest_position + Vec3(0, 0.1f, 0);
          }) == 2);
    CHECK(step("Preserve Volume on", [&] { arm->get<Armature>()->deform = 1; }) == 1);
    CHECK(step("Preserve Volume off", [&] { arm->get<Armature>()->deform = 0; }) == 2);
    CHECK(step("Rest Position on", [&] { arm->get<Armature>()->rest_position = true; }) == 1);
    CHECK(step("Rest Position off", [&] { arm->get<Armature>()->rest_position = false; }) == 2);
    CHECK(step("the armature deleted", [&] { ed.scene().destroy(arm); }) == 1);
    ed.livelink_stop();
  });
  test("the lesson's Try-it (round 378): window Rigging - a skinned character goes to the engines with its skin", [] {
    Editor ed;
    ed.init_headless(640, 400);
    ed.command("window Rigging");
    CHECK(ed.window_docked_for_test(WindowKind::Rigging));
  });
}

static void tests_round379() {
  test("Live Link's clips (round 379): a clip goes baked at the sync - each keyed object's local transform a frame, in Live Link's axes, as apply_clip poses it; a key changed sends it again once, nothing while unchanged; at most 4 a second while it keeps changing", [] {
    Editor ed;
    ed.init_headless(800, 600);
    GameObject *rig = ed.scene().create("Rig");
    GameObject *a = ed.scene().create("A", rig);
    GameObject *b = ed.scene().create("B", a);
    b->set_local_position(Vec3(0, 1, 0));
    AnimationClipPtr c = create_clip_asset("Assets/Animations/r379_clip.anim", "Swing");
    CHECK(c != nullptr);
    if (!c) return;
    c->frame_rate = 30.0f;
    c->track("A", "Rotation", 0).set_key(0.0f, 0.0f);
    c->track("A", "Rotation", 0).set_key(20.0f, 90.0f);
    c->track("A/B", "Scale", 1).set_key(0.0f, 1.0f);
    c->track("A/B", "Scale", 1).set_key(20.0f, 2.0f);
    c->track("A/B", "Material Base Color", 0).set_key(0.0f, 0.5f);  // (not a transform: not in the clip)
    c->length = 20.0f;
    c->touch();
    rig->add<Animation>()->clip = "Assets/Animations/r379_clip.anim";
    CHECK(ed.livelink_start(0));
    ed.livelink_for_test()->set_interval_for_test(0);
    net::LineClient cl;
    CHECK(cl.connect(ed.livelink_for_test()->port()));
    std::vector<Message> s = until(ed, cl, "sync_end");
    for (const Message &x : drain(ed, cl)) s.push_back(x);  // (baked on the clip worker: after the sync)
    const Message *m = last_for(s, "clip", rig->id);
    CHECK(m && m->j["name"].str == "Swing" && m->j["frames"].number() == 21 && m->j["fps"].number() == 30 && m->j["tracks"].size() == 2);
    if (!m || m->j["tracks"].size() != 2) return;
    CHECK(m->bin.size() == 2 * 21 * 10 * sizeof(float));
    /* Frame 10 against apply_clip on the scene itself. */
    const float *f = reinterpret_cast<const float *>(m->bin.data());
    apply_clip(*c, *rig, 10.0f);
    double worst = 0.0;
    for (size_t k = 0; k < 2; k++) {
      GameObject *g = ed.scene().find((uint64_t)m->j["tracks"][k].number());
      CHECK(g != nullptr);
      if (!g) return;
      const Transform &t = g->local();
      const float want[10] = {-t.position.x, t.position.y, t.position.z, t.rotation.x, -t.rotation.y, -t.rotation.z, t.rotation.w, t.scale.x, t.scale.y, t.scale.z};
      for (int i = 0; i < 10; i++) worst = std::max(worst, (double)std::fabs(f[(k * 21 + 10) * 10 + i] - want[i]));
    }
    std::printf("    Swing: %d frames, %zu tracks; frame 10 against apply_clip: at most %g apart\n", (int)m->j["frames"].number(), m->j["tracks"].size(), worst);
    CHECK(worst < 1e-5);
    (void)drain(ed, cl);
    CHECK(count_of(drain(ed, cl), "clip") == 0);  // (unchanged: nothing)
    /* A key changed: once; changed again at once: owed, then sent after the gap. */
    c->track("A", "Rotation", 0).set_key(20.0f, 45.0f);
    c->touch();
    CHECK(count_of(drain(ed, cl), "clip") == 1);
    c->track("A", "Rotation", 0).set_key(20.0f, 30.0f);
    c->touch();
    const int at_once = count_of(drain(ed, cl), "clip");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const int later = count_of(drain(ed, cl), "clip");
    std::printf("    changed twice in a row: %d at once, %d after a quarter second\n", at_once, later);
    CHECK(at_once == 0 && later == 1);
    ed.livelink_stop();
    forget_clip_assets("Assets/Animations/r379_clip.anim");
  });
  test("Live Link's clips as things change (review 379): another clip chosen and an axis the clip doesn't key moved send it again; a keyed axis moved (the clip playing) doesn't; a minute of 100 keyed objects costs the main thread little; ten minutes is over the cap and isn't sent", [] {
    Editor ed;
    ed.init_headless(800, 600);
    GameObject *rig = ed.scene().create("Rig");
    GameObject *a = ed.scene().create("A", rig);
    AnimationClipPtr one = create_clip_asset("Assets/Animations/r379_one.anim", "One"), two = create_clip_asset("Assets/Animations/r379_two.anim", "Two");
    CHECK(one && two);
    if (!one || !two) return;
    for (AnimationClipPtr c : {one, two}) {
      c->track("A", "Rotation", 0).set_key(0.0f, 0.0f);
      c->track("A", "Rotation", 0).set_key(10.0f, 90.0f);
      c->touch();
    }
    Animation *an = rig->add<Animation>();
    an->clip = "Assets/Animations/r379_one.anim";
    CHECK(ed.livelink_start(0));
    ed.livelink_for_test()->set_interval_for_test(0);
    net::LineClient cl;
    CHECK(cl.connect(ed.livelink_for_test()->port()));
    (void)until(ed, cl, "sync_end");
    (void)drain(ed, cl);
    auto clips_after = [&](const std::function<void()> &change) {
      change();
      std::this_thread::sleep_for(std::chrono::milliseconds(300));  // (past the rate limit)
      const std::vector<Message> ms = drain(ed, cl);
      const Message *m = last_for(ms, "clip", rig->id);
      return std::make_pair(count_of(ms, "clip"), m ? m->j["asset"].str : std::string());
    };
    const auto chosen = clips_after([&] { an->clip = "Assets/Animations/r379_two.anim"; });
    const auto moved = clips_after([&] { a->set_local_position(Vec3(0, 0.5f, 0)); });
    const auto played = clips_after([&] {
      Transform t = a->local();
      t.euler_hint.x = 45.0f;
      t.rotation = Quat::euler(t.euler_hint);
      a->set_local(t);
    });
    std::printf("    another clip chosen: %d (%s); an unkeyed axis moved: %d; a keyed one moved: %d\n", chosen.first, chosen.second.c_str(), moved.first, played.first);
    CHECK(chosen.first == 1 && chosen.second == "Assets/Animations/r379_two.anim" && moved.first == 1 && played.first == 0);
    /* A minute of 100 objects keyed every frame at 60 fps: the tick that sends it again only copies the tracks. */
    AnimationClipPtr big = create_clip_asset("Assets/Animations/r379_big.anim", "Big");
    for (int o = 0; o < 100; o++) {
      ed.scene().create(strprintf("Bone%d", o), rig);
      AnimTrack &t = big->track(strprintf("Bone%d", o), "Rotation", 1);
      for (int f = 0; f <= 3600; f++) t.keys.push_back({(float)f, (float)(f % 360)});
      AnimTrack &p = big->track(strprintf("Bone%d", o), "Position", 1);
      for (int f = 0; f <= 3600; f++) p.keys.push_back({(float)f, 0.01f * (float)f});
    }
    big->touch();
    an->clip = "Assets/Animations/r379_big.anim";
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    ed.livelink_for_test()->tick(ed.scene(), false, 1e6);
    const double ms = ed.livelink_for_test()->stats().tick_ms;
    const std::vector<Message> bm = drain(ed, cl);
    const Message *bc = last_for(bm, "clip", rig->id);
    std::printf("    a minute of 100 keyed objects: the tick %.2f ms on the main thread; %.1f MB baked on the clip worker\n", ms, bc ? bc->bin.size() / 1048576.0 : 0.0);
    CHECK(bc && bc->j["frames"].number() == 3601 && bc->j["tracks"].size() == 100);
    CHECK(ms < 40.0);  // (the bake on the main thread had been about 60 ms)
    /* Ten minutes: 144 MB baked - over the cap, said and not sent. */
    for (AnimTrack &t : big->tracks) t.set_key(36000.0f, 0.0f);
    big->touch();
    ed.livelink_for_test()->tick(ed.scene(), false, 2e6);  // (after the tick above, whose clock ran ahead)
    const std::vector<Message> tm = drain(ed, cl);
    std::printf("    ten minutes: %d clip(s) sent, %llu too big\n", count_of(tm, "clip"), (unsigned long long)ed.livelink_for_test()->stats().clips_too_big);
    CHECK(count_of(tm, "clip") == 0 && ed.livelink_for_test()->stats().clips_too_big == 1);
    ed.livelink_stop();
    for (const char *p : {"Assets/Animations/r379_one.anim", "Assets/Animations/r379_two.anim", "Assets/Animations/r379_big.anim"}) forget_clip_assets(p);
  });
  test("the lesson's Try-it (round 379): window Animation - a clip keyed there goes to the engines", [] {
    Editor ed;
    ed.init_headless(640, 400);
    ed.command("window Animation");
    CHECK(ed.window_docked_for_test(WindowKind::Animation));
  });
}

static void tests_round380() {
  test("Live Link's LODs and colliders (round 380): a collider and an LOD Group go at the sync, the levels past 0 as lodmesh; a collider changed or taken off sends it again (\"none\"); the ratios changed send the group and its levels; the mesh edited sends its levels once it has stood still half a second", [] {
    Editor ed;
    ed.init_headless(800, 600);
    GameObject *crate = make_cube(ed, "Crate");
    CHECK(crate != nullptr);
    if (!crate) return;
    LODGroupComponent *lod = crate->add<LODGroupComponent>();
    ColliderComponent *col = crate->add<ColliderComponent>();
    col->fit = false, col->shape = 2, col->center = Vec3(0.25f, 0, 0), col->radius = 0.4f, col->height = 1.5f;
    CHECK(ed.livelink_start(0));
    ed.livelink_for_test()->set_interval_for_test(0);
    net::LineClient c;
    CHECK(c.connect(ed.livelink_for_test()->port()));
    std::vector<Message> s = until(ed, c, "sync_end");
    for (const Message &m : drain(ed, c)) s.push_back(m);
    const Message *cm = last_for(s, "collider", crate->id), *lm = last_for(s, "lod", crate->id);
    CHECK(cm && cm->j["shape"].str == "capsule" && std::fabs(cm->j["center"][0].number() + 0.25) < 1e-6 && std::fabs(cm->j["radius"].number() - 0.4) < 1e-6);
    CHECK(lm && lm->j["screen"].size() == 3 && !lm->j["cull"].b);
    int levels = 0;
    for (const Message &m : s) levels += m.type() == "lodmesh" && m.j["id"].number() == (double)crate->id;
    std::printf("    at the sync: a %s collider, %zu levels, %d lodmesh\n", cm ? cm->j["shape"].str.c_str() : "-", lm ? lm->j["screen"].size() : 0, levels);
    CHECK(levels == 2);
    CHECK(count_of(drain(ed, c), "collider") == 0);  // (unchanged: nothing)
    col->radius = 0.6f;
    const std::vector<Message> changed = drain(ed, c);
    CHECK(count_of(changed, "collider") == 1 && count_of(changed, "lod") == 0);
    lod->ratio[1] = 0.3f;
    const std::vector<Message> ratios = drain(ed, c);
    CHECK(count_of(ratios, "lod") == 1 && count_of(ratios, "lodmesh") == 2);
    /* The mesh edited: no levels while it changes; both once it has stood still. */
    MeshPtr m = std::make_shared<Mesh>(*crate->mesh_filter()->mesh);
    m->positions[0] = m->positions[0] + Vec3(0.2f, 0, 0);
    m->touch();
    crate->mesh_filter()->mesh = m;
    const int at_once = count_of(drain(ed, c), "lodmesh");
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    const int settled = count_of(drain(ed, c), "lodmesh");
    std::printf("    the mesh edited: %d lodmesh at once, %d after it stood still\n", at_once, settled);
    CHECK(at_once == 0 && settled == 2);
    crate->remove_component(col);
    const std::vector<Message> off = drain(ed, c);
    const Message *none = last_for(off, "collider", crate->id);
    CHECK(none && none->j["shape"].str == "none");
    /* Review 380: a screen size or Cull changed sends the group, not its levels; a rename sends neither; three levels to
     * two sends no level 2 after; the group taken off, an empty "screen". */
    lod->screen[1] = 0.2f;
    const std::vector<Message> screen_only = drain(ed, c);
    crate->name = "Crate Renamed";
    const std::vector<Message> renamed = drain(ed, c);
    lod->levels = 2;
    std::vector<Message> fewer = drain(ed, c);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    for (const Message &m : drain(ed, c)) fewer.push_back(m);
    int level2 = 0;
    for (const Message &m : fewer) level2 += m.type() == "lodmesh" && m.j["level"].number() == 2;
    std::printf("    a screen size changed: %d lod, %d lodmesh; renamed: %d lod, %d lodmesh; three levels to two: %d level-2 mesh(es)\n", count_of(screen_only, "lod"),
                count_of(screen_only, "lodmesh"), count_of(renamed, "lod"), count_of(renamed, "lodmesh"), level2);
    CHECK(count_of(screen_only, "lod") == 1 && count_of(screen_only, "lodmesh") == 0 && count_of(renamed, "lod") + count_of(renamed, "lodmesh") == 0);
    CHECK(level2 == 0);
    crate->remove_component(lod);
    const std::vector<Message> gone_ms = drain(ed, c);
    const Message *gone = last_for(gone_ms, "lod", crate->id);
    CHECK(gone && gone->j["screen"].size() == 0);
    ed.livelink_stop();
  });
  test("Live Link's world kit as objects change (review 380): an empty with a Box Collider given a mesh (made again in the bridges) sends its collider again", [] {
    Editor ed;
    ed.init_headless(800, 600);
    GameObject *wall = ed.scene().create("Wall");
    ColliderComponent *col = wall->add<ColliderComponent>();
    col->fit = false;
    CHECK(ed.livelink_start(0));
    ed.livelink_for_test()->set_interval_for_test(0);
    net::LineClient c;
    CHECK(c.connect(ed.livelink_for_test()->port()));
    std::vector<Message> s = until(ed, c, "sync_end");
    CHECK(last_for(s, "collider", wall->id) != nullptr);
    (void)drain(ed, c);
    wall->add<MeshFilter>()->mesh = primitives::cube(1.0f);
    wall->add<MeshRenderer>();
    const std::vector<Message> made = drain(ed, c);
    const Message *again = last_for(made, "collider", wall->id);
    std::printf("    the empty given a mesh: %d object message(s), the collider %s\n", count_of(made, "object"), again ? "sent again" : "not sent");
    CHECK(again && again->j["shape"].str == "box");
    ed.livelink_stop();
  });
  test("the lesson's Try-it (round 380): create Cube; component Collider; component LODGroup - a crate the engines get with its collision and levels", [] {
    Editor ed;
    ed.init_headless(640, 400);
    for (const char *cmd : {"create Cube", "component Collider", "component LODGroup"}) ed.command(cmd);
    GameObject *g = ed.selected_object();
    CHECK(g && g->get<ColliderComponent>() && g->get<LODGroupComponent>());
  });
}

static void tests_round381() {
  test("Live Link's VRChat setup (round 381): an armature with eyes sends an avatar message - the view between the eyes at rest, the mesh with the 15 visemes, the blink, the eye bones; nothing again while unchanged; a SpringBone sends its chain; the eyes deleted fall back to the Head or none; Contribute GI marks the object static", [] {
    Editor ed;
    ed.init_headless(800, 600);
    GameObject *rig = ed.scene().create("Rig");
    rig->add<Armature>();
    GameObject *chain = ed.scene().create("Tail", rig);
    chain->add<Bone>();
    add_eyes(ed, *rig, Vec3(0, 1.6f, 0.08f));
    GameObject *face = add_face(ed, "Face", Vec3(0, 1.6f, 0));
    CHECK(face != nullptr);
    if (!face) return;
    face->get<SkinModifier>()->object = "Rig";
    GameObject *floor = make_cube(ed, "Floor");
    floor->mesh_renderer()->contribute_gi = true;
    CHECK(ed.livelink_start(0));
    ed.livelink_for_test()->set_interval_for_test(0);
    net::LineClient c;
    CHECK(c.connect(ed.livelink_for_test()->port()));
    std::vector<Message> s = until(ed, c, "sync_end");
    for (const Message &m : drain(ed, c)) s.push_back(m);
    const Message *av = last_for(s, "avatar", rig->id);
    CHECK(av != nullptr);
    if (!av) return;
    const Json &v = av->j["view"];
    std::printf("    the view (glTF axes, the rig's space): %.3f %.3f %.3f; visemes on %lld, %zu names; blink on %lld\n", v[0].number(), v[1].number(), v[2].number(),
                (long long)av->j["visemes"]["mesh"].number(), av->j["visemes"]["shapes"].size(), (long long)av->j["blink"]["mesh"].number());
    CHECK(std::fabs(v[0].number()) < 1e-5 && std::fabs(v[1].number() - 1.6) < 1e-5 && std::fabs(v[2].number() - 0.08) < 1e-5);
    CHECK(av->j["visemes"]["mesh"].number() == (double)face->id && av->j["visemes"]["shapes"].size() == 15 && av->j["visemes"]["shapes"][10].str == "vrc.v_aa");
    CHECK(av->j["blink"]["mesh"].number() == (double)face->id && av->j["eyes"]["left"].number() == (double)ed.scene().find_by_name("LeftEye")->id);
    const Message *fl = last_for(s, "object", floor->id);
    CHECK(fl && fl->j["static"].b && !last_for(s, "object", face->id)->j["static"].b);
    CHECK(count_of(s, "springs") == 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    CHECK(count_of(drain(ed, c), "avatar") == 0);  // (unchanged: nothing)
    chain->add<SpringBone>()->stiffness = 0.3f;
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    const std::vector<Message> sp = drain(ed, c);
    const Message *springs = last_for(sp, "springs", rig->id);
    CHECK(springs && springs->j["chains"].size() == 1 && springs->j["chains"][0]["root"].number() == (double)chain->id &&
          std::fabs(springs->j["chains"][0]["stiffness"].number() - 0.3) < 1e-6);
    ed.scene().destroy(ed.scene().find_by_name("LeftEye"));
    ed.scene().destroy(ed.scene().find_by_name("RightEye"));
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    const std::vector<Message> after_ms = drain(ed, c);
    const Message *after = last_for(after_ms, "avatar", rig->id);
    std::printf("    the eyes deleted: %s\n", !after ? "nothing sent" : after->j["none"].b ? "none" : "the view from the visemes' rig");
    CHECK(after && !after->j["none"].b && after->j["eyes"]["left"].number() == 0);  // (still an avatar: its visemes)
    ed.livelink_stop();
  });
  test("Live Link's VRChat setup as it changes (review 381): the avatar root is the object above the rig and its meshes, the view in its space; a blink on its own mesh; a hair rig's chains only in its own springs; a chain taken off sends an empty list; the Armature taken off sends none", [] {
    Editor ed;
    ed.init_headless(800, 600);
    GameObject *avatar = ed.scene().create("Avatar");
    avatar->set_local_position(Vec3(5, 0, 0));
    GameObject *rig = ed.scene().create("Rig", avatar);
    rig->add<Armature>();
    add_eyes(ed, *rig, Vec3(0, 1.6f, 0.08f));
    GameObject *face = add_face(ed, "Face", Vec3(0, 1.6f, 0));
    GameObject *lids = make_cube(ed, "Lids");
    CHECK(face && lids);
    if (!face || !lids) return;
    ed.scene().set_parent(face, avatar);
    face->get<SkinModifier>()->object = "Rig";
    Mesh &lm = *mesh_make_mutable(lids->get<MeshFilter>()->mesh);
    for (const char *k : {"vrc.blink_left", "vrc.blink_right"}) {
      ShapeKey sk;
      sk.name = k;
      sk.delta.assign(lm.vert_count(), Vec3(0, 0.01f, 0));
      lm.shape_keys.push_back(sk);
    }
    lm.touch();
    lids->add<SkinModifier>()->object = "Rig";
    ed.scene().set_parent(lids, avatar);
    GameObject *tail = ed.scene().create("Tail", rig);
    tail->add<Bone>();
    tail->add<SpringBone>();
    GameObject *hair = ed.scene().create("Hair Rig", tail);
    hair->add<Armature>();
    GameObject *strand = ed.scene().create("Strand", hair);
    strand->add<Bone>();
    strand->add<SpringBone>();
    CHECK(ed.livelink_start(0));
    ed.livelink_for_test()->set_interval_for_test(0);
    net::LineClient c;
    CHECK(c.connect(ed.livelink_for_test()->port()));
    std::vector<Message> s = until(ed, c, "sync_end");
    for (const Message &m : drain(ed, c)) s.push_back(m);
    const Message *av = last_for(s, "avatar", rig->id), *sp = last_for(s, "springs", rig->id), *hp = last_for(s, "springs", hair->id);
    CHECK(av && sp && hp);
    if (!av || !sp || !hp) return;
    std::printf("    the root %lld (the Avatar %llu), the view y %.3f; the blink on %lld (Lids %llu); the rig's chains %zu, the hair rig's %zu\n",
                (long long)av->j["root"].number(), (unsigned long long)avatar->id, av->j["view"][1].number(), (long long)av->j["blink"]["mesh"].number(),
                (unsigned long long)lids->id, sp->j["chains"].size(), hp->j["chains"].size());
    CHECK(av->j["root"].number() == (double)avatar->id && std::fabs(av->j["view"][1].number() - 1.6) < 1e-5 && std::fabs(av->j["view"][0].number()) < 1e-5);
    CHECK(av->j["blink"]["mesh"].number() == (double)lids->id && av->j["visemes"]["mesh"].number() == (double)face->id);
    CHECK(sp->j["chains"].size() == 1 && sp->j["chains"][0]["root"].number() == (double)tail->id && hp->j["chains"].size() == 1);
    tail->remove_component(tail->get<SpringBone>());
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    const std::vector<Message> emptied_ms = drain(ed, c);
    const Message *emptied = last_for(emptied_ms, "springs", rig->id);
    CHECK(emptied && emptied->j["chains"].size() == 0);
    rig->remove_component(rig->get<Armature>());
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    const std::vector<Message> none_ms = drain(ed, c);
    const Message *none = last_for(none_ms, "avatar", rig->id);
    std::printf("    the chain taken off: %zu chains; the Armature taken off: %s\n", emptied ? emptied->j["chains"].size() : 99, none && none->j["none"].b ? "none" : "-");
    CHECK(none && none->j["none"].b);
    ed.livelink_stop();
  });
  test("the lesson's Try-it (round 381): window Avatar - the setup VRChat's SDK in Unity takes", [] {
    Editor ed;
    ed.init_headless(640, 400);
    ed.command("window Avatar");
    CHECK(ed.window_docked_for_test(WindowKind::Avatar));
  });
}

/* A Live Link line that is `id`'s transform. */
static bool is_transform_of(const std::string &line, uint64_t id) {
  return line.find("\"transform\"") != std::string::npos && line.find(strprintf("\"id\":%llu,", (unsigned long long)id)) != std::string::npos;
}

static void tests_round382() {
  test("Live Link never pauses the editor (round 382): four clients connected, 200 objects, a 100,000-vertex mesh deformed and a 50-bone rig posed every frame, a clip keyed - the main thread's tick stays short, and everything goes", [] {
    Editor ed;
    ed.init_headless(800, 600);
    for (int i = 0; i < 200; i++)
      if (GameObject *c = make_cube(ed, strprintf("Prop %d", i).c_str())) c->set_local_position(Vec3((float)(i % 20), 0, (float)(i / 20)));
    GameObject *dense = ed.scene().create("Dense");
    dense->add<MeshFilter>()->mesh = primitives::uv_sphere(1.0f, 400, 250);
    dense->add<MeshRenderer>();
    GameObject *rig = ed.scene().create("Rig");
    rig->add<Armature>();
    std::vector<GameObject *> bones;
    GameObject *parent = rig;
    for (int b = 0; b < 50; b++) {
      parent = ed.scene().create(strprintf("Bone %d", b), parent);
      parent->add<Bone>();
      bones.push_back(parent);
    }
    /* A clip on the rig, keyed as it plays. */
    AnimationClipPtr clip = create_clip_asset("Assets/Animations/r382_clip.anim", "Stress");
    for (int b = 0; b < 50; b++)
      for (int f = 0; f <= 600; f += 10) clip->track(strprintf("Bone %d", b), "Rotation", 2).set_key((float)f, (float)(f % 90));
    clip->touch();
    rig->add<Animation>()->clip = "Assets/Animations/r382_clip.anim";
    const bool started = ed.livelink_start(0);
    CHECK(started);
    if (!started) return;
    ed.livelink_for_test()->set_interval_for_test(0);
    std::vector<std::unique_ptr<net::LineClient>> clients;
    for (int k = 0; k < 4; k++) {
      clients.push_back(std::make_unique<net::LineClient>());
      CHECK(clients.back()->connect(ed.livelink_for_test()->port()));
    }
    for (auto &c : clients) (void)until(ed, *c, "sync_end");
    const size_t verts = dense->mesh_filter()->mesh->vert_count();
    MeshPtr base = dense->mesh_filter()->mesh;
    double total = 0.0, worst = 0.0;
    const int frames = 120;
    for (int f = 0; f < frames; f++) {
      MeshPtr d = mesh_make_mutable(dense->mesh_filter()->mesh);
      const float s = 1.0f + 0.001f * (float)(f % 10);
      for (size_t v = 0; v < verts; v++) d->positions[v] = base->positions[v] * s;
      d->deform_version++;
      for (GameObject *b : bones) b->set_local_rotation(Quat::euler(Vec3(0, 0, (float)(f % 30))));
      clip->track("Bone 0", "Rotation", 2).set_key((float)(f % 600), (float)f);
      clip->touch();
      ed.livelink_for_test()->tick(ed.scene(), false, 1e7 + f / 60.0);  // (a clock past any real one: the suite may have run for minutes)
      const double ms = ed.livelink_for_test()->stats().tick_ms;
      total += ms;
      worst = std::max(worst, ms);
      std::string line, bin;
      for (auto &c : clients)
        while (c->read(line, bin, 0)) {}
    }
    const livelink::Stats st = ed.livelink_for_test()->stats();
    std::printf("    %d frames, 4 clients, %zu-vertex deforms: the tick %.2f ms on average, %.2f ms at most; %llu deforms, %llu poses sent\n", frames, verts,
                total / frames, worst, (unsigned long long)st.deforms, (unsigned long long)st.poses);
    /* A topology change on the dense mesh: its copy for the mesh worker is the main thread's part. */
    dense->mesh_filter()->mesh = primitives::uv_sphere(1.0f, 401, 250);
    ed.livelink_for_test()->tick(ed.scene(), false, 1e7 + 100.0);
    const double topo = ed.livelink_for_test()->stats().tick_ms;
    std::printf("    with a clip keyed each frame: %llu clips sent; a topology change of the dense mesh: %.2f ms\n", (unsigned long long)st.clips, topo);
    CHECK(total / frames < 8.0 && worst < 25.0);  // (the time: generous for slower machines and debug builds - this one ran about 1.5 and 3)
    CHECK(topo < 40.0);
    CHECK(st.deforms >= (uint64_t)frames && st.poses >= (uint64_t)frames - 1 && st.clips >= 6);  // (everything went: a cheap tick that sent nothing would pass the times)
    ed.livelink_stop();
    fs::remove_file(resolve_asset_path("Assets/Animations/r382_clip.anim"));
    forget_clip_assets("Assets/Animations/r382_clip.anim");
  });
  test("Live Link with a surface painted dab after dab (review 382): the paint tool's own strokes - each a new texture - go at most 4 a second while the tick stays short", [] {
    Editor ed;
    ed.init_headless(800, 600);
    ed.command("create Sphere");
    GameObject *g = ed.active_object_for_test();
    CHECK(g);
    if (!g) return;
    const uint64_t gid = g->id;
    g->set_local_scale(Vec3(6.0f));
    ed.mark_changed_for_test("a big sphere");
    for (int i = 0; i < 5; i++) ed.step_frame_headless();
    ed.command("surface new PaintSurf");
    ed.command("meshmaps 512 4");
    for (int i = 0; i < 4000 && ed.meshmaps_busy_for_test(); i++) ed.step_frame_headless(), std::this_thread::sleep_for(std::chrono::milliseconds(2));
    ed.command("surface maps");
    ed.command("paint on");
    Vec2 px;
    const bool ready = ed.paint_for_test() && ed.project_for_test(ed.scene().find(gid)->world_position(), px);
    CHECK(ready);
    if (!ready) return;
    for (int i = 0; i < 30; i++) ed.step_frame_headless();
    ed.command("paint size 20");
    const bool started = ed.livelink_start(0);
    CHECK(started);
    if (!started) return;
    ed.livelink_for_test()->set_interval_for_test(0);
    std::vector<std::unique_ptr<net::LineClient>> clients;
    for (int k = 0; k < 4; k++) {
      clients.push_back(std::make_unique<net::LineClient>());
      CHECK(clients.back()->connect(ed.livelink_for_test()->port()));
    }
    for (auto &c : clients) (void)until(ed, *c, "sync_end");
    const uint64_t before = ed.livelink_for_test()->stats().textures;
    double clock = 1e7, total = 0.0, worst = 0.0;  // (past any real clock: the suite may have run for minutes)
    const int frames = 120;
    for (int f = 0; f < frames; f++) {
      ed.command(strprintf("paint dab %g %g", px.x + (float)((f * 7) % 60) - 30.0f, px.y + (float)((f * 11) % 60) - 30.0f));
      clock += 1.0 / 60.0;
      ed.set_clock_for_test(clock);
      ed.step_frame_headless();
      const double ms = ed.livelink_for_test()->stats().tick_ms;
      total += ms;
      worst = std::max(worst, ms);
      std::string line, bin;
      for (auto &c : clients)
        while (c->read(line, bin, 0)) {}
    }
    ed.livelink_for_test()->wait_meshes_for_test();
    const uint64_t sent = ed.livelink_for_test()->stats().textures - before;
    std::printf("    %d dabs over 2 s with 4 clients: %llu texture sends; the tick %.2f ms on average, %.2f ms at most\n", frames, (unsigned long long)sent, total / frames,
                worst);
    CHECK(sent >= 4 && sent <= 12 && total / frames < 8.0 && worst < 40.0);  // (about 4 a second: what each dab's new texture costs the tick)
    ed.livelink_stop();
  });
  test("the lesson's Try-it (round 382): window Live Link - what Live Link sent, and how long it took", [] {
    Editor ed;
    ed.init_headless(640, 400);
    ed.command("window Live Link");
    CHECK(ed.window_docked_for_test(WindowKind::LiveLink));
  });
  test("Live Link keeps going while a long job runs (round 382): a skin bind of a 100,000-vertex mesh on a worker - a Box moved meanwhile reaches the client before the bind lands", [] {
    Editor ed;
    ed.init_headless(800, 600);
    GameObject *box = make_cube(ed, "Box");
    ed.command("create Armature");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    ed.set_skin_async_min_verts_for_test(0);
    GameObject *body = ed.scene().create("Body");
    body->add<MeshFilter>()->mesh = primitives::uv_sphere(1.0f, 400, 250);
    body->add<MeshRenderer>();
    ed.select_for_test(body->id, false);
    ed.command("component SkinModifier");
    ed.command("set SkinModifier.Armature Armature");
    const bool started = box && ed.livelink_start(0);
    CHECK(started);
    if (!started) return;
    ed.livelink_for_test()->set_interval_for_test(0);
    net::LineClient c;
    CHECK(c.connect(ed.livelink_for_test()->port()));
    (void)until(ed, c, "sync_end");
    (void)drain(ed, c);
    ed.command("skin bind");
    const bool running = !ed.jobs_for_test().empty();
    box->set_local_position(Vec3(4, 0, 0));
    bool moved_during = false;
    int frames = 0;
    for (; frames < 2000 && !ed.jobs_for_test().empty(); frames++) {
      ed.step_frame_headless();
      const bool still = !ed.jobs_for_test().empty();  // (a move sent on the frame the bind lands proves nothing - review 382)
      std::string line, bin;
      while (c.read(line, bin, 0)) moved_during = moved_during || (still && is_transform_of(line, box->id));
    }
    std::printf("    the bind ran on a worker: %d; the Box's move sent while it ran: %d (%d frames until it landed)\n", (int)running, (int)moved_during, frames);
    CHECK(running && moved_during);
    ed.livelink_stop();
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
  tests_round376();
  tests_round377();
  tests_round378();
  tests_round379();
  tests_round380();
  tests_round381();
  tests_round382();
}

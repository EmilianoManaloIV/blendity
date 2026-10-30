// SPDX-License-Identifier: GPL-2.0-or-later
// Live Link (task 0172, ADR 0055): see livelink.h, and docs/LIVELINK.md for the protocol.
#include "livelink.h"

#include "../core/core.h"
#include "../core/json.h"
#include "../scene/export.h"
#include "../scene/scene.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace bl::livelink {

namespace {

/* Coalescing keys: a newer message of the same kind about the same thing replaces one still queued. */
enum Kind : uint64_t { kMesh = 1, kDeform, kTransform, kPose, kLight, kCamera, kMaterial };
uint64_t ckey(uint64_t id, Kind k) { return id << 3 | (uint64_t)k; }

/* A local transform in glTF's axes: Blendity's left-handed Y up mirrored in X, as the exporter writes meshes. */
struct Trs {
  float v[10];
  bool operator==(const Trs &o) const { return std::memcmp(v, o.v, sizeof(v)) == 0; }
  bool operator!=(const Trs &o) const { return !(*this == o); }
};
Trs trs_of(const GameObject &g) {
  const Transform &t = g.local();
  return {{-t.position.x, t.position.y, t.position.z, t.rotation.x, -t.rotation.y, -t.rotation.z, t.rotation.w, t.scale.x, t.scale.y, t.scale.z}};
}
void write_trs(JsonWriter &w, const Trs &t) {
  w.key("position").nums(t.v, 3);
  w.key("rotation").nums(t.v + 3, 4);
  w.key("scale").nums(t.v + 7, 3);
}

const char *kind_of(const GameObject &g) {
  if (g.mesh_filter() && g.mesh_filter()->mesh) return "mesh";
  if (g.light()) return "light";
  if (g.camera()) return "camera";
  if (g.get<Armature>()) return "armature";
  return "empty";
}
uint64_t armature_id(const GameObject &bone) {
  const Armature *a = is_bone(&bone) ? bone_armature(bone) : nullptr;
  return a && a->owner ? a->owner->id : 0;
}

/* What a mesh looked like when it was last sent: a change of `stamp` with the same topology is a deform. */
struct MeshStamp {
  uint64_t uid = 0;  // (the mesh's own id: a copy, or a mesh made again at the same address, gets a new one)
  uint64_t version = 0, deform_version = 0, topology = 0;
  size_t verts = 0;
  bool operator==(const MeshStamp &o) const {
    return uid == o.uid && version == o.version && deform_version == o.deform_version && topology == o.topology && verts == o.verts;
  }
};
MeshStamp stamp_of(const Mesh *m) {
  if (!m) return {};
  return {m->uid.v, m->version, m->deform_version, m->topology_serial(), m->positions.size()};
}

/* What every client was last told about one object. */
struct Sent {
  std::string name, kind;
  uint64_t parent = 0, armature = 0;
  bool active = true, seen = false;
  Trs trs{};
  std::vector<int> materials;
  uint64_t light = 0, camera = 0;
  MeshStamp mesh;
  bool structure_equal(const Sent &o) const {
    return name == o.name && kind == o.kind && parent == o.parent && armature == o.armature && active == o.active && materials == o.materials;
  }
};

struct MaterialSent {
  std::weak_ptr<Material> ptr;
  std::string name;
  uint64_t hash = 0;
};

/* A glTF binary to build off the main thread: the mesh is a copy, so the scene can go on changing. */
struct MeshJob {
  uint64_t id = 0;
  std::shared_ptr<Mesh> mesh;
  std::string name;
  MeshStamp stamp;
  std::vector<int> clients;  // besides every ready client when `broadcast`
  bool broadcast = false;
};

/* The texture slots of a material by their Inspector names, as paths a client on this computer can open. */
struct MapCollector : Reflector {
  JsonWriter &w;
  explicit MapCollector(JsonWriter &w_) : w(w_) {}
  bool all_fields() const override { return true; }
  void field(const char *, float &, float, float, float) override {}
  void field(const char *, int &, int, int) override {}
  void field(const char *, bool &) override {}
  void field(const char *, Vec3 &) override {}
  void color(const char *, Vec3 &) override {}
  void enumeration(const char *, int &, const char *const *, int) override {}
  void text(const char *, std::string &) override {}
  void mesh(const char *, MeshPtr &) override {}
  void texture(const char *n, TextureRef &t) override {
    if (t.empty()) return;
    w.key(n).obj().key("path").str(resolve_asset_path(t.path)).key("non_color").boolean(t.non_color).end();
  }
};

}  // namespace

struct Link::Impl {
  net::Server server;
  mutable Stats stats;
  double interval = 1.0 / 60.0, last_compare = -1e30;
  bool playing = false;
  std::unordered_map<uint64_t, Sent> sent;
  std::unordered_map<int, MaterialSent> materials;  // by key
  std::unordered_map<const Material *, int> material_key;
  int next_material = 1;
  std::unordered_map<int, uint64_t> material_hash_now;  // this tick's hashes (a material shared by many objects hashed once)

  /* The mesh worker. */
  std::thread worker;
  std::mutex wm;
  std::condition_variable wcv;
  std::deque<uint64_t> order;                    // ids waiting, oldest first
  std::unordered_map<uint64_t, MeshJob> pending;  // the newest job per id
  std::vector<std::pair<uint64_t, MeshStamp>> done;  // built and sent: (id, the mesh it was built from)
  int busy = 0;
  bool quit = false;
  std::atomic<uint64_t> built{0}, encode_us{0};  // (the worker's counts, copied into `stats` on the main thread)
  void sync_stats() {
    stats.meshes = built.load();
    stats.mesh_encode_ms = encode_us.load() / 1000.0;
  }

  void start_worker();
  void stop_worker();
  void worker_loop();
  void queue_mesh(MeshJob job);

  /* Messages. During a comparison the broadcasts collect in `outbox` and go out together (one lock per client). */
  std::vector<net::Server::Out> outbox;
  bool batching = false;
  void send(const std::vector<int> &to, bool all, JsonWriter &w, uint64_t key = 0, const std::string &bin = {}) {
    if (batching && all && to.empty()) {
      outbox.push_back({std::move(w.out), bin, key});
      return;
    }
    server.send_to(to, all, w.out, bin, key);
  }
  std::vector<MeshJob> deferred;  // (meshes asked for during a comparison: queued after its batch, never before their object)
  void flush() {
    server.send_batch({}, true, outbox);
    outbox.clear();
    for (MeshJob &j : deferred) queue_mesh(std::move(j));
    deferred.clear();
  }
  int material_key_of(const MaterialPtr &m);
  int expired_key(const std::string &name) const;
  uint64_t material_hash(int key, const MaterialPtr &m);
  void send_material(const std::vector<int> &to, bool all, int key, const MaterialPtr &m);
  Sent capture(GameObject &g);
  void send_object(const std::vector<int> &to, bool all, GameObject &g, const Sent &s);
  void send_light(const std::vector<int> &to, bool all, GameObject &g);
  void send_camera(const std::vector<int> &to, bool all, GameObject &g);
  void send_deform(GameObject &g, const Mesh &m);
  void send_pose(const GameObject &armature, const std::vector<GameObject *> &bones);
  void send_transform(const GameObject &g, const Trs &trs);
  void request_mesh(const std::vector<int> &to, bool all, GameObject &g, const Mesh *m);
  void materials_of(GameObject &g, const std::vector<int> &to, bool all, std::vector<int> &keys, bool only_changed, bool record);

  void sync(Scene &scene, const std::vector<int> &fresh);
  void compare(Scene &scene, double now);
  bool compare_object(GameObject &g);
  void compare_mesh(GameObject &g, Sent &s);
  void finished_meshes(Scene &scene);
};

/* ------------------------------------------------------------ The worker */

void Link::Impl::start_worker() {
  quit = false;
  worker = std::thread([this] { worker_loop(); });
}

void Link::Impl::stop_worker() {
  {
    std::lock_guard<std::mutex> lk(wm);
    quit = true;
    order.clear();
    pending.clear();
  }
  wcv.notify_all();
  if (worker.joinable()) worker.join();
}

void Link::Impl::worker_loop() {
  for (;;) {
    MeshJob job;
    {
      std::unique_lock<std::mutex> lk(wm);
      wcv.wait(lk, [&] { return quit || !order.empty(); });
      if (quit) return;
      const uint64_t id = order.front();
      order.pop_front();
      auto it = pending.find(id);
      if (it == pending.end()) continue;
      job = std::move(it->second);
      pending.erase(it);
      busy++;
    }
    ScopedTimer t;
    ExportOptions o = export_defaults((int)ExportFormat::GLB);
    o.materials = false;  // (the materials go as their own messages; each primitive names its slot)
    o.vertex_ids = true;
    o.triangulate = false;
    const std::string glb = export_gltf({ExportItem{job.name, job.mesh.get(), Mat4::identity(), {}}}, o, true, "", nullptr);
    JsonWriter w;
    w.obj().key("t").str("mesh").key("id").integer((int64_t)job.id).key("topology").integer((int64_t)job.stamp.topology);
    w.key("verts").integer((int64_t)job.mesh->positions.size()).end();
    server.send_to(job.clients, job.broadcast, w.out, glb);  // (no key: a barrier, so no newer deform goes ahead of it)
    encode_us += (uint64_t)(t.ms() * 1000.0);
    built++;
    std::lock_guard<std::mutex> lk(wm);
    done.push_back({job.id, job.stamp});
    busy--;
    wcv.notify_all();
  }
}

/* `job` also goes to whoever `older` was for. */
static void merge_targets(MeshJob &job, const MeshJob &older) {
  for (int c : older.clients)
    if (std::find(job.clients.begin(), job.clients.end(), c) == job.clients.end()) job.clients.push_back(c);
  job.broadcast = job.broadcast || older.broadcast;
}

/* The newest job per object wins; one for new clients and one for everyone merge (each client gets it once). */
void Link::Impl::queue_mesh(MeshJob job) {
  {
    std::lock_guard<std::mutex> lk(wm);
    auto it = pending.find(job.id);
    if (it == pending.end()) {
      order.push_back(job.id);
      pending.emplace(job.id, std::move(job));
    }
    else {
      merge_targets(job, it->second);
      it->second = std::move(job);
    }
  }
  wcv.notify_all();
}

void Link::Impl::request_mesh(const std::vector<int> &to, bool all, GameObject &g, const Mesh *m) {
  if (!m || !m->face_count()) return;  // (glTF has no mesh without triangles)
  MeshJob job;
  job.id = g.id;
  job.mesh = std::make_shared<Mesh>(*m);
  job.name = g.name;
  job.stamp = stamp_of(m);
  job.clients = to;
  job.broadcast = all;
  if (batching) deferred.push_back(std::move(job));
  else queue_mesh(std::move(job));
}

/* ------------------------------------------------------------ Messages */

int Link::Impl::expired_key(const std::string &name) const {
  for (auto &[k, ms] : materials)
    if (ms.ptr.expired() && ms.name == name) return k;
  return 0;
}

int Link::Impl::material_key_of(const MaterialPtr &m) {
  auto it = material_key.find(m.get());
  if (it != material_key.end() && materials[it->second].ptr.lock() == m) return it->second;
  /* A material the scene replaced with a copy (undo, Stop, a reload) keeps its key: one gone, of the same name. */
  int key = expired_key(m->name);
  if (!key) key = next_material++;
  if (it != material_key.end()) material_key.erase(it);
  material_key[m.get()] = key;
  MaterialSent &ms = materials[key];
  ms.ptr = m;
  ms.name = m->name;
  return key;
}

uint64_t Link::Impl::material_hash(int key, const MaterialPtr &m) {
  auto it = material_hash_now.find(key);
  if (it != material_hash_now.end()) return it->second;
  const uint64_t h = hash_reflect([&](Reflector &r) { m->reflect(r); }) ^ std::hash<std::string>()(m->name);
  material_hash_now[key] = h;
  return h;
}

void Link::Impl::send_material(const std::vector<int> &to, bool all, int key, const MaterialPtr &m) {
  JsonWriter w;
  w.obj().key("t").str("material").key("key").integer(key).key("name").str(m->name);
  w.key("gltf").raw(gltf_material_json(*m));
  w.key("maps").obj();
  MapCollector maps(w);
  m->reflect(maps);
  w.end().end();
  send(to, all, w, ckey((uint64_t)key, kMaterial));
  stats.materials++;
}

/* The keys of the object's material slots, each slot's material as the renderer draws it (an empty or missing slot
 * is the default material); a material new to the clients, or changed, is sent first. */
void Link::Impl::materials_of(GameObject &g, const std::vector<int> &to, bool all, std::vector<int> &keys, bool only_changed, bool record) {
  const MeshRenderer *mr = g.mesh_renderer();
  if (!mr) return;
  const MeshFilter *mf = g.mesh_filter();
  const size_t slots = std::max<size_t>({1, mr->materials.size(), mf && mf->mesh ? (size_t)mf->mesh->material_count() : 0});
  for (size_t slot = 0; slot < slots; slot++) {
    const MaterialPtr &m = mr->material((int)slot);
    const int key = material_key_of(m);
    keys.push_back(key);
    const uint64_t h = material_hash(key, m);
    MaterialSent &ms = materials[key];
    if (only_changed && ms.hash == h) continue;
    if (record) ms.hash = h;  // (a sync while others are connected leaves it: compare() must still send them the change)
    send_material(to, all, key, m);
  }
}

Sent Link::Impl::capture(GameObject &g) {
  Sent s;
  s.name = g.name;
  s.kind = kind_of(g);
  s.parent = g.parent ? g.parent->id : 0;
  s.armature = armature_id(g);
  s.active = g.active;
  s.trs = trs_of(g);
  if (Light *l = g.light()) s.light = hash_component(*l);
  if (Camera *c = g.camera()) s.camera = hash_component(*c);
  return s;
}

void Link::Impl::send_object(const std::vector<int> &to, bool all, GameObject &g, const Sent &s) {
  JsonWriter w;
  w.obj().key("t").str("object").key("id").integer((int64_t)g.id).key("name").str(s.name).key("parent").integer((int64_t)s.parent);
  w.key("kind").str(s.kind).key("active").boolean(s.active);
  if (s.armature) w.key("bone_of").integer((int64_t)s.armature);
  write_trs(w, s.trs);
  if (g.mesh_renderer()) {
    w.key("materials").arr();
    for (int k : s.materials) w.integer(k);
    w.end();
  }
  w.end();
  send(to, all, w);
  stats.objects++;
}

void Link::Impl::send_light(const std::vector<int> &to, bool all, GameObject &g) {
  Light &l = *g.light();
  static const char *types[] = {"directional", "point", "spot", "area"};
  const Vec3 c = l.final_color();
  JsonWriter w;
  w.obj().key("t").str("light").key("id").integer((int64_t)g.id).key("type").str(types[std::max(0, std::min(3, l.type))]);
  w.key("color").nums(&c.x, 3).key("intensity").num(l.intensity).key("range").num(l.range);
  w.key("spot_angle").num(l.spot_angle).key("inner_spot_angle").num(l.inner_spot_angle);
  w.key("area").arr().num(l.area_width).num(l.area_height).end().key("disc").boolean(l.area_shape == 1);
  w.key("fields").raw(reflect_to_json([&](Reflector &r) { l.reflect(r); }));
  w.end();
  send(to, all, w, ckey(g.id, kLight));
  stats.lights++;
}

void Link::Impl::send_camera(const std::vector<int> &to, bool all, GameObject &g) {
  Camera &c = *g.camera();
  JsonWriter w;
  w.obj().key("t").str("camera").key("id").integer((int64_t)g.id).key("fov").num(c.fov).key("near").num(c.near_clip).key("far").num(c.far_clip);
  w.key("orthographic").boolean(c.orthographic).key("ortho_size").num(c.ortho_size);
  w.key("physical").boolean(c.physical).key("focal_length").num(c.focal_length);
  w.key("sensor").arr().num(c.sensor_width).num(c.sensor_height).end();
  w.key("fields").raw(reflect_to_json([&](Reflector &r) { c.reflect(r); }));
  w.end();
  send(to, all, w, ckey(g.id, kCamera));
  stats.cameras++;
}

/* Vertices that moved, the faces the same: the positions in the object's space and glTF's axes (float32 x, y, z per
 * vertex of the mesh; the binary's _VERT attribute says which of its vertices each one is). */
void Link::Impl::send_deform(GameObject &g, const Mesh &m) {
  std::string bin(m.positions.size() * 12, '\0');
  float *f = reinterpret_cast<float *>(&bin[0]);
  for (size_t i = 0; i < m.positions.size(); i++) {
    f[i * 3] = -m.positions[i].x;
    f[i * 3 + 1] = m.positions[i].y;
    f[i * 3 + 2] = m.positions[i].z;
  }
  JsonWriter w;
  w.obj().key("t").str("deform").key("id").integer((int64_t)g.id).key("topology").integer((int64_t)m.topology_serial());
  w.key("verts").integer((int64_t)m.positions.size()).end();
  send({}, true, w, ckey(g.id, kDeform), bin);
  stats.deforms++;
}

/* Every bone of the rig, so that a newer pose can replace a queued one whole. */
void Link::Impl::send_pose(const GameObject &armature, const std::vector<GameObject *> &bones) {
  JsonWriter w;
  w.obj().key("t").str("pose").key("id").integer((int64_t)armature.id).key("bones").arr();
  for (const GameObject *b : bones) {
    const Trs t = trs_of(*b);
    w.arr().integer((int64_t)b->id);
    for (float v : t.v) w.num(v);
    w.end();
  }
  w.end().end();
  send({}, true, w, ckey(armature.id, kPose));
  stats.poses++;
}

/* ------------------------------------------------------------ The whole scene, then the differences */

/* New clients get the scene: materials before the objects using them, parents before children, the meshes from the
 * worker. Those already connected are not sent anything here (compare() keeps them up to date). */
void Link::Impl::sync(Scene &scene, const std::vector<int> &fresh) {
  material_hash_now.clear();  // (this moment's hashes: what the new clients get is what is recorded)
  JsonWriter hello;
  hello.obj().key("t").str("hello").key("protocol").str(kProtocol).key("app").str("Blendity").key("axes").str("gltf").key("units").str("m").end();
  send(fresh, false, hello);
  JsonWriter head;
  head.obj().key("t").str("sync").key("scene").str(scene.name).key("objects").integer((int64_t)scene.object_count()).key("playing").boolean(playing).end();
  send(fresh, false, head);
  const bool others = !server.ready_clients().empty();
  if (!others) sent.clear();  // (what nobody holds: no deletes for objects that went while no one listened)
  scene.for_each_ordered([&](GameObject &g, int) {
    Sent s = capture(g);
    materials_of(g, fresh, false, s.materials, false, !others);
    send_object(fresh, false, g, s);
    if (g.light()) send_light(fresh, false, g);
    if (g.camera()) send_camera(fresh, false, g);
    const Mesh *m = s.kind == "mesh" ? g.evaluated_mesh(0) : nullptr;
    s.mesh = stamp_of(m);
    request_mesh(fresh, false, g, m);
    s.seen = true;
    if (!others) sent[g.id] = std::move(s);  // (with clients already there, `sent` is what they have: compare() moves it)
  });
  JsonWriter end;
  end.obj().key("t").str("sync_end").end();
  send(fresh, false, end);
  for (int c : fresh) server.ready(c);
  stats.syncs++;
}

void Link::Impl::compare_mesh(GameObject &g, Sent &s) {
  const Mesh *m = s.kind == "mesh" ? g.evaluated_mesh(0) : nullptr;
  const MeshStamp now = stamp_of(m);
  if (now == s.mesh) return;
  const bool same_faces = m && s.mesh.topology == now.topology && s.mesh.verts == now.verts;
  s.mesh = now;
  if (!m) return;
  if (same_faces) send_deform(g, *m);
  else request_mesh({}, true, g, m);
}

/* Sends what changed about one object; true when it is a bone that moved (its rig's pose is sent once, after). */
bool Link::Impl::compare_object(GameObject &g) {
  Sent now = capture(g);
  materials_of(g, {}, true, now.materials, true, true);
  auto it = sent.find(g.id);
  if (it == sent.end() || !it->second.structure_equal(now)) {
    send_object({}, true, g, now);
    if (g.light()) send_light({}, true, g);
    if (g.camera()) send_camera({}, true, g);
    const bool fresh = it == sent.end();
    if (fresh) it = sent.emplace(g.id, Sent()).first;
    now.mesh = fresh ? MeshStamp() : it->second.mesh;
    it->second = std::move(now);
    it->second.seen = true;
    compare_mesh(g, it->second);
    return false;
  }
  Sent &s = it->second;
  s.seen = true;
  const bool moved = s.trs != now.trs;
  s.trs = now.trs;
  if (moved && !s.armature) send_transform(g, s.trs);
  if (s.light != now.light && g.light()) send_light({}, true, g);
  if (s.camera != now.camera && g.camera()) send_camera({}, true, g);
  s.light = now.light, s.camera = now.camera;
  compare_mesh(g, s);
  return moved && s.armature;
}

void Link::Impl::send_transform(const GameObject &g, const Trs &trs) {
  JsonWriter w;
  w.obj().key("t").str("transform").key("id").integer((int64_t)g.id);
  write_trs(w, trs);
  w.end();
  send({}, true, w, ckey(g.id, kTransform));
  stats.transforms++;
}

void Link::Impl::compare(Scene &scene, double now) {
  ScopedTimer t;
  last_compare = now;
  batching = true;
  material_hash_now.clear();
  for (auto &[id, s] : sent) s.seen = false;
  /* Each rig's bones as they are met; a rig with a moved bone is sent whole, once. */
  std::unordered_map<uint64_t, std::vector<GameObject *>> bones;
  std::vector<uint64_t> moved;
  scene.for_each_ordered([&](GameObject &g, int) {
    const bool bone_moved = compare_object(g);
    const uint64_t arm = sent[g.id].armature;
    if (!arm) return;
    bones[arm].push_back(&g);
    if (bone_moved && std::find(moved.begin(), moved.end(), arm) == moved.end()) moved.push_back(arm);
  });
  for (uint64_t arm : moved)
    if (GameObject *a = scene.find(arm)) send_pose(*a, bones[arm]);
  for (auto it = sent.begin(); it != sent.end();) {
    if (it->second.seen) {
      ++it;
      continue;
    }
    JsonWriter w;
    w.obj().key("t").str("delete").key("id").integer((int64_t)it->first).end();
    send({}, true, w);
    stats.deletes++;
    it = sent.erase(it);
  }
  batching = false;
  flush();
  stats.ticks++;
  stats.tick_ms = t.ms();
  stats.tick_ms_max = std::max(stats.tick_ms_max, stats.tick_ms);
  stats.tick_ms_total += stats.tick_ms;
}

/* A mesh built from a copy older than the scene's mesh now: the positions follow as a deform. */
void Link::Impl::finished_meshes(Scene &scene) {
  std::vector<std::pair<uint64_t, MeshStamp>> list;
  {
    std::lock_guard<std::mutex> lk(wm);
    list.swap(done);
  }
  for (auto &[id, built] : list) {
    auto it = sent.find(id);
    GameObject *g = scene.find(id);
    if (it == sent.end() || !g || it->second.mesh == built) continue;
    const Mesh *m = it->second.kind == "mesh" ? g->evaluated_mesh(0) : nullptr;
    if (m && m->topology_serial() == built.topology && m->positions.size() == built.verts) send_deform(*g, *m);
  }
}

/* ------------------------------------------------------------ Link */

Link::Link() : d_(std::make_unique<Impl>()) {}
Link::~Link() { stop(); }

bool Link::start(int port, std::string *error) {
  stop();
  if (!d_->server.start(port, error)) return false;
  d_->sent.clear();
  d_->materials.clear();
  d_->material_key.clear();
  d_->start_worker();
  return true;
}

void Link::stop() {
  d_->stop_worker();
  d_->server.stop();
  d_->sent.clear();
}

bool Link::running() const { return d_->server.running(); }
int Link::port() const { return d_->server.port(); }
int Link::clients() const { return d_->server.client_count(); }

/* A hello's "app" (and "bridge" version): {"t":"client","app":"Unity 6000.0","bridge":"1.1"}; read, never acted on. */
std::vector<std::string> Link::client_names() const {
  std::vector<std::string> out;
  for (const std::string &h : d_->server.client_hellos()) {
    JsonParser p{h.data(), h.data() + h.size()};
    const Json j = h.empty() ? Json{} : p.value();
    std::string app = j["t"].str == "client" ? j["app"].str : std::string();
    if (app.size() > 80) app.resize(80);
    const std::string bridge = j["bridge"].str.substr(0, 20);
    out.push_back(app.empty() ? std::string("a client") : bridge.empty() ? app : app + " (bridge " + bridge + ")");
  }
  return out;
}
const Stats &Link::stats() const {
  d_->sync_stats();
  return d_->stats;
}
net::Server::Stats Link::net_stats() const { return d_->server.stats(); }
void Link::set_interval_for_test(double seconds) { d_->interval = seconds; }

void Link::wait_meshes_for_test() {
  std::unique_lock<std::mutex> lk(d_->wm);
  d_->wcv.wait(lk, [&] { return d_->order.empty() && d_->busy == 0; });
}

void Link::tick(Scene &scene, bool playing, double now_s) {
  if (!d_->server.running()) return;
  const std::vector<int> fresh = d_->server.take_new_clients();
  const bool any = !d_->server.ready_clients().empty();
  if (!any && fresh.empty()) return;
  if (any && playing != d_->playing) {
    JsonWriter w;
    w.obj().key("t").str("play").key("playing").boolean(playing).end();
    d_->send({}, true, w);
  }
  d_->playing = playing;
  if (any) d_->finished_meshes(scene);
  if (any && now_s - d_->last_compare >= d_->interval) d_->compare(scene, now_s);
  if (!fresh.empty()) d_->sync(scene, fresh);
}

}  // namespace bl::livelink

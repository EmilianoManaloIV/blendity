// SPDX-License-Identifier: GPL-2.0-or-later
// Live Link (task 0172, ADR 0055): see livelink.h, and docs/LIVELINK.md for the protocol.
#include "livelink.h"

#include "../core/core.h"
#include "../core/json.h"
#include "../image/image.h"
#include "../render/canvas.h"
#include "../render/shading.h"
#include "../scene/export.h"
#include "../scene/scene.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace bl::livelink {

namespace {

/* Coalescing keys: a newer message of the same kind about the same thing replaces one still queued. The scene's
 * own (the world, the render's look) are keyed on id 0, which no object has. */
enum Kind : uint64_t { kMesh = 1, kDeform, kTransform, kPose, kLight, kCamera, kMaterial, kWorld, kRender, kFilters, kTexture };
uint64_t ckey(uint64_t id, Kind k) { return id << 4 | (uint64_t)k; }

/* Round 376: the world as Live Link's `world` message - its mode, colours (linear), sky, HDRI (a path on this
 * computer), fog - and every field by its Inspector name. */
std::string world_message(EnvironmentSettings &e) {
  static const char *modes[] = {"gradient", "sky", "hdri", "color"};
  JsonWriter w;
  w.obj().key("t").str("world").key("mode").str(modes[std::clamp(e.mode, 0, 3)]);
  w.key("sky").nums(&e.sky.x, 3).key("equator").nums(&e.equator.x, 3).key("ground").nums(&e.ground.x, 3).key("color").nums(&e.color.x, 3);
  w.key("hdri").str(e.hdri.empty() ? std::string() : resolve_asset_path(e.hdri.path)).key("strength").num(e.strength).key("rotation").num(e.rotation);
  w.key("turbidity").num(e.turbidity);
  w.key("fog").obj().key("enabled").boolean(e.fog).key("start").num(e.fog_start).key("end").num(e.fog_end);
  w.key("color").nums(&e.fog_color.x, 3).key("color_far").nums(&e.fog_color_far.x, 3).end();
  w.key("fields").raw(reflect_to_json([&](Reflector &r) { e.reflect(r); }));
  w.end();
  return w.out;
}

/* Round 376: the scene's render camera as the editor finds it (ADR 0016): the Active Camera, else "Main Camera", else
 * the first enabled camera. */
GameObject *render_camera(Scene &scene) {
  GameObject *chosen = scene.active_camera ? scene.find(scene.active_camera) : nullptr;
  if (chosen && chosen->camera() && chosen->camera()->enabled && chosen->active_in_hierarchy()) return chosen;
  GameObject *first = nullptr, *main = nullptr;
  scene.for_each_ordered([&](GameObject &g, int) {
    if (!g.camera() || !g.camera()->enabled || !g.active_in_hierarchy()) return;
    first = first ? first : &g;
    main = main ? main : g.name == "Main Camera" ? &g : nullptr;
  });
  return main ? main : first;
}

/* Round 376: the render camera's filter stack - each effect by type, on or off, with its fields - as Live Link's
 * `filters` message; id 0 and no effects when there is no camera. */
std::string filters_message(Scene &scene) {
  GameObject *cam = render_camera(scene);
  JsonWriter w;
  w.obj().key("t").str("filters").key("id").integer(cam ? (int64_t)cam->id : 0).key("effects").arr();
  if (CameraFilters *f = cam ? cam->get<CameraFilters>() : nullptr)
    for (const auto &e : f->effects)
      w.obj().key("type").str(e->type_name()).key("enabled").boolean(e->enabled).key("fields").raw(reflect_to_json([&](Reflector &r) { e->reflect(r); })).end();
  w.end().end();
  return w.out;
}

/* Round 376: the render's look - exposure in stops and the view transform by name ("Standard", "Filmic", "ACES", or
 * an OpenColorIO view's). */
std::string render_message(const RenderSettings &rs) {
  static const char *builtin[] = {"Standard", "Filmic", "ACES"};
  const std::vector<std::string> &names = view_transform_names();
  std::string view = rs.view_transform >= 0 && rs.view_transform < 3 ? builtin[rs.view_transform]
                     : rs.view_transform < (int)names.size() ? names[(size_t)rs.view_transform] : std::string("Standard");
  const size_t ocio = view.find(" (OpenColorIO)");
  if (ocio != std::string::npos) view.resize(ocio);
  JsonWriter w;
  w.obj().key("t").str("render").key("exposure").num(rs.exposure).key("view_transform").str(view).end();
  return w.out;
}

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

/* A reflector that takes nothing: the collectors below override what they want. */
struct NoOpReflector : Reflector {
  bool all_fields() const override { return true; }
  void field(const char *, float &, float, float, float) override {}
  void field(const char *, int &, int, int) override {}
  void field(const char *, bool &) override {}
  void field(const char *, Vec3 &) override {}
  void color(const char *, Vec3 &) override {}
  void enumeration(const char *, int &, const char *const *, int) override {}
  void text(const char *, std::string &) override {}
  void mesh(const char *, MeshPtr &) override {}
};

/* The texture slots of a material by their Inspector names, as paths a client on this computer can open, and since
 * round 377 the key of the `texture` message carrying their pixels. */
struct MapCollector : NoOpReflector {
  JsonWriter &w;
  std::function<int(const TextureRef &)> key_of;
  MapCollector(JsonWriter &w_, std::function<int(const TextureRef &)> k) : w(w_), key_of(std::move(k)) {}
  void texture(const char *n, TextureRef &t) override {
    if (t.empty()) return;
    w.key(n).obj().key("path").str(resolve_asset_path(t.path)).key("non_color").boolean(t.non_color).key("texture").integer(key_of(t)).end();
  }
};

/* A material's texture slots, collected (round 377: their pixels are looked at each comparison). */
struct RefCollector : NoOpReflector {
  std::vector<TextureRef> refs;
  void texture(const char *, TextureRef &t) override {
    if (!t.empty()) refs.push_back(t);
  }
};

constexpr uint64_t kFnvOffset = 1469598103934665603ull, kFnvPrime = 1099511628211ull;
constexpr double kTextureMinGap = 0.25;  // (seconds between a texture's sends while it keeps changing: at most 4 a second)
constexpr size_t kStampSamples = 1024;   // (texels a stamp looks at)
constexpr int kMaxTextureSide = 2048;    // (a bigger texture goes at most this size: a connect's copies stay small)

/* Round 377: a texture's look now, cheaply - which image, its size and date, and 1,024 texels spread over it (an image
 * changed in place changes them; the worker's full hash then says whether the pixels really changed). Not the
 * textures' global generation: one texture's change would send every texture again (review 377). */
uint64_t texture_stamp(const Texture *t) {
  if (!t || t->levels.empty()) return 0;
  const Texture::Level &l = t->levels[0];
  uint64_t h = kFnvOffset ^ (uint64_t)(uintptr_t)t ^ ((uint64_t)l.w << 32) ^ (uint64_t)l.h ^ (uint64_t)t->mtime * 31;
  const size_t n = l.px8.empty() ? l.pxf.size() : l.px8.size();
  const size_t step = std::max<size_t>(1, n / kStampSamples);
  for (size_t i = 0; i < n; i += step) {
    const uint64_t v = l.px8.empty() ? (uint64_t)std::hash<float>()(l.pxf[i].x + 2 * l.pxf[i].y + 3 * l.pxf[i].z) : l.px8[i];
    h = (h ^ v) * kFnvPrime;
  }
  return h;
}

/* What every client was last told of one texture slot (a path in a colour space). */
struct TexSent {
  int key = 0;
  TextureRef ref;
  uint64_t stamp = 0;
  double last = -1e30;  // (when it was last sent: at most 4 a second while it keeps changing)
  bool live = false;    // (named by a material now: only those are looked at and sent - review 377)
};

/* A texture's first level copied as it is (at most kMaxTextureSide a side, every n-th texel past it) for the worker,
 * which turns it into PNG pixels: the main thread only copies (review 377). */
struct TexJob {
  int key = 0;
  std::string name;
  bool non_color = false, force = false;  // (force: a new client - sent whatever its hash)
  int w = 0, h = 0;
  std::vector<uint32_t> px8;  // (R | G<<8 | B<<16 | A<<24) - or, for a float image:
  std::vector<Vec4> pxf;      // (linear)
  std::vector<int> clients;
  bool broadcast = false;
};

/* The PNG's pixels (0xAARRGGBB) on the worker: 8-bit as they are, a float image's colour made sRGB unless the slot is
 * non-colour data (review 377: an EXR colour map went linear, too dark), clamped, a NaN black. */
std::vector<uint32_t> png_pixels(const TexJob &j) {
  std::vector<uint32_t> out((size_t)j.w * j.h);
  auto byte = [&](float v, bool colour) {
    const float c = std::isfinite(v) ? std::clamp(colour ? linear_to_srgb(v) : v, 0.0f, 1.0f) : 0.0f;
    return (uint32_t)std::lround(c * 255.0f);
  };
  for (size_t i = 0; i < out.size(); i++) {
    if (!j.px8.empty()) {
      const uint32_t p = j.px8[i];
      out[i] = (p & 0xFF00FF00u) | ((p & 0xFFu) << 16) | ((p >> 16) & 0xFFu);
      continue;
    }
    const Vec4 &f = j.pxf[i];
    out[i] = byte(f.x, !j.non_color) << 16 | byte(f.y, !j.non_color) << 8 | byte(f.z, !j.non_color) | byte(f.w, false) << 24;
  }
  return out;
}

/* A level's texels into the job, every `step`-th along each side (1: a plain copy). */
template <class T>
void copy_texels(const std::vector<T> &src, int w, int step, int ow, int oh, std::vector<T> &out) {
  if (step == 1) return void(out = src);
  out.resize((size_t)ow * oh);
  for (int y = 0; y < oh; y++)
    for (int x = 0; x < ow; x++) out[(size_t)y * ow + x] = src[(size_t)y * step * w + (size_t)x * step];
}

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
  uint64_t world_hash = 0, render_hash = 0, filters_hash = 0;  // (round 376: what every client was last told of the scene's look)

  /* Round 377: the textures by slot, and their worker (PNG encoding off the main thread; the newest job per texture). */
  std::unordered_map<std::string, TexSent> textures;
  int next_texture = 1;
  std::thread tex_worker;
  std::mutex tm;
  std::condition_variable tcv;
  std::deque<int> tex_order;
  std::unordered_map<int, TexJob> tex_pending;
  std::unordered_map<int, uint64_t> tex_hash_sent;  // (by key: the pixels last broadcast - the worker's)
  bool tex_quit = false;
  int tex_busy = 0;
  int texture_key(const TextureRef &t);
  void queue_texture(TexSent &ts, const std::vector<int> &to, bool all, bool force, double now);
  void compare_textures(double now);
  void tex_loop();

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
  std::atomic<uint64_t> tex_sent{0}, tex_same{0};  // (round 377: the texture worker's)
  void sync_stats() {
    stats.meshes = built.load();
    stats.mesh_encode_ms = encode_us.load() / 1000.0;
    stats.textures = tex_sent.load(), stats.textures_same = tex_same.load();
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
  void send_look(Scene &scene, const std::vector<int> &to, bool all);
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
  quit = false, tex_quit = false;
  worker = std::thread([this] { worker_loop(); });
  tex_worker = std::thread([this] { tex_loop(); });
}

/* ------------------------------------------------------------ Textures (round 377) */

/* A texture slot's key: the same path in the same colour space is one texture, whatever material uses it. */
int Link::Impl::texture_key(const TextureRef &t) {
  TexSent &ts = textures[t.path + (t.non_color ? "|data" : "|color")];
  if (!ts.key) ts.key = next_texture++, ts.ref = t;
  ts.live = true;
  return ts.key;
}

/* Its texels copied for the worker (to everyone, or to `to` only when a client is new). */
void Link::Impl::queue_texture(TexSent &ts, const std::vector<int> &to, bool all, bool force, double now) {
  TexturePtr tex = resolve_texture_ref(ts.ref);
  if (!tex || tex->levels.empty() || tex->levels[0].w <= 0) return;
  const Texture::Level &l = tex->levels[0];
  const int step = std::max(1, (std::max(l.w, l.h) + kMaxTextureSide - 1) / kMaxTextureSide);
  TexJob job;
  job.key = ts.key, job.name = texture_ref_label(ts.ref), job.non_color = ts.ref.non_color, job.force = force;
  job.w = (l.w + step - 1) / step, job.h = (l.h + step - 1) / step, job.clients = to, job.broadcast = all;
  if (!l.px8.empty()) copy_texels(l.px8, l.w, step, job.w, job.h, job.px8);
  else copy_texels(l.pxf, l.w, step, job.w, job.h, job.pxf);
  if (all) ts.last = now;
  {
    std::lock_guard<std::mutex> lk(tm);
    auto it = tex_pending.find(job.key);
    if (it == tex_pending.end()) tex_order.push_back(job.key);
    else {
      for (int c : it->second.clients)
        if (std::find(job.clients.begin(), job.clients.end(), c) == job.clients.end()) job.clients.push_back(c);
      job.broadcast = job.broadcast || it->second.broadcast, job.force = job.force || it->second.force;
    }
    tex_pending[job.key] = std::move(job);
  }
  tcv.notify_all();
}

/* Each comparison: the textures the live materials name now, each one whose stamp changed queued - at most 4 times a
 * second while it keeps changing (its last state goes after). A texture no material names any more isn't looked at. */
void Link::Impl::compare_textures(double now) {
  for (auto &[name, ts] : textures) ts.live = false;
  RefCollector rc;
  for (auto &[key, ms] : materials)
    if (MaterialPtr m = ms.ptr.lock()) m->reflect(rc);
  for (const TextureRef &r : rc.refs) texture_key(r);
  for (auto &[name, ts] : textures) {
    if (!ts.live || now - ts.last < kTextureMinGap) continue;
    const uint64_t stamp = texture_stamp(resolve_texture_ref(ts.ref).get());
    if (stamp == ts.stamp) continue;
    ts.stamp = stamp;
    queue_texture(ts, {}, true, false, now);
  }
}

/* The worker: the PNG's pixels made, hashed, a broadcast whose pixels are the same as the last one's dropped; the PNG
 * encoded and sent with its key, name, size and hash. */
void Link::Impl::tex_loop() {
  for (;;) {
    TexJob job;
    {
      std::unique_lock<std::mutex> lk(tm);
      tcv.wait(lk, [&] { return tex_quit || !tex_order.empty(); });
      if (tex_quit) return;
      const int key = tex_order.front();
      tex_order.pop_front();
      auto it = tex_pending.find(key);
      if (it == tex_pending.end()) continue;
      job = std::move(it->second);
      tex_pending.erase(it);
      tex_busy++;
    }
    const std::vector<uint32_t> px = png_pixels(job);
    uint64_t h = kFnvOffset ^ ((uint64_t)job.w << 32 | (uint32_t)job.h);
    for (uint32_t p : px) h = (h ^ p) * kFnvPrime;
    bool same = false;
    {
      std::lock_guard<std::mutex> lk(tm);
      same = job.broadcast && !job.force && tex_hash_sent[job.key] == h;
      if (job.broadcast) tex_hash_sent[job.key] = h;
    }
    if (!same) {
      const std::string png = encode_png(px.data(), job.w, job.h, job.w, true);
      JsonWriter w;
      w.obj().key("t").str("texture").key("key").integer(job.key).key("name").str(job.name).key("width").integer(job.w).key("height").integer(job.h);
      w.key("non_color").boolean(job.non_color).key("hash").str(strprintf("%016llx", (unsigned long long)h)).end();
      server.send_to(job.clients, job.broadcast, w.out, png, ckey((uint64_t)job.key, kTexture));
    }
    (same ? tex_same : tex_sent)++;
    std::lock_guard<std::mutex> lk(tm);
    tex_busy--;
    tcv.notify_all();
  }
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
  {
    std::lock_guard<std::mutex> lk(tm);
    tex_quit = true;
    tex_order.clear();
    tex_pending.clear();
  }
  tcv.notify_all();
  if (tex_worker.joinable()) tex_worker.join();
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
    o.shape_keys = false;  // (round 351: the evaluated mesh has its weights applied; the keys stream as deforms - round 364 sends them as blend shapes)
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
  MapCollector maps(w, [&](const TextureRef &t) { return texture_key(t); });
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

/* Round 376: the world and the render's look, to `to` (a sync) or to everyone when they changed (a comparison). The
 * filters are the render camera's - the one the Game view and F12 use - since each engine has one post-processing
 * of its own: another camera made the render camera, its stack changed, or the camera gone (an empty stack, id 0)
 * each send them (review 376: a deleted camera's Bloom stayed in every engine). */
void Link::Impl::send_look(Scene &scene, const std::vector<int> &to, bool all) {
  const std::string world = world_message(scene.environment), render = render_message(scene.render), filters = filters_message(scene);
  const uint64_t wh = std::hash<std::string>()(world), rh = std::hash<std::string>()(render), fh = std::hash<std::string>()(filters);
  const bool fresh = !all;
  const std::pair<const std::string *, Kind> parts[] = {{&world, kWorld}, {&render, kRender}, {&filters, kFilters}};
  const uint64_t was[] = {world_hash, render_hash, filters_hash}, now[] = {wh, rh, fh};
  for (int i = 0; i < 3; i++) {
    if (!fresh && now[i] == was[i]) continue;
    JsonWriter w;
    w.out = *parts[i].first;
    send(to, all, w, ckey(0, parts[i].second));
    (i == 0 ? stats.worlds : i == 1 ? stats.renders : stats.filters)++;
  }
  if (all || server.ready_clients().empty()) world_hash = wh, render_hash = rh, filters_hash = fh;
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
  send_look(scene, fresh, false);  // (round 376: the world and the look before the objects)
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
  for (auto &[name, ts] : textures) {  // (round 377: every texture its materials name, to the new clients; encoded on the worker)
    if (!ts.live) continue;
    if (!others) ts.stamp = texture_stamp(resolve_texture_ref(ts.ref).get());  // (with others there, compare() still owes them a change - review 377)
    queue_texture(ts, fresh, false, true, last_compare);
  }
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
  send_look(scene, {}, true);  // (round 376: the world and the look, when they changed)
  compare_textures(now);       // (round 377: a texture painted, baked or reloaded)
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
  d_->textures.clear(), d_->tex_hash_sent.clear(), d_->next_texture = 1;  // (round 377: a new session's own)
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
  {
    std::unique_lock<std::mutex> lk(d_->wm);
    d_->wcv.wait(lk, [&] { return d_->order.empty() && d_->busy == 0; });
  }
  std::unique_lock<std::mutex> lk(d_->tm);  // (round 377: the textures too)
  d_->tcv.wait(lk, [&] { return d_->tex_order.empty() && d_->tex_busy == 0; });
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

// SPDX-License-Identifier: GPL-2.0-or-later
// Live Link (task 0172, ADR 0055): the scene sent as it changes to other programs on this computer - Blender, Unity,
// Godot, Unreal through their bridges - over a local TCP connection, one way. The protocol, blendity-livelink/1, is
// in docs/LIVELINK.md: lines of JSON, meshes as glTF binaries, in glTF's axes (right-handed, Y up, metres).
//
// Each frame, tick() compares the scene with what the clients were last sent and sends the difference: a moved
// object as one `transform`, a posed rig as one `pose`, vertices that moved (a drag in Edit Mode, skinning, a
// deformer) as one `deform`, new faces as a whole `mesh` (a glTF binary built on a worker thread, so a big mesh never
// stalls the frame), materials, lights and cameras as they change. A new client first gets the whole scene.
#pragma once

#include "../core/net_server.h"

#include <cstdint>
#include <memory>
#include <string>

namespace bl {

class Scene;

namespace livelink {

constexpr const char *kProtocol = "blendity-livelink/1";
constexpr int kDefaultPort = 40100;

struct Stats {
  uint64_t syncs = 0, objects = 0, transforms = 0, poses = 0, meshes = 0, deforms = 0, materials = 0, lights = 0,
           cameras = 0, deletes = 0;
  uint64_t ticks = 0;          // ticks that compared the scene (at most 60 a second, none without a client)
  double tick_ms = 0;          // the last one's time on the main thread
  double tick_ms_max = 0;
  double tick_ms_total = 0;    // over every comparison
  double mesh_encode_ms = 0;   // the worker's time building glTF binaries, in all
};

class Link {
 public:
  Link();
  ~Link();
  Link(const Link &) = delete;
  Link &operator=(const Link &) = delete;

  bool start(int port, std::string *error = nullptr);  // 0: a free port (port() says which)
  void stop();
  bool running() const;
  int port() const;
  int clients() const;

  /* Once a frame, on the main thread: new clients get the whole scene, every client what changed since the last
   * comparison (at most every 1/60 s). Nothing at all happens while no client is connected. */
  void tick(Scene &scene, bool playing, double now_s);

  const Stats &stats() const;
  net::Server::Stats net_stats() const;
  /* Tests: compare every tick (0) instead of at most 60 times a second; wait for the mesh worker to finish. */
  void set_interval_for_test(double seconds);
  void wait_meshes_for_test();

  struct Impl;

 private:
  std::unique_ptr<Impl> d_;
};

}  // namespace livelink
}  // namespace bl

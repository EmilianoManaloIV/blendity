// SPDX-License-Identifier: GPL-2.0-or-later
// Window > Live Link and the console's `livelink` (task 0172, ADR 0055): start and stop the server, see who is
// connected and what went out. The link itself is livelink.cpp.
#include "editor.h"

#include <cstdlib>

namespace bl {

bool Editor::livelink_start(int port) {
  if (!livelink_) livelink_ = std::make_unique<livelink::Link>();
  std::string err;
  if (!livelink_->start(port, &err)) {
    livelink_error_ = err;
    Log::warn("Live Link: couldn't listen on port %d: %s", port, err.c_str());
    return false;
  }
  livelink_error_.clear();
  Log::info("Live Link: listening on 127.0.0.1:%d (%s); bridges in Blender, Unity, Godot or Unreal connect there", livelink_->port(),
            livelink::kProtocol);
  return true;
}

void Editor::livelink_stop() {
  if (!livelink_ || !livelink_->running()) return;
  livelink_->stop();
  Log::info("Live Link: stopped");
}

void Editor::livelink_tick() {
  if (livelink_ && scene_) livelink_->tick(*scene_, playing_, now_seconds());
}

void Editor::livelink_status() {
  if (!livelink_ || !livelink_->running()) {
    Log::info("Live Link: off (livelink start [port]; the default port is %d)", livelink::kDefaultPort);
    return;
  }
  const livelink::Stats &s = livelink_->stats();
  const net::Server::Stats n = livelink_->net_stats();
  Log::info("Live Link: 127.0.0.1:%d, %d client(s); sent %llu messages, %.1f MB", livelink_->port(), livelink_->clients(),
            (unsigned long long)n.messages, n.bytes / 1048576.0);
  Log::info("  objects %llu, transforms %llu, poses %llu, meshes %llu, deforms %llu, materials %llu, lights %llu, cameras %llu, deletes %llu",
            (unsigned long long)s.objects, (unsigned long long)s.transforms, (unsigned long long)s.poses, (unsigned long long)s.meshes,
            (unsigned long long)s.deforms, (unsigned long long)s.materials, (unsigned long long)s.lights, (unsigned long long)s.cameras,
            (unsigned long long)s.deletes);
}

/* livelink start [port] | stop | status */
void Editor::livelink_command(const std::vector<std::string> &t) {
  const std::string verb = t.size() > 1 ? to_lower(t[1]) : "status";
  if (verb == "start") {
    livelink_start(t.size() > 2 ? std::atoi(t[2].c_str()) : livelink_port_);
    return;
  }
  if (verb == "stop") {
    livelink_stop();
    return;
  }
  if (verb == "status") {
    livelink_status();
    return;
  }
  Log::warn("livelink start [port] | stop | status (port 0: any free one)");
}

void Editor::draw_livelink_window(const Recti &r) {
  auto &u = ui_;
  ui::Layout lay{{r.x + u.px(10), r.y, r.w - u.px(20), r.h}, r.y + u.px(8)};
  lay.row_h = u.row_h();
  const bool on = livelink_ && livelink_->running();
  Recti row = lay.row();
  u.label({row.x, row.y, u.px(40), row.h}, "Port", u.theme.text_dim);
  u.int_field(u.id("ll_port"), {row.x + u.px(44), row.y, u.px(90), row.h}, livelink_port_, 0, 65535);
  u.tooltip("The TCP port on 127.0.0.1 the bridges connect to (0: any free one). Only this computer can connect.");
  if (u.button({row.x + u.px(142), row.y, u.px(90), row.h}, on ? "Stop" : "Start")) {
    if (on) livelink_stop();
    else livelink_start(livelink_port_);
  }
  if (!livelink_error_.empty()) u.label(lay.row(), "Couldn't start: " + livelink_error_, u.theme.accent);
  if (!on) {
    u.label(lay.row(), "Off. Start it, then connect a bridge (Blender: the Blendity Live Link add-on; Unity: the package).", u.theme.text_dim);
    u.label(lay.row(), "The protocol is docs/LIVELINK.md: one way, lines of JSON and glTF binaries.", u.theme.text_dim);
    return;
  }
  const livelink::Stats &s = livelink_->stats();
  const net::Server::Stats n = livelink_->net_stats();
  u.label(lay.row(), strprintf("Listening on 127.0.0.1:%d - %d client(s)", livelink_->port(), livelink_->clients()), u.theme.text_bright);
  auto stat = [&](const std::string &k, const std::string &v) {
    Recti rr = lay.row();
    if (u.record_buttons) u.button_rects[k] = rr;
    u.label({rr.x, rr.y, rr.w / 2, rr.h}, k, u.theme.text_dim);
    u.label({rr.x + rr.w / 2, rr.y, rr.w / 2, rr.h}, v);
  };
  stat("Sent", strprintf("%llu messages, %.2f MB", (unsigned long long)n.messages, n.bytes / 1048576.0));
  stat("Objects / transforms / poses", strprintf("%llu / %llu / %llu", (unsigned long long)s.objects, (unsigned long long)s.transforms, (unsigned long long)s.poses));
  stat("Meshes / deforms", strprintf("%llu / %llu (built in %.0f ms)", (unsigned long long)s.meshes, (unsigned long long)s.deforms, s.mesh_encode_ms));
  stat("Materials / lights / cameras", strprintf("%llu / %llu / %llu", (unsigned long long)s.materials, (unsigned long long)s.lights, (unsigned long long)s.cameras));
  stat("Replaced before sending", strprintf("%llu", (unsigned long long)n.coalesced));
  stat("Comparing the scene", strprintf("%.2f ms (most %.2f)", s.tick_ms, s.tick_ms_max));
}

}  // namespace bl

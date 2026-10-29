// SPDX-License-Identifier: GPL-2.0-or-later
// Window > Live Link and the console's `livelink` (task 0172, ADR 0055): start and stop the server, see who is
// connected and what went out. The link itself is livelink.cpp.
//
// Task 0274 (ADR 0086): Live Link starts with Blendity (a preference, on by default), and while it listens a discovery
// file - ~/.blendity/livelink.json, beside the preferences - says where, so a bridge in Blender, Unity, Godot or Unreal
// connects without a port typed and finds it again after Blendity restarts. Port 40100 taken: the next free of
// 40100-40109.
#include "editor.h"

#include "../core/json.h"
#include "../core/releases.h"

#include <cstdlib>
#include <ctime>

#ifdef _WIN32
#include <process.h>
#define BL_GETPID _getpid
#else
#include <unistd.h>
#define BL_GETPID getpid
#endif

namespace bl {

namespace {
constexpr int kPortTries = 10;  // 40100-40109: the bridges' discovery file says which
}

std::string Editor::livelink_discovery_path() const { return fs::join(fs::parent(prefs_path_), "livelink.json"); }

/* Where the bridges look: the protocol, the port, this process and its scene. */
void Editor::livelink_write_discovery() {
  if (!livelink_ || !livelink_->running()) return;
  JsonWriter w;
  w.obj().key("protocol").str(livelink::kProtocol).key("port").integer(livelink_->port()).key("pid").integer((int64_t)BL_GETPID());
  w.key("app").str("Blendity").key("version").str(releases::version());
  w.key("scene").str(scene_ && !scene_->path.empty() ? fs::stem(scene_->path) : std::string("Untitled"));
  w.key("started").integer((int64_t)std::time(nullptr)).end();
  const std::string path = livelink_discovery_path(), tmp = path + ".tmp";
  livelink_discovery_ = fs::write_file(tmp, w.out + "\n");  // (whole, then in its place: a bridge never reads half a file)
  if (livelink_discovery_) fs::remove_file(path), livelink_discovery_ = fs::move(tmp, path);
  if (!livelink_discovery_) Log::warn("Live Link: couldn't write %s - bridges need their port typed", livelink_discovery_path().c_str());
}

/* Removed when it is this process's: another Blendity started since owns the file now. */
void Editor::livelink_remove_discovery() {
  if (!livelink_discovery_) return;
  livelink_discovery_ = false;
  std::string text;
  if (!fs::read_file(livelink_discovery_path(), text)) return;
  JsonParser p{text.data(), text.data() + text.size()};
  const Json j = p.value();
  if (j["pid"].integer() != (int)BL_GETPID()) return;
  fs::remove_file(livelink_discovery_path());
}

bool Editor::livelink_start(int port) {
  if (!livelink_) livelink_ = std::make_unique<livelink::Link>();
  livelink_remove_discovery();  // (a restart on another port: the old file goes first)
  std::string err;
  const bool range = port >= livelink::kDefaultPort && port < livelink::kDefaultPort + kPortTries;
  const int last = range ? livelink::kDefaultPort + kPortTries - 1 : port;
  int tried = port;
  bool ok = livelink_->start(tried, &err);
  while (!ok && tried < last) ok = livelink_->start(++tried, &err);
  if (!ok) {
    livelink_error_ = err;
    Log::warn("Live Link: couldn't listen on port %d%s: %s", port, range && port != last ? strprintf("-%d", last).c_str() : "", err.c_str());
    return false;
  }
  livelink_error_.clear();
  livelink_note_ = tried != port ? strprintf("Port %d was taken: listening on %d (the bridges read it from the discovery file).", port, tried) : std::string();
  livelink_write_discovery();
  Log::info("Live Link: listening on 127.0.0.1:%d (%s); bridges in Blender, Unity, Godot or Unreal connect there", livelink_->port(),
            livelink::kProtocol);
  return true;
}

void Editor::livelink_stop() {
  livelink_remove_discovery();
  if (!livelink_ || !livelink_->running()) return;
  livelink_->stop();
  livelink_note_.clear();
  Log::info("Live Link: stopped");
}

/* At start: on unless the preference is off; a check or a test only when asked (BLENDITY_LIVELINK_AUTOSTART=1). */
void Editor::livelink_autostart() {
  const char *env = std::getenv("BLENDITY_LIVELINK_AUTOSTART");
  const bool asked = env && std::atoi(env) != 0;
  if (headless_ && !asked) return;
  if (!livelink_autostart_ && !asked) return;
  livelink_start(livelink_port_);
}

bool Editor::livelink_pref(const std::string &k, const std::string &v) {
  if (k == "livelink_autostart") return livelink_autostart_ = std::atoi(v.c_str()) != 0, true;
  if (k == "livelink_port") return livelink_port_ = std::max(0, std::min(65535, std::atoi(v.c_str()))), true;
  return false;
}

std::string Editor::livelink_prefs_text() const {
  return strprintf("livelink_autostart=%d\nlivelink_port=%d\n", livelink_autostart_ ? 1 : 0, livelink_port_);
}

void Editor::livelink_tick() {
  bridge_job_poll();  // (task 0278)
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
  for (const std::string &c : livelink_->client_names()) Log::info("  connected: %s", c.c_str());
  Log::info("  objects %llu, transforms %llu, poses %llu, meshes %llu, deforms %llu, materials %llu, lights %llu, cameras %llu, deletes %llu",
            (unsigned long long)s.objects, (unsigned long long)s.transforms, (unsigned long long)s.poses, (unsigned long long)s.meshes,
            (unsigned long long)s.deforms, (unsigned long long)s.materials, (unsigned long long)s.lights, (unsigned long long)s.cameras,
            (unsigned long long)s.deletes);
  if (livelink_discovery_) Log::info("  discovery file: %s", livelink_discovery_path().c_str());
}

/* livelink start [port] | stop | status | install ... */
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
  if (verb == "install") {
    livelink_install_command(t);
    return;
  }
  Log::warn("livelink start [port] | stop | status | install blender | install unity|godot|unreal <project folder> (port 0: any free one)");
}

/* Preferences > Live Link: started with Blendity, and the port. */
void Editor::draw_livelink_settings() {
  auto &u = ui_;
  Recti r = u.popup_row(u.row_h() + u.px(4));
  u.label({r.x + u.px(12), r.y, u.px(140), r.h}, "Live Link");
  const Recti cb{r.x + u.px(160), r.y + u.px(2), r.h - u.px(4), r.h - u.px(4)};
  if (u.record_buttons) u.button_rects["pref:livelink_autostart"] = cb;
  if (u.checkbox(cb, livelink_autostart_)) save_prefs();
  u.label({cb.right() + u.px(6), r.y, u.px(170), r.h}, "Start with Blendity");
  u.tooltip("On: Live Link listens from the start and writes ~/.blendity/livelink.json, so the Blender, Unity, Godot and\n"
            "Unreal bridges connect by themselves. Off: Window > Live Link > Start.");
  u.label({r.x + u.px(360), r.y, u.px(40), r.h}, "Port", u.theme.text_dim);
  if (u.int_field(u.id("pref_ll_port"), {r.x + u.px(400), r.y + u.px(2), u.px(80), r.h - u.px(4)}, livelink_port_, 0, 65535)) save_prefs();
  u.tooltip("The first port tried (40100: the next free up to 40109 when taken; 0: any free one).");
}

void Editor::draw_livelink_window(const Recti &r) {
  auto &u = ui_;
  ui::Layout lay{{r.x + u.px(10), r.y, r.w - u.px(20), r.h}, r.y + u.px(8)};
  lay.row_h = u.row_h();
  const bool on = livelink_ && livelink_->running();
  Recti row = lay.row();
  u.label({row.x, row.y, u.px(40), row.h}, "Port", u.theme.text_dim);
  if (u.int_field(u.id("ll_port"), {row.x + u.px(44), row.y, u.px(90), row.h}, livelink_port_, 0, 65535)) save_prefs();
  u.tooltip("The TCP port on 127.0.0.1 the bridges connect to (0: any free one). Only this computer can connect.");
  const Recti sb{row.x + u.px(142), row.y, u.px(90), row.h};
  if (u.record_buttons) u.button_rects["livelink:start"] = sb;
  if (u.button(sb, on ? "Stop" : "Start")) {
    if (on) livelink_stop();
    else livelink_start(livelink_port_);
  }
  const Recti ab{sb.right() + u.px(10), row.y + u.px(2), row.h - u.px(4), row.h - u.px(4)};
  if (u.record_buttons) u.button_rects["livelink:autostart"] = ab;
  if (u.checkbox(ab, livelink_autostart_)) save_prefs();
  u.label({ab.right() + u.px(6), row.y, u.px(160), row.h}, "Start with Blendity", u.theme.text_dim);
  if (!livelink_error_.empty()) u.label(lay.row(), "Couldn't start: " + livelink_error_, u.theme.accent);
  lay.space(u.px(4));
  draw_bridge_install(lay);  // (task 0278)
  lay.space(u.px(4));
  if (!on) {
    u.label(lay.row(), "Off. Start it, and the bridges (Blender, Unity, Godot, Unreal) connect by themselves.", u.theme.text_dim);
    u.label(lay.row(), "The protocol is docs/LIVELINK.md: one way, lines of JSON and glTF binaries.", u.theme.text_dim);
    return;
  }
  const livelink::Stats &s = livelink_->stats();
  const net::Server::Stats n = livelink_->net_stats();
  u.label(lay.row(), strprintf("Listening on 127.0.0.1:%d - %d client(s)", livelink_->port(), livelink_->clients()), u.theme.text_bright);
  if (!livelink_note_.empty()) u.label(lay.row(), livelink_note_, u.theme.text_dim);
  for (const std::string &c : livelink_->client_names()) {
    const Recti cr = lay.row();
    if (u.record_buttons) u.button_rects["livelink:client " + c] = cr;
    u.label({cr.x + u.px(12), cr.y, cr.w - u.px(12), cr.h}, "Connected: " + c);
  }
  if (livelink_discovery_) {
    const Recti dr = lay.row();
    u.label(dr, "Bridges find it through " + livelink_discovery_path(), u.theme.text_dim);
    u.tooltip("The discovery file: the port and this scene, written while Live Link listens and removed when it stops.");
  }
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

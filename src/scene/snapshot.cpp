// SPDX-License-Identifier: GPL-2.0-or-later
// Send to Blendity's snapshot.json (round 374, task 0351): see snapshot.h.
#include "snapshot.h"

#include "../core/core.h"

#include <algorithm>
#include <cmath>

namespace bl {

namespace {

float num(const Json &j, const char *k, float d) {
  const double v = j[k].number(d);
  return std::isfinite(v) ? (float)v : d;
}

Vec3 vec3(const Json &j, Vec3 d) {
  if (j.size() < 3) return d;
  const Vec3 v((float)j[0].number(d.x), (float)j[1].number(d.y), (float)j[2].number(d.z));
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) ? v : d;
}

/* A property's value as text: strings as they are, numbers in their shortest form, the rest as JSON-ish. */
std::string value_text(const Json &j) {
  if (j.type == Json::Str) return j.str;
  if (j.type == Json::Num) return strprintf("%.9g", j.num);
  if (j.type == Json::Bool) return j.b ? "true" : "false";
  if (j.type != Json::Arr) return j.type == Json::Null ? "null" : "{...}";
  std::string s = "[";
  for (size_t i = 0; i < j.arr.size(); i++) s += (i ? ", " : "") + value_text(j.arr[i]);
  return s + "]";
}

bool read_transform(const Json &t, Transform &out) {
  if (t.type != Json::Obj) return false;
  out.position = vec3(t["position"], Vec3());
  out.scale = vec3(t["scale"], Vec3(1.0f));
  const Json &r = t["rotation"];
  if (r.size() >= 4) out.rotation = normalize(Quat{(float)r[0].number(), (float)r[1].number(), (float)r[2].number(), (float)r[3].number(1)});
  if (!std::isfinite(out.rotation.x + out.rotation.y + out.rotation.z + out.rotation.w)) out.rotation = Quat();
  out.euler_hint = out.rotation.to_euler();
  return true;
}

}  // namespace

bool read_snapshot(const std::string &text, Snapshot &out, std::string &why) {
  JsonParser p{text.data(), text.data() + text.size()};
  const Json j = p.value();
  if (!p.ok || j.type != Json::Obj || j["snapshot"].integer(0) != 1) return why = "not a Blendity snapshot (snapshot.json with \"snapshot\": 1)", false;
  out.source = j["source"].str, out.name = j["name"].str, out.model = j["model"].str;
  if (out.model.empty() || out.model.find("..") != std::string::npos || out.model.find_first_of("/\\:") != std::string::npos)
    return why = "its model isn't a file beside snapshot.json", false;  // (only a name: nothing outside the folder)
  const Json &mr = j["model_rotation"];
  if (mr.size() >= 4) out.model_rotation = normalize(Quat{(float)mr[0].number(), (float)mr[1].number(), (float)mr[2].number(), (float)mr[3].number(1)});
  if (!std::isfinite(out.model_rotation.x + out.model_rotation.y + out.model_rotation.z + out.model_rotation.w)) out.model_rotation = Quat();
  for (const Json &o : j["objects"].arr) {
    if (out.objects.size() >= Snapshot::kMaxObjects) break;
    SnapshotObject s;
    s.name = o["name"].str, s.path = o["path"].str.empty() ? s.name : o["path"].str, s.engine_class = o["class"].str;
    for (const auto &[k, v] : o["props"].obj) s.props += k + " = " + value_text(v) + "\n";
    s.has_light = o["light"].type == Json::Obj, s.light = o["light"];
    s.has_camera = o["camera"].type == Json::Obj, s.camera = o["camera"];
    s.has_transform = read_transform(o["transform"], s.transform);
    if (!s.name.empty()) out.objects.push_back(std::move(s));
  }
  if (out.name.empty()) out.name = fs::stem(out.model);
  return true;
}

std::string snapshot_engine(const Snapshot &s) {
  const size_t sp = s.source.find(' ');
  return s.source.empty() ? std::string("Other") : s.source.substr(0, sp);
}

void snapshot_light(const Json &j, Light &l) {
  const std::string type = j["type"].str;
  l.type = type == "directional" ? 0 : type == "spot" ? 2 : type == "area" ? 3 : 1;
  l.color = vec3(j["color"], Vec3(1.0f));
  l.intensity = std::max(num(j, "intensity", 1.0f), 0.0f);
  l.range = std::max(num(j, "range", 10.0f), 0.001f);
  l.spot_angle = std::clamp(num(j, "spot_angle", 30.0f), 0.1f, 179.0f);
  constexpr float kUnityInnerShare = 21.8f / 30.0f;  // (Unity's default inner angle against its outer one)
  l.inner_spot_angle = std::clamp(num(j, "inner_spot_angle", l.spot_angle * kUnityInnerShare), 0.0f, l.spot_angle);
  const Json &area = j["area"];
  l.area_width = std::max((float)area[0].number(1.0), 0.001f), l.area_height = std::max((float)area[1].number(1.0), 0.001f);
  l.area_shape = j["disc"].b ? 1 : 0;
}

void snapshot_camera(const Json &j, Camera &c) {
  c.fov = std::clamp(num(j, "fov", 60.0f), 1.0f, 179.0f);
  c.near_clip = std::max(num(j, "near", 0.1f), 0.0001f);
  c.far_clip = std::max(num(j, "far", 1000.0f), c.near_clip + 0.001f);
  c.orthographic = j["orthographic"].b;
  c.ortho_size = std::max(num(j, "ortho_size", 5.0f), 0.001f);
  c.physical = j["physical"].b;
  c.focal_length = std::max(num(j, "focal_length", 50.0f), 0.1f);
  const Json &sensor = j["sensor"];
  c.sensor_width = std::max((float)sensor[0].number(36.0), 0.1f), c.sensor_height = std::max((float)sensor[1].number(24.0), 0.1f);
  c.sensor_preset = 0;  // (Custom: a preset would write its own sizes back over these - review 374)
  c.sensor_fit = std::clamp(j["sensor_fit"].integer(0), 0, 2);
  const Json &shift = j["shift"];
  c.shift_x = (float)shift[0].number(0.0), c.shift_y = (float)shift[1].number(0.0);
}

}  // namespace bl

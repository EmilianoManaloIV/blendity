// SPDX-License-Identifier: GPL-2.0-or-later
// A reference board's items, their frames and the board's file (task 0299, ADR 0089).
#include "board.h"

#include "../core/json.h"

#include <cmath>

namespace bl {

Vec2 BoardItem::to_local(Vec2 p) const {
  const Vec2 s = size();
  const float a = -angle * kDeg2Rad, c = std::cos(a), sn = std::sin(a);
  const Vec2 d = p - pos;
  Vec2 l{d.x * c - d.y * sn, d.x * sn + d.y * c};  // (turned back)
  l = {s.x > 1e-6f ? l.x / s.x + 0.5f : 0.5f, s.y > 1e-6f ? l.y / s.y + 0.5f : 0.5f};
  if (flip_x) l.x = 1.0f - l.x;
  if (flip_y) l.y = 1.0f - l.y;
  return l;
}

Vec2 BoardItem::from_local(Vec2 l) const {
  if (flip_x) l.x = 1.0f - l.x;
  if (flip_y) l.y = 1.0f - l.y;
  const Vec2 s = size();
  const Vec2 d{(l.x - 0.5f) * s.x, (l.y - 0.5f) * s.y};
  const float a = angle * kDeg2Rad, c = std::cos(a), sn = std::sin(a);
  return pos + Vec2{d.x * c - d.y * sn, d.x * sn + d.y * c};
}

bool BoardItem::contains(Vec2 p) const {
  const Vec2 s = size();
  if (s.x <= 1e-6f || s.y <= 1e-6f) return false;  // (no size yet - an image still loading - catches nothing)
  const Vec2 l = to_local(p);
  return l.x >= 0.0f && l.y >= 0.0f && l.x <= 1.0f && l.y <= 1.0f;
}

void BoardItem::corners(Vec2 out[4]) const {
  BoardItem plain = *this;  // (the corners named as seen on the board: a flip doesn't move them)
  plain.flip_x = plain.flip_y = false;
  out[0] = plain.from_local({0, 0}), out[1] = plain.from_local({1, 0}), out[2] = plain.from_local({1, 1}), out[3] = plain.from_local({0, 1});
}

BoardItem *Board::find(uint64_t id) {
  for (BoardItem &it : items)
    if (it.id == id) return &it;
  return nullptr;
}

const BoardItem *Board::find(uint64_t id) const {
  for (const BoardItem &it : items)
    if (it.id == id) return &it;
  return nullptr;
}

uint64_t Board::item_at(Vec2 p) const {
  for (size_t i = items.size(); i-- > 0;)
    if (items[i].contains(p)) return items[i].id;
  return 0;
}

bool Board::bounds(const std::vector<uint64_t> &ids, Vec2 &lo, Vec2 &hi) const {
  lo = {1e30f, 1e30f}, hi = {-1e30f, -1e30f};
  bool any = false;
  for (const BoardItem &it : items) {
    bool wanted = ids.empty();
    for (uint64_t id : ids) wanted = wanted || id == it.id;
    if (!wanted) continue;
    Vec2 c[4];
    it.corners(c);
    for (const Vec2 &p : c) lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)}, hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
    any = true;
  }
  return any;
}

/* ---- the file ---- */

std::string board_to_json(const Board &b) {
  JsonWriter w;
  w.obj().key("blendity_board").integer(1).key("name").str(b.name).key("zoom").num(b.zoom).key("pan").nums(&b.pan.x, 2);
  w.key("items").arr();
  for (const BoardItem &it : b.items) {
    w.obj().key("kind").integer(it.kind).key("path").str(it.path).key("text").str(it.text).key("color").integer(it.color);
    w.key("pos").nums(&it.pos.x, 2).key("w").num(it.w).key("h").num(it.h).key("scale").num(it.scale).key("angle").num(it.angle);
    w.key("flip_x").boolean(it.flip_x).key("flip_y").boolean(it.flip_y).key("crop").nums(it.crop, 4);
    w.key("gray").boolean(it.gray).key("locked").boolean(it.locked).key("opacity").num(it.opacity).key("float_pad").integer(it.float_pad);
    w.end();
  }
  w.end();
  w.end();
  return w.out + "\n";
}

namespace {
float finite_or(double v, float def) { return std::isfinite(v) ? (float)v : def; }
float field(const Json &j, const char *k, float def) { return finite_or(j[k].number(def), def); }
bool flag(const Json &j, const char *k) { return j[k].type == Json::Bool && j[k].b; }
}  // namespace

bool board_from_json(const std::string &text, Board &b, std::string &err) {
  JsonParser p{text.data(), text.data() + text.size()};
  const Json root = p.value();
  if (!p.ok || root.type != Json::Obj || root["blendity_board"].type != Json::Num) {
    err = "not a Blendity board";
    return false;
  }
  Board out;
  if (root["name"].type == Json::Str) out.name = root["name"].str;
  out.zoom = std::max(0.01f, std::min(64.0f, field(root, "zoom", 1.0f)));
  out.pan = {finite_or(root["pan"][0].number(0), 0.0f), finite_or(root["pan"][1].number(0), 0.0f)};
  const Json &items = root["items"];
  for (size_t i = 0; i < items.size(); i++) {
    const Json &j = items[i];
    BoardItem it;
    it.kind = std::max(0, std::min(2, j["kind"].integer(0)));
    it.id = out.next_id++;
    it.path = j["path"].str, it.text = j["text"].str;
    it.color = (uint32_t)j["color"].number(0xFFFFFFFF);
    it.pos = {finite_or(j["pos"][0].number(0), 0.0f), finite_or(j["pos"][1].number(0), 0.0f)};
    it.w = std::max(0.0f, field(j, "w", 0.0f)), it.h = std::max(0.0f, field(j, "h", 0.0f));
    it.scale = std::max(1e-4f, std::min(1e4f, field(j, "scale", 1.0f))), it.angle = field(j, "angle", 0.0f);
    it.flip_x = flag(j, "flip_x"), it.flip_y = flag(j, "flip_y");
    for (int k = 0; k < 4; k++) it.crop[k] = std::max(0.0f, std::min(1.0f, finite_or(j["crop"][k].number(k < 2 ? 0 : 1), k < 2 ? 0.0f : 1.0f)));
    if (it.crop[2] <= it.crop[0] || it.crop[3] <= it.crop[1]) it.crop[0] = it.crop[1] = 0.0f, it.crop[2] = it.crop[3] = 1.0f;
    it.gray = flag(j, "gray"), it.locked = flag(j, "locked");
    it.opacity = std::max(0.0f, std::min(1.0f, field(j, "opacity", 1.0f)));
    it.float_pad = std::max(0, j["float_pad"].integer(0));
    out.items.push_back(std::move(it));
  }
  b = std::move(out);
  return true;
}

}  // namespace bl

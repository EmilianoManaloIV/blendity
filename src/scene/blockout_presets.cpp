// SPDX-License-Identifier: GPL-2.0-or-later
// Architectural presets of a Blockout (round 500, task 0484): a column, a pilaster, a beam, a railing, a roof, stairs with a landing, spiral stairs and an arcade are
// each ONE BlockoutPiece whose numbers this file makes into kernel pieces (the same road a styled window takes: generated ids that hold the piece's id, no child
// objects). Every kernel piece is an Add solid; a roof's top faces are told from the rest by the id (blockout_preset_face_class).
#include "blockout_object.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace bl {

const char *const kBlockoutPresetNames[kBlockoutPresets] = {"None", "Column", "Pilaster", "Beam", "Railing", "Roof", "Stairs L", "Stairs U", "Spiral Stairs", "Arcade"};
const char *const kBlockoutRoofStyleNames[kBlockoutRoofStyles] = {"Gable", "Hip", "Shed"};

namespace {

std::string lower(const std::string &s) {
  std::string r;
  for (char c : s) r += (char)std::tolower((unsigned char)c);
  return r;
}

}  // namespace

int blockout_preset_from_words(const std::vector<std::string> &words, size_t first, size_t &used) {
  used = 0;
  if (first >= words.size()) return 0;
  const std::string w = lower(words[first]), next = first + 1 < words.size() ? lower(words[first + 1]) : "";
  auto one = [&](int preset) { return used = 1, preset; };
  if (w == "column" || w == "pillar") return one(kPresetColumn);
  if (w == "pilaster") return one(kPresetPilaster);
  if (w == "beam" || w == "lintel") return one(kPresetBeam);
  if (w == "railing" || w == "balustrade") return one(kPresetRailing);
  if (w == "roof") return one(kPresetRoof);
  if (w == "arcade") return one(kPresetArcade);
  if (w == "spiral") return used = next == "stairs" ? 2 : 1, kPresetSpiral;
  if ((w == "stairs" || w == "stair") && (next == "l" || next == "u" || next == "spiral")) return used = 2, next == "l" ? kPresetStairsL : next == "u" ? kPresetStairsU : kPresetSpiral;
  return 0;
}

namespace {

constexpr float kBig = 200.0f;      // (a preset bigger than this in any size is left out and the status says so: the kernel's 32-bit positions are only so fine - round 499's 100 m for doors, twice that for a long wall)
constexpr float kTiny = 1e-4f;      // (a part thinner than this is left out: the kernel would skip it and name it)
constexpr int kMaxPosts = 1024;     // (posts of one railing, all its sides: more are left out with a status note)
constexpr int kMaxPieces = 4000;    // (the index field of a generated id holds 12 bits)

float clamp_size(float v) { return std::max(kBlockoutMinSize, std::fabs(v)); }  // (no upper limit here: a size over kBig is refused by the generator, not held to it)
float clamp_num(float v, float lo, float hi) { return std::min(hi, std::max(lo, v)); }
int clamp_count(int v) { return std::min(kBlockoutPresetMaxCount, std::max(1, v)); }
bool all_finite(std::initializer_list<float> v) {
  for (float x : v)
    if (!finite_bits(x)) return false;
  return true;
}

Mat4 rot_y(float yaw) {
  Mat4 r = Mat4::identity();
  const float c = std::cos(yaw), s = std::sin(yaw);
  r.m[0] = c, r.m[2] = -s, r.m[8] = s, r.m[10] = c;
  return r;
}

/* The pieces being made, each placed by `base` (the preset piece's matrix in the Blockout's space) times a matrix of the preset's own space. */
struct Maker {
  uint64_t owner;
  int kind;
  const Mat4 &base;
  std::vector<blockout::Piece> &out;
  uint32_t index = 0;

  blockout::Piece start(blockout::Shape shape, uint32_t tops = 0) {
    blockout::Piece p;
    p.id = kBlockoutGeneratedBit | ((uint64_t)(0x40 | kind) << 48) | ((uint64_t)((index & 0xFFF) | ((tops & 0xF) << 12)) << 32) | (owner & 0xFFFFFFFFull);
    index++;
    p.shape = shape, p.op = blockout::Op::Add;
    return p;
  }
  bool room() const { return (int)index < kMaxPieces; }
  void box(float x0, float x1, float y0, float y1, float z0, float z1) {
    if (!room() || !(x1 - x0 > kTiny) || !(y1 - y0 > kTiny) || !(z1 - z0 > kTiny)) return;
    blockout::Piece p = start(blockout::Shape::Box);
    p.size = Vec3(x1 - x0, y1 - y0, z1 - z0);
    p.to_blockout = base * Mat4::translate(Vec3(0.5f * (x0 + x1), 0.5f * (y0 + y1), 0.5f * (z0 + z1)));
    out.push_back(std::move(p));
  }
  /* A box `sx` along its own X (turned `yaw` about Y round its middle), `sy` up, `sz` across. */
  void box_turned(float cx, float y0, float cz, float sx, float sy, float sz, float yaw) {
    if (!room() || !(sx > kTiny) || !(sy > kTiny) || !(sz > kTiny)) return;
    blockout::Piece p = start(blockout::Shape::Box);
    p.size = Vec3(sx, sy, sz);
    p.to_blockout = base * Mat4::translate(Vec3(cx, y0 + 0.5f * sy, cz)) * rot_y(yaw);
    out.push_back(std::move(p));
  }
  void cylinder(float cx, float cz, float y0, float y1, float radius, int segments) {
    if (!room() || !(radius > kTiny) || !(y1 - y0 > kTiny)) return;
    blockout::Piece p = start(blockout::Shape::Cylinder);
    p.radius = radius, p.height = y1 - y0, p.segments = std::min(256, std::max(3, segments));
    p.to_blockout = base * Mat4::translate(Vec3(cx, 0.5f * (y0 + y1), cz));
    out.push_back(std::move(p));
  }
  /* A convex solid by its corners; false (nothing made) when they make none. */
  bool hull(const std::vector<Vec3> &pts) {
    if (!room()) return false;
    const int up = blockout::convex_up_faces(pts);
    if (up < 0) return false;
    blockout::Piece p = start(blockout::Shape::Convex, (uint32_t)up);
    p.points = pts;
    p.to_blockout = base;
    out.push_back(std::move(p));
    return true;
  }
  /* A flight of round 480's stairs: `size` (width, height, run), climbing along +Z of `local`, whose origin is the middle of the flight's box. */
  void stairs(Vec3 size, int steps, bool fill, const Mat4 &local) {
    if (!room() || !(size.x > kTiny) || !(size.y > kTiny) || !(size.z > kTiny)) return;
    blockout::Piece p = start(blockout::Shape::Stairs);
    p.size = size, p.steps = steps, p.fill_under = fill;
    p.to_blockout = base * local;
    out.push_back(std::move(p));
  }
};

/* What the grid cannot show (a part under 1.5 cells wide): held at one cell, the narrowest remembered for the status. */
struct Held {
  float cell;
  float narrowest = 1e30f;
  bool any = false;
  float hold(float v) {
    if (!(cell > 0.0f) || v <= 0.0f || v >= 1.499f * cell) return v;
    narrowest = std::min(narrowest, v), any = true;
    return std::max(v, cell);
  }
  void say(const char *what, std::vector<std::string> *notes) const {
    if (!any || !notes) return;
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.3f", std::max(0.001f, std::floor(narrowest / 1.5f * 1000.0f + 0.01f) / 1000.0f));
    char cellbuf[32];
    std::snprintf(cellbuf, sizeof(cellbuf), "%.2f", cell);
    notes->push_back(std::string("its ") + what + " are finer than Field Cell " + cellbuf + " m - held at one cell; lower Field Cell to " + buf + " m to see them");
  }
};

/* ---- Column and pilaster ---- */

/* The shaft's half-width at height y: R at the plinth's top, R * taper at the capital's underside. */
struct Shaft {
  float r, taper, y0, y1;
  float at(float y) const { return r * (1.0f + (taper - 1.0f) * (y - y0) / std::max(1e-6f, y1 - y0)); }
};

bool column_pieces(Maker &mk, const BlockoutPiece &bp, bool pilaster, Held &held, std::vector<std::string> *why) {
  if (!all_finite({bp.radius, bp.height, bp.base_height, bp.capital_height, bp.base_width, bp.capital_width, bp.taper})) return why->push_back("a number of it is not finite"), false;
  const float r = clamp_size(bp.radius), H = clamp_size(bp.height);
  if (r > kBig / 2 || H > kBig) return why->push_back("its size is over 200 m"), false;
  float bh = std::min(kBig, std::max(0.0f, std::fabs(bp.base_height))), ch = std::min(kBig, std::max(0.0f, std::fabs(bp.capital_height)));
  if (bh > 0.0f) bh = held.hold(bh);
  if (ch > 0.0f) ch = held.hold(ch);
  if (bh + ch > 0.9f * H) {  // (a plinth and a capital leave at least a tenth of the height for the shaft)
    const float k = 0.9f * H / (bh + ch);
    bh *= k, ch *= k;
  }
  const float bw = std::max(2.0f * r, std::min(kBig, std::fabs(bp.base_width))), cw = std::max(2.0f * r, std::min(kBig, std::fabs(bp.capital_width)));
  const float taper = clamp_num(bp.taper, 0.05f, 2.0f);
  const Shaft sh{r, taper, bh, H - ch};
  auto zlo = [&](float half) { return pilaster ? 0.0f : -half; };  // (a pilaster stands from the wall, z = 0, out to +Z; a column is centred)
  mk.box(-0.5f * bw, 0.5f * bw, 0.0f, bh, zlo(0.5f * bw), 0.5f * bw);
  mk.box(-0.5f * cw, 0.5f * cw, H - ch, H, zlo(0.5f * cw), 0.5f * cw);
  /* The shaft reaches half way into the plinth and the capital, so no face of it lies on theirs. */
  const float ya = 0.5f * bh, yb = H - 0.5f * ch;
  const bool plain = std::fabs(taper - 1.0f) < 1e-6f;
  if (!pilaster && !bp.square) {
    if (plain) return mk.cylinder(0.0f, 0.0f, ya, yb, r, bp.segments), true;
    const int n = std::min(32, std::max(3, bp.segments));  // (a tapered round shaft is a hull of two rings: 64 corners at most)
    std::vector<Vec3> pts;
    for (float y : {ya, yb})
      for (int i = 0; i < n; i++) {
        const float a = 6.2831853f * (float)i / (float)n, rr = sh.at(y);
        pts.push_back(Vec3(rr * std::cos(a), y, rr * std::sin(a)));
      }
    if (!mk.hull(pts)) why->push_back("its shaft could not be made: only the plinth and the capital are there");
    return true;
  }
  if (plain) return mk.box(-r, r, ya, yb, zlo(r), r), true;
  std::vector<Vec3> pts;
  for (float y : {ya, yb}) {
    const float rr = sh.at(y);
    for (int i = 0; i < 4; i++) pts.push_back(Vec3((i & 1) ? rr : -rr, y, (i & 2) ? rr : (pilaster ? 0.0f : -rr)));
  }
  if (!mk.hull(pts)) why->push_back("its shaft could not be made: only the plinth and the capital are there");
  return true;
}

/* ---- Beam ---- */

bool beam_pieces(Maker &mk, const BlockoutPiece &bp, std::vector<std::string> *why) {
  if (!all_finite({bp.size.x, bp.size.y, bp.size.z})) return why->push_back("a number of it is not finite"), false;
  const float l = clamp_size(bp.size.x), d = clamp_size(bp.size.y), w = clamp_size(bp.size.z);
  if (l > kBig || d > kBig || w > kBig) return why->push_back("its size is over 200 m"), false;
  mk.box(-0.5f * l, 0.5f * l, 0.0f, d, -0.5f * w, 0.5f * w);
  return true;
}

/* ---- Railing ---- */

/* The size of a loop's newel posts. The rails at a corner share a square of side rs/2 at a square corner and reach farther at a sharp one (rs/2 over the tangent of half the
 * angle): the newel is twice that reach and a quarter rail more, taken over the loop's sharpest corner, so the rails' shared stretch lies inside it. */
float railing_newel_size(const std::vector<Vec2> &path, float rs) {
  float size = 0.0f;
  for (size_t i = 0; path.size() >= 3 && i < path.size(); i++) {
    const Vec2 p0 = path[(i + path.size() - 1) % path.size()], p1 = path[i], p2 = path[(i + 1) % path.size()];
    const float ax = p0.x - p1.x, az = p0.y - p1.y, bx = p2.x - p1.x, bz = p2.y - p1.y;
    const float cosang = (ax * bx + az * bz) / std::max(1e-12f, std::hypot(ax, az) * std::hypot(bx, bz));
    const float t = std::max(1e-3f, std::tan(0.5f * std::acos(std::max(-1.0f, std::min(1.0f, cosang)))));
    size = std::max(size, std::min(kBig, rs * std::sqrt(1.0f + 1.0f / (t * t)) + 0.5f * rs));
  }
  return size;
}

bool railing_pieces(Maker &mk, const BlockoutPiece &bp, Held &held, std::vector<std::string> *why) {
  if (!all_finite({bp.length, bp.rail_height, bp.post_spacing, bp.post_size, bp.rail_size})) return why->push_back("a number of it is not finite"), false;
  const float H = clamp_size(bp.rail_height), spacing = clamp_size(bp.post_spacing);
  float ps = clamp_size(bp.post_size), rs = clamp_size(bp.rail_size);
  const float length = clamp_size(bp.length);
  if (H > kBig || length > kBig || ps > kBig || rs > kBig) return why->push_back("its size is over 200 m"), false;
  ps = held.hold(ps), rs = held.hold(rs);
  /* The line: each side of the outline (a closed loop) when the piece carries one, else its Length along X, centred. */
  std::vector<Vec2> path;
  bool loop = false;
  if (bp.outline.size() >= 3) {
    for (const Vec2 &q : bp.outline) {
      if (!all_finite({q.x, q.y})) return why->push_back("a corner of its outline is not finite"), false;
      if (std::fabs(q.x) > kBig || std::fabs(q.y) > kBig) return why->push_back("its outline is over 200 m"), false;
      path.push_back(q);
    }
    /* A corner at its neighbour's place or in a line with its neighbours is no corner: the rails either side would end in one plane against each other. */
    for (bool went = true; went && path.size() >= 3;) {
      went = false;
      for (size_t i = 0; i < path.size() && !went; i++) {
        const Vec2 a = path[(i + path.size() - 1) % path.size()], b = path[i], c = path[(i + 1) % path.size()];
        const float span = std::hypot(c.x - a.x, c.y - a.y), step = std::hypot(b.x - a.x, b.y - a.y);
        if (step > 1e-4f && span > 1e-4f && std::fabs((c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x)) / span > 1e-3f) continue;
        path.erase(path.begin() + (std::ptrdiff_t)i);
        went = true;
      }
    }
    loop = path.size() >= 3;
    if (!loop) return why->push_back("its outline has no area"), false;
  }
  else path = {Vec2(-0.5f * length, 0.0f), Vec2(0.5f * length, 0.0f)};
  const float post_h = std::max(kTiny * 2.0f, H - 0.5f * rs);  // (the posts end inside the top rail, so no face of theirs lies in its top)
  const float low = std::min(0.1f, 0.25f * H);
  const bool bottom = bp.bottom_rail && low + rs <= H - rs;
  int posts = 0;
  const size_t sides = loop ? path.size() : 1;
  const float newel_size = loop ? railing_newel_size(path, rs) : 0.0f;
  for (size_t s = 0; s < sides; s++) {
    const Vec2 a = path[s], b = path[(s + 1) % path.size()];
    const float len = std::hypot(b.x - a.x, b.y - a.y);
    if (!(len > kTiny)) continue;
    const int n = (int)std::min((double)kBlockoutPresetMaxCount, std::ceil((double)len / (double)spacing - 1e-6) + 1.0);  // (the posts at both ends: 5 m at 1 m is 6; at most 256 a side, the ends counted)
    const float yaw = -std::atan2(b.y - a.y, b.x - a.x);  // (a rotation about Y by yaw turns +X toward -Z)
    for (int k = 0; k < n; k++) {
      if (loop && k == n - 1) continue;  // (the next side's first post)
      if (posts >= kMaxPosts) break;
      const float u = (float)k / (float)(n - 1);
      /* A corner of a loop gets a newel post standing above the top rail, so the rails' top, bottom and end faces there lie inside it, not on each other (rails ending at
       * a thin post's middle laid their tops in one plane: 28 arrangements of 500 on the exact path). */
      const bool newel = loop && k == 0;
      const float half = newel ? 0.5f * std::max(ps, newel_size) : 0.5f * ps, top = newel ? H + 0.25f * rs : post_h;
      const float px = a.x + (b.x - a.x) * u, pz = a.y + (b.y - a.y) * u;
      mk.box(px - half, px + half, 0.0f, top, pz - half, pz + half);
      posts++;
    }
    const float cx = 0.5f * (a.x + b.x), cz = 0.5f * (a.y + b.y);
    /* (Measured in the fix pass: rails run a quarter post past the posts' middles gave 36 arrangements of 500 with faces on top of each other on the exact path, against 28 with the rails
     * ending at the middles - the two rails at a corner then lay their top and bottom faces in one plane over the stretch they share. They end at the middles.) */
    mk.box_turned(cx, H - rs, cz, len, rs, rs, yaw);
    if (bottom) mk.box_turned(cx, low, cz, len, rs, rs, yaw);
  }
  if (posts >= kMaxPosts && why) why->push_back("more than 1024 posts: the rest are left out");
  return true;
}

/* ---- Roof ---- */

float roof_tan(const BlockoutPiece &p) { return std::tan(clamp_num(finite_bits(p.pitch) ? p.pitch : 30.0f, 0.0f, 85.0f) * 3.14159265358979f / 180.0f); }

bool outline_rects(const std::vector<Vec2> &o, std::vector<std::array<float, 4>> &rects, std::string &why);

/* The roof's base: the footprint grown by the overhang on every side (a, b: half of it along X and Z). Over an outline the numbers are those of its widest rectangle (its
 * long half and its short half: the ridge of the roof over it), not Length and Span, which an outline does not read. */
void roof_halves(const BlockoutPiece &p, float &a, float &b) {
  const float o = finite_bits(p.overhang) ? clamp_num(std::fabs(p.overhang), 0.0f, kBig) : 0.0f;
  a = 0.5f * clamp_size(p.length) + o, b = 0.5f * clamp_size(p.span) + o;
  if (p.outline.size() < 3) return;
  std::vector<std::array<float, 4>> rects;
  std::string why;
  if (!outline_rects(p.outline, rects, why)) return;
  float best = -1.0f;
  for (const std::array<float, 4> &r : rects) {
    const float w = r[1] - r[0], d = r[3] - r[2], shorter = std::min(w, d);
    if (shorter > best) best = shorter, a = 0.5f * std::max(w, d) + o, b = 0.5f * shorter + o;
  }
}

/* How high the roof rises over the grown base (a half along the ridge's way, b across): Gable over b, Hip over the nearer of the two, Shed over the whole b * 2. */
float roof_rise(int style, float a, float b, float tan) { return (style == kRoofShed ? 2.0f * b : style == kRoofHip ? std::min(a, b) : b) * tan; }

/* The roof over one rectangle: the base's centre (cx, cz) and halves; `swap`: the ridge runs along Z (a roof over an outline's rectangle follows its long side). Solid: one
 * hull of the base and the ridge. With a thickness: sheets of that thickness, straight down from the roof's surface, which stands `thickness` above the eave. */
bool roof_rect(Maker &mk, const BlockoutPiece &bp, float cx, float cz, float a, float b, bool swap, float t, float tan) {
  const int style = std::min(kBlockoutRoofStyles - 1, std::max(0, bp.roof_style));
  const float h = roof_rise(style, a, b, tan);
  auto P = [&](float x, float y, float z) { return swap ? Vec3(cx + z, y, cz + x) : Vec3(cx + x, y, cz + z); };
  std::vector<std::vector<Vec3>> faces;  // the surface's pieces (anticlockwise or not: only their corners count)
  if (style == kRoofGable) {
    faces.push_back({P(-a, 0, b), P(a, 0, b), P(a, h, 0), P(-a, h, 0)});
    faces.push_back({P(-a, 0, -b), P(a, 0, -b), P(a, h, 0), P(-a, h, 0)});
  }
  else if (style == kRoofShed) faces.push_back({P(-a, 0, -b), P(a, 0, -b), P(a, h, b), P(-a, h, b)});
  else {
    const float m = std::min(a, b);
    const Vec3 r1 = a >= b ? P(-(a - m), h, 0) : P(0, h, -(b - m)), r2 = a >= b ? P(a - m, h, 0) : P(0, h, b - m);
    if (a >= b) {
      faces.push_back({P(-a, 0, b), P(a, 0, b), r2, r1});
      faces.push_back({P(-a, 0, -b), P(a, 0, -b), r2, r1});
      faces.push_back({P(a, 0, -b), P(a, 0, b), r2});
      faces.push_back({P(-a, 0, -b), P(-a, 0, b), r1});
    }
    else {
      faces.push_back({P(-a, 0, -b), P(-a, 0, b), r2, r1});
      faces.push_back({P(a, 0, -b), P(a, 0, b), r2, r1});
      faces.push_back({P(-a, 0, b), P(a, 0, b), r2});
      faces.push_back({P(-a, 0, -b), P(a, 0, -b), r1});
    }
  }
  if (t > 0.0f) {  // (sheets: each surface piece with its copy `t` higher; the lower ones are the eave's bottom)
    bool any = false;
    for (const std::vector<Vec3> &f : faces) {
      std::vector<Vec3> pts = f;
      for (const Vec3 &q : f) pts.push_back(Vec3(q.x, q.y + t, q.z));
      any = mk.hull(pts) || any;
    }
    return any;
  }
  std::vector<Vec3> pts;
  for (const std::vector<Vec3> &f : faces)
    for (const Vec3 &q : f) pts.push_back(q);
  if (style == kRoofShed) pts.push_back(P(-a, 0, b)), pts.push_back(P(a, 0, b));  // (a shed's one slope has the high wall and the floor under it: the lower corners at its high end)
  return mk.hull(pts);
}

/* An outline with square corners as the rectangles a roof is made over: horizontal slabs between its distinct z, each cut by the outline's vertical sides; a slab with the same
 * x-extent as the one before it joins it. False, with the reason, when a side is slanted, the outline crosses itself or it makes more than 64. */
bool outline_rects(const std::vector<Vec2> &o, std::vector<std::array<float, 4>> &rects, std::string &why) {
  const size_t n = o.size();
  float lo = 1e30f, hi = -1e30f;
  for (const Vec2 &q : o) {
    if (!all_finite({q.x, q.y})) return why = "a corner of its outline is not finite", false;
    if (std::fabs(q.x) > kBig || std::fabs(q.y) > kBig) return why = "its outline is over 200 m", false;
    lo = std::min(lo, std::min(q.x, q.y)), hi = std::max(hi, std::max(q.x, q.y));
  }
  const float tol = std::max(1e-5f, 1e-5f * (hi - lo));
  for (size_t i = 0; i < n; i++) {
    const Vec2 a = o[i], b = o[(i + 1) % n];
    if (std::fabs(a.x - b.x) > tol && std::fabs(a.y - b.y) > tol) return why = "its outline has a slanted side: a roof over an outline needs square corners", false;
  }
  std::vector<float> zs;
  for (const Vec2 &q : o) zs.push_back(q.y);
  std::sort(zs.begin(), zs.end());
  zs.erase(std::unique(zs.begin(), zs.end(), [&](float p, float q) { return std::fabs(p - q) <= tol; }), zs.end());
  for (size_t i = 0; i + 1 < zs.size(); i++) {
    const float zm = 0.5f * (zs[i] + zs[i + 1]);
    std::vector<float> xs;
    for (size_t k = 0; k < n; k++) {
      const Vec2 a = o[k], b = o[(k + 1) % n];
      if (std::fabs(a.x - b.x) <= tol && std::min(a.y, b.y) < zm && std::max(a.y, b.y) > zm) xs.push_back(a.x);
    }
    std::sort(xs.begin(), xs.end());
    if (xs.size() % 2 != 0) return why = "its outline crosses itself", false;
    for (size_t k = 0; k + 1 < xs.size(); k += 2) {
      if (!(xs[k + 1] - xs[k] > tol)) continue;
      bool joined = false;
      for (std::array<float, 4> &r : rects)
        if (!joined && std::fabs(r[0] - xs[k]) <= tol && std::fabs(r[1] - xs[k + 1]) <= tol && std::fabs(r[3] - zs[i]) <= tol) r[3] = zs[i + 1], joined = true;
      if (!joined) rects.push_back({xs[k], xs[k + 1], zs[i], zs[i + 1]});
    }
  }
  if (rects.empty()) return why = "its outline has no area", false;
  if (rects.size() > 64) return why = "its outline makes more than 64 rectangles", false;
  return true;
}

bool roof_pieces(Maker &mk, const BlockoutPiece &bp, Held &held, std::vector<std::string> *why) {
  if (!all_finite({bp.length, bp.span, bp.pitch, bp.overhang, bp.thickness})) return why->push_back("a number of it is not finite"), false;
  const float o = clamp_num(std::fabs(bp.overhang), 0.0f, kBig);
  if (clamp_size(bp.length) > kBig || clamp_size(bp.span) > kBig || o >= kBig) return why->push_back("its size is over 200 m"), false;
  const float tan = roof_tan(bp);
  float t = clamp_num(std::fabs(bp.thickness), 0.0f, kBig);
  if (t > 0.0f) t = held.hold(t);
  if (tan <= 1e-6f && !(t > 0.0f)) return why->push_back("its Pitch and its Thickness are both 0: a flat solid roof has no height"), false;
  bool any = false;
  if (bp.outline.size() >= 3) {
    std::vector<std::array<float, 4>> rects;
    std::string reason;
    if (!outline_rects(bp.outline, rects, reason)) return why->push_back(reason), false;
    for (const std::array<float, 4> &r : rects) {
      const float w = r[1] - r[0] + 2.0f * o, d = r[3] - r[2] + 2.0f * o;
      const bool along_x = w >= d;  // (the ridge follows the rectangle's long side)
      any = roof_rect(mk, bp, 0.5f * (r[0] + r[1]), 0.5f * (r[2] + r[3]), 0.5f * std::max(w, d), 0.5f * std::min(w, d), !along_x, t, tan) || any;
    }
  }
  else {
    float a, b;
    roof_halves(bp, a, b);
    any = roof_rect(mk, bp, 0.0f, 0.0f, a, b, false, t, tan);
  }
  if (!any) why->push_back("its numbers make no roof");
  return any;
}

/* ---- Stairs with a landing ---- */

bool landing_stairs(Maker &mk, const BlockoutPiece &bp, bool u_turn, std::vector<std::string> *why) {
  if (!all_finite({bp.stair_width, bp.total_height, bp.length})) return why->push_back("a number of it is not finite"), false;
  const float w = clamp_size(bp.stair_width), H = clamp_size(bp.total_height), run = clamp_size(bp.length);
  if (w > kBig || H > kBig || run > kBig) return why->push_back("its size is over 200 m"), false;
  const int n = clamp_count(bp.steps);
  const float half = 0.5f * H;
  const bool fill = bp.fill_under;
  /* Flight 1 from the origin along +Z, then the landing (a box as wide as the flights, as high as the first flight), then flight 2 turned a quarter (L) or a half (U). Under flight 2
   * a box of its own footprint (Fill Under): the second flight stands on it as the first stands on the ground. */
  /* Every contact is an overlap of real volume (a face against a face is a plane two solids share): the landing starts one step inside flight 1's last step, the second flight
   * starts a quarter of a rise below the landing's top (its steps are that much taller, so its last tread is still at the Total Height) and the mass under it reaches into the
   * landing and, in a U, into the first flight's side. */
  const float step = run / (float)n, dip = 0.25f * half / (float)n, up2 = half - dip, high2 = half + dip, side_in = std::min(0.1f * w, 0.05f);
  mk.stairs(Vec3(w, half, run), n, fill, Mat4::translate(Vec3(0.0f, 0.5f * half, 0.5f * run)));
  if (!u_turn) {
    mk.box(-0.5f * w, 0.5f * w + (fill ? run : 0.5f * step), 0.0f, half, run - step, run + w);  // (the landing, and with Fill Under the mass under flight 2 in one box)
    mk.stairs(Vec3(w, high2, run), n, fill, Mat4::translate(Vec3(0.5f * w + 0.5f * run, up2 + 0.5f * high2, run + 0.5f * w)) * rot_y(1.5707963f));
    return true;
  }
  mk.box(-0.5f * w, 1.5f * w, 0.0f, half, run - step, run + w);
  if (fill) mk.box(0.5f * w - side_in, 1.5f * w, 0.0f, half, 0.0f, run);
  mk.stairs(Vec3(w, high2, run), n, fill, Mat4::translate(Vec3(w, up2 + 0.5f * high2, 0.5f * run)) * rot_y(3.14159265f));
  return true;
}

/* ---- Spiral stairs ---- */

bool spiral_pieces(Maker &mk, const BlockoutPiece &bp, std::vector<std::string> *why) {
  if (!all_finite({bp.radius, bp.inner_radius, bp.total_height, bp.turn})) return why->push_back("a number of it is not finite"), false;
  const float R = clamp_size(bp.radius), r = std::min(R * 0.95f, std::max(0.0f, std::fabs(bp.inner_radius))), H = clamp_size(bp.total_height);
  if (R > kBig || H > kBig) return why->push_back("its size is over 200 m"), false;
  const int n = clamp_count(bp.steps);
  /* A step is a convex block, so it turns less than a half circle: 170 degrees at most (a turn of 1440 degrees is four circles at most). */
  const float turn = clamp_num(bp.turn, -1440.0f, 1440.0f), step_deg = clamp_num(turn / (float)n, -170.0f, 170.0f), da = step_deg * 3.14159265f / 180.0f;
  if (std::fabs(turn) < 0.01f) why->push_back("its Turn is 0: the steps lie on one another, only the post is made");
  else if (std::fabs(step_deg * (float)n - turn) > 0.01f)
    why->push_back("its Turn is held to " + std::to_string((int)std::lround(step_deg * (float)n)) + " degrees: a step turns 170 degrees at most (more Steps turn further)");
  else if (std::fabs(bp.turn) > 1440.0f) why->push_back("its Turn is held to 1440 degrees");
  const float rise = H / (float)n, slab = bp.fill_under ? 0.0f : std::max(kTiny * 2.0f, 0.8f * rise);
  const float ri = 0.5f * r;  // (the steps' inner ends lie inside the post)
  mk.cylinder(0.0f, 0.0f, 0.0f, H, r, 24);
  for (int i = 0; i < n; i++) {
    const float a0 = da * (float)i, a1 = da * (float)(i + 1), top = rise * (float)(i + 1), bottom = bp.fill_under ? 0.0f : top - slab;
    /* (Measured in the fix pass: a filled step running a quarter step past the plane it shares with the next one put the two bottoms in one plane over that wedge - 36 arrangements
     * of 500 with faces on top of each other on the exact path against 1 with the steps meeting in a plane. They meet in the plane.) */
    std::vector<Vec3> pts;
    for (float y : {bottom, top})
      for (float a : {a0, a1})
        for (float rad : {ri, R}) pts.push_back(Vec3(rad * std::cos(a), y, rad * std::sin(a)));
    mk.hull(pts);
  }
  return true;
}

/* ---- Arcade ---- */

bool arcade_pieces(Maker &mk, const BlockoutPiece &bp, Held &held, std::vector<std::string> *why) {
  if (!all_finite({bp.span, bp.column_width, bp.height, bp.arch_rise, bp.depth, bp.arch_thickness})) return why->push_back("a number of it is not finite"), false;
  const int n = clamp_count(bp.count);
  const float span = clamp_size(bp.span), cw = held.hold(clamp_size(bp.column_width)), Hc = clamp_size(bp.height), dz = clamp_size(bp.depth);
  const float rise = bp.arch_rise > 0.0f ? clamp_size(bp.arch_rise) : 0.5f * span;
  const float th = held.hold(std::min(clamp_size(bp.arch_thickness), cw * 0.99f));
  const float total = (float)n * span + (float)(n + 1) * cw;
  if (total > kBig || Hc > kBig || dz > kBig || rise > kBig) return why->push_back("its size is over 200 m"), false;
  const float pitch = span + cw, x0 = -0.5f * total;
  for (int k = 0; k <= n; k++) mk.box(x0 + (float)k * pitch, x0 + (float)k * pitch + cw, 0.0f, Hc, -0.5f * dz, 0.5f * dz);
  /* The ring: the clear arch is the half ellipse a x rise; its outer curve is that stretched so the ring is `th` wide at the springing (and `th * rise / a` at the crown). */
  /* (The fix pass tried the arch's feet inside the columns - a hair wider, a little lower: 5 arrangements of 500 with faces on top of each other on the exact path against 3, and the
   * 250 with small faces unchanged: those are the arcades standing in the room, whose columns reach through its walls. The feet stay on the columns' tops.) */
  const float a0 = 0.5f * span, m = 0.0f, e = 0.0f;
  const float a = a0 + m, A = a + th, rho = a / A;
  for (int k = 0; k < n; k++) {
    if (!mk.room()) break;
    blockout::Piece p = mk.start(blockout::Shape::Arch);
    p.radius = 1.0f, p.inner_radius = rho, p.angle = 180.0f, p.height = dz, p.segments = std::min(64, std::max(4, bp.segments / 2));
    Mat4 at = Mat4::translate(Vec3(x0 + cw + (float)k * pitch + a0, Hc - e, 0.0f));
    at.m[0] = A, at.m[5] = rise / rho;
    p.to_blockout = mk.base * at;
    mk.out.push_back(std::move(p));
  }
  return true;
}

}  // namespace

bool blockout_preset_pieces(const BlockoutPiece &bp, uint64_t owner, const Mat4 &to_blockout, std::vector<blockout::Piece> &out, float grid_cell, std::vector<std::string> *notes) {
  std::vector<std::string> local;
  std::vector<std::string> &why = notes ? *notes : local;
  if (!bp.is_preset()) return false;
  if (owner > 0xFFFFFFFFull) return why.push_back("its object id is too large"), false;
  const size_t first = out.size(), notes_first = why.size();
  Maker mk{owner, bp.preset, to_blockout, out};
  Held held{grid_cell};
  bool ok = false;
  switch (bp.preset) {
    case kPresetColumn: ok = column_pieces(mk, bp, false, held, &why); break;
    case kPresetPilaster: ok = column_pieces(mk, bp, true, held, &why); break;
    case kPresetBeam: ok = beam_pieces(mk, bp, &why); break;
    case kPresetRailing: ok = railing_pieces(mk, bp, held, &why); break;
    case kPresetRoof: ok = roof_pieces(mk, bp, held, &why); break;
    case kPresetStairsL: ok = landing_stairs(mk, bp, false, &why); break;
    case kPresetStairsU: ok = landing_stairs(mk, bp, true, &why); break;
    case kPresetSpiral: ok = spiral_pieces(mk, bp, &why); break;
    case kPresetArcade: ok = arcade_pieces(mk, bp, held, &why); break;
    default: break;
  }
  if (out.size() == first) ok = false;
  if (!ok) {
    out.resize(first);
    if (why.size() == notes_first) why.push_back("its numbers make nothing");
    return false;
  }
  const char *what = bp.preset == kPresetRailing ? "posts and rails" : bp.preset == kPresetRoof ? "sheets" : bp.preset == kPresetArcade ? "columns and arches" : "plinth and capital";
  held.say(what, &why);
  return true;
}

int blockout_preset_face_class(const blockout::Piece &piece, uint32_t side) {
  const int kind = (int)((piece.id >> 48) & 0x3F);
  const uint32_t tops = (uint32_t)((piece.id >> 44) & 0xF);
  return kind == kPresetRoof && side < tops ? kClassRoof : kClassSolid;
}

void blockout_preset_defaults(BlockoutPiece &p, int preset) {
  p.preset = preset;
  p.shape = 0, p.operation = 0, p.room = false;
  switch (preset) {
    case kPresetColumn: p.radius = 0.25f, p.height = 3.0f, p.segments = 24, p.square = false; break;
    case kPresetPilaster: p.radius = 0.25f, p.height = 3.0f, p.square = true; break;
    case kPresetBeam: p.size = {4.0f, 0.3f, 0.3f}; break;
    case kPresetRailing: p.length = 5.0f, p.rail_height = 1.0f, p.post_spacing = 1.0f, p.post_size = 0.08f, p.rail_size = 0.05f, p.bottom_rail = true; break;
    case kPresetRoof: p.length = 6.0f, p.span = 4.0f, p.pitch = 30.0f, p.overhang = 0.3f, p.thickness = 0.0f, p.roof_style = kRoofGable; break;
    case kPresetStairsL:
    case kPresetStairsU: p.stair_width = 1.0f, p.total_height = 3.0f, p.length = 2.0f, p.steps = 8, p.fill_under = true; break;
    case kPresetSpiral: p.radius = 1.0f, p.inner_radius = 0.1f, p.total_height = 3.0f, p.steps = 16, p.turn = 270.0f, p.fill_under = true; break;
    case kPresetArcade: p.count = 4, p.span = 2.5f, p.column_width = 0.4f, p.height = 3.0f, p.arch_rise = 0.0f, p.depth = 0.5f, p.arch_thickness = 0.3f, p.segments = 24; break;
    default: break;
  }
}

void blockout_preset_numbers(BlockoutPiece &p, const std::vector<float> &n) {
  auto at = [&](size_t i, float &f) { if (n.size() > i) f = n[i]; };
  switch (p.preset) {
    case kPresetColumn:
    case kPresetPilaster: at(0, p.radius), at(1, p.height); break;
    case kPresetBeam: at(0, p.size.x), at(1, p.size.y), at(2, p.size.z); break;
    case kPresetRailing: at(0, p.length), at(1, p.rail_height); break;
    case kPresetRoof: at(0, p.length), at(1, p.span), at(2, p.pitch); break;
    case kPresetStairsL:
    case kPresetStairsU:
      at(0, p.stair_width), at(1, p.total_height), at(2, p.length);
      if (n.size() > 3 && finite_bits(n[3])) p.steps = clamp_count((int)std::lround(std::min(1e6f, std::max(-1e6f, n[3]))));
      break;
    case kPresetSpiral:
      at(0, p.radius), at(1, p.inner_radius), at(2, p.total_height);
      if (n.size() > 3 && finite_bits(n[3])) p.steps = clamp_count((int)std::lround(std::min(1e6f, std::max(-1e6f, n[3]))));
      at(4, p.turn);
      break;
    case kPresetArcade:
      if (n.size() > 0 && finite_bits(n[0])) p.count = clamp_count((int)std::lround(std::min(1e6f, std::max(-1e6f, n[0]))));
      at(1, p.span), at(2, p.height);
      break;
    default: break;
  }
}

bool blockout_preset_bounds(const BlockoutPiece &p, Vec3 &lo, Vec3 &hi) {
  if (!p.is_preset()) return false;
  auto s = [](float v) { return finite_bits(v) ? std::min(kBig, std::fabs(v)) : 0.0f; };
  switch (p.preset) {
    case kPresetColumn: {
      const float w = std::max(s(p.radius) * 2.0f, std::max(s(p.base_width), s(p.capital_width))) * 0.5f;
      lo = Vec3(-w, 0.0f, -w), hi = Vec3(w, s(p.height), w);
      break;
    }
    case kPresetPilaster: {
      const float w = std::max(s(p.radius) * 2.0f, std::max(s(p.base_width), s(p.capital_width))) * 0.5f;
      lo = Vec3(-w, 0.0f, 0.0f), hi = Vec3(w, s(p.height), w);
      break;
    }
    case kPresetBeam: lo = Vec3(-0.5f * s(p.size.x), 0.0f, -0.5f * s(p.size.z)), hi = Vec3(0.5f * s(p.size.x), s(p.size.y), 0.5f * s(p.size.z)); break;
    case kPresetRailing: {
      float x0 = -0.5f * s(p.length), x1 = 0.5f * s(p.length), z0 = 0.0f, z1 = 0.0f;
      if (p.outline.size() >= 3) {
        x0 = z0 = 1e30f, x1 = z1 = -1e30f;
        for (const Vec2 &q : p.outline)
          if (finite_bits(q.x) && finite_bits(q.y)) x0 = std::min(x0, q.x), x1 = std::max(x1, q.x), z0 = std::min(z0, q.y), z1 = std::max(z1, q.y);
        if (x0 > x1) x0 = x1 = z0 = z1 = 0.0f;
      }
      const bool loop = p.outline.size() >= 3;
      const float e = 0.5f * std::max(s(p.post_size), loop ? railing_newel_size(p.outline, s(p.rail_size)) : 0.0f);
      lo = Vec3(x0 - e, 0.0f, z0 - e), hi = Vec3(x1 + e, s(p.rail_height) + (loop ? 0.25f * s(p.rail_size) : 0.0f), z1 + e);
      break;
    }
    case kPresetRoof: {
      float a, b;
      roof_halves(p, a, b);
      if (p.outline.size() >= 3) {
        float x0 = 1e30f, x1 = -1e30f, z0 = 1e30f, z1 = -1e30f;
        for (const Vec2 &q : p.outline)
          if (finite_bits(q.x) && finite_bits(q.y)) x0 = std::min(x0, q.x), x1 = std::max(x1, q.x), z0 = std::min(z0, q.y), z1 = std::max(z1, q.y);
        const float o = finite_bits(p.overhang) ? s(p.overhang) : 0.0f;
        if (x0 > x1) x0 = x1 = z0 = z1 = 0.0f;
        lo = Vec3(x0 - o, 0.0f, z0 - o), hi = Vec3(x1 + o, std::max(s(blockout_roof_ridge(p)), 0.0f) + s(p.thickness), z1 + o);
      }
      else lo = Vec3(-a, 0.0f, -b), hi = Vec3(a, s(blockout_roof_ridge(p)) + s(p.thickness), b);
      break;
    }
    case kPresetStairsL:
    case kPresetStairsU: {
      const float w = s(p.stair_width), run = s(p.length);
      lo = Vec3(-0.5f * w, 0.0f, 0.0f);
      hi = p.preset == kPresetStairsU ? Vec3(1.5f * w, s(p.total_height), run + w) : Vec3(0.5f * w + run, s(p.total_height), run + w);
      break;
    }
    case kPresetSpiral: {
      const float r = s(p.radius);
      lo = Vec3(-r, 0.0f, -r), hi = Vec3(r, s(p.total_height), r);
      break;
    }
    case kPresetArcade: {
      const float n = (float)clamp_count(p.count), total = n * s(p.span) + (n + 1.0f) * s(p.column_width);
      lo = Vec3(-0.5f * total, 0.0f, -0.5f * s(p.depth)), hi = Vec3(0.5f * total, s(p.height) + (p.arch_rise > 0.0f ? s(p.arch_rise) : 0.5f * s(p.span)) + s(p.arch_thickness), 0.5f * s(p.depth));
      break;
    }
    default: return false;
  }
  return true;
}

float blockout_roof_ridge(const BlockoutPiece &p) {
  float a, b;
  roof_halves(p, a, b);
  if (!all_finite({a, b, p.pitch})) return 0.0f;
  return roof_rise(std::min(kBlockoutRoofStyles - 1, std::max(0, p.roof_style)), a, b, roof_tan(p));
}

void blockout_roof_set_ridge(BlockoutPiece &p, float ridge) {
  float a, b;
  roof_halves(p, a, b);
  const int style = std::min(kBlockoutRoofStyles - 1, std::max(0, p.roof_style));
  const float run = style == kRoofShed ? 2.0f * b : style == kRoofHip ? std::min(a, b) : b;
  if (!finite_bits(ridge) || !(run > 0.0f)) return;
  p.pitch = clamp_num(std::atan2(std::max(0.0f, ridge), run) * 180.0f / 3.14159265358979f, 0.0f, 85.0f);
}

/* ---- `blockout set <word>` for a preset's numbers ---- */

namespace {

struct SetKey {
  const char *word;
  char kind;  // 'f' a size, 'p' a number with its own range, 'i' a count, 'b' a switch
  float BlockoutPiece::*f;
  int BlockoutPiece::*i;
  bool BlockoutPiece::*b;
  float lo, hi;
  unsigned presets;  // the presets that read it
};

constexpr unsigned bit(int preset) { return 1u << preset; }
constexpr unsigned kColumns = bit(kPresetColumn) | bit(kPresetPilaster);
constexpr unsigned kStairsLU = bit(kPresetStairsL) | bit(kPresetStairsU);

const SetKey kSetKeys[] = {
    {"radius", 'f', &BlockoutPiece::radius, nullptr, nullptr, kBlockoutMinSize, kBlockoutMaxSize, kColumns | bit(kPresetSpiral)},
    {"height", 'f', &BlockoutPiece::height, nullptr, nullptr, kBlockoutMinSize, kBlockoutMaxSize, kColumns | bit(kPresetArcade)},
    {"segments", 'i', nullptr, &BlockoutPiece::segments, nullptr, 3, 256, bit(kPresetColumn) | bit(kPresetArcade)},
    {"shaft", 'b', nullptr, nullptr, &BlockoutPiece::square, 0, 1, bit(kPresetColumn)},
    {"baseheight", 'f', &BlockoutPiece::base_height, nullptr, nullptr, 0.0f, kBlockoutMaxSize, kColumns},
    {"capitalheight", 'f', &BlockoutPiece::capital_height, nullptr, nullptr, 0.0f, kBlockoutMaxSize, kColumns},
    {"basewidth", 'f', &BlockoutPiece::base_width, nullptr, nullptr, kBlockoutMinSize, kBlockoutMaxSize, kColumns},
    {"capitalwidth", 'f', &BlockoutPiece::capital_width, nullptr, nullptr, kBlockoutMinSize, kBlockoutMaxSize, kColumns},
    {"taper", 'p', &BlockoutPiece::taper, nullptr, nullptr, 0.05f, 2.0f, kColumns},
    {"length", 'f', &BlockoutPiece::length, nullptr, nullptr, kBlockoutMinSize, kBlockoutMaxSize, bit(kPresetRailing) | bit(kPresetRoof) | kStairsLU},
    {"spacing", 'f', &BlockoutPiece::post_spacing, nullptr, nullptr, kBlockoutMinSize, kBlockoutMaxSize, bit(kPresetRailing)},
    {"postsize", 'f', &BlockoutPiece::post_size, nullptr, nullptr, kBlockoutMinSize, kBlockoutMaxSize, bit(kPresetRailing)},
    {"railsize", 'f', &BlockoutPiece::rail_size, nullptr, nullptr, kBlockoutMinSize, kBlockoutMaxSize, bit(kPresetRailing)},
    {"railheight", 'f', &BlockoutPiece::rail_height, nullptr, nullptr, kBlockoutMinSize, kBlockoutMaxSize, bit(kPresetRailing)},
    {"bottomrail", 'b', nullptr, nullptr, &BlockoutPiece::bottom_rail, 0, 1, bit(kPresetRailing)},
    {"span", 'f', &BlockoutPiece::span, nullptr, nullptr, kBlockoutMinSize, kBlockoutMaxSize, bit(kPresetRoof) | bit(kPresetArcade)},
    {"pitch", 'p', &BlockoutPiece::pitch, nullptr, nullptr, 0.0f, 85.0f, bit(kPresetRoof)},
    {"overhang", 'p', &BlockoutPiece::overhang, nullptr, nullptr, 0.0f, kBlockoutMaxSize, bit(kPresetRoof)},
    {"thickness", 'p', &BlockoutPiece::thickness, nullptr, nullptr, 0.0f, kBlockoutMaxSize, bit(kPresetRoof)},
    {"totalheight", 'f', &BlockoutPiece::total_height, nullptr, nullptr, kBlockoutMinSize, kBlockoutMaxSize, kStairsLU | bit(kPresetSpiral)},
    {"flightwidth", 'f', &BlockoutPiece::stair_width, nullptr, nullptr, kBlockoutMinSize, kBlockoutMaxSize, kStairsLU},
    {"steps", 'i', nullptr, &BlockoutPiece::steps, nullptr, 1, 256, kStairsLU | bit(kPresetSpiral)},
    {"fill", 'b', nullptr, nullptr, &BlockoutPiece::fill_under, 0, 1, kStairsLU | bit(kPresetSpiral)},
    {"innerradius", 'f', &BlockoutPiece::inner_radius, nullptr, nullptr, 0.0f, kBlockoutMaxSize, bit(kPresetSpiral)},
    {"turn", 'p', &BlockoutPiece::turn, nullptr, nullptr, -1440.0f, 1440.0f, bit(kPresetSpiral)},
    {"count", 'i', nullptr, &BlockoutPiece::count, nullptr, 1, 256, bit(kPresetArcade)},
    {"columnwidth", 'f', &BlockoutPiece::column_width, nullptr, nullptr, kBlockoutMinSize, kBlockoutMaxSize, bit(kPresetArcade)},
    {"archrise", 'p', &BlockoutPiece::arch_rise, nullptr, nullptr, 0.0f, kBlockoutMaxSize, bit(kPresetArcade)},
    {"archthickness", 'f', &BlockoutPiece::arch_thickness, nullptr, nullptr, kBlockoutMinSize, kBlockoutMaxSize, bit(kPresetArcade)},
    {"depth", 'f', &BlockoutPiece::depth, nullptr, nullptr, kBlockoutMinSize, kBlockoutMaxSize, bit(kPresetArcade)},
};

bool number_word(const std::string &w, float &out) {
  char *end = nullptr;
  const double v = std::strtod(w.c_str(), &end);
  if (w.empty() || end == w.c_str() || *end != '\0' || !std::isfinite(v)) return false;
  out = (float)std::max(-1e30, std::min(1e30, v));
  return true;
}

}  // namespace

const char *blockout_preset_set_words() {
  return "shaft round|square | baseheight|capitalheight|basewidth|capitalwidth <m> | taper <share> | length <m> | spacing|postsize|railsize|railheight <m> | bottomrail on|off | "
         "roofstyle gable|hip|shed | pitch <degrees> | ridge <m> | overhang|thickness <m> | span <m> | totalheight <m> | flightwidth <m> | steps <n> | fill on|off | innerradius <m> | "
         "turn <degrees> | count <n> | columnwidth|archrise|archthickness|depth <m> | radius|height <m> | segments <n>";
}

bool blockout_preset_set(BlockoutPiece &p, const std::string &key, const std::vector<std::string> &values, bool &ok) {
  ok = false;
  const std::string k = lower(key);
  auto reads = [&](unsigned mask) { return p.is_preset() && (mask & bit(p.preset)) != 0; };
  if (k == "roofstyle") {
    if (!reads(bit(kPresetRoof))) return true;
    const std::string v = values.empty() ? "" : lower(values[0]);
    const int style = v == "gable" ? kRoofGable : v == "hip" ? kRoofHip : v == "shed" ? kRoofShed : -1;
    if (style < 0) return true;
    p.roof_style = style, ok = true;
    return true;
  }
  if (k == "ridge") {
    float v = 0.0f;
    if (!reads(bit(kPresetRoof)) || values.empty() || !number_word(values[0], v)) return true;
    blockout_roof_set_ridge(p, v), ok = true;
    return true;
  }
  for (const SetKey &s : kSetKeys) {
    if (k != s.word) continue;
    if (!reads(s.presets)) return true;
    if (s.kind == 'b') {
      const std::string v = values.empty() ? "" : lower(values[0]);
      const bool round = k == "shaft" && v == "round", square = k == "shaft" && v == "square";
      const bool on = v == "on" || v == "true" || v == "1" || v == "yes", off = v == "off" || v == "false" || v == "0" || v == "no";
      if (k == "shaft" ? !(round || square) : !(on || off)) return true;
      p.*s.b = k == "shaft" ? square : on, ok = true;
      return true;
    }
    float v = 0.0f;
    if (values.empty() || !number_word(values[0], v)) return true;
    if (s.kind == 'i') p.*s.i = (int)std::lround(std::min(s.hi, std::max(s.lo, v)));
    else p.*s.f = std::min(s.hi, std::max(s.lo, v));
    ok = true;
    return true;
  }
  return false;
}

}  // namespace bl
